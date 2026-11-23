"""
DYM Cyber - Backend IoT (Python Flask)
=======================================
Flujo ETL + API REST para el Sistema IoT de Monitoreo de Velocidad.

  ESP32 (Wokwi) --MQTT--> Broker HiveMQ --MQTT--> [ESTE BACKEND] --REST--> Dashboard
                                                     |
                                                  SQLite

ETL:
  E (Extract)   : suscripción MQTT a dymcyber/banda01/# (velocidad, estado, alertas)
                  + endpoint REST POST /api/mediciones (ingesta alternativa por HTTP)
  T (Transform) : parseo JSON, validación de rangos, clasificación, sello de tiempo
  L (Load)      : inserción en SQLite (tablas mediciones, alertas, estados, mantenciones)

API REST (cliente-servidor):
  GET  /api/estado                 último estado del equipo
  GET  /api/mediciones?limit=50    últimas mediciones
  POST /api/mediciones             ingesta HTTP de una medición (JSON)
  GET  /api/alertas?limit=50       últimas alertas
  GET  /api/resumen?periodo=dia    resumen dia | semana | mes (HU-06)
  POST /api/config                 {"nominal":1.05} o {"umbral":8} -> MQTT -> ESP32 (HU-12)
  GET  /api/mantenciones           listado de mantenciones (HU-07)
  POST /api/mantenciones           registrar mantención
  GET  /api/export.csv             exportar mediciones a CSV (HU-04)
  GET  /                           dashboard web
"""

import csv
import io
import json
import os
import sqlite3
import threading
from datetime import datetime, timedelta

from flask import Flask, Response, g, jsonify, render_template, request

try:
    import paho.mqtt.client as mqtt
except ImportError:  # el backend funciona igual solo con REST
    mqtt = None

# ---------------------------------------------------------------- config
MQTT_BROKER = os.getenv("MQTT_BROKER", "broker.hivemq.com")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
BASE_TOPIC = os.getenv("MQTT_BASE_TOPIC", "dymcyber/banda01")
DB_PATH = os.getenv("DB_PATH", os.path.join(os.path.dirname(__file__), "dymcyber.db"))

VEL_MIN, VEL_MAX = 0.80, 1.20

app = Flask(__name__)
db_lock = threading.Lock()
mqtt_client = None


# ---------------------------------------------------------------- base de datos
SCHEMA = """
CREATE TABLE IF NOT EXISTS mediciones (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    id_medicion TEXT, sesion INTEGER, medicion INTEGER,
    nominal REAL, actual REAL, desviacion REAL, umbral REAL,
    estado TEXT, origen TEXT, recibido_en TEXT
);
CREATE TABLE IF NOT EXISTS alertas (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    id_medicion TEXT, sesion INTEGER, tipo TEXT,
    actual REAL, desviacion REAL, umbral REAL, recibido_en TEXT
);
CREATE TABLE IF NOT EXISTS estados (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    estado TEXT, sesion INTEGER, nominal REAL, umbral REAL, recibido_en TEXT
);
CREATE TABLE IF NOT EXISTS mantenciones (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    descripcion TEXT, responsable TEXT, fecha TEXT
);
"""


def conectar():
    con = sqlite3.connect(DB_PATH, check_same_thread=False)
    con.row_factory = sqlite3.Row
    return con


def init_db():
    with conectar() as con:
        con.executescript(SCHEMA)


def ahora():
    return datetime.now().isoformat(timespec="seconds")


def filas(rows):
    return [dict(r) for r in rows]


# ---------------------------------------------------------------- ETL: Transform + Load
class DatoInvalido(ValueError):
    pass


def transformar_medicion(d):
    """Valida y normaliza una medición (Transform)."""
    try:
        actual = float(d["actual"])
        nominal = float(d.get("nominal", 1.0))
        umbral = float(d.get("umbral", 5.0))
    except (KeyError, TypeError, ValueError):
        raise DatoInvalido("faltan campos numéricos: actual/nominal/umbral")
    if not (VEL_MIN <= actual <= VEL_MAX):
        raise DatoInvalido(f"velocidad fuera de rango: {actual}")
    if nominal <= 0:
        raise DatoInvalido("nominal debe ser > 0")

    desviacion = abs(actual - nominal) / nominal * 100.0  # se recalcula en servidor
    estado = "NORMAL" if desviacion <= umbral else "ALERTA"
    sesion = int(d.get("sesion", 0))
    medicion = int(d.get("medicion", 0))
    return {
        "id_medicion": d.get("id") or f"S{sesion}-M{medicion}",
        "sesion": sesion,
        "medicion": medicion,
        "nominal": round(nominal, 3),
        "actual": round(actual, 3),
        "desviacion": round(desviacion, 2),
        "umbral": round(umbral, 2),
        "estado": estado,
    }


def cargar_medicion(m, origen):
    with db_lock, conectar() as con:
        con.execute(
            "INSERT INTO mediciones (id_medicion,sesion,medicion,nominal,actual,desviacion,"
            "umbral,estado,origen,recibido_en) VALUES (?,?,?,?,?,?,?,?,?,?)",
            (m["id_medicion"], m["sesion"], m["medicion"], m["nominal"], m["actual"],
             m["desviacion"], m["umbral"], m["estado"], origen, ahora()),
        )


def cargar_alerta(d):
    with db_lock, conectar() as con:
        con.execute(
            "INSERT INTO alertas (id_medicion,sesion,tipo,actual,desviacion,umbral,recibido_en)"
            " VALUES (?,?,?,?,?,?,?)",
            (d.get("id"), d.get("sesion"), d.get("tipo", "DESCONOCIDA"), d.get("actual"),
             d.get("desviacion"), d.get("umbral"), ahora()),
        )


def cargar_estado(d):
    with db_lock, conectar() as con:
        con.execute(
            "INSERT INTO estados (estado,sesion,nominal,umbral,recibido_en) VALUES (?,?,?,?,?)",
            (d.get("estado"), d.get("sesion"), d.get("nominal"), d.get("umbral"), ahora()),
        )


def procesar_mensaje(topic, payload):
    """Punto de entrada del ETL para mensajes MQTT (Extract -> Transform -> Load)."""
    sub = topic.rsplit("/", 1)[-1]
    if sub not in ("velocidad", "alertas", "estado"):
        return  # latido, config u otros: no se almacenan
    try:
        d = json.loads(payload)
        if not isinstance(d, dict):
            raise ValueError
    except ValueError:
        print(f"[ETL] JSON inválido en {topic}: {payload!r}")
        return
    try:
        if sub == "velocidad":
            cargar_medicion(transformar_medicion(d), "MQTT")
        elif sub == "alertas":
            cargar_alerta(d)
        elif sub == "estado":
            cargar_estado(d)
        print(f"[ETL] {sub} OK -> {d.get('id', d.get('estado'))}")
    except DatoInvalido as e:
        print(f"[ETL] descartado ({e}): {payload}")


# ---------------------------------------------------------------- MQTT (Extract)
def iniciar_mqtt():
    global mqtt_client
    if mqtt is None:
        print("[MQTT] paho-mqtt no instalado: solo modo REST")
        return

    def on_connect(client, userdata, flags, rc, properties=None):
        print(f"[MQTT] conectado a {MQTT_BROKER} (rc={rc}); suscrito a {BASE_TOPIC}/#")
        client.subscribe(f"{BASE_TOPIC}/#", qos=1)

    def on_message(client, userdata, msg):
        procesar_mensaje(msg.topic, msg.payload.decode("utf-8", "replace"))

    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"dymcyber-backend-{os.getpid()}")
    except AttributeError:  # paho-mqtt 1.x
        client = mqtt.Client(client_id=f"dymcyber-backend-{os.getpid()}")
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect_async(MQTT_BROKER, MQTT_PORT, keepalive=30)
    client.loop_start()
    mqtt_client = client


# ---------------------------------------------------------------- API REST
@app.get("/")
def dashboard():
    return render_template("dashboard.html", topic=BASE_TOPIC, broker=MQTT_BROKER)


@app.get("/api/estado")
def api_estado():
    con = conectar()
    est = con.execute("SELECT * FROM estados ORDER BY id DESC LIMIT 1").fetchone()
    med = con.execute("SELECT * FROM mediciones ORDER BY id DESC LIMIT 1").fetchone()
    total = con.execute("SELECT COUNT(*) FROM mediciones").fetchone()[0]
    alertas = con.execute("SELECT COUNT(*) FROM alertas").fetchone()[0]
    return jsonify({
        "estado": dict(est) if est else None,
        "ultima_medicion": dict(med) if med else None,
        "total_mediciones": total,
        "total_alertas": alertas,
        "mqtt_conectado": bool(mqtt_client and mqtt_client.is_connected()),
    })


@app.get("/api/mediciones")
def api_mediciones():
    limit = min(int(request.args.get("limit", 50)), 1000)
    rows = conectar().execute(
        "SELECT * FROM mediciones ORDER BY id DESC LIMIT ?", (limit,)).fetchall()
    return jsonify(filas(rows)[::-1])


@app.post("/api/mediciones")
def api_crear_medicion():
    d = request.get_json(silent=True)
    if not d:
        return jsonify(error="se esperaba JSON"), 400
    try:
        m = transformar_medicion(d)
    except DatoInvalido as e:
        return jsonify(error=str(e)), 422
    cargar_medicion(m, "HTTP")
    if m["estado"] == "ALERTA":
        cargar_alerta({"id": m["id_medicion"], "sesion": m["sesion"],
                       "tipo": "SUBVELOCIDAD" if m["actual"] < m["nominal"] else "SOBREVELOCIDAD",
                       "actual": m["actual"], "desviacion": m["desviacion"], "umbral": m["umbral"]})
    return jsonify(m), 201


@app.get("/api/alertas")
def api_alertas():
    limit = min(int(request.args.get("limit", 50)), 1000)
    rows = conectar().execute(
        "SELECT * FROM alertas ORDER BY id DESC LIMIT ?", (limit,)).fetchall()
    return jsonify(filas(rows))


@app.get("/api/resumen")
def api_resumen():
    periodo = request.args.get("periodo", "dia")
    dias = {"dia": 1, "semana": 7, "mes": 30}.get(periodo)
    if dias is None:
        return jsonify(error="periodo debe ser dia, semana o mes"), 400
    desde = (datetime.now() - timedelta(days=dias)).isoformat(timespec="seconds")
    con = conectar()
    r = con.execute(
        "SELECT COUNT(*) n, AVG(actual) prom, MIN(actual) minv, MAX(actual) maxv, "
        "AVG(desviacion) desv_prom, SUM(estado='ALERTA') n_alerta "
        "FROM mediciones WHERE recibido_en >= ?", (desde,)).fetchone()
    tipos = con.execute(
        "SELECT tipo, COUNT(*) n FROM alertas WHERE recibido_en >= ? GROUP BY tipo",
        (desde,)).fetchall()
    n = r["n"] or 0
    return jsonify({
        "periodo": periodo, "desde": desde, "mediciones": n,
        "velocidad_promedio": round(r["prom"], 3) if n else None,
        "velocidad_min": r["minv"], "velocidad_max": r["maxv"],
        "desviacion_promedio": round(r["desv_prom"], 2) if n else None,
        "porcentaje_normal": round(100 * (n - (r["n_alerta"] or 0)) / n, 1) if n else None,
        "alertas_por_tipo": {t["tipo"]: t["n"] for t in tipos},
    })


@app.post("/api/config")
def api_config():
    d = request.get_json(silent=True) or {}
    comandos = []
    if "nominal" in d:
        v = float(d["nominal"])
        if not VEL_MIN <= v <= VEL_MAX:
            return jsonify(error="nominal fuera de rango 0.80-1.20"), 422
        comandos.append(f"N={v:.2f}")
    if "umbral" in d:
        v = float(d["umbral"])
        if not 0 < v <= 20:
            return jsonify(error="umbral fuera de rango 0-20"), 422
        comandos.append(f"U={v:g}")
    if not comandos:
        return jsonify(error="enviar 'nominal' y/o 'umbral'"), 400
    if not (mqtt_client and mqtt_client.is_connected()):
        return jsonify(error="backend sin conexión MQTT"), 503
    for c in comandos:
        mqtt_client.publish(f"{BASE_TOPIC}/config", c, qos=1)
    return jsonify(enviado=comandos, topic=f"{BASE_TOPIC}/config")


@app.get("/api/mantenciones")
def api_mantenciones():
    rows = conectar().execute("SELECT * FROM mantenciones ORDER BY id DESC").fetchall()
    return jsonify(filas(rows))


@app.post("/api/mantenciones")
def api_crear_mantencion():
    d = request.get_json(silent=True) or {}
    desc = (d.get("descripcion") or "").strip()
    if not desc:
        return jsonify(error="descripcion requerida"), 400
    with db_lock, conectar() as con:
        cur = con.execute(
            "INSERT INTO mantenciones (descripcion,responsable,fecha) VALUES (?,?,?)",
            (desc, d.get("responsable", ""), ahora()))
    return jsonify(id=cur.lastrowid, descripcion=desc), 201


@app.get("/api/export.csv")
def api_export():
    rows = conectar().execute("SELECT * FROM mediciones ORDER BY id").fetchall()
    buf = io.StringIO()
    w = csv.writer(buf)
    if rows:
        w.writerow(rows[0].keys())
        w.writerows([tuple(r) for r in rows])
    return Response(buf.getvalue(), mimetype="text/csv",
                    headers={"Content-Disposition": "attachment; filename=mediciones_dymcyber.csv"})


init_db()

if __name__ == "__main__":
    iniciar_mqtt()
    app.run(host="0.0.0.0", port=int(os.getenv("PORT", "5000")), debug=False)

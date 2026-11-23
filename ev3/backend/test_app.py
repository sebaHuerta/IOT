"""Pruebas automáticas del backend (se ejecutan en GitHub Actions)."""
import os
import tempfile

os.environ["DB_PATH"] = os.path.join(tempfile.mkdtemp(), "test.db")

import app as backend  # noqa: E402

backend.init_db()
cliente = backend.app.test_client()


def test_etl_mqtt_medicion_normal():
    backend.procesar_mensaje("dymcyber/banda01/velocidad",
        '{"id":"S1-M1","sesion":1,"medicion":1,"nominal":1.0,"actual":1.02,"umbral":5}')
    ult = cliente.get("/api/mediciones?limit=1").get_json()[-1]
    assert ult["id_medicion"] == "S1-M1"
    assert ult["estado"] == "NORMAL"
    assert ult["desviacion"] == 2.0


def test_etl_descarta_medicion_invalida():
    antes = cliente.get("/api/estado").get_json()["total_mediciones"]
    backend.procesar_mensaje("dymcyber/banda01/velocidad", '{"actual": 5.0}')   # fuera de rango
    backend.procesar_mensaje("dymcyber/banda01/velocidad", "no es json")
    backend.procesar_mensaje("dymcyber/banda01/latido", "42")                   # se ignora
    assert cliente.get("/api/estado").get_json()["total_mediciones"] == antes


def test_rest_post_medicion_con_alerta():
    r = cliente.post("/api/mediciones", json={"id": "S1-M2", "sesion": 1, "medicion": 2,
                                              "nominal": 1.0, "actual": 0.85, "umbral": 5})
    assert r.status_code == 201
    assert r.get_json()["estado"] == "ALERTA"
    assert cliente.get("/api/alertas").get_json()[0]["tipo"] == "SUBVELOCIDAD"


def test_rest_rechaza_datos_invalidos():
    assert cliente.post("/api/mediciones", json={"actual": 2}).status_code == 422
    assert cliente.post("/api/mediciones", data="x").status_code == 400


def test_estado_y_alerta_mqtt():
    backend.procesar_mensaje("dymcyber/banda01/estado",
        '{"equipo":"banda01","estado":"ACTIVO","sesion":1,"nominal":1.0,"umbral":5}')
    backend.procesar_mensaje("dymcyber/banda01/alertas",
        '{"id":"S1-M3","sesion":1,"tipo":"SOBREVELOCIDAD","actual":1.1,"desviacion":10,"umbral":5}')
    est = cliente.get("/api/estado").get_json()
    assert est["estado"]["estado"] == "ACTIVO"
    assert est["total_alertas"] >= 2


def test_resumen_mantenciones_y_export():
    res = cliente.get("/api/resumen?periodo=semana").get_json()
    assert res["mediciones"] >= 2 and "SUBVELOCIDAD" in res["alertas_por_tipo"]
    assert cliente.get("/api/resumen?periodo=anio").status_code == 400
    r = cliente.post("/api/mantenciones", json={"descripcion": "Cambio de rodillo", "responsable": "SysAdmin"})
    assert r.status_code == 201
    assert cliente.get("/api/mantenciones").get_json()[0]["descripcion"] == "Cambio de rodillo"
    csv = cliente.get("/api/export.csv").get_data(as_text=True)
    assert csv.startswith("id,id_medicion") and "S1-M1" in csv


def test_config_valida_rangos():
    assert cliente.post("/api/config", json={"nominal": 2}).status_code == 422
    assert cliente.post("/api/config", json={}).status_code == 400
    # sin broker en pruebas -> 503 (en producción publica en .../config)
    assert cliente.post("/api/config", json={"umbral": 8}).status_code == 503

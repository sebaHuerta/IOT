# DYM Cyber · Sistema IoT de Monitoreo de Velocidad (v2: Wokwi + MQTT + Cloud)

Migración del prototipo de Tinkercad (Arduino Uno R3) a **Wokwi (ESP32)**, con comunicación **MQTT** real,
un **backend Flask** que hace el flujo **ETL** y expone una **API REST**, y un **dashboard** web.

```
 ┌──────────── Nube gris IoT (borde) ────────────┐        ┌──────── Plataforma Cloud ────────┐
 │ Potenciómetro ─┐                               │        │                                  │
 │ Botón ON/OFF  ─┼─► ESP32 ──WiFi──► Internet ───┼─MQTT──►│ Broker HiveMQ (público)          │
 │ Sw. falla    ─┘   │ LEDs, LCD, motor (A4988)   │        │   │ dymcyber/banda01/#           │
 └───────────────────┼────────────────────────────┘        │   ▼                              │
                     ◄──── .../config (N= / U=) ───────────│ Backend Flask (ETL)              │
                                                           │   E: suscripción MQTT / POST     │
                                                           │   T: validar, recalcular, sellar │
                                                           │   L: SQLite                      │
                                                           │   ▼                              │
                                                           │ API REST ──► Dashboard (Chart.js)│
                                                           └──────────────────────────────────┘
```

## Estructura
| Carpeta | Contenido |
|---|---|
| `wokwi/` | `sketch.ino`, `diagram.json`, `libraries.txt` (proyecto Wokwi) |
| `backend/` | `app.py` (Flask + ETL + API), `templates/dashboard.html`, `test_app.py` |
| `../.github/workflows/ev3-ci.yml` | Pipeline: compila firmware ESP32, prueba backend, valida diagrama |

## 1. Simulación en Wokwi
1. Entrar a https://wokwi.com/projects/new/esp32 (con sesión iniciada para poder guardar).
2. Pegar `sketch.ino` y `diagram.json`; en *Library Manager* agregar **PubSubClient** y **LiquidCrystal I2C**.
3. ▶ Iniciar. Botón azul = ON/OFF, potenciómetro = velocidad (0,80–1,20 m/s),
   interruptor = "falla de sensor". En el monitor serial: `N=1.05` o `U=8`.

> Wokwi pausa la simulación si la pestaña queda en segundo plano: el broker publicará
> `OFFLINE` (Last Will) y el ESP32 se reconecta solo al volver.

## 2. Topics MQTT (broker.hivemq.com:1883)
| Topic | Dirección | Payload |
|---|---|---|
| `dymcyber/banda01/velocidad` | ESP32 → nube | `{"id":"S1-M5","actual":0.976,"desviacion":2.41,"umbral":5,"estado":"NORMAL",...}` |
| `dymcyber/banda01/alertas` | ESP32 → nube | `{"id":"S1-M6","tipo":"SUBVELOCIDAD"|"SOBREVELOCIDAD"|"MEDICION_INVALIDA",...}` |
| `dymcyber/banda01/estado` | ESP32 → nube (retenido + Last Will) | `{"estado":"ACTIVO"|"DETENIDO"|"OFFLINE",...}` |
| `dymcyber/banda01/config` | nube → ESP32 | `N=1.05` / `U=8` |
| `dymcyber/banda01/latido` | ESP32 → nube | segundos de funcionamiento (heartbeat) |

Para verlos en vivo: https://www.hivemq.com/demos/websocket-client/ → Connect → suscribirse a `dymcyber/banda01/#`.

## 3. Dashboard en vivo (`dashboard/index.html`)
Página propia que se conecta directo al broker por MQTT sobre WebSocket
(`wss://broker.hivemq.com:8884/mqtt`) y muestra todo en tiempo real: correa animada a la velocidad medida,
copia del LCD y los LED, gráfico con umbrales, alertas, resumen, mensajes MQTT, configuración remota
(N= / U=) y descarga CSV. No necesita instalar nada.

- **En tu PC:** doble clic en `dashboard/index.html` (abre en Chrome).
- **Con link propio (GitHub Pages):** en el repo, Settings → Pages → *Deploy from a branch* → `main` / carpeta raíz.
  Queda en `https://sebahuerta.github.io/IOT/ev3/dashboard/`.
- **Sin Wokwi a mano:** botón *Simular datos* genera mediciones en el navegador (no publica en el broker).

## 4. Backend + API REST (en tu PC)
```bash
cd backend
python -m venv .venv && source .venv/bin/activate   # Windows: .venv\Scripts\activate
pip install -r requirements.txt
python app.py          # abre http://localhost:5000
```
Con la simulación de Wokwi corriendo, las mediciones llegan solas al dashboard.

### API REST
| Método | Ruta | Uso (HU) |
|---|---|---|
| GET | `/api/estado` | estado actual y totales (HU-02) |
| GET/POST | `/api/mediciones` | consultar / ingresar mediciones por HTTP (HU-01) |
| GET | `/api/alertas` | alertas por tipo (HU-03) |
| GET | `/api/resumen?periodo=dia\|semana\|mes` | resúmenes (HU-06) |
| POST | `/api/config` | `{"nominal":1.05}` / `{"umbral":8}` → MQTT → ESP32 (HU-12) |
| GET/POST | `/api/mantenciones` | registro de mantenciones (HU-07) |
| GET | `/api/export.csv` | exportar resultados (HU-04) |

Ejemplo: `curl -X POST localhost:5000/api/config -H "Content-Type: application/json" -d '{"umbral":8}'`

## Relación con los indicadores de logro
- **IL 2.4 DevOps**: `ci.yml` con 3 etapas (firmware, pruebas backend, validación de diagrama).
- **IL 3.3 Cliente-servidor REST**: API REST de envío (POST) y recepción (GET) de datos IoT.
- **IL 3.4 Backend open source**: Python Flask + SQLite + paho-mqtt.
- **ETL/Cloud**: MQTT → transformación/validación → carga en BD → visualización.
  Migración a AWS: el broker se reemplaza por AWS IoT Core, SQLite por DynamoDB y Flask por Lambda + API Gateway (mismos topics y payloads).

## Notas de seguridad
El broker público es solo para la demo (sin usuario ni TLS). En producción: broker privado
(HiveMQ Cloud / AWS IoT Core) en puerto 8883 con TLS y credenciales/certificados por dispositivo.
PubSubClient publica con QoS 0; la suscripción a `config` usa QoS 1.

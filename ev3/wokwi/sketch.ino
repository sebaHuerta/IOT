// =====================================================================
//  DYM CYBER - SISTEMA IoT DE MONITOREO DE VELOCIDAD (v2 - Wokwi/ESP32)
//  Correa transportadora de chancado
//
//  Migración desde Tinkercad (Arduino Uno R3) a Wokwi (ESP32 DevKit):
//   - Misma lógica: botón ON/OFF, potenciómetro = velocidad, LEDs de estado,
//     nominal 1.00 m/s, umbral 5 %, medición cada 5 s, IDs S<sesion>-M<medicion>,
//     comandos N= y U= por monitor serial.
//   - NUEVO: WiFi + MQTT (telemetría, estado y alertas en JSON).
//   - NUEVO: configuración remota por MQTT (topic .../config) desde el backend.
//   - NUEVO: LCD 16x2 I2C con velocidad, desviación y estado.
//   - NUEVO: motor paso a paso (A4988) cuya velocidad sigue a la correa.
//   - NUEVO: interruptor "falla de sensor" para simular mediciones inválidas (HU-05).
// =====================================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>

// ---------------- Red y broker MQTT ----------------
const char* WIFI_SSID     = "Wokwi-GUEST";   // red simulada de Wokwi
const char* WIFI_PASS     = "";
const char* MQTT_BROKER   = "broker.hivemq.com";
const int   MQTT_PORT     = 1883;

const char* TOPIC_VELOCIDAD = "dymcyber/banda01/velocidad";
const char* TOPIC_ESTADO    = "dymcyber/banda01/estado";
const char* TOPIC_ALERTAS   = "dymcyber/banda01/alertas";
const char* TOPIC_CONFIG    = "dymcyber/banda01/config";   // entrada: N=1.05 / U=8
const char* TOPIC_LATIDO    = "dymcyber/banda01/latido";   // heartbeat

WiFiClient espClient;
PubSubClient mqtt(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- Pines ESP32 ----------------
const int pinBoton        = 4;
const int pinPotenciometro = 34;   // ADC1 (12 bits: 0-4095)
const int pinFallaSensor  = 14;    // interruptor: simula sensor desconectado
const int ledVerde        = 25;
const int ledAmarillo     = 26;
const int ledRojo         = 27;
const int pinStep         = 18;    // A4988 STEP
const int pinDir          = 19;    // A4988 DIR

// ---------------- Parámetros del proceso ----------------
float velocidadNominal = 1.00;     // m/s
float umbralDesviacion = 5.0;      // %
const float velocidadMinima = 0.80;
const float velocidadMaxima = 1.20;
const float PASOS_POR_METRO = 200.0;  // 1 m/s = 1 vuelta/s del motor (200 pasos)

bool sistemaActivo = false;

unsigned long ultimaMedicion = 0;
const unsigned long intervaloMedicion = 5000;
unsigned long numeroMedicion = 0;
unsigned long numeroSesion = 0;
unsigned long ultimoIntentoMqtt = 0;

float ultimaVelocidad = 0;
float ultimaDesviacion = 0;
String ultimoEstado = "DETENIDO";

// Botón por interrupción: no se pierden pulsaciones mientras el ESP32 atiende WiFi/MQTT
volatile bool botonPendiente = false;
volatile unsigned long ultimaPulsacion = 0;

void IRAM_ATTR isrBoton() {
  unsigned long t = millis();
  if (t - ultimaPulsacion > 250) {      // antirrebote de 250 ms
    ultimaPulsacion = t;
    botonPendiente = true;
  }
}

// ===================================================================
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);

  pinMode(pinBoton, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinBoton), isrBoton, FALLING);
  pinMode(pinFallaSensor, INPUT_PULLUP);
  pinMode(ledVerde, OUTPUT);
  pinMode(ledAmarillo, OUTPUT);
  pinMode(ledRojo, OUTPUT);
  pinMode(pinStep, OUTPUT);
  pinMode(pinDir, OUTPUT);
  digitalWrite(pinDir, HIGH);

  apagarSalidas();
  digitalWrite(ledAmarillo, HIGH);

  lcd.init();
  lcd.backlight();
  lcdMensaje("DYM CYBER", "Conectando WiFi");

  Serial.println();
  Serial.println("================================");
  Serial.println("            DYM CYBER");
  Serial.println("   MONITOREO DE VELOCIDAD v2");
  Serial.println("================================");
  Serial.println("COMANDOS: N=1.10 (nominal)  U=10 (umbral %)");

  conectarWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(alRecibirMqtt);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(60);
  conectarMqtt();

  lcdMensaje("SISTEMA DETENIDO", "Pulse el boton");
}

void loop() {
  // Reconexión automática (WiFi y MQTT) sin bloquear el monitoreo local
  static bool wifiAvisado = false;
  if (WiFi.status() == WL_CONNECTED && !wifiAvisado) {
    wifiAvisado = true;
    Serial.print("WiFi OK. IP: ");
    Serial.println(WiFi.localIP());
  }
  if (!mqtt.connected() && millis() - ultimoIntentoMqtt > 5000) {
    conectarMqtt();
  }
  mqtt.loop();

  // Latido cada 15 s: mantiene viva la sesión MQTT aunque la simulación vaya lenta
  static unsigned long ultimoLatido = 0;
  if (mqtt.connected() && millis() - ultimoLatido > 15000) {
    ultimoLatido = millis();
    mqtt.publish(TOPIC_LATIDO, String(millis() / 1000).c_str());
  }

  revisarBoton();
  revisarConfiguracionSerial();

  if (!sistemaActivo) return;

  if (millis() - ultimaMedicion >= intervaloMedicion) {
    ultimaMedicion = millis();
    medirVelocidad();
  }
}

// ---------------- Conectividad ----------------
void conectarWiFi() {
  Serial.print("WiFi: conectando a ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  WiFi.setAutoReconnect(true);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 10000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi aun sin conexion: se sigue intentando en segundo plano");
    Serial.println("(el monitoreo local funciona igual)");
  }
}

void conectarMqtt() {
  ultimoIntentoMqtt = millis();
  // Sin WiFi todavía: el ESP32 sigue intentando solo (setAutoReconnect)
  if (WiFi.status() != WL_CONNECTED) return;

  String clientId = "dymcyber-banda01-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("MQTT: conectando a ");
  Serial.print(MQTT_BROKER);
  Serial.print(" ... ");

  // Last Will: si el ESP32 se desconecta, el broker publica OFFLINE
  if (mqtt.connect(clientId.c_str(), TOPIC_ESTADO, 1, true,
                   "{\"equipo\":\"banda01\",\"estado\":\"OFFLINE\"}")) {
    Serial.println("OK");
    mqtt.subscribe(TOPIC_CONFIG, 1);
    publicarEstado();
  } else {
    Serial.print("ERROR rc=");
    Serial.println(mqtt.state());
  }
}

void publicar(const char* topic, const String& payload, bool retenido = false) {
  if (!mqtt.connected()) return;
  mqtt.publish(topic, payload.c_str(), retenido);
  Serial.print("MQTT >> ");
  Serial.print(topic);
  Serial.print(" ");
  Serial.println(payload);
}

void publicarEstado() {
  String json = "{\"equipo\":\"banda01\",\"estado\":\"";
  json += sistemaActivo ? "ACTIVO" : "DETENIDO";
  json += "\",\"sesion\":" + String(numeroSesion);
  json += ",\"nominal\":" + String(velocidadNominal, 3);
  json += ",\"umbral\":" + String(umbralDesviacion, 2) + "}";
  publicar(TOPIC_ESTADO, json, true);   // retenido: el último estado queda en el broker
}

// Mensajes que llegan desde el backend / dashboard
void alRecibirMqtt(char* topic, byte* payload, unsigned int length) {
  String comando;
  for (unsigned int i = 0; i < length; i++) comando += (char)payload[i];
  Serial.print("MQTT << ");
  Serial.print(topic);
  Serial.print(" ");
  Serial.println(comando);
  procesarComando(comando, "MQTT");
}

// ---------------- Botón ----------------
void revisarBoton() {
  if (!botonPendiente) return;
  botonPendiente = false;
  if (sistemaActivo) apagarSistema();
  else encenderSistema();
}

void encenderSistema() {
  sistemaActivo = true;
  numeroSesion++;
  apagarSalidas();
  Serial.println();
  Serial.println("======== SISTEMA ENCENDIDO ========");
  Serial.print("SESION N: ");
  Serial.println(numeroSesion);
  lcdMensaje("SISTEMA ACTIVO", "Sesion S" + String(numeroSesion));
  publicarEstado();
  ultimaMedicion = millis() - intervaloMedicion;   // medir de inmediato
}

void apagarSistema() {
  sistemaActivo = false;
  apagarSalidas();
  digitalWrite(ledAmarillo, HIGH);
  noTone(pinStep);
  Serial.println();
  Serial.println("======== SISTEMA DETENIDO ========");
  Serial.println("MOTOR: APAGADO | ALERTAS: DESACTIVADAS");
  lcdMensaje("SISTEMA DETENIDO", "Pulse el boton");
  publicarEstado();
}

void apagarSalidas() {
  digitalWrite(ledVerde, LOW);
  digitalWrite(ledAmarillo, LOW);
  digitalWrite(ledRojo, LOW);
}

// ---------------- Configuración N= / U= (serial o MQTT) ----------------
void revisarConfiguracionSerial() {
  if (Serial.available() == 0) return;
  String comando = Serial.readStringUntil('\n');
  procesarComando(comando, "SERIAL");
}

void procesarComando(String comando, const char* origen) {
  comando.trim();
  comando.toUpperCase();

  if (comando.length() < 3 || comando.charAt(1) != '=') {
    Serial.println("ERROR: COMANDO INVALIDO");
    return;
  }
  String texto = comando.substring(2);
  texto.trim();

  int puntos = 0, digitos = 0;
  for (unsigned int i = 0; i < texto.length(); i++) {
    char c = texto.charAt(i);
    if (c >= '0' && c <= '9') digitos++;
    else if (c == '.' && ++puntos <= 1) continue;
    else { Serial.println("ERROR: NUMERO INVALIDO"); return; }
  }
  if (digitos == 0) { Serial.println("ERROR: NUMERO INVALIDO"); return; }

  float valor = texto.toFloat();

  if (comando.charAt(0) == 'N') {
    if (valor >= velocidadMinima && valor <= velocidadMaxima) {
      velocidadNominal = valor;
      Serial.print("VELOCIDAD NOMINAL ACTUALIZADA (");
      Serial.print(origen);
      Serial.print("): ");
      Serial.print(velocidadNominal, 3);
      Serial.println(" m/s");
      publicarEstado();
    } else {
      Serial.println("ERROR: NOMINAL FUERA DE RANGO (0.80 - 1.20)");
    }
  } else if (comando.charAt(0) == 'U') {
    if (valor > 0 && valor <= 20) {
      umbralDesviacion = valor;
      Serial.print("UMBRAL ACTUALIZADO (");
      Serial.print(origen);
      Serial.print("): ");
      Serial.print(umbralDesviacion, 2);
      Serial.println(" %");
      publicarEstado();
    } else {
      Serial.println("ERROR: UMBRAL FUERA DE RANGO (0 - 20)");
    }
  } else {
    Serial.println("ERROR: COMANDO DESCONOCIDO");
  }
}

// ---------------- Medición ----------------
void medirVelocidad() {
  numeroMedicion++;
  String id = "S" + String(numeroSesion) + "-M" + String(numeroMedicion);

  // HU-05: sensor desconectado / lectura inválida
  if (digitalRead(pinFallaSensor) == LOW) {
    noTone(pinStep);
    apagarSalidas();
    digitalWrite(ledRojo, HIGH);
    Serial.println("ERROR: MEDICION INVALIDA (" + id + ") - sensor sin senal");
    lcdMensaje("MEDICION INVALID", "Revisar sensor");
    publicar(TOPIC_ALERTAS,
             "{\"id\":\"" + id + "\",\"sesion\":" + String(numeroSesion) +
             ",\"medicion\":" + String(numeroMedicion) +
             ",\"tipo\":\"MEDICION_INVALIDA\"}");
    return;
  }

  int lectura = analogRead(pinPotenciometro);
  float velocidadActual = velocidadMinima +
                          (lectura / 4095.0) * (velocidadMaxima - velocidadMinima);

  if (!isfinite(velocidadActual) || velocidadActual < velocidadMinima ||
      velocidadActual > velocidadMaxima) {
    Serial.println("ERROR: MEDICION INVALIDA");
    return;
  }

  // El motor gira proporcional a la velocidad medida (representa la correa)
  tone(pinStep, (unsigned int)(velocidadActual * PASOS_POR_METRO));

  float desviacion = fabs(velocidadActual - velocidadNominal) / velocidadNominal * 100.0;
  String estado, tipo = "NINGUNA";

  if (desviacion <= umbralDesviacion) {
    estado = "NORMAL";
    digitalWrite(ledVerde, HIGH);
    digitalWrite(ledAmarillo, LOW);
    digitalWrite(ledRojo, LOW);
  } else {
    estado = "ALERTA";
    tipo = (velocidadActual < velocidadNominal) ? "SUBVELOCIDAD" : "SOBREVELOCIDAD";
    digitalWrite(ledVerde, LOW);
    digitalWrite(ledAmarillo, LOW);
    digitalWrite(ledRojo, HIGH);
  }

  // ---- Monitor serial (igual que en Tinkercad) ----
  Serial.println("--------------------------------");
  Serial.println("ID de medicion    : " + id);
  Serial.println("Tiempo            : " + String(millis() / 1000) + " s");
  Serial.println("Velocidad nominal : " + String(velocidadNominal, 3) + " m/s");
  Serial.println("Velocidad actual  : " + String(velocidadActual, 3) + " m/s");
  Serial.println("Desviacion        : " + String(desviacion, 2) + " %");
  Serial.println("Umbral permitido  : " + String(umbralDesviacion, 2) + " %");
  Serial.println("ESTADO: " + estado + (tipo != "NINGUNA" ? " | TIPO: " + tipo : ""));

  // ---- LCD ----
  lcdMensaje("V:" + String(velocidadActual, 2) + " D:" + String(desviacion, 1) + "%",
             estado == "NORMAL" ? "NORMAL  " + id : tipo.substring(0, 5) + " " + id);

  // ---- MQTT: telemetría ----
  String json = "{\"id\":\"" + id + "\"";
  json += ",\"sesion\":" + String(numeroSesion);
  json += ",\"medicion\":" + String(numeroMedicion);
  json += ",\"t_seg\":" + String(millis() / 1000);
  json += ",\"nominal\":" + String(velocidadNominal, 3);
  json += ",\"actual\":" + String(velocidadActual, 3);
  json += ",\"desviacion\":" + String(desviacion, 2);
  json += ",\"umbral\":" + String(umbralDesviacion, 2);
  json += ",\"estado\":\"" + estado + "\"}";
  publicar(TOPIC_VELOCIDAD, json);

  // ---- MQTT: alerta (solo si hay desviación) ----
  if (estado == "ALERTA") {
    String alerta = "{\"id\":\"" + id + "\"";
    alerta += ",\"sesion\":" + String(numeroSesion);
    alerta += ",\"medicion\":" + String(numeroMedicion);
    alerta += ",\"tipo\":\"" + tipo + "\"";
    alerta += ",\"actual\":" + String(velocidadActual, 3);
    alerta += ",\"desviacion\":" + String(desviacion, 2);
    alerta += ",\"umbral\":" + String(umbralDesviacion, 2) + "}";
    publicar(TOPIC_ALERTAS, alerta);
  }

  ultimaVelocidad = velocidadActual;
  ultimaDesviacion = desviacion;
  ultimoEstado = estado;
}

void lcdMensaje(String l1, String l2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(l1.substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(l2.substring(0, 16));
}

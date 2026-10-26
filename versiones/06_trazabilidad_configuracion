// 06_trazabilidad_configuracion


#include <math.h>

const int pinBoton = 2;
const int pinMotor = 3;
const int pinPotenciometro = A0;

const int ledVerde = 8;
const int ledAmarillo = 9;
const int ledRojo = 10;

const float velocidadMinima = 0.80;
const float velocidadMaxima = 1.20;

float velocidadNominal = 1.00;
float umbralDesviacion = 5.0;

bool sistemaActivo = false;
bool lecturaAnterior = HIGH;
bool estadoBoton = HIGH;

unsigned long ultimoCambio = 0;
unsigned long ultimaMedicion = 0;

const unsigned long tiempoRebote = 50;
const unsigned long intervaloMedicion = 5000;

unsigned long numeroSesion = 0;
unsigned long numeroMedicion = 0;

void actualizarIndicadores(bool alerta) {
  digitalWrite(pinMotor, sistemaActivo ? HIGH : LOW);
  digitalWrite(ledAmarillo, sistemaActivo ? LOW : HIGH);
  digitalWrite(ledVerde, sistemaActivo && !alerta ? HIGH : LOW);
  digitalWrite(ledRojo, sistemaActivo && alerta ? HIGH : LOW);
}

void revisarBoton() {
  bool lecturaActual = digitalRead(pinBoton);

  if (lecturaActual != lecturaAnterior) {
    ultimoCambio = millis();
  }

  if (millis() - ultimoCambio > tiempoRebote) {
    if (lecturaActual != estadoBoton) {
      estadoBoton = lecturaActual;

      if (estadoBoton == LOW) {
        sistemaActivo = !sistemaActivo;

        if (sistemaActivo) {
          numeroSesion++;
          ultimaMedicion = millis() - intervaloMedicion;
          Serial.print("SESION INICIADA: ");
          Serial.println(numeroSesion);
        } else {
          Serial.println("SISTEMA DETENIDO");
        }

        actualizarIndicadores(false);
      }
    }
  }

  lecturaAnterior = lecturaActual;
}

void revisarConfiguracion() {
  if (Serial.available() == 0) {
    return;
  }

  String comando = Serial.readStringUntil('\n');
  comando.trim();
  comando.toUpperCase();

  if (comando.length() < 3 || comando.charAt(1) != '=') {
    Serial.println("COMANDO INVALIDO");
    return;
  }

  String texto = comando.substring(2);
  texto.trim();

  if (texto.length() == 0) {
    Serial.println("VALOR INVALIDO");
    return;
  }

  int puntos = 0;
  int digitos = 0;

  for (unsigned int i = 0; i < texto.length(); i++) {
    char c = texto.charAt(i);

    if (c >= '0' && c <= '9') {
      digitos++;
    } else if (c == '.') {
      puntos++;

      if (puntos > 1) {
        Serial.println("VALOR INVALIDO");
        return;
      }
    } else {
      Serial.println("VALOR INVALIDO");
      return;
    }
  }

  if (digitos == 0) {
    Serial.println("VALOR INVALIDO");
    return;
  }

  float valor = texto.toFloat();

  if (comando.charAt(0) == 'N') {
    if (valor >= velocidadMinima &&
        valor <= velocidadMaxima) {

      velocidadNominal = valor;

      Serial.print("NOMINAL: ");
      Serial.println(velocidadNominal, 3);
    } else {
      Serial.println("NOMINAL FUERA DE RANGO");
    }
  } else if (comando.charAt(0) == 'U') {
    if (valor > 0 && valor <= 20) {
      umbralDesviacion = valor;

      Serial.print("UMBRAL: ");
      Serial.println(umbralDesviacion, 2);
    } else {
      Serial.println("UMBRAL FUERA DE RANGO");
    }
  } else {
    Serial.println("COMANDO DESCONOCIDO");
  }
}

void medirVelocidad() {
  int lectura = analogRead(pinPotenciometro);

  float velocidadActual =
    velocidadMinima +
    (lectura / 1023.0) *
    (velocidadMaxima - velocidadMinima);

  if (!isfinite(velocidadActual) ||
      velocidadActual < velocidadMinima ||
      velocidadActual > velocidadMaxima) {

    actualizarIndicadores(true);
    Serial.println("MEDICION INVALIDA");
    return;
  }

  float desviacion =
    fabs(velocidadActual - velocidadNominal)
    / velocidadNominal * 100.0;

  numeroMedicion++;

  Serial.println("---------------------------");

  Serial.print("ID: S");
  Serial.print(numeroSesion);
  Serial.print("-M");
  Serial.println(numeroMedicion);

  Serial.print("TIEMPO: ");
  Serial.print(millis() / 1000);
  Serial.println(" s");

  Serial.print("VELOCIDAD: ");
  Serial.print(velocidadActual, 3);
  Serial.println(" m/s");

  Serial.print("DESVIACION: ");
  Serial.print(desviacion, 2);
  Serial.println(" %");

  bool alerta = desviacion > umbralDesviacion;

  actualizarIndicadores(alerta);

  if (!alerta) {
    Serial.println("ESTADO: NORMAL");
  } else if (velocidadActual < velocidadNominal) {
    Serial.println("ALERTA: SUBVELOCIDAD");
  } else {
    Serial.println("ALERTA: SOBREVELOCIDAD");
  }
}

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(100);

  pinMode(pinBoton, INPUT_PULLUP);
  pinMode(pinMotor, OUTPUT);

  pinMode(ledVerde, OUTPUT);
  pinMode(ledAmarillo, OUTPUT);
  pinMode(ledRojo, OUTPUT);

  actualizarIndicadores(false);

  Serial.println("DYM CYBER");
  Serial.println("SISTEMA DETENIDO");
  Serial.println("N=1.10 CAMBIAR NOMINAL");
  Serial.println("U=10 CAMBIAR UMBRAL");
}

void loop() {
  revisarBoton();
  revisarConfiguracion();

  if (sistemaActivo &&
      millis() - ultimaMedicion >= intervaloMedicion) {

    ultimaMedicion = millis();
    medirVelocidad();
  }
}

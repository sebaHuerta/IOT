
#include <math.h>

const int pinBoton = 2;
const int pinMotor = 3;
const int pinPotenciometro = A0;

const int ledVerde = 8;
const int ledAmarillo = 9;
const int ledRojo = 10;

float velocidadNominal = 1.00;
float umbralDesviacion = 5.0;

const float velocidadMinima = 0.80;
const float velocidadMaxima = 1.20;

bool sistemaActivo = false;
bool lecturaAnterior = HIGH;
bool estadoBoton = HIGH;

unsigned long ultimoCambio = 0;
const unsigned long tiempoRebote = 50;

unsigned long ultimaMedicion = 0;
const unsigned long intervaloMedicion = 5000;

unsigned long numeroMedicion = 0;
unsigned long numeroSesion = 0;

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(100);

  pinMode(pinBoton, INPUT_PULLUP);
  pinMode(pinMotor, OUTPUT);

  pinMode(ledVerde, OUTPUT);
  pinMode(ledAmarillo, OUTPUT);
  pinMode(ledRojo, OUTPUT);

  digitalWrite(pinMotor, LOW);
  digitalWrite(ledVerde, LOW);
  digitalWrite(ledAmarillo, HIGH);
  digitalWrite(ledRojo, LOW);

  Serial.println();
  Serial.println("================================");
  Serial.println("           DYM CYBER");
  Serial.println("   MONITOREO DE VELOCIDAD");
  Serial.println("================================");
  Serial.println("ESTADO: SISTEMA DETENIDO");
  Serial.println("MOTOR: APAGADO");
  Serial.println();
  Serial.println("COMANDOS DE CONFIGURACION:");
  Serial.println("N=1.10  Velocidad nominal");
  Serial.println("U=10    Umbral porcentual");
  Serial.println();
}

void loop() {
  revisarBoton();
  revisarConfiguracion();

  if (!sistemaActivo) {
    return;
  }

  if (millis() - ultimaMedicion >= intervaloMedicion) {
    ultimaMedicion = millis();
    medirVelocidad();
  }
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
        if (sistemaActivo) {
          apagarSistema();
        } else {
          encenderSistema();
        }
      }
    }
  }

  lecturaAnterior = lecturaActual;
}

void encenderSistema() {
  sistemaActivo = true;
  numeroSesion++;

  digitalWrite(pinMotor, HIGH);
  digitalWrite(ledVerde, LOW);
  digitalWrite(ledAmarillo, LOW);
  digitalWrite(ledRojo, LOW);

  Serial.println();
  Serial.println("================================");
  Serial.println("       SISTEMA ENCENDIDO");
  Serial.println("================================");
  Serial.print("SESION N: ");
  Serial.println(numeroSesion);
  Serial.println("MOTOR: ENCENDIDO");
  Serial.println("MONITOREO: ACTIVO");
  Serial.println();

  ultimaMedicion = millis() - intervaloMedicion;
}

void apagarSistema() {
  sistemaActivo = false;

  digitalWrite(pinMotor, LOW);
  digitalWrite(ledVerde, LOW);
  digitalWrite(ledAmarillo, HIGH);
  digitalWrite(ledRojo, LOW);

  Serial.println();
  Serial.println("================================");
  Serial.println("       SISTEMA DETENIDO");
  Serial.println("================================");
  Serial.println("MOTOR: APAGADO");
  Serial.println("MONITOREO: INACTIVO");
  Serial.println("ALERTAS: DESACTIVADAS");
  Serial.println();

  ultimaMedicion = millis();
}

void revisarConfiguracion() {
  if (Serial.available() == 0) {
    return;
  }

  String comando = Serial.readStringUntil('\n');
  comando.trim();
  comando.toUpperCase();

  if (comando.length() < 3 || comando.charAt(1) != '=') {
    Serial.println("ERROR: COMANDO INVALIDO");
    return;
  }

  String texto = comando.substring(2);
  texto.trim();

  if (texto.length() == 0) {
    Serial.println("ERROR: VALOR VACIO");
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
        Serial.println("ERROR: NUMERO INVALIDO");
        return;
      }
    } else {
      Serial.println("ERROR: NUMERO INVALIDO");
      return;
    }
  }

  if (digitos == 0) {
    Serial.println("ERROR: NUMERO INVALIDO");
    return;
  }

  float valor = texto.toFloat();

  if (comando.charAt(0) == 'N') {
    if (valor >= velocidadMinima &&
        valor <= velocidadMaxima) {

      velocidadNominal = valor;

      Serial.print("VELOCIDAD NOMINAL ACTUALIZADA: ");
      Serial.print(velocidadNominal, 3);
      Serial.println(" m/s");

    } else {
      Serial.println("ERROR: NOMINAL FUERA DE RANGO");
    }
  } else if (comando.charAt(0) == 'U') {
    if (valor > 0 && valor <= 20) {

      umbralDesviacion = valor;

      Serial.print("UMBRAL ACTUALIZADO: ");
      Serial.print(umbralDesviacion, 2);
      Serial.println(" %");

    } else {
      Serial.println("ERROR: UMBRAL FUERA DE RANGO");
    }
  } else {
    Serial.println("ERROR: COMANDO DESCONOCIDO");
  }
}

void medirVelocidad() {
  if (!sistemaActivo) {
    return;
  }

  int lectura = analogRead(pinPotenciometro);

  float velocidadActual =
    velocidadMinima +
    (lectura / 1023.0) *
    (velocidadMaxima - velocidadMinima);

  if (!isfinite(velocidadActual) ||
      velocidadActual < velocidadMinima ||
      velocidadActual > velocidadMaxima) {

    digitalWrite(ledVerde, LOW);
    digitalWrite(ledAmarillo, LOW);
    digitalWrite(ledRojo, HIGH);

    Serial.println("ERROR: MEDICION INVALIDA");
    return;
  }

  float desviacion =
    fabs(velocidadActual - velocidadNominal)
    / velocidadNominal * 100.0;

  numeroMedicion++;

  Serial.println("--------------------------------");
  Serial.print("MEDICION N: ");
  Serial.println(numeroMedicion);
  Serial.println("--------------------------------");

  Serial.print("Sesion            : ");
  Serial.println(numeroSesion);

  Serial.print("ID de medicion    : S");
  Serial.print(numeroSesion);
  Serial.print("-M");
  Serial.println(numeroMedicion);

  Serial.print("Tiempo transcurrido: ");
  Serial.print(millis() / 1000);
  Serial.println(" segundos");

  Serial.print("Velocidad nominal : ");
  Serial.print(velocidadNominal, 3);
  Serial.println(" m/s");

  Serial.print("Velocidad actual  : ");
  Serial.print(velocidadActual, 3);
  Serial.println(" m/s");

  Serial.print("Desviacion        : ");
  Serial.print(desviacion, 2);
  Serial.println(" %");

  Serial.print("Umbral permitido  : ");
  Serial.print(umbralDesviacion, 2);
  Serial.println(" %");

  Serial.println();

  if (desviacion <= umbralDesviacion) {
    digitalWrite(ledVerde, HIGH);
    digitalWrite(ledAmarillo, LOW);
    digitalWrite(ledRojo, LOW);

    Serial.println("ESTADO: NORMAL");
    Serial.println("ALERTA: NINGUNA");
  } else {
    digitalWrite(ledVerde, LOW);
    digitalWrite(ledAmarillo, LOW);
    digitalWrite(ledRojo, HIGH);

    Serial.println("ESTADO: ALERTA");

    if (velocidadActual < velocidadNominal) {
      Serial.println("TIPO: SUBVELOCIDAD");
    } else {
      Serial.println("TIPO: SOBREVELOCIDAD");
    }
  }

  Serial.println();
}


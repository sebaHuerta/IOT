// 04_simulacion_velocidad


const int pinBoton = 2;
const int pinMotor = 3;
const int pinPotenciometro = A0;

const int ledVerde = 8;
const int ledAmarillo = 9;
const int ledRojo = 10;

const float velocidadMinima = 0.80;
const float velocidadMaxima = 1.20;

bool sistemaActivo = false;
bool lecturaAnterior = HIGH;
bool estadoBoton = HIGH;

unsigned long ultimoCambio = 0;
unsigned long ultimaMedicion = 0;

const unsigned long tiempoRebote = 50;
const unsigned long intervaloMedicion = 5000;

void actualizarSistema() {
  digitalWrite(pinMotor, sistemaActivo ? HIGH : LOW);
  digitalWrite(ledVerde, sistemaActivo ? HIGH : LOW);
  digitalWrite(ledAmarillo, sistemaActivo ? LOW : HIGH);
  digitalWrite(ledRojo, LOW);
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
        actualizarSistema();

        if (sistemaActivo) {
          ultimaMedicion = millis() - intervaloMedicion;
          Serial.println("MONITOREO ACTIVO");
        } else {
          Serial.println("MONITOREO DETENIDO");
        }
      }
    }
  }

  lecturaAnterior = lecturaActual;
}

void medirVelocidad() {
  int lectura = analogRead(pinPotenciometro);

  float velocidad =
    velocidadMinima +
    (lectura / 1023.0) *
    (velocidadMaxima - velocidadMinima);

  Serial.print("VELOCIDAD SIMULADA: ");
  Serial.print(velocidad, 3);
  Serial.println(" m/s");
}

void setup() {
  Serial.begin(9600);

  pinMode(pinBoton, INPUT_PULLUP);
  pinMode(pinMotor, OUTPUT);

  pinMode(ledVerde, OUTPUT);
  pinMode(ledAmarillo, OUTPUT);
  pinMode(ledRojo, OUTPUT);

  actualizarSistema();

  Serial.println("DYM CYBER");
  Serial.println("SISTEMA DETENIDO");
}

void loop() {
  revisarBoton();

  if (sistemaActivo &&
      millis() - ultimaMedicion >= intervaloMedicion) {

    ultimaMedicion = millis();
    medirVelocidad();
  }
}

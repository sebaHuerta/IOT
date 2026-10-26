// 03_control_leds


const int pinBoton = 2;
const int pinMotor = 3;

const int ledVerde = 8;
const int ledAmarillo = 9;
const int ledRojo = 10;

bool sistemaActivo = false;
bool lecturaAnterior = HIGH;
bool estadoBoton = HIGH;

unsigned long ultimoCambio = 0;
const unsigned long tiempoRebote = 50;

void actualizarIndicadores() {
  digitalWrite(pinMotor, sistemaActivo ? HIGH : LOW);
  digitalWrite(ledVerde, sistemaActivo ? HIGH : LOW);
  digitalWrite(ledAmarillo, sistemaActivo ? LOW : HIGH);
  digitalWrite(ledRojo, LOW);
}

void setup() {
  Serial.begin(9600);

  pinMode(pinBoton, INPUT_PULLUP);
  pinMode(pinMotor, OUTPUT);

  pinMode(ledVerde, OUTPUT);
  pinMode(ledAmarillo, OUTPUT);
  pinMode(ledRojo, OUTPUT);

  actualizarIndicadores();

  Serial.println("DYM CYBER");
  Serial.println("SISTEMA DETENIDO");
}

void loop() {
  bool lecturaActual = digitalRead(pinBoton);

  if (lecturaActual != lecturaAnterior) {
    ultimoCambio = millis();
  }

  if (millis() - ultimoCambio > tiempoRebote) {
    if (lecturaActual != estadoBoton) {
      estadoBoton = lecturaActual;

      if (estadoBoton == LOW) {
        sistemaActivo = !sistemaActivo;
        actualizarIndicadores();

        if (sistemaActivo) {
          Serial.println("SISTEMA ENCENDIDO");
        } else {
          Serial.println("SISTEMA DETENIDO");
        }
      }
    }
  }

  lecturaAnterior = lecturaActual;
}

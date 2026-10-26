// 02_prueba_boton


const int pinBoton = 2;
const int pinMotor = 3;

bool sistemaActivo = false;
bool lecturaAnterior = HIGH;
bool estadoBoton = HIGH;

unsigned long ultimoCambio = 0;
const unsigned long tiempoRebote = 50;

void setup() {
  Serial.begin(9600);

  pinMode(pinBoton, INPUT_PULLUP);
  pinMode(pinMotor, OUTPUT);

  digitalWrite(pinMotor, LOW);

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

        digitalWrite(
          pinMotor,
          sistemaActivo ? HIGH : LOW
        );

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

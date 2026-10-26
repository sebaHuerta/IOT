// 01_prueba_motor

const int pinMotor = 3;

void setup() {
  pinMode(pinMotor, OUTPUT);
  digitalWrite(pinMotor, LOW);
}

void loop() {
  digitalWrite(pinMotor, HIGH);
  delay(3000);

  digitalWrite(pinMotor, LOW);
  delay(3000);
}

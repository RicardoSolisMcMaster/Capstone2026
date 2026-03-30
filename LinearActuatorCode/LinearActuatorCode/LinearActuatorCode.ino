const int RPWM = 5;
const int LPWM = 6;

void setup() {
  pinMode(RPWM, OUTPUT);
  pinMode(LPWM, OUTPUT);
}

void loop() {
  // FORWARD
  analogWrite(RPWM, 255);
  analogWrite(LPWM, 0);
  delay(3000);

  // STOP
  analogWrite(RPWM, 0);
  analogWrite(LPWM, 0);
  delay(1000);

  // REVERSE
  analogWrite(RPWM, 0);
  analogWrite(LPWM, 255);
  delay(3000);

  // STOP
  analogWrite(RPWM, 0);
  analogWrite(LPWM, 0);
  delay(2000);
}
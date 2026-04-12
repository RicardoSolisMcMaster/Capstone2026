const int RPWM = 5;
const int LPWM = 6;

void setup() {
  pinMode(RPWM, OUTPUT);
  pinMode(LPWM, OUTPUT);

  stopActuator();
  delay(2000); // small delay before starting
}

void loop() {

  // ===== EXTEND FULL SPEED =====
  analogWrite(RPWM, 255);  // FULL POWER
  analogWrite(LPWM, 0);
  delay(1000);             // adjust if needed (long enough to fully extend)

  // ===== STOP =====
  stopActuator();
  delay(1000);

  // ===== RETRACT FULL SPEED =====
  analogWrite(RPWM, 0);
  analogWrite(LPWM, 255);  // FULL POWER
  delay(1000);             // adjust if needed

  // ===== STOP =====
  stopActuator();
  delay(1000);
}

void stopActuator() {
  analogWrite(RPWM, 0);
  analogWrite(LPWM, 0);
}
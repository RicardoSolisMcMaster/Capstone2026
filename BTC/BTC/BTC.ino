const int button1 = 22;
const int button2 = 23;
const int button3 = 24;
const int button4 = 25;
const int button5 = 26;

void setup() {
  Serial.begin(9600);

  pinMode(button1, INPUT_PULLUP);
  pinMode(button2, INPUT_PULLUP);
  pinMode(button3, INPUT_PULLUP);
  pinMode(button4, INPUT_PULLUP);
  pinMode(button5, INPUT_PULLUP);


  Serial.println("Button test ready");
}

void loop() {
  if (digitalRead(button1) == LOW) {
    Serial.println("Button 1 pressed");
    delay(200);
  }

  if (digitalRead(button2) == LOW) {
    Serial.println("Button 2 pressed");
    delay(200);
  }

  if (digitalRead(button3) == LOW) {
    Serial.println("Button 3 pressed");
    delay(200);
  }
  if (digitalRead(button4) == LOW) {
    Serial.println("Button 4 pressed");
    delay(200);
  }
  if (digitalRead(button5) == LOW) {
    Serial.println("Button 5 pressed");
    delay(200);
  }
}
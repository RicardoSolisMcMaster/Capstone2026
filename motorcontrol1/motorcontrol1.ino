//pos 1 2 3

const int stepPin = 25;
const int dirPin  = 26;
const int enaPin  = 27;
const int check = 14;


const int MAX_STEPS = 23500;
const int STEPS_PER_MM = 80;

void setup() {
  int stepsTravelled = 0;
  int pos1 = 44;
  int pos2 = 102;
  int pos3 = 102;

  Serial.begin(9600);

  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  pinMode(enaPin, OUTPUT);
  pinMode(check, INPUT);

  delay(1000);

  Serial.println("beginning");
  digitalWrite(dirPin, LOW);//Right
  for(int i=0; i<pos1*STEPS_PER_MM; i++){
    digitalWrite(stepPin,HIGH);
    delayMicroseconds(300);         //movement block
    digitalWrite(stepPin,LOW);
    delayMicroseconds(300);
    stepsTravelled++;
  }
  Serial.println("Moved to Pos 1");
  delay(5000);
    
  Serial.println("Moving to pos 2");
  digitalWrite(dirPin, LOW); //  Right
  for(int i=0; i<pos2*STEPS_PER_MM; i++){
    digitalWrite(stepPin,HIGH);
    delayMicroseconds(300);
    digitalWrite(stepPin,LOW);
    delayMicroseconds(300);
    stepsTravelled++;
  }
  Serial.println("moved to pos 2");
  delay(5000);

  Serial.println("Moving to pos 3");
  digitalWrite(dirPin, LOW); //  Right
  for(int i=0; i<pos3*STEPS_PER_MM; i++){
      digitalWrite(stepPin,HIGH);
      delayMicroseconds(300);
      digitalWrite(stepPin,LOW);
      delayMicroseconds(300);
      stepsTravelled++;
  }
  Serial.println("moved to pos 3");
  delay(5000);


  Serial.println("returning home");
  digitalWrite(dirPin, HIGH); //  LEFT
  Serial.println(stepsTravelled);
  for(int i=0;i<stepsTravelled;i++){
      digitalWrite(stepPin,HIGH);
      delayMicroseconds(300);
      digitalWrite(stepPin,LOW);
      delayMicroseconds(300);
   }
  stepsTravelled = 0;
  Serial.println("home");
  delay(5000);
}

void loop() {
}
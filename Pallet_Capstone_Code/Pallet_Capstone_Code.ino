

#include <HardwareSerial.h>


const int DIR_X  = 25;
const int STEP_X = 26;
const int DIR_Y  = 18;
const int STEP_Y = 19;

// Scanner UART2 pins
const int SCAN_RX = 16;   // Scanner TX → ESP32 RX2
const int SCAN_TX = 17;   // Scanner RX → ESP32 TX2

HardwareSerial Scanner(2);


const float STEPS_PER_MM = 320.0;
const unsigned int STEP_DELAY_US = 350;

float currentXmm = 0;
float currentYmm = 0;
float targetXmm  = 0;
float targetYmm  = 0;

// STATE FLAGS
bool manualX = false;
bool manualY = false;
bool autoMode = false;
bool scanAllMode = false;
bool stopAll = false;

long stepX_out = 0, stepY_out = 0;
int activePallet = -1;
int scanPalletIndex = 1;
unsigned long dwellStart = 0;
bool barcodeCapturedThisDwell = false;

enum Stage { IDLE, GOING_OUT, DWELL, NEXT_MOVE, RETURN_HOME };
Stage autoStage = IDLE;

String command;


uint8_t SCAN_ON_CMD[]  = {0x7E,0x00,0x08,0x01,0x00,0x02,0x01,0xAB,0xCD};
uint8_t SCAN_OFF_CMD[] = {0x7E,0x00,0x08,0x01,0x00,0x02,0x00,0xAB,0xCD};
bool scannerIsOn = false;


String palletName[10];    // 1..9 used
String palletSerial[10];  // 1..9 used


const char* validSerials[] = {
  "11111", "22222", "33333", "44444", "55555",
  "66666", "77777", "88888", "99999"
};

const char* validNames[] = {
  "Kirkland",
  "Dove",
  "Tide",
  "Charmin",
  "Colgate",
  "LG",
  "Johnson & Johnson",
  "Clorox",
  "Lysol"
};

const int NUM_ITEMS = 9;


bool lookupItem(String serial, String &nameOut) {
  for (int i = 0; i < NUM_ITEMS; i++) {
    if (serial.equals(validSerials[i])) {
      nameOut = validNames[i];
      return true;
    }
  }
  return false;
}


String extractDigits(const String &s) {
  String out = "";
  for (size_t i = 0; i < s.length(); i++) {
    if (isDigit(s[i])) out += s[i];
  }

  // If scanner sends extra digits (like 3111111), keep last 5
  if (out.length() > 5) {
    out = out.substring(out.length() - 5);
  }
  return out;
}

void scannerStart() {
  Scanner.write(SCAN_ON_CMD, sizeof(SCAN_ON_CMD));
  scannerIsOn = true;
  Serial.println("[Scanner] ON");
}

void scannerStop() {
  Scanner.write(SCAN_OFF_CMD, sizeof(SCAN_OFF_CMD));
  scannerIsOn = false;
  Serial.println("[Scanner] OFF");
}

void stepPulse(int pin) {
  digitalWrite(pin, HIGH);
  delayMicroseconds(STEP_DELAY_US);
  digitalWrite(pin, LOW);
  delayMicroseconds(STEP_DELAY_US);
}

void getPalletCoords(int p, float &x, float &y) {
  int row = (p - 1) / 3;
  int col = (p - 1) % 3;
  x = 5 + col * 30;
  y = 0 + row * 30;
}

void goToPallet(int p) {
  activePallet = p;
  getPalletCoords(p, targetXmm, targetYmm);

  float dx = targetXmm - currentXmm;
  float dy = targetYmm - currentYmm;

  digitalWrite(DIR_X, dx > 0 ? HIGH : LOW);
  digitalWrite(DIR_Y, dy > 0 ? HIGH : LOW);

  stepX_out = abs((long)(dx * STEPS_PER_MM));
  stepY_out = abs((long)(dy * STEPS_PER_MM));

  autoMode = true;
  autoStage = GOING_OUT;
  stopAll = false;

  Serial.printf("Moving to pallet P%d | targetX=%.1fmm targetY=%.1fmm\n", p, targetXmm, targetYmm);
}

void startReturnHome() {
  targetXmm = 0;
  targetYmm = 0;

  float dx = targetXmm - currentXmm;
  float dy = targetYmm - currentYmm;

  digitalWrite(DIR_X, dx > 0 ? HIGH : LOW);
  digitalWrite(DIR_Y, dy > 0 ? HIGH : LOW);

  stepX_out = abs((long)(dx * STEPS_PER_MM));
  stepY_out = abs((long)(dy * STEPS_PER_MM));

  autoMode = true;
  autoStage = RETURN_HOME;
  stopAll = false;

  Serial.println("Returning HOME...");
}

void startScanAll() {
  scanAllMode = true;
  scanPalletIndex = 1;
  Serial.println("Starting SCANALL from HOME...");

  currentXmm = 0;
  currentYmm = 0;

  goToPallet(scanPalletIndex);
}

void emergencyStop() {
  stopAll = true;
  manualX = manualY = false;
  autoMode = false;
  scanAllMode = false;
  autoStage = IDLE;
  stepX_out = stepY_out = 0;

  if (scannerIsOn) scannerStop();

  Serial.println("!!! EMERGENCY STOP ACTIVATED !!!");
}


void setup() {
  Serial.begin(9600);

  pinMode(DIR_X, OUTPUT);
  pinMode(STEP_X, OUTPUT);
  pinMode(DIR_Y, OUTPUT);
  pinMode(STEP_Y, OUTPUT);

  Scanner.begin(9600, SERIAL_8N1, SCAN_RX, SCAN_TX);

  Serial.println("======= COMMANDS =======");
  Serial.println("XHIGH / XLOW / XSTOP");
  Serial.println("YHIGH / YLOW / YSTOP");
  Serial.println("P1..P9");
  Serial.println("SCANALL");
  Serial.println("INFO P#");
  Serial.println("STOP");
  Serial.println("=========================");
}


void loop() {

 
  if (Serial.available()) {
    command = Serial.readStringUntil('\n');
    command.trim();

    if (command == "STOP") emergencyStop();

    else if (command == "XHIGH") { manualX = true; digitalWrite(DIR_X, HIGH); }
    else if (command == "XLOW")  { manualX = true; digitalWrite(DIR_X, LOW); }
    else if (command == "XSTOP") manualX = false;

    else if (command == "YHIGH") { manualY = true; digitalWrite(DIR_Y, HIGH); }
    else if (command == "YLOW")  { manualY = true; digitalWrite(DIR_Y, LOW); }
    else if (command == "YSTOP") manualY = false;

    else if (command.startsWith("P")) {
      int p = command.substring(1).toInt();
      if (p >= 1 && p <= 9 && !autoMode && !scanAllMode) {
        goToPallet(p);
      }
    }

    else if (command == "SCANALL" && !autoMode && !scanAllMode) {
      startScanAll();
    }

    else if (command.startsWith("INFO P")) {
      int p = command.substring(6).toInt();
      if (p >= 1 && p <= 9) {
        if (palletSerial[p].length() > 0 && palletName[p].length() > 0) {
          Serial.printf("P%d → Serial: %s | Name: %s\n",
                        p,
                        palletSerial[p].c_str(),
                        palletName[p].c_str());
        } else {
          Serial.printf("P%d → Nothing is logged inside this position!\n", p);
        }
      }
    }
  }

  
  if (!autoMode && !stopAll) {
    if (manualX) stepPulse(STEP_X);
    if (manualY) stepPulse(STEP_Y);
  }

 
  if (autoMode && !stopAll) {
    switch (autoStage) {

      case GOING_OUT:
        if (stepX_out > 0) { stepPulse(STEP_X); stepX_out--; }
        if (stepY_out > 0) { stepPulse(STEP_Y); stepY_out--; }

        if (stepX_out <= 0 && stepY_out <= 0) {
          currentXmm = targetXmm;
          currentYmm = targetYmm;

          Serial.printf("At pallet P%d. Scanning...\n", activePallet);

          palletSerial[activePallet] = "";
          palletName[activePallet] = "";
          barcodeCapturedThisDwell = false;

          dwellStart = millis();
          scannerStart();
          autoStage = DWELL;
        }
        break;

      case DWELL:
        if (!barcodeCapturedThisDwell && Scanner.available()) {
          String raw = Scanner.readStringUntil('\n');
          raw.trim();

          Serial.print("[Raw scanner data] ");
          Serial.println(raw);

          String digits = extractDigits(raw);

          if (digits.length() == 5) {
            String itemName;
            if (lookupItem(digits, itemName)) {
              barcodeCapturedThisDwell = true;
              palletSerial[activePallet] = digits;
              palletName[activePallet] = itemName;

              Serial.printf("[P%d Valid] Serial: %s | Name: %s\n",
                            activePallet,
                            digits.c_str(),
                            itemName.c_str());
            } else {
              Serial.print("[Ignored - not in list] ");
              Serial.println(digits);
            }
          } else if (digits.length() > 0) {
            Serial.print("[Ignored - invalid format] ");
            Serial.println(digits);
          }
        }

        if (millis() - dwellStart >= 3000) {
          scannerStop();

          if (!barcodeCapturedThisDwell) {
            Serial.println("Nothing is logged inside this position!");
            palletSerial[activePallet] = "";
            palletName[activePallet] = "";
          }

          if (scanAllMode) {
            autoStage = NEXT_MOVE;
          } else {
            startReturnHome();
          }
        }
        break;

      case NEXT_MOVE:
        autoMode = false;
        autoStage = IDLE;

        if (scanAllMode) {
          scanPalletIndex++;
          if (scanPalletIndex <= 9) {
            delay(500);
            goToPallet(scanPalletIndex);
          } else {
            Serial.println("SCANALL COMPLETE. Returning HOME...");
            scanAllMode = false;
            startReturnHome();
          }
        }
        break;

      case RETURN_HOME:
        if (stepX_out > 0) { stepPulse(STEP_X); stepX_out--; }
        if (stepY_out > 0) { stepPulse(STEP_Y); stepY_out--; }

        if (stepX_out <= 0 && stepY_out <= 0) {
          currentXmm = 0;
          currentYmm = 0;
          autoMode = false;
          autoStage = IDLE;
          Serial.println("Back at HOME.");
        }
        break;

      default:
        break;
    }
  }
}

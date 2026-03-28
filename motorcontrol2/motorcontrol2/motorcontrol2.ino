#include <Arduino.h>

/*
  ============================================================
  2-AXIS GANTRY PROOF OF CONCEPT - ESP32
  ============================================================

  Sequence:
  1. Wait for Start press
  2. Only start if:
       - cane occupancy = true
       - stain fullness = true
  3. Move to Cane 1 X position
  4. Move Y to top of cane
  5. Pause and "coat" row-by-row while lowering Y
  6. Repeat for Cane 2 and Cane 3
  7. Return to home (X=0, Y=0)

  Controls:
  - Start button: begin cycle if requirements met
  - Pause button: pause/resume exactly where it left off
  - Stop button: abort cycle and return home
  - Occupancy button: toggle cane occupancy requirement
  - Stain button: toggle stain fullness requirement

  Notes:
  - Home/start is assumed to be X=0, Y=0 at power-up
  - This is open-loop step counting, no physical limit switches
  - If an axis moves the wrong direction, swap that axis direction constants
*/

// ============================================================
// X AXIS MOTOR PINS
// ============================================================
const int X_STEP_PIN = 25;
const int X_DIR_PIN  = 26;
const int X_ENA_PIN  = 27;

// ============================================================
// Y AXIS MOTOR PINS
// ============================================================
const int Y_STEP_PIN = 14;
const int Y_DIR_PIN  = 12;
const int Y_ENA_PIN  = 13;

// ============================================================
// BUTTON PINS
// ============================================================
const int START_BTN_PIN     = 32;
const int PAUSE_BTN_PIN     = 33;
const int STOP_BTN_PIN      = 15;
const int OCCUPANCY_BTN_PIN = 4;
const int STAIN_BTN_PIN     = 4;

// ============================================================
// DRIVER SETTINGS
// ============================================================
// Set to false if you do not want to actively control ENA
const bool USE_ENABLE_PINS = false;

// Common driver logic is often LOW = enabled, HIGH = disabled
const int DRIVER_ENABLE_LEVEL  = LOW;
const int DRIVER_DISABLE_LEVEL = HIGH;

// ============================================================
// DIRECTION SETTINGS
// Adjust these if either axis moves opposite to intended direction
// ============================================================

// X axis:
// LOW  = move right / away from home motor
// HIGH = move left  / toward home motor
const int X_POSITIVE_DIR = LOW;
const int X_NEGATIVE_DIR = HIGH;

// Y axis:
// choose one direction as "up" and the other as "down"
// You may need to swap these after testing
const int Y_POSITIVE_DIR = HIGH;   // up
const int Y_NEGATIVE_DIR = LOW;  // down

// ============================================================
// MECHANICAL SETTINGS
// ============================================================
const long X_MAX_STEPS = 23500;   // full X travel from your testing
const long Y_MAX_STEPS = 16000;   // adjust to your real Y travel if needed

const int X_STEPS_PER_MM = 80;
const int Y_STEPS_PER_MM = 80;    // adjust if Y differs from X

// Cane positions from your working X test
const int POS1_MM = 44;
const int POS2_MM = 102;
const int POS3_MM = 102;

// Cane painting settings for Y axis
const int CANE_HEIGHT_MM = 120;   // adjust to your proof-of-concept cane height
const int ROW_DROP_MM    = 15;    // how much to lower between coating rows
const int COAT_DWELL_MS  = 1200;  // how long to "coat" each row
const int MOVE_SETTLE_MS = 200;   // short settle pause after reaching motion points

// ============================================================
// STEP TIMING
// Higher interval = slower movement
// ============================================================
const unsigned long X_STEP_INTERVAL_US = 700;
const unsigned long Y_STEP_INTERVAL_US = 700;
const unsigned long STEP_HIGH_US       = 8;

// ============================================================
// DEBOUNCE
// ============================================================
const unsigned long DEBOUNCE_MS = 40;

// ============================================================
// SEQUENCE STORAGE
// ============================================================
const int MAX_SEQUENCE_POINTS = 80;

// ============================================================
// SYSTEM STATE
// ============================================================
enum SystemState {
  IDLE,
  RUNNING,
  PAUSED,
  STOPPING
};

SystemState systemState = IDLE;

// ============================================================
// REQUIREMENTS
// ============================================================
bool caneOccupancy = false;
bool stainFull     = false;

// ============================================================
// AXIS STATE
// ============================================================
long currentXSteps = 0;
long currentYSteps = 0;

long targetXSteps  = 0;
long targetYSteps  = 0;

bool xStepHigh = false;
bool yStepHigh = false;

unsigned long lastXStepMicros = 0;
unsigned long lastYStepMicros = 0;

// ============================================================
// SEQUENCE POINT
// ============================================================
struct SequencePoint {
  long x;
  long y;
  unsigned long dwellMs;
};

SequencePoint sequencePoints[MAX_SEQUENCE_POINTS];
int sequenceLength = 0;
int sequenceIndex  = 0;

bool waitingAtPoint = false;
unsigned long waitStartMs = 0;

// pause timing support
unsigned long pauseStartedMs = 0;

// ============================================================
// BUTTON CLASS
// ============================================================
class DebouncedButton {
public:
  DebouncedButton(int pin) : _pin(pin) {}

  void begin() {
    pinMode(_pin, INPUT_PULLUP);
    _stableState = digitalRead(_pin);
    _lastReading = _stableState;
    _lastDebounceTime = millis();
  }

  bool pressed() {
    bool reading = digitalRead(_pin);

    if (reading != _lastReading) {
      _lastDebounceTime = millis();
      _lastReading = reading;
    }

    if ((millis() - _lastDebounceTime) > DEBOUNCE_MS) {
      if (reading != _stableState) {
        _stableState = reading;
        if (_stableState == LOW) {
          return true;
        }
      }
    }

    return false;
  }

private:
  int _pin;
  bool _stableState;
  bool _lastReading;
  unsigned long _lastDebounceTime;
};

DebouncedButton startBtn(START_BTN_PIN);
DebouncedButton pauseBtn(PAUSE_BTN_PIN);
DebouncedButton stopBtn(STOP_BTN_PIN);
DebouncedButton occupancyBtn(OCCUPANCY_BTN_PIN);
DebouncedButton stainBtn(STAIN_BTN_PIN);

// ============================================================
// HELPERS
// ============================================================
long xMmToSteps(int mm) {
  return (long)mm * X_STEPS_PER_MM;
}

long yMmToSteps(int mm) {
  return (long)mm * Y_STEPS_PER_MM;
}

bool requirementsMet() {
  return caneOccupancy && stainFull;
}

void printRequirements() {
  Serial.print("Requirements -> Occupancy: ");
  Serial.print(caneOccupancy ? "READY" : "NOT READY");
  Serial.print(" | Stain: ");
  Serial.println(stainFull ? "READY" : "NOT READY");
}

void enableDrivers() {
  if (USE_ENABLE_PINS) {
    digitalWrite(X_ENA_PIN, DRIVER_ENABLE_LEVEL);
    digitalWrite(Y_ENA_PIN, DRIVER_ENABLE_LEVEL);
  }
}

void disableDrivers() {
  if (USE_ENABLE_PINS) {
    digitalWrite(X_ENA_PIN, DRIVER_DISABLE_LEVEL);
    digitalWrite(Y_ENA_PIN, DRIVER_DISABLE_LEVEL);
  }
}

void clearSequence() {
  sequenceLength = 0;
  sequenceIndex = 0;
  waitingAtPoint = false;
}

void addSequencePoint(long x, long y, unsigned long dwellMs) {
  if (sequenceLength < MAX_SEQUENCE_POINTS) {
    sequencePoints[sequenceLength].x = x;
    sequencePoints[sequenceLength].y = y;
    sequencePoints[sequenceLength].dwellMs = dwellMs;
    sequenceLength++;
  }
}

void setCurrentTargetFromSequence() {
  targetXSteps = sequencePoints[sequenceIndex].x;
  targetYSteps = sequencePoints[sequenceIndex].y;
}

void setXDirectionToward(long target) {
  if (target > currentXSteps) {
    digitalWrite(X_DIR_PIN, X_POSITIVE_DIR);
  } else {
    digitalWrite(X_DIR_PIN, X_NEGATIVE_DIR);
  }
}

void setYDirectionToward(long target) {
  if (target > currentYSteps) {
    digitalWrite(Y_DIR_PIN, Y_POSITIVE_DIR);
  } else {
    digitalWrite(Y_DIR_PIN, Y_NEGATIVE_DIR);
  }
}

bool xAtTarget() {
  return currentXSteps == targetXSteps;
}

bool yAtTarget() {
  return currentYSteps == targetYSteps;
}

bool allAxesAtTarget() {
  return xAtTarget() && yAtTarget();
}

// ============================================================
// BUILD PAINTING SEQUENCE
// ============================================================
void addCanePaintSequence(long caneX) {
  long topY = yMmToSteps(CANE_HEIGHT_MM);
  long rowDropSteps = yMmToSteps(ROW_DROP_MM);

  // Move directly to the TOP of this cane
  // This makes the platform move diagonally if both X and Y need to change
  addSequencePoint(caneX, topY, COAT_DWELL_MS);   // row 1 coating at top

  // Lower in rows, pausing at each row
  long rowY = topY;

  while (rowY > 0) {
    rowY -= rowDropSteps;
    if (rowY < 0) rowY = 0;
    addSequencePoint(caneX, rowY, COAT_DWELL_MS);
  }
}

void buildRunSequence() {
  clearSequence();

  long cane1X = xMmToSteps(POS1_MM);
  long cane2X = xMmToSteps(POS1_MM + POS2_MM);
  long cane3X = xMmToSteps(POS1_MM + POS2_MM + POS3_MM);

  addCanePaintSequence(cane1X);
  addCanePaintSequence(cane2X);
  addCanePaintSequence(cane3X);

  // Return to start position
  addSequencePoint(0, 0, 0);

  sequenceIndex = 0;
  setCurrentTargetFromSequence();

  Serial.print("Sequence built. Total points: ");
  Serial.println(sequenceLength);
}

// ============================================================
// CONTROL ACTIONS
// ============================================================
void beginCycle() {
  buildRunSequence();
  systemState = RUNNING;
  enableDrivers();
  Serial.println("START pressed -> Cycle started.");
}

void beginStopReturnHome() {
  clearSequence();
  targetXSteps = 0;
  targetYSteps = 0;
  waitingAtPoint = false;
  systemState = STOPPING;
  enableDrivers();
  Serial.println("STOP pressed -> Returning both axes home.");
}

void finishAndGoIdle(const char* msg) {
  systemState = IDLE;
  clearSequence();

  targetXSteps = currentXSteps;
  targetYSteps = currentYSteps;

  xStepHigh = false;
  yStepHigh = false;

  digitalWrite(X_STEP_PIN, LOW);
  digitalWrite(Y_STEP_PIN, LOW);

  disableDrivers();

  Serial.println(msg);
}

void togglePause() {
  if (systemState == RUNNING) {
    systemState = PAUSED;
    pauseStartedMs = millis();

    xStepHigh = false;
    yStepHigh = false;
    digitalWrite(X_STEP_PIN, LOW);
    digitalWrite(Y_STEP_PIN, LOW);

    Serial.println("Paused.");
  } else if (systemState == PAUSED) {
    unsigned long pausedDurationMs = millis() - pauseStartedMs;

    waitStartMs += pausedDurationMs;
    lastXStepMicros += pausedDurationMs * 1000UL;
    lastYStepMicros += pausedDurationMs * 1000UL;

    systemState = RUNNING;
    Serial.println("Resumed.");
  }
}

// ============================================================
// BUTTON HANDLING
// ============================================================
void handleButtons() {
  if (occupancyBtn.pressed()) {
    caneOccupancy = !caneOccupancy;
    Serial.print("Cane occupancy toggled -> ");
    Serial.println(caneOccupancy ? "READY" : "NOT READY");
    printRequirements();
  }

  if (stainBtn.pressed()) {
    stainFull = !stainFull;
    Serial.print("Stain fullness toggled -> ");
    Serial.println(stainFull ? "READY" : "NOT READY");
    printRequirements();
  }

  if (startBtn.pressed()) {
    if (systemState == IDLE) {
      if (requirementsMet()) {
        beginCycle();
      } else {
        Serial.println("START pressed -> Cannot start. Requirements not met.");
        printRequirements();
      }
    } else {
      Serial.println("START pressed -> Ignored, system not idle.");
    }
  }

  if (pauseBtn.pressed()) {
    if (systemState == RUNNING || systemState == PAUSED) {
      togglePause();
    } else {
      Serial.println("PAUSE pressed -> Ignored, system not running.");
    }
  }

  if (stopBtn.pressed()) {
    if (systemState == RUNNING || systemState == PAUSED || systemState == STOPPING) {
      if (systemState == PAUSED) {
        unsigned long pausedDurationMs = millis() - pauseStartedMs;
        waitStartMs += pausedDurationMs;
        lastXStepMicros += pausedDurationMs * 1000UL;
        lastYStepMicros += pausedDurationMs * 1000UL;
      }
      beginStopReturnHome();
    } else {
      Serial.println("STOP pressed -> Already idle.");
    }
  }
}

// ============================================================
// MOTION SERVICE - X AXIS
// ============================================================
void serviceXMotion() {
  if (!(systemState == RUNNING || systemState == STOPPING)) return;
  if (waitingAtPoint) return;
  if (currentXSteps == targetXSteps) return;

  unsigned long nowUs = micros();

  if (!xStepHigh) {
    if (nowUs - lastXStepMicros >= X_STEP_INTERVAL_US) {
      setXDirectionToward(targetXSteps);
      digitalWrite(X_STEP_PIN, HIGH);
      xStepHigh = true;
      lastXStepMicros = nowUs;
    }
  } else {
    if (nowUs - lastXStepMicros >= STEP_HIGH_US) {
      digitalWrite(X_STEP_PIN, LOW);
      xStepHigh = false;

      if (targetXSteps > currentXSteps) currentXSteps++;
      else if (targetXSteps < currentXSteps) currentXSteps--;

      if (currentXSteps < 0) currentXSteps = 0;
      if (currentXSteps > X_MAX_STEPS) currentXSteps = X_MAX_STEPS;
    }
  }
}

// ============================================================
// MOTION SERVICE - Y AXIS
// ============================================================
void serviceYMotion() {
  if (!(systemState == RUNNING || systemState == STOPPING)) return;
  if (waitingAtPoint) return;
  if (currentYSteps == targetYSteps) return;

  unsigned long nowUs = micros();

  if (!yStepHigh) {
    if (nowUs - lastYStepMicros >= Y_STEP_INTERVAL_US) {
      setYDirectionToward(targetYSteps);
      digitalWrite(Y_STEP_PIN, HIGH);
      yStepHigh = true;
      lastYStepMicros = nowUs;
    }
  } else {
    if (nowUs - lastYStepMicros >= STEP_HIGH_US) {
      digitalWrite(Y_STEP_PIN, LOW);
      yStepHigh = false;

      if (targetYSteps > currentYSteps) currentYSteps++;
      else if (targetYSteps < currentYSteps) currentYSteps--;

      if (currentYSteps < 0) currentYSteps = 0;
      if (currentYSteps > Y_MAX_STEPS) currentYSteps = Y_MAX_STEPS;
    }
  }
}

// ============================================================
// SEQUENCE SERVICE
// ============================================================
void serviceSequence() {
  if (systemState == IDLE || systemState == PAUSED) return;

  if (systemState == STOPPING) {
    if (currentXSteps == 0 && currentYSteps == 0) {
      finishAndGoIdle("Stop complete -> Home reached.");
    }
    return;
  }

  // RUNNING
  if (!waitingAtPoint) {
    if (allAxesAtTarget()) {
      unsigned long dwell = sequencePoints[sequenceIndex].dwellMs;

      if (dwell > 0) {
        waitingAtPoint = true;
        waitStartMs = millis();

        Serial.print("Reached point ");
        Serial.print(sequenceIndex + 1);
        Serial.print("/");
        Serial.print(sequenceLength);
        Serial.print(" -> X=");
        Serial.print(currentXSteps);
        Serial.print(" Y=");
        Serial.print(currentYSteps);
        Serial.print(" | dwell ");
        Serial.print(dwell);
        Serial.println(" ms");
      } else {
        sequenceIndex++;

        if (sequenceIndex >= sequenceLength) {
          finishAndGoIdle("Cycle complete -> Returned to start.");
          return;
        }

        setCurrentTargetFromSequence();
      }
    }
  } else {
    if (millis() - waitStartMs >= sequencePoints[sequenceIndex].dwellMs) {
      waitingAtPoint = false;
      sequenceIndex++;

      if (sequenceIndex >= sequenceLength) {
        finishAndGoIdle("Cycle complete -> Returned to start.");
        return;
      }

      setCurrentTargetFromSequence();

      Serial.print("Moving to next point -> target X=");
      Serial.print(targetXSteps);
      Serial.print(" Y=");
      Serial.println(targetYSteps);
    }
  }
}

// ============================================================
// STATUS PRINTING
// ============================================================
void printStatusPeriodically() {
  static unsigned long lastPrint = 0;

  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();

    Serial.print("State: ");
    switch (systemState) {
      case IDLE:     Serial.print("IDLE"); break;
      case RUNNING:  Serial.print("RUNNING"); break;
      case PAUSED:   Serial.print("PAUSED"); break;
      case STOPPING: Serial.print("STOPPING"); break;
    }

    Serial.print(" | X: ");
    Serial.print(currentXSteps);
    Serial.print("/");
    Serial.print(X_MAX_STEPS);

    Serial.print(" | Y: ");
    Serial.print(currentYSteps);
    Serial.print("/");
    Serial.print(Y_MAX_STEPS);

    Serial.print(" | TargetX: ");
    Serial.print(targetXSteps);

    Serial.print(" | TargetY: ");
    Serial.print(targetYSteps);

    Serial.print(" | Occupancy: ");
    Serial.print(caneOccupancy ? "ON" : "OFF");

    Serial.print(" | Stain: ");
    Serial.println(stainFull ? "ON" : "OFF");
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(X_STEP_PIN, OUTPUT);
  pinMode(X_DIR_PIN, OUTPUT);
  pinMode(X_ENA_PIN, OUTPUT);

  pinMode(Y_STEP_PIN, OUTPUT);
  pinMode(Y_DIR_PIN, OUTPUT);
  pinMode(Y_ENA_PIN, OUTPUT);

  digitalWrite(X_STEP_PIN, LOW);
  digitalWrite(Y_STEP_PIN, LOW);

  digitalWrite(X_DIR_PIN, X_NEGATIVE_DIR);
  digitalWrite(Y_DIR_PIN, Y_NEGATIVE_DIR);

  if (USE_ENABLE_PINS) {
    disableDrivers();
  } else {
    digitalWrite(X_ENA_PIN, LOW);
    digitalWrite(Y_ENA_PIN, LOW);
  }

  startBtn.begin();
  pauseBtn.begin();
  stopBtn.begin();
  occupancyBtn.begin();
  stainBtn.begin();

  Serial.println("====================================================");
  Serial.println("2-Axis Gantry Proof of Concept Ready");
  Serial.println("Buttons:");
  Serial.println("START     -> begin cycle if requirements are met");
  Serial.println("PAUSE     -> pause / resume");
  Serial.println("STOP      -> abort and return home");
  Serial.println("OCCUPANCY -> toggle cane occupancy");
  Serial.println("STAIN     -> toggle stain fullness");
  Serial.println("====================================================");

  printRequirements();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  handleButtons();
  serviceXMotion();
  serviceYMotion();
  serviceSequence();
  printStatusPeriodically();
}


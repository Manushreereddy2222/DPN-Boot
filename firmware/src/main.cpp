#include <Arduino.h>

// ---------- PIN MAPPING (Wokwi LEDs replace motors) ----------
#define BUTTON_PIN 23
#define POWER_BUTTON_PIN 27
#define NUM_ZONES 4
const int motorPins[NUM_ZONES] = {4, 5, 18, 19};
const int fsrPins[NUM_ZONES]   = {34, 35, 36, 39};

// ---------- TIMING (tuned for quick demo) ----------
#define POKE_DURATION 200
#define RESPONSE_TIMEOUT 1500
#define DEBOUNCE_DELAY 30
#define INTER_TRIAL_DELAY 500
#define LONG_PRESS_DURATION 3000

// ---------- STAIRCASE PARAMS ----------
#define STEP_SIZE 40
#define MIN_INTENSITY 0
#define MAX_INTENSITY 255
#define REVERSALS_NEEDED 3
#define START_INTENSITY 128

struct Zone {
  int motorPin;
  int fsrPin;
  bool lastResult;
  int intensity;
  int lastDirection;
  int reversalCount;
  int reversalHistory[REVERSALS_NEEDED];
  int reversalFSR[REVERSALS_NEEDED];
  bool active;
};

Zone zones[NUM_ZONES];

enum TestState { IDLE, FIRE, WAIT_RESPONSE, LOG_RESULT, TEST_COMPLETE };
TestState currentState = IDLE;

int currentZone = -1;
int lastZone = -1;
unsigned long stateStartTime = 0;
unsigned long responseTimestamp = 0;
bool responseReceived = false;

bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;

bool systemOn = true;
unsigned long powerButtonPressStart = 0;
bool powerButtonHandled = false;
int trialCount = 0;

// ---------- FSR CALIBRATION ----------
const int CAL_POINTS = 5;
int calRaw[CAL_POINTS]     = {0,   300,  600,  1400, 2800};
float calGrams[CAL_POINTS] = {0,   5,    10,   20,   50};

float rawToGrams(int raw) {
  if (raw <= calRaw[0]) return calGrams[0];
  if (raw >= calRaw[CAL_POINTS - 1]) return calGrams[CAL_POINTS - 1];
  for (int i = 0; i < CAL_POINTS - 1; i++) {
    if (raw >= calRaw[i] && raw <= calRaw[i + 1]) {
      float ratio = (float)(raw - calRaw[i]) / (calRaw[i + 1] - calRaw[i]);
      return calGrams[i] + ratio * (calGrams[i + 1] - calGrams[i]);
    }
  }
  return 0;
}

void setMotor(int pin, int intensity) {
  analogWrite(pin, intensity);
}

void checkPowerButton() {
  bool reading = digitalRead(POWER_BUTTON_PIN);
  if (reading == LOW) {
    if (powerButtonPressStart == 0) {
      powerButtonPressStart = millis();
    } else if (!powerButtonHandled && (millis() - powerButtonPressStart > LONG_PRESS_DURATION)) {
      systemOn = !systemOn;
      powerButtonHandled = true;
      Serial.println(systemOn ? "SYSTEM ON" : "SYSTEM OFF");
    }
  } else {
    powerButtonPressStart = 0;
    powerButtonHandled = false;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);

  for (int i = 0; i < NUM_ZONES; i++) {
    pinMode(motorPins[i], OUTPUT);
    zones[i].motorPin = motorPins[i];
    zones[i].fsrPin = fsrPins[i];
    zones[i].lastResult = false;
    zones[i].intensity = START_INTENSITY;
    zones[i].lastDirection = 0;
    zones[i].reversalCount = 0;
    zones[i].active = true;
    for (int r = 0; r < REVERSALS_NEEDED; r++) {
      zones[i].reversalHistory[r] = 0;
      zones[i].reversalFSR[r] = 0;
    }
  }

  randomSeed(analogRead(0));
  Serial.println("=== DPN Boot Sensitivity Test ===");
  Serial.println("Press GREEN button when you feel the stimulus.");
  Serial.println("Potentiometers simulate FSR pressure sensors.");
  Serial.println("----------------------------------");
}

void loop() {
  checkPowerButton();
  if (!systemOn) return;

  switch (currentState) {

    case IDLE: {
      bool anyActive = false;
      for (int i = 0; i < NUM_ZONES; i++) if (zones[i].active) anyActive = true;
      if (!anyActive) { currentState = TEST_COMPLETE; break; }

      int newZone;
      int attempts = 0;
      do {
        newZone = random(0, NUM_ZONES);
        attempts++;
        if (attempts > 20) break;
      } while ((newZone == lastZone || !zones[newZone].active) && attempts <= 20);

      if (!zones[newZone].active) {
        for (int i = 0; i < NUM_ZONES; i++) {
          if (zones[i].active) { newZone = i; break; }
        }
      }

      currentZone = newZone;
      lastZone = newZone;
      trialCount++;
      Serial.println();
      Serial.print(">>> Trial ");
      Serial.print(trialCount);
      Serial.print(" | Testing Zone ");
      Serial.print(currentZone);
      Serial.print(" at intensity ");
      Serial.println(zones[currentZone].intensity);
      currentState = FIRE;
      break;
    }

    case FIRE: {
      setMotor(zones[currentZone].motorPin, zones[currentZone].intensity);
      stateStartTime = millis();
      responseReceived = false;
      currentState = WAIT_RESPONSE;
      break;
    }

    case WAIT_RESPONSE: {
      if (millis() - stateStartTime > POKE_DURATION) {
        setMotor(zones[currentZone].motorPin, 0);
      }

      bool reading = digitalRead(BUTTON_PIN);
      if (reading != lastButtonState) lastDebounceTime = millis();

      if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
        if (reading == LOW && !responseReceived) {
          responseReceived = true;
          responseTimestamp = millis();
          lastButtonState = reading;
          currentState = LOG_RESULT;
          break;
        }
      }
      lastButtonState = reading;

      if (millis() - stateStartTime > RESPONSE_TIMEOUT) currentState = LOG_RESULT;
      break;
    }

    case LOG_RESULT: {
      int fsrValue = analogRead(zones[currentZone].fsrPin);
      float fsrGrams = rawToGrams(fsrValue);
      unsigned long reactionTime = responseReceived ? (responseTimestamp - stateStartTime) : 0;
      zones[currentZone].lastResult = responseReceived;

      int currentDirection = responseReceived ? -1 : 1;
      if (zones[currentZone].lastDirection != 0 &&
          currentDirection != zones[currentZone].lastDirection &&
          zones[currentZone].reversalCount < REVERSALS_NEEDED) {
        zones[currentZone].reversalHistory[zones[currentZone].reversalCount] = zones[currentZone].intensity;
        zones[currentZone].reversalFSR[zones[currentZone].reversalCount] = fsrValue;
        zones[currentZone].reversalCount++;
        if (zones[currentZone].reversalCount >= REVERSALS_NEEDED) {
          zones[currentZone].active = false;
          Serial.print("    *** Zone ");
          Serial.print(currentZone);
          Serial.println(" COMPLETE ***");
        }
      }
      zones[currentZone].lastDirection = currentDirection;

      if (responseReceived) zones[currentZone].intensity -= STEP_SIZE;
      else zones[currentZone].intensity += STEP_SIZE;
      if (zones[currentZone].intensity < MIN_INTENSITY) zones[currentZone].intensity = MIN_INTENSITY;
      if (zones[currentZone].intensity > MAX_INTENSITY) zones[currentZone].intensity = MAX_INTENSITY;

      String line = "Zone: " + String(currentZone) +
                    " | Felt: " + (responseReceived ? "YES" : "NO") +
                    " | Intensity: " + String(zones[currentZone].intensity) +
                    " | Reversals: " + String(zones[currentZone].reversalCount) + "/" + String(REVERSALS_NEEDED) +
                    " | FSR raw: " + String(fsrValue) +
                    " | FSR grams: " + String(fsrGrams, 1) +
                    " | Reaction time (ms): " + String(reactionTime);
      Serial.println(line);

      delay(INTER_TRIAL_DELAY);
      currentState = IDLE;
      break;
    }

    case TEST_COMPLETE: {
      Serial.println();
      Serial.println("====================================");
      Serial.println("       TEST COMPLETE — RESULTS      ");
      Serial.println("====================================");

      for (int i = 0; i < NUM_ZONES; i++) {
        int sum = 0, fsrSum = 0;
        for (int r = 0; r < REVERSALS_NEEDED; r++) {
          sum += zones[i].reversalHistory[r];
          fsrSum += zones[i].reversalFSR[r];
        }
        float threshold = sum / (float)REVERSALS_NEEDED;
        float fsrGramsThreshold = rawToGrams((int)(fsrSum / (float)REVERSALS_NEEDED));

        String result = "Zone " + String(i) +
                        " | Intensity threshold: " + String(threshold, 1) +
                        " | FSR threshold: " + String(fsrGramsThreshold, 1) + "g";
        Serial.println(result);
      }

      Serial.println("====================================");
      Serial.println("Test ended. Reset ESP32 to re-run.");

      while (true) delay(1000);
      break;
    }
  }
}

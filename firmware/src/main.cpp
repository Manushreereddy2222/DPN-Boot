#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "BluetoothSerial.h"

BluetoothSerial SerialBT;
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

// ---------- CONFIG ----------
#define BUTTON_PIN 23
#define POWER_BUTTON_PIN 27
#define NUM_ZONES 4
#define POKE_DURATION 200
#define RESPONSE_TIMEOUT 3000
#define DEBOUNCE_DELAY 30
#define STEP_SIZE 20
#define MIN_INTENSITY 0     // TODO: real motor's minimum spin PWM
#define MAX_INTENSITY 255
#define REVERSALS_NEEDED 6
#define LONG_PRESS_DURATION 3000

struct Zone {
  int motorChannel;   // PCA9685 channel, 0-3 (not a GPIO pin anymore)
  int fsrPin;
  bool lastResult;
  int intensity;
  int lastDirection;
  int reversalCount;
  int reversalHistory[6];
  int reversalFSR[6];
  bool active;
};

Zone zones[NUM_ZONES] = {
  {0, 34, false, 200, 0, 0, {0,0,0,0,0,0}, {0,0,0,0,0,0}, true},
  {1, 35, false, 200, 0, 0, {0,0,0,0,0,0}, {0,0,0,0,0,0}, true},
  {2, 36, false, 200, 0, 0, {0,0,0,0,0,0}, {0,0,0,0,0,0}, true},
  {3, 39, false, 200, 0, 0, {0,0,0,0,0,0}, {0,0,0,0,0,0}, true}
};

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

// ---------- FSR CALIBRATION (placeholder — replace after Phase 4) ----------
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

// Sets a motor's intensity via PCA9685 (0-255 input, converted to 12-bit)
void setMotor(int channel, int intensity) {
  int duty = map(intensity, 0, 255, 0, 4095);
  pwm.setPWM(channel, 0, duty);
}

void checkPowerButton() {
  bool reading = digitalRead(POWER_BUTTON_PIN);
  if (reading == LOW) {
    if (powerButtonPressStart == 0) {
      powerButtonPressStart = millis();
    } else if (!powerButtonHandled && (millis() - powerButtonPressStart > LONG_PRESS_DURATION)) {
      systemOn = !systemOn;
      powerButtonHandled = true;
      String msg = systemOn ? "SYSTEM ON" : "SYSTEM OFF";
      Serial.println(msg);
      SerialBT.println(msg);
    }
  } else {
    powerButtonPressStart = 0;
    powerButtonHandled = false;
  }
}

void setup() {
  Serial.begin(115200);
  SerialBT.begin("DPN_Boot");
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);

  Wire.begin();          // default ESP32 I2C pins: SDA=21, SCL=22
  pwm.begin();
  pwm.setPWMFreq(1000);  // 1kHz — avoids audible motor whine

  randomSeed(analogRead(0));
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
      do {
        newZone = random(0, NUM_ZONES);
      } while (newZone == lastZone || !zones[newZone].active);

      currentZone = newZone;
      lastZone = newZone;
      currentState = FIRE;
      break;
    }

    case FIRE: {
      setMotor(zones[currentZone].motorChannel, zones[currentZone].intensity);
      stateStartTime = millis();
      responseReceived = false;
      currentState = WAIT_RESPONSE;
      break;
    }

    case WAIT_RESPONSE: {
      if (millis() - stateStartTime > POKE_DURATION) {
        setMotor(zones[currentZone].motorChannel, 0);
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
        if (zones[currentZone].reversalCount >= REVERSALS_NEEDED) zones[currentZone].active = false;
      }
      zones[currentZone].lastDirection = currentDirection;

      if (responseReceived) zones[currentZone].intensity -= STEP_SIZE;
      else zones[currentZone].intensity += STEP_SIZE;
      if (zones[currentZone].intensity < MIN_INTENSITY) zones[currentZone].intensity = MIN_INTENSITY;
      if (zones[currentZone].intensity > MAX_INTENSITY) zones[currentZone].intensity = MAX_INTENSITY;

      String line = "Zone: " + String(currentZone) +
                    " | Felt: " + (responseReceived ? "YES" : "NO") +
                    " | Intensity: " + String(zones[currentZone].intensity) +
                    " | Reversals: " + String(zones[currentZone].reversalCount) +
                    " | FSR raw: " + String(fsrValue) +
                    " | FSR grams: " + String(fsrGrams, 1) +
                    " | Reaction time (ms): " + String(reactionTime);
      Serial.println(line);
      SerialBT.println(line);

      delay(1000);
      currentState = IDLE;
      break;
    }

    case TEST_COMPLETE: {
      String header = "\n--- TEST COMPLETE ---";
      Serial.println(header);
      SerialBT.println(header);

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
        SerialBT.println(result);
      }

      while (true) delay(1000);
      break;
    }
  }
}
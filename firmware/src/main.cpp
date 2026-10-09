#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "BluetoothSerial.h"

BluetoothSerial SerialBT;
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

// ===================== LINKED LIST =====================
struct TrialNode {
  int intensity;
  int fsrRaw;
  bool felt;
  TrialNode* next;

  TrialNode(int i, int f, bool fe) : intensity(i), fsrRaw(f), felt(fe), next(nullptr) {}
};

class TrialList {
private:
  TrialNode* head;
  int count;

public:
  TrialList() : head(nullptr), count(0) {}

  void append(int intensity, int fsrRaw, bool felt) {
    TrialNode* node = new TrialNode(intensity, fsrRaw, felt);
    if (!head) {
      head = node;
    } else {
      TrialNode* curr = head;
      while (curr->next) curr = curr->next;
      curr->next = node;
    }
    count++;
  }

  int size() { return count; }
  TrialNode* getHead() { return head; }

  int sumIntensity(TrialNode* node) {
    if (!node) return 0;
    return node->intensity + sumIntensity(node->next);
  }

  int sumFSR(TrialNode* node) {
    if (!node) return 0;
    return node->fsrRaw + sumFSR(node->next);
  }

  float avgIntensity() {
    if (count == 0) return 0;
    return sumIntensity(head) / (float)count;
  }

  float avgFSR() {
    if (count == 0) return 0;
    return sumFSR(head) / (float)count;
  }

  ~TrialList() {
    TrialNode* curr = head;
    while (curr) {
      TrialNode* tmp = curr;
      curr = curr->next;
      delete tmp;
    }
  }
};

// ===================== BASE CLASS (Inheritance) =====================
class HardwareComponent {
protected:
  int pin;
  String name;

public:
  HardwareComponent(int p, String n) : pin(p), name(n) {}
  virtual void init() = 0;
  int getPin() { return pin; }
  String getName() { return name; }
};

// ===================== FSR SENSOR (Inherits HardwareComponent) =====================
class FSRSensor : public HardwareComponent {
private:
  static const int CAL_POINTS = 5;
  int calRaw[CAL_POINTS]     = {0,   300,  600,  1400, 2800};
  float calGrams[CAL_POINTS] = {0.0, 5.0,  10.0, 20.0, 50.0};

  float interpolate(int raw, int index) {
    if (index >= CAL_POINTS - 1) return calGrams[CAL_POINTS - 1];
    if (raw >= calRaw[index] && raw <= calRaw[index + 1]) {
      float ratio = (float)(raw - calRaw[index]) / (calRaw[index + 1] - calRaw[index]);
      return calGrams[index] + ratio * (calGrams[index + 1] - calGrams[index]);
    }
    return interpolate(raw, index + 1);
  }

public:
  FSRSensor(int p, String n) : HardwareComponent(p, n) {}

  void init() override {}

  int readRaw() {
    return analogRead(pin);
  }

  float rawToGrams(int raw) {
    if (raw <= calRaw[0]) return calGrams[0];
    if (raw >= calRaw[CAL_POINTS - 1]) return calGrams[CAL_POINTS - 1];
    return interpolate(raw, 0);
  }
};

// ===================== MOTOR (Inherits HardwareComponent) =====================
class MotorActuator : public HardwareComponent {
private:
  int channel;

public:
  MotorActuator(int ch, int p, String n) : HardwareComponent(p, n), channel(ch) {}

  void init() override {}

  void setIntensity(int intensity) {
    int duty = map(intensity, 0, 255, 0, 4095);
    pwm.setPWM(channel, 0, duty);
  }

  void stop() {
    pwm.setPWM(channel, 0, 0);
  }
};

// ===================== ZONE CLASS =====================
#define STEP_SIZE 20
#define MIN_INTENSITY 0
#define MAX_INTENSITY 255
#define REVERSALS_NEEDED 6
#define START_INTENSITY 200

class Zone {
private:
  int id;
  FSRSensor* sensor;
  MotorActuator* motor;
  int intensity;
  int lastDirection;
  int reversalCount;
  bool active;
  TrialList reversals;

public:
  Zone() : id(0), sensor(nullptr), motor(nullptr), intensity(START_INTENSITY),
           lastDirection(0), reversalCount(0), active(true) {}

  void configure(int zoneId, FSRSensor* s, MotorActuator* m) {
    id = zoneId;
    sensor = s;
    motor = m;
  }

  bool isActive() { return active; }
  int getId() { return id; }
  int getIntensity() { return intensity; }
  FSRSensor* getSensor() { return sensor; }
  MotorActuator* getMotor() { return motor; }

  void fire() {
    motor->setIntensity(intensity);
  }

  void stopMotor() {
    motor->stop();
  }

  void processResult(bool felt, int fsrRaw) {
    int currentDirection = felt ? -1 : 1;

    if (lastDirection != 0 && currentDirection != lastDirection && reversalCount < REVERSALS_NEEDED) {
      reversals.append(intensity, fsrRaw, felt);
      reversalCount++;
      if (reversalCount >= REVERSALS_NEEDED) {
        active = false;
      }
    }
    lastDirection = currentDirection;

    if (felt) intensity -= STEP_SIZE;
    else intensity += STEP_SIZE;
    if (intensity < MIN_INTENSITY) intensity = MIN_INTENSITY;
    if (intensity > MAX_INTENSITY) intensity = MAX_INTENSITY;
  }

  int getReversalCount() { return reversalCount; }

  float getThresholdIntensity() {
    return reversals.avgIntensity();
  }

  float getThresholdFSRRaw() {
    return reversals.avgFSR();
  }
};

// ===================== BUTTON CLASS =====================
class Button : public HardwareComponent {
private:
  bool lastState;
  unsigned long lastDebounce;
  unsigned long debounceDelay;

public:
  Button(int p, String n, unsigned long deb = 30)
    : HardwareComponent(p, n), lastState(HIGH), lastDebounce(0), debounceDelay(deb) {}

  void init() override {
    pinMode(pin, INPUT_PULLUP);
  }

  bool pressed() {
    bool reading = digitalRead(pin);
    if (reading != lastState) lastDebounce = millis();
    bool result = false;
    if ((millis() - lastDebounce) > debounceDelay) {
      if (reading == LOW && lastState == HIGH) {
        result = true;
      }
    }
    lastState = reading;
    return result;
  }
};

// ===================== GLOBALS =====================
#define NUM_ZONES 4
#define POKE_DURATION 200
#define RESPONSE_TIMEOUT 3000
#define LONG_PRESS_DURATION 3000

FSRSensor sensors[NUM_ZONES] = {
  FSRSensor(34, "FSR_0"), FSRSensor(35, "FSR_1"),
  FSRSensor(36, "FSR_2"), FSRSensor(39, "FSR_3")
};

MotorActuator motors[NUM_ZONES] = {
  MotorActuator(0, 0, "Motor_0"), MotorActuator(1, 1, "Motor_1"),
  MotorActuator(2, 2, "Motor_2"), MotorActuator(3, 3, "Motor_3")
};

Zone zones[NUM_ZONES];
Button responseBtn(23, "Response");
Button powerBtn(27, "Power");

enum TestState { IDLE, FIRE, WAIT_RESPONSE, LOG_RESULT, TEST_COMPLETE };
TestState currentState = IDLE;

int currentZone = -1;
int lastZone = -1;
unsigned long stateStartTime = 0;
unsigned long responseTimestamp = 0;
bool responseReceived = false;
bool systemOn = true;
unsigned long powerPressStart = 0;
bool powerHandled = false;

void output(String msg) {
  Serial.println(msg);
  SerialBT.println(msg);
}

void checkPower() {
  bool reading = digitalRead(27);
  if (reading == LOW) {
    if (powerPressStart == 0) powerPressStart = millis();
    else if (!powerHandled && (millis() - powerPressStart > LONG_PRESS_DURATION)) {
      systemOn = !systemOn;
      powerHandled = true;
      output(systemOn ? "SYSTEM ON" : "SYSTEM OFF");
    }
  } else {
    powerPressStart = 0;
    powerHandled = false;
  }
}

void setup() {
  Serial.begin(115200);
  SerialBT.begin("DPN_Boot");

  Wire.begin();
  pwm.begin();
  pwm.setPWMFreq(1000);

  for (int i = 0; i < NUM_ZONES; i++) {
    sensors[i].init();
    motors[i].init();
    zones[i].configure(i, &sensors[i], &motors[i]);
  }
  responseBtn.init();
  powerBtn.init();

  randomSeed(analogRead(0));
}

void loop() {
  checkPower();
  if (!systemOn) return;

  switch (currentState) {

    case IDLE: {
      bool anyActive = false;
      for (int i = 0; i < NUM_ZONES; i++) if (zones[i].isActive()) anyActive = true;
      if (!anyActive) { currentState = TEST_COMPLETE; break; }

      int newZone;
      do {
        newZone = random(0, NUM_ZONES);
      } while (newZone == lastZone || !zones[newZone].isActive());

      currentZone = newZone;
      lastZone = newZone;
      currentState = FIRE;
      break;
    }

    case FIRE: {
      zones[currentZone].fire();
      stateStartTime = millis();
      responseReceived = false;
      currentState = WAIT_RESPONSE;
      break;
    }

    case WAIT_RESPONSE: {
      if (millis() - stateStartTime > POKE_DURATION) {
        zones[currentZone].stopMotor();
      }

      if (responseBtn.pressed() && !responseReceived) {
        responseReceived = true;
        responseTimestamp = millis();
        currentState = LOG_RESULT;
        break;
      }

      if (millis() - stateStartTime > RESPONSE_TIMEOUT) currentState = LOG_RESULT;
      break;
    }

    case LOG_RESULT: {
      FSRSensor* sensor = zones[currentZone].getSensor();
      int fsrValue = sensor->readRaw();
      float fsrGrams = sensor->rawToGrams(fsrValue);
      unsigned long reactionTime = responseReceived ? (responseTimestamp - stateStartTime) : 0;

      zones[currentZone].processResult(responseReceived, fsrValue);

      String line = "Zone: " + String(currentZone) +
                    " | Felt: " + (responseReceived ? "YES" : "NO") +
                    " | Intensity: " + String(zones[currentZone].getIntensity()) +
                    " | Reversals: " + String(zones[currentZone].getReversalCount()) +
                    " | FSR raw: " + String(fsrValue) +
                    " | FSR grams: " + String(fsrGrams, 1) +
                    " | Reaction time (ms): " + String(reactionTime);
      output(line);

      delay(1000);
      currentState = IDLE;
      break;
    }

    case TEST_COMPLETE: {
      String header = "\n--- TEST COMPLETE ---";
      output(header);

      for (int i = 0; i < NUM_ZONES; i++) {
        float threshold = zones[i].getThresholdIntensity();
        float fsrAvgRaw = zones[i].getThresholdFSRRaw();
        float fsrGramsThreshold = sensors[i].rawToGrams((int)fsrAvgRaw);

        String result = "Zone " + String(i) +
                        " | Intensity threshold: " + String(threshold, 1) +
                        " | FSR threshold: " + String(fsrGramsThreshold, 1) + "g";
        output(result);
      }

      while (true) delay(1000);
      break;
    }
  }
}

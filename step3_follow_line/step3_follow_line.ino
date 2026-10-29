// STEP 3: Follow the line.
#include <ECE3.h>

uint16_t sensors[8];

const int sensorMin[8] = { 798, 688, 642, 635, 612, 688, 735, 853 };
const int sensorMax[8] = { 2500, 2500, 2500, 2016, 2213, 2162, 2500, 2500 };

const int LEFT_SLEEP  = 31;
const int LEFT_DIR    = 29;
const int LEFT_PWM    = 40;
const int RIGHT_SLEEP = 11;
const int RIGHT_DIR   = 30;
const int RIGHT_PWM   = 39;

// ---- Numbers you can change ----
const int   BASE_SPEED = 50;     // how fast it drives
const float KP         = 0.014;  // how hard it steers
// ---------------------------------

long lastError = 0;
unsigned long lostTime = 0;
int allBlackCount = 0;

// speed above 0 = forward, below 0 = backward
void setWheel(int dirPin, int pwmPin, int speed) {
  if (speed >= 0) {
    digitalWrite(dirPin, LOW);
    analogWrite(pwmPin, speed);
  } else {
    digitalWrite(dirPin, HIGH);
    analogWrite(pwmPin, -speed);
  }
}

void drive(int left, int right) {
  setWheel(LEFT_DIR, LEFT_PWM, left);
  setWheel(RIGHT_DIR, RIGHT_PWM, right);
}

void setup() {
  ECE3_Init();

  pinMode(LEFT_SLEEP, OUTPUT);
  pinMode(LEFT_DIR, OUTPUT);
  pinMode(LEFT_PWM, OUTPUT);
  pinMode(RIGHT_SLEEP, OUTPUT);
  pinMode(RIGHT_DIR, OUTPUT);
  pinMode(RIGHT_PWM, OUTPUT);

  drive(0, 0);
  digitalWrite(LEFT_SLEEP, HIGH);
  digitalWrite(RIGHT_SLEEP, HIGH);

  delay(3000);   // 3 seconds to put the robot down
}

void loop() {
  ECE3_read_IR(sensors);

  long weighted = 0;
  long total = 0;
  int blackSensors = 0;

  // Sensor 0 is on the RIGHT, sensor 7 on the LEFT
  for (int i = 0; i < 8; i++) {
    long v = ((long)sensors[i] - sensorMin[i]) * 1000L / (sensorMax[i] - sensorMin[i]);
    v = constrain(v, 0L, 1000L);
    if (v < 300) v = 0;
    if (v > 0) blackSensors++;
    weighted += v * i * 1000L;
    total += v;
  }

  // All sensors black (end of track, or robot picked up): stop
  if (blackSensors >= 7) {
    allBlackCount++;
    if (allBlackCount >= 3) {
      drive(0, 0);
      return;
    }
  } else {
    allBlackCount = 0;
  }

  // No line seen: turn toward where the line was last seen, give up after 2 seconds
  if (total == 0) {
    if (lostTime == 0) lostTime = millis();
    if (millis() - lostTime > 2000) {
      drive(0, 0);
    } else if (lastError > 0) {
      drive(45, -45);      // line was on the right: spin right
    } else {
      drive(-45, 45);      // line was on the left: spin left
    }
    return;
  }
  lostTime = 0;

  // error > 0 means the line is to the RIGHT
  long error = 3500 - weighted / total;
  lastError = error;

  int correction = KP * error;
  int left  = constrain(BASE_SPEED + correction, 0, 100);
  int right = constrain(BASE_SPEED - correction, 0, 100);
  drive(left, right);
}

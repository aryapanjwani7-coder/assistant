// STEP 4: Full course.
// Drive over the start bar -> follow the line -> U-turn at the end bar ->
// follow the line back -> stop at the start bar.
// The line following itself is exactly the same as Step 3.
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

const int LED_FRONT_RIGHT = 41;
const int LED_FRONT_LEFT  = 51;

// ---- Numbers you can change ----
const int   BASE_SPEED = 50;     // how fast it drives
const float KP         = 0.014;  // how hard it steers
const int   SPIN_SPEED = 50;     // how fast it spins for the U-turn
// ---------------------------------

// What the robot is doing right now
const int START = 0;   // driving straight over the start bar
const int OUT   = 1;   // following the line to the end bar
const int UTURN = 2;   // spinning around at the end bar
const int BACK  = 3;   // following the line back to the start bar
const int DONE  = 4;   // stopped at the start bar

int phase = START;
unsigned long phaseStart = 0;   // when the current phase began

long weighted = 0;
long total = 0;
int blackSensors = 0;
bool centerBlack = false;       // middle sensors (3 and 4) see black

long lastError = 0;
unsigned long lostTime = 0;
int allBlackCount = 0;
bool sawStartBar = false;
bool everSawLine = false;
bool offEndBar = false;

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

void setPhase(int p) {
  phase = p;
  phaseStart = millis();
}

void readSensors() {
  ECE3_read_IR(sensors);

  weighted = 0;
  total = 0;
  blackSensors = 0;
  centerBlack = false;

  // Sensor 0 is on the RIGHT, sensor 7 on the LEFT
  for (int i = 0; i < 8; i++) {
    long v = ((long)sensors[i] - sensorMin[i]) * 1000L / (sensorMax[i] - sensorMin[i]);
    v = constrain(v, 0L, 1000L);
    if (v < 300) v = 0;
    if (v > 0) blackSensors++;
    if (v > 0 && (i == 3 || i == 4)) centerBlack = true;
    weighted += v * i * 1000L;
    total += v;
  }
}

void setup() {
  ECE3_Init();

  pinMode(LEFT_SLEEP, OUTPUT);
  pinMode(LEFT_DIR, OUTPUT);
  pinMode(LEFT_PWM, OUTPUT);
  pinMode(RIGHT_SLEEP, OUTPUT);
  pinMode(RIGHT_DIR, OUTPUT);
  pinMode(RIGHT_PWM, OUTPUT);
  pinMode(LED_FRONT_RIGHT, OUTPUT);
  pinMode(LED_FRONT_LEFT, OUTPUT);

  drive(0, 0);
  digitalWrite(LEFT_SLEEP, HIGH);
  digitalWrite(RIGHT_SLEEP, HIGH);

  delay(3000);   // 3 seconds to put the robot down
  setPhase(START);
}

void loop() {
  readSensors();
  unsigned long timeInPhase = millis() - phaseStart;

  // ---------- DONE: stay stopped ----------
  if (phase == DONE) {
    drive(0, 0);
    return;
  }

  // ---------- START: drive straight over the start bar ----------
  if (phase == START) {
    drive(BASE_SPEED, BASE_SPEED);
    if (blackSensors >= 4) sawStartBar = true;
    // Done when we have crossed the bar, or after 1.5 s anyway
    if ((sawStartBar && blackSensors <= 3) || timeInPhase > 1500) {
      setPhase(OUT);
    }
    return;
  }

  // ---------- UTURN: spin in place until the line is under the middle sensors ----------
  if (phase == UTURN) {
    drive(-SPIN_SPEED, SPIN_SPEED);           // spin left
    if (blackSensors == 0) offEndBar = true;  // sensors have swung off the end bar
    if (offEndBar && centerBlack) {           // found the line we came in on
      lastError = 0;
      lostTime = 0;
      digitalWrite(LED_FRONT_LEFT, HIGH);     // front LEDs on = on the way back
      digitalWrite(LED_FRONT_RIGHT, HIGH);
      setPhase(BACK);
    } else if (timeInPhase > 5000) {          // spun 5 s without finding it: give up
      setPhase(DONE);
    }
    return;
  }

  // ---------- OUT or BACK: watch for a bar ----------
  // Bars are ignored for 1 s after the start / U-turn. 3 readings in a row are
  // needed so one glitchy reading ("phantom crosspiece") does not count.
  if (blackSensors >= 7 && timeInPhase > 1000) {
    allBlackCount++;
    if (allBlackCount >= 3) {
      allBlackCount = 0;
      drive(0, 0);
      if (phase == OUT) {
        delay(200);             // let the robot settle before spinning
        offEndBar = false;
        setPhase(UTURN);
      } else {
        setPhase(DONE);         // back at the start bar
      }
      return;
    }
  } else {
    allBlackCount = 0;
  }

  // ---------- Follow the line (same as Step 3) ----------
  if (total == 0) {
    // Not found the line yet after the start: keep going straight
    if (!everSawLine) {
      drive(BASE_SPEED, BASE_SPEED);
      return;
    }
    // Lost the line: turn toward where it was last seen, give up after 2 seconds
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
  everSawLine = true;

  // error > 0 means the line is to the RIGHT
  long error = 3500 - weighted / total;
  lastError = error;

  int correction = KP * error;
  int left  = constrain(BASE_SPEED + correction, 0, 100);
  int right = constrain(BASE_SPEED - correction, 0, 100);
  drive(left, right);
}

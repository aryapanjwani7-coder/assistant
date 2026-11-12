// STEP 4: Full course.
// Drive over the start bar -> follow the line -> U-turn at the end bar ->
// follow the line back -> stop at the start bar.
// Line following is Step 3 plus:
//   - on sharp turns the inner wheel goes backwards
//   - if two lines are under the sensors (hairpins), it keeps the one it was following
//   - stricter end-bar check so hairpins are not mistaken for the end bar
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
const int LED_BACK_LEFT   = 57;
const int LED_BACK_RIGHT  = 58;

// ---- Numbers you can change ----
const int   BASE_SPEED = 50;     // how fast it drives
const float KP         = 0.014;  // how hard it steers
const int   SPIN_SPEED = 50;     // how fast it spins for the U-turn

// Sharp turns: when the line reaches the outer sensors, turn much harder
// so the inner wheel goes BACKWARDS. Set KP_SHARP = 0 to switch this off.
const int   SHARP_ERROR = 2000;  // line under the outer 2 sensors on either side
const float KP_SHARP    = 0.05;  // extra steering beyond SHARP_ERROR
const int   MAX_REVERSE = 50;    // fastest a wheel may go backwards

// Bars (end bar, start bar) are ignored for this long after the start and after
// the U-turn. The robot cannot reach a bar sooner than this.
const unsigned long BAR_IGNORE_MS = 10000;
// ---------------------------------

// What the robot is doing right now
const int START = 0;   // driving straight over the start bar
const int OUT   = 1;   // following the line to the end bar
const int UTURN = 2;   // spinning around at the end bar
const int BACK  = 3;   // following the line back to the start bar
const int DONE  = 4;   // stopped at the start bar

int phase = START;
unsigned long phaseStart = 0;   // when the current phase began

int  v[8];                      // each sensor: 0 = white .. 1000 = black
long weighted = 0;              // sums for the line we are following
long total = 0;
int  blackSensors = 0;          // how many sensors see black
int  strongBlack = 0;           // how many sensors see very black
int  lines = 0;                 // how many separate lines are under the sensors
bool centerBlack = false;       // middle sensors (3 and 4) see black

long lastPosition = 3500;       // where the line was last time (0 = right, 7000 = left)
long lastError = 0;
unsigned long lostTime = 0;
int  allBlackCount = 0;
int  straightCount = 0;         // readings in a row driving straight on a single line
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

  blackSensors = 0;
  strongBlack = 0;
  centerBlack = false;

  // Sensor 0 is on the RIGHT, sensor 7 on the LEFT
  for (int i = 0; i < 8; i++) {
    long x = ((long)sensors[i] - sensorMin[i]) * 1000L / (sensorMax[i] - sensorMin[i]);
    x = constrain(x, 0L, 1000L);
    if (x < 300) x = 0;
    v[i] = x;
    if (x > 0) blackSensors++;
    if (x > 700) strongBlack++;
    if (x > 0 && (i == 3 || i == 4)) centerBlack = true;
  }

  // Find each separate line under the sensors. If there are two (at hairpins,
  // where the track runs close to itself), keep the one nearest to where the
  // line was last time, so the robot does not jump onto the wrong piece of track.
  weighted = 0;
  total = 0;
  lines = 0;
  long bestDistance = 100000;
  int i = 0;
  while (i < 8) {
    if (v[i] == 0) {
      i++;
      continue;
    }
    long w = 0;
    long t = 0;
    while (i < 8 && v[i] > 0) {
      w += (long)v[i] * i * 1000L;
      t += v[i];
      i++;
    }
    lines++;
    long distance = labs(w / t - lastPosition);
    if (distance < bestDistance) {
      bestDistance = distance;
      weighted = w;
      total = t;
    }
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
  pinMode(LED_BACK_LEFT, OUTPUT);
  pinMode(LED_BACK_RIGHT, OUTPUT);

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
      lastPosition = 3500;
      lostTime = 0;
      straightCount = 0;
      digitalWrite(LED_BACK_LEFT, LOW);
      digitalWrite(LED_BACK_RIGHT, LOW);
      digitalWrite(LED_FRONT_LEFT, HIGH);     // front LEDs on = on the way back
      digitalWrite(LED_FRONT_RIGHT, HIGH);
      setPhase(BACK);
    } else if (timeInPhase > 5000) {          // spun 5 s without finding it: give up
      setPhase(DONE);
    }
    return;
  }

  // ---------- OUT or BACK: watch for a bar ----------
  // It only counts as a bar if:
  //   - 7 or more sensors are very black, 3 readings in a row (ignores one-off glitches)
  //   - the robot was driving straight on a single line just before (bars come after
  //     straights; hairpins do not)
  //   - enough time has passed since the start / U-turn
  bool looksLikeBar = strongBlack >= 7 && straightCount >= 50 && timeInPhase > BAR_IGNORE_MS;
  if (looksLikeBar) {
    allBlackCount++;
    if (allBlackCount >= 3) {
      allBlackCount = 0;
      drive(0, 0);
      if (phase == OUT) {
        delay(200);             // let the robot settle before spinning
        offEndBar = false;
        digitalWrite(LED_BACK_LEFT, HIGH);    // back LEDs on = doing the U-turn
        digitalWrite(LED_BACK_RIGHT, HIGH);
        setPhase(UTURN);
      } else {
        setPhase(DONE);         // back at the start bar
      }
      return;
    }
  } else {
    allBlackCount = 0;
  }

  // ---------- Follow the line ----------
  if (total == 0) {
    straightCount = 0;
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

  long position = weighted / total;
  lastPosition = position;

  // error > 0 means the line is to the RIGHT
  long error = 3500 - position;
  lastError = error;

  if (lines == 1 && labs(error) < 1000) straightCount++;
  else straightCount = 0;

  float correction = KP * error;

  // Sharp turn: line near the edge of the sensors -> extra steering
  if (error > SHARP_ERROR)  correction += KP_SHARP * (error - SHARP_ERROR);
  if (error < -SHARP_ERROR) correction += KP_SHARP * (error + SHARP_ERROR);

  int left  = constrain((int)(BASE_SPEED + correction), -MAX_REVERSE, 100);
  int right = constrain((int)(BASE_SPEED - correction), -MAX_REVERSE, 100);
  drive(left, right);
}

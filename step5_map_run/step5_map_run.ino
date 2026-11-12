// STEP 5: Mapping run. Drives exactly like Step 4, but also records the track
// with the wheel encoders. When it stops, plug in USB and open the Serial Monitor
// (9600): it prints the map every 5 seconds. Copy all of it and send it over.
//
// Drive over the start bar -> follow the line -> U-turn at the end bar ->
// follow the line back -> stop at the start bar.
// Line following is Step 3 plus:
//   - on sharp turns the inner wheel goes backwards
//   - if two lines are under the sensors (hairpins), it keeps the one it was following
//   - stricter end-bar check so hairpins are not mistaken for the end bar
//   - if the black suddenly gets wider than a line (end bar, tip of a V), it keeps
//     going straight instead of steering along it
//   - after losing the line at a V tip, it spins toward the side the other arm was seen
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
long otherPosition = -1;        // where a second line was seen (-1 = none)
bool otherLineRight = false;    // last time two lines were seen, the other one was on the right
unsigned long otherLineTime = 0;
long lastError = 0;
unsigned long lostTime = 0;
int  allBlackCount = 0;
int  straightCount = 0;         // readings in a row driving straight on a single line
bool sawStartBar = false;
bool everSawLine = false;
bool offEndBar = false;
bool spinRight = false;         // which way to spin when the line is lost

// ---------- Track map (from the wheel encoders) ----------
const long SEG_COUNTS = 150;    // one map entry every 150 encoder counts of travel
const int  MAX_SEGS   = 1500;  // plenty of room (about 15 KB of memory)

int  leftCmd = 0;               // last speed sent to each wheel (for direction)
int  rightCmd = 0;
long prevLeftCount = 0;
long prevRightCount = 0;
long travel2 = 0;               // total forward travel x2 (left + right counts)
long segTravel2 = 0;            // travel x2 in the current entry
long segTurn = 0;               // left counts - right counts (> 0 = turning right)
int  segLost = 0;               // times the line was lost in the current entry
unsigned long segStartTime = 0;

int  segCount = 0;
byte segLeg[MAX_SEGS];          // OUT, UTURN or BACK
long segDist[MAX_SEGS];         // distance from the start at the end of the entry
int  segTurnLog[MAX_SEGS];
byte segLostLog[MAX_SEGS];
int  segMs[MAX_SEGS];           // how long the entry took

long outEndDist = 0;            // distance where the end bar was found
long rawLeftTotal = 0;          // raw encoder totals, to check the encoders work
long rawRightTotal = 0;
unsigned long runStart = 0;
unsigned long outTime = 0;
unsigned long backTime = 0;
unsigned long lastPrint = 0;

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
  leftCmd = left;
  rightCmd = right;
  setWheel(LEFT_DIR, LEFT_PWM, left);
  setWheel(RIGHT_DIR, RIGHT_PWM, right);
}

void saveSegment() {
  if (segTravel2 == 0 && segTurn == 0) return;
  if (segCount < MAX_SEGS) {
    segLeg[segCount] = phase;
    segDist[segCount] = travel2 / 2;
    segTurnLog[segCount] = segTurn;
    segLostLog[segCount] = segLost;
    segMs[segCount] = millis() - segStartTime;
    segCount++;
  }
  segTravel2 = 0;
  segTurn = 0;
  segLost = 0;
  segStartTime = millis();
}

// Reads the encoders and adds to the map. Works whether the library counts
// backwards wheel turns up or down: the direction comes from the motor command.
void updateMap() {
  long l = getEncoderCount_left();
  long r = getEncoderCount_right();
  long dl = labs(l - prevLeftCount);
  long dr = labs(r - prevRightCount);
  prevLeftCount = l;
  prevRightCount = r;
  rawLeftTotal += dl;
  rawRightTotal += dr;
  if (leftCmd < 0) dl = -dl;
  if (rightCmd < 0) dr = -dr;

  travel2 += dl + dr;
  segTravel2 += dl + dr;
  segTurn += dl - dr;
  if (segTravel2 >= 2 * SEG_COUNTS) saveSegment();
}

void setPhase(int p) {
  saveSegment();
  if (p == UTURN) {
    outEndDist = travel2 / 2;
    outTime = millis() - runStart;
  }
  if (p == DONE && phase == BACK) backTime = millis() - runStart - outTime;
  phase = p;
  phaseStart = millis();
}

const char* legName(int leg) {
  if (leg == START) return "START";
  if (leg == OUT) return "OUT";
  if (leg == UTURN) return "UTURN";
  return "BACK";
}

void printMap() {
  Serial.println();
  Serial.println("===== MAP START (copy from here) =====");
  Serial.print("segments ");      Serial.println(segCount);
  Serial.print("out_end_dist ");  Serial.println(outEndDist);
  Serial.print("total_dist ");    Serial.println(travel2 / 2);
  Serial.print("out_time_ms ");   Serial.println(outTime);
  Serial.print("back_time_ms ");  Serial.println(backTime);
  Serial.print("raw_left ");      Serial.println(rawLeftTotal);
  Serial.print("raw_right ");     Serial.println(rawRightTotal);
  Serial.print("base_speed ");    Serial.println(BASE_SPEED);
  Serial.println("leg,dist,turn,lost,ms");
  for (int i = 0; i < segCount; i++) {
    Serial.print(legName(segLeg[i]));
    Serial.print(',');
    Serial.print(segDist[i]);
    Serial.print(',');
    Serial.print(segTurnLog[i]);
    Serial.print(',');
    Serial.print((int)segLostLog[i]);
    Serial.print(',');
    Serial.println(segMs[i]);
  }
  Serial.println("===== MAP END (copy to here) =====");
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
  otherPosition = -1;
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
      if (total > 0) otherPosition = weighted / total;
      bestDistance = distance;
      weighted = w;
      total = t;
    } else {
      otherPosition = w / t;
    }
  }

  // Remember which side the other line was on (it is on the inside of a hairpin)
  if (lines >= 2) {
    otherLineRight = otherPosition < weighted / total;   // smaller = more to the right
    otherLineTime = millis();
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

  Serial.begin(9600);

  delay(3000);   // 3 seconds to put the robot down
  prevLeftCount = getEncoderCount_left();
  prevRightCount = getEncoderCount_right();
  runStart = millis();
  segStartTime = runStart;
  setPhase(START);
}

void loop() {
  readSensors();
  updateMap();
  unsigned long timeInPhase = millis() - phaseStart;

  // ---------- DONE: stay stopped and print the map every 5 s ----------
  if (phase == DONE) {
    drive(0, 0);
    digitalWrite(LED_FRONT_LEFT, HIGH);       // all four LEDs on = map ready
    digitalWrite(LED_FRONT_RIGHT, HIGH);
    digitalWrite(LED_BACK_LEFT, HIGH);
    digitalWrite(LED_BACK_RIGHT, HIGH);
    if (millis() - lastPrint > 5000) {
      printMap();
      lastPrint = millis();
    }
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
      otherLineTime = 0;
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
  // "Driving straight" = it has been on a single, centered line for a while and has
  // not made a sharp turn or lost the line since. Bars come after straights.
  bool drivingStraight = straightCount >= 50;

  // It only counts as a bar if:
  //   - 7 or more sensors are very black, 2 readings in a row (ignores one-off glitches)
  //   - the robot was driving straight just before
  //   - enough time has passed since the start / U-turn
  bool looksLikeBar = strongBlack >= 7 && drivingStraight && timeInPhase > BAR_IGNORE_MS;
  if (looksLikeBar) {
    allBlackCount++;
    if (allBlackCount >= 2) {
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

  // Black wider than a line (5+ very black sensors) after driving straight: this is the
  // end bar (maybe hit at an angle) or the tip of a V. Keep going straight; don't
  // steer along it. On a bar this gets all sensors onto the bar so it is recognized.
  if (drivingStraight && lines == 1 && strongBlack >= 5) {
    drive(BASE_SPEED, BASE_SPEED);
    return;
  }

  // ---------- Follow the line ----------
  if (total == 0) {
    straightCount = 0;
    // Not found the line yet after the start: keep going straight
    if (!everSawLine) {
      drive(BASE_SPEED, BASE_SPEED);
      return;
    }
    // Lost the line: turn toward where it was last seen, give up after 2 seconds.
    // If it was lost while roughly centered (tip of a V) and a second line was seen
    // just before, the track continues on that second line's side.
    if (lostTime == 0) {
      lostTime = millis();
      segLost++;
      if (labs(lastError) < 1500 && millis() - otherLineTime < 1000) {
        spinRight = otherLineRight;
      } else {
        spinRight = lastError > 0;
      }
      lastPosition = spinRight ? 0 : 7000;   // the line will come back in on this side
    }
    if (millis() - lostTime > 2000) {
      setPhase(DONE);      // gave up: stop and print the map so far
    } else if (spinRight) {
      drive(45, -45);      // spin right
    } else {
      drive(-45, 45);      // spin left
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

  // Only a normal-width line counts (not a sensor bar lying along the line mid-spin)
  if (lines == 1 && labs(error) < 1000 && strongBlack <= 4) straightCount++;
  if (labs(error) > SHARP_ERROR) straightCount = 0;     // sharp turn: not driving straight

  float correction = KP * error;

  // Sharp turn: line near the edge of the sensors -> extra steering
  if (error > SHARP_ERROR)  correction += KP_SHARP * (error - SHARP_ERROR);
  if (error < -SHARP_ERROR) correction += KP_SHARP * (error + SHARP_ERROR);

  int left  = constrain((int)(BASE_SPEED + correction), -MAX_REVERSE, 100);
  int right = constrain((int)(BASE_SPEED - correction), -MAX_REVERSE, 100);
  drive(left, right);
}

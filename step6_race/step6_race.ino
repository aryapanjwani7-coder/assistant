// STEP 6: Race. Same as the Step 5 map run, but drives FAST on the long straights
// (found in the map) and slows down before the hairpins. It still records the map
// so the next run can be checked and tuned. When it stops, plug in USB and open the Serial Monitor
// (9600): it prints the map every 5 seconds. Copy all of it and send it over.
// To stop it at any time (and print the map), press one of the front bump switches.
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

// Front bump switches (course pin chart). Press any of them to stop the robot.
const int BUMP_PINS[6] = { 24, 25, 6, 27, 8, 28 };

// ---- Numbers you can change ----
const int   BASE_SPEED = 50;     // how fast it drives
const float KP         = 0.014;  // how hard it steers
const int   SPIN_SPEED = 50;     // how fast it spins for the U-turn
const int   BACKUP_SPEED = 40;   // how fast it backs off the end bar before the U-turn
const int   SLOW_SPIN_SPEED = 40; // spin speed once the line is almost found

// Sharp turns: when the line reaches the outer sensors, turn much harder
// so the inner wheel goes BACKWARDS. Set KP_SHARP = 0 to switch this off.
const int   SHARP_ERROR = 2000;  // line under the outer 2 sensors on either side
const float KP_SHARP    = 0.05;  // extra steering beyond SHARP_ERROR
const int   MAX_REVERSE = 50;    // fastest a wheel may go backwards

// Bars (end bar, start bar) are ignored for this long after the start and after
// the U-turn. The robot cannot reach a bar sooner than this.
const unsigned long BAR_IGNORE_MS = 10000;

// Fast zones, from the map run (encoder distance, same numbers as the map's "dist").
// Each zone ends well before the next sharp turn so the robot has time to slow down.
const long FAST_ZONES[][2] = {
  {  2560,  3500 },   // way out:  straight T5 -> T6  (T6 starts ~3760)
  {  7080,  7350 },   // way out:  straight T16 -> end bar (bar at ~7565)
  { 11410, 12500 },   // way back: straight T6 -> T5  (T5 starts ~12760)
};
const int NUM_ZONES  = 3;
const int FAST_SPEED = 80;       // speed in the fast zones
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
bool spinRight = false;         // which way to spin when the line is lost
int  uturnStep = 0;             // which part of the U-turn it is doing
unsigned long uturnStepStart = 0;
float lineWidth = 2.5;          // learned: how many sensors a normal line makes very black
int  speedNow = BASE_SPEED;     // current forward speed (BASE_SPEED or up to FAST_SPEED)
int  brokenZone = -1;           // fast zone where the line was lost: stay slow there

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
long backStartDist = 0;         // distance when the way back began

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
  if (p == BACK) backStartDist = travel2 / 2;
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
  Serial.print("fast_speed ");    Serial.println(FAST_SPEED);
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
  for (int i = 0; i < 6; i++) pinMode(BUMP_PINS[i], INPUT_PULLUP);

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

  // A front bump switch pressed: stop and print the map
  if (phase != DONE) {
    for (int i = 0; i < 6; i++) {
      if (digitalRead(BUMP_PINS[i]) == LOW) setPhase(DONE);
    }
  }

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

  // ---------- UTURN: back off the bar, then turn half a circle onto the line ----------
  // A normal line makes up to normalWidth sensors very black; more than that is the bar.
  int normalWidth = (int)(lineWidth + 1.0);
  if (phase == UTURN) {
    if (uturnStep == 0) {
      // Back up until the sensors are on the bar (they may have rolled past it)
      drive(-BACKUP_SPEED, -BACKUP_SPEED);
      if (strongBlack > normalWidth) uturnStep = 1;
    } else if (uturnStep == 1) {
      // Keep backing up until the sensors are off the bar, onto the line we came in on
      drive(-BACKUP_SPEED, -BACKUP_SPEED);
      if (strongBlack <= normalWidth) {
        uturnStep = 2;
        uturnStepStart = millis();
      }
    } else if (uturnStep == 2) {
      // Back up a little more, so the bar is out of reach while spinning
      drive(-BACKUP_SPEED, -BACKUP_SPEED);
      if (millis() - uturnStepStart > 250) uturnStep = 3;
    } else if (uturnStep == 3) {
      // Spin left until the sensors have left the line
      drive(-SPIN_SPEED, SPIN_SPEED);
      if (blackSensors == 0) uturnStep = 4;
    } else {
      // Keep spinning until the line is under the middle sensors again (half a turn).
      // Once the line shows up at the edge of the sensors, spin slower so it
      // does not swing past it.
      if (blackSensors > 0) drive(-SLOW_SPIN_SPEED, SLOW_SPIN_SPEED);
      else                  drive(-SPIN_SPEED, SPIN_SPEED);
      if (centerBlack) {
        drive(0, 0);
        delay(100);                             // stop the spin before following
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
        return;
      }
    }
    if (timeInPhase > 8000) setPhase(DONE);     // something went wrong: give up
    return;
  }

  // ---------- OUT or BACK: watch for a bar ----------
  // "Driving straight" = it has been on a single, centered line for a while and has
  // not made a sharp turn or lost the line since. Bars come after straights.
  bool drivingStraight = straightCount >= 50;

  // On the way back, how far along is it compared with the way out?
  // (Uses the wheel encoders; if they did not work, uses time instead.)
  int backPercent = 0;
  if (phase == BACK) {
    if (outEndDist > 1000) backPercent = (travel2 / 2 - backStartDist) * 100 / outEndDist;
    else if (outTime > 0)  backPercent = timeInPhase * 100 / outTime;
  }
  // Last 15% of the way back: the next wide black is the start bar, even if it
  // was not driving perfectly straight before it (after T1 the straight is short)
  bool nearStart = backPercent >= 85;
  if (backPercent >= 120) {      // well past where the start bar should be: stop
    setPhase(DONE);
    return;
  }

  // It only counts as a bar if:
  //   - 7 or more sensors are very black, 2 readings in a row (ignores one-off glitches)
  //   - the robot was driving straight just before
  //   - enough time has passed since the start / U-turn
  bool looksLikeBar = strongBlack >= 7 && (drivingStraight || nearStart) && timeInPhase > BAR_IGNORE_MS;
  if (looksLikeBar) {
    allBlackCount++;
    if (allBlackCount >= 2) {
      allBlackCount = 0;
      drive(0, 0);
      if (phase == OUT) {
        delay(200);             // let the robot settle
        uturnStep = (strongBlack > normalWidth) ? 1 : 0;   // already on the bar?
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

  // Black wider than a normal line after driving straight: this is the end bar
  // (maybe hit at an angle) or the tip of a V. Keep going straight; don't steer
  // along it. On a bar this gets all sensors onto the bar so it is recognized.
  if ((drivingStraight || nearStart) && lines == 1 && strongBlack > normalWidth) {
    drive(BASE_SPEED, BASE_SPEED);
    return;
  }

  // ---------- Speed: fast inside a fast zone, normal everywhere else ----------
  long distNow = travel2 / 2;
  int zone = -1;
  for (int z = 0; z < NUM_ZONES; z++) {
    if (distNow >= FAST_ZONES[z][0] && distNow < FAST_ZONES[z][1]) zone = z;
  }
  int targetSpeed = (zone >= 0 && zone != brokenZone) ? FAST_SPEED : BASE_SPEED;
  if (speedNow < targetSpeed) speedNow++;        // speed up gently
  else speedNow = targetSpeed;                   // slow down at once

  // ---------- Follow the line ----------
  if (total == 0) {
    if (zone >= 0) brokenZone = zone;            // lost the line here: no more fast in this zone
    speedNow = BASE_SPEED;
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

  // Only a normal-width line counts: not a sensor bar lying along the line mid-spin,
  // and not the end bar coming in at an angle (that also pulls the error sideways)
  bool normalLine = lines == 1 && strongBlack <= normalWidth;
  if (normalLine && labs(error) < 1000) {
    straightCount++;
    lineWidth += 0.01 * (strongBlack - lineWidth);    // keep learning the line width
  }
  if (normalLine && labs(error) > SHARP_ERROR) straightCount = 0;   // sharp turn

  float correction = KP * error;
  int maxSpeed = speedNow + 50;                  // outer wheel limit (at least 100)
  if (maxSpeed < 100) maxSpeed = 100;

  // Sharp turn: line near the edge of the sensors -> extra steering
  if (error > SHARP_ERROR)  correction += KP_SHARP * (error - SHARP_ERROR);
  if (error < -SHARP_ERROR) correction += KP_SHARP * (error + SHARP_ERROR);

  int left  = constrain((int)(speedNow + correction), -MAX_REVERSE, maxSpeed);
  int right = constrain((int)(speedNow - correction), -MAX_REVERSE, maxSpeed);
  drive(left, right);
}

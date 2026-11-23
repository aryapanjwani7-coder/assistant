// PARTS 1 + 2: start box -> T1 -> T2 horseshoe -> T3 -> T4 -> T5 -> stops at the
// start of the long straight to T6.
//
// Part 1 is driven as a PLAN:
//   1. LAUNCH    straight out of the box (any of the 4 boxes) until it meets the line
//                (ignores the first few cm, where it can see the box outline)
//   2. T1        follow the line round T1 (from every box this works the same)
//   3. STRAIGHT  once T1 is done: fixed distance at full speed, holding straight
//   4. HORSESHOE one smooth left arc at constant speed
//   5. HAIRPINS  T3 (right), T4 (left), T5 (right): careful line following.
//                If the line is lost it spins the way that hairpin turns.
//   then stop at the start of the long straight
// On the straight it follows the line normally, so it is lined up for the horseshoe.
// In the horseshoe the line sensors only make slight corrections.
//
// FOUR RUNS IN A ROW, one from each start box:
//   Put it in box 1 and press RESET. It runs Part 1 and stops.
//   The LEDs blink for 6 seconds: move it to box 2. It starts by itself. Same for 3, 4.
//   After run 4 all four LEDs stay on: plug in USB, open the Serial Monitor (9600)
//   and copy everything from PART 12 START to PART 12 END.
// A front bump switch ends the current run (then the 6 second wait for the next box).
// Bump switches are ignored while you move it between boxes.
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
const int BUMP_PINS[6] = { 24, 25, 6, 27, 8, 28 };

// ================= The plan (numbers to tune) =================
// Turn amounts are in encoder turn counts (+ = right, - = left, about 3.3 = 1 degree).
// Lengths are encoder counts of travel.
const int  LAUNCH_SPEED  = 100;  // 1. straight out of the box
const long LAUNCH_IGNORE = 40;   //    ignore black for this distance (box outline)

const int  T1_SPEED   = 70;      // 2. following the line round T1
const long T1_DONE    = 200;     //    T1 is done once it has turned right this much
                                 //    (counted from its most-left point, so a box that
                                 //    joins the line with a left turn works too)...
const long T1_STEADY  = 15;      //    ...and has stopped turning right (last 60 counts)
                                 //    or started turning left (the horseshoe)
const long T1_MAX     = 420;     //    or, at the latest, once it has turned this much
const long T1_MAX_DIST = 500;    //    or, at the latest, after this distance in T1
                                 //    (T1 turns differ by box: 275..370 in tests)

const int  STRAIGHT_SPEED  = 160; // 3. straight: speed
const long STRAIGHT_LENGTH = 160; //    distance (ends early if the horseshoe starts)

const int  HS_SPEED  = 150;      // 4. horseshoe arc: speed
// The horseshoe is not a perfect circle: it is gentler in the middle. Planned in
// 4 pieces, from the 4-box sessions (turn of each piece, length of each piece).
const int  HS_PIECES = 4;
const long HS_PIECE_TURN[HS_PIECES] = { -225, -150, -225, -220 };
const long HS_PIECE_LEN[HS_PIECES]  = {  150,  150,  150,  130 };
const long HS_TURN   = -820;     //    total turn (about 250 degrees left)
const long HS_LENGTH = 580;      //    total length

const int  RUNS = 4;             // runs in a row (one per start box)
const unsigned long WAIT_MS = 6000;  // time to move it to the next box

const int  EXIT_SPEED  = 100;    // short bit after the horseshoe
const long EXIT_LENGTH = 30;

// 5. Hairpins T3, T4, T5. Distances are counted from the end of the horseshoe
//    (from the map runs). They only decide which way to spin if the line is lost.
const int  HAIRPIN_SPEED = 65;   //    careful speed through T3-T5
const long T3_END = 450;         //    T3 (turns right) until here
const long T4_END = 800;         //    T4 (turns left) until here, then T5 (turns right)
const long PART2_LENGTH = 1250;  //    stop here: start of the long straight to T6

// Detecting where a curve starts: turned this much within the last 60 counts
const long CURVE_DETECT = 45;

// Line following
const float KP = 0.014;          // steering (tuned at speed 50, scaled up with speed)
const float KD = 0.07;           // damping
const float PLAN_KP = 0.5;       // line corrections in the planned steps (1 = normal)
                                 // (full strength again if the line is far off)
const int   SHARP_ERROR = 2000;  // line near the edge: steer harder (safety net)
const float KP_SHARP    = 0.12;
const int   MAX_REVERSE = 80;    // inner wheel may go this fast backwards
const int   SEARCH_SPEED = 80;   // spin speed if the line is lost
// ==============================================================

// Steps
const int LAUNCH = 0, T1 = 1, STRAIGHT = 2, HS_ARC = 3, EXIT = 4, HAIRPINS = 5, DONE = 6;
const char* STEP_NAMES[] = { "launch", "T1", "straight", "horseshoe", "exit", "hairpins", "done" };
int  run = 0;                    // which run (0..3 = box 1..4)
bool waiting = false;            // between runs: waiting to be moved to the next box
bool allDone = false;            // all runs finished
unsigned long waitStart = 0;
unsigned long runStart = 0;
int  step = LAUNCH;
long stepStartDist = 0;          // distance when this step began
long stepStartTurn = 0;          // turn total when this step began

// ---------- state ----------
int  v[8];
long weighted = 0, total = 0;
int  blackSensors = 0, strongBlack = 0;
long lastPosition = 3500;
long lastError = 0;
bool firstReading = true;
int  speedNow = LAUNCH_SPEED;
unsigned long lostTime = 0;
bool spinRight = false;

int  leftCmd = 0, rightCmd = 0;
long prevL = 0, prevR = 0;
long travel2 = 0;                // distance x2 (left + right counts)
long turnTotal = 0;              // all turning so far (+ = right)
// turning over the last 3 chunks of 20 counts (to detect where a curve starts)
long chunkTravel2 = 0, chunkTurn = 0;
long chunkTurns[3] = { 0, 0, 0 };

// log for each run, one line per ~150 counts (and one at every step change)
const int LOG_MAX = 40;
long logDist[RUNS][LOG_MAX];
int  logTurn[RUNS][LOG_MAX], logMs[RUNS][LOG_MAX], logMaxErr[RUNS][LOG_MAX], logSpeed[RUNS][LOG_MAX];
int  logLost[RUNS][LOG_MAX], logAvgErr[RUNS][LOG_MAX], logStep[RUNS][LOG_MAX];
int  logCount[RUNS];
unsigned long runTime[RUNS];
int  runReached[RUNS];
long launchDist[RUNS];           // how far it drove before seeing the line
int  lineSide[RUNS];             // where the line first appeared: -1 left, 1 right, 0 middle
long t1Turn[RUNS];               // turn when T1 was judged done
long minTurn = 0;                // most-left turn so far (for judging T1)
long segTravel2 = 0, segTurn = 0, segMaxErr = 0, segSpeedSum = 0, segReads = 0, segErrSum = 0;
int  segLost = 0;
unsigned long segStart = 0;
unsigned long lastPrint = 0;

// Time in milliseconds from the microsecond timer (millis() only moves in big steps)
unsigned long nowMs() {
  return micros() / 1000;
}

void setWheel(int dirPin, int pwmPin, int speed) {
  if (speed >= 0) { digitalWrite(dirPin, LOW);  analogWrite(pwmPin, speed); }
  else            { digitalWrite(dirPin, HIGH); analogWrite(pwmPin, -speed); }
}

void drive(int left, int right) {
  leftCmd = left;
  rightCmd = right;
  setWheel(LEFT_DIR, LEFT_PWM, left);
  setWheel(RIGHT_DIR, RIGHT_PWM, right);
}

void saveLog() {
  int n = logCount[run];
  if (n < LOG_MAX && segReads > 0) {
    logDist[run][n] = travel2 / 2;
    logTurn[run][n] = segTurn;
    logMs[run][n] = nowMs() - segStart;
    logMaxErr[run][n] = segMaxErr;
    logSpeed[run][n] = segSpeedSum / segReads;
    logLost[run][n] = segLost;
    logAvgErr[run][n] = segErrSum / segReads;
    logStep[run][n] = step;
    logCount[run] = n + 1;
  }
  segTravel2 = 0; segTurn = 0; segMaxErr = 0; segSpeedSum = 0; segReads = 0;
  segErrSum = 0; segLost = 0;
  segStart = nowMs();
}

void setStep(int s) {
  saveLog();                     // one log line per step, so each step can be judged
  if (step == T1) t1Turn[run] = turnTotal - minTurn;
  step = s;
  stepStartDist = travel2 / 2;
  stepStartTurn = turnTotal;
  // forget the turning of the previous step, so the next step starts clean
  chunkTravel2 = 0;
  chunkTurn = 0;
  chunkTurns[0] = chunkTurns[1] = chunkTurns[2] = 0;
}

// Encoders: distance travelled and turning (direction comes from the motor command)
void updateEncoders() {
  long l = getEncoderCount_left();
  long r = getEncoderCount_right();
  long dl = labs(l - prevL);
  long dr = labs(r - prevR);
  prevL = l;
  prevR = r;
  if (leftCmd < 0) dl = -dl;
  if (rightCmd < 0) dr = -dr;
  travel2 += dl + dr;
  turnTotal += dl - dr;
  chunkTravel2 += dl + dr;
  chunkTurn += dl - dr;
  if (chunkTravel2 >= 40) {      // every 20 counts of travel
    chunkTurns[0] = chunkTurns[1];
    chunkTurns[1] = chunkTurns[2];
    chunkTurns[2] = chunkTurn;
    chunkTravel2 = 0;
    chunkTurn = 0;
  }
  if (step != LAUNCH) {
    segTravel2 += dl + dr;
    segTurn += dl - dr;
    if (segTravel2 >= 300) saveLog();
  }
}

long recentTurn() {
  return chunkTurns[0] + chunkTurns[1] + chunkTurns[2];
}

void readSensors() {
  ECE3_read_IR(sensors);
  blackSensors = 0;
  strongBlack = 0;
  for (int i = 0; i < 8; i++) {     // sensor 0 = RIGHT, 7 = LEFT
    long x = ((long)sensors[i] - sensorMin[i]) * 1000L / (sensorMax[i] - sensorMin[i]);
    x = constrain(x, 0L, 1000L);
    if (x < 300) x = 0;
    v[i] = x;
    if (x > 0) blackSensors++;
    if (x > 700) strongBlack++;
  }
  // If two separate lines are seen, follow the one nearest to where the line was
  weighted = 0;
  total = 0;
  long best = 100000;
  int i = 0;
  while (i < 8) {
    if (v[i] == 0) { i++; continue; }
    long w = 0, t = 0;
    while (i < 8 && v[i] > 0) { w += (long)v[i] * i * 1000L; t += v[i]; i++; }
    long d = labs(w / t - lastPosition);
    if (d < best) { best = d; weighted = w; total = t; }
  }
}

void printLog() {
  Serial.println();
  Serial.println("===== PART 12 START (copy from here) =====");
  Serial.print("launch "); Serial.print(LAUNCH_SPEED); Serial.print(' '); Serial.println(LAUNCH_IGNORE);
  Serial.print("t1 ");    Serial.print(T1_SPEED);  Serial.print(' '); Serial.print(T1_DONE);
  Serial.print(' ');      Serial.println(T1_MAX);
  Serial.print("straight "); Serial.print(STRAIGHT_SPEED); Serial.print(' '); Serial.println(STRAIGHT_LENGTH);
  Serial.print("horseshoe "); Serial.print(HS_SPEED); Serial.print(' '); Serial.print(HS_TURN);
  Serial.print(' ');      Serial.println(HS_LENGTH);
  Serial.print("plan_kp "); Serial.println(PLAN_KP);
  Serial.print("hairpins "); Serial.print(HAIRPIN_SPEED); Serial.print(' ');
  Serial.print(T3_END); Serial.print(' '); Serial.print(T4_END); Serial.print(' ');
  Serial.println(PART2_LENGTH);
  for (int r = 0; r < RUNS; r++) {
    Serial.print("--- box "); Serial.print(r + 1);
    Serial.print(": time_ms "); Serial.print(runTime[r]);
    Serial.print(", reached "); Serial.print(STEP_NAMES[runReached[r]]);
    Serial.print(", launch_dist "); Serial.print(launchDist[r]);
    Serial.print(", line_side "); Serial.print(lineSide[r]);
    Serial.print(", t1_turn "); Serial.println(t1Turn[r]);
    // avgerr: average line position. - = line on the robot's LEFT, + = on its RIGHT.
    // In the LEFT horseshoe: - means running wide (outside), + means cutting inside.
    // In the RIGHT T1 arc:   + means running wide (outside), - means cutting inside.
    Serial.println("step,dist,turn,lost,ms,maxerr,speed,avgerr");
    for (int i = 0; i < logCount[r]; i++) {
      Serial.print(STEP_NAMES[logStep[r][i]]); Serial.print(',');
      Serial.print(logDist[r][i]);   Serial.print(',');
      Serial.print(logTurn[r][i]);   Serial.print(',');
      Serial.print(logLost[r][i]);   Serial.print(',');
      Serial.print(logMs[r][i]);     Serial.print(',');
      Serial.print(logMaxErr[r][i]); Serial.print(',');
      Serial.print(logSpeed[r][i]);  Serial.print(',');
      Serial.println(logAvgErr[r][i]);
    }
  }
  Serial.println("===== PART 12 END (copy to here) =====");
}

// Start a fresh run from the box the robot is in now
void startRun() {
  step = LAUNCH;
  stepStartDist = 0;
  stepStartTurn = 0;
  travel2 = 0;
  turnTotal = 0;
  chunkTravel2 = 0;
  chunkTurn = 0;
  chunkTurns[0] = chunkTurns[1] = chunkTurns[2] = 0;
  lastPosition = 3500;
  lastError = 0;
  firstReading = true;
  speedNow = LAUNCH_SPEED;
  minTurn = 0;
  lostTime = 0;
  segTravel2 = 0; segTurn = 0; segMaxErr = 0; segSpeedSum = 0; segReads = 0;
  segErrSum = 0; segLost = 0;
  prevL = getEncoderCount_left();
  prevR = getEncoderCount_right();
  runStart = nowMs();
  segStart = runStart;
  digitalWrite(LED_FRONT_LEFT, LOW);
  digitalWrite(LED_FRONT_RIGHT, LOW);
  digitalWrite(LED_BACK_LEFT, LOW);
  digitalWrite(LED_BACK_RIGHT, LOW);
}

// End of a run: stop, remember the result, then wait for the next box
void finishRun() {
  drive(0, 0);
  saveLog();
  runTime[run] = nowMs() - runStart;
  runReached[run] = step;
  step = DONE;
  waiting = true;
  waitStart = nowMs();
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
  startRun();
}

void loop() {
  readSensors();
  updateEncoders();
  long dist = travel2 / 2;
  long stepDist = dist - stepStartDist;
  long stepTurn = turnTotal - stepStartTurn;

  // All runs done: stay stopped, all LEDs on, print the logs every 5 seconds
  if (allDone) {
    drive(0, 0);
    if (nowMs() - lastPrint > 5000) {
      printLog();
      lastPrint = nowMs();
    }
    return;
  }

  // Between runs: blink the LEDs for 6 seconds while it is moved to the next box
  if (waiting) {
    drive(0, 0);
    bool on = ((nowMs() - waitStart) / 250) % 2 == 0;
    digitalWrite(LED_FRONT_LEFT, on ? HIGH : LOW);
    digitalWrite(LED_FRONT_RIGHT, on ? HIGH : LOW);
    digitalWrite(LED_BACK_LEFT, on ? LOW : HIGH);
    digitalWrite(LED_BACK_RIGHT, on ? LOW : HIGH);
    if (nowMs() - waitStart > WAIT_MS) {
      waiting = false;
      if (run + 1 < RUNS) {
        run++;
        startRun();
      } else {
        allDone = true;
        digitalWrite(LED_FRONT_LEFT, HIGH);
        digitalWrite(LED_FRONT_RIGHT, HIGH);
        digitalWrite(LED_BACK_LEFT, HIGH);
        digitalWrite(LED_BACK_RIGHT, HIGH);
      }
    }
    return;
  }

  // A bump switch ends this run (ignored for the first half second of a run)
  bool bumped = false;
  for (int i = 0; i < 6; i++) {
    if (digitalRead(BUMP_PINS[i]) == LOW) bumped = true;
  }
  if (bumped && nowMs() - runStart > 500) {
    finishRun();
    return;
  }

  // ----- 1. LAUNCH: straight out of the box until the line is seen -----
  // (a wide black patch, like the start bar, is driven straight over)
  if (step == LAUNCH) {
    drive(LAUNCH_SPEED, LAUNCH_SPEED);
    if (dist >= LAUNCH_IGNORE && total > 0 && strongBlack >= 1 && strongBlack < 5) {
      launchDist[run] = dist;                         // where it met the line...
      long pos = weighted / total;                    // ...and on which side of the sensors
      lineSide[run] = (pos > 4500) ? -1 : (pos < 2500) ? 1 : 0;
      minTurn = turnTotal;
      setStep(T1);
    }
    return;
  }

  // ----- Move on to the next step when this one is finished -----
  if (turnTotal < minTurn) minTurn = turnTotal;
  long t1Turned = turnTotal - minTurn;
  if (step == T1 && ((t1Turned >= T1_DONE && recentTurn() < T1_STEADY)
                     || t1Turned >= T1_MAX || stepDist >= T1_MAX_DIST)) {
    setStep(STRAIGHT);                                // T1 done: go
  } else if (step == STRAIGHT && (stepDist >= STRAIGHT_LENGTH ||
                                  (stepDist > 40 && recentTurn() <= -CURVE_DETECT))) {
    setStep(HS_ARC);                                  // planned distance, or the curve started
  } else if (step == HS_ARC && (stepTurn <= HS_TURN || stepDist >= HS_LENGTH)) {
    setStep(EXIT);
  } else if (step == EXIT && stepDist >= EXIT_LENGTH) {
    setStep(HAIRPINS);
  } else if (step == HAIRPINS && stepDist >= PART2_LENGTH) {
    finishRun();
    return;
  }

  // ----- Speed and planned turn rate for this step -----
  int target = T1_SPEED;
  float planCurve = 0;                                // turn counts per count travelled
  bool planned = false;
  if (step == STRAIGHT)      { target = STRAIGHT_SPEED; }  // full line following: line up for the horseshoe
  else if (step == HS_ARC) {
    target = HS_SPEED;
    planned = true;
    long into = stepDist;                             // which piece of the horseshoe
    int k = 0;
    while (k < HS_PIECES - 1 && into >= HS_PIECE_LEN[k]) { into -= HS_PIECE_LEN[k]; k++; }
    planCurve = (float)HS_PIECE_TURN[k] / HS_PIECE_LEN[k];
  }
  else if (step == EXIT)     { target = EXIT_SPEED; }
  else if (step == HAIRPINS) { target = HAIRPIN_SPEED; }
  if (speedNow < target) speedNow += 3;       // speed up quickly but not in one jump
  if (speedNow > target) speedNow = target;   // slow down at once

  // ----- Line lost -----
  if (total == 0) {
    if (lostTime == 0) {
      lostTime = nowMs();
      segLost++;
      if (step == HS_ARC) spinRight = false;                     // horseshoe turns left
      else if (step == HAIRPINS && labs(lastError) < 1500) {
        // lost while roughly centered: the hairpin we are in decides the way
        spinRight = !(stepDist >= T3_END && stepDist < T4_END);  // T4 turns left
      }
      else                spinRight = lastError > 0;
      lastPosition = spinRight ? 0 : 7000;
    }
    if (nowMs() - lostTime > 1500) {          // could not find it: end this run
      finishRun();
      return;
    }
    if (spinRight) drive(SEARCH_SPEED, -SEARCH_SPEED);
    else           drive(-SEARCH_SPEED, SEARCH_SPEED);
    speedNow = T1_SPEED;
    firstReading = true;
    return;
  }
  lostTime = 0;

  // ----- Steering -----
  long position = weighted / total;
  lastPosition = position;
  long error = 3500 - position;                // > 0: line is to the RIGHT
  long change = firstReading ? 0 : error - lastError;
  firstReading = false;
  lastError = error;

  // Line correction: P scales with speed (bigger wheel difference needed when fast);
  // D does not (the error already changes faster at higher speed).
  // The extra steering at the edge is NOT scaled with speed: it is already strong,
  // and tripling it at speed 150 made the robot swerve from side to side.
  float correction = KP * error;
  if (planned && labs(error) < 1500) correction *= PLAN_KP;   // the plan does the turning
  if (speedNow > 50) correction = correction * speedNow / 50;
  if (error > SHARP_ERROR)  correction += KP_SHARP * (error - SHARP_ERROR);
  if (error < -SHARP_ERROR) correction += KP_SHARP * (error + SHARP_ERROR);
  correction += KD * change;

  // The plan: constant turn rate. For c turn counts per count travelled the wheels
  // run at speed x (1 +- c/2).
  correction += speedNow * planCurve / 2;

  // Wheel speeds. If the outer wheel would go past full power (255), move BOTH
  // wheels down so the difference (the turn) is kept exactly.
  int left  = (int)(speedNow + correction);
  int right = (int)(speedNow - correction);
  if (left > 255)  { right -= left - 255;  left = 255; }
  if (right > 255) { left  -= right - 255; right = 255; }
  left  = constrain(left,  -MAX_REVERSE, 255);
  right = constrain(right, -MAX_REVERSE, 255);
  drive(left, right);

  // for the log
  if (labs(error) > segMaxErr) segMaxErr = labs(error);
  segSpeedSum += speedNow;
  segErrSum += error;
  segReads++;
}

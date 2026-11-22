// PART 1 ONLY: start box -> T1 -> T2 horseshoe -> stops before T3.
// Works from any of the 4 start boxes: it drives straight until it finds the line,
// follows it at a safe speed, and lines its distance up with the map at T1.
// When it stops (all four LEDs on), plug in USB, open the Serial Monitor (9600)
// and copy the PART 1 block. Press a front bump switch to stop it any time.
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

// ================= Numbers to tune =================
// Speed for each section of Part 1 after T1 (map distance). Before T1 it drives at
// PRE_T1_SPEED. The robot brakes early enough to reach each section at its speed.
const int  NUM_ZONES = 3;
const long ZONE_END[NUM_ZONES]   = {  330,  450, 1150 };
const int  ZONE_SPEED[NUM_ZONES] = {   70,  160,  150 };
//                                  rest of T1  T1->T2  T2 horseshoe
const int  START_SPEED = 100;    // jump start: straight out of the box to the line
const int  PRE_T1_SPEED = 70;    // following the line before T1 (distance not lined up yet)
const int  JOIN_SPEED = 70;      // just after finding the line, until it is centered
const int  BRAKE_COUNTS = 3;     // counts needed to lose 1 unit of speed

const float KP = 0.014;          // steering (tuned at speed 50, scaled up with speed)
const float KD = 0.07;           // damping
const float FF_GAIN = 1.0;       // drive the planned curve from the map (0 = off, 1 = full).
                                 // Runs wide on T2 -> raise. Cuts inside -> lower.
const int   SHARP_ERROR = 2000;  // line near the edge: steer harder
const float KP_SHARP    = 0.12;  // hard steering at the edge (T1 is a sharp corner)
const int   MAX_REVERSE = 80;    // inner wheel may go this fast backwards
const int   SEARCH_SPEED = 80;   // spin speed if the line is lost
const float CURVE_KP_FACTOR = 0.6; // in the horseshoe: line following only nudges
                                   // (full strength again if the line is far off)
// ===================================================

// Shape of Part 1 from the map runs (average of 3): turn per piece of ~150 counts.
// + = right, - = left, about 3.3 = 1 degree.
const int  MAP_N = 7;
const long MAP_END[MAP_N]  = {  297,  449,  600,  751,  902, 1053, 1204 };
const int  MAP_TURN[MAP_N] = {  322,   28, -120, -214, -135, -249,  -73 };
//                              T1          T2 horseshoe (about 230 degrees left)
// Steer into the curve from the map only on the round T2 horseshoe. T1 is a
// corner (the map can't say exactly where in its piece it starts), so normal
// line following handles T1.
const bool MAP_ROUND[MAP_N] = { false, false, true, true, true, true, true };
const long START_BAR_DIST = 147; // where the map starts
// T1 is the first landmark: once the robot has turned right this much (about 60
// degrees), counted from where it stopped turning left (some boxes join the line
// with a left turn), it is part-way round T1, at this map distance.
const long T1_SYNC_TURN = 200;
const long T1_SYNC_DIST = 240;
// The horseshoe entry is the second landmark: after T1, the first time the robot
// has turned left this much within the last 60 counts, it is at this map distance.
// The planned horseshoe steering only starts once this has happened.
const long HS_SYNC_TURN = -45;
const long HS_SYNC_DIST = 520;
const long PART_END = 1150;      // stop here (short straight heading into T3)

// ---------- state ----------
int  v[8];
long weighted = 0, total = 0;
int  blackSensors = 0, strongBlack = 0, lines = 0;
long lastPosition = 3500;
long lastError = 0;
bool firstReading = true;
int  speedNow = START_SPEED;
bool started = false;            // found the line after leaving the box
bool synced = false;             // distance lined up with the map at T1
bool hsSynced = false;           // lined up again at the horseshoe entry
bool joined = false;             // centered on the line after finding it
int  centeredReads = 0;
long turnSinceStart = 0;         // turning since the line was found (+ = right)
long minTurn = 0;                // most left it has turned (for the T1 check)
// turning over the last 3 chunks of 20 counts (for the horseshoe entry check)
long chunkTravel2 = 0, chunkTurn = 0;
long chunkTurns[3] = { 0, 0, 0 };
bool everSawLine = false;
bool done = false;
unsigned long lostTime = 0;
bool spinRight = false;
unsigned long startTime = 0;     // when it crossed the start bar
unsigned long partTime = 0;

int  leftCmd = 0, rightCmd = 0;
long prevL = 0, prevR = 0;
long travel2 = 0;                // distance x2 (left + right counts)

// log, one line per ~150 counts
const int LOG_MAX = 30;
long logDist[LOG_MAX];
int  logTurn[LOG_MAX], logMs[LOG_MAX], logMaxErr[LOG_MAX], logSpeed[LOG_MAX], logLost[LOG_MAX];
int  logAvgErr[LOG_MAX];
int  logCount = 0;
long segTravel2 = 0, segTurn = 0, segMaxErr = 0, segSpeedSum = 0, segReads = 0;
long segErrSum = 0;
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
  if (logCount < LOG_MAX && segReads > 0) {
    logDist[logCount] = travel2 / 2;
    logTurn[logCount] = segTurn;
    logMs[logCount] = nowMs() - segStart;
    logMaxErr[logCount] = segMaxErr;
    logSpeed[logCount] = segSpeedSum / segReads;
    logLost[logCount] = segLost;
    logAvgErr[logCount] = segErrSum / segReads;
    logCount++;
  }
  segTravel2 = 0; segTurn = 0; segMaxErr = 0; segSpeedSum = 0; segReads = 0; segLost = 0;
  segErrSum = 0;
  segStart = nowMs();
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
  if (started) {
    turnSinceStart += dl - dr;
    if (turnSinceStart < minTurn) minTurn = turnSinceStart;
    chunkTravel2 += dl + dr;
    chunkTurn += dl - dr;
    if (chunkTravel2 >= 40) {                  // every 20 counts of travel
      chunkTurns[0] = chunkTurns[1];
      chunkTurns[1] = chunkTurns[2];
      chunkTurns[2] = chunkTurn;
      chunkTravel2 = 0;
      chunkTurn = 0;
    }
    segTravel2 += dl + dr;
    segTurn += dl - dr;
    if (segTravel2 >= 300) saveLog();
  }
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
  lines = 0;
  long best = 100000;
  int i = 0;
  while (i < 8) {
    if (v[i] == 0) { i++; continue; }
    long w = 0, t = 0;
    while (i < 8 && v[i] > 0) { w += (long)v[i] * i * 1000L; t += v[i]; i++; }
    lines++;
    long d = labs(w / t - lastPosition);
    if (d < best) { best = d; weighted = w; total = t; }
  }
}

// Which map piece we are in (-1 = off the map)
int pieceAt(long d) {
  for (int i = 0; i < MAP_N; i++) {
    long start = (i == 0) ? START_BAR_DIST : MAP_END[i - 1];
    if (d >= start && d <= MAP_END[i]) return i;
  }
  return -1;
}

// Planned curve at this distance, blended smoothly between the middles of the map
// pieces (turn counts per count travelled; + = right). Only the round pieces.
float mapCurve(long d) {
  float c[MAP_N];
  long mid[MAP_N];
  for (int i = 0; i < MAP_N; i++) {
    long start = (i == 0) ? START_BAR_DIST : MAP_END[i - 1];
    c[i] = MAP_ROUND[i] ? (float)MAP_TURN[i] / (MAP_END[i] - start) : 0;
    mid[i] = (start + MAP_END[i]) / 2;
  }
  if (d <= mid[0]) return c[0];
  for (int i = 0; i < MAP_N - 1; i++) {
    if (d <= mid[i + 1]) {
      float f = (float)(d - mid[i]) / (mid[i + 1] - mid[i]);
      return c[i] + f * (c[i + 1] - c[i]);
    }
  }
  return 0;                                    // past the end of the map: straight
}

// Speed for this distance: each zone ahead allows its speed plus the room left to brake
int targetSpeed(long d) {
  long best = 255;
  long zoneStart = 0;
  for (int z = 0; z < NUM_ZONES; z++) {
    if (ZONE_END[z] >= d) {
      long allowed = ZONE_SPEED[z];
      if (zoneStart > d) allowed += (zoneStart - d) / BRAKE_COUNTS;
      if (allowed < best) best = allowed;
    }
    zoneStart = ZONE_END[z];
  }
  return best;
}

void printLog() {
  Serial.println();
  Serial.println("===== PART 1 START (copy from here) =====");
  Serial.print("part_time_ms ");  Serial.println(partTime);
  Serial.print("end_dist ");      Serial.println(travel2 / 2);
  Serial.print("synced_at_T1 ");  Serial.println(synced ? 1 : 0);
  Serial.print("synced_at_horseshoe "); Serial.println(hsSynced ? 1 : 0);
  Serial.print("ff_gain ");       Serial.println(FF_GAIN);
  Serial.print("curve_kp ");      Serial.println(CURVE_KP_FACTOR);
  Serial.print("start_speed ");  Serial.println(START_SPEED);
  Serial.print("pre_t1_speed "); Serial.println(PRE_T1_SPEED);
  Serial.print("kp_sharp ");     Serial.println(KP_SHARP);
  Serial.print("zone_speeds ");
  for (int z = 0; z < NUM_ZONES; z++) { Serial.print(ZONE_SPEED[z]); Serial.print(' '); }
  Serial.println();
  // avgerr: average line position. In a LEFT curve (the horseshoe):
  //   negative = line on the robot's left  = robot running WIDE (outside)
  //   positive = line on the robot's right = robot CUTTING INSIDE
  Serial.println("dist,turn,lost,ms,maxerr,speed,avgerr");
  for (int i = 0; i < logCount; i++) {
    Serial.print(logDist[i]);   Serial.print(',');
    Serial.print(logTurn[i]);   Serial.print(',');
    Serial.print(logLost[i]);   Serial.print(',');
    Serial.print(logMs[i]);     Serial.print(',');
    Serial.print(logMaxErr[i]); Serial.print(',');
    Serial.print(logSpeed[i]);  Serial.print(',');
    Serial.println(logAvgErr[i]);
  }
  Serial.println("===== PART 1 END (copy to here) =====");
}

void finish() {
  drive(0, 0);
  if (!done) {
    saveLog();
    partTime = nowMs() - startTime;
    done = true;
  }
  digitalWrite(LED_FRONT_LEFT, HIGH);
  digitalWrite(LED_FRONT_RIGHT, HIGH);
  digitalWrite(LED_BACK_LEFT, HIGH);
  digitalWrite(LED_BACK_RIGHT, HIGH);
  if (nowMs() - lastPrint > 5000) {
    printLog();
    lastPrint = nowMs();
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
  prevL = getEncoderCount_left();
  prevR = getEncoderCount_right();
}

void loop() {
  readSensors();
  updateEncoders();
  long dist = travel2 / 2;

  // Stop: end of Part 1, or a bump switch pressed
  bool bumped = false;
  for (int i = 0; i < 6; i++) {
    if (digitalRead(BUMP_PINS[i]) == LOW) bumped = true;
  }
  if (done || bumped || (synced && dist >= PART_END)) {
    finish();
    return;
  }

  if (startTime == 0) {
    startTime = nowMs();
    segStart = startTime;
  }

  // ----- Start: drive straight out of the box until the line is found -----
  // (a wide black patch, like the start bar, is driven straight over)
  if (!started) {
    drive(START_SPEED, START_SPEED);
    if (total > 0 && strongBlack < 5) {
      started = true;
      travel2 = 2 * START_BAR_DIST;
    }
    return;
  }

  // ----- Line the distance up with the map at T1 (first big right turn) -----
  if (!synced && turnSinceStart - minTurn >= T1_SYNC_TURN) {
    synced = true;
    travel2 = 2 * T1_SYNC_DIST;
    dist = T1_SYNC_DIST;
  }
  // ----- ...and again where the horseshoe starts curving left -----
  if (synced && !hsSynced && dist > T1_SYNC_DIST + 80 &&
      chunkTurns[0] + chunkTurns[1] + chunkTurns[2] <= HS_SYNC_TURN) {
    hsSynced = true;
    travel2 = 2 * HS_SYNC_DIST;
    dist = HS_SYNC_DIST;
  }

  // ----- Speed -----
  int target = synced ? targetSpeed(dist) : PRE_T1_SPEED;
  if (!joined) target = JOIN_SPEED;           // ease onto the line first
  if (speedNow < target) speedNow += 2;       // speed up gently
  if (speedNow > target) speedNow = target;   // slow down at once

  // ----- Line lost -----
  if (total == 0) {
    if (!everSawLine) {                        // not found the line yet: go straight
      drive(JOIN_SPEED, JOIN_SPEED);
      return;
    }
    if (lostTime == 0) {
      lostTime = nowMs();
      segLost++;
      int p = synced ? pieceAt(dist) : -1;
      if (labs(lastError) < 1000 && p >= 0) spinRight = MAP_TURN[p] > 0;  // map says which way
      else spinRight = lastError > 0;
      lastPosition = spinRight ? 0 : 7000;
    }
    if (nowMs() - lostTime > 1500) {          // could not find it: stop and print
      finish();
      return;
    }
    if (spinRight) drive(SEARCH_SPEED, -SEARCH_SPEED);
    else           drive(-SEARCH_SPEED, SEARCH_SPEED);
    speedNow = JOIN_SPEED;                     // continue at joining speed once found
    firstReading = true;
    return;
  }
  lostTime = 0;
  everSawLine = true;

  // ----- Follow the line -----
  long position = weighted / total;
  lastPosition = position;
  long error = 3500 - position;                // > 0: line is to the RIGHT
  long change = firstReading ? 0 : error - lastError;
  firstReading = false;
  lastError = error;

  // P (and the extra at the edge) scale with speed: the faster it goes, the bigger
  // the wheel difference needed for the same correction. D does not: the error
  // already changes faster at higher speed, so D grows by itself.
  float correction = KP * error;
  if (hsSynced && labs(error) < 1500) correction *= CURVE_KP_FACTOR;  // planned path does the turning
  if (error > SHARP_ERROR)  correction += KP_SHARP * (error - SHARP_ERROR);
  if (error < -SHARP_ERROR) correction += KP_SHARP * (error + SHARP_ERROR);
  if (speedNow > 50) correction = correction * speedNow / 50;   // faster -> steer more
  correction += KD * change;

  // Steer into the curve the map says is here (a little ahead)
  // (only once lined up at the horseshoe entry, so it never turns early)
  if (hsSynced) correction += FF_GAIN * speedNow * mapCurve(dist + 30) / 2;

  // Wheel speeds. If the outer wheel would go past full power (255), move BOTH
  // wheels down so the difference (the turn) is kept exactly.
  int left  = (int)(speedNow + correction);
  int right = (int)(speedNow - correction);
  if (left > 255)  { right -= left - 255;  left = 255; }
  if (right > 255) { left  -= right - 255; right = 255; }
  left  = constrain(left,  -MAX_REVERSE, 255);
  right = constrain(right, -MAX_REVERSE, 255);
  drive(left, right);

  // Joined: line centered for a moment after finding it
  if (!joined) {
    if (labs(error) < 800) centeredReads++;
    else centeredReads = 0;
    if (centeredReads >= 10) joined = true;
  }

  // for the log
  if (labs(error) > segMaxErr) segMaxErr = labs(error);
  segSpeedSum += speedNow;
  segErrSum += error;
  segReads++;
}

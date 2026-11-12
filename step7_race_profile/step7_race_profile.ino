// STEP 7: Race with a speed profile. Every piece of the track gets its own speed,
// worked out from the map run: fast on straights, slower on curves, slow on
// hairpins. It looks ahead on the map and brakes before each turn. It still
// records the map, so every run can be checked and tuned. When it stops, plug in USB and open the Serial Monitor
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
const int   SPIN_SPEED = 65;     // how fast it spins for the U-turn

// Sharp turns: when the line reaches the outer sensors, turn much harder
// so the inner wheel goes BACKWARDS. Set KP_SHARP = 0 to switch this off.
const int   SHARP_ERROR = 2000;  // line under the outer 2 sensors on either side
const float KP_SHARP    = 0.05;  // extra steering beyond SHARP_ERROR
const int   MAX_REVERSE = 50;    // fastest a wheel may go backwards


// ---- Speed profile (change these to go faster) ----
// Speed for each kind of track piece, before SPEED_PERCENT is applied.
const int V_STRAIGHT = 110;      // (almost) no turning
const int V_GENTLE   = 80;       // gentle curve
const int V_CURVE    = 60;       // curve
const int V_HAIRPIN  = 50;       // hairpin / sharp V (not changed by SPEED_PERCENT)
const int SPEED_PERCENT = 130;   // raise step by step: 100 -> 130 -> 160 -> ...
const float KD = 0.07;           // damping (course slides: Kd = 5..10 x Kp). 0 = off
const int BRAKE_COUNTS = 3;      // encoder counts it needs to lose 1 unit of speed.
                                 // Raise if it arrives at turns too fast.

// "Flowing" sections: T6 -> T15 (both ways). Curves, but no hairpins, so it
// drives faster there and steers more gently, with no wheel going backwards.
// If the line gets near the edge of the sensors, full steering comes back.
const long FLOW_SECTIONS[][2] = {
  {  4080,  6350 },   // way out:  after T6 -> before T16
  {  8540, 10950 },   // way back: after T16 -> before T6
};
const int   NUM_FLOW     = 2;
const int   FLOW_BOOST   = 120;  // % speed on top of the profile in these sections
const float FLOW_STEER   = 0.75; // steering strength there (1.0 = normal)
const int   FLOW_RESCUE  = 2800; // line this far off-center: full steering again

// Planned turns at the hairpins (zones from the map runs). If the line slips off
// the sensors inside a zone, it does not search: it knows which way the turn goes.
//  - Sharp corner (T5 ~95 degrees, T6 and T16 ~145 degree V): roll forward so the
//    WHEELS are at the corner, spin fast most of the way round, then slower until
//    the line is under the middle sensors.
//  - U hairpin (T3, T4): spin straight away (fast, then slower) onto the line.
const bool PLANNED_TURNS_ON = true;   // false = old behaviour (search for the line)
const long PLANNED_TURNS[][5] = {
  // from,   to,  direction (1 = right),  roll forward,  fast spin (turn counts)
  {  1150,  1550,   1,   0, 150 },   // way out:  T3
  {  1650,  1950,  -1,   0, 150 },   // way out:  T4
  {  2000,  2350,   1,  80, 250 },   // way out:  T5 (~95 degree corner)
  {  3550,  4100,   1,  80, 350 },   // way out:  T6 (V)
  {  6450,  6900,   1,  80, 350 },   // way out:  T16 (V)
  {  8050,  8500,  -1,  80, 350 },   // way back: T16 (V)
  { 10850, 11350,  -1,  80, 350 },   // way back: T6 (V)
  { 12700, 13050,  -1,  80, 250 },   // way back: T5 (~95 degree corner)
  { 13060, 13350,   1,   0, 150 },   // way back: T4
  { 13450, 13800,  -1,   0, 150 },   // way back: T3
};
const int NUM_PLANNED       = 10;
const int PLANNED_FAST_SPIN = 90;  // spin speed for the first part
const int PLANNED_SLOW_SPIN = 45;  // spin speed once it is nearly round
const int PLANNED_MAX_TURN  = 700; // turned this far without finding the line: give up

// Round curves: steer INTO the curve from the map (the line following then only
// fixes the small error). 0 = off, 1.0 = the full curve from the map.
const float FF_GAIN = 0.5;

// U-turn at the end bar: spin fast this many turn counts (~150 degrees), then
// slower until the line is under the middle sensors.
const int UTURN_FAST_TURN = 480;

// The map, from the map run. Each piece is about 150 encoder counts long.
// MAP_DIST = distance at the END of the piece, MAP_TURN = how much it turned
// (+ = right, - = left, about 3.3 = 1 degree), MAP_LOST = line was lost there.
const int  MAP_OUT_PIECES = 50;          // the first 50 pieces are the way out
const int  MAP_PIECES = 100;
const long MAP_OUT_START  = 147;   // distance when the start bar was crossed
const long MAP_OUT_END    = 7565;  // distance of the end bar
const long MAP_BACK_START = 7482;  // distance when the way back began (after the U-turn)
const int MAP_DIST[MAP_PIECES] = {
    297,   449,   600,   751,   902,  1053,  1204,  1354,  1506,  1656,  1806,  1956,
   2107,  2257,  2407,  2558,  2708,  2859,  3010,  3160,  3310,  3461,  3613,  3764,
   3914,  4065,  4216,  4366,  4517,  4667,  4819,  4969,  5119,  5270,  5421,  5571,
   5721,  5872,  6023,  6173,  6324,  6476,  6626,  6776,  6927,  7077,  7228,  7379,
   7530,  7565,  7633,  7784,  7935,  8086,  8237,  8387,  8538,  8688,  8838,  8989,
   9139,  9289,  9440,  9592,  9742,  9893, 10044, 10195, 10346, 10496, 10647, 10799,
  10950, 11100, 11251, 11403, 11554, 11705, 11856, 12006, 12156, 12307, 12457, 12608,
  12760, 12911, 13063, 13214, 13366, 13517, 13667, 13817, 13967, 14117, 14267, 14418,
  14569, 14721, 14872, 15014
};
const int MAP_TURN[MAP_PIECES] = {
    298,    55,  -137,  -203,  -139,  -254,   -82,   358,   347,   154,  -538,  -334,
     38,   504,    54,   -91,    34,    15,   -48,    18,     6,   -41,     7,     9,
    476,   177,   -65,   -18,   168,    68,   165,  -379,   170,   198,  -291,  -186,
    170,  -101,    83,    96,  -207,  -207,    26,   527,   131,   -69,    11,    33,
    -30,    -6,  -164,    17,    69,   -83,  -231,  -389,    47,   262,   112,   -81,
   -146,   216,  -266,   327,   200,  -340,   -40,   410,  -294,    15,   -97,   -86,
     98,  -258,  -405,    -7,    67,   -39,    16,    33,   -22,     2,    30,   -12,
    -11,  -473,   -52,   462,   345,  -108,  -486,  -207,    80,   260,   174,   138,
    176,   -70,  -327,   -21
};
const byte MAP_LOST[MAP_PIECES] = {
      0,     0,     0,     0,     0,     0,     0,     1,     0,     0,     1,     0,
      0,     1,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      1,     0,     0,     0,     0,     0,     0,     1,     0,     0,     0,     0,
      0,     0,     0,     0,     0,     0,     0,     1,     0,     0,     0,     0,
      0,     0,     0,     0,     0,     0,     1,     0,     0,     0,     0,     0,
      0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      0,     1,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      0,     1,     0,     1,     0,     0,     1,     0,     0,     0,     0,     0,
      0,     0,     0,     0
};
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
float lineWidth = 2.5;          // learned: how many sensors a normal line makes very black
int  speedNow = BASE_SPEED;     // current forward speed
long slowUntil = 0;             // after losing the line: hairpin speed until this distance
bool firstReading = true;       // first reading after finding the line (no D yet)
int  plannedStep = 0;           // 0 = not doing a planned turn, 1..3 = which part
int  plannedDir = 1;            // 1 = right, -1 = left
int  plannedForward = 0;        // how far to roll forward first
int  plannedFastTurn = 0;       // how far to spin fast
long plannedStartDist = 0;
long plannedStartTurn = 0;
bool plannedUsed[NUM_PLANNED];  // each planned turn happens once
long uturnStartTurn = 0;

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
long turnTotal = 0;             // all the turning so far (same units)
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
  turnTotal += dl - dr;
  if (segTravel2 >= 2 * SEG_COUNTS) saveSegment();
}

// Where map piece i begins
long pieceStartDist(int i) {
  if (i == 0) return MAP_OUT_START;
  if (i == MAP_OUT_PIECES) return MAP_BACK_START;
  return MAP_DIST[i - 1];
}

// If the map has a hairpin within 150 counts of here, which way it turns:
// 1 = right, -1 = left, 0 = no hairpin here
int hairpinDirection(long d) {
  int first = (phase == OUT) ? 0 : MAP_OUT_PIECES;
  int last  = (phase == OUT) ? MAP_OUT_PIECES : MAP_PIECES;
  for (int i = first; i < last; i++) {
    if (MAP_DIST[i] < d - 150 || pieceStartDist(i) > d + 150) continue;
    if (MAP_LOST[i] || abs(MAP_TURN[i]) >= 300) return MAP_TURN[i] > 0 ? 1 : -1;
  }
  return 0;
}

// Spin in place: dir 1 = right, -1 = left
void pivot(int dir, int speed) {
  if (dir > 0) drive(speed, -speed);
  else         drive(-speed, speed);
}

// Back to normal line following after a planned turn
void endPlannedTurn(long d) {
  plannedStep = 0;
  firstReading = true;
  lastPosition = 3500;
  lastError = 0;
  lostTime = 0;
  straightCount = 0;
  otherLineTime = 0;
  speedNow = V_HAIRPIN;
  slowUntil = d + 100;
}

// How much the map turns per encoder count at this distance (from the piece
// we are in), for steering into round curves. Hairpins and pieces where the line
// was lost are left out (those turns were search spins, not the track shape).
float mapCurve(long d) {
  int first = (phase == OUT) ? 0 : MAP_OUT_PIECES;
  int last  = (phase == OUT) ? MAP_OUT_PIECES : MAP_PIECES;
  for (int i = first; i < last; i++) {
    if (MAP_DIST[i] < d || pieceStartDist(i) > d) continue;
    if (MAP_LOST[i] || abs(MAP_TURN[i]) >= 400) return 0;
    return (float)MAP_TURN[i] / (MAP_DIST[i] - pieceStartDist(i));
  }
  return 0;
}

// Is this distance inside a flowing section?
bool inFlow(long d) {
  for (int f = 0; f < NUM_FLOW; f++) {
    if (d >= FLOW_SECTIONS[f][0] && d <= FLOW_SECTIONS[f][1]) return true;
  }
  return false;
}

// Speed for one piece of the map, from how much it turns
int pieceSpeed(int i) {
  int t = abs(MAP_TURN[i]);
  if (MAP_LOST[i] || t >= 300) return V_HAIRPIN;
  long v;
  if (t >= 150)     v = V_CURVE;
  else if (t >= 60) v = V_GENTLE;
  else              v = V_STRAIGHT;
  v = v * SPEED_PERCENT / 100;
  if (inFlow(pieceStartDist(i)) && inFlow(MAP_DIST[i])) v = v * FLOW_BOOST / 100;
  if (v > 255) v = 255;
  return v;
}

// Target speed at this distance. Each piece ahead allows a speed that rises the
// further away it is (there is room to brake), so the robot brakes just in time
// for each turn: speed allowed = piece speed + distance to it / BRAKE_COUNTS.
int mapSpeed(long d, int speed) {
  int first = (phase == OUT) ? 0 : MAP_OUT_PIECES;
  int last  = (phase == OUT) ? MAP_OUT_PIECES : MAP_PIECES;
  long v = 255;
  bool onMap = false;
  for (int i = first; i < last; i++) {
    long pieceEnd = MAP_DIST[i];
    long pieceStart = pieceStartDist(i);
    if (pieceEnd < d) continue;                       // already behind us
    onMap = true;
    long allowed = pieceSpeed(i);
    if (pieceStart > d) allowed += (pieceStart - d) / BRAKE_COUNTS;
    if (allowed < v) v = allowed;
    if (pieceStart - d > 255L * BRAKE_COUNTS) break;  // too far ahead to matter
  }
  if (!onMap) v = BASE_SPEED;    // past the end of the map
  // Arrive at the end bar gently so it is recognized
  if (phase == OUT && d > MAP_OUT_END - 300 && v > V_GENTLE) v = V_GENTLE;
  return v;
}

void setPhase(int p) {
  saveSegment();
  // Line up the distance with the map at two known points
  if (p == OUT)  travel2 = 2 * MAP_OUT_START;     // just crossed the start bar
  if (p == BACK) travel2 = 2 * MAP_BACK_START;    // just finished the U-turn
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
  Serial.print("speed_percent "); Serial.println(SPEED_PERCENT);
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
    long turned = uturnStartTurn - turnTotal;       // spinning left: counts go negative
    if (uturnStep == 0) {
      // Spin left fast most of the way round (the bar is passed in this part)
      drive(-PLANNED_FAST_SPIN, PLANNED_FAST_SPIN);
      if (turned >= UTURN_FAST_TURN) uturnStep = 1;
    } else {
      // Slower until the line we came in on is under the middle sensors
      drive(-PLANNED_SLOW_SPIN, PLANNED_SLOW_SPIN);
      if (centerBlack) {
        drive(0, 0);
        lastError = 0;
        lastPosition = 3500;
        lostTime = 0;
        straightCount = 0;
        otherLineTime = 0;
        firstReading = true;
        slowUntil = 0;
        speedNow = V_HAIRPIN;
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

  long distNow = travel2 / 2;

  // ---------- Planned turn (T6) in progress ----------
  if (plannedStep > 0) {
    long turned = (turnTotal - plannedStartTurn) * plannedDir;
    if (plannedStep == 1) {
      // Roll forward so the wheels are at the tip of the V
      drive(V_HAIRPIN, V_HAIRPIN);
      if (centerBlack) {
        endPlannedTurn(distNow);              // the line is back: it was not the tip
      } else if (distNow - plannedStartDist >= plannedForward) {
        plannedStep = 2;
        plannedStartTurn = turnTotal;
      }
    } else if (plannedStep == 2) {
      pivot(plannedDir, PLANNED_FAST_SPIN);   // fast most of the way round
      if (turned >= plannedFastTurn) plannedStep = 3;
    } else {
      pivot(plannedDir, PLANNED_SLOW_SPIN);   // slower until the line is in the middle
      if (centerBlack) {
        endPlannedTurn(distNow);
      } else if (turned >= PLANNED_MAX_TURN) {
        endPlannedTurn(distNow);              // not found: fall back to the normal search
        everSawLine = true;
      }
    }
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

  // Bars are only looked for where the map says one should be: the last part of
  // the way out (after T16) and the last 15% of the way back.
  bool barWindow = (phase == OUT && distNow >= MAP_OUT_END - 530) || (phase == BACK && nearStart);

  // It only counts as a bar if 7 or more sensors are very black, 2 readings in a
  // row (ignores one-off glitches), inside the bar window
  bool looksLikeBar = strongBlack >= 7 && barWindow;
  if (looksLikeBar) {
    allBlackCount++;
    if (allBlackCount >= 2) {
      allBlackCount = 0;
      drive(0, 0);
      if (phase == OUT) {
        delay(80);              // let the robot stop
        uturnStep = 0;
        uturnStartTurn = turnTotal;
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
  if ((drivingStraight || barWindow) && lines == 1 && strongBlack > normalWidth) {
    drive(BASE_SPEED, BASE_SPEED);
    return;
  }

  // ---------- Speed from the map ----------
  int targetSpeed = mapSpeed(distNow, speedNow);
  if (distNow < slowUntil && targetSpeed > V_HAIRPIN) targetSpeed = V_HAIRPIN;
  if (speedNow < targetSpeed) speedNow += 2;     // speed up gently
  if (speedNow > targetSpeed) speedNow = targetSpeed;   // slow down at once

  // ---------- Follow the line ----------
  if (total == 0) {
    speedNow = V_HAIRPIN;                        // lost the line: slow down
    slowUntil = distNow + 150;                   // and stay slow until past here
    firstReading = true;
    straightCount = 0;
    // Not found the line yet after the start: keep going straight
    if (!everSawLine) {
      drive(BASE_SPEED, BASE_SPEED);
      return;
    }
    // Lost the line: turn toward where it was last seen, give up after 2 seconds.
    // If it was lost while roughly centered (tip of a V) and a second line was seen
    // just before, the track continues on that second line's side.
    // Line ended inside a planned-turn zone (tip of T6): do the planned turn
    if (PLANNED_TURNS_ON && lostTime == 0) {
      for (int k = 0; k < NUM_PLANNED; k++) {
        bool rightLeg = (phase == OUT) == (PLANNED_TURNS[k][0] < MAP_OUT_END);
        if (rightLeg && !plannedUsed[k] &&
            distNow >= PLANNED_TURNS[k][0] && distNow <= PLANNED_TURNS[k][1]) {
          plannedUsed[k] = true;
          plannedDir = PLANNED_TURNS[k][2];
          plannedForward = PLANNED_TURNS[k][3];
          plannedFastTurn = PLANNED_TURNS[k][4];
          plannedStartDist = distNow;
          plannedStartTurn = turnTotal;
          plannedStep = (plannedForward > 0) ? 1 : 2;
          segLost++;
          drive(V_HAIRPIN, V_HAIRPIN);
          return;
        }
      }
    }
    if (lostTime == 0) {
      lostTime = millis();
      segLost++;
      int mapTurn = hairpinDirection(distNow);
      if (mapTurn != 0) {
        spinRight = mapTurn > 0;                  // the map knows which way this hairpin goes
      } else if (labs(lastError) < 1500 && millis() - otherLineTime < 1000) {
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
  long change = firstReading ? 0 : error - lastError;   // for the D part
  firstReading = false;
  lastError = error;

  // Only a normal-width line counts: not a sensor bar lying along the line mid-spin,
  // and not the end bar coming in at an angle (that also pulls the error sideways)
  bool normalLine = lines == 1 && strongBlack <= normalWidth;
  if (normalLine && labs(error) < 1000) {
    straightCount++;
    lineWidth += 0.01 * (strongBlack - lineWidth);    // keep learning the line width
  }
  if (normalLine && labs(error) > SHARP_ERROR) straightCount = 0;   // sharp turn

  float correction = KP * error + KD * change;

  // In a flowing section: gentler steering and no sharp-turn mode, unless the
  // line is close to slipping off the edge of the sensors
  bool flowing = inFlow(distNow) && labs(error) < FLOW_RESCUE;
  if (flowing) {
    correction = correction * FLOW_STEER;
  } else {
    // Sharp turn: line near the edge of the sensors -> extra steering
    if (error > SHARP_ERROR)  correction += KP_SHARP * (error - SHARP_ERROR);
    if (error < -SHARP_ERROR) correction += KP_SHARP * (error + SHARP_ERROR);
  }

  // Steer in proportion to speed: twice as fast needs twice the wheel difference
  // for the same turn. (All the gains above were tuned at BASE_SPEED.)
  if (speedNow > BASE_SPEED) correction = correction * speedNow / BASE_SPEED;

  // Round curve from the map: steer into it. For a curve of c turn counts per
  // count travelled, the wheels need speeds of speed x (1 +- c/2).
  correction += FF_GAIN * speedNow * mapCurve(distNow + 30) / 2;
  int maxSpeed = speedNow * 2;                   // outer wheel limit
  if (maxSpeed < 100) maxSpeed = 100;
  if (maxSpeed > 255) maxSpeed = 255;

  int minSpeed = flowing ? 0 : -MAX_REVERSE;     // no backwards wheel when flowing
  int left  = constrain((int)(speedNow + correction), minSpeed, maxSpeed);
  int right = constrain((int)(speedNow - correction), minSpeed, maxSpeed);
  drive(left, right);
}

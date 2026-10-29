// UCLA ECE 3 - TI-RSLK line follower (slow + reliable first)
//
// ONE sketch, three modes. Change MODE below and re-upload.
//   MODE 1 = SENSOR TEST : motors off, prints what the robot "sees" (Serial 9600)
//   MODE 2 = MOTOR TEST  : WHEELS OFF THE GROUND. Spins each wheel, then ramps PWM
//   MODE 0 = DRIVE       : follow the line
//
// Do MODE 1, then MODE 2, then MODE 0. See README.md in this folder.

#include <ECE3.h>

#define MODE 1

// ---------------- Orientation (verify with MODE 1 and MODE 2) ----------------
// On the TI-RSLK, IR sensor 0 (P7.0) is on the robot's RIGHT side and sensor 7
// is on the LEFT (looking down at the robot from behind, front pointing away).
// If MODE 1 says "LINE RIGHT" when the tape is on the robot's left, flip this.
const bool SENSOR0_ON_RIGHT = true;

// Direction-pin level that makes each wheel roll FORWARD. Flip one in MODE 2
// if that wheel spins backwards.
const int LEFT_FORWARD  = LOW;
const int RIGHT_FORWARD = LOW;

// ---------------- Pins ----------------
const int LEFT_SLEEP  = 31;
const int LEFT_DIR    = 29;
const int LEFT_PWM    = 40;
const int RIGHT_SLEEP = 11;
const int RIGHT_DIR   = 30;
const int RIGHT_PWM   = 39;

// ---------------- Calibration (raw ECE3_read_IR values, indexed like the library) ----------------
const int sensorMin[8] = { 798, 688, 642, 635, 612, 688, 735, 853 };      // white
const int sensorMax[8] = { 2500, 2500, 2500, 2016, 2213, 2162, 2500, 2500 }; // black

// ---------------- Tuning ----------------
const int   BASE_SPEED   = 50;   // forward PWM on a straight. Must be above the stall PWM from MODE 2
const int   MAX_SPEED    = 100;  // cap for the outer wheel
const int   MAX_REVERSE  = 40;   // inner wheel may back up this much on sharp curves
const float KP           = 0.020; // PWM per unit of error (error is -3500..+3500)
const float KD           = 0.0;   // start P-only; try 0.02-0.05 later if it wiggles on straights
const int   SEARCH_SPEED = 45;   // pivot speed while hunting for a lost line
const unsigned long LOST_TIMEOUT_MS = 2000; // give up searching after this long

const int NOISE_FLOOR    = 150;  // normalized readings below this are treated as pure white
const int BLACK_LEVEL    = 300;  // a sensor with a normalized reading above this "sees" the line
const int ALL_BLACK_SENSORS = 7; // this many black sensors = end bar or robot lifted -> stop

// ---------------- State ----------------
uint16_t raw[8];
int norm[8];          // 0 (white) .. 1000 (black), ordered LEFT -> RIGHT
int blackCount = 0;
long lineError = 0;   // negative = line is to the robot's LEFT, positive = RIGHT
long lastError = 0;
unsigned long lostSince = 0;
int allBlackReads = 0;

// Reads the sensors. Returns true if the line is visible and updates lineError.
bool readLine() {
  ECE3_read_IR(raw);

  long weighted = 0;
  long total = 0;
  blackCount = 0;

  for (int pos = 0; pos < 8; pos++) {           // pos 0 = leftmost, 7 = rightmost
    int s = SENSOR0_ON_RIGHT ? 7 - pos : pos;   // library index for that position
    long v = ((long)raw[s] - sensorMin[s]) * 1000L / (sensorMax[s] - sensorMin[s]);
    v = constrain(v, 0L, 1000L);
    if (v < NOISE_FLOOR) v = 0;
    norm[pos] = v;
    if (v >= BLACK_LEVEL) blackCount++;
    weighted += v * pos * 1000L;
    total += v;
  }

  if (blackCount == 0 || total == 0) return false;
  lineError = weighted / total - 3500;
  return true;
}

// speed > 0 forward, < 0 backward
void setMotor(int dirPin, int pwmPin, int forwardLevel, int speed) {
  speed = constrain(speed, -255, 255);
  if (speed >= 0) {
    digitalWrite(dirPin, forwardLevel);
    analogWrite(pwmPin, speed);
  } else {
    digitalWrite(dirPin, forwardLevel == LOW ? HIGH : LOW);
    analogWrite(pwmPin, -speed);
  }
}

void setMotors(int left, int right) {
  setMotor(LEFT_DIR, LEFT_PWM, LEFT_FORWARD, left);
  setMotor(RIGHT_DIR, RIGHT_PWM, RIGHT_FORWARD, right);
}

// Status LED on the LaunchPad: green = following, blue = searching, red = stopped
void setLed(bool r, bool g, bool b) {
  digitalWrite(RED_LED, r ? HIGH : LOW);
  digitalWrite(GREEN_LED, g ? HIGH : LOW);
  digitalWrite(BLUE_LED, b ? HIGH : LOW);
}

void setup() {
  ECE3_Init();
  Serial.begin(9600);

  pinMode(LEFT_SLEEP, OUTPUT);
  pinMode(LEFT_DIR, OUTPUT);
  pinMode(LEFT_PWM, OUTPUT);
  pinMode(RIGHT_SLEEP, OUTPUT);
  pinMode(RIGHT_DIR, OUTPUT);
  pinMode(RIGHT_PWM, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);

  setMotors(0, 0);
  digitalWrite(LEFT_SLEEP, HIGH);   // wake the motor drivers
  digitalWrite(RIGHT_SLEEP, HIGH);

  if (MODE == 0) {
    // 3 second countdown: blink red so you know it is about to move
    for (int i = 0; i < 6; i++) {
      setLed(i % 2 == 0, false, false);
      delay(500);
    }
  }
  Serial.println(MODE == 0 ? "DRIVE MODE" : MODE == 1 ? "SENSOR TEST MODE" : "MOTOR TEST MODE");
}

void loop() {
  if (MODE == 1) sensorTest();
  else if (MODE == 2) motorTest();
  else drive();
}

// ---------------- MODE 0: drive ----------------
void drive() {
  bool seen = readLine();

  // Every sensor black: either the end bar or someone picked the robot up. Stop.
  if (blackCount >= ALL_BLACK_SENSORS) {
    if (++allBlackReads >= 3) {
      setMotors(0, 0);
      setLed(true, false, true);
      return;
    }
  } else {
    allBlackReads = 0;
  }

  if (!seen) {
    if (lostSince == 0) lostSince = millis();
    if (millis() - lostSince > LOST_TIMEOUT_MS) {
      setMotors(0, 0);              // searched long enough; wait until the line is back
      setLed(true, false, false);
      return;
    }
    // Pivot toward the side where the line was last seen
    if (lastError >= 0) setMotors(SEARCH_SPEED, -SEARCH_SPEED);   // line went off to the right
    else                setMotors(-SEARCH_SPEED, SEARCH_SPEED);   // line went off to the left
    setLed(false, false, true);
    return;
  }
  lostSince = 0;

  // Line to the RIGHT (error > 0) -> left wheel faster, right wheel slower -> turn right.
  float correction = KP * lineError + KD * (lineError - lastError);
  lastError = lineError;

  int left  = constrain((int)(BASE_SPEED + correction), -MAX_REVERSE, MAX_SPEED);
  int right = constrain((int)(BASE_SPEED - correction), -MAX_REVERSE, MAX_SPEED);
  setMotors(left, right);
  setLed(false, true, false);
}

// ---------------- MODE 1: sensor test (no motors) ----------------
void sensorTest() {
  bool seen = readLine();

  Serial.print("raw[0..7]: ");
  for (int i = 0; i < 8; i++) {
    Serial.print(raw[i]);
    Serial.print(' ');
  }

  Serial.print(" || LEFT> ");
  for (int pos = 0; pos < 8; pos++) {
    Serial.print(norm[pos]);
    Serial.print(' ');
  }
  Serial.print("<RIGHT || ");

  if (blackCount >= ALL_BLACK_SENSORS) {
    Serial.println("ALL BLACK (end bar / lifted)");
  } else if (!seen) {
    Serial.println("NO LINE");
  } else {
    Serial.print("error ");
    Serial.print(lineError);
    if (lineError < -700)      Serial.println("  -> LINE LEFT  (robot would steer LEFT)");
    else if (lineError > 700)  Serial.println("  -> LINE RIGHT (robot would steer RIGHT)");
    else                       Serial.println("  -> CENTER");
  }
  delay(250);
}

// ---------------- MODE 2: motor test (WHEELS OFF THE GROUND) ----------------
void motorTest() {
  Serial.println("LEFT wheel forward (PWM 60)");
  setMotors(60, 0);
  delay(2000);
  setMotors(0, 0);
  delay(1000);

  Serial.println("RIGHT wheel forward (PWM 60)");
  setMotors(0, 60);
  delay(2000);
  setMotors(0, 0);
  delay(1000);

  Serial.println("PWM ramp, both wheels forward. Note the PWM where BOTH wheels turn:");
  for (int pwm = 10; pwm <= 80; pwm += 5) {
    Serial.print("  PWM ");
    Serial.println(pwm);
    setMotors(pwm, pwm);
    delay(700);
  }
  setMotors(0, 0);
  Serial.println("done, repeating in 4 s");
  delay(4000);
}

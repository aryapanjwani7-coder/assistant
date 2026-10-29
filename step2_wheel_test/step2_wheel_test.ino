// STEP 2: Wheel check. HOLD THE ROBOT SO THE WHEELS ARE IN THE AIR.
// Left wheel spins 2 seconds, pause, right wheel spins 2 seconds, pause, repeat.

const int LEFT_SLEEP  = 31;
const int LEFT_DIR    = 29;
const int LEFT_PWM    = 40;
const int RIGHT_SLEEP = 11;
const int RIGHT_DIR   = 30;
const int RIGHT_PWM   = 39;

void setup() {
  pinMode(LEFT_SLEEP, OUTPUT);
  pinMode(LEFT_DIR, OUTPUT);
  pinMode(LEFT_PWM, OUTPUT);
  pinMode(RIGHT_SLEEP, OUTPUT);
  pinMode(RIGHT_DIR, OUTPUT);
  pinMode(RIGHT_PWM, OUTPUT);

  digitalWrite(LEFT_DIR, LOW);     // LOW = forward (we are checking this)
  digitalWrite(RIGHT_DIR, LOW);
  digitalWrite(LEFT_SLEEP, HIGH);  // turn motors on
  digitalWrite(RIGHT_SLEEP, HIGH);

  delay(3000);                     // 3 seconds to pick the robot up
}

void loop() {
  analogWrite(LEFT_PWM, 50);       // LEFT wheel only
  delay(2000);
  analogWrite(LEFT_PWM, 0);
  delay(1500);

  analogWrite(RIGHT_PWM, 50);      // RIGHT wheel only
  delay(2000);
  analogWrite(RIGHT_PWM, 0);
  delay(3000);
}

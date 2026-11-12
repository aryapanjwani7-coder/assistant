// STEP 4b: Hand test. The wheels do NOT move.
// Slide the robot by hand over the track and watch the Serial Monitor (9600).
#include <ECE3.h>

uint16_t sensors[8];

const int sensorMin[8] = { 798, 688, 642, 635, 612, 688, 735, 853 };
const int sensorMax[8] = { 2500, 2500, 2500, 2016, 2213, 2162, 2500, 2500 };

const int LEFT_SLEEP  = 31;
const int RIGHT_SLEEP = 11;

void setup() {
  ECE3_Init();
  pinMode(LEFT_SLEEP, OUTPUT);
  pinMode(RIGHT_SLEEP, OUTPUT);
  digitalWrite(LEFT_SLEEP, LOW);    // motors off
  digitalWrite(RIGHT_SLEEP, LOW);
  Serial.begin(9600);
}

void loop() {
  ECE3_read_IR(sensors);

  int black = 0;       // sensors seeing black
  int veryBlack = 0;   // sensors seeing very black
  int lines = 0;       // separate lines under the sensors
  bool inLine = false;

  Serial.print("LEFT [ ");
  for (int i = 7; i >= 0; i--) {    // print left to right
    long v = ((long)sensors[i] - sensorMin[i]) * 1000L / (sensorMax[i] - sensorMin[i]);
    v = constrain(v, 0L, 1000L);
    if (v < 300) v = 0;
    if (v > 0) black++;
    if (v > 700) veryBlack++;
    if (v > 0 && !inLine) lines++;
    inLine = (v > 0);
    Serial.print(v > 700 ? "# " : v > 0 ? "+ " : ". ");
  }
  Serial.print("] RIGHT   black: ");
  Serial.print(black);
  Serial.print("   very black: ");
  Serial.print(veryBlack);
  Serial.print("   lines: ");
  Serial.println(lines);

  delay(200);
}

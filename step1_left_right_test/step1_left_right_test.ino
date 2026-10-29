// STEP 1: Left / Right check. The wheels do NOT move.
#include <ECE3.h>

uint16_t sensors[8];

const int sensorMin[8] = { 798, 688, 642, 635, 612, 688, 735, 853 };
const int sensorMax[8] = { 2500, 2500, 2500, 2016, 2213, 2162, 2500, 2500 };

void setup() {
  ECE3_Init();
  Serial.begin(9600);
}

void loop() {
  ECE3_read_IR(sensors);

  long weighted = 0;
  long total = 0;

  // Sensor 0 is on the robot's RIGHT, sensor 7 is on its LEFT
  for (int i = 0; i < 8; i++) {
    long v = ((long)sensors[i] - sensorMin[i]) * 1000L / (sensorMax[i] - sensorMin[i]);
    v = constrain(v, 0L, 1000L);
    if (v < 300) v = 0;
    weighted += v * i;
    total += v;
  }

  if (total == 0) {
    Serial.println("NO LINE");
  } else {
    long pos = weighted * 10 / total;   // 0 = far right, 70 = far left
    if (pos < 25)      Serial.println("LINE RIGHT");
    else if (pos > 45) Serial.println("LINE LEFT");
    else               Serial.println("LINE CENTER");
  }

  delay(300);
}

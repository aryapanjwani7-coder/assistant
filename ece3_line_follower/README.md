# ECE 3 RSLK line follower

One sketch (`ece3_line_follower.ino`) with three modes. Set `#define MODE` at the top, upload, and test in this order.

## 0. Before anything
- Use fresh or fully charged batteries and turn the battery switch **ON**, even when USB is plugged in. The motors run only on battery power. Weak batteries make the robot stall or reset.
- Test on the floor or another hard surface, not a tabletop. Avoid direct sunlight, because it contains IR and throws off the sensors.

## 1. MODE 1: sensor test (robot still, USB connected, Serial Monitor at 9600)
Hold the robot at normal height over the track, facing away from you.

| Put the tape under… | The last column must say |
|---|---|
| the middle | `CENTER` |
| the robot's **right** side | `LINE RIGHT` |
| the robot's **left** side | `LINE LEFT` |
| nothing (white paper) | `NO LINE` |

- If left and right are swapped, change `SENSOR0_ON_RIGHT` to `false`. The course `BasicCode` says sensor 0 is on the right, so it should already be correct.
- If white paper shows big numbers and the tape shows small numbers, the calibration doesn't match this surface or lighting.

## 2. MODE 2: motor test (WHEELS OFF THE GROUND)
- The **left** wheel spins first, then the **right** wheel. Each must roll **forward**. If one spins backward, flip its `LEFT_FORWARD` or `RIGHT_FORWARD` value (`LOW` ↔ `HIGH`).
- During the PWM ramp, note the PWM where **both** wheels turn. `BASE_SPEED` (50) must be at least about 15 above that number.

## 3. MODE 0: drive
1. Upload with `MODE 0`, unplug USB, and turn the battery ON.
2. Place the robot on the line and press RESET.
3. The red back LEDs blink for 3 seconds, then the robot drives.

Chassis LEDs:
- **Both front yellow:** following the line.
- **One front yellow:** line lost; the robot pivots toward that side.
- **Back red only:** searched for 2 seconds and didn't find the line, so it stopped. Put it back on the line and it resumes.
- **All four:** every sensor reads black (end crosspiece, or the robot was lifted), so it stopped.

Do the drive mode on a **straight track first** (Straight Line Stability Check printouts), then try curves.

## Gains (from Project Tips and PID Concepts)
- `KP = ERROR_MARGIN * BASE_SPEED / MAX_ERROR` = 1.0 × 50 / 3500 ≈ 0.0143. This follows the rule "Kp × error < base speed": at the largest error, the inner wheel just stops.
- **Check Kp** (KD = 0): place the car offset from the track but parallel to it, and watch the first turn. It should turn toward the line.
- **Then add Kd:** the slides suggest Kd ≈ 5–10 × Kp (try `KD = 0.07`). To check it, set KP = 0, place the car offset from the path and angled toward it, and watch the first turn.

| Symptom | Fix |
|---|---|
| Runs off the outside of curves | Raise `ERROR_MARGIN` (1.0 → 1.3 → 1.6) or lower `BASE_SPEED` |
| Weaves on straights | Add `KD`, or lower `ERROR_MARGIN` |
| A wheel stalls | Raise `BASE_SPEED` |

Make one change at a time. Log every run in the logbook: speed, Kp, Kd, start position, battery voltage, and where it failed, for example "Ran off at turn 2" or "weaving 3/5 (5 = worst)".

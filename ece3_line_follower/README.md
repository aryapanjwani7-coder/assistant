# ECE 3 RSLK line follower

One sketch (`ece3_line_follower.ino`) with three modes. Set `#define MODE` at the top, upload, and test in this order.

## 0. Before anything
- Use fresh or fully charged batteries and turn the battery switch **ON**, even when USB is plugged in. The motors run only on battery power. Weak batteries make the robot stall or reset.

## 1. MODE 1: sensor test (robot still, USB connected, Serial Monitor at 9600)
Hold the robot at normal height over the track, facing away from you.

| Put the tape under… | The last column must say |
|---|---|
| the middle | `CENTER` |
| the robot's **right** side | `LINE RIGHT` |
| the robot's **left** side | `LINE LEFT` |
| nothing (white paper) | `NO LINE`, with every middle number at or near 0 |

- If left and right are swapped, change `SENSOR0_ON_RIGHT` to `false`.
- If white paper shows big numbers and the tape shows small numbers, the calibration doesn't match this surface. Note the `raw` numbers for white and black and update `sensorMin` and `sensorMax`.

## 2. MODE 2: motor test (WHEELS OFF THE GROUND)
- The **left** wheel spins first, then the **right** wheel. Each must roll **forward**. If one spins backward, flip its `LEFT_FORWARD` or `RIGHT_FORWARD` value (`LOW` ↔ `HIGH`).
- During the PWM ramp, note the PWM where **both** wheels turn. `BASE_SPEED` (50) must be at least about 15 above that number.

## 3. MODE 0: drive
1. Upload with `MODE 0`, unplug USB, and turn the battery ON.
2. Place the robot centered on the line and press RESET.
3. The red LED blinks for 3 seconds, then the robot drives.

LED meanings:
- **Green:** following the line.
- **Blue:** line lost; the robot pivots toward where it last saw the line.
- **Red:** searched for 2 seconds and didn't find the line, so it stopped. Put it back on the line and it resumes.
- **Magenta:** all sensors read black (end bar, or the robot was lifted), so it stopped.

## Tuning (only after it drives)
| Symptom | Fix |
|---|---|
| Runs off the outside of curves | Raise `KP` (0.020 → 0.025 → 0.030) or lower `BASE_SPEED` |
| Zig-zags on straights | Lower `KP`, or add a little `KD` (0.02–0.05) |
| A wheel stalls | Raise `BASE_SPEED` |

Make one change at a time. Raise speed only after a full clean lap.

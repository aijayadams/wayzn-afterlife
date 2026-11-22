# Thermal ("IR SENSOR") board

Small daughterboard on a 4-wire lead to the main board's IR SENSOR connector J2
(GND / SCL / SDA / 3V3). Silkscreen: U1, R1–R5, C1–C3, FD1/FD2 fiducials.

| Item | Detail | Tag |
|---|---|---|
| U1 | Panasonic **Grid-EYE AMG88xx** 8×8 thermopile array (metal can with lens), likely AMG8833 | inferred from package; the cloud fields are named `Grideye*` |
| Bus | I2C at **0x69** (AD_SELECT strap high) | verified |
| Frame | 64 pixels, 0.25 °C/LSB, up to 10 fps | datasheet |

It's a thermal array, not a PIR: an off-the-shelf part with a public register map,
supported by ESPHome and Arduino/Adafruit libraries.

## Wayzn's cloud settings (inferred)

- `Mask`: 20 cells in a 4 col × 5 row detection area, so the 8×8 pixels are binned or
  cropped to 4×5 (likely 2×2 bins on part of the frame).
- `GrideyeDetectionThreshold` (0–3.5, default ~1.085): °C above background for a cell to
  count as "warm".
- `GrideyeCountThreshold` (1/2/4 = small/medium/large dog): minimum number of warm cells to
  trigger.

## The bus

J2 SCL/DATA = test points TP1/TP2 on the main board, ~100 kHz, 4.7 kΩ pull-ups on the main
board. No STM32 pin is on it; with Wayzn's firmware the imp is the master. Sniff it with
`tools/i2c-sniffer`.

| Addr | Device | Traffic from the imp | Conf. |
|---|---|---|---|
| 0x69 | Grid-EYE | `W69 80 \| R69 <128 B>` every ~100 ms (some frames read twice). **No configuration writes**: the sensor runs at power-on defaults (normal mode, 10 fps, no moving average). Reading starts 0.6 s after power-up | verified |
| 0x0F | Accelerometer U50 on the main board, Kionix KXTJ3 (WHO_AM_I 0x35) | Boot: `W0F 1B D8` (CNTL1: operate, high-res). Then `W0F 06 \| R0F <6 B>` (X/Y/Z) at ~2.3 Hz | verified |
| 0x3C | Unknown (LED driver?) | One write at boot; possibly a one-bit decode shift of another address | inferred |

Pixels: 12-bit two's complement, little-endian, 0.25 °C/LSB, registers 0x80..0xFF read as
one 128-byte block. A hand at ~10–30 cm gives +8 to +12 °C on 10–64 pixels over a 14–20 °C
bench background. Doorway measurements (noise, dog and person signatures, door-open
cooling) are in [DETECTION-RESEARCH.md](../DETECTION-RESEARCH.md).

## Drive lever detection (accelerometer)

The drive's engaged/disengaged lever tilts the whole housing ~90°. The imp tells the lever
position from the accelerometer on the main board (tilting the IR board alone changes
nothing). Tilting the main board on the bench, the imp sends:

| Accel (g) x / y / z | Pose | Imp sends |
|---|---|---|
| -0.09 / +0.05 / +1.00 | flat | `engage` |
| +0.76 / +0.21 / +0.67 | ~50° about y, moving | `disengage` (at once) |
| +0.93 / +0.11 / +0.18 | ~80° about y, ~1 s still | `engage` |
| +0.58 / -0.66 / +0.39 | rolling about x | `disengage` |
| ~0 / -0.9 / +0.4 | ~65° about x, held ~10 s | stays disengaged |
| +0.92 / +0.01 / +0.30 | ~72° about y, ~1.5 s still | `engage` |
| +0.63 / -0.04 / +0.71 | ~45° about y, moving | `disengage` (at once) |
| -0.15 / +0.04 / +1.00 | flat, ~1 s still | `engage` |

Model that fits every event (**inferred**): **`disengage` while the board is moving or in
the lever-up pose; `engage` once it has been still for ~1–2 s in any other pose.** The
lever-up pose is probably the roll about x (y ≈ -0.9). Each `engage` is followed by
`set_obstruction`, then `clear_obstruction` a few seconds later. Installed in the door and
engaged, the board reads x = +0.95, y ≈ 0, z ≈ 0 g.

Rule for a replacement (proposed): at install, store the gravity vector with the lever down
as the reference; count as disengaged when the angle to it exceeds ~45°, debounced ~1 s.
This works whatever the mounting orientation. (The ESP32 brain does not use the
accelerometer.)

## ESP32 as master

With the imp held in reset the ESP32 masters this bus (GPIO21 = SCL/TP1, GPIO22 = SDA/TP2,
100 kHz, the board's pull-ups). Both devices answer: Grid-EYE 0x69 (power-on defaults,
normal mode) and the KXTJ3 at 0x0F. The KXTJ3 starts in standby: write CTRL_REG1 (0x1B) =
0xC0 for 12-bit, ±2 g; data at 0x06–0x0B, little-endian, left-justified (>> 4, 1024
counts/g).

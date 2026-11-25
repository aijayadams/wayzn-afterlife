# Phase B: the control board (replacing the WYP-BC-100)

**Decision (2026-11-24):** replace the whole Wayzn control board with our own. The ESP32 drives the
motor directly, using standard techniques for a brushed DC gear motor with a quadrature encoder. The decompiled
STM32 firmware ([STM-FIRMWARE.md](STM-FIRMWARE.md)) is used as a **reference** for behaviour, scales and
thresholds, not cloned. Carrier v3 (`hardware/carrier/`) is parked as the fallback (it needs its right end
pulled in clear of the posts before it could be ordered: see WYP-BC-100-MECHANICAL.md).

**Board: [hardware/control-board/](../hardware/control-board/README.md)**, v1 laid out 2026-11-24 (ERC/DRC clean, not
ordered): C5 + H2, DRV8876 (current cap 1.10 A: VREF = 3V3, R_IPROPI = 3.0 k), TPS54202 buck, the Wayzn outline,
holes and connector positions from the scans.

Why: Phase A (an ESP32 dev board as the brain) works, and a full board is cleaner than a carrier piggybacked on someone
else's design. The original board stays untouched, so reverting is just swapping connectors back.

Confidence tags: **verified** (measured or read), **marking** (read off the part), **inferred** (educated
guess), as in the rest of `docs/`.

## What the board has to do

Sources: [WYP-BC-100.md](hardware/WYP-BC-100.md) (silkscreen and markings), [STM32-PINMAP.md](hardware/STM32-PINMAP.md)
(continuity), [STM-FIRMWARE.md](STM-FIRMWARE.md) "Motor driver" (scales and pin roles read from the code), and
the captures.

| Function | Wayzn board today | Ours | Conf. |
|---|---|---|---|
| 12 V input | **J7: GND upper pin, 12V lower pin** (silkscreen); SK84 Schottky (D250); buck U30/L30 | Same connector and pin order. Reverse-polarity protection + TVS, synchronous buck to 3V3 (≥ 1 A for C5 + H2 Wi-Fi peaks) | marking |
| Connector family (J6, J7) | 2-pin side entry, 3.93–3.96 mm pitch (scan) | JST VH 3.96 mm side entry (B2PS-VH) | verified |
| 12 V sense | R101/R102 divide by 7 → PA0 (`vsense` = ADC mV × 7) | Divider → C5 ADC | read from code |
| Motor | **J6: OUT_A upper pin, OUT_B lower pin**; DRV8828 | Same connector and pin order; a modern bridge (below) | marking |
| Motor current | R2 → U8 → PA1, **1.1 V/A**; `H`, `c` and the profile values are **mA** | The bridge's current mirror (IPROPI) → C5 ADC | read from code |
| Current the motor actually draws | Normal close ~0.40 A; firmware obstruction threshold ~0.67 A; highest seen 0.75 A | Same thresholds as starting points | captures |
| **Hardware current cap** | DRV8828 chopping at **1030 mA** (DAC 34 % of 3022 mA), set once at boot, never changed per move. The motor's own 12 V stall current never matters: the driver holds it to ~1 A | Bridge current regulation set to the same ~1.0–1.1 A | read from code |
| Decay | Fast (set at boot) | The bridge's default (DRV887x: brake/slow on PWM off) | read from code |
| Encoder | J5: GND, SIG B, SIG A, 3V3 → TIM3 encoder mode. Powered at 3V3. ~767 counts/inch, full travel ~13,588 counts | C5 PCNT quadrature; same connector; 10 k pull-ups and 100 R / 1 nF filters fitted | marking / verified |
| IR presence | Grid-EYE on J2, I2C, 4.7k pull-ups on the board | Same connector and pin order; pull-ups on our board | verified |
| Accelerometer | KXTJ3 (U50), dead | LIS2DH12 | verified |
| Button | Through-hole tactile switch beside J6 (photo), read by the STM | GPIO + RC debounce, same position (the shell's button cap sits on it) | marking |
| Status LED | D1004 under the clear light pipe on the front edge (photo) | One LED in the same spot, under the same light pipe | marking |
| J105 | 2x4 socket; pin 8 = 3V3, pin 4 = GND; empty in our door except the dev-board power wires | **Not needed**: nothing of Wayzn's plugs into it | verified |
| Radios | imp004m | ESP32-C5 + ESP32-H2, as on the carrier | |

Dropped: the imp module, the STM32, J105.

## Architecture

- **One MCU runs the door: the ESP32-C5.** MCPWM drives the bridge, PCNT decodes the encoder in hardware, the
  ADC reads motor current and 12 V. The C5 has both MCPWM and PCNT
  ([datasheet](https://documentation.espressif.com/esp32-c5_datasheet_en.html),
  [ESP-IDF MCPWM](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/api-reference/peripherals/mcpwm.html)).
  The 1 kHz control loop runs in a high-priority task; Wi-Fi can delay the loop, but not the counting or the
  hardware current limit.
- **H-bridge: TI DRV8876** (3.5 A peak; current mirror output IPROPI, hardware current regulation set by VREF,
  nFAULT, nSLEEP). Wayzn caps the motor at ~1 A in hardware, so 3.5 A is ample margin and the DRV8874 (6 A) isn't
  needed. The hardware current cap is what bounds the force if the firmware hangs.
- **ESP32-H2** for Thread, reflashed by the C5, exactly as on the carrier.
- Same programming scheme as the carrier: UART header, RESET/BOOT buttons, bench-3V3 jumper.

## Control platform: can the C5 run the loop? (checked 2026-11-24)

**Yes, with margin.** The time-critical parts are done by dedicated hardware, so the CPU only runs a slow outer
loop (Wayzn's STM32, a 48 MHz Cortex-M0, ran its whole controller at 10 ms).

ESP32-C5 resources, from ESP-IDF's `soc_caps.h` (release/v5.5,
[raw header](https://raw.githubusercontent.com/espressif/esp-idf/release/v5.5/components/soc/esp32c5/include/soc/soc_caps.h))
and `adc_channel.h` (**read from vendor source**):

| Need | C5 hardware | How we use it |
|---|---|---|
| PWM to the bridge | MCPWM: 1 group, 3 timers / 3 operators, 2 generators each; **3 GPIO fault inputs**; ETM | Operator 0 drives DRV_EN (GPIO10) at ~20 kHz; PH (GPIO9) is a plain GPIO set before each move. **DRV_NFAULT (GPIO7) is also an MCPWM fault input**: a driver fault forces the PWM low in hardware (one-shot brake), with no CPU involved. IMODE is tied low (fixed off-time chopping), so nFAULT means a real fault (overcurrent, thermal, undervoltage), not routine current chopping |
| Encoder counting | PCNT: **4 units**, 2 channels each (full quadrature, ×4), glitch filter, 2 threshold points, runtime threshold update, step notify | ENC_A/ENC_B (GPIO4/5) into one unit in quadrature. Hardware counting, so Wi-Fi interrupts can't lose counts (the encoder edges run at ~2.3 k counts/s) |
| Low-speed velocity | MCPWM capture: 1 timer, **3 channels** | The same two encoder pins also go to capture channels (the GPIO matrix fans one input out to several peripherals): edge-to-edge time gives clean velocity at the slow ends of travel and near a stall, where count deltas are coarse |
| Motor current | ADC: 1 unit, 6 channels on **GPIO1–6 only**, **DMA continuous mode** up to 83 kHz, **IIR filter**, **threshold monitor** | IPROPI (GPIO6, CH5) and 12 V (GPIO1, CH0) sampled continuously by DMA (no CPU per sample). The monitor raises an interrupt the moment current crosses a set level, ahead of the 1.10 A hardware cap. The C5 has only one ADC, so there's no ADC2/Wi-Fi conflict |
| Control timing | 1 HP RISC-V core at 240 MHz (shared with Wi-Fi), GPTimer, LP core | Velocity/position loop at 1 kHz from a GPTimer interrupt (IRAM, high priority). Wi-Fi interrupts add tens of microseconds of jitter at most, which a 1 ms loop absorbs |
| Status LED | RMT: 4 channels (2 TX) | Addressable RGB LED on GPIO25 (see the LED section in hardware/control-board/README.md) |

**Firmware rules this depends on** (Wi-Fi shares the core, so these keep the loop on time):
1. Run the control loop from a GPTimer interrupt placed in IRAM, with its data in RAM (IRAM-safe), so it keeps
   running while the flash cache is off.
2. Never write flash (NVS settings, OTA) while the motor is moving: queue the write until the move ends.
3. Feed a watchdog from the control loop. A stalled loop drops nSLEEP, and the move timeout still applies. If the
   loop stalls anyway, the motor keeps its last duty, still capped at 1.10 A by the driver.

**Pins.** Everything above except the ADC goes through the GPIO matrix, so no peripheral needs a particular pin.
The hard constraints are:
- The ADC inputs must be on GPIO1–6: IPROPI is on GPIO6 and the 12 V sense on GPIO1.
- Download mode needs UART0 on GPIO11/12.
- The bridge outputs must stay off strapping pins and pins that glitch at boot. EN, PH and nSLEEP are on GPIO10, 9 and 8, each with a 10 k pull-down.

**Not used, on purpose:**
- **An LP-core supervisor.** It would need nSLEEP on an LP GPIO, which would break the uncrossed lane. A hung HP
  core is already covered three ways:
  - the hardware current cap
  - the MCPWM fault input
  - the task and RTC watchdogs, whose reset floats every pin so the pull-downs turn the motor off
- **ETM.** The fault-to-PWM path doesn't need it. ETM on the C5 has an unconfirmed bug report against Espressif's
  emulator ([esp-emulator#58](https://github.com/espressif/esp-emulator/issues/58)), which doesn't concern silicon,
  but nothing here depends on ETM anyway.

## Safety (the governing principles applied to motion)

1. **Motor off by default in hardware.** nSLEEP and both bridge inputs pulled low, so a reset, boot, brownout or
   flash leaves the motor unpowered. Bridge pins must not be C5 strapping pins or pins that toggle at boot.
2. **Hardware force cap.** The bridge's own current regulation (VREF = 3V3, R_IPROPI = 3.0 k: 1.10 A) sits just
   above the highest current a legitimate move needs: Wayzn uses 1030 mA, against 0.40 A for a normal close and
   0.75 A at most seen. It can't be exceeded whatever the firmware does. A driver fault (overcurrent, thermal,
   undervoltage) also cuts the PWM in hardware through the MCPWM fault input.
3. **Firmware obstruction detection** (the standard "force learning" used by garage-door openers, and what
   Wayzn does): a learned current-vs-position profile per direction. Too much current over the profile + margin
   for N ms, or the velocity collapsing, means an obstruction: stop, then **reverse toward open**. Wayzn's
   numbers are starting points: 35 ticks × 10 ms over the limit, stall = |v| < 100 after 1 s, target window 45
   counts (10 when closing with a profile), 96 counts per profile bucket.
4. **Move timeout** and watchdogs: a move that runs too long drops nSLEEP; a control loop that stops running
   trips the task watchdog, and the reset floats every pin so the pull-downs turn the motor off.
5. **Honest state.** After any reset, the position is `unknown` until homed. A wrong "open" is worse than an
   honest "unknown". Homing never assumes it.
6. **Never lock a dog out.** On any fault the bias is open. Assumed state never suppresses an open command.
   Oscillation detection stays (it already exists in the brain firmware).

## Control approach (prior art, nothing novel)

- **Cascade:** a trapezoidal (or S-curve) position profile → a velocity PI loop at 1 kHz on the encoder → PWM
  duty. The current limit is a clamp on top (firmware), with the bridge's regulation as the backstop
  (hardware). No inner current loop: a door doesn't need torque control.
- **Homing:** drive slowly to the closed stop with a low current limit, detect the stall (velocity ~0 with
  current up), zero there; then learn the travel to open, and the current profile, on a calibration run. This
  is Wayzn's `auto_zero` and profile learning, re-implemented.
- **Learned profile:** current per position bucket and direction, stored in NVS (the brain already stores
  Wayzn's profile in NVS), refreshed slowly from good moves so it follows seasonal friction.
- From the decomp, take: counts per travel, direction sign, the shape and margins of the profile, the stall and
  overcurrent timing, and the button behaviour. Not its code.

## Mechanics

Board geometry from the owner's 1200 dpi scans: [WYP-BC-100-MECHANICAL.md](hardware/WYP-BC-100-MECHANICAL.md).
33.94 × 152.6 mm, four Ø4 holes on 27.95 × 146.3 mm. J6/J7 are JST VH (3.96 mm pitch measured), side entry at
the J6/J7 end; J2/J5 are JST PH SMD top entry at the other end. The button cap and the LED light pipe stick out of
the long edge on the button side.

## Bring-up plan

1. Board powered from a current-limited bench supply, motor unplugged: rails, C5/H2 flash, encoder counts by
   turning the drive by hand.
2. Motor connected, **belt/drive disengaged**, current limit low: open-loop PWM, then the velocity loop, then
   the hardware current cap tested by stalling the motor by hand.
3. Motor + panel on the bench: homing, calibration, obstruction tests (a soft object in the path).
4. In the door, only after 3 passes. The original board goes back if anything is off.

## Open questions

- Button press lengths and actions (decomp: events 2, 3, 6; event 6 wipes the calibration). Ours is new
  firmware, so this is a choice, not a measurement.

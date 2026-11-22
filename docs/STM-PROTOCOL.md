# imp ↔ STM32 UART protocol (STM+IMP header)

The protocol between the imp004m and the STM32 as it appears on the wire. See
[STM-FIRMWARE.md](STM-FIRMWARE.md) for the full command table, field meanings and state
list. Logs recorded with `tools/uart-sniffer` decode with
`python3 tools/uart-sniffer/decode.py <log>`.

## Physical layer (verified)

- **115200 8N1, 3.3 V, plain ASCII, `\n`-terminated** (STM lines end `\r\n`).
- Header pin names are from the **STM32's point of view**:
  - **RX** (sniffer channel A) = imp → STM: commands.
  - **TX** (sniffer channel B) = STM → imp: status.
  - Pressing EXP RST (STM reset) produces an `@@` boot line on TX at once; the STM then
    stays unconfigured until the host sends the configuration.
- The jumper wires run next to the motor: motor noise can drop bytes mid-line. Twist each
  signal with a ground wire, keep them short, or add a 100 Ω series resistor at the ESP32.

## imp → STM commands

| Command | Example | Meaning |
|---|---|---|
| `set_profile open <i> v×8` | `set_profile open 0 163 809 857 …` | Learned per-position motor-current profile for opening: 8 values per line, index steps by 8, 144 entries (~141 used, then zero padding). The STM's current limit follows it |
| `set_profile close <i> v×8` | `set_profile close 0 470 951 …` | Same for closing |
| `set_open_to <counts>` | `set_open_to -9204` | Open distance, = -(inches × 767) (verified: 12 in → -9204, 8 in → -6136). The next `open` targets it |
| `set_open_wait <s>` | `set_open_wait 15` | Hold-open time; -1 = remain open. **The STM runs this timer, for `open_and_close` only** (verified): after reaching open (state 7) it closes on its own after `<s>` seconds. A plain `open` goes to state F and stays open regardless |
| `set_button_op <0\|1>` | `set_button_op 1` | Physical button enable (verified). The STM handles the button itself |
| `profiled_state <n>` | `profiled_state 1` | The profile above is valid / in use |
| `set_dooroffset <counts>` | `set_dooroffset 13572` | Full travel (the cloud's `DoorOffset`) |
| `zero <pos> <state>` | `zero -69 14`, `zero -6194 15` | Restores the last known position and motion state after an STM boot (verified). Sets zerod = 1 and the state; for a resting state sets position = `<pos>`, for a moving state (12, 13, 26–28) stores a pending restore. `<pos>` = last `p` the STM reported; `<state>` = last `U` in **decimal** (14 = 0x0E closed, 15 = 0x0F open, 26 = 0x1A disengaged). The STM has no absolute position sensor, so it trusts this |
| `engage` | `engage` | Sent at boot after the configuration, from the app's engage action, and when the imp sees the drive lever engaged (accelerometer, see [hardware/ir-sensor-board.md](hardware/ir-sensor-board.md)). **The STM then drives the door closed on its own** (verified), with no `close` from the host: this is why the door moves at power-up. From disengaged: `U`=1C, target -50 |
| `disengage` | `disengage` | STM drives to **fully open** (target -13538) at low current (~0xF7–0x13B), then rests (`U`=1D) (verified). If not zerod, it drives to the end stop (-100000) instead |
| `open` | `open` | **Open to `set_open_to`**: target → -6136, `U` C→F, ~5 s travel (verified) |
| `close` | `close` | **Close**: target → -50, `U` D→E, ~7 s (verified). `close` during an `open` reverses mid-travel. With `set_obstruction` active, the STM drives open first and closes after `clear_obstruction` |
| `open_and_close` | `open_and_close` | Sent by the imp on a thermal detection: open to `set_open_to` (state C → 7), then the **STM** closes after `set_open_wait` seconds (verified) |
| `reset_zero` + `auto_zero` | `reset_zero` then `auto_zero` | Recalibrate: `reset_zero`, then the full sweep, followed by `get_profile` |
| `get_profile` | `get_profile` | Reads back the learned profile (reply: a `<hex>P` run) |
| `set_obstruction` / `clear_obstruction` | | Thermal "don't close" interlock, sent whatever the auto-open setting. Mid-close, `set_obstruction` makes the STM reverse (below) |
| `AutoOpen`, `AutoOpenPetTag`, `token`, `updateMask`, `GrideyeDetectionThreshold`, `GrideyeCountThreshold`, `poke_dog`, `ObstructionDetection` | bare names | The imp forwards the *name* of each cloud command it receives, with **no value**. The STM ignores them (unknown commands are dropped silently). Auto-open, detection and obstruction policy all live on the imp side (verified) |

At imp boot the STM prints a fresh `@@` boot line ~200 ms before the imp's configuration
burst: the imp resets the STM and then pushes the configuration. After an imp reset (SW1)
the imp re-sends the profiles, `set_open_to`/`set_open_wait`/`set_button_op`/`profiled_state`
but not always `set_dooroffset`/`zero` (the STM then reports K=0, p=0).

No thermal-sensor data crosses this UART (no pixels, mask or thresholds), yet the imp sends
motion decisions (`open_and_close`, `set/clear_obstruction`): the imp reads the Grid-EYE
over I2C itself and runs detection (see [hardware/ir-sensor-board.md](hardware/ir-sensor-board.md)).
A replacement brain must run detection itself.

## STM → imp status line

Every ~1.5 s, **or every 10 s once nothing has been received for 25 s** (verified). Format: a
run of **`<hex value><one-char key>`** fields (the key is any non-hex char), e.g.

```
4U0Z0%1CH0;1E0v0=0p0K                     unconfigured
24U3Z0%1CH0;1E0v0=FFFFCAFCp3504K          after set_dooroffset/zero
```

| Key | Values | Meaning |
|---|---|---|
| `U` | **C = opening, D = closing, E = closed (idle), F = open (stays open), 7 = open with hold timer running**; 4 unconfigured; 0x12 = reversing on current (effort) obstruction, 0x26 = reversing on thermal obstruction; 1A moving open, 1D resting, 1C engaging (disengage/engage) | **Motion state** (C/D/E/F and 1A/1D verified against door movement). Full list in STM-FIRMWARE.md |
| `Z` | 0, 2, 3 | **zerod + 2 × profiled**: 2 = profile loaded but not zerod, 3 = both (verified) |
| `%` | FFFFCB1E = -13538, FFFFFFCE = -50, FFFFE808 = -6136 | **Target position** (verified) |
| `H` | ~1B–1D idle, 0x15D–0x2EB moving | **Motor current**, the same ADC value as the `c` command. Idle ≈ 28; moving 350–750, same scale as the `set_profile` tables |
| `;` | -1, 0 | Field 0x9AC, set to -1 by `auto_zero` and `open`; probably a profile/retry index |
| `v` | 1E0 = **480** | **STM firmware version** (the cloud's `STM_V`) |
| `=` | 0 | A byte flag (0x9C4), not named |
| `p` | | **Position**, signed 32-bit counts. **0 ≈ closed, more negative = more open; -13588 = fully open** (verified) |
| `K` | e.g. 3504, 3514 | **Door offset** (full travel, hex): 0x3514 = 13588 |

Hex values are two's-complement 32-bit when they start `FFFF`.

Resting positions (door in `door-config/`): closed ≈ -72 (target -50), open-to ≈ -6185
(target -6136), fully open ≈ -13560 (target -13538).

### Button (verified)

A button press produces **no imp command**: the STM acts on its own (e.g. state F→D,
target → -50) and prints a status line starting with `*`, the button event marker. The STM
owns the button (gated by `set_button_op`); the imp only hears about it.

### Obstruction handling (verified)

Two independent mechanisms, both acting while closing:

1. **Thermal (host side):** the host sends `set_obstruction` mid-close. The STM reverses
   **at once** (transient state `0x26`) and reopens to `set_open_to` (target -6136). On
   `clear_obstruction` it resumes closing **by itself**, with no `close` from the host.
2. **Current (STM side):** resisting the panel needs no host command. The STM detects
   excess current against its learned profile, goes to state `0x12`, sets target
   **-100000** (`FFFE7960`: drive open to the end stop), reaches full open, then **retries the
   close on its own**. The move summary shows `` ` ``=2→0x12 and `#`=0x640.

So the host only has to supply the thermal interlock. Current-based obstruction, reversal
and retry are built into the STM firmware.

### Move-summary line

At each state change the STM prints fields keyed `{ ^ ( ) \` #`, e.g.
`8C{FFFFCB47^205(430)3\`98#`: `{` profile bucket, `^` position at the event, `(` expected
current from the profile, `)` current limit in use, `` ` `` the **new state** (same codes as
`U`), `#` latest motor current sample. The boot line (`@@` prefix) carries the same fields,
e.g. `8D{FFFFCAFC^1C(181)2\`1C#`.

### Profile dump

Reply to `get_profile`: 144 `<hex>P` values (open table) then 144 `<hex>g` values (close
table), one every 25 ms. The imp stores it and replays it with `set_profile open|close` at
every STM boot.

## Driving the STM from a replacement brain

1. **Boot:** reset the STM (NRST, or the `reset` shell command) and wait for its `@@` line.
   Send a bare `\n` first so a glitch byte from attaching TX doesn't eat the first command.
2. **Configure** (one line each, `\n`-terminated, paced ~0.4 s apart):
   `set_profile open <i> …` ×18 and `set_profile close <i> …` ×18, `set_open_to <-counts>`,
   `set_open_wait <s>`, `set_button_op 0|1`, `profiled_state 1`. Example values:
   [door-config/](../door-config/).
3. **Restore position:** `set_dooroffset <K>`, then `zero <last p> <last U as decimal>`. If the
   position can't be proven (e.g. the door may have been moved by hand while unpowered),
   treat it as **unknown** and recalibrate with `reset_zero` + `auto_zero` (AGENTS.md 1).
4. Send the current obstruction state (`set_obstruction` if anything is in the doorway)
   **before** `engage`, because `engage` makes the STM close the door by itself. Then wait
   for `Z=3` and state E.
5. **Operate:** `open`, `close`, `open_and_close`, and `set_obstruction` /
   `clear_obstruction` from the brain's thermal detection. Keep the obstruction interlock
   **always on**, regardless of auto-open (AGENTS.md 4).
6. **Monitor:** parse the status line (`U` state, `p` position, `%` target, `H` current,
   `K` travel). Report "unknown" if status goes stale (AGENTS.md 1).
7. **Maintenance:** `reset_zero`, `auto_zero` → wait for idle → `get_profile` → store the
   table → push it with `set_profile` at every STM boot.
8. Optional: `disengage` / `engage` for manual mode.

The STM handles on its own: the button, motor control, current limiting and current-based
obstruction against the learned profile. The profile tables and offsets are per-door
calibration: store them.

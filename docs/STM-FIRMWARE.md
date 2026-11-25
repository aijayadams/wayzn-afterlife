# STM32 (U300) firmware: what the code does

Firmware version 480 (`1E0v`), read from the STM32F051's flash over SWD
(`tools/swd-dump/`) and decompiled with Ghidra 12 headless (`tools/ghidra/`). The
dump and the decompile are Wayzn's code and are not published here. The addresses
below are in that image. **Verified live** = checked against a real board over the
STM+IMP header; everything else is read from the code.

Companion to [STM-PROTOCOL.md](STM-PROTOCOL.md), which covers the protocol on the wire.

## Layout

- Image 1, 0x08000000–0x08007F2C: **the running firmware** (vector table at 0; reset
  0x08006E2D; `main` loop at 0x0800138C). Source files named in asserts: `Src/main.c`
  `iwd.c` `quad.c` `usart.c` `pwm.c` `motor.c` `control.c` `adc.c` `stm32f0xx_it.c`.
- Image 2, 0x08008400–0x0800CBE8: an older or debug build of the same code (unoptimised,
  frame-pointer code, no vector table, extra UART error strings `parity` `noise`
  `frame` `overrun`). Nothing in image 1 calls it. The F0 has no VTOR, so it can't run
  from there: it is unused.

## UARTs (verified live)

| UART | Pins | 115200 8N1 | Used for |
|---|---|---|---|
| USART1 | PA9/PA10 = **the STM+IMP header** | yes | imp link: shell input, status line, `@@` boot line, replies |
| USART2 | PA2/PA3 (JP300 pads 3/5) | yes | debug console: `Reboot reason`, `Start`, `getstate`, `profile`, PID logging, "STM Sending + for BT" |

The shell reads **both** UARTs. Output of some commands goes only to USART2 (marked *debug* below).

## Main loop (0x0800138C)

- Polls both UARTs, the encoder, the button and the shell continuously.
- **Control step every 10 ms** (0x08003B9C).
- **Status line every 1.5 s if the shell received a line in the last 25 s, otherwise
  every 10 s.** Verified live: the STM drops to 10 s about 25 s after boot when nothing talks
  to it, and returns to 1.5 s as soon as a line arrives. A host that sends anything at
  least every 25 s keeps the 1.5 s rate.
- At boot: DRV8828 current limit 1030 mA, fast decay, state 0 → 4 (see "Motor driver" below).

## Motor driver (read from the code, 2026-11-24)

The motor init (0x080028E8) and the `limit`, `c`, `vsense`, `decay`, `go`, `reset_motor`, `sleep_motor` handlers
(command table at 0x08007330) give the DRV8828 hookup and every scale factor:

| STM pin | DRV8828 | How we know |
|---|---|---|
| PA12 | PHASE (direction) | written by the direction setter 0x08002728 (with a per-door invert flag) |
| PA5 | nRESET | `reset_motor` / `mreset` write it; low then high at init |
| PB0 | nSLEEP | `sleep_motor`; high (awake) at init |
| PB1 | nFAULT (input, active low) | read by 0x08002A48, "fault = pin low" |
| PA4 | DECAY | 0 = slow, 1 = fast, input (hi-Z) = mixed: the DRV8828's tri-level pin. Init: fast |
| PB3–PB7 | I0–I4 (5-bit current DAC) | set by `limit` (0x080027FC) |
| a timer channel (not identified) | ENBL (PWM) | `go` / the controller write a timer compare register, duty 0–100 |

- **Current limit (hardware, DRV8828 chopping):** `limit <mA>`, capped at **3022 mA**. The firmware maps mA to the
  DAC code through the DRV8828's 32-step table (0, 5, 10, … 98, 99, 100, 100 %) scaled so 100 % = 3022 mA. The
  only call besides the shell is the init: **1030 mA** (DAC 34 %, code 7 ≈ 1027 mA). Nothing changes it per
  move: the profile-based limit is a firmware threshold on the measured current, not the chopping limit.
- **Motor current (`c`, and `H` in move summaries):** ADC counts × 3300 / 4096 = mV, then mV × 1000 / 1100 =
  **mA**. So the sense chain (R2 → U8 → PA1) is **1.1 V per amp**. `c` replies (~451 while pushing) match the
  `H` values in move summaries (0x1B8 = 440) in the same capture, so `H` and the profile values are mA: a normal
  close ~400 mA, the firmware limit ~670 mA, the largest seen 747 mA.
- **Supply voltage (`vsense`):** ADC mV × 7 = mV of the 12 V rail (R101/R102 divide by 7).
- **Speed (`go`):** −100..100 % duty; the sign sets PHASE.


## Shell (0x080024C0)

- Line ends on `\n` or `\r`. Backspace (0x08/0x7F) edits. 200-byte buffer, **max 11 tokens**
  (the imp's `set_profile <dir> <i>` + 8 values is exactly 11).
- Exact, case-sensitive match against a 45-entry table at 0x08007330 (`{name, handler, help}`).
  **Unknown commands are silently ignored** (no error).
- `echo` (toggle, or `echo 0|1`) echoes input back. Off by default.
- A garbage byte before a command makes it unknown, so it's dropped silently. Attaching a
  host's TX line can produce one glitch byte (`F8`, verified), which costs the first command.
  **Send a bare `\n` first after connecting.**

### Command table

"Moves" = can start the motor. All motion commands just set a "shell command" code at
controller offset 0x9F0. The 10 ms control step acts on it, then clears it.

| Command | Args | Code | Moves? | What it does (code) | Reply on USART1 |
|---|---|---|---|---|---|
| `version` | | | no | | `1E0v` with **no line ending** (it runs into the next line) (verified) |
| `reset` | | | no | MCU software reset | `@@` boot line (verified) |
| `echo` | `[0\|1]` | | no | toggle echo | |
| `help` | | | no | prints the table | *debug* |
| `quad` | | | no | position (`p`) | decimal, e.g. `-72` (verified) |
| `mreset` | `[0\|1]` | | **direct** | no arg: toggles PA5; with arg: writes PA5 (motor-driver pin) | |
| `go` | `<speed>` | | **YES, direct** | raw motor speed | |
| `c` | | | no | motor current | decimal, `28` idle (verified) |
| `limit` | `<n>` | | no | stored and passed to the motor-driver setup (`FUN_080027fc`, clamped to 3022; 1030 at boot). **No measurable effect on motor current:** with `limit 400`, open peaks stayed at ~705-775 and close peaks at ~440-625, the same as at 1030 (alternating test, 3 cycles each) | |
| `zero` | `<pos> <state>` | | **can** | sets **zerod = 1**, state = `<state>`. Resting state: sets position to `<pos>`. If `<state>` is 12/13 (opening/closing) or 26–28 (disengage/engage moves), stores a *pending* restore instead | |
| `decay` | `[slow\|fast\|mixed]` | | indirect | DRV8828 decay mode | |
| `p` `v` `a` | `[n]` | | **YES** | target position / velocity / acceleration | value |
| `pgain` `igain` `dgain` `pmax` `imax` `dmax` | `[x]` | | indirect | PID tuning; no arg = print | value |
| `vsense` | | | no | supply voltage, mV | `11620` = 11.6 V (verified) |
| `stop` | | 7 | stops | stop the motor | |
| `open` | | 1 | **YES** | open to `set_open_to` (stay open). Needs zerod; ignored if disengaged-resting or already opening | |
| `close` | | 2 | **YES** | close to -50. Needs zerod | |
| `profiled_state` | `0\|1` | | no | profile valid flag | |
| `reset_zero` | | 10 | no | zerod = 0 (verified: `Z` 3→2) | |
| `reset_motor` / `sleep_motor` | `0\|1` | | **direct** | DRV8828 reset / sleep pins | |
| `open_and_close` | | 3 | **YES** | open to `set_open_to`, then close after `set_open_wait`. Needs zerod | |
| `auto_zero` | | 4 | **YES, always** | clears the profile tables, zerod = 0, drives to -100000 (end stop), state 0x10 → calibration sweep | |
| `profile` | | | no | print profile | *debug* |
| `set_profile` | `open\|close <i> v…` | | no | stores values from index `<i>` (verified round-trip) | |
| `get_profile` | | | no | 144 open values then 144 close values, hex, one every 25 ms | `<hex>P`×144 then `<hex>g`×144 (verified) |
| `safetoclose` | `0\|1` | | no | sets the safe-to-close flag (0x9B0) | |
| `engage` | | 6 | **YES** | needs zerod, refused if a pending restore (`engage refused, position unknown`, *debug*). If p > -51: state 0x1E, no move. Else state 0x1C, drive to -50 | |
| `disengage` | | 5 | **YES, always** | if zerod and no pending restore: already open (p ≤ 50 − K) → 0x1D, no move; else drive fully open, state 0x1A. **If not zerod: drive to -100000 (end stop), state 0x1B** | |
| `getstate` | | | no | timer, state, cmd, zerod, position, retries, destination, velocities… | *debug* |
| `start_log` | | | no | control logging | USART1 lines |
| `clear_obstruction` / `set_obstruction` | | 9 / 8 | indirectly | safe-to-close flag = 1 / 0. Mid-close, `set_obstruction` makes the STM reverse | |
| `set_dooroffset` | `<K>` | | no | full travel. Also re-bases position: `p` = raw count − K (verified: `p` → −13588) | |
| `get_fake_data` | | | no | 1000 lines of test output | spam |
| `set_open_to` | `<counts>` | | no | open target | |
| `set_button_op` | `0\|1` | | no | **physical button** enable (see below) | |
| `set_open_wait` | `<s>` | | no | hold-open seconds (stored ×100) | |

## Status line fields (0x08003ACC + 0x08001860)

`<U>U<Z>Z<%>%<H>H<;>;1E0v<=>=<p>p<K>K\r\n`, all hex:

| Key | Field | Notes |
|---|---|---|
| `U` | control state (0x9A4) | table below |
| `Z` | **zerod + 2 × profiled** | 0 = nothing, 2 = profiled only, 3 = profiled and zerod (verified) |
| `%` | destination (0x984) | |
| `H` | motor current (ADC) | ~0x1C idle |
| `;` | 0x9AC | -1 after `auto_zero` or an open. Probably a profile/retry index |
| `v` | constant 0x1E0 = 480 | firmware version |
| `=` | byte 0x9C4 | |
| `p` | position = raw encoder − K | |
| `K` | door offset (full travel) | |

Boot (`@@`) and move-summary lines: `{` profile bucket (position / 96, max 143), `^`
position, `(` expected current from the profile (0x998), `)` current limit in use (0x99C),
`` ` `` new state (0x9A4), `#` latest motor current sample (0x9EC; *not* a fault code, read
in the motion monitor at 0x08003468). The move summary prints when a state change sets
the event flag.

## Control states (`U`)

From the jump table at 0x08003D78 (40 entries) and the command code:

| U | Meaning |
|---|---|
| 0 | boot init → 4 |
| 4 | unconfigured idle. Shares a handler with 0x1D, 0x24 and others: no motion, just prints pending events |
| 7 | open, hold-open timer running (`open_and_close`). **Also entered after an obstruction reversal (26) during a plain `close`; from here the STM recloses by itself as soon as `clear_obstruction` arrives**, without waiting for `set_open_wait` (verified) |
| C | opening |
| D | closing |
| E | closed idle. **Physical button opens from here if `set_button_op 1`** |
| F | open (stay open) |
| 10 | auto_zero start |
| 12 | effort obstruction: reversing |
| 14, 15 | auto_zero passes |
| 1A | disengage: moving open |
| 1B | disengage with no zero: seeking the end stop |
| 1C | engage: moving closed |
| 1D | disengaged, resting |
| 1E | engaged without moving (already closed) |
| 25, 26 | obstruction (safe-to-close false) reversal |
| 27 | **open failed near closed**: idle. Set by the opening handler (0x080040F8) when the move ends in a stall or overcurrent while the door is still near closed. Verified live (an open that stalls at the start) |

Opening handler (state C), on the motion monitor's result (0x9F4: 1 overcurrent, 2 stall,
3 target reached):
- 3 → state 7 (hold timer) or close, depending on `open_and_close`/safe-to-close.
- 1 with p ≥ 250 − K, or 2 with p > −622 → stop, state 0x27 (0x080042DC / 0x08004304).

The state dispatch is `__gnu_thumb1_case_uhi` at 0x08003D74; handler addresses are in
the halfword table at 0x08003D78 (C 0x080040F8, D 0x0800437C, 27 = the idle handler 0x08003FE4).

Values above 0x27 hit an assert (`Src/control.c` line 0x42E) and reboot.

## Motion safety built into the STM (0x08003350, every 10 ms)

- **Current limit follows the learned profile:** limit = profile[bucket] × (100 + margin) / 100
  + offset. The margin is larger when no profile is loaded.
- **Overcurrent:** more than 35 consecutive over-limit ticks (~350 ms), after the first
  second of a move → stop (`this->consecutive_overcurrents`, *debug*), Pid Status 1, and the
  retry/reverse logic.
- **Stall** (|velocity| < 100 after 1 s): closing near closed (p > −100) with a profile →
  "target reached after stall", treated as closed. More than 10 stalls there → stop,
  status 3. Elsewhere: stop after 2 ticks (no profile) or 15 ticks (profiled), and flag which end.
- **Target reached:** within 45 counts (10 when closing with a profile).
- None of this depends on the host. **The STM has no host-silence timeout**: a move
  in progress finishes on its own if the host dies.

## Button (0x08003B9C, debounced in 0x08004870)

The debouncer reports event codes. Which press length gives which code isn't traced yet.

- Event 2 (a press): in state E with `set_button_op 1` → open. With `set_button_op 0` it is
  discarded, **except in state D (closing)**, where it goes to the closing handler whatever
  `set_button_op` says. A press in state F (open) starts a close (seen on the wire with
  `set_button_op 1`).
- Event 3 (handled once per boot): prints `+` to the imp ("STM Sending + for BT",
  presumably Bluetooth setup on the imp side).
- **Event 6: wipes both profile tables, zerod = 0, prints `~`.** This is a factory reset of
  the calibration. The brain must treat position as unknown and re-run `auto_zero`.

## Bring-up sequence (verified live, no motion)

```
\n                                  (flush any glitch byte)
set_profile open 0 …  … (18 lines)
set_profile close 0 … … (18 lines)
set_open_to -6136
set_open_wait 15
set_button_op 0|1
profiled_state 1                    → Z=2
set_dooroffset 13588                → K=3514, p=-13588
zero -72 14                         → U=E, Z=3, p=-72
```

After this the STM is in the same configured closed-idle state the imp leaves it in. The
next `engage` (if p ≤ −51), `open`, `close` or `open_and_close` will move the motor.

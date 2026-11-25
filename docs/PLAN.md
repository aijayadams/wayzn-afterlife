# Plan: keep the door running after Wayzn's cloud shuts down

> **Naming note (2026-11-25):** this plan's options map onto the README's phases: **option A** (replace the
> imp, keep the STM32) is **Phase A**, now working; **option C** (a new drop-in board) is **Phase B**, in
> design; option B (new STM32 firmware) was not pursued. The plan is kept as written for the record.

Written 2026-11-20. Wayzn's site now shows a closing notice; we have no
shutdown date for Firebase or their Electric Imp account.

## What depends on what

| Function | Path today | Survives Firebase shutdown? | Survives Imp-account shutdown? |
|---|---|---|---|
| HA status | Firebase RTDB `wayzn-app-tokens` | **No** | No |
| HA open/close and settings | Signed POST to the Imp **agent** URL; nonce and URL come from Firebase `wayzn-app-nonce` | **Probably no**: we lose the nonce. The agent itself might keep running if we cached the URL and the nonce scheme allows it (test) | **No** |
| Motion → open, button, schedule, obstruction stop | On the device (imp004m Squirrel + STM32) | Unknown | **Unknown, and the key question.** impOS can run cached code offline only if Wayzn's code allows it. Worst case the door stops at boot waiting for the cloud |
| Firmware/settings changes | App → agent → device | No | No |

The worst case is a door that only works by hand. Under "never lock a dog
out", the minimum acceptable interim state is: **the door opens for the dog
with no cloud at all**, plus some way to tell what it's doing.

## Phase 0: secure what we have (this week, no hardware risk)

1. **Simulate the shutdown.** Block the door's WAN at the router (it talks to
   52.10.174.82:31314 and other impCloud hosts), then power-cycle it. Record
   whether motion-open, the button, the schedule and close-after-hold still work,
   after 5 minutes and after 24 hours. This tells us how urgent everything
   below is. Repeat with an established session (block without power cycle).
2. **Snapshot all cloud state** with the existing client (`wayzn.py debug
   discover`, `status --verbose`, `schedule --json`). Keep it here under
   `captures/cloud/` (redact nonces/keys). These values (`DoorOffset`, `OpenTo`,
   `Mask`, thresholds, schedule) are the configuration any replacement must
   reproduce.
3. **Ask Wayzn, while someone is still there:**
   - transfer our imp004m device (or release it) to our own impCentral account;
   - the STM32 firmware and/or source, and the Squirrel device/agent code;
   - the shutdown date for Firebase and the Imp account.
   They may say no, but a yes on the first item alone keeps the existing board
   fully usable with our own Squirrel code.
4. **Get a spare unit or board** (eBay / marketplace; Chewy lists it as
   discontinued). All destructive work happens on the spare.
5. Do the pending `token:` logcat capture from `wayzn/docs/PROTOCOL.md` while
   the app still works. It's cheap, and it's the last chance.

## Phase 1: identify and probe (passive, on the in-service door)

See [PROBING.md](PROBING.md). In order of value:

1. **STM+IMP UART**, passive. Learn the imp↔STM32 protocol. If the imp sends
   high-level commands (`open`, `goto <counts>`, `set current`) and the STM
   reports state (position, current, obstruction), we can replace the imp and
   keep Wayzn's motor firmware.
2. **Ownership map**: continuity-trace which MCU owns the Grid-EYE I2C, encoder,
   button, LEDs, accelerometer, J105.
3. **STM32 SWD** via JP300: read the RDP level. If RDP0, dump the 64 KB flash.
   If RDP1, note it and stop (see Phase 2B).
4. **I2C sniff** of the Grid-EYE: frame rate, configuration writes, which MCU
   reads it.
5. **SPI flash** U210: dump read-only on the spare (likely encrypted; low
   priority).

## Phase 2: choose the brain

Three options, cheapest first. Phase 1 results pick one; C is the long-term
fallback regardless.

### A. Replace the imp, keep the STM32 ("brain transplant")
ESP32 (ESPHome or custom) wired to the STM+IMP header, speaking the
protocol from Phase 1. The imp is held in reset (IMP_RST) or its TX lifted.
- **Pros:** fastest; keeps Wayzn's tuned motor control, current limiting,
  obstruction detection and calibration; reversible (no rework on the board).
- **Cons:** only works if the STM does the motor work and the protocol is
  tractable. If the imp owns the Grid-EYE, the ESP32 must also tap that I2C bus
  (the imp must then be held off the bus).
- **Gate:** Phase 1.1 shows a readable command/state protocol.

### B. New firmware on the STM32F051 (+ ESP32 for networking)
Write our own motor controller for the existing board.
- **Pros:** full control, no Wayzn code left in the motion path.
- **Cons:** we re-derive motor control (encoder homing, current limit,
  obstruction, accel/decel) on a 64 KB Cortex-M0, and must map every pin
  first. If RDP1 is set, the original firmware is gone as soon as we reflash, so
  dump it first (on the spare: published STM32F0 RDP1 attacks exist, e.g.
  Obermaier & Tatschner, WOOT '17).
- **Gate:** pin map done; spare unit available.

### C. New drop-in board
Our own PCB in the same footprint and connectors: ESP32-S3, a modern H-bridge
with current sense (e.g. DRV8874/DRV8876), encoder input, Grid-EYE on I2C,
12 V buck, button/LED. Runs ESPHome with a custom component, talking to HA
over the native API, with no cloud at all.
- **Pros:** fully ours, maintainable for years, spares can be built; any
  stock Wayzn unit can be converted.
- **Cons:** most time: schematic, layout, fab, bring-up, motor tuning.
  Weeks, not days.
- **Gate:** connector families and mechanical outline measured.

**Recommendation:** run A and the C groundwork in parallel. A is likely the
quickest route to a cloud-free door. C is the durable answer and removes the
dependence on an STM32 binary we can't patch. Pursue B only if A fails and
the dump turns out to be trivially readable.

## Decision: plan C, our own control board (2026-11-24)

Plan A works (the ESP32 brain runs the door through the STM). The next step is C: replace the whole WYP-BC-100
with our own board, where the ESP32 drives the motor with standard brushed-DC control. The STM firmware is used
as a reference, not cloned. Design, measurement checklist and safety rules: [CONTROL-BOARD.md](CONTROL-BOARD.md).
The carrier board (`hardware/carrier/`) is parked as the fallback.

## Decision: the imp leaves the critical path (2026-11-20)

Once Wayzn's cloud is gone, the imp can never be updated, debugged or
reconfigured. Worse, a running imp could act unpredictably and still act on the
STM (it resets the STM at its own boot, then pushes config). So plan A is
**not** "ESP32 alongside the imp": the imp is **silenced or removed**, and the
ESP32 does everything the imp did, with no network dependency.

What has to be true, and how we'll know:

| Requirement | Status / how to establish |
|---|---|
| Imp can be held silent: its pins high-Z, it doesn't reset the STM | **Verified 2026-11-20 (bench):** with SW1 (IMP_RST) held, the imp sends nothing and the STM boots and runs on its own. On release, the imp resets the STM (~18 s later, via a trace under U200) and reconfigures it, so the imp must stay held **permanently**: wire SW1's reset-side pad to GND or an ESP32 pin. BOOT0 is tied to GND, so the imp can't enter the STM bootloader |
| ESP32 can drive STM RX and read STM TX | Header pins known (115200 8N1). Need series resistor and short wiring (noise seen in session 2) |
| ESP32 can reset the STM (optional but wanted for recovery) | **Verified:** NRST = SW300 top pad; a press with the imp held gives a clean STM reboot (`@@`). ESP32 drives it open-drain |
| ESP32 reads the Grid-EYE on J2 | J2 SCL/DATA = TP1/TP2, not on the STM (imp-owned); 4.7 kΩ pull-ups on board. Still to do: check that the bus idles high with the imp held, then read the Grid-EYE from the ESP32 |
| ESP32 runs all imp-side policy with no network | Detection (mask, dog size, sensitivity), thermal interlock, auto-open, home/away, schedule, hold-open timer (**the STM runs it** for `open_and_close`; verified 2026-11-20), position/state persistence + `zero` at boot, calibration flow |
| Power for the ESP32 | **Decision (2026-11-21): the board's 3V3 rail** (ESP32 3V3 pin to J105-8, GND to J105-4; never VIN, and never USB at the same time). Tested: 23 min on board power, alternating 30 s full Wi-Fi TX (~1,360 pkt/s) and 30 s idle, brownout detector raised to ~2.8 V, imp running, with door cycles: no reboots, no brownouts (verified, captures/2026-11-21_power-3v3.log). The board regulator is marked "DK-K0D" (not identified). Fallback: our own small buck from J7 12 V |
| Other imp duties identified | Continuity: status LEDs, accelerometer (U50), J105 (pet-tag bridge?). Decide which we need |
| Removal is reversible | Prefer holding IMP_RST over desoldering; do anything irreversible on a spare board |

## Phase 3: software for whichever brain wins

- ESPHome (`cover` entity) or a small custom firmware with MQTT. Either way
  the door integrates with HA locally.
- Port these behaviours from the cloud state, with defaults taken from the
  Phase 0 snapshot: open-to distance, hold-open time, motion mask and
  thresholds, schedule, home/away auto-open policy, obstruction detection,
  button enable, LED brightness, auto-zero (calibration).
- Safety in firmware, not HA: motion-open must work with Wi-Fi down;
  current-limit and obstruction reverse; an oscillation detector that latches
  **open** and raises a fault (AGENTS.md 2–4); a watchdog.
- **Calibration (find true closed) is a core capability, not a setup step.** The STM
  only knows the position it is told, and loses it on every reboot, on a button
  event 6 (`~`), and on power loss. The firmware must be able to run the full
  homing routine by itself: send profile and settings, `clear_obstruction` (or our
  own Grid-EYE gate), `reset_zero`, `auto_zero`, follow states 10 → 14 → 15 to a rest
  state, then read back the learned `K` and the profile (`get_profile`) and persist
  them on the ESP32. It must also detect a calibration that fails (STM reboot,
  stall in the wrong place, timeout) and report `unknown` rather than guess.
  Reference run: the imp's in-door `auto_zero` 2026-11-20 20:07:25–20:08:53
  (captures/2026-11-20_P1_bench-motor.log).
- Honest state: report position from the encoder and an explicit
  `unknown`/`fault` when it isn't homed.
- Retire `wayzn` / `wayzn-ha-integration` once the new path is live.

## Decision log

| Date | Decision | Why |
|---|---|---|
| 2026-11-20 | Repo created; Phase 0/1 started | Wayzn closing; cloud shutdown date unknown |
| 2026-11-20 | Plan A = imp **out** of the critical path (held in reset or removed); ESP32 does all imp duties offline | Imp can't be updated after shutdown and could still interfere (it resets/configures the STM) |
| 2026-11-20 | Internet-blocked imp test deferred | Not needed if the imp is removed from the path |
| 2026-11-20 | ESP32 powered from its own buck off 12 V, not the board 3V3 | Isolation from the STM rail and motor noise; the board regulator (marked "DK-K0D") couldn’t be identified |
| 2026-11-21 | Supersedes the row above: ESP32 runs from the board 3V3 | Passed a 23 min worst-case test (max Wi-Fi TX, imp running, door cycles, BOD at ~2.8 V) with no resets; one fewer part to build |

# wayzn-afterlife: Governing Principles

Principles 1–4 cover the whole dog door system and are shared with the `wayzn`
integration's AGENTS.md. They outrank implementation convenience and apply
equally to any firmware or hardware built here.

1. **Not lying matters.** State honesty above all. If state cannot be proven
   fresh, report "unknown"/unavailable. A wrong "open" is worse than an honest
   "unknown".
2. **Open is preferable to fast oscillations.** When state is uncertain, bias
   toward open/known-safe. Never flip-flop to chase shaky signals.
3. **Oscillating is an error.** Rapid open/close cycling is a fault: detect it,
   log it, alert on it.
4. **Never lock a dog out.** The failsafe bias is always toward open. Assumed
   state must never suppress an open command.

## Hardware-work rules (this repo)

- **The production door stays working.** Do destructive or risky work
  (desoldering, flash erase, RDP changes, cutting traces) only on a spare
  unit or board. The in-service door gets passive probing only.
- **Read before write.** Never erase, unlock or change option bytes on an MCU
  until its flash has been dumped and the dump verified (two reads, same hash).
  On STM32, dropping read protection from level 1 to 0 **mass-erases** the chip.
- **Passive taps first.** On the STM+IMP UART, connect analyzer RX/ground only
  and do not drive TX until the protocol is understood.
- **Mind the grounds and voltage.** Logic is 3.3 V; the input is 12 V and the
  motor bridge switches 12 V. Share ground with the board; never probe J6/J7
  with a 3.3 V tool.
- **The motor is strong enough to hurt.** A firmware bug can drive the panel.
  Test new motor code with the belt/drive disengaged or the panel clear, and keep
  a hand on the 12 V supply.
- **Wayzn's firmware is never published.** Flash dumps and their decompiles go in
  `firmware-dumps/`, which is gitignored here. Keep your own copy somewhere
  private and backed up (losing it would be unrecoverable), but never commit it
  to this repo or any public fork or mirror.
- Every finding goes in `docs/` with a confidence tag: **verified** (measured
  or read), **marking** (read off the part), **inferred** (educated guess).

## Working on a live door

The door is in service with dogs using it. These rules apply to every session that touches it.

- **Ask the owner before anything that moves the door:** `open`, `close`, `cal`, `disengage`, a test
  move or any `raw` STM motion command. Say what will move and how far. Read-only checks (status,
  logs, `ge`) need no go-ahead.
- **Flash over the air only while the door is still:** check that `/api/status` `detail` is `closed` or
  `open` first. The firmware also refuses an OTA while moving, but an update that restarts the ESP32
  mid-calibration costs a recalibration.
- **Power:** never connect the ESP32's USB while it is powered from the board's 3V3, and never power it
  from VIN. Flash over Wi-Fi in the door; use USB only on the bench with board power off.
- **After a power cycle,** check in this order (syslog or the console shows each step):
  1. Cold start: the Grid-EYE warms up for 90 s. The doorway reads occupied, there is no auto-open and
     no calibration in that time.
  2. The STM has no position, so the brain runs `auto_zero` once the doorway has been clear 10 s.
  3. The run ends either measured (`zero` from the STM) or at the closed stall (zeroed as closed,
     `position: inferred`, with `stall_allowance` applied).
  4. `/api/status`: `state` closed, `fault` null, `grideye_ok` true, `last_move_effort` well under 100.
  5. With the owner's go-ahead, one open and close: the close ends near -72 at roughly 30-55 % effort
     with no reversal.
- **Host tests before every flash** (from `firmware/brain`):
  `c++ -std=c++17 -O2 -I src test/host_test.cpp -o /tmp/host_test && /tmp/host_test [capture.log ...]`.
- **Build and flash** (from `firmware/brain`, `tools/uart-sniffer/secrets.ini` filled in from the example):
  `pio run -e brain` builds; `pio run -e brain-ota -t upload --upload-port <door-address>` flashes over
  Wi-Fi (the default port is `wayzn-esp.local`).
- **Console:** `nc <door-address> 23`, first line `auth <api_token>`, then `?` for the commands:
  `status`, `ge` (one frame and its rise over the background), `gebg` (relearn the empty doorway),
  `cal`, `fault clear`, `cfg` (resend the STM setup), `profile`, `log on|off`, `raw <stm line>`
  (expert only). The API (docs/API.md) does the same for Home Assistant.
- **Logs:** with `syslog_host` set, every event goes to syslog as APP-NAME `wayzn-brain`. Read those
  first to see what the door did; they are the record of every move, refusal and fault.
- **API changes:** docs/API.md is the contract with the `wayzn` integration's local mode
  (aijayadams/wayzn). Change both together, and keep new fields additive.
- **Door-specific values** (travel, stall allowance, thresholds) are settings set from Home Assistant,
  never constants in the firmware.
- Notes about one particular home (addresses, hostnames, deploy steps) belong in the owner's private
  notes, not here.

## Publishing hygiene (this repo is public)

Everything committed here is public for good, so check every file before it goes in:

- **No personal details:** no names beyond the GitHub handle, no email addresses, street addresses, Wi-Fi
  names, IP addresses, hostnames or local paths (`/Users/...`). Commits use the GitHub noreply address.
- **Photos and scans:** strip all metadata (GPS, camera, dates: re-save through an image library and check
  that no EXIF is left). Blur the unit's serial/barcode label and the imp module's label before committing.
  Don't commit phone originals (`*.HEIC`) or full-resolution scans; commit cleaned, downscaled copies.
- **Generated files:** regenerate images and fab outputs from the cleaned sources in this repo, never copy
  them from a private checkout (an overlay drawn on an unblurred photo leaks the label).
- **Wayzn's firmware:** describing behaviour and scales is fine; the dump, the decompile and the code are
  not (see above).
- **Secrets:** only in gitignored files (`secrets.ini`); never in code, logs or docs.


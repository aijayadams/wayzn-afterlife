# wayzn-brain firmware (production)

ESP32 firmware that replaces the imp. The STM32 keeps Wayzn's motor firmware; the ESP32 drives
its shell and reads the Grid-EYE. Principles: AGENTS.md. STM details: docs/STM-FIRMWARE.md.

- `src/stm_parse.h`: STM output parser (status, move summaries, boot `@@`, replies, `~`).
- `src/presence.h`: Grid-EYE presence detector (learned background, warming only, mask).
- `src/presence2.h`: v2 Grid-EYE detector (see below).
- `src/api.h`: HTTP API for Home Assistant (docs/API.md).
- `src/door_config.h`: generated from `door-config/` by `tools/gen_door_config.py`.
- `src/main.cpp`: supervisor, safety, console. `src/net.h`: Wi-Fi console + OTA.
- `test/host_test.cpp`: host unit tests, plus a replay of UART captures through the parser and
  detector (no hardware). Build and run from this directory:
  `c++ -std=c++17 -I src test/host_test.cpp -o /tmp/host_test && /tmp/host_test [capture.log ...]`.
  A capture is a log recorded with `tools/uart-sniffer/capture.py` (channel-B STM output, and
  `# F` Grid-EYE frames for the detector replay).
- `research/`: Grid-EYE detector research (docs/DETECTION-RESEARCH.md): Python reference
  detectors, a capture parser, and `replay2.cpp` (v2 unit tests and capture replay).

Build: `pio run -e brain -t upload` (USB) or `pio run -e brain-ota -t upload` (Wi-Fi), with `tools/uart-sniffer/secrets.ini` filled in from
`secrets.ini.example` (Wi-Fi, OTA password, API token, optional syslog host).

Behaviour:
- On ESP boot: adopt the STM's state (no reset) if it talks within 25 s; else pulse NRST.
- STM boot (`@@`): position UNKNOWN, resend the setup. If it rebooted at the closed stall
  (closing near closed, or auto_zero pass 14 near the end), zero as closed, marked inferred.
- STM silent 30 s: NRST pulse (≤1/min). Keepalive `c` every 20 s keeps the 1.5 s status rate.
- Grid-EYE presence → `set_obstruction` (the STM reverses a close); sensor failure counts
  as not clear. Close is refused unless position is known, the doorway is clear and the
  sensor is reading.
- `auto on`: presence opens the door (from closed or closing); it closes after `hold`
  seconds with the doorway clear. Default off.
- Faults latch OPEN (3 reversals in 2 min, move timeout, STM state 27, failed calibration).
- Open with unknown position uses `disengage` (always drives fully open).
- No OTA while the door is moving.
- Detection: the doorway is occupied (no closing; a close reverses) if either the original detector
  (`src/presence.h`) or v2 (`src/presence2.h`, docs/DETECTION-RESEARCH.md) says so. Presence (auto-open)
  still comes from the original; v2's presence is logged for comparison.
- Close recorder: the last 15 s of Grid-EYE frames stay in RAM (~10 KB); each close ships the 10 s before
  it and every frame until 3 s after the door stops to syslog (`frame e=...` lines, base64, one per
  frame). `tools/close_frames.py` decodes them.
- Syslog: every event line goes as RFC 5424 UDP to `syslog_host:5514` (secrets.ini; empty = off),
  APP-NAME `wayzn-brain`, facility local0; err = fault, warning = STM reboot/reset/refusal, notice =
  action/door change, info = the rest. Grid-EYE frames are sent only around closes (close recorder). Clock from NTP (router first).

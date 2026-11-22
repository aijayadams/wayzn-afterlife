# wayzn-brain firmware (production)

ESP32 firmware that replaces the imp. The STM32 keeps Wayzn's motor firmware; the ESP32 drives
its shell and read the Grid-EYE. Principles: AGENTS.md. STM details: docs/STM-FIRMWARE.md.

- `src/stm_parse.h`: STM output parser (status, move summaries, boot `@@`, replies, `~`).
- `src/presence.h`: Grid-EYE presence detector (learned background, warming only, mask).
- `src/door_config.h`: generated from `door-config/` by `tools/gen_door_config.py`.
- `src/main.cpp`: supervisor, safety, console. `src/net.h`: Wi-Fi console + OTA.
- `test/host_test.cpp`: host unit tests + replay of real captures (no hardware).

Behaviour:
- On ESP boot: adopt the STM's state (no reset) if it talks within 25 s; else pulse NRST.
- STM boot (`@@`): position UNKNOWN, resend the setup. If it rebooted at the closed stall
  (closing near closed, or auto_zero pass 14 near the end), zero as closed, marked inferred.
- STM silent 30 s: NRST pulse (≤1/min). Keepalive `c` every 20 s keeps the 1.5 s status rate.
- Grid-EYE presence → `set_obstruction` (the STM reverses a close); sensor failure counts
  as not clear. Close is refused unless position is known, the doorway is clear and the
  sensor is reading.
- `auto on`: presence opens the door (from closed or closing); it closes after `hold`
  seconds with the doorway clear. Default off until validated.
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
  action/door change, info = the rest. Raw Grid-EYE frames are not sent. Clock from NTP (router first).

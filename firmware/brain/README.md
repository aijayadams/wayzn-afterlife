# wayzn-brain firmware (production)

ESP32 firmware that replaces the imp. The STM32 keeps Wayzn's motor firmware; the ESP32 drives
its shell and reads the Grid-EYE. Principles: AGENTS.md. STM details: docs/STM-FIRMWARE.md.

- `src/stm_parse.h`: STM output parser (status, move summaries, boot `@@`, replies, `~`).
- `src/presence.h`: Grid-EYE presence detector (learned background, warming only, mask).
- `src/presence2.h`: v2 Grid-EYE detector (see below).
- `src/api.h`: HTTP API for Home Assistant (docs/API.md).
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
- `auto on`: presence opens the door from any state that isn't open or opening (with the
  position unknown, by `disengage`); it closes after `hold` seconds with the doorway clear.
  Default off.
- After a reversal the STM waits (state 7) and recloses by itself on `clear_obstruction`, sent
  once the doorway has been clear ~3 s. If an open, an HA hold or a fault comes in meanwhile, or
  the doorway stays occupied 5 min, the obstruction stays set and the door stays open until a close.
- The door's button: a close it starts gets the same rules as an HA close (releases the hold;
  reversed and held open on a fault, unknown position or sensor not reading; reversed and
  reclosed once clear if the doorway is occupied). Its open is unrestricted.
- Faults latch OPEN (3 reversals in 2 min, move timeout, STM state 27, failed calibration); the
  open is retried (3 times, 15 s apart) if the door isn't open. Clear with `POST /api/clear_fault`
  or `fault clear`.
- Kept across ESP32 software resets (RTC memory): the HA hold, a fault, the obstruction flag, and
  the last empty-doorway backgrounds of both detectors (so a dog in the doorway at boot isn't
  learned as background). A power cut clears them.
- STM silent since ESP boot: NRST pulse at 25 s, then once a minute until it talks.
- Grid-EYE returning 20 identical frames counts as not reading.
- Telnet console: the first line must be `auth <api_token>`; anything else disconnects.
- Open with unknown position uses `disengage` (always drives fully open).
- Position recovery: when the STM has no position after a start (power cut, STM reboot), the
  brain runs `auto_zero` itself once the doorway has been clear 10 s. Nothing is sent to the STM
  during the run (it takes any command as the end stop, giving a wrong zero); an open waits for
  the end. The measured travel is checked against `expected_travel` (a setting, from HA; 2 %);
  a wrong one is rejected and the calibration retried. 3 failures latch a fault (door open).
  auto_zero's first closing pass doesn't end by itself at the closed end (it keeps pushing at
  reduced force, or browns the STM out): still for 5 s, the brain resets the STM. A reboot in that
  pass past 85 % of `expected_travel` is the closed end: zeroed as closed with that travel
  (inferred); otherwise (or with no `expected_travel`) the position stays unknown and the
  calibration is retried. A calibration timeout resets the STM and opens.
- No door constants in the firmware. STM setup after every STM boot: the learned effort profile
  (read back with `get_profile` after each good calibration, kept in NVS; none stored = the STM's
  default margins), the open position, `set_open_wait 15`, `set_button_op 1`, `profiled_state 1`.
  The travel comes from the STM's own measurement, or `expected_travel` for an inferred zero.
- Effort: each STM move summary gives the current and the STM's current limit at that moment;
  `last_move_effort` is the highest current/limit (%) of a move.
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

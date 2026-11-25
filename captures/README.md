# Captures

Name files `YYYY-MM-DD_<probe>_<event>.<ext>`, e.g. `2026-11-21_P1_open.sal`.
Add a line for each below: probe, what happened, settings (baud, channels), and
anything notable. Redact Firebase nonces, wKeys and session tokens before committing.

| File | Probe | Event | Notes |
|---|---|---|---|
| 2026-11-20_P1_boot-bench-nomotor.log | P1 | Bench, 12 V, motor unplugged: power-up, then EXP RST, then IMP_RST | 115200 8N1; A=header RX (imp→STM), B=header TX (STM→imp). See docs/STM-PROTOCOL.md |
| 2026-11-20_P1_inservice-session1.log | P1 | In door. Boot with drive disengaged; STM TX wire loose until 13:26; `auto_zero` at 13:29:20; ESP32 reset by motor start | |
| 2026-11-20_P1_inservice-session1b.log | P1 | In door. Calibration + `get_profile` (13:30), close with dog (13:31), button (13:32:27), AutoOpen off (13:33), open/close (13:34), open distance 12/8 in (13:35), hold 15 s (13:37), button op off/on (13:38:22), obstruction det. off/on (13:38:35), disengage/engage (13:39), AutoOpen on (13:39:33) | ESP32 reconnect logic in use |
| 2026-11-20_P1_inservice-session2.log | P1 | Detection settings (mask 13:48:10, sensitivity 13:48:15, dog size 13:48:21), disengage 13:48:41, engage 13:49:00, dog-triggered open_and_close 13:49:18 | Wires hot-glued; 9 s status gap at the end of engage. Power cycles: closed 13:52 → `zero -69 14`; open 13:53 → `zero -6194 15`. STM TX silent 13:51:15–~13:56, then OK. Heat-trigger reversals 13:56:34/40; hand-resist (effort) reversals 13:57:43, 13:58:03; settled closed |
| 2026-11-20_P1_inservice-holdopen.log | P1 | Hold 15 s: motion `open_and_close` 14:06:06, open (state 7) 14:06:12, STM self-closes 14:06:27; app open 14:06:34 | |
| 2026-11-20_P1_bench-imp-held-reset.log | P1 | Bench, motor unplugged: SW1 held at power-up | STM boot + 14 s of `4U` status; channel A wire suspect, so the imp silence is unproven |
| 2026-11-20_P1_bench-imp-held-reset2.log | P1 | Bench, motor unplugged. Run 2 (16:49–16:51): tap wires broken, ignore. **Run 3 from 16:55:52:** SW1 held → STM boot 16:56:01, imp silent; SW300 16:56:52 → STM reboot; SW1 released ~16:56:53 → imp resets STM 16:57:11 and pushes config; then disengage/engage cycling (no motor) | Proves the imp can be held silent and the STM runs on its own |
| 2026-11-20_P1_i2c-grideye-boot.log | P3/I2C | Bench, imp running: power-up 17:31:04, hand waves 17:31:11–17:33:29 | tools/i2c-sniffer on TP1/TP2; 256+ frames, all 128 B; three glitch lines |
| 2026-11-20_P1_bench-tilt.log | P1 + P3/I2C | Bench, imp running, motor unplugged: power 19:48:00; both boards tilted 19:48:20–19:48:56; IR board alone from ~19:49:07 | uart-sniffer env esp32dev-i2c; ~15% accel reads misdecoded (`W1F`) |
| 2026-11-20_P1_bench-motor.log | P1 + P3/I2C | Bench, motor + encoder plugged in, from 19:56:24 (motor ran 19:58:59–19:59:05; imp reset 20:00:07). **In the door from 20:05:47:** power-up, lever disengaged/engaged, re-zero 20:07:25, open/close ~20:10 | uart-sniffer env esp32dev-i2c; `task_wdt` flood splits many B lines (reassemble on `K` / `#`) |
| 2026-11-21_power-3v3.log | Wi-Fi (tools/power-test) | ESP32 on USB until 09:19, on the board 3V3 from 09:21 (boot #4), door cycles ~09:21–09:44 | Columns: uptime, load, pkts/s, RSSI, heap, vdd (no divider: ignore) |
| 2026-11-21_P1_imp-hardwired-reset.log | P1 + P3/I2C | Bench, imp reset wire hard-wired to ground, ESP32 on USB: 12 V power-up 11:18:36 | Imp silent, 0 I2C transactions, STM `4U` alone: 1.5 s, then 10 s after ~25 s. Sniffer `wdt_deinit=0` (no error flood) |
| 2026-11-21_P1_stm-shell-probe.log | P1 (ESP32 → STM) | ESP32 drives the STM shell (env esp32dev-host, disarmed): queries 11:30, profile push + read-back 11:31–11:32, config + `zero -72 14` 11:32–11:33, `reset_zero`, `reset` 11:33:19 | No motion commands sent. `# inbox:` / `# sent:` lines mark what we sent; our bytes loop back as A frames |
| 2026-11-21_P1_bench-open-close.log | P1 (ESP32 → STM) | Bench, imp held in reset, motor + encoder plugged in. Config 13:29–13:31 (first batch garbled by USB overflow, then reset + paced resend + `get_profile` check); `open` 13:31:21 stalled against the end stop (wrong zero → state 27); `zero -6136 15`; `close` 13:38:22 and `open` 13:38:45 both completed; `disarm` 13:39:03 | env esp32dev-host. `# inbox:` / `# sent (ARMED):` lines mark what we sent |
| 2026-11-21_P1_net-console.log | P1 (ESP32 → STM, over Wi-Fi) | Telnet console of env esp32dev-net. 13:45–13:46: first net build (every byte its own frame, fixed); OTA updates 13:47, 13:49, 13:51; `version`/`quad`/`vsense` 13:48:32. STM idle in F at −6143 until the power cycle at ~14:18. 14:09–14:34: I2C master tests (floating SCL from a loose TP1 wire, resolders, then scan 0F 69, KXTJ3 and Grid-EYE reads) | Logged with `capture.py --port socket://wayzn-esp.local:23` |

**Before adding a capture here:** `captures/` is gitignored so raw logs never get committed by accident. Scrub a
log first (the LAN IP, Wi-Fi name and any hostnames, plus Firebase nonces, keys and tokens), then add it with
`git add -f`. In the logs below, `<lan-ip>` and `<wifi>` replace the originals.


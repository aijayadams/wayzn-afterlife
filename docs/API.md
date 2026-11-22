# wayzn-brain local API (for Home Assistant)

Served by the door's ESP32 (`firmware/brain/src/api.h`) on port 80, LAN only, plain HTTP.
mDNS service `_wayzn-brain._tcp`, host `wayzn-esp.local`. Every request needs
`Authorization: Bearer <api_token>` (from `tools/uart-sniffer/secrets.ini`, gitignored);
otherwise `401`.

| Request | Reply | Notes |
|---|---|---|
| `GET /api/status` | `200` JSON, below | |
| `POST /api/open` | `200 {"ok":true}` | **Always accepted** (never lock a dog out). Holds the door open (no auto-close) until `/api/close`. Unknown position: drives fully open (`disengage`). |
| `POST /api/close` | `200 {"ok":true}`, `200 {"ok":true,"reason":"waiting for the doorway to clear"}`, or `409 {"ok":false,"reason":"…"}` | If the doorway is occupied, the close is accepted and runs once the doorway has been clear for 3 s; it expires after 5 min and is cancelled by any open or a fault (`close_pending` in the status). Refused (409) when a fault is latched, the position isn't known or the Grid-EYE isn't reading. Closed or closing already: `200` with a reason. |
| `POST /api/stop` | `200 {"ok":true}` | |
| `POST /api/settings` body: any subset of `settings` below | `200 {"ok":true}` or `400 {"ok":false,"reason":"…"}` | All keys are validated first, then all are applied (or none). Unknown keys, wrong types and out-of-range values are rejected, never clamped. Saved on the door; every change is logged. |
| `POST /api/auto` body `{"on":true}` / `{"on":false}` | `200 {"ok":true}`, `400` on any other body | Grid-EYE auto-open (and its auto-close) on/off. Saved on the door. HA's open/close work either way. |

Commands are acknowledged in ~0.1-0.2 s (measured); the move itself takes ~5-8 s (open to
-6193 in ~6 s, close to -73 in ~8 s). Poll `/api/status` to follow it.

`/api/status`:

```json
{"state":"closed","detail":"closed","position":"known","p":-75,"fault":null,
 "presence":false,"occupied":false,"auto":true,"hold":false,"grideye_ok":true,
 "status_age_ms":6847,"uptime_s":14,
 "settings":{"auto_open":true,"auto_close":true,"obstruction_detection":true,"hold_s":15,
  "open_percent":45.2,"open_inches":8.0,"open_counts":-6136,"presence_threshold":1.00,"occupied_threshold":0.60,
  "noise_c":0.70}}
```

- `state`: `open` | `closed` | `opening` | `closing` | `unknown`. `unknown` when there is
  no STM status in the last 15 s, or the STM is unconfigured/calibrating/failed: never a guess.
  An obstruction reversal reads `opening`.
- `detail`: the brain's internal phase (`reversing`, `calibrating`, `open-failed`, …).
- `position`: `known` (zeroed) | `inferred` | `unknown`. `p`: encoder position (closed ≈ -73,
  open ≈ -6190).
- `presence_age_ms`, `occupied_age_ms`: milliseconds since presence / occupied (either detector) was last true; `-1` if not since boot. A poller can use these to see a visit shorter than its poll interval.
- `close_pending`: a close is waiting for the doorway to clear.
- `fault`: `null` or the latched fault (the door is then held open).
- `presence` (someone wants out), `occupied` (doorway not clear; blocks closing), `auto`
  (local Grid-EYE auto-open/close), `hold` (opened by HA, held), `grideye_ok`.
- `motor_current`: live motor current (STM32 units; ~30 at rest, ~450-800 while moving).
  `last_move_peak_current`: peak of the last move (-1 until a move).
- `status_age_ms`: age of the last STM status line. `uptime_s`: ESP32 uptime.
- `settings`: the current settings, below.

`settings` (in `/api/status`, writable with `POST /api/settings`):

| Key | Type, range | Meaning |
|---|---|---|
| `auto_open` | bool | Grid-EYE auto-open (same as `/api/auto`) |
| `auto_close` | bool | after a presence open, close once the doorway has been clear for `hold_s` (only while `auto_open` is on) |
| `obstruction_detection` | bool | Grid-EYE "occupied" blocks closing and reverses a close. Off leaves only the STM's motor-current limit (logged as a warning) |
| `hold_s` | 3..600 | seconds the doorway must be clear before an auto-close |
| `open_percent` | 15..95 | open position as % of full travel (13588 counts); 45.2 = the imp's "8 inch" |
| `open_inches` | 2.7..16.8 | the same, in inches of door opening (767 counts per inch; 8.0 = -6136) |
| `open_counts` | -12908..-2038 | the same, in encoder counts (exact) |
| `presence_threshold` | 0.3..10 | detector score that means "a dog wants out" (lower = more sensitive) |
| `occupied_threshold` | 0.2..5, <= presence | the safety level: doorway not clear |
| `noise_c` | 0.2..2 | per-pixel noise floor (°C) for the score |

Console only (`nc wayzn-esp.local 23`): calibration, fault clear, raw STM commands.

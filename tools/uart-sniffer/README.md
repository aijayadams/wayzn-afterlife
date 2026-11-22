# uart-sniffer

Two-channel UART sniffer for the **STM+IMP** header, on an ESP32 or ESP32-S3. The
base builds are listen-only (the UARTs are RX-only); the `-host` and `-net` builds can
also send to the STM32 (below).

## Wiring

| STM+IMP header pin | Classic ESP32 (`esp32dev`) | ESP32-S3 (`esp32s3`) |
|---|---|---|
| GND | GND | GND |
| RX → channel **A** | GPIO25 | GPIO4 |
| TX → channel **B** | GPIO26 | GPIO5 |
| 3V3 (square pad) | **not connected** | **not connected** |

Power the ESP32 from USB and the door board from its own 12 V. Join only the
grounds. If you scope at the same time, the scope's ground clip goes to header
GND only (a bench scope is earth-referenced).

## Build / flash / capture

```bash
pip install platformio             # or use the VS Code PlatformIO extension
cd tools/uart-sniffer
pio run -e esp32dev -t upload      # or -e esp32s3
pio device monitor -e esp32dev     # writes platformio-device-monitor-*.log here
```

Or use `python3 capture.py <event> --baud <baud>` (needs pyserial), which writes
`captures/YYYY-MM-DD_P1_<event>.log` at the repo root with host timestamps (create it
first with `mkdir -p ../../captures`; it is gitignored).
These logs are what `decode.py`, the firmware's host tests and the detector research tools
read.

## Procedure

1. **Scope first** (before wiring the ESP32): with the board powered, both RX
   and TX should idle at ~3.3 V. Trigger on a falling edge, power-cycle, and
   measure the narrowest low pulse: that's one bit. 8.68 µs = 115200,
   17.4 µs = 57600, 104 µs = 9600. Note which line talks first after
   pressing **EXP RST** (STM reset): that line is the STM's TX.
2. Wire the ESP32 and open the monitor. Type `l` to see both idle levels (expect 1).
   Note: the RX pins have internal pull-ups, so an **unconnected** pin also reads 1.
   The scope check in step 1 is what proves the wiring.
3. Type `m 5000` and power-cycle the door board within 5 s. That reports edges
   per line and a baud guess (above ~57600 the guess is rough; trust the scope).
4. `b <baud>` to set it. Frames should appear as hex + ASCII. Garbage on every
   byte usually means wrong baud; framing that looks inverted → `i`.
5. Record one log per event (power-up, STM reset, open, close, button, each settings
   change, calibration). Decode with `python3 decode.py <log>`.

## Bench vs. in-service

The bench is the safest place to start: power J7 from a 12 V bench supply (current
limit ~1 A) and leave the motor (J6) **unplugged**, so nothing can move. The STM then
reports the missing motor or encoder. Real open/close/motion needs the board in the
door.

## Output format

```
# wayzn uart-sniffer  A=GPIO25  B=GPIO26  baud=115200  invert=0  gap=300us
12.345678 A [5] 7F 01 02 03 0A  |.....|
```
seconds since boot, channel, byte count, hex, ASCII. Frames split on ~2
characters of idle. Timestamps are approximate (tens of µs).

## UART + I2C together (env `esp32dev-i2c`)

Same UART wiring, plus GPIO21 → TP1 (SCL) and GPIO22 → TP2 (SDA). Flash with
`pio run -e esp32dev-i2c -t upload`. The log then has `<secs> I <txn>` lines
interleaved with the UART frames (same device clock). `decode.py` shows
accelerometer reads as g and pitch/roll and hides Grid-EYE frames (add
`--grideye` to keep them).

## Talking to the STM32 (env `esp32dev-host`)

As `esp32dev-i2c`, plus transmit on pin A (header RX = STM32 USART1 RX). **Only with the imp
held in reset**: both would drive the same line. `pio run -e esp32dev-host -t upload`.

- `> <line>` sends `<line>\n` to the STM32. TX is attached on the first send (never at
  boot, so an unpowered board is never back-powered through the pin). Sent bytes loop
  back and show up as channel-A frames. Attaching TX makes one glitch byte; the firmware
  follows it with a bare `\n` so the STM drops it instead of spoiling the first command.
- Lines are checked against a no-motion allowlist (`kSafe` in src/main.cpp, from
  docs/STM-FIRMWARE.md). Anything else is refused with `# refused (disarmed)`.
- `arm` / `disarm` allow / forbid motion commands. Only `arm` with the doorway clear and
  someone at the door (AGENTS.md: the motor is strong enough to hurt).

`capture.py <event> --inbox FILE` forwards lines appended to FILE to the sniffer, so
commands can be sent during a capture: `echo '> version' >> FILE`.

## Wi-Fi console and OTA (env esp32dev-net)

Same as `esp32dev-host` (STM transmit, `arm`/`disarm`, safe list), without I2C sniffing,
plus Wi-Fi, a telnet console and OTA updates. See `src/net.h`.

1. `cp secrets.ini.example secrets.ini` and fill in the Wi-Fi and an OTA password
   (secrets.ini is gitignored).
2. First flash over USB: `pio run -e esp32dev-net -t upload`
3. After that, over Wi-Fi: `pio run -e esp32dev-net-ota -t upload` (hostname `wayzn-esp.local`).
4. Console: `nc wayzn-esp.local 23`, or log to `captures/` with
   `python3 capture.py <event> --port socket://wayzn-esp.local:23 [--inbox FILE]`.
   The logger reconnects after 15 s without data (e.g. after an OTA reboot).

- Network work runs in its own task on core 0; the sniffer loop only fills a buffer, so
  UART framing isn't disturbed. Telnet output that doesn't fit is dropped (counted in
  `?` as "telnet drops"); USB serial still gets everything.
- OTA is only accepted while **disarmed**. If the telnet client disconnects while
  armed, the link disarms itself (a move in progress still finishes; `> stop` aborts).
- Why no I2C here: the I2C poller disables interrupts on core 1, which would stall the
  flash writes an OTA update needs.

### STM reset line (GPIO27 → SW300 NRST pad)

The STM doesn't always start on a cold power-up with the ESP32 on the same rail (the imp
resets it). GPIO27 is open-drain on NRST: a 20 ms pulse ~2 s after boot, another if the
STM has been silent for 30 s (at most once a minute), and `sreset` by hand. `?` shows the
real line level: 1 while the STM runs. Other diagnostics: `fl [ms]` (UART pins with the
pull-ups off: a loose wire picks up noise) and `blprobe` (read-only STM bootloader probe).

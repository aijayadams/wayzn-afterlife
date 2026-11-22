# i2c-sniffer

Listen-only I2C sniffer for the Grid-EYE bus on the WYP-BC-100. Both pins are
inputs with no pull-ups, so it can't disturb the bus. Core 1 polls the GPIO
register at ~10 MHz and decodes START/bits/ACK/STOP; core 0 prints.

| ESP32 (classic) | Board |
|---|---|
| GPIO21 | TP1 (SCL) |
| GPIO22 | TP2 (SDA) |
| GND | Board GND (J105-4 or the header GND) |

Build/flash (same toolchain as uart-sniffer):

    pio run -e esp32dev -t upload

Log with the UART sniffer's logger (it sends a harmless `b 115200` first):

    python3 ../uart-sniffer/capture.py i2c-<event>

Output, one line per transaction: `<secs> I W69 80 | R69 <bytes...>`.
`W69`/`R69` = 7-bit address + direction, `|` = repeated START, `~` = NAK.

The decoder is `src/i2c_sniff.h`, shared with the UART sniffer: to sniff the
UART and I2C together, flash `uart-sniffer` env `esp32dev-i2c` instead.
Type `s` to collapse long reads (pixel frames) to `<n bytes sum=…>`.

The bus runs at SCL ≈ 100 kHz (scope).

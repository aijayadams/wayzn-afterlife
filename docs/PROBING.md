# Probing procedures

Results go in the "Result" line of each section, with a date and a link to
the capture in `captures/`. Pin numbers below are for the STM32F051K8T6
(LQFP32): NRST = 4, PA9/PA10 (USART1 TX/RX) = 19/20, PA13 SWDIO = 23,
PA14 SWCLK = 24, BOOT0 = 31. Check them against the datasheet before relying
on them.

Kit: logic analyzer (Saleae or similar, 3.3 V), USB-UART 3.3 V adapter,
multimeter, ST-Link V2/V3 (or J-Link) plus a Tag-Connect TC2030 cable if JP300
fits it, SOIC-8 clip with a CH341A/flashrom (3.3 V verified!) for U210.

## P1. STM+IMP UART (bench first, then in-service; passive)

Tool: [tools/uart-sniffer](../tools/uart-sniffer/) on an ESP32, plus the scope for baud/levels.

1. Board powered, header unpopulated. Measure each pin to GND: expect 3V3
   pin = 3.3 V, and both RX and TX idle high (~3.3 V).
2. Solder a 4-pin header (or hold pogo pins). Connect **GND + two analyzer
   channels** on RX and TX. Do not connect the adapter's TX or 3V3.
3. Capture from power-on for 2 minutes: boot chatter, version exchange
   (`STM_V` should appear, e.g. 480), periodic status.
4. Then capture each event separately, saving one file per event:
   - idle 60 s
   - HA/app `open`, then `close`
   - walk-by motion (dog or warm hand) → auto open
   - physical button press
   - obstruction (gently hold the panel while it closes)
   - a settings change (dog size, same value) and an `updateMask`
   - `auto_zero` (moves the door; only with the panel clear)
5. Decode: autobaud from the shortest bit (common: 115200, 57600, 9600), 8N1.
   Check whether it's ASCII, framed binary (sync byte + length + CRC) or the
   STM32 ROM bootloader protocol (0x7F autobaud, 0x79 ACK / 0x1F NACK, which
   would appear during an STM firmware update).
6. Record which side is which: the pin that talks when the STM is reset with
   EXP RST is the STM's TX.

Result: _pending_

## P2. Ownership map (spare board preferred; continuity, power off)

Beep each connector and test pad to the U300 pins and the imp004m pads, and
fill in the "To measure" checklist in [hardware/WYP-BC-100.md](hardware/WYP-BC-100.md).
Most important: IR SENSOR SCL/SDA, J5 SIG A/B, the DRV8828 control inputs
(STEP/DIR or PHASE/ENABLE, I0–I3 current DAC, nRESET, nSLEEP, nFAULT), and J105.

Result: _pending_

## P3. STM32 SWD: read only

1. Map the JP300 pads to SWDIO/SWCLK/NRST/3V3/GND (P2).
2. Connect the ST-Link with the board powered from its own 12 V supply (don't
   back-power through the ST-Link 3V3; use its VREF sense only).
3. `st-info --probe`, or OpenOCD `-f interface/stlink.cfg -f target/stm32f0x.cfg`,
   then `stm32f0x options_read 0`. Record the RDP level and option bytes.
4. RDP0: `st-flash read stm32_full.bin 0x08000000 0x10000` twice, compare
   hashes, save in `firmware-dumps/` (committed; the repo stays private). Note the sha256 here.
5. RDP1: **stop. Don't attempt an unlock** (it mass-erases). Record it here
   and move on; extraction attempts happen on the spare only.

Result (2026-11-21, **verified**): read with tools/swd-dump (bit-banged SWD on a
classic ESP32: GPIO18 → JP300-6 SWCLK, GPIO19 → JP300-1 SWDIO, GND → J105-4),
board on its own 12 V, imp held in reset, motor unplugged. DP IDCODE 0BB11477,
DBGMCU_IDCODE 20006440 (F05x rev 2000). **RDP level 0**, FLASH_OBR FFFFFF00,
option bytes 00FF55AA 00FF00FF 00FF00FF 00FF00FF. Two full reads identical:
`firmware-dumps/stm32f051-u300-2026-11-21.bin`, crc32 1EDDB71D, sha256
9848c2c8dec039ebcbcd3d3dea86acfdf835964979e16f530146dedbcbedbbd8. Initial SP
0x20002000, reset vector 0x08006E2D. Two similar images: 0x0000–~0x7E00
(running) and 0x8000–~0xC600, which points to an A/B update scheme (to analyse).
Nothing was written to the target.


## P4. Grid-EYE I2C sniff (in-service door, passive)

Clip the analyzer onto SCL/SDA at the IR SENSOR connector. Capture boot and a
motion event. Record the address, frame rate, register writes at init (0x00
PCTL, 0x02 FPSC, 0x07 MAMOD) and the pixel reads (0x80–0xFF). Decode one frame
to °C to sanity-check.

Result: _pending_

## P5. Motor and encoder (in-service door, passive)

- Analyzer on J5 SIG A/B during a full open: count edges and compare with
  `DoorOffset` (13572). That gives x1/x2/x4 decoding.
- Measure the motor winding resistance across J6 (power off, connector off).
- Optional: scope across R2 during a move to see current and stall
  behaviour.

Result: _pending_

## P6. SPI flash U210 (spare only, power off)

SOIC clip, `flashrom -p ch341a_spi -r u210_a.bin`, again to `u210_b.bin`,
compare. Run `binwalk -E` for entropy (encrypted or not). Low priority.

Result: _pending_

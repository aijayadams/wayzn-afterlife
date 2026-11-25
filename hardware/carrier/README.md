# Phase A carrier board v3 (WYP-BC-100 piggyback, parked)

A 4-layer board (53.8 x 33.4 mm) that replaces the hand-wired ESP32 DevKit. It sits parallel to the Wayzn
WYP-BC-100, above the STM32 end, inside one ~57 mm cell between the lid ribs. It plugs in: no screws, no
spacers, no soldering to test points.

Status: **laid out by hand and checked (ERC, DRC with schematic parity, courtyards, silkscreen: all clean); not
ordered.** The Wayzn-side positions come from the photos plus caliper readings, so check the 1:1 print before
ordering (see "Before ordering").

Pictures: `out/overlay-top.jpg` (the carrier and the lid ribs drawn on the Wayzn board photo), `out/render-top.png`,
`out/render-bottom.png`, `out/plan.png` (pads, nets and routes on a 1 mm grid).

## How it sits

- **x**: from 1 mm inside the lid rib beyond J105 (x -30) to 2 mm short of the rib that runs through the end
  posts (x 23.8). The Wayzn J2/J5 sockets (6.6 mm tall) stay uncovered past the carrier's end, so their cables
  plug in as before.
- **y**: within the Wayzn board's 34 mm width. Its top edge already touches the shell, so the carrier doesn't go
  past it.
- **Held by two connectors on its underside**:
  - **J1**: a round-hole 1x4 socket onto a round-pin 1x4 header soldered into the STM+IMP holes (standard square
    pins don't fit them). It carries 3V3, the STM UART and GND.
  - **J6**: a 2x4 2.0 mm male header plugged into the Wayzn **J105** socket. Mechanical only: every pin is
    unconnected on the carrier.
- **Stack height**: J105 plus J6's plastic sets the gap; the J1 socket and header heights have to match it.
  **J105's height still needs measuring** (`STACK_MM` in design.py is a placeholder 11 mm).

## What's on it

| Part | Why |
|---|---|
| **ESP32-C5-WROOM-1U-N8R8** (U1) | Main MCU: dual-band Wi-Fi 6 (2.4 + 5 GHz), BLE 5, 802.15.4; 8 MB flash + 8 MB PSRAM. U.FL (MHF I) antenna socket on the module: use a **2.4/5 GHz** antenna |
| **ESP32-H2-MINI-1U-H4S** (U2) | Thread/Zigbee radio, OpenThread RCP on UART; the C5 resets and reflashes it (EN, GPIO9). **W.FL / MHF III ("IPEX3")** antenna socket: 2.4 GHz antennas with that plug are sold as IPEX3/MHF3 (e.g. AliExpress, Ditronix, Adafruit 5445). Espressif certified it with an antenna of at most 2.33 dBi |
| **LIS2DH12** (U3) | Accelerometer for the drive-lever pose (the Wayzn board's KXTJ3 stopped answering). I2C 0x19 (SA0 open, on its internal pull-up; firmware must not disable it) |
| J1 1x4 round-hole socket (underside) | Onto a round-pin header in the Wayzn **STM+IMP** holes: 3V3, STM TX, STM RX, GND |
| J6 2x4 2.0 mm header (underside) | Into the Wayzn J105: second anchor, mechanical only |
| J2 / J3 JST PH 4-pin, top entry | I2C pass-through. The Grid-EYE cable moves from the Wayzn J2 to J3. A straight PH-PH cable runs from J2 to the Wayzn J2 (same pin order: GND, SCL, SDA, 3V3). Brings the bus and the Wayzn board's 4.7k pull-ups |
| J4 JST PH 2-pin | STM32 NRST: one wire from SW300's top-right pad (the only hand-soldered joint on the Wayzn board, as today) |
| **J5 1x6 2.54 mm, RESET and BOOT buttons** | Programming, see below |
| JP1 (solder jumper, open) | Connects J5 pin 1 to 3V3 for a bench flash only |
| D1 | Status LED (GPIO3) |

## Programming (first flash; OTA after that)

J5, top to bottom (printed beside each pin): **3V3\*, GND, EN, TXO, RXI, BOOT**. Any 3.3 V USB-serial adapter:
adapter GND to GND, adapter RX to TXO, adapter TX to RXI. Either let esptool drive EN/BOOT through DTR/RTS, or
by hand: hold **BOOT**, tap **RESET**, release BOOT, flash.

- **On the door** (normal): the door powers the board. Leave the adapter's 3V3 unconnected; JP1 stays open, so
  J5 pin 1 isn't connected to anything.
- **On the bench** (board off the door): bridge JP1 with solder, power the board from the adapter's 3V3 on J5
  pin 1. Remove the bridge before fitting it to the door, so the adapter and the door's rail can never meet.
- The H2 needs no programming access of its own: the C5 holds its BOOT strap low, resets it and flashes it over
  their UART (esp-serial-flasher), like Espressif's Thread Border Router board.

## Power

All from the Wayzn 3V3 rail (J1 and J2). The old ESP32 ran from it at full Wi-Fi load with no brownouts
(measured on the door, 2026-11-21); the C5 datasheet asks for a 600 mA supply (H2: 350 mA), so **check the rail under C5 +
H2 load**. Bulk 22 uF at entry, 22 uF + 100 nF at the C5, 10 uF + 100 nF at the H2. EN delays 10k/1u on both
chips; the firmware should also wait ~1 s before starting Wi-Fi.

## Pin map (C5)

| GPIO (module pin) | Net | Notes |
|---|---|---|
| EN (3) | C5_EN | 10k/1u, RESET button, J5 |
| 2 (4) | STM_NRST | open-drain, 100 R, J4 |
| 3 (5) | LED_STATUS | 1k. Strap MTDI: SDIO timing only, unused here |
| 9 (11) | H2_BOOT | open-drain to H2 GPIO9 (10k pull-up) |
| 10 (12) | H2_EN | open-drain to H2 EN (10k/1u) |
| 28 (15) | C5_BOOT | strap: 10k pull-up, BOOT button, J5 |
| 5 (16) / 4 (17) | H2_TXD / H2_RXD | from H2 U0TXD (GPIO24) / to H2 U0RXD (GPIO23) |
| 27 (18) | ACC_INT1 | strap: LIS2DH12 INT1 idles low, which only selects UART-download variant 1 over 0 |
| 23 (21) / 24 (23) | I2C_SCL / I2C_SDA | Grid-EYE 0x69, LIS2DH12 0x19, (dead KXTJ3 0x0F) |
| 12 (24) / 11 (25) | C5_RXD0 / C5_TXD0 | J5 |
| 25 (26) | STM_TX | UART1 RX from STM32 PA9, 100 R. Strap: SDIO timing only |
| 26 (27) | STM_RX | UART1 TX to STM32 PA10, 100 R |
| 0, 1, 6, 7, 8, 13, 14 | unused | module pin 19 (PSRAM chip select) must stay unconnected |

H2: GPIO8 10k pull-up (download mode needs it high); VBAT tied to 3V3.

## Layout

Placement, every signal route, and every silkscreen label are written out in `layout.py` (coordinates in the
Wayzn board's frame, so the carrier lines up with the board under it). Floorplan: programming zone on the left
(J5 on the edge, RESET above BOOT, the bench-3V3 jumper, J6 underneath); the C5 in the middle (left-edge pins
toward the programming zone, right-edge pins toward J1, the I2C parts and the H2); along the top edge, the status
LED, J4 (NRST) and J2; J3 on the right end; the accelerometer between J1 and J3; the H2 in the bottom-right
corner, rotated 180 so its UART faces the C5. Pins were chosen so no two signals cross. Each power pad gets its
own via to the In1 (GND) / In2 (3V3) plane. The LIS2DH12 (0.5 mm pitch) is wired to its decoupling caps instead.
GND stitching vias on a 3 mm grid.

Stackup: F.Cu signals + GND pour, In1 GND, In2 3V3, B.Cu signals + GND pour. 0.12 mm clearance (the LIS2DH12's
pads leave exactly 0.15 mm), 0.2 mm signal / 0.3 mm power tracks, 0.6/0.3 mm vias (JLC 4-layer minimum 0.09 mm).

Silkscreen: only the labels in `layout.SILK`: J5's pin names (one beside each pin), RESET/BOOT, the jumper, the
connectors, and a short how-to on the back. Footprint silk that would leave the board or run into a label is
dropped. Part values aren't drawn, and references stay on the assembly layer. The build fails if a label leaves
the board or touches a pad, via or another label.

**Don't edit `carrier.kicad_pcb` in KiCad and expect it to stick: `build.sh` regenerates it from `layout.py`.**
Change `layout.py` (or `design.py`) and rebuild.

## Files

- `design.py`: parts, nets, LCSC numbers, mechanics, rules. `layout.py`: placement, routes, silkscreen.
- `build.sh`: regenerates everything: `mk_carrier_sym.py` → `lib/Carrier.kicad_sym`; `gen_sch.py` →
  `carrier.kicad_sch`; `gen_pcb.py` (+ `route.py` for fanout and stitching) → `carrier.kicad_pcb`; ERC; DRC with
  schematic parity; `fab.py` → `fab/`. Needs KiCad 10.
- `fab/`: `carrier-gerbers.zip`, `carrier-bom.csv`, `carrier-cpl.csv` (JLC formats).
- `padmap.py`, `planplot.py`: pad coordinates and `out/plan.png`, for working on routes. `overlay.py` →
  `out/overlay-top.jpg`. `out/fit-check-1to1.pdf`: print at 100 % and lay it on the real board.
- `lib/Espressif.*`: vendored from espressif/kicad-libraries (CC-BY-SA 4.0 with the KiCad design exception).

## BOM and sourcing (checked 2026-11-23/24, mostly from search snapshots: confirm live before ordering)

| Ref | Part | LCSC | Notes |
|---|---|---|---|
| U1 | ESP32-C5-WROOM-1U-N8R8 | C51950748 | LCSC 0 (live read). Buy from Mouser |
| U2 | ESP32-H2-MINI-1U-H4S | C49450282 | DigiKey/Mouser hundreds. (The -H4 is not recommended for new designs) |
| U3 | LIS2DH12TR | C110926 | Patchy stock. Optional: the board works without it |
| SW1, SW2 | XKB TS-1187A-B-A-B | C318884 | JLC Basic |
| D1 | XL-1608UGC-04 green 0603 | C965804 | |
| J2, J3 | JST B4B-PH-K-S (4-pin PH, top entry) | C131334 | |
| J4 | JST B2B-PH-K-S | C131337 | |
| J6 | HCTL PZ200-2-04-Z, 2x4 2.0 mm straight | C2905990 | alt C225289 (check it's straight). Hand-solder on the underside |
| J1 | 1x4 2.54 round-hole (machined) socket | (C7495238?) | Mill-Max 310-43-104-41-001000 or CONNFLY C7495238. **Height to pick once J105 is measured**. Hand-solder on the underside |
| J5 | 1x6 2.54 male header | | cut from a strip; hand-solder |
| R, C | 0402/0603/0805 | C25744, C25076, C11702, C1525, C52923, C19702, C45783 | all JLC Basic |

Also: a round-pin 1x4 header for the Wayzn STM+IMP holes (Mill-Max 350-10-104-00-006000, 0.46 mm pins); a
straight 4-pin PH-PH cable, 100-150 mm (Adafruit 3568 or Pololu 5623/5624: check pin 1 to pin 1 with a meter); a
2-pin PH lead for NRST; antennas: 2.4/5 GHz U.FL (C5), 2.4 GHz MHF III (H2). Full list: `fab/order-bom.csv`.

## Before ordering

0. **Strapping fix needed (found 2026-11-24 on the control board):** UART download needs GPIO27 high when BOOT
   holds GPIO28 low, but here GPIO27 is the accelerometer's INT1, which idles low. Move a pulled-up signal
   (e.g. I2C SDA) onto GPIO27 before ordering, as hardware/control does.

1. **Measure J105's height** above the Wayzn board. It sets the stack, so pick the J1 socket and round-pin header
   heights to match, and check the stack clears everything underneath (U300, SW300, the imp module's edge).
2. Print `out/fit-check-1to1.pdf` at 100 % and lay it on the board: J1 over the STM+IMP holes, J6 over J105, both
   ends inside the lid ribs. The Wayzn landmarks are drawn on it.
3. Check the Grid-EYE cable reaches J3; if not, add a PH extension.
4. Firmware: port to the C5 (Arduino-ESP32 3.x / pioarduino; single core), pin map above, H2 RCP flashing.

# Ordering: wayzn-afterlife carrier v3 (5 boards, assembled at home)

The owner assembles everything: top-side SMD parts with a stencil, solder paste and a hotplate; through-hole parts
with an iron afterwards. So the order is **bare PCBs + a stencil** from JLCPCB, **loose parts** from LCSC, the two
**ESP32 modules** from Mouser/DigiKey, and a few off-board items.

| File | What |
|---|---|
| `order-bom.csv` | **Everything to buy**, one line per item, quantities for 5 boards (spares included for small parts), manufacturer, MPN, LCSC number, source. Other counts: `python3 order.py <boards>` |
| `carrier-gerbers.zip` | PCB + stencil fabrication files (Gerbers, drill, top paste layer) |
| `carrier-bom-jlc.csv` + `carrier-cpl-jlc.csv` | **If JLC assembles the small parts**: resistors, capacitors, both buttons, the LIS2DH12 only (all survive the later hotplate pass for the modules). Not on it: U1/U2 (modules), D1 (LED, hotplated at home with the modules), J2/J3/J4 (through-hole, iron), J1/J5/J6 (hand-soldered) |
| other `carrier-bom*.csv` / `carrier-cpl*.csv` | Other combinations, kept for reference: pair a BOM with the CPL of the same suffix |

## Gate: don't order the PCB/stencil yet

The fit is photo-measured plus calipers. **Parts can be ordered now** (some have long lead times), except the J1
socket and the round-pin header, whose heights depend on J105's height (not measured yet). **The PCB and stencil
wait** for that measurement and for the 1:1 fit-check printout (`../out/fit-check-1to1.pdf`) laid on the board.

## 1. Order now

- **ESP32-C5-WROOM-1U-N8R8** (Espressif) x5 from **Mouser** (LCSC showed 0; DigiKey ~20-week lead time). Exactly
  this part: `-1U-` (antenna connector), not `-1-` (PCB antenna).
- **ESP32-H2-MINI-1U-H4S** (Espressif) x5 from DigiKey or Mouser. `-H4S`, not the older `-H4`; `-1U-`.
- **LCSC, loose parts** (every line marked `LCSC` or `IRON` with an LCSC number in `order-bom.csv`): round each
  quantity up to LCSC's minimum order quantity (0402/0603 parts often come in 50/100s; that's fine, they're
  cents). The LIS2DH12TR (C110926, x7) is the one to watch: if LCSC is short, buy it from DigiKey/Mouser.
  The LCSC order can also be combined into the JLC order's shipment later.
- **Off-board items** (lines marked `EXTRA`): a 1x40 2.54 mm breakaway male header (J5); straight 4-pin JST PH
  cables, female both ends, 100-150 mm, x5 (Adafruit 3568 or Pololu 5623/5624; check pin 1 to pin 1 with a
  meter); JST PH 2-pin single-ended leads x5; 2.4/5 GHz U.FL antennas x5 (Molex 146187-0100 or equivalent);
  2.4 GHz **MHF III / W.FL ("IPEX3")** antennas, gain <= 2.33 dBi, x5 (AliExpress, or Adafruit 5445); a 3.3 V
  USB-serial adapter with DTR/RTS; solder paste **Type 4** (Sn63/Pb37 such as Chip Quik SMD291AX10T4, or
  SAC305), and no-clean tacky flux.
- **After J105 is measured**: the round-pin 1x4 header for the Wayzn STM+IMP holes (Mill-Max
  350-10-104-00-006000) and the round-hole 1x4 socket for J1 (Mill-Max 310-43-104-41-001000 or LCSC C7495238),
  chosen so header + socket = the J105 + J6 stack.

## 2. After the gate: PCB + stencil at JLCPCB

**PCB** (upload `carrier-gerbers.zip`, no assembly):

| Setting | Value |
|---|---|
| Layers | 4 (stackup JLC default, e.g. JLC04161H-7628) |
| Size | 53.8 x 33.4 mm (from the Gerbers) |
| Thickness | 1.6 mm |
| Surface finish | **ENIG** (flat pads for stencil printing the 0.5 mm-pitch LGA and the castellated modules) |
| Copper | 1 oz outer, 0.5 oz inner (standard) |
| Min track / clearance used | 0.15 mm / 0.12 mm (standard 4-layer capability) |
| Vias | 0.3 mm drill / 0.6 mm pad, tented |
| Order number on board | "Remove order number" if offered (no spot reserved on the silkscreen) |
| Quantity | 5 |
| PCB assembly | **No** |

**Stencil** (same order): **top side only**, stainless, 0.12 mm thick (suits the 0.5 mm-pitch LGA and the 0402s),
electropolished if offered; frameless is fine.

## 3. Assembly at home (owner)

1. Bake the ESP32 modules (MSL 3) if their bag has been open more than about a week.
2. Stencil-print paste on the top side; place every top-side SMD part from `order-bom.csv` (watch D1's polarity,
   U3's pin-1 dot, the module orientations against the silkscreen/assembly drawing).
3. Hotplate reflow. The bottom side has no SMD parts, so nothing falls off.
4. Iron: J2, J3 (JST PH 4-pin) and J4 (JST PH 2-pin) on top; J5 (1x6 cut from the header strip). Then, after a
   dry fit on the door: J6 (2x4 2.0 mm) and J1 (round-hole socket) on the underside, and the round-pin 1x4 into
   the Wayzn STM+IMP holes (plug the carrier on while soldering it so everything lines up). Plastic housings
   never go on the hotplate.
5. Leave JP1 open (bridge it only for a bench flash off the door).

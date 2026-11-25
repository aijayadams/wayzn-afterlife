# Ordering: wayzn-afterlife control board v1 (5 boards, assembled at home)

Same plan as the carrier: **bare PCBs + a top stencil** from JLCPCB, **loose parts** from LCSC, the **ESP32
modules** from Mouser/DigiKey, top-side SMD reflowed on the owner's 200 x 200 mm hotplate, through-hole parts by
iron afterwards.

| File | What |
|---|---|
| `order-bom.csv` | **Everything to buy**, quantities for 5 boards with spares (`python3 ../order.py <boards>` for other counts) |
| `control-gerbers.zip` | PCB + stencil files |
| `control-bom-jlc.csv` + `control-cpl-jlc.csv` | Only if JLC places the small parts: everything but the modules and the LED. Pair BOM/CPL files with the same suffix |

## Before ordering the PCB

1. Print `../out/fit-check-1to1.pdf` at 100 % and lay it on the Wayzn board: the four holes, J6/J7, the button pads
   and J2/J5 should line up (the drawing marks Wayzn's positions from the scan).
2. Light pipe: decide whether to reuse Wayzn's (if it comes off cleanly) or buy one; check the LED (D3) sits under
   its input.
3. Firmware: the motor control described in docs/CONTROL-BOARD.md has to exist before this board can run a door.
   The board can be brought up on the bench first (bring-up plan in that doc).

## PCB settings (JLCPCB)

| Setting | Value |
|---|---|
| Layers | 4 (JLC default stackup) |
| Size | 33.94 x 152.6 mm |
| Thickness | 1.6 mm |
| Surface finish | ENIG (flat pads for the 0.5 mm-pitch LGA and the HTSSOP thermal pad) |
| Copper | 1 oz outer (the motor tracks are 0.8 mm for 1.1 A), 0.5 oz inner |
| Min track / clearance | 0.15 / 0.12 mm; vias 0.3/0.6 mm (motor 0.4/0.8): smallest hole 0.3 mm |
| Min via hole size | **0.3mm/(0.4/0.45mm)** (the default; a 0.2 mm option roughly doubles the price) |
| Via covering | Plugged (default): keeps solder from wicking down the DRV8876 thermal-pad vias |
| Quantity | 5 |
| Assembly | No |

**Stencil:** top only, 0.12 mm, electropolished. The board is 152.6 mm long: use a frame, or a jig of spare boards.

## Assembly at home

1. Bake the ESP32 modules if their bag has been open more than about a week (MSL 3).
2. Paste (Type 4), place every top SMD part: the DRV8876's thermal pad gets paste; watch U3's pin-1 dot, D3's
   (RGB LED) pin-1 corner, D1/D2 cathode bands, C1's polarity.
3. Hotplate reflow (the board fits the 200 x 200 mm plate; nothing on the bottom).
4. Iron: J6, J7 (VH), SW3 (door button), J1 (1x6 cut from a strip). Leave JP1 open.
5. Bench bring-up before the door: see docs/CONTROL-BOARD.md.

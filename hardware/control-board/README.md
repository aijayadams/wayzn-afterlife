# Phase B: wayzn-afterlife control board v1 (WYP-BC-100 replacement)

A drop-in replacement for the Wayzn WYP-BC-100 control board: same 33.94 × 152.6 mm outline, the same four post
holes, and J6/J7/J2/J5 and the door button where Wayzn's are, so the door's cables, the shell's posts, the
button cap and the light pipe all line up. The ESP32-C5 drives the motor itself (DRV8876); there is no STM32 and
no imp. Design notes: [docs/CONTROL-BOARD.md](../../docs/CONTROL-BOARD.md). Board geometry:
[docs/hardware/WYP-BC-100-MECHANICAL.md](../../docs/hardware/WYP-BC-100-MECHANICAL.md).

Status: **laid out by hand and checked (ERC, DRC with schematic parity, courtyards, silkscreen: all clean); not
ordered.** Every part has an LCSC number (checked against JLC's library 2026-11-24). The original board stays in
the drawer: going back is just moving the connectors. Ordering: `fab/ORDER.md`.

Pictures: `out/overlay-scan.jpg` (this board's parts on the scan of the Wayzn board), `out/render-top.png`,
`out/render-bottom.png`, `out/plan.png` (pads, nets and routes, 1 mm grid). `out/fit-check-1to1.pdf`: print at
100 % and lay it on the Wayzn board.

## What's on it

| Ref | Part | Notes |
|---|---|---|
| U1 | ESP32-C5-WROOM-1U-N8R8 | Wi-Fi 6 (2.4 + 5 GHz), BLE; runs the door and the motor. U.FL antenna |
| U2 | ESP32-H2-MINI-1U-H4S | Thread RCP; the C5 resets and reflashes it. MHF III antenna |
| U5 | TI DRV8876PWPR | Motor H-bridge. PH/EN mode; IMODE tied low = fixed off-time current chopping, auto-retry, and nFAULT only on real faults (overcurrent, thermal, undervoltage), not on every chop; current mirror on IPROPI |
| U4 + L1 | TI TPS54202 + 10 uH | 12 V → 3.3 V buck, 2 A, 28 V input (above the TVS clamp) |
| D1, D2 | SS54, SMBJ15A | Reverse-polarity diode; 12 V TVS (clamps at 24.4 V) |
| U3 | LIS2DH12 | Accelerometer (optional) |
| J7 / J6 | JST VH 2-pin side entry | 12 V in / motor, in Wayzn's positions (pitch measured 3.96 mm on the scan) |
| J5 / J2 | JST PH 4-pin SMD top entry | Encoder / Grid-EYE: Wayzn pin order, orientation (pin 1 toward the middle) and housing positions |
| SW3 | C&K PTS645VL58 right-angle | The door button, on Wayzn's exact pads (same footprint), so the shell's cap fits its plunger |
| D3 + U6 | WS2812B-2020-V6 RGB LED + HT7536-1 3.6 V LDO | Addressable status LED under the shell's light pipe, one data line from the C5 (RMT). Its own 3.6 V supply from VM keeps it inside its 3.3–5.3 V rating; its data threshold (0.55 × VDD = 2.04 V worst case) leaves 1.2 V margin from 3.3 V logic. Limit brightness in firmware: the LDO dissipates ~0.3 W at full white |
| H1–H4 | NPTH 4.3 mm | The shell posts (Wayzn's holes are ~4.0; 0.3 mm slack for the scan's scale error) |
| R27, R28 | 100 k, 0 Ω (not fitted) | LED data pull-down (no random colour during reset); R28 is a fallback that runs the LED from 3V3 instead of U6 |
| J1, SW1, SW2, JP1 | 1x6 header, RESET, BOOT, bench-3V3 jumper | Programming, as on the carrier |

## Motor drive and safety (hardware side)

- **Current cap: 1.10 A** in hardware: VREF = 3V3, R_IPROPI = 3.0 k (ITRIP = 3.3 V / (1000 uA/A × 3.0 k)). Wayzn
  caps at 1.03 A; normal moves draw ~0.4 A and the most seen was 0.75 A. Firmware can't exceed it.
- **Motor off by default:** nSLEEP, EN and PH have 10 k pull-downs, and none of them is on a C5 strapping pin, so
  reset, boot, flashing or a brownout can't move the motor.
- **Current sense:** IPROPI → 1 k / 10 nF → C5 ADC (GPIO6), **3.0 V/A**; readings flatten near the cap. 12 V sense:
  100 k / 10 k → GPIO1 (÷ 11).
- **nFAULT** (overcurrent, thermal, undervoltage) → GPIO7 with a 10 k pull-up.

## C5 pin map

| Module pin (GPIO) | Net | Notes |
|---|---|---|
| 3 (EN) | C5_EN | 10 k / 1 u, RESET, J1 |
| 4 (2) / 5 (3) | H2_BOOT / H2_EN | open-drain to the H2 |
| 7 (1) | VM_ADC | ADC1_CH0, 12 V ÷ 11 |
| 8 (6) | IPROPI_ADC | ADC1_CH5, motor current 3.0 V/A |
| 9 (7) | DRV_NFAULT | input, 10 k pull-up; also an MCPWM fault input. Strap (JTAG source, no internal pull): the pull-up defines it |
| 10 (8) / 11 (9) / 12 (10) | DRV_NSLEEP / DRV_PH / DRV_EN | 10 k pull-downs; EN = PWM (MCPWM) |
| 13 (13) / 14 (14) | H2_TXD / H2_RXD | H2 UART (C5 RX / TX); native USB not used |
| 15 (28) | C5_BOOT | strap, BOOT, J1 |
| 16 (5) / 17 (4) | ENC_B / ENC_A | PCNT quadrature, 10 k pull-ups + 100 R / 1 nF |
| 18 (27) | I2C_SDA | strap: must be high at reset for UART download (Joint Download Boot 0); the 4.7 k pull-up does it |
| 21 (23) | ACC_INT1 | |
| 23 (24) | I2C_SCL | Grid-EYE + LIS2DH12 share the bus, 4.7 k pull-ups |
| 24 (12) / 25 (11) | C5_RXD0 / C5_TXD0 | J1 |
| 26 (25) / 27 (26) | LED_STATUS / BTN | WS2812 data (RMT); button 10 k pull-up + 100 n |
| 6 (0), 19 (15) | spare / PSRAM CS (must stay unconnected) | |

## Programming

As on the carrier: J1 (pin names on the back) 3V3\*, GND, EN, TXO, RXI, BOOT. Hold BOOT, tap RESET, flash. J1
pin 1 is connected only if JP1 is bridged: bench flashing with the board out of the door only.

## Layout

Placement, every route and every silkscreen label are in `layout.py`, in the scan frame (component side, J6/J7 end
at the top). Power and motor section at the J6/J7 end (0.8 mm tracks for 12 V and the motor; OUT1 returns on the
bottom layer through 0.8/0.4 vias). The C5 in the middle; its left pins run to the driver in one uncrossed lane (pull
resistors as tees at 2 mm pitch), its right pins go up to the button, LED and J1 and down to the encoder, the
accelerometer and the Grid-EYE. The H2 below it, programming buttons lower left. 4 layers: F signals + GND,
In1 GND, In2 3V3, B signals + GND; GND stitching on a 3 mm grid.

KiCad reports courtyard overlaps as warnings: `gen_pcb.check_overlaps` checks them instead, with two documented
exceptions (the VH housings 0.84 mm apart, as on Wayzn's board; the posts have no screw heads).

**Don't edit `control.kicad_pcb` in KiCad and expect it to stick:** `build.sh` regenerates it. Change `design.py`
or `layout.py` and rebuild. Needs KiCad 10.

## Files

`design.py` (parts, nets, rules), `layout.py`, `build.sh`, `gen_sch.py`, `gen_pcb.py`, `route.py` (fanout,
stitching), `mk_sym.py` (LIS2DH12, DRV8876 symbols), `fab.py` (JLC Gerbers/BOM/CPL), `order.py`
(`fab/order-bom.csv`), `padmap.py`, `planplot.py`, `overlay.py` (needs `out/boxes.json`, see its docstring).

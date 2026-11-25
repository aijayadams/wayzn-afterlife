#!/usr/bin/env python3
"""Purchasing BOM for N carrier boards -> fab/order-bom.csv (one line per thing to buy).
On-board parts come from design.py (quantities are exact); off-board items (cables, header strips, antennas)
are listed in EXTRAS. Run: python3 order.py [boards]   (default 5)"""
import csv
import math
import os
import sys

import design

HERE = os.path.dirname(os.path.abspath(__file__))
BOARDS = int(sys.argv[1]) if len(sys.argv) > 1 else 5

# Manufacturer + MPN for each LCSC number (checked on jlcpcb.com/partdetail, 2026-11-23).
MFR = {
    "C51950748": ("Espressif", "ESP32-C5-WROOM-1U-N8R8"),
    "C49450282": ("Espressif", "ESP32-H2-MINI-1U-H4S"),
    "C110926": ("STMicroelectronics", "LIS2DH12TR"),
    "C318884": ("XKB Connection", "TS-1187A-B-A-B"),
    "C965804": ("XINGLIGHT", "XL-1608UGC-04"),
    "C131334": ("JST", "B4B-PH-K-S(LF)(SN)"),
    "C131337": ("JST", "B2B-PH-K-S(LF)(SN)"),
    "C2905990": ("HCTL", "PZ200-2-04-Z"),
    "C25744": ("UNI-ROYAL", "0402WGF1002TCE"),
    "C25076": ("UNI-ROYAL", "0402WGF1000TCE"),
    "C11702": ("UNI-ROYAL", "0402WGF1001TCE"),
    "C1525": ("Samsung", "CL05B104KO5NNNC"),
    "C52923": ("Samsung", "CL05A105KA5NQNC"),
    "C19702": ("Samsung", "CL10A106KP8NNNC"),
    "C45783": ("Samsung", "CL21A226MAQNNNE"),
}
# Where to get it and how it goes on: everything is assembled at home (stencil + paste + hotplate for the top-side
# SMD parts, iron for the through-hole ones).
#   "LCSC"  loose parts from LCSC (same numbers as the JLC library; can ship with the JLC board order)
#   "BUY"   from the distributor named (not in LCSC stock)
#   "IRON"  through-hole, soldered with an iron after reflow
ROUTE = {
    "C51950748": ("BUY", "Mouser or DigiKey (LCSC showed 0). MSL 3: bake before reflow if the bag has been open > 1 week"),
    "C49450282": ("BUY", "DigiKey or Mouser. MSL 3: bake before reflow if the bag has been open > 1 week"),
    "C110926": ("LCSC", "2x2 mm LGA: stencil + paste, reflow. Stock is patchy: if LCSC is short, DigiKey/Mouser"),
    "C2905990": ("IRON", "2x4 2.0 mm straight header, underside, into the Wayzn J105 (mechanical anchor): solder "
                         "after the fit check. Alt LCSC C225289 (CJT A2005WV-2x4P, check it's straight)"),
    "C965804": ("LCSC", "LED: hotplated at home with the modules (not in the JLC placement); watch the polarity"),
    "C131334": ("IRON", "JST PH 4-pin top entry: through-hole, plastic housing: iron after reflow, never on the "
                        "hotplate"),
    "C131337": ("IRON", "through-hole, plastic housing: iron after reflow, never on the hotplate"),
}
SPARE = {"R": 2, "C": 2, "D": 2, "SW": 2}  # extra small parts per order (loose, for hand rework)

# Off-board / hand-assembly items, per board unless noted. (description, qty per board, manufacturer, MPN, source, notes)
EXTRAS = [
    ("PCB, 4 layers, bare (no assembly): see ORDER.md for the settings", 1, "JLCPCB", "carrier-gerbers.zip", "JLCPCB",
     "minimum order is 5"),
    ("SMT stencil for the top side (F.Paste), stainless 0.12 mm, electropolished, with the PCB order", 0, "JLCPCB",
     "from carrier-gerbers.zip (top paste only)", "JLCPCB", "one per order. Frameless is fine with a jig of spare boards"),
    ("Solder paste, Sn63/Pb37 (or SAC305), Type 4 (fine: 0.5 mm-pitch LGA), syringe or small jar", 0, "e.g. Chip Quik",
     "SMD291AX10T4 (Sn63/Pb37 T4) or equivalent", "DigiKey / Mouser / Amazon", "one per order; keep refrigerated"),
    ("No-clean tacky flux", 0, "e.g. Chip Quik", "SMD291 / any no-clean tacky flux", "DigiKey / Amazon",
     "one per order, for touch-up and the through-hole parts"),
    ("1x40 2.54 mm male breakaway header (square pins)", 1 / 6, "Sullins", "PRPC040SAAN-RC", "DigiKey/Mouser/LCSC",
     "cut a 1x6 for J5 per board; 1 strip covers 6 boards"),
    ("1x4 2.54 mm ROUND-pin (machined) male header, into the Wayzn STM+IMP holes", 1, "Mill-Max",
     "350-10-104-00-006000 (0.46 mm pins)", "DigiKey (restock ~22 Dec) / LCSC C3331242",
     "square pins don't fit those holes. Generic round-pin strip LCSC C9742 is cheaper but its pin size is unstated"),
    ("1x4 2.54 mm ROUND-hole (machined) socket, carrier J1 (underside)", 1, "Mill-Max",
     "310-43-104-41-001000 (or LCSC C7495238 CONNFLY DS1002-03-1x4131)", "Future / LCSC",
     "HEIGHT TBD: socket + round-pin header must equal the J105 + 2x4 header stack. Pick after measuring J105"),
    ("JST PH 4-pin cable, female both ends, straight (pin 1 to pin 1), ~100-150 mm", 1, "Adafruit",
     "3568 (150 mm) or Pololu 5623/5624 (100/160 mm)", "DigiKey / Adafruit / Pololu",
     "carrier J2 to the Wayzn J2 (IR SENSOR). Check pin 1 to pin 1 with a meter before plugging in"),
    ("JST PH 4-pin extension for the Grid-EYE, only if its cable no longer reaches carrier J3", 0, "TBD",
     "depends on the Grid-EYE end's connector", "TBD", "check the existing cable's reach first"),
    ("JST PH 2-pin cable, female one end, bare/tinned other end, ~100 mm", 1, "generic", "PH 2.0 mm 2P single-head",
     "AliExpress / Amazon", "carrier J4 to the SW300 NRST pad (one wire; GND pin unused at the door end)"),
    ("Antenna, 2.4/5 GHz dual-band, U.FL (MHF I) plug, flex/PCB type, ~100 mm cable", 1, "Molex", "146187-0100",
     "DigiKey/Mouser", "for the ESP32-C5. Any 2.4/5 GHz U.FL antenna works; keep gain modest"),
    ("Antenna, 2.4 GHz, W.FL / MHF III ('IPEX3') plug, gain <= 2.33 dBi", 1, "generic", "IPEX3 / MHF3 2.4 GHz FPC antenna",
     "AliExpress (or Adafruit 5445)", "for the ESP32-H2. Espressif certified the module with <= 2.33 dBi"),
    ("3.3 V USB-serial adapter (CP2102 / CH340 / FTDI), with DTR+RTS", 0, "generic", "any 3.3 V logic", "Amazon / AliExpress",
     "programming tool: one per order, not per board"),
]


def main():
    parts = design.all_parts()
    lines = {}
    for ref, p in parts.items():
        if p["dnp"] or ref.startswith(("JP", "TP")):
            continue
        key = p["lcsc"] or f'{p["value"]}|{p["footprint"]}'
        e = lines.setdefault(key, {"refs": [], "value": p["value"], "footprint": p["footprint"].split(":")[1],
                                   "lcsc": p["lcsc"], "notes": p["notes"]})
        e["refs"].append(ref)
    rows = []
    for key, e in lines.items():
        n = len(e["refs"])
        kind = "".join(c for c in e["refs"][0] if c.isalpha())
        total = n * BOARDS + SPARE.get(kind, 0) + (2 if e["lcsc"] == "C110926" else 0)  # LIS2DH12: 2x2 LGA, spares
        mfr, mpn = MFR.get(e["lcsc"], ("", e["value"]))
        route, note = ROUTE.get(e["lcsc"], ("LCSC", "round up to LCSC's minimum order quantity"))
        if e["refs"] in (["J5"], ["J1"]):
            continue  # J5: cut from the header strip; J1: the round-hole socket (both in EXTRAS)
        rows.append([", ".join(sorted(e["refs"])), n, total, e["value"], e["footprint"], mfr, mpn, e["lcsc"], route, note])
    rows.sort(key=lambda r: ({"BUY": 0, "LCSC": 1, "IRON": 2}[r[8]], r[0]))
    for desc, per, mfr, mpn, src, note in EXTRAS:
        total = max(1, math.ceil(per * BOARDS)) if per else 1
        rows.append(["(off-board)", round(per, 2) if per else "", total, desc, "", mfr, mpn, "", "EXTRA: " + src, note])
    out = os.path.join(HERE, "fab/order-bom.csv")
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Refs", "Qty per board", f"Qty to order ({BOARDS} boards)", "Value / description", "Footprint",
                    "Manufacturer", "MPN", "LCSC", "How", "Notes"])
        w.writerows(rows)
    print("wrote", out, len(rows), "lines")


if __name__ == "__main__":
    main()

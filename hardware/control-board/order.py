#!/usr/bin/env python3
"""Purchasing BOM for N control boards -> fab/order-bom.csv (one line per thing to buy).
On-board parts come from design.py (exact quantities); off-board items are in EXTRAS.
Run: python3 order.py [boards]   (default 5)"""
import csv
import math
import os
import sys

import design

HERE = os.path.dirname(os.path.abspath(__file__))
BOARDS = int(sys.argv[1]) if len(sys.argv) > 1 else 5

# Manufacturer + MPN per LCSC number (parts research 2026-11-24; confirm stock live before ordering).
MFR = {
    "C51950748": ("Espressif", "ESP32-C5-WROOM-1U-N8R8"),
    "C49450282": ("Espressif", "ESP32-H2-MINI-1U-H4S"),
    "C110926": ("STMicroelectronics", "LIS2DH12TR"),
    "C575551": ("Texas Instruments", "DRV8876PWPR"),
    "C191884": ("Texas Instruments", "TPS54202DDCR"),
    "C79272": ("Sunlord", "SWPA6045S100MT"),
    "C22452": ("MDD", "SS54"),
    "C188412": ("Shandong Jingdao", "SMBJ15A"),
    "C158007": ("JST", "B2PS-VH(LF)(SN)"),
    "C160354": ("JST", "B4B-PH-SM4-TB(LF)(SN)"),
    "C285524": ("C&K", "PTS645VL582LFS"),
    "C318884": ("XKB Connection", "TS-1187A-B-A-B"),
    "C52917434": ("Worldsemi", "WS2812B-2020-V6"),
    "C78876": ("Holtek", "HT7536-1 (SOT-89)"),
    "C25744": ("UNI-ROYAL", "0402WGF1002TCE"),
    "C25076": ("UNI-ROYAL", "0402WGF1000TCE"),
    "C11702": ("UNI-ROYAL", "0402WGF1001TCE"),
    "C25900": ("UNI-ROYAL", "0402WGF4701TCE"),
    "C1525": ("Samsung", "CL05B104KO5NNNC"),
    "C52923": ("Samsung", "CL05A105KA5NQNC"),
    "C96446": ("Samsung", "CL10A106MA8NRNC (25 V)"),
    "C45783": ("Samsung", "CL21A226MAQNNNE"),
    "C25741": ("UNI-ROYAL", "0402WGF1003TCE"),
    "C43473": ("UNI-ROYAL", "0402WGF2212TCE"),
    "C25784": ("UNI-ROYAL", "0402WGF3001TCE"),
    "C25765": ("UNI-ROYAL", "0402WGF2002TCE"),
    "C1572": ("Fenghua", "0402CG560J500NT"),
    "C15195": ("Samsung", "CL05B103KB5NNNC"),
    "C1523": ("Fenghua", "0402B102K500NT"),
    "C149620": ("YAGEO", "AC0603KRX7R9BB104"),
    "C21122": ("Samsung", "CL10B223KB8NNNC"),
    "C13585": ("Samsung", "CL31A106KBHNNNE"),
    "C125975": ("Nichicon", "UWT1V101MCL1GS"),
}
# How it goes on (all assembled at home: stencil + paste + hotplate for top-side SMD, iron for through-hole).
ROUTE = {
    "C51950748": ("BUY", "Mouser (LCSC showed 0). MSL 3: bake if the bag has been open > 1 week"),
    "C49450282": ("BUY", "DigiKey/Mouser. MSL 3: bake if the bag has been open > 1 week"),
    "C110926": ("LCSC", "2x2 mm LGA: stencil + paste. Patchy stock: DigiKey/Mouser if short. Optional part"),
    "C575551": ("LCSC", "HTSSOP-16 with thermal pad: paste the pad (stencil), hotplate"),
    "C158007": ("IRON", "through-hole JST VH side entry: iron after reflow, never on the hotplate"),
    "C285524": ("BUY", "LCSC/JLC stock 0 (2026-11-24): buy C&K PTS645VL58-2 LFS at DigiKey/Mouser. Through-hole, "
                        "iron after reflow; the shell's cap goes on its 5.8 mm plunger. Don't substitute a 3.9 mm "
                        "actuator (PTS645VK39): its tip ends inside the board edge"),
    "C79272": ("BUY", "Sunlord SWPA6045S100MT is listed at JLC but not in LCSC's store: any shielded 6x6x4.5 10 uH "
                      "with Isat >= 2 A fits the footprint"),
    "C160354": ("LCSC", "SMD connector (reflow-rated housing): hotplate with the rest"),
}
SPARE = {"R": 2, "C": 2, "D": 2}

EXTRAS = [
    ("PCB, 4 layers, 33.94 x 152.6 mm, bare: see ORDER.md", 1, "JLCPCB", "control-gerbers.zip", "JLCPCB", "minimum order 5"),
    ("SMT stencil, top side only, 0.12 mm, with the PCB order", 0, "JLCPCB", "from control-gerbers.zip", "JLCPCB",
     "one per order. Needs a frame/jig at least 160 mm long"),
    ("Solder paste Type 4 (0.5 mm-pitch LGA, 0.65 mm HTSSOP)", 0, "e.g. Chip Quik", "SMD291AX10T4", "DigiKey / Mouser", ""),
    ("1x40 2.54 mm male breakaway header", 1 / 6, "Sullins", "PRPC040SAAN-RC", "DigiKey/Mouser/LCSC",
     "a 1x6 for J1 per board"),
    ("Antenna 2.4/5 GHz, U.FL (MHF I)", 1, "Molex", "146187-0100", "DigiKey/Mouser", "for the C5"),
    ("Antenna 2.4 GHz, MHF III / W.FL ('IPEX3'), <= 2.33 dBi", 1, "generic", "IPEX3 2.4 GHz FPC", "AliExpress", "for the H2"),
    ("Right-angle light pipe for the status LED (to match the shell's window)", 1, "TBD", "TBD", "TBD",
     "reuse Wayzn's if it comes off cleanly; otherwise choose after a fit check"),
    ("3.3 V USB-serial adapter with DTR/RTS", 0, "generic", "CP2102 / CH340", "Amazon", "one per order"),
]


def main():
    lines = {}
    for ref, p in design.all_parts().items():
        if p["dnp"] or ref.startswith(("JP", "TP", "H")):
            continue
        key = p["lcsc"] or f'{p["value"]}|{p["footprint"]}'
        e = lines.setdefault(key, {"refs": [], "value": p["value"], "footprint": p["footprint"].split(":")[1],
                                   "lcsc": p["lcsc"]})
        e["refs"].append(ref)
    rows = []
    for key, e in lines.items():
        if e["refs"] == ["J1"]:
            continue  # cut from the header strip (EXTRAS)
        n = len(e["refs"])
        kind = "".join(c for c in e["refs"][0] if c.isalpha())
        total = n * BOARDS + SPARE.get(kind, 0) + (2 if e["lcsc"] == "C110926" else 0)
        mfr, mpn = MFR.get(e["lcsc"], ("", e["value"]))
        route, note = ROUTE.get(e["lcsc"], ("LCSC", "round up to LCSC's minimum order quantity"))
        if not e["lcsc"]:
            route, note = "LCSC", "LCSC number to choose (see design.py)"
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

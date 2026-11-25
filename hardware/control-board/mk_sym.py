#!/usr/bin/env python3
"""Write lib/Control.kicad_sym: symbols not in the KiCad 10 libraries.
  LIS2DH12 (LGA-12): ST datasheet "Pin description".
  DRV8876 (HTSSOP-16 PWP): TI SLVSDS7B pin table; pin 17 = the thermal pad (GND)."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
# number, name, electrical type, side (L/R/T/B)
LIS2DH12 = [
    ("1", "SCL/SPC", "input", "L"), ("4", "SDA/SDI/SDO", "bidirectional", "L"),
    ("3", "SDO/SA0", "bidirectional", "L"), ("2", "~{CS}", "input", "L"),
    ("12", "INT1", "output", "R"), ("11", "INT2", "output", "R"),
    ("9", "VDD", "power_in", "T"), ("10", "VDD_IO", "power_in", "T"),
    ("5", "RES", "passive", "B"), ("6", "GND", "power_in", "B"),
    ("7", "GND", "passive", "B"), ("8", "GND", "passive", "B"),
]
DRV8876 = [
    ("1", "EN/IN1", "input", "L"), ("2", "PH/IN2", "input", "L"), ("3", "~{SLEEP}", "input", "L"),
    ("16", "PMODE", "input", "L"), ("7", "IMODE", "input", "L"), ("5", "VREF", "input", "L"),
    ("4", "~{FAULT}", "open_collector", "R"), ("6", "IPROPI", "output", "R"),
    ("8", "OUT1", "output", "R"), ("10", "OUT2", "output", "R"),
    ("11", "VM", "power_in", "T"), ("12", "VCP", "passive", "T"), ("13", "CPH", "passive", "T"),
    ("14", "CPL", "passive", "T"),
    ("9", "PGND", "power_in", "B"), ("15", "GND", "power_in", "B"), ("17", "PAD", "passive", "B"),
]


def eff(hide=False):
    return "(effects\n\t\t\t\t(font (size 1.27 1.27))" + ("\n\t\t\t\t(hide yes)" if hide else "") + "\n\t\t\t)"


def sym(name, pins, fp, desc):
    side = {s: [p for p in pins if p[3] == s] for s in "LRTB"}
    rows = max(len(side["L"]), len(side["R"]), 2)
    cols = max(len(side["T"]), len(side["B"]), 2)
    W = max(10.16, 2.54 * (cols + 1))
    H = 2.54 * (rows + 1) / 2 + 1.27
    out = [f'\t(symbol "{name}"\n\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)',
           f'\t\t(property "Reference" "U"\n\t\t\t(at {-W:.2f} {H + 2.54:.2f} 0)\n\t\t\t{eff()}\n\t\t)',
           f'\t\t(property "Value" "{name}"\n\t\t\t(at 5.08 {H + 2.54:.2f} 0)\n\t\t\t{eff()}\n\t\t)',
           f'\t\t(property "Footprint" "{fp}"\n\t\t\t(at 0 0 0)\n\t\t\t{eff(True)}\n\t\t)',
           f'\t\t(property "Datasheet" ""\n\t\t\t(at 0 0 0)\n\t\t\t{eff(True)}\n\t\t)',
           f'\t\t(property "Description" "{desc}"\n\t\t\t(at 0 0 0)\n\t\t\t{eff(True)}\n\t\t)',
           f'\t\t(symbol "{name}_0_1"\n\t\t\t(rectangle\n\t\t\t\t(start {-W:.2f} {H:.2f})\n\t\t\t\t(end {W:.2f} {-H:.2f})\n'
           f'\t\t\t\t(stroke (width 0.254) (type default))\n\t\t\t\t(fill (type background))\n\t\t\t)\n\t\t)',
           f'\t\t(symbol "{name}_1_1"']
    for s, plist in side.items():
        n = len(plist)
        for k, (num, pname, et, _) in enumerate(plist):
            off = 2.54 * (k - (n - 1) / 2)
            off = round(off / 1.27) * 1.27
            if s == "L":
                x, y, a = -W - 2.54, -off, 0
            elif s == "R":
                x, y, a = W + 2.54, -off, 180
            elif s == "T":
                x, y, a = off, H + 2.54, 270
            else:
                x, y, a = off, -H - 2.54, 90
            out.append(f'\t\t\t(pin {et} line\n\t\t\t\t(at {x:.2f} {y:.2f} {a})\n\t\t\t\t(length 2.54)\n'
                       f'\t\t\t\t(name "{pname}"\n\t\t\t\t\t(effects\n\t\t\t\t\t\t(font (size 1.0 1.0))\n\t\t\t\t\t)\n\t\t\t\t)\n'
                       f'\t\t\t\t(number "{num}"\n\t\t\t\t\t(effects\n\t\t\t\t\t\t(font (size 1.0 1.0))\n\t\t\t\t\t)\n\t\t\t\t)\n\t\t\t)')
    out.append("\t\t)\n\t)")
    return "\n".join(out)


if __name__ == "__main__":
    txt = ('(kicad_symbol_lib\n\t(version 20251024)\n\t(generator "wayzn-control")\n\t(generator_version "10.0")\n'
           + sym("LIS2DH12", LIS2DH12, "Package_LGA:LGA-12_2x2mm_P0.5mm", "ST LIS2DH12 3-axis accelerometer, LGA-12 2x2")
           + "\n" + sym("DRV8876", DRV8876, "Package_SO:HTSSOP-16-1EP_4.4x5mm_P0.65mm_EP3.4x5mm_Mask2.46x2.31mm",
                        "TI DRV8876 brushed DC H-bridge, current sense + regulation, HTSSOP-16 PowerPAD")
           + "\n)\n")
    open(os.path.join(HERE, "lib/Control.kicad_sym"), "w").write(txt)
    print("wrote lib/Control.kicad_sym")

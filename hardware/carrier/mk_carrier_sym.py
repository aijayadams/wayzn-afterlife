#!/usr/bin/env python3
"""Write lib/Carrier.kicad_sym: symbols not in the KiCad 10 libraries (LIS2DH12, LGA-12).
Pin numbers/names: ST LIS2DH12 datasheet, "Pin description" table (see README)."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
# number, name, electrical type, side (L/R/T/B), slot on that side
LIS2DH12 = [
    ("1", "SCL/SPC", "input", "L", 0), ("4", "SDA/SDI/SDO", "bidirectional", "L", 1),
    ("3", "SDO/SA0", "bidirectional", "L", 2), ("2", "~{CS}", "input", "L", 3),
    ("12", "INT1", "output", "R", 0), ("11", "INT2", "output", "R", 1),
    ("9", "VDD", "power_in", "T", 0), ("10", "VDD_IO", "power_in", "T", 1),
    ("5", "RES", "passive", "B", 0), ("6", "GND", "power_in", "B", 1),
    ("7", "GND", "passive", "B", 2), ("8", "GND", "passive", "B", 3),
]


def eff(hide=False):
    return "(effects\n\t\t\t\t(font (size 1.27 1.27))" + ("\n\t\t\t\t(hide yes)" if hide else "") + "\n\t\t\t)"


def sym(name, pins, fp, desc):
    W, H = 10.16, 7.62
    out = [f'\t(symbol "{name}"\n\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)',
           f'\t\t(property "Reference" "U"\n\t\t\t(at {-W:.2f} {H + 2.54:.2f} 0)\n\t\t\t{eff()}\n\t\t)',
           f'\t\t(property "Value" "{name}"\n\t\t\t(at 5.08 {H + 2.54:.2f} 0)\n\t\t\t{eff()}\n\t\t)',
           f'\t\t(property "Footprint" "{fp}"\n\t\t\t(at 0 0 0)\n\t\t\t{eff(True)}\n\t\t)',
           f'\t\t(property "Datasheet" ""\n\t\t\t(at 0 0 0)\n\t\t\t{eff(True)}\n\t\t)',
           f'\t\t(property "Description" "{desc}"\n\t\t\t(at 0 0 0)\n\t\t\t{eff(True)}\n\t\t)',
           f'\t\t(symbol "{name}_0_1"\n\t\t\t(rectangle\n\t\t\t\t(start {-W:.2f} {H:.2f})\n\t\t\t\t(end {W:.2f} {-H:.2f})\n'
           f'\t\t\t\t(stroke (width 0.254) (type default))\n\t\t\t\t(fill (type background))\n\t\t\t)\n\t\t)',
           f'\t\t(symbol "{name}_1_1"']
    for num, pname, et, side, k in pins:
        if side == "L":
            x, y, a = -W - 2.54, 5.08 - 2.54 * k, 0
        elif side == "R":
            x, y, a = W + 2.54, 5.08 - 2.54 * k, 180
        elif side == "T":
            x, y, a = -2.54 + 5.08 * k, H + 2.54, 270
        else:
            x, y, a = -3.81 + 2.54 * k, -H - 2.54, 90
        out.append(f'\t\t\t(pin {et} line\n\t\t\t\t(at {x:.2f} {y:.2f} {a})\n\t\t\t\t(length 2.54)\n'
                   f'\t\t\t\t(name "{pname}"\n\t\t\t\t\t(effects\n\t\t\t\t\t\t(font (size 1.0 1.0))\n\t\t\t\t\t)\n\t\t\t\t)\n'
                   f'\t\t\t\t(number "{num}"\n\t\t\t\t\t(effects\n\t\t\t\t\t\t(font (size 1.0 1.0))\n\t\t\t\t\t)\n\t\t\t\t)\n\t\t\t)')
    out.append("\t\t)\n\t)")
    return "\n".join(out)


if __name__ == "__main__":
    txt = ('(kicad_symbol_lib\n\t(version 20251024)\n\t(generator "wayzn-carrier")\n\t(generator_version "10.0")\n'
           + sym("LIS2DH12", LIS2DH12, "Package_LGA:LGA-12_2x2mm_P0.5mm", "ST LIS2DH12 3-axis accelerometer, LGA-12 2x2")
           + "\n)\n")
    open(os.path.join(HERE, "lib/Carrier.kicad_sym"), "w").write(txt)
    print("wrote lib/Carrier.kicad_sym")

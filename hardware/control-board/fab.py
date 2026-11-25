#!/usr/bin/env python3
"""JLCPCB fabrication + assembly outputs from control.kicad_pcb and design.py -> fab/:
  control-gerbers.zip   Gerbers (4 layers) + Excellon drill, origin = the board corner (aux origin)
  control-bom.csv       JLC BOM: Comment, Designator, Footprint, LCSC
  control-cpl.csv       JLC CPL: Designator, Mid X, Mid Y, Layer, Rotation
Not in the BOM/CPL: the through-hole parts (J1 programming header, J6/J7 VH connectors, SW3 door button:
iron), the post holes (H*), test pads (TP*) and the solder jumper (JP1: copper only).
Check every rotation in JLC's placement preview before paying: their footprint zero orientations differ
from KiCad's for some parts (modules and connectors especially)."""
import csv
import os
import re
import shutil
import subprocess
import zipfile

import design

HERE = os.path.dirname(os.path.abspath(__file__))
KC = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
PCB = os.path.join(HERE, "control.kicad_pcb")
OUT = os.path.join(HERE, "fab")
JLC_SKIP = ("U1", "U2", "D3")   # if JLC places the small parts: modules and LED are hotplated at home
HAND = {"J1", "J6", "J7", "SW3"}  # through-hole: soldered with an iron after reflow


def run(*args):
    subprocess.run([KC, *args], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    # Only the generated files are replaced: fab/ also holds hand-written ones (ORDER.md, order-bom.csv).
    g = os.path.join(OUT, "gerbers")
    shutil.rmtree(g, ignore_errors=True)
    os.makedirs(g)
    layers = "F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"
    run("pcb", "export", "gerbers", "--layers", layers, "--use-drill-file-origin", "--subtract-soldermask",
        "-o", g + "/", PCB)
    run("pcb", "export", "drill", "--format", "excellon", "--drill-origin", "plot", "--excellon-separate-th",
        "--generate-map", "--map-format", "pdf", "-o", g + "/", PCB)
    zpath = os.path.join(OUT, "control-gerbers.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(g)):
            if not f.endswith(".pdf"):
                z.write(os.path.join(g, f), f)

    parts = design.all_parts()
    groups = {}
    for ref, p in parts.items():
        if p["dnp"] or ref in HAND or ref.startswith(("TP", "JP", "H")):  # JP: copper only; H: post holes
            continue
        key = (p["value"], p["footprint"].split(":")[1], p["lcsc"])
        groups.setdefault(key, []).append(ref)
    def bom(name, skip=()):
        with open(os.path.join(OUT, name), "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["Comment", "Designator", "Footprint", "LCSC"])
            for (val, fp, lcsc), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
                refs = [r for r in refs if r not in skip]
                if refs:
                    w.writerow([val, ",".join(sorted(refs, key=lambda r: (re.match(r"[A-Z]+", r).group(), int(re.search(r"\d+", r).group())))), fp, lcsc])
    bom("control-bom.csv")                                   # everything JLC can place
    bom("control-bom-no-modules.csv", skip=("U1", "U2"))     # ESP32 modules bought separately (consign/hand-solder)
    # If JLC places the small parts: everything but the modules and the LED (hotplated at home with the modules).
    bom("control-bom-jlc.csv", skip=JLC_SKIP)

    pos = os.path.join(OUT, "pos.csv")
    run("pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both", "--use-drill-file-origin",
        "--exclude-dnp", "-o", pos, PCB)
    with open(pos) as f, open(os.path.join(OUT, "control-cpl.csv"), "w", newline="") as o:
        w = csv.writer(o)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for row in csv.DictReader(f):
            ref = row["Ref"]
            if ref in HAND or ref.startswith(("TP", "H", "JP")) or parts.get(ref, {}).get("dnp"):
                continue
            # kicad-cli already writes Y-up coordinates from the aux origin, as JLC expects.
            w.writerow([ref, f'{float(row["PosX"]):.3f}mm', f'{float(row["PosY"]):.3f}mm',
                        "Top" if row["Side"] == "top" else "Bottom", f'{float(row["Rot"]) % 360:.0f}'])
    os.remove(pos)
    # Placement files to pair with the reduced BOMs (JLC warns about designators missing from the BOM).
    for name, skip in (("control-cpl-no-modules.csv", ("U1", "U2")), ("control-cpl-jlc.csv", JLC_SKIP)):
        with open(os.path.join(OUT, "control-cpl.csv")) as f, open(os.path.join(OUT, name), "w") as o:
            for line in f:
                if line.split(",")[0] not in skip:
                    o.write(line)
    missing = [r for (v, fp, l), refs in groups.items() if not l for r in refs]
    print("wrote", OUT, "| BOM lines:", len(groups), "| parts without LCSC:", missing or "none")


if __name__ == "__main__":
    main()

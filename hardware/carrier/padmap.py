#!/usr/bin/env python3
"""Print every netted pad's position (WYP-BC-100 frame) and each footprint's courtyard, from carrier.kicad_pcb.
For planning routes in layout.py. Run with KiCad's Python."""
import os
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_pcb  # noqa: E402

b = pcbnew.LoadBoard(os.path.join(HERE, "carrier.kicad_pcb"))
ox, oy = gen_pcb.PAGE_OFFSET
T = pcbnew.ToMM
want = set(sys.argv[1:])
for fp in sorted(b.GetFootprints(), key=lambda f: f.GetReference()):
    ref = fp.GetReference()
    if want and ref not in want:
        continue
    for layer, side in ((pcbnew.F_CrtYd, "F"), (pcbnew.B_CrtYd, "B")):
        c = fp.GetCourtyard(layer)
        if c.OutlineCount():
            bb = c.BBox()
            print(f"{ref} court {side} x {T(bb.GetLeft()) - ox:.2f}..{T(bb.GetRight()) - ox:.2f} "
                  f"y {T(bb.GetTop()) - oy:.2f}..{T(bb.GetBottom()) - oy:.2f}")
    pads = []
    for p in fp.Pads():
        n = p.GetNetname().lstrip("/")
        if n and n not in ("GND",) or (want and n):
            s = p.GetBoundingBox()
            pads.append(f"{p.GetNumber()}:{n}@({T(p.GetPosition().x) - ox:.2f},{T(p.GetPosition().y) - oy:.2f})"
                        f"[{T(s.GetWidth()):.2f}x{T(s.GetHeight()):.2f}]")
    if pads:
        print("   ", "  ".join(pads))

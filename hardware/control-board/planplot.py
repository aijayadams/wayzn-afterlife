#!/usr/bin/env python3
"""Planning plot of control.kicad_pcb: pads (labelled with their net), courtyards, tracks, vias, outline, 1 mm grid,
in the WYP-BC-100 frame. Two steps (KiCad's Python has no Pillow):
  <KiCad python> planplot.py dump   -> out/plan.json
  python3-with-Pillow planplot.py   -> out/plan.png"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
JS = os.path.join(HERE, "out/plan.json")


def dump():
    import pcbnew
    sys.path.insert(0, HERE)
    import gen_pcb
    b = pcbnew.LoadBoard(os.path.join(HERE, "control.kicad_pcb"))
    ox, oy = gen_pcb.PAGE_OFFSET
    T = pcbnew.ToMM
    d = {"pads": [], "courts": [], "tracks": [], "vias": []}
    for fp in b.GetFootprints():
        for layer, side in ((pcbnew.F_CrtYd, "F"), (pcbnew.B_CrtYd, "B")):
            c = fp.GetCourtyard(layer)
            if c.OutlineCount():
                bb = c.BBox()
                d["courts"].append([fp.GetReference(), side, T(bb.GetLeft()) - ox, T(bb.GetTop()) - oy,
                                    T(bb.GetRight()) - ox, T(bb.GetBottom()) - oy])
        for p in fp.Pads():
            bb = p.GetBoundingBox()
            d["pads"].append([fp.GetReference(), p.GetNumber(), p.GetNetname().lstrip("/"),
                              T(bb.GetLeft()) - ox, T(bb.GetTop()) - oy, T(bb.GetRight()) - ox, T(bb.GetBottom()) - oy,
                              p.GetAttribute() != pcbnew.PAD_ATTRIB_SMD, fp.IsFlipped()])
    for t in b.GetTracks():
        if isinstance(t, pcbnew.PCB_VIA):
            d["vias"].append([T(t.GetPosition().x) - ox, T(t.GetPosition().y) - oy, T(t.GetWidth(pcbnew.F_Cu)) / 2,
                              t.GetNetname().lstrip("/")])
        else:
            d["tracks"].append([T(t.GetStart().x) - ox, T(t.GetStart().y) - oy, T(t.GetEnd().x) - ox,
                                T(t.GetEnd().y) - oy, T(t.GetWidth()), t.GetLayerName()])
    json.dump(d, open(JS, "w"))
    print("wrote", JS)


def draw():
    from PIL import Image, ImageDraw, ImageFont
    import design
    d = json.load(open(JS))
    S = 28  # px per mm
    (x0, x1), (y0, y1) = design.OUTLINE["x"], design.OUTLINE["y"]
    W, H = int((x1 - x0 + 4) * S), int((y1 - y0 + 4) * S)
    im = Image.new("RGB", (W, H), (20, 24, 28))
    g = ImageDraw.Draw(im)
    P = lambda x, y: ((x - x0 + 2) * S, (y - y0 + 2) * S)
    F = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 11)
    Fb = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 15)
    for gx in range(int(x0), int(x1) + 1):
        g.line([P(gx, y0 - 1), P(gx, y1 + 1)], fill=(45, 50, 58) if gx % 5 else (80, 90, 100))
        if gx % 5 == 0:
            g.text(P(gx + 0.1, y0 - 1.9), str(gx), fill=(150, 160, 170), font=F)
    for gy in range(int(y0), int(y1) + 1):
        g.line([P(x0 - 1, gy), P(x1 + 1, gy)], fill=(45, 50, 58) if gy % 5 else (80, 90, 100))
        if gy % 5 == 0:
            g.text(P(x0 - 1.9, gy - 0.3), str(gy), fill=(150, 160, 170), font=F)
    g.rectangle([P(x0, y0), P(x1, y1)], outline=(230, 230, 230), width=2)
    for ref, side, a, b, c, e in d["courts"]:
        g.rectangle([P(a, b), P(c, e)], outline=(200, 80, 200) if side == "F" else (80, 140, 220), width=1)
        g.text(P(a + 0.15, b + 0.05), ref, fill=(230, 140, 230) if side == "F" else (120, 170, 240), font=Fb)
    for x0_, y0_, x1_, y1_, w, layer in d["tracks"]:
        col = (220, 60, 60) if layer == "F.Cu" else (60, 150, 230)
        g.line([P(x0_, y0_), P(x1_, y1_)], fill=col, width=max(2, int(w * S)))
    netcol = {"GND": (90, 90, 90), "+3V3": (200, 120, 40)}
    for ref, num, net, a, b, c, e, th, flipped in d["pads"]:
        col = netcol.get(net, (60, 170, 90) if net else (70, 70, 70))
        g.rectangle([P(a, b), P(c, e)], fill=col, outline=(255, 255, 255) if th else None)
        if net and net not in ("GND",):
            g.text(P(c + 0.05, b - 0.05), net, fill=(255, 255, 160), font=F)
    for x, y, r, net in d["vias"]:
        g.ellipse([P(x - r, y - r), P(x + r, y + r)], fill=(240, 200, 60) if net == "+3V3" else (160, 160, 160))
    out = os.path.join(HERE, "out/plan.png")
    im.save(out)
    print("wrote", out, im.size)


if __name__ == "__main__":
    dump() if sys.argv[1:] == ["dump"] else draw()

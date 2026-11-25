#!/usr/bin/env python3
"""Draw the carrier (outline, parts, the lid ribs it sits between) over the WYP-BC-100 bench photo -> out/overlay-top.jpg.
Needs Pillow, and out/boxes.json from `<KiCad python> overlay.py boxes` (part courtyards in the Wayzn frame).
Photo scale: the STM+IMP header's 0.1" pitch (35 px in the 1400 px preview of the 4032 px photo)."""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PHOTO = os.path.join(HERE, "../../photos/2026-11-20_bench_stm-imp-connectors.jpeg")
BOXES = os.path.join(HERE, "out/boxes.json")


def boxes():
    import pcbnew
    sys.path.insert(0, HERE)
    import gen_pcb
    b = pcbnew.LoadBoard(os.path.join(HERE, "carrier.kicad_pcb"))
    ox, oy = gen_pcb.PAGE_OFFSET
    out = {}
    for fp in b.GetFootprints():
        for lay, side in ((pcbnew.F_CrtYd, "F"), (pcbnew.B_CrtYd, "B")):
            c = fp.GetCourtyard(lay)
            if c.OutlineCount():
                bb = c.BBox()
                out[fp.GetReference()] = dict(side=side, box=[pcbnew.ToMM(bb.GetLeft()) - ox, pcbnew.ToMM(bb.GetTop()) - oy,
                                                             pcbnew.ToMM(bb.GetRight()) - ox, pcbnew.ToMM(bb.GetBottom()) - oy])
    json.dump(out, open(BOXES, "w"), indent=1)
    print("wrote", BOXES)


def draw():
    from PIL import Image, ImageDraw, ImageFont
    import design
    im = Image.open(PHOTO).convert("RGBA")
    k = im.width / 1400
    ppm = 35 * k / 2.54
    X0, Y0 = 829 * k, 450 * k

    def P(x, y):
        return (X0 + x * ppm, Y0 + y * ppm)
    ov = Image.new("RGBA", im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    font = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
    F = ImageFont.truetype(font, int(1.5 * ppm))
    Fs = ImageFont.truetype(font, int(1.1 * ppm))
    (x0, x1), (y0, y1) = design.OUTLINE["x"], design.OUTLINE["y"]
    d.rectangle([P(x0, y0), P(x1, y1)], fill=(0, 160, 255, 50), outline=(0, 200, 255, 255), width=12)
    bx = json.load(open(BOXES))
    style = {"U1": ((255, 215, 0), "ESP32-C5"), "U2": ((255, 140, 0), "ESP32-H2"), "U3": ((255, 90, 90), "accel"),
             "J1": ((0, 255, 120), "J1 socket\n(underside,\nonto STM+IMP)"),
             "J2": ((210, 140, 255), "J2: jumper\nto Wayzn J2"), "J3": ((210, 140, 255), "J3: Grid-EYE\ncable"),
             "J4": ((255, 255, 255), "J4 NRST"), "J5": ((255, 255, 255), "J5 PROG"),
             "J6": ((0, 255, 120), "J6 header\n(underside,\ninto J105)")}
    for ref, (c, label) in style.items():
        a, b_, c2, e = bx[ref]["box"]
        d.rectangle([P(a, b_), P(c2, e)], outline=c + (255,), width=9, fill=c + (35,))
        d.multiline_text(P(a + 0.4, b_ + 0.3), label, fill=c + (255,), font=Fs, stroke_width=4, stroke_fill=(0, 0, 0, 255))
    for rx in design.WAYZN["ribs_x"]:  # lid ribs (photo-measured): the carrier must fit between them
        d.line([P(rx, y0 - 4), P(rx, y1 + 0.5)], fill=(255, 60, 60, 255), width=10)
        d.text(P(rx + 0.4, y0 - 4), "lid rib", fill=(255, 60, 60, 255), font=Fs, stroke_width=4, stroke_fill=(0, 0, 0, 255))
    d.text(P(x0 + 0.5, y1 + 1.0), "blue: carrier outline, stacked 11 mm above the Wayzn board", fill=(0, 220, 255, 255),
           font=F, stroke_width=5, stroke_fill=(0, 0, 0, 255))
    crop = (int(X0 - 40 * ppm), int(Y0 - 9 * ppm), int(min(im.width, X0 + 33 * ppm)), int(Y0 + 37 * ppm))
    out = Image.alpha_composite(im, ov).crop(crop).convert("RGB")
    out = out.resize((1600, int(1600 * out.height / out.width)))
    path = os.path.join(HERE, "out/overlay-top.jpg")
    out.save(path, quality=88)
    print("wrote", path)


if __name__ == "__main__":
    boxes() if sys.argv[1:] == ["boxes"] else draw()

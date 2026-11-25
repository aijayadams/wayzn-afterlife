#!/usr/bin/env python3
"""Draw the control board's parts over the WYP-BC-100 scan (component-side view) -> out/overlay-scan.jpg.
Needs Pillow and out/boxes.json (part courtyards + J6/J7/SW3 pads, written by the KiCad-Python one-liner in README).
The scan is photos/2026-11-24_bench_scan-back-600dpi.jpeg: the straightened back scan, mirrored top-bottom here so
it reads as seen from the component side; its board corner is the design frame's origin (design.py)."""
import json
import os

from PIL import Image, ImageDraw, ImageFont, ImageOps

HERE = os.path.dirname(os.path.abspath(__file__))
SCAN = os.path.join(HERE, "../../photos/2026-11-24_bench_scan-back-600dpi.jpeg")
PX = 600 / 25.4          # px/mm in the committed 600 dpi copy
MARGIN = 3.0             # mm of background left around the board when it was cropped


def main():
    import design
    im = ImageOps.flip(Image.open(SCAN).convert("RGBA"))
    H = design.OUTLINE["y"][1]
    oy = im.height - (MARGIN + H) * PX          # the flip moves the board corner
    ox = MARGIN * PX

    def P(x, y):
        return (ox + x * PX, oy + y * PX)
    ov = Image.new("RGBA", im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", int(1.6 * PX))
    data = json.load(open(os.path.join(HERE, "out/boxes.json")))
    colours = {"U": (255, 200, 0), "J": (0, 255, 140), "S": (255, 120, 255), "D": (255, 90, 90), "L": (255, 160, 60)}
    for ref, (a, b, c, e) in data["boxes"].items():
        if ref.startswith(("R", "C", "TP", "H")):
            continue
        col = colours.get(ref[0], (200, 200, 255))
        d.rectangle([P(a, b), P(c, e)], outline=col + (255,), width=5, fill=col + (40,))
        d.text(P(a + 0.3, b + 0.2), ref, fill=col + (255,), font=font, stroke_width=3, stroke_fill=(0, 0, 0, 255))
    for x, y in data["pads"]:  # our J6/J7/SW3 pads: should sit on Wayzn's
        d.ellipse([P(x - 0.5, y - 0.5), P(x + 0.5, y + 0.5)], outline=(255, 0, 0, 255), width=4)
    for x, y in design.HOLES:
        d.ellipse([P(x - 2, y - 2), P(x + 2, y + 2)], outline=(255, 0, 0, 255), width=5)
    out = Image.alpha_composite(im, ov).convert("RGB")
    out = out.resize((out.width // 2, out.height // 2))
    path = os.path.join(HERE, "out/overlay-scan.jpg")
    out.save(path, quality=85)
    print("wrote", path)


if __name__ == "__main__":
    main()

"""Generate docs/before-after.svg and docs/presence.svg (README overview diagrams).

Usage: python3 tools/diagrams/gen_overview.py docs/
"""
import math
import os
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "."
FONT = 'font-family="Helvetica, Arial, sans-serif"'


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Svg:
    def __init__(self, w, h, title):
        self.w, self.h = w, h
        self.o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" {FONT}>',
                  f"<title>{esc(title)}</title>"]

    def add(self, s):
        self.o.append(s)

    def rect(self, x, y, w, h, fill, stroke="none", rx=14, sw=2, extra=""):
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" '
                 f'stroke-width="{sw}" {extra}/>')

    def text(self, x, y, s, size=16, fill="#0f172a", anchor="middle", weight="normal", extra=""):
        self.add(f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" '
                 f'font-weight="{weight}" {extra}>{esc(s)}</text>')

    def save(self, name):
        self.add("</svg>")
        with open(os.path.join(OUT, name), "w") as f:
            f.write("\n".join(self.o))


def cloud(svg, cx, cy, w, fill, stroke):
    s = w / 120
    p = (f"M {cx - 50 * s},{cy + 22 * s} "
         f"a {22 * s},{22 * s} 0 0 1 {4 * s},{-43 * s} "
         f"a {30 * s},{30 * s} 0 0 1 {55 * s},{-12 * s} "
         f"a {24 * s},{24 * s} 0 0 1 {41 * s},{20 * s} "
         f"a {19 * s},{19 * s} 0 0 1 {0},{35 * s} z")
    svg.add(f'<path d="{p}" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')


def arrow(svg, x1, y1, x2, y2, col, w=4, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    ang = math.atan2(y2 - y1, x2 - x1)
    hx, hy = x2 - 12 * math.cos(ang), y2 - 12 * math.sin(ang)
    svg.add(f'<line x1="{x1}" y1="{y1}" x2="{hx:.1f}" y2="{hy:.1f}" stroke="{col}" stroke-width="{w}"{d} '
            f'stroke-linecap="round"/>')
    a1, a2 = ang + 2.6, ang - 2.6
    svg.add(f'<polygon points="{x2},{y2} {x2 + 14 * math.cos(a1):.1f},{y2 + 14 * math.sin(a1):.1f} '
            f'{x2 + 14 * math.cos(a2):.1f},{y2 + 14 * math.sin(a2):.1f}" fill="{col}"/>')


def chip(svg, x, y, w, h, fill, label, sub, label_col="#f8fafc", sub_col="#cbd5e1"):
    for i in range(5):
        px = x + 18 + i * (w - 36) / 4
        svg.rect(px - 3, y - 8, 6, 10, "#94a3b8", rx=1)
        svg.rect(px - 3, y + h - 2, 6, 10, "#94a3b8", rx=1)
    svg.rect(x, y, w, h, fill, rx=8)
    svg.text(x + w / 2, y + h / 2 - 2, label, 17, label_col, weight="bold")
    svg.text(x + w / 2, y + h / 2 + 18, sub, 13, sub_col)


def badge(svg, x, y, s, fill, col, w=None):
    w = w or 14 + 7.6 * len(s)
    svg.rect(x, y, w, 26, fill, rx=13)
    svg.text(x + w / 2, y + 18, s, 13, col, weight="bold")
    return w


# --------------------------------------------------------------------------------------- before / after
v = Svg(1200, 600, "Before and after: the Wayzn door without the cloud")
v.add('<defs><linearGradient id="bgAfter" x1="0" y1="0" x2="0" y2="1">'
      '<stop offset="0" stop-color="#ecfdf5"/><stop offset="1" stop-color="#eff6ff"/></linearGradient></defs>')
v.rect(0, 0, 1200, 600, "#ffffff", rx=0)

# BEFORE card
v.rect(24, 24, 560, 552, "#f8fafc", "#e2e8f0")
v.text(304, 66, "Before", 28, "#64748b", weight="bold")
v.text(304, 92, "Every open and close went through two clouds", 15, "#64748b")
# chain
v.rect(214, 116, 180, 54, "#ffffff", "#cbd5e1", rx=12)
v.text(304, 150, "Wayzn app", 17, "#334155")
arrow(v, 304, 172, 304, 196, "#94a3b8", 3)
cloud(v, 214, 236, 150, "#f1f5f9", "#cbd5e1")
v.text(214, 240, "Wayzn cloud", 15, "#475569", weight="bold")
cloud(v, 394, 236, 150, "#f1f5f9", "#cbd5e1")
v.text(394, 240, "Electric Imp", 15, "#475569", weight="bold")
v.text(394, 258, "cloud", 13, "#64748b")
arrow(v, 275, 236, 333, 236, "#94a3b8", 3)
# shutdown stamp
v.add('<g transform="rotate(-8 304 214)">')
v.rect(200, 280, 208, 34, "#fef2f2", "#ef4444", rx=6, sw=2.5)
v.text(304, 303, "SHUT DOWN · MARCH 2027", 15, "#dc2626", weight="bold")
v.add("</g>")
arrow(v, 394, 274, 394, 340, "#94a3b8", 3, dash="6 5")
v.text(470, 314, "the door's own", 12, "#94a3b8", "start")
v.text(470, 330, "Wi-Fi module", 12, "#94a3b8", "start")
chip(v, 304, 350, 180, 64, "#475569", "imp module", "talks only to its cloud")
arrow(v, 394, 424, 394, 452, "#94a3b8", 3)
chip(v, 304, 460, 180, 64, "#1e3a8a", "STM32", "motor controller")
v.text(304, 556, "No cloud, no door.", 16, "#ef4444", weight="bold")

# AFTER card
v.rect(616, 24, 560, 552, "url(#bgAfter)", "#86efac", sw=2.5)
v.text(896, 66, "After", 28, "#047857", weight="bold")
v.text(896, 92, "Everything stays in your home", 15, "#047857")
# house boundary
v.rect(640, 108, 512, 420, "none", "#10b981", rx=22, sw=2, extra='stroke-dasharray="8 6"')
v.text(1140, 128, "your home network", 13, "#059669", "end", "bold")
# HA
v.rect(706, 140, 210, 62, "#ffffff", "#38bdf8", rx=14, sw=2.5)
v.text(811, 168, "Home Assistant", 17, "#0c4a6e", weight="bold")
v.text(811, 188, "open · close · schedule · tune", 12, "#0369a1")
v.add('<line x1="811" y1="204" x2="811" y2="246" stroke="#38bdf8" stroke-width="4"/>')
v.add('<polygon points="811,252 803,238 819,238" fill="#38bdf8"/><polygon points="811,198 803,212 819,212" fill="#38bdf8"/>')
v.text(822, 232, "local Wi-Fi", 12, "#0369a1", "start")
# thermal sensor
v.rect(986, 262, 150, 74, "#fff7ed", "#fb923c", rx=12, sw=2)
for r in range(4):
    for c in range(4):
        heat = math.exp(-((r - 2) ** 2 + (c - 1) ** 2) / 2.2)
        col = f"rgb({int(59 + 196 * heat)},{int(130 + 20 * heat - 60 * heat)},{int(246 - 200 * heat)})"
        v.rect(996 + c * 13, 272 + r * 13, 12, 12, col, rx=2)
v.text(1092, 292, "Thermal", 13, "#9a3412", weight="bold")
v.text(1092, 308, "sensor", 13, "#9a3412", weight="bold")
v.text(1092, 324, "8×8", 12, "#c2410c")
arrow(v, 984, 300, 916, 296, "#fb923c", 4)
chip(v, 716, 262, 190, 70, "#0f766e", "ESP32 brain", "open source firmware")
arrow(v, 811, 344, 811, 384, "#10b981", 4)
chip(v, 721, 392, 180, 64, "#1e3a8a", "STM32", "same motor controller")
v.text(811, 486, "Wayzn's motor & safety firmware, untouched", 12, "#047857")
# badges
bx = 676
for s, f, c in [("No cloud", "#dcfce7", "#166534"), ("Works offline", "#dbeafe", "#1e40af"),
                ("Open source", "#fef3c7", "#92400e"), ("Smarter dog detection", "#ffedd5", "#9a3412")]:
    bx += badge(v, bx, 540, s, f, c) + 8
v.save("before-after.svg")

# --------------------------------------------------------------------------------------- presence
p = Svg(1200, 470, "How the door sees your dog")
p.rect(0, 0, 1200, 470, "#ffffff", rx=0)
p.text(600, 44, "How the door sees your dog", 26, "#0f172a", weight="bold")
p.text(600, 70, "A tiny 8×8 thermal camera watches the doorway 10 times a second. Every pixel learns its own "
       "background and noise, so a dog stands out even when it only warms a pixel or two.", 14, "#475569")

# heatmap
gx, gy, cs = 60, 110, 34
p.text(gx + 4 * cs, gy - 14, "what the sensor sees", 14, "#334155", weight="bold")
for r in range(8):
    for c in range(8):
        heat = 0.9 * math.exp(-((r - 4.6) ** 2 / 3.2 + (c - 1.8) ** 2 / 1.6))
        heat += 0.05 * math.sin(r * 1.7 + c * 2.3) + 0.05
        heat = max(0.0, min(1.0, heat))
        rr = int(30 + 225 * heat)
        gg = int(64 + 120 * heat - 90 * heat * heat)
        bb = int(175 - 150 * heat)
        p.rect(gx + c * cs, gy + r * cs, cs - 3, cs - 3, f"rgb({rr},{max(gg, 20)},{max(bb, 20)})", rx=5)
p.add(f'<rect x="{gx + 0 * cs - 4}" y="{gy + 3 * cs - 4}" width="{4 * cs + 5}" height="{4 * cs + 5}" rx="10" '
      f'fill="none" stroke="#f97316" stroke-width="3" stroke-dasharray="7 5"/>')
p.text(gx + 4 * cs, gy + 8 * cs + 26, "a dog near the door: a few warm pixels", 13, "#9a3412")

arrow(p, 352, 248, 420, 248, "#64748b", 4)

# two levels
p.rect(432, 120, 330, 118, "#ecfdf5", "#10b981", rx=16, sw=2.5)
p.text(597, 154, "Presence", 22, "#047857", weight="bold")
p.text(597, 180, "a dog wants out", 15, "#065f46")
p.text(597, 210, "opens the door in ~0.5 s", 15, "#065f46", weight="bold")
p.rect(432, 262, 330, 138, "#fffbeb", "#f59e0b", rx=16, sw=2.5)
p.text(597, 296, "Occupied", 22, "#b45309", weight="bold")
p.text(597, 322, "anything in the doorway", 15, "#92400e")
p.text(597, 352, "the door never closes on it", 15, "#92400e", weight="bold")
p.text(597, 376, "and a closing door reverses", 15, "#92400e", weight="bold")
p.text(597, 428, "Occupied is the more sensitive of the two: a false alarm only delays", 12, "#64748b")
p.text(597, 444, "a close, so the door errs on the side of the dog.", 12, "#64748b")

# score trace
tx, ty, tw, th = 800, 120, 360, 250
p.rect(tx, ty, tw, th, "#f8fafc", "#e2e8f0", rx=14)
p.text(tx + tw / 2, ty + 26, "warmth score over a dog visit", 14, "#334155", weight="bold")
base = ty + th - 30
lvl_pres, lvl_occ = base - 120, base - 72
p.add(f'<line x1="{tx + 20}" y1="{lvl_pres}" x2="{tx + tw - 20}" y2="{lvl_pres}" stroke="#10b981" '
      f'stroke-width="2" stroke-dasharray="6 5"/>')
p.add(f'<line x1="{tx + 20}" y1="{lvl_occ}" x2="{tx + tw - 20}" y2="{lvl_occ}" stroke="#f59e0b" '
      f'stroke-width="2" stroke-dasharray="6 5"/>')
p.text(tx + tw - 22, lvl_pres - 6, "presence", 12, "#047857", "end", "bold")
p.text(tx + tw - 22, lvl_occ - 6, "occupied", 12, "#b45309", "end", "bold")
pts = []
for i in range(141):
    t = i / 140
    val = 175 * math.exp(-((t - 0.38) / 0.09) ** 2) + 60 * math.exp(-((t - 0.62) / 0.12) ** 2)
    val += 4 * math.sin(i * 1.9) + 3 * math.sin(i * 0.7)
    pts.append((tx + 20 + t * (tw - 40), base - max(val, 0) * 0.82))
p.add('<polyline points="' + " ".join(f"{x:.1f},{y:.1f}" for x, y in pts) +
      '" fill="none" stroke="#ea580c" stroke-width="3" stroke-linejoin="round"/>')
p.add(f'<line x1="{tx + 20}" y1="{base}" x2="{tx + tw - 20}" y2="{base}" stroke="#94a3b8" stroke-width="2"/>')
p.text(tx + 20 + 0.31 * (tw - 40), base + 20, "door opens", 12, "#047857", weight="bold")
p.text(tx + 20 + 0.80 * (tw - 40), base + 20, "clear: may close", 12, "#475569")
p.save("presence.svg")
print("ok")

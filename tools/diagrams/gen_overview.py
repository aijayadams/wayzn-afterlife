"""Generate docs/before-after.svg (original -> Phase A -> Phase B) and docs/presence.svg (README diagrams).

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


# --------------------------------------------------------------------------------------- original -> A -> B
# Three columns with the same layers, so you can watch Wayzn's parts go: Phase A drops the app, both clouds and the
# imp; Phase B also drops the STM32 and its motor driver (the whole Wayzn board).
W, H = 1320, 720
v = Svg(W, H, "Original, Phase A and Phase B: Wayzn's cloud and hardware removed step by step")
v.rect(0, 0, W, H, "#ffffff", rx=0)
WAYZN, OURS, GONE = "#475569", "#0f766e", "#cbd5e1"


def ghost(svg, cx, y, w, h, label, why):
    svg.rect(cx - w / 2, y, w, h, "#f8fafc", "#cbd5e1", rx=10, sw=2, extra='stroke-dasharray="6 5"')
    svg.text(cx, y + h / 2 - 1, label, 14, "#94a3b8", weight="bold", extra='text-decoration="line-through"')
    svg.text(cx, y + h / 2 + 16, why, 12, "#94a3b8")


def box(svg, cx, y, w, h, fill, stroke, label, sub, col, subcol):
    svg.rect(cx - w / 2, y, w, h, fill, stroke, rx=12, sw=2)
    svg.text(cx, y + h / 2 - 1, label, 15, col, weight="bold")
    svg.text(cx, y + h / 2 + 16, sub, 12, subcol)


cols = [
    dict(title="Original", sub="Wayzn's design", status="Cloud shuts down March 2027", scol="#dc2626",
         bg="#f8fafc", border="#e2e8f0", tcol="#64748b"),
    dict(title="Phase A", sub="ESP32 dev board on Wayzn's board", status="Works today", scol="#047857",
         bg="#f0fdf4", border="#86efac", tcol="#047857"),
    dict(title="Phase B", sub="Our own control board", status="In design and validation", scol="#b45309",
         bg="#fffbeb", border="#fcd34d", tcol="#92400e"),
]
CW, GAP = 412, 18
ROW = dict(ctl=118, cloud=196, brain=300, mcu=392, drv=484, door=576)
for k, c in enumerate(cols):
    x = 24 + k * (CW + GAP)
    cx = x + CW / 2
    v.rect(x, 20, CW, H - 40, c["bg"], c["border"], sw=2.5)
    v.text(cx, 58, c["title"], 26, c["tcol"], weight="bold")
    v.text(cx, 82, c["sub"], 14, c["tcol"])
    bw = 280
    # 1: who controls the door
    if k == 0:
        box(v, cx, ROW["ctl"], bw, 50, "#ffffff", "#cbd5e1", "Wayzn app", "on your phone", "#334155", "#64748b")
    else:
        box(v, cx, ROW["ctl"], bw, 50, "#ffffff", "#38bdf8", "Your home automation",
            "e.g. Home Assistant · local API", "#0c4a6e", "#0369a1")
    arrow(v, cx, ROW["ctl"] + 52, cx, ROW["cloud"] - 4, "#94a3b8" if k == 0 else "#38bdf8", 3,
          dash=None if k == 0 else "2 6")
    # 2: the clouds
    if k == 0:
        cloud(v, cx - 70, ROW["cloud"] + 44, 118, "#f1f5f9", "#cbd5e1")
        v.text(cx - 70, ROW["cloud"] + 48, "Wayzn cloud", 13, "#475569", weight="bold")
        cloud(v, cx + 70, ROW["cloud"] + 44, 118, "#f1f5f9", "#cbd5e1")
        v.text(cx + 70, ROW["cloud"] + 46, "Electric Imp", 13, "#475569", weight="bold")
        v.text(cx + 70, ROW["cloud"] + 61, "cloud", 12, "#64748b")
        arrow(v, cx - 20, ROW["cloud"] + 44, cx + 22, ROW["cloud"] + 44, "#94a3b8", 3)
    else:
        ghost(v, cx, ROW["cloud"] + 10, bw, 62, "Wayzn + Electric Imp clouds", "gone: local Wi-Fi only")
    arrow(v, cx, ROW["cloud"] + 80, cx, ROW["brain"] - 10, "#94a3b8" if k == 0 else "#10b981", 3)
    # 3: the brain
    if k == 0:
        chip(v, cx - 95, ROW["brain"], 190, 60, WAYZN, "imp module", "Wayzn's cloud brain")
    elif k == 1:
        chip(v, cx - 95, ROW["brain"], 190, 60, OURS, "ESP32 brain", "replaces the imp")
    else:
        chip(v, cx - 95, ROW["brain"], 190, 60, OURS, "ESP32-C5", "brain + motor control")
    # 4: motor controller
    arrow(v, cx, ROW["brain"] + 70, cx, ROW["mcu"] - 10, "#94a3b8" if k < 2 else "#10b981", 3)
    if k < 2:
        chip(v, cx - 95, ROW["mcu"], 190, 60, "#1e3a8a", "STM32",
             "Wayzn's motor controller" if k == 0 else "Wayzn's, untouched")
    else:
        ghost(v, cx, ROW["mcu"] - 2, 250, 62, "STM32 motor controller", "gone: the C5 drives the motor")
    # 5: motor driver
    arrow(v, cx, ROW["mcu"] + 70, cx, ROW["drv"] - 10, "#94a3b8" if k < 2 else "#10b981", 3)
    if k < 2:
        chip(v, cx - 95, ROW["drv"], 190, 60, "#334155", "DRV8828", "Wayzn's motor driver")
    else:
        chip(v, cx - 95, ROW["drv"], 190, 60, OURS, "DRV8876", "replaces the DRV8828")
    # Wayzn's board vs ours: a bracket label
    if k < 2:
        v.rect(x + 14, ROW["brain"] - 16, 22, ROW["drv"] + 76 - ROW["brain"] + 16, "none", "#94a3b8", rx=6, sw=2,
               extra='stroke-dasharray="4 4"')
        v.add(f'<text x="{x + 30}" y="{(ROW["brain"] + ROW["drv"] + 60) / 2}" font-size="12" fill="#64748b" '
              f'text-anchor="middle" transform="rotate(-90 {x + 30} {(ROW["brain"] + ROW["drv"] + 60) / 2})">'
              "Wayzn's control board</text>")
    else:
        v.rect(x + 14, ROW["brain"] - 16, 22, ROW["drv"] + 76 - ROW["brain"] + 16, "none", "#10b981", rx=6, sw=2,
               extra='stroke-dasharray="4 4"')
        v.add(f'<text x="{x + 30}" y="{(ROW["brain"] + ROW["drv"] + 60) / 2}" font-size="12" fill="#047857" '
              f'text-anchor="middle" transform="rotate(-90 {x + 30} {(ROW["brain"] + ROW["drv"] + 60) / 2})">'
              "our board, same size</text>")
    # 6: the door itself (always kept)
    arrow(v, cx, ROW["drv"] + 70, cx, ROW["door"] - 8, "#94a3b8" if k < 2 else "#10b981", 3)
    box(v, cx, ROW["door"], 340, 56, "#fff7ed", "#fb923c", "The door itself, kept",
        "motor · encoder · 8×8 thermal sensor · case", "#9a3412", "#c2410c")
    # status pill
    v.rect(cx - 150, H - 74, 300, 32, "#ffffff", c["scol"], rx=16, sw=2)
    v.text(cx, H - 52, c["status"], 15, c["scol"], weight="bold")
    if k < 2:
        v.add(f'<polygon points="{x + CW + 3},{H / 2 - 12} {x + CW + 15},{H / 2} {x + CW + 3},{H / 2 + 12}" '
              f'fill="#64748b"/>')
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

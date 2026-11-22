"""Generate docs/wiring.svg: the STM/imp end of the Wayzn WYP-BC-100 board, roughly to scale, traced from
photos/2026-11-20_bench_stm-imp-connectors.jpeg (coordinates in that photo's 1400 px preview; the 0.1" header
pitch is 35 px, so 1 px ~ 0.073 mm), with the ESP32 connections."""
import sys

out = []
W = out.append
MM = 35 / 2.54  # px per mm

# ---------------------------------------------------------------- canvas
W('<svg xmlns="http://www.w3.org/2000/svg" viewBox="-40 0 1310 1290" width="1310" height="1290" '
  'font-family="Helvetica, Arial, sans-serif">')
W('<title>Wiring an ESP32 to the Wayzn WYP-BC-100 board</title>')
W('<rect x="-40" y="0" width="1310" height="1290" fill="#f8fafc"/>')
W('<text x="-20" y="36" font-size="26" font-weight="bold" fill="#0f172a">Wiring the ESP32 to the Wayzn board '
  '(WYP-BC-100 Rev 2.6, component side)</text>')
W('<text x="-20" y="62" font-size="15" fill="#475569">Traced from photos/2026-11-20_bench_stm-imp-connectors.jpeg, '
  'roughly to scale. The board continues to the left (motor and 12 V end).</text>')

# ---------------------------------------------------------------- board
# Left edge drawn as a break: the board continues off the drawing.
brk = " ".join(f"{(-6 if i % 2 else 6)},{410 + i * 485 / 16:.0f}" for i in range(17))
W(f'<polygon points="{brk} 1225,895 1225,410" fill="#1e3a8a" stroke="#0b1b4d" stroke-width="2"/>')
W('<polyline points="' + brk + '" fill="none" stroke="#94a3b8" stroke-width="3" stroke-dasharray="6 5"/>')


def rect(x0, y0, x1, y1, fill, stroke="none", rx=2, sw=1.5, extra=""):
    W(f'<rect x="{x0}" y="{y0}" width="{x1 - x0}" height="{y1 - y0}" rx="{rx}" fill="{fill}" stroke="{stroke}" '
      f'stroke-width="{sw}" {extra}/>')


def text(x, y, s, size=14, fill="#e2e8f0", anchor="start", weight="normal", rot=None):
    t = f' transform="rotate({rot} {x} {y})"' if rot is not None else ""
    W(f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}"{t}>'
      f'{s}</text>')


SILK = "#e2e8f0"
GOLD = "#d4a24c"

# Mounting holes
for cx, cy in [(1178, 451), (1186, 862)]:
    W(f'<circle cx="{cx}" cy="{cy}" r="30" fill="#f8fafc" stroke="{GOLD}" stroke-width="6"/>')

# imp004m module (U200): green carrier, metal can
rect(98, 412, 366, 650, "#166534", "#14532d")
rect(110, 452, 360, 645, "#a8a29e", "#78716c", rx=6)
text(235, 545, "U200", 20, "#1c1917", "middle", "bold")
text(235, 570, "imp004m (Wi-Fi)", 16, "#1c1917", "middle")
text(235, 592, "held in reset", 14, "#7f1d1d", "middle", "bold")

# J105: 2x4 socket
rect(425, 419, 498, 535, "#111827", "#000")
for cx in (443, 480):
    for cy in (435, 463, 490, 518):
        rect(cx - 9, cy - 9, cx + 9, cy + 9, "#374151")
text(462, 553, "J105", 13, SILK, "middle")

# IMP_RST switch (SW1) with its 4 pads
rect(512, 437, 546, 487, "#cbd5e1", "#64748b", rx=3)
W('<circle cx="529" cy="462" r="11" fill="#334155"/>')
for cx, cy in [(520, 435), (542, 435), (520, 489), (542, 489)]:
    rect(cx - 6, cy - 4, cx + 6, cy + 4, "#e5e7eb", "#94a3b8", rx=1, sw=1)
text(529, 427, "IMP_RST", 14, SILK, "middle", "bold")

# U210 (SOIC-8 flash)
rect(601, 485, 673, 545, "#1f2937", "#000")
for i in range(4):
    x = 610 + i * 18
    rect(x - 3, 470, x + 3, 485, "#cbd5e1")
    rect(x - 3, 545, x + 3, 560, "#cbd5e1")
text(637, 520, "U210", 13, "#94a3b8", "middle")

# SW300 = EXP RST = STM32 NRST
rect(535, 608, 571, 658, "#cbd5e1", "#64748b", rx=3)
W('<circle cx="553" cy="633" r="11" fill="#334155"/>')
for cx, cy in [(541, 606), (566, 606), (541, 660), (566, 660)]:
    rect(cx - 6, cy - 4, cx + 6, cy + 4, "#e5e7eb", "#94a3b8", rx=1, sw=1)
text(553, 690, "SW300", 13, SILK, "middle", "bold")
text(553, 706, "EXP RST", 13, SILK, "middle")

# STM+IMP header (unpopulated, 0.1"): 3V3 (square), TX, RX, GND top to bottom
rect(813, 433, 846, 574, "none", SILK, rx=1, sw=2)
text(829, 425, "STM+IMP", 14, SILK, "middle", "bold")
for (cy, name, sq) in [(450, "3V3", True), (485, "TX", False), (520, "RX", False), (556, "GND", False)]:
    if sq:
        rect(820, cy - 9, 838, cy + 9, GOLD, rx=1)
    else:
        W(f'<circle cx="829" cy="{cy}" r="9" fill="{GOLD}"/>')
    W(f'<circle cx="829" cy="{cy}" r="3.5" fill="#0b1b4d"/>')
    text(806, cy + 5, name, 13, SILK, "end")

# TP1 / TP2: the I2C bus (J2 SCL / DATA)
rect(787, 601, 811, 658, "none", SILK, rx=1, sw=1.5)
for cy, name in [(614, "TP1"), (645, "TP2")]:
    W(f'<circle cx="799" cy="{cy}" r="8" fill="{GOLD}"/><circle cx="799" cy="{cy}" r="3.5" fill="#0b1b4d"/>')
    text(819, cy + 5, name, 13, SILK, "start")

# U300 STM32F051 (LQFP32)
rect(655, 693, 755, 793, "#111827", "#000")
for i in range(8):
    p = 663 + i * 12
    rect(p - 2, 683, p + 2, 693, "#cbd5e1"); rect(p - 2, 793, p + 2, 803, "#cbd5e1")
for i in range(8):
    p = 701 + i * 12
    rect(645, p - 2, 655, p + 2, "#cbd5e1"); rect(755, p - 2, 765, p + 2, "#cbd5e1")
text(705, 740, "U300", 15, "#94a3b8", "middle", "bold")
text(705, 758, "STM32F051", 12, "#94a3b8", "middle")
text(705, 774, "motor control", 11, "#94a3b8", "middle")

# U50 accelerometer
rect(862, 630, 896, 664, "#1f2937", "#000")
text(879, 682, "U50", 12, SILK, "middle")

# JP300 (SWD / USART2)
rect(862, 728, 962, 812, "none", SILK, rx=2, sw=1.5)
for cx, cy in [(880, 745), (880, 795), (944, 745), (944, 795)]:
    W(f'<circle cx="{cx}" cy="{cy}" r="10" fill="{GOLD}" opacity="0.8"/>')
for cx in (896, 912, 928):
    for cy in (762, 778):
        W(f'<circle cx="{cx}" cy="{cy}" r="4" fill="{GOLD}"/>')
text(912, 830, "JP300", 12, SILK, "middle")

# J2 IR SENSOR and J3 encoder connectors
for (y0, y1, name, pins, py) in [(523, 607, "J2  IR SENSOR", ["GND", "SCL", "DATA", "3V3"], 625),
                                 (669, 765, "J3  encoder", ["GND", "SIG B", "SIG A", "3V3"], 780)]:
    rect(1003, y0, 1226, y1, "#f5f0e1", "#a8a29e", rx=3)
    rect(1040, y0 + 18, 1196, y1 - 18, "#e7dfc6", "#c8bfa3", rx=2)
    text(1110, y0 + (y1 - y0) / 2 + 5, name, 13, "#57534e", "middle", "bold")
    for i, pn in enumerate(pins):
        x = 1047 + i * 31
        rect(x - 4, y1, x + 4, py, "#cbd5e1")
        text(x, py + 16, pn, 10, SILK, "middle")

# Other features for orientation
for i, (x, l) in enumerate([(196, "TP3"), (228, "TP5"), (260, "TP7"), (293, "TP8")]):
    W(f'<circle cx="{x}" cy="741" r="7" fill="{GOLD}"/>')
    text(x, 726, l, 10, SILK, "middle")
text(968, 470, "wayzn", 34, "#c08a4a", "start")
text(430, 838, "wayzn", 30, SILK, "start")
text(430, 868, "WYP-BC-100 Rev 2.6", 15, SILK, "start")

# Scale bar (10 mm)
x0, y0 = 40, 860
W(f'<line x1="{x0}" y1="{y0}" x2="{x0 + 10 * MM:.0f}" y2="{y0}" stroke="{SILK}" stroke-width="3"/>')
for x in (x0, x0 + 10 * MM):
    W(f'<line x1="{x:.0f}" y1="{y0 - 6}" x2="{x:.0f}" y2="{y0 + 6}" stroke="{SILK}" stroke-width="3"/>')
text(x0 + 5 * MM, y0 - 10, "10 mm", 13, SILK, "middle")

# ---------------------------------------------------------------- ESP32
EX0, EY0, EX1, EY1 = 520, 100, 1010, 250
rect(EX0, EY0, EX1, EY1, "#0f172a", "#020617", rx=10, sw=2)
rect(EX0 + 120, EY0 + 95, EX0 + 200, EY0 + 140, "#9ca3af", "#6b7280", rx=4)
text(EX0 + 160, EY0 + 124, "ESP32", 14, "#111827", "middle", "bold")
text(EX0 + 20, EY0 + 38, "ESP32 dev board", 20, "#f8fafc", "start", "bold")
text(EX0 + 20, EY0 + 62, "(DevKitC / WROOM-32)", 14, "#cbd5e1")
text(EX0 + 20, EY0 + 84, "firmware/brain", 14, "#cbd5e1")

# Wires: (ESP pin x, label, colour, path to target, note)
wires = [
    (570, "GPIO27", "#f97316", [(570, 606)], "SW300 top-right pad = STM32 NRST"),
    (740, "GPIO22", "#eab308", [(740, 645), (791, 645)], "TP2 = I2C SDA (DATA)"),
    (770, "GPIO21", "#a855f7", [(770, 614), (791, 614)], "TP1 = I2C SCL"),
    (870, "3V3", "#dc2626", [(870, 450), (838, 450)], "header 3V3 powers the ESP32"),
    (900, "GPIO26", "#16a34a", [(900, 485), (838, 485)], "header TX = STM32 TX, into the ESP32 (its UART RX)"),
    (930, "GPIO25", "#2563eb", [(930, 520), (838, 520)], "header RX = STM32 RX, driven by the ESP32 (its UART TX)"),
    (960, "GND", "#111827", [(960, 556), (838, 556)], "header GND"),
]
for px, name, col, path, _ in wires:
    pts = [(px, EY1)] + path
    d = "M " + " L ".join(f"{x},{y}" for x, y in pts)
    W(f'<path d="{d}" fill="none" stroke="#f8fafc" stroke-width="8" stroke-linejoin="round" stroke-linecap="round"/>')
    W(f'<path d="{d}" fill="none" stroke="{col}" stroke-width="5" stroke-linejoin="round" stroke-linecap="round"/>')
    W(f'<circle cx="{pts[-1][0]}" cy="{pts[-1][1]}" r="5" fill="{col}" stroke="#f8fafc" stroke-width="1.5"/>')
    rect(px - 7, EY1 - 7, px + 7, EY1 + 7, "#d1d5db", "#4b5563", rx=1)
    text(px + 5, EY1 - 14, name, 13, "#f8fafc", "start", "bold", rot=-90)

# IMP_RST jumper: top-right pad to bottom-left pad (shorts the switch: imp held in reset)
W('<path d="M 542,435 C 560,455 505,465 520,489" fill="none" stroke="#f8fafc" stroke-width="8" stroke-linecap="round"/>')
W('<path d="M 542,435 C 560,455 505,465 520,489" fill="none" stroke="#e11d48" stroke-width="5" stroke-linecap="round"/>')
for cx, cy in [(542, 435), (520, 489)]:
    W(f'<circle cx="{cx}" cy="{cy}" r="5" fill="#e11d48" stroke="#f8fafc" stroke-width="1.5"/>')

# Callouts on the board
def callout(x, y, tx, ty, lines, col):
    W(f'<line x1="{x}" y1="{y}" x2="{tx}" y2="{ty}" stroke="{col}" stroke-width="2" stroke-dasharray="4 3"/>')
    for i, l in enumerate(lines):
        W(f'<text x="{tx}" y="{ty + 18 * i + 5}" font-size="14" fill="#0f172a" font-weight="{"bold" if i == 0 else "normal"}" '
          f'text-anchor="end">{l}</text>')

callout(520, 489, 380, 330, ["Jumper across IMP_RST:", "top-right pad to bottom-left pad,", "holds the imp in reset"], "#e11d48")

# ---------------------------------------------------------------- legend
LY = 930
W(f'<text x="-20" y="{LY}" font-size="20" font-weight="bold" fill="#0f172a">Connections</text>')
rows = [("#e11d48", "jumper", "IMP_RST (SW1): top-right pad to bottom-left pad, on the board", "holds the imp004m in reset so it never drives the bus")]
rows += [(col, name, note, "") for _, name, col, _, note in wires]
for i, (col, name, note, extra) in enumerate(rows):
    y = LY + 32 + i * 28
    W(f'<line x1="-15" y1="{y - 5}" x2="25" y2="{y - 5}" stroke="{col}" stroke-width="6" stroke-linecap="round"/>')
    text(40, y, name, 15, "#0f172a", "start", "bold")
    text(140, y, note + (f" — {extra}" if extra else ""), 15, "#1e293b")
ny = LY + 32 + len(rows) * 28 + 8
notes = [
    "Power: the ESP32 runs from the board's 3V3 (header pin) into its 3V3 pin, not VIN.",
    "Never connect the ESP32's USB while the 3V3 wire is connected. The board's pull-ups serve the I2C bus.",
    "GPIO27 drives NRST open-drain (pulled low briefly): the STM32 doesn't always start on a cold power-up.",
]
for i, n in enumerate(notes):
    text(-20, ny + 22 * i, n, 14, "#334155")
W('</svg>')
open(sys.argv[1], 'w').write("\n".join(out))

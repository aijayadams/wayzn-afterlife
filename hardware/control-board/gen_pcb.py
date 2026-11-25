#!/usr/bin/env python3
"""Generate control.kicad_pcb from design.py + layout.py: 4-layer board, outline, footprints with
nets, placement, the hand-defined signal routes, power fanout (a via per power pad), GND/3V3 planes, GND
stitching, silkscreen. Nothing is autorouted.
Run with KiCad's Python:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 gen_pcb.py"""
import os
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import design  # noqa: E402

KICAD_FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
LOCAL_FP = {"Espressif": os.path.join(HERE, "lib/Espressif.pretty")}
mm = pcbnew.FromMM
PAGE_OFFSET = (40.0, 30.0)  # design frame origin on the A4 page (mm)

from layout import PLACE, ROUTES, SILK  # noqa: E402


def load_fp(libref):
    lib, name = libref.split(":")
    path = LOCAL_FP.get(lib) or os.path.join(KICAD_FP, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise SystemExit(f"footprint not found: {libref}")
    return fp


def outline(board):
    x0, x1 = design.OUTLINE["x"]
    y0, y1 = design.OUTLINE["y"]
    r = design.OUTLINE["corner_r"]
    def seg(a, b):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pcbnew.VECTOR2I(mm(a[0]), mm(a[1]))); s.SetEnd(pcbnew.VECTOR2I(mm(b[0]), mm(b[1])))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.05)); board.Add(s)
    def arc(c, start, mid, end):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
        s.SetArcGeometry(*(pcbnew.VECTOR2I(mm(p[0]), mm(p[1])) for p in (start, mid, end)))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.05)); board.Add(s)
    k = r * (1 - 0.70710678)
    seg((x0 + r, y0), (x1 - r, y0)); seg((x1, y0 + r), (x1, y1 - r))
    seg((x1 - r, y1), (x0 + r, y1)); seg((x0, y1 - r), (x0, y0 + r))
    arc(None, (x1 - r, y0), (x1 - k, y0 + k), (x1, y0 + r))
    arc(None, (x1, y1 - r), (x1 - k, y1 - k), (x1 - r, y1))
    arc(None, (x0 + r, y1), (x0 + k, y1 - k), (x0, y1 - r))
    arc(None, (x0, y0 + r), (x0 + k, y0 + k), (x0 + r, y0))


def zone(board, net, layer, prio=0):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer); z.SetNet(net); z.SetAssignedPriority(prio)
    z.SetLocalClearance(mm(0.25)); z.SetMinThickness(mm(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL); z.SetThermalReliefGap(mm(0.25))  # SMD solid
    z.SetThermalReliefSpokeWidth(mm(0.4))
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)  # no floating copper fragments
    x0, x1 = design.OUTLINE["x"]; y0, y1 = design.OUTLINE["y"]
    ol = z.Outline(); ol.NewOutline()
    for x, y in [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]:
        ol.Append(mm(x), mm(y))
    board.Add(z)


def wayzn_landmarks(board):
    """Features of the original WYP-BC-100 that our parts must line up with, on Cmts.User (fit print, review)."""
    W = design.WAYZN
    def line(a, b):
        sh = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        sh.SetStart(pcbnew.VECTOR2I(mm(a[0]), mm(a[1]))); sh.SetEnd(pcbnew.VECTOR2I(mm(b[0]), mm(b[1])))
        sh.SetLayer(pcbnew.Cmts_User); sh.SetWidth(mm(0.1)); board.Add(sh)
    def rect(x0, x1, y0, y1):
        for a, b in [((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))]:
            line(a, b)
    def circle(c, r):
        sh = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_CIRCLE)
        sh.SetCenter(pcbnew.VECTOR2I(mm(c[0]), mm(c[1]))); sh.SetEnd(pcbnew.VECTOR2I(mm(c[0] + r), mm(c[1])))
        sh.SetLayer(pcbnew.Cmts_User); sh.SetWidth(mm(0.1)); board.Add(sh)
    def text(t, x, y):
        tx = pcbnew.PCB_TEXT(board); tx.SetText(t); tx.SetLayer(pcbnew.Cmts_User)
        tx.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y))); tx.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
        tx.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT); board.Add(tx)
    for c in W["j7_pins"] + W["j6_pins"] + W["button_contacts"] + W["button_pegs"]:
        circle(c, 0.3)  # Wayzn pad centres: ours must sit on them
    rect(*W["light_pipe"]["x"], *W["light_pipe"]["y"]); text("LIGHT PIPE", W["light_pipe"]["x"][1] + 0.5, W["light_pipe"]["y"][0])
    rect(*W["j5"]["x"], *W["j5"]["y"]); text("WAYZN J5", W["j5"]["x"][0] - 9, W["j5"]["y"][0])
    rect(*W["j2"]["x"], *W["j2"]["y"]); text("WAYZN J2", W["j2"]["x"][1] + 0.5, W["j2"]["y"][0])
    text("Circles/boxes: WYP-BC-100 features from the 1200 dpi scans (docs/hardware/WYP-BC-100-MECHANICAL.md)", 0, 156)


def pad_pos(board, spec):
    ref, num = spec.split(".")
    for fp in board.GetFootprints():
        if fp.GetReference() == ref:
            for p in fp.Pads():
                if p.GetNumber() == num:
                    return p.GetPosition()
    raise SystemExit(f"route: no pad {spec}")


def apply_routes(board, nets):
    """layout.ROUTES: net -> [("F"|"B", [points]) tracks, ("V", [point]) vias]; points are (x, y) mm or "REF.PAD"."""
    layers = {"F": pcbnew.F_Cu, "B": pcbnew.B_Cu}
    for net, items in ROUTES.items():
        ni = nets[net]
        w = mm(design.RULES["motor_track"] if net in design.MOTOR_NETS else
               design.RULES["power_track"] if net in (design.G, design.V) else design.RULES["track"])
        for item in items:
            kind, pts = item[0], item[1]
            tw = mm(item[2]) if len(item) > 2 else w
            P = [pad_pos(board, p) if isinstance(p, str) else pcbnew.VECTOR2I(mm(p[0]), mm(p[1])) for p in pts]
            if kind == "V":  # ("V", [pt]) or ("V", [pt], diameter, drill)
                d, dr = (item[2], item[3]) if len(item) > 3 else (design.RULES["via_d"], design.RULES["via_drill"])
                for pt in P:
                    v = pcbnew.PCB_VIA(board); v.SetPosition(pt); v.SetWidth(mm(d))
                    v.SetDrill(mm(dr)); v.SetNet(ni); board.Add(v)
                continue
            for a, b in zip(P, P[1:]):
                t = pcbnew.PCB_TRACK(board); t.SetStart(a); t.SetEnd(b); t.SetWidth(tw)
                t.SetLayer(layers[kind]); t.SetNet(ni); board.Add(t)


def silkscreen(board):
    """Footprint reference text goes to the assembly layer (F.Fab); footprint silk that leaves the board is
    dropped; the board gets only the curated labels in layout.SILK."""
    x0, x1 = (mm(v) for v in design.OUTLINE["x"]); y0, y1 = (mm(v) for v in design.OUTLINE["y"])
    inset = mm(0.3)
    for fp in board.GetFootprints():
        r = fp.Reference()
        r.SetLayer(pcbnew.B_Fab if fp.IsFlipped() else pcbnew.F_Fab)
        r.SetTextSize(pcbnew.VECTOR2I(mm(0.6), mm(0.6))); r.SetTextThickness(mm(0.1))
        r.SetPosition(fp.GetPosition())          # reference: assembly layer, centred on the part
        fp.Value().SetVisible(False)             # values: in the BOM, not drawn
        for item in list(fp.GraphicalItems()):
            if item.GetLayer() not in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                continue
            bb = item.GetBoundingBox()
            if bb.GetLeft() < x0 + inset or bb.GetRight() > x1 - inset or bb.GetTop() < y0 + inset or bb.GetBottom() > y1 - inset:
                fp.Remove(item)
    labels = []
    just = {"L": pcbnew.GR_TEXT_H_ALIGN_LEFT, "C": pcbnew.GR_TEXT_H_ALIGN_CENTER, "R": pcbnew.GR_TEXT_H_ALIGN_RIGHT}
    for text, x, y, size, side, j, rot in SILK:
        t = pcbnew.PCB_TEXT(board); t.SetText(text)
        t.SetLayer(pcbnew.F_SilkS if side == "F" else pcbnew.B_SilkS)
        t.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y))); t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
        t.SetTextThickness(mm(0.15)); t.SetHorizJustify(just[j]); t.SetTextAngleDegrees(rot)
        if side == "B":
            t.SetMirrored(True)
        board.Add(t)
        labels.append((t.GetBoundingBox(), t))
    # Footprint silk that runs into a curated label goes (connector body outlines, mostly under the part anyway).
    for fp in board.GetFootprints():
        for item in list(fp.GraphicalItems()):
            if item.GetLayer() not in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                continue
            ib = item.GetBoundingBox()
            for bb, t in labels:
                g = pcbnew.BOX2I(bb.GetOrigin(), bb.GetSize()); g.Inflate(mm(0.15))
                if t.GetLayer() == item.GetLayer() and g.Intersects(ib):
                    fp.Remove(item)
                    break
    return labels


def silk_check(board, labels):
    """Every label inside the board (0.3 mm), clear of pads, vias and other labels on its side; every footprint
    silk item inside the board. Returns the number of problems."""
    x0, x1 = (mm(v) for v in design.OUTLINE["x"]); y0, y1 = (mm(v) for v in design.OUTLINE["y"])
    inset, gap = mm(0.3), mm(0.1)
    bad = 0
    def inside(bb):
        return bb.GetLeft() >= x0 + inset and bb.GetRight() <= x1 - inset and bb.GetTop() >= y0 + inset and bb.GetBottom() <= y1 - inset
    for fp in board.GetFootprints():
        for item in fp.GraphicalItems():
            if item.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS) and not inside(item.GetBoundingBox()):
                print(f"  silk: {fp.GetReference()} graphic leaves the board"); bad += 1
    for i, (bb, t) in enumerate(labels):
        name, layer = t.GetText(), t.GetLayer()
        if not inside(bb):
            print(f"  silk: '{name}' leaves the board"); bad += 1
        g = pcbnew.BOX2I(bb.GetOrigin(), bb.GetSize()); g.Inflate(gap)
        for fp in board.GetFootprints():
            for p in fp.Pads():
                on = p.IsOnLayer(pcbnew.F_Cu if layer == pcbnew.F_SilkS else pcbnew.B_Cu)
                if on and g.Intersects(p.GetBoundingBox()):
                    print(f"  silk: '{name}' overlaps {fp.GetReference()}.{p.GetNumber()}"); bad += 1
        for t2 in board.GetTracks():
            if isinstance(t2, pcbnew.PCB_VIA) and g.Intersects(t2.GetBoundingBox()):
                print(f"  silk: '{name}' overlaps a via at {pcbnew.ToMM(t2.GetPosition().x):.1f},{pcbnew.ToMM(t2.GetPosition().y):.1f}"); bad += 1
        for bb2, t3 in labels[i + 1:]:
            if t3.GetLayer() == layer and g.Intersects(bb2):
                print(f"  silk: '{name}' overlaps '{t3.GetText()}'"); bad += 1
    print("silkscreen check:", "OK" if not bad else f"{bad} problems")
    return bad


def court(fp, layer):
    c = fp.GetCourtyard(layer)
    return c.BBox() if c.OutlineCount() else None


# Deliberate exceptions, matching the Wayzn board: the VH housings sit 0.84 mm apart (their courtyards overlap) and
# overhang the board end; the button plunger sticks out past the long edge.
ALLOWED_PAIRS = {("J6", "J7"), ("J7", "J6")}
OVERHANG = {"J6", "J7", "SW3"}
POST_KEEP = 2.15 + 0.4  # 4.3 mm hole radius + margin


def check_overlaps(board):
    """Courtyard bounding boxes that overlap on the same side or leave the outline (THT parts count on both sides),
    and any courtyard within POST_KEEP of a shell post's centre."""
    x0, x1 = (mm(v) for v in design.OUTLINE["x"]); y0, y1 = (mm(v) for v in design.OUTLINE["y"])
    fps = list(board.GetFootprints())
    bad = 0
    for side, layer in (("top", pcbnew.F_CrtYd), ("bottom", pcbnew.B_CrtYd)):
        boxes = []
        for fp in fps:
            if fp.GetReference().startswith("H"):
                continue
            th = any(p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH) for p in fp.Pads())
            bb = court(fp, layer)
            if bb is None and th:  # a THT part on the other side: only its pads come through
                for pad in fp.Pads():
                    pb = pad.GetBoundingBox()
                    if bb is None:
                        bb = pb
                    else:
                        bb.Merge(pb)
            if bb:
                boxes.append((fp.GetReference(), bb))
        for i, (ra, a) in enumerate(boxes):
            if ra not in OVERHANG and (a.GetLeft() < x0 or a.GetRight() > x1 or a.GetTop() < y0 or a.GetBottom() > y1):
                print(f"  {side}: {ra} leaves the outline"); bad += 1
            for rb, b in boxes[i + 1:]:
                if (ra, rb) not in ALLOWED_PAIRS and a.Intersects(b):
                    print(f"  {side}: {ra} overlaps {rb}"); bad += 1
    for hx, hy in design.HOLES:
        for fp in fps:
            if fp.GetReference().startswith("H"):
                continue
            for layer in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
                bb = court(fp, layer)
                if not bb:
                    continue
                dx = max(pcbnew.ToMM(bb.GetLeft()) - hx, 0, hx - pcbnew.ToMM(bb.GetRight()))
                dy = max(pcbnew.ToMM(bb.GetTop()) - hy, 0, hy - pcbnew.ToMM(bb.GetBottom()))
                if (dx * dx + dy * dy) ** 0.5 < POST_KEEP:
                    print(f"  {fp.GetReference()} is within {POST_KEEP} mm of the post at ({hx}, {hy})"); bad += 1
                    break
    print("courtyard check:", "OK" if not bad else f"{bad} problems")


def main():
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(4)
    ds = board.GetDesignSettings()
    ds.m_CopperEdgeClearance = mm(0.3)
    parts = design.all_parts()
    nets = {}
    for name in sorted(design.nets()):
        ni = pcbnew.NETINFO_ITEM(board, "/" + name); board.Add(ni); nets[name] = ni  # "/": schematic label nets
    outline(board)
    missing = [r for r in parts if r not in PLACE]
    if missing:
        raise SystemExit(f"no placement for {missing}")
    for ref, p in parts.items():
        fp = load_fp(p["footprint"])
        fp.SetReference(ref); fp.SetValue(p["value"])
        fp.SetFPIDAsString(p["footprint"])
        if p["lcsc"]:
            f = pcbnew.PCB_FIELD(fp, pcbnew.FIELD_T_USER if hasattr(pcbnew, "FIELD_T_USER") else 0, "LCSC")
            f.SetText(p["lcsc"]); f.SetVisible(False); f.SetLayer(pcbnew.F_Fab); fp.Add(f)
        if p["dnp"]:
            fp.SetDNP(True); fp.SetExcludedFromBOM(True); fp.SetExcludedFromPosFiles(True)
        for pad in fp.Pads():
            net = p["pins"].get(pad.GetNumber())
            if net:
                pad.SetNet(nets[net])
            if ref == "U3":  # 0.5 mm-pitch LGA: ~10 % smaller stencil openings, less paste, fewer bridges
                pad.SetLocalSolderPasteMarginRatio(-0.05)  # per side: ~10 % smaller each way
        x, y, rot, side = PLACE[ref]
        fp.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        fp.SetOrientationDegrees(rot)
        board.Add(fp)  # Flip() needs the footprint on a board
        if side == "B":
            fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    zone(board, nets["GND"], pcbnew.In1_Cu)
    zone(board, nets["+3V3"], pcbnew.In2_Cu)
    zone(board, nets["GND"], pcbnew.F_Cu)
    zone(board, nets["GND"], pcbnew.B_Cu)
    apply_routes(board, nets)
    import route
    route.ESCAPES = {}            # the accelerometer's escapes are hand-routed in layout.ROUTES
    labels = silkscreen(board)
    if not os.environ.get("NOFANOUT"):  # NOFANOUT=1: placement and pads only, for planning
        route.fanout(board, avoid=[bb for bb, _ in labels])
    if not os.environ.get("NOSTITCH"):  # NOSTITCH=1 while planning routes: stitching fills the free space
        keep = route.stitch_keepouts(board)
        for bb, _ in labels:  # the keepout is tested against via centres: grow it by a via radius + gap
            g = pcbnew.BOX2I(bb.GetOrigin(), bb.GetSize()); g.Inflate(mm(design.RULES["via_d"] / 2 + 0.15))
            keep.append(g)
        print("stitching vias:", route.stitch(board, route.Space(board), keep))
    if silk_check(board, labels):
        raise SystemExit("silkscreen check failed")
    wayzn_landmarks(board)
    check_overlaps(board)
    # Put the design on the page (PDF plots use page coordinates); the drill/aux and grid origins mark the board
    # corner (the frame's origin) so fab outputs use it.
    off = pcbnew.VECTOR2I(mm(PAGE_OFFSET[0]), mm(PAGE_OFFSET[1]))
    for item in list(board.GetDrawings()) + list(board.GetFootprints()) + list(board.Zones()) + list(board.GetTracks()):
        item.Move(off)
    board.GetDesignSettings().SetAuxOrigin(off)
    board.GetDesignSettings().SetGridOrigin(off)
    filler = pcbnew.ZONE_FILLER(board); filler.Fill(board.Zones())
    out = os.path.join(HERE, "control.kicad_pcb")
    board.Save(out)
    design.write_project(os.path.join(HERE, "control.kicad_pro"))  # KiCad rewrites it on save
    print("wrote", out, len(parts), "parts,", len(nets), "nets")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Board helpers used by gen_pcb.py: power fanout (a short track and a via from each 3V3/GND pad to its plane),
GND stitching vias, and the clearance model (Space) both use. Signals are hand-routed in layout.py."""
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(HERE, "carrier.kicad_pcb")
PRO = os.path.join(HERE, "carrier.kicad_pro")


mm = pcbnew.FromMM
VIA_D, VIA_DRILL, CLEAR = 0.6, 0.3, 0.2
SEG_CLEAR = design.RULES["clearance"] + 0.01  # feed tracks: the board rule, a hair over
HOLE_GAP = 0.35   # mm between drilled holes (JLC: 0.25 minimum)
# Pads that get a via to the planes: 3V3 everywhere, and GND on the small parts (the modules' many GND pads
# reach the top GND pour directly).
FANOUT_NETS = ("/+3V3", "/GND")
NO_GND_FANOUT = ("U1", "U2", "J2", "J3", "J4", "J5", "J1")
HAND_POWER = ("U3",)


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0 if L == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L))
    qx, qy = ax + t * dx, ay + t * dy
    return ((px - qx) ** 2 + (py - qy) ** 2) ** 0.5


class Space:
    """Copper of other nets around a candidate via (+ its feed track), in nm, all copper layers lumped
    together (a through via touches every layer). Pads are bounding boxes; tracks are segments."""

    def __init__(self, b):
        self.edge = b.GetBoardEdgesBoundingBox()
        self.pads = [(p.GetBoundingBox(), p.GetNetname()) for fp in b.GetFootprints() for p in fp.Pads()]
        # Drilled holes of any net: hole-to-hole spacing is a fab rule, net or not.
        self.holes = [(p.GetPosition(), max(p.GetDrillSize().x, p.GetDrillSize().y) / 2)
                      for fp in b.GetFootprints() for p in fp.Pads() if p.GetDrillSize().x > 0]
        self.vias, self.segs = [], []
        for t in b.GetTracks():
            if isinstance(t, pcbnew.PCB_VIA):
                self.vias.append((t.GetPosition(), t.GetWidth(pcbnew.F_Cu) / 2, t.GetNetname()))
            else:
                s, e = t.GetStart(), t.GetEnd()
                self.segs.append((s.x, s.y, e.x, e.y, t.GetWidth() / 2, t.GetNetname()))

    def via_ok(self, pos, net, r=None):
        r = r if r is not None else mm(VIA_D / 2)
        c = mm(CLEAR)
        e = self.edge
        if not (e.GetLeft() + mm(0.8) < pos.x < e.GetRight() - mm(0.8) and e.GetTop() + mm(0.8) < pos.y < e.GetBottom() - mm(0.8)):
            return False
        for bb, n in self.pads:
            if n == net:
                continue
            dx = max(bb.GetLeft() - pos.x, 0, pos.x - bb.GetRight())
            dy = max(bb.GetTop() - pos.y, 0, pos.y - bb.GetBottom())
            if (dx * dx + dy * dy) ** 0.5 < r + c:
                return False
        for h, hr in self.holes:
            if (h - pos).EuclideanNorm() < hr + mm(VIA_DRILL / 2) + mm(HOLE_GAP):
                return False
        for v, vr, n in self.vias:
            d = (v - pos).EuclideanNorm()
            if d < r + vr + (c if n != net else mm(0.1)):
                return False
        for ax, ay, bx, by, w, n in self.segs:
            if n != net and seg_dist(pos.x, pos.y, ax, ay, bx, by) < r + w + c:
                return False
        return True

    def seg_ok(self, a, b, half_w, net, steps=8):
        for i in range(steps + 1):
            p = pcbnew.VECTOR2I(int(a.x + (b.x - a.x) * i / steps), int(a.y + (b.y - a.y) * i / steps))
            for bb, n in self.pads:
                if n == net:
                    continue
                dx = max(bb.GetLeft() - p.x, 0, p.x - bb.GetRight())
                dy = max(bb.GetTop() - p.y, 0, p.y - bb.GetBottom())
                if (dx * dx + dy * dy) ** 0.5 < half_w + mm(SEG_CLEAR):
                    return False
            for v, vr, n in self.vias:
                if n != net and (v - p).EuclideanNorm() < vr + half_w + mm(SEG_CLEAR):
                    return False
        return True

    @property
    def track_ends(self):
        return [(ax, ay) for ax, ay, bx, by, w, n in self.segs] + [(bx, by) for ax, ay, bx, by, w, n in self.segs]

    def add_via(self, pos, net):
        self.vias.append((pos, mm(VIA_D / 2), net))

    def add_seg(self, a, b, half_w, net):
        self.segs.append((a.x, a.y, b.x, b.y, half_w, net))


# Pre-routed escapes for fine-pitch signal pads (unit direction, via distance mm): the LIS2DH12's corner
# and edge signals get out before the power vias box them in.
ESCAPES = {("U3", "1"): ((-0.707, -0.707), 1.05), ("U3", "4"): ((-0.707, 0.707), 1.05),
           ("U3", "12"): ((0.0, -1.0), 1.15)}
ESCAPE_W = 0.15


def escapes(b, sp):
    n = 0
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            key = (fp.GetReference(), pad.GetNumber())
            if key not in ESCAPES:
                continue
            (ux, uy), dist = ESCAPES[key]
            p = pad.GetPosition()
            pos = pcbnew.VECTOR2I(int(p.x + ux * mm(dist)), int(p.y + uy * mm(dist)))
            if not sp.via_ok(pos, pad.GetNetname()):
                print(f"  escape {key}: no room")
                continue
            v = pcbnew.PCB_VIA(b); v.SetPosition(pos); v.SetWidth(mm(VIA_D)); v.SetDrill(mm(VIA_DRILL))
            v.SetNet(pad.GetNet()); b.Add(v)
            t = pcbnew.PCB_TRACK(b); t.SetStart(p); t.SetEnd(pos); t.SetWidth(mm(ESCAPE_W)); t.SetLayer(pcbnew.F_Cu)
            t.SetNet(pad.GetNet()); b.Add(t)
            sp.add_via(pos, pad.GetNetname()); sp.add_seg(p, pos, mm(ESCAPE_W) / 2, pad.GetNetname()); n += 1
    print(f"escapes: {n} of {len(ESCAPES)}")


def bb_has(pad, pt):
    bb = pad.GetBoundingBox()
    return bb.GetLeft() <= pt[0] <= bb.GetRight() and bb.GetTop() <= pt[1] <= bb.GetBottom()


def fanout(b, avoid=()):
    """A short track and a through via beside each power pad, so every pad reaches its plane (In1 GND,
    In2 3V3). Tries directions outward from the part first; skips a spot that would crowd another net."""
    sp = Space(b)
    for bb in avoid:  # silkscreen labels: keep vias out of them
        g = pcbnew.BOX2I(bb.GetOrigin(), bb.GetSize()); g.Inflate(mm(0.1))
        sp.pads.append((g, "__silk__"))
    escapes(b, sp)
    placed = skipped = 0
    for fp in b.GetFootprints():
        ref = fp.GetReference()
        if ref.startswith("H"):
            continue
        c = fp.GetPosition()
        for pad in fp.Pads():
            net = pad.GetNetname()
            if net not in FANOUT_NETS or pad.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
                continue
            if net == "/GND" and ref in NO_GND_FANOUT:
                continue
            if ref in HAND_POWER:  # fine-pitch: power hand-routed to its caps (layout.ROUTES), which get the vias
                continue
            p = pad.GetPosition()
            d = p - c
            n = d.EuclideanNorm() or 1
            ux, uy = d.x / n, d.y / n
            if fp.GetReference().startswith("U3") or abs(ux) > 0.6 or abs(uy) > 0.6:
                ux, uy = (1.0 if ux > 0 else -1.0, 0.0) if abs(ux) >= abs(uy) else (0.0, 1.0 if uy > 0 else -1.0)
            s = max(pad.GetSize().x, pad.GetSize().y) / 2 + mm(VIA_D / 2 + 0.25)
            w = min(mm(0.3), min(pad.GetSize().x, pad.GetSize().y))
            cands = [(ux, uy), (-uy, ux), (uy, -ux), (0, 1), (0, -1), (1, 0), (-1, 0)]
            hit = None
            for k in (1.0, 1.4, 1.9, 2.5):
                for cx, cy in cands:
                    pos = pcbnew.VECTOR2I(int(p.x + cx * s * k), int(p.y + cy * s * k))
                    if sp.via_ok(pos, net) and sp.seg_ok(p, pos, w / 2, net):
                        hit = pos
                        break
                if hit:
                    break
            if not hit:
                skipped += 1
                print(f"  no room for a via at {ref}.{pad.GetNumber()} ({net})")
                continue
            v = pcbnew.PCB_VIA(b); v.SetPosition(hit); v.SetWidth(mm(VIA_D)); v.SetDrill(mm(VIA_DRILL))
            v.SetNet(pad.GetNet()); b.Add(v)
            t = pcbnew.PCB_TRACK(b); t.SetStart(p); t.SetEnd(hit); t.SetWidth(w); t.SetLayer(pcbnew.F_Cu)
            t.SetNet(pad.GetNet()); b.Add(t)
            sp.add_via(hit, net); sp.add_seg(p, hit, w / 2, net); placed += 1
    print(f"fanout: {placed} vias, {skipped} pads without room")


STITCH_PITCH = 3.0  # mm
NO_STITCH = ("U1", "U2", "U3", "J1", "J2", "J3", "J4", "J5", "H1", "H2")


def stitch_keepouts(b):
    keep = []
    for fp in b.GetFootprints():
        if fp.GetReference() in NO_STITCH:
            for layer in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
                c = fp.GetCourtyard(layer)
                if c.OutlineCount():
                    keep.append(pcbnew.BOX2I(c.BBox().GetOrigin(), c.BBox().GetSize()))
        for p in fp.Pads():  # NPTH slots count as board edge (0.3 mm): keep stitching well clear
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                bb = p.GetBoundingBox(); bb.Inflate(mm(1.0))
                keep.append(pcbnew.BOX2I(bb.GetOrigin(), bb.GetSize()))
    return keep


def stitch(b, sp, keep):
    """GND vias on a grid wherever there is room outside the parts: they tie the top and bottom GND pours
    to the In1 plane (no isolated pour islands) and shorten return paths."""
    gnd = b.FindNet("/GND")
    e = b.GetBoardEdgesBoundingBox()
    n = 0
    y = e.GetTop() + mm(1.2)
    while y < e.GetBottom() - mm(1.0):
        x = e.GetLeft() + mm(1.2)
        while x < e.GetRight() - mm(1.0):
            pos = pcbnew.VECTOR2I(int(x), int(y))
            if not any(k.Contains(pos) for k in keep) and sp.via_ok(pos, "/GND"):
                v = pcbnew.PCB_VIA(b); v.SetPosition(pos); v.SetWidth(mm(VIA_D)); v.SetDrill(mm(VIA_DRILL))
                v.SetNet(gnd); b.Add(v); sp.add_via(pos, "/GND"); n += 1
            x += mm(STITCH_PITCH)
        y += mm(STITCH_PITCH)
    return n

#!/usr/bin/env python3
"""Decode a close captured by the door (syslog lines `frame e=<n> t=<ms> ph=<c> p=<pos> o=<occ> <b64>`).

Input: text containing those lines, one per frame, in any order (e.g. a Loki export, or
`logcli query '{job="..."} |= "frame e=12"'`). Prints, per frame: time from the close start,
door phase (c closing, o opening, r reversing, C closed, O open), position, occupied flags
(live, v2), and the warmest pixels relative to a baseline taken from the first second.

  python3 close_frames.py lines.txt [--event N] [--grid]
"""
import argparse
import base64
import re
import sys

LINE = re.compile(r"frame e=(\d+) t=(-?\d+) ph=(\S) p=(-?\d+) o=(\d) (\S+)")


def decode(b64):
    raw = base64.b64decode(b64)
    return [v / 4.0 - 16.0 for v in raw[:64]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file", nargs="?", default="-")
    ap.add_argument("--event", type=int)
    ap.add_argument("--grid", action="store_true", help="print the 8x8 rise grid for every frame")
    a = ap.parse_args()
    text = sys.stdin.read() if a.file == "-" else open(a.file).read()
    frames = {}
    for m in LINE.finditer(text):
        e, t = int(m.group(1)), int(m.group(2))
        if a.event is not None and e != a.event:
            continue
        frames[(e, t)] = (m.group(3), int(m.group(4)), int(m.group(5)), decode(m.group(6)))
    if not frames:
        sys.exit("no frame lines found")
    for e in sorted({k[0] for k in frames}):
        fs = sorted((t, v) for (ee, t), v in frames.items() if ee == e)
        base_n = max(1, sum(1 for t, _ in fs if t < fs[0][0] + 1000))
        base = [sum(v[3][i] for _, v in fs[:base_n]) / base_n for i in range(64)]
        print(f"== close e={e}: {len(fs)} frames, t {fs[0][0]}..{fs[-1][0]} ms, baseline = first {base_n} frames")
        for t, (ph, p, occ, px) in fs:
            rise = [px[i] - base[i] for i in range(64)]
            top = sorted(range(64), key=lambda i: -rise[i])[:3]
            cells = " ".join(f"r{i // 8}c{i % 8}{rise[i]:+.2f}" for i in top)
            print(f"{t:7d} {ph} p={p:6d} occ live={occ & 1} v2={occ >> 1 & 1}  warmest {cells}")
            if a.grid:
                for r in range(8):
                    print("        " + " ".join(f"{rise[r * 8 + c]:+5.1f}" for c in range(8)))


if __name__ == "__main__":
    main()

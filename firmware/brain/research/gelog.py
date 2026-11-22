"""Load Grid-EYE frames and door/detector events from a brain capture log.

Frame lines: `HH:MM:SS.ffffff # F <ms> t0 .. t63` (t = deg C x4, row-major, register order).
Returns times as seconds since midnight on the log clock.
"""
import re
import numpy as np

_F = re.compile(r"^(\d\d):(\d\d):(\d\d\.\d+) # F (\d+) ((?:-?\d+ ){63}-?\d+)\s*$")
_T = re.compile(r"^(\d\d):(\d\d):(\d\d\.\d+) # (.*)$")


def hms(s):
    h, m, x = s.split(":")
    return int(h) * 3600 + int(m) * 60 + float(x)


def fmt(t):
    h = int(t // 3600); m = int(t % 3600 // 60); s = t % 60
    return f"{h:02d}:{m:02d}:{s:06.3f}"


def load(path):
    ts, ms, fr, ev = [], [], [], []
    with open(path, "r", errors="replace") as f:
        for line in f:
            m = _F.match(line)
            if m:
                ts.append(int(m[1]) * 3600 + int(m[2]) * 60 + float(m[3]))
                ms.append(int(m[4]))
                fr.append(np.array(m[5].split(), dtype=np.int16))
                continue
            m = _T.match(line)
            if m:
                ev.append((int(m[1]) * 3600 + int(m[2]) * 60 + float(m[3]), m[4]))
    return np.array(ts), np.array(ms), np.array(fr, dtype=np.float32) / 4.0, ev


def door_intervals(ev):
    """(t_start, t_end, state) for door states from `door: a -> b` lines."""
    out, cur, t0 = [], "closed", None
    for t, s in ev:
        m = re.match(r"door: (\S+) -> (\S+)", s)
        if m:
            if t0 is not None:
                out.append((t0, t, cur))
            cur, t0 = m[2], t
    if t0 is not None:
        out.append((t0, 1e9, cur))
    return out


def door_not_closed(ts, ev):
    """Boolean per frame: door anywhere but closed (opening/open/closing/reversing...)."""
    nc = np.zeros(len(ts), bool)
    for a, b, st in door_intervals(ev):
        if st not in ("closed",):
            nc |= (ts >= a) & (ts < b)
    return nc


def segments(ms):
    """Index ranges [a, b) of continuous device-ms runs (a reboot resets ms)."""
    cut = np.where((np.diff(ms) <= 0) | (np.diff(ms) > 5000))[0] + 1
    edges = np.r_[0, cut, len(ms)]
    return list(zip(edges[:-1], edges[1:]))


def device_time(ts, ms):
    """Frame time on the log clock from the device's ms counter (removes Wi-Fi batching
    jitter): per segment, t = ms/1000 + min(ts - ms/1000), i.e. anchored to the
    least-delayed frame."""
    out = np.empty(len(ts))
    for a, b in segments(ms):
        off = ts[a:b] - ms[a:b] / 1000.0
        out[a:b] = ms[a:b] / 1000.0 + np.min(off)
    return out

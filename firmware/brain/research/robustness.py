"""Robustness checks that do not depend on the handful of labels:

1. Signal injection: cut the rise (frames - pre-event median) of real events, scale it by
   0.25..1, and add it to the empty doorway (16:48-17:09, door closed) every 90 s. Reports the
   detection rate and latency per detector and scale. Tests the margin, not the tuning.
2. Noise-only stress: synthetic frames with each pixel's measured noise (white, quantised to
   0.25 C) plus a slow common drift, 24 h, counts false presence/occupied episodes.

  python robustness.py [--cache file.npz]
"""
import os, sys, argparse
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gelog, evaluate as E, detectors as D

H = gelog.hms

# (name, event start, event end, bg window start, bg window end)
TEMPLATES = [
    ("dog weak 17:25 (edge)", "17:25:15", "17:25:43", "17:24:20", "17:24:48"),
    ("dog 17:31:59", "17:31:58", "17:32:04", "17:31:40", "17:31:55"),
    ("incident dogs 17:42:36", "17:42:36", "17:42:48", "17:41:00", "17:41:40"),
    ("dog 18:03:05", "18:03:04", "18:03:11", "18:02:00", "18:02:25"),
]
DETS = {
    "B0 baseline": lambda: D.Baseline(),
    "V2 fixed floor": lambda: D.PixelDetector(sigma_norm=False),
    "V2 proposal": lambda: D.PixelDetector(),
}


def run_one(make, frames):
    d = make(); P = np.zeros(len(frames), bool); O = np.zeros(len(frames), bool)
    for i, f in enumerate(frames):
        P[i], O[i], _ = d.step(f, True)
    return P, O


def injection(t, fr):
    seg = (t >= H("16:47:00")) & (t < H("17:09:50"))
    base = fr[seg]; tb = t[seg]
    starts = np.arange(600, len(base) - 400, 900)  # every 90 s, first after 60 s
    out = []
    for name, a, b, ba, bb in TEMPLATES:
        bgw = (t >= H(ba)) & (t < H(bb)); bg = np.median(fr[bgw], 0)
        w = (t >= H(a)) & (t < H(b)); rise = fr[w] - bg
        rise = np.clip(rise, 0, None)  # inject warming only (the cold open-door part is scene)
        for sc in (0.25, 0.5, 0.75, 1.0):
            x = base.copy()
            for s0 in starts:
                x[s0:s0 + len(rise)] += sc * rise
            for dn, mk in DETS.items():
                P, O = run_one(mk, x)
                hitp = []; hito = []; lp = []; lo = []; cov = []
                for s0 in starts:
                    win = slice(s0, s0 + len(rise))
                    hp = np.where(P[win])[0]; ho = np.where(O[win])[0]
                    hitp.append(len(hp) > 0); hito.append(len(ho) > 0)
                    if len(hp): lp.append(hp[0] / 10)
                    if len(ho): lo.append(ho[0] / 10)
                    cov.append(O[win].mean())
                # false alarms outside the injections (+20 s after each)
                mask = np.ones(len(x), bool)
                for s0 in starts: mask[s0 - 5:s0 + len(rise) + 200] = False
                fa_o = len([1 for s, f in E.episodes(O & mask, tb)])
                out.append((name, sc, dn, np.mean(hitp), np.median(lp) if lp else None, np.mean(hito),
                            np.median(lo) if lo else None, np.mean(cov), len(starts), fa_o))
    return out


def noise_stress(t, fr, hours=24, seed=1):
    rng = np.random.default_rng(seed)
    m = (t > H("16:48:00")) & (t < H("17:10:00"))
    x = fr[m]; sig = np.diff(x, axis=0).std(0) / np.sqrt(2); mu = x.mean(0)
    n = int(hours * 36000)
    res = {}
    for dn, mk in DETS.items():
        d = mk(); pe = oe = 0; lp = lo = False
        drift = 0.0
        for i in range(n):
            drift += rng.normal(0, 0.0005)  # random walk: ~0.1 C per 10 min
            f = mu + drift + rng.normal(0, 1, 64) * sig
            f = np.round(f * 4) / 4
            p, o, _ = d.step(f, True)
            pe += p and not lp; oe += o and not lo; lp, lo = p, o
        res[dn] = (pe, oe)
    return res, hours


if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("--cache"); ap.add_argument("--hours", type=float, default=24)
    a = ap.parse_args()
    t, ms, fr, ev = E.load(E.LOG, a.cache)
    print("| template | scale | detector | presence rate | presence lat s | occupied rate | occupied lat s | occupied coverage | n | false occupied eps between |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for r in injection(t, fr):
        name, sc, dn, hp, lp, ho, lo, cov, n, fa = r
        f = lambda v: "-" if v is None else f"{v:.1f}"
        print(f"| {name} | {sc:.2f} | {dn} | {hp:.2f} | {f(lp)} | {ho:.2f} | {f(lo)} | {cov:.2f} | {n} | {fa} |")
    res, hrs = noise_stress(t, fr, a.hours)
    for dn, (pe, oe) in res.items():
        print(f"noise-only {hrs:.0f} h, {dn}: presence episodes {pe}, occupied episodes {oe}")

"""Replay the 2026-11-21 frames through detectors and score them against labels.py.

  python evaluate.py [capture.log]            # table for all configured detectors
  python evaluate.py --cpp replay.csv          # score the C++ replay of presence2.h instead

Time base: the device's ms counter, anchored to the log clock (removes Wi-Fi batching jitter,
see gelog.device_time). Each reboot (ms reset) starts a fresh detector.
"""
import os, sys, argparse
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gelog, labels, detectors as D

HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, "../../../captures/2026-11-21_P1_brain-door.log")
H = gelog.hms


def load(path=LOG, cache=None):
    if cache and os.path.exists(cache):
        z = np.load(cache, allow_pickle=True)
        return z["t"], z["ms"], z["fr"], list(z["ev"])
    ts, ms, fr, ev = gelog.load(path)
    t = gelog.device_time(ts, ms)
    if cache:
        np.savez_compressed(cache, t=t, ms=ms, fr=fr, ev=np.array(ev, dtype=object))
    return t, ms, fr, ev


def run(make, t, ms, fr, closed):
    P = np.zeros(len(t), bool); O = np.zeros(len(t), bool); S = np.zeros(len(t))
    for a, b in gelog.segments(ms):
        d = make()
        for i in range(a, b):
            P[i], O[i], S[i] = d.step(fr[i], closed[i])
    return P, O, S


def episodes(x, t):
    """(start_time, end_time) runs of True."""
    e = np.diff(np.r_[0, x.astype(int), 0])
    s, f = np.where(e == 1)[0], np.where(e == -1)[0] - 1
    return [(t[i], t[j]) for i, j in zip(s, f)]


def score(t, P, O, ms):
    seg_start = np.array([t[a] for a, b in gelog.segments(ms)])
    out = {"events": []}
    pe, oe = episodes(P, t), episodes(O, t)
    for name, a, b, need, _ in labels.POS:
        a, b = H(a), H(b)
        w = (t >= a) & (t < b)
        ww = (t >= a - 2) & (t < b + 2)
        pon = [s for s, f in pe if a - 2 <= s < b + 2 or (s < a - 2 and f >= a - 2)]
        oon = [s for s, f in oe if a - 2 <= s < b + 2 or (s < a - 2 and f >= a - 2)]
        lat_p = (pon[0] - a) if pon else None
        lat_o = (oon[0] - a) if oon else None
        # time occupied stays on after the label end
        after = [f for s, f in oe if s <= b + 2 and f >= b]
        hold = (max(after) - b) if after else None
        out["events"].append(dict(name=name, need=need, lat_p=lat_p, lat_o=lat_o,
                                  cov_p=P[w].mean(), cov_o=O[w].mean(), hold=hold))
    a, b = map(H, labels.INCIDENT_WIDE)
    w = (t >= a) & (t < b)
    out["incident_wide_occ"] = O[w].mean()
    for key, wins in (("neg", labels.NEG), ("negd", labels.NEGD)):
        secs = 0; np_ = no = 0; ofrac = 0
        for _, a, b in wins:
            a, b = H(a), H(b)
            w = (t >= a) & (t < b)
            secs += w.sum() * 0.1
            np_ += sum(1 for s, f in pe if a <= s < b)
            no += sum(1 for s, f in oe if a <= s < b)
            ofrac += O[w].sum() * 0.1
        out[key] = dict(hours=secs / 3600, p_eps=np_, o_eps=no, o_secs=ofrac)
    # unlabelled: all frames outside any label window (+-5 s)
    lab = np.zeros(len(t), bool)
    for _, a, b, *_ in labels.POS:
        lab |= (t >= H(a) - 5) & (t < H(b) + 5)
    for _, a, b in labels.NEG + labels.NEGD:
        lab |= (t >= H(a)) & (t < H(b))
    lab |= (t >= H("17:42:00")) & (t < H("17:46:30"))  # incident period: dogs around
    out["unl"] = dict(hours=(~lab).sum() * 0.1 / 3600,
                      p_eps=sum(1 for s, f in pe if not lab[np.searchsorted(t, s)]),
                      o_eps=sum(1 for s, f in oe if not lab[np.searchsorted(t, s)]),
                      o_secs=float(O[~lab].sum() * 0.1),
                      p_list=[gelog.fmt(s) for s, f in pe if not lab[np.searchsorted(t, s)]])
    out["p_eps"], out["o_eps"] = pe, oe
    return out


def fmt_lat(x):
    return "  miss" if x is None else f"{x:+6.1f}"


def report(name, r, verbose=True):
    lines = [f"### {name}"]
    lines.append("| event | need | present latency s | occupied latency s | present cov | occupied cov | occ hold after s |")
    lines.append("|---|---|---|---|---|---|---|")
    for e in r["events"]:
        hold = "-" if e["hold"] is None else f"{e['hold']:.1f}"
        lines.append(f"| {e['name']} | {e['need']} | {fmt_lat(e['lat_p'])} | {fmt_lat(e['lat_o'])} | "
                     f"{e['cov_p']:.2f} | {e['cov_o']:.2f} | {hold} |")
    n, nd, u = r["neg"], r["negd"], r["unl"]
    lines.append(f"\nincident 17:42:30-17:43:15 occupied coverage {r['incident_wide_occ']:.2f}. "
                 f"Empty ({n['hours']*60:.0f} min): presence episodes {n['p_eps']}, occupied episodes {n['o_eps']} "
                 f"({n['o_secs']:.0f} s occupied). Door cycling ({nd['hours']*60:.0f} min): {nd['p_eps']} / {nd['o_eps']} "
                 f"({nd['o_secs']:.0f} s). Unlabelled ({u['hours']*60:.0f} min): presence episodes {u['p_eps']}, occupied {u['o_eps']}.")
    s = "\n".join(lines)
    if verbose: print(s)
    return s


def summary_row(name, r):
    ev = r["events"]
    need_p = [e for e in ev if e["need"] == "present"]
    miss_p = sum(1 for e in need_p if e["lat_p"] is None)
    lats = [e["lat_p"] for e in need_p if e["lat_p"] is not None]
    occ_need = [e for e in ev if e["need"] in ("occupied", "present", "either")]
    miss_o = sum(1 for e in occ_need if e["lat_o"] is None)
    inc = [e for e in ev if e["name"].startswith("INCIDENT")][0]
    n, nd, u = r["neg"], r["negd"], r["unl"]
    return (f"| {name} | {miss_p}/{len(need_p)} | {np.median(lats):+.1f} | {miss_o}/{len(occ_need)} | "
            f"{inc['cov_o']:.2f} | {r['incident_wide_occ']:.2f} | {n['p_eps']} | {n['o_eps']} ({n['o_secs']:.0f} s) | "
            f"{nd['p_eps']}/{nd['o_eps']} | {u['p_eps']}/{u['o_eps']} ({u['o_secs']:.0f} s) |")


HEADER = ("| detector | presence misses | median presence latency s | occupied misses | incident occ cov "
          "(17:42:39-51) | wide 17:42:30-43:15 | empty: presence eps | empty: occupied eps | door cycling P/O eps | unlabelled P/O eps |\n"
          "|---|---|---|---|---|---|---|---|---|---|")


def closed_mask(t, ev):
    return ~gelog.door_not_closed(t, ev)


EDGE_HALF = [0.5 if i % 8 == 0 else 1.0 for i in range(64)]          # column 0 at half weight
DOORWAY = [1.0 if (2 <= i // 8 <= 7 and i % 8 <= 4) else 0.0 for i in range(64)]  # rows 2-7, cols 0-4

CONFIGS = {
    "B0 baseline (presence.h)": lambda: D.Baseline(),
    "B1 baseline, occ_on 0.4": lambda: D.Baseline(occ_on=0.4, occ_off=0.25),
    "B2 baseline + common-mode removal": lambda: D.CommonModeBaseline(),
    "B3 baseline, doorway mask": lambda: D.Baseline(mask=[w > 0 for w in DOORWAY]),
    "V2 PROPOSAL (per-pixel smoothing, noise-normalised)": lambda: D.PixelDetector(),
    "V2 fixed floor (not noise-normalised)": lambda: D.PixelDetector(sigma_norm=False),
    "V2 without cooling clamp": lambda: D.PixelDetector(cool_clamp=None),
    "V2 without post-close recovery": lambda: D.PixelDetector(recover=0),
    "V2 freeze all bg while door not closed": lambda: D.PixelDetector(door_freeze=True),
    "V2 global (not per-pixel) bg freeze": lambda: D.PixelDetector(per_pixel_freeze=False),
    "V2 blob>=2 for presence": lambda: D.PixelDetector(min_blob=2),
    "V2 presence weights = doorway region": lambda: D.PixelDetector(weights=DOORWAY, occ_weights=[1.0] * 64),
    "V2 occupied weights col0 = 0.5": lambda: D.PixelDetector(occ_weights=EDGE_HALF),
}


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("log", nargs="?", default=LOG)
    ap.add_argument("--cache", default=None)
    ap.add_argument("--only", default=None)
    ap.add_argument("--cpp", default=None, help="CSV from replay2 (t_ms_index,present,occupied,score)")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    t, ms, fr, ev = load(a.log, a.cache)
    closed = closed_mask(t, ev)
    rows = []
    if a.cpp:
        z = np.loadtxt(a.cpp, delimiter=",", skiprows=1)
        P, O = z[:, 1] > 0, z[:, 2] > 0
        r = score(t, P, O, ms); report("C++ presence2.h", r); rows.append(summary_row("C++ presence2.h", r))
    else:
        for name, mk in CONFIGS.items():
            if a.only and a.only not in name: continue
            P, O, S = run(mk, t, ms, fr, closed)
            r = score(t, P, O, ms)
            if not a.quiet: report(name, r)
            rows.append(summary_row(name, r))
    print(HEADER); print("\n".join(rows))

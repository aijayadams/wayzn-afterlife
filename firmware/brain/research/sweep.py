"""Sensitivity of the V2 proposal to its two main knobs (occupied peak threshold, presence
score threshold): real-label metrics, injected weak signals at 0.5x and 24 h noise-only FPs.
  python sweep.py [--cache file.npz]
"""
import os, sys, argparse
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gelog, evaluate as E, detectors as D, robustness as R

ap = argparse.ArgumentParser(); ap.add_argument("--cache"); a = ap.parse_args()
t, ms, fr, ev = E.load(E.LOG, a.cache); closed = E.closed_mask(t, ev)
print("| occ_peak | on_score | incident cov | wide cov | presence misses | median latency | unlabelled occ s | inj 0.5x incident occ rate | inj 0.5x weak dog occ rate | inj 0.75x weak dog presence rate | noise 24 h P/O eps |")
print("|---|---|---|---|---|---|---|---|---|---|---|")
for occ_peak, on_score in [(0.5, 1.0), (0.6, 1.0), (0.8, 1.0), (1.0, 1.0), (0.6, 0.7), (0.6, 1.5), (0.6, 2.0)]:
    mk = lambda: D.PixelDetector(occ_peak=occ_peak, occ_peak_off=occ_peak * 0.75, on_score=on_score, off_score=on_score / 2)
    P, O, S = E.run(mk, t, ms, fr, closed); r = E.score(t, P, O, ms)
    ev_ = r["events"]; inc = [e for e in ev_ if e["name"].startswith("INCIDENT")][0]
    need = [e for e in ev_ if e["need"] == "present"]; lats = [e["lat_p"] for e in need if e["lat_p"] is not None]
    R.DETS = {"x": mk}
    inj = {(n, sc): (hp, ho) for n, sc, dn, hp, lp, ho, lo, cov, nn, fa in R.injection(t, fr)}
    res, _ = R.noise_stress(t, fr, 24)
    print(f"| {occ_peak} | {on_score} | {inc['cov_o']:.2f} | {r['incident_wide_occ']:.2f} | {len(need)-len(lats)}/{len(need)} | {np.median(lats):+.1f} | "
          f"{r['unl']['o_secs']:.0f} | {inj[('incident dogs 17:42:36', 0.5)][1]:.2f} | {inj[('dog weak 17:25 (edge)', 0.5)][1]:.2f} | "
          f"{inj[('dog weak 17:25 (edge)', 0.75)][0]:.2f} | {res['x'][0]}/{res['x'][1]} |")

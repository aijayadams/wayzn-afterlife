"""Figures for docs/DETECTION-RESEARCH.md -> docs/research/*.png
  python figures.py [--cache file.npz]"""
import os, sys, argparse
import numpy as np
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gelog, evaluate as E, detectors as D
H = gelog.hms
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../docs/research")
BLUE, ORANGE, INK, MUTED, GRID = "#2a78d6", "#eb6834", "#222222", "#6b6b66", "#e4e3de"
SEQ = LinearSegmentedColormap.from_list("seq", ["#ffffff", "#cde2fb", "#86b6ef", "#3987e5", "#1c5cab", "#0d366b"])
plt.rcParams.update({"font.size": 9, "axes.edgecolor": MUTED, "axes.labelcolor": INK, "xtick.color": MUTED,
                     "ytick.color": MUTED, "axes.spines.top": False, "axes.spines.right": False})

ap = argparse.ArgumentParser(); ap.add_argument("--cache"); a = ap.parse_args()
t, ms, fr, ev = E.load(E.LOG, a.cache); closed = E.closed_mask(t, ev)
os.makedirs(OUT, exist_ok=True)

# --- Figure 1: the 17:42 incident, baseline vs v2 --------------------------------------
w0, w1 = H("17:41:30"), H("17:44:40")
idx = np.where((t >= w0 - 600) & (t < w1))[0]
seg = [s for s in gelog.segments(ms) if s[0] <= idx[0] < s[1]][0]
b0 = D.Baseline(); v2 = D.PixelDetector()
sm, pk, O0, O2 = [], [], [], []
for i in range(seg[0], idx[-1] + 1):
    p0, o0, s0 = b0.step(fr[i], closed[i]); p2, o2, s2 = v2.step(fr[i], closed[i])
    if i >= idx[0]:
        sm.append(s0); pk.append(v2.last.get("peak", 0)); O0.append(o0); O2.append(o2)
tt = (t[idx] - w0)
fig, ax = plt.subplots(2, 1, figsize=(8, 4.6), sharex=True)
for k, (y, thr, occ, title, col) in enumerate([
        (np.array(sm), 0.6, np.array(O0), "presence.h: smoothed summed excess (occupied at >= 0.6)", ORANGE),
        (np.array(pk), 0.6, np.array(O2), "v2: largest smoothed, noise-normalised pixel rise, C (occupied at >= 0.6)", BLUE)]):
    A = ax[k]
    nc = ~closed[idx]
    for s_, f_ in E.episodes(nc, tt): A.axvspan(s_, f_, color="#f1f0ec", lw=0)
    for lab, la, lb in [("dogs", "17:42:04", "17:42:12"), ("dogs by door", "17:42:30", "17:43:15"), ("dog", "17:43:53", "17:44:29"), ("dog", "17:41:45", "17:41:51")]:
        A.axvspan(H(la) - w0, H(lb) - w0, ymin=0.93, ymax=1.0, color=MUTED, lw=0)
    A.axvspan(H("17:42:39") - w0, H("17:42:51") - w0, color="#fde1d6", lw=0)
    A.plot(tt, y, color=col, lw=1.2)
    A.axhline(thr, color=INK, lw=0.8, ls="--")
    for s_, f_ in E.episodes(occ, tt): A.plot([s_, f_], [-0.25, -0.25], color=col, lw=4, solid_capstyle="butt")
    A.set_ylim(-0.4, 3.2); A.set_title(title, loc="left", fontsize=9, color=INK)
    A.grid(axis="y", color=GRID, lw=0.6)
ax[1].set_xlabel("seconds after 17:41:30", fontsize=8)
fig.text(0.01, 0.005, "grey: door not closed; pink: must be occupied (17:42:39-51); dark bars at top: labelled dogs; "
         "coloured bar at bottom: detector 'occupied'", fontsize=7, color=MUTED)
fig.tight_layout(rect=(0, 0.03, 1, 1)); fig.savefig(os.path.join(OUT, "incident-1742.png"), dpi=150); plt.close(fig)

# --- Figure 2: where things appear (mean rise maps) + noise -------------------------------
def rise(a_, b_, ba, bb):
    bg = np.median(fr[(t >= H(ba)) & (t < H(bb))], 0)
    return (fr[(t >= H(a_)) & (t < H(b_))] - bg).mean(0).reshape(8, 8)
maps = [("person 17:24:53-59", rise("17:24:53", "17:24:59", "17:24:20", "17:24:48")),
        ("dog (weak) 17:25:17-42", rise("17:25:17", "17:25:42", "17:24:20", "17:24:48")),
        ("dog 17:31:59.5-32:02", rise("17:31:59.5", "17:32:02", "17:31:30", "17:31:55")),
        ("incident dogs 17:42:39-51", rise("17:42:39", "17:42:51", "17:41:00", "17:41:40"))]
m = (t > H("16:48:00")) & (t < H("17:10:00"))
noise = (np.diff(fr[m], axis=0).std(0) / np.sqrt(2)).reshape(8, 8)
fig, axs = plt.subplots(1, 7, figsize=(12, 2.6), gridspec_kw={"width_ratios": [1, 1, 1, 1, 0.06, 0.35, 1]})
axs[5].axis("off"); cax = axs[4]; ax = [axs[0], axs[1], axs[2], axs[3], axs[6]]
for A, (title, M) in zip(ax[:4], maps):
    im = A.imshow(np.clip(M, 0, None), cmap=SEQ, vmin=0, vmax=2.5)
    A.set_title(title, fontsize=8); A.set_xticks(range(8)); A.set_yticks(range(8)); A.tick_params(labelsize=6)
    for (r, c), v in np.ndenumerate(M):
        if v >= 0.5: A.text(c, r, f"{v:.1f}", ha="center", va="center", fontsize=5.5, color="white" if v > 1.3 else INK)
fig.colorbar(im, cax=cax).set_label("mean rise, C", fontsize=7)
im2 = ax[4].imshow(noise, cmap=SEQ, vmin=0.15, vmax=0.4)
ax[4].set_title("pixel noise sigma, C (empty)", fontsize=8); ax[4].set_xticks(range(8)); ax[4].set_yticks(range(8)); ax[4].tick_params(labelsize=6)
for (r, c), v in np.ndenumerate(noise): ax[4].text(c, r, f"{v:.2f}"[1:], ha="center", va="center", fontsize=5, color=INK)
fig.text(0.01, 0.02, "rows top->bottom 0-7, columns left->right 0-7 (register order, row-major)", fontsize=7, color=MUTED)
fig.savefig(os.path.join(OUT, "rise-maps.png"), dpi=150, bbox_inches="tight"); plt.close(fig)
print("wrote", OUT)

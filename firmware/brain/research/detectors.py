"""Reference (Python) implementations of the Grid-EYE detectors compared in
docs/DETECTION-RESEARCH.md. Each detector: d = Cls(**params); d.step(frame64, door_closed)
returns (present, occupied, score). Frames in deg C, row-major register order.

Baseline mirrors firmware/brain/src/presence.h exactly. PixelDetector ("v2") is what
firmware/brain/src/presence2.h implements; the other classes are ablations.
"""
import numpy as np

WARMUP = 20


class Baseline:
    """Port of presence.h."""

    def __init__(self, noise_c=0.7, on_score=1.0, off_score=0.6, smooth=0.3, occ_on=0.6, occ_off=0.35,
                 occ_off_frames=30, on_frames=3, off_frames=20, alpha=0.002, alpha_present=0.0002, mask=None):
        self.__dict__.update(locals()); del self.__dict__["self"]
        self.mask = np.ones(64, bool) if mask is None else np.asarray(mask, bool)
        self.bg = None; self.n = 0; self.sm = 0.0
        self.on_run = self.off_run = self.occ_clear = 0
        self.present = self.occupied = False

    def step(self, t, door_closed=True):
        if self.n < WARMUP:
            self.bg = t.copy() if self.n == 0 else self.bg + (t - self.bg) / (self.n + 1)
            self.n += 1
            return False, False, 0.0
        d = t - self.bg
        score = float(np.sum(np.clip(d[self.mask] - self.noise_c, 0, None)))
        self.sm += self.smooth * (score - self.sm)
        thr = self.off_score if self.present else self.on_score
        if self.sm >= thr: self.on_run += 1; self.off_run = 0
        else: self.off_run += 1; self.on_run = 0
        if not self.present and self.on_run >= self.on_frames: self.present = True
        if self.present and self.off_run >= self.off_frames: self.present = False
        if self.sm >= self.occ_on: self.occupied = True; self.occ_clear = 0
        elif self.sm < self.occ_off:
            if self.occupied:
                self.occ_clear += 1
                if self.occ_clear >= self.occ_off_frames: self.occupied = False
        else: self.occ_clear = 0
        if self.present: self.occupied = True
        a = self.alpha_present if self.occupied else self.alpha
        self.bg += a * (t - self.bg)
        return self.present, self.occupied, self.sm


def neighbours8():
    nb = []
    for i in range(64):
        r, c = divmod(i, 8)
        nb.append([rr * 8 + cc for rr in range(r - 1, r + 2) for cc in range(c - 1, c + 2)
                   if 0 <= rr < 8 and 0 <= cc < 8 and (rr, cc) != (r, c)])
    return nb


NB8 = neighbours8()


def largest_blob(on):
    """Connected components (8-neighbour) over a bool[64]; returns list of index lists."""
    seen = np.zeros(64, bool); blobs = []
    for i in range(64):
        if on[i] and not seen[i]:
            st = [i]; seen[i] = True; comp = []
            while st:
                j = st.pop(); comp.append(j)
                for k in NB8[j]:
                    if on[k] and not seen[k]:
                        seen[k] = True; st.append(k)
            blobs.append(comp)
    return blobs


class PixelDetector:
    """Per-pixel temporal smoothing BEFORE thresholding (the main proposal, "v2").

    s_i   = EMA_a(t_i - bg_i)                  per-pixel smoothed rise (noise 0.26 -> ~0.09 C)
    exc_i = w_i * max(0, s_i - k)              excess over a per-pixel floor (k ~ 4 sigma)
    score = sum exc_i;  peak = max w_i * s_i
    occupied: peak >= occ_peak or score >= occ_score;  clear after occ_off_frames with
              peak < occ_peak_off and score < occ_score_off.
    present:  score >= on_score (and blob >= min_blob pixels over k) for on_frames; off below
              off_score for off_frames.
    Background: per-pixel EMA; while occupied, only pixels that are warm (s_i > k) are frozen
    (alpha_present), the rest keep learning. Cooling clamp: bg_i never sits more than
    cool_clamp above the smoothed scene (an opening door cools the doorway by up to 1 C).
    Optional: freeze all learning while the door is not closed (door_freeze).
    """

    def __init__(self, a_s=0.2, k=0.35, on_score=1.0, off_score=0.5, on_frames=2, off_frames=20,
                 occ_peak=0.6, occ_score=0.4, occ_peak_off=0.45, occ_score_off=0.25, occ_off_frames=30,
                 alpha=0.002, alpha_present=0.0002, cool_clamp=0.3, door_freeze=False, min_blob=1,
                 weights=None, occ_weights=None, per_pixel_freeze=True, sigma_norm=True,
                 recover=0.01, recover_s=180):
        self.__dict__.update(locals()); del self.__dict__["self"]
        self.w = np.ones(64) if weights is None else np.asarray(weights, float)
        self.wo = self.w if occ_weights is None else np.asarray(occ_weights, float)
        self.bg = None; self.n = 0; self.s = np.zeros(64)
        self.on_run = self.off_run = self.occ_clear = 0
        self.present = self.occupied = False
        self.var = np.full(64, 0.26 ** 2)
        self.ref = None; self.was_closed = True; self.rec_left = 0; self.prev = None
        self.last = {}

    def step(self, t, door_closed=True):
        if self.n < WARMUP:
            self.bg = t.astype(float).copy() if self.n == 0 else self.bg + (t - self.bg) / (self.n + 1)
            self.n += 1
            return False, False, 0.0
        d = t - self.bg
        self.s += self.a_s * (d - self.s)
        s = self.s
        if self.sigma_norm:
            # Noise-normalised: express each pixel's rise in units of a reference pixel
            # (sigma 0.26 C), using its own noise learned from frame-to-frame differences.
            # Edge pixels are ~1.5x noisier (0.38 vs 0.23 C), so a fixed floor over-triggers there.
            f = np.clip(np.sqrt(self.var) / 0.26, 0.8, 2.0)
            s = s / f
        kk = self.k
        warm = s > kk
        exc = self.w * np.clip(s - kk, 0, None)
        score = float(exc.sum())
        peak = float(np.max(self.wo * s))
        occ_score = float((self.wo * np.clip(s - kk, 0, None)).sum())
        blob = max((len(b) for b in largest_blob(warm & (self.w > 0))), default=0) if self.min_blob > 1 else 99
        # presence
        if not self.present:
            hot = score >= self.on_score and blob >= self.min_blob
        else:
            hot = score >= self.off_score
        if hot: self.on_run += 1; self.off_run = 0
        else: self.off_run += 1; self.on_run = 0
        if not self.present and self.on_run >= self.on_frames: self.present = True
        if self.present and self.off_run >= self.off_frames: self.present = False
        # occupied
        if peak >= self.occ_peak or occ_score >= self.occ_score:
            self.occupied = True; self.occ_clear = 0
        elif peak < self.occ_peak_off and occ_score < self.occ_score_off:
            if self.occupied:
                self.occ_clear += 1
                if self.occ_clear >= self.occ_off_frames: self.occupied = False
        else:
            self.occ_clear = 0
        if self.present: self.occupied = True
        # background
        # Post-close recovery: the clamp lets bg follow the cold open-door scene; once the door
        # is closed again the scene warms back over ~1-2 min, which would read as warming. So
        # bg_i recovers toward the pre-open background `ref` at `recover`/frame, never above it.
        if self.recover:
            if self.was_closed and not door_closed:
                self.ref = self.bg.copy()
            if door_closed and not self.was_closed and self.ref is not None:
                self.rec_left = int(self.recover_s * 10)
            self.was_closed = door_closed
        if self.door_freeze and not door_closed:
            a = np.zeros(64)
        elif self.occupied and self.per_pixel_freeze:
            a = np.where(warm, self.alpha_present, self.alpha)
        elif self.occupied:
            a = np.full(64, self.alpha_present)
        else:
            a = np.full(64, self.alpha)
        self.bg += a * (t - self.bg)
        if self.recover and self.rec_left > 0 and door_closed:
            self.rec_left -= 1
            below = self.bg < self.ref
            step = np.minimum(self.recover * (self.ref - self.bg), 0.25 * self.a_s)  # never faster than s can follow
            # only while the scene itself is above bg (it is warming back); a pixel that is still
            # colder than bg has nothing to recover
            m = below & (self.s > 0)
            self.bg[m] += step[m]; self.s[m] -= step[m]
        if self.prev is not None and not self.occupied:
            self.var += 0.0005 * ((t - self.prev) ** 2 / 2 - self.var)
        self.prev = t.copy()
        if self.cool_clamp is not None:
            low = self.s < -self.cool_clamp
            if low.any():
                adj = self.s[low] + self.cool_clamp
                self.bg[low] += adj
                self.s[low] -= adj
        self.last = dict(peak=peak, score=score, blob=blob)
        return self.present, self.occupied, score


class CommonModeBaseline(Baseline):
    """Baseline with the frame's median rise removed before thresholding (ablation)."""

    def step(self, t, door_closed=True):
        if self.n >= WARMUP:
            t = t - max(0.0, float(np.median(t - self.bg)))
        return super().step(t, door_closed)

// v2 detector, running in SHADOW MODE in the firmware (logs only, decides nothing; see main.cpp).
// Findings and numbers: docs/DETECTION-RESEARCH.md. Python reference: research/detectors.py
// (PixelDetector); research/replay2.cpp replays a capture through this header.
//
// Grid-EYE (AMG88xx, 8x8, 10 Hz, 0.25 C/LSB) doorway detector, v2.
//
// What changes against presence.h, and why:
// 1. Smooth each pixel BEFORE thresholding. presence.h thresholds every raw frame at 0.7 C
//    (~2.7 sigma of the 0.26 C pixel noise) and smooths the sum afterwards, so a dog that
//    warms one pixel by 1.1-1.6 C (a dog standing beside the door) contributes only ~0.5 per frame and
//    stays under the "occupied" level. A per-pixel EMA (a = 0.2, ~0.5 s) cuts the noise to
//    ~0.09 C; over 22 min of empty doorway no smoothed pixel exceeded 0.45 C, while the
//    such a dog holds 1.1-1.6 C. Latency for real arrivals is unchanged or ~0.2 s faster.
// 2. Noise-normalised pixels. Edge/corner pixels are ~1.5x noisier (0.38 vs 0.23 C); each
//    pixel's noise is learned from frame-to-frame differences and its rise is expressed in
//    units of a 0.26 C reference pixel, so one threshold fits all 64.
// 3. Cooling clamp + post-close recovery. An opening door cools the doorway columns by up to
//    1.0 C (cold air), which hides a dog standing there. The background may not sit more than
//    `cool_clamp` above the smoothed scene, so warming is measured against the cold scene.
//    After the door closes the scene warms back over 1-2 min; the background then recovers
//    toward the pre-open background (never above it) instead of calling that warmth a dog.
// 4. Per-pixel learning freeze. While occupied, only the warm pixels stop learning; the rest
//    of the scene keeps tracking drift, so a long "occupied" cannot be prolonged by drift
//    elsewhere.
// 5. Cell weights (0..1) for presence and for occupied separately: the user's "zones".
//
// Outputs: `present` (auto-open trigger) and `occupied` (safety: never close, reverse a close).
// Only warming counts. A false `occupied` only delays a close; a missed one can hit a dog,
// so the occupied thresholds have a hard floor (kOccPeakMax) that settings cannot exceed.
//
// RAM: 6 x 64 floats + 2 x 64 weights ~= 2.0 KB. CPU: ~64 x 30 flops per frame.
#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>

namespace ge2 {

struct Params {
  float a_s = 0.2f;             // per-pixel EMA of the rise (10 Hz: ~0.5 s time constant)
  float k = 0.35f;              // per-pixel floor in reference-pixel C (~4 sigma after smoothing)
  // presence (auto-open): summed weighted excess over k
  float on_score = 1.0f;        // dogs here 1.5-7, person 9-17, empty <= 0.25 (smoothed units)
  float off_score = 0.5f;
  int on_frames = 2;            // 0.2 s
  int off_frames = 20;          // 2 s
  // occupied (safety): any one pixel clearly warm, or a little warmth spread over several
  float occ_peak = 0.6f;        // single pixel, reference-pixel C (empty max 0.45; dogs 1.1+)
  float occ_score = 0.4f;
  float occ_peak_off = 0.45f;
  float occ_score_off = 0.25f;
  int occ_off_frames = 30;      // 3 s quiet to call the doorway clear
  // background
  float alpha = 0.002f;         // ~50 s while clear (or for cold pixels while occupied)
  float alpha_present = 0.0002f;// ~8 min for warm pixels while occupied
  float cool_clamp = 0.3f;      // bg may sit at most this far above the smoothed scene
  float recover = 0.01f;        // after a close: bg back toward the pre-open bg, per frame
  int recover_frames = 1800;    // for at most 3 min after the door closed
  float noref_rate = 0.002f;    // no snapshot (power cut while open): bg follows warming at most this per frame
  float noise_ref = 0.26f;      // reference pixel noise (C); pixels are normalised to it
  float var_rate = 0.0005f;     // noise learning rate (frame-difference variance), while clear
  float w[64];                  // presence weights 0..1 (row-major, register 0x80 order)
  float wo[64];                 // occupied weights 0..1
  Params() { for (int i = 0; i < 64; i++) w[i] = wo[i] = 1.0f; }
};

// The least sensitive the occupied level may be made by settings (reference-pixel C).
static constexpr float kOccPeakMax = 1.0f;
static constexpr float kOccScoreMax = 0.8f;

// User sensitivity 1 (least) .. 5 (most) for presence only; 3 = defaults. The occupied level
// is a safety function and is not scaled down by it (see docs/DETECTION-RESEARCH.md).
inline void applySensitivity(Params &p, int level) {
  static const float f[5] = {2.0f, 1.4f, 1.0f, 0.8f, 0.65f};
  if (level < 1) level = 1;
  if (level > 5) level = 5;
  p.on_score = 1.0f * f[level - 1];
  p.off_score = 0.5f * f[level - 1];
}

struct Result {
  bool present = false;
  bool occupied = false;
  int warm = 0;          // pixels above k (diagnostics)
  float score = 0;       // presence score (weighted excess over k, smoothed per pixel)
  float occ_score = 0;
  float peak = 0;        // largest weighted normalised pixel rise
  int peak_i = -1;
  float max_delta = 0;   // largest raw rise this frame
};

class Detector {
 public:
  Params p;
  float bg[64], s[64], var[64], prev[64], ref[64];
  bool ready = false;

  Detector() { reset(); }

  void reset() {
    ready = false; n_ = 0; have_prev_ = false; have_ref_ = false; was_closed_ = true;
    rec_left_ = noref_left_ = 0; known_ = false; present_ = occupied_ = false; on_run_ = off_run_ = occ_clear_ = 0;
    for (int i = 0; i < 64; i++) { s[i] = 0; var[i] = p.noise_ref * p.noise_ref; }
  }

  // The closed-door snapshot (taken when the door starts to open), so a caller can keep it across
  // a reboot. refSeq() changes each time a new one is taken.
  bool hasRef() const { return have_ref_; }
  // Background and noise saved before a software reset (RTC memory), instead of learning the
  // first frames (which would take a dog in the doorway as background).
  void loadBackground(const float b[64], const float v[64]) {
    memcpy(bg, b, sizeof bg);
    memcpy(var, v, sizeof var);
    n_ = kWarmup;
    ready = true;
  }
  const float *refData() const { return ref; }
  uint32_t refSeq() const { return ref_seq_; }
  void loadRef(const float r[64]) { memcpy(ref, r, sizeof ref); have_ref_ = true; }

  // One frame (deg C), whether the door is closed, and whether the door state is known at all.
  // Door transitions count only between known states, and not until the first known state after a
  // reset, so a snapshot is never taken of an open door.
  Result update(const float t[64], bool door_closed, bool door_known = true) {
    Result r;
    if (!ready) {
      for (int i = 0; i < 64; i++) bg[i] = n_ ? bg[i] + (t[i] - bg[i]) / (n_ + 1) : t[i];
      if (++n_ >= kWarmup) ready = true;
      return r;
    }
    // [1]+[2] smoothed, normalised rise per pixel
    float sn[64];
    for (int i = 0; i < 64; i++) {
      float d = t[i] - bg[i];
      if (d > r.max_delta) r.max_delta = d;
      s[i] += p.a_s * (d - s[i]);
      float f = sqrtf(var[i]) / p.noise_ref;
      f = f < 0.8f ? 0.8f : (f > 2.0f ? 2.0f : f);
      sn[i] = s[i] / f;
      float e = sn[i] > p.k ? sn[i] - p.k : 0.0f;
      if (sn[i] > p.k) r.warm++;
      r.score += p.w[i] * e;
      r.occ_score += p.wo[i] * e;
      float pk = p.wo[i] * sn[i];
      if (r.peak_i < 0 || pk > r.peak) { r.peak = pk; r.peak_i = i; }
    }
    // presence
    bool hot = present_ ? r.score >= p.off_score : r.score >= p.on_score;
    if (hot) { on_run_++; off_run_ = 0; } else { off_run_++; on_run_ = 0; }
    if (!present_ && on_run_ >= p.on_frames) present_ = true;
    if (present_ && off_run_ >= p.off_frames) present_ = false;
    // occupied (thresholds capped: settings can't make the safety level blind)
    float op = p.occ_peak < kOccPeakMax ? p.occ_peak : kOccPeakMax;
    float os = p.occ_score < kOccScoreMax ? p.occ_score : kOccScoreMax;
    if (r.peak >= op || r.occ_score >= os) { occupied_ = true; occ_clear_ = 0; }
    else if (r.peak < p.occ_peak_off && r.occ_score < p.occ_score_off) {
      if (occupied_ && ++occ_clear_ >= p.occ_off_frames) occupied_ = false;
    } else occ_clear_ = 0;
    if (present_) occupied_ = true;
    // [3] remember the closed-door background when the door starts to open
    if (door_known) {
      if (!known_) known_ = true;
      else {
        if (was_closed_ && !door_closed) { memcpy(ref, bg, sizeof bg); have_ref_ = true; ref_seq_++; }
        if (door_closed && !was_closed_) {
          if (have_ref_) rec_left_ = p.recover_frames;
          else noref_left_ = p.recover_frames;
        }
      }
      was_closed_ = door_closed;
    }
    // [4] background learning
    for (int i = 0; i < 64; i++) {
      float a = (occupied_ && sn[i] > p.k) ? p.alpha_present : p.alpha;
      bg[i] += a * (t[i] - bg[i]);
    }
    if (rec_left_ > 0 && door_closed) {
      rec_left_--;
      for (int i = 0; i < 64; i++) {
        if (bg[i] < ref[i] && s[i] > 0) {
          float step = p.recover * (ref[i] - bg[i]);
          float cap = 0.25f * p.a_s;
          if (step > cap) step = cap;
          bg[i] += step; s[i] -= step;
        }
      }
    }
    // No snapshot: follow slow warming only (a dog is a ~1 C step, the scene warms ~0.015 C/s).
    if (noref_left_ > 0 && door_closed) {
      noref_left_--;
      for (int i = 0; i < 64; i++) {
        if (s[i] > 0) {
          float step = s[i] < p.noref_rate ? s[i] : p.noref_rate;
          bg[i] += step; s[i] -= step;
        }
      }
    }
    // noise, from frame differences (insensitive to slow drift), only while clear
    if (have_prev_ && !occupied_)
      for (int i = 0; i < 64; i++) { float df = t[i] - prev[i]; var[i] += p.var_rate * (df * df * 0.5f - var[i]); }
    memcpy(prev, t, sizeof prev); have_prev_ = true;
    // [3] cooling clamp
    for (int i = 0; i < 64; i++) {
      if (s[i] < -p.cool_clamp) { float adj = s[i] + p.cool_clamp; bg[i] += adj; s[i] -= adj; }
    }
    r.present = present_;
    r.occupied = occupied_;
    return r;
  }

 private:
  static constexpr int kWarmup = 20;
  int n_ = 0, on_run_ = 0, off_run_ = 0, occ_clear_ = 0, rec_left_ = 0, noref_left_ = 0;
  uint32_t ref_seq_ = 0;
  bool have_prev_ = false, have_ref_ = false, was_closed_ = true, known_ = false, present_ = false, occupied_ = false;
};

}  // namespace ge2

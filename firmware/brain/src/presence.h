// Grid-EYE (AMG88xx, 8x8 thermopile) presence detector. Pure C++: also built on the host.
//
// Each pixel keeps a slowly learned background. A dog in the doorway warms several pixels by
// only 1-2 C, so the score is the summed excess over
// the noise floor: score = sum over masked pixels of max(0, t - bg - noise_c), smoothed (EMA).
// Presence turns on at smoothed >= on_score for `on_frames` frames, off below off_score for
// `off_frames`. A dog reads ~1/5 of a person, so the thresholds are set for the dog.
// Only warming counts: an open door showing cold outside air must not look like a dog.
// `hot` (pixels >= delta_c above background) is reported for diagnostics.
//
// The background follows the scene with time constant ~1/alpha frames while nothing is present,
// and ~1/alpha_present while something is: a dog lying still is absorbed only slowly, and a
// stuck "present" (sun patch, heater) recovers eventually instead of holding the door forever.
#pragma once
#include <string.h>
#include <stdint.h>
#include <math.h>

namespace ge {

struct Params {
  float noise_c = 0.7f;           // per-pixel noise floor (empty doorway: max ~+0.9 C)
  float on_score = 1.0f;          // smoothed. Empty doorway <= ~0.5; dog 1.5-3.3; person 9-17
  float off_score = 0.6f;         // hysteresis
  float smooth = 0.3f;            // score EMA per frame (~0.3 s)
  // "Occupied" is the safety level, more sensitive than presence: anything above the noise
  // blocks closing and reverses a close. A false "occupied" only delays a close; a missed dog
  // can be hit (a dog right beside the door can score only 0.8-1.3).
  float occ_on = 0.6f;
  float occ_off = 0.35f;
  int occ_off_frames = 30;        // 3 s below occ_off to call the doorway clear
  float delta_c = 1.0f;           // diagnostics only: "hot" pixel
  int on_frames = 3;              // consecutive frames to declare presence (10 Hz: 0.3 s)
  int off_frames = 20;            // consecutive clear frames to drop it (2 s)
  float alpha = 0.002f;           // background rate while clear (10 Hz: ~50 s)
  float alpha_present = 0.0002f;  // while present (~8 min)
  uint64_t mask = ~0ULL;          // bit i = pixel i is used (row-major, register 0x80 order)
};

struct Result {
  bool present = false;   // someone wants out: auto-open trigger
  bool occupied = false;  // something may be in the doorway: never close
  int hot = 0;          // pixels >= delta_c above background this frame
  float score = 0;      // summed excess over noise_c, this frame
  float smoothed = 0;   // EMA of score: what the thresholds apply to
  float max_delta = 0;  // largest rise over background this frame (masked pixels)
};

class Detector {
 public:
  Params p;
  float bg[64];
  bool ready = false;

  // A background saved before a software reset (RTC memory): use it instead of learning the
  // first frames, which would take whatever is in the doorway as background.
  void loadBackground(const float b[64]) {
    memcpy(bg, b, sizeof bg);
    warm_n_ = kWarmup;
    ready = true;
  }

  // Feed one frame (degrees C). The first `warmup` frames only build the background.
  Result update(const float t[64]) {
    Result r;
    if (!ready) {
      for (int i = 0; i < 64; i++) bg[i] = warm_n_ ? bg[i] + (t[i] - bg[i]) / (warm_n_ + 1) : t[i];
      if (++warm_n_ >= kWarmup) ready = true;
      return r;
    }
    for (int i = 0; i < 64; i++) {
      if (!((p.mask >> i) & 1)) continue;
      float d = t[i] - bg[i];
      if (d > r.max_delta) r.max_delta = d;
      if (d >= p.delta_c) r.hot++;
      if (d > p.noise_c) r.score += d - p.noise_c;
    }
    sm_ += p.smooth * (r.score - sm_);
    r.smoothed = sm_;
    float thr = present_ ? p.off_score : p.on_score;
    bool hot_frame = sm_ >= thr;
    if (hot_frame) { on_run_++; off_run_ = 0; } else { off_run_++; on_run_ = 0; }
    if (!present_ && on_run_ >= p.on_frames) present_ = true;
    if (present_ && off_run_ >= p.off_frames) present_ = false;
    if (sm_ >= p.occ_on) { occupied_ = true; occ_clear_ = 0; }
    else if (sm_ < p.occ_off) { if (occupied_ && ++occ_clear_ >= p.occ_off_frames) occupied_ = false; }
    else occ_clear_ = 0;
    if (present_) occupied_ = true;
    // The background only learns while the doorway is clear, so a dog standing still is not
    // absorbed into it.
    float a = occupied_ ? p.alpha_present : p.alpha;
    for (int i = 0; i < 64; i++) bg[i] += a * (t[i] - bg[i]);
    r.present = present_;
    r.occupied = occupied_;
    return r;
  }
  void reset() { ready = false; warm_n_ = 0; present_ = occupied_ = false; on_run_ = off_run_ = occ_clear_ = 0; sm_ = 0; }

 private:
  static constexpr int kWarmup = 20;  // 2 s at 10 Hz
  int warm_n_ = 0, on_run_ = 0, off_run_ = 0;
  float sm_ = 0;
  bool occupied_ = false;
  int occ_clear_ = 0;
  bool present_ = false;
};

// Raw AMG88xx pixel (12-bit two's complement, 0.25 C/LSB) to degrees C.
inline float pixelC(uint8_t lo, uint8_t hi) {
  int v = (int16_t)(((hi & 0x0F) << 8) | lo);
  if (v & 0x800) v -= 0x1000;
  return v * 0.25f;
}

}  // namespace ge

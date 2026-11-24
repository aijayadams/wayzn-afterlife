// Grid-EYE scene-wide drift watch. Pure C++: also built on the host.
//
// Both detectors stop learning their background while the doorway is occupied (so a dog standing
// still is not absorbed). That deadlocks when the whole scene drifts warmer than the background:
// the drift reads as occupied, so the background never follows it. Seen 2026-11-24 after a power
// cut with the board taken apart (cold start, ~16 C): every pixel +0.3..+2.1 C over the background
// learned at boot, occupied for 3+ minutes, the calibration blocked.
//
// A dog or a person warms a patch: 1-6 pixels for a dog, ~10 for a person at the door
// (docs/DETECTION-RESEARCH.md). Drift warms the whole field. This watch keeps its own reference:
// the smoothed scene as it was the last time the doorway read clear (frozen while occupied, so it
// doesn't depend on how either detector learns). While occupied, the test is the low quartile of
// the smoothed rise over that reference: >= kQ means at least 3/4 of the pixels (48 of 64) have
// warmed, which a visit cannot do. Held for kHoldMs, the caller relearns both backgrounds. A dog
// in the doorway at that moment is learned into the background with it (as a dog lying still is
// anyway, over minutes, by the detectors' slow learning while occupied).
#pragma once
#include <stdint.h>
#include <string.h>
#include <algorithm>

namespace ge {

// The 16th smallest of 64 values (the low quartile).
inline float lowQuartile(const float v[64]) {
  float c[64];
  memcpy(c, v, sizeof c);
  std::nth_element(c, c + 15, c + 64);
  return c[15];
}

class DriftWatch {
 public:
  static constexpr float kQ = 0.25f;        // C. Empty doorway: smoothed pixels stay within ~0.45 C, the quartile ~0
  static constexpr uint32_t kHoldMs = 60000;
  static constexpr float kA = 0.2f;          // per-pixel EMA (as v2): noise 0.26 -> 0.09 C

  float q = 0;  // last low quartile of the rise over the reference (0 while clear)

  // One frame (C) and whether the doorway reads occupied (either detector). Returns true once
  // when the backgrounds should be relearned (the caller resets both detectors, then reset()).
  bool update(const float t[64], bool occupied, uint32_t now_ms) {
    for (int i = 0; i < 64; i++) s_[i] = have_ ? s_[i] + kA * (t[i] - s_[i]) : t[i];
    if (!have_ || !occupied) memcpy(ref_, s_, sizeof ref_);
    have_ = true;
    if (!occupied) { q = 0; since_ = 0; return false; }
    float rise[64];
    for (int i = 0; i < 64; i++) rise[i] = s_[i] - ref_[i];
    q = lowQuartile(rise);
    if (q < kQ) { since_ = 0; return false; }
    if (!since_) since_ = now_ms ? now_ms : 1;
    return now_ms - since_ >= kHoldMs;
  }
  // How long the drift condition has held (0 = not now).
  uint32_t heldMs(uint32_t now_ms) const { return since_ ? now_ms - since_ : 0; }
  void reset() { have_ = false; since_ = 0; q = 0; }

 private:
  float s_[64], ref_[64];
  bool have_ = false;
  uint32_t since_ = 0;
};

}  // namespace ge

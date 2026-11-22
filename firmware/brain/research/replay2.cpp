// Host tests and capture replay for src/presence2.h (and the current src/presence.h).
//   c++ -std=c++17 -O2 -I../src replay2.cpp -o /tmp/replay2
//   /tmp/replay2 --test                         unit checks on synthetic frames
//   /tmp/replay2 <capture.log> [out.csv]        replay; CSV: frame,present,occupied,score,
//                                               v1_present,v1_occupied (one row per frame)
// Door state comes from the log's `# door: <old> -> <new>` lines (closed = "closed").
// A reset of the device ms counter (reboot) starts fresh detectors.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <random>
#include "presence.h"
#include "presence2.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void fill(float *f, float v) { for (int i = 0; i < 64; i++) f[i] = v; }

static void tests() {
  std::mt19937 rng(1);
  std::normal_distribution<float> nz(0.0f, 0.26f);
  auto noisy = [&](float *f, float base) { for (int i = 0; i < 64; i++) f[i] = roundf((base + nz(rng)) * 4) / 4; };
  float f[64];
  // 1. Noise only, 10 min: nothing.
  {
    ge2::Detector d; int p = 0, o = 0;
    for (int k = 0; k < 6000; k++) { noisy(f, 17.0f); ge2::Result r = d.update(f, true); p += r.present; o += r.occupied; }
    CHECK(p == 0 && o == 0);
  }
  // 2. A single warm pixel: +1.0 C (dog at the edge of the view) -> occupied within 1 s, not present.
  //    presence.h scores ~0.35 per frame here, under its occ_on 0.6.
  {
    ge2::Detector d; ge::Detector v1; ge2::Result r; ge::Result r1;
    for (int k = 0; k < 600; k++) { noisy(f, 17.0f); d.update(f, true); v1.update(f); }
    int k_occ = -1, v1_occ = 0;
    for (int k = 0; k < 120; k++) {
      noisy(f, 17.0f); f[34] += 1.0f;
      r = d.update(f, true); r1 = v1.update(f);
      if (r.occupied && k_occ < 0) k_occ = k;
      v1_occ += r1.occupied;
    }
    CHECK(k_occ >= 0 && k_occ <= 10);
    CHECK(r.occupied && !r.present);
    printf("  single warm pixel: v2 occupied after %d frames; presence.h occupied %d/120 frames\n", k_occ, v1_occ);
  }
  // 3. A dog-sized blob (4 px +2 C) -> present within 0.5 s; clears 2-5 s after it leaves.
  {
    ge2::Detector d; ge2::Result r;
    for (int k = 0; k < 600; k++) { noisy(f, 17.0f); d.update(f, true); }
    int k_on = -1;
    for (int k = 0; k < 50; k++) {
      noisy(f, 17.0f); f[33] += 2; f[34] += 2; f[41] += 2; f[42] += 2;
      r = d.update(f, true);
      if (r.present && k_on < 0) k_on = k;
    }
    CHECK(k_on >= 0 && k_on <= 5);
    int k_off = -1;
    for (int k = 0; k < 200; k++) { noisy(f, 17.0f); r = d.update(f, true); if (!r.occupied && k_off < 0) k_off = k; }
    CHECK(k_off >= 20 && k_off <= 60);
  }
  // 4. Door opens: doorway column cools 1 C; a dog there (+1.2 over the cold scene, i.e. only
  //    +0.2 over the closed-door background) must still be occupied. Then the door closes and
  //    the scene warms back over 90 s: that must not read as occupied.
  {
    ge2::Detector d; ge2::Result r;
    for (int k = 0; k < 600; k++) { noisy(f, 17.0f); d.update(f, true); }
    for (int k = 0; k < 300; k++) {  // open, cooling ramps in over 10 s
      noisy(f, 17.0f); float c = k < 100 ? k / 100.0f : 1.0f;
      for (int row = 2; row < 7; row++) f[row * 8 + 3] -= c;
      r = d.update(f, false);
      CHECK(!r.occupied);
    }
    int occ = 0;
    for (int k = 0; k < 50; k++) {  // dog
      noisy(f, 17.0f); for (int row = 2; row < 7; row++) f[row * 8 + 3] -= 1.0f;
      f[4 * 8 + 3] += 1.2f;
      r = d.update(f, false); occ += r.occupied;
    }
    CHECK(occ >= 40);
    for (int k = 0; k < 100; k++) {  // dog leaves, door still open
      noisy(f, 17.0f); for (int row = 2; row < 7; row++) f[row * 8 + 3] -= 1.0f;
      r = d.update(f, false);
    }
    CHECK(!r.occupied);
    int false_occ = 0;
    for (int k = 0; k < 1500; k++) {  // closed, warms back over 90 s
      noisy(f, 17.0f); float c = k < 900 ? 1.0f - k / 900.0f : 0.0f;
      for (int row = 2; row < 7; row++) f[row * 8 + 3] -= c;
      r = d.update(f, true); false_occ += r.occupied;
    }
    CHECK(false_occ == 0);
  }
  // 5. Cooling never counts.
  {
    ge2::Detector d; ge2::Result r;
    for (int k = 0; k < 300; k++) { fill(f, 20); d.update(f, true); }
    for (int k = 0; k < 100; k++) { fill(f, 5); r = d.update(f, true); }
    CHECK(!r.present && !r.occupied);
  }
  // 6. Weights: a zero-weight cell can't trigger presence; occupied caps can't be exceeded.
  {
    ge2::Detector d; ge2::Result r;
    d.p.w[34] = 0; d.p.occ_peak = 5.0f; d.p.occ_score = 5.0f;  // user tries to blind "occupied"
    for (int k = 0; k < 300; k++) { noisy(f, 17.0f); d.update(f, true); }
    for (int k = 0; k < 30; k++) { noisy(f, 17.0f); f[34] += 3; r = d.update(f, true); }
    CHECK(!r.present && r.occupied);
  }
  // 7. A dog lying still for 5 min stays occupied (warm pixels learn at ~8 min).
  {
    ge2::Detector d; ge2::Result r;
    for (int k = 0; k < 300; k++) { noisy(f, 17.0f); d.update(f, true); }
    int occ = 0;
    for (int k = 0; k < 3000; k++) { noisy(f, 17.0f); f[32] += 1.0f; f[33] += 1.0f; r = d.update(f, true); occ += r.occupied; }
    CHECK(occ >= 2990);
  }
  printf("tests: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
}

static void replay(const char *path, const char *out) {
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); exit(1); }
  FILE *o = out ? fopen(out, "w") : nullptr;
  if (o) fprintf(o, "frame,present,occupied,score,v1_present,v1_occupied\n");
  ge2::Detector d; ge::Detector v1;
  static char line[8192];
  bool closed = true, wasP = false, wasO = false;
  long last_ms = -1; int n = 0, eps_p = 0, eps_o = 0;
  while (fgets(line, sizeof line, f)) {
    char *q = strstr(line, " # door: ");
    if (q) { char *arrow = strstr(q, "-> "); if (arrow) closed = strncmp(arrow + 3, "closed", 6) == 0; continue; }
    char *p = strstr(line, " # F ");
    if (!p) continue;
    p += 5;
    long msv = strtol(p, &p, 10);
    float fr[64]; int k = 0;
    for (; k < 64; k++) { char *e; long v = strtol(p, &e, 10); if (e == p) break; fr[k] = v / 4.0f; p = e; }
    if (k != 64) continue;
    if (last_ms >= 0 && (msv <= last_ms || msv - last_ms > 5000)) { d.reset(); v1.reset(); }
    last_ms = msv;
    ge2::Result r = d.update(fr, closed);
    ge::Result r1 = v1.update(fr);
    if (r.present && !wasP) { eps_p++; printf("%.15s present ON  score %.2f peak %.2f (r%dc%d) warm %d\n", line, r.score, r.peak, r.peak_i / 8, r.peak_i % 8, r.warm); }
    if (r.occupied && !wasO) { eps_o++; printf("%.15s occupied ON score %.2f peak %.2f (r%dc%d)\n", line, r.occ_score, r.peak, r.peak_i / 8, r.peak_i % 8); }
    if (!r.occupied && wasO) printf("%.15s occupied off\n", line);
    wasP = r.present; wasO = r.occupied;
    if (o) fprintf(o, "%d,%d,%d,%.3f,%d,%d\n", n, r.present, r.occupied, r.score, r1.present, r1.occupied);
    n++;
  }
  printf("== %s: %d frames, v2 presence episodes %d, occupied episodes %d\n", path, n, eps_p, eps_o);
  if (o) fclose(o);
  fclose(f);
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--test")) { tests(); return fails ? 1 : 0; }
  if (argc < 2) { fprintf(stderr, "usage: %s --test | <capture.log> [out.csv]\n", argv[0]); return 2; }
  replay(argv[1], argc > 2 ? argv[2] : nullptr);
  return 0;
}

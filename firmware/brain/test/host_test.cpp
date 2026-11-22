// Host tests for the pure logic, run against real captures:
//   c++ -std=c++17 -I../src host_test.cpp -o /tmp/host_test && /tmp/host_test <capture.log>...
// 1. Feeds every channel-B (STM -> host) byte of each capture to stm::Parser and prints the
//    events, so the parse can be checked by eye.
// 2. Unit checks on known lines and on the presence detector with synthetic frames.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "stm_parse.h"
#include "presence.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static stm::Parser feedStr(const char *s, int *events, int nev) {
  stm::Parser p;
  for (int i = 0; i < nev; i++) events[i] = 0;
  for (const char *c = s; *c; c++) events[p.feed(*c)]++;
  return p;
}

static void unit() {
  int ev[6];
  stm::Parser p = feedStr("EU3Z0%1BH0;1E0v0=FFFFFFB8p3514K\r\n", ev, 6);
  CHECK(ev[stm::STATUS] == 1);
  CHECK(p.st.U == 0x0E && p.st.Z == 3 && p.st.p == -72 && p.st.K == 0x3514 && p.st.H == 0x1B);
  CHECK(p.version == 0x1E0);
  p = feedStr("1BU0ZFFFE7960%226H0;1E0v0=FFFFFE50p0K\r\n", ev, 6);
  CHECK(p.st.U == 0x1B && p.st.dest == -100000 && p.st.p == -432);
  p = feedStr("@@0U0Z0%0H0;1E0v0=0{0^0(0)4`0#4U0Z0%1BH0;1E0v0=0p0K\r\n", ev, 6);
  CHECK(ev[stm::BOOT] == 1 && ev[stm::MOVE] == 1 && ev[stm::STATUS] == 1 && p.st.U == 4);
  p = feedStr("0{FFFFCB9F^1C(11A)1D`1C#", ev, 6);
  CHECK(ev[stm::MOVE] == 1 && p.mv.pos == -13409 && p.mv.state == 0x1D);
  p = feedStr("11606\r\n", ev, 6);
  CHECK(ev[stm::REPLY] == 1 && p.reply == 11606);
  p = feedStr("-6143\r\n", ev, 6);
  CHECK(ev[stm::REPLY] == 1 && p.reply == -6143);
  p = feedStr("1E0v", ev, 6);
  CHECK(p.version == 0x1E0 && ev[stm::REPLY] == 0);
  p = feedStr("~", ev, 6);
  CHECK(ev[stm::WIPED] == 1);
  // A status line broken by a garbage byte must not produce a bogus STATUS.
  p = feedStr("4U0Z0%1C", ev, 6);
  p.feed('\0');
  CHECK(p.st.U == -1);

  CHECK(stm::phase(0x0E) == stm::PH_CLOSED && stm::phase(0x0F) == stm::PH_OPEN && stm::phase(0x27) == stm::PH_FAILED);
  CHECK(ge::pixelC(0x40, 0x00) == 16.0f && ge::pixelC(0xFC, 0x0F) == -1.0f);

  // Presence: flat 20 C scene, then a 4-pixel 24 C blob, then gone.
  ge::Detector d;
  float f[64];
  for (int i = 0; i < 64; i++) f[i] = 20.0f;
  for (int k = 0; k < 25; k++) d.update(f);
  CHECK(d.ready);
  ge::Result r = d.update(f);
  CHECK(!r.present && r.hot == 0);
  f[27] = f[28] = f[35] = f[36] = 24.0f;
  int k_on = 0;
  for (; k_on < 10 && !(r = d.update(f)).present; k_on++) {}
  CHECK(r.present && k_on <= 3);  // on within 0.4 s
  for (int i = 0; i < 64; i++) f[i] = 20.0f;
  int k_off = 0;
  for (; k_off < 100 && (r = d.update(f)).present; k_off++) {}
  CHECK(!r.present && k_off >= 20 && k_off <= 40);  // holds >= 2 s, clears within 4 s
  // Cooling (door opened to cold air) never counts.
  for (int i = 0; i < 64; i++) f[i] = 5.0f;
  for (int k = 0; k < 5; k++) r = d.update(f);
  CHECK(!r.present && r.hot == 0);
  // A weak signal (a dog right beside the panel: smoothed ~0.6-1.3) must make the doorway
  // "occupied" (no closing) without necessarily counting as presence, and stay occupied while
  // it lasts: a still dog must not be learned into the background.
  {
    ge::Detector w;
    float g[64];
    for (int i = 0; i < 64; i++) g[i] = 16.5f;
    for (int k = 0; k < 25; k++) w.update(g);
    g[18] = g[26] = 16.5f + 0.7f + 0.45f;  // score 0.9
    ge::Result q2;
    for (int k = 0; k < 10; k++) q2 = w.update(g);
    CHECK(q2.occupied && !q2.present);
    for (int k = 0; k < 600; k++) q2 = w.update(g);  // 60 s standing still
    CHECK(q2.occupied);
    for (int i = 0; i < 64; i++) g[i] = 16.5f;
    int k = 0;
    for (; k < 100 && (q2 = w.update(g)).occupied; k++) {}
    CHECK(!q2.occupied && k >= 30);  // clear only after >= 3 s
  }
  // Mask excludes pixels.
  ge::Detector m;
  m.p.mask = ~((1ULL << 27) | (1ULL << 28) | (1ULL << 35) | (1ULL << 36));
  for (int i = 0; i < 64; i++) f[i] = 20.0f;
  for (int k = 0; k < 25; k++) m.update(f);
  f[27] = f[28] = f[35] = f[36] = 30.0f;
  for (int k = 0; k < 10; k++) r = m.update(f);
  CHECK(!r.present && r.hot == 0);
}

// Replay Grid-EYE frames ("<time> # F <ms> t0..t63", C x4) through the detector.
static void replayFrames(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return;
  ge::Detector d;
  char line[4096];
  bool was = false;
  int n = 0, on = 0;
  float maxScore = 0;
  char tmax[16] = {0};
  while (fgets(line, sizeof line, f)) {
    char *p = strstr(line, " # F ");
    if (!p) continue;
    p += 5;
    strtol(p, &p, 10);  // ms
    float fr[64];
    int k = 0;
    for (; k < 64; k++) { char *e; long v = strtol(p, &e, 10); if (e == p) break; fr[k] = v / 4.0f; p = e; }
    if (k != 64) continue;
    ge::Result r = d.update(fr);
    n++;
    if (r.smoothed > maxScore && !r.present) { maxScore = r.smoothed; strncpy(tmax, line, 15); }
    if (r.present != was) {
      printf("%.15s presence %s smoothed=%.1f hot=%d max=+%.1f\n", line, r.present ? "ON " : "off", r.smoothed, r.hot, r.max_delta);
      was = r.present;
      if (r.present) on++;
    }
  }
  fclose(f);
  if (n) printf("== frames %s: %d frames, %d presence episodes, highest smoothed score while absent %.2f at %s\n", path, n, on, maxScore, tmax);
}

// Feed channel-B bytes from a capture line "<time> <secs> B [n] HH HH ... |ascii|".
static void replay(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); return; }
  stm::Parser p;
  char line[4096];
  int counts[6] = {0};
  int lastU = -2;
  while (fgets(line, sizeof line, f)) {
    char *b = strstr(line, " B [");
    if (!b) continue;
    char *hx = strchr(b, ']');
    if (!hx) continue;
    hx++;
    char t[16] = {0};
    strncpy(t, line, 15);
    while (*hx == ' ') hx++;
    while (hx[0] && hx[1] && hx[0] != ' ' && hx[0] != '|') {
      char c = (char)strtol(std::string(hx, 2).c_str(), nullptr, 16);
      stm::Event e = p.feed(c);
      counts[e]++;
      if (e == stm::STATUS && p.st.U != lastU) {
        printf("%s STATUS U=%X (%s) Z=%d dest=%d H=%X p=%d K=%d\n", t, p.st.U, stm::phaseName(stm::phase(p.st.U)),
               p.st.Z, p.st.dest, p.st.H, p.st.p, p.st.K);
        lastU = p.st.U;
      } else if (e == stm::BOOT) {
        printf("%s BOOT\n", t);
        lastU = -2;
      } else if (e == stm::MOVE) {
        printf("%s MOVE pos=%d state=%X I=%X lim=%X\n", t, p.mv.pos, p.mv.state, p.mv.current, p.mv.limit);
      } else if (e == stm::WIPED) {
        printf("%s WIPED\n", t);
      }
      hx += 3;
    }
  }
  fclose(f);
  printf("== %s: status=%d move=%d boot=%d reply=%d wiped=%d\n", path, counts[1], counts[2], counts[3], counts[4],
         counts[5]);
}

int main(int argc, char **argv) {
  unit();
  for (int i = 1; i < argc; i++) { replay(argv[i]); replayFrames(argv[i]); }
  printf(fails ? "%d FAILED\n" : "all unit checks passed\n", fails);
  return fails != 0;
}

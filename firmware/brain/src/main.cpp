// wayzn-brain: production firmware for the ESP32 that replaces the imp on the Wayzn
// WYP-BC-100 board. The STM32 keeps running Wayzn's motor firmware; this drives its shell
// over USART1 and read the Grid-EYE ourselves. See docs/STM-FIRMWARE.md and docs/PLAN.md.
//
// Principles (AGENTS.md): never lie about state (unknown is a valid answer); open beats
// oscillation; oscillating is a fault; never lock a dog out (faults fail OPEN).
//
// Wiring (docs/wiring.svg): GPIO26 <- STM TX (header TX), GPIO25 -> STM RX (header RX),
// GPIO27 -> STM NRST (SW300, open-drain), GPIO21/22 = I2C SCL/SDA (TP1/TP2, board pull-ups).
//
// Console: USB serial or `nc wayzn-esp.local 23`. `?` lists commands.
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <deque>
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "esp_system.h"
#include "net.h"
#include "stm_parse.h"
#include "presence.h"
#include "presence2.h"
#include "drift.h"
#include <ArduinoJson.h>
#include "api.h"

#define OUT net::out

static constexpr int PIN_STM_RX = 26, PIN_STM_TX = 25, PIN_STM_RST = 27, PIN_SCL = 21, PIN_SDA = 22;
static constexpr uint8_t GE_ADDR = 0x69;
static constexpr uint32_t SEND_GAP_MS = 250;        // the STM shell drops lines sent back to back
static constexpr uint32_t KEEPALIVE_MS = 20000;     // a line < 25 s apart keeps 1.5 s status
static constexpr uint32_t SILENT_RESET_MS = 30000;  // STM prints at least every 10 s
static constexpr uint32_t MOVE_TIMEOUT_MS = 45000, CAL_TIMEOUT_MS = 180000;
static constexpr int REVERSALS_FAULT = 3;
static constexpr uint32_t REVERSAL_WINDOW_MS = 120000;

// ---------- settings (NVS) ----------
static Preferences prefs;
static bool s_auto = false;      // Grid-EYE auto-open
static bool s_auto_close = true; // auto-close after a presence open (only while auto-open is on)
static bool s_obst = true;       // Grid-EYE obstruction detection: blocks closing, reverses a close
static uint32_t s_hold_s = 15;   // close this long after the doorway is clear
// Expected full travel (counts), a hint set from Home Assistant: a calibration that measures a
// travel more than CAL_TRAVEL_TOL away is treated as failed (0 = no check). Not a door constant.
static int32_t s_expected_travel = 0;
static constexpr float CAL_TRAVEL_TOL = 0.02f;
// auto_zero's closing pass counts up from the open end. Its last status before a reboot at the
// closed stall lags the stall by up to 1.5 s (seen: p 12333 and 12553 for a 13588 travel), so
// "at the closed end" is past this fraction of the expected travel.
static constexpr float CAL_END_FRAC = 0.85f;
static int32_t s_open_counts = -6136;            // set_open_to (8 in = 45.2 %)
static constexpr float COUNTS_PER_INCH = 767.0f; // the imp's set_open_to mapping (8 in = -6136, 12 in = -9204)
static bool s_log = false;       // print every STM status line
// The STM's learned motor-effort profile (open and close tables, 144 points each). auto_zero clears
// and relearns it, but the STM keeps it only in RAM: the brain reads it back after a good
// calibration (`get_profile`), keeps it in NVS and sends it after every STM boot. Learned, not a
// firmware constant; with none stored the STM runs on its default (wider) margins.
static constexpr int PROF_N = 144;
static uint16_t prof_tab[2][PROF_N];
static bool prof_ok = false;
static bool profValid(const uint16_t t[2][PROF_N]) {
  for (int d = 0; d < 2; d++) {
    int nz = 0;
    for (int i = 0; i < PROF_N; i++) {
      if (t[d][i] > 0xFFF) return false;
      if (t[d][i]) nz++;
    }
    if (nz < 100) return false;  // a full travel fills ~140 points
  }
  return true;
}

static void loadSettings(ge::Detector &d) {
  prefs.begin("brain", false);
  s_auto = prefs.getBool("auto", false);
  s_hold_s = prefs.getUInt("hold", 15);
  s_auto_close = prefs.getBool("aclose", true);
  s_obst = prefs.getBool("obst", true);
  s_open_counts = prefs.getInt("opento", -6136);
  s_expected_travel = prefs.getInt("etravel", 0);
  prof_ok = prefs.getBytes("prof", prof_tab, sizeof prof_tab) == sizeof prof_tab && profValid(prof_tab);
  d.p.occ_on = prefs.getFloat("occ", 0.6f);
  d.p.occ_off = d.p.occ_on * 0.58f;
  d.p.on_score = prefs.getFloat("on2", 1.0f);
  d.p.off_score = prefs.getFloat("off2", 0.6f);
  d.p.noise_c = prefs.getFloat("noise", 0.7f);
  d.p.mask = prefs.getULong64("mask", ~0ULL);
}
static void saveSettings(const ge::Detector &d) {
  prefs.putBool("auto", s_auto);
  prefs.putUInt("hold", s_hold_s);
  prefs.putBool("aclose", s_auto_close);
  prefs.putBool("obst", s_obst);
  prefs.putInt("opento", s_open_counts);
  prefs.putInt("etravel", s_expected_travel);
  prefs.putFloat("occ", d.p.occ_on);
  prefs.putFloat("on2", d.p.on_score);
  prefs.putFloat("off2", d.p.off_score);
  prefs.putFloat("noise", d.p.noise_c);
  prefs.putULong64("mask", d.p.mask);
}

static void saveProfile() { prefs.putBytes("prof", prof_tab, sizeof prof_tab); }

// ---------- STM link ----------
static HardwareSerial Stm(1);
static stm::Parser P;
static std::deque<String> q;
static bool tx_on = false, heard = false, have_status = false;
static uint32_t last_rx_ms = 0, last_tx_ms = 0, last_rst_ms = 0, boot_ms = 0;
static uint32_t stm_boots = 0, stm_resets = 0;

static void attachTx() {
  if (tx_on) return;
  gpio_set_level((gpio_num_t)PIN_STM_TX, 1);
  gpio_set_direction((gpio_num_t)PIN_STM_TX, GPIO_MODE_OUTPUT);
  esp_rom_gpio_connect_out_signal(PIN_STM_TX, U1TXD_OUT_IDX, false, false);
  tx_on = true;
  delay(2);
  Stm.print('\n');  // attaching can make a glitch byte; end it as its own (ignored) line
}
static void send(const String &l, bool urgent = false) {
  if (urgent) q.push_front(l); else q.push_back(l);
}
// Profile read-back (`get_profile`): the STM prints 288 values, 25 ms apart (~7 s). Nothing else is
// sent meanwhile (PROF_READ_MS at most); it is only started with the door idle and the doorway clear.
static constexpr uint32_t PROF_READ_MS = 10000;
static bool prof_want = false, prof_reading = false;
static uint32_t prof_read_ms = 0;
static uint16_t prof_new[2][PROF_N];
static int prof_cnt[2] = {0, 0};

static void pumpQueue() {
  if (!tx_on || q.empty() || millis() - last_tx_ms < SEND_GAP_MS) return;
  if (prof_reading && millis() - prof_read_ms < PROF_READ_MS) return;
  String l = q.front();
  q.pop_front();
  Stm.print(l);
  Stm.print('\n');
  last_tx_ms = millis();
  if (!l.startsWith("set_profile") && l != "c") OUT.printf("# stm< %s\n", l.c_str());
  if (l == "get_profile") {
    prof_reading = true;
    prof_read_ms = millis();
    prof_cnt[0] = prof_cnt[1] = 0;
  }
}
static void stmReset(const char *why) {
  gpio_set_level((gpio_num_t)PIN_STM_RST, 0);
  delay(20);
  gpio_set_level((gpio_num_t)PIN_STM_RST, 1);
  last_rst_ms = last_rx_ms = millis();
  stm_resets++;
  OUT.printf("# stm reset pulse (%s)\n", why);
}

// ---------- door model ----------
enum PosKnow { POS_UNKNOWN, POS_KNOWN, POS_INFERRED };
static PosKnow pos_know = POS_UNKNOWN;
static const char *pos_why = "no STM status yet";
static stm::DoorPhase phase = stm::PH_UNKNOWN;
static uint32_t phase_since = 0;
static bool configuring = false, zero_after_config = false, zero_inferred = false;
static int32_t zero_k = 0;  // travel to set before an inferred zero (set_dooroffset)
static uint32_t last_cfg_ms = 0;
static bool calibrating = false;
// Position recovery: after a start (power cut, STM reboot) the STM has no position. The brain
// runs its auto_zero by itself once the doorway has been clear 10 s; an aborted or failed run is
// retried after 3 min; 3 failures in a row latch a fault (door open, HA alerted).
static constexpr int RECAL_MAX = 3;
static constexpr uint32_t RECAL_CLEAR_MS = 10000, RECAL_RETRY_MS = 180000;
static int recal_tries = 0;
static uint32_t recal_next = 0;
static bool cal_bad = false;        // the STM is zeroed, but by a calibration we rejected
static bool open_after_cal = false; // an open came in during a calibration: run it at the end
static void calEnded();
static uint32_t cal_start = 0;
static bool fault = false;
static String fault_why;
static bool fault_open = false;  // the fault wants the door open (retried); a stopped-move fault doesn't
static uint32_t fault_try_ms = 0;
static int fault_tries = 0;
static uint32_t reversals[8];
static int n_rev = 0;
static uint32_t open_since = 0;
static uint32_t last_status_ms = 0;
// Opened by Home Assistant: hold open (no auto-close) until HA closes it. Overnight HA lets
// the dogs back in from outside, where the Grid-EYE may not see them coming.
static bool api_hold = false;
// A close asked for while the doorway is occupied waits here and runs once the doorway has been
// clear for PENDING_CLEAR_MS (callers such as HA automations fire once and don't retry). It expires
// after PENDING_MAX_MS and is cancelled by any open or a fault.
static constexpr uint32_t PENDING_CLEAR_MS = 3000, PENDING_MAX_MS = 300000;
static bool close_pending = false;
static uint32_t close_pending_since = 0;
static void cancelPendingClose(const char *why);

// ---------- Grid-EYE ----------
static ge::Detector det;
static ge::Result ge_last;
// v2 detector (docs/DETECTION-RESEARCH.md): the doorway is occupied if it or the original says so.
static ge2::Detector det2;
// v2's closed-door snapshot survives software resets (OTA, crash) in RTC memory, so a reboot
// while the door is open doesn't turn the post-close warm-up into a false obstruction.
struct RtcRef { uint32_t magic; float ref[64]; uint32_t sum; };
RTC_NOINIT_ATTR static RtcRef rtc_ref;
static constexpr uint32_t RTC_REF_MAGIC = 0x57524546;  // "WREF"
static uint32_t rtcRefSum(const RtcRef &r) {
  uint32_t h = 2166136261u;
  const uint8_t *b = (const uint8_t *)r.ref;
  for (size_t i = 0; i < sizeof r.ref; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}
static uint32_t ref_seq_saved = 0;
static ge2::Result g2;
static ge::DriftWatch drift;  // scene-wide drift read as "occupied" (src/drift.h)
static uint32_t bg_relearns = 0;
static uint32_t g2_occ_since = 0, g2_pres_since = 0, g2_would_block = 0, g2_would_reverse = 0;
// Stage 2 (docs/DETECTION-RESEARCH.md): the doorway counts as occupied if EITHER detector says so.
// This can only block or reverse more closes, never fewer. v2 sees a small dog that warms a
// single pixel by ~1 C, which the original detector averages away.
static bool occupiedAny() { return ge_last.occupied || g2.occupied; }
static uint32_t last_present_seen = 0, last_occ_seen = 0;  // for the API: how long ago, so a short
static bool present_seen = false, occ_seen = false;         // visit between two polls still shows
static float frame[64];
static bool ge_ok = false, ge_stream = false, ge_raw = false;
static uint32_t ge_fail = 0, last_presence_ms = 0, last_occupied_ms = 0, last_ge_ms = 0;
static bool obstr_sent = false;   // what we last told the STM
// After a reversal the STM waits in state 7 and recloses by itself as soon as it gets
// clear_obstruction (verified again 2026-11-22 11:49:05.966 and 11:58:16.123). rev_keep: don't
// clear it (stay open) because an explicit open (HA, console; not presence), a hold or a fault came
// in since, or the doorway stayed occupied for 5 min. Cleared by a close request.
static bool rev_keep = false;
static uint32_t obstr_set_ms = 0, last_clear_ms = 0;
static int cur_U = -1;  // STM control state, from status lines and (sooner) move summaries
static int cur_now = 0, move_peak = 0, last_move_peak = -1;  // motor current (STM units: H / `c`)
// Effort: motor current as a % of the STM's own current limit at that moment, from its move
// summaries (printed at each state change; the limit follows the learned profile). Over 100 % for
// ~350 ms is the STM's own overcurrent reversal. The current's scale in amps isn't known.
static int move_effort = -1, last_move_effort = -1, last_effort_cur = 0, last_effort_lim = 0;
static uint32_t cur_poll_ms = 0;
static String last_motion;        // last motion command sent (see motion())
static uint32_t last_motion_ms = 0;
static uint32_t obstr_ms = 0;

static void raiseFault(const String &why);
static void requestOpen(const char *why, bool force = false);
static void cancelMotion();
static void clearFault(const char *who);
static void startCal(const char *why);
static bool calRefused(String &no);

// Door state that must survive a software reset (OTA, crash, watchdog), in RTC memory: the HA
// hold, a latched fault, the obstruction flag and post-reversal hold, and the last empty-doorway
// backgrounds of both detectors (so a reboot with a dog in the doorway doesn't learn the dog as
// background). A power cut clears it; that also resets the STM, which then has no position and
// won't close until calibrated.
struct RtcState {
  uint32_t magic;
  uint8_t api_hold, fault, fault_open, obstr, rev_keep, bg_ok;
  char fault_why[96];
  float bg1[64], bg2[64], var2[64];
  uint32_t sum;
};
RTC_NOINIT_ATTR static RtcState rtc_st;
static constexpr uint32_t RTC_ST_MAGIC = 0x57535432;  // "WST2"
static uint32_t rtcStSum() {
  uint32_t h = 2166136261u;
  const uint8_t *b = (const uint8_t *)&rtc_st + sizeof rtc_st.magic;
  for (size_t i = 0; i < offsetof(RtcState, sum) - sizeof rtc_st.magic; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}

// STM setup (no motion), sent after every STM boot. Door-specific values come from NVS (the learned
// profile, the open position) or, for an inferred zero, from the travel (`k`).
static void pushConfig(bool zero_closed, int32_t k = 0) {
  int n = 0;
  if (prof_ok) {
    for (int d = 0; d < 2; d++)
      for (int i = 0; i < PROF_N; i += 8) {
        String l = String("set_profile ") + (d ? "close " : "open ") + i;
        for (int j = i; j < i + 8; j++) l += String(" ") + prof_tab[d][j];
        send(l);
        n++;
      }
  }
  send(String("set_open_to ") + s_open_counts);
  send("set_open_wait 15");  // unused: the brain closes the door itself
  send("set_button_op 1");   // the physical button works (opens from closed)
  n += 3;
  if (prof_ok) { send("profiled_state 1"); n++; }
  configuring = true;
  zero_after_config = zero_closed && k > 0;
  zero_k = k;
  last_cfg_ms = millis();
  OUT.printf("# config: sending %d setup lines (profile: %s)%s\n", n,
             prof_ok ? "stored" : "none stored, the STM uses its default margins until a calibration",
             zero_after_config ? ", then zero as closed" : "");
}

static void onBoot() {
  stm_boots++;
  // A reboot at the closed stall (auto_zero pass 14 reaching closed can brown the STM out; the
  // brain resets it there itself, calStall) means the door is physically closed: say so, as
  // inferred. The zero needs the travel: the STM's own (a close near closed) or the Expected
  // travel setting (calibration: the STM hadn't measured it yet).
  const stm::Status &s = P.st;
  int32_t k = 0;
  pos_why = "STM rebooted";
  if (have_status && s.U == 0x14) {
    if (s_expected_travel <= 0) pos_why = "STM rebooted in calibration (closed not inferred: no Expected travel set)";
    else if (s.p > CAL_END_FRAC * s_expected_travel) k = s_expected_travel;
  } else if (have_status && stm::phase(s.U) == stm::PH_CLOSING && s.p > -600) {
    k = s.K > 0 ? s.K : s_expected_travel;
  }
  bool closed_stall = k > 0;
  pos_know = closed_stall ? POS_INFERRED : POS_UNKNOWN;
  if (closed_stall) pos_why = "STM rebooted at the closed stall";
  OUT.printf("# STM BOOT #%u (last U=%X p=%d): position %s\n", (unsigned)stm_boots, s.U, s.p,
             closed_stall ? "inferred CLOSED" : "UNKNOWN");
  if (calibrating) {
    calibrating = false;
    OUT.printf("# calibration: %s\n", closed_stall ? "reached closed (STM rebooted at the stall)"
                                                   : "FAILED: STM rebooted mid-run");
    if (!closed_stall && recal_tries >= RECAL_MAX) raiseFault("calibration failed " + String(recal_tries) + " times");
    else if (!closed_stall) OUT.printf("# calibration: will retry (%d/%d so far)\n", recal_tries, RECAL_MAX);
    calEnded();
  }
  q.clear();
  obstr_sent = false;  // the STM's flag restarts with it
  rev_keep = false;
  last_motion = "";
  prof_reading = false;
  pushConfig(closed_stall, k);
}

// Close recorder state (functions further down, at "close recorder").
static bool cap_on = false;             // a close is being captured
static uint32_t cap_evt = 0, cap_t0 = 0, cap_until = 0, cap_next = 0, cap_sent = 0, cap_lost = 0;
static void captureStart();

// A close the brain didn't command (the door's button; nothing else starts one on its own) gets
// the rules of a Home Assistant close: it releases the hold; refused (reversed, door stays open)
// on a fault, an unknown position or a sensor that isn't reading; with the doorway occupied it is
// reversed by obstruction() and the STM recloses once the doorway is clear, like a pending close.
static void onCloseStarted() {
  uint32_t now = millis();
  bool ours = calibrating || configuring || now - last_clear_ms < 8000 ||
              ((last_motion == "close" || last_motion == "engage" || last_motion == "open_and_close") &&
               now - last_motion_ms < 8000);
  if (ours) return;
  OUT.println("# close not commanded by the brain (door button): Home Assistant close rules apply");
  cancelPendingClose("button close");
  if (api_hold) { api_hold = false; OUT.println("# api hold released (button close)"); }
  const char *no = fault ? "fault latched"
                   : pos_know != POS_KNOWN ? "position not known"
                   : (!ge_ok || !det.ready) ? "Grid-EYE not reading" : nullptr;
  if (no) {
    cancelMotion();
    send("set_obstruction", true);
    obstr_sent = rev_keep = true;
    obstr_ms = obstr_set_ms = now;
    OUT.printf("# button close refused (%s) -> set_obstruction: reverse and stay open\n", no);
  }
}

static void onPhase(stm::DoorPhase np, int pos) {
  if (np == phase) return;
  if (stm::moving(np) && !stm::moving(phase)) { move_peak = cur_now; move_effort = -1; }
  if (!stm::moving(np) && stm::moving(phase)) {
    last_move_peak = move_peak;
    last_move_effort = move_effort;
    OUT.printf("# move peak current %d; peak effort %d%% of the STM's limit\n", move_peak, move_effort);
  }
  OUT.printf("# door: %s -> %s (p=%d)\n", stm::phaseName(phase), stm::phaseName(np), pos);
  if (np == stm::PH_REVERSING) {
    uint32_t now = millis();
    reversals[n_rev++ % 8] = now;
    int recent = 0;
    for (int i = 0; i < 8 && i < n_rev; i++) if (now - reversals[i] < REVERSAL_WINDOW_MS) recent++;
    if (recent >= REVERSALS_FAULT) raiseFault("oscillation: " + String(recent) + " reversals in 2 min");
  }
  if (np == stm::PH_OPEN) open_since = millis();
  if (np == stm::PH_CLOSING && phase != stm::PH_CLOSING) {
    captureStart();
    // Never trust the STM's flag across a new close: re-arm, so an occupied doorway reverses it.
    if (obstr_sent) { obstr_sent = false; OUT.println("# close started: obstruction check re-armed"); }
    onCloseStarted();
  }
  if (np == stm::PH_CLOSED) rev_keep = false;
  if (cap_on) cap_until = stm::moving(np) ? 0 : millis() + 3000;
  if (np == stm::PH_CLOSED && api_hold) { api_hold = false; OUT.println("# api hold released (door closed)"); }
  if (np == stm::PH_FAILED) raiseFault("STM open failed (state 27)");
  phase = np;
  phase_since = millis();
}

static void onStatus() {
  const stm::Status &s = P.st;
  have_status = true;
  cur_U = s.U;
  cur_now = s.H;
  if (stm::moving(stm::phase(s.U)) && cur_now > move_peak) move_peak = cur_now;
  last_status_ms = millis();
  if (s_log) OUT.printf("# st U=%X Z=%d dest=%d H=%X p=%d K=%d\n", s.U, s.Z, s.dest, s.H, s.p, s.K);
  if ((s.Z & 1) && !cal_bad) {
    if (pos_know != POS_KNOWN) {
      pos_know = POS_KNOWN;
      pos_why = zero_inferred ? "zeroed as closed after a reboot at the closed stall" : "zeroed";
    }
  } else if (pos_know == POS_KNOWN) {
    pos_know = POS_UNKNOWN;
    pos_why = cal_bad ? "calibration rejected" : "STM lost its zero";
  }
  // STM running but not set up (e.g. it restarted while we weren't listening): set it up.
  // (Without a stored profile Z stays 0 until a calibration: nothing to resend.)
  if (prof_ok && !(s.Z & 2) && !configuring && !calibrating && millis() - last_cfg_ms > 30000) pushConfig(false);
  if (calibrating && s.Z == 3 && !stm::moving(stm::phase(s.U)) && stm::phase(s.U) != stm::PH_UNKNOWN) {
    calibrating = false;
    float off = s_expected_travel > 0 ? fabsf((float)s.K - s_expected_travel) / s_expected_travel : 0;
    if (off > CAL_TRAVEL_TOL) {
      // e.g. a pass that stopped on something short of the end: the zero is wrong, so is every
      // position after it. Don't use it; recalibrate once the doorway is clear.
      cal_bad = true;
      pos_know = POS_UNKNOWN;
      pos_why = "calibration rejected";
      recal_next = millis() + RECAL_CLEAR_MS;
      OUT.printf("# calibration: REJECTED, travel K=%d is %.1f%% off the expected %d (retry %d/%d)\n", s.K,
                 off * 100, (int)s_expected_travel, recal_tries, RECAL_MAX);
      if (recal_tries >= RECAL_MAX) raiseFault("calibration travel wrong " + String(recal_tries) + " times");
    } else {
      OUT.printf("# calibration: done, K=%d (%.1f in) p=%d%s\n", s.K, s.K / COUNTS_PER_INCH, s.p,
                 s_expected_travel > 0 ? " (travel as expected)" : " (no Expected travel set: not checked)");
      prof_want = true;  // the STM relearned its profile: keep a copy
    }
    calEnded();
  }
  onPhase(stm::phase(s.U), s.p);
}

static void profileRead() {
  prof_reading = false;
  if (!profValid(prof_new)) {
    OUT.println("# profile: read-back from the STM not valid; not stored");
    return;
  }
  bool same = !memcmp(prof_new, prof_tab, sizeof prof_tab) && prof_ok;
  memcpy(prof_tab, prof_new, sizeof prof_tab);
  prof_ok = true;
  if (!same) saveProfile();
  OUT.printf("# profile: read from the STM and %s (open[8]=%u close[8]=%u)\n", same ? "unchanged" : "stored",
             prof_tab[0][8], prof_tab[1][8]);
}

// Start a profile read-back when one is wanted (after a good calibration; at start-up with none
// stored and the STM profiled), with the door idle, the doorway clear and nothing queued.
static void profPoll(uint32_t now) {
  if (prof_reading && now - prof_read_ms >= PROF_READ_MS) {
    prof_reading = false;
    OUT.printf("# profile: read-back incomplete (%d open, %d close values); retry later\n", prof_cnt[0], prof_cnt[1]);
    prof_want = true;
  }
  if (!prof_ok && !prof_reading && have_status && (P.st.Z & 2) && !calibrating && !cal_bad) prof_want = true;
  if (!prof_want || prof_reading || !tx_on || !have_status || !q.empty() || configuring || calibrating || cal_bad)
    return;
  if (stm::moving(phase) || (phase != stm::PH_CLOSED && phase != stm::PH_OPEN) || !(P.st.Z & 2)) return;
  if (!ge_ok || occupiedAny() || ge_last.present || close_pending || now - last_tx_ms < 2000) return;
  static uint32_t last_try = 0;
  if (last_try && now - last_try < 60000) return;
  last_try = now;
  prof_want = false;
  send("get_profile");
  OUT.println("# profile: reading the STM's learned profile (~7 s, nothing else sent meanwhile)");
}

static void handleStmByte(char c) {
  last_rx_ms = millis();
  if (!heard) { heard = true; attachTx(); }
  stm::Event ev = P.feed(c);
  switch (ev) {
    case stm::STATUS: onStatus(); break;
    case stm::BOOT: onBoot(); break;
    case stm::MOVE: {
      int e = P.mv.limit > 0 ? (P.mv.current * 100 + P.mv.limit / 2) / P.mv.limit : -1;
      OUT.printf("# move: pos=%d -> state %X, current %X (limit %X, effort %d%%)\n", P.mv.pos, P.mv.state, P.mv.current,
                 P.mv.limit, e);
      if (e >= 0) {
        if (stm::moving(phase) && e > move_effort) {
          move_effort = e;
          last_effort_cur = P.mv.current;
          last_effort_lim = P.mv.limit;
        }
        if (!stm::moving(phase)) move_effort = -1;  // a move starting: onPhase takes it from here
      }
      // The move summary prints the moment the state changes; status lines lag up to 1.5 s.
      // Pseudo-states in the first summary line (1, 2, 3) map to unknown and are skipped.
      if (stm::phase(P.mv.state) != stm::PH_UNKNOWN) {
        cur_U = P.mv.state;
        onPhase(stm::phase(P.mv.state), P.mv.pos);
      }
      break;
    }
    case stm::PROF_OPEN:
    case stm::PROF_CLOSE: {
      int d = ev == stm::PROF_CLOSE;
      if (!prof_reading || prof_cnt[d] >= PROF_N || (d && prof_cnt[0] < PROF_N)) break;
      prof_new[d][prof_cnt[d]++] = (uint16_t)min(P.prof, 0xFFFF);
      if (d && prof_cnt[1] == PROF_N) profileRead();
      break;
    }
    case stm::WIPED:
      OUT.println("# STM: calibration wiped by the button (~)");
      pos_know = POS_UNKNOWN;
      pos_why = "calibration wiped (button)";
      pushConfig(false);
      break;
    case stm::REPLY:  // only `c` (motor current) is queried automatically
      cur_now = P.reply;
      if (stm::moving(phase) && cur_now > move_peak) move_peak = cur_now;
      break;
    default: break;
  }
}

// ---------- actions ----------
// Every motion command goes through motion(): it removes any motion command still queued,
// so at most one is ever pending, and it drops a repeat of the same command within
// MOTION_DEDUP_MS (the STM's status lags ~1.5 s, so the phase can't be used to dedupe).
// Repeated `close` commands would override the STM's own reversal and a fault's open, so a
// command is never queued twice.
static constexpr uint32_t MOTION_DEDUP_MS = 4000;
static const char *const kMotionCmds[] = {"open", "close", "stop", "disengage", "engage", "auto_zero",
                                          "reset_zero", "open_and_close"};
static bool isMotion(const String &l) {
  int sp = l.indexOf(' ');
  String w = sp < 0 ? l : l.substring(0, sp);
  for (auto m : kMotionCmds) if (w == m) return true;
  return false;
}
static void cancelMotion() {
  for (auto it = q.begin(); it != q.end();) it = isMotion(*it) ? q.erase(it) : it + 1;
}
static bool motion(const char *cmd, const char *why, bool force = false) {
  uint32_t now = millis();
  if (!force && last_motion == cmd && now - last_motion_ms < MOTION_DEDUP_MS) return false;
  if (configuring && zero_after_config && strcmp(cmd, "stop") != 0) {
    zero_after_config = false;  // the door moved: it is no longer at the closed stall
    OUT.println("# config: the door moves during setup: won't zero it as closed");
  }
  cancelMotion();
  send(cmd, true);
  last_motion = cmd;
  last_motion_ms = now;
  OUT.printf("# action: %s (%s)\n", cmd, why);
  return true;
}

static void raiseFault(const String &why) {
  cancelPendingClose("fault");
  if (!fault) OUT.printf("# FAULT: %s -> latching OPEN; `fault clear` to resume\n", why.c_str());
  fault = true;
  fault_why = why;
  fault_open = true;
  fault_try_ms = millis();
  fault_tries = 0;
  requestOpen("fault", true);  // never deduped: an open sent just before may have failed (state 27)
}

static void cancelPendingClose(const char *why) {
  if (!close_pending) return;
  close_pending = false;
  OUT.printf("# pending close cancelled (%s)\n", why);
}

static void requestOpen(const char *why, bool force) {
  cancelPendingClose(why);
  if (calibrating) {
    // The STM doesn't abort auto_zero: a command mid-run is taken as reaching the end stop and
    // leaves a wrong zero (2026-11-22 12:40:59: travel 11137, door 3" short). Open when it ends.
    if (!open_after_cal) OUT.printf("# open (%s) during the calibration: will open when it ends (~90 s max)\n", why);
    open_after_cal = true;
    return;
  }
  // Presence during a reversal is what blocked the close, not a request to stay open: once the
  // doorway is clear the close continues. Only an explicit open (HA, console) or a fault keeps it.
  if (obstr_sent && !rev_keep && phase != stm::PH_CLOSED && strcmp(why, "presence") != 0) {
    rev_keep = true;  // the STM would reclose on clear_obstruction: keep it set
    OUT.printf("# open (%s) after a reversal: staying open\n", why);
  }
  if (phase == stm::PH_OPEN || phase == stm::PH_OPENING || phase == stm::PH_REVERSING) return;
  // Closing with the obstruction flag already sent: that reverses it. An open on top would reach
  // the STM during the reversal's opening and make it run to its open end stop (2026-11-22 11:54:17).
  if (phase == stm::PH_CLOSING && obstr_sent) return;
  // Position unknown, or the STM's open just failed (27): `disengage` drives fully open whatever
  // the STM thinks (never lock out).
  bool plain = pos_know == POS_KNOWN && phase != stm::PH_FAILED;
  motion(plain ? "open" : "disengage", why, force);
}

static bool requestClose(const char *why, String &no) {
  if (fault) { no = "fault latched: " + fault_why; return false; }
  if (pos_know != POS_KNOWN) { no = String("position ") + (pos_know == POS_INFERRED ? "inferred, not zeroed" : "unknown"); return false; }
  if (!ge_ok || !det.ready) { no = "Grid-EYE not reading: can't see the doorway"; return false; }
  if (s_obst && occupiedAny()) { no = "doorway not clear"; return false; }
  if (configuring) { no = "STM still being configured"; return false; }
  if (phase != stm::PH_OPEN) { no = String("door is ") + stm::phaseName(phase); return false; }
  rev_keep = false;
  if (obstr_sent) {  // still set from a reversal (the doorway is clear now)
    obstr_sent = false;
    obstr_ms = last_clear_ms = millis();
    if (cur_U == 0x07) {  // waiting after the reversal: the clear alone makes the STM reclose
      send("clear_obstruction", true);
      OUT.printf("# close (%s) after a reversal: clear_obstruction, the STM recloses\n", why);
      return true;
    }
    // Stay-open: the clear must reach the STM before the close (a close with the flag still set
    // reverses and runs to the open end stop, 2026-11-22 12:25:00). motion() puts the close at the
    // front of the queue, so queue it first and put the clear in front of it.
    bool ok = motion("close", why);
    send("clear_obstruction", true);
    return ok;
  }
  return motion("close", why);
}

// ---------- Grid-EYE ----------
static bool readFrame() {
  uint8_t raw[128];
  for (int chunk = 0; chunk < 4; chunk++) {  // 32-byte reads: within the Wire buffer
    Wire.beginTransmission(GE_ADDR);
    Wire.write(0x80 + chunk * 32);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)GE_ADDR, 32) != 32) return false;
    for (int i = 0; i < 32; i++) raw[chunk * 32 + i] = Wire.read();
  }
  for (int i = 0; i < 64; i++) frame[i] = ge::pixelC(raw[2 * i], raw[2 * i + 1]);
  return true;
}

// ---------- close recorder ----------
// The last REC_N frames (15 s) stay in RAM, one byte per pixel ((C + 16) x 4: -16..+47.75 C in
// the sensor's 0.25 C steps; ~10 KB). Every close ships the 10 s before it and every frame until
// 3 s after the door stops to syslog as `frame e=<event> t=<ms from close start> ph=<phase>
// p=<position> o=<occupied live,v2> <base64 of 64 bytes>`, paced to leave room for event lines.
static constexpr int REC_N = 150, REC_PRE = 100;
static uint8_t rec[REC_N][64];
static uint32_t rec_ms[REC_N];
static char rec_ph[REC_N];
static int16_t rec_p[REC_N];
static uint8_t rec_occ[REC_N];
static uint32_t rec_total = 0;          // frames recorded since boot

static char phaseChar(stm::DoorPhase ph) {
  switch (ph) {
    case stm::PH_CLOSING: return 'c';
    case stm::PH_OPENING: return 'o';
    case stm::PH_REVERSING: return 'r';
    case stm::PH_CLOSED: return 'C';
    case stm::PH_OPEN: return 'O';
    default: return '?';
  }
}

static void recordFrame() {
  int i = rec_total % REC_N;
  for (int k = 0; k < 64; k++) {
    long v = lroundf((frame[k] + 16.0f) * 4.0f);
    rec[i][k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
  }
  rec_ms[i] = millis();
  rec_ph[i] = phaseChar(phase);
  rec_p[i] = (int16_t)P.st.p;
  rec_occ[i] = (ge_last.occupied ? 1 : 0) | (g2.occupied ? 2 : 0);
  rec_total++;
}

static void captureStart() {
  if (cap_on) return;  // a close during a capture extends it
  cap_on = true;
  cap_evt++;
  cap_t0 = millis();
  cap_until = 0;
  cap_sent = cap_lost = 0;
  cap_next = rec_total > (uint32_t)REC_PRE ? rec_total - REC_PRE : 0;
  OUT.printf("# close capture e=%u started (frames from %u)\n", (unsigned)cap_evt, (unsigned)cap_next);
}

static void captureShip() {
  if (!cap_on) return;
  static const char *B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (rec_total - cap_next > (uint32_t)REC_N) {  // fell behind: oldest frames overwritten
    cap_lost += rec_total - REC_N - cap_next;
    cap_next = rec_total - REC_N;
  }
  for (int n = 0; n < 6 && cap_next < rec_total && net::logSpace() > 16; n++, cap_next++) {
    int i = cap_next % REC_N;
    char b[200];
    int len = snprintf(b, sizeof b, "frame e=%u t=%ld ph=%c p=%d o=%u ", (unsigned)cap_evt,
                       (long)(rec_ms[i] - cap_t0), rec_ph[i], rec_p[i], rec_occ[i]);
    const uint8_t *d = rec[i];
    for (int k = 0; k < 64; k += 3) {
      uint32_t v = (uint32_t)d[k] << 16 | (k + 1 < 64 ? (uint32_t)d[k + 1] << 8 : 0) | (k + 2 < 64 ? d[k + 2] : 0);
      b[len++] = B64[(v >> 18) & 63];
      b[len++] = B64[(v >> 12) & 63];
      b[len++] = k + 1 < 64 ? B64[(v >> 6) & 63] : '=';
      b[len++] = k + 2 < 64 ? B64[v & 63] : '=';
    }
    b[len] = 0;
    if (!net::logOnly(b)) break;
    cap_sent++;
  }
  // Finished when the door has been still for 3 s and everything recorded up to then is out.
  if (cap_until && millis() > cap_until && cap_next >= rec_total) {
    OUT.printf("# close capture e=%u done: %u frames shipped, %u lost\n", (unsigned)cap_evt, (unsigned)cap_sent,
               (unsigned)cap_lost);
    cap_on = false;
  }
}

// Both detectors learn their background again from the next 2 s of frames (they read clear meanwhile).
static void relearnBackground(const char *why) {
  det.reset();
  det2.reset();
  drift.reset();
  bg_relearns++;
  OUT.printf("# grid-eye: both detectors relearning background (2 s) (%s)\n", why);
}

// The doorway reads occupied but the whole field has warmed (drift, not a visit): relearn, while
// the door is closed or its position unknown and nothing moves. Never while open: there a relearn
// could let an auto-close through, and a stuck "occupied" only keeps the door open.
static void driftCheck() {
  bool fire = drift.update(frame, occupiedAny(), millis());
  bool still = (phase == stm::PH_CLOSED || phase == stm::PH_UNKNOWN || phase == stm::PH_UNCONFIGURED ||
                phase == stm::PH_FAILED) && !calibrating && !configuring;
  if (!fire || !still) return;
  char why[120];
  snprintf(why, sizeof why, "scene-wide drift: 3/4 of the pixels +%.2f C or more over the last clear scene for %lu s",
           drift.q, (unsigned long)(ge::DriftWatch::kHoldMs / 1000));
  relearnBackground(why);
}

static void shadow() {
  bool wasP = g2.present, wasO = g2.occupied;
  g2 = det2.update(frame, phase == stm::PH_CLOSED, phase != stm::PH_UNKNOWN);
  if (det2.refSeq() != ref_seq_saved) {
    ref_seq_saved = det2.refSeq();
    memcpy(rtc_ref.ref, det2.refData(), sizeof rtc_ref.ref);
    rtc_ref.sum = rtcRefSum(rtc_ref);
    rtc_ref.magic = RTC_REF_MAGIC;
  }
  uint32_t now = millis();
  auto cell = [](int i) { return i < 0 ? String("-") : "r" + String(i / 8) + "c" + String(i % 8); };
  if (g2.occupied != wasO) {
    if (g2.occupied) {
      g2_occ_since = now;
      OUT.printf("# v2 occupied ON (peak %.2f at %s, score %.2f) | live occupied=%d\n", g2.peak,
                 cell(g2.peak_i).c_str(), g2.occ_score, ge_last.occupied);
    } else {
      OUT.printf("# v2 occupied off after %.1f s | live occupied=%d\n", (now - g2_occ_since) / 1000.0f, ge_last.occupied);
    }
  }
  if (g2.present != wasP) {
    if (g2.present) {
      g2_pres_since = now;
      OUT.printf("# v2 presence ON (score %.2f, peak at %s) | live presence=%d\n", g2.score, cell(g2.peak_i).c_str(),
                 ge_last.present);
    } else {
      OUT.printf("# v2 presence off after %.1f s | live presence=%d\n", (now - g2_pres_since) / 1000.0f, ge_last.present);
    }
  }
  // The comparisons that matter: v2 sees the doorway occupied while the live detector lets the
  // door close.
  static bool closing_flagged = false;
  if (phase == stm::PH_CLOSING && g2.occupied && !obstr_sent) {
    if (!closing_flagged) {
      g2_would_reverse++;
      OUT.printf("# v2 occupied while closing (peak %.2f at %s), live occupied=%d%s\n", g2.peak,
                 cell(g2.peak_i).c_str(), ge_last.occupied, ge_last.occupied ? "" : ": caught by v2 only");
      closing_flagged = true;
    }
  } else if (phase != stm::PH_CLOSING) {
    closing_flagged = false;
  }
}

static void gridEye() {
  if (millis() - last_ge_ms < 100) return;  // the AMG88xx updates at 10 Hz
  last_ge_ms = millis();
  if (!readFrame()) {
    if (++ge_fail == 10 && ge_ok) {
      ge_ok = false;
      OUT.println("# Grid-EYE: not responding (no auto-open, no closing)");
    }
    return;
  }
  // A sensor stuck on one frame reads "fine" forever; its noise makes 20 identical frames impossible.
  static float prevf[64];
  static int same = 0;
  if (!memcmp(prevf, frame, sizeof prevf)) {
    if (++same == 20 && ge_ok) { ge_ok = false; OUT.println("# Grid-EYE: frozen (20 identical frames): not reading"); }
    if (same >= 20) return;
  } else {
    same = 0;
  }
  memcpy(prevf, frame, sizeof prevf);
  if (!ge_ok) { ge_ok = true; OUT.println("# Grid-EYE: reading"); }
  ge_fail = 0;
  bool was = ge_last.present, wasOcc = ge_last.occupied;
  ge_last = det.update(frame);
  recordFrame();
  if (ge_last.present) { last_present_seen = millis(); present_seen = true; }
  shadow();
  driftCheck();
  if (ge_last.occupied != wasOcc) OUT.printf("# occupied %s (score %.2f)\n", ge_last.occupied ? "ON" : "off", ge_last.smoothed);
  if (ge_last.present) last_presence_ms = millis();
  if (ge_last.present != was)
    OUT.printf("# presence %s (score %.1f, hot=%d, max +%.1f C)\n", ge_last.present ? "ON" : "off", ge_last.smoothed,
               ge_last.hot, ge_last.max_delta);
  if (ge_raw) {  // every frame, for offline analysis: "# F <ms> t0 .. t63" (C x4, integers)
    char b[400];
    int n = snprintf(b, sizeof b, "# F %lu", (unsigned long)millis());
    for (int i = 0; i < 64; i++) n += snprintf(b + n, sizeof b - n, " %d", (int)lroundf(frame[i] * 4));
    OUT.println(b);
  }
  if (ge_stream) OUT.printf("# ge hot=%d max=+%.1f present=%d\n", ge_last.hot, ge_last.max_delta, ge_last.present);
}

// The STM's safe-to-close flag. Set only while the door is CLOSING and the doorway is occupied
// (or the sensor stopped reading): the STM then reverses. A set during an open makes the STM
// drive fully open instead of to the open position, so never then. Once set it is held until
// the doorway has been clear for ~3 s (the detector's occ_off_frames); clearing it lets the STM
// reclose by itself from state 7, which is the wanted behaviour after a reversal.
static void obstruction() {
  if (!tx_on || millis() - obstr_ms < 300) return;
  bool danger = s_obst && (occupiedAny() || !ge_ok);
  if (!obstr_sent && danger && phase == stm::PH_CLOSING) {
    cancelMotion();  // no queued close may follow
    send("set_obstruction", true);
    obstr_sent = true;
    obstr_ms = obstr_set_ms = millis();
    OUT.printf("# obstruction: doorway occupied while closing (live=%d v2=%d) -> set_obstruction\n", ge_last.occupied,
               g2.occupied);
  } else if (obstr_sent && danger && !rev_keep && phase == stm::PH_OPEN && millis() - obstr_set_ms > PENDING_MAX_MS) {
    rev_keep = true;  // like a pending close: give up after 5 min
    OUT.println("# doorway not clear 5 min after the reversal: staying open");
  } else if (obstr_sent && !danger) {
    // In the stay-open, disengaged and closed states a clear doesn't move the door; elsewhere
    // (7, still reversing) the STM recloses on it.
    bool recloses = !(cur_U == 0x0F || cur_U == 0x1D || cur_U == 0x0E || cur_U == 0x1E);
    if (recloses && (fault || api_hold || rev_keep)) {
      static uint32_t said = 0;
      if (millis() - said > 60000) {
        said = millis();
        OUT.printf("# doorway clear, staying open after the reversal (%s): obstruction kept set\n",
                   fault ? "fault" : api_hold ? "HA hold" : "open requested or timed out");
      }
      return;
    }
    send("clear_obstruction", true);
    obstr_sent = rev_keep = false;
    obstr_ms = last_clear_ms = millis();
  }
}

static void autoLogic() {
  if (occupiedAny() || !ge_ok) last_occupied_ms = millis();
  if (occupiedAny()) { last_occ_seen = millis(); occ_seen = true; }
  if (!s_auto || configuring || calibrating) return;
  // Presence opens from any state that isn't open or opening, including after an STM reboot
  // (position unknown: requestOpen uses disengage).
  bool not_open = phase == stm::PH_CLOSED || phase == stm::PH_CLOSING || phase == stm::PH_UNKNOWN ||
                  phase == stm::PH_UNCONFIGURED || phase == stm::PH_FAILED;
  if (ge_last.present && have_status && not_open) requestOpen("presence");
  if (s_auto_close && phase == stm::PH_OPEN && !obstr_sent && !api_hold &&
      millis() - last_occupied_ms >= s_hold_s * 1000 &&
      millis() - open_since >= s_hold_s * 1000) {
    String no;
    if (!requestClose("doorway clear for hold time", no) && no.length()) {
      static uint32_t said = 0;
      if (millis() - said > 60000) { said = millis(); OUT.printf("# auto-close held: %s\n", no.c_str()); }
    }
  }
}

// ---------- settings API ----------
// Validates every key first, then applies all of them (or none). Unknown keys and out-of-range
// values are rejected, never clamped silently. Every change is logged (and so goes to syslog).
// Full travel: as the STM measured it, else the expected travel hint, else unknown (0).
static int32_t travel() { return P.st.K > 0 ? P.st.K : s_expected_travel; }
// For setting ranges when the travel isn't known: a generous bound (the STM's own range limits apply).
static float travelOr() { return travel() > 0 ? travel() : 40000.0f; }
static float openPercent() { return travel() > 0 ? roundf(-s_open_counts * 1000.0f / travel()) / 10.0f : 0; }
static float openInches() { return roundf(-s_open_counts * 10.0f / COUNTS_PER_INCH) / 10.0f; }

static bool applySettings(const char *body, String &err) {
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc.is<JsonObject>()) { err = "body must be a JSON object"; return false; }
  bool auto_open = s_auto, auto_close = s_auto_close, obst = s_obst;
  uint32_t hold = s_hold_s;
  int32_t open_counts = s_open_counts, etravel = s_expected_travel;
  float pres = det.p.on_score, occ = det.p.occ_on, noise = det.p.noise_c;
  auto num = [&](JsonVariant v, const char *k, float lo, float hi, float &out) {
    if (!v.is<float>() && !v.is<int>()) { err = String(k) + " must be a number"; return false; }
    float f = v.as<float>();
    if (!(f >= lo && f <= hi)) { err = String(k) + " out of range " + String(lo, 2) + ".." + String(hi, 2); return false; }
    out = f;
    return true;
  };
  auto flag = [&](JsonVariant v, const char *k, bool &out) {
    if (!v.is<bool>()) { err = String(k) + " must be true or false"; return false; }
    out = v.as<bool>();
    return true;
  };
  for (JsonPair kv : doc.as<JsonObject>()) {
    const char *k = kv.key().c_str();
    JsonVariant v = kv.value();
    float f;
    bool ok;
    if (!strcmp(k, "auto_open")) ok = flag(v, k, auto_open);
    else if (!strcmp(k, "auto_close")) ok = flag(v, k, auto_close);
    else if (!strcmp(k, "obstruction_detection")) ok = flag(v, k, obst);
    else if (!strcmp(k, "hold_s")) { ok = num(v, k, 3, 600, f); hold = (uint32_t)lroundf(f); }
    else if (!strcmp(k, "open_percent")) {
      if (travel() <= 0) { err = "open_percent: travel not known yet"; ok = false; }
      else { ok = num(v, k, 15, 95, f); open_counts = -lroundf(f * travel() / 100.0f); }
    }
    else if (!strcmp(k, "open_inches")) {
      ok = num(v, k, 0.15f * travelOr() / COUNTS_PER_INCH, 0.95f * travelOr() / COUNTS_PER_INCH, f);
      open_counts = -lroundf(f * COUNTS_PER_INCH);
    }
    else if (!strcmp(k, "open_counts")) { ok = num(v, k, -0.95f * travelOr(), -0.15f * travelOr(), f); open_counts = lroundf(f); }
    else if (!strcmp(k, "expected_travel")) {
      ok = num(v, k, 0, 40000, f);
      etravel = lroundf(f);
      if (ok && etravel != 0 && etravel < 2000) { err = "expected_travel: 0 (no check) or 2000..40000"; ok = false; }
    }
    else if (!strcmp(k, "expected_travel_inches")) {
      ok = num(v, k, 0, 40000 / COUNTS_PER_INCH, f);
      etravel = lroundf(f * COUNTS_PER_INCH);
      if (ok && etravel != 0 && etravel < 2000) {
        err = "expected_travel_inches: 0 (no check) or " + String(2000 / COUNTS_PER_INCH, 1) + ".." +
              String(40000 / COUNTS_PER_INCH, 1);
        ok = false;
      }
    }
    else if (!strcmp(k, "presence_threshold")) ok = num(v, k, 0.3f, 10, pres);
    else if (!strcmp(k, "occupied_threshold")) ok = num(v, k, 0.2f, 5, occ);
    else if (!strcmp(k, "noise_c")) ok = num(v, k, 0.2f, 2, noise);
    else { err = String("unknown setting ") + k; return false; }
    if (!ok) return false;
  }
  if (occ > pres) { err = "occupied_threshold must not exceed presence_threshold (occupied is the safety level)"; return false; }

  auto logChange = [](const char *k, const String &from, const String &to) {
    if (from != to) OUT.printf("# setting %s: %s -> %s\n", k, from.c_str(), to.c_str());
  };
  logChange("auto_open", s_auto ? "on" : "off", auto_open ? "on" : "off");
  logChange("auto_close", s_auto_close ? "on" : "off", auto_close ? "on" : "off");
  logChange("obstruction_detection", s_obst ? "on" : "off", obst ? "on" : "off");
  logChange("hold_s", String(s_hold_s), String(hold));
  logChange("open_counts", String(s_open_counts), String(open_counts));
  logChange("expected_travel", String(s_expected_travel), String(etravel));
  logChange("presence_threshold", String(det.p.on_score, 2), String(pres, 2));
  logChange("occupied_threshold", String(det.p.occ_on, 2), String(occ, 2));
  logChange("noise_c", String(det.p.noise_c, 2), String(noise, 2));
  if (s_obst && !obst) OUT.println("# WARNING: obstruction detection OFF: only the STM's own effort detection stops a close");
  bool open_changed = open_counts != s_open_counts;
  s_auto = auto_open; s_auto_close = auto_close; s_obst = obst; s_hold_s = hold; s_open_counts = open_counts;
  s_expected_travel = etravel;
  det.p.on_score = pres; det.p.off_score = pres * 0.6f;
  det.p.occ_on = occ; det.p.occ_off = occ * 0.58f;
  det.p.noise_c = noise;
  saveSettings(det);
  if (open_changed && tx_on) send(String("set_open_to ") + s_open_counts);  // takes effect on the next open
  return true;
}

// ---------- Home Assistant API ----------
static const char *haState() {
  if (!have_status || millis() - last_status_ms > 15000) return "unknown";  // can't prove it
  switch (phase) {
    case stm::PH_CLOSED: return "closed";
    case stm::PH_OPEN: return "open";
    case stm::PH_OPENING: case stm::PH_REVERSING: return "opening";
    case stm::PH_CLOSING: return "closing";
    default: return "unknown";
  }
}

static void apiPoll() {
  for (api::Request *r; (r = api::next());) {
    String no;
    switch (r->cmd) {
      case api::CMD_OPEN:
        api_hold = true;
        OUT.println("# api: open (hold open until HA closes)");
        requestOpen("home assistant", true);
        api::finish(r, true, nullptr);
        break;
      case api::CMD_CLOSE: {
        OUT.println("# api: close");
        if (phase == stm::PH_CLOSED || phase == stm::PH_CLOSING) { api_hold = false; api::finish(r, true, "already closed or closing"); break; }
        bool ok = requestClose("home assistant", no);
        if (ok || no.length() == 0) { api_hold = false; api::finish(r, true, nullptr); }  // empty = deduped repeat
        else if (no == "doorway not clear") {
          close_pending = true;
          close_pending_since = millis();
          OUT.println("# api: close pending: will close once the doorway has been clear for 3 s");
          api::finish(r, true, "waiting for the doorway to clear");
        } else { OUT.printf("# api: close refused: %s\n", no.c_str()); api::finish(r, false, no.c_str()); }
        break;
      }
      case api::CMD_SETTINGS: {
        String err;
        bool ok = applySettings(r->body, err);
        if (!ok) OUT.printf("# api: settings refused: %s\n", err.c_str());
        api::finish(r, ok, ok ? nullptr : err.c_str());
        break;
      }
      case api::CMD_AUTO:
        if (s_auto != r->arg) {
          s_auto = r->arg;
          saveSettings(det);
        }
        OUT.printf("# api: auto %s\n", s_auto ? "ON" : "off");
        api::finish(r, true, nullptr);
        break;
      case api::CMD_CLEAR_FAULT:
        clearFault("home assistant");
        api::finish(r, true, nullptr);
        break;
      case api::CMD_CALIBRATE:
        if (calRefused(no)) {
          OUT.printf("# api: calibrate refused: %s\n", no.c_str());
          api::finish(r, false, no.c_str());
        } else {
          api_hold = false;
          startCal("home assistant");
          api::finish(r, true, nullptr);
        }
        break;
      case api::CMD_RELEARN:
        if (stm::moving(phase) || calibrating) {
          OUT.println("# api: relearn refused: door is moving");
          api::finish(r, false, "door is moving");
        } else {
          relearnBackground("home assistant: doorway must be clear");
          api::finish(r, true, nullptr);
        }
        break;
      case api::CMD_STOP:
        OUT.println("# api: stop");
        last_motion = "";
        motion("stop", "home assistant");
        api::finish(r, true, nullptr);
        break;
    }
  }
  static uint32_t last_pub = 0;
  if (millis() - last_pub < 100 && !api::anyFinished()) return;
  last_pub = millis();
  api::Snapshot sn;
  strlcpy(sn.state, haState(), sizeof sn.state);
  strlcpy(sn.detail, stm::phaseName(phase), sizeof sn.detail);
  strlcpy(sn.position, pos_know == POS_KNOWN ? "known" : pos_know == POS_INFERRED ? "inferred" : "unknown",
          sizeof sn.position);
  if (fault) strlcpy(sn.fault, fault_why.c_str(), sizeof sn.fault);
  sn.presence = ge_last.present;
  sn.occupied = occupiedAny();
  sn.auto_mode = s_auto;
  sn.auto_close = s_auto_close;
  sn.obst = s_obst;
  sn.hold_s = s_hold_s;
  sn.open_counts = s_open_counts;
  sn.open_percent = openPercent();
  sn.open_inches = openInches();
  sn.expected_travel = s_expected_travel;
  sn.travel = P.st.K;
  sn.travel_inches = P.st.K > 0 ? roundf(P.st.K * 100.0f / COUNTS_PER_INCH) / 100.0f : -1;
  sn.expected_travel_inches = roundf(s_expected_travel * 100.0f / COUNTS_PER_INCH) / 100.0f;
  sn.last_move_effort = last_move_effort;
  sn.effort_current = last_effort_cur;
  sn.effort_limit = last_effort_lim;
  sn.profile_stored = prof_ok;
  sn.motor_current = cur_now;
  sn.last_move_peak = last_move_peak;
  sn.presence_th = det.p.on_score;
  sn.occupied_th = det.p.occ_on;
  sn.noise_c = det.p.noise_c;
  sn.hold = api_hold;
  sn.close_pending = close_pending;
  sn.presence_age_ms = present_seen ? (int32_t)(millis() - last_present_seen) : -1;
  sn.occupied_age_ms = occ_seen ? (int32_t)(millis() - last_occ_seen) : -1;
  sn.grideye_ok = ge_ok;
  sn.p = P.st.p;
  sn.status_age_ms = have_status ? millis() - last_status_ms : 0xFFFFFFFF;
  sn.uptime_s = millis() / 1000;
  api::publish(sn);
  api::release();
}

static void rtcSave() {
  static uint32_t last_bg = 0;
  uint32_t now = millis();
  bool valid = rtc_st.magic == RTC_ST_MAGIC;
  bool chg = !valid || rtc_st.api_hold != api_hold || rtc_st.fault != fault || rtc_st.fault_open != fault_open ||
             rtc_st.obstr != obstr_sent || rtc_st.rev_keep != rev_keep || strcmp(rtc_st.fault_why, fault_why.c_str());
  bool bg = ge_ok && det.ready && det2.ready && !occupiedAny() && !ge_last.present && now - last_bg > 5000;
  if (!chg && !bg) return;
  if (bg) {
    memcpy(rtc_st.bg1, det.bg, sizeof rtc_st.bg1);
    memcpy(rtc_st.bg2, det2.bg, sizeof rtc_st.bg2);
    memcpy(rtc_st.var2, det2.var, sizeof rtc_st.var2);
    rtc_st.bg_ok = 1;
    last_bg = now;
  } else if (!valid) {
    rtc_st.bg_ok = 0;
  }
  rtc_st.api_hold = api_hold;
  rtc_st.fault = fault;
  rtc_st.fault_open = fault_open;
  rtc_st.obstr = obstr_sent;
  rtc_st.rev_keep = rev_keep;
  strlcpy(rtc_st.fault_why, fault_why.c_str(), sizeof rtc_st.fault_why);
  rtc_st.sum = rtcStSum();
  rtc_st.magic = RTC_ST_MAGIC;
}

static void restoreRtcState() {
  if (rtc_st.magic != RTC_ST_MAGIC || rtc_st.sum != rtcStSum()) {
    rtc_st.magic = 0;
    OUT.println("# rtc: no saved door state (power-on)");
    return;
  }
  api_hold = rtc_st.api_hold;
  fault = rtc_st.fault;
  fault_open = rtc_st.fault_open;
  obstr_sent = rtc_st.obstr;
  rev_keep = rtc_st.rev_keep;
  rtc_st.fault_why[sizeof rtc_st.fault_why - 1] = 0;
  fault_why = rtc_st.fault_why;
  if (fault) fault_try_ms = millis();
  if (rtc_st.bg_ok) {
    det.loadBackground(rtc_st.bg1);
    det2.loadBackground(rtc_st.bg2, rtc_st.var2);
  }
  OUT.printf("# rtc: restored hold=%d fault=%s obstruction=%d stay_open=%d backgrounds=%s\n", api_hold,
             fault ? fault_why.c_str() : "none", obstr_sent, rev_keep, rtc_st.bg_ok ? "yes" : "no");
}

static void startCal(const char *why) {
  send("clear_obstruction");
  send("reset_zero");
  motion("auto_zero", why, true);
  obstr_sent = rev_keep = false;
  calibrating = true;
  cal_bad = false;
  cal_start = millis();
  zero_inferred = false;
  OUT.printf("# calibration (%s): auto_zero started (retracts fully, then closes; ~90 s)\n", why);
}

// A requested calibration (console or HA): only from a still door with a clear doorway.
static bool calRefused(String &no) {
  if (fault) no = "a fault is latched: clear it first";
  else if (!ge_ok) no = "Grid-EYE not reading";
  else if (ge_last.present || occupiedAny()) no = "doorway not clear";
  else if (calibrating) no = "already calibrating";
  else if (configuring) no = "still configuring";
  else if (stm::moving(phase)) no = "door is moving";
  else if (!have_status) no = "no STM status yet";
  else return false;
  return true;
}

static void calEnded() {
  if (!open_after_cal) return;
  open_after_cal = false;
  requestOpen("open requested during the calibration", true);
}

// auto_zero's first closing pass (0x14) on this kind of door never ends by itself: it reaches the
// closed end and keeps pushing at reduced force (at install the STM browned out there; on
// 2026-11-22 12:56-13:04 it pushed until reset). Not moving for 5 s means it has stalled: reset the
// STM; onBoot's closed-stall rule zeroes it as closed if that was the closed end.
static void calStall(uint32_t now) {
  static int last_p = 0, p0 = 0;
  static bool in14 = false;
  static uint32_t still_since = 0;
  const stm::Status &s = P.st;
  if (!calibrating || cur_U != 0x14) { still_since = 0; in14 = false; return; }
  if (!in14) { in14 = true; p0 = s.p; }
  if (s.p != last_p) { last_p = s.p; still_since = now; return; }
  if (!still_since) still_since = now;
  // Still for 5 s after the pass has moved: stalled. onBoot decides whether that's the closed end
  // (against the Expected travel); if not, the position stays unknown and the calibration is retried.
  if (abs(s.p - p0) > 1000 && now - still_since > 5000) {
    still_since = 0;
    OUT.printf("# calibration: closing pass stalled (p=%d for 5 s): STM reset ends it\n", s.p);
    stmReset("calibration at the closed stall");
  }
}

static void recoverPosition(uint32_t now) {
  if (pos_know == POS_KNOWN) { recal_tries = 0; recal_next = 0; return; }
  if (!tx_on || !have_status || configuring || calibrating || fault || stm::moving(phase)) return;
  if (pos_know != POS_UNKNOWN) return;  // inferred: the zero is on its way after setup
  if (!recal_next) {
    recal_next = now + RECAL_CLEAR_MS;
    OUT.println("# position unknown: automatic calibration once the doorway has been clear 10 s");
  }
  bool clear = ge_ok && det.ready && det2.ready && !occupiedAny() && !ge_last.present &&
               now - last_occupied_ms >= RECAL_CLEAR_MS;
  if (now < recal_next || !clear) return;
  if (recal_tries >= RECAL_MAX) {
    raiseFault("position unknown: automatic calibration failed " + String(recal_tries) + " times");
    return;
  }
  recal_tries++;
  recal_next = now + RECAL_RETRY_MS;
  startCal(recal_tries == 1 ? "position unknown after a start" : "position unknown, retry");
}

static void supervise() {
  uint32_t now = millis();
  recoverPosition(now);
  calStall(now);
  profPoll(now);
  if (close_pending) {
    if (fault) cancelPendingClose("fault");
    else if (phase != stm::PH_OPEN) cancelPendingClose(stm::phaseName(phase));
    else if (now - close_pending_since > PENDING_MAX_MS) cancelPendingClose("doorway never cleared in 5 min");
    else if (!occupiedAny() && ge_ok && now - last_occupied_ms >= PENDING_CLEAR_MS) {
      String no;
      close_pending = false;
      api_hold = false;
      if (requestClose("pending close: doorway clear", no) || no.length() == 0) {
        OUT.println("# pending close: doorway clear, closing");
      } else if (no == "doorway not clear") {
        close_pending = true;  // occupied again in the same instant: keep waiting
      } else {
        OUT.printf("# pending close dropped: %s\n", no.c_str());
      }
    }
  }
  // STM liveness: on ESP boot give it 25 s to speak (it may be at its 10 s rate), then reset it.
  // Later: 30 s of silence -> reset, at most once a minute.
  if (!heard && now - boot_ms > 25000 && (stm_resets == 0 || now - last_rst_ms > 60000))
    stmReset("no STM output since ESP boot");  // repeats each minute: one pulse may not start it
  if (heard && now - last_rx_ms > SILENT_RESET_MS && now - last_rst_ms > 60000) stmReset("STM silent 30 s");
  if (tx_on && now - last_tx_ms > KEEPALIVE_MS && q.empty()) send("c");
  // While moving, sample the motor current ~4x/s for the move's peak (status lines are 1.5 s apart).
  if (tx_on && stm::moving(phase) && q.empty() && now - cur_poll_ms >= 250) {
    send("c");
    cur_poll_ms = now;
  }
  if (configuring && q.empty() && now - last_tx_ms > 1000) {
    configuring = false;
    if (zero_after_config) {
      send(String("set_dooroffset ") + zero_k);
      send("zero -72 14");
      zero_inferred = true;
      OUT.println("# config: done; zero as closed (inferred)");
    } else {
      OUT.println("# config: done; position unknown until `cal`");
    }
  }
  // A fault holds the door open: retry the open (3 times, 15 s apart) if it isn't open.
  if (!fault) fault_open = false;
  if (fault && fault_open && tx_on && !configuring && !stm::moving(phase) && phase != stm::PH_OPEN && q.empty() &&
      now - fault_try_ms > 15000 && fault_tries < 3) {
    fault_try_ms = now;
    fault_tries++;
    OUT.printf("# fault: door is %s, not open: open retry %d/3\n", stm::phaseName(phase), fault_tries);
    requestOpen("fault retry", true);
  }
  rtcSave();
  static uint32_t hb = 0;
  if (now - hb >= 30000) {
    hb = now;
    OUT.printf("# hb door=%s pos=%s p=%d presence=%d occupied=%d score=%.2f auto=%d fault=%d | v2 presence=%d "
               "occupied=%d score=%.2f peak=%.2f would_block=%u would_reverse=%u | current %d\n",
               stm::phaseName(phase), pos_know == POS_KNOWN ? "known" : pos_know == POS_INFERRED ? "inferred" : "unknown",
               P.st.p, ge_last.present, ge_last.occupied, ge_last.smoothed, s_auto, fault, g2.present, g2.occupied,
               g2.score, g2.peak, (unsigned)g2_would_block, (unsigned)g2_would_reverse, cur_now);
  }
  if (stm::moving(phase)) {
    uint32_t lim = phase == stm::PH_CALIBRATING ? CAL_TIMEOUT_MS : MOVE_TIMEOUT_MS;
    if (now - phase_since > lim) {
      phase_since = now;
      String why = String("move timeout in ") + stm::phaseName(phase);
      if (phase == stm::PH_CLOSING) {
        raiseFault(why);  // -> open
      } else if (phase == stm::PH_CALIBRATING) {
        // The STM ignores commands mid-auto_zero: reset it, then open (position unknown: disengage).
        calibrating = false;
        open_after_cal = false;
        stmReset("calibration timeout");
        raiseFault(why);
      } else {
        motion("stop", "move timeout");
        if (!fault) OUT.printf("# FAULT: %s -> stopped; `fault clear` to resume\n", why.c_str());
        fault = true;
        fault_why = why;
      }
    }
  }
  // No `stop` during a calibration: the STM takes it as the end stop (wrong zero). Its passes run
  // at reduced force and stall on an obstacle; a pass cut short shows up as a wrong travel and is
  // rejected (expected_travel).
  static bool cal_occ_said = false;
  if (calibrating && occupiedAny() && !cal_occ_said) {
    cal_occ_said = true;
    OUT.println("# calibration: doorway occupied; the STM's reduced-force pass continues, travel will be checked");
  }
  if (!calibrating) cal_occ_said = false;
}

// ---------- console ----------
static void clearFault(const char *who) {
  fault = fault_open = false;
  fault_why = "";
  n_rev = 0;
  OUT.printf("# fault cleared (%s)\n", who);
}

static void printStatus() {
  const stm::Status &s = P.st;
  OUT.printf("# door=%s position=%s (%s) p=%d U=%X Z=%d H=%X K=%d fault=%s\n", stm::phaseName(phase),
             pos_know == POS_KNOWN ? "known" : pos_know == POS_INFERRED ? "inferred" : "UNKNOWN", pos_why, s.p, s.U,
             s.Z, s.H, s.K, fault ? fault_why.c_str() : "none");
  OUT.printf("# occupied=%d (original %d, v2 %d; blocks closing; on %.2f off %.2f) obstruction_sent=%d stay_open=%d\n",
             occupiedAny(), ge_last.occupied, g2.occupied, det.p.occ_on, det.p.occ_off, obstr_sent, rev_keep);
  OUT.printf("# auto=%s hold=%us presence=%d (score %.1f hot=%d max=+%.1f) grideye=%s on=%.1f off=%.1f noise=%.1fC "
             "obstruction_sent=%d\n",
             s_auto ? "ON" : "off", (unsigned)s_hold_s, ge_last.present, ge_last.score, ge_last.hot, ge_last.max_delta,
             ge_ok ? "ok" : "NOT READING", det.p.on_score, det.p.off_score, det.p.noise_c, obstr_sent);
  OUT.printf("# http: hold=%d requests=%u denied=%u | ", api_hold, (unsigned)api::n_requests, (unsigned)api::n_denied);
  OUT.printf("# stm: heard=%d boots=%u resets=%u queue=%u configuring=%d calibrating=%d | up %lus, reset reason %d\n",
             heard, (unsigned)stm_boots, (unsigned)stm_resets, (unsigned)q.size(), configuring, calibrating,
             (unsigned long)(millis() / 1000), (int)esp_reset_reason());
}

static void help() {
  OUT.println("# wayzn-brain. cmds: status | open | close | stop | cal | auto on|off | hold <s>");
  OUT.println("#   ge (frame + deltas) | ge on|off (stream) | ge raw on|off (all frames) | on <score> | off <score> | noise <C> | mask <hex64> | gebg (relearn)");
  OUT.println("#   fault clear | cfg (resend setup) | profile [read] | sreset | log on|off | raw <stm line> (expert)");
  printStatus();
}

static void printFrame() {
  OUT.println("# grid-eye: temperature C / rise over background (row 0 = register 0x80)");
  for (int r = 0; r < 8; r++) {
    char b[200];
    int n = snprintf(b, sizeof b, "#");
    for (int c = 0; c < 8; c++) n += snprintf(b + n, sizeof b - n, " %5.1f", frame[r * 8 + c]);
    n += snprintf(b + n, sizeof b - n, "  |");
    for (int c = 0; c < 8; c++) n += snprintf(b + n, sizeof b - n, " %+5.1f", frame[r * 8 + c] - det.bg[r * 8 + c]);
    OUT.println(b);
  }
}

static void command(String c) {
  c.trim();
  if (!c.length()) return;
  String no;
  if (c.startsWith("b ")) return;  // tools/uart-sniffer/capture.py sends this on connect
  if (c == "?" || c == "help") help();
  else if (c == "status") printStatus();
  else if (c == "open") requestOpen("command", true);
  else if (c == "close") { if (!requestClose("command", no)) OUT.printf("# close refused: %s\n", no.c_str()); }
  else if (c == "stop") { last_motion = ""; motion("stop", "command"); calibrating = false; }
  else if (c == "cal") {
    String why;
    if (calRefused(why)) OUT.printf("# cal refused: %s\n", why.c_str());
    else startCal("command");
  }
  else if (c == "auto on" || c == "auto off") { s_auto = c == "auto on"; saveSettings(det); printStatus(); }
  else if (c.startsWith("hold ")) { s_hold_s = max(3L, c.substring(5).toInt()); saveSettings(det); printStatus(); }
  else if (c.startsWith("on ")) { det.p.on_score = c.substring(3).toFloat(); saveSettings(det); printStatus(); }
  else if (c.startsWith("off ")) { det.p.off_score = c.substring(4).toFloat(); saveSettings(det); printStatus(); }
  else if (c.startsWith("noise ")) { det.p.noise_c = c.substring(6).toFloat(); saveSettings(det); printStatus(); }
  else if (c.startsWith("mask ")) { det.p.mask = strtoull(c.substring(5).c_str(), nullptr, 16); saveSettings(det); printStatus(); }
  else if (c == "ge") printFrame();
  else if (c == "ge on" || c == "ge off") ge_stream = c == "ge on";
  else if (c == "ge raw on" || c == "ge raw off") ge_raw = c == "ge raw on";
  else if (c == "gebg") relearnBackground("console: doorway must be clear");
  else if (c == "fault clear") clearFault("console");
  else if (c == "cfg") pushConfig(false);
  else if (c == "profile") {
    OUT.printf("# profile: %s", prof_ok ? "stored" : "none stored");
    if (prof_ok) {
      OUT.print("\n# open: ");
      for (int i = 0; i < PROF_N; i++) OUT.printf("%u ", prof_tab[0][i]);
      OUT.print("\n# close: ");
      for (int i = 0; i < PROF_N; i++) OUT.printf("%u ", prof_tab[1][i]);
    }
    OUT.println();
  }
  else if (c == "profile read") { prof_want = true; OUT.println("# profile: read-back requested (door idle, doorway clear)"); }
  else if (c == "sreset") stmReset("command");
  else if (c == "log on" || c == "log off") s_log = c == "log on";
  else if (c.startsWith("raw ")) send(c.substring(4));
  else OUT.printf("# unknown: %s (? for help)\n", c.c_str());
}

static void feed(String &buf, char ch) {
  if (ch == '\n' || ch == '\r') { command(buf); buf = ""; }
  else if (buf.length() < 200) buf += ch;
}

// ---------- main ----------
static void restoreV2Ref() {
  if (rtc_ref.magic == RTC_REF_MAGIC && rtc_ref.sum == rtcRefSum(rtc_ref)) {
    det2.loadRef(rtc_ref.ref);
    OUT.println("# v2: closed-door snapshot restored from RTC memory");
  } else {
    OUT.println("# v2: no closed-door snapshot in RTC memory; a close before the door next opens uses slow-warming follow");
  }
}

void setup() {
  // NRST first: open-drain, released, input on so we can read the real level.
  gpio_set_level((gpio_num_t)PIN_STM_RST, 1);
  gpio_set_direction((gpio_num_t)PIN_STM_RST, GPIO_MODE_INPUT_OUTPUT_OD);
  Serial.begin(921600);
  loadSettings(det);
  api::init();
  net::on_up = api::begin;
  net::on_poll = api::poll;
  net::begin(nullptr);
  restoreV2Ref();
  restoreRtcState();
  Stm.setRxBufferSize(4096);
  Stm.begin(115200, SERIAL_8N1, PIN_STM_RX, -1);  // TX attached once the STM is heard
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  Wire.setTimeOut(20);
  boot_ms = millis();
  delay(200);
  help();
}

void loop() {
  static String ubuf, nbuf;
  while (Serial.available()) feed(ubuf, Serial.read());
  for (int ch; (ch = net::read()) >= 0;) feed(nbuf, (char)ch);
  while (Stm.available()) handleStmByte((char)Stm.read());
  gridEye();
  obstruction();
  autoLogic();
  apiPoll();
  captureShip();
  supervise();
  pumpQueue();
  net::armed = stm::moving(phase) || calibrating;  // no OTA while the door moves
  delay(1);
}

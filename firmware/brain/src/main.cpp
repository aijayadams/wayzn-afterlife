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
#include "door_config.h"
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
static constexpr int32_t TRAVEL_COUNTS = 13588;  // full travel (set_dooroffset), for open_percent
static int32_t s_open_counts = -6136;            // set_open_to (8 in = 45.2 %)
static constexpr float COUNTS_PER_INCH = 767.0f; // the imp's set_open_to mapping (8 in = -6136, 12 in = -9204)
static bool s_log = false;       // print every STM status line

static void loadSettings(ge::Detector &d) {
  prefs.begin("brain", false);
  s_auto = prefs.getBool("auto", false);
  s_hold_s = prefs.getUInt("hold", 15);
  s_auto_close = prefs.getBool("aclose", true);
  s_obst = prefs.getBool("obst", true);
  s_open_counts = prefs.getInt("opento", -6136);
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
  prefs.putFloat("occ", d.p.occ_on);
  prefs.putFloat("on2", d.p.on_score);
  prefs.putFloat("off2", d.p.off_score);
  prefs.putFloat("noise", d.p.noise_c);
  prefs.putULong64("mask", d.p.mask);
}

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
static void pumpQueue() {
  if (!tx_on || q.empty() || millis() - last_tx_ms < SEND_GAP_MS) return;
  String l = q.front();
  q.pop_front();
  Stm.print(l);
  Stm.print('\n');
  last_tx_ms = millis();
  if (!l.startsWith("set_profile") && l != "c") OUT.printf("# stm< %s\n", l.c_str());
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
static uint32_t last_cfg_ms = 0;
static bool calibrating = false;
static uint32_t cal_start = 0;
static bool fault = false;
static String fault_why;
static uint32_t reversals[8];
static int n_rev = 0;
static uint32_t open_since = 0;
static uint32_t last_status_ms = 0;
// Opened by Home Assistant: hold open (no auto-close) until HA closes it. Overnight HA lets
// the dogs back in from outside, where the Grid-EYE may not see them coming.
static bool api_hold = false;

// ---------- Grid-EYE ----------
static ge::Detector det;
static ge::Result ge_last;
// v2 detector in SHADOW MODE (docs/DETECTION-RESEARCH.md stage 1): fed the same frames, it only
// logs its decisions and where it would have differed. Nothing here acts on it.
static ge2::Detector det2;
static ge2::Result g2;
static uint32_t g2_occ_since = 0, g2_pres_since = 0, g2_would_block = 0, g2_would_reverse = 0;
static float frame[64];
static bool ge_ok = false, ge_stream = false, ge_raw = false;
static uint32_t ge_fail = 0, last_presence_ms = 0, last_occupied_ms = 0, last_ge_ms = 0;
static bool obstr_sent = false;   // what we last told the STM
static String last_motion;        // last motion command sent (see motion())
static uint32_t last_motion_ms = 0;
static uint32_t obstr_ms = 0;

static void raiseFault(const String &why);
static void requestOpen(const char *why);

static void pushConfig(bool zero_closed) {
  for (int i = 0; i < kDoorConfigN; i++) send(kDoorConfig[i]);
  send(String("set_open_to ") + s_open_counts);  // the saved setting overrides door_config.h
  configuring = true;
  zero_after_config = zero_closed;
  last_cfg_ms = millis();
  OUT.printf("# config: sending %d setup lines%s\n", kDoorConfigN, zero_closed ? ", then zero as closed" : "");
}

static void onBoot() {
  stm_boots++;
  // A reboot at the closed stall (auto_zero pass 14 reaching closed can brown the STM out)
  // means the door is physically closed: say so, as inferred.
  const stm::Status &s = P.st;
  bool closed_stall = have_status && ((s.U == 0x14 && s.p > 12000) ||
                                      (stm::phase(s.U) == stm::PH_CLOSING && s.p > -600));
  pos_know = closed_stall ? POS_INFERRED : POS_UNKNOWN;
  pos_why = closed_stall ? "STM rebooted at the closed stall" : "STM rebooted";
  OUT.printf("# STM BOOT #%u (last U=%X p=%d): position %s\n", (unsigned)stm_boots, s.U, s.p,
             closed_stall ? "inferred CLOSED" : "UNKNOWN");
  if (calibrating) {
    calibrating = false;
    OUT.printf("# calibration: %s\n", closed_stall ? "reached closed (STM rebooted at the stall)"
                                                   : "FAILED: STM rebooted mid-run");
    if (!closed_stall) raiseFault("calibration failed");
  }
  q.clear();
  obstr_sent = false;  // the STM's flag restarts with it
  last_motion = "";
  pushConfig(closed_stall);
}

static void onPhase(stm::DoorPhase np, int pos) {
  if (np == phase) return;
  OUT.printf("# door: %s -> %s (p=%d)\n", stm::phaseName(phase), stm::phaseName(np), pos);
  if (np == stm::PH_REVERSING) {
    uint32_t now = millis();
    reversals[n_rev++ % 8] = now;
    int recent = 0;
    for (int i = 0; i < 8 && i < n_rev; i++) if (now - reversals[i] < REVERSAL_WINDOW_MS) recent++;
    if (recent >= REVERSALS_FAULT) raiseFault("oscillation: " + String(recent) + " reversals in 2 min");
  }
  if (np == stm::PH_OPEN) open_since = millis();
  if (np == stm::PH_CLOSED && api_hold) { api_hold = false; OUT.println("# api hold released (door closed)"); }
  if (np == stm::PH_FAILED) raiseFault("STM open failed (state 27)");
  phase = np;
  phase_since = millis();
}

static void onStatus() {
  const stm::Status &s = P.st;
  have_status = true;
  last_status_ms = millis();
  if (s_log) OUT.printf("# st U=%X Z=%d dest=%d H=%X p=%d K=%d\n", s.U, s.Z, s.dest, s.H, s.p, s.K);
  if (s.Z & 1) {
    if (pos_know != POS_KNOWN) {
      pos_know = POS_KNOWN;
      pos_why = zero_inferred ? "zeroed as closed after a reboot at the closed stall" : "zeroed";
    }
  } else if (pos_know == POS_KNOWN) {
    pos_know = POS_UNKNOWN;
    pos_why = "STM lost its zero";
  }
  // STM running but not set up (e.g. it restarted while we weren't listening): set it up.
  if (!(s.Z & 2) && !configuring && !calibrating && millis() - last_cfg_ms > 30000) pushConfig(false);
  if (calibrating && s.Z == 3 && !stm::moving(stm::phase(s.U)) && stm::phase(s.U) != stm::PH_UNKNOWN) {
    calibrating = false;
    OUT.printf("# calibration: done, K=%d p=%d\n", s.K, s.p);
  }
  onPhase(stm::phase(s.U), s.p);
}

static void handleStmByte(char c) {
  last_rx_ms = millis();
  if (!heard) { heard = true; attachTx(); }
  switch (P.feed(c)) {
    case stm::STATUS: onStatus(); break;
    case stm::BOOT: onBoot(); break;
    case stm::MOVE:
      OUT.printf("# move: pos=%d -> state %X, current %X (limit %X)\n", P.mv.pos, P.mv.state, P.mv.current, P.mv.limit);
      // The move summary prints the moment the state changes; status lines lag up to 1.5 s.
      // Pseudo-states in the first summary line (1, 2, 3) map to unknown and are skipped.
      if (stm::phase(P.mv.state) != stm::PH_UNKNOWN) onPhase(stm::phase(P.mv.state), P.mv.pos);
      break;
    case stm::WIPED:
      OUT.println("# STM: calibration wiped by the button (~)");
      pos_know = POS_UNKNOWN;
      pos_why = "calibration wiped (button)";
      pushConfig(false);
      break;
    case stm::REPLY:
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
static bool motion(const char *cmd, const char *why) {
  uint32_t now = millis();
  if (last_motion == cmd && now - last_motion_ms < MOTION_DEDUP_MS) return false;
  cancelMotion();
  send(cmd, true);
  last_motion = cmd;
  last_motion_ms = now;
  OUT.printf("# action: %s (%s)\n", cmd, why);
  return true;
}

static void raiseFault(const String &why) {
  if (!fault) OUT.printf("# FAULT: %s -> latching OPEN; `fault clear` to resume\n", why.c_str());
  fault = true;
  fault_why = why;
  requestOpen("fault");
}

static void requestOpen(const char *why) {
  if (phase == stm::PH_OPEN || phase == stm::PH_OPENING || phase == stm::PH_REVERSING) return;
  // Position unknown: `disengage` drives fully open whatever the STM thinks (never lock out).
  motion(pos_know == POS_KNOWN ? "open" : "disengage", why);
}

static bool requestClose(const char *why, String &no) {
  if (fault) { no = "fault latched: " + fault_why; return false; }
  if (pos_know != POS_KNOWN) { no = String("position ") + (pos_know == POS_INFERRED ? "inferred, not zeroed" : "unknown"); return false; }
  if (!ge_ok || !det.ready) { no = "Grid-EYE not reading: can't see the doorway"; return false; }
  if (s_obst && ge_last.occupied) { no = "doorway not clear"; return false; }
  if (configuring) { no = "STM still being configured"; return false; }
  if (phase != stm::PH_OPEN) { no = String("door is ") + stm::phaseName(phase); return false; }
  if (g2.occupied) {
    g2_would_block++;
    OUT.printf("# v2 WOULD BLOCK this close (%s): v2 occupied for %.1f s, peak %.2f\n", why,
               (millis() - g2_occ_since) / 1000.0f, g2.peak);
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

static void shadow() {
  bool wasP = g2.present, wasO = g2.occupied;
  g2 = det2.update(frame, phase == stm::PH_CLOSED);
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
      OUT.printf("# v2 WOULD REVERSE: door closing while v2 occupied (peak %.2f at %s), live occupied=%d\n", g2.peak,
                 cell(g2.peak_i).c_str(), ge_last.occupied);
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
  if (!ge_ok) { ge_ok = true; OUT.println("# Grid-EYE: reading"); }
  ge_fail = 0;
  bool was = ge_last.present, wasOcc = ge_last.occupied;
  ge_last = det.update(frame);
  shadow();
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
  bool danger = s_obst && (ge_last.occupied || !ge_ok);
  if (!obstr_sent && danger && phase == stm::PH_CLOSING) {
    cancelMotion();  // no queued close may follow
    send("set_obstruction", true);
    obstr_sent = true;
    obstr_ms = millis();
    OUT.println("# obstruction: doorway occupied while closing -> set_obstruction");
  } else if (obstr_sent && !danger) {
    send("clear_obstruction", true);
    obstr_sent = false;
    obstr_ms = millis();
  }
}

static void autoLogic() {
  if (ge_last.occupied || !ge_ok) last_occupied_ms = millis();
  if (!s_auto || configuring || calibrating) return;
  if (ge_last.present && (phase == stm::PH_CLOSED || phase == stm::PH_CLOSING)) requestOpen("presence");
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
static float openPercent() { return roundf(-s_open_counts * 1000.0f / TRAVEL_COUNTS) / 10.0f; }
static float openInches() { return roundf(-s_open_counts * 10.0f / COUNTS_PER_INCH) / 10.0f; }

static bool applySettings(const char *body, String &err) {
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc.is<JsonObject>()) { err = "body must be a JSON object"; return false; }
  bool auto_open = s_auto, auto_close = s_auto_close, obst = s_obst;
  uint32_t hold = s_hold_s;
  int32_t open_counts = s_open_counts;
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
    else if (!strcmp(k, "open_percent")) { ok = num(v, k, 15, 95, f); open_counts = -lroundf(f * TRAVEL_COUNTS / 100.0f); }
    else if (!strcmp(k, "open_inches")) {
      ok = num(v, k, 0.15f * TRAVEL_COUNTS / COUNTS_PER_INCH, 0.95f * TRAVEL_COUNTS / COUNTS_PER_INCH, f);
      open_counts = -lroundf(f * COUNTS_PER_INCH);
    }
    else if (!strcmp(k, "open_counts")) { ok = num(v, k, -0.95f * TRAVEL_COUNTS, -0.15f * TRAVEL_COUNTS, f); open_counts = lroundf(f); }
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
  logChange("presence_threshold", String(det.p.on_score, 2), String(pres, 2));
  logChange("occupied_threshold", String(det.p.occ_on, 2), String(occ, 2));
  logChange("noise_c", String(det.p.noise_c, 2), String(noise, 2));
  if (s_obst && !obst) OUT.println("# WARNING: obstruction detection OFF: only the STM's current limit stops a close");
  bool open_changed = open_counts != s_open_counts;
  s_auto = auto_open; s_auto_close = auto_close; s_obst = obst; s_hold_s = hold; s_open_counts = open_counts;
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
        requestOpen("home assistant");
        api::finish(r, true, nullptr);
        break;
      case api::CMD_CLOSE: {
        OUT.println("# api: close");
        if (phase == stm::PH_CLOSED || phase == stm::PH_CLOSING) { api_hold = false; api::finish(r, true, "already closed or closing"); break; }
        bool ok = requestClose("home assistant", no);
        if (ok || no.length() == 0) { api_hold = false; api::finish(r, true, nullptr); }  // empty = deduped repeat
        else { OUT.printf("# api: close refused: %s\n", no.c_str()); api::finish(r, false, no.c_str()); }
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
      case api::CMD_STOP:
        OUT.println("# api: stop");
        last_motion = "";
        motion("stop", "home assistant");
        api::finish(r, true, nullptr);
        break;
    }
  }
  static uint32_t last_pub = 0;
  if (millis() - last_pub < 100) return;
  last_pub = millis();
  api::Snapshot sn;
  strlcpy(sn.state, haState(), sizeof sn.state);
  strlcpy(sn.detail, stm::phaseName(phase), sizeof sn.detail);
  strlcpy(sn.position, pos_know == POS_KNOWN ? "known" : pos_know == POS_INFERRED ? "inferred" : "unknown",
          sizeof sn.position);
  if (fault) strlcpy(sn.fault, fault_why.c_str(), sizeof sn.fault);
  sn.presence = ge_last.present;
  sn.occupied = ge_last.occupied;
  sn.auto_mode = s_auto;
  sn.auto_close = s_auto_close;
  sn.obst = s_obst;
  sn.hold_s = s_hold_s;
  sn.open_counts = s_open_counts;
  sn.open_percent = openPercent();
  sn.open_inches = openInches();
  sn.presence_th = det.p.on_score;
  sn.occupied_th = det.p.occ_on;
  sn.noise_c = det.p.noise_c;
  sn.hold = api_hold;
  sn.grideye_ok = ge_ok;
  sn.p = P.st.p;
  sn.status_age_ms = have_status ? millis() - last_status_ms : 0xFFFFFFFF;
  sn.uptime_s = millis() / 1000;
  api::publish(sn);
}

static void supervise() {
  uint32_t now = millis();
  // STM liveness: on ESP boot give it 25 s to speak (it may be at its 10 s rate), then reset it.
  // Later: 30 s of silence -> reset, at most once a minute.
  if (!heard && now - boot_ms > 25000 && stm_resets == 0) stmReset("no STM output since ESP boot");
  if (heard && now - last_rx_ms > SILENT_RESET_MS && now - last_rst_ms > 60000) stmReset("STM silent 30 s");
  if (tx_on && now - last_tx_ms > KEEPALIVE_MS && q.empty()) send("c");
  if (configuring && q.empty() && now - last_tx_ms > 1000) {
    configuring = false;
    if (zero_after_config) {
      send("zero -72 14");
      zero_inferred = true;
      OUT.println("# config: done; zero as closed (inferred)");
    } else {
      OUT.println("# config: done; position unknown until `cal`");
    }
  }
  static uint32_t hb = 0;
  if (now - hb >= 30000) {
    hb = now;
    OUT.printf("# hb door=%s pos=%s p=%d presence=%d occupied=%d score=%.2f auto=%d fault=%d | v2 presence=%d "
               "occupied=%d score=%.2f peak=%.2f would_block=%u would_reverse=%u\n",
               stm::phaseName(phase), pos_know == POS_KNOWN ? "known" : pos_know == POS_INFERRED ? "inferred" : "unknown",
               P.st.p, ge_last.present, ge_last.occupied, ge_last.smoothed, s_auto, fault, g2.present, g2.occupied,
               g2.score, g2.peak, (unsigned)g2_would_block, (unsigned)g2_would_reverse);
  }
  if (stm::moving(phase)) {
    uint32_t lim = phase == stm::PH_CALIBRATING ? CAL_TIMEOUT_MS : MOVE_TIMEOUT_MS;
    if (now - phase_since > lim) {
      phase_since = now;
      String why = String("move timeout in ") + stm::phaseName(phase);
      if (phase == stm::PH_CLOSING) {
        raiseFault(why);  // -> open
      } else {
        motion("stop", "move timeout");
        if (!fault) OUT.printf("# FAULT: %s -> stopped; `fault clear` to resume\n", why.c_str());
        fault = true;
        fault_why = why;
      }
    }
  }
  if (calibrating && ge_last.occupied) {
    motion("stop", "calibration aborted");
    calibrating = false;
    OUT.println("# calibration: ABORTED, something in the doorway");
  }
}

// ---------- console ----------
static void printStatus() {
  const stm::Status &s = P.st;
  OUT.printf("# door=%s position=%s (%s) p=%d U=%X Z=%d H=%X K=%d fault=%s\n", stm::phaseName(phase),
             pos_know == POS_KNOWN ? "known" : pos_know == POS_INFERRED ? "inferred" : "UNKNOWN", pos_why, s.p, s.U,
             s.Z, s.H, s.K, fault ? fault_why.c_str() : "none");
  OUT.printf("# occupied=%d (blocks closing; on %.2f off %.2f)\n", ge_last.occupied, det.p.occ_on, det.p.occ_off);
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
  OUT.println("#   fault clear | cfg (resend setup) | sreset | log on|off | raw <stm line> (expert)");
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
  else if (c == "open") requestOpen("command");
  else if (c == "close") { if (!requestClose("command", no)) OUT.printf("# close refused: %s\n", no.c_str()); }
  else if (c == "stop") { last_motion = ""; motion("stop", "command"); calibrating = false; }
  else if (c == "cal") {
    if (ge_last.present || !ge_ok) OUT.println("# cal refused: doorway not clear / Grid-EYE not reading");
    else if (configuring) OUT.println("# cal refused: still configuring");
    else {
      send("clear_obstruction"); send("reset_zero"); motion("auto_zero", "calibration");
      calibrating = true; cal_start = millis(); zero_inferred = false;
      OUT.println("# calibration: auto_zero started (retracts fully, then closes; ~90 s)");
    }
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
  else if (c == "gebg") { det.reset(); OUT.println("# grid-eye: relearning background (2 s)"); }
  else if (c == "fault clear") { fault = false; fault_why = ""; n_rev = 0; OUT.println("# fault cleared"); }
  else if (c == "cfg") pushConfig(false);
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
  supervise();
  pumpQueue();
  net::armed = stm::moving(phase) || calibrating;  // no OTA while the door moves
  delay(1);
}

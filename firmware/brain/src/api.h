// Local HTTP API for Home Assistant (MVP). Header-only; include once, after net.h.
//
//   GET  /api/status   -> {"state":"open|closed|opening|closing|unknown", ...}
//   POST /api/open     -> {"ok":true}                     (always accepted: never lock a dog out)
//   POST /api/close    -> {"ok":false,"reason":"doorway not clear"}  when it isn't safe
//   POST /api/stop
//   POST /api/calibrate -> auto_zero now (409 with a reason unless still, clear and fault-free)
//   POST /api/relearn  -> both Grid-EYE detectors relearn the background (doorway must be clear)
//   POST /api/auto     body {"on":true|false} -> Grid-EYE auto-open on or off (saved)
//   POST /api/settings body: any subset of the "settings" object in /api/status -> all or nothing
// Every request needs `Authorization: Bearer <API_TOKEN>` (secrets.ini). LAN only, plain HTTP.
// mDNS: _wayzn-brain._tcp on port 80.
//
// Threading: the HTTP server runs in the net task (core 0). It never touches door state: it
// reads a snapshot the door loop publishes, and hands commands to the door loop through a
// queue, and waits for the verdict (the door loop answers within ~1 ms). So every command goes through the same checks
// (and the same single motion() path) as console commands.
#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "freertos/queue.h"

#ifndef API_TOKEN
#error "API_TOKEN not set (secrets.ini api_token)"
#endif

namespace api {

struct Snapshot {
  char state[12] = "unknown";  // what HA sees
  char detail[24] = "";        // internal phase name
  char position[10] = "unknown";
  char fault[64] = "";
  bool presence = false, occupied = false, auto_mode = false, hold = false, grideye_ok = false;
  bool auto_close = true, obst = true, close_pending = false;
  int32_t presence_age_ms = -1, occupied_age_ms = -1;  // since last true; -1 = never
  uint32_t hold_s = 15;
  int open_counts = 0, expected_travel = 0, travel = 0, stall_allowance = 0;
  int motor_current = 0, last_move_peak = -1;  // mA (the STM's H / `c` / move summaries; docs/STM-FIRMWARE.md)
  int last_move_effort = -1, effort_current = 0, effort_limit = 0;  // % of the STM's limit; -1 = none yet
  float travel_inches = -1, expected_travel_inches = 0;
  bool profile_stored = false;
  float open_percent = 0, open_inches = 0, presence_th = 0, occupied_th = 0, noise_c = 0;
  int p = 0;
  uint32_t status_age_ms = 0xFFFFFFFF;
  uint32_t uptime_s = 0;
};

enum Cmd : uint8_t { CMD_OPEN, CMD_CLOSE, CMD_STOP, CMD_AUTO, CMD_SETTINGS, CMD_CLEAR_FAULT, CMD_CALIBRATE, CMD_RELEARN };
struct Request {
  Cmd cmd;
  bool arg;
  char body[400];
  volatile bool done;
  volatile bool ok;
  char reason[128];
};

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static Snapshot snap;
static QueueHandle_t reqq;
static WebServer *web = nullptr;
static uint32_t n_requests = 0, n_denied = 0;

// Door loop side ------------------------------------------------------------
static void publish(const Snapshot &s) {
  portENTER_CRITICAL(&mux);
  snap = s;
  portEXIT_CRITICAL(&mux);
}
// Pops one pending request, if any. The door loop executes it, then calls finish().
static Request *next() {
  Request *r = nullptr;
  return xQueueReceive(reqq, &r, 0) == pdTRUE ? r : nullptr;
}
// Requests are answered only after the next snapshot is published (release()), so a status read
// straight after any reply already shows what the request changed.
static Request *finished[8];
static int n_finished = 0;
static void finish(Request *r, bool ok, const char *reason) {
  r->ok = ok;
  strlcpy(r->reason, reason ? reason : "", sizeof r->reason);
  if (n_finished < 8) finished[n_finished++] = r;
  else r->done = true;
}
static bool anyFinished() { return n_finished > 0; }
static void release() {
  for (int i = 0; i < n_finished; i++) finished[i]->done = true;
  n_finished = 0;
}

// HTTP side (net task) -------------------------------------------------------
static bool authorized() {
  static const String want = String("Bearer ") + API_TOKEN;
  if (web->header("Authorization") == want) return true;
  n_denied++;
  web->send(401, "application/json", "{\"ok\":false,\"reason\":\"unauthorized\"}");
  return false;
}

static String jsonEscape(const char *s) {
  String o;
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') o += '\\';
    if ((uint8_t)*s >= 0x20) o += *s;
  }
  return o;
}

static void handleStatus() {
  if (!authorized()) return;
  Snapshot s;
  portENTER_CRITICAL(&mux);
  s = snap;
  portEXIT_CRITICAL(&mux);
  static char b[1500];  // net task only; kept off its 6 KB stack
  snprintf(b, sizeof b,
           "{\"state\":\"%s\",\"detail\":\"%s\",\"position\":\"%s\",\"p\":%d,\"fault\":%s%s%s,"
           "\"presence\":%s,\"occupied\":%s,\"presence_age_ms\":%ld,\"occupied_age_ms\":%ld,\"auto\":%s,\"hold\":%s,\"close_pending\":%s,\"grideye_ok\":%s,"
           "\"status_age_ms\":%lu,\"uptime_s\":%lu,\"motor_current\":%d,\"last_move_peak_current\":%d,\"travel_counts\":%d,"
           "\"travel_inches\":%.2f,\"last_move_effort\":%d,\"effort_current\":%d,\"effort_limit\":%d,\"profile_stored\":%s,"
           "\"motor_current_a\":%.3f,\"last_move_current_a\":%.3f,\"last_move_current_limit_a\":%.3f,"
           "\"settings\":{\"auto_open\":%s,\"auto_close\":%s,\"obstruction_detection\":%s,\"hold_s\":%lu,"
           "\"open_percent\":%.1f,\"open_inches\":%.1f,\"open_counts\":%d,\"expected_travel\":%d,\"expected_travel_inches\":%.2f,\"stall_allowance\":%d,\"stall_allowance_inches\":%.3f,\"presence_threshold\":%.2f,\"occupied_threshold\":%.2f,"
           "\"noise_c\":%.2f}}",
           s.state, s.detail, s.position, s.p, s.fault[0] ? "\"" : "", s.fault[0] ? jsonEscape(s.fault).c_str() : "null",
           s.fault[0] ? "\"" : "", s.presence ? "true" : "false", s.occupied ? "true" : "false", (long)s.presence_age_ms, (long)s.occupied_age_ms,
           s.auto_mode ? "true" : "false", s.hold ? "true" : "false", s.close_pending ? "true" : "false",
           s.grideye_ok ? "true" : "false",
           (unsigned long)s.status_age_ms, (unsigned long)s.uptime_s, s.motor_current, s.last_move_peak, s.travel,
           s.travel_inches, s.last_move_effort, s.effort_current, s.effort_limit, s.profile_stored ? "true" : "false",
           s.motor_current / 1000.0f, s.last_move_peak < 0 ? -1.0f : s.last_move_peak / 1000.0f,
           s.effort_limit > 0 ? s.effort_limit / 1000.0f : -1.0f,
           s.auto_mode ? "true" : "false", s.auto_close ? "true" : "false", s.obst ? "true" : "false",
           (unsigned long)s.hold_s, s.open_percent, s.open_inches, s.open_counts, s.expected_travel, s.expected_travel_inches, s.stall_allowance, s.stall_allowance / 767.0f, s.presence_th, s.occupied_th, s.noise_c);
  web->send(200, "application/json", b);
}

static void handleCmd(Cmd c, bool arg = false, const String *body = nullptr) {
  if (!authorized()) return;
  n_requests++;
  Request r;
  r.cmd = c;
  r.arg = arg;
  r.body[0] = 0;
  if (body) {
    if (body->length() >= sizeof r.body) {
      web->send(413, "application/json", "{\"ok\":false,\"reason\":\"body too long\"}");
      return;
    }
    strlcpy(r.body, body->c_str(), sizeof r.body);
  }
  r.done = false;
  r.ok = false;
  r.reason[0] = 0;
  Request *rp = &r;
  if (xQueueSend(reqq, &rp, 0) != pdTRUE) {
    web->send(503, "application/json", "{\"ok\":false,\"reason\":\"busy\"}");
    return;
  }
  // The door loop answers within a pass (~1 ms). Wait for it: it holds a pointer to r.
  while (!r.done) vTaskDelay(pdMS_TO_TICKS(2));
  String resp = String("{\"ok\":") + (r.ok ? "true" : "false");
  if (r.reason[0]) resp += String(",\"reason\":\"") + jsonEscape(r.reason) + "\"";
  resp += "}";
  web->send(r.ok ? 200 : (c == CMD_SETTINGS ? 400 : 409), "application/json", resp);
}

// Called from the net task once Wi-Fi is up, and then every pass.
static void begin() {
  web = new WebServer(80);
  const char *hdrs[] = {"Authorization"};
  web->collectHeaders(hdrs, 1);
  web->on("/api/status", HTTP_GET, handleStatus);
  web->on("/api/open", HTTP_POST, [] { handleCmd(CMD_OPEN); });
  web->on("/api/close", HTTP_POST, [] { handleCmd(CMD_CLOSE); });
  web->on("/api/stop", HTTP_POST, [] { handleCmd(CMD_STOP); });
  web->on("/api/clear_fault", HTTP_POST, [] { handleCmd(CMD_CLEAR_FAULT); });
  web->on("/api/calibrate", HTTP_POST, [] { handleCmd(CMD_CALIBRATE); });
  web->on("/api/relearn", HTTP_POST, [] { handleCmd(CMD_RELEARN); });
  web->on("/api/settings", HTTP_POST, [] {
    String b = web->arg("plain");
    handleCmd(CMD_SETTINGS, false, &b);
  });
  web->on("/api/auto", HTTP_POST, [] {
    // Body {"on":true} / {"on":false}. Anything else is rejected rather than guessed.
    String b = web->arg("plain");
    b.replace(" ", "");
    if (b.indexOf("\"on\":true") >= 0) handleCmd(CMD_AUTO, true);
    else if (b.indexOf("\"on\":false") >= 0) handleCmd(CMD_AUTO, false);
    else if (authorized()) web->send(400, "application/json", "{\"ok\":false,\"reason\":\"body must be {\\\"on\\\":true|false}\"}");
  });
  web->onNotFound([] { web->send(404, "application/json", "{\"ok\":false,\"reason\":\"not found\"}"); });
  web->begin();
  MDNS.addService("wayzn-brain", "tcp", 80);
}
static void poll() {
  if (web) web->handleClient();
}
static void init() { reqq = xQueueCreate(4, sizeof(Request *)); }

}  // namespace api

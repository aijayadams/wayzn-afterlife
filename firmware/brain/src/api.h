// Local HTTP API for Home Assistant (MVP). Header-only; include once, after net.h.
//
//   GET  /api/status   -> {"state":"open|closed|opening|closing|unknown", ...}
//   POST /api/open     -> {"ok":true}                     (always accepted: never lock a dog out)
//   POST /api/close    -> {"ok":false,"reason":"doorway not clear"}  when it isn't safe
//   POST /api/stop
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
  bool auto_close = true, obst = true;
  uint32_t hold_s = 15;
  int open_counts = 0;
  float open_percent = 0, open_inches = 0, presence_th = 0, occupied_th = 0, noise_c = 0;
  int p = 0;
  uint32_t status_age_ms = 0xFFFFFFFF;
  uint32_t uptime_s = 0;
};

enum Cmd : uint8_t { CMD_OPEN, CMD_CLOSE, CMD_STOP, CMD_AUTO, CMD_SETTINGS };
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
static void finish(Request *r, bool ok, const char *reason) {
  r->ok = ok;
  strlcpy(r->reason, reason ? reason : "", sizeof r->reason);
  r->done = true;
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
  char b[900];
  snprintf(b, sizeof b,
           "{\"state\":\"%s\",\"detail\":\"%s\",\"position\":\"%s\",\"p\":%d,\"fault\":%s%s%s,"
           "\"presence\":%s,\"occupied\":%s,\"auto\":%s,\"hold\":%s,\"grideye_ok\":%s,"
           "\"status_age_ms\":%lu,\"uptime_s\":%lu,"
           "\"settings\":{\"auto_open\":%s,\"auto_close\":%s,\"obstruction_detection\":%s,\"hold_s\":%lu,"
           "\"open_percent\":%.1f,\"open_inches\":%.1f,\"open_counts\":%d,\"presence_threshold\":%.2f,\"occupied_threshold\":%.2f,"
           "\"noise_c\":%.2f}}",
           s.state, s.detail, s.position, s.p, s.fault[0] ? "\"" : "", s.fault[0] ? jsonEscape(s.fault).c_str() : "null",
           s.fault[0] ? "\"" : "", s.presence ? "true" : "false", s.occupied ? "true" : "false",
           s.auto_mode ? "true" : "false", s.hold ? "true" : "false", s.grideye_ok ? "true" : "false",
           (unsigned long)s.status_age_ms, (unsigned long)s.uptime_s,
           s.auto_mode ? "true" : "false", s.auto_close ? "true" : "false", s.obst ? "true" : "false",
           (unsigned long)s.hold_s, s.open_percent, s.open_inches, s.open_counts, s.presence_th, s.occupied_th, s.noise_c);
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

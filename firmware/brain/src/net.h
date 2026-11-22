// Wi-Fi console + OTA for wayzn-brain (forked from tools/uart-sniffer/src/net.h). Header-only; include once.
//
// - Joins the Wi-Fi from secrets.ini (WIFI_SSID / WIFI_PSK), hostname wayzn-esp.
// - Telnet console on port 23 (nc wayzn-esp.local 23): the same output as USB serial,
//   and it takes the same commands. One client at a time; a new one replaces the old.
// - ArduinoOTA (password OTA_PASS), handled only while disarmed, so an update can't
//   start while motion commands are allowed.
//
// All network work runs in its own task on core 0 (with the Wi-Fi stack). The sniffer
// loop on core 1 only touches two stream buffers and never waits on the network: a
// slow loop would break the idle-gap framing of UART bytes. Output that doesn't fit in
// the buffer is dropped from telnet (USB serial still gets it) and counted.
// No I2C sniffing in this build: that poller disables interrupts on core 1, which
// would stall the flash writes an OTA update needs.
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include "freertos/stream_buffer.h"
#include <WiFiUdp.h>
#include <lwip/sockets.h>
#include <sys/time.h>
#include <time.h>

// Syslog (RFC 5424 over UDP) of every event line ("# ..."), except raw Grid-EYE frames and the
// Grid-EYE debug stream. SYSLOG_HOST is an IP (no DNS dependency); empty disables it.
#ifndef SYSLOG_HOST
#define SYSLOG_HOST ""
#endif
#ifndef SYSLOG_PORT
#define SYSLOG_PORT 5514
#endif

#ifndef WIFI_SSID
#error "WIFI_SSID not set: copy secrets.ini.example to secrets.ini"
#endif
#ifndef HOSTNAME
#define HOSTNAME "wayzn-esp"
#endif

namespace net {

static StreamBufferHandle_t txbuf, rxbuf;  // to telnet / from telnet
static SemaphoreHandle_t txlock;           // stream buffers allow one writer at a time
static volatile bool up = false, connected = false, armed = false;
static uint32_t dropped = 0;  // approximate (two writers)
static void (*on_disconnect)() = nullptr;
struct LogLine { char s[232]; };
static QueueHandle_t logq;
static uint32_t log_dropped = 0, log_sent = 0;
static char linebuf[sizeof(LogLine::s)];
static size_t linelen = 0;

// Severity by content: faults err(3), STM reboots/resets warning(4), actions/door changes notice(5), else info(6).
static int severity(const char *l) {
  if (strstr(l, "FAULT")) return 3;
  if (strstr(l, "v2 WOULD")) return 4;  // shadow detector disagreed about a close
  if (strstr(l, "STM BOOT") || strstr(l, "reset pulse") || strstr(l, "refused") || strstr(l, "not responding") ||
      strstr(l, "OTA error"))
    return 4;
  if (!strncmp(l, "action:", 7) || !strncmp(l, "door:", 5) || !strncmp(l, "api:", 4) || strstr(l, "obstruction")) return 5;
  return 6;
}

// Called with the tx lock held: collect output into lines, queue the event lines for syslog.
static void collect(const uint8_t *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char c = (char)b[i];
    if (c == '\r') continue;
    if (c != '\n') {
      if (linelen < sizeof linebuf - 1) linebuf[linelen++] = c;
      continue;
    }
    linebuf[linelen] = 0;
    bool event = linelen > 2 && linebuf[0] == '#' && linebuf[1] == ' ' && strncmp(linebuf, "# F ", 4) != 0 &&
                 strncmp(linebuf, "# ge ", 5) != 0;
    if (event && logq) {
      LogLine ll;
      strlcpy(ll.s, linebuf + 2, sizeof ll.s);
      if (xQueueSend(logq, &ll, 0) != pdTRUE) log_dropped++;
    }
    linelen = 0;
  }
}
static void (*on_up)() = nullptr;    // once Wi-Fi is up (in the net task)
static void (*on_poll)() = nullptr;  // every net-task pass

// Everything printed goes to USB serial and (buffered) to the telnet client.
class Tee : public Print {
 public:
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *b, size_t n) override {
    Serial.write(b, n);
    if (!txlock) return n;
    xSemaphoreTake(txlock, portMAX_DELAY);
    if (connected && txbuf) {
      if (xStreamBufferSpacesAvailable(txbuf) >= n) xStreamBufferSend(txbuf, b, n, 0);
      else dropped++;
    }
    collect(b, n);
    xSemaphoreGive(txlock);
    return n;
  }
};
static Tee out;

static void sendSyslog(WiFiUDP &udp, const LogLine &ll) {
  char ts[40] = "-";
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec > 1700000000) {  // NTP has set the clock
    struct tm t;
    gmtime_r(&tv.tv_sec, &t);
    size_t k = strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%S", &t);
    snprintf(ts + k, sizeof ts - k, ".%03ldZ", (long)(tv.tv_usec / 1000));
  }
  int pri = 16 * 8 + severity(ll.s);  // local0
  char msg[320];
  int m = snprintf(msg, sizeof msg, "<%d>1 %s %s wayzn-brain - - - %s", pri, ts, HOSTNAME, ll.s);
  if (udp.beginPacket(SYSLOG_HOST, SYSLOG_PORT)) {
    udp.write((const uint8_t *)msg, m < (int)sizeof msg ? m : sizeof msg - 1);
    if (udp.endPacket()) log_sent++;
  }
}

static void task(void *) {
  WiFiServer server(23);
  WiFiClient client;
  WiFiUDP syslog_udp;
  static uint8_t chunk[1024];
  static size_t pend_len = 0, pend_off = 0;
  static uint32_t last_progress = 0;
  for (;;) {
    if (!up && WiFi.status() == WL_CONNECTED) {
      up = true;
      WiFi.setSleep(false);
      MDNS.begin(HOSTNAME);
      server.begin();
      server.setNoDelay(true);
      ArduinoOTA.setHostname(HOSTNAME);
      ArduinoOTA.setPassword(OTA_PASS);
      ArduinoOTA.setMdnsEnabled(false);  // MDNS is ours
      ArduinoOTA.onStart([] { out.println("# OTA update starting"); });
      ArduinoOTA.onEnd([] { out.println("# OTA update done, rebooting"); });
      ArduinoOTA.onError([](ota_error_t e) { out.printf("# OTA error %d\n", (int)e); });
      ArduinoOTA.begin();
      MDNS.enableArduino(3232, true);
      out.printf("# wifi %s ip %s  console: nc %s.local 23\n", WIFI_SSID,
                 WiFi.localIP().toString().c_str(), HOSTNAME);
      // Clock for syslog timestamps: the router first, a public pool as fallback.
      configTime(0, 0, WiFi.gatewayIP().toString().c_str(), "pool.ntp.org");
      if (on_up) on_up();
    }
    if (up) {
      if (!armed) ArduinoOTA.handle();
      if (server.hasClient()) {
        WiFiClient n = server.accept();
        if (client) client.stop();
        client = n;
        client.setNoDelay(true);
        xStreamBufferReset(txbuf);
        pend_len = pend_off = 0;
        last_progress = millis();
        connected = true;
        out.println("# telnet client connected (send ? for help)");
      }
      bool has = client && client.connected();
      if (connected && !has) {
        connected = false;
        if (on_disconnect) on_disconnect();
      }
      if (has) {
        // Never block on the console: a client that stops reading (a laptop asleep) must not
        // stall this task, which also serves the HTTP API. Send what the socket takes right
        // now (MSG_DONTWAIT), keep the rest for the next pass, and drop a client that has
        // taken nothing for 30 s. (NetworkClient::write retries for up to ~10 s.)
        if (pend_off >= pend_len) {
          pend_len = xStreamBufferReceive(txbuf, chunk, sizeof chunk, 0);
          pend_off = 0;
        }
        if (pend_len > pend_off) {
          int r = send(client.fd(), chunk + pend_off, pend_len - pend_off, MSG_DONTWAIT);
          if (r > 0) {
            pend_off += r;
            last_progress = millis();
          } else if (millis() - last_progress > 30000) {
            Serial.println("# telnet client not reading for 30 s: disconnected");
            client.stop();
            pend_len = pend_off = 0;
          }
        } else {
          last_progress = millis();
        }
        while (client.available()) {
          uint8_t c = client.read();
          xStreamBufferSend(rxbuf, &c, 1, 0);
        }
      }
    }
    if (up && on_poll) on_poll();
    if (up && SYSLOG_HOST[0]) {
      LogLine ll;
      for (int i = 0; i < 8 && xQueueReceive(logq, &ll, 0) == pdTRUE; i++) sendSyslog(syslog_udp, ll);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void begin(void (*onDisconnect)()) {
  on_disconnect = onDisconnect;
  txbuf = xStreamBufferCreate(16384, 1);
  rxbuf = xStreamBufferCreate(1024, 1);
  txlock = xSemaphoreCreateMutex();
  logq = xQueueCreate(48, sizeof(LogLine));
  WiFi.persistent(false);  // don't write Wi-Fi config to flash on every boot
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PSK);
  xTaskCreatePinnedToCore(task, "net", 6144, nullptr, 2, nullptr, 0);
}

// From the sniffer loop: next byte typed on telnet, or -1.
static int read() {
  uint8_t c;
  return xStreamBufferReceive(rxbuf, &c, 1, 0) == 1 ? c : -1;
}

}  // namespace net

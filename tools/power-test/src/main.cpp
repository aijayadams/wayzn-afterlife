// Supply-stability test for running the ESP32 from the Wayzn board's 3V3 rail.
//
// Monitor over Wi-Fi (no USB when on board power):  nc wayzn-esp.local 23
// First boot: joins nothing -> opens AP "wayzn-esp-setup"; pick your Wi-Fi from a phone.
//
// - Every boot is logged in NVS with its reset reason; BROWNOUT resets are counted.
// - Brownout detector raised to level BOD_LEVEL (7 ~= 2.8 V on the classic ESP32),
//   so a sagging rail trips it before it gets truly marginal.
// - Load: auto-cycles 30 s max Wi-Fi TX (UDP blast) / 30 s idle. Commands: l0 l1 la h
// - Optional rail measurement: 2 equal resistors (10k-100k) 3V3 -> GPIO34 -> GND.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"

#ifndef BOD_LEVEL
#define BOD_LEVEL 7
#endif
#define PIN_VDD 34

static WiFiServer server(23);
static WiFiClient client;
static WiFiUDP udp;
static Preferences prefs;
static uint32_t boots = 0, brownouts = 0;
static String hist;
static int load_mode = 2;  // 0 off, 1 on, 2 auto
static uint8_t pkt[1400];

static const char *rr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "ext-pin";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    default: return "other";
  }
}
static char rcode(esp_reset_reason_t r) {
  return r == ESP_RST_BROWNOUT ? 'B' : r == ESP_RST_POWERON ? 'P' : r == ESP_RST_PANIC ? 'X' :
         r == ESP_RST_SW ? 'S' : r == ESP_RST_EXT ? 'E' : (r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT) ? 'W' : '?';
}

static void out(const char *fmt, ...) {
  char b[256];
  va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  Serial.print(b);
  if (client && client.connected()) client.print(b);
}

static void banner() {
  out("# wayzn power-test  boot #%u  reset=%s  brownouts=%u  BOD level %d\n", boots,
      rr(esp_reset_reason()), brownouts, BOD_LEVEL);
  out("# history (oldest..newest; P power-on, B BROWNOUT, W wdt, X panic, S sw, E ext): %s\n", hist.c_str());
  out("# cols: up_s load pkts/s rssi heap vdd_mV(min/max, needs divider on GPIO34)\n");
  out("# cmds: l0 (load off) | l1 (on) | la (auto 30/30 s) | h (header)\n");
}

void setup() {
  Serial.begin(115200);
  REG_SET_FIELD(RTC_CNTL_BROWN_OUT_REG, RTC_CNTL_DBROWN_OUT_THRES, BOD_LEVEL);
  esp_reset_reason_t r = esp_reset_reason();
  prefs.begin("pwrtest", false);
  boots = prefs.getUInt("boots", 0) + 1;
  brownouts = prefs.getUInt("bo", 0) + (r == ESP_RST_BROWNOUT ? 1 : 0);
  hist = prefs.getString("hist", "") + rcode(r);
  if (hist.length() > 40) hist = hist.substring(hist.length() - 40);
  prefs.putUInt("boots", boots);
  prefs.putUInt("bo", brownouts);
  prefs.putString("hist", hist);
  banner();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("wayzn-esp");
  WiFiManager wm;
  wm.setConfigPortalTimeout(300);
  if (!wm.autoConnect("wayzn-esp-setup")) { out("# no Wi-Fi, rebooting\n"); delay(1000); ESP.restart(); }
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  MDNS.begin("wayzn-esp");
  server.begin();
  server.setNoDelay(true);
  memset(pkt, 0xA5, sizeof pkt);
  analogSetPinAttenuation(PIN_VDD, ADC_11db);
  out("# Wi-Fi %s  ip %s  -> nc wayzn-esp.local 23\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void loop() {
  static uint32_t last = 0, pkts = 0;
  static int vmin = 99999, vmax = 0;
  static String cmd;

  if (server.hasClient()) {
    WiFiClient n = server.accept();
    if (client) client.stop();
    client = n;
    banner();
  }
  while (client && client.available()) {
    char c = client.read();
    if (c == '\n' || c == '\r') {
      cmd.trim();
      if (cmd == "l0") load_mode = 0; else if (cmd == "l1") load_mode = 1; else if (cmd == "la") load_mode = 2;
      else if (cmd.length()) banner();
      cmd = "";
    } else cmd += c;
  }

  bool load = load_mode == 1 || (load_mode == 2 && (millis() / 30000) % 2 == 1);
  if (load && WiFi.isConnected()) {
    IPAddress dst = (client && client.connected()) ? client.remoteIP() : WiFi.gatewayIP();
    for (int i = 0; i < 8; i++) {
      udp.beginPacket(dst, 9);  // discard port; nothing needs to listen
      udp.write(pkt, sizeof pkt);
      if (udp.endPacket()) pkts++;
    }
  }

  int mv = analogRead(PIN_VDD) * 3100 / 4095 * 2;  // approx: this chip has no eFuse ADC calibration
  if (mv < vmin) vmin = mv;
  if (mv > vmax) vmax = mv;

  if (millis() - last >= 1000) {
    last = millis();
    out("%7lu %s %5u %4d %6u %4d/%4d\n", (unsigned long)(millis() / 1000), load ? "LOAD" : "idle", pkts,
        WiFi.RSSI(), ESP.getFreeHeap(), vmin, vmax);
    pkts = 0; vmin = 99999; vmax = 0;
  }
  if (!load) delay(2);
}

// Wi-Fi console + OTA for the uart-sniffer (env esp32dev-net). Header-only; include once.
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
static volatile uint32_t dropped = 0;
static void (*on_disconnect)() = nullptr;

// Everything printed goes to USB serial and (buffered) to the telnet client.
class Tee : public Print {
 public:
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *b, size_t n) override {
    Serial.write(b, n);
    if (connected && txbuf) {
      xSemaphoreTake(txlock, portMAX_DELAY);
      if (xStreamBufferSpacesAvailable(txbuf) >= n) xStreamBufferSend(txbuf, b, n, 0);
      else dropped++;
      xSemaphoreGive(txlock);
    }
    return n;
  }
};
static Tee out;

static void task(void *) {
  WiFiServer server(23);
  WiFiClient client;
  static uint8_t chunk[1024];
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
    }
    if (up) {
      if (!armed) ArduinoOTA.handle();
      if (server.hasClient()) {
        WiFiClient n = server.accept();
        if (client) client.stop();
        client = n;
        client.setNoDelay(true);
        xStreamBufferReset(txbuf);
        connected = true;
        out.println("# telnet client connected (send ? for help)");
      }
      bool has = client && client.connected();
      if (connected && !has) {
        connected = false;
        if (on_disconnect) on_disconnect();
      }
      if (has) {
        size_t n;
        while ((n = xStreamBufferReceive(txbuf, chunk, sizeof chunk, 0)) > 0) client.write(chunk, n);
        while (client.available()) {
          uint8_t c = client.read();
          xStreamBufferSend(rxbuf, &c, 1, 0);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void begin(void (*onDisconnect)()) {
  on_disconnect = onDisconnect;
  txbuf = xStreamBufferCreate(16384, 1);
  rxbuf = xStreamBufferCreate(1024, 1);
  txlock = xSemaphoreCreateMutex();
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

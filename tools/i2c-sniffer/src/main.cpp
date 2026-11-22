// Standalone passive I2C sniffer for the Wayzn Grid-EYE bus (TP1 = SCL, TP2 = SDA).
// The decoder lives in i2c_sniff.h (shared with uart-sniffer env esp32dev-i2c).
//
// Output (USB serial, 921600): `<secs> I <transaction>`, see i2c_sniff.h.
// Commands: s (toggle summary of long reads) | ? (status)
#include <Arduino.h>
#include "i2c_sniff.h"

static void help() {
  Serial.println("# wayzn i2c-sniffer");
  i2cs::status(Serial);
  Serial.println("# cmds: s (toggle summary of long reads) | ? (status)");
}

// Printing and commands run on core 0; core 1 belongs to the poller.
static void main_task(void *) {
  Serial.begin(921600);
  delay(300);
  help();
  i2cs::start();
  String cmd;
  for (;;) {
    i2cs::poll(Serial);
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        cmd.trim();
        if (cmd == "s") { i2cs::summary = !i2cs::summary; help(); }
        else if (cmd.length()) help();
        cmd = "";
      } else {
        cmd += c;
      }
    }
    vTaskDelay(1);
  }
}

void setup() { xTaskCreatePinnedToCore(main_task, "main", 8192, nullptr, 1, nullptr, 0); }
void loop() { vTaskDelete(nullptr); }

// I2C master on the Grid-EYE / accelerometer bus (J2 SCL/DATA = TP1/TP2), for the
// esp32dev-net build (-DWITH_I2C_MASTER). Only useful with the imp held in reset:
// the imp is the bus's normal master. Header-only; include once.
//
// Commands (prefix "i2c "):
//   i2c lv                 idle levels of SCL/SDA, pins as plain inputs (board pull-ups only)
//   i2c f <hz>             bus clock, decimal (default 100000)
//   i2c mon [ms]           sample SCL/SDA: edges, % time low, longest low
//   i2c pu                 levels with the ESP's internal pull-ups on
//   i2c clk                bus recovery: up to 9 SCL pulses until SDA is released, then STOP
//   i2c scan               probe addresses 0x08..0x77
//   i2c rd <a> <r> [n]     read n (default 1, max 128) bytes from register r of device a
//   i2c wr <a> <r> <b...>  write bytes to register r of device a
// Numbers are hex (0x prefix optional).
#pragma once
#include <Arduino.h>
#include <Wire.h>

#ifndef PIN_SCL
#define PIN_SCL 21
#endif
#ifndef PIN_SDA
#define PIN_SDA 22
#endif

namespace i2cm {

static bool started = false;
static uint32_t freq = 100000;

static void levels(Print &out) {
  if (started) { Wire.end(); started = false; }
  pinMode(PIN_SCL, INPUT);
  pinMode(PIN_SDA, INPUT);
  delay(2);
  out.printf("# i2c levels (no ESP pull-ups): SCL(GPIO%d)=%d SDA(GPIO%d)=%d\n", PIN_SCL,
             digitalRead(PIN_SCL), PIN_SDA, digitalRead(PIN_SDA));
}

static bool begin(Print &out) {
  if (started) return true;
  pinMode(PIN_SCL, INPUT);
  pinMode(PIN_SDA, INPUT);
  if (!digitalRead(PIN_SCL) || !digitalRead(PIN_SDA))
    out.printf("# i2c warning: bus not idle (SCL=%d SDA=%d); try i2c clk\n", digitalRead(PIN_SCL),
               digitalRead(PIN_SDA));
  started = Wire.begin(PIN_SDA, PIN_SCL, freq);
  Wire.setTimeOut(50);
  if (!started) out.println("# i2c: Wire.begin failed");
  return started;
}

static void recover(Print &out) {
  if (started) { Wire.end(); started = false; }
  pinMode(PIN_SDA, INPUT);
  pinMode(PIN_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_SCL, HIGH);
  delayMicroseconds(10);
  int pulses = 0;
  while (!digitalRead(PIN_SDA) && pulses < 9) {
    digitalWrite(PIN_SCL, LOW); delayMicroseconds(5);
    digitalWrite(PIN_SCL, HIGH); delayMicroseconds(5);
    pulses++;
  }
  // STOP: SDA low -> high while SCL high
  pinMode(PIN_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_SDA, LOW); delayMicroseconds(5);
  digitalWrite(PIN_SCL, HIGH); delayMicroseconds(5);
  digitalWrite(PIN_SDA, HIGH); delayMicroseconds(5);
  pinMode(PIN_SCL, INPUT);
  pinMode(PIN_SDA, INPUT);
  out.printf("# i2c clk: %d pulses; now SCL=%d SDA=%d\n", pulses, digitalRead(PIN_SCL), digitalRead(PIN_SDA));
}

static long hex(const String &s) { return strtol(s.c_str(), nullptr, 16); }

// Returns true if `cmd` was an i2c command.
static bool command(const String &cmd, Print &out) {
  if (!cmd.startsWith("i2c")) return false;
  String a[40];
  int n = 0;
  for (int i = 0; i < (int)cmd.length() && n < 40;) {
    while (i < (int)cmd.length() && cmd[i] == ' ') i++;
    int j = i;
    while (j < (int)cmd.length() && cmd[j] != ' ') j++;
    if (j > i) a[n++] = cmd.substring(i, j);
    i = j;
  }
  String op = n > 1 ? a[1] : "";
  if (op == "lv") { levels(out); return true; }
  if (op == "clk") { recover(out); return true; }
  if (op == "f" && n >= 3) {  // bus clock in Hz (decimal)
    freq = a[2].toInt();
    if (started) { Wire.end(); started = false; }
    out.printf("# i2c clock %u Hz\n", (unsigned)freq);
    return true;
  }
  if (op == "mon") {  // sample both lines: edges, time low, longest low
    if (started) { Wire.end(); started = false; }
    pinMode(PIN_SCL, INPUT);
    pinMode(PIN_SDA, INPUT);
    uint32_t ms = n > 2 ? a[2].toInt() : 3000;
    uint32_t edges[2] = {0, 0}, lowus[2] = {0, 0}, maxlow[2] = {0, 0}, lowstart[2] = {0, 0};
    int last[2] = {digitalRead(PIN_SCL), digitalRead(PIN_SDA)};
    uint32_t t0 = micros(), prev = t0;
    for (int k = 0; k < 2; k++) if (!last[k]) lowstart[k] = t0;
    while (micros() - t0 < ms * 1000) {
      uint32_t t = micros();
      int v[2] = {digitalRead(PIN_SCL), digitalRead(PIN_SDA)};
      for (int k = 0; k < 2; k++) {
        if (!v[k]) lowus[k] += t - prev;
        if (v[k] != last[k]) {
          edges[k]++;
          if (!v[k]) lowstart[k] = t;
          else if (t - lowstart[k] > maxlow[k]) maxlow[k] = t - lowstart[k];
          last[k] = v[k];
        }
      }
      prev = t;
    }
    for (int k = 0; k < 2; k++)
      if (!last[k] && prev - lowstart[k] > maxlow[k]) maxlow[k] = prev - lowstart[k];
    out.printf("# i2c mon %u ms: SCL edges=%u low=%u%% maxlow=%uus | SDA edges=%u low=%u%% maxlow=%uus\n", ms,
               edges[0], lowus[0] / (ms * 10), maxlow[0], edges[1], lowus[1] / (ms * 10), maxlow[1]);
    return true;
  }
  if (op == "pu") {  // with the ESP's ~45 kOhm pull-ups: tells "held low" from "no pull-up"
    if (started) { Wire.end(); started = false; }
    pinMode(PIN_SCL, INPUT_PULLUP);
    pinMode(PIN_SDA, INPUT_PULLUP);
    delay(2);
    out.printf("# i2c levels (ESP pull-ups on): SCL=%d SDA=%d\n", digitalRead(PIN_SCL), digitalRead(PIN_SDA));
    pinMode(PIN_SCL, INPUT);
    pinMode(PIN_SDA, INPUT);
    return true;
  }
  if (op == "scan") {
    if (!begin(out)) return true;
    out.print("# i2c scan:");
    int found = 0;
    for (int ad = 0x08; ad < 0x78; ad++) {
      Wire.beginTransmission(ad);
      if (Wire.endTransmission() == 0) { out.printf(" %02X", ad); found++; }
    }
    out.printf("  (%d found)\n", found);
    return true;
  }
  if (op == "rd" && n >= 4) {
    if (!begin(out)) return true;
    int ad = hex(a[2]), reg = hex(a[3]), cnt = n > 4 ? hex(a[4]) : 1;
    if (cnt < 1 || cnt > 128) { out.println("# i2c rd: n must be 1..80 (hex)"); return true; }
    Wire.beginTransmission(ad);
    Wire.write(reg);
    int e = Wire.endTransmission(false);  // repeated START
    if (e) { out.printf("# i2c rd %02X %02X: write phase error %d\n", ad, reg, e); return true; }
    int got = Wire.requestFrom(ad, cnt);
    out.printf("# i2c rd %02X %02X [%d/%d]:", ad, reg, got, cnt);
    while (Wire.available()) out.printf(" %02X", Wire.read());
    out.println();
    return true;
  }
  if (op == "wr" && n >= 5) {
    if (!begin(out)) return true;
    int ad = hex(a[2]), reg = hex(a[3]);
    Wire.beginTransmission(ad);
    Wire.write(reg);
    for (int i = 4; i < n; i++) Wire.write((uint8_t)hex(a[i]));
    int e = Wire.endTransmission();
    out.printf("# i2c wr %02X %02X (%d bytes): %s\n", ad, reg, n - 4, e ? "error" : "ok");
    if (e) out.printf("#   error code %d\n", e);
    return true;
  }
  out.println("# i2c cmds: i2c lv | i2c clk | i2c scan | i2c rd <a> <r> [n] | i2c wr <a> <r> <b...>  (hex)");
  return true;
}

}  // namespace i2cm

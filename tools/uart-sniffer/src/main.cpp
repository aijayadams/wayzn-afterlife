// Passive dual-UART sniffer for the Wayzn WYP-BC-100 "STM+IMP" header.
//
// Listens on two pins at once (A = header "RX", B = header "TX") with UART
// receivers only. TX is never attached, so this can't drive the bus.
// Output (USB serial, 921600):
//   <seconds> <chan> [<n>] <hex bytes>  |<printable ascii>|
// Lines starting with '#' are tool messages. Frames are split on an idle gap of
// ~20 bit-times. Timestamps are host-side arrival times (tens of µs jitter);
// use the scope or a logic analyzer for exact timing.
//
// Commands (type a line and press enter in the monitor):
//   b <baud>   set baud for both channels (default 115200)
//   i          toggle inverted polarity
//   m [ms]     measure: count edges and the shortest pulse on each pin (default 3000 ms)
//   l          print the current idle level of each pin
//   ?          help / show config
//   s          (I2C build) toggle summary of long I2C reads
//
// Build env esp32dev-host (-DSTM_TX) can also send lines to the STM32 on pin A
// (header RX), for bench tests with the imp held in reset:
//   > <line>   send <line>\n to the STM. Only commands on the safe list (no motor
//              motion, see kSafe below) are sent, unless the link is armed.
//   arm / disarm   allow / forbid motion commands (open, close, engage, ...).
// TX is attached on the first send, not at boot, so we never drive the STM's RX
// while the board is unpowered (that would back-power it through the pin).
// Our own bytes loop back and show up as channel-A frames.
//
// Build env esp32dev-net (-DSTM_TX -DWITH_NET, no I2C): as esp32dev-host, plus Wi-Fi
// (secrets.ini), a telnet console on port 23 and OTA updates (see src/net.h). The
// telnet client gets the same output and takes the same commands; if it disconnects
// while armed, the link is disarmed.
//
// Build env esp32dev-i2c (-DWITH_I2C) also sniffs the Grid-EYE/accelerometer
// I2C bus on GPIO21 (SCL) / GPIO22 (SDA), printing `<secs> I <txn>` lines
// (see ../i2c-sniffer/src/i2c_sniff.h). The I2C poller owns core 1, so in that
// build all UART/USB work runs on core 0.
#include <Arduino.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_rom_sys.h"
#ifdef STM_TX
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#endif
#ifdef WITH_I2C
#include "i2c_sniff.h"
#endif
#ifdef WITH_I2C_MASTER
#include "i2c_master.h"
#endif
#ifdef WITH_NET
#include "net.h"
#define OUT net::out
#else
#define OUT Serial
#endif

#ifndef PIN_A
#define PIN_A 25
#endif
#ifndef PIN_B
#define PIN_B 26
#endif

static uint32_t g_baud = 115200;
static bool g_invert = false;
#ifdef STM_TX
static bool g_armed = false, g_tx_on = false;
#endif

struct Chan {
  char name;
  int pin;
  HardwareSerial *ser;
  uint8_t buf[64];
  size_t len;
  int64_t tFirst;
  int64_t tLast;
};

static HardwareSerial SerA(1);
static HardwareSerial SerB(2);
static Chan g_ch[2] = {{'A', PIN_A, &SerA, {}, 0, 0, 0},
                       {'B', PIN_B, &SerB, {}, 0, 0, 0}};

static int64_t gapUs() {
  int64_t g = 20LL * 1000000LL / g_baud;  // ~2 characters of idle
  return g < 300 ? 300 : g;
}

static void startUarts() {
  for (auto &c : g_ch) {
    c.ser->end();
    c.ser->setRxBufferSize(8192);
    // rx only: txPin = -1. Never drive the bus.
    c.ser->begin(g_baud, SERIAL_8N1, c.pin, -1, g_invert);
    c.ser->setRxFIFOFull(1);  // hand bytes over promptly, for better timestamps
    c.len = 0;
  }
#ifdef STM_TX
  g_tx_on = false;  // begin() re-routed the pin as input only; next send re-attaches TX
#endif
}

static void stopUarts() {
  for (auto &c : g_ch) c.ser->end();
}

static void flush(Chan &c) {
  if (!c.len) return;
  char line[64 * 3 + 64 + 48];
  int n = snprintf(line, sizeof line, "%.6f %c [%u] ", c.tFirst / 1e6, c.name,
                   (unsigned)c.len);
  for (size_t i = 0; i < c.len; i++) n += snprintf(line + n, sizeof line - n, "%02X ", c.buf[i]);
  n += snprintf(line + n, sizeof line - n, " |");
  for (size_t i = 0; i < c.len; i++) {
    uint8_t b = c.buf[i];
    line[n++] = (b >= 0x20 && b < 0x7f) ? (char)b : '.';
  }
  line[n++] = '|';
  line[n] = 0;
  OUT.println(line);
  c.len = 0;
}

// ---- measure mode: shortest pulse on each pin gives the bit time ----
static volatile int64_t m_last[2];
static volatile uint32_t m_min[2], m_edges[2];

static void IRAM_ATTR edge(int i) {
  int64_t t = esp_timer_get_time();
  if (m_last[i]) {
    uint32_t w = (uint32_t)(t - m_last[i]);
    if (w < m_min[i]) m_min[i] = w;
  }
  m_last[i] = t;
  m_edges[i]++;
}
static void IRAM_ATTR isrA() { edge(0); }
static void IRAM_ATTR isrB() { edge(1); }

static uint32_t guessBaud(uint32_t us) {
  static const uint32_t std[] = {1200, 2400, 4800, 9600, 19200, 38400,
                                 57600, 115200, 230400, 460800};
  if (!us) return 0;
  float est = 1e6f / us;
  uint32_t best = 0;
  float bestErr = 0.2f;  // within 20%
  for (uint32_t s : std) {
    float e = fabsf(est - s) / s;
    if (e < bestErr) { bestErr = e; best = s; }
  }
  return best;
}

static void printLevels() {
  for (auto &c : g_ch)
    OUT.printf("# pin %c (GPIO%d) level=%d\n", c.name, c.pin, digitalRead(c.pin));
}

static void measure(uint32_t ms) {
  stopUarts();
  for (int i = 0; i < 2; i++) { m_last[i] = 0; m_min[i] = UINT32_MAX; m_edges[i] = 0; }
  pinMode(PIN_A, INPUT);
  pinMode(PIN_B, INPUT);
  printLevels();
  OUT.printf("# measuring for %u ms...\n", (unsigned)ms);
  attachInterrupt(digitalPinToInterrupt(PIN_A), isrA, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_B), isrB, CHANGE);
  delay(ms);
  detachInterrupt(digitalPinToInterrupt(PIN_A));
  detachInterrupt(digitalPinToInterrupt(PIN_B));
  for (int i = 0; i < 2; i++) {
    uint32_t w = m_min[i] == UINT32_MAX ? 0 : m_min[i];
    OUT.printf("# %c: edges=%u min_pulse=%uus -> baud guess %u%s\n", g_ch[i].name,
                  (unsigned)m_edges[i], (unsigned)w, (unsigned)guessBaud(w),
                  w && w < 15 ? " (ISR latency limits this; confirm on the scope)" : "");
  }
  startUarts();
}

#ifdef PIN_STM_RST
// STM32 NRST (SW300 "EXP RST" top pad), open-drain: we only ever pull it low briefly.
// The STM doesn't start reliably on a cold power-up with the ESP32 on the same 3V3 rail
// (the imp used to reset it), so: one pulse ~2 s after boot, and again if the STM has been
// silent for 30 s (it prints status at least every 10 s), at most once a minute.
static int64_t g_last_b_us = 0, g_last_rst_us = 0;
static bool g_boot_rst_done = false;
static uint32_t g_stm_resets = 0;
static void stmResetPulse(const char *why) {
  int before = gpio_get_level((gpio_num_t)PIN_STM_RST);
  gpio_set_level((gpio_num_t)PIN_STM_RST, 0);
  delay(20);
  int during = gpio_get_level((gpio_num_t)PIN_STM_RST);
  gpio_set_level((gpio_num_t)PIN_STM_RST, 1);
  delayMicroseconds(200);
  OUT.printf("# stm reset line: before=%d during=%d after=%d\n", before, during, gpio_get_level((gpio_num_t)PIN_STM_RST));
  g_last_rst_us = g_last_b_us = esp_timer_get_time();
  g_stm_resets++;
  OUT.printf("# stm reset pulse on GPIO%d (%s), #%u\n", PIN_STM_RST, why, (unsigned)g_stm_resets);
}
static void stmResetPoll() {
  int64_t now = esp_timer_get_time();
  if (!g_boot_rst_done && now > 2000000) { g_boot_rst_done = true; stmResetPulse("boot"); return; }
  if (g_boot_rst_done && now - g_last_b_us > 30000000LL && now - g_last_rst_us > 60000000LL)
    stmResetPulse("STM silent 30 s");
}
#endif
#ifdef PIN_IMP_RST
// Imp reset (SW1 wire). Open-drain: LOW holds the imp in reset, released = floats
// (the board pulls it up). Held from boot by default.
static bool g_imp_held = true;
static void impReset(bool hold) {
  g_imp_held = hold;
  pinMode(PIN_IMP_RST, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_IMP_RST, hold ? LOW : HIGH);
}
#endif

#ifdef STM_TX
// STM32 shell commands that cannot start the motor (from the firmware's command
// table, see docs/STM-FIRMWARE.md). `stop` only stops, so it's always allowed (abort).
// Excluded: open close open_and_close auto_zero engage disengage go p v a limit decay pgain igain dgain pmax imax dmax
// reset_motor sleep_motor mreset get_fake_data, and zero with a moving state.
static const char *kSafe[] = {"stop", "version", "reset", "echo", "help", "quad", "c", "vsense",
                              "getstate", "start_log", "profile", "get_profile", "set_profile",
                              "profiled_state", "reset_zero", "safetoclose", "set_obstruction",
                              "clear_obstruction", "set_dooroffset", "set_open_to",
                              "set_button_op", "set_open_wait", "zero"};

static bool safeLine(const String &l, String &why) {
  int sp = l.indexOf(' ');
  String w = sp < 0 ? l : l.substring(0, sp);
  bool ok = false;
  for (auto s : kSafe) ok |= (w == s);
  if (!ok) { why = "'" + w + "' can move the motor (or isn't on the safe list)"; return false; }
  if (w == "zero") {
    // zero <pos> <state>: states 12,13 (opening/closing) and 26..28 (disengage/engage
    // moves) make the STM resume a move. Only allow resting states.
    int sp2 = l.indexOf(' ', sp + 1);
    if (sp < 0 || sp2 < 0) { why = "zero needs <pos> <state>"; return false; }
    long st = l.substring(sp2 + 1).toInt();
    if (st == 12 || st == 13 || (st >= 26 && st <= 28)) { why = "zero with a moving state"; return false; }
  }
  return true;
}

static void attachTx();

// Read-only probe of the STM32 system bootloader (AN3155): 0x7F (autobaud, ACK 0x79), then
// GET_ID (0x02 0xFD). Every bit of these bytes' even parity is 1, which looks like a stop bit,
// so 8N1 sends valid 8E1 frames. Replies show up on channel B. Never writes or erases.
static void bootProbe() {
  attachTx();
  SerA.write((uint8_t)0x7F); SerA.flush(); delay(50);
  SerA.write((uint8_t)0x02); SerA.write((uint8_t)0xFD); SerA.flush();
  OUT.println("# sent bootloader probe: 7F, then GET_ID 02 FD (expect 79 ... on B if in bootloader)");
}

static void stmSend(const String &l) {
  String why;
  if (!g_armed && !safeLine(l, why)) { OUT.printf("# refused (disarmed): %s\n", why.c_str()); return; }
  attachTx();
  SerA.print(l);
  SerA.print('\n');
  SerA.flush();
  OUT.printf("# sent%s: %s\n", g_armed ? " (ARMED)" : "", l.c_str());
}

static void attachTx() {
  if (!g_tx_on) {
    // HardwareSerial::begin() routed RX on PIN_A; route UART1 TX to the same pin as well.
    gpio_set_direction((gpio_num_t)PIN_A, GPIO_MODE_INPUT_OUTPUT);
    esp_rom_gpio_connect_out_signal(PIN_A, U1TXD_OUT_IDX, false, false);
    g_tx_on = true;
    // Attaching makes one glitch byte (seen as F8). Terminate it as its own line,
    // which the STM shell drops as an unknown command.
    delay(2);
    SerA.print('\n');
    SerA.flush();
    OUT.println("# tx attached on pin A (sent a flush newline)");
  }
}
#endif

static const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "ext-pin";
    case ESP_RST_SW: return "software/OTA";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT-WDT (hang)";
    case ESP_RST_TASK_WDT: return "TASK-WDT (hang)";
    case ESP_RST_WDT: return "RTC-WDT (often a slow power-up)";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    default: return "other";
  }
}

static void help() {
  OUT.printf("# wayzn uart-sniffer  A=GPIO%d  B=GPIO%d  baud=%u  invert=%d  gap=%lldus\n",
                PIN_A, PIN_B, (unsigned)g_baud, g_invert, gapUs());
  OUT.printf("# up %lus, last reset: %s (code %d, rom %d)\n", (unsigned long)(millis() / 1000), resetReason(),
             (int)esp_reset_reason(), (int)esp_rom_get_reset_reason(0));
  OUT.println("# cmds: b <baud> | i (invert) | m [ms] (measure) | l (levels) | ? (help)");
#ifdef PIN_STM_RST
  OUT.printf("# stm reset GPIO%d (open-drain, released; pin reads %d), pulses so far %u  cmds: sreset\n",
             PIN_STM_RST, gpio_get_level((gpio_num_t)PIN_STM_RST), (unsigned)g_stm_resets);
#endif
#ifdef PIN_IMP_RST
  OUT.printf("# imp reset GPIO%d: %s (pin reads %d)  cmds: ih (hold) | ir (release)\n", PIN_IMP_RST,
                g_imp_held ? "HELD" : "released", digitalRead(PIN_IMP_RST));
#endif
#ifdef STM_TX
  OUT.printf("# stm tx: %s, %s  cmds: > <line> | arm | disarm\n", g_tx_on ? "attached" : "idle",
                g_armed ? "ARMED (motion allowed)" : "disarmed (safe list only)");
#endif
#ifdef WITH_NET
  OUT.printf("# net: wifi %s ip %s, telnet %s, OTA %s, telnet drops %u\n", WIFI_SSID,
             net::up ? WiFi.localIP().toString().c_str() : "(connecting)",
             net::connected ? "connected" : "none",
             g_armed ? "blocked while armed" : "ready", (unsigned)net::dropped);
#endif
#ifdef WITH_I2C_MASTER
  OUT.println("# i2c master: i2c lv | i2c clk | i2c scan | i2c rd <a> <r> [n] | i2c wr <a> <r> <b...>  (hex)");
#endif
#ifdef WITH_I2C
  i2cs::status(OUT);
  OUT.println("# i2c cmds: s (toggle summary of long reads)");
#endif
}

static void handleCommand(String cmd) {
  cmd.trim();
#ifdef WITH_I2C_MASTER
  if (i2cm::command(cmd, OUT)) return;
#endif
#ifdef STM_TX
  if (cmd.startsWith(">")) { String l = cmd.substring(1); l.trim(); if (l.length()) stmSend(l); return; }
  if (cmd == "blprobe") { bootProbe(); return; }
#ifdef PIN_STM_RST
  if (cmd == "sreset") { stmResetPulse("command"); return; }
#endif
  if (cmd == "arm" || cmd == "disarm") { g_armed = cmd == "arm"; help(); return; }
#endif
  if (cmd.startsWith("b ")) {
    uint32_t b = cmd.substring(2).toInt();
    if (b >= 300 && b <= 2000000) { g_baud = b; startUarts(); }
    help();
  } else if (cmd == "i") {
    g_invert = !g_invert; startUarts(); help();
  } else if (cmd.startsWith("m")) {
    uint32_t ms = cmd.length() > 2 ? cmd.substring(2).toInt() : 3000;
    measure(ms ? ms : 3000);
  } else if (cmd == "l") {
    printLevels();
  } else if (cmd.startsWith("fl")) {
    // Floating test: the UART RX pins have pull-ups, so a disconnected wire reads 1 like an
    // idle line. With the pull-ups off, a connected line stays put; a loose one picks up noise.
    stopUarts();
    for (auto &c : g_ch) pinMode(c.pin, INPUT);
    uint32_t ms = cmd.length() > 3 ? cmd.substring(3).toInt() : 2000, edges[2] = {0, 0}, low[2] = {0, 0}, samples = 0;
    int last[2] = {digitalRead(g_ch[0].pin), digitalRead(g_ch[1].pin)};
    uint32_t t0 = millis();
    while (millis() - t0 < ms) {
      for (int k = 0; k < 2; k++) {
        int v = digitalRead(g_ch[k].pin);
        if (v != last[k]) { edges[k]++; last[k] = v; }
        if (!v) low[k]++;
      }
      samples++;
    }
    for (int k = 0; k < 2; k++)
      OUT.printf("# float test %c (GPIO%d), pull-up off, %u ms: edges=%u low=%u%%\n", g_ch[k].name, g_ch[k].pin,
                 (unsigned)ms, (unsigned)edges[k], (unsigned)(low[k] * 100 / samples));
    startUarts();
#ifdef PIN_IMP_RST
  } else if (cmd == "ih" || cmd == "ir") {
    impReset(cmd == "ih");
    OUT.printf("# imp reset %s\n", g_imp_held ? "HELD" : "released");
    help();
#endif
#ifdef WITH_I2C
  } else if (cmd == "s") {
    i2cs::summary = !i2cs::summary;
    help();
#endif
  } else {
    help();
  }
}

#ifdef WITH_NET
static void onTelnetGone() {
#ifdef STM_TX
  // Whoever armed it may be gone: drop back to the safe list. A move in progress
  // still finishes (the STM has no host timeout); `> stop` aborts.
  if (g_armed) { g_armed = false; OUT.println("# telnet client gone: disarmed"); }
#endif
}
#endif

static void sniffSetup() {
#ifdef PIN_STM_RST
  // Open-drain with the input buffer on, so digitalRead() shows the real line level
  // (pinMode(OUTPUT_OPEN_DRAIN) leaves the input off and always reads 0).
  gpio_set_level((gpio_num_t)PIN_STM_RST, 1);  // released before it becomes an output
  gpio_set_direction((gpio_num_t)PIN_STM_RST, GPIO_MODE_INPUT_OUTPUT_OD);
  gpio_set_pull_mode((gpio_num_t)PIN_STM_RST, GPIO_FLOATING);
#endif
#ifdef PIN_IMP_RST
  impReset(true);  // first thing: hold the imp
#endif
  Serial.begin(921600);

  delay(500);
#ifdef WITH_NET
  net::begin(onTelnetGone);
#endif
  startUarts();
  help();
}

static void feed(String &cmd, char c) {
  if (c == '\n' || c == '\r') { if (cmd.length()) handleCommand(cmd); cmd = ""; }
  else if (cmd.length() < 256) cmd += c;
}

static void sniffLoop() {
  static String cmd;
  while (Serial.available()) feed(cmd, Serial.read());
#ifdef PIN_STM_RST
  stmResetPoll();
#endif
#ifdef WITH_NET
  static String ncmd;
  net::armed = g_armed;  // OTA is only handled while disarmed
  for (int c; (c = net::read()) >= 0;) feed(ncmd, (char)c);
#endif
  int64_t gap = gapUs();
  for (auto &c : g_ch) {
    while (c.ser->available()) {
      int b = c.ser->read();
      int64_t now = esp_timer_get_time();
      if (c.len && (now - c.tLast > gap || c.len == sizeof c.buf)) flush(c);
      if (!c.len) c.tFirst = now;
      c.buf[c.len++] = (uint8_t)b;
      c.tLast = now;
#ifdef PIN_STM_RST
      if (&c == &g_ch[1]) g_last_b_us = now;
#endif
#if defined(STM_TX) && defined(WITH_NET)
      // Drive the STM's RX line (idle high) as soon as the STM talks: that proves the board
      // is powered, so we can't back-power it through the pin. Otherwise the line floats.
      if (&c == &g_ch[1] && !g_tx_on) attachTx();
#endif
    }
    if (c.len && esp_timer_get_time() - c.tLast > gap) flush(c);
  }
}

#ifdef WITH_I2C
// Everything except the I2C poller runs on core 0, so the UART and USB driver
// interrupts are installed there and aren't held off by the poller on core 1.
// Busy loop (no delay) to keep UART timestamps tight; i2cs::start() turns the
// task watchdog off for that.
static void main_task(void *) {
  sniffSetup();
  i2cs::start();
  for (;;) {
    sniffLoop();
    i2cs::poll(OUT);
  }
}

void setup() { xTaskCreatePinnedToCore(main_task, "main", 8192, nullptr, 1, nullptr, 0); }
void loop() { vTaskDelete(nullptr); }
#else
void setup() { sniffSetup(); }
void loop() { sniffLoop(); }
#endif

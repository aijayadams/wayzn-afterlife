// Passive I2C sniffer core, shared by tools/i2c-sniffer and tools/uart-sniffer
// (env esp32dev-i2c). Header-only; include once.
//
// Both pins are plain inputs (no pull-ups, never driven). A task pinned to
// core 1 polls GPIO_IN_REG (~10 MHz) and decodes START / bits / ACK / STOP.
// Interrupts are disabled on core 1 while it polls, so anything else must run
// on core 0 (including the UART driver ISRs and USB serial).
//
// Printed line per transaction (START .. STOP):
//   <secs> I W69 80 | R69 <bytes...>
// "W69"/"R69" = 7-bit address + direction, '|' = repeated START, '~' = NAK,
// "!trunc" = more than I2C_MAX_BYTES, "!timeout" = no STOP within 20 ms.
#pragma once
#include <Arduino.h>
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "soc/gpio_reg.h"
#include "xtensa/core-macros.h"

#ifndef PIN_SCL
#define PIN_SCL 21
#endif
#ifndef PIN_SDA
#define PIN_SDA 22
#endif

namespace i2cs {

constexpr int MAX_BYTES = 200;  // Grid-EYE frame = 128 B + address/register
constexpr int RING = 64;

struct Txn {
  int64_t t_us;
  uint16_t n;
  uint8_t flags;  // 1 = truncated, 2 = timeout
  // bits 0-7 data, bit 8 = NAK, bit 9 = first byte after a START (address)
  uint16_t b[MAX_BYTES];
};

static Txn ring[RING];
static Txn scratch;
static volatile uint32_t head = 0, tail = 0;  // head: core 1, tail: printer
static uint32_t dropped = 0, total = 0;
static bool summary = false;  // collapse long reads to <n bytes sum=..>
static esp_err_t wdt_err = ESP_OK;  // result of esp_task_wdt_deinit()

static inline uint32_t rd() {
  uint32_t v = REG_READ(GPIO_IN_REG);
  return ((v >> PIN_SCL) & 1) | (((v >> PIN_SDA) & 1) << 1);  // bit0 SCL, bit1 SDA
}

// Two equal consecutive reads = stable (~200 ns deglitch; the bus has slow, noisy edges).
static inline uint32_t rd_stable() {
  uint32_t a = rd();
  for (;;) {
    uint32_t b = rd();
    if (a == b) return a;
    a = b;
  }
}

static void sniff_task(void *) {
  const uint32_t cpu_hz = getCpuFrequencyMhz() * 1000000u;
  uint32_t s = rd_stable();
  for (;;) {
    // Idle: poll for START (SDA falls while SCL high) in 50 ms windows with
    // interrupts off, re-enabling them between windows so the tick runs.
    portDISABLE_INTERRUPTS();
    uint32_t t0 = XTHAL_GET_CCOUNT();
    bool started = false;
    while ((uint32_t)(XTHAL_GET_CCOUNT() - t0) < cpu_hz / 20) {
      uint32_t n = rd_stable();
      if (n == s) continue;
      if ((s & 1) && (n & 1) && (s & 2) && !(n & 2)) { s = n; started = true; break; }
      s = n;
    }
    if (!started) { portENABLE_INTERRUPTS(); continue; }

    // In a transaction: interrupts stay off until STOP or 20 ms.
    uint32_t next = (head + 1) % RING;
    Txn *t = (next == tail) ? &scratch : &ring[head];
    t->t_us = esp_timer_get_time();
    t->n = 0;
    t->flags = 0;
    int bit = 0;
    uint8_t byte = 0;
    bool addr_next = true;
    t0 = XTHAL_GET_CCOUNT();
    for (;;) {
      if ((uint32_t)(XTHAL_GET_CCOUNT() - t0) > cpu_hz / 50) { t->flags |= 2; break; }
      uint32_t n = rd_stable();
      if (n == s) continue;
      bool scl_hi = (s & 1) && (n & 1);
      if (scl_hi && (s & 2) && !(n & 2)) {         // repeated START
        bit = 0; byte = 0; addr_next = true;
      } else if (scl_hi && !(s & 2) && (n & 2)) {  // STOP
        s = n;
        break;
      } else if (!(s & 1) && (n & 1)) {            // SCL rising: sample SDA
        int sda = (n >> 1) & 1;
        if (bit < 8) {
          byte = (byte << 1) | sda;
          bit++;
        } else {  // ACK bit
          if (t->n < MAX_BYTES) t->b[t->n++] = byte | (sda ? 0x100 : 0) | (addr_next ? 0x200 : 0);
          else t->flags |= 1;
          addr_next = false; bit = 0; byte = 0;
        }
      }
      s = n;
    }
    portENABLE_INTERRUPTS();
    total = total + 1;
    if (t == &scratch) dropped = dropped + 1;
    else head = next;
  }
}

static void print_txn(Print &out, const Txn &t) {
  static char line[MAX_BYTES * 4 + 80];
  int p = snprintf(line, sizeof line, "%.6f I", t.t_us / 1e6);
  int i = 0;
  while (i < t.n) {
    uint16_t v = t.b[i];
    if (v & 0x200) {
      p += snprintf(line + p, sizeof line - p, "%s %c%02X%s", i ? " |" : "",
                    (v & 1) ? 'R' : 'W', (v & 0xFF) >> 1, (v & 0x100) ? "~" : "");
      i++;
      int j = i;
      while (j < t.n && !(t.b[j] & 0x200)) j++;
      if (summary && j - i > 16) {
        uint32_t sum = 0;
        for (int k = i; k < j; k++) sum += t.b[k] & 0xFF;
        p += snprintf(line + p, sizeof line - p, " <%d bytes sum=%04X>", j - i, (unsigned)(sum & 0xFFFF));
        i = j;
      }
      continue;
    }
    p += snprintf(line + p, sizeof line - p, " %02X%s", v & 0xFF, (v & 0x100) ? "~" : "");
    i++;
  }
  if (t.flags & 1) p += snprintf(line + p, sizeof line - p, " !trunc");
  if (t.flags & 2) p += snprintf(line + p, sizeof line - p, " !timeout");
  out.println(line);
}

// Print any finished transactions. Call from core 0.
static void poll(Print &out) {
  while (tail != head) {
    print_txn(out, ring[tail]);
    tail = (tail + 1) % RING;
  }
}

static void status(Print &out) {
  out.printf("# i2c: SCL=GPIO%d SDA=GPIO%d levels SCL=%u SDA=%u summary=%d txns=%u dropped=%u wdt_deinit=%d\n",
             PIN_SCL, PIN_SDA, (unsigned)(rd() & 1), (unsigned)(rd() >> 1), summary,
             (unsigned)total, (unsigned)dropped, (int)wdt_err);
}

// Start the poller on core 1. Call after everything else is set up on core 0.
static void start() {
  pinMode(PIN_SCL, INPUT);
  pinMode(PIN_SDA, INPUT);
  // Busy tasks on both cores starve the idle tasks, so turn the task watchdog off
  // entirely. (Only unsubscribing the idle tasks leaves their idle hooks feeding a
  // watchdog they're no longer on: an endless "task not found" log flood.)
  wdt_err = esp_task_wdt_deinit();
  xTaskCreatePinnedToCore(sniff_task, "i2c-sniff", 4096, nullptr, configMAX_PRIORITIES - 1, nullptr, 1);
}

}  // namespace i2cs

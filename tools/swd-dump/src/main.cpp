// Read-only SWD flash reader for the Wayzn STM32F051 (U300), bit-banged on an ESP32.
//
// READ-ONLY BY CONSTRUCTION: the only target writes are to SWD debug-port/AP
// registers (ABORT, CTRL/STAT power-up, SELECT, CSW, TAR). There is no code path that writes
// to target memory, the flash controller or the option bytes. It never halts the core.
//
// Wiring (ESP8266: GPIO5/D1 -> SWCLK, GPIO4/D2 -> SWDIO): GPIO18 -> SWCLK (JP300-6), GPIO19 -> SWDIO (JP300-1), GND -> J105-4.
// USB serial 921600. Commands:
//   i  connect, print DP IDCODE, AP IDR, DBGMCU_IDCODE, FLASH_OBR / RDP level, option bytes
//   d  dump flash 0x08000000..+64 KB as "D <addr> <8 words>" lines + "CRC <crc32>"
//      (refuses unless RDP is level 0)
#include <Arduino.h>
#ifdef ESP8266
static uint32_t crc32_le(uint32_t crc, const uint8_t *b, size_t n) {
  crc = ~crc;
  while (n--) { crc ^= *b++; for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1)); }
  return ~crc;
}
#else
#include "esp_rom_crc.h"
#define crc32_le esp_rom_crc32_le
#endif

#ifndef PIN_SWCLK
#define PIN_SWCLK 18
#endif
#ifndef PIN_SWDIO
#define PIN_SWDIO 19
#endif

static const uint32_t FLASH_BASE_ = 0x08000000, FLASH_SIZE_ = 64 * 1024;
static const uint32_t FLASH_OBR = 0x4002201C, DBGMCU_IDCODE = 0x40015800, OPTBYTES = 0x1FFFF800;

enum Ack { ACK_OK = 1, ACK_WAIT = 2, ACK_FAULT = 4 };

static inline void dly() { delayMicroseconds(1); }
static inline void clk_cycle() { digitalWrite(PIN_SWCLK, LOW); dly(); digitalWrite(PIN_SWCLK, HIGH); dly(); }
static inline void wbit(int b) { digitalWrite(PIN_SWDIO, b); clk_cycle(); }
static inline int rbit() {
  digitalWrite(PIN_SWCLK, LOW); dly();
  int b = digitalRead(PIN_SWDIO);
  digitalWrite(PIN_SWCLK, HIGH); dly();
  return b;
}
static void drive() { pinMode(PIN_SWDIO, OUTPUT); }
static void release() { pinMode(PIN_SWDIO, INPUT_PULLUP); }
static void wbits(uint32_t v, int n) { for (int i = 0; i < n; i++) wbit((v >> i) & 1); }

static void line_reset() { drive(); for (int i = 0; i < 56; i++) wbit(1); }

static void connect_seq() {
  line_reset();
  wbits(0xE79E, 16);  // JTAG-to-SWD
  line_reset();
  wbits(0, 8);        // idle
}

// One SWD transaction. ap: 0 = DP, 1 = AP. a: register address (0, 4, 8, 0xC).
static int xfer(int ap, int rnw, uint8_t a, uint32_t *data) {
  for (int tries = 0; tries < 100; tries++) {
    int a2 = (a >> 2) & 1, a3 = (a >> 3) & 1;
    int par = (ap + rnw + a2 + a3) & 1;
    drive();
    wbit(1); wbit(ap); wbit(rnw); wbit(a2); wbit(a3); wbit(par); wbit(0); wbit(1);
    release();
    rbit();  // turnaround
    int ack = rbit() | (rbit() << 1) | (rbit() << 2);
    if (ack == ACK_OK) {
      if (rnw) {
        uint32_t v = 0; int p = 0;
        for (int i = 0; i < 32; i++) { int b = rbit(); v |= (uint32_t)b << i; p ^= b; }
        int pb = rbit();
        rbit();  // turnaround
        drive(); wbits(0, 2);
        if (pb != p) return -1;  // parity error
        *data = v;
      } else {
        rbit();  // turnaround
        drive();
        uint32_t v = *data; int p = 0;
        for (int i = 0; i < 32; i++) { int b = (v >> i) & 1; wbit(b); p ^= b; }
        wbit(p);
        wbits(0, 2);
      }
      return ACK_OK;
    }
    rbit();  // turnaround back
    drive(); wbits(0, 2);
    if (ack != ACK_WAIT) return ack ? ack : -2;  // FAULT, or no response (-2 / 7)
  }
  return ACK_WAIT;
}

static int dp_r(uint8_t a, uint32_t *v) { return xfer(0, 1, a, v); }
static int dp_w(uint8_t a, uint32_t v) { return xfer(0, 0, a, &v); }
static int ap_r(uint8_t a, uint32_t *v) { return xfer(1, 1, a, v); }
static int ap_w(uint8_t a, uint32_t v) { return xfer(1, 0, a, &v); }

static uint32_t cur_select = 0xFFFFFFFF;
static int select_bank(uint8_t a) {
  uint32_t s = a & 0xF0;  // APSEL 0, APBANKSEL = a[7:4]
  if (s == cur_select) return ACK_OK;
  int r = dp_w(0x8, s);
  if (r == ACK_OK) cur_select = s;
  return r;
}
static int ap_read(uint8_t a, uint32_t *v) {  // AP reads are posted: read, then RDBUFF
  int r = select_bank(a); if (r != ACK_OK) return r;
  uint32_t dummy;
  if ((r = ap_r(a & 0xC, &dummy)) != ACK_OK) return r;
  return dp_r(0xC, v);
}
static int ap_write(uint8_t a, uint32_t v) {
  int r = select_bank(a); if (r != ACK_OK) return r;
  return ap_w(a & 0xC, v);
}

static void clear_errors() { dp_w(0x0, 0x1E); }  // ABORT: clear sticky flags

static int mem_read(uint32_t addr, uint32_t *v) {
  int r;
  if ((r = ap_write(0x04, addr)) != ACK_OK) return r;  // TAR
  return ap_read(0x0C, v);                           // DRW
}

static bool connected = false;
static bool do_connect() {
  connected = false; cur_select = 0xFFFFFFFF;
  connect_seq();
  uint32_t id = 0;
  int r = dp_r(0x0, &id);
  if (r != ACK_OK) { Serial.printf("ERR IDCODE ack=%d (no target? check wiring, power, GND)\n", r); return false; }
  Serial.printf("DP IDCODE %08X%s\n", (unsigned)id, id == 0x0BB11477 ? " (Cortex-M0 SW-DP)" : "");
  clear_errors();
  dp_w(0x4, 0x50000000);  // CTRL/STAT: CSYSPWRUPREQ | CDBGPWRUPREQ
  uint32_t cs = 0;
  for (int i = 0; i < 100; i++) { dp_r(0x4, &cs); if ((cs & 0xA0000000) == 0xA0000000) break; delay(1); }
  Serial.printf("DP CTRL/STAT %08X\n", (unsigned)cs);
  if ((cs & 0xA0000000) != 0xA0000000) { Serial.println("ERR debug power-up not acked"); return false; }
  uint32_t idr = 0;
  if (ap_read(0xFC, &idr) != ACK_OK) { Serial.println("ERR AP IDR"); return false; }
  Serial.printf("AP IDR %08X\n", (unsigned)idr);
  if (ap_write(0x00, 0x23000002) != ACK_OK) { Serial.println("ERR CSW"); return false; }  // 32-bit, no auto-inc
  connected = true;
  return true;
}

static int rdp_level() {  // -1 unknown, 0, 1, 2
  uint32_t obr;
  if (mem_read(FLASH_OBR, &obr) != ACK_OK) return -1;
  int rdprt = (obr >> 1) & 3;
  return rdprt == 0 ? 0 : rdprt == 1 ? 1 : 2;
}

static void cmd_info() {
  if (!do_connect()) return;
  uint32_t v;
  if (mem_read(DBGMCU_IDCODE, &v) == ACK_OK)
    Serial.printf("DBGMCU_IDCODE %08X (DEV_ID %03X%s, REV_ID %04X)\n", (unsigned)v, (unsigned)(v & 0xFFF),
                  (v & 0xFFF) == 0x440 ? " = STM32F030x8/F05x" : "", (unsigned)(v >> 16));
  else { Serial.println("ERR DBGMCU read"); clear_errors(); }
  if (mem_read(FLASH_OBR, &v) == ACK_OK) {
    int rdprt = (v >> 1) & 3;
    Serial.printf("FLASH_OBR %08X  RDP level %s  OPTERR=%u\n", (unsigned)v,
                  rdprt == 0 ? "0 (unprotected)" : rdprt == 1 ? "1 (READ PROTECTED: do NOT unlock, it erases flash)" : "2 (locked)",
                  (unsigned)(v & 1));
  } else { Serial.println("ERR FLASH_OBR read"); clear_errors(); }
  Serial.print("OPTBYTES");
  for (int i = 0; i < 4; i++) {
    if (mem_read(OPTBYTES + 4 * i, &v) == ACK_OK) Serial.printf(" %08X", (unsigned)v);
    else { Serial.print(" ERR"); clear_errors(); }
  }
  Serial.println();
  if (mem_read(FLASH_BASE_, &v) == ACK_OK) Serial.printf("FLASH[0] %08X (initial SP)\n", (unsigned)v);
  if (mem_read(FLASH_BASE_ + 4, &v) == ACK_OK) Serial.printf("FLASH[4] %08X (reset vector)\n", (unsigned)v);
  Serial.println("OK info");
}

static void cmd_dump() {
  if (!do_connect()) return;
  int rdp = rdp_level();
  if (rdp != 0) { Serial.printf("ERR refusing dump: RDP level %d\n", rdp); return; }
  // Auto-increment within 1 KB blocks: TAR is re-set at each block.
  if (ap_write(0x00, 0x23000012) != ACK_OK) { Serial.println("ERR CSW"); return; }
  uint32_t crc = 0, line[8];
  int n = 0, errs = 0;
  for (uint32_t blk = 0; blk < FLASH_SIZE_; blk += 1024) {
    yield();  // ESP8266 soft watchdog
    if (ap_write(0x04, FLASH_BASE_ + blk) != ACK_OK) { errs++; break; }
    select_bank(0x0C);
    uint32_t prev;
    if (ap_r(0x0C, &prev) != ACK_OK) { errs++; break; }  // posted: first read is stale
    for (int i = 0; i < 256; i++) {
      uint32_t v;
      int r = (i < 255) ? ap_r(0x0C, &v) : dp_r(0xC, &v);
      if (r != ACK_OK) { errs++; clear_errors(); v = 0xDEADBEEF; }
      line[n++] = v;
      if (n == 8) {
        uint32_t a = FLASH_BASE_ + blk + (i - 7) * 4;
        Serial.printf("D %08X %08X %08X %08X %08X %08X %08X %08X %08X\n", (unsigned)a,
                      (unsigned)line[0], (unsigned)line[1], (unsigned)line[2], (unsigned)line[3],
                      (unsigned)line[4], (unsigned)line[5], (unsigned)line[6], (unsigned)line[7]);
        crc = crc32_le(crc, (const uint8_t *)line, sizeof line);
        n = 0;
      }
    }
  }
  ap_write(0x00, 0x23000002);
  Serial.printf("CRC %08X errors=%d\n", (unsigned)crc, errs);
  Serial.println(errs ? "ERR dump had errors" : "OK dump");
}

void setup() {
  Serial.begin(921600);
  pinMode(PIN_SWCLK, OUTPUT);
  digitalWrite(PIN_SWCLK, HIGH);
  release();
  delay(300);
  Serial.printf("# wayzn swd-dump (read-only) SWCLK=GPIO%d SWDIO=GPIO%d. cmds: i | d\n", PIN_SWCLK, PIN_SWDIO);
}

void loop() {
  static String cmd;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      cmd.trim();
      if (cmd == "i") cmd_info();
      else if (cmd == "d") cmd_dump();
      else if (cmd.length()) Serial.println("# cmds: i (info) | d (dump flash)");
      cmd = "";
    } else cmd += c;
  }
}

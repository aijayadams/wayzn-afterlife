// Parser for the STM32's USART1 output (see docs/STM-FIRMWARE.md, "Status line fields").
// Pure C++, no Arduino: also built on the host by test/host_test.cpp.
//
// The stream is a run of <hex value><key char> fields:
//   status:   <U>U<Z>Z<%>%<H>H<;>;1E0v<=>=<p>p<K>K\r\n
//   move:     <{>{<^>^<(>(<)>)<`>`<#>#          (bucket, position, expected I, limit, new state, current)
//   boot:     @@ then a partial status and a move summary
//   replies:  decimal, terminated by \r\n (`quad` -> -72, `vsense` -> 11606, `c` -> 28)
//   events:   `~` calibration wiped (button event 6), `+` (BT, ignored)
// None of the key chars is a hex digit, so a value is whatever hex digits precede a key.
#pragma once
#include <stdint.h>
#include <string.h>

namespace stm {

enum Event { NONE, STATUS, MOVE, BOOT, REPLY, WIPED };

struct Status {
  int U = -1, Z = 0, dest = 0, H = 0, semi = 0, eq = 0, p = 0, K = 0;
};
struct Move {
  int bucket = 0, pos = 0, expI = 0, limit = 0, state = 0, current = 0;
};

class Parser {
 public:
  Status st;      // last complete status line
  Move mv;        // last complete move summary
  int reply = 0;
  int version = 0;

  Event feed(char c) {
    if (isHex(c) || (c == '-' && n_ == 0)) {
      if (n_ < sizeof buf_ - 1) buf_[n_++] = c;
      return NONE;
    }
    if (c == '@') {
      n_ = 0;
      if (++at_ == 2) { at_ = 0; cur_ = Status(); haveU_ = false; return BOOT; }
      return NONE;
    }
    at_ = 0;
    int v = hexVal();
    bool had = n_ > 0;
    Event ev = NONE;
    switch (c) {
      case 'U': cur_ = Status(); cur_.U = v; haveU_ = had; break;
      case 'Z': cur_.Z = v; break;
      case '%': cur_.dest = v; break;
      case 'H': cur_.H = v; break;
      case ';': cur_.semi = v; break;
      case 'v': version = v; break;
      case '=': cur_.eq = v; break;
      case 'p': cur_.p = v; break;
      case 'K':
        cur_.K = v;
        if (haveU_) { st = cur_; ev = STATUS; }
        haveU_ = false;
        break;
      case '{': mcur_ = Move(); mcur_.bucket = v; break;
      case '^': mcur_.pos = v; break;
      case '(': mcur_.expI = v; break;
      case ')': mcur_.limit = v; break;
      case '`': mcur_.state = v; break;
      case '#': mcur_.current = v; mv = mcur_; ev = MOVE; break;
      case '~': ev = WIPED; break;
      case '\r':
      case '\n':
        if (had && decimal(reply)) ev = REPLY;
        break;
      default: break;  // '+', garbage: drop the pending value
    }
    n_ = 0;
    return ev;
  }

 private:
  char buf_[16];
  unsigned n_ = 0;
  int at_ = 0;
  bool haveU_ = false;
  Status cur_;
  Move mcur_;

  static bool isHex(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); }
  int hexVal() const {
    uint32_t v = 0;
    for (unsigned i = 0; i < n_; i++) {
      char c = buf_[i];
      if (c == '-') continue;
      v = v * 16 + (c <= '9' ? c - '0' : c - 'A' + 10);
    }
    return (int)v;  // 32-bit two's complement on the wire (FFFFFFB8 = -72)
  }
  bool decimal(int &out) const {
    int v = 0;
    bool neg = false;
    for (unsigned i = 0; i < n_; i++) {
      char c = buf_[i];
      if (c == '-' && i == 0) { neg = true; continue; }
      if (c < '0' || c > '9') return false;
      v = v * 10 + (c - '0');
    }
    out = neg ? -v : v;
    return true;
  }
};

// What a control state (U) means for the door. From the jump table in docs/STM-FIRMWARE.md.
enum DoorPhase { PH_UNKNOWN, PH_CLOSED, PH_OPEN, PH_OPENING, PH_CLOSING, PH_REVERSING, PH_CALIBRATING,
                 PH_UNCONFIGURED, PH_FAILED };
inline DoorPhase phase(int U) {
  switch (U) {
    case 0x0E: case 0x1E: return PH_CLOSED;
    case 0x0F: case 0x07: case 0x1D: return PH_OPEN;  // 1D = disengaged rest (fully open)
    case 0x0C: case 0x1A: case 0x1B: return PH_OPENING;
    case 0x0D: case 0x1C: return PH_CLOSING;
    case 0x12: case 0x25: case 0x26: return PH_REVERSING;
    case 0x10: case 0x14: case 0x15: return PH_CALIBRATING;
    case 0x00: case 0x04: return PH_UNCONFIGURED;
    case 0x27: return PH_FAILED;  // open failed near closed
    default: return PH_UNKNOWN;
  }
}
inline const char *phaseName(DoorPhase p) {
  static const char *n[] = {"unknown", "closed", "open", "opening", "closing", "reversing", "calibrating",
                            "unconfigured", "open-failed"};
  return n[p];
}
inline bool moving(DoorPhase p) {
  return p == PH_OPENING || p == PH_CLOSING || p == PH_REVERSING || p == PH_CALIBRATING;
}

}  // namespace stm

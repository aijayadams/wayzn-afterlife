#!/usr/bin/env python3
"""Reassemble a capture.py log into per-channel text lines.

    python3 decode.py captures/<file>.log [--raw]

Prints `<host time> <dev secs> <chan> <line>` for each complete line. A = header
"RX" pin, B = header "TX" pin. Non-printable bytes are shown as \\xNN. With
--raw, CR/LF are kept visible.

I2C lines (`I`, from the esp32dev-i2c build) are passed through in order.
Accelerometer reads (0x0F, 6 bytes from register 0x06) are shown as
`I accel x=.. y=.. z=.. g  pitch=.. roll=..`, assuming ±8 g full scale
(16-bit left-justified, 4096/g; matches 1 g when lying flat). Grid-EYE frame
reads (0x69) are hidden unless --grideye.
"""
import math
import re
import sys

FRAME = re.compile(r"^(\S+) (\d+\.\d+) ([AB]) \[\d+\] ((?:[0-9A-F]{2} )+)")
I2C = re.compile(r"^(\S+) (\d+\.\d+) I (.*)$")

raw = "--raw" in sys.argv
grideye = "--grideye" in sys.argv
accel_reg = None
path = [a for a in sys.argv[1:] if not a.startswith("--")][0]
buf = {"A": bytearray(), "B": bytearray()}
start = {"A": None, "B": None}


def show(b: bytes) -> str:
    out = []
    for c in b:
        if c == 0x0D and not raw:
            continue
        if 0x20 <= c < 0x7F:
            out.append(chr(c))
        else:
            out.append({0x0D: "\\r", 0x0A: "\\n"}.get(c, f"\\x{c:02x}"))
    return "".join(out)


for line in open(path, errors="replace"):
    mi = I2C.match(line)
    if mi:
        host, dev, txn = mi.groups()
        toks = txn.split()
        if toks[:1] == ["W0F"] and len(toks) == 2:
            accel_reg = int(toks[1].rstrip("~"), 16)
            continue
        if toks[:1] == ["R0F"] and accel_reg == 0x06 and len(toks) == 7:
            b = [int(x.rstrip("~"), 16) for x in toks[1:]]
            x, y, z = (int.from_bytes(bytes(b[i:i + 2]), "little", signed=True) / 4096 for i in (0, 2, 4))
            pitch = math.degrees(math.atan2(x, math.hypot(y, z)))
            roll = math.degrees(math.atan2(y, z))
            print(f"{host} {dev:>11} I accel x={x:+.2f} y={y:+.2f} z={z:+.2f} g  pitch={pitch:+6.1f} roll={roll:+6.1f}")
            continue
        if not grideye and "R69" in toks:
            continue
        print(f"{host} {dev:>11} I {txn}")
        continue
    m = FRAME.match(line)
    if not m:
        if line.split(" ", 1)[-1].startswith("#"):
            print(line.rstrip())
        continue
    host, dev, ch, hexs = m.groups()
    for h in hexs.split():
        if start[ch] is None:
            start[ch] = (host, dev)
        b = int(h, 16)
        buf[ch].append(b)
        if b == 0x0A:
            print(f"{start[ch][0]} {start[ch][1]:>11} {ch} {show(bytes(buf[ch][:-1]))}")
            buf[ch].clear()
            start[ch] = None
for ch, b in buf.items():
    if b:
        print(f"{start[ch][0]} {start[ch][1]:>11} {ch} {show(bytes(b))}  <no newline>")

#!/usr/bin/env python3
"""Drive tools/swd-dump on the ESP32: info, then (optionally) dump flash to a .bin.

    python3 dump.py info [--port /dev/cu.usbserial-0001]
    python3 dump.py dump OUT.bin [--port ...]      # reads twice, keeps it only if both match

Write the output to firmware-dumps/ (see AGENTS.md) or outside the repo.
Needs pyserial.
"""
import argparse, pathlib, struct, sys, time, zlib
import serial

ap = argparse.ArgumentParser()
ap.add_argument("cmd", choices=["info", "dump"])
ap.add_argument("out", nargs="?")
ap.add_argument("--port", default="/dev/cu.usbserial-0001")
a = ap.parse_args()

repo = pathlib.Path(__file__).resolve().parents[2]
if a.cmd == "dump":
    if not a.out:
        sys.exit("dump needs OUT.bin")
    out = pathlib.Path(a.out).resolve()
    if repo in out.parents and (repo / "firmware-dumps") not in out.parents:
        sys.exit(f"dumps go in {repo / 'firmware-dumps'} or outside the repo")

s = serial.Serial(a.port, 921600, timeout=1)
time.sleep(0.5)
s.reset_input_buffer()

def run(c, until=("OK ", "ERR "), timeout=60):
    s.write((c + "\n").encode())
    lines, t0 = [], time.time()
    while time.time() - t0 < timeout:
        l = s.readline().decode(errors="replace").rstrip()
        if not l:
            continue
        lines.append(l)
        if not l.startswith("D "):
            print(l)
        if l.startswith(until) and not l.startswith("ERR DBGMCU") and not l.startswith("ERR FLASH_OBR"):
            if l.startswith(("OK info", "OK dump")) or l.startswith("ERR"):
                break
    return lines

if a.cmd == "info":
    run("i")
    sys.exit(0)

def one_dump():
    lines = run("d", timeout=120)
    words, crc_dev = {}, None
    for l in lines:
        p = l.split()
        if p[0] == "D":
            base = int(p[1], 16)
            for i, w in enumerate(p[2:10]):
                words[base + 4 * i] = int(w, 16)
        elif p[0] == "CRC":
            crc_dev = int(p[1], 16)
    if not lines or not lines[-1].startswith("OK dump"):
        sys.exit("dump failed")
    base = 0x08000000
    if sorted(words) != [base + 4 * i for i in range(16384)]:
        sys.exit(f"incomplete: {len(words)} of 16384 words")
    data = b"".join(struct.pack("<I", words[base + 4 * i]) for i in range(16384))
    crc = zlib.crc32(data) & 0xFFFFFFFF
    # esp_rom_crc32_le(0, ...) matches zlib.crc32
    if crc_dev is not None and crc != crc_dev:
        sys.exit(f"CRC mismatch host {crc:08X} device {crc_dev:08X} (serial corruption?)")
    return data, crc

d1, c1 = one_dump()
d2, c2 = one_dump()
if d1 != d2:
    sys.exit(f"two reads differ ({c1:08X} vs {c2:08X}); not saving")
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(d1)
blank = d1.count(b"\xff\xff\xff\xff")
print(f"saved {out} ({len(d1)} B, crc32 {c1:08X}, two reads identical; {blank*4} B of 0xFF words)")

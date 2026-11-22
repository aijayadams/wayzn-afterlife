#!/usr/bin/env python3
"""Log the ESP32 uart-sniffer to captures/ with host timestamps.

    python3 capture.py <event> [--port /dev/cu.usbserial-0001] [--baud 115200] [--seconds N]
                               [--inbox FILE]

Writes ../../captures/YYYY-MM-DD_P1_<event>.log and echoes to the terminal.
Sends `b <baud>` first, so the sniffer is set to the right baud. Ctrl-C to stop.
--inbox FILE: every line appended to FILE is sent to the sniffer (e.g. `> version`
for the esp32dev-host build), and logged as `# inbox: ...`.
Needs pyserial (`pip install pyserial`).
"""
import argparse
import datetime as dt
import pathlib
import sys
import time

import serial

ap = argparse.ArgumentParser()
ap.add_argument("event")
ap.add_argument("--port", default="/dev/cu.usbserial-0001",
                help="serial port, or socket://wayzn-esp.local:23 for the esp32dev-net console")
ap.add_argument("--baud", type=int, default=115200, help="door UART baud")
ap.add_argument("--seconds", type=float, default=0, help="stop after N s (0 = until Ctrl-C)")
ap.add_argument("--inbox", help="forward lines appended to this file to the sniffer")
a = ap.parse_args()
inbox_pos = 0
if a.inbox:
    pathlib.Path(a.inbox).touch()
    inbox_pos = pathlib.Path(a.inbox).stat().st_size  # only lines added from now on

out_dir = pathlib.Path(__file__).resolve().parents[2] / "captures"
out = out_dir / f"{dt.date.today():%Y-%m-%d}_P1_{a.event}.log"

def open_port():
    while True:
        try:
            p = serial.serial_for_url(a.port, 921600, timeout=0.1)  # also socket://host:23
            time.sleep(0.8)  # let a freshly reset ESP32 boot before setting baud
            p.write(f"b {a.baud}\n".encode())
            return p
        except (serial.SerialException, OSError):
            time.sleep(0.5)


def log(f, text):
    line = f"{dt.datetime.now():%H:%M:%S.%f} {text}"
    print(line)
    f.write(line + "\n")
    f.flush()


start = time.time()
with out.open("a") as f:
    f.write(f"# capture {a.event} started {dt.datetime.now().isoformat()} baud={a.baud}\n")
    s = open_port()
    last_rx = time.time()
    buf = b""
    try:
        while not a.seconds or time.time() - start < a.seconds:
            try:
                d = s.read(4096)
                if d:
                    last_rx = time.time()
                elif a.port.startswith("socket://") and time.time() - last_rx > 60:
                    # A rebooted ESP32 (e.g. after OTA) leaves the TCP link silently dead.
                    # Sniffer and brain both print at least every 30 s, so silence means a dead link.
                    raise OSError("no data for 60 s")
                buf += d
            except (serial.SerialException, OSError) as e:
                # USB drops happen when the door motor starts (ground disturbance resets the ESP32)
                log(f, f"# link lost ({e}); reconnecting")
                try:
                    s.close()
                except Exception:
                    pass
                s = open_port()
                last_rx = time.time()
                log(f, "# reconnected (sniffer device time restarts from 0)")
                buf = b""
                continue
            if a.inbox:
                with open(a.inbox, "rb") as ib:
                    ib.seek(inbox_pos)
                    new = ib.read()
                if b"\n" in new:
                    new = new[: new.rindex(b"\n") + 1]
                    inbox_pos += len(new)
                    for cmd in new.decode(errors="replace").splitlines():
                        if cmd.strip():
                            log(f, f"# inbox: {cmd}")
                            s.write((cmd + "\n").encode())
            *lines, buf = buf.split(b"\n")
            for raw in lines:
                log(f, raw.decode(errors="replace").rstrip())
    except KeyboardInterrupt:
        pass
print(f"\nsaved {out}", file=sys.stderr)

#!/usr/bin/env python3
# =============================================================================
# tools/dmesg-pc.py — saves on this PC what a remote EX-OS kernel says
#
#     tools/dmesg-pc.py [host] [file]
#
# Default 192.168.0.183 and /tmp/exos-dmesg.log. Opens one telnet session and
# leaves `dmesg -segui` running on it: the session and the program are already
# in memory, so the log keeps coming when the disk has stopped and nothing can
# be loaded. Ctrl+C ends it. Reconnects after a reset of the machine.
# =============================================================================
import re
import socket
import sys
import time

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240
host = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.183"
dove = sys.argv[2] if len(sys.argv) > 2 else "/tmp/exos-dmesg.log"


def pulisci(s, buf):
    out = bytearray()
    i = 0
    while i < len(buf):
        b = buf[i]
        if b == IAC and i + 2 < len(buf):
            c = buf[i + 1]
            if c in (DO, WILL):
                # ! ONLY THESE: answering WONT and DONT as well never ends, and
                # the two sides keep each other busy at full speed.
                s.sendall(bytes([IAC, WONT if c == DO else DONT, buf[i + 2]]))
            if c in (DO, DONT, WILL, WONT):
                i += 3
                continue
            i += 2
            continue
        out.append(b)
        i += 1
    return re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", out.decode("latin-1"))


def sessione(f):
    s = socket.create_connection((host, 23), timeout=10)
    s.settimeout(1)
    time.sleep(2)
    s.sendall(b"dmesg -segui\r\n")
    while True:
        try:
            d = s.recv(65536)
        except socket.timeout:
            continue
        if not d:
            return
        t = pulisci(s, d)
        print(t, end="", flush=True)
        f.write(f"--- {time.strftime('%H:%M:%S')}\n{t}")
        f.flush()


with open(dove, "a", encoding="utf-8") as f:
    try:
        while True:
            try:
                sessione(f)
            except OSError as e:
                print(f"{time.strftime('%H:%M:%S')} {e}", flush=True)
            time.sleep(3)
    except KeyboardInterrupt:
        pass

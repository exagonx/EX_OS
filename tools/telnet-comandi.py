#!/usr/bin/env python3
# =============================================================================
# tools/telnet-comandi.py — run commands on an EX-OS machine over telnet
#
#     tools/telnet-comandi.py <host> "cmd@seconds" "cmd@seconds" ...
#
# For a telnetd started with -s (no login). Refuses every option
# negotiation, prints what comes back, and ALWAYS ends with `exit`: a session
# left open holds one of the machine's few ptys.
# =============================================================================
import re
import socket
import sys
import time

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240


def leggi(s, attesa):
    s.settimeout(0.3)
    buf = b""
    fine = time.time() + attesa
    while time.time() < fine:
        try:
            d = s.recv(65536)
            if not d:
                break
            buf += d
            fine = max(fine, time.time() + 0.8)
        except socket.timeout:
            pass
    out = bytearray()
    i = 0
    while i < len(buf):
        b = buf[i]
        if b == IAC and i + 1 < len(buf):
            c = buf[i + 1]
            if c in (DO, DONT, WILL, WONT) and i + 2 < len(buf):
                s.sendall(bytes([IAC, WONT if c in (DO, DONT) else DONT, buf[i + 2]]))
                i += 3
                continue
            if c == SB:
                j = buf.find(bytes([IAC, SE]), i)
                i = j + 2 if j >= 0 else len(buf)
                continue
            i += 2
            continue
        out.append(b)
        i += 1
    return re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", out.decode("latin-1"))


def main():
    s = socket.create_connection((sys.argv[1], 23), timeout=10)
    print(leggi(s, 4), end="")
    for c in sys.argv[2:]:
        testo, _, t = c.rpartition("@") if "@" in c else (c, "", "3")
        s.sendall(testo.encode() + b"\r\n")
        print(leggi(s, float(t) if t else 3), end="")
    s.sendall(b"exit\r\n")
    print(leggi(s, 2))
    s.close()


if __name__ == "__main__":
    main()

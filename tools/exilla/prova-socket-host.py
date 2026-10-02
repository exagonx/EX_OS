#!/usr/bin/env python3
# =============================================================================
# tools/exilla/prova-socket-host.py — l'altra parte di prova-socket (@SOCKET-BSD)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     prova-socket-host.py <seriale di QEMU> [solo-eco]
#
# Sull'host: un eco TCP sulla 7801 e un eco UDP sulla 7802 (EX-OS le vede a
# 10.0.0.2, la rete «user» di QEMU); quando la prova scrive sulla seriale che
# e' in ascolto, si collega alla 7700 — che QEMU gira alla 7000 di EX-OS —
# manda «ciao» e vuole «CIAO». Esce con 0 se il servitore di EX-OS ha risposto.
# =============================================================================
import socket
import sys
import threading
import time

SERIALE = sys.argv[1]


def eco_tcp():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", 7801))
    s.listen(4)
    while True:
        c, _ = s.accept()
        threading.Thread(target=servi, args=(c,), daemon=True).start()


def servi(c):
    try:
        while True:
            d = c.recv(65536)
            if not d:
                break
            c.sendall(d)
    except OSError:
        pass
    c.close()


def eco_udp():
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    u.bind(("127.0.0.1", 7802))
    while True:
        d, da = u.recvfrom(2048)
        u.sendto(d.upper(), da)


threading.Thread(target=eco_tcp, daemon=True).start()
threading.Thread(target=eco_udp, daemon=True).start()

# «solo-eco» (tools/exilla/prova-nspr.sh): niente cliente, solo i due eco finche'
# chi ci ha lanciato non ci ferma.
if len(sys.argv) > 2 and sys.argv[2] == "solo-eco":
    while True:
        time.sleep(3600)

# Si aspetta che EX-OS sia in ascolto, poi si bussa.
fine = time.time() + 400
while time.time() < fine:
    try:
        with open(SERIALE, "rb") as f:
            if b"IN ASCOLTO 7000" in f.read():
                break
    except OSError:
        pass
    time.sleep(0.5)
else:
    print("host: EX-OS non si e' mai messo in ascolto")
    sys.exit(1)

time.sleep(1)
for tentativo in range(10):
    try:
        c = socket.create_connection(("127.0.0.1", 7700), timeout=10)
        c.sendall(b"ciao")
        r = b""
        while len(r) < 4:
            d = c.recv(16)
            if not d:
                break
            r += d
        c.close()
        print("host: il servitore di EX-OS ha risposto %r" % r)
        sys.exit(0 if r == b"CIAO" else 1)
    except OSError as e:
        print("host: tentativo %d: %s" % (tentativo, e))
        time.sleep(2)
sys.exit(1)

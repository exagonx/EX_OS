#!/usr/bin/env python3
# =============================================================================
# tools/exilla/prova-nss-host.py — il server TLS dall'altra parte di prova-nss
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     prova-nss-host.py <cartella di lavoro>
#
# Un server HTTPS sulla 7803 dell'host (10.0.0.2 visto da EX-OS), con un
# certificato autofirmato fatto al momento da openssl. A ogni richiesta
# risponde con una riga che dice versione TLS e cifrario negoziati: e' cosi'
# che la prova sa che la stretta di mano l'ha fatta davvero NSS dentro EX-OS.
# =============================================================================
import os
import socket
import ssl
import subprocess
import sys

D = sys.argv[1]
crt, key = os.path.join(D, "host.crt"), os.path.join(D, "host.key")
if not os.path.exists(crt):
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                    "-keyout", key, "-out", crt, "-days", "2", "-subj", "/CN=10.0.0.2"],
                   check=True, capture_output=True)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(crt, key)
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 7803))
s.listen(4)
while True:
    c, _ = s.accept()
    try:
        t = ctx.wrap_socket(c, server_side=True)
        dati = b""
        while b"\r\n\r\n" not in dati:
            pezzo = t.recv(4096)
            if not pezzo:
                break
            dati += pezzo
        riga = "EXILLA TLS OK %s %s" % (t.version(), t.cipher()[0])
        print("host:", riga, flush=True)
        t.sendall(("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n%s\r\n" % riga).encode())
        t.close()
    except (OSError, ssl.SSLError) as e:
        print("host: stretta di mano fallita:", e, flush=True)
        c.close()

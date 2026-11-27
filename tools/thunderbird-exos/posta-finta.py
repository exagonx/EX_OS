#!/usr/bin/env python3
# =============================================================================
# tools/thunderbird-exos/posta-finta.py - a mail server for the Thunderbird
# test (tools/thunderbird-exos/prova.sh), on the host
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     posta-finta.py <cartella> [porta_pop3 porta_smtp]     11110 10025
#
# POP3 with one letter in the box (subject PROVA-EXOS-ARRIVATA), user
# "prova", password "prova"; SMTP that takes any letter without login and
# writes it in <cartella>/inviata-N.eml. Every command it gets goes to
# <cartella>/posta.log with the time, so the test reads what Thunderbird
# really did (USER, PASS, RETR, DATA...). Plain text, no TLS: what is
# tested is EX-OS's sockets and Thunderbird's protocols, not the certificates.
# QEMU's user network takes the guest's 10.0.0.2:<porta> to 127.0.0.1 here.
# =============================================================================
import os
import socketserver
import sys
import threading
import time

CARTELLA = sys.argv[1]
POP3 = int(sys.argv[2]) if len(sys.argv) > 2 else 11110
SMTP = int(sys.argv[3]) if len(sys.argv) > 3 else 10025
os.makedirs(CARTELLA, exist_ok=True)
LOG = open(os.path.join(CARTELLA, "posta.log"), "a", buffering=1)
LUCCHETTO = threading.Lock()
T0 = time.time()

LETTERA = (
    "From: Posta Finta <finta@exos.prova>\r\n"
    "To: Prova EX-OS <prova@exos.prova>\r\n"
    "Subject: PROVA-EXOS-ARRIVATA\r\n"
    "Date: Fri, 27 Nov 2026 10:00:00 +0100\r\n"
    "Message-ID: <prova-1@exos.prova>\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "\r\n"
    "Ciao da EX-OS: se la leggi, POP3 funziona.\r\n"
).encode()


def scrivi(chi, riga):
    with LUCCHETTO:
        LOG.write("%7.1f %s %s\n" % (time.time() - T0, chi, riga))


class Pop3(socketserver.StreamRequestHandler):
    def manda(self, s):
        self.wfile.write(s.encode() + b"\r\n")

    def handle(self):
        scrivi("POP3", "connessione da %s" % (self.client_address[0],))
        self.manda("+OK posta finta pronta")
        dentro = False
        while True:
            r = self.rfile.readline()
            if not r:
                break
            riga = r.decode(errors="replace").rstrip("\r\n")
            cmd, _, arg = riga.partition(" ")
            cmd = cmd.upper()
            scrivi("POP3", "PASS ****" if cmd == "PASS" else riga)
            if cmd == "CAPA":
                self.wfile.write(b"+OK\r\nUSER\r\nUIDL\r\nTOP\r\n.\r\n")
            elif cmd == "USER":
                self.manda("+OK" if arg == "prova" else "-ERR chi sei")
            elif cmd == "PASS":
                dentro = arg == "prova"
                scrivi("POP3", "password giusta" if dentro else "password sbagliata")
                self.manda("+OK dentro" if dentro else "-ERR password")
            elif not dentro and cmd not in ("QUIT", "AUTH"):
                self.manda("-ERR prima USER e PASS")
            elif cmd == "AUTH":
                self.manda("-ERR solo USER/PASS")
            elif cmd == "STAT":
                self.manda("+OK 1 %d" % len(LETTERA))
            elif cmd == "LIST":
                if arg:
                    self.manda("+OK 1 %d" % len(LETTERA))
                else:
                    self.wfile.write(b"+OK\r\n1 %d\r\n.\r\n" % len(LETTERA))
            elif cmd == "UIDL":
                if arg:
                    self.manda("+OK 1 prova-1")
                else:
                    self.wfile.write(b"+OK\r\n1 prova-1\r\n.\r\n")
            elif cmd in ("RETR", "TOP"):
                self.wfile.write(b"+OK\r\n" + LETTERA + b".\r\n")
            elif cmd in ("DELE", "NOOP", "RSET"):
                self.manda("+OK")
            elif cmd == "QUIT":
                self.manda("+OK ciao")
                break
            else:
                self.manda("-ERR non so")


class Smtp(socketserver.StreamRequestHandler):
    conta = 0

    def manda(self, s):
        self.wfile.write(s.encode() + b"\r\n")

    def handle(self):
        scrivi("SMTP", "connessione da %s" % (self.client_address[0],))
        self.manda("220 posta finta")
        while True:
            r = self.rfile.readline()
            if not r:
                break
            riga = r.decode(errors="replace").rstrip("\r\n")
            cmd = riga.split(" ", 1)[0].upper()
            scrivi("SMTP", riga)
            if cmd == "EHLO":
                self.wfile.write(b"250-posta finta\r\n250-8BITMIME\r\n250 SIZE 10485760\r\n")
            elif cmd == "HELO":
                self.manda("250 posta finta")
            elif cmd in ("MAIL", "RCPT", "RSET", "NOOP"):
                self.manda("250 OK")
            elif cmd == "DATA":
                self.manda("354 avanti")
                corpo = []
                while True:
                    d = self.rfile.readline()
                    if not d or d in (b".\r\n", b".\n"):
                        break
                    corpo.append(d[1:] if d.startswith(b"..") else d)
                with LUCCHETTO:
                    Smtp.conta += 1
                    n = Smtp.conta
                with open(os.path.join(CARTELLA, "inviata-%d.eml" % n), "wb") as f:
                    f.write(b"".join(corpo))
                scrivi("SMTP", "lettera %d ricevuta, %d byte" % (n, sum(map(len, corpo))))
                self.manda("250 presa")
            elif cmd == "QUIT":
                self.manda("221 ciao")
                break
            else:
                self.manda("502 non so")


class Servitore(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


for porta, classe in ((POP3, Pop3), (SMTP, Smtp)):
    s = Servitore(("127.0.0.1", porta), classe)
    threading.Thread(target=s.serve_forever, daemon=True).start()
scrivi("----", "pronta: POP3 %d, SMTP %d" % (POP3, SMTP))
while True:
    time.sleep(3600)

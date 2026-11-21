#!/usr/bin/env python3
"""tools/riga_lunga_prova.py PORT SERIAL_LOG: long lines over telnet (kernel 0.353)

Part of tools/prova_riga_lunga.sh. EX-OS runs telnetd -s with a test disk on
/a; each line here writes a file there, and the shell script reads the files
from the disk image after the machine is off: byte for byte, not what the
screen shows (the echo of a long paste may lose bytes, the line may not).

  echo <N x> FINE > /a/tN.txt   for N = 300, 4000, 20000, 32000
  echo <40000 x> > /a/t40000.txt  longer than a line: cut, the shell lives
  echo VIVA                      the session still answers
"""
import socket
import sys
import time

porta = int(sys.argv[1])
seriale = sys.argv[2]


def aspetta(testo, secondi):
    fine = time.time() + secondi
    while time.time() < fine:
        try:
            if testo in open(seriale, "rb").read():
                return True
        except OSError:
            pass
        time.sleep(1)
    return False


def leggi(s, secondi):
    dati = b""
    fine = time.time() + secondi
    while time.time() < fine:
        try:
            d = s.recv(65536)
            if not d:
                break
            dati += d
        except socket.timeout:
            pass
    return dati


def al_prompt(s, secondi):
    dati = b""
    fine = time.time() + secondi
    while time.time() < fine:
        try:
            d = s.recv(65536)
            if not d:
                return dati, False
            dati += d
            # the prompt is the last line, and only it says ex-os: the
            # echo of a line ends with "> /a/tN.txt" too
            ultima = dati.rsplit(b"\n", 1)[-1]
            if b"ex-os" in ultima and ultima.rstrip().endswith(b">"):
                return dati, True
        except socket.timeout:
            pass
    return dati, False


if not aspetta(b"telnetd: in ascolto", 240):
    print("  [NO] telnetd non e' partito")
    sys.exit(1)
s = socket.create_connection(("127.0.0.1", porta), 10)
s.settimeout(1)
_, ok = al_prompt(s, 30)
if not ok:
    print("  [NO] nessun prompt da telnet")
    sys.exit(1)

esito = 0
for n in (300, 4000, 20000, 32000):
    riga = b"echo " + b"x" * n + b" FINE > /a/t%d.txt\r\n" % n
    s.sendall(riga)
    _, ok = al_prompt(s, 120)
    print("  [%s] riga di %d byte mandata, prompt %s" % ("OK" if ok else "NO", len(riga), "tornato" if ok else "non tornato"))
    if not ok:
        esito = 1
s.sendall(b"echo " + b"x" * 40000 + b" > /a/t40000.txt\r\n")
al_prompt(s, 120)
s.sendall(b"echo VIVA\r\n")
d, ok = al_prompt(s, 30)
if b"VIVA\r\n" in d and ok:
    print("  [OK] dopo una riga di 40000 byte la sessione risponde")
else:
    print("  [NO] dopo una riga di 40000 byte la sessione non risponde")
    esito = 1
s.sendall(b"sync\r\n")
al_prompt(s, 30)
s.close()
sys.exit(esito)

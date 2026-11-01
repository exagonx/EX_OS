#!/usr/bin/env python3
# =============================================================================
# tools/prova_carico_pc.py — disk load on a remote EX-OS machine, with guardia
#
#     tools/prova_carico_pc.py [-due] [host] [file] [log]
#
# ! ONE TELNET SESSION DOES EVERYTHING: telnetd serves one connection at a
# time, so guardia-pc.py and dmesg-pc.py cannot run beside a test. Here
# `dmesg -segui` stays in the background of the session, `guardia 120` is sent
# every 30 seconds and a copy of the file runs with smpprova. If the disk stops
# `guardia` can no longer be loaded and the machine resets by itself.
# -due turns the second processor on first (smpprova -accendi).
# =============================================================================
import re
import socket
import sys
import time

IAC, DONT, DO, WONT, WILL = 255, 254, 253, 252, 251
arg = sys.argv[1:]
due = "-due" in arg
arg = [a for a in arg if a != "-due"]
host = arg[0] if len(arg) > 0 else "192.168.0.183"
src = arg[1] if len(arg) > 1 else "/exwin/app/exilla/firefox"
log = open(arg[2] if len(arg) > 2 else "/tmp/exos-carico.log", "a", encoding="utf-8")
LIMITE = 600

s = socket.create_connection((host, 23), timeout=10)
s.settimeout(0.5)
t0 = time.time()
tutto = ""


def leggi(sec):
    global tutto
    fine = time.time() + sec
    nuovo = ""
    while time.time() < fine:
        try:
            buf = s.recv(65536)
        except socket.timeout:
            continue
        if not buf:
            raise OSError("connessione chiusa")
        out = bytearray()
        i = 0
        while i < len(buf):
            if buf[i] == IAC and i + 2 < len(buf):
                c = buf[i + 1]
                if c == DO:
                    s.sendall(bytes([IAC, WONT, buf[i + 2]]))
                elif c == WILL:
                    s.sendall(bytes([IAC, DONT, buf[i + 2]]))
                i += 3
                continue
            out.append(buf[i])
            i += 1
        t = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", out.decode("latin-1"))
        t = t.replace("\r", "")
        nuovo += t
        log.write(t)
        log.flush()
        print(t, end="", flush=True)
    tutto += nuovo
    return nuovo


def manda(c, sec=2):
    log.write(f"\n=== {int(time.time() - t0)} s: {c}\n")
    s.sendall(c.encode() + b"\r\n")
    return leggi(sec)


try:
    leggi(3)
    manda("dmesg -segui &", 3)
    manda("guardia 120")
    if due:
        manda("smpprova -accendi", 5)
    manda("smpprova 2 2" if due else "smpprova 1 2", 7)
    manda(f"cp {src} /root/pa &", 1)
    manda("smpprova 2 5 &" if due else "smpprova 1 5 &", 1)
    while time.time() - t0 < LIMITE:
        leggi(30)
        manda("guardia 120")
        r = manda("ps", 4)
        if "/bin/cp" not in r and " cp" not in r and "PID" in r:
            break
    else:
        print("\nLIMITE: la copia non finisce")
    print(f"\nCOPIA FINITA a {int(time.time() - t0)} s")
    manda("guardia 120")
    a = manda(f"sha256sum {src}", 50)
    manda("guardia 120")
    b = manda("sha256sum /root/pa", 50)
    ia = re.findall(r"^[0-9a-f]{64}", a, re.M)
    ib = re.findall(r"^[0-9a-f]{64}", b, re.M)
    print("IMPRONTE UGUALI" if ia and ia == ib else f"IMPRONTE DIVERSE {ia} {ib}")
    manda("rm /root/pa", 5)
    manda("guardia 0")
    r = manda("ps", 4)
    for m in re.findall(r"^(\d+)\s.*dmesg", r, re.M):
        manda(f"kill {m}")
    manda("exit", 2)
except OSError as e:
    print(f"\nSESSIONE CADUTA a {int(time.time() - t0)} s: {e}")

#!/usr/bin/env python3
"""Verifica halt/poweroff/reboot: guarda se QEMU esce davvero e in quanto tempo."""
import os
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qemu_drive as qd

CMD = sys.argv[1] if len(sys.argv) > 1 else "poweroff"
SER = "/tmp/exos/pwr_serial.txt"
MON = "/tmp/exos/pwr_mon.sock"

for p in (MON, SER):
    if os.path.exists(p):
        os.remove(p)

qemu = subprocess.Popen([
    "qemu-system-i386",
    "-drive", "file=dist/floppy.img,format=raw,if=floppy",
    "-m", "32M", "-boot", "a", "-display", "none",
    "-serial", "file:%s" % SER,
    "-monitor", "unix:%s,server,nowait" % MON,
    "-no-reboot",
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

try:
    qd.MON = MON
    mon = qd.Monitor(MON)

    deadline = time.time() + 90
    while time.time() < deadline:
        if os.path.exists(SER):
            with open(SER, errors="replace") as fh:
                if "ex-os" in fh.read():
                    break
        time.sleep(0.5)
    time.sleep(1.0)

    print("--- invio comando: %s" % CMD)
    t0 = time.time()
    mon.typeline(CMD)

    # Attende l'uscita del processo QEMU: e' la prova che la macchina
    # si e' davvero spenta (o resettata, con -no-reboot).
    rc = None
    while time.time() - t0 < 30:
        rc = qemu.poll()
        if rc is not None:
            break
        time.sleep(0.2)

    dt = time.time() - t0
    if rc is None:
        print("QEMU ANCORA VIVO dopo %.1fs (nessuno spegnimento)" % dt)
    else:
        print("QEMU USCITO dopo %.1fs (exit=%s)" % (dt, rc))

    with open(SER, errors="replace") as fh:
        txt = fh.read()
    idx = txt.rfind("ex-os")
    print("=== seriale (coda) ===")
    print(txt[idx:] if idx >= 0 else txt[-1500:])
finally:
    if qemu.poll() is None:
        qemu.kill()
    qemu.wait()

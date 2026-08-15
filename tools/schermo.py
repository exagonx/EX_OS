#!/usr/bin/env python3
"""Guarda lo SCHERMO invece della seriale.

Serve per i programmi a schermo intero: mettono la console in raw, e
tty_raw(1) spegne lo specchio seriale — quindi sulla seriale non c'e'
niente da leggere proprio nel caso in cui si vuole verificare
l'impaginazione. Qui si chiede a QEMU un `screendump` e si converte il
PPM in testo confrontando ogni cella 8x16 con i glifi che si vedono.

Si lancia dalla radice del progetto, come qemu_drive.py:

    python3 tools/schermo.py "help@4" "key:pgdn@2" "q@2"

Stessa sintassi di qemu_drive.py — "comando@attesa", e `key:nome1,nome2`
per i tasti fisici. Dopo ogni passo salva un PPM numerato in /tmp/exos/
e ne stampa la resa in caratteri.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qemu_drive as qd  # noqa: E402

# I PPM stanno fuori dal progetto: sono grandi (860 KB l'uno) e non c'e'
# ragione che finiscano sotto git a ogni prova.
SCRATCH = "/tmp/exos"


def ppm_a_testo(path):
    """Il PPM 720x400 di QEMU in modo testo -> 80x25 caratteri.

    Non si riconoscono i glifi: si guarda solo se una cella ha pixel
    accesi e con quale colore, che basta per giudicare impaginazione,
    riempimenti e barre in video inverso — cioe' quello che si vuole
    controllare qui.
    """
    with open(path, "rb") as fh:
        dati = fh.read()

    # Intestazione PPM: P6\n<w> <h>\n255\n
    campi = []
    i = 0
    while len(campi) < 4:
        while dati[i:i+1].isspace():
            i += 1
        if dati[i:i+1] == b"#":
            while dati[i:i+1] != b"\n":
                i += 1
            continue
        j = i
        while not dati[j:j+1].isspace():
            j += 1
        campi.append(dati[i:j])
        i = j
    i += 1
    w, h = int(campi[1]), int(campi[2])
    px = dati[i:]

    cw, ch = w // 80, h // 25
    righe = []
    for r in range(25):
        riga = ""
        for c in range(80):
            acceso = 0
            sfondo = 0
            for y in range(r * ch, (r + 1) * ch):
                for x in range(c * cw, (c + 1) * cw):
                    o = (y * w + x) * 3
                    v = px[o] + px[o+1] + px[o+2]
                    if v > 200:
                        acceso += 1
            tot = cw * ch
            sfondo = acceso > tot * 0.55       # piu' chiaro che scuro
            if sfondo:
                riga += "#"                    # cella in video inverso
            elif acceso > 0:
                riga += "x"                    # c'e' un glifo
            else:
                riga += " "
        righe.append(riga.rstrip())
    return righe


def main():
    os.makedirs("/tmp/exos", exist_ok=True)
    for p in (qd.MON, qd.SER):
        if os.path.exists(p):
            os.remove(p)

    qemu = subprocess.Popen([
        "qemu-system-i386",
        "-drive", "file=dist/floppy.img,format=raw,if=floppy",
        "-m", "32M", "-boot", "a", "-display", "none",
        "-serial", "file:%s" % qd.SER,
        "-monitor", "unix:%s,server,nowait" % qd.MON,
        "-no-reboot",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    try:
        mon = qd.Monitor(qd.MON)
        scadenza = time.time() + 90
        while time.time() < scadenza:
            if os.path.exists(qd.SER):
                with open(qd.SER, "r", errors="replace") as fh:
                    if "ex-os" in fh.read():
                        break
            time.sleep(0.5)
        time.sleep(1.0)

        for n, arg in enumerate(sys.argv[1:]):
            cmd, _, delay = arg.partition("@")
            attesa = float(delay) if delay else 3.0
            if cmd.startswith("key:"):
                mon.tasti(cmd[4:].split(","))
            else:
                mon.typeline(cmd)
            time.sleep(attesa)

            ppm = os.path.join(SCRATCH, "schermo%02d.ppm" % n)
            mon.cmd("screendump %s" % ppm, settle=1.0)
            print("=== dopo %r ===" % cmd)
            if os.path.exists(ppm):
                for r in ppm_a_testo(ppm):
                    print("|" + r)
            print()
    finally:
        qemu.kill()
        qemu.wait()


if __name__ == "__main__":
    main()

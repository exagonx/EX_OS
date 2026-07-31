#!/usr/bin/env python3
"""Pilota EX-OS in QEMU: manda comandi via monitor 'sendkey' e raccoglie
lo stato hardware. Vedi HANDOFF.md - sezione strumentazione di debug."""

import os
import socket
import subprocess
import sys
import time

IMG = os.environ.get("EXOS_IMG", "dist/floppy.img")
MON = "/tmp/exos/mon.sock"
SER = "/tmp/exos/serial.txt"

KEYMAP = {
    " ": "spc",
    "\n": "ret",
    "\b": "backspace",
    "\x1b": "esc",
    "/": "slash",
    ".": "dot",
    "-": "minus",
    "_": "shift-minus",
    ",": "comma",
    ";": "semicolon",
    ":": "shift-semicolon",
    "=": "equal",
    "'": "apostrophe",
    # Caratteri jolly: servono per provare /bin/delete. '*' ha un tasto
    # proprio sul tastierino (kp_multiply); '?' e' shift+slash.
    "*": "kp_multiply",
    "?": "shift-slash",
}


def keyname(ch):
    if ch in KEYMAP:
        return KEYMAP[ch]
    if ch.isdigit():
        return ch
    if "a" <= ch <= "z":
        return ch
    if "A" <= ch <= "Z":
        return "shift-" + ch.lower()
    raise ValueError("carattere non mappato: %r" % ch)


class Monitor:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for _ in range(100):
            try:
                self.sock.connect(path)
                break
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(0.1)
        else:
            raise RuntimeError("monitor non raggiungibile: " + path)
        self.sock.settimeout(2.0)
        self.drain()

    def drain(self):
        out = b""
        try:
            while True:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                out += chunk
        except socket.timeout:
            pass
        return out.decode("utf-8", "replace")

    def cmd(self, line, settle=0.3):
        self.sock.sendall((line + "\n").encode())
        time.sleep(settle)
        return self.drain()

    def typeline(self, text):
        """Manda 'text' carattere per carattere, poi Invio."""
        for ch in text:
            self.sock.sendall(("sendkey %s\n" % keyname(ch)).encode())
            time.sleep(0.05)
        self.sock.sendall(b"sendkey ret\n")
        time.sleep(0.05)
        self.drain()


def main():
    os.makedirs("/tmp/exos", exist_ok=True)
    for p in (MON, SER):
        if os.path.exists(p):
            os.remove(p)

    # EXOS_NO_FLOPPY=1 avvia SENZA floppy: serve a provare un sistema
    # installato su disco, dove il floppy non deve esserci affatto —
    # lasciarlo attaccato proverebbe una cosa diversa da quella voluta.
    senza_floppy = os.environ.get("EXOS_NO_FLOPPY")
    supporto = ([] if senza_floppy
                else ["-drive", "file=%s,format=raw,if=floppy" % IMG])

    qemu = subprocess.Popen([
        "qemu-system-i386",
    ] + supporto + [
        "-m", "32M", "-boot", "c" if senza_floppy else "a",
        "-display", "none",
        "-serial", "file:%s" % SER,
        "-monitor", "unix:%s,server,nowait" % MON,
        "-no-reboot",
    ] + (os.environ.get("EXOS_QEMU_EXTRA", "").split() if os.environ.get("EXOS_QEMU_EXTRA") else []), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    try:
        mon = Monitor(MON)

        # Attendi che la shell sia arrivata al prompt: il marker e' la riga
        # di dump dello scheduler stampata subito dopo sched_start.
        # Due marker alternativi: "sblocco IRQ0" e' un klog INFO e sparisce
        # con verboseboot=0, quindi si accetta anche il prompt della shell,
        # che c'e' in entrambe le modalita'.
        deadline = time.time() + 90
        while time.time() < deadline:
            if os.path.exists(SER):
                with open(SER, "r", errors="replace") as fh:
                    txt = fh.read()
                if "sblocco IRQ0" in txt or "ex-os" in txt:
                    break
            time.sleep(0.5)
        else:
            print("TIMEOUT: la shell non ha raggiunto il prompt")
            return 1

        time.sleep(1.0)
        mark = os.path.getsize(SER)

        # Sintassi argomenti: "comando" oppure "comando@attesa_secondi"
        # (attesa 0 = type-ahead: manda il comando successivo senza
        # aspettare che il precedente finisca).
        for arg in sys.argv[1:] or ["help"]:
            cmd, _, delay = arg.partition("@")
            wait = float(delay) if delay else 4.0
            print("--- invio comando: %r (attesa %.1fs)" % (cmd, wait))
            mon.typeline(cmd)
            time.sleep(wait)

        print("=== info pic ===")
        print(mon.cmd("info pic", settle=0.6))
        print("=== info registers ===")
        print(mon.cmd("info registers", settle=0.6))

        with open(SER, "r", errors="replace") as fh:
            fh.seek(mark)
            print("=== seriale dal prompt in poi ===")
            print(fh.read())
    finally:
        qemu.kill()
        qemu.wait()
    return 0


if __name__ == "__main__":
    sys.exit(main())

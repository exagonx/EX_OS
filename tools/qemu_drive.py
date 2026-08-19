#!/usr/bin/env python3
"""Pilota EX-OS in QEMU: manda comandi via monitor 'sendkey' e raccoglie
lo stato hardware. Vedi HANDOFF.md - sezione strumentazione di debug."""

import os
import socket
import subprocess
import sys
import time

IMG = os.environ.get("EXOS_IMG", "dist/floppy.img")

# EXOS_ISTANZA distingue piu' macchine avviate insieme. Serve a provare la
# rete: con una sola macchina si puo' vedere solo la meta' del lavoro dello
# stack — quella in cui siamo noi a chiedere. Rispondere a un ARP o a un
# ping in arrivo richiede qualcuno che li mandi, e lo slirp di QEMU non lo
# fa mai. Due EX-OS su una rete a socket se li mandano a vicenda.
ISTANZA = os.environ.get("EXOS_ISTANZA", "")
MON = "/tmp/exos/mon%s.sock" % ISTANZA
SER = "/tmp/exos/serial%s.txt" % ISTANZA

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
    # '&' serve a lanciare in background, cioe' a provare qualunque
    # servizio: senza, un driver che gira come server non si puo'
    # nemmeno avviare da qui.
    "&": "shift-7",
    # I tasti di movimento: servono a provare la cronologia della shell.
    "\x01": "up",
    "\x02": "down",
    "\x03": "left",
    "\x04": "right",
    # Il resto della punteggiatura ASCII. Manca ancora qualcosa? Si
    # aggiunge QUI: un carattere non mappato fa cadere questo script con
    # un'eccezione a meta' prova, e la prova sembra fallita quando invece
    # non e' mai partita. E' successo con '~', cercando l'alias 8.3 di un
    # nome lungo — il sistema rispondeva benissimo, era il pilota a non
    # saper digitare.
    "`": "grave_accent",
    "~": "shift-grave_accent",
    "!": "shift-1",
    "@": "shift-2",
    "#": "shift-3",
    "$": "shift-4",
    "%": "shift-5",
    "^": "shift-6",
    "(": "shift-9",
    ")": "shift-0",
    "+": "shift-equal",
    "[": "bracket_left",
    "]": "bracket_right",
    "{": "shift-bracket_left",
    "}": "shift-bracket_right",
    "\\": "backslash",
    "|": "shift-backslash",
    "<": "shift-comma",
    ">": "shift-dot",
    '"': "shift-apostrophe",
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

    def rapidi(self, righe, passo=0.10):
        """Piu' comandi al monitor uno dietro l'altro, VELOCI DAVVERO.

        ! NON SI DRENA FRA UNO E L'ALTRO, ed e' tutto il punto. drain()
        legge finche' il socket non va in timeout, e il timeout e' DUE
        SECONDI: ogni cmd() costa percio' due secondi buoni, qualunque
        `settle` gli si dia. Per un movimento non importa; per un DOPPIO
        CLIC e' fatale — le due pressioni arrivano alla macchina a quattro
        secondi l'una dall'altra, e nessuna soglia sensata le riconoscera'
        mai come un doppio clic. Costato un pomeriggio: dai pixel si vedeva
        solo «il doppio clic non funziona», e la colpa era di questo
        attrezzo, non del sistema provato.
        """
        for r in righe:
            self.sock.sendall((r + "\n").encode())
            time.sleep(passo)
        self.drain()

    def tasti(self, nomi):
        """Manda TASTI FISICI per nome QEMU, senza passare dalla KEYMAP.

        ! SERVE A PROVARE LE DISPOSIZIONI, e non si puo' fare altrimenti.
        typeline() traduce un CARATTERE nel tasto che lo produce su una
        tastiera americana: e' esattamente il presupposto che una prova
        sulle disposizioni deve mettere in discussione. Qui si nomina il
        tasto fisico — `semicolon`, `altgr-bracket_left` — e si guarda
        quale carattere ne esce, che e' la domanda vera.
        """
        for n in nomi:
            self.sock.sendall(("sendkey %s\n" % n).encode())
            time.sleep(0.08)
        time.sleep(0.05)
        self.drain()

    def typeline(self, text, invio=True):
        """Manda 'text' carattere per carattere, poi Invio.

        ! `invio=False` serve a provare la MODIFICA della riga: frecce,
        Home, Backspace. Senza, ogni gruppo di tasti finisce con un Invio
        che manda in esecuzione la riga a meta', e le frecce del gruppo
        successivo agiscono su una riga vuota — cioe' non si prova niente.
        """
        for ch in text:
            self.sock.sendall(("sendkey %s\n" % keyname(ch)).encode())
            time.sleep(0.05)
        if invio:
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

    # EXOS_CDROM=<iso> avvia DAL CD (El Torito, emulazione floppy). Serve da
    # quando il CD di EX-OS e' avviabile: senza, l'unico modo di provarlo
    # era uno script a parte, e uno script a parte non ha la mappa dei
    # tasti — le maiuscole vanno mandate come "shift-x" e chi lo rifa' a
    # mano se ne dimentica, con il risultato che "-L" arriva come "-".
    cdrom = os.environ.get("EXOS_CDROM")

    # EXOS_NET_SOCKET="listen:PORTA" oppure "connect:PORTA" mette la scheda
    # su una LAN virtuale condivisa con un'altra istanza di QEMU, senza NAT
    # e senza gateway: due macchine e un cavo, che e' esattamente cio' che
    # serve per provare le risposte.
    peer = os.environ.get("EXOS_NET_SOCKET")
    rete = []
    if peer:
        modo, _, porta = peer.partition(":")
        # ! IL MAC VA DATO DIVERSO A OGNI ISTANZA. QEMU ne assegna uno
        # predefinito uguale per tutti, e due host con lo stesso indirizzo
        # sulla stessa LAN fanno passare un test ARP che non prova niente:
        # qualunque risposta sembra quella giusta. EXOS_MAC lo distingue.
        mac = os.environ.get("EXOS_MAC")
        dev = "ne2k_pci,netdev=n0" + (",mac=" + mac if mac else "")
        rete = ["-netdev", "socket,id=n0,%s=:%s" % (
                    "listen" if modo == "listen" else "connect", porta),
                "-device", dev]

    qemu = subprocess.Popen([
        "qemu-system-i386",
    ] + supporto + [
        # EXOS_RAM: 32 MB bastano a tutto il sistema, ma non a caricare cc1
        # — che da solo pesa 40 MB. Il caricamento ELF e' a richiesta,
        # quindi non serve che ci stia tutto insieme, ma serve spazio per
        # le pagine che tocca davvero mentre lavora.
        "-m", os.environ.get("EXOS_RAM", "32M"),
        "-boot", "d" if cdrom else ("c" if senza_floppy else "a"),
        "-display", "none",
        "-serial", "file:%s" % SER,
        "-monitor", "unix:%s,server,nowait" % MON,
        "-no-reboot",
    ] + rete + (["-cdrom", cdrom] if cdrom else [])
      + (os.environ.get("EXOS_QEMU_EXTRA", "").split() if os.environ.get("EXOS_QEMU_EXTRA") else []), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    try:
        mon = Monitor(MON)

        # Attendi che la shell sia arrivata al prompt: il marker e' la riga
        # di dump dello scheduler stampata subito dopo sched_start.
        # Due marker alternativi: "sblocco IRQ0" e' un klog INFO e sparisce
        # con verboseboot=0, quindi si accetta anche il prompt della shell,
        # che c'e' in entrambe le modalita'.
        marca = os.environ.get("EXOS_MARCA", "")
        deadline = time.time() + 90
        while time.time() < deadline:
            if os.path.exists(SER):
                with open(SER, "r", errors="replace") as fh:
                    txt = fh.read()
                # ! IL MARCATORE SI PUO' SCEGLIERE, e serve davvero: un
                # sistema INSTALLATO non arriva al prompt della shell, si
                # ferma su «nome utente:» finche' non si e' creato il primo
                # utente. Con i soli marcatori d'avvio questa funzione
                # rinuncia dopo novanta secondi e non manda MAI i comandi —
                # e il sintomo e' «la macchina non risponde», che non
                # somiglia a «sto aspettando la riga sbagliata».
                #
                #     EXOS_MARCA="nome utente:" python3 tools/qemu_drive.py ...
                if marca:
                    if marca in txt:
                        break
                elif "sblocco IRQ0" in txt or "ex-os" in txt:
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
        # Sintassi: "comando@secondi". Una 'n' davanti ai secondi
        # ("cmd@n2") manda i tasti SENZA Invio — vedi typeline().
        for arg in sys.argv[1:] or ["help"]:
            cmd, _, delay = arg.partition("@")
            invio = not delay.startswith("n")
            if not invio:
                delay = delay[1:]
            wait = float(delay) if delay else 4.0
            print("--- invio %r (attesa %.1fs%s)"
                  % (cmd, wait, "" if invio else ", senza Invio"))
            # `foto:/percorso.ppm` non manda niente alla macchina: chiede a
            # QEMU un'istantanea dello schermo. Serve alle prove sul video,
            # dove cio' che conta non e' il testo sulla seriale ma la
            # RISOLUZIONE — 720x400 in modo testo, 320x200 in modo 13h — e
            # quella e' scritta nell'intestazione del PPM. E' un numero,
            # non un'impressione.
            # `mon:<comando>` manda un comando al monitor di QEMU invece che
            # alla shell. Serve a INIETTARE input dall'esterno — `mouse_move
            # 10 5`, `mouse_button 1` — cioe' a provare un driver di input
            # con numeri decisi qui e verificabili la' dentro.
            # ! PIU' COMANDI IN UN `mon:` SOLO, separati da «;», e servono
            # davvero: un DOPPIO CLIC e' due pressioni entro qualche decimo
            # di secondo, e un `mon:` per conto suo ne costa PIU' DI DUE —
            # non per il `settle`, ma perche' drain() aspetta il timeout del
            # socket. Dentro un argomento solo si passa da rapidi(), che
            # scrive e basta: un decimo di secondo l'uno, che e' quanto
            # separa davvero due clic di una mano.
            if cmd.startswith("mon:"):
                pezzi = [z.strip() for z in cmd[4:].split(";")]
                if len(pezzi) == 1:
                    mon.cmd(pezzi[0], settle=0.25)
                else:
                    mon.rapidi(pezzi)
            elif cmd.startswith("foto:"):
                mon.cmd("screendump " + cmd[5:], settle=0.8)
            elif cmd.startswith("key:"):
                mon.tasti(cmd[4:].split(","))
            else:
                mon.typeline(cmd, invio)
            time.sleep(wait)

        print("=== info pic ===")
        print(mon.cmd("info pic", settle=0.6))
        print("=== info registers ===")
        print(mon.cmd("info registers", settle=0.6))

        # EXOS_ATTESA_FINALE tiene viva la macchina dopo l'ultimo comando:
        # serve all'istanza che fa da CONTROPARTE, che deve restare accesa
        # mentre l'altra la interroga.
        finale = float(os.environ.get("EXOS_ATTESA_FINALE", "0"))
        if finale > 0:
            time.sleep(finale)

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

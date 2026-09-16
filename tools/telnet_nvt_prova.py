#!/usr/bin/env python3
"""tools/telnet_nvt_prova.py — telnetd guardato SUI BYTE DEL CAVO

    python3 tools/telnet_nvt_prova.py <porta> <file-sulla-macchina> <file-qui>

! I DUE PERCORSI SONO DUE, e la prima stesura ne aveva uno solo: uno e' il nome
  del file DENTRO EX-OS (/boot/kernel.txt, quello che si batte nel comando),
  l'altro e' lo stesso file QUI (boot/kernel.txt, quello che si legge per
  sapere cosa doveva arrivare). Confonderli fa fallire la prova con un
  «file non trovato» che sembra un guasto della macchina.

Si collega con un socket NUDO e controlla due cose che un client vero
nasconde:

! 1. IL CAPO RIGA E' CR LF, non LF da solo. RFC 854: sul cavo la fine di una
     riga sono DUE byte. Un LF solo dice «scendi di una riga» e lascia il
     cursore dov'era, e l'uscita scende a scaletta verso destra. Il client
     telnet vero non aiuta a trovarlo, perche' molti terminali rimediano da
     soli: bisogna contare i byte.

! 2. L'USCITA LUNGA NON SI TRONCA. Il tubo verso il master di un pty e' 1024
     byte (PTY_DIM): se chi scrive non aspetta che ci sia posto, tutto quel
     che viene dopo il primo chilobyte sparisce in silenzio. Percio' si fa
     stampare un file GRANDE e si guarda se l'ultima riga arriva.

! E IL FILE SI FA STAMPARE CON textline -v, CHE NUMERA LE RIGHE. Quindi non si
  confrontano i byte del file con quelli ricevuti — non coincidono, e la prima
  stesura di questo controllo diceva «non va» mentre il software era a posto.
  Si confronta l'ULTIMA RIGA, che e' cio' che dimostra che non si e' troncato.
"""
import socket, sys, time

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240


def separa(raw):
    """Toglie i comandi e rende (dati, risposte-da-mandare)."""
    fuori, dati, i = b"", bytearray(), 0
    while i < len(raw):
        c = raw[i]
        if c != IAC:
            dati.append(c); i += 1; continue
        if i + 1 >= len(raw): break
        cmd = raw[i + 1]
        if cmd == IAC:
            dati.append(IAC); i += 2; continue
        if cmd in (DO, DONT, WILL, WONT):
            if i + 2 >= len(raw): break
            opt = raw[i + 2]
            if cmd == DO:     fuori += bytes([IAC, WONT, opt])
            elif cmd == WILL: fuori += bytes([IAC, DONT, opt])
            i += 3; continue
        if cmd == SB:
            j = i + 2
            while j + 1 < len(raw) and not (raw[j] == IAC and raw[j + 1] == SE):
                j += 1
            i = j + 2; continue
        i += 2
    return bytes(dati), fuori


def prova(porta, remoto, locale):
    atteso = open(locale, "rb").read()
    ultima = [r for r in atteso.split(b"\n") if r.strip()][-1].strip()

    s = socket.create_connection(("127.0.0.1", porta), timeout=20)
    s.settimeout(3)

    def pompa(secondi):
        raw = b""
        fine = time.time() + secondi
        while time.time() < fine:
            try:
                d = s.recv(65536)
            except socket.timeout:
                continue
            if not d:
                break
            raw += d
        return raw

    _, risposte = separa(pompa(4))
    if risposte:
        s.sendall(risposte)

    s.sendall(b"textline " + remoto.encode() + b" -v\r\n")
    dati, _ = separa(pompa(25))
    s.close()

    nudi = [i for i, c in enumerate(dati)
            if c == 10 and (i == 0 or dati[i - 1] != 13)]
    righe_att = atteso.count(b"\n")
    righe_ric = dati.count(b"\n")
    arrivata = ultima in dati.replace(b"\r\n", b"\n")

    print("byte ricevuti            : %d" % len(dati))
    print("capi riga LF senza CR    : %d   (devono essere 0)" % len(nudi))
    print("righe nel file / ricevute: %d / %d" % (righe_att, righe_ric))
    print("l'ultima riga e' arrivata: %s" % ("SI" if arrivata else "NO"))

    if nudi:
        for i in nudi[:3]:
            print("   LF nudo a %d, intorno: %r"
                  % (i, dati[max(0, i - 24):i + 4]))

    ok = (not nudi) and arrivata and righe_ric >= righe_att
    print("ESITO: %s" % ("TUTTO A POSTO" if ok else "NON VA"))
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(2)
    sys.exit(prova(int(sys.argv[1]), sys.argv[2], sys.argv[3]))

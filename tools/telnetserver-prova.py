#!/usr/bin/env python3
"""Server Telnet minimo, SOLO per provare /bin/telnet di EX-OS.

=============================================================================
! NON E' UN SERVER TELNET. Non ha utenti, non ha password, non da' una
shell: fa l'eco di quello che riceve e risponde a qualche comando finto.
Esiste perche' per provare un CLIENT serve qualcosa dall'altra parte, e
mettere in ascolto un telnetd vero — che da' una shell senza cifratura —
sarebbe una pessima idea su qualunque macchina.

Va lanciato a mano, su localhost, per il tempo di una prova.
=============================================================================

    python3 tools/telnetserver-prova.py [porta]

Cosa prova davvero, e che un semplice `nc -l` non proverebbe:

  - NEGOZIA. Manda WILL ECHO, WILL SGA, DO TTYPE, DO NAWS appena connesso,
    e stampa le risposte del client. Un client che non risponde si vede
    subito, perche' qui compare la richiesta e non la replica.
  - CHIEDE IL TIPO DI TERMINALE con una sottonegoziazione, cioe' la parte
    del protocollo che si puo' sbagliare in silenzio.
  - MANDA IAC IAC in mezzo ai dati, per verificare che il client lo
    riduca a un solo 0xFF invece di trattarlo come un comando.
"""

import socket
import sys

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240
O_ECHO, O_SGA, O_TTYPE, O_NAWS = 1, 3, 24, 31

NOMI = {O_ECHO: "ECHO", O_SGA: "SGA", O_TTYPE: "TTYPE", O_NAWS: "NAWS"}
VERBI = {WILL: "WILL", WONT: "WONT", DO: "DO", DONT: "DONT"}

PORTA = int(sys.argv[1]) if len(sys.argv) > 1 else 2323


def servi(c):
    c.sendall(bytes([IAC, WILL, O_ECHO, IAC, WILL, O_SGA,
                     IAC, DO, O_TTYPE, IAC, DO, O_NAWS]))
    c.sendall(b"\r\nEX-OS telnet di prova. Scrivi qualcosa; 'quit' chiude.\r\n")
    # ! 0xFF nei dati va mandato RADDOPPIATO: e' la regola che distingue
    # un byte da un comando, ed e' quella che un client scritto in fretta
    # sbaglia. Se il client la rispetta, a schermo compare un carattere
    # solo.
    c.sendall(b"un byte 0xFF nei dati: [" + bytes([IAC, IAC]) + b"]\r\n")
    c.sendall(b"> ")

    stato, verbo, sb = 0, 0, bytearray()
    riga = bytearray()

    while True:
        d = c.recv(1024)
        if not d:
            break
        for b in d:
            if stato == 0:
                if b == IAC:
                    stato = 1
                elif b in (13, 10):
                    if riga:
                        testo = riga.decode("latin-1")
                        print("  riga: %r" % testo)
                        if testo.strip() == "quit":
                            c.sendall(b"\r\nArrivederci.\r\n")
                            return
                        c.sendall(b"\r\nhai scritto: " + riga + b"\r\n> ")
                        riga.clear()
                    else:
                        c.sendall(b"\r\n> ")
                elif b == 127 or b == 8:
                    if riga:
                        riga.pop()
                        c.sendall(b"\b \b")
                else:
                    riga.append(b)
                    c.sendall(bytes([b]))       # eco: abbiamo detto WILL ECHO
            elif stato == 1:
                if b == IAC:
                    riga.append(IAC); stato = 0
                elif b == SB:
                    stato = 3; sb.clear()
                elif b in VERBI:
                    verbo = b; stato = 2
                else:
                    stato = 0
            elif stato == 2:
                print("  <- %s %s" % (VERBI[verbo], NOMI.get(b, b)))
                if verbo == WILL and b == O_TTYPE:
                    c.sendall(bytes([IAC, SB, O_TTYPE, 1, IAC, SE]))
                stato = 0
            elif stato == 3:
                if b == IAC:
                    stato = 4
                else:
                    sb.append(b)
            elif stato == 4:
                if b == SE:
                    if sb and sb[0] == O_TTYPE:
                        print("  <- terminale: %r" % sb[2:].decode("latin-1"))
                    elif sb and sb[0] == O_NAWS and len(sb) >= 5:
                        print("  <- schermo: %dx%d"
                              % (sb[1] * 256 + sb[2], sb[3] * 256 + sb[4]))
                    stato = 0
                else:
                    sb.append(b); stato = 3


def main():
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", PORTA))
    s.listen(1)
    print("server telnet di prova su :%d" % PORTA)
    while True:
        c, a = s.accept()
        print("connessione da %s" % (a,))
        try:
            servi(c)
        except OSError as e:
            print("  errore: %s" % e)
        c.close()
        print("  chiusa")


if __name__ == "__main__":
    main()

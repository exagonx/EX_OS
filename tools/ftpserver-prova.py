#!/usr/bin/env python3
"""Server FTP minimo, SOLO per provare /bin/ftp di EX-OS.

=============================================================================
⚠️ NON E' UN SERVER FTP. Non ha utenti, non ha permessi, non ha limiti: fa
entrare chiunque e serve una sola directory. Esiste perche' per provare un
CLIENT serve qualcosa dall'altra parte, e su questa macchina non c'era
nessun server FTP installato.

Va lanciato a mano, su localhost, per il tempo di una prova. Metterlo in
ascolto su un'interfaccia raggiungibile da altri sarebbe regalare a
chiunque una directory in lettura E SCRITTURA.
=============================================================================

    python3 tools/ftpserver-prova.py [directory] [porta]

Comandi serviti: USER PASS SYST TYPE PWD CWD PASV LIST NLST RETR STOR
MKD RMD DELE RNFR RNTO SIZE NOOP QUIT.
Solo modo PASSIVO, che e' l'unico che il client di EX-OS usa (vedi
drivers/net/ip_proto.h: il nostro TCP non sa mettersi in ascolto).
"""

import os
import socket
import sys
import threading

RADICE = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
PORTA = int(sys.argv[2]) if len(sys.argv) > 2 else 2121


def sicuro(base, arg=""):
    """Percorso assoluto sul disco per `arg` interpretato dentro `base`.

    ⚠️ PRENDE BASE E ARGOMENTO SEPARATI, e la prima versione no: univa i
    due e poi rifaceva os.path.join(RADICE, ...) sul risultato gia'
    assoluto, producendo RADICE/RADICE/nome. Il client di EX-OS riceveva
    un 550 corretto per un file che c'era.

    Impedisce anche di uscire dalla directory servita: nemmeno un server
    di prova deve poter consegnare /etc/shadow per una '..' di troppo."""
    if arg.startswith("/"):
        p = os.path.abspath(os.path.join(RADICE, arg.lstrip("/")))
    else:
        p = os.path.abspath(os.path.join(base, arg))
    return p if p == RADICE or p.startswith(RADICE + os.sep) else RADICE


def elenco(percorso):
    righe = []
    for nome in sorted(os.listdir(percorso)):
        pieno = os.path.join(percorso, nome)
        st = os.stat(pieno)
        tipo = "d" if os.path.isdir(pieno) else "-"
        righe.append("%srw-r--r-- 1 exos exos %8d Jan  1 00:00 %s"
                     % (tipo, st.st_size, nome))
    return ("\r\n".join(righe) + "\r\n").encode()


def servi(conn, addr):
    cwd = RADICE
    rinomina_da = None   # fra RNFR e RNTO, vedi sotto
    dati_srv = None
    f = conn.makefile("rwb", buffering=0)

    def rispondi(t):
        f.write((t + "\r\n").encode())

    rispondi("220 Server di prova per EX-OS")

    while True:
        riga = f.readline()
        if not riga:
            break
        riga = riga.decode("utf-8", "replace").strip()
        if not riga:
            continue
        parti = riga.split(" ", 1)
        cmd = parti[0].upper()
        arg = parti[1] if len(parti) > 1 else ""
        print("  <- %s" % riga)

        if cmd == "USER":
            rispondi("331 Serve la password")
        elif cmd == "PASS":
            rispondi("230 Accesso eseguito")
        elif cmd == "SYST":
            rispondi("215 UNIX Type: L8")
        elif cmd == "TYPE":
            rispondi("200 Tipo impostato")
        elif cmd == "PWD":
            rel = "/" + os.path.relpath(cwd, RADICE).replace("\\", "/")
            rispondi('257 "%s"' % ("/" if rel == "/." else rel))
        elif cmd == "CWD":
            n = sicuro(cwd, arg)
            if os.path.isdir(n):
                cwd = n
                rispondi("250 Directory cambiata")
            else:
                rispondi("550 Non esiste")
        # ⚠️ QUESTI SEI SERVONO A PROVARE IL CLIENT, e senza di loro le
        # sue funzioni nuove non si possono collaudare affatto: il server
        # risponderebbe "500 comando sconosciuto" e non si distinguerebbe
        # un client rotto da un server che non sa rispondere.
        elif cmd == "MKD":
            n = sicuro(cwd, arg)
            try:
                os.mkdir(n)
                rispondi('257 "%s" creata' % arg)
            except OSError as e:
                rispondi("550 %s" % e.strerror)
        elif cmd == "RMD":
            n = sicuro(cwd, arg)
            try:
                os.rmdir(n)
                rispondi("250 Rimossa")
            except OSError as e:
                rispondi("550 %s" % e.strerror)
        elif cmd == "DELE":
            n = sicuro(cwd, arg)
            try:
                os.remove(n)
                rispondi("250 Cancellato")
            except OSError as e:
                rispondi("550 %s" % e.strerror)
        elif cmd == "RNFR":
            n = sicuro(cwd, arg)
            if os.path.exists(n):
                rinomina_da = n
                # ⚠️ 350 e non 250: "ho capito, ora dimmi il nuovo nome".
                # Un 2xx qui farebbe credere al client di aver finito.
                rispondi("350 E adesso RNTO")
            else:
                rinomina_da = None
                rispondi("550 Non esiste")
        elif cmd == "RNTO":
            if rinomina_da is None:
                rispondi("503 Prima RNFR")
            else:
                try:
                    os.rename(rinomina_da, sicuro(cwd, arg))
                    rispondi("250 Rinominato")
                except OSError as e:
                    rispondi("550 %s" % e.strerror)
                rinomina_da = None
        elif cmd == "SIZE":
            n = sicuro(cwd, arg)
            if os.path.isfile(n):
                rispondi("213 %d" % os.path.getsize(n))
            else:
                rispondi("550 Non e' un file")
        elif cmd == "NOOP":
            rispondi("200 Eccomi")
        elif cmd == "PASV":
            dati_srv = socket.socket()
            dati_srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            dati_srv.bind(("127.0.0.1", 0))
            dati_srv.listen(1)
            p = dati_srv.getsockname()[1]
            # ⚠️ L'indirizzo annunciato e' quello con cui il CLIENT ci
            # vede, non quello su cui siamo in ascolto: dietro il NAT di
            # QEMU il client raggiunge l'host come 10.0.2.2.
            host = os.environ.get("FTP_ANNUNCIA", "10.0.2.2")
            h = host.replace(".", ",")
            rispondi("227 Entering Passive Mode (%s,%d,%d)" % (h, p >> 8, p & 0xFF))
        elif cmd in ("LIST", "NLST", "RETR", "STOR"):
            if dati_srv is None:
                rispondi("425 Prima PASV")
                continue
            rispondi("150 Apro la connessione dati")
            d, _ = dati_srv.accept()
            try:
                if cmd in ("LIST", "NLST"):
                    obiettivo = sicuro(cwd, arg) if arg else cwd
                    if cmd == "LIST":
                        d.sendall(elenco(obiettivo))
                    else:
                        d.sendall(("\r\n".join(sorted(os.listdir(obiettivo)))
                                   + "\r\n").encode())
                elif cmd == "RETR":
                    with open(sicuro(cwd, arg), "rb") as g:
                        d.sendall(g.read())
                else:
                    with open(sicuro(cwd, arg), "wb") as g:
                        while True:
                            b = d.recv(65536)
                            if not b:
                                break
                            g.write(b)
            except OSError as e:
                d.close()
                dati_srv.close()
                dati_srv = None
                rispondi("550 %s" % e)
                continue
            d.close()
            dati_srv.close()
            dati_srv = None
            rispondi("226 Trasferimento completato")
        elif cmd == "QUIT":
            rispondi("221 Arrivederci")
            break
        else:
            rispondi("502 Comando non gestito")

    conn.close()
    print("  connessione chiusa")


def main():
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", PORTA))
    s.listen(5)
    print("server di prova su 127.0.0.1:%d, radice %s" % (PORTA, RADICE))
    while True:
        c, a = s.accept()
        print("connessione da %s" % (a,))
        threading.Thread(target=servi, args=(c, a), daemon=True).start()


if __name__ == "__main__":
    main()

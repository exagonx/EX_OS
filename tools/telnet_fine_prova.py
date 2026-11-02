#!/usr/bin/env python3
"""tools/telnet_fine_prova.py PORT SERIAL_LOG: does a telnet client hear the end?

Part of tools/prova_telnet_fine.sh; EX-OS runs with EXOS_RIAVVIO=1, so a restart
keeps QEMU alive and its user-mode network keeps the host socket open. A socket
that ends here was ended by the guest.

  A  `guardia` restarts the machine without warning anybody. The connection
     now exists only on our side: the next key must get an RST (ip.drv 0.020).
  B  `reboot` kills telnetd with a signal: the client must hear the end by
     itself, with no key pressed, before the machine resets.
"""
import socket
import sys
import time

porta = int(sys.argv[1])
seriale = sys.argv[2]
guasti = 0


def ascolti():
    try:
        return open(seriale, "rb").read().count(b"telnetd: in ascolto")
    except OSError:
        return 0


def aspetta_ascolti(n, secondi):
    fine = time.time() + secondi
    while time.time() < fine:
        if ascolti() >= n:
            return True
        time.sleep(1)
    return False


def collega():
    s = socket.create_connection(("127.0.0.1", porta), 10)
    s.settimeout(1)
    fine = time.time() + 6
    while time.time() < fine:           # the prompt and the negotiation
        try:
            if not s.recv(4096):
                break
        except socket.timeout:
            pass
    return s


def aspetta_fine(s, secondi):
    """Seconds until the end of the stream, or None."""
    inizio = time.time()
    while time.time() - inizio < secondi:
        try:
            if not s.recv(4096):
                return time.time() - inizio
        except socket.timeout:
            pass
        except OSError:
            return time.time() - inizio
    return None


def esito(buono, testo):
    global guasti
    print("  [%s] %s" % ("OK" if buono else "NO", testo), flush=True)
    if not buono:
        guasti += 1


if not aspetta_ascolti(1, 120):
    print("NON RIUSCITO: telnetd non ascolta")
    sys.exit(2)
time.sleep(2)

s = collega()
s.sendall(b"guardia 15\r\n")
if not aspetta_ascolti(2, 150):
    print("NON RIUSCITO: la macchina non e' tornata")
    sys.exit(2)
time.sleep(2)
s.sendall(b"x")
t = aspetta_fine(s, 30)
esito(t is not None, "A: dopo il riavvio il tasto trova la fine della connessione"
      + ("" if t is None else " in %.1f s" % t))
s.close()

s = collega()
s.sendall(b"reboot\r\n")
t = aspetta_fine(s, 120)
esito(t is not None, "B: reboot, il client sente la fine da solo"
      + ("" if t is None else " in %.1f s" % t))
s.close()
sys.exit(1 if guasti else 0)

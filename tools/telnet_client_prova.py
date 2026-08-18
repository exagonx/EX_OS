# tools/telnet_client_prova.py — pilota il client telnet VERO contro telnetd
#
# ! IL CLIENT DEV'ESSERE VERO, e per questo serve un pty: NAWS non e' una cosa
#   che si batte alla tastiera. Il client la manda quando il TERMINALE gli dice
#   che e' cambiato — cioe' quando arriva un SIGWINCH — e senza un pty non c'e'
#   nessun terminale che possa dirglielo.
#
# ! E LA MISURA SI CAMBIA IN DUE TEMPI, o non succede niente: prima TIOCSWINSZ
#   sul pty del banco, poi il segnale. Cambiare la misura e basta lascia la
#   prova muta, e sembra che il server non risponda.
#
# Si usa da tools/prova_telnet.sh, che accende la macchina e poi chiama questo.
"""Pilota il client telnet vero con un pty, e gli fa cambiare misura."""
import os, pty, select, signal, struct, sys, time, fcntl, termios


def misura(fd, righe, colonne):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", righe, colonne, 0, 0))


def prova(porta, righe=40, colonne=120, attesa=40):
    pid, fd = pty.fork()
    if pid == 0:
        os.execvp("telnet", ["telnet", "127.0.0.1", str(porta)])
        os._exit(1)

    misura(fd, 24, 80)

    uscita = b""
    t0 = time.time()
    allargato = False

    while time.time() - t0 < attesa:
        r, _, _ = select.select([fd], [], [], 0.5)

        if not allargato and time.time() - t0 > 6:
            misura(fd, righe, colonne)
            os.kill(pid, signal.SIGWINCH)
            allargato = True

        if not r:
            continue
        try:
            d = os.read(fd, 4096)
        except OSError:
            break
        if not d:
            break
        uscita += d

        if allargato and b"ex-os" in uscita and time.time() - t0 > 12:
            os.write(fd, b"exit\n")
            time.sleep(2)
            break

    try:
        os.close(fd)
    except OSError:
        pass
    return uscita.decode("utf8", "replace")


if __name__ == "__main__":
    print(prova(int(sys.argv[1]) if len(sys.argv) > 1 else 2323))

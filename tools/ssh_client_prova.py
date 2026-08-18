# ssh_client_prova.py — pilota il client OpenSSH VERO contro sshd di EX-OS.
#
# ! IL CLIENT DEV'ESSERE VERO, e per questo serve un pty: ssh la password la
#   chiede al terminale, non a stdin. Due programmi scritti dalla stessa mano
#   che si capiscono non dimostrano niente — e' con OpenSSH dall'altra parte
#   che si scopre quale campo e' lungo un byte di troppo.
#
# Si usa da tools/prova_ssh.sh, che accende la macchina e poi chiama questo.

"""Pilota il client OpenSSH vero con un pty, cosi' puo' chiedere la password."""
import os, pty, select, signal, sys, time, fcntl, termios, struct

def misura(fd, righe, colonne):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", righe, colonne, 0, 0))


def allarga(pid, fd, righe, colonne):
    """Ridimensiona il pty del banco e lo dice a ssh.

    ! SENZA IL SEGNALE NON SUCCEDE NIENTE. ssh la finestra non la guarda: la
      RIcontrolla quando il terminale glielo dice con SIGWINCH, e solo allora
      manda «window-change» sul canale. Cambiare la misura e basta lascerebbe
      la prova muta e sembrerebbe che il server non risponda.
    """
    misura(fd, righe, colonne)
    os.kill(pid, signal.SIGWINCH)


def prova(porta, comandi, attesa=45):
    open("/tmp/exos/client.log","wb").close()
    pid, fd = pty.fork()
    if pid == 0:
        os.execvp("ssh", ["ssh", "-p", str(porta), "-tt",
                          "-o", "StrictHostKeyChecking=no",
                          "-o", "UserKnownHostsFile=/dev/null",
                          "-o", "PreferredAuthentications=password",
                          "-o", "PubkeyAuthentication=no",
                          "-o", "ConnectTimeout=10",
                                                    "root@127.0.0.1"])
        os._exit(1)

    # ! IL pty DEL BANCO VUOLE UNA MISURA, o il client dice «0x0» e il server
    # non ha modo di sapere quanto e' grande lo schermo.
    misura(fd, 24, 80)

    uscita = b""
    fine = time.time() + attesa
    mandati = 0
    scadenza_cmd = 0
    while time.time() < fine:
        r, _, _ = select.select([fd], [], [], 1.0)
        if mandati == 1 and time.time() > scadenza_cmd:
            allarga(pid, fd, 40, 120)
            time.sleep(2.0)
            for c in comandi:
                os.write(fd, c.encode() + b"\n")
                time.sleep(2.5)
            mandati = 2
        if r:
            try:
                d = os.read(fd, 4096)
            except OSError:
                break
            if not d:
                break
            uscita += d
            open("/tmp/exos/client.log","ab").write(d)
            if b"assword" in d and mandati == 0:
                os.write(fd, b"qualunque\n")
                mandati = 1
                scadenza_cmd = time.time() + 4
            elif mandati == 1 and time.time() > scadenza_cmd:
                allarga(pid, fd, 40, 120)
                time.sleep(2.0)
                for c in comandi:
                    os.write(fd, c.encode() + b"\n")
                    time.sleep(2.5)
                mandati = 2
    try:
        os.close(fd)
    except OSError:
        pass
    return uscita.decode("utf8", "replace")

if __name__ == "__main__":
    print(prova(2225, ["id", "exit"]))

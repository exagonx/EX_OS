#!/usr/bin/env python3
# =============================================================================
# tools/prova_ftp_server.py — a small FTP/FTPS server for testing lib/exftp
#
#     python3 tools/prova_ftp_server.py DIR PORT [CERT KEY [IMPLICIT_PORT]]
#
# Standard library only (no pyftpdlib on this machine). Serves DIR to the
# user "prova" with password "segreta" (and to anonymous, read-only). With
# CERT/KEY it offers explicit FTPS (AUTH TLS, PBSZ, PROT P) on PORT and, if
# IMPLICIT_PORT is given, implicit FTPS there. TLS 1.3 only, as EX-OS.
#
# Commands: USER PASS SYST FEAT PWD CWD CDUP TYPE MODE STRU EPSV PASV PORT
# LIST NLST MLSD RETR STOR APPE REST SIZE MDTM MFMT MKD RMD DELE RNFR RNTO
# NOOP QUIT HELP STAT SITE AUTH PBSZ PROT ABOR.
#
# ! THE PASV ADDRESS IS A WRONG ONE ON PURPOSE (192.168.77.77): a server
# behind NAT does that, and the client must use the control connection's
# address. Every line received is logged to stdout, PASS masked.
#
# ! PROVA_FTP_RIUSO=1 MAKES IT vsftpd (22 October 2026): a data connection
# whose TLS session is not a resumption of the control one is refused, as
# require_ssl_reuse=YES does, and the client certificate is asked for
# (ssl_request_cert=YES). Either way the log says "dati ripresi: si/no".
# =============================================================================
import os, socket, ssl, sys, threading, time, stat as st

RADICE = os.path.abspath(sys.argv[1])
PORTA = int(sys.argv[2])
CERT = sys.argv[3] if len(sys.argv) > 3 else None
CHIAVE = sys.argv[4] if len(sys.argv) > 4 else None
IMPLICITA = int(sys.argv[5]) if len(sys.argv) > 5 else 0
RIUSO = os.environ.get("PROVA_FTP_RIUSO") == "1"

ctx = None
if CERT:
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_3
    ctx.load_cert_chain(CERT, CHIAVE)
    if RIUSO:
        # e come vsftpd chiede il certificato del cliente senza pretenderlo
        ctx.verify_mode = ssl.CERT_OPTIONAL
        ctx.load_verify_locations(CERT)


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


class Sessione(threading.Thread):
    def __init__(self, s, implicita):
        super().__init__(daemon=True)
        self.s = s
        self.cwd = "/"
        self.utente = None
        self.dentro = False
        self.binario = True
        self.pasv = None
        self.port = None
        self.prot_p = False
        self.rest = 0
        self.rnfr = None
        self.tls = False
        if implicita:
            self.s = ctx.wrap_socket(self.s, server_side=True)
            self.tls = True
        self.f = self.s.makefile("rb")

    def manda(self, riga):
        log(">", riga)
        self.s.sendall((riga + "\r\n").encode())

    def vero(self, p):
        if not p:
            p = self.cwd
        elif not p.startswith("/"):
            p = os.path.join(self.cwd, p)
        p = os.path.normpath(p).replace("\\", "/")
        if not p.startswith("/"):
            p = "/" + p
        v = os.path.normpath(RADICE + p)
        if not (v == RADICE or v.startswith(RADICE + os.sep)):
            raise PermissionError(p)
        return v, p

    def dati(self):
        if self.pasv:
            self.pasv.settimeout(15)
            c, _ = self.pasv.accept()
            self.pasv.close()
            self.pasv = None
        elif self.port:
            c = socket.create_connection(self.port, timeout=15)
            self.port = None
        else:
            raise OSError("no data connection")
        if self.prot_p:
            c = ctx.wrap_socket(c, server_side=True)
            log("! dati ripresi:", "si" if c.session_reused else "no")
            if RIUSO and not c.session_reused:
                c.close()
                raise OSError("SSL connection failed: session reuse required")
        return c

    def elenco_unix(self, nome, v):
        s = os.stat(v)
        tipo = "d" if st.S_ISDIR(s.st_mode) else "-"
        t = time.strftime("%b %d %H:%M", time.gmtime(s.st_mtime))
        return "%srw-r--r--   1 prova    prova %10d %s %s" % (tipo, s.st_size, t, nome)

    def elenco_mlsd(self, nome, v):
        s = os.stat(v)
        tipo = "dir" if st.S_ISDIR(s.st_mode) else "file"
        t = time.strftime("%Y%m%d%H%M%S", time.gmtime(s.st_mtime))
        return "type=%s;size=%d;modify=%s; %s" % (tipo, s.st_size, t, nome)

    def lista(self, arg, fmt):
        if arg.startswith("-"):
            arg = ""
        v, _ = self.vero(arg)
        if os.path.isdir(v):
            nomi = sorted(os.listdir(v))
            righe = [fmt(n, os.path.join(v, n)) for n in nomi]
        elif os.path.exists(v):
            righe = [fmt(os.path.basename(v), v)]
        else:
            return None
        return "".join(r + "\r\n" for r in righe).encode()

    def manda_dati(self, testo):
        self.manda("150 Here comes the listing.")
        c = self.dati()
        c.sendall(testo)
        try:
            c.unwrap() if self.prot_p else None
        except Exception:
            pass
        c.close()
        self.manda("226 Transfer complete.")

    def run(self):
        try:
            self.manda("220-EX-OS prova FTP server")
            self.manda("220 pronto")
            while True:
                riga = self.f.readline()
                if not riga:
                    break
                riga = riga.decode("utf-8", "replace").rstrip("\r\n")
                cmd, _, arg = riga.partition(" ")
                cmd = cmd.upper()
                log("<", "PASS ****" if cmd == "PASS" else riga)
                if not self.comando(cmd, arg):
                    break
        except Exception as e:
            log("! sessione:", repr(e))
        finally:
            try:
                self.s.close()
            except Exception:
                pass

    def comando(self, cmd, arg):
        aperti = ("USER", "PASS", "QUIT", "AUTH", "SYST", "FEAT", "NOOP", "HELP", "PBSZ", "PROT")
        if not self.dentro and cmd not in aperti:
            self.manda("530 Please login with USER and PASS.")
            return True
        try:
            return self.esegui(cmd, arg)
        except PermissionError:
            self.manda("550 Permission denied.")
        except FileNotFoundError:
            self.manda("550 No such file or directory.")
        except OSError as e:
            self.manda("425 %s" % e)
        return True

    def esegui(self, cmd, arg):
        if cmd == "AUTH":
            if not ctx or arg.upper() not in ("TLS", "SSL"):
                self.manda("504 AUTH not supported.")
                return True
            self.manda("234 Proceed with negotiation.")
            self.s = ctx.wrap_socket(self.s, server_side=True)
            self.f = self.s.makefile("rb")
            self.tls = True
            log("! TLS on the control connection:", self.s.version())
        elif cmd == "PBSZ":
            self.manda("200 PBSZ=0")
        elif cmd == "PROT":
            if not self.tls:
                self.manda("503 PROT needs AUTH first.")
            else:
                self.prot_p = arg.upper() == "P"
                self.manda("200 PROT now %s." % ("Private" if self.prot_p else "Clear"))
        elif cmd == "USER":
            self.utente = arg
            self.dentro = False
            if arg in ("anonymous", "ftp"):
                self.manda("331 Anonymous: any password.")
            else:
                self.manda("331 Please specify the password.")
        elif cmd == "PASS":
            if self.utente in ("anonymous", "ftp") or (self.utente == "prova" and arg == "segreta"):
                self.dentro = True
                self.manda("230 Login successful.")
            else:
                self.manda("530 Login incorrect.")
        elif cmd == "SYST":
            self.manda("215 UNIX Type: L8")
        elif cmd == "FEAT":
            self.manda("211-Features:")
            for f in ("EPSV", "MDTM", "MFMT", "MLST type*;size*;modify*;", "REST STREAM", "SIZE", "UTF8", "AUTH TLS", "PBSZ", "PROT"):
                self.manda(" " + f)
            self.manda("211 End")
        elif cmd == "HELP":
            self.manda("214-The following commands are recognized.")
            self.manda(" USER PASS LIST NLST MLSD RETR STOR APPE CWD PWD MKD RMD DELE")
            self.manda("214 Help OK.")
        elif cmd == "STAT":
            self.manda("211-FTP server status:")
            self.manda("     Logged in as %s, TLS %s" % (self.utente, "on" if self.tls else "off"))
            self.manda("211 End of status")
        elif cmd == "SITE":
            self.manda("200 SITE %s accepted." % arg)
        elif cmd == "NOOP":
            self.manda("200 NOOP ok.")
        elif cmd == "QUIT":
            self.manda("221 Goodbye.")
            return False
        elif cmd == "PWD":
            self.manda('257 "%s" is the current directory' % self.cwd.replace('"', '""'))
        elif cmd in ("CWD", "CDUP"):
            v, p = self.vero(".." if cmd == "CDUP" else arg)
            if os.path.isdir(v):
                self.cwd = p
                self.manda("250 Directory successfully changed.")
            else:
                self.manda("550 Failed to change directory.")
        elif cmd == "TYPE":
            self.binario = arg.upper().startswith("I")
            self.manda("200 Switching to %s mode." % ("Binary" if self.binario else "ASCII"))
        elif cmd == "EPSV":
            self.pasv = socket.socket()
            self.pasv.bind(("0.0.0.0", 0))
            self.pasv.listen(1)
            self.manda("229 Entering Extended Passive Mode (|||%d|)" % self.pasv.getsockname()[1])
        elif cmd == "PASV":
            self.pasv = socket.socket()
            self.pasv.bind(("0.0.0.0", 0))
            self.pasv.listen(1)
            p = self.pasv.getsockname()[1]
            self.manda("227 Entering Passive Mode (192,168,77,77,%d,%d)." % (p // 256, p % 256))
        elif cmd == "PORT":
            n = [int(x) for x in arg.split(",")]
            ip = "%d.%d.%d.%d" % tuple(n[:4])
            # QEMU's user network: the guest's address is not reachable;
            # its port forward is (hostfwd), arranged by the test script.
            if ip.startswith("10.0.2."):
                ip = "127.0.0.1"
                p = int(os.environ.get("PROVA_FTP_PORT_ATTIVA", "0")) or n[4] * 256 + n[5]
            else:
                p = n[4] * 256 + n[5]
            self.port = (ip, p)
            self.manda("200 PORT command successful.")
        elif cmd in ("LIST", "NLST", "MLSD"):
            fmt = {"LIST": self.elenco_unix, "NLST": lambda n, v: n, "MLSD": self.elenco_mlsd}[cmd]
            t = self.lista(arg, fmt)
            if t is None:
                self.manda("550 No such directory.")
            else:
                self.manda_dati(t)
        elif cmd == "REST":
            self.rest = int(arg)
            self.manda("350 Restart position accepted (%d)." % self.rest)
        elif cmd == "RETR":
            v, _ = self.vero(arg)
            if not os.path.isfile(v):
                self.manda("550 Failed to open file.")
                return True
            with open(v, "rb") as fh:
                fh.seek(self.rest)
                b = fh.read()
            self.rest = 0
            if not self.binario:
                b = b.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
            self.manda("150 Opening data connection for %s (%d bytes)." % (arg, len(b)))
            c = self.dati()
            c.sendall(b)
            try:
                c.unwrap() if self.prot_p else None
            except Exception:
                pass
            c.close()
            self.manda("226 Transfer complete.")
        elif cmd in ("STOR", "APPE"):
            v, _ = self.vero(arg)
            if self.utente in ("anonymous", "ftp"):
                self.manda("550 Permission denied.")
                return True
            self.manda("150 Ok to send data.")
            c = self.dati()
            parti = []
            while True:
                b = c.recv(65536)
                if not b:
                    break
                parti.append(b)
            c.close()
            b = b"".join(parti)
            if not self.binario:
                b = b.replace(b"\r\n", b"\n")
            modo = "ab" if cmd == "APPE" else ("r+b" if self.rest and os.path.exists(v) else "wb")
            with open(v, modo) as fh:
                if modo == "r+b":
                    fh.seek(self.rest)
                    fh.truncate()
                fh.write(b)
            self.rest = 0
            self.manda("226 Transfer complete.")
        elif cmd == "SIZE":
            v, _ = self.vero(arg)
            if os.path.isfile(v):
                self.manda("213 %d" % os.path.getsize(v))
            else:
                self.manda("550 Could not get file size.")
        elif cmd == "MDTM":
            v, _ = self.vero(arg)
            if os.path.isfile(v):
                self.manda("213 " + time.strftime("%Y%m%d%H%M%S", time.gmtime(os.path.getmtime(v))))
            else:
                self.manda("550 Could not get file modification time.")
        elif cmd == "MFMT":
            data, _, nome = arg.partition(" ")
            v, _ = self.vero(nome)
            t = time.mktime(time.strptime(data, "%Y%m%d%H%M%S")) - time.timezone
            os.utime(v, (t, t))
            self.manda("213 Modify=%s; %s" % (data, nome))
        elif cmd == "MKD":
            v, p = self.vero(arg)
            if os.path.exists(v):
                self.manda("550 Create directory operation failed.")
            else:
                os.mkdir(v)
                self.manda('257 "%s" created' % p)
        elif cmd == "RMD":
            v, _ = self.vero(arg)
            os.rmdir(v)
            self.manda("250 Remove directory operation successful.")
        elif cmd == "DELE":
            v, _ = self.vero(arg)
            os.remove(v)
            self.manda("250 Delete operation successful.")
        elif cmd == "RNFR":
            v, _ = self.vero(arg)
            if os.path.exists(v):
                self.rnfr = v
                self.manda("350 Ready for RNTO.")
            else:
                self.manda("550 RNFR command failed.")
        elif cmd == "RNTO":
            v, _ = self.vero(arg)
            os.rename(self.rnfr, v)
            self.rnfr = None
            self.manda("250 Rename successful.")
        elif cmd == "ABOR":
            self.manda("225 No transfer to ABOR.")
        else:
            self.manda("502 Command not implemented.")
        return True


def ascolta(porta, implicita):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", porta))
    s.listen(8)
    log("in ascolto su", porta, "(FTPS implicito)" if implicita else "")
    while True:
        c, a = s.accept()
        log("connessione da", a)
        try:
            Sessione(c, implicita).start()
        except Exception as e:
            log("! TLS implicito:", repr(e))
            c.close()


if IMPLICITA:
    threading.Thread(target=ascolta, args=(IMPLICITA, True), daemon=True).start()
ascolta(PORTA, False)

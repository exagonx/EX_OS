#!/usr/bin/env python3
# tools/ssh_malformati.py -- packets with lying lengths against sshd
# (sshd 0.004, debug/bug_SSH001). Used by tools/prova_ssh.sh before the real
# client: sshd must close only the malformed connection and answer the next.
#
# A small SSH client of its own (curve25519-sha256, ssh-ed25519,
# chacha20-poly1305@openssh.com, kex-strict-c) on python3-cryptography:
# OpenSSH and paramiko do not send what a broken client sends.
#
# uso: ssh_malformati.py [porta]      (default 2225)
import socket, struct, sys, os, time, hashlib
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms
from cryptography.hazmat.primitives.poly1305 import Poly1305
from cryptography.hazmat.primitives import serialization

PORTA = int(sys.argv[1]) if len(sys.argv) > 1 else 2225
VC = b"SSH-2.0-Malformati_0.1"


def u32(v): return struct.pack(">I", v & 0xFFFFFFFF)
def s(b): return u32(len(b)) + b
def mpint(b):
    b = b.lstrip(b"\0")
    if b and b[0] & 0x80: b = b"\0" + b
    return s(b)


def chacha(k, ctr, seq, data):
    c = Cipher(algorithms.ChaCha20(k, struct.pack("<Q", ctr) + struct.pack(">Q", seq)), None)
    return c.encryptor().update(data)


class Fine(Exception):
    pass


class Ssh:
    def __init__(self, strict=True):
        self.c = socket.create_connection(("127.0.0.1", PORTA), timeout=20)
        self.buf = b""
        self.seq_in = self.seq_out = 0
        self.kin = self.kout = None
        self.strict = strict

    def leggi(self, n):
        while len(self.buf) < n:
            d = self.c.recv(65536)
            if not d: raise Fine("chiusa")
            self.buf += d
        r, self.buf = self.buf[:n], self.buf[n:]
        return r

    def manda(self, carico):
        if self.kout is None:
            pad = 8 - ((5 + len(carico)) % 8)
            if pad < 4: pad += 8
            pk = u32(1 + len(carico) + pad) + bytes([pad]) + carico + os.urandom(pad)
            self.c.sendall(pk)
        else:
            pad = 8 - ((1 + len(carico)) % 8)
            if pad < 4: pad += 8
            corpo = bytes([pad]) + carico + os.urandom(pad)
            k2, k1 = self.kout[:32], self.kout[32:]
            lung = chacha(k1, 0, self.seq_out, u32(len(corpo)))
            enc = chacha(k2, 1, self.seq_out, corpo)
            pk = chacha(k2, 0, self.seq_out, b"\0" * 32)
            tag = Poly1305.generate_tag(pk, lung + enc)
            self.c.sendall(lung + enc + tag)
        self.seq_out += 1

    def ricevi(self):
        if self.kin is None:
            tot = struct.unpack(">I", self.leggi(4))[0]
            d = self.leggi(tot)
            r = d[1:tot - d[0]]
        else:
            k2, k1 = self.kin[:32], self.kin[32:]
            lung = self.leggi(4)
            tot = struct.unpack(">I", chacha(k1, 0, self.seq_in, lung))[0]
            enc = self.leggi(tot)
            tag = self.leggi(16)
            pk = chacha(k2, 0, self.seq_in, b"\0" * 32)
            Poly1305.verify_tag(pk, lung + enc, tag)
            d = chacha(k2, 1, self.seq_in, enc)
            r = d[1:tot - d[0]]
        self.seq_in += 1
        return r

    def entra(self):
        self.c.sendall(VC + b"\r\n")
        riga = b""
        while not riga.startswith(b"SSH-"):
            riga = b""
            while not riga.endswith(b"\n"): riga += self.leggi(1)
        vs = riga.rstrip(b"\r\n")
        is_ = self.ricevi()
        kex = b"curve25519-sha256" + (b",kex-strict-c-v00@openssh.com" if self.strict else b"")
        cif = b"chacha20-poly1305@openssh.com"
        ic = bytes([20]) + os.urandom(16) + s(kex) + s(b"ssh-ed25519") + s(cif) * 2 + \
            s(b"") * 2 + s(b"none") * 2 + s(b"") * 2 + b"\0" + u32(0)
        self.manda(ic)
        priv = X25519PrivateKey.generate()
        qc = priv.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        self.manda(bytes([30]) + s(qc))
        r = self.ricevi()
        assert r[0] == 31, r[:1]
        p = 1
        ks_n = struct.unpack(">I", r[p:p + 4])[0]; ks = r[p + 4:p + 4 + ks_n]; p += 4 + ks_n
        qs_n = struct.unpack(">I", r[p:p + 4])[0]; qs = r[p + 4:p + 4 + qs_n]; p += 4 + qs_n
        sg_n = struct.unpack(">I", r[p:p + 4])[0]; sg = r[p + 4:p + 4 + sg_n]
        k = priv.exchange(X25519PublicKey.from_public_bytes(qs))
        h = hashlib.sha256(s(VC) + s(vs) + s(ic) + s(is_) + s(ks) + s(qc) + s(qs) + mpint(k)).digest()
        pub = ks[4 + 11 + 4:]
        firma = sg[4 + 11 + 4:]
        Ed25519PublicKey.from_public_bytes(pub).verify(firma, h)
        assert self.ricevi()[0] == 21
        if self.strict: self.seq_in = 0
        self.manda(bytes([21]))
        if self.strict: self.seq_out = 0

        def chiave(lettera):
            k1 = hashlib.sha256(mpint(k) + h + lettera + h).digest()
            return k1 + hashlib.sha256(mpint(k) + h + k1).digest()
        self.kout, self.kin = chiave(b"C"), chiave(b"D")
        self.manda(bytes([5]) + s(b"ssh-userauth"))
        assert self.ricevi()[0] == 6

    def autentica(self):
        self.manda(bytes([50]) + s(b"root") + s(b"ssh-connection") + s(b"password") + b"\0" + s(b"x"))
        assert self.ricevi()[0] == 52
        self.manda(bytes([90]) + s(b"session") + u32(0) + u32(1 << 20) + u32(32768))
        assert self.ricevi()[0] == 91

    def shell(self, pty_req=None):
        self.manda(pty_req or (bytes([98]) + u32(0) + s(b"pty-req") + b"\0" + s(b"xterm") +
                               u32(80) + u32(24) + u32(0) + u32(0) + s(b"")))
        self.manda(bytes([98]) + u32(0) + s(b"shell") + b"\1")

    def raccogli(self, secondi):
        """CHANNEL_DATA bytes until the time runs out or the connection closes"""
        dati = b""
        fine = time.time() + secondi
        self.c.settimeout(1)
        while time.time() < fine:
            try:
                r = self.ricevi()
            except socket.timeout:
                continue
            except Fine:
                break
            if r[0] == 94: dati += r[9:]
            if r[0] in (96, 97): break
        return dati


no = 0


def esito(c, m):
    global no
    print("  [%s] %s" % ("OK" if c else "NO", m))
    if not c: no += 1


def chiusa(x, secondi=10):
    x.c.settimeout(secondi)
    try:
        while True:
            x.ricevi()
    except Fine:
        return True
    except (socket.timeout, OSError):
        return False
    except Exception:
        return True


def vivo():
    """sshd answers a new connection with its version line"""
    for _ in range(20):
        try:
            c = socket.create_connection(("127.0.0.1", PORTA), timeout=10)
            r = c.recv(64)
            c.close()
            if r.startswith(b"SSH-"): return True
        except OSError:
            pass
        time.sleep(1)
    return False


# 1. a whole session, strict mode: the reference
x = Ssh()
x.entra()
x.autentica()
x.shell()
x.manda(bytes([94]) + u32(0) + s(b"echo pro\"\"va42\n"))
d = x.raccogli(12)
esito(b"prova42" in d, "sessione intera in modalita' rigida (kex-strict): %d byte" % len(d))
x.c.close()
time.sleep(2)

# 2. (a) USERAUTH_REQUEST with a name 0x7FFFFFF0 bytes long
x = Ssh(strict=False)
x.entra()
x.manda(bytes([50]) + u32(0x7FFFFFF0) + b"root")
esito(chiusa(x), "(a) nome lungo 0x7FFFFFF0: chiude la connessione")
esito(vivo(), "(a) sshd risponde alla prossima")

# 3. (a') a method string past the end
x = Ssh()
x.entra()
x.manda(bytes([50]) + s(b"root") + s(b"ssh-connection") + u32(0xFFFFFFFF))
esito(chiusa(x), "(a) metodo lungo 0xFFFFFFFF: chiude la connessione")
esito(vivo(), "(a) sshd risponde alla prossima")

# 4. (b) pty-req with a terminal name of 0xFFFFFFF8 bytes
x = Ssh()
x.entra()
x.autentica()
x.shell(bytes([98]) + u32(0) + s(b"pty-req") + b"\1" + u32(0xFFFFFFF8) + b"xterm")
x.manda(bytes([94]) + u32(0) + s(b"echo do\"\"po25\n"))
d = x.raccogli(12)
esito(b"dopo25" in d, "(b) pty-req bugiardo ignorato, la shell parte (%d byte)" % len(d))
x.c.close()
esito(vivo(), "(b) sshd risponde alla prossima")

# 5. (c) CHANNEL_DATA of 0xFFFFFFF8 bytes: before 0.004 the stack came back
x = Ssh()
x.entra()
x.autentica()
x.shell()
time.sleep(3)
x.raccogli(3)
x.manda(bytes([94]) + u32(0) + u32(0xFFFFFFF8) + b"A" * 8)
d = x.raccogli(8)
esito(len(d) < 4096, "(c) CHANNEL_DATA lungo 0xFFFFFFF8: tornano %d byte, non la pila" % len(d))
x.c.close()
esito(vivo(), "(c) sshd risponde alla prossima")

# 6. a packet with too little padding
x = Ssh()
x.entra()
corpo = bytes([1]) + bytes([2]) + b"\0" * 6    # padding 1, IGNORE
k2, k1 = x.kout[:32], x.kout[32:]
lung = chacha(k1, 0, x.seq_out, u32(len(corpo)))
enc = chacha(k2, 1, x.seq_out, corpo)
tag = Poly1305.generate_tag(chacha(k2, 0, x.seq_out, b"\0" * 32), lung + enc)
x.c.sendall(lung + enc + tag)
esito(chiusa(x), "riempimento di 1 byte (RFC 4253 6 vuole 4): chiude")
esito(vivo(), "sshd risponde alla prossima")

print("ssh_malformati: %d no" % no)
sys.exit(1 if no else 0)

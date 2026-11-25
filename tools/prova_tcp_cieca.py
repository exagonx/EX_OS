#!/usr/bin/env python3
# =============================================================================
# tools/prova_tcp_cieca.py - an attacker off the path against ip.drv's TCP
# (ip.drv 0.029, debug/bug_IP001). Used by tools/prova_tcp_cieca.sh.
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     prova_tcp_cieca.py PORTA SERIALE
#
# It is the other end of the cable: QEMU connects with "-netdev
# socket,connect=:PORTA" (4-byte big-endian length, then the frame) and this
# script writes and reads raw Ethernet frames as 10.9.0.2, so it can send
# what no honest stack sends. It waits for "telnetd: in ascolto" in SERIALE.
#   ISN:  five SYNs half a second apart; until 0.028 the ISNs were
#         1000 + uptime_ms & 0xFFFF and grew by ~500
#   RST outside the window, RST inside but not exact (challenge ACK), ACK of
#   bytes never sent, a SYN on the open connection, data far outside the
#   window: the connection stays and answers
#   RST with the exact number: the connection goes
# Then DNS (debug/bug_DNS001, the guest asks us, 10.9.0.2): to each question
# first a forged answer from 10.9.0.3, then one for another question, then
# the true one with an A of another name first; and a CNAME. The guest must
# print only the true addresses; ids and ports must not be in a row.
# =============================================================================
import socket, struct, sys, time, os

PORTA, SERIALE = int(sys.argv[1]), sys.argv[2]
NOI_MAC = bytes.fromhex("525400990002")
NOI_IP = bytes([10, 9, 0, 2])
LUI_IP = bytes([10, 9, 0, 1])
lui_mac = b"\xff" * 6


def somma(d):
    if len(d) & 1: d += b"\0"
    s = sum(struct.unpack(">%dH" % (len(d) // 2), d))
    while s >> 16: s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", PORTA))
srv.listen(1)
srv.settimeout(120)
cavo, _ = srv.accept()
buf = b""


def manda_frame(f):
    if len(f) < 60: f += b"\0" * (60 - len(f))
    cavo.sendall(struct.pack(">I", len(f)) + f)


def manda_tcp(sp, dp, seq, ack, flag, dati=b"", fin=29200):
    tcp = struct.pack(">HHIIBBHHH", sp, dp, seq & 0xFFFFFFFF, ack & 0xFFFFFFFF, 5 << 4, flag, fin, 0, 0) + dati
    ps = NOI_IP + LUI_IP + struct.pack(">BBH", 0, 6, len(tcp))
    tcp = tcp[:16] + struct.pack(">H", somma(ps + tcp)) + tcp[18:]
    ip = struct.pack(">BBHHHBBH4s4s", 0x45, 0, 20 + len(tcp), 0, 0x4000, 64, 6, 0, NOI_IP, LUI_IP)
    ip = ip[:10] + struct.pack(">H", somma(ip)) + ip[12:]
    manda_frame(lui_mac + NOI_MAC + b"\x08\x00" + ip + tcp)


def leggi_frame(scade):
    global buf, lui_mac
    while True:
        while len(buf) >= 4 and len(buf) >= 4 + struct.unpack(">I", buf[:4])[0]:
            n = struct.unpack(">I", buf[:4])[0]
            f, buf = buf[4:4 + n], buf[4 + n:]
            if f[12:14] == b"\x08\x06" and f[38:42] == NOI_IP and f[20:22] == b"\0\1":
                lui_mac = f[6:12]          # who-has 10.9.0.2: it is us
                manda_frame(f[6:12] + NOI_MAC + b"\x08\x06" + f[14:20] + b"\0\2" +
                            NOI_MAC + NOI_IP + f[22:28] + f[28:32])
                continue
            return f
        resto = scade - time.time()
        if resto <= 0: return None
        cavo.settimeout(resto)
        try:
            d = cavo.recv(65536)
        except socket.timeout:
            return None
        if not d: raise SystemExit("il cavo e' caduto")
        buf += d


def segmento(sp, secondi=2.0):
    """the next TCP segment from the guest to our port sp: (seq, ack, flag, data)"""
    global lui_mac
    scade = time.time() + secondi
    while True:
        f = leggi_frame(scade)
        if f is None: return None
        if f[12:14] != b"\x08\x00" or f[23] != 6 or f[30:34] != NOI_IP: continue
        lui_mac = f[6:12]
        ihl = (f[14] & 15) * 4
        tot = struct.unpack(">H", f[16:18])[0]
        t = f[14 + ihl:14 + tot]
        dp = struct.unpack(">H", t[2:4])[0]
        if dp != sp: continue
        seq, ack = struct.unpack(">II", t[4:12])
        off = (t[12] >> 4) * 4
        return seq, ack, t[13], t[off:]


no = 0


def esito(c, m):
    global no
    print("  [%s] %s" % ("OK" if c else "NO", m), flush=True)
    if not c: no += 1


# wait for telnetd
for _ in range(200):
    try:
        if b"telnetd: in ascolto" in open(SERIALE, "rb").read(): break
    except OSError:
        pass
    leggi_frame(time.time() + 1)
time.sleep(1)
# who are you: a gratuitous ARP from us, and the guest's MAC from its answer
manda_frame(b"\xff" * 6 + NOI_MAC + b"\x08\x06" + b"\0\1\x08\x00\x06\x04\0\1" + NOI_MAC + NOI_IP +
            b"\0" * 6 + LUI_IP)
f = leggi_frame(time.time() + 3)
while f is not None and not (f[12:14] == b"\x08\x06" and f[20:22] == b"\0\2"):
    f = leggi_frame(time.time() + 3)
if f is not None: lui_mac = f[6:12]

# 1. ISN
isn = []
for i in range(5):
    sp = 40001 + i
    manda_tcp(sp, 23, 7000 + i, 0, 0x02)
    r = segmento(sp)
    if r and r[2] & 0x12 == 0x12:
        isn.append(r[0])
        manda_tcp(sp, 23, 7001 + i, r[0] + 1, 0x04)        # RST, the exact number
    time.sleep(0.5)
salti = [(isn[i + 1] - isn[i]) & 0xFFFFFFFF for i in range(len(isn) - 1)]
print("      ISN: %s" % " ".join("%08x" % x for x in isn), flush=True)
esito(len(isn) == 5, "cinque SYN+ACK")
esito(sum(1 for x in salti if x < 200000) <= 1,
      "ISN non in fila col tempo (salti %s)" % " ".join(str(x) for x in salti))

# 2. one real connection
sp = 40100
mio = 123456
manda_tcp(sp, 23, mio, 0, 0x02)
r = segmento(sp)
esito(r is not None and r[2] & 0x12 == 0x12, "connessione aperta")
suo = r[0] + 1
mio += 1
manda_tcp(sp, 23, mio, suo, 0x10)


def bevi(secondi=1.5):
    """take what telnetd sends and ACK it; the last ACK number it sent"""
    global suo
    ultimo = None
    while True:
        r = segmento(sp, secondi)
        if r is None: return ultimo
        seq, ack, fl, d = r
        if fl & 0x04: return "RST"
        ultimo = ack
        if d and seq == suo:
            suo += len(d)
            manda_tcp(sp, 23, mio, suo, 0x10)


def vivo(nome):
    """a byte from us: the connection is there if it gets ACKed"""
    global mio
    manda_tcp(sp, 23, mio, suo, 0x18, b"\r")
    mio += 1
    a = bevi(2.5)
    esito(a not in (None, "RST") and ((a - mio) & 0xFFFFFFFF) < 0x80000000 and a == mio,
          "%s: la connessione c'e' ancora (ack %s)" % (nome, a))


bevi(3)
vivo("dopo l'apertura")

manda_tcp(sp, 23, mio + 1000000, 0, 0x04)
time.sleep(0.5)
vivo("RST fuori finestra")

manda_tcp(sp, 23, mio + 10, 0, 0x04)
r = segmento(sp, 1.5)
esito(r is not None and r[2] & 0x10 and r[1] == mio and not (r[2] & 0x04),
      "RST in finestra ma non esatto: challenge ACK (%s)" % (r[:3] if r else None,))
bevi(1)
vivo("RST in finestra")

manda_tcp(sp, 23, mio, suo + 100000, 0x10)
r = segmento(sp, 1.5)
esito(r is not None and r[1] == mio and not (r[2] & 0x04),
      "ACK di byte mai mandati: challenge ACK (%s)" % (r[:3] if r else None,))
bevi(1)
vivo("ACK falso")

manda_tcp(sp, 23, 99999, 0, 0x02)
r = segmento(sp, 1.5)
esito(r is not None and r[1] == mio and not (r[2] & 0x04),
      "SYN sulla connessione aperta: challenge ACK (%s)" % (r[:3] if r else None,))
bevi(1)
vivo("SYN ripetuto")

manda_tcp(sp, 23, mio + 3000000, suo, 0x18, b"rm -r /\r")
r = segmento(sp, 1.5)
esito(r is not None and r[1] == mio, "dati fuori finestra: un ACK col numero vero (%s)" % (r[:3] if r else None,))
bevi(1)
vivo("dati fuori finestra")

manda_tcp(sp, 23, mio, 0, 0x04)
time.sleep(0.8)
manda_tcp(sp, 23, mio, suo, 0x18, b"\r")
r = None
for _ in range(5):
    r = segmento(sp, 1.5)
    if r is None or r[2] & 0x04: break
esito(r is not None and r[2] & 0x04, "RST col numero esatto: la connessione non c'e' piu'")

# 3. DNS
def manda_udp(da_ip, sp, dp, dati):
    udp = struct.pack(">HHHH", sp, dp, 8 + len(dati), 0) + dati
    ip = struct.pack(">BBHHHBBH4s4s", 0x45, 0, 20 + len(udp), 0, 0x4000, 64, 17, 0, da_ip, LUI_IP)
    ip = ip[:10] + struct.pack(">H", somma(ip)) + ip[12:]
    manda_frame(lui_mac + NOI_MAC + b"\x08\x00" + ip + udp)


def nome(n):
    return b"".join(bytes([len(x)]) + x for x in n.split(b".")) + b"\0"


def rr_a(proprio, ip4):
    return proprio + struct.pack(">HHIH", 1, 1, 60, 4) + bytes(ip4)


domande = []
fine_dns = time.time() + 70
while time.time() < fine_dns and len(domande) < 4:
    f = leggi_frame(time.time() + 2)
    if f is None or f[12:14] != b"\x08\x00" or f[23] != 17 or f[30:34] != NOI_IP: continue
    ihl = (f[14] & 15) * 4
    u = f[14 + ihl:]
    sp, dp = struct.unpack(">HH", u[:4])
    if dp != 53: continue
    q = u[8:8 + struct.unpack(">H", u[4:6])[0] - 8]
    qid = struct.unpack(">H", q[:2])[0]
    qfine = q.index(b"\0", 12) + 5
    domanda = q[12:qfine]
    qnome = q[12:qfine - 4]
    domande.append((qid, sp, qnome))
    testa = lambda an: struct.pack(">HHHHHH", qid, 0x8180, 1, an, 0, 0)
    if qnome == nome(b"prova.exos"):
        # ! 0.3 s apart: ip.drv keeps no queue for a UDP port nobody is
        # reading (by design, see "Nessuno sta aspettando" in ip.c), and the
        # resolver books the next datagram only after looking at this one; in
        # a burst the true answer would be dropped before the booking
        manda_udp(bytes([10, 9, 0, 3]), 53, sp, testa(1) + domanda + rr_a(b"\xc0\x0c", [6, 6, 6, 6]))
        time.sleep(0.3)
        altra = nome(b"altro.exos") + b"\0\1\0\1"
        manda_udp(NOI_IP, 53, sp, testa(1) + altra + rr_a(b"\xc0\x0c", [6, 6, 6, 7]))
        time.sleep(0.3)
        manda_udp(NOI_IP, 53, sp, testa(2) + domanda + rr_a(nome(b"altro.exos"), [6, 6, 6, 8]) +
                  rr_a(b"\xc0\x0c", [10, 9, 0, 77]))
    elif qnome == nome(b"alias.exos"):
        vero = nome(b"vero.exos")
        manda_udp(NOI_IP, 53, sp, testa(3) + domanda + rr_a(nome(b"altro.exos"), [6, 6, 6, 9]) +
                  b"\xc0\x0c" + struct.pack(">HHIH", 5, 1, 60, len(vero)) + vero +
                  rr_a(vero, [10, 9, 0, 78]))
    else:
        manda_udp(NOI_IP, 53, sp, struct.pack(">HHHHHH", qid, 0x8183, 1, 0, 0, 0) + domanda)
print("      DNS: " + " ".join("id %04x porta %u" % (a, b) for a, b, c in domande), flush=True)
esito(len(domande) >= 3, "il risolutore ha chiesto (%d domande)" % len(domande))
# one port per question: a retry of the same question keeps id and port
porte = []
for a, b, c in domande:
    if (a, b) not in [(x, y) for x, y, z in domande[:domande.index((a, b, c))]]: porte.append(b)
esito(len(porte) >= 2 and all(abs(porte[i + 1] - porte[i]) > 1 for i in range(len(porte) - 1)),
      "porte UDP non in fila (%s)" % porte)
time.sleep(4)
testo = open(SERIALE, "rb").read().replace(b"\0", b"")
esito(b"10.9.0.77" in testo, "prova.exos: l'indirizzo vero (10.9.0.77)")
esito(b"10.9.0.78" in testo, "alias.exos: l'indirizzo dietro il CNAME (10.9.0.78)")
esito(b"6.6.6." not in testo, "nessun indirizzo falso creduto")

print("prova_tcp_cieca: %d no" % no, flush=True)
cavo.close()
sys.exit(1 if no else 0)

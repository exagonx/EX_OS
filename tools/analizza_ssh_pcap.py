# analizza_ssh_pcap.py — ricompone i flussi TCP da un dump di QEMU e rifa' i
# conti dello scambio di chiavi SSH da fuori.
#
#     qemu ... -object filter-dump,id=f1,netdev=n1,file=/tmp/exos/ssh.pcap
#     python3 tools/analizza_ssh_pcap.py
#
# ! E' L'ATTREZZO CHE HA TROVATO IL DIFETTO DELLA FIRMA, e il metodo vale piu'
#   dello script: si confrontano i BUFFER, non gli hash. Un hash dice
#   «diverso», un buffer dice DOVE.
#
# ! I SEGMENTI VANNO RIMESSI IN ORDINE DI SEQUENZA E DEDUPLICATI (un dump ha
#   anche le ritrasmissioni), e la fine dei dati la dice l'intestazione IP, non
#   il frame: un frame corto viene riempito fino a 60 byte, e quel riempimento
#   letto come dati sono zeri in testa al flusso.

import struct, hashlib, ctypes
ex = ctypes.CDLL("./libex.so")

def flussi(pcap):
    """Ricompone i due versi del flusso TCP dal dump di QEMU."""
    d = open(pcap, 'rb').read()
    # intestazione pcap 24 byte, poi record: ts(8) caplen(4) len(4) dati
    p = 24; a = {}
    while p + 16 <= len(d):
        caplen = struct.unpack("<I", d[p+8:p+12])[0]
        pkt = d[p+16:p+16+caplen]; p += 16 + caplen
        if len(pkt) < 54: continue
        if pkt[12:14] != b'\x08\x00': continue          # IPv4
        ihl = (pkt[14] & 0xf) * 4
        if pkt[23] != 6: continue                        # TCP
        tcp = 14 + ihl
        sport, dport = struct.unpack(">HH", pkt[tcp:tcp+4])
        off = (pkt[tcp+12] >> 4) * 4
        # ! LA FINE DEI DATI LA DICE L'INTESTAZIONE IP, NON IL FRAME: un frame
        # Ethernet corto viene riempito fino a 60 byte, e quel riempimento
        # letto come dati sono zeri in testa al flusso.
        iptot = struct.unpack(">H", pkt[16:18])[0]
        dati = pkt[tcp+off:14+iptot]
        if not dati: continue
        seq = struct.unpack(">I", pkt[tcp+4:tcp+8])[0]
        a.setdefault((sport, dport), {})
        a[(sport, dport)][seq] = dati
    # ! I SEGMENTI SI RIMETTONO IN ORDINE DI SEQUENZA E SI DEDUPLICANO: un
    # dump contiene anche le ritrasmissioni, e concatenarle da' byte doppi.
    fuori = {}
    for k, seg in a.items():
        b = b""
        base = min(seg)
        for sq in sorted(seg):
            off = sq - base
            if off < len(b): continue
            b += seg[sq]
        fuori[k] = b
    return fuori

f = flussi("/tmp/exos/pr.pcap")
for k in f: print("flusso", k, len(f[k]), "byte")

# il verso del server (porta 22 sorgente) e quello del client
srv = b"".join(v for k, v in f.items() if k[0] == 22)
cli = b"".join(v for k, v in f.items() if k[1] == 22)

def riga_versione(b):
    i = b.index(b"\n")
    v = b[:i]
    if v.endswith(b"\r"): v = v[:-1]
    return v, b[i+1:]

vs, srv = riga_versione(srv)
vc, cli = riga_versione(cli)
print("V_S =", vs)
print("V_C =", vc)

def pacchetto(b):
    tot = struct.unpack(">I", b[:4])[0]
    pad = b[4]
    return b[5:4+tot-pad], b[4+tot:]

i_s, srv = pacchetto(srv)
i_c, cli = pacchetto(cli)
print("I_S:", len(i_s), "byte, tipo", i_s[0])
print("I_C:", len(i_c), "byte, tipo", i_c[0])

ecdh_init, cli = pacchetto(cli)
print("ECDH_INIT tipo", ecdh_init[0])
qc = ecdh_init[5:5+32]

reply, srv = pacchetto(srv)
print("ECDH_REPLY tipo", reply[0], "lungo", len(reply))
p = 1
ks_len = struct.unpack(">I", reply[p:p+4])[0]; p += 4
ks = reply[p:p+ks_len]; p += ks_len
qs_len = struct.unpack(">I", reply[p:p+4])[0]; p += 4
qs = reply[p:p+qs_len]; p += qs_len
sig_len = struct.unpack(">I", reply[p:p+4])[0]; p += 4
sig = reply[p:p+sig_len]
print("K_S:", ks_len, "Q_S:", qs_len, "firma:", sig_len)

# K con la privata effimera (era tutta zeri)
priv = bytes.fromhex("62ffd45941d0c90b6248e101283dabf476df6b7c7074cde39cebb3c589e1b4ed")
K = ctypes.create_string_buffer(32)
ex.x25519(K, priv, qc)
K = K.raw

def s_(b): return struct.pack(">I", len(b)) + b
def mpint(b):
    b = b.lstrip(b'\0')
    if not b: return struct.pack(">I", 0)
    if b[0] & 0x80: b = b'\0' + b
    return struct.pack(">I", len(b)) + b

H = hashlib.sha256(s_(vc)+s_(vs)+s_(i_c)+s_(i_s)+s_(ks)+s_(qc)+s_(qs)+mpint(K)).digest()
print("H calcolato qui:", H.hex()[:32], "...")
print("(sshd aveva detto  d78d4e109af4e2d217b4504db848f6b1 ...)")

# e la firma?
pub = ks[4+11+4:]
firma = sig[4+11+4:]
r = ex.ed25519_verifica(firma, H, 32, pub)
print("la firma verifica sull'H calcolato qui:", r == 0)

print()
print("K calcolato qui:", K.hex()[:16], "  (sshd: 789bb5fc174b6425)")
print("Q_S dal filo:   ", qs.hex()[:16])
import hashlib as H2
for nome, val in (("V_C",vc),("V_S",vs),("I_C",i_c),("I_S",i_s),("K_S",ks),("Q_C",qc),("Q_S",qs)):
    print("%-4s %5d byte  sha256 %s" % (nome, len(val), H2.sha256(val).hexdigest()[:16]))

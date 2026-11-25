#!/usr/bin/env python3
"""Signs versione.txt for netupdate (debug/bug_NETUPD001).

    firma.py crea                 makes the private key ($EXOS_CHIAVE), once
    firma.py pubblica [file.h]    the public key as the C header netupdate uses
    firma.py pub <file>           the public key as 32 raw bytes (for
                                  /boot/netupdate.pub on someone else's repo)
    firma.py firma <versione.txt> [giorni]
                                  adds "serie", "scade" and, last, "firma"

The signature is the LAST line of versione.txt, "firma = <128 hex>", and covers
every byte before that line (ed25519, RFC 8032, whole message, no prehash: what
lib/excrypt/ed25519_verifica checks). One file, so the server never holds a new
versione.txt with an old signature, and old netupdates skip the unknown keys.

The key lives OUTSIDE the project and outside MEGA: by default
~/ex_os/chiavi/netupdate-ed25519.key (32 bytes, 0600). Anyone who builds their
own repository from a GitHub clone makes their own with "crea" and puts its
public half in /boot/netupdate.pub on his machines.
"""
import os
import sys
import time

try:
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    from cryptography.hazmat.primitives import serialization
except ImportError:
    sys.exit("firma.py: manca il modulo python cryptography "
             "(apt install python3-cryptography)")

CHIAVE = os.environ.get("EXOS_CHIAVE") or os.path.join(
    os.environ.get("EXOS_FUORI") or os.path.expanduser("~/ex_os"),
    "chiavi", "netupdate-ed25519.key")
GIORNI = 30


def privata():
    try:
        seme = open(CHIAVE, "rb").read()
    except OSError as e:
        sys.exit("firma.py: non leggo la chiave %s (%s); la prima volta: "
                 "firma.py crea" % (CHIAVE, e.strerror))
    if len(seme) != 32:
        sys.exit("firma.py: %s non e' una chiave ed25519 (32 byte)" % CHIAVE)
    return Ed25519PrivateKey.from_private_bytes(seme)


def pubblica_byte():
    return privata().public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def crea():
    if os.path.exists(CHIAVE):
        sys.exit("firma.py: %s c'e' gia': non la sovrascrivo (le macchine "
                 "installate conoscono quella)" % CHIAVE)
    os.makedirs(os.path.dirname(CHIAVE), mode=0o700, exist_ok=True)
    seme = Ed25519PrivateKey.generate().private_bytes(
        serialization.Encoding.Raw, serialization.PrivateFormat.Raw,
        serialization.NoEncryption())
    fd = os.open(CHIAVE, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    os.write(fd, seme)
    os.close(fd)
    print("chiave privata in %s (0600): tienila fuori dal progetto e "
          "fanne una copia su un supporto separato" % CHIAVE)


def intestazione(dove):
    p = pubblica_byte()
    righe = ["/* The public key versione.txt is signed with (tools/netinst/firma.py",
             " * pubblica). Generated: do not edit by hand. */",
             "static const unsigned char CHIAVE_UFFICIALE[32] = {"]
    for i in range(0, 32, 8):
        righe.append("    " + ", ".join("0x%02x" % b for b in p[i:i + 8]) + ",")
    righe.append("};")
    testo = "\n".join(righe) + "\n"
    if dove:
        open(dove, "w", encoding="ascii").write(testo)
    else:
        sys.stdout.write(testo)


def firma(percorso, giorni):
    dati = open(percorso, "rb").read()
    # an old signature, serie and scade go: they are written again here
    righe = [r for r in dati.split(b"\n")
             if not r.split(b"=")[0].strip() in (b"firma", b"serie", b"scade")]
    while righe and righe[-1] == b"":
        righe.pop()
    ora = int(time.time())
    righe.append(b"serie     = %d" % ora)
    righe.append(b"scade     = %d" % (ora + giorni * 86400))
    corpo = b"\n".join(righe) + b"\n"
    sig = privata().sign(corpo)
    tmp = percorso + ".tmp"
    open(tmp, "wb").write(corpo + b"firma     = " + sig.hex().encode() + b"\n")
    os.replace(tmp, percorso)


def main(a):
    if len(a) >= 1 and a[0] == "crea":
        crea()
    elif len(a) >= 1 and a[0] == "pubblica":
        intestazione(a[1] if len(a) > 1 else None)
    elif len(a) == 2 and a[0] == "pub":
        open(a[1], "wb").write(pubblica_byte())
    elif len(a) >= 2 and a[0] == "firma":
        firma(a[1], int(a[2]) if len(a) > 2 else GIORNI)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])

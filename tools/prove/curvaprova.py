#!/usr/bin/env python3
# =============================================================================
# tools/prove/curvaprova.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# ECDSA su P-256 e P-384: le firme le FA openssl, noi le guardiamo.
#
# ! IL RIFERIMENTO E' CHI FIRMA, non un vettore copiato. Un verificatore si
# prova solo contro firme vere fatte da qualcun altro — e soprattutto contro
# firme ROVINATE, perche' un verificatore che dice sempre «si'» passa tutte
# quelle buone senza fare una piega.
#
#     python3 tools/prove/curvaprova.py
# =============================================================================

import os, subprocess, sys, tempfile, hashlib, re, shutil

RADICE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BANCO  = tempfile.mkdtemp(prefix="curvaprova-")
ESE    = os.path.join(BANCO, "curvaprova")

passate = fallite = 0

def esito(nome, ok, extra=""):
    global passate, fallite
    if ok:
        passate += 1
        print("  [ok]     " + nome)
    else:
        fallite += 1
        print("  [FALLITO] " + nome + ("   " + extra if extra else ""))

def compila():
    c = ["cc", "-Wall", "-Wextra", "-O1", "-o", ESE,
         "tools/prove/curvaprova.c", "lib/excurva/excurva.c",
         "lib/exbig/exbig.c",
         "-I", "lib/excurva", "-I", "lib/exbig"]
    r = subprocess.run(c, cwd=RADICE, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr)
        sys.exit(1)

def sh(*a, **kw):
    return subprocess.run(a, cwd=BANCO, capture_output=True, **kw)

def coppia(i, curva="prime256v1", impronta="sha256"):
    """Una chiave, un messaggio, la firma: rende (pub, impronta, r, s).

    ! LA CURVA E L'IMPRONTA SI SCELGONO SEPARATAMENTE, ed e' il caso che conta:
    sul web una chiave P-256 firmata con SHA-384 e' normalissima, e chi prova
    solo le combinazioni «coerenti» non se ne accorge mai."""
    k = "k%d.pem" % i
    sh("openssl", "ecparam", "-name", curva, "-genkey", "-noout", "-out", k)

    msg = ("messaggio numero %d per EX-OS" % i).encode()
    open(os.path.join(BANCO, "m%d" % i), "wb").write(msg)
    h = hashlib.new(impronta, msg).digest()

    sh("openssl", "dgst", "-" + impronta, "-sign", k, "-out", "f%d.der" % i, "m%d" % i)

    pub = sh("openssl", "ec", "-in", k, "-pubout", "-text", "-noout",
             text=True).stdout
    m = re.search(r"pub:\s*((?:\s*[0-9a-f]{2}:?\n?)+)", pub)
    q = re.sub(r"[^0-9a-f]", "", m.group(1))

    der = open(os.path.join(BANCO, "f%d.der" % i), "rb").read()
    j = 2 + ((der[1] & 0x7f) if der[1] & 0x80 else 0)
    ln = der[j + 1]; r = der[j + 2:j + 2 + ln]; j = j + 2 + ln
    ln = der[j + 1]; s = der[j + 2:j + 2 + ln]

    return q, h.hex(), r.hex(), s.hex()

def verifica(q, h, r, s, curva="256"):
    return subprocess.run([ESE, curva, q, h, r, s], cwd=BANCO).returncode == 0

def gira(hexs, quale=0):
    """Gira un bit dentro una stringa esadecimale."""
    b = bytearray.fromhex(hexs)
    b[quale % len(b)] ^= 0x01
    return bytes(b).hex()

def main():
    print("ECDSA P-256: firme di openssl, verifica nostra\n")
    compila()

    buone = 0
    for i in range(6):
        q, h, r, s = coppia(i)
        if verifica(q, h, r, s):
            buone += 1
    esito("P-256/SHA-256: sei firme vere, tutte verificate",
          buone == 6, "%d su 6" % buone)

    # ! LE COMBINAZIONI MISTE SONO QUELLE CHE IL WEB USA DAVVERO.
    q3, h3, r3, s3 = coppia(20, "prime256v1", "sha384")
    esito("P-256 con impronta SHA-384", verifica(q3, h3, r3, s3))

    q4, h4, r4, s4 = coppia(21, "secp384r1", "sha384")
    esito("P-384 con impronta SHA-384", verifica(q4, h4, r4, s4, "384"))

    q5, h5, r5, s5 = coppia(22, "secp384r1", "sha256")
    esito("P-384 con impronta SHA-256", verifica(q5, h5, r5, s5, "384"))

    esito("una firma P-384 letta come P-256 si rifiuta",
          not verifica(q4, h4, r4, s4, "256"))

    q, h, r, s = coppia(100)

    esito("un bit girato in r si rifiuta",       not verifica(q, h, gira(r, 3), s))
    esito("un bit girato in s si rifiuta",       not verifica(q, h, r, gira(s, 5)))
    esito("un bit girato nell'impronta si rifiuta", not verifica(q, gira(h, 7), r, s))
    esito("un bit girato nella chiave si rifiuta",
          not verifica(q[:2] + gira(q[2:], 9), h, r, s))

    esito("r a zero si rifiuta", not verifica(q, h, "00", s))
    esito("s a zero si rifiuta", not verifica(q, h, r, "00"))

    # La chiave di un'altra coppia: firma valida, ma non sua.
    q2, _, _, _ = coppia(101)
    esito("la chiave di un altro si rifiuta", not verifica(q2, h, r, s))

    # Un punto che non sta sulla curva: si cambia solo la Y.
    esito("un punto fuori dalla curva si rifiuta",
          not verifica(q[:66] + gira(q[66:], 1), h, r, s))

    print("\n%d prove superate, %d fallite" % (passate, fallite))
    shutil.rmtree(BANCO, ignore_errors=True)
    return 1 if fallite else 0

if __name__ == "__main__":
    sys.exit(main())

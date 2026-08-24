#!/usr/bin/env python3
# =============================================================================
# tools/prove/tlsprova.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Prova HMAC, HKDF e RSA-PSS contro riferimenti che non abbiamo scritto noi.
#
# ! I VETTORI DELLE RFC SONO IL RIFERIMENTO PIU' SOLIDO CHE ESISTA: numeri
# stampati dentro uno standard, calcolati da altri, controllati da vent'anni di
# implementazioni. Qui ci sono quelli della RFC 4231 (HMAC-SHA256) e della RFC
# 5869 (HKDF-SHA256), copiati a mano dal testo.
#
# ! E POI IL CONFRONTO CON `hmac` DI PYTHON su dati a caso, perche' i vettori
# sono pochi e tutti corti: le chiavi piu' lunghe del blocco, i messaggi
# vuoti, i pezzi di chiave lunghi mille byte non stanno in nessuna RFC.
#
# ! PER PSS IL RIFERIMENTO E' OPENSSL, che le firme le FA: si firma con
# `openssl pkeyutl -pkeyopt rsa_padding_mode:pss` e si chiede a noi di
# verificarle. Poi si rovinano, perche' un verificatore che dice sempre «si'»
# passa tutte le firme buone.
#
#     python3 tools/prove/tlsprova.py
# =============================================================================

import hashlib, hmac as pyhmac, os, subprocess, sys, tempfile

RADICE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BANCO  = "/tmp/tlsprova"

def compila():
    c = ["cc", "-Wall", "-Wextra", "-O2", "-o", BANCO,
         "tools/prove/tlsprova.c", "lib/extls/extls_kdf.c",
         "lib/extls/extls_pss.c", "lib/exbig/exbig.c",
         "-I", "lib/extls", "-I", "lib/exbig", "-lcrypto"]
    r = subprocess.run(c, cwd=RADICE, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr)
        sys.exit("tlsprova: il banco non compila")

def chiedi(righe):
    r = subprocess.run([BANCO], input="\n".join(righe) + "\n",
                       capture_output=True, text=True)
    return r.stdout.strip().split("\n")

def hx(b):
    return b.hex() if b else "-"

male = []

def controlla(nome, avuto, atteso):
    if avuto != atteso:
        male.append("%s: %s != %s" % (nome, avuto, atteso))

# --- RFC 4231: HMAC-SHA256 ---------------------------------------------------
RFC4231 = [
    ("0b"*20, "4869205468657265",
     "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"),
    ("4a656665", "7768617420646f2079612077616e7420666f72206e6f7468696e673f",
     "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"),
    ("aa"*20, "dd"*50,
     "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"),
    ("aa"*131,
     "54657374205573696e67204c6172676572205468616e20426c6f636b2d53697a"
     "65204b6579202d2048617368204b6579204669727374",
     "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"),
]

# --- RFC 5869: HKDF-SHA256 ---------------------------------------------------
RFC5869 = [
    # (ikm, sale, info, quanti, prk, okm)
    ("0b"*22, "000102030405060708090a0b0c", "f0f1f2f3f4f5f6f7f8f9", 42,
     "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5",
     "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
     "34007208d5b887185865"),
    ("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
     "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
     "404142434445464748494a4b4c4d4e4f",
     "606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f"
     "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f"
     "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf",
     "b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf"
     "d0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeef"
     "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", 82,
     "06a6b88c5853361a06104c9ceb35b45cef760014904671014a193f40c15fc244",
     "b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c"
     "59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71"
     "cc30c58179ec3e87c14c01d5c1f3434f1d87"),
    ("0b"*22, "-", "-", 42,
     "19ef24a32c717b167f33a91d6f648bdf96596776afdb6377ac434c1c293ccb04",
     "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d"
     "9d201395faa4b61a96c8"),
]

def main():
    compila()

    # --- HMAC contro i vettori della RFC 4231 --------------------------------
    righe = ["hmac %s %s" % (k, m) for k, m, _ in RFC4231]
    for (k, m, atteso), avuto in zip(RFC4231, chiedi(righe)):
        controlla("RFC 4231 hmac", avuto, atteso)

    # --- HKDF contro i vettori della RFC 5869 --------------------------------
    righe = []
    for ikm, sale, info, quanti, prk, okm in RFC5869:
        righe.append("extract %s %s" % (sale, ikm))
    for (ikm, sale, info, quanti, prk, okm), avuto in zip(RFC5869, chiedi(righe)):
        controlla("RFC 5869 extract", avuto, prk)

    righe = ["expand %s %s %d" % (prk, info, quanti)
             for _, _, info, quanti, prk, _ in RFC5869]
    for (ikm, sale, info, quanti, prk, okm), avuto in zip(RFC5869, chiedi(righe)):
        controlla("RFC 5869 expand", avuto, okm)

    # --- e poi il caso che le RFC non coprono: dati a caso -------------------
    import random
    random.seed(20260824)
    righe, attesi = [], []
    for _ in range(300):
        k = os.urandom(random.choice([0, 1, 31, 32, 64, 65, 200]))
        m = os.urandom(random.randrange(0, 400))
        righe.append("hmac %s %s" % (hx(k) if k else "-", hx(m) if m else "-"))
        attesi.append(pyhmac.new(k, m, hashlib.sha256).hexdigest())
    for nome, (avuto, atteso) in enumerate(zip(chiedi(righe), attesi)):
        controlla("hmac a caso %d" % nome, avuto, atteso)

    # --- HKDF-Expand-Label: la struttura di TLS 1.3 --------------------------
    #
    # ! IL RIFERIMENTO QUI E' LA RFC 8446 SCRITTA IN DIECI RIGHE DI PYTHON:
    # la struttura e' cosi' corta che riscriverla dallo standard e' piu'
    # affidabile che fidarsi della nostra. E l'HKDF sotto e' gia' provato
    # contro i vettori veri.
    def expand_label(segreto, etichetta, contesto, quanti):
        et = b"tls13 " + etichetta.encode()
        info = quanti.to_bytes(2, "big") + bytes([len(et)]) + et + \
               bytes([len(contesto)]) + contesto
        t, out = b"", b""
        c = 1
        while len(out) < quanti:
            t = pyhmac.new(segreto, t + info + bytes([c]), hashlib.sha256).digest()
            out += t
            c += 1
        return out[:quanti].hex()

    righe, attesi = [], []
    for et, ctx, quanti in (("derived", b"", 32), ("c hs traffic", os.urandom(32), 32),
                            ("key", b"", 16), ("iv", b"", 12),
                            ("finished", b"", 32), ("res master", os.urandom(32), 32)):
        seg = os.urandom(32)
        # ! L'ETICHETTA IN ESADECIMALE: «c hs traffic» ha degli SPAZI dentro,
        # e mandarla come testo faceva leggere al banco meta' riga.
        righe.append("label %s %s %s %d"
                     % (hx(seg), et.encode().hex(), hx(ctx) if ctx else "-", quanti))
        attesi.append(expand_label(seg, et, ctx, quanti))
    for (avuto, atteso) in zip(chiedi(righe), attesi):
        controlla("expand-label", avuto, atteso)

    # --- RSA-PSS: firme fatte da openssl -------------------------------------
    d = tempfile.mkdtemp(prefix="extls-")
    fatte = rifiutate = 0
    for bit in (2048, 3072):
        subprocess.run(["openssl", "genrsa", "-out", "%s/k%d.pem" % (d, bit),
                        str(bit)], capture_output=True)
        pub = subprocess.run(["openssl", "rsa", "-in", "%s/k%d.pem" % (d, bit),
                              "-pubout", "-noout", "-modulus"],
                             capture_output=True).stdout.decode()
        modulo = pub.strip().split("=", 1)[1].lower()

        for k in range(6):
            msg = os.urandom(100 + k)
            imp = hashlib.sha256(msg).digest()
            open("%s/m" % d, "wb").write(imp)
            firma = subprocess.run(
                ["openssl", "pkeyutl", "-sign", "-inkey", "%s/k%d.pem" % (d, bit),
                 "-in", "%s/m" % d, "-pkeyopt", "digest:sha256",
                 "-pkeyopt", "rsa_padding_mode:pss",
                 "-pkeyopt", "rsa_pss_saltlen:32"],
                capture_output=True).stdout
            if not firma:
                sys.exit("pss: openssl non ha firmato")

            r = chiedi(["pss %s 010001 %s %s 32" % (modulo, imp.hex(), firma.hex())])
            controlla("pss %d/%d" % (bit, k), r[0], "SI")
            fatte += 1

            # ! E ADESSO LA STESSA FIRMA ROVINATA: un bit. Un verificatore che
            # dice sempre «si'» passa tutte le firme buone.
            b = bytearray(firma)
            b[len(b) // 2] ^= 0x01
            r = chiedi(["pss %s 010001 %s %s 32" % (modulo, imp.hex(), bytes(b).hex())])
            controlla("pss rovinata %d/%d" % (bit, k), r[0], "NO")

            # impronta diversa: la firma e' quella giusta, il messaggio no
            r = chiedi(["pss %s 010001 %s %s 32"
                        % (modulo, hashlib.sha256(msg + b"x").hexdigest(),
                           firma.hex())])
            controlla("pss messaggio diverso %d/%d" % (bit, k), r[0], "NO")

            # sale di lunghezza diversa da quella pretesa
            r = chiedi(["pss %s 010001 %s %s 20" % (modulo, imp.hex(), firma.hex())])
            controlla("pss sale sbagliato %d/%d" % (bit, k), r[0], "NO")
            rifiutate += 3

    print("%d vettori RFC 4231/5869, 300 HMAC a caso, 6 expand-label" % (len(RFC4231) + len(RFC5869) * 2))
    print("%d firme PSS di openssl verificate, %d varianti rovinate rifiutate"
          % (fatte, rifiutate))
    if male:
        for m in male[:8]:
            print("MALE  " + m)
        print("... %d differenze" % len(male))
        sys.exit(1)
    print("nessuna differenza")

main()

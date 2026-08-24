#!/usr/bin/env python3
# =============================================================================
# tools/prove/bigprova.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Confronta exbig con gli interi di Python, che sono esatti.
#
# ! IL RIFERIMENTO DEV'ESSERE DI QUALCUN ALTRO. `pow(a, e, m)` e' la risposta
# giusta per definizione: se i due sono d'accordo su migliaia di numeri presi
# a caso, l'aritmetica e' quella. Confrontare exbig con exbig direbbe soltanto
# che e' coerente con se stesso.
#
# ! E I CASI NON SONO SOLO «A CASO». I numeri casuali non passano mai per i
# bordi: modulo di una parola sola, esponente 0 e 1, base 0 e 1, base m-1,
# moduli con la parola alta tutta a uno, esponenti con tutti i bit accesi.
# Quelli si scrivono a mano, e sono i posti dove un riporto sbagliato si vede.
#
#     python3 tools/prove/bigprova.py [quanti-casuali]
# =============================================================================

import random, subprocess, sys, os

RADICE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BANCO  = "/tmp/bigprova"

def compila():
    c = ["cc", "-Wall", "-Wextra", "-O2", "-o", BANCO,
         os.path.join(RADICE, "tools/prove/bigprova.c"),
         os.path.join(RADICE, "lib/exbig/exbig.c"),
         "-I", os.path.join(RADICE, "lib/exbig")]
    r = subprocess.run(c, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr)
        sys.exit("bigprova: il banco non compila")

def hx(n):
    s = "%x" % n
    return "0" + s if len(s) % 2 else s

def casi(quanti):
    """Rende (a, e, m, atteso) — atteso None se exbig deve RIFIUTARE."""
    fuori = []

    # --- i bordi, scritti a mano ---------------------------------------------
    m_piccolo = 0xFFFFFFFB                      # una parola sola, primo
    fuori += [(0, 65537, m_piccolo), (1, 65537, m_piccolo),
              (m_piccolo - 1, 65537, m_piccolo),
              (12345, 0, m_piccolo), (12345, 1, m_piccolo),
              (12345, 2, m_piccolo), (0, 0, m_piccolo),
              (7, 0xFFFFFFFF, m_piccolo),       # esponente con tutti i bit
              (2, 65537, 3), (1, 65537, 3)]     # modulo minuscolo
    # modulo con la parola alta tutta a uno: il posto dove il riporto sborda
    m_alto = (1 << 2048) - 1 - 4                # dispari
    fuori += [(m_alto - 1, 65537, m_alto), (2, 65537, m_alto)]
    # modulo di 4096 bit: il tetto dichiarato
    m_max = (1 << 4096) - 3
    fuori += [(m_max - 7, 65537, m_max)]

    # --- e poi i casuali, alle misure che si incontrano davvero ---------------
    for _ in range(quanti):
        bit = random.choice([32, 64, 128, 512, 1024, 2048, 3072, 4096])
        m = random.getrandbits(bit) | (1 << (bit - 1)) | 1     # dispari, pieno
        a = random.randrange(0, m)
        e = random.choice([65537, 3, 17, random.getrandbits(16) | 1,
                           random.getrandbits(64) | 1])
        fuori.append((a, e, m))
    return fuori

def main():
    quanti = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    compila()
    prove = casi(quanti)

    ingresso = "".join("%s %s %s\n" % (hx(a), hx(e), hx(m)) for a, e, m in prove)
    r = subprocess.run([BANCO], input=ingresso, capture_output=True, text=True)
    righe = r.stdout.strip().split("\n") if r.stdout.strip() else []

    if len(righe) != len(prove):
        sys.exit("bigprova: %d risposte per %d domande" % (len(righe), len(prove)))

    male = 0
    for (a, e, m), riga in zip(prove, righe):
        atteso = pow(a, e, m)
        try:
            avuto = int(riga, 16)
        except ValueError:
            print("MALE  %s: %s" % (riga, "a=%x e=%x m=%x" % (a, e, m)))
            male += 1
            continue
        if avuto != atteso:
            male += 1
            if male <= 3:
                print("MALE  a=%x\n      e=%x\n      m=%x\n      atteso %x\n      avuto  %x"
                      % (a, e, m, atteso, avuto))

    print("%d prove, %d sbagliate" % (len(prove), male))
    if male:
        sys.exit(1)

    # ! IL RIFIUTO SI PROVA COME IL RISULTATO: exbig deve dire di no a un
    # modulo pari, a uno zero e a una base >= m, e dirlo invece di ridurre in
    # silenzio — una firma piu' grande del modulo e' malformata, non da
    # aggiustare.
    brutti = [(5, 3, 8),      # modulo pari
              (5, 3, 0),      # modulo zero
              (9, 3, 9),      # base uguale al modulo
              (10, 3, 9),     # base maggiore del modulo
              (5, 65537, 3)]  # e una base grande su un modulo minuscolo
    ing = "".join("%s %s %s\n" % (hx(a), hx(e), hx(m)) for a, e, m in brutti)
    r = subprocess.run([BANCO], input=ing, capture_output=True, text=True)
    rig = r.stdout.strip().split("\n")
    for (a, e, m), riga in zip(brutti, rig):
        if riga != "RIFIUTATO":
            sys.exit("bigprova: a=%x e=%x m=%x doveva essere rifiutato, ha reso %s"
                     % (a, e, m, riga))
    print("%d casi malformati, tutti rifiutati" % len(brutti))

main()

# =============================================================================
# ! E POI LA PROVA CHE CONTA: UNA FIRMA VERA, DI UN CERTIFICATO VERO
#
# Migliaia di numeri a caso dicono che l'aritmetica e' giusta. Non dicono che
# serva a qualcosa. Questa parte prende i certificati radice che stanno su
# questa macchina — quelli che il browser di EX-OS dovra' verificare — e per
# ognuno che sia RSA autofirmato fa esattamente cio' che fara' extls:
#
#     m = firma^e mod n
#
# e guarda se ne esce la struttura di PKCS#1 v1.5: 00 01 FF..FF 00, poi il
# DigestInfo con dentro l'impronta del TBSCertificate. Se l'impronta combacia,
# la firma e' verificata — con la nostra aritmetica e nient'altro.
#
# ! IL CERTIFICATO E' AUTOFIRMATO APPOSTA: la chiave che lo firma e' la sua,
# quindi non serve nessun magazzino di CA per provare il conto. Il magazzino
# servira' a extls per decidere DI CHI FIDARSI, che e' un'altra domanda.
# =============================================================================
import glob, hashlib

def der_certificati(quanti):
    fuori = []
    for f in sorted(glob.glob("/etc/ssl/certs/*.pem")):
        d = subprocess.run(["openssl", "x509", "-in", f, "-outform", "DER"],
                           capture_output=True)
        if d.returncode != 0:
            continue
        fuori.append((os.path.basename(f), d.stdout))
        if len(fuori) >= quanti:
            break
    return fuori

def pezzi(der):
    """(tbs, n, e, firma, nome_impronta) oppure None se non e' RSA/sha256."""
    t = subprocess.run(["openssl", "asn1parse", "-inform", "DER", "-i"],
                       input=der, capture_output=True)
    t_out = t.stdout.decode("utf-8", "replace")
    if t.returncode != 0:
        return None
    # Il TBSCertificate e' la prima SEQUENCE dentro la SEQUENCE esterna.
    off = None
    for riga in t_out.split("\n")[1:]:
        if ":d=1" in riga and "SEQUENCE" in riga:
            off = int(riga.split(":")[0].strip())
            break
    if off is None:
        return None
    tbs = subprocess.run(["openssl", "asn1parse", "-inform", "DER",
                          "-strparse", str(off), "-out", "/dev/stdout",
                          "-noout"], input=der, capture_output=True)
    if tbs.returncode != 0 or not tbs.stdout:
        return None

    txt = subprocess.run(["openssl", "x509", "-inform", "DER", "-noout",
                          "-text"], input=der,
                         capture_output=True).stdout.decode("utf-8", "replace")
    if "sha256WithRSAEncryption" not in txt or "Exponent: 65537" not in txt:
        return None

    mod = subprocess.run(["openssl", "x509", "-inform", "DER", "-noout",
                          "-modulus"], input=der,
                         capture_output=True).stdout.decode("utf-8", "replace")
    if not mod.startswith("Modulus="):
        return None
    n = int(mod.strip().split("=", 1)[1], 16)

    # La firma e' l'ultimo BIT STRING della SEQUENCE esterna.
    ultimo = None
    for riga in t_out.split("\n"):
        if ":d=1" in riga and "BIT STRING" in riga:
            ultimo = int(riga.split(":")[0].strip())
    if ultimo is None:
        return None
    bs = subprocess.run(["openssl", "asn1parse", "-inform", "DER",
                         "-strparse", str(ultimo), "-out", "/dev/stdout",
                         "-noout"], input=der, capture_output=True).stdout
    if not bs:
        return None
    return tbs.stdout, n, 65537, int.from_bytes(bs, "big")

def prova_firme(quanti=12):
    fatte = saltate = 0
    for nome, der in der_certificati(80):
        p = pezzi(der)
        if p is None:
            saltate += 1
            continue
        tbs, n, e, firma = p
        if firma >= n or n.bit_length() > 4096:
            saltate += 1
            continue

        r = subprocess.run([BANCO], input="%s %s %s\n" % (hx(firma), hx(e), hx(n)),
                           capture_output=True, text=True)
        uscita = r.stdout.strip()
        if uscita in ("RIFIUTATO", "LETTURA", "ERRORE", ""):
            sys.exit("firma: %s -> %s" % (nome, uscita))

        # Il risultato e' lungo quanto il modulo, con lo zero davanti tolto:
        # si rimette, o il riempimento non si riconosce.
        m = int(uscita, 16).to_bytes((n.bit_length() + 7) // 8, "big")

        if m[0] != 0x00 or m[1] != 0x01:
            sys.exit("firma: %s non comincia con 00 01" % nome)
        i = 2
        while i < len(m) and m[i] == 0xFF:
            i += 1
        if m[i] != 0x00 or i < 10:
            sys.exit("firma: %s riempimento sbagliato" % nome)
        digestinfo = m[i + 1:]

        atteso = hashlib.sha256(tbs).digest()
        if not digestinfo.endswith(atteso):
            sys.exit("firma: %s l'impronta non combacia" % nome)
        fatte += 1
        if fatte >= quanti:
            break

    if fatte == 0:
        sys.exit("firma: nessun certificato RSA/sha256 da provare")
    print("%d firme di certificati veri verificate con exbig "
          "(%d saltati: non RSA-sha256 o esponente diverso)" % (fatte, saltate))

prova_firme()

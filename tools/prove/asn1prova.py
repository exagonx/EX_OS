#!/usr/bin/env python3
# =============================================================================
# tools/prove/asn1prova.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Confronta il lettore DER/X.509 di EX-OS con `openssl`, su TUTTI i certificati
# radice di questa macchina.
#
# ! IL RIFERIMENTO E' DI QUALCUN ALTRO, come per gli interi lunghi e per i
# font: qui e' openssl, che quei certificati li legge da vent'anni. E i
# certificati sono VERI — centocinquanta CA diverse, scritte da programmi
# diversi, con versioni, estensioni e stranezze che nessuno si inventerebbe
# scrivendo casi di prova a mano.
#
# ! E L'ULTIMA PROVA NON CONFRONTA NIENTE: VERIFICA. Presi modulo, esponente,
# firma e TBSCertificate DAL NOSTRO lettore, si rifa' il conto con exbig e si
# guarda se ne esce l'impronta giusta. Se combacia, tutti e quattro i campi
# sono giusti insieme — ed e' esattamente cio' che dovra' fare extls.
#
#     python3 tools/prove/asn1prova.py
# =============================================================================

import glob, hashlib, os, subprocess, sys

RADICE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BANCO  = "/tmp/asn1prova"
BIG    = "/tmp/bigprova"

def compila():
    for uscita, sorgenti, inc in (
        (BANCO, ["tools/prove/asn1prova.c", "lib/exasn1/exasn1.c"], "lib/exasn1"),
        (BIG,   ["tools/prove/bigprova.c",  "lib/exbig/exbig.c"],   "lib/exbig")):
        c = ["cc", "-Wall", "-Wextra", "-O2", "-o", uscita] + \
            [os.path.join(RADICE, s) for s in sorgenti] + \
            ["-I", os.path.join(RADICE, inc)]
        r = subprocess.run(c, capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stderr)
            sys.exit("asn1prova: %s non compila" % uscita)

def campi(der):
    r = subprocess.run([BANCO], input=der, capture_output=True)
    fuori = {}
    for riga in r.stdout.decode().strip().split("\n"):
        if riga == "RIFIUTATO":
            return None
        k, _, v = riga.partition(" ")
        fuori[k] = v
    return fuori

def oss(der, *args):
    return subprocess.run(["openssl", "x509", "-inform", "DER", "-noout"] +
                          list(args), input=der,
                          capture_output=True).stdout.decode("utf-8", "replace")

def data_openssl(s):
    """«May  5 09:37:37 2011 GMT» -> «20110505093737Z»"""
    mesi = {"Jan":"01","Feb":"02","Mar":"03","Apr":"04","May":"05","Jun":"06",
            "Jul":"07","Aug":"08","Sep":"09","Oct":"10","Nov":"11","Dec":"12"}
    p = s.split("=", 1)[1].strip().split()
    if len(p) < 4:
        return None
    return "%s%s%02d%s%s" % (p[3], mesi[p[0]], int(p[1]),
                             p[2].replace(":", ""), "Z")

ALG = {"sha256WithRSAEncryption": 1, "sha384WithRSAEncryption": 2,
       "sha512WithRSAEncryption": 3, "sha1WithRSAEncryption": 4,
       "ecdsa-with-SHA256": 5}
IMPRONTA = {1: hashlib.sha256, 2: hashlib.sha384, 3: hashlib.sha512}

def main():
    compila()
    file = sorted(glob.glob("/etc/ssl/certs/*.pem"))
    if not file:
        sys.exit("asn1prova: nessun certificato in /etc/ssl/certs")

    letti = rifiutati = rsa = ec = 0
    male = []
    verificate = 0

    for f in file:
        d = subprocess.run(["openssl", "x509", "-in", f, "-outform", "DER"],
                           capture_output=True)
        if d.returncode != 0:
            continue
        der = d.stdout
        nome = os.path.basename(f)

        c = campi(der)
        if c is None:
            rifiutati += 1
            male.append("%s: RIFIUTATO da noi, letto da openssl" % nome)
            continue
        letti += 1
        if int(c["tipo"]) == 1: rsa += 1
        elif int(c["tipo"]) == 2: ec += 1

        testo = oss(der, "-text")

        # --- il modulo, confrontato con quello che dice openssl --------------
        #
        # ! SOLO PER LE CHIAVI RSA. `openssl x509 -modulus` su un certificato a
        # curva ellittica non stampa un modulo — non ne ha uno — e chiedergli
        # di confrontarlo faceva dire «modulo diverso» a quarantatre
        # certificati perfettamente letti. Era la prova a sbagliare domanda.
        mod = oss(der, "-modulus").strip() if int(c["tipo"]) == 1 else ""
        if mod.startswith("Modulus="):
            atteso = mod.split("=", 1)[1].lower().lstrip("0")
            avuto  = c["modulo"].lstrip("0")
            if atteso != avuto:
                male.append("%s: modulo diverso" % nome)
            if c["esponente"] != "010001" and "Exponent: 65537" in testo:
                male.append("%s: esponente diverso" % nome)

        # --- il numero di serie ---------------------------------------------
        ser = oss(der, "-serial").strip()
        if ser.startswith("serial="):
            a = ser.split("=", 1)[1].lower().lstrip("0")
            b = c["serie"].lstrip("0")
            if a != b:
                male.append("%s: numero di serie %s != %s" % (nome, a, b))

        # --- le date ---------------------------------------------------------
        for chiave, opt in (("nonprima", "-startdate"), ("nondopo", "-enddate")):
            a = data_openssl(oss(der, opt).strip())
            if a and a != c[chiave]:
                male.append("%s: %s %s != %s" % (nome, chiave, a, c[chiave]))

        # --- l'algoritmo della firma ----------------------------------------
        for testo_alg, numero in ALG.items():
            if "Signature Algorithm: " + testo_alg in testo:
                if int(c["alg"]) != numero:
                    male.append("%s: algoritmo %s letto come %s"
                                % (nome, testo_alg, c["alg"]))
                break

        # --- basicConstraints -------------------------------------------------
        atteso_ca = 1 if "CA:TRUE" in testo else 0
        if int(c["ca"]) != atteso_ca:
            male.append("%s: CA %s, openssl dice %d" % (nome, c["ca"], atteso_ca))

        # --- i due nomi sono DER validi per conto loro ------------------------
        for chiave in ("emittente", "soggetto"):
            b = bytes.fromhex(c[chiave])
            r = subprocess.run(["openssl", "asn1parse", "-inform", "DER"],
                               input=b, capture_output=True)
            if r.returncode != 0:
                male.append("%s: %s non e' DER valido" % (nome, chiave))

        # --- E LA PROVA CHE CONTA: la firma, con i campi che abbiamo letto NOI
        alg = int(c["alg"])
        if (alg in IMPRONTA and int(c["tipo"]) == 1 and
                c["esponente"] == "010001" and int(c["stessonome"]) == 1):
            n = int(c["modulo"], 16)
            firma = int(c["firma"], 16)
            if firma < n and n.bit_length() <= 4096:
                r = subprocess.run([BIG], input="%s 010001 %s\n"
                                   % (c["firma"], c["modulo"]),
                                   capture_output=True, text=True)
                u = r.stdout.strip()
                if u in ("", "RIFIUTATO", "LETTURA", "ERRORE"):
                    male.append("%s: exbig ha detto %s" % (nome, u))
                    continue
                m = int(u, 16).to_bytes((n.bit_length() + 7) // 8, "big")
                imp = IMPRONTA[alg](bytes.fromhex(c["tbs"])).digest()
                if m[0] != 0 or m[1] != 1 or not m.endswith(imp):
                    male.append("%s: la firma non torna con i nostri campi" % nome)
                else:
                    verificate += 1

    print("%d certificati letti, %d rifiutati  (%d RSA, %d EC P-256, %d altro)"
          % (letti, rifiutati, rsa, ec, letti - rsa - ec))
    print("%d firme verificate con i campi letti da exasn1 e il conto di exbig"
          % verificate)
    if male:
        for m in male[:10]:
            print("MALE  " + m)
        print("... %d differenze in tutto" % len(male))
        sys.exit(1)
    print("nessuna differenza con openssl")
    guasta(der)

# =============================================================================
# ! E POI I CERTIFICATI GUASTI, CHE SONO IL CASO VERO
#
# I certificati veri sono scritti bene per definizione: chi li ha firmati aveva
# un motivo per farli leggere. Ma questo lettore i byte li prende DALLA RETE, e
# chi risponde al posto del sito che si voleva sceglie lui cosa mandare.
#
# Qui si prende un certificato buono e lo si rovina in tutti i modi che
# costano poco: tagliato in ogni punto, un byte cambiato a caso, e le
# LUNGHEZZE portate a valori assurdi — che e' il campo con cui si fa uscire un
# lettore dal proprio buffer.
#
# ! LA PROVA NON E' «LO RIFIUTA»: e' «NON SI SCHIANTA». Un certificato
# rovinato puo' benissimo restare leggibile — cambiare un byte dentro un nome
# da' un nome diverso, non un errore. Cio' che non deve MAI succedere e'
# leggere fuori dal buffer, e quello si vede dal segnale.
# =============================================================================
def guasta(der):
    import random

    random.seed(20260824)
    prove = 0
    for _ in range(1500):
        b = bytearray(der)
        modo = random.randrange(3)
        if modo == 0:                              # tagliato
            b = b[:random.randrange(0, len(b))]
        elif modo == 1:                            # un byte qualunque
            i = random.randrange(len(b))
            b[i] = random.randrange(256)
        else:                                      # una lunghezza assurda
            i = random.randrange(len(b) - 5)
            b[i] = 0x84                            # «quattro byte di lunghezza»
            for k in range(1, 5):
                b[i + k] = random.choice([0xFF, 0x7F, 0x00])
        r = subprocess.run([BANCO], input=bytes(b), capture_output=True,
                           timeout=10)
        if r.returncode != 0:
            open("/tmp/asn1-guasto.der", "wb").write(bytes(b))
            sys.exit("guasto: il lettore e' morto con %d "
                     "(il certificato e' in /tmp/asn1-guasto.der)" % r.returncode)
        prove += 1
    print("%d certificati rovinati, nessuno ha fatto uscire il lettore dal "
          "proprio buffer" % prove)

main()

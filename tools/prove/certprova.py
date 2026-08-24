#!/usr/bin/env python3
# =============================================================================
# tools/prove/certprova.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Costruisce una PKI vera con openssl — radice, intermedia, sito — e chiede a
# excert di verificarla. Poi la rovina in tutti i modi che contano e pretende
# che la rifiuti PER IL MOTIVO GIUSTO.
#
# ! UNA PROVA CHE GUARDA SOLO IL «SI'» NON PROVA NIENTE. Un verificatore che
# risponde sempre OK passa il caso buono: e' il caso cattivo che lo smaschera.
# E il motivo conta quanto il rifiuto — «scaduto» e «non mi fido» mandano chi
# legge in due posti diversi.
#
#     python3 tools/prove/certprova.py
# =============================================================================

import os, subprocess, sys, tempfile, datetime

RADICE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BANCO  = "/tmp/certprova"

MOTIVO = {0: "OK", -1: "MALFORMATO", -2: "FIRMA SBAGLIATA", -3: "NOME DIVERSO",
          -4: "NON E' UNA CA", -5: "SCADUTO", -6: "NON ANCORA VALIDO",
          -7: "SENZA RADICE", -8: "ALGORITMO RIFIUTATO", -9: "TROPPO LUNGA"}

def compila():
    c = ["cc", "-Wall", "-Wextra", "-O2", "-o", BANCO,
         "tools/prove/certprova.c", "lib/excert/excert.c",
         "lib/exasn1/exasn1.c", "lib/exbig/exbig.c",
         "-I", "lib/excert", "-I", "lib/exasn1", "-I", "lib/exbig", "-lcrypto"]
    r = subprocess.run(c, cwd=RADICE, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr)
        sys.exit("certprova: il banco non compila")

def oss(*a, **k):
    r = subprocess.run(["openssl"] + list(a), capture_output=True, **k)
    if r.returncode != 0:
        sys.exit("openssl " + " ".join(a[:2]) + ":\n" + r.stderr.decode())
    return r.stdout

def pki(d):
    """Radice -> intermedia -> sito, piu' le varianti che devono fallire."""
    def chiave(n):
        oss("genrsa", "-out", "%s/%s.key" % (d, n), "2048")

    def conf(n, ca, giorni_da=None, giorni_a=None):
        s = "[req]\ndistinguished_name=dn\nprompt=no\nx509_extensions=v3\n"
        s += "[dn]\nCN=EX-OS prova %s\nO=EX-OS\n" % n
        s += "[v3]\n"
        s += "basicConstraints=critical,CA:TRUE\n" if ca else "basicConstraints=critical,CA:FALSE\n"
        s += "keyUsage=critical,keyCertSign,cRLSign\n" if ca else "keyUsage=critical,digitalSignature\n"
        p = "%s/%s.cnf" % (d, n)
        open(p, "w").write(s)
        return p

    for n in ("radice", "media", "sito", "furba", "altra"):
        chiave(n)

    # radice autofirmata
    oss("req", "-x509", "-new", "-key", "%s/radice.key" % d, "-sha256",
        "-days", "3650", "-config", conf("radice", True),
        "-out", "%s/radice.pem" % d)
    # una seconda radice, che NON finira' nel magazzino
    oss("req", "-x509", "-new", "-key", "%s/altra.key" % d, "-sha256",
        "-days", "3650", "-config", conf("altra", True),
        "-out", "%s/altra.pem" % d)

    def firma(nome, chi, ca, extra=()):
        oss("req", "-new", "-key", "%s/%s.key" % (d, nome),
            "-config", conf(nome, ca), "-out", "%s/%s.csr" % (d, nome))
        oss("x509", "-req", "-in", "%s/%s.csr" % (d, nome),
            "-CA", "%s/%s.pem" % (d, chi), "-CAkey", "%s/%s.key" % (d, chi),
            "-CAcreateserial", "-days", "365", "-sha256",
            "-extfile", conf(nome, ca), "-extensions", "v3",
            "-out", "%s/%s.pem" % (d, nome), *extra)

    firma("media", "radice", True)
    firma("sito",  "media",  False)
    # ! una CA «furba»: un certificato di SITO che ne firma un altro. E' il
    # difetto piu' vecchio di X.509, e senza il controllo su basicConstraints
    # funziona.
    firma("furba", "media", False)
    oss("req", "-new", "-key", "%s/altra.key" % d, "-config", conf("finto", False),
        "-out", "%s/finto.csr" % d)
    oss("x509", "-req", "-in", "%s/finto.csr" % d, "-CA", "%s/furba.pem" % d,
        "-CAkey", "%s/furba.key" % d, "-CAcreateserial", "-days", "365",
        "-sha256", "-extfile", conf("finto", False), "-extensions", "v3",
        "-out", "%s/finto.pem" % d)

    # scaduto: firmato ieri, valido un giorno... openssl non fa date passate,
    # quindi si guarda con una data di «adesso» spostata avanti.
    for n in ("radice", "media", "sito", "furba", "finto", "altra"):
        oss("x509", "-in", "%s/%s.pem" % (d, n), "-outform", "DER",
            "-out", "%s/%s.der" % (d, n))

def prova(nome, attesa, data, radici, catena):
    r = subprocess.run([BANCO, data] + radici + ["--"] + catena,
                       capture_output=True, text=True)
    avuto = int(r.stdout.strip() or "999")
    ok = (avuto == attesa)
    print("%-4s %-34s %s" % ("ok" if ok else "MALE", nome,
                             MOTIVO.get(avuto, str(avuto))))
    return ok

def main():
    compila()
    d = tempfile.mkdtemp(prefix="excert-")
    pki(d)
    D = lambda n: "%s/%s.der" % (d, n)

    oggi   = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d%H%M%SZ")
    fra_2a = (datetime.datetime.now(datetime.timezone.utc) +
              datetime.timedelta(days=730)).strftime("%Y%m%d%H%M%SZ")
    dieci_anni_fa = "20150101000000Z"

    tutte = [
        prova("catena buona", 0, oggi, [D("radice")], [D("sito"), D("media")]),
        prova("radice non nel magazzino", -7, oggi, [D("altra")],
              [D("sito"), D("media")]),
        prova("magazzino vuoto", -7, oggi, [], [D("sito"), D("media")]),
        prova("senza l'intermedia", -7, oggi, [D("radice")], [D("sito")]),
        prova("un certificato di sito che firma", -4, oggi, [D("radice")],
              [D("finto"), D("furba"), D("media")]),
        prova("emittente e soggetto non combaciano", -3, oggi, [D("radice")],
              [D("sito"), D("altra")]),
        prova("scaduto (data spostata avanti)", -5, fra_2a, [D("radice")],
              [D("sito"), D("media")]),
        prova("non ancora valido (data indietro)", -6, dieci_anni_fa,
              [D("radice")], [D("sito"), D("media")]),
        # ! QUESTA E' UNA CATENA BUONA, E L'ASPETTATIVA SBAGLIATA ERA LA MIA.
        # Una catena fatta della sola radice, con quella radice NEL MAGAZZINO,
        # e' valida: il certificato presentato e' uno di cui ci si fida per
        # definizione. Non prova che chi risponde possieda la chiave — ma non
        # e' compito della catena: e' la CertificateVerify dell'handshake a
        # chiederlo, e un server che manda una radice non ce l'ha. Anche
        # openssl la considera valida.
        prova("solo la radice, che sta nel magazzino", 0, oggi,
              [D("radice")], [D("radice")]),
    ]

    # ! E LA FIRMA ROVINATA: un byte del TBSCertificate cambiato. La catena e'
    # la stessa, i nomi combaciano, le date sono buone — e la firma no.
    b = bytearray(open(D("sito"), "rb").read())
    b[len(b) // 3] ^= 0x01
    open("%s/rovinato.der" % d, "wb").write(bytes(b))
    tutte.append(prova("un byte cambiato nel certificato", -2, oggi,
                       [D("radice")], ["%s/rovinato.der" % d, D("media")]))

    print("\n%d prove, %d sbagliate" % (len(tutte), tutte.count(False)))
    if not all(tutte):
        sys.exit(1)

main()

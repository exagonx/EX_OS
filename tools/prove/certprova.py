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
          -7: "SENZA RADICE", -8: "ALGORITMO RIFIUTATO", -9: "TROPPO LUNGA",
          -10: "CHIAVE RIFIUTATA", -11: "ANNULLATA DA CHI CHIAMA",
          -12: "CRITICA IGNOTA", -13: "USO SBAGLIATO", -14: "NOME VINCOLATO"}

def compila():
    # ! excurva STA QUI DENTRO DA QUANDO excert SA VERIFICARE ECDSA, e per
    # settimane non c'e' stato: il banco non compilava piu' e `make prova-tls`
    # si fermava su «excurva.h: File o directory non esistente». Una prova che
    # non parte non e' una prova che passa — ed e' proprio quella che dice se
    # la catena dei certificati regge.
    c = ["cc", "-Wall", "-Wextra", "-O2", "-o", BANCO,
         "tools/prove/certprova.c", "lib/excert/excert.c",
         "lib/exasn1/exasn1.c", "lib/exbig/exbig.c", "lib/excurva/excurva.c",
         "-I", "lib/excert", "-I", "lib/exasn1", "-I", "lib/exbig",
         "-I", "lib/excurva", "-I", "lib/excrypt", "-lcrypto"]
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

    # ! LA RADICE INCROCIATA, com'e' quella che manda www.amazon.it: la STESSA
    # radice — stessa chiave, stesso nome — ma firmata da un'altra CA invece
    # che da se' stessa. Nel magazzino c'e' la radice vera; in coda alla
    # catena arriva questa copia, con byte diversi e un emittente che nel
    # magazzino non c'e'.
    oss("req", "-new", "-key", "%s/radice.key" % d, "-config", conf("radice", True),
        "-out", "%s/incrociata.csr" % d)
    oss("x509", "-req", "-in", "%s/incrociata.csr" % d, "-CA", "%s/altra.pem" % d,
        "-CAkey", "%s/altra.key" % d, "-CAcreateserial", "-days", "365",
        "-sha256", "-extfile", conf("radice", True), "-extensions", "v3",
        "-out", "%s/incrociata.pem" % d)

    # =========================================================================
    # ! WHAT A CA MAY DO (debug/bug_TLS001): until November 2026 a CA was
    # cA=TRUE and nothing else. Certificates with their own v3 section:
    # limits by name and by depth, uses that are not a TLS server's.
    # =========================================================================
    def su_misura(nome, chi, v3):
        chiave(nome)
        c = "%s/%s-v3.cnf" % (d, nome)
        open(c, "w").write("[req]\ndistinguished_name=dn\nprompt=no\n"
                           "[dn]\nCN=EX-OS prova %s\nO=EX-OS\n[v3]\n%s" % (nome, v3))
        oss("req", "-new", "-key", "%s/%s.key" % (d, nome), "-config", c,
            "-out", "%s/%s.csr" % (d, nome))
        oss("x509", "-req", "-in", "%s/%s.csr" % (d, nome),
            "-CA", "%s/%s.pem" % (d, chi), "-CAkey", "%s/%s.key" % (d, chi),
            "-CAcreateserial", "-days", "365", "-sha256",
            "-extfile", c, "-extensions", "v3", "-out", "%s/%s.pem" % (d, nome))
        oss("x509", "-in", "%s/%s.pem" % (d, nome), "-outform", "DER",
            "-out", "%s/%s.der" % (d, nome))

    CA = "basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n"
    FOGLIA = "basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n"
    su_misura("vinc", "radice", CA.replace("CA:TRUE", "CA:TRUE,pathlen:0") +
              "nameConstraints=critical,permitted;DNS:prova.lan\n")
    su_misura("dentro", "vinc", FOGLIA + "subjectAltName=DNS:www.prova.lan,DNS:*.prova.lan\n")
    su_misura("fuori", "vinc", FOGLIA + "subjectAltName=DNS:www.prova.lan,DNS:www.banca.com\n")
    su_misura("simile", "vinc", FOGLIA + "subjectAltName=DNS:xprova.lan\n")
    su_misura("sottoca", "vinc", CA)
    su_misura("nipote", "sottoca", FOGLIA + "subjectAltName=DNS:www.prova.lan\n")
    su_misura("escl", "radice", CA + "nameConstraints=critical,excluded;DNS:banca.com\n")
    su_misura("banca", "escl", FOGLIA + "subjectAltName=DNS:www.banca.com\n")
    su_misura("jolly", "escl", FOGLIA + "subjectAltName=DNS:*.com\n")
    su_misura("libera", "escl", FOGLIA + "subjectAltName=DNS:www.posta.it\n")
    su_misura("codice", "media", FOGLIA + "extendedKeyUsage=codeSigning\n")
    su_misura("server", "media", FOGLIA + "extendedKeyUsage=serverAuth,clientAuth\n")
    su_misura("inventata", "media", FOGLIA + "1.2.3.4=critical,ASN1:NULL\n")
    su_misura("ignota", "media", FOGLIA + "1.2.3.5=ASN1:NULL\n")
    su_misura("nokcs", "radice", "basicConstraints=critical,CA:TRUE\n"
              "keyUsage=critical,digitalSignature\n")
    su_misura("dinokcs", "nokcs", FOGLIA)
    su_misura("cifra", "media", "basicConstraints=critical,CA:FALSE\n"
              "keyUsage=critical,keyEncipherment\n")
    su_misura("mediacod", "radice", CA + "extendedKeyUsage=codeSigning\n")
    su_misura("dimediacod", "mediacod", FOGLIA)

    # scaduto: firmato ieri, valido un giorno... openssl non fa date passate,
    # quindi si guarda con una data di «adesso» spostata avanti.
    for n in ("radice", "media", "sito", "furba", "finto", "altra", "incrociata"):
        oss("x509", "-in", "%s/%s.pem" % (d, n), "-outform", "DER",
            "-out", "%s/%s.der" % (d, n))

def prova(nome, attesa, data, radici, catena, extra=()):
    r = subprocess.run([BANCO] + list(extra) + [data] + radici + ["--"] + catena,
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
        # ! IL CASO DI www.amazon.it (23 settembre 2026): la catena si ferma
        # alla prima radice nostra, e la copia incrociata in coda non conta.
        # Fino a quel giorno questa era «SENZA RADICE».
        prova("radice incrociata in coda, la vera nel magazzino", 0, oggi,
              [D("radice")], [D("sito"), D("media"), D("incrociata")]),
        # E con la sola «altra» nel magazzino e' buona lo stesso: la copia
        # incrociata E' firmata da lei. E' la strada che la catena ha fatto
        # davvero, fino a una radice di cui ci si fida.
        prova("radice incrociata, nel magazzino solo chi la firma", 0, oggi,
              [D("altra")], [D("sito"), D("media"), D("incrociata")]),
        prova("radice incrociata, nel magazzino nessuna delle due", -7, oggi,
              [D("furba")], [D("sito"), D("media"), D("incrociata")]),
    ]

    R = [D("radice")]
    tutte += [
        prova("nameConstraints: dentro (anche *.prova.lan)", 0, oggi, R, [D("dentro"), D("vinc")]),
        prova("nameConstraints: un nome fuori", -14, oggi, R, [D("fuori"), D("vinc")]),
        prova("nameConstraints: xprova.lan non e' prova.lan", -14, oggi, R, [D("simile"), D("vinc")]),
        prova("pathlen 0 con una CA sotto", -9, oggi, R, [D("nipote"), D("sottoca"), D("vinc")]),
        prova("nome escluso", -14, oggi, R, [D("banca"), D("escl")]),
        prova("jolly che copre il nome escluso", -14, oggi, R, [D("jolly"), D("escl")]),
        prova("fuori dall'escluso: buona", 0, oggi, R, [D("libera"), D("escl")]),
        prova("foglia per firmare il codice", -13, oggi, R, [D("codice"), D("media")]),
        prova("foglia serverAuth: buona", 0, oggi, R, [D("server"), D("media")]),
        prova("estensione critica inventata", -12, oggi, R, [D("inventata"), D("media")]),
        prova("estensione inventata non critica: buona", 0, oggi, R, [D("ignota"), D("media")]),
        prova("CA senza keyCertSign", -13, oggi, R, [D("dinokcs"), D("nokcs")]),
        prova("foglia senza digitalSignature", -13, oggi, R, [D("cifra"), D("media")]),
        prova("intermedia con EKU codeSigning", -13, oggi, R, [D("dimediacod"), D("mediacod")]),
    ]

    # ! E LA FIRMA ROVINATA: un byte del TBSCertificate cambiato. La catena e'
    # la stessa, i nomi combaciano, le date sono buone — e la firma no.
    # =========================================================================
    # IL GANCIO FRA UN ANELLO E L'ALTRO
    #
    # ! LA CATENA SI VERIFICA UN ANELLO PER VOLTA, e da oggi lo si puo' dire a
    # chi aspetta: su una catena di quattro anelli ECDSA erano 850 ms di
    # finestra ferma in un pezzo solo (@DIF-TLS). Il gancio non cambia il
    # verdetto — e queste due prove servono a tenerlo vero.
    #
    # ! DUE MODI DI ROMPERSI IN SILENZIO. Un gancio chiamato ZERO volte non si
    # vede: la catena resta valida e chi aspetta continua ad aspettare come
    # prima. E un annullamento scambiato per un rifiuto manderebbe a cercare
    # una CA che non manca. Percio' si contano le chiamate e si guarda il
    # codice, non solo il fatto che qualcosa sia fallito.
    tutte.append(prova("il gancio vede due anelli (sito+media, poi la radice)",
                       2, oggi, [D("radice")], [D("sito"), D("media")],
                       extra=("-conta",)))
    tutte.append(prova("annullata alla prima chiamata", -11, oggi,
                       [D("radice")], [D("sito"), D("media")],
                       extra=("-annulla", "1")))
    tutte.append(prova("annullata sull'ultima, quella della radice", -11, oggi,
                       [D("radice")], [D("sito"), D("media")],
                       extra=("-annulla", "2")))
    tutte.append(prova("e senza annullare resta buona", 0, oggi,
                       [D("radice")], [D("sito"), D("media")],
                       extra=("-annulla", "9")))

    b = bytearray(open(D("sito"), "rb").read())
    b[len(b) // 3] ^= 0x01
    open("%s/rovinato.der" % d, "wb").write(bytes(b))
    tutte.append(prova("un byte cambiato nel certificato", -2, oggi,
                       [D("radice")], ["%s/rovinato.der" % d, D("media")]))

    print("\n%d prove, %d sbagliate" % (len(tutte), tutte.count(False)))
    if not all(tutte):
        sys.exit(1)

main()

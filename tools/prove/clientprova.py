#!/usr/bin/env python3
# =============================================================================
# tools/prove/clientprova.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# La prova del cliente TLS 1.3: si parla con un server OpenSSL vero.
#
# ! IL CASO BUONO NON PROVA NIENTE DA SOLO. Una stretta di mano che riesce dice
# che il protocollo e' scritto giusto e non dice NULLA sulla verifica: un
# cliente che accetta qualunque certificato completa la stretta esattamente
# come uno corretto, e la barra scrive `https://` lo stesso. Per questo qui i
# casi cattivi sono la meta' che conta — nome sbagliato, radice sconosciuta,
# certificato scaduto, jolly che non deve allargarsi.
#
# ! E IL SERVER E' OPENSSL, non un finto server scritto da noi. Un banco di
# prova fatto in casa ripete gli stessi fraintendimenti del codice che prova.
#
#     python3 tools/prove/clientprova.py
# =============================================================================

import os, subprocess, sys, time, socket, shutil, tempfile

RADICE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BANCO  = tempfile.mkdtemp(prefix="clientprova-")
ESE    = os.path.join(BANCO, "clientprova")

SORGENTI = [
    "tools/prove/clientprova.c",
    "lib/extls/extls_client.c", "lib/extls/extls_pem.c",
    "lib/extls/extls_kdf.c",    "lib/extls/extls_pss.c",
    "lib/excert/excert.c",      "lib/exasn1/exasn1.c",
    "lib/exbig/exbig.c",        "lib/excurva/excurva.c",
    "lib/excrypt/chacha20.c",   "lib/excrypt/poly1305.c",
    "lib/excrypt/x25519.c",     "lib/excrypt/fe25519.c",
    "lib/excrypt/sha512.c",
]
INCLUDI = ["lib/extls", "lib/excert", "lib/exasn1", "lib/exbig", "lib/excrypt",
           "lib/excurva"]

passate = 0
fallite = 0

def esito(nome, ok, dettaglio=""):
    global passate, fallite
    if ok:
        passate += 1
        print("  [ok]     " + nome)
    else:
        fallite += 1
        print("  [FALLITO] " + nome + ("   " + dettaglio if dettaglio else ""))

def sh(*args, **kw):
    return subprocess.run(args, cwd=kw.get("cwd", BANCO),
                          capture_output=True, text=True)

def compila():
    c = ["cc", "-Wall", "-Wextra", "-O1", "-o", ESE] + SORGENTI
    for i in INCLUDI:
        c += ["-I", i]
    c += ["-lcrypto"]
    r = subprocess.run(c, cwd=RADICE, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr)
        sys.exit(1)

def pki():
    """Una PKI vera: radice, sito buono, sito scaduto."""
    sh("openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
       "-keyout", "ca.key", "-out", "ca.pem", "-days", "3650",
       "-subj", "/CN=CA di prova", "-addext", "basicConstraints=critical,CA:TRUE")
    # Una seconda radice, di cui NON ci si fida: serve al caso «magazzino
    # che non contiene chi ha firmato».
    sh("openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
       "-keyout", "altra.key", "-out", "altra.pem", "-days", "3650",
       "-subj", "/CN=CA estranea", "-addext", "basicConstraints=critical,CA:TRUE")

    # ! E UNA PKI ECDSA ACCANTO A QUELLA RSA, perche' sul web ci sono tutt'e
    # due e la seconda e' quella che per mesi non si e' potuta aprire. La
    # radice e' su P-384 e firma con SHA-384, il sito su P-256: e' esattamente
    # la forma delle catene vere (example.com, wikipedia.org).
    sh("openssl", "ecparam", "-name", "secp384r1", "-genkey", "-noout",
       "-out", "eca.key")
    sh("openssl", "req", "-x509", "-new", "-key", "eca.key", "-sha384",
       "-out", "eca.pem", "-days", "3650", "-subj", "/CN=CA ellittica",
       "-addext", "basicConstraints=critical,CA:TRUE")

    with open(os.path.join(BANCO, "ext.cnf"), "w") as f:
        f.write("subjectAltName=DNS:prova.exos,DNS:*.jolly.exos\n"
                "basicConstraints=CA:FALSE\n")

    for nome, giorni, ca in (("srv", 365, "ca"), ("vecchio", -1, "ca")):
        sh("openssl", "req", "-newkey", "rsa:2048", "-nodes",
           "-keyout", nome + ".key", "-out", nome + ".csr",
           "-subj", "/CN=prova.exos")
        c = ["openssl", "x509", "-req", "-in", nome + ".csr",
             "-CA", ca + ".pem", "-CAkey", ca + ".key", "-CAcreateserial",
             "-out", nome + ".pem", "-extfile", "ext.cnf"]
        if giorni > 0:
            c += ["-days", str(giorni)]
        else:
            # Gia' scaduto: si data la fine a ieri.
            c += ["-not_before", "20200101000000Z", "-not_after", "20200201000000Z"]
        sh(*c)

def pki_ec():
    """Il certificato del sito su P-256, firmato dalla radice P-384."""
    sh("openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout",
       "-out", "esrv.key")
    sh("openssl", "req", "-new", "-key", "esrv.key", "-out", "esrv.csr",
       "-subj", "/CN=prova.exos")
    sh("openssl", "x509", "-req", "-in", "esrv.csr", "-CA", "eca.pem",
       "-CAkey", "eca.key", "-CAcreateserial", "-sha384",
       "-out", "esrv.pem", "-days", "365", "-extfile", "ext.cnf")

def porta_libera():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p

def con_server(cert, chiave, prova):
    """Accende un s_server, esegue `prova(porta)`, lo spegne."""
    p = porta_libera()
    srv = subprocess.Popen(
        ["openssl", "s_server", "-accept", str(p), "-cert", cert,
         "-key", chiave, "-tls1_3",
         "-ciphersuites", "TLS_CHACHA20_POLY1305_SHA256", "-www", "-quiet"],
        cwd=BANCO, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(50):
            try:
                socket.create_connection(("127.0.0.1", p), 0.2).close()
                break
            except OSError:
                time.sleep(0.1)
        return prova(p)
    finally:
        srv.terminate()
        srv.wait()

def cliente(porta, nome, ca):
    r = subprocess.run([ESE, "127.0.0.1", str(porta), nome, ca],
                       cwd=BANCO, capture_output=True, text=True, timeout=30)
    return r.returncode, r.stdout + r.stderr

def main():
    print("Il cliente TLS 1.3 contro un server OpenSSL\n")
    compila()
    pki()

    pki_ec()

    # ---- il caso buono ----------------------------------------------------
    def buono(p):
        rc, out = cliente(p, "prova.exos", "ca.pem")
        esito("la stretta di mano riesce", "stretta: riuscita" in out, out.strip())
        esito("e arriva una risposta HTTP", "HTTP/1.0 200" in out, out.strip())
        esito("l'uscita e' zero", rc == 0)
    con_server("srv.pem", "srv.key", buono)

    # ---- il jolly ---------------------------------------------------------
    def jolly(p):
        rc, out = cliente(p, "www.jolly.exos", "ca.pem")
        esito("il jolly copre un'etichetta", rc == 0, out.strip())

        rc, out = cliente(p, "a.b.jolly.exos", "ca.pem")
        esito("il jolly NON copre due etichette", "certificato e' di un altro" in out,
              out.strip())

        rc, out = cliente(p, "jolly.exos", "ca.pem")
        esito("il jolly NON copre il dominio nudo",
              "certificato e' di un altro" in out, out.strip())
    con_server("srv.pem", "srv.key", jolly)

    # ---- i casi cattivi ---------------------------------------------------
    def cattivi(p):
        rc, out = cliente(p, "banca.esempio", "ca.pem")
        esito("un nome che non c'e' nel certificato si rifiuta",
              "certificato e' di un altro sito" in out, out.strip())

        rc, out = cliente(p, "prova.exos", "altra.pem")
        esito("una radice che non ha firmato si rifiuta",
              "certificato non verificabile" in out, out.strip())
    con_server("srv.pem", "srv.key", cattivi)

    # ---- la catena ellittica ----------------------------------------------
    def ellittica(p):
        rc, out = cliente(p, "prova.exos", "eca.pem")
        esito("una catena ECDSA (sito P-256, CA P-384, firme SHA-384)",
              rc == 0, out.strip())

        rc, out = cliente(p, "prova.exos", "ca.pem")
        esito("e con la radice sbagliata si rifiuta lo stesso",
              "certificato non verificabile" in out, out.strip())
    con_server("esrv.pem", "esrv.key", ellittica)

    # ---- scaduto ----------------------------------------------------------
    def scaduto(p):
        rc, out = cliente(p, "prova.exos", "ca.pem")
        esito("un certificato scaduto si rifiuta",
              "certificato non verificabile" in out, out.strip())
    con_server("vecchio.pem", "vecchio.key", scaduto)

    print("\n%d prove superate, %d fallite" % (passate, fallite))
    shutil.rmtree(BANCO, ignore_errors=True)
    return 1 if fallite else 0

if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# =============================================================================
# tools/solo_ascii.py - le stringhe a schermo si scrivono in ASCII puro
#
#     tools/solo_ascii.py [radice]      elenca e rende 1 se ne trova
#
# ! IL PERCHE' E' UN DIFETTO VERO, VISTO IL 16 SETTEMBRE 2026. `netupdate
# -check` stampava
#
#     Registro da /tmp/netupdate GCo sistema 0.218 del ...
#
# dove al posto del trattino lungo comparivano tre glifi. Il sorgente diceva
# «Registro da %s - sistema %s», con un trattino lungo UTF-8: tre byte,
# E2 80 94. La console di EX-OS e' code page 437, dove quei tre byte valgono
# 'Gamma', 'C con cediglia' e 'o con dieresi'. Non e' un baco del programma:
# e' una stringa che non si poteva stampare li'.
#
# ! E I COMMENTI NON SI TOCCANO, ED E' TUTTA LA REGOLA. Gli accenti nei
# commenti sono voluti e vanno benissimo: quel testo non passa da nessuno
# schermo. Questo controllo guarda SOLO dentro le virgolette, e per farlo deve
# saltare commenti e caratteri come farebbe un compilatore - non basta un grep.
#
# ! SI CONTROLLA A OGNI `make all` (bersaglio `verifica-ascii`). Ottantotto
# stringhe sono state corrette in un colpo solo; l'ottantanovesima la ferma
# questo, il giorno che qualcuno incolla del testo da un editor che mette le
# virgolette basse da solo.
# =============================================================================
import re, sys, os

RADICE = sys.argv[1] if len(sys.argv) > 1 else "."
# ! tools/locali/estranei/ E' UNA COPIA PRISTINA, TENUTA APPOSTA PER IL
# CONFRONTO: correggerla vorrebbe dire toglierle l'unica ragione di esistere.
# Gli altri sono alberi di terzi, che hanno le loro regole.
SALTA = ("gcc/", "coreutils/", "rust/", "FreeBASIC", "openssl/", "quickjs/",
         "nasm/", "make/", "sed/", "grep/", "awk/", "liberation-fonts/",
         "freebasic/", "3p_app_source/", "drv_prop/", "build/", "dist/",
         "tools/locali/estranei/")

def spoglia(testo):
    """Toglie commenti e caratteri, lascia le stringhe al loro posto."""
    fuori = []
    i, n = 0, len(testo)
    riga = 1
    while i < n:
        c = testo[i]
        if c == '\n':
            riga += 1; i += 1; continue
        if testo.startswith('/*', i):
            j = testo.find('*/', i + 2)
            if j < 0: j = n
            riga += testo.count('\n', i, j); i = j + 2; continue
        if testo.startswith('//', i):
            j = testo.find('\n', i)
            i = j if j >= 0 else n; continue
        if c == '"':
            j, dentro = i + 1, []
            while j < n and testo[j] != '"':
                if testo[j] == '\\': dentro.append(testo[j]); j += 1
                if j < n: dentro.append(testo[j])
                j += 1
            fuori.append((riga, ''.join(dentro)))
            i = j + 1; continue
        if c == "'":
            j = i + 1
            while j < n and testo[j] != "'":
                if testo[j] == '\\': j += 1
                j += 1
            i = j + 1; continue
        i += 1
    return fuori

trovati = 0
for radice, dirs, files in os.walk(RADICE):
    rel = os.path.relpath(radice, RADICE) + "/"
    if any(s in rel.replace("./", "") for s in SALTA):
        dirs[:] = []; continue
    for f in files:
        if not f.endswith(('.c', '.h')): continue
        p = os.path.join(radice, f)
        if any(s in p for s in SALTA): continue
        try: testo = open(p, encoding='utf8').read()
        except Exception: continue
        for riga, s in spoglia(testo):
            brutti = [ch for ch in s if ord(ch) > 126]
            if brutti:
                print("%s:%d: %s   -> %r" % (os.path.relpath(p, RADICE), riga,
                                             s[:70], ''.join(sorted(set(brutti)))))
                trovati += 1
# =============================================================================
# ! E NON SOLO IL CODICE: CI SONO TESTI CHE IL SISTEMA MOSTRA COSI' COME SONO.
#
# /boot/help.txt lo stampa `help`, riga per riga, senza passare da nessuna
# printf: un trattino lungo li' dentro esce garbato esattamente come uno dentro
# una stringa C. Ce n'erano 31, trovati il 16 settembre 2026 cercando perche'
# «la stringa si vede anche all'avvio».
#
# ! GLI ALTRI FILE DI /boot NON CI SONO, ED E' UNA DISTINZIONE CHE CONTA.
# avvio.sh, autoexec.sh, kernel.cfg e telnetd.cfg hanno gli accenti SOLO nei
# commenti, e un commento non si stampa mai: bin/sh/shell.c salta le righe che
# cominciano con '#' prima ancora di stamparle («if (n == 0 || riga[0] == '#')
# continue;»). Metterli qui vorrebbe dire spogliare di accenti della prosa che
# nessuno vedra' mai - cioe' la stessa cosa che qui si e' deciso di NON fare
# per i commenti del C.
# =============================================================================
# ! strumenti.txt E' UN CASO A META': i COMMENTI non si stampano, ma i campi
# `nota` e `dice' si' - li stampa toolinst (bin/toolinst/toolinst.c, printf sui
# g->nota[k]). Si guardano quindi le righe che NON cominciano con '#'.
MOSTRATI      = ("boot/help.txt",)
MOSTRATI_DATI = ("tools/iso/strumenti.txt",)

for m in MOSTRATI_DATI:
    q = os.path.join(RADICE, m)
    if not os.path.exists(q): continue
    for i, r in enumerate(open(q, "rb").read().split(b"\n"), 1):
        if r.lstrip().startswith(b"#"): continue
        if any(b > 126 for b in r):
            print("%s:%d: %s" % (m, i, r.decode("utf8", "replace")[:70]))
            trovati += 1

for m in MOSTRATI:
    q = os.path.join(RADICE, m)
    if not os.path.exists(q): continue
    d = open(q, "rb").read()
    for i, r in enumerate(d.split(b"\n"), 1):
        if any(b > 126 for b in r):
            print("%s:%d: %s" % (m, i, r.decode("utf8", "replace")[:70]))
            trovati += 1


if trovati:
    print("")
    print("  ! %d stringhe a schermo con caratteri non ASCII." % trovati)
    print("")
    print("    La console di EX-OS e' code page 437: un carattere UTF-8 ci")
    print("    arriva come due o tre glifi sbagliati. Nei COMMENTI vanno")
    print("    benissimo - e' a schermo che non si possono scrivere.")
    print("")
    print("      -  al posto di  trattino lungo")
    print("      \\\"  al posto di  virgolette basse")
    print("      e'  al posto di  e accentata")
    print("")
    sys.exit(1)
print("[OK] le stringhe a schermo sono tutte ASCII")

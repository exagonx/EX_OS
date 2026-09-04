#!/usr/bin/env python3
# =============================================================================
# tools/nasm-exos/applica.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Mette (o toglie) l'ospite i386-exos in un albero dei sorgenti di NASM.
#
#     python3 tools/nasm-exos/applica.py <albero-nasm>
#     python3 tools/nasm-exos/applica.py <albero-nasm> --togli
#
# PERCHE' UNO SCRIPT E NON UNA PATCH. Stessa ragione di
# tools/binutils-exos/applica.py: una patch a contesto scade al primo
# rilascio che tocca una riga vicina alle nostre, mentre una sostituzione di
# stringhe esatte sopravvive a tutto cio' che non tocca proprio quelle — e
# quando invece le tocca LO DICE, invece di applicarsi a meta'.
# Idempotente: rilanciarlo non fa danni.
#
# =============================================================================
# QUI SI TOCCA L'OSPITE, NON IL BERSAGLIO — ed e' tutta la differenza con
# binutils e GCC.
#
# NASM non ha un «bersaglio» nel senso di autoconf: produce ELF, COFF, Mach-O
# e altri dieci formati SEMPRE, quale che sia la macchina su cui gira, e la
# scelta la fa l'utente con -f. Non c'e' niente da insegnargli su EX-OS come
# formato d'uscita: `nasm -f elf32` fa gia' esattamente cio' che serve, ed e'
# il formato che il nostro `ld` sa collegare.
#
# Quel che manca e' l'altra meta': NASM che GIRA dentro EX-OS. Per quello
# basta che il configure accetti il nome del sistema, ed e' una riga.
# =============================================================================
#
# LICENZA. NASM sta sotto una BSD a due clausole: chiede che l'avviso di
# copyright resti nelle copie, e non chiede di marcare le modifiche. Si
# marcano lo stesso, con la stessa riga che si mette in binutils e in GCC:
# chi apre uno di questi file dopo di noi deve vedere subito che non e'
# l'originale, e la licenza non e' l'unico motivo per cui vale la pena.
# =============================================================================

import datetime
import os
import sys

MARCA = "EX-OS: modificato il"

# (file, marcatore, testo_da_cercare, testo_nuovo)
#
# ! IL MARCATORE E' UNA STRINGA CHE ESISTE SOLO NELLO STATO APPLICATO: c'e'
# -> non fare niente; non c'e' -> applica. Vale nei due versi e non dipende
# da come e' fatta la sostituzione (la lezione e' di applica.py di binutils,
# dove un controllo fatto sul testo cercato applicava due volte).
MODIFICHE = [
    (
        "autoconf/helpers/config.sub",
        "| exos* ",
        "\t     | fiwix* )\n",
        "\t     | exos* | fiwix* )\n",
    ),

    # --- lo stile dei percorsi -------------------------------------------
    #
    # ! NASM SCEGLIE LO STILE DEI PERCORSI DAI MACRO DEL COMPILATORE, e per
    # un sistema che non riconosce prende PATH_UNKNOWN: `separators` e
    # `catsep` restano indefiniti, e path.c non compila —
    #
    #     path.c:204:21: error: 'separators' undeclared
    #
    # che e' un errore onesto e per fortuna rumoroso. La riga giusta non e'
    # far dire al nostro GCC di essere Unix: e' dire QUI che EX-OS ha i
    # percorsi fatti come quelli di Unix — la barra come unico separatore e
    # nessun concetto di volume — che e' vero e sta scritto in una riga.
    (
        "nasmlib/path.c",
        "defined(__exos__)",
        "    defined(__MACH__) || defined(__BEOS__) || defined(__HAIKU__)\n",
        "    defined(__MACH__) || defined(__BEOS__) || defined(__HAIKU__) || \\\n"
        "    defined(__exos__)\n",
    ),
]


def marca(percorso, testo):
    """Aggiunge la riga di modifica in testa, se non c'e' gia'.

    ! IL COMMENTO SI SCRIVE NELLA LINGUA DEL FILE. Un `# ...` in testa a un
    file C non e' un commento: e' una direttiva del preprocessore, e il
    compilatore risponde «invalid preprocessing directive #EX». Costava un
    giro di compilazione, ed e' l'unica cosa che questa funzione deve sapere
    fare bene.
    """
    if MARCA in testo:
        return testo
    oggi = datetime.date.today().isoformat()
    corpo = "%s %s per l'ospite i386-exos (EX-OS)" % (MARCA, oggi)
    if percorso.endswith((".c", ".h", ".cpp")):
        riga = "/* %s */" % corpo
    else:
        riga = "# %s" % corpo
    righe = testo.split("\n")
    # dopo lo shebang, se c'e': una riga davanti a `#!` renderebbe il file
    # non eseguibile, che e' un modo silenzioso di rompere config.sub.
    i = 1 if righe and righe[0].startswith("#!") else 0
    righe.insert(i, riga)
    return "\n".join(righe)


def main():
    if len(sys.argv) < 2:
        print(__doc__ or "uso: applica.py <albero-nasm> [--togli]")
        return 1
    albero = sys.argv[1]
    togli = "--togli" in sys.argv

    if not os.path.isfile(os.path.join(albero, "configure.ac")):
        print("applica.py: %s non sembra un albero di NASM "
              "(manca configure.ac)" % albero)
        return 1

    fatti = 0
    for rel, marcatore, cerca, mette in MODIFICHE:
        p = os.path.join(albero, rel)
        if not os.path.isfile(p):
            print("  ! manca %s — l'albero non e' quello che credo" % rel)
            return 1
        s = open(p, encoding="utf-8").read()
        applicato = marcatore in s

        if togli:
            if not applicato:
                print("  = %s: gia' pulito" % rel)
                continue
            if mette not in s:
                print("  ! %s: la modifica c'e' ma non nella forma che "
                      "conosco — la tolgo a mano o non la tolgo" % rel)
                return 1
            s = s.replace(mette, cerca, 1)
        else:
            if applicato:
                print("  = %s: c'e' gia'" % rel)
                continue
            if cerca not in s:
                print("  ! %s: non trovo il punto in cui inserire.\n"
                      "    Il rilascio ha toccato proprio quelle righe: va "
                      "riscritta la voce in MODIFICHE." % rel)
                return 1
            s = marca(p, s.replace(cerca, mette, 1))

        open(p, "w", encoding="utf-8").write(s)
        print("  %s %s" % ("-" if togli else "+", rel))
        fatti += 1

    print("[OK] %s: %d file toccati" % ("tolto" if togli else "applicato", fatti))
    return 0


sys.exit(main())

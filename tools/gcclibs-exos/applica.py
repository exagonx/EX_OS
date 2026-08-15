#!/usr/bin/env python3
# =============================================================================
# tools/gcclibs-exos/applica.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Mette (o toglie) il bersaglio exos in un albero dei sorgenti di GMP,
# MPFR o MPC.
#
#     python3 tools/gcclibs-exos/applica.py <albero>
#     python3 tools/gcclibs-exos/applica.py <albero> --togli
#
# UNA RIGA SOLA, PER TUTTE E TRE. A differenza di binutils — dove il
# bersaglio va spiegato a bfd, gas e ld — qui non c'e' niente di specifico
# da dichiarare: GMP, MPFR e MPC non sanno cosa sia un formato eseguibile,
# e l'unica cosa che li ferma e' `config.sub` che non riconosce il nome
# del sistema operativo.
#
# ! IL FILE DA TOCCARE NON E' LO STESSO IN TUTTI E TRE, e non e' sempre
# `config.sub`:
#
#   gmp     configfsf.sub          config.sub e' un involucro di GMP che
#                                  gestisce i suoi nomi di CPU e delega
#                                  il resto a questo
#   mpfr    config.sub
#   mpc     build-aux/config.sub
#
# e nemmeno la RIGA e' la stessa, perche' le tre versioni di config.sub
# hanno terminatori diversi nell'elenco dei sistemi ammessi. Per questo
# ogni voce porta il proprio testo di riferimento invece di una regola
# generale che li coprirebbe tutti solo per finta.
#
# LICENZA. GMP e' LGPLv3+/GPLv2+, MPFR e MPC sono LGPLv3+. Le modifiche
# sono banali (una riga in uno script di configurazione generato) e questo
# script marca comunque ogni file toccato con la dichiarazione richiesta a
# chi distribuisce una versione modificata.
# =============================================================================

import datetime
import os
import sys

MARCA = "Modificato per il bersaglio exos di EX-OS"

# (file relativo, marcatore, testo prima, testo dopo)
#
# Il marcatore e' una stringa che esiste SOLO nello stato applicato: c'e'
# -> non fare niente, non c'e' -> applica. Stessa convenzione (e stessa
# ragione) di tools/binutils-exos/applica.py, dove il controllo ingenuo
# "il testo nuovo c'e' gia'?" aveva duplicato una riga.
MODIFICHE = [
    (
        "configfsf.sub",                       # GMP
        "| mlibc* | exos* )",
        "\t     | fiwix* | mlibc* )\n",
        "\t     | fiwix* | mlibc* | exos* )\n",
    ),
    (
        "config.sub",                          # MPFR
        "| fiwix* | exos* )",
        "\t     | fiwix* )\n",
        "\t     | fiwix* | exos* )\n",
    ),
    (
        "build-aux/config.sub",                # MPC
        "| exos*)",
        "\t     | nsk* | powerunix* | genode* | zvmoe* | qnx* | emx* | zephyr*)\n",
        "\t     | nsk* | powerunix* | genode* | zvmoe* | qnx* | emx* | zephyr* \\\n"
        "\t     | exos*)\n",
    ),
]


def marca_file(percorso):
    """Aggiunge la dichiarazione di modifica, una volta sola."""
    with open(percorso, "r", encoding="utf-8", errors="surrogateescape") as f:
        testo = f.read()
    if MARCA in testo:
        return
    riga = "# %s (%s)\n" % (MARCA, datetime.date.today().isoformat())
    if testo.startswith("#!"):
        fine = testo.index("\n") + 1
        testo = testo[:fine] + riga + testo[fine:]
    else:
        testo = riga + testo
    with open(percorso, "w", encoding="utf-8", errors="surrogateescape") as f:
        f.write(testo)


def applica(albero, togli):
    fatte = saltate = 0
    trovato = 0

    for relativo, marcatore, prima, dopo in MODIFICHE:
        percorso = os.path.join(albero, relativo)
        if not os.path.exists(percorso):
            continue                    # non e' questa libreria: va bene

        trovato += 1

        with open(percorso, "r", encoding="utf-8", errors="surrogateescape") as f:
            testo = f.read()

        # GMP ha SIA config.sub SIA configfsf.sub: il primo e' l'involucro
        # e non contiene l'elenco dei sistemi, quindi la voce di MPFR non
        # ci si applica. Si riconosce dal testo di riferimento assente.
        da, a = (dopo, prima) if togli else (prima, dopo)
        applicato = marcatore in testo

        if applicato != togli:
            print("  = %s (gia' a posto)" % relativo)
            saltate += 1
            continue

        if testo.count(da) != 1:
            if testo.count(da) == 0:
                continue                # involucro di GMP: non e' il file giusto
            print("  ! %s: il testo di riferimento compare %d volte invece di una."
                  % (relativo, testo.count(da)))
            return 1

        with open(percorso, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write(testo.replace(da, a))
        if not togli:
            marca_file(percorso)
        print("  %s %s" % ("-" if togli else "+", relativo))
        fatte += 1

    if trovato == 0:
        print("  ! nessun config.sub trovato: non sembra un albero di GMP/MPFR/MPC")
        return 1

    print("\n%s: %d file modificati, %d gia' a posto."
          % ("Rimozione" if togli else "Applicazione", fatte, saltate))
    return 0


def main():
    if len(sys.argv) < 2:
        print("uso: applica.py <albero-gmp|mpfr|mpc> [--togli]")
        return 1
    albero = sys.argv[1]
    togli = "--togli" in sys.argv[2:]

    if not os.path.isdir(albero):
        print("Non e' una directory: %s" % albero)
        return 1

    print("%s il bersaglio exos in %s\n"
          % ("Tolgo" if togli else "Applico", albero))
    return applica(albero, togli)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# =============================================================================
# tools/binutils-exos/applica.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Mette (o toglie) il bersaglio i386-exos in un albero dei sorgenti di
# binutils.
#
#     python3 tools/binutils-exos/applica.py <albero-binutils>
#     python3 tools/binutils-exos/applica.py <albero-binutils> --togli
#
# PERCHE' UNO SCRIPT E NON UNA PATCH. Stessa ragione di
# tools/gcc-exos/applica.py: una patch a contesto scade al primo rilascio
# che tocca una riga vicina alle nostre, mentre una sostituzione di
# stringhe esatte sopravvive a tutto cio' che non tocca proprio quelle —
# e quando invece le tocca LO DICE, invece di applicarsi a meta'.
# Idempotente: rilanciarlo non fa danni.
#
# COSA CAMBIA, E PERCHE' SONO QUATTRO FILE
#
#   config.sub          'exos' entra fra i sistemi operativi ammessi, o
#                       qualunque configure risponde "Invalid configuration".
#   bfd/config.bfd      quale formato oggetto ha il bersaglio: ELF32 i386,
#                       lo stesso di i386-*-elf.
#   gas/configure.tgt   in che formato assembla `as`.
#   ld/configure.tgt    quale emulazione usa `ld` (elf_i386) e quali
#                       conosce.
#
# Non serve altro: EX-OS non ha un formato eseguibile proprio ne'
# convenzioni di rilocazione diverse da quelle di un ELF i386 qualunque.
# La differenza fra "elf" ed "exos" sta nel COMPILATORE (indirizzo di
# caricamento, crt0, -lc automatico: vedi tools/gcc-exos/exos.h), non negli
# strumenti che manipolano gli oggetti.
#
# LICENZA. binutils e' GPLv3+: le modifiche non possono che esserlo, e
# questo script marca ogni file toccato con la dichiarazione di modifica e
# la data che la GPLv3 §5(a) richiede a chi distribuisce una versione
# modificata. EX-OS resta GPL-2.0-or-later — vedi tools/gcc-exos/leggimi.md.
# =============================================================================

import datetime
import os
import sys

MARCA = "Modificato per il bersaglio i386-exos di EX-OS"

# ! IL MARCATORE NON E' RIDONDANTE, ed e' costato un giro di correzione.
#
# Due di queste modifiche sono INSERIMENTI: il testo di partenza resta
# dentro quello di arrivo (si aggiunge una riga sopra o sotto, non si
# sostituisce niente). Con il solo confronto "il testo nuovo c'e' gia'?"
# la seconda applicazione trovava ANCHE il testo vecchio e inseriva una
# seconda volta — due righe `| exos*` in config.sub — e la rimozione
# successiva ne toglieva una sola, lasciando l'albero modificato mentre
# lo script diceva di averlo pulito.
#
# Il marcatore e' una stringa che esiste SOLO nello stato applicato:
# c'e' -> non fare niente; non c'e' -> applica. Vale in entrambi i versi e
# non dipende da come e' fatta la sostituzione.
MODIFICHE = [
    (
        "config.sub",
        "| exos* \\",
        "\t| fiwix* \\\n",
        "\t| exos* \\\n"
        "\t| fiwix* \\\n",
    ),
    (
        "bfd/config.bfd",
        "i[3-7]86-*-exos*",
        "  i[3-7]86-*-elf* | i[3-7]86-*-rtems* | i[3-7]86-*-genode*)\n",
        "  i[3-7]86-*-elf* | i[3-7]86-*-rtems* | i[3-7]86-*-genode* | i[3-7]86-*-exos*)\n",
    ),
    (
        "gas/configure.tgt",
        "i386-*-exos*",
        "  i386-*-elf*)\t\t\t\tfmt=elf ;;\n",
        "  i386-*-elf*)\t\t\t\tfmt=elf ;;\n"
        "  i386-*-exos*)\t\t\t\tfmt=elf ;;\n",
    ),
    (
        "ld/configure.tgt",
        "i[3-7]86-*-exos*",
        "i[3-7]86-*-elf* | i[3-7]86-*-rtems* | i[3-7]86-*-genode*)\n",
        "i[3-7]86-*-elf* | i[3-7]86-*-rtems* | i[3-7]86-*-genode* | i[3-7]86-*-exos*)\n",
    ),

    # --- pex-exos: come si lancia un programma senza fork ------------------
    #
    # Le quattro modifiche qui sopra riguardano il BERSAGLIO (che formato
    # produrre). Queste tre riguardano l'OSPITE: valgono solo quando
    # binutils gira DENTRO EX-OS, e sono la ragione per cui esistono i
    # binutils nativi.
    #
    # libiberty sceglie l'implementazione di pex — "lancia un programma e
    # aspettalo" — in base al sistema ospite, e per tutto cio' che non e'
    # Windows o MSDOS prende pex-unix.c, che e' costruito su fork(). EX-OS
    # non ha fork: ha spawn_ex(), che il figlio lo crea gia' fatto. Vedi
    # pex-exos.c qui accanto per il perche' esteso.
    #
    # ! SI TOCCA `configure`, NON SOLO `configure.ac`. Il primo e' il
    # prodotto di autoconf ed e' quello che gira davvero; cambiare solo il
    # secondo non ha alcun effetto finche' qualcuno non rigenera, cioe' —
    # nella pratica — mai. Si cambiano entrambi perche' restino d'accordo.
    (
        "libiberty/configure",
        "pexecute=pex-exos",
        "     *)\t\t\t\tpexecute=pex-unix   ;;\n",
        "     *-*-exos*)\t\t\tpexecute=pex-exos   ;;\n"
        "     *)\t\t\t\tpexecute=pex-unix   ;;\n",
    ),
    (
        "libiberty/configure.ac",
        "pexecute=pex-exos",
        "     *)\t\t\t\tpexecute=pex-unix   ;;\n",
        "     *-*-exos*)\t\t\tpexecute=pex-exos   ;;\n"
        "     *)\t\t\t\tpexecute=pex-unix   ;;\n",
    ),
    # La regola di compilazione. Non basta che il file ci sia: la regola
    # implicita di libiberty per i .c e' `false` — apposta, perche' ogni
    # oggetto deve dichiarare le proprie dipendenze — quindi senza questa
    # la build fallisce con "false" e nient'altro, che e' il messaggio meno
    # utile della giornata.
    (
        "libiberty/Makefile.in",
        "./pex-exos.$(objext):",
        "./pex-unix.$(objext): $(srcdir)/pex-unix.c",
        "./pex-exos.$(objext): $(srcdir)/pex-exos.c config.h $(INCDIR)/ansidecl.h \\\n"
        "\t$(INCDIR)/libiberty.h $(srcdir)/pex-common.h\n"
        "\tif [ x\"$(PICFLAG)\" != x ]; then \\\n"
        "\t  $(COMPILE.c) $(PICFLAG) $(srcdir)/pex-exos.c -o pic/$@; \\\n"
        "\telse true; fi\n"
        "\tif [ x\"$(NOASANFLAG)\" != x ]; then \\\n"
        "\t  $(COMPILE.c) $(PICFLAG) $(NOASANFLAG) $(srcdir)/pex-exos.c -o noasan/$@; \\\n"
        "\telse true; fi\n"
        "\t$(COMPILE.c) $(srcdir)/pex-exos.c $(OUTPUT_OPTION)\n"
        "\n"
        "./pex-unix.$(objext): $(srcdir)/pex-unix.c",
    ),
]

# File nostri da copiare nell'albero di binutils. La sostituzione di
# stringhe non basta: pex-exos.c e' un sorgente intero, e tenerlo dentro
# questo script come stringa vorrebbe dire scriverlo senza colorazione,
# senza compilarlo mai da solo e senza poterlo leggere in diff.
FILE_NOSTRI = [
    ("pex-exos.c", "libiberty/pex-exos.c"),
]


def marca_file(percorso):
    """Aggiunge la dichiarazione di modifica GPLv3 §5(a), una volta sola."""
    with open(percorso, "r", encoding="utf-8", errors="surrogateescape") as f:
        testo = f.read()
    if MARCA in testo:
        return
    oggi = datetime.date.today().isoformat()
    riga = "# %s (%s)\n" % (MARCA, oggi)
    # Dopo la prima riga se e' uno shebang, altrimenti in testa.
    if testo.startswith("#!"):
        fine = testo.index("\n") + 1
        testo = testo[:fine] + riga + testo[fine:]
    else:
        testo = riga + testo
    with open(percorso, "w", encoding="utf-8", errors="surrogateescape") as f:
        f.write(testo)


def copia_file_nostri(albero, togli):
    """Mette (o toglie) i sorgenti che aggiungiamo noi all'albero."""
    qui = os.path.dirname(os.path.abspath(__file__))
    fatte = saltate = 0

    for nome, relativo in FILE_NOSTRI:
        origine = os.path.join(qui, nome)
        destino = os.path.join(albero, relativo)

        if togli:
            if os.path.exists(destino):
                os.remove(destino)
                print("  - %s" % relativo)
                fatte += 1
            else:
                print("  = %s (gia' a posto)" % relativo)
                saltate += 1
            continue

        if not os.path.exists(origine):
            print("  ! manca il sorgente %s" % origine)
            return None

        with open(origine, "rb") as f:
            nuovo = f.read()

        # Si ricopia anche se c'e' gia': durante lo sviluppo il file cambia
        # qui e va rispecchiato la'. Il confronto serve solo a dire cosa e'
        # successo davvero.
        vecchio = None
        if os.path.exists(destino):
            with open(destino, "rb") as f:
                vecchio = f.read()

        if vecchio == nuovo:
            print("  = %s (gia' a posto)" % relativo)
            saltate += 1
            continue

        with open(destino, "wb") as f:
            f.write(nuovo)
        print("  %s %s" % ("+" if vecchio is None else "~", relativo))
        fatte += 1

    return (fatte, saltate)


def applica(albero, togli):
    fatte = saltate = 0

    for relativo, marcatore, prima, dopo in MODIFICHE:
        percorso = os.path.join(albero, relativo)
        if not os.path.exists(percorso):
            print("  ! manca: %s" % relativo)
            return 1

        with open(percorso, "r", encoding="utf-8", errors="surrogateescape") as f:
            testo = f.read()

        applicato = marcatore in testo
        if applicato != togli:
            print("  = %s (gia' a posto)" % relativo)
            saltate += 1
            continue

        da, a = (dopo, prima) if togli else (prima, dopo)

        if testo.count(da) != 1:
            print("  ! %s: il testo di riferimento compare %d volte invece di una."
                  % (relativo, testo.count(da)))
            print("    Upstream ha toccato proprio quelle righe: va aggiornato")
            print("    questo script, non forzata la sostituzione.")
            return 1

        with open(percorso, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write(testo.replace(da, a))
        if not togli:
            marca_file(percorso)
        print("  %s %s" % ("-" if togli else "+", relativo))
        fatte += 1

    r = copia_file_nostri(albero, togli)
    if r is None:
        return 1
    fatte += r[0]
    saltate += r[1]

    print("\n%s: %d file modificati, %d gia' a posto."
          % ("Rimozione" if togli else "Applicazione", fatte, saltate))
    return 0


def main():
    if len(sys.argv) < 2:
        print(__doc__ or "uso: applica.py <albero-binutils> [--togli]")
        return 1
    albero = sys.argv[1]
    togli = "--togli" in sys.argv[2:]

    if not os.path.isdir(albero):
        print("Non e' una directory: %s" % albero)
        return 1
    if not os.path.exists(os.path.join(albero, "bfd", "config.bfd")):
        print("Non sembra un albero di binutils: %s" % albero)
        return 1

    print("%s il bersaglio i386-exos in %s\n"
          % ("Tolgo" if togli else "Applico", albero))
    return applica(albero, togli)


if __name__ == "__main__":
    sys.exit(main())

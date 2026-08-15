#!/usr/bin/env python3
# =============================================================================
# tools/freebasic-exos/applica.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
"""Mette (o toglie) il bersaglio EX-OS in un albero dei sorgenti di FreeBASIC.

    python3 tools/freebasic-exos/applica.py <albero-fb> [--togli]

! PERCHE' UNO SCRIPT E NON UNA PATCH. Una patch a contesto scade appena
qualcuno tocca una riga vicina alle nostre. Qui si sostituiscono stringhe
ESATTE: sopravvive a tutto cio' che non tocca proprio quelle righe, e
quando invece le tocca LO DICE, invece di applicarsi a meta'. E' anche
idempotente — rilanciarlo non fa danni.

Che cosa cambia, e perche' cosi' poco:

  src/rtlib/fb_config.h        riconosce __exos__ -> HOST_EXOS
  src/rtlib/fb.h               include exos/fb_exos.h
  src/rtlib/fb_private_thread.h  struct _FBTHREAD anche senza thread
  src/rtlib/exos/              NUOVA: lo strato di sistema, tutto nostro

! NON si definisce HOST_UNIX. Quella macro tira dentro termios, i
segnali, i thread e _FILE_OFFSET_BITS=64, e di quei quattro EX-OS non ha
niente. Lo strato in exos/ e' modellato su dos/, che e' l'unico scritto
per un sistema a un flusso solo.

Licenze: i file modificati appartengono a FreeBASIC — il compilatore e'
GPL v2, la runtime (src/rtlib/) e' LGPL v2.1 con eccezione di
collegamento. I file NUOVI sotto src/rtlib/exos/ sono nostri e sono
LGPL v2.1 o successiva, cioe' la stessa licenza della runtime in cui
entrano: una runtime con dentro un pezzo GPL costringerebbe alla GPL ogni
programma compilato con fbc, che e' esattamente cio' che l'eccezione di
collegamento esiste per evitare.
"""

import os
import shutil
import sys

QUI = os.path.dirname(os.path.abspath(__file__))

# (percorso relativo, testo da cercare, testo da metterci)
MODIFICHE = [
    (
        "src/rtlib/fb_config.h",
        """#elif defined __linux__
	#define HOST_LINUX
	#define HOST_UNIX""",
        """#elif defined __exos__
	/* EX-OS. ! NON si definisce HOST_UNIX: quella macro tira dentro
	   termios, i segnali, i thread e _FILE_OFFSET_BITS=64, e qui non
	   c'e' nessuno dei quattro. Lo strato di sistema sta in
	   src/rtlib/exos/ ed e' modellato su dos/. */
	#define HOST_EXOS
#elif defined __linux__
	#define HOST_LINUX
	#define HOST_UNIX""",
    ),
    (
        "src/rtlib/fb.h",
        """#if defined HOST_DOS
	#include "dos/fb_dos.h\"""",
        """#if defined HOST_EXOS
	#include "exos/fb_exos.h"
#elif defined HOST_DOS
	#include "dos/fb_dos.h\"""",
    ),
    (
        "src/rtlib/fb_private_thread.h",
        """struct _FBTHREAD {
#if defined HOST_DOS && defined ENABLE_MT""",
        """struct _FBTHREAD {
/* EX-OS: un flusso di esecuzione solo. ! La struttura deve comunque
   ESISTERE — il codice comune la nomina — ma un suo puntatore non sara'
   mai valido: qui i thread non si creano. */
#if defined HOST_EXOS
	int id;
	void *opaque;
#elif defined HOST_DOS && defined ENABLE_MT""",
    ),
]


def applica(albero):
    fatti = posto = 0

    for rel, vecchio, nuovo in MODIFICHE:
        p = os.path.join(albero, rel)
        testo = open(p, encoding="utf-8").read()

        if nuovo in testo:
            print("  = %s (gia' a posto)" % rel)
            posto += 1
            continue

        if testo.count(vecchio) != 1:
            print("  ! %s: il punto di aggancio non c'e' o e' ambiguo (%d volte)"
                  % (rel, testo.count(vecchio)), file=sys.stderr)
            print("    Upstream ha toccato proprio quelle righe: va rifatto a mano.",
                  file=sys.stderr)
            sys.exit(1)

        open(p, "w", encoding="utf-8").write(testo.replace(vecchio, nuovo))
        print("  + %s" % rel)
        fatti += 1

    # Lo strato di sistema: file nuovi, si copiano interi.
    dest = os.path.join(albero, "src/rtlib/exos")
    os.makedirs(dest, exist_ok=True)

    # =========================================================================
    # ! NESSUN NOME PUO' COLLIDERE CON IL LIVELLO COMUNE, e il controllo sta
    # qui perche' senza di lui il difetto e' INVISIBILE.
    #
    # Il makefile di FreeBASIC appiattisce gli oggetti in una directory sola:
    #
    #     LIBFB_C := $(sort $(foreach i,$(RTLIB_DIRS), %.c -> obj/%.o))
    #     VPATH   = $(RTLIB_DIRS)          # src/rtlib  src/rtlib/exos  ...
    #
    # Due sorgenti con lo STESSO nome in due directory diverse danno lo stesso
    # oggetto; `$(sort)` li fonde in uno solo, e VPATH cerca in ordine —
    # trovando prima `src/rtlib/`. Il nostro file non viene compilato MAI, e
    # nessuno lo dice: libfb.a esce semplicemente senza i suoi simboli.
    #
    # E' successo l'11 agosto 2026 con `exos/sys_exec.c`, gemello di
    # `src/rtlib/sys_exec.c`. Il link di fbc si e' fermato su
    #
    #     libfb.a(sys_exec.o): undefined reference to `fb_ExecEx'
    #
    # cioe' accusando il file comune, che era innocente. La correzione e'
    # stata dividere in sys_execex.c + sys_hshell.c, che e' come si chiamano
    # in unix/, dos/, win32/ e xbox/ — la convenzione c'era gia'.
    #
    # ! E NON SI VEDE COSTRUENDO IN CROCE: prepara-fb.sh compila `exos/*.c`
    # a mano dando agli oggetti il prefisso `exos_`, quindi li' lo scontro non
    # esiste. La libfb.a del CD era completa e quella del makefile no — due
    # strade per lo stesso risultato, e una sola sbagliata. Un controllo che
    # gira solo su una delle due non avrebbe trovato niente.
    # =========================================================================
    comuni = set()
    for d in ("src/rtlib", "src/rtlib/x86"):
        p = os.path.join(albero, d)
        if os.path.isdir(p):
            comuni.update(n for n in os.listdir(p) if n.endswith(".c"))

    scontri = [f for f in sorted(os.listdir(os.path.join(QUI, "exos")))
               if f.endswith(".c") and f in comuni]
    if scontri:
        print("  ! nomi che collidono con il livello comune: %s"
              % ", ".join(scontri), file=sys.stderr)
        print("    Il makefile compilerebbe quello comune e non il nostro, in",
              file=sys.stderr)
        print("    silenzio. Rinominare come fanno unix/ e win32/ (per esempio",
              file=sys.stderr)
        print("    sys_exec.c -> sys_execex.c + sys_hshell.c).", file=sys.stderr)
        sys.exit(1)

    # ! SI TOGLIE CIO' CHE NON E' PIU' NOSTRO. Senza, un file rinominato
    # lascerebbe in giro la versione vecchia — e con lo stesso scontro di nomi
    # di prima, che e' esattamente il difetto che si sta chiudendo.
    nostri = set(os.listdir(os.path.join(QUI, "exos")))
    for f in os.listdir(dest):
        if f not in nostri:
            os.remove(os.path.join(dest, f))
            print("  - src/rtlib/exos/%s (non e' piu' nostro)" % f)

    for f in sorted(nostri):
        shutil.copy2(os.path.join(QUI, "exos", f), os.path.join(dest, f))
    print("  + src/rtlib/exos/ (%d file)" % len(os.listdir(dest)))

    print("\nApplicazione: %d file modificati, %d gia' a posto." % (fatti, posto))


def togli(albero):
    tolti = 0
    for rel, vecchio, nuovo in MODIFICHE:
        p = os.path.join(albero, rel)
        testo = open(p, encoding="utf-8").read()
        if nuovo not in testo:
            continue
        open(p, "w", encoding="utf-8").write(testo.replace(nuovo, vecchio))
        print("  - %s" % rel)
        tolti += 1

    dest = os.path.join(albero, "src/rtlib/exos")
    if os.path.isdir(dest):
        shutil.rmtree(dest)
        print("  - src/rtlib/exos/")

    print("\nRimozione: %d file riportati com'erano." % tolti)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)

    albero = os.path.abspath(sys.argv[1])
    if not os.path.isfile(os.path.join(albero, "src/rtlib/fb_config.h")):
        print("'%s' non e' un albero di FreeBASIC" % albero, file=sys.stderr)
        sys.exit(1)

    if "--togli" in sys.argv:
        print("Tolgo il bersaglio exos da %s" % albero)
        togli(albero)
    else:
        print("Applico il bersaglio exos in %s" % albero)
        applica(albero)


if __name__ == "__main__":
    main()

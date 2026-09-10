# =============================================================================
# tools/iso/prova-make/makefile
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# La prova che il kit di strumenti e' COMPLETO, da eseguire DENTRO EX-OS:
#
#     cd /cdrom/prova-make
#     make
#     ./prova
#
# -----------------------------------------------------------------------------
# ! COSA PROVA, E PERCHE' QUESTE COSE E NON ALTRE
#
# Non prova «make funziona». Prova che i pezzi si parlano, e ognuna delle
# righe qui sotto e' li' perche' e' gia' stata rotta almeno una volta:
#
#   $(wildcard *.c)      make deve saper leggere una directory. Il suo glob
#                        e' quello che include <pwd.h> e chiama getpwnam:
#                        vedi la modifica in tools/make-exos/applica.py.
#
#   la regola implicita  make deve saper LANCIARE un comando. Su EX-OS non
#   %.o: %.c             c'e' fork(), quindi child_execute_job passa da
#                        spawn_ex: e' tutto il porting, ed e' qui che si
#                        vede se ha funzionato.
#
#   `rm -f $@; ar rcs`   DUE comandi su una riga sola. La shell di EX-OS ha
#                        imparato il `;` per questa riga, che e' la prima
#                        ricetta della runtime di FreeBASIC. Senza, si
#                        cercava un programma chiamato «rm -f libprova.a; ar».
#
#   ar + ranlib          l'archivio, e l'indice dentro l'archivio. `ar` c'era
#                        gia' compilato per i386-exos da giorni e non lo
#                        copiava nessuno sul CD.
#
#   il link finale       gcc che chiama collect2 che chiama ld, con una
#                        libreria .a in mezzo.
#
# ! IL RISULTATO SI CONTROLLA DA SE'. `./prova` stampa una somma di cui il
# valore giusto e' scritto qui sotto e dentro il programma: 385, la somma
# dei quadrati da 1 a 10. Un «sembra aver funzionato» non e' una prova, e un
# programma che stampa «tutto a posto» senza aver calcolato niente nemmeno.
#
# -----------------------------------------------------------------------------
# ! SUL CD IL MAKEFILE NON SI PUO' SCRIVERE, e make ha bisogno di scrivere
# gli oggetti. Percio' si copia l'albero da qualche parte prima:
#
#     mkdir /pm && cp /cdrom/prova-make/* /pm && cd /pm && make
#
# =============================================================================

CC      = gcc
AR      = ar
RANLIB  = ranlib
CFLAGS  = -O2

# ! `main.c` VA TOLTO dall'elenco della libreria: se ci finisse dentro,
# `ld` troverebbe due volte `main` — una nell'oggetto e una nell'archivio —
# e il messaggio parlerebbe di una definizione multipla senza dire che a
# metterla li' e' stato un carattere jolly.
SORGENTI := $(filter-out main.c,$(wildcard *.c))
OGGETTI  := $(SORGENTI:.c=.o)

prova: main.o libprova.a
	$(CC) $(CFLAGS) -o $@ main.o libprova.a

# Le due cose su una riga sola: e' la forma che usa il makefile di
# FreeBASIC, ed e' quella che ha richiesto il `;` nella shell.
libprova.a: $(OGGETTI)
	rm -f $@; $(AR) rcs $@ $(OGGETTI)
	$(RANLIB) $@

%.o: %.c prova.h
	$(CC) $(CFLAGS) -c $< -o $@

pulisci:
	rm -f *.o libprova.a prova

.PHONY: pulisci

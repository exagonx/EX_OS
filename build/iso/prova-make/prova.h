/* =============================================================================
 * tools/iso/prova-make/prova.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * L'header serve a una cosa sola oltre che a dichiarare: sta fra le
 * prerequisite della regola implicita `%.o: %.c prova.h`, cosi' toccarlo
 * ricompila tutto. E' il minimo di dipendenza che rende `make` piu' di uno
 * script — se dopo un `touch prova.h` non ricompila niente, la parte di make
 * che confronta le date sul filesystem di EX-OS non funziona, e quello e' un
 * difetto che un compilatore da solo non farebbe mai vedere.
 * ============================================================================= */

#ifndef PROVA_H
#define PROVA_H

/* La somma dei quadrati da 1 a n. */
long somma_quadrati(int n);

/* Quante volte `c` compare in `s`. Serve solo ad avere un SECONDO file
 * nella libreria: con uno solo, `ar` verrebbe esercitato a meta' — un
 * archivio con un membro non ha bisogno di un indice, e l'indice e'
 * esattamente cio' che `ranlib` deve scrivere. */
int conta(const char *s, char c);

#endif /* PROVA_H */

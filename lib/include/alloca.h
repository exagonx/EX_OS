/* =============================================================================
 * lib/include/alloca.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <alloca.h> — memoria sulla pila, liberata al ritorno dalla funzione.
 *
 * Non e' una funzione della libc: la fa il compilatore, spostando il
 * puntatore di pila. Il nome e' quello che il codice POSIX include.
 * ! La pila di un filo e' di 2 MB (FILO_STACK_SIZE): chi chiede molto qui
 * lo scopre con un fault, non con un NULL.
 * ============================================================================= */

#ifndef EXOS_ALLOCA_H
#define EXOS_ALLOCA_H

#include <stddef.h>

#undef alloca
#define alloca(dimensione) __builtin_alloca(dimensione)

#endif /* EXOS_ALLOCA_H */

/* =============================================================================
 * lib/include/malloc.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <malloc.h> — l'header che il codice di terzi si aspetta di trovare
 *
 * ! NON E' STANDARD, ED E' PROPRIO PER QUESTO CHE ESISTE. Il C dichiara
 * malloc, calloc, realloc e free in <stdlib.h>, e li' stanno anche qui. Ma
 * mezzo mondo — glibc, i BSD, Windows — porta anche un <malloc.h>, e il codice
 * scritto per quei sistemi lo include senza chiederselo. Il primo a farlo qui
 * dentro e' stato QuickJS: senza questo file, `#include <malloc.h>` finiva su
 * quello del sistema OSPITE, con tutto cio' che si porta dietro.
 *
 * ! E NON DUPLICA NIENTE: rimanda a <stdlib.h>, dove le funzioni sono
 * dichiarate una volta sola. Due dichiarazioni della stessa funzione in due
 * header diversi sono due cose da tenere allineate, cioe' una che prima o poi
 * diverge.
 *
 * L'unico nome che vive qui e' `malloc_usable_size`, che in <stdlib.h> non
 * c'e' perche' nello standard non c'e': e' l'estensione che permette a un
 * raccoglitore di memoria di sapere quanto sta occupando davvero. La sua
 * dichiarazione sta in libc.h accanto a free, e questo file la rende
 * raggiungibile anche a chi include soltanto <malloc.h>.
 * ============================================================================= */
#ifndef EXOS_MALLOC_H
#define EXOS_MALLOC_H

#include "stdlib.h"

#endif /* EXOS_MALLOC_H */

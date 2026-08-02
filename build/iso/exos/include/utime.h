/* =============================================================================
 * lib/include/utime.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <utime.h> — la data di un file, che qui non si cambia.
 *
 * ⚠️ utime() NON FA NIENTE. Nessun filesystem di EX-OS ha una syscall per
 * riscrivere le date di un file: quello che c'e' scritto e' la data della
 * scrittura, ed e' l'unica vera. La funzione ritorna 0 se il file esiste,
 * perche' chi la chiama — `objcopy` e `strip`, per conservare la data
 * dell'originale — stampa un avviso a ogni fallimento, e un avviso per
 * file su un'operazione che non e' andata storta e' solo rumore.
 *
 * Stessa convenzione di chmod e umask in <sys/stat.h>: il nome c'e', il
 * commento dice forte che e' inerte.
 * ============================================================================= */

#ifndef EXOS_UTIME_H
#define EXOS_UTIME_H

#include "libc.h"

/* La struttura che POSIX passa a utime(). I campi ci sono perche' il
 * chiamante li riempie; nessuno li legge. */
struct utimbuf {
    time_t actime;      /* ultimo accesso */
    time_t modtime;     /* ultima modifica */
};

#endif /* EXOS_UTIME_H */

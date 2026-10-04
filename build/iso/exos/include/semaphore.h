/* =============================================================================
 * lib/include/semaphore.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * I semafori POSIX sopra il Semaforo della libc (@PTHREAD, 28 settembre 2026).
 *
 * ! SOLO DENTRO UN PROCESSO: `pshared` diverso da zero rende ENOSYS. Il
 * Semaforo di EX-OS dorme su un indirizzo che vale dentro un gruppo di fili,
 * e due processi non si aspettano cosi' (vedi attesa_dormi in libc.h). I
 * semafori con nome (sem_open) non ci sono per la stessa ragione.
 * ============================================================================= */
#ifndef EXOS_SEMAPHORE_H
#define EXOS_SEMAPHORE_H

#include "libc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { volatile int s; } sem_t;   /* un Semaforo della libc */

int sem_init(sem_t *s, int pshared, unsigned int valore);
int sem_destroy(sem_t *s);
int sem_wait(sem_t *s);
int sem_trywait(sem_t *s);
int sem_timedwait(sem_t *s, const struct timespec *quando);
int sem_post(sem_t *s);
int sem_getvalue(sem_t *s, int *valore);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_SEMAPHORE_H */

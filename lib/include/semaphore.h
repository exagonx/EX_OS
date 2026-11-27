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
 * (0.373) BETWEEN PROCESSES TOO: sem_init with pshared on memory shared
 * with another process (MAP_SHARED, a named zone), and the named ones,
 * sem_open (Qt's QSystemSemaphore). A shared one waits in the kernel by the
 * physical address of the word (SYS_ATTESA_DORMI_COMUNE). sem_t stays one
 * int, as programs already built have it: bit 30 is "shared", the count is
 * below it (SEM_VALUE_MAX). A named semaphore is a named zone: it lives
 * while some process holds it, sem_unlink does nothing (kernel/include/shm.h).
 * A kernel before 0.373: pshared is ENOSYS, as before.
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

#define SEM_VALUE_MAX 0x3FFFFFFF
#define SEM_FAILED    ((sem_t *)0)
/* nome "/name"; with O_CREAT two more arguments: mode_t, unsigned value */
sem_t *sem_open(const char *nome, int flag, ...);
int    sem_close(sem_t *s);
int    sem_unlink(const char *nome);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_SEMAPHORE_H */

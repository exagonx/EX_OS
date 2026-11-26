/* tools/wamr-exos/ripieghi/exos_wamr.h — included in every file of WAMR
 * (-include): what EX-OS's libc does not have and WAMR's POSIX platform
 * names. Only for code paths that this build does not use (named
 * semaphores: the thread manager, off); ripieghi.c answers ENOSYS.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef EXOS_WAMR_H
#define EXOS_WAMR_H
#include <semaphore.h>
#ifndef SEM_FAILED
#define SEM_FAILED ((sem_t *)0)
#endif
sem_t *sem_open(const char *nome, int flag, ...);
int    sem_close(sem_t *s);
int    sem_unlink(const char *nome);
/* only for the code of AOT and JIT, which wants low addresses: off here */
#include <sys/mman.h>
#ifndef MAP_32BIT
#define MAP_32BIT 0
#endif
#endif

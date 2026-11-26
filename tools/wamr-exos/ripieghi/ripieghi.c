/* tools/wamr-exos/ripieghi/ripieghi.c — see exos_wamr.h
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <errno.h>
#include "exos_wamr.h"

sem_t *sem_open(const char *nome, int flag, ...)
{
    (void)nome; (void)flag;
    errno = ENOSYS;
    return SEM_FAILED;
}

int sem_close(sem_t *s) { (void)s; errno = ENOSYS; return -1; }
int sem_unlink(const char *nome) { (void)nome; errno = ENOSYS; return -1; }

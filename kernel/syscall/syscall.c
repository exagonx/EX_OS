/* =============================================================================
 * kernel/syscall/syscall.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#include "kernel.h"
#include "idt.h"
#include "syscall.h"
#include "sched.h"

static SyscallFn syscall_table[SYSCALL_COUNT];

int syscall_verify_ptr(const void *ptr, uint32_t size)
{
    uint32_t addr = (uint32_t)ptr;
    if (ptr == NULL)                  return 0;
    if (addr < 0x1000)                return 0;
    if (addr >= USER_SPACE_END)       return 0;
    if (addr + size < addr)           return 0;
    if (addr + size > USER_SPACE_END) return 0;
    return 1;
}

int syscall_verify_str(const char *str, uint32_t max_len)
{
    uint32_t i;
    if (!syscall_verify_ptr(str, 1)) return 0;
    for (i = 0; i < max_len; i++) {
        if ((uint32_t)(str + i) >= USER_SPACE_END) return 0;
        if (str[i] == '\0') return 1;
    }
    return 0;
}

void syscall_handler(InterruptFrame *frame)
{
    uint32_t num = frame->eax;
    int32_t  ret;

    if (num >= SYSCALL_COUNT || syscall_table[num] == NULL) {
        klog(LOG_WARN, "SYSCALL: %u non implementata (PID %u)",
             num, proc_get_current() ? proc_get_current()->pid : 0);
        frame->eax = (uint32_t)ERR(ENOSYS);
        return;
    }

    klog(LOG_DEBUG, "SYSCALL: %u ebx=0x%x ecx=0x%x edx=0x%x PID=%u",
         num, frame->ebx, frame->ecx, frame->edx,
         proc_get_current() ? proc_get_current()->pid : 0);

    ret = syscall_table[num](frame);
    frame->eax = (uint32_t)ret;
}

void syscall_init(void)
{
    uint32_t i, count = 0;
    klog(LOG_INFO, "SYSCALL: inizializzazione tabella...");
    for (i = 0; i < SYSCALL_COUNT; i++) syscall_table[i] = NULL;

    syscall_table[SYS_EXIT]        = sys_exit;
    syscall_table[SYS_SPAWN]       = sys_spawn;
    syscall_table[SYS_READ]        = sys_read;
    syscall_table[SYS_WRITE]       = sys_write;
    syscall_table[SYS_OPEN]        = sys_open;
    syscall_table[SYS_CLOSE]       = sys_close;
    syscall_table[SYS_WAITPID]     = sys_waitpid;
    syscall_table[SYS_GETPID]      = sys_getpid;
    syscall_table[SYS_GETPPID]     = sys_getppid;
    syscall_table[SYS_MMAP]        = sys_mmap;
    syscall_table[SYS_MUNMAP]      = sys_munmap;
    syscall_table[SYS_IOCTL]       = sys_ioctl;
    syscall_table[SYS_EXEC]        = sys_exec;
    syscall_table[SYS_SCHED_YIELD] = sys_sched_yield;
    syscall_table[SYS_SLEEP]       = sys_sleep;
    syscall_table[SYS_SBRK]        = sys_sbrk;
    syscall_table[SYS_GETCWD]      = sys_getcwd;
    syscall_table[SYS_CHDIR]       = sys_chdir;
    syscall_table[SYS_STAT]        = sys_stat;
    syscall_table[SYS_LSEEK]       = sys_lseek;
    syscall_table[SYS_READDIR]     = sys_readdir;
    syscall_table[SYS_GETENV]      = sys_getenv;
    syscall_table[SYS_MKDIR]       = sys_mkdir;
    syscall_table[SYS_RMDIR]       = sys_rmdir;
    syscall_table[SYS_UNLINK]      = sys_unlink;
    syscall_table[SYS_VERSION]     = sys_version;
    syscall_table[SYS_UPTIME]      = sys_uptime;
    syscall_table[SYS_MEMINFO]     = sys_meminfo;
    syscall_table[SYS_PROCINFO]    = sys_procinfo;
    syscall_table[SYS_DISKINFO]    = sys_diskinfo;
    syscall_table[SYS_BLKINFO]     = sys_blkinfo;
    syscall_table[SYS_MOUNT]       = sys_mount;
    syscall_table[SYS_UMOUNT]      = sys_umount;
    syscall_table[SYS_MOUNTINFO]   = sys_mountinfo;
    syscall_table[SYS_BOOTINSTALL] = sys_bootinstall;
    syscall_table[SYS_PARTWRITE]   = sys_partwrite;
    syscall_table[SYS_BLKREAD]     = sys_blkread;
    syscall_table[SYS_BLKWRITE]    = sys_blkwrite;
    syscall_table[SYS_TRUNCATE]    = sys_truncate;
    syscall_table[SYS_REBOOT]      = sys_reboot;
    syscall_table[SYS_IPC_SEND]     = sys_ipc_send;
    syscall_table[SYS_IPC_RECV]     = sys_ipc_recv;
    syscall_table[SYS_IPC_REGISTER] = sys_ipc_register;
    syscall_table[SYS_IPC_LOOKUP]   = sys_ipc_lookup;
    syscall_table[SYS_IRQ_BIND]     = sys_irq_bind;
    syscall_table[SYS_IOPORT_BIND]  = sys_ioport_bind;
    syscall_table[SYS_IOPORT_IN]    = sys_ioport_in;
    syscall_table[SYS_IOPORT_OUT]   = sys_ioport_out;

    for (i = 0; i < SYSCALL_COUNT; i++) if (syscall_table[i]) count++;
    klog(LOG_INFO, "SYSCALL: %u syscall registrate (int 0x80)", count);
}

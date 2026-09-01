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
#include "vfs.h"   /* vfs_sync: chi esce interrotto riversa come chi esce da se' */

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

    /* =========================================================================
     * ! CHI E' STATO INTERROTTO MUORE QUI, E NON DOVE E' STATO INTERROTTO.
     *
     * SYS_INTERROMPI alza un flag e sveglia: non tocca il processo. Ammazzarlo
     * dov'era — dentro il kernel, con una pagina mappata a meta', un lucchetto
     * preso, un settore in volo — vuol dire lasciare quello stato li' per
     * sempre. Questo e' il punto sicuro: la syscall e' finita, le sue strutture
     * sono a posto, e da qui si torna in ring 3.
     *
     * ! IL PROCESSO INTERROTTO NON DEVE MAI RIENTRARE IN RING 3, e per questo
     * il controllo sta qui e non nell'uscita di una singola syscall: qualunque
     * cosa avesse chiesto — leggere, scrivere, dormire — la risposta e' che non
     * la finisce.
     *
     * ! E UN CICLO CHE NON CHIAMA MAI IL KERNEL NON SI FERMA, ed e' dichiarato.
     * Un programma che gira senza chiedere niente a nessuno non passa mai di
     * qui. Fermarlo vorrebbe dire controllare il flag nel gestore del timer,
     * cioe' terminare un processo da dentro un IRQ — proc_exit() fa vfs_sync(),
     * che aspetta il disco: la strada per farlo esiste ma e' un lavoro suo.
     * ========================================================================= */
    if (proc_interrotto()) {
        Process *p = proc_get_current();

        klog(LOG_INFO, "SYSCALL: PID %u interrotto, esce", p ? p->pid : 0);
        vfs_sync();
        proc_exit(130);         /* 128 + 2, come Unix riporta un Ctrl+C */
    }
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
    syscall_table[SYS_DUP]         = sys_dup;
    syscall_table[SYS_PIPE]        = sys_pipe;
    syscall_table[SYS_RENAME]      = sys_rename;
    syscall_table[SYS_DUP2]        = sys_dup2;
    syscall_table[SYS_FCNTL]       = sys_fcntl;
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
    syscall_table[SYS_IOPORT_IN16]  = sys_ioport_in16;
    syscall_table[SYS_IOPORT_OUT16] = sys_ioport_out16;
    syscall_table[SYS_IOPORT_IN32]  = sys_ioport_in32;
    syscall_table[SYS_IOPORT_OUT32] = sys_ioport_out32;
    syscall_table[SYS_IRQ_DONE]     = sys_irq_done;
    syscall_table[SYS_IRQ_UNBIND]   = sys_irq_unbind;
    syscall_table[SYS_DMA_ALLOC]    = sys_dma_alloc;
    syscall_table[SYS_MMIO_MAP]     = sys_mmio_map;
    syscall_table[SYS_POLL]         = sys_poll;
    syscall_table[SYS_MODO_TESTO]   = sys_modo_testo;
    syscall_table[SYS_VIDEO_INFO]   = sys_video_info;
    syscall_table[SYS_LOG]          = sys_log;
    syscall_table[SYS_LIB_APRI]     = sys_lib_apri;
    syscall_table[SYS_LIB_TROVA]    = sys_lib_trova;
    syscall_table[SYS_FB_MAP]       = sys_fb_map;
    syscall_table[SYS_INTERROMPI]   = sys_interrompi;
    syscall_table[SYS_PTY_APRI]     = sys_pty_apri;
    syscall_table[SYS_PTY_CTL]      = sys_pty_ctl;
    syscall_table[SYS_STATPERM]     = sys_statperm;
    syscall_table[SYS_SU]           = sys_su;
    syscall_table[SYS_GETUID]       = sys_getuid;
    syscall_table[SYS_SETUID]       = sys_setuid;
    syscall_table[SYS_CHOWN]        = sys_chown;
    syscall_table[SYS_CHMOD]        = sys_chmod;
    syscall_table[SYS_SHM_APRI]     = sys_shm_apri;
    syscall_table[SYS_SHM_CHIUDI]   = sys_shm_chiudi;
    syscall_table[SYS_RANDOM]       = sys_random;
    syscall_table[SYS_IPC_RECV_TMO] = sys_ipc_recv_tmo;
    syscall_table[SYS_TIME]         = sys_time;
    syscall_table[SYS_CONSOLE_SWITCH] = sys_console_switch;
    syscall_table[SYS_CONSOLE_WRITE]  = sys_console_write;
    syscall_table[SYS_CONSOLE_INFO]   = sys_console_info;
    syscall_table[SYS_CONSOLE_SETFG]  = sys_console_setfg;
    syscall_table[SYS_CONSOLE_GRAFICA] = sys_console_grafica;

    for (i = 0; i < SYSCALL_COUNT; i++) if (syscall_table[i]) count++;
    klog(LOG_INFO, "SYSCALL: %u syscall registrate (int 0x80)", count);
}

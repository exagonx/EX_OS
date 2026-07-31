/* =============================================================================
 * kernel/include/syscall.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "kernel.h"
#include "idt.h"

/* =============================================================================
 * Numeri di syscall (stile Linux x86)
 * ============================================================================= */
#define SYS_EXIT        1
#define SYS_SPAWN       2      /* crea processo figlio autonomo (vedi sys_spawn) */
#define SYS_READ        3
#define SYS_WRITE       4
#define SYS_OPEN        5
#define SYS_CLOSE       6
#define SYS_WAITPID     7
#define SYS_GETPID      20
#define SYS_GETPPID     64
#define SYS_MMAP        90
#define SYS_MUNMAP      91
#define SYS_IOCTL       54
#define SYS_EXEC        11
#define SYS_SCHED_YIELD 158
#define SYS_SLEEP       162
#define SYS_SBRK        45
#define SYS_GETCWD      183
#define SYS_CHDIR       12
#define SYS_STAT        106
#define SYS_LSEEK       19
#define SYS_READDIR     141    /* elenca una directory (vedi sys_readdir) */
#define SYS_GETENV      184    /* legge una variabile [env] di /boot/kernel.cfg */
#define SYS_MKDIR        39    /* crea una directory */
#define SYS_RMDIR        40    /* cancella una directory vuota */
#define SYS_UNLINK       10    /* cancella un file */
#define SYS_VERSION     185    /* copia g_os_version (identità del sistema) */
#define SYS_UPTIME      186    /* millisecondi dall'avvio (vedi sys_uptime) */
#define SYS_MEMINFO     187    /* stato della memoria per fascia (vedi MemInfo) */
#define SYS_PROCINFO    188    /* elenca i processi e i loro stack (vedi ProcInfo) */
#define SYS_DISKINFO    189    /* disco fisico + tabella partizioni (vedi DiskInfo) */
#define SYS_BLKINFO     190    /* elenca i dispositivi a blocchi (vedi BlkInfo) */
#define SYS_MOUNT       191    /* monta un dispositivo su un punto */
#define SYS_UMOUNT      192    /* smonta un punto di montaggio */
#define SYS_MOUNTINFO   193    /* elenca i montaggi attivi (vedi MountInfo) */
#define SYS_BOOTINSTALL 194    /* installa MBR + settore di avvio (vedi bootinst.h) */
#define SYS_PARTWRITE   195    /* riscrive la tabella delle partizioni (vedi PartTabella) */
#define SYS_BLKREAD     196    /* legge settori da una partizione NON montata */
#define SYS_BLKWRITE    197    /* scrive settori in una partizione NON montata */
#define SYS_TRUNCATE     92    /* cambia la dimensione di un file (vedi sys_truncate) */
#define SYS_REBOOT       88    /* spegne, riavvia o ferma il sistema */

/* Numero totale syscall supportate */
#define SYSCALL_COUNT   228     /* deve coprire il numero syscall più alto (SYS_IOPORT_OUT=227) + 1 */

/* =============================================================================
 * Codici errno
 * ============================================================================= */
#define EOK         0
#define EPERM       1
#define ENOENT      2
#define EINTR       4
#define EIO         5
#define EBADF       9
#define ENOMEM      12
#define EACCES      13
#define EFAULT      14
#define EBUSY       16
#define EEXIST      17
#define EINVAL      22
#define EMFILE      24
#define ENOSPC      28
#define EPIPE       32
#define ENOSYS      38
#define ESRCH       3       /* processo destinatario non esiste */
#define ENOTDIR     20      /* atteso una directory, trovato altro */
#define ENOTEMPTY   39      /* directory non vuota (rmdir) */
#define EISDIR      21      /* atteso un file, trovata una directory */
#define EROFS       30      /* filesystem montato in sola lettura */
#define ENODEV      19      /* dispositivo assente */
#define EFBIG       27      /* file troppo grande per l'operazione */
#define ESPIPE      29      /* qui: file frammentato, non mappabile a un intervallo */

/* Syscall IPC — comunicazione kernel-mediata tra task ring3 */
#define SYS_IPC_SEND     220
#define SYS_IPC_RECV     221
#define SYS_IPC_REGISTER 222
#define SYS_IPC_LOOKUP   223

/* Syscall hardware kernel-mediato — driver ring3 */
#define SYS_IRQ_BIND      224   /* rivendica un IRQ hardware, notifiche via IPC */
#define SYS_IOPORT_BIND   225   /* richiede un range di porte I/O in whitelist */
#define SYS_IOPORT_IN     226   /* legge un byte da una porta nel proprio range */
#define SYS_IOPORT_OUT    227   /* scrive un byte su una porta nel proprio range */

/* Converti errno in valore di ritorno negativo */
#define ERR(e)      (-(int32_t)(e))

/* =============================================================================
 * Flag per sys_open
 * ============================================================================= */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_NONBLOCK  0x0800

/* =============================================================================
 * Flag per sys_mmap
 * ============================================================================= */
#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20
#define MAP_FIXED       0x10

/* =============================================================================
 * Struttura stat (per sys_stat)
 * ============================================================================= */
typedef struct {
    uint32_t    st_size;        /* Dimensione file */
    uint32_t    st_first_clus;  /* Primo cluster FAT12 */
    uint16_t    st_attr;        /* Attributi FAT12 */
    uint16_t    st_date;        /* Data modifica */
    uint16_t    st_time;        /* Ora modifica */
} Stat;

/* =============================================================================
 * Struttura voce di directory (per sys_readdir)
 * ============================================================================= */
#define DIRENT_NAME_MAX 13   /* "NOME8.EXT3" + NUL, formato 8.3 */

typedef struct {
    char     name[DIRENT_NAME_MAX];
    uint32_t size;
    uint8_t  is_dir;
} DirEntry;

/* =============================================================================
 * Stato della memoria fisica, per fascia (per sys_meminfo)
 *
 * Tutti i valori sono in KB e si riferiscono alla memoria FISICA, contata
 * interrogando la bitmap del PMM. "usata" si ricava come totale - libera.
 *
 * LE FASCE, e perche' sono queste. Sono le tre dell'architettura PC, non
 * una scelta di questo sistema:
 *
 *   convenzionale  0x00000-0x9FFFF   i primi 640 KB
 *   superiore/UMA  0xA0000-0xFFFFF   384 KB riservati a BIOS, video, ROM
 *   estesa (XMS)   da 0x100000       tutto il resto, dove vive il sistema
 *
 * MEMORIA ESPANSA (EMS): i campi ems_* esistono e valgono SEMPRE ZERO.
 * Non e' una lacuna da colmare — l'espansa e' un meccanismo a banchi
 * commutati (una scheda EMS, o un emulatore tipo EMM386) che affaccia
 * finestre di memoria dentro l'area superiore, ed era il modo di superare
 * il limite di 1 MB del modo reale su 8086/286. Su un 386+ in modo
 * protetto con paginazione quel limite non esiste: tutta la RAM oltre 1 MB
 * e' direttamente indirizzabile come memoria estesa. EX-OS non ha, e non
 * avrebbe motivo di avere, un gestore EMS. I campi restano per rendere la
 * risposta esplicita invece di far sembrare che manchi un dato.
 * ============================================================================= */
typedef struct {
    uint32_t conv_total_kb, conv_free_kb;   /* convenzionale, < 640 KB   */
    uint32_t uma_total_kb,  uma_free_kb;    /* superiore, 640 KB - 1 MB  */
    uint32_t ext_total_kb,  ext_free_kb;    /* estesa, >= 1 MB           */
    uint32_t ems_total_kb,  ems_free_kb;    /* espansa: sempre 0, v. sopra */
    uint32_t total_kb,      free_kb;        /* complessivi               */
    uint32_t page_size;                     /* granularita' del PMM      */
} MemInfo;

/* =============================================================================
 * Descrizione di un processo e dei suoi stack (per sys_procinfo)
 *
 * Espone gli indirizzi grezzi invece di dimensioni gia' calcolate: sono
 * loro a dire COME lo stack e' stato allocato, che e' la domanda vera.
 * Le dimensioni si ricavano per differenza, e il programma che le mostra
 * puo' scegliere come presentarle.
 *
 *   ustack_top    indirizzo piu' alto dello stack utente (fisso)
 *   ustack_base   pagina piu' bassa ATTUALMENTE mappata. Scende quando lo
 *                 stack cresce su fault: top - base = RAM impegnata ORA
 *   ustack_limit  confine della riserva. top - limit = spazio riservato.
 *                 Vale 0 per i task kernel, che non hanno stack utente
 *   kstack_base/top  stack kernel, allocato per intero alla creazione del
 *                 processo e mai cresciuto: la' KERNEL_STACK_SIZE e' ancora
 *                 una dimensione fissa
 *
 * Un ustack_limit a 0 con ustack_top a 0 identifica un task kernel (idle,
 * init): non e' un dato mancante.
 * ============================================================================= */
#define PROCINFO_NAME_MAX   32   /* deve coincidere con PROCESS_NAME_LEN */
#define PROCINFO_MAX_BATCH  16   /* voci per chiamata: limita lo stack kernel */

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t state;          /* ProcState: 1=READY 2=RUNNING 3=BLOCKED
                              * 4=ZOMBIE 5=SLEEPING */
    uint32_t prio;
    char     name[PROCINFO_NAME_MAX];
    uint32_t ustack_top;
    uint32_t ustack_base;
    uint32_t ustack_limit;
    uint32_t kstack_base;
    uint32_t kstack_top;
} ProcInfo;


/* =============================================================================
 * Disco fisico e sua tabella delle partizioni (per sys_diskinfo)
 *
 * I 64 bit viaggiano SPEZZATI in due uint32 (_lo/_hi). Non e' eleganza
 * mancata: questa struttura attraversa l'ABI della syscall ed e'
 * duplicata a mano in tre file (qui, lib/include/libc.h, lib/libc.c). Un
 * uint64_t dentro una struct condivisa introdurrebbe un allineamento a 8
 * byte su cui kernel e libc devono concordare esattamente; con due
 * uint32 il problema non esiste. Le stringhe sono dimensionate a
 * multipli di 4 per lo stesso motivo.
 *
 * settori_*  quanto il disco dichiara ORA (IDENTIFY DEVICE)
 * nativi_*   capacita' di fabbrica (READ NATIVE MAX ADDRESS)
 * clippato   1 se nativi > settori: c'e' spazio NASCOSTO, tipicamente una
 *            HPA o un jumper di limitazione. E' il caso in cui un disco
 *            da 64 GB si presenta come da 32.
 * ============================================================================= */
#define DISKINFO_MAX_PART   16

typedef struct {
    uint32_t attiva;        /* 0x80 = avviabile */
    uint32_t tipo;          /* byte di tipo MBR */
    uint32_t logica;        /* 1 = dentro la partizione estesa */
    uint32_t numero;        /* numero alla fdisk: 1-4 primarie, 5+ logiche */
    uint32_t inizio_lo, inizio_hi;
    uint32_t settori_lo, settori_hi;
    /* Filesystem riconosciuto leggendo il BPB della partizione (vedi
     * kernel/block/vol.c). fs_tipo usa i valori VOL_FS_*: 0 sconosciuto,
     * 12/16/32 la larghezza della FAT, 255 illeggibile. Il tipo NON viene
     * dal byte di tipo MBR, che e' solo un suggerimento: viene dal numero
     * di cluster, che e' l'unico criterio corretto. */
    uint32_t fs_tipo;
    uint32_t fs_incoerente;
    uint32_t fs_sett_per_clu;
    uint32_t fs_n_cluster;
    char     fs_etichetta[12];
} PartInfo;

typedef struct {
    uint32_t presente;
    uint32_t tipo;          /* 0 nessuno, 1 ATA, 2 ATAPI, 3 sconosciuto */
    uint32_t canale;        /* 0 primario, 1 secondario */
    uint32_t unita;         /* 0 master, 1 slave */
    uint32_t lba48;
    uint32_t hpa;
    uint32_t clippato;
    uint32_t settori_lo, settori_hi;
    uint32_t nativi_lo,  nativi_hi;
    char     modello[44];
    char     seriale[24];
    char     firmware[12];
    uint32_t schema;        /* 0 nessuno, 1 MBR, 2 GPT (protettivo) */
    uint32_t problemi;      /* maschera PT_PROB_* di kernel/include/mbr.h */
    uint32_t n_part;
    PartInfo part[DISKINFO_MAX_PART];
} DiskInfo;

/* =============================================================================
 * Un dispositivo a blocchi (per sys_blkinfo)
 *
 * `primo` e `settori` descrivono la FINESTRA: ogni accesso viene tradotto
 * e rifiutato se ne esce. E' cio' che impedisce a un filesystem montato
 * su una partizione di toccarne un'altra o la tabella delle partizioni.
 * ============================================================================= */
#define BLKINFO_NOME_MAX    12

typedef struct {
    char     nome[BLKINFO_NOME_MAX];  /* "fd0", "hd0", "hd0p1" */
    uint32_t tipo;                    /* 1 floppy, 2 disco intero, 3 partizione */
    uint32_t sola_lettura;
    uint32_t primo_lo, primo_hi;      /* LBA di partenza nel supporto */
    uint32_t settori_lo, settori_hi;  /* lunghezza della finestra */
} BlkInfo;

/* =============================================================================
 * Un montaggio attivo (per sys_mountinfo)
 * ============================================================================= */
#define MOUNTINFO_PUNTO_MAX 24

/* Flag di sys_mount (edx) */
#define MNT_SOLA_LETTURA    0x0001

typedef struct {
    char     punto[MOUNTINFO_PUNTO_MAX];  /* "/", "/disk" */
    char     dev[BLKINFO_NOME_MAX];       /* "fd0", "hd0p1" */
    uint32_t fs;                          /* 12, 16, 32 */
    uint32_t sola_lettura;
} MountInfo;

/* Esito dell'installazione dell'avvio (per sys_bootinstall) */
typedef struct {
    uint32_t s2_lba, s2_cnt;
    uint32_t k_lba,  k_cnt;
    uint32_t disco;
    uint32_t voce;
} BootInstallInfo;

/* =============================================================================
 * Tabella delle partizioni PROPOSTA (per sys_partwrite)
 *
 * Si passano sempre tutti e quattro gli SLOT delle primarie, anche quelli
 * liberi (tipo = 0). Una tabella e' un oggetto unico: consegnarla una
 * voce alla volta produrrebbe stati intermedi in cui le partizioni si
 * sovrappongono, e il kernel non avrebbe modo di validare l'insieme.
 *
 * Le partizioni LOGICHE non si toccano da qui: il kernel rifiuta la
 * proposta se sposta o rimuove l'estesa che le contiene (vedi mbr.h).
 *
 * `problemi` e' un campo di USCITA: quando la chiamata rifiuta con
 * -EINVAL ci trovi la maschera PT_PROB_* di kernel/include/mbr.h, cioe'
 * QUALE controllo non e' passato. Un rifiuto senza quel dettaglio
 * lascerebbe l'utente a indovinare cosa c'e' di sbagliato in una tabella
 * che a occhio sembra sensata.
 * ============================================================================= */
#define PARTWRITE_MAX_VOCI  4

typedef struct {
    uint32_t attiva;                    /* 0x00, oppure 0x80 = avviabile */
    uint32_t tipo;                      /* byte di tipo MBR; 0 = slot libero */
    uint32_t inizio_lo,  inizio_hi;     /* LBA assoluto del primo settore */
    uint32_t settori_lo, settori_hi;    /* lunghezza in settori */
} PartVoce;

typedef struct {
    uint32_t problemi;                  /* USCITA: maschera PT_PROB_* */
    PartVoce voce[PARTWRITE_MAX_VOCI];  /* voce[i] = slot i+1 */
} PartTabella;

/* =============================================================================
 * Accesso ai settori grezzi di una partizione (sys_blkread / sys_blkwrite)
 *
 * Serve a /bin/mkfs: un formattatore scrive strutture — BPB, tabelle FAT,
 * directory radice — che nessun filesystem montato sa produrre, perche' il
 * filesystem e' proprio cio' che sta creando.
 *
 * PERCHE' QUI LA CONCLUSIONE E' OPPOSTA A QUELLA DI bootinst.c. Li' la
 * logica sta nel kernel perche' l'installatore scrive FUORI da ogni
 * filesystem, nel settore 0, dove un errore rende irraggiungibile un disco
 * intero. Un formattatore invece scrive solo DENTRO una partizione, cioe'
 * dentro una finestra che il livello a blocchi fa gia' rispettare: non
 * c'e' niente da proteggere che blk_write() non protegga.
 *
 * Le quattro condizioni, e cosa impedisce ognuna:
 *
 *   solo BLK_TIPO_PART   il disco intero e il floppy non sono nominabili.
 *                        E' cio' che rende il settore 0 — la tabella delle
 *                        partizioni — IRRAGGIUNGIBILE da userspace: non
 *                        esiste un dispositivo che lo contenga e sia
 *                        accettato qui.
 *   non in uso           una partizione montata ha una cache write-back
 *                        sopra (vedi fat.c): scriverci sotto significa che
 *                        il primo fat_sync() ripristina i vecchi settori
 *                        sopra i nuovi. In lettura darebbe dati che non
 *                        corrispondono a quelli che il filesystem crede
 *                        di avere.
 *   non in sola lettura  lo stesso vincolo dei montaggi.
 *   n <= BLKIO_MAX_SETT  limita il lavoro per chiamata. Il kernel copia un
 *                        settore per volta con un buffer di 512 byte, non
 *                        n settori insieme: il costo sullo stack kernel non
 *                        cresce col numero richiesto.
 *
 * ebx = nome*  ("hd0p1")   ecx = lba RELATIVO   edx = n settori
 * esi = buf*   (n * 512 byte)
 *
 * Ritorna il numero di settori trasferiti, o un errno negativo.
 * ============================================================================= */
#define BLKIO_MAX_SETT      64      /* 32 KB per chiamata */

/* =============================================================================
 * Struttura parametri mmap (passata come puntatore in EBX)
 * ============================================================================= */
typedef struct {
    uint32_t    addr;
    uint32_t    length;
    uint32_t    prot;
    uint32_t    flags;
    int32_t     fd;
    uint32_t    offset;
} MmapParams;

/* =============================================================================
 * Tipo handler syscall
 * Ogni syscall riceve l'InterruptFrame e ritorna int32_t
 * (valore positivo = successo, negativo = errore)
 * ============================================================================= */
typedef int32_t (*SyscallFn)(InterruptFrame *frame);

/* =============================================================================
 * Interfaccia pubblica
 * ============================================================================= */
void    syscall_init(void);
void    syscall_handler(InterruptFrame *frame);

/* Implementazioni singole syscall (definite in syscall_impl.c) */
int32_t sys_exit(InterruptFrame *f);
int32_t sys_spawn(InterruptFrame *f);
int32_t sys_read(InterruptFrame *f);
int32_t sys_write(InterruptFrame *f);
int32_t sys_open(InterruptFrame *f);
int32_t sys_close(InterruptFrame *f);
int32_t sys_waitpid(InterruptFrame *f);
int32_t sys_getpid(InterruptFrame *f);
int32_t sys_getppid(InterruptFrame *f);
int32_t sys_mmap(InterruptFrame *f);
int32_t sys_munmap(InterruptFrame *f);
int32_t sys_ioctl(InterruptFrame *f);
int32_t sys_exec(InterruptFrame *f);
int32_t sys_sched_yield(InterruptFrame *f);
int32_t sys_sleep(InterruptFrame *f);
int32_t sys_sbrk(InterruptFrame *f);
int32_t sys_getcwd(InterruptFrame *f);
int32_t sys_chdir(InterruptFrame *f);
int32_t sys_stat(InterruptFrame *f);
int32_t sys_lseek(InterruptFrame *f);
int32_t sys_readdir(InterruptFrame *f);
int32_t sys_getenv(InterruptFrame *f);
int32_t sys_mkdir(InterruptFrame *f);
int32_t sys_rmdir(InterruptFrame *f);
int32_t sys_unlink(InterruptFrame *f);
int32_t sys_version(InterruptFrame *f);
int32_t sys_uptime(InterruptFrame *f);
int32_t sys_meminfo(InterruptFrame *f);
int32_t sys_procinfo(InterruptFrame *f);
int32_t sys_diskinfo(InterruptFrame *f);
int32_t sys_blkinfo(InterruptFrame *f);
int32_t sys_mount(InterruptFrame *f);
int32_t sys_umount(InterruptFrame *f);
int32_t sys_mountinfo(InterruptFrame *f);
int32_t sys_bootinstall(InterruptFrame *f);
int32_t sys_partwrite(InterruptFrame *f);
int32_t sys_blkread(InterruptFrame *f);
int32_t sys_blkwrite(InterruptFrame *f);
int32_t sys_truncate(InterruptFrame *f);
int32_t sys_reboot(InterruptFrame *f);
int32_t sys_ipc_send(InterruptFrame *f);
int32_t sys_ipc_recv(InterruptFrame *f);
int32_t sys_ipc_register(InterruptFrame *f);
int32_t sys_ipc_lookup(InterruptFrame *f);
int32_t sys_irq_bind(InterruptFrame *f);
int32_t sys_ioport_bind(InterruptFrame *f);
int32_t sys_ioport_in(InterruptFrame *f);
int32_t sys_ioport_out(InterruptFrame *f);

/* Verifica indirizzo utente (evita accessi kernel da ring3) */
int     syscall_verify_ptr(const void *ptr, uint32_t size);
int     syscall_verify_str(const char *str, uint32_t max_len);

#endif /* SYSCALL_H */

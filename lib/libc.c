/* =============================================================================
 * lib/libc.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Libreria C minimale per programmi utente EX-OS.
 *
 * Compilata come ELF shared object (/lib/libc.so).
 * I programmi utente la linkano dinamicamente per condividere il codice
 * e ridurre l'uso di memoria su floppy.
 *
 * Funzioni implementate:
 *   Stringa:   strlen, strcpy, strncpy, strcmp, strncmp, strcat, strncat,
 *              strchr, strrchr, strstr, strdup, strspn, strcspn, strtok
 *   Memoria:   memset, memcpy, memmove, memcmp, memchr
 *   Stdio:     FILE* bufferizzati (fopen/fread/fwrite/fseek/...), printf e
 *              le sue varianti su un solo formattatore
 *   Stdlib:    malloc/free/realloc/calloc con riuso, atoi, atol, strtol,
 *              strtoul, qsort, bsearch, abs, labs, exit, abort
 *   Altro:     errno + strerror, setjmp/longjmp, ctype
 *   Syscall:   wrappers per tutte le syscall EX-OS
 *
 * -----------------------------------------------------------------------
 * QUESTO FILE E' AUTOSUFFICIENTE, e non e' una svista: il Makefile lo
 * compila SENZA -I lib/include (vedi le regole dei programmi di /bin),
 * quindi non puo' includere libc.h. Tipi, numeri di syscall e costanti
 * sono ripetuti qui e devono restare allineati a mano con l'header — la
 * stessa convenzione gia' usata per DirEntry, MemInfo e i numeri di
 * syscall duplicati fra kernel e libc.
 * ============================================================================= */

/* Tipi base.
 *
 * size_t, ptrdiff_t e NULL vengono da <stddef.h>, che e' un header del
 * COMPILATORE e non della libreria: si include anche qui, dove non c'e'
 * -I lib/include, senza intaccare l'autosufficienza di questo file.
 *
 * Definirli a mano sarebbe stato peggio che ridondante: il gcc di sistema
 * con -m32 dice che size_t e' `unsigned int`, il bersaglio i386-exos dice
 * `long unsigned int`, e la stessa libc compilata dai due avrebbe avuto
 * prototipi diversi da quelli dell'header. Vedi lib/include/libc.h, dove
 * la scelta e' spiegata per esteso e deve restare la stessa. */
#include <stddef.h>

typedef unsigned int        uint32_t;
typedef unsigned short      uint16_t;
typedef unsigned char       uint8_t;
typedef int                 int32_t;
typedef unsigned long long  uint64_t;
typedef __PTRDIFF_TYPE__    ssize_t;
typedef __UINTPTR_TYPE__    uintptr_t;

/* ⚠️ DEVONO COINCIDERE CON QUELLI DI lib/include/libc.h, riga per riga.
 * Questo file non include il proprio header — e' autosufficiente di
 * proposito, vedi il commento qui sopra — quindi i due elenchi sono
 * separati e un disaccordo si manifesta come "conflicting types" al primo
 * programma che li usa entrambi. Qui ci sono solo quelli che servono alle
 * funzioni definite in questo file; gli altri stanno solo nell'header. */
typedef int                 pid_t;
typedef long                off_t;
typedef unsigned int        mode_t;
/* ⚠️ Gli altri tipi di POSIX che compaiono in `struct stat`. Duplicati da
 * lib/include/libc.h e devono restare identici: quella struttura la
 * riempiamo noi e la legge il chiamante, e un tipo diverso fra le due
 * copie non da' un errore — da' campi letti storti. */
typedef unsigned int        dev_t;
typedef unsigned int        ino_t;
typedef unsigned int        nlink_t;
typedef unsigned int        uid_t;
typedef unsigned int        gid_t;
typedef unsigned int        blksize_t;
typedef unsigned int        blkcnt_t;

/* Lo stato di una conversione multibyte. Duplicato da lib/include/libc.h,
 * come gli altri tipi: questo file non include il proprio header. */
typedef struct {
    int __nulla;
} mbstate_t;

/* Quoziente e resto. Anche questi duplicati da lib/include/libc.h: i
 * campi si chiamano `quot` e `rem` perche' lo dice lo standard C, e un
 * programma di terzi li nomina per esteso. */
typedef struct { int  quot; int  rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;

/* Duplicati da lib/include/inttypes.h. ⚠️ intmax_t E' `long long` su questo
 * bersaglio: e' il tipo intero piu' grande che ha, e lo standard dice che
 * intmax_t dev'essere quello. */
typedef long long           intmax_t;
typedef unsigned long long  uintmax_t;
typedef struct { intmax_t quot; intmax_t rem; } imaxdiv_t;

/* La posizione in un flusso come oggetto opaco. Duplicato da
 * lib/include/libc.h: qui e' un long, ma il contratto e' che nessuno ci
 * faccia aritmetica sopra. */
typedef long fpos_t;

/* Duplicato da lib/include/libc.h, come tutto il resto: questo file non
 * include il proprio header. Deve restare -1. */
#define EOF (-1)

/* Il flusso bufferizzato: la struttura sta piu' avanti, qui serve il nome
 * perche' le funzioni che lo usano vengono prima. */
typedef struct _FILE FILE;

/* Dichiarazioni anticipate. Questo file e' scritto in ordine di
 * argomento, non di dipendenza — lo stdio viene prima dell'allocatore
 * perche' e' li' che un lettore lo cerca — e senza queste righe fdopen()
 * chiamerebbe una malloc() non ancora dichiarata. Su GCC 14 non e' un
 * avviso: e' un errore (vedi KERNEL_CORE_NOTES.md). */
void   *malloc(size_t size);
void    free(void *ptr);
size_t  strlen(const char *s);
void   *memcpy(void *dst, const void *src, size_t n);
void   *memset(void *dst, int c, size_t n);
int     strncmp(const char *a, const char *b, size_t n);
char   *strchr(const char *s, int c);
int     isspace(int c);
int     isdigit(int c);
double  strtod(const char *s, char **fine);
unsigned int uptime_ms(void);
long    fsize(int fd);
void    abort(void);
/* Il valore di una cifra in una base qualsiasi: sta con le conversioni
 * numeriche, ma sscanf viene prima e ne ha bisogno. */
static int cifra_valore(int c);

/* =============================================================================
 * errno — e perche' NON cambia il valore di ritorno di nessuna funzione
 *
 * Su Unix una syscall fallita ritorna -1 e mette il motivo in errno. Su
 * EX-OS le funzioni ritornano l'errore NEGATIVO direttamente (-2 = ENOENT,
 * -30 = EROFS...), e tutti i programmi di /bin stampano quel numero.
 *
 * Cambiare convenzione avrebbe voluto dire riscrivere ogni chiamante per
 * guadagnare zero: `< 0` resta il test giusto in entrambi i mondi, e un
 * -EIO e' piu' informativo di un -1. Quindi errno viene IMPOSTATO in piu',
 * non al posto del ritorno: chi vuole un messaggio usa strerror(errno),
 * chi vuole il codice ce l'ha gia' in mano.
 * ============================================================================= */
int errno = 0;

/* Informazioni su un file (SYS_STAT). Duplicata da
 * kernel/include/syscall.h, come le altre strutture che attraversano
 * l'ABI. st_attr usa le convenzioni FAT anche sugli altri filesystem:
 * 0x10 = directory, 0x01 = sola lettura. */
typedef struct {
    uint32_t    st_size;
    uint32_t    st_first_clus;
    uint16_t    st_attr;
    uint16_t    st_date;
    uint16_t    st_time;
} Stat;

/* ⚠️ QUI C'ERANO DUE MACRO CON IL NOME SBAGLIATO (tolte nel 0.150):
 *
 *     #define S_ISDIR(attr)   (((attr) & 0x10) != 0)
 *     #define S_ISREG(attr)   (((attr) & 0x10) == 0)
 *
 * Lavoravano sull'ATTRIBUTO FAT di `Stat`, non sul `st_mode` POSIX di
 * `struct stat`, e portavano il nome delle macro standard — che piu' sotto
 * questo stesso file definisce di nuovo, correttamente, sopra S_IFMT.
 * Nessuno le usava, finche' opendir() non ha scritto la riga piu' naturale
 * del mondo, `S_ISDIR(st.st_mode)`, e si e' presa la prima delle due:
 * 0040755 & 0x10 fa zero, quindi opendir("/") rispondeva "non e' una
 * directory". Il test lo ha preso al primo giro.
 *
 * Per l'attributo FAT esiste gia' EXOS_ATTR_DIR() in libc.h, che si chiama
 * come cio' che fa. */



/* Costanti di lseek e di open, duplicate da kernel/include/syscall.h. */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

/* Numeri syscall */
#define SYS_EXIT        1
#define SYS_READ        3
#define SYS_WRITE       4
#define SYS_OPEN        5
#define SYS_CLOSE       6
#define SYS_SPAWN        2
#define SYS_WAITPID      7
#define SYS_GETPID      20
#define SYS_EXEC        11
#define SYS_MMAP        90
#define SYS_MUNMAP      91
#define SYS_SBRK        45
#define SYS_SCHED_YIELD 158
#define SYS_SLEEP       162
#define SYS_GETCWD      183
#define SYS_GETENV      184
#define SYS_MKDIR        39
#define SYS_RMDIR        40
#define SYS_UNLINK       10
#define SYS_VERSION     185
#define SYS_UPTIME      186
#define SYS_MEMINFO     187
#define SYS_PROCINFO    188
#define SYS_DISKINFO    189
#define SYS_BLKINFO     190
#define SYS_MOUNT       191
#define SYS_UMOUNT      192
#define SYS_MOUNTINFO   193
#define SYS_BOOTINSTALL 194
#define SYS_PARTWRITE   195
#define SYS_BLKREAD     196
#define SYS_BLKWRITE    197
#define SYS_TRUNCATE     92
#define SYS_CHDIR       12
#define SYS_LSEEK        19
#define SYS_STAT        106
#define SYS_READDIR     141
#define SYS_IPC_SEND     220
#define SYS_IPC_RECV     221
#define SYS_IPC_REGISTER 222
#define SYS_IPC_LOOKUP   223
#define SYS_IRQ_BIND      224
#define SYS_IOPORT_BIND   225
#define SYS_IOPORT_IN     226
#define SYS_IOPORT_OUT    227
#define SYS_IOPORT_IN16   233
#define SYS_IOPORT_OUT16  234
#define SYS_IOPORT_IN32   235
#define SYS_IOPORT_OUT32  236
#define SYS_IRQ_DONE      237
#define SYS_DMA_ALLOC     239
#define SYS_RANDOM        240

/* ⚠️ DEVE RESTARE IDENTICA a DmaZona in kernel/include/syscall.h e in
 * lib/include/libc.h. La riempie il kernel scrivendo nella memoria del
 * processo: due definizioni che divergono danno un indirizzo fisico letto
 * dal campo sbagliato, cioe' una scheda che fa DMA dove capita. */
typedef struct {
    unsigned int byte;
    unsigned int virt;
    unsigned int fisico;
} DmaZona;
#define SYS_IPC_RECV_TMO  228
#define SYS_TIME           13
#define SYS_CONSOLE_SWITCH 229
#define SYS_CONSOLE_WRITE  230
#define SYS_CONSOLE_INFO   231
#define SYS_IOCTL         54
#define SYS_DUP           41
#define SYS_DUP2          63
#define SYS_FCNTL         55
#define SYS_PIPE          42
#define SYS_RENAME        38

/* Comandi ioctl del terminale — devono restare identici a
 * drivers/tty/tty.h e a lib/include/libc.h (stessa convenzione di
 * DirEntry qui sotto: questo file non include il proprio header). */
#define TTY_IOCTL_GETSIZE    0x01
#define TTY_IOCTL_SETRAW     0x02
#define TTY_IOCTL_SETCOOKED  0x03
#define TTY_IOCTL_CLEAR      0x04
#define TTY_IOCTL_SETCOLOR   0x05

typedef struct {
    uint16_t rows;
    uint16_t cols;
    uint16_t xpixel;
    uint16_t ypixel;
} TtyWinSize;

/* Voce di directory — deve restare identica a kernel/include/syscall.h
 * (DirEntry) e a lib/include/libc.h: attraversa l'ABI della syscall. */
#define DIRENT_NAME_MAX 256
typedef struct {
    char           name[DIRENT_NAME_MAX];
    unsigned int   size;
    unsigned char  is_dir;
} DirEntry;

/* Nomi degli errori. Stessa convenzione del resto del file: duplicati da
 * lib/include/libc.h perche' libc.c si compila SENZA -I, e allineati alla
 * tabella di strerror() qui sotto. */
#define EPERM         1
#define ENOENT        2
#define ESRCH         3
#define EINTR         4
#define EIO           5
#define EBADF         9
#define ECHILD       10
#define EAGAIN       11
#define ENOMEM       12
#define EACCES       13
#define EFAULT       14
#define EBUSY        16
#define EEXIST       17
#define ENODEV       19
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define EMFILE       24
#define ENOTTY       25
#define ENOSPC       28
#define EROFS        30
#define ENOSYS       38
#define ENOTEMPTY    39
#define ENAMETOOLONG 36
#define EILSEQ       84
#define EDOM         33

/* La lunghezza massima di un percorso: VFS_PATH_MAX del kernel, e le due
 * devono restare uguali. Duplicata anche in <limits.h> e <sys/param.h>. */
#define PERCORSO_MAX 320

/* Numero massimo di voci per chiamata a listdir: vedi libc.h. */
#define LISTDIR_MAX_BATCH 16

/* spawn con ambiente e redirezioni — duplicate da kernel/include/syscall.h
 * e da lib/include/libc.h. La magia impedisce al kernel di leggere ESI
 * quando lo chiama un programma compilato per la vecchia forma. */
/* ⚠️ La magia e' 0x53504E59 e non piu' ...58: e' cambiata la disposizione
 * di SpawnAzione. Vedi lib/include/libc.h. */
#define SPAWN_EXTRA_MAGIA    0x53504E59u
#define SPAWN_MAX_AZIONI     4
#define SPAWN_RED_PATH_MAX   128
#define SPAWN_AZ_FILE   0
#define SPAWN_AZ_FD     1

typedef struct {
    unsigned int tipo;
    unsigned int fd;
    unsigned int flags;
    int          fd_padre;
    char         percorso[SPAWN_RED_PATH_MAX];
} SpawnAzione;

typedef struct {
    unsigned int magia;
    char       **envp;
    unsigned int n_azioni;
    SpawnAzione  azioni[SPAWN_MAX_AZIONI];
} SpawnExtra;

typedef struct {
    int         fd;
    int         flags;
    const char *percorso;   /* NULL = passa il descrittore `fd_padre` */
    int         fd_padre;
} SpawnRedir;

int spawn_ex(const char *path, char *const argv[], char *const envp[],
             const SpawnRedir *redir, int n_redir);

/* Directory nella forma POSIX */
#define DT_UNKNOWN  0
#define DT_REG      8
#define DT_DIR      4

struct dirent {
    unsigned int  d_ino;
    unsigned char d_type;
    char          d_name[DIRENT_NAME_MAX];
};
typedef struct __dir DIR;

/* Dichiarazioni anticipate: queste funzioni sono definite piu' in basso
 * ma servono qui sopra (mkstemp usa access, rename usa unlink, raise usa
 * strsignal). In un file solo l'ordine non puo' accontentare tutti. */
extern char **environ;
struct stat;
int         stat(const char *path, struct stat *st);
int         access(const char *path, int modo);
int         unlink(const char *path);
char *strsignal(int sig);
char       *strdup(const char *s);
int         tolower(int c);     /* strcasecmp, molto piu' su di dove sta */
char       *getenv(const char *chiave);   /* tmp_componi legge TMPDIR */

/* Modi di access() */
#define F_OK    0
#define X_OK    1
#define W_OK    2
#define R_OK    4

/* Segnali: i nomi ci sono, la consegna no */
#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGABRT  6
#define SIGFPE   8
#define SIGKILL  9
#define SIGSEGV 11
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIG_MAX 32
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

/* sysconf */
#define _SC_ARG_MAX             0
#define _SC_OPEN_MAX            4
#define _SC_PAGESIZE           30
/* pathconf: duplicati da lib/include/libc.h come tutto il resto. */
#define _PC_LINK_MAX            0
#define _PC_NAME_MAX            3
#define _PC_PATH_MAX            4
#define _PC_CHOWN_RESTRICTED    6
#define _PC_NO_TRUNC            7
#define _SC_CLK_TCK             2
#define _SC_NPROCESSORS_ONLN   84

typedef long clock_t;
struct tms { clock_t tms_utime, tms_stime, tms_cutime, tms_cstime; };

/* Stato della memoria — deve restare identico a kernel/include/syscall.h
 * (MemInfo) e a lib/include/libc.h: attraversa l'ABI della syscall, e
 * sys_meminfo rifiuta la chiamata se le sizeof non coincidono. */
typedef struct {
    unsigned int conv_total_kb, conv_free_kb;
    unsigned int uma_total_kb,  uma_free_kb;
    unsigned int ext_total_kb,  ext_free_kb;
    unsigned int ems_total_kb,  ems_free_kb;
    unsigned int total_kb,      free_kb;
    unsigned int page_size;
} MemInfo;

/* Processo + stack — deve restare identico a kernel/include/syscall.h
 * (ProcInfo) e a lib/include/libc.h: attraversa l'ABI della syscall. */
#define PROCINFO_NAME_MAX   32
typedef struct {
    unsigned int pid;
    unsigned int ppid;
    unsigned int state;
    unsigned int prio;
    char         name[PROCINFO_NAME_MAX];
    unsigned int ustack_top;
    unsigned int ustack_base;
    unsigned int ustack_limit;
    unsigned int kstack_base;
    unsigned int kstack_top;
} ProcInfo;


/* Disco + partizioni — deve restare identico a kernel/include/syscall.h
 * (DiskInfo/PartInfo) e a lib/include/libc.h. I 64 bit viaggiano spezzati
 * in _lo/_hi per non dover concordare un allineamento a 8 byte fra kernel
 * e libc. */
#define DISKINFO_MAX_PART   16
typedef struct {
    unsigned int attiva, tipo, logica;
    unsigned int numero;   /* numero alla fdisk: 1-4 primarie, 5+ logiche */
    unsigned int inizio_lo, inizio_hi;
    unsigned int settori_lo, settori_hi;
    unsigned int fs_tipo;          /* 0 sconosciuto, 12/16/32, 255 illeggibile */
    unsigned int fs_incoerente;
    unsigned int fs_sett_per_clu;
    unsigned int fs_n_cluster;
    char         fs_etichetta[12];
} PartInfo;
typedef struct {
    unsigned int presente, tipo, canale, unita;
    unsigned int lba48, hpa, clippato;
    unsigned int settori_lo, settori_hi;
    unsigned int nativi_lo,  nativi_hi;
    char         modello[44];
    char         seriale[24];
    char         firmware[12];
    unsigned int schema, problemi, n_part;
    PartInfo     part[DISKINFO_MAX_PART];
} DiskInfo;

#define BLKINFO_NOME_MAX    12
#define MOUNTINFO_PUNTO_MAX 24
typedef struct {
    char         nome[BLKINFO_NOME_MAX];
    unsigned int tipo;
    unsigned int sola_lettura;
    unsigned int primo_lo, primo_hi;
    unsigned int settori_lo, settori_hi;
} BlkInfo;

/* Montaggio attivo — deve restare identico a kernel/include/syscall.h
 * (MountInfo) e a lib/include/libc.h: attraversa l'ABI della syscall, e
 * sys_mountinfo rifiuta la chiamata se le sizeof non coincidono. */
typedef struct {
    char         punto[MOUNTINFO_PUNTO_MAX];
    char         dev[BLKINFO_NOME_MAX];
    unsigned int fs;
    unsigned int sola_lettura;
} MountInfo;

/* Esito dell'installazione dell'avvio — identico a kernel/include/syscall.h
 * (BootInstallInfo) e a lib/include/libc.h. */
typedef struct {
    unsigned int s2_lba, s2_cnt;
    unsigned int k_lba,  k_cnt;
    unsigned int k_next;
    unsigned int disco;
    unsigned int voce;
} BootInstallInfo;

/* Tabella delle partizioni proposta — identica a kernel/include/syscall.h
 * (PartVoce/PartTabella) e a lib/include/libc.h: attraversa l'ABI della
 * syscall, e sys_partwrite rifiuta la chiamata se le sizeof non
 * coincidono. */
#define PARTWRITE_MAX_VOCI  4
typedef struct {
    unsigned int attiva;
    unsigned int tipo;
    unsigned int inizio_lo,  inizio_hi;
    unsigned int settori_lo, settori_hi;
} PartVoce;
typedef struct {
    unsigned int problemi;
    PartVoce     voce[PARTWRITE_MAX_VOCI];
} PartTabella;

/* Messaggio IPC — deve restare identico a kernel/include/sched.h
 * (IpcMessage) e a lib/include/libc.h: attraversa l'ABI della syscall. */
/* 1536 = un frame Ethernet intero; il perche' e' in kernel/include/sched.h.
 * Qui non c'e' data[]: ipc_recv scrive il payload nel buffer separato che
 * gli si passa, e questa struttura porta solo l'intestazione. Vedi il
 * commento in libc.h. */
#define IPC_MSG_MAX_DATA 1536
typedef struct {
    unsigned int  sender_pid;
    unsigned int  tipo;      /* si chiama cosi' anche in libc.h: vedi li' */
    unsigned int  len;
} IpcMessage;

/* Data e ora — deve restare identica a kernel/include/rtc.h (RtcTime)
 * e a lib/include/libc.h: attraversa l'ABI della syscall. */
typedef struct {
    unsigned int anno, mese, giorno, ora, minuto, secondo;
} RtcTime;

/* Data e ora nella forma del C standard. Duplicate a mano da
 * lib/include/time.h e lib/include/sys/time.h — questo file si compila
 * senza -I lib/include, come dice la nota in testa.
 *
 * time_t e' `long` e non `long long`: a 32 bit con segno arriva al 2038,
 * che per un sistema del 2026 e' un problema vero ma non di oggi, mentre
 * allargarlo adesso cambierebbe la dimensione di ogni struttura che lo
 * contiene. Il posto in cui cambiarlo e' questo, e va cambiato anche
 * nell'header. */
/* =============================================================================
 * ⚠️ 64 BIT E NON 32, dal kernel 0.175 — e non e' solo il 2038
 *
 * Un `time_t` a 32 bit con segno finisce il 19 gennaio 2038. Su un sistema
 * scritto nel 2026 sarebbe una scadenza scritta in partenza, ed e' la
 * ragione per cui i Linux a 32 bit sono passati a time64.
 *
 * Ma il difetto che l'ha reso urgente e' un altro, ed e' aritmetico. GCC
 * misura il tempo cosi' (gcc/timevar.cc):
 *
 *     now->wall = tv.tv_sec * 1000000000 + tv.tv_usec * 1000;
 *
 * Con tv_sec a 32 bit quella moltiplicazione TRABOCCA prima di essere
 * allargata — 1,7 miliardi per un miliardo non ci sta — e il rapporto dei
 * tempi di cc1 usciva con fasi da 18446744071 secondi, cioe' differenze
 * negative lette come senza segno. Non e' codice di GCC da correggere: e'
 * codice giusto su un time_t giusto.
 * ============================================================================= */
typedef long long time_t;

struct tm {
    int tm_sec;     /* 0..60 (il 60 e' il secondo intercalare) */
    int tm_min;     /* 0..59 */
    int tm_hour;    /* 0..23 */
    int tm_mday;    /* 1..31 */
    int tm_mon;     /* 0..11 — gennaio e' ZERO */
    int tm_year;    /* anni dal 1900 */
    int tm_wday;    /* 0..6, domenica = 0 */
    int tm_yday;    /* 0..365 */
    int tm_isdst;   /* sempre 0: EX-OS non conosce l'ora legale */
};

/* ⚠️ tv_sec E' time_t, NON long, E QUESTA RIGA E' COSTATA CARA.
 *
 * Quando time_t e' passato a 64 bit questa struttura e' rimasta indietro:
 * lib/include/libc.h diceva 16 byte con un tv_sec da 8, questo file
 * scriveva 8 byte con un tv_sec da 4. La libc e il suo stesso header non
 * erano d'accordo sulla struttura che si scambiano.
 *
 * Il sintomo non nominava niente di tutto questo. gettimeofday() tornava
 * un tv_sec con la meta' bassa GIUSTA e la meta' alta piena di tv_usec,
 * cioe' un orologio grande un milione di volte il dovuto. `cc1` ci
 * calcolava sopra i tempi delle fasi, il suo autocontrollo trovava che la
 * somma delle parti superava il totale, e moriva con
 *
 *     internal compiler error: in validate_phases, at timevar.cc:553
 *
 * DOPO aver compilato — lasciando un .s con dentro solo l'intestazione.
 * Sembrava un difetto di GCC.
 *
 * La prova che lo coglie sta in bin/libctest: stampa le due meta' da 32
 * bit separate, perche' un %lld rotto avrebbe potuto mentire pure lui. */
struct timeval {
    time_t tv_sec;
    long   tv_usec;
};

/* I parametri di mmap, duplicati da kernel/include/syscall.h: e' l'ABI
 * della syscall, e cambiarla vuol dire cambiare il kernel. */
typedef struct {
    uint32_t    addr;
    uint32_t    length;
    uint32_t    prot;
    uint32_t    flags;
    int32_t     fd;
    uint32_t    offset;
} MmapParams;

#define MAP_ANONYMOUS   0x20
#define MAP_FAILED      ((void *)-1)
#define RUSAGE_SELF      0
#define RUSAGE_CHILDREN (-1)

/* Duplicata da lib/include/libc.h. ⚠️ Solo `ru_utime` e `ru_stime` vengono
 * riempiti, e il primo con un limite superiore invece che una misura: vedi
 * getrusage() piu' avanti. */
struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long ru_maxrss;
    long ru_ixrss;
    long ru_idrss;
    long ru_isrss;
    long ru_minflt;
    long ru_majflt;
    long ru_nswap;
    long ru_inblock;
    long ru_oublock;
    long ru_msgsnd;
    long ru_msgrcv;
    long ru_nsignals;
    long ru_nvcsw;
    long ru_nivcsw;
};

/* Duplicata da lib/include/libc.h come tutto il resto. ⚠️ La risoluzione
 * vera e' 10 ms: tv_nsec e' sempre un multiplo di 10 000 000. */
struct timespec {
    long tv_sec;
    long tv_nsec;
};
#define TIME_UTC 1

/* Le convenzioni numeriche della locale. Duplicata da lib/include/libc.h:
 * ⚠️ l'ORDINE DEI CAMPI deve combaciare riga per riga, non solo i nomi —
 * qui e' il chiamante a leggere la struttura che noi riempiamo, e due
 * disposizioni diverse darebbero campi scambiati senza nessun errore. */
struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *int_curr_symbol;
    char *currency_symbol;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
    char  int_frac_digits;
    char  frac_digits;
    char  p_cs_precedes;
    char  p_sep_by_space;
    char  n_cs_precedes;
    char  n_sep_by_space;
    char  p_sign_posn;
    char  n_sign_posn;
    char  int_p_cs_precedes;
    char  int_p_sep_by_space;
    char  int_n_cs_precedes;
    char  int_n_sep_by_space;
    char  int_p_sign_posn;
    char  int_n_sign_posn;
};

/* Attributi FAT e forma POSIX di stat: duplicati da lib/include/libc.h e
 * da lib/include/sys/stat.h. Il perche' dei due tipi affiancati sta
 * nell'header; qui basta sapere che devono restare identici. */
#define EXOS_ATTR_DIR(attr)     (((attr) & 0x10) != 0)
#define EXOS_ATTR_RDONLY(attr)  (((attr) & 0x01) != 0)

#define S_IFMT      0170000
#define S_IFDIR     0040000
#define S_IFREG     0100000

/* Le due macro standard, che qui dentro mancavano: c'erano solo i nomi
 * dei tipi. Vedi il commento sulle due omonime tolte in testa al file. */
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)

/* ⚠️ I TIPI DI POSIX, non `unsigned int`: deve combaciare CAMPO PER CAMPO
 * e TIPO PER TIPO con lib/include/libc.h. Il motivo per cui i tipi contano
 * anche quando la larghezza e' la stessa sta spiegato li'. */
struct stat {
    dev_t           st_dev;
    ino_t           st_ino;
    mode_t          st_mode;
    nlink_t         st_nlink;
    uid_t           st_uid;
    gid_t           st_gid;
    off_t           st_size;
    blksize_t       st_blksize;
    blkcnt_t        st_blocks;
    time_t          st_atime;
    time_t          st_mtime;
    time_t          st_ctime;
};

/* Console virtuali — deve restare identica a kernel/include/syscall.h
 * (ConsoleInfo) e a lib/include/libc.h. */
typedef struct {
    unsigned int totale;
    unsigned int mia;
    unsigned int visibile;
    unsigned int fg;
} ConsoleInfo;

/* =============================================================================
 * Syscall wrappers
 * ============================================================================= */

/* Cinque argomenti: EDI e' il quinto. Serve a SYS_BOOTINSTALL, che deve
 * passare percorso, struttura, dimensione, modalita' e i nomi alternativi
 * — e non si e' voluto un numero di syscall nuovo per una variante che
 * cambia solo se scrive o no. */
static inline int32_t _syscall5(uint32_t n, uint32_t a, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e)
{
    int32_t r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
        : "memory");
    return r;
}

static inline int32_t _syscall4(uint32_t n, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    int32_t r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d)
        : "memory");
    return r;
}

static inline int32_t _syscall3(uint32_t n, uint32_t a, uint32_t b, uint32_t c)
{
    int32_t r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "a"(n), "b"(a), "c"(b), "d"(c)
        : "memory");
    return r;
}

static inline int32_t _syscall2(uint32_t n, uint32_t a, uint32_t b)
{
    return _syscall3(n, a, b, 0);
}

static inline int32_t _syscall1(uint32_t n, uint32_t a)
{
    return _syscall3(n, a, 0, 0);
}

/* =============================================================================
 * Funzioni stringa
 * ============================================================================= */

size_t strlen(const char *s)
{
    size_t n = 0;
    while (*s++) n++;
    return n;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (n-- && (*d++ = *src++));
    while (n--) *d++ = '\0';
    return dst;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n-- && *a && *b && *a == *b) { a++; b++; }
    return n == (size_t)-1 ? 0 : ((unsigned char)*a - (unsigned char)*b);
}

/* Confronto senza distinzione fra maiuscole e minuscole.
 *
 * Non sono nel C standard — stanno in <strings.h>, che e' POSIX — ma le
 * chiama tutto: bfd le usa per riconoscere il nome di un'architettura
 * scritto come capita. Il confronto passa da tolower(), quindi segue le
 * regole della locale "C", l'unica che EX-OS ha: sopra il 127 non
 * converte niente, e va bene cosi' finche' i nomi restano ASCII. */
int strcasecmp(const char *a, const char *b)
{
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    while (n && *a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++; b++; n--;
    }
    if (n == 0) return 0;
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/* Nella locale "C" — l'unica che EX-OS ha — l'ordine di collazione E'
 * l'ordine dei byte, quindi strcoll e strcmp sono la stessa funzione. C'e'
 * perche' chi ordina dei nomi per l'utente scrive strcoll: `nm` lo fa per
 * ordinare i simboli. */
int strcoll(const char *a, const char *b)
{
    return strcmp(a, b);
}

/* La trasformazione che rende strcmp equivalente a strcoll. Nella locale
 * "C" i due gia' coincidono, quindi qui e' una copia.
 *
 * ⚠️ RITORNA LA LUNGHEZZA DELL'ORIGINALE, non quella copiata, ed e' cio'
 * che permette al chiamante di accorgersi che il buffer era corto: se il
 * valore di ritorno e' >= n, il contenuto di dst non e' utilizzabile. Con
 * la lunghezza copiata non ci sarebbe modo di distinguere una copia
 * completa da una troncata. */
size_t strxfrm(char *dst, const char *src, size_t n)
{
    size_t len = strlen(src);
    size_t i;

    for (i = 0; i < n && i < len; i++) dst[i] = src[i];
    if (n > 0 && i < n) dst[i] = '\0';

    return len;
}

/* Il primo carattere di `s` che compare in `accetta`, o NULL. E' strcspn
 * che ritorna un puntatore invece di una lunghezza — e infatti le due si
 * usano nello stesso posto: gas ci cerca il primo separatore dentro il
 * nome di una sezione. */
char *strpbrk(const char *s, const char *accetta)
{
    for (; *s; s++) {
        const char *a;
        for (a = accetta; *a; a++)
            if (*s == *a) return (char *)s;
    }
    return NULL;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst + strlen(dst);
    while ((*d++ = *src++));
    return dst;
}

/* =============================================================================
 * Multibyte → caratteri larghi, nella sola locale che esiste
 *
 * Nella locale "C" — l'unica di EX-OS, vedi setlocale piu' avanti — ogni
 * byte E' un carattere. La conversione e' quindi una promozione da
 * `unsigned char` a `wchar_t`, e non c'e' nessuna sequenza da riconoscere
 * ne' nessuna che possa essere invalida.
 *
 * ⚠️ QUESTO VUOL DIRE CHE NON FALLISCE MAI, e chi la chiama per VERIFICARE
 * ottiene sempre un si'. gas la usa cosi': `mbstowcs(NULL, nome, 0)` per
 * capire se un nome di simbolo fra virgolette e' scrivibile nella locale
 * corrente, e su EX-OS la risposta e' sempre che lo e'. E' vero — un
 * byte qualunque e' un carattere valido qui — ma un file UTF-8 passato a
 * un sistema che ragiona a byte resta una sequenza di byte, non diventa
 * testo. Vedi <wchar.h> per il resto del ragionamento.
 *
 * Con `dst` NULL conta e basta, come vuole lo standard.
 * ============================================================================= */
size_t mbstowcs(wchar_t *dst, const char *src, size_t n)
{
    size_t i = 0;

    if (src == NULL) return (size_t)-1;

    if (dst == NULL) return strlen(src);

    while (i < n && src[i] != '\0') {
        dst[i] = (wchar_t)(unsigned char)src[i];
        i++;
    }
    if (i < n) dst[i] = 0;
    return i;
}

/* Un solo carattere per volta, con lo stato che non serve. Ritorna 1 (un
 * byte consumato), 0 sul NUL, e (size_t)-2 se `n` e' zero — cioe' «non ho
 * abbastanza byte per decidere», che qui non puo' succedere per altre
 * ragioni. Non ritorna mai (size_t)-1: vedi mbstowcs qui sopra. */
size_t mbrtowc(wchar_t *dst, const char *src, size_t n, mbstate_t *stato)
{
    (void)stato;

    if (src == NULL) return 0;
    if (n == 0)      return (size_t)-2;

    if (dst != NULL) *dst = (wchar_t)(unsigned char)*src;
    return (*src == '\0') ? 0 : 1;
}

size_t wcstombs(char *dst, const wchar_t *src, size_t n)
{
    size_t i = 0;

    if (src == NULL) return (size_t)-1;

    if (dst == NULL) {
        while (src[i] != 0) i++;
        return i;
    }

    while (i < n && src[i] != 0) {
        /* ⚠️ Un carattere sopra 255 non ci sta in un byte: qui la
         * conversione FALLISCE invece di troncare in silenzio, perche' un
         * troncamento silenzioso e' un nome di file sbagliato che sembra
         * giusto. */
        if ((unsigned long)src[i] > 0xFF) { errno = EILSEQ; return (size_t)-1; }
        dst[i] = (char)(unsigned char)src[i];
        i++;
    }
    if (i < n) dst[i] = '\0';
    return i;
}

/* =============================================================================
 * mblen, mbtowc, wctomb — le tre a carattere singolo
 *
 * Nella locale "C" un byte E' un carattere, quindi non c'e' niente da
 * convertire: contano fino a uno e promuovono. Esistono perche' <cstdlib>
 * della libstdc++ fa `using ::mblen;` e `using ::mbtowc;` senza chiedersi
 * se qualcuno le chiamera' — se il nome non c'e', l'header non compila.
 *
 * ⚠️ IL VALORE DI RITORNO E' int E NON size_t, al contrario delle `mbr*`
 * di sopra: le due famiglie hanno convenzioni diverse e mescolarle e' il
 * modo classico di sbagliare. Qui -1 e' errore, 0 e' il NUL, 1 e' un
 * carattere. La chiamata con `s == NULL` chiede «questa codifica ha uno
 * stato?» e la risposta e' 0, cioe' no.
 * ============================================================================= */
int mblen(const char *s, size_t n)
{
    if (s == NULL) return 0;            /* la codifica non ha stato */
    if (n == 0)    return -1;
    return (*s == '\0') ? 0 : 1;
}

int mbtowc(wchar_t *dst, const char *src, size_t n)
{
    if (src == NULL) return 0;
    if (n == 0)      return -1;

    if (dst != NULL) *dst = (wchar_t)(unsigned char)*src;
    return (*src == '\0') ? 0 : 1;
}

int wctomb(char *dst, wchar_t c)
{
    if (dst == NULL) return 0;

    /* Stessa regola di wcstombs: sopra 255 si FALLISCE invece di
     * troncare, perche' un troncamento silenzioso e' un carattere
     * sbagliato che sembra giusto. */
    if ((unsigned long)c > 0xFF) { errno = EILSEQ; return -1; }

    *dst = (char)(unsigned char)c;
    return 1;
}

char *strchr(const char *s, int c)
{
    while (*s && *s != (char)c) s++;
    return (*s == (char)c) ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    while (*s) { if (*s == (char)c) last = s; s++; }
    return (char *)last;
}

/* =============================================================================
 * Funzioni memoria
 * ============================================================================= */

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

/* =============================================================================
 * STDIO — flussi bufferizzati
 *
 * ⚠️ COSA C'ERA PRIMA. putchar() faceva una SYSCALL PER CARATTERE, e
 * printf() chiamava putchar() per ogni carattere formattato: una riga di
 * ottanta colonne costava ottanta cambi di contesto, e su una console con
 * lo specchio seriale a 38400 baud si vedeva a occhio nudo. Non c'era
 * nessun FILE*: leggere un file significava open/read/close a mano, con il
 * proprio buffer, in ogni programma che ne avesse bisogno.
 *
 * -----------------------------------------------------------------------
 * LA POLITICA DI BUFFERING, che e' la sola decisione non ovvia qui
 *
 * stdout e stderr sono bufferizzati DENTRO la singola chiamata e svuotati
 * alla sua fine (flag _F_AUTO). Cioe': un printf costa una syscall invece
 * di ottanta, ma verso l'esterno il comportamento e' identico a prima —
 * quando printf ritorna, i byte sono usciti.
 *
 * NON e' il line buffering di Unix, ed e' una scelta contro corrente che
 * vale la pena spiegare. Con il line buffering, tutto cio' che non finisce
 * con '\n' resta nel buffer: il prompt della shell ("ex-os:/> ") sparirebbe
 * fino alla riga successiva, e un programma che disegna a schermo — gfedit
 * — mostrerebbe l'ultima riga incompleta solo dopo il tasto seguente. Unix
 * se la cava perche' la lettura da stdin svuota stdout per convenzione;
 * qui pero' gfedit NON legge da stdin (parla via IPC con il servizio kbd),
 * quindi quella convenzione non lo salverebbe. Svuotare a fine chiamata
 * elimina la classe di problemi e conserva quasi tutto il guadagno.
 *
 * I FILE su disco sono invece bufferizzati per davvero (4 KB): li' nessuno
 * guarda lo schermo, e una write() per fputc renderebbe inutile l'intero
 * strato.
 *
 * -----------------------------------------------------------------------
 * UN FLUSSO ALLA VOLTA LEGGE O SCRIVE, NON ENTRAMBI
 *
 * Un FILE aperto "r+" tiene un buffer solo. Passare da lettura a scrittura
 * richiede fseek() o fflush(), come impone anche il C standard: qui la
 * regola e' applicata invece che documentata e basta — una write dopo una
 * read senza riposizionare scriverebbe dove e' arrivato il buffer, non
 * dove crede il chiamante.
 * ============================================================================= */

#define _F_LETT     0x0001  /* aperto in lettura            */
#define _F_SCRIT    0x0002  /* aperto in scrittura          */
#define _F_EOF      0x0004
#define _F_ERR      0x0008
#define _F_AUTO     0x0010  /* svuota alla fine di ogni chiamata */
#define _F_MIO      0x0020  /* buffer allocato da noi: free alla chiusura */
#define _F_INSCRIT  0x0040  /* il buffer contiene byte da scrivere */
#define _F_APPEND   0x0080

struct _FILE {
    int             fd;
    unsigned        flag;
    unsigned char  *buf;
    size_t          dim;    /* capacita' del buffer            */
    size_t          pos;    /* byte consumati (R) o accumulati (W) */
    size_t          fine;   /* byte validi nel buffer (solo R) */
    int             rimesso;/* carattere di ungetc, -1 se nessuno */
};

#define STDIN_BUF_SIZE  512     /* quanto una riga del driver kbd */
#define FILE_BUF_SIZE   4096

static unsigned char buf_stdin[STDIN_BUF_SIZE];
static unsigned char buf_stdout[512];
static unsigned char buf_stderr[128];

static FILE f_stdin  = { 0, _F_LETT,             buf_stdin,  STDIN_BUF_SIZE, 0, 0, -1 };
static FILE f_stdout = { 1, _F_SCRIT | _F_AUTO,  buf_stdout, sizeof(buf_stdout), 0, 0, -1 };
static FILE f_stderr = { 2, _F_SCRIT | _F_AUTO,  buf_stderr, sizeof(buf_stderr), 0, 0, -1 };

FILE *stdin  = &f_stdin;
FILE *stdout = &f_stdout;
FILE *stderr = &f_stderr;

/* I flussi aperti, per svuotarli tutti all'uscita. Un programma che
 * scrive un file e poi chiama exit() senza fclose() deve trovare il file
 * scritto: e' la trappola classica dei buffer, e la si chiude qui invece
 * di chiederlo a ogni chiamante. */
#define MAX_FLUSSI  16
static FILE *flussi[MAX_FLUSSI];
static int   n_flussi = 0;

static void registra_flusso(FILE *f)
{
    if (n_flussi < MAX_FLUSSI) flussi[n_flussi++] = f;
}

static void dimentica_flusso(FILE *f)
{
    int i;
    for (i = 0; i < n_flussi; i++) {
        if (flussi[i] == f) {
            flussi[i] = flussi[--n_flussi];
            return;
        }
    }
}

/* Scrive sul descrittore tutto cio' che il buffer ha accumulato. */
static int scarica(FILE *f)
{
    size_t scritti = 0;

    if (!(f->flag & _F_INSCRIT) || f->pos == 0) { f->pos = 0; return 0; }

    while (scritti < f->pos) {
        int32_t n = _syscall3(SYS_WRITE, (uint32_t)f->fd,
                              (uint32_t)(f->buf + scritti),
                              (uint32_t)(f->pos - scritti));
        /* Una write parziale non e' un errore: si insiste. Una che ritorna
         * zero o meno lo e', e insistere sarebbe un ciclo infinito. */
        if (n <= 0) { f->flag |= _F_ERR; f->pos = 0; return -1; }
        scritti += (size_t)n;
    }

    f->pos = 0;
    return 0;
}

/* Riempie il buffer di lettura. Ritorna 0, -1 a fine file o errore. */
static int riempi(FILE *f)
{
    int32_t n;

    if (f->pos < f->fine) return 0;
    if (!(f->flag & _F_LETT)) { f->flag |= _F_ERR; return -1; }

    n = _syscall3(SYS_READ, (uint32_t)f->fd, (uint32_t)f->buf,
                  (uint32_t)f->dim);
    if (n < 0)  { f->flag |= _F_ERR; return -1; }
    if (n == 0) { f->flag |= _F_EOF; return -1; }

    f->pos  = 0;
    f->fine = (size_t)n;
    return 0;
}

/* Da chiamare in uscita da ogni funzione pubblica che ha scritto. */
static void auto_scarica(FILE *f)
{
    if (f->flag & _F_AUTO) scarica(f);
}

static int mette(FILE *f, unsigned char c)
{
    if (!(f->flag & _F_SCRIT)) { f->flag |= _F_ERR; return -1; }

    /* Passaggio da lettura a scrittura: il buffer di lettura contiene
     * byte gia' presi dal kernel e non ancora consumati, e la posizione
     * vera del file e' piu' avanti di cosi'. Si riporta indietro il
     * descrittore, o la scrittura finirebbe dopo i byte scartati. */
    if (!(f->flag & _F_INSCRIT)) {
        if (f->fine > f->pos) {
            _syscall3(SYS_LSEEK, (uint32_t)f->fd,
                      (uint32_t)(int32_t)-(int32_t)(f->fine - f->pos), 1);
        }
        f->pos = f->fine = 0;
        f->flag |= _F_INSCRIT;
    }

    if (f->pos >= f->dim && scarica(f) != 0) return -1;

    f->buf[f->pos++] = c;
    return (int)c;
}

int fputc(int c, FILE *f)
{
    int r;

    if (f == NULL) return -1;
    r = mette(f, (unsigned char)c);
    auto_scarica(f);
    return r;
}

int putc(int c, FILE *f) { return fputc(c, f); }

int putchar(int c)
{
    return fputc(c, stdout);
}

int fputs(const char *s, FILE *f)
{
    int n = 0;

    if (s == NULL || f == NULL) return -1;
    while (*s) {
        if (mette(f, (unsigned char)*s++) < 0) { auto_scarica(f); return -1; }
        n++;
    }
    auto_scarica(f);
    return n;
}

int puts(const char *s)
{
    int n;

    if (s == NULL) s = "(null)";
    n = 0;
    while (s[n]) { if (mette(stdout, (unsigned char)s[n]) < 0) break; n++; }
    mette(stdout, '\n');
    auto_scarica(stdout);
    return n;
}

size_t fwrite(const void *ptr, size_t dim, size_t n, FILE *f)
{
    const unsigned char *p = (const unsigned char *)ptr;
    size_t tot, i;

    if (ptr == NULL || f == NULL || dim == 0 || n == 0) return 0;

    tot = dim * n;
    for (i = 0; i < tot; i++) {
        if (mette(f, p[i]) < 0) break;
    }
    auto_scarica(f);
    return i / dim;
}

int fgetc(FILE *f)
{
    if (f == NULL) return -1;

    if (f->rimesso >= 0) {
        int c = f->rimesso;
        f->rimesso = -1;
        return c;
    }

    if (f->flag & _F_INSCRIT) { scarica(f); f->flag &= ~(unsigned)_F_INSCRIT; }
    if (riempi(f) != 0) return -1;

    return (int)f->buf[f->pos++];
}

int getc(FILE *f) { return fgetc(f); }

int getchar(void)
{
    return fgetc(stdin);
}

int ungetc(int c, FILE *f)
{
    /* Un solo carattere di rimessa: e' il minimo garantito dal C standard
     * ed e' tutto cio' che serve a un parser che guarda avanti di uno. */
    if (f == NULL || c < 0 || f->rimesso >= 0) return -1;
    f->rimesso = c;
    f->flag &= ~(unsigned)_F_EOF;
    return c;
}

size_t fread(void *ptr, size_t dim, size_t n, FILE *f)
{
    unsigned char *p = (unsigned char *)ptr;
    size_t tot, i = 0;

    if (ptr == NULL || f == NULL || dim == 0 || n == 0) return 0;

    tot = dim * n;
    while (i < tot) {
        int c = fgetc(f);
        if (c < 0) break;
        p[i++] = (unsigned char)c;
    }
    return i / dim;
}

char *fgets(char *buf, int max, FILE *f)
{
    int i = 0, c;

    if (buf == NULL || max <= 0 || f == NULL) return NULL;

    while (i < max - 1) {
        c = fgetc(f);
        if (c < 0) break;
        buf[i++] = (char)c;
        if (c == '\n') break;
    }

    buf[i] = '\0';
    return (i > 0) ? buf : NULL;
}

/* gets() di EX-OS NON e' la gets() del C: prende la dimensione del buffer
 * (quindi non e' quella insicura) e toglie il fine riga. Firma e
 * comportamento restano quelli che i programmi di /bin gia' usano —
 * NULL su riga vuota compresa. */
char *gets(char *buf, int max)
{
    int i = 0, c;

    if (buf == NULL || max <= 0) return NULL;

    while (i < max - 1 && (c = fgetc(stdin)) >= 0 && c != '\n') {
        buf[i++] = (char)c;
    }
    buf[i] = '\0';
    return (i > 0) ? buf : NULL;
}

int fflush(FILE *f)
{
    int i, r = 0;

    /* fflush(NULL) svuota tutto, come da C standard. */
    if (f == NULL) {
        if (scarica(stdout) != 0) r = -1;
        if (scarica(stderr) != 0) r = -1;
        for (i = 0; i < n_flussi; i++) {
            if (scarica(flussi[i]) != 0) r = -1;
        }
        return r;
    }

    return scarica(f);
}

int feof(FILE *f)      { return (f && (f->flag & _F_EOF))  ? 1 : 0; }
int ferror(FILE *f)    { return (f && (f->flag & _F_ERR))  ? 1 : 0; }
void clearerr(FILE *f) { if (f) f->flag &= ~(unsigned)(_F_EOF | _F_ERR); }
int fileno(FILE *f)    { return f ? f->fd : -1; }

FILE *fdopen(int fd, const char *modo)
{
    FILE *f;

    if (fd < 0 || modo == NULL) return NULL;

    f = (FILE *)malloc(sizeof(FILE));
    if (f == NULL) return NULL;

    f->buf = (unsigned char *)malloc(FILE_BUF_SIZE);
    if (f->buf == NULL) { free(f); return NULL; }

    f->fd      = fd;
    f->dim     = FILE_BUF_SIZE;
    f->pos     = 0;
    f->fine    = 0;
    f->rimesso = -1;
    f->flag    = _F_MIO;

    if (modo[0] == 'r') f->flag |= _F_LETT  | (modo[1] == '+' ? _F_SCRIT : 0);
    if (modo[0] == 'w') f->flag |= _F_SCRIT | (modo[1] == '+' ? _F_LETT  : 0);
    if (modo[0] == 'a') f->flag |= _F_SCRIT | _F_APPEND | (modo[1] == '+' ? _F_LETT : 0);

    registra_flusso(f);
    return f;
}

FILE *fopen(const char *path, const char *modo)
{
    int   flags = 0;
    int   fd;
    FILE *f;

    if (path == NULL || modo == NULL) return NULL;

    switch (modo[0]) {
        case 'r': flags = (modo[1] == '+') ? O_RDWR : O_RDONLY;          break;
        case 'w':
            flags = (modo[1] == '+') ? (O_RDWR | O_CREAT | O_TRUNC)
                                     : (O_WRONLY | O_CREAT | O_TRUNC);
            break;
        case 'a':
            flags = (modo[1] == '+') ? (O_RDWR | O_CREAT | O_APPEND)
                                     : (O_WRONLY | O_CREAT | O_APPEND);
            break;
        default:  return NULL;
    }

    fd = _syscall3(SYS_OPEN, (uint32_t)path, (uint32_t)flags, 0);
    if (fd < 0) { errno = -fd; return NULL; }

    f = fdopen(fd, modo);
    if (f == NULL) { _syscall1(SYS_CLOSE, (uint32_t)fd); return NULL; }

    /* "a" scrive sempre in coda: ci si posiziona subito, cosi' anche una
     * ftell() fatta prima della prima scrittura dice la verita'. */
    if (f->flag & _F_APPEND) _syscall3(SYS_LSEEK, (uint32_t)fd, 0, 2);

    return f;
}

/* Riapre un flusso GIA' ESISTENTE su un altro file, tenendo lo stesso
 * FILE*. Serve a chi ha gia' consegnato il puntatore a qualcun altro e non
 * puo' cambiarlo — il caso classico e' `freopen("/log", "w", stderr)`, e
 * infatti il primo a chiederla e' stato fopen_unlocked.c di libiberty.
 *
 * ⚠️ IL DESCRITTORE PUO' CAMBIARE NUMERO. Su Unix freopen() riusa lo
 * stesso fd (chiude e riapre sullo stesso numero); qui si chiude e si
 * apre, quindi il numero e' quello che capita. Per stdout e stderr la
 * differenza si sente: dopo una freopen su stdout, il descrittore 1 NON e'
 * piu' il file — chi lo scrive con write(1, ...) invece che con printf
 * scrive altrove. Farlo davvero vorrebbe dire una dup2 dopo l'apertura, e
 * si potra' fare adesso che dup2 c'e'; non lo si e' fatto perche' nessuno
 * dei due casi d'uso in vista mescola i due livelli. */
FILE *freopen(const char *path, const char *modo, FILE *f)
{
    FILE *nuovo;

    if (f == NULL || modo == NULL) return NULL;

    /* Quel che era in sospeso va fuori PRIMA di perdere il descrittore:
     * riaprire un flusso non e' un motivo per buttare via cio' che ci si
     * era scritto dentro. */
    scarica(f);
    if (path == NULL) return NULL;   /* la variante POSIX "cambia i modi" no */

    if (f->fd > 2) _syscall1(SYS_CLOSE, (uint32_t)f->fd);

    nuovo = fopen(path, modo);
    if (nuovo == NULL) return NULL;

    /* Si copia il contenuto e si getta il guscio: quello che il chiamante
     * ha in mano e' `f`, e deve restare valido. Il buffer vecchio resta
     * dov'e' — e' di `f` — quindi si scarta quello nuovo. */
    f->fd      = nuovo->fd;
    f->flag    = (nuovo->flag & ~(unsigned)_F_MIO) | (f->flag & _F_MIO);
    f->pos     = 0;
    f->fine    = 0;
    f->rimesso = -1;

    dimentica_flusso(nuovo);
    free(nuovo->buf);
    free(nuovo);

    return f;
}

int fclose(FILE *f)
{
    int r = 0;

    if (f == NULL) return -1;

    if (scarica(f) != 0) r = -1;

    /* =====================================================================
     * ⚠️ SU stdin, stdout E stderr SI SVUOTA E BASTA — NON SI CHIUDE.
     *
     * Il kernel rifiuta close() sui descrittori 0, 1 e 2 di proposito:
     * lascerebbe il processo senza un posto dove dire che qualcosa e'
     * andato storto (vedi kernel/syscall/syscall_impl.c). La conseguenza
     * era che fclose(stdout) tornava -1, e i programmi scritti bene —
     * quelli che l'esito lo GUARDANO — lo trattavano come un guasto.
     *
     * Ci e' cascato `cc1`, che alla fine di ogni compilazione fa
     *
     *     if (ferror (stdout) || fclose (stdout))
     *         fatal_error (input_location, "%s: %m", "stdout");
     *
     * e moriva con «cc1: fatal error: stdout: descrittore non valido»
     * DOPO aver fatto tutto il lavoro. Il sintomo non nominava la causa in
     * nessun modo: sembrava un difetto del compilatore.
     *
     * Chi chiama fclose(stdout) alla fine del programma sta dicendo «ho
     * finito, svuota»: quello si puo' fare, e riesce. Il descrittore resta
     * aperto, che e' esattamente cio' che il kernel vuole garantire.
     * ===================================================================== */
    if (f->fd > 2) {
        if (_syscall1(SYS_CLOSE, (uint32_t)f->fd) < 0) r = -1;
    }

    dimentica_flusso(f);

    if (f->flag & _F_MIO) {
        free(f->buf);
        free(f);
    }
    return r;
}

long ftell(FILE *f)
{
    int32_t k;

    if (f == NULL) return -1;

    k = _syscall3(SYS_LSEEK, (uint32_t)f->fd, 0, 1);   /* SEEK_CUR */
    if (k < 0) { errno = -k; return -1; }

    /* Il descrittore e' avanti rispetto alla posizione logica di quanto
     * resta nel buffer di lettura, e indietro di quanto e' accumulato nel
     * buffer di scrittura. Restituire la posizione del kernel senza questa
     * correzione e' l'errore che fa scrivere gli indici sbagliati in ogni
     * formato di file che li contiene. */
    if (f->flag & _F_INSCRIT) return (long)k + (long)f->pos;

    return (long)k - (long)(f->fine - f->pos) - (f->rimesso >= 0 ? 1 : 0);
}

int fseek(FILE *f, long off, int whence)
{
    int32_t r;

    if (f == NULL) return -1;

    if (f->flag & _F_INSCRIT) {
        if (scarica(f) != 0) return -1;
        f->flag &= ~(unsigned)_F_INSCRIT;
    } else if (whence == 1 /* SEEK_CUR */) {
        /* Stessa correzione di ftell: uno spostamento RELATIVO deve
         * partire da dove crede il chiamante, non da dove e' arrivata la
         * lettura anticipata. */
        off -= (long)(f->fine - f->pos) + (f->rimesso >= 0 ? 1 : 0);
    }

    f->pos = f->fine = 0;
    f->rimesso = -1;
    f->flag &= ~(unsigned)_F_EOF;

    r = _syscall3(SYS_LSEEK, (uint32_t)f->fd, (uint32_t)off, (uint32_t)whence);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

void rewind(FILE *f)
{
    fseek(f, 0, 0);
    clearerr(f);
}

/* =============================================================================
 * fgetpos, fsetpos — ftell/fseek con un'altra faccia
 *
 * Su EX-OS non aggiungono niente: la posizione E' un numero. Esistono
 * perche' su un sistema con codifiche a stato variabile non lo sarebbe, e
 * perche' <cstdio> della libstdc++ le dichiara. ⚠️ Ritornano 0/-1, non la
 * posizione: chi le confonde con ftell legge sempre "inizio del file".
 * ============================================================================= */
int fgetpos(FILE *f, fpos_t *pos)
{
    long p;

    if (f == NULL || pos == NULL) { errno = EINVAL; return -1; }

    p = ftell(f);
    if (p < 0) return -1;

    *pos = (fpos_t)p;
    return 0;
}

int fsetpos(FILE *f, const fpos_t *pos)
{
    if (f == NULL || pos == NULL) { errno = EINVAL; return -1; }
    return fseek(f, (long)*pos, 0 /* SEEK_SET */);
}

/* =============================================================================
 * setbuf, setvbuf — ⚠️ CI SONO MA NON CAMBIANO NIENTE
 *
 * La politica di bufferizzazione di EX-OS e' decisa e documentata piu'
 * sopra (4 KB sui file, svuotamento a fine chiamata su stdout/stderr), e
 * non e' regolabile: i buffer stanno DENTRO la struttura FILE, non
 * allocati a parte, quindi non c'e' niente da sostituire.
 *
 * ⚠️ setvbuf RITORNA DIVERSO DA ZERO — «non l'ho fatto» — invece di
 * fingere. Un programma che chiede _IONBF e riceve 0 andrebbe avanti
 * convinto che ogni putc sia gia' arrivato a destinazione, e su un log di
 * debug quella e' esattamente la differenza fra vedere l'ultima riga prima
 * di un crash e non vederla.
 *
 * L'unico caso che si accetta e' la richiesta che gia' descrive cio' che
 * facciamo: _IOFBF con dimensione uguale alla nostra. Dire di no a quella
 * sarebbe rifiutare di aver fatto cio' che si e' fatto.
 * ============================================================================= */
int setvbuf(FILE *f, char *buf, int modo, size_t dim)
{
    (void)buf;

    if (f == NULL) { errno = EINVAL; return -1; }

    if (modo == 0 /* _IOFBF */ && dim == FILE_BUF_SIZE) return 0;

    errno = ENOSYS;
    return -1;
}

/* ⚠️ Non ritorna niente per definizione, quindi NON PUO' dire che non ha
 * funzionato. E' il motivo per cui lo standard stesso raccomanda setvbuf,
 * ed e' il motivo per cui qui non fa proprio niente invece di provarci. */
void setbuf(FILE *f, char *buf)
{
    (void)f;
    (void)buf;
}

/* =============================================================================
 * NOTA STORICA — getchar() e il TTY orientato alla riga (luglio 2026)
 *
 * getchar() faceva _syscall3(SYS_READ, 0, &c, 1), cioe' chiedeva UN byte
 * allo stdin. Il TTY di EX-OS pero' non e' orientato al carattere: il
 * servizio kbd accumula la riga e la consegna intera su Invio (vedi
 * drivers/kbd/kbd.c). Una read() da 1 byte faceva consumare al driver
 * l'INTERA riga per poi consegnarne un solo carattere: tutto il resto
 * veniva buttato, e gets() restituiva solo il primo carattere di ogni riga
 * digitata.
 *
 * Il rimedio di allora era un buffer di riga privato di getchar(). Da
 * agosto 2026 quel buffer NON esiste piu' come caso speciale: e' il
 * buffer di stdin, cioe' lo stesso meccanismo di qualunque altro flusso.
 * La proprieta' che contava — una read() per riga, non per carattere —
 * resta, e adesso vale anche per fgets() e fread().
 * ============================================================================= */

/* =============================================================================
 * IL FORMATTATORE, UNO SOLO PER TUTTE LE printf
 *
 * Supporta:  %d %i %u %x %X %o %c %s %p %%
 *   flag     '-' (sinistra), '0' (zeri), '+' e ' ' (segno), '#' (0x/0)
 *   ampiezza  numero oppure '*'
 *   precisione '.' numero oppure '.*'  (cifre minime, o lunghezza massima
 *              per %s)
 *   modificatori di lunghezza  h hh l ll z
 *
 * PERCHE' UNO SOLO. printf, fprintf, sprintf e snprintf differiscono per
 * DOVE finiscono i caratteri, non per come si formattano: con quattro
 * copie, il giorno che si corregge la larghezza di campo si corregge in
 * una sola e le altre tre restano sbagliate. Qui la destinazione e' una
 * struttura `Uscita` e il resto e' condiviso.
 *
 * LA VIRGOLA MOBILE C'E' da agosto 2026: %f, %e, %g e %a con le loro
 * maiuscole. Prima consumavano l'argomento e stampavano "<float>" —
 * bastava finche' nessuno stampava numeri, e il primo a farlo e' stato
 * cc1, il cui rapporto dei tempi usciva cosi':
 *
 *     phase setup : <float> (<float>%)   985k
 *
 * ⚠️ LE CIFRE SIGNIFICATIVE SI FERMANO A 19, E OLTRE SI STAMPANO ZERI.
 * Un `double` porta al massimo 17 cifre decimali di informazione, il
 * `long double` a 80 bit ne porta 19: quello che c'e' oltre non e' un
 * dato dell'utente, e' l'espansione esatta del valore BINARIO. glibc la
 * stampa (con un'aritmetica a precisione arbitraria), noi no:
 *
 *     printf("%.30f", 0.1)
 *       glibc  0.100000000000000005551115123126
 *       EX-OS  0.100000000000000000000000000000
 *
 * Le prime 17 cifre coincidono, che e' tutto cio' che 0.1 contiene. La
 * differenza si vede solo chiedendo piu' cifre di quante il numero ne
 * abbia, e allora e' bene che si veda.
 *
 * I NUMERI A 64 BIT sono formattati con una divisione fatta a mano
 * (div64_10 e simili). Non e' pedanteria: dividere un uint64_t sull'i386
 * fa chiamare al compilatore __udivdi3 di libgcc, e i programmi di EX-OS
 * si linkano con -nostdlib e SENZA libgcc — l'errore sarebbe al link, non
 * a runtime.
 * ============================================================================= */


/* =============================================================================
 * Conversione di un `long double` in cifre decimali
 *
 * ⚠️ SI SCALA CON UNA TABELLA DI POTENZE, NON DIVIDENDO PER 10 IN CICLO.
 * Portare 1e300 nell'intervallo [0.1, 1) dividendo per dieci vuol dire
 * TRECENTO divisioni, e ognuna arrotonda: l'errore si accumula e le
 * ultime cifre escono sbagliate. Con la tabella (1e1, 1e2, 1e4, ... 1e256)
 * bastano nove moltiplicazioni, cioe' nove arrotondamenti invece di
 * trecento.
 *
 * ⚠️ SI LAVORA IN `long double`. Su i386 e' l'x87 a 80 bit, con 64 bit di
 * mantissa: 19 cifre decimali. Fare gli stessi conti in `double` ne
 * lascerebbe 15-16, cioe' meno di quante ne ha il numero da stampare, e
 * l'ultima cifra di un %.17g uscirebbe sbagliata.
 * ============================================================================= */
#define CIFRE_MAX  24       /* capienza del buffer, riporto compreso */

/* ⚠️ DICIOTTO, ED E' UN NUMERO MISURATO NON STIMATO. Oltre questa soglia
 * le cifre che escono dalla scalatura non sono piu' quelle del numero: la
 * moltiplicazione per le potenze di dieci arrotonda, e l'errore affiora
 * proprio in coda. Provato confrontando ld_cifre() con glibc su una
 * dozzina di valori: fino a 18 nessuna discordanza, a 19 la prima, a 22
 * cinque.
 *
 * Il caso che l'ha fatto vedere: 1234567890123456.0 con %.6f dava
 * "1234567890123456.000012" — dodici millesimi comparsi dal nulla su un
 * valore ESATTO, perche' si chiedevano 22 cifre a un numero che ne porta
 * 16. Oltre la soglia si stampano zeri, che e' l'unica cosa vera che si
 * puo' dire. */
#define CIFRE_UTILI 18

static const long double g_pot10[] = {
    1e1L, 1e2L, 1e4L, 1e8L, 1e16L, 1e32L, 1e64L, 1e128L, 1e256L
};

/* Scompone v > 0 in cifre[] ed esponente: v = 0.d1d2d3... * 10^(*exp10).
 * Produce esattamente `quante` cifre (<= CIFRE_MAX), gia' arrotondate. */
static void ld_cifre(long double v, char *cifre, int quante, int *exp10)
{
    int e = 0, i;

    if (quante > CIFRE_MAX) quante = CIFRE_MAX;

    /* Scala verso il basso: v >= 1 */
    if (v >= 1.0L) {
        for (i = 8; i >= 0; i--) {
            while (v >= g_pot10[i]) { v /= g_pot10[i]; e += (1 << i); }
        }
    }
    /* Scala verso l'alto: v < 0.1 */
    else {
        for (i = 8; i >= 0; i--) {
            while (v * g_pot10[i] < 1.0L && v != 0.0L) {
                v *= g_pot10[i];
                e -= (1 << i);
            }
        }
    }

    /* ⚠️ LA SCALATURA PUO' SBAGLIARE DI UNO per l'arrotondamento
     * dell'ultima moltiplicazione: si corregge dopo, guardando il valore
     * vero invece di fidarsi del conto. */
    while (v >= 1.0L) { v /= 10.0L; e++; }
    while (v < 0.1L && v != 0.0L) { v *= 10.0L; e--; }

    /* Estrazione: una cifra in piu' di quelle chieste, per arrotondare. */
    for (i = 0; i < quante + 1 && i < CIFRE_MAX; i++) {
        int d;

        v *= 10.0L;
        d = (int)v;
        if (d < 0) d = 0;
        if (d > 9) d = 9;
        cifre[i] = (char)('0' + d);
        v -= (long double)d;
    }

    /* =====================================================================
     * ⚠️ ARROTONDAMENTO AL PARI, non "mezzo verso l'alto".
     *
     * Con la regola ingenua (>= 5 sale) 2.5 con %.0f darebbe 3, e lo
     * standard dice 2: il modo di arrotondamento predefinito e' "al piu'
     * vicino, e a parita' al PARI". Non e' pedanteria — sommare una
     * colonna di valori arrotondati sempre verso l'alto accumula un
     * errore che cresce col numero di righe, mentre al pari gli scarti
     * si compensano.
     *
     * La parita' si guarda solo quando il resto e' ESATTAMENTE mezzo:
     * cifra di guardia '5' e niente dopo. Se dopo c'e' qualcosa — anche
     * un solo bit — il valore e' sopra la meta' e sale comunque.
     * `v` qui e' proprio quel resto.
     * ===================================================================== */
    if (i > quante &&
        (cifre[quante] > '5' ||
         (cifre[quante] == '5' &&
          (v != 0.0L || (quante > 0 && ((cifre[quante-1] - '0') & 1)))))) {
        int k = quante - 1;

        while (k >= 0) {
            if (cifre[k] != '9') { cifre[k]++; break; }
            cifre[k] = '0';
            k--;
        }
        /* Riporto uscito in testa: 999 -> 1000, e l'esponente sale. */
        if (k < 0) {
            for (k = quante - 1; k > 0; k--) cifre[k] = cifre[k-1];
            cifre[0] = '1';
            e++;
        }
    }

    cifre[quante] = '\0';
    *exp10 = e;
}

/* Vero per NaN: e' l'unico valore diverso da se stesso. */
static int ld_e_nan(long double v) { return v != v; }

static int ld_e_inf(long double v)
{
    return v > 1.7976931348623157e308L || v < -1.7976931348623157e308L;
}

/* Compone la rappresentazione di v in `out` secondo `conv` ('f','e','g'),
 * precisione `prec`, e i flag. Ritorna la lunghezza. `out` deve avere
 * almeno 512 byte. */
static int ld_formatta(char *out, long double v, char conv, int prec,
                       int alt, int maiuscolo)
{
    char cifre[CIFRE_MAX + 2];
    int  n = 0;
    int  negativo = 0;

    if (v < 0.0L) { negativo = 1; v = -v; }

    if (ld_e_nan(v)) {
        const char *s = maiuscolo ? "NAN" : "nan";
        while (*s) out[n++] = *s++;
        out[n] = '\0';
        return n;   /* il segno di un NaN non significa niente: non si stampa */
    }
    if (ld_e_inf(v)) {
        const char *s = maiuscolo ? "INF" : "inf";
        if (negativo) out[n++] = '-';
        while (*s) out[n++] = *s++;
        out[n] = '\0';
        return n;
    }

    if (prec < 0) prec = 6;

    /* --- %g: decide fra %e e %f, poi toglie gli zeri finali --- */
    if (conv == 'g') {
        int p = (prec == 0) ? 1 : prec;
        int e;

        if (v == 0.0L) e = 0;
        else {
            int volute = (p > CIFRE_UTILI) ? CIFRE_UTILI : p;
            ld_cifre(v, cifre, volute, &e);
            e = e - 1;
        }

        /* La regola dello standard: notazione scientifica se l'esponente
         * e' sotto -4 o non minore della precisione. */
        if (e < -4 || e >= p) {
            n = ld_formatta(out, negativo ? -v : v, 'e', p - 1, alt, maiuscolo);
        } else {
            n = ld_formatta(out, negativo ? -v : v, 'f', p - 1 - e, alt, maiuscolo);
        }

        if (!alt) {
            /* Zeri finali e punto orfano: %g li toglie, a meno di '#'. */
            int punto = -1, fine = n, k;

            for (k = 0; k < n; k++) if (out[k] == '.') { punto = k; break; }
            if (punto >= 0) {
                for (k = 0; k < n; k++)
                    if (out[k] == 'e' || out[k] == 'E') { fine = k; break; }
                k = fine - 1;
                while (k > punto && out[k] == '0') k--;
                if (k == punto) k--;
                if (fine < n) {
                    int j, d = fine - (k + 1);
                    for (j = fine; j < n; j++) out[j - d] = out[j];
                    n -= d;
                } else {
                    n = k + 1;
                }
                out[n] = '\0';
            }
        }
        return n;
    }

    if (negativo) out[n++] = '-';

    /* --- %e --- */
    if (conv == 'e') {
        int e, k, ae, sig = CIFRE_MAX;

        if (v == 0.0L) {
            for (k = 0; k < CIFRE_MAX; k++) cifre[k] = '0';
            cifre[CIFRE_MAX] = '\0';
            e = 1;
        } else {
            int volute = prec + 1;

            if (volute > CIFRE_UTILI) volute = CIFRE_UTILI;
            ld_cifre(v, cifre, volute, &e);
            sig = volute;
        }
        ae = e - 1;

        out[n++] = cifre[0];
        if (prec > 0 || alt) out[n++] = '.';
        for (k = 0; k < prec; k++) out[n++] = (k + 1 < sig) ? cifre[k+1] : '0';

        out[n++] = maiuscolo ? 'E' : 'e';
        out[n++] = (ae < 0) ? '-' : '+';
        if (ae < 0) ae = -ae;
        /* ⚠️ ALMENO DUE CIFRE DI ESPONENTE, come dice lo standard: "1e+5"
         * e' sbagliato, va scritto "1e+05". */
        if (ae >= 100) {
            out[n++] = (char)('0' + ae / 100);
            out[n++] = (char)('0' + (ae / 10) % 10);
            out[n++] = (char)('0' + ae % 10);
        } else {
            out[n++] = (char)('0' + ae / 10);
            out[n++] = (char)('0' + ae % 10);
        }
        out[n] = '\0';
        return n;
    }

    /* --- %f --- */
    {
        int e, k, sig;

        if (v == 0.0L) {
            out[n++] = '0';
            if (prec > 0 || alt) {
                out[n++] = '.';
                for (k = 0; k < prec; k++) out[n++] = '0';
            }
            out[n] = '\0';
            return n;
        }

        /* Cifre significative utili: quelle prima della virgola piu' la
         * precisione, limitate a quante il numero ne contiene davvero. */
        ld_cifre(v, cifre, CIFRE_UTILI, &e);
        sig = CIFRE_UTILI;

        /* ⚠️ RIESTRAZIONE CON L'ARROTONDAMENTO AL POSTO GIUSTO. La prima
         * chiamata serviva solo a sapere l'esponente: arrotondare a 22
         * cifre e poi troncare a `prec` darebbe 2.4999... -> 2.4 invece
         * di 2.5. Ora si chiede esattamente il numero di cifre che
         * finiranno stampate. */
        {
            int volute = e + prec;

            if (volute < 0) volute = 0;
            if (volute > CIFRE_UTILI) volute = CIFRE_UTILI;
            if (volute == 0) {
                /* Il numero e' interamente sotto la precisione chiesta:
                 * resta zero, ma va deciso se arrotonda a 1 nell'ultima
                 * posizione. */
                long double resto = v;
                int         sale;

                ld_cifre(v, cifre, 1, &e);
                sig = 1;

                /* Stessa regola del pari: la cifra implicita davanti e'
                 * uno zero, che e' pari — quindi 0.5 con %.0f resta 0.
                 * Serve pero' sapere se dopo il '5' c'era altro, e
                 * ld_cifre non lo dice: si riguarda il valore originale
                 * contro mezzo nell'ultima posizione utile. */
                sale = 0;
                if (e + prec == 0) {
                    long double meta = 0.5L;
                    int k;

                    for (k = 0; k < prec; k++) meta /= 10.0L;
                    if (resto > meta) sale = 1;
                }

                if (sale) {
                    cifre[0] = '1';
                    e = -prec + 1;
                } else if (e + prec <= 0) {
                    cifre[0] = '0';
                    e = 1;
                }
            } else {
                ld_cifre(v, cifre, volute, &e);
                sig = volute;
            }
        }

        if (e <= 0) {
            out[n++] = '0';
        } else {
            for (k = 0; k < e; k++)
                out[n++] = (k < sig) ? cifre[k] : '0';
        }

        if (prec > 0 || alt) {
            out[n++] = '.';
            for (k = 0; k < prec; k++) {
                int idx = e + k;

                if (idx < 0 || idx >= sig) out[n++] = '0';
                else                       out[n++] = cifre[idx];
            }
        }
        out[n] = '\0';
        return n;
    }
}

typedef struct {
    FILE   *f;      /* destinazione a flusso... */
    char   *buf;    /* ...oppure a buffer del chiamante */
    size_t  max;    /* capacita' del buffer, terminatore compreso */
    size_t  n;      /* caratteri PRODOTTI, anche oltre la capacita' */
} Uscita;

static void u_car(Uscita *u, char c)
{
    if (u->f != NULL) {
        mette(u->f, (unsigned char)c);
    } else if (u->buf != NULL && u->max > 0 && u->n + 1 < u->max) {
        u->buf[u->n] = c;
    }
    /* Si conta comunque: snprintf deve dire quanto SAREBBE servito, ed e'
     * l'unico modo che ha il chiamante di accorgersi del troncamento. */
    u->n++;
}

static void u_ripeti(Uscita *u, char c, int n)
{
    while (n-- > 0) u_car(u, c);
}

/* Divisione di un intero a 64 bit per una base piccola, a mano.
 * Algoritmo classico a spostamenti: un bit per giro, dal piu' alto. */
static uint64_t div64(uint64_t n, uint32_t d, uint32_t *resto)
{
    uint64_t q = 0;
    uint32_t r = 0;
    int      i;

    for (i = 63; i >= 0; i--) {
        r = (r << 1) | (uint32_t)((n >> i) & 1u);
        if (r >= d) { r -= d; q |= ((uint64_t)1u << i); }
    }
    if (resto) *resto = r;
    return q;
}

/* Converte a ritroso da `fine` (che ospita il terminatore) e ritorna il
 * puntatore alla prima cifra. */
static char *u_conv(uint64_t v, unsigned base, int maiuscole, char *fine)
{
    static const char giu[] = "0123456789abcdef";
    static const char su[]  = "0123456789ABCDEF";
    const char *cifre = maiuscole ? su : giu;
    char *p = fine;

    *p = '\0';
    if (v == 0) { *--p = '0'; return p; }

    while (v > 0) {
        uint32_t r;
        v = div64(v, base, &r);
        *--p = cifre[r];
    }
    return p;
}

static int formatta(Uscita *u, const char *fmt, __builtin_va_list args)
{
    while (*fmt) {
        int  sinistra = 0, zeri = 0, segno = 0, spazio = 0, alt = 0;
        int  ampiezza = 0, precisione = -1;
        int  lungo = 0;             /* 1 = long, 2 = long long, 3 = long double */
        char spec;

        if (*fmt != '%') { u_car(u, *fmt++); continue; }
        fmt++;

        for (;;) {
            if      (*fmt == '-') { sinistra = 1; fmt++; }
            else if (*fmt == '0') { zeri     = 1; fmt++; }
            else if (*fmt == '+') { segno    = 1; fmt++; }
            else if (*fmt == ' ') { spazio   = 1; fmt++; }
            else if (*fmt == '#') { alt      = 1; fmt++; }
            else break;
        }

        if (*fmt == '*') {
            ampiezza = __builtin_va_arg(args, int);
            if (ampiezza < 0) { sinistra = 1; ampiezza = -ampiezza; }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') ampiezza = ampiezza * 10 + (*fmt++ - '0');
        }

        if (*fmt == '.') {
            fmt++;
            precisione = 0;
            if (*fmt == '*') {
                precisione = __builtin_va_arg(args, int);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9')
                    precisione = precisione * 10 + (*fmt++ - '0');
            }
            if (precisione < 0) precisione = -1;
        }

        for (;;) {
            if      (*fmt == 'l') { lungo++;  fmt++; }
            else if (*fmt == 'h') { fmt++; }        /* h/hh: promossi a int */
            else if (*fmt == 'z') { lungo = 1; fmt++; }
            /* 'L' vale solo per la virgola mobile: dice `long double`.
             * Si usa lo stesso contatore perche' i due insiemi di
             * conversioni non si sovrappongono — %Ld non esiste. */
            else if (*fmt == 'L') { lungo = 3; fmt++; }
            else break;
        }

        spec = *fmt;
        if (spec == '\0') { u_car(u, '%'); break; }
        fmt++;

        switch (spec) {
            case 'd': case 'i': {
                char      tmp[24];
                char     *p;
                uint64_t  uv;
                int       neg = 0;
                int       len, pad, prefisso = 0;

                if (lungo >= 2) {
                    long long v = __builtin_va_arg(args, long long);
                    neg = (v < 0);
                    uv  = neg ? (uint64_t)(-v) : (uint64_t)v;
                } else {
                    long v = (lungo == 1) ? __builtin_va_arg(args, long)
                                          : (long)__builtin_va_arg(args, int);
                    neg = (v < 0);
                    uv  = neg ? (uint64_t)(-(long long)v) : (uint64_t)v;
                }

                p   = u_conv(uv, 10, 0, tmp + sizeof(tmp) - 1);
                len = (int)strlen(p);

                if (neg || segno || spazio) prefisso = 1;
                if (precisione > len) len = precisione;

                pad = ampiezza - len - prefisso;

                /* Con la precisione gli zeri di riempimento non contano:
                 * "%05.3d" da' "  007", non "00007". E' la regola del C,
                 * ed e' il genere di dettaglio che si scopre solo quando
                 * una colonna esce disallineata. */
                if (!sinistra && !(zeri && precisione < 0)) u_ripeti(u, ' ', pad);
                if (neg)         u_car(u, '-');
                else if (segno)  u_car(u, '+');
                else if (spazio) u_car(u, ' ');
                if (!sinistra && zeri && precisione < 0) u_ripeti(u, '0', pad);

                u_ripeti(u, '0', precisione - (int)strlen(p));
                while (*p) u_car(u, *p++);

                if (sinistra) u_ripeti(u, ' ', pad);
                break;
            }
            case 'u': case 'x': case 'X': case 'o': {
                char      tmp[24];
                char     *p;
                uint64_t  uv;
                unsigned  base = (spec == 'u') ? 10u : (spec == 'o' ? 8u : 16u);
                int       len, pad, prefisso = 0;

                if (lungo >= 2)      uv = __builtin_va_arg(args, unsigned long long);
                else if (lungo == 1) uv = __builtin_va_arg(args, unsigned long);
                else                 uv = __builtin_va_arg(args, unsigned int);

                p   = u_conv(uv, base, spec == 'X', tmp + sizeof(tmp) - 1);
                len = (int)strlen(p);

                if (alt && uv != 0 && base == 16u) prefisso = 2;
                if (alt && base == 8u)             prefisso = 1;
                if (precisione > len) len = precisione;

                pad = ampiezza - len - prefisso;

                if (!sinistra && !(zeri && precisione < 0)) u_ripeti(u, ' ', pad);
                if (prefisso == 2) { u_car(u, '0'); u_car(u, spec == 'X' ? 'X' : 'x'); }
                if (prefisso == 1) u_car(u, '0');
                if (!sinistra && zeri && precisione < 0) u_ripeti(u, '0', pad);

                u_ripeti(u, '0', precisione - (int)strlen(p));
                while (*p) u_car(u, *p++);

                if (sinistra) u_ripeti(u, ' ', pad);
                break;
            }
            case 'p': {
                char  tmp[24];
                char *p = u_conv((uint64_t)(uintptr_t)__builtin_va_arg(args, void *),
                                 16, 0, tmp + sizeof(tmp) - 1);
                int   len = (int)strlen(p);

                u_car(u, '0'); u_car(u, 'x');
                u_ripeti(u, '0', 8 - len);
                while (*p) u_car(u, *p++);
                break;
            }
            case 's': {
                const char *s = __builtin_va_arg(args, const char *);
                int         len = 0, pad;

                if (s == NULL) s = "(null)";
                while (s[len] && (precisione < 0 || len < precisione)) len++;

                pad = ampiezza - len;
                if (!sinistra) u_ripeti(u, ' ', pad);
                { int i; for (i = 0; i < len; i++) u_car(u, s[i]); }
                if (sinistra)  u_ripeti(u, ' ', pad);
                break;
            }
            case 'c': {
                char c   = (char)__builtin_va_arg(args, int);
                int  pad = ampiezza - 1;

                if (!sinistra) u_ripeti(u, ' ', pad);
                u_car(u, c);
                if (sinistra)  u_ripeti(u, ' ', pad);
                break;
            }
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
                char        num[512];
                long double v;
                char        conv;
                int         maiuscolo = (spec >= 'A' && spec <= 'Z');
                int         len, pad, i, salta_segno = 0;

                /* ⚠️ UN `float` PASSATO A UNA FUNZIONE VARIADICA ARRIVA
                 * COME `double`: e' la promozione automatica del C, e
                 * leggerlo come float darebbe quattro byte al posto di
                 * otto disallineando tutto il resto. Solo 'L' cambia
                 * davvero il tipo sullo stack. */
                if (lungo == 3) v = __builtin_va_arg(args, long double);
                else            v = (long double)__builtin_va_arg(args, double);

                conv = (char)(maiuscolo ? spec - 'A' + 'a' : spec);

                len = ld_formatta(num, v, conv, precisione, alt, maiuscolo);

                /* Segno esplicito: ld_formatta mette solo il '-'. */
                if (num[0] != '-' && (segno || spazio)) {
                    for (i = len; i >= 0; i--) num[i + 1] = num[i];
                    num[0] = segno ? '+' : ' ';
                    len++;
                }

                /* ⚠️ GLI ZERI DI RIEMPIMENTO VANNO DOPO IL SEGNO, non
                 * prima: "%+08.2f" di 3.5 e' "+0003.50", non "0000+3.5".
                 * E non si usano su inf e nan, dove riempirebbero di zeri
                 * una parola. */
                if (num[0] == '-' || num[0] == '+' || num[0] == ' ') salta_segno = 1;

                pad = ampiezza - len;
                if (pad < 0) pad = 0;

                if (sinistra) {
                    for (i = 0; i < len; i++) u_car(u, num[i]);
                    u_ripeti(u, ' ', pad);
                } else if (zeri && !ld_e_nan(v) && !ld_e_inf(v)) {
                    if (salta_segno) u_car(u, num[0]);
                    u_ripeti(u, '0', pad);
                    for (i = salta_segno; i < len; i++) u_car(u, num[i]);
                } else {
                    u_ripeti(u, ' ', pad);
                    for (i = 0; i < len; i++) u_car(u, num[i]);
                }
                break;
            }
            case '%':
                u_car(u, '%');
                break;
            default:
                /* Specificatore sconosciuto: si stampa alla lettera e NON
                 * si consuma niente — non sapendo di che tipo sia
                 * l'argomento, prenderlo sarebbe peggio che lasciarlo. */
                u_car(u, '%');
                u_car(u, spec);
                break;
        }
    }

    return (int)u->n;
}

int vfprintf(FILE *f, const char *fmt, __builtin_va_list args)
{
    Uscita u;
    int    n;

    if (f == NULL || fmt == NULL) return -1;

    u.f = f; u.buf = NULL; u.max = 0; u.n = 0;
    n = formatta(&u, fmt, args);
    auto_scarica(f);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    __builtin_va_list args;
    int n;

    __builtin_va_start(args, fmt);
    n = vfprintf(f, fmt, args);
    __builtin_va_end(args);
    return n;
}

int printf(const char *fmt, ...)
{
    __builtin_va_list args;
    int n;

    __builtin_va_start(args, fmt);
    n = vfprintf(stdout, fmt, args);
    __builtin_va_end(args);
    return n;
}

int vprintf(const char *fmt, __builtin_va_list args)
{
    return vfprintf(stdout, fmt, args);
}

int vsnprintf(char *buf, size_t dim, const char *fmt, __builtin_va_list args)
{
    Uscita u;
    int    n;

    u.f = NULL; u.buf = buf; u.max = dim; u.n = 0;
    n = formatta(&u, fmt, args);

    /* Il terminatore c'e' SEMPRE quando c'e' spazio per almeno un byte, e
     * va messo alla fine di cio' che ci sta, non alla fine di cio' che
     * sarebbe servito. */
    if (buf != NULL && dim > 0) {
        buf[(u.n < dim) ? u.n : dim - 1] = '\0';
    }
    return n;
}

int snprintf(char *buf, size_t dim, const char *fmt, ...)
{
    __builtin_va_list args;
    int n;

    __builtin_va_start(args, fmt);
    n = vsnprintf(buf, dim, fmt, args);
    __builtin_va_end(args);
    return n;
}

int vsprintf(char *buf, const char *fmt, __builtin_va_list args)
{
    /* Senza limite: e' l'interfaccia insicura del C, offerta perche' il
     * codice esistente la usa. Chi puo' scegliere usi snprintf. */
    return vsnprintf(buf, (size_t)-1, fmt, args);
}

int sprintf(char *buf, const char *fmt, ...)
{
    __builtin_va_list args;
    int n;

    __builtin_va_start(args, fmt);
    n = vsnprintf(buf, (size_t)-1, fmt, args);
    __builtin_va_end(args);
    return n;
}

/* =============================================================================
 * _assert_fallita — il corpo di assert(), che e' una macro in <assert.h>
 *
 * Scrive su stderr e non ritorna. Il testo della condizione arriva gia'
 * stampato dal preprocessore (#cond nella macro): senza quello il
 * messaggio direbbe solo che qualcosa e' andato storto, che e' la parte
 * che si sapeva gia'.
 * ============================================================================= */
void _assert_fallita(const char *cond, const char *file, int riga)
{
    fprintf(stderr, "assert fallita: %s (%s:%d)\n",
            cond ? cond : "?", file ? file : "?", riga);
    abort();
}

/* =============================================================================
 * Lettura formattata — sscanf
 *
 * E' l'inverso di printf, e come printf ha un solo motore. Ne esiste una
 * sola versione, quella che legge da una stringa: fscanf() e scanf()
 * NON ci sono perche' nessuno le chiede, e sarebbero due righe di
 * involucro sopra questo motore piu' un buffer di ritorno da gestire —
 * lavoro vero, non copia-incolla, e quindi si fa il giorno che serve.
 *
 * IL VALORE DI RITORNO ha tre casi e vanno distinti tutti e tre: il numero
 * di conversioni RIUSCITE (non di argomenti passati), zero se la prima
 * conversione ha trovato qualcosa che non le andava bene, e -1 se
 * l'ingresso era gia' finito prima di provarci. Confondere gli ultimi due
 * e' il modo classico di scrivere un ciclo di lettura che non termina.
 * ============================================================================= */

/* Legge un intero rispettando la larghezza massima del campo, cosa che
 * strtoll() non sa fare: "%2d" su "1234" deve fermarsi a 12. */
static int scan_intero(const char **pp, int base, int larghezza,
                       unsigned long long *out, int *negativo)
{
    const char *p = *pp;
    unsigned long long v = 0;
    int cifre = 0;

    *negativo = 0;

    if (larghezza > 0 && (*p == '+' || *p == '-')) {
        *negativo = (*p == '-');
        p++; larghezza--;
    }

    /* Prefisso 0x: vale per %x e per %i, che la base la deduce. */
    if ((base == 16 || base == 0) && larghezza >= 2 && p[0] == '0' &&
        (p[1] == 'x' || p[1] == 'X') && cifra_valore((unsigned char)p[2]) >= 0) {
        p += 2; larghezza -= 2; base = 16;
    } else if (base == 0) {
        base = (p[0] == '0' && cifra_valore((unsigned char)p[1]) >= 0) ? 8 : 10;
    }

    while (larghezza > 0) {
        int c = cifra_valore((unsigned char)*p);
        if (c < 0 || c >= base) break;
        v = v * (unsigned long long)base + (unsigned long long)c;
        p++; larghezza--; cifre++;
    }

    if (cifre == 0) return 0;
    *pp = p;
    *out = v;
    return 1;
}

/* Il corpo vero. `fine` riceve dove la scansione si e' fermata, ed e' cio'
 * che serve a vfscanf per riportare indietro il flusso di quanto NON ha
 * consumato: senza, leggere da un FILE vorrebbe dire riscrivere tutto lo
 * scanner con una sorgente diversa. */
static int scan_stringa(const char *s, const char *fmt,
                        __builtin_va_list args, const char **fine_out)
{
    const char *p = s;
    int         assegnate = 0;
    int         visto_qualcosa = 0;

    while (*fmt) {
        /* Uno spazio nel formato significa "salta QUANTO spazio vuoi,
         * anche nessuno": non e' un carattere da far combaciare. */
        if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*p)) p++;
            fmt++;
            continue;
        }

        if (*fmt != '%') {
            if (*p != *fmt) break;      /* non combacia: si smette */
            p++; fmt++;
            continue;
        }

        fmt++;                          /* oltre il '%' */

        if (*fmt == '%') {              /* "%%" e' un '%' letterale */
            if (*p != '%') break;
            p++; fmt++;
            continue;
        }

        int sopprimi = 0;
        if (*fmt == '*') { sopprimi = 1; fmt++; }

        int larghezza = 0;
        while (isdigit((unsigned char)*fmt)) larghezza = larghezza * 10 + (*fmt++ - '0');
        if (larghezza == 0) larghezza = 0x7FFFFFFF;   /* nessun limite */

        /* Modificatori di lunghezza. 'hh' e 'll' sono due lettere uguali di
         * fila: si contano invece di elencare i casi. */
        int lungo = 0, corto = 0;
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'L' || *fmt == 'z' || *fmt == 'j') {
            if (*fmt == 'l' || *fmt == 'L' || *fmt == 'j') lungo++;
            if (*fmt == 'h') corto++;
            if (*fmt == 'z') lungo++;
            fmt++;
        }

        int spec = *fmt++;
        if (spec == '\0') break;

        /* %c e %n non saltano lo spazio davanti: %c deve poter leggere uno
         * spazio, e %n non legge niente. Tutti gli altri lo saltano. */
        if (spec != 'c' && spec != 'n' && spec != '[') {
            while (isspace((unsigned char)*p)) p++;
        }

        if (*p == '\0' && spec != 'n') {
            /* Ingresso finito. Se non si era ancora convertito niente, il
             * chiamante deve poter distinguere questo caso da "c'era
             * qualcosa ma non andava bene": e' il -1. */
            if (assegnate == 0 && !visto_qualcosa) return -1;
            break;
        }
        visto_qualcosa = 1;

        switch (spec) {

        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': {
            int base = (spec == 'd' || spec == 'u') ? 10 :
                       (spec == 'o') ? 8 :
                       (spec == 'i') ? 0 : 16;
            unsigned long long v;
            int neg;

            if (!scan_intero(&p, base, larghezza, &v, &neg)) goto fine;
            if (neg) v = (unsigned long long)(-(long long)v);

            if (!sopprimi) {
                if (lungo >= 2)      *__builtin_va_arg(args, long long *)  = (long long)v;
                else if (lungo == 1) *__builtin_va_arg(args, long *)       = (long)v;
                else if (corto >= 2) *__builtin_va_arg(args, signed char *)= (signed char)v;
                else if (corto == 1) *__builtin_va_arg(args, short *)      = (short)v;
                else                 *__builtin_va_arg(args, int *)        = (int)v;
                assegnate++;
            }
            break;
        }

        case 'p': {
            unsigned long long v;
            int neg;
            if (!scan_intero(&p, 16, larghezza, &v, &neg)) goto fine;
            if (!sopprimi) { *__builtin_va_arg(args, void **) = (void *)(uintptr_t)v; assegnate++; }
            break;
        }

        case 'f': case 'e': case 'E': case 'g': case 'G': case 'a': {
            /* Il numero si ritaglia in un buffer e si passa a strtod:
             * riscrivere qui la scansione dei decimali vorrebbe dire due
             * posti in cui sbagliare l'esponente. */
            char  num[64];
            int   n = 0;
            const char *q = p;

            if (larghezza > 0 && (*q == '+' || *q == '-') && n < 63) num[n++] = *q++;
            while (isdigit((unsigned char)*q) && n < 63 && n < larghezza) num[n++] = *q++;
            if (*q == '.' && n < 63 && n < larghezza) {
                num[n++] = *q++;
                while (isdigit((unsigned char)*q) && n < 63 && n < larghezza) num[n++] = *q++;
            }
            if ((*q == 'e' || *q == 'E') && n < 63 && n < larghezza) {
                const char *r = q + 1;
                int m = n;
                char tmp[8]; int tn = 0;
                tmp[tn++] = *q;
                if ((*r == '+' || *r == '-')) tmp[tn++] = *r++;
                if (isdigit((unsigned char)*r)) {
                    for (int k = 0; k < tn && n < 63; k++) num[n++] = tmp[k];
                    while (isdigit((unsigned char)*r) && n < 63) num[n++] = *r++;
                    q = r;
                } else {
                    n = m;      /* 'e' senza cifre: non fa parte del numero */
                }
            }
            num[n] = '\0';
            if (n == 0) goto fine;

            double v = strtod(num, NULL);
            p = q;
            if (!sopprimi) {
                if (lungo >= 2)      *__builtin_va_arg(args, long double *) = (long double)v;
                else if (lungo == 1) *__builtin_va_arg(args, double *)      = v;
                else                 *__builtin_va_arg(args, float *)       = (float)v;
                assegnate++;
            }
            break;
        }

        case 's': {
            char *dst = sopprimi ? NULL : __builtin_va_arg(args, char *);
            int   n = 0;

            while (*p && !isspace((unsigned char)*p) && n < larghezza) {
                if (dst) dst[n] = *p;
                p++; n++;
            }
            if (n == 0) goto fine;
            if (dst) { dst[n] = '\0'; assegnate++; }
            break;
        }

        case 'c': {
            int   quanti = (larghezza == 0x7FFFFFFF) ? 1 : larghezza;
            char *dst = sopprimi ? NULL : __builtin_va_arg(args, char *);
            int   n = 0;

            /* %c NON chiude con '\0': legge esattamente i caratteri
             * chiesti, spazi compresi. Chi vuole una stringa usa %s. */
            while (*p && n < quanti) {
                if (dst) dst[n] = *p;
                p++; n++;
            }
            if (n < quanti) goto fine;
            if (dst) assegnate++;
            break;
        }

        case 'n':
            /* Non e' una conversione: non conta nel valore di ritorno. */
            if (!sopprimi) *__builtin_va_arg(args, int *) = (int)(p - s);
            break;

        default:
            goto fine;      /* specificatore sconosciuto: si smette */
        }
    }

fine:
    if (fine_out != NULL) *fine_out = p;
    return assegnate;
}

int vsscanf(const char *s, const char *fmt, __builtin_va_list args)
{
    return scan_stringa(s, fmt, args, NULL);
}

/* =============================================================================
 * fscanf — lo stesso scanner, con un flusso davanti
 *
 * COME FUNZIONA, e perche' non e' lo scanner riscritto. Si legge una
 * FINESTRA dal punto in cui sta il flusso, ci si passa sopra lo scanner
 * gia' collaudato, e poi si riporta il flusso esattamente dove la
 * scansione si e' fermata. Una seconda copia dello scanner, con una
 * sorgente a carattere invece che a stringa, sarebbe stata il doppio del
 * codice e il doppio dei difetti.
 *
 * ⚠️ NON SI LEGGE OLTRE LA FINESTRA. Una conversione che avrebbe bisogno
 * di piu' di SCANF_FINESTRA byte — un %s con dentro un valore lunghissimo,
 * un file senza spazi — si ferma li'. Sono 1024 byte: piu' di qualunque
 * riga di un file di testo che abbia senso leggere con fscanf, e va
 * DETTO invece di scoprirlo.
 *
 * ⚠️ SU UN FLUSSO NON POSIZIONABILE — la console — si legge una riga e si
 * scansiona quella, perche' non c'e' modo di rimettere indietro cio' che
 * non si e' consumato. Il resto della riga si perde. Per l'input
 * interattivo e' anche il comportamento che ci si aspetta; per un file
 * sarebbe sbagliato, ed e' per questo che i due casi sono separati.
 * ============================================================================= */
#define SCANF_FINESTRA 1024

int vfscanf(FILE *f, const char *fmt, __builtin_va_list args)
{
    char        buf[SCANF_FINESTRA];
    const char *fine = buf;
    long        partenza;
    size_t      letti;
    int         n;

    if (f == NULL || fmt == NULL) return EOF;

    partenza = ftell(f);

    if (partenza < 0) {
        /* Non posizionabile: una riga, e quella e'. */
        if (fgets(buf, sizeof(buf), f) == NULL) return EOF;
        return scan_stringa(buf, fmt, args, NULL);
    }

    letti = fread(buf, 1, sizeof(buf) - 1, f);
    if (letti == 0) return EOF;
    buf[letti] = '\0';

    n = scan_stringa(buf, fmt, args, &fine);

    /* Il flusso torna dove la scansione si e' fermata: quello che lo
     * scanner non ha guardato deve restare da leggere. */
    fseek(f, partenza + (long)(fine - buf), SEEK_SET);
    return n;
}

int fscanf(FILE *f, const char *fmt, ...)
{
    __builtin_va_list args;
    int n;

    __builtin_va_start(args, fmt);
    n = vfscanf(f, fmt, args);
    __builtin_va_end(args);
    return n;
}

int scanf(const char *fmt, ...)
{
    __builtin_va_list args;
    int n;

    __builtin_va_start(args, fmt);
    n = vfscanf(stdin, fmt, args);
    __builtin_va_end(args);
    return n;
}

/* scanf con la lista di argomenti gia' pronta: e' vfscanf su stdin, e c'e'
 * perche' <cstdio> la dichiara. */
int vscanf(const char *fmt, __builtin_va_list args)
{
    return vfscanf(stdin, fmt, args);
}

int sscanf(const char *s, const char *fmt, ...)
{
    __builtin_va_list args;
    int n;

    __builtin_va_start(args, fmt);
    n = vsscanf(s, fmt, args);
    __builtin_va_end(args);
    return n;
}

/* =============================================================================
 * Stdlib
 * ============================================================================= */

int atoi(const char *s)
{
    int v = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

/* =============================================================================
 * atexit — funzioni da chiamare all'uscita
 *
 * GCC e binutils la usano per ripulire i file temporanei: senza, un
 * compilatore che fallisce lascia dietro di se' i propri intermedi. Il
 * tetto e' 32 perche' lo standard ne pretende almeno 32 e nessuno dei
 * programmi che ci interessano ne registra piu' di due o tre.
 * ============================================================================= */
#define ATEXIT_MAX  32

static void (*g_atexit[ATEXIT_MAX])(void);
static int    g_atexit_n = 0;
static int    g_in_uscita = 0;

int atexit(void (*fn)(void))
{
    if (fn == NULL || g_atexit_n >= ATEXIT_MAX) return -1;
    g_atexit[g_atexit_n++] = fn;
    return 0;
}

/* Definita in fondo, accanto a _libc_start: qui serve solo il nome. */
void _libc_distruttori(void);

void exit(int code)
{
    /* All'indietro, come pretende lo standard: l'ultima registrata e' la
     * prima chiamata. La guardia serve a un handler che chiami exit() —
     * senza, sarebbe una ricorsione infinita invece di un'uscita. */
    if (!g_in_uscita) {
        g_in_uscita = 1;
        while (g_atexit_n > 0) g_atexit[--g_atexit_n]();

        /* ⚠️ I DISTRUTTORI GLOBALI DOPO gli atexit, non prima. Un
         * handler registrato con atexit() puo' usare un oggetto globale;
         * distruggerlo prima gli lascerebbe in mano un oggetto morto.
         * Vedi _libc_distruttori() in fondo al file. */
        _libc_distruttori();
    }

    /* I BUFFER VANNO SVUOTATI QUI, e non e' una cortesia: un programma che
     * scrive un file e poi esce senza fclose() troverebbe il file monco —
     * l'ultimo pezzo sarebbe ancora nel buffer di un processo che non
     * esiste piu'. E' la trappola classica dello stdio bufferizzato, e la
     * si chiude nell'unico punto da cui passano tutti. */
    fflush(NULL);

    _syscall1(SYS_EXIT, (uint32_t)code);
    for (;;) {}
}

/* Esce SENZA passare da niente: nessun handler di atexit, nessun buffer
 * svuotato. Non e' una versione spartana di exit() — e' l'uscita che si
 * usa quando si e' capito che lo stato del processo non e' piu'
 * attendibile, e far girare altro codice suo potrebbe peggiorare le cose.
 *
 * bfd.c la chiama esattamente cosi': quando un'asserzione interna
 * fallisce, esce con _exit() invece che con exit() per non far scrivere
 * niente a un programma che ha appena dichiarato di non capire piu' i
 * propri dati. ⚠️ Il rovescio e' che un file aperto in scrittura resta
 * monco: quello che era nel buffer non arriva sul disco. E' voluto. */
void _exit(int code)
{
    _syscall1(SYS_EXIT, (uint32_t)code);
    for (;;) {}
}

/* Lo stesso, con il nome del C99. */
void _Exit(int code)
{
    _syscall1(SYS_EXIT, (uint32_t)code);
    for (;;) {}
}

/* =============================================================================
 * quick_exit, at_quick_exit — la seconda lista
 *
 * ⚠️ LA LISTA E' SEPARATA DA QUELLA DI atexit, e deve restarlo: sono due
 * insiemi di funzioni con due scopi diversi. Chi si registra con atexit
 * conta di poter scrivere su un file; chi si registra qui sa che i flussi
 * NON verranno svuotati e deve limitarsi a cio' che si puo' fare in
 * fretta. Fonderle vorrebbe dire chiamare gli handler di atexit senza il
 * fflush che si aspettano.
 * ============================================================================= */
#define QUICK_EXIT_MAX 32
static void (*g_quick_exit[QUICK_EXIT_MAX])(void);
static int    g_quick_exit_n = 0;

int at_quick_exit(void (*fn)(void))
{
    if (fn == NULL || g_quick_exit_n >= QUICK_EXIT_MAX) return -1;
    g_quick_exit[g_quick_exit_n++] = fn;
    return 0;
}

void quick_exit(int code)
{
    /* All'indietro come atexit, e con la stessa guardia contro un handler
     * che richiami quick_exit. */
    static int dentro = 0;

    if (!dentro) {
        dentro = 1;
        while (g_quick_exit_n > 0) g_quick_exit[--g_quick_exit_n]();
    }

    /* ⚠️ NIENTE fflush: e' la differenza con exit(), ed e' il punto della
     * funzione. Un file aperto in scrittura resta monco, e chi chiama
     * quick_exit lo sa. */
    _Exit(code);
}

void abort(void)
{
    /* Causa un fault intenzionale */
    exit(134);
}

/* =============================================================================
 * ALLOCATORE — lista di blocchi con riuso e fusione
 *
 * ⚠️ COSA C'ERA PRIMA, e perche' andava rifatto (agosto 2026). L'allocatore
 * precedente chiamava sbrk() a OGNI malloc e aveva una free() vuota, con
 * un TODO in luogo dell'implementazione. Per i programmi di /bin non si
 * notava: allocano poche volte e poi escono, e il processo muore portandosi
 * via tutto. Non e' un difetto teorico:
 *
 *   - la memoria non tornava MAI indietro. Un programma che alloca e libera
 *     in ciclo — cioe' qualunque compilatore, qualunque parser, qualunque
 *     cosa lavori su una struttura ad albero — cresceva fino a esaurire lo
 *     spazio, e falliva senza aver mai tenuto in mano piu' di qualche KB.
 *   - realloc() copiava `size` byte dal vecchio blocco senza conoscerne la
 *     dimensione: ingrandire un blocco leggeva OLTRE la sua fine. Su un
 *     heap in crescita e' memoria non ancora scritta, quindi il difetto
 *     non si vedeva; su un heap con riuso sarebbe stato il dato di
 *     qualcun altro copiato dentro il proprio.
 *   - una syscall per allocazione, con il costo di un cambio di contesto
 *     su ogni malloc di otto byte.
 *
 * COME FUNZIONA ORA. Tutti i blocchi — usati e liberi — stanno in UNA lista
 * doppia in ordine di indirizzo. malloc cerca il primo libero abbastanza
 * grande e lo spezza se l'avanzo vale la pena; free lo marca libero e lo
 * FONDE con il vicino precedente e successivo se anche loro sono liberi.
 *
 * Perche' la lista e' in ordine di indirizzo e non "solo i liberi": la
 * fusione ha bisogno dei vicini FISICI, non del prossimo libero. Con una
 * lista dei soli liberi servirebbero i tag di confine (la dimensione
 * ripetuta in coda a ogni blocco) per risalire al precedente — stessa
 * memoria, piu' modi di sbagliare.
 *
 * E DALLA 0.157 LA MEMORIA TORNA ANCHE AL KERNEL. Fino ad allora free()
 * non chiamava mai sbrk con un incremento negativo: un blocco liberato
 * tornava disponibile per il processo ma non per il sistema, e un
 * programma che alloca a picchi teneva il picco massimo fino alla propria
 * uscita. Ora la coda dello heap si restituisce — solo la coda, perche'
 * sbrk sposta un confine e non sa bucare il mezzo. Vedi
 * heap_restituisci().
 *
 * IL PREZZO, dichiarato: l'intestazione e' di 16 byte per blocco, e la
 * ricerca e' lineare. Su un compilatore che alloca centinaia di migliaia
 * di oggetti piccoli l'intestazione pesa e la ricerca rallenta; il rimedio
 * (liste separate per taglia) si aggiunge sopra questa struttura senza
 * cambiarne il contratto, quando i numeri diranno che serve.
 * ============================================================================= */

#define HEAP_ALLINEA    8u
#define HEAP_MIN_SBRK   (64u * 1024u)   /* si chiede memoria a blocchi grossi */
#define HEAP_MIN_SPEZZA 32u             /* avanzo sotto il quale non si spezza */
#define HEAP_PAGINA     4096u           /* la pagina del bersaglio */
/* La coda libera che NON si restituisce mai, e la soglia sotto la quale
 * non vale la pena di una syscall. Vedi heap_restituisci(). */
#define HEAP_TRATTIENI  (64u * 1024u)
#define HEAP_MIN_RESO   (64u * 1024u)

/* sbrk() e' definita molto piu' in basso, insieme agli altri involucri
 * delle syscall, ma l'allocatore la usa: qui serve la dichiarazione, o il
 * compilatore ne inventa una che ritorna int e il puntatore arriva
 * troncato. */
void *sbrk(int incr);

typedef struct Blocco {
    struct Blocco *prec;    /* vicino precedente in ordine di INDIRIZZO */
    struct Blocco *succ;    /* vicino successivo                        */
    size_t         dim;     /* byte utili, esclusa questa intestazione  */
    size_t         libero;  /* 1 = disponibile */
} Blocco;

#define BLOCCO_HDR  (sizeof(Blocco))
#define BLOCCO_DATI(b)  ((void *)((char *)(b) + BLOCCO_HDR))
#define DATI_BLOCCO(p)  ((Blocco *)((char *)(p) - BLOCCO_HDR))

static Blocco *heap_primo = NULL;
static Blocco *heap_ultimo = NULL;

static size_t heap_allinea(size_t n)
{
    return (n + (HEAP_ALLINEA - 1u)) & ~(HEAP_ALLINEA - 1u);
}

/* Chiede memoria al kernel e la aggiunge in coda come un blocco libero.
 * Ritorna il blocco nuovo, o NULL se il kernel non ha piu' spazio. */
static Blocco *heap_estendi(size_t utile)
{
    size_t  quanto = heap_allinea(utile) + BLOCCO_HDR;
    Blocco *b;
    int32_t base;

    if (quanto < HEAP_MIN_SBRK) quanto = HEAP_MIN_SBRK;

    /* ⚠️ SI CHIEDE A PAGINE INTERE, e prima non si faceva.
     *
     * Il kernel non sposta il confine dei byte chiesti: sposta di PAGINE
     * INTERE (sys_sbrk fa ALIGN_UP). Chiedendo 2 MB + 16 byte, il confine
     * saliva di 2 MB + 4096 e la libc si segnava un blocco di 2 MB + 16:
     * gli ultimi 4080 byte esistevano, erano del processo, ed erano
     * INVISIBILI all'allocatore. Uno spreco fino a una pagina per ogni
     * estensione — che nessuno notava, perche' un blocco che non e' in
     * nessuna lista non fa danni, occupa e basta.
     *
     * Da quando c'e' heap_restituisci() il difetto smette di essere solo
     * uno spreco e diventa un impedimento: il controllo «questo blocco e'
     * davvero in cima al break?» non poteva mai riuscire, perche' fra la
     * fine del blocco e il confine c'era sempre quel residuo. La memoria
     * non tornava indietro mai, e senza dire perche'. */
    quanto = (quanto + (HEAP_PAGINA - 1u)) & ~(size_t)(HEAP_PAGINA - 1u);

    /* sbrk(0) ritorna la cima attuale; sbrk(n) la sposta e ritorna la
     * VECCHIA cima, che e' l'inizio della memoria appena ottenuta. */
    base = _syscall1(SYS_SBRK, (uint32_t)quanto);
    if (base <= 0) return NULL;

    b = (Blocco *)(uintptr_t)base;
    b->dim    = quanto - BLOCCO_HDR;
    b->libero = 1;
    b->succ   = NULL;
    b->prec   = heap_ultimo;

    if (heap_ultimo) heap_ultimo->succ = b;
    else             heap_primo = b;
    heap_ultimo = b;

    /* Se la memoria appena ottenuta e' contigua all'ultimo blocco libero,
     * i due sono un blocco solo. Senza questa fusione, chiedere piu' volte
     * lascerebbe una scia di blocchi che nessuna allocazione grande puo'
     * usare pur essendo adiacenti. */
    if (b->prec && b->prec->libero &&
        (char *)b->prec + BLOCCO_HDR + b->prec->dim == (char *)b) {
        b->prec->dim += BLOCCO_HDR + b->dim;
        b->prec->succ = NULL;
        heap_ultimo = b->prec;
        return b->prec;
    }

    return b;
}

/* Spezza `b` lasciando `utile` byte, se l'avanzo e' abbastanza grande da
 * essere un blocco a sua volta. Un avanzo minuscolo resta attaccato: una
 * lista piena di frammenti da otto byte costa piu' memoria (in
 * intestazioni) di quanta ne recuperi. */
static void heap_spezza(Blocco *b, size_t utile)
{
    Blocco *resto;

    if (b->dim < utile + BLOCCO_HDR + HEAP_MIN_SPEZZA) return;

    resto = (Blocco *)((char *)b + BLOCCO_HDR + utile);
    resto->dim    = b->dim - utile - BLOCCO_HDR;
    resto->libero = 1;
    resto->prec   = b;
    resto->succ   = b->succ;

    if (b->succ) b->succ->prec = resto;
    else         heap_ultimo = resto;

    b->succ = resto;
    b->dim  = utile;
}

void *malloc(size_t size)
{
    Blocco *b;
    size_t  utile;

    /* malloc(0) puo' ritornare NULL o un puntatore unico: si ritorna un
     * blocco vero, cosi' free() su quel puntatore e' legittima e il
     * chiamante non deve distinguere il caso. */
    if (size == 0) size = 1;

    utile = heap_allinea(size);
    if (utile < size) return NULL;      /* overflow dell'arrotondamento */

    for (b = heap_primo; b != NULL; b = b->succ) {
        if (b->libero && b->dim >= utile) {
            heap_spezza(b, utile);
            b->libero = 0;
            return BLOCCO_DATI(b);
        }
    }

    b = heap_estendi(utile);
    if (b == NULL) return NULL;

    heap_spezza(b, utile);
    b->libero = 0;
    return BLOCCO_DATI(b);
}

/* Fonde `b` con il successivo, se sono entrambi liberi E fisicamente
 * adiacenti. L'adiacenza va CONTROLLATA e non data per scontata: due
 * chiamate a sbrk possono restituire aree separate, e fondere un buco
 * consegnerebbe al chiamante memoria che non gli appartiene. */
/* Assorbe in `b` il vicino successivo, se e' libero e adiacente. NON
 * guarda se `b` stesso e' libero.
 *
 * ⚠️ ESISTE PERCHE' realloc DEVE POTER ALLUNGARE UN BLOCCO ALLOCATO, e
 * prima non poteva. Il ramo "il vicino e' libero, mi allungo sul posto
 * senza copiare" chiamava heap_fondi_con_succ(), che comincia rifiutando
 * i blocchi non liberi: quindi non fondeva niente, la heap_spezza()
 * successiva vedeva `dim` invariato e rinunciava anche lei, e realloc
 * restituiva il puntatore dichiarando una dimensione che il blocco non
 * aveva mai avuto.
 *
 * Il guasto non si vedeva li'. Il chiamante scriveva i byte che gli erano
 * stati promessi, sfondando nell'intestazione del blocco successivo, e il
 * danno usciva alla malloc DOPO — che seguiva un puntatore fatto dei dati
 * dell'utente. Su cc1 e' uscito come `succ == 3`; nella prova che ora sta
 * in libctest esce come un fault all'indirizzo 0xa7a6a5a4, che sono
 * esattamente i byte di riempimento del test.
 *
 * La separazione in due funzioni non e' cosmetica: il controllo su
 * `b->libero` SERVE agli altri chiamanti. free() e memalign() chiamano
 * heap_fondi_con_succ(b->prec) senza sapere se il predecessore sia
 * libero, e fondere un blocco allocato col suo vicino consegnerebbe due
 * volte la stessa memoria. Quel controllo resta nel guscio; qui sotto
 * c'e' solo la fusione, per chi ha gia' stabilito che si puo' fare. */
static void heap_assorbi_succ(Blocco *b)
{
    Blocco *s = b->succ;

    if (s == NULL || !s->libero) return;
    if ((char *)b + BLOCCO_HDR + b->dim != (char *)s) return;

    b->dim += BLOCCO_HDR + s->dim;
    b->succ = s->succ;
    if (s->succ) s->succ->prec = b;
    else         heap_ultimo = b;
}

/* Fonde due blocchi LIBERI e adiacenti. E' la versione da usare quando
 * non si sa in che stato sia `b` — cioe' quasi sempre. */
static void heap_fondi_con_succ(Blocco *b)
{
    if (!b->libero) return;
    heap_assorbi_succ(b);
}

/* =============================================================================
 * heap_restituisci — la memoria torna al kernel
 *
 * PERCHE' ESISTE. Fino ad agosto 2026 free() non chiamava MAI sbrk con un
 * incremento negativo: la memoria tornava disponibile per il processo, ma
 * non per il sistema. Un programma che allocasse a picchi — cioe'
 * qualunque compilatore: un albero di sintassi per funzione, buttato e
 * ricostruito — teneva il picco massimo fino alla propria uscita. Con un
 * cc1 e un `as` che girano di seguito sulla stessa macchina, il primo
 * affamava il secondo pur avendo gia' finito.
 *
 * COME. Solo la CODA dello heap si puo' restituire, perche' sbrk sposta un
 * confine e non ha un modo di bucare il mezzo. Quindi: se l'ultimo blocco
 * della lista e' libero e abbastanza grande, se ne restituisce l'eccesso.
 *
 * ⚠️ TRE CONDIZIONI, E OGNUNA EVITA UN GUASTO DIVERSO:
 *
 *   1. SI ARROTONDA A PAGINE INTERE. Il kernel libera pagine, non byte, e
 *      un residuo restituirebbe la pagina che contiene il nuovo confine —
 *      cioe' byte ancora vivi. Dalla 0.157 il kernel arrotonda per difetto
 *      per conto suo, ma chi chiama non deve appoggiarsi a quello.
 *
 *   2. SI CONTROLLA CHE IL BLOCCO SIA DAVVERO IN CIMA AL break, con
 *      sbrk(0). heap_ultimo e' l'ultimo blocco DELLA NOSTRA LISTA, che non
 *      e' la stessa cosa: fra la sua fine e il confine puo' esserci roba
 *      di qualcun altro (una mmap, per esempio) e restituirla sarebbe
 *      buttare via memoria che non ci appartiene.
 *
 *   3. SI TIENE SEMPRE UNA CODA (HEAP_TRATTIENI) e non si scende sotto
 *      HEAP_MIN_RESO. Senza, un ciclo che alloca e libera la stessa
 *      dimensione farebbe due syscall a giro: restituire e richiedere,
 *      all'infinito. E' lo stesso motivo per cui glibc ha M_TRIM_THRESHOLD.
 *
 * ⚠️ IL CONTROLLO DI TAGLIA VIENE PRIMA DI sbrk(0), ed e' voluto: e'
 * aritmetica pura, quindi la free() normale — quella che non restituisce
 * niente — non paga nessuna syscall. Rimettere il conto dopo la sbrk(0)
 * annullerebbe il guadagno che l'allocatore nuovo era andato a prendere.
 * ============================================================================= */
static void heap_restituisci(void)
{
    Blocco *b = heap_ultimo;
    char   *cima;
    size_t  totale, quanto;

    if (b == NULL || !b->libero) return;

    totale = BLOCCO_HDR + b->dim;       /* i byte che il blocco occupa */
    if (totale <= HEAP_TRATTIENI) return;

    quanto  = totale - HEAP_TRATTIENI;
    quanto &= ~(size_t)(HEAP_PAGINA - 1u);
    if (quanto < HEAP_MIN_RESO) return;

    cima = (char *)sbrk(0);
    if (cima == (char *)-1) return;
    if ((char *)b + totale != cima) return;

    if (sbrk(-(int)quanto) == (void *)-1) return;

    /* L'intestazione resta dov'e' — sta SOTTO la parte restituita — e il
     * blocco continua a esistere, solo piu' corto. Sfilarlo dalla lista
     * avrebbe voluto dire scrivere in `b` dopo averlo smappato. */
    b->dim -= quanto;
}

void free(void *ptr)
{
    Blocco *b;

    if (ptr == NULL) return;            /* free(NULL) e' legittima */

    b = DATI_BLOCCO(ptr);

    /* Una doppia free corromperebbe la lista fondendo due volte lo stesso
     * blocco. Qui si ignora invece di proseguire: e' un difetto del
     * chiamante, ma il danno resterebbe suo solo fino al momento in cui
     * l'heap comincia a consegnare due volte lo stesso indirizzo. */
    if (b->libero) return;

    b->libero = 1;
    heap_fondi_con_succ(b);
    if (b->prec) heap_fondi_con_succ(b->prec);

    /* Si prova a restituire solo se questa free ha toccato la CODA: negli
     * altri casi non c'e' niente da restituire e il controllo sarebbe
     * lavoro sprecato a ogni free. Dopo le fusioni il blocco in coda puo'
     * essere `b` oppure il suo predecessore, se b ci si e' fuso dentro. */
    if (heap_ultimo != NULL && heap_ultimo->libero) heap_restituisci();
}

void *calloc(size_t nmemb, size_t size)
{
    size_t tot = nmemb * size;
    void  *p;

    /* La moltiplicazione puo' traboccare, e un tetto superato in silenzio
     * darebbe un blocco piu' piccolo di quanto il chiamante crede — cioe'
     * una scrittura fuori dai suoi confini che nessuno segnala. */
    if (nmemb != 0 && tot / nmemb != size) return NULL;

    p = malloc(tot);
    if (p) memset(p, 0, tot);
    return p;
}

void *realloc(void *ptr, size_t size)
{
    Blocco *b;
    void   *nuovo;
    size_t  copia;

    if (ptr == NULL)  return malloc(size);
    if (size == 0)    { free(ptr); return NULL; }

    b = DATI_BLOCCO(ptr);

    /* Sta gia' dentro: si tiene il blocco com'e'. Rimpicciolire spezzando
     * e' possibile, e si fa: su un buffer che cresce a raddoppi il
     * recupero non e' trascurabile. */
    if (b->dim >= heap_allinea(size)) {
        heap_spezza(b, heap_allinea(size));
        if (b->succ && b->succ->libero) heap_fondi_con_succ(b->succ);
        return ptr;
    }

    /* Il vicino successivo e' libero e adiacente: si allunga sul posto,
     * senza copiare niente. E' il caso frequente di un buffer che cresce
     * mentre nessun altro alloca in mezzo. */
    if (b->succ && b->succ->libero &&
        (char *)b + BLOCCO_HDR + b->dim == (char *)b->succ &&
        b->dim + BLOCCO_HDR + b->succ->dim >= heap_allinea(size)) {
        /* heap_assorbi_succ e non heap_fondi_con_succ: `b` e' ALLOCATO, e
         * la versione con guscio rifiuterebbe di fondere lasciando il
         * blocco della dimensione di prima. Vedi il commento esteso su
         * heap_assorbi_succ. */
        heap_assorbi_succ(b);
        heap_spezza(b, heap_allinea(size));
        return ptr;
    }

    nuovo = malloc(size);
    if (nuovo == NULL) return NULL;     /* l'originale resta valido */

    /* Si copia il MINIMO fra vecchia e nuova dimensione. La versione
     * precedente copiava sempre `size`, cioe' leggeva oltre la fine del
     * blocco vecchio quando si ingrandiva. */
    copia = (b->dim < size) ? b->dim : size;
    memcpy(nuovo, ptr, copia);
    free(ptr);
    return nuovo;
}

/* =============================================================================
 * ALLOCAZIONE ALLINEATA — memalign, aligned_alloc, posix_memalign
 *
 * CHI LE CHIEDE: la libstdc++. Dal C++17 un tipo con allineamento
 * superiore a quello naturale non passa piu' per `operator new(size_t)`
 * ma per `operator new(size_t, align_val_t)`, e l'implementazione di
 * quella nella libreria standard e' un involucro attorno a memalign()
 * (libsupc++/new_opa.cc). Se memalign non c'e', la libstdc++ ne mette una
 * che ignora l'allineamento richiesto.
 *
 * COME SI FA CON QUESTO HEAP, dove i blocchi sono allineati a otto e
 * l'intestazione ne occupa sedici. Il trucco e' UNO SOLO: si chiede a
 * malloc un blocco abbastanza grande da contenere il risultato ovunque
 * cada l'allineamento, poi lo si SPEZZA IN DUE mettendo una vera
 * intestazione subito prima dell'indirizzo allineato.
 *
 *   prima:   [hdr b][........... dati grezzi ...........]
 *   dopo:    [hdr b][avanzo][hdr n][ dati allineati ....]
 *              ^libero              ^ e' questo che si restituisce
 *
 * ⚠️ LA CONSEGUENZA CHE CONTA: il puntatore restituito ha davanti a se'
 * un'intestazione normale, agganciata alla lista in ordine di indirizzo
 * come tutte. Quindi free() lo tratta come un blocco qualunque, e la
 * fusione con i vicini funziona senza sapere nulla di tutto questo. Non
 * serve una `aligned_free`, e chi passa il puntatore a una free() ignara
 * — per esempio codice di terzi — non rompe niente.
 *
 * La testa resta come blocco LIBERO invece di essere sprecata: su una
 * richiesta con allineamento 4096 sono fino a quattro KB che tornano
 * disponibili invece di restare in ostaggio del blocco allineato.
 * ============================================================================= */

void *memalign(size_t allineamento, size_t size)
{
    Blocco   *b, *nuovo;
    void     *grezzo;
    uintptr_t indirizzo;
    size_t    utile, offset, dim_orig;

    /* Deve essere una potenza di due: e' cio' che dicono sia POSIX sia il
     * C11, ed e' anche l'unica ipotesi sotto cui la maschera qui sotto
     * ha senso. */
    if (allineamento == 0 || (allineamento & (allineamento - 1u)) != 0) {
        errno = EINVAL;
        return NULL;
    }

    /* Fino a otto byte non c'e' niente da fare: malloc gia' li garantisce. */
    if (allineamento <= HEAP_ALLINEA) return malloc(size);

    if (size == 0) size = 1;
    utile = heap_allinea(size);
    if (utile < size) return NULL;              /* trabocco */

    /* Il margine e' `allineamento` (quanto al piu' si deve avanzare) piu'
     * un'intestazione (quella che va messa davanti al risultato). */
    if (utile + allineamento + BLOCCO_HDR < utile) return NULL;
    grezzo = malloc(utile + allineamento + BLOCCO_HDR);
    if (grezzo == NULL) return NULL;

    if (((uintptr_t)grezzo & (allineamento - 1u)) == 0) {
        /* Gia' allineato per caso: succede spesso con allineamenti di 16
         * su un heap allineato a 8. Non si spezza niente. */
        return grezzo;
    }

    b        = DATI_BLOCCO(grezzo);
    dim_orig = b->dim;

    /* Il primo indirizzo allineato che lasci spazio a un'intestazione. */
    indirizzo = ((uintptr_t)grezzo + BLOCCO_HDR + allineamento - 1u)
                & ~(uintptr_t)(allineamento - 1u);

    nuovo  = (Blocco *)(indirizzo - BLOCCO_HDR);
    offset = (size_t)((char *)nuovo - (char *)grezzo);

    nuovo->dim    = dim_orig - offset - BLOCCO_HDR;
    nuovo->libero = 0;
    nuovo->prec   = b;
    nuovo->succ   = b->succ;

    if (b->succ) b->succ->prec = nuovo;
    else         heap_ultimo = nuovo;

    b->succ   = nuovo;
    b->dim    = offset;
    b->libero = 1;
    if (b->prec) heap_fondi_con_succ(b->prec);

    /* La coda in eccesso torna all'heap, se ne vale la pena. */
    heap_spezza(nuovo, utile);

    return BLOCCO_DATI(nuovo);
}

/* Il C11 pretende che `size` sia un multiplo di `allineamento`. Qui non si
 * fa rispettare: rifiutare renderebbe la funzione inutilizzabile come
 * ripiego di memalign, che e' l'uso che ne fa la libstdc++, e concedere in
 * piu' non rompe nessun programma corretto. */
void *aligned_alloc(size_t allineamento, size_t size)
{
    return memalign(allineamento, size);
}

/* ⚠️ NON IMPOSTA errno E NON RITORNA -1: posix_memalign e' l'eccezione
 * che RITORNA il codice di errore. Trattarla come le altre e' l'errore
 * classico su questa funzione. */
int posix_memalign(void **risultato, size_t allineamento, size_t size)
{
    void *p;

    if (risultato == NULL) return EINVAL;

    /* In piu' rispetto a memalign: POSIX chiede che l'allineamento sia
     * anche un multiplo di sizeof(void *). */
    if (allineamento < sizeof(void *) ||
        (allineamento & (allineamento - 1u)) != 0) {
        return EINVAL;
    }

    p = memalign(allineamento, size);
    if (p == NULL) return ENOMEM;

    *risultato = p;
    return 0;
}

/* =============================================================================
 * Funzioni di utilità aggiuntive
 * ============================================================================= */

/* Registra l'errore in errno LASCIANDO il valore di ritorno com'era.
 *
 * E' il punto in cui si applica la regola dichiarata in testa al file:
 * errno si aggiunge, non sostituisce. I programmi che stampano il numero
 * negativo continuano a funzionare parola per parola; chi vuole un
 * messaggio ha perror() e strerror(errno). */
static int32_t err_reg(int32_t r)
{
    if (r < 0) errno = -r;
    return r;
}

/* Variadica come su POSIX, e non per gusto della compatibilita': ogni
 * programma che crea un file scrive open(path, O_CREAT|O_WRONLY, 0644), e
 * con un prototipo a due argomenti quella riga non compila. Il terzo
 * argomento — i permessi — EX-OS lo IGNORA, perche' non ha proprietari
 * ne' permessi sui file; leggerlo lo stesso e' l'unico modo di non
 * lasciare un argomento sullo stack che nessuno toglie. */
int open(const char *path, int flags, ...)
{
    if (flags & O_CREAT) {
        __builtin_va_list args;
        __builtin_va_start(args, flags);
        (void)__builtin_va_arg(args, int);   /* mode_t, ignorato */
        __builtin_va_end(args);
    }
    return err_reg(_syscall3(SYS_OPEN, (uint32_t)path, (uint32_t)flags, 0));
}

int close(int fd)
{
    return err_reg(_syscall1(SYS_CLOSE, (uint32_t)fd));
}

/* dup/dup2/fcntl — vedi il commento esteso in kernel/syscall/syscall_impl.c.
 *
 * ⚠️ I due descrittori condividono il FILE, non la POSIZIONE: ognuno si
 * ricorda per conto suo dove era arrivato, mentre su POSIX una read() da
 * uno dei due sposta anche l'altro. Chi legge da un fd duplicato faccia
 * una lseek() esplicita invece di dare per scontato di ripartire da capo. */
int dup(int fd)
{
    return (int)err_reg(_syscall1(SYS_DUP, (uint32_t)fd));
}

int dup2(int vecchio, int nuovo)
{
    return (int)err_reg(_syscall2(SYS_DUP2, (uint32_t)vecchio, (uint32_t)nuovo));
}

int fcntl(int fd, int cmd, ...)
{
    __builtin_va_list ap;
    uint32_t          arg;

    /* Il terzo argomento c'e' solo per F_DUPFD e F_SETFD/F_SETFL. Leggerlo
     * sempre e' innocuo — cdecl mette tutto sullo stack e il kernel lo
     * ignora dove non serve — ed evita tre rami che direbbero la stessa
     * cosa. */
    __builtin_va_start(ap, cmd);
    arg = __builtin_va_arg(ap, uint32_t);
    __builtin_va_end(ap);

    return (int)err_reg(_syscall3(SYS_FCNTL, (uint32_t)fd, (uint32_t)cmd, arg));
}

ssize_t read(int fd, void *buf, size_t n)
{
    return err_reg(_syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buf, n));
}

ssize_t write(int fd, const void *buf, size_t n)
{
    return err_reg(_syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, n));
}

int getpid(void)
{
    return _syscall1(SYS_GETPID, 0);
}

/* =============================================================================
 * ioctl — comandi al terminale. Vedi i TTY_IOCTL_* in lib/include/libc.h.
 *
 * 'arg' è un puntatore per TTY_IOCTL_GETSIZE e un VALORE per gli altri:
 * è la convenzione dell'ioctl di Unix, dove il terzo argomento è
 * genericamente una parola e ogni comando decide come leggerla.
 * ============================================================================= */
int ioctl(int fd, unsigned int request, void *arg)
{
    return _syscall3(SYS_IOCTL, (uint32_t)fd, request, (uint32_t)arg);
}

int tty_getsize(TtyWinSize *ws)
{
    return ioctl(1, TTY_IOCTL_GETSIZE, ws);
}

int tty_raw(int on)
{
    return ioctl(1, on ? TTY_IOCTL_SETRAW : TTY_IOCTL_SETCOOKED, (void *)0);
}

int tty_clear(void)
{
    return ioctl(1, TTY_IOCTL_CLEAR, (void *)0);
}

int chdir(const char *path)
{
    return err_reg(_syscall1(SYS_CHDIR, (uint32_t)path));
}

char *getcwd(char *buf, size_t size)
{
    int r = _syscall2(SYS_GETCWD, (uint32_t)buf, size);
    return (r >= 0) ? buf : NULL;
}

/* =============================================================================
 * realpath — il percorso in forma canonica
 *
 * Rende assoluto (contro la directory corrente), toglie i "." e i ".." e
 * i doppioni di '/', e verifica che il file ESISTA — che e' cio' che
 * distingue realpath da una normalizzazione di stringhe qualunque.
 *
 * ⚠️ NON SEGUE NESSUN COLLEGAMENTO SIMBOLICO, e non e' una
 * semplificazione: EX-OS non ne ha (vedi <sys/stat.h>), quindi non c'e'
 * niente da seguire. Su un Unix questa e' la parte difficile della
 * funzione; qui non esiste il problema.
 *
 * ⚠️ IL ".." SI RISOLVE SULLA STRINGA, non sul filesystem. Senza
 * collegamenti le due cose coincidono sempre; il giorno che ci fossero,
 * questa riga diventerebbe sbagliata.
 *
 * PERCHE' SERVIVA. `lrealpath` di libiberty prova quattro strade e, se il
 * sistema non ne offre nessuna, **cade in fondo alla funzione senza
 * ritornare niente** — non e' un errore che si vede, e' un valore di
 * ritorno che vale quel che resta in EAX. `ld` la usa per confrontare il
 * file di uscita con quelli di ingresso, e con due valori casuali uguali
 * rifiutava di collegare:
 *
 *     ld: input file '/prova.o' is the same as output file
 *
 * Con `resolved` a NULL il risultato e' allocato con malloc, come
 * consente POSIX 2008: chi lo riceve lo libera.
 * ============================================================================= */
char *realpath(const char *path, char *resolved)
{
    char        assoluto[PERCORSO_MAX];
    char        uscita[PERCORSO_MAX];
    struct stat st;
    size_t      n = 0;
    const char *p;

    if (path == NULL || path[0] == '\0') { errno = EINVAL; return NULL; }

    /* 1. Rendilo assoluto. */
    if (path[0] == '/') {
        if (strlen(path) >= sizeof(assoluto)) { errno = ENAMETOOLONG; return NULL; }
        strcpy(assoluto, path);
    } else {
        size_t l;
        if (getcwd(assoluto, sizeof(assoluto)) == NULL) return NULL;
        l = strlen(assoluto);
        if (l == 0 || assoluto[l - 1] != '/') {
            if (l + 1 >= sizeof(assoluto)) { errno = ENAMETOOLONG; return NULL; }
            assoluto[l++] = '/';
            assoluto[l] = '\0';
        }
        if (l + strlen(path) >= sizeof(assoluto)) { errno = ENAMETOOLONG; return NULL; }
        strcpy(assoluto + l, path);
    }

    /* 2. Componente per componente. `n` e' la lunghezza di cio' che si e'
     * gia' accettato, e resta SEMPRE senza '/' finale tranne che per la
     * radice — cosi' il ".." non deve distinguere i due casi. */
    uscita[0] = '/';
    uscita[1] = '\0';
    n = 1;

    p = assoluto;
    while (*p) {
        const char *fine;
        size_t      len;

        while (*p == '/') p++;
        if (*p == '\0') break;

        fine = p;
        while (*fine && *fine != '/') fine++;
        len = (size_t)(fine - p);

        if (len == 1 && p[0] == '.') {
            /* "." non aggiunge niente */
        } else if (len == 2 && p[0] == '.' && p[1] == '.') {
            /* Torna indietro di un componente. Sulla radice il ".." e' la
             * radice stessa: e' cosi' su ogni sistema, e non e' un errore. */
            while (n > 1 && uscita[n - 1] != '/') n--;
            if (n > 1) n--;             /* togli anche lo '/' */
            uscita[n ? n : 1] = '\0';
            if (n == 0) { uscita[0] = '/'; uscita[1] = '\0'; n = 1; }
        } else {
            if (n > 1) {
                if (n + 1 >= sizeof(uscita)) { errno = ENAMETOOLONG; return NULL; }
                uscita[n++] = '/';
            }
            if (n + len >= sizeof(uscita)) { errno = ENAMETOOLONG; return NULL; }
            memcpy(uscita + n, p, len);
            n += len;
            uscita[n] = '\0';
        }

        p = fine;
    }

    /* 3. Deve esistere: e' la differenza fra realpath e una pulizia di
     * stringa. Chi vuole il nome di un file da CREARE canonicalizza la
     * directory che lo conterra', non il file. */
    if (stat(uscita, &st) != 0) { errno = ENOENT; return NULL; }

    if (resolved == NULL) {
        char *copia = (char *)malloc(n + 1);
        if (copia == NULL) { errno = ENOMEM; return NULL; }
        memcpy(copia, uscita, n + 1);
        return copia;
    }

    memcpy(resolved, uscita, n + 1);
    return resolved;
}

int listdir_from(const char *path, DirEntry *buf, int max, int start)
{
    return _syscall4(SYS_READDIR, (uint32_t)path, (uint32_t)buf,
                     (uint32_t)max, (uint32_t)start);
}

int listdir(const char *path, DirEntry *buf, int max)
{
    return listdir_from(path, buf, max, 0);
}

/* =============================================================================
 * PROCESSI: spawn, redirezioni, attesa
 *
 * Non c'e' fork(), e non e' una mancanza da colmare: fork duplica uno
 * spazio di indirizzamento per poi buttarlo via alla exec successiva, e su
 * un sistema senza copy-on-write sarebbe la cosa piu' costosa che si possa
 * fare. Un driver di compilatore non fa altro che "lancia questo e
 * aspettalo", ed e' esattamente cio' che spawn fa in un colpo solo.
 *
 * La redirezione e' per PERCORSO, non per descrittore gia' aperto: il
 * figlio apre il proprio file. Vedi kernel/include/syscall.h per il
 * perche' — in due parole, due processi sullo stesso handle VFS vorrebbero
 * un conteggio di riferimenti che non c'e'.
 * ============================================================================= */
int spawn(const char *path, char *const argv[])
{
    return spawn_ex(path, argv, environ, NULL, 0);
}

/* =============================================================================
 * pipe — le tre regole stanno in lib/include/libc.h, qui c'e' la chiamata.
 *
 * ⚠️ IL KERNEL SCRIVE I DUE NUMERI NELL'ARRAY, non li ritorna: il valore
 * di ritorno e' solo riuscito/fallito. Chi si aspetta il descrittore come
 * da open() legge zero e crede di aver ricevuto stdin.
 * ============================================================================= */
int pipe(int fd[2])
{
    int32_t r;

    if (fd == NULL) { errno = EFAULT; return -1; }

    r = _syscall1(SYS_PIPE, (uint32_t)(uintptr_t)fd);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

int spawn_ex(const char *path, char *const argv[], char *const envp[],
             const SpawnRedir *redir, int n_redir)
{
    SpawnExtra ex;
    int        argc = 0, i;

    if (path == NULL) { errno = EINVAL; return -1; }

    if (argv != NULL) while (argv[argc] != NULL) argc++;

    ex.magia    = SPAWN_EXTRA_MAGIA;
    ex.envp     = (char **)envp;
    ex.n_azioni = 0;

    if (n_redir < 0) n_redir = 0;
    if (n_redir > SPAWN_MAX_AZIONI) n_redir = SPAWN_MAX_AZIONI;

    for (i = 0; i < n_redir; i++) {
        size_t j;

        ex.azioni[i].fd       = (unsigned)redir[i].fd;
        ex.azioni[i].flags    = (unsigned)redir[i].flags;
        ex.azioni[i].fd_padre = redir[i].fd_padre;

        /* `percorso` NULL vuol dire «passa il mio descrittore», ed e' il
         * modo in cui si costruisce una pipe fra due processi. Vedi il
         * commento su SpawnRedir in lib/include/libc.h. */
        if (redir[i].percorso == NULL) {
            ex.azioni[i].tipo        = SPAWN_AZ_FD;
            ex.azioni[i].percorso[0] = '\0';
        } else {
            ex.azioni[i].tipo = SPAWN_AZ_FILE;
            for (j = 0; j + 1 < SPAWN_RED_PATH_MAX && redir[i].percorso[j]; j++)
                ex.azioni[i].percorso[j] = redir[i].percorso[j];
            ex.azioni[i].percorso[j] = '\0';
        }
        ex.n_azioni++;
    }

    return err_reg(_syscall4(SYS_SPAWN, (uint32_t)path, (uint32_t)argc,
                             (uint32_t)argv, (uint32_t)&ex));
}

int waitpid(int pid, int *stato, int opzioni)
{
    return err_reg(_syscall3(SYS_WAITPID, (uint32_t)pid,
                             (uint32_t)stato, (uint32_t)opzioni));
}

int wait(int *stato)
{
    return waitpid(-1, stato, 0);
}

/* =============================================================================
 * DIRECTORY nella forma POSIX
 *
 * Sopra listdir_from(), che e' paginata: DIR tiene una pagina di voci e la
 * riempie quando finisce. Il buffer e' nella struttura e non condiviso,
 * cosi' due directory aperte insieme non si disturbano — che e' proprio
 * cio' che fa chi cerca un file dentro una catena di percorsi.
 * ============================================================================= */
struct __dir {
    char      percorso[256];
    DirEntry  voci[LISTDIR_MAX_BATCH];
    int       n;            /* voci valide nel buffer */
    int       i;            /* prossima da consegnare */
    int       start;        /* offset della prossima pagina */
    int       finito;
    struct dirent corrente;
};

DIR *opendir(const char *path)
{
    DIR *d;
    size_t i;

    if (path == NULL) { errno = EINVAL; return NULL; }

    /* Che il percorso ESISTA e sia una directory va verificato qui: se no
     * l'errore comparirebbe alla prima readdir(), dove chi chiama lo
     * legge come "directory vuota". */
    {
        struct stat st;
        if (stat(path, &st) != 0) return NULL;
        if (!S_ISDIR(st.st_mode)) { errno = ENOTDIR; return NULL; }
    }

    d = (DIR *)malloc(sizeof(DIR));
    if (d == NULL) { errno = ENOMEM; return NULL; }

    for (i = 0; i + 1 < sizeof(d->percorso) && path[i]; i++)
        d->percorso[i] = path[i];
    d->percorso[i] = '\0';

    d->n = d->i = d->start = d->finito = 0;
    return d;
}

struct dirent *readdir(DIR *d)
{
    if (d == NULL) { errno = EBADF; return NULL; }

    if (d->i >= d->n) {
        int n;

        if (d->finito) return NULL;

        n = listdir_from(d->percorso, d->voci, LISTDIR_MAX_BATCH, d->start);
        if (n <= 0) { d->finito = 1; return NULL; }

        /* Meno voci di quante ne abbiamo chieste: era l'ultima pagina.
         * E' lo stesso idioma di ls e install — vedi LISTDIR_MAX_BATCH. */
        if (n < LISTDIR_MAX_BATCH) d->finito = 1;

        d->n      = n;
        d->i      = 0;
        d->start += n;
    }

    {
        DirEntry *v = &d->voci[d->i++];
        size_t    k;

        for (k = 0; k + 1 < sizeof(d->corrente.d_name) && v->name[k]; k++)
            d->corrente.d_name[k] = v->name[k];
        d->corrente.d_name[k] = '\0';
        d->corrente.d_ino  = 0;
        d->corrente.d_type = v->is_dir ? DT_DIR : DT_REG;
        return &d->corrente;
    }
}

void rewinddir(DIR *d)
{
    if (d == NULL) return;
    d->n = d->i = d->start = d->finito = 0;
}

int closedir(DIR *d)
{
    if (d == NULL) { errno = EBADF; return -1; }
    free(d);
    return 0;
}

/* =============================================================================
 * File temporanei
 *
 * I nomi si costruiscono con PID e millisecondi dall'avvio, piu' un
 * contatore: due processi che chiedono un temporaneo nello stesso
 * millisecondo devono avere nomi diversi, e il PID lo garantisce.
 *
 * ⚠️ mkstemp APRE il file, e questo e' il punto: un tmpnam() seguito da
 * open() ha in mezzo una finestra in cui qualcun altro puo' creare quel
 * nome. Qui la finestra resta (manca O_EXCL nel kernel), ma il nome e' gia'
 * improbabile da indovinare e il file viene creato subito.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ LA DIRECTORY VIENE DA `TMPDIR`, E SENZA E' LA RADICE
 *
 * Qui c'era un `#define TMP_PREFISSO "/tmp"` che NESSUNO USAVA: il nome si
 * componeva sempre come "/t....tmp", cioe' nella radice. Innocuo finche' la
 * radice e' il floppy, che si scrive.
 *
 * Non lo e' piu' da quando EX-OS si avvia da CD: li' la radice e' in sola
 * lettura, ogni temporaneo fallisce con EROFS, e a fallire non e' il
 * programma che lo ha chiesto — e' il DRIVER del compilatore, che i
 * temporanei li usa per passare il .s da cc1 ad as. Il sintomo sarebbe
 * "gcc non funziona sul CD", che e' la descrizione piu' inutile possibile
 * del problema.
 *
 * `TMPDIR` e' la stessa variabile che legge choose_tmpdir() di libiberty
 * (vedi gcc/libiberty/make-temp-file.c): impostarla una volta serve al
 * nostro mkstemp E al codice di terzi, che altrimenti sceglierebbero due
 * posti diversi.
 *
 *     export TMPDIR=/disco        (un volume montato in scrittura)
 *
 * La radice resta il ripiego perche' e' l'unico posto che esiste di
 * sicuro: una directory /tmp qui non la crea nessuno all'avvio.
 * ============================================================================= */

static unsigned g_tmp_contatore = 0;

static void tmp_componi(char *dst, size_t max)
{
    unsigned    pid = (unsigned)getpid();
    unsigned    ms  = (unsigned)uptime_ms();
    const char *dir = getenv("TMPDIR");
    size_t      n;

    if (dir == NULL || dir[0] == '\0') dir = "";

    /* Una barra finale di troppo darebbe "/disco//t1.tmp": non e' un
     * errore per il VFS, ma il nome che l'utente vede in un messaggio
     * d'errore deve essere quello che puo' ridigitare. */
    n = strlen(dir);
    while (n > 0 && dir[n - 1] == '/') n--;

    snprintf(dst, max, "%.*s/t%x%x%x.tmp",
             (int)n, dir, pid, ms, ++g_tmp_contatore);
}

char *tmpnam(char *buf)
{
    static char interno[64];
    char *dst = (buf != NULL) ? buf : interno;

    tmp_componi(dst, 64);
    return dst;
}

int mkstemp(char *modello)
{
    /* Il modello finisce per XXXXXX e va riempito SUL POSTO: chi chiama si
     * aspetta di ritrovarci il nome vero, perche' e' cosi' che poi
     * cancella il file. */
    size_t len;
    int    fd, tentativi;

    if (modello == NULL) { errno = EINVAL; return -1; }
    len = strlen(modello);
    if (len < 6) { errno = EINVAL; return -1; }

    for (tentativi = 0; tentativi < 32; tentativi++) {
        char nome[64];
        size_t j;

        tmp_componi(nome, sizeof(nome));

        /* Le sei X prendono le ultime sei cifre del nome generato. */
        {
            size_t ln = strlen(nome);
            for (j = 0; j < 6; j++)
                modello[len - 6 + j] = nome[ln - 6 + j];
        }

        if (access(modello, F_OK) == 0) continue;   /* esiste: riprova */

        fd = open(modello, O_RDWR | O_CREAT | O_TRUNC);
        if (fd >= 0) return fd;
    }

    errno = EEXIST;
    return -1;
}

/* ⚠️ mktemp E' LA VERSIONE INSICURA DI mkstemp, e lo e' per costruzione:
 * riempie le sei X e se ne va SENZA creare il file, quindi fra il nome e
 * l'uso c'e' una finestra in cui qualcun altro puo' prendersi quel nome.
 * Non e' un difetto di questa implementazione — e' cosa fa la funzione, ed
 * e' il motivo per cui ogni Unix la segnala come deprecata.
 *
 * C'e' perche' choose-temp.c di libiberty la chiama, e il suo modo di
 * usarla e' quello innocuo: costruisce un nome e lo passa subito a una
 * open() con O_CREAT. Nel codice nuovo si usi mkstemp, che il file lo apre.
 *
 * Ritorna il modello riempito, o la stringa vuota se non ha trovato un
 * nome libero — che e' quello che dice POSIX, non NULL. */
char *mktemp(char *modello)
{
    size_t len;
    int    tentativi;

    if (modello == NULL) { errno = EINVAL; return modello; }
    len = strlen(modello);
    if (len < 6) { errno = EINVAL; modello[0] = '\0'; return modello; }

    for (tentativi = 0; tentativi < 32; tentativi++) {
        char   nome[64];
        size_t j, ln;

        tmp_componi(nome, sizeof(nome));
        ln = strlen(nome);
        for (j = 0; j < 6; j++)
            modello[len - 6 + j] = nome[ln - 6 + j];

        if (access(modello, F_OK) != 0) return modello;
    }

    errno = EEXIST;
    modello[0] = '\0';
    return modello;
}

FILE *tmpfile(void)
{
    char  nome[64];
    FILE *f;

    tmp_componi(nome, sizeof(nome));
    f = fopen(nome, "w+");
    if (f == NULL) return NULL;

    /* Su Unix qui si cancella il nome lasciando vivo il descrittore. Il
     * VFS di EX-OS non tiene un file aperto dopo unlink, quindi il file
     * RESTA sul disco e va cancellato da chi lo ha chiesto: e' una
     * differenza che vale la pena sapere, non un dettaglio. */
    return f;
}

/* =============================================================================
 * Interrogazioni sui file
 * ============================================================================= */
int access(const char *path, int modo)
{
    struct stat st;

    /* I permessi non esistono ancora: un file che c'e' e' leggibile e
     * scrivibile, salvo che il montaggio sia in sola lettura — e quello si
     * scopre alla scrittura. F_OK, R_OK e W_OK danno quindi la stessa
     * risposta, X_OK compresa: su un sistema senza bit di esecuzione,
     * "eseguibile" vuol dire "esiste". */
    (void)modo;
    if (stat(path, &st) != 0) return -1;
    return 0;
}

/* =============================================================================
 * ⚠️ chmod, fchmod e umask NON CAMBIANO NIENTE
 *
 * EX-OS non ha utenti, gruppi ne' permessi: l'unico bit che i filesystem
 * montabili tengono davvero e' la sola lettura di FAT, e per quello non
 * c'e' una syscall che lo scriva. Queste tre non sono un'approssimazione
 * di un permesso: sono l'accettazione di una richiesta che non ha dove
 * andare.
 *
 * PERCHE' ESISTONO LO STESSO, dopo che <sys/stat.h> aveva scritto per un
 * anno che dichiararle "vorrebbe dire promettere che cambiano qualcosa".
 * Perche' l'alternativa e' peggiore: bfd chiude ogni file eseguibile che
 * produce con `umask(0); umask(mask); chmod(nome, ...)`, e senza queste
 * tre righe la scelta e' fra rattoppare i sorgenti di terzi uno per uno —
 * e rifarlo a ogni rilascio — oppure non avere binutils. E' la stessa
 * convenzione gia' usata per O_EXCL e O_SYNC in <fcntl.h>: il nome c'e',
 * il commento dice forte che e' inerte.
 *
 * umask ritorna 0, ed e' l'unica delle tre a dire una cosa VERA: zero
 * significa "non maschero niente", che e' esattamente cio' che succede.
 * ============================================================================= */
int chmod(const char *path, mode_t modo)
{
    struct stat st;

    (void)modo;
    /* L'unica parte onesta: se il file non c'e', chmod deve fallire. */
    if (stat(path, &st) != 0) { errno = ENOENT; return -1; }
    return 0;
}

int fchmod(int fd, mode_t modo)
{
    (void)modo;
    if (fsize(fd) < 0) { errno = EBADF; return -1; }
    return 0;
}

mode_t umask(mode_t maschera)
{
    (void)maschera;
    return 0;
}

int isatty(int fd)
{
    unsigned short ws[4];

    /* ioctl risponde ENOTTY su tutto cio' che non e' la console: e'
     * esattamente la domanda, e non serve una syscall nuova. */
    if (ioctl(fd, 0x5413 /* TIOCGWINSZ */, ws) == 0) return 1;
    errno = ENOTTY;
    return 0;
}

/* =============================================================================
 * rename — cambia il NOME, e dalla 0.161 NON copia piu' i dati
 *
 * ⚠️ FINO ALLA 0.160 QUESTA FUNZIONE ERA UNA COPIA SEGUITA DA UNA
 * CANCELLAZIONE, e portava il nome di un'altra cosa. Le conseguenze non
 * erano teoriche:
 *   - costava quanto il file, mentre una rinomina vera non muove niente;
 *   - RIALLOCAVA i blocchi, quindi un file contiguo poteva tornare
 *     frammentato — ed e' proprio cio' che rendeva impossibile a
 *     `install` verificare la mappa dei settori prima di dare al kernel
 *     il suo nome definitivo.
 *
 * Ora e' la syscall SYS_RENAME, che riscrive la voce di directory e basta.
 * ⚠️ I BLOCCHI NON SI SPOSTANO: e' la garanzia su cui si regge
 * l'installatore.
 *
 * ⚠️ DUE DIFFERENZE DA POSIX, dichiarate:
 *   - solo NELLA STESSA DIRECTORY e nello stesso montaggio, ENOSYS per il
 *     resto. Attraversare un montaggio non e' una rinomina: e' una copia
 *     piu' una cancellazione, cioe' un'altra operazione con un altro
 *     costo e un altro modo di fallire;
 *   - NON sostituisce la destinazione: EEXIST. Sostituire vuol dire
 *     cancellare un file che il chiamante non ha nominato come vittima.
 *     Chi vuole sostituire cancella prima, e la perdita e' una scelta.
 * ============================================================================= */
int rename(const char *da, const char *a)
{
    int32_t r;

    if (da == NULL || a == NULL) { errno = EINVAL; return -1; }

    r = _syscall2(SYS_RENAME, (uint32_t)(uintptr_t)da, (uint32_t)(uintptr_t)a);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

/* =============================================================================
 * Segnali: ci sono i nomi, non c'e' la consegna
 *
 * EX-OS non ha segnali. Dichiararli comunque serve a far COMPILARE codice
 * che li usa per casi che qui non si presentano (un compilatore installa
 * un gestore di SIGSEGV per stampare "internal compiler error"), senza
 * fingere che funzionino: signal() registra e ritorna il gestore
 * precedente, e nessuno lo chiamera' mai — tranne raise(), che lo invoca
 * direttamente perche' e' l'unico caso in cui il mittente e' il processo
 * stesso.
 * ============================================================================= */
static void (*g_segnali[SIG_MAX])(int);

void (*signal(int sig, void (*gestore)(int)))(int)
{
    void (*prec)(int);

    if (sig <= 0 || sig >= SIG_MAX) { errno = EINVAL; return SIG_ERR; }

    prec = g_segnali[sig];
    g_segnali[sig] = gestore;
    return (prec == NULL) ? SIG_DFL : prec;
}

int raise(int sig)
{
    if (sig <= 0 || sig >= SIG_MAX) { errno = EINVAL; return -1; }

    if (g_segnali[sig] != NULL && g_segnali[sig] != SIG_IGN &&
        g_segnali[sig] != SIG_DFL) {
        g_segnali[sig](sig);
        return 0;
    }

    if (g_segnali[sig] == SIG_IGN) return 0;

    /* Comportamento predefinito: per i segnali fatali si esce, per gli
     * altri non succede niente. E' la sola parte che si puo' onorare
     * davvero senza un sistema di segnali. */
    if (sig == SIGABRT || sig == SIGSEGV || sig == SIGILL || sig == SIGFPE) {
        fprintf(stderr, "%s\n", strsignal(sig));
        exit(128 + sig);
    }
    return 0;
}

/* `char *` e non `const char *` per la stessa ragione di strerror: e' la
 * firma dello standard, ed e' quella che il codice di terzi ridichiara. */
char *strsignal(int sig)
{
    switch (sig) {
        case SIGHUP:  return "Interruzione della linea";
        case SIGINT:  return "Interruzione da tastiera";
        case SIGQUIT: return "Uscita richiesta";
        case SIGILL:  return "Istruzione non valida";
        case SIGABRT: return "Interruzione anomala";
        case SIGFPE:  return "Errore aritmetico";
        case SIGKILL: return "Terminato";
        case SIGSEGV: return "Accesso a memoria non valido";
        case SIGPIPE: return "Scrittura su una pipe senza lettori";
        case SIGALRM: return "Sveglia";
        case SIGTERM: return "Terminazione richiesta";
        default:      return "Segnale sconosciuto";
    }
}

/* =============================================================================
 * Localizzazione: esiste solo la locale "C"
 *
 * Un compilatore chiama setlocale(LC_ALL, "") all'avvio. Rispondere NULL
 * lo farebbe abortire su alcune versioni; rispondere "C" e' vero — e' la
 * sola locale che questo sistema ha, e le sue regole sono quelle che
 * ctype.c implementa gia'.
 * ============================================================================= */
char *setlocale(int categoria, const char *nome)
{
    static char c[] = "C";

    (void)categoria;
    if (nome == NULL || nome[0] == '\0' ||
        (nome[0] == 'C' && nome[1] == '\0')) return c;

    /* Qualunque altra locale non c'e': si dice, invece di far finta. */
    return NULL;
}

/* =============================================================================
 * localeconv — le convenzioni della locale "C", e sono quasi tutte vuote
 *
 * ⚠️ I CAMPI NON SPECIFICATI VALGONO 127 (CHAR_MAX) E NON ZERO. Non e' un
 * dettaglio: 127 significa «questa locale non lo dice», zero significa
 * «zero cifre». Un programma che formatta una somma di denaro leggendo
 * frac_digits stamperebbe, con lo zero, importi senza decimali credendo
 * che sia la regola locale. E' l'errore classico di chi riempie questa
 * struttura a memoria.
 *
 * La struttura e' `static` e non `const` solo perche' localeconv() deve
 * ritornare un `struct lconv *` non costante, come dice lo standard. Non
 * va modificata da nessuno.
 * ============================================================================= */
struct lconv *localeconv(void)
{
    static char vuota[] = "";
    static char punto[] = ".";
    static struct lconv c_locale;
    static int  pronta = 0;

    if (!pronta) {
        /* L'unico campo che la locale "C" specifica davvero. */
        c_locale.decimal_point     = punto;

        c_locale.thousands_sep     = vuota;
        c_locale.grouping          = vuota;
        c_locale.int_curr_symbol   = vuota;
        c_locale.currency_symbol   = vuota;
        c_locale.mon_decimal_point = vuota;
        c_locale.mon_thousands_sep = vuota;
        c_locale.mon_grouping      = vuota;
        c_locale.positive_sign     = vuota;
        c_locale.negative_sign     = vuota;

        c_locale.int_frac_digits    = 127;      /* CHAR_MAX: non specificato */
        c_locale.frac_digits        = 127;
        c_locale.p_cs_precedes      = 127;
        c_locale.p_sep_by_space     = 127;
        c_locale.n_cs_precedes      = 127;
        c_locale.n_sep_by_space     = 127;
        c_locale.p_sign_posn        = 127;
        c_locale.n_sign_posn        = 127;
        c_locale.int_p_cs_precedes  = 127;
        c_locale.int_p_sep_by_space = 127;
        c_locale.int_n_cs_precedes  = 127;
        c_locale.int_n_sep_by_space = 127;
        c_locale.int_p_sign_posn    = 127;
        c_locale.int_n_sign_posn    = 127;

        pronta = 1;
    }

    return &c_locale;
}

/* =============================================================================
 * Interrogazioni sul sistema
 * ============================================================================= */
long sysconf(int nome)
{
    switch (nome) {
        case _SC_PAGESIZE:          return 4096;
        case _SC_OPEN_MAX:          return 32;    /* MAX_FD del kernel */
        case _SC_CLK_TCK:           return 100;   /* il PIT gira a 100 Hz */
        case _SC_NPROCESSORS_ONLN:  return 1;
        case _SC_ARG_MAX:           return 16 * 320;
        default:                    errno = EINVAL; return -1;
    }
}

/* pathconf/fpathconf — gli stessi limiti di sysconf, ma per un file.
 *
 * ⚠️ NON DIPENDONO DAL PERCORSO, e su un sistema piu' grande dipenderebbero:
 * la lunghezza massima di un nome e' una proprieta' del FILESYSTEM, e su
 * EX-OS ne convivono quattro (FAT12, FAT16/32, ext2, ISO 9660) con limiti
 * diversi — 8.3 su FAT, 255 su ext2. Qui si risponde il massimo del piu'
 * generoso, che e' la risposta prudente per chi dimensiona un buffer e
 * quella sbagliata per chi verifica se un nome ci sta. Chi deve saperlo
 * davvero provi a creare il file e guardi l'errore.
 *
 * La chiama lrealpath di libiberty per decidere quanto allocare. */
long pathconf(const char *path, int nome)
{
    (void)path;
    switch (nome) {
        case _PC_PATH_MAX:      return PERCORSO_MAX;
        case _PC_NAME_MAX:      return 255;         /* il massimo di ext2 */
        case _PC_LINK_MAX:      return 1;           /* nessun collegamento */
        case _PC_NO_TRUNC:      return 0;           /* su FAT i nomi si troncano */
        case _PC_CHOWN_RESTRICTED: return 1;        /* non ci sono utenti */
        default:                errno = EINVAL; return -1;
    }
}

long fpathconf(int fd, int nome)
{
    if (fsize(fd) < 0) { errno = EBADF; return -1; }
    return pathconf(NULL, nome);
}

clock_t times(struct tms *t)
{
    /* Non c'e' contabilita' per processo: si riporta il tempo trascorso
     * dall'avvio in tick, e i quattro campi a zero. Un profilo basato su
     * questi numeri direbbe zero, ed e' meglio di un numero inventato. */
    clock_t tick = (clock_t)(uptime_ms() / 10);

    if (t != NULL) {
        t->tms_utime = t->tms_stime = 0;
        t->tms_cutime = t->tms_cstime = 0;
    }
    return tick;
}

clock_t clock(void)
{
    return (clock_t)(uptime_ms() / 10);
}

/* =============================================================================
 * getrusage — ⚠️ RIPORTA UN LIMITE SUPERIORE, NON UNA MISURA
 *
 * EX-OS non tiene contabilita' per processo: lo scheduler assegna quanti e
 * non misura consumi. Quindi `ru_utime` riporta il tempo TRASCORSO
 * dall'avvio del sistema — che e' certamente >= al tempo di CPU di questo
 * processo, quindi non e' un numero inventato — e tutto il resto vale zero.
 *
 * ⚠️ CHI CI COSTRUISCE SOPRA UN PROFILO OTTERRA' NUMERI PRIVI DI
 * SIGNIFICATO. E' il caso di `gcc -ftime-report`, che stampera' per ogni
 * passaggio lo stesso tempo. Si dichiara comunque perche' senza di lei GCC
 * non si collega, e perche' zero secco su tutto sarebbe altrettanto falso
 * e meno utile: almeno cosi' due chiamate successive danno numeri che
 * crescono, e una differenza fra due istanti resta leggibile.
 *
 * Il giorno che servisse davvero, la contabilita' va nello scheduler
 * (kernel/sched/sched.c): un contatore di tick per processo aggiornato a
 * ogni cambio di contesto, e una syscall per leggerlo.
 * ============================================================================= */
int getrusage(int chi, struct rusage *uso)
{
    unsigned int ms;

    if (uso == NULL) { errno = EFAULT; return -1; }
    if (chi != RUSAGE_SELF && chi != RUSAGE_CHILDREN) {
        errno = EINVAL;
        return -1;
    }

    memset(uso, 0, sizeof(*uso));

    /* Per i figli non si sa proprio niente: zero e' l'unica risposta
     * onesta, e non e' un ripiego — RUSAGE_CHILDREN chiede i consumi dei
     * figli TERMINATI E RACCOLTI, che qui nessuno registra. */
    if (chi == RUSAGE_CHILDREN) return 0;

    ms = uptime_ms();
    uso->ru_utime.tv_sec  = (long)(ms / 1000u);
    uso->ru_utime.tv_usec = (long)((ms % 1000u) * 1000u);
    return 0;
}

int getpagesize(void)
{
    return 4096;
}

/* =============================================================================
 * mmap, munmap
 *
 * ⚠️ SOLO MEMORIA ANONIMA, e il rifiuto e' esplicito. EX-OS non sa mappare
 * un file: servirebbero le pagine sporche e il momento in cui riscriverle,
 * cioe' un pezzo di gestore della memoria che non c'e'. Una mmap che
 * fingesse di mappare un file consegnando zeri darebbe un programma che
 * legge dati sbagliati senza che niente lo segnali.
 *
 * ⚠️ SU FALLIMENTO RITORNA MAP_FAILED, cioe' (void *)-1, NON NULL: e' la
 * convenzione di POSIX ed e' il modo classico di sbagliare a usarla.
 * ============================================================================= */
void *mmap(void *addr, size_t lung, int prot, int flags, int fd, long off)
{
    MmapParams p;
    int32_t    r;

    if (lung == 0) { errno = EINVAL; return MAP_FAILED; }

    if (fd != -1 || !(flags & MAP_ANONYMOUS)) {
        /* ENODEV e non ENOSYS: la syscall c'e', e' il TIPO di mappatura
         * che non e' supportato — ed e' quello che dice POSIX per una
         * mmap su un oggetto che non si puo' mappare. */
        errno = ENODEV;
        return MAP_FAILED;
    }

    p.addr   = (uint32_t)(uintptr_t)addr;
    p.length = (uint32_t)lung;
    p.prot   = (uint32_t)prot;
    p.flags  = (uint32_t)flags;
    p.fd     = -1;
    p.offset = (uint32_t)off;

    r = _syscall1(SYS_MMAP, (uint32_t)(uintptr_t)&p);
    if (r <= 0) {
        errno = (r < 0) ? -r : ENOMEM;
        return MAP_FAILED;
    }
    return (void *)(uintptr_t)r;
}

int munmap(void *addr, size_t lung)
{
    int32_t r = _syscall2(SYS_MUNMAP, (uint32_t)(uintptr_t)addr,
                          (uint32_t)lung);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

/* =============================================================================
 * _libc_start — chiamata da lib/start.S dopo aver letto argc/argv
 *               dallo stack iniziale costruito da sys_spawn.
 *
 * I programmi che usano la libc implementano main(int argc, char **argv).
 * lib/start.S fornisce _start (weak, assembly puro) che legge argc/argv
 * da [esp+4]/[esp+8] prima di qualunque prolog GCC, poi chiama qui.
 *
 * libc.c non include libc.h (è self-contained): dichiariamo main() qui.
 * Da GCC 14 la chiamata implicita è un errore, non più un warning.
 * ============================================================================= */
int main(int argc, char **argv);

/* =============================================================================
 * ⛔ I COSTRUTTORI GLOBALI, che fino ad agosto 2026 NON VENIVANO CHIAMATI
 *
 * In C++ un oggetto dichiarato a livello di file ha un costruttore che
 * deve girare PRIMA di main(). Il compilatore mette il puntatore a quella
 * funzione nella sezione `.init_array`, e sta al codice di avvio
 * percorrerla. La nostra non lo faceva: ogni oggetto globale di ogni
 * programma C++ restava con i byte che si trovava.
 *
 * NON SI VEDEVA, ed e' il motivo per cui e' rimasto li' tanto: i
 * programmi di EX-OS sono in C e non hanno costruttori globali, e la
 * prova di libstdc++ (bin/iso/prova-cpp.cpp) costruisce i propri oggetti
 * DENTRO main. Il primo programma vero a inciamparci e' stato cc1, che
 * ne ha 57:
 *
 *     [ 4] .init_array  INIT_ARRAY  0a1be000  0000e4  (228 byte = 57 voci)
 *
 * Fra quei 57 c'e' `static object_allocator<et_occ> et_occurrences`, il
 * pool da cui la foresta ET prende i nodi. Mai costruito, allocate()
 * restituiva NULL, e cc1 moriva con un fault a 0x00000004 dentro
 * et_splay() — cioe' `occ->parent` su un puntatore nullo. Il guasto
 * sembrava un difetto di GCC ed era il nostro codice di avvio.
 *
 * ⚠️ I SIMBOLI SONO `weak` PERCHE' POSSONO NON ESISTERE. Li definisce il
 * linker script, e i nostri (bin/<prog>/<prog>.ld) non lo fanno — non
 * serve, quei programmi non hanno costruttori. Un simbolo weak non
 * definito vale zero, i due estremi coincidono e il ciclo non gira
 * nemmeno una volta. Dichiararli forti farebbe fallire il link di ogni
 * programma di EX-OS.
 *
 * ⚠️ L'ORDINE E' QUELLO DELL'ARRAY, IN AVANTI. Per .init_array e'
 * l'ordine giusto; per .fini_array la specifica dice ALL'INDIETRO, e
 * distruggere nell'ordine di costruzione invece che al contrario
 * significa distruggere un oggetto mentre un altro, costruito dopo, lo
 * sta ancora usando.
 *
 * ⚠️ .preinit_array PRIMA DI TUTTO. Ci finiscono le funzioni che devono
 * girare prima di qualunque costruttore — le usa il codice di
 * strumentazione. Sono quasi sempre zero, e costano tre righe.
 * ============================================================================= */
/* ⚠️ QUESTO BLOCCO STA PRIMA DI __attribute__((weak, noreturn)), e non e'
 * indifferente: quell'attributo appartiene a _libc_start, ed e' scritto
 * sulla riga PRECEDENTE alla funzione. Infilando del codice in mezzo —
 * come ho fatto alla prima stesura — l'attributo si attacca alla prima
 * dichiarazione che trova (il typedef qui sotto, con tanto di avviso
 * «weak attribute ignored») e _libc_start perde sia weak sia noreturn,
 * in silenzio. */
typedef void (*FunzioneInit)(void);

extern FunzioneInit __preinit_array_start[] __attribute__((weak));
extern FunzioneInit __preinit_array_end[]   __attribute__((weak));
extern FunzioneInit __init_array_start[]    __attribute__((weak));
extern FunzioneInit __init_array_end[]      __attribute__((weak));
extern FunzioneInit __fini_array_start[]    __attribute__((weak));
extern FunzioneInit __fini_array_end[]      __attribute__((weak));

static void esegui_costruttori(void)
{
    FunzioneInit *p;

    for (p = __preinit_array_start; p != __preinit_array_end; p++)
        if (*p) (*p)();

    for (p = __init_array_start; p != __init_array_end; p++)
        if (*p) (*p)();
}

/* Chiamata da exit(). Vedi il commento sopra: all'indietro, non in
 * avanti. */
void _libc_distruttori(void)
{
    FunzioneInit *p;

    /* &a[0] e non a: confrontare due array direttamente e' un confronto
     * fra indirizzi che il compilatore segnala, perche' quasi sempre chi
     * lo scrive voleva confrontare il CONTENUTO. Qui gli indirizzi sono
     * proprio quello che serve. */
    if (&__fini_array_start[0] == &__fini_array_end[0]) return;

    for (p = __fini_array_end; p != __fini_array_start; ) {
        p--;
        if (*p) (*p)();
    }
}

__attribute__((weak, noreturn))
void _libc_start(int argc, char **argv, char **envp)
{
    /* L'ambiente del processo e' quello che il padre ha passato. Puo'
     * essere vuoto — succede al primo processo, che lo spawn lo fa il
     * kernel — e in quel caso getenv() ripiega sulla sezione [env] di
     * /boot/kernel.cfg: vedi getenv(). */
    environ = envp;

    /* ⚠️ DOPO environ E PRIMA DI main. Un costruttore globale puo'
     * chiamare getenv(); farlo girare prima che environ sia impostato
     * gli darebbe un ambiente vuoto invece di quello vero. */
    esegui_costruttori();

    exit(main(argc, argv));
    for (;;);
}

void sched_yield(void)
{
    _syscall1(SYS_SCHED_YIELD, 0);
}

/* ⚠️ Ritorna int e non void: e' quello che dice POSIX, e chi controlla il
 * valore non deve scoprire qui che non c'e'. Su EX-OS non fallisce mai —
 * non ci sono segnali che possano interromperla. */
int usleep(unsigned int us)
{
    /* Converti microsecondi in millisecondi (arrotondando) */
    uint32_t ms = (us + 999) / 1000;
    if (ms == 0) ms = 1;
    _syscall1(SYS_SLEEP, ms);
    return 0;
}

/* =============================================================================
 * ⚠️ RITORNA SEMPRE 0, ED E' LA RISPOSTA VERA
 *
 * POSIX dice che sleep() ritorna i secondi che RESTAVANO da dormire quando
 * un segnale l'ha interrotta. Su EX-OS non esistono segnali che possano
 * interrompere una dormita, quindi la dormita e' sempre completa e il
 * residuo e' sempre zero.
 *
 * La firma dev'essere quella giusta comunque, perche' il modo canonico di
 * usare questa funzione e' `while ((secs = sleep(secs))) {}` — che con un
 * ritorno void non compila nemmeno. E' cosi' che si e' scoperto: la
 * libstdc++ lo scrive in src/c++11/thread.cc.
 * ============================================================================= */
/* =============================================================================
 * nanosleep — la firma POSIX sopra un orologio che non ha i nanosecondi
 *
 * ⚠️ LA RISOLUZIONE VERA E' 10 ms, non un nanosecondo, e vale la pena
 * dirlo qui invece di lasciarlo scoprire a chi misura: sotto c'e'
 * SYS_SLEEP, che conta i tick del PIT a 100 Hz. Il nome della funzione
 * promette mille volte piu' di quello che la macchina puo' dare.
 *
 * ⚠️ SI ARROTONDA PER ECCESSO, e non e' un dettaglio: chi chiede di
 * dormire un microsecondo si aspetta di dormire ALMENO un microsecondo.
 * Arrotondando per difetto, una richiesta piu' corta di un tick
 * diventerebbe un ritorno immediato — cioe' un ciclo di attesa che non
 * aspetta, che e' il modo di trasformare una pausa in un consumo di CPU
 * al cento per cento.
 *
 * `rem` si azzera: qui non ci sono i segnali, quindi una dormita non puo'
 * essere interrotta e non resta mai niente da recuperare.
 * ============================================================================= */
/* =============================================================================
 * Identita' del processo: uid, gid — e perche' sono tutti zero
 *
 * EX-OS non ha utenti. Non c'e' un login, non c'e' un proprietario dei
 * file, non c'e' setuid: ogni processo ha gli stessi diritti di ogni
 * altro, e la separazione che conta e' quella fra spazi di
 * indirizzamento, non fra persone.
 *
 * ⚠️ ZERO NON VUOL DIRE «ROOT», VUOL DIRE «NON C'E' QUESTA DOMANDA». La
 * distinzione conta per l'unico uso serio che ne fa il codice di terzi:
 *
 *     OPENSSL_issetugid() = getuid() != geteuid() || getgid() != getegid()
 *
 * cioe' «sto girando con privilegi che non sono di chi mi ha lanciato?».
 * Su EX-OS la risposta e' NO, sempre e onestamente, perche' non esiste un
 * meccanismo che possa dare privilegi diversi. Restituendo lo stesso
 * valore da tutte e quattro, quella domanda riceve la risposta giusta —
 * non una finta.
 *
 * ⚠️ IL GIORNO CHE GLI UTENTI ARRIVASSERO, QUESTE VANNO RIFATTE PRIMA DI
 * TUTTO IL RESTO. Un sistema con i privilegi e una issetugid() che dice
 * sempre no e' un sistema che si fida delle variabili d'ambiente di
 * chiunque.
 * ============================================================================= */
int getuid(void)  { return 0; }
int geteuid(void) { return 0; }
int getgid(void)  { return 0; }
int getegid(void) { return 0; }

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    unsigned long long ms;

    if (req == NULL || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }

    ms  = (unsigned long long)req->tv_sec * 1000ULL;
    ms += ((unsigned long long)req->tv_nsec + 999999ULL) / 1000000ULL;

    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }

    while (ms > 0) {
        unsigned int fetta = (ms > 1000000ULL) ? 1000000u : (unsigned int)ms;

        _syscall1(SYS_SLEEP, fetta);
        ms -= fetta;
    }
    return 0;
}

unsigned int sleep(unsigned int sec)
{
    _syscall1(SYS_SLEEP, sec * 1000);
    return 0;
}

/* Millisecondi dall'avvio. Avanza a scatti di 10 (PIT a 100Hz) e torna a
 * zero dopo ~24,8 giorni: confrontare DIFFERENZE senza segno, mai valori
 * assoluti. Vedi SYS_UPTIME in kernel/syscall/syscall_impl.c. */
unsigned int uptime_ms(void)
{
    return (unsigned int)_syscall1(SYS_UPTIME, 0);
}

/* La sizeof viaggia con la chiamata: il kernel rifiuta se la sua copia
 * della struttura non ha la stessa dimensione. Vedi libc.h. */
int meminfo(MemInfo *mi)
{
    return _syscall2(SYS_MEMINFO, (uint32_t)mi, (uint32_t)sizeof(MemInfo));
}

int procinfo(ProcInfo *buf, unsigned int max, unsigned int start)
{
    return _syscall4(SYS_PROCINFO, (uint32_t)buf, max, start,
                     (uint32_t)sizeof(ProcInfo));
}

int diskinfo(unsigned int idx, DiskInfo *di)
{
    return _syscall3(SYS_DISKINFO, idx, (uint32_t)di, (uint32_t)sizeof(DiskInfo));
}

int blkinfo(BlkInfo *buf, unsigned int max, unsigned int start)
{
    return _syscall4(SYS_BLKINFO, (uint32_t)buf, max, start,
                     (uint32_t)sizeof(BlkInfo));
}

int mount(const char *dev, const char *punto, unsigned int flag)
{
    return (int)_syscall3(SYS_MOUNT, (uint32_t)dev, (uint32_t)punto, flag);
}

int umount(const char *punto)
{
    return (int)_syscall1(SYS_UMOUNT, (uint32_t)punto);
}

int mountinfo(MountInfo *buf, unsigned int max, unsigned int start)
{
    return _syscall4(SYS_MOUNTINFO, (uint32_t)buf, max, start,
                     (uint32_t)sizeof(MountInfo));
}

int bootinstall(const char *punto, BootInstallInfo *info)
{
    return (int)_syscall5(SYS_BOOTINSTALL, (uint32_t)punto, (uint32_t)info,
                          (uint32_t)sizeof(BootInstallInfo), 0, 0);
}

/* =============================================================================
 * bootverify — «questi due file sarebbero mappabili?»
 *
 * ⚠️ NON SCRIVE NIENTE: ne' l'MBR ne' il settore di avvio. Calcola la mappa
 * dei settori dei due file indicati e la riporta in `info`, esattamente
 * come farebbe bootinstall — ma il disco resta com'era.
 *
 * E' cio' che permette a `install` di chiedere PRIMA se il kernel appena
 * copiato sta in un solo tratto (su FAT) o in pochi (su ext2), mentre
 * quello vecchio e' ancora al suo posto e il sistema installato funziona
 * ancora. Senza, l'unico modo di saperlo era provarci dopo aver
 * cancellato.
 *
 * `nome_s2` e `nome_k` sono i NOMI SENZA DIRECTORY e in minuscolo — la
 * directory e' sempre /boot — oppure NULL per "stage2.bin" e "kernel.bin".
 * ============================================================================= */
int bootverify(const char *punto, const char *nome_s2, const char *nome_k,
               BootInstallInfo *info)
{
    /* I due nomi viaggiano come stringhe CONSECUTIVE in un solo buffer:
     * la syscall legge la prima, salta il terminatore e legge la seconda.
     * Un registro solo invece di due, e nessun numero di syscall nuovo. */
    char nomi[128];
    uint32_t i = 0, j;

    if (nome_s2 == NULL) nome_s2 = "stage2.bin";
    if (nome_k  == NULL) nome_k  = "kernel.bin";

    for (j = 0; nome_s2[j] && i < sizeof(nomi) - 2; j++) nomi[i++] = nome_s2[j];
    nomi[i++] = '\0';
    for (j = 0; nome_k[j] && i < sizeof(nomi) - 1; j++) nomi[i++] = nome_k[j];
    nomi[i] = '\0';

    return (int)_syscall5(SYS_BOOTINSTALL, (uint32_t)punto, (uint32_t)info,
                          (uint32_t)sizeof(BootInstallInfo),
                          1u /* BOOTINST_VERIFICA */, (uint32_t)(uintptr_t)nomi);
}

int partwrite(unsigned int disco, PartTabella *tab)
{
    return (int)_syscall3(SYS_PARTWRITE, disco, (uint32_t)tab,
                          (uint32_t)sizeof(PartTabella));
}

int blkread(const char *dev, unsigned int lba, unsigned int n, void *buf)
{
    return (int)_syscall4(SYS_BLKREAD, (uint32_t)dev, lba, n, (uint32_t)buf);
}

int blkwrite(const char *dev, unsigned int lba, unsigned int n, const void *buf)
{
    return (int)_syscall4(SYS_BLKWRITE, (uint32_t)dev, lba, n, (uint32_t)buf);
}

int truncate(const char *path, unsigned int size)
{
    return (int)err_reg(_syscall2(SYS_TRUNCATE, (uint32_t)path, size));
}

/* =============================================================================
 * IPC — wrapper userspace, vedi libc.h per la documentazione API.
 * ============================================================================= */
int ipc_send(unsigned int dest_pid, unsigned int tipo,
              const void *data, unsigned int len)
{
    return (int)_syscall4(SYS_IPC_SEND, dest_pid, tipo,
                           (uint32_t)data, len);
}

int ipc_recv(IpcMessage *out_meta, void *buf, unsigned int buf_len)
{
    return (int)_syscall3(SYS_IPC_RECV, (uint32_t)out_meta,
                           (uint32_t)buf, buf_len);
}

int ipc_recv_timeout(IpcMessage *out_meta, void *buf, unsigned int buf_len,
                     unsigned int timeout_ms)
{
    return (int)_syscall4(SYS_IPC_RECV_TMO, (uint32_t)out_meta,
                           (uint32_t)buf, buf_len, timeout_ms);
}

int time_now(RtcTime *t)
{
    return (int)_syscall1(SYS_TIME, (uint32_t)t);
}

/* =============================================================================
 * Data e ora nella forma del C standard
 *
 * time_now() da' i campi separati come li tiene l'orologio CMOS; time() e
 * localtime() danno la stessa informazione nella forma che si aspetta il
 * codice scritto per un sistema POSIX — un conteggio di secondi da
 * un'origine, e la struttura che lo rimette in pezzi.
 *
 * ⚠️ NON C'E' UN FUSO ORARIO. L'orologio CMOS di EX-OS e' ora locale, e il
 * sistema non sa in quale fuso si trova: percio' localtime() e gmtime()
 * fanno esattamente la stessa cosa, e i secondi ritornati da time() sono
 * "secondi dal 1970 letti su un orologio locale". Vanno benissimo per
 * misurare intervalli e per la data di un file; NON sono un istante
 * confrontabile con quello di un'altra macchina. Il giorno che EX-OS
 * imparera' i fusi, questo e' il punto in cui la differenza smettera' di
 * essere finta.
 *
 * L'algoritmo delle date e' quello dei "giorni civili" di Hinnant: niente
 * tabelle dei mesi, niente casi speciali per gli anni bisestili, e va
 * avanti e indietro con la stessa aritmetica. Il trucco e' spostare
 * l'inizio dell'anno a marzo, cosi' il 29 febbraio finisce IN FONDO
 * all'anno e smette di spostare tutti i mesi che lo seguono.
 * ============================================================================= */

/* Giorni fra il 1970-01-01 e la data data. */
static long giorni_da_civile(long anno, unsigned mese, unsigned giorno)
{
    anno -= (mese <= 2);

    const long     era = (anno >= 0 ? anno : anno - 399) / 400;
    const unsigned add = (unsigned)(anno - era * 400);            /* 0..399 */
    const unsigned gda = (153u * (mese + (mese > 2 ? -3u : 9u)) + 2u) / 5u
                         + giorno - 1u;                            /* 0..365 */
    const unsigned gde = add * 365u + add / 4u - add / 100u + gda; /* 0..146096 */

    return era * 146097L + (long)gde - 719468L;
}

/* L'inverso: dal numero di giorni alla data. */
static void civile_da_giorni(long g, long *anno, unsigned *mese, unsigned *giorno)
{
    g += 719468L;

    const long     era = (g >= 0 ? g : g - 146096L) / 146097L;
    const unsigned gde = (unsigned)(g - era * 146097L);            /* 0..146096 */
    const unsigned add = (gde - gde / 1460u + gde / 36524u - gde / 146096u) / 365u;
    const long     a   = (long)add + era * 400L;
    const unsigned gda = gde - (365u * add + add / 4u - add / 100u);
    const unsigned mp  = (5u * gda + 2u) / 153u;                   /* 0..11 */
    const unsigned d   = gda - (153u * mp + 2u) / 5u + 1u;         /* 1..31 */
    const unsigned m   = mp + (mp < 10u ? 3u : -9u);               /* 1..12 */

    *anno   = a + (m <= 2u);
    *mese   = m;
    *giorno = d;
}

time_t time(time_t *t)
{
    RtcTime r;
    time_t  secondi;

    if (time_now(&r) < 0) {
        /* L'orologio non risponde. (time_t)-1 e' il modo con cui il C
         * standard dice "non lo so": ritornare 0 significherebbe il 1970,
         * cioe' una data sbagliata spacciata per buona. */
        if (t) *t = (time_t)-1;
        return (time_t)-1;
    }

    secondi = (time_t)(giorni_da_civile((long)r.anno, r.mese, r.giorno) * 86400L
                       + (long)r.ora * 3600L + (long)r.minuto * 60L + (long)r.secondo);

    if (t) *t = secondi;
    return secondi;
}

/* Il risultato sta in una struttura statica, come vuole l'interfaccia del
 * C standard: due chiamate di seguito e la prima e' persa. */
static struct tm tm_statica;

struct tm *gmtime(const time_t *t)
{
    if (t == NULL) return NULL;

    long     secondi = (long)*t;
    long     giorni  = secondi / 86400L;
    long     resto   = secondi % 86400L;

    /* Prima del 1970 il resto sarebbe negativo e l'ora verrebbe assurda:
     * si prende in prestito un giorno, che e' cio' che fa la divisione
     * euclidea e non quella del C. */
    if (resto < 0) { resto += 86400L; giorni -= 1; }

    long     anno;
    unsigned mese, giorno;
    civile_da_giorni(giorni, &anno, &mese, &giorno);

    tm_statica.tm_sec   = (int)(resto % 60);
    tm_statica.tm_min   = (int)((resto / 60) % 60);
    tm_statica.tm_hour  = (int)(resto / 3600);
    tm_statica.tm_mday  = (int)giorno;
    tm_statica.tm_mon   = (int)mese - 1;          /* 0..11, come vuole tm */
    tm_statica.tm_year  = (int)(anno - 1900);     /* anni dal 1900 */
    /* Il 1970-01-01 era un giovedi', cioe' il giorno 4 della settimana che
     * comincia di domenica. Il resto negativo si corregge come sopra. */
    tm_statica.tm_wday  = (int)((giorni + 4) % 7);
    if (tm_statica.tm_wday < 0) tm_statica.tm_wday += 7;
    tm_statica.tm_yday  = (int)(giorni - giorni_da_civile(anno, 1, 1));
    tm_statica.tm_isdst = 0;                      /* nessun'ora legale: vedi sopra */

    return &tm_statica;
}

struct tm *localtime(const time_t *t)
{
    return gmtime(t);   /* nessun fuso orario: vedi il commento in testa */
}

/* =============================================================================
 * mktime — l'inversa di gmtime
 *
 * ⚠️ NORMALIZZA LA STRUTTURA CHE RICEVE, e non e' un effetto collaterale:
 * e' meta' del suo lavoro. Chi vuole «il primo del mese prossimo» scrive
 * `tm.tm_mon += 1` e chiama mktime, che accetta un mese 12 e lo trasforma
 * in gennaio dell'anno dopo. Da qui il fatto che il parametro NON e'
 * const: i campi tornano indietro corretti, tm_wday e tm_yday compresi —
 * quelli in ingresso vengono IGNORATI, come dice lo standard.
 *
 * ⚠️ NESSUN FUSO ORARIO: mktime interpreta i campi come UTC, perche' su
 * EX-OS localtime e gmtime sono la stessa funzione (il sistema non sa in
 * che fuso si trova). Su un sistema con i fusi questa e' la differenza fra
 * mktime e timegm, e qui non c'e'.
 * ============================================================================= */
time_t mktime(struct tm *tm)
{
    long anno, mese, giorni, secondi;

    if (tm == NULL) return (time_t)-1;

    /* I mesi si normalizzano per primi, perche' da quanti giorni ha un
     * mese dipende tutto il resto. La divisione dev'essere EUCLIDEA: con
     * quella del C, un tm_mon negativo darebbe un anno sbagliato. */
    anno = (long)tm->tm_year + 1900L;
    mese = (long)tm->tm_mon;
    anno += mese / 12L;
    mese %= 12L;
    if (mese < 0) { mese += 12L; anno -= 1L; }

    /* Giorno, ora, minuto e secondo possono essere fuori intervallo quanto
     * vogliono: si sommano tutti in secondi e ci pensa la conversione
     * inversa a rimetterli a posto. E' il motivo per cui `tm.tm_sec += 90`
     * e' un modo legittimo di dire "novanta secondi dopo". */
    giorni  = giorni_da_civile(anno, (unsigned)mese + 1u, 1u)
              + (long)tm->tm_mday - 1L;
    secondi = giorni * 86400L
              + (long)tm->tm_hour * 3600L
              + (long)tm->tm_min * 60L
              + (long)tm->tm_sec;

    /* La struttura torna indietro normalizzata: e' cio' per cui la si
     * passa non-const. */
    {
        time_t      quando = (time_t)secondi;
        struct tm  *rifatto = gmtime(&quando);
        if (rifatto != NULL && rifatto != tm) *tm = *rifatto;
        return quando;
    }
}

/* asctime e ctime — la data in venticinque caratteri e una riga a capo.
 *
 * "Sun Aug  2 17:04:05 2026\n", che e' la forma fissa dello standard: non
 * dipende dalla locale, non si puo' configurare, e la lunghezza e' sempre
 * la stessa. E' quella che stampa `ar tv` per la data di ogni membro.
 *
 * ⚠️ IL RISULTATO STA IN UN BUFFER STATICO, come vuole lo standard: la
 * chiamata successiva lo sovrascrive. Due date da stampare insieme vanno
 * copiate, o la seconda cancella la prima — ed e' un difetto che si vede
 * solo quando ce ne sono due. Vale anche per gmtime e localtime, che qui
 * sopra hanno la stessa nota. */
size_t strftime(char *buf, size_t max, const char *fmt, const struct tm *tm);

char *asctime(const struct tm *tm)
{
    static char buf[32];

    if (tm == NULL) return NULL;
    strftime(buf, sizeof(buf), "%c\n", tm);
    return buf;
}

char *ctime(const time_t *t)
{
    struct tm *tm = localtime(t);
    return (tm == NULL) ? NULL : asctime(tm);
}

/* ⚠️ utime NON CAMBIA NIENTE, come chmod e umask: nessun filesystem di
 * EX-OS ha una syscall per riscrivere le date di un file. Ritorna 0 se il
 * file c'e', perche' chi la chiama — `objcopy` e `strip`, per conservare
 * la data dell'originale — stampa un avviso a ogni fallimento, e un
 * avviso per file su un'operazione che non e' andata storta e' rumore.
 * Il file conserva la data della SCRITTURA, che e' quella vera. */
int utime(const char *path, const void *tempi)
{
    struct stat st;

    (void)tempi;
    if (stat(path, &st) != 0) { errno = ENOENT; return -1; }
    return 0;
}

/* =============================================================================
 * strftime — la data scritta come chiede il chiamante
 *
 * Non e' completa e lo dice: ci sono le conversioni che il codice reale
 * usa davvero (%Y %m %d %H %M %S %y %j %a %A %b %B %p %e %n %t %z %Z %%
 * piu' le composte %F %T %D %R %c %x %X). Quelle mancanti — la settimana
 * ISO, le varianti E e O della locale — si copiano NELLA loro forma
 * letterale invece di sparire, cosi' chi legge l'uscita vede che manca
 * qualcosa invece di trovare un buco silenzioso.
 *
 * ⚠️ %Z e %z DICONO SEMPRE UTC E +0000, e non e' una scorciatoia: EX-OS
 * non ha fusi orari, localtime() e' gmtime() (vedi qui sopra), e l'ora che
 * si stampa e' quella dell'orologio CMOS presa per buona. Scrivere il
 * nome di un fuso qualunque sarebbe l'unico modo di sbagliare davvero.
 *
 * I nomi dei giorni e dei mesi sono in inglese perche' e' quello che dice
 * la locale "C", l'unica che c'e' (vedi setlocale piu' sopra): un listato
 * di gas con i mesi in italiano sarebbe un file che nessun altro
 * strumento sa rileggere.
 * ============================================================================= */
static const char *g_giorni[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *g_mesi[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

/* Accoda cio' che ci sta e tiene il conto di quanto e' stato scritto.
 * `*n` puo' superare `max`: e' il modo di sapere alla fine che non ci
 * stava, che e' quello che strftime deve riportare (zero). */
static void sf_str(char *buf, size_t max, size_t *n, const char *s)
{
    while (*s) {
        if (*n + 1 < max) buf[*n] = *s;
        (*n)++;
        s++;
    }
}

static void sf_num(char *buf, size_t max, size_t *n, long v, int cifre, char riempi)
{
    char tmp[16];
    int  i = 0, neg = 0;

    if (v < 0) { neg = 1; v = -v; }
    do { tmp[i++] = (char)('0' + (v % 10)); v /= 10; } while (v > 0 && i < 15);
    while (i < cifre) tmp[i++] = riempi;
    if (neg && i < 15) tmp[i++] = '-';

    while (i > 0) {
        i--;
        if (*n + 1 < max) buf[*n] = tmp[i];
        (*n)++;
    }
}

size_t strftime(char *buf, size_t max, const char *fmt, const struct tm *tm)
{
    size_t n = 0;

    if (buf == NULL || fmt == NULL || tm == NULL || max == 0) return 0;

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            if (n + 1 < max) buf[n] = *fmt;
            n++;
            continue;
        }

        fmt++;
        if (*fmt == '\0') break;

        switch (*fmt) {
            case 'Y': sf_num(buf, max, &n, tm->tm_year + 1900, 4, '0'); break;
            case 'y': sf_num(buf, max, &n, (tm->tm_year + 1900) % 100, 2, '0'); break;
            case 'm': sf_num(buf, max, &n, tm->tm_mon + 1, 2, '0'); break;
            case 'd': sf_num(buf, max, &n, tm->tm_mday, 2, '0'); break;
            case 'e': sf_num(buf, max, &n, tm->tm_mday, 2, ' '); break;
            case 'H': sf_num(buf, max, &n, tm->tm_hour, 2, '0'); break;
            case 'M': sf_num(buf, max, &n, tm->tm_min, 2, '0'); break;
            case 'S': sf_num(buf, max, &n, tm->tm_sec, 2, '0'); break;
            case 'j': sf_num(buf, max, &n, tm->tm_yday + 1, 3, '0'); break;

            case 'I': {
                int h = tm->tm_hour % 12;
                sf_num(buf, max, &n, (h == 0) ? 12 : h, 2, '0');
                break;
            }
            case 'p': sf_str(buf, max, &n, (tm->tm_hour < 12) ? "AM" : "PM"); break;

            case 'a':
                if (tm->tm_wday >= 0 && tm->tm_wday < 7) {
                    char corto[4];
                    corto[0] = g_giorni[tm->tm_wday][0];
                    corto[1] = g_giorni[tm->tm_wday][1];
                    corto[2] = g_giorni[tm->tm_wday][2];
                    corto[3] = '\0';
                    sf_str(buf, max, &n, corto);
                }
                break;
            case 'A':
                if (tm->tm_wday >= 0 && tm->tm_wday < 7)
                    sf_str(buf, max, &n, g_giorni[tm->tm_wday]);
                break;
            case 'b':
            case 'h':
                if (tm->tm_mon >= 0 && tm->tm_mon < 12) {
                    char corto[4];
                    corto[0] = g_mesi[tm->tm_mon][0];
                    corto[1] = g_mesi[tm->tm_mon][1];
                    corto[2] = g_mesi[tm->tm_mon][2];
                    corto[3] = '\0';
                    sf_str(buf, max, &n, corto);
                }
                break;
            case 'B':
                if (tm->tm_mon >= 0 && tm->tm_mon < 12)
                    sf_str(buf, max, &n, g_mesi[tm->tm_mon]);
                break;

            /* Le composte, scritte in termini delle semplici. */
            case 'F':
                sf_num(buf, max, &n, tm->tm_year + 1900, 4, '0');
                sf_str(buf, max, &n, "-");
                sf_num(buf, max, &n, tm->tm_mon + 1, 2, '0');
                sf_str(buf, max, &n, "-");
                sf_num(buf, max, &n, tm->tm_mday, 2, '0');
                break;
            case 'D':
            case 'x':
                sf_num(buf, max, &n, tm->tm_mon + 1, 2, '0');
                sf_str(buf, max, &n, "/");
                sf_num(buf, max, &n, tm->tm_mday, 2, '0');
                sf_str(buf, max, &n, "/");
                sf_num(buf, max, &n, (tm->tm_year + 1900) % 100, 2, '0');
                break;
            case 'T':
            case 'X':
                sf_num(buf, max, &n, tm->tm_hour, 2, '0');
                sf_str(buf, max, &n, ":");
                sf_num(buf, max, &n, tm->tm_min, 2, '0');
                sf_str(buf, max, &n, ":");
                sf_num(buf, max, &n, tm->tm_sec, 2, '0');
                break;
            case 'R':
                sf_num(buf, max, &n, tm->tm_hour, 2, '0');
                sf_str(buf, max, &n, ":");
                sf_num(buf, max, &n, tm->tm_min, 2, '0');
                break;
            case 'c':
                /* "Sun Aug  2 17:30:00 2026", la forma della locale "C". */
                if (tm->tm_wday >= 0 && tm->tm_wday < 7) {
                    char corto[4];
                    corto[0] = g_giorni[tm->tm_wday][0];
                    corto[1] = g_giorni[tm->tm_wday][1];
                    corto[2] = g_giorni[tm->tm_wday][2];
                    corto[3] = '\0';
                    sf_str(buf, max, &n, corto);
                    sf_str(buf, max, &n, " ");
                }
                if (tm->tm_mon >= 0 && tm->tm_mon < 12) {
                    char corto[4];
                    corto[0] = g_mesi[tm->tm_mon][0];
                    corto[1] = g_mesi[tm->tm_mon][1];
                    corto[2] = g_mesi[tm->tm_mon][2];
                    corto[3] = '\0';
                    sf_str(buf, max, &n, corto);
                    sf_str(buf, max, &n, " ");
                }
                sf_num(buf, max, &n, tm->tm_mday, 2, ' ');
                sf_str(buf, max, &n, " ");
                sf_num(buf, max, &n, tm->tm_hour, 2, '0');
                sf_str(buf, max, &n, ":");
                sf_num(buf, max, &n, tm->tm_min, 2, '0');
                sf_str(buf, max, &n, ":");
                sf_num(buf, max, &n, tm->tm_sec, 2, '0');
                sf_str(buf, max, &n, " ");
                sf_num(buf, max, &n, tm->tm_year + 1900, 4, '0');
                break;

            /* Vedi il ⚠️ in testa: non c'e' nessun fuso da riportare. */
            case 'z': sf_str(buf, max, &n, "+0000"); break;
            case 'Z': sf_str(buf, max, &n, "UTC");   break;

            case 'n': sf_str(buf, max, &n, "\n"); break;
            case 't': sf_str(buf, max, &n, "\t"); break;
            case '%': sf_str(buf, max, &n, "%");  break;

            /* Sconosciuta: si ricopia com'era. Vedi il commento in testa —
             * un %V che sparisce e' un difetto che si scopre confrontando
             * due uscite; un %V che resta scritto si scopre subito. */
            default:
                if (n + 1 < max) buf[n] = '%';
                n++;
                if (n + 1 < max) buf[n] = *fmt;
                n++;
                break;
        }
    }

    if (n >= max) { buf[max - 1] = '\0'; return 0; }
    buf[n] = '\0';
    return n;
}

/* =============================================================================
 * ⛔ SECONDI E MICROSECONDI DALLA STESSA SORGENTE, e prima no
 *
 * La prima versione prendeva `tv_sec` da time() — cioe' dall'orologio
 * CMOS — e `tv_usec` da uptime_ms() % 1000, cioe' dal contatore dei tick
 * del PIT. Sono due orologi INDIPENDENTI, che non avanzano insieme: la
 * coppia poteva TORNARE INDIETRO ogni volta che i millisecondi si
 * avvolgevano prima che il secondo del CMOS scattasse.
 *
 * Un orologio che torna indietro non da' un errore: da' intervalli
 * NEGATIVI a chi sottrae due istanti. GCC li sottrae, e il rapporto dei
 * tempi di cc1 usciva cosi':
 *
 *     phase setup : 18446744070.44 (100%)
 *
 * che e' 2^64 nanosecondi meno tre secondi, cioe' un -3.3 letto come
 * senza segno.
 *
 * ⚠️ ORA IL TEMPO SCORRE TUTTO DA uptime_ms(), ancorato UNA VOLTA alla
 * lettura iniziale del CMOS. La conseguenza dichiarata: se qualcuno
 * corregge l'orologio di sistema mentre un programma gira, gettimeofday
 * non se ne accorge. E' il prezzo giusto — un orologio che non torna mai
 * indietro vale piu' di uno che insegue l'ora esatta a scatti.
 * ============================================================================= */
static long         g_tod_base_sec = 0;
static unsigned int g_tod_base_ms  = 0;
static int          g_tod_pronto   = 0;

int gettimeofday(struct timeval *tv, void *fuso)
{
    unsigned int ora_ms, trascorsi;

    (void)fuso;         /* obsoleto anche su POSIX */
    if (tv == NULL) return -1;

    ora_ms = uptime_ms();

    if (!g_tod_pronto) {
        g_tod_base_sec = (long)time(NULL);
        g_tod_base_ms  = ora_ms;
        g_tod_pronto   = 1;
    }

    trascorsi = ora_ms - g_tod_base_ms;   /* corretto anche all'avvolgimento */

    tv->tv_sec  = g_tod_base_sec + (long)(trascorsi / 1000u);
    /* ⚠️ La risoluzione vera resta 10 ms: il PIT batte a 100 Hz e le
     * ultime quattro cifre dei microsecondi sono sempre zero. Meglio di
     * zero secco — chi misura un intervallo breve vede almeno qualcosa
     * muoversi — ma non e' un orologio ad alta risoluzione, e EX-OS non
     * ne ha uno. */
    tv->tv_usec = (long)((trascorsi % 1000u) * 1000u);
    return 0;
}

/* La differenza fra due istanti. Su EX-OS time_t e' un intero di secondi e
 * la sottrazione basterebbe: c'e' perche' lo standard non garantisce che
 * time_t sia aritmetico, e chi scrive difftime scrive codice che vale
 * anche altrove. */
double difftime(time_t fine, time_t inizio)
{
    return (double)fine - (double)inizio;
}

/* ⚠️ RITORNA `base` SE HA FUNZIONATO E 0 SE NO — non e' la convenzione
 * 0/-1 di tutto il resto, e' quella che lo standard da' a questa
 * funzione. Confonderle significa leggere "riuscito" quando l'orologio non
 * ha risposto.
 *
 * ⚠️ La risoluzione vera resta 10 ms, il tick del PIT: tv_nsec e' sempre
 * un multiplo di 10 000 000. La struttura ha i nanosecondi perche' cosi'
 * e' fatta, non perche' li sappiamo misurare. */
int timespec_get(struct timespec *ts, int base)
{
    time_t adesso;

    if (ts == NULL || base != TIME_UTC) return 0;

    adesso = time(NULL);
    if (adesso == (time_t)-1) return 0;     /* l'orologio non risponde */

    ts->tv_sec  = (long)adesso;
    ts->tv_nsec = (long)((uptime_ms() % 1000u) * 1000000u);
    return base;
}

int console_switch(unsigned int n)
{
    return (int)_syscall1(SYS_CONSOLE_SWITCH, n);
}

int console_write(unsigned int n, const void *buf, unsigned int len)
{
    return (int)_syscall3(SYS_CONSOLE_WRITE, n, (uint32_t)buf, len);
}

int console_info(ConsoleInfo *ci)
{
    return (int)_syscall1(SYS_CONSOLE_INFO, (uint32_t)ci);
}

int ipc_register(const char *name)
{
    return (int)_syscall1(SYS_IPC_REGISTER, (uint32_t)name);
}

int ipc_lookup(const char *name)
{
    return (int)_syscall1(SYS_IPC_LOOKUP, (uint32_t)name);
}

/* =============================================================================
 * Hardware kernel-mediato — wrapper userspace per driver ring3
 * ============================================================================= */
int irq_bind(unsigned int irq)
{
    return (int)_syscall1(SYS_IRQ_BIND, irq);
}

int ioport_bind(unsigned int base, unsigned int count)
{
    return (int)_syscall2(SYS_IOPORT_BIND, base, count);
}

int ioport_in(unsigned int port)
{
    return (int)_syscall1(SYS_IOPORT_IN, port);
}

int ioport_out(unsigned int port, unsigned int value)
{
    return (int)_syscall2(SYS_IOPORT_OUT, port, value);
}

/* Accessi a 16 e 32 bit. Il perche' servano (bus PCI, porta dati NE2000)
 * e perche' ioport_in32 passi il valore da un puntatore invece che dal
 * ritorno stanno in libc.h. */
int ioport_in16(unsigned int port)
{
    return (int)_syscall1(SYS_IOPORT_IN16, port);
}

int ioport_out16(unsigned int port, unsigned int value)
{
    return (int)_syscall2(SYS_IOPORT_OUT16, port, value);
}

int ioport_in32(unsigned int port, unsigned int *out)
{
    return (int)_syscall2(SYS_IOPORT_IN32, port, (unsigned int)out);
}

int ioport_out32(unsigned int port, unsigned int value)
{
    return (int)_syscall2(SYS_IOPORT_OUT32, port, value);
}

int irq_done(unsigned int irq)
{
    return (int)_syscall1(SYS_IRQ_DONE, irq);
}

int dma_alloc(DmaZona *z)
{
    return (int)_syscall1(SYS_DMA_ALLOC, (unsigned int)z);
}

/* =============================================================================
 * Byte imprevedibili — vedi kernel/arch/x86/entropia.c per il perche'
 *
 * ⚠️ getentropy() E' IL NOME CHE CONTA, e non e' una scelta estetica:
 * OpenSSL lo cerca come simbolo DEBOLE prima di qualunque altra cosa
 * (providers/implementations/rands/seeding/rand_unix.c). Fornirlo con
 * questa firma esatta e' quello che permette di configurare OpenSSL per
 * EX-OS senza toccarne una riga.
 *
 * ⚠️ RITORNA 0 O -1, NON IL NUMERO DI BYTE. E' la firma di OpenBSD, che
 * e' quella che i chiamanti si aspettano: getentropy() o riempie TUTTO il
 * buffer o fallisce. Un riempimento parziale silenzioso qui sarebbe una
 * chiave meta' prevedibile.
 * ============================================================================= */
int getentropy(void *buf, size_t len)
{
    unsigned char *p = (unsigned char *)buf;
    size_t         fatti = 0;

    /* Il limite di OpenBSD, e vale la pena tenerlo: chi chiede piu' di
     * 256 byte di entropia vera sta usando l'attrezzo sbagliato — quello
     * che gli serve e' un DRBG seminato con questi. */
    if (len > 256) { errno = EIO; return -1; }

    while (fatti < len) {
        int n = (int)_syscall2(SYS_RANDOM, (unsigned int)(p + fatti),
                               (unsigned int)(len - fatti));

        if (n <= 0) {
            errno = (n == -EAGAIN) ? EAGAIN : EIO;
            return -1;
        }
        fatti += (size_t)n;
    }
    return 0;
}

/* La forma Linux: ritorna quanti byte ha scritto. `flags` si ignora —
 * qui non esiste la distinzione fra /dev/random e /dev/urandom, perche'
 * non esiste un generatore che continui a macinare: c'e' un serbatoio,
 * e quando e' vuoto lo si dice. */
ssize_t getrandom(void *buf, size_t len, unsigned int flags)
{
    int n;

    (void)flags;
    n = (int)_syscall2(SYS_RANDOM, (unsigned int)buf, (unsigned int)len);
    if (n < 0) { errno = -n; return -1; }
    return (ssize_t)n;
}

/* =============================================================================
 * Configurazione e identita' del sistema — vedi libc.h per il contratto
 * ============================================================================= */

/* ⚠️ IL SECONDO ARGOMENTO SI IGNORA, ed e' li' per la firma di POSIX.
 * EX-OS non ha permessi (vedi chmod piu' sopra), quindi non c'e' niente da
 * applicare. Fino ad agosto 2026 la firma era a UN argomento — piu'
 * onesta e incompatibile: `mkdir(nome, 0755)` non compilava, ed e' cio'
 * che scrive ogni programma portato da un Unix (bucomm.c di binutils fra
 * i primi). I due chiamanti interni sono stati aggiornati. */
int mkdir(const char *path, mode_t modo)
{
    (void)modo;
    return (int)err_reg(_syscall1(SYS_MKDIR, (uint32_t)path));
}

int rmdir(const char *path)
{
    return (int)err_reg(_syscall1(SYS_RMDIR, (uint32_t)path));
}

int unlink(const char *path)
{
    return (int)err_reg(_syscall1(SYS_UNLINK, (uint32_t)path));
}

/* remove() e' unlink() con il nome che usa il C standard. Sui sistemi
 * veri i due differiscono sulle directory (remove chiama rmdir); qui la
 * syscall di cancellazione e' una sola, e chi cancella una directory
 * riceve l'errore che il VFS ritiene giusto. */
int remove(const char *path)
{
    return unlink(path);
}

/* =============================================================================
 * execv / execvp — sostituire il proprio programma con un altro
 *
 * SYS_EXEC prende (percorso, argv, envp) e rimpiazza il processo che la
 * chiama: e' l'exec di POSIX, non una spawn. Se va a buon fine NON
 * RITORNA, e chi la chiama deve trattare ogni ritorno come un errore —
 * il valore negativo dice quale.
 *
 * envp e' sempre NULL: l'ambiente di EX-OS non e' un vettore sullo stack
 * ma la sezione [env] di /boot/kernel.cfg, che il nuovo programma legge
 * da se' con getconf(). Passare un vettore che il kernel ignorerebbe
 * darebbe l'impressione sbagliata che l'ambiente si erediti.
 * ============================================================================= */
int execv(const char *path, char *const argv[])
{
    return err_reg(_syscall3(SYS_EXEC, (uint32_t)path, (uint32_t)argv, 0));
}

int execvp(const char *file, char *const argv[])
{
    /* La 'p' sta per "cerca nel percorso di ricerca". Un nome che contiene
     * una barra non e' un nome da cercare: e' gia' un percorso, e cercarlo
     * altrove sarebbe sbagliato oltre che inutile. */
    if (file == NULL || *file == '\0') return -2;    /* -ENOENT */
    if (strchr(file, '/') != NULL) return execv(file, argv);

    static const char *dirs[] = { "/bin/", "/usr/bin/", NULL };
    char percorso[256];

    for (int i = 0; dirs[i]; i++) {
        size_t dl = strlen(dirs[i]), fl = strlen(file);
        if (dl + fl + 1 > sizeof(percorso)) continue;
        memcpy(percorso, dirs[i], dl);
        memcpy(percorso + dl, file, fl + 1);

        /* Se il file non c'e' si passa al prossimo; se c'e' ed exec
         * fallisce lo stesso, l'errore e' quello vero e va riportato. */
        int fd = open(percorso, O_RDONLY);
        if (fd < 0) continue;
        close(fd);
        return execv(percorso, argv);
    }
    return -2;      /* -ENOENT: nessuna directory di ricerca lo contiene */
}

int getconf(const char *key, char *buf, size_t size)
{
    return (int)_syscall3(SYS_GETENV, (uint32_t)key, (uint32_t)buf,
                          (uint32_t)size);
}

/* =============================================================================
 * getenv — la facciata POSIX su getconf()
 *
 * Le "variabili d'ambiente" di EX-OS sono la sezione [env] di
 * /boot/kernel.cfg, lette dal kernel e chieste con una syscall: non c'e'
 * un vettore `environ` in fondo allo stack del processo, come su Unix.
 * getconf() e' l'interfaccia onesta di quel meccanismo (scrive in un
 * buffer di chi chiama, dice quanto ha scritto) ed e' quella da preferire
 * nel codice nuovo — il commento sopra la sua dichiarazione lo diceva gia'
 * quando getenv() non esisteva.
 *
 * getenv() esiste perche' il codice di terzi la chiama e basta.
 *
 * ⚠️ IL PUNTATORE RITORNATO VALE FINO ALLA CHIAMATA SUCCESSIVA. Su Unix
 * punta dentro l'ambiente del processo e resta valido; qui punta a un
 * buffer statico riusato, perche' l'alternativa — allocare — vorrebbe dire
 * una perdita di memoria a ogni chiamata, dato che nessuno libera cio' che
 * getenv ritorna. Chi deve conservare il valore ne fa una strdup.
 * ============================================================================= */
static char getenv_buf[128];

/* =============================================================================
 * L'AMBIENTE DEL PROCESSO
 *
 * `environ` arriva dal padre attraverso sys_spawn (le stringhe stanno
 * sullo stack del figlio) e da li' in poi e' roba del processo: putenv() e
 * setenv() lavorano su una copia in heap, perche' la tabella iniziale sta
 * sullo stack e non si puo' allungare.
 *
 * ⚠️ getenv() RIPIEGA su /boot/kernel.cfg per le chiavi che non trova.
 * Non e' una comodita': e' cio' che tiene in piedi il comportamento di
 * prima, quando l'ambiente non esisteva e PATH/HOME/TERM venivano dalla
 * configurazione del kernel. Il primo processo (la shell, che la lancia il
 * kernel) non ha un padre da cui ereditare, e senza il ripiego si
 * troverebbe senza PATH.
 *
 * L'ordine e' quello giusto: quello che il padre ha passato VINCE sulla
 * configurazione di sistema, se no un figlio non potrebbe mai cambiare
 * niente al proprio ambiente — che e' esattamente cio' che serve a un
 * driver di compilatore per dire a cc1 dove sono gli header.
 * ============================================================================= */
char **environ = NULL;

/* Copia di environ in heap: nasce alla prima modifica. */
static char **g_env_mio  = NULL;
static int    g_env_n    = 0;    /* voci usate, escluso il NULL finale */
static int    g_env_max  = 0;

static int env_lunghezza_nome(const char *voce)
{
    int i = 0;
    while (voce[i] && voce[i] != '=') i++;
    return i;
}

/* Vero se `voce` e' della forma "nome=..." per questo nome. */
static int env_combacia(const char *voce, const char *nome, int len_nome)
{
    int i;
    if (env_lunghezza_nome(voce) != len_nome) return 0;
    for (i = 0; i < len_nome; i++)
        if (voce[i] != nome[i]) return 0;
    return 1;
}

/* Porta l'ambiente in heap, una volta sola. Ritorna 0, o -1 se manca
 * memoria — nel qual caso l'ambiente resta quello di partenza, in sola
 * lettura, che e' meglio di un ambiente a meta'. */
static int env_prendi_possesso(void)
{
    int n = 0, i;

    if (g_env_mio != NULL) return 0;

    if (environ != NULL)
        while (environ[n] != NULL) n++;

    g_env_max = n + 8;
    g_env_mio = (char **)malloc((size_t)(g_env_max + 1) * sizeof(char *));
    if (g_env_mio == NULL) { g_env_max = 0; return -1; }

    for (i = 0; i < n; i++) {
        g_env_mio[i] = strdup(environ[i]);
        if (g_env_mio[i] == NULL) { g_env_n = i; g_env_mio[i] = NULL;
                                    environ = g_env_mio; return -1; }
    }
    g_env_mio[n] = NULL;
    g_env_n      = n;
    environ      = g_env_mio;
    return 0;
}

char *getenv(const char *nome)
{
    int len;

    if (nome == NULL || *nome == '\0') return NULL;

    len = (int)strlen(nome);
    if (environ != NULL) {
        int i;
        for (i = 0; environ[i] != NULL; i++)
            if (env_combacia(environ[i], nome, len))
                return environ[i] + len + 1;
    }

    /* Ripiego sulla configurazione del kernel: vedi sopra. */
    if (getconf(nome, getenv_buf, sizeof(getenv_buf)) < 0) return NULL;
    return getenv_buf;
}

/* La voce passata a putenv() diventa parte dell'ambiente COM'E', senza
 * copia: e' il contratto di POSIX, e chi passa un buffer che poi
 * riutilizza si trova l'ambiente cambiato sotto. setenv() invece copia. */
int putenv(char *voce)
{
    int len, i;

    if (voce == NULL) return -1;
    len = env_lunghezza_nome(voce);
    if (len == 0 || voce[len] != '=') return -1;
    if (env_prendi_possesso() != 0) return -1;

    for (i = 0; i < g_env_n; i++) {
        if (env_combacia(g_env_mio[i], voce, len)) {
            g_env_mio[i] = voce;
            return 0;
        }
    }

    if (g_env_n + 1 >= g_env_max) {
        int    nuovo_max = g_env_max * 2 + 8;
        char **nuovo = (char **)realloc(g_env_mio,
                            (size_t)(nuovo_max + 1) * sizeof(char *));
        if (nuovo == NULL) return -1;
        g_env_mio = nuovo;
        g_env_max = nuovo_max;
        environ   = g_env_mio;
    }

    g_env_mio[g_env_n++] = voce;
    g_env_mio[g_env_n]   = NULL;
    return 0;
}

int setenv(const char *nome, const char *valore, int sovrascrivi)
{
    char *voce;
    size_t ln, lv;

    if (nome == NULL || *nome == '\0' || valore == NULL) return -1;
    if (env_lunghezza_nome(nome) != (int)strlen(nome)) return -1;  /* '=' nel nome */

    if (!sovrascrivi && getenv(nome) != NULL) return 0;

    ln = strlen(nome);
    lv = strlen(valore);
    voce = (char *)malloc(ln + lv + 2);
    if (voce == NULL) return -1;

    memcpy(voce, nome, ln);
    voce[ln] = '=';
    memcpy(voce + ln + 1, valore, lv);
    voce[ln + lv + 1] = '\0';

    return putenv(voce);
}

int unsetenv(const char *nome)
{
    int len, i;

    if (nome == NULL || *nome == '\0') return -1;
    if (env_prendi_possesso() != 0) return -1;

    len = (int)strlen(nome);
    for (i = 0; i < g_env_n; i++) {
        if (env_combacia(g_env_mio[i], nome, len)) {
            int j;
            for (j = i; j < g_env_n; j++) g_env_mio[j] = g_env_mio[j + 1];
            g_env_n--;
            return 0;
        }
    }
    return 0;
}

int osversion(char *buf, size_t size)
{
    return (int)_syscall2(SYS_VERSION, (uint32_t)buf, (uint32_t)size);
}

int verboseboot(void)
{
    char val[8];

    /* Default "parla": se la chiave manca o la lettura fallisce, meglio
     * un programma rumoroso di uno che tace per un errore. */
    if (getconf("verboseboot", val, sizeof(val)) < 0) return 1;
    return val[0] != '0';
}

/* =============================================================================
 * POSIZIONAMENTO E INFORMAZIONI SUI FILE
 *
 * Le syscall c'erano gia' (SYS_LSEEK 19, SYS_STAT 106) ma la libc non le
 * esponeva, e fino ad agosto 2026 il kernel rispondeva ENOSYS a entrambe
 * nei casi che contano: SEEK_END non sapeva quanto fosse lungo il file e
 * stat() non era mai stata scritta. Senza quelle due, nessun FILE* puo'
 * offrire ftell()/fseek() sulla fine — cioe' il modo con cui ogni
 * programma misura un file prima di leggerlo.
 * ============================================================================= */

/* sbrk — sposta la cima dell'heap e ritorna la posizione VECCHIA.
 *
 * L'allocatore la usa gia' internamente; esporla serve a chi porta una
 * libreria scritta per un altro sistema (newlib e picolibc chiedono
 * esattamente questa funzione, e nient'altro, per far funzionare il
 * proprio malloc) e a chi vuole misurare quanto heap sta consumando. */
void *sbrk(int incr)
{
    int32_t r = _syscall1(SYS_SBRK, (uint32_t)incr);
    if (r <= 0) { errno = (r < 0) ? -r : 12 /* ENOMEM */; return (void *)-1; }
    return (void *)(uintptr_t)r;
}

long lseek(int fd, long offset, int whence)
{
    int32_t r = _syscall3(SYS_LSEEK, (uint32_t)fd, (uint32_t)offset,
                          (uint32_t)whence);
    if (r < 0) { errno = -r; return -1; }
    return (long)r;
}

int statraw(const char *path, Stat *st)
{
    int32_t r = _syscall2(SYS_STAT, (uint32_t)path, (uint32_t)st);
    if (r < 0) errno = -r;
    return (int)r;
}

/* =============================================================================
 * stat / fstat nella forma POSIX
 *
 * Il filesystem risponde con gli attributi di FAT; questa conversione li
 * mette nella forma che si aspetta il codice di terzi. Vedi struct stat in
 * lib/include/libc.h per il perche' i due tipi convivono invece di
 * sostituirsi.
 *
 * LA DATA. FAT impacchetta data e ora in due parole a 16 bit, con l'anno
 * contato dal 1980 e i secondi divisi per due (un bit non bastava per 60
 * valori, e cosi' ne bastano cinque). E' il formato del 1980 e si decodifica
 * con degli spostamenti; l'unica cosa da non dimenticare e' il fattore due
 * sui secondi, che altrimenti fa un orario plausibile e sbagliato.
 * ============================================================================= */
static void stat_da_grezzo(const Stat *g, struct stat *st)
{
    unsigned anno   = 1980u + ((unsigned)(g->st_date >> 9) & 0x7Fu);
    unsigned mese   = ((unsigned)(g->st_date >> 5) & 0x0Fu);
    unsigned giorno = ((unsigned)g->st_date & 0x1Fu);
    unsigned ora    = ((unsigned)(g->st_time >> 11) & 0x1Fu);
    unsigned minuto = ((unsigned)(g->st_time >> 5) & 0x3Fu);
    unsigned sec    = ((unsigned)g->st_time & 0x1Fu) * 2u;

    st->st_dev   = 0;
    st->st_ino   = g->st_first_clus;
    st->st_nlink = 1;
    st->st_uid   = 0;
    st->st_gid   = 0;
    st->st_size  = (off_t)g->st_size;
    /* ⚠️ 512 e non 4096: e' il SETTORE, che e' l'unita' vera di tutti i
     * filesystem di EX-OS. Chi dimensiona un buffer su st_blksize deve
     * ricevere un numero che corrisponde a come si legge davvero. */
    st->st_blksize = 512;
    st->st_blocks  = (blkcnt_t)((g->st_size + 511u) / 512u);

    if (EXOS_ATTR_DIR(g->st_attr))          st->st_mode = S_IFDIR | 0755u;
    else if (EXOS_ATTR_RDONLY(g->st_attr))  st->st_mode = S_IFREG | 0555u;
    else                                    st->st_mode = S_IFREG | 0644u;

    /* Una data a zero significa "il filesystem non la tiene" (ISO 9660 e i
     * montaggi che non la riportano): meglio zero che il 1980. */
    if (g->st_date == 0) {
        st->st_mtime = 0;
    } else {
        if (mese   < 1u || mese   > 12u) mese   = 1u;
        if (giorno < 1u || giorno > 31u) giorno = 1u;
        st->st_mtime = (time_t)(giorni_da_civile((long)anno, mese, giorno) * 86400L
                                + (long)ora * 3600L + (long)minuto * 60L + (long)sec);
    }
    st->st_atime = st->st_mtime;
    st->st_ctime = st->st_mtime;
}

int stat(const char *path, struct stat *st)
{
    Stat g;
    int  r;

    if (st == NULL) { errno = 14; return -14; }   /* EFAULT */

    r = statraw(path, &g);
    if (r < 0) return r;

    stat_da_grezzo(&g, st);
    return 0;
}

/* lstat differisce da stat solo sui collegamenti simbolici, che EX-OS non
 * ha: nessun filesystem montabile qui ne crea, e S_ISLNK risponde sempre
 * falso (vedi <sys/stat.h>). Quindi non e' un'approssimazione, e' la
 * stessa operazione — c'e' perche' il codice di terzi la chiama per nome
 * quando NON vuole seguire un collegamento, e qui non c'e' niente da
 * seguire. */
int lstat(const char *path, struct stat *st)
{
    return stat(path, st);
}

int fstat(int fd, struct stat *st)
{
    long dim;

    if (st == NULL) { errno = 14; return -14; }   /* EFAULT */

    dim = fsize(fd);
    if (dim < 0) return (int)dim;

    /* Tutto il resto e' quello che si puo' dire di un descrittore senza
     * una syscall fstat: la dimensione e' vera, il tipo e' un'ipotesi
     * ragionevole, i tempi non ci sono. Dichiarato nell'header. */
    st->st_dev   = 0;
    st->st_ino   = 0;
    st->st_mode  = S_IFREG | 0644u;
    st->st_nlink = 1;
    st->st_uid   = 0;
    st->st_gid   = 0;
    st->st_size  = (off_t)dim;
    st->st_blksize = 512;
    st->st_blocks  = (blkcnt_t)((dim + 511) / 512);
    st->st_atime = 0;
    st->st_mtime = 0;
    st->st_ctime = 0;
    return 0;
}

/* fstat() non ha una syscall propria, e non serve: la dimensione di un
 * file aperto si ottiene posizionandosi alla fine e tornando indietro.
 * Aggiungere un numero di syscall per una cosa che si compone di due
 * esistenti avrebbe allargato l'ABI senza aggiungere informazione. */
long fsize(int fd)
{
    long ora = lseek(fd, 0, SEEK_CUR);
    long fine;

    if (ora < 0) return -1;
    fine = lseek(fd, 0, SEEK_END);
    if (fine < 0) return -1;

    if (lseek(fd, ora, SEEK_SET) < 0) return -1;
    return fine;
}

/* =============================================================================
 * setjmp / longjmp
 *
 * Sono in assembly perche' NON si possono scrivere in C: salvare e
 * ripristinare esattamente i registri che la convenzione di chiamata
 * dichiara conservati (ebx, esi, edi, ebp, esp e l'indirizzo di ritorno)
 * e' proprio cio' che il compilatore ha il permesso di riorganizzare.
 *
 * Servono a qualunque compilatore o interprete per uscire da una
 * ricorsione profonda quando trova un errore, senza far tornare a mano
 * ogni livello. Sono la ragione per cui questa coppia sta qui prima ancora
 * che TCC arrivi: e' il pezzo che non si puo' aggirare scrivendo il
 * programma "meglio".
 *
 * ⚠️ Non salvano la maschera dei segnali perche' EX-OS non ha segnali, e
 * non salvano lo stato della FPU. jmp_buf e' di sei parole: chi lo
 * dichiara deve usare il tipo, non un array di interi scelto a occhio.
 * ============================================================================= */
__asm__(
".text\n"
".globl setjmp\n"
".type setjmp, @function\n"
"setjmp:\n"
"    movl 4(%esp), %eax\n"      /* jmp_buf */
"    movl %ebx,  0(%eax)\n"
"    movl %esi,  4(%eax)\n"
"    movl %edi,  8(%eax)\n"
"    movl %ebp, 12(%eax)\n"
"    leal 4(%esp), %ecx\n"      /* esp come sara' DOPO il ret */
"    movl %ecx, 16(%eax)\n"
"    movl 0(%esp), %ecx\n"      /* indirizzo di ritorno */
"    movl %ecx, 20(%eax)\n"
"    xorl %eax, %eax\n"         /* la prima volta si ritorna 0 */
"    ret\n"
".globl longjmp\n"
".type longjmp, @function\n"
"longjmp:\n"
"    movl 4(%esp), %edx\n"      /* jmp_buf */
"    movl 8(%esp), %eax\n"      /* valore da restituire */
"    testl %eax, %eax\n"
"    jnz 1f\n"
"    movl $1, %eax\n"           /* longjmp(buf,0) deve valere 1 */
"1:\n"
"    movl  0(%edx), %ebx\n"
"    movl  4(%edx), %esi\n"
"    movl  8(%edx), %edi\n"
"    movl 12(%edx), %ebp\n"
"    movl 16(%edx), %esp\n"
"    movl 20(%edx), %ecx\n"
"    jmp *%ecx\n"
);

/* =============================================================================
 * ctype — classificazione dei caratteri
 *
 * Funzioni e non tabella: una tabella da 256 byte costerebbe piu' di
 * questi confronti in ogni binario che ne usa una sola. Valgono per l'ASCII
 * a sette bit; EX-OS non ha locale e i byte oltre 127 dipendono dalla code
 * page della console, quindi dichiararli "lettere" sarebbe una scelta
 * arbitraria travestita da informazione.
 * ============================================================================= */
int isdigit(int c)  { return c >= '0' && c <= '9'; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int islower(int c)  { return c >= 'a' && c <= 'z'; }
int isupper(int c)  { return c >= 'A' && c <= 'Z'; }
int isalpha(int c)  { return islower(c) || isupper(c); }
int isalnum(int c)  { return isalpha(c) || isdigit(c); }
int isspace(int c)  { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isprint(int c)  { return c >= 32 && c < 127; }

/* isascii e toascii non sono del C standard — sono di POSIX, e da POSIX
 * 2008 sono pure deprecate — ma il codice di terzi le chiama lo stesso:
 * la printf di GMP le usa per decidere se un carattere si puo' stampare.
 * Nella locale "C", l'unica che EX-OS ha, "ASCII" e "sotto il 128" sono
 * la stessa cosa. isblank invece e' del C99 e mancava per svista. */
int isascii(int c) { return (unsigned)c < 128; }
int toascii(int c) { return c & 0x7F; }
int isblank(int c) { return c == ' ' || c == '\t'; }
int isgraph(int c)  { return c > 32 && c < 127; }
int ispunct(int c)  { return isgraph(c) && !isalnum(c); }
int iscntrl(int c)  { return (c >= 0 && c < 32) || c == 127; }
int tolower(int c)  { return isupper(c) ? c - 'A' + 'a' : c; }
int toupper(int c)  { return islower(c) ? c - 'a' + 'A' : c; }

/* =============================================================================
 * Stringhe — il resto di <string.h>
 * ============================================================================= */

char *strncat(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (*d) d++;
    while (n-- > 0 && *src) *d++ = *src++;
    *d = '\0';
    return dst;
}

void *memchr(const void *s, int c, size_t n)
{
    const uint8_t *p = (const uint8_t *)s;
    while (n--) {
        if (*p == (uint8_t)c) return (void *)p;
        p++;
    }
    return NULL;
}

char *strstr(const char *fieno, const char *ago)
{
    size_t n;

    if (ago[0] == '\0') return (char *)fieno;

    n = strlen(ago);
    for (; *fieno; fieno++) {
        if (strncmp(fieno, ago, n) == 0) return (char *)fieno;
    }
    return NULL;
}

char *strdup(const char *s)
{
    size_t n;
    char  *p;

    if (s == NULL) return NULL;
    n = strlen(s) + 1u;
    p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

size_t strspn(const char *s, const char *accetta)
{
    size_t n = 0;
    while (s[n] && strchr(accetta, s[n]) != NULL) n++;
    return n;
}

size_t strcspn(const char *s, const char *rifiuta)
{
    size_t n = 0;
    while (s[n] && strchr(rifiuta, s[n]) == NULL) n++;
    return n;
}

/* strtok mantiene uno stato fra una chiamata e l'altra: e' l'interfaccia
 * del C standard, con il difetto del C standard — una sola scansione per
 * volta in tutto il programma. */
static char *strtok_stato = NULL;

char *strtok(char *s, const char *sep)
{
    char *inizio;

    if (s == NULL) s = strtok_stato;
    if (s == NULL) return NULL;

    s += strspn(s, sep);
    if (*s == '\0') { strtok_stato = NULL; return NULL; }

    inizio = s;
    s += strcspn(s, sep);
    if (*s != '\0') { *s = '\0'; strtok_stato = s + 1; }
    else            { strtok_stato = NULL; }

    return inizio;
}

/* =============================================================================
 * Conversioni numeriche
 *
 * strtol e strtoul fanno cio' che atoi() non sa fare e che serve a
 * chiunque legga un file di testo: dire DOVE si sono fermate (`fine`) e
 * riconoscere la base da sole (0x -> 16, 0 -> 8) quando base vale 0.
 * ============================================================================= */

static int cifra_valore(int c)
{
    if (isdigit(c)) return c - '0';
    if (islower(c)) return c - 'a' + 10;
    if (isupper(c)) return c - 'A' + 10;
    return -1;
}

/* Il convertitore vero e' a 64 bit e le tre versioni piu' strette gli
 * girano intorno. Non e' generalita' gratuita: TCC chiede strtoull per
 * leggere le costanti intere dei sorgenti che compila, e tenere due
 * copie della stessa scansione — una a 32 bit e una a 64 — vuol dire due
 * posti dove sbagliare il riconoscimento della base. */
unsigned long long strtoull(const char *s, char **fine, int base)
{
    unsigned long long v = 0;
    int           neg = 0, cifre = 0;
    const char   *p = s;

    while (isspace((unsigned char)*p)) p++;

    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }

    if ((base == 0 || base == 16) && p[0] == '0' &&
        (p[1] == 'x' || p[1] == 'X') && cifra_valore((unsigned char)p[2]) >= 0 &&
        cifra_valore((unsigned char)p[2]) < 16) {
        p += 2; base = 16;
    } else if (base == 0 && p[0] == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }

    for (;;) {
        int c = cifra_valore((unsigned char)*p);
        if (c < 0 || c >= base) break;
        v = v * (unsigned long long)base + (unsigned long long)c;
        p++; cifre++;
    }

    /* Nessuna cifra: la conversione non e' avvenuta, e `fine` deve
     * riportare il punto di PARTENZA. Chi controlla `fine == s` e' l'unico
     * modo di distinguere "zero" da "non era un numero". */
    if (fine) *fine = (char *)(cifre ? p : s);

    return neg ? (unsigned long long)(-(long long)v) : v;
}

long long strtoll(const char *s, char **fine, int base)
{
    return (long long)strtoull(s, fine, base);
}

unsigned long strtoul(const char *s, char **fine, int base)
{
    return (unsigned long)strtoull(s, fine, base);
}

long strtol(const char *s, char **fine, int base)
{
    return (long)strtoull(s, fine, base);
}

long atol(const char *s) { return strtol(s, NULL, 10); }
int  abs(int v)          { return (v < 0) ? -v : v; }
long labs(long v)        { return (v < 0) ? -v : v; }

/* =============================================================================
 * div, ldiv — quoziente e resto insieme
 *
 * ⚠️ IL TRONCAMENTO E' VERSO LO ZERO, non verso il basso: div(-7, 2) da'
 * quot = -3 e rem = -1, non -4 e +1. E' quello che dice lo standard C dal
 * C99, ed e' anche quello che fa `idiv` dell'x86, quindi qui l'operatore
 * del C basta e non c'e' niente da correggere. Su una macchina dove non
 * fosse cosi', queste due funzioni sarebbero il posto in cui rimediare —
 * e' per questo che esistono invece di essere due divisioni scritte a
 * mano dal chiamante.
 * ============================================================================= */
div_t div(int num, int den)
{
    div_t r;
    r.quot = num / den;
    r.rem  = num % den;
    return r;
}

ldiv_t ldiv(long num, long den)
{
    ldiv_t r;
    r.quot = num / den;
    r.rem  = num % den;
    return r;
}

long long llabs(long long v) { return (v < 0) ? -v : v; }

/* =============================================================================
 * lldiv — e la divisione a 64 bit che il compilatore non puo' fare
 *
 * ⚠️ QUI NON SI PUO' SCRIVERE `num / den`. Sull'i386 una divisione fra due
 * interi a 64 bit non e' un'istruzione: il compilatore la trasforma in una
 * chiamata a `__divmoddi4` di libgcc, e i programmi di EX-OS si linkano
 * con -nostdlib e SENZA libgcc. L'errore non sarebbe a runtime, sarebbe al
 * link — «undefined reference to __divmoddi4» — su qualunque programma che
 * includa questa funzione, anche senza chiamarla mai.
 *
 * E' la stessa ragione per cui la printf ha la sua div64 (vedi piu'
 * sopra); quella pero' divide per una BASE PICCOLA e tiene il resto in 32
 * bit, quindi non serve qui. Questa e' la versione completa, a
 * spostamenti: un bit per giro, dal piu' alto.
 *
 * ⚠️ DIVISIONE PER ZERO: l'hardware solleverebbe #DE, qui non c'e' niente
 * che possa sollevare niente. Si ritorna quot = 0 e rem = num, che e' un
 * risultato falso ma prevedibile — e resta un difetto del chiamante. Non
 * si finge un errore in errno: lo standard dice che e' comportamento
 * indefinito, e inventare una convenzione che nessun altro ha renderebbe
 * il codice che ci si appoggia non portabile.
 * ============================================================================= */
static unsigned long long u64_divmod(unsigned long long n, unsigned long long d,
                                     unsigned long long *resto)
{
    unsigned long long q = 0, r = 0;
    int i;

    if (d == 0) { if (resto) *resto = n; return 0; }

    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1ull);
        if (r >= d) { r -= d; q |= (1ull << i); }
    }
    if (resto) *resto = r;
    return q;
}

/* Il valore assoluto di un long long come unsigned, senza traboccare sul
 * minimo: -LLONG_MIN non ci sta in un long long, e scriverlo cosi' e'
 * comportamento indefinito che su i386 di solito funziona — finche' un
 * giorno non funziona. */
static unsigned long long ll_assoluto(long long v)
{
    return (v < 0) ? (unsigned long long)(-(v + 1)) + 1ull
                   : (unsigned long long)v;
}

lldiv_t lldiv(long long num, long long den)
{
    lldiv_t  r;
    unsigned long long q, resto;
    int      neg_q = ((num < 0) != (den < 0));

    q = u64_divmod(ll_assoluto(num), ll_assoluto(den), &resto);

    /* ⚠️ Il troncamento e' verso lo ZERO e il resto prende il segno del
     * DIVIDENDO — non del divisore. lldiv(-9000000000, 7) da'
     * (-1285714285, -5), non (-1285714286, +2). E' cio' che dice lo
     * standard C dal C99, ed e' anche cio' che fa `idiv` dell'x86: qui va
     * riprodotto a mano perche' la divisione la stiamo facendo noi. */
    r.quot = neg_q ? -(long long)q : (long long)q;
    r.rem  = (num < 0) ? -(long long)resto : (long long)resto;
    return r;
}

long long atoll(const char *s) { return strtoll(s, NULL, 10); }

/* =============================================================================
 * La famiglia <inttypes.h>: intmax_t e' `long long` su questo bersaglio
 *
 * Sono gli stessi calcoli delle `ll*`, con i nomi che usa chi scrive
 * codice indipendente dalla larghezza dei tipi. ⚠️ imaxdiv NON puo' usare
 * l'operatore `/` per la stessa ragione di lldiv: la divisione a 64 bit
 * diventa una chiamata a libgcc, che non colleghiamo.
 * ============================================================================= */
intmax_t imaxabs(intmax_t v) { return (v < 0) ? -v : v; }

imaxdiv_t imaxdiv(intmax_t num, intmax_t den)
{
    imaxdiv_t r;
    lldiv_t   l = lldiv((long long)num, (long long)den);
    r.quot = (intmax_t)l.quot;
    r.rem  = (intmax_t)l.rem;
    return r;
}

intmax_t strtoimax(const char *s, char **fine, int base)
{
    return (intmax_t)strtoll(s, fine, base);
}

uintmax_t strtoumax(const char *s, char **fine, int base)
{
    return (uintmax_t)strtoull(s, fine, base);
}

/* =============================================================================
 * rand, srand
 *
 * Il generatore congruenziale lineare dell'esempio del K&R, con le
 * costanti di POSIX. ⚠️ NON E' CASUALE: si ripete, e da un seme noto da'
 * sempre la stessa sequenza. Va bene per mescolare o per una prova; non va
 * bene per una chiave ne' per un identificativo che qualcuno abbia
 * interesse a indovinare. Il giorno che servisse quello servira' una
 * sorgente di entropia vera, che il kernel non ha.
 *
 * Il seme parte da 1 e non dall'orologio, come dice lo standard: un
 * programma che non chiama srand deve vedere sempre la stessa sequenza,
 * o le sue prove non si ripetono.
 * ============================================================================= */
static unsigned long rand_seme = 1;

void srand(unsigned int seme) { rand_seme = seme; }

int rand(void)
{
    rand_seme = rand_seme * 1103515245ul + 12345ul;
    /* Si scartano i bit bassi: in un LCG sono i meno casuali di tutti —
     * il bit 0 alterna e basta. */
    return (int)((rand_seme >> 16) & 0x7FFF);
}

/* =============================================================================
 * system — c'e' il nome, non c'e' l'interprete
 *
 * ⚠️ RITORNA SEMPRE -1 CON ENOSYS, e non e' un segnaposto da riempire piu'
 * avanti: e' la risposta giusta finche' /bin/sh non sa accettare un
 * comando sulla riga di argomenti. Oggi ha un `_start(void)` e legge solo
 * dal terminale, quindi non c'e' niente a cui passare la stringa.
 * Eseguire "qualcosa di simile" o ritornare 0 fingendo di aver eseguito
 * sarebbe peggio di non esserci: il chiamante andrebbe avanti convinto che
 * il comando sia stato fatto.
 *
 * system(NULL) chiede «esiste un interprete?» e la risposta e' 0, cioe'
 * no. Quella e' una risposta vera, e infatti e' l'unica che si da'.
 *
 * C'e' perche' <cstdlib> della libstdc++ fa `using ::system;`.
 * ============================================================================= */
int system(const char *comando)
{
    if (comando == NULL) return 0;      /* nessun interprete disponibile */
    errno = ENOSYS;
    return -1;
}

/* atof e' strtod senza il puntatore alla fine, e fabs e' abs in virgola
 * mobile. Nessuna delle due aggiunge niente a quello che c'e' gia': ci
 * sono perche' il codice di terzi le nomina — stabs.c di binutils la
 * prima, gprof la seconda. */
double atof(const char *s)  { return strtod(s, NULL); }

/* =============================================================================
 * ⚠️ QUATTRO FUNZIONI `weak`, E IL MOTIVO SI VEDE SOLO LINKANDO cc1
 *
 * fabs, sqrt, ldexp e frexp esistono qui perche' il codice di terzi le
 * nomina e perche' quando sono state scritte openlibm non c'era. Adesso
 * c'e', e le definisce anche lui: un programma che linka libc.a E libm.a
 * — cc1 lo fa, per via di MPFR e MPC — trova due definizioni dello stesso
 * simbolo e il link fallisce.
 *
 *     ld: libc.a(libc.o): in function `fabs':
 *         multiple definition of `fabs';
 *         libm.a(s_fabs.c.o): first defined here
 *
 * Toglierle da qui non si puo': i programmi di EX-OS linkano solo libc, e
 * resterebbero senza. Marcarle `weak` risolve entrambi i casi con una
 * regola sola — chi linka anche libm prende la versione di openlibm, che
 * e' quella giusta (arrotondamenti IEEE, casi limite, denormali); chi
 * linka solo libc prende queste, che bastano a quello che fanno.
 *
 * ⚠️ NON E' UNA SCELTA FRA DUE VERSIONI EQUIVALENTI. Quella di openlibm e'
 * migliore: frexp qui perde precisione sui denormali (lo dice il suo
 * commento), ldexp fa moltiplicazioni ripetute invece di toccare
 * l'esponente. `weak` significa proprio «se c'e' di meglio, usa quello».
 * ============================================================================= */
__attribute__((weak))
double fabs(double v)       { return (v < 0.0) ? -v : v; }

/* =============================================================================
 * Virgola mobile
 *
 * PERCHE' ESISTE QUESTA SEZIONE. Un compilatore deve leggere i letterali
 * numerici dei sorgenti che compila: `float x = 1.5;` passa da strtod, e
 * una strtod che ritorna zero non da' un errore — da' un programma
 * compilato con la costante sbagliata. E' il motivo per cui il kernel ha
 * dovuto imparare a inizializzare la FPU e a salvarne lo stato nel cambio
 * di contesto (kernel/include/fpu.h): senza, due processi che fanno conti
 * in virgola mobile si sovrascrivono i registri x87 a vicenda.
 *
 * QUANTO E' PRECISA. La mantissa si accumula in `double` cifra per cifra
 * e poi si scala per una potenza di dieci. Fino a 15-16 cifre
 * significative il risultato coincide con quello di una libc seria; oltre,
 * l'ultimo bit puo' differire, perche' l'arrotondamento avviene due volte
 * (accumulo e scala) invece che una. Non e' correttamente arrotondata a
 * mezzo ULP come pretende lo standard, e chi ci costruisce sopra un
 * calcolo numerico serio deve saperlo.
 *
 * PERCHE' NON in `unsigned long long`, che sarebbe piu' preciso: la
 * conversione da intero a 64 bit verso double e' una chiamata a
 * __floatundidf di libgcc, e i programmi di EX-OS si linkano con
 * -nostdlib e senza libgcc — la stessa ragione per cui la printf divide
 * con div64() invece che con l'operatore.
 *
 * NON riconosce gli esadecimali del C99 (`0x1p3`) ne' "inf"/"nan". TCC
 * converte i primi da se' (e' l'unica cosa per cui chiama ldexp); il
 * giorno che servissero davvero, il posto e' questo.
 * ============================================================================= */

/* Potenze di dieci ESATTE in doppia precisione: 10^22 e' l'ultima che lo
 * e', perche' 5^23 non entra piu' nei 53 bit di mantissa. Oltre, si
 * moltiplica piu' volte e si accetta l'errore. */
static const double pot10[] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
};
#define POT10_MAX  22

static double scala10(double v, int e)
{
    if (e > 0) {
        while (e > POT10_MAX) { v *= pot10[POT10_MAX]; e -= POT10_MAX; }
        v *= pot10[e];
    } else if (e < 0) {
        e = -e;
        /* Si DIVIDE invece di moltiplicare per 10^-e: le potenze negative
         * di dieci non sono rappresentabili esattamente, quindi
         * moltiplicare per una di esse aggiungerebbe un errore in piu'
         * rispetto a una sola divisione per una potenza esatta. */
        while (e > POT10_MAX) { v /= pot10[POT10_MAX]; e -= POT10_MAX; }
        v /= pot10[e];
    }
    return v;
}

double strtod(const char *s, char **fine)
{
    const char *p = s;
    double      v = 0.0;
    int         neg = 0, cifre = 0, sig = 0, exp10 = 0;

    while (isspace((unsigned char)*p)) p++;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }

    /* Parte intera. Oltre la 17esima cifra significativa il valore non
     * cambia piu' (il double non le distingue): le cifre in piu' si
     * contano solo come scala, altrimenti l'accumulo perde il controllo. */
    for (; isdigit((unsigned char)*p); p++) {
        cifre++;
        if (sig < 17) { v = v * 10.0 + (double)(*p - '0'); sig++; }
        else          { exp10++; }
    }

    if (*p == '.') {
        p++;
        for (; isdigit((unsigned char)*p); p++) {
            cifre++;
            if (sig < 17) { v = v * 10.0 + (double)(*p - '0'); sig++; exp10--; }
        }
    }

    /* Nessuna cifra: non era un numero. `fine` torna al punto di partenza,
     * che e' l'unico modo che ha il chiamante di accorgersene. */
    if (cifre == 0) {
        if (fine) *fine = (char *)s;
        return 0.0;
    }

    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        int         eneg = 0, ecifre = 0;
        long        ev = 0;

        if (*q == '+' || *q == '-') { eneg = (*q == '-'); q++; }
        for (; isdigit((unsigned char)*q); q++) {
            ecifre++;
            if (ev < 100000L) ev = ev * 10 + (*q - '0');   /* satura: oltre
                                        e' comunque infinito o zero */
        }
        /* Una 'e' senza cifre dietro NON fa parte del numero: "1e" vale 1
         * e `fine` deve fermarsi sulla 'e'. */
        if (ecifre) { exp10 += eneg ? -(int)ev : (int)ev; p = q; }
    }

    v = scala10(v, exp10);
    if (fine) *fine = (char *)p;
    return neg ? -v : v;
}

/* Le due varianti sono conversioni della stessa scansione. In particolare
 * strtold NON e' piu' precisa di strtod: il risultato passa comunque per
 * un double, quindi si ferma a 53 bit di mantissa invece dei 64 che l'x87
 * saprebbe tenere. E' dichiarato qui perche' chi legge il codice non
 * debba dedurlo. */
float strtof(const char *s, char **fine)
{
    return (float)strtod(s, fine);
}

long double strtold(const char *s, char **fine)
{
    return (long double)strtod(s, fine);
}

/* x * 2^e, per esponenziazione binaria: log2(e) moltiplicazioni invece di
 * e. L'ultima elevazione al quadrato si salta di proposito — servirebbe
 * solo a traboccare a infinito un valore che poi non verrebbe usato. */
__attribute__((weak))   /* vedi la nota su fabs */
double ldexp(double x, int e)
{
    double f = 1.0, p = 2.0;
    int    n = (e < 0) ? -e : e;

    while (n) {
        if (n & 1) f *= p;
        n >>= 1;
        if (n) p *= p;
    }
    return (e < 0) ? (x / f) : (x * f);
}

/* =============================================================================
 * sqrt — l'unica funzione di libm che si puo' scrivere senza scendere a patti
 *
 * <math.h> dice, e continua a dire, che qui non c'e' una libm: una sqrt
 * "quasi giusta" sarebbe peggio di nessuna sqrt, perche' sbaglia in
 * silenzio. Questa non e' quasi giusta — e' **esatta**.
 *
 * `fsqrt` dell'x87 e' una delle cinque operazioni che l'IEEE 754 obbliga a
 * essere correttamente arrotondate (le altre quattro sono +, -, *, /):
 * il risultato e' il numero rappresentabile piu' vicino alla radice vera,
 * a mezzo ULP. Non c'e' un'approssimazione da giudicare, c'e' un'istruzione
 * da chiamare — ed e' il motivo per cui questa entra e log, exp, sin non
 * entrano.
 *
 * ⚠️ SU ARGOMENTO NEGATIVO l'x87 solleva l'eccezione "operazione non
 * valida" e produce un NaN. Il kernel inizializza la FPU con le eccezioni
 * mascherate, quindi il NaN esce e basta; qui si imposta anche errno, che
 * e' cio' che si aspetta chi scrive codice per POSIX.
 *
 * La chiede MPC, che stima con la sqrt in doppia precisione quanti bit
 * servono prima di lavorare in precisione arbitraria.
 * ============================================================================= */
__attribute__((weak))   /* vedi la nota su fabs */
double sqrt(double x)
{
    double r;

    if (x < 0.0) { errno = EDOM; }

    __asm__ ("fsqrt" : "=t" (r) : "0" (x));
    return r;
}

/* L'inversa di ldexp: separa x in mantissa (in [0.5, 1) ) ed esponente.
 *
 * Serve a chi manipola i numeri in virgola mobile invece di calcolarci —
 * il primo a chiederla e' stato floatformat.c di libiberty, che converte
 * fra i formati IEEE dei vari bersagli e ha bisogno dei due pezzi separati.
 *
 * Si scala per moltiplicazioni invece di leggere i bit dell'esponente: due
 * cicli invece di uno, ma nessuna ipotesi su come e' fatto un `double` in
 * memoria, e quindi niente da rivedere il giorno che si compilasse altrove.
 *
 * ⚠️ I DENORMALI PERDONO PRECISIONE. Un valore sotto ~2.2e-308 viene
 * moltiplicato fino a rientrare nell'intervallo, e i bit gia' persi nella
 * rappresentazione denormale non tornano indietro. Zero, infiniti e NaN
 * escono come sono, con esponente 0, che e' cio' che dice lo standard. */
__attribute__((weak))   /* vedi la nota su fabs */
double frexp(double x, int *e)
{
    int n = 0;

    /* x != x e' vero solo per NaN; il confronto con se stesso e' il modo
     * di riconoscerlo senza isnan(), che qui non c'e'. */
    if (x == 0.0 || x != x || x > 1.7976931348623157e308
                           || x < -1.7976931348623157e308) {
        if (e) *e = 0;
        return x;
    }

    while (x >= 1.0 || x <= -1.0) { x /= 2.0; n++; }
    while (x > -0.5 && x < 0.5)   { x *= 2.0; n--; }

    if (e) *e = n;
    return x;
}

/* =============================================================================
 * Ordinamento e ricerca
 *
 * qsort e' uno shell sort, non un quicksort: niente ricorsione (quindi
 * nessun consumo di stack che dipende dai dati, su un sistema dove lo
 * stack utente cresce su fault), nessun caso peggiore quadratico
 * sull'input gia' ordinato — che e' esattamente il caso frequente — e
 * venti righe invece di sessanta. Su vettori grandi e' piu' lento di un
 * quicksort fatto bene; su quelli che si ordinano dentro EX-OS non si
 * misura.
 * ============================================================================= */
void qsort(void *base, size_t n, size_t dim, int (*cmp)(const void *, const void *))
{
    char  *v = (char *)base;
    size_t salto;

    if (base == NULL || cmp == NULL || dim == 0) return;

    for (salto = n / 2; salto > 0; salto /= 2) {
        size_t i;
        for (i = salto; i < n; i++) {
            size_t j;
            for (j = i; j >= salto; j -= salto) {
                char *a = v + (j - salto) * dim;
                char *b = v + j * dim;
                size_t k;

                if (cmp(a, b) <= 0) break;

                for (k = 0; k < dim; k++) {
                    char t = a[k]; a[k] = b[k]; b[k] = t;
                }
            }
        }
    }
}

void *bsearch(const void *chiave, const void *base, size_t n, size_t dim,
              int (*cmp)(const void *, const void *))
{
    size_t basso = 0, alto = n;

    while (basso < alto) {
        size_t mezzo = basso + (alto - basso) / 2;
        char  *p = (char *)base + mezzo * dim;
        int    r = cmp(chiave, p);

        if (r == 0) return p;
        if (r < 0)  alto = mezzo;
        else        basso = mezzo + 1;
    }
    return NULL;
}

/* =============================================================================
 * Messaggi di errore
 *
 * I codici sono quelli di kernel/include/syscall.h. Un numero che non e'
 * in elenco non diventa "errore sconosciuto" e basta: il chiamante ha
 * comunque il numero in mano, e perderlo nel messaggio sarebbe togliere
 * l'unica informazione utile.
 *
 * ⚠️ IL RITORNO E' `char *`, NON `const char *`, ed e' voluto anche se
 * sembra il contrario. Lo standard dichiara `char *strerror(int)`: la
 * versione con const e' piu' sicura ma NON e' compatibile, e il codice di
 * terzi che ridichiara la funzione — xstrerror.c di libiberty lo fa —
 * smette di compilare con "conflicting types". Nessun cast serve: in C un
 * letterale di stringa ha tipo `char[]`, non `const char[]`. Il messaggio
 * resta comunque immodificabile davvero, perche' sta in .rodata; e' il
 * tipo che dice meno della verita', non il contrario.
 * ============================================================================= */
char *strerror(int err)
{
    if (err < 0) err = -err;

    switch (err) {
        case 0:   return "nessun errore";
        case 1:   return "operazione non permessa";
        case 2:   return "file o directory inesistente";
        case 3:   return "processo inesistente";
        case 4:   return "interrotto";
        case 5:   return "errore di I/O";
        case 9:   return "descrittore non valido";
        case 10:  return "nessun figlio";
        case 12:  return "memoria esaurita";
        case 13:  return "accesso negato";
        case 14:  return "indirizzo non valido";
        case 16:  return "risorsa occupata";
        case 17:  return "esiste gia'";
        case 19:  return "dispositivo assente";
        case 20:  return "non e' una directory";
        case 21:  return "e' una directory";
        case 22:  return "argomento non valido";
        case 24:  return "troppi file aperti";
        case 25:  return "non e' un terminale";
        case 27:  return "file troppo grande";
        case 28:  return "spazio esaurito";
        case 29:  return "posizionamento non consentito";
        case 30:  return "filesystem in sola lettura";
        case 32:  return "pipe interrotta";
        case 38:  return "funzione non implementata";
        case 39:  return "directory non vuota";
        case 110: return "attesa scaduta";
        case 123: return "nessun disco nel lettore";
        default:  return "errore";
    }
}

void perror(const char *msg)
{
    if (msg != NULL && msg[0] != '\0') {
        fputs(msg, stderr);
        fputs(": ", stderr);
    }
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
}

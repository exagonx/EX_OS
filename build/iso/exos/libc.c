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

/* Lo stato di una conversione multibyte. Duplicato da lib/include/libc.h,
 * come gli altri tipi: questo file non include il proprio header. */
typedef struct {
    int __nulla;
} mbstate_t;

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
#define SYS_IPC_RECV_TMO  228
#define SYS_TIME           13
#define SYS_CONSOLE_SWITCH 229
#define SYS_CONSOLE_WRITE  230
#define SYS_CONSOLE_INFO   231
#define SYS_IOCTL         54
#define SYS_DUP           41
#define SYS_DUP2          63
#define SYS_FCNTL         55

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

/* La lunghezza massima di un percorso: VFS_PATH_MAX del kernel, e le due
 * devono restare uguali. Duplicata anche in <limits.h> e <sys/param.h>. */
#define PERCORSO_MAX 320

/* Numero massimo di voci per chiamata a listdir: vedi libc.h. */
#define LISTDIR_MAX_BATCH 16

/* spawn con ambiente e redirezioni — duplicate da kernel/include/syscall.h
 * e da lib/include/libc.h. La magia impedisce al kernel di leggere ESI
 * quando lo chiama un programma compilato per la vecchia forma. */
#define SPAWN_EXTRA_MAGIA    0x53504E58u
#define SPAWN_MAX_AZIONI     4
#define SPAWN_RED_PATH_MAX   128

typedef struct {
    unsigned int fd;
    unsigned int flags;
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
    const char *percorso;
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
#define IPC_MSG_MAX_DATA 512
typedef struct {
    unsigned int  sender_pid;
    unsigned int  type;
    unsigned int  len;
    unsigned char data[IPC_MSG_MAX_DATA];
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
typedef long time_t;

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

struct timeval {
    long tv_sec;
    long tv_usec;
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

struct stat {
    unsigned int    st_dev;
    unsigned int    st_ino;
    unsigned int    st_mode;
    unsigned int    st_nlink;
    unsigned int    st_uid;
    unsigned int    st_gid;
    unsigned int    st_size;
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
    if (_syscall1(SYS_CLOSE, (uint32_t)f->fd) < 0) r = -1;

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
 * ⚠️ NIENTE VIRGOLA MOBILE. %f, %e e %g consumano il loro argomento (un
 * double, otto byte) e stampano "<float>". Consumarlo NON e' un dettaglio:
 * saltarlo disallineerebbe tutti gli argomenti successivi, e la stampa
 * dopo un %f diventerebbe spazzatura invece di un buco. EX-OS non salva lo
 * stato della FPU nel cambio di contesto (vedi kernel/sched/sched.c):
 * finche' e' cosi', un programma che usa i double non ha solo una printf
 * incompleta, ha un problema piu' grosso.
 *
 * I NUMERI A 64 BIT sono formattati con una divisione fatta a mano
 * (div64_10 e simili). Non e' pedanteria: dividere un uint64_t sull'i386
 * fa chiamare al compilatore __udivdi3 di libgcc, e i programmi di EX-OS
 * si linkano con -nostdlib e SENZA libgcc — l'errore sarebbe al link, non
 * a runtime.
 * ============================================================================= */

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
        int  lungo = 0;             /* 1 = long, 2 = long long */
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
                /* Vedi la nota in testa: l'argomento va CONSUMATO comunque,
                 * o tutto quello che segue slitta. */
                const char *s = "<float>";
                (void)__builtin_va_arg(args, double);
                while (*s) u_car(u, *s++);
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

void exit(int code)
{
    /* All'indietro, come pretende lo standard: l'ultima registrata e' la
     * prima chiamata. La guardia serve a un handler che chiami exit() —
     * senza, sarebbe una ricorsione infinita invece di un'uscita. */
    if (!g_in_uscita) {
        g_in_uscita = 1;
        while (g_atexit_n > 0) g_atexit[--g_atexit_n]();
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
 * IL PREZZO, dichiarato: l'intestazione e' di 16 byte per blocco, e la
 * ricerca e' lineare. Su un compilatore che alloca centinaia di migliaia
 * di oggetti piccoli l'intestazione pesa e la ricerca rallenta; il rimedio
 * (liste separate per taglia) si aggiunge sopra questa struttura senza
 * cambiarne il contratto, quando i numeri diranno che serve.
 * ============================================================================= */

#define HEAP_ALLINEA    8u
#define HEAP_MIN_SBRK   (64u * 1024u)   /* si chiede memoria a blocchi grossi */
#define HEAP_MIN_SPEZZA 32u             /* avanzo sotto il quale non si spezza */

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
static void heap_fondi_con_succ(Blocco *b)
{
    Blocco *s = b->succ;

    if (s == NULL || !s->libero || !b->libero) return;
    if ((char *)b + BLOCCO_HDR + b->dim != (char *)s) return;

    b->dim += BLOCCO_HDR + s->dim;
    b->succ = s->succ;
    if (s->succ) s->succ->prec = b;
    else         heap_ultimo = b;
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
        heap_fondi_con_succ(b);
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

        ex.azioni[i].fd    = (unsigned)redir[i].fd;
        ex.azioni[i].flags = (unsigned)redir[i].flags;
        for (j = 0; j + 1 < SPAWN_RED_PATH_MAX && redir[i].percorso[j]; j++)
            ex.azioni[i].percorso[j] = redir[i].percorso[j];
        ex.azioni[i].percorso[j] = '\0';
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
 * ============================================================================= */
#define TMP_PREFISSO  "/tmp"

static unsigned g_tmp_contatore = 0;

static void tmp_componi(char *dst, size_t max)
{
    unsigned pid = (unsigned)getpid();
    unsigned ms  = (unsigned)uptime_ms();
    snprintf(dst, max, "/t%x%x%x.tmp", pid, ms, ++g_tmp_contatore);
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
 * rename — ⚠️ NON e' atomica, e va saputo
 *
 * Il VFS non ha un'operazione di rinomina: servirebbe nei driver (ext2 e
 * FAT), dove significa aggiungere una voce di directory e toglierne
 * un'altra senza lasciare il file irraggiungibile in mezzo. Finche' non
 * c'e', qui si copia e si cancella.
 *
 * Le due differenze da tenere presenti: costa quanto il file (una rename
 * vera non muove dati) e non e' indivisibile — un'interruzione a meta'
 * lascia entrambi i nomi. Per i file temporanei di un compilatore va
 * bene; per rinominare un archivio da mezzo giga no.
 * ============================================================================= */
int rename(const char *da, const char *a)
{
    int   fs, fd, n;
    char *buf;

    if (da == NULL || a == NULL) { errno = EINVAL; return -1; }

    fs = open(da, O_RDONLY);
    if (fs < 0) return -1;

    fd = open(a, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { close(fs); return -1; }

    buf = (char *)malloc(4096);
    if (buf == NULL) { close(fs); close(fd); errno = ENOMEM; return -1; }

    while ((n = (int)read(fs, buf, 4096)) > 0) {
        int scritti = 0;
        while (scritti < n) {
            int w = (int)write(fd, buf + scritti, (unsigned)(n - scritti));
            if (w <= 0) { free(buf); close(fs); close(fd); errno = EIO; return -1; }
            scritti += w;
        }
    }

    free(buf);
    close(fs);
    close(fd);

    if (n < 0) { errno = EIO; return -1; }

    return unlink(da);
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

__attribute__((weak, noreturn))
void _libc_start(int argc, char **argv, char **envp)
{
    /* L'ambiente del processo e' quello che il padre ha passato. Puo'
     * essere vuoto — succede al primo processo, che lo spawn lo fa il
     * kernel — e in quel caso getenv() ripiega sulla sezione [env] di
     * /boot/kernel.cfg: vedi getenv(). */
    environ = envp;

    exit(main(argc, argv));
    for (;;);
}

void sched_yield(void)
{
    _syscall1(SYS_SCHED_YIELD, 0);
}

void usleep(unsigned int us)
{
    /* Converti microsecondi in millisecondi (arrotondando) */
    uint32_t ms = (us + 999) / 1000;
    if (ms == 0) ms = 1;
    _syscall1(SYS_SLEEP, ms);
}

void sleep(unsigned int sec)
{
    _syscall1(SYS_SLEEP, sec * 1000);
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
    return (int)_syscall3(SYS_BOOTINSTALL, (uint32_t)punto, (uint32_t)info,
                          (uint32_t)sizeof(BootInstallInfo));
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
int ipc_send(unsigned int dest_pid, unsigned int type,
              const void *data, unsigned int len)
{
    return (int)_syscall4(SYS_IPC_SEND, dest_pid, type,
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

int gettimeofday(struct timeval *tv, void *fuso)
{
    (void)fuso;         /* obsoleto anche su POSIX */
    if (tv == NULL) return -1;

    tv->tv_sec = (long)time(NULL);
    /* I microsecondi vengono dal contatore dei tick, che avanza a scatti
     * di 10 ms: le ultime quattro cifre sono sempre zero. Meglio di zero
     * secco, perche' chi misura un intervallo breve almeno vede qualcosa
     * muoversi; peggio di un vero orologio ad alta risoluzione, che EX-OS
     * non ha. */
    tv->tv_usec = (long)((uptime_ms() % 1000u) * 1000u);
    return 0;
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
    st->st_size  = g->st_size;

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
    st->st_size  = (unsigned int)dim;
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

/* atof e' strtod senza il puntatore alla fine, e fabs e' abs in virgola
 * mobile. Nessuna delle due aggiunge niente a quello che c'e' gia': ci
 * sono perche' il codice di terzi le nomina — stabs.c di binutils la
 * prima, gprof la seconda. */
double atof(const char *s)  { return strtod(s, NULL); }
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

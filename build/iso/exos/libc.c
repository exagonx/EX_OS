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

#define S_ISDIR(attr)   (((attr) & 0x10) != 0)
#define S_ISREG(attr)   (((attr) & 0x10) == 0)

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

char *strcat(char *dst, const char *src)
{
    char *d = dst + strlen(dst);
    while ((*d++ = *src++));
    return dst;
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

int vsscanf(const char *s, const char *fmt, __builtin_va_list args)
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
    return assegnate;
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

void exit(int code)
{
    /* I BUFFER VANNO SVUOTATI QUI, e non e' una cortesia: un programma che
     * scrive un file e poi esce senza fclose() troverebbe il file monco —
     * l'ultimo pezzo sarebbe ancora nel buffer di un processo che non
     * esiste piu'. E' la trappola classica dello stdio bufferizzato, e la
     * si chiude nell'unico punto da cui passano tutti. */
    fflush(NULL);

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
void _libc_start(int argc, char **argv)
{
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

int mkdir(const char *path)
{
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

char *getenv(const char *nome)
{
    if (nome == NULL || *nome == '\0') return NULL;
    if (getconf(nome, getenv_buf, sizeof(getenv_buf)) < 0) return NULL;
    return getenv_buf;
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
 * ============================================================================= */
const char *strerror(int err)
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

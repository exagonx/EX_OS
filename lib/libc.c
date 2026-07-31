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
 *   Stringa:   strlen, strcpy, strncpy, strcmp, strncmp, strcat, strchr, strrchr
 *   Memoria:   memset, memcpy, memmove, memcmp
 *   Stdio:     putchar, puts, printf (minimale), getchar, gets
 *   Stdlib:    atoi, itoa, malloc, free, exit, abort
 *   Syscall:   wrappers per tutte le syscall EX-OS
 * ============================================================================= */

/* Tipi base */
typedef unsigned int    size_t;
typedef unsigned int    uint32_t;
typedef unsigned short  uint16_t;
typedef unsigned char   uint8_t;
typedef int             int32_t;
typedef int             ssize_t;
#define NULL ((void*)0)

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
#define SYS_READDIR     141
#define SYS_IPC_SEND     220
#define SYS_IPC_RECV     221
#define SYS_IPC_REGISTER 222
#define SYS_IPC_LOOKUP   223
#define SYS_IRQ_BIND      224
#define SYS_IOPORT_BIND   225
#define SYS_IOPORT_IN     226
#define SYS_IOPORT_OUT    227

/* Voce di directory — deve restare identica a kernel/include/syscall.h
 * (DirEntry) e a lib/include/libc.h: attraversa l'ABI della syscall. */
#define DIRENT_NAME_MAX 13
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
 * Stdio
 * ============================================================================= */

int putchar(int c)
{
    char ch = (char)c;
    _syscall3(SYS_WRITE, 1, (uint32_t)&ch, 1);
    return c;
}

int puts(const char *s)
{
    int n = (int)strlen(s);
    _syscall3(SYS_WRITE, 1, (uint32_t)s, (uint32_t)n);
    putchar('\n');
    return n;
}

/* =============================================================================
 * getchar — un carattere per volta su un TTY orientato alla RIGA
 *
 * BUG CORRETTO (luglio 2026): questa funzione faceva
 *     _syscall3(SYS_READ, 0, &c, 1)
 * cioe' chiedeva UN byte allo stdin. Il TTY di EX-OS pero' non e'
 * orientato al carattere: il servizio kbd accumula la riga e la consegna
 * intera su Invio (vedi drivers/kbd/kbd.c). Una read() da 1 byte faceva
 * consumare al driver l'INTERA riga per poi consegnarne un solo
 * carattere: tutto il resto veniva buttato. gets(), che chiama getchar()
 * in ciclo, restituiva quindi solo il primo carattere di ogni riga
 * digitata, e ogni carattere successivo costava un'altra riga di input.
 *
 * Fix: la libc tiene un proprio buffer di riga. getchar() lo riempie con
 * una read() completa quando e' vuoto e poi serve i byte da li'. Il
 * comportamento verso il chiamante e' quello atteso da un getchar(), e
 * il numero di syscall scende da uno per carattere a uno per riga.
 * ============================================================================= */
#define STDIN_BUF_SIZE  256

static char     stdin_buf[STDIN_BUF_SIZE];
static uint32_t stdin_len = 0;   /* byte validi nel buffer */
static uint32_t stdin_pos = 0;   /* prossimo byte da restituire */

int getchar(void)
{
    if (stdin_pos >= stdin_len) {
        int r = _syscall3(SYS_READ, 0, (uint32_t)stdin_buf, STDIN_BUF_SIZE);
        if (r <= 0) return -1;
        stdin_len = (uint32_t)r;
        stdin_pos = 0;
    }

    return (unsigned char)stdin_buf[stdin_pos++];
}

char *gets(char *buf, int max)
{
    int i = 0;
    int c;
    while (i < max - 1 && (c = getchar()) != -1 && c != '\n') {
        buf[i++] = (char)c;
    }
    buf[i] = '\0';
    return (i > 0) ? buf : NULL;
}

/* =============================================================================
 * printf minimale — supporta %d %i %u %x %X %o %s %c %p %%
 * con flag '-' (allineamento a sinistra), flag '0' (padding con zeri) e
 * larghezza minima di campo: es. "%-12s", "%04x", "%8u".
 *
 * BUG RISOLTO (luglio 2026): flag e larghezza non erano riconosciuti. Su
 * "%-12s" il carattere '-' finiva nel ramo default, che stampava "%-" come
 * testo letterale SENZA consumare l'argomento variadico; "12s" veniva poi
 * stampato come testo e lo specificatore successivo leggeva l'argomento
 * sbagliato. Era il motivo per cui `ls` stampava letteralmente
 * "%-12s 3221219808" (il numero era il puntatore al nome del file, letto
 * da %u al posto della dimensione).
 * ============================================================================= */

/* pf_pad — stampa s in un campo di larghezza width.
 * Il padding con zeri ha senso solo per l'allineamento a destra: come in C,
 * "%-05d" usa spazi. */
static int pf_pad(const char *s, int width, int left_align, char pad)
{
    int         len = 0;
    int         total = 0;
    const char *p = s;

    while (*p++) len++;

    if (!left_align) {
        while (len < width) { putchar(pad); total++; width--; }
    }

    p = s;
    while (*p) { putchar(*p++); total++; }

    if (left_align) {
        while (len < width) { putchar(' '); total++; width--; }
    }

    return total;
}

/* pf_utoa — converte v in base 'base' scrivendo a ritroso da 'end'
 * (che ospita il terminatore). Ritorna il puntatore alla prima cifra. */
static char *pf_utoa(unsigned v, unsigned base, const char *digits, char *end)
{
    char *p = end;

    *p = '\0';
    if (v == 0) {
        *--p = '0';
    } else {
        while (v > 0) { *--p = digits[v % base]; v /= base; }
    }
    return p;
}

int printf(const char *fmt, ...)
{
    static const char lower[] = "0123456789abcdef";
    static const char upper[] = "0123456789ABCDEF";

    /* Accede agli argomenti variabili via __builtin_va_* */
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    char buf[16];
    int  total = 0;

    while (*fmt) {
        int  left_align = 0;
        int  width      = 0;
        char pad        = ' ';
        char spec;

        if (*fmt != '%') {
            putchar(*fmt++);
            total++;
            continue;
        }
        fmt++;

        /* Flag: '-' (sinistra) e '0' (zeri), in qualunque ordine */
        for (;;) {
            if (*fmt == '-')      { left_align = 1; fmt++; }
            else if (*fmt == '0') { pad = '0';      fmt++; }
            else break;
        }

        /* Larghezza minima di campo */
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }

        spec = *fmt;
        if (spec == '\0') {      /* '%' finale senza specificatore */
            putchar('%'); total++;
            break;
        }
        fmt++;

        switch (spec) {
            case 'd': case 'i': {
                int      v   = __builtin_va_arg(args, int);
                int      neg = (v < 0);
                unsigned uv  = neg ? (unsigned)(-v) : (unsigned)v;
                char    *p   = pf_utoa(uv, 10, lower, buf + sizeof(buf) - 1);

                if (neg) {
                    /* Con padding a zeri il segno va PRIMA degli zeri
                     * ("-0042", non "00-42"). */
                    if (pad == '0' && !left_align && width > 0) {
                        putchar('-'); total++; width--;
                    } else {
                        *--p = '-';
                    }
                }
                total += pf_pad(p, width, left_align, pad);
                break;
            }
            case 'u': {
                unsigned v = __builtin_va_arg(args, unsigned);
                total += pf_pad(pf_utoa(v, 10, lower, buf + sizeof(buf) - 1),
                                width, left_align, pad);
                break;
            }
            case 'x': {
                unsigned v = __builtin_va_arg(args, unsigned);
                total += pf_pad(pf_utoa(v, 16, lower, buf + sizeof(buf) - 1),
                                width, left_align, pad);
                break;
            }
            case 'X': {
                unsigned v = __builtin_va_arg(args, unsigned);
                total += pf_pad(pf_utoa(v, 16, upper, buf + sizeof(buf) - 1),
                                width, left_align, pad);
                break;
            }
            case 'o': {
                unsigned v = __builtin_va_arg(args, unsigned);
                total += pf_pad(pf_utoa(v, 8, lower, buf + sizeof(buf) - 1),
                                width, left_align, pad);
                break;
            }
            case 'p': {
                unsigned v = __builtin_va_arg(args, unsigned);
                putchar('0'); putchar('x'); total += 2;
                total += pf_pad(pf_utoa(v, 16, lower, buf + sizeof(buf) - 1),
                                8, 0, '0');
                break;
            }
            case 's': {
                const char *s = __builtin_va_arg(args, const char *);
                if (!s) s = "(null)";
                total += pf_pad(s, width, left_align, ' ');
                break;
            }
            case 'c': {
                char c = (char)__builtin_va_arg(args, int);
                char cs[2];
                cs[0] = c; cs[1] = '\0';
                total += pf_pad(cs, width, left_align, ' ');
                break;
            }
            case '%':
                putchar('%'); total++;
                break;
            default:
                /* Specificatore sconosciuto: stampa letterale, nessun
                 * argomento consumato (non possiamo sapere il tipo). */
                putchar('%'); putchar(spec); total += 2;
                break;
        }
    }

    __builtin_va_end(args);
    return total;
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
    _syscall1(SYS_EXIT, (uint32_t)code);
    for (;;) {}
}

void abort(void)
{
    /* Causa un fault intenzionale */
    exit(134);
}

/* Heap utente semplice tramite sbrk */
static char *heap_ptr  = NULL;
static char *heap_base = NULL;

void *malloc(size_t size)
{
    if (!heap_ptr) {
        /* Prima allocazione: inizializza heap tramite sbrk(0) */
        heap_base = (char *)(int)_syscall1(SYS_SBRK, 0);
        heap_ptr  = heap_base;
    }

    /* Allinea a 8 byte */
    size = (size + 7) & ~7u;

    /* Espandi heap */
    char *old = heap_ptr;
    int32_t ret = _syscall1(SYS_SBRK, size);
    if (ret < 0) return NULL;

    heap_ptr = old + size;
    return old;
}

void free(void *ptr)
{
    /* Implementazione semplificata: no-op
     * Una vera implementazione richiede un allocatore completo.
     * TODO: implementare free list. */
    (void)ptr;
}

void *calloc(size_t nmemb, size_t size)
{
    void *p = malloc(nmemb * size);
    if (p) memset(p, 0, nmemb * size);
    return p;
}

void *realloc(void *ptr, size_t size)
{
    /* Semplificazione: alloca nuovo blocco e copia */
    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    if (ptr) {
        /* Copia i dati — non conosciamo la dimensione del vecchio blocco
         * quindi copiamo 'size' byte (può leggere leggermente oltre, ma
         * su EX-OS è accettabile in questa fase) */
        memcpy(new_ptr, ptr, size);
    }
    return new_ptr;
}

/* =============================================================================
 * Funzioni di utilità aggiuntive
 * ============================================================================= */

int open(const char *path, int flags)
{
    return _syscall3(SYS_OPEN, (uint32_t)path, (uint32_t)flags, 0);
}

int close(int fd)
{
    return _syscall1(SYS_CLOSE, (uint32_t)fd);
}

ssize_t read(int fd, void *buf, size_t n)
{
    return _syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buf, n);
}

ssize_t write(int fd, const void *buf, size_t n)
{
    return _syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, n);
}

int getpid(void)
{
    return _syscall1(SYS_GETPID, 0);
}

int chdir(const char *path)
{
    return _syscall1(SYS_CHDIR, (uint32_t)path);
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
    return (int)_syscall2(SYS_TRUNCATE, (uint32_t)path, size);
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
    return (int)_syscall1(SYS_MKDIR, (uint32_t)path);
}

int rmdir(const char *path)
{
    return (int)_syscall1(SYS_RMDIR, (uint32_t)path);
}

int unlink(const char *path)
{
    return (int)_syscall1(SYS_UNLINK, (uint32_t)path);
}

int getconf(const char *key, char *buf, size_t size)
{
    return (int)_syscall3(SYS_GETENV, (uint32_t)key, (uint32_t)buf,
                          (uint32_t)size);
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

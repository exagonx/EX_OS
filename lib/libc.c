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

/* ! DEVONO COINCIDERE CON QUELLI DI lib/include/libc.h, riga per riga.
 * Questo file non include il proprio header — e' autosufficiente di
 * proposito, vedi il commento qui sopra — quindi i due elenchi sono
 * separati e un disaccordo si manifesta come "conflicting types" al primo
 * programma che li usa entrambi. Qui ci sono solo quelli che servono alle
 * funzioni definite in questo file; gli altri stanno solo nell'header. */
typedef int                 pid_t;
typedef long                off_t;
typedef unsigned int        mode_t;
/* ! Gli altri tipi di POSIX che compaiono in `struct stat`. Duplicati da
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
/* ! wint_t, duplicato da lib/include/libc.h e da tenere identico. Ci
 * vuole perche' wchar_t arriva da <stddef.h> — che e' del compilatore —
 * ma wint_t no: quello e' della libreria, e le isw* e tow* lo prendono
 * come argomento. */
typedef int                 wint_t;
#define WEOF                ((wint_t)-1)

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

/* Duplicati da lib/include/inttypes.h. ! intmax_t E' `long long` su questo
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
 * errno — e perche' le funzioni POSIX ritornano -1, non -errno
 *
 * ! QUESTA REGOLA E' STATA ROVESCIATA AD AGOSTO 2026, e vale la pena
 * lasciare scritto sia cosa diceva prima sia perche' non reggeva.
 *
 * Diceva: su Unix una syscall fallita ritorna -1 e mette il motivo in
 * errno; qui ritorna l'errore NEGATIVO (-2 = ENOENT, -30 = EROFS), tutti i
 * programmi di /bin stampano quel numero, `< 0` resta il test giusto in
 * entrambi i mondi e un -EIO dice piu' di un -1.
 *
 * Il ragionamento e' onesto e vale finche' TUTTI i chiamanti sono nostri.
 * Non lo sono piu'. Il codice di terzi non scrive `< 0`: scrive
 * `!= -1`, perche' e' cio' che lo standard promette. Da libcpp, il
 * preprocessore di GCC:
 *
 *     file->fd = open (file->path, O_RDONLY | O_BINARY, 0666);
 *     if (file->fd != -1)
 *         { fstat (file->fd, &file->st); ... }
 *
 * Con un open() che rende -2 per «non esiste», quel test passa. cc1
 * proseguiva credendo di avere un descrittore, faceva fstat(-2) —
 * che fallisce con EBADF — e moriva con «fatal error: stdio.h:
 * descrittore non valido» su un header che semplicemente non stava in
 * QUELLA directory e stava benissimo nella successiva. Il messaggio
 * accusava il file sbagliato con l'errore sbagliato.
 *
 * Percio': le funzioni con un nome POSIX (open, read, write, close,
 * waitpid...) ritornano -1 e basta, e il codice sta in errno. Le funzioni
 * NOSTRE — quelle che non esistono altrove, come listdir_from() o
 * mount() — tengono il -errno, perche' li' nessuno arriva con
 * un'aspettativa da standard e il numero e' piu' informativo.
 * ============================================================================= */
/* ! DENTRO QUESTO FILE `errno` E' UNA MACRO, non questa variabile, dal 4
 * settembre 2026: vedi la riga subito sotto. Questa resta come posto di
 * scorta — per chi non ha un blocco TLS — e come definizione unica del
 * simbolo. */
int errno_condiviso = 0;

int *__errno_dove(void);
#define errno (*__errno_dove())

/* Dichiarata qui e non accanto a __libc_distruttori_registra(): la usa exit(),
 * che in questo file viene molto prima. */
static void (*g_distruttori_prog)(void) = 0;

/* Informazioni su un file (SYS_STAT). Duplicata da
 * kernel/include/syscall.h, come le altre strutture che attraversano
 * l'ABI. st_attr usa le convenzioni FAT anche sugli altri filesystem:
 * 0x10 = directory, 0x01 = sola lettura. */
typedef struct {
    uint32_t    st_size;
    uint32_t    st_ident;
    uint16_t    st_attr;
    uint16_t    st_date;
    uint16_t    st_time;
} Stat;

/* ! QUI C'ERANO DUE MACRO CON IL NOME SBAGLIATO (tolte nel 0.150):
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
/* I fili (4 settembre 2026): stessi numeri di kernel/include/syscall.h. */
#define SYS_THREAD_CREA    201
#define SYS_THREAD_ESCI    202
#define SYS_THREAD_ATTENDI 203
#define SYS_ATTESA_DORMI   204
#define SYS_ATTESA_SVEGLIA 205
#define SYS_THREAD_FERMA    206
#define SYS_THREAD_STACCA   217
#define SYS_THREAD_PILA     218
#define SYS_PROC_GRUPPO     164
/* Signals and mprotect (kernel 0.227): see "SIGNALS, DELIVERED BY THE KERNEL". */
#define SYS_MPROTECT        125
#define SYS_SEG_AZIONE      165
#define SYS_SEG_MASCHERA    166
#define SYS_SEG_RITORNO     167
#define SYS_SEG_PILA        168
#define SYS_SEG_MANDA       169
#define SYS_THREAD_FERMARSI 207
#define SYS_GETPID      20
#define SYS_GETPPID     64
/* ! I NUMERI SONO DUPLICATI DA kernel/include/syscall.h, come tutti gli altri
 * qui sopra: libc.c non include quell'header. Sono quelli di Linux. */
#define SYS_SETUID      23
#define SYS_GETUID      24
#define SYS_CHOWN      182
#define SYS_CHMOD       15
#define SYS_FB_MAP     249
#define SYS_INTERROMPI 250
#define SYS_PTY_APRI   251
#define SYS_PTY_CTL    252
#define SYS_STATPERM   253
#define SYS_SU         254
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
#define SYS_FSTAT       108
#define SYS_FTRUNCATE   109
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
#define SYS_FDPROVA       198
#define SYS_KBPROVA       199

/* ! DEVE RESTARE IDENTICA a FdPasso in kernel/include/syscall.h e in
 * lib/include/libc.h. TRE copie, come per DmaZona e ShmZona, e per la stessa
 * ragione: questo file non include libc.h. La riempie il kernel scrivendo
 * nella memoria del processo, quindi due definizioni che divergono danno
 * numeri letti dal campo sbagliato — una diagnostica che mente e' peggio di
 * una che non c'e'. */
typedef struct {
    unsigned int passo;
    unsigned int codice;
    int          esito;
    unsigned int a, b;
} FdPasso;
#define SYS_IRQ_UNBIND    219
#define SYS_IRQ_DONE      237
#define SYS_DMA_ALLOC     239
#define SYS_RANDOM        240

/* ! DEVE RESTARE IDENTICA a DmaZona in kernel/include/syscall.h e in
 * lib/include/libc.h. La riempie il kernel scrivendo nella memoria del
 * processo: due definizioni che divergono danno un indirizzo fisico letto
 * dal campo sbagliato, cioe' una scheda che fa DMA dove capita. */
typedef struct {
    unsigned int byte;
    unsigned int virt;
    unsigned int fisico;
} DmaZona;

#define SYS_MMIO_MAP      241
#define SYS_BLK_OFFRI     208
#define SYS_BLK_ATTENDI   209
#define SYS_BLK_RISPOSTA  210
#define SYS_BLK_SCANSIONA 211
#define SYS_BLK_ESPELLI   212

/* ! DEVE RESTARE IDENTICA a MmioZona in kernel/include/syscall.h e in
 * lib/include/libc.h. E' la TERZA copia: questo file non include libc.h e
 * ridichiara tutto a mano. Due definizioni che divergono darebbero un
 * indirizzo fisico letto dal campo sbagliato — cioe' i registri di una
 * scheda mappati altrove, o peggio un pezzo di RAM. */
typedef struct {
    unsigned int fisico;
    unsigned int byte;
    unsigned int virt;
} MmioZona;

#define SYS_SHM_APRI      242
#define SYS_SHM_CHIUDI    243
#define SHM_CREA          0x0001

/* ! DEVE RESTARE IDENTICA a ShmZona in kernel/include/syscall.h e in
 * lib/include/libc.h — la TERZA copia, perche' questo file non include
 * libc.h. Qui il campo che non si puo' sbagliare e' `byte`: entra come
 * richiesta ed esce come dimensione VERA della zona. */
typedef struct {
    char         nome[16];
    unsigned int byte;
    unsigned int flag;
    unsigned int virt;
} ShmZona;

#define SYS_POLL          244
#define SYS_MODO_TESTO    245
#define SYS_REBOOT        88
#define SYS_VIDEO_INFO    246
#define SYS_LOG           247

/* ! DEVE RESTARE IDENTICA a struct pollfd in kernel/include/syscall.h e in
 * lib/include/libc.h — la TERZA copia, perche' questo file non include
 * libc.h. I campi sono int e short come su POSIX: sono 8 byte, e il kernel
 * copia l'elenco contando su quella misura. */
struct pollfd {
    int   fd;
    short events;
    short revents;
};

#define POLL_FD_IPC       128   /* = FD_IPC, dal kernel 0.232 */
#define POLL_IN       0x0001
#define POLL_OUT      0x0004

/* fd_set: una maschera a 32 bit, uno per descrittore. MAX_FD del kernel e' 32,
 * quindi ci sta tutto. Definizione ripetuta in lib/include/libc.h. */
#define FD_SETSIZE  32
typedef struct { unsigned int bit; } fd_set;
#define FD_ZERO(s)      ((s)->bit = 0u)
#define FD_SET(f, s)    ((s)->bit |=  (1u << (f)))
#define FD_CLR(f, s)    ((s)->bit &= ~(1u << (f)))
#define FD_ISSET(f, s)  ((((s)->bit) >> (f)) & 1u)

#define SYS_IPC_RECV_TMO  228
#define SYS_TIME           13
#define SYS_TIME_SET      213
#define SYS_CONSOLE_SWITCH 229
#define SYS_CONSOLE_WRITE  230
#define SYS_CONSOLE_INFO   231
#define SYS_CONSOLE_SETFG  232
#define SYS_CONSOLE_GRAFICA 255
#define SYS_CONSOLE_TESTO  214
#define SYS_CONSOLE_REGISTRO 215
#define SYS_CONSOLE_CTRLC  216
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
 * (DirEntry) e a lib/include/libc.h: attraversa l'ABI della syscall.
 *
 * ! SONO TRE COPIE DELLA STESSA STRUTTURA e vanno cambiate insieme. Questa
 * e' la terza, e c'e' perche' libc.c non include libc.h (si compila da
 * sola). `ident` e' arrivato ad agosto 2026: vedi il commento accanto alla
 * copia del kernel per cosa costava lo zero che c'era al posto suo. */
#define DIRENT_NAME_MAX 256
typedef struct {
    char           name[DIRENT_NAME_MAX];
    unsigned int   size;
    unsigned int   ident;
    unsigned char  is_dir;
} DirEntry;

/* Nomi degli errori. Stessa convenzione del resto del file: duplicati da
 * lib/include/libc.h perche' libc.c si compila SENZA -I, e allineati alla
 * tabella di strerror() qui sotto. */
#define EPERM         1
#define ENOENT        2
#define ENXIO         6
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
#define EOVERFLOW    75
#define ERANGE       34
#define ETIMEDOUT   110

/* La lunghezza massima di un percorso: VFS_PATH_MAX del kernel, e le due
 * devono restare uguali. Duplicata anche in <limits.h> e <sys/param.h>. */
#define PERCORSO_MAX 320

/* Numero massimo di voci per chiamata a listdir: vedi libc.h. */
#define LISTDIR_MAX_BATCH 16

/* spawn con ambiente e redirezioni — duplicate da kernel/include/syscall.h
 * e da lib/include/libc.h. La magia impedisce al kernel di leggere ESI
 * quando lo chiama un programma compilato per la vecchia forma. */
/* ! La magia e' 0x53504E59 e non piu' ...58: e' cambiata la disposizione
 * di SpawnAzione. Vedi lib/include/libc.h. */
/* ! UNA DEFINIZIONE SOLA dal 17 agosto 2026: ce n'erano quattro e una e'
 * rimasta indietro, costando alla shell redirezioni e ambiente per tre
 * giorni senza un messaggio. Vedi lib/include/spawn_abi.h.
 *
 * ! IL PERCORSO E' RELATIVO E NON PASSA DA -I, perche' libc.c viene compilata
 * da una ventina di regole del Makefile e non tutte hanno `-I lib/include`.
 * Scriverlo relativo lo fa trovare a tutte senza toccarne nemmeno una. */
#include "include/spawn_abi.h"

typedef struct {
    int         fd;
    int         flags;
    const char *percorso;   /* NULL = passa il descrittore `fd_padre` */
    int         fd_padre;
} SpawnRedir;

int spawn_ex(const char *path, char *const argv[], char *const envp[],
             const SpawnRedir *redir, int n_redir);
int spawn_su_console(const char *path, char *const argv[], char *const envp[],
                     const SpawnRedir *redir, int n_redir, int console);

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
#define SIGBUS   7
#define SIGSTOP 19

/* The signal types, duplicated from lib/include/libc.h like everything else
 * here (kernel 0.227, @SEGNALI). The layouts are explained there. */
typedef unsigned int sigset_t;
typedef struct {
    int si_signo;
    int si_errno;
    int si_code;
    union {
        void *si_addr;
        struct { int si_pid; unsigned int si_uid; } _mandato;
        int _resto[29];
    } _campi;
} siginfo_t;
struct sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, siginfo_t *, void *);
    } _gestore;
    sigset_t sa_mask;
    int      sa_flags;
};
#define sa_handler   _gestore.sa_handler
#define sa_sigaction _gestore.sa_sigaction
#define SA_SIGINFO   0x00000004
#define SA_RESTART   0x10000000
#define SA_RESETHAND 0x80000000
#define SIG_BLOCK    0
#define SIG_UNBLOCK  1
#define SIG_SETMASK  2
typedef struct { void *ss_sp; int ss_flags; size_t ss_size; } stack_t;
typedef struct {
    int           gregs[19];
    void         *fpregs;
    unsigned long oldmask;
    unsigned long cr2;
} mcontext_t;
typedef struct ucontext_t {
    unsigned long      uc_flags;
    struct ucontext_t *uc_link;
    stack_t            uc_stack;
    mcontext_t         uc_mcontext;
    sigset_t           uc_sigmask;
} ucontext_t;
typedef unsigned int sigjmp_buf[8];
int  sigprocmask(int come, const sigset_t *nuova, sigset_t *prima);
void longjmp(unsigned int *env, int val) __attribute__((noreturn));

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
#define _SC_PHYS_PAGES         85
#define _SC_GETPW_R_SIZE_MAX   70
#define _SC_AVPHYS_PAGES       86

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

/* Proprietario e permessi — deve restare identico a kernel/include/syscall.h
 * (StatPerm) e a lib/include/libc.h: attraversa l'ABI della syscall, e la
 * chiamata porta con se' il sizeof proprio per accorgersene. */
typedef struct {
    unsigned short modo;
    unsigned short uid;
    unsigned short gid;
} StatPerm;


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
/* ! TERZA COPIA, e il 9 settembre 2026 e' stata quella dimenticata.
 * Aggiungendo `guasto` a BlkInfo sono state corrette syscall.h e libc.h ma non
 * questa: sys_blkinfo controlla che il chiamante dichiari la STESSA misura
 * (`size != sizeof(BlkInfo)` -> EINVAL), quindi la libc chiedeva 40 byte a un
 * kernel che ne voleva 44 e l'elenco tornava VUOTO. Il sintomo era «'ram0' non
 * esiste» su un dispositivo perfettamente registrato, e `disk` che non
 * mostrava piu' nemmeno il CD.
 *
 * Quel controllo ha funzionato: ha rifiutato invece di riempire una struttura
 * della misura sbagliata. Vale la pena di ricordarlo quando si e' tentati di
 * toglierlo. */
typedef struct {
    char         nome[BLKINFO_NOME_MAX];
    unsigned int tipo;
    unsigned int sola_lettura;
    unsigned int primo_lo, primo_hi;
    unsigned int settori_lo, settori_hi;
    unsigned int guasto;
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

/* Il filtro di ipc_scegli, e le tre risposte che puo' dare. Deve restare
 * identico a lib/include/libc.h, dove sta anche il perche'. */
#define IPC_ALTRUI   0
#define IPC_MIO      1
#define IPC_BUTTA  (-1)

/* Una scadenza che vuol dire «non aspettare affatto»: vedi ipc_scegli. */
#define IPC_SUBITO  ((unsigned int)-1)

typedef int (*IpcFiltro)(const IpcMessage *meta, void *dato);

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
 * ! 64 BIT E NON 32, dal kernel 0.175 — e non e' solo il 2038
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

    /* ! GLI ULTIMI DUE CAMPI ESISTONO DA QUANDO SERVONO A QUALCUNO — li
     * chiede QuickJS, e stanno in POSIX dal 2024. Valgono sempre 0 e «UTC»
     * perche' EX-OS non sa in che fuso si trova, e un fuso inventato sarebbe
     * peggio che nessun campo. Il perche' per esteso e' in lib/include/libc.h,
     * accanto al gemello di questa struttura: le due definizioni vanno tenute
     * allineate a mano, come dice il commento in testa a questo file. */
    long        tm_gmtoff;
    const char *tm_zone;
};

/* ! tv_sec E' time_t, NON long, E QUESTA RIGA E' COSTATA CARA.
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
#ifndef MAP_SHARED                      /* gli stessi valori di lib/include/libc.h */
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#endif
#define MAP_FAILED      ((void *)-1)
#define RUSAGE_SELF      0
#define RUSAGE_CHILDREN (-1)

/* Duplicata da lib/include/libc.h. ! Solo `ru_utime` e `ru_stime` vengono
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

/* Duplicata da lib/include/libc.h come tutto il resto. ! La risoluzione
 * vera e' 10 ms: tv_nsec e' sempre un multiplo di 10 000 000. ! tv_sec e'
 * time_t dal 29 settembre 2026: vedi l'header. */
struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};
#define TIME_UTC 1

/* Le convenzioni numeriche della locale. Duplicata da lib/include/libc.h:
 * ! l'ORDINE DEI CAMPI deve combaciare riga per riga, non solo i nomi —
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

/* ! I TIPI DI POSIX, non `unsigned int`: deve combaciare CAMPO PER CAMPO
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
 * mem_parola — il tipo con cui si leggono quattro byte per volta
 *
 * ! LEGGERE BYTE ATTRAVERSO UN uint32_t* E' ESATTAMENTE CIO' CHE LA REGOLA DI
 * ALIASING DI C VIETA, e -O2 senza -fno-strict-aliasing e' il posto in cui
 * quella regola si paga: il compilatore ha il diritto di dare per scontato che
 * una scrittura a byte e una lettura a parola non tocchino la stessa memoria, e
 * di riordinarle. may_alias glielo toglie, dichiarandolo invece di sperare che
 * non se ne accorga. Costa una riga e toglie una classe di difetti che si
 * manifestano lontano da dove sono stati fatti.
 *
 * Serve a strlen qui sotto e a tutte le funzioni memoria piu' avanti, ed e'
 * dichiarato qui perche' il primo che lo usa e' strlen.
 * ============================================================================= */
typedef uint32_t __attribute__((__may_alias__)) mem_parola;

/* =============================================================================
 * Funzioni stringa
 * ============================================================================= */

/* ! strlen A PAROLE — il trucco di Mycroft, e perche' e' SICURO
 *
 * (v - 0x01010101) & ~v & 0x80808080 e' diverso da zero se e solo se una delle
 * quattro colonne della parola vale zero: il prestito del sottrarre uno accende
 * il bit alto di quel byte, e ~v lo tiene acceso solo dove il byte era nullo.
 * Quattro byte esaminati con tre operazioni invece che con quattro confronti.
 *
 * ! LEGGERE QUATTRO BYTE DOVE IL CHIAMANTE NE HA GARANTITI MENO SAREBBE UNA
 * LETTURA FUORI BUFFER, e la si evita in un modo solo: prima si avanza a byte
 * fino ad allineare il puntatore a quattro, POI si legge a parole. Una parola
 * allineata non attraversa mai il confine di una pagina, quindi o la pagina e'
 * la stessa da cui si stava gia' leggendo, o il byte nullo si e' gia' trovato.
 * Saltare l'allineamento fa una libreria che funziona per anni e poi prende un
 * page fault su una stringa che finisce in fondo a una pagina.
 *
 * ! LA CODA TORNA AI BYTE APPOSTA: la parola dice CHE c'e' uno zero, non DOVE.
 */
size_t strlen(const char *s)
{
    const char       *p = s;
    const mem_parola *w;

    while (((unsigned long)p & 3u) != 0) {
        if (*p == '\0') return (size_t)(p - s);
        p++;
    }

    for (w = (const mem_parola *)p;
         ((*w - 0x01010101u) & ~*w & 0x80808080u) == 0;
         w++) { }

    p = (const char *)w;
    while (*p) p++;
    return (size_t)(p - s);
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

/* =============================================================================
 * strncpy — e il post-decremento che azzerava tutta la memoria
 *
 * ! QUI C'ERA UN DIFETTO CHE SI VEDEVA SOLO NEL CASO PER CUI strncpy
 * ESISTE, cioe' quando la sorgente NON ci sta in n. Le due righe erano:
 *
 *     while (n-- && (*d++ = *src++));
 *     while (n--) *d++ = '\0';
 *
 * Con una sorgente piu' corta di n il conto torna. Con una sorgente lunga
 * almeno n, il primo ciclo esce perche' `n--` VALE 0 — ma il
 * post-decremento scatta lo stesso, e n e' un size_t: da 0 passa a
 * SIZE_MAX. Il secondo ciclo si mette allora a scrivere zeri per quattro
 * miliardi di byte, e si ferma solo quando incontra una pagina non
 * mappata.
 *
 * Il sintomo non somigliava alla causa: dentro EX-OS usciva come
 *
 *     [FAULT] PID 12 '/cdrom/exos/libexec/gcc/i386-ex': page fault a
 *             0x0813b000 (pagina assente, scrittura, EIP=0x080aeef0)
 *
 * — collect2 che muore scrivendo esattamente al confine del proprio heap,
 * cioe' l'aspetto di un allocatore rotto. L'allocatore non c'entrava
 * niente: era questa funzione che gli passava sopra.
 *
 * ! La gemella larga, wcsncpy() piu' in basso, era gia' scritta bene —
 * `while (n && ...)` con il decremento DENTRO il corpo. Vale la pena
 * saperlo: quando due funzioni fanno la stessa cosa su tipi diversi, la
 * differenza fra le due e' il posto dove guardare per primo.
 * ============================================================================= */
char *strncpy(char *dst, const char *src, size_t n)
{
    char *d = dst;

    while (n > 0 && *src) { *d++ = *src++; n--; }

    /* Il riempimento con zeri fino a n e' lo standard, ed e' anche la
     * parte che tutti dimenticano — compreso il caso n == 0, in cui NON
     * si scrive niente e nemmeno il terminatore. */
    while (n > 0) { *d++ = '\0'; n--; }

    return dst;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* ! QUI IL TRABOCCO DI `n` E' VOLUTO, ed e' l'opposto del difetto che
 * strncpy aveva poco piu' sopra: uscire dal ciclo con `n` passato a
 * SIZE_MAX significa «li ho confrontati tutti e n, ed erano uguali», ed e'
 * proprio cio' che il confronto con (size_t)-1 riconosce. La differenza
 * con strncpy e' che li' `n` veniva RIUSATO da un secondo ciclo, qui no.
 *
 * La stessa funzione, copiata, sta in kernel/fs/cfg.c e in bin/sh/shell.c,
 * che non possono usare la libc: se si tocca una, si toccano tutte e tre. */
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
 * ! RITORNA LA LUNGHEZZA DELL'ORIGINALE, non quella copiata, ed e' cio'
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
 * ! QUESTO VUOL DIRE CHE NON FALLISCE MAI, e chi la chiama per VERIFICARE
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
        /* ! Un carattere sopra 255 non ci sta in un byte: qui la
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
 * ! IL VALORE DI RITORNO E' int E NON size_t, al contrario delle `mbr*`
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
 *
 * ! QUI C'ERA UN BYTE PER VOLTA, E QUANTO COSTAVA E' STATO MISURATO.
 * `while (n--) *d++ = *s++;` in RAM non si vede — la cache assorbe, 357 MB/s —
 * ma verso il framebuffer sono 47 MB/s contro i 375 della stessa copia fatta a
 * otto byte: OTTO VOLTE. La misura e' di bin/fbprova sull'Acer Aspire 3000
 * (SiS 6330, 800x600x32), 16 settembre 2026, ed e' quella che ha giustificato
 * questa riscrittura. Il kernel la copia a parole ce l'aveva gia'
 * (kernel/arch/x86/memfun.c): era solo lo spazio utente a pagarla.
 *
 * -----------------------------------------------------------------------------
 * ! TRE STRADE, E SI SCEGLIE SULLA MISURA DEL BLOCCO, non su quella della
 * macchina:
 *
 *   sotto 32 byte          byte per volta. Un blocco corto e' il caso NORMALE
 *                          — una struttura, un nome di file, una riga — e li'
 *                          il prologo che allinea costa piu' di quanto faccia
 *                          risparmiare.
 *   da 32 byte in su       parole da 32 bit.
 *   da 256 byte in su      MMX, otto byte per store.
 *
 * ! LE DUE SOGLIE NON SONO TONDE PER CASO: sono i due punti in cui bin/memprova
 * vede le curve incrociarsi. Alzarle o abbassarle senza rifare quella misura
 * vuol dire disfare una prova gia' pagata.
 *
 * ! E SI ALLINEA LA DESTINAZIONE, NON LA SORGENTE. Su x86 un carico non
 * allineato costa poco; uno STORE non allineato che attraversa una riga di
 * cache costa, e sulla memoria video write-combining ROMPE LA COMBINAZIONE —
 * cioe' proprio il meccanismo per cui li' MMX vale il doppio. E' la scelta di
 * musl e di glibc, per questa ragione e non per tradizione.
 *
 * -----------------------------------------------------------------------------
 * ! MMX SI PUO' USARE DENTRO UNA FUNZIONE DI LIBRERIA, E NON E' OVVIO. I
 * registri MMX sono quelli dell'x87: sporcarli qui e' lecito solo perche'
 *
 *   (a) il kernel salva lo stato FPU al cambio di contesto — fnsave/fxsave con
 *       commutazione pigra via CR0_TS — quindi due processi non se li
 *       calpestano a vicenda;
 *   (b) l'ABI i386 dichiara lo stack x87 VUOTO al momento della chiamata,
 *       quindi nessun chiamante ha valori vivi li' dentro da perdere.
 *
 * E' lo stesso ragionamento che ha permesso MMX nel compositore di wserver.
 *
 * ! E SI CHIAMA emms ALLA FINE DI OGNI GIRO. Senza, la prima istruzione in
 * virgola mobile che arriva dopo — anche in un ALTRO processo, se lo scheduler
 * entra prima — trova uno stack x87 che non e' suo. E' l'errore classico di
 * MMX, e non da' nessun sintomo finche' qualcuno non usa la virgola mobile.
 *
 * ! SI GUARDA CPUID UNA VOLTA SOLA. La CPU di base e' -march=pentium-mmx,
 * quindi MMX ci sarebbe per definizione; si controlla lo stesso, perche' una
 * istruzione che non c'e' non e' un numero brutto, e' una #UD. Stessa scelta,
 * e stessa forma, di wserver.c e di fbprova.
 *
 * ! LE PAROLE SI LEGGONO CON mem_parola, che e' un uint32_t may_alias: il
 * perche' sta dove e' dichiarato, sopra le funzioni stringa.
 * ============================================================================= */

#define MEM_SOGLIA_PAROLA   32u     /* sotto: il byte per byte conviene */
#define MEM_SOGLIA_MMX     256u     /* sotto: le parole bastano         */

/* -1 = non ancora guardato. L'inizializzatore la porta in .data e non nel BSS,
 * ed e' voluto: 0 vorrebbe dire «gia' guardato, e MMX non c'e'», cioe' il
 * ripiego a byte per sempre su una macchina che MMX ce l'ha. */
static int g_mem_mmx = -1;

static int mem_ha_mmx(void)
{
    unsigned int a, b, c, d;

    if (g_mem_mmx >= 0) return g_mem_mmx;

    __asm__ __volatile__("cpuid"
                         : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                         : "a"(1));
    g_mem_mmx = (d & (1u << 23)) ? 1 : 0;
    return g_mem_mmx;
}

/* Copia a blocchi di 64 byte. La destinazione e' gia' allineata a otto; la
 * sorgente puo' non esserlo, e movq non se ne lamenta (l'allineamento lo
 * pretende movdqa di SSE, non MMX). */
static void mem_copia_mmx(unsigned char *d, const unsigned char *s,
                          unsigned int blocchi)
{
    __asm__ __volatile__(
        "1:\n\t"
        "movq   0(%1), %%mm0\n\t"
        "movq   8(%1), %%mm1\n\t"
        "movq  16(%1), %%mm2\n\t"
        "movq  24(%1), %%mm3\n\t"
        "movq  32(%1), %%mm4\n\t"
        "movq  40(%1), %%mm5\n\t"
        "movq  48(%1), %%mm6\n\t"
        "movq  56(%1), %%mm7\n\t"
        "movq  %%mm0,  0(%0)\n\t"
        "movq  %%mm1,  8(%0)\n\t"
        "movq  %%mm2, 16(%0)\n\t"
        "movq  %%mm3, 24(%0)\n\t"
        "movq  %%mm4, 32(%0)\n\t"
        "movq  %%mm5, 40(%0)\n\t"
        "movq  %%mm6, 48(%0)\n\t"
        "movq  %%mm7, 56(%0)\n\t"
        "addl  $64, %0\n\t"
        "addl  $64, %1\n\t"
        "decl  %2\n\t"
        "jnz   1b\n\t"
        "emms"
        : "+r"(d), "+r"(s), "+r"(blocchi)
        :
        : "memory", "cc");
}

/* Riempimento a blocchi di 64 byte, con la parola gia' replicata negli otto
 * byte di *q. */
static void mem_riempi_mmx(unsigned char *d, const unsigned long long *q,
                           unsigned int blocchi)
{
    __asm__ __volatile__(
        "movq  (%2), %%mm0\n\t"
        "1:\n\t"
        "movq  %%mm0,  0(%0)\n\t"
        "movq  %%mm0,  8(%0)\n\t"
        "movq  %%mm0, 16(%0)\n\t"
        "movq  %%mm0, 24(%0)\n\t"
        "movq  %%mm0, 32(%0)\n\t"
        "movq  %%mm0, 40(%0)\n\t"
        "movq  %%mm0, 48(%0)\n\t"
        "movq  %%mm0, 56(%0)\n\t"
        "addl  $64, %0\n\t"
        "decl  %1\n\t"
        "jnz   1b\n\t"
        "emms"
        : "+r"(d), "+r"(blocchi)
        : "r"(q)
        : "memory", "cc");
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint8_t  v = (uint8_t)c;

    if (n >= MEM_SOGLIA_PAROLA) {
        uint32_t parola;

        while (((unsigned long)d & 7u) != 0) { *d++ = v; n--; }

        parola = ((uint32_t)v << 24) | ((uint32_t)v << 16) |
                 ((uint32_t)v << 8)  |  (uint32_t)v;

        if (n >= MEM_SOGLIA_MMX && mem_ha_mmx()) {
            unsigned long long q = ((unsigned long long)parola << 32) | parola;
            unsigned int blocchi = (unsigned int)(n >> 6);

            mem_riempi_mmx(d, &q, blocchi);
            d += (size_t)blocchi << 6;
            n -= (size_t)blocchi << 6;
        }

        while (n >= 4) {
            *(mem_parola *)d = parola;
            d += 4; n -= 4;
        }
    }

    while (n--) *d++ = v;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (n >= MEM_SOGLIA_PAROLA) {
        while (((unsigned long)d & 7u) != 0) { *d++ = *s++; n--; }

        if (n >= MEM_SOGLIA_MMX && mem_ha_mmx()) {
            unsigned int blocchi = (unsigned int)(n >> 6);

            if (blocchi) {
                mem_copia_mmx(d, s, blocchi);
                d += (size_t)blocchi << 6;
                s += (size_t)blocchi << 6;
                n -= (size_t)blocchi << 6;
            }
        }

        while (n >= 4) {
            *(mem_parola *)d = *(const mem_parola *)s;
            d += 4; s += 4; n -= 4;
        }
    }

    while (n--) *d++ = *s++;
    return dst;
}

/* ! memmove DEVE FUNZIONARE ANCHE QUANDO LE DUE ZONE SI SOVRAPPONGONO: copiando
 * in avanti su una sovrapposizione si riscrivono i byte che non si sono ancora
 * letti. In avanti non c'e' niente da inventare — e' memcpy — e all'indietro le
 * parole si possono usare lo stesso, purche' si scenda a blocchi interi.
 *
 * ! E ALL'INDIETRO E' SICURO PROPRIO PERCHE' dst > src: si scrive sempre a un
 * indirizzo PIU' ALTO di quello che si dovra' ancora leggere, quindi il blocco
 * appena scritto non puo' coprire byte non ancora presi. Se fosse dst < src
 * varrebbe il contrario, e infatti quel caso finisce in memcpy. */
void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s || n == 0) return dst;
    if (d < s) return memcpy(dst, src, n);

    d += n;
    s += n;

    if (n >= MEM_SOGLIA_PAROLA) {
        while (((unsigned long)d & 3u) != 0) { *--d = *--s; n--; }

        while (n >= 4) {
            d -= 4; s -= 4; n -= 4;
            *(mem_parola *)d = *(const mem_parola *)s;
        }
    }

    while (n--) *--d = *--s;
    return dst;
}

/* ! IL CONFRONTO A PAROLE SI FERMA SULLA PRIMA PAROLA DIVERSA E POI TORNA AI
 * BYTE, e non e' un ripiego: memcmp deve rendere il segno del PRIMO byte
 * diverso, e quale sia dentro la parola lo dice solo il confronto a byte. La
 * parola serve a saltare in fretta tutto cio' che e' uguale, che nel caso
 * normale e' quasi tutto. */
int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;

    if (n >= MEM_SOGLIA_PAROLA) {
        while (n >= 4) {
            if (*(const mem_parola *)pa != *(const mem_parola *)pb) break;
            pa += 4; pb += 4; n -= 4;
        }
    }

    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

/* =============================================================================
 * STDIO — flussi bufferizzati
 *
 * ! COSA C'ERA PRIMA. putchar() faceva una SYSCALL PER CARATTERE, e
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
    /* ! QUATTRO CARATTERI DI ungetc, NON UNO (30 settembre 2026). Uno e' il
     * minimo che il C garantisce, ma la shell di SpiderMonkey ne rimette tre
     * di fila (guarda se il file comincia col BOM dell'UTF-8) e con uno solo
     * gli ultimi due si perdevano: ogni file JavaScript arrivava al parser
     * senza i primi due caratteri. La glibc ne tiene di piu'; qui quattro. */
    int             rimessi;    /* quanti ce ne sono in pila */
    unsigned char   pila[4];    /* l'ultimo rimesso e' il primo a uscire */
};

#define STDIN_BUF_SIZE  512     /* quanto una riga del driver kbd */
#define FILE_BUF_SIZE   4096

static unsigned char buf_stdin[STDIN_BUF_SIZE];
static unsigned char buf_stdout[512];
static unsigned char buf_stderr[128];

static FILE f_stdin  = { 0, _F_LETT,             buf_stdin,  STDIN_BUF_SIZE, 0, 0, 0 };
static FILE f_stdout = { 1, _F_SCRIT | _F_AUTO,  buf_stdout, sizeof(buf_stdout), 0, 0, 0 };
static FILE f_stderr = { 2, _F_SCRIT | _F_AUTO,  buf_stderr, sizeof(buf_stderr), 0, 0, 0 };

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

    if (f->rimessi > 0) return (int)f->pila[--f->rimessi];

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
    /* Fino a quattro caratteri: vedi struct _FILE. */
    if (f == NULL || c < 0 || f->rimessi >= 4) return -1;
    f->pila[f->rimessi++] = (unsigned char)c;
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
    f->rimessi = 0;
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
 * ! IL DESCRITTORE PUO' CAMBIARE NUMERO. Su Unix freopen() riusa lo
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
    f->rimessi = 0;

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
     * ! SU stdin, stdout E stderr SI SVUOTA E BASTA — NON SI CHIUDE.
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

    return (long)k - (long)(f->fine - f->pos) - f->rimessi;
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
        off -= (long)(f->fine - f->pos) + f->rimessi;
    }

    f->pos = f->fine = 0;
    f->rimessi = 0;
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
 * perche' <cstdio> della libstdc++ le dichiara. ! Ritornano 0/-1, non la
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
 * setbuf, setvbuf — ! CI SONO MA NON CAMBIANO NIENTE
 *
 * La politica di bufferizzazione di EX-OS e' decisa e documentata piu'
 * sopra (4 KB sui file, svuotamento a fine chiamata su stdout/stderr), e
 * non e' regolabile: i buffer stanno DENTRO la struttura FILE, non
 * allocati a parte, quindi non c'e' niente da sostituire.
 *
 * ! setvbuf RITORNA DIVERSO DA ZERO — «non l'ho fatto» — invece di
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

/* ! Non ritorna niente per definizione, quindi NON PUO' dire che non ha
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
 * ! LE CIFRE SIGNIFICATIVE SI FERMANO A 19, E OLTRE SI STAMPANO ZERI.
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
 * ! SI SCALA CON UNA TABELLA DI POTENZE, NON DIVIDENDO PER 10 IN CICLO.
 * Portare 1e300 nell'intervallo [0.1, 1) dividendo per dieci vuol dire
 * TRECENTO divisioni, e ognuna arrotonda: l'errore si accumula e le
 * ultime cifre escono sbagliate. Con la tabella (1e1, 1e2, 1e4, ... 1e256)
 * bastano nove moltiplicazioni, cioe' nove arrotondamenti invece di
 * trecento.
 *
 * ! SI LAVORA IN `long double`. Su i386 e' l'x87 a 80 bit, con 64 bit di
 * mantissa: 19 cifre decimali. Fare gli stessi conti in `double` ne
 * lascerebbe 15-16, cioe' meno di quante ne ha il numero da stampare, e
 * l'ultima cifra di un %.17g uscirebbe sbagliata.
 * ============================================================================= */
#define CIFRE_MAX  24       /* capienza del buffer, riporto compreso */

/* ! DICIOTTO, ED E' UN NUMERO MISURATO NON STIMATO. Oltre questa soglia
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

    /* ! LA SCALATURA PUO' SBAGLIARE DI UNO per l'arrotondamento
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
     * ! ARROTONDAMENTO AL PARI, non "mezzo verso l'alto".
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
        /* ! ALMENO DUE CIFRE DI ESPONENTE, come dice lo standard: "1e+5"
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

        /* ! RIESTRAZIONE CON L'ARROTONDAMENTO AL POSTO GIUSTO. La prima
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

                /* ! UN `float` PASSATO A UNA FUNZIONE VARIADICA ARRIVA
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

                /* ! GLI ZERI DI RIEMPIMENTO VANNO DOPO IL SEGNO, non
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
 * ! NON SI LEGGE OLTRE LA FINESTRA. Una conversione che avrebbe bisogno
 * di piu' di SCANF_FINESTRA byte — un %s con dentro un valore lunghissimo,
 * un file senza spazi — si ferma li'. Sono 1024 byte: piu' di qualunque
 * riga di un file di testo che abbia senso leggere con fscanf, e va
 * DETTO invece di scoprirlo.
 *
 * ! SU UN FLUSSO NON POSIZIONABILE — la console — si legge una riga e si
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

        /* ! I DISTRUTTORI GLOBALI DOPO gli atexit, non prima. Un
         * handler registrato con atexit() puo' usare un oggetto globale;
         * distruggerlo prima gli lascerebbe in mano un oggetto morto.
         * Vedi _libc_distruttori() in fondo al file. */
        /* ! QUELLO DEL PROGRAMMA SE C'E'. Vedi __libc_distruttori_registra():
         * dentro la libc condivisa, _libc_distruttori() qui sotto vedrebbe il
         * __fini_array della LIBRERIA invece che quello del programma. */
        if (g_distruttori_prog) {
            g_distruttori_prog();
        }
#ifndef EXOS_LIBC_SO
        /* ! DENTRO LA LIBRERIA QUESTA RIGA NON C'E'. _libc_distruttori() sta in
         * libc_avvio.c, che la libreria non compila apposta (percorrerebbe il
         * __fini_array della LIBRERIA invece che quello del programma). Chi si
         * collega alla libc condivisa registra il suo qui sopra. */
        else {
            _libc_distruttori();
        }
#endif
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
 * propri dati. ! Il rovescio e' che un file aperto in scrittura resta
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
 * ! LA LISTA E' SEPARATA DA QUELLA DI atexit, e deve restarlo: sono due
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

    /* ! NIENTE fflush: e' la differenza con exit(), ed e' il punto della
     * funzione. Un file aperto in scrittura resta monco, e chi chiama
     * quick_exit lo sa. */
    _Exit(code);
}

/* ! DEBOLE (30 settembre 2026): la libc.a e' un oggetto solo, quindi chi
 * ridefinisce abort — mozalloc di Firefox lo fa, per lasciare una traccia prima
 * di cadere — si scontrava con questa. Debole, vince la sua. */
__attribute__((weak)) void abort(void)
{
    /* Causa un fault intenzionale */
    exit(134);
}

/* =============================================================================
 * ALLOCATORE — lista di blocchi con riuso e fusione
 *
 * ! COSA C'ERA PRIMA, e perche' andava rifatto (agosto 2026). L'allocatore
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

/* ! SEDICI, NON OTTO (2 ottobre 2026, tappa 6 di Exilla). Su i386 il GCC di
 * EX-OS dice _Alignof(max_align_t) == 16 e __STDCPP_DEFAULT_NEW_ALIGNMENT__
 * == 16: un `operator new` semplice deve dare memoria allineata a 16, e un
 * tipo alignas(16) NON passa da memalign. Il codice che si fida (arene che
 * arrotondano il puntatore e poi riempiono il blocco fino in fondo) con un
 * blocco allineato a 8 scriveva fino a 8 byte oltre la fine: proprio
 * `prec` e `succ` dell'intestazione dopo. Gecko cadeva dentro malloc
 * seguendo un `succ` fatto di testo (0x6d2e7379, «ys.m»). Con
 * l'intestazione da 16 e il break a pagine intere, arrotondare le taglie a
 * 16 basta perche' ogni blocco cada su 16. */
#define HEAP_ALLINEA    16u
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
typedef char heap_hdr_allineata[(sizeof(Blocco) % HEAP_ALLINEA) == 0 ? 1 : -1];
#define BLOCCO_DATI(b)  ((void *)((char *)(b) + BLOCCO_HDR))
#define DATI_BLOCCO(p)  ((Blocco *)((char *)(p) - BLOCCO_HDR))

/* Il controllo dello heap (EXOS_MALLOC_CONTROLLA) sta piu' in basso, coi
 * wrapper pubblici; heap_malloc lo chiama mentre scorre la lista. */
#ifdef EXOS_LIBC_PORTATI
static int  g_heap_controlla;
static void ctl_anello(struct Blocco *b);
static void mm_dice(const char *op, uint32_t a, uint32_t l, uint32_t x, int32_t r);
#else
#define mm_dice(op, a, l, x, r) ((void)0)
#endif

static Blocco *heap_primo = NULL;
static Blocco *heap_ultimo = NULL;

/* =============================================================================
 * I CASSETTI — le liste dei blocchi LIBERI per taglia (3 ottobre 2026, Exilla)
 *
 * ! malloc SCORREVA TUTTI I BLOCCHI, liberi e occupati, dal primo: con lo heap
 * di Firefox (centinaia di MB, centinaia di migliaia di blocchi) ogni malloc
 * ne visitava centinaia di migliaia tenendo il lucchetto, e gli altri fili
 * aspettavano. Sembrava un blocco: l'interfaccia di Firefox non finiva mai di
 * caricarsi, e campionando la CPU l'EIP stava nel ciclo di heap_malloc.
 *
 * La lista per indirizzo (prec/succ) resta com'era: serve alle fusioni. In
 * piu' ogni blocco libero sta in un CASSETTO secondo la sua taglia, e i due
 * puntatori del cassetto stanno nei SUOI dati (un blocco libero non ha dati
 * da tenere): niente memoria in piu'. Cassetti 0..63 = taglie esatte 16..1024
 * byte (tutto e' multiplo di HEAP_ALLINEA); dal 64 in su una potenza di due
 * ciascuno. malloc guarda il cassetto della taglia e quelli sopra: nei
 * piccoli ogni blocco va bene, nei grandi si cerca il primo che basta.
 *
 * ! REGOLA: un blocco libero sta in un cassetto se e solo se dim > 0 (il
 * pezzo di testa lasciato da memalign puo' avere dim 0, e li' non ci sono i
 * sedici byte dei due puntatori). Chi cambia dim a un blocco libero lo toglie
 * prima e lo rimette dopo.
 * ============================================================================= */
#define HEAP_CASSETTI   96
typedef struct Legami { struct Blocco *prima, *dopo; } Legami;
#define LEGAMI(b)       ((Legami *)BLOCCO_DATI(b))
static Blocco *g_cassetto[HEAP_CASSETTI];

static unsigned int cassetto_di(size_t dim)
{
    unsigned int k = 0;

    if (dim <= 1024u) return (unsigned int)((dim + 15u) / 16u) - (dim ? 1u : 0u);
    while ((dim >> k) > 1u) k++;            /* k = floor(log2(dim)), >= 10 */
    k = 64u + (k - 10u);
    return k < HEAP_CASSETTI ? k : HEAP_CASSETTI - 1u;
}

static void cassetto_metti(Blocco *b)
{
    unsigned int i;
    Legami      *l;

    if (b->dim == 0) return;
    i = cassetto_di(b->dim);
    l = LEGAMI(b);
    l->prima = NULL;
    l->dopo  = g_cassetto[i];
    if (g_cassetto[i]) LEGAMI(g_cassetto[i])->prima = b;
    g_cassetto[i] = b;
}

static void cassetto_togli(Blocco *b)
{
    Legami *l;

    if (b->dim == 0) return;
    l = LEGAMI(b);
    if (l->prima) LEGAMI(l->prima)->dopo = l->dopo;
    else          g_cassetto[cassetto_di(b->dim)] = l->dopo;
    if (l->dopo)  LEGAMI(l->dopo)->prima = l->prima;
}

/* Il primo blocco libero con dim >= utile, tolto dal suo cassetto; o NULL. */
static Blocco *cassetto_cerca(size_t utile)
{
    unsigned int i;
    Blocco      *b;

    for (i = cassetto_di(utile); i < HEAP_CASSETTI; i++) {
        for (b = g_cassetto[i]; b != NULL; b = LEGAMI(b)->dopo) {
            if (b->dim >= utile) {
                cassetto_togli(b);
                return b;
            }
            if (i < 64) break;              /* taglia esatta: o va o no */
        }
    }
    return NULL;
}

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

    /* ! SI CHIEDE A PAGINE INTERE, e prima non si faceva.
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
    mm_dice("sbrk", (uint32_t)base, (uint32_t)quanto, 0, base);
    /* ! UN ERRORE E' -4095..-1, NON «<= 0»: lo heap puo' salire oltre i 2 GB
     * (heap_max arriva a ~0xB79BB000), e un indirizzo da li' in su, letto
     * come int, e' negativo — malloc avrebbe risposto NULL con la memoria
     * libera. Trovato dal banco di prova dell'allocatore (3 ottobre 2026). */
    if (base == 0 || (uint32_t)base >= 0xFFFFF001u) return NULL;

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
        cassetto_togli(b->prec);
        b->prec->dim += BLOCCO_HDR + b->dim;
        b->prec->succ = NULL;
        heap_ultimo = b->prec;
        cassetto_metti(b->prec);
        return b->prec;
    }

    cassetto_metti(b);
    return b;
}

/* Spezza `b` lasciando `utile` byte, se l'avanzo e' abbastanza grande da
 * essere un blocco a sua volta. Un avanzo minuscolo resta attaccato: una
 * lista piena di frammenti da otto byte costa piu' memoria (in
 * intestazioni) di quanta ne recuperi. */
static void heap_fondi_con_succ(Blocco *b);
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
    cassetto_metti(resto);
    /* L'avanzo si unisce al vicino libero che lo segue: senza, restavano
     * blocchi liberi affiancati e mai fusi (136 su un milione di operazioni
     * del banco di prova), e lo heap non tornava mai indietro. */
    heap_fondi_con_succ(resto);
}

static void *heap_malloc(size_t size)
{
    Blocco *b;
    size_t  utile;

    /* heap_malloc(0) puo' ritornare NULL o un puntatore unico: si ritorna un
     * blocco vero, cosi' heap_free() su quel puntatore e' legittima e il
     * chiamante non deve distinguere il caso. */
    if (size == 0) size = 1;

    utile = heap_allinea(size);
    if (utile < size) return NULL;      /* overflow dell'arrotondamento */

    b = cassetto_cerca(utile);
    if (b == NULL) {
        b = heap_estendi(utile);
        if (b == NULL) return NULL;
        cassetto_togli(b);
    }
#ifdef EXOS_LIBC_PORTATI
    if (g_heap_controlla > 0) { if (b->prec) ctl_anello(b->prec); ctl_anello(b); }
#endif
    b->libero = 0;
    heap_spezza(b, utile);
    return BLOCCO_DATI(b);
}

/* Fonde `b` con il successivo, se sono entrambi liberi E fisicamente
 * adiacenti. L'adiacenza va CONTROLLATA e non data per scontata: due
 * chiamate a sbrk possono restituire aree separate, e fondere un buco
 * consegnerebbe al chiamante memoria che non gli appartiene. */
/* Assorbe in `b` il vicino successivo, se e' libero e adiacente. NON
 * guarda se `b` stesso e' libero.
 *
 * ! ESISTE PERCHE' realloc DEVE POTER ALLUNGARE UN BLOCCO ALLOCATO, e
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
 * `b->libero` SERVE agli altri chiamanti. heap_free() e heap_memalign() chiamano
 * heap_fondi_con_succ(b->prec) senza sapere se il predecessore sia
 * libero, e fondere un blocco allocato col suo vicino consegnerebbe due
 * volte la stessa memoria. Quel controllo resta nel guscio; qui sotto
 * c'e' solo la fusione, per chi ha gia' stabilito che si puo' fare. */
static void heap_assorbi_succ(Blocco *b)
{
    Blocco *s = b->succ;

    if (s == NULL || !s->libero) return;
    if ((char *)b + BLOCCO_HDR + b->dim != (char *)s) return;

    cassetto_togli(s);
    if (b->libero) cassetto_togli(b);
    b->dim += BLOCCO_HDR + s->dim;
    b->succ = s->succ;
    if (s->succ) s->succ->prec = b;
    else         heap_ultimo = b;
    if (b->libero) cassetto_metti(b);
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
 * PERCHE' ESISTE. Fino ad agosto 2026 heap_free() non chiamava MAI sbrk con un
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
 * ! TRE CONDIZIONI, E OGNUNA EVITA UN GUASTO DIVERSO:
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
 * ! IL CONTROLLO DI TAGLIA VIENE PRIMA DI sbrk(0), ed e' voluto: e'
 * aritmetica pura, quindi la heap_free() normale — quella che non restituisce
 * niente — non paga nessuna syscall. Rimettere il conto dopo la sbrk(0)
 * annullerebbe il guadagno che l'allocatore nuovo era andato a prendere.
 * ============================================================================= */
/* Diventa 1 alla prima pthread_create e non torna piu' a 0. */
static volatile int g_ci_sono_fili = 0;

static void heap_restituisci(void)
{
    Blocco *b = heap_ultimo;
    char   *cima;
    size_t  totale, quanto;

    /* =====================================================================
     * ! CON PIU' FILI LO HEAP NON SI ACCORCIA (1 ottobre 2026, Exilla).
     *
     * Sul kernel di EX-OS sbrk e mmap spostano LO STESSO confine
     * (heap_end): una mmap anonima si prende le pagine in cima, proprio dove
     * lo heap della malloc cresce. Restituire e' fatto di due passi — leggere
     * la cima con sbrk(0), poi abbassarla con sbrk(-n) — e se in mezzo un
     * altro filo fa una mmap (il garbage collector di SpiderMonkey mappa a
     * blocchi da un mega) la cima e' salita, e sbrk(-n) toglie le ultime
     * pagine della SUA mappatura. Gecko cadeva leggendo un mutex «appena
     * oltre heap_end». Il lucchetto della malloc non basta: mmap non ci passa.
     * Con un filo solo la gara non c'e' e la memoria torna come prima.
     * ===================================================================== */
    if (g_ci_sono_fili) return;

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
    cassetto_togli(b);
    b->dim -= quanto;
    cassetto_metti(b);
}

static void heap_free(void *ptr)
{
    Blocco *b;

    if (ptr == NULL) return;            /* heap_free(NULL) e' legittima */

    b = DATI_BLOCCO(ptr);

    /* Una doppia free corromperebbe la lista fondendo due volte lo stesso
     * blocco. Qui si ignora invece di proseguire: e' un difetto del
     * chiamante, ma il danno resterebbe suo solo fino al momento in cui
     * l'heap comincia a consegnare due volte lo stesso indirizzo. */
    if (b->libero) return;

    b->libero = 1;
    cassetto_metti(b);
    heap_fondi_con_succ(b);
    if (b->prec) heap_fondi_con_succ(b->prec);

    /* Si prova a restituire solo se questa free ha toccato la CODA: negli
     * altri casi non c'e' niente da restituire e il controllo sarebbe
     * lavoro sprecato a ogni free. Dopo le fusioni il blocco in coda puo'
     * essere `b` oppure il suo predecessore, se b ci si e' fuso dentro. */
    if (heap_ultimo != NULL && heap_ultimo->libero) heap_restituisci();
}

/* =============================================================================
 * malloc_usable_size — quanti byte si possono davvero usare
 *
 * ! NON E' UN LUSSO GNU: e' cio' che permette a un raccoglitore di memoria di
 * sapere quanto sta occupando. QuickJS lo chiede per contare la memoria viva e
 * decidere QUANDO raccogliere; senza, lo standard prevede che si risponda 0 —
 * e allora il motore conta solo le taglie che ha chiesto lui, sbagliando per
 * difetto di tutto l'arrotondamento.
 *
 * ! IL NUMERO E' VERO, non la taglia chiesta: e' `dim` dell'intestazione,
 * cioe' quello che malloc ha davvero riservato dopo l'allineamento a otto e
 * l'eventuale rifiuto di spezzare un avanzo troppo piccolo. Chi scrive fin
 * la' dentro non corrompe niente, ed e' esattamente la promessa che questa
 * funzione fa.
 *
 * ! SU UN PUNTATORE CHE NON VIENE DA malloc IL RISULTATO NON HA SENSO, come
 * per free: si legge un'intestazione che non c'e'. NULL invece si accetta e
 * rende 0, perche' e' il caso che capita davvero.
 * ============================================================================= */
static size_t heap_malloc_usable_size(void *ptr)
{
    Blocco *b;

    if (ptr == NULL) return 0;

    b = DATI_BLOCCO(ptr);
    return b->dim;
}

static void *heap_calloc(size_t nmemb, size_t size)
{
    size_t tot = nmemb * size;
    void  *p;

    /* La moltiplicazione puo' traboccare, e un tetto superato in silenzio
     * darebbe un blocco piu' piccolo di quanto il chiamante crede — cioe'
     * una scrittura fuori dai suoi confini che nessuno segnala. */
    if (nmemb != 0 && tot / nmemb != size) return NULL;

    p = heap_malloc(tot);
    if (p) memset(p, 0, tot);
    return p;
}

static void *heap_realloc(void *ptr, size_t size)
{
    Blocco *b;
    void   *nuovo;
    size_t  copia;

    if (ptr == NULL)  return heap_malloc(size);
    if (size == 0)    { heap_free(ptr); return NULL; }

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

    nuovo = heap_malloc(size);
    if (nuovo == NULL) return NULL;     /* l'originale resta valido */

    /* Si copia il MINIMO fra vecchia e nuova dimensione. La versione
     * precedente copiava sempre `size`, cioe' leggeva oltre la fine del
     * blocco vecchio quando si ingrandiva. */
    copia = (b->dim < size) ? b->dim : size;
    memcpy(nuovo, ptr, copia);
    heap_free(ptr);
    return nuovo;
}

/* =============================================================================
 * ALLOCAZIONE ALLINEATA — memalign, aligned_alloc, posix_memalign
 *
 * CHI LE CHIEDE: la libstdc++. Dal C++17 un tipo con allineamento
 * superiore a quello naturale non passa piu' per `operator new(size_t)`
 * ma per `operator new(size_t, align_val_t)`, e l'implementazione di
 * quella nella libreria standard e' un involucro attorno a heap_memalign()
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
 * ! LA CONSEGUENZA CHE CONTA: il puntatore restituito ha davanti a se'
 * un'intestazione normale, agganciata alla lista in ordine di indirizzo
 * come tutte. Quindi heap_free() lo tratta come un blocco qualunque, e la
 * fusione con i vicini funziona senza sapere nulla di tutto questo. Non
 * serve una `aligned_free`, e chi passa il puntatore a una heap_free() ignara
 * — per esempio codice di terzi — non rompe niente.
 *
 * La testa resta come blocco LIBERO invece di essere sprecata: su una
 * richiesta con allineamento 4096 sono fino a quattro KB che tornano
 * disponibili invece di restare in ostaggio del blocco allineato.
 * ============================================================================= */

static void *heap_memalign(size_t allineamento, size_t size)
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

    /* Fino a HEAP_ALLINEA byte non c'e' niente da fare: malloc gia' li garantisce. */
    if (allineamento <= HEAP_ALLINEA) return heap_malloc(size);

    if (size == 0) size = 1;
    utile = heap_allinea(size);
    if (utile < size) return NULL;              /* trabocco */

    /* Il margine e' `allineamento` (quanto al piu' si deve avanzare) piu'
     * un'intestazione (quella che va messa davanti al risultato). */
    if (utile + allineamento + BLOCCO_HDR < utile) return NULL;
    grezzo = heap_malloc(utile + allineamento + BLOCCO_HDR);
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
    cassetto_metti(b);
    if (b->prec) heap_fondi_con_succ(b->prec);

    /* La coda in eccesso torna all'heap, se ne vale la pena. */
    heap_spezza(nuovo, utile);

    return BLOCCO_DATI(nuovo);
}

/* Il C11 pretende che `size` sia un multiplo di `allineamento`. Qui non si
 * fa rispettare: rifiutare renderebbe la funzione inutilizzabile come
 * ripiego di memalign, che e' l'uso che ne fa la libstdc++, e concedere in
 * piu' non rompe nessun programma corretto. */
/* ! NON IMPOSTA errno E NON RITORNA -1: posix_memalign e' l'eccezione
 * che RITORNA il codice di errore. Trattarla come le altre e' l'errore
 * classico su questa funzione. */
static int heap_posix_memalign(void **risultato, size_t allineamento, size_t size)
{
    void *p;

    if (risultato == NULL) return EINVAL;

    /* In piu' rispetto a memalign: POSIX chiede che l'allineamento sia
     * anche un multiplo di sizeof(void *). */
    if (allineamento < sizeof(void *) ||
        (allineamento & (allineamento - 1u)) != 0) {
        return EINVAL;
    }

    p = heap_memalign(allineamento, size);
    if (p == NULL) return ENOMEM;

    *risultato = p;
    return 0;
}

/* =============================================================================
 * ! LO HEAP HA UN LUCCHETTO (1 ottobre 2026, tappa 6 di Exilla).
 *
 * Non l'aveva: la lista dei blocchi si scorreva e si cuciva senza protezione,
 * e bastava che due fili allocassero insieme per rovinarla. Finche' i fili li
 * usavano pochi programmi di casa non si vedeva; Gecko alloca da decine di
 * fili e cadeva dentro malloc, scrivendo a un «indirizzo» che era un pezzo di
 * testo (0x72657377, «wser»).
 *
 * Le funzioni heap_* qui sopra sono quelle di prima, senza lucchetto, e si
 * chiamano fra loro (realloc usa malloc e free): il lucchetto lo prendono solo
 * queste, una volta per chiamata. E' mutex_prendi, il futex a tre stati: senza
 * contesa nessuna chiamata di sistema.
 * ============================================================================= */
void mutex_prendi(volatile int *m);
void mutex_lascia(volatile int *m);
static volatile int g_heap_lucchetto = 0;

/* =============================================================================
 * IL CONTROLLO DELLO HEAP (EXOS_MALLOC_CONTROLLA=1, 2 ottobre 2026)
 *
 * Per scoprire CHI scrive oltre la fine di un blocco. Gecko cadeva dentro
 * malloc seguendo un `succ` fatto di testo: il danno si vede lontano da dove
 * e' stato fatto, e senza strumenti il colpevole non ha nome.
 *
 * Con la variabile d'ambiente a 1 (letta alla prima allocazione, e da li'
 * non cambia: environ e' pronto prima dei costruttori, vedi libc_avvio.c)
 * ogni blocco ha in coda una ZONA ROSSA e una targhetta:
 *
 *   [hdr][byte chiesti][zona rossa 0xFD, almeno 16][targhetta]
 *   targhetta = magia, byte chiesti, quattro indirizzi di ritorno
 *
 * free e realloc controllano la zona rossa del blocco; malloc, mentre
 * scorre la lista, controlla che ogni `succ` punti dentro lo heap e indietro.
 * Al primo guasto stampa sullo stderr il blocco, chi lo aveva chiesto (i
 * ritorni si risolvono con addr2line o col nm del programma) e i byte attorno,
 * poi abort(). malloc_usable_size dice i byte chiesti, non `dim`: chi scrive
 * fino al numero che riceve non deve toccare la zona rossa.
 *
 * ! LA CATENA DEI CHIAMANTI SEGUE I FRAME POINTER: vale per il codice
 * compilato con -fno-omit-frame-pointer (Gecko lo e'); dove manca, si ferma
 * al primo anello che non sembra un frame dello stesso stack.
 *
 * ! SOLO NELLA libc.a DELLA TOOLCHAIN CROSS (EXOS_LIBC_PORTATI, vedi
 * tools/gcc-exos/prepara-cross.sh). Nella libc del Makefile ogni programma
 * del floppy che chiama malloc se lo sarebbe portato dentro, e il floppy non
 * si chiudeva piu'. Li' le ctl_* sono gusci vuoti e spariscono.
 * ============================================================================= */
#ifdef EXOS_LIBC_PORTATI
#define CTL_ZONA     16u
#define CTL_RITORNI  4
#define CTL_TARGA    (8u + 4u * CTL_RITORNI)
#define CTL_EXTRA    (CTL_ZONA + CTL_TARGA)
#define CTL_MAGIA    0xC0DE5AFEu
#define CTL_ROSSO    0xFDu

typedef struct {
    uint32_t magia;
    uint32_t chiesti;
    uint32_t ritorni[CTL_RITORNI];
} CtlTarga;

static int g_heap_controlla = -1;           /* -1 = non ancora deciso */
ssize_t write(int fd, const void *buf, size_t n);

static int ctl_attivo(void)
{
    if (g_heap_controlla < 0) {
        const char *v = getenv("EXOS_MALLOC_CONTROLLA");
        g_heap_controlla = (v != NULL && v[0] == '1') ? 1 : 0;
    }
    return g_heap_controlla;
}

static CtlTarga *ctl_targa(Blocco *b)
{
    return (CtlTarga *)((char *)BLOCCO_DATI(b) + b->dim - CTL_TARGA);
}

static void ctl_scrivi(const char *s)
{
    write(2, s, strlen(s));
}

static void ctl_hex(const char *etichetta, uint32_t v)
{
    char buf[12];
    int  i;

    ctl_scrivi(etichetta);
    buf[0] = '0'; buf[1] = 'x';
    for (i = 0; i < 8; i++) buf[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15u];
    buf[10] = ' '; buf[11] = 0;
    ctl_scrivi(buf);
}

/* `n` byte da `p` in esadecimale e come testo, sedici per riga. */
static void ctl_byte(const unsigned char *p, size_t n)
{
    size_t i, j;
    char   riga[80];

    for (i = 0; i < n; i += 16) {
        int k = 0;
        ctl_hex("\n  ", (uint32_t)(uintptr_t)(p + i));
        for (j = 0; j < 16 && i + j < n; j++) {
            riga[k++] = "0123456789abcdef"[p[i + j] >> 4];
            riga[k++] = "0123456789abcdef"[p[i + j] & 15u];
            riga[k++] = ' ';
        }
        riga[k++] = ' ';
        for (j = 0; j < 16 && i + j < n; j++)
            riga[k++] = (p[i + j] >= 32 && p[i + j] < 127) ? (char)p[i + j] : '.';
        riga[k] = 0;
        ctl_scrivi(riga);
    }
}

static void ctl_racconta(const char *cosa, Blocco *b)
{
    CtlTarga *t;
    int       i;

    ctl_scrivi("\n[MALLOC] ");
    ctl_scrivi(cosa);
    ctl_hex("\n  blocco ", (uint32_t)(uintptr_t)b);
    ctl_hex("dati ", (uint32_t)(uintptr_t)BLOCCO_DATI(b));
    ctl_hex("dim ", (uint32_t)b->dim);
    ctl_hex("libero ", (uint32_t)b->libero);
    ctl_hex("prec ", (uint32_t)(uintptr_t)b->prec);
    ctl_hex("succ ", (uint32_t)(uintptr_t)b->succ);
    if (!b->libero && b->dim >= CTL_EXTRA && b->dim < 0x40000000u) {
        t = ctl_targa(b);
        ctl_hex("\n  targhetta: magia ", t->magia);
        ctl_hex("chiesti ", t->chiesti);
        ctl_scrivi("\n  chiesto da:");
        for (i = 0; i < CTL_RITORNI; i++) ctl_hex(" ", t->ritorni[i]);
        ctl_scrivi("\n  fine dei dati e zona rossa:");
        if (t->chiesti <= b->dim) {
            size_t da = t->chiesti > 48 ? t->chiesti - 48 : 0;
            ctl_byte((unsigned char *)BLOCCO_DATI(b) + da, b->dim - da);
        }
    }
    ctl_scrivi("\n");
}

/* Marca un blocco appena dato: zona rossa, targhetta, chiamanti. */
static void *ctl_marca(void *p, size_t chiesti, void *cornice)
{
    Blocco        *b;
    CtlTarga      *t;
    unsigned char *z;
    uint32_t      *fp = (uint32_t *)cornice;
    int            i;

    if (p == NULL) return NULL;
    b = DATI_BLOCCO(p);
    t = ctl_targa(b);
    z = (unsigned char *)p + chiesti;
    memset(z, CTL_ROSSO, (size_t)((unsigned char *)t - z));
    t->magia   = CTL_MAGIA;
    t->chiesti = (uint32_t)chiesti;
    for (i = 0; i < CTL_RITORNI; i++) {
        uint32_t *succ;
        if (fp == NULL || ((uintptr_t)fp & 3u) != 0) { t->ritorni[i] = 0; continue; }
        t->ritorni[i] = fp[1];
        succ = (uint32_t *)(uintptr_t)fp[0];
        if (fp[1] == 0 || succ <= fp || (uintptr_t)succ - (uintptr_t)fp > 0x100000u) fp = NULL;
        else fp = succ;
    }
    return p;
}

/* Controlla zona rossa e targhetta di un blocco che sta per essere liberato o
 * spostato. Al guasto racconta e abortisce. */
static void ctl_verifica(void *p, const char *chi)
{
    Blocco        *b = DATI_BLOCCO(p);
    CtlTarga      *t;
    unsigned char *z;

    if (b->libero) { ctl_racconta(chi, b); ctl_scrivi("  (blocco gia' libero)\n"); abort(); }
    if (b->dim < CTL_EXTRA) { ctl_racconta(chi, b); abort(); }
    t = ctl_targa(b);
    if (t->magia != CTL_MAGIA || t->chiesti > b->dim - CTL_EXTRA) {
        ctl_racconta("targhetta rovinata: scritto oltre la zona rossa", b);
        abort();
    }
    for (z = (unsigned char *)p + t->chiesti; z < (unsigned char *)t; z++) {
        if (*z != CTL_ROSSO) {
            ctl_racconta("zona rossa scritta: qualcuno e' andato oltre i byte chiesti", b);
            abort();
        }
    }
}

/* Dentro heap_malloc, per ogni blocco della lista: `succ` deve stare nello
 * heap, dopo `b`, e puntare indietro a `b`. */
static void ctl_anello(Blocco *b)
{
    Blocco *s = b->succ;

    if (s == NULL) return;
    if (((uintptr_t)s & (HEAP_ALLINEA - 1u)) != 0 || s <= b || s > heap_ultimo ||
        s->prec != b) {
        ctl_racconta("intestazione rovinata (succ non valido)", b);
        if (b->prec) {
            ctl_racconta("il blocco prima, il probabile colpevole:", b->prec);
        }
        ctl_scrivi("  byte attorno all'intestazione:");
        ctl_byte((unsigned char *)b - 48, 48 + BLOCCO_HDR);
        ctl_scrivi("\n");
        abort();
    }
}
/* EXOS_MMAP_DICE=1: ogni sbrk, mmap, munmap e mprotect su stderr, una riga
 * ciascuno (indirizzo, byte, prot|flag<<8, risultato). Per scoprire chi ha
 * tolto la scrittura a una pagina che la malloc credeva sua. */
static void mm_dice(const char *op, uint32_t a, uint32_t l, uint32_t x, int32_t r)
{
    static int dice = -1;

    if (dice < 0) {
        const char *v = getenv("EXOS_MMAP_DICE");
        dice = (v != NULL && v[0] == '1');
    }
    if (!dice) return;
    ctl_scrivi("[MM] ");
    ctl_scrivi(op);
    ctl_hex(" ", a);
    ctl_hex("", l);
    ctl_hex("", x);
    ctl_hex("-> ", (uint32_t)r);
    ctl_scrivi("\n");
}

#else  /* il controllo e' solo nella libc.a del software portato: vedi prepara-cross.sh */
#define CTL_EXTRA 0u
typedef struct { uint32_t chiesti; } CtlTarga;
static int       ctl_attivo(void) { return 0; }
static CtlTarga *ctl_targa(Blocco *b) { (void)b; return NULL; }
static void     *ctl_marca(void *p, size_t n, void *c) { (void)n; (void)c; return p; }
static void      ctl_verifica(void *p, const char *c) { (void)p; (void)c; }
#endif

void *malloc(size_t size)
{
    void *p;
    mutex_prendi(&g_heap_lucchetto);
    if (ctl_attivo())
        p = ctl_marca(heap_malloc(size + CTL_EXTRA), size, __builtin_frame_address(0));
    else
        p = heap_malloc(size);
    mutex_lascia(&g_heap_lucchetto);
    return p;
}

void free(void *ptr)
{
    if (ptr == NULL) return;
    mutex_prendi(&g_heap_lucchetto);
    if (ctl_attivo()) ctl_verifica(ptr, "free");
    heap_free(ptr);
    mutex_lascia(&g_heap_lucchetto);
}

void *calloc(size_t nmemb, size_t size)
{
    void  *p;
    size_t tot = nmemb * size;

    if (nmemb != 0 && tot / nmemb != size) return NULL;
    mutex_prendi(&g_heap_lucchetto);
    if (ctl_attivo()) {
        p = ctl_marca(heap_malloc(tot + CTL_EXTRA), tot, __builtin_frame_address(0));
        if (p) memset(p, 0, tot);
    } else {
        p = heap_calloc(nmemb, size);
    }
    mutex_lascia(&g_heap_lucchetto);
    return p;
}

void *realloc(void *ptr, size_t size)
{
    void *p;
    mutex_prendi(&g_heap_lucchetto);
    if (ctl_attivo() && ptr != NULL && size != 0) {
        /* Sempre spostato: la zona rossa va rifatta comunque. */
        uint32_t vecchi;
        ctl_verifica(ptr, "realloc");
        vecchi = ctl_targa(DATI_BLOCCO(ptr))->chiesti;
        p = ctl_marca(heap_malloc(size + CTL_EXTRA), size, __builtin_frame_address(0));
        if (p) {
            memcpy(p, ptr, vecchi < size ? vecchi : size);
            heap_free(ptr);
        }
    } else if (ctl_attivo() && ptr == NULL) {
        p = ctl_marca(heap_malloc(size + CTL_EXTRA), size, __builtin_frame_address(0));
    } else if (ctl_attivo()) {
        ctl_verifica(ptr, "realloc a zero");
        heap_free(ptr);
        p = NULL;
    } else {
        p = heap_realloc(ptr, size);
    }
    mutex_lascia(&g_heap_lucchetto);
    return p;
}

void *memalign(size_t allineamento, size_t size)
{
    void *p;
    mutex_prendi(&g_heap_lucchetto);
    if (ctl_attivo())
        p = ctl_marca(heap_memalign(allineamento, size + CTL_EXTRA), size,
                      __builtin_frame_address(0));
    else
        p = heap_memalign(allineamento, size);
    mutex_lascia(&g_heap_lucchetto);
    return p;
}

void *aligned_alloc(size_t allineamento, size_t size)
{
    return memalign(allineamento, size);
}

int posix_memalign(void **risultato, size_t allineamento, size_t size)
{
    int r;
    mutex_prendi(&g_heap_lucchetto);
    if (ctl_attivo()) {
        r = heap_posix_memalign(risultato, allineamento, size + CTL_EXTRA);
        if (r == 0) ctl_marca(*risultato, size, __builtin_frame_address(0));
    } else {
        r = heap_posix_memalign(risultato, allineamento, size);
    }
    mutex_lascia(&g_heap_lucchetto);
    return r;
}

size_t malloc_usable_size(void *ptr)
{
    size_t n;
    mutex_prendi(&g_heap_lucchetto);
    if (ctl_attivo() && ptr != NULL) n = ctl_targa(DATI_BLOCCO(ptr))->chiesti;
    else                             n = heap_malloc_usable_size(ptr);
    mutex_lascia(&g_heap_lucchetto);
    return n;
}

/* =============================================================================
 * Funzioni di utilità aggiuntive
 * ============================================================================= */

/* Il ritorno di una funzione POSIX: il codice va in errno, il chiamante
 * riceve -1. E' il punto in cui si applica la regola dichiarata in testa al
 * file — chi vuole il numero usa errno, chi vuole un messaggio strerror().
 *
 * ! SOLO PER LE FUNZIONI CON UN NOME POSIX. Le nostre continuano a
 * ritornare -errno: vedi il commento in testa al file. */
static int32_t err_posix(int32_t r)
{
    if (r < 0) { errno = -r; return -1; }
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
    /* O_CLOEXEC e O_NOFOLLOW: vedi libc.h. Il kernel non li conosce. */
    flags &= ~(0x80000 | 0x20000);
    return err_posix(_syscall3(SYS_OPEN, (uint32_t)path, (uint32_t)flags, 0));
}

/* ! I SOCKET HANNO DESCRITTORI DA PRESA_BASE IN SU, e le funzioni dei
 * descrittori li riconoscono qui: il kernel non ne sa niente (vedi la sezione
 * dei socket in fondo al file e in lib/include/libc.h). */
#define PRESA_BASE      1024
/* And shared memory from shm_open() from SHMFD_BASE up (@SHM-OPEN, 29
 * September 2026): a socket is below it, a zone above. */
#define SHMFD_BASE      2048
/* 128 e non 16 (2 ottobre 2026): Gecko ne tiene aperte decine insieme, e
 * con SHM_ANON non costano zone del kernel. */
#define SHMFD_MAX       128
#ifdef EXOS_LIBC_SO
#define E_PRESA(fd)     0               /* nella libc.so i socket non ci sono */
#define E_SHMFD(fd)     0
#else
#define E_PRESA(fd)     ((fd) >= PRESA_BASE && (fd) < SHMFD_BASE)
#define E_SHMFD(fd)     ((fd) >= SHMFD_BASE && (fd) < SHMFD_BASE + SHMFD_MAX)
/* ! THROUGH POINTERS THAT ONLY shm_open FILLS, and not direct calls. close,
 * fstat, mmap and munmap are in every program; naming the shm functions from
 * them made the linker pull the whole of shm into every program that closes
 * a file — and the floppy lost 4 KB to it. A program that never calls
 * shm_open now pays four null pointers. */
struct stat;
static int  (*g_shm_chiudi)(int fd);
static int  (*g_shm_fstat)(int fd, struct stat *st);
static long (*g_shm_mmap)(size_t lung, int fd, long off);
static int  (*g_shm_munmap)(void *addr);
static int  (*g_shm_dup)(int fd);
static int  g_shmfd[SHMFD_MAX];        /* vedi shm_open: zona+1, 0 = libero */
#endif
#ifndef EXOS_LIBC_SO
static int     presa_chiudi(int fd);
static int     presa_fcntl(int fd, int cmd, unsigned int arg);
static int     presa_ioctl(int fd, unsigned int req, void *arg);
#else
#define presa_chiudi(fd)            (-1)
#define presa_fcntl(fd, cmd, arg)   (-1)
#define presa_ioctl(fd, req, arg)   (-1)
#define poll_misto(f, n, t)         (-1)
#endif
ssize_t        send(int fd, const void *buf, size_t n, int flag);
ssize_t        recv(int fd, void *buf, size_t n, int flag);

int close(int fd)
{
    if (E_PRESA(fd)) return presa_chiudi(fd);
#ifndef EXOS_LIBC_SO
    if (E_SHMFD(fd) && g_shm_chiudi) return g_shm_chiudi(fd);
#endif
    return err_posix(_syscall1(SYS_CLOSE, (uint32_t)fd));
}

/* dup/dup2/fcntl — vedi il commento esteso in kernel/syscall/syscall_impl.c.
 *
 * ! I due descrittori condividono il FILE, non la POSIZIONE: ognuno si
 * ricorda per conto suo dove era arrivato, mentre su POSIX una read() da
 * uno dei due sposta anche l'altro. Chi legge da un fd duplicato faccia
 * una lseek() esplicita invece di dare per scontato di ripartire da capo. */
int dup(int fd)
{
    if (E_PRESA(fd)) { errno = 95; return -1; }    /* EOPNOTSUPP: vedi libc.h */
#ifndef EXOS_LIBC_SO
    if (E_SHMFD(fd)) {
        if (g_shm_dup) return g_shm_dup(fd);
        errno = EBADF;
        return -1;
    }
#endif
    return (int)err_posix(_syscall1(SYS_DUP, (uint32_t)fd));
}

int dup2(int vecchio, int nuovo)
{
    if (E_PRESA(vecchio) || E_PRESA(nuovo) || E_SHMFD(vecchio) || E_SHMFD(nuovo)) {
        errno = 95;
        return -1;
    }
    return (int)err_posix(_syscall2(SYS_DUP2, (uint32_t)vecchio, (uint32_t)nuovo));
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

    if (E_PRESA(fd)) return presa_fcntl(fd, cmd, arg);
#ifndef EXOS_LIBC_SO
    /* Una zona di shm_open: F_DUPFD e' dup, il resto (close-on-exec, flag)
     * non ha niente da cambiare — EX-OS non ha exec che erediti descrittori
     * di memoria condivisa — e si risponde che va bene. */
    if (E_SHMFD(fd)) {
        if (cmd == 0 /* F_DUPFD */) return dup(fd);
        if (!g_shm_dup || g_shmfd[fd - SHMFD_BASE] == 0) { errno = EBADF; return -1; }
        return (cmd == 3 /* F_GETFL */) ? 2 /* O_RDWR */ : 0;
    }
#endif
    /* ! I LUCCHETTI SUI FILE (F_GETLK 5, F_SETLK 6, F_SETLKW 7) NON CI SONO:
     * «libero» a chi chiede, «preso» a chi prende — vedi <fcntl.h>. Il
     * descrittore pero' deve esistere. (@EXILLA-NSS, 30 settembre 2026) */
    if (cmd >= 5 && cmd <= 7) {
        if (_syscall3(SYS_FCNTL, (uint32_t)fd, 1u /* F_GETFD */, 0) < 0) { errno = EBADF; return -1; }
        if (cmd == 5 && arg) *(short *)arg = 2;         /* l_type = F_UNLCK */
        return 0;
    }
    return (int)err_posix(_syscall3(SYS_FCNTL, (uint32_t)fd, (uint32_t)cmd, arg));
}

ssize_t read(int fd, void *buf, size_t n)
{
    if (E_PRESA(fd)) return recv(fd, buf, n, 0);
    return err_posix(_syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buf, n));
}

ssize_t write(int fd, const void *buf, size_t n)
{
    if (E_PRESA(fd)) return send(fd, buf, n, 0);
    return err_posix(_syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, n));
}

/* Il programma a cui appartiene `pid` (0 = chi chiama): il pid del capogruppo.
 * Vedi SYS_PROC_GRUPPO in kernel/include/syscall.h. */
int proc_gruppo(int pid)
{
    return (int)err_posix(_syscall1(SYS_PROC_GRUPPO, (uint32_t)pid));
}

/* ! getpid() E' IL PROCESSO, filo_id() IL FILO (1 ottobre 2026, Exilla).
 * Per il kernel ogni filo e' un processo del gruppo del capogruppo, e
 * SYS_GETPID risponde quello del filo. POSIX invece vuole lo STESSO getpid()
 * in tutti i fili: Gecko ricorda il processo che ha creato un canale e lo
 * confronta dal filo che lo usa, e con due numeri diversi si fermava
 * (MOZ_RELEASE_ASSERT in Endpoint::Bind). getpid() ora e' il capogruppo, e si
 * ricorda: non cambia per tutta la vita del processo. Quello che dentro la
 * libc vuole davvero il filo (il tid dei pthread, il padrone di un mutex, lo
 * scaffale IPC) chiama filo_id(). */
static int filo_id(void)
{
    return _syscall1(SYS_GETPID, 0);
}

static int g_mio_pid = 0;

int getpid(void)
{
    if (g_mio_pid == 0) {
        int io = filo_id();
        int capo = _syscall1(SYS_PROC_GRUPPO, (uint32_t)io);

        g_mio_pid = capo > 0 ? capo : io;
    }
    return g_mio_pid;
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
    if (E_PRESA(fd)) return presa_ioctl(fd, request, arg);
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
    return err_posix(_syscall1(SYS_CHDIR, (uint32_t)path));
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
 * ! NON SEGUE NESSUN COLLEGAMENTO SIMBOLICO, e non e' una
 * semplificazione: EX-OS non ne ha (vedi <sys/stat.h>), quindi non c'e'
 * niente da seguire. Su un Unix questa e' la parte difficile della
 * funzione; qui non esiste il problema.
 *
 * ! IL ".." SI RISOLVE SULLA STRINGA, non sul filesystem. Senza
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
 * ! IL KERNEL SCRIVE I DUE NUMERI NELL'ARRAY, non li ritorna: il valore
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

/* =============================================================================
 * LA RICERCA NEL PATH — perche' sta qui e non nella shell
 *
 * ! UN NOME SENZA BARRE SI CERCA NEL PATH, come fa execvp() su Unix. Fino
 * ad agosto 2026 non succedeva: spawn("as", ...) chiedeva al kernel il
 * file "/as", che non esiste, e l'errore diceva proprio quello — un
 * percorso che nessuno aveva scritto.
 *
 * L'ha fatto entrare fbc, il compilatore FreeBASIC, che lancia
 * l'assemblatore chiamandolo "as" e basta; ma la regola vale per ogni
 * programma di terzi portato qui — il driver di GCC fa lo stesso, e
 * anche `make`. La shell la sua ricerca ce l'aveva gia', per conto suo:
 * il punto e' che un PROGRAMMA non ha una shell dietro, e non deve
 * doversene scrivere una.
 *
 * ! SOLO SE NON C'E' NEMMENO UNA BARRA. "./prog" e "/bin/prog" restano
 * percorsi e si usano come sono: e' la regola di execvp, ed evita che un
 * "bin/prog" relativo venga cercato in giro per il sistema.
 * ============================================================================= */
static int spawn_cerca_path(const char *nome, char *dst, size_t dim)
{
    const char *p = getenv("PATH");
    size_t      i;

    if (p == NULL || *p == '\0') p = "/bin";

    while (*p) {
        i = 0;
        while (*p && *p != ':' && i + 1 < dim) dst[i++] = *p++;
        while (*p && *p != ':') p++;            /* voce troppo lunga: si scarta */
        if (*p == ':') p++;
        if (i == 0) continue;

        if (dst[i - 1] != '/' && i + 1 < dim) dst[i++] = '/';
        {
            size_t j = 0;
            while (nome[j] && i + 1 < dim) dst[i++] = nome[j++];
        }
        dst[i] = '\0';

        /* X_OK: il file dev'esserci ed essere eseguibile. Su EX-OS i
         * permessi sono una finzione (vedi <sys/stat.h>), quindi in
         * pratica risponde «c'e' ed e' un file»: e' comunque il controllo
         * giusto da scrivere, e diventera' vero da solo il giorno che i
         * permessi ci saranno. */
        if (access(dst, X_OK) == 0) return 0;
    }

    return -1;
}

/* Il corpo unico: `console` < 0 = quella del padre, `uid` == NULL = l'identita'
 * del padre. Le tre forme pubbliche qui sotto sono tre modi di chiamarlo. */
static int spawn_completo(const char *path, char *const argv[],
                          char *const envp[], const SpawnRedir *redir,
                          int n_redir, int console,
                          const unsigned int *uid, const unsigned int *gid)
{
    SpawnExtra ex;
    char       trovato[PERCORSO_MAX];
    int        argc = 0, i;

    if (path == NULL) { errno = EINVAL; return -1; }

    if (strchr(path, '/') == NULL) {
        if (spawn_cerca_path(path, trovato, sizeof trovato) != 0) {
            errno = ENOENT;
            return -1;
        }
        path = trovato;
    }

    if (argv != NULL) while (argv[argc] != NULL) argc++;

    /* ! IL BLOCCO SI AZZERA PRIMA DI RIEMPIRLO. Da quando ha un campo
     * `flag`, lasciarlo come capita sullo stack vuol dire chiedere per caso
     * una console che non si voleva — e il kernel guarda quel flag. */
    memset(&ex, 0, sizeof(ex));
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

    /* ! IL FLAG SI ACCENDE SOLO SE UNA CONSOLE E' STATA CHIESTA DAVVERO.
     * Un console < 0 vuol dire «quella del padre», che e' il comportamento
     * di sempre e quello che vuole chiunque non ci stia pensando. */
    if (console >= 0) {
        ex.flag    |= SPAWN_F_CONSOLE;
        ex.console  = (unsigned int)console;
    }

    /* Stessa regola della console: il flag si accende solo se l'identita' e'
     * stata chiesta davvero. Un uid 0 lasciato in una struttura azzerata
     * vorrebbe dire «figlio di root», cioe' il contrario di cio' che serve. */
    if (uid != NULL) {
        ex.flag |= SPAWN_F_UTENTE;
        ex.uid   = *uid;
        ex.gid   = (gid != NULL) ? *gid : *uid;
    }

    return err_posix(_syscall4(SYS_SPAWN, (uint32_t)path, (uint32_t)argc,
                             (uint32_t)argv, (uint32_t)&ex));
}

int spawn_su_console(const char *path, char *const argv[], char *const envp[],
                     const SpawnRedir *redir, int n_redir, int console)
{
    return spawn_completo(path, argv, envp, redir, n_redir, console, NULL, NULL);
}

int spawn_ex(const char *path, char *const argv[], char *const envp[],
             const SpawnRedir *redir, int n_redir)
{
    return spawn_completo(path, argv, envp, redir, n_redir, -1, NULL, NULL);
}

/* =============================================================================
 * spawn_utente — un figlio che e' qualcun altro
 *
 * ! LA PUO' CHIAMARE SOLO root, e chi non lo e' prende EPERM senza che parta
 * niente. Il perche' sta in lib/include/spawn_abi.h: e' la stessa regola di
 * setuid(), spostata dal processo che chiama al processo che nasce.
 * ============================================================================= */
int spawn_utente(const char *path, char *const argv[], char *const envp[],
                 unsigned int uid, unsigned int gid)
{
    return spawn_completo(path, argv, envp, NULL, 0, -1, &uid, &gid);
}

int waitpid(int pid, int *stato, int opzioni)
{
    return err_posix(_syscall3(SYS_WAITPID, (uint32_t)pid,
                             (uint32_t)stato, (uint32_t)opzioni));
}

/* =============================================================================
 * I FILI
 * ============================================================================= */
static void errno_posto_lascia(void);   /* piu' avanti, accanto a __errno_dove */

int thread_crea(void (*fn)(void *), void *arg)
{
    return err_posix(_syscall2(SYS_THREAD_CREA, (uint32_t)fn, (uint32_t)arg));
}

void thread_esci(int codice)
{
    errno_posto_lascia();
    _syscall1(SYS_THREAD_ESCI, (uint32_t)codice);
    for (;;) { }                /* non ci si arriva */
}

int thread_attendi(int tid, int *codice)
{
    return err_posix(_syscall2(SYS_THREAD_ATTENDI, (uint32_t)tid,
                               (uint32_t)codice));
}

/* -----------------------------------------------------------------------------
 * LA CANCELLAZIONE ORDINATA — si chiede, e la risposta la da' il filo
 *
 * ! NON C'E' NIENTE DA FARE QUI DENTRO, ed e' il punto: sono due chiamate di
 * sistema secche. Il messaggio sta nel PCB perche' e' la' che lo puo' mettere
 * chi chiede senza sapere niente di dove sia arrivato il filo, e perche' il
 * kernel e' l'unico che possa scrollare chi dorme. Il perche' delle due parole
 * sta in kernel/include/sched.h.
 *
 * ! E NON ESISTE UN «FERMATI ADESSO», apposta: un filo interrotto dove capita
 * lascia i lucchetti presi e le strutture a meta', e dentro un processo solo
 * quelle sono le strutture di tutti. Chi vuole davvero uccidere un filo puo'
 * ancora farlo con kill — il tid e' un pid — e si prende quel che ne viene.
 * --------------------------------------------------------------------------- */
int thread_ferma(int tid)
{
    return err_posix(_syscall1(SYS_THREAD_FERMA, (uint32_t)tid));
}

int thread_devo_fermarmi(void)
{
    return (int)_syscall1(SYS_THREAD_FERMARSI, 0);
}

/* -----------------------------------------------------------------------------
 * IL LUCCHETTO
 *
 * ! `xchg` E' ATOMICO SENZA `lock`, ed e' l'unica istruzione x86 che lo sia
 * per costruzione: scambiando con la memoria il processore alza il segnale di
 * blocco da solo. Su una macchina a un processore basterebbe anche meno, ma
 * scriverlo giusto ora costa una riga e il giorno che i processori saranno due
 * non ci sara' niente da rileggere.
 *
 * ! E CHI ASPETTA CEDE LA CPU, non gira a vuoto. Con un processore solo, un
 * ciclo che gira senza cedere aspetta chi ha il lucchetto senza mai lasciarlo
 * lavorare: si sbloccherebbe solo allo scadere del quanto. `sched_yield()`
 * trasforma un'attesa di dieci millisecondi in una di pochi microsecondi.
 * --------------------------------------------------------------------------- */
int sched_yield(void);         /* piu' avanti in questo file */

static int mutex_xchg(volatile int *dove, int valore)
{
    __asm__ __volatile__("xchgl %0, %1"
                         : "+r"(valore), "+m"(*dove)
                         :
                         : "memory");
    return valore;
}

/* ! `cmpxchg` VUOLE IL PREFISSO `lock`, a differenza di `xchg` che lo ha per
 * costruzione. Su una macchina a un processore non cambierebbe niente;
 * scriverlo giusto adesso costa un prefisso. */
static int mutex_cmpxchg(volatile int *dove, int atteso, int nuovo)
{
    __asm__ __volatile__("lock cmpxchgl %2, %1"
                         : "+a"(atteso), "+m"(*dove)
                         : "r"(nuovo)
                         : "memory");
    return atteso;
}

int attesa_dormi(volatile int *dove, int atteso, unsigned int ms)
{
    return err_posix(_syscall3(SYS_ATTESA_DORMI, (uint32_t)dove,
                               (uint32_t)atteso, ms));
}

int attesa_sveglia(volatile int *dove, int quanti)
{
    return err_posix(_syscall2(SYS_ATTESA_SVEGLIA, (uint32_t)dove,
                               (uint32_t)quanti));
}

int mutex_prova(volatile int *m)
{
    return mutex_cmpxchg(m, 0, 1) == 0;
}

/* -----------------------------------------------------------------------------
 * IL LUCCHETTO CHE DORME — tre stati, e nessuna chiamata di sistema quando non
 * c'e' contesa
 *
 *   0 = libero
 *   1 = preso, e nessuno sta aspettando
 *   2 = preso, e c'e' ALMENO UNO che dorme
 *
 * ! IL TERZO STATO ESISTE PER CHI LASCIA, non per chi prende: senza, chi
 * lascia dovrebbe chiamare la sveglia ogni volta per il dubbio che qualcuno
 * dorma — cioe' una chiamata di sistema a ogni sblocco, anche quando il
 * lucchetto non l'ha mai voluto nessuno. Con il 2, chi lasciando si ritrova in
 * mano un 1 sa che non c'e' nessuno e non chiama niente.
 *
 * ! E CHI SI ADDORMENTA METTE 2 PRIMA DI DORMIRE, sempre, anche se era 1: e'
 * la promessa che chi lascia trovera' il 2 e chiamera' la sveglia. Metterlo
 * solo «se serve» e' il modo classico di perdere un risveglio.
 * --------------------------------------------------------------------------- */
void mutex_prendi(volatile int *m)
{
    int v = mutex_cmpxchg(m, 0, 1);

    if (v == 0) return;                 /* libero: preso senza chiedere niente */

    if (v != 2) v = mutex_xchg(m, 2);
    while (v != 0) {
        attesa_dormi(m, 2, 0);
        v = mutex_xchg(m, 2);
    }
}

void mutex_lascia(volatile int *m)
{
    if (mutex_xchg(m, 0) == 2) attesa_sveglia(m, 1);
}

/* ! IL PREFISSO `lock` SERVE ANCHE PER UN INCREMENTO, e non solo il giorno che
 * i processori saranno due: leggi-modifica-scrivi e' fatto di tre passi, e due
 * fili che segnalano insieme, interrotti in mezzo, conterebbero un segnale
 * solo. Un segnale perso e' un filo che non si sveglia piu'. */
static void atomico_piu_uno(volatile int *dove)
{
    __asm__ __volatile__("lock incl %0" : "+m"(*dove) : : "memory");
}

/* -----------------------------------------------------------------------------
 * LE VARIABILI DI CONDIZIONE — «lascia il lucchetto, dormi, riprendilo»
 *
 * La condizione E' un contatore di segnali, e non ha altro stato: si dorme su
 * quel contatore, e segnalare vuol dire cambiarlo.
 *
 * ! IL CONTATORE SI LEGGE PRIMA DI LASCIARE IL LUCCHETTO, ed e' l'unica riga
 * che conta davvero. Fra il «lascia» e il «dormi» c'e' una finestra in cui chi
 * segnala puo' passare: senza il valore atteso, quel segnale arriverebbe prima
 * che ci sia qualcuno da svegliare e il filo dormirebbe per sempre. Avendolo
 * letto prima, se qualcuno segnala li' in mezzo il contatore non e' piu' quello
 * e attesa_dormi non dorme affatto — torna subito, e la condizione si
 * ricontrolla. La finestra non si chiude: si rende innocua.
 *
 * ! PERCIO' SI ASPETTA DENTRO UN `while`, MAI DENTRO UN `if`. Il risveglio
 * dice «guarda di nuovo», non «adesso c'e'»: puo' tornare per un segnale, per
 * la scadenza, o perche' il contatore era gia' cambiato. Chi controlla una
 * volta sola prima o poi prosegue senza che la condizione sia vera.
 *
 * ! E IL LUCCHETTO TORNA IN MANO A CHI ESCE, sempre, anche quando non si e'
 * dormito: chi chiama continua a leggere lo stato protetto come se non fosse
 * successo niente, che e' tutto il senso di questa funzione.
 *
 * ! LA SCADENZA NON SI DISTINGUE DAL RISVEGLIO, perche' il kernel non lo dice
 * (sys_attesa_dormi rende 0 in tutti e due i casi). Non e' un problema per chi
 * usa il `while`: e' una rete, non un esito. Distinguerla vorrebbe una riga
 * dentro sys_attesa_dormi che confronti g_ticks con block_until.
 * --------------------------------------------------------------------------- */
void condizione_aspetta_ms(volatile int *c, volatile int *m, unsigned int ms)
{
    int visto = *c;             /* PRIMA di lasciare: e' tutta qui la corsa */

    mutex_lascia(m);
    attesa_dormi(c, visto, ms);
    mutex_prendi(m);
}

void condizione_aspetta(volatile int *c, volatile int *m)
{
    condizione_aspetta_ms(c, m, 0);
}

void condizione_segnala(volatile int *c)
{
    atomico_piu_uno(c);
    attesa_sveglia(c, 1);
}

void condizione_segnala_tutti(volatile int *c)
{
    atomico_piu_uno(c);
    attesa_sveglia(c, 0);
}

/* -----------------------------------------------------------------------------
 * I SEMAFORI — un contatore, piu' l'attesa
 *
 * Prendere = se e' maggiore di zero, decrementalo; se e' zero, dormici sopra.
 * Lasciare = incrementalo e sveglia uno.
 *
 * ! IL VALORE ATTESO E' ZERO, e chiude la stessa finestra di sempre: fra il
 * momento in cui si vede il contatore a zero e quello in cui ci si addormenta,
 * chi lascia puo' averlo portato a uno. Il kernel lo rilegge a interruzioni
 * spente, vede che non e' piu' zero e non fa dormire nessuno; il giro dopo il
 * cmpxchg riesce.
 *
 * ! SI RIPROVA IN CERCHIO E NON UNA VOLTA SOLA, perche' fra il risveglio e il
 * cmpxchg un altro filo puo' essersi preso il posto: svegliarsi non e' avere.
 *
 * ! CHI LASCIA CHIAMA LA SVEGLIA SEMPRE, anche quando non dorme nessuno — cioe'
 * una chiamata di sistema per ogni `lascia`. Il lucchetto quella spesa la evita
 * col terzo stato, ma li' il numero E' lo stato del lucchetto e ci sta dentro
 * anche il «c'e' chi dorme»; qui il contatore e' un conto di posti e non ha
 * dove metterlo. La via, il giorno che qualcuno la misuri, e' un secondo campo
 * «dormienti» incrementato con `lock` prima di addormentarsi e guardato da chi
 * lascia dopo aver incrementato: due `lock` che si fanno da barriera a vicenda,
 * cosi' almeno uno dei due vede l'altro. E' una struttura al posto di un
 * intero, cioe' un tipo nuovo nell'ABI: non si fa prima di avere il numero.
 * --------------------------------------------------------------------------- */
int semaforo_prova(volatile int *s)
{
    int v = *s;

    while (v > 0) {
        if (mutex_cmpxchg(s, v, v - 1) == v) return 1;
        v = *s;                 /* qualcuno ci e' arrivato prima: rileggi */
    }
    return 0;
}

void semaforo_prendi(volatile int *s)
{
    for (;;) {
        if (semaforo_prova(s)) return;
        attesa_dormi(s, 0, 0);
    }
}

int semaforo_prendi_ms(volatile int *s, unsigned int ms)
{
    unsigned int fine = uptime_ms() + ms;

    for (;;) {
        unsigned int ora;

        if (semaforo_prova(s)) return 0;

        /* ! LA DIFFERENZA SI GUARDA COL SEGNO, non i due numeri fra loro:
         * uptime_ms() gira dopo quarantanove giorni, e `ora > fine` di la'
         * direbbe il contrario di quel che e'. */
        ora = uptime_ms();
        if ((int)(ora - fine) >= 0) { errno = ETIMEDOUT; return -1; }
        attesa_dormi(s, 0, fine - ora);
    }
}

void semaforo_lascia(volatile int *s)
{
    atomico_piu_uno(s);
    attesa_sveglia(s, 1);
}

/* =============================================================================
 * I THREAD POSIX (@PTHREAD, 28 settembre 2026 — tappa 1 di Exilla)
 *
 * L'interfaccia di lib/include/pthread.h sopra i fili di qui sopra: il perche'
 * di ogni scelta sta la'. Qui l'aritmetica.
 *
 * ! LE STRUTTURE SONO RIPETUTE, come tutto in questo file: libc.c non include
 * il proprio header (vedi in cima). DEVONO COINCIDERE con pthread.h e
 * semaphore.h campo per campo — un campo in piu' da una parte sola sposta
 * tutti quelli dopo, e i lucchetti di chi ha compilato con l'header
 * diventerebbero i contatori di chi ha compilato con la libc.
 *
 * ! IL DESCRITTORE DI UN FILO STA IN UNA TABELLA FISSA, trovata dal thread
 * pointer (`%gs:0`) come errno: ogni filo ha il suo TCB, e il suo indirizzo e'
 * una chiave buona. Fissa perche' la dimensione e' quella dei fili possibili
 * (FILI_MAX_PER_PROCESSO, con margine), e perche' pthread_self dev'essere
 * chiamabile anche prima che malloc sia pronta.
 * ============================================================================= */
#define EDEADLK       35
#define PTHREAD_FILI  128     /* = FILO_MAX del kernel (0.231) */
#define P_CHIAVI      128
#define P_ITERAZIONI  4

typedef struct {
    int     staccato;
    size_t  pila;
    void   *pila_base;
    size_t  pila_misura;
} pthread_attr_t;
typedef struct { volatile int m; int tipo; volatile int padrone; int conta; } pthread_mutex_t;
typedef struct { int tipo; } pthread_mutexattr_t;
typedef struct { volatile int c; int orologio; } pthread_cond_t;
typedef struct { int orologio; } pthread_condattr_t;
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  c;
    int lettori, scrittore, scrittori_attesa;
} pthread_rwlock_t;
typedef struct { int nulla; } pthread_rwlockattr_t;
typedef unsigned long pthread_t;
typedef unsigned int  pthread_key_t;
struct sched_param { int sched_priority; };
typedef struct { volatile int s; } sem_t;

void thread_esci(int codice);
int  thread_attendi(int tid, int *codice);
int  thread_crea(void (*fn)(void *), void *arg);
void mutex_prendi(volatile int *m);
int  mutex_prova(volatile int *m);
void mutex_lascia(volatile int *m);
void condizione_aspetta(volatile int *c, volatile int *m);
void condizione_aspetta_ms(volatile int *c, volatile int *m, unsigned int ms);
void condizione_segnala(volatile int *c);
void condizione_segnala_tutti(volatile int *c);
void semaforo_prendi(volatile int *s);
int  semaforo_prendi_ms(volatile int *s, unsigned int ms);
int  semaforo_prova(volatile int *s);
void semaforo_lascia(volatile int *s);
int  getpid(void);
int  raise(int sig);
int  nanosleep(const struct timespec *req, struct timespec *rem);
int sched_yield(void);

typedef struct {
    volatile int usato;
    volatile int tp;            /* il thread pointer del filo: la chiave */
    int          tid;
    void      *(*fn)(void *);
    void        *arg;
    void        *ris;
    volatile int finito;
    volatile int staccato;
    char         nome[16];
    void        *chiavi[P_CHIAVI];
} Filo;

static Filo         g_fili[PTHREAD_FILI];
static volatile int g_fili_m = 0;                  /* Mutex della tabella */
static volatile int g_chiavi_usate[P_CHIAVI];
static void       (*g_chiavi_distr[P_CHIAVI])(void *);

static unsigned int tp_mio(void)
{
    unsigned int tp;
    __asm__ __volatile__("movl %%gs:0, %0" : "=r"(tp));
    return tp;
}

static Filo *filo_libero(void)
{
    int i;

    mutex_prendi(&g_fili_m);
    for (i = 0; i < PTHREAD_FILI; i++)
        if (!g_fili[i].usato) {
            memset(&g_fili[i], 0, sizeof(Filo));
            g_fili[i].usato = 1;
            mutex_lascia(&g_fili_m);
            return &g_fili[i];
        }
    mutex_lascia(&g_fili_m);
    return NULL;
}

static void filo_rilascia(Filo *f)
{
    mutex_prendi(&g_fili_m);
    f->usato = 0;
    f->tp    = 0;
    mutex_lascia(&g_fili_m);
}

/* ! IL FILO PRINCIPALE (e ogni filo nato con thread_crea invece che con
 * pthread_create) NON HA UN DESCRITTORE finche' non lo chiede: se ne fa uno
 * alla prima domanda. Staccato, perche' nessuno lo aspettera' con join. */
pthread_t pthread_self(void)
{
    unsigned int tp = tp_mio();
    Filo        *f;
    int          i;

    for (i = 0; i < PTHREAD_FILI; i++)
        if (g_fili[i].usato && (unsigned int)g_fili[i].tp == tp && tp != 0)
            return (pthread_t)&g_fili[i];

    f = filo_libero();
    if (!f) return 0;
    f->tp       = (int)tp;
    f->tid      = filo_id();
    f->staccato = 1;
    return (pthread_t)f;
}

int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

static void chiavi_distruggi(Filo *f)
{
    int giro, k, ancora;

    for (giro = 0; giro < P_ITERAZIONI; giro++) {
        ancora = 0;
        for (k = 0; k < P_CHIAVI; k++) {
            void *v = f->chiavi[k];

            if (v && g_chiavi_usate[k] && g_chiavi_distr[k]) {
                f->chiavi[k] = NULL;
                g_chiavi_distr[k](v);
                ancora = 1;
            }
        }
        if (!ancora) break;
    }
}

void pthread_exit(void *ris)
{
    Filo *f = (Filo *)pthread_self();

    if (f) {
        chiavi_distruggi(f);
        f->ris    = ris;
        f->finito = 1;
        /* Staccato: nessuno verra' a prendere il risultato, il posto torna
         * libero subito. Il PCB del filo lo raccoglie il reaper di init: il
         * kernel lo sa da SYS_THREAD_STACCA (kernel 0.222). */
        if (f->staccato) filo_rilascia(f);
    }
    thread_esci(0);
    for (;;) { }
}

/* Il primo codice di ogni filo nato qui: si fa riconoscere, poi chiama.
 *
 * ! force_align_arg_pointer: la pila che il kernel da' al filo non e'
 * allineata a 16, e il codice SSE di Firefox (PremultiplyRow_SSE2, un movdqa
 * su una variabile locale) cadeva con #GP sul filo di WebRender (3 ottobre
 * 2026). GCC riallinea qui, e da qui in giu' l'ABI e' rispettata. */
__attribute__((force_align_arg_pointer, noinline))
static void filo_trampolino(void *arg)
{
    Filo *f = (Filo *)arg;

    f->tp = (int)tp_mio();
    pthread_exit(f->fn(f->arg));
}

int pthread_create(pthread_t *t, const pthread_attr_t *a,
                   void *(*fn)(void *), void *arg)
{
    Filo *f;
    int   tid;

    if (!t || !fn) return EINVAL;
    g_ci_sono_fili = 1;                 /* vedi heap_restituisci */
    f = filo_libero();
    if (!f) return EAGAIN;
    f->fn       = fn;
    f->arg      = arg;
    f->staccato = a ? a->staccato : 0;

    tid = thread_crea(filo_trampolino, f);
    if (tid < 0) {
        int e = errno;
        filo_rilascia(f);
        return e ? e : EAGAIN;
    }
    f->tid = tid;
    *t = (pthread_t)f;
    /* ! IL KERNEL LO SA DOPO, e va bene: se il filo e' gia' finito il kernel
     * lo trova zombie e lo passa a init lo stesso. Il descrittore qui non si
     * tocca piu': un filo staccato lo libera da se' in pthread_exit. */
    if (f->staccato) _syscall1(SYS_THREAD_STACCA, (uint32_t)tid);
    return 0;
}

int pthread_join(pthread_t t, void **ris)
{
    Filo *f = (Filo *)t;

    if (!f || !f->usato) return ESRCH;
    if (f->staccato) return EINVAL;
    if (f->tp != 0 && (unsigned int)f->tp == tp_mio()) return EDEADLK;
    if (thread_attendi(f->tid, NULL) != 0) return errno ? errno : ESRCH;
    if (ris) *ris = f->ris;
    filo_rilascia(f);
    return 0;
}

/* EX-OS non ha fork(): non c'e' niente da preparare, i gestori non
 * serviranno mai. Solo in libc.a. */
#ifndef EXOS_LIBC_SO
int pthread_atfork(void (*prima)(void), void (*genitore)(void), void (*figlio)(void))
{
    (void)prima; (void)genitore; (void)figlio;
    return 0;
}
#endif

int pthread_detach(pthread_t t)
{
    Filo *f = (Filo *)t;

    if (!f || !f->usato) return ESRCH;
    if (f->staccato) return EINVAL;
    f->staccato = 1;
    _syscall1(SYS_THREAD_STACCA, (uint32_t)f->tid);
    if (f->finito) filo_rilascia(f);
    return 0;
}

int pthread_yield(void) { sched_yield(); return 0; }

/* <sched.h>: uno scheduler con una priorita' sola per i fili. Solo in
 * libc.a (il floppy e' pieno). */
#ifndef EXOS_LIBC_SO
int sched_get_priority_min(int politica) { (void)politica; return 0; }
int sched_get_priority_max(int politica) { (void)politica; return 0; }
#endif

#ifdef EXOS_LIBC_SO
/* I segnali di EX-OS sono del processo e si consegnano dentro (raise): a se
 * stessi si puo', a un altro filo no, e lo si dice. */
int pthread_kill(pthread_t t, int segnale)
{
    if (t != pthread_self()) return ENOSYS;
    if (segnale == 0) return 0;
    return raise(segnale) == 0 ? 0 : EINVAL;
}

int pthread_sigmask(int come, const sigset_t *nuovo, sigset_t *vecchio)
{
    (void)come; (void)nuovo; (void)vecchio;
    return 0;
}
#else
/* Kernel 0.227: the signal goes to that thread (its tid is a pid). The
 * thread that calls is 0, which is also how the main thread — whose Filo
 * may have no tid — names itself. */
int pthread_kill(pthread_t t, int segnale)
{
    Filo   *f = (Filo *)t;
    int32_t r;

    if (!f) return ESRCH;
    r = _syscall2(SYS_SEG_MANDA,
                  (t == pthread_self()) ? 0u : (uint32_t)f->tid,
                  (uint32_t)segnale);
    return (r < 0) ? -r : 0;
}

int pthread_sigmask(int come, const sigset_t *nuovo, sigset_t *vecchio)
{
    return sigprocmask(come, nuovo, vecchio) < 0 ? errno : 0;
}
#endif

int pthread_setname_np(pthread_t t, const char *nome)
{
    Filo *f = (Filo *)t;

    if (!f || !nome) return EINVAL;
    strncpy(f->nome, nome, sizeof(f->nome) - 1);
    f->nome[sizeof(f->nome) - 1] = '\0';
    return 0;
}

int pthread_getname_np(pthread_t t, char *nome, size_t max)
{
    Filo *f = (Filo *)t;

    if (!f || !nome || max == 0) return EINVAL;
    strncpy(nome, f->nome, max - 1);
    nome[max - 1] = '\0';
    return 0;
}

/* ! I LIMITI DELLA PILA LI DICE IL KERNEL (SYS_THREAD_PILA, kernel 0.222):
 * e' lui che l'ha piazzata. Chi misura la pila con questi numeri
 * (SpiderMonkey) su un numero inventato scriverebbe fuori. La base e' il
 * fondo, come in POSIX; la misura arriva fino alla cima utile. */
int pthread_getattr_np(pthread_t t, pthread_attr_t *a)
{
    Filo        *f = (Filo *)t;
    unsigned int pila[2];
    int          r;

    if (!a || !f) return EINVAL;
    memset(a, 0, sizeof(*a));
    r = (int)_syscall2(SYS_THREAD_PILA, (uint32_t)f->tid, (uint32_t)pila);
    if (r < 0) return -r;
    a->staccato    = f->staccato;
    a->pila_base   = (void *)pila[0];
    a->pila_misura = pila[1] - pila[0];
    a->pila        = a->pila_misura;
    return 0;
}

int pthread_setschedparam(pthread_t t, int politica, const struct sched_param *p)
{ (void)t; (void)politica; (void)p; return 0; }
int pthread_getschedparam(pthread_t t, int *politica, struct sched_param *p)
{ (void)t; if (politica) *politica = 0; if (p) p->sched_priority = 0; return 0; }

/* --- gli attributi ---------------------------------------------------------- */
int pthread_attr_init(pthread_attr_t *a)
{ if (!a) return EINVAL; memset(a, 0, sizeof(*a)); a->pila = 2u * 1024u * 1024u; return 0; }
int pthread_attr_destroy(pthread_attr_t *a) { (void)a; return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *a, int s)
{ if (!a || (s != 0 && s != 1)) return EINVAL; a->staccato = s; return 0; }
int pthread_attr_getdetachstate(const pthread_attr_t *a, int *s)
{ if (!a || !s) return EINVAL; *s = a->staccato; return 0; }
int pthread_attr_setstacksize(pthread_attr_t *a, size_t m)
{ if (!a || m < 16384) return EINVAL; a->pila = m; return 0; }
int pthread_attr_getstacksize(const pthread_attr_t *a, size_t *m)
{ if (!a || !m) return EINVAL; *m = a->pila; return 0; }
int pthread_attr_getstack(const pthread_attr_t *a, void **b, size_t *m)
{ if (!a || !b || !m) return EINVAL; *b = a->pila_base; *m = a->pila_misura; return 0; }
int pthread_attr_setguardsize(pthread_attr_t *a, size_t m) { (void)a; (void)m; return 0; }
int pthread_attr_setscope(pthread_attr_t *a, int x) { (void)a; (void)x; return 0; }
int pthread_attr_setinheritsched(pthread_attr_t *a, int x) { (void)a; (void)x; return 0; }
int pthread_attr_setschedpolicy(pthread_attr_t *a, int x) { (void)a; (void)x; return 0; }
int pthread_attr_getschedpolicy(const pthread_attr_t *a, int *x)
{ (void)a; if (x) *x = 0; return 0; }
int pthread_attr_setschedparam(pthread_attr_t *a, const struct sched_param *p)
{ (void)a; (void)p; return 0; }
int pthread_attr_getschedparam(const pthread_attr_t *a, struct sched_param *p)
{ (void)a; if (p) p->sched_priority = 0; return 0; }

/* --- i lucchetti ---------------------------------------------------------------
 * ! IL PADRONE SI SEGNA SOLO DOVE SERVE, cioe' nei ricorsivi e in quelli col
 * controllo: getpid() e' una chiamata di sistema, e un lucchetto normale non
 * deve pagarla a ogni presa. */
int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a)
{
    if (!m) return EINVAL;
    m->m = 0; m->padrone = 0; m->conta = 0;
    m->tipo = a ? a->tipo : 0;
    return 0;
}
int pthread_mutex_destroy(pthread_mutex_t *m) { return (m && m->m) ? EBUSY : 0; }

int pthread_mutex_lock(pthread_mutex_t *m)
{
    int io;

    if (!m) return EINVAL;
    if (m->tipo == 0) { mutex_prendi(&m->m); return 0; }
    io = filo_id();
    if (m->padrone == io) {
        if (m->tipo == 1) { m->conta++; return 0; }
        return EDEADLK;
    }
    mutex_prendi(&m->m);
    m->padrone = io;
    m->conta   = 1;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
    int io;

    if (!m) return EINVAL;
    if (m->tipo == 0) return mutex_prova(&m->m) ? 0 : EBUSY;
    io = filo_id();
    if (m->padrone == io) {
        if (m->tipo == 1) { m->conta++; return 0; }
        return EBUSY;
    }
    if (!mutex_prova(&m->m)) return EBUSY;
    m->padrone = io;
    m->conta   = 1;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
    if (!m) return EINVAL;
    if (m->tipo != 0) {
        if (m->padrone != filo_id()) return EPERM;
        if (--m->conta > 0) return 0;
        m->padrone = 0;
    }
    mutex_lascia(&m->m);
    return 0;
}

/* Quanti millisecondi mancano a `quando` sull'orologio `orologio`: 0 se e'
 * gia' passato. */
int clock_gettime(int orologio, struct timespec *ts);
static unsigned int ms_a(const struct timespec *quando, int orologio)
{
    struct timespec ora;
    long long diff;
    long secondi, ms;

    /* ! A 32 BIT, NON A 64: la divisione di un long long vuole __divdi3, che
     * libc.so non porta con se'. Si tagliano i secondi a ventiquattro giorni —
     * oltre, i millisecondi non stanno comunque in un int. La sottrazione dei
     * time_t si fa a 64 (non divide) e si taglia prima di scendere a 32. */
    if (clock_gettime(orologio, &ora) != 0) return 0;
    diff = quando->tv_sec - ora.tv_sec;
    if (diff < 0) return 0;
    secondi = diff > 2000000 ? 2000000 : (long)diff;
    ms = secondi * 1000 + (quando->tv_nsec - ora.tv_nsec) / 1000000;
    return ms > 0 ? (unsigned int)ms : 0;
}

int pthread_mutex_timedlock(pthread_mutex_t *m, const struct timespec *quando)
{
    for (;;) {
        unsigned int resta;
        int          r = pthread_mutex_trylock(m);

        if (r != EBUSY) return r;
        resta = quando ? ms_a(quando, 0) : 1;
        if (resta == 0) return ETIMEDOUT;
        {
            struct timespec pausa = { 0, 1000000 };    /* un tick, arrotondato */
            nanosleep(&pausa, NULL);
        }
    }
}

int pthread_mutexattr_init(pthread_mutexattr_t *a) { if (!a) return EINVAL; a->tipo = 0; return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t *a) { (void)a; return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int t)
{ if (!a || t < 0 || t > 2) return EINVAL; a->tipo = t; return 0; }
int pthread_mutexattr_gettype(const pthread_mutexattr_t *a, int *t)
{ if (!a || !t) return EINVAL; *t = a->tipo; return 0; }

/* --- le condizioni ---------------------------------------------------------------
 * ! UN LUCCHETTO RICORSIVO SI LASCIA TUTTO E SI RIPRENDE TUTTO: condizione_
 * aspetta lascia il Mutex di sotto, e il conteggio va rimesso com'era dopo. */
int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a)
{ if (!c) return EINVAL; c->c = 0; c->orologio = a ? a->orologio : 0; return 0; }
int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }

static int cond_aspetta(pthread_cond_t *c, pthread_mutex_t *m, int con_scadenza,
                        unsigned int ms)
{
    int padrone = m->padrone, conta = m->conta;

    if (m->tipo != 0) { m->padrone = 0; m->conta = 0; }
    if (con_scadenza) condizione_aspetta_ms(&c->c, &m->m, ms ? ms : 1);
    else              condizione_aspetta(&c->c, &m->m);
    if (m->tipo != 0) { m->padrone = padrone; m->conta = conta; }
    return 0;
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
    if (!c || !m) return EINVAL;
    return cond_aspetta(c, m, 0, 0);
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m,
                           const struct timespec *quando)
{
    unsigned int ms;

    if (!c || !m || !quando) return EINVAL;
    ms = ms_a(quando, c->orologio);
    if (ms == 0) return ETIMEDOUT;
    cond_aspetta(c, m, 1, ms);
    /* condizione_aspetta_ms non dice se e' scaduta: lo dice l'orologio. */
    return ms_a(quando, c->orologio) == 0 ? ETIMEDOUT : 0;
}

int pthread_cond_signal(pthread_cond_t *c)    { if (!c) return EINVAL; condizione_segnala(&c->c); return 0; }
int pthread_cond_broadcast(pthread_cond_t *c) { if (!c) return EINVAL; condizione_segnala_tutti(&c->c); return 0; }
int pthread_condattr_init(pthread_condattr_t *a) { if (!a) return EINVAL; a->orologio = 0; return 0; }

/* ! PTHREAD_PROCESS_SHARED SI ACCETTA E NON CAMBIA NIENTE: il lucchetto e'
 * un futex nella memoria del chiamante, e funziona fra processi solo se
 * quella memoria e' condivisa — cosa che il lucchetto non puo' sapere. Gecko
 * lo chiede per i lucchetti fra processi, e su EX-OS gira in un processo solo.
 * Solo in libc.a. */
#ifndef EXOS_LIBC_SO
int pthread_mutexattr_setpshared(pthread_mutexattr_t *a, int p) { return (a && (p == 0 || p == 1)) ? 0 : EINVAL; }
int pthread_mutexattr_getpshared(const pthread_mutexattr_t *a, int *p) { if (!a || !p) return EINVAL; *p = 0; return 0; }
int pthread_condattr_setpshared(pthread_condattr_t *a, int p) { return (a && (p == 0 || p == 1)) ? 0 : EINVAL; }
int pthread_condattr_getpshared(const pthread_condattr_t *a, int *p) { if (!a || !p) return EINVAL; *p = 0; return 0; }
#endif
int pthread_condattr_destroy(pthread_condattr_t *a) { (void)a; return 0; }
int pthread_condattr_setclock(pthread_condattr_t *a, int o)
{ if (!a || (o != 0 && o != 1)) return EINVAL; a->orologio = o; return 0; }
int pthread_condattr_getclock(const pthread_condattr_t *a, int *o)
{ if (!a || !o) return EINVAL; *o = a->orologio; return 0; }

/* --- lettori e scrittori -------------------------------------------------------------
 * ! CHI ASPETTA DI SCRIVERE PASSA DAVANTI ai lettori nuovi: altrimenti un
 * flusso continuo di letture lo farebbe aspettare per sempre. */
int pthread_rwlock_init(pthread_rwlock_t *l, const pthread_rwlockattr_t *a)
{
    (void)a;
    if (!l) return EINVAL;
    memset(l, 0, sizeof(*l));
    return 0;
}
int pthread_rwlock_destroy(pthread_rwlock_t *l) { (void)l; return 0; }

int pthread_rwlock_rdlock(pthread_rwlock_t *l)
{
    pthread_mutex_lock(&l->m);
    while (l->scrittore || l->scrittori_attesa) pthread_cond_wait(&l->c, &l->m);
    l->lettori++;
    pthread_mutex_unlock(&l->m);
    return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *l)
{
    int r = EBUSY;

    pthread_mutex_lock(&l->m);
    if (!l->scrittore && !l->scrittori_attesa) { l->lettori++; r = 0; }
    pthread_mutex_unlock(&l->m);
    return r;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *l)
{
    pthread_mutex_lock(&l->m);
    l->scrittori_attesa++;
    while (l->scrittore || l->lettori) pthread_cond_wait(&l->c, &l->m);
    l->scrittori_attesa--;
    l->scrittore = 1;
    pthread_mutex_unlock(&l->m);
    return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *l)
{
    int r = EBUSY;

    pthread_mutex_lock(&l->m);
    if (!l->scrittore && !l->lettori) { l->scrittore = 1; r = 0; }
    pthread_mutex_unlock(&l->m);
    return r;
}

int pthread_rwlock_unlock(pthread_rwlock_t *l)
{
    pthread_mutex_lock(&l->m);
    if (l->scrittore) l->scrittore = 0;
    else if (l->lettori > 0) l->lettori--;
    pthread_cond_broadcast(&l->c);
    pthread_mutex_unlock(&l->m);
    return 0;
}

/* --- una volta sola ---------------------------------------------------------------------
 * 0 = mai, 1 = in corso, 2 = fatto. ! Chi arriva mentre e' in corso ASPETTA
 * che finisca: tornare subito vorrebbe dire usare quel che non e' ancora
 * pronto. */
static volatile int g_una_m = 0;
static volatile int g_una_c = 0;

int pthread_once(volatile int *o, void (*fn)(void))
{
    if (!o || !fn) return EINVAL;
    if (*o == 2) return 0;
    mutex_prendi(&g_una_m);
    if (*o == 0) {
        *o = 1;
        mutex_lascia(&g_una_m);
        fn();
        mutex_prendi(&g_una_m);
        *o = 2;
        condizione_segnala_tutti(&g_una_c);
    } else {
        while (*o != 2) condizione_aspetta(&g_una_c, &g_una_m);
    }
    mutex_lascia(&g_una_m);
    return 0;
}

/* --- le chiavi ----------------------------------------------------------------------------- */
int pthread_key_create(pthread_key_t *k, void (*distr)(void *))
{
    int i;

    if (!k) return EINVAL;
    mutex_prendi(&g_fili_m);
    for (i = 0; i < P_CHIAVI; i++)
        if (!g_chiavi_usate[i]) {
            int j;
            g_chiavi_usate[i] = 1;
            g_chiavi_distr[i] = distr;
            for (j = 0; j < PTHREAD_FILI; j++) g_fili[j].chiavi[i] = NULL;
            mutex_lascia(&g_fili_m);
            *k = (pthread_key_t)i;
            return 0;
        }
    mutex_lascia(&g_fili_m);
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t k)
{
    if (k >= P_CHIAVI || !g_chiavi_usate[k]) return EINVAL;
    g_chiavi_usate[k] = 0;
    g_chiavi_distr[k] = NULL;
    return 0;
}

void *pthread_getspecific(pthread_key_t k)
{
    Filo *f;

    if (k >= P_CHIAVI) return NULL;
    f = (Filo *)pthread_self();
    return f ? f->chiavi[k] : NULL;
}

int pthread_setspecific(pthread_key_t k, const void *v)
{
    Filo *f;

    if (k >= P_CHIAVI || !g_chiavi_usate[k]) return EINVAL;
    f = (Filo *)pthread_self();
    if (!f) return ENOMEM;
    f->chiavi[k] = (void *)v;
    return 0;
}

/* --- i semafori POSIX ------------------------------------------------------------------------ */
int sem_init(sem_t *s, int pshared, unsigned int v)
{
    if (!s) { errno = EINVAL; return -1; }
    if (pshared) { errno = ENOSYS; return -1; }
    s->s = (int)v;
    return 0;
}
int sem_destroy(sem_t *s) { (void)s; return 0; }
int sem_wait(sem_t *s) { semaforo_prendi(&s->s); return 0; }
int sem_trywait(sem_t *s)
{
    if (semaforo_prova(&s->s)) return 0;
    errno = EAGAIN;
    return -1;
}
int sem_timedwait(sem_t *s, const struct timespec *quando)
{
    unsigned int ms = ms_a(quando, 0);

    if (semaforo_prova(&s->s)) return 0;
    if (ms == 0) { errno = ETIMEDOUT; return -1; }
    return semaforo_prendi_ms(&s->s, ms);
}
int sem_post(sem_t *s) { semaforo_lascia(&s->s); return 0; }
int sem_getvalue(sem_t *s, int *v) { *v = s->s; return 0; }

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
        /* ! ERA ZERO, SEMPRE, ED E' COSTATO UN `make` CHE NON TROVAVA IL
         * MAKEFILE. Su Unix d_ino == 0 vuol dire «voce cancellata», e il
         * codice di terzi salta quelle voci: GNU make lo fa nella propria
         * dir.c (`REAL_DIR_ENTRY`), quindi vedeva ogni directory VUOTA e
         * rispondeva «No targets specified and no makefile found» dentro
         * una directory con dentro un makefile.
         *
         * Adesso e' l'identita' vera, la stessa che stat() mette in st_ino:
         * la compone il VFS con VFS_IDENT e arriva fin qui dentro DirEntry.
         * Che i due combacino non e' un di piu' — confrontare d_ino e
         * st_ino dello stesso file e' una cosa che si fa. */
        d->corrente.d_ino  = v->ident;
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
 * ! mkstemp APRE il file, e questo e' il punto: un tmpnam() seguito da
 * open() ha in mezzo una finestra in cui qualcun altro puo' creare quel
 * nome. Qui la finestra resta (manca O_EXCL nel kernel), ma il nome e' gia'
 * improbabile da indovinare e il file viene creato subito.
 *
 * -----------------------------------------------------------------------------
 * ! LA DIRECTORY VIENE DA `TMPDIR`, E SENZA E' LA RADICE
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

/* mkstemps: come mkstemp, ma le sei X stanno prima di un suffisso lungo
 * `suffisso` caratteri ("prefXXXXXX.png", 4). Le X si riempiono con cifre
 * esadecimali di random(). Solo in libc.a. */
#ifndef EXOS_LIBC_SO
long random(void);                  /* piu' avanti in questo file */

int mkstemps(char *modello, int suffisso)
{
    static const char cifre[] = "0123456789abcdef";
    size_t len;
    int    fd, tentativi, j;

    if (modello == NULL || suffisso < 0) { errno = EINVAL; return -1; }
    len = strlen(modello);
    if (len < 6 + (size_t)suffisso) { errno = EINVAL; return -1; }

    for (tentativi = 0; tentativi < 32; tentativi++) {
        char *x = modello + len - suffisso - 6;
        unsigned long r = (unsigned long)random() ^ ((unsigned long)getpid() << 16);

        for (j = 0; j < 6; j++, r >>= 4) x[j] = cifre[r & 0xf];
        if (access(modello, F_OK) == 0) continue;
        fd = open(modello, O_RDWR | O_CREAT | O_TRUNC);
        if (fd >= 0) return fd;
    }
    errno = EEXIST;
    return -1;
}
#endif

/* ! mktemp E' LA VERSIONE INSICURA DI mkstemp, e lo e' per costruzione:
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
 * ! chmod, fchmod e umask NON CAMBIANO NIENTE
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
/* ! DAL 17 AGOSTO 2026 chmod FA QUALCOSA DAVVERO. Era inerte — e il commento
 * qui sopra spiega perche' esisteva comunque: bfd chiude ogni eseguibile che
 * produce con umask/chmod, e senza il nome binutils non si collegava. Adesso
 * c'e' una syscall dietro, e su ext2 i permessi cambiano per davvero.
 *
 * ! SU FAT RENDE 0, NON UN ERRORE, e il contrario di chown: chmod chiede di
 * mettere dei permessi, e su un volume senza permessi lo stato che si ottiene
 * — nessuna restrizione — e' gia' quello. chown chiede di consegnare un file a
 * un altro utente, e QUELLO su FAT non si puo' fare affatto. E' anche il
 * comportamento di Linux su vfat, e il motivo e' concreto: bfd chiude ogni
 * eseguibile che produce con umask/chmod. */
/* -----------------------------------------------------------------------------
 * fb_map — il framebuffer, e solo quello
 *
 * ! NON SERVE ESSERE root NE' UN DRIVER, ed e' il punto: mmio_map() mappa un
 * indirizzo fisico qualunque e per questo e' riservata: con i registri di un
 * dispositivo si arriva al DMA, e col DMA a tutta la RAM. Qui l'indirizzo lo
 * sceglie il kernel, non chi chiama — e a chi attacca resta niente da scegliere.
 *
 * Rende il puntatore, oppure 0 con errno impostato.
 * --------------------------------------------------------------------------- */
void *fb_map(void)
{
    int32_t r = _syscall1(SYS_FB_MAP, 0);

    if (r < 0) { errno = -r; return 0; }
    return (void *)(unsigned int)r;
}

int chmod(const char *path, mode_t modo)
{
    int32_t r = _syscall2(SYS_CHMOD, (uint32_t)path, (uint32_t)modo);

    if (r < 0) { errno = -r; return -1; }
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
 * ! FINO ALLA 0.160 QUESTA FUNZIONE ERA UNA COPIA SEGUITA DA UNA
 * CANCELLAZIONE, e portava il nome di un'altra cosa. Le conseguenze non
 * erano teoriche:
 *   - costava quanto il file, mentre una rinomina vera non muove niente;
 *   - RIALLOCAVA i blocchi, quindi un file contiguo poteva tornare
 *     frammentato — ed e' proprio cio' che rendeva impossibile a
 *     `install` verificare la mappa dei settori prima di dare al kernel
 *     il suo nome definitivo.
 *
 * Ora e' la syscall SYS_RENAME, che riscrive la voce di directory e basta.
 * ! I BLOCCHI NON SI SPOSTANO: e' la garanzia su cui si regge
 * l'installatore.
 *
 * ! DUE DIFFERENZE DA POSIX, dichiarate:
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

#ifdef EXOS_LIBC_SO
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

#else /* libc.a: signals delivered by the kernel */
/* =============================================================================
 * SIGNALS, DELIVERED BY THE KERNEL (kernel 0.227, @SEGNALI)
 *
 * The kernel knows, per signal, only "default / ignore / caught" with the
 * flags and the extra mask, plus ONE entry point: __exos_segnale_entra below.
 * The handlers are here, in g_azioni. When a signal comes the kernel writes a
 * SegTelaio on the stack (the interrupted registers, siginfo, ucontext) and
 * jumps to the entry point with ESP on it; the entry point saves the FPU,
 * calls the handler through __exos_segnale_esegui, restores the FPU and asks
 * the kernel to resume the interrupted code (SYS_SEG_RITORNO).
 *
 * ! THE FPU IS SAVED HERE, NOT BY THE KERNEL: FNSAVE in ring 3 is allowed and
 * costs the kernel nothing. It is done in assembly BEFORE any C runs, since
 * compiled code may use the x87 anywhere; FNSAVE also resets the unit, so the
 * handler starts from a clean state. 108 bytes: FNSAVE, not FXSAVE, because
 * the baseline is the Pentium MMX (MMX registers are the x87 ones).
 *
 * ! ONLY IN libc.a, like the sockets: the libc.so on the floppy has no room.
 * There, signal() and raise() are the old ones above.
 * ============================================================================= */

/* ! DUPLICATED BY HAND from kernel/include/syscall.h. */
typedef struct {
    uint32_t tipo;          /* 0 default, 1 ignore, 2 caught */
    uint32_t flag;
    uint32_t maschera;
    uint32_t ingresso;
} SegAzione;

typedef struct {
    uint32_t    sig;
    siginfo_t  *info;
    ucontext_t *contesto;
    uint32_t    riservato;
    siginfo_t   si;
    ucontext_t  uc;
} SegTelaio;

static struct sigaction g_azioni[SIG_MAX];

void __exos_segnale_esegui(SegTelaio *t, void *fpu);

/* Riallinea la pila a 16 per il gestore (vedi filo_trampolino). */
__attribute__((force_align_arg_pointer))
void __exos_segnale_esegui(SegTelaio *t, void *fpu)
{
    struct sigaction *a;

    if (t->sig == 0 || t->sig >= SIG_MAX) return;
    a = &g_azioni[t->sig];
    t->uc.uc_mcontext.fpregs = fpu;

    if (a->sa_flags & SA_SIGINFO) {
        if (a->sa_sigaction) a->sa_sigaction((int)t->sig, t->info, t->contesto);
    } else if (a->sa_handler != SIG_DFL && a->sa_handler != SIG_IGN) {
        a->sa_handler((int)t->sig);
    }
    /* SA_RESETHAND: the kernel has already gone back to SIG_DFL; the copy
     * here follows, so sigaction(sig, NULL, &old) tells the truth. */
    if (a->sa_flags & SA_RESETHAND) {
        a->sa_handler = SIG_DFL;
        a->sa_flags   = 0;
    }
}

/* ESP -> SegTelaio (16-byte aligned by the kernel). 112 = 108 of FNSAVE
 * rounded to 16, so the call below happens with ESP aligned as the i386
 * ABI wants. */
__asm__(
".text\n"
".globl __exos_segnale_entra\n"
".type __exos_segnale_entra, @function\n"
"__exos_segnale_entra:\n"
"    movl  %esp, %esi\n"            /* esi = the frame (callee-saved) */
"    subl  $112, %esp\n"
"    fnsave (%esp)\n"
"    movl  %esp, %edi\n"            /* edi = the FPU image */
"    subl  $8, %esp\n"
"    pushl %edi\n"
"    pushl %esi\n"
"    call  __exos_segnale_esegui\n"
"    addl  $16, %esp\n"
"    frstor (%edi)\n"
"    movl  8(%esi), %ebx\n"         /* SegTelaio.contesto */
"    movl  $167, %eax\n"            /* SYS_SEG_RITORNO */
"    int   $0x80\n"
"    hlt\n"                         /* never: seg_ritorno does not come back */
".size __exos_segnale_entra, .-__exos_segnale_entra\n"
);
extern void __exos_segnale_entra(void);

int sigemptyset(sigset_t *s) { if (!s) { errno = EINVAL; return -1; } *s = 0; return 0; }
int sigfillset(sigset_t *s)  { if (!s) { errno = EINVAL; return -1; } *s = ~0u; return 0; }

int sigaddset(sigset_t *s, int sig)
{
    if (!s || sig <= 0 || sig >= SIG_MAX) { errno = EINVAL; return -1; }
    *s |= 1u << sig;
    return 0;
}

int sigdelset(sigset_t *s, int sig)
{
    if (!s || sig <= 0 || sig >= SIG_MAX) { errno = EINVAL; return -1; }
    *s &= ~(1u << sig);
    return 0;
}

int sigismember(const sigset_t *s, int sig)
{
    if (!s || sig <= 0 || sig >= SIG_MAX) { errno = EINVAL; return -1; }
    return (*s >> sig) & 1u;
}

int sigaction(int sig, const struct sigaction *nuova, struct sigaction *prima)
{
    SegAzione k;
    int32_t   r;

    if (sig <= 0 || sig >= SIG_MAX) { errno = EINVAL; return -1; }
    if (prima) *prima = g_azioni[sig];
    if (!nuova) return 0;

    k.tipo     = (nuova->sa_handler == SIG_IGN) ? 1
               : (nuova->sa_handler == SIG_DFL && !(nuova->sa_flags & SA_SIGINFO)) ? 0
               : 2;
    k.flag     = (uint32_t)nuova->sa_flags;
    k.maschera = nuova->sa_mask;
    k.ingresso = (uint32_t)(uintptr_t)__exos_segnale_entra;

    /* The table first: a signal that comes the instant the kernel is told
     * must find its handler already here. */
    {
        struct sigaction vecchia = g_azioni[sig];

        g_azioni[sig] = *nuova;
        r = _syscall3(SYS_SEG_AZIONE, (uint32_t)sig, (uint32_t)(uintptr_t)&k, 0);
        if (r < 0) {
            g_azioni[sig] = vecchia;
            errno = -r;
            return -1;
        }
    }
    return 0;
}

/* BSD semantics, as on glibc: the handler stays installed, and a signal is
 * blocked while its own handler runs. */
void (*signal(int sig, void (*gestore)(int)))(int)
{
    struct sigaction a, prima;

    memset(&a, 0, sizeof(a));
    a.sa_handler = gestore;
    a.sa_flags   = SA_RESTART;
    if (sigaction(sig, &a, &prima) < 0) return SIG_ERR;
    return (prima.sa_flags & SA_SIGINFO) ? (void (*)(int))prima.sa_sigaction
                                         : prima.sa_handler;
}

int sigprocmask(int come, const sigset_t *nuova, sigset_t *prima)
{
    int32_t r = _syscall3(SYS_SEG_MASCHERA, (uint32_t)come,
                          (uint32_t)(uintptr_t)nuova, (uint32_t)(uintptr_t)prima);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

int sigaltstack(const stack_t *nuova, stack_t *prima)
{
    int32_t r = _syscall2(SYS_SEG_PILA, (uint32_t)(uintptr_t)nuova,
                          (uint32_t)(uintptr_t)prima);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

/* pid > 0 only: EX-OS has no process groups, so 0 and the negative forms
 * have nobody to name. */
int kill(int pid, int sig)
{
    int32_t r;

    if (pid <= 0) { errno = (pid == 0 || pid == -1) ? EPERM : ESRCH; return -1; }
    r = _syscall2(SYS_SEG_MANDA, (uint32_t)pid, (uint32_t)sig);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

/* ! THE HANDLER HAS RUN WHEN raise() RETURNS, as POSIX requires: the signal
 * becomes pending on this thread and the kernel delivers it on the way out
 * of this very syscall. */
int raise(int sig)
{
    int32_t r;

    if (sig <= 0 || sig >= SIG_MAX) { errno = EINVAL; return -1; }

    /* The old libc said why it was dying; the kernel only logs it. */
    if (g_azioni[sig].sa_handler == SIG_DFL &&
        !(g_azioni[sig].sa_flags & SA_SIGINFO) &&
        (sig == SIGABRT || sig == SIGSEGV || sig == SIGILL || sig == SIGFPE ||
         sig == SIGBUS)) {
        sigset_t m = 0;
        sigprocmask(SIG_BLOCK, NULL, &m);
        if (!(m & (1u << sig))) fprintf(stderr, "%s\n", strsignal(sig));
    }

    r = _syscall2(SYS_SEG_MANDA, 0, (uint32_t)sig);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

/* sigsetjmp saves the mask if asked, then becomes setjmp (a tail jump: the
 * return address and env are still where setjmp looks for them). */
__asm__(
".text\n"
".globl sigsetjmp\n"
".type sigsetjmp, @function\n"
"sigsetjmp:\n"
"    movl  4(%esp), %eax\n"
"    movl  8(%esp), %ecx\n"
"    movl  %ecx, 24(%eax)\n"
"    testl %ecx, %ecx\n"
"    jz    1f\n"
"    pushl %ebx\n"
"    leal  28(%eax), %edx\n"        /* &env[7] */
"    xorl  %ecx, %ecx\n"
"    movl  $166, %eax\n"            /* SYS_SEG_MASCHERA: only read */
"    movl  %ecx, %ebx\n"
"    int   $0x80\n"
"    popl  %ebx\n"
"1:  jmp   setjmp\n"
".size sigsetjmp, .-sigsetjmp\n"
);

void siglongjmp(sigjmp_buf env, int val)
{
    if (env[6]) sigprocmask(SIG_SETMASK, (const sigset_t *)&env[7], NULL);
    longjmp(env, val);
}


/* ! libc.a only, like the rest: 176 bytes were enough to fill the floppy
 * (29 September 2026, "Disk full" copying libc.so). */
int mprotect(void *addr, size_t lung, int prot)
{
    int32_t r = _syscall3(SYS_MPROTECT, (uint32_t)(uintptr_t)addr,
                          (uint32_t)lung, (uint32_t)prot);
    mm_dice("mprotect", (uint32_t)(uintptr_t)addr, (uint32_t)lung, (uint32_t)prot, r);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}
#endif /* EXOS_LIBC_SO */

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
 * ! I CAMPI NON SPECIFICATI VALGONO 127 (CHAR_MAX) E NON ZERO. Non e' un
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
int meminfo(MemInfo *mi);          /* piu' avanti in questo file */

long sysconf(int nome)
{
    switch (nome) {
        case _SC_PAGESIZE:          return 4096;
        case _SC_OPEN_MAX:          return 128;   /* MAX_FD del kernel (0.232) */
        case _SC_CLK_TCK:           return 100;   /* il PIT gira a 100 Hz */
        case _SC_NPROCESSORS_ONLN:  return 1;
        case 83 /* _SC_NPROCESSORS_CONF */: return 1;   /* per NSPR, 30 settembre 2026 */
        case 75 /* _SC_THREAD_STACK_MIN */: return 16384;
        case _SC_GETPW_R_SIZE_MAX:  return 64 + 256;   /* getpwuid_r, vedi <pwd.h> */
        case _SC_PHYS_PAGES:
        case _SC_AVPHYS_PAGES: {                  /* per Gecko, 30 settembre 2026 */
            MemInfo mi;

            if (meminfo(&mi) < 0) { errno = EINVAL; return -1; }
            return (long)((nome == _SC_PHYS_PAGES ? mi.total_kb : mi.free_kb) / 4u);
        }
        case _SC_ARG_MAX:           return 16 * 320;
        default:                    errno = EINVAL; return -1;
    }
}

/* pathconf/fpathconf — gli stessi limiti di sysconf, ma per un file.
 *
 * ! NON DIPENDONO DAL PERCORSO, e su un sistema piu' grande dipenderebbero:
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
 * getrusage — ! RIPORTA UN LIMITE SUPERIORE, NON UNA MISURA
 *
 * EX-OS non tiene contabilita' per processo: lo scheduler assegna quanti e
 * non misura consumi. Quindi `ru_utime` riporta il tempo TRASCORSO
 * dall'avvio del sistema — che e' certamente >= al tempo di CPU di questo
 * processo, quindi non e' un numero inventato — e tutto il resto vale zero.
 *
 * ! CHI CI COSTRUISCE SOPRA UN PROFILO OTTERRA' NUMERI PRIVI DI
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
 * ! SOLO MEMORIA ANONIMA, e il rifiuto e' esplicito. EX-OS non sa mappare
 * un file: servirebbero le pagine sporche e il momento in cui riscriverle,
 * cioe' un pezzo di gestore della memoria che non c'e'. Una mmap che
 * fingesse di mappare un file consegnando zeri darebbe un programma che
 * legge dati sbagliati senza che niente lo segnali.
 *
 * ! SU FALLIMENTO RITORNA MAP_FAILED, cioe' (void *)-1, NON NULL: e' la
 * convenzione di POSIX ed e' il modo classico di sbagliare a usarla.
 * ============================================================================= */
#ifndef EXOS_LIBC_SO
/* Le mappature condivise e scrivibili di un file (vedi mmap): dove stanno, e
 * un descrittore loro (dup) per riscrivere i byte nel file. */
#define MAPPE_COND 32
static struct { unsigned char *a; size_t lung; int fd; long off; } g_mappe_cond[MAPPE_COND];
static volatile int g_mappe_cond_m = 0;

static int mappa_ricorda(void *m, size_t lung, int fd, long off)
{
    int i, d = dup(fd);

    if (d < 0) return -1;
    mutex_prendi(&g_mappe_cond_m);
    for (i = 0; i < MAPPE_COND; i++)
        if (!g_mappe_cond[i].a) {
            g_mappe_cond[i].a = (unsigned char *)m;
            g_mappe_cond[i].lung = lung;
            g_mappe_cond[i].fd = d;
            g_mappe_cond[i].off = off;
            mutex_lascia(&g_mappe_cond_m);
            return 0;
        }
    mutex_lascia(&g_mappe_cond_m);
    close(d);
    return -1;
}

/* Riscrive nel file la parte [a, a+lung) delle mappature condivise che la
 * toccano; con `togli` le dimentica (munmap). Rende 1 se ne ha trovata una. */
static int mappe_riscrivi(void *a, size_t lung, int togli)
{
    extern ssize_t pwrite(int fd, const void *buf, size_t n, long pos);
    unsigned char *da = (unsigned char *)a, *a_fine = da + lung;
    int i, trovata = 0;

    mutex_prendi(&g_mappe_cond_m);
    for (i = 0; i < MAPPE_COND; i++) {
        unsigned char *m = g_mappe_cond[i].a, *m_fine;
        unsigned char *x, *y;

        if (!m) continue;
        m_fine = m + g_mappe_cond[i].lung;
        if (a_fine <= m || da >= m_fine) continue;
        x = da > m ? da : m;
        y = a_fine < m_fine ? a_fine : m_fine;
        (void)pwrite(g_mappe_cond[i].fd, x, (size_t)(y - x),
                     g_mappe_cond[i].off + (long)(x - m));
        trovata = 1;
        if (togli) {
            close(g_mappe_cond[i].fd);
            g_mappe_cond[i].a = 0;
        }
    }
    mutex_lascia(&g_mappe_cond_m);
    return trovata;
}
#endif

void *mmap(void *addr, size_t lung, int prot, int flags, int fd, long off)
{
    MmapParams p;
    int32_t    r;

    if (lung == 0) { errno = EINVAL; return MAP_FAILED; }
    flags &= ~0x4000;                   /* MAP_NORESERVE: vedi libc.h */

#ifndef EXOS_LIBC_SO
    /* A descriptor from shm_open: the zone is already mapped, its address
     * is the answer. `addr`, `prot` and MAP_PRIVATE are not honoured: see
     * the shm_open section. */
    if (E_SHMFD(fd) && g_shm_mmap) {
        long v = g_shm_mmap(lung, fd, off);
        return (v < 0) ? MAP_FAILED : (void *)(uintptr_t)v;
    }
#endif

#ifndef EXOS_LIBC_SO
    /* =====================================================================
     * ! UN FILE SI MAPPA COPIANDOLO (1 ottobre 2026, tappa 6 di Exilla).
     * Il kernel mappa solo memoria anonima. Una mappatura PRIVATA, o in
     * sola lettura, di un file e' pero' indistinguibile da memoria anonima
     * con dentro i byte del file: chi la scrive cambia la propria copia, che
     * e' esattamente MAP_PRIVATE. Gecko legge cosi' omni.ja, e senza questo
     * rispondeva «The installation seems to be corrupt».
     *
     * ! RESTA FUORI MAP_SHARED IN SCRITTURA: li' le scritture dovrebbero
     * tornare nel file, e una copia le perderebbe in silenzio. ENODEV, come
     * prima. Il prezzo della copia e' la RAM e la lettura iniziale: un file
     * da 40 MB costa 40 MB subito. Solo in libc.a.
     * ===================================================================== */
    if (fd != -1 && !(flags & MAP_ANONYMOUS)) {
        extern ssize_t pread(int fd, void *buf, size_t n, long pos);
        extern int munmap(void *addr, size_t lung);
        unsigned char *m;
        size_t         letti = 0;

        /* ! MAP_SHARED IN SCRITTURA: una copia che torna nel file a msync e
         * munmap (3 ottobre 2026). Vale per un processo solo — due
         * processi che mappano lo stesso file non si vedrebbero — ed e' il
         * caso di SQLite in modalita' WAL (il file -shm), che Firefox usa
         * per i suoi database e che rispondeva «disk I/O error». */
        int condivisa = (flags & MAP_SHARED) && (prot & PROT_WRITE);
        m = mmap(addr, lung, PROT_READ | PROT_WRITE,
                 (flags & ~(MAP_SHARED | MAP_PRIVATE)) | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED) return MAP_FAILED;
        while (letti < lung) {
            ssize_t n = pread(fd, m + letti, lung - letti, off + (long)letti);
            if (n < 0) { int e = errno; munmap(m, lung); errno = e; return MAP_FAILED; }
            if (n == 0) break;          /* oltre la fine: il resto resta a zero */
            letti += (size_t)n;
        }
        if (prot != (PROT_READ | PROT_WRITE) && mprotect(m, lung, prot) != 0) {
            int e = errno; munmap(m, lung); errno = e; return MAP_FAILED;
        }
        if (condivisa && mappa_ricorda(m, lung, fd, off) != 0) {
            munmap(m, lung);
            errno = ENOMEM;
            return MAP_FAILED;
        }
        return m;
    }
#endif

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
    mm_dice("mmap", p.addr, p.length, p.prot | (p.flags << 8), r);
    /* Un indirizzo oltre i 2 GB e' negativo come int: errore e' -4095..-1. */
    if (r == 0 || (uint32_t)r >= 0xFFFFF001u) {
        errno = (r != 0) ? -r : ENOMEM;
        return MAP_FAILED;
    }
    return (void *)(uintptr_t)r;
}

int munmap(void *addr, size_t lung)
{
    int32_t r;

#ifndef EXOS_LIBC_SO
    /* ! A ZONE IS NOT GIVEN TO SYS_MUNMAP, which would hand its pages back
     * to the system while another process still uses them. */
    if (g_shm_munmap) {
        int k = g_shm_munmap(addr);
        if (k != 1) return k;
    }
    (void)mappe_riscrivi(addr, lung, 1);
#endif
    r = _syscall2(SYS_MUNMAP, (uint32_t)(uintptr_t)addr,
                          (uint32_t)lung);
    mm_dice("munmap", (uint32_t)(uintptr_t)addr, (uint32_t)lung, 0, r);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

/* ! int E NON void (30 settembre 2026): e' la firma di POSIX, e la libstdc++
 * con i fili la usa come valore (gthr-posix.h: `return sched_yield ();`).
 * Non fallisce mai. */
int sched_yield(void)
{
    _syscall1(SYS_SCHED_YIELD, 0);
    return 0;
}

/* ! Ritorna int e non void: e' quello che dice POSIX, e chi controlla il
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
 * ! RITORNA SEMPRE 0, ED E' LA RISPOSTA VERA
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
 * ! LA RISOLUZIONE VERA E' 10 ms, non un nanosecondo, e vale la pena
 * dirlo qui invece di lasciarlo scoprire a chi misura: sotto c'e'
 * SYS_SLEEP, che conta i tick del PIT a 100 Hz. Il nome della funzione
 * promette mille volte piu' di quello che la macchina puo' dare.
 *
 * ! SI ARROTONDA PER ECCESSO, e non e' un dettaglio: chi chiede di
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
 * ! ZERO NON VUOL DIRE «ROOT», VUOL DIRE «NON C'E' QUESTA DOMANDA». La
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
 * ! IL GIORNO CHE GLI UTENTI ARRIVASSERO, QUESTE VANNO RIFATTE PRIMA DI
 * TUTTO IL RESTO. Un sistema con i privilegi e una issetugid() che dice
 * sempre no e' un sistema che si fida delle variabili d'ambiente di
 * chiunque.
 * ============================================================================= */
/* ! DAL 17 AGOSTO 2026 RENDONO L'IDENTITA' VERA, non piu' zero fisso. Prima
 * era onesto — non c'erano utenti — e adesso sarebbe una bugia: un programma
 * che chiede chi e' e si sente rispondere «root» si comporterebbe da root.
 *
 * ! EFFETTIVO E REALE COINCIDONO, e coincideranno finche' non ci sara' il bit
 * setuid sui file: e' quel bit a creare la differenza fra «chi sono» e «con
 * quali diritti sto girando», e senza di lui distinguere due numeri sempre
 * uguali sarebbe fingere una cosa che non c'e'. */
int getuid(void)  { return (int)_syscall1(SYS_GETUID, 0); }
int geteuid(void) { return (int)_syscall1(SYS_GETUID, 0); }
int getgid(void)  { return (int)_syscall1(SYS_GETUID, 0); }
int getegid(void) { return (int)_syscall1(SYS_GETUID, 0); }

/* -----------------------------------------------------------------------------
 * setuid — scendere a un utente, e non si torna su
 *
 * ! LA PUO' CHIAMARE SOLO root, e serve a login: init resta root, avvia login
 * che resta root, login chiede le credenziali e scende con questa prima di
 * lanciare la shell. Da quel momento non puo' piu' tornare su — il privilegio
 * si spende, non si presta.
 *
 * Rende 0, oppure -1 con errno EPERM se chi chiama non e' root.
 * --------------------------------------------------------------------------- */
/* -----------------------------------------------------------------------------
 * chown — consegnare un file a un altro utente
 *
 * ! SOLO root, e non e' prudenza generica: se un utente potesse regalare i
 * propri file potrebbe anche PRENDERSI quelli che trova, e se potesse
 * regalarli via potrebbe riempire il disco e poi intestarlo a un altro.
 *
 * Su un filesystem senza proprietari — FAT, ISO 9660 — rende -1 con ENOSYS
 * invece di fingere di aver fatto qualcosa.
 * --------------------------------------------------------------------------- */
int chown(const char *percorso, unsigned int uid, unsigned int gid)
{
    int32_t r = _syscall3(SYS_CHOWN, (uint32_t)percorso, uid, gid);

    if (r < 0) { errno = -r; return -1; }
    return 0;
}

int setuid(unsigned int uid)
{
    int32_t r = _syscall2(SYS_SETUID, uid, uid);

    if (r < 0) { errno = -r; return -1; }
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    unsigned long long ms;

    if (req == NULL || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }

    ms  = (unsigned long long)req->tv_sec * 1000ULL;
    /* ! LA DIVISIONE E' A 32 BIT, e puo' esserlo perche' tv_nsec e' gia' stato
     * validato sotto il miliardo qui sopra: la somma con 999999 sta in 32 bit
     * con margine. A 64 bit servirebbe __udivdi3 di libgcc — che in un
     * programma statico --gc-sections butta via insieme a nanosleep, ma nella
     * libc CONDIVISA resta, e la libreria non collega libgcc. */
    ms += ((unsigned int)req->tv_nsec + 999999u) / 1000000u;

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

/* La misura viaggia con la chiamata: e' cosi' che il kernel si accorge se
 * questa copia di StatPerm e' andata fuori sincrono con la sua. */
int statperm(const char *path, StatPerm *p)
{
    return _syscall3(SYS_STATPERM, (uint32_t)path, (uint32_t)p,
                     (uint32_t)sizeof(StatPerm));
}

/* Il perche' di questa funzione — e perche' sta qui e non dentro /bin/id —
 * e' scritto accanto alla sua dichiarazione in libc.h. */
/* Il perche' della verifica nel kernel sta accanto alla dichiarazione. */
int diventa_root(const char *nome, const char *password)
{
    return _syscall2(SYS_SU, (uint32_t)nome, (uint32_t)password);
}

int nome_utente(unsigned int uid, char *out, unsigned int max)
{
    static char testo[4096];
    int fd, n, i = 0;

    if (!out || max < 2) return 0;

    fd = open("/boot/utenti", O_RDONLY);
    if (fd < 0) return 0;
    n = (int)read(fd, testo, sizeof(testo) - 1);
    close(fd);
    if (n < 0) n = 0;
    testo[n] = '\0';

    while (i < n) {
        int p = i, primo = -1, secondo = -1;

        while (i < n && testo[i] != '\n') {
            if (testo[i] == ':') {
                if (primo < 0)        primo = i;
                else if (secondo < 0) secondo = i;
            }
            i++;
        }
        if (i < n) i++;
        if (primo < 0 || secondo < 0) continue;

        if ((unsigned int)atoi(testo + primo + 1) == uid) {
            unsigned int l = (unsigned int)(primo - p);

            if (l >= max) l = max - 1;
            memcpy(out, testo + p, l);
            out[l] = '\0';
            return 1;
        }
    }
    return 0;
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
 * ! NON SCRIVE NIENTE: ne' l'MBR ne' il settore di avvio. Calcola la mappa
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
    return (int)err_posix(_syscall2(SYS_TRUNCATE, (uint32_t)path, size));
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

/* =============================================================================
 * LO SCAFFALE — CHI POSSIEDE LA CASSETTA POSTALE MENTRE DUE ATTESE SI INCROCIANO
 *
 * ! LA MAILBOX E' UNA SOLA E I CONSUMATORI SONO PIU' D'UNO, ed e' da qui che
 * nasce tutto. Un'applicazione grafica riceve nella stessa coda gli eventi del
 * server a finestre E le risposte dello stack IP. Chi aspetta le une scorre i
 * messaggi finche' non trova il suo, e cio' che trova per strada e' di
 * qualcun altro.
 *
 * ! NON SI PUO' «NON LEGGERE» UN MESSAGGIO: ipc_recv toglie dalla coda del
 * kernel e non c'e' modo di sbirciare. L'unica difesa e' tenerlo da questa
 * parte, e questo scaffale e' quel posto.
 *
 * ! E STA NELLA libc, NON IN CHI FILTRA. Il problema non e' dell'HTTP ne' del
 * DNS: e' di chiunque condivida la mailbox con qualcun altro. Uno scaffale per
 * ognuno vorrebbe dire scaffali che non si vedono fra loro, cioe' un messaggio
 * messo da parte da uno e mai visto dall'altro. C'e' una sola libc.so per
 * processo — i ponti di ogni programma e di ogni .so saltano tutti li' — quindi
 * questo scaffale e' uno solo davvero.
 *
 * =============================================================================
 * ! LA REGOLA, IN UNA RIGA: CHI LEGGE NON TIENE IN MANO CIO' CHE NON E' SUO.
 *
 * Fino al 3 settembre 2026 ognuno faceva cosi': scorreva la cassetta, si METTEVA
 * DA PARTE in un vettore suo cio' che non era suo, e alla fine lo RIMETTEVA
 * qui. Metteva da parte quattro messaggi perche' quattro erano i posti dello
 * scaffale — e se lo scaffale nel frattempo non era vuoto, `ipc_rimetti`
 * rendeva -1 e quei messaggi sparivano in silenzio. Nessuno guardava quel -1.
 *
 * Adesso il giro non esiste piu': `ipc_scegli` scorre lo scaffale e la cassetta
 * insieme e cio' che non e' del chiamante RESTA DOV'E' — non passa mai per le
 * mani di chi non lo vuole, quindi non lo puo' perdere. Il filtro dice una di
 * tre cose (IPC_MIO, IPC_ALTRUI, IPC_BUTTA) e questa e' l'intera decisione di
 * proprieta': cio' che e' ALTRUI aspetta il suo padrone, cio' che e' BUTTA non
 * ha piu' padrone.
 *
 * ! ED E' QUEL CHE TOGLIE LA TRAPPOLA DEL «RIMETTERE E RILEGGERE». Lo scaffale
 * si serve PRIMA della coda del kernel: chi rimetteva un messaggio e rileggeva
 * subito se lo ritrovava davanti all'infinito. Per questo il ciclo dei messaggi
 * di exwin rimetteva solo quando non dormiva, e per questo — messo sullo
 * scaffale un messaggio dello stack — la pompa della finestra si fermava li'
 * sopra a ogni giro. Scorrere senza togliere non ha quel problema: un ALTRUI si
 * SALTA, non si rimette.
 *
 * =============================================================================
 * ! SI CONTANO I BYTE, NON I MESSAGGI, e il numero quattro se ne va.
 *
 * Prima erano quattro posti da 1536 byte l'uno: un clic del mouse — venti byte
 * — ne occupava uno intero, e quattro clic riempivano lo scaffale. Adesso c'e'
 * un pozzo di byte e una fila di descrittori: gli stessi sei kilobyte tengono
 * quattro risposte piene di rete OPPURE ventiquattro eventi del mouse, che e'
 * il caso vero. La misura non e' cambiata; la capienza per quel che ci finisce
 * davvero e' cresciuta di sei volte.
 *
 * ! L'ORDINE SI RISPETTA: prima cio' che e' sullo scaffale, e fra quelli il
 * piu' vecchio. Un messaggio messo da parte e' arrivato PRIMA di quelli che
 * ancora devono arrivare, e consegnarlo dopo vorrebbe dire riordinare una
 * coda — che per un protocollo a domanda e risposta e' il modo di far
 * combaciare la risposta sbagliata con la domanda giusta. Saltare un ALTRUI
 * non rompe l'ordine di NESSUNO: fra i messaggi di uno stesso padrone l'ordine
 * resta quello d'arrivo, ed e' l'unico che conta.
 *
 * ! E SE LO SCAFFALE SI RIEMPIE DAVVERO si butta il piu' vecchio di quelli che
 * non sono del chiamante. E' l'unica perdita rimasta in tutto il meccanismo, ed
 * e' dichiarata: chi sta aspettando una risposta la aspetta per un motivo, e
 * fermarsi qui vorrebbe dire non leggere piu' NIENTE — la cassetta piena, il
 * mittente bloccato, e la pagina che non arriva. Un evento perso e' un clic da
 * rifare. Perche' ci si arrivi servono ventiquattro messaggi non reclamati, e
 * il ciclo dei messaggi ne reclama otto per giro.
 * ============================================================================= */
#define IPC_SCAFF_MSG   24      /* quanti messaggi al massimo */
#define IPC_SCAFF_BYTE  6144    /* e quanti byte in tutto: quattro pieni */

/* =============================================================================
 * ! OGNI MESSAGGIO SULLO SCAFFALE SA DI QUALE FILO E' (29 settembre 2026).
 *
 * La cassetta IPC e' del FILO — il kernel indirizza per pid, e un filo ha il
 * suo — ma lo scaffale e' della libc, cioe' di tutto il processo. Senza questo
 * campo un filo metteva da parte la risposta dello stack IP destinata a lui, e
 * un altro filo che aspettava una risposta dello stesso tipo se la prendeva:
 * due connessioni che si scambiano i dati. Con i socket BSD, dove ogni filo di
 * un navigatore parla con lo stack per conto suo, sarebbe stata la regola.
 *
 * ! IL LUCCHETTO PROTEGGE LO SCAFFALE, NON L'ATTESA: si prende per guardare e
 * per mettere, mai mentre si dorme sulla cassetta del kernel. Tenerlo durante
 * l'attesa vorrebbe dire un filo solo alla volta in ascolto.
 * ============================================================================= */
static struct {
    IpcMessage   meta;
    unsigned int off;           /* dove stanno i dati dentro g_scaff_dati */
    unsigned int len;
    unsigned int filo;          /* il pid del filo nella cui cassetta era arrivato */
} g_scaff[IPC_SCAFF_MSG];
static volatile int g_scaff_m = 0;        /* Mutex dello scaffale */

static unsigned char g_scaff_dati[IPC_SCAFF_BYTE];
static unsigned int  g_scaff_n     = 0;   /* messaggi in fila, dal piu' vecchio */
static unsigned int  g_scaff_usati = 0;   /* byte occupati nel pozzo */

/* ! IL POZZO E' COMPATTO E RESTA COMPATTO. Togliere dal mezzo sposta indietro
 * la coda dei dati e corregge gli scostamenti di chi viene dopo: sono al piu'
 * sei kilobyte di memmove su una macchina che nel frattempo aspetta la rete.
 * L'alternativa — un pozzo a buchi con una lista di liberi — costerebbe righe
 * e frammentazione per risparmiare microsecondi che nessuno misurerebbe. */
static void scaff_togli(unsigned int i)
{
    unsigned int off = g_scaff[i].off;
    unsigned int len = g_scaff[i].len;
    unsigned int j;

    if (i >= g_scaff_n) return;

    if (len && off + len < g_scaff_usati)
        memmove(g_scaff_dati + off, g_scaff_dati + off + len,
                g_scaff_usati - (off + len));
    g_scaff_usati -= len;

    for (j = i + 1; j < g_scaff_n; j++) {
        g_scaff[j].off -= len;
        g_scaff[j - 1]  = g_scaff[j];
    }
    g_scaff_n--;
}

static int scaff_metti(const IpcMessage *meta, const void *dati,
                       unsigned int len, unsigned int filo)
{
    if (!meta) return -1;
    if (len > IPC_MSG_MAX_DATA) return -1;
    if (g_scaff_n >= IPC_SCAFF_MSG) return -1;
    if (g_scaff_usati + len > IPC_SCAFF_BYTE) return -1;

    g_scaff[g_scaff_n].meta = *meta;
    g_scaff[g_scaff_n].off  = g_scaff_usati;
    g_scaff[g_scaff_n].len  = len;
    g_scaff[g_scaff_n].filo = filo;
    if (len && dati) memcpy(g_scaff_dati + g_scaff_usati, dati, len);
    g_scaff_usati += len;
    g_scaff_n++;
    return 0;
}

int ipc_rimetti(const IpcMessage *meta, const void *dati, unsigned int len)
{
    unsigned int filo = (unsigned int)filo_id();
    int          r;

    mutex_prendi(&g_scaff_m);
    r = scaff_metti(meta, dati, len, filo);
    mutex_lascia(&g_scaff_m);
    return r;
}

/* Quanti ce ne sono PER CHI CHIEDE: i messaggi degli altri fili non si
 * possono prendere da qui, e contarli farebbe credere che c'e' posta. */
unsigned int ipc_pronto(void)
{
    unsigned int filo = (unsigned int)filo_id(), i, n = 0;

    mutex_prendi(&g_scaff_m);
    for (i = 0; i < g_scaff_n; i++)
        if (g_scaff[i].filo == filo) n++;
    mutex_lascia(&g_scaff_m);
    return n;
}

/* Serve il messaggio i-esimo a chi lo ha chiesto e lo toglie. */
static int scaff_consegna(unsigned int i, IpcMessage *out_meta,
                          void *buf, unsigned int buf_len)
{
    unsigned int q = g_scaff[i].len;

    if (out_meta) *out_meta = g_scaff[i].meta;
    if (q > buf_len) q = buf_len;
    if (q && buf) memcpy(buf, g_scaff_dati + g_scaff[i].off, q);
    scaff_togli(i);
    return (int)q;
}

/* Serve il primo messaggio dello scaffale DI QUESTO FILO, se ce n'e' uno.
 * Rende i byte, o -1. */
static int scaffale_prendi(IpcMessage *out_meta, void *buf, unsigned int buf_len)
{
    unsigned int filo, i;
    int          r = -1;

    if (g_scaff_n == 0) return -1;          /* il caso di sempre: niente lucchetto */
    filo = (unsigned int)filo_id();
    mutex_prendi(&g_scaff_m);
    for (i = 0; i < g_scaff_n; i++)
        if (g_scaff[i].filo == filo) {
            r = scaff_consegna(i, out_meta, buf, buf_len);
            break;
        }
    mutex_lascia(&g_scaff_m);
    return r;
}

int ipc_recv(IpcMessage *out_meta, void *buf, unsigned int buf_len)
{
    int r = scaffale_prendi(out_meta, buf, buf_len);

    if (r >= 0) return r;
    return (int)_syscall3(SYS_IPC_RECV, (uint32_t)out_meta,
                           (uint32_t)buf, buf_len);
}

int ipc_recv_timeout(IpcMessage *out_meta, void *buf, unsigned int buf_len,
                     unsigned int timeout_ms)
{
    int r = scaffale_prendi(out_meta, buf, buf_len);

    /* ! CIO' CHE E' GIA' SULLO SCAFFALE NON FA ASPETTARE, nemmeno con una
     * scadenza lunga: e' gia' arrivato. */
    if (r >= 0) return r;
    return (int)_syscall4(SYS_IPC_RECV_TMO, (uint32_t)out_meta,
                           (uint32_t)buf, buf_len, timeout_ms);
}

/* =============================================================================
 * ipc_scegli — aspetta il PROPRIO messaggio senza toccare quelli degli altri
 *
 * ! IL BUFFER DI SERVIZIO E' UNO SOLO E STA QUI, non sulla pila. Il kernel
 * TRONCA a buf_len senza dirlo (vedi ipc_recv_timeout in kernel/ipc/ipc.c):
 * leggere dentro il buffer del chiamante vorrebbe dire mettere sullo scaffale
 * un messaggio di qualcun altro gia' tagliato, quando il chiamante ha chiesto
 * meno di 1536 byte. Sono 1536 byte di dato statico dentro la libreria
 * condivisa, non 1536 byte di pila in ogni chiamata.
 *
 * ! E NON E' RIENTRANTE, ne' ha motivo di esserlo: il filtro e' un predicato —
 * guarda mittente e tipo e risponde — e non chiama la posta.
 *
 * ! LA SCADENZA E' UNA SOLA PER TUTTA LA CHIAMATA, e prima non lo era: il
 * vecchio giro passava `ms` a OGNI lettura dentro un ciclo che poteva girare
 * sessantaquattro volte, cioe' `attendi(..., 2000)` poteva stare via due
 * minuti. Qui si scala il tempo gia' passato, e `ms == 0` continua a voler dire
 * «senza scadenza» come in tutta questa libc.
 *
 * ! E IPC_SUBITO VUOL DIRE «SOLO QUEL CHE C'E' GIA'». Non e' un doppione di una
 * scadenza corta: la scadenza piu' corta che il kernel sappia rappresentare e'
 * un tick del PIT, dieci millisecondi, e chi guarda la posta ripetutamente
 * mentre aspetta altro non puo' pagarli. Con IPC_SUBITO la cassetta si chiede a
 * poll() — che risponde senza dormire — e quando e' vuota si torna scaduti.
 * ============================================================================= */
/* ! IL BUFFER DI SERVIZIO STA SULLA PILA dal 29 settembre 2026: era statico, e
 * due fili dentro ipc_scegli insieme ci scrivevano tutti e due. Sono 1536 byte
 * per chiamata, su pile che sono di 256 KB (il principale) e 2 MB (i fili). */

/* ! C'E' POSTA NELLA CASSETTA DEL KERNEL? — chiesto senza aspettare. Serve solo
 * a IPC_SUBITO: leggere con una scadenza corta non sarebbe la stessa cosa,
 * perche' il PIT e' a 100 Hz e la scadenza piu' breve che esista e' un tick
 * intero. Otto domande da dieci millisecondi per giro sono ottanta millisecondi
 * di pompa che non pompa — e la pompa dei messaggi gira mentre si scarica una
 * pagina, cioe' proprio quando quel tempo conta. */
static int c_e_posta(void)
{
    struct pollfd p;

    p.fd = POLL_FD_IPC;
    p.events = POLL_IN;
    p.revents = 0;
    return (int)_syscall3(SYS_POLL, (uint32_t)&p, 1u, 0u) > 0;
}

int ipc_scegli(IpcFiltro filtro, void *dato, IpcMessage *out_meta,
               void *buf, unsigned int buf_len, unsigned int ms)
{
    unsigned int  i = 0;
    unsigned int  inizio = uptime_ms();
    unsigned int  filo;
    unsigned char posta[IPC_MSG_MAX_DATA];

    if (!filtro) return ipc_recv_timeout(out_meta, buf, buf_len, ms);

    /* Prima lo scaffale, dal piu' vecchio: cio' che e' gia' in casa non fa
     * aspettare nessuno. Un ALTRUI si salta e resta al suo posto — e quel che
     * e' arrivato a un altro filo non si guarda nemmeno. */
    filo = (unsigned int)filo_id();
    mutex_prendi(&g_scaff_m);
    while (i < g_scaff_n) {
        int d;

        if (g_scaff[i].filo != filo) { i++; continue; }
        d = filtro(&g_scaff[i].meta, dato);
        if (d == IPC_MIO) {
            int r = scaff_consegna(i, out_meta, buf, buf_len);
            mutex_lascia(&g_scaff_m);
            return r;
        }
        if (d == IPC_BUTTA) { scaff_togli(i); continue; }
        i++;
    }
    mutex_lascia(&g_scaff_m);

    /* Poi la cassetta del kernel. */
    for (;;) {
        IpcMessage   meta;
        unsigned int resta;
        int          r;

        if (ms == IPC_SUBITO) {
            /* Solo quel che c'e' gia': se la cassetta e' vuota si torna
             * subito, senza pagare nemmeno un tick. */
            if (!c_e_posta()) return -ETIMEDOUT;
            resta = 1;
        } else if (ms == 0) {
            resta = 0;                      /* senza scadenza */
        } else {
            unsigned int passati = uptime_ms() - inizio;

            if (passati >= ms) return -ETIMEDOUT;
            resta = ms - passati;
        }

        r = (int)_syscall4(SYS_IPC_RECV_TMO, (uint32_t)&meta,
                           (uint32_t)posta, IPC_MSG_MAX_DATA, resta);
        if (r < 0) return r;

        {
            unsigned int q = meta.len;
            int          d;

            if (q > IPC_MSG_MAX_DATA) q = IPC_MSG_MAX_DATA;
            d = filtro(&meta, dato);

            if (d == IPC_MIO) {
                unsigned int c = q;

                if (out_meta) *out_meta = meta;
                if (c > buf_len) c = buf_len;
                if (c && buf) memcpy(buf, posta, c);
                return (int)c;
            }

            if (d == IPC_BUTTA) continue;

            /* ALTRUI: aspetta il suo padrone sullo scaffale. E se non c'e'
             * piu' posto si fa spazio buttando il piu' vecchio che non e' del
             * chiamante — vedi il perche' in cima. */
            mutex_prendi(&g_scaff_m);
            while (scaff_metti(&meta, posta, q, filo) < 0) {
                unsigned int k;
                int          buttato = 0;

                /* ! IL PIU' VECCHIO CHE NON E' DEL CHIAMANTE, e se ne serve
                 * piu' d'uno se ne butta piu' d'uno: fermarsi al primo
                 * lascerebbe cadere in silenzio il messaggio che si sta
                 * cercando di mettere via, che e' esattamente cio' che questa
                 * funzione esiste per non fare. Uno dei posti si libera
                 * sempre: quel che e' del chiamante e' gia' stato consegnato
                 * dalla scorsa dello scaffale, qui sopra. */
                for (k = 0; k < g_scaff_n; k++)
                    if (g_scaff[k].filo == filo &&
                        filtro(&g_scaff[k].meta, dato) != IPC_MIO) {
                        scaff_togli(k);
                        buttato = 1;
                        break;
                    }
                /* Nessuno dei miei da buttare: il piu' vecchio in assoluto. */
                if (!buttato && g_scaff_n > 0) { scaff_togli(0); buttato = 1; }

                if (!buttato) break;
            }
            mutex_lascia(&g_scaff_m);
        }
    }
}

int time_now(RtcTime *t)
{
    return (int)_syscall1(SYS_TIME, (uint32_t)t);
}

/* Rimette l'orologio. Come time_now, rende 0 o un -errno: e' una chiamata
 * di EX-OS, non di POSIX — vedi la nota sui ritorni in cima a questo file. */
static int g_tod_pronto;    /* vedi tod_adesso(), piu' sotto */

int time_set(const RtcTime *t)
{
    int r = (int)_syscall1(SYS_TIME_SET, (uint32_t)t);

    /* Chi ha appena rimesso l'orologio lo rilegge alla prossima domanda,
     * senza aspettare il minuto: e' l'unico che SA che e' cambiato. */
    if (r == 0) g_tod_pronto = 0;
    return r;
}

/* =============================================================================
 * Data e ora nella forma del C standard
 *
 * time_now() da' i campi separati come li tiene l'orologio CMOS; time() e
 * localtime() danno la stessa informazione nella forma che si aspetta il
 * codice scritto per un sistema POSIX — un conteggio di secondi da
 * un'origine, e la struttura che lo rimette in pezzi.
 *
 * ! NON C'E' UN FUSO ORARIO. L'orologio CMOS di EX-OS e' ora locale, e il
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

/* I secondi dall'epoca letti dall'orologio CMOS, o -1 se non risponde. */
static long ora_cmos(void)
{
    RtcTime r;

    if (time_now(&r) < 0) return -1;
    return giorni_da_civile((long)r.anno, r.mese, r.giorno) * 86400L
           + (long)r.ora * 3600L + (long)r.minuto * 60L + (long)r.secondo;
}

/* =============================================================================
 * ! L'OROLOGIO CMOS SI LEGGE UNA VOLTA, POI SI CONTA DAL TIMER (7 ottobre 2026)
 *
 * time() e clock_gettime(CLOCK_REALTIME) leggevano il CMOS a ogni chiamata:
 * una trentina di accessi alle porte 0x70/0x71, piu' l'attesa che l'orologio
 * finisca di aggiornarsi. Firefox chiede l'ora 1,7 milioni di volte in un
 * quarto d'ora, e misurato dal kernel erano 209 secondi passati li' dentro.
 * In piu' i secondi venivano dal CMOS e i millisecondi da uptime_ms(), due
 * orologi con la fase diversa: dentro lo stesso secondo l'ora poteva tornare
 * indietro di quasi un secondo.
 *
 * Adesso il CMOS da' la base alla prima domanda, e da li' l'ora e' la base
 * piu' il tempo del timer: secondi e millisecondi dallo stesso orologio, lo
 * stesso per time(), gettimeofday() e clock_gettime(). Una volta al minuto il
 * CMOS si rilegge, e la base si sposta SOLO se la differenza e' di due secondi
 * o piu': e' il caso di chi ha cambiato la data con time_set(), non della
 * deriva di un tick. Senza quel caso l'ora non torna mai indietro (vedi piu'
 * sotto, a gettimeofday, che cosa succede a GCC quando lo fa).
 * ========================================================================== */
static long         g_tod_base_sec = 0;
static unsigned int g_tod_base_ms  = 0;
static unsigned int g_tod_visto_ms = 0;
static int          g_tod_pronto   = 0;

static int tod_adesso(long *sec, unsigned int *ms)
{
    unsigned int ora_ms = uptime_ms(), trascorsi;

    if (!g_tod_pronto) {
        long c = ora_cmos();
        if (c < 0) return -1;
        g_tod_base_sec = c;
        g_tod_base_ms  = ora_ms;
        g_tod_visto_ms = ora_ms;
        g_tod_pronto   = 1;
    } else if (ora_ms - g_tod_visto_ms >= 60000u) {
        long c, d;
        g_tod_visto_ms = ora_ms;
        c = ora_cmos();
        if (c >= 0) {
            d = c - (g_tod_base_sec + (long)((ora_ms - g_tod_base_ms) / 1000u));
            if (d >= 2 || d <= -2) {
                g_tod_base_sec = c;
                g_tod_base_ms  = ora_ms;
            }
        }
    }
    trascorsi = ora_ms - g_tod_base_ms;   /* corretto anche all'avvolgimento */
    *sec = g_tod_base_sec + (long)(trascorsi / 1000u);
    *ms  = trascorsi % 1000u;
    return 0;
}

time_t time(time_t *t)
{
    long         sec;
    unsigned int ms;

    /* L'orologio non risponde: (time_t)-1 e' il modo con cui il C standard
     * dice "non lo so". Ritornare 0 sarebbe il 1970 spacciato per buono. */
    if (tod_adesso(&sec, &ms) < 0) sec = -1;
    if (t) *t = (time_t)sec;
    return (time_t)sec;
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
 * gmtime_r / localtime_r — le stesse, ma nella memoria di chi chiama
 *
 * ! NON SONO UN VEZZO POSIX: sono cio' che permette a due chiamate di
 * convivere. `gmtime` tiene il risultato in una struttura statica, quindi
 *
 *     printf("%d %d", gmtime(&a)->tm_year, gmtime(&b)->tm_year);
 *
 * stampa due volte lo stesso anno e non se ne accorge nessuno. Il codice di
 * terzi le da' per scontate — QuickJS non compila senza `localtime_r`.
 *
 * ! E RIEMPIONO ANCHE tm_gmtoff E tm_zone, che la versione statica lascia a
 * zero: qui la struttura e' di chi chiama, ed e' l'unico posto dove si sa che
 * ha la forma di OGGI. Vedi il commento su struct tm in libc.h.
 * ============================================================================= */
struct tm *gmtime_r(const time_t *t, struct tm *out)
{
    struct tm *s;

    if (t == NULL || out == NULL) return NULL;

    s = gmtime(t);
    if (s == NULL) return NULL;

    *out = *s;
    out->tm_gmtoff = 0;         /* EX-OS non sa in che fuso si trova */
    out->tm_zone   = "UTC";
    return out;
}

struct tm *localtime_r(const time_t *t, struct tm *out)
{
    return gmtime_r(t, out);    /* nessun fuso orario: vedi il commento in testa */
}

/* =============================================================================
 * mktime — l'inversa di gmtime
 *
 * ! NORMALIZZA LA STRUTTURA CHE RICEVE, e non e' un effetto collaterale:
 * e' meta' del suo lavoro. Chi vuole «il primo del mese prossimo» scrive
 * `tm.tm_mon += 1` e chiama mktime, che accetta un mese 12 e lo trasforma
 * in gennaio dell'anno dopo. Da qui il fatto che il parametro NON e'
 * const: i campi tornano indietro corretti, tm_wday e tm_yday compresi —
 * quelli in ingresso vengono IGNORATI, come dice lo standard.
 *
 * ! NESSUN FUSO ORARIO: mktime interpreta i campi come UTC, perche' su
 * EX-OS localtime e gmtime sono la stessa funzione (il sistema non sa in
 * che fuso si trova). Su un sistema con i fusi questa e' la differenza fra
 * mktime e timegm, e qui non c'e'.
 * ============================================================================= */
time_t timegm(struct tm *tm);       /* sotto */

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
        /* =====================================================================
         * ! CAMPO PER CAMPO, E NON `*tm = *rifatto`, ED E' UN'ABI CHE SI
         * ROMPEVA IN SILENZIO.
         *
         * La struttura di chi chiama e' SUA: sta sulla sua pila, e ha la forma
         * che aveva l'header con cui e' stato compilato. Copiarci sopra una
         * `struct tm` intera vuol dire scriverci `sizeof(struct tm)` byte di
         * OGGI — e il giorno in cui la struttura e' cresciuta (tm_gmtoff e
         * tm_zone, agosto 2026) sarebbero stati otto byte oltre la variabile
         * di un programma compilato ieri. Sulla pila, cioe' sopra qualcos'altro
         * di suo.
         *
         * ! E I CAMPI NUOVI NON SI TOCCANO APPOSTA: mktime normalizza una data,
         * e il fuso non e' una cosa che normalizza. Chi li vuole riempiti
         * chiama gmtime_r, che scrive in una struttura di cui conosce la
         * forma perche' gliel'ha data lui.
         * ===================================================================== */
        if (rifatto != NULL && rifatto != tm) {
            tm->tm_sec   = rifatto->tm_sec;
            tm->tm_min   = rifatto->tm_min;
            tm->tm_hour  = rifatto->tm_hour;
            tm->tm_mday  = rifatto->tm_mday;
            tm->tm_mon   = rifatto->tm_mon;
            tm->tm_year  = rifatto->tm_year;
            tm->tm_wday  = rifatto->tm_wday;
            tm->tm_yday  = rifatto->tm_yday;
            tm->tm_isdst = rifatto->tm_isdst;
        }
        return quando;
    }
}

/* Vedi la dichiarazione in lib/include/libc.h. */
time_t timegm(struct tm *tm) { return mktime(tm); }

/* asctime e ctime — la data in venticinque caratteri e una riga a capo.
 *
 * "Sun Aug  2 17:04:05 2026\n", che e' la forma fissa dello standard: non
 * dipende dalla locale, non si puo' configurare, e la lunghezza e' sempre
 * la stessa. E' quella che stampa `ar tv` per la data di ogni membro.
 *
 * ! IL RISULTATO STA IN UN BUFFER STATICO, come vuole lo standard: la
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

/* ! utime NON CAMBIA NIENTE, come chmod e umask: nessun filesystem di
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
 * ! %Z e %z DICONO SEMPRE UTC E +0000, e non e' una scorciatoia: EX-OS
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

            /* Vedi il ! in testa: non c'e' nessun fuso da riportare. */
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
 * ! SECONDI E MICROSECONDI DALLA STESSA SORGENTE, e prima no
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
 * ! ORA IL TEMPO SCORRE TUTTO DA uptime_ms(), ancorato alla lettura
 * iniziale del CMOS: e' tod_adesso(), piu' sopra, che dal 7 ottobre 2026
 * serve anche time() e clock_gettime(). Se qualcuno corregge l'orologio di
 * sistema mentre un programma gira, lo si vede entro un minuto e solo se la
 * correzione e' di due secondi o piu': un orologio che non torna indietro
 * per la deriva di un tick vale piu' di uno che insegue l'ora a scatti.
 * ============================================================================= */

int gettimeofday(struct timeval *tv, void *fuso)
{
    long         sec;
    unsigned int ms;

    (void)fuso;         /* obsoleto anche su POSIX */
    if (tv == NULL) return -1;
    if (tod_adesso(&sec, &ms) < 0) return -1;

    tv->tv_sec  = sec;
    tv->tv_usec = (long)(ms * 1000u);
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

/* ! RITORNA `base` SE HA FUNZIONATO E 0 SE NO — non e' la convenzione
 * 0/-1 di tutto il resto, e' quella che lo standard da' a questa
 * funzione. Confonderle significa leggere "riuscito" quando l'orologio non
 * ha risposto.
 *
 * ! La risoluzione vera resta 10 ms, il tick del PIT: tv_nsec e' sempre
 * un multiplo di 10 000 000. La struttura ha i nanosecondi perche' cosi'
 * e' fatta, non perche' li sappiamo misurare. */
int timespec_get(struct timespec *ts, int base)
{
    long         sec;
    unsigned int ms;

    if (ts == NULL || base != TIME_UTC) return 0;
    if (tod_adesso(&sec, &ms) < 0) return 0;    /* l'orologio non risponde */

    ts->tv_sec  = (time_t)sec;
    ts->tv_nsec = (long)(ms * 1000000u);
    return base;
}

/* clock_gettime — vedi lib/include/libc.h. REALTIME e' timespec_get,
 * MONOTONIC conta da uptime_ms(); i due «CPUTIME» non si misurano, e rendono
 * il monotono: un tempo che scorre e' meglio di un errore per chi misura
 * quanto ci mette un pezzo di codice. */
int clock_gettime(int orologio, struct timespec *ts)
{
    unsigned int ms;

    if (ts == NULL) { errno = EINVAL; return -1; }
    switch (orologio) {
    case 0: case 5:                                     /* REALTIME */
        if (timespec_get(ts, TIME_UTC) != TIME_UTC) { errno = EINVAL; return -1; }
        return 0;
    case 1: case 2: case 3: case 4: case 6: case 7:     /* il resto: monotono */
        ms = uptime_ms();
        ts->tv_sec  = (time_t)(ms / 1000u);
        ts->tv_nsec = (long)((ms % 1000u) * 1000000u);
        return 0;
    default:
        errno = EINVAL;
        return -1;
    }
}

int clock_getres(int orologio, struct timespec *ts)
{
    if (orologio < 0 || orologio > 7) { errno = EINVAL; return -1; }
    if (ts) { ts->tv_sec = 0; ts->tv_nsec = 10000000; }   /* il tick: 10 ms */
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

int console_setfg(unsigned int pid)
{
    return (int)_syscall1(SYS_CONSOLE_SETFG, pid);
}

/* ! CHI TIENE LA CONSOLE DELLA GRAFICA. `azione`: 0 chiedi (rende il numero
 * della console, -1 se non c'e' nessuna grafica accesa), 1 prendi (quella del
 * chiamante), 2 lascia.
 *
 * Serve al driver di tastiera, che deve sapere se Alt+F<ultima> porta da
 * qualche parte: una console senza il server sopra e' uno schermo nero da cui
 * non si capisce come tornare indietro. E serve a `exwin`, che aspetta. */
int console_grafica(int azione)
{
    return (int)_syscall1(SYS_CONSOLE_GRAFICA, (unsigned int)azione);
}

/* The text of the graphics console (@EXWIN-LOG): see libc.h. */
int console_testo(char *buf, unsigned int max)
{
    return (int)_syscall2(SYS_CONSOLE_TESTO, (uint32_t)buf, (uint32_t)max);
}

/* The log ring of the graphics console (@EXWIN-LOG): see libc.h. */
int console_registro(char *buf, unsigned int max)
{
    return (int)_syscall2(SYS_CONSOLE_REGISTRO, (uint32_t)buf, (uint32_t)max);
}

/* Who Ctrl+C stops on text console n (@TASTI-SISTEMA): see libc.h. */
int console_ctrlc(unsigned int n)
{
    return (int)_syscall1(SYS_CONSOLE_CTRLC, n);
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
 * ipc_attendi — lo stesso servizio, ma dando il tempo di nascere
 *
 * ! ESISTE PER L'AVVIO DA kernel.cfg. Con i servizi lanciati a mano da uno
 * script l'ordine e' quello delle righe, e chi le scrive aspetta: `netdetect
 * -c` non torna finche' la scheda non ha registrato il proprio nome. I moduli
 * di [modules] invece PARTONO TUTTI INSIEME — il kernel li crea uno dopo
 * l'altro e li mette in coda — quindi lo stack IP puo' benissimo cercare la
 * scheda mentre la scheda sta ancora leggendo la PROM.
 *
 * Chi non trova il proprio fornitore ESCE, e un driver uscito all'avvio non lo
 * rilancia nessuno: la rete resterebbe spenta a ogni accensione, e la volta
 * dopo funzionerebbe — perche' i tempi cambiano. E' il difetto peggiore da
 * cercare. Aspettare qualche secondo lo toglie di mezzo per costruzione.
 * ============================================================================= */
int ipc_attendi(const char *name, unsigned int ms)
{
    unsigned int trascorso = 0;
    int          pid;

    for (;;) {
        pid = ipc_lookup(name);
        if (pid > 0) return pid;
        if (trascorso >= ms) return pid;    /* l'ultimo errore, non un -1 finto */
        usleep(100 * 1000);
        trascorso += 100;
    }
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

int irq_unbind(unsigned int irq)
{
    return (int)_syscall1(SYS_IRQ_UNBIND, irq);
}

int fdprova(FdPasso *passi, unsigned int max)
{
    return (int)_syscall3(SYS_FDPROVA, (unsigned int)passi, max, 0);
}

int kbprova(FdPasso *passi, unsigned int max)
{
    return (int)_syscall3(SYS_KBPROVA, (unsigned int)passi, max, 0);
}

int kbstato(FdPasso *passi, unsigned int max)
{
    return (int)_syscall3(SYS_KBPROVA, (unsigned int)passi, max, 1);
}

int dma_alloc(DmaZona *z)
{
    return (int)_syscall1(SYS_DMA_ALLOC, (unsigned int)z);
}

int mmio_map(MmioZona *m)
{
    return (int)_syscall1(SYS_MMIO_MAP, (unsigned int)m);
}

/* --- Un disco servito da questo processo: vedi libc.h ----------------------
 *
 * ! STRUTTURE DUPLICATE A MANO da kernel/include/syscall.h e da libc.h, come
 * MmioZona e VideoInfo: questo file non include libc.h. Tre copie identiche,
 * e se una diverge il kernel legge i campi nel posto sbagliato — qui vorrebbe
 * dire leggere un settore al posto di un altro. Per questo stanno in
 * tools/abi-bersaglio.c. */
typedef struct {
    char         nome[12];
    unsigned int settori_lo, settori_hi;
    unsigned int byte_settore;
    unsigned int sola_lettura;
} BlkOfferta;

typedef struct {
    unsigned int op;
    unsigned int lba_lo;
    unsigned int lba_hi;
    unsigned int settori;
    unsigned int quale;
    void        *dati;
    unsigned int dati_max;
} BlkRichiesta;

int blk_offri(BlkOfferta *o)
{
    return (int)_syscall1(SYS_BLK_OFFRI, (unsigned int)o);
}

int blk_attendi(BlkRichiesta *r, unsigned int ms)
{
    return (int)_syscall2(SYS_BLK_ATTENDI, (unsigned int)r, ms);
}

int blk_risposta(BlkRichiesta *r, int esito)
{
    return (int)_syscall2(SYS_BLK_RISPOSTA, (unsigned int)r,
                          (unsigned int)esito);
}

int blk_scansiona(const char *nome)
{
    return (int)_syscall1(SYS_BLK_SCANSIONA, (unsigned int)nome);
}

int blk_espelli(const char *nome)
{
    return (int)_syscall1(SYS_BLK_ESPELLI, (unsigned int)nome);
}

/* ! STRUTTURA DUPLICATA A MANO da kernel/include/syscall.h e da libc.h, come
 * MmioZona e DmaZona: questo file non include libc.h. Tre copie, tutte
 * identiche — e se una diverge, il kernel scrive i campi nel posto
 * sbagliato. Per questo VideoInfo sta in tools/abi-bersaglio.c. */
typedef struct {
    unsigned int fisico;
    unsigned int passo;
    unsigned int larghezza;
    unsigned int altezza;
    unsigned int bit;
} VideoInfo;

int video_info(VideoInfo *v)
{
    return (int)_syscall1(SYS_VIDEO_INFO, (unsigned int)v);
}

/* Una riga sul log del kernel, cioe' sulla seriale. Vedi libc.h per il
 * perche' non e' un doppione di printf. */
/* Spegne, riavvia o ferma. Rende solo se il kernel ha rifiutato. */
int reboot(int cosa)
{
    return (int)_syscall1(SYS_REBOOT, (unsigned int)cosa);
}

int log_seriale(const char *s)
{
    unsigned int n = 0;

    if (s == 0) return 0;
    while (s[n] && n < 200u) n++;
    return (int)_syscall2(SYS_LOG, (unsigned int)s, n);
}

/* ! -1 E errno, non -errno (29 settembre 2026): come stat, era rimasta alla
 * convenzione vecchia. */
static int poll_kernel(struct pollfd *fds, unsigned int nfds, int timeout)
{
    return (int)err_posix(_syscall3(SYS_POLL, (unsigned int)fds, nfds,
                                    (unsigned int)timeout));
}

#ifndef EXOS_LIBC_SO
static int poll_misto(struct pollfd *fds, unsigned int nfds, int timeout);
#endif

/* Con un socket fra i descrittori il giro lo fa poll_misto (sezione dei
 * socket); senza, e' la syscall di sempre. */
int poll(struct pollfd *fds, unsigned int nfds, int timeout)
{
    unsigned int i;

    for (i = 0; fds && i < nfds; i++)
        if (E_PRESA(fds[i].fd)) return poll_misto(fds, nfds, timeout);
    return poll_kernel(fds, nfds, timeout);
}

/* =============================================================================
 * select() — la forma vecchia, costruita SOPRA poll()
 *
 * ! NON E' UNA SECONDA IMPLEMENTAZIONE, ed e' il punto. Un'attesa su piu'
 * sorgenti scritta due volte vuol dire due volte la race del risveglio
 * perduto, e la seconda si scopre mesi dopo. Nel kernel c'e' solo poll();
 * questa traduce le maschere di bit e richiama quella.
 *
 * ! LE MASCHERE DI select() ARRIVANO A 32 DESCRITTORI e non oltre — fd_set e'
 * un unsigned int. Su EX-OS MAX_FD e' 32, quindi non ci si perde niente. La
 * mailbox IPC NON si puo' nominare qui: e' il numero 32, cioe' il primo che
 * una maschera a 32 bit non ha. Chi deve aspettare anche i messaggi usa
 * poll(), che e' anche il motivo per cui questa e' la forma vecchia.
 *
 * `nfds` e' il piu' alto descrittore piu' uno, come su POSIX. Le maschere
 * vengono RISCRITTE con cio' che e' pronto, e questo e' il difetto storico di
 * select(): dopo la chiamata non si sa piu' cosa si era chiesto.
 * ============================================================================= */
int select(int nfds, fd_set *leggere, fd_set *scrivere, fd_set *eccezioni,
           struct timeval *scadenza)
{
    struct pollfd v[32];
    unsigned int  n = 0;
    int           i, rc, ms;
    int           mappa[32];

    if (nfds < 0 || nfds > 32) { errno = EINVAL; return -1; }

    for (i = 0; i < nfds; i++) {
        short ev = 0;
        if (leggere  && FD_ISSET(i, leggere))  ev |= POLL_IN;
        if (scrivere && FD_ISSET(i, scrivere)) ev |= POLL_OUT;
        if (ev == 0) continue;

        v[n].fd      = i;
        v[n].events  = ev;
        v[n].revents = 0;
        mappa[n]     = i;
        n++;
    }

    /* NULL vuol dire «senza scadenza»; una timeval tutta a zero vuol dire
     * «guarda e torna subito», e sono due cose diverse. */
    if (scadenza == 0) ms = -1;
    else {
        long v_ms = scadenza->tv_sec * 1000L + scadenza->tv_usec / 1000L;
        if (v_ms < 0) v_ms = 0;
        ms = (int)v_ms;
        /* Sotto il millisecondo ma non zero: si aspetta un tick invece di
         * trasformare un'attesa brevissima in nessuna attesa. */
        if (ms == 0 && (scadenza->tv_sec != 0 || scadenza->tv_usec != 0)) ms = 1;
    }

    rc = poll(v, n, ms);
    if (rc < 0) return -1;                  /* errno l'ha gia' messo poll */

    if (leggere)   FD_ZERO(leggere);
    if (scrivere)  FD_ZERO(scrivere);
    if (eccezioni) FD_ZERO(eccezioni);

    {
        int pronti = 0;
        for (i = 0; i < (int)n; i++) {
            if (leggere  && (v[i].revents & POLL_IN))  { FD_SET(mappa[i], leggere);  pronti++; }
            if (scrivere && (v[i].revents & POLL_OUT)) { FD_SET(mappa[i], scrivere); pronti++; }
        }
        return pronti;
    }
}

int modo_testo(void)
{
    return (int)_syscall3(SYS_MODO_TESTO, 0, 0, 0);
}

int shm_apri(ShmZona *z)
{
    return (int)_syscall1(SYS_SHM_APRI, (unsigned int)z);
}

int shm_chiudi(void *p)
{
    return (int)_syscall1(SYS_SHM_CHIUDI, (unsigned int)p);
}

#ifndef EXOS_LIBC_SO
/* =============================================================================
 * shm_open, ftruncate, mmap: POSIX shared memory on EX-OS zones
 * (@SHM-OPEN, 29 September 2026, for Exilla)
 *
 * POSIX says: open a name, give it a size with ftruncate, mmap the
 * descriptor. The EX-OS zone (shm_apri) is born with its size and already
 * mapped, so the pieces are rearranged:
 *   - shm_open attaches to the zone if it exists; otherwise it only
 *     remembers the name, and the zone is CREATED by ftruncate, which is the
 *     first moment the size is known;
 *   - mmap on the descriptor returns the zone's address (plus the offset);
 *   - the zone is closed when the descriptor is closed AND every mapping is
 *     gone, in either order, as POSIX wants (a mapping outlives its fd).
 *
 * ! WHAT DOES NOT HOLD, said here and not discovered:
 *   - O_EXCL is 0 on EX-OS (<fcntl.h>): O_CREAT on an existing name attaches;
 *   - a zone cannot grow: ftruncate beyond its size is EINVAL;
 *   - MAP_PRIVATE on a zone is still shared, and PROT_* is not applied;
 *   - shm_unlink only answers 0: the zone goes away when its last user
 *     closes it, and until then the name is taken;
 *   - a name opened again by the same process, or dup()ed, is a new
 *     descriptor on the SAME zone (see ShmZonaP below).
 *   - shm_open(SHM_ANON, ...) is a zone private to the process: anonymous
 *     memory, no kernel zone (<sys/mman.h>).
 *
 * NAMES: the kernel takes 15 characters and Mozilla's are longer
 * ("/org.mozilla.ipc.1234.5"), so the name becomes "P" + the name without
 * the slash when it fits, else "P#" + an FNV-1a hash in hex. The "P" keeps
 * them away from the system's own zones (windows, clipboard).
 * ============================================================================= */
/* ! LA ZONA E IL DESCRITTORE SONO DUE COSE (2 ottobre 2026, tappa 6 di
 * Exilla). Prima ogni descrittore ERA una zona, e un processo poteva aprire
 * un nome una volta sola. Gecko apre lo stesso nome due volte (il secondo in
 * sola lettura: e' il descrittore "congelato" che poi passa agli altri), lo
 * duplica con dup() per darlo a chi lo mappa, e solo dopo chiude: cadeva in
 * WritableSharedMap con un MOZ_RELEASE_ASSERT. Adesso un nome gia' aperto
 * da questo processo da' un descrittore NUOVO sulla stessa zona, dup() fa
 * lo stesso, e la zona va via quando non la tiene piu' nessun descrittore e
 * nessuna mappatura. La sola lettura non si applica (vedi sopra: PROT_*). */
typedef struct {
    int          usato;
    int          privata;       /* da SHM_ANON: memoria anonima, niente kernel */
    int          descrittori;   /* descrittori aperti su questa zona */
    int          mappe;         /* mmap calls not yet undone */
    char         nome[16];      /* the kernel's name */
    unsigned int byte;          /* 0 until the zone exists */
    unsigned int virt;
} ShmZonaP;

static ShmZonaP g_shmzona[SHMFD_MAX];
#ifndef SHM_ANON
#define SHM_ANON  ((char *)1)       /* come in <sys/mman.h> */
#endif

static void shmfd_nome(const char *posix, char *k)
{
    unsigned int h = 2166136261u, n;
    const char  *c;

    while (*posix == '/') posix++;
    n = (unsigned int)strlen(posix);
    if (n > 0 && n <= 14) {
        k[0] = 'P';
        memcpy(k + 1, posix, n + 1);
        return;
    }
    for (c = posix; *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
    snprintf(k, 16, "P#%08x", h);
}

/* La zona di un descrittore, o NULL se non e' aperto. */
static ShmZonaP *shmfd_zona(int fd)
{
    int z;

    if (!E_SHMFD(fd)) return NULL;
    z = g_shmfd[fd - SHMFD_BASE];
    return z ? &g_shmzona[z - 1] : NULL;
}

/* The zone goes when nobody holds it any more: no descriptor and no
 * mapping left. */
static void shmfd_forse_libera(ShmZonaP *z)
{
    if (z->descrittori || z->mappe) return;
    if (z->virt && z->privata) {
        int32_t r = _syscall2(SYS_MUNMAP, z->virt, (z->byte + 4095u) & ~4095u);
        mm_dice("munmap-shm", z->virt, (z->byte + 4095u) & ~4095u, 0, r);
    }
    else if (z->virt)
        shm_chiudi((void *)(uintptr_t)z->virt);
    memset(z, 0, sizeof(*z));
}

/* Un descrittore nuovo sulla zona `zi`. */
static int shmfd_nuovo(int zi)
{
    int i;

    for (i = 0; i < SHMFD_MAX && g_shmfd[i]; i++) ;
    if (i == SHMFD_MAX) { errno = EMFILE; return -1; }
    g_shmfd[i] = zi + 1;
    g_shmzona[zi].descrittori++;
    return SHMFD_BASE + i;
}

static int  shmfd_chiudi(int fd);
static int  shmfd_fstat(int fd, struct stat *st);
static long shmfd_mmap(size_t lung, int fd, long off);
static int  shmfd_munmap(void *addr);
static int  shmfd_dup(int fd);

int shm_open(const char *nome, int flag, mode_t modo)
{
    ShmZona  q;
    ShmZonaP *z;
    int      i, r, fd;
    char     k[16];

    (void)modo;
    if (!nome) { errno = EINVAL; return -1; }
    if (nome != SHM_ANON && !nome[0]) { errno = EINVAL; return -1; }

    g_shm_chiudi = shmfd_chiudi;
    g_shm_fstat  = shmfd_fstat;
    g_shm_mmap   = shmfd_mmap;
    g_shm_munmap = shmfd_munmap;
    g_shm_dup    = shmfd_dup;

    /* SHM_ANON: una zona privata, senza nome; la memoria arriva con
     * ftruncate, come per le altre. */
    if (nome == SHM_ANON) {
        for (i = 0; i < SHMFD_MAX && g_shmzona[i].usato; i++) ;
        if (i == SHMFD_MAX) { errno = EMFILE; return -1; }
        memset(&g_shmzona[i], 0, sizeof(g_shmzona[i]));
        g_shmzona[i].usato   = 1;
        g_shmzona[i].privata = 1;
        fd = shmfd_nuovo(i);
        if (fd < 0) memset(&g_shmzona[i], 0, sizeof(g_shmzona[i]));
        return fd;
    }
    shmfd_nome(nome, k);

    /* Gia' aperta da questo processo: un altro descrittore sulla stessa. */
    for (i = 0; i < SHMFD_MAX; i++)
        if (g_shmzona[i].usato && strcmp(g_shmzona[i].nome, k) == 0)
            return shmfd_nuovo(i);

    for (i = 0; i < SHMFD_MAX && g_shmzona[i].usato; i++) ;
    if (i == SHMFD_MAX) { errno = EMFILE; return -1; }
    z = &g_shmzona[i];

    memset(&q, 0, sizeof(q));
    strcpy(q.nome, k);
    r = shm_apri(&q);                 /* attach, if it is there */
    if (r < 0 && (r != -ENOENT || !(flag & O_CREAT))) { errno = -r; return -1; }

    memset(z, 0, sizeof(*z));
    z->usato = 1;
    strcpy(z->nome, k);
    if (r == 0) { z->byte = q.byte; z->virt = q.virt; }
    fd = shmfd_nuovo(i);
    if (fd < 0) shmfd_forse_libera(z);
    return fd;
}

int shm_unlink(const char *nome)
{
    (void)nome;
    return 0;
}

int ftruncate(int fd, off_t lung)
{
    ShmZonaP *z = shmfd_zona(fd);
    ShmZona   q;
    int       r;

    if (z == NULL) {
        /* Un file: il kernel lo sa fare dal 0.233 (SYS_FTRUNCATE). */
        if (E_SHMFD(fd)) { errno = EBADF; return -1; }
        if (lung < 0)    { errno = EINVAL; return -1; }
        return (int)err_posix(_syscall2(SYS_FTRUNCATE, (uint32_t)fd, (uint32_t)lung));
    }
    if (lung < 0) { errno = EINVAL; return -1; }
    if (z->virt) {
        if ((unsigned long)lung > z->byte) { errno = EINVAL; return -1; }
        return 0;
    }
    if (lung == 0) return 0;

    if (z->privata) {
        void *m = mmap(NULL, (size_t)lung, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED) return -1;
        z->byte = (unsigned int)lung;
        z->virt = (unsigned int)(uintptr_t)m;
        return 0;
    }

    memset(&q, 0, sizeof(q));
    strcpy(q.nome, z->nome);
    q.byte = (unsigned int)lung;
    q.flag = SHM_CREA;
    r = shm_apri(&q);
    if (r < 0) { errno = -r; return -1; }
    z->byte = q.byte;
    z->virt = q.virt;
    return 0;
}

static int shmfd_chiudi(int fd)
{
    ShmZonaP *z = shmfd_zona(fd);

    if (z == NULL) { errno = EBADF; return -1; }
    g_shmfd[fd - SHMFD_BASE] = 0;
    z->descrittori--;
    shmfd_forse_libera(z);
    return 0;
}

static int shmfd_dup(int fd)
{
    ShmZonaP *z = shmfd_zona(fd);

    if (z == NULL) { errno = EBADF; return -1; }
    return shmfd_nuovo((int)(z - g_shmzona));
}

static int shmfd_fstat(int fd, struct stat *st)
{
    ShmZonaP *z = shmfd_zona(fd);

    if (z == NULL) { errno = EBADF; return -1; }
    memset(st, 0, sizeof(*st));
    st->st_mode    = S_IFREG | 0600u;
    st->st_nlink   = 1;
    st->st_size    = z->byte;
    st->st_blksize = 4096;
    return 0;
}

static long shmfd_mmap(size_t lung, int fd, long off)
{
    ShmZonaP *z = shmfd_zona(fd);

    if (z == NULL) { errno = EBADF; return -1; }
    if (!z->virt) { errno = ENXIO; return -1; }     /* no ftruncate yet */
    if (off < 0 || (off & 4095) ||
        (unsigned long)off + lung > ((z->byte + 4095u) & ~4095u)) {
        errno = ENXIO;
        return -1;
    }
    z->mappe++;
    return (long)(z->virt + (unsigned int)off);
}

/* 1: not a zone, go on with SYS_MUNMAP. 0: done. */
static int shmfd_munmap(void *addr)
{
    unsigned int a = (unsigned int)(uintptr_t)addr;
    int i;

    for (i = 0; i < SHMFD_MAX; i++) {
        ShmZonaP *z = &g_shmzona[i];

        if (!z->usato || !z->virt || !z->mappe) continue;
        if (a < z->virt || a >= z->virt + z->byte) continue;
        z->mappe--;
        shmfd_forse_libera(z);
        return 0;
    }
    return 1;
}

/* =============================================================================
 * dlopen, dlsym, dlclose, dlerror — on EX-OS libraries (@DLOPEN, 29 Sept 2026)
 *
 * An EX-OS shared library is not an ELF shared object with a dynamic symbol
 * table: it is linked at its own address and exports a table of names and
 * addresses, ExLibTesta (lib/include/exlib.h), that SYS_LIB_APRI maps and
 * hands back. dlopen is exlib_apri with a search path, dlsym is a look-up in
 * that table. It is what a ported program needs to open an OPTIONAL library
 * and carry on without it when it is not there — which is how Firefox uses
 * dlopen almost everywhere.
 *
 * ! NOT AN ELF LOADER. A library built by the cross compiler as an ordinary
 * .so (PIC, DT_NEEDED, relocations) is not loaded: dlopen says NULL and
 * dlerror says why. libxul is therefore linked statically into the program
 * (the choice written in tools/exilla/leggimi.md, stage 1); a real dynamic
 * loader is a job of its own.
 *
 * ! THE SAME CODE AS lib/exlib/exlib.c, with other names: system programs
 * link libc.c AND exlib.c, and the same names twice would not link. The
 * checks (magic, version) and the call to __lib_avvio are the same, and for
 * the same reasons, written there.
 * ============================================================================= */
#define SYS_LIB_APRI_DL   248
#define DL_MAGIA          0x424C5845u     /* EXLIB_MAGIA */
#define DL_VERSIONE       1u              /* EXLIB_VERSIONE */

typedef struct {
    unsigned int        magia;
    unsigned int        versione;
    unsigned int        n;
    const char *const  *nomi;
    void *const        *indirizzi;
} DlTesta;                               /* ExLibTesta, duplicated by hand */

static const char *g_dl_errore = 0;
static char        g_dl_testo[160];

static void dl_sbaglio(const char *cosa, const char *chi)
{
    snprintf(g_dl_testo, sizeof(g_dl_testo), "%s: %s", chi ? chi : "?", cosa);
    g_dl_errore = g_dl_testo;
}

static void *dl_cerca(const DlTesta *t, const char *nome)
{
    unsigned int i;

    for (i = 0; i < t->n; i++)
        if (t->nomi[i] && strcmp(t->nomi[i], nome) == 0) return t->indirizzi[i];
    return 0;
}

/* The table, or 0; *esiste says whether the file could be mapped at all. */
static const DlTesta *dl_apri_uno(const char *percorso, int *esiste)
{
    int            r = (int)_syscall1(SYS_LIB_APRI_DL, (uint32_t)(uintptr_t)percorso);
    const DlTesta *t;

    if (r <= 0) return 0;
    *esiste = 1;
    t = (const DlTesta *)(uintptr_t)r;
    if (t->magia != DL_MAGIA || t->versione != DL_VERSIONE ||
        t->n == 0 || t->nomi == 0 || t->indirizzi == 0)
        return 0;
    {
        void (*avvia)(void) = (void (*)(void))dl_cerca(t, "__lib_avvio");
        if (avvia) avvia();
    }
    return t;
}

/* ! THE HANDLE OF THE PROGRAM ITSELF (dlopen(NULL)) exists, because code
 * asks for it before looking anything up; but a static program has no symbol
 * table at run time, so dlsym on it finds nothing — said by dlerror. */
static const DlTesta g_dl_programma = { 0, 0, 0, 0, 0 };

void *dlopen(const char *nome, int flag)
{
    static const char *const dove[] = {
        "/lib/", "/exwin/lib/", "/cdrom/lib/", "/cdrom/exwin/lib/", 0
    };
    const DlTesta *t = 0;
    int            esiste = 0, i;
    char           percorso[256];

    (void)flag;                 /* RTLD_LAZY/NOW/GLOBAL: everything is bound */
    if (nome == 0) return (void *)&g_dl_programma;

    if (strchr(nome, '/')) {
        t = dl_apri_uno(nome, &esiste);
    } else {
        const char *extra = getenv("LD_LIBRARY_PATH");

        if (extra && *extra && strlen(extra) + strlen(nome) + 2 < sizeof(percorso)) {
            snprintf(percorso, sizeof(percorso), "%s/%s", extra, nome);
            t = dl_apri_uno(percorso, &esiste);
        }
        for (i = 0; !t && dove[i]; i++) {
            snprintf(percorso, sizeof(percorso), "%s%s", dove[i], nome);
            t = dl_apri_uno(percorso, &esiste);
        }
    }
    if (!t) {
        dl_sbaglio(esiste ? "not an EX-OS library (no ExLibTesta): only EX-OS "
                            "libraries can be opened, see dlopen in lib/libc.c"
                          : "cannot open shared object file (missing, or not an ELF the kernel can map)",
                   nome);
        return 0;
    }
    return (void *)t;
}

void *dlsym(void *h, const char *nome)
{
    const DlTesta *t = (const DlTesta *)h;
    void          *p;

    if (!nome) { dl_sbaglio("no symbol name", "dlsym"); return 0; }
    if (t == 0 || t == &g_dl_programma || t == (const DlTesta *)-1) {
        dl_sbaglio("undefined symbol (a static program has no symbol table "
                   "at run time)", nome);
        return 0;
    }
    p = dl_cerca(t, nome);
    if (!p) dl_sbaglio("undefined symbol", nome);
    return p;
}

/* Libraries stay mapped until the program ends: the kernel shares them
 * between processes and has no per-process unload. POSIX allows it. */
int dlclose(void *h)
{
    (void)h;
    return 0;
}

char *dlerror(void)
{
    const char *e = g_dl_errore;

    g_dl_errore = 0;
    return (char *)e;
}

int dladdr(const void *indirizzo, void *info) /* Dl_info *, see libc.h */
{
    (void)indirizzo; (void)info;
    return 0;                   /* "not found": no symbol tables at run time */
}
#endif /* !EXOS_LIBC_SO */

int interrompi(int pid)
{
    return (int)_syscall1(SYS_INTERROMPI, (unsigned int)pid);
}

int pty_apri(int fd[2])
{
    return (int)_syscall1(SYS_PTY_APRI, (unsigned int)fd);
}

int pty_ctl(int fd, unsigned int cmd, unsigned int arg)
{
    return (int)_syscall3(SYS_PTY_CTL, (unsigned int)fd, cmd, arg);
}

/* =============================================================================
 * Byte imprevedibili — vedi kernel/arch/x86/entropia.c per il perche'
 *
 * ! getentropy() E' IL NOME CHE CONTA, e non e' una scelta estetica:
 * OpenSSL lo cerca come simbolo DEBOLE prima di qualunque altra cosa
 * (providers/implementations/rands/seeding/rand_unix.c). Fornirlo con
 * questa firma esatta e' quello che permette di configurare OpenSSL per
 * EX-OS senza toccarne una riga.
 *
 * ! RITORNA 0 O -1, NON IL NUMERO DI BYTE. E' la firma di OpenBSD, che
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

#ifndef EXOS_LIBC_SO
/* =============================================================================
 * arc4random — il DRBG in spazio utente (@ARC4RANDOM, 1 ottobre 2026)
 *
 * Il contratto sta in lib/include/libc.h. Il kernel raccoglie il seme e lo
 * conta con prudenza: ogni prelievo da getentropy() scala la stima, e chi
 * chiede spesso (Gecko vuole byte casuali per ogni UUID, ogni hash, ogni
 * connessione) riceve EAGAIN. Qui il seme si prende UNA volta, 40 byte
 * (chiave e nonce di ChaCha20), e si espande.
 *
 * Il generatore e' quello di OpenBSD nella sostanza: ChaCha20 (RFC 7539),
 * e dopo ogni richiesta i primi 40 byte di un blocco nuovo diventano la
 * chiave successiva ("fast key erasure"), quindi chi legge la memoria dopo
 * non risale ai byte gia' consegnati. Ogni ARC4_RESEME byte prodotti si
 * mescolano nella chiave altri 40 byte del kernel; se in quel momento il
 * kernel dice EAGAIN si va avanti con la chiave che c'e' — e' gia' buona.
 *
 * ! IL PRIMO SEME SI ASPETTA, NON SI INVENTA: se getentropy() dice EAGAIN si
 * riprova ogni 10 ms (ogni richiesta fa girare al kernel la raccolta da
 * jitter), fino a un minuto; poi abort(). Proseguire senza seme vorrebbe
 * dire chiavi prevedibili, e arc4random non ha un modo per fallire.
 *
 * Solo in libc.a: la libc.so del floppy non ha posto.
 * ============================================================================= */

#define ARC4_RESEME     (1600u * 1024u)

static volatile int g_arc4_lucchetto = 0;
static uint32_t     g_arc4_stato[16];
static int          g_arc4_seminato = 0;
static uint32_t     g_arc4_prodotti = 0;

#define ARC4_ROT(v, n)   (((v) << (n)) | ((v) >> (32 - (n))))
#define ARC4_QR(a, b, c, d) do {                                   \
    a += b; d ^= a; d = ARC4_ROT(d, 16);                           \
    c += d; b ^= c; b = ARC4_ROT(b, 12);                           \
    a += b; d ^= a; d = ARC4_ROT(d, 8);                            \
    c += d; b ^= c; b = ARC4_ROT(b, 7);                            \
} while (0)

/* Un blocco di ChaCha20 (64 byte) dallo stato, poi il contatore avanza. */
static void arc4_blocco(uint32_t st[16], unsigned char out[64])
{
    uint32_t x[16];
    int      i;

    for (i = 0; i < 16; i++) x[i] = st[i];
    for (i = 0; i < 10; i++) {
        ARC4_QR(x[0], x[4], x[ 8], x[12]);
        ARC4_QR(x[1], x[5], x[ 9], x[13]);
        ARC4_QR(x[2], x[6], x[10], x[14]);
        ARC4_QR(x[3], x[7], x[11], x[15]);
        ARC4_QR(x[0], x[5], x[10], x[15]);
        ARC4_QR(x[1], x[6], x[11], x[12]);
        ARC4_QR(x[2], x[7], x[ 8], x[13]);
        ARC4_QR(x[3], x[4], x[ 9], x[14]);
    }
    for (i = 0; i < 16; i++) {
        uint32_t v = x[i] + st[i];
        out[4 * i]     = (unsigned char)v;
        out[4 * i + 1] = (unsigned char)(v >> 8);
        out[4 * i + 2] = (unsigned char)(v >> 16);
        out[4 * i + 3] = (unsigned char)(v >> 24);
    }
    st[12]++;
}

static uint32_t arc4_le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Chiave (32 byte) e nonce (12 byte) nello stato; il contatore riparte.
 * Esposta col nome riservato solo per la prova coi vettori di RFC 7539. */
void __exos_chacha20_imposta(uint32_t st[16], const unsigned char chiave[32],
                             uint32_t contatore, const unsigned char nonce[12])
{
    int i;

    st[0] = 0x61707865u; st[1] = 0x3320646eu;
    st[2] = 0x79622d32u; st[3] = 0x6b206574u;
    for (i = 0; i < 8; i++) st[4 + i] = arc4_le32(chiave + 4 * i);
    st[12] = contatore;
    for (i = 0; i < 3; i++) st[13 + i] = arc4_le32(nonce + 4 * i);
}

void __exos_chacha20_blocco(uint32_t st[16], unsigned char out[64])
{
    arc4_blocco(st, out);
}

/* Mescola 40 byte nella chiave e nel nonce correnti (XOR) e riparte. */
static void arc4_mescola(const unsigned char seme[40])
{
    unsigned char k[40];
    int           i;

    for (i = 0; i < 8; i++) {
        k[4 * i]     = (unsigned char)g_arc4_stato[4 + i];
        k[4 * i + 1] = (unsigned char)(g_arc4_stato[4 + i] >> 8);
        k[4 * i + 2] = (unsigned char)(g_arc4_stato[4 + i] >> 16);
        k[4 * i + 3] = (unsigned char)(g_arc4_stato[4 + i] >> 24);
    }
    for (i = 0; i < 2; i++) {
        k[32 + 4 * i]     = (unsigned char)g_arc4_stato[14 + i];
        k[32 + 4 * i + 1] = (unsigned char)(g_arc4_stato[14 + i] >> 8);
        k[32 + 4 * i + 2] = (unsigned char)(g_arc4_stato[14 + i] >> 16);
        k[32 + 4 * i + 3] = (unsigned char)(g_arc4_stato[14 + i] >> 24);
    }
    for (i = 0; i < 40; i++) k[i] ^= seme[i];
    {
        unsigned char nonce[12] = {0};
        for (i = 0; i < 8; i++) nonce[4 + i] = k[32 + i];
        __exos_chacha20_imposta(g_arc4_stato, k, 0, nonce);
    }
    memset(k, 0, sizeof k);
}

static void arc4_semina(void)
{
    unsigned char seme[40];
    int           tentativi;

    for (tentativi = 0; getentropy(seme, sizeof seme) != 0; tentativi++) {
        struct timespec pausa = { 0, 10 * 1000 * 1000 };
        if (tentativi >= 6000) {
            static const char msg[] =
                "arc4random: il sistema non da' entropia dopo un minuto\n";
            write(2, msg, sizeof msg - 1);
            abort();
        }
        nanosleep(&pausa, NULL);
    }
    arc4_mescola(seme);
    memset(seme, 0, sizeof seme);
    g_arc4_seminato = 1;
    g_arc4_prodotti = 0;
}

static void arc4_controlla_seme(size_t n)
{
    if (!g_arc4_seminato) {
        arc4_semina();
    } else if (g_arc4_prodotti > ARC4_RESEME) {
        unsigned char seme[40];
        if (getentropy(seme, sizeof seme) == 0) arc4_mescola(seme);
        memset(seme, 0, sizeof seme);
        g_arc4_prodotti = 0;
    }
    g_arc4_prodotti += (uint32_t)(n > 0xffffu ? 0xffffu : n);
}

/* Dopo ogni richiesta: un blocco nuovo diventa la chiave seguente. */
static void arc4_ricambia(void)
{
    unsigned char b[64];

    arc4_blocco(g_arc4_stato, b);
    arc4_mescola(b);
    memset(b, 0, sizeof b);
}

void arc4random_buf(void *buf, size_t n)
{
    unsigned char *p = (unsigned char *)buf;
    unsigned char  b[64];

    mutex_prendi(&g_arc4_lucchetto);
    arc4_controlla_seme(n);
    while (n > 0) {
        size_t m = n < 64 ? n : 64;
        arc4_blocco(g_arc4_stato, b);
        memcpy(p, b, m);
        p += m; n -= m;
        if (g_arc4_stato[12] == 0) arc4_ricambia();   /* contatore girato */
    }
    arc4_ricambia();
    mutex_lascia(&g_arc4_lucchetto);
    memset(b, 0, sizeof b);
}

uint32_t arc4random(void)
{
    uint32_t v;
    arc4random_buf(&v, sizeof v);
    return v;
}

/* Uniforme in [0, limite): si scartano i valori sotto 2^32 mod limite. */
uint32_t arc4random_uniform(uint32_t limite)
{
    uint32_t minimo, v;

    if (limite < 2) return 0;
    minimo = (uint32_t)(-limite) % limite;
    do { v = arc4random(); } while (v < minimo);
    return v % limite;
}
#endif /* !EXOS_LIBC_SO */

/* =============================================================================
 * Configurazione e identita' del sistema — vedi libc.h per il contratto
 * ============================================================================= */

/* ! IL SECONDO ARGOMENTO SI IGNORA, ed e' li' per la firma di POSIX.
 * EX-OS non ha permessi (vedi chmod piu' sopra), quindi non c'e' niente da
 * applicare. Fino ad agosto 2026 la firma era a UN argomento — piu'
 * onesta e incompatibile: `mkdir(nome, 0755)` non compilava, ed e' cio'
 * che scrive ogni programma portato da un Unix (bucomm.c di binutils fra
 * i primi). I due chiamanti interni sono stati aggiornati. */
int mkdir(const char *path, mode_t modo)
{
    (void)modo;
    return (int)err_posix(_syscall1(SYS_MKDIR, (uint32_t)path));
}

int rmdir(const char *path)
{
    return (int)err_posix(_syscall1(SYS_RMDIR, (uint32_t)path));
}

int unlink(const char *path)
{
    return (int)err_posix(_syscall1(SYS_UNLINK, (uint32_t)path));
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
    return err_posix(_syscall3(SYS_EXEC, (uint32_t)path, (uint32_t)argv, 0));
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
 * ! IL PUNTATORE RITORNATO VALE FINO ALLA CHIAMATA SUCCESSIVA. Su Unix
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
 * ! getenv() RIPIEGA su /boot/kernel.cfg per le chiavi che non trova.
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

/* =============================================================================
 * DOVE STANNO LE CINQUE VARIABILI GLOBALI — accessori per la libc condivisa
 *
 * ! UNA FUNZIONE NON PUO' AVVOLGERE UNA VARIABILE. Le 313 funzioni della libc
 * si raggiungono con un salto indiretto, che non ha bisogno di conoscerne la
 * firma; `errno`, `stdin`, `stdout`, `stderr` ed `environ` no: un programma
 * che scrive `errno = 0` scrive nella PROPRIA copia, e la libc leggerebbe la
 * sua — due variabili con lo stesso nome e nessun errore da nessuna parte.
 *
 * La soluzione e' quella di ogni libc vera: si esporta l'INDIRIZZO, e
 * l'header trasforma il nome in una lettura di quell'indirizzo (vedi le macro
 * in lib/include/libc.h). Il sorgente di chi le usa non cambia: `errno = 0` e
 * `if (errno == ENOENT)` continuano a scriversi cosi'.
 *
 * ! VALGONO ANCHE COLLEGANDO LA libc STATICAMENTE, e apposta: un solo
 * comportamento invece di due che divergono. Il costo e' una chiamata in piu'
 * per accesso, e a errno non ci si accede in un ciclo stretto.
 * ============================================================================= */
/* =============================================================================
 * errno PER FILO — trovato dal thread pointer (4 settembre 2026)
 *
 * ! DENTRO libc.so NON SI PUO' SCRIVERE `__thread`, e non e' una scelta: il
 * TLS dinamico non c'e' (lo dice kernel/include/sched.h). Le variabili
 * thread-local funzionano nei PROGRAMMI, che sono collegati staticamente e
 * usano il modello local-exec; una libreria condivisa vorrebbe
 * __tls_get_addr, che qui non esiste.
 *
 * ! MA IL THREAD POINTER SI PUO' LEGGERE, ed e' l'unica cosa che serve: ogni
 * filo ha il suo blocco TLS, e `%gs:0` contiene l'indirizzo del suo TCB — un
 * numero diverso per ogni filo, buono come chiave. La tabella qui sotto
 * associa quel numero a un errno, e la sua misura e' quella dei fili possibili
 * per processo con qualcosa di margine.
 *
 * ! IL POSTO SI PRENDE CON UN xchg, non con «se e' libero allora scrivilo»:
 * due fili che nascono insieme guarderebbero lo stesso posto vuoto e
 * scriverebbero tutti e due. Lo scambio atomico rende il valore di prima: chi
 * si ritrova in mano lo zero ha vinto, gli altri passano oltre.
 *
 * ! E IL BLOCCO C'E' SEMPRE, anche nei programmi senza variabili __thread: da
 * oggi elf_load ne fa uno del solo TCB a tutti, apposta per questa lettura.
 * Il ramo `tp == 0` resta per i casi che quel caricatore non ha preparato — un
 * task del kernel — dove un errno condiviso e' comunque meglio di un fault.
 * ============================================================================= */
#define ERRNO_POSTI  64          /* quanti FILO_MAX nel kernel (0.222) */

static struct { volatile int tp; int valore; } g_errno_posti[ERRNO_POSTI];

/* ! QUI DENTRO LA MACRO SI SPEGNE, o `&errno_condiviso` diventerebbe
 * `&(*__errno_dove())` — cioe' questa funzione che chiama se stessa. Si
 * riaccende subito dopo, perche' tutto il resto del file la vuole. */
#undef errno
int *__errno_dove(void)
{
    unsigned int tp;
    int          i;

    __asm__ __volatile__("movl %%gs:0, %0" : "=r"(tp));
    if (tp == 0) return &errno_condiviso;        /* nessun blocco: quello di scorta */

    for (i = 0; i < ERRNO_POSTI; i++)
        if ((unsigned int)g_errno_posti[i].tp == tp) return &g_errno_posti[i].valore;

    for (i = 0; i < ERRNO_POSTI; i++)
        if (g_errno_posti[i].tp == 0 &&
            mutex_xchg(&g_errno_posti[i].tp, (int)tp) == 0) {
            g_errno_posti[i].valore = 0;
            return &g_errno_posti[i].valore;
        }

    /* Tabella piena: si torna a quello di scorta invece di rifiutare. Un errno
     * condiviso e' impreciso; non averne uno e' un puntatore nullo. */
    return &errno_condiviso;
}

/* Un filo che finisce lascia libero il suo posto: senza, un programma che crea
 * e aspetta fili in un ciclo riempirebbe la tabella e da li' in poi tornerebbe
 * all'errno condiviso senza dirlo a nessuno. */
#define errno (*__errno_dove())

static void errno_posto_lascia(void)
{
    unsigned int tp;
    int          i;

    __asm__ __volatile__("movl %%gs:0, %0" : "=r"(tp));
    if (tp == 0) return;
    for (i = 0; i < ERRNO_POSTI; i++)
        if ((unsigned int)g_errno_posti[i].tp == tp) {
            g_errno_posti[i].valore = 0;
            g_errno_posti[i].tp     = 0;
            return;
        }
}

FILE  **__stdin_dove(void)   { return &stdin; }
FILE  **__stdout_dove(void)  { return &stdout; }
FILE  **__stderr_dove(void)  { return &stderr; }
char ***__environ_dove(void) { return &environ; }

/* =============================================================================
 * I DISTRUTTORI GLOBALI SONO DEL PROGRAMMA, NON DELLA LIBRERIA
 *
 * ! _libc_distruttori() percorre __fini_array, che e' un vettore del BINARIO
 * in cui si trova. Dentro la libc condivisa percorrerebbe quello della
 * LIBRERIA — vuoto — e i distruttori degli oggetti globali del programma non
 * girerebbero mai. Il programma registra il suo, e exit() chiama quello.
 *
 * Collegando staticamente non si registra niente e si chiama quello locale,
 * che e' lo stesso: il comportamento di sempre.
 * ============================================================================= */
void __libc_distruttori_registra(void (*f)(void))
{
    g_distruttori_prog = f;
}


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
    /* Oltre i 2 GB l'indirizzo e' negativo come int: errore e' -4095..-1. */
    if (r == 0 || (uint32_t)r >= 0xFFFFF001u) {
        errno = (r != 0) ? -r : 12 /* ENOMEM */;
        return (void *)-1;
    }
    return (void *)(uintptr_t)r;
}

long lseek(int fd, long offset, int whence)
{
    if (E_PRESA(fd)) { errno = 29; return -1; }   /* ESPIPE: un socket non ha posizione */
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

    /* ! st_dev RESTA 0, ED E' CORRETTO QUI: l'identita' del volume e'
     * gia' dentro st_ident (il VFS ci mette il montaggio nei bit alti —
     * vedi stat_interno in kernel/fs/vfs.c). Due st_ino uguali con
     * st_dev uguale significano «lo stesso file», ed e' esattamente cio'
     * che chiede chi confronta due percorsi. */
    st->st_dev   = 0;
    st->st_ino   = g->st_ident;
    st->st_nlink = 1;
    st->st_uid   = 0;
    st->st_gid   = 0;
    st->st_size  = (off_t)g->st_size;
    /* ! 512 e non 4096: e' il SETTORE, che e' l'unita' vera di tutti i
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

    /* ! -1 E errno, COME VUOLE POSIX (29 settembre 2026). Rendeva -errno —
     * -2 per un file che non c'e' — la convenzione vecchia che fstat aveva
     * gia' lasciato ad agosto. Chi controllava `< 0` non se ne accorgeva; la
     * std di Rust guarda `== -1`, e per lei ogni percorso esisteva:
     * fs::metadata di un file cancellato rispondeva Ok, create_dir_all non
     * creava niente. statraw imposta gia' errno. */
    if (st == NULL) { errno = EFAULT; return -1; }

    r = statraw(path, &g);
    if (r < 0) return -1;

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

    /* ! ERA `return -14`, cioe' -EFAULT: l'ultima funzione POSIX rimasta
     * con la convenzione vecchia, sfuggita alla conversione di agosto 2026
     * perche' non passa da err_posix(). Vedi il commento su errno in testa
     * al file per cosa e' costato altrove. `fsize` invece rendeva gia' -1,
     * quindi il ramo sotto era corretto per caso. */
    if (st == NULL) { errno = EFAULT; return -1; }

#ifndef EXOS_LIBC_SO
    if (E_SHMFD(fd) && g_shm_fstat) return g_shm_fstat(fd, st);
#endif
    if (E_PRESA(fd)) {                            /* un socket: tipo e basta */
        if (fcntl(fd, 1 /* F_GETFD */) < 0) return -1;
        memset(st, 0, sizeof(*st));
        st->st_mode    = 0140000u | 0666u;        /* S_IFSOCK */
        st->st_nlink   = 1;
        st->st_blksize = 512;
        return 0;
    }

    /* Un file: lo dice il kernel (SYS_FSTAT, dal 0.228), con la stessa
     * identita' e la stessa data che stat() da' per il suo percorso.
     *
     * ! PRIMA st_ino QUI VALEVA 0, «perche' un descrittore non porta con se'
     * il percorso». SQLite prende l'identita' con fstat all'apertura e prima
     * di ogni scrittura la confronta con stat sul percorso: 0 contro un
     * numero vero voleva dire «il file e' stato spostato», e rifiutava di
     * scriverci — il database delle chiavi di NSS restava senza password.
     * Vedi sys_fstat in kernel/syscall/syscall_impl.c. */
    {
        Stat g;

        if (_syscall2(SYS_FSTAT, (uint32_t)fd, (uint32_t)&g) == 0) {
            stat_da_grezzo(&g, st);
            return 0;
        }
    }

    dim = fsize(fd);
    if (dim < 0) return -1;

    /* Il resto (console, pipe, un kernel piu' vecchio del 0.228) e' quello
     * che si puo' dire di un descrittore senza SYS_FSTAT: la dimensione e'
     * vera, il tipo e' un'ipotesi ragionevole, identita' e tempi non ci
     * sono. */
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
 * ! Non salvano la maschera dei segnali perche' EX-OS non ha segnali, e
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

/* strtok with the state in the caller's hands (POSIX), for code that splits
 * two strings at once or runs in threads. Added on 27 September 2026 for the
 * BASIC interpreter (@RUNBAS): without it the compiler assumed an int, and
 * a pointer cut to an int is a crash waiting for the right address. */
char *strtok_r(char *s, const char *sep, char **stato)
{
    char *inizio;

    if (s == NULL) s = *stato;
    if (s == NULL) return NULL;

    s += strspn(s, sep);
    if (*s == '\0') { *stato = NULL; return NULL; }

    inizio = s;
    s += strcspn(s, sep);
    if (*s != '\0') { *s = '\0'; *stato = s + 1; }
    else            { *stato = NULL; }

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
 * ! IL TRONCAMENTO E' VERSO LO ZERO, non verso il basso: div(-7, 2) da'
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
 * ! QUI NON SI PUO' SCRIVERE `num / den`. Sull'i386 una divisione fra due
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
 * ! DIVISIONE PER ZERO: l'hardware solleverebbe #DE, qui non c'e' niente
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

    /* ! Il troncamento e' verso lo ZERO e il resto prende il segno del
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
 * codice indipendente dalla larghezza dei tipi. ! imaxdiv NON puo' usare
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
 * costanti di POSIX. ! NON E' CASUALE: si ripete, e da un seme noto da'
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
 * system e popen — un comando lo esegue la shell, non noi
 *
 * ! FINO AD AGOSTO 2026 system() RITORNAVA -1 CON ENOSYS, e la nota che
 * c'era qui diceva perche': /bin/sh aveva un `_start(void)`, non vedeva i
 * propri argomenti, e non c'era niente a cui passare la stringa. Adesso
 * la shell accetta `-c` (vedi bin/sh/start.S, scritto apposta), quindi si
 * puo' fare la cosa giusta.
 *
 * ! E LA COSA GIUSTA E' PASSARE DALLA SHELL, non spezzare il comando qui.
 * Una stringa di comando puo' contenere virgolette, redirezioni, pipe e
 * variabili: chi sa interpretarle e' la shell, e ne abbiamo una. Una
 * seconda mezza implementazione dentro la libc divergerebbe dalla prima
 * il giorno stesso, e la differenza si vedrebbe come un comando che
 * funziona dal prompt e non da un programma.
 *
 * Il codice di uscita e' quello del comando: la shell lo riporta con
 * `sh_exit(g_ultimo_stato)`, e 127 vuol dire «non trovato», come
 * dappertutto.
 * ============================================================================= */

#define SHELL_PERCORSO "/bin/sh"

int system(const char *comando)
{
    char *argv[4];
    int   pid, stato;

    /* system(NULL) chiede «esiste un interprete?». Adesso la risposta e'
     * si', e va data guardando se il file c'e' davvero: su un sistema
     * avviato da un supporto senza /bin/sh sarebbe no. */
    if (comando == NULL) return (access(SHELL_PERCORSO, X_OK) == 0);

    argv[0] = (char *)SHELL_PERCORSO;
    argv[1] = (char *)"-c";
    argv[2] = (char *)comando;
    argv[3] = NULL;

    pid = spawn(SHELL_PERCORSO, argv);
    /* spawn() ha gia' messo il codice in errno e reso -1: qui non c'e'
     * piu' niente da tradurre. */
    if (pid < 0) return -1;

    if (waitpid(pid, &stato, 0) < 0) return -1;
    return stato;
}

/* -----------------------------------------------------------------------------
 * popen / pclose
 *
 * ! UNA SOLA DIREZIONE PER VOLTA, e "r+" non esiste: servirebbero due
 * pipe e un processo che non si blocchi a riempirne una mentre l'altro
 * aspetta sull'altra. E' un problema vero (lo stallo classico delle
 * coprocessi), non una svista: chi ne ha bisogno usi pipe() e spawn_ex()
 * e decida lui l'ordine delle letture.
 *
 * ! IL PADRE CHIUDE SUBITO L'ESTREMITA' CHE HA PASSATO AL FIGLIO. Se non
 * lo facesse, la pipe conterebbe ancora uno scrittore vivo — lui — e la
 * lettura non vedrebbe mai la fine dei dati: aspetterebbe per sempre byte
 * che nessuno scrivera'. E' l'errore classico con le pipe, ed e' scritto
 * anche in testa a pipe() in lib/include/libc.h.
 * --------------------------------------------------------------------------- */

/* Il PID del figlio, indicizzato per descrittore: pclose() deve sapere chi
 * aspettare, e FILE non ha un posto dove tenerlo.
 *
 * ! E' UNA TABELLA PICCOLA E FISSA (MAX_FD voci come il kernel): un
 * processo che apra piu' pipe di cosi' ha gia' finito i descrittori. */
#define POPEN_MAX_FD 32
static int g_popen_pid[POPEN_MAX_FD];

FILE *popen(const char *comando, const char *modo)
{
    char       *argv[4];
    SpawnRedir  redir;
    int         p[2], pid, fd_mio, fd_suo;
    int         lettura;
    FILE       *f;

    if (comando == NULL || modo == NULL) { errno = EINVAL; return NULL; }

    if      (modo[0] == 'r') lettura = 1;
    else if (modo[0] == 'w') lettura = 0;
    else                     { errno = EINVAL; return NULL; }

    if (pipe(p) != 0) return NULL;

    if (lettura) {
        fd_mio = p[0];              /* noi leggiamo cio' che il figlio stampa */
        fd_suo = p[1];
        redir.fd = 1;               /* il suo stdout */
    } else {
        fd_mio = p[1];              /* noi scriviamo, lui legge */
        fd_suo = p[0];
        redir.fd = 0;               /* il suo stdin */
    }
    redir.flags    = 0;
    redir.percorso = NULL;          /* NULL = passa un descrittore gia' aperto */
    redir.fd_padre = fd_suo;

    argv[0] = (char *)SHELL_PERCORSO;
    argv[1] = (char *)"-c";
    argv[2] = (char *)comando;
    argv[3] = NULL;

    pid = spawn_ex(SHELL_PERCORSO, argv, NULL, &redir, 1);

    /* Subito, sia se e' andata sia se no: vedi il ! qui sopra. */
    close(fd_suo);

    if (pid < 0) { int e = errno; close(fd_mio); errno = e; return NULL; }

    f = fdopen(fd_mio, lettura ? "r" : "w");
    if (f == NULL) { close(fd_mio); return NULL; }

    if (fd_mio >= 0 && fd_mio < POPEN_MAX_FD) g_popen_pid[fd_mio] = pid;
    return f;
}

int pclose(FILE *f)
{
    int fd, pid, stato;

    if (f == NULL) { errno = EINVAL; return -1; }

    fd = fileno(f);
    pid = (fd >= 0 && fd < POPEN_MAX_FD) ? g_popen_pid[fd] : 0;
    if (fd >= 0 && fd < POPEN_MAX_FD) g_popen_pid[fd] = 0;

    /* ! PRIMA SI CHIUDE, POI SI ASPETTA. Al contrario, un figlio che
     * scrive piu' di quanto sta nella pipe resterebbe fermo sulla write
     * con noi fermi ad aspettarlo: uno stallo perfetto, e per giunta
     * intermittente, perche' si vede solo quando l'output supera il
     * buffer. */
    fclose(f);

    if (pid <= 0) { errno = ECHILD; return -1; }
    if (waitpid(pid, &stato, 0) < 0) return -1;
    return stato;
}

/* atof e' strtod senza il puntatore alla fine, e fabs e' abs in virgola
 * mobile. Nessuna delle due aggiunge niente a quello che c'e' gia': ci
 * sono perche' il codice di terzi le nomina — stabs.c di binutils la
 * prima, gprof la seconda. */
double atof(const char *s)  { return strtod(s, NULL); }

/* =============================================================================
 * ! QUATTRO FUNZIONI `weak`, E IL MOTIVO SI VEDE SOLO LINKANDO cc1
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
 * ! NON E' UNA SCELTA FRA DUE VERSIONI EQUIVALENTI. Quella di openlibm e'
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
 * ! SU ARGOMENTO NEGATIVO l'x87 solleva l'eccezione "operazione non
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
 * ! I DENORMALI PERDONO PRECISIONE. Un valore sotto ~2.2e-308 viene
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
 * ! IL RITORNO E' `char *`, NON `const char *`, ed e' voluto anche se
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
        case 7:   return "riga di comando troppo lunga";
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

/* strerror_r, la forma XSI di POSIX (quella che rende un int): il messaggio di
 * strerror copiato in `buf`. ERANGE se non ci sta — il pezzo che ci sta c'e'
 * comunque, terminato. La chiede la std di Rust (@RUST-STD, 29 settembre 2026)
 * per io::Error, e la usa il codice C di terzi che vuole essere sicuro coi
 * fili: qui strerror e' gia' rientrante (stringhe costanti), la copia no. */
int strerror_r(int err, char *buf, size_t n)
{
    const char *m = strerror(err);
    size_t      k = strlen(m);

    if (buf == NULL || n == 0) return ERANGE;
    if (k >= n) {
        memcpy(buf, m, n - 1);
        buf[n - 1] = '\0';
        return ERANGE;
    }
    memcpy(buf, m, k + 1);
    return 0;
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

/* =============================================================================
 * Caratteri larghi — <wchar.h> e <wctype.h>
 *
 * ! FINO AD AGOSTO 2026 QUI NON C'ERA NIENTE, E C'ERA UNA RAGIONE.
 * <wchar.h> diceva a chiare lettere «nessuna funzione»: EX-OS lavora a
 * byte, la console e' una VGA con una code page a 8 bit e l'unica locale
 * e' "C". Le ha fatte entrare la RUNTIME DI FREEBASIC, che di caratteri
 * larghi ha un tipo di dato intero (WSTRING) e non se ne puo' fare a
 * meno: senza queste, meta' di src/rtlib non compila.
 *
 * ! LA CODIFICA E' LATIN-1, cioe' un wchar_t E' un byte esteso a 32 bit.
 * Non e' UTF-32 e non fa finta di esserlo: sopra 255 la conversione verso
 * i byte FALLISCE invece di troncare (vedi wcstombs e wctomb qui sopra),
 * perche' un troncamento silenzioso e' un carattere sbagliato che sembra
 * giusto. Chi vuole Unicode vero deve prima decidere in che codifica
 * stanno i nomi di file gia' scritti sui volumi — quella e' la parte
 * difficile, non queste funzioni.
 *
 * Le funzioni isw* e tow* seguono la locale "C" e quindi non convertono niente sopra
 * il 127, esattamente come le loro sorelle strette in <ctype.h>.
 * ============================================================================= */

size_t wcslen(const wchar_t *s)
{
    const wchar_t *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

wchar_t *wcschr(const wchar_t *s, wchar_t c)
{
    for (; *s; s++) if (*s == c) return (wchar_t *)s;
    return (c == 0) ? (wchar_t *)s : NULL;
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c)
{
    const wchar_t *ultimo = NULL;
    for (;; s++) {
        if (*s == c) ultimo = s;
        if (*s == 0) break;
    }
    return (wchar_t *)ultimo;
}

int wcscmp(const wchar_t *a, const wchar_t *b)
{
    while (*a && *a == *b) { a++; b++; }
    /* Il confronto e' fra valori SENZA segno: wchar_t su i386 e' `int`,
     * e un carattere sopra 0x7FFFFFFF non esiste, ma la regola giusta e'
     * comunque quella dello standard. */
    return (*a < *b) ? -1 : (*a > *b) ? 1 : 0;
}

int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (*a < *b) ? -1 : (*a > *b) ? 1 : 0;
}

wchar_t *wcscpy(wchar_t *dst, const wchar_t *src)
{
    wchar_t *d = dst;
    while ((*d++ = *src++) != 0) { }
    return dst;
}

wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    wchar_t *d = dst;
    while (n && (*d = *src) != 0) { d++; src++; n--; }
    /* Riempimento con zeri fino a n: e' lo standard, ed e' anche la parte
     * che tutti dimenticano. */
    while (n--) *d++ = 0;
    return dst;
}

wchar_t *wcscat(wchar_t *dst, const wchar_t *src)
{
    wchar_t *d = dst;
    while (*d) d++;
    while ((*d++ = *src++) != 0) { }
    return dst;
}

wchar_t *wcsncat(wchar_t *dst, const wchar_t *src, size_t n)
{
    wchar_t *d = dst;
    while (*d) d++;
    while (n && *src) { *d++ = *src++; n--; }
    *d = 0;
    return dst;
}

wchar_t *wcsstr(const wchar_t *ago, const wchar_t *pagliaio)
{
    size_t n;

    if (*pagliaio == 0) return (wchar_t *)ago;
    n = wcslen(pagliaio);
    for (; *ago; ago++)
        if (wcsncmp(ago, pagliaio, n) == 0) return (wchar_t *)ago;
    return NULL;
}

size_t wcsspn(const wchar_t *s, const wchar_t *ammessi)
{
    const wchar_t *p = s;
    for (; *p; p++) if (wcschr(ammessi, *p) == NULL) break;
    return (size_t)(p - s);
}

size_t wcscspn(const wchar_t *s, const wchar_t *rifiutati)
{
    const wchar_t *p = s;
    for (; *p; p++) if (wcschr(rifiutati, *p) != NULL) break;
    return (size_t)(p - s);
}

wchar_t *wcspbrk(const wchar_t *s, const wchar_t *cercati)
{
    for (; *s; s++) if (wcschr(cercati, *s) != NULL) return (wchar_t *)s;
    return NULL;
}

wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n)
{
    for (; n--; s++) if (*s == c) return (wchar_t *)s;
    return NULL;
}

int wmemcmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    for (; n--; a++, b++)
        if (*a != *b) return (*a < *b) ? -1 : 1;
    return 0;
}

wchar_t *wmemcpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    wchar_t *d = dst;
    while (n--) *d++ = *src++;
    return dst;
}

/* ! Copia all'indietro quando le due zone si sovrappongono e la
 * destinazione sta dopo l'origine: e' l'unica differenza da wmemcpy, ed
 * e' tutta la ragione per cui questa funzione esiste. Il ciclo e' scritto
 * qui invece di appoggiarsi a memmove() perche' questo file e'
 * autosufficiente e memmove e' definita piu' sotto. */
wchar_t *wmemmove(wchar_t *dst, const wchar_t *src, size_t n)
{
    if (dst < src) {
        wchar_t *d = dst;
        while (n--) *d++ = *src++;
    } else if (dst > src) {
        wchar_t       *d = dst + n;
        const wchar_t *s = src + n;
        while (n--) *--d = *--s;
    }
    return dst;
}

wchar_t *wmemset(wchar_t *dst, wchar_t c, size_t n)
{
    wchar_t *d = dst;
    while (n--) *d++ = c;
    return dst;
}

/* --- Classificazione, locale "C" -------------------------------------------
 *
 * ! SOPRA IL 127 RISPONDONO SEMPRE NO (e tow* non convertono). Non e'
 * una semplificazione: nella locale "C" e' proprio cosi', ed e' l'unica
 * locale che EX-OS ha. Delegano alle <ctype.h> strette, cosi' la
 * definizione di «lettera» e' UNA SOLA in tutta la libc. */

int iswalnum(wint_t c)  { return (c < 128) ? isalnum((int)c)  : 0; }
int iswalpha(wint_t c)  { return (c < 128) ? isalpha((int)c)  : 0; }
int iswblank(wint_t c)  { return (c == L' ' || c == L'\t'); }
int iswcntrl(wint_t c)  { return (c < 128) ? iscntrl((int)c)  : 0; }
int iswdigit(wint_t c)  { return (c < 128) ? isdigit((int)c)  : 0; }
int iswgraph(wint_t c)  { return (c < 128) ? isgraph((int)c)  : 0; }
int iswlower(wint_t c)  { return (c < 128) ? islower((int)c)  : 0; }
int iswprint(wint_t c)  { return (c < 128) ? isprint((int)c)  : 0; }
int iswpunct(wint_t c)  { return (c < 128) ? ispunct((int)c)  : 0; }
int iswspace(wint_t c)  { return (c < 128) ? isspace((int)c)  : 0; }
int iswupper(wint_t c)  { return (c < 128) ? isupper((int)c)  : 0; }
int iswxdigit(wint_t c) { return (c < 128) ? isxdigit((int)c) : 0; }

wint_t towlower(wint_t c) { return (c < 128) ? (wint_t)tolower((int)c) : c; }
wint_t towupper(wint_t c) { return (c < 128) ? (wint_t)toupper((int)c) : c; }

/* --- Numeri ----------------------------------------------------------------
 *
 * ! PASSANO DALLA VERSIONE STRETTA, e non e' pigrizia: un numero e'
 * fatto di cifre, segni, punti e 'e' — tutti sotto il 128 — quindi
 * restringere la stringa e chiamare strtol() da' esattamente lo stesso
 * risultato, con UNA sola implementazione da tenere giusta invece di due.
 * Il carattere sopra 255 ferma la copia: li' il numero e' finito
 * comunque, e `fine` viene riportato al punto giusto dell'originale. */

#define WNUM_MAX 128

static size_t w_restringi(const wchar_t *s, char *buf, size_t dim)
{
    size_t i = 0;
    while (s[i] != 0 && i < dim - 1 && (unsigned long)s[i] <= 0xFF) {
        buf[i] = (char)(unsigned char)s[i];
        i++;
    }
    buf[i] = '\0';
    return i;
}

long wcstol(const wchar_t *s, wchar_t **fine, int base)
{
    char buf[WNUM_MAX], *f = NULL;
    long v;
    size_t n = w_restringi(s, buf, sizeof buf);
    v = strtol(buf, &f, base);
    if (fine) *fine = (wchar_t *)s + (f ? (size_t)(f - buf) : n);
    return v;
}

unsigned long wcstoul(const wchar_t *s, wchar_t **fine, int base)
{
    char buf[WNUM_MAX], *f = NULL;
    unsigned long v;
    size_t n = w_restringi(s, buf, sizeof buf);
    v = strtoul(buf, &f, base);
    if (fine) *fine = (wchar_t *)s + (f ? (size_t)(f - buf) : n);
    return v;
}

long long wcstoll(const wchar_t *s, wchar_t **fine, int base)
{
    char buf[WNUM_MAX], *f = NULL;
    long long v;
    size_t n = w_restringi(s, buf, sizeof buf);
    v = strtoll(buf, &f, base);
    if (fine) *fine = (wchar_t *)s + (f ? (size_t)(f - buf) : n);
    return v;
}

unsigned long long wcstoull(const wchar_t *s, wchar_t **fine, int base)
{
    char buf[WNUM_MAX], *f = NULL;
    unsigned long long v;
    size_t n = w_restringi(s, buf, sizeof buf);
    v = strtoull(buf, &f, base);
    if (fine) *fine = (wchar_t *)s + (f ? (size_t)(f - buf) : n);
    return v;
}

double wcstod(const wchar_t *s, wchar_t **fine)
{
    char buf[WNUM_MAX], *f = NULL;
    double v;
    size_t n = w_restringi(s, buf, sizeof buf);
    v = strtod(buf, &f);
    if (fine) *fine = (wchar_t *)s + (f ? (size_t)(f - buf) : n);
    return v;
}

/* --- Formattazione ---------------------------------------------------------
 *
 * ! %ls E %lc NON SONO SUPPORTATE, e la funzione lo DICE tornando -1 con
 * EILSEQ invece di stampare qualcosa di sbagliato.
 *
 * Il perche': qui si restringe il formato, si chiama vsnprintf() — cioe'
 * l'UNICA implementazione di printf che EX-OS ha, gia' collaudata — e si
 * riallarga il risultato. Funziona per ogni conversione i cui ARGOMENTI
 * sono stretti (%d, %u, %x, %g, %s...). Per %ls e %lc l'argomento e'
 * largo, e restringerlo vorrebbe dire camminare da soli sui varargs: si
 * riscriverebbe printf da capo per due conversioni che nessuno qui usa.
 * FreeBASIC, che e' chi ha portato queste funzioni dentro EX-OS, chiama
 * swprintf solo con %d, %u, %o, %lld, %llu, %llo e %g.
 * ============================================================================= */

int vswprintf(wchar_t *buf, size_t dim, const wchar_t *fmt, __builtin_va_list ap)
{
    char stretto[1024], fmt_stretto[256];
    int  n, i;

    for (i = 0; fmt[i] != 0; i++) {
        if ((size_t)i >= sizeof fmt_stretto - 1) { errno = EOVERFLOW; return -1; }
        if ((unsigned long)fmt[i] > 0xFF)        { errno = EILSEQ;    return -1; }
        /* Il rifiuto esplicito di %ls / %lc: vedi il commento sopra. */
        if (fmt[i] == L'%') {
            const wchar_t *p = &fmt[i] + 1;
            while (*p && wcschr(L"-+ #0123456789.*hljzt", *p) != NULL) {
                if (*p == L'l' && (p[1] == L's' || p[1] == L'c')) {
                    errno = EILSEQ;
                    return -1;
                }
                p++;
            }
        }
        fmt_stretto[i] = (char)(unsigned char)fmt[i];
    }
    fmt_stretto[i] = '\0';

    n = vsnprintf(stretto, sizeof stretto, fmt_stretto, ap);
    if (n < 0) return -1;

    /* ! swprintf NON e' snprintf: quando non ci sta, torna -1 (contenuto
     * del buffer indeterminato), NON la lunghezza che sarebbe servita.
     * Sono due contratti diversi, e chi li confonde scrive un ciclo di
     * riallocazione che non termina mai. */
    if (dim == 0) return -1;
    if ((size_t)n >= dim) { buf[0] = 0; return -1; }

    for (i = 0; i < n; i++) buf[i] = (wchar_t)(unsigned char)stretto[i];
    buf[n] = 0;
    return n;
}

int swprintf(wchar_t *buf, size_t dim, const wchar_t *fmt, ...)
{
    __builtin_va_list ap;
    int               n;

    /* __builtin_va_* e non <stdarg.h>: e' la convenzione di tutto questo
     * file, che non include header di libreria. Il tipo e' lo stesso che
     * <stdarg.h> chiama va_list, quindi il prototipo in wchar.h combacia. */
    __builtin_va_start(ap, fmt);
    n = vswprintf(buf, dim, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

/* =============================================================================
 * SHA-256 (FIPS 180-4)
 *
 * Sta qui e non in un file a parte perche' la libc di EX-OS e' UN SOLO
 * file: ogni programma la compila insieme al proprio sorgente, e una
 * traduzione in piu' vorrebbe dire toccare le trenta regole del Makefile
 * che la nominano.
 *
 * ! NON E' UNA FUNZIONE DI DERIVAZIONE DI CHIAVI. SHA-256 e' veloce per
 * costruzione, ed e' esattamente cio' che non si vuole da un'impronta di
 * password: chi si porta via /boot/utenti puo' provare milioni di
 * candidati al secondo. Il sale che /bin/login ci mette davanti impedisce
 * le tabelle precalcolate e rende diverse due password uguali, ma non
 * rallenta chi attacca un singolo conto. Una vera KDF (molte iterazioni,
 * o memoria dura) e' il passo successivo, e va fatto sapendo che si
 * cambia il formato del file.
 * ============================================================================= */

static uint32_t sha_ruotad(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

/* ! IL GIRO DI COMPRESSIONE, UNA VOLTA SOLA. Fino al 15 settembre 2026 stava
 * scritto DUE VOLTE dentro sha256(): una per i blocchi normali e una,
 * copiata riga per riga, per il blocco di coda che serve quando la lunghezza
 * non entra nell'ultimo. Sessanta righe identiche in due posti sono due posti
 * dove sbagliarle, e nessun modo di accorgersi se divergono. */
static void sha_blocco(uint32_t h[8], const uint8_t b[64])
{
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,
        0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
        0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
        0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
        0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
        0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,
        0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,
        0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
        0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t w[64], a, bb, c, d, e, f, g, hh, t1, t2;
    int      t;

    for (t = 0; t < 16; t++)
        w[t] = ((uint32_t)b[t*4] << 24) | ((uint32_t)b[t*4+1] << 16) |
               ((uint32_t)b[t*4+2] << 8) | ((uint32_t)b[t*4+3]);
    for (t = 16; t < 64; t++) {
        uint32_t s0 = sha_ruotad(w[t-15],7) ^ sha_ruotad(w[t-15],18) ^ (w[t-15] >> 3);
        uint32_t s1 = sha_ruotad(w[t-2],17) ^ sha_ruotad(w[t-2],19) ^ (w[t-2] >> 10);
        w[t] = w[t-16] + s0 + w[t-7] + s1;
    }

    a=h[0]; bb=h[1]; c=h[2]; d=h[3]; e=h[4]; f=h[5]; g=h[6]; hh=h[7];
    for (t = 0; t < 64; t++) {
        uint32_t S1 = sha_ruotad(e,6) ^ sha_ruotad(e,11) ^ sha_ruotad(e,25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t S0 = sha_ruotad(a,2) ^ sha_ruotad(a,13) ^ sha_ruotad(a,22);
        uint32_t mj = (a & bb) ^ (a & c) ^ (bb & c);

        t1 = hh + S1 + ch + K[t] + w[t];
        t2 = S0 + mj;
        hh=g; g=f; f=e; e=d+t1; d=c; c=bb; bb=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=bb; h[2]+=c; h[3]+=d;
    h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}

/* ! LO STATO DELL'IMPRONTA A PEZZI, RIPETUTO QUI. Questo file non include
 * lib/include/libc.h — e' autosufficiente per costruzione — quindi il tipo
 * che l'header dichiara va riscritto, campo per campo e nello stesso
 * ordine: e' la stessa convenzione gia' usata per DirEntry e MemInfo.
 * Se cambia uno dei due, cambia l'altro. */
typedef struct {
    uint32_t      h[8];       /* lo stato, otto parole                  */
    uint8_t       resto[64];  /* il blocco incompleto                   */
    uint32_t      n_resto;    /* quanti byte ci sono dentro             */
    uint64_t      bit;        /* quanti BIT in tutto: va nella coda     */
} Sha256;

void sha256_avvia(Sha256 *s)
{
    s->h[0]=0x6a09e667; s->h[1]=0xbb67ae85; s->h[2]=0x3c6ef372; s->h[3]=0xa54ff53a;
    s->h[4]=0x510e527f; s->h[5]=0x9b05688c; s->h[6]=0x1f83d9ab; s->h[7]=0x5be0cd19;
    s->n_resto = 0;
    s->bit     = 0;
}

void sha256_dai(Sha256 *s, const void *dati, size_t len)
{
    const uint8_t *p = (const uint8_t *)dati;
    size_t i;

    s->bit += (unsigned long long)len * 8;

    /* Prima si finisce il blocco lasciato a meta' dalla consegna precedente:
     * chi consegna a pezzi non ha nessun motivo di farlo a multipli di 64. */
    if (s->n_resto > 0) {
        while (len > 0 && s->n_resto < 64) { s->resto[s->n_resto++] = *p++; len--; }
        if (s->n_resto < 64) return;
        sha_blocco(s->h, s->resto);
        s->n_resto = 0;
    }

    while (len >= 64) {
        /* ! SI COPIA INVECE DI PASSARE p COM'E', e non e' pigrizia: quel che
         * arriva dalla rete non e' allineato a niente. La copia costa poco e
         * toglie di mezzo la domanda. */
        for (i = 0; i < 64; i++) s->resto[i] = p[i];
        sha_blocco(s->h, s->resto);
        p   += 64;
        len -= 64;
    }

    for (i = 0; i < len; i++) s->resto[i] = p[i];
    s->n_resto = (unsigned int)len;
}

void sha256_fine(Sha256 *s, uint8_t out[32])
{
    unsigned long long bit = s->bit;
    unsigned int       n   = s->n_resto;
    int i;

    /* L'imbottitura: un bit a uno, poi zeri, poi la lunghezza in bit su otto
     * byte. Se la lunghezza non ci sta in questo blocco, il blocco va com'e' e
     * la coda si prende un blocco tutto suo. */
    s->resto[n++] = 0x80;
    if (n > 56) {
        while (n < 64) s->resto[n++] = 0;
        sha_blocco(s->h, s->resto);
        n = 0;
    }
    while (n < 56) s->resto[n++] = 0;
    for (i = 0; i < 8; i++)
        s->resto[56 + i] = (uint8_t)(bit >> (56 - 8 * i));
    sha_blocco(s->h, s->resto);

    for (i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(s->h[i] >> 24);
        out[i*4+1] = (uint8_t)(s->h[i] >> 16);
        out[i*4+2] = (uint8_t)(s->h[i] >> 8);
        out[i*4+3] = (uint8_t)(s->h[i]);
    }
}

static const char sha_cifre[] = "0123456789abcdef";

void sha256_fine_esa(Sha256 *s, char out[65])
{
    uint8_t d[32];
    int i;

    sha256_fine(s, d);
    for (i = 0; i < 32; i++) {
        out[i*2]   = sha_cifre[d[i] >> 4];
        out[i*2+1] = sha_cifre[d[i] & 0x0F];
    }
    out[64] = '\0';
}

/* ! E LE DUE DI SEMPRE SONO QUATTRO RIGHE SOPRA QUELLE. Stessa firma, stesso
 * risultato: chi ha il messaggio intero in mano continua a chiamarle come
 * prima e non deve sapere che esista uno stato. */
void sha256(const void *dati, size_t len, uint8_t out[32])
{
    Sha256 s;

    sha256_avvia(&s);
    sha256_dai(&s, dati, len);
    sha256_fine(&s, out);
}

/* L'impronta in esadecimale minuscolo, 64 caratteri piu' il terminatore. */
void sha256_esa(const void *dati, size_t len, char out[65])
{
    Sha256 s;

    sha256_avvia(&s);
    sha256_dai(&s, dati, len);
    sha256_fine_esa(&s, out);
}

/* =============================================================================
 * ! L'AVVIO E' IN UN FILE A PARTE, E LO SI INCLUDE QUI. Il perche' — e perche'
 * si include un .c invece di collegarlo — sta scritto in cima a libc_avvio.c.
 * In breve: quelle funzioni toccano `main` e i vettori dei costruttori, che
 * appartengono al programma e non alla libreria; e la libc la compilano venti
 * regole del Makefile, quindi un oggetto in piu' sarebbe stato venti modifiche
 * e almeno una dimenticanza.
 * ============================================================================= */
/* =============================================================================
 * CIO' CHE LA SHELL DI SPIDERMONKEY CHIEDE ALLA LIBC (@EXILLA-JS, 30 settembre
 * 2026, tappa 4 di Exilla). Le dichiarazioni stanno in <string.h>, <time.h>,
 * <wchar.h>, <sys/mman.h>, <sys/resource.h>, <sys/syscall.h> e <libgen.h>.
 *
 * ! SOLO NELLA libc.a, come i socket, i segnali e dlopen: il floppy non ha
 * posto, e i programmi del sistema non le chiedono.
 * ============================================================================= */
#ifndef EXOS_LIBC_SO
struct rlimit { unsigned long rlim_cur, rlim_max; };

size_t strnlen(const char *s, size_t n)
{
    size_t i = 0;

    while (i < n && s[i]) i++;
    return i;
}

/* EX-OS non sa in che fuso si trova: UTC, come localtime(). */
long  timezone = 0;
int   daylight = 0;
char *tzname[2] = { "UTC", "UTC" };

void tzset(void) { }

/* ! UN CONSIGLIO, E SI PUO' NON SEGUIRE: le pagine restano come sono. Chi
 * chiede MADV_DONTNEED per restituire memoria (il GC) la ritrova com'era, che
 * POSIX permette; il costo e' che la RAM non torna al sistema finche' il
 * programma non la libera con munmap. */
int madvise(void *addr, size_t lung, int consiglio)
{
    (void)addr; (void)lung; (void)consiglio;
    return 0;
}

int msync(void *addr, size_t lung, int flag)
{
    (void)flag;
#ifndef EXOS_LIBC_SO
    (void)mappe_riscrivi(addr, lung, 0);
#else
    (void)addr; (void)lung;
#endif
    return 0;
}

/* ! EX-OS NON HA I LUCCHETTI SUI FILE: flock dice di si' e non esclude
 * nessuno. Chi conta su di lui per non aprire due volte la stessa cosa (il
 * profilo di Firefox) non e' protetto, e lo si dice qui e in <sys/file.h>. */
int flock(int fd, int come)
{
    (void)come;
    return fcntl(fd, 1 /* F_GETFD */) < 0 ? -1 : 0;
}

int posix_madvise(void *addr, size_t lung, int consiglio)
{
    (void)addr; (void)lung; (void)consiglio;
    return 0;
}

/* I limiti che ci sono per costruzione: vedi <sys/resource.h>. */
int getrlimit(int risorsa, struct rlimit *r)
{
    if (!r) { errno = EFAULT; return -1; }
    switch (risorsa) {
    case 3:  r->rlim_cur = r->rlim_max = 8ul * 1024 * 1024; return 0;   /* RLIMIT_STACK: USER_STACK_MAX (0.229) */
    case 7:  r->rlim_cur = r->rlim_max = 32ul;     return 0;   /* RLIMIT_NOFILE: MAX_FD */
    case 0: case 1: case 2: case 4: case 5: case 6: case 8: case 9:
        r->rlim_cur = r->rlim_max = ~0ul;                      /* RLIM_INFINITY */
        return 0;
    default: errno = EINVAL; return -1;
    }
}

int setrlimit(int risorsa, const struct rlimit *r)
{
    struct rlimit ora;

    if (getrlimit(risorsa, &ora) != 0) return -1;
    if (!r || r->rlim_cur > ora.rlim_max || r->rlim_max > ora.rlim_max) {
        errno = 1;                                   /* EPERM: non si allarga */
        return -1;
    }
    return 0;                                        /* stringere: si accetta e basta */
}

/* ! LA VARIANTE «LOCALE» DI __stack_chk_fail: GCC su i386 la chiama nel
 * codice non-PIC compilato con -fstack-protector (lo fa Firefox). Fa la stessa
 * cosa: il canarino e' rotto, il programma finisce. */
void __stack_chk_fail(void);
__attribute__((visibility("hidden"))) void __stack_chk_fail_local(void)
{
    __stack_chk_fail();
}

/* =============================================================================
 * ! LE ATOMICHE A 64 BIT (@EXILLA-JS, 30 settembre 2026). Senza SSE non c'e'
 * un'istruzione che legga otto byte in un colpo, e GCC chiama queste funzioni
 * di libatomic, che la toolchain non ha (--disable-libatomic). Sono tutte
 * sopra `lock cmpxchg8b`, che il Pentium ha: si confronta e si scambia finche'
 * nessuno si e' messo in mezzo. Il nome e' dato con asm: con il nome vero GCC
 * le prenderebbe per le sue funzioni interne e protesterebbe per la firma.
 * ============================================================================= */
typedef unsigned long long u64_at;

static u64_at cas8(volatile u64_at *p, u64_at atteso, u64_at nuovo)
{
    __asm__ __volatile__("lock; cmpxchg8b %1"
                         : "+A"(atteso), "+m"(*p)
                         : "b"((unsigned int)nuovo), "c"((unsigned int)(nuovo >> 32))
                         : "memory", "cc");
    return atteso;                          /* cio' che c'era */
}

u64_at at_load8(const volatile void *p, int m) __asm__("__atomic_load_8");
u64_at at_load8(const volatile void *p, int m)
{
    (void)m;
    return cas8((volatile u64_at *)p, 0, 0);
}

u64_at at_xchg8(volatile void *p, u64_at v, int m) __asm__("__atomic_exchange_8");
u64_at at_xchg8(volatile void *p, u64_at v, int m)
{
    u64_at vecchio = *(volatile u64_at *)p, visto;

    (void)m;
    while ((visto = cas8((volatile u64_at *)p, vecchio, v)) != vecchio) vecchio = visto;
    return vecchio;
}

void at_store8(volatile void *p, u64_at v, int m) __asm__("__atomic_store_8");
void at_store8(volatile void *p, u64_at v, int m)
{
    at_xchg8(p, v, m);
}

_Bool at_cmpxchg8(volatile void *p, void *atteso, u64_at v, _Bool debole, int ms, int mf)
    __asm__("__atomic_compare_exchange_8");
_Bool at_cmpxchg8(volatile void *p, void *atteso, u64_at v, _Bool debole, int ms, int mf)
{
    u64_at a = *(u64_at *)atteso;
    u64_at visto = cas8((volatile u64_at *)p, a, v);

    (void)debole; (void)ms; (void)mf;
    if (visto == a) return 1;
    *(u64_at *)atteso = visto;
    return 0;
}

#define AT_OP8(nome, simbolo, espr, rende)                              \
    u64_at nome(volatile void *p, u64_at v, int m) __asm__(simbolo);    \
    u64_at nome(volatile void *p, u64_at v, int m)                      \
    {                                                                   \
        u64_at vecchio = *(volatile u64_at *)p, nuovo, visto;           \
        (void)m;                                                        \
        for (;;) {                                                      \
            nuovo = (espr);                                             \
            visto = cas8((volatile u64_at *)p, vecchio, nuovo);         \
            if (visto == vecchio) return (rende);                       \
            vecchio = visto;                                            \
        }                                                               \
    }
AT_OP8(at_fadd8, "__atomic_fetch_add_8", vecchio + v, vecchio)
AT_OP8(at_fsub8, "__atomic_fetch_sub_8", vecchio - v, vecchio)
AT_OP8(at_fand8, "__atomic_fetch_and_8", vecchio & v, vecchio)
AT_OP8(at_for8,  "__atomic_fetch_or_8",  vecchio | v, vecchio)
AT_OP8(at_fxor8, "__atomic_fetch_xor_8", vecchio ^ v, vecchio)
AT_OP8(at_addf8, "__atomic_add_fetch_8", vecchio + v, nuovo)
AT_OP8(at_subf8, "__atomic_sub_fetch_8", vecchio - v, nuovo)

/* =============================================================================
 * POSIX a meta' (vedi <unistd.h>)
 * ============================================================================= */
int getppid(void)
{
    return (int)_syscall1(SYS_GETPPID, 0);
}

ssize_t pread(int fd, void *buf, size_t n, long pos)
{
    long    prima = lseek(fd, 0, 1);            /* SEEK_CUR */
    ssize_t r;

    if (prima < 0 || lseek(fd, pos, 0) < 0) return -1;
    r = read(fd, buf, n);
    lseek(fd, prima, 0);
    return r;
}

ssize_t pwrite(int fd, const void *buf, size_t n, long pos)
{
    long    prima = lseek(fd, 0, 1);
    ssize_t r;

    if (prima < 0 || lseek(fd, pos, 0) < 0) return -1;
    r = write(fd, buf, n);
    lseek(fd, prima, 0);
    return r;
}

/* I filesystem di EX-OS scrivono sul disco a ogni write: non c'e' niente in
 * sospeso da spingere. */
int fsync(int fd)
{
    return fcntl(fd, 1 /* F_GETFD */) < 0 ? -1 : 0;
}

ssize_t readlink(const char *percorso, char *buf, size_t n)
{
    struct stat st;

    (void)buf; (void)n;
    if (stat(percorso, &st) != 0) return -1;
    errno = EINVAL;                             /* esiste, e non e' un link */
    return -1;
}

int symlink(const char *b, const char *p)                  { (void)b; (void)p; errno = ENOSYS; return -1; }
int linkat(int a, const char *p1, int b, const char *p2, int f) { (void)a; (void)p1; (void)b; (void)p2; (void)f; errno = ENOSYS; return -1; }
int lchown(const char *p, unsigned int u, unsigned int g)   { return chown(p, u, g); }
int fchown(int fd, unsigned int u, unsigned int g)          { (void)fd; (void)u; (void)g; errno = ENOSYS; return -1; }
int chroot(const char *p)                                   { (void)p; errno = ENOSYS; return -1; }
int mkfifo(const char *p, unsigned int m)                   { (void)p; (void)m; errno = ENOSYS; return -1; }

/* =============================================================================
 * Per NSPR (@EXILLA-NSPR, 30 settembre 2026): execve, uname, i protocolli,
 * gethostbyaddr. Vedi libc.h e <sys/utsname.h>.
 * ============================================================================= */
int execve(const char *percorso, char *const argv[], char *const envp[])
{
    return err_posix(_syscall3(SYS_EXEC, (uint32_t)percorso, (uint32_t)argv,
                               (uint32_t)envp));
}

struct utsname { char sysname[65], nodename[65], release[65], version[65], machine[65]; };

int uname(struct utsname *u)
{
    const char *n = getenv("HOSTNAME"), *v = getenv("OSVER");

    if (!u) { errno = EFAULT; return -1; }
    memset(u, 0, sizeof(*u));
    strcpy(u->sysname, "EX-OS");
    strncpy(u->nodename, (n && *n) ? n : "exos", 64);
    strncpy(u->release, (v && *v) ? v : "0", 64);
    strcpy(u->version, "EX-OS");
    strcpy(u->machine, "i386");
    return 0;
}

int h_errno = 0;

const char *hstrerror(int e)
{
    switch (e) {
    case 1:  return "nome sconosciuto";
    case 2:  return "il DNS non risponde, riprovare";
    case 3:  return "errore del DNS";
    case 4:  return "il nome non ha un indirizzo";
    default: return "errore di risoluzione";
    }
}

struct protoent { char *p_name; char **p_aliases; int p_proto; };

static struct protoent *protocollo(const char *nome, int numero)
{
    static char           *niente[1] = { 0 };
    static struct protoent p;
    static const struct { const char *nome; int n; } noti[] = {
        { "ip", 0 }, { "icmp", 1 }, { "tcp", 6 }, { "udp", 17 },
    };
    unsigned int i;

    for (i = 0; i < sizeof(noti) / sizeof(noti[0]); i++)
        if ((nome && strcmp(nome, noti[i].nome) == 0) || (!nome && numero == noti[i].n)) {
            p.p_name = (char *)noti[i].nome;
            p.p_aliases = niente;
            p.p_proto = noti[i].n;
            return &p;
        }
    return NULL;
}

struct protoent *getprotobyname(const char *nome)  { return nome ? protocollo(nome, -1) : NULL; }
struct protoent *getprotobynumber(int numero)      { return protocollo(NULL, numero); }

/* =============================================================================
 * Per NSS e SQLite (@EXILLA-NSS): utimes, syslog, termios. Vedi gli header.
 * ============================================================================= */
int utimes(const char *percorso, const struct timeval tempi[2])
{
    struct stat st;

    (void)tempi;
    return stat(percorso, &st);
}

static char g_syslog_nome[32] = "";

void openlog(const char *nome, int opzioni, int servizio)
{
    (void)opzioni; (void)servizio;
    strncpy(g_syslog_nome, nome ? nome : "", sizeof(g_syslog_nome) - 1);
}

void vsyslog(int priorita, const char *fmt, __builtin_va_list ap)
{
    char riga[200];
    int  n = 0;

    (void)priorita;
    if (g_syslog_nome[0]) n = snprintf(riga, sizeof(riga), "%s: ", g_syslog_nome);
    vsnprintf(riga + n, sizeof(riga) - (size_t)n, fmt, ap);
    log_seriale(riga);
}

void syslog(int priorita, const char *fmt, ...)
{
    __builtin_va_list ap;

    __builtin_va_start(ap, fmt);
    vsyslog(priorita, fmt, ap);
    __builtin_va_end(ap);
}

void closelog(void) { g_syslog_nome[0] = '\0'; }

struct termios;
int tcgetattr(int fd, struct termios *t)             { (void)fd; (void)t; errno = ENOTTY; return -1; }
int tcsetattr(int fd, int c, const struct termios *t) { (void)fd; (void)c; (void)t; errno = ENOTTY; return -1; }

/* fork: EX-OS non ce l'ha (vedi <unistd.h>). */
int fork(void)
{
    errno = ENOSYS;
    return -1;
}

/* syscall() alla Linux: solo gettid, che su EX-OS e' il pid del filo. */
long syscall(long numero, ...)
{
    if (numero == 224) return filo_id();             /* SYS_gettid */
    errno = ENOSYS;
    return -1;
}

/* basename/dirname di POSIX: vedi <libgen.h>. */
char *basename(char *p)
{
    static char punto[] = ".";
    size_t      n;
    char       *b;

    if (!p || !*p) return punto;
    n = strlen(p);
    while (n > 1 && p[n - 1] == '/') p[--n] = '\0';
    b = strrchr(p, '/');
    return (b && b[1]) ? b + 1 : p;
}

char *dirname(char *p)
{
    static char punto[] = ".";
    size_t      n;

    if (!p || !*p) return punto;
    n = strlen(p);
    while (n > 1 && p[n - 1] == '/') n--;              /* le «/» in fondo */
    while (n > 0 && p[n - 1] != '/') n--;              /* l'ultimo nome */
    if (n == 0) return punto;
    while (n > 1 && p[n - 1] == '/') n--;              /* le «/» prima del nome */
    p[n] = '\0';
    return p;
}

/* fputwc, fputws — la stdio larga, quanto ne chiede fmt (dentro le stringhe
 * di Gecko, xpcom/string). Latin-1 come il resto: un carattere che non ci
 * sta diventa '?', perche' lo stream e' di byte e mezza riga persa e' peggio
 * di un punto interrogativo. */
wint_t fputwc(wchar_t c, FILE *f)
{
    unsigned char b = (unsigned int)c > 0xFFu ? '?' : (unsigned char)c;

    return fputc(b, f) == EOF ? WEOF : (wint_t)c;
}

int fputws(const wchar_t *s, FILE *f)
{
    for (; *s; s++) {
        if (fputwc(*s, f) == WEOF) return -1;
    }
    return 0;
}

/* =============================================================================
 * La stdio larga e i suoi dintorni (30 settembre 2026, tappa 6 di Exilla)
 *
 * ! LA libstdc++ ACCENDE wchar_t SOLO SE <wchar.h> DICHIARA TUTTO QUESTO: il
 * suo configure prova ogni nome, e al primo che manca spegne std::wstring,
 * std::wostream e compagni. Gecko li usa nel suo nucleo (fmt dentro
 * xpcom/string). Latin-1 come il resto della parte larga: un carattere
 * largo e' un byte dello stream.
 *
 * ! LE scanf LARGHE LEGGONO IN STRETTO: il testo e il formato si
 * convertono in Latin-1 e decide vsscanf. Vale per i numeri e le parole;
 * %ls e %lc scriverebbero byte dove il chiamante aspetta wchar_t, quindi non
 * si usano (non li usa nessuno di cio' che gira qui).
 * ============================================================================= */
wint_t btowc(int c) { return c == EOF ? WEOF : (wint_t)(unsigned char)c; }
int    wctob(wint_t c) { return (c == WEOF || (unsigned int)c > 0xFFu) ? EOF : (int)c; }
int    mbsinit(const mbstate_t *stato) { (void)stato; return 1; }   /* senza stato */
size_t mbrlen(const char *s, size_t n, mbstate_t *stato) { return mbrtowc(0, s, n, stato); }

wint_t fgetwc(FILE *f) { int c = fgetc(f); return c == EOF ? WEOF : (wint_t)(unsigned char)c; }
wint_t getwc(FILE *f) { return fgetwc(f); }
wint_t getwchar(void) { return fgetwc(stdin); }
wint_t ungetwc(wint_t c, FILE *f)
{
    if (c == WEOF || (unsigned int)c > 0xFFu) return WEOF;
    return ungetc((int)c, f) == EOF ? WEOF : c;
}
wint_t putwc(wchar_t c, FILE *f) { return fputwc(c, f); }
wint_t putwchar(wchar_t c) { return fputwc(c, stdout); }

wchar_t *fgetws(wchar_t *s, int n, FILE *f)
{
    int i = 0;

    if (n <= 0) return 0;
    while (i < n - 1) {
        wint_t c = fgetwc(f);
        if (c == WEOF) break;
        s[i++] = (wchar_t)c;
        if (c == L'\n') break;
    }
    if (i == 0) return 0;
    s[i] = 0;
    return s;
}

/* Lo stream non ha un orientamento: si dice quello che si chiede. */
int fwide(FILE *f, int modo) { (void)f; return modo; }

int vfwprintf(FILE *f, const wchar_t *fmt, __builtin_va_list ap)
{
    wchar_t  corto[512];
    wchar_t *buf = corto;
    size_t   dim = 512;
    int      n;

    for (;;) {
        __builtin_va_list copia;
        __builtin_va_copy(copia, ap);
        n = vswprintf(buf, dim, fmt, copia);
        __builtin_va_end(copia);
        if (n >= 0 && (size_t)n < dim) break;
        if (buf != corto) free(buf);
        dim *= 4;
        if (dim > 1u << 20 || !(buf = malloc(dim * sizeof(wchar_t)))) return -1;
    }
    if (fputws(buf, f) < 0) n = -1;
    if (buf != corto) free(buf);
    return n;
}
int vwprintf(const wchar_t *fmt, __builtin_va_list ap) { return vfwprintf(stdout, fmt, ap); }
int fwprintf(FILE *f, const wchar_t *fmt, ...)
{
    __builtin_va_list ap; int n;
    __builtin_va_start(ap, fmt); n = vfwprintf(f, fmt, ap); __builtin_va_end(ap);
    return n;
}
int wprintf(const wchar_t *fmt, ...)
{
    __builtin_va_list ap; int n;
    __builtin_va_start(ap, fmt); n = vfwprintf(stdout, fmt, ap); __builtin_va_end(ap);
    return n;
}

/* Da largo a Latin-1, in un buffer allocato (0 se non ci sta). */
static char *largo_in_stretto(const wchar_t *w)
{
    size_t n = 0, i;
    char  *s;

    while (w[n]) n++;
    if (!(s = malloc(n + 1))) return 0;
    for (i = 0; i <= n; i++) s[i] = (unsigned int)w[i] > 0xFFu ? '?' : (char)w[i];
    return s;
}

int vswscanf(const wchar_t *s, const wchar_t *fmt, __builtin_va_list ap)
{
    char *ss = largo_in_stretto(s), *ff = largo_in_stretto(fmt);
    int   n = EOF;

    if (ss && ff) n = vsscanf(ss, ff, ap);
    free(ss); free(ff);
    return n;
}
int swscanf(const wchar_t *s, const wchar_t *fmt, ...)
{
    __builtin_va_list ap; int n;
    __builtin_va_start(ap, fmt); n = vswscanf(s, fmt, ap); __builtin_va_end(ap);
    return n;
}
/* Dallo stream: una riga alla volta, poi come sopra. */
int vfwscanf(FILE *f, const wchar_t *fmt, __builtin_va_list ap)
{
    wchar_t riga[1024];

    if (!fgetws(riga, 1024, f)) return EOF;
    return vswscanf(riga, fmt, ap);
}
int vwscanf(const wchar_t *fmt, __builtin_va_list ap) { return vfwscanf(stdin, fmt, ap); }
int fwscanf(FILE *f, const wchar_t *fmt, ...)
{
    __builtin_va_list ap; int n;
    __builtin_va_start(ap, fmt); n = vfwscanf(f, fmt, ap); __builtin_va_end(ap);
    return n;
}
int wscanf(const wchar_t *fmt, ...)
{
    __builtin_va_list ap; int n;
    __builtin_va_start(ap, fmt); n = vfwscanf(stdin, fmt, ap); __builtin_va_end(ap);
    return n;
}

/* La collazione della località C e' l'ordine dei codici. */
int wcscoll(const wchar_t *a, const wchar_t *b) { return wcscmp(a, b); }
size_t wcsxfrm(wchar_t *dst, const wchar_t *src, size_t n)
{
    size_t l = 0;

    while (src[l]) l++;
    if (n > 0) {
        size_t i, m = l < n - 1 ? l : n - 1;
        for (i = 0; i < m; i++) dst[i] = src[i];
        dst[m] = 0;
    }
    return l;
}

wchar_t *wcstok(wchar_t *s, const wchar_t *sep, wchar_t **resto)
{
    wchar_t *fine;

    if (!s) s = *resto;
    if (!s) return 0;
    s += wcsspn(s, sep);
    if (!*s) { *resto = 0; return 0; }
    fine = wcspbrk(s, sep);
    if (fine) { *fine = 0; *resto = fine + 1; } else *resto = 0;
    return s;
}

size_t wcsftime(wchar_t *dst, size_t max, const wchar_t *fmt, const struct tm *tm)
{
    char   *ff = largo_in_stretto(fmt);
    char    corto[512];
    size_t  n = 0, i;

    if (!ff) return 0;
    if (max > 0) {
        n = strftime(corto, max < sizeof(corto) ? max : sizeof(corto), ff, tm);
        for (i = 0; i <= n; i++) dst[i] = (wchar_t)(unsigned char)corto[i];
    }
    free(ff);
    return n;
}

/* <wctype.h> per nome: l'indice nell'elenco, 0 per un nome sconosciuto. */
static const char *const classi_larghe[] = {
    "alnum", "alpha", "blank", "cntrl", "digit", "graph",
    "lower", "print", "punct", "space", "upper", "xdigit", 0
};
unsigned int wctype(const char *nome)
{
    unsigned int i;

    for (i = 0; classi_larghe[i]; i++)
        if (strcmp(nome, classi_larghe[i]) == 0) return i + 1;
    return 0;
}
int iswctype(wint_t c, unsigned int classe)
{
    switch (classe) {
        case 1:  return iswalnum(c);
        case 2:  return iswalpha(c);
        case 3:  return iswblank(c);
        case 4:  return iswcntrl(c);
        case 5:  return iswdigit(c);
        case 6:  return iswgraph(c);
        case 7:  return iswlower(c);
        case 8:  return iswprint(c);
        case 9:  return iswpunct(c);
        case 10: return iswspace(c);
        case 11: return iswupper(c);
        case 12: return iswxdigit(c);
        default: return 0;
    }
}
unsigned int wctrans(const char *nome)
{
    if (strcmp(nome, "tolower") == 0) return 1;
    if (strcmp(nome, "toupper") == 0) return 2;
    return 0;
}
wint_t towctrans(wint_t c, unsigned int t)
{
    return t == 1 ? towlower(c) : t == 2 ? towupper(c) : c;
}

float       wcstof(const wchar_t *s, wchar_t **fine)  { return (float)wcstod(s, fine); }
long double wcstold(const wchar_t *s, wchar_t **fine) { return wcstod(s, fine); }

/* =============================================================================
 * Quello che Gecko chiede per nome (30 settembre 2026, tappa 6 di Exilla).
 * Le spiegazioni stanno accanto alle dichiarazioni in lib/include/libc.h.
 * ============================================================================= */
int faccessat(int dirfd, const char *path, int modo, int flag)
{
    (void)flag;
    if (dirfd != -100 /* AT_FDCWD */ && (!path || path[0] != '/')) {
        errno = ENOSYS;
        return -1;
    }
    return access(path, modo);
}

/* random: xorshift a 32 bit, con uno stato suo (rand() ha il proprio). */
static unsigned int g_random_stato = 1;
void srandom(unsigned int seme) { g_random_stato = seme ? seme : 1; }
long random(void)
{
    unsigned int x = g_random_stato;

    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g_random_stato = x;
    return (long)(x & 0x7FFFFFFFu);
}

unsigned int alarm(unsigned int secondi) { (void)secondi; return 0; }

static const char *const nomi_mesi[12] = {
    "january", "february", "march", "april", "may", "june", "july",
    "august", "september", "october", "november", "december"
};
static const char *const nomi_giorni[7] = {
    "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"
};

/* Un numero di al piu' `cifre` cifre, fra min e max. */
static const char *sp_numero(const char *s, int cifre, int min, int max, int *v)
{
    int n = 0, letti = 0;

    while (*s == ' ') s++;
    while (letti < cifre && *s >= '0' && *s <= '9') { n = n * 10 + (*s++ - '0'); letti++; }
    if (letti == 0 || n < min || n > max) return 0;
    *v = n;
    return s;
}

/* Un nome dall'elenco, intero o abbreviato alle prime tre lettere. */
static const char *sp_nome(const char *s, const char *const *nomi, int quanti, int *v)
{
    int i;

    for (i = 0; i < quanti; i++) {
        size_t l = strlen(nomi[i]);
        if (strncasecmp(s, nomi[i], l) == 0) { *v = i; return s + l; }
        if (strncasecmp(s, nomi[i], 3) == 0) { *v = i; return s + 3; }
    }
    return 0;
}

char *strptime(const char *s, const char *fmt, struct tm *tm)
{
    int v;

    while (*fmt && s) {
        if (*fmt == ' ' || *fmt == '\t' || *fmt == '\n') {
            while (*s == ' ' || *s == '\t' || *s == '\n') s++;
            fmt++;
            continue;
        }
        if (*fmt != '%') {
            if (*s++ != *fmt++) return 0;
            continue;
        }
        fmt++;
        switch (*fmt++) {
            case 'Y': if ((s = sp_numero(s, 4, 0, 9999, &v))) tm->tm_year = v - 1900; break;
            case 'y': if ((s = sp_numero(s, 2, 0, 99, &v))) tm->tm_year = v < 69 ? v + 100 : v; break;
            case 'm': if ((s = sp_numero(s, 2, 1, 12, &v))) tm->tm_mon = v - 1; break;
            case 'd': case 'e': if ((s = sp_numero(s, 2, 1, 31, &v))) tm->tm_mday = v; break;
            case 'H': if ((s = sp_numero(s, 2, 0, 23, &v))) tm->tm_hour = v; break;
            case 'M': if ((s = sp_numero(s, 2, 0, 59, &v))) tm->tm_min = v; break;
            case 'S': if ((s = sp_numero(s, 2, 0, 60, &v))) tm->tm_sec = v; break;
            case 'j': if ((s = sp_numero(s, 3, 1, 366, &v))) tm->tm_yday = v - 1; break;
            case 'b': case 'B': case 'h':
                if ((s = sp_nome(s, nomi_mesi, 12, &v))) tm->tm_mon = v;
                break;
            case 'a': case 'A':
                if ((s = sp_nome(s, nomi_giorni, 7, &v))) tm->tm_wday = v;
                break;
            case 'p':
                if (strncasecmp(s, "am", 2) == 0) { if (tm->tm_hour == 12) tm->tm_hour = 0; s += 2; }
                else if (strncasecmp(s, "pm", 2) == 0) { if (tm->tm_hour < 12) tm->tm_hour += 12; s += 2; }
                else s = 0;
                break;
            case 'T': s = strptime(s, "%H:%M:%S", tm); break;
            case 'D': s = strptime(s, "%m/%d/%y", tm); break;
            case 'R': s = strptime(s, "%H:%M", tm); break;
            case 'n': case 't': while (*s == ' ' || *s == '\t' || *s == '\n') s++; break;
            case '%': if (*s++ != '%') s = 0; break;
            default: return 0;                 /* una conversione che non si sa fare */
        }
    }
    return (char *)s;
}

/* mlock/munlock: vedi lib/include/libc.h. */
int mlock(const void *addr, size_t lung) { (void)addr; (void)lung; return 0; }
int munlock(const void *addr, size_t lung) { (void)addr; (void)lung; return 0; }

int pipe2(int fd[2], int flag)
{
    if (pipe(fd) < 0) return -1;
    if (flag & 0x800 /* O_NONBLOCK */) {
        fcntl(fd[0], 4 /* F_SETFL */, 0x800);
        fcntl(fd[1], 4 /* F_SETFL */, 0x800);
    }
    return 0;
}

/* <net/if.h>: nessuna interfaccia con un nome (vedi l'header). */
struct if_nameindex { unsigned int if_index; char *if_name; };
unsigned int if_nametoindex(const char *nome) { (void)nome; errno = ENXIO; return 0; }
char *if_indextoname(unsigned int indice, char *nome) { (void)indice; (void)nome; errno = ENXIO; return 0; }
struct if_nameindex *if_nameindex(void)
{
    struct if_nameindex *e = calloc(1, sizeof(*e));    /* solo il terminatore */
    return e;
}
void if_freenameindex(struct if_nameindex *e) { free(e); }

/* <link.h>: i programmi sono statici, non c'e' un elenco di oggetti. */
struct dl_phdr_info;
int dl_iterate_phdr(int (*f)(struct dl_phdr_info *, size_t, void *), void *dato)
{
    (void)f; (void)dato;
    return 0;
}

/* <pwd.h>: una voce sola, fatta con l'ambiente (vedi lib/include/pwd.h). */
struct passwd {
    char *pw_name; char *pw_passwd; uid_t pw_uid; gid_t pw_gid;
    char *pw_gecos; char *pw_dir; char *pw_shell;
};

static void voce_utente(struct passwd *pw, char *nome, char *casa)
{
    const char *u = getenv("USER"), *h = getenv("HOME");

    strncpy(nome, u && *u ? u : "utente", 63); nome[63] = 0;
    strncpy(casa, h && *h ? h : "/", 255);     casa[255] = 0;
    pw->pw_name = nome;
    pw->pw_passwd = (char *)"x";
    pw->pw_uid = (uid_t)getuid();
    pw->pw_gid = (gid_t)getgid();
    pw->pw_gecos = nome;
    pw->pw_dir = casa;
    pw->pw_shell = (char *)"/bin/sh";
}

struct passwd *getpwuid(uid_t uid)
{
    static struct passwd pw;
    static char nome[64], casa[256];

    (void)uid;
    voce_utente(&pw, nome, casa);
    return &pw;
}

struct passwd *getpwnam(const char *nome)
{
    struct passwd *pw = getpwuid(0);
    return strcmp(nome, pw->pw_name) == 0 ? pw : 0;
}

int getpwuid_r(uid_t uid, struct passwd *pw, char *buf, size_t dim, struct passwd **ris)
{
    (void)uid;
    *ris = 0;
    if (dim < 64 + 256) return ERANGE;
    voce_utente(pw, buf, buf + 64);
    *ris = pw;
    return 0;
}

int getpwnam_r(const char *nome, struct passwd *pw, char *buf, size_t dim, struct passwd **ris)
{
    int r = getpwuid_r(0, pw, buf, dim, ris);

    if (r == 0 && strcmp(nome, pw->pw_name) != 0) *ris = 0;
    return r;
}

/* <grp.h>: un gruppo solo, col nome dell'utente (vedi l'header). */
struct group { char *gr_name; char *gr_passwd; gid_t gr_gid; char **gr_mem; };

struct group *getgrgid(gid_t gid)
{
    static struct group gr;
    static char nome[64];
    static char *membri[1];
    const char *u = getenv("USER");

    if (gid != (gid_t)getgid()) return 0;
    strncpy(nome, u && *u ? u : "utente", 63); nome[63] = 0;
    gr.gr_name = nome;
    gr.gr_passwd = (char *)"x";
    gr.gr_gid = gid;
    membri[0] = 0;
    gr.gr_mem = membri;
    return &gr;
}

struct group *getgrnam(const char *nome)
{
    struct group *gr = getgrgid((gid_t)getgid());
    return (gr && strcmp(nome, gr->gr_name) == 0) ? gr : 0;
}

/* Nessun gruppo supplementare. */
int getgroups(int dim, gid_t elenco[]) { (void)dim; (void)elenco; return 0; }

/* ftello, fseeko — ftell e fseek con off_t. Qui off_t e' un long come la
 * posizione di ftell, quindi sono la stessa cosa; il codice di terzi (llama.cpp
 * di Firefox) le chiama per non fermarsi ai 2 GB altrove. */
off_t ftello(FILE *f) { return (off_t)ftell(f); }

int fseeko(FILE *f, off_t off, int da) { return fseek(f, (long)off, da); }

/* Latin-1, come tutta la parte larga di questa libc: un byte, un carattere. */
size_t wcrtomb(char *dst, wchar_t c, mbstate_t *stato)
{
    (void)stato;
    if (!dst) return 1;
    if ((unsigned int)c > 0xFFu) { errno = EILSEQ; return (size_t)-1; }
    *dst = (char)c;
    return 1;
}

size_t mbsrtowcs(wchar_t *dst, const char **src, size_t n, mbstate_t *stato)
{
    const unsigned char *s = (const unsigned char *)*src;
    size_t               i = 0;

    (void)stato;
    if (!dst) return strlen(*src);
    while (i < n) {
        dst[i] = (wchar_t)s[i];
        if (s[i] == 0) { *src = NULL; return i; }
        i++;
    }
    *src = (const char *)(s + i);
    return i;
}

size_t wcsrtombs(char *dst, const wchar_t **src, size_t n, mbstate_t *stato)
{
    const wchar_t *s = *src;
    size_t         i = 0;

    (void)stato;
    for (;;) {
        wchar_t c = s[i];

        if ((unsigned int)c > 0xFFu) { errno = EILSEQ; return (size_t)-1; }
        if (dst) {
            if (i >= n) { *src = s + i; return i; }
            dst[i] = (char)c;
        }
        if (c == 0) { if (dst) *src = NULL; return i; }
        i++;
    }
}
#endif /* !EXOS_LIBC_SO */

/* ! NON NELLA libc.so (29 settembre 2026): la libreria condivisa sta sul floppy
 * da 1,44 MB, e i socket la portavano da 98 a 117 KB — il floppy non si
 * chiudeva piu'. I programmi del sistema parlano allo stack per IPC e non li
 * usano; li usa chi si collega alla libc.a: Rust, Exilla, il software portato.
 * Se un giorno servissero a un programma dinamico, la strada e' una socket.so
 * sul CD, non la libc del floppy. */
#ifndef EXOS_LIBC_SO
/* =============================================================================
 * I SOCKET BSD (@SOCKET-BSD, 29 settembre 2026)
 *
 * Il contratto sta in lib/include/libc.h, nella sezione omonima: qui come e'
 * fatto. Un socket e' una voce di g_prese; il suo descrittore e' PRESA_BASE +
 * indice. Dietro c'e' lo stack IP (drivers/ip/ip.c), a cui si parla col
 * protocollo di drivers/net/ip_proto.h — le strutture sono ripetute qui sotto,
 * come tutto cio' che questo file prende dagli header, e vanno tenute uguali.
 *
 * ! UNA DOMANDA ALLA VOLTA, E NESSUNA ABBANDONATA. Le risposte dello stack non
 * portano un numero di richiesta: l'unico modo di non scambiarle e' che ogni
 * filo ne aspetti una sola e la aspetti fino in fondo. Per questo le attese con
 * scadenza non si fanno MAI dentro lo stack (una RICEVI dimenticata
 * consegnerebbe i suoi dati a chi non li aspetta): quando serve una scadenza,
 * si chiede lo STATO a intervalli e si legge solo quando c'e' qualcosa.
 * Lo stack risponde sempre — anche alle attese che scadono, con -ETIMEDOUT.
 *
 * ! LE RISPOSTE ARRIVANO AL FILO CHE CHIEDE: la cassetta IPC e' sua, e lo
 * scaffale della libc dal 29 settembre separa i messaggi per filo. Lo stack
 * assegna le connessioni al PROGRAMMA (ip.drv 0.005), quindi un filo apre e un
 * altro legge.
 * ============================================================================= */
#define PRESE_MAX       64

#define S_AF_INET       2
#define S_SOCK_STREAM   1
#define S_SOCK_DGRAM    2
#define S_SOCK_NONBLOCK 04000
#define S_SOL_SOCKET    1
#define S_IPPROTO_TCP   6
#define S_IPPROTO_UDP   17
#define S_MSG_PEEK      0x2
#define S_MSG_DONTWAIT  0x40
#define S_MSG_WAITALL   0x100
#define S_O_NONBLOCK    0x800
#define S_F_GETFD       1
#define S_F_SETFD       2
#define S_F_GETFL       3
#define S_F_SETFL       4
#define S_S_IFSOCK      0140000
#define S_FIONREAD      0x541B
#define S_FIONBIO       0x5421
#define S_POLLIN        0x1
#define S_POLLOUT       0x4
#define S_POLLERR       0x8
#define S_POLLHUP       0x10
#define S_POLLNVAL      0x20

#define E_NOTSOCK       88
#define E_DESTADDRREQ   89
#define E_MSGSIZE       90
#define E_PROTONOSUPP   93
#define E_OPNOTSUPP     95
#define E_AFNOSUPPORT   97
#define E_ADDRINUSE     98
#define E_NETDOWN       100
#define E_NETUNREACH    101
#define E_CONNABORTED   103
#define E_CONNRESET     104
#define E_ISCONN        106
#define E_NOTCONN       107
#define E_CONNREFUSED   111
#define E_HOSTUNREACH   113
#define E_PIPE          32
#define E_NOPROTOOPT    92
#define E_NFILE         23

/* drivers/net/ip_proto.h, ripetuto */
#define P_IP_MSG_STATO       2
#define P_IP_MSG_UDP_APRI    5
#define P_IP_MSG_UDP_CHIUDI  6
#define P_IP_MSG_UDP_INVIA   7
#define P_IP_MSG_UDP_RICEVI  8
#define P_IP_MSG_TCP_APRI    9
#define P_IP_MSG_TCP_INVIA  10
#define P_IP_MSG_TCP_RICEVI 11
#define P_IP_MSG_TCP_CHIUDI 12
#define P_IP_MSG_TCP_STATO  13
#define P_IP_MSG_TCP_ASCOLTA 14
#define P_IP_MSG_TCP_ACCETTA 15
#define P_IP_MSG_ESITO     128
#define P_IP_MSG_STATO_R   129
#define P_IP_MSG_UDP_DATI  132
#define P_IP_MSG_TCP_DATI  133
#define P_IP_MSG_TCP_INFO  134
#define P_TCP_APERTA         2
#define P_TCP_DATI_MAX     (IPC_MSG_MAX_DATA - 16)

typedef struct { unsigned char ip[4]; unsigned int porta, timeout_ms; } PTcpApri;
typedef struct { unsigned int id; } PTcpRif;
typedef struct { unsigned int porta; } PTcpAscolta;
typedef struct { unsigned int id, timeout_ms; } PTcpAccetta;
typedef struct { unsigned int id, len; } PTcpDati;
typedef struct {
    unsigned int  id, stato, in_coda_rx, in_coda_tx;
    unsigned char ip[4];
    unsigned int  porta, porta_loc;
} PTcpInfo;
typedef struct { unsigned int porta; } PUdpApri;
typedef struct { unsigned char ip[4]; unsigned int porta, porta_locale; } PUdpInvia;
typedef struct { unsigned char ip[4]; unsigned int porta, porta_locale, len; } PUdpDati;
typedef struct {
    unsigned char ip[4], maschera[4], gateway[4], dns[4];
} PIpConfig;

/* <netinet/in.h>, ripetuto */
struct sockaddr        { unsigned short sa_family; char sa_data[14]; };
struct in_addr         { unsigned int s_addr; };
struct sockaddr_in {
    unsigned short sin_family, sin_port;
    struct in_addr sin_addr;
    unsigned char  sin_zero[8];
};
struct iovec  { void *iov_base; size_t iov_len; };
struct msghdr {
    void *msg_name; unsigned int msg_namelen;
    struct iovec *msg_iov; int msg_iovlen;
    void *msg_control; unsigned int msg_controllen; int msg_flags;
};
struct addrinfo {
    int ai_flags, ai_family, ai_socktype, ai_protocol;
    unsigned int ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};
struct hostent { char *h_name; char **h_aliases; int h_addrtype, h_length; char **h_addr_list; };

typedef struct {
    int           usata;
    int           tipo;          /* S_SOCK_STREAM o S_SOCK_DGRAM */
    unsigned int  id;            /* connessione o ascoltatore dello stack, 0 = nessuno */
    int           ascolta;
    unsigned int  porta_loc;     /* legata con bind, o data dallo stack */
    int           udp_aperta;    /* la porta UDP e' stata aperta nello stack */
    int           udp_prenotata; /* c'e' una UDP_RICEVI in volo */
    unsigned char ip_rem[4];
    unsigned int  porta_rem;
    int           connesso;
    int           nonblocc;
    int           cloexec;
    int           chiusa_rd, chiusa_wr;
    int           fine;          /* l'altra parte ha chiuso: la lettura rende 0 */
    unsigned int  rcv_ms, snd_ms;
    unsigned char *avanzo;       /* cio' che una consegna ha portato in piu' */
    unsigned int  a_pos, a_fine;
    unsigned char *udp_dato;     /* un datagramma arrivato e non ancora letto */
    unsigned int  udp_len;
    int           udp_pieno;
    unsigned char udp_da_ip[4];
    unsigned int  udp_da_porta;
} Presa;

static Presa        g_prese[PRESE_MAX];
static volatile int g_prese_m = 0;
static int          g_pid_ip  = 0;

static unsigned short scambia16(unsigned short v) { return (unsigned short)((v >> 8) | (v << 8)); }
static unsigned int   scambia32(unsigned int v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
}
unsigned short htons(unsigned short v) { return scambia16(v); }
unsigned short ntohs(unsigned short v) { return scambia16(v); }
unsigned int   htonl(unsigned int v)   { return scambia32(v); }
unsigned int   ntohl(unsigned int v)   { return scambia32(v); }

static Presa *presa_di(int fd)
{
    int i = fd - PRESA_BASE;

    if (i < 0 || i >= PRESE_MAX || !g_prese[i].usata) return NULL;
    return &g_prese[i];
}

static int presa_nuova(int tipo)
{
    int i;

    mutex_prendi(&g_prese_m);
    for (i = 0; i < PRESE_MAX; i++)
        if (!g_prese[i].usata) {
            memset(&g_prese[i], 0, sizeof(Presa));
            g_prese[i].usata = 1;
            g_prese[i].tipo  = tipo;
            mutex_lascia(&g_prese_m);
            return PRESA_BASE + i;
        }
    mutex_lascia(&g_prese_m);
    return -E_NFILE;
}

static void dormi_ms(unsigned int ms)
{
    struct timespec t;

    t.tv_sec  = ms / 1000u;
    t.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&t, NULL);
}

/* --- il dialogo con lo stack ------------------------------------------------ */
typedef struct { int pid; unsigned int tipo; } AttesaIp;

/* La risposta che si aspetta, oppure un ESITO: su una richiesta sbagliata lo
 * stack risponde con un ESITO anche quando la risposta buona sarebbe altro
 * (tcp_ricevi con un id che non c'e').
 *
 * ! UN FILTRO VEDE SOLO L'INTESTAZIONE, non i dati (IpcMessage della libc non
 * li porta): per UDP non puo' sapere di quale porta e' un datagramma. Per
 * questo i datagrammi li prende chiunque aspetti e li smista udp_smista(),
 * nella casella del socket giusto. */
static int filtro_ip(const IpcMessage *m, void *d)
{
    const AttesaIp *a = (const AttesaIp *)d;

    if ((int)m->sender_pid != a->pid) return IPC_ALTRUI;
    if (m->tipo == a->tipo) return IPC_MIO;
    if (m->tipo == P_IP_MSG_ESITO && a->tipo != P_IP_MSG_UDP_DATI) return IPC_MIO;
    return IPC_ALTRUI;
}

static int pid_ip(void)
{
    if (g_pid_ip <= 0) g_pid_ip = ipc_lookup("ip");
    return g_pid_ip;
}

/* Manda e aspetta; rende i byte della risposta (tipo in *tipo_r) o -errno. */
static int ip_chiedi(unsigned int tipo, const void *dati, unsigned int len,
                     unsigned int risp, void *buf, unsigned int max,
                     unsigned int *tipo_r)
{
    AttesaIp   a;
    IpcMessage meta;
    int        r, pid = pid_ip();

    if (pid <= 0) return -E_NETDOWN;
    if (ipc_send((unsigned int)pid, tipo, dati, len) < 0) {
        g_pid_ip = 0;               /* lo stack e' ripartito: si ricerca */
        return -E_NETDOWN;
    }
    a.pid = pid; a.tipo = risp;
    r = ipc_scegli(filtro_ip, &a, &meta, buf, max, 0);
    if (r >= 0 && tipo_r) *tipo_r = meta.tipo;
    return r;
}

static int ip_esito(unsigned int tipo, const void *dati, unsigned int len)
{
    int codice = -EIO;
    int r = ip_chiedi(tipo, dati, len, P_IP_MSG_ESITO, &codice, sizeof(codice), NULL);

    if (r < 0) return r;
    return r < (int)sizeof(codice) ? -EIO : codice;
}

static int tcp_info(Presa *p, PTcpInfo *info)
{
    PTcpRif      r;
    unsigned int t = 0;
    int          n;

    memset(info, 0, sizeof(*info));
    r.id = p->id;
    n = ip_chiedi(P_IP_MSG_TCP_STATO, &r, sizeof(r), P_IP_MSG_TCP_INFO,
                  info, sizeof(*info), &t);
    if (n < 0) return n;
    return t == P_IP_MSG_TCP_INFO ? 0 : -EIO;
}

static int presa_errore(int e) { errno = e < 0 ? -e : e; return -1; }

/* --- creare e legare ---------------------------------------------------------- */
int socket(int dominio, int tipo, int protocollo)
{
    int base = tipo & ~(S_SOCK_NONBLOCK | 02000000);
    int fd;

    (void)protocollo;
    if (dominio != S_AF_INET) return presa_errore(E_AFNOSUPPORT);
    if (base != S_SOCK_STREAM && base != S_SOCK_DGRAM) return presa_errore(E_PROTONOSUPP);
    fd = presa_nuova(base);
    if (fd < 0) return presa_errore(fd);
    presa_di(fd)->nonblocc = (tipo & S_SOCK_NONBLOCK) != 0;
    presa_di(fd)->cloexec  = (tipo & 02000000) != 0;
    return fd;
}

int socketpair(int dominio, int tipo, int protocollo, int sv[2])
{
    (void)dominio; (void)tipo; (void)protocollo; (void)sv;
    return presa_errore(E_AFNOSUPPORT);     /* niente AF_UNIX: vedi libc.h */
}

static int indirizzo_in(const struct sockaddr *a, unsigned int len,
                        unsigned char ip[4], unsigned int *porta)
{
    struct sockaddr_in s;

    if (a == NULL || len < sizeof(s)) return -EINVAL;
    memcpy(&s, a, sizeof(s));
    if (s.sin_family != S_AF_INET) return -E_AFNOSUPPORT;
    memcpy(ip, &s.sin_addr.s_addr, 4);
    *porta = ntohs(s.sin_port);
    return 0;
}

static void scrivi_in(struct sockaddr *a, unsigned int *len,
                      const unsigned char ip[4], unsigned int porta)
{
    struct sockaddr_in s;
    unsigned int       q;

    if (a == NULL || len == NULL) return;
    memset(&s, 0, sizeof(s));
    s.sin_family = S_AF_INET;
    s.sin_port   = htons((unsigned short)porta);
    memcpy(&s.sin_addr.s_addr, ip, 4);
    q = *len < sizeof(s) ? *len : sizeof(s);
    memcpy(a, &s, q);
    *len = sizeof(s);
}

static int udp_apri(Presa *p)
{
    PUdpApri a;
    int      r;

    if (p->udp_aperta) return 0;
    a.porta = p->porta_loc;
    r = ip_esito(P_IP_MSG_UDP_APRI, &a, sizeof(a));
    if (r < 0) return r;
    p->porta_loc  = (unsigned int)r;
    p->udp_aperta = 1;
    return 0;
}

int bind(int fd, const struct sockaddr *a, unsigned int len)
{
    Presa        *p = presa_di(fd);
    unsigned char ip[4];
    unsigned int  porta;
    int           r;

    if (!p) return presa_errore(fd >= 0 && fd < PRESA_BASE ? E_NOTSOCK : EBADF);
    r = indirizzo_in(a, len, ip, &porta);
    if (r < 0) return presa_errore(r);
    if (p->porta_loc != 0 || p->udp_aperta) return presa_errore(EINVAL);
    p->porta_loc = porta;
    if (p->tipo == S_SOCK_DGRAM) {
        r = udp_apri(p);
        if (r < 0) { p->porta_loc = 0; return presa_errore(r); }
    }
    return 0;
}

int listen(int fd, int coda)
{
    static unsigned int prossima = 0;
    Presa      *p = presa_di(fd);
    PTcpAscolta a;
    int         r, giri;

    (void)coda;
    if (!p) return presa_errore(EBADF);
    if (p->tipo != S_SOCK_STREAM) return presa_errore(E_OPNOTSUPP);
    if (p->ascolta) return 0;
    for (giri = 0; giri < 64; giri++) {
        /* Porta 0: se ne sceglie una effimera, come farebbe bind(). */
        if (p->porta_loc == 0) {
            if (prossima == 0) prossima = 40000u + (uptime_ms() % 9000u);
            a.porta = prossima++;
        } else a.porta = p->porta_loc;
        r = ip_esito(P_IP_MSG_TCP_ASCOLTA, &a, sizeof(a));
        if (r > 0) {
            p->id = (unsigned int)r; p->ascolta = 1; p->porta_loc = a.porta;
            return 0;
        }
        if (r != -E_ADDRINUSE || p->porta_loc != 0) return presa_errore(r);
    }
    return presa_errore(E_ADDRINUSE);
}

/* --- connettersi -------------------------------------------------------------- */
int connect(int fd, const struct sockaddr *a, unsigned int len)
{
    Presa        *p = presa_di(fd);
    unsigned char ip[4];
    unsigned int  porta;
    PTcpApri      ap;
    int           r, giri;

    if (!p) return presa_errore(fd >= 0 && fd < PRESA_BASE ? E_NOTSOCK : EBADF);
    r = indirizzo_in(a, len, ip, &porta);
    if (r < 0) return presa_errore(r);

    if (p->tipo == S_SOCK_DGRAM) {           /* UDP: si ricorda e basta */
        memcpy(p->ip_rem, ip, 4);
        p->porta_rem = porta;
        p->connesso  = 1;
        return 0;
    }
    if (p->connesso || p->ascolta) return presa_errore(E_ISCONN);

    memcpy(ap.ip, ip, 4);
    ap.porta      = porta;
    ap.timeout_ms = 20000;
    /* ! -EAGAIN E' L'ARP: lo stack non conosce ancora l'indirizzo Ethernet del
     * prossimo salto, ha mandato la domanda e chiede di riprovare. */
    for (giri = 0; giri < 30; giri++) {
        r = ip_esito(P_IP_MSG_TCP_APRI, &ap, sizeof(ap));
        if (r != -EAGAIN) break;
        dormi_ms(100);
    }
    if (r <= 0) {
        if (r == 0 || r == -ETIMEDOUT) return presa_errore(r ? r : ETIMEDOUT);
        return presa_errore(r);
    }
    p->id        = (unsigned int)r;
    p->connesso  = 1;
    memcpy(p->ip_rem, ip, 4);
    p->porta_rem = porta;
    {
        PTcpInfo info;

        if (tcp_info(p, &info) == 0) p->porta_loc = info.porta_loc;
    }
    return 0;
}

int accept4(int fd, struct sockaddr *a, unsigned int *len, int flag)
{
    Presa       *p = presa_di(fd), *n;
    PTcpAccetta  ac;
    PTcpInfo     info;
    int          r, nuovo;
    unsigned int inizio = uptime_ms();

    if (!p) return presa_errore(EBADF);
    if (!p->ascolta) return presa_errore(EINVAL);
    ac.id = p->id;
    for (;;) {
        ac.timeout_ms = p->nonblocc ? 0 : 1000;
        r = ip_esito(P_IP_MSG_TCP_ACCETTA, &ac, sizeof(ac));
        if (r > 0) break;
        if (r != -EAGAIN && r != -ETIMEDOUT) return presa_errore(r);
        if (p->nonblocc) return presa_errore(EAGAIN);
        if (p->rcv_ms && uptime_ms() - inizio >= p->rcv_ms) return presa_errore(EAGAIN);
    }
    nuovo = presa_nuova(S_SOCK_STREAM);
    if (nuovo < 0) {
        PTcpRif rif;

        rif.id = (unsigned int)r;
        ip_esito(P_IP_MSG_TCP_CHIUDI, &rif, sizeof(rif));
        return presa_errore(nuovo);
    }
    n = presa_di(nuovo);
    n->id       = (unsigned int)r;
    n->connesso = 1;
    n->nonblocc = (flag & S_SOCK_NONBLOCK) != 0;
    n->cloexec  = (flag & 02000000) != 0;
    if (tcp_info(n, &info) == 0) {
        memcpy(n->ip_rem, info.ip, 4);
        n->porta_rem = info.porta;
        n->porta_loc = info.porta_loc ? info.porta_loc : p->porta_loc;
    }
    scrivi_in(a, len, n->ip_rem, n->porta_rem);
    return nuovo;
}

int accept(int fd, struct sockaddr *a, unsigned int *len)
{
    return accept4(fd, a, len, 0);
}

/* --- scrivere ------------------------------------------------------------------- */
static ssize_t tcp_manda(Presa *p, const unsigned char *buf, size_t n, int flag)
{
    unsigned char msg[IPC_MSG_MAX_DATA];
    PTcpDati      d;
    size_t        fatti = 0;
    unsigned int  inizio = uptime_ms();
    int           nb = p->nonblocc || (flag & S_MSG_DONTWAIT);

    if (!p->connesso) return presa_errore(E_NOTCONN);
    if (p->chiusa_wr) return presa_errore(E_PIPE);
    while (fatti < n) {
        unsigned int q = (unsigned int)(n - fatti);
        int          r;

        if (q > P_TCP_DATI_MAX) q = P_TCP_DATI_MAX;
        d.id = p->id; d.len = q;
        memcpy(msg, &d, sizeof(d));
        memcpy(msg + sizeof(d), buf + fatti, q);
        r = ip_esito(P_IP_MSG_TCP_INVIA, msg, (unsigned int)sizeof(d) + q);
        if (r < 0) {
            if (fatti) return (ssize_t)fatti;
            return presa_errore(r == -EBADF || r == -EINVAL ? -E_PIPE : r);
        }
        if (r == 0) {
            /* Il buffer dello stack e' pieno: chi non blocca lo dice, chi
             * blocca aspetta che si svuoti — se la connessione e' ancora viva. */
            PTcpInfo info;

            if (fatti && nb) return (ssize_t)fatti;
            if (nb) return presa_errore(EAGAIN);
            if (tcp_info(p, &info) < 0 || info.stato != P_TCP_APERTA)
                return fatti ? (ssize_t)fatti : presa_errore(E_PIPE);
            if (p->snd_ms && uptime_ms() - inizio >= p->snd_ms)
                return fatti ? (ssize_t)fatti : presa_errore(EAGAIN);
            dormi_ms(10);
            continue;
        }
        fatti += (size_t)r;
        if (nb && (unsigned int)r < q) break;
    }
    return (ssize_t)fatti;
}

static int udp_prenota(Presa *p);

static ssize_t udp_manda(Presa *p, const void *buf, size_t n,
                         const unsigned char ip[4], unsigned int porta)
{
    unsigned char msg[IPC_MSG_MAX_DATA];
    PUdpInvia     inv;
    int           r, giri;

    if (n > IPC_MSG_MAX_DATA - sizeof(inv)) return presa_errore(E_MSGSIZE);
    r = udp_apri(p);
    if (r < 0) return presa_errore(r);
    /* ! LA RICEZIONE SI PRENOTA PRIMA DI MANDARE: lo stack butta un datagramma
     * che arriva mentre nessuno lo aspetta, e la risposta di un DNS vicino
     * arriva prima che il chiamante sia tornato da sendto per chiamare
     * recvfrom. E' quello che fa gia' lib/dns.c; la prima versione di questa
     * sezione no, e ogni domanda al DNS di QEMU andava persa. */
    udp_prenota(p);
    memcpy(inv.ip, ip, 4);
    inv.porta = porta; inv.porta_locale = p->porta_loc;
    memcpy(msg, &inv, sizeof(inv));
    memcpy(msg + sizeof(inv), buf, n);
    for (giri = 0; giri < 20; giri++) {
        r = ip_esito(P_IP_MSG_UDP_INVIA, msg, (unsigned int)(sizeof(inv) + n));
        if (r != -EBUSY) break;         /* un'altra risoluzione ARP in corso */
        dormi_ms(50);
    }
    return r < 0 ? presa_errore(r) : (ssize_t)n;
}

ssize_t sendto(int fd, const void *buf, size_t n, int flag,
               const struct sockaddr *a, unsigned int len)
{
    Presa        *p = presa_di(fd);
    unsigned char ip[4];
    unsigned int  porta;

    if (!p) return presa_errore(EBADF);
    if (p->tipo == S_SOCK_STREAM) return tcp_manda(p, (const unsigned char *)buf, n, flag);
    if (a) {
        int r = indirizzo_in(a, len, ip, &porta);

        if (r < 0) return presa_errore(r);
    } else if (p->connesso) {
        memcpy(ip, p->ip_rem, 4);
        porta = p->porta_rem;
    } else return presa_errore(E_DESTADDRREQ);
    return udp_manda(p, buf, n, ip, porta);
}

ssize_t send(int fd, const void *buf, size_t n, int flag)
{
    return sendto(fd, buf, n, flag, NULL, 0);
}

/* --- leggere ---------------------------------------------------------------------- */
static ssize_t da_avanzo(Presa *p, unsigned char *buf, size_t n, int flag)
{
    unsigned int q = p->a_fine - p->a_pos;

    if (q > n) q = (unsigned int)n;
    memcpy(buf, p->avanzo + p->a_pos, q);
    if (!(flag & S_MSG_PEEK)) p->a_pos += q;
    return (ssize_t)q;
}

/* Una consegna dallo stack, nell'avanzo. Rende 1 se ha portato dati, 0 alla
 * fine, -errno sull'errore. */
static int tcp_riempi(Presa *p)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    PTcpRif       r;
    PTcpDati      d;
    unsigned int  t = 0;
    int           n;

    if (!p->avanzo) {
        p->avanzo = (unsigned char *)malloc(P_TCP_DATI_MAX);
        if (!p->avanzo) return -ENOMEM;
    }
    r.id = p->id;
    n = ip_chiedi(P_IP_MSG_TCP_RICEVI, &r, sizeof(r), P_IP_MSG_TCP_DATI,
                  buf, sizeof(buf), &t);
    if (n < 0) return n;
    if (t == P_IP_MSG_ESITO) {
        int codice;

        memcpy(&codice, buf, sizeof(codice));
        return codice < 0 ? codice : -EIO;
    }
    if (n < (int)sizeof(d)) return -EIO;
    memcpy(&d, buf, sizeof(d));
    if (d.len == 0) { p->fine = 1; return 0; }
    if (d.len > P_TCP_DATI_MAX || d.len > (unsigned int)n - sizeof(d)) return -EIO;
    memcpy(p->avanzo, buf + sizeof(d), d.len);
    p->a_pos = 0; p->a_fine = d.len;
    return 1;
}

static ssize_t tcp_prendi(Presa *p, unsigned char *buf, size_t n, int flag)
{
    unsigned int inizio = uptime_ms();
    int          nb = p->nonblocc || (flag & S_MSG_DONTWAIT);
    size_t       fatti = 0;

    if (!p->connesso) return presa_errore(E_NOTCONN);
    if (n == 0) return 0;
    for (;;) {
        int r;

        if (p->a_pos < p->a_fine) {
            ssize_t q = da_avanzo(p, buf + fatti, n - fatti, flag);

            fatti += (size_t)q;
            if (!(flag & S_MSG_WAITALL) || fatti == n || (flag & S_MSG_PEEK))
                return (ssize_t)fatti;
            continue;
        }
        if (p->fine || p->chiusa_rd) return (ssize_t)fatti;

        /* ! CON UNA SCADENZA, O SENZA BLOCCARE, SI GUARDA PRIMA DI CHIEDERE: una
         * RICEVI resta in volo finche' arrivano dati, e non si puo' ritirare
         * (vedi in cima alla sezione). */
        if (nb || p->rcv_ms) {
            PTcpInfo info;

            r = tcp_info(p, &info);
            if (r < 0) return fatti ? (ssize_t)fatti : presa_errore(r);
            if (info.in_coda_rx == 0 && info.stato == P_TCP_APERTA) {
                if (fatti) return (ssize_t)fatti;
                if (nb) return presa_errore(EAGAIN);
                if (uptime_ms() - inizio >= p->rcv_ms) return presa_errore(EAGAIN);
                dormi_ms(10);
                continue;
            }
        }
        r = tcp_riempi(p);
        if (r < 0) return fatti ? (ssize_t)fatti : presa_errore(r == -EBADF ? -E_CONNRESET : r);
    }
}

/* Prende UN datagramma dello stack arrivato a questo filo e lo mette nella
 * casella del socket della sua porta (se e' piena, o il socket non c'e' piu',
 * si butta: UDP perde comunque). Rende 1 se ne ha smistato uno, 0 se e'
 * scaduta l'attesa, -errno. `ms` come ipc_scegli: 0 senza scadenza. */
static int udp_smista(unsigned int ms)
{
    unsigned char msg[IPC_MSG_MAX_DATA];
    AttesaIp      at;
    IpcMessage    meta;
    PUdpDati      u;
    int           r, i;

    at.pid = pid_ip(); at.tipo = P_IP_MSG_UDP_DATI;
    if (at.pid <= 0) return -E_NETDOWN;
    r = ipc_scegli(filtro_ip, &at, &meta, msg, sizeof(msg), ms);
    if (r == -ETIMEDOUT) return 0;
    if (r < 0) return r;
    if (r < (int)sizeof(u)) return 1;
    memcpy(&u, msg, sizeof(u));
    if (u.len > (unsigned int)r - sizeof(u)) u.len = (unsigned int)r - sizeof(u);
    for (i = 0; i < PRESE_MAX; i++) {
        Presa *p = &g_prese[i];

        if (!p->usata || p->tipo != S_SOCK_DGRAM || !p->udp_aperta) continue;
        if (p->porta_loc != u.porta_locale) continue;
        p->udp_prenotata = 0;               /* la sua prenotazione e' servita */
        if (p->udp_pieno) break;
        if (!p->udp_dato) p->udp_dato = (unsigned char *)malloc(IPC_MSG_MAX_DATA);
        if (!p->udp_dato) break;
        memcpy(p->udp_dato, msg + sizeof(u), u.len);
        p->udp_len = u.len;
        memcpy(p->udp_da_ip, u.ip, 4);
        p->udp_da_porta = u.porta;
        p->udp_pieno = 1;
        break;
    }
    return 1;
}

static int udp_prenota(Presa *p)
{
    PUdpApri ap;
    int      pid = pid_ip();

    if (p->udp_prenotata) return 0;
    if (pid <= 0) return -E_NETDOWN;
    ap.porta = p->porta_loc;
    if (ipc_send((unsigned int)pid, P_IP_MSG_UDP_RICEVI, &ap, sizeof(ap)) < 0)
        return -E_NETDOWN;
    p->udp_prenotata = 1;
    return 0;
}

/* UDP: la ricezione si prenota e si aspetta finche' nella casella del socket
 * non c'e' un datagramma. Con una scadenza la prenotazione resta: il datagramma
 * che arrivera' sara' del prossimo recvfrom. */
static ssize_t udp_prendi(Presa *p, void *buf, size_t n, int flag,
                          struct sockaddr *a, unsigned int *len)
{
    unsigned int inizio = uptime_ms();
    int          nb = p->nonblocc || (flag & S_MSG_DONTWAIT);

    if (!p->udp_aperta) return presa_errore(EINVAL);   /* niente bind, niente sendto */
    while (!p->udp_pieno) {
        unsigned int ms;
        int          r = udp_prenota(p);

        if (r < 0) return presa_errore(r);
        if (nb) ms = IPC_SUBITO;
        else if (p->rcv_ms) {
            unsigned int passati = uptime_ms() - inizio;

            if (passati >= p->rcv_ms) return presa_errore(EAGAIN);
            ms = p->rcv_ms - passati;
        } else ms = 0;
        r = udp_smista(ms);
        if (r < 0) return presa_errore(r);
        if (r == 0 && !p->udp_pieno) return presa_errore(EAGAIN);
    }
    if (n > p->udp_len) n = p->udp_len;
    memcpy(buf, p->udp_dato, n);
    scrivi_in(a, len, p->udp_da_ip, p->udp_da_porta);
    if (!(flag & S_MSG_PEEK)) p->udp_pieno = 0;
    return (ssize_t)n;
}

ssize_t recvfrom(int fd, void *buf, size_t n, int flag,
                 struct sockaddr *a, unsigned int *len)
{
    Presa *p = presa_di(fd);

    if (!p) return presa_errore(EBADF);
    if (p->tipo == S_SOCK_DGRAM) return udp_prendi(p, buf, n, flag, a, len);
    if (a && len) scrivi_in(a, len, p->ip_rem, p->porta_rem);
    return tcp_prendi(p, (unsigned char *)buf, n, flag);
}

ssize_t recv(int fd, void *buf, size_t n, int flag)
{
    return recvfrom(fd, buf, n, flag, NULL, NULL);
}

ssize_t sendmsg(int fd, const struct msghdr *m, int flag)
{
    unsigned char *tutto;
    size_t         tot = 0, o = 0;
    ssize_t        r;
    int            i;

    if (!m) return presa_errore(EINVAL);
    for (i = 0; i < m->msg_iovlen; i++) tot += m->msg_iov[i].iov_len;
    tutto = (unsigned char *)malloc(tot ? tot : 1);
    if (!tutto) return presa_errore(ENOMEM);
    for (i = 0; i < m->msg_iovlen; i++) {
        memcpy(tutto + o, m->msg_iov[i].iov_base, m->msg_iov[i].iov_len);
        o += m->msg_iov[i].iov_len;
    }
    r = sendto(fd, tutto, tot, flag, (const struct sockaddr *)m->msg_name, m->msg_namelen);
    free(tutto);
    return r;
}

ssize_t recvmsg(int fd, struct msghdr *m, int flag)
{
    unsigned char *tutto;
    size_t         tot = 0, o = 0;
    ssize_t        r;
    int            i;

    if (!m) return presa_errore(EINVAL);
    for (i = 0; i < m->msg_iovlen; i++) tot += m->msg_iov[i].iov_len;
    tutto = (unsigned char *)malloc(tot ? tot : 1);
    if (!tutto) return presa_errore(ENOMEM);
    r = recvfrom(fd, tutto, tot, flag, (struct sockaddr *)m->msg_name,
                 m->msg_name ? &m->msg_namelen : NULL);
    for (i = 0; r > 0 && i < m->msg_iovlen && o < (size_t)r; i++) {
        size_t q = m->msg_iov[i].iov_len;

        if (q > (size_t)r - o) q = (size_t)r - o;
        memcpy(m->msg_iov[i].iov_base, tutto + o, q);
        o += q;
    }
    free(tutto);
    m->msg_controllen = 0;
    m->msg_flags = 0;
    return r;
}

/* readv/writev per ogni descrittore: un pezzo alla volta, quindi non atomiche
 * — come lo e' gia' write() su una pipe piena. */
ssize_t readv(int fd, const struct iovec *v, int n)
{
    ssize_t tot = 0;
    int     i;

    for (i = 0; i < n; i++) {
        ssize_t r = read(fd, v[i].iov_base, v[i].iov_len);

        if (r < 0) return tot ? tot : r;
        tot += r;
        if ((size_t)r < v[i].iov_len) break;
    }
    return tot;
}

ssize_t writev(int fd, const struct iovec *v, int n)
{
    ssize_t tot = 0;
    int     i;

    for (i = 0; i < n; i++) {
        ssize_t r = write(fd, v[i].iov_base, v[i].iov_len);

        if (r < 0) return tot ? tot : r;
        tot += r;
        if ((size_t)r < v[i].iov_len) break;
    }
    return tot;
}

/* --- chiudere --------------------------------------------------------------------- */

static int presa_chiudi(int fd)
{
    Presa *p = presa_di(fd);

    if (!p) return presa_errore(EBADF);
    if (p->tipo == S_SOCK_STREAM && p->id) {
        PTcpRif r;

        r.id = p->id;
        ip_esito(P_IP_MSG_TCP_CHIUDI, &r, sizeof(r));
    } else if (p->tipo == S_SOCK_DGRAM && p->udp_aperta) {
        PUdpApri a;

        a.porta = p->porta_loc;
        ip_esito(P_IP_MSG_UDP_CHIUDI, &a, sizeof(a));
        /* Un datagramma gia' consegnato per una prenotazione non servita
         * arrivera' a nessuno: udp_smista lo buttera' quando lo trova. */
    }
    free(p->avanzo);
    free(p->udp_dato);
    mutex_prendi(&g_prese_m);
    p->usata = 0;
    mutex_lascia(&g_prese_m);
    return 0;
}

int shutdown(int fd, int come)
{
    Presa *p = presa_di(fd);

    if (!p) return presa_errore(fd >= 0 && fd < PRESA_BASE ? E_NOTSOCK : EBADF);
    if (!p->connesso) return presa_errore(E_NOTCONN);
    if (come == 0 || come == 2) p->chiusa_rd = 1;
    if (come == 1 || come == 2) p->chiusa_wr = 1;
    /* ! SOLO SHUT_RDWR CHIUDE NELLO STACK: la sua chiusura butta anche cio' che
     * deve ancora arrivare, e un «ho finito di scrivere» non deve perdere la
     * risposta. Vedi libc.h. */
    if (come == 2 && p->tipo == S_SOCK_STREAM && p->id) {
        PTcpRif r;

        r.id = p->id;
        ip_esito(P_IP_MSG_TCP_CHIUDI, &r, sizeof(r));
        p->fine = 1;
    }
    return 0;
}

/* --- opzioni e nomi ------------------------------------------------------------------ */
int getsockopt(int fd, int livello, int nome, void *val, unsigned int *len)
{
    Presa *p = presa_di(fd);
    int    v = 0;

    if (!p) return presa_errore(fd >= 0 && fd < PRESA_BASE ? E_NOTSOCK : EBADF);
    if (!val || !len) return presa_errore(EFAULT);
    if (livello == S_SOL_SOCKET && (nome == 20 || nome == 21)) {
        struct timeval tv;
        unsigned int   ms = nome == 20 ? p->rcv_ms : p->snd_ms;

        tv.tv_sec = ms / 1000u; tv.tv_usec = (long)(ms % 1000u) * 1000L;
        if (*len > sizeof(tv)) *len = sizeof(tv);
        memcpy(val, &tv, *len);
        return 0;
    }
    if (livello == S_SOL_SOCKET) {
        switch (nome) {
        case 3:  v = p->tipo; break;                  /* SO_TYPE */
        case 4:  v = 0; break;                        /* SO_ERROR: connect e' sincrono */
        case 7: case 8: v = 8192; break;              /* SO_SNDBUF, SO_RCVBUF */
        case 2: case 6: case 9: case 13: case 15: v = 0; break;
        default: return presa_errore(E_NOPROTOOPT);
        }
    } else if (livello == S_IPPROTO_TCP) {
        v = (nome == 1) ? 1 : 0;                      /* TCP_NODELAY: non si ritarda mai */
    } else if (livello == 0) {
        v = (nome == 2) ? 64 : 0;                     /* IP_TTL */
    } else return presa_errore(E_NOPROTOOPT);
    if (*len > sizeof(v)) *len = sizeof(v);
    memcpy(val, &v, *len);
    return 0;
}

int setsockopt(int fd, int livello, int nome, const void *val, unsigned int len)
{
    Presa *p = presa_di(fd);

    if (!p) return presa_errore(fd >= 0 && fd < PRESA_BASE ? E_NOTSOCK : EBADF);
    if (livello == S_SOL_SOCKET && (nome == 20 || nome == 21)) {
        struct timeval tv;
        unsigned int   ms;

        if (!val || len < sizeof(tv)) return presa_errore(EINVAL);
        memcpy(&tv, val, sizeof(tv));
        ms = (unsigned int)tv.tv_sec * 1000u + (unsigned int)(tv.tv_usec / 1000L);
        if (ms == 0 && (tv.tv_sec || tv.tv_usec)) ms = 1;
        if (nome == 20) p->rcv_ms = ms; else p->snd_ms = ms;
        return 0;
    }
    /* ! LE ALTRE SI ACCETTANO E NON CAMBIANO NIENTE, e lo si dice: riuso delle
     * porte, keepalive, misure dei buffer, TCP_NODELAY (lo stack non ritarda
     * mai), TTL. Rifiutarle farebbe fallire programmi che le chiedono per
     * abitudine; getsockopt risponde con quel che succede davvero. */
    if (livello == S_SOL_SOCKET || livello == S_IPPROTO_TCP || livello == 0 ||
        livello == 41) return 0;
    return presa_errore(E_NOPROTOOPT);
}

static int ip_locale(unsigned char ip[4])
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  t = 0;
    int           n = ip_chiedi(P_IP_MSG_STATO, NULL, 0, P_IP_MSG_STATO_R,
                                buf, sizeof(buf), &t);

    if (n < (int)sizeof(PIpConfig) || t != P_IP_MSG_STATO_R) return -1;
    memcpy(ip, ((PIpConfig *)buf)->ip, 4);
    return 0;
}

int getsockname(int fd, struct sockaddr *a, unsigned int *len)
{
    Presa        *p = presa_di(fd);
    unsigned char ip[4] = { 0, 0, 0, 0 };

    if (!p) return presa_errore(fd >= 0 && fd < PRESA_BASE ? E_NOTSOCK : EBADF);
    if (p->connesso) ip_locale(ip);
    scrivi_in(a, len, ip, p->porta_loc);
    return 0;
}

int getpeername(int fd, struct sockaddr *a, unsigned int *len)
{
    Presa *p = presa_di(fd);

    if (!p) return presa_errore(fd >= 0 && fd < PRESA_BASE ? E_NOTSOCK : EBADF);
    if (!p->connesso) return presa_errore(E_NOTCONN);
    scrivi_in(a, len, p->ip_rem, p->porta_rem);
    return 0;
}

/* --- i descrittori comuni ----------------------------------------------------------- */
static int presa_fcntl(int fd, int cmd, unsigned int arg)
{
    Presa *p = presa_di(fd);

    if (!p) return presa_errore(EBADF);
    switch (cmd) {
    case S_F_GETFL: return O_RDWR | (p->nonblocc ? S_O_NONBLOCK : 0);
    case S_F_SETFL: p->nonblocc = (arg & S_O_NONBLOCK) != 0; return 0;
    case S_F_GETFD: return p->cloexec;
    case S_F_SETFD: p->cloexec = (int)(arg & 1u); return 0;
    default:        return presa_errore(E_OPNOTSUPP);   /* F_DUPFD: vedi libc.h */
    }
}

static int presa_ioctl(int fd, unsigned int req, void *arg)
{
    Presa *p = presa_di(fd);

    if (!p) return presa_errore(EBADF);
    if (req == S_FIONBIO) {
        if (!arg) return presa_errore(EFAULT);
        p->nonblocc = *(int *)arg != 0;
        return 0;
    }
    if (req == S_FIONREAD) {
        int n = (int)(p->a_fine - p->a_pos);

        if (!arg) return presa_errore(EFAULT);
        if (p->tipo == S_SOCK_STREAM && p->connesso) {
            PTcpInfo info;

            if (tcp_info(p, &info) == 0) n += (int)info.in_coda_rx;
        }
        *(int *)arg = n;
        return 0;
    }
    return presa_errore(ENOTTY);
}

/* Che cosa si puo' fare adesso con un socket, per poll(). */
static short presa_pronta(Presa *p, short voluti)
{
    short    r = 0;
    PTcpInfo info;

    if (p->tipo == S_SOCK_DGRAM) {
        r |= S_POLLOUT;
        /* ! UN DATAGRAMMA C'E' SE E' GIA' NELLA CASELLA, o se arriva adesso: si
         * prenota (se non lo e') e si smista quel che c'e' senza aspettare. */
        if ((voluti & S_POLLIN) && p->udp_aperta && !p->udp_pieno) {
            if (udp_prenota(p) == 0)
                while (udp_smista(IPC_SUBITO) > 0 && !p->udp_pieno) { }
        }
        if (p->udp_pieno) r |= S_POLLIN;
        return (short)(r & (voluti | S_POLLERR | S_POLLHUP));
    }

    if (p->ascolta) {
        /* Un ascoltatore e' «leggibile» quando c'e' qualcuno da accettare: lo si
         * saprebbe solo accettando, e allora lo si dice pronto e accept(), non
         * bloccante, rende EAGAIN se non era vero. */
        return (short)(voluti & S_POLLIN);
    }
    if (!p->connesso) return 0;
    if (p->a_pos < p->a_fine || p->fine) r |= S_POLLIN;
    if (tcp_info(p, &info) < 0) return S_POLLERR;
    if (info.in_coda_rx > 0) r |= S_POLLIN;
    if (info.stato == P_TCP_APERTA) r |= S_POLLOUT;
    else r |= S_POLLIN | S_POLLHUP;           /* la lettura rende la fine */
    return (short)(r & (voluti | S_POLLERR | S_POLLHUP));
}

/* ! poll CON I SOCKET DENTRO: i descrittori del kernel li guarda il kernel, i
 * socket li guarda lo stack. Se ce n'e' anche uno solo, poll diventa un giro:
 * il kernel con una scadenza corta (dieci millisecondi, un tick — dorme
 * davvero, e si sveglia prima se un suo descrittore si muove), poi lo STATO di
 * ogni socket. Costa una domanda allo stack per socket per giro, cioe' niente
 * rispetto a una pagina che arriva; e un programma senza socket non passa di
 * qui. La strada per toglierlo, il giorno che si misuri, e' che lo stack
 * sappia svegliare chi aspetta. */
static int poll_kernel(struct pollfd *fds, unsigned int nfds, int timeout);

static int poll_misto(struct pollfd *fds, unsigned int nfds, int timeout)
{
    struct pollfd  locali[64];
    unsigned int   mappa[64];
    unsigned int   nk = 0, i;
    unsigned int   inizio = uptime_ms();

    if (nfds > 64) { errno = EINVAL; return -1; }
    for (i = 0; i < nfds; i++)
        if (fds[i].fd < PRESA_BASE) { locali[nk] = fds[i]; mappa[nk] = i; nk++; }

    for (;;) {
        int pronti = 0, r;

        for (i = 0; i < nfds; i++) fds[i].revents = 0;
        if (nk) {
            unsigned int k;

            r = poll_kernel(locali, nk, 0);
            if (r < 0) return -1;
            for (k = 0; k < nk; k++) fds[mappa[k]].revents = locali[k].revents;
        }
        for (i = 0; i < nfds; i++) {
            if (fds[i].fd >= PRESA_BASE) {
                Presa *p = presa_di(fds[i].fd);

                fds[i].revents = p ? presa_pronta(p, fds[i].events) : S_POLLNVAL;
            }
            if (fds[i].revents) pronti++;
        }
        if (pronti) return pronti;
        if (timeout == 0) return 0;
        if (timeout > 0 && uptime_ms() - inizio >= (unsigned int)timeout) return 0;
        if (nk) {
            unsigned int k;

            r = poll_kernel(locali, nk, 10);
            if (r > 0) {
                for (k = 0; k < nk; k++) fds[mappa[k]].revents = locali[k].revents;
                return r;
            }
        } else dormi_ms(10);
    }
}

/* --- indirizzi in testo -------------------------------------------------------------- */
int inet_pton(int famiglia, const char *s, void *dst)
{
    unsigned char ip[4];
    int           i;

    if (famiglia != S_AF_INET) { errno = E_AFNOSUPPORT; return -1; }
    for (i = 0; i < 4; i++) {
        int v = 0, cifre = 0;

        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (*s++ - '0');
            if (++cifre > 3 || v > 255) return 0;
        }
        if (cifre == 0) return 0;
        ip[i] = (unsigned char)v;
        if (i < 3 && *s++ != '.') return 0;
    }
    if (*s) return 0;
    memcpy(dst, ip, 4);
    return 1;
}

const char *inet_ntop(int famiglia, const void *src, char *dst, unsigned int n)
{
    const unsigned char *b = (const unsigned char *)src;
    char                 t[16];

    if (famiglia != S_AF_INET) { errno = E_AFNOSUPPORT; return NULL; }
    snprintf(t, sizeof(t), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    if (strlen(t) + 1 > n) { errno = 28; return NULL; }   /* ENOSPC */
    strcpy(dst, t);
    return dst;
}

int inet_aton(const char *s, struct in_addr *out)
{
    return inet_pton(S_AF_INET, s, &out->s_addr) == 1;
}

unsigned int inet_addr(const char *s)
{
    unsigned int a;

    return inet_pton(S_AF_INET, s, &a) == 1 ? a : 0xFFFFFFFFu;
}

char *inet_ntoa(struct in_addr a)
{
    static char t[16];

    inet_ntop(S_AF_INET, &a.s_addr, t, sizeof(t));
    return t;
}

int gethostname(char *nome, size_t n)
{
    if (!nome || n < 5) { errno = EINVAL; return -1; }
    strcpy(nome, "exos");
    return 0;
}

/* --- il risolutore ---------------------------------------------------------------------
 * Una domanda A al DNS che il DHCP ha dato allo stack (IpConfig.dns), sopra un
 * socket UDP di questa sezione. Tre tentativi da due secondi. */
static int dns_chiedi(const char *nome, unsigned char ip[4])
{
    unsigned char      buf[IPC_MSG_MAX_DATA], q[300];
    PIpConfig          cfg;
    struct sockaddr_in srv;
    struct timeval     tv;
    unsigned int       t = 0, o, id;
    int                n, fd, giro, esito = -2;       /* EAI_NONAME */
    const char        *s;

    n = ip_chiedi(P_IP_MSG_STATO, NULL, 0, P_IP_MSG_STATO_R, buf, sizeof(buf), &t);
    if (n < (int)sizeof(cfg) || t != P_IP_MSG_STATO_R) return -11;    /* EAI_SYSTEM */
    memcpy(&cfg, buf, sizeof(cfg));
    if (!cfg.dns[0] && !cfg.dns[1] && !cfg.dns[2] && !cfg.dns[3]) return -4;   /* EAI_FAIL */

    /* La domanda: intestazione, nome a etichette, tipo A, classe IN. */
    id = (uptime_ms() * 2654435761u) >> 16;
    memset(q, 0, 12);
    q[0] = (unsigned char)(id >> 8); q[1] = (unsigned char)id;
    q[2] = 0x01;                                /* ricorsione desiderata */
    q[5] = 1;                                   /* una domanda */
    o = 12;
    for (s = nome; *s; ) {
        const char  *pt = strchr(s, '.');
        unsigned int l  = pt ? (unsigned int)(pt - s) : (unsigned int)strlen(s);

        if (l == 0 || l > 63 || o + l + 6 > sizeof(q)) return -2;
        q[o++] = (unsigned char)l;
        memcpy(q + o, s, l); o += l;
        s += l;
        if (*s == '.') s++;
    }
    q[o++] = 0;
    q[o++] = 0; q[o++] = 1;                     /* A */
    q[o++] = 0; q[o++] = 1;                     /* IN */

    fd = socket(S_AF_INET, S_SOCK_DGRAM, 0);
    if (fd < 0) return -11;
    tv.tv_sec = 2; tv.tv_usec = 0;
    setsockopt(fd, S_SOL_SOCKET, 20, &tv, sizeof(tv));
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = S_AF_INET;
    srv.sin_port   = htons(53);
    memcpy(&srv.sin_addr.s_addr, cfg.dns, 4);

    for (giro = 0; giro < 3 && esito == -2; giro++) {
        unsigned int k, dom, ris, p;

        if (sendto(fd, q, o, 0, (struct sockaddr *)&srv, sizeof(srv)) < 0) { esito = -3; continue; }
        n = (int)recvfrom(fd, buf, sizeof(buf), 0, NULL, NULL);
        if (n < 12) { esito = -2; if (n < 0) esito = -3; continue; }   /* EAI_AGAIN */
        if (((unsigned int)buf[0] << 8 | buf[1]) != (id & 0xFFFFu)) continue;   /* non e' la nostra */
        if ((buf[3] & 0x0F) == 3) { esito = -2; break; }                 /* NXDOMAIN */
        if ((buf[3] & 0x0F) != 0) { esito = -4; break; }
        dom = (unsigned int)buf[4] << 8 | buf[5];
        ris = (unsigned int)buf[6] << 8 | buf[7];
        p = 12;
        for (k = 0; k < dom && p < (unsigned int)n; k++) {        /* salta le domande */
            while (p < (unsigned int)n && buf[p] && (buf[p] & 0xC0) != 0xC0) p += buf[p] + 1u;
            p += (p < (unsigned int)n && (buf[p] & 0xC0) == 0xC0) ? 2u : 1u;
            p += 4;
        }
        for (k = 0; k < ris && p + 10 < (unsigned int)n; k++) {    /* le risposte */
            unsigned int tipo, lung;

            while (p < (unsigned int)n && buf[p] && (buf[p] & 0xC0) != 0xC0) p += buf[p] + 1u;
            p += (p < (unsigned int)n && (buf[p] & 0xC0) == 0xC0) ? 2u : 1u;
            if (p + 10 > (unsigned int)n) break;
            tipo = (unsigned int)buf[p] << 8 | buf[p + 1];
            lung = (unsigned int)buf[p + 8] << 8 | buf[p + 9];
            p += 10;
            if (tipo == 1 && lung == 4 && p + 4 <= (unsigned int)n) {
                memcpy(ip, buf + p, 4);
                esito = 0;
                break;
            }
            p += lung;                          /* un CNAME: si va avanti */
        }
    }
    close(fd);
    return esito;
}

static int servizio_porta(const char *s, int solo_numeri)
{
    static const struct { const char *nome; int porta; } noti[] = {
        { "http", 80 }, { "https", 443 }, { "ftp", 21 }, { "ssh", 22 },
        { "telnet", 23 }, { "domain", 53 }, { "smtp", 25 }, { "pop3", 110 },
        { "imap", 143 }, { "ntp", 123 },
    };
    unsigned int i;
    char        *fine;
    long         v;

    if (!s || !*s) return 0;
    v = strtol(s, &fine, 10);
    if (*fine == '\0') return (v >= 0 && v <= 65535) ? (int)v : -1;
    if (solo_numeri) return -1;
    for (i = 0; i < sizeof(noti) / sizeof(noti[0]); i++)
        if (strcmp(s, noti[i].nome) == 0) return noti[i].porta;
    return -1;
}

void freeaddrinfo(struct addrinfo *ai);

int getaddrinfo(const char *nodo, const char *servizio,
                const struct addrinfo *consigli, struct addrinfo **ris)
{
    unsigned char    ip[4] = { 0, 0, 0, 0 };
    int              flag = consigli ? consigli->ai_flags : 0;
    int              fam  = consigli ? consigli->ai_family : 0;
    int              tipo = consigli ? consigli->ai_socktype : 0;
    int              porta, t;
    struct addrinfo *testa = NULL, **coda = &testa;

    if (!ris) return -1;
    *ris = NULL;
    if (!nodo && !servizio) return -2;
    if (fam != 0 && fam != S_AF_INET) return -6;                         /* EAI_FAMILY */
    if (tipo != 0 && tipo != S_SOCK_STREAM && tipo != S_SOCK_DGRAM) return -7;
    porta = servizio_porta(servizio, flag & 0x400);
    if (porta < 0) return -8;                                            /* EAI_SERVICE */

    if (!nodo) {
        if (!(flag & 0x01)) { ip[0] = 127; ip[3] = 1; }                  /* non passivo */
    } else if (inet_pton(S_AF_INET, nodo, ip) != 1) {
        if (flag & 0x04) return -2;                                      /* NUMERICHOST */
        if (strcmp(nodo, "localhost") == 0) { ip[0] = 127; ip[3] = 1; }
        else {
            int r = dns_chiedi(nodo, ip);

            if (r != 0) return r;
        }
    }

    for (t = S_SOCK_STREAM; t <= S_SOCK_DGRAM; t++) {
        struct addrinfo    *a;
        struct sockaddr_in *s;
        size_t              ln = (flag & 0x02) && nodo ? strlen(nodo) + 1 : 0;

        if (tipo && tipo != t) continue;
        a = (struct addrinfo *)malloc(sizeof(*a) + sizeof(*s) + ln);
        if (!a) { freeaddrinfo(testa); return -10; }                     /* EAI_MEMORY */
        memset(a, 0, sizeof(*a) + sizeof(*s));
        s = (struct sockaddr_in *)(a + 1);
        s->sin_family = S_AF_INET;
        s->sin_port   = htons((unsigned short)porta);
        memcpy(&s->sin_addr.s_addr, ip, 4);
        a->ai_family   = S_AF_INET;
        a->ai_socktype = t;
        a->ai_protocol = t == S_SOCK_STREAM ? S_IPPROTO_TCP : S_IPPROTO_UDP;
        a->ai_addrlen  = sizeof(*s);
        a->ai_addr     = (struct sockaddr *)s;
        if (ln) { a->ai_canonname = (char *)(s + 1); memcpy(a->ai_canonname, nodo, ln); }
        *coda = a;
        coda  = &a->ai_next;
    }
    *ris = testa;
    return 0;
}

void freeaddrinfo(struct addrinfo *ai)
{
    while (ai) {
        struct addrinfo *dopo = ai->ai_next;

        free(ai);
        ai = dopo;
    }
}

const char *gai_strerror(int e)
{
    switch (e) {
    case 0:   return "nessun errore";
    case -2:  return "nome sconosciuto";
    case -3:  return "il DNS non risponde, riprovare";
    case -4:  return "errore del DNS";
    case -6:  return "famiglia di indirizzi non supportata";
    case -7:  return "tipo di socket non supportato";
    case -8:  return "servizio sconosciuto";
    case -10: return "memoria esaurita";
    case -11: return "errore di sistema (vedi errno)";
    default:  return "errore di risoluzione";
    }
}

int getnameinfo(const struct sockaddr *a, unsigned int len, char *host,
                unsigned int hlen, char *serv, unsigned int slen, int flag)
{
    unsigned char ip[4];
    unsigned int  porta;

    (void)flag;       /* solo numerico: non c'e' la ricerca inversa */
    if (indirizzo_in(a, len, ip, &porta) < 0) return -6;
    if (host && hlen && !inet_ntop(S_AF_INET, ip, host, hlen)) return -11;
    if (serv && slen) snprintf(serv, slen, "%u", porta);
    return 0;
}

/* Nessuna ricerca inversa: il nome e' il numero (vedi libc.h). */
struct hostent *gethostbyaddr(const void *ind, unsigned int len, int famiglia)
{
    static unsigned char  ip[4];
    static char          *lista[2], *alias[1];
    static char           nome[16];
    static struct hostent h;
    extern int            h_errno;

    if (!ind || len != 4 || famiglia != S_AF_INET) { h_errno = 1; return NULL; }
    memcpy(ip, ind, 4);
    inet_ntop(S_AF_INET, ip, nome, sizeof(nome));
    lista[0] = (char *)ip; lista[1] = NULL; alias[0] = NULL;
    h.h_name = nome; h.h_aliases = alias; h.h_addrtype = S_AF_INET;
    h.h_length = 4; h.h_addr_list = lista;
    return &h;
}

struct hostent *gethostbyname(const char *nome)
{
    static unsigned char  ip[4];
    static char          *lista[2];
    static char           nome_c[256];
    static struct hostent h;
    static char          *alias[1];

    if (!nome) return NULL;
    if (inet_pton(S_AF_INET, nome, ip) != 1) {
        if (strcmp(nome, "localhost") == 0) { ip[0] = 127; ip[1] = 0; ip[2] = 0; ip[3] = 1; }
        else if (dns_chiedi(nome, ip) != 0) return NULL;
    }
    strncpy(nome_c, nome, sizeof(nome_c) - 1);
    lista[0] = (char *)ip; lista[1] = NULL; alias[0] = NULL;
    h.h_name = nome_c; h.h_aliases = alias; h.h_addrtype = S_AF_INET;
    h.h_length = 4; h.h_addr_list = lista;
    return &h;
}

#endif /* !EXOS_LIBC_SO: i socket */

#ifndef EXOS_LIBC_SO
#include "libc_avvio.c"
#endif

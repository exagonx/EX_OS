/* =============================================================================
 * lib/include/libc.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef LIBC_H
#define LIBC_H

/* =============================================================================
 * Tipi fondamentali — li dichiara il COMPILATORE, non noi
 *
 * <stddef.h> non appartiene alla libreria: lo fornisce il compilatore, ed
 * e' disponibile anche in modalita' freestanding — cioe' anche qui, dove
 * sotto non c'e' nessun sistema. Da li' vengono size_t, ptrdiff_t e NULL.
 *
 * Scriverli a mano ha funzionato finche' l'unico compilatore era il gcc
 * di sistema con -m32, dove size_t e' davvero `unsigned int`. Il
 * bersaglio i386-exos usa `long unsigned int`: stessa larghezza, tipo
 * diverso, e qualunque sorgente che includa <stddef.h> accanto a questo
 * header smette di compilare —
 *
 *     error: conflicting types for 'size_t'; have 'long unsigned int'
 *     note:  previous declaration of 'size_t' with type 'unsigned int'
 *
 * cioe' quasi tutto il codice di terzi, a cominciare da quello di un
 * compilatore da portare dentro EX-OS. Chiedere il tipo al compilatore
 * toglie di mezzo la possibilita' stessa del disaccordo, qualunque sia il
 * compilatore.
 * ============================================================================= */
#include <stddef.h>

/* Questi tre in <stddef.h> non ci sono, ma valgono le stesse ragioni:
 * ricavarli dalle macro predefinite li fa COINCIDERE con quelli di
 * <stdint.h> invece di contraddirli. */
typedef __PTRDIFF_TYPE__    ssize_t;
typedef __INTPTR_TYPE__     intptr_t;
typedef __UINTPTR_TYPE__    uintptr_t;

/* Stringa */
size_t  strlen(const char *s);
char   *strcpy(char *dst, const char *src);
char   *strncpy(char *dst, const char *src, size_t n);
int     strcmp(const char *a, const char *b);
int     strncmp(const char *a, const char *b, size_t n);
char   *strcat(char *dst, const char *src);
char   *strchr(const char *s, int c);
char   *strrchr(const char *s, int c);

/* Memoria */
void   *memset(void *dst, int c, size_t n);
void   *memcpy(void *dst, const void *src, size_t n);
void   *memmove(void *dst, const void *src, size_t n);
int     memcmp(const void *a, const void *b, size_t n);

char   *strncat(char *dst, const char *src, size_t n);
char   *strstr(const char *fieno, const char *ago);
char   *strdup(const char *s);
char   *strtok(char *s, const char *sep);
size_t  strspn(const char *s, const char *accetta);
size_t  strcspn(const char *s, const char *rifiuta);
void   *memchr(const void *s, int c, size_t n);

/* =============================================================================
 * errno — impostato IN PIU', non al posto del valore di ritorno
 *
 * Le funzioni di EX-OS continuano a ritornare l'errore negativo (-2 =
 * ENOENT, -30 = EROFS...): `< 0` resta il test giusto e il codice preciso
 * resta in mano al chiamante. errno serve a chi vuole un messaggio senza
 * portarsi dietro il numero — perror() e strerror(errno).
 * ============================================================================= */
extern int  errno;
const char *strerror(int err);
void        perror(const char *msg);

/* =============================================================================
 * Stdio — flussi bufferizzati
 *
 * La struttura FILE e' opaca di proposito: i programmi ne tengono solo il
 * puntatore, e cambiarne i campi non deve costringere a ricompilare chi
 * non li guarda. Vedi lib/libc.c per la politica di buffering, che NON e'
 * quella di Unix e ha una ragione precisa (il prompt della shell e gfedit).
 * ============================================================================= */
typedef struct _FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE   *fopen(const char *path, const char *modo);
FILE   *fdopen(int fd, const char *modo);
int     fclose(FILE *f);
int     fflush(FILE *f);            /* NULL = tutti i flussi */
size_t  fread(void *ptr, size_t dim, size_t n, FILE *f);
size_t  fwrite(const void *ptr, size_t dim, size_t n, FILE *f);
int     fgetc(FILE *f);
int     getc(FILE *f);
int     fputc(int c, FILE *f);
int     putc(int c, FILE *f);
int     ungetc(int c, FILE *f);
char   *fgets(char *buf, int max, FILE *f);
int     fputs(const char *s, FILE *f);
long    ftell(FILE *f);
int     fseek(FILE *f, long off, int whence);
void    rewind(FILE *f);
int     feof(FILE *f);
int     ferror(FILE *f);
void    clearerr(FILE *f);
int     fileno(FILE *f);

int     putchar(int c);
int     puts(const char *s);
int     getchar(void);
/* NON e' la gets() del C: prende la dimensione del buffer e toglie il
 * fine riga. Ritorna NULL sulla riga vuota. */
char   *gets(char *buf, int max);

/* Lettura formattata. C'e' solo la versione su stringa: vedi lib/libc.c
 * per il perche' fscanf/scanf non ci sono. Ritorna il numero di
 * conversioni riuscite, 0 se la prima non e' andata, -1 se l'ingresso era
 * gia' finito — tre casi da distinguere tutti. */
int     sscanf(const char *s, const char *fmt, ...);
int     vsscanf(const char *s, const char *fmt, __builtin_va_list args);

/* Corpo di assert(): la macro sta in <assert.h>, che deve espanderla li'
 * per catturare file, riga e testo della condizione. */
void    _assert_fallita(const char *cond, const char *file, int riga)
    __attribute__((noreturn));

int     printf(const char *fmt, ...);
int     fprintf(FILE *f, const char *fmt, ...);
int     sprintf(char *buf, const char *fmt, ...);
int     snprintf(char *buf, size_t dim, const char *fmt, ...);
int     vprintf(const char *fmt, __builtin_va_list args);
int     vfprintf(FILE *f, const char *fmt, __builtin_va_list args);
int     vsprintf(char *buf, const char *fmt, __builtin_va_list args);
int     vsnprintf(char *buf, size_t dim, const char *fmt, __builtin_va_list args);

/* Stdlib */
int     atoi(const char *s);
long    atol(const char *s);
long    strtol(const char *s, char **fine, int base);
unsigned long strtoul(const char *s, char **fine, int base);
long long strtoll(const char *s, char **fine, int base);
unsigned long long strtoull(const char *s, char **fine, int base);
int     abs(int v);
long    labs(long v);

/* =============================================================================
 * Virgola mobile
 *
 * Un compilatore ospitato su EX-OS deve leggere i letterali numerici dei
 * sorgenti che compila, e una strtod mancante non darebbe un errore ma una
 * costante sbagliata. Da qui anche la FPU inizializzata nel kernel (vedi
 * kernel/include/fpu.h): senza salvataggio dello stato x87 nel cambio di
 * contesto, due processi che fanno conti si calpestano i registri.
 *
 * Precisione dichiarata: fino a 15-16 cifre significative il risultato
 * coincide con quello di una libc seria, oltre l'ultimo bit puo'
 * differire. NON e' correttamente arrotondata a mezzo ULP, e strtold non
 * e' piu' precisa di strtod — passa comunque per un double. Niente
 * esadecimali del C99 (0x1p3) ne' "inf"/"nan". Il perche' sta in
 * lib/libc.c.
 * ============================================================================= */
double  strtod(const char *s, char **fine);
float   strtof(const char *s, char **fine);
long double strtold(const char *s, char **fine);
double  ldexp(double x, int e);

/* Le "variabili d'ambiente" di EX-OS sono la sezione [env] di
 * /boot/kernel.cfg: getenv() e' la facciata POSIX su getconf() (vedi piu'
 * avanti), e il puntatore che ritorna vale FINO ALLA CHIAMATA SUCCESSIVA.
 * Nel codice nuovo si preferisca getconf(). */
char   *getenv(const char *nome);
void    exit(int code);
void    abort(void);
void   *malloc(size_t size);
void    free(void *ptr);
void   *calloc(size_t nmemb, size_t size);
void   *realloc(void *ptr, size_t size);
void    qsort(void *base, size_t n, size_t dim,
              int (*cmp)(const void *, const void *));
void   *bsearch(const void *chiave, const void *base, size_t n, size_t dim,
                int (*cmp)(const void *, const void *));

/* Ctype */
int     isdigit(int c);
int     isxdigit(int c);
int     islower(int c);
int     isupper(int c);
int     isalpha(int c);
int     isalnum(int c);
int     isspace(int c);
int     isprint(int c);
int     isgraph(int c);
int     ispunct(int c);
int     iscntrl(int c);
int     tolower(int c);
int     toupper(int c);

/* =============================================================================
 * setjmp / longjmp
 *
 * jmp_buf tiene ebx, esi, edi, ebp, esp e l'indirizzo di ritorno: sei
 * parole, e vanno dichiarate con QUESTO tipo — un array di interi scelto a
 * occhio che risultasse piu' corto verrebbe scritto oltre la fine.
 *
 * Non salvano lo stato della FPU ne' una maschera di segnali: EX-OS non ha
 * segnali, e la FPU non e' conservata nel cambio di contesto.
 * ============================================================================= */
typedef unsigned int jmp_buf[6];

int     setjmp(jmp_buf env);
void    longjmp(jmp_buf env, int val) __attribute__((noreturn));

/* Syscall wrapper */
/* Variadica come su POSIX: il terzo argomento sono i permessi di
 * creazione, che EX-OS IGNORA (non ha proprietari ne' permessi sui file).
 * Sta nella firma perche' senza di lui non compila una riga come
 * open(path, O_CREAT|O_WRONLY, 0644), che e' come la scrivono tutti. */
int     open(const char *path, int flags, ...);
int     close(int fd);
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int     getpid(void);
int     chdir(const char *path);

/* Sostituiscono il programma in esecuzione: se vanno a buon fine NON
 * ritornano, quindi ogni ritorno e' un errore. execvp cerca un nome senza
 * barre in /bin e poi /usr/bin. L'ambiente non si eredita: si legge con
 * getconf(). */
int     execv (const char *path, char *const argv[]);
int     execvp(const char *file, char *const argv[]);

/* =============================================================================
 * Controllo del terminale
 *
 * Costanti DUPLICATE A MANO da drivers/tty/tty.h (stessa convenzione di
 * DirEntry e MemInfo): quell'header include kernel.h e non è
 * compilabile in ring3.
 *
 * TTY_IOCTL_SETRAW riguarda l'USCITA, non l'ingresso: spegne lo
 * specchio seriale, che a 38400 baud costa un'attesa per carattere e
 * rende impraticabile ridisegnare lo schermo. Per ricevere i tasti uno
 * per uno invece che una riga alla volta si parla direttamente al
 * servizio "kbd" via IPC (KBD_MSG_SETMODE, vedi
 * drivers/kbd/kbd_proto.h): la line discipline vive lì, non qui.
 *
 * Chi accende SETRAW deve rimettere SETCOOKED prima di uscire.
 * ============================================================================= */
#define TTY_IOCTL_GETSIZE    0x01
#define TTY_IOCTL_SETRAW     0x02
#define TTY_IOCTL_SETCOOKED  0x03
#define TTY_IOCTL_CLEAR      0x04
#define TTY_IOCTL_SETCOLOR   0x05

typedef struct {
    unsigned short rows;
    unsigned short cols;
    unsigned short xpixel;
    unsigned short ypixel;
} TtyWinSize;

int     ioctl(int fd, unsigned int request, void *arg);
int     tty_getsize(TtyWinSize *ws);
int     tty_raw(int on);       /* 1 = raw, 0 = cooked */
int     tty_clear(void);

/* Crea una directory. Nessun parametro 'mode': FAT12 non ha permessi.
 * Ritorna 0, o un errno negativo — in particolare -17 (EEXIST) se il nome
 * e' gia' occupato e -38 (ENOSYS) se il percorso richiederebbe piu' di un
 * livello di annidamento, che questo FAT12 non risolve. */
int     mkdir(const char *path);

/* Cancella una directory VUOTA. Ritorna 0, o un errno negativo:
 * -2 non trovata, -20 non e' una directory, -39 non vuota. */
int     rmdir(const char *path);

/* Cancella un file. Ritorna 0, -2 se non esiste, -21 se e' una directory
 * (per quelle serve rmdir). Non espande caratteri jolly: accetta un nome
 * preciso. */
int     unlink(const char *path);
int     remove(const char *path);   /* unlink() col nome del C standard */
char   *getcwd(char *buf, size_t size);
void    sched_yield(void);
void    usleep(unsigned int us);
void    sleep(unsigned int sec);

/* =============================================================================
 * Data e ora dall'orologio CMOS della macchina.
 *
 * DUPLICATA A MANO da kernel/include/rtc.h e lib/libc.c (stessa
 * convenzione di DirEntry e MemInfo): le tre copie devono restare
 * identiche.
 *
 * Diversa da uptime_ms(), che misura DURATE e non sa che ora sia. Qui
 * c'e' l'ora del giorno vera, quella che l'orologio a batteria continua
 * a contare anche a macchina spenta.
 *
 * time_now() ritorna 0, oppure -19 (ENODEV) se l'orologio non risponde
 * o consegna una data impossibile — succede su hardware vecchio con la
 * batteria del CMOS scarica. In quel caso *t non viene toccata: un
 * programma deve poter dire "ora ignota" invece di mostrare il 47 del
 * mese 93.
 * ============================================================================= */
typedef struct {
    unsigned int anno;      /* 4 cifre, es. 2026 */
    unsigned int mese;      /* 1-12 */
    unsigned int giorno;    /* 1-31 */
    unsigned int ora;       /* 0-23 */
    unsigned int minuto;    /* 0-59 */
    unsigned int secondo;   /* 0-59 */
} RtcTime;

int time_now(RtcTime *t);

/* =============================================================================
 * La stessa ora, nella forma del C standard
 *
 * DUPLICATE A MANO in lib/libc.c e ripetute in lib/include/time.h.
 *
 * ⚠️ NESSUN FUSO ORARIO. L'orologio CMOS di EX-OS e' ora locale e il
 * sistema non sa dove si trova: localtime() e gmtime() fanno la stessa
 * identica cosa, e i secondi di time() sono "secondi dal 1970 letti su un
 * orologio locale". Buoni per misurare intervalli e datare un file, NON
 * confrontabili con l'istante di un'altra macchina.
 *
 * time_t e' `long`: a 32 bit con segno arriva al 2038.
 * ============================================================================= */
typedef long time_t;

struct tm {
    int tm_sec;     /* 0..60 */
    int tm_min;     /* 0..59 */
    int tm_hour;    /* 0..23 */
    int tm_mday;    /* 1..31 */
    int tm_mon;     /* 0..11 — gennaio e' ZERO */
    int tm_year;    /* anni dal 1900 */
    int tm_wday;    /* 0..6, domenica = 0 */
    int tm_yday;    /* 0..365 */
    int tm_isdst;   /* sempre 0 */
};

struct timeval {
    long tv_sec;
    long tv_usec;   /* risoluzione vera: 10 ms, il tick del PIT */
};

/* Ritorna (time_t)-1 se l'orologio non risponde: zero significherebbe il
 * 1970, cioe' una data sbagliata spacciata per buona. */
time_t     time(time_t *t);
struct tm *gmtime(const time_t *t);      /* risultato in una struttura STATICA */
struct tm *localtime(const time_t *t);   /* identica a gmtime: vedi sopra */
int        gettimeofday(struct timeval *tv, void *fuso);

/* =============================================================================
 * Console virtuali
 *
 * Quattro schermi indipendenti, uno solo visibile per volta. Ogni
 * processo ne ha una — la eredita dal padre — e write(1, ...) finisce
 * sempre lì: un programma che gira su una console nascosta continua a
 * disegnare nel proprio buffer e si ritrova lo schermo intatto quando
 * l'utente ci torna sopra con Alt+F1..F4.
 *
 * La commutazione la fa il DRIVER TASTIERA quando riconosce Alt+Fn: un
 * normale programma non ha motivo di chiamare console_switch(), e
 * console_write() serve solo a chi deve scrivere su una console che non
 * e' la propria — cioe' al driver tastiera, per l'eco dei tasti.
 *
 * Struttura DUPLICATA A MANO da kernel/include/syscall.h e lib/libc.c.
 * ============================================================================= */
typedef struct {
    unsigned int totale;    /* quante console esistono */
    unsigned int mia;       /* quella del processo chiamante */
    unsigned int visibile;  /* quella attualmente a video */

    /* PID in primo piano sulla console del chiamante, 0 se nessuno.
     *
     * Serve a chi prende la tastiera parlando DIRETTAMENTE al servizio
     * 'kbd' via IPC — la modalita' raw di /bin/gfedit — perche' quella
     * strada non passa da sys_read e la guardia del kernel non la vede.
     * Un programma del genere deve controllare da se' di essere in primo
     * piano: se lo lanciassero con '&' ruberebbe i tasti alla shell, che
     * resterebbe bloccata in attesa di una riga che non arriverebbe mai. */
    unsigned int fg;
} ConsoleInfo;

int console_info(ConsoleInfo *ci);
int console_switch(unsigned int n);
int console_write(unsigned int n, const void *buf, unsigned int len);

/* Millisecondi dall'avvio. Serve a dare una scadenza REALE a un'attesa
 * senza contare iterazioni (che dipende dalla velocita' della CPU) e senza
 * dormire a passi (che non puo' scendere sotto il tick da 10 ms).
 *
 * Avanza a scatti di 10 (PIT a 100Hz) e torna a zero dopo ~24,8 giorni:
 * confrontare sempre DIFFERENZE fra due letture in aritmetica senza segno,
 *   unsigned inizio = uptime_ms();
 *   ... while (uptime_ms() - inizio < timeout_ms) ...
 * che attraversa il wrap correttamente. Mai confrontare valori assoluti. */
unsigned int uptime_ms(void);

/* =============================================================================
 * Stato della memoria fisica, per fascia. Valori in KB; "usata" = totale -
 * libera. STRUTTURA DUPLICATA A MANO da kernel/include/syscall.h (stessa
 * convenzione di DirEntry): deve restare identica, e meminfo() fallisce
 * con -EINVAL se le due copie divergono di dimensione.
 *
 * Le fasce sono quelle dell'architettura PC:
 *   convenzionale  < 640 KB
 *   superiore/UMA  640 KB - 1 MB (BIOS, video, ROM)
 *   estesa (XMS)   >= 1 MB, dove vive tutto il sistema
 *
 * I campi ems_* (memoria ESPANSA) valgono sempre 0, e non e' una lacuna:
 * l'espansa e' un meccanismo a banchi commutati per superare il limite di
 * 1 MB del modo reale su 8086/286. Su un 386+ in modo protetto con
 * paginazione quel limite non esiste e tutta la RAM oltre 1 MB e'
 * direttamente indirizzabile come estesa. Vedi il commento esteso in
 * kernel/include/syscall.h.
 * ============================================================================= */
typedef struct {
    unsigned int conv_total_kb, conv_free_kb;
    unsigned int uma_total_kb,  uma_free_kb;
    unsigned int ext_total_kb,  ext_free_kb;
    unsigned int ems_total_kb,  ems_free_kb;
    unsigned int total_kb,      free_kb;
    unsigned int page_size;
} MemInfo;

/* Riempie *mi. Ritorna 0, o un valore negativo in caso di errore. */
int meminfo(MemInfo *mi);

/* =============================================================================
 * Descrizione di un processo e dei suoi stack. DUPLICATA A MANO in
 * kernel/include/syscall.h e lib/libc.c: deve restare identica, e
 * procinfo() fallisce con -EINVAL se le copie divergono di dimensione.
 *
 *   ustack_top    indirizzo piu' alto dello stack utente (fisso)
 *   ustack_base   pagina piu' bassa ATTUALMENTE mappata; scende quando lo
 *                 stack cresce su fault. top - base = RAM impegnata ORA
 *   ustack_limit  confine della riserva. top - limit = spazio riservato.
 *                 0 = task kernel, che non ha stack utente
 *   kstack_*      stack kernel: allocato per intero alla creazione e mai
 *                 cresciuto, li' la dimensione e' ancora fissa
 * ============================================================================= */
#define PROCINFO_NAME_MAX   32
#define PROCINFO_MAX_BATCH  16   /* voci per chiamata */

typedef struct {
    unsigned int pid;
    unsigned int ppid;
    unsigned int state;     /* 1=READY 2=RUNNING 3=BLOCKED 4=ZOMBIE 5=SLEEPING */
    unsigned int prio;
    char         name[PROCINFO_NAME_MAX];
    unsigned int ustack_top;
    unsigned int ustack_base;
    unsigned int ustack_limit;
    unsigned int kstack_base;
    unsigned int kstack_top;
} ProcInfo;

/* Riempie fino a max voci partendo dalla start-esima (paginazione come
 * listdir_from). Ritorna quante ne ha scritte, 0 quando sono finite. */
int procinfo(ProcInfo *buf, unsigned int max, unsigned int start);

/* =============================================================================
 * Disco fisico e sua tabella delle partizioni. DUPLICATA A MANO in
 * kernel/include/syscall.h e lib/libc.c: deve restare identica.
 *
 * I 64 bit viaggiano spezzati in _lo/_hi per non dover concordare un
 * allineamento a 8 byte fra kernel e libc attraverso l'ABI.
 *
 * settori_*  quanto il disco dichiara ORA (IDENTIFY DEVICE)
 * nativi_*   capacita' di fabbrica (READ NATIVE MAX ADDRESS)
 * clippato   1 se nativi > settori: c'e' spazio NASCOSTO. E' il caso di un
 *            disco da 64 GB che si presenta come da 32, per una HPA
 *            attiva o per un jumper di limitazione.
 * ============================================================================= */
#define DISKINFO_MAX_PART   16

/* Schema */
#define PT_SCHEMA_NESSUNO   0
#define PT_SCHEMA_MBR       1
#define PT_SCHEMA_GPT       2

/* Anomalie (maschera) — vedi kernel/include/mbr.h per il dettaglio */
#define PT_PROB_FIRMA       0x0001
#define PT_PROB_OLTRE_FINE  0x0002
#define PT_PROB_SOVRAPP     0x0004
#define PT_PROB_CATENA      0x0008
#define PT_PROB_BOOTFLAG    0x0010
#define PT_PROB_GPT         0x0020
#define PT_PROB_VUOTA       0x0040
#define PT_PROB_TROPPE_EXT  0x0080
#define PT_PROB_TRONCATA    0x0100
#define PT_PROB_SETTORE0    0x0200

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

/* Riempie *di per l'unita' idx (0..3). Ritorna 0 anche se lo slot e'
 * vuoto: in quel caso di->presente vale 0. */
int diskinfo(unsigned int idx, DiskInfo *di);

/* Dispositivo a blocchi. `primo`/`settori` sono la FINESTRA: ogni accesso
 * viene tradotto e rifiutato se ne esce. DUPLICATA A MANO in
 * kernel/include/syscall.h e lib/libc.c. */
#define BLKINFO_NOME_MAX    12
typedef struct {
    char         nome[BLKINFO_NOME_MAX];
    unsigned int tipo;          /* 1 floppy, 2 disco intero, 3 partizione */
    unsigned int sola_lettura;
    unsigned int primo_lo, primo_hi;
    unsigned int settori_lo, settori_hi;
} BlkInfo;

int blkinfo(BlkInfo *buf, unsigned int max, unsigned int start);

/* =============================================================================
 * Montaggi
 *
 * `punto` e' un percorso assoluto ("/disk") e NON deve esistere gia': i
 * punti di montaggio sono virtuali e compaiono nell'elenco della root
 * perche' li aggiunge il VFS. Montare su un nome esistente viene
 * rifiutato invece di nasconderlo in silenzio.
 *
 * I montaggi sono in lettura/scrittura salvo MNT_SOLA_LETTURA; su un
 * montaggio in sola lettura ogni tentativo di modifica torna -EROFS.
 * ============================================================================= */
#define MOUNTINFO_PUNTO_MAX 24

typedef struct {
    char         punto[MOUNTINFO_PUNTO_MAX];
    char         dev[BLKINFO_NOME_MAX];
    unsigned int fs;             /* 12, 16, 32 */
    unsigned int sola_lettura;
} MountInfo;

/* flag: MNT_SOLA_LETTURA per montare senza permettere modifiche. */
#define MNT_SOLA_LETTURA 0x0001

int mount(const char *dev, const char *punto, unsigned int flag);
int umount(const char *punto);
int mountinfo(MountInfo *buf, unsigned int max, unsigned int start);

/* =============================================================================
 * Installazione dell'avvio su disco
 *
 * `punto` e' un punto di montaggio GIA' ATTIVO in scrittura ("/disk") che
 * contiene /BOOT/STAGE2.BIN e /BOOT/KERNEL.BIN.
 *
 * Il kernel scrive l'MBR (solo il codice: la tabella delle partizioni non
 * viene toccata) e il settore di avvio della partizione (conservandone il
 * BPB), con dentro la mappa dei settori dei due file.
 *
 * ⚠️ La mappa vale finche' quei file non si spostano: ricopiare il kernel
 * OBBLIGA a rieseguire l'installazione. -29 (ESPIPE) significa che un file
 * e' frammentato e non riassumibile in un intervallo unico.
 * ============================================================================= */
typedef struct {
    unsigned int s2_lba, s2_cnt;
    unsigned int k_lba,  k_cnt;   /* primo intervallo, e settori TOTALI */
    unsigned int k_next;          /* in quanti intervalli e' spezzato */
    unsigned int disco;
    unsigned int voce;
} BootInstallInfo;

int bootinstall(const char *punto, BootInstallInfo *info);

/* =============================================================================
 * Scrittura della tabella delle partizioni
 *
 * Si passano SEMPRE tutti e quattro gli slot delle primarie, anche quelli
 * liberi (tipo = 0): una tabella e' un oggetto unico, e consegnarla una
 * voce alla volta darebbe stati intermedi in cui le partizioni si
 * sovrappongono, che il kernel non potrebbe validare.
 *
 * Il kernel RIFIUTA in blocco — senza scrivere niente — una proposta che
 * abbia una qualunque delle anomalie che `disk` sa segnalare, piu' due
 * casi che riguardano solo la scrittura: un disco GPT, e un'estesa che
 * contiene partizioni logiche e che la proposta sposterebbe o
 * cancellerebbe (le logiche da qui non si toccano).
 *
 * Rifiuta anche se una partizione di quel disco e' MONTATA — compresa la
 * root, se il sistema sta girando da li'.
 *
 * In caso di -22 (EINVAL) il campo `problemi` contiene la maschera
 * PT_PROB_* di cosa non andava: usala per dire all'utente quale controllo
 * non e' passato invece di un generico "rifiutata".
 *
 * Dopo il successo i dispositivi hd<n>p* sono gia' aggiornati: non serve
 * riavviare.
 *
 * ⚠️ Non formatta niente. Le partizioni nuove contengono quello che c'era
 * prima in quei settori.
 * ============================================================================= */
#define PARTWRITE_MAX_VOCI  4

typedef struct {
    unsigned int attiva;                  /* 0x00, oppure 0x80 = avviabile */
    unsigned int tipo;                    /* byte di tipo MBR; 0 = slot libero */
    unsigned int inizio_lo,  inizio_hi;   /* LBA assoluto del primo settore */
    unsigned int settori_lo, settori_hi;  /* lunghezza in settori */
} PartVoce;

typedef struct {
    unsigned int problemi;                  /* USCITA: maschera PT_PROB_* */
    PartVoce     voce[PARTWRITE_MAX_VOCI];  /* voce[i] = slot i+1 */
} PartTabella;

int partwrite(unsigned int disco, PartTabella *tab);

/* =============================================================================
 * Settori grezzi di una partizione
 *
 * Serve a scrivere un filesystem dentro una partizione: le strutture di un
 * volume — settore di avvio, tabelle FAT, directory radice — nessun
 * filesystem montato sa produrle, perche' il filesystem e' proprio cio' che
 * si sta creando.
 *
 * Il kernel accetta SOLO nomi di partizione ("hd0p1"), mai un disco intero
 * ("hd0") ne' il floppy. Non e' una limitazione arbitraria: e' cio' che
 * rende la tabella delle partizioni irraggiungibile da qui. Il settore 0
 * non appartiene a nessuna partizione, quindi non esiste una coppia
 * (nome, lba) che lo raggiunga.
 *
 * Rifiuta anche una partizione MONTATA: sopra c'e' una cache write-back, e
 * scriverci sotto significa che il primo sync ci ricopre i settori vecchi.
 *
 * `lba` e' RELATIVO alla partizione: 0 e' il suo primo settore, non quello
 * del disco. Ogni accesso fuori dalla finestra viene rifiutato.
 *
 * Al massimo BLKIO_MAX_SETT settori per chiamata. Ritornano quanti ne hanno
 * trasferiti, o un errno negativo: -1 (EPERM) non e' una partizione,
 * -16 (EBUSY) e' montata, -2 (ENOENT) non esiste, -22 (EINVAL) fuori
 * finestra o n fuori scala.
 * ============================================================================= */
#define BLKIO_MAX_SETT      64      /* 32 KB per chiamata */

int blkread (const char *dev, unsigned int lba, unsigned int n, void *buf);
int blkwrite(const char *dev, unsigned int lba, unsigned int n, const void *buf);

/* =============================================================================
 * Cambia la dimensione di un file
 *
 * ALLUNGARE non alloca niente: lo spazio in mezzo diventa un BUCO, che si
 * legge come zeri e non occupa spazio sul volume finche' non ci si scrive.
 * E' il modo di creare un file grande senza consumare il disco.
 *
 * ACCORCIARE libera i blocchi in coda. I byte oltre la nuova dimensione
 * sono persi: riallungando si ottengono zeri, non i dati di prima.
 *
 * -38 (ENOSYS) sul floppy: fat12.c non ha un troncamento. -30 (EROFS) su
 * un montaggio in sola lettura, -21 (EISDIR) su una directory.
 * ============================================================================= */
int truncate(const char *path, unsigned int size);




/* Flag open */
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     0x40
#define O_TRUNC     0x200
#define O_APPEND    0x400

/* =============================================================================
 * Posizionamento e informazioni sui file
 *
 * st_attr usa le convenzioni FAT su TUTTI i filesystem, perche' sono
 * quelle che i programmi gia' interpretano: 0x10 directory, 0x01 sola
 * lettura. st_first_clus vale 0 fuori da una FAT — un numero di cluster
 * inventato sarebbe peggio di uno assente.
 * ============================================================================= */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

typedef struct {
    unsigned int    st_size;
    unsigned int    st_first_clus;
    unsigned short  st_attr;
    unsigned short  st_date;
    unsigned short  st_time;
} Stat;

/* Attributi FAT, da usare su Stat.st_attr. Si chiamavano S_ISDIR e
 * S_ISREG fino ad agosto 2026: quei due nomi sono di POSIX e li' operano
 * su st_mode, che e' un'altra cosa. Due macro con lo stesso nome e un
 * campo diverso in ingresso sono un errore che non si vede — compila,
 * gira, risponde sbagliato — quindi qui hanno preso il prefisso del
 * sistema a cui appartengono. */
#define EXOS_ATTR_DIR(attr)     (((attr) & 0x10) != 0)
#define EXOS_ATTR_REG(attr)     (((attr) & 0x10) == 0)
#define EXOS_ATTR_RDONLY(attr)  (((attr) & 0x01) != 0)

/* =============================================================================
 * struct stat — la forma POSIX della stessa informazione
 *
 * Stat (sopra) e' l'ABI della syscall: quello che il filesystem sa, con i
 * nomi di FAT. struct stat e' quello che si aspetta il codice scritto per
 * un sistema POSIX, ed e' cio' che stat() riempie oggi.
 *
 * Non e' una duplicazione: la prima attraversa il confine col kernel e
 * cambiarla vuol dire cambiare il kernel; la seconda e' una comodita' di
 * libreria, e i campi che EX-OS non ha (proprietario, gruppo, numero di
 * collegamenti) valgono zero o uno invece di essere inventati.
 *
 * I permessi in st_mode sono ricostruiti dall'attributo di sola lettura:
 * 0555 se il file e' in sola lettura, 0755 per una directory, 0644
 * altrimenti. EX-OS non ha permessi veri — sono li' perche' un programma
 * che stampa "rw-r--r--" non abbia da lamentarsi.
 * ============================================================================= */
#define S_IFMT      0170000
#define S_IFDIR     0040000
#define S_IFREG     0100000
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)

struct stat {
    unsigned int    st_dev;     /* sempre 0: EX-OS non numera i volumi */
    unsigned int    st_ino;     /* primo cluster/inode, 0 dove non si applica */
    unsigned int    st_mode;    /* tipo | permessi ricostruiti */
    unsigned int    st_nlink;   /* sempre 1 */
    unsigned int    st_uid;     /* sempre 0: non ci sono utenti */
    unsigned int    st_gid;     /* sempre 0 */
    unsigned int    st_size;
    time_t          st_atime;   /* = st_mtime: non si tiene l'ultimo accesso */
    time_t          st_mtime;
    time_t          st_ctime;   /* = st_mtime */
};

void   *sbrk(int incr);      /* cima dell'heap; ritorna la posizione VECCHIA */
long    lseek(int fd, long offset, int whence);

/* stat() riempie la struttura POSIX; statraw() da' i campi grezzi del
 * filesystem (attributi FAT, primo cluster, data e ora codificate) a chi
 * ne ha bisogno davvero — mkfs, fdisk, un ls che mostri gli attributi. */
int     stat(const char *path, struct stat *st);
int     statraw(const char *path, Stat *st);

/* fstat() lavora su un file APERTO. Non c'e' una syscall per farlo: la
 * dimensione si ottiene con due lseek (vedi fsize), quindi tipo e data
 * NON sono quelli veri — st_mode dice "file regolare" e i tempi valgono
 * zero. Chi ha il percorso usi stat(), che sa tutto. */
int     fstat(int fd, struct stat *st);
/* Dimensione di un file APERTO, lasciando la posizione dov'era.
 * Non c'e' una syscall fstat: si compone di due lseek. */
long    fsize(int fd);

/* Voce di directory (vedi SYS_READDIR in kernel/include/syscall.h —
 * struct identica, deve restare sincronizzata a mano: stessa convenzione
 * usata altrove nel progetto, es. i numeri di syscall duplicati in
 * bin/sh/shell.c invece di includere gli header del kernel). */
/* 256 = 255 caratteri + NUL, il massimo di ext2. Su FAT i nomi restano
 * 8.3 e ne usano 13: il campo e' largo per il filesystem piu' generoso. */
#define DIRENT_NAME_MAX 256

/* Voci che il kernel consegna al MASSIMO per chiamata. Chiederne di piu'
 * non e' un errore ma non serve: la chiamata ne restituisce comunque
 * questo numero, e chi usa l'idioma "ne ho ricevute meno di quante ne ho
 * chieste, quindi sono finite" si fermerebbe a meta' directory. Usare
 * questa costante come dimensione del proprio blocco rende quell'idioma
 * sempre vero. */
#define LISTDIR_MAX_BATCH 16
typedef struct {
    char           name[DIRENT_NAME_MAX];
    unsigned int   size;
    unsigned char  is_dir;
} DirEntry;

/* listdir — elenca una directory in un colpo solo (non e' la classica
 * tripletta POSIX opendir/readdir/closedir, qui non serve un handle).
 * path: NULL/""/"/" per la root, "/NOME" per una subdirectory.
 * Ritorna il numero di entry scritte in buf (>=0), o <0 in caso di errore. */
int     listdir(const char *path, DirEntry *buf, int max);

/* Come listdir, ma parte dalla 'start'-esima voce. Serve per percorrere
 * una directory piu' grande del buffer: il kernel limita comunque quante
 * voci restituisce per chiamata (vedi READDIR_MAX_BATCH), e senza questa
 * il resto della directory era invisibile SENZA alcuna segnalazione.
 *
 * Uso tipico:
 *   int start = 0, n;
 *   while ((n = listdir_from(dir, buf, MAX, start)) > 0) {
 *       ... elabora le n voci ...
 *       start += n;
 *       if (n < MAX) break;      // ultima pagina
 *   }
 */
int     listdir_from(const char *path, DirEntry *buf, int max, int start);

/* Programmi che usano la libc implementano main() invece di _start().
 * La libc fornisce _start (naked, weak): legge argc/argv dallo stack
 * iniziale costruito da sys_spawn e chiama _libc_start → main().
 * I programmi che definiscono _start() propria (es. hello, shell)
 * la sovrascrivono al link senza includere _libc_start.
 *
 * main() NON e' dichiarata qui, ed e' voluto: un prototipo
 * `int main(int, char **)` in un header incluso da tutti rende ERRORE
 * l'altra forma ammessa dallo standard, `int main(void)` — quella con cui
 * e' scritto quasi ogni programma di esempio e di prova. Chi ne ha
 * bisogno e' lib/libc.c, che se la dichiara da se': i due argomenti in
 * piu' che passa a un main(void) la convenzione cdecl li ignora. */
void    _libc_start(int argc, char **argv);  /* richiamata da _start naked */

/* =============================================================================
 * IPC — comunicazione kernel-mediata tra task ring3.
 *
 * Un driver o servizio registra un nome all'avvio (ipc_register), poi
 * resta in attesa di messaggi con ipc_recv. Un client cerca il PID del
 * servizio con ipc_lookup e gli invia richieste con ipc_send — senza mai
 * conoscere direttamente la memoria del destinatario: il kernel copia i
 * dati attraverso un buffer intermedio (vedi kernel/ipc/ipc.c).
 * ============================================================================= */
#define IPC_MSG_MAX_DATA    512
#define IPC_NAME_LEN        16

typedef struct {
    unsigned int  sender_pid;
    unsigned int  type;
    unsigned int  len;
    unsigned char data[IPC_MSG_MAX_DATA];
} IpcMessage;

/* Invia un messaggio a dest_pid. data può essere NULL se len=0.
 * Ritorna 0 su successo, <0 su errore (-ESRCH se dest_pid non esiste,
 * -EBUSY se la mailbox del destinatario resta piena troppo a lungo). */
int     ipc_send(unsigned int dest_pid, unsigned int type,
                  const void *data, unsigned int len);

/* Riceve il prossimo messaggio nella propria mailbox, bloccando se
 * vuota. out_meta (opzionale) riceve sender_pid/type/len; buf riceve il
 * payload fino a buf_len byte. Ritorna 0 su successo. */
int     ipc_recv(IpcMessage *out_meta, void *buf, unsigned int buf_len);

/* =============================================================================
 * Come ipc_recv, ma rinuncia dopo timeout_ms e ritorna -110 (ETIMEDOUT).
 * timeout_ms == 0 = attesa senza scadenza, cioè esattamente ipc_recv.
 *
 * È la primitiva che permette a un programma interattivo di fare
 * qualcosa MENTRE aspetta. Senza, chi attende un tasto resta fermo
 * finché non lo si preme: nessun orologio che avanza, nessun
 * autosalvataggio a tempo, nessun aggiornamento di stato. Il ciclo
 * tipico e' quello di qualunque interfaccia —
 *
 *   for (;;) {
 *       disegna();
 *       if (ipc_recv_timeout(&meta, buf, sizeof buf, 500) == 0) gestisci(buf);
 *       // scaduta: si torna a disegnare, e l'orologio e' avanzato
 *   }
 *
 * La scadenza e' arrotondata per eccesso al tick del PIT (10 ms): non
 * ha senso chiederne una piu' fine di cosi'.
 * ============================================================================= */
int     ipc_recv_timeout(IpcMessage *out_meta, void *buf, unsigned int buf_len,
                         unsigned int timeout_ms);

/* Registra il chiamante come fornitore del servizio 'name' (es. "tty",
 * "floppy"). Ritorna 0 su successo, <0 se il nome è già in uso. */
int     ipc_register(const char *name);

/* Cerca il PID del processo che fornisce 'name'.
 * Ritorna il PID (>0) su successo, <0 se non trovato. */
int     ipc_lookup(const char *name);

/* =============================================================================
 * Hardware kernel-mediato — accesso a IRQ e porte I/O per driver ring3.
 *
 * Un driver ring3 non può eseguire in/out o gestire direttamente un IDT
 * gate (istruzioni privilegiate, CPL=3 le rifiuta con #GP). Il kernel
 * media entrambi gli accessi: irq_bind() fa arrivare gli interrupt
 * hardware come messaggi IPC (vedi IpcMessage — sender_pid=0 e
 * type=0xFFFFFFFF segnalano una notifica IRQ, il payload è l'uint32_t
 * col numero IRQ); ioport_bind() dichiara quali porte il processo può
 * toccare, verificate ad ogni ioport_in/ioport_out dal kernel.
 * ============================================================================= */
#define IPC_SENDER_KERNEL    0
#define IPC_TYPE_IRQ_NOTIFY  0xFFFFFFFFu

/* Rivendica un IRQ hardware (0-15). Ritorna 0 su successo, -EEXIST se
 * già rivendicato da un altro processo vivo. */
int     irq_bind(unsigned int irq);

/* Richiede accesso a un range di porte I/O [base, base+count).
 * Sovrascrive un'eventuale bind precedente. Ritorna 0 su successo. */
int     ioport_bind(unsigned int base, unsigned int count);

/* Legge/scrive un byte su una porta nel proprio range. Ritorna -EPERM
 * se la porta non è stata dichiarata con ioport_bind(). */
int     ioport_in(unsigned int port);
int     ioport_out(unsigned int port, unsigned int value);

/* =============================================================================
 * Configurazione e identità del sistema
 * ============================================================================= */

/* Legge un'opzione di /boot/kernel.cfg: sia le variabili di [env]
 * (PATH, HOME, TERM, OSNAME, ...) sia le opzioni scalari fuori da [env]
 * come 'verboseboot'. Copia il valore in buf.
 * Ritorna la lunghezza del valore (>=0), <0 se assente o buffer piccolo.
 *
 * Non si chiama getenv() di proposito: la firma è diversa da quella
 * standard (nessun puntatore a memoria statica del sistema, il chiamante
 * fornisce il buffer) e un nome identico con semantica diversa sarebbe
 * una trappola. */
int     getconf(const char *key, char *buf, size_t size);

/* Copia l'identità del sistema — nome, copyright, licenza, versione — dalla
 * variabile globale del kernel (g_os_version). Ritorna la lunghezza, <0
 * se il buffer è troppo piccolo (nessuna copia parziale). */
int     osversion(char *buf, size_t size);

/* Comodità: 1 se il boot verboso è attivo, 0 se il sistema deve restare
 * silenzioso. In caso di errore di lettura ritorna 1 — un programma muto
 * per un problema di configurazione è indistinguibile da uno bloccato. */
int     verboseboot(void);

#endif /* LIBC_H */

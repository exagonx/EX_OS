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

/* =============================================================================
 * I nomi POSIX dei tipi — <sys/types.h>
 *
 * Non aggiungono niente: sono gli stessi interi che le funzioni qui sotto
 * usano gia' in chiaro (lseek() prende un `long`, waitpid() ritorna un
 * `int`). Ci sono perche' il codice di terzi li SCRIVE — `off_t pos;`,
 * `pid_t figlio;` — e senza non compila la prima riga, non perche' EX-OS
 * distingua qualcosa in piu'.
 *
 * ! Le larghezze sono quelle che il kernel usa davvero, non quelle che
 * userebbe un sistema piu' grande: off_t e' `long`, cioe' 32 bit con
 * segno, quindi 2 GB per file — lo stesso limite che ha lseek(), che e'
 * la syscall sotto. Dichiararlo a 64 bit non renderebbe piu' grandi i
 * file: renderebbe solo silenziosa la troncatura al confine col kernel.
 * ============================================================================= */
typedef int                 pid_t;
typedef long                off_t;
typedef unsigned int        mode_t;
typedef unsigned int        dev_t;
typedef unsigned int        ino_t;
typedef unsigned int        nlink_t;
typedef unsigned int        blksize_t;
typedef unsigned int        blkcnt_t;
typedef unsigned int        useconds_t;
/* EX-OS non ha utenti ne' gruppi: questi due esistono perche' struct stat
 * ha i campi (a zero) e chi li stampa deve poterli dichiarare. */
typedef unsigned int        uid_t;
typedef unsigned int        gid_t;

/* =============================================================================
 * ! DA QUI IN GIU' E' TUTTO extern "C" QUANDO CI PASSA UN COMPILATORE C++
 *
 * PERCHE' SERVE, ed e' meno ovvio di quanto sembri. Il C++ decora i nomi
 * delle funzioni con i tipi degli argomenti — `printf` diventa
 * `_Z6printfPKcz` — perche' gli serve per il sovraccarico. La nostra
 * libc.a e' compilata da un compilatore C e dentro ha `printf`, il nome
 * nudo. Senza questa guardia un programma C++ che include questo header
 * chiama un simbolo che nell'archivio NON C'E':
 *
 *     undefined reference to `printf(char const*, ...)'
 *
 * ! E NON E' SOLO UN PROBLEMA DI COLLEGAMENTO. La libstdc++ dichiara per
 * conto suo alcune funzioni della libc con `extern "C"` esplicito —
 * libsupc++/new_opa.cc lo fa con memalign — e allora l'errore arriva
 * prima, in compilazione, e dice una cosa che sembra assurda:
 *
 *     error: conflicting declaration of 'void* memalign(size_t, size_t)'
 *            with 'C' linkage
 *
 * cioe' «questa dichiarazione e' in conflitto con se stessa». La causa e'
 * che la NOSTRA era in C++ e la loro in C: due funzioni diverse con lo
 * stesso nome. E' stato il primo sintomo, ed e' un sintomo fuorviante.
 *
 * La guardia si chiude in fondo al file. Non racchiude i `typedef` e le
 * macro qui sopra perche' non ne hanno bisogno — i tipi non hanno
 * collegamento — ma tenerli fuori non costa niente e rende evidente che
 * la regola riguarda le FUNZIONI.
 * ============================================================================= */
#ifdef __cplusplus
extern "C" {
#endif

/* Stringa */
size_t  strlen(const char *s);
char   *strcpy(char *dst, const char *src);
char   *strncpy(char *dst, const char *src, size_t n);
int     strcmp(const char *a, const char *b);
/* Confronto che ignora maiuscole e minuscole. Non e' del C standard —
 * <strings.h>, POSIX — ma lo chiama tutto il codice di terzi. */
int     strcasecmp(const char *a, const char *b);
/* Nella locale "C" l'ordine di collazione e' quello dei byte: strcoll e'
 * strcmp. C'e' perche' chi ordina nomi per l'utente scrive strcoll. */
int     strcoll(const char *a, const char *b);
/* ! La trasformazione che rende strcmp equivalente a strcoll. Nella
 * locale "C" i due gia' coincidono, quindi qui e' una COPIA — e il valore
 * di ritorno e' la lunghezza della stringa d'origine, non quella copiata:
 * chi passa un buffer troppo corto lo scopre confrontando, come dice lo
 * standard, invece di ritrovarsi un risultato troncato che sembra buono. */
size_t  strxfrm(char *dst, const char *src, size_t n);
int     strncasecmp(const char *a, const char *b, size_t n);
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
char   *strpbrk(const char *s, const char *accetta);

/* =============================================================================
 * Multibyte e caratteri larghi — <wchar.h>
 *
 * Nella locale "C", l'unica che EX-OS ha, un byte E' un carattere: queste
 * sono promozioni, non conversioni, e MB_CUR_MAX vale 1 per costruzione.
 * ! mbstowcs e mbrtowc non falliscono MAI — vedi lib/libc.c e <wchar.h>,
 * dove sta anche il perche' non c'e' nessuna wcslen.
 *
 * wchar_t lo dichiara il compilatore in <stddef.h>, incluso qui sopra.
 * ============================================================================= */
typedef int wint_t;
#define WEOF        ((wint_t)-1)
#define MB_CUR_MAX  1

/* Lo stato di una conversione multibyte: e' una struttura perche' lo
 * standard vuole un tipo completo da dichiarare e azzerare, ma non c'e'
 * niente da ricordare — non c'e' nessuna conversione da fare. */
typedef struct {
    int __nulla;
} mbstate_t;

size_t  mbstowcs(wchar_t *dst, const char *src, size_t n);
size_t  mbrtowc(wchar_t *dst, const char *src, size_t n, mbstate_t *stato);
size_t  wcstombs(char *dst, const wchar_t *src, size_t n);

/* Le tre versioni a carattere singolo. Le chiede <cstdlib> della libstdc++,
 * che fa `using ::mblen;` e `using ::mbtowc;` senza guardare se servono a
 * qualcuno: se il nome non esiste, la libreria non compila. */
int     mblen(const char *s, size_t n);
int     mbtowc(wchar_t *dst, const char *src, size_t n);
int     wctomb(char *dst, wchar_t c);
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
#define errno   (*__errno_dove())
int *__errno_dove(void);
char *strerror(int err);
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

/* ! EOF NON C'ERA, e il valore c'era da sempre: fgetc() e compagne
 * ritornano -1 dal principio, ma il NOME mancava, quindi lo scriveva solo
 * chi aveva letto questo header. Il codice di terzi scrive `!= EOF` e
 * basta: senza la macro non compila, e — peggio — un `#if EOF != -1`, che
 * e' come safe-ctype.h di libiberty verifica di poter lavorare, vede uno
 * zero al posto di un nome sconosciuto e conclude che la libc e' sbagliata.
 *
 * Deve restare -1 anche il giorno che si cambiasse qualcos'altro: mezza
 * libc di terzi presume EOF == -1 per poterlo mescolare con i codici di
 * errore negativi. */
#define EOF (-1)

/* Le costanti che <stdio.h> deve avere, con i valori VERI di questa libc:
 * BUFSIZ e' la dimensione del buffer di un flusso aperto con fopen,
 * FOPEN_MAX il numero di flussi che exit() sa svuotare, L_tmpnam la
 * lunghezza del nome che compone tmpnam(). Un valore inventato qui sarebbe
 * peggio dell'assenza: chi dimensiona un proprio buffer su BUFSIZ si
 * aspetta che sia quello che serve, non un numero decorativo. */
#define BUFSIZ          4096
#define FOPEN_MAX       16
#define FILENAME_MAX    256
#define L_tmpnam        64
#define TMP_MAX         32      /* tentativi di mkstemp prima di arrendersi */

/* =============================================================================
 * ! LE CINQUE VARIABILI GLOBALI SI RAGGIUNGONO CON UN INDIRIZZO, non con un
 * nome, dal 17 agosto 2026 — cioe' da quando la libc puo' essere una libreria
 * CONDIVISA.
 *
 * Le 313 funzioni della libc si raggiungono con un salto indiretto, che non ha
 * bisogno di conoscerne la firma. Una variabile no: un programma che scrive
 * `errno = 0` scriverebbe nella PROPRIA copia mentre la libc legge la sua —
 * due variabili con lo stesso nome, e nessun errore da nessuna parte.
 *
 * E' la soluzione di ogni libc vera. Il sorgente di chi le usa non cambia:
 * `errno = 0`, `if (errno == ENOENT)`, `fprintf(stderr, ...)` e `environ` si
 * continuano a scrivere esattamente cosi'.
 *
 * ! VALGONO ANCHE COLLEGANDO STATICAMENTE, e apposta: un comportamento solo
 * invece di due che divergono in silenzio.
 * ============================================================================= */
int    *__errno_dove(void);
FILE  **__stdin_dove(void);
FILE  **__stdout_dove(void);
FILE  **__stderr_dove(void);
char ***__environ_dove(void);

/* Il programma registra i propri distruttori globali: exit(), che sta nella
 * libreria, non puo' trovarli da se'. Vedi lib/libc.c. */
void    __libc_distruttori_registra(void (*f)(void));

#define stdin   (*__stdin_dove())
#define stdout  (*__stdout_dove())
#define stderr  (*__stderr_dove())

FILE   *fopen(const char *path, const char *modo);
FILE   *fdopen(int fd, const char *modo);
/* Riapre un flusso esistente su un altro file tenendo lo stesso FILE*.
 * ! Il NUMERO del descrittore cambia — vedi lib/libc.c. */
FILE   *freopen(const char *path, const char *modo, FILE *f);
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

/* =============================================================================
 * fpos_t, fgetpos, fsetpos — la posizione come oggetto opaco
 *
 * Sono ftell/fseek con un'altra faccia, e su EX-OS non aggiungono niente:
 * esistono perche' esistono in <cstdio> della libstdc++, e perche' su un
 * sistema con codifiche a stato variabile la posizione non e' un numero.
 * Qui lo e', e fpos_t e' un long — ma resta OPACO per contratto: chi ci
 * fa aritmetica sopra scrive codice che altrove non compila.
 * ============================================================================= */
typedef long fpos_t;
int     fgetpos(FILE *f, fpos_t *pos);
int     fsetpos(FILE *f, const fpos_t *pos);

/* =============================================================================
 * setbuf, setvbuf — ! CI SONO MA NON CAMBIANO NIENTE
 *
 * La politica di bufferizzazione di EX-OS e' decisa e documentata (4 KB
 * sui file, svuotamento a fine chiamata su stdout/stderr — vedi
 * lib/libc.c), e non e' regolabile a runtime: i buffer sono dentro la
 * struttura FILE, non allocati a parte, quindi non c'e' niente da
 * sostituire.
 *
 * ! setvbuf RITORNA DIVERSO DA ZERO, cioe' «non l'ho fatto», invece di
 * fingere. Un programma che chiede di non bufferizzare e riceve 0 andrebbe
 * avanti convinto che ogni putc sia arrivato a destinazione. setbuf non
 * ritorna niente per definizione, e quindi non puo' dirlo: e' il motivo
 * per cui lo standard stesso raccomanda setvbuf.
 * ============================================================================= */
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
void    setbuf(FILE *f, char *buf);
int     setvbuf(FILE *f, char *buf, int modo, size_t dim);

int     vscanf(const char *fmt, __builtin_va_list args);

int     putchar(int c);
int     puts(const char *s);
int     getchar(void);
/* NON e' la gets() del C: prende la dimensione del buffer e toglie il
 * fine riga. Ritorna NULL sulla riga vuota. */
char   *gets(char *buf, int max);

/* Lettura formattata. Ritorna il numero di conversioni riuscite, 0 se la
 * prima non e' andata, -1 se l'ingresso era gia' finito — tre casi da
 * distinguere tutti. */
int     sscanf(const char *s, const char *fmt, ...);
/* fscanf legge una FINESTRA di 1024 byte dal flusso, ci passa sopra lo
 * stesso scanner di sscanf e riporta indietro il flusso di quanto non ha
 * consumato. ! Una conversione che avrebbe bisogno di piu' di 1024 byte
 * si ferma li'; su un flusso non posizionabile (la console) si legge una
 * riga e il resto si perde. Vedi lib/libc.c. */
int     fscanf(FILE *f, const char *fmt, ...);
int     scanf(const char *fmt, ...);
int     vfscanf(FILE *f, const char *fmt, __builtin_va_list args);
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

/* Quoziente e resto in un colpo solo. Il tipo lo pretende <cstdlib> della
 * libstdc++, che ne dichiara le funzioni inline sopra: senza `ldiv_t` non
 * compila l'header, prima ancora che qualcuno chiami qualcosa. */
typedef struct { int  quot; int  rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;
div_t   div(int num, int den);
ldiv_t  ldiv(long num, long den);
lldiv_t lldiv(long long num, long long den);
long long llabs(long long v);
long long atoll(const char *s);

/* =============================================================================
 * Numeri pseudocasuali
 *
 * ! NON SONO CASUALI E NON VANNO USATI DOVE CONTA. E' un generatore
 * congruenziale lineare, quello dell'esempio del K&R: si ripete, e da un
 * seme noto da' sempre la stessa sequenza. Va bene per mescolare, per
 * scegliere un ripiego, per una prova; non va bene per una chiave, per un
 * identificativo che deve essere imprevedibile, per niente che qualcuno
 * possa avere interesse a indovinare. Il giorno che servisse quello,
 * servira' una sorgente di entropia vera, che il kernel non ha.
 * ============================================================================= */
#define RAND_MAX 32767
int     rand(void);
void    srand(unsigned int seme);

/* Esegue `comando` con `/bin/sh -c` e ritorna il SUO codice di uscita;
 * 127 vuol dire «comando non trovato», come in ogni shell. system(NULL)
 * risponde se un interprete c'e'.
 *
 * ! FINO AD AGOSTO 2026 RITORNAVA -1 CON ENOSYS, perche' /bin/sh aveva un
 * `_start(void)` e non vedeva i propri argomenti. Adesso li vede (vedi
 * bin/sh/start.S) e il comando lo interpreta la shell — non una seconda
 * mezza implementazione qui dentro, che divergerebbe dalla prima il giorno
 * stesso. */
int     system(const char *comando);

/* Un comando come flusso: "r" per leggere cio' che stampa, "w" per
 * scrivergli sullo stdin. pclose() aspetta il figlio e ne ritorna il
 * codice di uscita.
 *
 * ! UNA DIREZIONE PER VOLTA: "r+" non c'e'. Servirebbero due pipe, e
 * con due pipe si finisce nello stallo classico dei coprocessi — uno
 * fermo a riempire una mentre l'altro aspetta sull'altra. Chi ne ha
 * bisogno usi pipe() e spawn_ex() e decida lui l'ordine delle letture. */
FILE   *popen(const char *comando, const char *modo);
int     pclose(FILE *f);

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
double  fabs(double v);
/* ! L'unica funzione di libm che c'e': e' `fsqrt` dell'x87, quindi
 * correttamente arrotondata. Vedi <math.h>. */
double  sqrt(double x);
double  atof(const char *s);
double  frexp(double x, int *e);   /* l'inversa: mantissa in [0.5,1) ed esponente */

/* Le "variabili d'ambiente" di EX-OS sono la sezione [env] di
 * /boot/kernel.cfg: getenv() e' la facciata POSIX su getconf() (vedi piu'
 * avanti), e il puntatore che ritorna vale FINO ALLA CHIAMATA SUCCESSIVA.
 * Nel codice nuovo si preferisca getconf(). */
char   *getenv(const char *nome);
void    exit(int code);
/* ! _exit NON svuota i buffer e NON chiama gli handler di atexit: e'
 * l'uscita di chi ha perso fiducia nel proprio stato. Un file aperto in
 * scrittura resta monco — vedi lib/libc.c. */
void    _exit(int code);
/* Lo stesso di _exit con il nome che gli da' il C99. Un nome in piu' per
 * la stessa cosa, e c'e' perche' <cstdlib> lo dichiara. */
void    _Exit(int code);

/* =============================================================================
 * quick_exit, at_quick_exit
 *
 * Una seconda lista di funzioni di uscita, separata da quella di atexit().
 * ! LA DIFFERENZA CHE CONTA: quick_exit NON svuota i flussi e NON chiama
 * gli handler di atexit — chiama solo i propri, poi _Exit. Serve a chi
 * vuole terminare in fretta senza rinunciare del tutto a ripulire, e
 * confonderla con exit() significa perdere l'ultima scrittura di ogni file
 * aperto.
 * ============================================================================= */
int     at_quick_exit(void (*fn)(void));
void    quick_exit(int code);

/* I due nomi che <stdlib.h> da' allo stato di uscita. Valgono quello che
 * gia' vale: la shell di EX-OS legge 0 come riuscita e diverso da zero
 * come errore, come tutti. */
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void    abort(void);
void   *malloc(size_t size);

/* ! alloca() NON E' UNA FUNZIONE E NON PUO' ESSERLO: prende spazio sullo
 * STACK DI CHI LA CHIAMA, quindi deve essere il compilatore a emetterla
 * dentro quel frame. Una vera funzione libererebbe il proprio frame
 * uscendo, restituendo un puntatore a memoria appena riciclata — e il
 * guasto si vedrebbe altrove, molto dopo.
 *
 * ! NON SI LIBERA e sparisce da sola al `return`. Dentro un ciclo lo
 * stack cresce a ogni giro senza tornare indietro, ed EX-OS lo fa
 * crescere a richiesta fino a un tetto: un alloca() in un ciclo lungo si
 * manifesta come un page fault, non come un malloc che ritorna NULL.
 *
 * C'e' perche' la runtime di FreeBASIC la usa in una trentina di file per
 * i descrittori temporanei di stringa. */
#ifndef alloca
#define alloca(n) __builtin_alloca(n)
#endif
void    free(void *ptr);

/* ! QUANTI BYTE SI POSSONO DAVVERO USARE dietro un puntatore di malloc: e'
 * `dim` dell'intestazione, cioe' la taglia vera dopo l'allineamento, non
 * quella chiesta. Serve a chi conta la memoria viva — il primo a chiederla e'
 * stato il raccoglitore di QuickJS. NULL rende 0. */
size_t  malloc_usable_size(void *ptr);
void   *calloc(size_t nmemb, size_t size);
void   *realloc(void *ptr, size_t size);

/* -----------------------------------------------------------------------
 * Allocazione ALLINEATA.
 *
 * malloc() garantisce otto byte, che bastano per un double e per qualunque
 * tipo scalare del bersaglio. Non bastano per `alignas(16)` e simili, e
 * dal C++17 il compilatore per quei tipi non chiama piu' `operator new`
 * ma la sua variante allineata — che nella libstdc++ e' costruita sopra
 * memalign(). Senza queste tre funzioni la libreria standard del C++ si
 * costruisce lo stesso, ma con una versione che ARROTONDA e restituisce
 * memoria non allineata: sbaglia in silenzio, che e' il caso che questo
 * progetto rifiuta.
 *
 * ! IL PUNTATORE RESTITUITO SI LIBERA CON free(), non con una free
 * speciale: l'allineamento si ottiene spezzando un blocco dell'heap e
 * mettendo una vera intestazione davanti al risultato, quindi per free()
 * e' un blocco come tutti gli altri. Vedi lib/libc.c.
 * ----------------------------------------------------------------------- */
void   *memalign(size_t allineamento, size_t size);
void   *aligned_alloc(size_t allineamento, size_t size);
int     posix_memalign(void **risultato, size_t allineamento, size_t size);

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
/* POSIX, non C standard (isblank e' C99): le chiama il codice di terzi —
 * isascii la printf di GMP. Nella locale "C" ASCII e' tutto cio' che sta
 * sotto il 128, quindi non e' un'approssimazione. */
int     isascii(int c);
int     toascii(int c);
int     isblank(int c);
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
/* ! Il secondo argomento si IGNORA: EX-OS non ha permessi. C'e' per la
 * firma di POSIX, che ogni programma portato da un Unix scrive. */
int     mkdir(const char *path, mode_t modo);

/* Cancella una directory VUOTA. Ritorna 0, o un errno negativo:
 * -2 non trovata, -20 non e' una directory, -39 non vuota. */
int     rmdir(const char *path);

/* Cancella un file. Ritorna 0, -2 se non esiste, -21 se e' una directory
 * (per quelle serve rmdir). Non espande caratteri jolly: accetta un nome
 * preciso. */
int     unlink(const char *path);
int     remove(const char *path);   /* unlink() col nome del C standard */
char   *getcwd(char *buf, size_t size);
/* Il percorso in forma canonica: assoluto, senza "." ne' ".." ne' doppi
 * '/', e il file deve ESISTERE. ! Non segue collegamenti simbolici
 * perche' EX-OS non ne ha. Con `resolved` NULL alloca con malloc. */
char   *realpath(const char *path, char *resolved);
void    sched_yield(void);

/* =============================================================================
 * ! sleep RITORNA unsigned int, E NON void — corretto ad agosto 2026
 *
 * POSIX dice che sleep() ritorna **i secondi che restavano da dormire**
 * quando un segnale l'ha interrotta, e 0 se ha dormito tutto. Non e' una
 * finezza: il modo canonico di usarla e'
 *
 *     while ((secs = sleep(secs))) { }
 *
 * cioe' «riprova finche' non hai finito davvero» — ed e' esattamente cio'
 * che scrive src/c++11/thread.cc della libstdc++. Con una sleep che
 * ritorna void quella riga non compila:
 *
 *     error: void value not ignored as it ought to be
 *
 * ! SU EX-OS IL VALORE E' SEMPRE 0, e non e' una bugia: non ci sono
 * segnali che possano interrompere il sonno, quindi la dormita e' sempre
 * completa. La firma dice la verita' sul contratto; il valore dice la
 * verita' su questo sistema.
 *
 * usleep ritorna int per la stessa ragione (0, o -1 con errno): e' quello
 * che dice POSIX, e chi controlla il ritorno non deve scoprire qui che
 * non c'e'.
 * ============================================================================= */
int             usleep(unsigned int us);
unsigned int    sleep(unsigned int sec);

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
 * ! NESSUN FUSO ORARIO. L'orologio CMOS di EX-OS e' ora locale e il
 * sistema non sa dove si trova: localtime() e gmtime() fanno la stessa
 * identica cosa, e i secondi di time() sono "secondi dal 1970 letti su un
 * orologio locale". Buoni per misurare intervalli e datare un file, NON
 * confrontabili con l'istante di un'altra macchina.
 *
 * time_t e' `long`: a 32 bit con segno arriva al 2038.
 * ============================================================================= */
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
    int tm_sec;     /* 0..60 */
    int tm_min;     /* 0..59 */
    int tm_hour;    /* 0..23 */
    int tm_mday;    /* 1..31 */
    int tm_mon;     /* 0..11 — gennaio e' ZERO */
    int tm_year;    /* anni dal 1900 */
    int tm_wday;    /* 0..6, domenica = 0 */
    int tm_yday;    /* 0..365 */
    int tm_isdst;   /* sempre 0 */

    /* =========================================================================
     * ! I DUE CAMPI DEL FUSO CI SONO E DICONO LA VERITA': zero e «UTC».
     *
     * Non sono un'aggiunta di comodo: stanno in POSIX dal 2024, li hanno la
     * glibc e i BSD, e il codice di terzi li usa senza chiedere permesso —
     * il primo a chiederli e' stato QuickJS, che senza `tm_gmtoff` non
     * compila la sua `Date`.
     *
     * ! E QUI VALGONO SEMPRE 0 e «UTC» PERCHE' EX-OS NON SA DOVE SI TROVA.
     * Riempirli con un fuso inventato sarebbe peggio che non averli: un
     * programma che li legge crederebbe a un'informazione che nessuno ha
     * mai misurato. Zero non e' un ripiego, e' la risposta esatta finche'
     * localtime() e gmtime() sono la stessa funzione.
     *
     * ! STANNO IN FONDO, ed e' l'unico posto dove potevano stare. I campi
     * di prima non si spostano di un byte, quindi un binario gia' collegato
     * continua a leggerli agli scostamenti di sempre. Ma `sizeof` cambia:
     * chi passa una PROPRIA struct tm a gmtime_r dev'essere ricompilato con
     * questo header — ed e' per questo che le due funzioni nuove sono nuove
     * e non il rifacimento di due vecchie.
     * ========================================================================= */
    long        tm_gmtoff;  /* secondi a est di UTC: sempre 0 */
    const char *tm_zone;    /* il nome del fuso: sempre "UTC" */
};

struct timeval {
    /* time_t, non long: vedi il commento sulla sua definizione. */
    time_t tv_sec;
    long   tv_usec;   /* risoluzione vera: 10 ms, il tick del PIT */
};

/* Ritorna (time_t)-1 se l'orologio non risponde: zero significherebbe il
 * 1970, cioe' una data sbagliata spacciata per buona. */
time_t     time(time_t *t);
struct tm *gmtime(const time_t *t);      /* risultato in una struttura STATICA */
struct tm *localtime(const time_t *t);   /* identica a gmtime: vedi sopra */

/* ! LE DUE VERSIONI RIENTRANTI, che scrivono dove dice CHI CHIAMA. Le altre
 * due tengono il risultato in una struttura statica: due chiamate di seguito e
 * la prima e' persa, e con due processi leggeri sarebbe peggio. Rendono `out`,
 * o NULL se `t` e' NULL — come vuole POSIX. */
struct tm *gmtime_r(const time_t *t, struct tm *out);
struct tm *localtime_r(const time_t *t, struct tm *out);
/* ! L'INVERSA DI gmtime, E NORMALIZZA LA STRUTTURA CHE RICEVE — per
 * questo il parametro non e' const. Un tm_mon a 12 diventa gennaio
 * dell'anno dopo, un tm_sec a 90 diventa un minuto e mezzo: e' cosi' che
 * si fa aritmetica sulle date. tm_wday e tm_yday in ingresso vengono
 * IGNORATI e riscritti. Interpreta i campi come UTC, perche' EX-OS non sa
 * in che fuso si trova. */
time_t     mktime(struct tm *tm);
int        gettimeofday(struct timeval *tv, void *fuso);

/* La differenza fra due istanti, in secondi. Su EX-OS time_t e' un intero
 * di secondi e la sottrazione basterebbe: c'e' perche' lo standard non
 * garantisce che time_t sia aritmetico, e chi scrive difftime scrive
 * codice che vale anche altrove. */
double     difftime(time_t fine, time_t inizio);

/* ! LA RISOLUZIONE VERA E' 10 ms, il tick del PIT: tv_nsec e' sempre un
 * multiplo di 10 000 000. La struttura ha i nanosecondi perche' cosi' e'
 * fatta, non perche' li sappiamo misurare. */
struct timespec {
    long tv_sec;
    long tv_nsec;
};
#define TIME_UTC 1
/* Ritorna `base` se ha funzionato, 0 se no — e non e' la solita
 * convenzione 0/-1: e' quella di timespec_get, ed e' un modo classico di
 * sbagliare a usarla. */
int        timespec_get(struct timespec *ts, int base);

/* ! La risoluzione vera e' 10 ms, non un nanosecondo: sotto c'e' il
 * timer a 100 Hz. Si arrotonda per ECCESSO, cosi' una richiesta piu'
 * corta di un tick dorme davvero invece di tornare subito. */
int        nanosleep(const struct timespec *req, struct timespec *rem);

/* ! Tutte e quattro tornano 0, e non significa «root»: significa che su
 * EX-OS non esiste la domanda. Serve a OPENSSL_issetugid(), che chiede
 * «giro con privilegi non miei?» — e qui la risposta onesta e' no. */
/* ! RENDONO L'IDENTITA' VERA dal 17 agosto 2026: prima erano zero fisso, che
 * era onesto quando non c'erano utenti e sarebbe una bugia adesso.
 *
 * setuid() la puo' chiamare solo root e serve a scendere: e' cosi' che `login`
 * cambia utente senza che esista il bit setuid sui file. Rende 0, o -1 con
 * errno EPERM. */
int        setuid(unsigned int uid);
/* Consegna un file a un altro utente. Solo root; su FAT rende -1 con ENOSYS
 * invece di fingere. */
int        chown(const char *percorso, unsigned int uid, unsigned int gid);

/* Il framebuffer, e SOLO quello. Non serve essere root ne' un driver: e' una
 * capacita' stretta al posto di mmio_map, che mappa un indirizzo qualunque ed
 * e' riservata. Rende 0 con errno se non c'e' un framebuffer. */
void      *fb_map(void);
int        getuid(void);
int        geteuid(void);
int        getgid(void);
int        getegid(void);

/* Formatta una data secondo `fmt`. Ritorna i caratteri scritti (NUL
 * escluso), o 0 se non ci stavano — nel qual caso il contenuto di `buf`
 * non e' utilizzabile, come dice lo standard.
 *
 * ! NON HA TUTTE LE CONVERSIONI, e quelle che non ha le ricopia alla
 * lettera invece di ingoiarle. %z e %Z dicono sempre +0000 e UTC: EX-OS
 * non ha fusi orari. Vedi lib/libc.c per l'elenco di cosa c'e'. */
size_t     strftime(char *buf, size_t max, const char *fmt, const struct tm *tm);

/* La data in venticinque caratteri e un a capo: "Sun Aug  2 17:04:05 2026\n".
 * ! Il risultato sta in un buffer STATICO, sovrascritto dalla chiamata
 * dopo — come gmtime e localtime. */
char      *asctime(const struct tm *tm);
char      *ctime(const time_t *t);

/* ! utime NON CAMBIA NIENTE: nessun filesystem di EX-OS sa riscrivere le
 * date di un file. Ritorna 0 se il file c'e'. Vedi lib/libc.c. */
int        utime(const char *path, const void *tempi);

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

/* Chi tiene la console della GRAFICA. `azione`: 0 chiedi (rende il numero
 * della console, -1 se nessuna), 1 prendi (quella del chiamante), 2 lascia.
 *
 * ! LO STATO STA NEL KERNEL, non nel server grafico: quando il server muore —
 * anche ucciso — qualcuno deve poter dire «li' non c'e' piu' niente», e un
 * flag tenuto dal server morirebbe con lui lasciando la porta aperta su una
 * stanza vuota. Il kernel ricontrolla da se' che chi l'aveva presa sia vivo. */
int console_grafica(int azione);
int console_write(unsigned int n, const void *buf, unsigned int len);

/* =============================================================================
 * console_setfg — dichiara chi ha la tastiera su questa console
 *
 * ! CHI LANCIA UN PROGRAMMA INTERATTIVO DEVE PASSARGLI IL PRIMO PIANO, e
 * poi riprenderselo. sys_read su stdin serve SOLO il processo dichiarato in
 * primo piano: a tutti gli altri rende zero, cioe' fine dell'input. E' il
 * controllo dei job — un comando lanciato con '&' non deve rubare la
 * tastiera alla shell — ma vale per chiunque, non solo per il background.
 *
 * Il modo in cui si sbaglia non somiglia a un errore. Un programma che ne
 * lancia un altro senza cedere il primo piano lo condanna a leggere zero
 * byte da ogni domanda: le risposte diventano tutte quella predefinita, e
 * l'utente vede scorrere le domande senza poterci mettere mano. Nessun
 * messaggio, nessun codice d'errore — solo scelte fatte da nessuno.
 *
 *     pid = spawn(prog, argv);
 *     console_setfg((unsigned)pid);        <- adesso legge lui
 *     waitpid(pid, &stato, 0);
 *     console_setfg((unsigned)getpid());   <- e adesso di nuovo io
 *
 * La shell lo fa da sempre (avvia_figlio in bin/sh/shell.c); e' chi sta in
 * mezzo — install che lancia toolinst — a doverselo ricordare.
 * ============================================================================= */
int console_setfg(unsigned int pid);

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
 * ! La mappa vale finche' quei file non si spostano: ricopiare il kernel
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

/* ! CALCOLA LA MAPPA E NON SCRIVE NIENTE. Serve a sapere se due file
 * sarebbero mappabili PRIMA di cancellare quelli che funzionano: su FAT la
 * mappa ammette un solo intervallo, e un kernel cresciuto di qualche KB
 * puo' non entrarci piu'. I nomi sono senza directory e minuscoli (la
 * directory e' sempre /boot), NULL per quelli predefiniti. */
int bootverify(const char *punto, const char *nome_s2, const char *nome_k,
               BootInstallInfo *info);

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
 * ! Non formatta niente. Le partizioni nuove contengono quello che c'era
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
#define O_NONBLOCK  0x800
#define O_ACCMODE   3

/* =============================================================================
 * Un secondo descrittore sullo stesso file — dup, dup2, fcntl
 *
 * ! CONDIVIDONO IL FILE, NON LA POSIZIONE. Su POSIX due fd duplicati
 * hanno un offset SOLO: una read() da uno sposta anche l'altro. Qui no —
 * la posizione sta nel descrittore del processo, e ognuno tiene la sua.
 * Quello che condividono e' il file aperto: finche' ne resta uno, il file
 * non si chiude, che e' il motivo per cui dup() esiste.
 *
 * Chi legge da un fd duplicato faccia una lseek() esplicita invece di dare
 * per scontato di ripartire da dove stava l'altro. Il perche' della scelta
 * sta in kernel/syscall/syscall_impl.c, sopra fd_duplica().
 *
 * dup2() e' anche l'unico modo di sostituire stdin/stdout/stderr: close()
 * su 0, 1 o 2 e' rifiutata apposta, perche' lascerebbe il processo senza
 * uscita, mentre chi arriva da dup2 il rimpiazzo ce l'ha gia'.
 * ============================================================================= */
#define F_DUPFD     0
#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4

/* C'e' per chi lo scrive (`fcntl(fd, F_SETFD, FD_CLOEXEC)` e' in ogni
 * programma POSIX), non perche' faccia qualcosa: spawn() non eredita i
 * descrittori del padre, quindi non esiste il momento in cui un fd
 * sopravvive a un exec. F_GETFD risponde sempre 0. */
#define FD_CLOEXEC  1

int dup(int fd);
int dup2(int vecchio, int nuovo);
int fcntl(int fd, int cmd, ...);

/* =============================================================================
 * Posizionamento e informazioni sui file
 *
 * st_attr usa le convenzioni FAT su TUTTI i filesystem, perche' sono
 * quelle che i programmi gia' interpretano: 0x10 directory, 0x01 sola
 * lettura.
 *
 * st_ident e' l'IDENTITA' dell'oggetto: due percorsi danno lo stesso
 * numero se e solo se sono lo stesso file o la stessa directory, su
 * qualunque filesystem e attraverso i montaggi. Si chiamava
 * st_first_clus e valeva 0 fuori da FAT — con le conseguenze raccontate
 * sopra stat_interno() in kernel/fs/vfs.c. E' anche cio' che finisce in
 * st_ino di `struct stat`.
 * ============================================================================= */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

typedef struct {
    unsigned int    st_size;
    unsigned int    st_ident;
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

/* =============================================================================
 * ! I TIPI DI FILE CHE EX-OS NON HA
 *
 * FIFO, dispositivi a caratteri e a blocchi, collegamenti simbolici,
 * socket. stat() non metterà mai uno di questi in st_mode, e le macro
 * S_IS*() corrispondenti ritorneranno sempre 0.
 *
 * Ci sono per lo stesso motivo dei codici errno della rete e delle
 * costanti DT_*: **servono a essere nominati**. La <filesystem> del C++17
 * costruisce `std::filesystem::file_type` con uno switch su tutti — e un
 * nome mancante è un errore di compilazione, non un ramo morto.
 *
 * ! Ritornare sempre 0 è la risposta GIUSTA, non un ripiego: su EX-OS un
 * collegamento simbolico non esiste, quindi «questo file è un
 * collegamento?» ha davvero risposta no. È diverso dal caso in cui non si
 * sapesse rispondere.
 *
 * I valori sono quelli di Linux e non vanno reinventati: il giorno che i
 * collegamenti simbolici arrivassero, S_IFLNK dovrà valere 0120000 come
 * ovunque.
 * ============================================================================= */
#define S_IFIFO     0010000
#define S_IFCHR     0020000
#define S_IFBLK     0060000
#define S_IFLNK     0120000
#define S_IFSOCK    0140000
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/* I bit dei permessi. ! EX-OS NON HA PERMESSI VERI: st_mode li ricostruisce
 * dall'attributo di sola lettura di FAT (vedi sopra), e chmod() è inerte.
 * Questi nomi servono a leggere quei bit e a scrivere codice portabile —
 * `if (st.st_mode & S_IWUSR)` risponde davvero «è scrivibile?». */
#define S_IRWXU     0000700
#define S_IRUSR     0000400
#define S_IWUSR     0000200
#define S_IXUSR     0000100
#define S_IRWXG     0000070
#define S_IRGRP     0000040
#define S_IWGRP     0000020
#define S_IXGRP     0000010
#define S_IRWXO     0000007
#define S_IROTH     0000004
#define S_IWOTH     0000002
#define S_IXOTH     0000001
#define S_ISUID     0004000
#define S_ISGID     0002000
#define S_ISVTX     0001000

/* ! I CAMPI HANNO I TIPI DI POSIX, NON `unsigned int`, e la differenza
 * non e' cosmetica.
 *
 * Fino ad agosto 2026 erano tutti `unsigned int`. Sul nostro bersaglio
 * hanno la stessa larghezza dei tipi giusti, quindi i VALORI erano
 * corretti e nessuno se ne era accorto — ma il tipo si vede appena
 * qualcuno prende l'INDIRIZZO di un campo:
 *
 *     libcpp/files.cc:803: error: invalid conversion from 'unsigned int*'
 *                          to 'off_t*' {aka 'long int*'}
 *
 * cioe' `&st.st_size` passato dove serve un `off_t *`. E' il preprocessore
 * di GCC, che lo fa per dire al lettore quanti byte ha letto davvero.
 *
 * ! `st_size` E' SEGNATO (`off_t` e' `long`), quindi il tetto e' 2 GB e
 * non 4. E' lo stesso limite che ha gia' `lseek()`, che e' la syscall
 * sotto: dichiararlo senza segno non renderebbe piu' grandi i file,
 * renderebbe solo silenziosa la troncatura al confine col kernel. */
struct stat {
    dev_t           st_dev;     /* sempre 0: EX-OS non numera i volumi */
    ino_t           st_ino;     /* primo cluster/inode, 0 dove non si applica */
    mode_t          st_mode;    /* tipo | permessi ricostruiti */
    nlink_t         st_nlink;   /* sempre 1 */
    uid_t           st_uid;     /* sempre 0: non ci sono utenti */
    gid_t           st_gid;     /* sempre 0 */
    off_t           st_size;
    blksize_t       st_blksize; /* 512: il settore, l'unita' vera dei nostri fs */
    blkcnt_t        st_blocks;  /* settori da 512 occupati, arrotondati per eccesso */
    time_t          st_atime;   /* = st_mtime: non si tiene l'ultimo accesso */
    time_t          st_mtime;
    time_t          st_ctime;   /* = st_mtime */
};

void   *sbrk(int incr);      /* cima dell'heap; ritorna la posizione VECCHIA */
long    lseek(int fd, long offset, int whence);

/* =============================================================================
 * Proprietario e permessi — StatPerm
 *
 * ! E' UNA CHIAMATA IN PIU', NON CAMPI IN PIU' A `Stat`, ed e' la stessa regola
 * scritta accanto a spawn_su_console piu' avanti: cambiare una struttura che i
 * programmi si passano gia' vuol dire ricostruire tutto cio' che la usa, e
 * `Stat` la usa chiunque apra un file. Percio' `st_uid` e `st_gid` di
 * `struct stat` restano a zero e continuano a dire il falso — la verita' la
 * chiede chi la vuole, con questa.
 *
 * ! `modo == 0` VUOL DIRE «QUESTO VOLUME NON HA PROPRIETARI», non «nessun
 * permesso»: su FAT e su ISO 9660 non esistono, e il VFS lo dichiara cosi'.
 * Chi stampa i permessi deve distinguere i due casi, o mostrerebbe
 * `----------` su ogni file di un floppy.
 *
 * STRUTTURA DUPLICATA A MANO da kernel/include/syscall.h, come DirEntry e
 * MemInfo: deve restare identica, e statperm() rende -EINVAL se le due copie
 * divergono di dimensione.
 * ============================================================================= */
typedef struct {
    unsigned short modo;    /* permessi POSIX (0644, 0755...), 0 = non ci sono */
    unsigned short uid;
    unsigned short gid;
} StatPerm;

int     statperm(const char *path, StatPerm *p);

/* =============================================================================
 * Dal numero al nome — /boot/utenti
 *
 * ! ADESSO GLI UTENTI SONO DUE, ed e' la condizione che questo sistema chiede
 * prima di condividere qualcosa: la traduzione uid -> nome stava dentro
 * /bin/id, e con `ls -l` sarebbe diventata una seconda copia dello stesso
 * parser. Due copie di un formato di file divergono al primo campo aggiunto.
 *
 * ! LEGGE IL FILE PUBBLICO, non quello delle password: /boot/utenti e' 0644 e
 * contiene `nome:uid:gid`; le impronte stanno in /boot/ombra, che e' 0600. Se
 * i due non fossero separati, tradurre un uid riuscirebbe solo a root.
 *
 * Rende 1 se l'ha trovato. Il file puo' non esserci — avviando da floppy o da
 * CD non c'e' affatto — e non e' un errore: chi chiama mostra il numero.
 * ============================================================================= */
int     nome_utente(unsigned int uid, char *out, unsigned int max);

/* =============================================================================
 * diventa_root() — diventare root provando di sapere una password
 *
 * ! LA VERIFICA LA FA IL KERNEL, e non e' un giro piu' lungo: /boot/ombra e'
 * 0600, quindi un processo di un utente normale NON PUO' LEGGERLO — ed e'
 * esattamente il punto. Se controllasse un programma in spazio utente
 * bisognerebbe consegnargli qualcosa di quel file, e allora il file potrebbe
 * anche essere pubblico.
 *
 * ! ED E' UNA CAPACITA' STRETTA, NON IL BIT setuid SUI FILE: quello renderebbe
 * pericoloso ogni eseguibile che lo porta. Qui c'e' un solo modo di diventare
 * root, e passa da una password.
 *
 * La regola: DIMOSTRA DI ESSERE `nome`, e se `nome` puo' fare root lo diventi.
 * `nome` puo' fare root se ha uid 0, oppure se sta in /boot/amministratori.
 *
 * Rende 0 se il processo e' diventato root, -EPERM altrimenti — e non dice
 * quale delle due cose e' andata storta, perche' la differenza serve solo a
 * chi sta provando i nomi.
 * ============================================================================= */
/* ! IL NOME NON E' `su`, ED E' UN CASO ISTRUTTIVO: `su` in una libreria e' tre
 * lettere buttate in uno spazio di nomi che tutti condividono, e infatti
 * bin/mkfs/ext2.c aveva gia' una sua `su`. Il compilatore l'ha detto subito —
 * ma un nome corto in una libreria condivisa e' una collisione che aspetta. */
int     diventa_root(const char *nome, const char *password);

/* stat() riempie la struttura POSIX; statraw() da' i campi grezzi del
 * filesystem (attributi FAT, primo cluster, data e ora codificate) a chi
 * ne ha bisogno davvero — mkfs, fdisk, un ls che mostri gli attributi. */
/* ! chmod, fchmod e umask NON CAMBIANO NIENTE: EX-OS non ha permessi, e
 * l'unico bit che i filesystem tengono davvero — la sola lettura di FAT —
 * non ha una syscall che lo scriva. Ci sono perche' il codice di terzi le
 * chiama (bfd le usa a ogni file eseguibile che produce) e rattoppare i
 * sorgenti altrui sarebbe peggio. Vedi lib/libc.c per il ragionamento, ed
 * e' la stessa convenzione di O_EXCL in <fcntl.h>. */
int     chmod(const char *path, mode_t modo);
int     fchmod(int fd, mode_t modo);
mode_t  umask(mode_t maschera);

int     stat(const char *path, struct stat *st);
/* Identica a stat: EX-OS non ha collegamenti simbolici, quindi non c'e'
 * niente da non seguire. C'e' perche' il codice di terzi la nomina. */
int     lstat(const char *path, struct stat *st);
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
/* ! `ident` E' L'IDENTITA' DEL FILE, la stessa di Stat.st_ident, ed e' cio'
 * che readdir() mette in d_ino. Prima non c'era e d_ino valeva zero su ogni
 * voce: GNU make — che come ogni Unix legge lo zero come «voce cancellata» —
 * vedeva ogni directory vuota. Il perche' per esteso sta accanto alla
 * struttura gemella in kernel/include/syscall.h, che deve restare identica
 * a questa. */
typedef struct {
    char           name[DIRENT_NAME_MAX];
    unsigned int   size;
    unsigned int   ident;
    unsigned char  is_dir;
} DirEntry;

/* listdir — elenca una directory in un colpo solo (non e' la classica
 * tripletta POSIX opendir/readdir/closedir, qui non serve un handle).
 * path: NULL/""/"/" per la root, "/NOME" per una subdirectory.
 * Ritorna il numero di entry scritte in buf (>=0), o <0 in caso di errore. */
int     listdir(const char *path, DirEntry *buf, int max);

/* =============================================================================
 * I NOMI DEGLI ERRORI
 *
 * I numeri c'erano dal principio — le syscall li ritornano negativi e
 * strerror() li traduce — ma i NOMI no, e senza quelli non compila una
 * riga di codice di terzi: `if (errno == ENOENT)` e' in ogni programma
 * scritto per POSIX, a cominciare da quelli che si vogliono portare qui.
 *
 * I valori sono quelli di Linux, come il resto della numerazione delle
 * syscall, e devono restare allineati alla tabella di strerror() in
 * lib/libc.c e ai codici del kernel.
 * ============================================================================= */
#define EPERM         1
#define ENOENT        2
#define ESRCH         3
#define EINTR         4
#define EIO           5
#define ENXIO         6
#define E2BIG         7
#define ENOEXEC       8
#define EBADF         9
#define ECHILD       10
#define EAGAIN       11
#define ENOMEM       12
#define EACCES       13
#define EFAULT       14
#define EBUSY        16
#define EEXIST       17
#define EXDEV        18
#define ENODEV       19
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define ENFILE       23
#define EMFILE       24
#define ENOTTY       25
#define EFBIG        27
#define ENOSPC       28
#define ESPIPE       29
#define EROFS        30
#define EMLINK       31
#define EPIPE        32
/* Nessuna funzione di EX-OS ritorna EDOM: non c'e' una libm che possa
 * ricevere un argomento fuori dominio. C'e' perche' il codice di terzi lo
 * usa come "questo valore non ha senso qui" — libctf ci segnala una
 * chiave di hash impossibile — e senza il nome non compila. */
#define EDOM         33
#define ERANGE       34
#define ENAMETOOLONG 36
#define ENOSYS       38
#define ENOTEMPTY    39
#define ELOOP        40
/* Sequenza multibyte non convertibile: la ritorna wcstombs quando un
 * carattere largo non ci sta in un byte. Vedi <wchar.h>. */
#define EILSEQ        84
#define ETIMEDOUT   110
#define ENOMEDIUM   123

/* =============================================================================
 * ! I NOMI CHE EX-OS NON RITORNERA' MAI
 *
 * Tutto quello che segue riguarda cose che questo sistema NON HA: socket,
 * rete, blocchi sui file, code di messaggi. Nessuna syscall di EX-OS
 * ritornera' mai uno di questi numeri, e non e' previsto che accada.
 *
 * CI SONO PERCHE' SERVONO A ESSERE NOMINATI, non a essere ritornati. La
 * libstdc++ costruisce l'enumerazione `std::errc` da questo elenco
 * (bits/error_constants.h): ogni nome mancante e' un errore di
 * compilazione, e senza `std::errc` non c'e' <system_error>, che sta sotto
 * a mezza libreria standard. Lo stesso vale per il codice di terzi che
 * scrive `case EWOULDBLOCK:` in uno switch: gli serve la costante, non il
 * comportamento.
 *
 * I valori sono quelli di Linux, come tutto il resto della numerazione.
 * ! Se un giorno EX-OS avra' i socket, questi numeri sono gia' quelli
 * giusti e non vanno rinumerati — e' il motivo per cui si copiano invece
 * di inventarli.
 * ============================================================================= */
#define EDEADLK          35
#define ENOLCK           37
#define ENOMSG           42
#define ENOTSOCK         88
#define EDESTADDRREQ     89
#define EMSGSIZE         90
#define EPROTOTYPE       91
#define ENOPROTOOPT      92
#define EPROTONOSUPPORT  93
#define EOPNOTSUPP       95
#define EAFNOSUPPORT     97
#define EADDRINUSE       98
#define EADDRNOTAVAIL    99
#define ENETDOWN        100
#define ENETUNREACH     101
#define ENETRESET       102
#define ECONNABORTED    103
#define ECONNRESET      104
#define ENOBUFS         105
#define EISCONN         106
#define ENOTCONN        107
#define ECONNREFUSED    111
#define EHOSTUNREACH    113
#define EALREADY        114
#define EINPROGRESS     115
/* ! Su Linux EWOULDBLOCK E' EAGAIN, stesso numero: sono due nomi per la
 * stessa condizione. Definirlo con un valore proprio romperebbe ogni
 * `if (errno == EAGAIN || errno == EWOULDBLOCK)` — che e' il modo in cui
 * il codice portabile si difende dai sistemi dove invece differiscono. */
#define EWOULDBLOCK     EAGAIN

/* Il resto di cio' che <system_error> nomina: code di messaggi System V,
 * flussi STREAMS, mutex robusti. Stessa regola dei precedenti — servono a
 * essere nominati, non a essere ritornati. */
#define EIDRM            43
#define ETXTBSY          26
#define EOVERFLOW        75
#define ENOTSUP     EOPNOTSUPP   /* due nomi per la stessa cosa, come su Linux */
#define ECANCELED       125
#define EOWNERDEAD      130
#define ENOTRECOVERABLE 131
#define ENODATA          61
#define ENOSR            63
#define ENOSTR           60
#define ETIME            62
#define EBADMSG          74
#define ENOLINK          67
#define EPROTO           71

/* =============================================================================
 * PROCESSI — spawn con ambiente e redirezioni
 *
 * ! SpawnAzione/SpawnExtra sono duplicate da kernel/include/syscall.h (la
 * stessa convenzione di DirEntry, MemInfo e i numeri di syscall): le due
 * copie DEVONO restare identiche, perche' e' la struttura che attraversa
 * la syscall. La magia esiste per non far leggere ESI ai programmi
 * compilati per la vecchia forma a tre argomenti — vedi il commento nel
 * kernel.
 * ============================================================================= */
/* ! UNA DEFINIZIONE SOLA dal 17 agosto 2026: ce n'erano quattro e una e'
 * rimasta indietro, costando alla shell redirezioni e ambiente per tre
 * giorni senza un messaggio. Vedi lib/include/spawn_abi.h. */
#include "spawn_abi.h"

/* La forma comoda per chi chiama: percorso invece di buffer a lunghezza
 * fissa, e nessuna magia da ricordare.
 *
 * ! CON `percorso` NON NULL il figlio APRE quel file; con `percorso`
 * NULL riceve invece il descrittore `fd_padre` di chi lo lancia — ed e'
 * cosi' che si costruisce `cmd1 | cmd2`:
 *
 *     int p[2]; pipe(p);
 *     SpawnRedir a = { 1, 0, NULL, p[1] };   // stdout del figlio = scrittura
 *     spawn_ex("/bin/cmd1", argv, environ, &a, 1);
 *     close(p[1]);                            // ! vedi sotto
 *
 * ! IL PADRE DEVE CHIUDERE L'ESTREMITA' CHE HA PASSATO. Se non lo fa, la
 * pipe conta ancora uno scrittore vivo — lui — e chi legge non vedra' mai
 * la fine dei dati: aspettera' per sempre byte che nessuno scrivera'. E'
 * l'errore classico con le pipe, e qui non c'e' niente che lo segnali. */
typedef struct {
    int         fd;             /* descrittore del figlio da sostituire */
    int         flags;          /* O_WRONLY | O_CREAT | O_TRUNC, ... */
    const char *percorso;       /* NULL = passa un descrittore, vedi sotto */
    int         fd_padre;       /* usato solo se `percorso` e' NULL */
} SpawnRedir;

/* Lancia `path` e ritorna il PID del figlio, o un errno negativo. Non
 * aspetta: per quello c'e' waitpid(). spawn() passa l'ambiente corrente e
 * nessuna redirezione. */
/* =============================================================================
 * pipe — un tubo di byte, e le tre regole che lo fanno funzionare
 *
 * `fd[0]` legge, `fd[1]` scrive. Ritorna 0, o -1 con errno.
 *
 * ! 1. LEGGERE DA UNA PIPE VUOTA CON UNO SCRITTORE VIVO BLOCCA, non
 *       ritorna 0. Zero significa «non arrivera' piu' niente», e si ha
 *       solo quando l'ultima estremita' di scrittura e' stata chiusa.
 *
 * ! 2. SCRIVERE SENZA PIU' LETTORI da -1 con EPIPE. Su Unix arriverebbe
 *       anche SIGPIPE, che EX-OS non ha: chi non guarda il valore di
 *       ritorno di write() non se ne accorge.
 *
 * ! 3. LA SCRITTURA PUO' ESSERE PARZIALE. write() ritorna quanti byte ha
 *       preso, e su un buffer da 4 KB una scrittura piu' grande ne prende
 *       una parte: il chiamante deve richiamare. Non c'e' la garanzia di
 *       atomicita' di POSIX per le scritture sotto PIPE_BUF.
 *
 * PER COLLEGARE DUE PROCESSI serve passare un'estremita' al figlio, e lo
 * si fa con SpawnRedir a `percorso` NULL — vedi il suo commento piu'
 * avanti. ! E il padre DEVE chiudere l'estremita' che ha passato, o chi
 * legge non vedra' mai la fine dei dati.
 * ============================================================================= */
int     pipe(int fd[2]);

/* =============================================================================
 * LA VERSIONE DI UNO STRUMENTO — dichiarata una volta, stampata da sola
 *
 * Ogni programma di EX-OS porta una versione propria, e la regola e' quella del
 * kernel: **+0.001 a ogni modifica**, tre decimali sempre presenti.
 *
 *     EX_VERSIONE("browser", "0.001");     in cima al file, fuori da main
 *
 * Da quel momento `browser -version` stampa «browser 0.001» ed esce, e le
 * applicazioni grafiche passano gli stessi due pezzi a exinfo_testo(), che li
 * mostra in «Informazioni su».
 *
 * ! IL PROGRAMMA NON GUARDA argv, E NON DEVE. Se ognuno si scrivesse il proprio
 * `if (strcmp(argv[i], "-version"))` sarebbero cinquantanove copie della stessa
 * riga: la prima volta uguali, poi una stampa il nome e un'altra no, una esce
 * con 0 e un'altra con 1, e tre si dimenticano l'opzione nella pagina d'uso.
 * Se ne occupa l'avvio della libc (lib/libc_avvio.c), che vede argv prima di
 * main.
 *
 * ! E SI ACCENDE SOLO SE IL PROGRAMMA LA DICHIARA. I due simboli sono DEBOLI:
 * chi non usa la macro non ha `-version`, e il codice di terzi — `make`, `gcc`,
 * `fbc`, che si collegano alla nostra libc e hanno un `--version` loro — non se
 * ne accorge nemmeno.
 *
 * ! LA PAROLA INTERA, NON `-v`. Una lettera sola era gia' presa da tre
 * programmi con tre significati diversi: «parla di piu'» in sshd e telnetd,
 * «visualizza il file» in textline. Una regola che vale per tutti tranne tre
 * non e' una regola — e chi legge `-version` in uno script capisce cosa chiede
 * senza andare a vedere il programma.
 *
 * ! E DEV'ESSERE L'UNICO ARGOMENTO: `prog -version` e' una domanda,
 * `prog -version qualcosa` e' un comando malformato.
 * ============================================================================= */
#define EX_VERSIONE(nome, versione)                 \
    const char *const __ex_nome     = (nome);       \
    const char *const __ex_versione = (versione)

int     spawn(const char *path, char *const argv[]);
int     spawn_ex(const char *path, char *const argv[], char *const envp[],
                 const SpawnRedir *redir, int n_redir);

/* Come spawn_ex(), ma il figlio nasce sulla CONSOLE indicata invece che su
 * quella del padre. `console` negativa = quella del padre, cioe' spawn_ex().
 *
 * ! SERVE AL SERVER GRAFICO, ed e' il motivo per cui esiste: deve poter stare
 * su una console tutta sua mentre su un'altra continua a girare una shell, e
 * con Alt+Fn si passa dall'una all'altra. Senza, il server nasce dove gira chi
 * l'ha lanciato — addosso alla shell — e i due si contendono lo schermo.
 *
 * ! E' UNA FUNZIONE IN PIU', NON UN PARAMETRO IN PIU' A spawn_ex(). Cambiare
 * la firma di una funzione che i programmi gia' chiamano vuol dire ricostruire
 * tutto cio' che la usa, ed e' esattamente cio' che il blocco EXTRA di
 * SYS_SPAWN e' stato inventato per evitare. */
int     spawn_su_console(const char *path, char *const argv[],
                         char *const envp[], const SpawnRedir *redir,
                         int n_redir, int console);

/* Come spawn_ex(), ma il figlio nasce con l'IDENTITA' indicata invece che con
 * quella di chi lo lancia.
 *
 * ! SOLO root PUO' CHIAMARLA: a chiunque altro risponde -1 con EPERM, e non
 * lancia niente. E' la stessa regola di setuid() — l'unica direzione possibile
 * e' giu' — spostata dal processo che chiama al processo che nasce.
 *
 * ! SERVE A CHI DEVE RESTARE root MENTRE APRE UNA SESSIONE ALTRUI. `sudo` non
 * ne ha bisogno: sale, esegue e muore. `login` si': e' un ciclo, e scendendo
 * con setuid() non poteva piu' leggere /boot/ombra per l'accesso successivo —
 * al primo `exit` la console restava chiusa per tutti. */
int     spawn_utente(const char *path, char *const argv[], char *const envp[],
                     unsigned int uid, unsigned int gid);
int     waitpid(int pid, int *stato, int opzioni);
int     wait(int *stato);

/* =============================================================================
 * Lo stato di un figlio — <sys/wait.h>
 *
 * ! NON E' LA CODIFICA DI UNIX, e la differenza va detta perche' e'
 * invisibile finche' non fa danno. Su Unix `*stato` impacchetta due cose
 * in un intero — codice di uscita negli otto bit ALTI, numero del segnale
 * negli otto bassi — perche' un processo puo' finire in due modi. Qui il
 * modo e' uno solo: EX-OS non consegna segnali, quindi il kernel scrive il
 * codice di uscita e basta.
 *
 * Le macro ci sono lo stesso perche' il codice di terzi le scrive sempre —
 * `if (WIFEXITED(s) && WEXITSTATUS(s) != 0)` e' la forma canonica — e
 * dicono la verita' di questo sistema: si esce sempre "normalmente",
 * nessuno muore mai di segnale. Chi legge `*stato` direttamente (la shell
 * di EX-OS lo fa) ci trova il codice, non il codice spostato di otto bit.
 * ============================================================================= */
#define WNOHANG         0x0001
#define WUNTRACED       0x0002

#define WIFEXITED(s)    ((void)(s), 1)
#define WEXITSTATUS(s)  ((s) & 0xFF)
#define WIFSIGNALED(s)  ((void)(s), 0)
#define WTERMSIG(s)     ((void)(s), 0)
#define WIFSTOPPED(s)   ((void)(s), 0)
#define WSTOPSIG(s)     ((void)(s), 0)
#define WCOREDUMP(s)    ((void)(s), 0)

/* =============================================================================
 * AMBIENTE
 *
 * environ e' quello che il padre ha passato. getenv() ripiega sulla
 * sezione [env] di /boot/kernel.cfg per le chiavi che non trova: senza
 * quel ripiego il primo processo — che il padre non ce l'ha — resterebbe
 * senza PATH. Vedi il commento in lib/libc.c.
 * ============================================================================= */
#define environ (*__environ_dove())

int     putenv(char *voce);          /* la voce ENTRA nell'ambiente, senza copia */
int     setenv(const char *nome, const char *valore, int sovrascrivi);
int     unsetenv(const char *nome);

/* =============================================================================
 * DIRECTORY nella forma POSIX (sopra listdir)
 * ============================================================================= */
/* I tipi di voce di directory, con i valori di Linux.
 *
 * ! EX-OS NE RITORNA SOLO TRE: DT_DIR, DT_REG e DT_UNKNOWN. Gli altri
 * quattro riguardano cose che questo sistema non ha — FIFO, dispositivi a
 * caratteri e a blocchi, collegamenti simbolici, socket — e nessuna
 * readdir() ne restituira' mai uno.
 *
 * Ci sono per lo stesso motivo dei codici errno della rete: **servono a
 * essere nominati**. La <filesystem> del C++17 fa `case DT_LNK:` in uno
 * switch (libstdc++-v3/src/filesystem/dir-common.h), e un nome mancante e'
 * un errore di compilazione — non un ramo che non verra' mai preso.
 *
 * ! I valori sono quelli di Linux e non vanno reinventati: il giorno che
 * EX-OS avesse i collegamenti simbolici, DT_LNK dovra' valere 10 come
 * ovunque, o ogni programma portato di la' leggerebbe il tipo sbagliato. */
#define DT_UNKNOWN   0
#define DT_FIFO      1
#define DT_CHR       2
#define DT_DIR       4
#define DT_BLK       6
#define DT_REG       8
#define DT_LNK      10
#define DT_SOCK     12
#define DT_WHT      14

struct dirent {
    unsigned int  d_ino;
    unsigned char d_type;
    char          d_name[DIRENT_NAME_MAX];
};

typedef struct __dir DIR;

DIR           *opendir(const char *path);
struct dirent *readdir(DIR *d);
void           rewinddir(DIR *d);
int            closedir(DIR *d);

/* =============================================================================
 * File temporanei, interrogazioni, rinomina
 * ============================================================================= */
#define F_OK    0
#define X_OK    1
#define W_OK    2
#define R_OK    4

char   *tmpnam(char *buf);
int     mkstemp(char *modello);      /* il modello finisce per XXXXXX */
/* ! mktemp NON crea il file: da' solo il nome, e fra il nome e l'uso ci
 * puo' entrare qualcun altro. E' cosi' su ogni Unix — si usi mkstemp. */
char   *mktemp(char *modello);
FILE   *tmpfile(void);
int     access(const char *path, int modo);
int     isatty(int fd);
/* Cambia il NOME di un file senza spostarne i dati (dalla 0.161: prima
 * copiava e cancellava). ! DUE DIFFERENZE DA POSIX: solo nella STESSA
 * directory e nello stesso montaggio (ENOSYS per il resto), e NON
 * sostituisce la destinazione (EEXIST). La garanzia in cambio e' che i
 * blocchi non si spostano — vedi lib/libc.c. */
int     rename(const char *da, const char *a);
int     atexit(void (*fn)(void));

/* =============================================================================
 * SEGNALI — ci sono i nomi, non c'e' la consegna. Vedi lib/libc.c.
 * ============================================================================= */
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
/* ! NSIG e' lo STESSO numero di SIG_MAX con il nome che usa il codice di
 * terzi: e' quello con cui si dimensiona una tabella di gestori, e
 * src/rtlib/signals.c di FreeBASIC fa esattamente `sigTb[NSIG]`. Due nomi
 * per una cosa sola non e' bello, ma inventarne un terzo lo sarebbe meno. */
#define NSIG    SIG_MAX

/* ! Il tipo su cui si puo' scrivere e leggere ATOMICAMENTE rispetto a un
 * gestore di segnale — l'unica cosa che lo standard C permette di toccare
 * da dentro un handler, insieme alle funzioni async-signal-safe.
 *
 * Su i386 un `int` allineato si legge e si scrive con una sola istruzione,
 * quindi `int` e' la risposta giusta e non un ripiego. `volatile` NON fa
 * parte del typedef, come su ogni altro sistema: lo mette chi dichiara la
 * variabile (`static volatile sig_atomic_t flag;`), perche' e' la variabile
 * a dover essere volatile, non il tipo.
 *
 * C'e' perche' <csignal> della libstdc++ fa `using ::sig_atomic_t;`. */
typedef int sig_atomic_t;

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

void  (*signal(int sig, void (*gestore)(int)))(int);
int     raise(int sig);
char *strsignal(int sig);

/* =============================================================================
 * Localizzazione (solo "C") e interrogazioni sul sistema
 * ============================================================================= */
#define LC_ALL       0
#define LC_COLLATE   1
#define LC_CTYPE     2
#define LC_MONETARY  3
#define LC_NUMERIC   4
#define LC_TIME      5
#define LC_MESSAGES  6

char   *setlocale(int categoria, const char *nome);

/* =============================================================================
 * struct lconv, localeconv — le convenzioni numeriche della locale
 *
 * Esiste solo la locale "C", quindi i valori sono quelli che lo standard
 * prescrive per lei: il punto come separatore decimale e **tutto il resto
 * vuoto**. ! I campi non impostati valgono `CHAR_MAX`, che significa
 * «questa locale non lo specifica» — NON zero, che vorrebbe dire «zero
 * cifre». E' la distinzione su cui sbaglia chi la implementa a memoria, e
 * un programma che raggruppa le migliaia leggendo `grouping` la vede
 * subito: con zero stamperebbe gruppi vuoti all'infinito.
 * ============================================================================= */
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

/* ! Ritorna un puntatore a una struttura STATICA che non va modificata:
 * e' l'interfaccia dello standard, non una scorciatoia. */
struct lconv *localeconv(void);

#define _SC_ARG_MAX             0
#define _SC_OPEN_MAX            4
#define _SC_PAGESIZE            30
#define _SC_CLK_TCK             2
#define _SC_NPROCESSORS_ONLN    84

long    sysconf(int nome);

/* La dimensione della pagina, con il nome che le da' BSD. E' `sysconf
 * (_SC_PAGESIZE)` scritto piu' corto, e c'e' perche' il codice di terzi
 * usa l'uno o l'altro senza criterio — ggc-page.cc di GCC usa questo. */
int     getpagesize(void);

/* =============================================================================
 * mmap, munmap — memoria a pagine dal kernel
 *
 * ! SOLO MEMORIA ANONIMA. EX-OS non sa mappare un file: `fd` deve essere
 * -1 e `flags` deve contenere MAP_ANONYMOUS, altrimenti si prende ENODEV.
 * Mappare un file vorrebbe dire pagine sporche da riscrivere al momento
 * giusto, cioe' un pezzo di gestore della memoria che non c'e' — e una
 * mmap che finge di mappare un file consegnando zeri sarebbe il genere di
 * bugia che questo progetto rifiuta.
 *
 * ! SU FALLIMENTO RITORNA MAP_FAILED, cioe' (void *)-1, NON NULL. E' la
 * convenzione di POSIX, ed e' il modo classico di sbagliare a usarla:
 * `if (p == NULL)` non si accorge di niente.
 *
 * Chi ha solo bisogno di memoria usi malloc: questa serve a chi vuole
 * pagine intere e allineate, per esempio un garbage collector che ragiona
 * per pagine — ggc-page.cc di GCC fa esattamente cosi'.
 * ============================================================================= */
#define PROT_NONE       0x0
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_ANON        MAP_ANONYMOUS
#define MAP_FAILED      ((void *)-1)

void   *mmap(void *addr, size_t lung, int prot, int flags, int fd, long off);
int     munmap(void *addr, size_t lung);

/* pathconf: gli stessi limiti, ma riferiti a un file.
 * ! NON dipendono dal percorso: su EX-OS convivono quattro filesystem
 * con limiti diversi, e si risponde il massimo del piu' generoso — la
 * risposta prudente per chi dimensiona un buffer, quella sbagliata per
 * chi verifica se un nome ci sta. Vedi lib/libc.c. */
#define _PC_LINK_MAX            0
#define _PC_NAME_MAX            3
#define _PC_PATH_MAX            4
#define _PC_CHOWN_RESTRICTED    6
#define _PC_NO_TRUNC            7

long    pathconf(const char *path, int nome);
long    fpathconf(int fd, int nome);

typedef long clock_t;

struct tms {
    clock_t tms_utime, tms_stime, tms_cutime, tms_cstime;
};

clock_t times(struct tms *t);
clock_t clock(void);

/* =============================================================================
 * getrusage — quanto ha consumato il processo
 *
 * ! EX-OS NON HA CONTABILITA' PER PROCESSO. Non c'e' un contatore di tick
 * spesi in modo utente e in modo kernel: lo scheduler assegna quanti e non
 * misura consumi. Quindi:
 *
 *   - `ru_utime` riporta il tempo TRASCORSO dall'avvio del sistema, non il
 *     tempo di CPU di questo processo. E' un limite superiore onesto, non
 *     una misura;
 *   - `ru_stime` e tutti gli altri campi valgono ZERO.
 *
 * Si dichiara perche' GCC la chiama con -ftime-report e perche' `times()`
 * da sola non basta a chi si aspetta questa forma. ! Chi ci costruisce
 * sopra un profilo otterra' numeri privi di significato: e' scritto qui,
 * ed e' scritto anche in lib/libc.c sopra l'implementazione.
 *
 * La struttura ha tutti i campi di POSIX anche se ne riempiamo due: chi
 * legge `ru_maxrss` deve poterlo scrivere senza che il codice non compili.
 * ============================================================================= */
#define RUSAGE_SELF      0
#define RUSAGE_CHILDREN (-1)

struct rusage {
    struct timeval ru_utime;    /* tempo in modo utente   — vedi sopra */
    struct timeval ru_stime;    /* tempo in modo kernel   — sempre 0 */
    long ru_maxrss;             /* tutti i campi seguenti sono 0 */
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

int getrusage(int chi, struct rusage *uso);
#define CLOCKS_PER_SEC  100

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
/* Payload massimo di un messaggio. Vale 1536 e non 512 perché un frame
 * Ethernet arriva a 1514 byte e un driver di rete deve poterlo
 * consegnare intero — il perché per esteso sta in kernel/include/sched.h. */
#define IPC_MSG_MAX_DATA    1536
#define IPC_NAME_LEN        16

typedef struct {
    unsigned int  sender_pid;
    /* ! IL CAMPO SI CHIAMA `tipo` E NON `type`, e non e' un vezzo linguistico.
     *
     * `type` e' una parola che il codice di terzi definisce come MACRO. Non e'
     * un caso limite: openlibm scrive `#define type float` in cima a
     * s_lroundf.c e poi include gli header di sistema, e a quel punto questa
     * riga diventa `unsigned int float` — un errore che il compilatore segnala
     * SULLA DEFINIZIONE DELLA MACRO, in un file di terzi, senza nominare mai
     * il nostro header:
     *
     *     src/s_lroundf.c:4:25: error: two or more data types in declaration
     *          4 | #define type            float
     *
     * Un header pubblico e' incluso da codice che non conosciamo, quindi non
     * puo' usare parole comuni come nomi di campo. Le altre della stessa
     * famiglia — `class`, `new`, `delete`, `template` — qui non ci sono, ed e'
     * stato verificato: le userebbe il C++ come parole chiave, e libstdc++
     * include questo file. */
    unsigned int  tipo;
    unsigned int  len;
    /* ! QUI NON C'E' NESSUN data[], E NON E' UNA DIMENTICANZA.
     *
     * Questa struttura serve solo a ricevere l'INTESTAZIONE di un
     * messaggio: ipc_recv scrive il payload nel buffer separato che gli
     * si passa, e non ha mai scritto niente qui dentro. Fino ad agosto
     * 2026 c'era comunque un array di 512 byte, copiato dal kernel per
     * intero a ogni ricezione — 512 byte di traffico e di stack per un
     * campo che nessuno leggeva.
     *
     * Toglierlo è servito a poter alzare il limite dei messaggi a 1536
     * senza che ogni driver si portasse 1.5 KB di stack in più per
     * niente. Il costo del frame Ethernet lo paga la mailbox nel kernel,
     * dove serve, e non chi la legge. */
} IpcMessage;

/* Invia un messaggio a dest_pid. data può essere NULL se len=0.
 * Ritorna 0 su successo, <0 su errore (-ESRCH se dest_pid non esiste,
 * -EBUSY se la mailbox del destinatario resta piena troppo a lungo). */
int     ipc_send(unsigned int dest_pid, unsigned int tipo,
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

/* =============================================================================
 * ipc_scegli — aspettare il PROPRIO messaggio quando la cassetta e' di due
 *
 * ! LA MAILBOX E' UNA SOLA E I CONSUMATORI SONO PIU' D'UNO. Un'applicazione
 * grafica riceve nella stessa coda gli eventi del server a finestre E le
 * risposte dello stack IP; e non si puo' «non leggere» un messaggio, perche'
 * ipc_recv lo toglie dalla coda del kernel e non c'e' modo di sbirciare.
 *
 * Chi aspetta passa un FILTRO, che di ogni messaggio dice una di tre cose:
 *
 *     IPC_MIO      e' quello che aspettavo: consegnamelo
 *     IPC_ALTRUI   non e' mio: lascialo dov'e', lo aspetta un altro
 *     IPC_BUTTA    non e' di nessuno: e' una risposta vecchia, si butta
 *
 * ! CIO' CHE E' ALTRUI NON PASSA MAI PER LE MANI DI CHI NON LO VUOLE, ed e'
 * l'intero punto: resta sullo scaffale della libc, in ordine d'arrivo, finche'
 * non viene a prenderselo il suo padrone. Chi legge non puo' perdere quel che
 * non ha mai tenuto in mano.
 *
 * ! E NON SI RIMETTE NIENTE: un ALTRUI si SALTA. Rimettere e rileggere subito
 * rende lo stesso messaggio all'infinito, perche' lo scaffale si serve prima
 * della coda del kernel — la trappola che questa funzione toglie di mezzo.
 *
 * Rende i byte consegnati (>= 0), -ETIMEDOUT se scade, o l'errore del kernel.
 * `ms == 0` vuol dire attesa senza scadenza, come in tutta questa libc, e la
 * scadenza vale per TUTTA la chiamata, non per ogni singola lettura.
 *
 * ! E `ms == IPC_SUBITO` VUOL DIRE «SOLO QUEL CHE C'E' GIA'», che non e' la
 * stessa cosa di una scadenza corta: la piu' corta che il kernel sappia
 * rappresentare e' un tick del PIT, dieci millisecondi. Chi guarda la posta
 * mentre aspetta altro — la pompa dei messaggi di una finestra durante uno
 * scaricamento — la guarda otto volte per giro, e otto tick sono ottanta
 * millisecondi buttati a ogni giro.
 * ============================================================================= */
#define IPC_ALTRUI   0
#define IPC_MIO      1
#define IPC_BUTTA  (-1)

/* Una scadenza che vuol dire «non aspettare affatto». */
#define IPC_SUBITO  ((unsigned int)-1)

typedef int (*IpcFiltro)(const IpcMessage *meta, void *dato);

int     ipc_scegli(IpcFiltro filtro, void *dato, IpcMessage *out_meta,
                   void *buf, unsigned int buf_len, unsigned int ms);

/* Quanti messaggi sono gia' sullo scaffale, cioe' quanti se ne possono avere
 * senza chiedere niente al kernel.
 *
 * ! SERVE A CHI DORME SU poll(): poll guarda la coda DEL KERNEL, e un messaggio
 * gia' messo da parte non la fa scattare. Senza questo controllo un evento
 * lasciato sullo scaffale mentre si scaricava una pagina resterebbe li' fino al
 * prossimo messaggio che arriva davvero — su uno schermo fermo, per sempre. */
unsigned int ipc_pronto(void);

/* Mette un messaggio sullo scaffale, come fa ipc_scegli con un IPC_ALTRUI.
 *
 * ! E' LA PRIMITIVA SOTTO ipc_scegli, E QUASI SEMPRE SI VUOLE QUELLA. Chi la
 * chiama a mano deve ricordarsi che lo scaffale si serve PRIMA della coda del
 * kernel: rimettere un messaggio e rileggere subito vuol dire ritrovarselo
 * davanti all'infinito. E' il difetto che ha tenuto ferma la pompa dei messaggi
 * del navigatore per tutto il tempo che uno stack IP aveva una risposta da
 * parte.
 *
 * Rende 0, oppure -1 se lo scaffale e' pieno — e in quel caso il messaggio si
 * perde. */
int     ipc_rimetti(const IpcMessage *meta, const void *dati, unsigned int len);

/* Registra il chiamante come fornitore del servizio 'name' (es. "tty",
 * "floppy"). Ritorna 0 su successo, <0 se il nome è già in uso. */
int     ipc_register(const char *name);

/* Cerca il PID del processo che fornisce 'name'.
 * Ritorna il PID (>0) su successo, <0 se non trovato. */
int     ipc_lookup(const char *name);

/* Come ipc_lookup(), ma riprova per `ms` millisecondi prima di arrendersi.
 *
 * ! SERVE A CHI PARTE DA [modules] DI kernel.cfg: quei processi il kernel li
 * avvia TUTTI INSIEME, quindi chi dipende da un altro puo' arrivare prima che
 * quello si sia registrato. In uno script l'ordine lo garantiva chi l'ha
 * scritto; all'avvio non lo garantisce nessuno, e un driver che esce perche'
 * era in anticipo di mezzo secondo lascia la rete spenta fino al riavvio
 * successivo — dove magari funziona. */
int     ipc_attendi(const char *name, unsigned int ms);

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

/* ! OBBLIGATORIA DOPO OGNI NOTIFICA DI IRQ. Il kernel maschera la linea
 * PRIMA di consegnare la notifica, e finché non si chiama questa resta
 * chiusa: il driver riceverebbe un solo interrupt e poi silenzio.
 *
 * Non è burocrazia. Un driver ring3 non gira dentro l'interrupt: fra la
 * notifica e il momento in cui tocca la scheda passano dei tick. Su un
 * IRQ a livello — tutti quelli PCI — la scheda tiene la linea alta finché
 * non le si azzera il registro di stato, quindi senza mascheramento
 * l'interrupt riparte subito e il processo driver non riceve mai la CPU
 * per andare ad azzerarlo. La macchina si ferma.
 *
 * Si chiama DOPO aver azzerato lo stato della scheda, mai prima. */
int     irq_done(unsigned int irq);

/* Restituisce una linea rivendicata con irq_bind(). ! SERVE SOLO A UNA
 * SONDA: chi cerca su quale IRQ e' cablata una scheda che non lo dichiara
 * — una Sound Blaster anteriore alla 16 — ne rivendica quattro, ne scopre
 * una e deve poter ridare indietro le altre tre. NON e' un «chiudi» da
 * chiamare prima di uscire: alla morte del processo le rilascia tutte il
 * kernel, e un driver vivo che restituisce la linea del proprio hardware
 * sta solo spegnendolo senza dirlo. Rende 0, -EPERM se non e' tua. */
int     irq_unbind(unsigned int irq);

/* =============================================================================
 * fdprova — la prova passo passo del drive floppy
 *
 * Rende quanti passi hanno dato problemi (0 = nessuno), oppure -EPERM se non
 * la chiede root.
 *
 * ! I PASSI LI STAMPA IL KERNEL mentre la prova va avanti, non questa
 * funzione: meta' della prova la fa l'orecchio di chi sta davanti alla
 * macchina, e «adesso la testina va a cilindro 40» va letto PRIMA del rumore.
 * Chi chiama stampa il verdetto, non la cronaca. */
/* =============================================================================
 * La prova passo passo del drive floppy
 *
 * Riempie fino a `max` passi e rende quanti ne ha registrati, oppure -EPERM
 * se non la chiede root. Con `passi` NULL la prova si fa lo stesso.
 *
 * ! I PASSI SI ANNUNCIANO A SCHERMO MENTRE LA PROVA GIRA, e li stampa il
 * kernel: meta' della prova la fa l'orecchio di chi sta davanti alla macchina.
 * Questi dati servono a scriverne un log, non a raccontarla.
 *
 * ! FdPasso DEVE RESTARE IDENTICA a quella in kernel/include/syscall.h.
 * ============================================================================= */
#define FD_MSR          1
#define FD_RESET_IRQ    2
#define FD_SENSE        3
#define FD_MOTORE_ON    4
#define FD_RECAL        5
#define FD_SEEK         6
#define FD_LETTURA      7
#define FD_RIPETUTA     8
#define FD_MOTORE_OFF   9

typedef struct {
    unsigned int passo;
    unsigned int codice;
    int          esito;
    unsigned int a, b;
} FdPasso;

#define FD_RIACCESO    10

#define KB_OCCUPATO     20
#define KB_STATO        21
#define KB_SELFTEST     22
#define KB_PORTA        23
#define KB_CONFIG       24
#define KB_RESET        25
#define KB_SCANSIONE    26
#define KB_PIC          27
#define KB_TASTI        28

int     fdprova(FdPasso *passi, unsigned int max);

/* La prova passo passo della tastiera. Stesso contratto di fdprova().
 * ! CHIEDE CHE /dev/kbd.drv NON SIA CARICATO: legge il buffer dell'8042 a
 * tappeto, e due lettori si rubano i byte a vicenda. Si esegue da
 * dist/diagnostic.img, che i moduli non li carica. */
int     kbprova(FdPasso *passi, unsigned int max);

/* Solo lo stato: quale delle due strade serve la tastiera, se il servizio
 * 'kbd' e' registrato, chi ha l'IRQ1, se la linea e' aperta nel PIC.
 * ! QUESTA SI PUO' CHIAMARE CON kbd.drv CARICATO — guarda variabili del
 * kernel e non parla con l'8042, quindi non ruba byte a nessuno. */
int     kbstato(FdPasso *passi, unsigned int max);
#define KB_SORGENTE     29

/* =============================================================================
 * Memoria per un bus master — vedi kernel/include/syscall.h per il perche'
 *
 * ! DEVE RESTARE IDENTICA a DmaZona in kernel/include/syscall.h: la
 * struttura la riempie il kernel scrivendo nella memoria del processo, e
 * due definizioni che divergono danno un indirizzo fisico letto dal campo
 * sbagliato — cioe' una scheda che fa DMA dove capita.
 * ============================================================================= */
typedef struct {
    unsigned int byte;      /* in:  quanti byte servono */
    unsigned int virt;      /* out: indirizzo nel processo */
    unsigned int fisico;    /* out: indirizzo da dare alla scheda */
} DmaZona;

int     dma_alloc(DmaZona *z);

/* =============================================================================
 * mmio_map — i registri di un dispositivo, dentro il proprio processo
 *
 * `fisico` e `byte` descrivono la finestra che la scheda decodifica, e si
 * leggono da un BAR PCI (PciDispositivo.bar[i] con bar_io[i] == 0). In
 * uscita `virt` e' l'indirizzo con cui il processo la vede.
 *
 *     MmioZona m;
 *     m.fisico = d.bar[0];
 *     m.byte   = 0x20000;
 *     if (mmio_map(&m) == 0) reg = (volatile unsigned int *)m.virt;
 *
 * ! IL PUNTATORE VA `volatile`, SEMPRE. Un registro cambia senza che
 * nessuno scriva, e leggerlo ha effetti: senza volatile il compilatore
 * tiene il valore in un registro della CPU, ripete la lettura solo quando
 * gli pare, e un ciclo che aspetta un bit non finisce mai.
 *
 * ! SOLO PER I DRIVER, e «driver» vuol dire una cosa precisa: l'eseguibile
 * dev'essere stato caricato da un file che si chiama <qualcosa>.drv.
 * Altrimenti -EPERM. Non c'e' niente da chiamare per ottenere il permesso —
 * lo decide il kernel guardando il nome, quindi un programma che vuole
 * guidare dell'hardware va CHIAMATO in un modo, non corretto.
 *
 * E la finestra deve stare SOPRA la RAM: chiedere un indirizzo che e' memoria
 * normale da' -EPERM lo stesso, perche' sarebbe un buco verso il kernel e non
 * un servizio.
 *
 * Le pagine sono NON CACHEABILI d'ufficio, e non c'e' modo di chiederle
 * altrimenti: con la cache accesa le scritture ai registri resterebbero
 * nella cache e le letture renderebbero valori vecchi.
 *
 * ! NON SI SMAPPA. La finestra torna al sistema quando il processo muore.
 *
 * STRUTTURA DUPLICATA A MANO da kernel/include/syscall.h e ripetuta in
 * lib/libc.c, che non include questo file: TRE copie, tutte identiche.
 * ============================================================================= */
typedef struct {
    unsigned int fisico;    /* in:  base della finestra, da un BAR PCI */
    unsigned int byte;      /* in:  quanto e' larga */
    unsigned int virt;      /* out: dove il processo la vede */
} MmioZona;

int     mmio_map(MmioZona *m);

/* =============================================================================
 * video_info — dov'e' il framebuffer e che forma ha
 *
 * ! SENZA QUESTO, mmio_map() NON BASTA A DISEGNARE. Sa mappare una finestra
 * fisica qualunque, ma l'indirizzo del framebuffer lo conosce solo il kernel:
 * gliel'ha dato Stage 2, l'unico che poteva chiederlo al BIOS in modo reale.
 * Senza, un server grafico dovrebbe INDOVINARE un indirizzo fisico.
 *
 * ! larghezza == 0 VUOL DIRE MODO TESTO, e non e' un errore: vuol dire che
 * nessuno ha chiesto la grafica, o che la scheda non offriva la risoluzione
 * richiesta e Stage 2 ha ripiegato sul testo.
 *
 * ! NON CONCEDE NIENTE, DICE DEI NUMERI, quindi non e' riservata ai driver:
 * mappare resta un privilegio da .drv, e chi non lo e' con questi numeri non
 * ci fa niente.
 *
 *     VideoInfo v;
 *     if (video_info(&v) == 0 && v.larghezza) {
 *         MmioZona m;
 *         m.fisico = v.fisico;
 *         m.byte   = v.passo * v.altezza;
 *         mmio_map(&m);              // solo da un .drv
 *     }
 *
 * STRUTTURA DUPLICATA A MANO da kernel/include/syscall.h e da lib/libc.c:
 * TRE copie, tutte identiche. Per questo sta in tools/abi-bersaglio.c.
 * ============================================================================= */
typedef struct {
    unsigned int fisico;    /* out: indirizzo fisico, 0 = modo testo */
    unsigned int passo;     /* out: byte per riga di scansione */
    unsigned int larghezza; /* out: pixel */
    unsigned int altezza;   /* out: pixel */
    unsigned int bit;       /* out: bit per pixel: 16, 24 o 32 */
} VideoInfo;

int     video_info(VideoInfo *v);

/* =============================================================================
 * log_seriale — una riga che arriva anche se nessuno guarda la tua console
 *
 * ! NON E' UN DOPPIONE DI printf, E LA DIFFERENZA E' TUTTA QUI. printf scrive
 * sulla console del processo: se quella console non e' quella a video, il
 * messaggio non lo legge nessuno — e se il processo e' un server grafico che
 * gira su una console sua, non lo legge nessuno MAI.
 *
 * Serve a chi deve poter riferire un guasto proprio quando il guasto gli
 * impedisce di farsi vedere. Il 14 agosto 2026 il server a finestre avviato
 * sulla console 1 non dipingeva, e i suoi messaggi d'errore finivano sulla
 * console che non si riusciva a guardare: uno strumento cieco costa piu' del
 * difetto che deve trovare.
 *
 * ! UNA RIGA PER CHIAMATA, tagliata a 200 caratteri e marcata con il PID. E'
 * un diario, non un flusso: chi ha dati da consegnare usa una pipe.
 * ============================================================================= */
int     log_seriale(const char *s);

/* =============================================================================
 * reboot — spegne, riavvia o ferma la macchina
 *
 * ! RENDE SOLO SE HA FALLITO. Se ha funzionato, questa chiamata non torna:
 * chi ci scrive dopo del codice deve trattarlo come un errore, non come il
 * caso normale.
 *
 * ! IL KERNEL SINCRONIZZA I DISCHI PRIMA, ed e' il motivo per cui questa
 * passa dal kernel invece di scrivere sulla porta dell'emulatore da ring 3:
 * spegnere senza svuotare le cache vuol dire perdere l'ultimo file scritto.
 * ============================================================================= */
#define EXOS_RB_POWEROFF  0     /* sincronizza, aspetta, spegne */
#define EXOS_RB_RESTART   1     /* sincronizza e riavvia */
#define EXOS_RB_HALT      2     /* sincronizza e ferma, senza spegnere */

int     reboot(int cosa);

/* =============================================================================
 * MEMORIA CONDIVISA FRA PROCESSI — le stesse pagine fisiche in due spazi
 *
 *     ShmZona z;
 *     strcpy(z.nome, "schermo");
 *     z.byte = 1228800;
 *     z.flag = SHM_CREA;              il primo la crea, gli altri mettono 0
 *     if (shm_apri(&z) == 0) {
 *         unsigned char *p = (unsigned char *)z.virt;
 *         ...
 *         shm_chiudi(p);
 *     }
 *
 * I messaggi IPC sono da 1536 byte: una finestra 640x480x32 sarebbero ~800
 * messaggi a fotogramma. Questa e' la strada per tutto cio' che e' piu' grande
 * di un messaggio.
 *
 * ! `byte` E' ANCHE UN CAMPO DI USCITA, ED E' LA COSA DA NON SBAGLIARE. Vale
 * come richiesta solo per chi CREA la zona; a chi si attacca viene reso quanto
 * la zona misura DAVVERO. Usare il valore che si e' chiesto invece di quello
 * che si e' ricevuto vuol dire due processi con due idee diverse della stessa
 * memoria: uno scrive dove l'altro non guarda, o legge oltre la fine.
 *
 * ! LA ZONA VIVE FINCHE' QUALCUNO LA TIENE APERTA. Non c'e' un shm_unlink: la
 * pagine tornano al sistema quando l'ultimo processo chiude o muore. Quindi
 * NON si puo' creare una zona, riempirla e uscire perche' qualcun altro la
 * trovi dopo — chi crea deve restare vivo finche' il secondo si e' attaccato.
 *
 * ! UN PROCESSO NON PUO' APRIRE DUE VOLTE LA STESSA ZONA: la seconda rende
 * -EEXIST. Una apri e una chiudi si corrispondono una a una, altrimenti chi
 * chiude una volta sola la lascerebbe aperta per sempre.
 *
 * Errori: -ENOENT (non c'e' e non hai chiesto SHM_CREA), -EEXIST (l'hai gia'
 * aperta tu), -ENOSPC (troppe zone nel sistema), -EMFILE (troppe per questo
 * processo), -ENOMEM (niente memoria, o piu' grande del massimo per zona),
 * -EINVAL (nome vuoto, troppo lungo o con caratteri di controllo).
 *
 * STRUTTURA DUPLICATA A MANO da kernel/include/syscall.h e ripetuta in
 * lib/libc.c, che non include questo file: TRE copie, tutte identiche.
 * ============================================================================= */
#define SHM_CREA    0x0001      /* creala se non c'e' (senza: solo attacco) */

typedef struct {
    char         nome[16];  /* in:  come si chiama la zona */
    unsigned int byte;      /* in:  quanto grande la vuoi (solo se la crei)
                             * out: quanto e' grande DAVVERO */
    unsigned int flag;      /* in:  SHM_CREA, oppure 0 */
    unsigned int virt;      /* out: dove la vedi */
} ShmZona;

/* =============================================================================
 * RIMETTERE LO SCHERMO — la rete di sicurezza della grafica
 *
 * Se un server grafico muore con la scheda in modalita' grafica, il sistema e'
 * vivo — kernel, scheduler, seriale e tastiera funzionano — ma lo schermo
 * resta congelato su cio' che c'era: un comando digitato alla cieca viene
 * eseguito e non se ne vede l'effetto. Questa e' la chiamata che si digita
 * alla cieca, e /bin/testo e' il comando che la fa.
 *
 * Rimette il modo testo 80x25 programmando i registri VGA, senza passare dal
 * BIOS — che da modo protetto non e' raggiungibile. Ricarica anche il
 * carattere e la tavolozza, che una modalita' grafica sovrascrive: senza,
 * si otterrebbe uno schermo che risponde e non si legge.
 *
 * ! LA CONSOLE TORNA PULITA. Il testo che c'era prima non si conserva: era
 * disposto per una geometria diversa, e tenerlo darebbe righe mescolate.
 *
 * ! NON SERVE ESSERE UN DRIVER, apposta: serve proprio quando il processo
 * privilegiato e' morto, e una rete di sicurezza che si possa tirare solo dai
 * driver e' una rete che non c'e' nel momento in cui serve.
 *
 * Rende sempre 0. Il perche' per esteso, e il limite su hardware reale con
 * una VESA attiva, stanno in kernel/arch/x86/vga_modo3.c.
 * ============================================================================= */
int     modo_testo(void);

int     shm_apri(ShmZona *z);
int     shm_chiudi(void *p);

/* -----------------------------------------------------------------------------
 * interrompi() — «smettila», detto a un altro processo
 *
 * Rende 0, oppure -ESRCH (non c'e'), -EPERM (non e' tuo, o e' init), -EINVAL
 * (sei tu: per uscire c'e' exit).
 *
 * ! IL BERSAGLIO ESCE, NON RICEVE NIENTE. Non e' un segnale: non c'e' gestore,
 * non c'e' maschera, non si puo' ignorare — e il codice di uscita e' 130, come
 * una shell Unix riporta un Ctrl+C. Un programma non puo' quindi «pulire prima
 * di andarsene»: quello che ha aperto lo chiude il kernel.
 * --------------------------------------------------------------------------- */
int     interrompi(int pid);

/* -----------------------------------------------------------------------------
 * Pseudo-terminali — una pipe CON una disciplina di linea attaccata
 *
 *     int fd[2];
 *     pty_apri(fd);                  // fd[0] master, fd[1] slave
 *     SpawnRedir r[3] = { {0,0,NULL,fd[1]}, {1,0,NULL,fd[1]}, {2,0,NULL,fd[1]} };
 *     spawn_ex("/bin/sh", argv, environ, r, 3);
 *     close(fd[1]);                  // ! come con le pipe: si chiude il capo dato
 *
 * ! CHI SCRIVE NEL MASTER STA BATTENDO TASTI, non stampando: quei byte passano
 * dalla disciplina, che li accumula in una riga, li fa vedere (l'eco esce dal
 * master, da leggere e disegnare) e li consegna allo slave solo all'Invio.
 * Backspace cancella, Ctrl+C interrompe chi e' in primo piano, Ctrl+D a riga
 * vuota e' la fine dei dati.
 *
 * ! IL PRIMO PIANO SI DICHIARA, e finche' nessuno lo fa Ctrl+C non interrompe
 * nessuno: e' voluto. Il pty potrebbe prendersela con «l'ultimo che ha letto»,
 * che pero' e' quasi sempre la shell — cioe' la sessione invece del programma.
 *
 * Le costanti PTY_* stanno qui sotto e valgono anche per il kernel.
 * --------------------------------------------------------------------------- */
#define PTY_CANONICO    0x0001
#define PTY_ECO         0x0002

#define PTY_CTL_FG              1   /* arg = pid che Ctrl+C deve interrompere */
#define PTY_CTL_MODO            2   /* arg = PTY_CANONICO | PTY_ECO           */
#define PTY_CTL_MISURA          3   /* arg = (righe << 16) | colonne          */
#define PTY_CTL_LEGGI_MISURA    4   /* rende (righe << 16) | colonne          */

int     pty_apri(int fd[2]);
int     pty_ctl(int fd, unsigned int cmd, unsigned int arg);

/* =============================================================================
 * ASPETTARE PIU' SORGENTI INSIEME — poll() e select()
 *
 *     struct pollfd v[3];
 *     v[0].fd = FD_IPC;      v[0].events = POLLIN;   // la mailbox
 *     v[1].fd = fd_client;   v[1].events = POLLIN;   // una pipe
 *     v[2].fd = 0;           v[2].events = POLLIN;   // la tastiera
 *
 *     int n = poll(v, 3, 100);        // -1 = per sempre, 0 = solo un'occhiata
 *     if (n > 0 && (v[0].revents & POLLIN)) ipc_recv(...);
 *
 * Senza questo un programma che debba badare a piu' cose puo' aspettarne UNA
 * sola e interrogare le altre a giro: e' attesa attiva, funziona e brucia la
 * CPU. Un server a finestre aspetta tastiera, mouse e N client, e non puo'
 * scegliere quale.
 *
 * ! LA MAILBOX IPC SI NOMINA CON FD_IPC, e non e' un descrittore vero: non si
 * apre, non si chiude, non si eredita, non si duplica. Ha un numero — 32,
 * cioe' il primo che un descrittore vero non puo' avere — perche' i valori
 * NEGATIVI mantengono il significato POSIX di «salta questa voce», che e'
 * come si smette di badare a un client senza rifare l'elenco.
 *
 * Su questo sistema meta' degli eventi arriva li' dentro: driver, servizi e
 * notifiche di IRQ. Un poll che sapesse guardare solo i file lascerebbe fuori
 * proprio le sorgenti per cui serve.
 *
 * ! I FILE NORMALI SONO SEMPRE PRONTI, come dice POSIX: una read su un file
 * non blocca mai, al piu' arriva alla fine. Aspettare su un file vorrebbe
 * dire aspettare per sempre qualcosa che e' gia' successo.
 *
 * ! POLLERR, POLLHUP e POLLNVAL ARRIVANO ANCHE SE NON SI CHIEDONO. Sono
 * condizioni, non desideri: chi aspetta POLLOUT su una pipe i cui lettori
 * sono spariti deve saperlo, o scriverebbe per sempre nel vuoto.
 *
 * Rende quante voci hanno `revents` non nullo, 0 se la scadenza e' passata
 * senza che succedesse niente, o un valore negativo (-EINVAL se le voci sono
 * piu' di POLL_MAX, -EFAULT se l'elenco non e' leggibile).
 *
 * ! poll() SU AL PIU' POLL_MAX (16) VOCI, perche' il kernel se le copia. Un
 * server con piu' di sedici client dovra' dividerle in gruppi, e quando
 * servira' davvero si alzera' il numero in un posto solo.
 *
 * select() c'e' per i programmi che la usano gia', ed e' scritta SOPRA poll()
 * dentro la libc: nel kernel l'attesa su piu' sorgenti e' una sola, perche'
 * scriverla due volte vuol dire sbagliarla due volte. Le sue maschere non
 * possono nominare FD_IPC — un fd_set arriva a 32 bit e FD_IPC e' il 32esimo.
 *
 * STRUTTURA DUPLICATA A MANO da kernel/include/syscall.h e ripetuta in
 * lib/libc.c, che non include questo file: TRE copie, tutte identiche.
 * ============================================================================= */
#define FD_IPC          32      /* la mailbox IPC come sorgente di poll() */
#define POLL_MAX        16      /* voci per chiamata */

#define POLLIN      0x0001      /* c'e' qualcosa da leggere */
#define POLLOUT     0x0004      /* si puo' scrivere senza bloccare */
#define POLLERR     0x0008      /* errore: solo in uscita */
#define POLLHUP     0x0010      /* l'altro capo se n'e' andato: solo in uscita */
#define POLLNVAL    0x0020      /* descrittore non valido: solo in uscita */

struct pollfd {
    int   fd;
    short events;       /* in:  cosa aspetto */
    short revents;      /* out: cosa e' successo */
};

int     poll(struct pollfd *fds, unsigned int nfds, int timeout);

/* fd_set: una maschera a 32 bit, uno per descrittore. MAX_FD del kernel e'
 * 32, quindi ci sta tutto. Definizione ripetuta in lib/libc.c. */
#define FD_SETSIZE  32
typedef struct { unsigned int bit; } fd_set;
#define FD_ZERO(s)      ((s)->bit = 0u)
#define FD_SET(f, s)    ((s)->bit |=  (1u << (f)))
#define FD_CLR(f, s)    ((s)->bit &= ~(1u << (f)))
#define FD_ISSET(f, s)  ((((s)->bit) >> (f)) & 1u)

int     select(int nfds, fd_set *leggere, fd_set *scrivere, fd_set *eccezioni,
               struct timeval *scadenza);

/* =============================================================================
 * Byte imprevedibili
 *
 * getentropy() riempie TUTTO il buffer o fallisce (0 / -1, firma OpenBSD);
 * getrandom() ritorna quanti byte ha scritto (firma Linux).
 *
 * ! ENTRAMBE POSSONO FALLIRE CON EAGAIN, e non e' un caso da ignorare:
 * significa che il sistema non ha ancora raccolto abbastanza
 * imprevedibilita'. La risposta giusta e' dirlo a chi usa il programma e
 * chiedergli di battere qualcosa sulla tastiera — non riprovare in ciclo
 * stretto, che non aggiunge entropia, e tanto meno proseguire con quello
 * che c'e'.
 *
 * Il perche' per esteso sta in kernel/arch/x86/entropia.c: il kernel
 * raccoglie entropia e non genera numeri casuali.
 * ============================================================================= */
int     getentropy(void *buf, size_t len);
ssize_t getrandom(void *buf, size_t len, unsigned int flags);

/* Richiede accesso a un range di porte I/O [base, base+count).
 * Sovrascrive un'eventuale bind precedente. Ritorna 0 su successo.
 *
 * ! SOLO PER I DRIVER, come dma_alloc e mmio_map: l'eseguibile dev'essere
 * stato caricato da un file <qualcosa>.drv, altrimenti -EPERM. Una porta I/O e'
 * pericolosa quanto un registro mappato in memoria — con 0xCF8 si
 * riprogrammano le BAR di qualunque dispositivo PCI — quindi passa dallo
 * stesso varco. Fino al 13 agosto 2026 la poteva chiamare chiunque. */
int     ioport_bind(unsigned int base, unsigned int count);

/* Legge/scrive un byte su una porta nel proprio range. Ritorna -EPERM
 * se la porta non è stata dichiarata con ioport_bind(). */
int     ioport_in(unsigned int port);
int     ioport_out(unsigned int port, unsigned int value);

/* -----------------------------------------------------------------------------
 * Accessi a 16 e 32 bit
 *
 * Non sono una comodità: il registro CONFIG_ADDRESS del bus PCI (0xCF8)
 * DEVE essere scritto con un singolo accesso a 32 bit, altrimenti il
 * ponte non lo interpreta come ciclo di configurazione — e siccome
 * 0xCF9 è il registro di reset di molti chipset, scriverlo a byte tende
 * a riavviare la macchina. La porta dati di una NE2000 vuole i 16 bit.
 *
 * ! ioport_in32 NON restituisce il valore letto. Una configurazione PCI
 * che vale 0xFFFFFFFF significa «nessun dispositivo», ed è un risultato
 * normale: come int sarebbe -1, cioè indistinguibile da un errore. Il
 * valore esce quindi dal puntatore, e il ritorno dice solo se è andata:
 *
 *     unsigned int v;
 *     if (ioport_in32(0xCFC, &v) == 0) usa(v);
 *
 * ioport_in16 non ha il problema (0..65535 sta nei positivi) e
 * restituisce il valore direttamente.
 *
 * La porta deve essere allineata all'ampiezza (multipla di 2 o di 4):
 * un accesso disallineato viene spezzato dal chipset in due cicli e sul
 * bus PCI il secondo non è più un ciclo di configurazione. Il kernel
 * risponde -EINVAL invece di leggere dati inventati.
 * --------------------------------------------------------------------------- */
int     ioport_in16(unsigned int port);
int     ioport_out16(unsigned int port, unsigned int value);
int     ioport_in32(unsigned int port, unsigned int *out);
int     ioport_out32(unsigned int port, unsigned int value);

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

#ifdef __cplusplus
}   /* extern "C" — aperto molto piu' sopra, vedi il commento li' */
#endif


/* =============================================================================
 * SHA-256 — vedi il commento in lib/libc.c prima di usarla per le password
 * ============================================================================= */
void sha256(const void *dati, size_t len, unsigned char out[32]);
void sha256_esa(const void *dati, size_t len, char out[65]);

#endif /* LIBC_H */

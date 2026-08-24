/* =============================================================================
 * bin/sh/shell.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Shell utente EX-OS (/bin/sh)
 *
 * Gira in ring3 come primo processo utente avviato dal kernel.
 * Comunica col kernel esclusivamente tramite syscall (int 0x80).
 *
 * Comandi built-in:
 *   help      — mostra i comandi disponibili
 *   helpconfig— come si installano e configurano i driver
 *   echo      — stampa argomenti
 *   cls/clear — pulisce lo schermo
 *   pwd       — directory corrente
 *   cd        — cambia directory
 *   ls        — lista file (syscall stat)
 *   cat       — mostra contenuto file
 *   exec      — esegue un programma ELF
 *   env       — mostra variabili d'ambiente
 *   reboot    — riavvio (tramite BIOS)
 *   halt      — ferma il sistema
 *   exit      — termina la shell
 *
 * Modello di esecuzione:
 *   - Un comando non built-in (es. "hello") viene lanciato come processo
 *     FIGLIO AUTONOMO (sys_spawn) e la shell attende la sua conclusione
 *     (sys_waitpid) prima di ridare il prompt — la shell resta sempre viva.
 *   - Il built-in "exec" invece SOSTITUISCE la shell stessa (vero exec()
 *     POSIX, via sys_exec): da usare consapevolmente, la shell corrente
 *     termina quando termina il programma eseguito con exec.
 *
 * Funzionalità:
 *   - Prompt colorato "ex-os> "
 *   - Parsing argomenti (spazi come separatori)
 *   - PATH per trovare eseguibili in /bin
 *   - Variabili d'ambiente minimali
 *   - History non implementata (Fase futura)
 * ============================================================================= */

/* ============================================================================
 * NOTA COMPILAZIONE:
 * Questo file è compilato come programma utente ELF32 statico.
 * NON include nessuna libreria C standard.
 * Tutte le chiamate di sistema vanno via int 0x80.
 * Makefile: $(CC) $(CFLAGS_USER) -static -o bin/sh shell.c
 * ============================================================================ */

/* =============================================================================
 * Tipi base (senza libc)
 * ============================================================================= */
typedef unsigned int    uint32_t;
typedef unsigned short  uint16_t;
typedef unsigned char   uint8_t;
typedef int             int32_t;
typedef uint32_t        size_t;
#define NULL    ((void*)0)

/* =============================================================================
 * Syscall numbers (identici a kernel/include/syscall.h)
 * ============================================================================= */
#define SYS_EXIT        1
#define SYS_SPAWN       2
#define SYS_IOCTL       54
#define SYS_READ        3
#define SYS_WRITE       4
#define SYS_OPEN        5
#define SYS_CLOSE       6
#define SYS_CONSOLE_INFO 231
#define SYS_SBRK         45
#define SYS_PROCINFO    188
#define SYS_IPC_SEND     220
#define SYS_IPC_RECV_TMO 228
#define SYS_IPC_LOOKUP   223
#define SYS_WAITPID     7
#define SYS_UPTIME     186
#define SYS_GETPID      20
#define SYS_EXEC        11
#define SYS_SCHED_YIELD 158
#define SYS_SLEEP       162
#define SYS_GETCWD      183
#define SYS_GETENV      184
#define SYS_VERSION     185
#define SYS_REBOOT       88

/* Comandi di SYS_REBOOT — devono restare allineati a kernel/include/power.h */
#define EXOS_RB_POWEROFF  0
#define EXOS_RB_RESTART   1
#define EXOS_RB_HALT      2
#define SYS_CHDIR       12
#define SYS_STAT        106
#define SYS_CONSOLE_SETFG 232   /* dichiara il processo in primo piano */
#define SYS_PTY_CTL       252   /* lo stesso, ma per uno pseudo-terminale */
#define PTY_CTL_FG          1   /* arg = chi Ctrl+C deve interrompere */
#define PTY_CTL_LEGGI_MISURA 4  /* rende la misura, o -ENOTTY se non e' un pty */

/* Opzioni di waitpid — identiche a kernel/include/syscall.h */
#define WNOHANG         0x0001

/* I due esiti che la shell deve saper distinguere quando un avvio va male.
 * Le syscall rendono l'errno cambiato di segno, quindi si confrontano con
 * il meno davanti. Valori identici a kernel/include/syscall.h.
 *
 *   ENOENT   il file non c'e'                 -> «comando non trovato»
 *   ENOEXEC  il file c'e' e non e' un ELF     -> il comando c'e', e' rotto
 *
 * Senza la seconda, un binario corrotto veniva annunciato come assente. */
#define SH_ENOENT       2
#define SH_ENOEXEC      8
/* ! SERVE DA QUANDO ESISTONO I PERMESSI (17 agosto 2026). Senza, un programma
 * rifiutato per mancanza del bit x veniva annunciato come «non e' un programma
 * eseguibile» — che e' falso: lo e', ed e' proprio per questo che ci si prova.
 * Un messaggio sbagliato manda a cercare il difetto nel binario invece che nei
 * permessi. */
#define SH_EACCES      13

/* I nomi dei servizi, per `helpconfig`: si chiede al registro IPC chi c'e'
 * gia'. Sono header di soli #define e struct, senza dipendenze — il nome
 * si prende da li' e non si ricopia, cosi' rinominare un servizio non
 * lascia indietro una stringa in questo file. */
#include "exinfo.h"
#include "kbd_proto.h"
#include "pci_proto.h"
#include "net_proto.h"
#include "ip_proto.h"

/* stdin=0, stdout=1, stderr=2 */
#define STDIN   0
#define STDOUT  1
#define STDERR  2

/* =============================================================================
 * Wrapper syscall inline ASM (stile Linux x86)
 * ============================================================================= */

/* ! DEVE RESTARE IDENTICA a ConsoleInfo in lib/include/libc.h e in
 * kernel/include/vga.h: attraversa l'ABI della syscall. La shell non
 * include libc.h (si compila da sola, vedi il commento in testa), quindi
 * la struttura si ripete qui — stessa convenzione dei numeri di syscall
 * duplicati poco sopra. */
typedef struct {
    uint32_t totale;
    uint32_t mia;
    uint32_t visibile;
    uint32_t fg;
} ConsoleInfo;

static inline int32_t syscall1(uint32_t num, uint32_t a)
{
    int32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a)
        : "memory"
    );
    return ret;
}

static inline int32_t syscall2(uint32_t num, uint32_t a, uint32_t b)
{
    int32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a), "c"(b)
        : "memory"
    );
    return ret;
}

static inline int32_t syscall3(uint32_t num, uint32_t a, uint32_t b, uint32_t c)
{
    int32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a), "c"(b), "d"(c)
        : "memory"
    );
    return ret;
}

/* ESI come quarto argomento: e' la stessa convenzione di lib/libc.c, e la
 * usa SYS_IPC_RECV_TMO per la scadenza. */
static inline int32_t syscall4(uint32_t num, uint32_t a, uint32_t b,
                               uint32_t c, uint32_t d)
{
    int32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d)
        : "memory"
    );
    return ret;
}

/* =============================================================================
 * Funzioni syscall user-friendly
 * ============================================================================= */

/* Definita piu' avanti, insieme al resto del dialogo con il driver di
 * tastiera: serve qui perche' sh_exit deve restituire la console prima di
 * sparire, e sh_exit e' il primo pezzo di codice del file. */
static void kbd_modo(unsigned int modo);

static void sh_exit(int code)
{
    /* ! LA CONSOLE SI RESTITUISCE COME LA SI E' TROVATA.
     *
     * riga_modifica() tiene la console in RAW per tutta la vita della
     * shell — e' la modalita' in cui riceve i tasti uno per uno — e fino
     * ad agosto 2026 uscire la lasciava cosi'. Non si notava perche' dopo
     * `exit` la console restava morta e nessuno leggeva piu' niente.
     *
     * Con /bin/login davanti non e' piu' vero: il login riprende il
     * prompt, chiede una riga, e trova la console in raw. Il driver se ne
     * accorge e la riporta in cooked da solo — e' la sua rete di
     * sicurezza — ma stampa "READLINE su console N in raw, ripristino
     * cooked" a ogni uscita dalla shell. Un avviso che scatta nel caso
     * NORMALE smette di segnalare quello anomalo, che e' il programma a
     * schermo intero morto senza rimettere a posto.
     *
     * Chi muore per un fault non passa di qui, ed e' giusto cosi': quello
     * e' il caso per cui la rete del driver esiste. */
    kbd_modo(KBD_MODE_COOKED);

    syscall1(SYS_EXIT, (uint32_t)code);
    /* Non ritorna */
    for(;;) {}
}

static int sh_write(int fd, const char *buf, uint32_t n)
{
    return syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, n);
}

/* Messaggio IPC: solo l'intestazione, come in lib/include/libc.h — il
 * payload arriva nel buffer separato. Ripetuta qui perche' la shell non
 * include libc.h, stessa convenzione dei numeri di syscall. */
typedef struct {
    uint32_t sender_pid;
    uint32_t tipo;
    uint32_t len;
} ShIpcMsg;

static int sh_ipc_lookup(const char *nome)
{
    return (int)syscall1(SYS_IPC_LOOKUP, (uint32_t)nome);
}

static int sh_ipc_send(int pid, uint32_t tipo, const void *dati, uint32_t len)
{
    return (int)syscall4(SYS_IPC_SEND, (uint32_t)pid, tipo,
                         (uint32_t)dati, len);
}

static int sh_ipc_recv(ShIpcMsg *m, void *buf, uint32_t len, uint32_t ms)
{
    return (int)syscall4(SYS_IPC_RECV_TMO, (uint32_t)m, (uint32_t)buf, len, ms);
}

static int sh_console_info(ConsoleInfo *ci)
{
    return (int)syscall2(SYS_CONSOLE_INFO, (uint32_t)ci, (uint32_t)sizeof(*ci));
}

static int sh_read(int fd, char *buf, uint32_t n)
{
    return syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buf, n);
}

static int sh_exec(const char *path)
{
    return syscall3(SYS_EXEC, (uint32_t)path, 0, 0);
}

/* ! LA FORMA A TRE ARGOMENTI NON C'E' PIU'. C'era `sh_spawn(path, argc,
 * argv)`, che lasciava ESI a caso e quindi non passava ne' ambiente ne'
 * redirezioni. Da quando la shell li passa sempre — vedi env_vettore e
 * redir_in_extra — l'unica forma e' sh_spawn_ex, piu' avanti: tenerne due
 * avrebbe voluto dire che il figlio riceve l'ambiente o no a seconda di
 * quale chiamante lo lancia, cioe' un comportamento che dipende dal punto
 * del file in cui si e' scritto il codice. */

/* sh_waitpid — attende la terminazione di `pid`.
 *
 * Tre argomenti e non due: il terzo (le opzioni) viaggia in EDX, e un
 * wrapper a due registri ci lascerebbe dentro un valore qualunque che il
 * kernel interpreterebbe come WNOHANG. Con options=0 il comportamento e'
 * quello di sempre — si aspetta.
 *
 * Ritorna il PID raccolto, 0 con WNOHANG se non e' ancora finito nulla,
 * o un errno negativo: -10 (ECHILD) se quel figlio non esiste piu'. */
static int sh_waitpid(int pid, int32_t *status, uint32_t options)
{
    return syscall3(SYS_WAITPID, (uint32_t)pid, (uint32_t)status, options);
}

/* Dichiara chi possiede la tastiera su questa console. La shell la
 * chiama con il proprio PID quando torna al prompt e con quello del
 * figlio quando ne aspetta uno in primo piano: e' cio' che impedisce a
 * un job in background di rubarle l'input (vedi la guardia su stdin in
 * sys_read). */
/* ! IL PRIMO PIANO SI DICHIARA DUE VOLTE, E NON E' UNA RIPETIZIONE: sono due
 * posti diversi che devono saperlo. La console lo usa per decidere chi puo'
 * leggere la tastiera; il pty lo usa per sapere CHI INTERROMPERE con Ctrl+C.
 * Una shell dentro un terminale in finestra ha tutt'e due, una sulla console
 * di testo ha solo la prima — e pty_ctl su uno stdin che non e' un pty rende
 * -ENOTTY e non fa niente, che e' esattamente cio' che serve. */
static int sh_getpid(void);

static void sh_setfg(int pid)
{
    syscall1(SYS_CONSOLE_SETFG, (uint32_t)pid);

    /* ! AL PTY SI DICE UN ALTRO PROCESSO, MAI SE STESSA, e la differenza fra
     * le due righe e' tutta qui. Per la console «primo piano» vuol dire chi
     * puo' leggere la tastiera, e la shell al prompt e' proprio lei. Per il
     * pty vuol dire CHI MUORE con Ctrl+C: dichiararsi li' significa che un
     * Ctrl+C battuto al prompt chiude la sessione — provato, e la finestra
     * restava con l'eco che funzionava e nessuno dall'altra parte.
     *
     * Su Unix la shell resta in primo piano e IGNORA il segnale; qui non ci
     * sono gestori, quindi la stessa idea si ottiene non dichiarandosi. Al
     * prompt Ctrl+C cancella la riga in corso — lo fa la disciplina — e non
     * ammazza nessuno. */
    syscall3(SYS_PTY_CTL, 0, PTY_CTL_FG,
             (pid == sh_getpid()) ? 0u : (uint32_t)pid);
}

/* =============================================================================
 * REDIREZIONI, SEQUENZE E PIPE — agosto 2026
 *
 * ! PERCHE' UNA SHELL A UN COMANDO PER RIGA NON BASTAVA PIU'. Fino a qui
 * `esegui_riga` faceva una cosa sola: prendere una riga, spezzarla in
 * argomenti, lanciare un programma. Niente `;`, niente `>`, niente `|`.
 * Era abbastanza finche' a scrivere le righe c'era una persona.
 *
 * Da quando dentro EX-OS gira `make`, a scriverle e' un makefile, e la
 * prima ricetta della runtime di FreeBASIC e'
 *
 *     rm -f $@; $(AR) rcs $@ $^
 *
 * cioe' DUE comandi separati da un punto e virgola. GNU make non li spezza:
 * consegna la ricetta intera a `/bin/sh -c` e si aspetta che la shell sappia
 * leggerla. Senza il `;`, la shell cercava un programma chiamato
 * «rm -f libfb.a; ar» e rispondeva «comando non trovato» nominando una
 * stringa che nel makefile non c'e' — il modo piu' rapido di mandare a
 * cercare il difetto nel posto sbagliato.
 *
 * QUELLO CHE SI CAPISCE ORA, e in che ordine lo si smonta:
 *
 *     a ; b        sequenza          esegui_lista      il livello piu' esterno
 *     a && b       se a e' riuscito       "
 *     a || b       se a e' fallito        "
 *     a | b        pipe              esegui_pipeline
 *     a > f        redirezioni       esegui_comando
 *     a >> f  a < f  a 2> f  a 2>&1
 *
 *     a *.c        caratteri jolly   espandi_jolly
 *
 * ! QUELLO CHE NON SI CAPISCE, dichiarato: niente `$VAR` nelle ricette,
 * niente sostituzione di comando con i backtick, niente `for`/`if`. Non
 * sono dimenticanze: sono le costruzioni che i makefile usano nei bersagli
 * di IMPACCHETTAMENTO (`dist`, `install`), non in quelli di compilazione.
 * Aggiungerle a meta' — accettarle senza espanderle — darebbe comandi che
 * partono e lavorano sul file sbagliato, che e' peggio di un comando che si
 * rifiuta di partire.
 *
 * ! I CARATTERI JOLLY C'ERANO IN QUESTO ELENCO fino a poche ore fa, e ci
 * sono usciti per un motivo preciso: `make clean` e' `rm -f *.o`, e senza
 * espansione quella riga non cancellava niente E NON LO DICEVA, perche' il
 * `-f` di rm tace sui file assenti. La compilazione dopo riusava oggetti
 * vecchi. Vedi espandi_jolly.
 * ============================================================================= */

#define SYS_DUP          41
#define SYS_PIPE         42
#define SYS_DUP2         63

/* Identici a kernel/include/syscall.h: la shell non include quell'header
 * (si compila da sola, vedi il commento in testa) e i valori attraversano
 * l'ABI della syscall esattamente come i numeri qui sopra. */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

/* ! LA STRUTTURA NON SI RICOPIA PIU' QUI DENTRO. Fino al 17 agosto 2026 c'era
 * una copia, con la raccomandazione di tenerla identica a quella del kernel: e'
 * rimasta indietro di tre giorni, e in quei tre giorni la shell non ha avuto
 * ne' redirezioni ne' ambiente, in silenzio. La definizione e' una sola e sta
 * in lib/include/spawn_abi.h — che non tira dentro la libc e usa solo
 * `unsigned int`, apposta per poter essere inclusa proprio da qui. */
#include "spawn_abi.h"

/* =============================================================================
 * IL CANARINO DELLO STACK, QUI E NON NELLA libc
 *
 * ! LA SHELL NON COLLEGA LA libc — usa le syscall dirette — quindi non ha ne'
 * __stack_chk_guard ne' __stack_chk_fail, che il compilatore pretende quando
 * la protezione e' accesa. Definirli qui costa quindici righe.
 *
 * ! ED E' IL PROGRAMMA CHE PIU' LI MERITA: la shell legge una riga scritta da
 * una persona e la spezza in argomenti dentro buffer di lunghezza fissa. E' il
 * posto dove un overflow arriva per primo, ed e' anche quello dove conta di
 * piu' — chi riscrive lo stack della shell riscrive il processo che lancia
 * tutti gli altri.
 *
 * Il valore si compone all'avvio: una costante scritta nel binario la
 * rimetterebbe a posto chi la riscrive.
 * ============================================================================= */
static int sh_write(int fd, const char *buf, uint32_t n);
static int sh_getpid(void);

unsigned long __stack_chk_guard = 0x00AA55C3ul;

static void canarino_avvia(void)
{
    unsigned long v = (unsigned long)syscall1(SYS_UPTIME, 0);

    v ^= ((unsigned long)sh_getpid() << 16);
    /* Il byte basso resta zero: ferma gli overflow fatti con le funzioni di
     * stringa, che si arrestano sullo zero. */
    __stack_chk_guard = ((v << 8) ^ 0x5A3C0000ul) & ~0xFFul;
}

void __stack_chk_fail(void)
{
    static const char m[] =
        "\n*** sh: stack rotto, una scrittura e' andata oltre un buffer.\n"
        "    La shell si ferma: proseguire vorrebbe dire tornare a un\n"
        "    indirizzo che non e' piu' quello giusto.\n";

    sh_write(2, m, (uint32_t)(sizeof(m) - 1));
    syscall1(SYS_EXIT, 134);
    for (;;) { }
}

static int sh_open(const char *path, uint32_t flags)
{
    return syscall3(SYS_OPEN, (uint32_t)path, flags, 0666);
}

static int sh_close(int fd)
{
    return syscall1(SYS_CLOSE, (uint32_t)fd);
}

static int sh_dup(int fd)
{
    return syscall1(SYS_DUP, (uint32_t)fd);
}

static int sh_dup2(int vecchio, int nuovo)
{
    return syscall2(SYS_DUP2, (uint32_t)vecchio, (uint32_t)nuovo);
}

static int sh_pipe(int fd[2])
{
    return syscall1(SYS_PIPE, (uint32_t)fd);
}

/* Come sh_spawn, ma con il blocco EXTRA in ESI: l'ambiente da passare al
 * figlio e le redirezioni da applicargli prima che parta. */
static int sh_spawn_ex(const char *path, int argc, char **argv,
                       SpawnExtra *extra)
{
    return syscall4(SYS_SPAWN, (uint32_t)path, (uint32_t)argc,
                    (uint32_t)argv, (uint32_t)extra);
}

/* =============================================================================
 * Leggere una directory, per i caratteri jolly.
 *
 * ! IDENTICA a DirEntry in kernel/include/syscall.h e in lib/include/libc.h.
 * Sono quattro copie in tutto — questa, quelle due e quella dentro lib/libc.c
 * — e vanno cambiate insieme: attraversano l'ABI della syscall. La shell non
 * include gli header del kernel ne' la libc (si compila da sola: vedi il
 * commento in testa al file), stessa convenzione dei numeri di syscall
 * ricopiati qui sopra.
 *
 * ! NON C'E' UNA DIR SULLO HEAP perche' la shell non ha uno heap. La
 * lettura e' a blocchi di SH_DIR_BLOCCO voci — e' cosi' che funziona
 * SYS_READDIR, che prende l'indice di partenza — e lo stato sta in una
 * struttura sullo stack di chi legge.
 * ============================================================================= */
/* 320 come PERCORSO_MAX del kernel: qui si compone "<PATH>/<comando>", e
 * un buffer piu' corto taglierebbe percorsi che la syscall accetterebbe. */
#define PATH_MAX_SH 320

#define SYS_READDIR        141
#define SH_DIRENT_NAME_MAX 256
#define SH_DIR_BLOCCO      16      /* = LISTDIR_MAX_BATCH del kernel */

typedef struct {
    char         name[SH_DIRENT_NAME_MAX];
    uint32_t     size;
    uint32_t     ident;
    unsigned char is_dir;
} ShDirEntry;

typedef struct {
    char        percorso[PATH_MAX_SH];
    ShDirEntry  voci[SH_DIR_BLOCCO];
    int         n;          /* quante ce ne sono nel blocco */
    int         i;          /* la prossima da rendere */
    uint32_t    start;      /* indice della prima del blocco, per il kernel */
    int         finito;
} ShDir;

/* Una sola alla volta: l'espansione dei jolly guarda una directory per
 * token e la chiude prima di passare al successivo. Statica e non sullo
 * stack perche' sono quattro kilobyte e mezzo, e questa shell gira anche
 * con lo stack iniziale di un processo appena nato. */
static ShDir g_dir;

static ShDir *sh_opendir(const char *percorso)
{
    uint32_t i;

    for (i = 0; i + 1 < sizeof(g_dir.percorso) && percorso[i]; i++)
        g_dir.percorso[i] = percorso[i];
    g_dir.percorso[i] = '\0';

    g_dir.n = g_dir.i = 0;
    g_dir.start  = 0;
    g_dir.finito = 0;

    /* ! SI PROVA SUBITO A LEGGERE, invece di rendere un handle e scoprire
     * dopo che il percorso non e' una directory. Chi chiama distingue cosi'
     * «directory vuota» da «non e' una directory», e nel secondo caso lascia
     * il modello com'e' invece di farlo sparire. */
    {
        int r = syscall4(SYS_READDIR, (uint32_t)g_dir.percorso,
                         (uint32_t)g_dir.voci, SH_DIR_BLOCCO, 0);

        if (r < 0) return 0;
        g_dir.n = r;
        if (r < SH_DIR_BLOCCO) g_dir.finito = 1;
        g_dir.start = (uint32_t)r;
    }
    return &g_dir;
}

static const char *sh_readdir(ShDir *d)
{
    if (d->i >= d->n) {
        int r;

        if (d->finito) return 0;

        r = syscall4(SYS_READDIR, (uint32_t)d->percorso,
                     (uint32_t)d->voci, SH_DIR_BLOCCO, d->start);
        if (r <= 0) { d->finito = 1; return 0; }

        /* Meno voci di quante ne abbiamo chieste: era l'ultimo blocco. E'
         * lo stesso idioma di ls, install e della readdir della libc. */
        if (r < SH_DIR_BLOCCO) d->finito = 1;
        d->n      = r;
        d->i      = 0;
        d->start += (uint32_t)r;
    }
    return d->voci[d->i++].name;
}

static void sh_closedir(ShDir *d) { (void)d; }

static int sh_chdir(const char *path)
{
    return syscall1(SYS_CHDIR, (uint32_t)path);
}

/* Legge una variabile della sezione [env] di /boot/kernel.cfg.
 * Ritorna la lunghezza del valore, <0 se assente o buffer troppo piccolo. */
static int sh_getenv_kernel(const char *key, char *buf, uint32_t size)
{
    return syscall3(SYS_GETENV, (uint32_t)key, (uint32_t)buf, size);
}

/* Copia l'identità del sistema (g_os_version del kernel) in buf.
 * Ritorna la lunghezza, <0 su errore. */
static int sh_version(char *buf, uint32_t size)
{
    return syscall2(SYS_VERSION, (uint32_t)buf, size);
}

static int sh_getcwd(char *buf, uint32_t size)
{
    return syscall2(SYS_GETCWD, (uint32_t)buf, size);
}

static int sh_getpid(void)
{
    return syscall1(SYS_GETPID, 0);
}

/* =============================================================================
 * LA MEMORIA DI QUESTO PROCESSO — per `ver`
 *
 * ! LA SHELL NON SI COLLEGA ALLA libc, ED E' UNA SCELTA CHE SI PAGA QUI. E'
 * il programma con cui si ripara un sistema: deve partire anche quando
 * /lib/libc.so non c'e'. Quindi lib/exinfo, che la libc la usa, non si puo'
 * collegare — e questi venti righe sono la stessa aritmetica rifatta con le
 * sue chiamate diirette.
 *
 * ! DI exinfo.h SI PRENDE SOLO L'INTESTAZIONE, che e' fatta di #define e non
 * di codice: cosi' l'autore e l'indirizzo restano scritti in UN FILE SOLO,
 * che era il punto di avere quel modulo.
 * ========================================================================== */
extern void _start(void);
extern char _bss_end;

/* La stessa ProcInfo di libc.h e di kernel/include/syscall.h: le tre copie
 * devono restare identiche. Qui servono solo i primi campi piu' gli stack. */
typedef struct {
    uint32_t pid, ppid, state, prio;
    char     name[32];
    uint32_t ustack_top, ustack_base, ustack_limit;
    uint32_t kstack_base, kstack_top;
} ShProcInfo;

static void sh_memoria(uint32_t *immagine, uint32_t *heap, uint32_t *pila)
{
    uint32_t base = (uint32_t)(void *)_start;
    uint32_t fine = (uint32_t)&_bss_end;
    uint32_t inizio, cima;

    *immagine = (fine > base) ? fine - base : 0;

    inizio = (fine + 4095u) & ~4095u;
    cima   = (uint32_t)syscall1(SYS_SBRK, 0);
    *heap  = (cima > inizio) ? cima - inizio : 0;

    *pila = 0;
    {
        ShProcInfo v[8];
        uint32_t   start = 0, mio = (uint32_t)sh_getpid();
        int        n, i;

        /* ! QUATTRO ARGOMENTI, E IL QUARTO E' sizeof: il kernel rifiuta con
         * EINVAL se non combacia, ed e' cosi' che si accorge di una copia
         * della struttura andata fuori sincrono. Con syscall3 il quarto
         * registro era spazzatura, la chiamata falliva e la pila si leggeva
         * «0 KB» — un numero plausibile e sbagliato, che e' il modo peggiore
         * di sbagliare. Visto perche' zero non e' un valore possibile: uno
         * stack toccato occupa almeno una pagina. */
        while ((n = syscall4(SYS_PROCINFO, (uint32_t)v, 8, start,
                             (uint32_t)sizeof(ShProcInfo))) > 0) {
            for (i = 0; i < n; i++) {
                if (v[i].pid != mio) continue;
                if (v[i].ustack_top > v[i].ustack_base)
                    *pila = v[i].ustack_top - v[i].ustack_base;
                return;
            }
            start += (uint32_t)n;
        }
    }
}

static void sh_yield(void)
{
    syscall1(SYS_SCHED_YIELD, 0);
}

/* =============================================================================
 * Funzioni stringa (senza libc)
 * ============================================================================= */

static uint32_t sh_strlen(const char *s)
{
    uint32_t n = 0;
    while (s && *s++) n++;
    return n;
}

static void sh_strcpy(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int sh_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int sh_strncmp(const char *a, const char *b, uint32_t n)
{
    while (n-- && *a && *b && *a == *b) { a++; b++; }
    return n == (uint32_t)-1 ? 0 : ((unsigned char)*a - (unsigned char)*b);
}

static char *sh_strcat(char *dst, const char *src, uint32_t max)
{
    uint32_t dl = sh_strlen(dst);
    uint32_t i  = 0;
    while (dl + i < max - 1 && src[i]) { dst[dl + i] = src[i]; i++; }
    dst[dl + i] = '\0';
    return dst;
}

static uint32_t sh_itoa(uint32_t v, char *buf, uint32_t base)
{
    static const char hex[] = "0123456789abcdef";
    char tmp[32];
    uint32_t i = 0, j;
    if (v == 0) { buf[0]='0'; buf[1]='\0'; return 1; }
    while (v > 0) { tmp[i++] = hex[v % base]; v /= base; }
    for (j = 0; j < i; j++) buf[j] = tmp[i - j - 1];
    buf[i] = '\0';
    return i;
}

/* =============================================================================
 * Output helpers
 * ============================================================================= */

static void print(const char *s)
{
    if (s) sh_write(STDOUT, s, sh_strlen(s));
}

static void println(const char *s)
{
    print(s);
    sh_write(STDOUT, "\n", 1);
}

static void printerr(const char *s)
{
    sh_write(STDERR, s, sh_strlen(s));
    sh_write(STDERR, "\n", 1);
}

/* Stampa un numero decimale */
static void print_uint(uint32_t v)
{
    char buf[12];
    sh_itoa(v, buf, 10);
    print(buf);
}

/* =============================================================================
 * Colori ANSI (il driver TTY li interpreta)
 *
 * Codici della serie 90-97 (versione CHIARA) e non 30-37: il prompt è
 * sempre stato di colori vivaci, ma li otteneva per un equivoco. Il TTY
 * aveva un proprio parser ANSI che mappava 31 su LIGHT_RED, 32 su
 * LIGHT_GREEN e così via — cioè trattava la serie scura come se fosse
 * quella chiara. Da agosto 2026 quel parser doppio non c'è più (le
 * sequenze le interpreta solo vga.c, che è l'unico a saper fare anche
 * cursore e cancellazioni), e vga.c rispetta lo standard: 32 è verde
 * scuro. Chiedere 92 significa chiedere davvero ciò che si vedeva
 * prima.
 * ============================================================================= */
#define CLR_RESET   "\x1B[0m"
#define CLR_RED     "\x1B[91m"
#define CLR_GREEN   "\x1B[92m"
#define CLR_YELLOW  "\x1B[93m"
#define CLR_CYAN    "\x1B[96m"
#define CLR_WHITE   "\x1B[97m"

/* =============================================================================
 * Variabili d'ambiente della shell (array semplice chiave=valore)
 * ============================================================================= */
#define ENV_MAX     32
#define ENV_LEN     128

static char g_env[ENV_MAX][ENV_LEN];
static uint32_t g_env_count = 0;

static const char *env_get(const char *key)
{
    uint32_t i;
    uint32_t kl = sh_strlen(key);
    for (i = 0; i < g_env_count; i++) {
        if (sh_strncmp(g_env[i], key, kl) == 0 && g_env[i][kl] == '=') {
            return &g_env[i][kl + 1];
        }
    }
    return NULL;
}

static void env_set(const char *key, const char *value)
{
    uint32_t i;
    uint32_t kl = sh_strlen(key);
    char entry[ENV_LEN];

    sh_strcpy(entry, key, ENV_LEN);
    sh_strcat(entry, "=", ENV_LEN);
    sh_strcat(entry, value, ENV_LEN);

    /* Aggiorna se esiste già */
    for (i = 0; i < g_env_count; i++) {
        if (sh_strncmp(g_env[i], key, kl) == 0 && g_env[i][kl] == '=') {
            sh_strcpy(g_env[i], entry, ENV_LEN);
            return;
        }
    }

    /* Aggiungi nuovo */
    if (g_env_count < ENV_MAX) {
        sh_strcpy(g_env[g_env_count++], entry, ENV_LEN);
    }
}

/* Chiavi che la shell eredita dalla sezione [env] di /boot/kernel.cfg.
 * I valori qui accanto sono usati SOLO se il kernel non ha quella chiave
 * (file assente, sezione incompleta): servono a far partire la shell in
 * uno stato sensato, non a duplicare la configurazione. */
static const char *const ENV_INHERITED[][2] = {
    /* ! /cdrom/bin C'E' APPOSTA: e' dove stanno gli strumenti di
     * sviluppo (as, ld, cc1, fbc), che sul floppy non ci stanno. Da
     * quando spawn() cerca nel PATH (vedi lib/libc.c), e' anche cio' che
     * permette a un compilatore di trovare il proprio assemblatore
     * chiamandolo "as" e basta, come fa su qualunque altro sistema.
     * Un lettore vuoto non e' un problema: la voce semplicemente non
     * combacia con niente. */
    { "PATH",   "/bin:/dev:/cdrom/bin" },
    { "HOME",   "/"                },
    { "TERM",   "vga"              },
    { "OSNAME", "EX-OS"            },
    { "OSVER",  "0.1"              },
    { "AUTHOR", "Graziano Falcone" },
};
#define ENV_INHERITED_COUNT (sizeof(ENV_INHERITED)/sizeof(ENV_INHERITED[0]))

/* =============================================================================
 * env_init — popola l'ambiente della shell
 *
 * Fino a luglio 2026 questa funzione ri-hardcodava le stesse coppie
 * chiave/valore già presenti in /boot/kernel.cfg. Erano due copie della
 * stessa verità: modificare OSNAME nel file di configurazione non aveva
 * alcun effetto visibile, perché nessuno leggeva quella sezione — il
 * kernel la parsava, la stampava nel log di boot e poi la ignorava
 * (cfg_getenv() non aveva un solo chiamante in tutto il progetto).
 *
 * Ora i valori arrivano dal kernel via SYS_GETENV, con fallback locale.
 * ============================================================================= */
static void env_init(void)
{
    char     val[ENV_LEN];
    uint32_t i;

    for (i = 0; i < ENV_INHERITED_COUNT; i++) {
        if (sh_getenv_kernel(ENV_INHERITED[i][0], val, sizeof(val)) >= 0) {
            env_set(ENV_INHERITED[i][0], val);
        } else {
            env_set(ENV_INHERITED[i][0], ENV_INHERITED[i][1]);
        }
    }

    /* Non viene da kernel.cfg: è la shell stessa a sapere cosa sta
     * eseguendo, e il kernel dichiara il percorso in [boot] shell= per
     * un altro scopo (chi lanciare), non come variabile d'ambiente. */
    env_set("SHELL", "/bin/sh");
}

/* =============================================================================
 * env_eredita — l'ambiente che arriva dal padre vince su kernel.cfg
 *
 * ! FINO AD AGOSTO 2026 LA SHELL BUTTAVA VIA L'AMBIENTE DEL PADRE. `_start`
 * leggeva argc e argv e ignorava envp; env_init() ricostruiva tutto da
 * /boot/kernel.cfg. Per una shell interattiva non si vedeva — il padre e' il
 * kernel e non ha niente da passare — e per `system()` quasi neanche.
 *
 * Si vede da quando c'e' `make`. Un makefile esporta variabili, e la catena
 * e' `make -> /bin/sh -c "ricetta" -> gcc`: se l'anello di mezzo azzera
 * l'ambiente, il comando in fondo gira con quello di kernel.cfg invece che
 * con quello che il makefile ha preparato per lui. Il sintomo non e' un
 * errore, e' un comando che usa il PATH sbagliato — cioe' UN ALTRO
 * PROGRAMMA con lo stesso nome, e nessun messaggio.
 *
 * L'ordine e' quello di sempre: prima i valori di sistema, poi sopra quelli
 * del padre. Chi ci ha messo qualcosa apposta ha ragione su un default.
 * ============================================================================= */
static void env_eredita(char **envp)
{
    uint32_t i;

    if (envp == NULL) return;

    for (i = 0; i < ENV_MAX && envp[i] != NULL; i++) {
        char        chiave[ENV_LEN];
        const char *v = envp[i];
        uint32_t    k = 0;

        while (v[k] && v[k] != '=' && k < ENV_LEN - 1) { chiave[k] = v[k]; k++; }
        /* Una voce senza '=' non e' una variabile: si scarta invece di
         * inventarle un valore vuoto, che sarebbe indistinguibile da una
         * variabile davvero impostata a niente. */
        if (v[k] != '=') continue;
        chiave[k] = '\0';

        env_set(chiave, v + k + 1);
    }
}

/* =============================================================================
 * env_vettore — l'ambiente della shell nella forma che sys_spawn vuole
 *
 * ! IL VETTORE E' STATICO E VIENE RIUSATO A OGNI SPAWN. Puo' esserlo perche'
 * sys_spawn COPIA le stringhe nell'arena del kernel prima di tornare (vedi
 * spawn_arena_dup): quando questa funzione viene richiamata, del vettore
 * precedente non ha piu' bisogno nessuno. Se un giorno la syscall diventasse
 * asincrona questa assunzione cadrebbe, ed e' il motivo per cui sta scritta.
 * ============================================================================= */
static char *g_envv[ENV_MAX + 1];

static char **env_vettore(void)
{
    uint32_t i;

    for (i = 0; i < g_env_count && i < ENV_MAX; i++) g_envv[i] = g_env[i];
    g_envv[i] = NULL;
    return g_envv;
}

/* =============================================================================
 * Avvio silenzioso — verboseboot
 *
 * L'opzione vive in [kernel] di /boot/kernel.cfg, non in [env], perché è
 * una scelta di sistema; SYS_GETENV la espone comunque (vedi
 * cfg_get_option in kernel/fs/cfg.c). Se la shell non riuscisse a
 * leggerla, il default è "parla": un'interfaccia muta per un errore di
 * lettura sarebbe indistinguibile da un'interfaccia bloccata.
 *
 * Silenzioso significa: niente banner d'avvio. NON significa niente
 * prompt e niente output dei comandi - quello è "l'output normale" che va
 * mostrato sempre.
 * ============================================================================= */
static int g_verbose_boot = 1;

/* Il codice di uscita dell'ultimo comando in primo piano.
 *
 * ! SERVE A `sh -c`, che deve RIPORTARLO al proprio padre: senza, la
 * system() della libc direbbe «riuscito» a qualunque comando, compresi
 * quelli falliti — e chi la chiama non avrebbe modo di accorgersene.
 * E' anche la meta' del lavoro che servira' a `$?`, quando ci sara'. */
static int g_ultimo_stato = 0;

static void verbose_init(void)
{
    char val[8];

    if (sh_getenv_kernel("verboseboot", val, sizeof(val)) < 0) {
        g_verbose_boot = 1;
        return;
    }
    g_verbose_boot = (val[0] != '0');
}

/* =============================================================================
 * Parsing della riga di comando
 *
 * Divide la stringa in argv[] separando per spazi, tenendo insieme cio'
 * che sta fra virgolette:
 *
 *     cp "appunti di riunione.txt" /disco     due argomenti, non quattro
 *
 * ! APICI SINGOLI E DOPPI FANNO LA STESSA COSA, e non e' una
 * semplificazione affrettata. Su una shell Unix la differenza esiste
 * perche' fra virgolette doppie $VAR viene espansa e fra apici singoli no.
 * Qui NON c'e' nessuna espansione — ne' di variabili ne' di caratteri
 * jolly — quindi le due forme non avrebbero niente da distinguere.
 * Accettarle entrambe e trattarle uguale e' onesto; accettarne una sola
 * costringerebbe a ricordare quale.
 *
 * ! UNA VIRGOLETTA NON CHIUSA VIENE SEGNALATA. Prima l'argomento si
 * prendeva fino a fine riga in silenzio: `cp "prova /disco` diventava un
 * solo argomento chiamato "prova /disco" e il comando falliva lamentando
 * un file inesistente dal nome assurdo. Il difetto era nella riga, non
 * nel file, e va detto li'.
 *
 * Ritorna argc, oppure -1 se la riga e' malformata.
 * ============================================================================= */
/* Lo stesso tetto di MAX_SPAWN_ARGS nel kernel, e devono restare uguali:
 * uno piu' basso qui farebbe sparire argomenti che la syscall avrebbe
 * accettato, e la riga digitata a mano non potrebbe mai arrivare dove
 * arriva quella costruita da un programma.
 *
 * ! ERA 64 QUI E 64 NEL KERNEL, e i due sono saliti insieme a 512 per la
 * stessa ragione: la riga `ar rcs libfb.a <~200 oggetti>` che il makefile
 * di FreeBASIC costruisce. Vedi MAX_SPAWN_ARGS in
 * kernel/syscall/syscall_impl.c.
 *
 * ! E C'ERA UN SECONDO TETTO, PIU' BASSO E NASCOSTO: run_program copiava
 * argv in un `char *spawn_argv[32]` e si fermava a 31 SENZA DIRLO. Cioe'
 * proprio il troncamento silenzioso che il kernel aveva smesso di fare,
 * rifatto un livello piu' su. Adesso non c'e' piu' nessuna copia. */
#define MAX_ARGS    512
/* 512, cioe' quanto il driver di tastiera puo' consegnare in un messaggio
 * (KBD_LINE_MAX). Era 256: bastava con i nomi 8.3, non basta piu' da
 * quando un nome ext2 puo' essere di 255 byte, perche' `cp <lungo> <dest>`
 * supera i 256. Un nome che si puo' creare ed elencare ma non digitare e'
 * un nome irraggiungibile per meta'. */
#define MAX_LINE    512

/* ! LA RIGA DI `sh -c` NON E' UNA RIGA DIGITATA, e non puo' avere lo
 * stesso tetto. MAX_LINE e' tarato su quanto una persona puo' scrivere al
 * prompt; una ricetta di make e' costruita da un programma, e quella del
 * link di FreeBASIC — centoquarantacinque percorsi di oggetti — sono
 * seimila caratteri. Con 512 la si sarebbe TAGLIATA A META', e cio' che
 * resta di una riga di comando tagliata a meta' e' quasi sempre ancora un
 * comando valido: si sarebbe collegato un compilatore con meta' dei suoi
 * pezzi, e l'errore sarebbe uscito da `ld` parlando di simboli mancanti.
 *
 * 32768 e' lo stesso numero di SPAWN_ARENA_BYTES nel kernel: e' inutile
 * accettare qui una riga che la syscall rifiutera' dopo. */
#define MAX_CMDLINE 32768

/* Che cosa e' un token, oltre a essere una parola.
 *
 * ! SERVE UN ARRAY A PARTE E NON BASTA GUARDARE LA STRINGA: dopo il
 * parsing, un `>` che era un operatore e un `">"` che era il nome di un
 * file sono la stessa identica stringa. La differenza — le virgolette —
 * l'ha gia' mangiata il tokenizzatore, ed e' l'unica cosa che distingue
 * «scrivi su un file» da «passa questo argomento». */
#define OP_NESSUNO  0
#define OP_OUT      1   /* >     stdout su file, troncando   */
#define OP_APP      2   /* >>    stdout su file, in coda     */
#define OP_IN       3   /* <     stdin da file               */
#define OP_ERR      4   /* 2>    stderr su file, troncando   */
#define OP_ERRAPP   5   /* 2>>   stderr su file, in coda     */
#define OP_ERR2OUT  6   /* 2>&1  stderr dove va stdout       */

/* =============================================================================
 * parse_line — da una riga a un vettore di token
 *
 * ! I TOKEN SI COPIANO IN `scratch` INVECE DI SPEZZARE LA RIGA IN LOCO, e
 * il cambio non e' di stile. Spezzare in loco vuol dire piantare un '\0' al
 * posto del separatore, e funziona finche' il separatore e' uno spazio —
 * che si puo' distruggere. Non funziona piu' con `ls>f`: li' il carattere
 * che chiude il token e' il `>`, cioe' il token SUCCESSIVO, e scriverci
 * sopra il terminatore del primo cancella il secondo. Con una copia il
 * problema non esiste, e in cambio la riga originale resta leggibile — cosa
 * che serve all'elenco dei job, che vuole il comando com'e' stato scritto.
 *
 * ! LE VIRGOLETTE SI TOLGONO OVUNQUE, NON SOLO IN TESTA. Prima si
 * riconoscevano solo se il token COMINCIAVA con una virgoletta, quindi
 * `-d 'FBSHA1="abc"'` — che e' come i makefile passano una macro con dentro
 * una stringa — arrivava al programma con gli apici attaccati. Il compilatore
 * riceveva un nome di macro che nessuno aveva scritto e falliva nominandolo.
 *
 * Ritorna il numero di token, oppure -1 se la riga e' malformata.
 * ============================================================================= */
static int parse_line(const char *line, char *scratch, uint32_t scratch_len,
                      char *argv[], int oper[], char *quotato,
                      uint32_t *usato, int max_args)
{
    const char *p = line;
    uint32_t    w = 0;          /* dove scriviamo dentro scratch */
    int         argc = 0;

    for (;;) {
        char virg = 0;
        int  op   = OP_NESSUNO;
        int  len  = 0;

        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (argc >= max_args) {
            print(CLR_YELLOW);
            println("sh: troppi argomenti in un comando solo");
            print(CLR_RESET);
            return -1;
        }

        /* --- Un operatore di redirezione e' un token per conto suo -------
         *
         * Si riconosce solo IN TESTA a un token, ed e' cio' che tiene fuori
         * dai guai `echo 2 > f`: li' il `2` e' seguito da uno spazio, quindi
         * quando il ciclo ci arriva `p[1]` non e' `>` e non c'e' nessun
         * operatore da vedere.
         *
         * ! COSTO DICHIARATO: `echo 12>f` qui vuol dire `echo 12 > f`,
         * mentre una shell Unix ci leggerebbe una redirezione del
         * descrittore 12. Descrittori diversi da 0, 1 e 2 non si
         * ridirigono, e fingere di capirli sarebbe peggio. */
        if      (p[0]=='2' && p[1]=='>' && p[2]=='&' && p[3]=='1')
                                                { op=OP_ERR2OUT; len=4; }
        else if (p[0]=='2' && p[1]=='>' && p[2]=='>') { op=OP_ERRAPP; len=3; }
        else if (p[0]=='2' && p[1]=='>')        { op=OP_ERR;    len=2; }
        else if (p[0]=='1' && p[1]=='>' && p[2]=='>') { op=OP_APP; len=3; }
        else if (p[0]=='1' && p[1]=='>')        { op=OP_OUT;    len=2; }
        else if (p[0]=='>' && p[1]=='>')        { op=OP_APP;    len=2; }
        else if (p[0]=='>')                     { op=OP_OUT;    len=1; }
        else if (p[0]=='<')                     { op=OP_IN;     len=1; }

        if (op != OP_NESSUNO) {
            argv[argc] = (char *)"";    /* l'operatore non ha un testo utile */
            oper[argc] = op;
            argc++;
            p += len;
            continue;
        }

        /* --- Una parola ------------------------------------------------- */
        argv[argc]   = scratch + w;
        oper[argc]   = OP_NESSUNO;
        /* ! SI ANNOTA SE IL TOKEN AVEVA DELLE VIRGOLETTE, e serve dopo:
         * `rm *.o` espande, `rm "*.o"` no. Dopo il parsing i due token sono
         * la stessa identica stringa — le virgolette le ha gia' mangiate
         * questo ciclo — quindi l'unico posto in cui la differenza esiste
         * ancora e' qui. Senza, non si potrebbe piu' nominare un file che
         * ha davvero un asterisco nel nome. */
        quotato[argc] = 0;
        argc++;

        for (;;) {
            char c = *p;

            if (c == '\0') break;

            if (virg) {
                if (c == virg) { virg = 0; p++; continue; }
                if (w + 1 >= scratch_len) goto troppo_lunga;
                scratch[w++] = c; p++;
                continue;
            }

            if (c == ' ' || c == '\t') break;
            if (c == '"' || c == '\'') {
                virg = c; p++;
                quotato[argc - 1] = 1;
                continue;
            }
            if (c == '>' || c == '<')  break;

            /* La barra rovesciata protegge il carattere che segue. Serve ai
             * nomi con dentro uno spazio quando le virgolette darebbero piu'
             * fastidio che aiuto, ed e' cio' che i makefile usano per
             * proteggere le virgolette stesse. */
            if (c == '\\' && p[1]) {
                if (w + 1 >= scratch_len) goto troppo_lunga;
                scratch[w++] = p[1]; p += 2;
                quotato[argc - 1] = 1;   /* `\*` e' un asterisco, non un jolly */
                continue;
            }

            if (w + 1 >= scratch_len) goto troppo_lunga;
            scratch[w++] = c; p++;
        }

        if (virg) {
            print(CLR_YELLOW);
            print("sh: manca la ");
            print(virg == '"' ? "virgoletta" : "apice");
            println(" di chiusura");
            print(CLR_RESET);
            return -1;
        }

        scratch[w++] = '\0';
    }

    argv[argc] = NULL;
    *usato = w;
    return argc;

troppo_lunga:
    print(CLR_YELLOW);
    println("sh: riga di comando troppo lunga");
    print(CLR_RESET);
    return -1;
}

/* Definita piu' in basso (il motore degli script sta vicino all'autoexec,
 * che e' il suo primo utente), ma serve al dispatch dei built-in qui
 * sopra. In un file solo l'ordine non puo' accontentare tutti. */
static int esegui_script(const char *nome, const char *etichetta);

/* =============================================================================
 * Comandi built-in
 * ============================================================================= */

/* =============================================================================
 * L'AIUTO DI RISERVA, e nient'altro.
 *
 * L'aiuto vero e' /boot/help.txt, sfogliato da /bin/help: blocchi con
 * un'intestazione, `help ls` che mostra solo quello che serve, e lo
 * scorrimento su e giu'. Vedi bin/help/help.c.
 *
 * ! QUESTO SI VEDE SOLO SE /bin/help NON C'E'. Non e' un secondo elenco
 * da tenere aggiornato — sarebbe la solita coppia di liste che divergono,
 * ed e' proprio cio' che si e' voluto togliere. Dice il minimo per
 * muoversi e dove sta l'aiuto vero: se qualcuno finisce qui, il problema
 * e' un'immagine incompleta, non la mancanza di documentazione.
 * ============================================================================= */
static void cmd_help(void)
{
    print(CLR_YELLOW);
    println("/bin/help non c'e': questo e' l'aiuto di riserva.");
    print(CLR_WHITE);
    println("");
    println("L'aiuto completo sta in /boot/help.txt e lo mostra /bin/help.");
    println("Mancano l'uno o l'altro: l'immagine e' incompleta.");
    println("");
    print(CLR_CYAN);
    println("Per muoversi intanto:");
    print(CLR_WHITE);
    println("  ls [percorso]     elenca i file        cd [dir]   cambia directory");
    println("  cat [file]        mostra un file       pwd        dove sei");
    println("  env / export K=V  variabili            mount      i volumi montati");
    println("  jobs / fg [n]     i comandi con '&'    stack      i processi");
    println("  ver               versione e licenza   helpconfig i driver");
    println("  reboot / halt / poweroff               exit       termina la shell");
    println("");
    println("Un programma di /bin si lancia scrivendone il nome; quasi tutti");
    println("spiegano il proprio uso se lanciati senza argomenti.");
}

/* =============================================================================
 * helpconfig — come si accendono i driver
 *
 * ! MOSTRA LO STATO, NON SOLO LE ISTRUZIONI. Un elenco di comandi da dare
 * lo si trova gia' nel leggimi; quello che al prompt non si sa e' a che
 * punto si e' arrivati. Chiedere al registro IPC chi c'e' costa una
 * syscall per servizio e trasforma "ecco la procedura" in "sei qui".
 *
 * ! SI IMPAGINA A MANO. Lo schermo e' 80x25 e questo testo e' piu' lungo:
 * senza pause le prime pagine scorrerebbero via, cioe' proprio quelle che
 * spiegano da dove si comincia. Le pause stanno dove il discorso cambia
 * argomento, non ogni N righe: una pagina che si interrompe a meta' di un
 * elenco e' peggio di una piu' corta.
 * ============================================================================= */

/* Aspetta Invio. Ritorna 0 se si vuole smettere ('q'), 1 per continuare.
 *
 * ! LEGGE UNA RIGA, NON UN TASTO. La modalita' raw della tastiera
 * appartiene alla modifica della riga di comando; qui basta una lettura
 * normale, e il driver ci torna da solo (vedi kbd_proto.h). Una lettura
 * fallita — nessun servizio tastiera — vale "continua": meglio far
 * scorrere il testo che bloccare la shell su una pausa che nessuno puo'
 * sbloccare. */
static int hc_pausa(void)
{
    char riga[8];
    int  n;

    print(CLR_CYAN);
    print("  -- Invio per continuare, q per smettere --");
    print(CLR_WHITE);

    n = sh_read(STDIN, riga, sizeof(riga) - 1);
    print("\n");
    if (n <= 0) return 1;

    return (riga[0] != 'q' && riga[0] != 'Q');
}

/* Una riga di stato: "[ok] nome" oppure "[manca] nome". */
static void hc_stato(const char *servizio, const char *descrizione)
{
    int c = sh_ipc_lookup(servizio);

    print(c > 0 ? CLR_GREEN : CLR_YELLOW);
    print(c > 0 ? "  [ok]    " : "  [manca] ");
    print(CLR_WHITE);
    println(descrizione);
}

static void cmd_helpconfig(void)
{
    print(CLR_CYAN);
    println("Installare e configurare i driver");
    print(CLR_WHITE);
    println("");
    print(CLR_GREEN);
    println("  Se non vuoi leggere il resto: `hwconfig` guarda cosa c'e' nella");
    println("  macchina e scrive kernel.cfg e autoexec.sh da solo. Mostra tutto");
    println("  prima di chiedere, e mette da parte i file di adesso.");
    print(CLR_WHITE);
    println("");
    println("Un driver di EX-OS e' un programma come gli altri: sta in /dev,");
    println("si lancia dal prompt e gira in ring3. Se si pianta si rilancia,");
    println("e il resto del sistema non se ne accorge.");
    println("");
    println("  /dev/pci.drv &      lo lancia e lo lascia acceso ('&')");
    println("  jobs                mostra che e' ancora vivo");
    println("");
    print(CLR_YELLOW);
    println("  I driver di rete stanno SOLO sul CD di EX-OS, non sul floppy:");
    print(CLR_WHITE);
    println("  in 1.44 MB non ci stanno. Con il floppy da solo i comandi di");
    println("  rete ci sono ma non trovano niente da accendere.");
    println("");
    if (!hc_pausa()) return;

    print(CLR_CYAN);
    println("A che punto sei adesso");
    print(CLR_WHITE);
    println("");
    hc_stato(PCI_SERVIZIO,   "bus PCI          /dev/pci.drv &");
    hc_stato(NET_SERVIZIO_0, "scheda di rete   netdetect -c");
    hc_stato(IP_SERVIZIO,    "stack IP         /dev/ip.drv &");
    hc_stato(KBD_SERVICE_NAME, "tastiera         [modules] in /boot/kernel.cfg");
    println("");
    print(CLR_YELLOW);
    println("  Vanno accesi IN QUEST'ORDINE: ognuno serve al successivo.");
    print(CLR_WHITE);
    println("  Lanciarli tutti insieme fa fallire quelli dopo il primo per");
    println("  un motivo diverso da quello vero.");
    println("");
    println("  L'indirizzo IP non e' un servizio: si vede con 'ipcfg'.");
    println("");
    if (!hc_pausa()) return;

    print(CLR_CYAN);
    println("La rete, dal bus all'indirizzo");
    print(CLR_WHITE);
    println("");
    println("  /dev/pci.drv &      1. enumera il bus PCI");
    println("  netdetect -c        2. riconosce la scheda e avvia il driver");
    println("  /dev/ip.drv &       3. ARP, IPv4, ICMP, UDP, TCP");
    println("  dhcp                4. chiede indirizzo, maschera, gateway, DNS");
    println("");
    println("Senza un server DHCP l'indirizzo si mette a mano:");
    println("");
    println("  ipcfg -a 192.168.1.10 -m 255.255.255.0 -g 192.168.1.1");
    println("");
    println("Poi 'ping 8.8.8.8' e 'ftp 10.0.2.2 ls' funzionano.");
    println("");
    if (!hc_pausa()) return;

    print(CLR_CYAN);
    println("Quando non funziona");
    print(CLR_WHITE);
    println("");
    println("  netdetect           quali schede ci sono e quale driver vuole");
    println("                      ognuna. Se non compare niente, il bus PCI");
    println("                      non e' acceso oppure la scheda non c'e'.");
    println("  nettest -a IP       prova ARP: se risponde, la scheda");
    println("                      trasmette E riceve, e a ping manca solo");
    println("                      software.");
    println("  ipcfg               indirizzo e contatori. 'IP ricevuti' a");
    println("                      zero, 'scartati' o 'somme errate' che");
    println("                      salgono indicano tre punti diversi.");
    println("  ipcfg -r            tabella ARP: chi si e' fatto vedere.");
    println("");
    print(CLR_YELLOW);
    println("  Una NE2000 ISA non si trova da sola: va dichiarata.");
    print(CLR_WHITE);
    println("  Cercarla vorrebbe dire scrivere sulla sua porta di reset, e");
    println("  se li' c'e' un'altra scheda le si scrive addosso.");
    println("");
    println("  /dev/ne2k.drv -p 0x300 -q 3");
    println("");
    if (!hc_pausa()) return;

    print(CLR_CYAN);
    println("Farlo fare all'avvio");
    print(CLR_WHITE);
    println("");
    println("/boot/autoexec.sh: una riga = un comando, come se fosse");
    println("digitato. '#' commenta, '@' esegue senza stampare.");
    println("");
    println("  /dev/pci.drv &");
    println("  netdetect -c");
    println("  /dev/ip.drv &");
    println("  dhcp");
    println("");
    println("Lo esegue solo la shell della prima console.");
    println("");
    print(CLR_YELLOW);
    println("  Se un comando dell'autoexec si blocca: Alt+F2 da' sempre una");
    println("  shell pulita, e 'autoexec=0' in /boot/kernel.cfg lo salta.");
    print(CLR_WHITE);
    println("");
    println("La tastiera segue un'altra strada: e' un driver di avvio, e si");
    println("dichiara nella sezione [modules] di /boot/kernel.cfg. Senza,");
    println("il kernel serve la console con la tastiera interna di ripiego");
    println("e si perde la modalita' raw (niente frecce, niente gfedit).");
}

static void cmd_echo(int argc, char *argv[])
{
    int i;
    for (i = 1; i < argc; i++) {
        print(argv[i]);
        if (i < argc - 1) print(" ");
    }
    print("\n");
}

static void cmd_cls(void)
{
    /* ESC[2J = cancella schermo, ESC[H = cursore a 0,0 */
    print("\x1B[2J\x1B[H");
}

static void cmd_pwd(void)
{
    char buf[256];
    int n = sh_getcwd(buf, sizeof(buf));
    if (n > 0) println(buf);
    else printerr("pwd: errore");
}

static void cmd_cd(int argc, char *argv[])
{
    const char *path = (argc >= 2) ? argv[1] : "/";
    int ret = sh_chdir(path);
    if (ret < 0) {
        print("cd: ");
        print(path);
        printerr(": directory non trovata");
    }
}

static void cmd_env(void)
{
    uint32_t i;
    for (i = 0; i < g_env_count; i++) {
        println(g_env[i]);
    }
}

static void cmd_export(int argc, char *argv[])
{
    if (argc < 2) { printerr("export: uso: export CHIAVE=VALORE"); return; }
    char *eq = argv[1];
    while (*eq && *eq != '=') eq++;
    if (*eq != '=') { printerr("export: formato: CHIAVE=VALORE"); return; }
    *eq = '\0';
    env_set(argv[1], eq + 1);
    *eq = '=';   /* Ripristina */
}

/* =============================================================================
 * cmd_version — comandi `ver` e `version`
 *
 * Stampa la variabile globale g_os_version del kernel, recuperata con
 * SYS_VERSION. La shell non conosce nome, versione, autore o licenza: li
 * chiede, così incrementare EXOS_VERSION in kernel/include/version.h si
 * riflette qui senza ricompilare nulla di userspace concettualmente
 * (in pratica il floppy si ricostruisce comunque, ma non esiste una
 * seconda copia della stringa da tenere allineata).
 * ============================================================================= */
static void cmd_version(void)
{
    char     buf[256];
    uint32_t img = 0, hp = 0, pl = 0;
    int      n = sh_version(buf, sizeof(buf));

    if (n < 0) {
        printerr("version: il kernel non ha fornito l'identita' di sistema");
        return;
    }

    print(CLR_GREEN);
    println(buf);
    print(CLR_WHITE);

    /* Le stesse cose che dicono le finestre «Informazioni su» delle
     * applicazioni grafiche: nome, a cosa serve, chi l'ha scritta, e quanto
     * occupa. La shell non ha un menu, quindi il posto e' `ver`. */
    println("");
    println("Shell");
    println("L'interprete di comandi di EX-OS: pipe, redirezioni, lavori in");
    println("background, cronologia e completamento.");
    println("");
    println(EXINFO_AUTORE "  -  " EXINFO_EMAIL);
    println("");

    sh_memoria(&img, &hp, &pl);
    print("Memoria di questo programma: ");
    print_uint((img + hp + pl + 1023u) / 1024u);
    println(" KB");
    print("  programma ");   print_uint((img + 1023u) / 1024u);
    print(" KB, heap ");     print_uint((hp  + 1023u) / 1024u);
    print(" KB, pila ");     print_uint((pl  + 1023u) / 1024u);
    println(" KB");
    println("  (le librerie condivise non sono contate: sono di tutti)");
}

/* ! `uname` NON E' PIU' UN BUILT-IN, ed e' un programma: /bin/uname.
 *
 * Quello che c'era qui stampava una frase per una persona — «EX-OS version
 * 0.176 (x86 32-bit) - Copyright ...» — e andava bene finche' a leggerla
 * c'era una persona. Un makefile scrive `ifeq ($(shell uname),Linux)` e
 * confronta con UNA PAROLA: una frase non combacia con niente.
 *
 * ! E soprattutto: GNU make NON PASSA DALLA SHELL per un comando senza
 * metacaratteri, lo lancia diretto. Quindi `$(shell uname)` non poteva
 * vedere nessun built-in — cercava un eseguibile, e rispondeva «fork: file
 * o directory inesistente».
 *
 * Tenerli tutti e due avrebbe voluto dire che `uname` risponde due cose
 * diverse a seconda di chi lo chiama. La frase per le persone c'era gia'
 * altrove e c'e' ancora: si chiama `ver`. */

static void cmd_pid(void)
{
    int pid = sh_getpid();
    print("PID: ");
    print_uint((uint32_t)pid);
    print("\n");
}

static void cmd_sleep(int argc, char *argv[])
{
    if (argc < 2) { printerr("sleep: uso: sleep [millisecondi]"); return; }
    uint32_t ms = 0;
    const char *s = argv[1];
    while (*s >= '0' && *s <= '9') ms = ms * 10 + (uint32_t)(*s++ - '0');
    /* syscall sleep */
    __asm__ volatile (
        "int $0x80"
        :: "a"(SYS_SLEEP), "b"(ms)
        : "memory"
    );
}

/* =============================================================================
 * JOB CONTROL
 *
 * `comando &` non aspetta e ridà subito il prompt; `jobs` elenca ciò che
 * sta ancora girando; `fg [n]` riporta un job in primo piano e ne aspetta
 * la fine.
 *
 * Non c'è `bg`, e non è una dimenticanza: `bg` riprende un processo
 * SOSPESO, e per sospenderlo servirebbe un Ctrl+Z — cioè i segnali, che
 * EX-OS non ha. Un job qui o gira o è finito, non esiste lo stato in
 * mezzo.
 *
 * DOVE FINISCE L'OUTPUT. Un job in background scrive sulla stessa
 * console della shell, quindi le sue righe si mescolano al prompt. È il
 * comportamento di qualunque shell Unix, e la via d'uscita è la stessa:
 * se il programma ha bisogno dello schermo tutto per sé, si lancia su
 * un'altra console con Alt+Fn invece che con '&'.
 *
 * L'INPUT invece è protetto: sys_read su stdin restituisce la fine
 * dell'input a chi non è in primo piano. Senza, un job in background che
 * legge sostituirebbe la shell come lettore della tastiera e il prompt
 * non riceverebbe mai più una riga (vedi la guardia in sys_read).
 * ============================================================================= */
#define JOBS_MAX    8
#define JOB_CMD_LEN 48

typedef struct {
    int  in_use;
    int  pid;
    int  numero;                /* numero mostrato da 'jobs', 1-based */
    char cmd[JOB_CMD_LEN];
} Job;

static Job g_jobs[JOBS_MAX];
static int g_job_seq = 0;

static void job_aggiungi(int pid, const char *cmd)
{
    int i;

    for (i = 0; i < JOBS_MAX; i++) {
        if (g_jobs[i].in_use) continue;

        g_jobs[i].in_use = 1;
        g_jobs[i].pid    = pid;
        g_jobs[i].numero = ++g_job_seq;
        sh_strcpy(g_jobs[i].cmd, cmd, JOB_CMD_LEN);

        print("[");
        print_uint((uint32_t)g_jobs[i].numero);
        print("] ");
        print_uint((uint32_t)pid);
        print("\n");
        return;
    }

    printerr("jobs: tabella piena, il processo gira ma non e' elencato");
}

/* Cerca un job per numero. Con numero <= 0 restituisce il piu' recente,
 * che e' quello che 'fg' senza argomenti deve riprendere. */
static Job *job_trova(int numero)
{
    int i;
    Job *scelto = NULL;

    for (i = 0; i < JOBS_MAX; i++) {
        if (!g_jobs[i].in_use) continue;
        if (numero > 0) {
            if (g_jobs[i].numero == numero) return &g_jobs[i];
            continue;
        }
        if (!scelto || g_jobs[i].numero > scelto->numero) scelto = &g_jobs[i];
    }
    return scelto;
}

/* Raccoglie i job finiti e li annuncia. Chiamata a ogni prompt: è così
 * che si scopre che un job è terminato senza doverlo chiedere, ed è anche
 * l'unico modo di liberare lo slot di processo — un figlio terminato
 * resta ZOMBIE finché il padre non lo raccoglie (il reaper di init si
 * occupa solo degli orfani). */
static void job_raccogli(void)
{
    int i;

    for (i = 0; i < JOBS_MAX; i++) {
        int32_t status = 0;
        int     r;

        if (!g_jobs[i].in_use) continue;

        r = sh_waitpid(g_jobs[i].pid, &status, WNOHANG);

        if (r == 0) continue;            /* ancora in esecuzione */

        if (r > 0) {
            print("[");
            print_uint((uint32_t)g_jobs[i].numero);
            print("] terminato: ");
            print(g_jobs[i].cmd);
            print(" (codice ");
            print_uint((uint32_t)status);
            print(")\n");
        } else {
            /* -ECHILD: qualcun altro l'ha gia' raccolto, o non e' mai
             * esistito. Lo slot va liberato comunque, o resterebbe a
             * mentire in 'jobs' per sempre. */
            print("[");
            print_uint((uint32_t)g_jobs[i].numero);
            print("] sparito: ");
            print(g_jobs[i].cmd);
            print("\n");
        }
        g_jobs[i].in_use = 0;
    }
}

static void cmd_jobs(void)
{
    int i, trovati = 0;

    job_raccogli();

    for (i = 0; i < JOBS_MAX; i++) {
        if (!g_jobs[i].in_use) continue;
        trovati++;
        print("[");
        print_uint((uint32_t)g_jobs[i].numero);
        print("] PID ");
        print_uint((uint32_t)g_jobs[i].pid);
        print("  ");
        print(g_jobs[i].cmd);
        print("\n");
    }

    if (trovati == 0) print("Nessun job in esecuzione.\n");
}

static void cmd_fg(int argc, char *argv[])
{
    int     numero = 0;
    Job    *j;
    int32_t status = 0;
    int     r;

    job_raccogli();

    if (argc >= 2) {
        const char *p = argv[1];
        if (*p == '%') p++;              /* si accetta anche "fg %2" */
        while (*p >= '0' && *p <= '9') numero = numero * 10 + (*p++ - '0');
        if (numero == 0) { printerr("fg: uso: fg [numero]"); return; }
    }

    j = job_trova(numero);
    if (!j) {
        printerr(argc >= 2 ? "fg: nessun job con quel numero"
                           : "fg: nessun job in esecuzione");
        return;
    }

    print(j->cmd);
    print("\n");

    /* La tastiera passa al job: da qui in poi e' lui a poter leggere da
     * stdin, e la shell — che comunque sta per bloccarsi in waitpid —
     * smette di essere il lettore legittimo. */
    sh_setfg(j->pid);
    r = sh_waitpid(j->pid, &status, 0);
    sh_setfg(sh_getpid());

    if (r < 0) printerr("fg: quel processo non esiste piu'");
    j->in_use = 0;
}

/* Lancia il figlio e ne aspetta la fine, oppure lo mette fra i job.
 *
 * In primo piano la tastiera passa al figlio per tutta la durata: e'
 * quello che permette a un programma interattivo di leggere da stdin
 * mentre la shell e' ferma in waitpid. Al ritorno la si riprende. */
static void avvia_figlio(int pid, int background, const char *cmdline)
{
    int32_t status;

    if (background) {
        job_aggiungi(pid, cmdline);
        return;
    }

    sh_setfg(pid);
    if (sh_waitpid(pid, &status, 0) >= 0) {
        /* ! `status` E' GIA' IL CODICE DI USCITA, non l'intero impacchettato
         * di Unix: EX-OS non consegna segnali, quindi il modo di finire e'
         * uno solo e il kernel ci scrive il numero e basta. Vedi il
         * commento su waitpid in lib/include/libc.h. */
        g_ultimo_stato = (int)status;
    }
    sh_setfg(sh_getpid());
}

/* Vero se `path` esiste ed e' un file che si puo' aprire. Una directory
 * non passa: il VFS la rifiuta con EISDIR (vedi kernel/fs/vfs.c), quindi
 * una cartella di nome `ls` dentro una voce del PATH non viene scambiata
 * per il comando. */
static int file_esiste(const char *path)
{
    int fd = syscall3(SYS_OPEN, (uint32_t)path, 0, 0);
    if (fd < 0) return 0;
    syscall1(SYS_CLOSE, (uint32_t)fd);
    return 1;
}

/* Vero se `nome` contiene almeno una barra.
 *
 * ! E' QUESTA la condizione che decide se cercare nel PATH — non «inizia
 * con /», che e' cio' che si guardava fino ad agosto 2026. E' la regola di
 * execvp, ed e' la stessa che applica spawn_ex() in lib/libc.c: un nome
 * NUDO si cerca nel PATH, mentre qualunque cosa contenga una barra e' gia'
 * un percorso e si usa com'e', relativo alla directory corrente — a
 * risolverlo ci pensa resolve_path() nel kernel.
 *
 * Guardare solo il primo carattere faceva finire `./mioprog` nella
 * ricerca, e li' succedeva una delle due:
 *
 *   - nessuna voce del PATH aveva un omonimo, e il programma che stava
 *     nella directory corrente veniva dichiarato inesistente;
 *   - una ce l'aveva, e allora era peggio: `/bin/./mioprog` si riduce a
 *     `/bin/mioprog`, quindi partiva UN ALTRO PROGRAMMA con lo stesso
 *     nome, senza che niente lo segnalasse.
 *
 * Vale per `sotto/prog` esattamente come per `./prog`. */
static int ha_barra(const char *nome)
{
    while (*nome) if (*nome++ == '/') return 1;
    return 0;
}

/* Compone in `dst` la prima voce del PATH sotto cui `prog` esiste
 * davvero. Ritorna 0 se l'ha trovato, -1 se nessuna voce combacia.
 *
 * ! SI CHIEDE «C'E'?», NON SI PROVA A LANCIARLO. Fino ad agosto 2026 il
 * ciclo chiamava spawn() su ogni voce e usava il fallimento come
 * risposta. Costava caro due volte: per ogni voce sbagliata il kernel
 * creava un processo, ci caricava sopra un ELF inesistente, lo uccideva e
 * lo raccoglieva; e siccome «file non trovato» era un LOG_ERROR — che
 * kernel.cfg stampa SEMPRE, qualunque sia loglevel — un comando battuto
 * male riempiva lo schermo di due righe rosse per voce del PATH, sei in
 * tutto, prima dell'unica riga che l'utente doveva leggere.
 *
 * E' lo stesso controllo che fa spawn_cerca_path() in lib/libc.c, dove la
 * ricerca e' sempre stata silenziosa. */
static int cerca_nel_path(const char *prog, char *dst, uint32_t dim)
{
    const char *p = env_get("PATH");

    if (!p || !*p) p = "/bin";

    while (*p) {
        uint32_t i = 0;

        while (*p && *p != ':' && i < dim - 64) dst[i++] = *p++;
        /* Voce piu' lunga del buffer: si scarta INTERA. Fermarsi a meta'
         * lascerebbe la coda a fare da voce successiva, e si cercherebbe
         * il comando in una cartella che nessuno ha scritto nel PATH. */
        while (*p && *p != ':') p++;
        if (*p == ':') p++;
        if (i == 0) continue;

        if (dst[i - 1] != '/') dst[i++] = '/';
        sh_strcpy(dst + i, prog, dim - i);

        if (file_esiste(dst)) return 0;
    }

    return -1;
}

/* Riferisce perche' `sh_spawn`/`sh_exec` ha detto di no su un percorso
 * che era stato trovato, e imposta lo stato d'uscita di conseguenza.
 *
 * I due codici sono la convenzione di ogni shell, e contano davvero da
 * quando esiste `sh -c`: chi chiama system() distingue cosi' un comando
 * che non c'e' (127) da un comando che c'e' e non parte (126) da un
 * comando partito e finito male (il suo codice). */
static void spiega_avvio_fallito(const char *path, int err)
{
    if (err == -SH_ENOENT) {
        print("exec: comando non trovato: ");
        printerr(path);
        g_ultimo_stato = 127;
        return;
    }
    print("exec: ");
    print(path);
    if (err == -SH_EACCES) {
        /* ! IL RIMEDIO SI DICE, E DEV'ESSERE UN COMANDO CHE ESISTE. La prima
         * versione suggeriva `ls -l` per guardare i permessi: `ls` di EX-OS
         * l'opzione -l non ce l'ha, e un consiglio che non si puo' seguire e'
         * peggio di nessun consiglio. */
        printerr(": non hai il permesso di eseguirlo");
        print("      Se il file e' tuo:  chmod 755 ");
        printerr(path);
    } else {
        printerr(err == -SH_ENOEXEC ? ": non e' un programma eseguibile"
                                    : ": impossibile avviarlo");
    }
    g_ultimo_stato = 126;
}

/* =============================================================================
 * run_program — trova il programma, lo lancia, e decide chi lo aspetta.
 *
 * `extra` puo' essere NULL: allora il figlio riceve solo l'ambiente. Se c'e',
 * porta anche le redirezioni — un file aperto dalla shell, o un'estremita' di
 * pipe — e questa funzione non ne sa niente: le ha gia' preparate chi chiama.
 *
 * `pid_out` distingue i due modi di lanciare:
 *
 *   NULL      si aspetta la fine qui dentro (o si mette fra i job): e' il
 *             comportamento di un comando semplice.
 *   non NULL  ci si scrive il PID e si torna SUBITO. E' cio' che serve a una
 *             pipe, dove tutti gli anelli devono girare INSIEME: aspettare il
 *             primo prima di lanciare il secondo sarebbe uno stallo garantito
 *             appena il primo scrive piu' di quanto la pipe tiene in pancia.
 *
 * ! NON C'E' PIU' NESSUNA COPIA DI argv. Fino ad agosto 2026 qui c'era un
 * `char *spawn_argv[32]` con dentro un `spawn_argc < 31`: gli argomenti oltre
 * il trentunesimo sparivano in silenzio, un livello sopra il kernel che
 * proprio quel troncamento aveva smesso di fare. argv[] arriva gia'
 * NULL-terminato da parse_line e si passa com'e'.
 *
 * ! argv[0] E' IL PERCORSO TROVATO, NON IL NOME SCRITTO, e va lasciato cosi'.
 * Una shell Unix ci mette il nome com'e' stato battuto; qui ci va il percorso
 * intero perche' GCC e FreeBASIC CALCOLANO IL PROPRIO PREFISSO da argv[0] —
 * «la directory dell'eseguibile meno bin» — ed e' da li' che ritrovano cc1,
 * libexec, i propri header e le proprie librerie. Con il nome nudo dovrebbero
 * ricercarsi nel PATH per sapere dove sono, e il giorno che quella ricerca
 * trovasse un omonimo prima di loro, il compilatore userebbe i pezzi di un
 * altro. Vedi il commento sull'albero /exos nella regola del CD, nel Makefile.
 * ============================================================================= */
static void run_program(const char *prog, int argc, char *argv[],
                        int background, const char *cmdline,
                        SpawnExtra *extra, int *pid_out)
{
    char        path[PATH_MAX_SH];
    const char *da_lanciare = prog;
    SpawnExtra  solo_ambiente;
    int         pid;

    (void)argc;

    if (!ha_barra(prog)) {
        if (cerca_nel_path(prog, path, PATH_MAX_SH) != 0) {
            print("exec: comando non trovato: ");
            printerr(prog);
            g_ultimo_stato = 127;
            if (pid_out) *pid_out = -1;
            return;
        }
        da_lanciare = path;
    }

    /* Senza redirezioni resta comunque una cosa da passare: l'ambiente.
     * Vedi env_eredita — un anello della catena che lo azzera fa girare il
     * comando in fondo con il PATH di kernel.cfg invece che con quello che
     * il padre aveva preparato. */
    if (extra == NULL) {
        solo_ambiente.magia    = SPAWN_EXTRA_MAGIA;
        solo_ambiente.envp     = env_vettore();
        solo_ambiente.n_azioni = 0;
        extra = &solo_ambiente;
    }

    /* argv[0] diventa il percorso per la durata della spawn e torna com'era
     * subito dopo: chi ci ha passato l'array lo riusa per l'elenco dei job e
     * per i messaggi d'errore, e se lo ritroverebbe cambiato sotto. */
    {
        char *argv0_scritto = argv[0];

        argv[0] = (char *)da_lanciare;
        pid = sh_spawn_ex(da_lanciare, argc, argv, extra);
        argv[0] = argv0_scritto;
    }

    if (pid > 0) {
        if (pid_out) { *pid_out = pid; return; }
        avvia_figlio(pid, background, cmdline);
        return;
    }

    /* Il file c'era e lo spawn e' fallito lo stesso: il motivo e' un
     * altro e va detto com'e'. Prima si passava alla voce successiva del
     * PATH e si finiva per dire «comando non trovato» di un binario che
     * esiste — mandando a cercare il guasto dove non era. */
    spiega_avvio_fallito(da_lanciare, pid);
    if (pid_out) *pid_out = -1;
}

static void run_program_replace(const char *prog, int argc, char *argv[])
{
    char        path[PATH_MAX_SH];
    const char *da_lanciare = prog;
    int         ret;

    (void)argc; (void)argv;

    if (!ha_barra(prog)) {
        if (cerca_nel_path(prog, path, PATH_MAX_SH) != 0) {
            print("exec: comando non trovato: ");
            printerr(prog);
            g_ultimo_stato = 127;
            return;
        }
        da_lanciare = path;
    }

    /* Se ha successo non torna: l'immagine di questo processo e' stata
     * sostituita. Qui ci si arriva solo avendo fallito. */
    ret = sh_exec(da_lanciare);
    spiega_avvio_fallito(da_lanciare, ret);
}

static void cmd_exec(int argc, char *argv[])
{
    if (argc < 2) { printerr("exec: uso: exec [programma]"); return; }
    run_program_replace(argv[1], argc - 1, argv + 1);
}

/* -----------------------------------------------------------------------------
 * cat
 *
 * ! SENZA NOME DI FILE LEGGE DA stdin, ed e' cio' che lo rende utilizzabile in
 * una pipe. Prima chiedeva per forza un nome, quindi `hello | cat` rispondeva
 * «uso: cat [file]» — e sembrava un difetto della pipe, mentre la pipe era
 * costruita bene: la shell mette un builtin in fondo a una pipeline dentro un
 * `/bin/sh -c` figlio, che il descrittore 0 ce l'ha giusto. Era cat a non
 * guardarlo.
 * --------------------------------------------------------------------------- */
static int stdin_e_console(void);       /* definita piu' sotto, vedi il ciclo del prompt */

static void cmd_cat(int argc, char *argv[])
{
    char buf[512];
    int  fd, n;

    if (argc < 2) {
        /* ! MA SOLO SE stdin NON E' LA CONSOLE. Battuto al prompt, un `cat`
         * senza argomenti si metterebbe a leggere la tastiera per sempre, e
         * senza Ctrl+C non se ne uscirebbe piu': la console resterebbe muta e
         * sembrerebbe bloccata. Dietro una pipe quella domanda non esiste,
         * perche' la pipe finisce. E' la stessa prova che fa riga_modifica(),
         * e per la stessa ragione. */
        if (stdin_e_console()) { printerr("cat: uso: cat [file]"); return; }

        while ((n = sh_read(STDIN, buf, sizeof(buf))) > 0)
            sh_write(STDOUT, buf, (uint32_t)n);
        return;
    }

    fd = syscall3(SYS_OPEN, (uint32_t)argv[1], 0, 0);
    if (fd < 0) {
        print("cat: ");
        print(argv[1]);
        printerr(": file non trovato");
        return;
    }

    /* Leggi e stampa a blocchi */
    while ((n = sh_read(fd, buf, sizeof(buf))) > 0) {
        sh_write(STDOUT, buf, (uint32_t)n);
    }

    syscall1(SYS_CLOSE, (uint32_t)fd);
}

static void cmd_reboot(void)
{
    syscall1(SYS_REBOOT, EXOS_RB_RESTART);
    printerr("reboot: il kernel ha rifiutato la richiesta di riavvio");
}

/* =============================================================================
 * cmd_halt / cmd_poweroff / cmd_reboot
 *
 * BUG CORRETTO (luglio 2026): queste funzioni eseguivano direttamente
 * `cli; hlt` (halt) e `inb`/`outb` sulla porta 0x64 (reboot). Sono
 * istruzioni PRIVILEGIATE e la shell gira in ring3: sollevavano #GP, che
 * il kernel — non avendo un handler per quel vettore — trasformava in
 * KERNEL PANIC. Da cui "halt mostra un kernel panic". Anche `reboot` era
 * rotto nello stesso modo, solo che il panic arrivava dopo.
 *
 * Ora la sequenza sta nel kernel (SYS_REBOOT -> kernel/arch/x86/power.c),
 * che oltre a poter eseguire quelle istruzioni sincronizza il filesystem
 * — cosa che dalla shell non era comunque possibile fare.
 * ============================================================================= */
static void cmd_poweroff(void)
{
    /* Non stampa nulla: da qui in poi parla il kernel, che ha il conto
     * alla rovescia e l'esito della sincronizzazione. */
    syscall1(SYS_REBOOT, EXOS_RB_POWEROFF);

    /* Raggiunta solo se il kernel ha rifiutato la richiesta. */
    printerr("poweroff: il kernel ha rifiutato la richiesta di spegnimento");
}

static void cmd_halt(void)
{
    syscall1(SYS_REBOOT, EXOS_RB_HALT);
    printerr("halt: il kernel ha rifiutato la richiesta di arresto");
}

/* =============================================================================
 * Prompt
 * ============================================================================= */
static void print_prompt(void)
{
    char cwd[128];
    sh_getcwd(cwd, sizeof(cwd));

    print(CLR_GREEN);
    print("ex-os");
    print(CLR_WHITE);
    print(":");
    print(CLR_CYAN);
    print(cwd);
    print(CLR_WHITE);
    print("> ");
}

/* =============================================================================
 * Banner di avvio shell
 * ============================================================================= */
/* Nome/versione/autore vengono dall'ambiente, che env_init() ha appena
 * popolato da /boot/kernel.cfg: modificare OSNAME nel file cambia davvero
 * quello che si legge qui. env_get() non ritorna mai NULL per queste
 * chiavi (env_init garantisce sempre un fallback), ma il controllo resta
 * perché print() su NULL sarebbe un fault in ring3. */
static void print_banner(void)
{
    const char *osname = env_get("OSNAME");
    const char *osver  = env_get("OSVER");
    const char *author = env_get("AUTHOR");

    if (!osname) osname = "EX-OS";
    if (!osver)  osver  = "?";
    if (!author) author = "Graziano Falcone";

    print(CLR_CYAN);
    println("  ============================================");
    print("   "); print(osname);
    print(" - Extensible Operating System v"); print(osver); print("\n");
    print("   Copyright (C) 2025 "); print(author); print("\n");
    println("   <exagonx@hotmail.com>");
    println("   Licenza: GNU GPL v2");
    println("  ============================================");
    print(CLR_WHITE);
    println("  Digita 'help' per l'elenco dei comandi.\n");
}

/* =============================================================================
 * Esegue UN comando semplice: i built-in, oppure un programma.
 *
 * ! ESTRATTA DAL CICLO PRINCIPALE, e non per eleganza: serviva un secondo
 * chiamante. L'autoexec deve eseguire i comandi ESATTAMENTE come li
 * esegue chi li digita — stessi built-in, stesse virgolette, stesso '&'
 * per il background. Riscrivere un secondo interprete accanto al primo
 * avrebbe significato due comportamenti che divergono al primo comando
 * che si aggiunge da una parte sola.
 *
 * ! RICEVE argv GIA' FATTO, non la riga. Sopra di lei ci sono adesso tre
 * strati — sequenze, pipe, redirezioni — e ognuno consuma un pezzo di
 * sintassi prima di passare oltre. Questa e' l'ultima fermata: qui non ci
 * sono piu' operatori, solo un comando e i suoi argomenti.
 *
 * Ritorna 0 normalmente, -1 se il comando era `exit`.
 * ============================================================================= */
static int esegui_builtin(int argc, char **argv, int background,
                          const char *cmdline)
{
    if (argc <= 0) return 0;

    /* Dispatch comandi built-in */
    const char *cmd = argv[0];

    /* ! UNO SCRIPT SI ESEGUE, NON SI LANCIA. `source` non e' un
     * eseguibile: i comandi devono girare in QUESTA shell, altrimenti un
     * `cd` o un `export` dentro lo script sparirebbero insieme al
     * processo figlio — che e' il motivo per cui `source` esiste anche
     * sulle shell vere. */
    if (sh_strcmp(cmd, "source") == 0) {
        if (argc < 2) { printerr("uso: source <file.sh>"); return 0; }
        return esegui_script(argv[1], argv[1]);
    }

    /* ! UN NOME CHE FINISCE IN .sh E' UNO SCRIPT, e si decide QUI e non
     * dopo che la spawn e' fallita. Il ripiego sull'errore sembra piu'
     * tollerante ed e' peggio: la spawn fallisce per molti motivi — file
     * assente, ELF corrotto, memoria finita — e trattarli tutti come
     * "sara' uno script" trasforma un errore preciso in un secondo errore
     * che parla d'altro.
     *
     * Il prezzo dichiarato: un eseguibile ELF chiamato `qualcosa.sh` non
     * si lancia piu' per nome. E' un nome che nessuno da' a un binario. */
    {
        int l = 0;
        while (cmd[l]) l++;
        if (l > 3 && cmd[l-3] == '.' && cmd[l-2] == 's' && cmd[l-1] == 'h')
            return esegui_script(cmd, cmd);
    }

    /* `help helpconfig` e `helpconfig` da solo fanno la stessa cosa: chi
     * ha letto la riga nell'aiuto prova l'una, chi se l'e' sentita dire
     * prova l'altra, e far fallire la seconda sarebbe gratuito. */
    if (sh_strcmp(cmd, "helpconfig") == 0) { cmd_helpconfig(); return 0; }
    if (sh_strcmp(cmd, "help")  == 0) {
        if (argc >= 2 && sh_strcmp(argv[1], "helpconfig") == 0) {
            cmd_helpconfig();
            return 0;
        }

        /* ! L'AIUTO NON E' PIU' QUI DENTRO. Il testo sta in
         * /boot/help.txt e lo sfoglia /bin/help — che sa impaginare,
         * scorrere e mostrare un blocco solo (`help ls`), cose che una
         * sequenza di println() non puo' fare. Vedi bin/help/help.c.
         *
         * Resta un built-in perche' `help` deve funzionare comunque:
         * se il programma non c'e', invece di «comando non trovato» si
         * stampa l'essenziale qui sotto. E' il comando che il banner
         * suggerisce a ogni avvio — non puo' essere il primo a rompersi
         * su un'immagine incompleta. */
        {
            char percorso[PATH_MAX_SH];

            /* Si cerca PRIMA di lanciare. Passando da run_program, un
             * /bin/help assente stamperebbe «comando non trovato: help»
             * e solo dopo il ripiego: due messaggi, di cui il primo
             * manda a cercare un comando che l'utente non ha scritto. */
            if (cerca_nel_path("help", percorso, PATH_MAX_SH) == 0)
                run_program("help", argc, argv, background, cmdline,
                            NULL, NULL);
            else
                cmd_help();
        }
        return 0;
    }
    if (sh_strcmp(cmd, "echo")  == 0) { cmd_echo(argc,argv);  return 0; }
    if (sh_strcmp(cmd, "cls")   == 0 ||
        sh_strcmp(cmd, "clear") == 0) { cmd_cls();            return 0; }
    if (sh_strcmp(cmd, "pwd")   == 0) { cmd_pwd();            return 0; }
    if (sh_strcmp(cmd, "cd")    == 0) { cmd_cd(argc, argv);   return 0; }
    if (sh_strcmp(cmd, "env")   == 0) { cmd_env();            return 0; }
    if (sh_strcmp(cmd, "export")== 0) { cmd_export(argc,argv);return 0; }
    if (sh_strcmp(cmd, "ver")   == 0) { cmd_version();        return 0; }
    if (sh_strcmp(cmd, "version") == 0) { cmd_version();      return 0; }
    if (sh_strcmp(cmd, "pid")   == 0) { cmd_pid();            return 0; }
    if (sh_strcmp(cmd, "jobs")  == 0) { cmd_jobs();           return 0; }
    if (sh_strcmp(cmd, "fg")    == 0) { cmd_fg(argc, argv);   return 0; }
    if (sh_strcmp(cmd, "sleep") == 0) { cmd_sleep(argc,argv); return 0; }
    if (sh_strcmp(cmd, "cat")   == 0) { cmd_cat(argc, argv);  return 0; }
    if (sh_strcmp(cmd, "exec")  == 0) { cmd_exec(argc, argv); return 0; }
    if (sh_strcmp(cmd, "reboot")== 0) { cmd_reboot();         return 0; }
    if (sh_strcmp(cmd, "halt")  == 0) { cmd_halt();           return 0; }
    if (sh_strcmp(cmd, "poweroff") == 0) { cmd_poweroff();    return 0; }
    if (sh_strcmp(cmd, "shutdown") == 0) { cmd_poweroff();    return 0; }

    if (sh_strcmp(cmd, "exit")  == 0) {
        int code = (argc >= 2) ? (int)(*argv[1] - '0') : 0;
        sh_exit(code);
    }

    /* Ci si arriva solo se e_builtin() ha detto di si' su un nome che il
     * dispatch qui sopra non conosce: i due elenchi sono usciti allineati.
     * Vedi il ! su e_builtin — e' il difetto che quel commento descrive,
     * detto nel momento in cui succede invece che come un anello di pipe
     * silenziosamente vuoto. */
    (void)background; (void)cmdline;
    print(CLR_YELLOW);
    print("sh: built-in dichiarato e non implementato: ");
    println(cmd);
    print(CLR_RESET);
    g_ultimo_stato = 127;
    return 0;
}

/* Vero se `cmd` lo esegue la shell stessa invece di lanciare un programma.
 *
 * ! QUESTO ELENCO DEVE RESTARE ALLINEATO al dispatch di esegui_comando qui
 * sopra. Serve a una cosa sola, ma importante: dentro una pipe un built-in non
 * puo' girare nella shell (scriverebbe sulla console invece che nella pipe), e
 * chi costruisce la pipe deve saperlo PRIMA di aver creato i descrittori.
 * Un nome che manca qui e c'e' la' e' un anello di pipe che non produce
 * niente e non lo dice. */
static int e_builtin(const char *cmd)
{
    static const char *const nomi[] = {
        "source", "helpconfig", "help", "echo", "cls", "clear", "pwd", "cd",
        "env", "export", "ver", "version", "pid", "jobs", "fg",
        "sleep", "cat", "exec", "reboot", "halt", "poweroff", "shutdown",
        "exit", NULL
    };
    int i;

    for (i = 0; nomi[i]; i++) if (sh_strcmp(cmd, nomi[i]) == 0) return 1;
    return 0;
}

/* =============================================================================
 * LE REDIREZIONI
 *
 * ! LI APRE LA SHELL, NON IL FIGLIO, anche se il kernel saprebbe fare
 * l'altra cosa (SPAWN_AZ_FILE, «apri questo percorso e mettilo su questo
 * fd»). Aprirli qui costa una syscall in piu' e ne guadagna tre cose:
 *
 *   1. `2>&1` diventa esprimibile. Quel costrutto vuol dire «lo stesso posto
 *      dove va stdout», e se stdout e' un file appena aperto, il figlio non
 *      ha nessun modo di nominarlo — il percorso l'ha gia' consumato l'altra
 *      redirezione. Con il file aperto qui sono due azioni che puntano allo
 *      STESSO descrittore, ed e' esattamente cio' che `2>&1` significa.
 *   2. L'errore lo vede la shell. `> /disco/pieno/f` fallisce prima che il
 *      programma parta, e a dirlo e' chi ha letto la riga di comando.
 *   3. Le pipe e i file diventano la stessa cosa: entrambe finiscono nel
 *      figlio come «prendi questo mio descrittore», SPAWN_AZ_FD. Un modo solo
 *      invece di due.
 *
 * `fd[i]` e' quale descrittore DELLA SHELL diventera' il descrittore `i` del
 * figlio. Fuori dalle redirezioni vale i stesso, cioe' «lascialo com'e'».
 * ============================================================================= */
typedef struct {
    int fd[3];
    int chiudere[SPAWN_MAX_AZIONI];   /* quelli aperti da noi */
    int n_chiudere;
} Redir;

static void redir_init(Redir *r)
{
    r->fd[0] = 0; r->fd[1] = 1; r->fd[2] = 2;
    r->n_chiudere = 0;
}

/* ! VA CHIAMATA ANCHE QUANDO TUTTO E' ANDATO BENE. Un fd lasciato aperto
 * nella shell non e' solo una perdita: su una pipe e' un guasto, perche' la
 * pipe conta ancora uno scrittore vivo — noi — e chi legge non vedra' mai la
 * fine dei dati. E' l'errore descritto in testa a pipe() in libc.h, e qui si
 * presenterebbe come un `make` che si ferma per sempre a meta' di una
 * ricetta con la pipe dentro. */
static void redir_chiudi(Redir *r)
{
    while (r->n_chiudere > 0) sh_close(r->chiudere[--r->n_chiudere]);
}

/* Toglie da argv gli operatori e i loro argomenti, e annota in `r` dove
 * vanno a finire i tre descrittori. Ritorna 0, oppure -1 se un file non si
 * e' potuto aprire (l'ha gia' detto). */
static int redir_estrai(int *argc, char **argv, int *oper, Redir *r)
{
    int i, out = 0;

    for (i = 0; i < *argc; i++) {
        uint32_t    flags;
        int         quale;      /* 0=stdin 1=stdout 2=stderr */
        int         fd;

        if (oper[i] == OP_NESSUNO) { argv[out] = argv[i]; out++; continue; }

        /* `2>&1` non ha un nome di file dopo: dice soltanto «dove va
         * l'altro». Si legge la destinazione ATTUALE di stdout, che e'
         * corretta anche se il `> f` viene prima — ed e' sbagliata se viene
         * dopo, esattamente come su una shell vera. L'ordine conta, ed e'
         * l'unica cosa da ricordare di questo costrutto. */
        if (oper[i] == OP_ERR2OUT) { r->fd[2] = r->fd[1]; continue; }

        if (i + 1 >= *argc || oper[i + 1] != OP_NESSUNO) {
            print(CLR_YELLOW);
            println("sh: manca il nome del file dopo la redirezione");
            print(CLR_RESET);
            g_ultimo_stato = 2;
            return -1;
        }

        switch (oper[i]) {
        case OP_OUT:    quale = 1; flags = O_WRONLY|O_CREAT|O_TRUNC;  break;
        case OP_APP:    quale = 1; flags = O_WRONLY|O_CREAT|O_APPEND; break;
        case OP_ERR:    quale = 2; flags = O_WRONLY|O_CREAT|O_TRUNC;  break;
        case OP_ERRAPP: quale = 2; flags = O_WRONLY|O_CREAT|O_APPEND; break;
        default:        quale = 0; flags = O_RDONLY;                  break;
        }

        fd = sh_open(argv[i + 1], flags);
        if (fd < 0) {
            print(CLR_YELLOW);
            print("sh: non riesco ad aprire ");
            println(argv[i + 1]);
            print(CLR_RESET);
            g_ultimo_stato = 1;
            return -1;
        }

        /* Non puo' traboccare: gli slot sono SPAWN_MAX_AZIONI e le
         * destinazioni possibili sono tre. Una quarta redirezione
         * sovrascrive la destinazione di una delle tre, non ne aggiunge. */
        if (r->n_chiudere < SPAWN_MAX_AZIONI)
            r->chiudere[r->n_chiudere++] = fd;

        r->fd[quale] = fd;
        i++;                    /* il nome del file e' gia' stato consumato */
    }

    *argc = out;
    argv[out] = NULL;
    return 0;
}

/* Traduce le destinazioni di `r` nelle azioni che sys_spawn capisce, e ci
 * aggiunge l'ambiente. Un descrittore che punta a se stesso non produce
 * nessuna azione: il figlio lo eredita gia' cosi'. */
static void redir_in_extra(const Redir *r, SpawnExtra *extra)
{
    int i;

    extra->magia    = SPAWN_EXTRA_MAGIA;
    extra->envp     = env_vettore();
    extra->n_azioni = 0;

    for (i = 0; i < 3; i++) {
        SpawnAzione *az;

        if (r->fd[i] == i) continue;

        az = &extra->azioni[extra->n_azioni++];
        az->tipo        = SPAWN_AZ_FD;
        az->fd          = (uint32_t)i;
        az->flags       = 0;
        az->fd_padre    = r->fd[i];
        az->percorso[0] = '\0';
    }
}

/* =============================================================================
 * I CARATTERI JOLLY
 *
 * ! FINO AD AGOSTO 2026 LI ESPANDEVANO I COMANDI, NON LA SHELL, ed era una
 * scelta dichiarata: `delete *.tmp` se li espande da solo, come su MS-DOS, e
 * puo' dire quanti file ha selezionato prima di toccarli. Funziona finche' a
 * scrivere la riga c'e' una persona che sa quale comando sta usando.
 *
 * Smette di funzionare con `make`. Il bersaglio `clean` di qualunque
 * progetto — FreeBASIC compreso — e'
 *
 *     rm -f *.o libfb.a
 *
 * e `rm` non espande niente: e' il comando POSIX, e su POSIX l'espansione la
 * fa la shell. Senza, quella riga cercava un file chiamato davvero `*.o`, non
 * lo trovava, e `-f` la faceva TACERE. Cioe' `make clean` non puliva niente e
 * non lo diceva — e la compilazione successiva riusava oggetti vecchi.
 *
 * ! SI DISTINGUE MAIUSCOLO E MINUSCOLO, come ogni shell Unix. Su FAT i nomi
 * tornano in maiuscolo, quindi li' `*.o` non trova `MAIN.O`: e' scomodo ed e'
 * meno pericoloso dell'alternativa, perche' un confronto insensibile al caso
 * farebbe selezionare a `rm *` file che chi ha scritto il modello non aveva
 * in mente. `delete` resta insensibile al caso — quello lo lancia una
 * persona, che vede l'elenco prima della cancellazione.
 *
 * ! SE NON CORRISPONDE NIENTE, IL MODELLO RESTA COM'E'. E' cio' che fa una
 * shell POSIX, ed e' cio' su cui i makefile contano: `rm -f *.o` in una
 * directory gia' pulita deve essere silenzioso, non un errore.
 * ============================================================================= */

/* Confronto di `nome` con un modello che contiene ? e *.
 *
 * Il `*` puo' assorbire una sequenza di lunghezza qualsiasi, quindi serve
 * poter TORNARE INDIETRO: se il resto del modello non si aggancia, si
 * riprova facendo assorbire al `*` un carattere in piu'. Senza, `*.txt`
 * fallirebbe su `nota.txt` — il `*` si mangerebbe tutto. E' lo stesso
 * algoritmo di bin/delete/delete.c, senza la conversione di maiuscole. */
static int jolly_corrisponde(const char *mod, const char *nome)
{
    const char *stella = 0;
    const char *ripresa = 0;

    while (*nome) {
        if (*mod == '?' || *mod == *nome) { mod++; nome++; continue; }
        if (*mod == '*') { stella = mod++; ripresa = nome; continue; }
        if (stella) { mod = stella + 1; nome = ++ripresa; continue; }
        return 0;
    }
    while (*mod == '*') mod++;
    return *mod == '\0';
}

static int ha_jolly(const char *s)
{
    for (; *s; s++) if (*s == '*' || *s == '?') return 1;
    return 0;
}

/* =============================================================================
 * espandi_jolly — sostituisce i token con modello con i nomi che combaciano.
 *
 * I nomi trovati si copiano IN CODA allo stesso `scratch` in cui parse_line
 * ha messo i token: e' gia' dimensionato sulla riga di comando intera, e i
 * nomi che escono da un'espansione stanno nello stesso ordine di grandezza.
 *
 * Ritorna 0, oppure -1 se non c'e' piu' posto (e lo dice).
 * ============================================================================= */
static int espandi_jolly(int *argc, char **argv, int *oper, const char *quotato,
                         char *scratch, uint32_t uso, uint32_t dim)
{
    char *nuovo[MAX_ARGS + 1];
    int   nuovo_op[MAX_ARGS + 1];
    int   n = 0;
    int   i;

    for (i = 0; i < *argc; i++) {
        char        dir[PATH_MAX_SH];
        const char *modello;
        const char *t = argv[i];
        ShDir      *d;
        int         trovati = 0;
        int         primo   = n;
        uint32_t    k, barra;

        if (oper[i] != OP_NESSUNO || quotato[i] || !ha_jolly(t)) {
            if (n >= MAX_ARGS) goto troppi;
            nuovo[n] = argv[i]; nuovo_op[n] = oper[i]; n++;
            continue;
        }

        /* Si separa la parte di percorso dal modello: il jolly vale solo
         * nell'ULTIMO pezzo. Un modello in coda — "sotto" barra "asterisco
         * punto c" — guarda dentro `sotto`; un modello in MEZZO al percorso
         * non si espande e resta com'e' scritto. Espandere anche quello
         * vorrebbe dire percorrere l'albero, cioe' un'altra cosa con un
         * altro costo. */
        barra = 0;
        for (k = 0; t[k]; k++) if (t[k] == '/') barra = k + 1;

        if (barra == 0) {
            dir[0] = '.'; dir[1] = '\0';
        } else {
            uint32_t j;

            if (barra >= sizeof(dir)) { /* percorso assurdo: lascialo com'e' */
                if (n >= MAX_ARGS) goto troppi;
                nuovo[n] = argv[i]; nuovo_op[n] = oper[i]; n++;
                continue;
            }
            for (j = 0; j + 1 < barra; j++) dir[j] = t[j];
            /* `/x*` ha la barra in posizione 0: la directory e' la radice,
             * e togliendola resterebbe una stringa vuota. */
            if (j == 0) dir[j++] = '/';
            dir[j] = '\0';
        }
        modello = t + barra;

        d = sh_opendir(dir);
        if (d == 0) {
            if (n >= MAX_ARGS) goto troppi;
            nuovo[n] = argv[i]; nuovo_op[n] = oper[i]; n++;
            continue;
        }

        for (;;) {
            const char *nome = sh_readdir(d);
            uint32_t    l;

            if (nome == 0) break;

            /* I nomi che cominciano per punto si nascondono a un modello
             * che non comincia per punto: e' la regola di ogni shell, e
             * serve a non far mangiare `.` e `..` a un `rm *`. */
            if (nome[0] == '.' && modello[0] != '.') continue;
            if (!jolly_corrisponde(modello, nome)) continue;

            for (l = 0; nome[l]; l++) ;
            if (uso + barra + l + 1 > dim) { sh_closedir(d); goto pieno; }
            if (n >= MAX_ARGS)             { sh_closedir(d); goto troppi; }

            /* Il nome trovato si riattacca alla parte di percorso: chi ha
             * scritto un modello dentro `sotto` si aspetta `sotto/a.c`, non
             * `a.c` — che indicherebbe un altro file, nella directory
             * corrente. */
            {
                char    *dst = scratch + uso;
                uint32_t j;

                for (j = 0; j < barra; j++) dst[j] = t[j];
                for (l = 0; nome[l]; l++)   dst[barra + l] = nome[l];
                dst[barra + l] = '\0';
                uso += barra + l + 1;

                nuovo[n] = dst; nuovo_op[n] = OP_NESSUNO; n++;
                trovati++;
            }
        }
        sh_closedir(d);

        if (!trovati) {
            /* Nessuna corrispondenza: il modello passa com'e'. Vedi il !
             * in testa — e' cio' che rende innocuo `rm -f *.o` su una
             * directory gia' pulita. */
            if (n >= MAX_ARGS) goto troppi;
            nuovo[n] = argv[i]; nuovo_op[n] = oper[i]; n++;
            continue;
        }

        /* In ordine alfabetico, come ogni shell: l'ordine di readdir e'
         * quello del filesystem, cioe' arbitrario, e una riga di comando che
         * cambia ordine fra due esecuzioni rende diversi due `ar` che
         * dovrebbero produrre lo stesso archivio. */
        {
            int a, b;

            for (a = primo + 1; a < n; a++) {
                char *v = nuovo[a];

                for (b = a; b > primo && sh_strcmp(nuovo[b - 1], v) > 0; b--)
                    nuovo[b] = nuovo[b - 1];
                nuovo[b] = v;
            }
        }
    }

    for (i = 0; i < n; i++) { argv[i] = nuovo[i]; oper[i] = nuovo_op[i]; }
    argv[n] = NULL;
    *argc = n;
    return 0;

troppi:
    print(CLR_YELLOW);
    println("sh: i caratteri jolly danno troppi nomi per un comando solo");
    print(CLR_RESET);
    g_ultimo_stato = 2;
    return -1;

pieno:
    print(CLR_YELLOW);
    println("sh: i caratteri jolly danno una riga di comando troppo lunga");
    print(CLR_RESET);
    g_ultimo_stato = 2;
    return -1;
}

/* =============================================================================
 * esegui_comando — l'ultimo gradino: un built-in oppure un programma.
 *
 * ! LE DUE META' RIDIRIGONO IN DUE MODI DIVERSI, e non e' una duplicazione.
 * Un programma e' un altro processo: le sue redirezioni gliele monta il
 * kernel mentre nasce, e la shell non tocca i propri descrittori. Un
 * built-in gira DENTRO la shell: l'unico modo di ridirigerlo e' ridirigere
 * la shell stessa, e quindi rimetterla com'era subito dopo — perche' il
 * comando successivo, e il prompt, si aspettano di trovare la console.
 * ============================================================================= */
static int esegui_comando(int argc, char **argv, int background,
                          const char *cmdline, Redir *r, SpawnExtra *extra)
{
    int salvato[3];
    int i, ret;

    if (argc <= 0) return 0;

    if (!e_builtin(argv[0])) {
        run_program(argv[0], argc, argv, background, cmdline, extra, NULL);
        return 0;
    }

    for (i = 0; i < 3; i++) {
        salvato[i] = -1;
        if (r->fd[i] == i) continue;

        /* ! SI SALVA PRIMA DI SOVRASCRIVERE, e se il salvataggio non riesce
         * NON si ridirige. Una shell che ridirige il proprio stdout e poi non
         * sa piu' tornare alla console e' muta da li' in avanti: la perdita
         * dell'output di un comando e' meno grave della perdita del prompt. */
        salvato[i] = sh_dup(i);
        if (salvato[i] < 0) continue;
        sh_dup2(r->fd[i], i);
    }

    ret = esegui_builtin(argc, argv, background, cmdline);

    for (i = 0; i < 3; i++) {
        if (salvato[i] < 0) continue;
        sh_dup2(salvato[i], i);
        sh_close(salvato[i]);
    }

    return ret;
}

/* =============================================================================
 * LE PIPE
 *
 * ! UN BUILT-IN DENTRO UNA PIPE SI RILANCIA COME `/bin/sh -c`, e non e' un
 * ripiego pigro. Un built-in gira DENTRO la shell: la sua uscita va dove va
 * quella della shell, cioe' sulla console, non nella pipe. Le alternative
 * erano due, ed erano peggio entrambe:
 *
 *   - redirigere i descrittori della shell stessa con dup2 e rimetterli a
 *     posto dopo. Funziona per un comando semplice (ed e' cio' che si fa la',
 *     vedi esegui_comando), ma dentro una pipe no: gli anelli devono girare
 *     INSIEME, e un anello che gira dentro la shell blocca gli altri. Con
 *     `cat file | grep x` la shell riempirebbe la pipe e si fermerebbe ad
 *     aspettare un lettore che non ha ancora lanciato. Stallo perfetto, e
 *     visibile solo con file piu' grandi del buffer della pipe.
 *   - vietarli. Ma `echo` e `cat` sono built-in qui, e sono i due comandi che
 *     compaiono a sinistra di una pipe piu' spesso di ogni altro.
 *
 * Costa un processo in piu' per anello. E' il prezzo giusto: quella shell
 * figlia il built-in ce l'ha, e i suoi descrittori sono i suoi.
 * ============================================================================= */
#define MAX_ANELLI  8

static void esegui_pipeline(char *testo, int background)
{
    char *anello[MAX_ANELLI];
    int   n = 0;
    int   pid[MAX_ANELLI];
    int   i;
    int   fd_prec = -1;         /* estremita' di lettura dell'anello prima */

    /* --- Spezza su '|' (uno solo: '||' l'ha gia' preso esegui_lista) --- */
    {
        char *p    = testo;
        char  virg = 0;

        anello[n++] = p;
        for (; *p; p++) {
            if (virg) { if (*p == virg) virg = 0; continue; }
            if (*p == '"' || *p == '\'') { virg = *p; continue; }
            if (*p == '\\' && p[1]) { p++; continue; }
            if (*p != '|') continue;

            if (n >= MAX_ANELLI) {
                print(CLR_YELLOW);
                println("sh: troppi anelli in una pipe");
                print(CLR_RESET);
                g_ultimo_stato = 2;
                return;
            }
            *p = '\0';
            anello[n++] = p + 1;
        }
    }

    /* --- Un anello solo: nessuna pipe da creare ------------------------ */
    if (n == 1) {
        static char  scratch[MAX_CMDLINE];
        char        *argv[MAX_ARGS + 1];
        int          oper[MAX_ARGS + 1];
        char         quotato[MAX_ARGS + 1];
        uint32_t     uso = 0;
        int          argc;
        Redir        r;
        SpawnExtra   extra;

        argc = parse_line(testo, scratch, sizeof(scratch), argv, oper,
                          quotato, &uso, MAX_ARGS);
        if (argc <= 0) return;      /* vuota, o malformata: l'ha gia' detto */

        if (espandi_jolly(&argc, argv, oper, quotato,
                          scratch, uso, sizeof(scratch)) != 0) return;

        redir_init(&r);
        if (redir_estrai(&argc, argv, oper, &r) != 0) { redir_chiudi(&r); return; }
        if (argc <= 0) { redir_chiudi(&r); return; }

        redir_in_extra(&r, &extra);
        esegui_comando(argc, argv, background, testo, &r, &extra);
        redir_chiudi(&r);
        return;
    }

    /* --- Piu' anelli: una pipe fra ognuno e il successivo -------------- */
    for (i = 0; i < n; i++) pid[i] = -1;

    for (i = 0; i < n; i++) {
        static char  scratch[MAX_CMDLINE];
        char        *argv[MAX_ARGS + 1];
        int          oper[MAX_ARGS + 1];
        char         quotato[MAX_ARGS + 1];
        uint32_t     uso = 0;
        char        *sh_argv[4];
        int          argc;
        int          p[2];
        Redir        r;
        SpawnExtra   extra;

        /* ! `scratch` E' RIUSATO A OGNI GIRO, e puo' esserlo per la stessa
         * ragione per cui lo e' g_envv: sys_spawn COPIA argv nell'arena del
         * kernel prima di tornare. Quando il giro dopo ci riscrive sopra,
         * del contenuto precedente non ha piu' bisogno nessuno. */
        argc = parse_line(anello[i], scratch, sizeof(scratch),
                          argv, oper, quotato, &uso, MAX_ARGS);
        if (argc <= 0) break;

        if (espandi_jolly(&argc, argv, oper, quotato,
                          scratch, uso, sizeof(scratch)) != 0) break;

        redir_init(&r);
        if (redir_estrai(&argc, argv, oper, &r) != 0) { redir_chiudi(&r); break; }
        if (argc <= 0) { redir_chiudi(&r); break; }

        /* L'uscita dell'anello precedente e' il nostro ingresso, a meno che
         * una `<` esplicita non dica altro: cio' che e' scritto nella riga
         * vince su cio' che la pipe implica. */
        if (fd_prec >= 0) {
            if (r.fd[0] == 0) r.fd[0] = fd_prec;
        }

        /* Tranne l'ultimo, ognuno scrive nella pipe verso quello dopo. */
        if (i + 1 < n) {
            if (sh_pipe(p) != 0) {
                print(CLR_YELLOW);
                println("sh: non riesco a creare la pipe");
                print(CLR_RESET);
                g_ultimo_stato = 1;
                redir_chiudi(&r);
                break;
            }
            if (r.fd[1] == 1) r.fd[1] = p[1];
        } else {
            p[0] = p[1] = -1;
        }

        redir_in_extra(&r, &extra);

        if (e_builtin(argv[0])) {
            sh_argv[0] = (char *)"/bin/sh";
            sh_argv[1] = (char *)"-c";
            sh_argv[2] = anello[i];
            sh_argv[3] = NULL;
            run_program("/bin/sh", 3, sh_argv, 0, anello[i], &extra, &pid[i]);
        } else {
            run_program(argv[0], argc, argv, 0, anello[i], &extra, &pid[i]);
        }

        /* ! LE ESTREMITA' SI CHIUDONO SUBITO, sia se lo spawn e' riuscito
         * sia se no. Il figlio ha gia' la sua copia (sys_spawn ha preso un
         * riferimento in piu' quando ha applicato l'azione); quella che
         * resta qui e' solo uno scrittore in piu' che la pipe conta, e
         * finche' c'e', chi legge non vede mai la fine dei dati. */
        redir_chiudi(&r);
        if (fd_prec >= 0) { sh_close(fd_prec); fd_prec = -1; }
        if (p[1] >= 0)    { sh_close(p[1]); }
        fd_prec = p[0];

        if (pid[i] < 0) break;
    }

    if (fd_prec >= 0) sh_close(fd_prec);

    /* ! SI ASPETTANO TUTTI, e lo stato e' quello dell'ULTIMO. Aspettarne uno
     * solo lascerebbe gli altri zombie; prendere lo stato del primo direbbe
     * che `prog-che-fallisce | grep x` e' fallito, mentre ogni shell — e ogni
     * makefile che ci conta sopra — legge l'esito dell'ultimo anello. */
    for (i = 0; i < n; i++) {
        int32_t stato;

        if (pid[i] < 0) continue;
        if (i == n - 1) sh_setfg(pid[i]);
        if (sh_waitpid(pid[i], &stato, 0) >= 0 && i == n - 1)
            g_ultimo_stato = (int)stato;
    }
    sh_setfg(sh_getpid());
}

/* =============================================================================
 * LE SEQUENZE — `;`, `&&`, `||`
 *
 * E' il livello piu' esterno, e quello per cui tutto il resto e' stato
 * scritto: la prima ricetta che GNU make consegna a `sh -c` costruendo la
 * runtime di FreeBASIC e'
 *
 *     rm -f lib/exos-x86/libfb.a; ar rcs lib/exos-x86/libfb.a <200 oggetti>
 *
 * ! IL `&` FINALE SI GUARDA QUI, per segmento e non per riga. Prima stava
 * nel gradino piu' esterno, che era la riga intera; adesso la riga puo'
 * contenerne piu' di uno, e `a; b &` deve mettere in background `b` e non
 * anche `a`.
 * ============================================================================= */
#define SEP_FINE   0
#define SEP_SEQ    1    /* ;  */
#define SEP_E      2    /* && */
#define SEP_O      3    /* || */

static char *trova_separatore(char *s, int *tipo, int *salto)
{
    char virg = 0;

    for (; *s; s++) {
        if (virg) { if (*s == virg) virg = 0; continue; }
        if (*s == '"' || *s == '\'') { virg = *s; continue; }
        if (*s == '\\' && s[1]) { s++; continue; }

        /* ! SI GUARDA IL CARATTERE DOPO. `&&` e `&` sono due cose diverse
         * (la seconda e' il background) e cosi' `||` e `|` (la seconda e' la
         * pipe): confonderli qui vorrebbe dire mangiare un operatore che
         * spetta a un altro strato. */
        if (*s == ';')                { *tipo = SEP_SEQ; *salto = 1; return s; }
        if (*s == '&' && s[1] == '&') { *tipo = SEP_E;   *salto = 2; return s; }
        if (*s == '|' && s[1] == '|') { *tipo = SEP_O;   *salto = 2; return s; }
    }

    *tipo = SEP_FINE;
    *salto = 0;
    return NULL;
}

static void esegui_lista(char *line)
{
    char *p        = line;
    int   prec_tipo = SEP_SEQ;      /* il primo segmento si esegue sempre */

    while (*p) {
        char *sep, *fine;
        int   tipo, salto;
        int   background = 0;
        int   esegui;

        sep = trova_separatore(p, &tipo, &salto);
        if (sep) { *sep = '\0'; fine = sep + salto; } else { fine = NULL; }

        /* `&` in coda al segmento: background. Si accetta sia «comando &»
         * sia «comando&», come prima. */
        {
            char *e = p;

            while (*e) e++;
            while (e > p && (e[-1] == ' ' || e[-1] == '\t')) e--;
            if (e > p && e[-1] == '&') {
                background = 1;
                e--;
                while (e > p && (e[-1] == ' ' || e[-1] == '\t')) e--;
            }
            *e = '\0';
        }

        /* Il segmento si esegue in base a com'era il separatore PRIMA di lui
         * e a com'e' andato il comando precedente. */
        esegui = (prec_tipo == SEP_SEQ) ||
                 (prec_tipo == SEP_E && g_ultimo_stato == 0) ||
                 (prec_tipo == SEP_O && g_ultimo_stato != 0);

        if (esegui) {
            /* Salta gli spazi in testa: un segmento fatto di soli spazi —
             * `a ; ; b`, oppure la coda di una riga che finisce con `;` —
             * non e' un errore, e' semplicemente niente da fare. */
            while (*p == ' ' || *p == '\t') p++;
            if (*p) esegui_pipeline(p, background);
        }

        if (!sep) break;
        prec_tipo = tipo;
        p = fine;
    }
}

/* Esegue UNA riga com'e' stata digitata (o com'e' arrivata da `sh -c`).
 * Ritorna 0 normalmente, -1 se il comando era `exit` — che pero' non torna
 * mai da qui: sh_exit() termina il processo. */
static int esegui_riga(char *line, int n)
{
    if (n <= 0) return 0;
    esegui_lista(line);
    return 0;
}


/* =============================================================================
 * CRONOLOGIA E MODIFICA DELLA RIGA
 *
 * Le frecce non arrivano in modalita' cooked: li' il driver di tastiera
 * assembla la riga e la consegna su Invio, e i tasti di movimento non
 * hanno modo di attraversare un flusso di testo. Per averli bisogna
 * passare in RAW e prendersi la disciplina di riga — eco, backspace,
 * cursore — che prima faceva il driver.
 *
 * ! SE LA MODALITA' RAW NON E' DISPONIBILE SI TORNA A LEGGERE RIGHE.
 * Il servizio 'kbd' potrebbe non essere avviato (kernel.cfg senza la voce
 * [modules], o driver morto). Una shell che in quel caso non accetta piu'
 * comandi sarebbe un sistema inutilizzabile per una funzione di comodo:
 * si ripiega su sh_read() e si perde solo la cronologia.
 *
 * ! IL DRIVER TORNA IN COOKED DA SOLO quando qualcuno chiede una riga
 * (vedi drivers/kbd/kbd_proto.h): succede ogni volta che un programma
 * lanciato da qui legge da stdin. Percio' la modalita' si riafferma a
 * OGNI prompt invece di impostarla una volta all'avvio — ed e' anche cio'
 * che rende la cosa autoriparante se un job in background la cambia.
 *
 * ! IL RIDISEGNO USA SOLO BACKSPACE, non '\r' e nessuna sequenza di
 * controllo. Il TTY di EX-OS non ha un linguaggio di posizionamento del
 * cursore: l'unica cosa su cui si puo' contare e' che un backspace
 * indietreggi di uno. Conseguenza dichiarata: se la riga supera la
 * larghezza dello schermo e va a capo, il ridisegno non torna indietro
 * oltre il capo e la modifica si vede male. La riga resta corretta —
 * quello che si legge e' cio' che si eseguira'.
 * ============================================================================= */
#include "kbd_proto.h"

#define CRONOLOGIA_N   24

static char g_storia[CRONOLOGIA_N][MAX_LINE];
static int  g_storia_n = 0;     /* quante righe valide */
static int  g_storia_p = 0;     /* prossima posizione di scrittura */

static int  g_kbd_pid = -1;     /* -1 = non ancora cercato, 0 = assente */

/* Aggiunge una riga alla cronologia.
 *
 * ! NON SI REGISTRANO LE RIGHE VUOTE NE' I DOPPIONI CONSECUTIVI: chi
 * ripete lo stesso comando dieci volte non vuole dieci voci da
 * riattraversare con la freccia. */
static void storia_aggiungi(const char *riga)
{
    int prec;

    if (riga[0] == '\0') return;

    prec = (g_storia_p - 1 + CRONOLOGIA_N) % CRONOLOGIA_N;
    if (g_storia_n > 0 && sh_strcmp(g_storia[prec], riga) == 0) return;

    sh_strcpy(g_storia[g_storia_p], riga, MAX_LINE);
    g_storia_p = (g_storia_p + 1) % CRONOLOGIA_N;
    if (g_storia_n < CRONOLOGIA_N) g_storia_n++;
}

/* La riga a `indietro` passi dal fondo (1 = l'ultima). NULL se non c'e'. */
static const char *storia_leggi(int indietro)
{
    int i;

    if (indietro < 1 || indietro > g_storia_n) return 0;
    i = (g_storia_p - indietro + CRONOLOGIA_N * 2) % CRONOLOGIA_N;
    return g_storia[i];
}

static int kbd_trova(void)
{
    if (g_kbd_pid < 0) {
        int p = sh_ipc_lookup(KBD_SERVICE_NAME);
        g_kbd_pid = (p > 0) ? p : 0;
    }
    return g_kbd_pid;
}

static void kbd_modo(unsigned int modo)
{
    KbdSetMode  m;
    ConsoleInfo ci;

    if (kbd_trova() <= 0) return;

    m.modo    = modo;
    m.console = (sh_console_info(&ci) == 0) ? ci.mia : 0;
    sh_ipc_send(g_kbd_pid, KBD_MSG_SETMODE, &m, sizeof(m));
}

/* Il prossimo tasto, o 0 se non arriva (driver tornato in cooked, o
 * servizio sparito). */
static unsigned int kbd_tasto(void)
{
    ShIpcMsg     meta;
    unsigned int payload = 0;
    unsigned int console = 0;
    ConsoleInfo  ci;
    int          i;

    if (kbd_trova() <= 0) return 0;
    if (sh_console_info(&ci) == 0) console = ci.mia;

    /* =====================================================================
     * ! SI RIPROVA ALL'INFINITO, RIAFFERMANDO OGNI VOLTA LA MODALITA'.
     *
     * La prima stesura aspettava due secondi e poi rinunciava. Sembrava
     * prudente ed era sbagliato per il motivo piu' semplice: una persona
     * che si ferma tre secondi a pensare perdeva la modalita' raw, e la
     * riga successiva tornava senza cronologia e con le frecce che
     * arrivavano come sequenze di escape stampate alla lettera.
     *
     * La scadenza serve, ma non per rinunciare: serve per RIAFFERMARE la
     * modalita'. In cooked il driver ignora le READKEY in silenzio, e
     * senza questo giro la shell resterebbe ferma per sempre. Cosi'
     * invece si ripara da sola ogni due secondi — che e' esattamente
     * quello che occorre quando un programma appena terminato ha
     * riportato il driver in cooked.
     *
     * Si rinuncia solo se il servizio non accetta piu' messaggi, cioe' se
     * e' morto davvero.
     * ===================================================================== */
    for (;;) {
        if (sh_ipc_send(g_kbd_pid, KBD_MSG_READKEY, &console, sizeof(console)) < 0)
            return 0;

        for (i = 0; i < 4; i++) {
            if (sh_ipc_recv(&meta, &payload, sizeof(payload), 2000) < 0) break;
            if ((int)meta.sender_pid != g_kbd_pid) continue;
            if (meta.tipo != KBD_MSG_KEY) continue;
            return payload;
        }

        kbd_modo(KBD_MODE_RAW);
    }
}

/* Ridisegna la riga: torna all'inizio con dei backspace, riscrive, e
 * cancella la coda di quella vecchia se si e' accorciata. */
static void riga_ridisegna(const char *buf, int len, int cur,
                           int vecchia_len, int vecchia_cur)
{
    int i;

    for (i = 0; i < vecchia_cur; i++) sh_write(STDOUT, "\b", 1);
    if (len > 0) sh_write(STDOUT, buf, (uint32_t)len);
    for (i = len; i < vecchia_len; i++) sh_write(STDOUT, " ", 1);
    for (i = (len > vecchia_len ? len : vecchia_len); i > cur; i--)
        sh_write(STDOUT, "\b", 1);
}

/* Legge una riga con cronologia e modifica. Ritorna la lunghezza, o -1
 * se la modalita' raw non e' utilizzabile (il chiamante ripiega). */
/* Il nostro stdin e' la console, o qualcos'altro?
 *
 * ! LA DOMANDA E' ioctl, NON UNA SYSCALL NUOVA: TIOCGWINSZ riesce solo su un
 * terminale e rende ENOTTY su tutto il resto — pipe comprese. E' la stessa
 * prova che fa isatty() nella libc, rifatta qui perche' questa shell chiama
 * le syscall dirette e non si porta dietro la libc. */
static int stdin_e_console(void)
{
    unsigned short ws[4];

    return syscall3(SYS_IOCTL, (uint32_t)STDIN, 0x5413 /* TIOCGWINSZ */,
                    (uint32_t)ws) == 0;
}

static int riga_modifica(char *buf, int max)
{
    int len = 0, cur = 0, sfoglia = 0;
    char salvata[MAX_LINE];

    salvata[0] = '\0';
    buf[0] = '\0';

    /* ! SE stdin NON E' LA CONSOLE, QUESTA STRADA E' SBAGLIATA — e per un
     * anno non c'e' stato modo di accorgersene, perche' finora lo era sempre.
     *
     * Qui i tasti si prendono dal servizio 'kbd' via IPC, per avere editing di
     * riga e cronologia: il descrittore 0 non viene guardato mai. Giusto su
     * una console; sbagliato appena qualcuno ci mette una PIPE — una shell in
     * una finestra di terminale — perche' allora l'uscita va dove le si dice
     * (il prompt arriva) e l'ingresso continua ad arrivare dalla tastiera. Il
     * comando scritto nella pipe non lo legge nessuno, e la shell sembra
     * bloccata mentre e' viva e sta aspettando altrove.
     *
     * Rendendo -1 si ripiega su sh_read(STDIN), qui sotto: si perdono
     * cronologia e frecce — che dietro una pipe non esistono comunque — e non
     * si perde la shell. */
    if (!stdin_e_console()) return -1;

    /* ! SOLO LA CONSOLE IN PRIMO PIANO PRENDE I TASTI. Senza questo
     * controllo tutte e quattro le shell chiedevano la modalita' raw e si
     * contendevano la tastiera: le tre non visibili scadevano, ripiegavano
     * su una lettura di riga, e quella riportava il driver in cooked —
     * togliendola proprio a chi stava scrivendo. Il registro si riempiva
     * di "READLINE su console N in raw, ripristino cooked" a ogni tasto.
     *
     * Chi non e' visibile si mette semplicemente in attesa di una riga,
     * come ha sempre fatto: nessuno gli sta scrivendo. */
    {
        ConsoleInfo ci;

        if (sh_console_info(&ci) != 0 || ci.mia != ci.visibile) return -1;
    }

    kbd_modo(KBD_MODE_RAW);
    if (g_kbd_pid <= 0) return -1;

    for (;;) {
        unsigned int ev = kbd_tasto();
        unsigned int k  = ev & KBD_KEY_MASK;
        int vl = len, vc = cur;

        if (ev == 0) return -1;         /* il driver non risponde: si ripiega */

        if (k == '\n' || k == '\r') {
            sh_write(STDOUT, "\n", 1);
            buf[len] = '\0';
            return len;
        }

        if (k == '\b' || k == 127u) {
            if (cur > 0) {
                int i;
                for (i = cur - 1; i < len - 1; i++) buf[i] = buf[i+1];
                len--; cur--;
                riga_ridisegna(buf, len, cur, vl, vc);
            }
            continue;
        }

        if (k == KBD_K_DEL) {
            if (cur < len) {
                int i;
                for (i = cur; i < len - 1; i++) buf[i] = buf[i+1];
                len--;
                riga_ridisegna(buf, len, cur, vl, vc);
            }
            continue;
        }

        if (k == KBD_K_LEFT)  { if (cur > 0)   { cur--; sh_write(STDOUT, "\b", 1); } continue; }
        if (k == KBD_K_RIGHT) { if (cur < len) { sh_write(STDOUT, buf + cur, 1); cur++; } continue; }
        if (k == KBD_K_HOME)  { riga_ridisegna(buf, len, 0, vl, vc);   cur = 0;   continue; }
        if (k == KBD_K_END)   { riga_ridisegna(buf, len, len, vl, vc); cur = len; continue; }

        if (k == KBD_K_UP || k == KBD_K_DOWN) {
            const char *s;
            int nuovo = (k == KBD_K_UP) ? sfoglia + 1 : sfoglia - 1;

            if (nuovo > g_storia_n) continue;   /* piu' indietro non si va */
            if (nuovo < 0) continue;

            /* ! LA RIGA IN CORSO SI SALVA alla prima freccia in su e si
             * rimette scendendo fino in fondo: chi ha scritto meta'
             * comando e va a cercarne uno vecchio non deve perderlo. */
            if (sfoglia == 0 && nuovo > 0) {
                buf[len] = '\0';
                sh_strcpy(salvata, buf, sizeof(salvata));
            }

            s = (nuovo == 0) ? salvata : storia_leggi(nuovo);
            if (s == 0) continue;

            sh_strcpy(buf, s, (uint32_t)max);
            len = 0; while (buf[len]) len++;
            cur = len;
            sfoglia = nuovo;
            riga_ridisegna(buf, len, cur, vl, vc);
            continue;
        }

        /* Ctrl+C: si abbandona la riga e se ne comincia una nuova. */
        if ((ev & KBD_MOD_CTRL) && (k == 'c' || k == 'C')) {
            sh_write(STDOUT, "^C\n", 3);
            buf[0] = '\0';
            return 0;
        }

        /* =================================================================
         * Carattere stampabile: si inserisce dove sta il cursore.
         *
         * ! ANCHE SOPRA 127, e senza questo le disposizioni non inglesi
         * non servono a niente. Questo filtro diceva `k < 127`, cioe'
         * «solo ASCII», ed era invisibile finche' la tastiera e' stata
         * americana. Con la disposizione italiana la `ò` (0x95 nella code
         * page 437) arrivava dal driver e la shell la buttava via: il
         * tasto sembrava rotto, e il difetto stava tre livelli piu' su di
         * dove lo si cercava.
         *
         * 0x7F resta fuori: e' DEL, e un glifo sensato non ce l'ha.
         * ================================================================= */
        if (k >= 32u && k != 127u && k < 256u && len < max - 1) {
            int i;
            int in_coda = (cur == len);

            for (i = len; i > cur; i--) buf[i] = buf[i-1];
            buf[cur] = (char)k;
            len++; cur++;

            /* ! IN CODA SI SCRIVE UN CARATTERE SOLO. Ridisegnare tutta la
             * riga a ogni tasto funziona ma fa lampeggiare lo schermo e
             * su una console lenta si vede: il caso normale — si scrive
             * in fondo — non ha niente da ridisegnare. */
            if (in_coda) sh_write(STDOUT, buf + cur - 1, 1);
            else         riga_ridisegna(buf, len, cur, vl, vc);
        }
    }
}

/* =============================================================================
 * AUTOEXEC — comandi eseguiti all'avvio
 *
 * Il file predefinito e' /boot/autoexec.sh; il nome si cambia con la
 * chiave `autoexec` di /boot/kernel.cfg, e `autoexec=0` lo disattiva.
 *
 * Formato: una riga = un comando, come se fosse digitato. Le righe vuote
 * e quelle che cominciano con '#' si saltano. Una riga che comincia con
 * '@' viene eseguita SENZA essere stampata, come nell'autoexec del DOS.
 *
 * -----------------------------------------------------------------------------
 * !silenced — l'`echo off` di autoexec.bat
 *
 *     !silenced      da qui in poi i comandi non si vedono piu'
 *     !verbose       si tornano a vedere
 *
 * ! ZITTISCE IL COMANDO, NON IL SUO RISULTATO. E' la distinzione che
 * rende l'opzione utile: quello che un comando stampa e' il motivo per
 * cui lo si e' messo nello script, mentre la riga di comando la si e'
 * gia' scritta e riletterla non aggiunge niente. Uno script che accende
 * la rete deve far vedere l'indirizzo ottenuto, non "dhcp".
 *
 * ! VALE DA DOVE STA IN POI, non per tutto il file. Cosi' si puo'
 * zittire la parte rumorosa e lasciar vedere quella che interessa,
 * invece di dover scegliere una volta sola per l'intero script. La riga
 * della direttiva non si stampa mai, in nessuno dei due modi.
 *
 * ! E' DIVERSO DA '@', e i due convivono: '@' zittisce UNA riga,
 * `!silenced` cambia lo stato. Chi ha un solo comando da nascondere non
 * deve ricordarsi di riaccendere.
 *
 * -----------------------------------------------------------------------------
 * ! SOLO LA PRIMA CONSOLE
 *
 * EX-OS avvia una shell per ognuna delle quattro console virtuali. Senza
 * questo controllo l'autoexec girerebbe QUATTRO VOLTE — e per comandi
 * come `/dev/pci.drv &` significherebbe quattro processi che si
 * contendono lo stesso servizio, con tre che falliscono e un registro
 * pieno di errori a ogni avvio.
 *
 * -----------------------------------------------------------------------------
 * ! LA VIA D'USCITA DEVE ESISTERE PRIMA DI SERVIRE
 *
 * Un autoexec con dentro un comando che si blocca renderebbe il sistema
 * inutilizzabile, e il file per correggerlo sta sul disco che non si
 * riesce piu' a raggiungere. Percio':
 *
 *   - `autoexec=0` in kernel.cfg lo salta, e kernel.cfg si puo'
 *     modificare da un'altra macchina montando il supporto;
 *   - le altre tre console NON lo eseguono, quindi Alt+F2 da' sempre una
 *     shell pulita anche mentre la prima e' impegnata.
 *
 * Il secondo punto e' quello che conta davvero: non richiede di poter
 * modificare niente.
 *
 * ! NON C'E' RICORSIONE. Un autoexec che lancia `sh` non rilancia
 * l'autoexec, perche' la seconda shell non e' sulla console 0 — ma se
 * qualcuno cambiasse quel controllo, un autoexec che lancia sh sarebbe un
 * ciclo infinito di processi. E' il motivo per cui la condizione e' "la
 * console" e non "sono la prima shell".
 * ============================================================================= */
#define AUTOEXEC_PREDEFINITO  "/boot/autoexec.sh"

/* ! UNO SCRIPT PUO' LANCIARNE UN ALTRO, MA NON ALL'INFINITO. Due file
 * che si chiamano a vicenda sono un ciclo che riempie lo stack e ferma la
 * shell senza dire perche'. Quattro livelli bastano a qualunque uso
 * ragionevole e rendono il ciclo un messaggio invece di un blocco. */
#define SCRIPT_ANNIDAMENTO_MAX  4

static int g_script_livello = 0;

/* Esegue un file di comandi. `etichetta` e' il prefisso mostrato davanti
 * a ogni riga eseguita; NULL usa il nome del file.
 * Ritorna 0, oppure -1 se lo script ha chiesto `exit`. */
static int esegui_script(const char *nome, const char *etichetta)
{
    char        buf[2048];
    char        riga[MAX_LINE];
    int         fd, letti, i, r = 0;
    int         zitto_sempre = 0;

    if (g_script_livello >= SCRIPT_ANNIDAMENTO_MAX) {
        printerr("sh: script annidati troppo in profondita'");
        return 0;
    }

    fd = syscall3(SYS_OPEN, (uint32_t)nome, 0, 0);
    if (fd < 0) return 0;           /* non c'e': non e' un errore */

    letti = (int)sh_read(fd, buf, sizeof(buf) - 1);
    syscall1(SYS_CLOSE, (uint32_t)fd);
    if (letti <= 0) return 0;
    buf[letti] = '\0';

    if (etichetta == 0) etichetta = nome;
    g_script_livello++;

    /* ! SI LEGGE TUTTO IN UNA VOLTA, e il limite e' dichiarato: 2 KB.
     * Un autoexec e' un elenco di comandi, non un programma; leggerlo a
     * pezzi vorrebbe dire gestire una riga spezzata a meta' fra due
     * letture, che e' complicazione per un caso che non si presenta. Un
     * file piu' lungo viene TRONCATO, e lo si dice. */
    if (letti == (int)sizeof(buf) - 1)
        println("sh: script piu' lungo di 2 KB, il resto e' stato ignorato");

    i = 0;
    while (i < letti) {
        int n = 0, zitto = 0;

        while (i < letti && buf[i] != '\n' && n < MAX_LINE - 1) riga[n++] = buf[i++];
        while (i < letti && buf[i] != '\n') i++;    /* riga troppo lunga: si scarta la coda */
        if (i < letti) i++;                        /* salta il newline */

        while (n > 0 && (riga[n-1] == '\r' || riga[n-1] == ' ' ||
                         riga[n-1] == '\t')) n--;
        riga[n] = '\0';

        {
            int s = 0;
            while (s < n && (riga[s] == ' ' || riga[s] == '\t')) s++;
            if (s > 0) { int k; for (k = 0; k <= n - s; k++) riga[k] = riga[k+s]; n -= s; }
        }

        if (n == 0 || riga[0] == '#') continue;

        /* Direttive: cambiano lo stato e non sono comandi. Non si
         * stampano mai — stampare "!silenced" sarebbe l'unica riga
         * visibile di uno script che si e' appena zittito. */
        if (riga[0] == '!') {
            if (sh_strcmp(riga, "!silenced") == 0) { zitto_sempre = 1; continue; }
            if (sh_strcmp(riga, "!verbose")  == 0) { zitto_sempre = 0; continue; }
            printerr("sh: direttiva sconosciuta (valgono !silenced e !verbose)");
            continue;
        }

        if (riga[0] == '@') {
            zitto = 1;
            { int k; for (k = 0; k < n; k++) riga[k] = riga[k+1]; n--; }
            if (n == 0) continue;
        }

        if (!zitto && !zitto_sempre) {
            print(CLR_CYAN);
            print(etichetta);
            print("> ");
            print(CLR_RESET);
            println(riga);
        }

        r = esegui_riga(riga, n);
        if (r < 0) break;           /* `exit` dentro lo script */
    }

    g_script_livello--;
    return (r < 0) ? -1 : 0;
}

static void esegui_autoexec(void)
{
    char        nome[128];
    ConsoleInfo ci;

    /* Solo la console 0: vedi il commento qui sopra. */
    if (sh_console_info(&ci) != 0 || ci.mia != 0) return;

    /* ! E MAI DENTRO UNO PSEUDO-TERMINALE, che e' la console 0 di riflesso: una
     * shell in un terminale in finestra o in una sessione telnet eredita la
     * console di chi l'ha lanciata, e senza questo controllo rieseguirebbe
     * l'avvio del sistema — riaccendendo driver gia' accesi. Si e' visto in
     * tutt'e due i posti: nella finestra e via rete arrivava «Avvio
     * automatico: accendo la rete...» seguito da una fila di ipc_register
     * fallite con -17.
     *
     * ! IL CRITERIO E' «HO UN pty», NON «SONO REMOTA»: l'autoexec e' l'avvio
     * DEL SISTEMA, e il sistema si avvia su una console vera. Dentro un pty c'e'
     * sempre qualcun altro che ha gia' fatto quel lavoro. */
    if (syscall3(SYS_PTY_CTL, 0, PTY_CTL_LEGGI_MISURA, 0) >= 0) return;

    if (sh_getenv_kernel("autoexec", nome, sizeof(nome)) < 0) {
        sh_strcpy(nome, AUTOEXEC_PREDEFINITO, sizeof(nome));
    } else if (nome[0] == '0' && nome[1] == '\0') {
        return;                     /* disattivato di proposito */
    }

    esegui_script(nome, "autoexec");
}

/* =============================================================================
 * shell_main — la shell, con i suoi argomenti
 *
 * La chiama _start in bin/sh/start.S, che è ciò che l'ELF loader salta.
 *
 * ! FINO AD AGOSTO 2026 QUESTA ERA `void _start(void)` E NON VEDEVA argv.
 * Sembrava una mancanza innocua — la shell la lancia il kernel, senza
 * argomenti — ed era invece il tappo di una fila intera: senza `sh -c`
 * non si possono scrivere system() e popen() nella libc, e senza QUELLE
 * non gira la runtime di FreeBASIC. Il perché del file assembly sta in
 * testa a start.S.
 *
 * Tre modi d'uso:
 *
 *     sh                    interattiva: banner, autoexec, prompt
 *     sh -c "comando"       esegue una riga ed esce con il SUO codice
 *     sh script.sh          esegue lo script ed esce
 *
 * ! `-c` NON stampa il banner e NON esegue l'autoexec, e non è un
 * dettaglio estetico: chi la chiama è un programma che legge l'output del
 * comando, e il banner glielo sporcherebbe.
 * ============================================================================= */
int shell_main(int argc, char **argv, char **envp)
{
    char line[MAX_LINE];
    int  n;

    /* Inizializza variabili d'ambiente: prima i valori di sistema, poi
     * sopra quelli che il padre ci ha passato. Vedi env_eredita. */
    env_init();
    env_eredita(envp);

    if (argc >= 2 && sh_strcmp(argv[1], "-c") == 0) {
        /* ! NON E' `line`, ED E' TRENTADUE VOLTE PIU' GRANDE. La riga di
         * `-c` non la scrive una persona: la scrive `make`, e la ricetta che
         * collega il compilatore FreeBASIC — centoquarantacinque percorsi di
         * oggetti — sono seimila caratteri. In `line` (512) ci sarebbe
         * entrata TAGLIATA, e una riga di comando tagliata resta quasi sempre
         * un comando valido: si sarebbe collegato meta' programma, e a
         * lamentarsi sarebbe stato `ld` parlando di simboli. Statico e non
         * sullo stack perche' sono 32 KB e questa funzione non torna mai. */
        static char riga_c[MAX_CMDLINE];

        if (argc < 3) {
            printerr("sh: -c vuole un comando");
            sh_exit(2);
        }
        /* ! SI COPIA: esegui_riga() spezza la riga piantandoci dentro dei
         * terminatori, e argv[2] sta nello stack costruito da sys_spawn —
         * non è roba nostra da modificare. */
        sh_strcpy(riga_c, argv[2], MAX_CMDLINE);
        n = 0;
        while (riga_c[n] != '\0' && n < MAX_CMDLINE - 1) n++;

        /* ! SI CONTROLLA CHE CI SIA ENTRATA TUTTA. Un comando accorciato di
         * nascosto e' il difetto peggiore che questo punto possa produrre —
         * e' la stessa ragione per cui sys_spawn rende E2BIG invece di
         * troncare argv. */
        if (n >= MAX_CMDLINE - 1 && argv[2][n] != '\0') {
            printerr("sh: -c: comando piu' lungo del massimo, non eseguito");
            sh_exit(2);
        }

        /* La tastiera è già nostra: senza, un comando interattivo lanciato
         * da qui non riuscirebbe a leggere una riga. */
        sh_setfg(sh_getpid());
        esegui_riga(riga_c, n);
        sh_exit(g_ultimo_stato);
    }

    /* Un argomento che non è un'opzione è il nome di uno script. */
    if (argc >= 2 && argv[1][0] != '-') {
        sh_setfg(sh_getpid());
        esegui_script(argv[1], NULL);
        sh_exit(g_ultimo_stato);
    }

    /* Da subito la tastiera e' della shell: finche' nessuno dichiara il
     * primo piano, sys_read lascia leggere chiunque — e il primo job
     * lanciato con '&' potrebbe rubarle l'input. */
    sh_setfg(sh_getpid());

    /* Deve venire dopo env_init: entrambe passano da SYS_GETENV, ma
     * verboseboot non è una variabile d'ambiente e non finisce in g_env. */
    verbose_init();

    /* Banner, solo se richiesto dalla configurazione */
    if (g_verbose_boot) print_banner();

    /* I comandi di /boot/autoexec.sh, se c'e'. Dopo il banner, cosi' quel
     * che stampano si legge sotto e non in mezzo. */
    esegui_autoexec();

    /* Loop principale della shell */
    for (;;) {
        /* Job finiti nel frattempo: annunciati qui, prima del prompt,
         * come fa qualunque shell. E' anche il momento in cui il loro
         * slot di processo viene liberato. */
        job_raccogli();

        /* Prompt */
        print_prompt();

        /* ! PRIMA LA MODIFICA CON CRONOLOGIA, POI IL RIPIEGO. Se il
         * servizio 'kbd' non risponde riga_modifica() ritorna -1 e si
         * legge come si e' sempre fatto: si perde la cronologia, non la
         * shell. */
        n = riga_modifica(line, MAX_LINE);
        if (n < 0) {
            n = sh_read(STDIN, line, MAX_LINE - 1);
            if (n <= 0) {
                sh_yield();
                continue;
            }
        }
        line[n] = '\0';

        /* Rimuovi newline finale */
        if (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) {
            line[n-1] = '\0';
            n--;
        }

        if (n == 0) continue;   /* Riga vuota */

        /* Nella cronologia va la riga COME DIGITATA, prima che
         * esegui_riga() la spezzi piantandoci dentro i terminatori. */
        storia_aggiungi(line);

        if (esegui_riga(line, n) < 0) break;
    }

    sh_exit(0);
    return 0;   /* non raggiunto: sh_exit non ritorna */
}

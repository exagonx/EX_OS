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
 *   uname     — informazioni sistema
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
#define SYS_READ        3
#define SYS_WRITE       4
#define SYS_OPEN        5
#define SYS_CLOSE       6
#define SYS_CONSOLE_INFO 231
#define SYS_IPC_SEND     220
#define SYS_IPC_RECV_TMO 228
#define SYS_IPC_LOOKUP   223
#define SYS_WAITPID     7
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

/* Opzioni di waitpid — identiche a kernel/include/syscall.h */
#define WNOHANG         0x0001

/* I nomi dei servizi, per `helpconfig`: si chiede al registro IPC chi c'e'
 * gia'. Sono header di soli #define e struct, senza dipendenze — il nome
 * si prende da li' e non si ricopia, cosi' rinominare un servizio non
 * lascia indietro una stringa in questo file. */
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

/* ⚠️ DEVE RESTARE IDENTICA a ConsoleInfo in lib/include/libc.h e in
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

static void sh_exit(int code)
{
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

/* sh_spawn — crea un processo figlio autonomo che esegue `path`.
 * Passa argc/argv al processo figlio (disponibili via main(argc,argv)
 * nei programmi che usano la libc). Ritorna il PID del figlio (>0)
 * o un errno negativo. */
static int sh_spawn(const char *path, int argc, char **argv)
{
    return syscall3(SYS_SPAWN, (uint32_t)path, (uint32_t)argc, (uint32_t)argv);
}

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
static void sh_setfg(int pid)
{
    syscall1(SYS_CONSOLE_SETFG, (uint32_t)pid);
}

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
    /* ⚠️ /cdrom/bin C'E' APPOSTA: e' dove stanno gli strumenti di
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
 * ⚠️ SERVE A `sh -c`, che deve RIPORTARLO al proprio padre: senza, la
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
 * ⚠️ APICI SINGOLI E DOPPI FANNO LA STESSA COSA, e non e' una
 * semplificazione affrettata. Su una shell Unix la differenza esiste
 * perche' fra virgolette doppie $VAR viene espansa e fra apici singoli no.
 * Qui NON c'e' nessuna espansione — ne' di variabili ne' di caratteri
 * jolly — quindi le due forme non avrebbero niente da distinguere.
 * Accettarle entrambe e trattarle uguale e' onesto; accettarne una sola
 * costringerebbe a ricordare quale.
 *
 * ⚠️ UNA VIRGOLETTA NON CHIUSA VIENE SEGNALATA. Prima l'argomento si
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
 * arriva quella costruita da un programma. Sono 64 puntatori sullo stack
 * della shell, cioe' niente. */
#define MAX_ARGS    64
/* 512, cioe' quanto il driver di tastiera puo' consegnare in un messaggio
 * (KBD_LINE_MAX). Era 256: bastava con i nomi 8.3, non basta piu' da
 * quando un nome ext2 puo' essere di 255 byte, perche' `cp <lungo> <dest>`
 * supera i 256. Un nome che si puo' creare ed elencare ma non digitare e'
 * un nome irraggiungibile per meta'. */
#define MAX_LINE    512

static int parse_line(char *line, char *argv[], int max_args)
{
    int argc = 0;
    char *p  = line;

    while (*p && argc < max_args) {
        /* Salta spazi */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (*p == '"' || *p == '\'') {
            char chiusura = *p++;

            argv[argc++] = p;
            while (*p && *p != chiusura) p++;

            if (*p) {
                *p++ = '\0';
            } else {
                print(CLR_YELLOW);
                print("sh: manca la ");
                print(chiusura == '"' ? "virgoletta" : "apice");
                println(" di chiusura");
                print(CLR_RESET);
                return -1;
            }
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }

    /* ⚠️ SI AVVISA QUANDO GLI ARGOMENTI NON CI STANNO TUTTI. Prima quelli
     * oltre il sedicesimo sparivano senza dire niente, e un comando che
     * riceve meta' dei suoi argomenti fa qualcosa di diverso da quello
     * chiesto — non fallisce, il che e' peggio. */
    if (argc == max_args) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p) {
            print(CLR_YELLOW);
            println("sh: troppi argomenti, gli ultimi sono stati ignorati");
            print(CLR_RESET);
        }
    }

    return argc;
}

/* Definita piu' in basso (il motore degli script sta vicino all'autoexec,
 * che e' il suo primo utente), ma serve al dispatch dei built-in qui
 * sopra. In un file solo l'ordine non puo' accontentare tutti. */
static int esegui_script(const char *nome, const char *etichetta);

/* =============================================================================
 * Comandi built-in
 * ============================================================================= */

static void cmd_help(void)
{
    print(CLR_CYAN);
    println("Comandi disponibili:");
    print(CLR_WHITE);
    println("  help              - questo messaggio");
    println("  echo [testo]      - stampa testo");
    println("  cls / clear       - pulisce lo schermo");
    println("  pwd               - directory corrente");
    println("  cd [dir]          - cambia directory");
    println("  ls                - elenca file (root dir)");
    println("  cat [file]        - mostra contenuto file");
    println("  [programma]       - esegue come task autonomo (la shell attende la fine)");
    println("  exec [programma]  - SOSTITUISCE la shell con l'ELF (non torna)");
    println("  env               - mostra variabili d'ambiente");
    println("  export K=V        - imposta variabile d'ambiente");
    println("  uname             - informazioni sistema (riga singola)");
    println("  ver / version     - nome, versione, autore e licenza");
    println("  pid               - mostra PID del processo corrente");
    println("  sleep [ms]        - attende N millisecondi");
    println("  jobs              - elenca i processi lanciati con '&'");
    println("  fg [n]            - riporta in primo piano il job n (l'ultimo se omesso)");
    println("");
    print(CLR_CYAN);
    println("  keymap [it|fr|..] - disposizione della tastiera (senza nome: la mostra)");
    println("  source <file.sh>  - esegue uno script in questa shell (anche `nome.sh`)");
    println("                      !silenced nello script nasconde i comandi, non l'output");
    println("  help helpconfig   - come si installano e configurano i driver");
    print(CLR_WHITE);
    println("");
    println("");
    println("  \"con spazi\"      - un nome con spazi va fra virgolette:");
    println("                      cp \"appunti di riunione.txt\" /disco");
    println("                      gli apici singoli fanno la stessa cosa");
    println("");
    println("  comando &         - esegue in background e torna subito al prompt");
    println("  Alt+F1..F4        - passa da una console virtuale all'altra");
    println("  reboot            - riavvia il sistema");
    println("  halt              - ferma il sistema (non spegne)");
    println("  poweroff/shutdown - ferma e spegne dopo 3 secondi");
    println("  exit [codice]     - termina la shell");
}

/* =============================================================================
 * helpconfig — come si accendono i driver
 *
 * ⚠️ MOSTRA LO STATO, NON SOLO LE ISTRUZIONI. Un elenco di comandi da dare
 * lo si trova gia' nel leggimi; quello che al prompt non si sa e' a che
 * punto si e' arrivati. Chiedere al registro IPC chi c'e' costa una
 * syscall per servizio e trasforma "ecco la procedura" in "sei qui".
 *
 * ⚠️ SI IMPAGINA A MANO. Lo schermo e' 80x25 e questo testo e' piu' lungo:
 * senza pause le prime pagine scorrerebbero via, cioe' proprio quelle che
 * spiegano da dove si comincia. Le pause stanno dove il discorso cambia
 * argomento, non ogni N righe: una pagina che si interrompe a meta' di un
 * elenco e' peggio di una piu' corta.
 * ============================================================================= */

/* Aspetta Invio. Ritorna 0 se si vuole smettere ('q'), 1 per continuare.
 *
 * ⚠️ LEGGE UNA RIGA, NON UN TASTO. La modalita' raw della tastiera
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
    char buf[256];
    int  n = sh_version(buf, sizeof(buf));

    if (n < 0) {
        printerr("version: il kernel non ha fornito l'identità di sistema");
        return;
    }

    print(CLR_GREEN);
    println(buf);
    print(CLR_WHITE);
}

/* uname resta la forma compatta su una riga; `ver`/`version` danno il
 * blocco completo. Entrambi partono dalla stessa sorgente: qui
 * dall'ambiente ereditato dal kernel, che a sua volta viene da
 * kernel.cfg. */
static void cmd_uname(void)
{
    const char *osname = env_get("OSNAME");
    const char *osver  = env_get("OSVER");
    const char *author = env_get("AUTHOR");

    if (!osname) osname = "EX-OS";
    if (!osver)  osver  = "?";
    if (!author) author = "Graziano Falcone";

    print(CLR_GREEN);
    print(osname);
    print(CLR_WHITE);
    print(" version "); print(osver); print(" (x86 32-bit) - ");
    print("Copyright (C) 2025 "); print(author);
    print(" <exagonx@hotmail.com>\n");
    println("Licenza: GNU GPL v2 - Software Libero");
}

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

/* 320 come PERCORSO_MAX del kernel: qui si compone "<PATH>/<comando>", e
 * un buffer piu' corto taglierebbe percorsi che la syscall accetterebbe. */
#define PATH_MAX_SH 320

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
        /* ⚠️ `status` E' GIA' IL CODICE DI USCITA, non l'intero impacchettato
         * di Unix: EX-OS non consegna segnali, quindi il modo di finire e'
         * uno solo e il kernel ci scrive il numero e basta. Vedi il
         * commento su waitpid in lib/include/libc.h. */
        g_ultimo_stato = (int)status;
    }
    sh_setfg(sh_getpid());
}

static void run_program(const char *prog, int argc, char *argv[],
                        int background, const char *cmdline)
{
    char path[PATH_MAX_SH];

    if (prog[0] != '/') {
        const char *path_env = env_get("PATH");
        if (!path_env) path_env = "/bin";

        const char *p = path_env;
        while (*p) {
            uint32_t i = 0;
            while (*p && *p != ':' && i < PATH_MAX_SH - 64) { path[i++] = *p++; }
            if (*p == ':') p++;
            if (i == 0) continue;
            path[i++] = '/';
            sh_strcpy(path + i, prog, PATH_MAX_SH - i);

            /* argv[0] = path completo del comando */
            char *spawn_argv[32];
            int   spawn_argc = 0;
            spawn_argv[spawn_argc++] = path;
            int ai;
            for (ai = 1; ai < argc && spawn_argc < 31; ai++)
                spawn_argv[spawn_argc++] = argv[ai];
            spawn_argv[spawn_argc] = NULL;

            int pid = sh_spawn(path, spawn_argc, spawn_argv);
            if (pid > 0) {
                avvia_figlio(pid, background, cmdline);
                return;
            }
        }
        print("exec: comando non trovato: ");
        printerr(prog);
        /* 127 e' la convenzione di ogni shell per «non trovato», e conta
         * davvero da quando esiste `sh -c`: chi chiama system() distingue
         * cosi' un comando fallito da un comando che non c'e'. */
        g_ultimo_stato = 127;
    } else {
        char *spawn_argv[32];
        int   spawn_argc = 0;
        spawn_argv[spawn_argc++] = (char*)prog;
        int ai;
        for (ai = 1; ai < argc && spawn_argc < 31; ai++)
            spawn_argv[spawn_argc++] = argv[ai];
        spawn_argv[spawn_argc] = NULL;

        int pid = sh_spawn(prog, spawn_argc, spawn_argv);
        if (pid > 0) {
            avvia_figlio(pid, background, cmdline);
        } else {
            print("exec: ");
            print(prog);
            printerr(": non trovato o non eseguibile");
        }
    }
}

static void run_program_replace(const char *prog, int argc, char *argv[])
{
    char path[256];
    (void)argc; (void)argv;

    if (prog[0] != '/') {
        const char *path_env = env_get("PATH");
        if (!path_env) path_env = "/bin";

        const char *p = path_env;
        while (*p) {
            uint32_t i = 0;
            while (*p && *p != ':' && i < PATH_MAX_SH - 64) { path[i++] = *p++; }
            if (*p == ':') p++;
            if (i == 0) continue;
            path[i++] = '/';
            sh_strcpy(path + i, prog, PATH_MAX_SH - i);

            int ret = sh_exec(path);
            if (ret >= 0) return;
        }
        print("exec: comando non trovato: ");
        printerr(prog);
    } else {
        int ret = sh_exec(prog);
        if (ret < 0) {
            print("exec: ");
            print(prog);
            printerr(": non trovato o non eseguibile");
        }
    }
}

static void cmd_exec(int argc, char *argv[])
{
    if (argc < 2) { printerr("exec: uso: exec [programma]"); return; }
    run_program_replace(argv[1], argc - 1, argv + 1);
}

/* Lettura minimale di un file per cmd_cat */
static void cmd_cat(int argc, char *argv[])
{
    char buf[512];
    int  fd, n;

    if (argc < 2) { printerr("cat: uso: cat [file]"); return; }

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
 * Esegue UNA riga, come se fosse stata digitata.
 *
 * ⚠️ ESTRATTA DAL CICLO PRINCIPALE, e non per eleganza: serviva un secondo
 * chiamante. L'autoexec deve eseguire i comandi ESATTAMENTE come li
 * esegue chi li digita — stessi built-in, stesse virgolette, stesso '&'
 * per il background. Riscrivere un secondo interprete accanto al primo
 * avrebbe significato due comportamenti che divergono al primo comando
 * che si aggiunge da una parte sola.
 *
 * Ritorna 0 normalmente, -1 se il comando era `exit`.
 * ============================================================================= */
static int esegui_riga(char *line, int n)
{
    char *argv[MAX_ARGS];
    int   argc;
    int   background;
    char  cmdline[JOB_CMD_LEN];

    if (n <= 0) return 0;

    /* =================================================================
     * '&' finale: esecuzione in background.
     *
     * Si accetta sia "comando &" sia "comando&". La riga viene
     * copiata PRIMA di essere spezzata da parse_line, che ci pianta
     * dentro dei terminatori: serve intera per l'elenco di 'jobs',
     * dove leggere "hello" e' molto piu' utile che leggere un PID.
     * ================================================================= */
    {
        int k = n - 1;
        while (k >= 0 && (line[k] == ' ' || line[k] == '\t')) k--;
        if (k >= 0 && line[k] == '&') {
            background = 1;
            line[k] = '\0';
            n = k;
            while (n > 0 && (line[n-1] == ' ' || line[n-1] == '\t')) line[--n] = '\0';
            if (n == 0) return 0;   /* solo una '&' */
        } else {
            background = 0;
        }
    }
    sh_strcpy(cmdline, line, sizeof(cmdline));

    /* Parsing */
    argc = parse_line(line, argv, MAX_ARGS);
    if (argc <= 0) return 0;    /* vuota, oppure malformata (l'ha gia' detto) */

    /* Dispatch comandi built-in */
    const char *cmd = argv[0];

    /* ⚠️ UNO SCRIPT SI ESEGUE, NON SI LANCIA. `source` non e' un
     * eseguibile: i comandi devono girare in QUESTA shell, altrimenti un
     * `cd` o un `export` dentro lo script sparirebbero insieme al
     * processo figlio — che e' il motivo per cui `source` esiste anche
     * sulle shell vere. */
    if (sh_strcmp(cmd, "source") == 0) {
        if (argc < 2) { printerr("uso: source <file.sh>"); return 0; }
        return esegui_script(argv[1], argv[1]);
    }

    /* ⚠️ UN NOME CHE FINISCE IN .sh E' UNO SCRIPT, e si decide QUI e non
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
        if (argc >= 2 && sh_strcmp(argv[1], "helpconfig") == 0) cmd_helpconfig();
        else                                                    cmd_help();
        return 0;
    }
    if (sh_strcmp(cmd, "echo")  == 0) { cmd_echo(argc,argv);  return 0; }
    if (sh_strcmp(cmd, "cls")   == 0 ||
        sh_strcmp(cmd, "clear") == 0) { cmd_cls();            return 0; }
    if (sh_strcmp(cmd, "pwd")   == 0) { cmd_pwd();            return 0; }
    if (sh_strcmp(cmd, "cd")    == 0) { cmd_cd(argc, argv);   return 0; }
    if (sh_strcmp(cmd, "env")   == 0) { cmd_env();            return 0; }
    if (sh_strcmp(cmd, "export")== 0) { cmd_export(argc,argv);return 0; }
    if (sh_strcmp(cmd, "uname") == 0) { cmd_uname();          return 0; }
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

    /* Comando non built-in: cerca nel PATH e tenta exec */
    run_program(cmd, argc, argv, background, cmdline);
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
 * ⚠️ SE LA MODALITA' RAW NON E' DISPONIBILE SI TORNA A LEGGERE RIGHE.
 * Il servizio 'kbd' potrebbe non essere avviato (kernel.cfg senza la voce
 * [modules], o driver morto). Una shell che in quel caso non accetta piu'
 * comandi sarebbe un sistema inutilizzabile per una funzione di comodo:
 * si ripiega su sh_read() e si perde solo la cronologia.
 *
 * ⚠️ IL DRIVER TORNA IN COOKED DA SOLO quando qualcuno chiede una riga
 * (vedi drivers/kbd/kbd_proto.h): succede ogni volta che un programma
 * lanciato da qui legge da stdin. Percio' la modalita' si riafferma a
 * OGNI prompt invece di impostarla una volta all'avvio — ed e' anche cio'
 * che rende la cosa autoriparante se un job in background la cambia.
 *
 * ⚠️ IL RIDISEGNO USA SOLO BACKSPACE, non '\r' e nessuna sequenza di
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
 * ⚠️ NON SI REGISTRANO LE RIGHE VUOTE NE' I DOPPIONI CONSECUTIVI: chi
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
     * ⚠️ SI RIPROVA ALL'INFINITO, RIAFFERMANDO OGNI VOLTA LA MODALITA'.
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
static int riga_modifica(char *buf, int max)
{
    int len = 0, cur = 0, sfoglia = 0;
    char salvata[MAX_LINE];

    salvata[0] = '\0';
    buf[0] = '\0';

    /* ⚠️ SOLO LA CONSOLE IN PRIMO PIANO PRENDE I TASTI. Senza questo
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

            /* ⚠️ LA RIGA IN CORSO SI SALVA alla prima freccia in su e si
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
         * ⚠️ ANCHE SOPRA 127, e senza questo le disposizioni non inglesi
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

            /* ⚠️ IN CODA SI SCRIVE UN CARATTERE SOLO. Ridisegnare tutta la
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
 * ⚠️ ZITTISCE IL COMANDO, NON IL SUO RISULTATO. E' la distinzione che
 * rende l'opzione utile: quello che un comando stampa e' il motivo per
 * cui lo si e' messo nello script, mentre la riga di comando la si e'
 * gia' scritta e riletterla non aggiunge niente. Uno script che accende
 * la rete deve far vedere l'indirizzo ottenuto, non "dhcp".
 *
 * ⚠️ VALE DA DOVE STA IN POI, non per tutto il file. Cosi' si puo'
 * zittire la parte rumorosa e lasciar vedere quella che interessa,
 * invece di dover scegliere una volta sola per l'intero script. La riga
 * della direttiva non si stampa mai, in nessuno dei due modi.
 *
 * ⚠️ E' DIVERSO DA '@', e i due convivono: '@' zittisce UNA riga,
 * `!silenced` cambia lo stato. Chi ha un solo comando da nascondere non
 * deve ricordarsi di riaccendere.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ SOLO LA PRIMA CONSOLE
 *
 * EX-OS avvia una shell per ognuna delle quattro console virtuali. Senza
 * questo controllo l'autoexec girerebbe QUATTRO VOLTE — e per comandi
 * come `/dev/pci.drv &` significherebbe quattro processi che si
 * contendono lo stesso servizio, con tre che falliscono e un registro
 * pieno di errori a ogni avvio.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ LA VIA D'USCITA DEVE ESISTERE PRIMA DI SERVIRE
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
 * ⚠️ NON C'E' RICORSIONE. Un autoexec che lancia `sh` non rilancia
 * l'autoexec, perche' la seconda shell non e' sulla console 0 — ma se
 * qualcuno cambiasse quel controllo, un autoexec che lancia sh sarebbe un
 * ciclo infinito di processi. E' il motivo per cui la condizione e' "la
 * console" e non "sono la prima shell".
 * ============================================================================= */
#define AUTOEXEC_PREDEFINITO  "/boot/autoexec.sh"

/* ⚠️ UNO SCRIPT PUO' LANCIARNE UN ALTRO, MA NON ALL'INFINITO. Due file
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

    /* ⚠️ SI LEGGE TUTTO IN UNA VOLTA, e il limite e' dichiarato: 2 KB.
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
 * ⚠️ FINO AD AGOSTO 2026 QUESTA ERA `void _start(void)` E NON VEDEVA argv.
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
 * ⚠️ `-c` NON stampa il banner e NON esegue l'autoexec, e non è un
 * dettaglio estetico: chi la chiama è un programma che legge l'output del
 * comando, e il banner glielo sporcherebbe.
 * ============================================================================= */
int shell_main(int argc, char **argv)
{
    char line[MAX_LINE];
    int  n;

    /* Inizializza variabili d'ambiente */
    env_init();

    if (argc >= 2 && sh_strcmp(argv[1], "-c") == 0) {
        if (argc < 3) {
            printerr("sh: -c vuole un comando");
            sh_exit(2);
        }
        /* ⚠️ SI COPIA: esegui_riga() spezza la riga piantandoci dentro dei
         * terminatori, e argv[2] sta nello stack costruito da sys_spawn —
         * non è roba nostra da modificare. */
        sh_strcpy(line, argv[2], MAX_LINE);
        n = 0;
        while (line[n] != '\0' && n < MAX_LINE - 1) n++;

        /* La tastiera è già nostra: senza, un comando interattivo lanciato
         * da qui non riuscirebbe a leggere una riga. */
        sh_setfg(sh_getpid());
        esegui_riga(line, n);
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

        /* ⚠️ PRIMA LA MODIFICA CON CRONOLOGIA, POI IL RIPIEGO. Se il
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

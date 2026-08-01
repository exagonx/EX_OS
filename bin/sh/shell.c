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

/* stdin=0, stdout=1, stderr=2 */
#define STDIN   0
#define STDOUT  1
#define STDERR  2

/* =============================================================================
 * Wrapper syscall inline ASM (stile Linux x86)
 * ============================================================================= */

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

/* sh_waitpid — attende la terminazione del processo `pid`. Se `status`
 * non e' NULL, ci scrive il codice di uscita del figlio. Ritorna il PID
 * raccolto, o un errno negativo. */
static int sh_waitpid(int pid, int32_t *status)
{
    return syscall2(SYS_WAITPID, (uint32_t)pid, (uint32_t)status);
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
    { "PATH",   "/bin:/dev"        },
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
 * prompt e niente output dei comandi — quello è "l'output normale" che va
 * mostrato sempre.
 * ============================================================================= */
static int g_verbose_boot = 1;

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
 * Divide la stringa in argv[] separando per spazi.
 * Ritorna argc.
 * ============================================================================= */
#define MAX_ARGS    16
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

        /* Gestione stringa tra virgolette */
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p) *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }

    return argc;
}

/* =============================================================================
 * Comandi built-in
 * ============================================================================= */

static void cmd_help(void)
{
    print(CLR_CYAN);
    println("Comandi disponibili:");
    print(CLR_WHITE);
    println("  help              — questo messaggio");
    println("  echo [testo]      — stampa testo");
    println("  cls / clear       — pulisce lo schermo");
    println("  pwd               — directory corrente");
    println("  cd [dir]          — cambia directory");
    println("  ls                — elenca file (root dir)");
    println("  cat [file]        — mostra contenuto file");
    println("  [programma]       — esegue come task autonomo (la shell attende la fine)");
    println("  exec [programma]  — SOSTITUISCE la shell con l'ELF (non torna)");
    println("  env               — mostra variabili d'ambiente");
    println("  export K=V        — imposta variabile d'ambiente");
    println("  uname             — informazioni sistema (riga singola)");
    println("  ver / version     — nome, versione, autore e licenza");
    println("  pid               — mostra PID del processo corrente");
    println("  sleep [ms]        — attende N millisecondi");
    println("  reboot            — riavvia il sistema");
    println("  halt              — ferma il sistema (non spegne)");
    println("  poweroff/shutdown — ferma e spegne dopo 3 secondi");
    println("  exit [codice]     — termina la shell");
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
    print(" version "); print(osver); print(" (x86 32-bit) — ");
    print("Copyright (C) 2025 "); print(author);
    print(" <exagonx@hotmail.com>\n");
    println("Licenza: GNU GPL v2 — Software Libero");
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

/* 320 come PERCORSO_MAX del kernel: qui si compone "<PATH>/<comando>", e
 * un buffer piu' corto taglierebbe percorsi che la syscall accetterebbe. */
#define PATH_MAX_SH 320

static void run_program(const char *prog, int argc, char *argv[])
{
    char path[PATH_MAX_SH];
    int32_t status;

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
                sh_waitpid(pid, &status);
                return;
            }
        }
        print("exec: comando non trovato: ");
        printerr(prog);
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
            sh_waitpid(pid, &status);
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
    print(" — Extensible Operating System v"); print(osver); print("\n");
    print("   Copyright (C) 2025 "); print(author); print("\n");
    println("   <exagonx@hotmail.com>");
    println("   Licenza: GNU GPL v2");
    println("  ============================================");
    print(CLR_WHITE);
    println("  Digita 'help' per l'elenco dei comandi.\n");
}

/* =============================================================================
 * main — Entry point della shell
 *
 * Questa è la funzione chiamata dall'ELF loader quando sched_enter_usermode
 * salta all'entry point del binario /bin/sh.
 * ============================================================================= */
void _start(void)
{
    char  line[MAX_LINE];
    char *argv[MAX_ARGS];
    int   argc;
    int   n;

    /* Inizializza variabili d'ambiente */
    env_init();

    /* Deve venire dopo env_init: entrambe passano da SYS_GETENV, ma
     * verboseboot non è una variabile d'ambiente e non finisce in g_env. */
    verbose_init();

    /* Banner, solo se richiesto dalla configurazione */
    if (g_verbose_boot) print_banner();

    /* Loop principale della shell */
    for (;;) {
        /* Prompt */
        print_prompt();

        /* Leggi riga da stdin (tastiera, bloccante) */
        n = sh_read(STDIN, line, MAX_LINE - 1);
        if (n <= 0) {
            sh_yield();
            continue;
        }
        line[n] = '\0';

        /* Rimuovi newline finale */
        if (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) {
            line[n-1] = '\0';
            n--;
        }

        if (n == 0) continue;   /* Riga vuota */

        /* Parsing */
        argc = parse_line(line, argv, MAX_ARGS);
        if (argc == 0) continue;

        /* Dispatch comandi built-in */
        const char *cmd = argv[0];

        if (sh_strcmp(cmd, "help")  == 0) { cmd_help();           continue; }
        if (sh_strcmp(cmd, "echo")  == 0) { cmd_echo(argc,argv);  continue; }
        if (sh_strcmp(cmd, "cls")   == 0 ||
            sh_strcmp(cmd, "clear") == 0) { cmd_cls();            continue; }
        if (sh_strcmp(cmd, "pwd")   == 0) { cmd_pwd();            continue; }
        if (sh_strcmp(cmd, "cd")    == 0) { cmd_cd(argc, argv);   continue; }
        if (sh_strcmp(cmd, "env")   == 0) { cmd_env();            continue; }
        if (sh_strcmp(cmd, "export")== 0) { cmd_export(argc,argv);continue; }
        if (sh_strcmp(cmd, "uname") == 0) { cmd_uname();          continue; }
        if (sh_strcmp(cmd, "ver")   == 0) { cmd_version();        continue; }
        if (sh_strcmp(cmd, "version") == 0) { cmd_version();      continue; }
        if (sh_strcmp(cmd, "pid")   == 0) { cmd_pid();            continue; }
        if (sh_strcmp(cmd, "sleep") == 0) { cmd_sleep(argc,argv); continue; }
        if (sh_strcmp(cmd, "cat")   == 0) { cmd_cat(argc, argv);  continue; }
        if (sh_strcmp(cmd, "exec")  == 0) { cmd_exec(argc, argv); continue; }
        if (sh_strcmp(cmd, "reboot")== 0) { cmd_reboot();         continue; }
        if (sh_strcmp(cmd, "halt")  == 0) { cmd_halt();           continue; }
        if (sh_strcmp(cmd, "poweroff") == 0) { cmd_poweroff();    continue; }
        if (sh_strcmp(cmd, "shutdown") == 0) { cmd_poweroff();    continue; }

        if (sh_strcmp(cmd, "exit")  == 0) {
            int code = (argc >= 2) ? (int)(*argv[1] - '0') : 0;
            sh_exit(code);
        }

        /* Comando non built-in: cerca nel PATH e tenta exec */
        run_program(cmd, argc, argv);
    }

    sh_exit(0);
}

/* =============================================================================
 * tools/mariadb-exos/mariadb-svc.c — the MariaDB service of EX-OS
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     mariadb-svc -install     user mysql, /etc/my.cnf, /var/lib/mysql with
 *                              the system tables, and the block in
 *                              /boot/avvio.sh that starts the server
 *     mariadb-svc -remove      stops it and takes the block away (the data
 *                              and /etc/my.cnf stay)
 *     mariadb-svc -avvia       starts mariadbd and waits until it answers
 *     mariadb-svc -ferma       SIGTERM, and waits until it has ended
 *     mariadb-svc -stato       running or not, and on which port
 *
 * ! THE SERVER RUNS BEFORE ANYONE LOGS IN: login runs /boot/avvio.sh as root
 * at boot, the block calls `mariadb-svc -avvia`, mariadbd drops to the user
 * mysql itself (user=mysql in /etc/my.cnf). The shutdown stops it: the
 * kernel sends SIGTERM to every program and waits before syncing the disks
 * (segnali_arresto, kernel 0.279), and mariadbd closes InnoDB on SIGTERM.
 *
 * ! TCP ONLY, ON 127.0.0.1: EX-OS has no AF_UNIX sockets. The [client]
 * section of /etc/my.cnf sends `mariadb`, `mariadb-admin`, `mariadb-dump` to
 * 127.0.0.1:3306 without options. root@localhost has no password at first:
 * nobody outside the machine can reach the port; set one with
 *     mariadb -u root -e "ALTER USER root@localhost IDENTIFIED BY '...'"
 *
 * The user mysql has a line in /boot/utenti and none in /boot/ombra: it
 * owns the data, and nobody can log in as mysql.
 *
 * -install also makes the TLS certificate (server-cert.pem, server-key.pem
 * in the data directory, EC P-256): see g_conf_tls.
 * ============================================================================= */
#include <libc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

EX_VERSIONE("mariadb-svc", "0.002");

#define BASE        "/exos/mariadb"
#define MARIADBD    BASE "/bin/mariadbd"
#define BOOTSTRAP   BASE "/share/bootstrap.sql"
#define CONF        "/etc/my.cnf"
#define DATI        "/var/lib/mysql"
#define TMP         "/var/lib/mysql-tmp"
#define LOGDIR      "/var/log/mariadb"
#define LOG         LOGDIR "/mariadb.log"
#define USCITA      LOGDIR "/mariadbd.out"     /* what mariadbd prints itself */
#define PIDFILE     DATI "/mariadbd.pid"
#define UTENTI      "/boot/utenti"
#define UTENTE      "mysql"
#define PORTA       3306
#define BLOCCO      "mariadb-svc"
#define CERT        DATI "/server-cert.pem"
#define CHIAVE      DATI "/server-key.pem"

/* ! THE SIZES ARE EX-OS'S, NOT MARIADB'S DEFAULTS: 24 TCP connections in the
 * whole system and 64 sockets per program (ip.drv), 128 descriptors per
 * program, and a program's address space ends at 3 GB, so InnoDB must be
 * told how far its buffer pool may grow (it reserves 8 TB at 64 bit). */
static const char *const g_conf =
    "# /etc/my.cnf - MariaDB on EX-OS, written by `mariadb-svc -install`.\n"
    "# Changes take effect at the next `mariadb-svc -ferma` / `-avvia`.\n"
    "\n"
    "[mysqld]\n"
    "user                        = mysql\n"
    "basedir                     = " BASE "\n"
    "datadir                     = " DATI "\n"
    "tmpdir                      = " TMP "\n"
    "lc-messages-dir             = " BASE "/share\n"
    "character-sets-dir          = " BASE "/share/charsets\n"
    "pid-file                    = " PIDFILE "\n"
    "log-error                   = " LOG "\n"
    "# TCP only (EX-OS has no AF_UNIX sockets), and only from this machine:\n"
    "# 0.0.0.0 here opens it to the network.\n"
    "bind-address                = 127.0.0.1\n"
    "port                        = 3306\n"
    "skip-name-resolve\n"
    "max_connections             = 16\n"
    "table_open_cache            = 64\n"
    "innodb_open_files           = 32\n"
    "innodb_buffer_pool_size     = 64M\n"
    "# InnoDB reserves address space for the largest pool it may grow to:\n"
    "# 8 TB by default at 64 bit, and an EX-OS program has 3 GB.\n"
    "innodb_buffer_pool_size_max = 256M\n"
    "innodb_log_file_size        = 32M\n";

/* ! THE TLS CERTIFICATE IS MADE ONCE, BY -install (mariadb-svc 0.002): left
 * alone, MariaDB 11.8 makes a 4096-bit RSA key at every start
 * (vio_keygen), 20 s to minutes on a slow CPU, at random; -install timed
 * out on it. Written only when the files are there: an ssl-cert that does
 * not exist stops the server ("Failed to setup SSL"). */
static const char *const g_conf_tls =
    "# TLS: made once by -install (an EC P-256 key, valid 10 years)\n"
    "ssl-cert                    = " CERT "\n"
    "ssl-key                     = " CHIAVE "\n";

static const char *const g_conf_client =
    "\n"
    "[client]\n"
    "host                        = 127.0.0.1\n"
    "port                        = 3306\n"
    "character-sets-dir          = " BASE "/share/charsets\n";

static void uso(void)
{
    printf("uso: mariadb-svc -install | -remove | -avvia | -ferma | -stato\n\n");
    printf("  -install   crea l'utente mysql, " CONF ", " DATI " con le\n");
    printf("             tabelle di sistema, e accende il server all'avvio\n");
    printf("             (un blocco in /boot/avvio.sh). Solo root.\n");
    printf("  -remove    lo ferma e toglie il blocco; dati e " CONF " restano\n");
    printf("  -avvia     accende mariadbd e aspetta che risponda\n");
    printf("  -ferma     lo ferma (SIGTERM) e aspetta che abbia chiuso\n");
    printf("  -stato     dice se gira\n\n");
    printf("Il server ascolta su 127.0.0.1:%d; il registro e' " LOG ".\n", PORTA);
    printf("Da qui: mariadb -u root   (root@localhost senza password, all'inizio)\n");
}

/* ----------------------------------------------------------------------------- */

static int esiste(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

/* mkdir -p, then the owner (uid 0 = leave it to root) */
static int cartella(const char *p, unsigned int uid)
{
    char b[256];
    size_t i, n = strlen(p);

    if (n >= sizeof b) return -1;
    memcpy(b, p, n + 1);
    for (i = 1; i <= n; i++) {
        if (b[i] != '/' && b[i] != 0) continue;
        b[i] = 0;
        if (!esiste(b) && mkdir(b, 0755) != 0) {
            printf("mariadb-svc: non riesco a creare %s (%s)\n", b, strerror(errno));
            return -1;
        }
        b[i] = p[i];
    }
    if (uid && chown(p, uid, uid) != 0) {
        printf("mariadb-svc: non riesco a dare %s a " UTENTE " (%s)\n", p, strerror(errno));
        return -1;
    }
    return 0;
}

/* The pid in the pid file if that program is alive, else 0. */
static int pid_vivo(void)
{
    FILE *f = fopen(PIDFILE, "r");
    int   pid = 0;

    if (!f) return 0;
    if (fscanf(f, "%d", &pid) != 1) pid = 0;
    fclose(f);
    if (pid > 1 && kill(pid, 0) == 0) return pid;
    return 0;
}

/* Where the log ends now, and whether mariadbd wrote "ready for
 * connections" after that point. ! THE LOG AND NOT A connect(): a connect
 * to a port nobody listens on yet gets no RST from ip.drv and waits for its
 * timeout (voce 28 in the queue), which is all of InnoDB's startup. */
static long fine_registro(void)
{
    struct stat st;
    return stat(LOG, &st) == 0 ? (long)st.st_size : 0;
}

static int pronto_dopo(long da)
{
    FILE *f = fopen(LOG, "r");
    char  r[512];
    int   si = 0;

    if (!f) return 0;
    if (fseek(f, da, SEEK_SET) == 0)
        while (!si && fgets(r, sizeof r, f)) si = strstr(r, "ready for connections") != NULL;
    fclose(f);
    return si;
}

/* 1 if something accepts on 127.0.0.1:3306 (only asked of a running server) */
static int risponde(void)
{
    struct sockaddr_in a;
    int s = socket(AF_INET, SOCK_STREAM, 0), ok;

    if (s < 0) return 0;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(PORTA);
    a.sin_addr.s_addr = htonl(0x7F000001);
    ok = connect(s, (struct sockaddr *)&a, sizeof a) == 0;
    close(s);
    return ok;
}

static void dormi_ms(long ms)
{
    struct timespec t;
    t.tv_sec = ms / 1000; t.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
}

/* the last lines of the log, when something went wrong */
static void coda_del_registro(void)
{
    FILE *f = fopen(LOG, "r");
    char  r[256], ultime[8][256];
    int   n = 0, i;

    if (!f) return;
    while (fgets(r, sizeof r, f)) { strcpy(ultime[n % 8], r); n++; }
    fclose(f);
    printf("  le ultime righe di " LOG ":\n");
    for (i = n > 8 ? n - 8 : 0; i < n; i++) printf("    %s", ultime[i % 8]);
}

/* mariadbd with these options; stdin from `ingresso` (NULL = none), stdout
 * and stderr appended to USCITA. Returns the pid. */
static int lancia(const char *opz1, const char *opz2, const char *ingresso)
{
    char       *argv[5];
    SpawnRedir  r[3];
    int         n = 0, a = 0;

    argv[a++] = (char *)MARIADBD;
    argv[a++] = (char *)"--defaults-file=" CONF;
    if (opz1) argv[a++] = (char *)opz1;
    if (opz2) argv[a++] = (char *)opz2;
    argv[a] = NULL;
    if (ingresso) { r[n].fd = 0; r[n].flags = O_RDONLY; r[n].percorso = ingresso; r[n].fd_padre = -1; n++; }
    r[n].fd = 1; r[n].flags = O_WRONLY | O_CREAT | O_APPEND; r[n].percorso = USCITA; r[n].fd_padre = -1; n++;
    r[n].fd = 2; r[n].flags = O_WRONLY | O_CREAT | O_APPEND; r[n].percorso = USCITA; r[n].fd_padre = -1; n++;
    return spawn_ex(MARIADBD, argv, environ, r, n);
}

/* ----------------------------------------------------------------------------- */

static int avvia(void)
{
    int  pid, i, stato;
    long da;

    if (pid_vivo()) { printf("mariadb-svc: mariadbd gira gia' (pid %d)\n", pid_vivo()); return 0; }
    if (!esiste(CONF) || !esiste(DATI "/mysql")) {
        printf("mariadb-svc: manca la configurazione: prima `mariadb-svc -install`\n");
        return 1;
    }
    /* ! ip.drv FIRST: 127.0.0.1 is inside it. At boot it is started a line
     * earlier and waits up to 30 s for a card before it stays with the
     * loopback alone (ip.drv 0.014); mariadbd would fail its bind before. */
    if (ipc_attendi("ip", 40000) <= 0) {
        printf("mariadb-svc: ip.drv non gira, e 127.0.0.1 sta dentro di lui: `ip.drv &`\n");
        return 1;
    }
    da = fine_registro();
    pid = lancia(NULL, NULL, NULL);
    if (pid < 0) { printf("mariadb-svc: non parte " MARIADBD " (%s)\n", strerror(-pid)); return 1; }
    /* InnoDB takes a few seconds; 120 for a slow disk after a crash */
    for (i = 0; i < 240; i++) {
        if (pronto_dopo(da)) {
            printf("mariadb-svc: MariaDB pronto su 127.0.0.1:%d (pid %d)\n", PORTA, pid);
            return 0;
        }
        if (waitpid(pid, &stato, WNOHANG) == pid) {
            printf("mariadb-svc: mariadbd si e' fermato (codice %d)\n", WEXITSTATUS(stato));
            coda_del_registro();
            return 1;
        }
        dormi_ms(500);
    }
    printf("mariadb-svc: mariadbd (pid %d) non risponde dopo 120 s\n", pid);
    coda_del_registro();
    return 1;
}

static int ferma(void)
{
    int pid = pid_vivo(), i;

    if (!pid) { printf("mariadb-svc: mariadbd non gira\n"); return 0; }
    if (kill(pid, SIGTERM) != 0) {
        printf("mariadb-svc: kill %d: %s\n", pid, strerror(errno));
        return 1;
    }
    for (i = 0; i < 240; i++) {
        if (kill(pid, 0) != 0) { printf("mariadb-svc: mariadbd fermato\n"); return 0; }
        dormi_ms(500);
    }
    printf("mariadb-svc: mariadbd (pid %d) non si e' fermato in 120 s\n", pid);
    return 1;
}

static int stato(void)
{
    int pid = pid_vivo();

    if (!pid) { printf("mariadb-svc: mariadbd non gira\n"); return 3; }
    printf("mariadb-svc: mariadbd gira (pid %d), %s su 127.0.0.1:%d\n",
           pid, risponde() ? "risponde" : "NON risponde", PORTA);
    return 0;
}

/* The uid of mysql, made if there is none: the first free from 100, below
 * the people (exuser gives them 1000 and up). */
static unsigned int utente_mysql(void)
{
    struct passwd *pw = getpwnam(UTENTE);
    unsigned char  preso[900];
    unsigned int   uid;
    char           riga[160];
    FILE          *f;

    if (pw) return (unsigned int)pw->pw_uid;
    f = fopen(UTENTI, "r");
    if (!f) {
        printf("mariadb-svc: manca " UTENTI ": serve un sistema installato\n");
        return 0;
    }
    memset(preso, 0, sizeof preso);
    while (fgets(riga, sizeof riga, f)) {
        char *a = strchr(riga, ':');
        if (a && (uid = (unsigned int)strtoul(a + 1, NULL, 10)) >= 100 && uid < 1000)
            preso[uid - 100] = 1;
    }
    fclose(f);
    for (uid = 100; uid < 1000 && preso[uid - 100]; uid++) ;
    if (uid == 1000) { printf("mariadb-svc: nessun uid libero fra 100 e 999\n"); return 0; }
    f = fopen(UTENTI, "a");
    if (!f || fprintf(f, UTENTE ":%u:%u\n", uid, uid) < 0) {
        printf("mariadb-svc: non riesco a scrivere " UTENTI " (%s)\n", strerror(errno));
        if (f) fclose(f);
        return 0;
    }
    fclose(f);
    printf("  utente " UTENTE " (uid %u), senza password: nessuno entra come " UTENTE "\n", uid);
    return uid;
}

/* An EC P-256 key and a self-signed certificate for the server, with the
 * libcrypto of EX-OS. RAND may refuse on a machine just switched on (a small
 * entropy pool): a few tries, then without (the server makes its own). */
static int certificato(unsigned int uid)
{
    EVP_PKEY      *k = NULL;
    X509          *x = NULL;
    X509_NAME     *n;
    unsigned char  sn[8];
    FILE          *f;
    int            i, ok = 0;

    if (esiste(CERT) && esiste(CHIAVE)) return 1;
    for (i = 0; i < 10 && !k; i++) {
        k = EVP_EC_gen("P-256");
        if (!k) dormi_ms(1000);
    }
    if (!k || RAND_bytes(sn, sizeof sn) != 1 || !(x = X509_new())) goto fine;
    sn[0] &= 0x7F;                                     /* a positive serial */
    X509_set_version(x, X509_VERSION_3);
    ASN1_INTEGER_set_uint64(X509_get_serialNumber(x),
        ((uint64_t)sn[0] << 56) | ((uint64_t)sn[1] << 48) | ((uint64_t)sn[2] << 40) |
        ((uint64_t)sn[3] << 32) | ((uint64_t)sn[4] << 24) | ((uint64_t)sn[5] << 16) |
        ((uint64_t)sn[6] << 8) | sn[7]);
    X509_gmtime_adj(X509_getm_notBefore(x), 0);
    X509_gmtime_adj(X509_getm_notAfter(x), 10L * 365 * 24 * 3600);
    X509_set_pubkey(x, k);
    /* a name of its own: OpenSSL 4 gives the certificate's one as const */
    if (!(n = X509_NAME_new())) goto fine;
    X509_NAME_add_entry_by_txt(n, "O", MBSTRING_ASC, (const unsigned char *)"EX-OS", -1, -1, 0);
    X509_NAME_add_entry_by_txt(n, "CN", MBSTRING_ASC, (const unsigned char *)"MariaDB server", -1, -1, 0);
    X509_set_subject_name(x, n);
    X509_set_issuer_name(x, n);
    X509_NAME_free(n);
    if (!X509_sign(x, k, EVP_sha256())) goto fine;
    if (!(f = fopen(CHIAVE, "w"))) goto fine;
    ok = PEM_write_PrivateKey(f, k, NULL, NULL, 0, NULL, NULL);
    fclose(f);
    if (ok && (f = fopen(CERT, "w"))) {
        ok = PEM_write_X509(f, x);
        fclose(f);
    } else {
        ok = 0;
    }
    if (ok) {
        chmod(CHIAVE, 0600);
        if (chown(CHIAVE, uid, uid) != 0 || chown(CERT, uid, uid) != 0) ok = 0;
    }
    if (!ok) { unlink(CHIAVE); unlink(CERT); }
fine:
    X509_free(x);
    EVP_PKEY_free(k);
    if (ok) printf("  certificato TLS in " CERT "\n");
    else    printf("  ! certificato TLS non fatto: MariaDB ne fara' uno lento a ogni avvio\n");
    return ok;
}

static int installa(void)
{
    unsigned int uid;
    char         righe[256];
    int          pid, st = 0;

    if (getuid() != 0) { printf("mariadb-svc: -install e -remove solo da root\n"); return 1; }
    if (!esiste(MARIADBD) || !esiste(BOOTSTRAP)) {
        printf("mariadb-svc: manca " BASE ": `netupdate -install:mariadb` o il CD\n");
        printf("             degli strumenti (gruppo [mariadb])\n");
        return 1;
    }
    if ((uid = utente_mysql()) == 0) return 1;
    if (cartella("/etc", 0) || cartella(DATI, uid) || cartella(TMP, uid) || cartella(LOGDIR, uid))
        return 1;
    if (!esiste(CONF)) {
        int   tls = certificato(uid);
        FILE *f = fopen(CONF, "w");
        if (!f || fputs(g_conf, f) < 0 || (tls && fputs(g_conf_tls, f) < 0) || fputs(g_conf_client, f) < 0) {
            printf("mariadb-svc: non riesco a scrivere " CONF "\n");
            if (f) fclose(f);
            return 1;
        }
        fclose(f);
        printf("  " CONF " scritto\n");
    } else {
        printf("  " CONF " c'e' gia': lo lascio com'e'\n");
    }

    /* the system tables, once: what mariadb-install-db (a shell script) does */
    if (!esiste(DATI "/mysql")) {
        printf("  le tabelle di sistema in " DATI " (un minuto o due)...\n");
        pid = lancia("--bootstrap", "--log-warnings=0", BOOTSTRAP);
        if (pid < 0 || waitpid(pid, &st, 0) != pid || WEXITSTATUS(st) != 0 || !esiste(DATI "/mysql")) {
            printf("mariadb-svc: il bootstrap non e' riuscito\n");
            coda_del_registro();
            return 1;
        }
        printf("  fatto\n");
    }

    /* ! ip.drv EVEN WITHOUT A NETWORK: 127.0.0.1 is inside it (the loopback
     * of ip.drv 0.013), and without a card it stays with that alone (0.014);
     * hwconfig writes `ip.drv &` where there is a card. */
    snprintf(righe, sizeof righe, "# MariaDB, 127.0.0.1:%d (" CONF ")\n%s" "/exos/bin/mariadb-svc -avvia\n",
             PORTA, (avvio_ha_riga("ip.drv") || avvio_ha_riga("/dev/ip.drv")) ? "" : "ip.drv &\n");
    if (avvio_blocco(BLOCCO, NULL) < 0 || avvio_blocco(BLOCCO, righe) < 0) {
        printf("mariadb-svc: non riesco a scrivere /boot/avvio.sh (%s)\n", strerror(errno));
        return 1;
    }
    printf("  /boot/avvio.sh: il server parte a ogni avvio\n");
    if (ipc_lookup("ip") <= 0) {
        char *av[2] = { (char *)"/dev/ip.drv", NULL };
        if (spawn(av[0], av) < 0) printf("mariadb-svc: non parte /dev/ip.drv\n");
        else printf("  ip.drv acceso (127.0.0.1)\n");
    }
    return avvia();
}

static int togli(void)
{
    int r;

    if (getuid() != 0) { printf("mariadb-svc: -install e -remove solo da root\n"); return 1; }
    r = ferma();
    if (avvio_blocco(BLOCCO, NULL) < 0) {
        printf("mariadb-svc: non riesco a scrivere /boot/avvio.sh (%s)\n", strerror(errno));
        return 1;
    }
    printf("  tolto da /boot/avvio.sh; " DATI " e " CONF " restano\n");
    return r;
}

int main(int argc, char **argv)
{
    if (argc != 2) { uso(); return 2; }
    if (strcmp(argv[1], "-install") == 0) return installa();
    if (strcmp(argv[1], "-remove") == 0)  return togli();
    if (strcmp(argv[1], "-avvia") == 0)   return avvia();
    if (strcmp(argv[1], "-ferma") == 0)   return ferma();
    if (strcmp(argv[1], "-stato") == 0)   return stato();
    uso();
    return strcmp(argv[1], "-?") == 0 ? 0 : 2;
}

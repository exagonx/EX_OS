/* tools/prove/sqliteio.c - every way SQLite talks to the disk, on EX-OS.
 * Part 1 tries each system call SQLite uses, one by one, as it uses them.
 * Part 2 is SQLite itself, with Firefox's options, several threads on one
 * directory. Failures print SQLite's extended code and errno.
 * Usage: sqliteio <directory> [threads] [rounds]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "sqlite3.h"

static const char *g_dir;
static int g_no;
static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;

static void ok(int cond, const char *what)
{
    if (cond) printf("  [OK] %s\n", what);
    else { printf("  [NO] %s (errno %d %s)\n", what, errno, strerror(errno)); g_no++; }
    fflush(stdout);
}

static void percorso(char *b, size_t n, const char *f)
{
    snprintf(b, n, "%s/%s", g_dir, f);
}

static void chiamate(void)
{
    char a[256], b[256], buf[4096], rd[4096];
    int fd, fd2, i;
    struct stat st;
    void *m;

    printf("== 1. le chiamate di SQLite\n");
    fd = open(g_dir, O_RDONLY);
    if (fd < 0) printf("  [--] open(directory) rifiutato, errno %d: SQLite salta il sync\n", errno);
    else {
        printf("  [--] open(directory) riuscito\n");
        ok(fsync(fd) == 0, "fsync su una directory");
        close(fd);
    }

    percorso(a, sizeof a, "buchi");
    fd = open(a, O_RDWR | O_CREAT, 0644);
    ok(fd >= 0, "open O_CREAT");
    memset(buf, 'A', sizeof buf);
    ok(pwrite(fd, buf, 1, 32768 * 8 - 1) == 1, "pwrite di 1 byte lontano (buco)");
    for (i = 7; i >= 0; i--)
        if (pwrite(fd, buf, 4096, i * 32768L) != 4096) break;
    ok(i < 0, "riempire i buchi all'indietro");
    for (i = 0; i < 8; i += 2)
        if (pwrite(fd, buf, 32768 > 4096 ? 4096 : 0, i * 32768L + 8192) != 4096) break;
    ok(i >= 8, "riempire buchi a salti");
    ok(pread(fd, rd, 4096, 0) == 4096 && memcmp(buf, rd, 4096) == 0, "rilettura");
    ok(pread(fd, rd, 4096, 4096) == 4096 && rd[0] == 0, "un buco si legge a zero");
    ok(ftruncate(fd, 1 << 20) == 0, "ftruncate allunga");
    ok(fstat(fd, &st) == 0 && st.st_size == 1 << 20, "fstat vede la misura");
    ok(st.st_ino != 0, "st_ino non zero");
    ok(st.st_blksize > 0, "st_blksize non zero");
    printf("  [--] st_blksize %ld\n", (long)st.st_blksize);
    ok(pwrite(fd, buf, 4096, 512 * 1024) == 4096, "pwrite dentro lo spazio allungato");
    ok(ftruncate(fd, 4096) == 0, "ftruncate accorcia");
    ok(ftruncate(fd, 0) == 0, "ftruncate a zero");
    ok(fsync(fd) == 0, "fsync su un file");
    ok(fdatasync(fd) == 0, "fdatasync");

    errno = 0;
    fd2 = open(a, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (fd2 >= 0) { printf("  [NO] O_EXCL: file esistente aperto lo stesso\n"); g_no++; close(fd2); }
    else ok(errno == EEXIST, "O_EXCL su un file che esiste da' EEXIST");

    percorso(b, sizeof b, "buchi-2");
    ok(pwrite(fd, "x", 1, 0) == 1, "scrittura prima della cancellazione");
    ok(unlink(a) == 0, "unlink con il file aperto");
    ok(access(a, F_OK) != 0, "il nome e' sparito");
    ok(pwrite(fd, "y", 1, 1) == 1, "pwrite dopo unlink");
    ok(pread(fd, rd, 2, 0) == 2 && rd[0] == 'x' && rd[1] == 'y', "pread dopo unlink");
    ok(fsync(fd) == 0, "fsync dopo unlink");
    ok(ftruncate(fd, 0) == 0, "ftruncate dopo unlink");
    close(fd);

    percorso(a, sizeof a, "ren-a");
    fd = open(a, O_RDWR | O_CREAT, 0644);
    ok(pwrite(fd, "a", 1, 0) == 1, "file da rinominare");
    ok(rename(a, b) == 0, "rename con il file aperto");
    ok(pwrite(fd, "b", 1, 1) == 1, "pwrite dopo rename");
    ok(fsync(fd) == 0, "fsync dopo rename");
    close(fd);
    ok(unlink(b) == 0, "unlink");

    percorso(a, sizeof a, "condiviso");
    fd = open(a, O_RDWR | O_CREAT, 0644);
    ok(ftruncate(fd, 32768) == 0, "misura per mmap");
    m = mmap(NULL, 32768, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) ok(0, "mmap MAP_SHARED su file");
    else {
        printf("  [OK] mmap MAP_SHARED su file\n");
        memcpy(m, "zz", 2);
        ok(msync(m, 32768, MS_SYNC) == 0, "msync");
        ok(pread(fd, rd, 2, 0) == 2 && rd[0] == 'z', "la scrittura in memoria arriva al file");
        fd2 = open(a, O_RDWR);
        {
            void *m2 = mmap(NULL, 32768, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
            if (m2 == MAP_FAILED) ok(0, "seconda mmap MAP_SHARED");
            else {
                ((char *)m)[100] = 'Q';
                ok(((char *)m2)[100] == 'Q', "due mappe dello stesso file vedono la stessa memoria");
                munmap(m2, 32768);
            }
        }
        close(fd2);
        munmap(m, 32768);
    }
    close(fd);
    unlink(a);

    percorso(a, sizeof a, "lucchetto");
    fd = open(a, O_RDWR | O_CREAT, 0644);
    {
        struct flock l;
        memset(&l, 0, sizeof l);
        l.l_type = F_WRLCK; l.l_whence = SEEK_SET; l.l_start = 1073741824; l.l_len = 1;
        ok(fcntl(fd, F_SETLK, &l) == 0, "F_SETLK a 1 GB (il byte di SQLite)");
        l.l_type = F_WRLCK;
        ok(fcntl(fd, F_GETLK, &l) == 0, "F_GETLK");
        l.l_type = F_UNLCK; l.l_start = 1073741824; l.l_len = 1; l.l_whence = SEEK_SET;
        ok(fcntl(fd, F_SETLK, &l) == 0, "F_SETLK sblocca");
    }
    close(fd);
    unlink(a);
}

static void registro(void *u, int code, const char *msg)
{
    (void)u;
    pthread_mutex_lock(&g_m);
    printf("  [SQLITE] codice %d (base %d) errno %d: %s\n", code, code & 255,
           errno, msg ? msg : "");
    fflush(stdout);
    if ((code & 255) == SQLITE_IOERR || (code & 255) == SQLITE_CORRUPT ||
        (code & 255) == SQLITE_NOTADB || (code & 255) == SQLITE_CANTOPEN) g_no++;
    pthread_mutex_unlock(&g_m);
}

static int g_giri = 30;

static int esegui(sqlite3 *d, const char *q, const char *chi)
{
    char *e = NULL;
    int r = sqlite3_exec(d, q, NULL, NULL, &e);

    if (r != SQLITE_OK && r != SQLITE_BUSY && r != SQLITE_LOCKED) {
        pthread_mutex_lock(&g_m);
        printf("  [NO] %s: '%.60s' -> %d esteso %d: %s\n", chi, q, r,
               sqlite3_extended_errcode(d), e ? e : "");
        g_no++;
        pthread_mutex_unlock(&g_m);
    }
    sqlite3_free(e);
    return r;
}

static void *filo(void *arg)
{
    long n = (long)arg;
    static const char *const modo[] = { "PRAGMA journal_mode=DELETE", "PRAGMA journal_mode=TRUNCATE",
                                        "PRAGMA journal_mode=WAL" };
    char nome[256], f[64], q[256];
    int g, i;

    for (g = 0; g < g_giri; g++) {
        sqlite3 *d;

        snprintf(f, sizeof f, "db%ld.%d.sqlite", n, g % 3);
        percorso(nome, sizeof nome, f);
        if (g >= 3) unlink(nome);
        if (sqlite3_open(nome, &d) != SQLITE_OK) {
            pthread_mutex_lock(&g_m);
            printf("  [NO] apertura %s: %s\n", nome, sqlite3_errmsg(d));
            g_no++;
            pthread_mutex_unlock(&g_m);
            sqlite3_close(d);
            continue;
        }
        sqlite3_busy_timeout(d, 20000);
        esegui(d, "PRAGMA page_size=32768", f);
        esegui(d, "PRAGMA synchronous=NORMAL", f);
        esegui(d, "PRAGMA auto_vacuum=2", f);
        esegui(d, modo[g % 3], f);
        esegui(d, "CREATE TABLE IF NOT EXISTS t(a INTEGER PRIMARY KEY, b TEXT)", f);
        for (i = 0; i < 12; i++) {
            snprintf(q, sizeof q, "INSERT INTO t(b) VALUES(hex(randomblob(%d)))", 200 + i * 900);
            esegui(d, q, f);
        }
        esegui(d, "DELETE FROM t WHERE a%2=0", f);
        esegui(d, "PRAGMA incremental_vacuum", f);
        esegui(d, "PRAGMA integrity_check", f);
        sqlite3_close(d);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    int fili = argc > 2 ? atoi(argv[2]) : 4;
    pthread_t t[32];
    long i;

    g_dir = argc > 1 ? argv[1] : ".";
    if (argc > 3) g_giri = atoi(argv[3]);
    setvbuf(stdout, NULL, _IONBF, 0);
    sqlite3_config(SQLITE_CONFIG_LOG, registro, NULL);
    chiamate();
    printf("== 2. SQLite, %d fili, %d giri\n", fili, g_giri);
    for (i = 0; i < fili; i++) pthread_create(&t[i], NULL, filo, (void *)i);
    for (i = 0; i < fili; i++) pthread_join(t[i], NULL);
    printf("sqliteio: %d NO\n", g_no);
    return g_no != 0;
}

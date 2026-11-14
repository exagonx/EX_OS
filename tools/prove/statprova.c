/* tools/prove/statprova.c <ext2 dir> <fat dir> <cd dir>: what stat() and
 * fstat() say, against what Linux wrote (kernel 0.339, Stat with mode,
 * owner, links and Unix times)
 *
 * tools/prova_stat.sh makes the volumes: on ext2 f751 (0751, 1234:56,
 * mtime 1000000000, atime 1100000000) with a hard link due, a directory d
 * (0710) with a subdirectory, a symbolic link lnk; on the CD tempo.txt with
 * mtime 1000000000.
 * Lines: "STAT <name> <0|1> ..." (0 = good), last "STAT fine <errors>". */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

static int errori;

static void esito(const char *nome, int male, const char *cosa)
{
    printf("STAT %s %d %s\n", nome, male, cosa);
    errori += male;
}

static void mostra(char *b, size_t n, const char *f, const struct stat *s)
{
    snprintf(b, n, "%s: modo %o nlink %u uid %u gid %u size %ld mtime %lld atime %lld ctime %lld ino %lu",
             f, (unsigned)s->st_mode, (unsigned)s->st_nlink, (unsigned)s->st_uid,
             (unsigned)s->st_gid, (long)s->st_size, (long long)s->st_mtime,
             (long long)s->st_atime, (long long)s->st_ctime, (unsigned long)s->st_ino);
}

/* the old SYS_STAT (106), straight to the kernel */
static int stat_vecchia(const char *p)
{
    char buf[64];
    int  r;

    __asm__ volatile("int $0x80" : "=a"(r) : "a"(106), "b"(p), "c"(buf), "d"(0) : "memory");
    return r;
}

int main(int argc, char **argv)
{
    char        f[256], g[256], cosa[300];
    struct stat s, t;
    int         fd;
    long long   ora;

    if (argc < 4) { puts("STAT fine 1"); return 1; }

    /* --- ext2, as Linux wrote it --- */
    snprintf(f, sizeof(f), "%s/f751", argv[1]);
    if (stat(f, &s) != 0) { esito("ext2", 1, "stat f751 fallita"); }
    else {
        mostra(cosa, sizeof(cosa), "f751", &s);
        esito("ext2", s.st_mode != 0100751 || s.st_uid != 1234 || s.st_gid != 56 ||
                      s.st_nlink != 2 || s.st_mtime != 1000000000 ||
                      s.st_atime != 1100000000 || s.st_ctime < 1600000000 || s.st_size != 5, cosa);
    }
    snprintf(g, sizeof(g), "%s/due", argv[1]);
    if (stat(g, &t) != 0) esito("collegamento", 1, "stat due fallita");
    else {
        snprintf(cosa, sizeof(cosa), "due: ino %lu nlink %u, f751 ino %lu",
                 (unsigned long)t.st_ino, (unsigned)t.st_nlink, (unsigned long)s.st_ino);
        esito("collegamento", t.st_ino != s.st_ino || t.st_nlink != 2, cosa);
    }
    snprintf(g, sizeof(g), "%s/d", argv[1]);
    if (stat(g, &t) != 0) esito("directory", 1, "stat d fallita");
    else {
        mostra(cosa, sizeof(cosa), "d", &t);
        esito("directory", t.st_mode != 040710 || t.st_nlink != 3, cosa);
    }
    snprintf(g, sizeof(g), "%s/lnk", argv[1]);
    if (stat(g, &t) != 0) esito("simbolico", 1, "stat lnk fallita");
    else {
        mostra(cosa, sizeof(cosa), "lnk", &t);
        esito("simbolico", !S_ISLNK(t.st_mode), cosa);
    }

    /* --- chmod, then stat and fstat agree --- */
    if (chmod(f, 0640) != 0) esito("chmod", 1, "chmod 0640 fallita");
    fd = open(f, O_RDONLY);
    if (fd < 0 || stat(f, &s) != 0 || fstat(fd, &t) != 0) esito("fstat", 1, "open, stat o fstat fallita");
    else {
        mostra(cosa, sizeof(cosa), "dopo chmod 0640", &t);
        esito("fstat", s.st_mode != 0100640 || t.st_mode != s.st_mode || t.st_ino != s.st_ino ||
                       t.st_uid != s.st_uid || t.st_nlink != s.st_nlink ||
                       t.st_mtime != s.st_mtime || t.st_ctime != s.st_ctime, cosa);
    }
    if (fd >= 0) close(fd);

    /* --- FAT: no owners, the date of the write --- */
    snprintf(f, sizeof(f), "%s/nuovo.txt", argv[2]);
    fd  = open(f, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    ora = (long long)time(NULL);
    if (fd < 0 || write(fd, "abc", 3) != 3) esito("fat", 1, "scrittura fallita");
    if (fd >= 0) close(fd);
    if (stat(f, &s) != 0) esito("fat", 1, "stat nuovo.txt fallita");
    else {
        mostra(cosa, sizeof(cosa), "nuovo.txt", &s);
        esito("fat", s.st_mode != 0100644 || s.st_nlink != 1 || s.st_uid != 0 || s.st_size != 3 ||
                     s.st_mtime < ora - 120 || s.st_mtime > ora + 120 || s.st_atime != s.st_mtime, cosa);
    }
    if (stat(argv[2], &s) != 0) esito("fat-radice", 1, "stat della radice fallita");
    else {
        mostra(cosa, sizeof(cosa), argv[2], &s);
        esito("fat-radice", s.st_mode != 040755 || s.st_nlink != 1, cosa);
    }

    /* --- the CD: read-only, the date xorriso wrote --- */
    snprintf(f, sizeof(f), "%s/tempo.txt", argv[3]);
    if (stat(f, &s) != 0) esito("cd", 1, "stat tempo.txt fallita");
    else {
        mostra(cosa, sizeof(cosa), "tempo.txt", &s);
        esito("cd", s.st_mode != 0100555 || s.st_nlink != 1 || s.st_mtime != 1000000000, cosa);
    }

    /* --- a device, and the old call --- */
    if (stat("/dev/null", &s) != 0) esito("dispositivo", 1, "stat /dev/null fallita");
    else {
        mostra(cosa, sizeof(cosa), "/dev/null", &s);
        esito("dispositivo", !S_ISCHR(s.st_mode) || s.st_nlink != 1, cosa);
    }
    {
        int r = stat_vecchia(argv[1]);

        snprintf(cosa, sizeof(cosa), "la syscall 106 risponde %d (ENOSYS = -38)", r);
        esito("vecchia", r != -38, cosa);
    }

    printf("STAT fine %d\n", errori);
    return errori != 0;
}

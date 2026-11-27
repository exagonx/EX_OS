/* =============================================================================
 * tools/prove/tempiprova.c - the dates of a file (kernel 0.373, SYS_TEMPI)
 *
 *     tempiprova <directory>
 *
 * In <directory> makes the file tempi.txt and sets its dates every way the
 * libc has, reading them back with stat after each:
 *   TEMPI fisso a=<atime> m=<mtime>      utimensat 1000000000 / 1234567890
 *   TEMPI omit a=... m=...               UTIME_OMIT on atime, mtime 1300000000
 *   TEMPI utime a=... m=...              utime 1100000000 / 1200000000
 *   TEMPI utimes a=... m=...             utimes 1400000000 / 1500000000
 *   TEMPI futimens a=... m=... ora=<t>   futimens(fd, NULL): now
 *   TEMPI dir m=...                      utimensat on <directory>/sotto
 *   TEMPI einval <0|1>                   tv_nsec 2e9 is refused (EINVAL)
 * Last it leaves tempi.txt at 1234567890 for the host to read on the disk.
 * tools/prova_tempi.sh compares.
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/time.h>

static char f[512];

static void mostra(const char *nome)
{
    struct stat st;

    if (stat(f, &st) != 0) { printf("TEMPI %s STAT-ERRORE\n", nome); return; }
    printf("TEMPI %s a=%lld m=%lld\n", nome, (long long)st.st_atime, (long long)st.st_mtime);
}

int main(int argc, char **argv)
{
    struct timespec ts[2];
    struct utimbuf  ub;
    struct timeval  tv[2];
    struct stat     st;
    char            d[512];
    int             fd, r;

    if (argc < 2) { printf("uso: tempiprova <directory>\n"); return 2; }
    snprintf(f, sizeof(f), "%s/tempi.txt", argv[1]);
    fd = open(f, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { printf("TEMPI CREA-ERRORE %d\n", errno); return 1; }
    write(fd, "date\n", 5);
    close(fd);

    ts[0].tv_sec = 1000000000; ts[0].tv_nsec = 0;
    ts[1].tv_sec = 1234567890; ts[1].tv_nsec = 0;
    r = utimensat(AT_FDCWD, f, ts, 0);
    if (r != 0) printf("TEMPI fisso ERRORE %d\n", errno); else mostra("fisso");

    ts[0].tv_nsec = UTIME_OMIT;
    ts[1].tv_sec = 1300000000;
    r = utimensat(AT_FDCWD, f, ts, 0);
    if (r != 0) printf("TEMPI omit ERRORE %d\n", errno); else mostra("omit");

    ub.actime = 1100000000; ub.modtime = 1200000000;
    if (utime(f, &ub) != 0) printf("TEMPI utime ERRORE %d\n", errno); else mostra("utime");

    tv[0].tv_sec = 1400000000; tv[0].tv_usec = 0;
    tv[1].tv_sec = 1500000000; tv[1].tv_usec = 500000;
    if (utimes(f, tv) != 0) printf("TEMPI utimes ERRORE %d\n", errno); else mostra("utimes");

    fd = open(f, O_RDWR);
    r = fd >= 0 ? futimens(fd, NULL) : -1;
    if (fd >= 0) close(fd);
    if (r != 0) printf("TEMPI futimens ERRORE %d\n", errno);
    else if (stat(f, &st) == 0)
        printf("TEMPI futimens a=%lld m=%lld ora=%lld\n", (long long)st.st_atime,
               (long long)st.st_mtime, (long long)time(NULL));

    snprintf(d, sizeof(d), "%s/sotto", argv[1]);
    mkdir(d, 0755);
    ts[0].tv_sec = 1111111111; ts[0].tv_nsec = 0;
    ts[1].tv_sec = 1222222222; ts[1].tv_nsec = 0;
    if (utimensat(AT_FDCWD, d, ts, 0) != 0) printf("TEMPI dir ERRORE %d\n", errno);
    else if (stat(d, &st) == 0) printf("TEMPI dir m=%lld\n", (long long)st.st_mtime);

    ts[0].tv_nsec = 2000000000l;
    r = utimensat(AT_FDCWD, f, ts, 0);
    printf("TEMPI einval %d\n", r != 0 && errno == EINVAL);

    ts[0].tv_sec = 1000000000; ts[0].tv_nsec = 0;
    ts[1].tv_sec = 1234567890; ts[1].tv_nsec = 0;
    utimensat(AT_FDCWD, f, ts, 0);
    sync();
    printf("TEMPI FINE\n");
    return 0;
}

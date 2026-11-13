/* tools/prove/riuso.c DIR [2]: blocks freed and reused at once (prova_giornale_tempo.sh)
 *
 * /vecchio is written and synced, then unlinked, and new files are written at
 * once: an unlink must be on the disk before its blocks can take other data.
 * With "2" (case D, ext4.mod 0.006, analisi_kernel voce 74): /vecchio2 synced
 * and unlinked, /nuovo2 written with no fsync and left open (a close
 * commits), and the machine is stopped at once, before the commit. The unlink may be lost - /vecchio2 back - but its
 * blocks must not hold /nuovo2's data: they were held until the commit. */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static char g_b[1024 * 1024];

/* Case D wants 256 KB: it must end well inside the 5 s of a transaction. */
static int g_n = (int)sizeof(g_b);

static void scrivi(const char *dir, const char *nome, int c, int sinc)
{
    char p[128];
    int  fd;

    snprintf(p, sizeof(p), "%s/%s", dir, nome);
    memset(g_b, c, sizeof(g_b));
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write(fd, g_b, (size_t)g_n) != g_n) puts("RIUSO scrittura");
    if (sinc) fsync(fd);
    close(fd);
}

int main(int argc, char **argv)
{
    char p[128];
    int  fd;

    if (argc > 2 && strcmp(argv[2], "2") == 0) {
        g_n = 256 * 1024;
        scrivi(argv[1], "vecchio2", 'V', 1);
        snprintf(p, sizeof(p), "%s/vecchio2", argv[1]);
        if (unlink(p) != 0) puts("RIUSO unlink");
        /* ! LEFT OPEN: a close commits, and the unlink would be on the
         * disk before the stop - the case would test nothing. */
        snprintf(p, sizeof(p), "%s/nuovo2", argv[1]);
        memset(g_b, 'N', sizeof(g_b));
        fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0 || write(fd, g_b, (size_t)g_n) != g_n) puts("RIUSO scrittura");
        puts("RIUSO2 FATTO");
        fflush(stdout);
        sleep(60);          /* the machine is stopped while we wait */
        close(fd);
        return 0;
    }
    scrivi(argv[1], "vecchio", 'V', 1);
    snprintf(p, sizeof(p), "%s/vecchio", argv[1]);
    if (unlink(p) != 0) puts("RIUSO unlink");
    scrivi(argv[1], "nuovo", 'N', 0);
    snprintf(p, sizeof(p), "%s/sincrono", argv[1]);
    fd = open(p, O_WRONLY | O_CREAT, 0644);
    if (fd < 0 || write(fd, "dentro", 6) != 6 || fsync(fd) != 0) puts("RIUSO fsync");
    close(fd);
    puts("RIUSO FATTO");
    return 0;
}

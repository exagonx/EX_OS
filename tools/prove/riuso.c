/* tools/prove/riuso.c DIR: blocks freed and reused at once (prova_giornale_tempo.sh)
 *
 * /vecchio is written and synced, then unlinked, and new files are written at
 * once: an unlink must be on the disk before its blocks can take other data. */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static char g_b[1024 * 1024];

static void scrivi(const char *dir, const char *nome, int c, int sinc)
{
    char p[128];
    int  fd;

    snprintf(p, sizeof(p), "%s/%s", dir, nome);
    memset(g_b, c, sizeof(g_b));
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write(fd, g_b, sizeof(g_b)) != (int)sizeof(g_b)) puts("RIUSO scrittura");
    if (sinc) fsync(fd);
    close(fd);
}

int main(int argc, char **argv)
{
    char p[128];
    int  fd;

    (void)argc;
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

/* tools/prove/isoletture.c <cd dir>: reading files of an ISO 9660 CD in
 * pieces (analisi_kernel voce 75)
 *
 * tools/prova_iso_letture.sh writes the CD on Linux: grande.bin (8 MB plus
 * 777 bytes), whose byte i is atteso(i), small files of 1, 2047, 2048 and
 * 2049 bytes made the same way, and a/b/c/d/e/profondo.txt.
 * Lines: "ISOL <name> <0|1> ..." (0 = good), last "ISOL fine <errors>". */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

unsigned int uptime_ms(void);

static unsigned char atteso(unsigned long i)
{
    unsigned int x = (unsigned int)i * 2654435761u;     /* 32 bit on both */

    return (unsigned char)((x >> 13) ^ ((unsigned int)i >> 11));
}

static unsigned char g_buf[300 * 1024 + 64];
static int           errori;

static void esito(const char *nome, int male, const char *cosa)
{
    printf("ISOL %s %d %s\n", nome, male, cosa);
    errori += male;
}

/* the bytes at `pos`, `n` of them, are the ones of the file */
static long sbagliati(const unsigned char *p, unsigned long pos, long n)
{
    long k, s = 0;

    for (k = 0; k < n; k++) if (p[k] != atteso(pos + (unsigned long)k)) s++;
    return s;
}

static void in_sequenza(const char *f, unsigned long dim)
{
    static const long pezzi[] = { 1, 7, 2048, 4096, 131072, 300001, 2047, 65536, 2049, 262144 };
    unsigned long pos = 0;
    long          male = 0, i = 0;
    char          cosa[160];
    int           fd = open(f, O_RDONLY);

    if (fd < 0) { esito("sequenza", 1, "open fallita"); return; }
    for (;;) {
        long n = read(fd, g_buf, (size_t)pezzi[i++ % 10]);

        if (n < 0) { male++; break; }
        if (n == 0) break;
        male += sbagliati(g_buf, pos, n);
        pos  += (unsigned long)n;
    }
    close(fd);
    snprintf(cosa, sizeof(cosa), "letti %lu su %lu, %ld byte sbagliati", pos, dim, male);
    esito("sequenza", male != 0 || pos != dim, cosa);
}

static void a_salti(const char *f, unsigned long dim)
{
    unsigned long seme = 12345, pos;
    long          male = 0, corti = 0, k;
    char          cosa[160];
    int           fd = open(f, O_RDONLY);

    if (fd < 0) { esito("salti", 1, "open fallita"); return; }
    for (k = 0; k < 1500; k++) {
        long n, voluti, attesi;

        seme   = seme * 1103515245ul + 12345ul;
        pos    = (seme >> 3) % (dim + 100);           /* also past the end */
        seme   = seme * 1103515245ul + 12345ul;
        voluti = (long)((seme >> 5) % (k & 1 ? 9000 : 300000)) + 1;
        attesi = pos >= dim ? 0 : (long)(dim - pos < (unsigned long)voluti ? dim - pos : (unsigned long)voluti);
        if (lseek(fd, (off_t)pos, SEEK_SET) != (off_t)pos) { male++; continue; }
        n = read(fd, g_buf, (size_t)voluti);
        if (n != attesi) { corti++; continue; }
        male += sbagliati(g_buf, pos, n);
    }
    close(fd);
    snprintf(cosa, sizeof(cosa), "1500 letture, %ld byte sbagliati, %ld di misura sbagliata", male, corti);
    esito("salti", male != 0 || corti != 0, cosa);
}

static void piccolo(const char *dir, const char *nome, long dim)
{
    char     f[256], cosa[160];
    long     n;
    int      fd;
    struct stat st;

    snprintf(f, sizeof(f), "%s/%s", dir, nome);
    if (stat(f, &st) != 0 || (long)st.st_size != dim) {
        snprintf(cosa, sizeof(cosa), "%s: stat %ld invece di %ld", nome, stat(f, &st) == 0 ? (long)st.st_size : -1L, dim);
        esito("piccolo", 1, cosa);
        return;
    }
    fd = open(f, O_RDONLY);
    n  = fd >= 0 ? read(fd, g_buf, sizeof(g_buf)) : -1;
    if (fd >= 0) close(fd);
    snprintf(cosa, sizeof(cosa), "%s: letti %ld su %ld", nome, n, dim);
    esito("piccolo", n != dim || sbagliati(g_buf, 0, n) != 0, cosa);
}

int main(int argc, char **argv)
{
    char          f[256], cosa[160];
    struct stat   st;
    unsigned long dim;
    unsigned int  t0, giri;
    int           fd;

    if (argc < 2) { puts("ISOL fine 1"); return 1; }
    snprintf(f, sizeof(f), "%s/grande.bin", argv[1]);
    if (stat(f, &st) != 0) { puts("ISOL grande.bin manca"); puts("ISOL fine 1"); return 1; }
    dim = (unsigned long)st.st_size;

    in_sequenza(f, dim);
    a_salti(f, dim);
    piccolo(argv[1], "uno.bin", 1);
    piccolo(argv[1], "b2047.bin", 2047);
    piccolo(argv[1], "b2048.bin", 2048);
    piccolo(argv[1], "b2049.bin", 2049);

    snprintf(f, sizeof(f), "%s/a/b/c/d/e/profondo.txt", argv[1]);
    fd = open(f, O_RDONLY);
    {
        long n = fd >= 0 ? read(fd, g_buf, 64) : -1;

        if (fd >= 0) close(fd);
        esito("profondo", n != 8 || memcmp(g_buf, "profondo", 8) != 0, "a/b/c/d/e/profondo.txt");
    }
    snprintf(f, sizeof(f), "%s/a/b/c/d/e/manca.txt", argv[1]);
    esito("manca", open(f, O_RDONLY) >= 0, "un nome che non c'e' non si apre");

    /* how fast: the whole file in 64 KB pieces, three times */
    snprintf(f, sizeof(f), "%s/grande.bin", argv[1]);
    t0 = uptime_ms();
    for (giri = 0; giri < 3; giri++) {
        fd = open(f, O_RDONLY);
        if (fd < 0) break;
        while (read(fd, g_buf, 65536) > 0) { }
        close(fd);
    }
    {
        unsigned int ms = uptime_ms() - t0;

        snprintf(cosa, sizeof(cosa), "3 x %lu byte a pezzi da 64 KB in %u ms, %lu KB/s", dim, ms,
                 ms ? (unsigned long)(3ul * (dim / 1024ul) * 1000ul / ms) : 0ul);
        esito("velocita", 0, cosa);
    }
    printf("ISOL fine %d\n", errori);
    return errori != 0;
}

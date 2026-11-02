/* tools/prove/cachepag.c DIR RAM_MB: the page cache never lies (tools/prova_cache.sh)
 *
 * A file of 8 MB and its model in memory: thousands of reads and of writes at
 * unaligned places, each read compared with the model, then the whole file
 * read twice, then memory filled until the cache must give pages back, and the
 * file compared again. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define DIM (8u * 1024u * 1024u)

static unsigned int g_s = 12345;

static unsigned int caso(void)
{
    g_s = g_s * 1103515245u + 12345u;
    return (g_s >> 8) & 0xFFFFFFu;
}

static unsigned char *g_m, *g_b;

static int confronta(int fd, const char *quando)
{
    unsigned int fatti = 0;

    lseek(fd, 0, SEEK_SET);
    while (fatti < DIM) {
        unsigned int q = DIM - fatti > 65536u ? 65536u : DIM - fatti;

        if (read(fd, g_b, q) != (int)q || memcmp(g_b, g_m + fatti, q) != 0) {
            printf("CACHEPAG DIVERSO a %u (%s)\n", fatti, quando);
            return 1;
        }
        fatti += q;
    }
    return 0;
}

int main(int argc, char **argv)
{
    char nome[128];
    unsigned int i, k, mb = argc > 2 ? (unsigned)atoi(argv[2]) : 0;
    int fd, errori = 0;

    g_m = malloc(DIM);
    g_b = malloc(DIM);
    if (!g_m || !g_b) { puts("CACHEPAG niente memoria"); return 1; }
    for (i = 0; i < DIM; i++) g_m[i] = (unsigned char)caso();
    snprintf(nome, sizeof(nome), "%s/grande", argv[1]);
    fd = open(nome, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write(fd, g_m, DIM) != (int)DIM) { puts("CACHEPAG scrittura"); return 1; }
    fsync(fd);
    errori += confronta(fd, "prima lettura");

    for (i = 0; i < 6000 && !errori; i++) {
        unsigned int off = (caso() * 7u) % DIM, n = 1 + caso() % 9000u;

        if (off + n > DIM) n = DIM - off;
        lseek(fd, (off_t)off, SEEK_SET);
        if (caso() & 1u) {
            for (k = 0; k < n; k++) g_b[k] = (unsigned char)caso();
            memcpy(g_m + off, g_b, n);
            if (write(fd, g_b, n) != (int)n) { puts("CACHEPAG write"); errori++; }
        } else {
            if (read(fd, g_b, n) != (int)n || memcmp(g_b, g_m + off, n) != 0) {
                printf("CACHEPAG DIVERSO giro %u off %u n %u\n", i, off, n);
                errori++;
            }
        }
        if (i % 500u == 499u) fsync(fd);
    }
    errori += confronta(fd, "dopo i giri");
    errori += confronta(fd, "rilettura");
    close(fd);

    if (mb > 0) {
        unsigned int fatti = 0;
        char **v = calloc(mb + 1u, sizeof(char *));

        for (i = 0; i < mb; i++) {
            v[i] = malloc(1024u * 1024u);
            if (v[i] == NULL) break;
            memset(v[i], (int)i, 1024u * 1024u);
            fatti++;
        }
        printf("CACHEPAG toccati %u MB\n", fatti);
        for (i = 0; i < fatti; i++) free(v[i]);
    }
    fd = open(nome, O_RDONLY);
    errori += confronta(fd, "dopo la memoria");
    close(fd);

    /* The same path, another file: nothing of the old one may be remembered
     * (the paths kept by risolvi in ext2.c). */
    {
        char altro[128];
        char t[16];
        int  r;

        snprintf(altro, sizeof(altro), "%s/altro", argv[1]);
        fd = open(altro, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0 || write(fd, "corto", 5) != 5) errori++;
        close(fd);
        if (rename(altro, nome) != 0) errori++;
        fd = open(nome, O_RDONLY);
        r = fd >= 0 ? (int)read(fd, t, sizeof(t)) : -1;
        if (r != 5 || memcmp(t, "corto", 5) != 0) {
            printf("CACHEPAG rename: letti %d\n", r);
            errori++;
        }
        close(fd);
        if (unlink(nome) != 0) errori++;
        if (open(nome, O_RDONLY) >= 0) { puts("CACHEPAG unlink: c'e' ancora"); errori++; }
        fd = open(nome, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0 || write(fd, g_m, DIM) != (int)DIM) errori++;
        errori += confronta(fd, "rifatto");
        close(fd);
    }
    printf("CACHEPAG %s\n", errori ? "SBAGLIATO" : "A POSTO");
    return errori != 0;
}

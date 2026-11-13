/* tools/prove/nomiprova.c DIR [fat]: names and sizes seen right after they change
 * (kernel 0.333, analisi_kernel voce 56)
 *
 * The ext2 path cache now forgets a name only in create, mkdir, rmdir,
 * unlink, rename and mount, and an inode written makes a hit read the inode
 * again. Here every change is followed at once by a look through the old and
 * the new name, so a stale entry shows: a renamed file still found under its
 * old name, a recreated name giving the old file, a size or a truncate not
 * seen. Then a 3 MB file in 4 KB pieces, read back forward, backward and
 * jumping (voce 75: FAT goes on from the last cluster reached). Each NO line
 * says what; the last line is "nomiprova: N NO". */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static int  g_no = 0;
static char g_b[8192];

static void no(const char *cosa, int giro)
{
    printf("NO %s (giro %d)\n", cosa, giro);
    g_no++;
}

static int scrivi(const char *p, int c, int n)
{
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) return -1;
    memset(g_b, c, (size_t)n);
    if (write(fd, g_b, (size_t)n) != n) { close(fd); return -1; }
    close(fd);
    return 0;
}

/* The first byte of p and its size, or -1 if it cannot be opened. */
static int primo(const char *p, long *dim)
{
    struct stat st;
    char        c;
    int         fd = open(p, O_RDONLY);

    if (fd < 0) return -1;
    if (read(fd, &c, 1) != 1) c = 0;
    if (dim != NULL) *dim = (fstat(fd, &st) == 0) ? (long)st.st_size : -1;
    close(fd);
    return (unsigned char)c;
}

static int esiste(const char *p)
{
    struct stat st;

    return stat(p, &st) == 0;
}

int main(int argc, char **argv)
{
    char a[160], b[160], d[160], e[160], df[160], ef[160], z[160];
    long dim;
    int  giro, fd, i, fat;

    if (argc < 2) { puts("uso: nomiprova DIR [fat]"); return 2; }
    fat = argc > 2 && strcmp(argv[2], "fat") == 0;
    snprintf(a, sizeof(a), "%s/np_a", argv[1]);
    snprintf(b, sizeof(b), "%s/np_b", argv[1]);
    snprintf(d, sizeof(d), "%s/np_d", argv[1]);
    snprintf(e, sizeof(e), "%s/np_e", argv[1]);
    snprintf(df, sizeof(df), "%s/np_d/f", argv[1]);
    snprintf(ef, sizeof(ef), "%s/np_e/f", argv[1]);

    for (giro = 0; giro < 50; giro++) {
        /* rename: the old name must be gone, the new one the same file */
        if (scrivi(a, 'A', 10) != 0) { no("scrivi a", giro); break; }
        if (primo(a, NULL) != 'A') no("a appena scritto", giro);
        if (rename(a, b) != 0) no("rename a b", giro);
        if (esiste(a)) no("a esiste ancora dopo il rename", giro);
        if (primo(b, NULL) != 'A') no("b non e' il vecchio a", giro);

        /* a name made again: a new file, not the one renamed away */
        if (scrivi(a, 'B', 20) != 0) no("scrivi a di nuovo", giro);
        if (primo(a, &dim) != 'B' || dim != 20) no("a nuovo", giro);
        if (primo(b, &dim) != 'A' || dim != 10) no("b toccato dal nuovo a", giro);

        /* unlink and the same name again, likely on the same inode */
        if (unlink(b) != 0) no("unlink b", giro);
        if (esiste(b)) no("b esiste dopo unlink", giro);
        if (scrivi(b, 'C', 30) != 0) no("scrivi b di nuovo", giro);
        if (primo(b, &dim) != 'C' || dim != 30) no("b ricreato", giro);

        /* a rename over an existing name */
        if (rename(b, a) != 0) no("rename b sopra a", giro);
        if (esiste(b)) no("b esiste dopo il rename sopra a", giro);
        if (primo(a, &dim) != 'C' || dim != 30) no("a dopo il rename sopra", giro);

        /* the inode changes, the name does not: size seen at once */
        fd = open(a, O_WRONLY | O_APPEND);
        if (fd < 0) no("apri a in coda", giro);
        else {
            memset(g_b, 'D', 5000);
            if (write(fd, g_b, 5000) != 5000) no("scrivi in coda", giro);
            close(fd);
        }
        if (primo(a, &dim) != 'C' || dim != 5030) no("dimensione dopo la coda", giro);
        if (truncate(a, 3) != 0) no("truncate", giro);
        if (primo(a, &dim) != 'C' || dim != 3) no("dimensione dopo truncate", giro);
        /* ! The mode is not looked at: the libc's stat() makes it up
         * (S_IFREG | 0644, uid 0, lib/libc.c), 0.332 as well - in
         * in_lavorazione. The chmod writes the inode, which is the point:
         * the size and the content must still be right after it. */
        if (!fat && chmod(a, 0600) != 0) no("chmod", giro);
        if (primo(a, &dim) != 'C' || dim != 3) no("dopo chmod", giro);

        /* directories: a file seen through the renamed directory only */
        if (mkdir(d, 0755) != 0) no("mkdir d", giro);
        if (scrivi(df, 'F', 40) != 0) no("scrivi d/f", giro);
        if (primo(df, NULL) != 'F') no("d/f", giro);
        if (rename(d, e) != 0) no("rename d e", giro);
        if (esiste(df)) no("d/f esiste dopo il rename di d", giro);
        if (primo(ef, &dim) != 'F' || dim != 40) no("e/f", giro);
        if (unlink(ef) != 0) no("unlink e/f", giro);
        if (rmdir(e) != 0) no("rmdir e", giro);
        if (esiste(ef) || esiste(e)) no("e o e/f dopo rmdir", giro);

        /* more names than the cache holds (16), then back to the first */
        for (i = 0; i < 24; i++) {
            snprintf(z, sizeof(z), "%s/np_z%d", argv[1], i);
            if (scrivi(z, 'a' + i, i + 1) != 0) no("scrivi z", giro);
        }
        for (i = 0; i < 24; i++) {
            snprintf(z, sizeof(z), "%s/np_z%d", argv[1], i);
            if (primo(z, &dim) != 'a' + i || dim != i + 1) no("z riletto", giro);
            if (unlink(z) != 0) no("unlink z", giro);
            if (esiste(z)) no("z esiste dopo unlink", giro);
        }
        if (unlink(a) != 0) no("unlink a", giro);
    }

    /* A FILE IN PIECES (voce 75, FAT): 3 MB written 4 KB at a time, read
     * back forward, backward and jumping, every byte a function of its
     * offset - a read that goes on from the wrong cluster shows at once. */
    {
        char     g[160];
        unsigned k, j, pezzi = 768, ord[3] = { 0, 1, 2 };
        int      fg;

        snprintf(g, sizeof(g), "%s/np_gr", argv[1]);
        fg = open(g, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fg < 0) no("apri il grande", 0);
        for (k = 0; fg >= 0 && k < pezzi; k++) {
            for (j = 0; j < 4096; j++) g_b[j] = (char)((k * 4096u + j) * 7u + (k >> 3));
            if (write(fg, g_b, 4096) != 4096) { no("scrivi il grande", (int)k); break; }
        }
        for (i = 0; fg >= 0 && i < 3; i++) {
            for (k = 0; k < pezzi; k++) {
                unsigned q = ord[i] == 0 ? k : ord[i] == 1 ? pezzi - 1u - k
                                                      : (k * 389u) % pezzi;
                if (lseek(fg, (off_t)q * 4096, SEEK_SET) < 0 ||
                    read(fg, g_b, 4096) != 4096) { no("leggi il grande", (int)q); break; }
                for (j = 0; j < 4096; j++)
                    if (g_b[j] != (char)((q * 4096u + j) * 7u + (q >> 3))) break;
                if (j < 4096) { no(i == 0 ? "grande in avanti" : i == 1 ?
                                   "grande all'indietro" : "grande a salti", (int)q); break; }
            }
        }
        if (fg >= 0) close(fg);
        if (unlink(g) != 0) no("unlink il grande", 0);
    }
    printf("nomiprova: %d NO\n", g_no);
    return g_no ? 1 : 0;
}

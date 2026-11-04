/* tools/prove/mappa.c - what the boot sector of a partition really loads (voce 75).
 *     mappa DEV [FILE...]
 * Reads only. Prints the kernel map the boot sector holds (bootinst.c,
 * PATCH_OFF 0x1A0: 'EXHD', stage2 LBA/count, kernel bytes, extents), reads
 * the kernel from those sectors and says, for each FILE, whether it is the
 * same bytes, and how many times "0.310" / "0.311" occur in it.
 * The partition must not be mounted (BLKIO refuses it): run it after
 * `system -unload`, with the FILEs copied to /rdboot first. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int blkread(const char *dev, unsigned int lba, unsigned int n, void *buf);

static unsigned g32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned g16(const unsigned char *p) { return p[0] | p[1] << 8; }

static unsigned conta(const unsigned char *b, unsigned n, const char *t)
{
    unsigned l = strlen(t), c = 0, p;
    for (p = 0; p + l <= n; p++) if (!memcmp(b + p, t, l)) c++;
    return c;
}

int main(int argc, char **argv)
{
    unsigned char s[512], *k;
    unsigned primo, s2, s2n, kb, nx, i, j, pos = 0, tot = 0;
    int a;

    if (argc < 2) { printf("uso: mappa DEV [FILE...]\n"); return 2; }
    if (blkread(argv[1], 0, 1, s) < 0) { printf("settore 0 illeggibile (montato?)\n"); return 1; }
    if (g32(s + 0x1A0) != 0x44485845u) { printf("niente 'EXHD' a 0x1A0\n"); return 1; }
    primo = argc >= 3 && !strncmp(argv[argc - 1], "-primo:", 7) ? strtoul(argv[--argc] + 7, 0, 10) : 2048;
    s2 = g32(s + 0x1A4); s2n = g16(s + 0x1A8); kb = g32(s + 0x1AA); nx = g16(s + 0x1AE);
    printf("stage2 LBA %u x%u, kernel %u byte in %u intervalli (partizione da %u)\n", s2, s2n, kb, nx, primo);
    if (nx == 0 || nx > 12) { printf("numero di intervalli impossibile\n"); return 1; }
    for (i = 0; i < nx; i++) tot += g16(s + 0x1B0 + i * 6 + 4);
    k = malloc(tot * 512u + 512);
    if (!k) { printf("niente memoria\n"); return 1; }
    for (i = 0; i < nx; i++) {
        unsigned l = g32(s + 0x1B0 + i * 6), n = g16(s + 0x1B0 + i * 6 + 4);
        printf("  %u: LBA %u (nella partizione %u) x%u\n", i, l, l - primo, n);
        for (j = 0; j < n; j += 32) {
            unsigned q = n - j < 32 ? n - j : 32;
            if (blkread(argv[1], l - primo + j, q, k + pos) < 0) { printf("lettura fallita a %u\n", l - primo + j); return 1; }
            pos += q * 512u;
        }
    }
    printf("dai settori: \"0.310\" %u volte, \"0.311\" %u volte\n",
           conta(k, kb, "0.310"), conta(k, kb, "0.311"));
    for (a = 2; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        unsigned char *b; long n; unsigned d;
        if (!f) { printf("%s: non si apre\n", argv[a]); continue; }
        fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
        b = malloc((size_t)n + 1);
        if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { printf("%s: non si legge\n", argv[a]); fclose(f); continue; }
        fclose(f);
        for (d = 0; d < kb && d < (unsigned)n && b[d] == k[d]; d++) ;
        if ((unsigned)n == kb && d == kb) printf("%s (%ld byte): UGUALE ai settori\n", argv[a], n);
        else printf("%s (%ld byte): diverso dal byte %u\n", argv[a], n, d);
        free(b);
    }
    return 0;
}

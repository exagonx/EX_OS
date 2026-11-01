/* tools/prove/gruppi.c - who points at a block, on an unmounted ext volume.
 *
 *     gruppi DEV [BLOCCO...]
 *
 * Reads only. Lists every structure of the volume (superblock copies,
 * descriptors, bitmaps, inode tables) and says which ones land on the same
 * block, and which structure owns each BLOCCO given. Written for voce 58/63:
 * chkdsk says a block is "used by inode 0" twice and does not say by whom. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int blkread(const char *dev, unsigned int lba, unsigned int n, void *buf);

static unsigned le32(const unsigned char *p)
{ return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned le16(const unsigned char *p) { return p[0] | p[1] << 8; }

static int potenza(unsigned g, unsigned b)
{
    unsigned x = b;
    while (x < g) x *= b;
    return x == g;
}
static int copia(unsigned g, unsigned compat_ro)
{
    if (g <= 1 || !(compat_ro & 1)) return g <= 1 || !(compat_ro & 1);
    return potenza(g, 3) || potenza(g, 5) || potenza(g, 7);
}

static unsigned char *mappa, *gdt;
static unsigned tot, dimb, dimd, nd, rgdt, bpg, ipg, dimi, ng, primo, cro;
static unsigned *cerca;
static int ncerca, doppi;

static void vede(unsigned b, unsigned g, const char *cosa)
{
    int i;
    if (b >= tot) { printf("gruppo %u: %s a %u FUORI DAL VOLUME\n", g, cosa, b); return; }
    for (i = 0; i < ncerca; i++)
        if (cerca[i] == b) printf("BLOCCO %u: gruppo %u, %s\n", b, g, cosa);
    if (mappa[b >> 3] & (1 << (b & 7))) {
        if (doppi++ < 60) printf("DOPPIO %u: gruppo %u, %s\n", b, g, cosa);
    }
    mappa[b >> 3] |= (unsigned char)(1 << (b & 7));
}

int main(int argc, char **argv)
{
    static unsigned char sb[1024];
    unsigned g, k, sett, desc0, hi, tab_n;
    const char *dev;

    if (argc < 2) { printf("uso: gruppi DEV [BLOCCO...]\n"); return 2; }
    dev = argv[1];
    if (blkread(dev, 2, 2, sb) < 0) { printf("non leggo %s\n", dev); return 1; }
    if (le16(sb + 56) != 0xEF53) { printf("non e' ext\n"); return 1; }
    dimb = 1024u << le32(sb + 24); primo = le32(sb + 20); bpg = le32(sb + 32);
    ipg = le32(sb + 40); tot = le32(sb + 4); cro = le32(sb + 100);
    dimi = le16(sb + 88); dimd = 32; if (le32(sb + 96) & 0x80) dimd = le16(sb + 254);
    rgdt = le16(sb + 206); ng = (tot - primo + bpg - 1) / bpg;
    nd = (ng * dimd + dimb - 1) / dimb; desc0 = dimb == 1024 ? 2 : 1;
    printf("blocchi %u da %u, gruppi %u da %u, inode %u x %u, desc %u x %u in %u, "
           "riserva %u\ncompat %x incompat %x ro %x stato %x flex %u liberi %u\n",
           tot, dimb, ng, bpg, ipg, dimi, ng, dimd, nd, rgdt, le32(sb + 92),
           le32(sb + 96), cro, le16(sb + 58), 1u << sb[372], le32(sb + 12));
    sett = dimb / 512;
    gdt = malloc(nd * dimb); mappa = calloc(tot / 8 + 1, 1);
    ncerca = argc - 2; cerca = malloc(sizeof(unsigned) * (unsigned)(ncerca + 1));
    if (!gdt || !mappa || !cerca) { printf("memoria\n"); return 1; }
    for (k = 0; k < (unsigned)ncerca; k++) cerca[k] = (unsigned)strtoul(argv[k + 2], 0, 10);
    for (k = 0; k < nd; k++) {
        unsigned s = 0;
        while (s < sett) {
            unsigned n = sett - s > 32 ? 32 : sett - s;
            if (blkread(dev, (desc0 + k) * sett + s, n, gdt + k * dimb + s * 512) < 0)
                { printf("non leggo i descrittori\n"); return 1; }
            s += n;
        }
    }
    tab_n = (ipg * dimi + dimb - 1) / dimb;
    for (k = 0; k < nd; k++) vede(desc0 + k, 0, "descrittori");
    for (k = 0; k < rgdt; k++) vede(desc0 + nd + k, 0, "riserva");
    for (g = 0; g < ng; g++) {
        const unsigned char *d = gdt + g * dimd;
        hi = dimd >= 64 ? le32(d + 32) | le32(d + 36) | le32(d + 40) : 0;
        if (hi) printf("gruppo %u: parti alte %x\n", g, hi);
        if (g > 0 && copia(g, cro))
            for (k = 0; k < nd + 1 + rgdt; k++) vede(primo + g * bpg + k, g, "copia");
        vede(le32(d), g, "bitmap blocchi");
        vede(le32(d + 4), g, "bitmap inode");
        for (k = 0; k < tab_n; k++) vede(le32(d + 8) + k, g, "tabella inode");
    }
    for (k = 0; k < (unsigned)ncerca; k++) {
        g = (cerca[k] - primo) / bpg;
        printf("gruppo %u del blocco %u: bitmap %u inode %u tabella %u flag %x copia %d\n",
               g, cerca[k], le32(gdt + g * dimd), le32(gdt + g * dimd + 4),
               le32(gdt + g * dimd + 8), le16(gdt + g * dimd + 18), copia(g, cro));
    }
    printf("FINE: %d blocchi doppi\n", doppi);
    return 0;
}

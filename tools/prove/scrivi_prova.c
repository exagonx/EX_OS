/* Banco di prova dei codificatori d'immagine (lib/eximg/scrivi.c) e del
 * lettore BMP (lib/eximg/bmp.c), sull'host. Lo guida tools/prova_scrivi.sh.
 *
 *   scrivi_prova genera DIR     scrive in DIR le immagini di prova, ognuna in
 *                               tre forme: .png e .bmp (i nostri) e .rgba (i
 *                               pixel attesi, crudi) — e le rilegge con
 *                               eximg_carica, confrontando pixel per pixel
 *   scrivi_prova leggi F OUT    decodifica F con eximg e scrive i pixel in OUT,
 *                               RGBA crudo: e' cosi' che lo script confronta
 *                               il NOSTRO lettore con quello di ImageMagick
 *
 * ! IL CONFRONTO VERO E' CON UN DECODIFICATORE CHE NON E' NOSTRO. Un PNG
 * scritto da noi e riletto da noi puo' essere sbagliato in modo simmetrico —
 * una CRC calcolata male due volte allo stesso modo — e il giro tornerebbe.
 * ImageMagick non condivide i nostri errori. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eximg.h"
#include "scrivi.h"

static int falliti = 0, fatti = 0;

static void ok(const char *t, int cond)
{
    fatti++;
    if (!cond) { printf("  *** FALLITO: %s\n", t); falliti++; }
}

static int su_file(void *chi, const unsigned char *p, unsigned int n)
{
    return fwrite(p, 1, n, (FILE *)chi) == n;
}

static unsigned char *leggi_tutto(const char *nome, unsigned int *n)
{
    FILE *f = fopen(nome, "rb");
    unsigned char *d;
    long l;

    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    l = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = malloc((size_t)l + 1);
    if (fread(d, 1, (size_t)l, f) != (size_t)l) { fclose(f); free(d); return 0; }
    fclose(f);
    *n = (unsigned int)l;
    return d;
}

static void scrivi_rgba(const char *nome, const EximgBitmap *bm)
{
    FILE *f = fopen(nome, "wb");
    unsigned int i;

    for (i = 0; i < bm->larghezza * bm->altezza; i++) {
        unsigned int v = bm->px[i];
        unsigned char q[4] = { (unsigned char)(v >> 16), (unsigned char)(v >> 8),
                               (unsigned char)v, (unsigned char)(v >> 24) };
        fwrite(q, 1, 4, f);
    }
    fclose(f);
}

/* Le immagini di prova. Le larghezze dispari mettono alla prova il
 * riempimento delle righe del BMP (a multipli di 4 byte); il rumore mette
 * alla prova i filtri del PNG, che sui colori piatti sceglierebbero sempre
 * lo stesso. */
static void riempi(EximgBitmap *bm, int tipo)
{
    unsigned int x, y, s = 12345;

    for (y = 0; y < bm->altezza; y++)
        for (x = 0; x < bm->larghezza; x++) {
            unsigned int v;
            s = s * 1103515245u + 12345u;
            switch (tipo) {
            case 0:  v = 0xFF000000u | ((x * 255 / bm->larghezza) << 16) | ((y * 255 / bm->altezza) << 8) | 0x40; break;
            case 1:  v = 0xFF000000u | (s >> 8); break;
            case 2:  v = ((x * 255 / bm->larghezza) << 24) | 0x00336699u; break;     /* con l'alfa */
            default: v = ((x / 8 + y / 8) & 1) ? 0xFFFFFFFFu : 0xFF000000u; break;
            }
            bm->px[y * bm->larghezza + x] = v;
        }
}

static int uguali(const EximgBitmap *a, const EximgBitmap *b, int con_alfa)
{
    unsigned int i, m = con_alfa ? 0xFFFFFFFFu : 0x00FFFFFFu;

    if (a->larghezza != b->larghezza || a->altezza != b->altezza) return 0;
    for (i = 0; i < a->larghezza * a->altezza; i++)
        if ((a->px[i] & m) != (b->px[i] & m)) return 0;
    return 1;
}

static void genera(const char *dir)
{
    static const struct { unsigned int w, h; int tipo; } casi[] = {
        { 1, 1, 0 }, { 3, 5, 0 }, { 5, 3, 1 }, { 7, 7, 3 },
        { 64, 48, 0 }, { 301, 199, 1 }, { 120, 80, 2 }, { 640, 400, 3 },
    };
    unsigned int k;

    for (k = 0; k < sizeof(casi) / sizeof(casi[0]); k++) {
        EximgBitmap bm, rl;
        char nome[512], t[128];
        unsigned char *d;
        unsigned int n;
        FILE *f;
        int e;

        bm.larghezza = casi[k].w;
        bm.altezza = casi[k].h;
        bm.px = malloc(bm.larghezza * bm.altezza * 4);
        riempi(&bm, casi[k].tipo);

        snprintf(nome, sizeof(nome), "%s/img%u.rgba", dir, k);
        scrivi_rgba(nome, &bm);

        snprintf(nome, sizeof(nome), "%s/img%u.png", dir, k);
        f = fopen(nome, "wb");
        e = eximg_scrivi_png(&bm, su_file, f);
        fclose(f);
        snprintf(t, sizeof(t), "img%u: PNG %ux%u scritto", k, bm.larghezza, bm.altezza);
        ok(t, e);
        d = leggi_tutto(nome, &n);
        e = d && eximg_carica(d, n, &rl);
        /* ! SENZA L'ALFA: il lettore PNG di eximg la butta via apposta ("alfa
         * ignorata" in png.c, e il toolkit ci conta). L'alfa scritta la
         * controlla ImageMagick, nel secondo giro di prova_scrivi.sh. */
        snprintf(t, sizeof(t), "img%u: PNG riletto da eximg uguale (senza alfa)", k);
        ok(t, e && uguali(&bm, &rl, 0));
        if (e) eximg_libera(&rl);
        free(d);

        snprintf(nome, sizeof(nome), "%s/img%u.bmp", dir, k);
        f = fopen(nome, "wb");
        e = eximg_scrivi_bmp(&bm, su_file, f);
        fclose(f);
        snprintf(t, sizeof(t), "img%u: BMP scritto", k);
        ok(t, e);
        d = leggi_tutto(nome, &n);
        e = d && eximg_carica(d, n, &rl);
        snprintf(t, sizeof(t), "img%u: BMP riletto da eximg uguale (senza alfa)", k);
        ok(t, e && uguali(&bm, &rl, 0));
        if (e) eximg_libera(&rl);
        free(d);
        free(bm.px);
    }

    ok("il nome .PNG si riconosce", eximg_nome_png("/disk/a.PNG") && !eximg_nome_png("a.bmp") &&
                                    !eximg_nome_png("png"));
    {
        /* Un BMP troncato si rifiuta, non si legge a meta'. */
        unsigned char *d;
        unsigned int n;
        EximgBitmap rl;
        char nome[512];
        snprintf(nome, sizeof(nome), "%s/img5.bmp", dir);
        d = leggi_tutto(nome, &n);
        ok("BMP troncato rifiutato", d && !eximg_carica(d, n - 100, &rl));
        free(d);
    }
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "genera")) {
        genera(argv[2]);
        printf("scrivi_prova: %d prove, %d fallite\n", fatti, falliti);
        return falliti != 0;
    }
    if (argc == 4 && !strcmp(argv[1], "leggi")) {
        unsigned int n;
        unsigned char *d = leggi_tutto(argv[2], &n);
        EximgBitmap bm;
        if (!d || !eximg_carica(d, n, &bm)) { printf("non letto: %s\n", argv[2]); return 1; }
        scrivi_rgba(argv[3], &bm);
        printf("%ux%u\n", bm.larghezza, bm.altezza);
        return 0;
    }
    /* scrivi_prova anima FILE DIR N: i primi N fotogrammi composti di una GIF
     * animata, DIR/f0.rgba ..., e su stdout il ritardo di ognuno (@NAV-GIF). */
    if (argc == 5 && !strcmp(argv[1], "anima")) {
        unsigned int n, ms, k, tot = (unsigned int)atoi(argv[4]);
        unsigned char *d = leggi_tutto(argv[2], &n);
        EximgAnim *a;
        EximgBitmap v;
        char nome[512];
        if (!d || !eximg_anima_apri(d, n, &a)) { printf("non animata: %s\n", argv[2]); return 1; }
        free(d);                                    /* la libreria ne ha una copia */
        for (k = 0; k < tot; k++) {
            if (!eximg_anima_passo(a, &v, &ms)) { printf("passo %u fallito\n", k); return 1; }
            snprintf(nome, sizeof(nome), "%s/f%u.rgba", argv[3], k);
            scrivi_rgba(nome, &v);
            printf("%u %ux%u %u\n", k, v.larghezza, v.altezza, ms);
        }
        eximg_anima_chiudi(a);
        return 0;
    }
    fprintf(stderr, "uso: scrivi_prova genera DIR | leggi FILE OUT.rgba | anima FILE DIR N\n");
    return 2;
}

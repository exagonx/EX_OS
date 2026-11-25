/* =============================================================================
 * debug/grafica/anim_anteprima.c — i fotogrammi delle animazioni, dal codice
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Gira sull'HOST.  anim_anteprima elenco          i nomi, uno per riga
 *                  anim_anteprima <nome> <dir>    scrive <dir>/f000.ppm ...
 * anim_png.py ne fa anim_<nome>.png (PNG animato) e
 * anim_<nome>_fotogrammi.png (sei fotogrammi in griglia).
 * Le scene usano il tema Cristallo (quello del logo); gli effetti prendono
 * dal tema solo i colori, quindi valgono con qualunque tema.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "extanim.h"

#define W 520
#define H 340

static unsigned int g_sfondo[W * H];    /* la scrivania, disegnata una volta */
static unsigned int g_pix[W * H];       /* il fotogramma                      */
static unsigned int g_fin[W * H];       /* un'immagine fuori schermo          */
static unsigned int g_fin2[W * H];      /* la seconda (anim 11)               */
static const ExTema *M;

static void tela(ExTela *t, unsigned int *p) { ext_tela(t, p, W, H, W); }

static void sfondo(void)
{
    ExTela t;
    tela(&t, g_sfondo);
    ext_scrivania(&t, M, 0, 0, W, H);
}

static void inizio(ExTela *t)
{
    memcpy(g_pix, g_sfondo, sizeof(g_pix));
    tela(t, g_pix);
}

/* una finestra di prova, disegnata in (0,0) di un buffer largo W, sopra la
 * porzione di scrivania che stara' sotto di lei a (FX, FY): gli angoli
 * arrotondati e l'antialias del bordo si fondono col fondo giusto */
#define FW 300
#define FH 190
#define FX ((W - FW) / 2)
#define FY ((H - FH) / 2 - 10)
static void finestra_prova(unsigned int *dove, int pagina)
{
    ExTela t;
    int cx, cy, cw, ch, j;
    static const char *const ling[] = { "Generale", "Aspetto" };

    for (j = 0; j < FH; j++)
        memcpy(dove + j * W, g_sfondo + (FY + j) * W + FX, FW * sizeof(unsigned int));
    tela(&t, dove);
    ext_finestra(&t, M, 0, 0, FW, FH, "Impostazioni", EXT_ATTIVA);
    ext_finestra_client(M, 0, 0, FW, FH, &cx, &cy, &cw, &ch);
    ext_linguette(&t, M, cx + 8, cy + 6, cw - 16, 24, ling, 2, pagina, -1);
    if (pagina == 0) {
        ext_spunta(&t, M, cx + 14, cy + 42, "Avvia con la sessione", EXT_SCELTO);
        ext_spunta(&t, M, cx + 14, cy + 64, "Mostra le notifiche", 0);
        ext_casella(&t, M, cx + 14, cy + 88, cw - 28, 24, "/home/mario", -1, 0);
    } else {
        ext_radio(&t, M, cx + 14, cy + 42, "Tema scuro", EXT_SCELTO);
        ext_radio(&t, M, cx + 14, cy + 64, "Tema chiaro", 0);
        ext_progresso(&t, M, cx + 14, cy + 90, cw - 28, 20, 450);
    }
    ext_pulsante(&t, M, cx + cw - 180, cy + ch - 36, 80, 26, "OK", EXT_PREDEFINITO);
    ext_pulsante(&t, M, cx + cw - 92, cy + ch - 36, 80, 26, "Annulla", 0);
}

static void scrivi(const char *dir, int n)
{
    char p[512];
    FILE *f;
    int i;

    snprintf(p, sizeof(p), "%s/f%03d.ppm", dir, n);
    f = fopen(p, "wb");
    if (!f) { perror(p); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (i = 0; i < W * H; i++) {
        unsigned char c[3] = { (unsigned char)(g_pix[i] >> 16), (unsigned char)(g_pix[i] >> 8),
                               (unsigned char)g_pix[i] };
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}

static void barra_applicazioni(ExTela *t, int *ix, int *iy)
{
    ext_riempi_alfa(t, 0, H - 30, W, 30, 0x00050D20u, 200);
    ext_riempi(t, 0, H - 30, W, 1, M->c.bordo);
    ext_tondo_pieno(t, W / 2 - 14, H - 26, 28, 22, 5, M->c.accento, M->c.barra_b);
    ext_scintilla(t, W / 2, H - 15, 5, 0x00FFFFFFu);
    *ix = W / 2; *iy = H - 15;
}

/* ciascuna scena: n fotogrammi, t = k * 1000 / (n - 1) */
static int fotogramma(const char *nome, int k, int n)
{
    ExTela t;
    int tt = n > 1 ? k * 1000 / (n - 1) : 1000;

    inizio(&t);
    if (!strcmp(nome, "01_dissolvenza")) {
        exa_dissolvenza(&t, FX, FY, g_fin, FW, FH, W, tt, 1);
    } else if (!strcmp(nome, "02_zoom")) {
        exa_zoom(&t, FX, FY, g_fin, FW, FH, W, tt, 1);
    } else if (!strcmp(nome, "03_scivola")) {
        exa_scivola(&t, FX, FY, g_fin, FW, FH, W, tt);
    } else if (!strcmp(nome, "04_genio")) {
        int ix, iy;
        barra_applicazioni(&t, &ix, &iy);
        exa_genio(&t, FX, FY - 20, g_fin, FW, FH, W, ix, iy, tt);
        barra_applicazioni(&t, &ix, &iy);       /* l'icona sopra la finestra che entra */
    } else if (!strcmp(nome, "05_cristallo")) {
        exa_cristallo(&t, M, FX, FY, g_fin, FW, FH, W, tt);
    } else if (!strcmp(nome, "06_onda")) {
        int bx = W / 2 - 110, by = H / 2 - 26;
        ext_pulsante(&t, M, bx, by, 220, 52, "Premi qui", tt < 150 ? EXT_PREMUTO : EXT_SOPRA);
        exa_onda(&t, M, bx, by, 220, 52, bx + 64, by + 34, tt);
    } else if (!strcmp(nome, "07_respiro")) {
        int bx = W / 2 - 70, by = H / 2 - 20;
        exa_respiro(&t, M, bx, by, 140, 40, tt);
        ext_pulsante(&t, M, bx, by, 140, 40, "OK", EXT_PREDEFINITO);
    } else if (!strcmp(nome, "08_tendina")) {
        static const char *const menu[] = { "File", "Modifica", "Vista" };
        static const char *const voci[] = { "Nuovo", "Apri...", "Salva", "-", "Esci" };
        ExTela f;
        int mx = 40, my = 40 + EXT_MENU_H - 1;

        ext_barra_menu(&t, M, 40, 40, W - 80, menu, 3, 0);
        memcpy(g_fin, g_pix, sizeof(g_pix));
        tela(&f, g_fin);
        ext_tendina(&f, M, mx, my, 180, voci, 5, 1);
        /* la tendina sta in g_fin da (mx,my): misura 180 x (4 voci*22 + 9 + 8) */
        exa_tendina(&t, mx - 8, my, g_fin + my * W + mx - 8, 196, 4 * EXT_MENU_H + 9 + 8 + 10, W, tt);
    } else if (!strcmp(nome, "09_selezione")) {
        static const char *const righe[] = { "Documenti", "Immagini", "Musica", "Video",
                                             "Scaricati", "Progetti", "Cestino" };
        int lx = W / 2 - 120, ly = 40, i, ya = ly + 3 + 1 * EXT_RIGA_H, yb = ly + 3 + 5 * EXT_RIGA_H;
        int p = exa_esce(tt), yc = exa_lerp(ya, yb, p);

        ext_lista(&t, M, lx, ly, 240, 7 * EXT_RIGA_H + 8, righe, 0, -1, -1, 0, 0);
        exa_selezione(&t, M, lx + 4, 232, ya + 1, yb + 1, EXT_RIGA_H - 2, tt);
        for (i = 0; i < 7; i++) {
            int ry = ly + 3 + i * EXT_RIGA_H;
            int sopra = ry + EXT_RIGA_H / 2 > yc && ry + EXT_RIGA_H / 2 < yc + EXT_RIGA_H;
            ext_testo(&t, lx + 10, ry + 2, righe[i], sopra ? M->c.accento_testo : M->c.campo_testo);
        }
    } else if (!strcmp(nome, "10_luccichio")) {
        ext_testo(&t, 60, H / 2 - 40, "Scarico l'elenco dei pacchetti...", M->c.testo);
        exa_luccichio(&t, M, 60, H / 2 - 12, W - 120, 22, tt);
    } else if (!strcmp(nome, "11_cambio")) {
        exa_cambio(&t, FX, FY, g_fin, g_fin2, FW, FH, W, 1, tt);
    } else {
        return -1;
    }
    return 0;
}

static const struct { const char *nome; int n; } g_anim[] = {
    { "01_dissolvenza", 14 }, { "02_zoom", 16 }, { "03_scivola", 18 }, { "04_genio", 24 },
    { "05_cristallo", 26 }, { "06_onda", 18 }, { "07_respiro", 30 }, { "08_tendina", 12 },
    { "09_selezione", 12 }, { "10_luccichio", 30 }, { "11_cambio", 14 },
};
#define N_ANIM ((int)(sizeof(g_anim) / sizeof(g_anim[0])))

int main(int argc, char **argv)
{
    int i, k;

    if (argc == 2 && !strcmp(argv[1], "elenco")) {
        for (i = 0; i < N_ANIM; i++) printf("%s\n", g_anim[i].nome);
        return 0;
    }
    if (argc != 3) { fprintf(stderr, "uso: anim_anteprima elenco | <nome> <dir>\n"); return 2; }
    M = extema_per_nome("cristallo");
    sfondo();
    for (i = 0; i < N_ANIM && strcmp(g_anim[i].nome, argv[1]); i++) ;
    if (i == N_ANIM) { fprintf(stderr, "non c'e': %s\n", argv[1]); return 1; }

    /* le immagini fuori schermo: la finestra (in posizione FX,FY dentro il
     * buffer, cosi' gli angoli vedono la scrivania giusta) */
    if (strcmp(argv[1], "08_tendina")) {
        finestra_prova(g_fin2, 1);
        finestra_prova(g_fin, 0);
    }
    for (k = 0; k < g_anim[i].n; k++) {
        if (fotogramma(argv[1], k, g_anim[i].n) < 0) return 1;
        scrivi(argv[2], k);
    }
    printf("%d\n", g_anim[i].n);
    return 0;
}

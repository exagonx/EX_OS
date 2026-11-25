/* =============================================================================
 * debug/grafica/colori/anteprima_pannello.c — come apparira' Impostazioni >
 * Colori in pm. Sull'HOST: i controlli sono disegnati con extema alle stesse
 * coordinate che pm_colori.c (colori_crea) da' al toolkit; l'anteprima, le
 * tinte e il campione sono le funzioni VERE di colori_anteprima.c.
 *
 *     anteprima_pannello <schema.colori> <elemento 0..18> <uscita.ppm>
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "extema.h"
#include "excolori.h"
#include "colori_anteprima.h"

#define IW 640                  /* area client di Impostazioni */
#define IH 500
#define W (IW + 80)
#define H (IH + 90)

static unsigned int g_pix[W * H];

int main(int argc, char **argv)
{
    static ExColori sc;
    static ExTema vivo, vista;
    static char buf[4096];
    static const char *const schede[] = { "Video", "Scrivania", "Sistema", "Applicazioni", "Colori" };
    ExTela t;
    ColZone z, r;
    FILE *f;
    size_t n;
    int fx = 40, fy = 30, cx, cy, cw, ch, e, i, sel;
    char esa[8], num[8];

    if (argc != 4) { fprintf(stderr, "uso: anteprima_pannello <schema.colori> <elemento> <out.ppm>\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    excol_da_tema(&sc, &extema_classico);
    excol_leggi(&sc, buf, (int)n);
    /* pm stesso e' disegnato col tema applicato; l'anteprima con lo schema
     * in modifica: qui sono lo stesso */
    excol_applica(&sc, &extema_classico, &vivo);
    vista = vivo;
    sel = atoi(argv[2]);

    ext_tela(&t, g_pix, W, H, W);
    ext_scrivania(&t, &vivo, 0, 0, W, H);
    {
        int sp = 4;
        ext_finestra(&t, &vivo, fx, fy, IW + 2 * sp, IH + 2 * sp + vivo.f.barra_h,
                     "Impostazioni", EXT_ATTIVA);
        ext_finestra_client(&vivo, fx, fy, IW + 2 * sp, IH + 2 * sp + vivo.f.barra_h,
                            &cx, &cy, &cw, &ch);
    }

    /* le schede, "Colori" attiva */
    ext_linguette(&t, &vivo, cx + 8, cy + 6, IW - 16, 24, schede, 5, 4, -1);

    ext_testo(&t, cx + 12, cy + 42, "Schema:", vivo.c.testo);
    ext_combo(&t, &vivo, cx + 70, cy + 38, 220, 24, sc.schema, 0);
    ext_pulsante(&t, &vivo, cx + 300, cy + 38, 110, 24, "Salva come...", 0);
    ext_pulsante(&t, &vivo, cx + 416, cy + 38, 80, 24, "Elimina", EXT_SPENTO);

    /* l'anteprima viva e le tinte rapide: il codice di pm */
    colori_anteprima(&t, &vista, cx + 12, cy + 70, 300, 210, sel, &z);
    r.n = 0;
    colori_rapidi(&t, cx + 14, cy + 290, &r);

    /* l'elenco degli elementi */
    {
        const char *righe[EXC_N];
        for (e = 0; e < EXC_N; e++) righe[e] = excol_etichetta(e);
        ext_lista(&t, &vivo, cx + 324, cy + 70, IW - 336, 150, righe, EXC_N, sel,
                  -1, sel > 5 ? sel - 5 : 0, 0);
        ext_scorri(&t, &vivo, cx + 324 + IW - 336 - 18, cy + 72, 16, 146, 1,
                   sel > 5 ? sel - 5 : 0, 7, EXC_N, 0);
    }

    /* il colore scelto: campione, casella, chiaro/scuro, R G B */
    ext_riempi(&t, cx + 324, cy + 230, 44, 44, sc.c[sel]);
    ext_rilievo(&t, cx + 323, cy + 229, 46, 46, 0x00808080u, 0x00FFFFFFu);
    excol_esa(sc.c[sel], esa);
    ext_casella(&t, &vivo, cx + 376, cy + 230, 90, 24, esa, -1, 0);
    ext_pulsante(&t, &vivo, cx + 472, cy + 230, 76, 24, "Chiaro", 0);
    ext_pulsante(&t, &vivo, cx + 552, cy + 230, 76, 24, "Scuro", 0);
    for (i = 0; i < 3; i++) {
        unsigned int v = (sc.c[sel] >> (16 - 8 * i)) & 255u;
        static const char *const rgb[3] = { "R", "G", "B" };
        ext_testo(&t, cx + 376, cy + 262 + 22 * i, rgb[i], vivo.c.testo);
        ext_scorri(&t, &vivo, cx + 392, cy + 262 + 22 * i, 196, 16, 0, (int)v, 16, 271, 0);
        snprintf(num, sizeof(num), "%u", v);
        ext_testo(&t, cx + 594, cy + 262 + 22 * i, num, vivo.c.testo);
    }

    ext_testo(&t, cx + 12, cy + 342, excol_spiega(sel), vivo.c.testo);
    {
        int c = excol_contrasto(sc.c[EXC_TESTO], sc.c[EXC_FINESTRA]);
        char s[120];
        snprintf(s, sizeof(s), "Contrasto testo/finestre %d.%d:1 - si legge bene.", c / 100, (c % 100) / 10);
        ext_testo(&t, cx + 12, cy + 362, s, vivo.c.testo_spento);
    }
    ext_spunta(&t, &vivo, cx + 12, cy + 394, "Barra sfumata", sc.sfumata ? EXT_SCELTO : 0);
    ext_spunta(&t, &vivo, cx + 166, cy + 394, "Titolo in grassetto", sc.grassetto ? EXT_SCELTO : 0);
    ext_spunta(&t, &vivo, cx + 364, cy + 394, "Ombra sotto le finestre", sc.ombra ? EXT_SCELTO : 0);
    ext_spunta(&t, &vivo, cx + 12, cy + 418, "Angoli arrotondati", sc.raggio ? EXT_SCELTO : 0);

    ext_pulsante(&t, &vivo, cx + 12, cy + IH - 44, 110, 28, "Predefinito", 0);
    ext_pulsante(&t, &vivo, cx + IW - 284, cy + IH - 44, 152, 28, "Annulla modifiche", 0);
    ext_pulsante(&t, &vivo, cx + IW - 124, cy + IH - 44, 112, 28, "Applica", EXT_PREDEFINITO | EXT_FUOCO);

    f = fopen(argv[3], "wb");
    if (!f) { perror(argv[3]); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (i = 0; i < W * H; i++) {
        unsigned char p[3] = { (unsigned char)(g_pix[i] >> 16), (unsigned char)(g_pix[i] >> 8),
                               (unsigned char)g_pix[i] };
        fwrite(p, 1, 3, f);
    }
    fclose(f);
    return 0;
}

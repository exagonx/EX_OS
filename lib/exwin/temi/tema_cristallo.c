/* =============================================================================
 * lib/exwin/temi/tema_cristallo.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * TEMA 02 — CRISTALLO (dal logo di EX-OS, ex_os_logo/logo.png)
 *
 * La tavolozza e' quella del logo, misurata sul PNG e letta nell'SVG:
 *     notte      #060F26 #0B1737 #13244F   lo spazio dietro al cristallo
 *     cristallo  #5B5CFF #3D3F9B #1A2F7A   le facce viola-blu
 *     bagliore   #3FD8FF #86C8E5 #9FDCFF   le linee ciano e la luce
 *     scintille  #DFFCFF #FFFFFF
 *
 * Finestre blu notte con la barra sfumata come una faccia del cristallo
 * (viola in alto, blu in basso) e un riflesso; fuoco con l'alone ciano;
 * pulsante predefinito viola pieno; linguette a pillola.
 *
 * Lo sfondo della scrivania non e' una sfumatura: e' la scena del logo —
 * cielo stellato, l'orbita ellittica con le sue perle di luce, la
 * piattaforma esagonale con i tre anelli e le tracce di circuito ai lati.
 * E' disegnato una volta sola (cambia solo con la misura dello schermo): il
 * server lo tiene in un buffer e lo copia sotto le finestre.
 *
 * Anteprima: tema_02_cristallo.png
 * ============================================================================= */
#include "extema.h"

static unsigned int mescola_hash(unsigned int a)
{
    a ^= a >> 16; a *= 0x7feb352du; a ^= a >> 15; a *= 0x846ca68bu; a ^= a >> 16;
    return a;
}

/* Tracce di circuito: tre righe che partono dalla piattaforma, piegano a 45
 * gradi e finiscono in un anello, come ai lati del logo. verso = -1 a
 * sinistra, +1 a destra. */
static void tracce(ExTela *t, int x0, int y0, int verso, unsigned int c)
{
    int k;

    for (k = 0; k < 3; k++) {
        int y = y0 + (k - 1) * 30, piega = 50 + k * 14;
        int x1 = x0 + verso * 40, x2 = x1 + verso * piega;
        int y2 = y + (k - 1) * 12, x3 = x2 + verso * 70;

        ext_linea_alfa(t, x0, y, x1, y, 1, c, 200);
        ext_linea_alfa(t, x1, y, x1 + verso * 14, y2, 1, c, 200);
        ext_linea_alfa(t, x1 + verso * 14, y2, x3, y2, 1, c, 200);
        ext_anello(t, x3 + verso * 5, y2, 4, 1, c);
        ext_ombra(t, x3 + verso * 5 - 1, y2 - 1, 3, 3, 1, 6, 90, c, 0);
        (void)x2;
    }
}

static void sfondo_cristallo(ExTela *t, const ExTema *m, int x, int y, int w, int h)
{
    int i, cx = x + w / 2, cy = y + h * 72 / 100;
    unsigned int ciano = 0x003FD8FFu, luce = 0x0086C8E5u;

    /* il cielo: dal blu notte al quasi nero, poi una luce morbida sotto la
     * piattaforma (un'ombra chiara: ext_ombra disegna un alone) */
    ext_sfuma_v(t, x, y, w, h, m->c.desktop_a, m->c.desktop_b);
    ext_ombra(t, cx - w / 6, cy - h / 30, w / 3, h / 15, h / 30, h / 4, 70, 0x001F3771u, 0);

    /* le stelle: un hash della posizione decide dove e quanto brillano */
    for (i = 0; i < w * h / 1800; i++) {
        unsigned int r = mescola_hash((unsigned int)i * 2654435761u);
        int sx = x + (int)(r % (unsigned int)w), sy = y + (int)((r >> 12) % (unsigned int)h);
        int lum = 60 + (int)((r >> 24) % 160);

        ext_punto(t, sx, sy, 0x00DFFCFFu, lum);
        if ((r & 0x3Fu) == 0) ext_scintilla(t, sx, sy, 4, 0x00FFFFFFu);
    }

    /* l'orbita: un'ellisse inclinata (si disegna dritta, schiacciata), piu'
     * luminosa davanti; tre perle sopra */
    ext_ellisse(t, cx, y + h * 34 / 100, w * 26 / 100, h * 9 / 100, 0, 180, 1, luce, 220);
    ext_ellisse(t, cx, y + h * 34 / 100, w * 26 / 100, h * 9 / 100, 180, 360, 1, luce, 70);
    for (i = 0; i < 3; i++) {
        int g = 20 + i * 140;
        int px = cx + (w * 26 / 100) * ext_coseno(g) / 1000;
        int py = y + h * 34 / 100 + (h * 9 / 100) * ext_seno(g) / 1000;
        ext_ombra(t, px - 2, py - 2, 5, 5, 2, 10, 140, 0x00FFFFFFu, 0);
        ext_cerchio(t, px, py, 3, 0x00FFFFFFu);
    }

    /* la piattaforma: esagono pieno scuro, bordo chiaro, tre anelli interni
     * ciano e il pozzo di luce al centro */
    {
        int rx = w * 22 / 100, ry = h * 9 / 100;

        ext_esagono_pieno(t, cx, cy + 6, rx, ry, 0x00050D20u, 230);
        ext_esagono_pieno(t, cx, cy, rx, ry, 0x000F1D44u, 255);
        ext_esagono(t, cx, cy, rx, ry, 2, luce, 220);
        for (i = 1; i <= 3; i++)
            ext_esagono(t, cx, cy, rx * (10 - 2 * i) / 10, ry * (10 - 2 * i) / 10, 1, ciano, 120 + i * 30);
        ext_ombra(t, cx - rx / 4, cy - ry / 4, rx / 2, ry / 2, ry / 4, ry, 120, ciano, 0);
        for (i = 0; i < 3; i++)
            ext_ellisse(t, cx, cy - 10 - i * 14, rx * (24 - i * 3) / 100, ry * (30 - i * 3) / 100,
                        0, 360, 1, ciano, 90 - i * 20);
        tracce(t, cx - rx, cy, -1, ciano);
        tracce(t, cx + rx, cy, 1, ciano);
    }
}

const ExTema extema_cristallo = {
    EXTEMA_ABI, "cristallo", "Cristallo (dal logo)",
    {
        .desktop_a = 0x000F1D44, .desktop_b = 0x00040A19,
        .fondo = 0x00101C3E, .fondo_alt = 0x00172852,
        .testo = 0x00DDEBFF, .testo_spento = 0x005E6F99,
        .campo = 0x000A142E, .campo_testo = 0x00E8F4FF, .campo_spento = 0x00121F42,
        .bordo = 0x002A4A8A, .bordo_sopra = 0x0046C8FF,
        .luce = 0x004A6AB0, .ombra = 0x00050D20, .luce2 = 0x002A4A8A, .ombra2 = 0x000B1737,
        .accento = 0x005B5CFF, .accento_testo = 0x00FFFFFF, .accento_chiaro = 0x00243A7A,
        .fuoco = 0x003FD8FF,
        .puls_a = 0x00253F80, .puls_b = 0x00172B5E,
        .puls_sopra_a = 0x002F52A6, .puls_sopra_b = 0x001D3778,
        .puls_giu_a = 0x000B1737, .puls_giu_b = 0x00142756,
        .puls_testo = 0x00DFFCFF,
        .barra_a = 0x005B5CFF, .barra_b = 0x001A2F7A,
        .barra_ina_a = 0x00243156, .barra_ina_b = 0x00141E3C,
        .titolo = 0x00FFFFFF, .titolo_ina = 0x007F8DB5,
        .telaio = 0x00142756,
        .chiudi = 0x00FF5C7A, .chiudi_sopra = 0x00D0305A,
        .riduci = 0x003FD8FF, .ingrandisci = 0x005B5CFF,
        .scorri_fondo = 0x000B1737, .scorri_cursore = 0x003D5FB0, .scorri_cursore_sopra = 0x0046C8FF,
        .menu_fondo = 0x000E1A3C, .menu_testo = 0x00DDEBFF,
        .menu_sopra = 0x005B5CFF, .menu_sopra_testo = 0x00FFFFFF,
        .ombra_finestra = 0x00000308,
        .suggerimento = 0x00DFFCFF, .suggerimento_testo = 0x000B1737,
        .progresso_a = 0x003FD8FF, .progresso_b = 0x005B5CFF,
    },
    {
        .raggio = 6, .raggio_finestra = 10, .bordo3d = 0, .spessore = 1,
        .ombra_finestra = 20, .ombra_alfa = 170, .ombra_pulsante = 3,
        .sfumato = 1, .lucido = 1, .barra_h = 26,
        .titolo_centrato = 1, .titolo_grassetto = 1,
        .chiudi_stile = EXT_CHIUDI_PIATTO, .spunta_raggio = 4,
        .scorri_largo = 12, .scorri_frecce = 0, .scorri_tondo = 1,
        .linguette = EXT_LING_PILLOLA, .fuoco_stile = EXT_FUOCO_ANELLO,
        .sfondo = EXT_SFONDO_SFUMATO, .maiuscolo = 0, .bagliore = 5,
    },
    0, 0, sfondo_cristallo
};

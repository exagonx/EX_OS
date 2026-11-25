/* =============================================================================
 * debug/grafica/tema_09_ambra.c  ->  lib/exwin/temi/tema_ambra.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * TEMA 09 — AMBRA (terminale a fosfori)
 *
 * Un monitor a fosfori ambra degli anni '80: nero, un solo colore (#FFB000)
 * in tre intensita', nessun angolo tondo, righe di scansione sullo sfondo.
 * Il telaio della finestra non e' un rettangolo: e' la cornice doppia dei
 * programmi in modo testo, e il titolo sta fra le parentesi dentro la riga
 * di sopra (funzione finestra propria, sotto). I pulsanti si invertono
 * (testo nero su ambra) quando sono il predefinito o premuti; il fuoco e' il
 * bordo acceso. Lega con la console di EX-OS: chi passa da ExWin al testo
 * (Alt+F1) non cambia mondo.
 *
 * Anteprima: tema_09_ambra.png
 * ============================================================================= */
#include "extema.h"

static void finestra_ambra(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                           const char *titolo, unsigned int st)
{
    unsigned int c = (st & EXT_ATTIVA) ? m->c.telaio : m->c.testo_spento;
    int bh = m->f.barra_h, k, tw;

    ext_riempi(t, x, y, w, h, m->c.fondo);
    /* la cornice doppia, come i caratteri 0xC9..0xBC del CP437 */
    ext_contorno(t, x + 2, y + 2, w - 4, h - 4, c);
    ext_contorno(t, x + 5, y + 5, w - 10, h - 10, c);
    /* la riga sotto il titolo, che chiude la "barra" */
    ext_riempi(t, x + 5, y + bh, w - 10, 1, c);
    ext_riempi(t, x + 5, y + bh + 3, w - 10, 1, c);
    /* il titolo fra parentesi quadre, al centro, su un buco nella cornice */
    if (titolo) {
        char buf[96];
        /* quanti caratteri del titolo ci stanno: la riga meno i tre segni a
         * destra (3 x 30 px + margine) e altrettanto a sinistra per restare centrato,
         * meno "[ " e " ]" */
        int max = (w - 2 * 112) / 8 - 4, i = 0, j;

        if (max < 1) max = 1;
        if (max > 88) max = 88;
        buf[i++] = '['; buf[i++] = ' ';
        for (j = 0; titolo[j] && j < max; j++)
            buf[i++] = (titolo[j] >= 'a' && titolo[j] <= 'z') ? (char)(titolo[j] - 32) : titolo[j];
        if (titolo[j]) buf[i - 1] = '~';          /* tagliato: lo si dice */
        buf[i++] = ' '; buf[i++] = ']'; buf[i] = 0;
        tw = ext_larg(buf);
        ext_riempi(t, x + (w - tw) / 2 - 4, y + 7, tw + 8, bh - 8, m->c.fondo);
        ext_testo(t, x + (w - tw) / 2, y + (bh - 12) / 2, buf,
                  (st & EXT_ATTIVA) ? m->c.titolo : m->c.titolo_ina);
    }
    /* [_] [o] [X] a destra, testo e non disegni */
    {
        static const char *const segni[3] = { "[_]", "[^]", "[X]" };
        for (k = 0; k < 3; k++)
            ext_testo(t, x + w - 10 - (3 - k) * 30, y + (bh - 12) / 2, segni[k],
                      k == 2 && (st & EXT_SOPRA) ? m->c.chiudi_sopra : c);
    }
    /* il client, dentro la cornice */
    {
        int cx, cy, cw, ch;
        ext_finestra_client(m, x, y, w, h, &cx, &cy, &cw, &ch);
        ext_riempi(t, cx, cy, cw, ch, m->c.fondo);
    }
}

const ExTema extema_ambra = {
    EXTEMA_ABI, "ambra", "Ambra (fosfori)",
    {
        .desktop_a = 0x001E1200, .desktop_b = 0x00050300,
        .fondo = 0x00100A00, .fondo_alt = 0x001C1100,
        .testo = 0x00FFB000, .testo_spento = 0x00805800,
        .campo = 0x00000000, .campo_testo = 0x00FFC840, .campo_spento = 0x00100A00,
        .bordo = 0x00A06E00, .bordo_sopra = 0x00FFB000,
        .luce = 0x00A06E00, .ombra = 0x00402C00, .luce2 = 0x00604000, .ombra2 = 0x00201600,
        .accento = 0x00FFB000, .accento_testo = 0x00000000, .accento_chiaro = 0x00402C00,
        .fuoco = 0x00FFD060,
        .puls_a = 0x00100A00, .puls_b = 0x00100A00,
        .puls_sopra_a = 0x00281A00, .puls_sopra_b = 0x00281A00,
        .puls_giu_a = 0x00FFB000, .puls_giu_b = 0x00FFB000,
        .puls_testo = 0x00FFB000,
        .barra_a = 0x00100A00, .barra_b = 0x00100A00,
        .barra_ina_a = 0x00100A00, .barra_ina_b = 0x00100A00,
        .titolo = 0x00FFD060, .titolo_ina = 0x00805800,
        .telaio = 0x00FFB000,
        .chiudi = 0x00FFB000, .chiudi_sopra = 0x00FFE0A0,
        .riduci = 0x00FFB000, .ingrandisci = 0x00FFB000,
        .scorri_fondo = 0x00000000, .scorri_cursore = 0x00A06E00, .scorri_cursore_sopra = 0x00FFB000,
        .menu_fondo = 0x00000000, .menu_testo = 0x00FFB000,
        .menu_sopra = 0x00FFB000, .menu_sopra_testo = 0x00000000,
        .ombra_finestra = 0x00000000,
        .suggerimento = 0x00FFB000, .suggerimento_testo = 0x00000000,
        .progresso_a = 0x00FFB000, .progresso_b = 0x00FFB000,
    },
    {
        .raggio = 0, .raggio_finestra = 0, .bordo3d = 0, .spessore = 6,
        .ombra_finestra = 0, .ombra_alfa = 0, .ombra_pulsante = 0,
        .sfumato = 0, .lucido = 0, .barra_h = 26,
        .titolo_centrato = 1, .titolo_grassetto = 0,
        .chiudi_stile = EXT_CHIUDI_PIATTO, .spunta_raggio = 0,
        .scorri_largo = 12, .scorri_frecce = 1, .scorri_tondo = 0,
        .linguette = EXT_LING_SOTTOLINEA, .fuoco_stile = EXT_FUOCO_BORDO,
        .sfondo = EXT_SFONDO_SCANSIONE, .maiuscolo = 1, .bagliore = 0,
    },
    0, finestra_ambra, 0
};

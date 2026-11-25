/* =============================================================================
 * debug/grafica/tema_04_circuito.c  ->  lib/exwin/temi/tema_circuito.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * TEMA 04 — CIRCUITO (le tracce ciano del logo)
 *
 * Quasi nero (#050D20), tutto il colore sta nel ciano #30E0FF delle tracce
 * del logo: contorni sottili, pulsanti VUOTI col bordo acceso e l'alone
 * quando il mouse ci passa sopra (funzione pulsante propria, sotto), testo
 * dei pulsanti in maiuscolo, angoli quasi vivi. Lo sfondo e' una scheda:
 * tracce orizzontali che piegano a 45 gradi e finiscono in un anello.
 *
 * Anteprima: tema_04_circuito.png
 * ============================================================================= */
#include "extema.h"

static unsigned int h32(unsigned int a)
{
    a ^= a >> 16; a *= 0x7feb352du; a ^= a >> 15; a *= 0x846ca68bu; a ^= a >> 16;
    return a;
}

static void sfondo_circuito(ExTela *t, const ExTema *m, int x, int y, int w, int h)
{
    int r, k;

    ext_sfuma_v(t, x, y, w, h, m->c.desktop_a, m->c.desktop_b);
    for (r = 0; r < h / 22; r++) {
        int yy = y + 11 + r * 22, xx = x;
        unsigned int s = h32((unsigned int)r * 977u + 5u);

        for (k = 0; xx < x + w; k++) {
            unsigned int q = h32(s + (unsigned int)k * 131u);
            int l = 30 + (int)(q % 140u), su = (q >> 9) & 1u ? 11 : -11;
            int a = 40 + (int)((q >> 13) % 110u);

            if ((q >> 20) % 3u == 0) { xx += l / 2; continue; }       /* un buco */
            ext_linea_alfa(t, xx, yy, xx + l, yy, 1, m->c.bordo_sopra, a);
            if ((q >> 22) & 1u) {
                ext_linea_alfa(t, xx + l, yy, xx + l + 11, yy + su, 1, m->c.bordo_sopra, a);
                ext_anello(t, xx + l + 14, yy + su, 3, 1, m->c.bordo_sopra);
                xx += l + 28;
            } else {
                ext_cerchio(t, xx + l + 2, yy, 2, m->c.bordo_sopra);
                xx += l + 12;
            }
        }
    }
}

/* Pulsante vuoto: solo il contorno ciano; col mouse sopra si accende un
 * alone e il fondo si tinge appena; premuto si riempie. Il predefinito e'
 * pieno di ciano col testo scuro. */
static void pulsante_circuito(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                              const char *testo, unsigned int st)
{
    unsigned int c = m->c.accento, ct = m->c.accento;
    char buf[40];
    int i, r = 3;

    for (i = 0; testo && testo[i] && i < 39; i++)
        buf[i] = (testo[i] >= 'a' && testo[i] <= 'z') ? (char)(testo[i] - 32) : testo[i];
    buf[i] = 0;

    if (st & EXT_SPENTO) {
        ext_tondo_bordo(t, x, y, w, h, r, 1, m->c.testo_spento);
        ext_testo_centrato(t, x, y, w, h, buf, m->c.testo_spento, 0);
        return;
    }
    if (st & (EXT_SOPRA | EXT_FUOCO)) ext_ombra(t, x, y, w, h, r, 8, 120, c, 0);
    if (st & (EXT_PREMUTO | EXT_PREDEFINITO)) {
        ext_tondo_pieno(t, x, y, w, h, r, (st & EXT_PREMUTO) ? ext_schiarisci(c, -60) : c,
                        (st & EXT_PREMUTO) ? ext_schiarisci(c, -60) : ext_schiarisci(c, -30));
        ct = m->c.accento_testo;
    } else {
        ext_tondo_pieno(t, x, y, w, h, r, m->c.fondo, m->c.fondo);
        if (st & EXT_SOPRA) ext_tondo_alfa(t, x, y, w, h, r, c, 40);
    }
    ext_tondo_bordo(t, x, y, w, h, r, 1, c);
    /* le due tacche agli angoli, come i piedini di un componente */
    ext_riempi(t, x + 6, y - 2, 6, 2, c);
    ext_riempi(t, x + w - 12, y + h, 6, 2, c);
    ext_testo_centrato(t, x, y, w, h, buf, ct, 1);
}

const ExTema extema_circuito = {
    EXTEMA_ABI, "circuito", "Circuito (tracce del logo)",
    {
        .desktop_a = 0x00071129, .desktop_b = 0x00030814,
        .fondo = 0x00071129, .fondo_alt = 0x000B1737,
        .testo = 0x00BFF8FF, .testo_spento = 0x00406078,
        .campo = 0x00040A19, .campo_testo = 0x00DFFCFF, .campo_spento = 0x00081430,
        .bordo = 0x001A5872, .bordo_sopra = 0x0030E0FF,
        .luce = 0x001A5872, .ombra = 0x00030814, .luce2 = 0x00103448, .ombra2 = 0x00050D20,
        .accento = 0x0030E0FF, .accento_testo = 0x0004101E, .accento_chiaro = 0x00103A50,
        .fuoco = 0x0030E0FF,
        .puls_a = 0x00071129, .puls_b = 0x00071129,
        .puls_sopra_a = 0x000B2040, .puls_sopra_b = 0x000B2040,
        .puls_giu_a = 0x00107090, .puls_giu_b = 0x00107090,
        .puls_testo = 0x0030E0FF,
        .barra_a = 0x000B1737, .barra_b = 0x00071129,
        .barra_ina_a = 0x00071129, .barra_ina_b = 0x00071129,
        .titolo = 0x0030E0FF, .titolo_ina = 0x00406078,
        .telaio = 0x0030E0FF,
        .chiudi = 0x00FF3D6E, .chiudi_sopra = 0x00C0204C,
        .riduci = 0x0030E0FF, .ingrandisci = 0x0030E0FF,
        .scorri_fondo = 0x00040A19, .scorri_cursore = 0x001A5872, .scorri_cursore_sopra = 0x0030E0FF,
        .menu_fondo = 0x00050D20, .menu_testo = 0x00BFF8FF,
        .menu_sopra = 0x0030E0FF, .menu_sopra_testo = 0x0004101E,
        .ombra_finestra = 0x0030E0FF,
        .suggerimento = 0x0030E0FF, .suggerimento_testo = 0x0004101E,
        .progresso_a = 0x0030E0FF, .progresso_b = 0x003FD8FF,
    },
    {
        .raggio = 3, .raggio_finestra = 4, .bordo3d = 0, .spessore = 1,
        .ombra_finestra = 10, .ombra_alfa = 70, .ombra_pulsante = 0,
        .sfumato = 0, .lucido = 0, .barra_h = 24,
        .titolo_centrato = 0, .titolo_grassetto = 1,
        .chiudi_stile = EXT_CHIUDI_PIATTO, .spunta_raggio = 2,
        .scorri_largo = 10, .scorri_frecce = 0, .scorri_tondo = 0,
        .linguette = EXT_LING_SOTTOLINEA, .fuoco_stile = EXT_FUOCO_BORDO,
        .sfondo = EXT_SFONDO_SFUMATO, .maiuscolo = 1, .bagliore = 6,
    },
    pulsante_circuito, 0, sfondo_circuito
};

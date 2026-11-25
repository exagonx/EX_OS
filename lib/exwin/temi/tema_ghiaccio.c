/* =============================================================================
 * lib/exwin/temi/tema_ghiaccio.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * TEMA 03 — GHIACCIO (il logo in chiaro)
 *
 * Gli stessi colori del cristallo usati su fondo chiaro: per chi lavora di
 * giorno e trova faticoso il blu notte. Bianco-ghiaccio, accenti #4063D0 e
 * #5B5CFF, linee e fuoco ciano #3FD8FF, barra del titolo come la luce che
 * attraversa il cristallo (#89E2FA -> #6C9DEF). Pulsanti della barra a tre
 * tondi nei tre colori del logo. Sfondo: una trama di esagoni appena
 * visibile, con qualche scintilla.
 *
 * Anteprima: tema_03_ghiaccio.png
 * ============================================================================= */
#include "extema.h"

static void sfondo_ghiaccio(ExTela *t, const ExTema *m, int x, int y, int w, int h)
{
    int i, j, passo = 56, rx = 30, ry = 26;

    ext_sfuma_v(t, x, y, w, h, m->c.desktop_a, m->c.desktop_b);
    /* la trama: esagoni a nido d'ape, righe sfalsate di mezzo passo */
    for (j = 0; j * (ry * 2 - 4) < h + ry * 2; j++)
        for (i = -1; i * passo * 2 / 2 < w + passo; i++) {
            int cx = x + i * (rx * 3 / 2 + 15) * 2 + ((j & 1) ? rx * 3 / 2 + 15 : 0);
            int cy = y + j * (ry * 2 - 4);
            ext_esagono(t, cx, cy, rx, ry, 1, 0x00FFFFFFu, 70);
        }
    for (i = 0; i < 14; i++) {
        int sx = x + (int)((unsigned int)(i * 7919 + 13) % (unsigned int)w);
        int sy = y + (int)((unsigned int)(i * 104729 + 7) % (unsigned int)h);
        ext_scintilla(t, sx, sy, 5 + i % 4, 0x00FFFFFFu);
    }
}

const ExTema extema_ghiaccio = {
    EXTEMA_ABI, "ghiaccio", "Ghiaccio (logo chiaro)",
    {
        .desktop_a = 0x00BFEFFF, .desktop_b = 0x006C9DEF,
        .fondo = 0x00F3F8FE, .fondo_alt = 0x00E4EEFA,
        .testo = 0x0013244F, .testo_spento = 0x0098A8C8,
        .campo = 0x00FFFFFF, .campo_testo = 0x0013244F, .campo_spento = 0x00EEF3FA,
        .bordo = 0x00AFC6E6, .bordo_sopra = 0x0046C8FF,
        .luce = 0x00FFFFFF, .ombra = 0x007F96C0, .luce2 = 0x00F3F8FE, .ombra2 = 0x00AFC6E6,
        .accento = 0x004063D0, .accento_testo = 0x00FFFFFF, .accento_chiaro = 0x00D8E8FF,
        .fuoco = 0x003FD8FF,
        .puls_a = 0x00FFFFFF, .puls_b = 0x00E6F0FC,
        .puls_sopra_a = 0x00FFFFFF, .puls_sopra_b = 0x00D6F3FF,
        .puls_giu_a = 0x00C9DDF6, .puls_giu_b = 0x00E6F0FC,
        .puls_testo = 0x0013244F,
        .barra_a = 0x0089E2FA, .barra_b = 0x006C9DEF,
        .barra_ina_a = 0x00E4EEFA, .barra_ina_b = 0x00D0DDF0,
        .titolo = 0x000B1737, .titolo_ina = 0x007F8DB5,
        .telaio = 0x00C6DAF2,
        .chiudi = 0x005B5CFF, .chiudi_sopra = 0x004849E0,
        .riduci = 0x003FD8FF, .ingrandisci = 0x0089E2FA,
        .scorri_fondo = 0x00EAF2FC, .scorri_cursore = 0x00AFC6E6, .scorri_cursore_sopra = 0x004063D0,
        .menu_fondo = 0x00FFFFFF, .menu_testo = 0x0013244F,
        .menu_sopra = 0x00D8E8FF, .menu_sopra_testo = 0x000B1737,
        .ombra_finestra = 0x0013244F,
        .suggerimento = 0x0013244F, .suggerimento_testo = 0x00DFFCFF,
        .progresso_a = 0x003FD8FF, .progresso_b = 0x005B5CFF,
    },
    {
        .raggio = 6, .raggio_finestra = 9, .bordo3d = 0, .spessore = 1,
        .ombra_finestra = 16, .ombra_alfa = 90, .ombra_pulsante = 2,
        .sfumato = 1, .lucido = 0, .barra_h = 26,
        .titolo_centrato = 1, .titolo_grassetto = 1,
        .chiudi_stile = EXT_CHIUDI_TONDI, .spunta_raggio = 4,
        .scorri_largo = 12, .scorri_frecce = 0, .scorri_tondo = 1,
        .linguette = EXT_LING_SOTTOLINEA, .fuoco_stile = EXT_FUOCO_ANELLO,
        .sfondo = EXT_SFONDO_SFUMATO, .maiuscolo = 0, .bagliore = 0,
    },
    0, 0, sfondo_ghiaccio
};

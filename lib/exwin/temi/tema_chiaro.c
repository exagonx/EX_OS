/* =============================================================================
 * lib/exwin/temi/tema_chiaro.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * TEMA 05 — PIATTO CHIARO
 *
 * Lo stile piatto delle interfacce di oggi: niente rilievo, contorni grigi di
 * un pixel, angoli a 5, una sola tinta d'accento (blu #2F6FED) per cio' che
 * conta (pulsante predefinito, selezione, fuoco). Il fuoco colora il bordo e
 * sottolinea le caselle. Pulsanti della barra piatti, la X diventa rossa col
 * mouse sopra. Ombra morbida e larga sotto le finestre: e' lei, e non il
 * telaio, a separare una finestra dall'altra.
 *
 * Anteprima: tema_05_chiaro.png
 * ============================================================================= */
#include "extema.h"

const ExTema extema_chiaro = {
    EXTEMA_ABI, "chiaro", "Piatto chiaro",
    {
        .desktop_a = 0x006A8FC8, .desktop_b = 0x002D4A7C,
        .fondo = 0x00F5F6F8, .fondo_alt = 0x00ECEEF2,
        .testo = 0x001F2328, .testo_spento = 0x00A0A6AE,
        .campo = 0x00FFFFFF, .campo_testo = 0x001F2328, .campo_spento = 0x00F0F1F3,
        .bordo = 0x00C9CED6, .bordo_sopra = 0x008C96A3,
        .luce = 0x00FFFFFF, .ombra = 0x00A0A6AE, .luce2 = 0x00F5F6F8, .ombra2 = 0x00C9CED6,
        .accento = 0x002F6FED, .accento_testo = 0x00FFFFFF, .accento_chiaro = 0x00E3ECFD,
        .fuoco = 0x002F6FED,
        .puls_a = 0x00FFFFFF, .puls_b = 0x00FFFFFF,
        .puls_sopra_a = 0x00F0F3F8, .puls_sopra_b = 0x00F0F3F8,
        .puls_giu_a = 0x00E1E6EE, .puls_giu_b = 0x00E1E6EE,
        .puls_testo = 0x001F2328,
        .barra_a = 0x00FFFFFF, .barra_b = 0x00FFFFFF,
        .barra_ina_a = 0x00F0F1F3, .barra_ina_b = 0x00F0F1F3,
        .titolo = 0x001F2328, .titolo_ina = 0x008C96A3,
        .telaio = 0x00D5D9DF,
        .chiudi = 0x00E81123, .chiudi_sopra = 0x00E81123,
        .riduci = 0x001F2328, .ingrandisci = 0x001F2328,
        .scorri_fondo = 0x00F0F1F3, .scorri_cursore = 0x00C2C8D0, .scorri_cursore_sopra = 0x008C96A3,
        .menu_fondo = 0x00FFFFFF, .menu_testo = 0x001F2328,
        .menu_sopra = 0x00E3ECFD, .menu_sopra_testo = 0x001F2328,
        .ombra_finestra = 0x00102040,
        .suggerimento = 0x00303438, .suggerimento_testo = 0x00FFFFFF,
        .progresso_a = 0x002F6FED, .progresso_b = 0x002F6FED,
    },
    {
        .raggio = 5, .raggio_finestra = 8, .bordo3d = 0, .spessore = 1,
        .ombra_finestra = 18, .ombra_alfa = 80, .ombra_pulsante = 0,
        .sfumato = 0, .lucido = 0, .barra_h = 30,
        .titolo_centrato = 0, .titolo_grassetto = 0,
        .chiudi_stile = EXT_CHIUDI_PIATTO, .spunta_raggio = 3,
        .scorri_largo = 10, .scorri_frecce = 0, .scorri_tondo = 1,
        .linguette = EXT_LING_SOTTOLINEA, .fuoco_stile = EXT_FUOCO_BORDO,
        .sfondo = EXT_SFONDO_SFUMATO, .maiuscolo = 0, .bagliore = 0,
    },
    0, 0, 0
};

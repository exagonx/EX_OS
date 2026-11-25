/* =============================================================================
 * debug/grafica/tema_08_pastello.c  ->  lib/exwin/temi/tema_pastello.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * TEMA 08 — PASTELLO
 *
 * Morbido: fondi crema-rosa, accento lilla #A57FD8, angoli larghi (10 sui
 * controlli, 14 sulle finestre), ombre colorate (viola, non nero) e basse.
 * Linguette a pillola, tre tondi pastello nella barra. Lo sfondo e' una
 * sfumatura dal rosa all'azzurro.
 *
 * Anteprima: tema_08_pastello.png
 * ============================================================================= */
#include "extema.h"

const ExTema extema_pastello = {
    EXTEMA_ABI, "pastello", "Pastello",
    {
        .desktop_a = 0x00FBD3E9, .desktop_b = 0x00BBD5F7,
        .fondo = 0x00FFF8F3, .fondo_alt = 0x00F7ECF2,
        .testo = 0x004A3F55, .testo_spento = 0x00BCB0C4,
        .campo = 0x00FFFFFF, .campo_testo = 0x004A3F55, .campo_spento = 0x00F8F1F4,
        .bordo = 0x00E4D0E2, .bordo_sopra = 0x00C9A9E6,
        .luce = 0x00FFFFFF, .ombra = 0x00C8B6CC, .luce2 = 0x00FFF8F3, .ombra2 = 0x00E4D0E2,
        .accento = 0x00A57FD8, .accento_testo = 0x00FFFFFF, .accento_chiaro = 0x00F0E2FA,
        .fuoco = 0x00C9A9E6,
        .puls_a = 0x00FFFFFF, .puls_b = 0x00FBEFF6,
        .puls_sopra_a = 0x00FFFFFF, .puls_sopra_b = 0x00F3E6FB,
        .puls_giu_a = 0x00EEDDF4, .puls_giu_b = 0x00F6EAF6,
        .puls_testo = 0x004A3F55,
        .barra_a = 0x00F3DDF0, .barra_b = 0x00E3D7F6,
        .barra_ina_a = 0x00F9F0F4, .barra_ina_b = 0x00F4EEF6,
        .titolo = 0x004A3F55, .titolo_ina = 0x00A99BB4,
        .telaio = 0x00EBDDEB,
        .chiudi = 0x00F4A6A6, .chiudi_sopra = 0x00EC8A8A,
        .riduci = 0x00F7D79A, .ingrandisci = 0x00A8DDB5,
        .scorri_fondo = 0x00F7ECF2, .scorri_cursore = 0x00DCC4EA, .scorri_cursore_sopra = 0x00A57FD8,
        .menu_fondo = 0x00FFFFFF, .menu_testo = 0x004A3F55,
        .menu_sopra = 0x00F0E2FA, .menu_sopra_testo = 0x004A3F55,
        .ombra_finestra = 0x008A6FA8,
        .suggerimento = 0x004A3F55, .suggerimento_testo = 0x00FFF8F3,
        .progresso_a = 0x00F4A6C8, .progresso_b = 0x00A57FD8,
    },
    {
        .raggio = 10, .raggio_finestra = 14, .bordo3d = 0, .spessore = 1,
        .ombra_finestra = 18, .ombra_alfa = 70, .ombra_pulsante = 3,
        .sfumato = 1, .lucido = 0, .barra_h = 28,
        .titolo_centrato = 1, .titolo_grassetto = 1,
        .chiudi_stile = EXT_CHIUDI_TONDI, .spunta_raggio = 5,
        .scorri_largo = 12, .scorri_frecce = 0, .scorri_tondo = 1,
        .linguette = EXT_LING_PILLOLA, .fuoco_stile = EXT_FUOCO_ANELLO,
        .sfondo = EXT_SFONDO_SFUMATO, .maiuscolo = 0, .bagliore = 0,
    },
    0, 0, 0
};

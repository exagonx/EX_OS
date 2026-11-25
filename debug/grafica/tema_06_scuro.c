/* =============================================================================
 * debug/grafica/tema_06_scuro.c  ->  lib/exwin/temi/tema_scuro.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * TEMA 06 — PIATTO SCURO
 *
 * Il modo scuro: grafite (#202225) e tre livelli di grigio per separare
 * pannelli, campi e barre senza linee forti; accento verde acqua #3BA99C.
 * Il fuoco e' un anello esterno: su fondo scuro un bordo colorato di un pixel
 * si perde, l'anello di due no. Testo non bianco puro (#E6E6E6): il bianco
 * pieno sul grafite abbaglia.
 *
 * Anteprima: tema_06_scuro.png
 * ============================================================================= */
#include "extema.h"

const ExTema extema_scuro = {
    EXTEMA_ABI, "scuro", "Piatto scuro",
    {
        .desktop_a = 0x002B3A4A, .desktop_b = 0x00101418,
        .fondo = 0x00202225, .fondo_alt = 0x002B2D31,
        .testo = 0x00E6E6E6, .testo_spento = 0x006A6E75,
        .campo = 0x0018191C, .campo_testo = 0x00E6E6E6, .campo_spento = 0x00242629,
        .bordo = 0x003D4046, .bordo_sopra = 0x00666B73,
        .luce = 0x003D4046, .ombra = 0x00101114, .luce2 = 0x002B2D31, .ombra2 = 0x0018191C,
        .accento = 0x003BA99C, .accento_testo = 0x0010201E, .accento_chiaro = 0x0026403D,
        .fuoco = 0x003BA99C,
        .puls_a = 0x0036393E, .puls_b = 0x0036393E,
        .puls_sopra_a = 0x00414449, .puls_sopra_b = 0x00414449,
        .puls_giu_a = 0x002B2D31, .puls_giu_b = 0x002B2D31,
        .puls_testo = 0x00E6E6E6,
        .barra_a = 0x0018191C, .barra_b = 0x0018191C,
        .barra_ina_a = 0x00202225, .barra_ina_b = 0x00202225,
        .titolo = 0x00E6E6E6, .titolo_ina = 0x006A6E75,
        .telaio = 0x00303236,
        .chiudi = 0x00D9534F, .chiudi_sopra = 0x00C42B1C,
        .riduci = 0x00E6E6E6, .ingrandisci = 0x00E6E6E6,
        .scorri_fondo = 0x0018191C, .scorri_cursore = 0x004A4E55, .scorri_cursore_sopra = 0x00666B73,
        .menu_fondo = 0x002B2D31, .menu_testo = 0x00E6E6E6,
        .menu_sopra = 0x003BA99C, .menu_sopra_testo = 0x0010201E,
        .ombra_finestra = 0x00000000,
        .suggerimento = 0x00E6E6E6, .suggerimento_testo = 0x00202225,
        .progresso_a = 0x003BA99C, .progresso_b = 0x0055C7B8,
    },
    {
        .raggio = 6, .raggio_finestra = 8, .bordo3d = 0, .spessore = 1,
        .ombra_finestra = 20, .ombra_alfa = 150, .ombra_pulsante = 0,
        .sfumato = 0, .lucido = 0, .barra_h = 30,
        .titolo_centrato = 1, .titolo_grassetto = 0,
        .chiudi_stile = EXT_CHIUDI_PIATTO, .spunta_raggio = 4,
        .scorri_largo = 10, .scorri_frecce = 0, .scorri_tondo = 1,
        .linguette = EXT_LING_PILLOLA, .fuoco_stile = EXT_FUOCO_ANELLO,
        .sfondo = EXT_SFONDO_RIGHE, .maiuscolo = 0, .bagliore = 0,
    },
    0, 0, 0
};

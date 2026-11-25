/* =============================================================================
 * lib/exwin/temi/tema_metallo.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * TEMA 10 — METALLO SPAZZOLATO
 *
 * Acciaio: il rilievo a due anelli del classico con luci fredde e ombre
 * profonde, pulsanti sfumati come lamiera curvata, barra del titolo in
 * canna di fucile (#5A6068 -> #2E3238) col titolo bianco, sfondo spazzolato
 * (rumore orizzontale, EXT_SFONDO_SPAZZOLATO). Accento arancio #E8833A,
 * l'unico colore caldo: selezione e pulsante predefinito.
 *
 * Anteprima: tema_10_metallo.png
 * ============================================================================= */
#include "extema.h"

const ExTema extema_metallo = {
    EXTEMA_ABI, "metallo", "Metallo spazzolato",
    {
        .desktop_a = 0x008E949C, .desktop_b = 0x004C5158,
        .fondo = 0x00BFC3C8, .fondo_alt = 0x00B0B4BA,
        .testo = 0x0016191C, .testo_spento = 0x00767C84,
        .campo = 0x00F2F3F5, .campo_testo = 0x0016191C, .campo_spento = 0x00C8CCD0,
        .bordo = 0x00767C84, .bordo_sopra = 0x00454A50,
        .luce = 0x00F4F6F8, .ombra = 0x0030343A, .luce2 = 0x00D8DCE0, .ombra2 = 0x007A8088,
        .accento = 0x00E8833A, .accento_testo = 0x00FFFFFF, .accento_chiaro = 0x00F2C9A8,
        .fuoco = 0x0016191C,
        .puls_a = 0x00E4E7EA, .puls_b = 0x00A4AAB2,
        .puls_sopra_a = 0x00EEF0F2, .puls_sopra_b = 0x00B4BAC2,
        .puls_giu_a = 0x009AA0A8, .puls_giu_b = 0x00C8CCD2,
        .puls_testo = 0x0016191C,
        .barra_a = 0x005A6068, .barra_b = 0x002E3238,
        .barra_ina_a = 0x009AA0A8, .barra_ina_b = 0x00787E86,
        .titolo = 0x00FFFFFF, .titolo_ina = 0x00D8DCE0,
        .telaio = 0x00AEB3B9,
        .chiudi = 0x00C8452E, .chiudi_sopra = 0x00E0583E,
        .riduci = 0x00BFC3C8, .ingrandisci = 0x00BFC3C8,
        .scorri_fondo = 0x00A4AAB2, .scorri_cursore = 0x00C8CCD2, .scorri_cursore_sopra = 0x00DDE0E4,
        .menu_fondo = 0x00D4D7DB, .menu_testo = 0x0016191C,
        .menu_sopra = 0x00E8833A, .menu_sopra_testo = 0x00FFFFFF,
        .ombra_finestra = 0x00000000,
        .suggerimento = 0x0030343A, .suggerimento_testo = 0x00F4F6F8,
        .progresso_a = 0x00F4A060, .progresso_b = 0x00C8642A,
    },
    {
        .raggio = 0, .raggio_finestra = 0, .bordo3d = 1, .spessore = 4,
        .ombra_finestra = 12, .ombra_alfa = 110, .ombra_pulsante = 0,
        .sfumato = 1, .lucido = 0, .barra_h = 24,
        .titolo_centrato = 0, .titolo_grassetto = 1,
        .chiudi_stile = EXT_CHIUDI_SEGNI, .spunta_raggio = 0,
        .scorri_largo = 16, .scorri_frecce = 1, .scorri_tondo = 0,
        .linguette = EXT_LING_CLASSICA, .fuoco_stile = EXT_FUOCO_PUNTINI,
        .sfondo = EXT_SFONDO_SPAZZOLATO, .maiuscolo = 0, .bagliore = 0,
    },
    0, 0, 0
};

/* =============================================================================
 * lib/exwin/temi/tema_vetro.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * TEMA 07 — VETRO LUCIDO
 *
 * Superfici lucide: ogni pulsante e la barra del titolo hanno la sfumatura e
 * il riflesso bianco sulla meta' alta (ExForma.lucido), angoli larghi (9) che
 * fanno dei pulsanti quasi delle pillole, il predefinito blu in gel. Barra
 * del titolo argentata con i tre tondi a sinistra (chiudi, riduci,
 * ingrandisci) e il titolo al centro. Sfondo a righe sottili orizzontali.
 *
 * ! E' IL TEMA CHE COSTA DI PIU': ogni pulsante paga gli angoli
 * sovracampionati e un riempimento in piu' per il riflesso. Su una macchina
 * lenta conviene la cache dei pulsanti descritta in
 * implementazione_TEMA001.txt (passo 5).
 *
 * Anteprima: tema_07_vetro.png
 * ============================================================================= */
#include "extema.h"

const ExTema extema_vetro = {
    EXTEMA_ABI, "vetro", "Vetro lucido",
    {
        .desktop_a = 0x005B8FD6, .desktop_b = 0x00244F8E,
        .fondo = 0x00ECECEC, .fondo_alt = 0x00DEDEDE,
        .testo = 0x00202020, .testo_spento = 0x00A0A0A0,
        .campo = 0x00FFFFFF, .campo_testo = 0x00202020, .campo_spento = 0x00F2F2F2,
        .bordo = 0x009A9A9A, .bordo_sopra = 0x005A8ED8,
        .luce = 0x00FFFFFF, .ombra = 0x00808080, .luce2 = 0x00F4F4F4, .ombra2 = 0x00B0B0B0,
        .accento = 0x003A86E8, .accento_testo = 0x00FFFFFF, .accento_chiaro = 0x00D2E4FA,
        .fuoco = 0x007FB2F0,
        .puls_a = 0x00FDFDFD, .puls_b = 0x00D9D9D9,
        .puls_sopra_a = 0x00FFFFFF, .puls_sopra_b = 0x00E4ECF7,
        .puls_giu_a = 0x00C4C4C4, .puls_giu_b = 0x00E2E2E2,
        .puls_testo = 0x00202020,
        .barra_a = 0x00EDEDED, .barra_b = 0x00BEBEBE,
        .barra_ina_a = 0x00F4F4F4, .barra_ina_b = 0x00DCDCDC,
        .titolo = 0x00303030, .titolo_ina = 0x00909090,
        .telaio = 0x00A8A8A8,
        .chiudi = 0x00E0565B, .chiudi_sopra = 0x00C83A40,
        .riduci = 0x00E8B84A, .ingrandisci = 0x005DBB63,
        .scorri_fondo = 0x00EEEEEE, .scorri_cursore = 0x007FAEEA, .scorri_cursore_sopra = 0x003A86E8,
        .menu_fondo = 0x00F7F7F7, .menu_testo = 0x00202020,
        .menu_sopra = 0x003A86E8, .menu_sopra_testo = 0x00FFFFFF,
        .ombra_finestra = 0x00000000,
        .suggerimento = 0x00FFFFE6, .suggerimento_testo = 0x00202020,
        .progresso_a = 0x007FB8F8, .progresso_b = 0x001F6ED0,
    },
    {
        .raggio = 9, .raggio_finestra = 7, .bordo3d = 0, .spessore = 1,
        .ombra_finestra = 18, .ombra_alfa = 120, .ombra_pulsante = 2,
        .sfumato = 1, .lucido = 1, .barra_h = 24,
        .titolo_centrato = 1, .titolo_grassetto = 0,
        .chiudi_stile = EXT_CHIUDI_TONDI, .spunta_raggio = 3,
        .scorri_largo = 14, .scorri_frecce = 0, .scorri_tondo = 1,
        .linguette = EXT_LING_PILLOLA, .fuoco_stile = EXT_FUOCO_ANELLO,
        .sfondo = EXT_SFONDO_RIGHE, .maiuscolo = 0, .bagliore = 3,
    },
    0, 0, 0
};

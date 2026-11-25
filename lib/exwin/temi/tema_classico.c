/* =============================================================================
 * lib/exwin/temi/tema_classico.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * TEMA 01 — CLASSICO RIFINITO
 *
 * L'aspetto di oggi, rifatto con cura: rilievo a due anelli invece di uno
 * (luce/luce2, ombra/ombra2: il pulsante ha spessore invece di un filo),
 * barra del titolo sfumata blu, grigio leggermente caldo invece del C0C0C0
 * puro, pulsanti della barra con i segni disegnati a tratto spesso, fuoco a
 * puntini dentro il controllo. E' il tema predefinito consigliato per le
 * macchine lente: niente ombre morbide, niente angoli da sovracampionare —
 * solo rettangoli pieni e linee, come oggi.
 *
 * ! I COLORI SONO QUELLI CHE excol_applica() RICAVA dallo schema "Classico"
 * (colori/schemi/01_classico.colori): aprire Impostazioni > Colori e
 * premere Applica senza toccare niente lascia l'aspetto com'e': al piu'
 * 12 livelli su 255 in un canale, a occhio nessuna differenza (verificato).
 *
 * Anteprima: tema_01_classico.png (uscita da questo codice: fa_anteprime.sh)
 * ============================================================================= */
#include "extema.h"

const ExTema extema_classico = {
    EXTEMA_ABI, "classico", "Classico rifinito",
    {
        .desktop_a = 0x003A6EA5, .desktop_b = 0x00152F55,
        .fondo = 0x00D4D0C8, .fondo_alt = 0x00C8C4BC,
        .testo = 0x00000000, .testo_spento = 0x00808080,
        .campo = 0x00FFFFFF, .campo_testo = 0x00000000, .campo_spento = 0x00D4D0C8,
        .bordo = 0x00808080, .bordo_sopra = 0x00404040,
        .luce = 0x00FFFFFF, .ombra = 0x00404040,
        .luce2 = 0x00E8E6E0, .ombra2 = 0x00808080,
        .accento = 0x000A246A, .accento_testo = 0x00FFFFFF, .accento_chiaro = 0x00BCC3D7,
        .fuoco = 0x00000000,
        .puls_a = 0x00E4E1DA, .puls_b = 0x00CCC8C0,
        .puls_sopra_a = 0x00DEDBD4, .puls_sopra_b = 0x00D8D4CC,
        .puls_giu_a = 0x00BCB8B0, .puls_giu_b = 0x00CCC8C0,
        .puls_testo = 0x00000000,
        .barra_a = 0x000A246A, .barra_b = 0x00A6CAF0,
        .barra_ina_a = 0x00808080, .barra_ina_b = 0x00C0C0C0,
        .titolo = 0x00FFFFFF, .titolo_ina = 0x00D4D0C8,
        .telaio = 0x00D4D0C8,
        .chiudi = 0x00C04040, .chiudi_sopra = 0x00C95D5D,
        .riduci = 0x00D4D0C8, .ingrandisci = 0x00D4D0C8,
        .scorri_fondo = 0x00E8E6E0, .scorri_cursore = 0x00D4D0C8, .scorri_cursore_sopra = 0x00E4E1DA,
        .menu_fondo = 0x00F2F0EC, .menu_testo = 0x00000000,
        .menu_sopra = 0x000A246A, .menu_sopra_testo = 0x00FFFFFF,
        .ombra_finestra = 0x00000000,
        .suggerimento = 0x00FFFFE1, .suggerimento_testo = 0x00000000,
        .progresso_a = 0x000A246A, .progresso_b = 0x005877AD,
    },
    {
        .raggio = 0, .raggio_finestra = 0, .bordo3d = 1, .spessore = 4,
        .ombra_finestra = 0, .ombra_alfa = 0, .ombra_pulsante = 0,
        .sfumato = 1, .lucido = 0, .barra_h = 22,
        .titolo_centrato = 0, .titolo_grassetto = 1,
        .chiudi_stile = EXT_CHIUDI_SEGNI, .spunta_raggio = 0,
        .scorri_largo = 16, .scorri_frecce = 1, .scorri_tondo = 0,
        .linguette = EXT_LING_CLASSICA, .fuoco_stile = EXT_FUOCO_PUNTINI,
        .sfondo = EXT_SFONDO_SFUMATO, .maiuscolo = 0, .bagliore = 0,
    },
    0, 0, 0
};

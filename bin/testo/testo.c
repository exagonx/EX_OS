/* =============================================================================
 * bin/testo/testo.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Rimette lo schermo in modo testo 80x25.
 *
 *     testo
 *
 * ! E' UN COMANDO CHE SI DIGITA ALLA CIECA, e tutto in questo file segue da
 * li'. Serve quando un server grafico e' morto lasciando la scheda in
 * modalita' grafica: il sistema e' vivo, la tastiera funziona, ma lo schermo
 * e' congelato su cio' che c'era. Chi lo batte non vede quello che sta
 * scrivendo.
 *
 * Percio': nessuna opzione, nessuna conferma, nessuna domanda. Il nome e'
 * corto perche' va battuto senza vederlo, e non fa nient'altro — un secondo
 * compito qui dentro sarebbe un secondo modo di sbagliare a occhi chiusi.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `testo -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("testo", "0.001");

int main(void)
{
    modo_testo();

    /* Si stampa DOPO, perche' prima non si sarebbe visto: e' anche la prova
     * che la console e' tornata a funzionare. */
    printf("Modo testo 80x25 ripristinato.\n");
    return 0;
}

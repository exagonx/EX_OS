/* =============================================================================
 * lib/exinfo/exinfo.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * «Informazioni su» — la stessa cosa detta allo stesso modo da ogni programma
 *
 * ! E' UN MODULO COMPILATO DENTRO, NON UNA .so, e per una volta il criterio
 * dei due utenti non c'entra: sono centoventi righe senza stato e senza
 * dipendenze, e una libreria condivisa costerebbe piu' spazio nella tabella
 * delle librerie aperte (LIB_MAX) di quanto ne faccia risparmiare.
 *
 * ! IL NOME E LA DESCRIZIONE LI DA' IL PROGRAMMA, il resto lo sa questo file:
 * l'autore, l'indirizzo, la versione del sistema e la memoria. Cosi' il giorno
 * che cambia l'indirizzo di posta si cambia QUI, e non in quattro programmi
 * che nel frattempo sono diventati otto.
 * ============================================================================= */
#ifndef EXINFO_H
#define EXINFO_H

#ifdef __cplusplus
extern "C" {
#endif

#define EXINFO_AUTORE   "Graziano Falcone"
#define EXINFO_EMAIL    "exagonx@hotmail.com"

/* La memoria del processo in byte, divisa nelle tre parti che si sanno
 * misurare. Un puntatore nullo si puo' passare: quel pezzo non si riporta. */
void exinfo_memoria(unsigned int *immagine, unsigned int *heap,
                    unsigned int *pila);

/* Compone il testo di «Informazioni su» in `out`. `nome` e `descrizione` li
 * da' il programma; l'autore, l'indirizzo, la versione e la memoria no. */
void exinfo_testo(char *out, unsigned int max,
                  const char *nome, const char *descrizione);

#ifdef __cplusplus
}
#endif

#endif /* EXINFO_H */

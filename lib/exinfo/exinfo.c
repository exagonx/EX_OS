/* =============================================================================
 * lib/exinfo/exinfo.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il perche' sta in exinfo.h. Qui c'e' solo come si misura.
 * ============================================================================= */

#include "libc.h"
#include "exinfo.h"

/* I due estremi dell'immagine caricata. `_start` e' il punto d'ingresso, cioe'
 * l'inizio di .text; `_bss_end` lo pone il linker script alla fine di .bss.
 * Li dichiarano tutti i linker script dei programmi. */
extern void _start(void);
extern char _bss_end;

/* =============================================================================
 * ! LA MEMORIA DI UN PROCESSO NON LA DICE NESSUNA CHIAMATA, e va composta.
 *
 * `meminfo()` racconta la RAM della MACCHINA, non quella di chi chiede;
 * `procinfo()` da' gli stack ma non il resto. Le tre parti si sanno pero'
 * ricavare tutte, e sono quelle che contano:
 *
 *   immagine  da `_start` a `_bss_end`: codice, dati e bss, cioe' quanto il
 *             programma occupa prima ancora di cominciare
 *   heap      da dove finisce la bss (arrotondato alla pagina, che e' dove il
 *             kernel mette heap_start) fino a sbrk(0)
 *   pila      ustack_top - ustack_base, cioe' le pagine di stack che il
 *             processo ha DAVVERO toccato: lo stack cresce su fault, quindi la
 *             riserva dichiarata non dice niente su quanto se ne usa
 *
 * ! CIO' CHE MANCA SI DICHIARA: non ci sono dentro le librerie condivise —
 * exwin, la libc, exhttp — perche' quelle pagine sono di tutti e attribuirle a
 * chi le apre le conterebbe una volta per programma. Chi vuole quel numero
 * guarda `mem`.
 * ============================================================================= */
void exinfo_memoria(unsigned int *immagine, unsigned int *heap,
                    unsigned int *pila)
{
    unsigned int base = (unsigned int)(void *)_start;
    unsigned int fine = (unsigned int)&_bss_end;

    if (immagine) *immagine = (fine > base) ? fine - base : 0;

    if (heap) {
        unsigned int inizio = (fine + 4095u) & ~4095u;
        unsigned int cima   = (unsigned int)sbrk(0);

        *heap = (cima > inizio) ? cima - inizio : 0;
    }

    if (pila) {
        ProcInfo     v[PROCINFO_MAX_BATCH];
        unsigned int start = 0, mio = (unsigned int)getpid();
        int          n, i, trovato = 0;

        *pila = 0;
        while (!trovato && (n = procinfo(v, PROCINFO_MAX_BATCH, start)) > 0) {
            for (i = 0; i < n; i++) {
                if (v[i].pid != mio) continue;
                if (v[i].ustack_top > v[i].ustack_base)
                    *pila = v[i].ustack_top - v[i].ustack_base;
                trovato = 1;
                break;
            }
            start += (unsigned int)n;
        }
    }
}

/* Un numero di byte in KB arrotondati per eccesso: sotto il kilobyte non c'e'
 * niente di interessante da dire, e le cifre in piu' fanno solo rumore. */
static unsigned int kb(unsigned int byte)
{
    return (byte + 1023u) / 1024u;
}

void exinfo_testo(char *out, unsigned int max, const char *nome,
                  const char *versione, const char *descrizione)
{
    unsigned int img = 0, hp = 0, pl = 0;

    if (!out || max < 2) return;

    exinfo_memoria(&img, &hp, &pl);

    /* ! IL TESTO STA IN DODICI RIGHE, che e' quanto ne mostra ex_dlg_avviso.
     * Non e' un tetto da indovinare: e' scritto in lib/exdlg/exdlg.c accanto a
     * AVVISO_RIGHE, e superarlo significa un dialogo TRONCATO — che e' peggio
     * di uno corto, perche' quello che si legge sembra tutto. La versione del
     * SISTEMA non c'e' apposta: la dicono gia' `ver` e `uname`, e qui
     * costerebbe due righe su dodici. Quella del PROGRAMMA c'e', e non costa
     * niente: sta sulla riga del nome.
     *
     * ! E SOLO ASCII. Il font della console non ha i trattini lunghi ne' le
     * virgolette basse: al loro posto compaiono tre caratteri di spazzatura in
     * mezzo a una frase, e si e' visto proprio qui. */
    snprintf(out, max,
             "%s %s\n\n%s\n\n%s  -  %s\n\n"
             "Memoria: %u KB  (programma %u, heap %u, pila %u)",
             nome ? nome : "",
             versione ? versione : "",
             descrizione ? descrizione : "",
             EXINFO_AUTORE, EXINFO_EMAIL,
             kb(img + hp + pl), kb(img), kb(hp), kb(pl));
}

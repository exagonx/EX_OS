/* =============================================================================
 * lib/exsuono/exsuono_stub.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Lo stub di exsuono: e' quel che un programma collega per usare
 * /exwin/lib/exsuono.so. La libreria si apre alla prima chiamata.
 *
 * ! QUI NON SI MUORE SE LA LIBRERIA MANCA, al contrario di exwin ed exhttp.
 * Il suono e' un di piu': una scrivania che non puo' fare l'intro deve
 * aprirsi lo stesso, e un programma che non puo' fare «bip» deve fare il
 * resto. Senza libreria ogni funzione risponde come se non ci fosse il file
 * (EXSUONO_NO_FILE), che e' un errore che chi chiama gestisce gia'.
 * ============================================================================= */

#include "exsuono.h"
#include "exlib.h"

static const char *const g_dove[] = {
    "/exwin/lib/exsuono.so",
    "/cdrom/exwin/lib/exsuono.so"
};

static struct {
    int stato;                          /* 0 da provare, 1 c'e', -1 non c'e' */
    int  (*durata)(const char *, unsigned int *);
    int  (*apri)(const char *, ExSuonoInfo *);
    void (*chiudi)(void);
    int  (*suona)(void);
    void (*pausa)(void);
    int  (*vai)(unsigned int);
    int  (*passo)(void);
    unsigned int (*posizione)(void);
    int  (*in_corso)(void);
    void (*volume)(unsigned int);
    int  (*avvia_file)(const char *);
    void (*ferma_file)(int);
    int  (*mix_leggi)(unsigned int, ExSuonoVoce *);
    int  (*mix_metti)(unsigned int, unsigned int, unsigned int, ExSuonoVoce *);
} P;

static int assicura(void)
{
    const ExLibTesta *t;

    if (P.stato) return P.stato > 0;

    t = exlib_apri_fra(g_dove, (int)(sizeof g_dove / sizeof g_dove[0]));
    if (t == 0) { P.stato = -1; return 0; }

    P.durata     = (int (*)(const char *, unsigned int *))exlib_simbolo(t, "exsuono_durata");
    P.apri       = (int (*)(const char *, ExSuonoInfo *))exlib_simbolo(t, "exsuono_apri");
    P.chiudi     = (void (*)(void))exlib_simbolo(t, "exsuono_chiudi");
    P.suona      = (int (*)(void))exlib_simbolo(t, "exsuono_suona");
    P.pausa      = (void (*)(void))exlib_simbolo(t, "exsuono_pausa");
    P.vai        = (int (*)(unsigned int))exlib_simbolo(t, "exsuono_vai");
    P.passo      = (int (*)(void))exlib_simbolo(t, "exsuono_passo");
    P.posizione  = (unsigned int (*)(void))exlib_simbolo(t, "exsuono_posizione");
    P.in_corso   = (int (*)(void))exlib_simbolo(t, "exsuono_in_corso");
    P.volume     = (void (*)(unsigned int))exlib_simbolo(t, "exsuono_volume");
    P.avvia_file = (int (*)(const char *))exlib_simbolo(t, "exsuono_avvia_file");
    P.ferma_file = (void (*)(int))exlib_simbolo(t, "exsuono_ferma_file");
    /* Il mixer e' del 10 ottobre 2026: su una exsuono.so di prima manca, e
     * non per questo il resto si rifiuta. Le due funzioni rendono un errore. */
    P.mix_leggi  = (int (*)(unsigned int, ExSuonoVoce *))exlib_simbolo(t, "exsuono_mix_leggi");
    P.mix_metti  = (int (*)(unsigned int, unsigned int, unsigned int, ExSuonoVoce *))
                   exlib_simbolo(t, "exsuono_mix_metti");

    /* Le dodici ci devono essere tutte: una libreria a meta' non si usa. */
    if (!P.durata || !P.apri || !P.chiudi || !P.suona || !P.pausa || !P.vai ||
        !P.passo || !P.posizione || !P.in_corso || !P.volume || !P.avvia_file ||
        !P.ferma_file) { P.stato = -1; return 0; }

    P.stato = 1;
    return 1;
}

int exsuono_durata(const char *file, unsigned int *ms)
{ return assicura() ? P.durata(file, ms) : EXSUONO_NO_FILE; }

int exsuono_apri(const char *file, ExSuonoInfo *info)
{ return assicura() ? P.apri(file, info) : EXSUONO_NO_FILE; }

void exsuono_chiudi(void)            { if (assicura()) P.chiudi(); }
int  exsuono_suona(void)             { return assicura() ? P.suona() : EXSUONO_NO_FILE; }
void exsuono_pausa(void)             { if (assicura()) P.pausa(); }
int  exsuono_vai(unsigned int ms)    { return assicura() ? P.vai(ms) : EXSUONO_NO_FILE; }
int  exsuono_passo(void)             { return assicura() ? P.passo() : 0; }
unsigned int exsuono_posizione(void) { return assicura() ? P.posizione() : 0; }
int  exsuono_in_corso(void)          { return assicura() ? P.in_corso() : 0; }
void exsuono_volume(unsigned int p)  { if (assicura()) P.volume(p); }

int exsuono_avvia_file(const char *file)
{ return assicura() ? P.avvia_file(file) : EXSUONO_NO_FILE; }

void exsuono_ferma_file(int pid)     { if (assicura()) P.ferma_file(pid); }

int exsuono_mix_leggi(unsigned int i, ExSuonoVoce *v)
{ return (assicura() && P.mix_leggi) ? P.mix_leggi(i, v) : EXSUONO_NO_SCHEDA; }

int exsuono_mix_metti(unsigned int i, unsigned int sin, unsigned int des, ExSuonoVoce *v)
{ return (assicura() && P.mix_metti) ? P.mix_metti(i, sin, des, v) : EXSUONO_NO_SCHEDA; }

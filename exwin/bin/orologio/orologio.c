/* =============================================================================
 * exwin/bin/orologio/orologio.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La data e l'ora nell'angolo della barra
 *
 * ! E' UN PROCESSO A PARTE, E NON UN THREAD, PERCHE' I THREAD NON ESISTONO.
 * EX-OS ha processi: non c'e' pthread_create, non c'e' clone, non c'e' nessuna
 * chiamata di sistema che crei un secondo filo dentro lo stesso programma.
 *
 * ! E ANCHE SE CI FOSSERO, UN PROCESSO SAREBBE MEGLIO QUI. Quello che si vuole
 * e' che l'ora si aggiorni «qualunque cosa faccia il resto del sistema»: un
 * thread dentro il program manager condividerebbe con lui la connessione al
 * server a finestre e la sua coda di messaggi, quindi un program manager
 * occupato sarebbe un orologio fermo. Un processo separato lo scheduler lo fa
 * girare per conto suo, e se muore si porta via solo l'orologio.
 *
 * ! LA FINESTRA STA SOPRA A TUTTE, come la barra: e' un pezzo della barra, e
 * deve restare visibile anche sotto una finestra a schermo intero.
 *
 * ! L'ORA E' QUELLA UNIVERSALE, e va detto invece di lasciarlo scoprire. La
 * libc dichiara che localtime() e' IDENTICA a gmtime(): questo sistema non sa
 * in che fuso si trovi, perche' non c'e' niente che glielo dica ne' un posto
 * dove tenerlo. Mostrare un'ora locale inventata sarebbe peggio di mostrare
 * quella universale, che almeno e' vera.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"

/* ! LA MISURA E' RICAVATA DAL TESTO PIU' LUNGO CHE PUO' USCIRE, non scelta a
 * occhio: «19/08/2026  05:49:07» sono venti caratteri, e col font di sistema
 * un carattere e' otto pixel. Quattro di margine per parte. */
#define TESTO_MAX   20
#define FIN_W       (TESTO_MAX * 8 + 8)
#define FIN_H       20

/* La barra delle applicazioni del program manager e' alta 28 e sta in fondo:
 * lo stesso numero sta in exwin/bin/pm/pm.c. Se cambia la', cambia qui. */
#define BARRA_H     28

static ExFinestra g_f;
static char       g_testo[TESTO_MAX + 1] = "";

static const char *MESI[12] = {
    "gen", "feb", "mar", "apr", "mag", "giu",
    "lug", "ago", "set", "ott", "nov", "dic"
};

/* Riempie g_testo con la data e l'ora. Rende 1 se e' CAMBIATO. */
static int aggiorna(void)
{
    struct timeval tv;
    struct tm     *t;
    char           nuovo[TESTO_MAX + 1];

    if (gettimeofday(&tv, 0) != 0) return 0;

    t = localtime(&tv.tv_sec);
    if (!t) return 0;

    /* ! IL MESE E' UNA PAROLA, NON UN NUMERO, e non e' vezzo: «08/09» e' una
     * data diversa in Europa e in America, «8 set» no. Con tre lettere si sta
     * anche in meno spazio di quattro cifre e due barre. */
    sprintf(nuovo, "%2d %s %04d  %02d:%02d",
            t->tm_mday,
            (t->tm_mon >= 0 && t->tm_mon < 12) ? MESI[t->tm_mon] : "???",
            t->tm_year + 1900, t->tm_hour, t->tm_min);

    if (strcmp(nuovo, g_testo) == 0) return 0;

    strcpy(g_testo, nuovo);
    return 1;
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CHIUDI:
        ex_esci(0);
        return 0;

    /* ! SI RIDISEGNA SOLO SE IL TESTO E' CAMBIATO, e la sveglia batte molto
     * piu' spesso del minuto. Ridisegnare ogni volta vorrebbe dire chiedere al
     * server di ricomporre lo schermo due volte al secondo per sempre — su una
     * macchina che ridisegna tutto lo schermo a ogni aggiornamento e' lavoro
     * continuo per niente. Cosi' si lavora sessanta volte in meno. */
    case EXM_TEMPO:
        if (!aggiorna()) return 0;
        /* cade nel disegno */

    case EXM_DISEGNA:
        ex_riempi(f, 0, 0, FIN_W, FIN_H, EX_GRIGIO);
        ex_incavo(f, 0, 0, FIN_W, FIN_H);
        ex_scrivi(f, (FIN_W - ex_larghezza_testo(EX_FONT_SISTEMA, g_testo)) / 2,
                  2, g_testo, EX_NERO);
        ex_aggiorna(f);
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }
}

int main(int argc, char **argv)
{
    ExMsg        m;
    unsigned int sw = 0, sh = 0;

    (void)argc; (void)argv;

    ex_schermo(&sw, &sh);
    if (sw == 0) {
        printf("orologio: il server a finestre non risponde.\n");
        printf("          Avvialo con:  exwin\n");
        return 1;
    }

    /* All'estremita' destra della barra, con quattro pixel di aria dal bordo. */
    g_f = ex_crea("finestra", "", EX_SOPRA,
                  (int)sw - FIN_W - 4, (int)sh - BARRA_H + 4,
                  FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) {
        printf("orologio: non riesco a creare la finestra\n");
        return 1;
    }

    aggiorna();
    proc(g_f, EXM_DISEGNA, 0, 0);

    /* ! MEZZO SECONDO, PER UN OROLOGIO AL MINUTO, e non e' spreco: la sveglia
     * costa un confronto dentro un ciclo che gia' si sveglia da solo cinque
     * volte al secondo, e il disegno si fa solo quando il testo cambia. Con un
     * periodo di un minuto esatto l'ora si vedrebbe cambiare con un ritardo
     * fino a un minuto intero, che su un orologio si nota. */
    ex_sveglia(g_f, 500);

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}

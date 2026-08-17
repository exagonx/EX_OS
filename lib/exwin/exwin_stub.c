/* =============================================================================
 * lib/exwin/exwin_stub.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Lo STUB di ExWin: cio' che si collega dentro l'applicazione al posto della
 * libreria intera
 *
 * Un'applicazione grafica si collegava con `exwin.c` dentro: 13 KB di toolkit
 * piu' 4 KB di font in OGNI programma. Adesso si collega con questo file, che
 * definisce gli stessi nomi e li fa arrivare alla libreria condivisa.
 *
 * ! IL SORGENTE DELL'APPLICAZIONE NON CAMBIA DI UNA RIGA. Stesso `exwin.h`,
 * stesse chiamate. Cambia solo cosa si collega, e lo decide il Makefile: e'
 * questa la ragione per cui lo stub definisce esattamente i nomi di exwin.h
 * invece di offrirne di nuovi.
 *
 * ! LA RISOLUZIONE E' PIGRA, E NON C'E' NIENTE DA CHIAMARE PRIMA DI main().
 * Un `ex_avvia()` da mettere in cima a ogni main sarebbe una riga in piu' in
 * ogni applicazione — e una riga che, dimenticata, da' un salto a zero invece
 * di un messaggio. Qui la prima chiamata a una qualunque funzione di ExWin
 * risolve TUTTI i nomi in un colpo, e il costo e' un confronto per chiamata.
 *
 * ! SI CERCA IN DUE POSTI, E L'ORDINE CONTA: su un sistema installato la
 * libreria sta in /exwin/lib, avviando dal CD sta sotto /cdrom. E' la stessa
 * regola del program manager e del file manager, per la stessa ragione.
 * ============================================================================= */

#include "exwin.h"
#include "exlib.h"

/* Il minimo per lamentarsi e morire senza tirarsi dietro la libc: se ExWin non
 * si trova, printf potrebbe non essere l'unica cosa che manca. */
#define SYS_WRITE   4
#define SYS_EXIT    1

static void grida_e_muori(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    __asm__ volatile ("int $0x80" :: "a"(SYS_WRITE), "b"(2), "c"(s), "d"(n) : "memory");
    __asm__ volatile ("int $0x80" :: "a"(SYS_EXIT), "b"(1) : "memory");
    for (;;) { }
}

static const char *const g_dove[] = {
    "/exwin/lib/exwin.so",
    "/cdrom/exwin/lib/exwin.so"
};

/* I puntatori risolti. Uno solo `pronto`: o ci sono tutti o non si parte. */
static struct {
    int pronto;
    ExFinestra  (*crea)(const char *, const char *, unsigned int,
                        int, int, int, int, ExFinestra, unsigned int, ExProcedura);
    void        (*distruggi)(ExFinestra);
    void        (*titolo)(ExFinestra, const char *);
    void        (*sposta)(ExFinestra, int, int);
    void        (*mostra)(ExFinestra, int);
    void        (*fuoco)(ExFinestra);
    void        (*testo_metti)(ExFinestra, const char *);
    const char *(*testo_prendi)(ExFinestra);
    int         (*prendi_msg)(ExMsg *);
    void        (*smista)(const ExMsg *);
    void        (*esci)(int);
    long        (*procedura_base)(ExFinestra, unsigned int, unsigned int, long);
    void        (*riempi)(ExFinestra, int, int, int, int, unsigned int);
    void        (*riquadro)(ExFinestra, int, int, int, int, unsigned int);
    void        (*scrivi)(ExFinestra, int, int, const char *, unsigned int);
    void        (*aggiorna)(ExFinestra);
    void        (*pixmap)(ExFinestra, int, int, int, int,
                          const unsigned int *, unsigned int);
    int         (*immagine)(ExFinestra, const char *, int, int);
    void         (*l_svuota)(ExFinestra);
    int          (*l_aggiungi)(ExFinestra, const char *);
    unsigned int (*l_quante)(ExFinestra);
    unsigned int (*l_scelta)(ExFinestra);
    void         (*l_scegli)(ExFinestra, unsigned int);
    const char  *(*l_testo)(ExFinestra, unsigned int);
    void         (*a_svuota)(ExFinestra);
    int          (*a_aggiungi)(ExFinestra, const char *);
    unsigned int (*a_righe)(ExFinestra);
    const char  *(*a_riga)(ExFinestra, unsigned int);
    int          (*a_modificato)(ExFinestra);
    void         (*a_pulita)(ExFinestra);
    void         (*a_cursore)(ExFinestra, unsigned int *, unsigned int *);
    void        (*schermo)(unsigned int *, unsigned int *);
} P;

static void *chiedi(const ExLibTesta *t, const char *nome)
{
    void *p = exlib_simbolo(t, nome);

    /* ! UN NOME CHE MANCA SI DICE, E SI DICE QUALE. E' il modo in cui ci si
     * accorge che la libreria installata e' piu' vecchia del programma —
     * l'unico guaio che il patto per nome non impedisce. Proseguire con un
     * puntatore a zero darebbe un salto nel vuoto, e il fault indicherebbe
     * l'applicazione invece della libreria. */
    if (p == 0) {
        grida_e_muori("exwin: la libreria condivisa non esporta un nome che "
                      "serve a questo programma:\n        ");
    }
    return p;
}

static void assicura(void)
{
    const ExLibTesta *t;

    if (P.pronto) return;

    t = exlib_apri_fra(g_dove, (int)(sizeof g_dove / sizeof g_dove[0]));
    if (t == 0) {
        grida_e_muori("exwin: non trovo la libreria condivisa.\n"
                      "       Cercata in /exwin/lib/exwin.so e "
                      "/cdrom/exwin/lib/exwin.so\n");
    }

    P.crea           = (ExFinestra (*)(const char *, const char *, unsigned int,
                                       int, int, int, int, ExFinestra,
                                       unsigned int, ExProcedura))
                       chiedi(t, "ex_crea");
    P.distruggi      = (void (*)(ExFinestra))              chiedi(t, "ex_distruggi");
    P.titolo         = (void (*)(ExFinestra, const char *))chiedi(t, "ex_titolo");
    P.sposta         = (void (*)(ExFinestra, int, int))    chiedi(t, "ex_sposta");
    P.mostra         = (void (*)(ExFinestra, int))         chiedi(t, "ex_mostra");
    P.fuoco          = (void (*)(ExFinestra))              chiedi(t, "ex_fuoco");
    P.testo_metti    = (void (*)(ExFinestra, const char *))chiedi(t, "ex_testo_metti");
    P.testo_prendi   = (const char *(*)(ExFinestra))       chiedi(t, "ex_testo_prendi");
    P.prendi_msg     = (int (*)(ExMsg *))                  chiedi(t, "ex_prendi_msg");
    P.smista         = (void (*)(const ExMsg *))           chiedi(t, "ex_smista");
    P.esci           = (void (*)(int))                     chiedi(t, "ex_esci");
    P.procedura_base = (long (*)(ExFinestra, unsigned int, unsigned int, long))
                       chiedi(t, "ex_procedura_base");
    P.riempi         = (void (*)(ExFinestra, int, int, int, int, unsigned int))
                       chiedi(t, "ex_riempi");
    P.riquadro       = (void (*)(ExFinestra, int, int, int, int, unsigned int))
                       chiedi(t, "ex_riquadro_disegna");
    P.scrivi         = (void (*)(ExFinestra, int, int, const char *, unsigned int))
                       chiedi(t, "ex_scrivi");
    P.aggiorna       = (void (*)(ExFinestra))              chiedi(t, "ex_aggiorna");
    P.pixmap         = (void (*)(ExFinestra, int, int, int, int,
                                 const unsigned int *, unsigned int))
                       chiedi(t, "ex_pixmap");
    P.immagine       = (int (*)(ExFinestra, const char *, int, int))
                       chiedi(t, "ex_immagine");
    P.l_svuota   = (void (*)(ExFinestra))                     chiedi(t, "ex_lista_svuota");
    P.l_aggiungi = (int (*)(ExFinestra, const char *))        chiedi(t, "ex_lista_aggiungi");
    P.l_quante   = (unsigned int (*)(ExFinestra))             chiedi(t, "ex_lista_quante");
    P.l_scelta   = (unsigned int (*)(ExFinestra))             chiedi(t, "ex_lista_scelta");
    P.l_scegli   = (void (*)(ExFinestra, unsigned int))       chiedi(t, "ex_lista_scegli");
    P.l_testo    = (const char *(*)(ExFinestra, unsigned int))chiedi(t, "ex_lista_testo");

    P.a_svuota     = (void (*)(ExFinestra))                  chiedi(t, "ex_area_svuota");
    P.a_aggiungi   = (int (*)(ExFinestra, const char *))     chiedi(t, "ex_area_aggiungi");
    P.a_righe      = (unsigned int (*)(ExFinestra))          chiedi(t, "ex_area_righe");
    P.a_riga       = (const char *(*)(ExFinestra, unsigned int)) chiedi(t, "ex_area_riga");
    P.a_modificato = (int (*)(ExFinestra))                   chiedi(t, "ex_area_modificato");
    P.a_pulita     = (void (*)(ExFinestra))                  chiedi(t, "ex_area_pulita");
    P.a_cursore    = (void (*)(ExFinestra, unsigned int *, unsigned int *))
                     chiedi(t, "ex_area_cursore");

    P.schermo        = (void (*)(unsigned int *, unsigned int *))
                       chiedi(t, "ex_schermo");

    P.pronto = 1;
}

/* -----------------------------------------------------------------------------
 * I ponti. Uno per funzione, e non c'e' altro modo di scriverli: una macro che
 * li generasse renderebbe illeggibile l'unico posto in cui si vede, in chiaro,
 * che cosa questo stub promette.
 * --------------------------------------------------------------------------- */
ExFinestra ex_crea(const char *classe, const char *titolo, unsigned int stile,
                   int x, int y, int w, int h,
                   ExFinestra padre, unsigned int id, ExProcedura proc)
{
    assicura();
    return P.crea(classe, titolo, stile, x, y, w, h, padre, id, proc);
}

void ex_distruggi(ExFinestra f)            { assicura(); P.distruggi(f); }
void ex_titolo(ExFinestra f, const char *s){ assicura(); P.titolo(f, s); }
void ex_sposta(ExFinestra f, int x, int y) { assicura(); P.sposta(f, x, y); }
void ex_mostra(ExFinestra f, int v)        { assicura(); P.mostra(f, v); }
void ex_fuoco(ExFinestra f)                { assicura(); P.fuoco(f); }

void ex_testo_metti(ExFinestra f, const char *s) { assicura(); P.testo_metti(f, s); }
const char *ex_testo_prendi(ExFinestra f)        { assicura(); return P.testo_prendi(f); }

int  ex_prendi_msg(ExMsg *m)        { assicura(); return P.prendi_msg(m); }
void ex_smista(const ExMsg *m)      { assicura(); P.smista(m); }
void ex_esci(int codice)            { assicura(); P.esci(codice); }

long ex_procedura_base(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    assicura();
    return P.procedura_base(f, msg, wp, lp);
}

void ex_riempi(ExFinestra f, int x, int y, int w, int h, unsigned int c)
{
    assicura();
    P.riempi(f, x, y, w, h, c);
}

void ex_riquadro_disegna(ExFinestra f, int x, int y, int w, int h, unsigned int c)
{
    assicura();
    P.riquadro(f, x, y, w, h, c);
}

void ex_scrivi(ExFinestra f, int x, int y, const char *s, unsigned int c)
{
    assicura();
    P.scrivi(f, x, y, s, c);
}

void ex_aggiorna(ExFinestra f) { assicura(); P.aggiorna(f); }

void ex_pixmap(ExFinestra f, int x, int y, int w, int h,
               const unsigned int *px, unsigned int passo)
{
    assicura();
    P.pixmap(f, x, y, w, h, px, passo);
}

int ex_immagine(ExFinestra f, const char *percorso, int x, int y)
{
    assicura();
    return P.immagine(f, percorso, x, y);
}

void ex_lista_svuota(ExFinestra f)  { assicura(); P.l_svuota(f); }
unsigned int ex_lista_quante(ExFinestra f)  { assicura(); return P.l_quante(f); }
unsigned int ex_lista_scelta(ExFinestra f)  { assicura(); return P.l_scelta(f); }
void ex_lista_scegli(ExFinestra f, unsigned int i) { assicura(); P.l_scegli(f, i); }

int ex_lista_aggiungi(ExFinestra f, const char *s)
{
    assicura();
    return P.l_aggiungi(f, s);
}

const char *ex_lista_testo(ExFinestra f, unsigned int i)
{
    assicura();
    return P.l_testo(f, i);
}

void ex_area_svuota(ExFinestra f)          { assicura(); P.a_svuota(f); }
unsigned int ex_area_righe(ExFinestra f)   { assicura(); return P.a_righe(f); }
int  ex_area_modificato(ExFinestra f)      { assicura(); return P.a_modificato(f); }
void ex_area_pulita(ExFinestra f)          { assicura(); P.a_pulita(f); }

int ex_area_aggiungi(ExFinestra f, const char *r) { assicura(); return P.a_aggiungi(f, r); }
const char *ex_area_riga(ExFinestra f, unsigned int i) { assicura(); return P.a_riga(f, i); }

void ex_area_cursore(ExFinestra f, unsigned int *r, unsigned int *c)
{
    assicura();
    P.a_cursore(f, r, c);
}

void ex_schermo(unsigned int *l, unsigned int *a) { assicura(); P.schermo(l, a); }

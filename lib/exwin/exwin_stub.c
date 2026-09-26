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
    void        (*misura)(ExFinestra, int, int);
    void        (*mostra)(ExFinestra, int);
    void        (*fuoco)(ExFinestra);
    void        (*fuoco_via)(ExFinestra);
    ExFinestra  (*fuoco_chi)(ExFinestra);
    void        (*spegni)(void);
    unsigned int (*app_metti)(const char *, unsigned int);
    unsigned int (*app_prendi)(char *, unsigned int);
    void        (*testo_metti)(ExFinestra, const char *);
    const char *(*testo_prendi)(ExFinestra);
    int         (*prendi_msg)(ExMsg *);
    int         (*msg_ora)(ExMsg *);
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
    void         (*a_sel_tutto)(ExFinestra);
    int          (*a_copia)(ExFinestra);
    int          (*a_taglia)(ExFinestra);
    int          (*a_incolla)(ExFinestra);
    int          (*a_cancella)(ExFinestra);
    ExFinestra  (*menu)(ExFinestra);
    int         (*menu_voce)(ExFinestra, const char *, const char *, unsigned int);
    void        (*rilievo)(ExFinestra, int, int, int, int);
    void        (*incavo)(ExFinestra, int, int, int, int);
    void        (*schermo)(unsigned int *, unsigned int *);
    /* I font. In fondo, come vuole la regola dell'elenco esportato. */
    unsigned int (*font_apri)(const char *, int);
    unsigned int (*font_trova)(int, int, int, int);
    const char  *(*font_nome)(int, int, int);
    void         (*font_chiudi)(unsigned int);
    int          (*font_altezza)(unsigned int);
    int          (*font_base)(unsigned int);
    int          (*larghezza_testo)(unsigned int, const char *);
    void         (*scrivi_con)(ExFinestra, unsigned int, int, int,
                               const char *, unsigned int);
    void         (*sveglia)(ExFinestra, unsigned int);

    /* La spunta, il radio, la barra, le voci di un combo o di una barra di
     * linguette: aggiunti il 3 settembre 2026. */
    int          (*acceso)(ExFinestra);
    void         (*accendi)(ExFinestra, int);
    void         (*scorri_limiti)(ExFinestra, unsigned int, unsigned int);
    unsigned int (*scorri_dove)(ExFinestra);
    void         (*scorri_vai)(ExFinestra, unsigned int);
    void         (*voci_svuota)(ExFinestra);
    int          (*voce_aggiungi)(ExFinestra, const char *);
    unsigned int (*voci_quante)(ExFinestra);
    unsigned int (*voce_scelta)(ExFinestra);
    void         (*voce_scegli)(ExFinestra, unsigned int);
    const char  *(*voce_testo)(ExFinestra, unsigned int);
    void         (*area_vai)(ExFinestra, unsigned int, unsigned int);
    void         (*area_riga_metti)(ExFinestra, unsigned int, const char *);
    void         (*area_colora)(ExFinestra, ExColora, void *);
    unsigned int (*colora_c)(void *, const char *, unsigned char *, unsigned int);
    ExFinestra   (*mdi_attivo)(ExFinestra);
    void         (*mdi_attiva)(ExFinestra);

    void         (*lista_icona)(ExFinestra, unsigned int, ExIcona);
    unsigned int (*lista_margine)(ExFinestra);
    ExIcona      (*icona_apri)(const char *);
    unsigned int (*icona_lato)(ExIcona);
    int          (*icona_disegna)(ExFinestra, ExIcona, int, int,
                                  unsigned int, unsigned int);
    void         (*icona_metti)(ExFinestra, ExIcona, unsigned int);
    void         (*icona_chiudi)(ExIcona);
    unsigned int (*a_vista)(ExFinestra, unsigned int *);
    void         (*a_mostra_da)(ExFinestra, unsigned int);
    int          (*guarda_fd)(int, ExGuarda, void *);
    void         (*tab_contenuto)(ExFinestra, int);
    void         (*finestre_segui)(ExFinestra);
    int          (*finestre_elenco)(ExVoceFin *, int);
    void         (*finestra_attiva)(unsigned int);
    void         (*finestra_riduci)(unsigned int);
    void         (*chiudi_altre)(void);
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
    P.misura         = (void (*)(ExFinestra, int, int))    chiedi(t, "ex_misura");
    P.mostra         = (void (*)(ExFinestra, int))         chiedi(t, "ex_mostra");
    P.fuoco          = (void (*)(ExFinestra))              chiedi(t, "ex_fuoco");
    P.fuoco_via      = (void (*)(ExFinestra))              chiedi(t, "ex_fuoco_via");
    P.fuoco_chi      = (ExFinestra (*)(ExFinestra))        chiedi(t, "ex_fuoco_chi");
    P.spegni         = (void (*)(void))                    chiedi(t, "ex_spegni_scrivania");
    P.app_metti      = (unsigned int (*)(const char *, unsigned int)) chiedi(t, "ex_appunti_metti");
    P.app_prendi     = (unsigned int (*)(char *, unsigned int))       chiedi(t, "ex_appunti_prendi");
    P.testo_metti    = (void (*)(ExFinestra, const char *))chiedi(t, "ex_testo_metti");
    P.testo_prendi   = (const char *(*)(ExFinestra))       chiedi(t, "ex_testo_prendi");
    P.prendi_msg     = (int (*)(ExMsg *))                  chiedi(t, "ex_prendi_msg");
    P.msg_ora        = (int (*)(ExMsg *))                  chiedi(t, "ex_msg_ora");
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

    P.a_sel_tutto = (void (*)(ExFinestra))chiedi(t, "ex_area_seleziona_tutto");
    P.a_copia     = (int (*)(ExFinestra)) chiedi(t, "ex_area_copia");
    P.a_taglia    = (int (*)(ExFinestra)) chiedi(t, "ex_area_taglia");
    P.a_incolla   = (int (*)(ExFinestra)) chiedi(t, "ex_area_incolla");
    P.a_cancella  = (int (*)(ExFinestra)) chiedi(t, "ex_area_cancella");
    P.menu           = (ExFinestra (*)(ExFinestra))        chiedi(t, "ex_menu");
    P.menu_voce      = (int (*)(ExFinestra, const char *, const char *, unsigned int))
                       chiedi(t, "ex_menu_voce");
    P.rilievo        = (void (*)(ExFinestra, int, int, int, int))
                       chiedi(t, "ex_rilievo");
    P.incavo         = (void (*)(ExFinestra, int, int, int, int))
                       chiedi(t, "ex_incavo");
    P.schermo        = (void (*)(unsigned int *, unsigned int *))
                       chiedi(t, "ex_schermo");

    P.font_apri       = (unsigned int (*)(const char *, int))
                        chiedi(t, "ex_font_apri");
    P.font_trova      = (unsigned int (*)(int, int, int, int))
                        chiedi(t, "ex_font_trova");
    P.font_nome       = (const char *(*)(int, int, int))
                        chiedi(t, "ex_font_nome");
    P.font_chiudi     = (void (*)(unsigned int))chiedi(t, "ex_font_chiudi");
    P.font_altezza    = (int (*)(unsigned int))chiedi(t, "ex_font_altezza");
    P.font_base       = (int (*)(unsigned int))chiedi(t, "ex_font_base");
    P.larghezza_testo = (int (*)(unsigned int, const char *))
                        chiedi(t, "ex_larghezza_testo");
    P.scrivi_con      = (void (*)(ExFinestra, unsigned int, int, int,
                                  const char *, unsigned int))
                        chiedi(t, "ex_scrivi_con");
    P.sveglia         = (void (*)(ExFinestra, unsigned int))
                        chiedi(t, "ex_sveglia");

    P.acceso        = (int (*)(ExFinestra))            chiedi(t, "ex_acceso");
    P.accendi       = (void (*)(ExFinestra, int))      chiedi(t, "ex_accendi");
    P.scorri_limiti = (void (*)(ExFinestra, unsigned int, unsigned int))
                      chiedi(t, "ex_scorri_limiti");
    P.scorri_dove   = (unsigned int (*)(ExFinestra))   chiedi(t, "ex_scorri_dove");
    P.scorri_vai    = (void (*)(ExFinestra, unsigned int))
                      chiedi(t, "ex_scorri_vai");
    P.voci_svuota   = (void (*)(ExFinestra))           chiedi(t, "ex_voci_svuota");
    P.voce_aggiungi = (int (*)(ExFinestra, const char *))
                      chiedi(t, "ex_voce_aggiungi");
    P.voci_quante   = (unsigned int (*)(ExFinestra))   chiedi(t, "ex_voci_quante");
    P.voce_scelta   = (unsigned int (*)(ExFinestra))   chiedi(t, "ex_voce_scelta");
    P.voce_scegli   = (void (*)(ExFinestra, unsigned int))
                      chiedi(t, "ex_voce_scegli");
    P.voce_testo    = (const char *(*)(ExFinestra, unsigned int))
                      chiedi(t, "ex_voce_testo");
    P.area_vai      = (void (*)(ExFinestra, unsigned int, unsigned int))
                      chiedi(t, "ex_area_vai");
    P.area_riga_metti = (void (*)(ExFinestra, unsigned int, const char *))
                        chiedi(t, "ex_area_riga_metti");
    P.area_colora   = (void (*)(ExFinestra, ExColora, void *))
                      chiedi(t, "ex_area_colora");
    P.colora_c      = (unsigned int (*)(void *, const char *, unsigned char *,
                                        unsigned int))
                      chiedi(t, "ex_colora_c");
    P.mdi_attivo    = (ExFinestra (*)(ExFinestra)) chiedi(t, "ex_mdi_attivo");
    P.mdi_attiva    = (void (*)(ExFinestra))       chiedi(t, "ex_mdi_attiva");

    P.lista_icona   = (void (*)(ExFinestra, unsigned int, ExIcona))
                      chiedi(t, "ex_lista_icona");
    P.lista_margine = (unsigned int (*)(ExFinestra))
                      chiedi(t, "ex_lista_margine");
    P.icona_apri    = (ExIcona (*)(const char *))  chiedi(t, "ex_icona_apri");
    P.icona_lato    = (unsigned int (*)(ExIcona))  chiedi(t, "ex_icona_lato");
    P.icona_disegna = (int (*)(ExFinestra, ExIcona, int, int, unsigned int,
                               unsigned int))      chiedi(t, "ex_icona_disegna");
    P.icona_metti   = (void (*)(ExFinestra, ExIcona, unsigned int))
                      chiedi(t, "ex_icona_metti");
    P.icona_chiudi  = (void (*)(ExIcona))          chiedi(t, "ex_icona_chiudi");
    P.a_vista       = (unsigned int (*)(ExFinestra, unsigned int *))
                      chiedi(t, "ex_area_vista");
    P.a_mostra_da   = (void (*)(ExFinestra, unsigned int))
                      chiedi(t, "ex_area_mostra_da");
    /* ! FACOLTATIVA: chi la usa (i download di exdlg) sa dire che manca. */
    P.guarda_fd     = (int (*)(int, ExGuarda, void *))
                      exlib_simbolo(t, "ex_guarda_fd");
    P.tab_contenuto = (void (*)(ExFinestra, int))
                      exlib_simbolo(t, "ex_tab_contenuto");
    /* ! OPTIONAL, like the two above: a new taskbar over an old exwin.so
     * starts, and simply shows no entries. */
    P.finestre_segui  = (void (*)(ExFinestra))
                        exlib_simbolo(t, "ex_finestre_segui");
    P.finestre_elenco = (int (*)(ExVoceFin *, int))
                        exlib_simbolo(t, "ex_finestre_elenco");
    P.finestra_attiva = (void (*)(unsigned int))
                        exlib_simbolo(t, "ex_finestra_attiva");
    P.finestra_riduci = (void (*)(unsigned int))
                        exlib_simbolo(t, "ex_finestra_riduci");
    P.chiudi_altre    = (void (*)(void))
                        exlib_simbolo(t, "ex_chiudi_le_altre");

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
void ex_misura(ExFinestra f, int w, int h) { assicura(); P.misura(f, w, h); }

void ex_area_seleziona_tutto(ExFinestra f) { assicura(); P.a_sel_tutto(f); }
int  ex_area_copia(ExFinestra f)    { assicura(); return P.a_copia(f); }
int  ex_area_taglia(ExFinestra f)   { assicura(); return P.a_taglia(f); }
int  ex_area_incolla(ExFinestra f)  { assicura(); return P.a_incolla(f); }
int  ex_area_cancella(ExFinestra f) { assicura(); return P.a_cancella(f); }

ExFinestra ex_menu(ExFinestra f) { assicura(); return P.menu(f); }
int ex_menu_voce(ExFinestra m, const char *t, const char *v, unsigned int id)
{ assicura(); return P.menu_voce(m, t, v, id); }

void ex_rilievo(ExFinestra f, int x, int y, int w, int h)
{ assicura(); P.rilievo(f, x, y, w, h); }
void ex_incavo(ExFinestra f, int x, int y, int w, int h)
{ assicura(); P.incavo(f, x, y, w, h); }
void ex_mostra(ExFinestra f, int v)        { assicura(); P.mostra(f, v); }
void ex_fuoco(ExFinestra f)                { assicura(); P.fuoco(f); }
void ex_fuoco_via(ExFinestra f)            { assicura(); P.fuoco_via(f); }
ExFinestra ex_fuoco_chi(ExFinestra f)      { assicura(); return P.fuoco_chi(f); }
void ex_spegni_scrivania(void)             { assicura(); P.spegni(); }
unsigned int ex_appunti_metti(const char *t, unsigned int n)
                                           { assicura(); return P.app_metti(t, n); }
unsigned int ex_appunti_prendi(char *o, unsigned int m)
                                           { assicura(); return P.app_prendi(o, m); }

void ex_testo_metti(ExFinestra f, const char *s) { assicura(); P.testo_metti(f, s); }
const char *ex_testo_prendi(ExFinestra f)        { assicura(); return P.testo_prendi(f); }

int  ex_prendi_msg(ExMsg *m)        { assicura(); return P.prendi_msg(m); }
int  ex_msg_ora(ExMsg *m)           { assicura(); return P.msg_ora(m); }
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

ExFont ex_font_apri(const char *p, int corpo) { assicura(); return P.font_apri(p, corpo); }
ExFont ex_font_trova(int fam, int corpo, int g, int c) { assicura(); return P.font_trova(fam, corpo, g, c); }
const char *ex_font_nome(int fam, int g, int c) { assicura(); return P.font_nome(fam, g, c); }
void   ex_font_chiudi(ExFont f)               { assicura(); P.font_chiudi(f); }
int    ex_font_altezza(ExFont f)              { assicura(); return P.font_altezza(f); }
int    ex_font_base(ExFont f)                 { assicura(); return P.font_base(f); }

int ex_larghezza_testo(ExFont f, const char *s)
{ assicura(); return P.larghezza_testo(f, s); }

void ex_scrivi_con(ExFinestra w, ExFont f, int x, int y,
                   const char *s, unsigned int c)
{ assicura(); P.scrivi_con(w, f, x, y, s, c); }

void ex_sveglia(ExFinestra f, unsigned int ms) { assicura(); P.sveglia(f, ms); }

int  ex_acceso(ExFinestra c)             { assicura(); return P.acceso(c); }
void ex_accendi(ExFinestra c, int a)     { assicura(); P.accendi(c, a); }

void ex_scorri_limiti(ExFinestra c, unsigned int massimo, unsigned int pagina)
{ assicura(); P.scorri_limiti(c, massimo, pagina); }
unsigned int ex_scorri_dove(ExFinestra c) { assicura(); return P.scorri_dove(c); }
void ex_scorri_vai(ExFinestra c, unsigned int d) { assicura(); P.scorri_vai(c, d); }

void ex_voci_svuota(ExFinestra c)         { assicura(); P.voci_svuota(c); }
int  ex_voce_aggiungi(ExFinestra c, const char *t)
{ assicura(); return P.voce_aggiungi(c, t); }
unsigned int ex_voci_quante(ExFinestra c) { assicura(); return P.voci_quante(c); }
unsigned int ex_voce_scelta(ExFinestra c) { assicura(); return P.voce_scelta(c); }
void ex_voce_scegli(ExFinestra c, unsigned int i) { assicura(); P.voce_scegli(c, i); }
const char *ex_voce_testo(ExFinestra c, unsigned int i)
{ assicura(); return P.voce_testo(c, i); }

void ex_area_vai(ExFinestra c, unsigned int riga, unsigned int col)
{ assicura(); P.area_vai(c, riga, col); }

void ex_area_riga_metti(ExFinestra c, unsigned int riga, const char *testo)
{ assicura(); P.area_riga_metti(c, riga, testo); }
void ex_area_colora(ExFinestra c, ExColora fn, void *dato)
{ assicura(); P.area_colora(c, fn, dato); }

/* ! CHI SCRIVE `ex_area_colora(a, ex_colora_c, 0)` PASSA QUESTO PONTE, non la
 * funzione dentro la libreria: l'indirizzo che il programma conosce e' questo.
 * Funziona — la libreria chiama qui e questo rimbalza di la' — e costa un salto
 * in piu' per riga disegnata, cioe' una trentina per ridisegno. Si dice perche'
 * chi misurera' i tempi lo trovera' nel mezzo e non deve stupirsi. */
unsigned int ex_colora_c(void *dato, const char *riga, unsigned char *ruoli,
                         unsigned int stato)
{ assicura(); return P.colora_c(dato, riga, ruoli, stato); }

ExFinestra ex_mdi_attivo(ExFinestra c) { assicura(); return P.mdi_attivo(c); }
void       ex_mdi_attiva(ExFinestra c) { assicura(); P.mdi_attiva(c); }

ExIcona      ex_icona_apri(const char *p)  { assicura(); return P.icona_apri(p); }

void ex_lista_icona(ExFinestra f, unsigned int riga, ExIcona ic)
{
    assicura();
    P.lista_icona(f, riga, ic);
}

unsigned int ex_lista_margine(ExFinestra f)
{
    assicura();
    return P.lista_margine(f);
}
unsigned int ex_icona_lato(ExIcona ic)     { assicura(); return P.icona_lato(ic); }
void         ex_icona_chiudi(ExIcona ic)   { assicura(); P.icona_chiudi(ic); }
unsigned int ex_area_vista(ExFinestra f, unsigned int *v) { assicura(); return P.a_vista(f, v); }
void         ex_area_mostra_da(ExFinestra f, unsigned int r) { assicura(); P.a_mostra_da(f, r); }
int          ex_guarda_fd(int fd, ExGuarda fn, void *dato)
{ assicura(); return P.guarda_fd ? P.guarda_fd(fd, fn, dato) : -2; }

/* Optional: an older exwin.so without it just keeps Tab to itself. */
void         ex_tab_contenuto(ExFinestra f, int si)
{ assicura(); if (P.tab_contenuto) P.tab_contenuto(f, si); }

void         ex_finestre_segui(ExFinestra f)
{ assicura(); if (P.finestre_segui) P.finestre_segui(f); }

int          ex_finestre_elenco(ExVoceFin *v, int max)
{ assicura(); return P.finestre_elenco ? P.finestre_elenco(v, max) : 0; }

void         ex_finestra_attiva(unsigned int id)
{ assicura(); if (P.finestra_attiva) P.finestra_attiva(id); }

void         ex_finestra_riduci(unsigned int id)
{ assicura(); if (P.finestra_riduci) P.finestra_riduci(id); }

void         ex_chiudi_le_altre(void)
{ assicura(); if (P.chiudi_altre) P.chiudi_altre(); }

void ex_icona_metti(ExFinestra c, ExIcona ic, unsigned int lato)
{
    assicura();
    P.icona_metti(c, ic, lato);
}

int ex_icona_disegna(ExFinestra f, ExIcona ic, int x, int y,
                     unsigned int lato, unsigned int sfondo)
{
    assicura();
    return P.icona_disegna(f, ic, x, y, lato, sfondo);
}

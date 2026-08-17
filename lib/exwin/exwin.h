/* =============================================================================
 * lib/exwin/exwin.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ExWin — il toolkit di EX-OS, in stile Win32
 *
 *     #include <exwin.h>
 *     collegare con -lexwin
 *
 * ! PERCHE' MANIGLIE E CICLO DEI MESSAGGI INVECE DI WIDGET E SEGNALI.
 * Tre ragioni, e nessuna e' il gusto:
 *
 *   1. IL CICLO DEI MESSAGGI E' GIA' LA FORMA DI QUESTO SISTEMA. Su EX-OS
 *      meta' degli eventi arriva nella mailbox IPC, e ex_prendi_msg() e'
 *      letteralmente poll() su FD_IPC piu' ipc_recv. Un ciclo principale in
 *      stile glib andrebbe costruito SOPRA questo, non al posto suo;
 *   2. NON C'E' UN SISTEMA A OGGETTI DA SCRIVERE. Segnali, tipi, conteggio
 *      dei riferimenti e proprieta' sono un mini-glib da mantenere per
 *      sempre; qui bastano una maniglia opaca e uno switch;
 *   3. FreeBASIC E C++ SI LEGANO SENZA TRUCCHI. Una maniglia e' un intero e
 *      un messaggio e' una struttura di interi: un `declare` e un `type`, e
 *      FreeBASIC ha finito. I segnali con varargs vorrebbero un adattatore
 *      per ogni firma di callback.
 *
 * ! UN CONTROLLO E' UNA FINESTRA FIGLIA, come su Win32, e questo tiene un
 * concetto solo invece di due. Ma le figlie le gestisce QUESTA LIBRERIA, non
 * il server: il server conosce solo le finestre di primo livello. Un pulsante
 * non ha bisogno di una zona di memoria condivisa sua, e darne una a ognuno
 * vorrebbe dire una zona per etichetta.
 *
 * ! LO SCOSTAMENTO DA Win32, DICHIARATO: `id` e `procedura` sono due
 * parametri distinti invece dell'ultimo argomento sovrapposto. In C si
 * potrebbe fare come Win32, ma un `declare` di FreeBASIC non sa esprimere un
 * parametro che a volte e' un intero e a volte un puntatore a funzione senza
 * un cast che il chiamante deve ricordarsi. Meglio due campi.
 * ============================================================================= */

#ifndef EXWIN_H
#define EXWIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* ! LA MANIGLIA E' UN INTERO OPACO, e non un puntatore: attraversa il confine
 * verso FreeBASIC, dove un puntatore a struttura interna sarebbe un invito a
 * guardarci dentro. 0 vuol dire «nessuna finestra». */
typedef unsigned int ExFinestra;

typedef struct {
    ExFinestra   finestra;
    unsigned int msg;
    unsigned int wp;        /* «chi»: l'id di un controllo, un tasto... */
    long         lp;        /* «cosa»: coordinate impacchettate, un puntatore */
} ExMsg;

typedef long (*ExProcedura)(ExFinestra, unsigned int, unsigned int, long);

/* --- I messaggi ---------------------------------------------------------- */
#define EXM_CREA        0x0001  /* la finestra e' nata */
#define EXM_DISEGNA     0x0002  /* ridisegnati: il contenuto va rifatto */
#define EXM_COMANDO     0x0003  /* wp = id del controllo che e' stato premuto */
#define EXM_CHIUDI      0x0004  /* l'utente ha premuto il pulsante di chiusura */
#define EXM_MOUSE_GIU   0x0005  /* lp = x | (y << 16) */
#define EXM_MOUSE_SU    0x0006
#define EXM_TASTO       0x0007  /* wp = scancode */
#define EXM_DISTRUGGI   0x0008
/* ! IL PROGRAMMA DENTRO UN CONTROLLO «terminale» E' USCITO. Arriva una volta
 * sola, alla finestra che contiene il terminale. Un'applicazione che apre un
 * terminale e non lo gestisce resta con una finestra viva intorno a una shell
 * morta: e' quello che succedeva prima che questo messaggio esistesse. */
#define EXM_TERMFINITO  0x0009

#define EX_X(lp)        ((int)((lp) & 0xFFFF))
#define EX_Y(lp)        ((int)(((lp) >> 16) & 0xFFFF))

/* --- Gli stili ----------------------------------------------------------- */
#define EX_TITOLO       0x0001
#define EX_BORDO        0x0002
#define EX_CHIUDI       0x0004
#define EX_VISIBILE     0x0008
#define EX_SFONDO       0x0010  /* sta sotto a tutte e non si sposta */
/* ! SOPRA A TUTTE: e' lo sfondo girato dall'altra parte. Serve alla barra
 * delle applicazioni, che dev'essere raggiungibile anche sotto una finestra a
 * schermo intero — altrimenti l'unico modo di tornare al menu sarebbe
 * spostare quella finestra, e a schermo intero non si potrebbe. */
#define EX_SOPRA        0x0020
#define EX_FIGLIO       0x0100  /* e' un controllo dentro un'altra finestra */

/* --- I colori, in ARGB --------------------------------------------------- */
#define EX_NERO         0x00000000
#define EX_BIANCO       0x00FFFFFF
#define EX_GRIGIO       0x00C0C0C0
#define EX_GRIGIO_SC    0x00808080
#define EX_BLU          0x00305A8A
#define EX_ROSSO        0x00C04040

/* -----------------------------------------------------------------------------
 * Creare
 *
 * `classe` e' una stringa, come su Win32, e non un enum: aggiungere un
 * controllo non deve voler dire ricompilare chi non lo usa.
 *
 *     "finestra"      di primo livello, la conosce il server
 *     "pulsante"      premibile, manda EXM_COMANDO al padre
 *     "etichetta"     testo e basta
 *     "testo"         casella di testo modificabile
 *     "riquadro"      cornice con un titolo, per raggruppare
 *     "separatore"    una riga
 *     "intestazione"  una fascia di titolo dentro la finestra
 *
 * Per una finestra di primo livello: `padre` = 0, `id` = 0, `proc` = la
 * procedura. Per un controllo: `padre` = la finestra, `id` = il numero con cui
 * lo riconoscerai in EXM_COMANDO, `proc` = 0.
 * --------------------------------------------------------------------------- */
ExFinestra ex_crea(const char *classe, const char *titolo, unsigned int stile,
                   int x, int y, int w, int h,
                   ExFinestra padre, unsigned int id, ExProcedura proc);

void       ex_distruggi(ExFinestra f);
void       ex_titolo(ExFinestra f, const char *s);
void       ex_sposta(ExFinestra f, int x, int y);
void       ex_mostra(ExFinestra f, int visibile);

/* Il testo di un controllo: lo legge una casella, lo cambia un'etichetta. */
void        ex_testo_metti(ExFinestra f, const char *s);
const char *ex_testo_prendi(ExFinestra f);

/* -----------------------------------------------------------------------------
 * Il ciclo dei messaggi
 *
 * ! ex_prendi_msg() DORME DAVVERO mentre non succede niente: dentro c'e'
 * poll() su FD_IPC, non un giro a vuoto. Rende 0 quando l'applicazione ha
 * chiesto di uscire.
 * --------------------------------------------------------------------------- */
int  ex_prendi_msg(ExMsg *m);
void ex_smista(const ExMsg *m);
void ex_esci(int codice);

/* Cio' che la procedura deve chiamare per tutto quello che non gestisce:
 * disegna i controlli, ridisegna la cornice, chiude su EXM_CHIUDI. */
long ex_procedura_base(ExFinestra f, unsigned int msg, unsigned int wp, long lp);

/* -----------------------------------------------------------------------------
 * Disegnare dentro una finestra
 *
 * Le coordinate sono relative all'area del client. Si disegna nella memoria
 * condivisa della finestra: il server non vede queste chiamate, vede solo il
 * risultato.
 * --------------------------------------------------------------------------- */
void ex_riempi(ExFinestra f, int x, int y, int w, int h, unsigned int c);
void ex_riquadro_disegna(ExFinestra f, int x, int y, int w, int h, unsigned int c);
void ex_scrivi(ExFinestra f, int x, int y, const char *s, unsigned int c);
void ex_aggiorna(ExFinestra f);     /* «ho finito»: lo dice al server */

/* -----------------------------------------------------------------------------
 * Le immagini
 *
 * ! IL DECODIFICATORE STA QUI, NON NEL SERVER, ed e' una decisione di
 * struttura. Un lettore di JPG o di PNG e' migliaia di righe che interpretano
 * dati venuti da fuori: nel server un suo difetto sarebbe un difetto di TUTTE
 * le applicazioni insieme. Qui e' un guaio dell'applicazione che ha aperto
 * quel file, e il server non sa nemmeno che esistano le immagini.
 *
 * ! OGGI LEGGE SOLO BMP, e la forma e' gia' quella giusta per gli altri: il
 * formato si riconosce dai primi byte del file, non dall'estensione — che
 * mente — e ogni lettore nuovo e' una voce in una tabella. JPG, PNG e ICO si
 * aggiungono senza toccare ne' questa firma ne' il server.
 *
 * Rende 1 se l'ha disegnata, 0 se il formato non e' (ancora) riconosciuto.
 * --------------------------------------------------------------------------- */
int ex_immagine(ExFinestra f, const char *percorso, int x, int y);

/* Quanto e' grande lo schermo. 0 se si e' in modo testo. */
void ex_schermo(unsigned int *larghezza, unsigned int *altezza);

#ifdef __cplusplus
}
#endif

#endif /* EXWIN_H */

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
 *      meta' degli eventi arriva nella mailbox IPC, e ex_get_message() e'
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
typedef unsigned int ExWindow;

typedef struct {
    /* `window` dal 3 ottobre 2026; `finestra` e' lo stesso campo, per i
     * programmi scritti prima. */
    union { ExWindow window; ExWindow finestra; };
    unsigned int msg;
    unsigned int wp;        /* «chi»: l'id di un controllo, un tasto... */
    long         lp;        /* «cosa»: coordinate impacchettate, un puntatore */
} ExMsg;

typedef long (*ExWindowProc)(ExWindow, unsigned int, unsigned int, long);

/* What a window procedure returns (exwin.so 0.010, 29 September 2026):
 *   0                    handled: the toolkit then redraws the WHOLE window
 *                        (EXM_PAINT to the procedure, and ex_update);
 *   EX_NO_REDRAW   handled, and nothing visible changed — or the
 *                        procedure has already drawn and called ex_update
 *                        itself: the toolkit does not redraw;
 *   anything else        not handled: ex_default_proc does its default.
 * ! THE FIRST ONE IS WHY A BIG PROGRAM FLICKERED: every clock tick and every
 * mouse move it handled redrew the whole window. A procedure that knows
 * better says so with the second. */
#define EX_NO_REDRAW  0x4E524431L

/* --- I messaggi ---------------------------------------------------------- */
#define EXM_CREATE        0x0001  /* la finestra e' nata */
#define EXM_PAINT     0x0002  /* ridisegnati: il contenuto va rifatto */
#define EXM_COMMAND     0x0003  /* wp = id del controllo che e' stato premuto */
#define EXM_CLOSE      0x0004  /* l'utente ha premuto il pulsante di chiusura */
#define EXM_MOUSE_DOWN   0x0005  /* lp = x | (y << 16) */
#define EXM_MOUSE_UP    0x0006
#define EXM_KEY       0x0007  /* wp = scancode */
#define EXM_DESTROY   0x0008
/* ! IL PROGRAMMA DENTRO UN CONTROLLO «terminale» E' USCITO. Arriva una volta
 * sola, alla finestra che contiene il terminale. Un'applicazione che apre un
 * terminale e non lo gestisce resta con una finestra viva intorno a una shell
 * morta: e' quello che succedeva prima che questo messaggio esistesse. */
#define EXM_TERM_EXITED  0x0009
/* ! LA FINESTRA HA CAMBIATO MISURA, e lp porta quella nuova: EX_X(lp) e
 * EX_Y(lp) sono larghezza e altezza dell'area del client. Arriva DOPO che il
 * toolkit ha gia' preso la zona di pixel nuova, quindi disegnare qui dentro e'
 * sicuro — ma i controlli sono ancora dove stavano, e rimetterli a posto e'
 * il lavoro dell'applicazione. Vedi ex_resize(). */
#define EXM_SIZE      0x000A
/* ! IL PUNTATORE SI E' MOSSO CON UN BOTTONE PREMUTO, e solo allora: un
 * movimento a mano libera non arriva a nessuno. lp porta le coordinate come
 * per EXM_MOUSE_DOWN. Arriva a chi ha ricevuto il bottone giu' — anche quando
 * il puntatore e' finito fuori dalla finestra: il trascinamento appartiene a
 * chi l'ha cominciato. */
#define EXM_MOUSE_MOVE 0x000B
/* ! DUE CLIC VICINI NEL TEMPO E NELLO SPAZIO, sullo stesso punto: e' il
 * toolkit a riconoscerli, non il server — vedi il commento accanto a
 * doppio_clic() in exwin.c. lp porta le coordinate come EXM_MOUSE_DOWN.
 *
 * ! ARRIVA SOLO PER CIO' CHE IL TOOLKIT NON HA GIA' INTERPRETATO. Su una
 * lista il doppio clic diventa EXM_COMMAND con EX_IS_OPEN(lp) a 1, che e' la
 * stessa cosa che dice l'Invio: un'applicazione che gestisce l'Invio ha gia'
 * il doppio clic senza scrivere una riga. */
#define EXM_DOUBLE_CLICK  0x000C
/* ! LA SVEGLIA PERIODICA E' SCATTATA: vedi ex_set_timer(). lp porta i
 * millisecondi dall'avvio, che servono a chi vuole misurare invece di
 * contare i messaggi. Senza questo un'applicazione non puo' fare NIENTE da
 * sola: il ciclo dei messaggi dorme finche' non arriva un evento, e un
 * orologio si aggiornerebbe solo quando l'utente muove il mouse. */
#define EXM_TIMER       0x000D
/* The list of open windows has changed: read it with ex_list_windows().
 * Only the window that called ex_track_windows() receives it. */
#define EXM_WINDOW_LIST    0x000E
/* Ctrl+Alt+Canc was pressed with the graphics on screen (@TASTI-SISTEMA).
 * Only the window that called ex_track_windows() receives it: the desktop,
 * which asks what to do. */
#define EXM_SYSTEM     0x000F  /* wp: 0 = Ctrl+Alt+Canc, 1 = menu di avvio (Windows, Ctrl+Esc) */
/* The mouse wheel turned over this window (28 September 2026). wp is the
 * number of notches as a SIGNED int — positive towards the user, i.e. "the
 * page goes down" — and lp the pointer, as for EXM_MOUSE_DOWN. It arrives only
 * when the toolkit did not use it: a list, a text area or a scroll bar under
 * the pointer scrolls by itself, three lines per notch (EX_WHEEL_LINES). */
#define EXM_WHEEL     0x0010
/* The RIGHT mouse button was pressed on this window (29 September 2026,
 * @MOUSE-DESTRO). lp is the point, as for EXM_MOUSE_DOWN; wp is the control
 * under it, or 0. On a list the row under the pointer is chosen first, so
 * ex_list_get_selected says which one was clicked. The usual answer is
 * ex_popup_menu(). */
#define EXM_RIGHT_CLICK 0x0011
/* Rows of a list were dragged and released over ANOTHER control of the same
 * window (29 September 2026, @FM-TRASCINA). wp: the list's id in the low 16
 * bits, the target control's id in the high 16; lp: the release point, as for
 * EXM_MOUSE_DOWN. Which rows: ex_list_get_selection on the list; where on a target
 * list: ex_list_row_at. */
#define EXM_DROP    0x0012
/* Una scheda di documenti chiede di chiudersi: la sua X, o Ctrl+W. wp = id
 * del controllo, lp = l'indice. Il toolkit NON la toglie: e' il programma
 * che sa se c'e' da salvare, e poi chiama ex_item_remove(). (29 settembre
 * 2026, @TOOLKIT-SCHEDE) */
#define EXM_TAB_CLOSE 0x0013
/* The pointer passing over the window with no button pressed, lp = x | y<<16
 * like the other mouse messages; and leaving it (no coordinates). Only to a
 * window that asked with ex_track_mouse_hover (exwin.so 0.011, wserver 0.008,
 * @EXWIN-PASSAGGIO). ! They come a hundred times a second: a procedure that
 * handles them returns EX_NO_REDRAW unless it has something to redraw,
 * or the toolkit redraws the whole window at every pixel. */
#define EXM_MOUSE_ENTER   0x0014
#define EXM_MOUSE_LEAVE   0x0015
/* The pointer, with no button pressed, has come onto a control (exwin.so
 * 0.015): once when it arrives, not at every movement. wp = the control's id,
 * lp = the control. Only for a window that asked with ex_track_mouse_hover.
 * It is where a program shows a hint (ex_show_popup_pointer) or changes a
 * colour; exide calls <Name>_MouseOver() from it. */
#define EXM_MOUSE_OVER    0x0016
/* The user changed the text of a text box or a text area (exwin.so 0.015):
 * wp = the control's id, lp = the control. Only for a window that asked with
 * ex_notify_changes. For a text area it means «a key that writes or deletes
 * was taken», which is almost always a change. */
#define EXM_CHANGED       0x0017
/* EXM_MOUSE_DOWN e EXM_DOUBLE_CLICK portano in wp i modificatori (KBD_MOD_CTRL,
 * _SHIFT, _ALT) del momento del clic: dal 29 settembre 2026, con wserver
 * 0.006 e exwin.so 0.009. Prima wp era 0. */
#define EX_FROM(wp)       ((unsigned int)(wp) & 0xFFFFu)
#define EX_TO(wp)        ((unsigned int)(wp) >> 16)
#define EX_WHEEL_LINES 3

/* ! THE SIGN IS KEPT (30 September 2026): a drag that leaves the window on
 * the left or at the top goes on sending moves, and there x or y is negative.
 * Read unsigned, -2 became 65534, and the browser's selection jumped from
 * the first word of the line to the last. */
#define EX_X(lp)        ((int)(short)((lp) & 0xFFFF))
#define EX_Y(lp)        ((int)(short)(((lp) >> 16) & 0xFFFF))

/* =============================================================================
 * ! PER UN EXM_COMMAND CHE VIENE DA UNA LISTA, lp DICE **COME**, e sono due
 * cose distinte impacchettate insieme.
 *
 *   EX_IS_OPEN(lp)  1 = si e' chiesto di APRIRE: Invio, oppure doppio clic.
 *                  0 = si e' solo scelto, guardando.
 *   EX_COLUMN(lp)     la colonna del clic dentro la riga, contata in caratteri,
 *                  oppure -1 se il comando e' arrivato dalla tastiera.
 *
 * ! SCEGLIERE E APRIRE SONO DUE DESIDERI DIVERSI. Chi scorre una lista col
 * mouse o con le frecce vuole guardare; chi batte Invio o fa doppio clic ha
 * chiesto di ENTRARE. Senza la distinzione, un file manager con un albero lo
 * aprirebbe sotto le dita di chi voleva solo dare un'occhiata.
 *
 * ! LA COLONNA SERVE A CHI DISEGNA DENTRO LA RIGA. Una lista e' testo, e
 * un'applicazione ci puo' mettere dei segni con un significato loro — il «+»
 * e il «-» dell'albero del file manager. Senza sapere DOVE e' caduto il clic,
 * quel segno si potrebbe solo guardare, mai premere. Il toolkit non sa cosa
 * significhino quei caratteri, e non deve saperlo: dice la colonna e basta.
 *
 * ! DALLA TASTIERA LA COLONNA NON C'E', e si dice -1 invece di zero: zero e'
 * una colonna vera, la prima, e confonderla con «non c'e'» vorrebbe dire che
 * l'Invio si comporta come un clic sul primo carattere della riga.
 *
 * Per un pulsante lp non vuol dire niente: un pulsante lo si preme e basta.
 * ============================================================================= */
#define EX_IS_OPEN(lp)   ((int)((lp) & 1))
#define EX_COLUMN(lp)      ((((lp) >> 8) & 0xFFFF) ? \
                         (int)((((lp) >> 8) & 0xFFFF) - 1) : -1)

/* --- Gli stili ----------------------------------------------------------- */
#define EX_CAPTION       0x0001
#define EX_BORDER        0x0002
#define EX_CLOSEBOX       0x0004
#define EX_VISIBLE     0x0008
#define EX_BACKGROUND       0x0010  /* sta sotto a tutte e non si sposta */
/* ! SOPRA A TUTTE: e' lo sfondo girato dall'altra parte. Serve alla barra
 * delle applicazioni, che dev'essere raggiungibile anche sotto una finestra a
 * schermo intero — altrimenti l'unico modo di tornare al menu sarebbe
 * spostare quella finestra, e a schermo intero non si potrebbe. */
#define EX_TOPMOST        0x0020

/* ! MODALE: finche' e' aperta, le ALTRE finestre di questo programma non
 * ricevono ne' clic ne' tasti. Non e' «sta sopra»: una finestra che copre e
 * basta lascia cliccare quello che si vede intorno, e un «vuoi perdere le
 * modifiche?» a cui si puo' rispondere continuando a scrivere non sta
 * chiedendo niente. Il blocco lo fa il server, che e' l'unico a sapere dove
 * vanno a finire i clic. */
#define EX_MODAL       0x0040
/* ! SI RIDIMENSIONA SOLO CHI L'HA CHIESTO. Con questo bit il server disegna la
 * presa nell'angolo in basso a destra e la finestra si puo' tirare col mouse;
 * senza, non compare nemmeno. Non e' prudenza: una finestra che cambia misura
 * e non sa rifare la propria disposizione mostra i controlli fermi dov'erano
 * dentro un'area piu' grande — cioe' una finestra rotta, fatta rompere dal
 * server. Chi mette questo bit deve gestire EXM_SIZE. */
#define EX_RESIZABLE        0x0080
#define EX_CHILD       0x0100  /* e' un controllo dentro un'altra finestra */
/* ! SPENTO (27 settembre 2026, per Calctor): il controllo c'e', si vede
 * grigio, e non si preme ne' prende il fuoco. Si mette alla creazione o con
 * ex_enable(). Nasconderlo (ex_show) direbbe «non esiste»; spento dice
 * «esiste, ma adesso no» — le cifre 2..9 di una calcolatrice in binario. */
#define EX_DISABLED       0x0200
/* A pop-up window (30 September 2026): a click anywhere outside it sends it
 * EXM_CLOSE (from wserver 0.008). ex_popup_menu uses it. */
#define EX_POPUP     0x0400

/* --- I colori, in ARGB --------------------------------------------------- */
#define EX_BLACK         0x00000000
#define EX_WHITE       0x00FFFFFF
#define EX_GRAY       0x00C0C0C0
#define EX_DARK_GRAY    0x00808080
#define EX_BLUE          0x00305A8A
#define EX_RED        0x00C04040
/* ! LA SCRIVANIA NON E' DELLO STESSO BLU DELLE BARRE DEL TITOLO, e dal 18
 * agosto 2026 nemmeno per sbaglio. Fondo e barra dello stesso colore vogliono
 * dire che il bordo di una finestra a schermo pieno sparisce nello sfondo —
 * e che nessuno, guardando una fotografia dello schermo, sa dire dove
 * finisce una finestra e comincia la scrivania. */
#define EX_DESKTOP_COLOR    0x00204060

/* =============================================================================
 * IL RILIEVO — due righe di luce e due di ombra, e tutto sembra un oggetto
 *
 * ! SONO GLI STESSI NUMERI DI BIANCO E NERO, E HANNO UN NOME LORO APPOSTA. Un
 * pulsante non e' «bianco sopra e nero sotto»: e' «illuminato da sopra a
 * sinistra». Il giorno che la tavolozza cambia — un grigio piu' scuro, un tema
 * — si cambiano questi due e cambiano tutti i controlli insieme; scritti come
 * EX_WHITE ed EX_BLACK bisognerebbe distinguerli uno per uno da quelli che
 * bianco e nero lo sono per davvero.
 *
 * ! E LA LUCE VIENE DA SOPRA A SINISTRA, SEMPRE. E' l'unica convenzione che
 * conta: se due controlli la prendessero da due parti diverse, uno dei due
 * sembrerebbe premuto senza esserlo.
 * ============================================================================= */
#define EX_HIGHLIGHT         0x00FFFFFF
#define EX_SHADOW        0x00000000

/* -----------------------------------------------------------------------------
 * Creare
 *
 * `classe` e' una stringa, come su Win32, e non un enum: aggiungere un
 * controllo non deve voler dire ricompilare chi non lo usa.
 *
 *     "finestra"      di primo livello, la conosce il server
 *     "pulsante"      premibile, manda EXM_COMMAND al padre
 *     "etichetta"     testo e basta
 *     "testo"         casella di testo modificabile
 *     "riquadro"      cornice con un titolo, per raggruppare
 *     "separatore"    una riga
 *     "intestazione"  una fascia di titolo dentro la finestra
 *     "lista"         un elenco che scorre, con una riga scelta
 *     "areatesto"     un'area di testo multiriga, con il cursore
 *     "terminale"     una griglia di testo con dentro un programma
 *     "mdi"           il contenitore: un ripiano su cui stanno le finestre
 *     "mdifiglio"     una finestra dentro un contenitore MDI
 *     "spunta"        una casella da spuntare, con la sua scritta accanto
 *     "radio"         una scelta fra fratelli: accenderne uno spegne gli altri
 *     "scorrimento"   una barra: verticale o orizzontale secondo la FORMA
 *     "combo"         un elenco a discesa: si vede la scelta, si apre l'elenco
 *     "tab"           una fila di linguette, una scelta
 *     "immagine"      un'icona e basta: ornamento, non si clicca
 *
 * ! «immagine» NON PRENDE IL FUOCO E NON MANDA COMANDI, ed e' il suo mestiere:
 * sta li' e si fa guardare. Gli si da' l'icona con ex_set_icon() come a un
 * pulsante, e la disegna QUADRATA E CENTRATA nel riquadro — le icone sono
 * quadrate, il riquadro che si tira no, e allargarle al rettangolo vorrebbe
 * dire deformarle. Prima dell'immagine l'ornamento si faceva con
 * un'«etichetta» dal testo vuoto: funzionava, ed era un trucco da spiegare
 * ogni volta.
 *
 * ! E LA MISURA E' QUELLA DEL CONTROLLO: il `lato` passato a ex_set_icon()
 * qui non conta, perche' l'icona NON sta accanto a una scritta — e' tutto il
 * controllo. Si tira il riquadro, e l'immagine lo riempie.
 *
 * ! CHI VUOLE UNA FIGURA CHE SI CLICCA USA UN'«etichetta» CON L'ICONA: quella
 * ha l'evento Clic, e un'etichetta con icona ed evento Clic e' esattamente un
 * collegamento. Un controllo in piu' per chiamarlo con un altro nome sarebbe
 * un controllo che fa quel che ne fa gia' uno.
 *
 * Per una finestra di primo livello: `padre` = 0, `id` = 0, `proc` = la
 * procedura. Per un controllo: `padre` = la finestra, `id` = il numero con cui
 * lo riconoscerai in EXM_COMMAND, `proc` = 0.
 * --------------------------------------------------------------------------- */
/* ! EX_AUTO COME x E y VUOL DIRE «METTILA TU», ED E' CIO' CHE PERMETTE DI
 * APRIRE DUE VOLTE LO STESSO PROGRAMMA. Una finestra che nasce sempre nello
 * stesso punto va bene finche' e' una sola: la seconda copia si sovrappone
 * alla prima ESATTAMENTE, e chi guarda crede che non si sia aperta — un
 * difetto che non da' nessun messaggio d'errore.
 *
 * La posizione la sceglie il SERVER, che e' l'unico a sapere quante finestre
 * ci sono gia': le mette a cascata, con lo scostamento giusto perche' della
 * finestra sotto resti visibile la barra del titolo. */
#define EX_AUTO         (-1)

ExWindow ex_create(const char *classe, const char *titolo, unsigned int stile,
                   int x, int y, int w, int h,
                   ExWindow padre, unsigned int id, ExWindowProc proc);

/* =============================================================================
 * ! AGGIUNGERE UN CONTROLLO E' UNA COSA CHE SI FA, e i posti da toccare sono
 * sette: stanno elencati in exwin.c, sopra i numeri CL_*. Qui basta sapere che
 * la classe e' una stringa apposta — un programma gia' compilato continua a
 * funzionare quando la libreria ne impara una nuova, perche' non c'e' nessun
 * enum condiviso da tenere allineato.
 * ============================================================================= */
void       ex_destroy(ExWindow f);
void       ex_set_title(ExWindow f, const char *s);
void       ex_move(ExWindow f, int x, int y);
void       ex_show(ExWindow f, int visibile);

/* -----------------------------------------------------------------------------
 * Visible and enabled, for every control (exwin.so 0.014, 7 October 2026)
 *
 *   ex_set_visible(c, 0)   hides the control without destroying it: it is not
 *                          drawn, gets no clicks, Tab skips it. Its text, rows
 *                          and selection stay; ex_set_visible(c, 1) brings it
 *                          back as it was.
 *   ex_set_enabled(c, 0)   the control is shown, dimmed, with whatever the
 *                          program puts in it, but the user cannot press it,
 *                          type in it or give it the focus: read-only for the
 *                          user, still writable by the program (ex_set_text...).
 *
 * Every control is born visible and enabled. The window is drawn again by
 * itself when the message handler returns, as for ex_set_text.
 * (ex_show and ex_enable are the same two switches under their older names.)
 * --------------------------------------------------------------------------- */
void       ex_set_visible(ExWindow c, int si);
int        ex_is_visible(ExWindow c);
void       ex_set_enabled(ExWindow c, int si);
int        ex_is_enabled(ExWindow c);

/* -----------------------------------------------------------------------------
 * Cambiare misura
 *
 * ! SU UN CONTROLLO CAMBIA SUBITO; SU UNA FINESTRA DI PRIMO LIVELLO E' UNA
 * RICHIESTA, e la differenza va saputa. Una finestra e' una zona di memoria
 * condivisa creata dal server: la misura nuova arriva quando il server ha
 * finito di preparare la zona nuova, e arriva come EXM_SIZE. Leggere w e h
 * subito dopo aver chiamato questa darebbe ancora quelli di prima — e la
 * misura concessa puo' anche non essere quella chiesta, se non ci sta nello
 * schermo.
 *
 * ! SU UN TERMINALE, UNA LISTA O UN'AREA RIFA' ANCHE LA GEOMETRIA DI DENTRO:
 * colonne e righe, la finestra visibile sul testo, e per il terminale anche la
 * misura del pty — che e' cio' che permette a un programma a schermo pieno di
 * accorgersene. Senza, si otterrebbe un controllo grande il doppio con dentro
 * ancora 80x25.
 * --------------------------------------------------------------------------- */
void       ex_resize(ExWindow f, int w, int h);

/* -----------------------------------------------------------------------------
 * Il fuoco: chi riceve i tasti dentro una finestra
 *
 * ! SENZA QUESTA, IL FUOCO ANDAVA AL PRIMO CONTROLLO CREATO che lo accettasse,
 * e per spostarlo bisognava creare i controlli in un ordine che non e' quello
 * in cui si leggono. Chiedere il fuoco per un controllo che non lo accetta —
 * un'etichetta, un separatore — non e' un errore: e' un no, e le cose restano
 * come stanno.
 * --------------------------------------------------------------------------- */
void        ex_set_focus(ExWindow controllo);

/* ! IL FUOCO A NESSUNO, e serve a chi disegna i propri controlli. `ex_set_focus`
 * lo puo' dare solo a un controllo del toolkit — un oggetto che lo accetta —
 * quindi non c'e' modo di dire «da qui in avanti i tasti li voglio io».
 * Il browser ne aveva bisogno: i controlli di un modulo HTML sono rettangoli
 * disegnati, non finestre, e finche' la casella dell'indirizzo teneva il fuoco
 * ogni lettera battuta dentro un modulo finiva nella barra dell'indirizzo.
 *
 * Dopo questa chiamata i tasti arrivano alla procedura della finestra come
 * EXM_KEY, e chi li vuole se li gestisce. */
void        ex_clear_focus(ExWindow finestra);

/* ! CHI HA IL FUOCO ADESSO, e l'ha chiesto il navigatore quando nella barra
 * sono comparse DUE caselle: l'indirizzo e la ricerca. Invio arriva
 * all'applicazione come EXM_KEY — la casella lascia passare Invio apposta —
 * ma il messaggio non dice da quale casella arrivi, e il fuoco lo sa solo il
 * toolkit. Senza questa, l'unico modo era indovinarlo guardando quale testo e'
 * cambiato: cioe' sbagliarlo il giorno che uno cerca due volte la stessa cosa.
 *
 * Rende il controllo che ha il fuoco dentro `finestra` (la radice: il fuoco e'
 * della finestra di primo livello, non del singolo controllo), oppure 0 se non
 * ce l'ha nessuno — che e' anche il caso dopo ex_clear_focus. */
ExWindow  ex_get_focus(ExWindow finestra);

/* ! SPEGNE LA SCRIVANIA INTERA, non questa finestra. Chiede al server di
 * mandare a ogni applicazione la stessa chiusura della crocetta, di aspettare
 * che se ne vadano, di rimettere il modo testo e di morire.
 *
 * Lo chiama il program manager quando si sceglie «Esci»: prima quella voce
 * chiudeva solo la scrivania e lasciava la grafica accesa senza nessuno
 * dentro. Un programma qualunque non ha motivo di chiamarla. */
void        ex_shutdown_desktop(void);

/* =============================================================================
 * THE OPEN WINDOWS, FOR A TASKBAR (@FIN-ICONA, 26 September 2026)
 *
 * ex_track_windows(f): from now on the server sends the list of open
 * windows whenever it changes, and f receives EXM_WINDOW_LIST each time. The
 * first list arrives at once. One follower per desktop: the last to ask.
 *
 * ex_list_windows(): copies the last list received into v, at most max
 * entries, in creation order; returns how many there are.
 *
 * ex_activate_window_id(id): restore it if minimized, bring it to the front,
 * give it the focus. ex_minimize_window_id(id): minimize it — the same as the
 * «_» button the server draws next to the close button.
 *
 * ! THE ids ARE THE SERVER'S, not ExWindow handles: the windows belong to
 * other programs. The pid says whose, and is how a taskbar finds the icon.
 * ============================================================================= */
#define EX_WE_MINIMIZED   0x0001      /* minimized */
#define EX_WE_FOCUSED     0x0002      /* has the keyboard focus */

typedef struct {
    unsigned int id;
    unsigned int pid;
    union { unsigned int state; unsigned int stato; };   /* EX_WE_* */
    union { char title[48]; char titolo[48]; };
} ExWindowEntry;

void        ex_track_windows(ExWindow f);
int         ex_list_windows(ExWindowEntry *v, int max);
void        ex_activate_window_id(unsigned int id);
/* The same for one of this program's own windows, by handle: to the front,
 * with the keyboard focus (28 September 2026). A program that opens a side
 * window — Calctor's tape — gives the focus back to the main one with this.
 * Optional in the stub: over an older exwin.so it does nothing. */
void        ex_activate(ExWindow f);

/* A pop-up menu at (x, y) of window f's client area (where the right button
 * was pressed): `n` items, returns the index chosen - click or Enter - or -1
 * for Esc or a closed window (a click on another of the program's windows
 * only brings the menu back to the front: it is modal). Modal for the program, like
 * a dialog. An item "-" is a separator line and cannot be chosen.
 * (29 September 2026, @MOUSE-DESTRO; optional in the stub: -1 over an older
 * exwin.so.) */
int         ex_popup_menu(ExWindow f, int x, int y,
                             const char *const *voci, int n);

/* Opens a file with the program /exwin/lib/tipi.txt associates to its
 * extension (the editor for what is not listed), from /cdrom too when the
 * system runs from the CD. Returns the pid, or -1; `prog` (may be 0) gets the
 * program chosen. The one place that reads tipi.txt: the file manager, the
 * desktop and the others call this. (29 September 2026, @ASSOCIAZIONI;
 * optional in the stub: -1 over an older exwin.so.) */
int         ex_open_file(const char *percorso, char *prog, unsigned int max);

/* MULTIPLE CHOICE in a list (29 September 2026, @LISTA-MULTI): switched on
 * per list. Ctrl+click adds or removes a row, Shift+click takes the range
 * from the last click, a plain click takes one row - unless it is already
 * chosen, so that the group can be dragged away. ex_list_get_selection writes the
 * chosen rows (at most `max`) and returns how many; with the multiple choice
 * off, or nothing marked, it is the current row alone. ex_list_row_at is
 * the row under the window y, or -1. All three optional in the stub. */
void        ex_list_set_multiselect(ExWindow lista, int si);
int         ex_list_get_selection(ExWindow lista, unsigned int *righe, int max);
int         ex_list_row_at(ExWindow lista, int y);

void        ex_minimize_window_id(unsigned int id);

/* EXM_MOUSE_ENTER and EXM_MOUSE_LEAVE for this window: 1 to receive them, 0
 * to stop. Over an older server nothing arrives, which is the same as not
 * asking: a program must work without them. */
void        ex_track_mouse_hover(ExWindow f, int si);

/* EXM_CHANGED for the text boxes and text areas of this window: 1 to receive
 * it, 0 to stop (exwin.so 0.015). */
void        ex_notify_changes(ExWindow f, int si);

/* A small box with `text` next to the pointer, inside the window the pointer
 * is over (exwin.so 0.015). The three colours are strings "red,green,blue"
 * (0..255); "0", "" or NULL means the default: a pale, almost beige yellow
 * background, black text, dark grey border. `text` may hold up to four lines
 * separated by a newline. The box opens below and to the right of the
 * pointer, and on the other side when it would not fit. It goes away by
 * itself when the pointer moves to another control or leaves the window, at a
 * click, at a key; ex_hide_popup_pointer() removes it at once. Meant to be
 * called from EXM_MOUSE_OVER (in exide: from <Name>_MouseOver). */
void        ex_show_popup_pointer(const char *text, const char *background,
                                  const char *foreground, const char *border);
void        ex_hide_popup_pointer(void);

/* Draws ONE control again and shows only its rectangle, without the window
 * around it: a label (a status line), a terminal, a text area, a list, when
 * it is a direct child of its window and nothing created after it lies on
 * top. 1 = done; 0 = it could not, and the caller redraws the window. Over
 * an older exwin.so it always says 0. (exwin.so 0.011) */
int         ex_redraw(ExWindow c);

/* A menu entry with a check mark (exwin.so 0.011): after ex_menu_add_item, this
 * makes the entry `id` one that shows a mark, on or off. The click still
 * arrives as EXM_COMMAND: the program flips its setting and calls this again.
 * Over an older exwin.so the entry is a plain one. */
void        ex_menu_check(ExWindow menu, unsigned int id, int acceso);

/* Drawing in window f stays inside (x, y, w, h) until ex_set_clip(f, 0, 0, 0, 0)
 * (30 September 2026). For a program that draws a part of its window itself
 * (a canvas, a page): set it, draw, take it away before returning. Over an
 * older exwin.so it does nothing. */
void        ex_set_clip(ExWindow f, int x, int y, int w, int h);

/* As ex_pixmap, but each pixel is blended over what is there by its alpha
 * (255 opaque, 0 not drawn). Over an older exwin.so it is ex_pixmap. */
void        ex_pixmap_blend(ExWindow f, int x, int y, int w, int h,
                           const unsigned int *px, unsigned int passo);

/* Asks every program but this one to close, as with their X button; the
 * server stays on. Used by the desktop to shut down with the graphics still
 * on (@GRAFICA-MODALE): the list of ex_track_windows() then says who is
 * left. */
void        ex_close_others(void);

/* -----------------------------------------------------------------------------
 * Gli appunti, senza passare da `ex_area`
 *
 * ! SERVONO A CHI DISEGNA I PROPRI CONTROLLI. `ex_textarea_copy` e compagni
 * lavorano sul controllo `ex_area` del toolkit, e vanno benissimo per chi lo
 * usa; ma i campi di un modulo HTML nel browser sono RETTANGOLI DISEGNATI, non
 * `ex_area` — e senza queste due funzioni la scrivania avrebbe avuto gli
 * appunti dappertutto tranne che dentro una pagina web.
 *
 * ! E SONO LE STESSE DI `ex_area`, non una seconda copia: la zona di memoria
 * condivisa e' una sola, quindi si taglia da una casella del browser e si
 * incolla in un editor, e viceversa. Un secondo blocco di appunti sarebbe
 * stato la cosa peggiore — due «ultimi copiati» e nessun modo di sapere quale.
 *
 * `ex_clipboard_set` rende quanti byte ha preso; `ex_clipboard_get` quanti ne
 * ha messi in `out` (sempre terminato da zero). Zero vuol dire appunti vuoti,
 * o nessuna zona condivisa — che succede quando non c'e' nessuna applicazione
 * grafica, e allora non si muore: si continua senza appunti.
 * --------------------------------------------------------------------------- */
unsigned int ex_clipboard_set(const char *testo, unsigned int n);
unsigned int ex_clipboard_get(char *out, unsigned int max);

/* Il testo di un controllo: lo legge una casella, lo cambia un'etichetta. */
void        ex_set_text(ExWindow f, const char *s);
const char *ex_get_text(ExWindow f);

/* -----------------------------------------------------------------------------
 * Il ciclo dei messaggi
 *
 * ! ex_get_message() DORME DAVVERO mentre non succede niente: dentro c'e'
 * poll() su FD_IPC, non un giro a vuoto. Rende 0 quando l'applicazione ha
 * chiesto di uscire.
 * --------------------------------------------------------------------------- */
int  ex_get_message(ExMsg *m);

/* ! COME ex_get_message MA NON DORME: rende 0 se in questo momento non c'e'
 * niente da fare. Serve a chi sta gia' aspettando altro — una risposta dalla
 * rete, per dirne una — e vuole restare vivo senza rinunciare a quel che sta
 * facendo: dormendo qui dentro, le attese diventerebbero due.
 *
 * ! E CHI LA USA DEVE SAPERE CHE RIENTRA IN CASA PROPRIA. I messaggi si
 * smistano alla procedura della finestra, che e' la stessa che sta girando in
 * quel momento: se quella procedura puo' far ripartire il lavoro che si sta
 * aspettando, chi chiama deve impedirglielo con una bandiera. Non e' un
 * dettaglio da scoprire dopo. */
int  ex_peek_message(ExMsg *m);
void ex_dispatch(const ExMsg *m);
void ex_quit(int codice);

/* Cio' che la procedura deve chiamare per tutto quello che non gestisce:
 * disegna i controlli, ridisegna la cornice, chiude su EXM_CLOSE.
 *
 * ! WHAT YOUR WINDOW DRAWS ITSELF BELONGS IN YOUR OWN EXM_PAINT, not only in
 * the function that changes it. This call wipes the client area and knows
 * nothing about the header row, the status line or the path label you paint by
 * hand: anything that brings your window back to the front erases them, and
 * what brings it back is the server itself, every time a dialog opened on top
 * of it closes.
 *
 * So the shape that works is this one, and the redraw helper goes through the
 * procedure rather than around it:
 *
 *     case EXM_PAINT:
 *         ex_default_proc(f, msg, wp, lp);
 *         la_mia_roba();
 *         return 0;
 *
 * ! IT IS WRITTEN HERE BECAUSE IT WAS PAID FOR TWICE IN ONE DAY, on 22
 * September 2026: the path line of ExDlg vanished the instant it changed, the
 * defect was found and written down, and a few hours later a brand new program
 * lost its header row to the same cause. A lesson kept in the file where it
 * was learnt is a lesson the next file does not read. */
long ex_default_proc(ExWindow f, unsigned int msg, unsigned int wp, long lp);

/* -----------------------------------------------------------------------------
 * Disegnare dentro una finestra
 *
 * Le coordinate sono relative all'area del client. Si disegna nella memoria
 * condivisa della finestra: il server non vede queste chiamate, vede solo il
 * risultato.
 * --------------------------------------------------------------------------- */
void ex_fill_rect(ExWindow f, int x, int y, int w, int h, unsigned int c);
void ex_draw_rect(ExWindow f, int x, int y, int w, int h, unsigned int c);

/* -----------------------------------------------------------------------------
 * Il rilievo: quello che rende un pulsante un pulsante
 *
 *     ex_draw_raised   SPORGE:   luce sopra e a sinistra, ombra sotto e a destra
 *     ex_draw_sunken    RIENTRA:  il contrario
 *
 * ! DISEGNANO SOLO LE QUATTRO RIGHE, NON IL FONDO. Chi chiama ha gia' riempito
 * l'area del colore che vuole — grigio per un pulsante, bianco per una casella
 * — e un fondo disegnato due volte e' un fondo che lampeggia.
 *
 * ! E LA REGOLA E' UNA SOLA: SPORGE CIO' CHE SI PREME, RIENTRA CIO' IN CUI SI
 * SCRIVE. Un pulsante sporge e rientra quando lo si preme; una casella di
 * testo, una lista, un'area rientrano sempre — sono buchi nel pannello, non
 * cose da premere. Un elenco che sporgesse inviterebbe a cliccarlo come un
 * pulsante.
 * --------------------------------------------------------------------------- */
void ex_draw_raised(ExWindow f, int x, int y, int w, int h);
void ex_draw_sunken(ExWindow f, int x, int y, int w, int h);
void ex_draw_text(ExWindow f, int x, int y, const char *s, unsigned int c);

/* =============================================================================
 * I FONT — piu' di uno, e non solo per il browser
 *
 * ! ZERO E' IL FONT DI SISTEMA, e c'e' SEMPRE. E' l'8x16 compilato dentro il
 * toolkit: non sta in un file, non si puo' chiudere, e non puo' mancare. Un
 * programma che non chiede niente scrive con quello, cioe' si comporta come
 * prima che i font esistessero.
 *
 * ! E CHI NON TROVA IL SUO FONT NON MUORE: ex_font_open() rende 0, che e' il
 * font di sistema. Il testo esce con un carattere diverso da quello voluto e
 * il programma continua — che e' l'unica risposta ragionevole per una
 * decorazione. Chi vuole accorgersene guarda il valore.
 *
 * ! LA MISURA DI UNA STRINGA SI CHIEDE, NON SI CALCOLA. `strlen(s) * 8` e'
 * vero solo finche' il font e' quello di sistema: e' il conto che va tolto da
 * ogni impaginazione, ed e' la ragione per cui questa parte arriva PRIMA del
 * browser e non dopo.
 * ============================================================================= */
typedef unsigned int ExFont;

#define EX_FONT_SYSTEM 0

/* Apre un font e rende il manico, o 0 — che vuol dire «userai il font di
 * sistema».
 *
 * `corpo` e' l'altezza voluta in PIXEL. Zero vuol dire «quella che il font ha
 * per natura», che per una bitmap e' l'unica possibile.
 *
 * ! IL CORPO STA NELLA FIRMA DA SUBITO, ANCHE SE OGGI I FONT SONO BITMAP e
 * quindi si ignora. E' l'unico parametro che un font scalabile chiede e una
 * bitmap no: aggiungerlo dopo vorrebbe dire o una seconda funzione accanto a
 * questa — due modi di fare la stessa cosa, di cui uno sbagliato — o cambiare
 * la firma quando le applicazioni la useranno gia'. Costa una riga adesso.
 *
 * ! E IL FORMATO SI RICONOSCE DAI PRIMI BYTE, non dall'estensione. Un file di
 * font arriva anche dalla rete, e li' il nome lo sceglie chi sta dall'altra
 * parte. */
ExFont ex_font_open(const char *percorso, int corpo);

/* =============================================================================
 * TROVARE UN CARATTERE PER FAMIGLIA, invece che per percorso
 *
 * ! IL PERCORSO SCRITTO NEL PROGRAMMA E' UNA DIPENDENZA NASCOSTA. Ogni
 * applicazione che voleva il grassetto si portava dentro
 * «/exwin/font/LiberationSans-Bold.ttf»: il giorno che quel file cambia nome,
 * o che accanto ne arriva un altro, vanno ritoccati tutti i programmi — e
 * quello che si dimentica non da' un errore, da' un carattere diverso.
 *
 * ! E LE QUATTRO FACCE DI UNA FAMIGLIA NON SONO QUATTRO FAMIGLIE. Chiedere
 * «serif, grassetto, corsivo» e' cio' che un foglio di stile dice davvero;
 * comporre il nome del file da quei tre pezzi e' un lavoro che va fatto in un
 * posto solo, o il browser e l'editor lo faranno in due modi diversi.
 *
 * ! CHI NON TROVA RIPIEGA, E NON MUORE MAI: la faccia chiesta, poi la normale
 * della stessa famiglia, poi il sans, poi 0 — che e' il font di sistema. Una
 * finestra con un carattere diverso e' meglio di una finestra vuota.
 *
 * I font aperti restano in una riserva: chiedere due volte lo stesso non
 * riapre il file. Non si chiudono con ex_font_close().
 * ========================================================================== */
#define EX_FAMILY_SERIF    0
#define EX_FAMILY_SANS     1
#define EX_FAMILY_MONO     2

ExFont ex_font_find(int famiglia, int corpo, int grassetto, int corsivo);

/* Writes `s` INTO an ARGB bitmap of w x h pixels (not on a window), with the
 * font's glyphs and the same antialiasing as ex_draw_text_font; y is the top of
 * the line, and touched pixels become opaque. For programs that draw text
 * into a picture - Pennello's Text tool (28 September 2026). Optional in the
 * stub: over an older exwin.so it draws nothing. */
void        ex_draw_text_buffer(unsigned int *px, int w, int h, ExFont f, int x, int y,
                         const char *s, unsigned int c);

/* Il nome del file che ex_font_find userebbe, senza aprirlo: serve a chi
 * vuole DIRE quale carattere sta usando — o quale non ha trovato. */
const char *ex_font_name(int famiglia, int grassetto, int corsivo);
void   ex_font_close(ExFont f);

/* L'interlinea, e la distanza fra la cima e la linea di base. La seconda serve
 * a chi mette due font diversi sulla stessa riga: si allineano le BASI, non le
 * cime, o le lettere ballerebbero. */
int    ex_font_height(ExFont f);
int    ex_font_baseline(ExFont f);

/* Quanti pixel occupa `s` scritto con quel font. */
int    ex_text_width(ExFont f, const char *s);

/* Come ex_draw_text, ma con un font scelto. `ex_draw_text` e' questa con f = 0. */
void   ex_draw_text_font(ExWindow w, ExFont f, int x, int y,
                     const char *s, unsigned int c);

/* Chiede EXM_TIMER ogni `ms` millisecondi per questa finestra; 0 smette.
 *
 * ! LA RISOLUZIONE VERA E' 200 ms, la scadenza del poll dentro il ciclo dei
 * messaggi: chiedere 50 ne da' 200. Per un orologio al secondo il ritardo
 * massimo e' un quinto di secondo; per un'animazione fluida serve un'altra
 * cosa, non questa. */
void   ex_set_timer(ExWindow f, unsigned int ms);

/* A descriptor watched by the message loop: `fn` is called, from inside
 * ex_get_message, when there is something to read on `fd` or its other end has
 * closed. fn == 0 stops watching it. Up to 16. It is how a program keeps
 * reading a pipe — a child process, a download — while its windows stay alive,
 * with no timer and no window needed. (23 September 2026) */
typedef void (*ExWatchProc)(void *dato, int fd);
int    ex_watch_fd(int fd, ExWatchProc fn, void *dato);

/* Tab on the last control (or with nothing focused) leaves the toolkit and
 * reaches the window procedure as EXM_KEY, so an application that draws its
 * own focusable things — the browser's page — can take it. Off by default;
 * the application gives the focus back with ex_set_focus(). Shift+Tab is not
 * affected. See exwin.c. */
void   ex_tab_content(ExWindow finestra, int si);
void ex_update(ExWindow f);     /* «ho finito»: lo dice al server */

/* -----------------------------------------------------------------------------
 * Posare un rettangolo di pixel gia' pronti, in ARGB a 32 bit.
 *
 * ! SENZA, UN'IMMAGINE SI DISEGNA UN PIXEL PER CHIAMATA — e per 800x600 sono
 * 480000 chiamate, ognuna con il suo controllo dei limiti e, attraverso la
 * libreria condivisa, anche un salto indiretto.
 *
 * `passo` e' quanti pixel ci sono fra l'inizio di una riga e la successiva:
 * serve a posare un RITAGLIO senza ricopiare. Zero vuol dire «largo quanto w».
 * --------------------------------------------------------------------------- */
void ex_pixmap(ExWindow f, int x, int y, int w, int h,
               const unsigned int *pixel, unsigned int passo);

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
int ex_draw_image(ExWindow f, const char *percorso, int x, int y);

/* -----------------------------------------------------------------------------
 * UN'IMMAGINE DENTRO UN RIQUADRO (27 settembre 2026, per lo sfondo di pm)
 *
 * Riempie il riquadro x,y,w,h con l'immagine disposta secondo `modo`:
 *
 *     EX_IMAGE_TOPLEFT   com'e', nell'angolo in alto a sinistra (= ex_draw_image)
 *     EX_IMAGE_CENTER   com'e', al centro; se e' piu' grande si vede il mezzo
 *     EX_IMAGE_STRETCH  stirata a coprire tutto il riquadro, senza badare alle
 *                     proporzioni (come «Estendi» di Windows)
 *     EX_IMAGE_TILE   ripetuta a piastrelle dall'angolo
 *
 * Dove l'immagine non arriva resta `sfondo`. L'immagine e' OPACA, come in
 * ex_draw_image: l'alfa non si fonde. Si legge con gli stessi lettori delle
 * icone (BMP qui, il resto in eximg.so). Rende 1 se l'ha disegnata, 0 se non
 * la sa leggere.
 *
 * ! IL RIQUADRO SI COMPONE IN MEMORIA E SI POSA UNA VOLTA: per 800x600 sono
 * 1,9 MB presi e resi dentro la chiamata. Pezzo per pezzo, una piastrella da
 * 16 pixel su uno schermo intero sarebbero duemila ex_pixmap.
 * --------------------------------------------------------------------------- */
#define EX_IMAGE_TOPLEFT   0
#define EX_IMAGE_CENTER   1
#define EX_IMAGE_STRETCH  2
#define EX_IMAGE_TILE   3

int ex_draw_image_mode(ExWindow f, const char *percorso, int x, int y,
                        int w, int h, int modo, unsigned int sfondo);

/* -----------------------------------------------------------------------------
 * LE ICONE — un'immagine che si tiene, e si ridisegna a qualunque misura
 *
 * ! UN'ICONA NON E' UN'IMMAGINE DISEGNATA UNA VOLTA, ed e' per questo che ha
 * funzioni sue. Un'immagine si posa e si dimentica; un'icona sta in un menu che
 * si ridisegna a ogni apertura, in una riga di un elenco che scorre, su un
 * pulsante che si preme: decodificarla a ogni disegno vorrebbe dire leggere un
 * file e aprire eximg.so venti volte per aprire un menu. Qui si apre una volta,
 * si tiene, e si disegna quante volte serve.
 *
 * ! E SI RIDIMENSIONA, PERCHE' I FILE NON SONO DELLA MISURA CHE SERVE. Le icone
 * di questo sistema nascono a 64 e 128 pixel; una voce di menu e' alta 24 e una
 * riga di elenco 16. Senza un riduttore l'unica scelta sarebbe disegnarle
 * grandi come sono - cioe' non usarle - o chiedere a chi le disegna una copia
 * per ogni posto in cui compariranno, che e' un lavoro che si rifa' ogni volta
 * che una misura cambia. Il riduttore fa la MEDIA dei pixel che collassano in
 * uno solo: prendere quello in mezzo (il "piu' vicino") su un rimpicciolimento
 * di quattro volte butta quindici pixel su sedici, e i bordi sottili spariscono
 * a chiazze.
 *
 * ! IL COLORE DI FONDO SI PASSA, E NON E' UNA SCOMODITA' EVITABILE. Le icone
 * hanno l'alfa - i bordi sono semitrasparenti, e senza alfa un tondo dentro un
 * quadrato ha gli angoli neri - ma ex_pixmap() posa i pixel e basta: e' una
 * memcpy, non una fusione. Finche' il server non sapra' fondere, chi disegna
 * deve dire SU CHE COSA: il giorno che lo sapra', questo parametro diventera'
 * facoltativo e niente di cio' che e' scritto oggi cambiera' comportamento.
 *
 *     ExIcon ic = ex_icon_open("/exwin/icon/baseapp/filemgr_64.ico");
 *     ex_icon_draw(f, ic, 6, y, 16, EX_GRAY);    // 64 -> 16
 *
 * `ex_icon_open` rende 0 se il file non c'e' o se nessun lettore lo riconosce,
 * e 0 e' un'icona che non si disegna: chi non ha un'icona passa 0 e non deve
 * scrivere un `if`. E' la stessa idea del font 0 di ex_draw_text_font().
 * --------------------------------------------------------------------------- */
typedef unsigned int ExIcon;

ExIcon      ex_icon_open(const char *percorso);

/* Il lato dell'icona com'e' nel file, in pixel. 0 se l'icona non c'e'. */
unsigned int ex_icon_size(ExIcon ic);

/* La disegna in un quadrato di `lato` pixel, fondendola su `sfondo`. Rende 1,
 * o 0 se l'icona non c'e' - e allora non ha disegnato niente, che e' cio' che
 * permette di chiamarla senza controllare. */
int          ex_icon_draw(ExWindow f, ExIcon ic, int x, int y,
                              unsigned int lato, unsigned int sfondo);

/* ! CHIUDERE UN'ICONA E' RARO, e la maggior parte dei programmi non lo fara'
 * mai: un menu tiene le sue finche' vive. Esiste per chi ne carica di nuove a
 * ogni cambio di directory - un file manager - e senza la tavola si
 * riempirebbe. */
/* Attacca un'icona a un CONTROLLO — un pulsante, un'etichetta — che da quel
 * momento se la disegna da se', a sinistra della scritta. `lato` a 0 la fa
 * scegliere al controllo, sull'altezza che ha.
 *
 * ! UN PULSANTE CON L'ICONA RESTA UN PULSANTE: si preme, si sposta, si
 * ridisegna, e l'icona lo segue. Disegnarla sopra da fuori vorrebbe dire
 * rifarla a ogni ridisegno del padre e ricordarsi di spostarla a mano. */
void         ex_set_icon(ExWindow controllo, ExIcon ic, unsigned int lato);

void         ex_icon_close(ExIcon ic);

/* =============================================================================
 * IL CONTENITORE MDI — finestre dentro una finestra
 *
 *     ExWindow cont = ex_create("mdi", "", EX_CHILD, 0, 24, 800, 500, f, 0, 0);
 *     ExWindow ed   = ex_create("mdichild", "sorgente.c",
 *                               EX_CAPTION | EX_CLOSEBOX,
 *                               10, 10, 400, 300, cont, 0, proc_editor);
 *     ex_create("codearea", "", EX_CHILD, 4, 4, 380, 260, ed, ID_COD, 0);
 *
 * ! UNA FINESTRA FIGLIA NON E' UNA FINESTRA DEL SERVER: e' un controllo, con i
 * suoi pixel dentro la zona del padre e disegnato dalla libreria. Da questo
 * discende tutto il resto — non puo' uscire dal contenitore (e trascinandola si
 * ferma al bordo), non compare nella barra delle applicazioni, e se ne possono
 * aprire quante ne stanno negli oggetti senza chiedere niente al server.
 *
 * ! LA MISURA E' QUELLA DI FUORI, telaio compreso, e QUI E' DIVERSO da una
 * finestra di primo livello — dove w e h sono l'area del client e il telaio lo
 * aggiunge il server intorno. La ragione e' che una finestra figlia si trascina
 * e si deve fermare al bordo del contenitore: con la misura di dentro, «ci
 * sta?» vorrebbe dire sommare il telaio a ogni confronto. Il client e' 4 pixel
 * piu' stretto e 24 piu' basso.
 *
 * ! I CONTROLLI DENTRO PARTONO DALL'AREA DEL CLIENT, come in qualunque
 * finestra: un pulsante a (10,10) sta a dieci pixel dal bordo di dentro, non
 * sotto la barra del titolo.
 *
 * ! E I COMANDI DEI SUOI CONTROLLI VANNO ALLA SUA PROCEDURA, non a quella
 * dell'applicazione: e' cio' che rende l'MDI utile invece che decorativo — ogni
 * finestra si occupa dei suoi controlli, senza una procedura sola che smisti
 * gli id di tutte. Se la finestra figlia non ha una procedura, i comandi vanno
 * all'applicazione come sempre.
 *
 * ! IL PULSANTE DI CHIUSURA MANDA EXM_CLOSE ALLA FINESTRA FIGLIA, e chi non lo
 * gestisce se la vede distrutta — NON esce dal programma, come farebbe una
 * finestra vera. Chi ha da salvare intercetta quel messaggio.
 *
 * ! TAB GIRA DENTRO LA FINESTRA ATTIVA. In un MDI i controlli sono quelli di
 * tutte le finestre messi insieme, e un Tab che ne uscisse lascerebbe un
 * cursore che lampeggia in una finestra che non si sta guardando.
 * ============================================================================= */
ExWindow ex_mdi_get_active(ExWindow contenitore);
void       ex_mdi_activate(ExWindow figlio);

/* -----------------------------------------------------------------------------
 * La spunta e il radio: acceso o spento
 *
 * ! DUE CONTROLLI, DUE FUNZIONI, ed e' voluto: da fuori sono la stessa domanda.
 * La differenza sta in cosa succede agli ALTRI — accendere un radio spegne i
 * suoi fratelli, cioe' i controlli "radio" che hanno lo STESSO PADRE. Due
 * gruppi nella stessa finestra si fanno con due "riquadro", che e' anche come
 * si disegnano: la cornice che si vede E' il gruppo che vale.
 *
 * ! UN RADIO NON SI SPEGNE CLICCANDOLO, una spunta si'. «Nessuno dei tre» non
 * e' una risposta che un gruppo di radio sappia dare; ex_set_checked(c, 0) lo puo'
 * fare da programma, dove chi lo scrive sa cosa sta facendo.
 *
 * Il clic e la barra spaziatrice mandano EXM_COMMAND con l'id del controllo, e
 * in `lp` c'e' il valore NUOVO — cosi' chi gestisce il messaggio non deve
 * richiamare ex_is_checked per sapere cos'e' appena successo.
 * --------------------------------------------------------------------------- */
int  ex_is_checked(ExWindow c);
void ex_set_checked(ExWindow c, int acceso);

/* Accende (1) o spegne (0) un controllo: vedi EX_DISABLED. Uno spento che aveva
 * il fuoco lo passa al successivo. Il ridisegno lo chiede chi chiama. */
void ex_enable(ExWindow c, int si);

/* -----------------------------------------------------------------------------
 * La barra di scorrimento
 *
 * ! L'ORIENTAMENTO LO DICE LA FORMA: piu' larga che alta e' orizzontale, piu'
 * alta che larga e' verticale. Un bit di stile in piu' si potrebbe mettere in
 * disaccordo con la misura, e allora bisognerebbe decidere chi ha ragione.
 *
 * ! IL MODELLO E' `massimo` PIU' `pagina`. Il valore va da 0 a `massimo`;
 * `pagina` e' quanto se ne vede in una volta, e serve a due cose che si vedono
 * tutt'e due: quanto e' lungo il cursore — cioe' quanto e' lungo il documento,
 * a colpo d'occhio — e di quanto si salta cliccando nella gola. Una barra
 * appena creata ha massimo 0, cioe' un cursore che riempie la gola: dice la
 * verita' — non c'e' niente da scorrere — finche' non le si danno i limiti.
 *
 * Ogni movimento manda EXM_COMMAND con l'id e il valore nuovo in `lp`, ANCHE
 * durante il trascinamento: una barra che dicesse la sua solo al rilascio
 * vorrebbe dire un cursore che si muove sopra una pagina ferma.
 * --------------------------------------------------------------------------- */
void         ex_scroll_set_range(ExWindow c, unsigned int massimo,
                              unsigned int pagina);
unsigned int ex_scroll_get_pos(ExWindow c);
void         ex_scroll_set_pos(ExWindow c, unsigned int dove);

/* -----------------------------------------------------------------------------
 * Le voci di un elenco a discesa o di una barra di linguette
 *
 * ! LE STESSE FUNZIONI PER TUTT'E DUE, e per questo si chiamano ex_voce_* e non
 * ex_combo_*: aggiungere una voce a un elenco a discesa e aggiungere una
 * linguetta a una barra sono la stessa cosa, e due serie di nomi identici
 * sarebbero due serie da tenere d'accordo per sempre.
 *
 * Trentadue voci da trentadue caratteri, in una tabella statica: sono elenchi
 * corti per definizione — i valori di una proprieta', i file aperti — e non un
 * documento. Chi ne ha bisogno di piu' vuole una "lista".
 *
 * La scelta arriva come EXM_COMMAND con l'id del controllo e l'INDICE in `lp`.
 * --------------------------------------------------------------------------- */
void         ex_items_clear(ExWindow c);
int          ex_item_add(ExWindow c, const char *testo);
unsigned int ex_items_count(ExWindow c);
unsigned int ex_item_get_selected(ExWindow c);
void         ex_item_select(ExWindow c, unsigned int i);
const char  *ex_item_text(ExWindow c, unsigned int i);

/* LE SCHEDE DI DOCUMENTI (29 settembre 2026, @TOOLKIT-SCHEDE): una "tab"
 * con ex_items_as_tabs(c, 1) diventa la barra delle schede di un editor o di
 * un navigatore. Ogni linguetta ha la sua X (manda EXM_TAB_CLOSE), le
 * frecce compaiono quando non ci stanno tutte, e dalla finestra intera
 * Ctrl+Tab / Ctrl+PgGiu vanno alla scheda dopo, Ctrl+Shift+Tab / Ctrl+PgSu a
 * quella prima (arrivano come EXM_COMMAND col nuovo indice), Ctrl+W chiede
 * di chiudere quella scelta. La barra tiene solo i titoli: cosa mostrare
 * sotto lo sa il programma. ex_item_remove toglie una scheda (la scelta passa
 * alla vicina), ex_item_rename cambia un titolo (l'asterisco di un file
 * modificato). Facoltative nello stub. */
void         ex_items_as_tabs(ExWindow c, int si);
int          ex_item_remove(ExWindow c, unsigned int i);
void         ex_item_rename(ExWindow c, unsigned int i, const char *testo);

/* =============================================================================
 * IL TESTO COLORATO — «areacodice», e il gancio che lo colora
 *
 * ! E' LA STESSA AREA DI TESTO CON UN'ALTRA CAPIENZA. `ex_create("codearea",
 * ...)` da' un'area da 3000 righe per 240 colonne invece di 512 per 200; tutto
 * il resto — il cursore, la selezione, gli appunti, il clic, ex_area_* — e'
 * identico, perche' e' lo stesso controllo. Non c'e' una seconda serie di
 * funzioni da imparare.
 *
 * ! E IL COLORITORE NON STA NEL TOOLKIT, STA IN CHI SA LA LINGUA. Questo
 * controllo sa disegnare del testo colorato; quali parole siano chiavi e dove
 * finisca un commento lo sa chi scrive l'editor. Il gancio riceve UNA RIGA
 * INTERA e riempie un ruolo per carattere:
 *
 *     unsigned int mio_colore(void *dato, const char *riga,
 *                             unsigned char *ruoli, unsigned int stato)
 *
 * `stato` e' com'era finita la riga PRIMA (0 alla prima riga), e cio' che si
 * rende e' come finisce questa: e' il modo in cui un commento a blocco
 * attraversa le righe. Chi non ne ha bisogno rende sempre 0.
 *
 * ! I RUOLI SONO RUOLI, NON COLORI, e la differenza conta: il toolkit decide
 * la tavolozza in un punto solo, quindi due editor diversi non colorano le
 * chiavi di due blu diversi — e il giorno che si vorranno i temi, la tavolozza
 * e' una tabella sola da cambiare.
 *
 * Per il C c'e' gia' `ex_highlight_c`, che si passa cosi':
 *
 *     ex_textarea_set_highlighter(area, ex_highlight_c, 0);
 * ============================================================================= */
#define EX_CODE_NORMAL   0
#define EX_CODE_KEYWORD    1      /* if, while, return... */
#define EX_CODE_TYPE      2      /* int, char, unsigned... */
#define EX_CODE_STRING   3      /* "..." e '.' */
#define EX_CODE_NUMBER    4
#define EX_CODE_COMMENT  5
#define EX_CODE_PREPROC   6      /* la riga che comincia con # */
#define EX_CODE_FUNCTION  7      /* un nome seguito da ( */
#define EX_CODE_SYMBOL   8      /* punteggiatura e operatori */

typedef unsigned int (*ExHighlighter)(void *dato, const char *riga,
                                 unsigned char *ruoli, unsigned int stato);

void ex_textarea_set_highlighter(ExWindow area, ExHighlighter fn, void *dato);

/* Il coloritore del C, pronto: chiavi, tipi, stringhe, numeri, commenti (anche
 * a blocco, su piu' righe), preprocessore e nomi seguiti da parentesi. */
unsigned int ex_highlight_c(void *dato, const char *riga,
                         unsigned char *ruoli, unsigned int stato);

/* -----------------------------------------------------------------------------
 * La lista a scorrimento
 *
 * ! TRE APPLICAZIONI SE L'ERANO DISEGNATA A MANO prima che esistesse: l'elenco
 * del file manager, quello del dialogo Apri/Salva, e l'area dell'editor. Tre
 * volte vuol dire che il pezzo mancante era nel toolkit.
 *
 * Frecce, PgSu/PgGiu, Home/End muovono la scelta e la vista la insegue. Invio
 * e il clic arrivano all'applicazione come EXM_COMMAND con l'id della lista —
 * lo stesso messaggio di un pulsante premuto, perche' e' la stessa decisione.
 *
 * ! IL TESTO SI COPIA DENTRO LA LISTA. Un vettore passato dal chiamante
 * vorrebbe dire che lui lo tiene vivo finche' la lista esiste, e nessuno se ne
 * ricorda: qui si puo' passare un buffer sullo stack e dimenticarsene.
 * --------------------------------------------------------------------------- */
void         ex_list_clear(ExWindow lista);
int          ex_list_add(ExWindow lista, const char *testo);

/* Un'icona davanti a una riga; `ic` a 0 la toglie.
 *
 * ! SI CHIAMA DOPO ex_list_add(), sulla riga appena messa, e non e' una
 * scomodita': quasi tutte le liste di questo sistema sono testo e basta, e un
 * argomento in piu' in ex_list_add() avrebbe voluto dire uno zero
 * aggiunto a ogni chiamata gia' scritta.
 *
 *     ex_list_add(l, "Pulsante");
 *     ex_list_set_icon(l, ex_list_count(l) - 1, ic);
 *
 * ! L'ICONA SI FONDE SUL FONDO DELLA RIGA, che sulla riga scelta e' blu: lo fa
 * il controllo, e per questo l'icona va data a lui invece di disegnarla sopra
 * da fuori. */
void         ex_list_set_icon(ExWindow lista, unsigned int riga, ExIcon ic);

/* Quanti PIXEL la lista tiene a sinistra per le icone, 0 se non ne ha nessuna.
 * Serve a chi disegna un'intestazione sopra una lista incolonnata a spazi: le
 * righe si spostano, e l'intestazione deve seguirle. La corsia e' larga due
 * caratteri esatti proprio perche' quei conti tornino. */
unsigned int ex_list_margin(ExWindow lista);
unsigned int ex_list_count(ExWindow lista);
unsigned int ex_list_get_selected(ExWindow lista);
void         ex_list_select(ExWindow lista, unsigned int i);
const char  *ex_list_text(ExWindow lista, unsigned int i);

/* -----------------------------------------------------------------------------
 * L'area di testo multiriga
 *
 * Un'area non e' una lista con dentro delle righe: ha un cursore che si muove
 * in due direzioni, scorre anche in orizzontale, e i tasti la CAMBIANO invece
 * di limitarsi a sceglierne una riga. Frecce, Home/End, PgSu/PgGiu, Backspace,
 * Canc, Invio e il clic del mouse li gestisce il controllo.
 *
 * ! IL TESTO SI CARICA E SI RILEGGE UNA RIGA PER VOLTA, e non c'e' una
 * funzione che renda tutto il buffer: darebbe a chi chiama un puntatore dentro
 * la libreria, cioe' un modo di scriverci sopra senza che il controllo se ne
 * accorga.
 *
 * ! ex_textarea_add_line() RENDE 0 QUANDO L'AREA E' PIENA, e chi carica un file
 * DEVE guardarlo: caricare mezzo file e poi salvarlo cancellerebbe il resto
 * senza averlo mai mostrato.
 *
 * Limiti: 512 righe da 200 colonne. Sono una conseguenza dell'allocatore a
 * bump, dove free() non restituisce niente.
 * --------------------------------------------------------------------------- */
void         ex_textarea_clear(ExWindow area);
int          ex_textarea_add_line(ExWindow area, const char *riga);
unsigned int ex_textarea_line_count(ExWindow area);
const char  *ex_textarea_line(ExWindow area, unsigned int i);
int          ex_textarea_is_modified(ExWindow area);
void         ex_textarea_set_unmodified(ExWindow area);
/* ! COUNTS FROM ONE (riga 1, col 1): it was written for a status line.
 * ex_textarea_set_cursor() and ex_textarea_select() count from ZERO — subtract one
 * before passing a position from here to them. */
void         ex_textarea_get_cursor(ExWindow area, unsigned int *riga, unsigned int *col);

/* ! IL CURSORE SI PORTA, e non solo si legge. E' quel che serve a una colonna
 * che elenca le funzioni di un sorgente: cliccarci sopra e finire su quella
 * riga. Porta con se' la vista — la riga cercata finisce a meta' altezza, non
 * incollata in cima — e toglie la selezione, perche' arrivando da un'altra
 * parte del documento il tasto dopo cancellerebbe un pezzo di testo lontano da
 * dove si sta guardando. */
void         ex_textarea_set_cursor(ExWindow area, unsigned int riga, unsigned int col);

/* ! LA VISTA, SEPARATA DAL CURSORE (23 settembre 2026): e' quel che serve a
 * una barra di scorrimento accanto all'area. ex_textarea_get_view() dice la prima
 * riga visibile, e in `visibili` quante ne stanno a video; ex_textarea_scroll_to()
 * fa partire la vista da una riga SENZA muovere il cursore — come in ogni
 * editor, trascinare la barra guarda altrove, e il primo tasto riporta la
 * vista dove si sta scrivendo. Oltre la fine non si va: l'ultima pagina e'
 * piena, non mezza vuota. */
unsigned int ex_textarea_get_view(ExWindow area, unsigned int *visibili);
void         ex_textarea_scroll_to(ExWindow area, unsigned int riga);

/* ! UNA RIGA INTERA SI SOSTITUISCE IN UN COLPO, senza passare da un tasto per
 * volta: e' quel che serve a un «cerca e sostituisci», che cambia un pezzo di
 * riga senza che nessuno lo stia scrivendo a tastiera. Si tronca alla
 * capienza dell'area come qualunque altra scrittura, e invalida la catena dei
 * colori da questa riga in giu' come ogni altra modifica. */
void         ex_textarea_set_line(ExWindow area, unsigned int riga,
                                const char *testo);

/* Selects columns [da, a) of one line, cursor at the end, and brings it into
 * view as ex_textarea_set_cursor() does: what «find» needs to show what it found — and
 * then a typed character replaces it, as with any selection (26 September
 * 2026, @EDIT-CERCA). */
void         ex_textarea_select(ExWindow area, unsigned int riga,
                               unsigned int da, unsigned int a);

/* -----------------------------------------------------------------------------
 * La selezione e gli appunti
 *
 * Shift piu' le frecce, Home/End, PgSu/PgGiu allarga la selezione; una freccia
 * senza Shift la toglie. Scrivere su una selezione la SOSTITUISCE.
 *
 * ! GLI APPUNTI SONO DI TUTTA LA SCRIVANIA, non dell'applicazione: stanno in
 * una zona di memoria condivisa, quindi si copia in un editor e si incolla in
 * un altro. Una variabile dentro la libreria non sarebbe bastata — i dati di
 * una libreria condivisa sono una copia fresca per ogni processo.
 *
 * ! E VIVONO FINCHE' C'E' ALMENO UN'APPLICAZIONE GRAFICA APERTA: chiuse tutte,
 * la zona muore con l'ultima. E' la semantica della memoria condivisa di EX-OS.
 *
 * Il taglio e la copia rendono quanti byte hanno preso, 0 se non c'era niente
 * di scelto; l'incolla quanti ne ha messi.
 * --------------------------------------------------------------------------- */
void         ex_textarea_select_all(ExWindow area);
int          ex_textarea_copy(ExWindow area);
int          ex_textarea_cut(ExWindow area);
int          ex_textarea_paste(ExWindow area);
int          ex_textarea_delete(ExWindow area);

/* -----------------------------------------------------------------------------
 * I MENU A TENDINA
 *
 *     ExWindow mb = ex_menu_bar(finestra);
 *     ex_menu_add_item(mb, "File", "Nuovo",        ID_NUOVO);
 *     ex_menu_add_item(mb, "File", "Salva\tCtrl+S", ID_SALVA);
 *     ex_menu_add_item(mb, "File", "-",            0);          un solco
 *     ex_menu_add_item(mb, "File", "Esci",         ID_ESCI);
 *
 * ! LA SCELTA ARRIVA COME EXM_COMMAND, con lo stesso id di un pulsante. Non e'
 * pigrizia: premere «Salva» fra i pulsanti e sceglierlo dal menu File sono LA
 * STESSA DECISIONE presa in due modi, e chi scrive l'applicazione non deve
 * imparare due meccanismi per sentirla. E' la stessa regola dell'Invio su una
 * lista.
 *
 * ! IL TITOLO SI NOMINA OGNI VOLTA, e non c'e' una maniglia di «tendina». Una
 * maniglia in piu' vorrebbe dire un secondo tipo opaco da spiegare, da
 * esportare e da dichiarare in FreeBASIC; una stringa ripetuta e' piu' lunga
 * da scrivere e non ha niente da imparare. Il titolo si crea alla prima voce
 * che lo nomina, e l'ordine in barra e' quello in cui compaiono.
 *
 * ! UN TAB NEL TESTO ALLINEA A DESTRA QUELLO CHE SEGUE: e' cosi' che si scrive
 * la scorciatoia. Il menu non la ESEGUE — i tasti li gestisce l'applicazione,
 * che e' l'unica a sapere cosa fa Ctrl+S — la mostra e basta. Un menu che
 * catturasse le scorciatoie da solo se le prenderebbe anche mentre si scrive
 * in una casella di testo.
 *
 * ! LA TENDINA STA DENTRO LA FINESTRA, e il perche' per esteso e' in exwin.c.
 * In breve: una tendina che esca dal bordo dovrebbe essere una finestra a se',
 * cioe' una zona di memoria condivisa e un giro di richieste al server per
 * ogni menu aperto. Il prezzo: una tendina piu' alta della finestra viene
 * tagliata, e una troppo a destra si sposta a sinistra invece di uscire.
 *
 * Coi tasti: F10 apre, le frecce girano fra titoli e voci, Invio sceglie, Esc
 * chiude — e qualunque altro tasto chiude, invece di sparire nel nulla.
 *
 * ! LE TENDINE LATERALI (27 settembre 2026): un titolo con la barra,
 *
 *     ex_menu_add_item(mb, "Opzioni/Modalita", "Normale",     ID_NORMALE);
 *     ex_menu_add_item(mb, "Opzioni/Modalita", "Scientifica", ID_SCIENT);
 *
 * mette in «Opzioni» la voce «Modalita >», che apre di fianco una tendina con
 * Normale e Scientifica. Si apre col clic o con destra/Invio, sinistra o Esc
 * la richiudono. Un livello solo, quattro tendine laterali per finestra. Su un
 * exwin.so di prima «Opzioni/Modalita» diventa un titolo in barra: le voci ci
 * sono lo stesso, piu' scomode.
 *
 * Rende 0 se non c'e' piu' posto (6 titoli per finestra, 16 voci per titolo).
 * --------------------------------------------------------------------------- */
ExWindow ex_menu_bar(ExWindow finestra);
int        ex_menu_add_item(ExWindow menu, const char *titolo, const char *voce,
                        unsigned int id);

/* La versione di ExWin — di questo exwin.so, quello che gira, non quello con
 * cui il programma e' stato compilato. +0.001 a ogni modifica della libreria,
 * come ogni programma (EXWIN_VERSIONE in exwin.c). Contata dal 27 settembre
 * 2026: prima la libreria non ne aveva una. «Informazioni su» la mostra. */
const char *ex_version(void);

/* Quanto e' grande lo schermo. 0 se si e' in modo testo. */
void ex_screen_size(unsigned int *larghezza, unsigned int *altezza);


/* =============================================================================
 * I NOMI ITALIANI, come alias deprecati (3 ottobre 2026)
 *
 * L'API e' in inglese: ex_create, ex_destroy, EXM_CLOSE, ExWindow... (la
 * tabella completa e' tools/exwin-inglese/mappa.txt). I programmi scritti coi
 * nomi italiani si compilano ancora grazie a questi alias, e quelli gia'
 * compilati trovano ancora i nomi vecchi nella tabella di exwin.so. Chi
 * definisce EXWIN_SENZA_NOMI_ITALIANI prima di includere questo file non li
 * vede: e' il modo di controllare che un programma sia passato tutto.
 * ============================================================================= */
#ifndef EXWIN_SENZA_NOMI_ITALIANI
#define ExFinestra               ExWindow
#define ExProcedura              ExWindowProc
#define ExVoceFin                ExWindowEntry
#define ExIcona                  ExIcon
#define ExGuarda                 ExWatchProc
#define ExColora                 ExHighlighter
#define ex_crea                  ex_create
#define ex_distruggi             ex_destroy
#define ex_titolo                ex_set_title
#define ex_sposta                ex_move
#define ex_mostra                ex_show
#define ex_misura                ex_resize
#define ex_fuoco                 ex_set_focus
#define ex_fuoco_via             ex_clear_focus
#define ex_fuoco_chi             ex_get_focus
#define ex_spegni_scrivania      ex_shutdown_desktop
#define ex_finestre_segui        ex_track_windows
#define ex_finestre_elenco       ex_list_windows
#define ex_finestra_attiva       ex_activate_window_id
#define ex_attiva                ex_activate
#define ex_menu_comparsa         ex_popup_menu
#define ex_apri_file             ex_open_file
#define ex_lista_multipla        ex_list_set_multiselect
#define ex_lista_scelte          ex_list_get_selection
#define ex_lista_riga_a          ex_list_row_at
#define ex_finestra_riduci       ex_minimize_window_id
#define ex_mouse_passaggio       ex_track_mouse_hover
#define ex_ridisegna             ex_redraw
#define ex_menu_spunta           ex_menu_check
#define ex_ritaglio              ex_set_clip
#define ex_pixmap_fuso           ex_pixmap_blend
#define ex_chiudi_le_altre       ex_close_others
#define ex_appunti_metti         ex_clipboard_set
#define ex_appunti_prendi        ex_clipboard_get
#define ex_testo_metti           ex_set_text
#define ex_testo_prendi          ex_get_text
#define ex_prendi_msg            ex_get_message
#define ex_msg_ora               ex_peek_message
#define ex_smista                ex_dispatch
#define ex_esci                  ex_quit
#define ex_procedura_base        ex_default_proc
#define ex_riempi                ex_fill_rect
#define ex_riquadro_disegna      ex_draw_rect
#define ex_rilievo               ex_draw_raised
#define ex_incavo                ex_draw_sunken
#define ex_scrivi                ex_draw_text
#define ex_font_apri             ex_font_open
#define ex_font_trova            ex_font_find
#define ex_scrivi_in             ex_draw_text_buffer
#define ex_font_nome             ex_font_name
#define ex_font_chiudi           ex_font_close
#define ex_font_altezza          ex_font_height
#define ex_font_base             ex_font_baseline
#define ex_larghezza_testo       ex_text_width
#define ex_scrivi_con            ex_draw_text_font
#define ex_sveglia               ex_set_timer
#define ex_guarda_fd             ex_watch_fd
#define ex_tab_contenuto         ex_tab_content
#define ex_aggiorna              ex_update
#define ex_immagine              ex_draw_image
#define ex_immagine_disponi      ex_draw_image_mode
#define ex_icona_apri            ex_icon_open
#define ex_icona_lato            ex_icon_size
#define ex_icona_disegna         ex_icon_draw
#define ex_icona_metti           ex_set_icon
#define ex_icona_chiudi          ex_icon_close
#define ex_mdi_attivo            ex_mdi_get_active
#define ex_mdi_attiva            ex_mdi_activate
#define ex_acceso                ex_is_checked
#define ex_accendi               ex_set_checked
#define ex_abilita               ex_enable
#define ex_avvisa_cambi            ex_notify_changes
#define ex_riquadro_puntatore      ex_show_popup_pointer
#define ex_riquadro_puntatore_via  ex_hide_popup_pointer
#define EXM_SUL_MOUSE              EXM_MOUSE_OVER
#define EXM_CAMBIATO               EXM_CHANGED
#define ex_visibile               ex_set_visible
#define ex_e_visibile             ex_is_visible
#define ex_attivo                 ex_set_enabled
#define ex_e_attivo               ex_is_enabled
#define ex_scorri_limiti         ex_scroll_set_range
#define ex_scorri_dove           ex_scroll_get_pos
#define ex_scorri_vai            ex_scroll_set_pos
#define ex_voci_svuota           ex_items_clear
#define ex_voce_aggiungi         ex_item_add
#define ex_voci_quante           ex_items_count
#define ex_voce_scelta           ex_item_get_selected
#define ex_voce_scegli           ex_item_select
#define ex_voce_testo            ex_item_text
#define ex_voci_schede           ex_items_as_tabs
#define ex_voce_togli            ex_item_remove
#define ex_voce_rinomina         ex_item_rename
#define ex_area_colora           ex_textarea_set_highlighter
#define ex_colora_c              ex_highlight_c
#define ex_lista_svuota          ex_list_clear
#define ex_lista_aggiungi        ex_list_add
#define ex_lista_icona           ex_list_set_icon
#define ex_lista_margine         ex_list_margin
#define ex_lista_quante          ex_list_count
#define ex_lista_scelta          ex_list_get_selected
#define ex_lista_scegli          ex_list_select
#define ex_lista_testo           ex_list_text
#define ex_area_svuota           ex_textarea_clear
#define ex_area_aggiungi         ex_textarea_add_line
#define ex_area_righe            ex_textarea_line_count
#define ex_area_riga             ex_textarea_line
#define ex_area_modificato       ex_textarea_is_modified
#define ex_area_pulita           ex_textarea_set_unmodified
#define ex_area_cursore          ex_textarea_get_cursor
#define ex_area_vai              ex_textarea_set_cursor
#define ex_area_vista            ex_textarea_get_view
#define ex_area_mostra_da        ex_textarea_scroll_to
#define ex_area_riga_metti       ex_textarea_set_line
#define ex_area_seleziona        ex_textarea_select
#define ex_area_seleziona_tutto  ex_textarea_select_all
#define ex_area_copia            ex_textarea_copy
#define ex_area_taglia           ex_textarea_cut
#define ex_area_incolla          ex_textarea_paste
#define ex_area_cancella         ex_textarea_delete
#define ex_menu                  ex_menu_bar
#define ex_menu_voce             ex_menu_add_item
#define ex_versione              ex_version
#define ex_schermo               ex_screen_size
#define EX_NON_RIDISEGNARE       EX_NO_REDRAW
#define EXM_CREA                 EXM_CREATE
#define EXM_DISEGNA              EXM_PAINT
#define EXM_COMANDO              EXM_COMMAND
#define EXM_CHIUDI               EXM_CLOSE
#define EXM_MOUSE_GIU            EXM_MOUSE_DOWN
#define EXM_MOUSE_SU             EXM_MOUSE_UP
#define EXM_TASTO                EXM_KEY
#define EXM_DISTRUGGI            EXM_DESTROY
#define EXM_TERMFINITO           EXM_TERM_EXITED
#define EXM_MISURA               EXM_SIZE
#define EXM_MOUSE_MOSSO          EXM_MOUSE_MOVE
#define EXM_DOPPIOCLIC           EXM_DOUBLE_CLICK
#define EXM_TEMPO                EXM_TIMER
#define EXM_FINESTRE             EXM_WINDOW_LIST
#define EXM_SISTEMA              EXM_SYSTEM
#define EXM_ROTELLA              EXM_WHEEL
#define EXM_MOUSE_DESTRO         EXM_RIGHT_CLICK
#define EXM_LASCIATO             EXM_DROP
#define EXM_SCHEDA_CHIUDI        EXM_TAB_CLOSE
#define EXM_MOUSE_SOPRA          EXM_MOUSE_ENTER
#define EXM_MOUSE_FUORI          EXM_MOUSE_LEAVE
#define EX_DA                    EX_FROM
#define EX_A                     EX_TO
#define EX_ROTELLA_RIGHE         EX_WHEEL_LINES
#define EX_APRIRE                EX_IS_OPEN
#define EX_COL                   EX_COLUMN
#define EX_TITOLO                EX_CAPTION
#define EX_BORDO                 EX_BORDER
#define EX_CHIUDI                EX_CLOSEBOX
#define EX_VISIBILE              EX_VISIBLE
#define EX_SFONDO                EX_BACKGROUND
#define EX_SOPRA                 EX_TOPMOST
#define EX_MODALE                EX_MODAL
#define EX_RIDIM                 EX_RESIZABLE
#define EX_FIGLIO                EX_CHILD
#define EX_SPENTO                EX_DISABLED
#define EX_COMPARSA              EX_POPUP
#define EX_NERO                  EX_BLACK
#define EX_BIANCO                EX_WHITE
#define EX_GRIGIO                EX_GRAY
#define EX_GRIGIO_SC             EX_DARK_GRAY
#define EX_BLU                   EX_BLUE
#define EX_ROSSO                 EX_RED
#define EX_SCRIVANIA             EX_DESKTOP_COLOR
#define EX_LUCE                  EX_HIGHLIGHT
#define EX_OMBRA                 EX_SHADOW
#define EX_VF_RIDOTTA            EX_WE_MINIMIZED
#define EX_VF_FUOCO              EX_WE_FOCUSED
#define EX_FONT_SISTEMA          EX_FONT_SYSTEM
#define EX_FAM_SERIF             EX_FAMILY_SERIF
#define EX_FAM_SANS              EX_FAMILY_SANS
#define EX_FAM_MONO              EX_FAMILY_MONO
#define EX_IMM_ANGOLO            EX_IMAGE_TOPLEFT
#define EX_IMM_CENTRO            EX_IMAGE_CENTER
#define EX_IMM_ALLARGA           EX_IMAGE_STRETCH
#define EX_IMM_RIPETI            EX_IMAGE_TILE
#define EX_COD_NORMALE           EX_CODE_NORMAL
#define EX_COD_CHIAVE            EX_CODE_KEYWORD
#define EX_COD_TIPO              EX_CODE_TYPE
#define EX_COD_STRINGA           EX_CODE_STRING
#define EX_COD_NUMERO            EX_CODE_NUMBER
#define EX_COD_COMMENTO          EX_CODE_COMMENT
#define EX_COD_PREPROC           EX_CODE_PREPROC
#define EX_COD_FUNZIONE          EX_CODE_FUNCTION
#define EX_COD_SIMBOLO           EX_CODE_SYMBOL
#endif /* EXWIN_SENZA_NOMI_ITALIANI */

#ifdef __cplusplus
}
#endif

#endif /* EXWIN_H */

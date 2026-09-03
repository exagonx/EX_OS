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
/* ! LA FINESTRA HA CAMBIATO MISURA, e lp porta quella nuova: EX_X(lp) e
 * EX_Y(lp) sono larghezza e altezza dell'area del client. Arriva DOPO che il
 * toolkit ha gia' preso la zona di pixel nuova, quindi disegnare qui dentro e'
 * sicuro — ma i controlli sono ancora dove stavano, e rimetterli a posto e'
 * il lavoro dell'applicazione. Vedi ex_misura(). */
#define EXM_MISURA      0x000A
/* ! IL PUNTATORE SI E' MOSSO CON UN BOTTONE PREMUTO, e solo allora: un
 * movimento a mano libera non arriva a nessuno. lp porta le coordinate come
 * per EXM_MOUSE_GIU. Arriva a chi ha ricevuto il bottone giu' — anche quando
 * il puntatore e' finito fuori dalla finestra: il trascinamento appartiene a
 * chi l'ha cominciato. */
#define EXM_MOUSE_MOSSO 0x000B
/* ! DUE CLIC VICINI NEL TEMPO E NELLO SPAZIO, sullo stesso punto: e' il
 * toolkit a riconoscerli, non il server — vedi il commento accanto a
 * doppio_clic() in exwin.c. lp porta le coordinate come EXM_MOUSE_GIU.
 *
 * ! ARRIVA SOLO PER CIO' CHE IL TOOLKIT NON HA GIA' INTERPRETATO. Su una
 * lista il doppio clic diventa EXM_COMANDO con EX_APRIRE(lp) a 1, che e' la
 * stessa cosa che dice l'Invio: un'applicazione che gestisce l'Invio ha gia'
 * il doppio clic senza scrivere una riga. */
#define EXM_DOPPIOCLIC  0x000C
/* ! LA SVEGLIA PERIODICA E' SCATTATA: vedi ex_sveglia(). lp porta i
 * millisecondi dall'avvio, che servono a chi vuole misurare invece di
 * contare i messaggi. Senza questo un'applicazione non puo' fare NIENTE da
 * sola: il ciclo dei messaggi dorme finche' non arriva un evento, e un
 * orologio si aggiornerebbe solo quando l'utente muove il mouse. */
#define EXM_TEMPO       0x000D

#define EX_X(lp)        ((int)((lp) & 0xFFFF))
#define EX_Y(lp)        ((int)(((lp) >> 16) & 0xFFFF))

/* =============================================================================
 * ! PER UN EXM_COMANDO CHE VIENE DA UNA LISTA, lp DICE **COME**, e sono due
 * cose distinte impacchettate insieme.
 *
 *   EX_APRIRE(lp)  1 = si e' chiesto di APRIRE: Invio, oppure doppio clic.
 *                  0 = si e' solo scelto, guardando.
 *   EX_COL(lp)     la colonna del clic dentro la riga, contata in caratteri,
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
#define EX_APRIRE(lp)   ((int)((lp) & 1))
#define EX_COL(lp)      ((((lp) >> 8) & 0xFFFF) ? \
                         (int)((((lp) >> 8) & 0xFFFF) - 1) : -1)

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

/* ! MODALE: finche' e' aperta, le ALTRE finestre di questo programma non
 * ricevono ne' clic ne' tasti. Non e' «sta sopra»: una finestra che copre e
 * basta lascia cliccare quello che si vede intorno, e un «vuoi perdere le
 * modifiche?» a cui si puo' rispondere continuando a scrivere non sta
 * chiedendo niente. Il blocco lo fa il server, che e' l'unico a sapere dove
 * vanno a finire i clic. */
#define EX_MODALE       0x0040
/* ! SI RIDIMENSIONA SOLO CHI L'HA CHIESTO. Con questo bit il server disegna la
 * presa nell'angolo in basso a destra e la finestra si puo' tirare col mouse;
 * senza, non compare nemmeno. Non e' prudenza: una finestra che cambia misura
 * e non sa rifare la propria disposizione mostra i controlli fermi dov'erano
 * dentro un'area piu' grande — cioe' una finestra rotta, fatta rompere dal
 * server. Chi mette questo bit deve gestire EXM_MISURA. */
#define EX_RIDIM        0x0080
#define EX_FIGLIO       0x0100  /* e' un controllo dentro un'altra finestra */

/* --- I colori, in ARGB --------------------------------------------------- */
#define EX_NERO         0x00000000
#define EX_BIANCO       0x00FFFFFF
#define EX_GRIGIO       0x00C0C0C0
#define EX_GRIGIO_SC    0x00808080
#define EX_BLU          0x00305A8A
#define EX_ROSSO        0x00C04040
/* ! LA SCRIVANIA NON E' DELLO STESSO BLU DELLE BARRE DEL TITOLO, e dal 18
 * agosto 2026 nemmeno per sbaglio. Fondo e barra dello stesso colore vogliono
 * dire che il bordo di una finestra a schermo pieno sparisce nello sfondo —
 * e che nessuno, guardando una fotografia dello schermo, sa dire dove
 * finisce una finestra e comincia la scrivania. */
#define EX_SCRIVANIA    0x00204060

/* =============================================================================
 * IL RILIEVO — due righe di luce e due di ombra, e tutto sembra un oggetto
 *
 * ! SONO GLI STESSI NUMERI DI BIANCO E NERO, E HANNO UN NOME LORO APPOSTA. Un
 * pulsante non e' «bianco sopra e nero sotto»: e' «illuminato da sopra a
 * sinistra». Il giorno che la tavolozza cambia — un grigio piu' scuro, un tema
 * — si cambiano questi due e cambiano tutti i controlli insieme; scritti come
 * EX_BIANCO ed EX_NERO bisognerebbe distinguerli uno per uno da quelli che
 * bianco e nero lo sono per davvero.
 *
 * ! E LA LUCE VIENE DA SOPRA A SINISTRA, SEMPRE. E' l'unica convenzione che
 * conta: se due controlli la prendessero da due parti diverse, uno dei due
 * sembrerebbe premuto senza esserlo.
 * ============================================================================= */
#define EX_LUCE         0x00FFFFFF
#define EX_OMBRA        0x00000000

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
 *
 * Per una finestra di primo livello: `padre` = 0, `id` = 0, `proc` = la
 * procedura. Per un controllo: `padre` = la finestra, `id` = il numero con cui
 * lo riconoscerai in EXM_COMANDO, `proc` = 0.
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

ExFinestra ex_crea(const char *classe, const char *titolo, unsigned int stile,
                   int x, int y, int w, int h,
                   ExFinestra padre, unsigned int id, ExProcedura proc);

/* =============================================================================
 * ! AGGIUNGERE UN CONTROLLO E' UNA COSA CHE SI FA, e i posti da toccare sono
 * sette: stanno elencati in exwin.c, sopra i numeri CL_*. Qui basta sapere che
 * la classe e' una stringa apposta — un programma gia' compilato continua a
 * funzionare quando la libreria ne impara una nuova, perche' non c'e' nessun
 * enum condiviso da tenere allineato.
 * ============================================================================= */
void       ex_distruggi(ExFinestra f);
void       ex_titolo(ExFinestra f, const char *s);
void       ex_sposta(ExFinestra f, int x, int y);
void       ex_mostra(ExFinestra f, int visibile);

/* -----------------------------------------------------------------------------
 * Cambiare misura
 *
 * ! SU UN CONTROLLO CAMBIA SUBITO; SU UNA FINESTRA DI PRIMO LIVELLO E' UNA
 * RICHIESTA, e la differenza va saputa. Una finestra e' una zona di memoria
 * condivisa creata dal server: la misura nuova arriva quando il server ha
 * finito di preparare la zona nuova, e arriva come EXM_MISURA. Leggere w e h
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
void       ex_misura(ExFinestra f, int w, int h);

/* -----------------------------------------------------------------------------
 * Il fuoco: chi riceve i tasti dentro una finestra
 *
 * ! SENZA QUESTA, IL FUOCO ANDAVA AL PRIMO CONTROLLO CREATO che lo accettasse,
 * e per spostarlo bisognava creare i controlli in un ordine che non e' quello
 * in cui si leggono. Chiedere il fuoco per un controllo che non lo accetta —
 * un'etichetta, un separatore — non e' un errore: e' un no, e le cose restano
 * come stanno.
 * --------------------------------------------------------------------------- */
void        ex_fuoco(ExFinestra controllo);

/* ! IL FUOCO A NESSUNO, e serve a chi disegna i propri controlli. `ex_fuoco`
 * lo puo' dare solo a un controllo del toolkit — un oggetto che lo accetta —
 * quindi non c'e' modo di dire «da qui in avanti i tasti li voglio io».
 * Il browser ne aveva bisogno: i controlli di un modulo HTML sono rettangoli
 * disegnati, non finestre, e finche' la casella dell'indirizzo teneva il fuoco
 * ogni lettera battuta dentro un modulo finiva nella barra dell'indirizzo.
 *
 * Dopo questa chiamata i tasti arrivano alla procedura della finestra come
 * EXM_TASTO, e chi li vuole se li gestisce. */
void        ex_fuoco_via(ExFinestra finestra);

/* ! SPEGNE LA SCRIVANIA INTERA, non questa finestra. Chiede al server di
 * mandare a ogni applicazione la stessa chiusura della crocetta, di aspettare
 * che se ne vadano, di rimettere il modo testo e di morire.
 *
 * Lo chiama il program manager quando si sceglie «Esci»: prima quella voce
 * chiudeva solo la scrivania e lasciava la grafica accesa senza nessuno
 * dentro. Un programma qualunque non ha motivo di chiamarla. */
void        ex_spegni_scrivania(void);

/* -----------------------------------------------------------------------------
 * Gli appunti, senza passare da `ex_area`
 *
 * ! SERVONO A CHI DISEGNA I PROPRI CONTROLLI. `ex_area_copia` e compagni
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
 * `ex_appunti_metti` rende quanti byte ha preso; `ex_appunti_prendi` quanti ne
 * ha messi in `out` (sempre terminato da zero). Zero vuol dire appunti vuoti,
 * o nessuna zona condivisa — che succede quando non c'e' nessuna applicazione
 * grafica, e allora non si muore: si continua senza appunti.
 * --------------------------------------------------------------------------- */
unsigned int ex_appunti_metti(const char *testo, unsigned int n);
unsigned int ex_appunti_prendi(char *out, unsigned int max);

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

/* ! COME ex_prendi_msg MA NON DORME: rende 0 se in questo momento non c'e'
 * niente da fare. Serve a chi sta gia' aspettando altro — una risposta dalla
 * rete, per dirne una — e vuole restare vivo senza rinunciare a quel che sta
 * facendo: dormendo qui dentro, le attese diventerebbero due.
 *
 * ! E CHI LA USA DEVE SAPERE CHE RIENTRA IN CASA PROPRIA. I messaggi si
 * smistano alla procedura della finestra, che e' la stessa che sta girando in
 * quel momento: se quella procedura puo' far ripartire il lavoro che si sta
 * aspettando, chi chiama deve impedirglielo con una bandiera. Non e' un
 * dettaglio da scoprire dopo. */
int  ex_msg_ora(ExMsg *m);
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

/* -----------------------------------------------------------------------------
 * Il rilievo: quello che rende un pulsante un pulsante
 *
 *     ex_rilievo   SPORGE:   luce sopra e a sinistra, ombra sotto e a destra
 *     ex_incavo    RIENTRA:  il contrario
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
void ex_rilievo(ExFinestra f, int x, int y, int w, int h);
void ex_incavo(ExFinestra f, int x, int y, int w, int h);
void ex_scrivi(ExFinestra f, int x, int y, const char *s, unsigned int c);

/* =============================================================================
 * I FONT — piu' di uno, e non solo per il browser
 *
 * ! ZERO E' IL FONT DI SISTEMA, e c'e' SEMPRE. E' l'8x16 compilato dentro il
 * toolkit: non sta in un file, non si puo' chiudere, e non puo' mancare. Un
 * programma che non chiede niente scrive con quello, cioe' si comporta come
 * prima che i font esistessero.
 *
 * ! E CHI NON TROVA IL SUO FONT NON MUORE: ex_font_apri() rende 0, che e' il
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

#define EX_FONT_SISTEMA 0

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
ExFont ex_font_apri(const char *percorso, int corpo);

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
 * riapre il file. Non si chiudono con ex_font_chiudi().
 * ========================================================================== */
#define EX_FAM_SERIF    0
#define EX_FAM_SANS     1
#define EX_FAM_MONO     2

ExFont ex_font_trova(int famiglia, int corpo, int grassetto, int corsivo);

/* Il nome del file che ex_font_trova userebbe, senza aprirlo: serve a chi
 * vuole DIRE quale carattere sta usando — o quale non ha trovato. */
const char *ex_font_nome(int famiglia, int grassetto, int corsivo);
void   ex_font_chiudi(ExFont f);

/* L'interlinea, e la distanza fra la cima e la linea di base. La seconda serve
 * a chi mette due font diversi sulla stessa riga: si allineano le BASI, non le
 * cime, o le lettere ballerebbero. */
int    ex_font_altezza(ExFont f);
int    ex_font_base(ExFont f);

/* Quanti pixel occupa `s` scritto con quel font. */
int    ex_larghezza_testo(ExFont f, const char *s);

/* Come ex_scrivi, ma con un font scelto. `ex_scrivi` e' questa con f = 0. */
void   ex_scrivi_con(ExFinestra w, ExFont f, int x, int y,
                     const char *s, unsigned int c);

/* Chiede EXM_TEMPO ogni `ms` millisecondi per questa finestra; 0 smette.
 *
 * ! LA RISOLUZIONE VERA E' 200 ms, la scadenza del poll dentro il ciclo dei
 * messaggi: chiedere 50 ne da' 200. Per un orologio al secondo il ritardo
 * massimo e' un quinto di secondo; per un'animazione fluida serve un'altra
 * cosa, non questa. */
void   ex_sveglia(ExFinestra f, unsigned int ms);
void ex_aggiorna(ExFinestra f);     /* «ho finito»: lo dice al server */

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
void ex_pixmap(ExFinestra f, int x, int y, int w, int h,
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
int ex_immagine(ExFinestra f, const char *percorso, int x, int y);

/* =============================================================================
 * IL CONTENITORE MDI — finestre dentro una finestra
 *
 *     ExFinestra cont = ex_crea("mdi", "", EX_FIGLIO, 0, 24, 800, 500, f, 0, 0);
 *     ExFinestra ed   = ex_crea("mdifiglio", "sorgente.c",
 *                               EX_TITOLO | EX_CHIUDI,
 *                               10, 10, 400, 300, cont, 0, proc_editor);
 *     ex_crea("areacodice", "", EX_FIGLIO, 4, 4, 380, 260, ed, ID_COD, 0);
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
 * ! IL PULSANTE DI CHIUSURA MANDA EXM_CHIUDI ALLA FINESTRA FIGLIA, e chi non lo
 * gestisce se la vede distrutta — NON esce dal programma, come farebbe una
 * finestra vera. Chi ha da salvare intercetta quel messaggio.
 *
 * ! TAB GIRA DENTRO LA FINESTRA ATTIVA. In un MDI i controlli sono quelli di
 * tutte le finestre messi insieme, e un Tab che ne uscisse lascerebbe un
 * cursore che lampeggia in una finestra che non si sta guardando.
 * ============================================================================= */
ExFinestra ex_mdi_attivo(ExFinestra contenitore);
void       ex_mdi_attiva(ExFinestra figlio);

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
 * e' una risposta che un gruppo di radio sappia dare; ex_accendi(c, 0) lo puo'
 * fare da programma, dove chi lo scrive sa cosa sta facendo.
 *
 * Il clic e la barra spaziatrice mandano EXM_COMANDO con l'id del controllo, e
 * in `lp` c'e' il valore NUOVO — cosi' chi gestisce il messaggio non deve
 * richiamare ex_acceso per sapere cos'e' appena successo.
 * --------------------------------------------------------------------------- */
int  ex_acceso(ExFinestra c);
void ex_accendi(ExFinestra c, int acceso);

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
 * Ogni movimento manda EXM_COMANDO con l'id e il valore nuovo in `lp`, ANCHE
 * durante il trascinamento: una barra che dicesse la sua solo al rilascio
 * vorrebbe dire un cursore che si muove sopra una pagina ferma.
 * --------------------------------------------------------------------------- */
void         ex_scorri_limiti(ExFinestra c, unsigned int massimo,
                              unsigned int pagina);
unsigned int ex_scorri_dove(ExFinestra c);
void         ex_scorri_vai(ExFinestra c, unsigned int dove);

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
 * La scelta arriva come EXM_COMANDO con l'id del controllo e l'INDICE in `lp`.
 * --------------------------------------------------------------------------- */
void         ex_voci_svuota(ExFinestra c);
int          ex_voce_aggiungi(ExFinestra c, const char *testo);
unsigned int ex_voci_quante(ExFinestra c);
unsigned int ex_voce_scelta(ExFinestra c);
void         ex_voce_scegli(ExFinestra c, unsigned int i);
const char  *ex_voce_testo(ExFinestra c, unsigned int i);

/* =============================================================================
 * IL TESTO COLORATO — «areacodice», e il gancio che lo colora
 *
 * ! E' LA STESSA AREA DI TESTO CON UN'ALTRA CAPIENZA. `ex_crea("areacodice",
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
 * Per il C c'e' gia' `ex_colora_c`, che si passa cosi':
 *
 *     ex_area_colora(area, ex_colora_c, 0);
 * ============================================================================= */
#define EX_COD_NORMALE   0
#define EX_COD_CHIAVE    1      /* if, while, return... */
#define EX_COD_TIPO      2      /* int, char, unsigned... */
#define EX_COD_STRINGA   3      /* "..." e '.' */
#define EX_COD_NUMERO    4
#define EX_COD_COMMENTO  5
#define EX_COD_PREPROC   6      /* la riga che comincia con # */
#define EX_COD_FUNZIONE  7      /* un nome seguito da ( */
#define EX_COD_SIMBOLO   8      /* punteggiatura e operatori */

typedef unsigned int (*ExColora)(void *dato, const char *riga,
                                 unsigned char *ruoli, unsigned int stato);

void ex_area_colora(ExFinestra area, ExColora fn, void *dato);

/* Il coloritore del C, pronto: chiavi, tipi, stringhe, numeri, commenti (anche
 * a blocco, su piu' righe), preprocessore e nomi seguiti da parentesi. */
unsigned int ex_colora_c(void *dato, const char *riga,
                         unsigned char *ruoli, unsigned int stato);

/* -----------------------------------------------------------------------------
 * La lista a scorrimento
 *
 * ! TRE APPLICAZIONI SE L'ERANO DISEGNATA A MANO prima che esistesse: l'elenco
 * del file manager, quello del dialogo Apri/Salva, e l'area dell'editor. Tre
 * volte vuol dire che il pezzo mancante era nel toolkit.
 *
 * Frecce, PgSu/PgGiu, Home/End muovono la scelta e la vista la insegue. Invio
 * e il clic arrivano all'applicazione come EXM_COMANDO con l'id della lista —
 * lo stesso messaggio di un pulsante premuto, perche' e' la stessa decisione.
 *
 * ! IL TESTO SI COPIA DENTRO LA LISTA. Un vettore passato dal chiamante
 * vorrebbe dire che lui lo tiene vivo finche' la lista esiste, e nessuno se ne
 * ricorda: qui si puo' passare un buffer sullo stack e dimenticarsene.
 * --------------------------------------------------------------------------- */
void         ex_lista_svuota(ExFinestra lista);
int          ex_lista_aggiungi(ExFinestra lista, const char *testo);
unsigned int ex_lista_quante(ExFinestra lista);
unsigned int ex_lista_scelta(ExFinestra lista);
void         ex_lista_scegli(ExFinestra lista, unsigned int i);
const char  *ex_lista_testo(ExFinestra lista, unsigned int i);

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
 * ! ex_area_aggiungi() RENDE 0 QUANDO L'AREA E' PIENA, e chi carica un file
 * DEVE guardarlo: caricare mezzo file e poi salvarlo cancellerebbe il resto
 * senza averlo mai mostrato.
 *
 * Limiti: 512 righe da 200 colonne. Sono una conseguenza dell'allocatore a
 * bump, dove free() non restituisce niente.
 * --------------------------------------------------------------------------- */
void         ex_area_svuota(ExFinestra area);
int          ex_area_aggiungi(ExFinestra area, const char *riga);
unsigned int ex_area_righe(ExFinestra area);
const char  *ex_area_riga(ExFinestra area, unsigned int i);
int          ex_area_modificato(ExFinestra area);
void         ex_area_pulita(ExFinestra area);
void         ex_area_cursore(ExFinestra area, unsigned int *riga, unsigned int *col);

/* ! IL CURSORE SI PORTA, e non solo si legge. E' quel che serve a una colonna
 * che elenca le funzioni di un sorgente: cliccarci sopra e finire su quella
 * riga. Porta con se' la vista — la riga cercata finisce a meta' altezza, non
 * incollata in cima — e toglie la selezione, perche' arrivando da un'altra
 * parte del documento il tasto dopo cancellerebbe un pezzo di testo lontano da
 * dove si sta guardando. */
void         ex_area_vai(ExFinestra area, unsigned int riga, unsigned int col);

/* ! UNA RIGA INTERA SI SOSTITUISCE IN UN COLPO, senza passare da un tasto per
 * volta: e' quel che serve a un «cerca e sostituisci», che cambia un pezzo di
 * riga senza che nessuno lo stia scrivendo a tastiera. Si tronca alla
 * capienza dell'area come qualunque altra scrittura, e invalida la catena dei
 * colori da questa riga in giu' come ogni altra modifica. */
void         ex_area_riga_metti(ExFinestra area, unsigned int riga,
                                const char *testo);

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
void         ex_area_seleziona_tutto(ExFinestra area);
int          ex_area_copia(ExFinestra area);
int          ex_area_taglia(ExFinestra area);
int          ex_area_incolla(ExFinestra area);
int          ex_area_cancella(ExFinestra area);

/* -----------------------------------------------------------------------------
 * I MENU A TENDINA
 *
 *     ExFinestra mb = ex_menu(finestra);
 *     ex_menu_voce(mb, "File", "Nuovo",        ID_NUOVO);
 *     ex_menu_voce(mb, "File", "Salva\tCtrl+S", ID_SALVA);
 *     ex_menu_voce(mb, "File", "-",            0);          un solco
 *     ex_menu_voce(mb, "File", "Esci",         ID_ESCI);
 *
 * ! LA SCELTA ARRIVA COME EXM_COMANDO, con lo stesso id di un pulsante. Non e'
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
 * Rende 0 se non c'e' piu' posto (6 titoli per finestra, 12 voci per titolo).
 * --------------------------------------------------------------------------- */
ExFinestra ex_menu(ExFinestra finestra);
int        ex_menu_voce(ExFinestra menu, const char *titolo, const char *voce,
                        unsigned int id);

/* Quanto e' grande lo schermo. 0 se si e' in modo testo. */
void ex_schermo(unsigned int *larghezza, unsigned int *altezza);

#ifdef __cplusplus
}
#endif

#endif /* EXWIN_H */

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

#define EX_X(lp)        ((int)((lp) & 0xFFFF))
#define EX_Y(lp)        ((int)(((lp) >> 16) & 0xFFFF))

/* ! PER UN EXM_COMANDO CHE VIENE DA UNA LISTA, lp DICE **COME**: 1 se e'
 * arrivato dall'Invio, 0 dal clic. Sono due desideri diversi e vanno distinti
 * — chi scorre una lista col mouse o con le frecce vuole guardare, chi batte
 * Invio ha chiesto di ENTRARE. Senza questo, un file manager con un albero lo
 * aprirebbe sotto le dita di chi voleva solo dare un'occhiata.
 *
 * Per un pulsante lp non vuol dire niente: un pulsante lo si preme e basta. */
#define EX_DA_INVIO(lp) ((lp) != 0)

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

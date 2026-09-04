/* =============================================================================
 * exwin/bin/exide/exide.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * EX-IDE — l'ambiente di sviluppo visuale: si disegna una finestra, esce del C
 *
 * Tre aree, come in Visual Basic: a sinistra gli strumenti, in mezzo la
 * maschera su cui si dispongono, a destra le proprieta' di quello scelto.
 * Doppio clic su un controllo e si apre l'editor, dentro la funzione che
 * l'evento chiamera'.
 *
 * =============================================================================
 * ! LA GIUNTURA FRA IL DISEGNO E IL CODICE E' L'ID, e c'era gia'.
 *
 * `ex_crea("pulsante", ..., padre, ID, 0)` da' a ogni controllo un numero, e
 * l'evento torna come EXM_COMANDO con quel numero dentro. Nel disegno il
 * pulsante E' `ID_PULSANTE1`, nel sorgente c'e' `case ID_PULSANTE1:`. Questo
 * programma e' fattibile qui piu' che altrove perche' ExWin era gia' fatto a
 * forma di VB6: non c'e' niente da inventare, c'e' da SCRIVERE quel che il
 * disegno dice.
 *
 * =============================================================================
 * ! TRE FILE, E LA REGOLA CHE DECIDE SE IL PROGETTO SOPRAVVIVE
 *
 *     finestra.dis      lo scrive e lo legge SOLO exide
 *     finestra.h        lo scrive SOLO exide: gli id, i puntatori, i prototipi
 *     finestra_gen.c    lo scrive SOLO exide: crea i controlli e smista
 *     finestra.c        lo scrivi SOLO tu; exide ci AGGIUNGE gli handler che
 *                       mancano, in fondo, e non riscrive mai quel che c'e'
 *
 * E' il punto in cui VB6 si rompeva: un file solo, scritto a meta' dal
 * generatore e a meta' a mano, e ogni rigenerazione era una scommessa. Qui il
 * generato e lo scritto non si toccano mai — e' il modello di Qt
 * (`.ui` -> `ui_form.h`), l'unico che regge dopo mesi di modifiche a mano.
 *
 * ! E IL DOPPIO CLIC AGGIUNGE SOLO SE NON C'E'. Su un controllo che ha gia' la
 * sua funzione, il doppio clic porta il cursore su quella riga e basta.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"
#include "exwin.h"
#include "exdlg.h"
#include "exinfo.h"

/* +0.001 a ogni modifica, aggiunta o prova: `exide -version` la stampa.
 * Vedi EX_VERSIONE in libc.h; la stessa stringa la mostra «Informazioni su». */
#define VERSIONE_APP "0.009"
EX_VERSIONE("exide", VERSIONE_APP);

/* -----------------------------------------------------------------------------
 * Gli id dei comandi. Sopra 900 quelli del menu, sotto i controlli veri.
 * --------------------------------------------------------------------------- */
#define ID_STRUMENTI   10   /* la lista degli strumenti, a sinistra */
#define ID_PROPRIETA   11   /* la lista delle proprieta', a destra */
#define ID_VALORE      12   /* la casella in cui si scrive il valore */
#define ID_APPLICA     13
#define ID_ELIMINA     14
#define ID_FORM        15   /* l'elenco a discesa delle maschere */
#define ID_FORM_NUOVA  16
#define ID_FORM_TOGLI  17

#define ID_NUOVO      901
#define ID_APRI       902
#define ID_SALVA      903
#define ID_SALVA_COME 904
#define ID_CHIUDI     905
#define ID_ESCI       906

#define ID_COPIA      911
#define ID_INCOLLA    912
#define ID_TAGLIA     913
#define ID_CANCELLA   914
#define ID_CERCA      915
#define ID_SOSTIT     916
#define ID_ANNULLA    917

#define ID_SORGENTE   921
#define ID_SHELL      922
#define ID_COMPILA    923
#define ID_LIBRERIE   924
#define ID_FILES      925
#define ID_DIRECTORY  926
#define ID_PROGETTO   927

#define ID_MANUALE    931
#define ID_INFO       932

/* La finestra del compilatore e quella delle librerie. */
#define ID_CC_RADICE  951
#define ID_CC_OPZ     952
#define ID_CC_NOME    953
#define ID_CC_SCRIVI  954
#define ID_CC_COMPILA 955
#define ID_CC_CHIUDI  956
#define ID_CC_USCITA  957
#define ID_LIB_ELENCO 961
#define ID_LIB_AGG    962
#define ID_LIB_CHIUDI 963
#define ID_PR_AUTORE  971
#define ID_PR_VERS    972
#define ID_PR_DESCR   973
#define ID_PR_NOTA    974
#define ID_PR_SALVA   975
#define ID_PR_CHIUDI  976
#define ID_FL_ELENCO  981
#define ID_FL_CHIUDI  982
#define ID_FE_SALVA   983
#define ID_FE_CERCA   984
#define ID_FE_CHIUDI  985
#define ID_FE_SOSTIT  986

/* La finestra dell'editor: il suo menu e i suoi controlli. */
#define ID_ED_SALVA   941
#define ID_ED_CHIUDI  942
#define ID_ED_FUNZ    943
#define ID_ED_CODICE  944
#define ID_ED_TAB     945

#define PERC_MAX      320

/* =============================================================================
 * GLI STRUMENTI — la tavola che dice tutto quello che exide sa di un controllo
 *
 * ! UNA RIGA PER CONTROLLO, E NIENTE ALTROVE. Aggiungere uno strumento e' una
 * riga qui: il pannello di sinistra, il disegno sulla maschera, il nome
 * predefinito, l'evento e il codice generato leggono tutti da questa tavola.
 * Sparpagliare le stesse informazioni in cinque `switch` vorrebbe dire cinque
 * posti da tenere d'accordo, e il quinto che se ne dimentica genera codice che
 * non compila.
 *
 * ! L'EVENTO PREDEFINITO E' IL PRIMO DELLA LISTA, e gli altri si scelgono dalle
 * proprieta'. Un pulsante nasce con «Clic» perche' e' quel che fa un pulsante;
 * chi vuole «SulMouse» lo dice, e non deve dirlo per avere il caso normale.
 * ============================================================================= */
#define EVENTI_MAX  3

typedef struct {
    const char *classe;             /* la classe di ExWin */
    const char *etichetta;          /* come si chiama nel pannello */
    const char *prefisso;           /* Pulsante1, Casella2... */
    int         w, h;               /* la misura con cui nasce */
    const char *evento[EVENTI_MAX]; /* [0] e' quello predefinito, 0 = fine */
} Strumento;

static const Strumento g_strum[] = {
    { "pulsante",     "Pulsante",     "Pulsante",  90, 26, { "Clic", "SulMouse", 0 } },
    { "etichetta",    "Etichetta",    "Etichetta", 90, 16, { "SulMouse", "Clic", 0 } },
    { "testo",        "Casella",      "Casella",  140, 22, { "Cambiato", "Invio", 0 } },
    { "spunta",       "Spunta",       "Spunta",   140, 20, { "Cambiato", 0, 0 } },
    { "radio",        "Radio",        "Radio",    140, 20, { "Cambiato", 0, 0 } },
    { "riquadro",     "Riquadro",     "Riquadro", 160, 90, { 0, 0, 0 } },
    { "separatore",   "Separatore",   "Riga",     160,  2, { 0, 0, 0 } },
    { "intestazione", "Intestazione", "Titolo",   200, 22, { 0, 0, 0 } },
    { "lista",        "Lista",        "Lista",    160,110, { "Scelta", "Apertura", 0 } },
    { "areatesto",    "Area testo",   "Testo",    200,120, { "Cambiato", 0, 0 } },
    { "areacodice",   "Area codice",  "Codice",   240,140, { "Cambiato", 0, 0 } },
    { "combo",        "Elenco",       "Elenco",   140, 22, { "Scelta", 0, 0 } },
    { "tab",          "Linguette",    "Linguette",200, 24, { "Scelta", 0, 0 } },
    { "scorrimento",  "Scorrimento",  "Barra",     16,120, { "Scorso", 0, 0 } },
};

#define STRUM_N ((int)(sizeof(g_strum) / sizeof(g_strum[0])))

/* =============================================================================
 * IL DISEGNO — i controlli messi sulla maschera
 * ============================================================================= */
#define CTRL_MAX   64
#define NOME_MAX   24
#define TESTO_MAX  48

typedef struct {
    int          usato;
    int          tipo;              /* quale riga di g_strum */
    int          form;              /* di quale maschera fa parte */
    char         nome[NOME_MAX];
    char         testo[TESTO_MAX];
    int          x, y, w, h;        /* dentro la maschera */
    unsigned int id;
    int          evento;            /* quale voce di g_strum[tipo].evento */
} Ctrl;

static Ctrl g_ctrl[CTRL_MAX];
static int  g_sel = -1;             /* il controllo scelto, -1 = nessuno */
static int  g_strum_sel = -1;       /* lo strumento armato, -1 = nessuno */
static int  g_ridim = -1;           /* quale maniglia si sta tirando, -1 = nessuna */

/* ! UNA FOTOGRAFIA PER TRASCINAMENTO, E SOLO SE QUALCOSA CAMBIA. Un
 * trascinamento manda decine di EXM_MOUSE_MOSSO: fotografando a ognuno, i
 * sedici passi indietro se li mangerebbe tutti un movimento solo, e si
 * tornerebbe a mezzo pixel per volta. Fotografando invece all'inizio del
 * trascinamento, un clic che sceglie e basta lascerebbe un passo indietro che
 * non fa niente — e un Annulla che non fa niente e' peggio di non averlo.
 * Percio' si fotografa al PRIMO cambiamento vero, e questa bandiera se lo
 * ricorda per il resto del trascinamento. */
static int  g_tras_segnato = 0;

/* =============================================================================
 * LE MASCHERE — piu' di una, dal 3 settembre 2026
 *
 * ! I CONTROLLI RESTANO IN UN ELENCO SOLO, con dentro il numero della maschera
 * a cui appartengono. L'altra strada — un elenco di controlli dentro ogni
 * maschera — sembra piu' ordinata e costa di piu' in ogni punto che oggi
 * funziona: il nome libero, l'id libero e il giro che assicura gli handler
 * guardano TUTTI i controlli del programma, non quelli di una maschera, e con
 * gli elenchi annidati diventerebbero due cicli invece di uno. Un campo in
 * piu' e un `continue` dove si disegna: e' tutta la differenza.
 *
 * ! I NOMI RESTANO UNICI IN TUTTO IL PROGRAMMA, non dentro la maschera. Il
 * nome di un controllo diventa un `#define ID_...`, una variabile `h_...` e
 * un nome di funzione in finestra.h, che e' un file solo: due «Pulsante1» in
 * due maschere diverse sarebbero due definizioni con lo stesso nome. Non
 * serve controllarlo da nessuna parte proprio perche' id_libero() e il
 * generatore di nomi guardano gia' tutto l'elenco.
 *
 * ! LA MASCHERA 0 E' QUELLA PRINCIPALE E NON SI TOGLIE. E' l'unica che il
 * programma generato apre da solo (finestra_crea, che sta scritto dentro il
 * main di finestra.c da quando exide esiste): togliere quella vorrebbe dire
 * un programma senza finestre.
 * ============================================================================= */
#define FORM_MAX   8

typedef struct {
    int  usato;
    char nome[NOME_MAX];            /* diventa un nome di funzione: unico */
    char titolo[TESTO_MAX];         /* quel che si legge nella barra */
    int  w, h;
} Form;

static Form g_form[FORM_MAX];
static int  g_form_sel = 0;         /* quale si sta disegnando */

/* ! LA PRIMA TIENE I NOMI DI SEMPRE — `g_form`, `finestra_crea()`,
 * `finestra_proc()` — e le altre no. Non e' un'incoerenza: il finestra.c di
 * ogni progetto gia' scritto chiama `finestra_crea()` dal suo main, e quel
 * file exide non lo riscrive MAI. Cambiare quei tre nomi per simmetria
 * vorrebbe dire che ogni progetto fatto prima di oggi non compila piu'. */
static int form_e_principale(int f) { return f == 0; }

/* La maschera che si sta disegnando adesso. */
static Form *forma(void) { return &g_form[g_form_sel]; }

/* Il valore di partenza, uguale per la prima e per quelle aggiunte dopo. */
static void form_azzera(Form *F, const char *nome, const char *titolo)
{
    memset(F, 0, sizeof(*F));
    F->usato = 1;
    strncpy(F->nome, nome, NOME_MAX - 1);
    strncpy(F->titolo, titolo, TESTO_MAX - 1);
    F->w = 400;
    F->h = 260;
}

/* =============================================================================
 * ANNULLA — una fotografia del disegno prima di ogni modifica
 *
 * ! SI FOTOGRAFA TUTTO, non si registra COSA e' cambiato, ed e' una scelta
 * fatta guardando quante cose sono «una modifica»: mettere un controllo,
 * cancellarlo, spostarlo, ridimensionarlo, cambiargli una qualunque delle otto
 * proprieta', cambiare le quattro della maschera, aggiungere una maschera,
 * toglierne una che si porta via i suoi controlli. Un registro di comandi
 * vorrebbe dire scrivere l'operazione INVERSA di ognuna di quelle, e ognuna
 * puo' essere sbagliata in un modo suo — mentre «rimetti tutto com'era» non
 * puo' sbagliare: e' una copia.
 *
 * ! E IL DISEGNO E' PICCOLO ABBASTANZA PERCHE' SIA SENSATO. Un istante pesa
 * quanto l'elenco dei controlli piu' quello delle maschere, sedici istanti
 * stanno in un centinaio di kilobyte di memoria azzerata, e non finiscono nel
 * binario. Se un giorno i controlli diventassero migliaia questa scelta si
 * riguarderebbe; con sessantaquattro non c'e' niente da bilanciare.
 *
 * ! LA STORIA E' UN ANELLO, e cosi' il sedicesimo passo indietro non costa una
 * copia di tutto l'elenco per far posto: costa quanto il primo.
 * ============================================================================= */
#define ANNULLA_MAX  16

typedef struct {
    Ctrl ctrl[CTRL_MAX];
    Form form[FORM_MAX];
    int  form_sel;
    int  sel;
} Istante;

static Istante g_storia[ANNULLA_MAX];
static int     g_storia_n = 0;      /* quanti passi indietro si possono fare */
static int     g_storia_testa = 0;  /* dove si scrive il prossimo */

/* ! LA STORIA NON ATTRAVERSA I PROGETTI. Aprirne un altro e poi premere
 * Annulla rimetterebbe sulla maschera i controlli di quello di prima, con i
 * loro nomi e i loro id: un disegno che non e' mai esistito, salvato sopra
 * quello vero. Si azzera aprendo, creando e copiando. */
static void storia_azzera(void)
{
    g_storia_n = 0;
    g_storia_testa = 0;
}

static void istante_segna(void)
{
    Istante *s = &g_storia[g_storia_testa];

    memcpy(s->ctrl, g_ctrl, sizeof(g_ctrl));
    memcpy(s->form, g_form, sizeof(g_form));
    s->form_sel = g_form_sel;
    s->sel      = g_sel;

    g_storia_testa = (g_storia_testa + 1) % ANNULLA_MAX;
    if (g_storia_n < ANNULLA_MAX) g_storia_n++;
}

/* Il progetto. */
static char g_prog_dir[PERC_MAX]  = "";     /* la directory del progetto */
static char g_prog_nome[NOME_MAX] = "";
static int  g_sporco = 0;                   /* c'e' qualcosa da salvare */

/* Le finestre e i controlli di exide. */
static ExFinestra g_f, g_lst_strum, g_lst_prop, g_val, g_stato, g_menu;
static ExFinestra g_cmb_form;       /* l'elenco a discesa delle maschere */

/* L'area della maschera dentro la finestra di exide. */
#define TELA_X   164
#define TELA_Y    46
#define TELA_W   436
#define TELA_H   396

/* ! LA GRIGLIA E' DI QUATTRO PIXEL, e serve a una cosa sola: due controlli
 * messi «alla stessa altezza» ci stanno davvero. A mano si sbaglia di un
 * pixel, e un pixel di disallineamento si vede su una fila di pulsanti. */
#define GRIGLIA   4

/* La misura minima di un controllo, tirandolo col mouse o scrivendola nelle
 * proprieta': un numero solo, o le due strade si fermerebbero in due posti
 * diversi e chi scrive 4 nella casella otterrebbe quel che tirando non puo'. */
#define MISURA_MIN 8

static void dico(const char *s) { ex_testo_metti(g_stato, s); }

/* =============================================================================
 * LE LIBRERIE — quel che si collega insieme al progetto
 *
 * ! UNA LIBRERIA CONDIVISA SI COLLEGA CON IL SUO STUB, non con un archivio. In
 * EX-OS una .so non si linka: si collega dentro il programma un file di poche
 * righe — lo stub — che alla prima chiamata apre la libreria e risolve i nomi.
 * Percio' «scegliere una libreria» qui vuol dire «aggiungere il suo stub alla
 * riga di compilazione», e niente altro: nessun -l, nessun percorso di ricerca.
 *
 * ! exwin C'E' SEMPRE E NON SI TOGLIE. Un programma disegnato con questo
 * ambiente e' fatto di finestre: senza il toolkit non si collega, e offrire di
 * levarlo sarebbe offrire di rompere il progetto.
 *
 * ! E GLI STUB STANNO SUL CD DEGLI STRUMENTI dal 3 settembre 2026: prima non
 * c'erano, e chi compilava dentro EX-OS poteva solo tirarsi dentro exwin.c
 * intero. Il percorso e' <radice degli strumenti>/include/<nome>_stub.c.
 * ============================================================================= */
#define LIB_MAX      12
#define LIB_NOME_MAX 40

static struct {
    char nome[LIB_NOME_MAX];    /* come si legge nel pannello */
    char stub[PERC_MAX];        /* il file da compilare; vuoto = solo un nome */
    int  base;                  /* 1 = c'e' sempre */
    int  scelta;
} g_lib[LIB_MAX] = {
    { "exwin   finestre e controlli", "include/exwin_stub.c",  1, 1 },
    { "exdlg   dialoghi (apri, salva)", "include/exdlg_stub.c",  0, 0 },
    { "exhttp  rete e http",          "include/exhttp_stub.c", 0, 0 },
    { "exhtml  albero HTML",          "include/exhtml_stub.c", 0, 0 },
    { "excss   fogli di stile",       "include/excss_stub.c",  0, 0 },
    { "exjs    JavaScript",           "include/exjs_stub.c",   0, 0 },
    { "exdom   il ponte JS-pagina",   "include/exdom_stub.c",  0, 0 },
};

static int g_lib_n = 7;

/* La radice dell'albero degli strumenti: da qui si ricava tutto il resto —
 * il compilatore, gli header, lo start.S, i ponti della libc, il linker
 * script. Un campo solo invece di sei, e i due casi veri sono due valori. */
static char g_cc_radice[PERC_MAX] = "/cdrom/exos";

/* ! LE OPZIONI OBBLIGATORIE NON STANNO NELLA CASELLA, e c'e' un motivo preciso.
 * La casella e' un controllo "testo", e "testo" tiene al massimo 63 caratteri
 * (TESTO_LEN in exwin.c) — non i 160 del campo che la legge. La prima versione
 * ci aveva scritto settantadue caratteri: alla creazione ex_testo_metti li ha
 * TRONCATI a 63, tagliando "-fno-pie" a un "-" solitario in fondo alla riga.
 * Per gcc, un "-" da solo vuol dire «leggi da stdin» — ed e' la ragione vera
 * dietro «-E o -x richiesto quando l'ingresso e' lo standard input»: non
 * un'opzione sbagliata, un'opzione TAGLIATA A META'.
 *
 * ! E NON E' SOLO UN LIMITE DA RISPETTARE: e' anche giusto che siano fisse.
 * -ffreestanding, -fno-builtin, -nostdlib, -fno-pic, -fno-pie non sono un
 * gusto di chi compila — sono il patto con il caricatore ELF (vedi il perche'
 * in lib/programma.ld, il codice non scrivibile) e con l'assenza di una libc
 * ospite. Lasciarle in una casella modificabile vorrebbe dire poterle spegnere
 * per sbaglio scrivendoci sopra, e il sintomo sarebbe un binario che il
 * caricatore rifiuta — o peggio, uno col codice scrivibile.
 *
 * La casella resta per quel che e' davvero facoltativo: ottimizzazione,
 * avvisi, definizioni. Vuota va benissimo. */
#define CC_OBBLIGATORIE \
    "-m32 -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie"
static char g_cc_opzioni[100] = "-O2 -Wall";
static char g_cc_nome[NOME_MAX] = "";

static int arrotonda(int v) { return (v + GRIGLIA / 2) / GRIGLIA * GRIGLIA; }

/* =============================================================================
 * DISEGNARE LA MASCHERA
 *
 * ! I CONTROLLI SI DISEGNANO, NON SI CREANO VIVI, ed e' la differenza fra un
 * disegnatore e un programma che si guarda. Un pulsante vero dentro la tela
 * risponderebbe al clic invece di farsi scegliere e trascinare, e un'area di
 * testo si mangerebbe i tasti. Qui sono rettangoli: somigliano a quel che
 * saranno, e ubbidiscono al disegnatore.
 *
 * ! E SOMIGLIANO, NON SONO IDENTICI. Un'anteprima fedele al pixel vorrebbe dire
 * riscrivere il disegno di ogni controllo una seconda volta, dentro exide, e
 * tenerla d'accordo con quella del toolkit per sempre. Quel che conta e' che si
 * riconoscano a colpo d'occhio e che la MISURA sia quella vera.
 * ============================================================================= */
static void disegna_controllo(const Ctrl *c, int ox, int oy)
{
    int x = ox + c->x, y = oy + c->y;
    const char *cl = g_strum[c->tipo].classe;

    if (strcmp(cl, "pulsante") == 0) {
        ex_riempi(g_f, x, y, c->w, c->h, EX_GRIGIO);
        ex_riquadro_disegna(g_f, x, y, c->w, c->h, EX_NERO);
        ex_rilievo(g_f, x + 1, y + 1, c->w - 2, c->h - 2);
        ex_scrivi(g_f, x + 6, y + (c->h - 16) / 2, c->testo, EX_NERO);
    } else if (strcmp(cl, "etichetta") == 0) {
        ex_scrivi(g_f, x, y, c->testo, EX_NERO);
    } else if (strcmp(cl, "testo") == 0 || strcmp(cl, "combo") == 0) {
        ex_riempi(g_f, x, y, c->w, c->h, EX_BIANCO);
        ex_incavo(g_f, x, y, c->w, c->h);
        ex_scrivi(g_f, x + 3, y + (c->h - 16) / 2, c->testo, EX_NERO);
        if (strcmp(cl, "combo") == 0) {
            ex_riempi(g_f, x + c->w - 19, y + 2, 17, c->h - 4, EX_GRIGIO);
            ex_rilievo(g_f, x + c->w - 19, y + 2, 17, c->h - 4);
        }
    } else if (strcmp(cl, "spunta") == 0 || strcmp(cl, "radio") == 0) {
        ex_riempi(g_f, x, y + (c->h - 13) / 2, 13, 13, EX_BIANCO);
        ex_incavo(g_f, x, y + (c->h - 13) / 2, 13, 13);
        ex_scrivi(g_f, x + 18, y + (c->h - 16) / 2, c->testo, EX_NERO);
    } else if (strcmp(cl, "riquadro") == 0) {
        ex_incavo(g_f, x, y + 8, c->w, c->h - 8);
        ex_rilievo(g_f, x + 1, y + 9, c->w - 2, c->h - 10);
        ex_riempi(g_f, x + 6, y + 8, (int)strlen(c->testo) * 8 + 6, 2, EX_GRIGIO);
        ex_scrivi(g_f, x + 9, y, c->testo, EX_NERO);
    } else if (strcmp(cl, "separatore") == 0) {
        ex_riempi(g_f, x, y, c->w, 1, EX_OMBRA);
        ex_riempi(g_f, x, y + 1, c->w, 1, EX_LUCE);
    } else if (strcmp(cl, "intestazione") == 0) {
        ex_riempi(g_f, x, y, c->w, c->h, EX_BLU);
        ex_rilievo(g_f, x, y, c->w, c->h);
        ex_scrivi(g_f, x + 6, y + (c->h - 16) / 2, c->testo, EX_BIANCO);
    } else if (strcmp(cl, "scorrimento") == 0) {
        ex_riempi(g_f, x, y, c->w, c->h, EX_GRIGIO_SC);
        ex_riempi(g_f, x, y, c->w, 16, EX_GRIGIO);
        ex_rilievo(g_f, x, y, c->w, 16);
        ex_riempi(g_f, x, y + c->h - 16, c->w, 16, EX_GRIGIO);
        ex_rilievo(g_f, x, y + c->h - 16, c->w, 16);
    } else if (strcmp(cl, "tab") == 0) {
        ex_riempi(g_f, x, y, c->w, c->h, EX_GRIGIO);
        ex_riempi(g_f, x, y + c->h - 1, c->w, 1, EX_OMBRA);
        ex_rilievo(g_f, x, y, (int)strlen(c->testo) * 8 + 16, c->h);
        ex_scrivi(g_f, x + 8, y + 3, c->testo, EX_NERO);
    } else {
        /* lista, areatesto, areacodice: un buco bianco con dentro la prima riga */
        ex_riempi(g_f, x, y, c->w, c->h, EX_BIANCO);
        ex_incavo(g_f, x, y, c->w, c->h);
        ex_scrivi(g_f, x + 3, y + 2, c->testo, EX_NERO);
    }
}

/* ! LE MANIGLIE DICONO «QUESTO E' SCELTO», e non un bordo colorato: un bordo si
 * confonde con quello del controllo, otto quadratini neri agli angoli e a meta'
 * lato no. E' la convenzione di ogni disegnatore mai scritto, e chi la vede sa
 * gia' cosa vuol dire. */
/* =============================================================================
 * LE OTTO MANIGLIE
 *
 * ! DOVE SONO LO DICE UNA FUNZIONE SOLA, e la usano sia il disegno che il
 * clic. Erano otto coppie di numeri scritte a mano in un posto solo finche'
 * servivano solo a disegnarle; adesso che si possono anche PRENDERE, tenerne
 * due copie vorrebbe dire che il giorno in cui una si sposta le maniglie si
 * disegnano dove non si possono afferrare — e il sintomo sarebbe «il mouse non
 * le prende», che non somiglia affatto a «i due elenchi non sono piu' uguali».
 *
 * ! IL LATO CHE SI DISEGNA E QUELLO CHE SI PRENDE NON SONO LO STESSO. Cinque
 * pixel sono giusti da vedere — piu' grandi coprirebbero il controllo — e sono
 * pochi da colpire col mouse. La presa e' 11, cioe' tre pixel di margine per
 * lato: si mira al quadratino e si prende comunque.
 * ============================================================================= */
#define MAN_LATO   5        /* il quadratino che si vede */
#define MAN_PRESA 11        /* quanto e' largo il bersaglio del mouse */

/* Per ogni maniglia: quale bordo muove, in orizzontale e in verticale.
 * -1 = quello di sinistra/sopra, 0 = nessuno (sta in mezzo), 1 = destra/sotto. */
static const int g_man_x[8] = { -1,  0,  1, -1,  1, -1,  0,  1 };
static const int g_man_y[8] = { -1, -1, -1,  0,  0,  1,  1,  1 };

/* Il centro della maniglia i, in coordinate della finestra di exide. */
static void maniglia_centro(const Ctrl *c, int i, int ox, int oy,
                            int *cx, int *cy)
{
    *cx = ox + c->x + (g_man_x[i] < 0 ? 0 : g_man_x[i] > 0 ? c->w : c->w / 2);
    *cy = oy + c->y + (g_man_y[i] < 0 ? 0 : g_man_y[i] > 0 ? c->h : c->h / 2);
}

static void disegna_maniglie(const Ctrl *c, int ox, int oy)
{
    int i, cx, cy;

    for (i = 0; i < 8; i++) {
        maniglia_centro(c, i, ox, oy, &cx, &cy);
        ex_riempi(g_f, cx - MAN_LATO / 2, cy - MAN_LATO / 2,
                  MAN_LATO, MAN_LATO, EX_NERO);
    }
}

static void disegna_tela(void)
{
    int ox = TELA_X + 8, oy = TELA_Y + 8 + 20;      /* dentro il telaio finto */
    int i;

    /* Il ripiano su cui sta la maschera. */
    ex_riempi(g_f, TELA_X, TELA_Y, TELA_W, TELA_H, 0x00505050);
    ex_incavo(g_f, TELA_X, TELA_Y, TELA_W, TELA_H);

    /* La maschera: telaio, barra del titolo, area del client. */
    ex_riempi(g_f, TELA_X + 6, TELA_Y + 6, forma()->w + 4, forma()->h + 24,
              EX_GRIGIO);
    ex_rilievo(g_f, TELA_X + 6, TELA_Y + 6, forma()->w + 4, forma()->h + 24);
    ex_riempi(g_f, TELA_X + 8, TELA_Y + 8, forma()->w, 20, EX_BLU);
    ex_scrivi(g_f, TELA_X + 13, TELA_Y + 10, forma()->titolo, EX_BIANCO);
    ex_riempi(g_f, ox, oy, forma()->w, forma()->h, EX_GRIGIO);

    /* ! SI DISEGNA UNA MASCHERA PER VOLTA, e non e' un ripiego rispetto a
     * mostrarle tutte insieme dentro un contenitore MDI. Il ripiano e' 436x396
     * e una maschera nasce 400x260: due finestre di quella misura, li' dentro,
     * si coprirebbero quasi per intero e si passerebbe il tempo a spostarle per
     * vedere quella sotto. E si disegna una cosa sola per volta anche perche'
     * i controlli qui sono DISEGNATI, non vivi (vedi disegna_controllo): due
     * maschere insieme vorrebbero dire decidere a ogni clic in quale delle due
     * e' caduto, che e' lavoro in piu' per una cosa che non serve a chi
     * disegna — si lavora su una finestra per volta comunque. */
    for (i = 0; i < CTRL_MAX; i++)
        if (g_ctrl[i].usato && g_ctrl[i].form == g_form_sel)
            disegna_controllo(&g_ctrl[i], ox, oy);

    if (g_sel >= 0 && g_ctrl[g_sel].usato)
        disegna_maniglie(&g_ctrl[g_sel], ox, oy);
}

/* =============================================================================
 * LE PROPRIETA'
 *
 * ! SONO UNA LISTA E UNA CASELLA, e non una griglia: una griglia con le celle
 * modificabili sul posto e' un controllo che nel toolkit non c'e', e farlo
 * dentro exide vorrebbe dire un controllo che vive in un programma solo — lo
 * stesso difetto che ha tenuto il navigatore senza caselle di spunta per un
 * mese. Si sceglie la riga, si scrive nella casella, si preme Applica.
 * ============================================================================= */
#define PROP_N  8

static const char *const g_prop_nome[PROP_N] = {
    "nome", "testo", "x", "y", "larghezza", "altezza", "id", "evento"
};

static void prop_valore(int k, char *out, unsigned int max)
{
    const Ctrl *c;

    out[0] = '\0';
    if (g_sel < 0 || !g_ctrl[g_sel].usato) return;
    c = &g_ctrl[g_sel];

    switch (k) {
    case 0: strncpy(out, c->nome, max - 1);  out[max - 1] = '\0'; break;
    case 1: strncpy(out, c->testo, max - 1); out[max - 1] = '\0'; break;
    case 2: sprintf(out, "%d", c->x); break;
    case 3: sprintf(out, "%d", c->y); break;
    case 4: sprintf(out, "%d", c->w); break;
    case 5: sprintf(out, "%d", c->h); break;
    case 6: sprintf(out, "%u", c->id); break;
    case 7: {
        const char *e = g_strum[c->tipo].evento[c->evento];

        strncpy(out, e ? e : "(nessuno)", max - 1);
        out[max - 1] = '\0';
        break;
    }
    default: break;
    }
}

/* Le proprieta' della MASCHERA, che si vedono quando non c'e' nessun controllo
 * scelto. Il nome c'e' da quando le maschere sono piu' d'una: e' quello che
 * diventa `opzioni_crea()` nel codice generato, quindi si deve poter cambiare. */
#define FPROP_N  4

static const char *const g_fprop_nome[FPROP_N] = {
    "nome", "titolo", "larghezza", "altezza"
};

static void fprop_valore(int k, char *out, unsigned int max)
{
    out[0] = '\0';
    switch (k) {
    case 0: strncpy(out, forma()->nome, max - 1);   out[max - 1] = '\0'; break;
    case 1: strncpy(out, forma()->titolo, max - 1); out[max - 1] = '\0'; break;
    case 2: sprintf(out, "%d", forma()->w); break;
    case 3: sprintf(out, "%d", forma()->h); break;
    default: break;
    }
}

static void prop_mostra(void)
{
    int k;
    char riga[80], val[64];

    ex_lista_svuota(g_lst_prop);

    if (g_sel < 0 || !g_ctrl[g_sel].usato) {
        /* ! CON IL VALORE ACCANTO, come per i controlli. Prima qui c'erano i
         * soli nomi: si vedeva «larghezza» e bisognava sceglierla per sapere
         * quanto fosse, mentre due righe piu' in la' le proprieta' di un
         * pulsante si leggevano tutte insieme. */
        for (k = 0; k < FPROP_N; k++) {
            fprop_valore(k, val, sizeof(val));
            sprintf(riga, "%-10s %s", g_fprop_nome[k], val);
            ex_lista_aggiungi(g_lst_prop, riga);
        }
        fprop_valore((int)ex_lista_scelta(g_lst_prop), val, sizeof(val));
        ex_testo_metti(g_val, val);
        return;
    }

    for (k = 0; k < PROP_N; k++) {
        prop_valore(k, val, sizeof(val));
        sprintf(riga, "%-10s %s", g_prop_nome[k], val);
        ex_lista_aggiungi(g_lst_prop, riga);
    }
    prop_valore((int)ex_lista_scelta(g_lst_prop), val, sizeof(val));
    ex_testo_metti(g_val, val);
}

/* ! IL NOME DI UN CONTROLLO DIVENTA UN NOME DI FUNZIONE, quindi non puo'
 * contenere quel che il C non ammette. Si ripulisce qui, una volta, invece di
 * scoprirlo al primo `Pulsante 1_Clic()` che non compila. */
static void nome_pulito(char *s)
{
    unsigned int i;

    for (i = 0; s[i]; i++) {
        char c = s[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_'))
            s[i] = '_';
    }
    if (s[0] >= '0' && s[0] <= '9') s[0] = '_';
}

/* Definita piu' avanti, con le altre operazioni sulle maschere: qui serve
 * perche' cambiare il nome dalla scheda deve aggiornare anche l'elenco. */
static void form_mostra(void);

static void prop_applica(void)
{
    Ctrl *c;
    int   k = (int)ex_lista_scelta(g_lst_prop);
    const char *v = ex_testo_prendi(g_val);
    int   n;

    if (g_sel < 0 || !g_ctrl[g_sel].usato) {
        /* Senza controllo scelto le proprieta' sono quelle della maschera. */
        switch (k) {
        case 0:
            /* ! IL NOME DELLA MASCHERA DIVENTA UN NOME DI FUNZIONE, come
             * quello di un controllo: stessa ripulitura, per la stessa
             * ragione. E due maschere non possono chiamarsi uguale, o il
             * generatore scriverebbe due volte la stessa funzione. */
            if (v[0] == '\0') { dico("il nome non puo' essere vuoto"); return; }
            {
                char nuovo[NOME_MAX];
                int  i;

                strncpy(nuovo, v, NOME_MAX - 1);
                nuovo[NOME_MAX - 1] = '\0';
                nome_pulito(nuovo);

                /* ! «finestra» E' PRENOTATO DALLA PRINCIPALE. I suoi nomi
                 * generati sono finestra_crea() e finestra_proc() (vedi
                 * form_crea_fn): una maschera secondaria chiamata cosi'
                 * genererebbe le stesse due funzioni una seconda volta, e a
                 * fermarsi sarebbe il compilatore su un file che nessuno ha
                 * scritto a mano. */
                if (!form_e_principale(g_form_sel) &&
                    strcmp(nuovo, "finestra") == 0) {
                    dico("«finestra» e' il nome della maschera principale");
                    return;
                }
                for (i = 0; i < FORM_MAX; i++)
                    if (i != g_form_sel && g_form[i].usato &&
                        strcmp(g_form[i].nome, nuovo) == 0) {
                        dico("c'e' gia' una maschera con questo nome");
                        return;
                    }
                istante_segna();    /* dopo i controlli: si cambia da qui */
                strcpy(forma()->nome, nuovo);
            }
            break;
        case 1:
            istante_segna();
            strncpy(forma()->titolo, v, TESTO_MAX - 1);
            forma()->titolo[TESTO_MAX - 1] = '\0';
            break;
        default:
            istante_segna();
            n = atoi(v);
            if (n < 80)   n = 80;
            if (n > 1000) n = 1000;
            if (k == 2) forma()->w = n; else forma()->h = n;
            break;
        }
        g_sporco = 1;
        form_mostra();
        prop_mostra();
        dico("maschera aggiornata");
        return;
    }

    c = &g_ctrl[g_sel];

    /* ! L'EVENTO SI GUARDA PRIMA DI TOCCARE QUALUNQUE COSA. Scriverne uno che
     * il controllo non ha non cambia niente, e prima di oggi diceva lo stesso
     * «proprieta' applicata» e segnava il disegno da salvare — adesso lo dice
     * e si ferma, cosi' non lascia nemmeno un passo indietro che non fa
     * niente. */
    if (k == 7) {
        int e;

        for (e = 0; e < EVENTI_MAX; e++)
            if (g_strum[c->tipo].evento[e] &&
                strcmp(g_strum[c->tipo].evento[e], v) == 0)
                break;
        if (e == EVENTI_MAX) {
            dico("evento sconosciuto per questo controllo");
            return;
        }
        istante_segna();
        c->evento = e;
        g_sporco = 1;
        prop_mostra();
        dico("proprieta' applicata");
        return;
    }

    istante_segna();

    switch (k) {
    case 0:
        strncpy(c->nome, v, NOME_MAX - 1);
        c->nome[NOME_MAX - 1] = '\0';
        nome_pulito(c->nome);
        break;
    case 1:
        strncpy(c->testo, v, TESTO_MAX - 1);
        c->testo[TESTO_MAX - 1] = '\0';
        break;
    case 2: c->x = atoi(v); break;
    case 3: c->y = atoi(v); break;
    case 4: c->w = atoi(v) < MISURA_MIN ? MISURA_MIN : atoi(v); break;
    case 5: c->h = atoi(v) < MISURA_MIN ? MISURA_MIN : atoi(v); break;
    case 6: c->id = (unsigned int)atoi(v); break;
    default: break;                 /* l'evento, k == 7, e' gia' passato sopra */
    }

    g_sporco = 1;
    prop_mostra();
    dico("proprieta' applicata");
}

/* =============================================================================
 * METTERE, SCEGLIERE, SPOSTARE
 * ============================================================================= */
static int ctrl_in(int x, int y)
{
    int ox = TELA_X + 8, oy = TELA_Y + 28;
    int i;

    /* All'indietro: chi e' stato messo dopo sta sopra, come nel toolkit. */
    for (i = CTRL_MAX - 1; i >= 0; i--) {
        Ctrl *c = &g_ctrl[i];

        if (!c->usato || c->form != g_form_sel) continue;
        if (x >= ox + c->x && x < ox + c->x + c->w &&
            y >= oy + c->y && y < oy + c->y + c->h)
            return i;
    }
    return -1;
}

/* Quale maniglia del controllo SCELTO sta sotto (x,y), o -1. */
static int maniglia_in(int x, int y)
{
    int ox = TELA_X + 8, oy = TELA_Y + 28;
    int i, cx, cy;

    if (g_sel < 0 || !g_ctrl[g_sel].usato) return -1;

    for (i = 0; i < 8; i++) {
        maniglia_centro(&g_ctrl[g_sel], i, ox, oy, &cx, &cy);
        if (x >= cx - MAN_PRESA / 2 && x <= cx + MAN_PRESA / 2 &&
            y >= cy - MAN_PRESA / 2 && y <= cy + MAN_PRESA / 2)
            return i;
    }
    return -1;
}

/* -----------------------------------------------------------------------------
 * ! SI TIRA IL BORDO CHE LA MANIGLIA RAPPRESENTA, E GLI ALTRI RESTANO DOVE
 * SONO. E' tutta la differenza fra ridimensionare e spostare: tirando la
 * maniglia di sinistra cambiano `x` E `larghezza` insieme, perche' il bordo
 * DESTRO non si deve muovere. Cambiando la sola larghezza il controllo
 * scivolerebbe a destra mentre lo si tira a sinistra.
 *
 * ! E I LIMITI SI APPLICANO AL BORDO CHE SI MUOVE, non alla misura. Se la
 * misura minima si facesse rispettare accorciando «da destra» mentre si tira
 * il bordo sinistro, il controllo si metterebbe a scappare appena arrivato al
 * minimo. Qui il bordo tirato si ferma e l'altro non si muove mai.
 * --------------------------------------------------------------------------- */
static void ridimensiona(int mx, int my)
{
    int   ox = TELA_X + 8, oy = TELA_Y + 28;
    Ctrl *c = &g_ctrl[g_sel];
    int   sx = c->x, sy = c->y;                 /* i bordi: sinistro e alto */
    int   dx = c->x + c->w, dy = c->y + c->h;   /* destro e basso */
    int   px = arrotonda(mx - ox), py = arrotonda(my - oy);

    if (g_man_x[g_ridim] < 0) sx = px;
    if (g_man_x[g_ridim] > 0) dx = px;
    if (g_man_y[g_ridim] < 0) sy = py;
    if (g_man_y[g_ridim] > 0) dy = py;

    /* Dentro la maschera. */
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (dx > forma()->w) dx = forma()->w;
    if (dy > forma()->h) dy = forma()->h;

    /* E mai piu' piccolo del minimo: si ferma il bordo che si sta tirando. */
    if (dx - sx < MISURA_MIN) {
        if (g_man_x[g_ridim] < 0) sx = dx - MISURA_MIN;
        else                      dx = sx + MISURA_MIN;
    }
    if (dy - sy < MISURA_MIN) {
        if (g_man_y[g_ridim] < 0) sy = dy - MISURA_MIN;
        else                      dy = sy + MISURA_MIN;
    }

    if (sx == c->x && sy == c->y && dx == c->x + c->w && dy == c->y + c->h)
        return;                                 /* niente e' cambiato */

    if (!g_tras_segnato) { istante_segna(); g_tras_segnato = 1; }

    c->x = sx;
    c->y = sy;
    c->w = dx - sx;
    c->h = dy - sy;
    g_sporco = 1;

    {
        char t[64];

        sprintf(t, "%s: %d x %d", c->nome, c->w, c->h);
        dico(t);
    }
    prop_mostra();
    disegna_tela();
    ex_aggiorna(g_f);
}

static unsigned int id_libero(void)
{
    unsigned int id = 1001;
    int i, scontro = 1;

    while (scontro) {
        scontro = 0;
        for (i = 0; i < CTRL_MAX; i++)
            if (g_ctrl[i].usato && g_ctrl[i].id == id) { id++; scontro = 1; break; }
    }
    return id;
}

static int aggiungi(int tipo, int x, int y)
{
    int i, n = 1;
    Ctrl *c;

    for (i = 0; i < CTRL_MAX; i++) if (!g_ctrl[i].usato) break;
    if (i == CTRL_MAX) { dico("non c'e' posto per un altro controllo"); return -1; }

    /* Dopo il controllo del posto: se non se ne aggiunge nessuno non c'e'
     * niente da annullare, e un passo indietro che non fa niente e' peggio di
     * non averlo. */
    istante_segna();

    c = &g_ctrl[i];
    memset(c, 0, sizeof(*c));
    c->usato = 1;
    c->tipo  = tipo;
    c->form  = g_form_sel;          /* nasce nella maschera che si sta disegnando */
    c->x = arrotonda(x);
    c->y = arrotonda(y);
    c->w = g_strum[tipo].w;
    c->h = g_strum[tipo].h;
    c->id = id_libero();
    c->evento = 0;

    /* Il nome: prefisso piu' il primo numero libero. */
    for (;;) {
        int k, preso = 0;

        sprintf(c->nome, "%s%d", g_strum[tipo].prefisso, n);
        for (k = 0; k < CTRL_MAX; k++)
            if (k != i && g_ctrl[k].usato && strcmp(g_ctrl[k].nome, c->nome) == 0)
                { preso = 1; break; }
        if (!preso) break;
        n++;
    }
    strncpy(c->testo, c->nome, TESTO_MAX - 1);
    c->testo[TESTO_MAX - 1] = '\0';

    g_sporco = 1;
    return i;
}

/* =============================================================================
 * LE MASCHERE — aggiungerne una, sceglierla, toglierla
 * ============================================================================= */
static void form_mostra(void)
{
    char riga[NOME_MAX + TESTO_MAX + 8];
    int  i, quale = 0, n = 0;

    if (g_cmb_form == 0) return;    /* si chiama anche prima che esista */

    ex_voci_svuota(g_cmb_form);
    for (i = 0; i < FORM_MAX; i++) {
        if (!g_form[i].usato) continue;
        /* Il nome e' quello che conta nel codice generato; il titolo e' quello
         * che si legge a schermo. Qui servono tutti e due: si sceglie col
         * primo e ci si riconosce col secondo. */
        sprintf(riga, "%s - %s", g_form[i].nome, g_form[i].titolo);
        ex_voce_aggiungi(g_cmb_form, riga);
        if (i == g_form_sel) quale = n;
        n++;
    }
    ex_voce_scegli(g_cmb_form, (unsigned int)quale);
}

/* Dalla riga scelta nell'elenco alla maschera: non coincidono appena se ne
 * toglie una di mezzo, perche' l'elenco salta i buchi e l'indice no. */
static int form_da_riga(int riga)
{
    int i, n = 0;

    for (i = 0; i < FORM_MAX; i++) {
        if (!g_form[i].usato) continue;
        if (n == riga) return i;
        n++;
    }
    return 0;
}

static void form_scegli(int quale)
{
    if (quale < 0 || quale >= FORM_MAX || !g_form[quale].usato) return;

    g_form_sel = quale;
    g_sel = -1;                     /* la scelta non attraversa le maschere */
    form_mostra();
    prop_mostra();
    disegna_tela();
    ex_aggiorna(g_f);
    dico(g_form[quale].titolo);
}

static void form_nuova(void)
{
    char nome[NOME_MAX], titolo[TESTO_MAX];
    int  i, k, n;

    for (i = 0; i < FORM_MAX; i++) if (!g_form[i].usato) break;
    if (i == FORM_MAX) { dico("non c'e' posto per un'altra maschera"); return; }

    istante_segna();

    /* Il nome: «finestra2», «finestra3»... il primo numero libero. */
    for (n = 2; ; n++) {
        int preso = 0;

        sprintf(nome, "finestra%d", n);
        for (k = 0; k < FORM_MAX; k++)
            if (g_form[k].usato && strcmp(g_form[k].nome, nome) == 0)
                { preso = 1; break; }
        if (!preso) break;
    }
    sprintf(titolo, "Finestra %d", n);

    form_azzera(&g_form[i], nome, titolo);
    g_sporco = 1;
    form_scegli(i);
    dico("maschera nuova: si apre dal codice con <nome>_crea()");
}

static void form_togli(void)
{
    int i, quanti = 0;

    /* ! LA PRINCIPALE NON SI TOGLIE: e' quella che il programma apre da solo. */
    if (form_e_principale(g_form_sel)) {
        dico("la maschera principale non si toglie");
        return;
    }

    for (i = 0; i < CTRL_MAX; i++)
        if (g_ctrl[i].usato && g_ctrl[i].form == g_form_sel) quanti++;

    /* ! SI CHIEDE, PERCHE' PORTA VIA ANCHE I CONTROLLI. Cancellare un
     * controllo per sbaglio costa un controllo; cancellare una maschera ne
     * costa dieci, e finche' non c'e' Annulla non si torna indietro. Il
     * codice scritto a mano nei suoi handler resta comunque in finestra.c —
     * exide non cancella mai quello. */
    {
        char avviso[160];

        sprintf(avviso, "Togliere la maschera «%s» e i %d controlli che ci "
                        "stanno dentro?\n\nGli handler gia' scritti in "
                        "finestra.c restano dove sono.",
                g_form[g_form_sel].titolo, quanti);
        if (!ex_dlg_conferma("Togliere la maschera", avviso, "Togli", "Lascia"))
            return;
    }

    istante_segna();                /* dopo il «si'»: annullare un no non serve */

    for (i = 0; i < CTRL_MAX; i++)
        if (g_ctrl[i].usato && g_ctrl[i].form == g_form_sel)
            g_ctrl[i].usato = 0;

    g_form[g_form_sel].usato = 0;
    g_sporco = 1;
    form_scegli(0);
    dico("maschera tolta");
}

/* Un passo indietro: si rimette l'ultima fotografia e la si toglie dall'anello. */
static void annulla(void)
{
    Istante *s;
    char     t[64];

    if (g_storia_n == 0) { dico("non c'e' niente da annullare"); return; }

    g_storia_testa = (g_storia_testa + ANNULLA_MAX - 1) % ANNULLA_MAX;
    g_storia_n--;
    s = &g_storia[g_storia_testa];

    memcpy(g_ctrl, s->ctrl, sizeof(g_ctrl));
    memcpy(g_form, s->form, sizeof(g_form));
    g_form_sel = s->form_sel;
    g_sel      = s->sel;

    /* ! RESTA SPORCO ANCHE TORNANDO INDIETRO, e non e' una dimenticanza:
     * l'unica cosa che si sa e' che il disegno in memoria non e' piu' quello
     * scritto su disco. Contare i passi per accorgersi di essere tornati
     * esattamente al punto salvato vorrebbe dire tenere anche il numero del
     * salvataggio, per risparmiare a chi esce una domanda a cui puo'
     * rispondere «no». */
    g_sporco = 1;

    /* ! IL RITORNO PUO' RIPORTARE UNA MASCHERA CHE NON C'E' PIU'. Se si e'
     * annullato mentre si guardava una maschera tolta e poi rimessa, l'indice
     * torna valido da solo perche' arriva dalla fotografia; se pero' punta a
     * un posto vuoto — non dovrebbe mai, ma il disegno viene anche da un file
     * scritto da qualcun altro — si torna sulla principale invece di
     * disegnare una maschera che non esiste. */
    if (g_form_sel < 0 || g_form_sel >= FORM_MAX || !g_form[g_form_sel].usato)
        g_form_sel = 0;

    form_mostra();
    prop_mostra();
    disegna_tela();
    ex_aggiorna(g_f);

    sprintf(t, "annullato (restano %d passi indietro)", g_storia_n);
    dico(t);
}

/* =============================================================================
 * IL PROGETTO — una directory con dentro cinque, e il file che le descrive
 *
 *     <progetto>/src   i sorgenti: finestra.dis, .h, _gen.c e il tuo .c
 *     <progetto>/inc   le intestazioni tue
 *     <progetto>/lib   le librerie tue
 *     <progetto>/bin   il programma che esce
 *     <progetto>/obj   gli oggetti intermedi
 *
 * ! LE DIRECTORY SI FANNO TUTTE SUBITO, anche quelle che restano vuote per
 * settimane: una struttura che nasce a pezzi e' una struttura in cui ognuno
 * mette le cose in un posto diverso, e il compilatore poi non le trova.
 * ============================================================================= */
static void percorso(char *out, const char *file)
{
    sprintf(out, "%s/src/%s", g_prog_dir, file);
}

/* =============================================================================
 * ! «MKDIR E' FALLITO» NON VUOL DIRE «C'ERA GIA'», e per un giorno intero
 * questo programma ha creduto di si'. Avviando EX-OS dal CD non si puo'
 * scrivere DA NESSUNA PARTE: la creazione del progetto falliva a ogni passo, e
 * poiche' nessun passo si lamentava, exide arrivava fino ad aprire l'editor —
 * VUOTO, perche' il file da leggere non era mai stato scritto. Il sintomo era
 * «l'IDE si apre vuoto», che non somiglia affatto a «il disco e' in sola
 * lettura».
 *
 * ! LA PROVA CHE CONTA E' SCRIVERE, non chiedere. Un filesystem puo' rifiutare
 * per mille ragioni — sola lettura, permessi, spazio finito — e distinguerle
 * costerebbe piu' di quanto serva: quel che conta e' se il file c'e' dopo che
 * si e' provato a farlo. Percio' si prova, e se non riesce si dice E SI FERMA.
 * ============================================================================= */
static int progetto_crea(const char *dir)
{
    char p[PERC_MAX], avviso[PERC_MAX + 120];
    static const char *const sotto[] = { "src", "inc", "lib", "bin", "obj", 0 };
    int i, fd;
    RtcTime ora;

    mkdir(dir, 0755);                   /* se c'era gia', si riusa */
    for (i = 0; sotto[i]; i++) {
        sprintf(p, "%s/%s", dir, sotto[i]);
        mkdir(p, 0755);
    }

    /* La prova: se questo file non si scrive, li' dentro non si scrive niente. */
    sprintf(p, "%s/src/finestra.dis", dir);
    fd = open(p, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        sprintf(avviso,
                "Non riesco a scrivere in %s.\n\n"
                "Avviando EX-OS dal CD non si puo' scrivere da nessuna "
                "parte: serve un disco montato in lettura e scrittura, per "
                "esempio  mount hd0p1 /disk  e poi un progetto in /disk.", dir);
        ex_dlg_avviso("Non si puo' scrivere", avviso);
        dico("il progetto non e' stato creato: non si puo' scrivere li'");
        return 0;
    }
    close(fd);

    /* ! LA SCHEDA SI SCRIVE SOLO SE NON C'ERA GIA', e non e' la stessa regola
     * di finestra.dis qui sopra: quella si apre senza O_TRUNC e non scrive
     * niente, quindi un file gia' presente resta byte per byte quello che
     * era. progetto.txt aveva l'O_TRUNC — «Nuovo progetto» puntato su una
     * directory gia' esistente RISCRIVEVA la scheda ogni volta, cancellando
     * l'autore, la versione, la descrizione e la nota che si erano appena
     * salvate dalla finestra «Progetto». Il disegno restava intatto, la
     * scheda no: la stessa azione trattava due file dello stesso progetto
     * in due modi diversi, e chi riapriva un progetto per sbagliarci sopra
     * un «Nuovo progetto» invece che un «Apri» si ritrovava la nota persa
     * senza nessun avviso.
     *
     * Si guarda prima se il file esiste — con una lettura, che non lo tocca
     * — e si scrive SOLO se manca. */
    sprintf(p, "%s/progetto.txt", dir);
    fd = open(p, O_RDONLY, 0);
    if (fd >= 0) { close(fd); return 1; }       /* c'era gia': non si tocca */

    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char riga[256];
        const char *n = strrchr(dir, '/');

        n = n ? n + 1 : dir;
        memset(&ora, 0, sizeof(ora));
        time_now(&ora);
        /* ! LO STESSO FORMATO CHE SCRIVE progetto_scheda_salva(), campo per
         * campo — compreso il marcatore [nota] con la nota vuota dopo. Un
         * file appena nato e uno appena salvato dalla finestra «Progetto»
         * devono essere indistinguibili: due formati per lo stesso file
         * sarebbero due posti da tenere d'accordo. */
        sprintf(riga, "nome = %s\nautore = %s\nversione = 0.001\n"
                      "creato = %04u-%02u-%02u\ndescrizione = \n\n[nota]\n",
                n, EXINFO_AUTORE,
                (unsigned int)ora.anno, (unsigned int)ora.mese,
                (unsigned int)ora.giorno);
        write(fd, riga, strlen(riga));
        close(fd);
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * Il file del disegno
 *
 * ! UNA RIGA PER CONTROLLO, E IL TESTO IN FONDO. Il testo di un'etichetta puo'
 * contenere spazi; qualunque altro ordine dei campi vorrebbe dire virgolette,
 * e le virgolette vogliono dire cosa succede a chi ne scrive una nel testo.
 * Mettendolo per ultimo la domanda non esiste: il resto della riga E' il testo.
 * --------------------------------------------------------------------------- */
/* ! LE MASCHERE SONO PIU' D'UNA, E IL FORMATO NON E' CAMBIATO PER QUESTO. Una
 * riga «F» apre una maschera e le righe «c» che seguono sono sue, fino alla
 * «F» dopo: e' lo stesso ordine che il file aveva gia' quando la maschera era
 * una sola. La riga vecchia si chiamava «f» e non aveva il nome; si continua a
 * leggerla (vedi dis_carica) e si smette di scriverla, cosi' un progetto fatto
 * prima di oggi si apre senza accorgersi di niente.
 *
 * ! IL NOME PRIMA DEL TITOLO, e non e' un dettaglio: il titolo puo' contenere
 * spazi ed e' per questo che sta in fondo alla riga. Il nome no — nome_pulito()
 * lo garantisce — quindi puo' stare in mezzo. */
static int dis_salva(void)
{
    char p[PERC_MAX], riga[256];
    int  fd, i, k;

    percorso(p, "finestra.dis");
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { dico("non riesco a scrivere finestra.dis"); return 0; }

    sprintf(riga, "# exide 0.002 - lo scrive e lo legge solo exide\n");
    write(fd, riga, strlen(riga));

    for (k = 0; k < FORM_MAX; k++) {
        if (!g_form[k].usato) continue;

        sprintf(riga, "F %s %d %d %s\n",
                g_form[k].nome, g_form[k].w, g_form[k].h, g_form[k].titolo);
        write(fd, riga, strlen(riga));

        for (i = 0; i < CTRL_MAX; i++) {
            Ctrl *c = &g_ctrl[i];

            if (!c->usato || c->form != k) continue;
            sprintf(riga, "c %s %s %u %d %d %d %d %d %s\n",
                    g_strum[c->tipo].classe, c->nome, c->id,
                    c->x, c->y, c->w, c->h, c->evento, c->testo);
            write(fd, riga, strlen(riga));
        }
    }
    close(fd);
    return 1;
}

/* Una parola dalla riga: rende dove ricomincia il resto. */
static const char *parola(const char *s, char *out, unsigned int max)
{
    unsigned int n = 0;

    while (*s == ' ') s++;
    while (*s && *s != ' ' && *s != '\n' && n + 1 < max) out[n++] = *s++;
    out[n] = '\0';
    while (*s == ' ') s++;
    return s;
}

static int dis_carica(void)
{
    char p[PERC_MAX], buf[512], riga[256], w[64];
    int  fd, n, i;
    int  corrente = 0;          /* la maschera a cui appartengono le «c» */
    int  vista_una = 0;         /* la prima «F» riempie la principale */
    unsigned int col = 0;

    percorso(p, "finestra.dis");
    fd = open(p, O_RDONLY, 0);
    if (fd < 0) return 0;

    memset(g_ctrl, 0, sizeof(g_ctrl));
    memset(g_form, 0, sizeof(g_form));
    form_azzera(&g_form[0], "principale", "Finestra");
    g_form_sel = 0;
    corrente = 0;
    g_sel = -1;

    while ((n = (int)read(fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < n; i++) {
            const char *s;

            if (buf[i] != '\n') {
                if (col + 1 < sizeof(riga)) riga[col++] = buf[i];
                continue;
            }
            riga[col] = '\0';
            col = 0;

            if (riga[0] == '#' || riga[0] == '\0') continue;
            s = parola(riga, w, sizeof(w));

            /* La riga vecchia, senza nome: e' sempre la maschera principale.
             * Un file scritto prima del 3 settembre 2026 ha solo questa. */
            if (strcmp(w, "f") == 0) {
                s = parola(s, w, sizeof(w)); g_form[0].w = atoi(w);
                s = parola(s, w, sizeof(w)); g_form[0].h = atoi(w);
                strncpy(g_form[0].titolo, s, TESTO_MAX - 1);
                g_form[0].titolo[TESTO_MAX - 1] = '\0';
                corrente = 0;
                continue;
            }

            /* La riga nuova: nome, misura, titolo. La prima riempie la
             * principale — che c'e' gia' — e le altre aprono una maschera. */
            if (strcmp(w, "F") == 0) {
                int k;

                if (!vista_una) { k = 0; vista_una = 1; }
                else {
                    for (k = 0; k < FORM_MAX; k++) if (!g_form[k].usato) break;
                    /* ! PIU' DI FORM_MAX: SI SALTA LA MASCHERA E ANCHE I SUOI
                     * CONTROLLI. Lasciando `corrente` dov'era, le righe «c»
                     * che seguono finirebbero nella maschera PRECEDENTE —
                     * controlli che spuntano dentro una finestra che non e'
                     * la loro, che e' peggio di non vederli affatto. */
                    if (k == FORM_MAX) { corrente = -1; continue; }
                }

                memset(&g_form[k], 0, sizeof(g_form[k]));
                g_form[k].usato = 1;
                s = parola(s, g_form[k].nome, NOME_MAX);
                s = parola(s, w, sizeof(w)); g_form[k].w = atoi(w);
                s = parola(s, w, sizeof(w)); g_form[k].h = atoi(w);
                strncpy(g_form[k].titolo, s, TESTO_MAX - 1);
                g_form[k].titolo[TESTO_MAX - 1] = '\0';
                if (g_form[k].nome[0] == '\0')
                    sprintf(g_form[k].nome, "finestra%d", k + 1);
                corrente = k;
                continue;
            }
            if (strcmp(w, "c") != 0) continue;
            if (corrente < 0) continue;     /* la sua maschera non c'e' entrata */

            {
                int k, tipo = -1;
                Ctrl *c;

                s = parola(s, w, sizeof(w));
                for (k = 0; k < STRUM_N; k++)
                    if (strcmp(g_strum[k].classe, w) == 0) { tipo = k; break; }
                if (tipo < 0) continue;     /* classe sconosciuta: si salta */

                for (k = 0; k < CTRL_MAX; k++) if (!g_ctrl[k].usato) break;
                if (k == CTRL_MAX) continue;

                c = &g_ctrl[k];
                memset(c, 0, sizeof(*c));
                c->usato = 1;
                c->tipo  = tipo;
                c->form  = corrente;
                s = parola(s, c->nome, NOME_MAX);
                s = parola(s, w, sizeof(w)); c->id = (unsigned int)atoi(w);
                s = parola(s, w, sizeof(w)); c->x = atoi(w);
                s = parola(s, w, sizeof(w)); c->y = atoi(w);
                s = parola(s, w, sizeof(w)); c->w = atoi(w);
                s = parola(s, w, sizeof(w)); c->h = atoi(w);
                s = parola(s, w, sizeof(w)); c->evento = atoi(w);
                strncpy(c->testo, s, TESTO_MAX - 1);
                c->testo[TESTO_MAX - 1] = '\0';
            }
        }
    }
    close(fd);
    return 1;
}

/* =============================================================================
 * GENERARE IL C
 *
 * ! QUEL CHE SI GENERA NON SI LEGGE MAI PIU', e per questo va scritto come se
 * qualcuno dovesse leggerlo. Un generatore che sputa righe illeggibili produce
 * un file che nessuno apre nemmeno quando il compilatore ci si ferma sopra — e
 * allora l'errore sembra del compilatore.
 * ============================================================================= */
static void nome_id(const Ctrl *c, char *out)
{
    unsigned int i;

    strcpy(out, "ID_");
    for (i = 0; c->nome[i] && i < NOME_MAX - 1; i++) {
        char ch = c->nome[i];

        out[3 + i] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
    }
    out[3 + i] = '\0';
}

static int ha_evento(const Ctrl *c)
{
    return g_strum[c->tipo].evento[c->evento] != 0;
}

static void nome_handler(const Ctrl *c, char *out)
{
    sprintf(out, "%s_%s", c->nome, g_strum[c->tipo].evento[c->evento]);
}

/* ! I NOMI DELLA MASCHERA PRINCIPALE NON HANNO IL SUO NOME DENTRO, e quelli
 * delle altre si'. Sarebbe piu' simmetrico chiamarle tutte allo stesso modo —
 * `principale_crea()`, `opzioni_crea()` — e ogni progetto fatto prima di oggi
 * smetterebbe di compilare: il suo finestra.c, che exide non riscrive MAI,
 * chiama `finestra_crea()` dal main. Fra la simmetria e i progetti che
 * continuano ad aprirsi non c'e' partita. */
static void form_var(int k, char *out)
{
    if (form_e_principale(k)) strcpy(out, "g_form");
    else sprintf(out, "g_form_%s", g_form[k].nome);
}

static void form_crea_fn(int k, char *out)
{
    if (form_e_principale(k)) strcpy(out, "finestra_crea");
    else sprintf(out, "%s_crea", g_form[k].nome);
}

static void form_proc_fn(int k, char *out)
{
    if (form_e_principale(k)) strcpy(out, "finestra_proc");
    else sprintf(out, "%s_proc", g_form[k].nome);
}

static int gen_h(void)
{
    char p[PERC_MAX], riga[256], idn[NOME_MAX + 8], hn[NOME_MAX + 32];
    char fn[NOME_MAX + 16], pn[NOME_MAX + 16];
    int  fd, i, k;

    percorso(p, "finestra.h");
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;

#define SCRIVI(s) write(fd, (s), strlen(s))
    SCRIVI("/* GENERATO DA EXIDE - non modificare: si riscrive a ogni\n"
           " * salvataggio. Il tuo codice sta in finestra.c. */\n"
           "#ifndef FINESTRA_H\n#define FINESTRA_H\n\n"
           "#include \"exwin.h\"\n\n");

    for (i = 0; i < CTRL_MAX; i++)
        if (g_ctrl[i].usato) {
            nome_id(&g_ctrl[i], idn);
            sprintf(riga, "#define %-20s %u\n", idn, g_ctrl[i].id);
            SCRIVI(riga);
        }

    SCRIVI("\n");
    for (k = 0; k < FORM_MAX; k++)
        if (g_form[k].usato) {
            form_var(k, fn);
            sprintf(riga, "extern ExFinestra %s;\n", fn);
            SCRIVI(riga);
        }

    for (i = 0; i < CTRL_MAX; i++)
        if (g_ctrl[i].usato) {
            sprintf(riga, "extern ExFinestra h_%s;\n", g_ctrl[i].nome);
            SCRIVI(riga);
        }

    SCRIVI("\n/* La principale la apre il main di finestra.c. LE ALTRE LE APRI\n"
           " * TU, chiamando la loro <nome>_crea() da dove serve — di solito\n"
           " * dall'handler di un pulsante. Chiamarla due volte non apre due\n"
           " * finestre: se c'e' gia', la ridisegna e basta.\n"
           " *\n"
           " * Chiudendone una col suo pulsante, la finestra viene distrutta e\n"
           " * il suo handle torna a zero: il programma NON esce. Esce solo\n"
           " * chiudendo la principale. */\n");

    for (k = 0; k < FORM_MAX; k++)
        if (g_form[k].usato) {
            form_crea_fn(k, fn);
            form_proc_fn(k, pn);
            sprintf(riga, "void %s(void);\n", fn);
            SCRIVI(riga);
            sprintf(riga, "long %s(ExFinestra f, unsigned int msg,\n"
                          "%*sunsigned int wp, long lp);\n",
                    pn, (int)strlen(pn) + 6, "");
            SCRIVI(riga);
        }

    SCRIVI("\n/* Gli handler degli eventi: li scrivi tu in finestra.c. */\n");

    for (i = 0; i < CTRL_MAX; i++)
        if (g_ctrl[i].usato && ha_evento(&g_ctrl[i])) {
            nome_handler(&g_ctrl[i], hn);
            sprintf(riga, "void %s(void);\n", hn);
            SCRIVI(riga);
        }

    SCRIVI("\n#endif /* FINESTRA_H */\n");
#undef SCRIVI
    close(fd);
    return 1;
}

static int gen_c(void)
{
    char p[PERC_MAX], riga[320], idn[NOME_MAX + 8], hn[NOME_MAX + 32];
    char fn[NOME_MAX + 16], pn[NOME_MAX + 16], vn[NOME_MAX + 16];
    int  fd, i, k;

    percorso(p, "finestra_gen.c");
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;

#define SCRIVI(s) write(fd, (s), strlen(s))
    SCRIVI("/* GENERATO DA EXIDE - non modificare: si riscrive a ogni\n"
           " * salvataggio. Il tuo codice sta in finestra.c. */\n\n"
           "#include \"libc.h\"\n#include \"finestra.h\"\n\n");

    for (k = 0; k < FORM_MAX; k++)
        if (g_form[k].usato) {
            form_var(k, vn);
            sprintf(riga, "ExFinestra %s;\n", vn);
            SCRIVI(riga);
        }

    for (i = 0; i < CTRL_MAX; i++)
        if (g_ctrl[i].usato) {
            sprintf(riga, "ExFinestra h_%s;\n", g_ctrl[i].nome);
            SCRIVI(riga);
        }

    for (k = 0; k < FORM_MAX; k++) {
        if (!g_form[k].usato) continue;

        form_var(k, vn);
        form_crea_fn(k, fn);
        form_proc_fn(k, pn);

        /* ! CHIAMARLA DUE VOLTE NON APRE DUE FINESTRE. Una finestra si apre
         * dall'handler di un pulsante, e un pulsante si preme piu' di una
         * volta: senza questa riga il decimo clic darebbe la decima copia
         * della stessa finestra, tutte vive e tutte con gli stessi id. */
        sprintf(riga, "\nvoid %s(void)\n{\n"
                      "    if (%s) {\n"
                      "        ex_procedura_base(%s, EXM_DISEGNA, 0, 0);\n"
                      "        return;\n    }\n\n", fn, vn, vn);
        SCRIVI(riga);

        sprintf(riga, "    %s = ex_crea(\"finestra\", \"%s\",\n"
                      "        EX_TITOLO | EX_BORDO | EX_CHIUDI,\n"
                      "        EX_AUTO, EX_AUTO, %d, %d, 0, 0, %s);\n"
                      "    if (%s == 0) return;\n\n",
                vn, g_form[k].titolo, g_form[k].w, g_form[k].h, pn, vn);
        SCRIVI(riga);

        for (i = 0; i < CTRL_MAX; i++) {
            Ctrl *c = &g_ctrl[i];

            if (!c->usato || c->form != k) continue;
            nome_id(c, idn);
            sprintf(riga, "    h_%s = ex_crea(\"%s\", \"%s\", EX_FIGLIO,\n"
                          "        %d, %d, %d, %d, %s, %s, 0);\n",
                    c->nome, g_strum[c->tipo].classe, c->testo,
                    c->x, c->y, c->w, c->h, vn, idn);
            SCRIVI(riga);
        }

        sprintf(riga, "\n    ex_procedura_base(%s, EXM_DISEGNA, 0, 0);\n}\n\n",
                vn);
        SCRIVI(riga);

        sprintf(riga, "long %s(ExFinestra f, unsigned int msg,\n"
                      "%*sunsigned int wp, long lp)\n{\n"
                      "    if (msg == EXM_COMANDO) {\n"
                      "        switch (wp) {\n",
                pn, (int)strlen(pn) + 6, "");
        SCRIVI(riga);

        for (i = 0; i < CTRL_MAX; i++) {
            Ctrl *c = &g_ctrl[i];

            if (!c->usato || c->form != k || !ha_evento(c)) continue;
            nome_id(c, idn);
            nome_handler(c, hn);
            sprintf(riga, "        case %s: %s(); return 0;\n", idn, hn);
            SCRIVI(riga);
        }

        SCRIVI("        default: break;\n        }\n    }\n\n");

        if (form_e_principale(k)) {
            SCRIVI("    if (msg == EXM_CHIUDI) { ex_esci(0); return 0; }\n");
        } else {
            /* ! CHIUDERE UNA SECONDARIA NON ESCE DAL PROGRAMMA, e gli handle
             * tornano a zero. Il secondo pezzo conta quanto il primo: senza,
             * h_Casella1 continuerebbe a puntare a un controllo distrutto, e
             * chi lo tocca da un handler della finestra rimasta aperta
             * lavorerebbe su un fantasma. Riaprendola, <nome>_crea() li
             * riempie di nuovo. */
            sprintf(riga, "    if (msg == EXM_CHIUDI) {\n"
                          "        ex_distruggi(%s);\n"
                          "        %s = 0;\n", vn, vn);
            SCRIVI(riga);
            for (i = 0; i < CTRL_MAX; i++)
                if (g_ctrl[i].usato && g_ctrl[i].form == k) {
                    sprintf(riga, "        h_%s = 0;\n", g_ctrl[i].nome);
                    SCRIVI(riga);
                }
            SCRIVI("        return 0;\n    }\n");
        }

        SCRIVI("    return ex_procedura_base(f, msg, wp, lp);\n}\n");
    }
#undef SCRIVI
    close(fd);
    return 1;
}

/* =============================================================================
 * IL FILE CHE E' TUO
 *
 * ! SI CREA UNA VOLTA E POI NON SI RISCRIVE PIU'. Da qui in avanti exide ci
 * AGGIUNGE in fondo gli handler che mancano, e non tocca niente di quel che
 * c'e' — nemmeno un handler di un controllo cancellato: cancellare del codice
 * scritto a mano perche' un rettangolo non c'e' piu' sulla maschera sarebbe la
 * cosa peggiore che un generatore possa fare.
 * ============================================================================= */
static int mio_c_assicura(void)
{
    char p[PERC_MAX];
    int  fd;

    percorso(p, "finestra.c");
    fd = open(p, O_RDONLY, 0);
    if (fd >= 0) { close(fd); return 1; }

    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;

#define SCRIVI(s) write(fd, (s), strlen(s))
    SCRIVI("/* Questo file e' TUO: exide ci aggiunge in fondo gli handler che\n"
           " * mancano e non riscrive mai quel che c'e' gia'.\n"
           " *\n"
           " * Gli id, i puntatori ai controlli e i prototipi degli handler\n"
           " * stanno in finestra.h, che invece si rigenera a ogni salvataggio.\n"
           " */\n\n"
           "#include \"libc.h\"\n#include \"finestra.h\"\n\n"
           "int main(void)\n{\n"
           "    ExMsg m;\n\n"
           "    finestra_crea();\n"
           "    while (ex_prendi_msg(&m)) ex_smista(&m);\n"
           "    return 0;\n}\n");
#undef SCRIVI
    close(fd);
    return 1;
}

/* Cerca `void NOME(` dentro finestra.c. Rende la riga (da 0) o -1. */
static int handler_riga(const char *nome)
{
    char p[PERC_MAX], buf[512], riga[256], cerca[NOME_MAX + 40];
    int  fd, n, i, num = 0;
    unsigned int col = 0;

    sprintf(cerca, "void %s(", nome);
    percorso(p, "finestra.c");
    fd = open(p, O_RDONLY, 0);
    if (fd < 0) return -1;

    while ((n = (int)read(fd, buf, sizeof(buf))) > 0)
        for (i = 0; i < n; i++) {
            if (buf[i] != '\n') {
                if (col + 1 < sizeof(riga)) riga[col++] = buf[i];
                continue;
            }
            riga[col] = '\0';
            col = 0;
            if (strstr(riga, cerca)) { close(fd); return num; }
            num++;
        }
    close(fd);
    return -1;
}

/* Rende la riga su cui si trova l'handler, aggiungendolo in fondo se manca. */
static int handler_assicura(const Ctrl *c)
{
    char p[PERC_MAX], hn[NOME_MAX + 32], testo[256];
    int  fd, riga;

    if (!ha_evento(c)) return -1;
    nome_handler(c, hn);

    riga = handler_riga(hn);
    if (riga >= 0) return riga;

    percorso(p, "finestra.c");
    fd = open(p, O_WRONLY, 0);
    if (fd < 0) return -1;
    lseek(fd, 0, SEEK_END);

    sprintf(testo, "\n/* %s: %s */\nvoid %s(void)\n{\n}\n",
            c->nome, g_strum[c->tipo].evento[c->evento], hn);
    write(fd, testo, strlen(testo));
    close(fd);

    return handler_riga(hn);
}

/* =============================================================================
 * LA FINESTRA DEL SORGENTE
 *
 * ! E' MODALE, ed e' una richiesta e non una comodita': finche' si scrive
 * codice il disegno non si tocca, perche' una modifica alla maschera mentre il
 * suo sorgente e' aperto vorrebbe dire decidere quale delle due versioni vale.
 * Si chiude, e il disegnatore torna sotto le mani.
 *
 * ! E ALLA CHIUSURA SI SALVA DA SE'. Chiedere «vuoi salvare?» a chi ha appena
 * chiuso l'editor di un progetto suo e' una domanda a cui si risponde sempre
 * si': una domanda che ha una sola risposta e' una domanda da non fare.
 * ============================================================================= */
static ExFinestra g_ed, g_ed_cod, g_ed_funz, g_ed_tab, g_ed_stato;
static int        g_ed_file = 0;        /* quale linguetta */

static const char *const g_ed_nomi[3] = {
    "finestra.c", "finestra_gen.c", "finestra.h"
};

/* Le funzioni e le variabili di quel che c'e' nell'area, per la colonna di
 * sinistra. La regola e' grossolana e dichiarata: comincia a colonna zero, non
 * e' un commento ne' una direttiva. Con la parentesi e' una funzione, col punto
 * e virgola e' una variabile. */
static unsigned int g_ed_riga[64];
static unsigned int g_ed_n;

static void ed_indice(void)
{
    unsigned int i, n = ex_area_righe(g_ed_cod);

    g_ed_n = 0;
    ex_lista_svuota(g_ed_funz);

    for (i = 0; i < n && g_ed_n < 64; i++) {
        const char *r = ex_area_riga(g_ed_cod, i);
        unsigned int l = (unsigned int)strlen(r);

        if (l == 0 || r[0] == ' ' || r[0] == '\t' || r[0] == '#') continue;
        if (r[0] == '/' || r[0] == '*' || r[0] == '{' || r[0] == '}') continue;

        if (strchr(r, '(') && r[l - 1] != ';') {
            ex_lista_aggiungi(g_ed_funz, r);
            g_ed_riga[g_ed_n++] = i;
        } else if (!strchr(r, '(') && l > 3 && r[l - 1] == ';') {
            ex_lista_aggiungi(g_ed_funz, r);
            g_ed_riga[g_ed_n++] = i;
        }
    }
}

static int ed_carica(int quale)
{
    char p[PERC_MAX], buf[512], riga[300];
    int  fd, n, i;
    unsigned int col = 0;

    percorso(p, g_ed_nomi[quale]);
    ex_area_svuota(g_ed_cod);

    fd = open(p, O_RDONLY, 0);
    if (fd < 0) { ex_testo_metti(g_ed_stato, "il file non c'e'"); return 0; }

    while ((n = (int)read(fd, buf, sizeof(buf))) > 0)
        for (i = 0; i < n; i++) {
            if (buf[i] == '\r') continue;
            if (buf[i] == '\n') {
                riga[col] = '\0';
                col = 0;
                if (!ex_area_aggiungi(g_ed_cod, riga)) goto pieno;
                continue;
            }
            if (col + 1 < sizeof(riga)) riga[col++] = buf[i];
        }
    riga[col] = '\0';
    if (col) ex_area_aggiungi(g_ed_cod, riga);
pieno:
    close(fd);
    ex_area_pulita(g_ed_cod);
    g_ed_file = quale;
    ed_indice();
    return 1;
}

static int ed_salva(void)
{
    char p[PERC_MAX];
    int  fd;
    unsigned int i, n;

    if (!ex_area_modificato(g_ed_cod)) return 1;

    percorso(p, g_ed_nomi[g_ed_file]);
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { ex_testo_metti(g_ed_stato, "non riesco a scrivere"); return 0; }

    n = ex_area_righe(g_ed_cod);
    for (i = 0; i < n; i++) {
        const char  *r = ex_area_riga(g_ed_cod, i);
        unsigned int l = (unsigned int)strlen(r);

        if (l) write(fd, r, l);
        write(fd, "\n", 1);
    }
    close(fd);
    ex_area_pulita(g_ed_cod);
    ex_testo_metti(g_ed_stato, "salvato");
    return 1;
}

/* ! GENERALIZZATA SU QUALE AREA E QUALE RIGA DI STATO, dal momento che il
 * file-editor (piu' avanti, aperto da «Files») ha bisogno della stessa
 * ricerca sulla sua propria area. La ricerca da capo e la riga trovata non
 * cambiano da un'area all'altra; scriverla due volte avrebbe voluto dire due
 * copie da tenere d'accordo per lo stesso identico ciclo. */
static void area_cerca(ExFinestra area, ExFinestra stato)
{
    static char cosa[64] = "";
    unsigned int i, n, r0 = 0, c0 = 0;

    if (!ex_dlg_riga("Cerca", "testo da cercare:", cosa, sizeof(cosa))) return;
    if (cosa[0] == '\0') return;

    ex_area_cursore(area, &r0, &c0);
    n = ex_area_righe(area);

    for (i = 1; i <= n; i++) {
        unsigned int k = (r0 + i) % n;
        const char *r = ex_area_riga(area, k);
        const char *t = strstr(r, cosa);

        if (t) {
            ex_area_vai(area, k, (unsigned int)(t - r));
            ex_testo_metti(stato, "trovato");
            return;
        }
    }
    ex_testo_metti(stato, "non trovato");
}

static void ed_cerca(void) { area_cerca(g_ed_cod, g_ed_stato); }

/* =============================================================================
 * ! CERCA UNA VOLTA SOLA, SOSTITUISCE UNA VOLTA SOLA — la stessa scelta,
 * fatta due volte. Un «sostituisci tutto» che gira da solo su un intero file
 * e' comodo finche' funziona e un disastro silenzioso il giorno che «cerca»
 * trova qualcosa che non ci si aspettava: e in un sorgente C, dove lo stesso
 * pezzo di testo puo' comparire dentro una stringa, un commento e un nome di
 * variabile, capita piu' spesso di quanto sembri. Una sostituzione alla
 * volta, con il risultato visibile subito, e' la stessa prudenza di «Cerca»
 * applicata a chi scrive invece che a chi legge.
 *
 * ! DUE DOMANDE, NON UN DIALOGO SOLO: ExDlg ha `ex_dlg_riga`, che chiede una
 * parola per volta, e non un modulo con due caselle — e non vale la pena
 * scriverne uno apposta per due righe. Chi vuole annullare puo' farlo dopo
 * la prima domanda quanto dopo la seconda.
 * ============================================================================= */
static void area_sostituisci(ExFinestra area, ExFinestra stato)
{
    static char cerca[64]  = "";
    static char cambia[64] = "";
    unsigned int i, n, r0 = 0, c0 = 0;

    if (!ex_dlg_riga("Sostituisci", "cerca:", cerca, sizeof(cerca))) return;
    if (cerca[0] == '\0') return;
    if (!ex_dlg_riga("Sostituisci", "sostituisci con:", cambia, sizeof(cambia)))
        return;

    ex_area_cursore(area, &r0, &c0);
    n = ex_area_righe(area);

    for (i = 1; i <= n; i++) {
        unsigned int k = (r0 + i) % n;
        const char *r = ex_area_riga(area, k);
        const char *t = strstr(r, cerca);

        if (t) {
            /* ! IL PEZZO DOPO SI LEGGE DA `r` PRIMA DI SCRIVERE, non dopo:
             * ex_area_riga_metti sostituisce il contenuto della riga, e `r`
             * punta DENTRO quel contenuto. Costruire prima l'intera riga
             * nuova in un buffer nostro — e solo poi scriverla — e' cio' che
             * evita di leggere da un posto che si e' appena riscritto. */
            char         nuova[512];
            unsigned int prima = (unsigned int)(t - r);
            unsigned int dopo  = prima + (unsigned int)strlen(cerca);

            if (prima >= sizeof(nuova)) prima = sizeof(nuova) - 1;
            memcpy(nuova, r, prima);
            nuova[prima] = '\0';
            strncat(nuova, cambia, sizeof(nuova) - strlen(nuova) - 1);
            strncat(nuova, r + dopo, sizeof(nuova) - strlen(nuova) - 1);

            ex_area_riga_metti(area, k, nuova);
            ex_area_vai(area, k, prima + (unsigned int)strlen(cambia));
            ex_testo_metti(stato, "sostituito");
            return;
        }
    }
    ex_testo_metti(stato, "non trovato");
}

static long proc_ed(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_COMANDO:
        switch (wp) {
        case ID_ED_SALVA:  ed_salva(); break;
        case ID_ED_CHIUDI:
            ed_salva();
            ex_distruggi(f);
            g_ed = 0;
            ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
            return 0;
        case ID_COPIA:     ex_area_copia(g_ed_cod);    break;
        case ID_INCOLLA:   ex_area_incolla(g_ed_cod);  break;
        case ID_TAGLIA:    ex_area_taglia(g_ed_cod);   break;
        case ID_CANCELLA:  ex_area_cancella(g_ed_cod); break;
        case ID_CERCA:     ed_cerca();                 break;
        case ID_SOSTIT:    area_sostituisci(g_ed_cod, g_ed_stato); break;
        case ID_ED_TAB: {
            /* ! SI SALVA PRIMA DI CAMBIARE LINGUETTA: l'area del codice e' una
             * sola, e il testo di prima sparisce nel momento in cui si carica
             * l'altro file. */
            int nuovo = (int)lp;

            ed_salva();
            if (nuovo >= 0 && nuovo < 3) ed_carica(nuovo);
            break;
        }
        case ID_ED_FUNZ: {
            unsigned int k = ex_lista_scelta(g_ed_funz);

            if (k < g_ed_n) {
                ex_area_vai(g_ed_cod, g_ed_riga[k], 0);
                ex_fuoco(g_ed_cod);
            }
            break;
        }
        default: break;
        }
        ed_indice();
        return 0;

    case EXM_CHIUDI:
        ed_salva();
        ex_distruggi(f);
        g_ed = 0;
        ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
        return 0;

    default: break;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

static void editor_apri(int quale, int riga)
{
    ExFinestra menu;
    int i;

    if (g_prog_dir[0] == '\0') { dico("prima apri o crea un progetto"); return; }

    if (g_ed) {                         /* c'e' gia': si porta li' e basta */
        if (quale != g_ed_file) { ed_salva(); ed_carica(quale); }
        if (riga >= 0) ex_area_vai(g_ed_cod, (unsigned int)riga, 0);
        ex_procedura_base(g_ed, EXM_DISEGNA, 0, 0);
        return;
    }

    g_ed = ex_crea("finestra", "Sorgente",
                   EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_MODALE,
                   30, 20, 740, 520, 0, 0, proc_ed);
    if (g_ed == 0) { dico("non riesco ad aprire l'editor"); return; }

    menu = ex_menu(g_ed);
    ex_menu_voce(menu, "File", "Salva\tCtrl+S", ID_ED_SALVA);
    ex_menu_voce(menu, "File", "-", 0);
    ex_menu_voce(menu, "File", "Chiudi", ID_ED_CHIUDI);
    ex_menu_voce(menu, "Modifica", "Copia\tCtrl+C",   ID_COPIA);
    ex_menu_voce(menu, "Modifica", "Incolla\tCtrl+V", ID_INCOLLA);
    ex_menu_voce(menu, "Modifica", "Taglia\tCtrl+X",  ID_TAGLIA);
    ex_menu_voce(menu, "Modifica", "Cancella",        ID_CANCELLA);
    ex_menu_voce(menu, "Modifica", "-", 0);
    ex_menu_voce(menu, "Modifica", "Cerca\tCtrl+F",   ID_CERCA);
    ex_menu_voce(menu, "Modifica", "Sostituisci",     ID_SOSTIT);

    g_ed_tab = ex_crea("tab", "", EX_FIGLIO, 0, 22, 740, 24, g_ed, ID_ED_TAB, 0);
    for (i = 0; i < 3; i++) ex_voce_aggiungi(g_ed_tab, g_ed_nomi[i]);

    g_ed_funz = ex_crea("lista", "", EX_FIGLIO, 4, 50, 200, 420,
                        g_ed, ID_ED_FUNZ, 0);
    g_ed_cod  = ex_crea("areacodice", "", EX_FIGLIO, 208, 50, 526, 420,
                        g_ed, ID_ED_CODICE, 0);
    g_ed_stato = ex_crea("etichetta", "", EX_FIGLIO, 6, 476, 720, 16,
                         g_ed, 0, 0);

    ex_area_colora(g_ed_cod, ex_colora_c, 0);

    /* ! UN EDITOR VUOTO NON DICE NIENTE A NESSUNO. Se il file non c'e' — e non
     * c'e' quando il progetto non si e' potuto scrivere — si chiude e si dice
     * perche', invece di mostrare una finestra bianca che sembra un difetto
     * dell'editor. */
    if (!ed_carica(quale)) {
        char avviso[PERC_MAX + 80];

        sprintf(avviso, "Non riesco a leggere %s/src/%s.\n\n"
                        "Il progetto e' stato creato davvero? Da CD non si "
                        "puo' scrivere: serve un disco montato in lettura e "
                        "scrittura.", g_prog_dir, g_ed_nomi[quale]);
        ex_distruggi(g_ed);
        g_ed = 0;
        ex_dlg_avviso("Il sorgente non c'e'", avviso);
        dico("il sorgente non si legge");
        return;
    }
    ex_voce_scegli(g_ed_tab, (unsigned int)quale);
    if (riga >= 0) ex_area_vai(g_ed_cod, (unsigned int)riga, 0);
    ex_fuoco(g_ed_cod);

    ex_procedura_base(g_ed, EXM_DISEGNA, 0, 0);
}

/* =============================================================================
 * IL PROGETTO — aprire, creare, salvare
 * ============================================================================= */
static void progetto_titolo(void)
{
    char t[PERC_MAX + 32];

    if (g_prog_dir[0]) sprintf(t, "EX-IDE - %s", g_prog_dir);
    else               strcpy(t, "EX-IDE - nessun progetto");
    ex_titolo(g_f, t);
}

static int progetto_salva(void)
{
    if (g_prog_dir[0] == '\0') { dico("non c'e' nessun progetto aperto"); return 0; }

    if (!dis_salva()) return 0;
    if (!gen_h())     { dico("non riesco a scrivere finestra.h"); return 0; }
    if (!gen_c())     { dico("non riesco a scrivere finestra_gen.c"); return 0; }
    if (!mio_c_assicura()) { dico("non riesco a creare finestra.c"); return 0; }

    /* ! OGNI CONTROLLO CON UN EVENTO HA IL SUO HANDLER DOPO OGNI SALVATAGGIO,
     * e non solo quello su cui si e' fatto doppio clic. finestra.h dichiara
     * l'handler di TUTTI e finestra_gen.c li chiama: uno che manca non e' un
     * dettaglio da sistemare dopo, e' un progetto che non si collega — e
     * l'errore arriva dal collegatore, che dice «undefined reference» e non
     * «fai doppio clic sulla casella».
     *
     * Il doppio clic resta il modo di ARRIVARCI, non il modo di crearlo. */
    {
        int i;

        for (i = 0; i < CTRL_MAX; i++)
            if (g_ctrl[i].usato && ha_evento(&g_ctrl[i]))
                handler_assicura(&g_ctrl[i]);
    }

    g_sporco = 0;
    dico("salvato: finestra.dis, finestra.h, finestra_gen.c");
    return 1;
}

static void progetto_nuovo(void)
{
    char dir[PERC_MAX] = "/progetti/prova";
    const char *n;

    if (g_sporco && !ex_dlg_conferma("Modifiche non salvate",
                                     "Il disegno e' cambiato. Cominciare "
                                     "un progetto nuovo?",
                                     "Comincia", "Annulla")) return;

    if (!ex_dlg_riga("Progetto nuovo", "directory del progetto:",
                     dir, sizeof(dir))) return;
    if (dir[0] == '\0') return;

    if (!progetto_crea(dir)) { dico("non riesco a creare la directory"); return; }

    strncpy(g_prog_dir, dir, PERC_MAX - 1);
    g_prog_dir[PERC_MAX - 1] = '\0';
    n = strrchr(g_prog_dir, '/');
    strncpy(g_prog_nome, n ? n + 1 : g_prog_dir, NOME_MAX - 1);
    g_prog_nome[NOME_MAX - 1] = '\0';

    memset(g_ctrl, 0, sizeof(g_ctrl));
    memset(g_form, 0, sizeof(g_form));
    form_azzera(&g_form[0], "principale", g_prog_nome);
    g_form_sel = 0;
    g_sel = -1;
    storia_azzera();

    progetto_salva();
    progetto_titolo();
    form_mostra();
    prop_mostra();
    dico("progetto creato: src, inc, lib, bin, obj");
}

static void progetto_apri(void)
{
    char p[PERC_MAX];
    char *taglio;

    strncpy(p, g_prog_dir[0] ? g_prog_dir : "/progetti", PERC_MAX - 1);
    p[PERC_MAX - 1] = '\0';

    /* ! SI SCEGLIE UN FILE, E SI TIENE LA SUA DIRECTORY. Il dialogo di ExDlg
     * sceglie file, non directory: si punta finestra.dis e si risale di due
     * livelli, che e' anche il modo di verificare che quella directory sia
     * davvero un progetto e non una qualunque. */
    if (!ex_dlg_apri(p, sizeof(p))) return;

    taglio = strrchr(p, '/');
    if (!taglio) { dico("percorso strano: serve .../src/finestra.dis"); return; }
    *taglio = '\0';                     /* via il nome del file */
    taglio = strrchr(p, '/');
    if (!taglio || strcmp(taglio + 1, "src") != 0) {
        dico("scegli il finestra.dis dentro src/");
        return;
    }
    *taglio = '\0';                     /* via anche «src» */

    strncpy(g_prog_dir, p, PERC_MAX - 1);
    g_prog_dir[PERC_MAX - 1] = '\0';

    if (!dis_carica()) { dico("finestra.dis non si legge"); return; }
    storia_azzera();            /* la storia non attraversa i progetti */

    g_sel = -1;
    g_sporco = 0;
    progetto_titolo();
    form_mostra();
    prop_mostra();
    dico("progetto aperto");
}

/* Definita vicino al file-editor, dove sia «Sorgente» che «Files» sono gia'
 * dichiarati: la usa progetto_salva_come(), qui sotto. */
static void editori_salva_tutti(void);

/* =============================================================================
 * SALVA CON NOME — copia l'INTERO progetto in una directory nuova
 *
 * ! NON E' «SALVA IL DISEGNO IN UN ALTRO POSTO», e la differenza conta. Un
 * progetto e' una directory intera — sorgenti scritti a mano compresi — e un
 * «Salva con nome» che rigenerasse solo finestra.dis/.h/_gen.c nella nuova
 * sede perderebbe silenziosamente finestra.c: esattamente il file che il
 * resto di exide si guarda bene dal toccare mai. Si copia tutto, si passa al
 * progetto copiato, e da li' in poi si lavora sulla copia — e' quello che
 * «Salva con nome» vuol dire in ogni altro programma.
 *
 * ! GLI EDITOR APERTI SI SVUOTANO PRIMA DI COPIARE (editori_salva_tutti()).
 * Se «Sorgente» o un file aperto da «Files» hanno modifiche non ancora
 * scritte su disco, copiare adesso porterebbe nella copia la versione
 * VECCHIA — e chi ha appena scritto qualcosa non se lo aspetta.
 *
 * ! UN LIVELLO SOLO DENTRO OGNI SOTTODIRECTORY, ed e' un limite dichiarato:
 * src, inc, lib, bin, obj sono pensate piatte da progetto_crea() in poi, e un
 * progetto che ci mette sottocartelle dentro va oltre quel che questa
 * funzione sa seguire — la stessa scelta di «Files», per la stessa ragione.
 * ============================================================================= */
/* ! UN PERCORSO TAGLIATO NON SI USA, e non e' pignoleria: un percorso tagliato
 * non e' «quasi giusto», e' il nome di un file DIVERSO — che poi si apre con
 * O_TRUNC. Qui i pezzi non sono piu' i nomi corti decisi da noi come nel resto
 * del programma: `PERC_MAX` e' 320 e un nome trovato da listdir_from puo'
 * essere lungo 256 (DIRENT_NAME_MAX), quindi la somma puo' non starci. snprintf
 * rende quanto SAREBBE servito (vedi u_car in lib/libc.c), ed e' l'unico modo
 * che ha il chiamante di accorgersene. */
static int perc_unisci(char *dst, unsigned int dim, const char *a,
                       const char *b, const char *c)
{
    int n = c ? snprintf(dst, dim, "%s/%s/%s", a, b, c)
              : snprintf(dst, dim, "%s/%s", a, b);

    return n > 0 && (unsigned int)n < dim;
}

static int copia_file(const char *da, const char *a)
{
    char buf[1024];
    int  fd_in, fd_out, n = 0;

    /* ! MAI UN FILE SU SE STESSO, ed e' la differenza fra una copia e una
     * distruzione. Il VFS tronca all'APERTURA — vfs.c, il ramo O_TRUNC chiama
     * ext2_truncate(...,0) — e non guarda chi altro tiene quel file aperto:
     * con `da` uguale ad `a` il file e' gia' vuoto quando la lettura comincia,
     * la read rende 0, il ciclo non gira nemmeno una volta e questa funzione
     * direbbe RIUSCITO di un file che ha appena azzerato. Chi chiama lo
     * controlla piu' in grande; qui c'e' lo stesso, perche' costa una riga e
     * perche' e' il posto in cui il danno accadrebbe. */
    if (strcmp(da, a) == 0) return 0;

    fd_in = open(da, O_RDONLY, 0);
    if (fd_in < 0) return 0;
    fd_out = open(a, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_out < 0) { close(fd_in); return 0; }

    while ((n = (int)read(fd_in, buf, sizeof(buf))) > 0)
        if (write(fd_out, buf, (unsigned int)n) != n) { n = -1; break; }

    close(fd_in);
    close(fd_out);
    return n >= 0;
}

/* Rende QUANTI FILE NON SONO ARRIVATI: zero vuol dire che la sottodirectory
 * e' copiata tutta. Prima non rendeva niente, e un errore di scrittura non
 * aveva nessun modo di farsi vedere. */
static int copia_sottodir(const char *dest, const char *nome)
{
    char     srcdir[PERC_MAX], da[PERC_MAX], a[PERC_MAX];
    DirEntry v[8];
    int      start = 0, n, i, guasti = 0;

    if (!perc_unisci(srcdir, sizeof(srcdir), g_prog_dir, nome, 0)) return 1;

    while ((n = listdir_from(srcdir, v, 8, start)) > 0) {
        for (i = 0; i < n; i++) {
            if (v[i].is_dir) continue;      /* un livello solo, vedi sopra */
            if (v[i].name[0] == '.' && v[i].name[1] == '\0') continue;

            if (!perc_unisci(da, sizeof(da), g_prog_dir, nome, v[i].name) ||
                !perc_unisci(a,  sizeof(a),  dest,       nome, v[i].name)) {
                guasti++;
                continue;
            }
            if (!copia_file(da, a)) guasti++;
        }
        start += n;
        if (n < 8) break;
    }
    return guasti;
}

static void progetto_salva_come(const char *nuova)
{
    static const char *const sotto[] = { "src", "inc", "lib", "bin", "obj", 0 };
    char         dest[PERC_MAX], p[PERC_MAX], q[PERC_MAX];
    char         avviso[2 * PERC_MAX + 220];
    unsigned int l;
    int          i, fd, guasti = 0;

    if (g_prog_dir[0] == '\0') { dico("prima apri o crea un progetto"); return; }

    /* ! LA BARRA FINALE NON CAMBIA LA DIRECTORY MA CAMBIA strcmp, e qui sotto
     * e' proprio uno strcmp a decidere se la destinazione e' quella di adesso:
     * «/disk/prg6/» e «/disk/prg6» sono lo stesso posto. Si toglie prima. */
    strncpy(dest, nuova, PERC_MAX - 1);
    dest[PERC_MAX - 1] = '\0';
    l = (unsigned int)strlen(dest);
    while (l > 1 && dest[l - 1] == '/') dest[--l] = '\0';
    if (dest[0] == '\0') { dico("serve un percorso"); return; }

    /* ! COPIARE UN PROGETTO SU SE STESSO LO CANCELLA — vedi copia_file qui
     * sopra. Il campo del dialogo nasce riempito con «<dir>-copia», ma resta
     * un campo di testo: chi ci riscrive dentro il percorso di adesso, senza
     * questa riga, si ritroverebbe ogni file a zero byte e la riga di stato
     * che dice «progetto copiato». */
    if (strcmp(dest, g_prog_dir) == 0) {
        dico("e' la directory di adesso: per una copia serve un nome diverso");
        return;
    }

    /* ! UN PROGETTO CHE C'E' GIA' NON SI SOVRASCRIVE IN SILENZIO, ed e' la
     * stessa prudenza con cui progetto_crea() si rifiuta di riscrivere una
     * scheda gia' esistente. Si guarda con una lettura, che non tocca niente.
     * Bastano i due segni che fanno un progetto: il disegno e la scheda. */
    if ((perc_unisci(p, sizeof(p), dest, "src", "finestra.dis") &&
         (fd = open(p, O_RDONLY, 0)) >= 0) ||
        (perc_unisci(p, sizeof(p), dest, "progetto.txt", 0) &&
         (fd = open(p, O_RDONLY, 0)) >= 0)) {
        close(fd);
        snprintf(avviso, sizeof(avviso),
                 "In %s c'e' gia' un progetto.\n\n"
                 "«Salva con nome» non lo sovrascrive: i sorgenti scritti a "
                 "mano che ci sono dentro andrebbero persi, e non c'e' modo "
                 "di riaverli. Scegli una directory che non esiste ancora, "
                 "oppure sposta altrove quella di prima.", dest);
        ex_dlg_avviso("C'e' gia' un progetto", avviso);
        dico("non copiato: li' dentro c'e' gia' un progetto");
        return;
    }

    editori_salva_tutti();
    if (!progetto_salva()) return;      /* il disegno in memoria, sul vecchio progetto */

    mkdir(dest, 0755);
    for (i = 0; sotto[i]; i++) {
        if (!perc_unisci(p, sizeof(p), dest, sotto[i], 0)) { guasti++; continue; }
        mkdir(p, 0755);
    }

    /* ! LA PROVA CHE CONTA E' SCRIVERE, non chiedere — la stessa di
     * progetto_crea(), e per la stessa ragione: mkdir non si lamenta quando il
     * disco e' in sola lettura, e senza questa prova la copia «riuscirebbe»
     * lasciando l'IDE puntato su una directory che non esiste. Il file di
     * prova si toglie subito: se restasse, un secondo tentativo sulla stessa
     * destinazione la troverebbe e la scambierebbe per un progetto gia' li'. */
    if (!perc_unisci(p, sizeof(p), dest, "src", "prova.tmp") ||
        (fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
        snprintf(avviso, sizeof(avviso),
                 "Non riesco a scrivere in %s.\n\n"
                 "Il progetto NON e' stato copiato: si continua a lavorare "
                 "in %s, che non e' stato toccato.", dest, g_prog_dir);
        ex_dlg_avviso("Non si puo' scrivere", avviso);
        dico("non copiato: non si puo' scrivere li'");
        return;
    }
    close(fd);
    unlink(p);

    for (i = 0; sotto[i]; i++) guasti += copia_sottodir(dest, sotto[i]);

    if (perc_unisci(p, sizeof(p), g_prog_dir, "progetto.txt", 0) &&
        perc_unisci(q, sizeof(q), dest,       "progetto.txt", 0))
        guasti += !copia_file(p, q);
    else
        guasti++;

    /* compila.sh puo' non esserci ancora — lo scrive la finestra del
     * compilatore la prima volta. La sua assenza non e' un guasto; la sua
     * presenza sfortunata si'. */
    if (perc_unisci(p, sizeof(p), g_prog_dir, "compila.sh", 0) &&
        perc_unisci(q, sizeof(q), dest,       "compila.sh", 0)) {
        fd = open(p, O_RDONLY, 0);
        if (fd >= 0) { close(fd); guasti += !copia_file(p, q); }
    }

    /* ! SE QUALCOSA NON E' ARRIVATO, NON CI SI SPOSTA. Mezza copia piu' un IDE
     * che punta alla mezza copia vuol dire che il prossimo Salva scrive li'
     * dentro, e da quel momento la mezza copia E' il progetto. Restando dove
     * si era, quello buono resta buono e la copia incompleta e' li' da
     * guardare — che e' l'unica cosa utile da fare, dopo. */
    if (guasti) {
        snprintf(avviso, sizeof(avviso),
                 "%d file non sono arrivati in %s.\n\n"
                 "Si continua a lavorare in %s, che non e' stato toccato. La "
                 "copia incompleta e' rimasta dov'e': guardala prima di "
                 "riprovare.", guasti, dest, g_prog_dir);
        ex_dlg_avviso("Copia incompleta", avviso);
        dico("copia incompleta: si resta nel progetto di prima");
        return;
    }

    strncpy(g_prog_dir, dest, PERC_MAX - 1);
    g_prog_dir[PERC_MAX - 1] = '\0';
    {
        const char *n = strrchr(g_prog_dir, '/');

        strncpy(g_prog_nome, n ? n + 1 : g_prog_dir, NOME_MAX - 1);
        g_prog_nome[NOME_MAX - 1] = '\0';
    }

    g_sporco = 0;
    progetto_titolo();
    dico("progetto copiato: ora si lavora nella nuova directory");
}

/* =============================================================================
 * IL MANUALE — la spiegazione sta nel programma, non in un file accanto
 *
 * ! UN MANUALE CHE STA IN UN FILE E' UN MANUALE CHE UN GIORNO NON C'E'. Da CD,
 * da disco, da un'installazione a componenti: il file puo' mancare, e chi apre
 * «Manuale» si trova una finestra vuota. Queste righe pesano tre kilobyte e ci
 * sono sempre.
 * ============================================================================= */
static const char *const g_manuale[] = {
"EX-IDE - l'ambiente di sviluppo visuale di EX-OS",
"",
"! QUESTO E' IL PROMEMORIA, non il manuale. Quello vero e' la PAGINA:",
"  Aiuto > Manuale la apre nel navigatore, con l'indice in cima, un",
"  rimando per ogni strumento e gli esempi scritti per intero. Sta in",
"  /exwin/doc/exide.html - se stai leggendo QUESTO, quel file non",
"  c'era: manca il componente /exwin, oppure il CD non e' montato.",
"",
"COME SI FA UN PROGRAMMA",
"",
"1. File > Nuovo progetto, e si da' una directory: exide ci crea",
"   dentro src, inc, lib, bin e obj, piu' progetto.txt.",
"2. Si sceglie uno strumento a sinistra e si clicca sulla maschera.",
"3. Lo si sposta trascinandolo e lo si ridimensiona tirando una delle",
"   otto maniglie. Le proprieta' a destra: si sceglie la riga, si",
"   scrive nella casella, si preme Applica.",
"4. Doppio clic sul controllo: si apre il sorgente dentro la funzione",
"   che l'evento chiamera'. Se non c'era, exide la aggiunge vuota.",
"5. File > Salva scrive finestra.dis, finestra.h e finestra_gen.c.",
"6. Strumenti > Compilatore, e il pulsante Compila.",
"",
"Ctrl+Z annulla, fino a sedici passi indietro.",
"",
"I QUATTRO FILE, E QUALE E' TUO",
"",
"  finestra.dis     il disegno: lo legge e lo scrive solo exide",
"  finestra.h       gli id, i puntatori e i prototipi - generato",
"  finestra_gen.c   crea i controlli e smista gli eventi - generato",
"  finestra.c       IL TUO: exide ci aggiunge in fondo gli handler che",
"                   mancano e non riscrive mai quel che c'e' gia'",
"",
"PIU' DI UNA FINESTRA",
"",
"  L'elenco sopra la maschera le cambia; Nuova e Togli. La principale",
"  la apre il programma; le altre le apri tu chiamando la loro",
"  <nome>_crea() dal codice. Chiuderne una non fa uscire il programma.",
"",
"GLI STRUMENTI, E I LORO EVENTI",
"",
"  Pulsante      Clic, SulMouse       ex_testo_metti/prendi",
"  Etichetta     SulMouse, Clic       ex_testo_metti",
"  Casella       Cambiato, Invio      ex_testo_prendi (63 caratteri)",
"  Spunta        Cambiato             ex_acceso / ex_accendi",
"  Radio         Cambiato             come la Spunta; gruppi = padri",
"  Riquadro      -                    raggruppa, e fa da padre",
"  Separatore    -",
"  Intestazione  -",
"  Lista         Scelta, Apertura     ex_lista_*",
"  Area testo    Cambiato             ex_area_*  (due per programma)",
"  Area codice   Cambiato             ex_area_*, ex_area_colora",
"  Elenco        Scelta               ex_voce_*",
"  Linguette     Scelta               ex_voce_*, come l'Elenco",
"  Scorrimento   Scorso               ex_scorri_*  (la forma decide se",
"                                     e' verticale o orizzontale)",
"",
"! CAMBIARE UN DATO NON RIDISEGNA: dopo aver cambiato un controllo si",
"  chiude con ex_procedura_base(g_form, EXM_DISEGNA, 0, 0).",
"",
"Gli esempi - due caselle che si scambiano il testo, un pulsante che",
"chiude, l'uscita con la conferma, aprire una seconda finestra - sono",
"nella pagina, scritti per intero con il perche' di ogni riga.",
0
};

/* ! IL MANUALE BUONO E' LA PAGINA, e questa funzione prova prima quella. In
 * `/exwin/doc/exide.html` c'e' lo stesso testo con l'indice in cima e un
 * rimando per ogni strumento e ogni esempio: un manuale lungo senza indice si
 * legge una volta sola, e la seconda si cerca la riga scorrendo. Il navigatore
 * sa saltare alle ancore dal 3 settembre 2026, quindi adesso l'indice porta
 * davvero dove dice.
 *
 * ! E NON SI RISCRIVE UN VISUALIZZATORE, per la stessa ragione per cui
 * «Directory» non riscrive un file manager: il navigatore c'e', impagina,
 * colora, segue i link e lo fa gia' per le altre nove pagine della guida.
 *
 * Rende 1 se il navigatore e' partito. */
static int manuale_pagina(void)
{
    static const char *const bin[] = {
        "/exwin/bin/browser",
        "/cdrom/exwin/bin/browser"
    };
    static const char *const doc[] = {
        "file:///exwin/doc/exide.html",
        "file:///cdrom/exwin/doc/exide.html"
    };
    int b, d, fd;

    for (d = 0; d < 2; d++) {
        /* Il percorso vero, senza «file://», per guardare se il file c'e':
         * lanciare il navigatore su una pagina che non esiste vorrebbe dire
         * una finestra aperta apposta per dire che non ha trovato niente. */
        fd = open(doc[d] + 7, O_RDONLY, 0);
        if (fd < 0) continue;
        close(fd);

        for (b = 0; b < 2; b++) {
            char *av[3];

            av[0] = (char *)bin[b];
            av[1] = (char *)doc[d];
            av[2] = 0;
            if (spawn_ex(av[0], av, environ, 0, 0) >= 0) {
                dico("manuale aperto nel navigatore");
                return 1;
            }
        }
    }
    return 0;
}

/* ! E QUELLO DENTRO IL PROGRAMMA RESTA, ed e' la ragione per cui era stato
 * scritto: un manuale che sta in un file e' un manuale che un giorno non c'e'
 * — il componente /exwin non installato, un CD montato a meta', una copia del
 * solo binario. Queste righe pesano tre kilobyte e ci sono sempre. Adesso sono
 * la seconda scelta invece che l'unica. */
static void manuale(void)
{
    static ExFinestra fm = 0, am = 0;
    int i;

    if (fm) { ex_procedura_base(fm, EXM_DISEGNA, 0, 0); return; }

    if (manuale_pagina()) return;

    fm = ex_crea("finestra", "Manuale di EX-IDE",
                 EX_TITOLO | EX_BORDO | EX_CHIUDI, 60, 40, 620, 460, 0, 0, 0);
    if (fm == 0) { dico("non riesco ad aprire il manuale"); return; }

    am = ex_crea("areatesto", "", EX_FIGLIO, 4, 4, 612, 452, fm, 0, 0);
    if (am == 0) { ex_distruggi(fm); fm = 0; dico("non c'e' posto per il manuale"); return; }

    for (i = 0; g_manuale[i]; i++)
        if (!ex_area_aggiungi(am, g_manuale[i])) break;

    ex_procedura_base(fm, EXM_DISEGNA, 0, 0);
}

static void informazioni(void)
{
    char t[700];

    exinfo_testo(t, sizeof(t), "EX-IDE", VERSIONE_APP,
                 "L'ambiente di sviluppo visuale di EX-OS: si disegna una "
                 "finestra con gli strumenti di ExWin e ne esce del C da "
                 "compilare. Il disegno sta in finestra.dis, il codice "
                 "generato in finestra.h e finestra_gen.c, e il tuo in "
                 "finestra.c - che exide non riscrive mai.");
    ex_dlg_avviso("Informazioni su", t);
}

/* =============================================================================
 * LA FINESTRA DEL COMPILATORE
 *
 * ! LA RIGA DI COMPILAZIONE FINISCE IN UN FILE, E IL FILE E' IL PRODOTTO.
 * `compila.sh` sta nella directory del progetto, si legge, si corregge a mano e
 * si lancia dalla shell come qualunque altro script: il pulsante «Compila» non
 * fa niente di diverso da quel che si potrebbe fare digitando. Un ambiente che
 * compila con una riga che nessuno puo' vedere e' un ambiente in cui, il giorno
 * che qualcosa non torna, non si ha nessun appiglio.
 *
 * ! E LA RADICE DEGLI STRUMENTI E' UN CAMPO SOLO. Da li' si ricavano il
 * compilatore, gli header, lo start.S, i ponti della libc e il linker script:
 * sono cinque percorsi che stanno o tutti sul CD (/cdrom/exos) o tutti sul
 * disco dopo `toolinst` (/exos). Sei campi da tenere d'accordo sarebbero sei
 * modi di sbagliarne uno.
 *
 * ! IL COMPILATORE VA CHIAMATO CON IL SUO PERCORSO DENTRO L'ALBERO, e non dal
 * PATH: GCC si calcola dove stanno le proprie cose da DOVE STA LUI. Lanciato
 * come /cdrom/bin/gcc cerca gli header in /cdrom/lib/gcc e non trova niente;
 * lanciato come /cdrom/exos/bin/gcc torna tutto al suo posto. Sta scritto nel
 * leggimi del CD degli strumenti, ed e' la ragione per cui qui si compone il
 * percorso invece di scrivere «gcc».
 * ============================================================================= */
static ExFinestra g_cc, g_cc_f_radice, g_cc_f_opz, g_cc_f_nome, g_cc_uscita,
                  g_cc_stato;

/* Il nome del binario: se non l'ha scelto nessuno, e' quello del progetto. */
static const char *binario(void)
{
    if (g_cc_nome[0]) return g_cc_nome;
    return g_prog_nome[0] ? g_prog_nome : "programma";
}

/* Compone la riga di compilazione. Rende quanti caratteri ha scritto. */
static unsigned int riga_compila(char *out, unsigned int max)
{
    char r[PERC_MAX];
    int  i;

    strncpy(r, g_cc_radice, sizeof(r) - 1);
    r[sizeof(r) - 1] = '\0';

    out[0] = '\0';
    snprintf(out, max,
             "%s/bin/gcc " CC_OBBLIGATORIE " %s -I %s/include -I inc"
             " -T %s/programma.ld %s/start.S"
             " src/finestra.c src/finestra_gen.c",
             r, g_cc_opzioni, r, r, r);

    for (i = 0; i < g_lib_n; i++) {
        if (!g_lib[i].scelta || g_lib[i].stub[0] == '\0') continue;
        strncat(out, " ", max - strlen(out) - 1);
        /* Uno stub dell'albero degli strumenti si scrive relativo alla radice;
         * una libreria aggiunta a mano e' gia' un percorso intero. */
        if (g_lib[i].stub[0] != '/') {
            strncat(out, r, max - strlen(out) - 1);
            strncat(out, "/", max - strlen(out) - 1);
        }
        strncat(out, g_lib[i].stub, max - strlen(out) - 1);
    }

    strncat(out, " ", max - strlen(out) - 1);
    strncat(out, r, max - strlen(out) - 1);
    strncat(out, "/libc_ponti_asm.o ", max - strlen(out) - 1);
    strncat(out, r, max - strlen(out) - 1);
    strncat(out, "/libc_ponti_c.o ", max - strlen(out) - 1);
    strncat(out, r, max - strlen(out) - 1);
    strncat(out, "/libc_ponti_avvio.o ", max - strlen(out) - 1);
    strncat(out, r, max - strlen(out) - 1);
    strncat(out, "/libc_ponti_exlib.o -o bin/", max - strlen(out) - 1);
    strncat(out, binario(), max - strlen(out) - 1);

    return (unsigned int)strlen(out);
}

/* Scrive compila.sh nella directory del progetto. */
static int scrivi_script(void)
{
    static char riga[2048];
    char p[PERC_MAX], testa[512];
    int  fd;

    if (g_prog_dir[0] == '\0') { dico("prima apri o crea un progetto"); return 0; }

    riga_compila(riga, sizeof(riga));

    sprintf(p, "%s/compila.sh", g_prog_dir);
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { dico("non riesco a scrivere compila.sh"); return 0; }

    sprintf(testa,
            "# GENERATO DA EXIDE - si riscrive a ogni «Scrivi compila.sh».\n"
            "#\n"
            "# Si puo' lanciare anche a mano, dalla directory del progetto:\n"
            "#     cd %s\n"
            "#     sh compila.sh\n"
            "#\n"
            "# Il compilatore va chiamato con il suo percorso DENTRO l'albero\n"
            "# degli strumenti: GCC calcola dove stanno le proprie cose da dove\n"
            "# sta lui, e da /bin non troverebbe ne' cc1 ne' gli header.\n\n",
            g_prog_dir);
    write(fd, testa, strlen(testa));
    write(fd, riga, strlen(riga));
    write(fd, "\n", 1);
    close(fd);
    return 1;
}

/* Rilegge il registro dell'ultima compilazione dentro la lista. */
static void carica_uscita(void)
{
    char p[PERC_MAX], buf[512], riga[200];
    int  fd, n, i;
    unsigned int col = 0, righe = 0;

    ex_lista_svuota(g_cc_uscita);
    sprintf(p, "%s/obj/compila.log", g_prog_dir);

    fd = open(p, O_RDONLY, 0);
    if (fd < 0) { ex_lista_aggiungi(g_cc_uscita, "(nessun registro)"); return; }

    while ((n = (int)read(fd, buf, sizeof(buf))) > 0)
        for (i = 0; i < n; i++) {
            if (buf[i] == '\r') continue;
            if (buf[i] != '\n') {
                if (col + 1 < sizeof(riga)) riga[col++] = buf[i];
                continue;
            }
            riga[col] = '\0';
            col = 0;
            if (!ex_lista_aggiungi(g_cc_uscita, riga)) goto fine;
            righe++;
        }
    riga[col] = '\0';
    if (col) ex_lista_aggiungi(g_cc_uscita, riga);
fine:
    close(fd);
    if (righe == 0 && col == 0)
        ex_lista_aggiungi(g_cc_uscita, "(il compilatore non ha detto niente:"
                                       " e' andata bene)");
}

/* =============================================================================
 * ! SI ASPETTA SENZA MORIRE, e non con un waitpid che blocca. Una compilazione
 * dentro EX-OS non e' istantanea — cc1 e' un programma da quaranta megabyte —
 * e una finestra ferma per un minuto sembra un programma piantato. Si guarda se
 * il figlio e' finito con WNOHANG, si smista un pugno di messaggi, si dorme un
 * istante: e' la stessa forma dell'attesa di rete del navigatore.
 * ============================================================================= */
static int aspetta_vivo(int pid)
{
    unsigned int inizio = uptime_ms();
    int stato = 0;

    for (;;) {
        ExMsg m;
        int   n = 0;
        int   r = waitpid(pid, &stato, WNOHANG);

        if (r == pid) return stato;
        if (r < 0)    return -1;

        while (n++ < 8 && ex_msg_ora(&m)) ex_smista(&m);
        usleep(20000);

        /* Un tetto c'e', e generoso: cinque minuti. Oltre, si smette di
         * aspettare e si dice — meglio di una finestra che aspetta per
         * sempre un compilatore che si e' impiccato. */
        if (uptime_ms() - inizio > 300000u) return -2;
    }
}

static void compila(void)
{
    char        p[PERC_MAX], log[PERC_MAX], msg[120];
    char        prima[PERC_MAX];
    char       *argv[3];
    SpawnRedir  red[2];
    int         pid, stato;

    if (g_prog_dir[0] == '\0') { dico("prima apri o crea un progetto"); return; }
    if (!progetto_salva()) return;
    if (!scrivi_script()) return;

    sprintf(p, "%s/compila.sh", g_prog_dir);
    sprintf(log, "%s/obj/compila.log", g_prog_dir);

    /* ! L'USCITA VA IN UN FILE, non in una pipe: una pipe vorrebbe dire
     * leggerla mentre il figlio scrive, e chi legge una pipe piena mentre il
     * figlio ne riempie un'altra si blocca. Un file lo si rilegge dopo, tutto
     * insieme, e resta li' anche dopo — che e' quel che serve a chi vuole
     * rileggere l'errore con calma. */
    red[0].fd = 1; red[0].flags = O_WRONLY | O_CREAT | O_TRUNC;
    red[0].percorso = log; red[0].fd_padre = -1;
    red[1].fd = 2; red[1].flags = O_WRONLY | O_CREAT;
    red[1].percorso = log; red[1].fd_padre = -1;

    argv[0] = "sh";
    argv[1] = p;
    argv[2] = 0;

    ex_testo_metti(g_cc_stato, "compilo...");
    ex_procedura_base(g_cc, EXM_DISEGNA, 0, 0);

    /* ! IL FIGLIO EREDITA LA NOSTRA DIRECTORY DI LAVORO, e spawn_ex non ne
     * prende una per parametro: se non ci si sposta prima, `sh` — e con lui
     * gcc — restano nella directory da cui e' partito exide, che e' la radice
     * del CD in sola lettura. compila.sh scrive percorsi RELATIVI apposta
     * (src/finestra.c, -o bin/...) perche' e' pensato per girare "da dentro"
     * il progetto, come se lo si lanciasse a mano dopo un `cd`; e gcc, oltre
     * agli oggetti, scrive anche dei file temporanei nella directory corrente
     * — che e' il punto in cui il difetto si vedeva: "Cannot create temporary
     * file in ./: filesystem in sola lettura", con il progetto giusto scelto
     * e il disco giusto montato, perche' il cwd non era ne' l'uno ne' l'altro. */
    prima[0] = '\0';
    getcwd(prima, sizeof(prima));
    if (chdir(g_prog_dir) != 0) {
        ex_testo_metti(g_cc_stato, "non riesco a entrare nella directory del progetto");
        return;
    }

    pid = spawn_ex("/bin/sh", argv, environ, red, 2);

    /* Si torna subito da dove si era, riuscito o no il lancio: exide non deve
     * restare "dentro" un progetto per un dettaglio di implementazione — il
     * resto del programma (dialoghi, l'editor) non se lo aspetta. */
    if (prima[0]) chdir(prima);

    if (pid < 0) {
        ex_testo_metti(g_cc_stato, "non riesco ad avviare /bin/sh");
        return;
    }

    stato = aspetta_vivo(pid);
    carica_uscita();

    if (stato == -2) strcpy(msg, "il compilatore non e' tornato in cinque minuti");
    else if (stato == 0) sprintf(msg, "fatto: bin/%s", binario());
    else sprintf(msg, "il compilatore si e' fermato (esito %d): guarda qui sotto",
                 stato);

    ex_testo_metti(g_cc_stato, msg);
    dico(msg);
    ex_procedura_base(g_cc, EXM_DISEGNA, 0, 0);
}

static long proc_cc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    static char riga[2048];

    switch (msg) {
    case EXM_COMANDO:
        switch (wp) {
        case ID_CC_RADICE:
            strncpy(g_cc_radice, ex_testo_prendi(g_cc_f_radice),
                    sizeof(g_cc_radice) - 1);
            g_cc_radice[sizeof(g_cc_radice) - 1] = '\0';
            break;
        case ID_CC_OPZ:
            strncpy(g_cc_opzioni, ex_testo_prendi(g_cc_f_opz),
                    sizeof(g_cc_opzioni) - 1);
            g_cc_opzioni[sizeof(g_cc_opzioni) - 1] = '\0';
            break;
        case ID_CC_NOME:
            strncpy(g_cc_nome, ex_testo_prendi(g_cc_f_nome),
                    sizeof(g_cc_nome) - 1);
            g_cc_nome[sizeof(g_cc_nome) - 1] = '\0';
            break;
        case ID_CC_SCRIVI:
            /* I campi si rileggono sempre: chi ha scritto e non ha premuto
             * Invio si aspetta lo stesso che valga quel che vede. */
            strncpy(g_cc_radice, ex_testo_prendi(g_cc_f_radice), sizeof(g_cc_radice) - 1);
            strncpy(g_cc_opzioni, ex_testo_prendi(g_cc_f_opz), sizeof(g_cc_opzioni) - 1);
            strncpy(g_cc_nome, ex_testo_prendi(g_cc_f_nome), sizeof(g_cc_nome) - 1);
            if (scrivi_script()) {
                riga_compila(riga, sizeof(riga));
                ex_lista_svuota(g_cc_uscita);
                ex_lista_aggiungi(g_cc_uscita, "compila.sh scritto:");
                ex_lista_aggiungi(g_cc_uscita, riga);
                ex_testo_metti(g_cc_stato, "compila.sh scritto nel progetto");
            }
            break;
        case ID_CC_COMPILA:
            strncpy(g_cc_radice, ex_testo_prendi(g_cc_f_radice), sizeof(g_cc_radice) - 1);
            strncpy(g_cc_opzioni, ex_testo_prendi(g_cc_f_opz), sizeof(g_cc_opzioni) - 1);
            strncpy(g_cc_nome, ex_testo_prendi(g_cc_f_nome), sizeof(g_cc_nome) - 1);
            compila();
            break;
        case ID_CC_CHIUDI:
            ex_distruggi(f);
            g_cc = 0;
            ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
            return 0;
        default: break;
        }
        return 0;

    case EXM_CHIUDI:
        ex_distruggi(f);
        g_cc = 0;
        ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
        return 0;

    default: break;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

static void compilatore_apri(void)
{
    if (g_prog_dir[0] == '\0') { dico("prima apri o crea un progetto"); return; }
    if (g_cc) { ex_procedura_base(g_cc, EXM_DISEGNA, 0, 0); return; }

    g_cc = ex_crea("finestra", "Compilatore",
                   EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_MODALE,
                   40, 40, 700, 440, 0, 0, proc_cc);
    if (g_cc == 0) { dico("non riesco ad aprire il compilatore"); return; }

    ex_crea("etichetta", "radice degli strumenti:", EX_FIGLIO,
            10, 12, 190, 16, g_cc, 0, 0);
    g_cc_f_radice = ex_crea("testo", g_cc_radice, EX_FIGLIO,
                            206, 8, 470, 22, g_cc, ID_CC_RADICE, 0);

    ex_crea("etichetta", "opzioni:", EX_FIGLIO, 10, 42, 190, 16, g_cc, 0, 0);
    g_cc_f_opz = ex_crea("testo", g_cc_opzioni, EX_FIGLIO,
                         206, 38, 470, 22, g_cc, ID_CC_OPZ, 0);

    ex_crea("etichetta", "nome del binario (in bin/):", EX_FIGLIO,
            10, 72, 190, 16, g_cc, 0, 0);
    g_cc_f_nome = ex_crea("testo", binario(), EX_FIGLIO,
                          206, 68, 200, 22, g_cc, ID_CC_NOME, 0);

    ex_crea("pulsante", "Scrivi compila.sh", EX_FIGLIO,
            10, 102, 150, 26, g_cc, ID_CC_SCRIVI, 0);
    ex_crea("pulsante", "Compila", EX_FIGLIO,
            168, 102, 100, 26, g_cc, ID_CC_COMPILA, 0);
    ex_crea("pulsante", "Chiudi", EX_FIGLIO,
            276, 102, 100, 26, g_cc, ID_CC_CHIUDI, 0);

    ex_crea("intestazione", "Uscita del compilatore", EX_FIGLIO,
            10, 136, 666, 20, g_cc, 0, 0);
    g_cc_uscita = ex_crea("lista", "", EX_FIGLIO, 10, 158, 666, 220,
                          g_cc, ID_CC_USCITA, 0);
    g_cc_stato = ex_crea("etichetta", "", EX_FIGLIO, 10, 386, 666, 16,
                         g_cc, 0, 0);

    carica_uscita();
    ex_procedura_base(g_cc, EXM_DISEGNA, 0, 0);
}

/* =============================================================================
 * LA FINESTRA DELLE LIBRERIE
 *
 * ! LA SPUNTA STA DENTRO LA RIGA, e non e' un ripiego: una lista di controlli
 * «spunta» uno sotto l'altro sarebbe una lista che non scorre — i controlli
 * stanno dove sono stati messi, e dodici righe piu' l'aggiunta a mano non ci
 * starebbero in una finestra ragionevole. Cliccare la riga la accende: e' quel
 * che fa chiunque, e la parentesi quadra dice com'e' messa.
 * ============================================================================= */
static ExFinestra g_libf, g_lib_lista;

static void lib_mostra(void)
{
    char riga[LIB_NOME_MAX + 8];
    int  i;

    ex_lista_svuota(g_lib_lista);
    for (i = 0; i < g_lib_n; i++) {
        sprintf(riga, "[%c] %s", g_lib[i].scelta ? 'x' : ' ', g_lib[i].nome);
        ex_lista_aggiungi(g_lib_lista, riga);
    }
}

static void lib_aggiungi(void)
{
    char perc[PERC_MAX] = "";

    if (g_lib_n >= LIB_MAX) { dico("non c'e' posto per un'altra libreria"); return; }
    if (!ex_dlg_riga("Libreria tua", "percorso dello stub (.c):",
                     perc, sizeof(perc))) return;
    if (perc[0] == '\0') return;

    strncpy(g_lib[g_lib_n].stub, perc, PERC_MAX - 1);
    g_lib[g_lib_n].stub[PERC_MAX - 1] = '\0';
    strncpy(g_lib[g_lib_n].nome, perc, LIB_NOME_MAX - 1);
    g_lib[g_lib_n].nome[LIB_NOME_MAX - 1] = '\0';
    g_lib[g_lib_n].base = 0;
    g_lib[g_lib_n].scelta = 1;
    g_lib_n++;
    lib_mostra();
}

static long proc_lib(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_COMANDO:
        if (wp == ID_LIB_ELENCO) {
            unsigned int k = ex_lista_scelta(g_lib_lista);

            if ((int)k < g_lib_n) {
                if (g_lib[k].base) {
                    dico("exwin serve sempre: non si toglie");
                } else {
                    g_lib[k].scelta = !g_lib[k].scelta;
                    lib_mostra();
                    ex_lista_scegli(g_lib_lista, k);
                }
            }
        } else if (wp == ID_LIB_AGG) {
            lib_aggiungi();
        } else if (wp == ID_LIB_CHIUDI) {
            ex_distruggi(f);
            g_libf = 0;
            ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
            return 0;
        }
        return 0;

    case EXM_CHIUDI:
        ex_distruggi(f);
        g_libf = 0;
        ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
        return 0;

    default: break;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

static void librerie_apri(void)
{
    if (g_libf) { ex_procedura_base(g_libf, EXM_DISEGNA, 0, 0); return; }

    g_libf = ex_crea("finestra", "Librerie",
                     EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_MODALE,
                     120, 80, 420, 320, 0, 0, proc_lib);
    if (g_libf == 0) { dico("non riesco ad aprire le librerie"); return; }

    ex_crea("etichetta", "clicca una riga per accenderla o spegnerla",
            EX_FIGLIO, 10, 10, 400, 16, g_libf, 0, 0);
    g_lib_lista = ex_crea("lista", "", EX_FIGLIO, 10, 32, 396, 210,
                          g_libf, ID_LIB_ELENCO, 0);
    ex_crea("pulsante", "Aggiungi...", EX_FIGLIO, 10, 250, 120, 26,
            g_libf, ID_LIB_AGG, 0);
    ex_crea("pulsante", "Chiudi", EX_FIGLIO, 138, 250, 100, 26,
            g_libf, ID_LIB_CHIUDI, 0);

    lib_mostra();
    ex_procedura_base(g_libf, EXM_DISEGNA, 0, 0);
}

/* =============================================================================
 * LA SCHEDA DEL PROGETTO
 *
 * ! IL FILE E' LO STESSO CHE progetto_crea() HA GIA' SCRITTO. Questa finestra
 * non inventa un secondo formato: rilegge progetto.txt, lascia modificare
 * quel che ha senso modificare, e lo riscrive. Chi apre il file con un editor
 * qualunque — o con «Sorgente», che non lo sa leggere ma lo mostrerebbe come
 * testo — vede la stessa cosa che vede questa finestra.
 *
 * ! IL NOME E LA DATA DI CREAZIONE NON SI EDITANO QUI, e non e' una
 * dimenticanza: il nome viene dalla directory del progetto — riscriverlo in
 * questo campo non rinominerebbe niente, e un campo che non fa quel che
 * promette e' peggio di un campo assente. La data di creazione, per
 * definizione, non e' qualcosa che si possa correggere senza che smetta di
 * essere vera. Si mostrano come etichette, non come caselle.
 *
 * ! LA NOTA E' L'UNICO CAMPO SU PIU' RIGHE, ed e' per questo che vive fuori
 * dalle «chiave = valore» del resto del file: dopo la riga «[nota]» tutto cio'
 * che segue, fino alla fine del file, e' la nota — comprese le righe vuote.
 * Le altre chiavi si scambiano sulla stessa riga apposta per restare un
 * formato a una riga per campo; darle anche loro il permesso di andare a capo
 * vorrebbe dire non sapere piu' dove finisce un valore e comincia il prossimo.
 * ============================================================================= */
#define PRG_CAMPO_MAX 64   /* = TESTO_LEN di exwin.c: quanto una casella regge
                            * DAVVERO. Scriverne una piu' grande qui non
                            * aiuterebbe: la casella la taglierebbe lo stesso —
                            * vedi il difetto delle opzioni del compilatore. */

static ExFinestra g_prgf, g_prg_e_nome, g_prg_e_creato,
                  g_prg_c_autore, g_prg_c_vers, g_prg_c_descr, g_prg_c_nota,
                  g_prg_stato;

static char g_prg_nome[PRG_CAMPO_MAX]    = "";
static char g_prg_creato[PRG_CAMPO_MAX]  = "";

static void progetto_percorso(char *out)
{
    sprintf(out, "%s/progetto.txt", g_prog_dir);
}

/* Il valore dopo il primo '=' di "chiave = valore", con gli spazi tolti da
 * tutt'e due i lati. Rende 0 se sulla riga non c'e' nessun '='. */
static int prg_valore(const char *riga, const char *chiave, char *out,
                      unsigned int max)
{
    unsigned int kl = (unsigned int)strlen(chiave);
    const char  *eq;
    const char  *v;
    unsigned int n;

    if (strncmp(riga, chiave, kl) != 0) return 0;
    eq = riga + kl;
    while (*eq == ' ' || *eq == '\t') eq++;
    if (*eq != '=') return 0;

    v = eq + 1;
    while (*v == ' ' || *v == '\t') v++;

    n = (unsigned int)strlen(v);
    while (n > 0 && (v[n - 1] == ' ' || v[n - 1] == '\t')) n--;
    if (n >= max) n = max - 1;

    memcpy(out, v, n);
    out[n] = '\0';
    return 1;
}

static void progetto_leggi(void)
{
    char p[PERC_MAX], buf[512], riga[300];
    char autore[PRG_CAMPO_MAX] = "", vers[PRG_CAMPO_MAX] = "0.001";
    char descr[PRG_CAMPO_MAX]  = "";
    int  fd, n, i, nota = 0;
    unsigned int col = 0;

    /* Il nome viene SEMPRE dalla directory, mai dal file: e' cosi' che lo
     * mostra il titolo della finestra principale (progetto_nuovo/apri/
     * salva_come derivano g_prog_dir allo stesso modo), e non c'e' un
     * secondo posto dove possa restare vecchio. Se leggessimo "nome" dal
     * file, una copia fatta con "Salva con nome" mostrerebbe qui il nome
     * DEL PROGETTO ORIGINALE — la directory e' prg6-copia ma la scheda
     * direbbe ancora prg6, perche' progetto.txt e' stato copiato cosi'
     * com'era. Derivarlo qui lo tiene sempre allineato, e il prossimo Salva
     * lo riscrive nel file da solo. */
    {
        const char *dn = strrchr(g_prog_dir, '/');
        strncpy(g_prg_nome, dn ? dn + 1 : g_prog_dir, PRG_CAMPO_MAX - 1);
        g_prg_nome[PRG_CAMPO_MAX - 1] = '\0';
    }
    g_prg_creato[0] = '\0';
    ex_area_svuota(g_prg_c_nota);

    progetto_percorso(p);
    fd = open(p, O_RDONLY, 0);
    if (fd < 0) {
        /* Nessun file: si propone com'era in progetto_crea(), ma non si
         * scrive — solo Salva scrive. */
        RtcTime ora;

        strncpy(autore, EXINFO_AUTORE, PRG_CAMPO_MAX - 1);
        memset(&ora, 0, sizeof(ora));
        time_now(&ora);
        sprintf(g_prg_creato, "%04u-%02u-%02u", (unsigned int)ora.anno,
                (unsigned int)ora.mese, (unsigned int)ora.giorno);
    } else {
        while ((n = (int)read(fd, buf, sizeof(buf))) > 0)
            for (i = 0; i < n; i++) {
                if (buf[i] == '\r') continue;
                if (buf[i] != '\n') {
                    if (col + 1 < sizeof(riga)) riga[col++] = buf[i];
                    continue;
                }
                riga[col] = '\0';
                col = 0;

                if (nota) {
                    ex_area_aggiungi(g_prg_c_nota, riga);
                    continue;
                }
                if (strcmp(riga, "[nota]") == 0) { nota = 1; continue; }

                if (prg_valore(riga, "autore", autore, sizeof(autore))) continue;
                if (prg_valore(riga, "versione", vers, sizeof(vers))) continue;
                if (prg_valore(riga, "creato", g_prg_creato, sizeof(g_prg_creato))) continue;
                if (prg_valore(riga, "descrizione", descr, sizeof(descr))) continue;
            }
        if (col && nota) { riga[col] = '\0'; ex_area_aggiungi(g_prg_c_nota, riga); }
        close(fd);
    }

    ex_testo_metti(g_prg_e_nome, g_prg_nome);
    ex_testo_metti(g_prg_e_creato, g_prg_creato[0] ? g_prg_creato : "(non ancora salvata)");
    ex_testo_metti(g_prg_c_autore, autore);
    ex_testo_metti(g_prg_c_vers, vers);
    ex_testo_metti(g_prg_c_descr, descr);
    ex_area_pulita(g_prg_c_nota);
}

static void progetto_scheda_salva(void)
{
    char p[PERC_MAX], riga[PRG_CAMPO_MAX + 32];
    int  fd, i, n;

    progetto_percorso(p);
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { ex_testo_metti(g_prg_stato, "non riesco a scrivere progetto.txt"); return; }

    sprintf(riga, "nome = %s\n", g_prg_nome);
    write(fd, riga, strlen(riga));
    sprintf(riga, "autore = %s\n", ex_testo_prendi(g_prg_c_autore));
    write(fd, riga, strlen(riga));
    sprintf(riga, "versione = %s\n", ex_testo_prendi(g_prg_c_vers));
    write(fd, riga, strlen(riga));
    sprintf(riga, "creato = %s\n", g_prg_creato);
    write(fd, riga, strlen(riga));
    sprintf(riga, "descrizione = %s\n", ex_testo_prendi(g_prg_c_descr));
    write(fd, riga, strlen(riga));

    write(fd, "\n[nota]\n", 8);
    n = (int)ex_area_righe(g_prg_c_nota);
    for (i = 0; i < n; i++) {
        const char  *r = ex_area_riga(g_prg_c_nota, (unsigned int)i);
        unsigned int l = (unsigned int)strlen(r);

        if (l) write(fd, r, l);
        write(fd, "\n", 1);
    }
    close(fd);

    ex_testo_metti(g_prg_stato, "salvato: progetto.txt");
    dico("scheda del progetto salvata");
}

static long proc_prg(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_COMANDO:
        if (wp == ID_PR_SALVA) {
            progetto_scheda_salva();
        } else if (wp == ID_PR_CHIUDI) {
            progetto_scheda_salva();
            ex_distruggi(f);
            g_prgf = 0;
            ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
            return 0;
        }
        return 0;

    case EXM_CHIUDI:
        /* ! LA X CHIUDE COME «CHIUDI», salvando: e' una scheda di dati, non
         * codice — non c'e' niente da poter rompere restando aggiornati,
         * e chiedere conferma per un'informazione a basso rischio sarebbe
         * una domanda che si impara a schiacciare senza leggerla. */
        progetto_scheda_salva();
        ex_distruggi(f);
        g_prgf = 0;
        ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
        return 0;

    default: break;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

static void progetto_scheda_apri(void)
{
    if (g_prog_dir[0] == '\0') { dico("prima apri o crea un progetto"); return; }
    if (g_prgf) { ex_procedura_base(g_prgf, EXM_DISEGNA, 0, 0); return; }

    g_prgf = ex_crea("finestra", "Progetto",
                     EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_MODALE,
                     100, 60, 520, 420, 0, 0, proc_prg);
    if (g_prgf == 0) { dico("non riesco ad aprire la scheda del progetto"); return; }

    ex_crea("etichetta", "nome:", EX_FIGLIO, 10, 12, 90, 16, g_prgf, 0, 0);
    g_prg_e_nome = ex_crea("etichetta", "", EX_FIGLIO, 110, 12, 380, 16,
                           g_prgf, 0, 0);

    ex_crea("etichetta", "autore:", EX_FIGLIO, 10, 38, 90, 16, g_prgf, 0, 0);
    g_prg_c_autore = ex_crea("testo", "", EX_FIGLIO, 110, 34, 380, 22,
                             g_prgf, ID_PR_AUTORE, 0);

    ex_crea("etichetta", "versione:", EX_FIGLIO, 10, 68, 90, 16, g_prgf, 0, 0);
    g_prg_c_vers = ex_crea("testo", "", EX_FIGLIO, 110, 64, 120, 22,
                           g_prgf, ID_PR_VERS, 0);

    ex_crea("etichetta", "creato:", EX_FIGLIO, 250, 68, 60, 16, g_prgf, 0, 0);
    g_prg_e_creato = ex_crea("etichetta", "", EX_FIGLIO, 310, 68, 180, 16,
                             g_prgf, 0, 0);

    ex_crea("etichetta", "descrizione:", EX_FIGLIO, 10, 98, 90, 16, g_prgf, 0, 0);
    g_prg_c_descr = ex_crea("testo", "", EX_FIGLIO, 110, 94, 380, 22,
                            g_prgf, ID_PR_DESCR, 0);

    ex_crea("intestazione", "Nota", EX_FIGLIO, 10, 128, 480, 20, g_prgf, 0, 0);
    g_prg_c_nota = ex_crea("areatesto", "", EX_FIGLIO, 10, 150, 480, 190,
                           g_prgf, ID_PR_NOTA, 0);

    ex_crea("pulsante", "Salva", EX_FIGLIO, 10, 350, 90, 26,
            g_prgf, ID_PR_SALVA, 0);
    ex_crea("pulsante", "Chiudi", EX_FIGLIO, 108, 350, 90, 26,
            g_prgf, ID_PR_CHIUDI, 0);
    g_prg_stato = ex_crea("etichetta", "", EX_FIGLIO, 10, 384, 480, 16,
                          g_prgf, 0, 0);

    progetto_leggi();
    ex_fuoco(g_prg_c_autore);
    ex_procedura_base(g_prgf, EXM_DISEGNA, 0, 0);
}

/* =============================================================================
 * IL FILE-EDITOR — UN SORGENTE QUALUNQUE, non i tre del disegno
 *
 * ! NON E' LA STESSA FINESTRA DI «Sorgente», ed e' una scelta e non una
 * scorciatoia. La finestra di «Sorgente» conosce esattamente tre nomi —
 * finestra.c, finestra_gen.c, finestra.h — e mezzo programma (gli handler, il
 * salvataggio generato) e' scritto sapendo che sono quelli. Infilarci un
 * quarto nome qualunque vorrebbe dire portare quella certezza dappertutto:
 * ogni punto che oggi scrive `g_ed_nomi[quale]` dovrebbe imparare a gestire
 * anche «non e' uno dei tre». Una finestra a parte, piu' piccola e piu'
 * semplice, costa meno e non rischia di rompere quella che gia' funziona.
 *
 * ! IL COLORITORE SI DECIDE DAL NOME, e si spegne per chi non lo vuole. La
 * stessa area «areacodice» resta in vita da un file all'altro — riaprirla
 * ogni volta vorrebbe dire perdere il posto nel toolkit che gia' non si
 * libera (vedi il perche' in exwin.c) — e se non si azzera il coloritore un
 * file .txt aperto dopo un .c si vede colorato come se fosse C.
 * ============================================================================= */
static ExFinestra g_fe_f, g_fe_cod, g_fe_stato;
static char       g_fe_nome[64] = "";     /* relativo a <progetto>/src */

static void file_percorso(char *out, const char *nome)
{
    sprintf(out, "%s/src/%s", g_prog_dir, nome);
}

static int fe_estensione_c(const char *nome)
{
    unsigned int n = (unsigned int)strlen(nome);

    return (n > 2 && strcmp(nome + n - 2, ".c") == 0) ||
           (n > 2 && strcmp(nome + n - 2, ".h") == 0);
}

static int fe_carica(const char *nome)
{
    char p[PERC_MAX], buf[512], riga[300];
    int  fd, n, i;
    unsigned int col = 0;

    file_percorso(p, nome);
    ex_area_svuota(g_fe_cod);
    ex_area_colora(g_fe_cod, fe_estensione_c(nome) ? ex_colora_c : 0, 0);

    fd = open(p, O_RDONLY, 0);
    if (fd < 0) { ex_testo_metti(g_fe_stato, "il file non c'e'"); return 0; }

    while ((n = (int)read(fd, buf, sizeof(buf))) > 0)
        for (i = 0; i < n; i++) {
            if (buf[i] == '\r') continue;
            if (buf[i] != '\n') {
                if (col + 1 < sizeof(riga)) riga[col++] = buf[i];
                continue;
            }
            riga[col] = '\0';
            col = 0;
            if (!ex_area_aggiungi(g_fe_cod, riga)) goto pieno;
        }
    riga[col] = '\0';
    if (col) ex_area_aggiungi(g_fe_cod, riga);
pieno:
    close(fd);
    ex_area_pulita(g_fe_cod);

    strncpy(g_fe_nome, nome, sizeof(g_fe_nome) - 1);
    g_fe_nome[sizeof(g_fe_nome) - 1] = '\0';
    ex_titolo(g_fe_f, nome);
    return 1;
}

static int fe_salva(void)
{
    char p[PERC_MAX];
    int  fd;
    unsigned int i, n;

    if (!ex_area_modificato(g_fe_cod)) return 1;

    file_percorso(p, g_fe_nome);
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { ex_testo_metti(g_fe_stato, "non riesco a scrivere"); return 0; }

    n = ex_area_righe(g_fe_cod);
    for (i = 0; i < n; i++) {
        const char  *r = ex_area_riga(g_fe_cod, i);
        unsigned int l = (unsigned int)strlen(r);

        if (l) write(fd, r, l);
        write(fd, "\n", 1);
    }
    close(fd);
    ex_area_pulita(g_fe_cod);
    ex_testo_metti(g_fe_stato, "salvato");
    return 1;
}

/* ! SVUOTA VERSO IL DISCO OGNI EDITOR RIMASTO APERTO, e sta qui — non prima,
 * vicino a «Salva con nome» che la chiama — perche' e' qui che «Sorgente» E
 * il file-editor sono gia' tutt'e due dichiarati. Serve a chi copia l'intero
 * progetto: copiare mentre una finestra tiene ancora in memoria una riga non
 * scritta vorrebbe dire portare nella copia la versione VECCHIA. */
static void editori_salva_tutti(void)
{
    if (g_ed)   ed_salva();
    if (g_fe_f) fe_salva();
}

static long proc_fe(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_COMANDO:
        switch (wp) {
        case ID_FE_SALVA:  fe_salva(); break;
        case ID_FE_CERCA:  area_cerca(g_fe_cod, g_fe_stato); break;
        case ID_FE_SOSTIT: area_sostituisci(g_fe_cod, g_fe_stato); break;
        case ID_FE_CHIUDI:
            fe_salva();
            ex_distruggi(f);
            g_fe_f = 0;
            ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
            return 0;
        default: break;
        }
        return 0;

    case EXM_CHIUDI:
        fe_salva();
        ex_distruggi(f);
        g_fe_f = 0;
        ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
        return 0;

    default: break;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

/* Apre `nome` (un file dentro <progetto>/src) nel file-editor. Se la finestra
 * e' gia' aperta su un ALTRO file lo salva prima di cambiarlo — la stessa
 * regola delle linguette del sorgente principale. */
static void file_editor_apri(const char *nome)
{
    if (g_fe_f) {
        if (strcmp(g_fe_nome, nome) != 0) fe_salva();
        fe_carica(nome);
        ex_fuoco(g_fe_cod);
        ex_procedura_base(g_fe_f, EXM_DISEGNA, 0, 0);
        return;
    }

    g_fe_f = ex_crea("finestra", nome,
                     EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_MODALE,
                     60, 40, 640, 460, 0, 0, proc_fe);
    if (g_fe_f == 0) { dico("non riesco ad aprire il file"); return; }

    g_fe_cod = ex_crea("areacodice", "", EX_FIGLIO, 4, 4, 632, 400,
                       g_fe_f, 0, 0);
    ex_crea("pulsante", "Salva", EX_FIGLIO, 4, 408, 90, 26, g_fe_f, ID_FE_SALVA, 0);
    ex_crea("pulsante", "Cerca", EX_FIGLIO, 102, 408, 90, 26, g_fe_f, ID_FE_CERCA, 0);
    ex_crea("pulsante", "Sostituisci", EX_FIGLIO, 200, 408, 110, 26,
            g_fe_f, ID_FE_SOSTIT, 0);
    ex_crea("pulsante", "Chiudi", EX_FIGLIO, 318, 408, 90, 26, g_fe_f, ID_FE_CHIUDI, 0);
    g_fe_stato = ex_crea("etichetta", "", EX_FIGLIO, 4, 440, 630, 16, g_fe_f, 0, 0);

    fe_carica(nome);
    ex_fuoco(g_fe_cod);
    ex_procedura_base(g_fe_f, EXM_DISEGNA, 0, 0);
}

/* =============================================================================
 * LA FINESTRA FILES — i sorgenti del progetto, un doppio clic e si aprono
 *
 * ! SCOPE: SOLO <progetto>/src, non tutto l'albero. E' il posto dove stanno i
 * sorgenti veri, ed e' l'unica lettura di «files» compatibile con «un doppio
 * clic apre come sorgente» — un doppio clic su un file .o dentro obj/ non
 * aprirebbe niente di leggibile. Chi vuole vedere anche bin/ e obj/ ha
 * «Directory», qui accanto, che mostra tutto.
 * ============================================================================= */
#define FL_MAX      64
#define FL_NOME_MAX 64

static ExFinestra   g_flf, g_fl_lista;
static char         g_fl_nome[FL_MAX][FL_NOME_MAX];
static unsigned int g_fl_n;

static void files_mostra(void)
{
    char         srcdir[PERC_MAX], riga[80];
    DirEntry     v[8];
    int          start = 0, n, i;
    unsigned int dim[FL_MAX];

    sprintf(srcdir, "%s/src", g_prog_dir);
    ex_lista_svuota(g_fl_lista);
    g_fl_n = 0;

    while ((n = listdir_from(srcdir, v, 8, start)) > 0) {
        for (i = 0; i < n && g_fl_n < FL_MAX; i++) {
            if (v[i].is_dir) continue;      /* qui si aprono file, non cartelle */
            if (v[i].name[0] == '.' && v[i].name[1] == '\0') continue;
            strncpy(g_fl_nome[g_fl_n], v[i].name, FL_NOME_MAX - 1);
            g_fl_nome[g_fl_n][FL_NOME_MAX - 1] = '\0';
            dim[g_fl_n] = v[i].size;
            g_fl_n++;
        }
        start += n;
        if (n < 8) break;
    }

    for (i = 0; i < (int)g_fl_n; i++) {
        sprintf(riga, "%-40s %8u", g_fl_nome[i], dim[i]);
        ex_lista_aggiungi(g_fl_lista, riga);
    }
    if (g_fl_n == 0) ex_lista_aggiungi(g_fl_lista, "(src/ e' vuota)");
}

static long proc_fl(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_COMANDO:
        if (wp == ID_FL_ELENCO) {
            unsigned int k = ex_lista_scelta(g_fl_lista);

            /* ! SOLO IL DOPPIO CLIC O L'INVIO APRONO, come in ogni lista di
             * questo toolkit — EX_APRIRE(lp). Una riga scelta con le frecce
             * non deve gia' aprire un file: chi scorre l'elenco per leggere i
             * nomi non sta chiedendo di aprirli tutti uno per uno. */
            if (EX_APRIRE(lp) && k < g_fl_n) file_editor_apri(g_fl_nome[k]);
        } else if (wp == ID_FL_CHIUDI) {
            ex_distruggi(f);
            g_flf = 0;
            ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
            return 0;
        }
        return 0;

    case EXM_CHIUDI:
        ex_distruggi(f);
        g_flf = 0;
        ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
        return 0;

    default: break;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

static void files_apri(void)
{
    if (g_prog_dir[0] == '\0') { dico("prima apri o crea un progetto"); return; }
    if (g_flf) { files_mostra(); ex_procedura_base(g_flf, EXM_DISEGNA, 0, 0); return; }

    g_flf = ex_crea("finestra", "Files",
                    EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_MODALE,
                    140, 90, 420, 340, 0, 0, proc_fl);
    if (g_flf == 0) { dico("non riesco ad aprire la finestra dei file"); return; }

    ex_crea("etichetta", "doppio clic per aprire un sorgente:", EX_FIGLIO,
            10, 10, 400, 16, g_flf, 0, 0);
    g_fl_lista = ex_crea("lista", "", EX_FIGLIO, 10, 32, 396, 250,
                         g_flf, ID_FL_ELENCO, 0);
    ex_crea("pulsante", "Chiudi", EX_FIGLIO, 10, 292, 100, 26,
            g_flf, ID_FL_CHIUDI, 0);

    files_mostra();
    ex_procedura_base(g_flf, EXM_DISEGNA, 0, 0);
}

/* =============================================================================
 * LA VOCE DIRECTORY — tutto l'albero, comprese le cose che non sono sorgenti
 *
 * ! NON SI RISCRIVE UN SECONDO FILE MANAGER, SE C'E' GIA' QUELLO VERO. exide
 * sa disegnare rettangoli e liste, non copiare, spostare o cancellare file in
 * sicurezza — filemgr lo sa gia' fare, con la sua stessa struttura ad albero
 * piu' elenco che la voce Directory promette. Riscriverlo qui dentro sarebbe
 * la stessa copia che l'esistenza delle librerie condivise serve a togliere,
 * solo fatta di sorgente invece che di .so.
 *
 * ! SI PASSA LA DIRECTORY DEL PROGETTO COME ARGOMENTO, non si apre sulla
 * radice: filemgr accetta gia' un percorso di partenza (lo stesso argomento
 * che usa chi lo avvia dal menu Applicazioni), e chi ha chiesto «Directory»
 * voleva vedere il SUO progetto, non ripartire da /.
 * ============================================================================= */
static void directory_apri(void)
{
    static const char *const dove[] = {
        "/exwin/bin/filemgr",
        "/cdrom/exwin/bin/filemgr"
    };
    int k;

    if (g_prog_dir[0] == '\0') { dico("prima apri o crea un progetto"); return; }

    for (k = 0; k < 2; k++) {
        char *av[3];

        av[0] = (char *)dove[k];
        av[1] = g_prog_dir;
        av[2] = 0;
        if (spawn_ex(av[0], av, environ, 0, 0) >= 0) {
            dico("file manager aperto sulla directory del progetto");
            return;
        }
    }
    dico("non trovo filemgr (ne' in /exwin ne' in /cdrom/exwin)");
}

/* =============================================================================
 * LA SHELL NELLA DIRECTORY DEL PROGETTO
 *
 * ! LA DIRECTORY SI CAMBIA PRIMA DI CREARE IL CONTROLLO, e non si passa al
 * terminale: il controllo «terminale» avvia il programma che ha per titolo, e
 * quel programma nasce nella directory di chi lo crea. Un argomento in piu' nel
 * protocollo servirebbe a fare quel che un chdir fa gia'.
 * ============================================================================= */
static void shell_progetto(void)
{
    static ExFinestra ft = 0;
    ExFinestra t;

    if (g_prog_dir[0] == '\0') { dico("prima apri o crea un progetto"); return; }
    if (ft) { ex_procedura_base(ft, EXM_DISEGNA, 0, 0); return; }

    if (chdir(g_prog_dir) != 0) { dico("non riesco a entrare nel progetto"); return; }

    ft = ex_crea("finestra", "Shell del progetto",
                 EX_TITOLO | EX_BORDO | EX_CHIUDI, 80, 60, 648, 408, 0, 0, 0);
    if (ft == 0) { dico("non riesco ad aprire la shell"); return; }

    t = ex_crea("terminale", "/bin/sh", EX_FIGLIO, 2, 2, 644, 404, ft, 0, 0);
    if (t == 0) {
        ex_distruggi(ft);
        ft = 0;
        dico("la shell non parte");
        return;
    }
    ex_procedura_base(ft, EXM_DISEGNA, 0, 0);
    dico("shell aperta nella directory del progetto");
}

/* =============================================================================
 * LA PROCEDURA DELLA FINESTRA
 * ============================================================================= */
static int g_trascina = 0;
static int g_tras_dx, g_tras_dy;

static void tela_clic(int x, int y, int doppio)
{
    int ox = TELA_X + 8, oy = TELA_Y + 28;
    int k;

    if (x < TELA_X || x >= TELA_X + TELA_W ||
        y < TELA_Y || y >= TELA_Y + TELA_H) return;

    /* ! LE MANIGLIE SI GUARDANO PRIMA DEL CONTROLLO, e non e' un dettaglio
     * d'ordine: stanno a cavallo del bordo, quindi meta' di ognuna cade
     * DENTRO il controllo. Cercando prima il controllo, un clic sull'angolo
     * comincerebbe uno spostamento e la maniglia non si potrebbe prendere
     * mai — con il risultato che le maniglie si vedono e non servono a
     * niente, che e' come stavano fino a oggi. */
    if (!doppio) {
        int man = maniglia_in(x, y);

        if (man >= 0) {
            g_ridim = man;
            g_tras_segnato = 0;
            dico("tira per cambiare la misura");
            return;
        }
    }

    k = ctrl_in(x, y);

    if (k >= 0) {
        g_sel = k;
        /* ! IL DOPPIO CLIC APRE IL CODICE, e non aggiunge un controllo: chi fa
         * doppio clic su qualcosa che c'e' gia' sta chiedendo di quello, non
         * di uno nuovo. */
        if (doppio) {
            int riga;

            if (g_prog_dir[0] == '\0') { dico("prima apri o crea un progetto"); return; }
            if (!ha_evento(&g_ctrl[k])) {
                dico("questo controllo non ha eventi");
                prop_mostra();
                return;
            }
            if (!progetto_salva()) return;   /* il perche' l'ha gia' detto */
            riga = handler_assicura(&g_ctrl[k]);
            editor_apri(0, riga);
            return;
        }
        g_trascina = 1;
        g_tras_segnato = 0;
        g_tras_dx  = x - (ox + g_ctrl[k].x);
        g_tras_dy  = y - (oy + g_ctrl[k].y);
        prop_mostra();
        dico(g_ctrl[k].nome);
        return;
    }

    /* Sul vuoto: se c'e' uno strumento armato si mette li', altrimenti si
     * deseleziona — che e' come si arriva alle proprieta' della maschera. */
    if (g_strum_sel >= 0) {
        int n = aggiungi(g_strum_sel, x - ox, y - oy);

        if (n >= 0) {
            g_sel = n;
            dico(g_ctrl[n].nome);
        }
    } else {
        g_sel = -1;
        dico("maschera");
    }
    prop_mostra();
}

static void elimina(void)
{
    if (g_sel < 0 || !g_ctrl[g_sel].usato) { dico("non c'e' niente di scelto"); return; }

    istante_segna();

    /* ! SI TOGLIE DAL DISEGNO E NON DAL CODICE. L'handler in finestra.c resta
     * dov'e': e' codice scritto a mano, e cancellarlo perche' un rettangolo non
     * c'e' piu' sulla maschera sarebbe la cosa peggiore che questo programma
     * possa fare. Se non serve piu', lo si toglie leggendolo. */
    g_ctrl[g_sel].usato = 0;
    g_sel = -1;
    g_sporco = 1;
    prop_mostra();
    dico("controllo tolto dal disegno (l'handler in finestra.c resta)");
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_COMANDO:
        switch (wp) {
        case ID_STRUMENTI:
            g_strum_sel = (int)ex_lista_scelta(g_lst_strum);
            if (g_strum_sel >= 0 && g_strum_sel < STRUM_N) {
                char t[80];

                sprintf(t, "%s: clicca sulla maschera per metterlo",
                        g_strum[g_strum_sel].etichetta);
                dico(t);
            }
            break;

        case ID_PROPRIETA: {
            char v[64];
            int  k = (int)ex_lista_scelta(g_lst_prop);

            /* Senza controllo scelto l'elenco e' quello della maschera, e i
             * valori vanno letti da li': prop_valore risponderebbe vuoto. */
            if (g_sel < 0 || !g_ctrl[g_sel].usato) fprop_valore(k, v, sizeof(v));
            else                                   prop_valore(k, v, sizeof(v));
            ex_testo_metti(g_val, v);
            ex_fuoco(g_val);
            break;
        }

        case ID_VALORE:
        case ID_APPLICA:  prop_applica(); break;
        case ID_ELIMINA:  elimina();      break;

        /* L'elenco a discesa manda la riga scelta in lp, non l'indice della
         * maschera: fra le due c'e' di mezzo quale posto e' libero. */
        case ID_FORM:       form_scegli(form_da_riga((int)lp)); break;
        case ID_FORM_NUOVA: form_nuova(); break;
        case ID_FORM_TOGLI: form_togli(); break;

        case ID_NUOVO:      progetto_nuovo();  break;
        case ID_APRI:       progetto_apri();   break;
        case ID_SALVA:      progetto_salva();  break;
        case ID_SALVA_COME: {
            char dir[PERC_MAX];

            if (g_prog_dir[0] == '\0') { dico("prima apri o crea un progetto"); break; }
            strncpy(dir, g_prog_dir, PERC_MAX - 1);
            dir[PERC_MAX - 1] = '\0';
            strncat(dir, "-copia", PERC_MAX - strlen(dir) - 1);

            if (ex_dlg_riga("Salva con nome", "nuova directory del progetto:",
                            dir, sizeof(dir)) && dir[0])
                progetto_salva_come(dir);
            break;
        }
        case ID_CHIUDI:
            if (g_sporco && !ex_dlg_conferma("Modifiche non salvate",
                                             "Chiudere il progetto senza "
                                             "salvare?", "Chiudi", "Annulla"))
                break;
            g_prog_dir[0] = '\0';
            memset(g_ctrl, 0, sizeof(g_ctrl));
            g_sel = -1;
            g_sporco = 0;
            progetto_titolo();
            prop_mostra();
            dico("progetto chiuso");
            break;
        case ID_ESCI:
            if (g_sporco && !ex_dlg_conferma("Modifiche non salvate",
                                             "Uscire senza salvare?",
                                             "Esci", "Annulla")) break;
            ex_esci(0);
            break;

        case ID_SORGENTE:  editor_apri(0, -1);   break;
        case ID_SHELL:     shell_progetto();     break;
        case ID_COMPILA:   compilatore_apri();   break;
        case ID_LIBRERIE:  librerie_apri();      break;
        case ID_PROGETTO:  progetto_scheda_apri(); break;
        case ID_FILES:     files_apri();           break;
        case ID_DIRECTORY: directory_apri();       break;

        case ID_MANUALE:   manuale();       break;
        case ID_INFO:      informazioni();  break;

        case ID_ANNULLA:  annulla(); break;

        case ID_COPIA: case ID_INCOLLA: case ID_TAGLIA:
        case ID_CANCELLA: case ID_CERCA: case ID_SOSTIT:
            dico("le voci di Modifica lavorano dentro il sorgente");
            break;

        default: break;
        }
        return 0;

    case EXM_MOUSE_GIU:
        tela_clic(EX_X(lp), EX_Y(lp), 0);
        ex_fuoco_via(f);
        return 0;

    case EXM_DOPPIOCLIC:
        tela_clic(EX_X(lp), EX_Y(lp), 1);
        return 0;

    case EXM_MOUSE_MOSSO:
        if (g_ridim >= 0 && g_sel >= 0 && g_ctrl[g_sel].usato) {
            ridimensiona(EX_X(lp), EX_Y(lp));
            return 0;
        }
        if (g_trascina && g_sel >= 0 && g_ctrl[g_sel].usato) {
            int ox = TELA_X + 8, oy = TELA_Y + 28;
            int nx = arrotonda(EX_X(lp) - g_tras_dx - ox);
            int ny = arrotonda(EX_Y(lp) - g_tras_dy - oy);

            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            if (nx + g_ctrl[g_sel].w > forma()->w)
                nx = forma()->w - g_ctrl[g_sel].w;
            if (ny + g_ctrl[g_sel].h > forma()->h)
                ny = forma()->h - g_ctrl[g_sel].h;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;

            if (nx != g_ctrl[g_sel].x || ny != g_ctrl[g_sel].y) {
                if (!g_tras_segnato) { istante_segna(); g_tras_segnato = 1; }
                g_ctrl[g_sel].x = nx;
                g_ctrl[g_sel].y = ny;
                g_sporco = 1;
                prop_mostra();
                /* ! IL DISEGNO SI RIFA' QUI, e prima non lo faceva nessuno:
                 * EXM_DISEGNA non arriva da solo mentre si trascina, e il
                 * controllo si vedeva saltare nel posto nuovo solo quando
                 * qualcos'altro faceva ridisegnare la finestra. */
                disegna_tela();
                ex_aggiorna(g_f);
            }
        }
        return 0;

    case EXM_MOUSE_SU:
        g_trascina = 0;
        g_ridim = -1;
        return 0;

    case EXM_TASTO:
        if ((wp & KBD_KEY_MASK) == KBD_K_DEL) { elimina(); return 0; }

        /* ! LE SCORCIATOIE SCRITTE NEL MENU ADESSO FANNO QUALCOSA. Erano
         * etichette e basta — il menu prometteva Ctrl+S dal primo giorno e
         * premerlo non salvava — e per Annulla la scorciatoia conta piu' che
         * per gli altri: si annulla subito dopo aver sbagliato, con la mano
         * ancora sulla tastiera, non aprendo un menu.
         *
         * Ctrl+A arriva come 'a' | KBD_MOD_CTRL (vedi kbd_proto.h); si guarda
         * anche la maiuscola perche' con il Bloc Maiusc acceso arriva quella. */
        if (wp & KBD_MOD_CTRL) {
            switch (wp & KBD_KEY_MASK) {
            case 'z': case 'Z': annulla();        return 0;
            case 's': case 'S': progetto_salva(); return 0;
            case 'n': case 'N': progetto_nuovo(); return 0;
            case 'o': case 'O': progetto_apri();  return 0;
            case 'q': case 'Q': ex_esci(0);       return 0;
            default: break;
            }
        }
        return 0;

    case EXM_DISEGNA:
        ex_procedura_base(f, EXM_DISEGNA, 0, 0);
        disegna_tela();
        ex_aggiorna(f);
        return 0;

    case EXM_CHIUDI:
        if (g_sporco && !ex_dlg_conferma("Modifiche non salvate",
                                         "Uscire senza salvare?",
                                         "Esci", "Annulla")) return 0;
        ex_esci(0);
        return 0;

    default: break;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

int main(int argc, char **argv)
{
    ExMsg m;
    int   i;

    /* ! LA MASCHERA PRINCIPALE ESISTE DA PRIMA DEL PROGETTO. Senza, exide
     * appena aperto disegnerebbe una finestra 0x0 senza titolo: si apre
     * SEMPRE su un disegno, anche quando non c'e' niente da salvare. */
    form_azzera(&g_form[0], "principale", "Finestra");

    g_f = ex_crea("finestra", "EX-IDE - nessun progetto",
                  EX_TITOLO | EX_BORDO | EX_CHIUDI, 4, 24, 780, 486, 0, 0, proc);
    if (g_f == 0) {
        printf("exide: il server a finestre non risponde.\n");
        printf("       Avvialo con:  exwin\n");
        return 1;
    }

    g_menu = ex_menu(g_f);
    ex_menu_voce(g_menu, "File", "Nuovo progetto\tCtrl+N", ID_NUOVO);
    ex_menu_voce(g_menu, "File", "Apri...\tCtrl+O",        ID_APRI);
    ex_menu_voce(g_menu, "File", "Salva\tCtrl+S",          ID_SALVA);
    ex_menu_voce(g_menu, "File", "Salva con nome...",      ID_SALVA_COME);
    ex_menu_voce(g_menu, "File", "Chiudi",                 ID_CHIUDI);
    ex_menu_voce(g_menu, "File", "-",                      0);
    ex_menu_voce(g_menu, "File", "Esci\tCtrl+Q",           ID_ESCI);

    ex_menu_voce(g_menu, "Modifica", "Annulla\tCtrl+Z", ID_ANNULLA);
    ex_menu_voce(g_menu, "Modifica", "-",               0);
    ex_menu_voce(g_menu, "Modifica", "Copia\tCtrl+C",   ID_COPIA);
    ex_menu_voce(g_menu, "Modifica", "Incolla\tCtrl+V", ID_INCOLLA);
    ex_menu_voce(g_menu, "Modifica", "Taglia\tCtrl+X",  ID_TAGLIA);
    ex_menu_voce(g_menu, "Modifica", "Cancella",        ID_CANCELLA);
    ex_menu_voce(g_menu, "Modifica", "-",               0);
    ex_menu_voce(g_menu, "Modifica", "Cerca\tCtrl+F",   ID_CERCA);
    ex_menu_voce(g_menu, "Modifica", "Sostituisci",     ID_SOSTIT);

    ex_menu_voce(g_menu, "Strumenti", "Sorgente",    ID_SORGENTE);
    ex_menu_voce(g_menu, "Strumenti", "Shell",       ID_SHELL);
    ex_menu_voce(g_menu, "Strumenti", "Compilatore", ID_COMPILA);
    ex_menu_voce(g_menu, "Strumenti", "Librerie",    ID_LIBRERIE);
    ex_menu_voce(g_menu, "Strumenti", "Files",       ID_FILES);
    ex_menu_voce(g_menu, "Strumenti", "Directory",   ID_DIRECTORY);
    ex_menu_voce(g_menu, "Strumenti", "Progetto",    ID_PROGETTO);

    ex_menu_voce(g_menu, "Aiuto", "Manuale",          ID_MANUALE);
    ex_menu_voce(g_menu, "Aiuto", "Informazioni su",  ID_INFO);

    ex_crea("intestazione", "Strumenti", EX_FIGLIO, 6, 24, 152, 20, g_f, 0, 0);
    g_lst_strum = ex_crea("lista", "", EX_FIGLIO, 6, 46, 152, 396,
                          g_f, ID_STRUMENTI, 0);
    for (i = 0; i < STRUM_N; i++)
        ex_lista_aggiungi(g_lst_strum, g_strum[i].etichetta);

    /* ! LA STRISCIA SOPRA LA TELA ERA VUOTA, ed e' esattamente larga quanto
     * la tela: 436 pixel fra il pannello degli strumenti e quello delle
     * proprieta'. L'elenco delle maschere sta li' perche' e' li' che si
     * guarda quando ci si chiede «quale sto disegnando» — sopra il disegno,
     * non in un menu che bisogna aprire per sapere la risposta. */
    g_cmb_form = ex_crea("combo", "", EX_FIGLIO, TELA_X, 24, 256, 22,
                         g_f, ID_FORM, 0);
    ex_crea("pulsante", "Nuova", EX_FIGLIO, TELA_X + 262, 24, 84, 22,
            g_f, ID_FORM_NUOVA, 0);
    ex_crea("pulsante", "Togli", EX_FIGLIO, TELA_X + 350, 24, 84, 22,
            g_f, ID_FORM_TOGLI, 0);

    ex_crea("intestazione", "Proprieta'", EX_FIGLIO, 606, 24, 168, 20,
            g_f, 0, 0);
    g_lst_prop = ex_crea("lista", "", EX_FIGLIO, 606, 46, 168, 340,
                         g_f, ID_PROPRIETA, 0);
    g_val = ex_crea("testo", "", EX_FIGLIO, 606, 392, 168, 22, g_f, ID_VALORE, 0);
    ex_crea("pulsante", "Applica", EX_FIGLIO, 606, 418, 80, 24,
            g_f, ID_APPLICA, 0);
    ex_crea("pulsante", "Elimina", EX_FIGLIO, 694, 418, 80, 24,
            g_f, ID_ELIMINA, 0);

    g_stato = ex_crea("etichetta", "", EX_FIGLIO, 8, 452, 760, 16, g_f, 0, 0);

    /* Un argomento e' la directory di un progetto da aprire subito. */
    if (argc >= 2) {
        strncpy(g_prog_dir, argv[1], PERC_MAX - 1);
        g_prog_dir[PERC_MAX - 1] = '\0';
        if (!dis_carica()) g_prog_dir[0] = '\0';
        storia_azzera();
    }

    progetto_titolo();
    form_mostra();
    prop_mostra();
    dico(g_prog_dir[0] ? "progetto aperto"
                       : "File > Nuovo progetto per cominciare");

    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    disegna_tela();
    ex_aggiorna(g_f);

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}

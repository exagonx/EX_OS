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
#define VERSIONE_APP "0.003"
EX_VERSIONE("exide", VERSIONE_APP);

/* -----------------------------------------------------------------------------
 * Gli id dei comandi. Sopra 900 quelli del menu, sotto i controlli veri.
 * --------------------------------------------------------------------------- */
#define ID_STRUMENTI   10   /* la lista degli strumenti, a sinistra */
#define ID_PROPRIETA   11   /* la lista delle proprieta', a destra */
#define ID_VALORE      12   /* la casella in cui si scrive il valore */
#define ID_APPLICA     13
#define ID_ELIMINA     14

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
#define CTRL_MAX   48
#define NOME_MAX   24
#define TESTO_MAX  48

typedef struct {
    int          usato;
    int          tipo;              /* quale riga di g_strum */
    char         nome[NOME_MAX];
    char         testo[TESTO_MAX];
    int          x, y, w, h;        /* dentro la maschera */
    unsigned int id;
    int          evento;            /* quale voce di g_strum[tipo].evento */
} Ctrl;

static Ctrl g_ctrl[CTRL_MAX];
static int  g_sel = -1;             /* il controllo scelto, -1 = nessuno */
static int  g_strum_sel = -1;       /* lo strumento armato, -1 = nessuno */

/* La maschera: la finestra che si sta disegnando. */
static char g_form_titolo[TESTO_MAX] = "Finestra";
static int  g_form_w = 400, g_form_h = 260;

/* Il progetto. */
static char g_prog_dir[PERC_MAX]  = "";     /* la directory del progetto */
static char g_prog_nome[NOME_MAX] = "";
static int  g_sporco = 0;                   /* c'e' qualcosa da salvare */

/* Le finestre e i controlli di exide. */
static ExFinestra g_f, g_lst_strum, g_lst_prop, g_val, g_stato, g_menu;

/* L'area della maschera dentro la finestra di exide. */
#define TELA_X   164
#define TELA_Y    46
#define TELA_W   436
#define TELA_H   396

/* ! LA GRIGLIA E' DI QUATTRO PIXEL, e serve a una cosa sola: due controlli
 * messi «alla stessa altezza» ci stanno davvero. A mano si sbaglia di un
 * pixel, e un pixel di disallineamento si vede su una fila di pulsanti. */
#define GRIGLIA   4

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
static void disegna_maniglie(const Ctrl *c, int ox, int oy)
{
    int x = ox + c->x, y = oy + c->y, i;
    int px[8], py[8];

    px[0] = x - 2;              py[0] = y - 2;
    px[1] = x + c->w / 2 - 2;   py[1] = y - 2;
    px[2] = x + c->w - 2;       py[2] = y - 2;
    px[3] = x - 2;              py[3] = y + c->h / 2 - 2;
    px[4] = x + c->w - 2;       py[4] = y + c->h / 2 - 2;
    px[5] = x - 2;              py[5] = y + c->h - 2;
    px[6] = x + c->w / 2 - 2;   py[6] = y + c->h - 2;
    px[7] = x + c->w - 2;       py[7] = y + c->h - 2;

    for (i = 0; i < 8; i++) ex_riempi(g_f, px[i], py[i], 5, 5, EX_NERO);
}

static void disegna_tela(void)
{
    int ox = TELA_X + 8, oy = TELA_Y + 8 + 20;      /* dentro il telaio finto */
    int i;

    /* Il ripiano su cui sta la maschera. */
    ex_riempi(g_f, TELA_X, TELA_Y, TELA_W, TELA_H, 0x00505050);
    ex_incavo(g_f, TELA_X, TELA_Y, TELA_W, TELA_H);

    /* La maschera: telaio, barra del titolo, area del client. */
    ex_riempi(g_f, TELA_X + 6, TELA_Y + 6, g_form_w + 4, g_form_h + 24,
              EX_GRIGIO);
    ex_rilievo(g_f, TELA_X + 6, TELA_Y + 6, g_form_w + 4, g_form_h + 24);
    ex_riempi(g_f, TELA_X + 8, TELA_Y + 8, g_form_w, 20, EX_BLU);
    ex_scrivi(g_f, TELA_X + 13, TELA_Y + 10, g_form_titolo, EX_BIANCO);
    ex_riempi(g_f, ox, oy, g_form_w, g_form_h, EX_GRIGIO);

    for (i = 0; i < CTRL_MAX; i++)
        if (g_ctrl[i].usato) disegna_controllo(&g_ctrl[i], ox, oy);

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

static void prop_mostra(void)
{
    int k;
    char riga[80], val[64];

    ex_lista_svuota(g_lst_prop);

    if (g_sel < 0 || !g_ctrl[g_sel].usato) {
        ex_lista_aggiungi(g_lst_prop, "titolo");
        ex_lista_aggiungi(g_lst_prop, "larghezza");
        ex_lista_aggiungi(g_lst_prop, "altezza");
        ex_testo_metti(g_val, "");
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

static void prop_applica(void)
{
    Ctrl *c;
    int   k = (int)ex_lista_scelta(g_lst_prop);
    const char *v = ex_testo_prendi(g_val);
    int   n;

    if (g_sel < 0 || !g_ctrl[g_sel].usato) {
        /* Senza controllo scelto le proprieta' sono quelle della maschera. */
        if (k == 0) {
            strncpy(g_form_titolo, v, TESTO_MAX - 1);
            g_form_titolo[TESTO_MAX - 1] = '\0';
        } else {
            n = atoi(v);
            if (n < 80)   n = 80;
            if (n > 1000) n = 1000;
            if (k == 1) g_form_w = n; else g_form_h = n;
        }
        g_sporco = 1;
        dico("maschera aggiornata");
        return;
    }

    c = &g_ctrl[g_sel];
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
    case 4: c->w = atoi(v) < 8 ? 8 : atoi(v); break;
    case 5: c->h = atoi(v) < 8 ? 8 : atoi(v); break;
    case 6: c->id = (unsigned int)atoi(v); break;
    case 7: {
        /* ! L'EVENTO SI SCEGLIE FRA QUELLI CHE IL CONTROLLO HA, e scriverne uno
         * inventato non fa niente: il codice generato chiamerebbe una funzione
         * che non verra' mai chiamata da nessuno. */
        int e;

        for (e = 0; e < EVENTI_MAX; e++)
            if (g_strum[c->tipo].evento[e] &&
                strcmp(g_strum[c->tipo].evento[e], v) == 0) {
                c->evento = e;
                break;
            }
        if (e == EVENTI_MAX) dico("evento sconosciuto per questo controllo");
        break;
    }
    default: break;
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

        if (!c->usato) continue;
        if (x >= ox + c->x && x < ox + c->x + c->w &&
            y >= oy + c->y && y < oy + c->y + c->h)
            return i;
    }
    return -1;
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

    c = &g_ctrl[i];
    memset(c, 0, sizeof(*c));
    c->usato = 1;
    c->tipo  = tipo;
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
static int dis_salva(void)
{
    char p[PERC_MAX], riga[256];
    int  fd, i;

    percorso(p, "finestra.dis");
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { dico("non riesco a scrivere finestra.dis"); return 0; }

    sprintf(riga, "# exide 0.001 - lo scrive e lo legge solo exide\n"
                  "f %d %d %s\n", g_form_w, g_form_h, g_form_titolo);
    write(fd, riga, strlen(riga));

    for (i = 0; i < CTRL_MAX; i++) {
        Ctrl *c = &g_ctrl[i];

        if (!c->usato) continue;
        sprintf(riga, "c %s %s %u %d %d %d %d %d %s\n",
                g_strum[c->tipo].classe, c->nome, c->id,
                c->x, c->y, c->w, c->h, c->evento, c->testo);
        write(fd, riga, strlen(riga));
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
    unsigned int col = 0;

    percorso(p, "finestra.dis");
    fd = open(p, O_RDONLY, 0);
    if (fd < 0) return 0;

    memset(g_ctrl, 0, sizeof(g_ctrl));
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

            if (strcmp(w, "f") == 0) {
                s = parola(s, w, sizeof(w)); g_form_w = atoi(w);
                s = parola(s, w, sizeof(w)); g_form_h = atoi(w);
                strncpy(g_form_titolo, s, TESTO_MAX - 1);
                g_form_titolo[TESTO_MAX - 1] = '\0';
                continue;
            }
            if (strcmp(w, "c") != 0) continue;

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

static int gen_h(void)
{
    char p[PERC_MAX], riga[256], idn[NOME_MAX + 8], hn[NOME_MAX + 32];
    int  fd, i;

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

    SCRIVI("\nextern ExFinestra g_form;\n");
    for (i = 0; i < CTRL_MAX; i++)
        if (g_ctrl[i].usato) {
            sprintf(riga, "extern ExFinestra h_%s;\n", g_ctrl[i].nome);
            SCRIVI(riga);
        }

    SCRIVI("\nvoid finestra_crea(void);\n"
           "long finestra_proc(ExFinestra f, unsigned int msg,\n"
           "                   unsigned int wp, long lp);\n\n"
           "/* Gli handler degli eventi: li scrivi tu in finestra.c. */\n");

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
    int  fd, i;

    percorso(p, "finestra_gen.c");
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;

#define SCRIVI(s) write(fd, (s), strlen(s))
    SCRIVI("/* GENERATO DA EXIDE - non modificare: si riscrive a ogni\n"
           " * salvataggio. Il tuo codice sta in finestra.c. */\n\n"
           "#include \"libc.h\"\n#include \"finestra.h\"\n\n"
           "ExFinestra g_form;\n");

    for (i = 0; i < CTRL_MAX; i++)
        if (g_ctrl[i].usato) {
            sprintf(riga, "ExFinestra h_%s;\n", g_ctrl[i].nome);
            SCRIVI(riga);
        }

    SCRIVI("\nvoid finestra_crea(void)\n{\n");
    sprintf(riga, "    g_form = ex_crea(\"finestra\", \"%s\",\n"
                  "                     EX_TITOLO | EX_BORDO | EX_CHIUDI,\n"
                  "                     EX_AUTO, EX_AUTO, %d, %d, 0, 0,\n"
                  "                     finestra_proc);\n"
                  "    if (g_form == 0) return;\n\n",
            g_form_titolo, g_form_w, g_form_h);
    SCRIVI(riga);

    for (i = 0; i < CTRL_MAX; i++) {
        Ctrl *c = &g_ctrl[i];

        if (!c->usato) continue;
        nome_id(c, idn);
        sprintf(riga, "    h_%s = ex_crea(\"%s\", \"%s\", EX_FIGLIO,\n"
                      "        %d, %d, %d, %d, g_form, %s, 0);\n",
                c->nome, g_strum[c->tipo].classe, c->testo,
                c->x, c->y, c->w, c->h, idn);
        SCRIVI(riga);
    }

    SCRIVI("\n    ex_procedura_base(g_form, EXM_DISEGNA, 0, 0);\n}\n\n"
           "long finestra_proc(ExFinestra f, unsigned int msg,\n"
           "                   unsigned int wp, long lp)\n{\n"
           "    if (msg == EXM_COMANDO) {\n"
           "        switch (wp) {\n");

    for (i = 0; i < CTRL_MAX; i++) {
        Ctrl *c = &g_ctrl[i];

        if (!c->usato || !ha_evento(c)) continue;
        nome_id(c, idn);
        nome_handler(c, hn);
        sprintf(riga, "        case %s: %s(); return 0;\n", idn, hn);
        SCRIVI(riga);
    }

    SCRIVI("        default: break;\n        }\n    }\n\n"
           "    if (msg == EXM_CHIUDI) { ex_esci(0); return 0; }\n"
           "    return ex_procedura_base(f, msg, wp, lp);\n}\n");
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

static void ed_cerca(void)
{
    static char cosa[64] = "";
    unsigned int i, n, r0 = 0, c0 = 0;

    if (!ex_dlg_riga("Cerca", "testo da cercare:", cosa, sizeof(cosa))) return;
    if (cosa[0] == '\0') return;

    ex_area_cursore(g_ed_cod, &r0, &c0);
    n = ex_area_righe(g_ed_cod);

    for (i = 1; i <= n; i++) {
        unsigned int k = (r0 + i) % n;
        const char *r = ex_area_riga(g_ed_cod, k);
        const char *t = strstr(r, cosa);

        if (t) {
            ex_area_vai(g_ed_cod, k, (unsigned int)(t - r));
            ex_testo_metti(g_ed_stato, "trovato");
            return;
        }
    }
    ex_testo_metti(g_ed_stato, "non trovato");
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
        case ID_SOSTIT:
            ex_testo_metti(g_ed_stato, "Sostituisci: non ancora");
            break;
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
    g_sel = -1;
    strcpy(g_form_titolo, g_prog_nome);
    g_form_w = 400; g_form_h = 260;

    progetto_salva();
    progetto_titolo();
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

    g_sel = -1;
    g_sporco = 0;
    progetto_titolo();
    prop_mostra();
    dico("progetto aperto");
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
"COME SI FA UN PROGRAMMA",
"",
"1. File > Nuovo progetto, e si da' una directory: exide ci crea dentro",
"   src, inc, lib, bin e obj, piu' progetto.txt con autore e data.",
"2. Si sceglie uno strumento nell'elenco a sinistra e si clicca sulla",
"   maschera: il controllo nasce li', con un nome e un id suoi.",
"3. Lo si sposta trascinandolo. Le proprieta' a destra dicono nome,",
"   testo, posizione, misura, id ed evento: si sceglie la riga, si",
"   scrive nella casella e si preme Applica.",
"4. Doppio clic sul controllo: si apre il sorgente dentro la funzione",
"   che l'evento chiamera'. Se non c'era, exide la aggiunge vuota.",
"5. File > Salva scrive finestra.dis, finestra.h e finestra_gen.c.",
"",
"I QUATTRO FILE, E QUALE E' TUO",
"",
"  finestra.dis     il disegno: lo legge e lo scrive solo exide",
"  finestra.h       gli id, i puntatori e i prototipi - generato",
"  finestra_gen.c   crea i controlli e smista gli eventi - generato",
"  finestra.c       IL TUO: exide ci aggiunge in fondo gli handler che",
"                   mancano e non riscrive mai quel che c'e' gia'",
"",
"UN ESEMPIO: DUE CASELLE CHE SI SCAMBIANO IL TESTO",
"",
"  Metti sulla maschera due Casella e un Pulsante.",
"  Chiama le caselle Casella1 e Casella2 (proprieta' nome).",
"  Doppio clic sul pulsante: si apre Pulsante1_Clic() in finestra.c.",
"  Dentro, scrivi:",
"",
"      char t[64];",
"",
"      strncpy(t, ex_testo_prendi(h_Casella1), sizeof(t) - 1);",
"      t[sizeof(t) - 1] = 0;",
"      ex_testo_metti(h_Casella1, ex_testo_prendi(h_Casella2));",
"      ex_testo_metti(h_Casella2, t);",
"      ex_procedura_base(g_form, EXM_DISEGNA, 0, 0);",
"",
"  I nomi h_Casella1 e h_Casella2 li ha dichiarati finestra.h: il nome",
"  del controllo con h_ davanti.",
"",
"GLI STRUMENTI, E A COSA SERVONO",
"",
"  Pulsante      si preme e chiama il suo handler (evento Clic)",
"  Etichetta     testo e basta, non si puo' scrivere dentro",
"  Casella       una riga di testo modificabile",
"  Spunta        acceso o spento, indipendente dalle altre",
"  Radio         una scelta fra fratelli: accenderne uno spegne gli",
"                altri che hanno lo STESSO PADRE (mettili in un",
"                Riquadro per fare due gruppi separati)",
"  Riquadro      una cornice con un titolo: raggruppa, e fa da padre",
"  Separatore    una riga incisa",
"  Intestazione  una fascia blu con un titolo",
"  Lista         un elenco che scorre, con una riga scelta",
"  Area testo    testo su piu' righe, con cursore e selezione",
"  Area codice   come l'area testo, ma piu' capiente e colorabile",
"  Elenco        a discesa: si vede la scelta, si apre l'elenco",
"  Linguette     una fila di tab, una scelta",
"  Scorrimento   una barra: verticale o orizzontale secondo la forma",
"",
"IL MENU",
"",
"  File        opera sul PROGETTO, non sul singolo sorgente",
"  Modifica    le voci classiche di un editor (dentro il sorgente)",
"  Strumenti   Sorgente apre l'editor; Shell una riga di comando nella",
"              directory del progetto; Compilatore, Librerie, Files,",
"              Directory e Progetto sono in arrivo",
"  Aiuto       questo manuale e le informazioni sul programma",
0
};

static void manuale(void)
{
    static ExFinestra fm = 0, am = 0;
    int i;

    if (fm) { ex_procedura_base(fm, EXM_DISEGNA, 0, 0); return; }

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

    g_prg_nome[0] = '\0';
    g_prg_creato[0] = '\0';
    ex_area_svuota(g_prg_c_nota);

    progetto_percorso(p);
    fd = open(p, O_RDONLY, 0);
    if (fd < 0) {
        /* Nessun file: si propone com'era in progetto_crea(), ma non si
         * scrive — solo Salva scrive. */
        const char *dn = strrchr(g_prog_dir, '/');
        RtcTime     ora;

        strncpy(g_prg_nome, dn ? dn + 1 : g_prog_dir, PRG_CAMPO_MAX - 1);
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

                if (prg_valore(riga, "nome", g_prg_nome, sizeof(g_prg_nome))) continue;
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

            prop_valore((int)ex_lista_scelta(g_lst_prop), v, sizeof(v));
            ex_testo_metti(g_val, v);
            ex_fuoco(g_val);
            break;
        }

        case ID_VALORE:
        case ID_APPLICA:  prop_applica(); break;
        case ID_ELIMINA:  elimina();      break;

        case ID_NUOVO:      progetto_nuovo();  break;
        case ID_APRI:       progetto_apri();   break;
        case ID_SALVA:      progetto_salva();  break;
        case ID_SALVA_COME:
            dico("Salva con nome: per ora si usa File > Nuovo progetto");
            break;
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
        case ID_FILES:
        case ID_DIRECTORY:
            dico("questa voce e' in arrivo: vedi Aiuto > Manuale");
            break;

        case ID_MANUALE:   manuale();       break;
        case ID_INFO:      informazioni();  break;

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
        if (g_trascina && g_sel >= 0 && g_ctrl[g_sel].usato) {
            int ox = TELA_X + 8, oy = TELA_Y + 28;
            int nx = arrotonda(EX_X(lp) - g_tras_dx - ox);
            int ny = arrotonda(EX_Y(lp) - g_tras_dy - oy);

            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            if (nx + g_ctrl[g_sel].w > g_form_w) nx = g_form_w - g_ctrl[g_sel].w;
            if (ny + g_ctrl[g_sel].h > g_form_h) ny = g_form_h - g_ctrl[g_sel].h;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;

            if (nx != g_ctrl[g_sel].x || ny != g_ctrl[g_sel].y) {
                g_ctrl[g_sel].x = nx;
                g_ctrl[g_sel].y = ny;
                g_sporco = 1;
                prop_mostra();
            }
        }
        return 0;

    case EXM_MOUSE_SU:
        g_trascina = 0;
        return 0;

    case EXM_TASTO:
        if ((wp & KBD_KEY_MASK) == KBD_K_DEL) { elimina(); return 0; }
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
    }

    progetto_titolo();
    prop_mostra();
    dico(g_prog_dir[0] ? "progetto aperto"
                       : "File > Nuovo progetto per cominciare");

    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    disegna_tela();
    ex_aggiorna(g_f);

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}

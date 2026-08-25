/* =============================================================================
 * exwin/bin/browser/browser.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il browser: mette insieme tutto quello che e' stato costruito
 *
 *     exhttp    prende i byte dalla rete e segue le redirezioni
 *     exhtml    da testo ad albero
 *     exfont    misura e disegna il testo, anche proporzionale
 *     exwin     la finestra, la casella dell'indirizzo, i clic
 *
 * ! L'IMPAGINAZIONE MISURA IL TESTO, NON CONTA LE LETTERE, ed e' la ragione
 * per cui i font sono arrivati prima di questo file. `larghezza = lettere per
 * otto` e' vero solo col font di sistema: un motore nato su quel presupposto
 * andrebbe riscritto il giorno che si sceglie un font proporzionale, cioe'
 * subito.
 *
 * ! E SI IMPAGINA IN DUE TEMPI: prima si producono i PEZZI — un pezzo e' un
 * tratto di testo su una riga, con la sua posizione e il suo aspetto — poi si
 * disegnano solo quelli che si vedono. Impaginare a ogni disegno vorrebbe dire
 * rifare tutto il lavoro a ogni riga di scorrimento; disegnare tutto vorrebbe
 * dire dipingere migliaia di righe fuori dalla finestra.
 *
 * ! QUELLO CHE C'E', E QUESTA RIGA E' RIMASTA INDIETRO DI GIORNI. Diceva
 * «niente CSS, niente tabelle impaginate come tabelle»: erano vere quando il
 * file e' nato, e sono diventate false senza che nessuno le riguardasse —
 * proprio come il commento del font che diceva Latin-1 su dati CP437. Un
 * elenco di cio' che manca invecchia peggio del codice: il codice, quando
 * cambia, lo si sta guardando.
 *
 * Oggi ci sono: i blocchi e il testo che scorre e si spezza, i collegamenti,
 * le immagini, i FOGLI DI STILE (excss: colori, corpo, stile e peso del
 * carattere, i quattro margini, l'allineamento, `display`, selettori per tag,
 * classe e id) e le TABELLE impaginate come tabelle, annidate comprese.
 *
 * ! E DAL 25 AGOSTO 2026 ANCHE `https`, che era il muro grosso: TLS 1.3 con
 * X25519, ChaCha20-Poly1305 e la catena dei certificati verificata contro un
 * magazzino di CA vere. Il tubo cifrato sta sotto exhttp e da qui non si vede
 * — questo file non ha una riga che sappia se sotto ci sia il TLS.
 *
 * ! QUELLO CHE NON C'E', DICHIARATO: i siti che servono SOLO certificati
 * ECDSA (la verifica della catena vuole RSA), `colspan` e `rowspan`, i moduli
 * (`form` e `input` si impaginano ma non si compilano ne' si inviano),
 * JavaScript, e la compressione, che non si chiede apposta.
 *
 * ! LE IMMAGINI ARRIVANO DOPO IL TESTO, ED E' LA DECISIONE CHE CONTA QUI. La
 * pagina si impagina e si disegna con le sole parole; solo allora si scarica
 * un'immagine per volta, e a ognuna che arriva si reimpagina e si ridisegna.
 * Prenderle prima vorrebbe dire una finestra vuota finche' l'ultima non
 * risponde — e una che non risponde costa otto secondi da sola, cioe' una
 * pagina con cinque immagini morte non si vedrebbe per quaranta secondi.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exlib.h"
#include "eximg.h"
#include "exhttp.h"
#include "html.h"
#include "css.h"
#include "exdlg.h"
#include "exinfo.h"
#include "kbd_proto.h"

/* +0.001 a ogni modifica: `browser -version` la stampa. Vedi EX_VERSIONE in libc.h. */
#define VERSIONE_APP "0.001"
EX_VERSIONE("browser", VERSIONE_APP);

#define FIN_W       760
#define FIN_H       520

#define BARRA_H     30          /* la riga dell'indirizzo */
#define MARGINE     8

#define ID_URL      1
#define ID_VAI      2
#define ID_INDIETRO 3
#define ID_INFO     4

/* ! I TETTI SONO DICHIARATI E NON SI CRESCE: una pagina la sceglie chi sta
 * dall'altra parte, quindi ogni numero che dipende da lei ha un limite. Una
 * pagina che sfora si vede a meta' e lo dice, che e' meglio di una macchina
 * che rallenta finche' non finisce la memoria. */
/* =============================================================================
 * I TETTI, E IL CONTO CHE LI HA SCELTI
 *
 * ! NON SONO NUMERI TONDI SCELTI A OCCHIO: vengono da una pagina vera. La voce
 * «Operating system» di Wikipedia e' 676 kilobyte di HTML e circa undicimila
 * tag; con i tetti di prima — mezzo megabyte e quattromila nodi — si troncava
 * a meta', e il troncamento di una pagina non e' una pagina corta: e' un
 * albero senza chiusure, cioe' un'impaginazione che sbaglia dalla' in giu'.
 *
 * ! E IL CONTO SI FA IN FACCIA, perche' questa e' una macchina da 32 MB. Ecco
 * cosa costa ogni tetto, e il totale e' cio' che il browser occupa PRIMA di
 * avere una pagina:
 *
 *     la pagina scaricata          1024 KB
 *     i nodi dell'albero            750 KB   (24000 x 32 byte)
 *     gli attributi                 125 KB   (16000 x 8)
 *     l'arena (testo E attributi)  1024 KB
 *     i pezzi impaginati            750 KB   (24000 x 32)
 *     gli indirizzi dei link        200 KB   (arena + scostamenti)
 *     la cronologia                  19 KB
 *     le immagini                 128 KB + il tetto scelto all'avvio
 *                                  -------
 *                                   4020 KB piu' i pixel
 *
 * Tre megabyte e mezzo FISSI, piu' i pixel delle immagini — e quelli non sono
 * una costante: si decidono all'avvio su un sedicesimo della memoria libera,
 * fra un quarto di milione e due milioni di pixel. Su una macchina da 32 MB
 * sono circa un megabyte e mezzo; su una da 128 il massimo. Vedi IMM_PX_MIN.
 * ========================================================================== */
#define PAGINA_MAX  (1024u * 1024u)
#define NODI_MAX    24000

/* =============================================================================
 * ! L'ARENA NON E' PIENA DI TESTO: E' PIENA DI ATTRIBUTI.
 *
 * Per settimane si e' chiamata «l'arena del testo» e la si e' creduta stretta
 * per via delle parole della pagina. Misurata sulla voce «Operating system» di
 * Wikipedia, quel che ci finisce dentro e':
 *
 *     il testo dei nodi         71 KB    15%
 *     i nomi dei tag            20 KB     4%
 *     gli ATTRIBUTI            386 KB    81%   <-- il grosso
 *
 * cioe' 467 KB, e il tetto era 512. Il testo VISIBILE di quella voce sta in
 * settanta kilobyte: si troncava una pagina di 70 KB di parole perche' non
 * c'era posto per gli attributi.
 *
 * ! E FRA GLI ATTRIBUTI I PIU' PESANTI SONO href (96 KB), class (83 KB),
 * id (54 KB) e title (52 KB). I primi tre servono davvero — i collegamenti e i
 * selettori del CSS — mentre `title`, `typeof`, `about` e i `data-*` non li
 * guarda nessuno qui dentro: sono un centinaio di kilobyte di arena spesi per
 * niente. Non filtrarli e' una scelta, non una svista: un attributo che non si
 * salva non si puo' piu' chiedere, e la lista di quelli «che non servono» e'
 * esattamente il genere di elenco che si dimentica di aggiornare il giorno che
 * serve. Un megabyte costa mezzo megabyte di BSS e non si dimentica.
 * ========================================================================== */
#define ARENA_MAX   (1024u * 1024u)

/* ! E GLI ATTRIBUTI SI CONTANO ANCHE A UNO A UNO, non solo a byte. Quella voce
 * ne ha circa tredicimilaseicento: col tetto a dodicimila, alzare la sola arena
 * avrebbe spostato la troncatura di un metro senza toglierla. Otto byte l'uno,
 * quindi sedicimila costano centoventotto kilobyte. */
#define ATTR_MAX    16000
#define PEZZI_MAX   24000

/* ! GLI INDIRIZZI DEI LINK STANNO IN UN'ARENA, NON IN CASELLE FISSE. Erano
 * 512 caselle da 600 byte — 300 KB — e per tenerne duemila (una pagina di
 * Wikipedia ne ha tanti) sarebbero diventati 1,2 MB, quasi tutti spazio vuoto:
 * un indirizzo medio sta in sessanta byte, non in seicento. Con l'arena i
 * duemila link costano quanto sono lunghi davvero. */
#define LINK_MAX    2048
#define LINK_ARENA  (192u * 1024u)
#define STORIA_MAX  32

/* ! E I TETTI DELLE IMMAGINI SONO TRE, PERCHE' TRE SONO LE COSE CHE LA PAGINA
 * SCEGLIE: quante ne mette, quanto pesa ognuna, e quanti pixel diventano una
 * volta decodificate. Il terzo e' quello che conta davvero: centoventotto
 * chilobyte di PNG possono essere quattromila per tremila pixel, cioe'
 * quarantotto megabyte su una macchina che ne ha trentadue. */
/* ! I TETTI DEL FOGLIO DI STILE, con la stessa regola di tutto il resto: li
 * sceglie chi apre la pagina, non la pagina. Un sito con diecimila regole non
 * deve poter decidere quanta memoria prendere qui — si applica quello che ci
 * sta e si dice che il resto e' stato lasciato fuori. */
#define CSS_REGOLE_MAX  600
#define CSS_DICH_MAX    2000
#define CSS_ARENA_MAX   (24u * 1024u)
#define CSS_FOGLI_MAX   4       /* quanti <link rel=stylesheet> si seguono */

/* ! SESSANTAQUATTRO E NON DODICI, E IL CONTO NON E' QUELLO CHE SEMBRA. Una
 * casella di questo elenco costa l'indirizzo che ci sta dentro — 600 byte piu'
 * una manciata — non i pixel: passare da dodici a sessantaquattro sono
 * trentadue kilobyte, cioe' niente. I PIXEL LI CONTA il tetto qui sotto, ed e'
 * quello il tetto vero.
 *
 * ! CHE E' ANCHE IL MOTIVO PER CUI ALZARE QUESTO NUMERO DA SOLO NON SERVIVA A
 * NIENTE. Sulla voce «Operating system» di Wikipedia le immagini con le misure
 * dichiarate sono venticinque e, dopo il cap alla finestra, vogliono 2,2
 * milioni di pixel: con mezzo milione ne entravano CINQUE. Il tetto che si
 * toccava non era mai stato dodici. */
#define IMM_MAX      64
#define IMM_BYTE_MAX (128u * 1024u)     /* il file di UNA immagine  */

/* =============================================================================
 * ! QUANTI PIXEL SI TENGONO LO DECIDE LA MACCHINA, NON QUESTA RIGA.
 *
 * Era una costante, ed era la cosa sbagliata: mezzo milione di pixel sono
 * avari su un PC con 128 MB e sono ANCORA TROPPI su uno con 32, dove il
 * browser occupa gia' tre megabyte e mezzo di suo. Sulla voce «Operating
 * system» di Wikipedia si sono visti tutt'e due i difetti lo stesso
 * pomeriggio: con mezzo milione entravano CINQUE immagini su venticinque, e
 * alzando a un milione la macchina da 32 MB finiva le pagine fisiche — il
 * kernel scriveva «OUT OF MEMORY» e il browser ripiegava sugli `alt`.
 *
 * ! UN SEDICESIMO DELLA MEMORIA LIBERA. I due estremi sono dichiarati perche'
 * nessuna delle due direzioni deve poter scappare: un quarto di milione di
 * pixel e' il minimo sotto cui la pagina non e' piu' una pagina, due milioni
 * il massimo che ha senso tenere per una finestra sola.
 *
 * ! E RESTA UN TETTO, NON UNA PROMESSA. `ridimensiona` puo' fallire lo stesso
 * se qualcun altro ha preso la memoria nel frattempo, e quel caso e' gia'
 * gestito: l'immagine si salta e al suo posto si legge il suo `alt`. Questo
 * conto serve a non ARRIVARCI, non a sostituire il controllo.
 * ========================================================================== */
#define IMM_PX_MIN   (256u * 1024u)
#define IMM_PX_MAX   (2048u * 1024u)

static unsigned int g_imm_px_tot = IMM_PX_MIN;   /* deciso all'avvio */

static void imm_tetto_scegli(void)
{
    MemInfo mi;

    if (meminfo(&mi) != 0) { g_imm_px_tot = IMM_PX_MIN; return; }

    /* free_kb / 16 kilobyte, in pixel da quattro byte: free_kb * 16. */
    g_imm_px_tot = mi.free_kb * 16u;

    if (g_imm_px_tot < IMM_PX_MIN) g_imm_px_tot = IMM_PX_MIN;
    if (g_imm_px_tot > IMM_PX_MAX) g_imm_px_tot = IMM_PX_MAX;
}

/* Un tratto di testo — o un'immagine — gia' collocato. */
/* ! IL PEZZO PORTA IL CARATTERE E IL COLORE GIA' SCELTI, e non piu' un «e' un
 * titolo si'/no». Con i fogli di stile il carattere non e' piu' una di due
 * possibilita': dipende da `font-weight`, `font-style` e `font-size`, che
 * cambiano elemento per elemento. Deciderlo durante l'impaginazione — dove lo
 * stile e' gia' calcolato — e lasciare al disegno solo il compito di usarlo
 * tiene la scelta in un posto solo. */
typedef struct {
    int          x, y, w;
    unsigned int testo;         /* scostamento nell'arena del documento */
    ExFont       font;          /* il carattere, gia' scelto            */
    unsigned int colore;        /* ARGB, gia' deciso                    */
    short        h;             /* immagini e controlli: la loro altezza */
    short        link;          /* indice in g_link, -1 = niente         */
    short        img;           /* indice in g_imm, -1 = e' testo        */
    short        ctrl;          /* indice in g_ctrl, -1 = non e' un controllo */
} Pezzo;

/* =============================================================================
 * I CONTROLLI DI UN MODULO
 *
 * ! SI DISEGNANO QUI DENTRO E NON SONO FINESTRE DEL TOOLKIT, ed e' la scelta
 * che decide tutto il resto. Un `<input>` in mezzo a un paragrafo scorre col
 * testo: farne una finestra figlia vorrebbe dire spostarla a ogni riga di
 * scorrimento, e ritagliarla quando esce dall'area — cioe' rifare a mano il
 * lavoro che il disegno diretto fa da solo. Sono rettangoli con dentro del
 * testo, come tutto il resto della pagina.
 *
 * ! E QUELLO CHE C'E' E' LA FORMA, NON L'INVIO. Si vedono, si cliccano, in una
 * casella si scrive; ma nessun modulo parte. Mandare un modulo vuol dire
 * costruire una query o un corpo POST, seguire `action` e `method`, e
 * codificare i campi: e' un lavoro che comincia dove questo finisce, ed e'
 * dichiarato qui invece che scoperto premendo un pulsante che non fa niente —
 * per questo un pulsante premuto lo DICE nella barra di stato.
 * ========================================================================== */
#define CTRL_MAX        64
/* ! DUECENTOCINQUANTASEI E NON NOVANTASEI, e la differenza si vede in una
 * <textarea>: novantasei byte sono una riga e mezza, e il resto del testo
 * spariva dentro un riquadro alto ottantotto pixel che sembrava mezzo vuoto.
 * Sessantaquattro controlli per 256 byte sono sedici kilobyte. */
#define CTRL_VAL_MAX    256

#define CTRL_TESTO      0       /* input di testo, password, ricerca... */
#define CTRL_PULSANTE   1       /* button, submit, reset, image         */
#define CTRL_SPUNTA     2       /* checkbox                             */
#define CTRL_RADIO      3       /* radio                                */
#define CTRL_SCELTA     4       /* select                               */
#define CTRL_AREA       5       /* textarea                             */

#define CTRL_NOME_MAX   40

/* ! LE OPZIONI DI UNA SCELTA STANNO IN UN ELENCO CONDIVISO, e ogni <select>
 * ne possiede un tratto: sono stringhe corte e poche, e una casella per
 * controllo sarebbe stata memoria buttata su ogni pagina che non ha scelte. */
#define OPZ_MAX     128

typedef struct {
    unsigned char tipo;
    unsigned char segreto;      /* password: si mostrano asterischi */
    unsigned char acceso;       /* spunta e radio */
    short         modulo;       /* indice del <form> che lo contiene, -1 */
    short         opz_primo;    /* scelta: la prima opzione in g_opz, -1 */
    short         opz_n;        /* quante ne ha */
    short         opz_ora;      /* quale e' scelta adesso */
    int           nodo;         /* il nodo che l'ha generato, -1 se libero */
    short         cur;          /* dove si sta scrivendo, dentro `valore` */
    char          nome[CTRL_NOME_MAX];   /* l'attributo `name` */
    char          valore[CTRL_VAL_MAX];
} Ctrl;

static char g_opz[OPZ_MAX][CTRL_VAL_MAX];
static int  g_opz_n = 0;

/* =============================================================================
 * I MODULI
 *
 * ! UN MODULO E' UN INDIRIZZO E UN VERBO, e i controlli che gli appartengono.
 * `action` dice dove, `method` dice come: GET mette i campi nella query
 * dell'indirizzo, POST li manda nel corpo. Qui si fa GET — che e' cio' che
 * fanno le ricerche, cioe' il caso per cui uno vuole un modulo in un browser
 * che non ha ancora una sessione da nessuna parte.
 *
 * ! POST E' DICHIARATO FUORI, e non per pigrizia: mandare un corpo vuol dire
 * `Content-Length`, `Content-Type` e una richiesta che exhttp oggi non sa
 * costruire — `http_richiesta` fa GET e basta. E' un lavoro onesto, ma e' un
 * lavoro in exhttp, non qui.
 * ========================================================================== */
#define MODULI_MAX      16
#define AZIONE_MAX      EXHTTP_URL_MAX

typedef struct {
    char azione[AZIONE_MAX];
    int  post;                  /* 1 = method="post" */
} Modulo;

static Modulo g_mod[MODULI_MAX];
static int    g_mod_n = 0;
static int    g_mod_ora = -1;   /* il <form> che stiamo impaginando */

static Ctrl g_ctrl[CTRL_MAX];
static int  g_ctrl_n = 0;
static int  g_ctrl_fuoco = -1;      /* quale casella prende i tasti */

static unsigned char g_pagina[PAGINA_MAX];
static HtmlNodo      g_nodi[NODI_MAX];
static HtmlAttr      g_attr[ATTR_MAX];
static char          g_arena[ARENA_MAX];
static HtmlDoc       g_doc;

static Pezzo         g_pez[PEZZI_MAX];
static int           g_pez_n = 0;

static char          g_link_arena[LINK_ARENA];
static unsigned int  g_link_off[LINK_MAX];
static unsigned int  g_link_usati = 0;
static int           g_link_n = 0;

/* L'indirizzo del link `k`, o la stringa vuota. */
static const char *link_url(int k)
{
    if (k < 0 || k >= g_link_n) return "";
    return g_link_arena + g_link_off[k];
}

static char          g_storia[STORIA_MAX][EXHTTP_URL_MAX];
static int           g_storia_n = 0;

static ExFinestra    g_f, g_url, g_stato;
static ExFont        g_font_testo = 0, g_font_titolo = 0;
static int           g_scorri = 0, g_altezza = 0;
static char          g_qui[EXHTTP_URL_MAX] = "";

/* -----------------------------------------------------------------------------
 * Le immagini
 *
 * ! L'IDENTITA' DI UN'IMMAGINE E' IL SUO NODO, non la sua posizione: la pagina
 * si reimpagina a ogni immagine che arriva, e alla seconda impaginazione la
 * prima immagine deve ritrovarsi, non riscaricarsi. Gli indici dei nodi non si
 * muovono finche' l'albero e' quello.
 *
 * ! E I PIXEL SONO NOSTRI, NON DI eximg: si decodifica, si copia nella misura
 * con cui si disegnera', e il bitmap naturale si restituisce SUBITO. Tenerlo
 * vorrebbe dire lasciar scegliere alla pagina quanta memoria prendere.
 * --------------------------------------------------------------------------- */
typedef struct {
    int           nodo;             /* il nodo <img> dentro g_doc              */
    unsigned int  dich_w, dich_h;   /* width= e height=, 0 se non ci sono      */
    unsigned int  w, h;             /* la misura con cui si disegna            */
    unsigned int  ris_w, ris_h;     /* il posto riservato prima che arrivasse  */
    unsigned int *px;               /* ARGB, nostri: free() li restituisce     */
    unsigned char stato;            /* 0 da prendere, 1 presa, 2 rinunciata    */
    char          src[EXHTTP_URL_MAX];
} Imm;

/* -----------------------------------------------------------------------------
 * I riquadri di sfondo
 *
 * ! UNO SFONDO NON E' UN PEZZO, E' CIO' CHE STA SOTTO I PEZZI, quindi vive in
 * un elenco suo e si disegna PRIMA di tutto il testo. Metterlo fra i pezzi
 * vorrebbe dire dipingere sopra le parole gia' scritte ogni volta che un
 * blocco colorato viene dopo — e l'ordine dei pezzi e' quello del documento,
 * che non ha niente a che fare con la profondita'.
 *
 * ! E LA MISURA SI SA SOLO QUANDO IL BLOCCO E' FINITO: si segna la y d'inizio
 * entrando e si chiude il riquadro uscendo.
 * --------------------------------------------------------------------------- */
#define SFONDI_MAX  256

typedef struct {
    int          x, y, w, h;
    unsigned int colore;
} Sfondo;

static Sfondo g_sfondi[SFONDI_MAX];
static int    g_sfondi_n = 0;

static CssRegola     g_css_reg[CSS_REGOLE_MAX];
static CssDich       g_css_dich[CSS_DICH_MAX];
static char          g_css_arena[CSS_ARENA_MAX];
static CssFoglio     g_css;

static Imm           g_imm[IMM_MAX];
static int           g_imm_n = 0;
static unsigned int  g_imm_px = 0;      /* quanti pixel si stanno tenendo */
static int           g_imm_fuori = 0;   /* quante lasciate fuori: non c'era posto */

/* ! LA PIU' PICCOLA CHE LA MACCHINA HA RIFIUTATO, in pixel; 0 = nessuna.
 *
 * Serve a non ripetere una domanda di cui si conosce gia' la risposta. Il tetto
 * dice quanti pixel VOGLIAMO tenere; `ridimensiona` dice quanti ce ne sono
 * DAVVERO, e le due risposte possono differire. Ma quando differiscono c'e' un
 * difetto sottile: un'immagine che fallisce non consuma tetto, quindi il
 * controllo del tetto continua a dire di si' e il ciclo SCARICA TUTTE LE ALTRE
 * per buttarle una per una. Sulla voce di Wikipedia si sono viste scaricare
 * tutte e venticinque le immagini e tenerne quasi nessuna.
 *
 * Con questo numero la domanda si fa una volta sola: se non c'e' stato posto
 * per N pixel, non ce n'e' per N o piu'. Quelle piu' piccole si provano ancora,
 * perche' per loro la risposta puo' essere davvero diversa. */
static unsigned int  g_imm_px_negato = 0;
static unsigned char g_imm_buf[IMM_BYTE_MAX];

static int  (*g_img_carica)(const unsigned char *, unsigned int, EximgBitmap *);
static void (*g_img_libera)(EximgBitmap *);

/* =============================================================================
 * LA BARRA DI SCORRIMENTO
 *
 * ! IL POSTO SI RISERVA SEMPRE, ANCHE QUANDO LA BARRA NON SERVE, e non e' per
 * pigrizia. Se la larghezza dell'area cambiasse a seconda che la pagina sia
 * lunga o corta, un documento al limite entrerebbe senza barra, l'assenza della
 * barra lo allargherebbe, l'allargamento lo accorcerebbe di una riga... e il
 * disegno oscillerebbe. E' un anello che si chiude solo togliendo la variabile
 * dal giro: sedici pixel sono di chi scorre, sempre.
 *
 * ! E IL POLLICE E' GRANDE QUANTO LA PARTE VISIBILE, in proporzione: e' l'unica
 * cosa che dice a chi guarda QUANTO manca. Una barra col cursore di misura
 * fissa e' un ornamento — sposta la pagina e non informa. */
#define SCORRI_W    16
#define SCORRI_MIN  24          /* il pollice non scende sotto: sparirebbe */

/* Definita piu' in basso, accanto al disegno: l'impaginazione la chiama
 * subito dopo css_calcola. */
static void suggerimenti(int v, CssStile *st);

static int area_x(void) { return MARGINE; }
static int area_y(void) { return BARRA_H + MARGINE; }
static int area_w(void) { return FIN_W - 2 * MARGINE - SCORRI_W; }
static int area_h(void) { return FIN_H - BARRA_H - 2 * MARGINE - 20; }

static int barra_x(void) { return area_x() + area_w() + 2; }

/* Definita piu' in basso, accanto al resto della barra: `disegna` sta qui
 * sopra e ha bisogno di poterla chiamare. */
static void disegna_barra(void);

/* eximg.so si apre una volta sola, e una volta sola si rinuncia.
 *
 * ! IL PERCORSO E' DOPPIO come per ogni libreria: su un sistema installato sta
 * in /exwin/lib, avviando dal CD sotto /cdrom.
 *
 * ! E SE NON C'E' NON SI MUORE: un browser senza eximg mostra il testo, che e'
 * la maggior parte di una pagina. E' la stessa scelta che fa il toolkit in
 * ex_immagine, per la stessa ragione. */
static int eximg_pronta(void)
{
    static const char *const dove[] = {
        "/exwin/lib/eximg.so",
        "/cdrom/exwin/lib/eximg.so"
    };
    static int cercata = 0;

    if (!cercata) {
        const ExLibTesta *t;

        cercata = 1;    /* prima del tentativo: non si ricerca a ogni immagine */
        t = exlib_apri_fra(dove, (int)(sizeof dove / sizeof dove[0]));
        if (t) {
            g_img_carica = (int (*)(const unsigned char *, unsigned int,
                                    EximgBitmap *))
                           exlib_simbolo(t, "eximg_carica");
            g_img_libera = (void (*)(EximgBitmap *))
                           exlib_simbolo(t, "eximg_libera");
        }
    }

    /* Uno dei due senza l'altro vuol dire una eximg.so piu' vecchia di questo
     * browser: si rinuncia invece di chiamare un indirizzo nullo. */
    return g_img_carica != 0 && g_img_libera != 0;
}

static void imm_libera_tutte(void)
{
    int i;

    for (i = 0; i < g_imm_n; i++)
        if (g_imm[i].px) { free(g_imm[i].px); g_imm[i].px = 0; }

    g_imm_n  = 0;
    g_imm_px = 0;
    g_imm_fuori = 0;
    g_imm_px_negato = 0;
}

/* Le cifre in testa, e basta.
 *
 * ! «80%» NON E' OTTANTA PIXEL, quindi rende 0 — cioe' «non l'hanno detto» —
 * invece di collocare un'immagine larga ottanta punti. Le percentuali vogliono
 * la larghezza del contenitore, che qui non esiste: meglio la misura vera. */
static unsigned int numero(const char *s)
{
    unsigned int v = 0;
    int          viste = 0;

    if (!s) return 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (unsigned int)(*s - '0');
        s++;
        viste = 1;
    }
    if (!viste || *s == '%') return 0;
    return v;
}

/* =============================================================================
 * ! UN'IMMAGINE CHE NON SAPREMMO DECODIFICARE NON SI SCARICA AFFATTO.
 *
 * Sembra un'ottimizzazione e non lo e': su https ogni immagine e' una stretta
 * di mano TLS intera — chiave effimera, catena di certificati, firma — e su un
 * 386 emulato sono venti secondi. La pagina «Operating system» di Wikipedia ne
 * ha undici, e OTTO SONO SVG: due minuti e mezzo passati a scaricare file che
 * finiscono comunque nel cestino, con la barra di stato che dice «immagine 11
 * di 11» e sembra bloccata.
 *
 * ! LA REGOLA E' SULL'ESTENSIONE, ed e' un'approssimazione dichiarata: il tipo
 * vero lo direbbe il Content-Type, che pero' arriva DOPO aver aperto la
 * connessione — cioe' dopo aver speso quello che si voleva risparmiare. Un
 * `.svg` servito come PNG non si vedrebbe lo stesso; un PNG chiamato `.svg`
 * si perde, e non capita.
 *
 * Vale anche per i formati vettoriali in generale: EX-OS disegna pixel, e un
 * SVG e' un disegno da eseguire, non un'immagine da leggere.
 * ========================================================================== */
static int formato_ignoto(const char *src)
{
    static const char *const FUORI[] = { ".svg", ".webp", ".avif", ".bmp", 0 };
    int n = 0, i, k;

    while (src[n]) n++;

    for (i = 0; FUORI[i]; i++) {
        int m = 0;

        while (FUORI[i][m]) m++;
        if (n < m) continue;

        for (k = 0; k < m; k++) {
            char a = src[n - m + k], b = FUORI[i][k];

            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (a != b) break;
        }
        if (k == m) return 1;
    }
    return 0;
}

/* Trova l'immagine di questo nodo, o la registra. Rende -1 se non c'e' posto. */
static int imm_indice(int nodo, const char *src)
{
    int i;

    for (i = 0; i < g_imm_n; i++)
        if (g_imm[i].nodo == nodo) return i;

    if (g_imm_n >= IMM_MAX) return -1;
    if (formato_ignoto(src)) return -1;

    i = g_imm_n++;
    g_imm[i].nodo   = nodo;
    g_imm[i].dich_w = numero(html_attr(&g_doc, nodo, "width"));
    g_imm[i].dich_h = numero(html_attr(&g_doc, nodo, "height"));
    g_imm[i].w      = 0;
    g_imm[i].h      = 0;
    g_imm[i].ris_w  = 0;
    g_imm[i].ris_h  = 0;
    g_imm[i].px     = 0;
    g_imm[i].stato  = 0;
    strncpy(g_imm[i].src, src, sizeof(g_imm[i].src) - 1);
    g_imm[i].src[sizeof(g_imm[i].src) - 1] = '\0';
    return i;
}

/* Da quanto e' l'immagine a quanto se ne disegna: `width`/`height` se ci sono,
 * altrimenti la misura naturale — e in ogni caso dentro l'area.
 *
 * ! LE PROPORZIONI SI TENGONO ANCHE QUANDO SI RIDUCE, e non e' vezzo: una
 * fotografia schiacciata si riconosce peggio di una fotografia piccola. */
static void misura(const Imm *im, unsigned int nw, unsigned int nh,
                   unsigned int *pw, unsigned int *ph)
{
    unsigned int w  = im->dich_w, h = im->dich_h;
    unsigned int aw = (unsigned int)area_w(), ah = (unsigned int)area_h();

    *pw = 0;
    *ph = 0;
    if (nw == 0 || nh == 0) return;

    if (!w && !h)  { w = nw; h = nh; }
    else if (!h)   { h = nh * w / nw; }
    else if (!w)   { w = nw * h / nh; }

    /* ! IL TETTO E' LA FINESTRA. Una pagina che dichiara width=4000 non deve
     * poter chiedere quattromila colonne di pixel. */
    if (w > aw && w) { h = h * aw / w; w = aw; }
    if (h > ah && h) { w = w * ah / h; h = ah; }

    if (w < 1) w = 1;
    if (h < 1) h = 1;

    *pw = w;
    *ph = h;
}

/* Copia nella misura voluta, col vicino piu' vicino. */
static unsigned int *ridimensiona(const EximgBitmap *bm,
                                  unsigned int w, unsigned int h)
{
    unsigned int *d = (unsigned int *)malloc(w * h * sizeof(unsigned int));
    unsigned int  y;

    if (!d) return 0;

    for (y = 0; y < h; y++) {
        const unsigned int *s = bm->px +
                                (y * bm->altezza / h) * bm->larghezza;
        unsigned int       *r = d + y * w;
        unsigned int        x;

        for (x = 0; x < w; x++) r[x] = s[x * bm->larghezza / w];
    }
    return d;
}

/* -----------------------------------------------------------------------------
 * L'impaginazione
 * --------------------------------------------------------------------------- */
static int  g_pen_x, g_pen_y, g_riga_h;
static int  g_link_ora;

/* Lo stile dell'elemento dentro cui stiamo impaginando adesso. I nodi di testo
 * non hanno uno stile proprio: usano quello del padre, che e' questo. */
static CssStile g_stile_ora;

/* -----------------------------------------------------------------------------
 * La riga e i rientri
 *
 * ! I MARGINI SINISTRO E DESTRO RESTRINGONO LA RIGA, e vanno tenuti come stato
 * dell'impaginazione perche' si ACCUMULANO: un `blockquote` dentro un altro
 * rientra due volte. Il valore si mette entrando nell'elemento e si rimette
 * com'era uscendone — la stessa disciplina del collegamento in corso.
 *
 * ! E L'ALLINEAMENTO NON SI PUO' APPLICARE MENTRE SI SCRIVE, ed e' la ragione
 * per cui serve `g_riga_primo`: per centrare una riga bisogna sapere quanto e'
 * larga, e lo si sa solo quando e' finita. Si segna dove la riga comincia, e
 * al momento di andare a capo si spostano tutti i pezzi che ci stanno dentro.
 * --------------------------------------------------------------------------- */
static int g_marg_sx, g_marg_dx;    /* rientri di adesso, in pixel */
static int g_riga_primo;            /* primo pezzo della riga in corso */

/* ! MENTRE SI MISURA NON SI ALLINEA, e non e' un dettaglio: la prima passata
 * delle tabelle impagina ogni cella su tutta la riga per vedere quanto e'
 * larga, e la larghezza si legge dal bordo destro dei pezzi. Con un `th`
 * centrato — e il foglio predefinito li centra tutti — quei pezzi finiscono in
 * mezzo alla riga, e la misura torna larga quanto meta' pagina invece che
 * quanto la parola. Il risultato erano tre colonne quasi uguali, con quella
 * lunga stretta e quella di due lettere larghissima.
 *
 * L'allineamento e' una decisione su DOVE mettere una riga; la misura chiede
 * QUANTO occupa. Tenerli separati e' la correzione, non un caso particolare. */
static int g_misura = 0;

static int riga_x(void) { return area_x() + g_marg_sx; }
static int riga_w(void)
{
    int w = area_w() - g_marg_sx - g_marg_dx;

    /* Una pagina che dichiara margini enormi non deve poter produrre una riga
     * di larghezza negativa: si stringe fino a un minimo e li' ci si ferma. */
    return w < 40 ? 40 : w;
}

/* -----------------------------------------------------------------------------
 * La riserva dei caratteri
 *
 * ! UN CARATTERE SI APRE UNA VOLTA SOLA E SI TIENE. Con i fogli di stile la
 * faccia non e' piu' una di due: `font-weight`, `font-style` e `font-size` la
 * scelgono elemento per elemento, e aprire un TrueType costa — il file si
 * legge, il contenitore si analizza, la cache dei glifi si riempie. Senza
 * questa riserva una pagina con dieci corpi diversi aprirebbe dieci volte lo
 * stesso file.
 *
 * ! E IL TETTO E' DICHIARATO: oltre, si ripiega sul carattere di sistema invece
 * di continuare ad aprirne. Le combinazioni le sceglie la pagina.
 * --------------------------------------------------------------------------- */
/* ! VENTIQUATTRO DA QUANDO LE FAMIGLIE SONO TRE. Il conto delle combinazioni
 * si e' triplicato — tre facce per quattro stili fanno gia' dodici prima ancora
 * di contare i corpi — e con dodici caselle una pagina normale le esauriva a
 * meta', ripiegando sul carattere di sistema per tutto il resto. Una casella
 * non e' un file: i byte del TrueType stanno in una riserva condivisa del
 * toolkit, qui c'e' la cache dei glifi a quel corpo. */
#define FONT_MAX    24
#define CORPO_MIN   6
#define CORPO_MAX   72

typedef struct {
    unsigned char neretto, corsivo, famiglia;
    short         corpo;
    ExFont        f;
} FontVoce;

static FontVoce g_font[FONT_MAX];
static int      g_font_n = 0;

/* ! IL CARATTERE A LARGHEZZA FISSA E' UNA FAMIGLIA, NON UN CORPO: dentro <pre>
 * e <code> gli spazi devono valere quanto le lettere, o l'incolonnamento — che
 * e' l'unica ragione per cui quel testo e' preformattato — non si vede. */
/* ! LE FAMIGLIE SONO TRE, e non piu' «fisso si'/no»: da quando il foglio di
 * stile puo' chiedere `font-family`, il senza-grazie e' una richiesta comune
 * quanto il monospazio — su un sito moderno lo e' molto di piu'. Le tre facce
 * sono quelle che il sistema porta con se'. */
#define FAM_SERIF   0
#define FAM_SANS    1
#define FAM_MONO    2

static ExFont font_per(int neretto, int corsivo, int famiglia, int corpo)
{
    static const char *const FACCIA[12] = {
        "/exwin/font/LiberationSerif-Regular.ttf",
        "/exwin/font/LiberationSerif-Bold.ttf",
        "/exwin/font/LiberationSerif-Italic.ttf",
        "/exwin/font/LiberationSerif-BoldItalic.ttf",
        "/exwin/font/LiberationSans-Regular.ttf",
        "/exwin/font/LiberationSans-Bold.ttf",
        "/exwin/font/LiberationSans-Italic.ttf",
        "/exwin/font/LiberationSans-BoldItalic.ttf",
        "/exwin/font/LiberationMono-Regular.ttf",
        "/exwin/font/LiberationMono-Bold.ttf",
        "/exwin/font/LiberationMono-Italic.ttf",
        "/exwin/font/LiberationMono-BoldItalic.ttf"
    };
    int i, k;

    if (corpo < CORPO_MIN) corpo = CORPO_MIN;
    if (corpo > CORPO_MAX) corpo = CORPO_MAX;
    neretto  = neretto ? 1 : 0;
    corsivo  = corsivo ? 1 : 0;
    if (famiglia < FAM_SERIF || famiglia > FAM_MONO) famiglia = FAM_SERIF;

    for (i = 0; i < g_font_n; i++)
        if (g_font[i].neretto == neretto && g_font[i].corsivo == corsivo &&
            g_font[i].famiglia == famiglia && g_font[i].corpo == (short)corpo)
            return g_font[i].f;

    if (g_font_n >= FONT_MAX) return g_font_testo;

    k = famiglia * 4 + neretto + corsivo * 2;
    g_font[g_font_n].f = ex_font_apri(FACCIA[k], corpo);

    /* ! ex_font_apri RENDE 0 SE IL FILE NON C'E', e zero E' il font di sistema:
     * si mette in riserva lo stesso, cosi' non si torna a cercarlo a ogni
     * parola. Una pagina con un carattere diverso e' meglio di una pagina
     * lenta. */
    g_font[g_font_n].neretto  = (unsigned char)neretto;
    g_font[g_font_n].corsivo  = (unsigned char)corsivo;
    g_font[g_font_n].famiglia = (unsigned char)famiglia;
    g_font[g_font_n].corpo    = (short)corpo;
    return g_font[g_font_n++].f;
}

/* Dentro quanti <pre>/<code> siamo: e' un contatore e non un si'/no, perche'
 * si annidano — <pre> con dentro <code> e' comunissimo. */
static int g_fisso = 0;

/* Il carattere che tocca allo stile di adesso. */
static ExFont font_di(const CssStile *st)
{
    int neretto = (st->grassetto == 1);
    int corsivo = (st->corsivo == 1);
    int corpo   = (st->corpo == CSS_MISURA_NO) ? 15 : st->corpo;
    int fam;

    /* ! IL TAG BATTE IL FOGLIO SOLO DOVE IL FOGLIO TACE. Dentro <pre> o <code>
     * il monospazio e' il valore predefinito, non un obbligo: una pagina che
     * scrive `code { font-family: sans-serif }` lo sta chiedendo davvero, e
     * quel foglio ha l'ultima parola. Ma se non dice niente, <pre> vuole il
     * monospazio — e' il motivo per cui quel tag esiste. */
    if (st->famiglia == CSS_FAM_FISSO)      fam = FAM_MONO;
    else if (st->famiglia == CSS_FAM_SANS)  fam = FAM_SANS;
    else if (st->famiglia == CSS_FAM_SERIF) fam = FAM_SERIF;
    else                                    fam = g_fisso > 0 ? FAM_MONO
                                                              : FAM_SERIF;

    if (!neretto && !corsivo && fam == FAM_SERIF && corpo == 15)
        return g_font_testo;
    return font_per(neretto, corsivo, fam, corpo);
}

static unsigned int colore_di(const CssStile *st)
{
    return (st->colore == CSS_NIENTE) ? EX_NERO : st->colore;
}

static int alt_riga_f(ExFont f)
{
    int h = ex_font_altezza(f);

    return h > 0 ? h + 3 : 19;
}

/* Sposta i pezzi della riga appena finita, se non e' allineata a sinistra. */
static void allinea_riga(void)
{
    int avanzo, dx, i;

    if (g_misura) return;
    if (g_stile_ora.allineamento != CSS_ALL_CENTRO &&
        g_stile_ora.allineamento != CSS_ALL_DX) return;
    if (g_riga_primo >= g_pez_n) return;

    avanzo = (riga_x() + riga_w()) - g_pen_x;
    if (avanzo <= 0) return;

    dx = (g_stile_ora.allineamento == CSS_ALL_CENTRO) ? avanzo / 2 : avanzo;
    for (i = g_riga_primo; i < g_pez_n; i++) g_pez[i].x += dx;
}

static void a_capo(void)
{
    /* ! UNA RIGA VUOTA NON SI ACCUMULA. Un documento indentato produce spazi
     * fra un blocco e l'altro: andando a capo per ognuno si otterrebbero
     * pagine fatte di buchi. Si va a capo solo se sulla riga c'e' qualcosa. */
    if (g_pen_x <= riga_x()) return;

    allinea_riga();

    g_pen_x  = riga_x();
    g_pen_y += g_riga_h;
    g_riga_h = alt_riga_f(font_di(&g_stile_ora));
    g_riga_primo = g_pez_n;
}

/* Lo spazio sopra e sotto un blocco. ! IL MARGINE DICHIARATO SOSTITUISCE IL
 * PREDEFINITO, non ci si somma: `margin-top: 0` deve poter togliere lo spazio,
 * e sommando non lo toglierebbe mai. */
static void spazio_blocco(int quale)
{
    int m = g_stile_ora.margine[quale];

    a_capo();
    g_pen_y += (m == CSS_MISURA_NO) ? 6 : m;
}


/* Mette una parola, andando a capo se non ci sta. */
static void parola(const char *t, unsigned int off, int n)
{
    static char cop[256];
    ExFont      f = font_di(&g_stile_ora);
    int         w, i;

    if (n <= 0) return;
    if (n > (int)sizeof(cop) - 1) n = (int)sizeof(cop) - 1;
    for (i = 0; i < n; i++) cop[i] = t[i];
    cop[n] = '\0';

    w = ex_larghezza_testo(f, cop);

    /* ! SI VA A CAPO SULLA PAROLA, NON SUL CARATTERE, ed e' cio' che rende il
     * testo leggibile: spezzare in mezzo a una parola si vede subito. Una
     * parola piu' larga della finestra si mette lo stesso e sborda — meglio
     * che sparire. */
    if (g_pen_x + w > riga_x() + riga_w() && g_pen_x > riga_x()) a_capo();

    if (g_pez_n < PEZZI_MAX) {
        g_pez[g_pez_n].x = g_pen_x;
        g_pez[g_pez_n].y = g_pen_y;
        g_pez[g_pez_n].w = w;
        g_pez[g_pez_n].testo = off;
        g_pez[g_pez_n].font = f;
        g_pez[g_pez_n].colore = colore_di(&g_stile_ora);
        g_pez[g_pez_n].h = 0;
        g_pez[g_pez_n].link = (short)g_link_ora;
        g_pez[g_pez_n].img = -1;
        g_pez[g_pez_n].ctrl = -1;
        g_pez_n++;
    }

    g_pen_x += w;
    if (alt_riga_f(f) > g_riga_h) g_riga_h = alt_riga_f(f);
}

/* Un testo intero, spezzato in parole. Serve al testo dei nodi e al testo di
 * ripiego di un'immagine, che e' la stessa cosa: parole nell'arena. */
/* =============================================================================
 * IL TESTO CHE NON VIENE DAL DOCUMENTO
 *
 * ! I SEGNI DELLE LISTE NON STANNO NELLA PAGINA, e un pezzo pero' sa indicare
 * solo un punto dell'arena del documento. Si scrivono percio' IN CODA a
 * quell'arena, dopo il segno lasciato da html_analizza: e' memoria che il
 * browser possiede gia' e che nessun altro tocca piu'.
 *
 * ! E SI RIPARTE DAL SEGNO A OGNI IMPAGINAZIONE, o la coda crescerebbe di un
 * giro per volta — e la pagina si reimpagina a ogni immagine che arriva.
 * ========================================================================== */
static unsigned int g_arena_doc = 0;    /* dove finisce il testo del documento */

/* ! ZERO NON PUO' VOLER DIRE «NON C'E' POSTO», ED E' COSTATO UN DIFETTO CHE
 * SEMBRAVA UN'ALTRA COSA. Zero e' un OFFSET VALIDO — e' l'inizio dell'arena,
 * cioe' il primo testo della pagina. Con l'arena piena, il segno di una voce
 * di elenco riceveva offset 0 e veniva disegnato con quel testo: su Wikipedia
 * si vedeva «#Main page» sovrapposto a «Main page», e sembrava che
 * l'impaginazione disegnasse due volte. Non disegnava due volte: disegnava
 * una volta la cosa sbagliata.
 *
 * Adesso l'impossibile e' un valore che non puo' essere un offset, e chi
 * chiama salta il pezzo. Un elenco senza i segni e' meno di un elenco; un
 * elenco con dentro pezzi di un'altra frase e' peggio di niente. */
#define GENERA_NIENTE   ((unsigned int)-1)

static unsigned int genera(const char *s)
{
    unsigned int inizio = g_doc.arena_n, i = 0;

    while (s[i]) i++;
    if (g_doc.arena_n + i + 1 > ARENA_MAX) {
        g_doc.troncato = 1;         /* la barra di stato lo dira' */
        return GENERA_NIENTE;
    }

    for (i = 0; s[i]; i++) g_arena[g_doc.arena_n++] = s[i];
    g_arena[g_doc.arena_n++] = '\0';
    return inizio;
}

/* Fuori da <pre>, questi quattro sono tutti «spazio». */
static int bianco(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void parole(const char *t, unsigned int base)
{
    int i = 0;

    while (t[i]) {
        int a;

        /* ! DENTRO <pre> LO SPAZIO E' TESTO, e l'a capo pure: e' tutta la
         * ragione per cui quel tag esiste. Fuori, una sequenza di spazi e di a
         * capo vale uno spazio solo — che e' la regola dell'HTML. */
        if (g_fisso > 0) {
            if (t[i] == '\n') { g_pen_x = riga_x() + 1; a_capo(); i++; continue; }
            if (t[i] == '\r') { i++; continue; }
            if (t[i] == ' ' || t[i] == '\t') {
                int quanti = (t[i] == '\t') ? 8 : 1;

                g_pen_x += quanti * ex_larghezza_testo(font_di(&g_stile_ora), " ");
                i++;
                continue;
            }
            a = i;
            while (t[i] && t[i] != ' ' && t[i] != '\t' &&
                   t[i] != '\n' && t[i] != '\r') i++;
            parola(t + a, base + (unsigned int)a, i - a);
            continue;
        }

        /* =====================================================================
         * ! GLI A CAPO SONO SPAZI, E NON LO ERANO: qui si guardava solo ' ',
         * e l'HTML fra un tag e l'altro va a capo di continuo. Il risultato si
         * leggeva su qualunque pagina vera — «Hacker Newsnew» al posto di
         * «Hacker News new» — perche' quel nodo di testo fatto di un solo a
         * capo non avanzava la penna e diventava una parola vuota.
         *
         * ! E UNA SEQUENZA DI BIANCHI VALE UNO SPAZIO SOLO, che e' la regola
         * dell'HTML: prima ogni spazio ne aggiungeva uno, quindi il testo
         * sorgente indentato apriva buchi larghi quanto il rientro.
         *
         * ! A INIZIO RIGA NON VALE NIENTE. Senza questa riga ogni paragrafo che
         * nel sorgente comincia a capo partirebbe rientrato di uno spazio, e
         * il margine sinistro della pagina sembrerebbe storto.
         * ===================================================================== */
        if (bianco(t[i])) {
            while (bianco(t[i])) i++;
            if (g_pen_x > riga_x())
                g_pen_x += ex_larghezza_testo(font_di(&g_stile_ora), " ");
            continue;
        }
        if (!t[i]) break;
        a = i;
        while (t[i] && !bianco(t[i])) i++;
        parola(t + a, base + (unsigned int)a, i - a);
    }
}

/* Un'immagine — o il posto che le si tiene — si colloca come una parola molto
 * grande. */
static void pezzo_immagine(int k, int w, int h)
{

    if (g_pen_x + w > riga_x() + riga_w() && g_pen_x > riga_x()) a_capo();

    if (g_pez_n < PEZZI_MAX) {
        g_pez[g_pez_n].x = g_pen_x;
        g_pez[g_pez_n].y = g_pen_y;
        g_pez[g_pez_n].w = w;
        g_pez[g_pez_n].testo = 0;
        g_pez[g_pez_n].font = g_font_testo;
        g_pez[g_pez_n].colore = EX_NERO;
        g_pez[g_pez_n].h = (short)h;
        g_pez[g_pez_n].link = (short)g_link_ora;
        g_pez[g_pez_n].img = (short)k;
        g_pez[g_pez_n].ctrl = -1;
        g_pez_n++;
    }

    g_pen_x += w;

    /* ! LA RIGA CRESCE FINO ALL'IMMAGINE, altrimenti la riga dopo le passa
     * sopra: l'altezza di una riga e' quella del suo pezzo piu' alto. */
    if (h + 3 > g_riga_h) g_riga_h = h + 3;
}

static int blocco(const char *nome)
{
    static const char *const B[] = {
        "p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li",
        "pre", "blockquote", "form", "hr", "section",
        "article", "header", "footer", "nav", "aside", "main", "title", 0
    };
    int i;

    for (i = 0; B[i]; i++) {
        const char *b = B[i];
        const char *n = nome;

        while (*b && *n && *b == *n) { b++; n++; }
        if (*b == '\0' && *n == '\0') return 1;
    }
    return 0;
}

static int uguale(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

/* ! CIO' CHE NON SI VEDE NON SI IMPAGINA: dentro <script>, <style>, <head> e
 * <title> c'e' testo che non appartiene alla pagina. Senza questo, la prima
 * cosa che si legge su un sito vero e' un chilometro di JavaScript. */
static int invisibile(const char *n)
{
    return uguale(n, "script") || uguale(n, "style") || uguale(n, "head") ||
           uguale(n, "title") || uguale(n, "meta") || uguale(n, "link");
}

/* impagina_nodo e impagina_tabella si chiamano a vicenda: una tabella contiene
 * del contenuto qualunque, e quel contenuto puo' contenere un'altra tabella. */
static void impagina_nodo(int v, const CssStile *ered);

/* =============================================================================
 * IL FOGLIO PREDEFINITO — quello che il browser porta con se'
 *
 * ! I TAG CHE «CAMBIANO L'ASPETTO» SONO DIVENTATI CSS, E NON E' UN GIRO PIU'
 * LUNGO: e' cio' che li rende sovrascrivibili. Prima `h1` era grande e in
 * neretto perche' c'era un `if` nell'impaginazione, e nessuna pagina poteva
 * dire altrimenti. Adesso e' una regola come le altre, con l'origine piu'
 * bassa della cascata: la pagina che vuole un `h1` piccolo lo ottiene.
 *
 * ! ED E' ANCHE COME SONO ARRIVATI <b>, <i>, <strong> ed <em>, che prima non
 * c'erano: cinque righe qui invece di cinque casi nel motore.
 * ============================================================================= */
static const char CSS_DI_SISTEMA[] =
    "h1 { font-size: 22px; font-weight: bold }"
    "h2 { font-size: 19px; font-weight: bold }"
    "h3 { font-size: 17px; font-weight: bold }"
    "h4, h5, h6 { font-weight: bold }"
    "b, strong { font-weight: bold }"
    "i, em, cite, var { font-style: italic }"
    "a { color: #0000ee }"
    "blockquote { margin-left: 32px; margin-right: 16px }"
    "ul, ol, dd { margin-left: 28px }"
    "center { text-align: center }"
    "th { font-weight: bold; text-align: center }"

    /* ! UNA TABELLA NON EREDITA L'ALLINEAMENTO DA CHI LA CONTIENE, e questa
     * riga vale piu' di tutte le altre messe insieme su una pagina vera. Molti
     * siti — Hacker News per dirne uno che si guarda ogni giorno — chiudono
     * tutto dentro un `<center>` per centrare la TABELLA, non il testo dentro
     * le celle. Ereditando, ogni titolo e ogni riga finivano in mezzo alla
     * colonna: la pagina si leggeva, ma sembrava scritta da un ubriaco.
     *
     * ! E' LA REGOLA DEI BROWSER VERI, non una nostra invenzione: l'effetto di
     * `<center>` e di `align=center` si ferma al bordo della tabella, ed e'
     * proprio perche' quel modo di centrare una tabella e' vecchio quanto il
     * web. Chi vuole centrare una cella lo dice sulla cella, e allora vince
     * perche' la sua regola sta piu' in alto nella cascata. */
    "table, td { text-align: left }";

/* =============================================================================
 * LE TABELLE — e sono l'unico posto che vuole DUE passate
 *
 * ! LA LARGHEZZA DI UNA COLONNA NON SI SA FINCHE' NON SI E' GUARDATO OGNI
 * CONTENUTO DI QUELLA COLONNA, ed e' tutta la difficolta': il resto della
 * pagina si impagina in avanti, una parola dopo l'altra, senza tornare
 * indietro. Qui no. Prima si misura ogni cella come se avesse tutta la riga a
 * disposizione, poi si decide quanto e' larga ogni colonna, e solo allora si
 * impagina davvero.
 *
 * ! LA PRIMA PASSATA PRODUCE PEZZI CHE SI BUTTANO, e vanno buttati per
 * davvero: si segna dove arrivava `g_pez_n` e ci si torna. Lasciarli
 * vorrebbe dire disegnare due volte ogni cella, la seconda nel posto giusto e
 * la prima dove capita.
 *
 * ! E SE LE COLONNE NON CI STANNO SI RESTRINGONO IN PROPORZIONE, non si
 * lasciano sbordare: una tabella piu' larga della finestra e' la cosa che
 * rende illeggibili le pagine vere, e qui non c'e' lo scorrimento
 * orizzontale per rimediare.
 *
 * ! `colspan` E `rowspan` CI SONO, e cambiano piu' di quanto sembri: senza,
 * ogni tabella con un'intestazione che scavalca due colonne — cioe' quasi
 * tutte quelle vere — mandava fuori posto tutte le celle dopo di lei, non solo
 * quella. Una cella che scavalca prende la somma delle colonne che occupa piu'
 * gli spazi in mezzo; e chi scavalca delle RIGHE si tiene la sua colonna per i
 * giri successivi, che e' il motivo per cui serve una mappa di cio' che e' gia'
 * occupato invece di un semplice contatore di celle.
 *
 * ! L'ALTEZZA DI UNA CELLA CHE SCAVALCA SI DIVIDE FRA LE RIGHE che occupa, non
 * si scarica tutta sulla prima. Scaricarla sulla prima farebbe una riga alta e
 * due vuote sotto — il contrario di quel che si vede in una tabella vera.
 *
 * ! QUELLO CHE NON C'E', DICHIARATO: i bordi.
 * ============================================================================= */
#define TAB_COL_MAX     10
#define TAB_RIG_MAX     120
#define TAB_LIV_MAX     3       /* tabelle dentro tabelle */
#define TAB_SPAZIO      8       /* fra una colonna e l'altra */

static int g_tab_liv = 0;

/* Quante colonne (o righe) scavalca questa cella. Sempre almeno una, e con un
 * tetto: `colspan="9999"` esiste davvero sulle pagine vere, ed e' un modo di
 * dire «tutta la riga» che non deve poter allocare novemila colonne. */
static int quanto_scavalca(int nodo, const char *attr)
{
    const char  *a = html_attr(&g_doc, nodo, attr);
    unsigned int v;

    if (!a) return 1;
    v = numero(a);
    if (v < 1) return 1;
    if (v > TAB_COL_MAX) return TAB_COL_MAX;
    return (int)v;
}

static int e_riga(const char *n)  { return uguale(n, "tr"); }
static int e_cella(const char *n) { return uguale(n, "td") || uguale(n, "th"); }

/* Raccoglie le `tr` di questa tabella, saltando thead/tbody/tfoot e fermandosi
 * davanti a una tabella annidata — le sue righe sono sue. */
static void raccogli_righe(int v, int *righe, int *n)
{
    int f;

    for (f = g_doc.nodi[v].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo) {
        const char *nome;

        if (g_doc.nodi[f].tipo != HTML_ELEMENTO) continue;
        nome = html_nome(&g_doc, f);

        if (uguale(nome, "table")) continue;
        if (e_riga(nome)) { if (*n < TAB_RIG_MAX) righe[(*n)++] = f; continue; }
        raccogli_righe(f, righe, n);
    }
}

/* Impagina `nodo` dentro una colonna, e rende l'altezza che ha occupato.
 * Con `prova` a 1 i pezzi si buttano e si rende invece la larghezza usata. */
static int impagina_in_colonna(int nodo, const CssStile *ered,
                               int x, int y, int w, int prova, int *alt)
{
    int era_sx = g_marg_sx, era_dx = g_marg_dx;
    int era_px = g_pen_x, era_py = g_pen_y;
    int era_rh = g_riga_h, era_rp = g_riga_primo;
    int primo  = g_pez_n, primo_sf = g_sfondi_n;
    int era_mis = g_misura;
    int larga  = 0, i, f;

    g_misura = prova;

    g_marg_sx = x - area_x();
    g_marg_dx = (area_x() + area_w()) - (x + w);
    if (g_marg_sx < 0) g_marg_sx = 0;
    if (g_marg_dx < 0) g_marg_dx = 0;

    g_pen_x      = riga_x();
    g_pen_y      = y;
    g_riga_h     = alt_riga_f(font_di(ered));
    g_riga_primo = g_pez_n;

    /* I figli della cella, non la cella: `td` non e' un blocco, e trattarlo da
     * tale aggiungerebbe uno stacco dentro ogni casella. */
    for (f = g_doc.nodi[nodo].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo)
        impagina_nodo(f, ered);
    a_capo();

    *alt = g_pen_y - y;
    if (*alt < g_riga_h) *alt = g_riga_h;

    for (i = primo; i < g_pez_n; i++) {
        int destra = g_pez[i].x + g_pez[i].w - x;

        if (destra > larga) larga = destra;
    }

    if (prova) { g_pez_n = primo; g_sfondi_n = primo_sf; }
    g_misura = era_mis;

    g_marg_sx = era_sx; g_marg_dx = era_dx;
    g_pen_x = era_px;   g_pen_y = era_py;
    g_riga_h = era_rh;  g_riga_primo = era_rp;
    return larga;
}

static void impagina_tabella(int v, const CssStile *mio)
{
    int      righe[TAB_RIG_MAX], n_righe = 0;
    int      largh[TAB_COL_MAX];
    int      resta[TAB_COL_MAX], debito[TAB_COL_MAX];
    int      n_col = 0, r, c, somma = 0, disp, alt;
    int      x0, y0;

    raccogli_righe(v, righe, &n_righe);
    if (n_righe == 0 || g_tab_liv >= TAB_LIV_MAX) {
        int f;

        /* Niente righe, o troppo annidata: si impagina come un blocco
         * qualunque, che e' cio' che si faceva prima delle tabelle. */
        for (f = g_doc.nodi[v].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo)
            impagina_nodo(f, mio);
        return;
    }

    g_tab_liv++;
    for (c = 0; c < TAB_COL_MAX; c++) largh[c] = 0;

    /* --- prima passata: quanto vorrebbe essere larga ogni colonna --------- */
    for (r = 0; r < n_righe; r++) {
        int f;

        c = 0;
        for (f = g_doc.nodi[righe[r]].primo_figlio; f >= 0;
             f = g_doc.nodi[f].prossimo) {
            CssStile sc;
            int      w, a, sp;

            if (g_doc.nodi[f].tipo != HTML_ELEMENTO) continue;
            if (!e_cella(html_nome(&g_doc, f))) continue;
            if (c >= TAB_COL_MAX) break;

            css_calcola(&g_css, &g_doc, f, mio, &sc);
            suggerimenti(f, &sc);
            w = impagina_in_colonna(f, &sc, riga_x(), 0, riga_w(), 1, &a);

            sp = quanto_scavalca(f, "colspan");
            if (c + sp > TAB_COL_MAX) sp = TAB_COL_MAX - c;

            /* ! UNA CELLA CHE SCAVALCA NON ALLARGA UNA COLONNA SOLA. La sua
             * larghezza si spalma sulle colonne che occupa, e solo per la
             * parte che quelle non hanno gia': altrimenti un titolo lungo su
             * due colonne le renderebbe larghe il doppio del necessario. */
            if (sp <= 1) {
                if (w > largh[c]) largh[c] = w;
            } else {
                int k, gia = 0;

                for (k = 0; k < sp; k++) gia += largh[c + k];
                gia += (sp - 1) * TAB_SPAZIO;
                if (w > gia) {
                    int manca = (w - gia + sp - 1) / sp;

                    for (k = 0; k < sp; k++) largh[c + k] += manca;
                }
            }
            c += sp;
        }
        if (c > n_col) n_col = c;
    }

    if (n_col == 0) { g_tab_liv--; return; }

    /* --- la distribuzione ------------------------------------------------- */
    for (c = 0; c < n_col; c++) {
        if (largh[c] < 12) largh[c] = 12;
        somma += largh[c];
    }
    disp = riga_w() - (n_col - 1) * TAB_SPAZIO;
    if (disp < n_col * 12) disp = n_col * 12;

    if (somma > disp) {
        /* Si stringe in proporzione: chi voleva piu' spazio ne perde di piu'. */
        int resto = disp;

        for (c = 0; c < n_col; c++) {
            int w = (c == n_col - 1) ? resto : (int)((long)largh[c] * disp / somma);

            if (w < 12) w = 12;
            largh[c] = w;
            resto -= w;
            if (resto < 0) resto = 0;
        }
    }

    /* --- seconda passata: si impagina per davvero ------------------------- */
    a_capo();
    y0 = g_pen_y;

    /* ! LA MAPPA DI CIO' CHE E' GIA' OCCUPATO, ed e' tutto cio' che serve per
     * `rowspan`. `resta[c]` dice per quanti giri ancora la colonna c e' presa
     * da una cella cominciata sopra; `debito[c]` quanta altezza quella cella
     * deve ancora coprire, cosi' le righe sotto non si schiacciano. */
    {
        int k;

        for (k = 0; k < TAB_COL_MAX; k++) { resta[k] = 0; debito[k] = 0; }
    }

    for (r = 0; r < n_righe; r++) {
        int f, alt_riga_tab = 0;

        /* Quel che una cella cominciata prima pretende da QUESTA riga. */
        for (c = 0; c < n_col; c++)
            if (resta[c] > 0) {
                int q = (debito[c] + resta[c] - 1) / resta[c];

                if (q > alt_riga_tab) alt_riga_tab = q;
            }

        x0 = riga_x();
        c  = 0;
        for (f = g_doc.nodi[righe[r]].primo_figlio; f >= 0;
             f = g_doc.nodi[f].prossimo) {
            CssStile sc;
            int      sp, rp, largh_cella, k;

            if (g_doc.nodi[f].tipo != HTML_ELEMENTO) continue;
            if (!e_cella(html_nome(&g_doc, f))) continue;

            /* ! LE COLONNE GIA' PRESE SI SALTANO, e si salta anche il loro
             * spazio: e' l'unica cosa che tiene incolonnato quel che viene
             * dopo una cella che scavalca delle righe. */
            while (c < n_col && resta[c] > 0) {
                x0 += largh[c] + TAB_SPAZIO;
                c++;
            }
            if (c >= n_col) break;

            css_calcola(&g_css, &g_doc, f, mio, &sc);
            suggerimenti(f, &sc);

            sp = quanto_scavalca(f, "colspan");
            if (c + sp > n_col) sp = n_col - c;
            if (sp < 1) sp = 1;
            rp = quanto_scavalca(f, "rowspan");

            largh_cella = 0;
            for (k = 0; k < sp; k++) largh_cella += largh[c + k];
            largh_cella += (sp - 1) * TAB_SPAZIO;

            /* Lo sfondo della cella si segna PRIMA, con l'altezza rimessa a
             * posto quando la riga e' finita: e' lo stesso giro dei blocchi. */
            if (sc.sfondo != CSS_NIENTE && g_sfondi_n < SFONDI_MAX) {
                g_sfondi[g_sfondi_n].x = x0;
                g_sfondi[g_sfondi_n].y = y0;
                g_sfondi[g_sfondi_n].w = largh_cella;
                g_sfondi[g_sfondi_n].h = 0;
                g_sfondi[g_sfondi_n].colore = sc.sfondo;
                g_sfondi_n++;
            }

            impagina_in_colonna(f, &sc, x0, y0, largh_cella, 0, &alt);

            if (rp <= 1) {
                if (alt > alt_riga_tab) alt_riga_tab = alt;
            } else {
                /* Si tiene la colonna per i giri successivi, e ci si porta
                 * dietro l'altezza che resta da coprire. */
                int primo = (alt + rp - 1) / rp;

                if (primo > alt_riga_tab) alt_riga_tab = primo;

                /* ! SI SEGNA `rp`, NON `rp - 1`, e la differenza e' un giro
                 * intero: in fondo a QUESTA riga si decrementa insieme a tutte
                 * le altre, quindi partire da rp-1 lasciava la colonna libera
                 * un giro troppo presto. Il sintomo era la cella dell'ultima
                 * riga che tornava nella colonna della cella che scavalca, e
                 * ci si scriveva sopra.
                 *
                 * Il debito e' l'altezza INTERA: quel che ogni riga copre si
                 * sottrae in fondo alla riga, sempre nello stesso posto. */
                for (k = 0; k < sp; k++) {
                    resta[c + k]  = rp;
                    debito[c + k] = alt;
                }
            }

            x0 += largh_cella + TAB_SPAZIO;
            c += sp;
        }

        /* Gli sfondi di questa riga prendono adesso la loro altezza vera. */
        {
            int k;

            for (k = g_sfondi_n - 1; k >= 0; k--) {
                if (g_sfondi[k].y != y0 || g_sfondi[k].h != 0) continue;
                g_sfondi[k].h = alt_riga_tab;
            }
        }

        /* Le celle che scavalcano hanno consumato un giro. */
        for (c = 0; c < n_col; c++)
            if (resta[c] > 0) {
                debito[c] -= alt_riga_tab;
                if (debito[c] < 0) debito[c] = 0;
                resta[c]--;
            }

        y0 += alt_riga_tab;
    }

    g_pen_y      = y0;
    g_pen_x      = riga_x();
    g_riga_h     = alt_riga_f(font_di(mio));
    g_riga_primo = g_pez_n;
    g_tab_liv--;
}

/* Il testo che sta DENTRO un elemento, messo in fila.
 *
 * ! UN <button> NON HA `value`, HA UN CONTENUTO, e la stessa cosa vale per
 * <option> e <textarea>. Con i controlli si scende nei figli una volta sola,
 * qui, e poi non ci si scende piu': se il contenuto finisse anche nel flusso
 * della pagina, l'etichetta di un pulsante comparirebbe due volte — una dentro
 * il pulsante e una accanto. */
static void testo_dentro(int v, char *out, unsigned int max)
{
    unsigned int n = 0;
    int          f;

    out[0] = '\0';
    if (v < 0) return;

    for (f = g_doc.nodi[v].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo) {
        if (g_doc.nodi[f].tipo == HTML_TESTO) {
            const char *t = g_doc.arena + g_doc.nodi[f].testo;

            while (*t && n < max - 1) {
                /* Gli spazi multipli diventano uno solo, come nel resto. */
                if (*t == '\n' || *t == '\r' || *t == '\t') {
                    if (n > 0 && out[n - 1] != ' ') out[n++] = ' ';
                } else {
                    out[n++] = *t;
                }
                t++;
            }
        } else {
            char dentro[CTRL_VAL_MAX];
            unsigned int k = 0;

            testo_dentro(f, dentro, sizeof(dentro));
            while (dentro[k] && n < max - 1) out[n++] = dentro[k++];
        }
        if (n >= max - 1) break;
    }

    /* Via gli spazi in testa e in coda: l'HTML ne mette sempre. */
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
    if (out[0] == ' ') {
        unsigned int i = 0;
        while (out[i] == ' ') i++;
        for (n = 0; out[i]; i++) out[n++] = out[i];
        out[n] = '\0';
    }
}

static void impagina_nodo(int v, const CssStile *ered)
{
    int f;

    if (v < 0) return;

    /* ! UN NODO DI TESTO NON HA UNO STILE SUO: prende quello del padre, che e'
     * esattamente cio' che `ered` porta. Calcolargliene uno vorrebbe dire far
     * corrispondere dei selettori a qualcosa che selettore non ha. */
    if (g_doc.nodi[v].tipo == HTML_TESTO) {
        g_stile_ora = *ered;
        parole(html_testo(&g_doc, v), g_doc.nodi[v].testo);
        return;
    }

    {
        const char *nome = html_nome(&g_doc, v);
        int         era_link = g_link_ora;
        int         era_modulo = g_mod_ora;
        int         era_sx = g_marg_sx, era_dx = g_marg_dx;
        int         sfondo_mio = -1;
        CssStile    mio;
        int         e_blocco;

        if (invisibile(nome)) return;

        css_calcola(&g_css, &g_doc, v, ered, &mio);
        suggerimenti(v, &mio);

        /* ! `display: none` TOGLIE ANCHE I FIGLI, e va fatto qui prima di
         * qualunque altra cosa: e' cosi' che i siti veri nascondono i menu che
         * si aprono col mouse. Impaginarli lo stesso vorrebbe dire una pagina
         * piena di voci che non dovrebbero vedersi. */
        if (mio.display == CSS_DISPLAY_NIENTE) return;

        g_stile_ora = mio;

        /* ! LA TABELLA HA UNA STRADA SUA, e va presa PRIMA della logica dei
         * blocchi: quella impagina i figli uno dietro l'altro, che e'
         * esattamente cio' che una tabella non deve fare. */
        if (uguale(nome, "table")) {
            int sf = -1;

            spazio_blocco(0);

            /* ! LO SFONDO DELLA TABELLA E' SUO, NON DELLE CELLE, e va segnato
             * qui: la strada delle tabelle salta tutta la logica dei blocchi,
             * e con lei il riquadro di sfondo. E' il caso della barra
             * arancione di Hacker News, che e' un `bgcolor` sulla <table> —
             * non sulle celle. L'altezza si rimette quando la tabella e'
             * finita, come per i blocchi. */
            if (mio.sfondo != CSS_NIENTE && g_sfondi_n < SFONDI_MAX) {
                sf = g_sfondi_n++;
                g_sfondi[sf].x = riga_x();
                g_sfondi[sf].y = g_pen_y;
                g_sfondi[sf].w = riga_w();
                g_sfondi[sf].h = 0;
                g_sfondi[sf].colore = mio.sfondo;
            }

            impagina_tabella(v, &mio);

            if (sf >= 0) {
                g_sfondi[sf].h = g_pen_y - g_sfondi[sf].y;
                if (g_sfondi[sf].h < 1) g_sfondi[sf].h = 1;
            }

            g_link_ora = era_link;
            spazio_blocco(2);
            return;
        }

        if (uguale(nome, "br")) { g_pen_x = riga_x() + 1; a_capo(); return; }

        /* ! <hr> E' UNA RIGA, non uno stacco piu' grande: finora era solo un
         * blocco vuoto, cioe' un po' d'aria in mezzo alla pagina — e chi
         * scrive <hr> vuole vedere il segno che separa. Si disegna come uno
         * sfondo alto due pixel, perche' e' esattamente cio' che e'. */
        if (uguale(nome, "hr")) {
            spazio_blocco(0);
            if (g_sfondi_n < SFONDI_MAX) {
                g_sfondi[g_sfondi_n].x = riga_x();
                g_sfondi[g_sfondi_n].y = g_pen_y + 2;
                g_sfondi[g_sfondi_n].w = riga_w();
                g_sfondi[g_sfondi_n].h = 2;
                g_sfondi[g_sfondi_n].colore = EX_OMBRA;
                g_sfondi_n++;
            }
            g_pen_y += 6;
            g_riga_h = alt_riga_f(font_di(&mio));
            g_riga_primo = g_pez_n;
            spazio_blocco(2);
            return;
        }

        /* =====================================================================
         * I CONTROLLI DI UN MODULO
         *
         * ! LA MISURA VIENE DALL'ATTRIBUTO `size` QUANDO C'E', e altrimenti da
         * un valore ragionevole: venti caratteri e' quello che quasi tutti i
         * browser hanno usato per trent'anni, e una casella troppo stretta si
         * nota molto piu' di una troppo larga.
         * ===================================================================== */
        if (uguale(nome, "input") || uguale(nome, "button") ||
            uguale(nome, "select") || uguale(nome, "textarea")) {
            const char *tipo = html_attr(&g_doc, v, "type");
            const char *val  = html_attr(&g_doc, v, "value");
            const char *sz   = html_attr(&g_doc, v, "size");
            int         t    = CTRL_TESTO;
            int         w, h;

            if (uguale(nome, "button"))        t = CTRL_PULSANTE;
            else if (uguale(nome, "select"))   t = CTRL_SCELTA;
            else if (uguale(nome, "textarea")) t = CTRL_AREA;
            else if (tipo) {
                if (uguale(tipo, "submit") || uguale(tipo, "reset") ||
                    uguale(tipo, "button") || uguale(tipo, "image"))
                    t = CTRL_PULSANTE;
                else if (uguale(tipo, "checkbox")) t = CTRL_SPUNTA;
                else if (uguale(tipo, "radio"))    t = CTRL_RADIO;
                else if (uguale(tipo, "hidden"))   return;   /* non si vede */
            }

            if (g_ctrl_n >= CTRL_MAX || g_pez_n >= PEZZI_MAX) return;

            {
                Ctrl *c = &g_ctrl[g_ctrl_n];
                int   i = 0;

                const char *nm = html_attr(&g_doc, v, "name");

                /* ! QUEL CHE L'UTENTE HA SCRITTO SOPRAVVIVE ALLA
                 * REIMPAGINAZIONE. L'albero non cambia fra un'impaginazione e
                 * l'altra, quindi i controlli escono sempre nello stesso
                 * ordine e lo slot `i` e' sempre dello stesso nodo: se e'
                 * ancora suo, il valore digitato e la spunta restano dov'erano.
                 *
                 * Senza questo, un'immagine che arriva mentre si compila un
                 * modulo cancellerebbe il campo sotto le dita — e il colpevole
                 * sembrerebbe la tastiera, non l'impaginazione. */
                int   suo = (c->nodo == v);
                short opz_prima = c->opz_ora;
                char  scritto[CTRL_VAL_MAX];

                scritto[0] = '\0';
                if (suo) {
                    int q = 0;

                    while (c->valore[q] && q < CTRL_VAL_MAX - 1) {
                        scritto[q] = c->valore[q]; q++;
                    }
                    scritto[q] = '\0';
                }

                c->tipo    = (unsigned char)t;
                c->segreto = (unsigned char)(tipo && uguale(tipo, "password"));
                if (!suo)
                    c->acceso = (unsigned char)(html_attr(&g_doc, v, "checked") != 0);
                c->nodo    = v;
                c->valore[0] = '\0';

                /* ! IL `name` SERVE AI RADIO PRIMA CHE AI MODULI. Due gruppi di
                 * scelte nella stessa pagina sono due gruppi solo se si sa a
                 * quale nome appartiene ognuna: senza, accenderne una spegne
                 * anche quelle dell'altro gruppo. */
                c->modulo  = (short)g_mod_ora;
                c->opz_primo = -1;
                c->opz_n     = 0;
                c->opz_ora   = 0;
                c->nome[0] = '\0';
                if (nm) {
                    int q = 0;

                    while (nm[q] && q < CTRL_NOME_MAX - 1) { c->nome[q] = nm[q]; q++; }
                    c->nome[q] = '\0';
                }

                /* Il testo dentro: `value` per gli input, il contenuto per un
                 * <button>. Il contenuto sta nei figli, e qui non si scende:
                 * si prende `value`, e senza quello un'etichetta onesta. */
                if (val) {
                    while (val[i] && i < CTRL_VAL_MAX - 1) { c->valore[i] = val[i]; i++; }
                    c->valore[i] = '\0';
                } else if (t == CTRL_SCELTA) {
                    /* ! LE OPZIONI SI RACCOLGONO UNA PER UNA, e non si prende
                     * il testo di tutto il <select>: quello darebbe le voci
                     * incollate in una riga sola. Ognuna e' una scelta
                     * possibile, e l'utente deve poterle avere tutte. */
                    int f2;

                    c->opz_primo = (short)g_opz_n;
                    for (f2 = g_doc.nodi[v].primo_figlio; f2 >= 0;
                         f2 = g_doc.nodi[f2].prossimo) {
                        if (g_doc.nodi[f2].tipo != HTML_ELEMENTO) continue;
                        if (!uguale(html_nome(&g_doc, f2), "option")) continue;
                        if (g_opz_n >= OPZ_MAX) break;

                        testo_dentro(f2, g_opz[g_opz_n], CTRL_VAL_MAX);
                        if (html_attr(&g_doc, f2, "selected"))
                            c->opz_ora = (short)(g_opz_n - c->opz_primo);
                        g_opz_n++;
                        c->opz_n++;
                    }

                    if (c->opz_n > 0) {
                        int q = 0;
                        const char *o = g_opz[c->opz_primo + c->opz_ora];

                        while (o[q] && q < CTRL_VAL_MAX - 1) { c->valore[q] = o[q]; q++; }
                        c->valore[q] = '\0';
                    }
                } else if (t != CTRL_TESTO) {
                    /* <button> e <textarea> portano dentro il proprio testo. */
                    testo_dentro(v, c->valore, CTRL_VAL_MAX);
                }
                if (val == 0 && t == CTRL_TESTO) c->valore[0] = '\0';

                if (t == CTRL_PULSANTE && c->valore[0] == '\0') {
                    const char *d = tipo && uguale(tipo, "reset") ? "Azzera" : "Invia";
                    i = 0;
                    while (d[i] && i < CTRL_VAL_MAX - 1) { c->valore[i] = d[i]; i++; }
                    c->valore[i] = '\0';
                }

                /* ! E SOLO ADESSO SI RIMETTE QUEL CHE L'UTENTE AVEVA SCRITTO,
                 * perche' solo adesso si conosce il tipo. Vale per le caselle
                 * e per le aree, che sono le uniche in cui si scrive: il testo
                 * di un pulsante e le opzioni di una scelta vengono dalla
                 * pagina e si rifanno ogni volta, com'e' giusto. Di una scelta
                 * si tiene invece la RIGA SCELTA, che e' quel che l'utente ha
                 * deciso. */
                if (suo && (t == CTRL_TESTO || t == CTRL_AREA)) {
                    int q = 0;

                    while (scritto[q] && q < CTRL_VAL_MAX - 1) {
                        c->valore[q] = scritto[q]; q++;
                    }
                    c->valore[q] = '\0';
                } else if (suo && t == CTRL_SCELTA && c->opz_n > 0) {
                    int q = 0;
                    const char *o;

                    if (opz_prima >= 0 && opz_prima < c->opz_n)
                        c->opz_ora = opz_prima;
                    o = g_opz[c->opz_primo + c->opz_ora];
                    while (o[q] && q < CTRL_VAL_MAX - 1) { c->valore[q] = o[q]; q++; }
                    c->valore[q] = '\0';
                }

                /* ! IL CURSORE SI ANCORA QUANDO IL VALORE E' DEFINITIVO, non
                 * prima: sopra il testo puo' ancora cambiare. Se lo slot era
                 * gia' suo si tiene dov'era — reimpaginare mentre si scrive
                 * non deve spostare il punto in cui si sta scrivendo — e se e'
                 * nuovo si mette in fondo. */
                {
                    int q = 0;

                    while (c->valore[q]) q++;
                    if (!suo || c->cur > (short)q) c->cur = (short)q;
                    if (c->cur < 0) c->cur = 0;
                }
            }

            switch (t) {
            case CTRL_SPUNTA:
            case CTRL_RADIO:    w = 14; h = 14; break;
            case CTRL_PULSANTE: {
                int n_car = 0;
                while (g_ctrl[g_ctrl_n].valore[n_car]) n_car++;
                w = 16 + n_car * 8;
                if (w < 56) w = 56;
                h = 22;
                break;
            }
            case CTRL_AREA:     w = 320; h = 88; break;
            case CTRL_SCELTA:   w = 160; h = 22; break;
            default: {
                int car = sz ? atoi(sz) : 20;
                if (car < 2)  car = 2;
                if (car > 80) car = 80;
                w = car * 8 + 8;
                h = 22;
                break;
            }
            }

            if (w > riga_w()) w = riga_w();
            if (g_pen_x + w > riga_x() + riga_w() && g_pen_x > riga_x()) a_capo();

            g_pez[g_pez_n].x = g_pen_x;
            g_pez[g_pez_n].y = g_pen_y;
            g_pez[g_pez_n].w = w;
            g_pez[g_pez_n].testo = 0;
            g_pez[g_pez_n].font = g_font_testo;
            g_pez[g_pez_n].colore = EX_NERO;
            g_pez[g_pez_n].h = (short)h;
            g_pez[g_pez_n].link = -1;
            g_pez[g_pez_n].img = -1;
            g_pez[g_pez_n].ctrl = (short)g_ctrl_n;
            g_pez_n++;
            g_ctrl_n++;

            g_pen_x += w + 4;
            if (h + 4 > g_riga_h) g_riga_h = h + 4;
            return;
        }

        if (uguale(nome, "img")) {
            const char *src = html_attr(&g_doc, v, "src");
            const char *alt;
            int         k = (src && src[0]) ? imm_indice(v, src) : -1;

            if (k >= 0 && g_imm[k].px) {
                pezzo_immagine(k, (int)g_imm[k].w, (int)g_imm[k].h);
                return;
            }

            /* =================================================================
             * ! SE LA PAGINA DICE QUANTO E' GRANDE, IL POSTO SI TIENE SUBITO.
             *
             * E' la differenza fra una pagina che si riassesta a ogni immagine
             * e una che si riempie: con `width` e `height` sull'<img> la
             * misura finale si sa PRIMA di aver scaricato un solo byte, quindi
             * l'impaginazione e' gia' quella definitiva. Quando l'immagine
             * arriva non si sposta niente — e infatti non si reimpagina, si
             * ridisegna soltanto.
             *
             * ! ED E' TUTTA LA LENTEZZA CHE RESTAVA. Reimpaginare un documento
             * di ventiquattromila pezzi per ognuna delle nove immagini di una
             * voce di Wikipedia costa piu' dello scaricarle. Chi dichiara le
             * misure — e i siti seri le dichiarano, proprio per questo — non
             * lo paga piu'.
             * ================================================================= */
            if (k >= 0 && g_imm[k].stato != 2 &&
                g_imm[k].dich_w && g_imm[k].dich_h) {
                unsigned int rw, rh;

                misura(&g_imm[k], g_imm[k].dich_w, g_imm[k].dich_h, &rw, &rh);
                if (rw && rh) {
                    g_imm[k].ris_w = rw;
                    g_imm[k].ris_h = rh;
                    pezzo_immagine(k, (int)rw, (int)rh);
                    return;
                }
            }

            /* ! FINCHE' L'IMMAGINE NON C'E' SI LEGGE IL SUO `alt`, ed e'
             * esattamente il motivo per cui quell'attributo esiste. Il valore
             * sta gia' nell'arena del documento, quindi si impagina con le
             * stesse parole di tutto il resto. */
            alt = html_attr(&g_doc, v, "alt");
            if (alt && alt[0])
                parole(alt, (unsigned int)(alt - g_doc.arena));
            return;
        }

        /* L'elenco dei blocchi resta la regola di base; `display` la
         * sovrascrive nei due versi, che e' a cosa serve. */
        e_blocco = blocco(nome);
        if (mio.display == CSS_DISPLAY_BLOCCO) e_blocco = 1;
        if (mio.display == CSS_DISPLAY_INLINE) e_blocco = 0;

        if (e_blocco) {
            spazio_blocco(0);

            /* ! I RIENTRI SI SOMMANO A QUELLI DI FUORI: un blocco dentro un
             * altro rientra due volte, ed e' cosi' che si vedono le citazioni
             * annidate. Valgono solo sui blocchi — un margine su un pezzo di
             * testo in linea non ha un lato a cui attaccarsi. */
            if (mio.margine[3] != CSS_MISURA_NO) g_marg_sx += mio.margine[3];
            if (mio.margine[1] != CSS_MISURA_NO) g_marg_dx += mio.margine[1];
            if (g_marg_sx < 0) g_marg_sx = 0;
            if (g_marg_dx < 0) g_marg_dx = 0;

            g_pen_x = riga_x();
            g_riga_primo = g_pez_n;

            if (mio.sfondo != CSS_NIENTE && g_sfondi_n < SFONDI_MAX) {
                sfondo_mio = g_sfondi_n++;
                g_sfondi[sfondo_mio].x = riga_x();
                g_sfondi[sfondo_mio].y = g_pen_y;
                g_sfondi[sfondo_mio].w = riga_w();
                g_sfondi[sfondo_mio].h = 0;
                g_sfondi[sfondo_mio].colore = mio.sfondo;
            }
        }

        /* ! IL SEGNO DI UNA VOCE DIPENDE DALLA LISTA CHE LA CONTIENE, e la
         * lista si trova risalendo: <li> non sa da solo se e' puntato o
         * numerato. Per <ol> serve anche la POSIZIONE, cioe' quanti <li> lo
         * precedono fra i fratelli — e si contano li', non con un contatore
         * globale, o due liste annidate si darebbero i numeri a vicenda. */
        if (uguale(nome, "li")) {
            int su = g_doc.nodi[v].padre;
            int numerata = 0;

            while (su >= 0) {
                const char *n = html_nome(&g_doc, su);

                if (uguale(n, "ol")) { numerata = 1; break; }
                if (uguale(n, "ul")) break;
                su = g_doc.nodi[su].padre;
            }

            if (numerata) {
                int  quanti = 1, f2;
                char seg[16];
                int  q = 0, cifre[8], nc = 0;

                for (f2 = g_doc.nodi[su].primo_figlio; f2 >= 0 && f2 != v;
                     f2 = g_doc.nodi[f2].prossimo)
                    if (g_doc.nodi[f2].tipo == HTML_ELEMENTO &&
                        uguale(html_nome(&g_doc, f2), "li")) quanti++;

                while (quanti > 0) { cifre[nc++] = quanti % 10; quanti /= 10; }
                while (nc > 0) seg[q++] = (char)('0' + cifre[--nc]);
                seg[q++] = '.';
                seg[q] = '\0';
                {
                    unsigned int off = genera(seg);

                    if (off != GENERA_NIENTE) parola(seg, off, q);
                }
            } else {
                /* Un pallino, non un asterisco: e' il segno che ci si aspetta,
                 * e il carattere c'e' in tutte le facce Liberation. */
                {
                    unsigned int off = genera("-");

                    if (off != GENERA_NIENTE) parola("-", off, 1);
                }
            }
            g_pen_x += ex_larghezza_testo(font_di(&mio), " ");
        }

        /* ! IL MODULO SI APRE QUI E SI CHIUDE DOPO I FIGLI, come un
         * collegamento: i controlli dentro ci finiscono per posizione, che e'
         * l'unica cosa che l'HTML garantisce. (L'attributo `form` che permette
         * a un campo di stare fuori dal suo modulo esiste, ed e' rarissimo:
         * dichiarato fuori.) */
        if (uguale(nome, "form") && g_mod_n < MODULI_MAX) {
            const char *az = html_attr(&g_doc, v, "action");
            const char *me = html_attr(&g_doc, v, "method");
            int q = 0;

            g_mod[g_mod_n].post = (me && (uguale(me, "post") || uguale(me, "POST")));
            if (az) {
                while (az[q] && q < AZIONE_MAX - 1) { g_mod[g_mod_n].azione[q] = az[q]; q++; }
            }
            g_mod[g_mod_n].azione[q] = '\0';
            g_mod_ora = g_mod_n++;
        }

        if (uguale(nome, "a")) {
            const char *h = html_attr(&g_doc, v, "href");

            if (h && h[0] && g_link_n < LINK_MAX) {
                unsigned int k = 0;

                /* ! SE L'ARENA E' PIENA IL LINK NON SI SCRIVE A META'. Un
                 * indirizzo troncato porta da un'altra parte, e «da un'altra
                 * parte» in un browser vuol dire una pagina sbagliata senza
                 * un errore. Si smette di raccoglierli e basta. */
                while (h[k]) k++;
                if (g_link_usati + k + 1 <= LINK_ARENA) {
                    g_link_off[g_link_n] = g_link_usati;
                    for (k = 0; h[k]; k++)
                        g_link_arena[g_link_usati + k] = h[k];
                    g_link_arena[g_link_usati + k] = '\0';
                    g_link_usati += k + 1;
                    g_link_ora = g_link_n++;
                }
            }
        }

        if (uguale(nome, "pre") || uguale(nome, "code") ||
            uguale(nome, "tt")  || uguale(nome, "kbd") ||
            uguale(nome, "samp")) g_fisso++;

        /* ! I MARGINI DI UN ELEMENTO IN LINEA SPOSTANO LA PENNA, NON IL
         * BLOCCO. Su un blocco un margine e' un rientro del lato — e quello si
         * fa piu' su, con g_marg_sx e g_marg_dx. Su uno <span> non c'e' nessun
         * lato a cui attaccarsi: il margine e' spazio orizzontale prima e dopo
         * il testo, ed e' proprio cosi' che i siti separano le voci di un
         * menu. Senza, quelle voci si toccano e sembrano una parola sola.
         *
         * ! I MARGINI VERTICALI IN LINEA NON ESISTONO, e non e' una
         * semplificazione nostra: e' la regola del CSS. Un margine sopra e
         * sotto uno <span> non sposta niente. */
        if (!e_blocco && mio.margine[3] != CSS_MISURA_NO && mio.margine[3] > 0)
            g_pen_x += mio.margine[3];

        for (f = g_doc.nodi[v].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo) {
            impagina_nodo(f, &mio);
            g_stile_ora = mio;      /* i figli l'hanno cambiato: si rimette */
        }

        if (!e_blocco && mio.margine[1] != CSS_MISURA_NO && mio.margine[1] > 0)
            g_pen_x += mio.margine[1];

        if (uguale(nome, "pre") || uguale(nome, "code") ||
            uguale(nome, "tt")  || uguale(nome, "kbd") ||
            uguale(nome, "samp")) g_fisso--;

        g_link_ora = era_link;
        if (uguale(nome, "form")) g_mod_ora = era_modulo;

        if (e_blocco) {
            a_capo();
            if (sfondo_mio >= 0) {
                int fine = g_pen_y + ((g_pen_x > riga_x()) ? g_riga_h : 0);

                g_sfondi[sfondo_mio].h = fine - g_sfondi[sfondo_mio].y;
                if (g_sfondi[sfondo_mio].h < 1) g_sfondi[sfondo_mio].h = 1;
            }
            g_marg_sx = era_sx;
            g_marg_dx = era_dx;
            g_pen_x = riga_x();
            g_riga_primo = g_pez_n;
            spazio_blocco(2);
        }
    }
}

static void impagina(void)
{
    g_pez_n = 0;
    g_link_n = 0;
    g_link_usati = 0;
    g_mod_n = 0;
    g_mod_ora = -1;

    /* =====================================================================
     * ! I CONTROLLI SONO UN PRODOTTO DELL'IMPAGINAZIONE, come i pezzi e i
     * collegamenti, e per molto tempo sono stati l'unico che non si
     * azzerava qui. Ogni `impagina()` ne accodava una copia nuova senza
     * buttare le vecchie: dodici reimpaginazioni di una pagina con cinque
     * controlli ne facevano sessanta, e a CTRL_MAX (64) `impagina_nodo`
     * cominciava a RINUNCIARE — non solo al controllo, ma a tutto il
     * sottoalbero sotto di lui.
     *
     * ! E IL SINTOMO NON SOMIGLIAVA ALLA CAUSA: sparivano pezzi di pagina
     * lontani dai moduli, e sparivano solo sulle pagine con molte immagini
     * — cioe' quelle che si reimpaginano tante volte. Si e' visto
     * confrontando due build sulla stessa voce di Wikipedia: quella che
     * reimpagina di meno mostrava PIU' contenuto, che e' esattamente il
     * contrario di quello che ci si aspetta da un'ottimizzazione.
     * ===================================================================== */
    g_ctrl_n = 0;
    g_fisso = 0;
    g_doc.arena_n = g_arena_doc;    /* si butta il testo generato dal giro prima */
    g_marg_sx = g_marg_dx = 0;
    g_riga_primo = 0;
    g_sfondi_n = 0;
    g_pen_x = area_x();
    g_pen_y = area_y();
    g_link_ora = -1;
    css_stile_vuoto(&g_stile_ora);
    g_riga_h = alt_riga_f(g_font_testo);

    {
        CssStile radice;

        css_stile_vuoto(&radice);
        impagina_nodo(g_doc.radice, &radice);
    }
    a_capo();

    g_altezza = g_pen_y - area_y() + g_riga_h;
    if (g_altezza < 1) g_altezza = 1;
}

/* =============================================================================
 * I SUGGERIMENTI DI PRESENTAZIONE DELL'HTML VECCHIO
 *
 * ! MEZZO WEB SCRIVE ANCORA I COLORI NEGLI ATTRIBUTI, e non nei fogli di
 * stile: `<table bgcolor="#ff6600">` e' la barra arancione di Hacker News, e
 * `<td align="right">` e' come si incolonnavano i numeri prima del CSS. Sono
 * chiamati «suggerimenti di presentazione» e stanno al gradino PIU' BASSO
 * della cascata: valgono solo dove il foglio di stile non ha detto niente.
 *
 * ! ED E' PROPRIO PERCHE' STANNO IN FONDO CHE SI APPLICANO DOPO css_calcola e
 * solo sui campi rimasti vuoti. Applicarli prima — o sopra — vorrebbe dire che
 * un `bgcolor` batte una regola CSS, che e' il contrario di quello che deve
 * succedere: una pagina moderna che ha ereditato un vecchio attributo si
 * vedrebbe con i colori di vent'anni fa.
 * ========================================================================== */
static void suggerimenti(int v, CssStile *st)
{
    const char *a;

    a = html_attr(&g_doc, v, "bgcolor");
    if (a && a[0] && st->sfondo == CSS_NIENTE) {
        unsigned int c;
        unsigned int n = 0;

        while (a[n]) n++;
        if (css_colore(a, n, &c)) st->sfondo = c;
    }

    /* `text` sta su <body> e su <font>, e il colore SI EREDITA: qui non c'e'
     * modo di sapere se il valore che c'e' viene da una regola o dal padre.
     * L'attributo vince — su una pagina moderna non c'e', e su una vecchia e'
     * quello che l'autore intendeva. */
    a = html_attr(&g_doc, v, "text");
    if (!a || !a[0]) a = html_attr(&g_doc, v, "color");
    if (a && a[0]) {
        unsigned int c;
        unsigned int n = 0;

        while (a[n]) n++;
        if (css_colore(a, n, &c)) st->colore = c;
    }

    /* ! E L'ALLINEAMENTO SI APPLICA SEMPRE, per la stessa ragione: anche lui
     * si eredita, quindi «vuoto» non si distingue da «ereditato». Un `align`
     * scritto sull'elemento e' un'intenzione esplicita di chi ha scritto la
     * pagina, e vale piu' di quella del padre. */
    a = html_attr(&g_doc, v, "align");
    if (a && a[0]) {
        if (uguale(a, "center"))     st->allineamento = CSS_ALL_CENTRO;
        else if (uguale(a, "right")) st->allineamento = CSS_ALL_DX;
        else if (uguale(a, "left"))  st->allineamento = CSS_ALL_SX;
    }
}

/* -----------------------------------------------------------------------------
 * Il disegno
 * --------------------------------------------------------------------------- */
static void disegna(void)
{
    int i;

    /* ! I CONTROLLI SI RIDISEGNANO PRIMA DEL CONTENUTO, e non basta riempire
     * di grigio. Qui c'era un ex_riempi su tutta la finestra: dipingeva SOPRA
     * la casella dell'indirizzo e i pulsanti, che sono figli e stanno negli
     * stessi pixel. Il risultato era una barra sparita al primo disegno — e
     * siccome il primo disegno arriva subito, non si vedeva mai.
     *
     * ex_procedura_base riempie il fondo E ridisegna i figli: e' la stessa
     * cosa in una chiamata, e resta giusta il giorno che si aggiunge un
     * pulsante. */
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);

    ex_riempi(g_f, area_x() - 2, area_y() - 2, area_w() + 4, area_h() + 4,
              EX_BIANCO);
    ex_incavo(g_f, area_x() - 2, area_y() - 2, area_w() + 4, area_h() + 4);

    disegna_barra();

    /* ! GLI SFONDI PRIMA DI TUTTO IL RESTO, e ritagliati a mano all'area come
     * le immagini: ex_riempi ritaglia alla FINESTRA, non al documento. */
    for (i = 0; i < g_sfondi_n; i++) {
        int y = g_sfondi[i].y - g_scorri;
        int h = g_sfondi[i].h;

        if (y + h < area_y() || y > area_y() + area_h()) continue;
        if (y < area_y()) { h -= area_y() - y; y = area_y(); }
        if (y + h > area_y() + area_h()) h = area_y() + area_h() - y;
        if (h > 0)
            ex_riempi(g_f, g_sfondi[i].x, y, g_sfondi[i].w, h,
                      g_sfondi[i].colore);
    }

    for (i = 0; i < g_pez_n; i++) {
        int y  = g_pez[i].y - g_scorri;
        int ph = (g_pez[i].img >= 0 || g_pez[i].ctrl >= 0)
                 ? g_pez[i].h : ex_font_altezza(g_pez[i].font);

        /* ! SI DISEGNA SOLO CIO' CHE SI VEDE. Con una pagina di migliaia di
         * righe, dipingere tutto vorrebbe dire pagare l'intero documento a
         * ogni riga di scorrimento — e per il novantanove per cento fuori
         * dalla finestra. */
        if (y + ph < area_y() || y > area_y() + area_h()) continue;

        /* ! E UNA RIGA A META' NON SI DISEGNA AFFATTO, perche' non c'e' un
         * ritaglio. `ex_scrivi` taglia alla FINESTRA, non all'area del
         * documento: una riga che comincia sopra il bordo veniva dipinta
         * SOPRA LA BARRA DELL'INDIRIZZO, e una in fondo sopra la barra di
         * stato. Si vedeva appena il documento diventava piu' lungo della
         * finestra — cioe' proprio quando e' arrivata la barra di
         * scorrimento.
         *
         * Le immagini no: quelle un ritaglio ce l'hanno, fatto a mano qui
         * sotto, e possono sporgere quanto vogliono. */
        if (g_pez[i].img < 0 &&
            (y < area_y() || y + ph > area_y() + area_h())) continue;

        /* =================================================================
         * UN CONTROLLO DI MODULO
         *
         * ! LA FORMA LA FA IL RILIEVO, non un bordo disegnato: `ex_incavo`
         * per cio' in cui si scrive, `ex_rilievo` per cio' che si preme. Sono
         * le stesse due funzioni con cui il toolkit disegna i propri
         * controlli, ed e' il motivo per cui una pagina web dentro EX-OS
         * sembra fatta della stessa materia del resto del sistema.
         * ================================================================= */
        if (g_pez[i].ctrl >= 0) {
            Ctrl *c  = &g_ctrl[g_pez[i].ctrl];
            int   cx = g_pez[i].x, cw = g_pez[i].w, ch = g_pez[i].h;
            char  mostra[CTRL_VAL_MAX];
            int   k;

            for (k = 0; c->valore[k] && k < CTRL_VAL_MAX - 1; k++)
                mostra[k] = c->segreto ? '*' : c->valore[k];
            mostra[k] = '\0';

            switch (c->tipo) {
            case CTRL_PULSANTE:
                ex_riempi(g_f, cx, y, cw, ch, EX_GRIGIO);
                ex_rilievo(g_f, cx, y, cw, ch);
                ex_scrivi(g_f,
                          cx + (cw - ex_larghezza_testo(EX_FONT_SISTEMA, mostra)) / 2,
                          y + (ch - 16) / 2, mostra, EX_NERO);
                break;

            case CTRL_SPUNTA:
            case CTRL_RADIO:
                ex_riempi(g_f, cx, y, cw, ch, EX_BIANCO);
                ex_incavo(g_f, cx, y, cw, ch);
                /* ! IL SEGNO E' UN QUADRATINO PIENO, e vale per tutt'e due.
                 * Un cerchio disegnato a mano su quattordici pixel viene un
                 * ottagono storto: peggio di un quadrato onesto. */
                if (c->acceso)
                    ex_riempi(g_f, cx + 3, y + 3, cw - 6, ch - 6, EX_NERO);
                break;

            case CTRL_SCELTA:
                ex_riempi(g_f, cx, y, cw, ch, EX_BIANCO);
                ex_incavo(g_f, cx, y, cw, ch);
                ex_scrivi(g_f, cx + 4, y + (ch - 16) / 2, mostra, EX_NERO);
                /* La freccia in fondo: dice che si apre, anche se non si apre
                 * ancora. */
                ex_riempi(g_f, cx + cw - 18, y + 2, 16, ch - 4, EX_GRIGIO);
                ex_rilievo(g_f, cx + cw - 18, y + 2, 16, ch - 4);
                ex_scrivi(g_f, cx + cw - 14, y + (ch - 16) / 2, "v", EX_NERO);
                break;

            case CTRL_AREA: {
                /* ! L'AREA VA A CAPO, e non e' un vezzo: una <textarea> alta
                 * ottantotto pixel che mostra una riga sola sembra una casella
                 * rotta. Si spezza sui pixel e non sulle parole — un'area di
                 * testo non e' un paragrafo — ma si vede tutto quello che c'e'
                 * dentro, che e' il punto. */
                int riga = 0, i0 = 0;
                int per_riga = (cw - 8) / 8;

                ex_riempi(g_f, cx, y, cw, ch, EX_BIANCO);
                ex_incavo(g_f, cx, y, cw, ch);

                if (per_riga < 1) per_riga = 1;
                while (mostra[i0] && (riga + 1) * 18 < ch) {
                    char pezzo[CTRL_VAL_MAX];
                    int  q = 0;
                    int  ini = i0;

                    /* ! GLI A CAPO SCRITTI DA CHI DIGITA VALGONO, e vengono
                     * prima del riempimento: un'area che ignora l'Invio
                     * mostrerebbe due paragrafi come una frase sola. */
                    while (mostra[i0] && mostra[i0] != '\n' &&
                           q < per_riga && q < CTRL_VAL_MAX - 1)
                        pezzo[q++] = mostra[i0++];
                    pezzo[q] = '\0';
                    if (mostra[i0] == '\n') i0++;

                    ex_scrivi(g_f, cx + 4, y + 3 + riga * 18, pezzo, EX_NERO);
                    riga++;

                    /* ! IL CURSORE STA SULLA RIGA CHE LO CONTIENE. Questo giro
                     * ha appena impaginato i caratteri da `ini` a `i0`: se il
                     * punto di scrittura cade li' dentro, il cursore e' su
                     * QUESTA riga, alla colonna che gli tocca. */
                    if (g_pez[i].ctrl == g_ctrl_fuoco) {
                        int cu = g_ctrl[g_pez[i].ctrl].cur;

                        if (cu >= ini && (cu < i0 || !mostra[i0])) {
                            static char prima[CTRL_VAL_MAX];
                            int         j, cur;

                            for (j = 0; j < cu - ini && j < q; j++)
                                prima[j] = pezzo[j];
                            prima[j] = '\0';

                            cur = cx + 4 +
                                  ex_larghezza_testo(EX_FONT_SISTEMA, prima);
                            if (cur < cx + cw - 3)
                                ex_riempi(g_f, cur, y + 3 + (riga - 1) * 18,
                                          2, 15, EX_NERO);
                        }
                    }
                }

                if (g_pez[i].ctrl == g_ctrl_fuoco && riga == 0)
                    ex_riempi(g_f, cx + 4, y + 3, 2, 15, EX_NERO);
                break;
            }

            default:                     /* casella di testo */
                ex_riempi(g_f, cx, y, cw, ch, EX_BIANCO);
                ex_incavo(g_f, cx, y, cw, ch);
                ex_scrivi(g_f, cx + 4, y + 3, mostra, EX_NERO);
                /* ! IL CURSORE SI VEDE SOLO DOVE SI STA SCRIVENDO. Senza, non
                 * c'e' modo di sapere quale casella prende i tasti — e chi
                 * scrive nel posto sbagliato pensa che la tastiera sia rotta. */
                if (g_pez[i].ctrl == g_ctrl_fuoco) {
                    /* ! IL CURSORE STA DOVE SI SCRIVE, non in fondo: si misura
                     * il testo che lo PRECEDE. `mostra` ha un carattere per
                     * ogni carattere del valore — gli asterischi di una
                     * password compresi — quindi l'indice vale per tutt'e due. */
                    /* ! STATICO COME `cop` QUI SOTTO, e per la stessa ragione:
                     * `disegna` gira dentro un ciclo su ventiquattromila pezzi
                     * e la sua cornice e' gia' grassa — mostra[], pezzo[] —
                     * mentre lo stack impegnato al caricamento e' 8 KB. Non
                     * c'e' ricorsione qui dentro, quindi una copia sola basta. */
                    static char prima[CTRL_VAL_MAX];
                    int         q = g_ctrl[g_pez[i].ctrl].cur, j;
                    int         cur;

                    if (q < 0) q = 0;
                    for (j = 0; j < q && mostra[j]; j++) prima[j] = mostra[j];
                    prima[j] = '\0';

                    cur = cx + 4 + ex_larghezza_testo(EX_FONT_SISTEMA, prima);
                    if (cur < cx + cw - 3)
                        ex_riempi(g_f, cur, y + 3, 2, ch - 6, EX_NERO);
                }
                break;
            }
            continue;
        }

        /* ! UN'IMMAGINE SI RITAGLIA A MANO, e non e' pignoleria: ex_pixmap
         * ritaglia alla FINESTRA, non all'area del documento, quindi
         * un'immagine alta trecento pixel scorsa in su dipingerebbe sopra la
         * casella dell'indirizzo. Il testo se la cava perche' e' alto venti
         * punti e sborda di poco; un'immagine no. */
        if (g_pez[i].img >= 0) {
            const Imm *im    = &g_imm[g_pez[i].img];
            int        cima  = y;
            int        salta = 0;
            int        alta  = (int)im->h;

            /* ! IL POSTO RISERVATO SI VEDE, e non e' decorazione: un buco
             * bianco in mezzo al testo sembra un difetto di impaginazione,
             * mentre un riquadro dice «qui sta arrivando un'immagine». E'
             * quello che hanno sempre fatto i browser.
             *
             * ! E SI RITAGLIA COME L'IMMAGINE CHE ASPETTA, per la ragione
             * scritta qui sopra: anche ex_riempi ritaglia alla FINESTRA e non
             * all'area del documento. Disegnarlo solo quando ci sta tutto
             * sarebbe stato piu' corto, ma un riquadro alto quanto l'area non
             * ci sta MAI per intero: sparirebbe appena lo si scorre, cioe'
             * proprio mentre lo si guarda. */
            if (!im->px) {
                int rw = g_pez[i].w;

                alta = g_pez[i].h;
                if (cima < area_y()) {
                    salta = area_y() - cima;
                    cima  = area_y();
                    alta -= salta;
                }
                if (cima + alta > area_y() + area_h())
                    alta = area_y() + area_h() - cima;

                if (rw > 0 && alta > 0) {
                    ex_riempi(g_f, g_pez[i].x, cima, rw, alta, EX_GRIGIO);

                    /* Il bordo si incide solo quando il riquadro c'e' tutto:
                     * un incavo tagliato a meta' disegna una riga di luce in
                     * mezzo al testo, e si legge come un difetto. */
                    if (salta == 0 && alta == g_pez[i].h)
                        ex_incavo(g_f, g_pez[i].x, cima, rw, alta);
                }
                continue;
            }

            if (cima < area_y()) {
                salta = area_y() - cima;
                cima  = area_y();
                alta -= salta;
            }
            if (cima + alta > area_y() + area_h())
                alta = area_y() + area_h() - cima;

            if (alta > 0)
                ex_pixmap(g_f, g_pez[i].x, cima, (int)im->w, alta,
                          im->px + (unsigned int)salta * im->w, im->w);
            continue;
        }

        {
            ExFont       f = g_pez[i].font;
            const char  *t = g_arena + g_pez[i].testo;
            unsigned int c = g_pez[i].colore;

            /* Il testo nell'arena e' una parola sola perche' l'impaginazione
             * l'ha spezzato: si disegna fino allo spazio. */
            {
                static char cop[256];
                int k = 0;

                /* ! CI SI FERMA A QUALUNQUE BIANCO, non al solo spazio.
                 * L'impaginazione ha gia' spezzato il testo in parole e il
                 * pezzo punta all'inizio di una: quello che segue nell'arena
                 * appartiene alla parola dopo. Fermarsi al solo ' ' bastava
                 * finche' gli a capo non arrivavano fin qui — dentro <pre>
                 * arrivano, e venivano DISEGNATI, cioe' un rettangolino in
                 * coda a ogni riga. */
                while (t[k] && t[k] != ' ' && t[k] != '\n' &&
                       t[k] != '\r' && t[k] != '\t' &&
                       k < (int)sizeof(cop) - 1) {
                    cop[k] = t[k]; k++;
                }
                cop[k] = '\0';
                ex_scrivi_con(g_f, f, g_pez[i].x, y, cop, c);

                /* ! UN COLLEGAMENTO SI SOTTOLINEA, e non basta il colore: su
                 * uno schermo a pochi colori il blu e il nero si distinguono
                 * male, e chi non li distingue non trova i collegamenti. */
                if (g_pez[i].link >= 0)
                    ex_riempi(g_f, g_pez[i].x, y + ex_font_altezza(f) - 2,
                              g_pez[i].w, 1, c);
            }
        }
    }

    ex_aggiorna(g_f);
}

static void dico(const char *s)
{
    if (g_stato) ex_testo_metti(g_stato, s);
}

/* -----------------------------------------------------------------------------
 * Gli indirizzi relativi
 *
 * ! I RIFERIMENTI RELATIVI SI RISOLVONO, e sono la maggioranza: «/x» o
 * «pagina.html» senza schema. La stessa regola sta in exhttp per le
 * redirezioni; qui la si applica a quello che scrive la pagina — i
 * collegamenti e le immagini, che sono la stessa cosa vista da due parti.
 * Averla in una funzione sola vuol dire che il giorno che sbaglia, sbaglia in
 * un posto solo.
 * --------------------------------------------------------------------------- */
static int risolvi(const char *rif, char *out, unsigned int max)
{
    HttpUrl u;

    if (!rif || !rif[0] || max < 2) return 0;

    if (rif[0] == 'h' && rif[1] == 't') {
        strncpy(out, rif, max - 1);
        out[max - 1] = '\0';
        return 1;
    }

    /* ! UNO SCHEMA CHE NON E' http NON SI SEGUE, e va riconosciuto PRIMA di
     * trattarlo da percorso relativo: «data:image/png;base64,...» attaccato in
     * coda all'indirizzo di adesso produrrebbe una richiesta lunga un
     * chilometro verso il sito sbagliato. I due punti prima di qualunque «/»
     * sono uno schema. */
    {
        const char *c = rif;

        while (*c && *c != ':' && *c != '/' && *c != '?' && *c != '#') c++;
        if (*c == ':') return 0;
    }

    if (!http_url(g_qui, &u)) return 0;

    /* ! «//host/x» E' UN INDIRIZZO SENZA SCHEMA, non un percorso: vuol dire
     * «lo stesso schema della pagina». Le immagini dei siti veri sono scritte
     * cosi' molto piu' spesso dei collegamenti. */
    if (rif[0] == '/' && rif[1] == '/') {
        strcpy(out, u.cifrato ? "https:" : "http:");
        strncat(out, rif, max - strlen(out) - 1);
        return 1;
    }

    strcpy(out, u.cifrato ? "https://" : "http://");
    strncat(out, u.host, max - strlen(out) - 1);

    if ((u.cifrato && u.porta != 443) || (!u.cifrato && u.porta != 80)) {
        char cifre[8], rov[8];
        unsigned int p = u.porta;
        int a = 0, b = 0;

        while (p) { rov[b++] = (char)('0' + (p % 10)); p /= 10; }
        while (b) cifre[a++] = rov[--b];
        cifre[a] = '\0';
        strncat(out, ":", max - strlen(out) - 1);
        strncat(out, cifre, max - strlen(out) - 1);
    }

    if (rif[0] == '/') {
        strncat(out, rif, max - strlen(out) - 1);
    } else {
        char base[HTTP_PERCORSO_MAX];
        int  i;

        strncpy(base, u.percorso, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        i = (int)strlen(base);
        while (i > 0 && base[i - 1] != '/') i--;
        base[i] = '\0';
        strncat(out, base, max - strlen(out) - 1);
        strncat(out, rif, max - strlen(out) - 1);
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * La cache su disco
 *
 * ! IL POSTO NON SI SCRIVE NEL CODICE, SI RICAVA DA `HOME`. La casa e' /root
 * solo per root e /home/<utente> per tutti gli altri — la regola sta in
 * bin/login/login.c — e un percorso costante nel sorgente funzionerebbe per
 * una persona sola. Da li' in giu' la convenzione e' $HOME/.app/<programma>/,
 * cioe' per noi $HOME/.app/browser/cache.
 *
 * ! E SE NON SI PUO' SCRIVERE NON SI MUORE. Avviando da CD la radice e' in
 * sola lettura e la directory non si crea: il browser lavora in memoria
 * esattamente come prima, e lo dice una volta sola invece di riprovarci a
 * ogni immagine.
 *
 * ! IL NOME DEL FILE E' L'IMPRONTA DELL'INDIRIZZO, MA A DECIDERE E'
 * L'INDIRIZZO SCRITTO DENTRO. Un'impronta a 32 bit ogni tanto collide, e una
 * collisione servirebbe l'immagine SBAGLIATA — che e' un difetto silenzioso,
 * il peggiore che una cache possa avere. Con l'indirizzo nella testa del file
 * una collisione diventa semplicemente un buco: si riscarica e si riscrive.
 *
 * ! ADESSO SOPRAVVIVE AI RIAVVII, e prima no: si svuotava all'avvio perche'
 * una cache che dura vuole una politica di scadenza, e non c'era. Adesso c'e',
 * ed e' fatta di due numeri soli — quanto puo' occupare in tutto e quanti
 * giorni puo' avere una voce. Alla partenza si POTA: si buttano le voci
 * scadute, e se si sfora ancora si buttano le piu' vecchie finche' non si
 * rientra.
 *
 * ! E LA SCADENZA E' PER ETA', NON PER INTESTAZIONI HTTP. `Cache-Control` ed
 * `ETag` vogliono una richiesta condizionale e un dialogo con il server: e'
 * lavoro vero, e va fatto il giorno che si vuole rispettare cio' che il sito
 * chiede. Sette giorni sono una regola nostra, dichiarata, e sbagliano sempre
 * dalla parte prudente — una pagina di sette giorni fa non si serve.
 *
 * ! DAVANTI AL DISCO C'E' UN PEZZO DI RAM, E SOLO UN PEZZO. Otto caselle da
 * sessantaquattro chilobyte: ci stanno le immagini, i fogli di stile e le
 * pagine piccole — cioe' le cose che si rileggono spesso — e non ci sta una
 * pagina da mezzo megabyte, che tanto sta gia' in g_pagina. Una cache in RAM
 * che tiene tutto e' un altro modo di scrivere «memoria finita».
 *
 * ! LE CASELLE SONO A MISURA FISSA, e non e' pigrizia: con un'arena a
 * scorrimento servirebbe compattare, e compattare vuol dire spostare byte che
 * qualcuno sta guardando. Otto pezzi uguali si buttano e si riusano senza
 * muovere niente.
 *
 * ! E QUANDO LA RETE NON RISPONDE, LA COPIA SU DISCO VALE. E' il motivo per
 * cui una cache che dura serve davvero: la pagina di ieri e' meglio di una
 * finestra vuota, purche' si DICA che e' di ieri.
 * --------------------------------------------------------------------------- */
#define CACHE_MAX_BYTE  (4u * 1024u * 1024u)   /* quanto si scrive per sessione */
#define CACHE_PERC_MAX  192
#define CACHE_PULIZIA   128                    /* nomi per giro di potatura */
#define CACHE_DISCO_MAX (16u * 1024u * 1024u)  /* quanto puo' occupare in tutto */
#define CACHE_GIORNI    7                      /* oltre, una voce e' scaduta */

#define RAM_VOCI        8
#define RAM_CASELLA     (64u * 1024u)

typedef struct {
    char         magia[12];
    unsigned int byte;
    char         url[EXHTTP_URL_MAX];
} CacheTesta;

static char         g_cache[CACHE_PERC_MAX] = "";
static unsigned int g_cache_scritti = 0;

/* Il percorso del file di una risorsa: otto cifre esadecimali piu' «.dat»,
 * che sta anche in un nome 8.3 se la casa dell'utente e' su FAT. */
static void cache_nome(const char *url, char *out, unsigned int max)
{
    static const char cifre[] = "0123456789abcdef";
    unsigned int      h = 2166136261u;      /* FNV-1a */
    unsigned int      i;
    char              nome[16];

    while (*url) { h ^= (unsigned char)*url++; h *= 16777619u; }

    for (i = 0; i < 8; i++) nome[i] = cifre[(h >> ((7 - i) * 4)) & 0xFu];
    nome[8] = '.'; nome[9] = 'd'; nome[10] = 'a'; nome[11] = 't';
    nome[12] = '\0';

    strncpy(out, g_cache, max - 1);
    out[max - 1] = '\0';
    strncat(out, "/", max - strlen(out) - 1);
    strncat(out, nome, max - strlen(out) - 1);
}


/* Vero se il nome e' di una nostra voce: otto cifre esadecimali e «.dat».
 *
 * ! SI GUARDA IL NOME PRIMA DI CANCELLARE, e non e' pignoleria: la directory
 * e' di chi usa il sistema, e potrebbe averci messo dentro qualcosa. Una
 * potatura che cancella cio' che non ha scritto lei e' un difetto che si
 * scopre quando e' tardi. */
static int cache_nostro(const char *n)
{
    int i;

    for (i = 0; i < 8; i++) {
        char c = n[i];

        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return n[8] == '.' && n[9] == 'd' && n[10] == 'a' && n[11] == 't' &&
           n[12] == '\0';
}

/* =============================================================================
 * La potatura: cosa si butta e perche'
 *
 * Due passate, e la prima e' quella che conta: si tolgono le voci SCADUTE. Se
 * dopo quella si sfora ancora il tetto, si tolgono le piu' vecchie finche' non
 * si rientra.
 *
 * ! SI GUARDA IL TEMPO DI MODIFICA, che per una voce di cache e' il momento in
 * cui e' stata scritta: e' l'unico orologio che c'e' senza aggiungere un campo
 * all'intestazione — e un campo in piu' vorrebbe dire che le voci scritte dalla
 * versione di prima non si leggono.
 *
 * ! E SE L'OROLOGIO NON C'E' NON SI BUTTA NIENTE PER ETA'. Su una macchina
 * senza batteria la data all'avvio puo' essere il 1980: con quella, «sette
 * giorni fa» sta nel futuro e si cancellerebbe tutta la cache a ogni accensione.
 * Il tetto sulla misura invece vale sempre, perche' non dipende dall'orologio.
 * ============================================================================= */
static void cache_pota(void)
{
    static char  nomi[CACHE_PULIZIA][16];
    static unsigned int eta[CACHE_PULIZIA];
    static unsigned int misura[CACHE_PULIZIA];
    DIR           *d;
    struct dirent *e;
    unsigned int   totale = 0, adesso = (unsigned int)time(0);
    int            n = 0, i, buttate = 0;

    if (!g_cache[0]) return;

    d = opendir(g_cache);
    if (!d) return;

    while (n < CACHE_PULIZIA && (e = readdir(d)) != 0) {
        char        p[CACHE_PERC_MAX + 24];
        struct stat st;

        if (!cache_nostro(e->d_name)) continue;

        strncpy(nomi[n], e->d_name, sizeof(nomi[0]) - 1);
        nomi[n][sizeof(nomi[0]) - 1] = '\0';

        snprintf(p, sizeof(p), "%s/%s", g_cache, nomi[n]);
        if (stat(p, &st) != 0) continue;

        misura[n] = (unsigned int)st.st_size;
        eta[n]    = (adesso > (unsigned int)st.st_mtime)
                    ? adesso - (unsigned int)st.st_mtime : 0;
        totale += misura[n];
        n++;
    }
    closedir(d);

    /* 1. le scadute */
    for (i = 0; i < n; i++) {
        char p[CACHE_PERC_MAX + 24];

        if (adesso == 0 || eta[i] <= CACHE_GIORNI * 24u * 3600u) continue;
        snprintf(p, sizeof(p), "%s/%s", g_cache, nomi[i]);
        unlink(p);
        totale -= misura[i];
        misura[i] = 0;
        buttate++;
    }

    /* 2. le piu' vecchie, finche' non si rientra nel tetto */
    while (totale > CACHE_DISCO_MAX) {
        int          peggiore = -1;
        unsigned int piu_vecchia = 0;

        for (i = 0; i < n; i++)
            if (misura[i] != 0 && eta[i] >= piu_vecchia) {
                piu_vecchia = eta[i];
                peggiore = i;
            }
        if (peggiore < 0) break;

        {
            char p[CACHE_PERC_MAX + 24];

            snprintf(p, sizeof(p), "%s/%s", g_cache, nomi[peggiore]);
            unlink(p);
            totale -= misura[peggiore];
            misura[peggiore] = 0;
            buttate++;
        }
    }

    printf("browser: cache in %s — %u voci, %u KB", g_cache,
           (unsigned int)n - (unsigned int)buttate, totale / 1024u);
    if (buttate) printf(", %d potate", buttate);
    printf("\n");
}

/* Crea $HOME/.app/browser/cache. Si chiama una volta, all'avvio. */
static void cache_prepara(void)
{
    const char *casa = getenv("HOME");
    char        p[CACHE_PERC_MAX];
    int         i;

    static const char *const passi[] = { "/.app", "/browser", "/cache" };

    g_cache[0] = '\0';

    if (!casa || !casa[0]) {
        printf("browser: HOME non c'e', niente cache su disco.\n");
        return;
    }
    if (strlen(casa) + 24 >= sizeof(p)) {
        printf("browser: HOME troppo lungo, niente cache su disco.\n");
        return;
    }

    strcpy(p, casa);

    /* ! LE BARRE FINALI SI TOLGONO TUTTE, COMPRESA QUELLA DELLA RADICE, o
     * «/» piu' «/.app» diventa «//.app». Su POSIX le due barre portano allo
     * stesso posto, ma il percorso finisce stampato nei messaggi e scritto
     * nella variabile: uno che si legge male e' uno che si cerca male. */
    i = (int)strlen(p);
    while (i > 0 && p[i - 1] == '/') p[--i] = '\0';

    /* ! mkdir NON FA LA CATENA, e EEXIST non e' un errore: e' il caso normale
     * dalla seconda volta in poi. */
    for (i = 0; i < 3; i++) {
        strncat(p, passi[i], sizeof(p) - strlen(p) - 1);
        if (mkdir(p, i == 2 ? 0700 : 0755) != 0 && errno != EEXIST) {
            printf("browser: niente cache in %s (%s), lavoro in memoria.\n",
                   p, strerror(errno));
            return;
        }
    }

    strncpy(g_cache, p, sizeof(g_cache) - 1);
    g_cache[sizeof(g_cache) - 1] = '\0';

    cache_pota();
    printf("browser: cache in %s\n", g_cache);
}

/* Rende 1 e riempie `buf` se la risorsa c'e' ed e' proprio la sua. */
/* =============================================================================
 * Il pezzo di RAM davanti al disco
 *
 * Otto caselle uguali; quando servono tutte, si butta quella usata piu' tempo
 * fa. Il contatore d'uso e' un numero che cresce: non serve un orologio, serve
 * un ordine.
 * ============================================================================= */
typedef struct {
    char         url[EXHTTP_URL_MAX];
    unsigned int byte;                  /* 0 = casella libera */
    unsigned int uso;
} RamVoce;

static RamVoce      g_ram[RAM_VOCI];
static unsigned char g_ram_dati[RAM_VOCI][RAM_CASELLA];
static unsigned int  g_ram_orologio = 1;
static unsigned int  g_ram_colpi = 0, g_ram_giri = 0;

static int ram_cerca(const char *url, unsigned char *buf, unsigned int max,
                     unsigned int *quanti)
{
    unsigned int i;

    for (i = 0; i < RAM_VOCI; i++) {
        if (g_ram[i].byte == 0 || !uguale(g_ram[i].url, url)) continue;
        if (g_ram[i].byte > max) return 0;
        memcpy(buf, g_ram_dati[i], g_ram[i].byte);
        *quanti = g_ram[i].byte;
        g_ram[i].uso = g_ram_orologio++;
        g_ram_colpi++;
        return 1;
    }
    return 0;
}

static void ram_metti(const char *url, const unsigned char *dati, unsigned int n)
{
    unsigned int i, scelta = 0, piu_vecchia = 0xFFFFFFFFu;

    /* ! CIO' CHE NON CI STA NON ENTRA, e non e' un fallimento: la pagina
     * grande sta gia' nel suo buffer, e sacrificare otto casella per lei
     * vorrebbe dire buttare le otto cose che si rileggono davvero. */
    if (n == 0 || n > RAM_CASELLA) return;

    for (i = 0; i < RAM_VOCI; i++) {
        if (g_ram[i].byte == 0) { scelta = i; break; }
        if (uguale(g_ram[i].url, url)) { scelta = i; break; }
        if (g_ram[i].uso < piu_vecchia) { piu_vecchia = g_ram[i].uso; scelta = i; }
    }

    memcpy(g_ram_dati[scelta], dati, n);
    strncpy(g_ram[scelta].url, url, sizeof(g_ram[0].url) - 1);
    g_ram[scelta].url[sizeof(g_ram[0].url) - 1] = '\0';
    g_ram[scelta].byte = n;
    g_ram[scelta].uso  = g_ram_orologio++;
}

static int cache_leggi(const char *url, unsigned char *buf, unsigned int max,
                       unsigned int *quanti)
{
    CacheTesta t;
    char       p[CACHE_PERC_MAX + 24];
    int        fd, n;

    g_ram_giri++;
    if (ram_cerca(url, buf, max, quanti)) return 1;

    if (!g_cache[0]) return 0;

    cache_nome(url, p, sizeof(p));
    fd = open(p, O_RDONLY);
    if (fd < 0) return 0;

    n = (int)read(fd, &t, sizeof(t));
    if (n != (int)sizeof(t)) { close(fd); return 0; }

    t.magia[sizeof(t.magia) - 1] = '\0';
    t.url[sizeof(t.url) - 1]     = '\0';

    if (!uguale(t.magia, "EXCACHE1") || t.byte == 0 || t.byte > max ||
        !uguale(t.url, url)) { close(fd); return 0; }

    n = (int)read(fd, buf, t.byte);
    close(fd);
    if (n != (int)t.byte) return 0;

    *quanti = t.byte;

    /* Letta dal disco una volta, la prossima si prende dalla RAM. */
    ram_metti(url, buf, t.byte);
    return 1;
}

static void cache_scrivi(const char *url, const unsigned char *dati,
                         unsigned int n)
{
    CacheTesta t;
    char       p[CACHE_PERC_MAX + 24];
    int        fd, bene;

    if (!g_cache[0] || n == 0) return;

    /* ! QUANDO IL TETTO E' PIENO SI SMETTE DI SCRIVERE MA SI CONTINUA A
     * LEGGERE. Una cache che si svuota da sola a meta' navigazione sarebbe
     * peggio di nessuna cache: ogni pagina ricomincerebbe da zero senza che si
     * capisca perche'. */
    if (g_cache_scritti + n > CACHE_MAX_BYTE) return;

    cache_nome(url, p, sizeof(p));
    fd = open(p, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (fd < 0) return;

    memset(&t, 0, sizeof(t));
    strcpy(t.magia, "EXCACHE1");
    t.byte = n;
    strncpy(t.url, url, sizeof(t.url) - 1);

    bene = (write(fd, &t, sizeof(t)) == (ssize_t)sizeof(t) &&
            write(fd, dati, n) == (ssize_t)n);
    close(fd);

    /* Una voce scritta a meta' e' peggio di una voce assente: si toglie. */
    if (bene) { g_cache_scritti += n; ram_metti(url, dati, n); }
    else      unlink(p);
}

/* -----------------------------------------------------------------------------
 * Prendere le immagini
 * --------------------------------------------------------------------------- */

/* Una sola immagine: la scarica, la decodifica, se la copia. Rende 1 se ce
 * l'ha fatta — e su 0 non ha lasciato niente in giro. */
static int imm_prendi(int k)
{
    Imm         *im = &g_imm[k];
    ExHttpEsito  e;
    EximgBitmap  bm;
    char         url[EXHTTP_URL_MAX];
    unsigned int n = 0;
    unsigned int w, h;

    if (!eximg_pronta()) return 0;
    if (!risolvi(im->src, url, sizeof(url))) return 0;

    /* ! LA CACHE SI GUARDA PRIMA DELLA RETE, e si ripaga gia' dentro una
     * pagina sola: due <img> con lo stesso `src` erano due richieste, e nella
     * prova lo si vedeva nel log del server. */
    if (!cache_leggi(url, g_imm_buf, sizeof(g_imm_buf), &n)) {
        if (!exhttp_prendi(url, g_imm_buf, sizeof(g_imm_buf), &e)) return 0;
        if (e.codice != 200 || e.byte == 0) return 0;

        /* ! UN FILE TRONCATO NON SI PROVA A DECODIFICARE: un PNG a meta' non
         * e' un PNG piu' piccolo, e' un file rotto — e il decodificatore lo
         * scoprirebbe dopo aver allocato. E non si mette in cache, o il
         * troncamento diventerebbe permanente. */
        if (e.troncata) return 0;

        n = e.byte;
        cache_scrivi(url, g_imm_buf, n);
    }

    if (!g_img_carica(g_imm_buf, n, &bm)) return 0;

    misura(im, bm.larghezza, bm.altezza, &w, &h);

    /* ! IL TETTO SI CONTROLLA PRIMA DI COPIARE, e vale sulla SOMMA: dodici
     * immagini che stanno ciascuna nell'area sono lo stesso dodici volte
     * l'area. */
    if (w == 0 || h == 0 || g_imm_px + w * h > g_imm_px_tot) {
        /* ! QUI CI FINISCE SOLO CHI NON AVEVA DICHIARATO LE MISURE: per le
         * altre la risposta si e' gia' data prima di scaricare. Si conta lo
         * stesso, perche' a chi guarda la pagina non importa in che momento
         * abbiamo scoperto che non c'era posto. */
        if (w && h) g_imm_fuori++;
        g_img_libera(&bm);
        return 0;
    }

    im->px = ridimensiona(&bm, w, h);
    g_img_libera(&bm);

    /* ! ANCHE QUESTA E' «NON C'ERA POSTO», e va contata come le altre. Il tetto
     * dei pixel dice quanto vogliamo prenderne; questa riga e' la macchina che
     * dice quanto ce n'e' davvero, e le due risposte possono differire — un
     * altro programma puo' aver preso la memoria nel frattempo. Chi guarda la
     * pagina vede la stessa cosa in tutt'e due i casi: un'immagine che non
     * c'e'. Lasciarla fuori dal conto rendeva la riga di stato una bugia. */
    if (!im->px) {
        unsigned int chiesti = w * h;

        if (g_imm_px_negato == 0 || chiesti < g_imm_px_negato)
            g_imm_px_negato = chiesti;
        g_imm_fuori++;
        return 0;
    }

    im->w = w;
    im->h = h;
    g_imm_px += w * h;
    return 1;
}

/* =============================================================================
 * ! C'E' POSTO PER QUESTA? SI RISPONDE PRIMA DI SCARICARLA.
 *
 * Il tetto dei pixel si controllava dentro `imm_prendi`, cioe' DOPO aver
 * scaricato il file e DOPO averlo decodificato: l'immagine che non ci stava
 * costava tutta la rete e tutta la CPU per finire buttata. Con dodici caselle
 * si perdevano pochi secondi; con sessantaquattro sarebbero minuti.
 *
 * Due risposte sicure, e nessuna delle due indovina:
 *   - se non c'e' piu' un pixel libero, non c'e' posto per NESSUNO;
 *   - se la pagina ha dichiarato `width` e `height`, la misura finale si sa
 *     gia' — e' la stessa che tiene il posto nell'impaginazione — quindi si sa
 *     con certezza se ci sta.
 * Quella senza misure dichiarate si scarica e si vede: non c'e' modo di
 * saperlo prima, e tirare a indovinare butterebbe via immagini buone.
 * ========================================================================== */
static int imm_ci_sta(int k)
{
    unsigned int w, h;

    if (g_imm_px >= g_imm_px_tot) return 0;
    if (!g_imm[k].dich_w || !g_imm[k].dich_h) return 1;

    misura(&g_imm[k], g_imm[k].dich_w, g_imm[k].dich_h, &w, &h);
    if (w == 0 || h == 0) return 1;

    /* Gia' rifiutata una piu' piccola di questa: inutile scaricarla. */
    if (g_imm_px_negato && w * h >= g_imm_px_negato) return 0;

    return g_imm_px + w * h <= g_imm_px_tot;
}

/* Tutte quelle che l'impaginazione ha trovato, una per volta. */
static void immagini_prendi(void)
{
    char msg[160];
    int  k;

    for (k = 0; k < g_imm_n; k++) {
        if (g_imm[k].stato != 0) continue;

        if (!imm_ci_sta(k)) {
            g_imm_fuori++;
            g_imm[k].stato = 2;
            if (g_imm[k].ris_w) {
                g_imm[k].ris_w = g_imm[k].ris_h = 0;
                impagina();
                disegna();
            }
            continue;
        }

        sprintf(msg, "immagine %d di %d...", k + 1, g_imm_n);
        dico(msg);

        if (!imm_prendi(k)) {
            /* ! QUELLA CHE NON ARRIVA SI SALTA E BASTA: al suo posto resta il
             * suo `alt`, e la pagina va avanti. Un browser che si ferma sulla
             * prima immagine irraggiungibile non mostra mai niente.
             *
             * ! MA IL POSTO RISERVATO VA RESTITUITO, o resterebbe un riquadro
             * vuoto per sempre al posto di un `alt` che si puo' leggere. */
            g_imm[k].stato = 2;
            if (g_imm[k].ris_w) {
                g_imm[k].ris_w = g_imm[k].ris_h = 0;
                impagina();
                disegna();
            }
            continue;
        }

        g_imm[k].stato = 1;

        /* =====================================================================
         * ! SI REIMPAGINA SOLO SE LA MISURA E' CAMBIATA, e la differenza si
         * misura in minuti. Reimpaginare vuol dire rifare l'albero intero —
         * ventiquattromila pezzi su una voce di Wikipedia — e farlo per ognuna
         * delle nove immagini costa piu' dello scaricarle.
         *
         * Se la pagina aveva dichiarato `width` e `height`, il posto era gia'
         * stato tenuto della misura giusta: l'immagine ci entra dentro e non
         * sposta una virgola. Allora si ridisegna e basta.
         *
         * ! E SE NON COMBACIA SI REIMPAGINA DAVVERO, senza scorciatoie: una
         * misura diversa sposta tutto quello che viene dopo, e disegnare
         * sopra un'impaginazione vecchia darebbe testo sovrapposto — il genere
         * di difetto che sembra un problema di disegno e non lo e'.
         * ===================================================================== */
        if (g_imm[k].ris_w == g_imm[k].w && g_imm[k].ris_h == g_imm[k].h) {
            disegna();
        } else {
            impagina();
            disegna();
        }
    }

}

/* -----------------------------------------------------------------------------
 * Raccogliere i fogli di stile
 *
 * ! L'ORDINE E' QUELLO DEL DOCUMENTO, ED E' META' DELLA CASCATA: a parita' di
 * peso vince l'ultima regola letta, quindi leggerle nell'ordine sbagliato
 * cambia il risultato. Gli indici dei nodi sono gia' in ordine di documento —
 * html.c li assegna mentre analizza — quindi basta un giro dritto sul vettore,
 * senza ricorsione.
 *
 * ! E I FOGLI ESTERNI SI ASPETTANO, al contrario delle immagini. Un'immagine
 * che arriva dopo sposta il testo e si vede arrivare; un foglio di stile che
 * arrivasse dopo cambierebbe TUTTA la pagina sotto gli occhi — colori, corpi,
 * cose che spariscono. Meglio aspettare quei pochi decimi, con un tetto
 * dichiarato di quanti seguirne.
 * --------------------------------------------------------------------------- */
static void raccogli_css(void)
{
    int i, presi = 0;

    css_prepara(&g_css, g_css_reg, CSS_REGOLE_MAX, g_css_dich, CSS_DICH_MAX,
                g_css_arena, CSS_ARENA_MAX);
    css_analizza(&g_css, CSS_DI_SISTEMA, sizeof(CSS_DI_SISTEMA) - 1,
                 CSS_ORIGINE_SISTEMA);

    for (i = 0; i < (int)g_doc.nodi_n; i++) {
        const char *nome;

        if (g_doc.nodi[i].tipo != HTML_ELEMENTO) continue;
        nome = html_nome(&g_doc, i);

        if (uguale(nome, "style")) {
            int f;

            for (f = g_doc.nodi[i].primo_figlio; f >= 0;
                 f = g_doc.nodi[f].prossimo) {
                const char  *t;
                unsigned int n = 0;

                if (g_doc.nodi[f].tipo != HTML_TESTO) continue;
                t = html_testo(&g_doc, f);
                while (t[n]) n++;
                css_analizza(&g_css, t, n, CSS_ORIGINE_FOGLIO);
            }
            continue;
        }

        if (uguale(nome, "link") && presi < CSS_FOGLI_MAX) {
            const char  *rel  = html_attr(&g_doc, i, "rel");
            const char  *href = html_attr(&g_doc, i, "href");
            char         url[EXHTTP_URL_MAX];
            unsigned int n = 0;
            int          e_foglio = 0, a;

            if (!rel || !href || !href[0]) continue;

            /* «stylesheet» puo' stare in mezzo ad altre parole e in qualunque
             * cassa: si cerca dentro invece di confrontare tutto. */
            for (a = 0; rel[a] && rel[a+1] && rel[a+2] && rel[a+3] &&
                        rel[a+4] && rel[a+5]; a++) {
                if ((rel[a]   | 32) == 's' && (rel[a+1] | 32) == 't' &&
                    (rel[a+2] | 32) == 'y' && (rel[a+3] | 32) == 'l' &&
                    (rel[a+4] | 32) == 'e' && (rel[a+5] | 32) == 's') {
                    e_foglio = 1; break;
                }
            }
            if (!e_foglio) continue;
            if (!risolvi(href, url, sizeof(url))) continue;

            /* ! SI RIUSA IL BUFFER DELLE IMMAGINI, e si puo': i fogli si
             * prendono PRIMA della prima impaginazione, le immagini dopo, e
             * fra le due cose non c'e' sovrapposizione. Un buffer in piu' da
             * centoventotto chilobyte non si paga per niente. */
            if (!cache_leggi(url, g_imm_buf, sizeof(g_imm_buf), &n)) {
                ExHttpEsito e;

                dico("foglio di stile...");
                if (!exhttp_prendi(url, g_imm_buf, sizeof(g_imm_buf), &e))
                    continue;
                if (e.codice != 200 || e.byte == 0) continue;
                n = e.byte;
                if (!e.troncata) cache_scrivi(url, g_imm_buf, n);
            }

            css_analizza(&g_css, (const char *)g_imm_buf, n,
                         CSS_ORIGINE_FOGLIO);
            presi++;
        }
    }
}

/* -----------------------------------------------------------------------------
 * Andare
 * --------------------------------------------------------------------------- */
/* ! «INDIETRO» SI SERVE DALLA CACHE, TUTTO IL RESTO VA IN RETE, ed e' la
 * distinzione che rende una cache di pagine accettabile: tornare indietro deve
 * mostrare la pagina che si e' vista, mentre battere un indirizzo o premere un
 * collegamento e' una richiesta nuova e vuole la pagina di adesso. Una cache
 * che risponde anche a quelle mostrerebbe notizie vecchie senza dirlo. */
/* ! IL CORPO DI UN POST SI POSA PRIMA DI CHIAMARE `vai`, e non si aggiunge un
 * quarto argomento a una funzione che ha gia' otto chiamanti: sarebbe un `0`
 * in piu' in otto posti, e il nono che si dimentica passerebbe spazzatura.
 * `vai` lo prende, lo usa e lo azzera — un POST non si ripete. */
static const char *g_da_postare = 0;

static void vai(const char *url, int in_storia, int usa_cache)
{
    ExHttpEsito  e;
    /* ! CENTOSESSANTA BYTE NON BASTAVANO, E IL MODO DI SCOPRIRLO E' STATO IL
     * PEGGIORE. Con tutte le note accese — «copia locale», pagina troncata,
     * albero troncato, stile troncato — piu' i quattro contatori, la riga
     * supera i centosessanta caratteri: `sprintf` scriveva oltre il buffer,
     * cioe' sopra l'indirizzo di ritorno, e il browser saltava a 0x00000005.
     * Si vedeva SOLO sulle pagine grandi, che sono le uniche che accendono le
     * note. E si e' cercato prima nel riuso delle connessioni, che era appena
     * arrivato e non c'entrava niente. */
    char         msg[320];
    unsigned int n = 0;
    int          da_cache = 0;

    if (!url || !url[0]) { g_da_postare = 0; return; }

    /* ! UNA PAGINA MANDATA IN POST NON SI PRENDE DALLA CACHE, ne' ci finisce
     * dentro: la risposta a un invio dipende da cosa si e' inviato, e servirla
     * a un altro invio sarebbe la risposta sbagliata. */
    if (g_da_postare) usa_cache = 0;

    dico("sto scaricando...");
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);

    memset(&e, 0, sizeof(e));

    if (usa_cache && cache_leggi(url, g_pagina, sizeof(g_pagina), &n)) {
        e.codice = 200;
        e.byte   = n;
        strncpy(e.finale, url, sizeof(e.finale) - 1);
        da_cache = 1;
    } else if (g_da_postare
               ? !exhttp_posta(url, g_da_postare, g_pagina, sizeof(g_pagina), &e)
               : !exhttp_prendi(url, g_pagina, sizeof(g_pagina), &e)) {
        /* =====================================================================
         * ! LA RETE NON RISPONDE: SE LA COPIA SU DISCO C'E', VALE. E' il motivo
         * per cui una cache che sopravvive ai riavvii serve davvero — la pagina
         * di ieri e' meglio di una finestra vuota.
         *
         * ! MA SI DICE CHE E' DI IERI, e questa e' la meta' che conta. Una
         * copia vecchia servita come se fosse quella di adesso e' peggio di un
         * errore: chi legge prende decisioni su un'informazione che crede
         * fresca. Nella riga di stato compare «(copia locale: la rete non
         * risponde)», e non e' una nota, e' il verdetto.
         * ===================================================================== */
        if (cache_leggi(url, g_pagina, sizeof(g_pagina), &n)) {
            memset(&e, 0, sizeof(e));
            e.codice = 200;
            e.byte   = n;
            strncpy(e.finale, url, sizeof(e.finale) - 1);
            da_cache = 2;
        } else {
            sprintf(msg, "%s: %s", url, e.errore[0] ? e.errore : "non riuscito");
            dico(msg);
            return;
        }
    } else if (!e.troncata) {
        /* ! LA CHIAVE E' `finale`, NON L'INDIRIZZO CHIESTO, perche' e' li' che
         * il contenuto sta davvero: dopo una redirezione i due sono diversi, e
         * `indietro` cerchera' proprio `finale` — e' quello che finisce nella
         * storia. */
        cache_scrivi(e.finale, g_pagina, e.byte);
    }

    if (in_storia && g_storia_n < STORIA_MAX && g_qui[0]) {
        strncpy(g_storia[g_storia_n], g_qui, EXHTTP_URL_MAX - 1);
        g_storia[g_storia_n][EXHTTP_URL_MAX - 1] = '\0';
        g_storia_n++;
    }

    strncpy(g_qui, e.finale, sizeof(g_qui) - 1);
    g_qui[sizeof(g_qui) - 1] = '\0';
    ex_testo_metti(g_url, g_qui);

    /* ! LE IMMAGINI DELLA PAGINA DI PRIMA SE NE VANNO QUI, prima che l'albero
     * cambi: dopo html_analizza gli indici dei nodi sono di un altro documento
     * e non vogliono piu' dire niente. */
    imm_libera_tutte();

    html_prepara(&g_doc, g_nodi, NODI_MAX, g_attr, ATTR_MAX,
                 g_arena, ARENA_MAX);
    html_analizza(&g_doc, (const char *)g_pagina, e.byte);
    g_arena_doc = g_doc.arena_n;    /* da qui in poi c'e' il testo generato */

    g_scorri = 0;
    g_ctrl_n = 0;
    g_ctrl_fuoco = -1;
    g_opz_n = 0;

    /* ! E GLI SLOT SI DICHIARANO DI NESSUNO, o il primo giro sulla pagina
     * NUOVA troverebbe li' dentro i numeri di nodo della pagina VECCHIA. Sono
     * indici in un albero che non esiste piu': uno di loro puo' benissimo
     * combaciare per caso con un nodo di adesso, e allora il campo si
     * riempirebbe con quel che era stato scritto su un altro sito. */
    {
        int q;

        for (q = 0; q < CTRL_MAX; q++) g_ctrl[q].nodo = -1;
    }

    raccogli_css();
    impagina();

    /* ! E SI SCRIVE CON snprintf, non con sprintf: la riga qui sotto e' fatta
     * di pezzi che dipendono dalla pagina, e nessuno di loro ha una lunghezza
     * che si possa contare guardando il codice.
     *
     * ! QUANDO SI TRONCA SI DICE ANCHE DI QUANTO, e non e' vanita' di numeri:
     * «albero troncato» non dice se manca un tetto di poco o di molto, e i
     * tetti di questo browser sono quattro. Con i numeri davanti si sa quale
     * alzare — e si sa anche quando NON serve alzare niente. */
    if (g_doc.troncato || g_css.troncato || e.troncata) {
        snprintf(msg, sizeof(msg),
                 "%d, %u byte%s%s%s%s  [nodi %u/%u, testo %uK/%uK, "
                 "pezzi %u/%u]",
                e.codice, e.byte,
                da_cache == 2 ? " (copia locale: la rete non risponde)"
                              : da_cache ? " (dalla cache)" : "",
                e.troncata ? " pagina troncata" : "",
                g_doc.troncato ? " albero troncato" : "",
                g_css.troncato ? " stile troncato" : "",
                g_doc.nodi_n, (unsigned int)NODI_MAX,
                g_doc.arena_n / 1024u, (unsigned int)(ARENA_MAX / 1024u),
                (unsigned int)g_pez_n, (unsigned int)PEZZI_MAX);
    } else {
        snprintf(msg, sizeof(msg), "%d, %u byte, %u nodi%s",
                 e.codice, e.byte, g_doc.nodi_n,
                 da_cache == 2 ? " (copia locale: la rete non risponde)"
                               : da_cache ? " (dalla cache)" : "");
    }
    dico(msg);

    disegna();

    /* Il testo si vede: adesso, e solo adesso, si va a prendere il resto. */
    immagini_prendi();

    /* ! E SE QUALCHE IMMAGINE E' RIMASTA FUORI LO SI DICE QUI, COI NUMERI, con
     * la stessa regola dell'albero troncato: «manca un'immagine» non dice se
     * il tetto va alzato di poco o se quella pagina non ci starebbe mai. Al
     * suo posto si legge il suo `alt`, non un riquadro vuoto.
     *
     * ! E VA SCRITTO QUI E NON DENTRO `immagini_prendi`, perche' la riga qui
     * sotto rimette la riga di stato della pagina: un `dico` la' dentro
     * verrebbe cancellato un'istruzione dopo — che e' esattamente quello che
     * succedeva. */
    if (g_imm_fuori > 0) {
        int l = (int)strlen(msg);

        snprintf(msg + l, sizeof(msg) - (size_t)l,
                 "  [%d immagini fuori: pixel %uK/%uK]",
                 g_imm_fuori, g_imm_px / 1024u, g_imm_px_tot / 1024u);
    }
    dico(msg);
    g_da_postare = 0;
}

/* Un collegamento premuto: si risolve contro l'indirizzo di adesso. */
static void segui(int k)
{
    char nuovo[EXHTTP_URL_MAX];

    if (k < 0 || k >= g_link_n) return;
    if (!risolvi(link_url(k), nuovo, sizeof(nuovo))) return;

    vai(nuovo, 1, 0);
}

/* Quale collegamento sta sotto quel punto, o -1. */
static int link_sotto(int x, int y)
{
    int i;

    for (i = 0; i < g_pez_n; i++) {
        int py = g_pez[i].y - g_scorri;
        int h  = g_pez[i].img >= 0 ? g_pez[i].h
                                   : ex_font_altezza(g_pez[i].font);

        if (g_pez[i].link < 0) continue;
        if (x >= g_pez[i].x && x < g_pez[i].x + g_pez[i].w &&
            y >= py && y < py + h) return g_pez[i].link;
    }
    return -1;
}

/* Due controlli sono dello stesso gruppo se portano lo stesso `name`. Due
 * nomi vuoti contano come lo stesso gruppo: e' quello che fa un browser vero
 * con dei radio senza nome, e sono comunque un modulo scritto male. */
static int confronta_nome(const char *a, const char *b)
{
    int i = 0;

    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

/* =============================================================================
 * MANDARE UN MODULO
 *
 * ! LA CODIFICA PERCENTO NON E' UN DETTAGLIO. Un campo che contiene uno
 * spazio, una `&` o un `=` va scritto in modo che il server non lo scambi per
 * la struttura della query: senza, cercare «pane & vino» manda due campi
 * invece di uno, e il secondo si chiama « vino». La regola e' semplice —
 * lettere, cifre e quattro segni passano, lo spazio diventa `+`, tutto il
 * resto diventa %XX — ed e' la stessa da trent'anni.
 *
 * ! SI MANDANO SOLO I CAMPI CON UN `name`, e le spunte solo se accese: e' cio'
 * che il server si aspetta. Una spunta spenta non si manda affatto — non si
 * manda «no», si tace — ed e' il motivo per cui i moduli hanno spesso un campo
 * nascosto accanto.
 * ========================================================================== */
static int esadecimale(int v) { return v < 10 ? '0' + v : 'A' + v - 10; }

static int aggiungi_codificato(char *out, int pos, int max, const char *s)
{
    int i;

    for (i = 0; s[i]; i++) {
        unsigned char ch = (unsigned char)s[i];

        if (pos + 4 >= max) return pos;

        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out[pos++] = (char)ch;
        } else if (ch == ' ') {
            out[pos++] = '+';
        } else {
            out[pos++] = '%';
            out[pos++] = (char)esadecimale(ch >> 4);
            out[pos++] = (char)esadecimale(ch & 15);
        }
    }
    return pos;
}

static void manda_modulo(int m)
{
    char q[EXHTTP_URL_MAX];
    char meta[EXHTTP_URL_MAX];
    int  pos = 0, primo = 1, i;

    if (m < 0 || m >= g_mod_n) {
        dico("questo campo non sta dentro nessun modulo");
        return;
    }

    for (i = 0; i < g_ctrl_n && pos < (int)sizeof(q) - 8; i++) {
        const Ctrl *c = &g_ctrl[i];

        if (c->modulo != m) continue;
        if (c->nome[0] == '\0') continue;
        if (c->tipo == CTRL_PULSANTE) continue;
        if ((c->tipo == CTRL_SPUNTA || c->tipo == CTRL_RADIO) && !c->acceso)
            continue;

        if (!primo) q[pos++] = '&';
        primo = 0;

        pos = aggiungi_codificato(q, pos, (int)sizeof(q), c->nome);
        q[pos++] = '=';
        /* Una spunta accesa senza valore vale «on», come ovunque. */
        pos = aggiungi_codificato(q, pos, (int)sizeof(q),
                (c->valore[0] || c->tipo == CTRL_TESTO || c->tipo == CTRL_AREA)
                ? c->valore : "on");
    }
    q[pos] = '\0';

    /* L'azione vuota vuol dire «questa stessa pagina». */
    if (g_mod[m].azione[0] == '\0') {
        int k = 0;

        while (g_qui[k] && g_qui[k] != '?' && k < (int)sizeof(meta) - 1) {
            meta[k] = g_qui[k]; k++;
        }
        meta[k] = '\0';
    } else {
        int k = 0;

        while (g_mod[m].azione[k] && k < (int)sizeof(meta) - 1) {
            meta[k] = g_mod[m].azione[k]; k++;
        }
        meta[k] = '\0';
    }

    /* ! LA QUERY CHE C'E' GIA' NELL'AZIONE SI BUTTA, e non si mescola: la
     * specifica dice che i campi del modulo SOSTITUISCONO la query
     * dell'action, e tenere tutt'e due darebbe due volte lo stesso parametro. */
    {
        int k = 0;

        while (meta[k] && meta[k] != '?') k++;
        meta[k] = '\0';
    }

    {
        char nuovo[EXHTTP_URL_MAX];
        int  k = 0;

        while (meta[k] && k < (int)sizeof(nuovo) - 2) { nuovo[k] = meta[k]; k++; }

        /* ! IN GET I CAMPI VANNO NELL'INDIRIZZO, IN POST NEL CORPO, ed e' la
         * sola differenza che il browser deve conoscere: il resto lo fa
         * exhttp. Con POST l'indirizzo resta pulito — che e' anche il motivo
         * per cui un modulo di pagamento non usa GET: la query finisce nella
         * cronologia e nei log del server. */
        if (!g_mod[m].post && pos > 0 && k < (int)sizeof(nuovo) - 2) {
            int j = 0;

            nuovo[k++] = '?';
            while (q[j] && k < (int)sizeof(nuovo) - 1) nuovo[k++] = q[j++];
        }
        nuovo[k] = '\0';

        {
            static char corpo[EXHTTP_URL_MAX];
            char assoluto[EXHTTP_URL_MAX];
            int  j = 0;

            if (!risolvi(nuovo, assoluto, sizeof(assoluto))) {
                dico("l'indirizzo del modulo non si capisce");
                return;
            }

            if (g_mod[m].post) {
                while (q[j] && j < (int)sizeof(corpo) - 1) { corpo[j] = q[j]; j++; }
                corpo[j] = '\0';
                g_da_postare = corpo;
            }

            ex_testo_metti(g_url, assoluto);
            vai(assoluto, 1, 0);
        }
    }
}

/* =============================================================================
 * L'ELENCO A TENDINA DI UN <select>
 *
 * ! E' UNA FINESTRA MODALE, com'e' un dialogo, e non un rettangolo disegnato
 * sopra la pagina. Un rettangolo disegnato bisognerebbe ritagliarlo, farlo
 * scorrere con la pagina, ridisegnarlo a ogni movimento e togliergli i tasti a
 * mano: sono tutte cose che il server a finestre sa gia' fare. Una finestra
 * costa qualche riga in piu' qui e nessuna nel disegno.
 *
 * ! E IL CICLO ANNIDATO E' L'IDIOMA DI exdlg, non un'invenzione: si continua a
 * chiamare ex_prendi_msg/ex_smista finche' qualcuno non dichiara di aver
 * finito. Il resto dell'applicazione resta viva — ridisegna, risponde — e
 * quando l'elenco si chiude si torna dove si era.
 * ========================================================================== */
#define ID_TENDINA_OK   700

static ExFinestra g_tendina_lista;
static int        g_tendina_fatto;      /* 0 = aperta, 1 = scelto, 2 = via */

static long tendina_proc(ExFinestra f, unsigned int msg, unsigned int wp,
                         long lp)
{
    switch (msg) {
    case EXM_CHIUDI:
        g_tendina_fatto = 2;
        return 0;

    /* ! LA SCELTA SI LEGGE ALLA FINE, NON QUI DENTRO, ed e' una lezione che
     * costa poco impararla e molto no: il messaggio che dice «ho scelto» e
     * l'aggiornamento della riga scelta dentro il controllo sono due cose, e
     * l'ordine fra loro non e' scritto da nessuna parte. Leggendo qui si
     * prendeva a volte la riga di PRIMA — e il difetto compariva a
     * intermittenza, che e' il peggio. Qui si dice solo CHE si e' finito. */
    case EXM_COMANDO:
        if (wp == ID_TENDINA_OK || wp == 1) g_tendina_fatto = 1;
        return 0;

    case EXM_TASTO:
        if ((wp & 0xFFFF) == '\n' || (wp & 0xFFFF) == '\r') g_tendina_fatto = 1;
        else if ((wp & 0xFFFF) == 27)                         g_tendina_fatto = 2;
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }
}

/* Apre l'elenco delle opzioni di `k` e rende quella scelta, o -1. */
static int tendina(int k, int x, int y)
{
    Ctrl        *c = &g_ctrl[k];
    ExFinestra   f;
    ExMsg        m;
    unsigned int sw = 0, sh = 0;
    int          h, i, scelto = -1;

    if (c->opz_n <= 0) return -1;

    /* Alta quanto le opzioni, entro un tetto: un <select> con cento voci non
     * deve diventare una finestra piu' alta dello schermo. */
    h = 44 + c->opz_n * 16;
    if (h > 300) h = 300;

    ex_schermo(&sw, &sh);
    if (x + 240 > (int)sw) x = (int)sw - 240;
    if (y + h > (int)sh)   y = (int)sh - h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    g_tendina_fatto = 0;

    f = ex_crea("finestra", "Scegli",
                EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_SOPRA | EX_MODALE,
                x, y, 240, h, 0, 0, tendina_proc);
    if (f == 0) return -1;

    g_tendina_lista = ex_crea("lista", "", EX_FIGLIO, 6, 24, 228, h - 30,
                              f, 1, 0);
    if (g_tendina_lista == 0) { ex_distruggi(f); return -1; }

    for (i = 0; i < c->opz_n; i++)
        ex_lista_aggiungi(g_tendina_lista, g_opz[c->opz_primo + i]);
    ex_lista_scegli(g_tendina_lista, (unsigned int)c->opz_ora);
    ex_fuoco(g_tendina_lista);

    ex_procedura_base(f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(f);

    while (!g_tendina_fatto && ex_prendi_msg(&m)) ex_smista(&m);

    if (g_tendina_fatto == 1) scelto = (int)ex_lista_scelta(g_tendina_lista);
    ex_distruggi(f);

    /* ! LA PAGINA SI RIDISEGNA DOPO, SEMPRE. La finestra che se ne va lascia
     * il suo buco: il server ridisegna cio' che stava sotto solo se qualcuno
     * glielo chiede, e quel qualcuno e' chi ha aperto la finestra. */
    disegna();
    return scelto;
}

/* Quale controllo sta sotto quel punto, o -1. */
static int ctrl_sotto(int x, int y)
{
    int i;

    for (i = 0; i < g_pez_n; i++) {
        int py = g_pez[i].y - g_scorri;

        if (g_pez[i].ctrl < 0) continue;
        if (x >= g_pez[i].x && x < g_pez[i].x + g_pez[i].w &&
            y >= py && y < py + g_pez[i].h) return g_pez[i].ctrl;
    }
    return -1;
}

static int scorri_max(void)
{
    int max = g_altezza - area_h();

    return (max < 0) ? 0 : max;
}

/* Il pollice: dove comincia e quanto e' alto, dentro la corsa. */
static void pollice(int *py, int *ph)
{
    int corsa = area_h();
    int max   = scorri_max();
    int h, y;

    if (max == 0) { *py = area_y(); *ph = corsa; return; }

    /* Alto in proporzione a quanto si vede del documento. */
    h = (int)(((long)corsa * corsa) / (long)g_altezza);
    if (h < SCORRI_MIN) h = SCORRI_MIN;
    if (h > corsa)      h = corsa;

    y = (int)(((long)g_scorri * (corsa - h)) / (long)max);
    *py = area_y() + y;
    *ph = h;
}

static void disegna_barra(void)
{
    int x = barra_x(), y = area_y(), h = area_h();
    int py, ph;

    /* La corsa: incavata, come una scanalatura. */
    ex_riempi(g_f, x, y, SCORRI_W, h, EX_GRIGIO_SC);
    ex_incavo(g_f, x, y, SCORRI_W, h);

    /* ! SENZA NIENTE DA SCORRERE NON SI DISEGNA IL POLLICE. Un pollice che
     * riempie tutta la corsa e non si muove sembra bloccato; la scanalatura
     * vuota si legge subito come «ci sta tutto». */
    if (scorri_max() == 0) return;

    pollice(&py, &ph);
    ex_riempi(g_f, x + 1, py, SCORRI_W - 2, ph, EX_GRIGIO);
    ex_rilievo(g_f, x + 1, py, SCORRI_W - 2, ph);
}

static void scorri(int quanto)
{
    int max = scorri_max();

    g_scorri += quanto;
    if (g_scorri < 0) g_scorri = 0;
    if (g_scorri > max) g_scorri = max;
    disegna();
}

/* Porta la cima della finestra a `y` del documento. */
static void scorri_a(int y)
{
    int max = scorri_max();

    if (y < 0) y = 0;
    if (y > max) y = max;
    if (y == g_scorri) return;
    g_scorri = y;
    disegna();
}

/* ! IL TRASCINAMENTO SI RICORDA DA DOVE HA PRESO IL POLLICE, e non e' un
 * dettaglio: senza, il pollice salta col centro sotto il puntatore al primo
 * pixel di movimento, e la pagina fa un balzo. Si tiene lo scarto fra il punto
 * cliccato e la cima del pollice, e lo si sottrae sempre. */
static int g_trascino = 0;
static int g_trascino_dy = 0;

static int nella_barra(int x, int y)
{
    return x >= barra_x() && x < barra_x() + SCORRI_W &&
           y >= area_y()  && y < area_y() + area_h();
}

/* Rende 1 se il clic era suo. */
static int barra_giu(int x, int y)
{
    int py, ph;

    if (!nella_barra(x, y)) return 0;
    if (scorri_max() == 0) return 1;        /* suo, ma non c'e' niente da fare */

    pollice(&py, &ph);

    if (y >= py && y < py + ph) {
        g_trascino    = 1;
        g_trascino_dy = y - py;
        return 1;
    }

    /* Sopra o sotto il pollice: una schermata per volta, come ovunque. */
    scorri(y < py ? -(area_h() - 24) : (area_h() - 24));
    return 1;
}

static void barra_mosso(int y)
{
    int corsa = area_h();
    int max   = scorri_max();
    int py, ph;

    if (!g_trascino || max == 0) return;

    pollice(&py, &ph);
    if (corsa == ph) return;

    scorri_a((int)(((long)(y - g_trascino_dy - area_y()) * max) /
                   (long)(corsa - ph)));
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CHIUDI:
        ex_esci(0);
        return 0;

    case EXM_COMANDO:
        if (wp == ID_VAI) {
            const char *t = ex_testo_prendi(g_url);

            if (t && t[0]) vai(t, 1, 0);
            return 0;
        }
        if (wp == ID_INFO) {
            char t[640];

            exinfo_testo(t, sizeof(t), "Navigatore", VERSIONE_APP,
                         "Il browser di EX-OS.  Mette insieme exhttp per la "
                         "rete, exhtml per l'albero, excss per i fogli di "
                         "stile, eximg per le immagini e i font per misurare "
                         "e disegnare il testo.  http e https (TLS 1.3, "
                         "certificati verificati).  Niente JavaScript.");
            ex_dlg_avviso("Informazioni su", t);
            return 0;
        }
        if (wp == ID_INDIETRO) {
            if (g_storia_n > 0) {
                char indietro[EXHTTP_URL_MAX];

                g_storia_n--;
                strncpy(indietro, g_storia[g_storia_n], sizeof(indietro) - 1);
                indietro[sizeof(indietro) - 1] = '\0';
                vai(indietro, 0, 1);
            }
            return 0;
        }
        return 0;

    case EXM_TASTO: {
        unsigned int c = wp & 0xFFFF;

        /* ! CON UNA CASELLA A FUOCO I TASTI SONO SUOI, e le frecce non
         * scorrono piu' la pagina: e' la regola di ogni browser, e senza di
         * essa scrivere in un campo farebbe saltare il documento. */
        if (g_ctrl_fuoco >= 0) {
            Ctrl *k = &g_ctrl[g_ctrl_fuoco];
            int   n = 0;

            while (k->valore[n]) n++;

            /* Esc o Tab: si esce dalla casella, e il fuoco torna dov'era. */
            if (c == 27 || c == '\t') {
                g_ctrl_fuoco = -1;
                ex_fuoco(g_url);
                disegna();
                return 0;
            }

            if (k->cur > (short)n) k->cur = (short)n;
            if (k->cur < 0)        k->cur = 0;

            /* =================================================================
             * ! IL CURSORE SI MUOVE, e non e' un lusso. Prima si scriveva solo
             * in coda e si cancellava solo dalla coda: per correggere la prima
             * lettera di un indirizzo di posta bisognava cancellare tutto il
             * resto. E' il genere di cosa che non si nota leggendo il codice e
             * si nota alla prima riga digitata storta.
             *
             * ! DENTRO UN'AREA LE FRECCE SU E GIU' CAMBIANO RIGA, e non
             * scorrono la pagina: mentre si scrive in un'area, la pagina sotto
             * non deve muoversi. Fuori da una casella tornano a scorrere, ed e'
             * il ramo piu' sotto.
             * ================================================================= */
            if (c == KBD_K_LEFT)  { if (k->cur > 0) k->cur--; disegna(); return 0; }
            if (c == KBD_K_RIGHT) { if (k->cur < (short)n) k->cur++; disegna(); return 0; }
            if (c == KBD_K_HOME)  { k->cur = 0;          disegna(); return 0; }
            if (c == KBD_K_END)   { k->cur = (short)n;   disegna(); return 0; }

            if (c == KBD_K_UP || c == KBD_K_DOWN) {
                if (k->tipo == CTRL_AREA) {
                    int i, ini = 0, col, prec = -1, succ = -1;

                    /* L'inizio della riga di adesso, e quelle intorno. */
                    for (i = 0; i < (int)k->cur; i++)
                        if (k->valore[i] == '\n') { prec = ini; ini = i + 1; }
                    col = (int)k->cur - ini;
                    for (i = (int)k->cur; k->valore[i]; i++)
                        if (k->valore[i] == '\n') { succ = i + 1; break; }

                    if (c == KBD_K_UP && prec >= 0) ini = prec;
                    else if (c == KBD_K_DOWN && succ >= 0) ini = succ;
                    else { disegna(); return 0; }

                    for (i = 0; i < col && k->valore[ini + i] &&
                                k->valore[ini + i] != '\n'; i++) { }
                    k->cur = (short)(ini + i);
                    disegna();
                    return 0;
                }
                /* In una casella di una riga sola non c'e' dove andare. */
                return 0;
            }

            if (c == '\b') {
                if (k->cur > 0) {
                    int i;

                    for (i = (int)k->cur - 1; i < n; i++)
                        k->valore[i] = k->valore[i + 1];
                    k->cur--;
                }
                disegna();
                return 0;
            }
            if (c == KBD_K_DEL) {
                if ((int)k->cur < n) {
                    int i;

                    for (i = (int)k->cur; i < n; i++)
                        k->valore[i] = k->valore[i + 1];
                }
                disegna();
                return 0;
            }
            /* ! INVIO DENTRO UNA CASELLA MANDA IL MODULO, ed e' cosi' che si
             * usa una casella di ricerca: nessuno cerca il pulsante.
             *
             * ! MA DENTRO UN'AREA VA A CAPO, e la differenza non e' un
             * dettaglio: un'area di testo esiste PER contenere piu' righe, e
             * un Invio che manda il modulo a meta' della seconda riga e' il
             * modo piu' rapido di perdere quello che si stava scrivendo. Il
             * modulo lo si manda col pulsante, come in ogni browser. */
            if (c == '\n' || c == '\r') {
                if (k->tipo == CTRL_AREA) {
                    if (n < CTRL_VAL_MAX - 1) {
                        int i;

                        for (i = n; i >= (int)k->cur; i--)
                            k->valore[i + 1] = k->valore[i];
                        k->valore[k->cur] = '\n';
                        k->cur++;
                        disegna();
                    }
                    return 0;
                }
                {
                    int m = k->modulo;

                    g_ctrl_fuoco = -1;
                    manda_modulo(m);
                }
                return 0;
            }
            if (c >= 32 && c < 256 && n < CTRL_VAL_MAX - 1) {
                int i;

                for (i = n; i >= (int)k->cur; i--)
                    k->valore[i + 1] = k->valore[i];
                k->valore[k->cur] = (char)c;
                k->cur++;
                disegna();
            }
            return 0;
        }

        if (c == '\n' || c == '\r') {
            const char *t = ex_testo_prendi(g_url);

            if (t && t[0]) vai(t, 1, 0);
            return 0;
        }
        if (c == KBD_K_DOWN)  { scorri(24);  return 0; }
        if (c == KBD_K_UP)    { scorri(-24); return 0; }
        if (c == KBD_K_PGDN)  { scorri(area_h() - 24);  return 0; }
        if (c == KBD_K_PGUP)  { scorri(-(area_h() - 24)); return 0; }
        if (c == KBD_K_HOME)  { scorri_a(0); return 0; }
        if (c == KBD_K_END)   { scorri_a(scorri_max()); return 0; }
        return ex_procedura_base(f, msg, wp, lp);
    }

    case EXM_MOUSE_GIU: {
        int x = EX_X(lp), y = EX_Y(lp), k;

        if (barra_giu(x, y)) return 0;

        /* ! I CONTROLLI PRIMA DEI COLLEGAMENTI. Un `<input>` dentro un `<a>`
         * capita, e in quel caso vince il controllo: chi clicca dentro una
         * casella vuole scriverci, non essere portato altrove. */
        k = ctrl_sotto(x, y);
        if (k >= 0) {
            Ctrl *c = &g_ctrl[k];

            switch (c->tipo) {
            case CTRL_SPUNTA:
                c->acceso = (unsigned char)!c->acceso;
                g_ctrl_fuoco = -1;
                break;

            case CTRL_RADIO: {
                /* ! UNO SOLO ACCESO PER GRUPPO, E IL GRUPPO E' IL `name`. Si
                 * spengono le scelte che portano lo STESSO nome, non tutte
                 * quelle della pagina: un modulo con «spedizione» e
                 * «pagamento» ha due gruppi, e spegnerli insieme renderebbe
                 * impossibile rispondere a tutt'e due. */
                int j;

                for (j = 0; j < g_ctrl_n; j++)
                    if (g_ctrl[j].tipo == CTRL_RADIO &&
                        confronta_nome(g_ctrl[j].nome, c->nome))
                        g_ctrl[j].acceso = 0;
                c->acceso = 1;
                g_ctrl_fuoco = -1;
                break;
            }

            case CTRL_PULSANTE:
                g_ctrl_fuoco = -1;
                manda_modulo(c->modulo);
                return 0;

            case CTRL_SCELTA: {
                /* L'elenco si apre sotto al controllo, dove ci si aspetta. */
                int scelto = tendina(k, x, y + 20);

                if (scelto >= 0 && scelto < c->opz_n) {
                    int q = 0;
                    const char *o = g_opz[c->opz_primo + scelto];

                    c->opz_ora = (short)scelto;
                    while (o[q] && q < CTRL_VAL_MAX - 1) { c->valore[q] = o[q]; q++; }
                    c->valore[q] = '\0';
                }
                g_ctrl_fuoco = -1;
                break;
            }

            default:
                /* =========================================================
                 * ! IL FUOCO SI TOGLIE ALLA CASELLA DELL'INDIRIZZO, o i tasti
                 * non arrivano MAI qui. E' il difetto vero dietro la voce
                 * «la <textarea> non ha un cursore»: non era il cursore a
                 * mancare, erano i TASTI. `ex_fuoco(g_url)` all'avvio da il
                 * fuoco a un controllo del toolkit, e da quel momento ogni
                 * tasto e' suo — i controlli della PAGINA non sono finestre
                 * del toolkit, quindi non possono averlo e non ricevevano
                 * niente. Si scriveva nella barra dell'indirizzo credendo di
                 * scrivere nel modulo.
                 *
                 * Dandolo alla finestra si toglie a ogni suo figlio, e i tasti
                 * tornano al nostro gestore, che sa dei controlli disegnati.
                 * ========================================================= */
                ex_fuoco_via(g_f);
                g_ctrl_fuoco = k;
                break;
            }
            disegna();
            return 0;
        }

        /* Un clic fuori da ogni casella toglie il fuoco: e' quello che si
         * aspetta chi ha finito di scrivere. */
        if (g_ctrl_fuoco >= 0) { g_ctrl_fuoco = -1; disegna(); }

        k = link_sotto(x, y);
        if (k >= 0) { segui(k); return 0; }
        return 0;
    }

    case EXM_MOUSE_MOSSO:
        barra_mosso(EX_Y(lp));
        return 0;

    case EXM_MOUSE_SU:
        g_trascino = 0;
        return 0;

    case EXM_DISEGNA:
        disegna();
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }
}

int main(int argc, char **argv)
{
    ExMsg m;

    g_f = ex_crea("finestra", "Navigatore", EX_TITOLO | EX_BORDO | EX_CHIUDI,
                  EX_AUTO, EX_AUTO, FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) {
        printf("browser: il server a finestre non risponde.\n");
        printf("         Avvialo con:  exwin\n");
        return 1;
    }

    /* ! IL FONT E' PROPORZIONALE SE C'E', ALTRIMENTI QUELLO DI SISTEMA, e non
     * si muore per un file mancante: ex_font_apri rende 0, che E' il font di
     * sistema. Una pagina con un carattere diverso e' meglio di un browser che
     * non parte. */
    g_font_testo  = ex_font_apri("/exwin/font/LiberationSerif-Regular.ttf", 15);
    g_font_titolo = ex_font_apri("/exwin/font/LiberationSans-Bold.ttf", 22);

    ex_crea("pulsante", "<", EX_FIGLIO, MARGINE, 4, 26, 22,
            g_f, ID_INDIETRO, 0);

    /* ! I DUE PULSANTI DI DESTRA SI MISURANO DALLA DESTRA, non dalla
     * sinistra: cosi' aggiungerne uno sposta solo il campo dell'indirizzo, che
     * e' l'unico pezzo che puo' restringersi senza diventare inutile. */
    ex_crea("pulsante", "?", EX_FIGLIO, FIN_W - MARGINE - 24, 4, 24, 22,
            g_f, ID_INFO, 0);
    ex_crea("pulsante", "Vai", EX_FIGLIO, FIN_W - MARGINE - 24 - 4 - 44, 4,
            44, 22, g_f, ID_VAI, 0);
    g_url = ex_crea("testo", "", EX_FIGLIO, MARGINE + 32, 4,
                    FIN_W - MARGINE - 24 - 4 - 44 - 4 - (MARGINE + 32), 22,
                    g_f, ID_URL, 0);

    g_stato = ex_crea("etichetta", "", EX_FIGLIO,
                      MARGINE, FIN_H - 18, FIN_W - 2 * MARGINE, 16, g_f, 0, 0);

    /* ! IL TETTO DELLE IMMAGINI SI SCEGLIE QUI, a finestra gia' aperta: prima
     * di ex_crea la memoria che il server a finestre prendera' per questa
     * finestra e' ancora libera, e conterebbe come nostra. */
    imm_tetto_scegli();

    cache_prepara();

    ex_fuoco(g_url);
    dico("scrivi un indirizzo e premi Invio. http e https.");
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    disegna();

    if (argc >= 2) { ex_testo_metti(g_url, argv[1]); vai(argv[1], 0, 0); }

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}

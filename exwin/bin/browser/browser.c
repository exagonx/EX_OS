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
#include "exjs.h"
#include "exdom.h"
#include "exdlg.h"
#include "exinfo.h"
#include "kbd_proto.h"
#include "biscotti.h"

/* +0.001 a ogni modifica: `browser -version` la stampa. Vedi EX_VERSIONE in libc.h. */
#define VERSIONE_APP "0.002"
EX_VERSIONE("browser", VERSIONE_APP);

#define FIN_W       760

/* ! LA BARRA DEI MENU NON RESTRINGE L'AREA DEL CLIENT: il toolkit la mette in
 * cima e larga quanto la finestra, ma il posto glielo deve lasciare chi scrive
 * il programma. Venti pixel sono MENU_BARRA_H di lib/exwin/exwin.c, e sono
 * l'unica misura di questo file che non decide questo file. */
#define MENU_H      20
#define FIN_H       (520 + MENU_H)

#define BARRA_H     30          /* la riga dell'indirizzo, sotto i menu */
#define BARRA_Y     MENU_H      /* dove comincia */
#define MARGINE     8

/* Quanto lungo puo' essere un percorso: e' la misura di ExDlg, e i due
 * dialoghi sono gli unici che ne producono. */
#define PERC_MAX    192

#define ID_URL      1
#define ID_VAI      2
#define ID_INDIETRO 3
#define ID_INFO     4
#define ID_APRI     5
#define ID_SALVA    6
#define ID_ESCI     7
#define ID_AIUTO    8
#define ID_DOC      9
#define ID_HOME     10
#define ID_IMPOST   11

/* La schermata delle impostazioni ha i propri, lontani da quelli della
 * finestra principale: sono due finestre e due procedure, ma un id ripetuto
 * si confonde leggendo. */
#define ID_IMP_HOME    720
#define ID_IMP_ORA     721
#define ID_IMP_JS      722
#define ID_IMP_IMG     723
#define ID_IMP_CACHE   724
#define ID_IMP_MOTORE  727
#define ID_IMP_SALVA   725
#define ID_IMP_ANNULLA 726

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
 *     i pezzi impaginati            844 KB   (24000 x 36)
 *     gli indirizzi dei link        200 KB   (arena + scostamenti)
 *     la cronologia                  19 KB
 *     le immagini                 128 KB + il tetto scelto all'avvio
 *                                  -------
 *                                   4114 KB piu' i pixel
 *
 * ! E IL MOTORE JAVASCRIPT NON E' IN QUESTO CONTO, perche' non e' un tetto
 * fisso: si chiede alla libc quando una pagina ha davvero uno script, e si
 * rende quando la pagina se ne va. Una pagina senza script non lo paga — e
 * sono ancora la maggioranza di quelle che questo browser riesce ad aprire.
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

    /* ! E IL NODO DA CUI VIENE, che prima non serviva a nessuno e adesso e'
     * l'unico modo di dire a uno script DOVE si e' cliccato. Sono quattro byte
     * per pezzo, novantasei chilobyte al tetto — e l'alternativa era ricavare
     * il nodo dalla posizione ripercorrendo l'albero a ogni clic, cioe' una
     * seconda impaginazione per sapere una cosa che la prima aveva in mano.
     *
     * ! PER IL TESTO CI SI METTE IL PADRE, non il nodo di testo. E' quel che
     * fa il browser vero: `event.target` di un clic su una parola e'
     * l'elemento che la contiene, e uno script che leggesse `target.tagName`
     * su un nodo di testo troverebbe `undefined`. */
    int          nodo;
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
/* ! CENTONOVANTADUE DA QUANDO I CAMPI NASCOSTI CONTANO. Sessantaquattro
 * bastavano a un modulo che si vede — nessuno chiede a una persona di
 * riempire sessanta caselle — ma i campi nascosti non li riempie nessuno e non
 * si contano a occhio: la pagina di consenso di google.com ne ha cinquantacinque
 * sparsi in cinque moduli, piu' i pulsanti. Con il tetto vecchio l'ultimo
 * modulo restava senza meta' dei suoi campi, e la richiesta partiva incompleta
 * senza che niente lo dicesse. */
#define CTRL_MAX        192
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
/* ! «NON SI VEDE» NON VUOL DIRE «NON C'E'», ed e' costato un pomeriggio.
 * `<input type="hidden">` qui si SALTAVA — c'era scritto «non si vede», ed era
 * vero e irrilevante: un campo nascosto non si disegna, ma e' meta' di quel
 * che un modulo manda. I gettoni contro la falsificazione delle richieste, gli
 * identificativi di sessione, il «continua a» di una pagina di consenso: sono
 * tutti nascosti. Il modulo di google.com/consent ne ha tredici e nient'altro,
 * e la POST partiva vuota — il server rispondeva «400, richiesta malformata» e
 * dallo schermo non c'era modo di capire perche'. */
#define CTRL_NASCOSTO   6       /* input type=hidden: si manda, non si vede */

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
    /* ! L'ANCORA DELLA SELEZIONE, -1 quando non c'e' niente di scelto. La
     * selezione e' il tratto fra `sel` e `cur`, in un verso o nell'altro: chi
     * la usa ordina i due estremi. Tenere l'ancora invece di «inizio e fine»
     * e' cio' che fa muovere l'estremo giusto quando si allarga con Shift. */
    short         sel;
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

/* ! QUANTI BYTE HA LA PAGINA DI ADESSO, e fino a ieri non lo sapeva nessuno:
 * la misura viveva dentro `vai()` e moriva con lei, perche' l'unica cosa che
 * ne aveva bisogno — l'analizzatore — la riceveva li' per argomento. «Salva»
 * la vuole da fuori, e senza scriverebbe il buffer INTERO: un megabyte, di cui
 * l'HTML e' la prima parte e il resto e' la pagina di prima. */
static unsigned int  g_pagina_n = 0;

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

/* =============================================================================
 * LE IMPOSTAZIONI
 *
 * ! SONO QUATTRO VARIABILI E UN FILE, e il file sta in $HOME/.app/browser/
 * come la cache — non in un percorso fisso. Un percorso fisso funziona per il
 * primo utente e scrive addosso al secondo.
 *
 * ! I VALORI PREDEFINITI SONO QUELLI DI PRIMA. Chi non ha mai aperto la
 * schermata dev'essere esattamente dove era: JavaScript acceso, immagini
 * accese, cache accesa. Un'impostazione nuova che cambia il comportamento
 * predefinito e' un difetto travestito da funzione.
 *
 * ! E LA PAGINA INIZIALE PREDEFINITA E' LA DOCUMENTAZIONE, non una pagina in
 * rete: e' l'unica che c'e' di sicuro — su una macchina appena installata,
 * senza rete configurata, una pagina iniziale che non si apre sarebbe la
 * prima cosa che il browser fa e la prima che sbaglia.
 * ============================================================================= */
static char          g_home[EXHTTP_URL_MAX] = "";
static int           g_js_acceso    = 1;
static int           g_img_accese   = 1;
static int           g_cache_accesa = 1;

/* ! QUALE MOTORE: 0 = ExJs, 1 = QuickJS. La scelta si fa PRIMA di aprire —
 * vedi exjs_motore() in exjs.h — quindi cambiarla vale dalla pagina dopo, come
 * per JavaScript acceso/spento. Il predefinito e' ExJs: e' quello che c'e'
 * sempre, mentre quickjs.so e' mezzo megabyte che si puo' non aver
 * installato. */
static int           g_qjs = 0;

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

/* ! LO STESSO ELENCO PORTA ANCHE I BORDI, e non e' pigrizia: un bordo di
 * tabella e' un rettangolo che sta SOTTO il testo e sopra lo sfondo,
 * esattamente come uno sfondo. Dargli un elenco suo avrebbe voluto dire un
 * secondo tetto da scegliere, un secondo ciclo da ritagliare all'area e un
 * secondo posto dove sbagliare l'ordine di disegno. */
typedef struct {
    int           x, y, w, h;
    unsigned int  colore;
    unsigned char bordo;    /* 0 = si riempie, >0 = contorno di tanti pixel */
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
static int area_y(void) { return BARRA_Y + BARRA_H + MARGINE; }
static int area_w(void) { return FIN_W - 2 * MARGINE - SCORRI_W; }
static int area_h(void) { return FIN_H - BARRA_Y - BARRA_H - 2 * MARGINE - 20; }

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

/* ! IL NODO DA CUI ESCE IL PEZZO CHE SI STA COLLOCANDO. Non ha bisogno del
 * salva-e-rimetti che g_link_ora si porta dietro, e la differenza vale la
 * riga: un collegamento vale per tutto quel che c'e' DENTRO, quindi va
 * ricordato risalendo; il nodo invece si scrive un istante prima di ogni
 * pezzo, nei tre soli posti dove un pezzo nasce. */
static int  g_nodo_ora = -1;

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

/* =============================================================================
 * ! IL RIPIEGO SUL FONT DI SISTEMA E' MUTO, E QUESTI DUE INTERI SONO LA SUA
 * VOCE. `ex_font_apri` rende 0 quando il file non c'e' o non si apre, e zero
 * E' il font di sistema: la pagina esce leggibile, tutta con lo stesso
 * carattere, e da nessuna parte compare il motivo. Chi guarda non vede «manca
 * un file»: vede un browser che non sa cambiare carattere — che e' una
 * diagnosi sbagliata, e porta a cercare il difetto dentro l'impaginatore.
 *
 * Contarli costa due interi e una riga in «Informazioni su», e trasforma «non
 * funziona» in «di tredici facce, tre non si aprono», che dice anche DOVE
 * guardare: /exwin/font e /exwin/lib/exfont.so.
 * ========================================================================== */
static int      g_facce_si = 0, g_facce_no = 0;

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

    /* ! E QUANDO NON SI APRE LO SI SCRIVE, col nome del file e col corpo.
     * Il conto in «Informazioni su» dice QUANTI; questo dice QUALI, e la
     * differenza si sente quando a mancare e' una faccia sola. Va sulla
     * console da cui il browser e' partito — cioe' sulla seriale, quando si
     * prova dentro QEMU. */
    if (g_font[g_font_n].f) g_facce_si++;
    else {
        g_facce_no++;
        printf("browser: carattere NON aperto: %s corpo %d\n",
               FACCIA[k], corpo);
    }

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

/* =============================================================================
 * LA SELEZIONE DENTRO UN CONTROLLO DISEGNATO
 *
 * ! IL TRATTO SI ORDINA, l'ancora no. `sel` e' dove la selezione e' COMINCIATA
 * e `cur` dov'e' arrivata: tirando all'indietro il primo e' maggiore del
 * secondo, ed e' giusto cosi' — e' l'ancora che deve restare ferma mentre
 * l'altro estremo si muove. Chi vuole il tratto se lo fa ordinare qui.
 * ========================================================================== */
static int sel_tratto(Ctrl *k, int *a, int *b)
{
    int n = 0;

    while (k->valore[n]) n++;

    if (k->sel < 0 || k->sel == k->cur) {
        /* ! SENZA SELEZIONE SI PRENDE TUTTO IL CAMPO, e non «niente»: chi
         * preme Ctrl+C in una casella senza aver scelto nulla vuole il suo
         * contenuto, non il silenzio. */
        *a = 0; *b = n;
        return n > 0;
    }

    *a = k->sel < k->cur ? k->sel : k->cur;
    *b = k->sel < k->cur ? k->cur : k->sel;
    if (*b > n) *b = n;
    if (*a > *b) *a = *b;
    return *b > *a;
}

/* Toglie di mezzo il testo scelto, se c'e'. Il cursore resta dove cominciava. */
static void sel_togli(Ctrl *k)
{
    int a, b, i, n = 0;

    if (k->sel < 0 || k->sel == k->cur) return;
    while (k->valore[n]) n++;

    a = k->sel < k->cur ? k->sel : k->cur;
    b = k->sel < k->cur ? k->cur : k->sel;
    if (b > n) b = n;

    for (i = a; i + (b - a) <= n; i++) k->valore[i] = k->valore[i + (b - a)];
    k->cur = (short)a;
    k->sel = -1;
}

/* =============================================================================
 * COPIA, TAGLIA, INCOLLA — le tre cose, in un posto solo
 *
 * ! CI SI ARRIVA DA DUE STRADE, e sono tutt'e due standard. Ctrl+C/X/V e'
 * quella che conosce chi viene da Windows o da un desktop moderno; Ctrl+Ins,
 * Shift+Ins e Shift+Canc sono quella di CUA — DOS, OS/2, i terminali Unix, e
 * ancora oggi mezzo mondo dei programmi a schermo intero. Nessuna delle due e'
 * «quella giusta»: dipende da dove uno ha imparato, e sono entrambe gratis.
 *
 * ! LE FUNZIONI STANNO QUI E I TASTI LA': se il codice fosse duplicato nei due
 * rami, il giorno che l'incolla cambia ne cambierebbe uno solo — e il difetto
 * si vedrebbe solo a chi usa l'altra scorciatoia.
 * ========================================================================== */
static void ctrl_inserisci(Ctrl *k, char ch);

static void app_copia(Ctrl *k, int taglia)
{
    int a, b;

    if (!sel_tratto(k, &a, &b)) return;
    ex_appunti_metti(k->valore + a, (unsigned int)(b - a));
    if (taglia) sel_togli(k);
}

static void app_incolla(Ctrl *k)
{
    static char  inc[CTRL_VAL_MAX];
    unsigned int q = ex_appunti_prendi(inc, sizeof(inc));
    unsigned int i;

    sel_togli(k);
    for (i = 0; i < q; i++) {
        char ch = inc[i];

        /* ! IN UNA CASELLA DI UNA RIGA SOLA UN A CAPO DIVENTA UNO SPAZIO, e
         * non si perde: incollando un indirizzo copiato da un testo su due
         * righe, quel che si vuole e' l'indirizzo intero, non la prima meta'. */
        if ((ch == '\n' || ch == '\r') && k->tipo != CTRL_AREA) ch = ' ';
        ctrl_inserisci(k, ch);
    }
}

/* Un carattere nel punto in cui si scrive. */
static void ctrl_inserisci(Ctrl *k, char ch)
{
    int i, n = 0;

    while (k->valore[n]) n++;
    if (n >= CTRL_VAL_MAX - 1) return;

    for (i = n; i >= (int)k->cur; i--) k->valore[i + 1] = k->valore[i];
    k->valore[k->cur] = ch;
    k->cur++;
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
        g_pez[g_pez_n].nodo = g_nodo_ora;
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

/* =============================================================================
 * ! IL TESTO CHE L'IMPAGINAZIONE INVENTA HA UN'ARENA SUA, E PRIMA NO — ED E'
 * STATO IL DIFETTO CHE HA ROVINATO OGNI PAGINA CON UNO SCRIPT.
 *
 * I segni degli elenchi («-», «3.») non stanno nel documento: li fabbrica
 * l'impaginazione. Finivano nell'arena del DOCUMENTO, dopo il testo vero, e
 * `impagina()` la riavvolgeva a ogni giro per non accumularli — «si butta il
 * testo generato dal giro prima». Era giusto finche' in quell'arena scriveva
 * solo l'impaginazione.
 *
 * ! DA QUANDO C'E' JAVASCRIPT, NELLA STESSA ARENA SCRIVONO ANCHE GLI SCRIPT:
 * `innerHTML`, `textContent`, `createTextNode` chiedono a exhtml, che copia
 * li' dentro. Riavvolgere buttava via IL LORO testo mentre i nodi continuavano
 * a puntarci — e la scrittura successiva ci finiva sopra. Il sintomo era
 * cattivo: il riquadro 1 mostrava un pezzo del testo del riquadro 7, e il
 * primo elemento di una lista una briciola di un'altra frase. Sembrava un
 * difetto del motore JavaScript, e lo faceva con TUTT'E DUE i motori — che e'
 * stato il modo in cui si e' capito che il motore non c'entrava.
 *
 * ! LA CURA NON E' SPOSTARE IL SEGNAPOSTO, E' NON SCRIVERE LA'. L'arena del
 * documento e' del documento; l'impaginazione ha la sua, che si azzera a ogni
 * giro perche' quel testo dura un giro. E' la stessa regola dei buffer di chi
 * chiama applicata dentro un programma solo.
 *
 * ! L'OFFSET PORTA IL BIT PIU' ALTO ACCESO per dire da quale delle due arene
 * viene. Un'arena da un megabyte non arriva a 0x80000000 nemmeno per sbaglio,
 * quindi il bit e' libero davvero — e un pezzo continua a costare quattro byte
 * di scostamento invece di un puntatore da quattro piu' un si'/no.
 * ============================================================================= */
#define GEN_MAX     (64u * 1024u)
#define GEN_BIT     0x80000000u

static char         g_gen[GEN_MAX];
static unsigned int g_gen_n = 0;

static unsigned int genera(const char *s)
{
    unsigned int inizio = g_gen_n, i = 0;

    while (s[i]) i++;
    if (g_gen_n + i + 1 > GEN_MAX) {
        g_doc.troncato = 1;         /* la barra di stato lo dira' */
        return GENERA_NIENTE;
    }

    for (i = 0; s[i]; i++) g_gen[g_gen_n++] = s[i];
    g_gen[g_gen_n++] = '\0';
    return inizio | GEN_BIT;
}

/* Il testo di un pezzo, da qualunque delle due arene venga. */
static const char *testo_pezzo(unsigned int off)
{
    if (off & GEN_BIT) return g_gen + (off & ~GEN_BIT);
    return g_arena + off;
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
        g_pez[g_pez_n].nodo = g_nodo_ora;
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
 * cosa che si legge su un sito vero e' un chilometro di JavaScript.
 *
 * ! E <noscript> DIPENDE DA COM'E' MESSO L'INTERRUTTORE, che e' l'unico tag
 * di questo elenco a non essere sempre uguale a se stesso. E' li' apposta per
 * chi il JavaScript non ce l'ha: mostrarlo COMUNQUE vuol dire che una pagina
 * con gli script accesi fa vedere due volte la stessa cosa — una dagli script
 * e una dal ripiego — o, peggio, fa vedere il ripiego di una pagina che gli
 * script hanno gia' costruito.
 *
 * ! SI E' VISTO SU google.com/search, ED ERA IL SINTOMO CHE SEMBRAVA UN
 * ALTRO. La pagina dei risultati ha TUTTO il contenuto dentro <noscript> —
 * «Se non vieni reindirizzato automaticamente entro alcuni secondi, fai clic
 * qui» — e i risultati veri li costruisce uno script. Il browser mostrava
 * quella riga e sembrava che il motore non girasse: girava, e quella riga non
 * doveva essere sullo schermo. */
static int invisibile(const char *n)
{
    return uguale(n, "script") || uguale(n, "style") || uguale(n, "head") ||
           uguale(n, "title") || uguale(n, "meta") || uguale(n, "link") ||
           (g_js_acceso && uguale(n, "noscript"));
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

/* ! IL BORDO LO DICE L'ATTRIBUTO, non il foglio di stile, e per il web vero e'
 * la scelta giusta: `<table border="1">` e' come si sono disegnate le tabelle
 * per vent'anni, e le pagine che lo usano sono le stesse che non hanno un CSS.
 * `border-collapse`, i colori e i lati separati non ci sono: un filo scuro
 * intorno a ogni cella e' quel che quell'attributo ha sempre voluto dire.
 *
 * ! E SI CAPPA A QUATTRO. `border="20"` esiste, ed e' una tabella fatta quasi
 * solo di bordo: chi la scrive vuole «spesso», non venti pixel per lato. */
static int bordo_tabella(int v)
{
    const char  *a = html_attr(&g_doc, v, "border");
    unsigned int b;

    if (!a) return 0;
    if (!a[0]) return 1;        /* `border` da solo vale «si'» */
    b = numero(a);
    if (b == 0) return 0;
    return b > 4 ? 4 : (int)b;
}

/* Un rettangolo da contornare, nello stesso elenco degli sfondi. */
static int bordo_metti(int x, int y, int w, int h, int spess)
{
    if (spess <= 0 || g_sfondi_n >= SFONDI_MAX) return -1;

    g_sfondi[g_sfondi_n].x      = x;
    g_sfondi[g_sfondi_n].y      = y;
    g_sfondi[g_sfondi_n].w      = w;
    g_sfondi[g_sfondi_n].h      = h;
    g_sfondi[g_sfondi_n].colore = EX_GRIGIO_SC;
    g_sfondi[g_sfondi_n].bordo  = (unsigned char)spess;
    return g_sfondi_n++;
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
    int      x0, y0, bordo, y_inizio, i_bordo_tab;

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
    bordo = bordo_tabella(v);
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
    y_inizio = y0;

    /* Il contorno di TUTTA la tabella: si apre adesso e si chiude in fondo,
     * quando si sa quanto e' venuta alta. */
    i_bordo_tab = -1;
    if (bordo > 0) {
        int tot = 0;

        for (c = 0; c < n_col; c++) tot += largh[c];
        tot += (n_col - 1) * TAB_SPAZIO;
        i_bordo_tab = bordo_metti(riga_x(), y0, tot, 0, bordo);
    }

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
            int      sp, rp, largh_cella, k, i_bordo;

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
                g_sfondi[g_sfondi_n].bordo  = 0;
                g_sfondi_n++;
            }

            i_bordo = bordo_metti(x0, y0, largh_cella, 0, bordo);

            impagina_in_colonna(f, &sc, x0, y0, largh_cella, 0, &alt);

            /* ! UNA CELLA CHE SCAVALCA CHIUDE IL SUO RIQUADRO DA SOLA, adesso:
             * il giro di fine riga qui sotto rimette l'altezza a tutto cio' che
             * e' ancora aperto su questa y, e per una cella alta tre righe
             * sarebbe l'altezza di UNA. */
            if (rp > 1) {
                int q;

                for (q = g_sfondi_n - 1; q >= 0; q--)
                    if ((q == i_bordo || g_sfondi[q].y == y0) &&
                        g_sfondi[q].h == 0 && g_sfondi[q].x == x0)
                        g_sfondi[q].h = alt;
            }

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

    if (i_bordo_tab >= 0) g_sfondi[i_bordo_tab].h = y0 - y_inizio;

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
        g_nodo_ora  = g_doc.nodi[v].padre;
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
                else if (uguale(tipo, "hidden"))   t = CTRL_NASCOSTO;
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
                    if (!suo || c->sel > (short)q) c->sel = -1;
                }
            }

            /* ! UN CAMPO NASCOSTO ENTRA NELL'ELENCO E NON NELL'IMPAGINAZIONE:
             * niente pezzo, niente larghezza, niente penna che avanza. Da qui
             * in giu' si parla solo di come si DISEGNA un controllo, e quello
             * non si disegna. */
            if (t == CTRL_NASCOSTO) { g_ctrl_n++; return; }

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
            g_pez[g_pez_n].nodo = v;
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

            /* Un'immagine e' un pezzo suo, quindi il nodo e' lei stessa: e'
             * il caso in cui `event.target` deve dire `IMG`. */
            g_nodo_ora = v;

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
    /* ! L'ARENA DEL DOCUMENTO NON SI RIAVVOLGE PIU', e la riga che lo faceva
     * era diventata un difetto il giorno che JavaScript ha cominciato a
     * scrivere li' dentro: vedi il commento esteso accanto a genera(). Quel
     * che l'impaginazione inventa sta in g_gen, che si azzera qui. */
    g_gen_n = 0;
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
        if (h <= 0) continue;

        if (!g_sfondi[i].bordo) {
            ex_riempi(g_f, g_sfondi[i].x, y, g_sfondi[i].w, h,
                      g_sfondi[i].colore);
            continue;
        }

        /* ! IL CONTORNO SI FA DI QUATTRO RIEMPIMENTI, e i due orizzontali si
         * disegnano solo se il loro lato e' DENTRO l'area: il ritaglio qui
         * sopra ha gia' accorciato il rettangolo, quindi una tabella scorsa a
         * meta' avrebbe altrimenti una riga di bordo dove il bordo non c'e'. */
        {
            int b  = g_sfondi[i].bordo;
            int x  = g_sfondi[i].x, w = g_sfondi[i].w;
            int y0 = g_sfondi[i].y - g_scorri;
            int h0 = g_sfondi[i].h;

            ex_riempi(g_f, x, y, b, h, g_sfondi[i].colore);
            ex_riempi(g_f, x + w - b, y, b, h, g_sfondi[i].colore);
            if (y0 >= area_y())
                ex_riempi(g_f, x, y0, w, b, g_sfondi[i].colore);
            if (y0 + h0 <= area_y() + area_h())
                ex_riempi(g_f, x, y0 + h0 - b, w, b, g_sfondi[i].colore);
        }
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

                /* ! IL TRATTO SCELTO SI VEDE, e va disegnato PRIMA del testo:
                 * e' uno sfondo, non un colore delle lettere. Dipingerlo dopo
                 * vorrebbe dire coprire le parole che dovrebbe evidenziare. */
                if (c->sel >= 0 && c->sel != c->cur) {
                    static char pre[CTRL_VAL_MAX];
                    int a = c->sel < c->cur ? c->sel : c->cur;
                    int b = c->sel < c->cur ? c->cur : c->sel;
                    int j, x0, x1;

                    for (j = 0; j < a && mostra[j]; j++) pre[j] = mostra[j];
                    pre[j] = '\0';
                    x0 = cx + 4 + ex_larghezza_testo(EX_FONT_SISTEMA, pre);

                    for (j = 0; j < b && mostra[j]; j++) pre[j] = mostra[j];
                    pre[j] = '\0';
                    x1 = cx + 4 + ex_larghezza_testo(EX_FONT_SISTEMA, pre);

                    if (x1 > cx + cw - 3) x1 = cx + cw - 3;
                    if (x1 > x0)
                        ex_riempi(g_f, x0, y + 3, x1 - x0, ch - 6, EX_BLU);
                }

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
            const char  *t = testo_pezzo(g_pez[i].testo);
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

/* =============================================================================
 * I FILE LOCALI — «file:», e perche' passano dalla stessa porta di tutto
 *
 * ! UN FILE APERTO DAL MENU DIVENTA UN INDIRIZZO, non un caso a parte. La
 * strada che porta una pagina sullo schermo — libera le immagini, chiudi il
 * motore, analizza, azzera i controlli, impagina, disegna — sta dentro `vai()`
 * ed e' lunga: rifarla per il disco vorrebbe dire due strade che divergono al
 * primo difetto corretto in una sola. Qui si leggono i byte e si lascia fare a
 * lei.
 *
 * ! COSI' CRONOLOGIA, PULSANTE INDIETRO E RIFERIMENTI RELATIVI VENGONO GRATIS.
 * Un `<a href="altra.html">` dentro una pagina locale e' un riferimento
 * relativo come tutti gli altri, e `risolvi()` sa da quale pagina viene: era
 * l'unico modo perche' la documentazione — che e' fatta di pagine che si
 * rimandano — si potesse sfogliare invece di guardare.
 *
 * ! LO SCHEMA SI SCRIVE COME LO SCRIVONO TUTTI. `file:///exwin/doc/x.html` ha
 * tre barre perche' le prime due sono l'host (vuoto: e' questa macchina) e la
 * terza e' l'inizio del percorso. Si accetta anche `file:/percorso`, che e' la
 * stessa cosa scritta piu' corta ed e' quella che viene da digitare.
 *
 * ! E LA CACHE NON C'ENTRA, in nessuna delle due direzioni. Serve a
 * risparmiare la RETE; tenere in /tmp la copia di un file che sta gia' su
 * disco sarebbe spazio speso per non risparmiare niente, e servire la copia
 * VECCHIA di un file che intanto si sta modificando con l'editor sarebbe la
 * cosa peggiore che possa fare a chi scrive una pagina.
 * ============================================================================= */
static int e_locale(const char *url)
{
    return url && url[0] == 'f' && url[1] == 'i' && url[2] == 'l' &&
           url[3] == 'e' && url[4] == ':';
}

/* Il percorso vero dentro un «file:...», sempre assoluto. Rende 0 se dopo lo
 * schema e le barre non resta niente. */
static int percorso_di(const char *url, char *out, unsigned int max)
{
    const char  *c = url + 5;           /* dopo «file:» */
    unsigned int i = 0;

    while (*c == '/') c++;              /* le barre dello schema, quante sono */
    if (!*c || max < 3) return 0;

    /* ! LA RADICE SI RIMETTE, e non e' pignoleria: `open("exwin/doc/x.html")`
     * e' un percorso RELATIVO alla directory di lavoro del browser, che non e'
     * quella che si aveva in mente. */
    out[i++] = '/';
    while (*c && i + 1 < max) out[i++] = *c++;
    out[i] = '\0';
    return 1;
}

/* Il contrario: da un percorso a un indirizzo. */
static void url_di_percorso(const char *perc, char *out, unsigned int max)
{
    snprintf(out, max, "file://%s%s", perc[0] == '/' ? "" : "/", perc);
}

static int locale_esiste(const char *perc)
{
    int fd = open(perc, O_RDONLY);

    if (fd < 0) return 0;
    close(fd);
    return 1;
}

/* Legge il file di un «file:...» dentro `buf`. Rende 0 se non si apre; scrive
 * in `quanti` i byte letti e in `troncata` se il file era piu' grande del
 * tetto. */
static int locale_leggi(const char *url, unsigned char *buf, unsigned int max,
                        unsigned int *quanti, int *troncata)
{
    char         perc[PERC_MAX];
    int          fd, k;
    unsigned int n = 0;

    *quanti = 0;
    *troncata = 0;

    if (!percorso_di(url, perc, sizeof(perc))) return 0;

    fd = open(perc, O_RDONLY);
    if (fd < 0) return 0;

    while (n < max && (k = (int)read(fd, buf + n, max - n)) > 0)
        n += (unsigned int)k;

    /* ! IL TRONCAMENTO SI SCOPRE LEGGENDO UN BYTE IN PIU', non confrontando
     * `n` col tetto: un file lungo ESATTAMENTE un megabyte non e' troncato, e
     * dirlo lo stesso sarebbe una nota falsa su una pagina intera. */
    if (n == max) {
        unsigned char altro;

        if (read(fd, &altro, 1) > 0) *troncata = 1;
    }
    close(fd);

    *quanti = n;
    return 1;
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

    /* Un «file:» e' gia' un indirizzo intero, come un http. */
    if (e_locale(rif)) {
        strncpy(out, rif, max - 1);
        out[max - 1] = '\0';
        return 1;
    }

    /* ! UN RIFERIMENTO CHE E' SOLO UN'ANCORA («#qualcosa») PUNTA ALLA PAGINA
     * CHE SI STA GIA' GUARDANDO, e questo browser non sa ancora saltare a un
     * punto dentro un documento. Seguirlo vorrebbe dire RICARICARE la stessa
     * pagina e riportarla in cima: il contrario esatto di quel che chiede chi
     * lo preme. Finche' il salto non c'e', non fare niente e' la risposta piu'
     * onesta. */
    if (rif[0] == '#') return 0;

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

    /* =========================================================================
     * ! DA UNA PAGINA LOCALE I RIFERIMENTI RESTANO LOCALI, e la base e' la
     * DIRECTORY del file, non il file: «altra.html» accanto a
     * /exwin/doc/browser.html vuol dire /exwin/doc/altra.html. Senza questo il
     * riferimento finirebbe in http_url(), che di un «file:» non sa niente, e
     * la voce Aiuto sarebbe una pagina sola e senza uscite.
     * ===================================================================== */
    if (e_locale(g_qui)) {
        char base[PERC_MAX];
        int  i;

        if (!percorso_di(g_qui, base, sizeof(base))) return 0;

        /* Un riferimento che comincia con «/» e' gia' un percorso assoluto:
         * la directory della pagina non c'entra. */
        if (rif[0] == '/') {
            url_di_percorso(rif, out, max);
            return 1;
        }

        for (i = 0; base[i]; i++) { }
        while (i > 0 && base[i - 1] != '/') i--;    /* via il nome del file */
        base[i] = '\0';

        snprintf(out, max, "file://%s%s", base, rif);
        return 1;
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

    printf("browser: cache in %s - %u voci, %u KB", g_cache,
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


/* =============================================================================
 * IL FILE DELLE IMPOSTAZIONI
 *
 *     $HOME/.app/browser/impostazioni.txt
 *
 * ! E' TESTO, chiave = valore, una per riga, e resta modificabile a mano
 * apposta: un file di configurazione che solo il programma sa scrivere e' un
 * file che non si puo' riparare quando quel programma non parte. E' la stessa
 * regola di /exwin/lib/applicazioni.txt.
 *
 * ! UNA CHIAVE SCONOSCIUTA SI SALTA IN SILENZIO, come fa il kernel con
 * kernel.cfg. E' cio' che permette a una versione vecchia del browser di
 * leggere un file scritto da una nuova senza morire su una riga che non
 * conosce — e alle «opzioni future» di aggiungersi senza rompere niente.
 *
 * ! E CIO' CHE NON C'E' NEL FILE RESTA COM'E', non torna al valore
 * predefinito: si legge sopra allo stato di adesso, che all'avvio e' quello
 * predefinito. Un file scritto a meta' toglie meno di quanto tolga
 * riazzerare tutto.
 * ============================================================================= */
#define IMP_PERC_MAX  192

static char g_imp_perc[IMP_PERC_MAX] = "";

/* Costruisce $HOME/.app/browser/impostazioni.txt creando le directory che
 * mancano. Rende 0 se non si puo': allora le impostazioni valgono per questa
 * sessione e basta, e lo si dice. */
static int imp_prepara(void)
{
    static const char *const passi[] = { "/.app", "/browser" };
    const char *casa = getenv("HOME");
    char        p[IMP_PERC_MAX];
    int         i;

    g_imp_perc[0] = '\0';

    if (!casa || !casa[0]) return 0;
    if (strlen(casa) + 32 >= sizeof(p)) return 0;

    strcpy(p, casa);
    i = (int)strlen(p);
    while (i > 0 && p[i - 1] == '/') p[--i] = '\0';

    for (i = 0; i < 2; i++) {
        strncat(p, passi[i], sizeof(p) - strlen(p) - 1);
        if (mkdir(p, 0755) != 0 && errno != EEXIST) return 0;
    }

    strncat(p, "/impostazioni.txt", sizeof(p) - strlen(p) - 1);
    strncpy(g_imp_perc, p, sizeof(g_imp_perc) - 1);
    g_imp_perc[sizeof(g_imp_perc) - 1] = '\0';
    return 1;
}

/* «si», «no», «1», «0», «acceso», «spento». Rende il valore, o `ora` se non
 * si capisce: un valore incomprensibile non deve cambiare niente. */
static int imp_bandiera(const char *v, int ora)
{
    if (!v || !v[0]) return ora;
    if (v[0] == 's' || v[0] == 'S' || v[0] == '1' ||
        v[0] == 'a' || v[0] == 'A') return 1;
    if (v[0] == 'n' || v[0] == 'N' || v[0] == '0' ||
        v[0] == 'f' || v[0] == 'F') return 0;
    return ora;
}

static void imp_riga(char *riga)
{
    char *ug, *k, *v;
    int   i;

    /* via i commenti e gli spazi ai due capi */
    for (i = 0; riga[i]; i++)
        if (riga[i] == '#') { riga[i] = '\0'; break; }

    ug = 0;
    for (i = 0; riga[i]; i++) if (riga[i] == '=') { ug = riga + i; break; }
    if (!ug) return;

    *ug = '\0';
    k = riga;
    v = ug + 1;

    while (*k == ' ' || *k == '\t') k++;
    i = (int)strlen(k);
    while (i > 0 && (k[i-1] == ' ' || k[i-1] == '\t')) k[--i] = '\0';

    while (*v == ' ' || *v == '\t') v++;
    i = (int)strlen(v);
    while (i > 0 && (v[i-1] == ' ' || v[i-1] == '\t' ||
                     v[i-1] == '\r' || v[i-1] == '\n')) v[--i] = '\0';

    if (uguale(k, "home")) {
        strncpy(g_home, v, sizeof(g_home) - 1);
        g_home[sizeof(g_home) - 1] = '\0';
        return;
    }
    if (uguale(k, "javascript")) { g_js_acceso    = imp_bandiera(v, g_js_acceso);    return; }
    if (uguale(k, "immagini"))   { g_img_accese   = imp_bandiera(v, g_img_accese);   return; }
    if (uguale(k, "cache"))      { g_cache_accesa = imp_bandiera(v, g_cache_accesa); return; }
    if (uguale(k, "motore")) {
        /* ! IL VALORE E' UN NOME, NON UN SI'/NO, perche' i motori possono
         * diventare tre: «exjs», «quickjs». Qualunque altra cosa lascia le
         * cose come stanno, come per ogni chiave che non si capisce. */
        if (v[0] == 'q' || v[0] == 'Q') g_qjs = 1;
        else if (v[0] == 'e' || v[0] == 'E') g_qjs = 0;
        return;
    }
    /* una chiave che non conosciamo: si salta, e non e' un errore */
}

static void imp_leggi(void)
{
    char  buf[1024];
    char  riga[256];
    int   fd, n, i, col = 0;

    if (!g_imp_perc[0]) return;

    fd = open(g_imp_perc, O_RDONLY);
    if (fd < 0) return;

    while ((n = (int)read(fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < n; i++) {
            char c = buf[i];

            if (c == '\n' || c == '\r') {
                riga[col] = '\0';
                if (col) imp_riga(riga);
                col = 0;
                continue;
            }
            if (col + 1 < (int)sizeof(riga)) riga[col++] = c;
        }
    }
    close(fd);

    riga[col] = '\0';
    if (col) imp_riga(riga);
}

/* Rende 1 se il file e' stato scritto per intero. */
static int imp_scrivi(void)
{
    char t[EXHTTP_URL_MAX + 256];
    int  fd, n, scritti = 0;

    if (!g_imp_perc[0]) return 0;

    /* ! L'INTESTAZIONE DICE DOVE STA E CHI LO SCRIVE. Chi lo trova aprendolo
     * con `cat` deve capire in dieci secondi che cos'e' e che si puo'
     * modificare. */
    snprintf(t, sizeof(t),
             "# Le impostazioni del navigatore di EX-OS.\n"
             "# Si puo' modificare a mano: chiave = valore, # commenta.\n"
             "# Le chiavi che questo browser non conosce si saltano.\n"
             "\n"
             "home       = %s\n"
             "javascript = %s\n"
             "motore     = %s\n"
             "immagini   = %s\n"
             "cache      = %s\n",
             g_home,
             g_js_acceso    ? "si" : "no",
             g_qjs          ? "quickjs" : "exjs",
             g_img_accese   ? "si" : "no",
             g_cache_accesa ? "si" : "no");

    fd = open(g_imp_perc, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return 0;

    n = (int)strlen(t);
    while (scritti < n) {
        int k = (int)write(fd, t + scritti, (unsigned int)(n - scritti));

        if (k <= 0) break;
        scritti += k;
    }
    close(fd);
    return scritti == n;
}

/* =============================================================================
 * I BISCOTTI — la dispensa, il file, e le due porte da cui entrano
 *
 * ! LA DISPENSA STA QUI, E LE SUE REGOLE NO. Quali biscotti valgono per quale
 * dominio e per quale percorso lo decide biscotti.c, che sta in un file suo
 * apposta: quelle regole si sbagliano in silenzio e si provano senza schermo,
 * senza rete e senza disco (make prova-biscotti). Qui c'e' quel che il banco
 * non puo' avere — l'orologio vero, il file su disco, e le due porte.
 *
 * ! LE PORTE SONO DUE E VANNO TENUTE DISTINTE. Da una entrano i `Set-Cookie`
 * del server, dall'altra quel che uno script scrive in `document.cookie`; la
 * differenza e' `HttpOnly`, che uno script non puo' ne' leggere ne' fabbricare.
 * Se fossero la stessa funzione, quella bandiera non vorrebbe dire piu' niente.
 *
 * ! E IL FILE SI RISCRIVE SOLO QUANDO QUALCOSA E' CAMBIATO. Una pagina con
 * venti immagini fa venti richieste e nessuna cambia la dispensa: riscrivere il
 * file a ogni giro vorrebbe dire venti scritture su un CD che non si scrive, e
 * venti messaggi d'errore.
 * ========================================================================== */
#define BIS_TESTO_MAX  (16u * 1024u)

static Dispensa g_bis;
static char     g_bis_perc[IMP_PERC_MAX] = "";
static int      g_bis_sporca = 0;       /* c'e' qualcosa da salvare */

/* L'ora vera, in secondi dall'epoca. Zero vuol dire «non si sa», e allora
 * biscotti.c non fa scadere niente: e' meglio tenere un biscotto morto che
 * buttarne uno vivo perche' l'orologio non risponde. */
static unsigned int ora_epoca(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, 0) != 0) return 0;
    if (tv.tv_sec <= 0) return 0;
    return (unsigned int)tv.tv_sec;
}

/* $HOME/.app/browser/biscotti.txt, accanto alle impostazioni. */
static int bis_prepara(void)
{
    char t[IMP_PERC_MAX];
    int  i;

    g_bis_perc[0] = '\0';
    if (!g_imp_perc[0]) return 0;

    /* Si parte dal percorso delle impostazioni, che le directory le ha gia'
     * create: rifare qui la stessa salita vorrebbe dire due funzioni che
     * devono restare d'accordo su dove sta la casa. */
    strncpy(t, g_imp_perc, sizeof(t) - 1);
    t[sizeof(t) - 1] = '\0';
    i = (int)strlen(t);
    while (i > 0 && t[i - 1] != '/') i--;
    if (i == 0) return 0;
    t[i] = '\0';
    strncat(t, "biscotti.txt", sizeof(t) - strlen(t) - 1);

    strncpy(g_bis_perc, t, sizeof(g_bis_perc) - 1);
    g_bis_perc[sizeof(g_bis_perc) - 1] = '\0';
    return 1;
}

static void bis_leggi_file(void)
{
    static char testo[BIS_TESTO_MAX];
    int         fd, n;
    unsigned int u = 0;

    if (!g_bis_perc[0]) return;
    fd = open(g_bis_perc, O_RDONLY);
    if (fd < 0) return;

    while (u + 1 < sizeof(testo) &&
           (n = (int)read(fd, testo + u, sizeof(testo) - 1 - u)) > 0)
        u += (unsigned int)n;
    close(fd);
    testo[u] = '\0';

    bis_carica(&g_bis, testo, ora_epoca());
}

static void bis_scrivi_file(void)
{
    static char  testo[BIS_TESTO_MAX];
    unsigned int n;
    int          fd;

    if (!g_bis_sporca || !g_bis_perc[0]) return;
    g_bis_sporca = 0;

    n = bis_salva(&g_bis, testo, sizeof(testo), ora_epoca());

    fd = open(g_bis_perc, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    /* ! UN DISCO IN SOLA LETTURA NON E' UN ERRORE DA GRIDARE. Da CD non si
     * scrive niente, e i biscotti valgono per questa sessione: e' gia' detto
     * all'avvio per la cache, e ripeterlo a ogni pagina sarebbe rumore. */
    if (fd < 0) return;
    if (n) write(fd, testo, n);
    close(fd);
}

/* ! IL PERCORSO DELLA PAGINA DI ADESSO, per capire quali biscotti la
 * riguardano. Se l'indirizzo non si smonta si risponde vuoto: meglio non
 * mandarne che mandarne di un altro sito. */
static int bis_qui(HttpUrl *u)
{
    if (!g_qui[0] || e_locale(g_qui)) return 0;
    return http_url(g_qui, u) ? 1 : 0;
}

/* --- la porta dell'HTTP: i due ganci che exhttp chiama -------------------- */
static void bis_chiedi(void *dato, const char *host, const char *percorso,
                       int cifrata, char *fuori, unsigned int max)
{
    (void)dato;
    bis_da_mandare(&g_bis, host, percorso, cifrata, 0, ora_epoca(), fuori, max);
}

static void bis_arrivo(void *dato, const char *host, const char *riga)
{
    (void)dato;
    /* ! IL PERCORSO NON CE L'ABBIAMO QUI E NON SERVE INVENTARLO: exhttp ci
     * da' l'host, e il percorso predefinito lo si prende dalla pagina che si
     * sta caricando. Nei casi che contano — una redirezione dentro la stessa
     * chiamata — e' quello giusto; e comunque un `Path` scritto nel biscotto
     * vince su qualunque predefinito. */
    if (bis_arrivato(&g_bis, host, "/", riga, ora_epoca())) g_bis_sporca = 1;
}

/* --- la porta degli script: document.cookie ------------------------------- */

/* Quel che uno script puo' vedere di questa pagina. */
static void bis_per_script(char *fuori, unsigned int max)
{
    HttpUrl u;

    fuori[0] = '\0';
    if (!bis_qui(&u)) return;
    bis_da_mandare(&g_bis, u.host, u.percorso, u.cifrato, 1, ora_epoca(),
                   fuori, max);
}

/* ! QUEL CHE UNO SCRIPT HA SCRITTO SI RACCOGLIE A PEZZI E SI CONFRONTA. Il
 * ponte tiene una stringa sola — «a=1; b=2» — e non dice QUALE riga e'
 * cambiata: la si spezza e si rimette dentro ogni pezzo. Rimetterne uno che
 * c'era gia' non fa niente, ed e' molto piu' semplice che tenere due elenchi
 * d'accordo fra loro. */
static ExDom *bis_ponte(void);          /* vedi motore_apri, piu' avanti */

static void bis_dallo_script(void)
{
    const char  *s;
    HttpUrl      u;
    char         pezzo[BIS_NOME_MAX + BIS_VAL_MAX + 4];
    unsigned int i = 0, k;

    { ExDom *D = bis_ponte();

      if (!D || !bis_qui(&u)) return;
      s = exdom_biscotti(D); }
    if (!s || !s[0]) return;

    while (s[i]) {
        while (s[i] == ' ' || s[i] == ';') i++;
        if (!s[i]) break;
        k = 0;
        while (s[i] && s[i] != ';') {
            if (k + 1 < sizeof(pezzo)) pezzo[k++] = s[i];
            i++;
        }
        while (k > 0 && pezzo[k - 1] == ' ') k--;
        pezzo[k] = '\0';
        if (pezzo[0] &&
            bis_da_script(&g_bis, u.host, u.percorso, pezzo, ora_epoca()))
            g_bis_sporca = 1;
    }
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
    /* ! SPENTA VUOL DIRE ANCHE «NON LEGGERE», non solo «non scrivere». Una
     * cache che smette di riempirsi ma continua a servire quel che ha
     * dentro sarebbe la cosa peggiore: chi la spegne lo fa proprio perche'
     * vuole la pagina di adesso. */
    if (!g_cache_accesa) { *quanti = 0; return 0; }

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

    if (!g_cache_accesa) return;
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
    /* ! UN'IMMAGINE DI UNA PAGINA LOCALE SI LEGGE DAL DISCO, e senza questo
     * ramo finirebbe in exhttp, che di «file:» non sa niente e risponde di no:
     * una guida con dentro una figura mostrerebbe l'`alt` e nient'altro. */
    if (e_locale(url)) {
        int troncata = 0;

        if (!locale_leggi(url, g_imm_buf, sizeof(g_imm_buf), &n, &troncata))
            return 0;
        if (n == 0 || troncata) return 0;
    } else if (!cache_leggi(url, g_imm_buf, sizeof(g_imm_buf), &n)) {
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

    /* ! SPENTE NON VUOL DIRE «RIQUADRO VUOTO»: i pezzi non sono nemmeno
     * stati creati, e al loro posto si legge l'`alt`, che e' cio' che il
     * testo alternativo serve a fare. Chi spegne le immagini di solito lo fa
     * perche' la rete e' lenta, e vuole leggere. */
    if (!g_img_accese) return;

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
            /* ! E ANCHE QUI IL DISCO PRIMA DELLA RETE: una pagina locale con
             * accanto il suo .css e' il caso normale di chi scrive una pagina
             * e la guarda, non un caso di confine. */
            if (e_locale(url)) {
                int troncato = 0;

                if (!locale_leggi(url, g_imm_buf, sizeof(g_imm_buf), &n,
                                  &troncato)) continue;
                if (n == 0) continue;
            } else if (!cache_leggi(url, g_imm_buf, sizeof(g_imm_buf), &n)) {
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

/* =============================================================================
 * IL MOTORE JAVASCRIPT
 *
 * ! NON E' UN TETTO FISSO, E' UNA RICHIESTA ALLA LIBC. Tutti gli altri buffer
 * di questo browser sono vettori dichiarati, e va bene: servono a ogni pagina.
 * Il motore no — la maggioranza delle pagine che questo browser riesce ad
 * aprire non ha nemmeno uno script, e farle pagare due megabyte e mezzo fissi
 * vorrebbe dire due megabyte e mezzo in meno per le immagini di tutte.
 *
 * ! E SI CHIUDE QUANDO LA PAGINA SE NE VA. Un contesto vive quanto la pagina
 * che lo ha chiesto: le sue variabili, le sue funzioni e i suoi involucri
 * parlano di un albero che dopo html_analizza non esiste piu'. Tenerlo aperto
 * vorrebbe dire uno script della pagina di prima che tocca i nodi di quella di
 * adesso — che e' peggio di non avere JavaScript per niente.
 *
 * ! LE TAGLIE SONO PICCOLE APPOSTA. Duemila caselle e novantasei chilobyte di
 * arena non aprono google.com, e non ci provano: aprono le pagine che
 * costruiscono un pezzo di se' con uno script, che sono tante e sono proprio
 * quelle che oggi si vedono vuote. Quando ci sara' un motore piu' grande — il
 * QuickJS gia' dichiarato in exjs.h — questi due numeri saranno la prima cosa
 * da rifare, e sono in un posto solo.
 * ========================================================================== */
#define JS_OGGETTI    2000u
#define JS_ARENA      (96u * 1024u)
#define JS_TESTO      (64u * 1024u)
#define JS_ASCOLTI    256u
#define JS_SCRIPT_MAX 16

static ExJsCtx *g_js  = 0;
static ExDom   *g_dom = 0;

/* ! UNA RIGA SOLA PER NON SPOSTARE MEZZO FILE. Le funzioni dei biscotti stanno
 * accanto alle impostazioni — e' li' che si scrive su disco — ma una di loro ha
 * bisogno del ponte, che si dichiara duemila righe piu' giu'. Fra spostare un
 * blocco e dichiarare una funzione, la seconda si legge meglio. */
static ExDom *bis_ponte(void) { return g_dom; }

/* ! SI CHIAMA DOVUNQUE UNO SCRIPT ABBIA POTUTO GIRARE, e sono tre posti: dopo
 * gli script della pagina, dopo un evento, e dopo la pompa dei tempi. Un
 * `document.cookie = ...` scritto dentro un gestore di clic e' comune quanto
 * uno scritto in cima alla pagina, e raccoglierlo solo nel primo caso vorrebbe
 * dire un biscotto che sparisce a seconda di DOVE e' stato messo. */
static void dopo_gli_script(void)
{
    bis_dallo_script();
    bis_scrivi_file();
}
static void    *g_js_mem = 0;
static void    *g_dom_mem = 0;

/* L'ultima versione del documento che l'impaginazione ha visto. Il perche' di
 * questo confronto sta accanto al campo `versione` in html.h. */
static unsigned int g_vista = 0;

/* ! `console.log` FINISCE NELLA BARRA DI STATO, e non nel vuoto. Una pagina
 * che stampa un messaggio lo sta scrivendo per qualcuno; buttarlo via
 * risparmierebbe tre righe e toglierebbe l'unico modo di capire cosa sta
 * facendo uno script quando la pagina non cambia. Si tiene l'ULTIMA riga: la
 * barra e' alta sedici pixel, e mostrarne una vecchia sarebbe peggio. */
static void js_uscita(const char *t, unsigned int n, void *dato)
{
    static char riga[160];
    unsigned int i = 0, k = 0;

    (void)dato;
    while (i < n && k < sizeof(riga) - 1) {
        if (t[i] != '\n' && t[i] != '\r') riga[k++] = t[i];
        else if (k > 0 && riga[k-1] != ' ') riga[k++] = ' ';
        i++;
    }
    riga[k] = '\0';
    if (k) dico(riga);
}


/* =============================================================================
 * LA RETE PER GLI SCRIPT — il gancio che exdom chiama
 *
 * ! IL PONTE NON CHIAMA exhttp, LO CHIAMA IL BROWSER. Da questa parte c'e'
 * tutto quello che serve e che il ponte non ha: risolvi() per gli indirizzi
 * relativi, e_locale() per lo schema `file:`, e un tetto scelto qui. Il perche'
 * della divisione sta in exdom.h, accanto a ExDomRete.
 *
 * ! IL BUFFER E' SUO E SI PRENDE ALLA PRIMA RICHIESTA. Riusare g_imm_buf —
 * quello di immagini, fogli di stile e script esterni — sarebbe stato gratis e
 * sbagliato: uno script che fa una XMLHttpRequest sta girando MENTRE
 * esegui_script tiene il proprio sorgente in quel buffer, e la risposta gli
 * scriverebbe sopra il codice che si sta ancora eseguendo. Una pagina che non
 * chiede niente alla rete non paga niente, perche' la malloc si fa qui.
 *
 * ! E IL TETTO E' DICHIARATO. Centoventotto kilobyte: una risposta piu' grande
 * arriva TRONCATA e lo si scrive sulla console, invece di far credere a una
 * pagina di avere tutto. Con ExJs c'e' anche un secondo tetto, piu' basso e
 * fuori di qui — l'arena delle stringhe del motore, che e' di 96 KB: una
 * risposta che non ci sta diventa la stringa vuota, e il motore segna che la
 * memoria e' finita.
 * ========================================================================== */
#define XHR_MAX  (128u * 1024u)

static unsigned char *g_xhr_buf = 0;

static int rete_per_script(void *dato, ExDomRichiesta *r)
{
    char         url[EXHTTP_URL_MAX];
    ExHttpEsito  e;
    int          post;

    (void)dato;
    if (!r->url || !r->url[0]) return 0;

    /* ! I BISCOTTI DEGLI SCRIPT SI RACCOLGONO PRIMA DI PARTIRE, non alla fine
     * della pagina, e la differenza si vede in tre righe di JavaScript:
     *
     *     document.cookie = 'x=1';
     *     fetch('/dove-sono');        <- deve gia' portarselo
     *
     * Le due cose succedono dentro lo stesso script, e raccogliere solo dopo
     * — com'era — voleva dire che la richiesta partiva senza. Non e' un caso
     * di scuola: e' il modo normale in cui una pagina segna una scelta e la
     * manda al server subito dopo. */
    bis_dallo_script();

    /* ! L'INDIRIZZO ARRIVA COM'E' SCRITTO NELLA PAGINA, e risolverlo tocca a
     * noi: il ponte non sa rispetto a che cosa. E' la stessa risolvi() dei
     * collegamenti, quindi «/a», «../b» e «c.html» si comportano nello
     * script come si comportano in un href. */
    if (!risolvi(r->url, url, sizeof(url))) return 0;

    if (!g_xhr_buf) {
        g_xhr_buf = malloc(XHR_MAX);
        if (!g_xhr_buf) { dico("javascript: memoria non disponibile"); return 0; }
    }

    post = r->metodo && (r->metodo[0] | 32) == 'p' && (r->metodo[1] | 32) == 'o';

    /* ! ANCHE `file:` PASSA DI QUI, e non e' un di piu': la documentazione sta
     * su disco, e una pagina locale che si legge un pezzo con fetch e' il modo
     * piu' comodo di provare tutto questo senza accendere una rete. exhttp di
     * «file:» non sa niente e risponderebbe di no. */
    if (e_locale(url)) {
        unsigned int n = 0;
        int          troncata = 0;

        if (!locale_leggi(url, g_xhr_buf, XHR_MAX, &n, &troncata)) return 0;
        if (troncata) dico("javascript: risposta troncata");
        r->risposta = (const char *)g_xhr_buf;
        r->byte     = n;
        r->codice   = 200;
        r->tipo     = 0;
        return 1;
    }

    memset(&e, 0, sizeof(e));
    if (post ? !exhttp_posta(url, r->corpo ? r->corpo : "",
                             g_xhr_buf, XHR_MAX, &e)
             : !exhttp_prendi(url, g_xhr_buf, XHR_MAX, &e)) {
        /* ! «NON E' PARTITA» E «IL SERVER HA DETTO DI NO» SONO DUE COSE, e la
         * differenza esce da qui: rendendo 0, fetch rifiuta la promessa e
         * XMLHttpRequest lascia `status` a zero. Rendere 1 con codice 0
         * direbbe a una pagina che ha ricevuto una risposta vuota. */
        return 0;
    }

    if (e.troncata) dico("javascript: risposta troncata");
    r->risposta = (const char *)g_xhr_buf;
    r->byte     = e.byte;
    r->codice   = e.codice;
    r->tipo     = e.tipo[0] ? e.tipo : 0;
    return 1;
}

static void motore_chiudi(void)
{
    /* ! IL MOTORE SI CHIUDE PRIMA DI LIBERARE IL SUO BLOCCO, e l'ordine e'
     * l'unica cosa che conta qui. Con ExJs la chiamata non fa niente — tutto
     * quello che possiede sta in quel blocco — ma un motore puo' possedere
     * anche altro: QuickJS ha il proprio runtime, preso dalla libc, e senza
     * questa riga ogni pagina ne lascerebbe dietro uno intero. Liberare prima
     * il blocco vorrebbe dire chiamarla su un contesto che non c'e' piu'. */
    if (g_js) exjs_chiudi(g_js);

    if (g_js_mem)  { free(g_js_mem);  g_js_mem = 0; }
    if (g_dom_mem) { free(g_dom_mem); g_dom_mem = 0; }
    g_js  = 0;
    g_dom = 0;
    ex_sveglia(g_f, 0);
}

/* Rende 1 se il motore c'e' ed e' agganciato al documento di adesso. */
static int motore_apri(void)
{
    unsigned int quanto;

    if (g_js && g_dom) { exdom_indirizzo(g_dom, g_qui); return 1; }
    motore_chiudi();

    /* ! LA SCELTA DEL MOTORE VA FATTA QUI, PRIMA DI TUTTO IL RESTO: dopo la
     * prima chiamata la libreria e' gia' mappata e non si cambia piu'. E' il
     * motivo per cui cambiare motore vale dalla pagina dopo. */
    exjs_motore(g_qjs);

    quanto   = exjs_quanto_serve(JS_OGGETTI, JS_ARENA);
    g_js_mem = malloc(quanto);
    if (!g_js_mem) { dico("javascript: memoria non disponibile"); return 0; }

    g_js = exjs_apri(g_js_mem, quanto, JS_OGGETTI, JS_ARENA);
    if (!g_js) { motore_chiudi(); dico("javascript: il motore non si apre"); return 0; }
    exjs_uscita_metti(g_js, js_uscita, 0);

    quanto    = exdom_quanto_serve(NODI_MAX, JS_TESTO, JS_ASCOLTI);
    g_dom_mem = malloc(quanto);
    if (!g_dom_mem) { motore_chiudi(); dico("javascript: memoria non disponibile"); return 0; }

    g_dom = exdom_apri(g_dom_mem, quanto, g_js, &g_doc,
                       NODI_MAX, JS_TESTO, JS_ASCOLTI);
    if (!g_dom) { motore_chiudi(); dico("javascript: il ponte non si apre"); return 0; }

    /* ! L'INDIRIZZO GLIELO DICIAMO NOI, e da qui viene tutto `location`. Il
     * ponte non sa dove sia la pagina che ha in mano — non apre connessioni —
     * e senza questa riga `location.href` sarebbe una stringa vuota su ogni
     * pagina, cioe' una risposta sbagliata data senza errore. */
    exdom_indirizzo(g_dom, g_qui);

    /* ! E I BISCOTTI CHE QUESTA PAGINA PUO' VEDERE, che non sono tutti: gli
     * `HttpOnly` restano fuori, ed e' tutto il senso di quella bandiera. Il
     * ponte ne tiene una copia e ci lascia scrivere sopra; a raccogliere quel
     * che gli script hanno scritto ci pensa bis_dallo_script(). */
    {
        static char miei[BIS_TESTO_MAX / 8];

        bis_per_script(miei, sizeof(miei));
        exdom_biscotti_metti(g_dom, miei);
    }

    /* ! E LA RETE, che il ponte non ha e non deve avere. Senza questa riga
     * XMLHttpRequest e fetch ci sono lo stesso e rispondono «non e' partita»:
     * e' la verita', ed e' quel che si vede se un giorno la riga sparisce. */
    exdom_rete_metti(g_dom, rete_per_script, 0);
    return 1;
}

/* ! UN <script> SI SALTA SE DICHIARA DI NON ESSERE JAVASCRIPT. `type` assente
 * vuol dire JavaScript — e' cosi' che sta scritto in mezzo web — ma
 * `type="text/template"` e' un pezzo di marcatore che una libreria copiera'
 * altrove, e darlo in pasto a un interprete darebbe un errore di sintassi su
 * una pagina che non ha nessuno sbaglio. */
static int e_javascript(int v)
{
    const char *t = html_attr(&g_doc, v, "type");
    int         i;

    if (!t || !t[0]) return 1;
    for (i = 0; t[i]; i++)
        if ((t[i] | 32) == 'j' && (t[i+1] | 32) == 'a' && (t[i+2] | 32) == 'v')
            return 1;
    /* «ecmascript» e' l'altro nome dello stesso linguaggio. */
    for (i = 0; t[i]; i++)
        if ((t[i] | 32) == 'e' && (t[i+1] | 32) == 'c' && (t[i+2] | 32) == 'm')
            return 1;
    return 0;
}

static void js_grida(const ExJsErrore *e)
{
    char b[160];

    if (!e->messaggio[0]) return;
    snprintf(b, sizeof(b), "javascript, riga %d: %s", e->riga, e->messaggio);
    dico(b);
}

/* -----------------------------------------------------------------------------
 * Far girare gli script della pagina
 *
 * ! SI GIRA DRITTO SUL VETTORE DEI NODI, nell'ordine in cui l'analizzatore li
 * ha creati, che e' l'ordine del documento. E' lo stesso giro che fa
 * raccogli_css, e per lo stesso motivo: gli script della pagina vanno eseguiti
 * nell'ordine in cui stanno scritti, perche' il secondo si aspetta di trovare
 * quel che ha definito il primo.
 *
 * ! E SI ESEGUONO PRIMA DEI FOGLI DI STILE E DELL'IMPAGINAZIONE. Uno script
 * che costruisce meta' della pagina — ed e' quel che fanno le pagine che oggi
 * si vedono vuote — deve aver finito prima che si decida come impaginare, o
 * si impaginerebbe il documento senza il pezzo che quello script aggiunge.
 * --------------------------------------------------------------------------- */
static void esegui_script(void)
{
    int i, fatti = 0, aperti = 0;

    /* ! SPENTO SI CONTROLLA QUI E NON PIU' IN BASSO, prima che il motore si
     * apra: aprirlo vuol dire chiedere alla libc mezzo megabyte e mappare due
     * librerie condivise, e su una pagina che non deve eseguire niente
     * sarebbero spesi per non fare nulla. */
    if (!g_js_acceso) return;

    for (i = 0; i < (int)g_doc.nodi_n && fatti < JS_SCRIPT_MAX; i++) {
        const char *src;
        ExJsErrore  err;
        ExJsVal     r;
        int         f;

        if (g_doc.nodi[i].tipo != HTML_ELEMENTO) continue;
        if (!uguale(html_nome(&g_doc, i), "script")) continue;
        if (!e_javascript(i)) continue;

        if (!aperti) {
            if (!motore_apri()) return;
            aperti = 1;
        }

        /* --- lo script che sta altrove ---------------------------------- */
        src = html_attr(&g_doc, i, "src");
        if (src && src[0]) {
            char         url[EXHTTP_URL_MAX];
            unsigned int n = 0;

            if (!risolvi(src, url, sizeof(url))) continue;

            /* ! SI RIUSA IL BUFFER DELLE IMMAGINI, come fanno i fogli di
             * stile e per la stessa ragione: gli script si prendono PRIMA
             * della prima impaginazione, le immagini dopo. */
            if (e_locale(url)) {
                int troncato = 0;

                if (!locale_leggi(url, g_imm_buf, sizeof(g_imm_buf), &n,
                                  &troncato)) continue;
                if (n == 0) continue;
            } else if (!cache_leggi(url, g_imm_buf, sizeof(g_imm_buf), &n)) {
                ExHttpEsito e;

                dico("script...");
                if (!exhttp_prendi(url, g_imm_buf, sizeof(g_imm_buf), &e)) continue;
                if (e.codice != 200 || e.byte == 0) continue;
                n = e.byte;
                if (!e.troncata) cache_scrivi(url, g_imm_buf, n);
            }

            memset(&err, 0, sizeof(err));
            if (!exjs_esegui(g_js, (const char *)g_imm_buf, n, &r, &err))
                js_grida(&err);
            fatti++;
            continue;
        }

        /* --- lo script scritto qui -------------------------------------- */
        for (f = g_doc.nodi[i].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo) {
            const char  *t;
            unsigned int n = 0;

            if (g_doc.nodi[f].tipo != HTML_TESTO) continue;
            t = html_testo(&g_doc, f);
            while (t[n]) n++;
            if (!n) continue;

            memset(&err, 0, sizeof(err));
            if (!exjs_esegui(g_js, t, n, &r, &err)) js_grida(&err);
            fatti++;
        }
    }

    /* ! LA SVEGLIA SI CHIEDE SOLO SE C'E' QUALCOSA CHE ASPETTA. Una pagina con
     * uno script che ha finito non ha motivo di svegliare il browser cinque
     * volte al secondo per sempre — e su una macchina da 32 MB il costo di un
     * risveglio inutile e' un ridisegno che nessuno ha chiesto. */
    if (g_js && exjs_lavori_in_attesa(g_js)) ex_sveglia(g_f, 200);

    dopo_gli_script();
}

/* ! DOPO OGNI SCRIPT SI GUARDA SE IL DOCUMENTO E' CAMBIATO, e si rifa' solo
 * allora. E' tutto il motivo per cui html.c tiene un contatore di versione:
 * senza, l'unica scelta sarebbe fra rimpaginare dopo ogni clic — su un albero
 * di ventiquattromila nodi, a ogni movimento — e non rimpaginare mai, cioe'
 * una pagina che non risponde. */
static void rifai_se_cambiato(void)
{
    if (!g_dom) return;
    if (html_versione(&g_doc) == g_vista) return;

    /* I fogli di stile si rileggono perche' uno script puo' aver aggiunto un
     * <style> o cambiato una classe: il calcolo dello stile guarda l'albero,
     * ma le REGOLE stanno in un'altra struttura, e quella non si aggiorna da
     * sola. */
    raccogli_css();
    impagina();
    g_vista = html_versione(&g_doc);
    disegna();
}

/* Il nodo sotto il puntatore, o -1. */
static int nodo_sotto(int x, int y)
{
    int i;

    for (i = 0; i < g_pez_n; i++) {
        int py = g_pez[i].y - g_scorri;
        int h  = g_pez[i].img >= 0 ? g_pez[i].h
                                   : ex_font_altezza(g_pez[i].font);

        if (g_pez[i].nodo < 0) continue;
        if (x >= g_pez[i].x && x < g_pez[i].x + g_pez[i].w &&
            y >= py && y < py + h) return g_pez[i].nodo;
    }
    return -1;
}

/* C'e' almeno un <script> in questa pagina? Serve solo a scegliere le parole
 * di un messaggio: vedi in fondo a vai(). */
static int ha_script(void)
{
    unsigned int i;

    for (i = 0; i < g_doc.nodi_n; i++)
        if (g_doc.nodi[i].tipo == HTML_ELEMENTO &&
            uguale(html_nome(&g_doc, (int)i), "script")) return 1;
    return 0;
}

/* Rende 0 se uno script ha chiamato preventDefault(): allora il collegamento
 * NON si segue e il modulo NON si manda. E' l'unica cosa che il browser deve
 * sapere di quel che e' successo dentro il motore. */
static int clic_al_documento(int x, int y)
{
    ExJsErrore err;
    int        nodo, seguire;

    if (!g_dom) return 1;
    nodo = nodo_sotto(x, y);
    if (nodo < 0) return 1;

    memset(&err, 0, sizeof(err));
    seguire = exdom_evento(g_dom, nodo, "click", &err);
    js_grida(&err);
    rifai_se_cambiato();
    dopo_gli_script();
    return seguire;
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

/* =============================================================================
 * QUANDO UNO SCRIPT DICE DOVE ANDARE
 *
 * ! SI VA DOPO, MAI IN MEZZO. `location.href = "..."` non carica niente da
 * se': il ponte mette da parte l'indirizzo e questa funzione lo raccoglie
 * quando lo script ha finito. Caricare un'altra pagina mentre uno script gira
 * vorrebbe dire buttare via l'albero che quello script sta ancora usando —
 * e' la stessa ragione per cui gli eventi li fa partire il browser e non il
 * ponte.
 *
 * ! E SI CONTANO I SALTI. Una pagina che si rimanda da sola all'infinito
 * esiste — e' anche un modo classico di sbagliare un controllo — e senza un
 * tetto il browser ci girerebbe dentro per sempre, con la rete accesa e senza
 * un modo di fermarlo. Cinque e' il numero delle redirezioni HTTP di exhttp:
 * lo stesso problema, quindi lo stesso tetto.
 *
 * ! IL CONTO SI AZZERA DA SOLO appena una navigazione la chiede una persona.
 * La bandiera `g_js_salta` dice a vai() da dove sta arrivando: senza, il
 * contatore andrebbe azzerato in nove punti diversi, e il decimo — quello che
 * si aggiunge fra sei mesi — se ne dimenticherebbe.
 * ========================================================================== */
#define JS_SALTI_MAX  5

static int g_js_salti  = 0;
static int g_js_salta  = 0;

static void vai(const char *url, int in_storia, int usa_cache);

/* Rende 1 se ha davvero cambiato pagina: chi chiama deve sapere che l'albero
 * che aveva in mano un istante fa non c'e' piu'. */
static int segui_location(void)
{
    char dove[EXHTTP_URL_MAX], assoluto[EXHTTP_URL_MAX];

    if (!g_dom) return 0;
    if (!exdom_dove_andare(g_dom, dove, sizeof(dove))) return 0;
    if (!dove[0]) return 0;

    if (g_js_salti >= JS_SALTI_MAX) {
        dico("javascript: troppi cambi di indirizzo di fila, mi fermo");
        return 0;
    }
    if (!risolvi(dove, assoluto, sizeof(assoluto))) return 0;

    ex_testo_metti(g_url, assoluto);
    g_js_salta = 1;
    vai(assoluto, 1, 0);
    return 1;
}

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
    char         stato_codice[16];
    unsigned int n = 0;
    int          da_cache = 0;

    if (!url || !url[0]) { g_da_postare = 0; return; }

    /* Vedi segui_location: chi arriva di li' alza il conto, tutti gli altri
     * lo azzerano — e «tutti gli altri» vuol dire una persona che ha cliccato
     * o scritto un indirizzo. */
    if (g_js_salta) { g_js_salti++; g_js_salta = 0; } else g_js_salti = 0;

    /* ! UNA PAGINA MANDATA IN POST NON SI PRENDE DALLA CACHE, ne' ci finisce
     * dentro: la risposta a un invio dipende da cosa si e' inviato, e servirla
     * a un altro invio sarebbe la risposta sbagliata. */
    if (g_da_postare) usa_cache = 0;

    dico(e_locale(url) ? "sto aprendo il file..." : "sto scaricando...");
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);

    memset(&e, 0, sizeof(e));

    /* ! IL DISCO PRIMA DI TUTTO IL RESTO, e senza passare dalla cache: vedi il
     * blocco «I FILE LOCALI» piu' in alto. */
    if (e_locale(url)) {
        if (!locale_leggi(url, g_pagina, sizeof(g_pagina), &n, &e.troncata)) {
            char perc[PERC_MAX];

            /* ! SI DICE IL PERCORSO, NON L'INDIRIZZO. Chi ha sbagliato a
             * scrivere ha in mente un file, e «file:///exwin/doc/x.html: non
             * si apre» lo fa cercare nello schema; il percorso nudo gli fa
             * cercare dove guarda lui, cioe' nella directory. */
            if (!percorso_di(url, perc, sizeof(perc)))
                strcpy(perc, "(nessun percorso)");
            sprintf(msg, "%s: non si apre", perc);
            dico(msg);
            g_da_postare = 0;
            return;
        }
        e.codice = 200;
        e.byte   = n;
        strncpy(e.finale, url, sizeof(e.finale) - 1);
        e.finale[sizeof(e.finale) - 1] = '\0';
    } else if (usa_cache && cache_leggi(url, g_pagina, sizeof(g_pagina), &n)) {
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

    if (e_locale(url)) strcpy(stato_codice, "dal disco");
    else               sprintf(stato_codice, "%d", e.codice);

    /* ! DA QUI IN POI LA MISURA E' DI TUTTI, e «Salva» e' l'unico che la
     * guarda: senza, scriverebbe il buffer intero invece della pagina. */
    g_pagina_n = e.byte;

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

    /* ! E CON LORO SE NE VA IL MOTORE, per lo stesso identico motivo: un
     * contesto JavaScript e' pieno di involucri che sono indici in QUESTO
     * albero, e fra un istante l'albero e' un altro. Tenerlo aperto vorrebbe
     * dire uno script della pagina di prima che tocca i nodi di quella di
     * adesso. */
    motore_chiudi();

    html_prepara(&g_doc, g_nodi, NODI_MAX, g_attr, ATTR_MAX,
                 g_arena, ARENA_MAX);
    html_analizza(&g_doc, (const char *)g_pagina, e.byte);

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

    /* ! GLI SCRIPT PRIMA DEI FOGLI DI STILE E DELL'IMPAGINAZIONE. Il perche'
     * sta accanto a esegui_script: uno script che costruisce meta' della
     * pagina deve aver finito prima che si decida come impaginarla. */
    esegui_script();

    raccogli_css();
    impagina();
    g_vista = html_versione(&g_doc);

    /* ! E SI SCRIVE CON snprintf, non con sprintf: la riga qui sotto e' fatta
     * di pezzi che dipendono dalla pagina, e nessuno di loro ha una lunghezza
     * che si possa contare guardando il codice.
     *
     * ! QUANDO SI TRONCA SI DICE ANCHE DI QUANTO, e non e' vanita' di numeri:
     * «albero troncato» non dice se manca un tetto di poco o di molto, e i
     * tetti di questo browser sono quattro. Con i numeri davanti si sa quale
     * alzare — e si sa anche quando NON serve alzare niente. */
    /* ! UN FILE LETTO DAL DISCO NON HA UN CODICE HTTP, e stampare «200» su una
     * pagina che la rete non l'ha nemmeno toccata e' una riga di stato che
     * dice il falso su cosa e' appena successo. */
    if (g_doc.troncato || g_css.troncato || e.troncata) {
        snprintf(msg, sizeof(msg),
                 "%s, %u byte%s%s%s%s  [nodi %u/%u, testo %uK/%uK, "
                 "pezzi %u/%u]",
                stato_codice, e.byte,
                da_cache == 2 ? " (copia locale: la rete non risponde)"
                              : da_cache ? " (dalla cache)" : "",
                e.troncata ? " pagina troncata" : "",
                g_doc.troncato ? " albero troncato" : "",
                g_css.troncato ? " stile troncato" : "",
                g_doc.nodi_n, (unsigned int)NODI_MAX,
                g_doc.arena_n / 1024u, (unsigned int)(ARENA_MAX / 1024u),
                (unsigned int)g_pez_n, (unsigned int)PEZZI_MAX);
    } else {
        snprintf(msg, sizeof(msg), "%s, %u byte, %u nodi%s",
                 stato_codice, e.byte, g_doc.nodi_n,
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

    /* ! UNA PAGINA BIANCA NON E' UN BROWSER ROTTO, MA LO SEMBRA — e si dice.
     * Da quando <noscript> non si mostra piu' con gli script accesi, un sito
     * che costruisce TUTTO in JavaScript e non ci riesce lascia lo schermo
     * vuoto e nessuna spiegazione: e' il caso della pagina dei risultati di
     * google.com. Prima si vedeva il ripiego «se non vieni reindirizzato...»
     * e sembrava che il motore non girasse; adesso non si vede niente e
     * sembra che sia rotto il browser. La verita' e' la terza: quella pagina
     * qui non si puo' disegnare, e la riga di stato e' il posto per dirlo.
     *
     * ! SI DICE SOLO SE GLI SCRIPT CI SONO DAVVERO. Un documento vuoto e'
     * vuoto e basta, e dare la colpa a un JavaScript che non c'e' manderebbe
     * a cercare un guasto dove non ce n'e' nessuno.
     *
     * ! ED E' CORTA PERCHE' LA RIGA DI STATO TAGLIA. La prima stesura diceva
     * «niente da mostrare: questa pagina si disegna da sola con JavaScript» e
     * sullo schermo si leggeva «...questa pagina si»: un messaggio troncato a
     * meta' e' peggio di uno breve, perche' sembra un guasto anche lui. */
    if (g_pez_n == 0 && ha_script()) {
        int l = (int)strlen(msg);

        snprintf(msg + l, sizeof(msg) - (size_t)l,
                 "  [vuota: la disegna JavaScript]");
    }
    dico(msg);
    g_da_postare = 0;

    /* ! IN FONDO, E NON PRIMA: se uno script della pagina appena caricata ha
     * chiesto un altro indirizzo, si va li' adesso — con l'albero, i controlli
     * e la riga di stato di QUESTA pagina gia' a posto. Chiamarla in mezzo a
     * `vai` vorrebbe dire una pagina caricata a meta' dentro un'altra. */
    segui_location();
}


/* =============================================================================
 * LE VOCI DEL MENU
 *
 * ! LE STESSE DECISIONI CHE SI PRENDONO DAI TASTI, e per questo stanno in
 * funzioni e non dentro il ramo EXM_COMANDO: «Apri» si sceglie dal menu o con
 * Ctrl+O, e le due strade devono arrivare nello stesso posto. Il toolkit manda
 * la voce di menu e il pulsante come lo stesso messaggio proprio per questo —
 * le scorciatoie no, quelle le esegue l'applicazione, che e' l'unica a sapere
 * se in quel momento hanno senso.
 * ============================================================================= */

/* Il nome che si propone salvando: l'ultimo pezzo dell'indirizzo. Un indirizzo
 * che finisce con «/» non ne ha uno, e allora vale la convenzione di tutti. */
static void nome_proposto(const char *url, char *out, unsigned int max)
{
    int i, fine, inizio;

    for (i = 0; url[i]; i++) { }
    fine = i;

    /* Via l'ancora e la query: non fanno parte del nome di un file. */
    for (i = 0; i < fine; i++)
        if (url[i] == '?' || url[i] == '#') { fine = i; break; }

    inizio = fine;
    while (inizio > 0 && url[inizio - 1] != '/') inizio--;

    if (inizio >= fine) { strncpy(out, "pagina.html", max - 1);
                          out[max - 1] = '\0'; return; }

    snprintf(out, max, "%.*s", fine - inizio, url + inizio);
}

static void apri_locale(void)
{
    char perc[PERC_MAX];
    char url[EXHTTP_URL_MAX];

    /* ! IL DIALOGO PARTE DA DOVE SI E' GIA'. Se la pagina di adesso e' un
     * file, si riparte dalla sua directory: chi apre due pagine della
     * documentazione di fila non deve rifare la strada la seconda volta. */
    perc[0] = '\0';
    if (!e_locale(g_qui) || !percorso_di(g_qui, perc, sizeof(perc))) {
        /* Non si viene da un file: si parte da casa, non dalla radice. Chi
         * apre una pagina che ha scritto lui l'ha scritta li'. */
        const char *casa = getenv("HOME");

        if (casa && casa[0] && strlen(casa) + 2 < sizeof(perc))
            snprintf(perc, sizeof(perc), "%s%s", casa,
                     casa[strlen(casa) - 1] == '/' ? "" : "/");
    }

    if (!ex_dlg_apri(perc, sizeof(perc))) { dico("apertura annullata"); return; }
    if (!perc[0])                          { dico("nessun file scelto");  return; }

    url_di_percorso(perc, url, sizeof(url));
    ex_testo_metti(g_url, url);
    vai(url, 1, 0);
}

/* =============================================================================
 * ! SI SALVA L'HTML COM'E' ARRIVATO, NON LA PAGINA COME SI VEDE, ed e' una
 * differenza che va detta e non nascosta. Nel file finiscono i byte del
 * documento: non le immagini, non i fogli di stile esterni, non quello che
 * uno script ha costruito dopo. Riaprendolo da qui si rivede la stessa pagina
 * solo se non aveva nulla di tutto cio' — ed e' esattamente quel che fa
 * «Salva pagina con nome» di qualunque browser quando si sceglie «solo HTML».
 *
 * ! E SI SALVA g_pagina, NON L'ALBERO. Serializzare il documento darebbe un
 * HTML RIPARATO — con i tag chiusi che la pagina non aveva — cioe' un file
 * diverso da quello che il sito ha mandato. Chi salva una pagina per guardarci
 * dentro vuole l'originale, difetti compresi.
 * ============================================================================= */
static void salva_pagina(void)
{
    char         perc[PERC_MAX];
    char         msg[PERC_MAX + 64];
    int          fd;
    unsigned int scritti = 0;

    if (!g_qui[0] || g_pagina_n == 0) {
        dico("non c'e' nessuna pagina da salvare");
        return;
    }

    /* =====================================================================
     * ! IL NOME PROPOSTO DEV'ESSERE UN PERCORSO ASSOLUTO, e non e' un
     * dettaglio di stile: ExDlg spezza cio' che riceve in directory e nome
     * cercando l'ultima barra, e su un nome nudo — «browser.html» — la barra
     * non c'e'. Quel che ne esce non e' «la directory di adesso»: e' la prima
     * lettera presa per directory e il resto per nome. Un salvataggio che
     * comincia con una destinazione inventata e' peggio di uno che chiede.
     *
     * ! DOVE, IN ORDINE: la directory della pagina se e' un file locale —
     * salvare accanto all'originale e' quel che si sta facendo se si e' li' —
     * altrimenti $HOME, che e' il posto di chi sta usando la macchina. La
     * radice solo se HOME non c'e'.
     * ===================================================================== */
    {
        char        nome[PERC_MAX];
        const char *casa = getenv("HOME");

        nome_proposto(g_qui, nome, sizeof(nome));

        if (e_locale(g_qui) && percorso_di(g_qui, perc, sizeof(perc))) {
            int i;

            for (i = 0; perc[i]; i++) { }
            while (i > 0 && perc[i - 1] != '/') i--;
            perc[i] = '\0';
            strncat(perc, nome, sizeof(perc) - strlen(perc) - 1);
        } else if (casa && casa[0] && strlen(casa) + strlen(nome) + 2 < sizeof(perc)) {
            snprintf(perc, sizeof(perc), "%s%s%s", casa,
                     casa[strlen(casa) - 1] == '/' ? "" : "/", nome);
        } else {
            snprintf(perc, sizeof(perc), "/%s", nome);
        }
    }

    if (!ex_dlg_salva(perc, sizeof(perc))) { dico("salvataggio annullato"); return; }
    if (!perc[0])                          { dico("nessun nome: non salvo");  return; }

    fd = open(perc, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        sprintf(msg, "non riesco a scrivere %s", perc);
        dico(msg);
        return;
    }

    while (scritti < g_pagina_n) {
        int k = (int)write(fd, g_pagina + scritti, g_pagina_n - scritti);

        if (k <= 0) break;
        scritti += (unsigned int)k;
    }
    close(fd);

    /* ! UN FILE SCRITTO A META' SI DICE, e non si cancella: chi salva una
     * pagina perche' il disco sta finendo preferisce i due terzi che ci sono
     * stati al niente che gli lascerebbe una pulizia zelante. */
    if (scritti == g_pagina_n)
        sprintf(msg, "salvato: %s (%u byte)", perc, scritti);
    else
        sprintf(msg, "%s: scritti solo %u byte su %u", perc, scritti, g_pagina_n);
    dico(msg);
}

/* =============================================================================
 * L'AIUTO E' UNA PAGINA, E LA APRE QUESTO BROWSER
 *
 * ! NON E' UN DIALOGO PIENO DI TESTO, ed e' una scelta e non una comodita': la
 * documentazione di un browser e' fatta di titoli, elenchi ed esempi, cioe' di
 * cose che un avviso a una schermata non sa mostrare. E aprirla con se stesso
 * vuol dire che il giorno che l'HTML si vede male, si vede male anche l'aiuto:
 * il difetto si presenta da solo invece di nascondersi.
 *
 * ! I DUE POSTI SONO QUELLI DEI FONT, E PER LA STESSA RAGIONE. Sul sistema
 * installato la guida sta in /exwin/doc; avviando dal CD la radice e' un
 * altro disco, e /cdrom e' dove si trova. Chi scrive il programma non deve
 * sapere quale dei due casi e' quello di adesso.
 * ============================================================================= */
/* ! IL NOME DELLA PAGINA E' UN ARGOMENTO, e le voci di menu sono due: il
 * manuale del navigatore e l'indice di tutta la documentazione. La ricerca nei
 * due posti resta una sola — due copie divergerebbero il giorno che se ne
 * aggiunge un terzo. */
static void doc_apri(const char *nome)
{
    static const char *const DOVE[] = { "/exwin/doc", "/cdrom/exwin/doc" };
    char perc[PERC_MAX];
    char url[EXHTTP_URL_MAX];
    int  i;

    for (i = 0; i < (int)(sizeof(DOVE) / sizeof(DOVE[0])); i++) {
        snprintf(perc, sizeof(perc), "%s/%s", DOVE[i], nome);
        if (!locale_esiste(perc)) continue;

        url_di_percorso(perc, url, sizeof(url));
        ex_testo_metti(g_url, url);
        vai(url, 1, 0);
        return;
    }

    ex_dlg_avviso("Documentazione",
                  "Le pagine della guida non ci sono.  Dovrebbero stare in "
                  "/exwin/doc, oppure in /cdrom/exwin/doc avviando dal CD.  "
                  "Fanno parte del componente /exwin: reinstallandolo "
                  "tornano al loro posto.");
}

static void informazioni(void)
{
    char t[900];
    char coda[128];

    exinfo_testo(t, sizeof(t), "Navigatore", VERSIONE_APP,
                 "Il browser di EX-OS.  Mette insieme exhttp per la rete, "
                 "exhtml per l'albero, excss per i fogli di stile, exjs ed "
                 "exdom per JavaScript, eximg per le immagini e i font per "
                 "misurare e disegnare il testo.  http e https (TLS 1.3, "
                 "certificati verificati) e i file locali con file:.");

    /* ! LA RIGA DEI CARATTERI STA QUI E NON IN exinfo, perche' e' una cosa di
     * QUESTO programma: exinfo dice chi l'ha scritto e quanta memoria usa, che
     * valgono per tutti. Vedi i due contatori accanto a font_per(). */
    snprintf(coda, sizeof(coda),
             "\nCaratteri: %d facce aperte, %d ripiegate sul font di sistema.",
             g_facce_si, g_facce_no);
    strncat(t, coda, sizeof(t) - strlen(t) - 1);

    /* ! QUELLO CHE GIRA DAVVERO, non quello che si e' chiesto. Se si e' scelto
     * QuickJS e quickjs.so non e' installata, lo stub apre ExJs e va avanti:
     * senza questa riga il ripiego sarebbe invisibile, e la pagina che non
     * funziona sembrerebbe colpa del motore sbagliato. */
    {
        int ora = exjs_motore_ora();

        snprintf(coda, sizeof(coda), "\nMotore JavaScript: %s.",
                 ora < 0 ? (g_qjs ? "QuickJS (non ancora aperto)"
                                  : "ExJs (non ancora aperto)")
                         : (ora ? "QuickJS" : "ExJs"));
        strncat(t, coda, sizeof(t) - strlen(t) - 1);
    }

    ex_dlg_avviso("Informazioni su", t);
}


/* =============================================================================
 * LA SCHERMATA DELLE IMPOSTAZIONI
 *
 * ! E' UNA FINESTRA MODALE CON UN CICLO SUO, come la tendina di un <select>:
 * si apre, si gira dentro finche' non si e' deciso, si chiude e si ridisegna
 * la pagina sotto. Il ciclo dei messaggi resta uno solo — `ex_prendi_msg`
 * continua a smistare a tutte le finestre — quindi il resto
 * dell'applicazione non muore mentre e' aperta.
 *
 * ! GLI INTERRUTTORI SONO PULSANTI CHE CAMBIANO SCRITTA, e non caselle da
 * spuntare: nel toolkit una casella di spunta non c'e', e disegnarne una a
 * mano qui dentro vorrebbe dire un controllo che vive in un programma solo.
 * Un pulsante che dice «JavaScript: acceso» dice lo stato E cosa succede a
 * premerlo, che e' quello che serve.
 *
 * ! SI LAVORA SU UNA COPIA, e si scrive solo su «Salva». Cambiare le
 * variabili vere a ogni clic vorrebbe dire che «Annulla» deve saper tornare
 * indietro — cioe' tenere la copia lo stesso, ma nel posto piu' scomodo.
 * ============================================================================= */
static ExFinestra g_imp_f, g_imp_campo, g_imp_bjs, g_imp_bimg, g_imp_bcache;
static ExFinestra g_imp_bmotore;
static int        g_imp_fatto;          /* 0 = aperta, 1 = salva, 2 = annulla */
static int        g_imp_js, g_imp_img, g_imp_cache, g_imp_qjs;

static void imp_etichette(void)
{
    char t[80];

    snprintf(t, sizeof(t), "JavaScript:  %s", g_imp_js ? "acceso" : "spento");
    ex_titolo(g_imp_bjs, t);
    /* ! IL NOME DEL MOTORE PORTA CON SE' LA SUA TAGLIA, e non e' un vezzo: la
     * differenza fra i due e' mezzo megabyte, ed e' l'unica cosa che chi
     * sceglie deve sapere per scegliere. */
    snprintf(t, sizeof(t), "Motore:  %s", g_imp_qjs ? "QuickJS (594 KB)"
                                                    : "ExJs (66 KB)");
    ex_titolo(g_imp_bmotore, t);
    snprintf(t, sizeof(t), "Immagini:  %s", g_imp_img ? "accese" : "spente");
    ex_titolo(g_imp_bimg, t);
    snprintf(t, sizeof(t), "Cache su disco:  %s",
             g_imp_cache ? "accesa" : "spenta");
    ex_titolo(g_imp_bcache, t);

    /* ex_titolo cambia la scritta e non ridisegna: il disegno lo chiede chi
     * sa che e' cambiata qualcosa. */
    ex_procedura_base(g_imp_f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(g_imp_f);
}

static long imp_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CHIUDI:
        g_imp_fatto = 2;
        return 0;

    case EXM_COMANDO:
        if (wp == ID_IMP_JS)    { g_imp_js    = !g_imp_js;    imp_etichette(); return 0; }
        if (wp == ID_IMP_IMG)   { g_imp_img   = !g_imp_img;   imp_etichette(); return 0; }
        if (wp == ID_IMP_CACHE) { g_imp_cache = !g_imp_cache; imp_etichette(); return 0; }
        if (wp == ID_IMP_MOTORE){ g_imp_qjs   = !g_imp_qjs;   imp_etichette(); return 0; }

        /* ! «USA LA PAGINA DI ADESSO» E' L'UNICO MODO COMODO DI RIEMPIRE QUEL
         * CAMPO. Nessuno ricopia a mano un indirizzo lungo guardandolo nella
         * finestra di sotto. */
        if (wp == ID_IMP_ORA) {
            if (g_qui[0]) ex_testo_metti(g_imp_campo, g_qui);
            return 0;
        }

        if (wp == ID_IMP_SALVA)   { g_imp_fatto = 1; return 0; }
        if (wp == ID_IMP_ANNULLA) { g_imp_fatto = 2; return 0; }
        return 0;

    case EXM_TASTO: {
        unsigned int c = wp & KBD_KEY_MASK;

        if (c == 27)                  g_imp_fatto = 2;
        else if (c == '\n' || c == '\r') g_imp_fatto = 1;
        return 0;
    }

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }
}

static void impostazioni(void)
{
    const int    W = 440, H = 300;
    ExMsg        m;
    unsigned int sw = 0, sh = 0;
    int          x, y;

    ex_schermo(&sw, &sh);
    x = ((int)sw - W) / 2;
    y = ((int)sh - H) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    g_imp_js    = g_js_acceso;
    g_imp_img   = g_img_accese;
    g_imp_cache = g_cache_accesa;
    g_imp_qjs   = g_qjs;
    g_imp_fatto = 0;

    g_imp_f = ex_crea("finestra", "Impostazioni",
                      EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_SOPRA | EX_MODALE,
                      x, y, W, H, 0, 0, imp_proc);
    if (!g_imp_f) { dico("non riesco ad aprire le impostazioni"); return; }

    ex_crea("etichetta", "Pagina iniziale - si apre all'avvio:", EX_FIGLIO,
            12, 10, 416, 16, g_imp_f, 0, 0);
    g_imp_campo = ex_crea("testo", "", EX_FIGLIO, 12, 30, 416, 22,
                          g_imp_f, ID_IMP_HOME, 0);
    ex_crea("pulsante", "Usa la pagina di adesso", EX_FIGLIO,
            12, 58, 200, 22, g_imp_f, ID_IMP_ORA, 0);

    ex_crea("etichetta", "Che cosa puo' fare una pagina:", EX_FIGLIO,
            12, 96, 416, 16, g_imp_f, 0, 0);
    g_imp_bjs    = ex_crea("pulsante", "", EX_FIGLIO, 12, 116, 220, 24,
                           g_imp_f, ID_IMP_JS, 0);
    g_imp_bimg   = ex_crea("pulsante", "", EX_FIGLIO, 12, 146, 220, 24,
                           g_imp_f, ID_IMP_IMG, 0);
    g_imp_bcache = ex_crea("pulsante", "", EX_FIGLIO, 12, 176, 220, 24,
                           g_imp_f, ID_IMP_CACHE, 0);
    g_imp_bmotore = ex_crea("pulsante", "", EX_FIGLIO, 240, 116, 188, 24,
                            g_imp_f, ID_IMP_MOTORE, 0);

    ex_crea("etichetta", "Si scrivono in $HOME/.app/browser/impostazioni.txt",
            EX_FIGLIO, 12, 214, 416, 16, g_imp_f, 0, 0);
    ex_crea("etichetta", "e si possono modificare a mano.",
            EX_FIGLIO, 12, 232, 416, 16, g_imp_f, 0, 0);

    ex_crea("pulsante", "Salva",   EX_FIGLIO, 12,  260, 100, 26,
            g_imp_f, ID_IMP_SALVA, 0);
    ex_crea("pulsante", "Annulla", EX_FIGLIO, 120, 260, 100, 26,
            g_imp_f, ID_IMP_ANNULLA, 0);

    ex_testo_metti(g_imp_campo, g_home);
    imp_etichette();
    ex_fuoco(g_imp_campo);

    while (!g_imp_fatto && ex_prendi_msg(&m)) ex_smista(&m);

    if (g_imp_fatto == 1) {
        const char *t = ex_testo_prendi(g_imp_campo);
        int         js_era = g_js_acceso;

        strncpy(g_home, t ? t : "", sizeof(g_home) - 1);
        g_home[sizeof(g_home) - 1] = '\0';
        g_js_acceso    = g_imp_js;
        g_img_accese   = g_imp_img;
        g_cache_accesa = g_imp_cache;
        g_qjs          = g_imp_qjs;

        ex_distruggi(g_imp_f);
        g_imp_f = 0;

        /* ! SI DICE ANCHE QUANDO NON SI RIESCE A SCRIVERE, e le impostazioni
         * valgono lo stesso per questa sessione: perderle in silenzio al
         * prossimo avvio sarebbe il modo peggiore di scoprire che HOME non
         * c'e'. */
        if (!imp_scrivi())
            dico("impostazioni valide per questa sessione: il file non si scrive");
        else if (js_era != g_js_acceso)
            dico(g_js_acceso ? "impostazioni salvate: JavaScript acceso dalla "
                               "prossima pagina"
                             : "impostazioni salvate: JavaScript spento dalla "
                               "prossima pagina");
        else
            dico("impostazioni salvate");
    } else {
        ex_distruggi(g_imp_f);
        g_imp_f = 0;
        dico("impostazioni non cambiate");
    }

    /* La finestra che se ne va lascia il suo buco: il server ridisegna cio'
     * che stava sotto solo se qualcuno glielo chiede. */
    disegna();
}

/* Va alla pagina iniziale. Se non ce n'e' una, lo dice invece di non fare
 * niente: un comando che non risponde sembra rotto. */
static void vai_a_casa(void)
{
    if (!g_home[0]) {
        dico("nessuna pagina iniziale: scegline una in File > Impostazioni");
        return;
    }
    ex_testo_metti(g_url, g_home);
    vai(g_home, 1, 1);
}

/* La pagina iniziale, quando nessuno ne ha ancora scelta una. */
static void home_predefinita(void)
{
    static const char *const DOVE[] = {
        "/exwin/doc/index.html",
        "/cdrom/exwin/doc/index.html"
    };
    int i;

    for (i = 0; i < (int)(sizeof(DOVE) / sizeof(DOVE[0])); i++)
        if (locale_esiste(DOVE[i])) {
            url_di_percorso(DOVE[i], g_home, sizeof(g_home));
            return;
        }
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

/* ! IL CORPO DI UNA POST NON HA LA MISURA DI UN INDIRIZZO, e per un pezzo qui
 * ce l'ha avuta: i campi finivano in un buffer da EXHTTP_URL_MAX, seicento
 * byte, che e' il tetto giusto per un URL e non per un modulo. Un modulo di
 * consenso — quello di google.com ne ha tredici, uno solo da duecentocinquanta
 * caratteri — lo supera senza sforzo, e il corpo partiva TAGLIATO: il server
 * rispondeva «400, la richiesta e' malformata» e non c'era modo di capire
 * perche' guardando lo schermo.
 *
 * ! E ADESSO QUANDO NON CI STA NON SI MANDA. Un modulo troncato non e' un
 * modulo con qualche campo in meno: e' una richiesta che il server rifiuta, o
 * peggio accetta a meta'. Meglio dirlo e fermarsi. */
#define MODULO_CORPO_MAX  (8u * 1024u)

static void manda_modulo(int m)
{
    static char q[MODULO_CORPO_MAX];
    char meta[EXHTTP_URL_MAX];
    int  pos = 0, primo = 1, i, pieno = 0;

    if (m < 0 || m >= g_mod_n) {
        dico("questo campo non sta dentro nessun modulo");
        return;
    }

    for (i = 0; i < g_ctrl_n; i++) {
        const Ctrl *c = &g_ctrl[i];

        if (pos >= (int)sizeof(q) - 8) { pieno = 1; break; }
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

    if (pieno) {
        dico("il modulo e' troppo grande: non lo mando a meta'");
        return;
    }

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
        if (!g_mod[m].post && pos > 0) {
            int j = 0;

            /* ! IN GET IL TETTO E' QUELLO DELL'INDIRIZZO, e li' non si scappa:
             * un URL ha una lunghezza massima. Se i campi non ci stanno si
             * dice, invece di mandarne meta' — che e' una ricerca sbagliata
             * spacciata per giusta. */
            if (k >= (int)sizeof(nuovo) - 2 ||
                pos > (int)sizeof(nuovo) - 2 - k) {
                dico("il modulo non sta in un indirizzo: servirebbe un POST");
                return;
            }
            nuovo[k++] = '?';
            while (q[j] && k < (int)sizeof(nuovo) - 1) nuovo[k++] = q[j++];
        }
        nuovo[k] = '\0';

        {
            static char corpo[MODULO_CORPO_MAX];
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
        if (wp == ID_INFO)  { informazioni(); return 0; }
        if (wp == ID_APRI)  { apri_locale();  return 0; }
        if (wp == ID_SALVA) { salva_pagina(); return 0; }
        if (wp == ID_IMPOST) { impostazioni(); return 0; }
        if (wp == ID_HOME)   { vai_a_casa();   return 0; }
        if (wp == ID_AIUTO) { doc_apri("browser.html"); return 0; }
        if (wp == ID_DOC)   { doc_apri("index.html");   return 0; }

        /* ! «ESCI» NON CHIEDE NIENTE, e qui e' giusto: un browser non ha un
         * lavoro non salvato da perdere. La domanda la fa l'editor, che ce
         * l'ha. */
        if (wp == ID_ESCI)  { ex_esci(0);     return 0; }
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
        /* ! IL CARATTERE E I MODIFICATORI SONO DUE COSE, e qui c'era solo la
         * prima: `wp & 0xFFFF` butta via i bit alti, che sono Ctrl, Shift e
         * Alt. Finche' il browser non li guardava non si notava; il giorno di
         * Ctrl+C il sintomo e' stato una `c` scritta dentro la casella — cioe'
         * il tasto e' arrivato, spogliato di quel che lo distingueva. */
        unsigned int c = wp & KBD_KEY_MASK;

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
            if (k->sel > (short)n) k->sel = -1;

            /* =================================================================
             * ! GLI APPUNTI SONO QUELLI DI TUTTA LA SCRIVANIA, non un blocco
             * del browser: la zona di memoria condivisa e' la stessa che usa
             * `ex_area`, quindi si copia da una casella di un modulo e si
             * incolla in un editor, e viceversa. Due «ultimi copiati» sarebbe
             * la cosa peggiore — nessuno saprebbe quale sta usando.
             *
             * ! E LA SELEZIONE SI CANCELLA SCRIVENDOCI SOPRA, com'e' ovunque:
             * chi ha scelto tre lettere e batte un tasto si aspetta che quelle
             * tre spariscano, non che il testo si allunghi.
             * ================================================================= */
            /* ! LE DUE SCORCIATOIE, PRIMA DI TUTTO IL RESTO. Ctrl+Canc e
             * Shift+Canc devono essere guardate qui: piu' sotto c'e' il ramo
             * che cancella un carattere, e Canc da solo ci finirebbe dentro
             * portandosi via il taglio. */
            {
                int ctrl  = (wp & KBD_MOD_CTRL)  != 0;
                int shf   = (wp & KBD_MOD_SHIFT) != 0;
                int copia = 0, taglia = 0, incolla = 0;

                if (ctrl && (c == 'c' || c == 'C'))       copia   = 1;
                if (ctrl && (c == 'x' || c == 'X'))       taglia  = 1;
                if (ctrl && (c == 'v' || c == 'V'))       incolla = 1;

                /* CUA: quella di DOS, OS/2 e dei terminali Unix. */
                if (ctrl && c == KBD_K_INS)               copia   = 1;
                if (shf  && c == KBD_K_INS)               incolla = 1;
                if (shf  && c == KBD_K_DEL)               taglia  = 1;
                /* ! CTRL+CANC NON E' CUA — li' e' Shift+Canc — ma lo chiedono
                 * le dita di chi l'ha imparato altrove, e costa una riga.
                 * Averle tutt'e due non toglie niente a nessuno. */
                if (ctrl && c == KBD_K_DEL)               taglia  = 1;

                if (ctrl && (c == 'a' || c == 'A')) {
                    k->sel = 0;
                    k->cur = (short)n;
                    disegna();
                    return 0;
                }
                if (copia || taglia) {
                    app_copia(k, taglia);
                    disegna();
                    return 0;
                }
                if (incolla) {
                    app_incolla(k);
                    disegna();
                    return 0;
                }
            }

            /* =================================================================
             * ! I TASTI DI MOVIMENTO SI GUARDANO SENZA I MODIFICATORI, e prima
             * non era cosi': `c == KBD_K_LEFT` e' falso appena si tiene Shift,
             * perche' il modificatore sta nei bit alti. Finche' non c'era la
             * selezione non si notava — Shift+freccia semplicemente non faceva
             * niente; con la selezione sarebbe stato il difetto principale.
             *
             * ! E SHIFT POSA L'ANCORA, una freccia nuda la toglie. E' l'unica
             * regola che non stupisce, ed e' la stessa che segue `ex_area`.
             * ================================================================= */
            {
                unsigned int t     = c;
                int          shift = (wp & KBD_MOD_SHIFT) != 0;
                int          muove = (t == KBD_K_LEFT || t == KBD_K_RIGHT ||
                                      t == KBD_K_HOME || t == KBD_K_END ||
                                      t == KBD_K_UP   || t == KBD_K_DOWN);

                if (muove) {
                    if (shift) { if (k->sel < 0) k->sel = k->cur; }
                    else       k->sel = -1;
                }

                if (t == KBD_K_LEFT)  { if (k->cur > 0) k->cur--; disegna(); return 0; }
                if (t == KBD_K_RIGHT) { if (k->cur < (short)n) k->cur++; disegna(); return 0; }
                if (t == KBD_K_HOME)  { k->cur = 0;          disegna(); return 0; }
                if (t == KBD_K_END)   { k->cur = (short)n;   disegna(); return 0; }
            }

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

            /* ! CANCELLARE CON DEL TESTO SCELTO TOGLIE QUELLO, e non il
             * carattere accanto: e' la cosa che si sta guardando. */
            if (c == '\b' || c == KBD_K_DEL) {
                if (k->sel >= 0 && k->sel != k->cur) {
                    sel_togli(k);
                    disegna();
                    return 0;
                }
                if (c == '\b' && k->cur > 0) {
                    int i;

                    for (i = (int)k->cur - 1; i < n; i++)
                        k->valore[i] = k->valore[i + 1];
                    k->cur--;
                } else if (c == KBD_K_DEL && (int)k->cur < n) {
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
            if (c >= 32 && c < 256) {
                sel_togli(k);           /* scrivere sopra una scelta la sostituisce */
                ctrl_inserisci(k, (char)c);
                disegna();
            }
            return 0;
        }

        /* ! LE SCORCIATOIE DEL MENU FILE, e arrivano fin qui solo quando
         * nessuna casella di un modulo ha il fuoco — il ramo qui sopra e'
         * uscito prima. Con Ctrl premuto la casella dell'indirizzo lascia
         * passare il tasto, che e' esattamente cio' che permette a Ctrl+O di
         * funzionare mentre si sta scrivendo un indirizzo. */
        if (wp & KBD_MOD_CTRL) {
            if (c == 'o' || c == 'O') { apri_locale();  return 0; }
            if (c == 's' || c == 'S') { salva_pagina(); return 0; }
            if (c == 'q' || c == 'Q') { ex_esci(0);     return 0; }
            if (c == 'h' || c == 'H') { vai_a_casa();   return 0; }
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

    /* ! I TEMPI SI POMPANO DA QUI, e la risoluzione vera e' 200 ms — la
     * scadenza del poll dentro il ciclo dei messaggi, dichiarata accanto a
     * ex_sveglia. Un `setTimeout(f, 50)` girera' dopo 200: e' poco per
     * un'animazione e basta per tutto il resto, e dirlo e' meglio che
     * promettere i millisecondi e non darli.
     *
     * ! E LA SVEGLIA SI SPEGNE QUANDO NON ASPETTA PIU' NESSUNO. Una pagina che
     * ha finito non ha motivo di svegliare il browser cinque volte al secondo
     * per sempre. */
    case EXM_TEMPO:
        if (g_js) {
            exjs_pompa(g_js, uptime_ms());
            rifai_se_cambiato();
            dopo_gli_script();
            if (!exjs_lavori_in_attesa(g_js)) ex_sveglia(g_f, 0);
            /* Un `setTimeout` che cambia indirizzo e' il modo classico di
             * scrivere una redirezione: si guarda anche qui, non solo dopo
             * gli script della pagina. */
            segui_location();
        }
        return 0;

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

        /* ! IL DOCUMENTO SENTE IL CLIC PRIMA CHE IL BROWSER LO USI, ed e'
         * l'ordine giusto: preventDefault() esiste proprio per impedire a un
         * collegamento di essere seguito, e non potrebbe farlo se il
         * collegamento fosse gia' stato seguito. */
        /* ! ANCHE SE IL GESTORE HA DETTO «FERMO» SI GUARDA DOVE VUOLE
         * ANDARE, ed e' l'accoppiata piu' comune del web: `onclick` chiama
         * preventDefault() per non seguire l'href e poi assegna
         * `location.href` per andare da un'altra parte. Guardare solo quando
         * il clic prosegue vorrebbe dire perdere proprio quel caso. */
        if (!clic_al_documento(x, y)) { segui_location(); return 0; }
        /* Se lo script ha portato altrove, il collegamento sotto il dito
         * appartiene a una pagina che non c'e' piu': non lo si segue. */
        if (segui_location()) return 0;

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

    /* Anche queste due contano: sono le facce del testo normale e dei titoli,
     * cioe' le due che si notano per prime se non si aprono. */
    if (g_font_testo)  g_facce_si++; else g_facce_no++;
    if (g_font_titolo) g_facce_si++; else g_facce_no++;

    /* ! LE DUE DI PARTENZA SI DICONO SUBITO. Se non si aprono queste, non si
     * aprira' nessuna: sono le prime due chiamate del programma, quando la
     * memoria e' tutta libera e nessuna tabella e' piena. Vederlo qui vuol
     * dire che il guasto e' nel caricamento dei font e non nella pagina. */
    if (!g_font_testo || !g_font_titolo)
        printf("browser: i caratteri di base non si aprono "
               "(testo %s, titolo %s): si disegna col font di sistema.\n",
               g_font_testo ? "ok" : "NO", g_font_titolo ? "ok" : "NO");

    /* ! LA BARRA DEI MENU SI CREA PRIMA DEI CONTROLLI, e non e' indifferente:
     * sta a y = 0 e larga quanto la finestra, e tutto il resto comincia sotto
     * di lei — vedi BARRA_Y. */
    {
        ExFinestra menu = ex_menu(g_f);

        ex_menu_voce(menu, "File", "Apri...\tCtrl+O",             ID_APRI);
        ex_menu_voce(menu, "File", "Pagina iniziale\tCtrl+H",     ID_HOME);
        ex_menu_voce(menu, "File", "Salva con nome...\tCtrl+S",   ID_SALVA);
        ex_menu_voce(menu, "File", "-",                           0);
        ex_menu_voce(menu, "File", "Impostazioni...",             ID_IMPOST);
        ex_menu_voce(menu, "File", "-",                           0);
        ex_menu_voce(menu, "File", "Esci\tCtrl+Q",                ID_ESCI);

        ex_menu_voce(menu, "Aiuto", "Guida del navigatore",       ID_AIUTO);
        ex_menu_voce(menu, "Aiuto", "Documentazione di EX-OS",    ID_DOC);
        ex_menu_voce(menu, "Aiuto", "-",                          0);
        ex_menu_voce(menu, "Aiuto", "Informazioni su",            ID_INFO);
    }

    ex_crea("pulsante", "<", EX_FIGLIO, MARGINE, BARRA_Y + 4, 26, 22,
            g_f, ID_INDIETRO, 0);

    /* ! I DUE PULSANTI DI DESTRA SI MISURANO DALLA DESTRA, non dalla
     * sinistra: cosi' aggiungerne uno sposta solo il campo dell'indirizzo, che
     * e' l'unico pezzo che puo' restringersi senza diventare inutile. */
    ex_crea("pulsante", "?", EX_FIGLIO, FIN_W - MARGINE - 24, BARRA_Y + 4,
            24, 22, g_f, ID_INFO, 0);
    ex_crea("pulsante", "Vai", EX_FIGLIO, FIN_W - MARGINE - 24 - 4 - 44,
            BARRA_Y + 4, 44, 22, g_f, ID_VAI, 0);
    g_url = ex_crea("testo", "", EX_FIGLIO, MARGINE + 32, BARRA_Y + 4,
                    FIN_W - MARGINE - 24 - 4 - 44 - 4 - (MARGINE + 32), 22,
                    g_f, ID_URL, 0);

    g_stato = ex_crea("etichetta", "", EX_FIGLIO,
                      MARGINE, FIN_H - 18, FIN_W - 2 * MARGINE, 16, g_f, 0, 0);

    /* ! IL TETTO DELLE IMMAGINI SI SCEGLIE QUI, a finestra gia' aperta: prima
     * di ex_crea la memoria che il server a finestre prendera' per questa
     * finestra e' ancora libera, e conterebbe come nostra. */
    imm_tetto_scegli();

    cache_prepara();

    /* ! LE IMPOSTAZIONI PRIMA DI TUTTO CIO' CHE DIPENDE DA LORO, e la pagina
     * iniziale e' una di quelle. Leggerle dopo aver gia' aperto una pagina
     * vorrebbe dire aprirne due. */
    imp_prepara();
    imp_leggi();

    /* ! I BISCOTTI DOPO LE IMPOSTAZIONI, perche' il loro file sta accanto a
     * quello e riusa le directory che imp_prepara ha appena creato. E il
     * gancio si registra SUBITO: da questo momento ogni richiesta di exhttp
     * porta quel che c'e' nella dispensa, comprese quelle che partono dentro
     * una redirezione. */
    bis_azzera(&g_bis);
    if (bis_prepara()) bis_leggi_file();
    exhttp_biscotti(bis_chiedi, bis_arrivo, 0);
    if (!g_home[0]) home_predefinita();

    ex_fuoco(g_url);
    dico("scrivi un indirizzo e premi Invio. http, https e file locali.");
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    disegna();

    /* ! UN ARGOMENTO CHE COMINCIA CON «/» E' UN FILE, non un indirizzo, e
     * chiamarlo cosi' e' l'unico modo di scriverlo che venga in mente da una
     * riga di comando: `browser /exwin/doc/browser.html`. Lo si trasforma qui
     * in un «file:», che e' la sola forma che il resto del programma conosce. */
    if (argc >= 2) {
        char primo[EXHTTP_URL_MAX];

        if (argv[1][0] == '/') url_di_percorso(argv[1], primo, sizeof(primo));
        else {
            strncpy(primo, argv[1], sizeof(primo) - 1);
            primo[sizeof(primo) - 1] = '\0';
        }

        ex_testo_metti(g_url, primo);
        vai(primo, 0, 0);
    } else if (g_home[0]) {
        /* ! L'ARGOMENTO BATTE LA PAGINA INIZIALE, e non e' discutibile: chi
         * scrive `browser qualcosa` ha chiesto QUELLA pagina. */
        ex_testo_metti(g_url, g_home);
        vai(g_home, 0, 1);
    }

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}

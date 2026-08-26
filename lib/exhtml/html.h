/* =============================================================================
 * lib/exhtml/html.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * HTML — da testo ad albero
 *
 * ! L'HTML NON E' XML, ED E' TUTTA LA DIFFICOLTA'. Un lettore XML puo'
 * rifiutare un documento malformato; un lettore HTML no, perche' quasi nessuna
 * pagina vera e' «ben formata»: i tag restano aperti, si chiudono nell'ordine
 * sbagliato, ne mancano meta'. Un browser che rifiutasse un documento cosi'
 * mostrerebbe una pagina bianca sul novanta per cento del web. Qui si accetta
 * tutto e si CHIUDE PER CONTO PROPRIO secondo regole scritte.
 *
 * ! NON ALLOCA NIENTE, e i buffer li da' chi chiama. Sono la stessa scelta di
 * raster.c: chi apre una pagina e' l'unico che sappia quanta memoria vuole
 * spenderci, e un albero che cresce quanto vuole il documento e' un documento
 * che decide quanta memoria prendere sulla macchina di chi lo guarda.
 *
 * ! E NON INCLUDE LA libc, come http.c, ttf.c e raster.c: si compila con un
 * `cc` sull'host e si prova contro pagine scritte a mano, invece di volere una
 * macchina virtuale per ogni riga cambiata.
 *
 * ! IL TESTO SI COPIA NELL'ARENA E NON SI PUNTA AL DOCUMENTO, e non e' spreco:
 * le entita' («&amp;») si sciolgono mentre si copia, quindi il testo dell'albero
 * e' gia' quello da disegnare. Puntando all'originale, ogni lettore dovrebbe
 * scioglierle da se' — cioe' la stessa tabella in due posti.
 * ============================================================================= */
#ifndef HTML_H
#define HTML_H

#ifdef __cplusplus
extern "C" {
#endif

#define HTML_ELEMENTO   0
#define HTML_TESTO      1

/* ! GLI INDICI SONO int E NON PUNTATORI, cosi' l'albero si puo' copiare,
 * salvare e guardare senza rilocare niente. -1 vuol dire «non c'e'». */
typedef struct {
    unsigned char tipo;
    int           padre;
    int           primo_figlio;
    int           ultimo_figlio;    /* per attaccare in coda senza scorrere */
    int           prossimo;         /* il fratello dopo */
    unsigned int  nome;             /* scostamento nell'arena: solo elementi */
    unsigned int  testo;            /* scostamento nell'arena: solo testo */
    int           attributi;        /* indice del primo attributo, -1 */
} HtmlNodo;

typedef struct {
    unsigned int nome;              /* scostamento nell'arena */
    unsigned int valore;            /* scostamento nell'arena */
    int          prossimo;
} HtmlAttr;

typedef struct {
    HtmlNodo    *nodi;
    unsigned int nodi_max, nodi_n;
    HtmlAttr    *attr;
    unsigned int attr_max, attr_n;
    char        *arena;
    unsigned int arena_max, arena_n;

    int          radice;

    /* =====================================================================
     * ! IL CONTATORE DELLE MODIFICHE, e senza di lui il DOM non serve a
     * niente.
     *
     * Uno script che cambia il documento deve far rifare l'impaginazione. Le
     * due strade sbagliate sono simmetriche: rifarla SEMPRE dopo ogni script —
     * e allora una pagina con dieci script si impagina dieci volte per niente
     * — oppure MAI, e allora la pagina mostra quello che c'era prima. Questo
     * numero cresce a ogni mutazione, e chi impagina confronta il suo con
     * quello di adesso: due interi, e la domanda ha una risposta esatta.
     *
     * ! CRESCE ANCHE PER LE MODIFICHE CHE NON SI VEDONO — un attributo che
     * nessuno legge, un nodo staccato e riattaccato dov'era. Distinguere «ha
     * cambiato qualcosa di visibile» da «ha cambiato qualcosa» vorrebbe dire
     * sapere cosa guarda l'impaginatore, cioe' mettere qui una conoscenza che
     * e' sua. Meglio un'impaginazione di troppo che una di meno.
     * ===================================================================== */
    unsigned int versione;

    /* ! CHE SIA FINITO LO SPAZIO SI DICE, non si lascia intuire da una pagina
     * a meta'. Un browser che mostra meno di quello che c'e' senza avvisare
     * fa credere che la pagina sia cosi'. */
    int          troncato;
} HtmlDoc;

/* Prepara il documento sui buffer di chi chiama. Va chiamata prima di
 * html_analizza, e rimette tutto a zero: lo stesso documento si puo' riusare
 * per una pagina nuova senza rifare i buffer. */
void html_prepara(HtmlDoc *d,
                  HtmlNodo *nodi, unsigned int nodi_max,
                  HtmlAttr *attr, unsigned int attr_max,
                  char *arena, unsigned int arena_max);

/* Costruisce l'albero. Rende 1 se ha prodotto qualcosa — anche troncato — e 0
 * solo se i buffer sono cosi' piccoli da non starci nemmeno la radice.
 *
 * ! NON RENDE 0 PER UN DOCUMENTO MALFATTO, apposta: vedi in cima. */
int html_analizza(HtmlDoc *d, const char *testo, unsigned int n);

/* Il nome di un elemento, o "" se il nodo e' testo. In MINUSCOLO: i nomi dei
 * tag non distinguono maiuscole, e deciderlo qui evita che ogni lettore
 * confronti due volte. */
const char *html_nome(const HtmlDoc *d, int nodo);

/* Il testo di un nodo di testo, o "". Le entita' sono gia' sciolte. */
const char *html_testo(const HtmlDoc *d, int nodo);

/* Il valore di un attributo, o 0 se l'elemento non ce l'ha. Il nome si
 * confronta senza distinguere maiuscole. */
const char *html_attr(const HtmlDoc *d, int nodo, const char *nome);

/* =============================================================================
 * MUTARE IL DOCUMENTO
 *
 * ! FINO A OGGI L'ALBERO ERA DI SOLA LETTURA, e andava benissimo: chi lo legge
 * e' l'impaginatore, che non ha motivo di toccarlo. Da quando c'e' un motore
 * JavaScript non basta piu' — uno script che puo' leggere la pagina e non
 * cambiarla non serve a niente, e `document.createElement` e' la prima riga di
 * qualunque codice vero.
 *
 * ! NIENTE SI LIBERA, e va saputo. Un nodo tolto lascia la sua casella
 * occupata, un attributo riscritto lascia la vecchia stringa nell'arena. E' la
 * stessa scelta dichiarata in ExJs, per la stessa ragione: un documento vive
 * quanto una pagina, e si butta tutto insieme. Diventera' un problema il
 * giorno che una pagina sola restera' aperta a mutare per ore — e quel giorno
 * il posto dove intervenire e' qui, non nei chiamanti.
 *
 * ! E OGNI FUNZIONE ALZA `versione`. Il perche' sta accanto al campo.
 * ========================================================================== */

/* Un elemento nuovo, ancora attaccato a niente. Rende l'indice, o -1 se non
 * c'e' piu' posto. Il nome si copia e si mette in minuscolo, come fa
 * l'analizzatore: `createElement('DIV')` e `<div>` devono dare la stessa cosa. */
int html_crea_elemento(HtmlDoc *d, const char *nome);

/* Un nodo di testo nuovo. ! IL TESTO SI PRENDE COM'E': le entita' NON si
 * sciolgono, perche' qui non arriva da un documento — arriva da uno script che
 * ha gia' scritto quello che voleva. Scioglierle vorrebbe dire che
 * `createTextNode('&amp;')` mostra una `&`, cioe' il contrario di quel che ha
 * chiesto chi lo ha scritto. */
int html_crea_testo(HtmlDoc *d, const char *testo);

/* Attacca `figlio` in fondo ai figli di `padre`. Se il figlio era attaccato
 * altrove, prima lo si stacca — come fa il DOM: un nodo sta in un posto solo,
 * e appendChild SPOSTA. Rende 1, o 0 se rifiuta.
 *
 * ! RIFIUTA DI FARE UN CICLO. Attaccare un nodo dentro un proprio discendente
 * darebbe un albero che non finisce, e chi lo percorre — l'impaginatore — ci
 * girerebbe dentro per sempre. Costa una risalita, e la si paga volentieri. */
int html_aggiungi(HtmlDoc *d, int padre, int figlio);

/* Come sopra, ma prima di `riferimento` (che dev'essere figlio di `padre`).
 * Con riferimento = -1 si comporta come html_aggiungi. */
int html_inserisci_prima(HtmlDoc *d, int padre, int figlio, int riferimento);

/* Stacca un nodo dal padre. Il nodo resta valido e si puo' riattaccare.
 * Rende 1, o 0 se non aveva un padre. */
int html_togli(HtmlDoc *d, int nodo);

/* Mette o cambia un attributo. Rende 1, o 0 se non c'e' posto. */
int html_attr_metti(HtmlDoc *d, int nodo, const char *nome, const char *valore);

/* Toglie un attributo. Rende 1 se c'era. */
int html_attr_togli(HtmlDoc *d, int nodo, const char *nome);

/* Cambia il testo di un nodo di testo. Rende 1, o 0 se non e' testo. */
int html_testo_metti(HtmlDoc *d, int nodo, const char *testo);

/* Il numero di modifiche fatte finora. Vedi il campo `versione`. */
unsigned int html_versione(const HtmlDoc *d);

/* Analizza `testo` DENTRO un elemento che esiste gia', attaccando quel che
 * trova in fondo ai suoi figli. E' la meta' che serve a `innerHTML = ...`,
 * insieme a html_svuota. Rende 1, o 0 se il nodo non e' un elemento valido.
 *
 * ! NON TOCCA LA RADICE e non azzera niente: chi vuole SOSTITUIRE il contenuto
 * chiama prima html_svuota. Tenerle separate lascia fare anche l'altra meta'
 * — `insertAdjacentHTML`, che aggiunge senza cancellare — senza una seconda
 * funzione quasi uguale. */
int html_analizza_in(HtmlDoc *d, int padre, const char *testo, unsigned int n);

/* Stacca tutti i figli di un nodo in un colpo solo. Rende 1, o 0 se il nodo
 * non e' valido. I figli restano validi e si possono riattaccare altrove. */
int html_svuota(HtmlDoc *d, int nodo);

/* Rimette in marcatore il sottoalbero: con `con_se_stesso` a 0 solo i figli
 * (`innerHTML`), a 1 anche il nodo stesso (`outerHTML`).
 *
 * ! RENDE LA LUNGHEZZA CHE SERVIVA, come snprintf, e scrive fino a `max`
 * lasciando sempre lo zero finale. Con `fuori` a 0 misura soltanto: cosi' chi
 * non sa quanto gli serve lo chiede, invece di tentare con buffer sempre piu'
 * grandi.
 *
 * ! NON RIDA' IL DOCUMENTO DI PARTENZA: gli spazi sono gia' ridotti e i
 * commenti gia' buttati. Rida' un marcatore che, rianalizzato, produce lo
 * stesso albero — che e' quel che `innerHTML` promette davvero. */
unsigned int html_serializza(HtmlDoc *d, int nodo, int con_se_stesso,
                             char *fuori, unsigned int max);

#ifdef __cplusplus
}
#endif

#endif /* HTML_H */

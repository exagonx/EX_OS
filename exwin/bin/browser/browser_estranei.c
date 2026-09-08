/* =============================================================================
 * exwin/bin/browser/browser_estranei.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * I PEZZI ESTRANEI — i moduli, le immagini e gli script, dalla parte del
 * navigatore
 *
 * ! QUESTO FILE NON E' UN TRASLOCO, E' UN CONFINE. Le righe che stanno qui
 * dentro erano in browser_impagina.c e sono le stesse: quel che e' cambiato e'
 * CHI le chiama. Prima l'impaginato riconosceva <input>, <img> e <noscript> di
 * persona; adesso chiede al cliente installato «di questo nodo che ne
 * facciamo?», e il cliente e' questo file. L'impaginato non nomina piu'
 * nessuno di quei tre tag.
 *
 * ! IL CLIENTE HA IL DIRITTO DI SAPERE TUTTO, ed e' il punto: qui si tocca
 * g_ctrl, g_imm, g_mod, g_js_acceso, la finestra e l'area del documento, cioe'
 * esattamente cio' che una libreria non puo' toccare. Il taglio serve a
 * mettere quel diritto DA UNA PARTE SOLA — vedi browser_vista.h.
 *
 * ! E DA QUI SI VEDE CHE COSA COSTEREBBE UN SECONDO CLIENTE. Il manuale dentro
 * exide e l'editor RTF vogliono l'impaginato senza i moduli e senza le
 * immagini di rete: il loro cliente e' una funzione che rende sempre
 * VISTA_NIENTE, cioe' venti righe. Prima sarebbe stato un altro navigatore.
 * ============================================================================= */
#include "browser_priv.h"
#include "browser_estranei.h"

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

/* Il <form> che contiene un controllo, o -1 se e' fuori da tutti.
 *
 * ! SI SALE L'ALBERO INVECE DI RICORDARE «IL MODULO APERTO», ed e' il pezzo di
 * questo taglio che si sarebbe potuto sbagliare. Prima l'impaginato teneva un
 * g_mod_ora: apriva il modulo quando incontrava <form>, lo richiudeva dopo i
 * figli, e ogni controllo prendeva quello. Era un legame in piu' — l'ordine
 * dell'impaginazione — per una risposta che l'ALBERO ha gia': nel DOM
 * `element.form` e' l'antenato <form>, non l'ultimo aperto.
 *
 * ! I MODULI SI REGISTRANO QUANDO SERVONO, non quando si incontrano. Un <form>
 * senza un solo controllo non entra piu' nell'elenco, e non manca a nessuno:
 * non c'e' niente da mandare. Il tetto MODULI_MAX vale adesso per i moduli che
 * CONTANO, che e' un modo migliore di spenderlo. */
static int est_modulo_di(int v)
{
    int su = g_doc.nodi[v].padre;
    int i;

    while (su >= 0) {
        if (g_doc.nodi[su].tipo == HTML_ELEMENTO &&
            uguale(html_nome(&g_doc, su), "form")) break;
        su = g_doc.nodi[su].padre;
    }
    if (su < 0) return -1;

    for (i = 0; i < g_mod_n; i++)
        if (g_mod[i].nodo == su) return i;
    if (g_mod_n >= MODULI_MAX) return -1;

    {
        const char *az = html_attr(&g_doc, su, "action");
        const char *me = html_attr(&g_doc, su, "method");
        int q = 0;

        g_mod[g_mod_n].nodo = su;
        g_mod[g_mod_n].post = (me && (uguale(me, "post") || uguale(me, "POST")));
        if (az) {
            while (az[q] && q < AZIONE_MAX - 1) { g_mod[g_mod_n].azione[q] = az[q]; q++; }
        }
        g_mod[g_mod_n].azione[q] = '\0';
    }
    return g_mod_n++;
}

/* =============================================================================
 * ! I CONTROLLI SONO UN PRODOTTO DELL'IMPAGINAZIONE, come i pezzi e i
 * collegamenti, e per molto tempo sono stati l'unico che non si azzerava a
 * ogni giro. Ogni `impagina()` ne accodava una copia nuova senza buttare le
 * vecchie: dodici reimpaginazioni di una pagina con cinque controlli ne
 * facevano sessanta, e a CTRL_MAX (64) l'impaginazione cominciava a
 * RINUNCIARE — non solo al controllo, ma a tutto il sottoalbero sotto di lui.
 *
 * ! E IL SINTOMO NON SOMIGLIAVA ALLA CAUSA: sparivano pezzi di pagina lontani
 * dai moduli, e sparivano solo sulle pagine con molte immagini — cioe' quelle
 * che si reimpaginano tante volte. Si e' visto confrontando due build sulla
 * stessa voce di Wikipedia: quella che reimpagina di meno mostrava PIU'
 * contenuto, che e' esattamente il contrario di quello che ci si aspetta da
 * un'ottimizzazione.
 *
 * ! QUESTO AZZERAMENTO ERA DENTRO impagina() E ADESSO E' UN GANCIO, ma resta
 * nello stesso istante: e' il motivo per cui il cliente ha un «si ricomincia»
 * invece di ripulirsi per conto suo.
 *
 * ! E g_opz_n NON SI AZZERA, come non si azzerava prima. Le opzioni dei
 * <select> si accodano a ogni giro finche' OPZ_MAX non e' pieno, e allora
 * l'elenco di una scelta resta quello del giro buono. E' una perdita
 * conosciuta, non un dimenticanza di oggi: toccarla adesso vorrebbe dire
 * cambiare due cose insieme e non sapere piu' quale ha mosso i pixel.
 * ============================================================================= */
static void est_azzera(void)
{
    g_ctrl_n = 0;
    g_mod_n  = 0;
}

/* =============================================================================
 * DI QUESTO NODO CHE NE FACCIAMO?
 *
 * ! L'ORDINE DELLE TRE DOMANDE E' QUELLO DI PRIMA, e non e' indifferente: gli
 * script vengono per primi perche' un <noscript> non deve nemmeno essere
 * guardato, e i controlli prima delle immagini perche' cosi' era.
 * ============================================================================= */
static int est_misura(int v, VistaPezzo *p)
{
    const char *nome = html_nome(&g_doc, v);

    /* ! IL <noscript> SPARISCE QUANDO IL MOTORE C'E', ed e' l'unico «script»
     * che l'impaginazione incontrasse. Si e' visto su google.com/search: la
     * pagina dei risultati ha TUTTO il contenuto dentro <noscript> — «Se non
     * vieni reindirizzato automaticamente entro alcuni secondi, fai clic qui»
     * — e i risultati veri li costruisce uno script. Il browser mostrava
     * quella riga e sembrava che il motore non girasse: girava, e quella riga
     * non doveva essere sullo schermo. */
    if (g_js_acceso && uguale(nome, "noscript")) return VISTA_SALTA;

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

        if (g_ctrl_n >= CTRL_MAX || g_pez_n >= PEZZI_MAX)
            return VISTA_SALTA;

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
            c->modulo  = (short)est_modulo_di(v);
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
        if (t == CTRL_NASCOSTO) { g_ctrl_n++; return VISTA_SALTA; }

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

        /* ! DI QUI IN GIU' NON SI IMPAGINA PIU', SI DICHIARA. La larghezza, lo
         * stacco e il respiro erano scritti dentro l'impaginazione; adesso
         * sono quattro campi che l'impaginato legge senza sapere di che pezzo
         * parla. I numeri sono gli stessi di prima, uno per uno. */
        p->w        = w;
        p->h        = h;
        p->rif      = EST_CTRL(g_ctrl_n);
        p->stringi  = 1;
        p->aria_dx  = 4;
        p->aria_giu = 4;
        g_ctrl_n++;
        return VISTA_PEZZO;
    }

    if (uguale(nome, "img")) {
        const char *src = html_attr(&g_doc, v, "src");
        const char *alt;
        int         k = (src && src[0]) ? imm_indice(v, src) : -1;

        /* ! UN'IMMAGINE STA NEL FLUSSO COME UNA PAROLA: eredita il
         * collegamento che la contiene, e diventa il nodo corrente — e' il
         * caso in cui `event.target` deve dire `IMG`. Prima erano due righe
         * in due posti diversi; adesso e' un campo solo, e lo legge chi
         * colloca. */
        p->nel_flusso = 1;
        p->aria_giu   = 3;

        if (k >= 0 && g_imm[k].px) {
            p->w   = (int)g_imm[k].w;
            p->h   = (int)g_imm[k].h;
            p->rif = EST_IMM(k);
            return VISTA_PEZZO;
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
                p->w   = (int)rw;
                p->h   = (int)rh;
                p->rif = EST_IMM(k);
                return VISTA_PEZZO;
            }
        }

        /* ! FINCHE' L'IMMAGINE NON C'E' SI LEGGE IL SUO `alt`, ed e'
         * esattamente il motivo per cui quell'attributo esiste. Il valore
         * sta gia' nell'arena del documento, quindi si impagina con le
         * stesse parole di tutto il resto. */
        alt = html_attr(&g_doc, v, "alt");
        if (alt && alt[0]) {
            p->testo     = alt;
            p->testo_off = (unsigned int)(alt - g_doc.arena);
            return VISTA_TESTO;
        }
        return VISTA_SALTA;
    }

    return VISTA_NIENTE;
}

/* =============================================================================
 * E ADESSO DISEGNATI
 *
 * ! LE COORDINATE ARRIVANO GIA' BUONE: `y` porta dentro lo scorrimento, e w e
 * h sono quelle che questo file aveva chiesto. Non c'e' un solo numero da
 * ricalcolare — l'impaginato ha tenuto il conto, come per ogni altra parola.
 * ============================================================================= */
static void est_disegna(int rif, int x, int y, int w, int h)
{
    /* =================================================================
     * UN CONTROLLO DI MODULO
     *
     * ! LA FORMA LA FA IL RILIEVO, non un bordo disegnato: `ex_incavo`
     * per cio' in cui si scrive, `ex_rilievo` per cio' che si preme. Sono
     * le stesse due funzioni con cui il toolkit disegna i propri
     * controlli, ed e' il motivo per cui una pagina web dentro EX-OS
     * sembra fatta della stessa materia del resto del sistema.
     * ================================================================= */
    if (EST_E_CTRL(rif)) {
        int   qua = EST_CHI(rif);
        Ctrl *c   = &g_ctrl[qua];
        int   cx  = x, cw = w, ch = h;
        char  mostra[CTRL_VAL_MAX];
        int   k;

        /* ! UN CONTROLLO E' TUTTO O NIENTE, e il ritaglio sta qui perche' qui
         * si sa che pezzo e': `ex_scrivi` taglia alla FINESTRA, non all'area
         * del documento, e una casella disegnata a meta' finirebbe sopra la
         * barra dell'indirizzo. Un'immagine invece sporge quanto vuole: il suo
         * ritaglio se lo fa a mano, qui sotto. */
        if (y < area_y() || y + ch > area_y() + area_h()) return;

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
                if (qua == g_ctrl_fuoco) {
                    int cu = c->cur;

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

            if (qua == g_ctrl_fuoco && riga == 0)
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
            if (qua == g_ctrl_fuoco) {
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
                int         q = c->cur, j;
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
        return;
    }

    /* ! UN'IMMAGINE SI RITAGLIA A MANO, e non e' pignoleria: ex_pixmap
     * ritaglia alla FINESTRA, non all'area del documento, quindi
     * un'immagine alta trecento pixel scorsa in su dipingerebbe sopra la
     * casella dell'indirizzo. Il testo se la cava perche' e' alto venti
     * punti e sborda di poco; un'immagine no. */
    if (EST_E_IMM(rif)) {
        const Imm *im    = &g_imm[EST_CHI(rif)];
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
            int rw = w;

            alta = h;
            if (cima < area_y()) {
                salta = area_y() - cima;
                cima  = area_y();
                alta -= salta;
            }
            if (cima + alta > area_y() + area_h())
                alta = area_y() + area_h() - cima;

            if (rw > 0 && alta > 0) {
                ex_riempi(g_f, x, cima, rw, alta, EX_GRIGIO);

                /* Il bordo si incide solo quando il riquadro c'e' tutto:
                 * un incavo tagliato a meta' disegna una riga di luce in
                 * mezzo al testo, e si legge come un difetto. */
                if (salta == 0 && alta == h)
                    ex_incavo(g_f, x, cima, rw, alta);
            }
            return;
        }

        if (cima < area_y()) {
            salta = area_y() - cima;
            cima  = area_y();
            alta -= salta;
        }
        if (cima + alta > area_y() + area_h())
            alta = area_y() + area_h() - cima;

        if (alta > 0)
            ex_pixmap(g_f, x, cima, (int)im->w, alta,
                      im->px + (unsigned int)salta * im->w, im->w);
        return;
    }
}

const VistaCliente g_estranei = { est_azzera, est_misura, est_disegna };

/* =============================================================================
 * tools/prove/bisprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il banco della dispensa dei biscotti.
 *
 * ! LE REGOLE DEI BISCOTTI SI SBAGLIANO IN SILENZIO, ed e' il motivo per cui
 * questo file esiste. Un suffisso di dominio confrontato a lettere invece che
 * a etichette manda i biscotti di `ex.os` a `notex.os`; un percorso confrontato
 * a prefisso manda quelli di `/conto` a `/contoltre`. Nessuno dei due si vede
 * navigando: si vede quando qualcuno lo sfrutta.
 *
 * ! E NON SERVE NE' UNA RETE NE' UN OROLOGIO. L'ora si passa come numero — la
 * stessa regola di exjs e di exdom — quindi una prova sulle scadenze si scrive
 * scegliendo che ore sono, invece di aspettare.
 *
 *     cc -Wall -Wextra -O2 -o /tmp/bisprova tools/prove/bisprova.c \
 *        exwin/bin/browser/biscotti.c -I exwin/bin/browser
 * ============================================================================= */

#include <stdio.h>
#include <string.h>
#include "biscotti.h"

static int fatte = 0, sbagliate = 0;

static void ok(const char *nome, int cond, const char *dettaglio)
{
    fatte++;
    printf("%s   %-44s %s\n", cond ? "ok" : "NO", nome, dettaglio ? dettaglio : "");
    if (!cond) sbagliate++;
}

static void uguale(const char *nome, const char *avuto, const char *atteso)
{
    fatte++;
    if (strcmp(avuto, atteso) == 0) {
        printf("ok   %-44s %s\n", nome, avuto[0] ? avuto : "(vuoto)");
    } else {
        printf("NO   %-44s atteso \"%s\", trovato \"%s\"\n", nome, atteso, avuto);
        sbagliate++;
    }
}

/* Un momento qualunque ma fissato: 1 gennaio 2026, 00:00:00 GMT. Le prove
 * sulle scadenze si scrivono intorno a questo. */
#define ORA  1767225600u

static const char *manda(Dispensa *d, const char *host, const char *perc,
                         int cifrata, int per_script)
{
    static char b[1024];

    bis_da_mandare(d, host, perc, cifrata, per_script, ORA, b, sizeof(b));
    return b;
}

int main(void)
{
    Dispensa d;

    printf("\n=== le date di scadenza ===\n");
    /* Il valore atteso e' quello dell'epoca Unix, contato a mano una volta e
     * scritto qui: se cambia, e' cambiato l'algoritmo. */
    ok("RFC 1123", bis_data("Wed, 09 Jun 2021 10:18:14 GMT") == 1623233894u, "");
    ok("RFC 850, anno a due cifre",
       bis_data("Wednesday, 09-Jun-21 10:18:14 GMT") == 1623233894u, "");
    ok("mezzanotte del 2026",  bis_data("Thu, 01 Jan 2026 00:00:00 GMT") == ORA, "");
    /* ! IL 1970 E' IL MODO IN CUI MEZZO WEB CANCELLA UN BISCOTTO, e deve
     * leggersi: renderebbe 0 solo se non si capisse la data. */
    ok("l'epoca si legge e non e' «non capita»",
       bis_data("Thu, 01 Jan 1970 00:00:01 GMT") == 1u, "");
    ok("un anno bisestile",
       bis_data("Mon, 29 Feb 2016 00:00:00 GMT") == 1456704000u, "");
    ok("una data illeggibile da' zero", bis_data("domani mattina") == 0, "");
    ok("e una vuota pure", bis_data("") == 0, "");

    printf("\n=== mettere e rimandare ===\n");
    bis_azzera(&d);
    bis_arrivato(&d, "ex.os", "/", "a=1", ORA);
    uguale("il piu' semplice torna indietro", manda(&d, "ex.os", "/", 0, 0), "a=1");
    bis_arrivato(&d, "ex.os", "/", "b=2", ORA);
    uguale("due si separano con punto e virgola",
           manda(&d, "ex.os", "/", 0, 0), "a=1; b=2");
    /* ! LO STESSO NOME SOSTITUISCE, non si affianca: due biscotti con lo
     * stesso nome sullo stesso dominio non esistono. */
    bis_arrivato(&d, "ex.os", "/", "a=3", ORA);
    uguale("lo stesso nome sostituisce", manda(&d, "ex.os", "/", 0, 0), "a=3; b=2");
    ok("un valore vuoto e' legale",
       bis_arrivato(&d, "ex.os", "/", "c=", ORA) == 1, "");
    ok("una riga senza uguale non e' un biscotto",
       bis_arrivato(&d, "ex.os", "/", "spazzatura", ORA) == 0, "");

    printf("\n=== i domini ===\n");
    bis_azzera(&d);
    bis_arrivato(&d, "www.ex.os", "/", "solo=host", ORA);
    uguale("senza Domain vale solo per quell'host",
           manda(&d, "www.ex.os", "/", 0, 0), "solo=host");
    uguale("e non per il dominio sopra", manda(&d, "ex.os", "/", 0, 0), "");
    uguale("ne' per un fratello", manda(&d, "img.ex.os", "/", 0, 0), "");

    bis_azzera(&d);
    bis_arrivato(&d, "www.ex.os", "/", "largo=si; Domain=.ex.os", ORA);
    uguale("con Domain vale per i sottodomini",
           manda(&d, "img.ex.os", "/", 0, 0), "largo=si");
    uguale("e per il dominio stesso", manda(&d, "ex.os", "/", 0, 0), "largo=si");
    /* ! IL CONFRONTO E' A ETICHETTE, NON A LETTERE: `notex.os` finisce per
     * `ex.os` e non c'entra niente. E' il difetto che questo banco esiste per
     * non far nascere. */
    uguale("ma non per chi ci somiglia soltanto",
           manda(&d, "notex.os", "/", 0, 0), "");

    /* ! UN SITO NON PUO' METTERE BISCOTTI PER UN ALTRO. Senza questo controllo
     * una pagina qualunque scriverebbe «Domain=google.com» e ce lo farebbe
     * mandare la volta dopo. */
    bis_azzera(&d);
    ok("un dominio che non e' il proprio si rifiuta",
       bis_arrivato(&d, "cattivo.os", "/", "x=1; Domain=ex.os", ORA) == 0, "");
    uguale("e infatti non arriva a nessuno", manda(&d, "ex.os", "/", 0, 0), "");

    printf("\n=== i percorsi ===\n");
    bis_azzera(&d);
    bis_arrivato(&d, "ex.os", "/", "r=radice; Path=/", ORA);
    bis_arrivato(&d, "ex.os", "/", "c=conto; Path=/conto", ORA);
    uguale("nella radice c'e' solo quello della radice",
           manda(&d, "ex.os", "/", 0, 0), "r=radice");
    uguale("dentro il percorso ci sono tutt'e due",
           manda(&d, "ex.os", "/conto", 0, 0), "r=radice; c=conto");
    uguale("e anche piu' sotto",
           manda(&d, "ex.os", "/conto/estratto", 0, 0), "r=radice; c=conto");
    /* ! «/conto» NON COPRE «/contoltre», e chi si ferma al prefisso di lettere
     * sbaglia proprio li'. */
    uguale("ma non in un percorso che ci somiglia",
           manda(&d, "ex.os", "/contoltre", 0, 0), "r=radice");
    /* ! SENZA Path IL PREDEFINITO E' LA DIRECTORY, non la radice. */
    bis_azzera(&d);
    bis_arrivato(&d, "ex.os", "/conto/entra.html", "s=1", ORA);
    uguale("senza Path vale per la directory",
           manda(&d, "ex.os", "/conto/esci.html", 0, 0), "s=1");
    uguale("e non per tutto il sito", manda(&d, "ex.os", "/", 0, 0), "");
    /* ! IL PREDEFINITO DI «/conto» E' «/», NON «/conto», e sembra il
     * contrario: l'ultima barra di «/conto» e' quella iniziale, e la directory
     * di un file che sta nella radice E' la radice. Chi lo legge di fretta
     * scrive una prova sbagliata — e' successo scrivendo queste. */
    bis_azzera(&d);
    bis_arrivato(&d, "ex.os", "/conto", "p=1", ORA);
    uguale("la directory di /conto e' la radice",
           manda(&d, "ex.os", "/altro", 0, 0), "p=1");

    /* La query non fa parte del percorso. */
    bis_azzera(&d);
    bis_arrivato(&d, "ex.os", "/", "q=1; Path=/cerca", ORA);
    uguale("la query non conta nel confronto",
           manda(&d, "ex.os", "/cerca?testo=ciao", 0, 0), "q=1");

    printf("\n=== Secure e HttpOnly ===\n");
    bis_azzera(&d);
    bis_arrivato(&d, "ex.os", "/", "aperto=1", ORA);
    bis_arrivato(&d, "ex.os", "/", "chiuso=2; Secure", ORA);
    uguale("in chiaro il Secure non parte",
           manda(&d, "ex.os", "/", 0, 0), "aperto=1");
    uguale("su https partono tutt'e due",
           manda(&d, "ex.os", "/", 1, 0), "aperto=1; chiuso=2");

    bis_azzera(&d);
    bis_arrivato(&d, "ex.os", "/", "visibile=1", ORA);
    bis_arrivato(&d, "ex.os", "/", "nascosto=2; HttpOnly", ORA);
    uguale("al server vanno tutt'e due",
           manda(&d, "ex.os", "/", 0, 0), "visibile=1; nascosto=2");
    /* ! E' TUTTO IL SENSO DI HttpOnly: un biscotto di sessione che uno script
     * non puo' leggere non si puo' nemmeno rubare con uno script. */
    uguale("agli script solo il primo",
           manda(&d, "ex.os", "/", 0, 1), "visibile=1");
    /* ! E UNO SCRIPT NON PUO' FABBRICARNE UNO INVISIBILE AGLI SCRIPT, o
     * `HttpOnly` sarebbe una parola che chiunque puo' scrivere. */
    bis_da_script(&d, "ex.os", "/", "furbo=3; HttpOnly", ORA);
    uguale("e non puo' fabbricarne uno HttpOnly",
           manda(&d, "ex.os", "/", 0, 1), "visibile=1; furbo=3");

    printf("\n=== le scadenze ===\n");
    bis_azzera(&d);
    bis_arrivato(&d, "ex.os", "/", "vivo=1; Max-Age=3600", ORA);
    uguale("Max-Age nel futuro vive", manda(&d, "ex.os", "/", 0, 0), "vivo=1");
    {
        char b[64];

        bis_da_mandare(&d, "ex.os", "/", 0, 0, ORA + 3601u, b, sizeof(b));
        uguale("un'ora dopo non parte piu'", b, "");
    }
    /* ! Max-Age=0 E' COME UN SITO CANCELLA UN BISCOTTO, ed e' il caso che si
     * incontra facendo «esci» da qualunque servizio. */
    bis_azzera(&d);
    bis_arrivato(&d, "ex.os", "/", "s=1", ORA);
    bis_arrivato(&d, "ex.os", "/", "s=1; Max-Age=0", ORA);
    uguale("Max-Age=0 lo toglie", manda(&d, "ex.os", "/", 0, 0), "");
    bis_azzera(&d);
    bis_arrivato(&d, "ex.os", "/", "s=1", ORA);
    bis_arrivato(&d, "ex.os", "/",
                 "s=1; Expires=Thu, 01 Jan 1970 00:00:01 GMT", ORA);
    uguale("una scadenza nel passato pure", manda(&d, "ex.os", "/", 0, 0), "");
    /* ! Max-Age VINCE SU Expires, e i server mandano tutt'e due proprio
     * contando su quella regola. */
    bis_azzera(&d);
    bis_arrivato(&d, "ex.os", "/",
                 "s=1; Expires=Thu, 01 Jan 1970 00:00:01 GMT; Max-Age=3600", ORA);
    uguale("Max-Age vince su Expires", manda(&d, "ex.os", "/", 0, 0), "s=1");

    printf("\n=== il file ===\n");
    {
        char     testo[4096];
        Dispensa e;

        bis_azzera(&d);
        bis_arrivato(&d, "ex.os", "/", "sessione=1", ORA);
        bis_arrivato(&d, "ex.os", "/conto",
                     "duraturo=2; Path=/conto; Max-Age=86400; Secure", ORA);
        bis_salva(&d, testo, sizeof(testo), ORA);

        /* ! I BISCOTTI DI SESSIONE NON VANNO SU DISCO: scriverli vorrebbe dire
         * che chiudere il navigatore non chiude piu' la sessione. */
        ok("quello di sessione non e' nel file",
           strstr(testo, "sessione") == 0, "");
        ok("quello duraturo si'", strstr(testo, "duraturo") != 0, "");

        bis_azzera(&e);
        bis_carica(&e, testo, ORA);
        uguale("e rileggendolo torna dov'era",
               manda(&e, "ex.os", "/conto", 1, 0), "duraturo=2");
        uguale("con le sue bandiere: in chiaro non parte",
               manda(&e, "ex.os", "/conto", 0, 0), "");
        /* ! E COL SUO PERCORSO: se il campo si perdesse nel giro sul file, un
         * biscotto di «/conto» tornerebbe valido per tutto il sito. */
        uguale("e col suo percorso", manda(&e, "ex.os", "/", 1, 0), "");

        /* ! UNO GIA' SCADUTO NON SI RICARICA: il file resta com'e' finche' non
         * lo si riscrive, e rileggerlo vorrebbe dire mandare al server una
         * cosa che lui ha gia' fatto morire. */
        bis_azzera(&e);
        bis_carica(&e, testo, ORA + 90000u);
        uguale("uno scaduto non si ricarica", manda(&e, "ex.os", "/conto", 1, 0), "");

        /* Un file con righe strane non deve far danni. */
        bis_azzera(&e);
        bis_carica(&e, "# solo un commento\n\n\nrigasenzacampi\n", ORA);
        uguale("un file malfatto non mette niente", manda(&e, "ex.os", "/", 0, 0), "");
    }

    printf("\n=== il valore puo' contenere di tutto ===\n");
    bis_azzera(&d);
    bis_arrivato(&d, "ex.os", "/", "t=abc def=ghi; Path=/", ORA);
    uguale("spazi e uguali dentro il valore",
           manda(&d, "ex.os", "/", 0, 0), "t=abc def=ghi");
    {
        char testo[2048];
        Dispensa e;

        bis_azzera(&d);
        bis_arrivato(&d, "ex.os", "/", "t=abc def=ghi; Max-Age=99999", ORA);
        bis_salva(&d, testo, sizeof(testo), ORA);
        bis_azzera(&e);
        bis_carica(&e, testo, ORA);
        uguale("e sopravvivono al giro sul file",
               manda(&e, "ex.os", "/", 0, 0), "t=abc def=ghi");
    }

    printf("\n=== la dispensa piena ===\n");
    {
        int i;
        char nome[64];

        bis_azzera(&d);
        for (i = 0; i < BIS_MAX + 8; i++) {
            sprintf(nome, "n%d=v; Max-Age=%d", i, 100 + i);
            bis_arrivato(&d, "ex.os", "/", nome, ORA);
        }
        ok("quando e' piena si dice", d.persi == 1, "");
        /* ! SI BUTTA IL PIU' VICINO A SCADERE, non il primo che capita: quello
         * appena arrivato e' il piu' probabile che serva. */
        {
            char b[2048];

            bis_da_mandare(&d, "ex.os", "/", 0, 0, ORA, b, sizeof(b));
            ok("e l'ultimo arrivato c'e' ancora",
               strstr(b, "n71=v") != 0, "");
            ok("mentre il primo e' uscito", strstr(b, "n0=v;") == 0, "");
        }
    }

    printf("\n=== un buffer piccolo non si riempie a meta' ===\n");
    {
        char b[12];

        bis_azzera(&d);
        bis_arrivato(&d, "ex.os", "/", "unnomelungo=unvalorelungo", ORA);
        bis_da_mandare(&d, "ex.os", "/", 0, 0, ORA, b, sizeof(b));
        /* ! MEGLIO NIENTE CHE UN «Cookie:» TAGLIATO IN MEZZO A UN VALORE: il
         * server lo legge, non lo riconosce, e risponde una cosa che non si
         * spiega. */
        uguale("meglio vuoto che tagliato", b, "");
    }

    printf("\n%d prove, %d sbagliate\n\n", fatte, sbagliate);
    return sbagliate ? 1 : 0;
}

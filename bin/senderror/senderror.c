/* =============================================================================
 * bin/senderror/senderror.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * senderror — manda un referto a chi puo' leggerlo
 *
 *     senderror <url> <file>            manda <file>
 *     senderror -n NOME <url> <file>    con un altro nome
 *     senderror -k CHIAVE <url> <file>  con un'altra chiave
 *
 * Esempio:
 *     senderror http://exagonx.altervista.org/exos/netinst/report/ /SIS900.TXT
 *
 * -----------------------------------------------------------------------------
 * ! ESISTE PERCHE' PORTARE VIA UN FILE DA QUI E' LA PARTE DIFFICILE
 *
 * Una macchina su cui si prova EX-OS e' quasi sempre una macchina che non ha
 * niente: niente chiavetta montata, niente stampante, spesso nemmeno uno
 * schermo che si possa fotografare per intero. `sis900.drv -debug` scrive un
 * referto di duecento righe, e fin qui e' facile; il problema e' farlo
 * arrivare a chi deve leggerlo. Finora la risposta era «copialo su una
 * chiavetta e portala di la'», che su una macchina in un'altra stanza vuol
 * dire un viaggio, e su una macchina di qualcun altro vuol dire niente.
 *
 * ! LA RETE C'E' GIA', ED E' L'UNICA COSA CHE ATTRAVERSA LE STANZE. Un POST e
 * venti righe di PHP dall'altra parte, e il referto e' dove serve.
 *
 * -----------------------------------------------------------------------------
 * ! IL CORPO SI CODIFICA QUI, E IL PERCHE' STA IN exhttp.h
 *
 * exhttp_posta() vuole il corpo GIA' codificato: «chi costruisce un modulo e'
 * l'unico a sapere come andava fatta la codifica». Giusto — e quindi la
 * codifica percentuale sta qui sotto, dove si sa che i campi sono tre.
 *
 * ! E COSTA FINO A TRE VOLTE. Ogni byte che non e' lettera o cifra diventa
 * %XX: un referto di 60 KB pieno di spazi e a capo puo' arrivare a 180 KB
 * codificati. Il tetto e' su quel che si LEGGE, non su quel che si manda,
 * perche' e' il primo dei due che chi lancia il comando puo' capire.
 * ============================================================================= */

#include "libc.h"
#include "exhttp.h"

/* +0.001 a ogni modifica: `senderror -version` la stampa. Vedi EX_VERSIONE. */
EX_VERSIONE("senderror", "0.001");

/* ! IL TETTO E' SUL FILE LETTO. Sessantaquattromila byte sono piu' di
 * qualunque referto che questo sistema sappia produrre — quello di sis900 ne
 * fa cinquemila — e lasciano spazio al triplo della codifica. */
#define FILE_MAX    (64u * 1024u)
#define CORPO_MAX   (FILE_MAX * 3u + 512u)
#define RISP_MAX    4096u

static char          g_file[FILE_MAX + 1];
static char          g_corpo[CORPO_MAX];
static unsigned char g_risp[RISP_MAX];

/* ! LA CHIAVE NON E' UNA DIFESA, E' UNA PORTA CHIUSA A CHIAVE SU UNA CASA DI
 * VETRO. Sta dentro un binario che si distribuisce, quindi chiunque voglia
 * leggerla la legge. Serve a una cosa sola e la fa bene: impedire che un
 * indirizzo trovato per caso — da uno scanner, da un motore di ricerca — si
 * trasformi in un posto dove chiunque scrive. Contro qualcuno che ce l'ha con
 * te non serve a niente, e dall'altra parte c'e' un tetto ai byte e uno ai
 * file proprio per quello. */
#define CHIAVE_PRED "exos"

static const char *g_chiave = CHIAVE_PRED;

/* --- la codifica percentuale ---------------------------------------------
 *
 * ! SI SALVANO SOLO I CARATTERI CHE NON POSSONO MAI DAR FASTIDIO. La regola
 * corta — lettere, cifre e quattro segni — e' piu' lunga da scrivere e piu'
 * facile da leggere di un elenco di caratteri da evitare, che invece si
 * dimentica sempre di qualcosa. Lo spazio diventa %20 e non '+': dall'altra
 * parte lo srotola PHP, e %20 vale in tutti e due i posti in cui puo' finire
 * un valore. */
static int innocuo(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == '.'               || c == '~';
}

static unsigned int codifica(char *dst, unsigned int spazio,
                             const char *src, unsigned int n)
{
    static const char esa[] = "0123456789ABCDEF";
    unsigned int i, w = 0;

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)src[i];

        if (innocuo((char)c)) {
            if (w + 1 >= spazio) break;
            dst[w++] = (char)c;
        } else {
            if (w + 3 >= spazio) break;
            dst[w++] = '%';
            dst[w++] = esa[(c >> 4) & 0x0F];
            dst[w++] = esa[c & 0x0F];
        }
    }
    dst[w] = '\0';
    return w;
}

/* Il nome senza il percorso: «/SIS900.TXT» -> «SIS900.TXT». Dall'altra parte
 * lo ripuliscono comunque, ma mandare una barra vuol dire far discutere il
 * server con una cosa che qui si sa gia' come togliere. */
static const char *solo_nome(const char *p)
{
    const char *u = p;

    while (*p) { if (*p == '/' || *p == '\\') u = p + 1; p++; }
    return u;
}

static void uso(void)
{
    printf("uso: senderror [-n NOME] [-k CHIAVE] <url> <file>\n\n");
    printf("  Manda <file> a <url> con una POST: dall'altra parte c'e'\n");
    printf("  uno script che lo salva e risponde dove l'ha messo.\n\n");
    printf("  senderror http://esempio.org/exos/netinst/report/ /SIS900.TXT\n\n");
    printf("  -n  il nome con cui salvarlo (predefinito: quello del file)\n");
    printf("  -k  la chiave che lo script si aspetta (predefinita: %s)\n\n",
           CHIAVE_PRED);
    printf("! IL FILE VIAGGIA IN CHIARO e finisce su un server che non e'\n");
    printf("  tuo. Un referto dell'hardware va benissimo; un file con\n");
    printf("  dentro qualcosa di tuo, no.\n\n");
    printf("Il tetto e' %u byte: oltre, si tronca e si dice.\n", FILE_MAX);
}

int main(int argc, char **argv)
{
    const char  *url = NULL, *perc = NULL, *nome = NULL;
    int          i, fd, troncato = 0;
    long         letti;
    unsigned int n = 0;
    ExHttpEsito  e;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            uso(); return 0;
        }
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) { nome = argv[++i]; continue; }
        if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) { g_chiave = argv[++i]; continue; }
        if (argv[i][0] == '-') {
            printf("senderror: opzione '%s' sconosciuta.\n", argv[i]);
            return 1;
        }
        if (url == NULL)       url  = argv[i];
        else if (perc == NULL) perc = argv[i];
        else { printf("senderror: un file per volta.\n"); return 1; }
    }

    if (url == NULL || perc == NULL) { uso(); return 1; }
    if (nome == NULL) nome = solo_nome(perc);

    /* --- il file ---------------------------------------------------------- */
    fd = open(perc, O_RDONLY);
    if (fd < 0) {
        printf("senderror: non riesco ad aprire %s\n", perc);
        return 1;
    }
    letti = (long)read(fd, g_file, FILE_MAX);
    close(fd);

    if (letti < 0) { printf("senderror: %s non si legge.\n", perc); return 1; }
    if (letti == 0) {
        printf("senderror: %s e' vuoto: non mando niente.\n", perc);
        return 1;
    }
    n = (unsigned int)letti;
    g_file[n] = '\0';

    /* ! SI DICE CHE SI E' TRONCATO, E LO SI DICE PRIMA DI MANDARE. Un referto
     * troncato letto dall'altra parte senza saperlo fa cercare un guasto
     * dentro un silenzio che e' solo la fine del buffer. */
    if (n == FILE_MAX) {
        troncato = 1;
        printf("senderror: %s e' piu' lungo di %u byte: mando i primi.\n",
               perc, FILE_MAX);
    }

    /* --- il corpo ---------------------------------------------------------- */
    {
        unsigned int w = 0;
        int          k;

        k = snprintf(g_corpo, CORPO_MAX, "chiave=");
        w = (unsigned int)k;
        w += codifica(g_corpo + w, CORPO_MAX - w, g_chiave,
                      (unsigned int)strlen(g_chiave));

        k = snprintf(g_corpo + w, CORPO_MAX - w, "&nome=");
        w += (unsigned int)k;
        w += codifica(g_corpo + w, CORPO_MAX - w, nome,
                      (unsigned int)strlen(nome));

        k = snprintf(g_corpo + w, CORPO_MAX - w, "&troncato=%d&testo=",
                     troncato);
        w += (unsigned int)k;
        w += codifica(g_corpo + w, CORPO_MAX - w, g_file, n);
    }

    printf("senderror: mando %u byte di %s a %s\n", n, nome, url);

    if (!exhttp_posta(url, g_corpo, g_risp, RISP_MAX, &e)) {
        printf("senderror: %s\n", e.errore[0] ? e.errore : "non riuscito");
        return 1;
    }

    /* ! LA RISPOSTA SI STAMPA, ED E' META' DEL PUNTO. Lo script dice dove ha
     * messo il file: senza quella riga chi ha mandato il referto non sa se e'
     * arrivato, e la prima cosa che fa e' rimandarlo. */
    printf("senderror: %d\n", e.codice);
    if (e.byte > 0) {
        unsigned int q = e.byte;

        if (q > RISP_MAX - 1) q = RISP_MAX - 1;
        g_risp[q] = '\0';
        printf("%s\n", (char *)g_risp);
    }

    return (e.codice >= 200 && e.codice < 300) ? 0 : 1;
}

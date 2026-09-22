/* =============================================================================
 * bin/blkprova/blkprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * FIN DOVE UN DISCO RISPONDE
 *
 *     blkprova hd0p1              cerca il confine e campiona quel che c'e' oltre
 *     blkprova hd0p1 44550146     un settore, poi due, quattro ... fino a 64:
 *                                 dice se a quell'indirizzo e' la MISURA a
 *                                 non passare, invece del posto
 *     blkprova hd0p1 44550146 64  una lettura sola, della misura voluta
 *     blkprova hd0p1 -scorri 44000000 64
 *                                 legge 64 MB di fila a blocchi da 32 KB —
 *                                 cioe' come fa chkdsk — e dice dove si ferma
 *     blkprova hd0p1 -tutto       la partizione INTERA, a blocchi da 32 KB, e
 *                                 al primo intoppo dice DOPO QUANTE LETTURE
 *
 * ! ESISTE PERCHE' «chkdsk TROVA ERRORI» NON E' UNA DIAGNOSI. Un controllo del
 * filesystem legge dove gli serve, e quando il disco non risponde elenca gli
 * indirizzi che ha chiesto lui: da quell'elenco non si capisce se il disco sia
 * rotto DA UN CERTO PUNTO IN POI — e allora si ripartiziona sotto quel punto e
 * la macchina continua a vivere — oppure a chiazze, e allora il disco si
 * cambia. Sono due conclusioni opposte, e la differenza si misura in una
 * decina di letture fatte apposta.
 *
 * ! E LA RICERCA E' BINARIA, non una scansione. Un disco da 57 GB ha
 * centodiciassette milioni di settori: provarli tutti, o anche uno ogni
 * cinquanta megabyte, vuol dire ore — e ogni lettura che NON risponde costa il
 * timeout del driver, qualche secondo. Dimezzando, il confine si trova in
 * ventisette letture: un minuto, non un pomeriggio.
 *
 * ! UNA COSA CHE IL NUMERO DA SOLO NON DICE: un settore ILLEGGIBILE e un
 * disco che NON RISPONDE sono guasti diversi. Il primo risponde «errore» e la
 * testina sta bene; il secondo lascia il canale occupato (BSY), e nel registro
 * del driver si vede come `stato=0xd0`. Questo programma non li distingue —
 * per lui e' «la lettura non e' riuscita» — ma chi guarda il registro accanto
 * si': vedi @ATA-RESET in in_lavorazione.txt.
 *
 * ! SI LANCIA SU UNA PARTIZIONE NON MONTATA, e non e' una scelta di questo
 * programma: `blkread` rifiuta un disco intero (non e' una partizione) e
 * rifiuta una partizione montata, che ha una cache sopra. Sul disco di sistema
 * vuol dire avviare dal dischetto di soccorso o dal CD.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `blkprova -version` la stampa. */
EX_VERSIONE("blkprova", "0.005");

/* ! IL BUFFER E' QUELLO DI UNA LETTURA INTERA DA 64 SETTORI, che e' il massimo
 * che blkread accetta per chiamata (BLKIO_MAX_SETT) ed e' anche la misura che
 * usa chi legge sul serio. Tenerlo grosso serve a poter CHIEDERE come chiede
 * un filesystem: un guasto che si vede solo a 32 KB non si trova leggendo 512
 * byte per volta — ed e' esattamente cio' che e' successo sull'Acer, dove il
 * settore per settore passava dappertutto e chkdsk si piantava lo stesso. */
#define SETT_MAX    64
static unsigned char g_sett[SETT_MAX * 512];

/* Rende 1 se il settore si legge, 0 se il disco non risponde o rende errore,
 * e -1 se la richiesta e' FUORI dalla partizione — che non e' un guasto, e'
 * la fine del volume. */
static int leggi_n(const char *dev, unsigned int lba, unsigned int n)
{
    int r = blkread(dev, lba, n, g_sett);

    if (r == (int)n)     return 1;
    if (r == -EINVAL)    return -1;     /* oltre la fine: non e' un guasto */
    return 0;
}

static int leggi(const char *dev, unsigned int lba)
{
    int r = blkread(dev, lba, 1, g_sett);

    if (r == 1)          return 1;
    if (r == -EINVAL)    return -1;     /* oltre la fine: non e' un guasto */
    return 0;
}

static void in_misura(unsigned int lba, char *out)
{
    /* I settori sono da 512 byte: 2048 per megabyte. Si divide PRIMA di
     * moltiplicare, o a otto milioni di settori il conto trabocca. */
    unsigned int mb = lba / 2048u;

    if (mb >= 1024u) sprintf(out, "%u,%u GB", mb / 1024u, (mb % 1024u) * 10u / 1024u);
    else             sprintf(out, "%u MB", mb);
}

static void dimmi(const char *dev, unsigned int lba)
{
    char m[32];
    int  r = leggi(dev, lba);

    in_misura(lba, m);
    if (r == 1)  printf("  lba %-12u (%-8s)  risponde\n", lba, m);
    else if (r < 0) printf("  lba %-12u (%-8s)  fuori dalla partizione\n", lba, m);
    else printf("  lba %-12u (%-8s)  NON risponde\n", lba, m);
}

int main(int argc, char **argv)
{
    const char  *dev;
    unsigned int lo, hi, fine, i, falliti = 0, provati = 0;
    char         m1[32], m2[32];

    if (argc < 2) {
        printf("uso: blkprova <partizione> [lba [settori]]\n");
        printf("     blkprova <partizione> -scorri <lba> <mb>\n\n");
        printf("  blkprova hd0p1                    fin dove il disco risponde\n");
        printf("  blkprova hd0p1 44550146           a quell'indirizzo, che misura passa\n");
        printf("  blkprova hd0p1 44550146 64        una lettura sola, 64 settori\n");
        printf("  blkprova hd0p1 -scorri 44000000 64  64 MB di fila, come chkdsk\n");
        printf("  blkprova hd0p1 -tutto             la partizione intera: dopo\n");
        printf("                                    quante letture si pianta\n\n");
        printf("La partizione NON dev'essere montata: si avvia dal dischetto\n");
        printf("di soccorso o dal CD. I nomi si vedono con `disk`.\n");
        return 1;
    }
    dev = argv[1];

    /* --- una lettura sola, della misura chiesta --------------------------- */
    if (argc >= 4 && argv[2][0] != '-') {
        unsigned int lba = (unsigned int)strtoul(argv[2], 0, 10);
        unsigned int n   = (unsigned int)strtoul(argv[3], 0, 10);
        char         m[32];

        if (n < 1u || n > SETT_MAX) { printf("blkprova: da 1 a %u settori\n", SETT_MAX); return 1; }
        in_misura(lba, m);
        printf("  lba %u (%s), %u settori: %s\n", lba, m, n,
               leggi_n(dev, lba, n) == 1 ? "risponde" : "NON risponde");
        return 0;
    }

    /* --- come legge chi legge sul serio: di fila, a blocchi ----------------
     *
     * ! E' QUESTA LA PROVA CHE RIPRODUCE UN chkdsk, non le letture sparse. Un
     * controllo del filesystem scorre grandi tratti a blocchi da 32 KB, e se
     * il guasto sta nella MISURA o nel ritmo — non nel posto — si vede solo
     * cosi'. */
    if (argc >= 5 && strcmp(argv[2], "-scorri") == 0) {
        unsigned int da  = (unsigned int)strtoul(argv[3], 0, 10);
        unsigned int mb  = (unsigned int)strtoul(argv[4], 0, 10);
        unsigned int fatti = 0, rotti = 0, lba;
        unsigned int fino = da + mb * 2048u;
        char         m[32];

        in_misura(da, m);
        printf("blkprova - %s: %u MB da lba %u (%s), a blocchi da %u settori\n\n",
               dev, mb, da, m, SETT_MAX);

        for (lba = da; lba < fino; lba += SETT_MAX) {
            int r = leggi_n(dev, lba, SETT_MAX);

            if (r < 0) { printf("  la partizione finisce a lba %u\n", lba); break; }
            if (r == 0) {
                rotti++;
                if (rotti <= 10u) {
                    in_misura(lba, m);
                    printf("  NON risponde: lba %u (%s)\n", lba, m);
                }
            } else fatti++;
        }
        printf("\n  %u blocchi letti, %u falliti\n", fatti, rotti);
        if (rotti == 0)
            printf("  A questa misura, di fila, il disco regge.\n");
        return rotti ? 1 : 0;
    }

    /* --- tutta la partizione, contando le letture --------------------------
     *
     * ! IL NUMERO CHE SERVE E' «DOPO QUANTE», non «dove». Sull'Acer il posto e
     * la misura sono stati esclusi tutt'e due — a settori singoli risponde fino
     * in fondo, e sessantaquattro megabyte di fila a blocchi da 32 KB passano
     * senza un errore — mentre chkdsk, che di letture ne fa decine di migliaia,
     * si pianta. Se il guasto viene dal LAVORO ACCUMULATO, l'unico modo di
     * inchiodarlo e' fare quel lavoro contandolo.
     *
     * ! E ADESSO TIRA DRITTO, mentre prima si fermava al primo intoppo. Si
     * fermava quando un intoppo voleva dire «il canale e' morto»: andare avanti
     * non avrebbe detto niente di nuovo. Da quando il driver distingue un
     * settore rifiutato dal disco da un canale fermo (vedi @ATA-RESET), un
     * intoppo e' un SETTORE: e allora quello che serve e' la mappa — quanti
     * sono, dove, e se stanno tutti insieme. Il numero della prima volta si
     * stampa lo stesso, perche' e' quello che dice se il guaio viene dal
     * lavoro accumulato. */
    if (argc >= 3 && strcmp(argv[2], "-tutto") == 0) {
        unsigned int lba = 0, letti = 0, tappa = 0, rotti = 0;
        char         m[32];
        /* ! IL TEMPO E' UNA MISURA, NON UN ABBELLIMENTO. Quanto ci mette a
         * leggere dice se sta lavorando in DMA o in PIO, e su un disco che se
         * ne va dice di piu': un supporto che comincia a cedere RALLENTA prima
         * di dare errori, perche' il disco ritenta al suo interno e non lo
         * racconta a nessuno. Un numero che cala fra una scansione e l'altra
         * e' un avviso che nessun errore avrebbe dato. */
        time_t       t0 = time(0), dt;

        printf("blkprova - %s, tutta la partizione a blocchi da %u settori.\n",
               dev, SETT_MAX);
        printf("Ogni punto e' mezzo gigabyte.\n\n");

        for (;;) {
            int r = leggi_n(dev, lba, SETT_MAX);

            if (r < 0) {
                in_misura(lba, m);
                dt = time(0) - t0;
                if (dt < 1) dt = 1;
                printf("\n\nFINITA: %s, %u blocchi letti, %u NON letti.\n",
                       m, letti, rotti);
                printf("        %u secondi, %u KB al secondo.\n",
                       (unsigned int)dt,
                       (unsigned int)((letti * 32u) / (unsigned int)dt));
                if (rotti == 0u) {
                    printf("Nessun intoppo: il disco regge una lettura intera,\n");
                    printf("di fila, a blocchi da 32 KB.\n");
                } else {
                    printf("\nI blocchi che non rispondono sono %u su %u, cioe'\n",
                           rotti, rotti + letti);
                    printf("%u ogni diecimila. Un disco con dei settori andati si\n",
                           rotti * 10000u / (rotti + letti));
                    printf("copia via finche' si legge: riparare la mappa di un\n");
                    printf("filesystem sopra dei buchi non li fa tornare.\n");
                }
                return rotti ? 1 : 0;
            }
            if (r == 0) {
                rotti++;
                if (rotti == 1u) {
                    in_misura(lba, m);
                    printf("\n\nprimo intoppo a lba %u (%s), dopo %u letture\n",
                           lba, m, letti);
                }
                if (rotti <= 20u) {
                    in_misura(lba, m);
                    printf("  non risponde: lba %u (%s)\n", lba, m);
                }
                if (rotti == 21u) printf("  (gli altri non li elenco)\n");
                lba += SETT_MAX;
                continue;
            }
            letti++;
            lba += SETT_MAX;

            /* Un punto ogni mezzo giga: chi guarda deve vedere che e' vivo. */
            if (lba / (1024u * 1024u) > tappa) {
                tappa = lba / (1024u * 1024u);
                printf(".");
            }
        }
    }

    /* --- un indirizzo solo: la misura che passa e quella che no ------------ */
    if (argc >= 3) {
        unsigned int lba = (unsigned int)strtoul(argv[2], 0, 10);
        unsigned int n;
        char         m[32];

        in_misura(lba, m);
        printf("blkprova - %s, lba %u (%s)\n\n", dev, lba, m);
        printf("  la stessa lettura, a misure diverse:\n");
        for (n = 1u; n <= SETT_MAX; n *= 2u) {
            int r = leggi_n(dev, lba, n);

            printf("    %3u settori (%5u byte):  %s\n", n, n * 512u,
                   r == 1 ? "risponde" : (r < 0 ? "fuori dalla partizione"
                                                : "NON risponde"));
        }
        printf("\n  ! SE IL SETTORE SINGOLO PASSA E IL BLOCCO NO, il posto non\n");
        printf("    c'entra: non passa la MISURA, e allora il guasto e' nostro,\n");
        printf("    non del disco.\n");
        return 0;
    }

    /* --- il primo settore: se non c'e' quello, non c'e' niente da cercare -- */
    if (leggi(dev, 0) != 1) {
        printf("blkprova: %s non risponde nemmeno al settore 0.\n", dev);
        printf("          Se e' montata, blkread rifiuta: smontala o avvia\n");
        printf("          da un altro supporto.\n");
        return 1;
    }
    printf("blkprova - %s\n\n", dev);
    printf("  il settore 0 risponde: cerco fin dove\n");

    /* --- la fine della partizione, a raddoppi -----------------------------
     *
     * ! NON SI CHIEDE A NESSUNO QUANTO E' GRANDE, si scopre: blkread rende
     * -EINVAL fuori dalla finestra, ed e' una risposta immediata che non costa
     * un timeout. Raddoppiando si arriva in poco piu' di venti letture. */
    hi = 1;
    while (leggi(dev, hi) >= 0 && hi < 0x40000000u) hi *= 2u;
    fine = hi;                      /* qui siamo o fuori, o sul guasto */

    /* --- il confine, dimezzando ------------------------------------------- */
    lo = 0;                         /* 0 risponde di sicuro */
    while (hi - lo > 1u) {
        unsigned int mezzo = lo + (hi - lo) / 2u;

        if (leggi(dev, mezzo) == 1) lo = mezzo;
        else                        hi = mezzo;
    }

    in_misura(lo, m1);
    in_misura(hi, m2);

    /* --- che cosa c'e' oltre ----------------------------------------------
     *
     * ! LA DOMANDA VERA E' SE IL CONFINE SIA UN CONFINE. Dieci campioni sparsi
     * oltre: se non risponde nessuno, il disco finisce li' davvero e sotto quel
     * punto ci si puo' ripartizionare; se qualcuno risponde, il disco e' a
     * chiazze — e un disco a chiazze non si ripara, si copia via. */
    printf("\n  ultimo settore che risponde:  lba %u  (%s)\n", lo, m1);
    printf("  primo che non risponde:       lba %u  (%s)\n\n", hi, m2);

    if (leggi(dev, hi) < 0) {
        printf("  E NON E' UN GUASTO: oltre quel settore la partizione\n");
        printf("  finisce. Il disco risponde per intero.\n");
        return 0;
    }

    printf("  dieci campioni oltre il confine:\n");
    for (i = 1; i <= 10u; i++) {
        unsigned int p = hi + (fine - hi) / 11u * i;
        int          r;

        if (p <= hi) p = hi + i;
        r = leggi(dev, p);
        if (r >= 0) {
            provati++;
            if (r == 0) falliti++;
            dimmi(dev, p);
        }
    }

    printf("\n  verdetto:\n");
    if (provati == 0) {
        printf("    oltre il confine c'e' solo la fine della partizione.\n");
    } else if (falliti == provati) {
        printf("    IL DISCO RISPONDE FINO A %s E POI PIU' NIENTE.\n", m1);
        printf("    Una partizione che finisca prima di quel punto e' usabile:\n");
        printf("    i dati sotto si leggono, quelli sopra non ci sono mai stati.\n");
    } else {
        printf("    A CHIAZZE: oltre il confine %u campioni su %u rispondono\n",
               provati - falliti, provati);
        printf("    lo stesso. Un disco cosi' non si ripara ripartizionando:\n");
        printf("    si copia via quel che serve, finche' si legge.\n");
    }
    return 0;
}

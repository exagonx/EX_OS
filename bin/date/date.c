/* =============================================================================
 * bin/date/date.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Che giorno e' e che ore sono — e, da root, rimetterli.
 *
 *     date                          giorno, data e ora per esteso
 *     date -d                       solo la data, 2026-09-16
 *     date -t                       solo l'ora, 17:52:31
 *     date -set-date:2026/09/16     rimette la data   (serve root)
 *     date -set-time:17:52:30       rimette l'ora     (serve root)
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' UN BINARIO SOLO E NON `date` PIU' `time`
 *
 * Su MS-DOS erano due comandi, e ognuno chiedeva la sua meta'. Qui sono uno,
 * per due ragioni che non c'entrano con i gusti.
 *
 * La prima e' il floppy: il sistema di base ci sta in 1.44 MB, e quando questo
 * comando e' stato scritto ne restavano liberi 63488 byte. Il piu' piccolo
 * programma di EX-OS pesa 13,5 KB — due binari invece di uno vorrebbero dire
 * spendere quasi meta' di quello spazio per stampare due meta' della stessa
 * lettura del CMOS.
 *
 * La seconda e' che `time` VUOL DIRE UN'ALTRA COSA, e non da oggi: su qualunque
 * sistema Unix `time comando` MISURA quanto ci mette un comando a girare. Se
 * `time` diventasse «stampa l'ora», il giorno in cui la shell vorra' il `time`
 * vero il nome sarebbe gia' occupato da qualcos'altro — e a quel punto o si
 * rompe chi lo usa, o si sceglie un nome peggiore.
 *
 * -----------------------------------------------------------------------------
 * ! L'ORA SI SCRIVE, DA OGGI — E PRIMA NO
 *
 * Fino al kernel 0.218 l'orologio di EX-OS si LEGGEVA e basta: non esisteva
 * nessuna syscall per rimetterlo. Non era una dimenticanza teorica, si vedeva
 * sulla macchina vera: l'Acer Aspire 3000 segnava il 2005, e ogni file che
 * caricava sul server arrivava datato «Feb 9 2005». L'unico modo di aggiustarlo
 * era entrare nel BIOS.
 *
 * ! E LE DUE META' SI RIMETTONO SEPARATE, non insieme. Chi rimette solo l'ora
 * (il caso normale: l'ora legale, o un orologio che ha derivato di due minuti)
 * non deve ribattere anche la data, e soprattutto non deve RISCHIARE di
 * sbagliarla. `-set-time:` legge la data dall'orologio e riscrive solo l'ora;
 * `-set-date:` fa il contrario.
 *
 * ! LA LETTURA PRIMA DELLA SCRITTURA NON E' UNA FORMALITA'. time_set() vuole
 * una RtcTime INTERA — il chip non ha un modo di scrivere «solo i minuti» —
 * quindi i campi che non si cambiano vanno riletti. Se l'orologio non risponde
 * affatto, questo e' anche il punto in cui ce ne accorgiamo PRIMA di aver
 * scritto qualcosa.
 *
 * ! E I SECONDI, IN `-set-date:`, SONO UN RISCHIO NOTO. Fra la lettura e la
 * scrittura passa qualche microsecondo, ma se la lettura cade su 12:59:59 e il
 * chip scatta subito dopo, la scrittura rimette 12:59:59 — cioe' l'orologio
 * torna indietro di un secondo. E' l'unico errore possibile, vale un secondo, e
 * costa meno di qualunque rimedio: l'alternativa sarebbe leggere due volte e
 * confrontare, che e' la difesa giusta per rtc_read e sproporzionata qui.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `date -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("date", "0.001");

/* ! I NOMI SONO IN ASCII PURO, accenti compresi: la console di EX-OS e' code
 * page 437 e un carattere UTF-8 ci arriva come due segni sbagliati. Si scrive
 * "mercoledi'" con l'apostrofo, come in tutto il resto del sistema. */
static const char *g_giorni[7] = {
    "domenica", "lunedi'", "martedi'", "mercoledi'",
    "giovedi'", "venerdi'", "sabato"
};

static const char *g_mesi[12] = {
    "gennaio", "febbraio", "marzo", "aprile", "maggio", "giugno",
    "luglio", "agosto", "settembre", "ottobre", "novembre", "dicembre"
};

static void aiuto(void)
{
    printf("uso: date [opzione]\n\n");
    printf("Senza opzioni: il giorno della settimana, la data e l'ora.\n\n");
    printf("Leggere:\n");
    printf("  -d                    solo la data, come 2026-09-16\n");
    printf("  -t                    solo l'ora, come 17:52:31\n\n");
    printf("Rimettere l'orologio (serve essere root):\n");
    printf("  -set-date:AAAA/MM/GG  la data  (va bene anche 2026-09-16)\n");
    printf("  -set-time:HH:MM:SS    l'ora    (i secondi si possono omettere)\n\n");
    printf("  -h                    questo aiuto (anche -help e --help)\n\n");
    printf("Le due meta' si rimettono separate: chi cambia l'ora non deve\n");
    printf("ribattere la data, e viceversa.\n\n");
    printf("! L'anno sta fra 1980 e 2099. L'orologio conserva DUE cifre\n");
    printf("  d'anno, e il secolo lo mette il sistema: un 2150 tornerebbe\n");
    printf("  indietro come 2050, e un errore silenzioso e' peggio di un no.\n\n");
    printf("! Non c'e' nessun fuso orario. L'orologio della macchina e' ora\n");
    printf("  locale, e il sistema non sa dove si trova.\n\n");
    printf("Esempi:\n");
    printf("  date                        che ore sono\n");
    printf("  date -set-date:2026/09/16   ci si rimette al giorno giusto\n");
    printf("  date -set-time:17:52        e all'ora giusta, secondi a zero\n");
}

/* Legge un numero decimale di esattamente `cifre` cifre. Torna 0 se va bene e
 * lascia `p` sul carattere dopo.
 *
 * ! ESATTAMENTE, non «al massimo»: con un conteggio libero "2026/9/1" e
 * "20269/1" si leggerebbero allo stesso modo, e la seconda e' un errore di
 * battitura che deve essere rifiutato, non interpretato. */
static int numero(const char **p, int cifre, unsigned int *out)
{
    const char  *s = *p;
    unsigned int v = 0;
    int          i;

    for (i = 0; i < cifre; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10u + (unsigned int)(s[i] - '0');
    }
    *p = s + cifre;
    *out = v;
    return 0;
}

/* "AAAA/MM/GG" oppure "AAAA-MM-GG". Il separatore si accetta di tutt'e due le
 * forme perche' e' l'unica differenza fra come si scrive una data qui e come la
 * stampa `ls`: farla sbagliare per un trattino sarebbe una trappola gratuita. */
static int leggi_data(const char *s, RtcTime *t)
{
    const char *p = s;

    if (numero(&p, 4, &t->anno) != 0) return -1;
    if (*p != '/' && *p != '-') return -1;
    p++;
    if (numero(&p, 2, &t->mese) != 0) return -1;
    if (*p != '/' && *p != '-') return -1;
    p++;
    if (numero(&p, 2, &t->giorno) != 0) return -1;
    return (*p == '\0') ? 0 : -1;
}

/* "HH:MM:SS", oppure "HH:MM" e i secondi vanno a zero. */
static int leggi_ora(const char *s, RtcTime *t)
{
    const char *p = s;

    if (numero(&p, 2, &t->ora) != 0) return -1;
    if (*p != ':') return -1;
    p++;
    if (numero(&p, 2, &t->minuto) != 0) return -1;
    if (*p == '\0') { t->secondo = 0; return 0; }
    if (*p != ':') return -1;
    p++;
    if (numero(&p, 2, &t->secondo) != 0) return -1;
    return (*p == '\0') ? 0 : -1;
}

/* Il giorno della settimana. Lo darebbe anche localtime(), ma per averlo da li'
 * bisognerebbe passare da time() — cioe' da una SECONDA lettura dell'orologio,
 * che puo' cadere dall'altra parte di uno scatto e contraddire la prima. Con i
 * campi gia' in mano il conto e' questo, ed e' esatto dal 1583 in poi. */
static int giorno_settimana(unsigned int anno, unsigned int mese, unsigned int giorno)
{
    static const int scarto[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    unsigned int a = anno;

    if (mese < 3) a--;
    return (int)((a + a / 4u - a / 100u + a / 400u + (unsigned int)scarto[mese - 1]
                  + giorno) % 7u);
}

/* Quanti giorni ha il mese. La stessa tabella sta in kernel/arch/x86/rtc.c, e
 * la doppia copia e' voluta: il kernel deve rifiutare una data impossibile da
 * CHIUNQUE gliela mandi, e questo programma deve poter dire QUAL E' il campo
 * sbagliato — cosa che un -EINVAL solo non dice. */
static unsigned int giorni_del_mese(unsigned int mese, unsigned int anno)
{
    static const unsigned int g[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };

    if (mese < 1 || mese > 12) return 0;
    if (mese == 2 && ((anno % 4 == 0 && anno % 100 != 0) || anno % 400 == 0))
        return 29;
    return g[mese - 1];
}

/* =============================================================================
 * ! IL CONTROLLO SI FA DUE VOLTE, QUI E NEL KERNEL, E NON E' UNO SPRECO
 *
 * Il kernel DEVE rifiutare il 31 di febbraio comunque, perche' non e' questo
 * programma l'unico che potra' chiamare time_set(). Ma il kernel ha un solo
 * modo di dire di no — -EINVAL — e chi lo riceve non sa se ha sbagliato l'anno,
 * il mese o l'ora. Il controllo qui esiste per IL MESSAGGIO: dice quale campo
 * non va e perche', che e' l'unica cosa che serve a chi ha appena battuto il
 * comando.
 *
 * Torna 0 se va bene, e altrimenti ha gia' stampato.
 * ============================================================================= */
static int valida(const RtcTime *t)
{
    if (t->anno < 1980 || t->anno > 2099) {
        printf("date: l'anno %u non si puo' scrivere.\n", t->anno);
        printf("      L'orologio conserva DUE cifre d'anno e il secolo lo\n");
        printf("      mette il sistema: sta fra il 1980 e il 2099.\n");
        return -1;
    }
    if (t->mese < 1 || t->mese > 12) {
        printf("date: non esiste il mese %u. Sta fra 01 e 12.\n", t->mese);
        return -1;
    }
    if (t->giorno < 1 || t->giorno > giorni_del_mese(t->mese, t->anno)) {
        printf("date: %u non e' un giorno di quel mese: ne ha %u.\n",
               t->giorno, giorni_del_mese(t->mese, t->anno));
        return -1;
    }
    if (t->ora > 23) {
        printf("date: non esistono le %u. L'ora sta fra 00 e 23: l'orologio\n",
               t->ora);
        printf("      si scrive su 24 ore, non su 12 con AM e PM.\n");
        return -1;
    }
    if (t->minuto > 59) {
        printf("date: non esiste il minuto %u. Sta fra 00 e 59.\n", t->minuto);
        return -1;
    }
    if (t->secondo > 59) {
        printf("date: non esiste il secondo %u. Sta fra 00 e 59.\n", t->secondo);
        return -1;
    }
    return 0;
}

/* Il motivo per cui l'orologio non ha risposto, detto in modo utile. */
static void spiega(int err, const char *cosa)
{
    if (err == -EPERM)
        printf("date: %s: serve essere root.\n"
               "      L'ora di sistema non e' un'impostazione personale: la\n"
               "      data di ogni file scritto da chiunque dipende da lei.\n"
               "      Prova con `sudo date ...`.\n", cosa);
    else if (err == -EINVAL)
        printf("date: %s: il kernel dice che quella data non esiste.\n"
               "      ! E qui non ci si doveva arrivare: il controllo di\n"
               "        valida() avrebbe dovuto fermarla prima. Se lo leggi,\n"
               "        i due controlli non dicono piu' la stessa cosa.\n", cosa);
    else if (err == -ENODEV)
        printf("date: %s: l'orologio non risponde.\n"
               "      Su una macchina vecchia vuol dire quasi sempre la\n"
               "      batteria del CMOS scarica.\n", cosa);
    else
        printf("date: %s: errore %d (%s).\n", cosa, -err, strerror(-err));
}

int main(int argc, char **argv)
{
    RtcTime     t;
    const char *arg = NULL;
    int         solo_data = 0, solo_ora = 0;
    int         e;
    int         i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "-h") == 0 || strcmp(a, "-help") == 0 ||
            strcmp(a, "--help") == 0 || strcmp(a, "-?") == 0) {
            aiuto();
            return 0;
        }
        if (strcmp(a, "-d") == 0) { solo_data = 1; continue; }
        if (strcmp(a, "-t") == 0) { solo_ora   = 1; continue; }
        if (strncmp(a, "-set-date:", 10) == 0 ||
            strncmp(a, "-set-time:", 10) == 0) {
            /* ! UNO SOLO PER VOLTA, e non per pigrizia: due scritture di
             * seguito sono due fermate del chip, e chi ne batte due nello
             * stesso comando si aspetta che siano una cosa sola. Se un giorno
             * servira' davvero, si aggiunge `-set:AAAA/MM/GG-HH:MM:SS`, che e'
             * una scrittura sola e lo dice. */
            if (arg != NULL) {
                printf("date: un'opzione di scrittura per volta.\n");
                printf("      `date -set-date:...` e poi `date -set-time:...`.\n");
                return 1;
            }
            arg = a;
            continue;
        }

        /* ! SI RIFIUTA UN'OPZIONE SCONOSCIUTA invece di ignorarla: ignorata in
         * silenzio, `date -set-dat:2026/09/16` stamperebbe l'ora e farebbe
         * credere che l'orologio sia stato rimesso. */
        printf("date: opzione sconosciuta '%s'\n", a);
        printf("      `date -h` le elenca tutte.\n");
        return 1;
    }

    if (solo_data && solo_ora) { solo_data = 0; solo_ora = 0; }

    /* La lettura serve SEMPRE: per stampare, e per riempire la meta' che una
     * scrittura non tocca. Vedi il commento in testa al file. */
    e = time_now(&t);
    if (e != 0) {
        spiega(e, "lettura");
        return 1;
    }

    if (arg != NULL) {
        RtcTime nuovo = t;
        int     ok;

        if (strncmp(arg, "-set-date:", 10) == 0) {
            ok = leggi_data(arg + 10, &nuovo);
            if (ok != 0) {
                printf("date: non capisco la data '%s'.\n", arg + 10);
                printf("      Si scrive AAAA/MM/GG, per esempio 2026/09/16\n");
                printf("      (va bene anche 2026-09-16). Tutte le cifre ci\n");
                printf("      vogliono: 2026/9/1 non basta, ci vuole 2026/09/01.\n");
                return 1;
            }
        } else {
            ok = leggi_ora(arg + 10, &nuovo);
            if (ok != 0) {
                printf("date: non capisco l'ora '%s'.\n", arg + 10);
                printf("      Si scrive HH:MM:SS su 24 ore, per esempio\n");
                printf("      17:52:30, oppure 17:52 e i secondi vanno a zero.\n");
                return 1;
            }
        }

        if (valida(&nuovo) != 0) return 1;

        e = time_set(&nuovo);
        if (e != 0) {
            spiega(e, "scrittura");
            return 1;
        }

        /* ! SI RILEGGE E SI STAMPA QUEL CHE C'E' ADESSO, non quel che si e'
         * chiesto. Sono due cose diverse ogni volta che il chip interpreta un
         * valore a modo suo, ed e' l'unica prova che la scrittura e' arrivata
         * fin dentro l'orologio invece di fermarsi per strada. */
        e = time_now(&t);
        if (e != 0) {
            spiega(e, "rilettura");
            return 1;
        }
        printf("orologio rimesso: ");
    }

    if (solo_data) {
        printf("%04u-%02u-%02u\n", t.anno, t.mese, t.giorno);
        return 0;
    }
    if (solo_ora) {
        printf("%02u:%02u:%02u\n", t.ora, t.minuto, t.secondo);
        return 0;
    }

    printf("%s %u %s %u, %02u:%02u:%02u\n",
           g_giorni[giorno_settimana(t.anno, t.mese, t.giorno)],
           t.giorno, g_mesi[t.mese - 1], t.anno,
           t.ora, t.minuto, t.secondo);
    return 0;
}

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
 *     date -ntp[:SERVER]            l'ora dalla rete  (serve root)
 *     date -fuso[:+1,ue]            il fuso per -ntp
 *     date -auto                    -ntp in silenzio, da /boot/avvio.sh
 *     date -install | -remove       la riga in /boot/avvio.sh
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
#include "dns.h"
#include "ip_proto.h"

/* +0.001 a ogni modifica: `date -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("date", "0.003");

/* Per -ntp e -fuso: vedi «-ntp — L'ORA DALLA RETE» piu' sotto. */
#define FUSO_CFG     "/boot/fuso.cfg"
#define NTP_SERVER   "pool.ntp.org"

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
    printf("Dalla rete (NTP, porta 123 UDP; serve root):\n");
    printf("  -ntp                  chiede l'ora a %s e rimette l'orologio\n", NTP_SERVER);
    printf("  -ntp:SERVER[:PORTA]   a quel server (anche -npt)\n");
    printf("  -fuso                 il fuso con cui l'ora UTC diventa locale\n");
    printf("  -fuso:+1,ue           lo cambia: ore da UTC (anche +5:30), e ue\n");
    printf("                        per l'ora legale europea o no. Senza\n");
    printf("                        %s vale +1,ue (Italia).\n\n", FUSO_CFG);
    printf("  -auto                 come -ntp, senza stampare niente: per l'avvio\n");
    printf("  -install, -remove     a ogni accensione si' o no, in /boot/avvio.sh\n\n");
    printf("  -h                    questo aiuto (anche -help e --help)\n\n");
    printf("Le due meta' si rimettono separate: chi cambia l'ora non deve\n");
    printf("ribattere la data, e viceversa.\n\n");
    printf("! L'anno sta fra 1980 e 2099. L'orologio conserva DUE cifre\n");
    printf("  d'anno, e il secolo lo mette il sistema: un 2150 tornerebbe\n");
    printf("  indietro come 2050, e un errore silenzioso e' peggio di un no.\n\n");
    printf("! L'orologio della macchina e' ora locale. Il fuso serve solo a\n");
    printf("  -ntp, che riceve l'ora UTC.\n\n");
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

/* =============================================================================
 * -ntp — L'ORA DALLA RETE (21 ottobre 2026)
 *
 *     date -ntp                 chiede l'ora al server e rimette l'orologio
 *     date -ntp:SERVER[:PORTA]  a quel server (nome o indirizzo)
 *     date -fuso                il fuso con cui si converte
 *     date -fuso:+1,ue          lo cambia: ore (anche +5:30), e ue = ora
 *                               legale europea, no = mai
 *
 * (-npt e -npt: valgono come -ntp: l'utente l'ha chiesto cosi', e un errore
 * di battitura che fa la cosa giusta costa meno di uno che la rifiuta.)
 *
 * ! NTP DA' L'ORA UTC, E L'OROLOGIO DELLA MACCHINA E' IN ORA LOCALE. EX-OS non
 * ha fusi orari (vedi tm_gmtoff in libc.h): la conversione si fa QUI, con
 * /boot/fuso.cfg. Senza il file vale l'Italia - UTC+1 e l'ora legale europea
 * - e il comando lo dice ogni volta, perche' un fuso supposto e' un'ora
 * sbagliata per chiunque stia altrove.
 *
 * ! L'ORA LEGALE EUROPEA E' UNA REGOLA SOLA, in UTC: dall'ultima domenica di
 * marzo alle 01:00 all'ultima domenica di ottobre alle 01:00, uguale per
 * tutti i paesi dell'Unione. Si calcola sull'istante UTC che arriva, quindi
 * il cambio dell'ora non ha l'ora doppia o mancante che avrebbe un conto
 * fatto sull'ora locale.
 *
 * ! E' SNTP (RFC 4330): una domanda, una risposta, il ritardo diviso a meta'.
 * L'orologio del CMOS tiene i secondi interi: si aspetta il secondo
 * successivo e lo si scrive allo scatto, cosi' l'errore resta di qualche
 * decina di millisecondi invece di arrivare a uno. Una deriva continua non
 * si corregge qui (time_set ferma il chip): questo e' il gesto di chi rimette
 * l'ora, come -set-time:.
 * ============================================================================= */
#define NTP_1970     2208988800u        /* secondi fra il 1900 di NTP e il 1970 */

typedef struct {
    int  minuti;            /* a est di UTC */
    int  ue;                /* 1 = ora legale europea */
    int  dal_file;          /* 0 = supposto (il file non c'e') */
    char server[128];
} Fuso;

static void fuso_leggi(Fuso *f)
{
    FILE *fp;
    char  riga[200];

    f->minuti = 60; f->ue = 1; f->dal_file = 0;
    strcpy(f->server, NTP_SERVER);
    fp = fopen(FUSO_CFG, "r");
    if (fp == NULL) return;
    f->dal_file = 1;
    while (fgets(riga, sizeof(riga), fp) != NULL) {
        char *c = riga, *v, *e;

        while (*c == ' ' || *c == '\t') c++;
        if (*c == '#' || (v = strchr(c, '=')) == NULL) continue;
        *v++ = '\0';
        for (e = v + strlen(v); e > v && (e[-1] == '\n' || e[-1] == '\r' ||
                                           e[-1] == ' ' || e[-1] == '\t'); ) *--e = '\0';
        while (*v == ' ' || *v == '\t') v++;
        for (e = c + strlen(c); e > c && (e[-1] == ' ' || e[-1] == '\t'); ) *--e = '\0';
        if (strcmp(c, "minuti") == 0)      f->minuti = atoi(v);
        else if (strcmp(c, "legale") == 0) f->ue = strcmp(v, "ue") == 0;
        else if (strcmp(c, "server") == 0 && *v && strlen(v) < sizeof(f->server))
            strcpy(f->server, v);
    }
    fclose(fp);
}

static void fuso_scrivi_ore(char *out, int minuti)
{
    int a = minuti < 0 ? -minuti : minuti;

    if (a % 60) sprintf(out, "%c%d:%02d", minuti < 0 ? '-' : '+', a / 60, a % 60);
    else        sprintf(out, "%c%d", minuti < 0 ? '-' : '+', a / 60);
}

static void fuso_mostra(const Fuso *f)
{
    char ore[12];

    fuso_scrivi_ore(ore, f->minuti);
    printf("fuso: UTC%s, ora legale %s, server %s%s\n", ore,
           f->ue ? "europea" : "mai", f->server,
           f->dal_file ? "" : "  (supposto: " FUSO_CFG " non c'e')");
}

/* "+1", "-3", "+5:30", "0", con ",ue" o ",no" in coda. */
static int fuso_imposta(const char *s)
{
    Fuso f;
    int  segno = 1, ore = 0, min = 0;
    const char *p = s;
    FILE *fp;
    char  testo[12];

    fuso_leggi(&f);
    if (*p == '+' || *p == '-') { if (*p == '-') segno = -1; p++; }
    if (*p < '0' || *p > '9') goto male;
    while (*p >= '0' && *p <= '9') ore = ore * 10 + (*p++ - '0');
    if (*p == ':') {
        p++;
        if (p[0] < '0' || p[0] > '5' || p[1] < '0' || p[1] > '9') goto male;
        min = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
    }
    if (ore > 14) goto male;
    if (*p == ',') {
        p++;
        if (strcmp(p, "ue") == 0)      f.ue = 1;
        else if (strcmp(p, "no") == 0) f.ue = 0;
        else goto male;
    } else if (*p != '\0') goto male;
    f.minuti = segno * (ore * 60 + min);

    fp = fopen(FUSO_CFG, "w");
    if (fp == NULL) {
        printf("date: non riesco a scrivere %s (%s).\n", FUSO_CFG, strerror(errno));
        if (errno == EACCES || errno == EPERM) printf("      Serve essere root.\n");
        return 1;
    }
    fuso_scrivi_ore(testo, f.minuti);
    fprintf(fp, "# %s - lo scrive `date -fuso:`, lo legge `date -ntp`\n", FUSO_CFG);
    fprintf(fp, "# UTC%s; legale = ue (ora legale europea) oppure no\n", testo);
    fprintf(fp, "minuti = %d\nlegale = %s\nserver = %s\n", f.minuti, f.ue ? "ue" : "no", f.server);
    fclose(fp);
    f.dal_file = 1;
    fuso_mostra(&f);
    return 0;

male:
    printf("date: non capisco il fuso '%s'.\n", s);
    printf("      Si scrive in ore da UTC, con ,ue per l'ora legale europea\n");
    printf("      o ,no: -fuso:+1,ue (Italia), -fuso:0,no, -fuso:+5:30,no.\n");
    return 1;
}

/* L'ultima domenica di `mese` (1-12) dell'anno, alle 01:00 UTC. */
static time_t ultima_domenica(int anno, int mese)
{
    struct tm t;
    time_t    x;

    memset(&t, 0, sizeof(t));
    t.tm_year = anno - 1900; t.tm_mon = mese - 1; t.tm_mday = 31; t.tm_hour = 1;
    x = timegm(&t);                         /* riempie tm_wday */
    return x - (time_t)t.tm_wday * 86400;
}

static int legale_ue(time_t utc)
{
    struct tm t;

    gmtime_r(&utc, &t);
    return utc >= ultima_domenica(t.tm_year + 1900, 3) &&
           utc <  ultima_domenica(t.tm_year + 1900, 10);
}

/* Cio' che viene dallo stack e' nostro da guardare; il resto resta sullo
 * scaffale (vedi filtro_stack in lib/dns.c, e perche'). */
static int filtro_stack(const IpcMessage *m, void *dato)
{
    return (int)m->sender_pid == *(const int *)dato ? IPC_MIO : IPC_ALTRUI;
}

static int attendi_esito(int pid_ip, unsigned int ms)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    IpcMessage    meta;
    IpEsito       e;

    for (;;) {
        if (ipc_scegli(filtro_stack, &pid_ip, &meta, buf, sizeof(buf), ms) < 0) return -EIO;
        if (meta.tipo != IP_MSG_ESITO || meta.len < sizeof(e)) continue;
        memcpy(&e, buf, sizeof(e));
        return e.codice;
    }
}

/* Una domanda al server. Rende 0 e l'istante UTC (secondi e millesimi) a cui
 * la risposta e' arrivata, o -1 dicendo perche'.
 *
 * ! NIENTE SOCKET BSD: nella libc.so non ci sono, e apposta (il floppy, vedi
 * lib/libc.c). Si parla allo stack come lib/dns.c, con una porta UDP aperta
 * e IP_MSG_UDP_*: la ricezione si prenota PRIMA dell'invio, o la risposta
 * di un server vicino arriva quando nessuno l'aspetta e lo stack la butta. */
static int ntp_chiedi(const char *server, unsigned int porta, time_t *sec,
                      unsigned int *ms, unsigned int *giro_ms)
{
    unsigned char  ip[4], buf[IPC_MSG_MAX_DATA], r[48];
    unsigned char  msg[sizeof(IpUdpInvia) + 48];
    unsigned char *q = msg + sizeof(IpUdpInvia);
    IpUdpInvia     inv;
    IpUdpApri      apri;
    IpcMessage     meta;
    unsigned int   t0 = 0, t1 = 0, frazione;
    unsigned long  s;
    int            pid_ip, e, prova, preso = 0;

    e = dns_risolvi(server, ip);
    if (e != 0) {
        printf("date: non trovo %s: %s.\n", server,
               e == -ENODEV   ? "la rete non e' accesa (manca /dev/ip.drv)" :
               e == -ENETDOWN ? "nessun server DNS configurato" :
               e == -ENOENT   ? "il nome non esiste" :
               e == -ETIMEDOUT ? "il DNS non risponde" : "errore del DNS");
        return -1;
    }
    pid_ip = ipc_lookup(IP_SERVIZIO);
    if (pid_ip <= 0) { printf("date: la rete non e' accesa.\n"); return -1; }

    apri.porta = 0;
    if (ipc_send(pid_ip, IP_MSG_UDP_APRI, &apri, sizeof(apri)) < 0 ||
        (e = attendi_esito(pid_ip, 2000)) <= 0) {
        printf("date: lo stack non apre una porta UDP.\n");
        return -1;
    }
    apri.porta = (unsigned int)e;

    for (prova = 0; prova < 3 && !preso; prova++) {
        unsigned int scadenza;

        if (ipc_send(pid_ip, IP_MSG_UDP_RICEVI, &apri, sizeof(apri)) < 0) break;
        memset(q, 0, 48);
        q[0] = 0x23;                         /* nessun avviso, versione 4, cliente */
        t0 = uptime_ms();
        q[40] = (unsigned char)(t0 >> 24); q[41] = (unsigned char)(t0 >> 16);
        q[42] = (unsigned char)(t0 >> 8);  q[43] = (unsigned char)t0;
        memcpy(inv.ip, ip, 4);
        inv.porta        = porta;
        inv.porta_locale = apri.porta;
        memcpy(msg, &inv, sizeof(inv));
        if (ipc_send(pid_ip, IP_MSG_UDP_INVIA, msg, sizeof(msg)) < 0) break;

        scadenza = uptime_ms() + 3000;
        while (!preso && (int)(uptime_ms() - scadenza) < 0) {
            IpUdpDati d;

            if (ipc_scegli(filtro_stack, &pid_ip, &meta, buf, sizeof(buf),
                           scadenza - uptime_ms()) < 0) break;
            if (meta.tipo == IP_MSG_ESITO) continue;     /* quello dell'invio */
            if (meta.tipo != IP_MSG_UDP_DATI || meta.len < sizeof(d)) continue;
            memcpy(&d, buf, sizeof(d));
            /* ! LA RISPOSTA DEVE ESSERE LA NOSTRA: modo server, e il nostro
             * istante d'invio rimandato indietro (origin timestamp). */
            if (d.len >= 48 && sizeof(d) + d.len <= meta.len &&
                (buf[sizeof(d)] & 7) == 4 && memcmp(buf + sizeof(d) + 24, q + 40, 8) == 0) {
                t1 = uptime_ms();
                memcpy(r, buf + sizeof(d), 48);
                preso = 1;
            } else if (ipc_send(pid_ip, IP_MSG_UDP_RICEVI, &apri, sizeof(apri)) < 0) {
                break;
            }
        }
    }
    ipc_send(pid_ip, IP_MSG_UDP_CHIUDI, &apri, sizeof(apri));
    attendi_esito(pid_ip, 500);   /* consumata: vedi la fine di lib/dns.c */

    if (!preso) {
        printf("date: %s non risponde (porta %u UDP, tre tentativi).\n", server, porta);
        return -1;
    }
    if (r[1] == 0 || (r[0] & 0xC0) == 0xC0) {
        /* Stratum 0 e' un «Kiss-o'-Death»; avviso 3 = orologio non sincronizzato. */
        printf("date: %s risponde ma non da' l'ora (%s).\n", server,
               r[1] == 0 ? "ci chiede di smettere" : "il suo orologio non e' sincronizzato");
        return -1;
    }
    s = ((unsigned long)r[40] << 24) | ((unsigned long)r[41] << 16) |
        ((unsigned long)r[42] << 8) | r[43];
    frazione = ((unsigned int)r[44] << 8) | r[45];          /* 1/65536 di secondo */
    *giro_ms = t1 - t0;
    /* Il server ha scritto l'ora quando ha risposto: a noi arriva mezzo giro dopo. */
    *ms  = (unsigned int)((frazione * 1000u) >> 16) + *giro_ms / 2;
    *sec = (time_t)(s - NTP_1970) + (time_t)(*ms / 1000);
    *ms %= 1000;
    return 0;
}

static void rtc_da(time_t x, RtcTime *t)
{
    struct tm m;

    gmtime_r(&x, &m);
    t->anno = (unsigned int)m.tm_year + 1900; t->mese = (unsigned int)m.tm_mon + 1;
    t->giorno = (unsigned int)m.tm_mday;
    t->ora = (unsigned int)m.tm_hour; t->minuto = (unsigned int)m.tm_min;
    t->secondo = (unsigned int)m.tm_sec;
}

static time_t rtc_a(const RtcTime *t)
{
    struct tm m;

    memset(&m, 0, sizeof(m));
    m.tm_year = (int)t->anno - 1900; m.tm_mon = (int)t->mese - 1; m.tm_mday = (int)t->giorno;
    m.tm_hour = (int)t->ora; m.tm_min = (int)t->minuto; m.tm_sec = (int)t->secondo;
    return timegm(&m);
}

static int comando_ntp(const char *server_chiesto)
{
    Fuso         f;
    RtcTime      prima, nuovo;
    time_t       utc, locale;
    unsigned int ms, giro, inizio, porta = 123;
    char        *due;
    long         scarto;
    int          e, legale;

    fuso_leggi(&f);
    if (server_chiesto && *server_chiesto) {
        if (strlen(server_chiesto) >= sizeof(f.server)) {
            printf("date: nome del server troppo lungo.\n");
            return 1;
        }
        strcpy(f.server, server_chiesto);
    }
    /* SERVER:PORTA, per un server che non sta sulla 123 (e per la prova). */
    due = strchr(f.server, ':');
    if (due != NULL) {
        *due++ = '\0';
        porta = (unsigned int)atoi(due);
        if (porta == 0 || porta > 65535) {
            printf("date: la porta '%s' non va: sta fra 1 e 65535.\n", due);
            return 1;
        }
    }
    fuso_mostra(&f);

    if (ntp_chiedi(f.server, porta, &utc, &ms, &giro) != 0) return 1;
    inizio = uptime_ms();
    printf("ntp: %s, andata e ritorno %u ms\n", f.server, giro);

    /* Allo scatto del secondo dopo: vedi in testa alla sezione. ! L'ora
     * legale si decide sull'istante che si SCRIVE, non su quello arrivato:
     * alle 00:59:59,5 UTC dell'ultima domenica di marzo il secondo scritto
     * e' gia' quello del cambio. */
    {
        unsigned int ora_ms = ms + (uptime_ms() - inizio);

        utc += (time_t)(ora_ms / 1000);
        usleep((1000u - ora_ms % 1000u) * 1000u);
        utc += 1;
    }
    legale = f.ue && legale_ue(utc);
    locale = utc + (time_t)f.minuti * 60 + (legale ? 3600 : 0);

    e = time_now(&prima);
    if (e == 0) {
        scarto = (long)(rtc_a(&prima) - locale);
        if (scarto == 0)      printf("l'orologio era giusto (al secondo).\n");
        else if (scarto > 0)  printf("l'orologio era avanti di %ld s.\n", scarto);
        else                  printf("l'orologio era indietro di %ld s.\n", -scarto);
    }
    rtc_da(locale, &nuovo);
    if (valida(&nuovo) != 0) return 1;
    e = time_set(&nuovo);
    if (e != 0) {
        spiega(e, "scrittura");
        return 1;
    }
    e = time_now(&nuovo);
    if (e != 0) { spiega(e, "rilettura"); return 1; }
    printf("orologio rimesso: %s %u %s %u, %02u:%02u:%02u%s\n",
           g_giorni[giorno_settimana(nuovo.anno, nuovo.mese, nuovo.giorno)],
           nuovo.giorno, g_mesi[nuovo.mese - 1], nuovo.anno,
           nuovo.ora, nuovo.minuto, nuovo.secondo, legale ? " (ora legale)" : "");
    return 0;
}

/* -install / -remove: the boot block in /boot/avvio.sh (avvio_blocco, libc).
 *
 * ! A MACHINE WITH A DEAD CMOS BATTERY STARTS FROM THE DATE OF ITS BIOS (the
 * laptop: 2007-09-01), and until someone sets it every file is dated that day
 * and every certificate is "not yet valid". So the line goes after `dhcp`,
 * with '&': no network or no answer must not hold up the boot.
 *
 * ! hwconfig and the CD's avvio.sh write the same line outside any block, so
 * -install looks first: two lines would only ask the server twice. */
static int installa(int togli)
{
    int rc;

    if (getuid() != 0) { printf("date: -install e -remove solo da root.\n"); return 1; }
    rc = avvio_blocco("date", NULL);
    if (rc == 0 && !togli && !avvio_ha_riga("date -auto"))
        rc = avvio_blocco("date",
                          "# L'ora dalla rete a ogni accensione, in silenzio: chi ha la\n"
                          "# pila dell'orologio scarica riparte dalla data del BIOS.\n"
                          "# `date -remove` toglie questo blocco.\n"
                          "date -auto &\n");
    if (rc < 0) {
        printf("date: non riesco a scrivere /boot/avvio.sh (errno %d).\n", errno);
        return 1;
    }
    if (togli && avvio_ha_riga("date -auto"))
        printf("date: la riga `date -auto &` in /boot/avvio.sh l'ha scritta hwconfig: "
               "va tolta a mano.\n");
    else
        printf(togli ? "date: niente piu' ora dalla rete all'avvio.\n"
                     : "date: ora dalla rete a ogni accensione (/boot/avvio.sh).\n");
    return 0;
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
        if (strcmp(a, "-ntp") == 0 || strcmp(a, "-npt") == 0) return comando_ntp(NULL);
        if (strncmp(a, "-ntp:", 5) == 0 || strncmp(a, "-npt:", 5) == 0)
            return comando_ntp(a + 5);
        if (strcmp(a, "-auto") == 0) {
            /* a clock put right is not news, and a missing network is not an
             * error at boot: this runs while the login prompt is coming up */
            close(1);
            close(2);
            return comando_ntp(NULL);
        }
        if (strcmp(a, "-install") == 0) return installa(0);
        if (strcmp(a, "-remove") == 0)  return installa(1);
        if (strcmp(a, "-fuso") == 0) { Fuso f; fuso_leggi(&f); fuso_mostra(&f); return 0; }
        if (strncmp(a, "-fuso:", 6) == 0) return fuso_imposta(a + 6);
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

/* =============================================================================
 * exwin/bin/browser/biscotti.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La dispensa dei biscotti. Il perche' delle scelte grosse sta in biscotti.h;
 * qui ci sono quelle che si vedono solo scrivendo.
 *
 * ! QUESTO FILE NON CHIAMA LA LIBC, come exdom e per la stessa ragione: gira
 * anche sull'ospite, dentro il banco di prova. Le quattro funzioni che servono
 * stanno qui e sono corte.
 * ============================================================================= */

#include "biscotti.h"

static unsigned int lung(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static int minusc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* Confronto senza distinzione di maiuscole: i domini e i nomi delle voci di un
 * `Set-Cookie` arrivano scritti come capita. */
static int ugu_min(const char *a, const char *b)
{
    while (*a && minusc((unsigned char)*a) == minusc((unsigned char)*b)) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

static int ugu(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

static void copia(char *dst, unsigned int max, const char *s, unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n && i + 1 < max; i++) dst[i] = s[i];
    dst[i] = '\0';
}

static int bianco(int c) { return c == ' ' || c == '\t'; }

/* =============================================================================
 * LE DATE
 *
 * ! «Expires» ARRIVA IN INGLESE E IN UN FORMATO DEL 1982, ed e' l'unico posto
 * di tutto il sistema in cui si legge una data scritta da qualcun altro:
 *
 *     Wed, 09 Jun 2021 10:18:14 GMT        RFC 1123, quella che mandano tutti
 *     Wednesday, 09-Jun-21 10:18:14 GMT    RFC 850, che si incontra ancora
 *
 * ! SI LEGGONO I PEZZI CHE SERVONO E SI IGNORA IL RESTO. Il giorno della
 * settimana non lo si guarda — e' ridondante e i server lo sbagliano — e il
 * fuso e' sempre GMT per specifica. Quel che conta e' arrivare a un numero di
 * secondi confrontabile con il nostro orologio.
 *
 * ! E SE NON SI CAPISCE SI RENDE 0, che vuol dire «di sessione». Inventare una
 * scadenza da una data illeggibile vorrebbe dire o buttare via un biscotto
 * buono o tenerne uno morto per anni; farlo morire alla chiusura e' l'errore
 * piu' piccolo dei due, ed e' anche quello che l'utente puo' capire.
 * ========================================================================== */
static const char *MESI[12] = {
    "jan", "feb", "mar", "apr", "may", "jun",
    "jul", "aug", "sep", "oct", "nov", "dec"
};

static int mese_di(const char *s)
{
    int m;

    for (m = 0; m < 12; m++)
        if (minusc((unsigned char)s[0]) == MESI[m][0] &&
            minusc((unsigned char)s[1]) == MESI[m][1] &&
            minusc((unsigned char)s[2]) == MESI[m][2]) return m + 1;
    return 0;
}

/* I giorni dall'epoca per una data del calendario. E' l'algoritmo dei giorni
 * civili: niente tabelle di mesi, e gli anni bisestili vengono da se'. */
static long giorni_civili(long y, int m, int d)
{
    long era, yoe, doy, doe;

    y -= (m <= 2);
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int cifra(int c) { return c >= '0' && c <= '9'; }

unsigned int bis_data(const char *s)
{
    int          d = 0, m = 0, hh = 0, mm = 0, ss = 0;
    long         y = 0, gg;
    unsigned int i = 0;

    if (!s) return 0;

    /* Si salta tutto fino alla prima cifra: e' il giorno del mese, e quel che
     * viene prima e' il nome del giorno della settimana, che non serve. */
    while (s[i] && !cifra((unsigned char)s[i])) i++;
    if (!s[i]) return 0;
    while (cifra((unsigned char)s[i])) d = d * 10 + (s[i++] - '0');

    while (s[i] == ' ' || s[i] == '-') i++;
    if (!s[i] || !s[i+1] || !s[i+2]) return 0;
    m = mese_di(s + i);
    if (!m) return 0;
    while (s[i] && s[i] != ' ' && s[i] != '-') i++;
    while (s[i] == ' ' || s[i] == '-') i++;

    while (cifra((unsigned char)s[i])) y = y * 10 + (s[i++] - '0');
    /* ! DUE CIFRE SONO DEL SECOLO NOSTRO O DI QUELLO PRIMA, e la regola e'
     * quella della specifica: sotto 70 e' 20xx, da 70 in su e' 19xx. Un
     * biscotto con «-70» e' del 1970, cioe' morto: ed e' proprio cosi' che
     * mezzo web ne cancella uno. */
    if (y < 100) y += (y < 70) ? 2000 : 1900;

    while (s[i] == ' ') i++;
    if (cifra((unsigned char)s[i])) {
        while (cifra((unsigned char)s[i])) hh = hh * 10 + (s[i++] - '0');
        if (s[i] == ':') i++;
        while (cifra((unsigned char)s[i])) mm = mm * 10 + (s[i++] - '0');
        if (s[i] == ':') i++;
        while (cifra((unsigned char)s[i])) ss = ss * 10 + (s[i++] - '0');
    }

    if (d < 1 || d > 31 || y < 1970 || y > 2100) return 0;

    gg = giorni_civili(y, m, d);
    if (gg < 0) return 0;
    return (unsigned int)(gg * 86400L + hh * 3600L + mm * 60L + ss);
}

/* =============================================================================
 * DOMINI E PERCORSI
 * ========================================================================== */

/* ! IL CONFRONTO FRA DOMINI SI FA A ETICHETTE, non a lettere. `ex.os` e'
 * suffisso di `www.ex.os` ma NON di `notex.os`, e un confronto che guarda solo
 * la coda direbbe di si' a tutt'e due: sarebbe un modo per far arrivare i
 * biscotti di un sito a un altro che ha scelto il nome apposta. */
static int dominio_copre(const char *dom, const char *host)
{
    unsigned int ld = lung(dom), lh = lung(host), i;

    if (ld == 0 || lh < ld) return 0;
    for (i = 0; i < ld; i++)
        if (minusc((unsigned char)dom[ld - 1 - i]) !=
            minusc((unsigned char)host[lh - 1 - i])) return 0;
    if (lh == ld) return 1;
    return host[lh - ld - 1] == '.';
}

/* ! IL PERCORSO PREDEFINITO E' LA DIRECTORY, NON LA PAGINA. Un biscotto messo
 * da «/conto/entra.html» senza `Path` vale per «/conto/», cioe' anche per
 * «/conto/esci.html»: e' quel che dice la specifica, ed e' anche l'unica cosa
 * utile — un biscotto che valesse per una sola pagina non servirebbe a nessuno. */
static void percorso_predefinito(const char *perc, char *fuori, unsigned int max)
{
    unsigned int ultima = 0, i;

    if (!perc || perc[0] != '/') { copia(fuori, max, "/", 1); return; }
    for (i = 0; perc[i] && perc[i] != '?' && perc[i] != '#'; i++)
        if (perc[i] == '/') ultima = i;
    if (ultima == 0) { copia(fuori, max, "/", 1); return; }
    copia(fuori, max, perc, ultima);
}

/* ! «/conto» COPRE «/conto/x» MA NON «/contoltre». Il confronto e' per pezzi
 * di percorso, e chi si ferma al prefisso di lettere sbaglia proprio li'. */
static int percorso_copre(const char *p, const char *richiesto)
{
    unsigned int lp = lung(p), i;

    if (lp == 0) return 1;
    for (i = 0; i < lp; i++) if (p[i] != richiesto[i]) return 0;
    if (richiesto[lp] == '\0') return 1;
    if (p[lp - 1] == '/') return 1;
    return richiesto[lp] == '/';
}

/* =============================================================================
 * LEGGERE UNA RIGA Set-Cookie
 * ========================================================================== */
typedef struct {
    char         nome[BIS_NOME_MAX];
    char         valore[BIS_VAL_MAX];
    char         dominio[BIS_DOM_MAX];
    char         percorso[BIS_PERC_MAX];
    unsigned int scade_s;
    int          ha_scadenza;
    int          togli;             /* Max-Age <= 0, o scadenza gia' passata */
    int          sicuro;
    int          solo_http;
    int          con_sottodomini;
} Letto;

/* Copia il pezzo [a,b) senza gli spazi ai bordi. */
static void pezzo(const char *s, unsigned int a, unsigned int b,
                  char *fuori, unsigned int max)
{
    while (a < b && bianco(s[a])) a++;
    while (b > a && bianco(s[b - 1])) b--;
    copia(fuori, max, s + a, b - a);
}

static int leggi_riga(const char *riga, Letto *L, unsigned int ora_s)
{
    unsigned int i = 0, a, b;
    char         voce[32], val[BIS_VAL_MAX];

    L->nome[0] = L->valore[0] = L->dominio[0] = L->percorso[0] = '\0';
    L->scade_s = 0;
    L->ha_scadenza = L->togli = L->sicuro = L->solo_http = 0;
    L->con_sottodomini = 0;
    if (!riga) return 0;

    /* Il primo pezzo e' «nome=valore», e li' l'uguale non e' facoltativo. */
    a = 0;
    while (riga[i] && riga[i] != ';') i++;
    b = i;
    {
        unsigned int u = a;

        while (u < b && riga[u] != '=') u++;
        if (u >= b) return 0;
        pezzo(riga, a, u, L->nome, sizeof(L->nome));
        pezzo(riga, u + 1, b, L->valore, sizeof(L->valore));
        if (!L->nome[0]) return 0;
    }

    while (riga[i] == ';') {
        i++;
        a = i;
        while (riga[i] && riga[i] != ';') i++;
        b = i;
        {
            unsigned int u = a;

            while (u < b && riga[u] != '=') u++;
            pezzo(riga, a, u, voce, sizeof(voce));
            if (u < b) pezzo(riga, u + 1, b, val, sizeof(val));
            else       val[0] = '\0';
        }

        if (ugu_min(voce, "path") && val[0] == '/') {
            copia(L->percorso, sizeof(L->percorso), val, lung(val));
        } else if (ugu_min(voce, "domain") && val[0]) {
            /* Il punto davanti si toglie: «.ex.os» e «ex.os» vogliono dire la
             * stessa cosa da vent'anni, e tenerlo vorrebbe dire due forme
             * dello stesso dominio nella dispensa. */
            const char *dv = (val[0] == '.') ? val + 1 : val;

            copia(L->dominio, sizeof(L->dominio), dv, lung(dv));
            L->con_sottodomini = 1;
        } else if (ugu_min(voce, "secure")) {
            L->sicuro = 1;
        } else if (ugu_min(voce, "httponly")) {
            L->solo_http = 1;
        } else if (ugu_min(voce, "max-age")) {
            int segno = 1, v = 0;
            unsigned int k = 0;

            if (val[k] == '-') { segno = -1; k++; }
            else if (val[k] == '+') k++;
            while (cifra((unsigned char)val[k])) v = v * 10 + (val[k++] - '0');
            /* ! E SI CANCELLA QUEL CHE `Expires` AVEVA GIA' DECISO. Le due
             * voci arrivano nell'ordine che vuole il server, e un
             * «Expires nel 1970» seguito da «Max-Age=3600» vuol dire che il
             * biscotto VIVE: e' la forma con cui si tiene in piedi un browser
             * vecchio e uno nuovo con la stessa riga. Senza questo azzeramento
             * si leggeva la prima e si buttava la seconda. */
            L->ha_scadenza = 1;
            if (segno < 0 || v == 0) { L->togli = 1; L->scade_s = 0; }
            else                     { L->togli = 0; L->scade_s = ora_s + (unsigned int)v; }
        } else if (ugu_min(voce, "expires")) {
            unsigned int q = bis_data(val);

            /* ! Max-Age VINCE SU Expires, e non e' un capriccio: lo dice la
             * specifica, e i server mandano tutt'e due proprio contando su
             * quella regola per i browser vecchi. */
            if (!L->ha_scadenza) {
                if (q == 0) {
                    /* data illeggibile: resta di sessione, vedi bis_data */
                } else if (q <= ora_s) {
                    L->ha_scadenza = 1;
                    L->togli = 1;
                } else {
                    L->ha_scadenza = 1;
                    L->scade_s = q;
                }
            }
        }
        /* Una voce che non si conosce si salta, come fa il kernel con
         * kernel.cfg: e' cio' che permette al web di aggiungerne di nuove
         * senza rompere chi c'era prima. */
    }
    return 1;
}

/* =============================================================================
 * LA DISPENSA
 * ========================================================================== */
void bis_azzera(Dispensa *d)
{
    int i;

    if (!d) return;
    for (i = 0; i < BIS_MAX; i++) d->b[i].usato = 0;
    d->persi = 0;
}

static int trova(Dispensa *d, const char *dom, const char *perc, const char *nome)
{
    int i;

    for (i = 0; i < BIS_MAX; i++)
        if (d->b[i].usato && ugu_min(d->b[i].dominio, dom) &&
            ugu(d->b[i].percorso, perc) && ugu(d->b[i].nome, nome)) return i;
    return -1;
}

/* ! QUANDO E' PIENA SI BUTTA IL PIU' VICINO A SCADERE, non il primo che
 * capita. Un biscotto di sessione non ha scadenza e vale piu' di uno che muore
 * fra un minuto: e' quello che tiene in piedi un accesso fatto adesso. */
static int posto_libero(Dispensa *d)
{
    int i, peggiore = -1;
    unsigned int quando = 0xFFFFFFFFu;

    for (i = 0; i < BIS_MAX; i++) if (!d->b[i].usato) return i;
    for (i = 0; i < BIS_MAX; i++) {
        unsigned int s = d->b[i].scade_s ? d->b[i].scade_s : 0xFFFFFFFEu;

        if (s < quando) { quando = s; peggiore = i; }
    }
    d->persi = 1;
    return peggiore;
}

static int metti(Dispensa *d, const char *host, const char *richiesta,
                 const Letto *L, int da_script)
{
    const char *dom = L->dominio[0] ? L->dominio : host;
    char        perc[BIS_PERC_MAX];
    int         k;

    /* ! UN SITO NON PUO' METTERE BISCOTTI PER UN ALTRO, ed e' il controllo che
     * non si puo' saltare: senza, una pagina qualunque potrebbe scrivere un
     * biscotto «Domain=google.com» e farselo mandare da noi la volta dopo. */
    if (L->dominio[0] && !dominio_copre(L->dominio, host)) return 0;

    if (L->percorso[0]) copia(perc, sizeof(perc), L->percorso, lung(L->percorso));
    else                percorso_predefinito(richiesta, perc, sizeof(perc));

    k = trova(d, dom, perc, L->nome);

    if (L->togli) {
        if (k >= 0) { d->b[k].usato = 0; return 1; }
        return 0;
    }

    if (k < 0) {
        k = posto_libero(d);
        if (k < 0) return 0;
    }

    d->b[k].usato = 1;
    copia(d->b[k].dominio,  sizeof(d->b[k].dominio),  dom, lung(dom));
    copia(d->b[k].percorso, sizeof(d->b[k].percorso), perc, lung(perc));
    copia(d->b[k].nome,     sizeof(d->b[k].nome),     L->nome, lung(L->nome));
    copia(d->b[k].valore,   sizeof(d->b[k].valore),   L->valore, lung(L->valore));
    d->b[k].scade_s         = L->ha_scadenza ? L->scade_s : 0;
    d->b[k].sicuro          = (unsigned char)(L->sicuro != 0);
    /* ! UNO SCRIPT NON PUO' FARE UN BISCOTTO CHE GLI SCRIPT NON VEDONO. E'
     * l'unica differenza fra le due porte d'ingresso, e senza di lei
     * `HttpOnly` diventerebbe una parola che chiunque puo' scrivere — cioe'
     * nessuna protezione. */
    d->b[k].solo_http       = (unsigned char)(!da_script && L->solo_http);
    d->b[k].con_sottodomini = (unsigned char)(L->con_sottodomini != 0);
    return 1;
}

int bis_arrivato(Dispensa *d, const char *host, const char *percorso,
                 const char *riga, unsigned int ora_s)
{
    Letto L;

    if (!d || !host || !riga) return 0;
    if (!leggi_riga(riga, &L, ora_s)) return 0;
    return metti(d, host, percorso, &L, 0);
}

int bis_da_script(Dispensa *d, const char *host, const char *percorso,
                  const char *riga, unsigned int ora_s)
{
    Letto L;

    if (!d || !host || !riga) return 0;
    if (!leggi_riga(riga, &L, ora_s)) return 0;
    return metti(d, host, percorso, &L, 1);
}

static int vale_qui(const Biscotto *b, const char *host, const char *perc,
                    int cifrata, int per_script, unsigned int ora_s)
{
    if (!b->usato) return 0;
    if (b->scade_s && ora_s && b->scade_s <= ora_s) return 0;
    if (b->sicuro && !cifrata) return 0;
    if (per_script && b->solo_http) return 0;

    if (b->con_sottodomini) { if (!dominio_copre(b->dominio, host)) return 0; }
    else                    { if (!ugu_min(b->dominio, host)) return 0; }

    return percorso_copre(b->percorso, perc);
}

void bis_da_mandare(const Dispensa *d, const char *host, const char *percorso,
                    int cifrata, int per_script, unsigned int ora_s,
                    char *fuori, unsigned int max)
{
    unsigned int u = 0;
    char         perc[BIS_PERC_MAX];
    int          i;

    if (!fuori || max == 0) return;
    fuori[0] = '\0';
    if (!d || !host) return;

    /* Il percorso della richiesta si taglia prima della query: «/a?b=1» e'
     * la pagina «/a». */
    {
        unsigned int k = 0;

        while (percorso && percorso[k] && percorso[k] != '?' &&
               percorso[k] != '#' && k + 1 < sizeof(perc)) k++;
        copia(perc, sizeof(perc), percorso ? percorso : "/", k);
        if (!perc[0]) copia(perc, sizeof(perc), "/", 1);
    }

    for (i = 0; i < BIS_MAX; i++) {
        const Biscotto *b = &d->b[i];
        unsigned int    ln, lv;

        if (!vale_qui(b, host, perc, cifrata, per_script, ora_s)) continue;

        ln = lung(b->nome); lv = lung(b->valore);
        /* ! SE NON CI STA NON SI SCRIVE A META'. Un «Cookie:» tagliato in
         * mezzo a un valore e' peggio di uno che manca: il server lo legge,
         * non lo riconosce, e risponde una cosa che non si spiega. */
        if (u + (u ? 2 : 0) + ln + 1 + lv + 1 > max) break;
        if (u) { fuori[u++] = ';'; fuori[u++] = ' '; }
        copia(fuori + u, max - u, b->nome, ln);   u += ln;
        fuori[u++] = '=';
        copia(fuori + u, max - u, b->valore, lv); u += lv;
        fuori[u] = '\0';
    }
}

int bis_pulisci(Dispensa *d, unsigned int ora_s)
{
    int i, tolti = 0;

    if (!d || !ora_s) return 0;
    for (i = 0; i < BIS_MAX; i++)
        if (d->b[i].usato && d->b[i].scade_s && d->b[i].scade_s <= ora_s) {
            d->b[i].usato = 0;
            tolti++;
        }
    return tolti;
}

/* =============================================================================
 * IL FILE
 *
 * ! UNA RIGA PER BISCOTTO, I CAMPI SEPARATI DA UNA TABULAZIONE, e il valore in
 * fondo. La tabulazione perche' e' l'unico carattere che in un biscotto non ci
 * puo' stare; il valore in fondo perche' e' l'unico che puo' contenere di
 * tutto, spazi compresi, e cosi' si legge «fino a fine riga» senza domande.
 *
 * ! E LA PRIMA RIGA DICE COS'E'. Un file di dati senza intestazione, aperto fra
 * un anno, e' un file di cui si deve indovinare il formato.
 * ========================================================================== */
static unsigned int metti_s(char *out, unsigned int max, unsigned int u,
                            const char *s)
{
    unsigned int i;

    for (i = 0; s[i]; i++) {
        if (u + 1 >= max) return u;
        out[u++] = s[i];
    }
    return u;
}

static unsigned int metti_n(char *out, unsigned int max, unsigned int u,
                            unsigned int v)
{
    char rov[12];
    int  k = 0;

    if (v == 0) { if (u + 1 < max) out[u++] = '0'; return u; }
    while (v && k < 12) { rov[k++] = (char)('0' + (v % 10)); v /= 10; }
    while (k) { if (u + 1 >= max) return u; out[u++] = rov[--k]; }
    return u;
}

unsigned int bis_salva(const Dispensa *d, char *fuori, unsigned int max,
                       unsigned int ora_s)
{
    unsigned int u = 0;
    int          i;

    if (!fuori || max == 0) return 0;
    fuori[0] = '\0';
    if (!d) return 0;

    u = metti_s(fuori, max, u,
                "# i biscotti del navigatore di EX-OS\n"
                "# dominio<TAB>percorso<TAB>bandiere<TAB>scadenza<TAB>nome<TAB>valore\n"
                "# bandiere: s=solo https, h=non visibile agli script, "
                "d=vale anche per i sottodomini\n");

    for (i = 0; i < BIS_MAX; i++) {
        const Biscotto *b = &d->b[i];

        if (!b->usato) continue;
        /* Vedi biscotti.h: i biscotti di sessione non vanno su disco. */
        if (!b->scade_s) continue;
        if (ora_s && b->scade_s <= ora_s) continue;

        u = metti_s(fuori, max, u, b->dominio);   u = metti_s(fuori, max, u, "\t");
        u = metti_s(fuori, max, u, b->percorso);  u = metti_s(fuori, max, u, "\t");
        if (b->sicuro)          u = metti_s(fuori, max, u, "s");
        if (b->solo_http)       u = metti_s(fuori, max, u, "h");
        if (b->con_sottodomini) u = metti_s(fuori, max, u, "d");
        u = metti_s(fuori, max, u, "\t");
        u = metti_n(fuori, max, u, b->scade_s);   u = metti_s(fuori, max, u, "\t");
        u = metti_s(fuori, max, u, b->nome);      u = metti_s(fuori, max, u, "\t");
        u = metti_s(fuori, max, u, b->valore);    u = metti_s(fuori, max, u, "\n");
    }
    if (u < max) fuori[u] = '\0';
    return u;
}

void bis_carica(Dispensa *d, const char *testo, unsigned int ora_s)
{
    unsigned int i = 0;

    if (!d || !testo) return;

    while (testo[i]) {
        unsigned int a[6], b[6];
        int          k, campo = 0;

        /* Una riga per volta; le vuote e i commenti si saltano. */
        while (testo[i] == '\n' || testo[i] == '\r') i++;
        if (!testo[i]) break;
        if (testo[i] == '#') {
            while (testo[i] && testo[i] != '\n') i++;
            continue;
        }

        for (k = 0; k < 6; k++) { a[k] = b[k] = i; }
        a[0] = i;
        while (testo[i] && testo[i] != '\n' && testo[i] != '\r') {
            /* ! L'ULTIMO CAMPO NON SI SPEZZA PIU': una tabulazione dentro il
             * valore non ci puo' stare, ma leggere «fino a fine riga» e' quel
             * che rende il formato rileggibile senza pensarci. */
            if (testo[i] == '\t' && campo < 5) {
                b[campo] = i;
                campo++;
                a[campo] = i + 1;
            }
            i++;
        }
        b[campo] = i;

        if (campo == 5) {
            Biscotto B;
            unsigned int k2, scad = 0;

            B.usato = 1;
            copia(B.dominio,  sizeof(B.dominio),  testo + a[0], b[0] - a[0]);
            copia(B.percorso, sizeof(B.percorso), testo + a[1], b[1] - a[1]);
            B.sicuro = B.solo_http = B.con_sottodomini = 0;
            for (k2 = a[2]; k2 < b[2]; k2++) {
                if (testo[k2] == 's') B.sicuro = 1;
                if (testo[k2] == 'h') B.solo_http = 1;
                if (testo[k2] == 'd') B.con_sottodomini = 1;
            }
            for (k2 = a[3]; k2 < b[3]; k2++)
                if (cifra((unsigned char)testo[k2]))
                    scad = scad * 10 + (unsigned int)(testo[k2] - '0');
            B.scade_s = scad;
            copia(B.nome,   sizeof(B.nome),   testo + a[4], b[4] - a[4]);
            copia(B.valore, sizeof(B.valore), testo + a[5], b[5] - a[5]);

            /* ! UN BISCOTTO GIA' SCADUTO NON SI RICARICA. Il file resta com'e'
             * finche' non lo si riscrive, e ricaricarlo vorrebbe dire mandare
             * al server una cosa che lui ha gia' fatto morire. */
            if (B.nome[0] && B.scade_s && (!ora_s || B.scade_s > ora_s)) {
                int p = trova(d, B.dominio, B.percorso, B.nome);

                if (p < 0) p = posto_libero(d);
                if (p >= 0) d->b[p] = B;
            }
        }
    }
}

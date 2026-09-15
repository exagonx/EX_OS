/* =============================================================================
 * bin/soccorso/soccorso.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * soccorso — rimette a posto /lib/libc.so quando non parte piu' niente
 *
 *     soccorso                 la prende dal server dell'aggiornamento
 *     soccorso -url <radice>   da un indirizzo dato a mano
 *     soccorso -guarda         dice solo che cosa farebbe
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' ESISTE, E PERCHE' E' STATICO
 *
 * Il 15 settembre 2026 un aggiornamento su una macchina vera ha installato
 * ottantaquattro programmi nuovi e poi, arrivato a `lib/libc.so`, ha perso la
 * rete. Da quel momento ogni comando rispondeva:
 *
 *     libc: la libreria condivisa non ha la funzione blk_espelli
 *           La libc installata e' piu' vecchia di questo programma.
 *
 * compresi quelli che servivano a rimediare — `scarica`, `dhcp`, `ipcfg`,
 * `netupdate`. Una macchina con la rete a posto e nessun modo di usarla.
 *
 * ! QUESTO PROGRAMMA NON DIPENDE DALLA LIBC CONDIVISA. Si compila con
 * lib/libc.c DENTRO, come fanno i driver: nessun ponte da risolvere, nessun
 * nome da cercare in un file che magari non c'e' nemmeno. Finche' il kernel
 * carica un ELF e lo stack IP risponde, questo parte — ed e' l'unica cosa che
 * deve fare.
 *
 * ! E PER LA STESSA RAGIONE NON USA exhttp.so. Anche quella e' una libreria
 * condivisa: appoggiarcisi vorrebbe dire dipendere proprio da cio' che si sta
 * riparando. L'HTTP qui dentro e' ridotto all'osso — una GET, «Connection:
 * close», Content-Length — e va bene cosi': deve scaricare un file da un
 * server che conosciamo, non navigare.
 *
 * ! L'IMPRONTA SI CONTROLLA LO STESSO. Una libc sbagliata e' peggio di una
 * libc vecchia: si prende `elenco.txt`, si cerca la riga di lib/libc.so, e si
 * confrontano dimensione e SHA-256 mentre il file arriva. Se non tornano, il
 * .new si butta e non si tocca niente.
 * ============================================================================= */
#include "libc.h"
#include "ip_proto.h"
#include "rete.h"
#include "dns.h"

/* +0.001 a ogni modifica: `soccorso -version` la stampa. Vedi EX_VERSIONE. */
EX_VERSIONE("soccorso", "0.001");

#define CNF        "/boot/netupdate.cnf"
#define LIBC       "/lib/libc.so"
#define LIBC_NEW   "/lib/libc.so.new"
#define LIBC_PRIMA "/lib/libc.so.prima"

#define URL_MAX    256
#define RIGA_MAX   512
#define BLOCCO     1024

static int  g_pid_ip = 0;
static int  g_guarda = 0;

/* -----------------------------------------------------------------------------
 * Lo stretto indispensabile di TCP
 * --------------------------------------------------------------------------- */
static int attendi(unsigned int tipo, unsigned char *buf, unsigned int *len,
                   unsigned int ms)
{
    IpcMessage meta;
    int        i;

    for (i = 0; i < 32; i++) {
        if (ipc_recv_timeout(&meta, buf, IPC_MSG_MAX_DATA, ms) < 0) return -1;
        if ((int)meta.sender_pid != g_pid_ip) continue;
        if (meta.tipo != tipo) continue;
        if (len) *len = meta.len;
        return 0;
    }
    return -1;
}

static int esito(unsigned int ms)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpEsito       e;

    if (attendi(IP_MSG_ESITO, buf, &len, ms) != 0 || len < sizeof(e)) return -1;
    memcpy(&e, buf, sizeof(e));
    return e.codice;
}

static int tcp_apri(const unsigned char *ip, unsigned int porta)
{
    IpTcpApri a;
    int       id = -1, giri;

    memcpy(a.ip, ip, 4);
    a.porta      = porta;
    a.timeout_ms = 8000;

    /* Si ritenta su -EAGAIN: la prima volta l'ARP non ha ancora risposto, e
     * quello non e' un errore ma un «non ancora». */
    for (giri = 0; giri < 10; giri++) {
        if (ipc_send(g_pid_ip, IP_MSG_TCP_APRI, &a, sizeof(a)) < 0) return -1;
        id = esito(10000);
        if (id != -EAGAIN) break;
        usleep(200 * 1000);
    }
    return id;
}

static void tcp_chiudi(int id)
{
    IpTcpRif r;

    if (id <= 0) return;
    r.id = (unsigned int)id;
    ipc_send(g_pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
    esito(2000);
}

static int tcp_leggi(int id, unsigned char *dst, unsigned int max, unsigned int ms)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpTcpRif      r;
    IpTcpDati     d;

    r.id = (unsigned int)id;
    if (ipc_send(g_pid_ip, IP_MSG_TCP_RICEVI, &r, sizeof(r)) < 0) return -1;
    if (attendi(IP_MSG_TCP_DATI, buf, &len, ms) != 0) return -1;
    if (len < sizeof(d)) return -1;

    memcpy(&d, buf, sizeof(d));
    if (d.len == 0) return 0;
    if (d.len > max) d.len = max;
    if (d.len > len - sizeof(d)) d.len = len - (unsigned int)sizeof(d);

    memcpy(dst, buf + sizeof(d), d.len);
    return (int)d.len;
}

static int tcp_scrivi(int id, const unsigned char *src, unsigned int n)
{
    unsigned char msg[sizeof(IpTcpDati) + BLOCCO];
    IpTcpDati     d;
    unsigned int  fatti = 0;

    while (fatti < n) {
        unsigned int q = n - fatti;
        int          rc;

        if (q > BLOCCO) q = BLOCCO;

        d.id  = (unsigned int)id;
        d.len = q;
        memcpy(msg, &d, sizeof(d));
        memcpy(msg + sizeof(d), src + fatti, q);

        if (ipc_send(g_pid_ip, IP_MSG_TCP_INVIA, msg, sizeof(d) + q) < 0) return -1;
        rc = esito(8000);
        if (rc < 0) return rc;
        if (rc == 0) { usleep(50 * 1000); continue; }
        fatti += (unsigned int)rc;
    }
    return (int)fatti;
}

/* -----------------------------------------------------------------------------
 * L'indirizzo: «host[:porta]/percorso»
 * --------------------------------------------------------------------------- */
typedef struct {
    char         host[128];
    unsigned int porta;
    char         percorso[URL_MAX];   /* sempre con lo / davanti */
} Indirizzo;

static void indirizzo_leggi(const char *url, Indirizzo *a)
{
    const char *p = url;
    unsigned int i = 0;

    memset(a, 0, sizeof(*a));
    a->porta = 80;

    if (strncmp(p, "http://", 7) == 0) p += 7;

    while (*p && *p != '/' && *p != ':' && i < sizeof(a->host) - 1)
        a->host[i++] = *p++;
    a->host[i] = '\0';

    if (*p == ':') {
        p++;
        a->porta = 0;
        while (*p >= '0' && *p <= '9') a->porta = a->porta * 10 + (unsigned)(*p++ - '0');
        if (a->porta == 0) a->porta = 80;
    }

    if (*p != '/') a->percorso[0] = '\0';
    else {
        strncpy(a->percorso, p, sizeof(a->percorso) - 1);
        a->percorso[sizeof(a->percorso) - 1] = '\0';
    }

    /* Via la barra in coda: i percorsi si compongono aggiungendola noi. */
    {
        unsigned int n = (unsigned int)strlen(a->percorso);
        while (n > 0 && a->percorso[n - 1] == '/') a->percorso[--n] = '\0';
    }
}

/* =============================================================================
 * Una GET, e nient'altro
 *
 * `su_file` != 0: il corpo va li' dentro, e si calcola l'impronta mentre passa.
 * `in_memoria` != 0: il corpo va in quel buffer, fino a `max`.
 *
 * Rende i byte del corpo, o -1. Il codice HTTP finisce in *codice.
 *
 * ! SI CHIEDE «Connection: close» E SI LEGGE FINCHE' NON CHIUDE. E' la forma
 * piu' povera di HTTP che esista, ed e' quella giusta qui: niente riuso della
 * connessione, niente corpo a pezzi da rimettere insieme, niente stato. Un
 * programma di soccorso deve avere poche cose da sbagliare.
 * ========================================================================== */
static long http_prendi(const Indirizzo *a, const char *coda,
                        int su_file, const char *dove,
                        char *in_memoria, long max,
                        int *codice, char impronta_esa[65])
{
    unsigned char ip[4], buf[BLOCCO];
    char          req[URL_MAX + 256];
    Sha256        sha;
    long          corpo = 0, dichiarata = -1;
    int           id, fd = -1, n, testa_finita = 0;
    unsigned int  acc_n = 0;
    static char   acc[4096];

    *codice = 0;
    if (impronta_esa) impronta_esa[0] = '\0';

    if (dns_risolvi(a->host, ip) != 0) {
        printf("  ! %s non si risolve: la rete non e' pronta?\n", a->host);
        return -1;
    }

    id = tcp_apri(ip, a->porta);
    if (id <= 0) { printf("  ! non mi collego a %s (%d)\n", a->host, id); return -1; }

    snprintf(req, sizeof(req),
             "GET %s%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: EX-OS soccorso\r\n"
             "Accept: */*\r\nConnection: close\r\n\r\n",
             a->percorso, coda, a->host);

    if (tcp_scrivi(id, (const unsigned char *)req, (unsigned int)strlen(req)) < 0) {
        printf("  ! non riesco a mandare la richiesta\n");
        tcp_chiudi(id);
        return -1;
    }

    sha256_avvia(&sha);

    if (su_file) {
        fd = open(dove, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) {
            printf("  ! non riesco ad aprire %s (%s)\n", dove, strerror(errno));
            tcp_chiudi(id);
            return -1;
        }
    }

    for (;;) {
        unsigned char *dati = buf;
        unsigned int   quanti;

        n = tcp_leggi(id, buf, sizeof(buf), 30000);
        if (n < 0) break;
        if (n == 0) break;          /* ha chiuso: e' la fine del corpo */
        quanti = (unsigned int)n;

        if (!testa_finita) {
            unsigned int i;

            if (acc_n + quanti > sizeof(acc)) quanti = (unsigned int)sizeof(acc) - acc_n;
            memcpy(acc + acc_n, buf, quanti);
            acc_n += quanti;

            for (i = 3; i < acc_n; i++) {
                if (acc[i-3] != '\r' || acc[i-2] != '\n' ||
                    acc[i-1] != '\r' || acc[i]   != '\n') continue;

                /* La prima riga: «HTTP/1.1 200 ...» */
                {
                    const char *c = acc;
                    while (*c && *c != ' ') c++;
                    while (*c == ' ') c++;
                    *codice = atoi(c);
                }
                /* Content-Length, se c'e' */
                {
                    unsigned int k;

                    for (k = 0; k + 15 < i; k++)
                        if (strncasecmp(acc + k, "Content-Length:", 15) == 0) {
                            dichiarata = (long)strtoul(acc + k + 15, 0, 10);
                            break;
                        }
                }

                testa_finita = 1;
                dati   = (unsigned char *)acc + i + 1;
                quanti = acc_n - (i + 1);
                break;
            }
            if (!testa_finita) continue;
        }

        if (quanti == 0) continue;

        sha256_dai(&sha, dati, (size_t)quanti);
        corpo += (long)quanti;

        if (su_file) {
            unsigned int scritti = 0;

            while (scritti < quanti) {
                int k = (int)write(fd, dati + scritti, quanti - scritti);

                if (k <= 0) {
                    printf("  ! la scrittura si e' fermata (%s)\n", strerror(errno));
                    close(fd);
                    tcp_chiudi(id);
                    return -1;
                }
                scritti += (unsigned int)k;
            }
        } else if (in_memoria) {
            long spazio = max - (corpo - (long)quanti);

            if (spazio > 0) {
                long q = (long)quanti < spazio ? (long)quanti : spazio;
                memcpy(in_memoria + (corpo - (long)quanti), dati, (size_t)q);
            }
        }
    }

    if (fd >= 0) close(fd);
    tcp_chiudi(id);

    if (impronta_esa) sha256_fine_esa(&sha, impronta_esa);

    /* ! UN CORPO PIU' CORTO DI QUELLO DICHIARATO NON E' UN CORPO: e' una
     * connessione caduta a meta'. Meglio dirlo qui che installare mezza libc. */
    if (dichiarata >= 0 && corpo != dichiarata) {
        printf("  ! arrivati %ld byte su %ld dichiarati: la rete si e' interrotta\n",
               corpo, dichiarata);
        return -1;
    }

    return corpo;
}

/* -----------------------------------------------------------------------------
 * La riga di lib/libc.so dentro elenco.txt
 * --------------------------------------------------------------------------- */
static int elenco_cerca_libc(const Indirizzo *a, long *byte, char impronta[65])
{
    static char elenco[400 * 1024];
    long        n;
    int         codice;
    char       *p;

    printf("  chiedo l'elenco...\n");
    n = http_prendi(a, "/elenco.txt", 0, 0, elenco, (long)sizeof(elenco) - 1,
                    &codice, 0);
    if (n < 0) return -1;
    if (codice != 200) { printf("  ! elenco.txt: il server risponde %d\n", codice); return -1; }
    if (n >= (long)sizeof(elenco) - 1) {
        printf("  ! elenco.txt non ci sta in memoria (%ld byte)\n", n);
        return -1;
    }
    elenco[n] = '\0';

    for (p = elenco; *p; ) {
        char *riga = p, *fine = p;

        while (*fine && *fine != '\n') fine++;
        if (*fine == '\n') *fine++ = '\0';
        p = fine;

        if (strncmp(riga, "lib/libc.so\t", 12) != 0) continue;

        {
            char *sdim = riga + 12, *simp;

            simp = strchr(sdim, '\t');
            if (simp == 0) return -1;
            *simp++ = '\0';

            *byte = (long)strtoul(sdim, 0, 10);
            strncpy(impronta, simp, 64);
            impronta[64] = '\0';
            {
                char *t = strchr(impronta, '\t');
                if (t) *t = '\0';
            }
            return 0;
        }
    }

    printf("  ! nell'elenco non c'e' lib/libc.so\n");
    return -1;
}

/* -----------------------------------------------------------------------------
 * La configurazione dell'aggiornamento, per sapere dove chiedere
 * --------------------------------------------------------------------------- */
static int cnf_url(char *out, unsigned int max)
{
    FILE *f = fopen(CNF, "r");
    char  riga[RIGA_MAX];
    int   trovato = 0;

    if (f == 0) return -1;

    while (fgets(riga, sizeof(riga), f) != 0) {
        char *chiave = riga, *valore, *fine;

        while (*chiave == ' ' || *chiave == '\t') chiave++;
        if (*chiave == '#' || *chiave == '\n' || *chiave == '\0') continue;

        valore = strchr(chiave, '=');
        if (valore == 0) continue;
        *valore++ = '\0';

        fine = chiave + strlen(chiave);
        while (fine > chiave && (fine[-1] == ' ' || fine[-1] == '\t')) *--fine = '\0';
        while (*valore == ' ' || *valore == '\t') valore++;
        fine = valore + strlen(valore);
        while (fine > valore && (fine[-1] == '\n' || fine[-1] == '\r' ||
                                 fine[-1] == ' '  || fine[-1] == '\t')) *--fine = '\0';

        if (strcasecmp(chiave, "url") == 0) {
            strncpy(out, valore, max - 1);
            out[max - 1] = '\0';
            trovato = 1;
        }
    }
    fclose(f);
    return trovato ? 0 : -1;
}

static void uso(void)
{
    printf("soccorso - rimette a posto %s quando non parte piu' niente\n\n", LIBC);
    printf("  soccorso                prende la libc dal server degli aggiornamenti\n");
    printf("  soccorso -url <radice>  da un indirizzo dato a mano\n");
    printf("  soccorso -guarda        dice che cosa farebbe, senza toccare\n\n");
    printf("A che serve: i programmi di EX-OS chiamano la libc per NOME, e un\n");
    printf("programma nuovo con una libc vecchia non parte affatto. Se un\n");
    printf("aggiornamento si interrompe fra i due, ogni comando risponde «la\n");
    printf("libreria condivisa non ha la funzione ...» e non resta niente con\n");
    printf("cui rimediare. Questo programma NON usa la libc condivisa: se la\n");
    printf("porta dentro, e percio' parte comunque.\n");
}

int main(int argc, char **argv)
{
    Indirizzo a;
    char      url[URL_MAX] = "";
    char      atteso[65], avuto[65];
    long      byte = 0, presi;
    int       i, codice;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            uso();
            return 0;
        }
        if (strcmp(argv[i], "-guarda") == 0) { g_guarda = 1; continue; }
        if (strcmp(argv[i], "-url") == 0 && i + 1 < argc) {
            strncpy(url, argv[++i], sizeof(url) - 1);
            continue;
        }
        printf("soccorso: non conosco '%s'. Prova -h.\n", argv[i]);
        return 1;
    }

    printf("soccorso %s\n\n", "0.001");

    if (url[0] == '\0' && cnf_url(url, sizeof(url)) != 0) {
        printf("  Non so da dove prenderla: %s non c'e' o non dice l'url.\n", CNF);
        printf("  Dammelo tu:  soccorso -url esempio.it/exos/netinst\n");
        return 1;
    }

    indirizzo_leggi(url, &a);
    printf("  server: %s:%u%s\n", a.host, a.porta, a.percorso);

    g_pid_ip = rete_richiedi(IP_SERVIZIO);
    if (g_pid_ip <= 0) {
        printf("\n  ! lo stack IP non risponde. Prima serve la rete:\n");
        printf("    netdetect -c ; /dev/ip.drv & ; dhcp\n");
        printf("    (e se anche quelli non partono, e' un caso da CD.)\n");
        return 1;
    }

    if (elenco_cerca_libc(&a, &byte, atteso) != 0) return 1;
    printf("  la libc buona: %ld byte\n", byte);

    if (g_guarda) {
        printf("\n  -guarda: mi fermo qui senza toccare niente.\n");
        return 0;
    }

    printf("  la scarico...\n");
    presi = http_prendi(&a, "/file/lib/libc.so", 1, LIBC_NEW, 0, 0, &codice, avuto);
    if (presi < 0) return 1;
    if (codice != 200) {
        printf("  ! il server risponde %d\n", codice);
        remove(LIBC_NEW);
        return 1;
    }

    if (presi != byte) {
        printf("  ! %ld byte invece di %ld: NON la installo\n", presi, byte);
        remove(LIBC_NEW);
        return 1;
    }
    if (strcmp(avuto, atteso) != 0) {
        printf("  ! l'impronta non torna: NON la installo\n");
        printf("    attesa %s\n    avuta  %s\n", atteso, avuto);
        remove(LIBC_NEW);
        return 1;
    }

    /* ! LA VECCHIA SI TIENE DA PARTE, non si cancella. Se la nuova avesse
     * qualcosa che non va, l'unica via di ritorno e' quel file. */
    remove(LIBC_PRIMA);
    if (rename(LIBC, LIBC_PRIMA) != 0 && errno != ENOENT)
        printf("  ! non riesco a mettere da parte la vecchia (%s)\n", strerror(errno));

    if (rename(LIBC_NEW, LIBC) != 0) {
        printf("  ! non riesco a metterla al suo posto (%s)\n", strerror(errno));
        rename(LIBC_PRIMA, LIBC);        /* si rimette com'era */
        return 1;
    }

    printf("\n  FATTO: %s e' quella giusta (%ld byte).\n", LIBC, byte);
    printf("  La vecchia e' in %s.\n\n", LIBC_PRIMA);
    printf("  Adesso i comandi ripartono. Se l'aggiornamento si era\n");
    printf("  interrotto, finiscilo:  netupdate -check\n");
    return 0;
}

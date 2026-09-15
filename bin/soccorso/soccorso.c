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
 * soccorso — rimette a posto le librerie condivise quando non parte piu niente
 *
 *     soccorso                 le prende dal server dell'aggiornamento
 *     soccorso -url <radice>   da un indirizzo dato a mano
 *     soccorso -guarda         dice solo quali rifarebbe, e non tocca niente
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
 * ! L'IMPRONTA SI CONTROLLA LO STESSO. Una libreria sbagliata e' peggio di una
 * vecchia: si prende `elenco.txt`, si cercano le righe che finiscono per `.so`,
 * e si confrontano dimensione e SHA-256 mentre il file arriva. Se non tornano,
 * il .new si butta e non si tocca niente.
 *
 * ! E NON SOLO LA libc: TUTTE LE LIBRERIE CONDIVISE. Il 15 settembre 2026 la
 * stessa macchina si e' rotta due volte nello stesso modo — prima per
 * lib/libc.so, poi per exwin/lib/exhttp.so. La seconda ha ucciso proprio
 * netupdate, che quella libreria la usa: «exhttp: la libreria condivisa non
 * esporta un nome che serve a questo programma», e da li' la macchina non
 * poteva piu' aggiornarsi da sola. Riparare solo la libc avrebbe lasciato in
 * piedi meta' del guaio.
 *
 * ! E SI GUARDANO I BYTE, NON IL REGISTRO. Una libreria si controlla
 * ricalcolando la sua impronta sul file che sta sul disco: il registro degli
 * aggiornamenti dice quel che la macchina CREDE di avere, ed e' esattamente
 * cio' che in questo caso non si puo' credere.
 * ============================================================================= */
#include "libc.h"
#include "ip_proto.h"
#include "rete.h"
#include "dns.h"

/* +0.001 a ogni modifica: `soccorso -version` la stampa. Vedi EX_VERSIONE. */
EX_VERSIONE("soccorso", "0.002");

#define CNF        "/boot/netupdate.cnf"
#define LIBC       "/lib/libc.so"
#define LIBC_NEW   "/lib/libc.so.new"
#define LIBC_PRIMA "/lib/libc.so.prima"

#define URL_MAX    256
#define RIGA_MAX   512
#define BLOCCO     1024                  /* quanto si scrive per volta */
#define LETTURA    IPC_MSG_MAX_DATA      /* quanto puo' arrivare in una consegna */

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
    /* =========================================================================
     * ! IL BUFFER DI CHI LEGGE DEV'ESSERE GRANDE QUANTO UNA CONSEGNA INTERA,
     * E IL 15 SETTEMBRE 2026 NON LO ERA.
     *
     * Lo stack consegna fino a IP_TCP_DATI_MAX byte in un colpo (1520: un
     * messaggio IPC meno l'intestazione). Con un buffer da 1024 la riga qui
     * sotto tagliava il resto e non lo diceva a nessuno: i byte in piu' non
     * finivano da nessuna parte e la prenotazione era gia' stata consumata.
     *
     * ! IL SINTOMO NON SOMIGLIAVA A UNA PERDITA DI DATI: `soccorso` elencava
     * SETTE librerie invece di dodici, e le cinque mancanti erano CONSECUTIVE —
     * cioe' mezzo kilobyte di elenco sparito in mezzo. Sembrava un difetto del
     * riconoscimento dei nomi, e invece era un buco nel testo.
     *
     * Adesso chi chiama passa un buffer da IPC_MSG_MAX_DATA e questa riga non
     * scatta mai; resta come rete, perche' un buffer piccolo non deve
     * diventare una corruzione silenziosa.
     * ========================================================================= */
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
    unsigned char ip[4], buf[LETTURA];
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

/* L'impronta di un file che sta sul disco, letto a pezzi: la SHA-256
 * incrementale non ha bisogno di tenerlo in memoria. Rende 0, o -1 se il file
 * non c'e' o non si e' potuto leggere tutto. */
static int impronta_locale(const char *perc, char esa[65])
{
    static unsigned char pezzo[BLOCCO];
    Sha256 sha;
    int    fd, n;

    fd = open(perc, O_RDONLY);
    if (fd < 0) return -1;

    sha256_avvia(&sha);
    while ((n = (int)read(fd, pezzo, sizeof(pezzo))) > 0)
        sha256_dai(&sha, pezzo, (size_t)n);
    close(fd);

    if (n < 0) return -1;
    sha256_fine_esa(&sha, esa);
    return 0;
}

/* -----------------------------------------------------------------------------
 * Le righe delle librerie dentro elenco.txt
 * --------------------------------------------------------------------------- */
/* =============================================================================
 * LE LIBRERIE, UNA PER UNA
 *
 * Si scarica `elenco.txt`, si guardano le righe che finiscono per `.so`, e per
 * ognuna si RICALCOLA l'impronta del file che sta sul disco. Quelle che non
 * combaciano si riportano a posto.
 *
 * ! SI GUARDANO I BYTE E NON IL REGISTRO, per la ragione scritta in testa: il
 * registro dice quel che la macchina crede di avere, e qui si e' arrivati
 * proprio perche' quella convinzione era sbagliata.
 * ========================================================================== */
static int ripara_librerie(const Indirizzo *a)
{
    static char elenco[400 * 1024];
    long        n;
    int         codice, guardate = 0, riparate = 0, guai = 0;
    char       *p;

    printf("  chiedo l'elenco...\n");
    n = http_prendi(a, "/elenco.txt", 0, 0, elenco, (long)sizeof(elenco) - 1,
                    &codice, 0);
    if (n < 0) return -1;
    if (codice != 200) {
        printf("  ! elenco.txt: il server risponde %d\n", codice);
        return -1;
    }
    if (n >= (long)sizeof(elenco) - 1) {
        printf("  ! elenco.txt non ci sta in memoria (%ld byte)\n", n);
        return -1;
    }
    elenco[n] = '\0';

    for (p = elenco; *p; ) {
        char *riga = p, *fine = p, *sdim, *simp, *t;
        char  assoluto[URL_MAX], coda[URL_MAX], nuovo[URL_MAX], prima[URL_MAX];
        char  esa[65], avuto[65];
        long  byte, presi;
        unsigned int lung;

        while (*fine && *fine != '\n') fine++;
        if (*fine == '\n') *fine++ = '\0';
        p = fine;

        /* percorso<TAB>byte<TAB>impronta<TAB>pacchetto */
        sdim = strchr(riga, '\t');
        if (sdim == 0) continue;
        *sdim++ = '\0';

        lung = (unsigned int)strlen(riga);
        if (lung < 4 || strcmp(riga + lung - 3, ".so") != 0) continue;

        simp = strchr(sdim, '\t');
        if (simp == 0) continue;
        *simp++ = '\0';
        t = strchr(simp, '\t');
        if (t) *t = '\0';

        byte = (long)strtoul(sdim, 0, 10);
        guardate++;

        snprintf(assoluto, sizeof(assoluto), "/%s", riga);

        if (impronta_locale(assoluto, esa) == 0 && strcmp(esa, simp) == 0) {
            printf("    %-28s a posto\n", assoluto);
            continue;
        }

        printf("    %-28s DA RIFARE (%ld byte)\n", assoluto, byte);
        if (g_guarda) continue;

        snprintf(coda,  sizeof(coda),  "/file/%s", riga);
        snprintf(nuovo, sizeof(nuovo), "%s.new", assoluto);
        snprintf(prima, sizeof(prima), "%s.prima", assoluto);

        presi = http_prendi(a, coda, 1, nuovo, 0, 0, &codice, avuto);
        if (presi < 0 || codice != 200) {
            printf("      ! non arrivata (codice %d)\n", codice);
            remove(nuovo);
            guai++;
            continue;
        }
        if (presi != byte || strcmp(avuto, simp) != 0) {
            printf("      ! %ld byte su %ld, o impronta diversa: NON la installo\n",
                   presi, byte);
            remove(nuovo);
            guai++;
            continue;
        }

        /* ! LA VECCHIA SI TIENE DA PARTE, non si cancella: se la nuova avesse
         * qualcosa che non va, l'unica via di ritorno e' quel file. */
        remove(prima);
        if (rename(assoluto, prima) != 0 && errno != ENOENT)
            printf("      ! non riesco a mettere da parte la vecchia (%s)\n",
                   strerror(errno));

        if (rename(nuovo, assoluto) != 0) {
            printf("      ! non riesco a metterla al suo posto (%s)\n",
                   strerror(errno));
            rename(prima, assoluto);
            guai++;
            continue;
        }

        printf("      rimessa a posto, la vecchia e' in %s\n", prima);
        riparate++;
    }

    printf("\n  %d librerie guardate, %d rifatte", guardate, riparate);
    if (guai) printf(", %d non riuscite", guai);
    printf(".\n");

    if (guardate == 0) {
        printf("  ! nell'elenco non c'e' nessuna libreria: indirizzo giusto?\n");
        return -1;
    }
    return guai ? -1 : riparate;
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
    printf("soccorso - rimette a posto le librerie condivise quando non\n");
    printf("           parte piu' niente\n\n");
    printf("  soccorso                le controlla tutte e rifa' quelle sbagliate\n");
    printf("  soccorso -url <radice>  da un indirizzo dato a mano\n");
    printf("  soccorso -guarda        dice quali rifarebbe, senza toccare\n\n");
    printf("A che serve: i programmi di EX-OS chiamano le librerie per NOME, e\n");
    printf("un programma nuovo con una libreria vecchia non parte affatto. Se\n");
    printf("un aggiornamento si interrompe fra i due, i comandi rispondono «la\n");
    printf("libreria condivisa non ha la funzione ...» oppure «non esporta un\n");
    printf("nome che serve a questo programma», e non resta niente con cui\n");
    printf("rimediare. Questo programma NON usa nessuna libreria condivisa: se\n");
    printf("le porta dentro, e percio' parte comunque.\n\n");
    printf("Guarda i BYTE dei file, non il registro degli aggiornamenti: qui si\n");
    printf("arriva proprio quando quel registro dice il falso.\n");
}

int main(int argc, char **argv)
{
    Indirizzo a;
    char      url[URL_MAX] = "";
    int       i;

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

    printf("soccorso %s\n\n", "0.002");

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

    {
        int rc = ripara_librerie(&a);

        if (rc < 0) return 1;

        if (g_guarda) {
            printf("\n  -guarda: mi sono fermato prima di toccare qualunque cosa.\n");
            return 0;
        }

        if (rc == 0) {
            printf("\n  Le librerie erano gia' tutte a posto: il guasto e'\n");
            printf("  da un'altra parte. Se un comando non parte, guarda che\n");
            printf("  cosa dice esattamente.\n");
            return 0;
        }

        printf("\n  FATTO. Adesso i comandi ripartono.\n");
        printf("  Se l'aggiornamento si era interrotto, finiscilo:\n");
        printf("      netupdate -check\n");
    }
    return 0;
}

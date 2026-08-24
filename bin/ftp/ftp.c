/* =============================================================================
 * bin/ftp/ftp.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Client FTP.
 *
 *   ftp server                      entra e apre la riga di comando
 *   ftp server -u tizio -w segreto
 *   ftp server ls                   un comando solo, poi esce
 *   ftp server get remoto.txt
 *
 * Comandi: ls, cd, pwd, get, put, bye.
 *
 * -----------------------------------------------------------------------------
 * ! SOLO MODO PASSIVO (PASV), E NON E' UNA SEMPLIFICAZIONE
 *
 * FTP ha due modi di aprire la connessione dati. In modo ATTIVO e' il
 * SERVER a ricollegarsi al client, che quindi deve mettersi in ascolto —
 * e il TCP di EX-OS non sa farlo (vedi drivers/net/ip_proto.h: manca il
 * ramo LISTEN, di proposito). In modo PASSIVO e' il client ad aprire
 * anche la seconda connessione, ed e' l'unica cosa che sappiamo fare.
 *
 * Non e' un ripiego: il modo attivo non funziona comunque dietro un NAT,
 * ed e' per questo che ogni client serio usa PASV da vent'anni.
 *
 * -----------------------------------------------------------------------------
 * ! FTP MANDA LA PASSWORD IN CHIARO
 *
 * Non e' un difetto di questo programma: e' il protocollo. Chiunque stia
 * sul percorso legge utente e password cosi' come sono. Va usato su una
 * rete di cui ci si fida, o non va usato — e quando ci sara' TLS,
 * l'alternativa si chiamera' SFTP o FTPS, non "ftp con una toppa".
 *
 * Il programma lo dice all'accesso, una volta, invece di lasciarlo
 * intendere.
 *
 * -----------------------------------------------------------------------------
 * ! LE RISPOSTE FTP POSSONO ESSERE SU PIU' RIGHE
 *
 * "220-Benvenuto" seguito da altre righe e chiuso da "220 Pronto". La
 * regola e' il TRATTINO dopo il codice: finche' c'e', la risposta
 * continua. Leggere solo la prima riga significa trovarsi le righe
 * successive in testa alla risposta del comando DOPO, e da li' in avanti
 * ogni codice letto e' quello sbagliato.
 * ============================================================================= */

#include "libc.h"
#include "ip_proto.h"
#include "dns.h"
#include "rete.h"

/* +0.001 a ogni modifica: `ftp -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("ftp", "0.001");

#define BUF_CTRL   4096
#define BUF_DATI   2048

static int ip_uguali_4(const unsigned char *a, const unsigned char *b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static int pid_ip = 0;
static int ctrl   = 0;          /* id della connessione di controllo */

/* Accumulatore della connessione di controllo: le risposte arrivano a
 * pezzi che non coincidono con le righe. */
static char         g_acc[BUF_CTRL];
static unsigned int g_acc_len = 0;

/* =============================================================================
 * Dialogo con lo stack
 * ============================================================================= */
static int attendi(unsigned int tipo, unsigned char *buf, unsigned int *len,
                   unsigned int ms)
{
    IpcMessage meta;
    int        i;

    for (i = 0; i < 64; i++) {
        if (ipc_recv_timeout(&meta, buf, IPC_MSG_MAX_DATA, ms) < 0) return -1;
        if ((int)meta.sender_pid != pid_ip) continue;
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

/* Apre una connessione, ritentando su -EAGAIN (l'ARP non c'e' ancora). */
static int apri(const unsigned char *ip, unsigned int porta)
{
    IpTcpApri a;
    int       id = -1, giri;

    memcpy(a.ip, ip, 4);
    a.porta      = porta;
    a.timeout_ms = 8000;

    for (giri = 0; giri < 10; giri++) {
        if (ipc_send(pid_ip, IP_MSG_TCP_APRI, &a, sizeof(a)) < 0) return -1;
        id = esito(10000);
        if (id != -EAGAIN) break;
        usleep(200 * 1000);
    }
    return id;
}

static void chiudi(int id)
{
    IpTcpRif r;

    if (id <= 0) return;
    r.id = (unsigned int)id;
    ipc_send(pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
    esito(2000);
}

/* Legge un blocco da una connessione. Ritorna i byte letti, 0 se l'altro
 * ha chiuso, <0 su errore. */
static int leggi(int id, unsigned char *dst, unsigned int max, unsigned int ms)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpTcpRif      r;
    IpTcpDati     d;

    r.id = (unsigned int)id;
    if (ipc_send(pid_ip, IP_MSG_TCP_RICEVI, &r, sizeof(r)) < 0) return -1;
    if (attendi(IP_MSG_TCP_DATI, buf, &len, ms) != 0) return -1;
    if (len < sizeof(d)) return -1;

    memcpy(&d, buf, sizeof(d));
    if (d.len == 0) return 0;
    if (d.len > max) d.len = max;

    memcpy(dst, buf + sizeof(d), d.len);
    return (int)d.len;
}

static int scrivi(int id, const unsigned char *src, unsigned int n)
{
    unsigned char msg[sizeof(IpTcpDati) + 1024];
    IpTcpDati     d;
    unsigned int  fatti = 0;

    while (fatti < n) {
        unsigned int q = n - fatti;
        int          rc;

        if (q > 1024u) q = 1024u;

        d.id = (unsigned int)id;
        d.len = q;
        memcpy(msg, &d, sizeof(d));
        memcpy(msg + sizeof(d), src + fatti, q);

        if (ipc_send(pid_ip, IP_MSG_TCP_INVIA, msg, sizeof(d) + q) < 0) return -1;
        rc = esito(8000);
        if (rc < 0) return rc;

        /* ! LO STACK PUO' ACCETTARNE MENO DI QUANTI GLIENE OFFRIAMO
         * (buffer quasi pieno): il valore restituito e' quanti ne sono
         * entrati davvero. Andare avanti come se li avesse presi tutti
         * significherebbe saltare dei byte in mezzo al file. */
        if (rc == 0) { usleep(50 * 1000); continue; }
        fatti += (unsigned int)rc;
    }
    return (int)fatti;
}

/* =============================================================================
 * Righe e risposte della connessione di controllo
 * ============================================================================= */

/* Estrae una riga dall'accumulatore. 1 se c'era, 0 se serve leggere. */
static int riga_pronta(char *out, unsigned int max)
{
    unsigned int i, n;

    for (i = 0; i < g_acc_len; i++) {
        if (g_acc[i] != '\n') continue;

        n = i;
        if (n > 0 && g_acc[n - 1] == '\r') n--;
        if (n > max - 1) n = max - 1;

        memcpy(out, g_acc, n);
        out[n] = '\0';

        memmove(g_acc, g_acc + i + 1, g_acc_len - i - 1);
        g_acc_len -= i + 1;
        return 1;
    }
    return 0;
}

static int riga_ctrl(char *out, unsigned int max, unsigned int ms)
{
    while (!riga_pronta(out, max)) {
        int n;

        if (g_acc_len >= sizeof(g_acc)) return -1;   /* riga assurda */
        n = leggi(ctrl, (unsigned char *)g_acc + g_acc_len,
                  (unsigned int)(sizeof(g_acc) - g_acc_len), ms);
        if (n <= 0) return -1;
        g_acc_len += (unsigned int)n;
    }
    return 0;
}

/* Legge una risposta completa, righe di continuazione comprese.
 * Ritorna il codice numerico, <0 se non arriva. */
static int risposta(int mostra)
{
    char riga[512];
    char codice[4];
    int  primo = 1, n;

    codice[0] = '\0';

    for (;;) {
        if (riga_ctrl(riga, sizeof(riga), 10000) != 0) return -1;
        if (mostra) printf("%s\n", riga);

        n = (int)strlen(riga);
        if (n < 4) continue;
        if (riga[0] < '0' || riga[0] > '9') continue;   /* riga di testo */

        if (primo) {
            codice[0] = riga[0]; codice[1] = riga[1];
            codice[2] = riga[2]; codice[3] = '\0';
            primo = 0;
            /* ! IL TRATTINO DICE CHE CONTINUA. Senza questo controllo la
             * prima riga di un banner multiriga verrebbe presa per la
             * risposta intera, e tutto il resto finirebbe in testa alla
             * risposta del comando successivo. */
            if (riga[3] == '-') continue;
            break;
        }

        /* Fine: stesso codice seguito da uno spazio. */
        if (riga[0] == codice[0] && riga[1] == codice[1] &&
            riga[2] == codice[2] && riga[3] == ' ') break;
    }

    return (codice[0] - '0') * 100 + (codice[1] - '0') * 10 + (codice[2] - '0');
}

static int comando(const char *cmd, const char *arg, int mostra)
{
    char linea[600];
    unsigned int n = 0, i;

    for (i = 0; cmd[i] && n < sizeof(linea) - 4; i++) linea[n++] = cmd[i];
    if (arg && arg[0]) {
        linea[n++] = ' ';
        for (i = 0; arg[i] && n < sizeof(linea) - 3; i++) linea[n++] = arg[i];
    }
    linea[n++] = '\r';
    linea[n++] = '\n';

    if (scrivi(ctrl, (const unsigned char *)linea, n) < 0) return -1;
    return risposta(mostra);
}

/* =============================================================================
 * PASV
 *
 * La risposta e' "227 <testo> (h1,h2,h3,h4,p1,p2)". Si cercano sei numeri
 * dentro le parentesi: i primi quattro sono l'indirizzo, gli ultimi due la
 * porta in due byte (p1*256 + p2).
 *
 * ! SI CERCANO I NUMERI, NON SI SEGUE UN FORMATO. Il testo fra il codice
 * e la parentesi cambia da server a server ("Entering Passive Mode",
 * "=", tradotto in un'altra lingua): affidarsi alle parole vorrebbe dire
 * funzionare con un server e non con il successivo.
 * ============================================================================= */
static int leggi_pasv(const char *riga, unsigned char *ip, unsigned int *porta)
{
    const char *p = riga;
    unsigned int v[6];
    int          i;

    while (*p && *p != '(') p++;
    if (*p != '(') return 0;
    p++;

    for (i = 0; i < 6; i++) {
        unsigned int x = 0;
        int          cifre = 0;

        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { x = x * 10u + (unsigned)(*p - '0'); p++; cifre++; }
        if (cifre == 0 || x > 255u) return 0;
        v[i] = x;
        if (i < 5) {
            while (*p == ' ') p++;
            if (*p != ',') return 0;
            p++;
        }
    }

    ip[0] = (unsigned char)v[0]; ip[1] = (unsigned char)v[1];
    ip[2] = (unsigned char)v[2]; ip[3] = (unsigned char)v[3];
    *porta = v[4] * 256u + v[5];
    return 1;
}

/* Apre la connessione dati. Ritorna l'id, <0 in caso di errore.
 *
 * ! SI USA L'INDIRIZZO DEL SERVER DI CONTROLLO, NON QUELLO ANNUNCIATO
 * DAL PASV, quando i due differiscono. Un server dietro NAT annuncia
 * spesso il proprio indirizzo PRIVATO, che dal nostro lato non e'
 * raggiungibile: la porta e' l'informazione utile, l'indirizzo lo
 * sappiamo gia'. */
static int apri_dati(const unsigned char *ip_server)
{
    char          riga[512];
    unsigned char ip[4];
    unsigned int  porta;
    int           codice, id;

    /* La risposta al PASV va letta riga per riga: ci serve il TESTO, non
     * solo il codice. */
    {
        char linea[16];
        unsigned int n = 0;
        const char  *c = "PASV\r\n";

        while (c[n]) { linea[n] = c[n]; n++; }
        if (scrivi(ctrl, (const unsigned char *)linea, n) < 0) return -1;
    }

    if (riga_ctrl(riga, sizeof(riga), 10000) != 0) return -1;
    codice = (riga[0] - '0') * 100 + (riga[1] - '0') * 10 + (riga[2] - '0');
    if (codice != 227) {
        printf("%s\n", riga);
        return -1;
    }

    if (!leggi_pasv(riga, ip, &porta)) {
        printf("ftp: risposta PASV incomprensibile:\n  %s\n", riga);
        return -1;
    }

    if (!ip_uguali_4(ip, ip_server)) {
        printf("  (il server annuncia %u.%u.%u.%u: uso il suo indirizzo vero)\n",
               ip[0], ip[1], ip[2], ip[3]);
        memcpy(ip, ip_server, 4);
    }

    id = apri(ip, porta);
    if (id <= 0) printf("ftp: non riesco ad aprire la connessione dati (%d)\n", id);
    return id;
}

/* =============================================================================
 * Comandi
 * ============================================================================= */
static unsigned char g_ip_server[4];

/* ! UN CODICE 4xx O 5xx E' UN RIFIUTO, e va trattato come tale. La prima
 * versione controllava solo che comando() non tornasse un errore di
 * TRASPORTO: su un "550 file inesistente" proseguiva, apriva il file
 * locale, non leggeva niente e annunciava «0 byte» — cioe' dichiarava
 * riuscito un trasferimento mai avvenuto, lasciando in giro un file vuoto
 * al posto di quello vero. */
static int rifiutato(int codice)
{
    return codice < 0 || codice >= 400;
}

static int cmd_lista(const char *arg)
{
    unsigned char blocco[BUF_DATI];
    int           dati, n, totale = 0;

    dati = apri_dati(g_ip_server);
    if (dati <= 0) return 1;

    if (rifiutato(comando("LIST", arg, 0))) { chiudi(dati); return 1; }

    while ((n = leggi(dati, blocco, sizeof(blocco), 10000)) > 0) {
        write(1, blocco, (unsigned int)n);
        totale += n;
    }
    chiudi(dati);

    risposta(0);        /* il "226 completato" della connessione di controllo */
    if (totale == 0) printf("(directory vuota)\n");
    return 0;
}

static int cmd_get(const char *remoto, const char *locale)
{
    unsigned char blocco[BUF_DATI];
    int           dati, n, fd, totale = 0;

    if (locale == NULL || locale[0] == '\0') locale = remoto;

    dati = apri_dati(g_ip_server);
    if (dati <= 0) return 1;

    if (rifiutato(comando("RETR", remoto, 1))) { chiudi(dati); return 1; }

    fd = open(locale, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf("ftp: non riesco a creare '%s'\n", locale);
        chiudi(dati);
        return 1;
    }

    while ((n = leggi(dati, blocco, sizeof(blocco), 15000)) > 0) {
        if (write(fd, blocco, (unsigned int)n) != n) {
            printf("ftp: scrittura fallita su '%s'\n", locale);
            break;
        }
        totale += n;
    }

    close(fd);
    chiudi(dati);
    risposta(1);

    printf("%d byte in '%s'\n", totale, locale);
    return 0;
}

static int cmd_put(const char *locale, const char *remoto)
{
    unsigned char blocco[BUF_DATI];
    int           dati, fd, n, totale = 0;

    if (remoto == NULL || remoto[0] == '\0') remoto = locale;

    fd = open(locale, O_RDONLY);
    if (fd < 0) { printf("ftp: '%s' non si apre\n", locale); return 1; }

    dati = apri_dati(g_ip_server);
    if (dati <= 0) { close(fd); return 1; }

    if (rifiutato(comando("STOR", remoto, 1))) { close(fd); chiudi(dati); return 1; }

    while ((n = (int)read(fd, blocco, sizeof(blocco))) > 0) {
        if (scrivi(dati, blocco, (unsigned int)n) < 0) {
            printf("ftp: invio interrotto\n");
            break;
        }
        totale += n;
    }

    close(fd);
    chiudi(dati);
    risposta(1);

    printf("%d byte mandati come '%s'\n", totale, remoto);
    return 0;
}

/* =============================================================================
 * Riga di comando interattiva
 * ============================================================================= */
static void aiuto(void)
{
    printf("  ls [percorso]      elenca\n");
    printf("  cd percorso        cambia directory sul server\n");
    printf("  pwd                dove siamo sul server\n");
    printf("  get remoto [loc]   scarica\n");
    printf("  put locale [rem]   invia\n");
    printf("  mkdir directory    crea una directory sul server\n");
    printf("  rmdir directory    la toglie (deve essere vuota)\n");
    printf("  delete file        cancella un file sul server\n");
    printf("  rename vec nuovo   rinomina\n");
    printf("  binary | ascii     modo di trasferimento (parte in binario)\n");
    printf("  size file          quanto e' grande\n");
    printf("  syst               che sistema e'\n");
    printf("  bye                chiude ed esce\n");
}

/* Spezza una riga in al massimo tre pezzi. Le virgolette le gestisce la
 * shell per gli argomenti del comando, non qui: dentro questa riga di
 * comando i nomi con spazi si passano interi come ultimo argomento. */
static int pezzi(char *riga, char *out[], int max)
{
    int n = 0;
    char *p = riga;

    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        out[n++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

/* =============================================================================
 * I comandi che cambiano qualcosa sul server
 *
 * ! SI GUARDA IL CODICE DI RISPOSTA, non si stampa e basta. Un server che
 * rifiuta MKD risponde con un 5xx e una riga di testo: senza questo
 * controllo il programma direbbe «fatto» a un comando fallito, e chi lo
 * usa in uno script non avrebbe modo di accorgersene. La regola FTP e'
 * semplice — 2xx e' riuscito, 3xx vuole un seguito, 4xx e 5xx sono
 * fallimenti — e vale per tutti allo stesso modo.
 * ============================================================================= */
static int riuscito(const char *cosa, int codice)
{
    if (codice < 0) {
        printf("ftp: %s: il server non risponde\n", cosa);
        return -1;
    }
    if (codice >= 200 && codice < 300) return 0;

    /* Il testo del server e' gia' stato stampato da risposta(): qui si
     * aggiunge solo la riga che dice CHE COSA non e' riuscito. */
    printf("ftp: %s non riuscito (%d)\n", cosa, codice);
    return -1;
}

/* RENAME e' l'unico in due tempi: RNFR nomina l'originale e il server
 * risponde 350 «e adesso dimmi il nuovo»; RNTO lo completa.
 *
 * ! SE RNFR NON DA' 350 NON SI MANDA RNTO. Mandarlo lo stesso lascerebbe
 * il server con un rinomino a meta' — e su alcuni server il RNTO
 * successivo si attaccherebbe a un RNFR di prima, rinominando un file che
 * non c'entra. */
static int cmd_rinomina(const char *da, const char *a)
{
    int c = comando("RNFR", da, 1);

    if (c < 300 || c >= 400) return riuscito("rename", c);
    return riuscito("rename", comando("RNTO", a, 1));
}

/* ! IL TIPO DI TRASFERIMENTO E' UNO STATO DEL SERVER, non un'opzione del
 * comando: vale finche' non lo si cambia. Il programma parte in binario
 * (vedi TYPE I dopo il login), che e' l'unica scelta sensata come
 * predefinita — in ASCII il server converte i fine riga, e un eseguibile
 * scaricato cosi' arriva corrotto in modo silenzioso.
 *
 * `ascii` serve per i file di testo scambiati con sistemi che hanno una
 * convenzione di riga diversa dalla nostra; per tutto il resto, binario. */
static int cmd_tipo(const char *tipo, const char *nome)
{
    if (riuscito("type", comando("TYPE", tipo, 0)) != 0) return -1;
    printf("modo di trasferimento: %s\n", nome);
    return 0;
}

static int esegui(int n, char *a[])
{
    if (n == 0) return 0;

    if (strcmp(a[0], "ls") == 0)   return cmd_lista(n > 1 ? a[1] : "");
    if (strcmp(a[0], "cd") == 0)   { comando("CWD", n > 1 ? a[1] : "/", 1); return 0; }
    if (strcmp(a[0], "pwd") == 0)  { comando("PWD", "", 1); return 0; }
    if (strcmp(a[0], "get") == 0) {
        if (n < 2) { printf("uso: get remoto [locale]\n"); return 0; }
        return cmd_get(a[1], n > 2 ? a[2] : NULL);
    }
    if (strcmp(a[0], "put") == 0) {
        if (n < 2) { printf("uso: put locale [remoto]\n"); return 0; }
        return cmd_put(a[1], n > 2 ? a[2] : NULL);
    }
    if (strcmp(a[0], "mkdir") == 0) {
        if (n < 2) { printf("uso: mkdir directory\n"); return 0; }
        riuscito("mkdir", comando("MKD", a[1], 1));
        return 0;
    }
    if (strcmp(a[0], "rmdir") == 0) {
        if (n < 2) { printf("uso: rmdir directory\n"); return 0; }
        riuscito("rmdir", comando("RMD", a[1], 1));
        return 0;
    }
    /* `delete` e `del`: il primo e' il nome storico dei client FTP, il
     * secondo quello che viene in mente a chi arriva da EX-OS, dove il
     * comando si chiama cosi'. */
    if (strcmp(a[0], "delete") == 0 || strcmp(a[0], "del") == 0) {
        if (n < 2) { printf("uso: delete file\n"); return 0; }
        riuscito("delete", comando("DELE", a[1], 1));
        return 0;
    }
    if (strcmp(a[0], "rename") == 0) {
        if (n < 3) { printf("uso: rename vecchio nuovo\n"); return 0; }
        cmd_rinomina(a[1], a[2]);
        return 0;
    }
    if (strcmp(a[0], "binary") == 0 || strcmp(a[0], "bin") == 0) {
        cmd_tipo("I", "binario");
        return 0;
    }
    if (strcmp(a[0], "ascii") == 0) {
        cmd_tipo("A", "testo (ASCII)");
        return 0;
    }
    if (strcmp(a[0], "size") == 0) {
        if (n < 2) { printf("uso: size file\n"); return 0; }
        riuscito("size", comando("SIZE", a[1], 1));
        return 0;
    }
    if (strcmp(a[0], "syst") == 0)  { comando("SYST", "", 1); return 0; }
    if (strcmp(a[0], "noop") == 0)  { comando("NOOP", "", 1); return 0; }
    if (strcmp(a[0], "help") == 0 || strcmp(a[0], "?") == 0) { aiuto(); return 0; }
    if (strcmp(a[0], "bye") == 0 || strcmp(a[0], "quit") == 0) return -1;

    printf("ftp: '%s' non lo conosco. `help` li elenca.\n", a[0]);
    return 0;
}

int main(int argc, char **argv)
{
    char          riga[512];
    char         *a[4];
    const char   *utente = "anonymous";
    const char   *chiave = "exos@";
    const char   *porta_s = NULL;
    int           i, primo_cmd = 0, rc;
    unsigned int  porta = 21;

    if (argc < 2) {
        printf("uso: ftp SERVER [-u utente] [-w password] [-p porta] [comando ...]\n\n");
        aiuto();
        printf("\nSenza comando si apre una riga di comando.\n");
        printf("Senza -u si entra come 'anonymous'.\n\n");
        printf("ATTENZIONE: FTP manda la password IN CHIARO. Non e' un\n");
        printf("difetto di questo programma, e' il protocollo.\n");
        return 1;
    }

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0 && i + 1 < argc)      utente = argv[++i];
        else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) chiave = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) porta_s = argv[++i];
        else { primo_cmd = i; break; }
    }
    if (porta_s) porta = (unsigned int)atoi(porta_s);

    pid_ip = rete_richiedi(IP_SERVIZIO);
    if (pid_ip <= 0) return 1;

    if (dns_risolvi(argv[1], g_ip_server) != 0) {
        printf("ftp: non riesco a risolvere '%s'\n", argv[1]);
        return 1;
    }

    printf("connessione a %u.%u.%u.%u:%u ...\n",
           g_ip_server[0], g_ip_server[1], g_ip_server[2], g_ip_server[3], porta);

    ctrl = apri(g_ip_server, porta);
    if (ctrl <= 0) {
        printf("ftp: connessione fallita (%d)\n", ctrl);
        return 1;
    }

    if (risposta(1) < 0) { printf("ftp: il server non saluta\n"); return 1; }

    printf("\nATTENZIONE: la password viaggia IN CHIARO su questa rete.\n\n");

    if (comando("USER", utente, 1) < 0) return 1;
    comando("PASS", chiave, 1);
    comando("TYPE", "I", 0);        /* binario: un file non e' testo */

    if (primo_cmd > 0) {
        int n = 0;

        for (i = primo_cmd; i < argc && n < 3; i++) a[n++] = argv[i];
        rc = esegui(n, a);
        comando("QUIT", "", 0);
        chiudi(ctrl);
        return (rc < 0) ? 0 : rc;
    }

    printf("\n`help` elenca i comandi.\n");
    for (;;) {
        int n;

        printf("ftp> ");
        n = (int)read(0, riga, sizeof(riga) - 1);
        if (n <= 0) break;
        riga[n] = '\0';
        while (n > 0 && (riga[n - 1] == '\n' || riga[n - 1] == '\r')) riga[--n] = '\0';

        if (esegui(pezzi(riga, a, 3), a) < 0) break;
    }

    comando("QUIT", "", 1);
    chiudi(ctrl);
    return 0;
}

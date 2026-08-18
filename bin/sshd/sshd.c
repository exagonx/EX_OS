/* =============================================================================
 * bin/sshd/sshd.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * sshd — una sessione cifrata (SSH-2, RFC 4251..4254)
 *
 * ! E' telnetd CON LA CRITTOGRAFIA IN MEZZO, e la somiglianza e' voluta:
 * accettare, aprire un pty, avviare login, far passare i byte e ripulire e'
 * lo stesso lavoro, gia' provato in chiaro. Quello che cambia sta tutto fra la
 * connessione e i byte — ed e' cio' che questo file aggiunge.
 *
 * ! SI PARLA UNA LINGUA SOLA, E VA DETTO PERCHE'. Un algoritmo per ogni
 * mestiere: curve25519-sha256 per lo scambio, ssh-ed25519 per l'identita' del
 * server, chacha20-poly1305@openssh.com per i dati. Non c'e' negoziazione da
 * fare: si dichiara quello che si sa fare, e se il client non lo sa la
 * connessione finisce li' con un messaggio chiaro. Offrire piu' scelte
 * vorrebbe dire piu' codice di cui una parte non verrebbe mai eseguita — e un
 * codice crittografico mai eseguito e' il posto peggiore dove tenere un
 * difetto.
 *
 * ! IL PROTOCOLLO E' LA META' LUNGA, LA MATEMATICA ERA QUELLA CHE NON PERDONA.
 * Quella e' in lib/excrypt/, provata contro i vettori degli RFC: quando qui
 * qualcosa non torna, si sa che non e' li'.
 *
 * -----------------------------------------------------------------------------
 * COME SI PROVA
 *
 *     sshd -v                       sulla macchina EX-OS
 *     ssh -p 2222 root@127.0.0.1    da un'altra macchina, con un client vero
 *
 * ! IL CLIENT DEV'ESSERE VERO, non uno scritto da noi. Due programmi che si
 * capiscono a vicenda non dimostrano niente: e' con OpenSSH dall'altra parte
 * che si scopre quale campo e' lungo un byte di troppo.
 * ============================================================================= */

#include "libc.h"
#include "ip_proto.h"
#include "rete.h"
#include "excrypt.h"

/* I messaggi che ci servono. Gli altri si ignorano o chiudono. */
#define SSH_DISCONNECT          1
#define SSH_IGNORE              2
#define SSH_UNIMPLEMENTED       3
#define SSH_DEBUG               4
#define SSH_SERVICE_REQUEST     5
#define SSH_SERVICE_ACCEPT      6
#define SSH_EXT_INFO            7
#define SSH_KEXINIT            20
#define SSH_NEWKEYS            21
#define SSH_KEX_ECDH_INIT      30
#define SSH_KEX_ECDH_REPLY     31
#define SSH_USERAUTH_REQUEST   50
#define SSH_USERAUTH_FAILURE   51
#define SSH_USERAUTH_SUCCESS   52
#define SSH_USERAUTH_BANNER    53
#define SSH_GLOBAL_REQUEST     80
#define SSH_CHANNEL_OPEN       90
#define SSH_CHANNEL_OPEN_CONF  91
#define SSH_CHANNEL_OPEN_FAIL  92
#define SSH_CHANNEL_WINDOW_ADJ 93
#define SSH_CHANNEL_DATA       94
#define SSH_CHANNEL_EOF        96
#define SSH_CHANNEL_CLOSE      97
#define SSH_CHANNEL_REQUEST    98
#define SSH_CHANNEL_SUCCESS    99
#define SSH_CHANNEL_FAILURE   100

#define PKT_MAX     4096        /* ! UN PACCHETTO SSH PUO' ARRIVARE A 32 KB, e
                                 * qui si tiene 4: una sessione di shell manda
                                 * righe, non file. Un pacchetto piu' grande si
                                 * rifiuta invece di troncarlo — troncare
                                 * vorrebbe dire un flusso che va fuori passo e
                                 * un errore che si manifesta molto dopo. */

static const char VERSIONE[] = "SSH-2.0-EXOS_0.1";

static int  pid_ip = 0;
static int  g_verboso = 0;

/* -----------------------------------------------------------------------------
 * Byte imprevedibili, o niente
 *
 * ! getentropy() PUO' FALLIRE E NON RIEMPIRE NIENTE, e la libc lo dice a chiare
 * lettere accanto al prototipo: «entrambe possono fallire con EAGAIN, e non e'
 * un caso da ignorare». Chiamarla senza guardare l'esito e' esattamente cio'
 * che ho fatto nella prima stesura, e la diagnosi l'ha mostrato in tre parole:
 *
 *     sshd: DIAGNOSI privata effimera 0000000000000000...
 *
 * Una chiave effimera di soli zeri non da' nessun errore: lo scambio riesce, la
 * sessione parte, e chiunque ascolti puo' rifare gli stessi conti. E' il modo
 * peggiore in cui possa rompersi una cosa del genere — funzionando.
 *
 * ! QUANDO NON C'E' ENTROPIA SI ASPETTA, E POI SI RINUNCIA. Il kernel raccoglie
 * imprevedibilita' dagli eventi e non genera numeri: appena acceso ne ha poca, e
 * un servizio che parte all'avvio la chiede proprio nel momento peggiore.
 * Aspettare qualche istante quasi sempre basta; se non basta, si chiude la
 * connessione — mai proseguire con byte che non sono segreti.
 * --------------------------------------------------------------------------- */
static int casuale(unsigned char *buf, unsigned int n)
{
    int giri;

    for (giri = 0; giri < 20; giri++) {
        if (getentropy(buf, n) == 0) return 0;
        usleep(200000);
    }

    printf("sshd: non riesco ad avere %u byte imprevedibili dal kernel.\n", n);
    printf("      Non proseguo: una chiave che si puo' indovinare e' peggio\n");
    printf("      di nessuna connessione.\n");
    return -1;
}

/* -----------------------------------------------------------------------------
 * Lo stato di una connessione
 * --------------------------------------------------------------------------- */
typedef struct {
    int           id;                   /* la connessione TCP */
    unsigned char coda[PKT_MAX * 2];    /* byte arrivati e non ancora usati */
    unsigned int  coda_n;

    int           cifrato;              /* da NEWKEYS in poi */
    unsigned char k_c2s[64], k_s2c[64]; /* K_2 || K_1, come vuole OpenSSH */
    unsigned int  seq_in, seq_out;

    unsigned char sid[32];              /* l'identificativo di sessione */
    int           canale;               /* il canale del client, -1 = nessuno */
    unsigned int  finestra;             /* quanto possiamo ancora mandargli */
} Sess;

/* -----------------------------------------------------------------------------
 * Numeri e stringhe come li vuole SSH
 * --------------------------------------------------------------------------- */
static void metti32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static unsigned int prendi32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8)  |  (unsigned int)p[3];
}

/* Una stringa SSH e' lunghezza a 32 bit piu' i byte. */
static unsigned int metti_str(unsigned char *p, const void *d, unsigned int n)
{
    metti32(p, n);
    memcpy(p + 4, d, n);
    return 4 + n;
}

/* ! UN mpint PORTA UNO ZERO DAVANTI SE IL PRIMO BIT E' ACCESO, perche' e' un
 * numero CON SEGNO: senza quello zero, il segreto condiviso diventerebbe
 * negativo e l'impronta dello scambio non coinciderebbe con quella del client.
 * E' l'errore che da' «corrupted MAC» e fa cercare il difetto nel cifrario. */
static unsigned int metti_mpint(unsigned char *p, const unsigned char *n,
                                unsigned int len)
{
    unsigned int i = 0;

    while (i < len && n[i] == 0) i++;        /* niente zeri inutili davanti */

    if (i == len) { metti32(p, 0); return 4; }

    if (n[i] & 0x80) {
        metti32(p, len - i + 1);
        p[4] = 0;
        memcpy(p + 5, n + i, len - i);
        return 4 + 1 + (len - i);
    }
    metti32(p, len - i);
    memcpy(p + 4, n + i, len - i);
    return 4 + (len - i);
}

/* -----------------------------------------------------------------------------
 * Il trasporto: byte dentro e fuori dalla connessione
 * --------------------------------------------------------------------------- */
static int attendi_ipc(unsigned int tipo, unsigned char *buf, unsigned int *len,
                       unsigned int ms)
{
    IpcMessage meta;
    int i;

    for (i = 0; i < 64; i++) {
        if (ipc_recv_timeout(&meta, buf, IPC_MSG_MAX_DATA, ms) < 0) return -1;
        if ((int)meta.sender_pid != pid_ip) continue;
        if (meta.tipo != tipo) continue;
        if (len) *len = meta.len;
        return 0;
    }
    return -1;
}

static int esito_ipc(unsigned int ms)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpEsito       e;

    if (attendi_ipc(IP_MSG_ESITO, buf, &len, ms) != 0 || len < sizeof(e)) return -1;
    memcpy(&e, buf, sizeof(e));
    return e.codice;
}

static int manda(Sess *s, const unsigned char *d, unsigned int n)
{
    unsigned char msg[sizeof(IpTcpDati) + 512];
    IpTcpDati     h;

    while (n > 0) {
        unsigned int q = (n > 512) ? 512 : n;

        h.id = (unsigned int)s->id;
        h.len = q;
        memcpy(msg, &h, sizeof(h));
        memcpy(msg + sizeof(h), d, q);

        if (ipc_send(pid_ip, IP_MSG_TCP_INVIA, msg, sizeof(h) + q) < 0) return -1;
        if (esito_ipc(4000) < 0) return -1;

        d += q; n -= q;
    }
    return 0;
}

/* Porta nella coda almeno `quanti` byte. Rende 0, o -1 se la connessione
 * finisce. */
static int riempi(Sess *s, unsigned int quanti, unsigned int ms)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpTcpRif      r;
    IpTcpDati     d;
    int           giri;

    r.id = (unsigned int)s->id;

    for (giri = 0; s->coda_n < quanti && giri < 400; giri++) {
        if (ipc_send(pid_ip, IP_MSG_TCP_RICEVI, &r, sizeof(r)) < 0) return -1;

        if (attendi_ipc(IP_MSG_TCP_DATI, buf, &len, ms) != 0) {
            IpTcpInfo info;

            if (ipc_send(pid_ip, IP_MSG_TCP_STATO, &r, sizeof(r)) < 0) return -1;
            if (attendi_ipc(IP_MSG_TCP_INFO, buf, &len, 2000) != 0) return -1;
            memcpy(&info, buf, sizeof(info));
            if (info.stato != IP_TCP_APERTA) return -1;
            continue;
        }

        if (len < sizeof(d)) return -1;
        memcpy(&d, buf, sizeof(d));
        if (d.len == 0) continue;
        if (s->coda_n + d.len > sizeof(s->coda)) return -1;

        memcpy(s->coda + s->coda_n, buf + sizeof(d), d.len);
        s->coda_n += d.len;
    }
    return (s->coda_n >= quanti) ? 0 : -1;
}

static void consuma(Sess *s, unsigned int n)
{
    unsigned int i;

    for (i = n; i < s->coda_n; i++) s->coda[i - n] = s->coda[i];
    s->coda_n -= n;
}

/* -----------------------------------------------------------------------------
 * chacha20-poly1305@openssh.com
 *
 * ! LA LUNGHEZZA SI CIFRA CON UNA CHIAVE SUA, ed e' l'idea di questo cifrario:
 * chi ascolta non vede nemmeno quanto e' lungo un pacchetto. Le due chiavi
 * arrivano dallo stesso blocco di 64 byte — i primi 32 sono K_2, per il
 * contenuto, i secondi K_1, per la lunghezza — e l'ordine e' quello, non il
 * contrario.
 *
 * ! IL NUMERO DI SEQUENZA E' IL NONCE, e non si ripete mai: e' cio' che
 * impedisce di rigiocare un pacchetto vecchio. Va contato anche sui pacchetti
 * che si buttano, o i due capi vanno fuori passo.
 * --------------------------------------------------------------------------- */
static void nonce_da_seq(unsigned char nonce[12], unsigned int seq)
{
    memset(nonce, 0, 12);
    metti32(nonce + 8, seq);            /* gli 8 byte alti restano zero */
}

static void poly_chiave(const unsigned char k2[32], unsigned int seq,
                        unsigned char chiave[32])
{
    unsigned char nonce[12], blocco[64];

    nonce_da_seq(nonce, seq);
    chacha20_blocco(k2, 0, nonce, blocco);
    memcpy(chiave, blocco, 32);
}

/* -----------------------------------------------------------------------------
 * Un pacchetto: lunghezza, riempimento, contenuto
 * --------------------------------------------------------------------------- */
static int pacchetto_manda(Sess *s, const unsigned char *carico, unsigned int n)
{
    unsigned char pkt[PKT_MAX + 64];
    unsigned int  blocco = s->cifrato ? 8u : 8u;
    unsigned int  riempi_n, len, tot;

    /* ! IL RIEMPIMENTO E' ALMENO QUATTRO BYTE E PORTA IL TOTALE A UN MULTIPLO
     * DI OTTO. Con il cifrario a flusso non servirebbe, ma il formato lo
     * chiede e il client lo verifica. */
    len = 1 + n;                                  /* padding_length + carico */
    riempi_n = blocco - ((len + (s->cifrato ? 0u : 4u)) % blocco);
    if (riempi_n < 4) riempi_n += blocco;

    tot = len + riempi_n;
    if (tot + 4 > sizeof(pkt)) return -1;

    metti32(pkt, tot);
    pkt[4] = (unsigned char)riempi_n;
    memcpy(pkt + 5, carico, n);
    memset(pkt + 5 + n, 0, riempi_n);

    if (!s->cifrato) {
        int r = manda(s, pkt, 4 + tot);
        s->seq_out++;
        return r;
    }

    {
        unsigned char nonce[12], pchiave[32], tag[16];
        const unsigned char *k2 = s->k_s2c;
        const unsigned char *k1 = s->k_s2c + 32;

        nonce_da_seq(nonce, s->seq_out);

        /* La lunghezza con K_1, il resto con K_2 dal contatore 1 (lo zero l'ha
         * consumato la chiave di Poly1305). */
        chacha20(k1, 0, nonce, pkt, pkt, 4);
        chacha20(k2, 1, nonce, pkt + 4, pkt + 4, tot);

        poly_chiave(k2, s->seq_out, pchiave);
        poly1305(pchiave, pkt, 4 + tot, tag);

        memcpy(pkt + 4 + tot, tag, 16);

        {
            int r = manda(s, pkt, 4 + tot + 16);
            s->seq_out++;
            return r;
        }
    }
}

/* Rende la lunghezza del carico, o -1. */
static int pacchetto_ricevi(Sess *s, unsigned char *carico, unsigned int ms)
{
    unsigned int len, tot, riempi_n;

    if (!s->cifrato) {
        if (riempi(s, 4, ms) != 0) return -1;
        tot = prendi32(s->coda);
        if (tot < 8 || tot > PKT_MAX) return -1;
        if (riempi(s, 4 + tot, ms) != 0) return -1;

        riempi_n = s->coda[4];
        if (riempi_n + 1u > tot) return -1;

        len = tot - riempi_n - 1;
        memcpy(carico, s->coda + 5, len);
        consuma(s, 4 + tot);
        s->seq_in++;
        return (int)len;
    }

    {
        unsigned char nonce[12], pchiave[32], tag[16], lung[4];
        const unsigned char *k2 = s->k_c2s;
        const unsigned char *k1 = s->k_c2s + 32;

        if (riempi(s, 4, ms) != 0) return -1;

        nonce_da_seq(nonce, s->seq_in);
        chacha20(k1, 0, nonce, s->coda, lung, 4);
        tot = prendi32(lung);
        if (tot < 8 || tot > PKT_MAX) return -1;

        if (riempi(s, 4 + tot + 16, ms) != 0) return -1;

        /* ! L'IMPRONTA SI VERIFICA PRIMA DI DECIFRARE, e non dopo: decifrare
         * dei byte che nessuno ha autenticato vuol dire lavorare su dati che
         * chi sta in mezzo ha scelto. E il confronto e' a tempo costante. */
        poly_chiave(k2, s->seq_in, pchiave);
        poly1305(pchiave, s->coda, 4 + tot, tag);
        if (!poly1305_uguali(tag, s->coda + 4 + tot)) {
            if (g_verboso) printf("sshd: impronta sbagliata, chiudo\n");
            return -1;
        }

        chacha20(k2, 1, nonce, s->coda + 4, s->coda + 4, tot);

        riempi_n = s->coda[4];
        if (riempi_n + 1u > tot) return -1;

        len = tot - riempi_n - 1;
        memcpy(carico, s->coda + 5, len);
        consuma(s, 4 + tot + 16);
        s->seq_in++;
        return (int)len;
    }
}

/* -----------------------------------------------------------------------------
 * La chiave dell'host
 *
 * ! E' L'IDENTITA' DELLA MACCHINA, e cambia tutto se cambia: un client che ne
 * ha memorizzata una e ne trova un'altra si ferma e grida, ed e' giusto — e'
 * esattamente cio' che si vedrebbe se qualcuno si fosse messo in mezzo.
 *
 * ! SI LEGGE DA UN FILE, E SE NON C'E' SE NE FA UNA NUOVA A OGNI AVVIO. Su un
 * sistema installato il file c'e' e l'identita' resta; avviando dal CD, che e'
 * di sola lettura, non puo' esserci — e allora la si genera e SI DICE, perche'
 * un client si lamentera' a ogni collegamento e chi lo vede deve sapere
 * perche'.
 * --------------------------------------------------------------------------- */
static unsigned char g_host_seme[32], g_host_pub[32];

static void host_chiave(const char *percorso)
{
    int fd = open(percorso, O_RDONLY);

    if (fd >= 0) {
        int n = (int)read(fd, g_host_seme, 32);
        close(fd);
        if (n == 32) {
            ed25519_pubblica(g_host_pub, g_host_seme);
            return;
        }
    }

    if (casuale(g_host_seme, 32) != 0) {
        printf("sshd: senza chiave dell'host non posso servire nessuno.\n");
        exit(1);
    }
    ed25519_pubblica(g_host_pub, g_host_seme);

    fd = open(percorso, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        write(fd, g_host_seme, 32);
        close(fd);
        printf("sshd: chiave dell'host nuova, scritta in %s\n", percorso);
    } else {
        printf("sshd: non posso salvare la chiave dell'host in %s\n", percorso);
        printf("      ne uso una nuova a ogni avvio: i client si lamenteranno\n");
        printf("      che l'identita' della macchina e' cambiata, ed e' vero.\n");
    }
}

/* La chiave pubblica come la vuole SSH: string "ssh-ed25519" || string chiave */
static unsigned int host_blob(unsigned char *p)
{
    unsigned int k = 0;

    k += metti_str(p + k, "ssh-ed25519", 11);
    k += metti_str(p + k, g_host_pub, 32);
    return k;
}

/* -----------------------------------------------------------------------------
 * KEXINIT — si dichiara quello che si sa fare
 * --------------------------------------------------------------------------- */
static const char *ALG_KEX   = "curve25519-sha256";
static const char *ALG_HOST  = "ssh-ed25519";
static const char *ALG_CIF   = "chacha20-poly1305@openssh.com";
static const char *ALG_MAC   = "";      /* il cifrario autentica da se' */
static const char *ALG_COMP  = "none";

static unsigned int kexinit_componi(unsigned char *p)
{
    unsigned int k = 0;
    int i;

    p[k++] = SSH_KEXINIT;
    if (casuale(p + k, 16) != 0) return 0;
    k += 16;

    k += metti_str(p + k, ALG_KEX,  (unsigned int)strlen(ALG_KEX));
    k += metti_str(p + k, ALG_HOST, (unsigned int)strlen(ALG_HOST));
    for (i = 0; i < 2; i++) k += metti_str(p + k, ALG_CIF, (unsigned int)strlen(ALG_CIF));
    for (i = 0; i < 2; i++) k += metti_str(p + k, ALG_MAC, (unsigned int)strlen(ALG_MAC));
    for (i = 0; i < 2; i++) k += metti_str(p + k, ALG_COMP, (unsigned int)strlen(ALG_COMP));
    for (i = 0; i < 2; i++) k += metti_str(p + k, "", 0);   /* le lingue */

    p[k++] = 0;                         /* niente ipotesi sul primo pacchetto */
    metti32(p + k, 0); k += 4;          /* riservato */
    return k;
}

/* -----------------------------------------------------------------------------
 * Lo scambio di chiavi
 *
 * ! L'IMPRONTA DELLO SCAMBIO E' TUTTO CIO' CHE CI SIAMO DETTI FIN QUI, e la
 * firma sta sopra a quella: le versioni, le due liste di algoritmi, la chiave
 * dell'host, i due punti pubblici e il segreto. Un solo byte diverso da una
 * parte e il client vede una firma che non torna — ed e' cosi' che si accorge
 * di qualcuno che ha cambiato la lista degli algoritmi per farne scegliere uno
 * debole.
 * --------------------------------------------------------------------------- */
static int kex(Sess *s, const unsigned char *vc, unsigned int vc_n,
               const unsigned char *ic, unsigned int ic_n,
               const unsigned char *is, unsigned int is_n)
{
    unsigned char buf[PKT_MAX], out[PKT_MAX];
    unsigned char qc[32], priv[32], qs[32], segreto[32];
    unsigned char h[32], firma[64];
    unsigned char *hbuf;
    unsigned int  k = 0, n;
    int           len;

    len = pacchetto_ricevi(s, buf, 20000);
    if (len < 1 || buf[0] != SSH_KEX_ECDH_INIT) return -1;
    if (len < 5 || prendi32(buf + 1) != 32) return -1;
    memcpy(qc, buf + 5, 32);

    if (casuale(priv, 32) != 0) return -1;
    x25519_pubblica(qs, priv);

    if (x25519(segreto, priv, qc) != 0) {
        if (g_verboso) printf("sshd: punto di ordine piccolo, chiudo\n");
        return -1;
    }

    /* H = SHA256(V_C, V_S, I_C, I_S, K_S, Q_C, Q_S, K) */
    hbuf = (unsigned char *)malloc(PKT_MAX * 2);
    if (!hbuf) return -1;

    k = 0;
    k += metti_str(hbuf + k, vc, vc_n);
    k += metti_str(hbuf + k, VERSIONE, (unsigned int)strlen(VERSIONE));
    k += metti_str(hbuf + k, ic, ic_n);
    k += metti_str(hbuf + k, is, is_n);
    {
        unsigned char ks[64];
        unsigned int  ks_n = host_blob(ks);
        k += metti_str(hbuf + k, ks, ks_n);
    }
    k += metti_str(hbuf + k, qc, 32);
    k += metti_str(hbuf + k, qs, 32);
    k += metti_mpint(hbuf + k, segreto, 32);

    sha256(hbuf, k, h);

    free(hbuf);

    /* ! L'IDENTIFICATIVO DI SESSIONE E' LA PRIMA IMPRONTA, PER SEMPRE. Non
     * cambia nemmeno se le chiavi si rinnovano, ed e' cio' che lega
     * l'autenticazione a QUESTA sessione: una richiesta rubata non vale su
     * un'altra connessione. */
    memcpy(s->sid, h, 32);

    ed25519_firma(firma, h, 32, g_host_seme, g_host_pub);

    /* La risposta: chiave dell'host, il nostro punto, la firma. */
    n = 0;
    out[n++] = SSH_KEX_ECDH_REPLY;
    {
        unsigned char ks[64];
        unsigned int  ks_n = host_blob(ks);
        n += metti_str(out + n, ks, ks_n);
    }
    n += metti_str(out + n, qs, 32);
    {
        unsigned char fb[128];
        unsigned int  fn = 0;

        fn += metti_str(fb + fn, "ssh-ed25519", 11);
        fn += metti_str(fb + fn, firma, 64);
        n += metti_str(out + n, fb, fn);
    }
    if (pacchetto_manda(s, out, n) != 0) return -1;

    n = 0;
    out[n++] = SSH_NEWKEYS;
    if (pacchetto_manda(s, out, n) != 0) return -1;

    /* Il client manda il suo NEWKEYS: da li' in poi si cifra. */
    len = pacchetto_ricevi(s, buf, 10000);
    if (len < 1 || buf[0] != SSH_NEWKEYS) return -1;

    /* -------------------------------------------------------------------------
     * Le chiavi vere: HASH(K || H || lettera || session_id), e poi si allunga
     * con HASH(K || H || quello che si ha finora) finche' bastano.
     *
     * ! LA LETTERA DISTINGUE I VERSI, e prenderne una sola vorrebbe dire usare
     * la stessa chiave in andata e ritorno: due flussi cifrati con la stessa
     * chiave e lo stesso contatore si annullano a vicenda con uno XOR.
     * ----------------------------------------------------------------------- */
    {
        unsigned char base[PKT_MAX];
        unsigned int  b0;
        int  v;

        b0 = 0;
        b0 += metti_mpint(base, segreto, 32);
        memcpy(base + b0, h, 32); b0 += 32;

        for (v = 0; v < 2; v++) {
            unsigned char *dove = v ? s->k_s2c : s->k_c2s;
            unsigned char  lettera = v ? 'D' : 'C';
            unsigned char  primo[32], secondo[32];

            base[b0] = lettera;
            memcpy(base + b0 + 1, s->sid, 32);
            sha256(base, b0 + 1 + 32, primo);

            memcpy(base + b0, primo, 32);
            sha256(base, b0 + 32, secondo);

            memcpy(dove, primo, 32);
            memcpy(dove + 32, secondo, 32);
        }
    }

    s->cifrato = 1;
    if (g_verboso) printf("sshd: chiavi in vigore, da qui si cifra\n");
    return 0;
}

/* -----------------------------------------------------------------------------
 * L'autenticazione
 *
 * ! LA PASSWORD LA VERIFICA login, NON QUESTO PROGRAMMA. Sembra piu' semplice
 * leggere /boot/ombra qui dentro, e sarebbe una seconda copia della stessa
 * verita': il formato del file, il sale, l'impronta, le liste di chi puo'
 * entrare. Due copie divergono al primo cambiamento — e la meta' che diverge
 * per prima e' sempre quella che si usa meno.
 *
 * Quindi qui si accetta l'utente e si lascia che sia login, sul pty, a
 * chiedere e verificare. E' un giro in piu' e una verita' sola.
 * --------------------------------------------------------------------------- */
static int autentica(Sess *s, char *utente, unsigned int max)
{
    unsigned char buf[PKT_MAX], out[PKT_MAX];
    int  len, tentativi;

    /* SERVICE_REQUEST: si accetta ssh-userauth e basta. */
    len = pacchetto_ricevi(s, buf, 20000);
    if (len < 1 || buf[0] != SSH_SERVICE_REQUEST) return -1;

    {
        unsigned int n = 0;
        out[n++] = SSH_SERVICE_ACCEPT;
        n += metti_str(out + n, "ssh-userauth", 12);
        if (pacchetto_manda(s, out, n) != 0) return -1;
    }

    for (tentativi = 0; tentativi < 8; tentativi++) {
        unsigned int p, ln;

        len = pacchetto_ricevi(s, buf, 60000);
        if (len < 1) return -1;

        if (buf[0] == SSH_IGNORE || buf[0] == SSH_DEBUG) continue;
        if (buf[0] == SSH_DISCONNECT) return -1;
        if (buf[0] != SSH_USERAUTH_REQUEST) continue;

        p = 1;
        ln = prendi32(buf + p); p += 4;
        if (ln >= max) ln = max - 1;
        memcpy(utente, buf + p, ln);
        utente[ln] = '\0';
        p += prendi32(buf + 1);

        /* Si salta il servizio e si guarda il metodo. */
        p += 4 + prendi32(buf + p);
        ln = prendi32(buf + p); p += 4;

        if (ln == 4 && memcmp(buf + p, "none", 4) == 0) {
            /* ! AL PRIMO GIRO IL CLIENT CHIEDE «none» PER SAPERE COSA SI
             * ACCETTA, e non e' un tentativo di entrare: si risponde con
             * l'elenco dei metodi, che qui e' uno solo. */
            unsigned int n = 0;

            out[n++] = SSH_USERAUTH_FAILURE;
            n += metti_str(out + n, "password", 8);
            out[n++] = 0;
            if (pacchetto_manda(s, out, n) != 0) return -1;
            continue;
        }

        if (ln == 8 && memcmp(buf + p, "password", 8) == 0) {
            unsigned int n = 0;

            /* ! LA PASSWORD ARRIVA QUI E NON SI GUARDA. La chiedera' login sul
             * pty, che sa come si verifica; questo programma non deve nemmeno
             * poterla leggere per sbaglio. */
            out[n++] = SSH_USERAUTH_SUCCESS;
            if (pacchetto_manda(s, out, n) != 0) return -1;

            if (g_verboso) printf("sshd: utente '%s' ammesso al terminale\n", utente);
            return 0;
        }

        {
            unsigned int n = 0;

            out[n++] = SSH_USERAUTH_FAILURE;
            n += metti_str(out + n, "password", 8);
            out[n++] = 0;
            if (pacchetto_manda(s, out, n) != 0) return -1;
        }
    }
    return -1;
}

/* -----------------------------------------------------------------------------
 * Il canale, e la shell dall'altra parte
 * --------------------------------------------------------------------------- */
static int canale_apri(Sess *s)
{
    unsigned char buf[PKT_MAX], out[PKT_MAX];
    int  len, giri;

    for (giri = 0; giri < 16; giri++) {
        len = pacchetto_ricevi(s, buf, 30000);
        if (len < 1) return -1;

        if (buf[0] == SSH_GLOBAL_REQUEST) {
            /* Si risponde di no a tutto: non serviamo inoltri di porte. */
            unsigned int n = 0;
            out[n++] = 82;                      /* REQUEST_FAILURE */
            if (pacchetto_manda(s, out, n) != 0) return -1;
            continue;
        }

        if (buf[0] == SSH_CHANNEL_OPEN) {
            unsigned int p = 1, tn, n;

            tn = prendi32(buf + p); p += 4;
            if (tn != 7 || memcmp(buf + p, "session", 7) != 0) {
                n = 0;
                out[n++] = SSH_CHANNEL_OPEN_FAIL;
                metti32(out + n, prendi32(buf + p + tn)); n += 4;
                metti32(out + n, 3); n += 4;    /* unknown channel type */
                n += metti_str(out + n, "solo session", 12);
                n += metti_str(out + n, "", 0);
                if (pacchetto_manda(s, out, n) != 0) return -1;
                continue;
            }
            p += tn;

            s->canale   = (int)prendi32(buf + p); p += 4;
            s->finestra = prendi32(buf + p);

            n = 0;
            out[n++] = SSH_CHANNEL_OPEN_CONF;
            metti32(out + n, (unsigned int)s->canale); n += 4;
            metti32(out + n, 0); n += 4;            /* il nostro numero */
            metti32(out + n, 1u << 20); n += 4;     /* quanto accettiamo */
            metti32(out + n, PKT_MAX - 64); n += 4; /* pacchetto massimo */
            if (pacchetto_manda(s, out, n) != 0) return -1;
            return 0;
        }
    }
    return -1;
}

/* Aspetta «shell», rispondendo alle richieste che arrivano prima (pty-req,
 * env). Rende 0 quando la shell e' stata chiesta. */
static int canale_pronto(Sess *s, unsigned int *righe, unsigned int *colonne)
{
    unsigned char buf[PKT_MAX], out[PKT_MAX];
    int  len, giri;

    *righe = 24; *colonne = 80;

    for (giri = 0; giri < 16; giri++) {
        unsigned int p, tn, n;
        int          vuole_risposta;

        len = pacchetto_ricevi(s, buf, 30000);
        if (len < 1) return -1;
        if (buf[0] != SSH_CHANNEL_REQUEST) continue;

        p = 1 + 4;                       /* il numero del canale, che e' il nostro */
        tn = prendi32(buf + p); p += 4;

        if (p + tn >= (unsigned int)len) continue;   /* richiesta monca */
        vuole_risposta = buf[p + tn];

        if (g_verboso) {
            char nome[32];
            unsigned int q = tn < sizeof(nome) - 1 ? tn : sizeof(nome) - 1;

            memcpy(nome, buf + p, q); nome[q] = '\0';
            printf("sshd: il canale chiede '%s' (%u byte di richiesta)\n", nome, (unsigned int)len);
        }

        if (tn == 7 && memcmp(buf + p, "pty-req", 7) == 0) {
            unsigned int q = p + tn + 1;

            /* ! LA MISURA ARRIVA QUI, E VA MESSA NEL pty PRIMA DELLA SHELL:
             * chi disegna a schermo pieno la chiede all'avvio, e una misura
             * sbagliata la scopre solo ridisegnando storto. */
            q += 4 + prendi32(buf + q);         /* il nome del terminale */
            *colonne = prendi32(buf + q); q += 4;
            *righe   = prendi32(buf + q); q += 4;

            /* ! UN TERMINALE DI MISURA ZERO NON ESISTE, e un client che la
             * manda cosi' non sta mentendo: sta dicendo «non lo so». Succede
             * quando dall'altra parte non c'e' davvero uno schermo — un
             * comando pilotato da uno script, per esempio. Prendere quel zero
             * alla lettera vorrebbe dire una shell che crede di avere zero
             * righe, e un programma a schermo pieno che divide per zero. */
            if (*colonne == 0 || *righe == 0) { *colonne = 80; *righe = 24; }

            if (vuole_risposta) {
                n = 0;
                out[n++] = SSH_CHANNEL_SUCCESS;
                metti32(out + n, (unsigned int)s->canale); n += 4;
                if (pacchetto_manda(s, out, n) != 0) return -1;
            }
            continue;
        }

        if (tn == 5 && memcmp(buf + p, "shell", 5) == 0) {
            if (vuole_risposta) {
                n = 0;
                out[n++] = SSH_CHANNEL_SUCCESS;
                metti32(out + n, (unsigned int)s->canale); n += 4;
                if (pacchetto_manda(s, out, n) != 0) return -1;
            }
            return 0;
        }

        /* Tutto il resto — env, exec, subsystem — si rifiuta e si dice. */
        if (vuole_risposta) {
            n = 0;
            out[n++] = SSH_CHANNEL_FAILURE;
            metti32(out + n, (unsigned int)s->canale); n += 4;
            if (pacchetto_manda(s, out, n) != 0) return -1;
        }
    }
    return -1;
}

/* -----------------------------------------------------------------------------
 * La sessione: il pty da una parte, il canale dall'altra
 *
 * ! DA QUI IN GIU' E' telnetd, con due differenze: i byte passano dal cifrario
 * e viaggiano dentro CHANNEL_DATA invece che nudi. Il resto — pty, login, il
 * poll su due sorgenti, l'interruzione alla fine — e' lo stesso lavoro, gia'
 * provato in chiaro.
 * --------------------------------------------------------------------------- */
/* Rende 1 se il figlio e' finito. Sta in una funzione perche' il ciclo lo
 * chiede in due punti e la forma «waitpid con WNOHANG» va scritta uguale. */
static int waitfiglio_finito(int pid, int *stato)
{
    return waitpid(pid, stato, WNOHANG) == pid;
}

static void sessione_dati(Sess *s, const char *shell, unsigned int righe,
                          unsigned int colonne)
{
    unsigned char buf[PKT_MAX], out[PKT_MAX];
    struct pollfd v[2];
    SpawnRedir    red[3];
    char         *argv[2];
    int           fd[2], figlio, i, len;

    if (pty_apri(fd) != 0) return;

    pty_ctl(fd[0], PTY_CTL_MISURA, (righe << 16) | colonne);

    for (i = 0; i < 3; i++) {
        red[i].fd = i; red[i].flags = 0; red[i].percorso = 0;
        red[i].fd_padre = fd[1];
    }
    argv[0] = (char *)shell;
    argv[1] = 0;

    figlio = spawn_ex(shell, argv, environ, red, 3);
    close(fd[1]);

    if (figlio < 0) { close(fd[0]); return; }

    if (g_verboso) printf("sshd: %s ha il PID %d\n", shell, figlio);

    for (;;) {
        int stato = 0;

        /* ! CHI MUORE LASCIA DEI BYTE NEL TUBO, e vanno consegnati prima di
         * chiudere. Uscendo appena il figlio termina si perde l'ultima cosa
         * che ha stampato — e l'ultima cosa e' quasi sempre quella che
         * interessava: la risposta al comando che si e' appena battuto. Il
         * sintomo era «id» che si vedeva scritto e non rispondeva mai. */
        if (waitfiglio_finito(figlio, &stato)) {
            for (;;) {
                int n = (int)read(fd[0], buf, 512);
                unsigned int k;

                if (n <= 0) break;

                k = 0;
                out[k++] = SSH_CHANNEL_DATA;
                metti32(out + k, (unsigned int)s->canale); k += 4;
                k += metti_str(out + k, buf, (unsigned int)n);
                if (pacchetto_manda(s, out, k) != 0) break;
            }
            break;
        }

        /* ! LA LETTURA DALLA RETE SI PRENOTA, E ASPETTARE UN MESSAGGIO SENZA
         * AVERLA PRENOTATA E' UNO STALLO. Lo stack consegna a chi ha chiesto:
         * qui si aspettava che la mailbox diventasse pronta, e la mailbox non
         * poteva diventarlo perche' nessuno aveva chiesto niente. Il sintomo
         * era una sessione che si apriva, mostrava il prompt e poi non
         * rispondeva ai tasti — sembrava la shell bloccata, ed era il tubo che
         * non era stato aperto. telnetd lo faceva; qui, riscrivendo il ciclo,
         * si era perso.
         *
         * Si chiede con una scadenza corta e si tratta il silenzio come «per
         * ora niente», invece di guardare FD_IPC: cosi' la prenotazione c'e'
         * sempre e non se ne accumulano due. */
        v[0].fd = fd[0];  v[0].events = POLLIN; v[0].revents = 0;

        if (poll(v, 1, 60) < 0) break;

        /* Dal programma al client. */
        if (v[0].revents & POLLIN) {
            int n = (int)read(fd[0], buf, 512);

            if (n > 0) {
                unsigned int k = 0;

                out[k++] = SSH_CHANNEL_DATA;
                metti32(out + k, (unsigned int)s->canale); k += 4;
                k += metti_str(out + k, buf, (unsigned int)n);
                if (pacchetto_manda(s, out, k) != 0) break;
            } else if (n == 0) break;
        }

        /* Dal client al programma. */
        {
            len = pacchetto_ricevi(s, buf, 60);
            if (len < 0) continue;      /* per ora niente: si rigira */
            if (len < 1) continue;

            if (buf[0] == SSH_CHANNEL_DATA) {
                unsigned int n = prendi32(buf + 5);

                if (n > 0 && 9 + n <= (unsigned int)len) write(fd[0], buf + 9, n);

                /* ! LA FINESTRA SI RIAPRE, o dopo un po' il client smette di
                 * mandare: e' il controllo di flusso di SSH, e chi non lo fa
                 * vede la sessione fermarsi dopo qualche schermata senza che
                 * nulla sembri rotto. */
                {
                    unsigned int k = 0;

                    out[k++] = SSH_CHANNEL_WINDOW_ADJ;
                    metti32(out + k, (unsigned int)s->canale); k += 4;
                    metti32(out + k, n + 4096); k += 4;
                    if (pacchetto_manda(s, out, k) != 0) break;
                }
                continue;
            }

            /* =============================================================
             * ! LA FINESTRA CAMBIA MISURA ANCHE DOPO, E FINO AL 18 AGOSTO
             * 2026 QUELLA MISURA NON AVEVA DOVE ANDARE.
             *
             * pty-req arriva una volta sola, prima della shell: dice quanto e'
             * grande il terminale NEL MOMENTO in cui la sessione si apre. Chi
             * poi allarga la finestra del proprio client manda
             * «window-change», e chi non lo tratta lascia il pty convinto di
             * essere ancora 80x24 — con il risultato che un programma a
             * schermo pieno disegna in un rettangolo che non c'e' piu' e il
             * testo va a capo dove non deve. Il sintomo non somiglia per
             * niente a «ho ridimensionato la finestra»: somiglia a un
             * programma che disegna storto.
             *
             * ! RFC 4254 6.7 DICE CHE want_reply E' FALSE, ma si risponde lo
             * stesso a chi la chiede: un client che aspetta una risposta che
             * non arriva si ferma, e discutere di chi ha ragione con un
             * programma che sta aspettando non serve a nessuno.
             * ============================================================= */
            if (buf[0] == SSH_CHANNEL_REQUEST && len > 1 + 4 + 4) {
                unsigned int p = 1 + 4;
                unsigned int tn = prendi32(buf + p);

                p += 4;
                if (p + tn >= (unsigned int)len) continue;   /* monca */

                if (tn == 13 && memcmp(buf + p, "window-change", 13) == 0) {
                    unsigned int q = p + tn + 1;    /* saltato want_reply */

                    if (q + 8 <= (unsigned int)len) {
                        unsigned int colonne = prendi32(buf + q);
                        unsigned int righe   = prendi32(buf + q + 4);

                        /* Zero vuol dire «non lo so», come in pty-req: si
                         * lascia stare invece di dare a un programma una
                         * geometria in cui dividere per zero. */
                        if (colonne && righe) {
                            pty_ctl(fd[0], PTY_CTL_MISURA,
                                    (righe << 16) | colonne);
                            if (g_verboso)
                                printf("sshd: il terminale adesso e' %ux%u\n",
                                       colonne, righe);
                        }
                    }
                }

                if (buf[p + tn]) {              /* want_reply */
                    unsigned int k = 0;

                    out[k++] = SSH_CHANNEL_SUCCESS;
                    metti32(out + k, (unsigned int)s->canale); k += 4;
                    if (pacchetto_manda(s, out, k) != 0) break;
                }
                continue;
            }

            if (buf[0] == SSH_CHANNEL_CLOSE || buf[0] == SSH_DISCONNECT) break;
            if (buf[0] == SSH_CHANNEL_EOF) continue;
        }
    }

    interrompi(figlio);
    close(fd[0]);

    {
        unsigned int k = 0;

        out[k++] = SSH_CHANNEL_CLOSE;
        metti32(out + k, (unsigned int)s->canale); k += 4;
        pacchetto_manda(s, out, k);
    }
}

/* -----------------------------------------------------------------------------
 * Una connessione, dall'inizio alla fine
 * --------------------------------------------------------------------------- */
static void connessione(int id, const char *shell)
{
    Sess s;
    unsigned char buf[PKT_MAX], ic[PKT_MAX], is[PKT_MAX];
    unsigned char vc[256];
    unsigned int  ic_n = 0, is_n = 0, vc_n = 0, righe, colonne;
    char          utente[64];
    int           len, i;

    memset(&s, 0, sizeof(s));
    s.id = id;
    s.canale = -1;

    /* --- le versioni, in chiaro e terminate da CR LF --- */
    {
        char riga[64];
        int  n = sprintf(riga, "%s\r\n", VERSIONE);

        if (manda(&s, (const unsigned char *)riga, (unsigned int)n) != 0) return;
    }

    /* ! LA RIGA DEL CLIENT PUO' ESSERE PRECEDUTA DA ALTRE, e la specifica lo
     * permette apposta: si scartano finche' non ne arriva una che comincia per
     * «SSH-». Fermarsi alla prima vorrebbe dire rifiutare i client che
     * salutano. */
    for (i = 0; i < 8; i++) {
        unsigned int j;

        if (riempi(&s, 4, 15000) != 0) return;

        for (j = 0; j < s.coda_n; j++) {
            if (s.coda[j] != '\n') continue;

            vc_n = j;
            while (vc_n > 0 && s.coda[vc_n - 1] == '\r') vc_n--;
            if (vc_n > sizeof(vc)) return;

            memcpy(vc, s.coda, vc_n);
            consuma(&s, j + 1);
            break;
        }
        if (vc_n >= 4 && memcmp(vc, "SSH-", 4) == 0) break;
        vc_n = 0;
    }
    if (vc_n < 4) return;

    if (g_verboso) {
        char nome[80];
        unsigned int q = vc_n < sizeof(nome) - 1 ? vc_n : sizeof(nome) - 1;
        memcpy(nome, vc, q); nome[q] = '\0';
        printf("sshd: il client dice di essere %s\n", nome);
    }

    /* --- KEXINIT: prima il nostro, poi il suo --- */
    is_n = kexinit_componi(is);
    if (pacchetto_manda(&s, is, is_n) != 0) return;

    len = pacchetto_ricevi(&s, ic, 20000);
    if (len < 1 || ic[0] != SSH_KEXINIT) return;
    ic_n = (unsigned int)len;

    if (kex(&s, vc, vc_n, ic, ic_n, is, is_n) != 0) {
        printf("sshd: scambio di chiavi fallito\n");
        return;
    }

    if (autentica(&s, utente, sizeof(utente)) != 0) {
        printf("sshd: autenticazione non riuscita\n");
        return;
    }

    if (canale_apri(&s) != 0) return;
    if (canale_pronto(&s, &righe, &colonne) != 0) return;

    if (g_verboso)
        printf("sshd: terminale %ux%u, avvio %s\n", colonne, righe, shell);

    sessione_dati(&s, shell, righe, colonne);

    (void)buf;
    printf("sshd: sessione chiusa\n");
}

int main(int argc, char **argv)
{
    IpTcpAscolta a;
    IpTcpAccetta ac;
    const char  *shell = "/bin/login";
    const char  *chiave = "/boot/ssh_host_key";
    int          asc, porta = 22, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) g_verboso = 1;
        else if (strcmp(argv[i], "-s") == 0) shell = "/bin/sh";
        else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) chiave = argv[++i];
        else if (strcmp(argv[i], "-h") == 0) {
            printf("uso: sshd [PORTA] [-s] [-v] [-k FILE]\n\n");
            printf("  Una sessione cifrata per volta.\n");
            printf("  -s  la shell senza chiedere l'accesso\n");
            printf("  -k  il file della chiave dell'host\n\n");
            printf("  Si parla una lingua sola: curve25519-sha256,\n");
            printf("  ssh-ed25519, chacha20-poly1305@openssh.com.\n");
            return 0;
        }
        else porta = atoi(argv[i]);
    }

    host_chiave(chiave);

    {
        unsigned char imp[32];
        char esa[80];
        int  j;

        sha256(g_host_pub, 32, imp);
        for (j = 0; j < 8; j++) sprintf(esa + j * 3, "%02x:", imp[j]);
        esa[23] = '\0';
        printf("sshd: identita' della macchina (SHA-256, primi 8 byte) %s\n", esa);
    }

    pid_ip = rete_richiedi(IP_SERVIZIO);
    if (pid_ip <= 0) return 1;

    a.porta = (unsigned int)porta;
    if (ipc_send(pid_ip, IP_MSG_TCP_ASCOLTA, &a, sizeof(a)) < 0) return 1;

    asc = esito_ipc(3000);
    if (asc < 0) {
        printf("sshd: non posso ascoltare sulla porta %d (%d)\n", porta, asc);
        return 1;
    }

    printf("sshd: in ascolto sulla porta %d, %s\n", porta, shell);

    for (;;) {
        int id;

        ac.id = (unsigned int)asc;
        ac.timeout_ms = 10000;

        if (ipc_send(pid_ip, IP_MSG_TCP_ACCETTA, &ac, sizeof(ac)) < 0) break;

        id = esito_ipc(12000);
        if (id == -ETIMEDOUT || id == -EAGAIN) continue;
        if (id < 0) break;

        printf("sshd: qualcuno si e' collegato (connessione %d)\n", id);
        connessione(id, shell);

        {
            IpTcpRif r;
            r.id = (unsigned int)id;
            ipc_send(pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
            esito_ipc(2000);
        }
    }
    return 0;
}

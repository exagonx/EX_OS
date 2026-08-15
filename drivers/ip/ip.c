/* =============================================================================
 * drivers/ip/ip.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Stack IPv4 di EX-OS (/dev/ip.drv) — PROCESSO RING3 AUTONOMO.
 *
 *   /dev/ip.drv                                  indirizzi predefiniti
 *   /dev/ip.drv -a 192.168.1.10 -m 255.255.255.0 -g 192.168.1.1
 *   /dev/ip.drv -s                               stampa lo stato ed esce
 *
 * Fa ARP, IPv4 e ICMP. Non tocca nessuna porta: parla col driver di
 * scheda via IPC, esattamente come un qualunque altro programma.
 *
 *   ping ──IPC──> ip.drv ──IPC──> ne2k.drv ──porte──> scheda
 *
 * Cosa NON fa (frammentazione, routing, DHCP) sta scritto in
 * drivers/net/ip_proto.h, insieme al perché.
 *
 * -----------------------------------------------------------------------------
 * ! IL CICLO NON PUO' MAI BLOCCARSI SU UNA SINGOLA ATTESA
 *
 * La tentazione, scrivendo `ping`, è: manda la richiesta, aspetta la
 * risposta. Qui non si può, e non per eleganza.
 *
 * Questo processo ha UNA mailbox da cui arrivano tre cose diverse: i
 * frame dal driver, le richieste dei client, e le conferme di invio.
 * Fermarsi ad aspettare "la risposta ICMP" significa che una richiesta di
 * un client, o una richiesta ARP di qualcun altro a cui dovremmo
 * rispondere, resta in coda finché non scade il ping — e con la mailbox
 * profonda 4 basta poco perché il driver si blocchi dentro `ipc_send`
 * verso di noi.
 *
 * Quindi: un ciclo solo, che riceve con una scadenza breve, smista per
 * tipo, e tiene lo stato dell'operazione in corso in una struttura
 * (g_att). Le scadenze si controllano a ogni giro, non aspettando.
 *
 * -----------------------------------------------------------------------------
 * ! SI TIENE SEMPRE UNA NET_MSG_RICEVI IN VOLO
 *
 * Il driver non spinge frame di sua iniziativa (vedi net_proto.h): risponde
 * a chi ha chiesto. Se dopo aver ricevuto un frame non se ne chiede subito
 * un altro, i frame si accumulano nella coda del driver e da lì non escono
 * più — la rete sembra funzionare per un pacchetto e poi fermarsi.
 * Riarmare la richiesta è quindi la prima cosa che si fa dopo aver
 * ricevuto un frame, prima ancora di guardarne il contenuto.
 * ============================================================================= */

#include "libc.h"
#include "net_proto.h"
#include "ip_proto.h"

/* =============================================================================
 * Costanti di protocollo
 * ============================================================================= */
#define ETH_INTEST      14
#define ETH_TIPO_ARP    0x0806
#define ETH_TIPO_IP     0x0800

#define ARP_ETHERNET    1
#define ARP_RICHIESTA   1
#define ARP_RISPOSTA    2

#define IP_INTEST       20      /* senza opzioni: le nostre non ne hanno */
#define IP_VERSIONE_IHL 0x45
#define IP_TTL          64
#define IP_PROTO_ICMP   1
#define IP_PROTO_UDP    17
#define IP_PROTO_TCP    6

#define ICMP_ECHO_RISP  0
#define ICMP_ECHO_RICH  8

/* Quanto vive una voce ARP. Sessanta secondi è il valore che usano un po'
 * tutti: abbastanza lungo da non riempire la rete di richieste, abbastanza
 * corto da accorgersi che una macchina ha cambiato scheda. */
#define ARP_VITA_MS     60000

/* Ogni quanto si torna a guardare le scadenze anche senza messaggi. Non è
 * un'attesa attiva: è il tempo massimo che passa fra il momento in cui una
 * scadenza matura e quello in cui ce ne accorgiamo. */
#define GIRO_MS         50

/* =============================================================================
 * Stato
 * ============================================================================= */
static IpConfig     g_cfg;
static IpStato      g_st;
static unsigned char g_mac[6];
static int          g_pid_rete = 0;
static int          g_ricevi_armato = 0;
static unsigned int g_id_ip = 1;        /* campo identificazione dei datagrammi */
static unsigned int g_id_icmp;          /* identificatore delle nostre echo */

/* --- Tabella ARP ---------------------------------------------------------- */
typedef struct {
    unsigned char ip[4];
    unsigned char mac[6];
    unsigned int  scadenza;     /* uptime_ms() oltre il quale non vale più */
    int           usata;
} VoceArp;

static VoceArp g_arp[IP_ARP_VOCI];

/* --- Porte UDP aperte ------------------------------------------------------ */
/* Vedi ip_proto.h: non sono prese, sono porte. Quattro bastano a un client
 * DHCP e a un risolutore DNS, che e' cio' che serve adesso. */
#define UDP_PORTE      4
#define UDP_CARICO_MAX 1472     /* 1500 di MTU - 20 di IP - 8 di UDP */

static struct {
    unsigned int porta;
    unsigned int proprietario;   /* chi l'ha aperta */
    unsigned int lettore;        /* chi aspetta un datagramma, 0 = nessuno */
    int          usata;
} g_udp[UDP_PORTE];

/* Prossima porta effimera da assegnare a chi chiede la porta 0. L'intervallo
 * 49152-65535 e' quello che IANA riserva proprio a questo. */
static unsigned int g_porta_effimera = 49152;

/* --- Operazione in corso -------------------------------------------------- */
/* Una sola per volta: vedi ip_proto.h.
 *
 * Gli stati sono tre e non due perche' «sto aspettando che qualcuno mi dica
 * il MAC» e «sto aspettando la risposta al ping» falliscono per motivi
 * diversi e vanno detti in modo diverso a chi ha chiesto.
 *
 * ! L'ATTESA DELL'ARP SERVE A DUE OPERAZIONI, NON A UNA. Un ping e un
 * datagramma UDP verso un indirizzo che non conosciamo ancora hanno lo
 * stesso identico problema — manca il MAC del prossimo salto — e la stessa
 * identica soluzione. Tenere due macchine a stati separate significherebbe
 * scrivere due volte le ritrasmissioni e le scadenze, e sbagliarne una.
 * Percio' `operazione` dice COSA fare quando l'ARP arriva. */
#define ATT_NULLA   0
#define ATT_ARP     1
#define ATT_ECHO    2

#define OP_ECHO     0
#define OP_UDP      1

static struct {
    int           stato;
    int           operazione;
    unsigned int  client_pid;
    unsigned char ip[4];         /* destinazione finale */
    unsigned char hop[4];        /* prossimo salto: destinazione o gateway */
    unsigned int  seq;
    unsigned int  payload;
    unsigned int  scadenza;      /* uptime_ms() oltre cui si rinuncia */
    unsigned int  inizio;        /* per il tempo di andata e ritorno */
    unsigned int  arp_prossimo;  /* quando ritentare la richiesta ARP */

    /* Il datagramma UDP tenuto da parte mentre si risolve l'ARP. */
    unsigned int  udp_porta_dest;
    unsigned int  udp_porta_loc;
    unsigned int  udp_len;
    unsigned char udp_dati[UDP_CARICO_MAX];
} g_att;

/* Ogni quanto si ripete la richiesta ARP finché nessuno risponde. Una sola
 * richiesta persa non deve costare tutto il tempo di attesa del ping. */
#define ARP_RITENTA_MS 400

/* =============================================================================
 * Utilità
 * ============================================================================= */
static int ip_uguali(const unsigned char *a, const unsigned char *b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static int ip_nullo(const unsigned char *a)
{
    return (a[0] | a[1] | a[2] | a[3]) == 0;
}

/* Vero per 255.255.255.255 e per il broadcast della NOSTRA sottorete
 * (bit di host tutti a uno).
 *
 * ! SERVE IN DUE PUNTI E PER DUE MOTIVI DIVERSI: in uscita per non fare
 * ARP (a «tutti» non si chiede il MAC, lo si sa), in entrata per accettare
 * pacchetti che non sono indirizzati a noi in particolare. Senza il
 * secondo, un client DHCP non riceverebbe mai la risposta: il server la
 * manda in broadcast proprio perche' noi un indirizzo non ce l'abbiamo
 * ancora. */
static int e_broadcast(const unsigned char *ip)
{
    int i;

    if (ip[0] == 255 && ip[1] == 255 && ip[2] == 255 && ip[3] == 255) return 1;

    /* Senza maschera configurata non esiste un broadcast di sottorete. */
    if ((g_cfg.maschera[0] | g_cfg.maschera[1] |
         g_cfg.maschera[2] | g_cfg.maschera[3]) == 0) return 0;

    for (i = 0; i < 4; i++) {
        unsigned char host = (unsigned char)(ip[i] | g_cfg.maschera[i]);
        if (host != 0xFF) return 0;
        if ((ip[i] & g_cfg.maschera[i]) !=
            (g_cfg.ip[i] & g_cfg.maschera[i])) return 0;
    }
    return 1;
}

static void metti16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)((v >> 8) & 0xFF);
    p[1] = (unsigned char)(v & 0xFF);
}

static unsigned int prendi16(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | p[1];
}

static void metti32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)((v >> 24) & 0xFF);
    p[1] = (unsigned char)((v >> 16) & 0xFF);
    p[2] = (unsigned char)((v >> 8)  & 0xFF);
    p[3] = (unsigned char)(v & 0xFF);
}

static unsigned int prendi32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8)  | p[3];
}

/* Somma di controllo di Internet: somma a 16 bit in complemento a uno,
 * ripiegata e poi complementata.
 *
 * ! IL RIPIEGAMENTO SI FA IN UN CICLO, NON UNA VOLTA SOLA. Sommando
 * abbastanza parole il riporto può a sua volta produrre un riporto: una
 * singola riga `s = (s & 0xFFFF) + (s >> 16)` è giusta quasi sempre, e
 * quel "quasi" è un pacchetto ogni tanto rifiutato dall'altra parte senza
 * che si capisca perché. */
static unsigned int somma_controllo(const unsigned char *dati, unsigned int len)
{
    unsigned int s = 0, i;

    for (i = 0; i + 1 < len; i += 2)
        s += ((unsigned int)dati[i] << 8) | dati[i + 1];
    if (i < len) s += (unsigned int)dati[i] << 8;   /* byte dispari finale */

    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (~s) & 0xFFFF;
}

/* =============================================================================
 * Tabella ARP
 * ============================================================================= */
static VoceArp *arp_cerca(const unsigned char *ip)
{
    unsigned int ora = uptime_ms();
    int i;

    for (i = 0; i < IP_ARP_VOCI; i++) {
        if (!g_arp[i].usata) continue;
        if (!ip_uguali(g_arp[i].ip, ip)) continue;
        /* Una voce scaduta vale come assente: si preferisce una richiesta
         * ARP in più a un pacchetto mandato a una scheda che non c'è più. */
        if ((int)(ora - g_arp[i].scadenza) >= 0) { g_arp[i].usata = 0; return NULL; }
        return &g_arp[i];
    }
    return NULL;
}

static void arp_registra(const unsigned char *ip, const unsigned char *mac)
{
    unsigned int ora = uptime_ms();
    int i, libero = -1, piu_vecchia = 0;

    for (i = 0; i < IP_ARP_VOCI; i++) {
        if (!g_arp[i].usata) { if (libero < 0) libero = i; continue; }
        if (ip_uguali(g_arp[i].ip, ip)) { libero = i; break; }
        if ((int)(g_arp[i].scadenza - g_arp[piu_vecchia].scadenza) < 0)
            piu_vecchia = i;
    }

    if (libero < 0) libero = piu_vecchia;   /* si butta quella che scade prima */

    memcpy(g_arp[libero].ip, ip, 4);
    memcpy(g_arp[libero].mac, mac, 6);
    g_arp[libero].scadenza = ora + ARP_VITA_MS;
    g_arp[libero].usata    = 1;
}

/* =============================================================================
 * Invio di un frame al driver
 *
 * Non si aspetta la conferma: arriverà nel ciclo principale come un
 * NET_MSG_ESITO qualunque. Fermarsi qui a leggerla vorrebbe dire scartare
 * o rimandare tutto quello che nel frattempo è arrivato — vedi il commento
 * di testa.
 * ============================================================================= */
static void manda_frame(const unsigned char *f, unsigned int len)
{
    if (ipc_send(g_pid_rete, NET_MSG_INVIA, f, len) < 0)
        g_st.scartati++;
}

static void riarma_ricezione(void)
{
    if (g_ricevi_armato) return;
    if (ipc_send(g_pid_rete, NET_MSG_RICEVI, NULL, 0) == 0)
        g_ricevi_armato = 1;
}

/* =============================================================================
 * ARP
 * ============================================================================= */
static void arp_chiedi(const unsigned char *ip)
{
    unsigned char f[42];
    int i;

    for (i = 0; i < 6; i++) f[i] = 0xFF;
    memcpy(f + 6, g_mac, 6);
    metti16(f + 12, ETH_TIPO_ARP);

    metti16(f + 14, ARP_ETHERNET);
    metti16(f + 16, ETH_TIPO_IP);
    f[18] = 6; f[19] = 4;
    metti16(f + 20, ARP_RICHIESTA);
    memcpy(f + 22, g_mac, 6);
    memcpy(f + 28, g_cfg.ip, 4);
    memset(f + 32, 0, 6);
    memcpy(f + 38, ip, 4);

    manda_frame(f, sizeof(f));
    g_st.arp_richieste_inviate++;
}

static void arp_rispondi(const unsigned char *a_mac, const unsigned char *a_ip)
{
    unsigned char f[42];
    int i;

    memcpy(f, a_mac, 6);
    memcpy(f + 6, g_mac, 6);
    metti16(f + 12, ETH_TIPO_ARP);

    metti16(f + 14, ARP_ETHERNET);
    metti16(f + 16, ETH_TIPO_IP);
    f[18] = 6; f[19] = 4;
    metti16(f + 20, ARP_RISPOSTA);
    memcpy(f + 22, g_mac, 6);
    memcpy(f + 28, g_cfg.ip, 4);
    memcpy(f + 32, a_mac, 6);
    memcpy(f + 38, a_ip, 4);
    (void)i;

    manda_frame(f, sizeof(f));
    g_st.arp_risposte_date++;
}

static void tratta_arp(const unsigned char *f, unsigned int len)
{
    unsigned int op;

    if (len < 42) { g_st.scartati++; return; }
    if (prendi16(f + 14) != ARP_ETHERNET || prendi16(f + 16) != ETH_TIPO_IP)
        { g_st.scartati++; return; }
    if (f[18] != 6 || f[19] != 4) { g_st.scartati++; return; }

    op = prendi16(f + 20);

    /* ! SI IMPARA ANCHE DALLE RICHIESTE, NON SOLO DALLE RISPOSTE. Chi ci
     * chiede chi siamo ci sta dicendo il proprio indirizzo, e quasi
     * sempre subito dopo gli dovremo mandare qualcosa. Registrarlo qui
     * risparmia una richiesta ARP nostra un istante più tardi. */
    if (!ip_nullo(f + 28)) arp_registra(f + 28, f + 22);

    if (op == ARP_RICHIESTA && ip_uguali(f + 38, g_cfg.ip))
        arp_rispondi(f + 22, f + 28);

    /* Se stavamo aspettando proprio questo indirizzo, l'attesa è finita.
     * Lo si controlla dopo aver registrato, così il passo successivo trova
     * la voce già in tabella. */
    if (g_att.stato == ATT_ARP && ip_uguali(f + 28, g_att.hop))
        g_att.stato = ATT_ECHO;   /* il ciclo principale manderà l'echo */
}

/* =============================================================================
 * ICMP / IPv4
 * ============================================================================= */

/* Compone un datagramma IPv4 completo di intestazione Ethernet.
 * Ritorna la lunghezza del frame, 0 se non ci sta. */
static unsigned int componi_ip(unsigned char *f, const unsigned char *mac_dest,
                               const unsigned char *ip_dest,
                               unsigned int protocollo,
                               const unsigned char *carico, unsigned int n)
{
    unsigned int tot = IP_INTEST + n;

    if (ETH_INTEST + tot > NET_FRAME_MAX) return 0;

    memcpy(f, mac_dest, 6);
    memcpy(f + 6, g_mac, 6);
    metti16(f + 12, ETH_TIPO_IP);

    f[14] = IP_VERSIONE_IHL;
    f[15] = 0;                        /* nessuna priorità */
    metti16(f + 16, tot);
    metti16(f + 18, g_id_ip++);
    metti16(f + 20, 0);               /* nessun flag, nessun frammento */
    f[22] = IP_TTL;
    f[23] = (unsigned char)protocollo;
    metti16(f + 24, 0);               /* somma di controllo: dopo */
    memcpy(f + 26, g_cfg.ip, 4);
    memcpy(f + 30, ip_dest, 4);

    /* La somma dell'intestazione IP copre SOLO l'intestazione, e va
     * calcolata con il campo stesso a zero — che è il motivo per cui si
     * scrive dopo aver riempito tutto il resto. */
    metti16(f + 24, somma_controllo(f + 14, IP_INTEST));

    memcpy(f + 14 + IP_INTEST, carico, n);
    return ETH_INTEST + tot;
}

static void manda_echo(void)
{
    unsigned char f[NET_FRAME_MAX];
    unsigned char icmp[8 + 1024];
    VoceArp      *v = arp_cerca(g_att.hop);
    unsigned int  n, len, i;

    if (v == NULL) { g_att.stato = ATT_ARP; return; }

    n = g_att.payload;
    if (n > 1024) n = 1024;

    icmp[0] = ICMP_ECHO_RICH;
    icmp[1] = 0;
    metti16(icmp + 2, 0);             /* somma: dopo */
    metti16(icmp + 4, g_id_icmp);
    metti16(icmp + 6, g_att.seq);

    /* Riempimento riconoscibile: se una risposta torna con i byte
     * cambiati, il confronto lo dice. Zeri non direbbero niente. */
    for (i = 0; i < n; i++) icmp[8 + i] = (unsigned char)('a' + (i % 26));

    /* ! LA SOMMA ICMP COPRE INTESTAZIONE **E** DATI, a differenza di
     * quella IP che copre la sola intestazione. Confonderle è l'errore
     * più comune: il pacchetto parte, sembra giusto, e l'altra parte lo
     * butta senza dire niente. */
    metti16(icmp + 2, somma_controllo(icmp, 8 + n));

    len = componi_ip(f, v->mac, g_att.ip, IP_PROTO_ICMP, icmp, 8 + n);
    if (len == 0) return;

    manda_frame(f, len);
    g_st.ip_inviati++;
    g_att.inizio = uptime_ms();
}

/* Risponde a un ping che arriva da fuori. Si riusa il pacchetto ricevuto:
 * la risposta è identica alla richiesta salvo il tipo, che è quello che
 * dice la specifica — e riscriverla da zero vorrebbe dire copiare il
 * carico due volte per ottenere lo stesso frame. */
static void rispondi_echo(const unsigned char *f, unsigned int len,
                          unsigned int ihl, unsigned int len_icmp)
{
    unsigned char risp[NET_FRAME_MAX];
    unsigned char icmp[NET_FRAME_MAX];
    const unsigned char *sorgente = f + 14 + 12;
    VoceArp      *v;
    unsigned int  n;

    if (len_icmp > sizeof(icmp)) return;
    (void)len;

    memcpy(icmp, f + 14 + ihl, len_icmp);
    icmp[0] = ICMP_ECHO_RISP;
    icmp[1] = 0;
    metti16(icmp + 2, 0);
    metti16(icmp + 2, somma_controllo(icmp, len_icmp));

    /* Chi ci ha pinga to è quasi sempre già in tabella: il suo frame è
     * arrivato, quindi il MAC lo conosciamo. Se non c'è (voce scaduta
     * fra l'arrivo e adesso) si rinuncia invece di mandare in broadcast:
     * una risposta ICMP non vale una tempesta. */
    v = arp_cerca(sorgente);
    if (v == NULL) return;

    n = componi_ip(risp, v->mac, sorgente, IP_PROTO_ICMP, icmp, len_icmp);
    if (n == 0) return;

    manda_frame(risp, n);
    /* ! ANCHE QUESTO E' UN DATAGRAMMA CHE ESCE. Il contatore stava solo
     * in manda_echo(), e una macchina che aveva risposto a tre ping
     * dichiarava «IP inviati 0» mentre ne aveva spediti tre. Un contatore
     * che non conta tutto e' peggio di un contatore assente: non lascia il
     * dubbio, da' una risposta sbagliata. */
    g_st.ip_inviati++;
    g_st.echo_serviti++;
}

/* =============================================================================
 * UDP
 * ============================================================================= */
static int udp_cerca(unsigned int porta)
{
    int i;

    for (i = 0; i < UDP_PORTE; i++)
        if (g_udp[i].usata && g_udp[i].porta == porta) return i;
    return -1;
}

/* =============================================================================
 * Somma di controllo UDP
 *
 * ! NON COPRE SOLO IL DATAGRAMMA: davanti c'e' una PSEUDO-INTESTAZIONE
 * fatta di indirizzo sorgente, destinazione, un byte a zero, il numero di
 * protocollo e la lunghezza UDP. Quei dodici byte non viaggiano sul cavo:
 * esistono solo per essere sommati, e servono a far accorgere al
 * destinatario che un pacchetto e' arrivato all'host sbagliato.
 *
 * Si compone tutto in un buffer e si somma in una volta sola, invece di
 * sommare a pezzi: somma_controllo() lavora su un blocco contiguo, e
 * cucire due somme parziali vuol dire gestire a mano il byte dispari di
 * mezzo — che e' il punto preciso in cui queste implementazioni sbagliano.
 * ============================================================================= */
/* ! IL PROTOCOLLO E' UN PARAMETRO, e prima era scritto dentro. UDP e TCP
 * usano la STESSA pseudo-intestazione salvo quel byte: duplicare la
 * funzione avrebbe significato due posti dove sbagliare la stessa cosa,
 * e la somma di controllo e' precisamente il punto in cui un errore non
 * si vede — il pacchetto parte, sembra giusto, e l'altro lo butta. */
static unsigned char g_somma_buf[12 + 4 + NET_FRAME_MAX];

static unsigned int pseudo_somma(const unsigned char *src, const unsigned char *dst,
                                 unsigned int protocollo,
                                 const unsigned char *seg, unsigned int len)
{
    if (len > sizeof(g_somma_buf) - 12u) return 0xFFFF;

    memcpy(g_somma_buf, src, 4);
    memcpy(g_somma_buf + 4, dst, 4);
    g_somma_buf[8] = 0;
    g_somma_buf[9] = (unsigned char)protocollo;
    metti16(g_somma_buf + 10, len);
    memcpy(g_somma_buf + 12, seg, len);

    return somma_controllo(g_somma_buf, 12 + len);
}

static unsigned int udp_prepara_somma(const unsigned char *src,
                                      const unsigned char *dst,
                                      const unsigned char *udp,
                                      unsigned int len_udp)
{
    return pseudo_somma(src, dst, IP_PROTO_UDP, udp, len_udp);
}

/* Da scrivere nel campo di un datagramma in partenza (che deve avere il
 * campo stesso a zero mentre si calcola).
 *
 * ! IL RISULTATO 0 SI SCRIVE COME 0xFFFF. In UDP il valore zero nel campo
 * significa «somma non calcolata»: una somma che venisse davvero zero
 * spegnerebbe il controllo invece di superarlo. I due valori sono
 * equivalenti in complemento a uno, e la specifica dice di usare l'altro. */
static unsigned int somma_udp(const unsigned char *src, const unsigned char *dst,
                              const unsigned char *udp, unsigned int len_udp)
{
    unsigned int s = udp_prepara_somma(src, dst, udp, len_udp);

    return (s == 0) ? 0xFFFF : s;
}

/* Verifica di un datagramma arrivato: si risomma tutto CON il campo com'e'
 * arrivato, e deve venire zero. */
static int somma_udp_valida(const unsigned char *src, const unsigned char *dst,
                            const unsigned char *udp, unsigned int len_udp)
{
    return udp_prepara_somma(src, dst, udp, len_udp) == 0;
}

/* Invia un datagramma. Il MAC del prossimo salto deve essere gia' noto:
 * chi chiama ha gia' risolto (o e' broadcast). */
static int udp_manda(const unsigned char *mac_dest, const unsigned char *ip_dest,
                     unsigned int porta_loc, unsigned int porta_dest,
                     const unsigned char *dati, unsigned int n)
{
    static unsigned char udp[8 + UDP_CARICO_MAX];
    unsigned char        f[NET_FRAME_MAX];
    unsigned int         len;

    if (n > UDP_CARICO_MAX) return -EMSGSIZE;

    metti16(udp,     porta_loc);
    metti16(udp + 2, porta_dest);
    metti16(udp + 4, 8 + n);
    metti16(udp + 6, 0);
    if (n) memcpy(udp + 8, dati, n);

    metti16(udp + 6, somma_udp(g_cfg.ip, ip_dest, udp, 8 + n));

    len = componi_ip(f, mac_dest, ip_dest, IP_PROTO_UDP, udp, 8 + n);
    if (len == 0) return -EMSGSIZE;

    manda_frame(f, len);
    g_st.ip_inviati++;
    g_st.udp_inviati++;
    return 0;
}

static void tratta_udp(const unsigned char *f, unsigned int ihl, unsigned int tot)
{
    const unsigned char *ip  = f + 14;
    const unsigned char *udp = f + 14 + ihl;
    unsigned int len_udp = tot - ihl;
    unsigned int porta_dest, porta_sorg, n;
    int          i;

    if (len_udp < 8) { g_st.scartati++; return; }

    porta_sorg = prendi16(udp);
    porta_dest = prendi16(udp + 2);

    /* La lunghezza dichiarata dentro UDP deve stare dentro quella
     * dichiarata da IP: se e' piu' grande, seguirla vorrebbe dire leggere
     * byte che non fanno parte del pacchetto. */
    n = prendi16(udp + 4);
    if (n < 8 || n > len_udp) { g_st.scartati++; return; }
    n -= 8;

    /* ! SOMMA A ZERO = «NON CALCOLATA», E VA ACCETTATA. In UDP su IPv4
     * il controllo e' facoltativo e c'e' chi non lo usa: rifiutare quei
     * datagrammi farebbe sparire traffico legittimo senza dire niente. */
    if (prendi16(udp + 6) != 0 &&
        !somma_udp_valida(ip + 12, ip + 16, udp, n + 8)) {
        g_st.checksum_errati++;
        return;
    }

    i = udp_cerca(porta_dest);
    if (i < 0) { g_st.udp_senza_porta++; return; }

    g_st.udp_ricevuti++;

    /* Nessuno sta aspettando: si scarta. Vedi ip_proto.h — una coda che
     * cresce mentre nessuno legge e' un modo lento di finire la memoria. */
    if (g_udp[i].lettore == 0) return;

    {
        static unsigned char msg[sizeof(IpUdpDati) + UDP_CARICO_MAX];
        IpUdpDati            d;

        if (n > UDP_CARICO_MAX) return;

        memcpy(d.ip, ip + 12, 4);
        d.porta        = porta_sorg;
        d.porta_locale = porta_dest;
        d.len          = n;

        memcpy(msg, &d, sizeof(d));
        if (n) memcpy(msg + sizeof(d), udp + 8, n);

        ipc_send(g_udp[i].lettore, IP_MSG_UDP_DATI, msg, sizeof(d) + n);
        g_udp[i].lettore = 0;
    }
}

/* Dichiarate qui perche' il TCP sta PRIMA di loro nel file: spostarlo
 * dopo vorrebbe dire metterlo dopo tratta_ip(), che lo chiama. */
static void rispondi_esito(unsigned int client, int codice);
static int  prossimo_salto(const unsigned char *dest, unsigned char *hop);

/* =============================================================================
 * TCP — connessioni in uscita
 *
 * Cosa NON fa (riordino, congestione, SACK, RTO misurato) sta scritto in
 * drivers/net/ip_proto.h, insieme al perche'. Qui c'e' la macchina.
 *
 * -----------------------------------------------------------------------------
 * ! I NUMERI DI SEQUENZA SI CONFRONTANO CON LA SOTTRAZIONE, MAI CON `<`
 *
 * Sono a 32 bit e si avvolgono. `a < b` su due numeri a cavallo
 * dell'avvolgimento da' la risposta rovesciata, e succede una volta ogni
 * 4 GB trasmessi — cioe' raramente, e sempre quando la connessione e'
 * carica. La forma giusta e' `(int32_t)(a - b) < 0`, che resta corretta
 * perche' la differenza e' piccola anche quando i due valori sono
 * lontanissimi.
 * ============================================================================= */

#define TCP_FIN   0x01
#define TCP_SYN   0x02
#define TCP_RST   0x04
#define TCP_PSH   0x08
#define TCP_ACK   0x10

/* Buffer per connessione. 4 KB per verso: due connessioni FTP ne usano
 * 16 KB in tutto, che su un processo ring3 e' poco. Non e' la finestra
 * ideale per una rete veloce, ed e' il numero da alzare quando lo sara'. */
#define TCP_BUF        4096
#define TCP_MSS        1400    /* sotto i 1460 tipici: lascia margine, e
                                  non frammentiamo comunque */
#define TCP_RTO_MS     600     /* primo intervallo di ritrasmissione */
#define TCP_TENTATIVI  6       /* poi si dichiara persa la connessione */
#define TCP_APERTURA_MS 5000   /* attesa predefinita per il collegamento */

/* Stati interni. Sono piu' di quelli che vede un client (IP_TCP_*) perche'
 * la chiusura ordinata di TCP ha piu' passi di quanti ne interessino a
 * chi usa la connessione. */
#define S_LIBERA     0
#define S_SYN_INVIA  1   /* SYN mandato, aspetto SYN+ACK */
#define S_APERTA     2
#define S_FIN_MIO    3   /* ho mandato FIN, aspetto che lo confermi */
#define S_FIN_SUO    4   /* lui ha mandato FIN, io posso ancora scrivere */
#define S_ULTIMO_ACK 5   /* entrambi i FIN mandati, aspetto l'ultimo ACK */
#define S_MORTA      6   /* finita o azzerata: resta finche' il client legge */

typedef struct {
    int           stato;
    unsigned char ip[4];
    unsigned char mac[6];        /* prossimo salto, risolto all'apertura */
    unsigned int  porta_loc, porta_rem;

    unsigned int  snd_una;       /* piu' vecchio non confermato */
    unsigned int  snd_nxt;       /* prossimo da mandare */
    unsigned int  rcv_nxt;       /* prossimo atteso */
    unsigned int  fin_seq;       /* numero di sequenza del nostro FIN */
    unsigned int  finestra;      /* quella annunciata dall'altro */

    unsigned char tx[TCP_BUF];
    unsigned int  tx_len;        /* byte in coda, confermati compresi */
    unsigned char rx[TCP_BUF];
    unsigned int  rx_len;

    unsigned int  rto_scade;
    unsigned int  rto_ms;
    unsigned int  tentativi;

    unsigned int  proprietario;  /* chi l'ha aperta */
    unsigned int  attesa_pid;    /* chi aspetta una risposta */
    unsigned int  attesa_tipo;   /* IP_MSG_TCP_APRI o IP_MSG_TCP_RICEVI */
    unsigned int  attesa_scade;
} Conn;

static Conn         g_tcp[IP_TCP_CONNESSIONI];
static unsigned int g_tcp_porta = 49152;   /* fascia effimera, come UDP */

/* Vero se a precede b nello spazio circolare dei numeri di sequenza. */
static int seq_prima(unsigned int a, unsigned int b)
{
    return (int)(a - b) < 0;
}

static Conn *tcp_da_id(unsigned int id, unsigned int cliente)
{
    Conn *c;

    if (id == 0 || id > IP_TCP_CONNESSIONI) return NULL;
    c = &g_tcp[id - 1];
    if (c->stato == S_LIBERA) return NULL;
    /* ! Solo chi l'ha aperta puo' usarla: un identificativo e' un numero
     * piccolo e indovinabile, e senza questo controllo un processo
     * qualunque potrebbe leggere i dati di un altro. */
    if (c->proprietario != cliente) return NULL;
    return c;
}

static unsigned int tcp_id(const Conn *c)
{
    return (unsigned int)(c - g_tcp) + 1u;
}

/* Traduce lo stato interno in quello che il client puo' capire. */
static unsigned int tcp_stato_pubblico(const Conn *c)
{
    switch (c->stato) {
    case S_SYN_INVIA:  return IP_TCP_IN_APERTURA;
    case S_APERTA:     return IP_TCP_APERTA;
    case S_FIN_MIO:
    case S_FIN_SUO:
    case S_ULTIMO_ACK: return IP_TCP_IN_CHIUSURA;
    case S_MORTA:      return (c->rx_len > 0) ? IP_TCP_IN_CHIUSURA : IP_TCP_CHIUSA;
    default:           return IP_TCP_CHIUSA;
    }
}

/* =============================================================================
 * Composizione e invio di un segmento
 * ============================================================================= */
static void tcp_manda(Conn *c, unsigned int flag,
                      const unsigned char *dati, unsigned int n)
{
    unsigned char seg[20 + TCP_MSS];
    unsigned char f[NET_FRAME_MAX];
    unsigned int  len, tot;

    if (n > TCP_MSS) n = TCP_MSS;

    metti16(seg,     c->porta_loc);
    metti16(seg + 2, c->porta_rem);
    metti32(seg + 4, c->snd_nxt);
    metti32(seg + 8, (flag & TCP_ACK) ? c->rcv_nxt : 0u);
    seg[12] = 5 << 4;                       /* 20 byte di intestazione */
    seg[13] = (unsigned char)flag;
    /* La finestra che annunciamo e' lo spazio libero nel buffer di
     * ricezione: dichiararne di piu' vorrebbe dire invitare l'altro a
     * mandare dati che poi butteremmo. */
    metti16(seg + 14, TCP_BUF - c->rx_len);
    metti16(seg + 16, 0);                   /* somma: dopo */
    metti16(seg + 18, 0);                   /* puntatore urgente */

    if (n) memcpy(seg + 20, dati, n);
    tot = 20u + n;

    metti16(seg + 16, pseudo_somma(g_cfg.ip, c->ip, IP_PROTO_TCP, seg, tot));

    len = componi_ip(f, c->mac, c->ip, IP_PROTO_TCP, seg, tot);
    if (len == 0) return;

    manda_frame(f, len);
    g_st.ip_inviati++;
}

/* Manda quello che c'e' in coda e non e' ancora partito. */
static void tcp_spingi(Conn *c)
{
    unsigned int inviati = c->snd_nxt - c->snd_una;
    unsigned int restanti;

    if (c->stato != S_APERTA && c->stato != S_FIN_SUO) return;
    if (c->tx_len <= inviati) return;

    restanti = c->tx_len - inviati;

    /* ! NON SI SUPERA LA FINESTRA DELL'ALTRO. Mandare oltre significa
     * mandare dati che il destinatario dichiara di non poter accettare:
     * li butta, noi li ritrasmettiamo, e la connessione si trascina. */
    if (restanti > c->finestra - inviati) {
        if (c->finestra <= inviati) return;
        restanti = c->finestra - inviati;
    }

    while (restanti > 0) {
        unsigned int q = (restanti > TCP_MSS) ? TCP_MSS : restanti;

        tcp_manda(c, TCP_ACK | TCP_PSH, c->tx + inviati, q);
        c->snd_nxt += q;
        inviati    += q;
        restanti   -= q;
    }

    if (c->rto_scade == 0) {
        c->rto_ms    = TCP_RTO_MS;
        c->rto_scade = uptime_ms() + c->rto_ms;
        c->tentativi = 0;
    }
}

/* Toglie dalla coda i byte che l'altro ha confermato. */
static void tcp_conferma(Conn *c, unsigned int ack)
{
    unsigned int quanti;

    if (!seq_prima(c->snd_una, ack)) return;    /* niente di nuovo */

    quanti = ack - c->snd_una;
    if (quanti > c->tx_len) quanti = c->tx_len;

    if (quanti > 0 && c->tx_len > quanti)
        memmove(c->tx, c->tx + quanti, c->tx_len - quanti);
    c->tx_len -= quanti;
    c->snd_una = ack;

    /* Tutto confermato: niente piu' da ritrasmettere. */
    if (c->snd_una == c->snd_nxt) {
        c->rto_scade = 0;
        c->tentativi = 0;
    } else {
        c->rto_ms    = TCP_RTO_MS;
        c->rto_scade = uptime_ms() + c->rto_ms;
        c->tentativi = 0;
    }
}

static void tcp_chiudi_dura(Conn *c, int codice)
{
    if (c->attesa_pid != 0) {
        IpEsito e;

        e.codice = codice;
        ipc_send(c->attesa_pid, IP_MSG_ESITO, &e, sizeof(e));
        c->attesa_pid = 0;
    }
    c->stato     = S_MORTA;
    c->rto_scade = 0;
}

/* Consegna al client che sta aspettando, se c'e' qualcosa da consegnare. */
static void tcp_consegna(Conn *c)
{
    static unsigned char msg[sizeof(IpTcpDati) + TCP_BUF];
    IpTcpDati            d;
    unsigned int         n;

    if (c->attesa_pid == 0 || c->attesa_tipo != IP_MSG_TCP_RICEVI) return;

    /* ! ANCHE A ZERO BYTE SI RISPONDE, se la connessione e' finita.
     * Altrimenti un client fermo su IP_MSG_TCP_RICEVI aspetterebbe dati
     * che non arriveranno mai da una connessione gia' chiusa — e la fine
     * dei dati e' proprio l'informazione che gli serve. */
    if (c->rx_len == 0 && c->stato != S_MORTA && c->stato != S_FIN_SUO &&
        c->stato != S_ULTIMO_ACK) return;

    n = c->rx_len;
    if (n > TCP_BUF) n = TCP_BUF;

    d.id  = tcp_id(c);
    d.len = n;
    memcpy(msg, &d, sizeof(d));
    if (n) memcpy(msg + sizeof(d), c->rx, n);

    ipc_send(c->attesa_pid, IP_MSG_TCP_DATI, msg, sizeof(d) + n);
    c->attesa_pid = 0;

    c->rx_len = 0;
}

/* =============================================================================
 * Arrivo di un segmento
 * ============================================================================= */
static void tratta_tcp(const unsigned char *f, unsigned int ihl, unsigned int tot)
{
    const unsigned char *ip  = f + 14;
    const unsigned char *seg = f + 14 + ihl;
    unsigned int len_seg = tot - ihl;
    unsigned int porta_loc, porta_rem, off, flag, n, i;
    unsigned int seq, ack, finestra;
    Conn        *c = NULL;

    if (len_seg < 20) { g_st.scartati++; return; }

    if (pseudo_somma(ip + 12, ip + 16, IP_PROTO_TCP, seg, len_seg) != 0) {
        g_st.checksum_errati++;
        return;
    }

    porta_rem = prendi16(seg);
    porta_loc = prendi16(seg + 2);
    seq       = prendi32(seg + 4);
    ack       = prendi32(seg + 8);
    off       = (unsigned int)(seg[12] >> 4) * 4u;
    flag      = seg[13];
    finestra  = prendi16(seg + 14);

    /* ! L'INTESTAZIONE PUO' ESSERE PIU' LUNGA DI 20 BYTE: le opzioni
     * (MSS, timestamp, SACK permitted) stanno li'. Non ne leggiamo
     * nessuna, ma bisogna SALTARLE, o i primi byte di dati sarebbero le
     * opzioni interpretate come contenuto. */
    if (off < 20u || off > len_seg) { g_st.scartati++; return; }
    n = len_seg - off;

    for (i = 0; i < IP_TCP_CONNESSIONI; i++) {
        Conn *k = &g_tcp[i];

        if (k->stato == S_LIBERA || k->stato == S_MORTA) continue;
        if (k->porta_loc != porta_loc || k->porta_rem != porta_rem) continue;
        if (!ip_uguali(k->ip, ip + 12)) continue;
        c = k;
        break;
    }

    /* Segmento per una connessione che non esiste. Non si risponde con un
     * RST: sarebbe la cosa giusta su un vero stack, ma qui significherebbe
     * comporre un segmento senza avere una connessione da cui prendere gli
     * indirizzi, e il caso che conta — un ritardatario di una connessione
     * appena chiusa — non merita quel codice. */
    if (c == NULL) { g_st.scartati++; return; }

    if (flag & TCP_RST) { tcp_chiudi_dura(c, -ECONNRESET); return; }

    c->finestra = finestra;

    /* --- apertura: aspettavamo SYN+ACK --- */
    if (c->stato == S_SYN_INVIA) {
        if (!(flag & TCP_SYN) || !(flag & TCP_ACK)) return;
        if (ack != c->snd_nxt) return;      /* non conferma il nostro SYN */

        c->rcv_nxt = seq + 1u;
        c->snd_una = ack;
        c->stato   = S_APERTA;
        c->rto_scade = 0;

        tcp_manda(c, TCP_ACK, NULL, 0);     /* terzo passo della stretta */

        if (c->attesa_pid != 0 && c->attesa_tipo == IP_MSG_TCP_APRI) {
            IpEsito e;

            e.codice = (int)tcp_id(c);
            ipc_send(c->attesa_pid, IP_MSG_ESITO, &e, sizeof(e));
            c->attesa_pid = 0;
        }
        return;
    }

    if (flag & TCP_ACK) tcp_conferma(c, ack);

    /* --- dati --- */
    if (n > 0) {
        if (seq != c->rcv_nxt) {
            /* ! FUORI SEQUENZA: SI SCARTA E SI RICONFERMA. Non si tiene
             * da parte (vedi ip_proto.h). Il duplicato di ACK dice
             * all'altro dove siamo rimasti, e lui ritrasmettera'. */
            tcp_manda(c, TCP_ACK, NULL, 0);
        } else if (c->rx_len + n > TCP_BUF) {
            /* Il buffer e' pieno: non si conferma quello che non si puo'
             * tenere, o l'altro lo considererebbe consegnato. */
            tcp_manda(c, TCP_ACK, NULL, 0);
        } else {
            memcpy(c->rx + c->rx_len, seg + off, n);
            c->rx_len  += n;
            c->rcv_nxt += n;
            tcp_manda(c, TCP_ACK, NULL, 0);
        }
    }

    /* --- FIN --- */
    if ((flag & TCP_FIN) && seq + n == c->rcv_nxt) {
        c->rcv_nxt++;
        tcp_manda(c, TCP_ACK, NULL, 0);

        if (c->stato == S_APERTA)      c->stato = S_FIN_SUO;
        else if (c->stato == S_FIN_MIO) {
            /* Chiusi entrambi i versi. Niente TIME_WAIT: aspettare due
             * minuti per un segmento in ritardo terrebbe occupata una
             * delle quattro connessioni per tutto quel tempo, e su questo
             * sistema quel prezzo e' piu' alto del rischio. */
            c->stato = S_MORTA;
            c->rto_scade = 0;
        }
    }

    /* Il nostro FIN e' stato confermato? */
    if ((c->stato == S_FIN_MIO || c->stato == S_ULTIMO_ACK) &&
        !seq_prima(c->snd_una, c->fin_seq + 1u)) {
        c->stato = (c->stato == S_ULTIMO_ACK) ? S_MORTA : c->stato;
        c->rto_scade = 0;
    }

    tcp_spingi(c);
    tcp_consegna(c);
}

/* =============================================================================
 * Scadenze: ritrasmissione e attese dei client
 * ============================================================================= */
static void tcp_scadenze(void)
{
    unsigned int ora = uptime_ms();
    unsigned int i;

    for (i = 0; i < IP_TCP_CONNESSIONI; i++) {
        Conn *c = &g_tcp[i];

        if (c->stato == S_LIBERA) continue;

        /* Attesa del client scaduta (apertura che non si completa). */
        if (c->attesa_pid != 0 && c->attesa_scade != 0 &&
            (int)(ora - c->attesa_scade) >= 0) {
            IpEsito e;

            e.codice = -ETIMEDOUT;
            ipc_send(c->attesa_pid, IP_MSG_ESITO, &e, sizeof(e));
            c->attesa_pid = 0;
            if (c->stato == S_SYN_INVIA) { c->stato = S_MORTA; c->rto_scade = 0; }
            continue;
        }

        if (c->rto_scade == 0 || (int)(ora - c->rto_scade) < 0) continue;

        if (++c->tentativi > TCP_TENTATIVI) {
            tcp_chiudi_dura(c, -ETIMEDOUT);
            continue;
        }

        /* ! L'INTERVALLO RADDOPPIA. Ritrasmettere sempre allo stesso
         * ritmo su una rete che non risponde significa aggiungere
         * traffico a una rete gia' in difficolta'. */
        c->rto_ms   *= 2u;
        c->rto_scade = ora + c->rto_ms;

        if (c->stato == S_SYN_INVIA) {
            c->snd_nxt = c->snd_una;
            tcp_manda(c, TCP_SYN, NULL, 0);
            c->snd_nxt = c->snd_una + 1u;
        } else if (c->stato == S_FIN_MIO || c->stato == S_ULTIMO_ACK) {
            c->snd_nxt = c->fin_seq;
            tcp_manda(c, TCP_ACK | TCP_FIN, NULL, 0);
            c->snd_nxt = c->fin_seq + 1u;
        } else {
            /* Si riparte dal primo non confermato: e' la ritrasmissione
             * "go-back-N", l'unica possibile senza SACK. */
            c->snd_nxt = c->snd_una;
            tcp_spingi(c);
        }
    }
}

/* =============================================================================
 * Richieste dei client
 * ============================================================================= */
static void tcp_apri(unsigned int cliente, const IpTcpApri *a)
{
    unsigned char hop[4];
    VoceArp      *v;
    Conn         *c = NULL;
    unsigned int  i;

    for (i = 0; i < IP_TCP_CONNESSIONI; i++) {
        /* Una connessione MORTA si riusa solo se il suo cliente ha gia'
         * letto tutto: buttare via dati non ancora consegnati sarebbe
         * perderli in silenzio. */
        if (g_tcp[i].stato == S_LIBERA ||
            (g_tcp[i].stato == S_MORTA && g_tcp[i].rx_len == 0 &&
             g_tcp[i].attesa_pid == 0)) {
            c = &g_tcp[i];
            break;
        }
    }
    if (c == NULL) { rispondi_esito(cliente, -ENFILE); return; }

    if (prossimo_salto(a->ip, hop) != 0) {
        rispondi_esito(cliente, -ENETUNREACH);
        return;
    }

    /* ! SERVE IL MAC SUBITO. La macchina a stati di TCP ha gia' i suoi
     * tempi; infilarci dentro anche l'attesa di un ARP vorrebbe dire due
     * scadenze annidate sulla stessa connessione. Se il MAC non c'e' si
     * chiede e si rifiuta con -EAGAIN: il client riprova fra un istante e
     * a quel punto la tabella ARP e' piena. Un client FTP lo fa senza
     * accorgersene; scriverlo qui costerebbe molto di piu'. */
    v = arp_cerca(hop);
    if (v == NULL) {
        arp_chiedi(hop);
        rispondi_esito(cliente, -EAGAIN);
        return;
    }

    memset(c, 0, sizeof(*c));
    memcpy(c->ip, a->ip, 4);
    memcpy(c->mac, v->mac, 6);
    c->porta_rem = a->porta;

    /* Porta locale effimera, saltando quelle gia' in uso. */
    for (i = 0; i < 1024u; i++) {
        unsigned int p = g_tcp_porta++;
        unsigned int j, libera = 1;

        if (g_tcp_porta > 65535u) g_tcp_porta = 49152u;
        for (j = 0; j < IP_TCP_CONNESSIONI; j++)
            if (g_tcp[j].stato != S_LIBERA && &g_tcp[j] != c &&
                g_tcp[j].porta_loc == p) libera = 0;
        if (libera) { c->porta_loc = p; break; }
    }
    if (c->porta_loc == 0) { c->stato = S_LIBERA; rispondi_esito(cliente, -ENFILE); return; }

    /* ! IL NUMERO DI SEQUENZA INIZIALE NON DEV'ESSERE PREVEDIBILE, ma
     * qui non c'e' una sorgente di casualita': si mescolano il tempo
     * trascorso e la porta, che e' meglio di zero e non e' sicurezza.
     * Con TLS sopra, l'attaccante che potrebbe indovinarlo ha comunque
     * bisogno di ben altro. */
    c->snd_una  = (uptime_ms() << 8) ^ (c->porta_loc << 16) ^ 0x45584F53u;
    c->snd_nxt  = c->snd_una;
    c->finestra = 1024;          /* provvisoria: la vera arriva col SYN+ACK */
    c->proprietario = cliente;

    c->stato    = S_SYN_INVIA;
    tcp_manda(c, TCP_SYN, NULL, 0);
    c->snd_nxt  = c->snd_una + 1u;

    c->rto_ms    = TCP_RTO_MS;
    c->rto_scade = uptime_ms() + c->rto_ms;
    c->tentativi = 0;

    /* Non si risponde adesso: la risposta e' l'esito della stretta di
     * mano, e arriva quando arriva. */
    c->attesa_pid   = cliente;
    c->attesa_tipo  = IP_MSG_TCP_APRI;
    c->attesa_scade = uptime_ms() +
                      (a->timeout_ms ? a->timeout_ms : TCP_APERTURA_MS);
}

static void tcp_invia(unsigned int cliente, const unsigned char *payload,
                      unsigned int len_msg)
{
    IpTcpDati    d;
    Conn        *c;
    unsigned int n, spazio;

    if (len_msg < sizeof(d)) { rispondi_esito(cliente, -EINVAL); return; }
    memcpy(&d, payload, sizeof(d));

    c = tcp_da_id(d.id, cliente);
    if (c == NULL) { rispondi_esito(cliente, -EBADF); return; }
    if (c->stato != S_APERTA && c->stato != S_FIN_SUO) {
        rispondi_esito(cliente, -ENOTCONN);
        return;
    }

    n = len_msg - sizeof(d);
    if (n > d.len) n = d.len;

    /* ! SI ACCETTA QUELLO CHE CI STA E SI DICE QUANTO. Rifiutare tutto
     * quando il buffer e' quasi pieno obbligherebbe il client a
     * ritentare l'intero blocco; accettarlo tutto vorrebbe dire scrivere
     * oltre il buffer. Il numero restituito e' quanti byte sono davvero
     * entrati, ed e' compito di chi scrive guardarlo. */
    spazio = TCP_BUF - c->tx_len;
    if (n > spazio) n = spazio;

    if (n > 0) {
        memcpy(c->tx + c->tx_len, payload + sizeof(d), n);
        c->tx_len += n;
        tcp_spingi(c);
    }

    rispondi_esito(cliente, (int)n);
}

static void tcp_ricevi(unsigned int cliente, const IpTcpRif *r)
{
    Conn *c = tcp_da_id(r->id, cliente);

    if (c == NULL) { rispondi_esito(cliente, -EBADF); return; }

    c->attesa_pid   = cliente;
    c->attesa_tipo  = IP_MSG_TCP_RICEVI;
    c->attesa_scade = 0;            /* nessuna scadenza: si aspettano i dati */
    tcp_consegna(c);
}

static void tcp_chiudi(unsigned int cliente, const IpTcpRif *r)
{
    Conn *c = tcp_da_id(r->id, cliente);

    if (c == NULL) { rispondi_esito(cliente, -EBADF); return; }

    if (c->stato == S_APERTA || c->stato == S_FIN_SUO) {
        /* Il FIN va DOPO i dati in coda: il suo numero di sequenza e'
         * quello che segue l'ultimo byte accodato, non quello attuale. */
        c->fin_seq = c->snd_una + c->tx_len;
        c->snd_nxt = c->fin_seq;
        tcp_manda(c, TCP_ACK | TCP_FIN, NULL, 0);
        c->snd_nxt = c->fin_seq + 1u;

        c->stato     = (c->stato == S_FIN_SUO) ? S_ULTIMO_ACK : S_FIN_MIO;
        c->rto_ms    = TCP_RTO_MS;
        c->rto_scade = uptime_ms() + c->rto_ms;
        c->tentativi = 0;
    } else {
        c->stato = S_MORTA;
    }

    rispondi_esito(cliente, 0);
}

static void tcp_info(unsigned int cliente, const IpTcpRif *r)
{
    Conn      *c = tcp_da_id(r->id, cliente);
    IpTcpInfo  info;

    memset(&info, 0, sizeof(info));
    if (c == NULL) { info.stato = IP_TCP_CHIUSA; }
    else {
        info.id         = tcp_id(c);
        info.stato      = tcp_stato_pubblico(c);
        info.in_coda_rx = c->rx_len;
        info.in_coda_tx = c->tx_len;
        memcpy(info.ip, c->ip, 4);
        info.porta      = c->porta_rem;
    }
    ipc_send(cliente, IP_MSG_TCP_INFO, &info, sizeof(info));
}

static void tratta_ip(const unsigned char *f, unsigned int len)
{
    unsigned int ihl, tot, frag, len_icmp;
    const unsigned char *ip = f + 14;

    if (len < ETH_INTEST + IP_INTEST) { g_st.scartati++; return; }
    if ((ip[0] >> 4) != 4) { g_st.scartati++; return; }

    ihl = (unsigned int)(ip[0] & 0x0F) * 4;
    if (ihl < IP_INTEST || len < ETH_INTEST + ihl) { g_st.scartati++; return; }

    if (somma_controllo(ip, ihl) != 0) { g_st.checksum_errati++; return; }

    tot = prendi16(ip + 2);
    if (tot < ihl || len < ETH_INTEST + tot) { g_st.scartati++; return; }

    /* ! FRAMMENTI SCARTATI E CONTATI. Il campo tiene i flag nei tre bit
     * alti: un pacchetto con "altri frammenti in arrivo" (0x2000) o con un
     * offset diverso da zero è un pezzo, non un datagramma. Contarli a
     * parte serve a distinguere «la rete non funziona» da «arrivano
     * pacchetti troppo grandi per la nostra MTU». */
    frag = prendi16(ip + 6);
    if ((frag & 0x2000) || (frag & 0x1FFF)) { g_st.frammenti++; return; }

    /* Per noi, oppure a tutti. Il secondo caso non e' una concessione: un
     * client DHCP non ha ancora un indirizzo, e la risposta del server
     * arriva percio' in broadcast. */
    if (!ip_uguali(ip + 16, g_cfg.ip) && !e_broadcast(ip + 16)) {
        g_st.scartati++;
        return;
    }

    g_st.ip_ricevuti++;

    if (ip[9] == IP_PROTO_UDP) { tratta_udp(f, ihl, tot); return; }
    if (ip[9] == IP_PROTO_TCP) { tratta_tcp(f, ihl, tot); return; }
    if (ip[9] != IP_PROTO_ICMP) return;   /* nient'altro, per ora */

    len_icmp = tot - ihl;
    if (len_icmp < 8) { g_st.scartati++; return; }
    if (somma_controllo(f + 14 + ihl, len_icmp) != 0) {
        g_st.checksum_errati++;
        return;
    }

    {
        const unsigned char *icmp = f + 14 + ihl;

        if (icmp[0] == ICMP_ECHO_RICH) {
            rispondi_echo(f, len, ihl, len_icmp);
            return;
        }

        if (icmp[0] == ICMP_ECHO_RISP &&
            g_att.stato == ATT_ECHO &&
            prendi16(icmp + 4) == g_id_icmp &&
            prendi16(icmp + 6) == g_att.seq) {

            IpEchoRisposta r;

            r.codice = 0;
            r.seq    = g_att.seq;
            r.rtt_ms = uptime_ms() - g_att.inizio;
            r.ttl    = ip[8];
            memcpy(r.da, ip + 12, 4);

            ipc_send(g_att.client_pid, IP_MSG_ECHO_R, &r, sizeof(r));
            g_att.stato = ATT_NULLA;
        }
    }
}

/* =============================================================================
 * Smistamento dei frame
 * ============================================================================= */
static void tratta_frame(const unsigned char *f, unsigned int len)
{
    unsigned int tipo;

    if (len < ETH_INTEST) { g_st.scartati++; return; }

    tipo = prendi16(f + 12);
    if (tipo == ETH_TIPO_ARP)     tratta_arp(f, len);
    else if (tipo == ETH_TIPO_IP) tratta_ip(f, len);
    else                          g_st.scartati++;
}

/* =============================================================================
 * Instradamento: dentro la maschera si va diretti, fuori al gateway
 * ============================================================================= */
static int prossimo_salto(const unsigned char *dest, unsigned char *hop)
{
    int i, locale = 1;

    for (i = 0; i < 4; i++)
        if ((dest[i] & g_cfg.maschera[i]) != (g_cfg.ip[i] & g_cfg.maschera[i]))
            locale = 0;

    if (locale) { memcpy(hop, dest, 4); return 0; }
    if (ip_nullo(g_cfg.gateway)) return -1;   /* fuori rete e senza gateway */

    memcpy(hop, g_cfg.gateway, 4);
    return 0;
}

/* =============================================================================
 * Scadenze
 * ============================================================================= */
/* Chiude l'operazione in corso rispondendo a chi l'aveva chiesta. La
 * risposta ha un TIPO diverso secondo l'operazione: un client che ha
 * mandato un IP_MSG_UDP_INVIA sta aspettando un IP_MSG_ESITO e non
 * riconoscerebbe mai un IP_MSG_ECHO_R — resterebbe fermo fino alla propria
 * scadenza senza capire perche'. */
static void chiudi_attesa(int codice)
{
    if (g_att.operazione == OP_UDP) {
        IpEsito e;

        e.codice = codice;
        ipc_send(g_att.client_pid, IP_MSG_ESITO, &e, sizeof(e));
    } else {
        IpEchoRisposta r;

        memset(&r, 0, sizeof(r));
        r.codice = codice;
        r.seq    = g_att.seq;
        ipc_send(g_att.client_pid, IP_MSG_ECHO_R, &r, sizeof(r));
    }
    g_att.stato = ATT_NULLA;
}

/* L'ARP e' arrivato: si fa quello per cui lo si stava aspettando. */
static void esegui_operazione(void)
{
    VoceArp *v = arp_cerca(g_att.hop);

    if (v == NULL) return;              /* non ancora: si riprova al giro dopo */

    if (g_att.operazione == OP_UDP) {
        int rc = udp_manda(v->mac, g_att.ip, g_att.udp_porta_loc,
                           g_att.udp_porta_dest, g_att.udp_dati, g_att.udp_len);
        chiudi_attesa(rc);
        return;
    }

    manda_echo();
}

static void controlla_scadenze(void)
{
    tcp_scadenze();

    unsigned int ora = uptime_ms();

    if (g_att.stato == ATT_NULLA) return;

    if ((int)(ora - g_att.scadenza) >= 0) {
        /* ! DUE ERRORI DIVERSI, NON UNO. Se siamo ancora fermi all'ARP,
         * l'host non ha nemmeno risposto a «chi sei»: non è che il ping si
         * è perso, è che sulla rete locale non c'è nessuno a
         * quell'indirizzo. Dirlo con -EHOSTUNREACH invece che con
         * -ETIMEDOUT risparmia a chi guarda mezz'ora di ricerche
         * nel posto sbagliato. */
        chiudi_attesa(g_att.stato == ATT_ARP ? -EHOSTUNREACH : -ETIMEDOUT);
        return;
    }

    if (g_att.stato == ATT_ARP && (int)(ora - g_att.arp_prossimo) >= 0) {
        arp_chiedi(g_att.hop);
        g_att.arp_prossimo = ora + ARP_RITENTA_MS;
        return;
    }

    /* L'ARP è arrivato mentre aspettavamo: adesso si può fare quello che si
     * stava aspettando di fare. Lo si fa qui e non dentro tratta_arp() per
     * non annidare un invio dentro l'elaborazione di un frame. */
    if (g_att.stato == ATT_ECHO && g_att.inizio == 0) esegui_operazione();
}

/* =============================================================================
 * Richieste dei client
 * ============================================================================= */
static void avvia_echo(unsigned int client, const IpEcho *e)
{
    IpEchoRisposta r;
    unsigned char  hop[4];

    if (g_att.stato != ATT_NULLA) {
        memset(&r, 0, sizeof(r));
        r.codice = -EBUSY;      /* una per volta: vedi ip_proto.h */
        r.seq    = e->seq;
        ipc_send(client, IP_MSG_ECHO_R, &r, sizeof(r));
        return;
    }

    if (prossimo_salto(e->ip, hop) != 0) {
        memset(&r, 0, sizeof(r));
        r.codice = -ENETUNREACH;
        r.seq    = e->seq;
        ipc_send(client, IP_MSG_ECHO_R, &r, sizeof(r));
        return;
    }

    g_att.operazione   = OP_ECHO;
    g_att.client_pid   = client;
    memcpy(g_att.ip, e->ip, 4);
    memcpy(g_att.hop, hop, 4);
    g_att.seq          = e->seq;
    g_att.payload      = e->payload;
    g_att.scadenza     = uptime_ms() + (e->timeout_ms ? e->timeout_ms : 2000);
    g_att.inizio       = 0;
    g_att.arp_prossimo = uptime_ms() + ARP_RITENTA_MS;

    if (arp_cerca(hop) != NULL) {
        g_att.stato = ATT_ECHO;
        manda_echo();
    } else {
        g_att.stato = ATT_ARP;
        arp_chiedi(hop);
    }
}

/* =============================================================================
 * Richieste UDP dei client
 * ============================================================================= */
static void rispondi_esito(unsigned int client, int codice)
{
    IpEsito e;

    e.codice = codice;
    ipc_send(client, IP_MSG_ESITO, &e, sizeof(e));
}

static void udp_apri(unsigned int client, const IpUdpApri *a)
{
    unsigned int porta = a->porta;
    int i, libero = -1;

    /* Porta 0 = «scegline una tu». Si prende dalla fascia effimera, che
     * IANA riserva proprio a questo, e si salta quelle gia' aperte. */
    if (porta == 0) {
        int tentativi;

        for (tentativi = 0; tentativi < 1024; tentativi++) {
            porta = g_porta_effimera++;
            if (g_porta_effimera > 65535) g_porta_effimera = 49152;
            if (udp_cerca(porta) < 0) break;
        }
    }

    if (porta == 0 || porta > 65535) { rispondi_esito(client, -EINVAL); return; }

    /* ! UNA PORTA GIA' APERTA NON SI CONSEGNA DUE VOLTE. Senza questo
     * controllo due client si dividerebbero i datagrammi a caso, e il
     * secondo sembrerebbe funzionare a intermittenza. */
    if (udp_cerca(porta) >= 0) { rispondi_esito(client, -EADDRINUSE); return; }

    for (i = 0; i < UDP_PORTE; i++) if (!g_udp[i].usata) { libero = i; break; }
    if (libero < 0) { rispondi_esito(client, -ENFILE); return; }

    g_udp[libero].porta        = porta;
    g_udp[libero].proprietario = client;
    g_udp[libero].lettore      = 0;
    g_udp[libero].usata        = 1;

    /* Si risponde con il NUMERO di porta assegnato, non con uno zero: chi
     * ha chiesto «una qualunque» deve sapere quale gli e' toccata. */
    rispondi_esito(client, (int)porta);
}

static void udp_chiudi(unsigned int client, const IpUdpApri *a)
{
    int i = udp_cerca(a->porta);

    if (i < 0) { rispondi_esito(client, -ENOENT); return; }
    if (g_udp[i].proprietario != client) { rispondi_esito(client, -EPERM); return; }

    g_udp[i].usata = 0;
    rispondi_esito(client, 0);
}

static void udp_invia(unsigned int client, const unsigned char *payload,
                      unsigned int len_msg)
{
    IpUdpInvia    inv;
    unsigned int  n;
    unsigned char hop[4];
    VoceArp      *v;
    int           i;

    if (len_msg < sizeof(inv)) { rispondi_esito(client, -EINVAL); return; }
    memcpy(&inv, payload, sizeof(inv));
    n = len_msg - sizeof(inv);

    if (n > UDP_CARICO_MAX) { rispondi_esito(client, -EMSGSIZE); return; }

    i = udp_cerca(inv.porta_locale);
    if (i < 0 || g_udp[i].proprietario != client) {
        rispondi_esito(client, -EPERM);
        return;
    }

    /* A «tutti» non si chiede il MAC: lo si sa. E' il caso del client
     * DHCP, che deve poter parlare prima di avere un indirizzo — se
     * dovesse fare ARP non partirebbe mai. */
    if (e_broadcast(inv.ip)) {
        unsigned char tutti[6];
        int k;

        for (k = 0; k < 6; k++) tutti[k] = 0xFF;
        rispondi_esito(client,
                       udp_manda(tutti, inv.ip, inv.porta_locale,
                                 inv.porta, payload + sizeof(inv), n));
        return;
    }

    if (prossimo_salto(inv.ip, hop) != 0) {
        rispondi_esito(client, -ENETUNREACH);
        return;
    }

    v = arp_cerca(hop);
    if (v != NULL) {
        rispondi_esito(client,
                       udp_manda(v->mac, inv.ip, inv.porta_locale,
                                 inv.porta, payload + sizeof(inv), n));
        return;
    }

    /* MAC sconosciuto: si tiene da parte il datagramma e si chiede l'ARP.
     * La risposta al client arriva quando il pacchetto e' davvero partito,
     * o quando si rinuncia — cosi' chi ha chiamato non deve sapere che
     * esiste un ARP di mezzo.
     *
     * ! UNA SOLA OPERAZIONE IN SOSPESO. Se ce n'e' gia' una si rifiuta
     * subito invece di accodare: una coda vorrebbe le sue scadenze, e
     * -EBUSY e' una risposta che il chiamante puo' gestire. */
    if (g_att.stato != ATT_NULLA) { rispondi_esito(client, -EBUSY); return; }

    g_att.operazione      = OP_UDP;
    g_att.client_pid      = client;
    memcpy(g_att.ip, inv.ip, 4);
    memcpy(g_att.hop, hop, 4);
    g_att.udp_porta_dest  = inv.porta;
    g_att.udp_porta_loc   = inv.porta_locale;
    g_att.udp_len         = n;
    if (n) memcpy(g_att.udp_dati, payload + sizeof(inv), n);
    g_att.scadenza        = uptime_ms() + 2000;
    g_att.inizio          = 0;
    g_att.arp_prossimo    = uptime_ms() + ARP_RITENTA_MS;
    g_att.stato           = ATT_ARP;

    arp_chiedi(hop);
}

static void udp_ricevi(unsigned int client, const IpUdpApri *a)
{
    int i = udp_cerca(a->porta);

    if (i < 0 || g_udp[i].proprietario != client) {
        rispondi_esito(client, -EPERM);
        return;
    }

    /* Non si risponde adesso: si ricorda chi aspetta, e gli si scrive
     * quando arriva un datagramma. Vedi ip_proto.h sul perche' lo stack
     * non spinge mai niente di sua iniziativa. */
    g_udp[i].lettore = client;
}

static void manda_tabella_arp(unsigned int client)
{
    IpArpTabella t;
    unsigned int ora = uptime_ms();
    int i;

    memset(&t, 0, sizeof(t));
    for (i = 0; i < IP_ARP_VOCI; i++) {
        if (!g_arp[i].usata) continue;
        if ((int)(ora - g_arp[i].scadenza) >= 0) continue;
        memcpy(t.voce[t.n].ip, g_arp[i].ip, 4);
        memcpy(t.voce[t.n].mac, g_arp[i].mac, 6);
        t.voce[t.n].scade_fra_ms = g_arp[i].scadenza - ora;
        t.n++;
    }
    ipc_send(client, IP_MSG_ARP_R, &t, sizeof(t));
}

/* =============================================================================
 * Collegamento al driver di scheda
 * ============================================================================= */
static int aggancia_driver(void)
{
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    NetStato      s;
    int           tentativi;

    g_pid_rete = ipc_lookup(NET_SERVIZIO_0);
    if (g_pid_rete <= 0) {
        printf("ip: il servizio '%s' non e' attivo.\n", NET_SERVIZIO_0);
        printf("    Avvia prima la scheda:  netdetect -c\n");
        return -1;
    }

    if (ipc_send(g_pid_rete, NET_MSG_INFO, NULL, 0) < 0) return -1;

    for (tentativi = 0; tentativi < 8; tentativi++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return -1;
        if ((int)meta.sender_pid != g_pid_rete) continue;
        if (meta.tipo == NET_MSG_STATO) break;
    }
    if (meta.tipo != NET_MSG_STATO || meta.len < sizeof(s)) {
        printf("ip: il driver di rete non ha detto chi e'.\n");
        return -1;
    }

    memcpy(&s, buf, sizeof(s));
    memcpy(g_mac, s.mac, 6);
    memcpy(g_st.mac, s.mac, 6);
    return 0;
}

/* =============================================================================
 * Ciclo principale
 * ============================================================================= */
static void servi(void)
{
    IpcMessage    meta;
    unsigned char payload[IPC_MSG_MAX_DATA];

    riarma_ricezione();

    for (;;) {
        int r = ipc_recv_timeout(&meta, payload, sizeof(payload), GIRO_MS);

        if (r == 0) {
            if ((int)meta.sender_pid == g_pid_rete) {
                if (meta.tipo == NET_MSG_FRAME) {
                    /* Prima si riarma, poi si guarda: vedi il commento di
                     * testa sul perché l'ordine conta. */
                    g_ricevi_armato = 0;
                    riarma_ricezione();
                    tratta_frame(payload, meta.len);
                } else if (meta.tipo == NET_MSG_ESITO) {
                    NetEsito e;

                    if (meta.len >= sizeof(e)) {
                        memcpy(&e, payload, sizeof(e));
                        if (e.codice != 0) g_st.scartati++;
                    }
                }
            } else {
                switch (meta.tipo) {

                case IP_MSG_CONFIG: {
                    IpEsito  es;
                    IpConfig c;

                    es.codice = -EINVAL;
                    if (meta.len >= sizeof(c)) {
                        memcpy(&c, payload, sizeof(c));
                        g_cfg = c;
                        g_st.cfg = c;
                        es.codice = 0;
                    }
                    ipc_send(meta.sender_pid, IP_MSG_ESITO, &es, sizeof(es));
                    break;
                }

                case IP_MSG_STATO:
                    g_st.cfg = g_cfg;
                    ipc_send(meta.sender_pid, IP_MSG_STATO_R, &g_st, sizeof(g_st));
                    break;

                case IP_MSG_ECHO: {
                    IpEcho e;

                    if (meta.len >= sizeof(e)) {
                        memcpy(&e, payload, sizeof(e));
                        avvia_echo(meta.sender_pid, &e);
                    }
                    break;
                }

                case IP_MSG_ARP:
                    manda_tabella_arp(meta.sender_pid);
                    break;

                case IP_MSG_UDP_APRI: {
                    IpUdpApri a;

                    if (meta.len >= sizeof(a)) {
                        memcpy(&a, payload, sizeof(a));
                        udp_apri(meta.sender_pid, &a);
                    } else rispondi_esito(meta.sender_pid, -EINVAL);
                    break;
                }

                case IP_MSG_UDP_CHIUDI: {
                    IpUdpApri a;

                    if (meta.len >= sizeof(a)) {
                        memcpy(&a, payload, sizeof(a));
                        udp_chiudi(meta.sender_pid, &a);
                    } else rispondi_esito(meta.sender_pid, -EINVAL);
                    break;
                }

                case IP_MSG_UDP_INVIA:
                    udp_invia(meta.sender_pid, payload, meta.len);
                    break;

                case IP_MSG_TCP_APRI: {
                    IpTcpApri a;

                    if (meta.len >= sizeof(a)) {
                        memcpy(&a, payload, sizeof(a));
                        tcp_apri(meta.sender_pid, &a);
                    } else rispondi_esito(meta.sender_pid, -EINVAL);
                    break;
                }

                case IP_MSG_TCP_INVIA:
                    tcp_invia(meta.sender_pid, payload, meta.len);
                    break;

                case IP_MSG_TCP_RICEVI:
                case IP_MSG_TCP_CHIUDI:
                case IP_MSG_TCP_STATO: {
                    IpTcpRif r;

                    if (meta.len < sizeof(r)) {
                        rispondi_esito(meta.sender_pid, -EINVAL);
                        break;
                    }
                    memcpy(&r, payload, sizeof(r));
                    if (meta.tipo == IP_MSG_TCP_RICEVI)      tcp_ricevi(meta.sender_pid, &r);
                    else if (meta.tipo == IP_MSG_TCP_CHIUDI) tcp_chiudi(meta.sender_pid, &r);
                    else                                     tcp_info(meta.sender_pid, &r);
                    break;
                }

                case IP_MSG_UDP_RICEVI: {
                    IpUdpApri a;

                    if (meta.len >= sizeof(a)) {
                        memcpy(&a, payload, sizeof(a));
                        udp_ricevi(meta.sender_pid, &a);
                    } else rispondi_esito(meta.sender_pid, -EINVAL);
                    break;
                }

                default: {
                    IpEsito es;

                    es.codice = -EINVAL;
                    ipc_send(meta.sender_pid, IP_MSG_ESITO, &es, sizeof(es));
                    break;
                }
                }
            }
        }

        controlla_scadenze();

        /* Se per qualsiasi motivo la richiesta di ricezione non è in volo
         * — il driver era occupato, l'invio è fallito — si riprova. Senza,
         * una singola ipc_send fallita spegnerebbe la ricezione per
         * sempre. */
        riarma_ricezione();
    }
}

/* =============================================================================
 * main
 * ============================================================================= */
static int leggi_ip(const char *s, unsigned char *out)
{
    int i, v, cifre;

    for (i = 0; i < 4; i++) {
        v = 0; cifre = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; cifre++; }
        if (cifre == 0 || v > 255) return 0;
        out[i] = (unsigned char)v;
        if (i < 3) { if (*s != '.') return 0; s++; }
    }
    return (*s == '\0');
}

static void stampa_ip(const unsigned char *p)
{
    printf("%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

static void stampa_config(void)
{
    printf("ip: indirizzo  "); stampa_ip(g_cfg.ip);      printf("\n");
    printf("    maschera   "); stampa_ip(g_cfg.maschera); printf("\n");
    printf("    gateway    ");
    if (ip_nullo(g_cfg.gateway)) printf("nessuno\n");
    else { stampa_ip(g_cfg.gateway); printf("\n"); }
    printf("    MAC        %02x:%02x:%02x:%02x:%02x:%02x\n",
           g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
}

static void uso(void)
{
    printf("uso: ip.drv [-a IND] [-m MASCHERA] [-g GATEWAY]\n");
    printf("     ip.drv -s        stampa la configurazione ed esce\n");
    printf("     ip.drv -i        dice se serve su questa macchina ed esce\n\n");
    printf("Senza argomenti usa 10.0.2.15/255.255.255.0 gw 10.0.2.2,\n");
    printf("cioe' i valori della rete 'user' di QEMU.\n");
}

int main(int argc, char **argv)
{
    int solo_stato = 0, i, rc;

    /* Valori predefiniti: la rete "user" di QEMU. Non sono una scelta
     * arbitraria — sono gli unici indirizzi utilizzabili finche' non c'e'
     * un client DHCP, perche' lo slirp non ne assegna altri. */
    g_cfg.ip[0] = 10; g_cfg.ip[1] = 0; g_cfg.ip[2] = 2; g_cfg.ip[3] = 15;
    g_cfg.maschera[0] = 255; g_cfg.maschera[1] = 255;
    g_cfg.maschera[2] = 255; g_cfg.maschera[3] = 0;
    g_cfg.gateway[0] = 10; g_cfg.gateway[1] = 0;
    g_cfg.gateway[2] = 2;  g_cfg.gateway[3] = 2;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            /* La convenzione comune a tutti i driver: sonda, dice cosa ha
             * trovato ed esce con 0 se si applica a questa macchina.
             *
             * ! SI RISPONDE PRIMA DI aggancia_driver(), e la risposta e'
             * sempre si'. Questo non e' un driver di periferica: e' lo
             * stack IP, software puro, che sta sopra QUALUNQUE scheda si
             * riesca ad accendere. Legarne la presenza a una scheda gia'
             * attiva darebbe sempre «no» — al momento della sonda nessun
             * driver di rete e' ancora partito — e su ogni macchina si
             * finirebbe con le schede installate e lo stack no. */
            printf("ip: stack TCP/IP, ICMP e UDP — software, nessuna "
                   "periferica propria\n");
            printf("    Si applica ovunque ci sia una scheda di rete.\n");
            return 0;
        }
        if (strcmp(argv[i], "-s") == 0) solo_stato = 1;
        else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
            if (!leggi_ip(argv[++i], g_cfg.ip)) { uso(); return 1; }
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            if (!leggi_ip(argv[++i], g_cfg.maschera)) { uso(); return 1; }
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            if (!leggi_ip(argv[++i], g_cfg.gateway)) { uso(); return 1; }
        } else { uso(); return 1; }
    }

    if (aggancia_driver() != 0) return 1;

    /* L'identificatore delle nostre richieste echo: il PID. Deve solo
     * distinguere le nostre risposte da quelle di altri sulla stessa
     * rete, e il PID è un numero che nessun altro processo ha. */
    g_id_icmp = (unsigned int)getpid() & 0xFFFF;

    g_st.cfg = g_cfg;

    if (solo_stato) { stampa_config(); return 0; }

    rc = ipc_register(IP_SERVIZIO);
    if (rc < 0) {
        printf("ip: ipc_register('%s') fallita (%d) — esco\n", IP_SERVIZIO, rc);
        return 1;
    }

    stampa_config();
    printf("ip: servizio '%s' attivo\n", IP_SERVIZIO);

    servi();
    return 0;
}

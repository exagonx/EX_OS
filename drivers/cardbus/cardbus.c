/* =============================================================================
 * drivers/cardbus/cardbus.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * IL PONTE CARDBUS — lo slot PCMCIA di un portatile
 *
 *     cardbus.drv            dice cosa c'e' e in che stato e'
 *     cardbus.drv -accendi   lo configura e guarda nello slot
 *     cardbus.drv -i         c'e' un ponte CardBus su questa macchina?
 *
 * -----------------------------------------------------------------------------
 * ! NON SERVE IL REVERSE ENGINEERING, E VALE LA PENA DIRE PERCHE'
 *
 * Un ponte CardBus e' descritto dalla specifica Yenta, che e' pubblica: i
 * registri di configurazione stanno nell'intestazione PCI di TIPO 2 — quella
 * che il PCI definisce apposta per questi ponti — e i registri di socket sono
 * sedici parole con un significato scritto. La TI PCI1410 di questo portatile
 * la implementa come tutti gli altri.
 *
 * E' la stessa ragione per cui l'audio non ha voluto un driver nuovo: quando
 * una cosa ha una specifica pubblica, scavare nel driver del costruttore
 * significa faticare per riscoprire quello che c'e' scritto.
 *
 * -----------------------------------------------------------------------------
 * ! SU QUESTO PORTATILE IL BIOS NON L'HA CONFIGURATO, ed e' il motivo per cui
 * questo file esiste. Dal referto:
 *
 *     comando 0000     il ponte non decodifica ne' porte ne' memoria
 *     cfg 0x10 = 0     la base dei registri di socket non e' assegnata
 *     cfg 0x18 = 0     i numeri di bus non sono assegnati
 *     irq ff           nessuna linea di interrupt
 *
 * Cioe': lo slot c'e' e non lo vede nessuno. Su Windows lo configura il
 * gestore PCI del sistema quando parte; senza qualcuno che lo faccia, una
 * scheda infilata li' dentro non compare da nessuna parte.
 *
 * -----------------------------------------------------------------------------
 * ! COSA FA E COSA NON FA, DETTO SUBITO
 *
 *   - configura il ponte e dice cosa c'e' nello slot. Una scheda CARDBUS —
 *     cioe' a 32 bit — e' un normale dispositivo PCI sul bus secondario, e da
 *     quel momento pci.drv la enumera come qualunque altra: se e' una scheda
 *     di rete, netdetect la trova.
 *   - NON GESTISCE LE PC CARD A 16 BIT. Sono il protocollo PCMCIA vecchio, con
 *     la sua finestra di attributi e il suo tuple parser: un altro mondo, e
 *     grande. Se ce n'e' una nello slot, questo driver lo dice e si ferma li'.
 *   - non assegna risorse alla scheda infilata. Le finestre che il ponte
 *     inoltra restano quelle che erano: una scheda che ha bisogno di memoria
 *     propria la vedra' enumerata e non funzionante, e questo e' onesto
 *     dirlo prima.
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"

/* +0.001 a ogni modifica: `cardbus.drv -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("cardbus.drv", "0.001");

#define PCI_INDIRIZZO   0xCF8
#define PCI_DATO        0xCFC
#define ATTESA_PCI_MS   5000

#define CLASSE_PONTE    0x06
#define SOTTO_CARDBUS   0x07

/* --- L'intestazione di tipo 2, dove differisce dalla tipo 0 --------------- */
#define C_SOCKET_BASE   0x10    /* dove mettere i registri di socket */
#define C_BUS_PRIMARIO  0x18
#define C_BUS_CARDBUS   0x19
#define C_BUS_SUBORD    0x1A
#define C_LATENZA_CB    0x1B
#define C_MEM0_BASE     0x1C
#define C_MEM0_LIMITE   0x20
#define C_MEM1_BASE     0x24
#define C_MEM1_LIMITE   0x28
#define C_IO0_BASE      0x2C
#define C_IO0_LIMITE    0x30
#define C_IO1_BASE      0x34
#define C_IO1_LIMITE    0x38
#define C_CONTROLLO     0x3E
#define C_SOTTOSISTEMA  0x40

/* --- I registri di socket, dalla specifica Yenta ------------------------- */
#define S_EVENTO        0x00
#define S_MASCHERA      0x04
#define S_PRESENZA      0x08    /* cosa c'e' nello slot: il registro che conta */
#define S_FORZA         0x0C
#define S_CONTROLLO     0x10

/* S_PRESENZA, bit per bit. Sono quelli della specifica, non deduzioni. */
#define P_CARDBUS       0x00000001u   /* c'e' una scheda CardBus (32 bit) */
#define P_16BIT         0x00000002u   /* c'e' una PC Card a 16 bit */
#define P_CICLO         0x00000008u   /* sta accendendo o spegnendo */
#define P_CD1           0x00000010u   /* rilevamento 1: ZERO = inserita */
#define P_CD2           0x00000020u   /* rilevamento 2: ZERO = inserita */
#define P_5V            0x00000400u
#define P_3V            0x00000800u

/* =============================================================================
 * Stato
 * ========================================================================== */
static unsigned int g_bus = 0, g_slot = 0, g_funzione = 0;
static unsigned int g_venditore = 0, g_dispositivo = 0, g_rev = 0;
static unsigned int g_socket = 0;       /* la base che assegniamo o troviamo */
static unsigned int g_base_voluta = 0;  /* -base: detta da chi lancia */

/* Le finestre di memoria gia' occupate, raccolte dal servizio PCI. */
#define OCCUPATE_MAX 64
static unsigned int g_occ_base[OCCUPATE_MAX];
static int          g_occupate = 0;

/* =============================================================================
 * Configurazione PCI, letta e scritta direttamente
 *
 * ! IL SERVIZIO PCI NON SA SCRIVERE, ed e' una scelta scritta in
 * pci_proto.h: un server di enumerazione non deve toccare l'hardware di
 * nessuno. Qui pero' bisogna scrivere davvero — i numeri di bus, la base del
 * socket — e allora si passa dalle porte, che e' quello che un .drv puo' fare.
 * ========================================================================== */
static unsigned int cfg32(unsigned int off)
{
    unsigned int ind = 0x80000000u | (g_bus << 16) | (g_slot << 11) |
                       (g_funzione << 8) | (off & 0xFC);
    unsigned int v;

    if (ioport_out32(PCI_INDIRIZZO, ind) != 0) return 0xFFFFFFFFu;
    if (ioport_in32(PCI_DATO, &v) != 0)        return 0xFFFFFFFFu;
    return v;
}

static void cfg32_scrivi(unsigned int off, unsigned int v)
{
    unsigned int ind = 0x80000000u | (g_bus << 16) | (g_slot << 11) |
                       (g_funzione << 8) | (off & 0xFC);

    if (ioport_out32(PCI_INDIRIZZO, ind) != 0) return;
    ioport_out32(PCI_DATO, v);
}

/* Un byte dentro una parola, senza toccare gli altri tre. */
static void cfg8_scrivi(unsigned int off, unsigned int v)
{
    unsigned int allineato = off & ~3u;
    unsigned int posto     = (off & 3) * 8;
    unsigned int p         = cfg32(allineato);

    p &= ~(0xFFu << posto);
    p |= (v & 0xFF) << posto;
    cfg32_scrivi(allineato, p);
}

/* =============================================================================
 * Dove mettere i registri di socket
 *
 * ! NON SI INDOVINA UN INDIRIZZO, SI CERCA UNO LIBERO. Assegnare al ponte una
 * finestra che si sovrappone a quella di un altro dispositivo vuol dire due
 * cose che rispondono allo stesso indirizzo: e' il modo di bloccare una
 * macchina in un punto che poi nessuno ritrova.
 *
 * Percio' si chiede al servizio PCI l'elenco di TUTTI i dispositivi, si
 * raccolgono le loro finestre di memoria, e si prende il primo posto libero.
 *
 * ! E SI CERCA SOPRA 0xE0000000, che e' l'altra meta' della cautela. Sotto
 * quell'indirizzo c'e' la RAM, e un ponte che decodifica un indirizzo di RAM
 * la copre: il sintomo sarebbe una macchina che si comporta male in un punto
 * che non c'entra niente con lo slot PCMCIA. Su questo portatile i
 * dispositivi stanno tutti fra 0xE0000000 e 0xE9000000, cioe' e' li' che il
 * ponte nord instrada la memoria.
 * ========================================================================== */
#define ZONA_INIZIO   0xE0000000u
#define ZONA_FINE     0xF0000000u
#define SOCKET_BYTE   0x1000        /* la specifica ne chiede 4096 */

static int raccogli_occupate(int pid)
{
    unsigned int n;

    for (n = 0; n < 64; n++) {
        PciRichiesta   r;
        PciDispositivo d;
        IpcMessage     meta;
        unsigned char  buf[IPC_MSG_MAX_DATA];
        int i, tentativi;

        r.ordinale    = n;
        r.classe      = PCI_QUALUNQUE;
        r.sottoclasse = PCI_QUALUNQUE;
        r.venditore   = PCI_QUALUNQUE;
        r.dispositivo = PCI_QUALUNQUE;

        if (ipc_send(pid, PCI_MSG_ELENCA, &r, sizeof(r)) < 0) return -1;

        for (tentativi = 0; tentativi < 8; tentativi++) {
            if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return -1;
            if ((int)meta.sender_pid == pid) break;
        }
        if ((int)meta.sender_pid != pid) return -1;
        if (meta.tipo == PCI_MSG_FINE) break;
        if (meta.tipo != PCI_MSG_DISPOSITIVO || meta.len < sizeof(d)) return -1;
        memcpy(&d, buf, sizeof(d));

        /* ! SI SCARTANO I PONTI PCI-PCI E CARDBUS, NON TUTTI I PONTI, e la
         * differenza e' importante quanto la somiglianza.
         *
         * Quei due hanno l'intestazione di tipo 1 e 2, dove dall'offset 0x18 in
         * poi NON ci sono BAR: ci sono i numeri di bus e le finestre inoltrate.
         * Letti come indirizzi danno cose come 0x0220a0a0 o 0xe210e210, che
         * sono due meta' di registri diversi messe insieme.
         *
         * ! IL PONTE NORD INVECE HA BAR VERE, e la sua e' la piu' importante
         * di tutte: e' l'APERTURA AGP, decine di megabyte che nessun altro
         * deve toccare. Scartarla perche' «e' un ponte» vuol dire scegliere un
         * indirizzo proprio li' dentro — provato a tavolino sui dati
         * dell'Acer, ed e' esattamente quello che succedeva. */
        if (d.classe == CLASSE_PONTE &&
            (d.sottoclasse == SOTTO_CARDBUS || d.sottoclasse == 0x04))
            continue;

        for (i = 0; i < 6; i++) {
            if (d.bar[i] == 0 || d.bar_io[i]) continue;
            if (g_occupate >= OCCUPATE_MAX) break;
            g_occ_base[g_occupate++] = d.bar[i];
        }
    }
    return 0;
}

/* Il buco piu' grande fra le finestre note, e ci si mette in mezzo.
 *
 * ! IL PRIMO POSTO LIBERO NON VA BENE, e questa e' costata una prova fatta a
 * tavolino sui dati veri dell'Acer prima di scrivere una riga sul ferro.
 * Cercando dal basso con un margine di un megabyte, l'algoritmo sceglieva
 * 0xE0100000 — cioe' dentro l'APERTURA AGP del ponte nord, che comincia a
 * 0xE0000000 ed e' larga decine di megabyte. Un margine fisso non la vede,
 * perche' non e' una finestra come le altre: e' una finestra grande.
 *
 * ! LA LARGHEZZA DELLE FINESTRE ALTRUI NON SI PUO' SAPERE, e pci_proto.h
 * spiega perche': misurarla vorrebbe dire scrivere nei BAR di dispositivi che
 * stanno lavorando. Non sapendo quanto sono larghe, la cosa piu' sicura non e'
 * stare a un margine da ognuna: e' stare il piu' LONTANO POSSIBILE da tutte,
 * cioe' in mezzo al vuoto piu' grande.
 *
 * Sull'Acer i dispositivi veri stanno a 0xE2000000, 0xE2005000, 0xE2100000 e
 * 0xE8000000: il buco piu' grande e' fra gli ultimi due, novantacinque
 * megabyte, e il centro cade attorno a 0xE5000000. Da li' a qualunque cosa
 * nota ci sono quarantasette megabyte.
 */
#define BUCO_MINIMO  0x1000000u      /* sotto i 16 MB non ci si fida */

static unsigned int trova_posto(void)
{
    unsigned int inizio_migliore = 0, larghezza_migliore = 0;
    unsigned int precedente = ZONA_INIZIO;
    int i, j;

    /* ordinamento a scambio: sono al massimo sessantaquattro numeri */
    for (i = 0; i < g_occupate; i++)
        for (j = i + 1; j < g_occupate; j++)
            if (g_occ_base[j] < g_occ_base[i]) {
                unsigned int t = g_occ_base[i];
                g_occ_base[i] = g_occ_base[j];
                g_occ_base[j] = t;
            }

    /* ! SI GUARDANO SOLO I VUOTI CHIUSI FRA DUE FINESTRE NOTE, e il perche' e'
     * la seconda cosa che questa funzione ha imparato a tavolino.
     *
     * Del vuoto DOPO l'ultimo dispositivo non si sa niente: quel dispositivo
     * potrebbe estendersi fino in fondo, e sull'Acer si estende davvero — il
     * framebuffer video comincia a 0xE8000000 ed e' largo centoventotto
     * megabyte, cioe' occupa tutto quello che resta. La prima versione
     * sceglieva 0xEC000000 credendolo vuoto: e' il centro del framebuffer.
     *
     * Un vuoto chiuso a destra da un'altra finestra e' invece garantito: se
     * quel dispositivo comincia li', quello prima non puo' arrivarci. */
    for (i = 0; i < g_occupate; i++) {
        unsigned int fine = g_occ_base[i];

        if (fine > precedente && fine - precedente > larghezza_migliore) {
            larghezza_migliore = fine - precedente;
            inizio_migliore    = precedente;
        }
        if (g_occ_base[i] >= precedente) precedente = g_occ_base[i];
    }

    if (larghezza_migliore < BUCO_MINIMO) return 0;

    /* In mezzo, allineato a 4 KB come la specifica chiede. */
    return (inizio_migliore + larghezza_migliore / 2) & ~0xFFFu;
}

/* =============================================================================
 * Lo slot
 * ========================================================================== */
static void racconta_presenza(unsigned int p)
{
    printf("cardbus: registro di presenza = %08x\n", p);

    /* ! I DUE BIT DI RILEVAMENTO SONO A ZERO QUANDO LA SCHEDA C'E', e non e'
     * un capriccio della specifica: sono due contatti che la scheda mette a
     * massa, e sono due perche' devono chiudersi tutti e due — una scheda
     * infilata storta ne chiude uno solo, e cosi' si sa che e' storta invece
     * di provare ad accenderla. */
    if (p & (P_CD1 | P_CD2)) {
        if ((p & (P_CD1 | P_CD2)) == (P_CD1 | P_CD2))
            printf("         lo slot e' vuoto.\n");
        else
            printf("         c'e' qualcosa ma non e' infilato fino in fondo:\n"
                   "         un solo contatto di rilevamento su due.\n");
        return;
    }

    if (p & P_CARDBUS) {
        printf("         c'e' una scheda CARDBUS (32 bit).\n");
        printf("         E' un dispositivo PCI sul bus %u: `netdetect` e\n",
               (cfg32(C_BUS_PRIMARIO) >> 8) & 0xFF);
        printf("         `mappa.drv` la vedono come qualunque altra scheda.\n");
    } else if (p & P_16BIT) {
        printf("         c'e' una PC CARD a 16 bit.\n");
        printf("         ! QUESTO DRIVER NON LE GESTISCE: e' il protocollo\n");
        printf("         PCMCIA vecchio, con la finestra degli attributi e il\n");
        printf("         suo formato di descrizione. Un altro lavoro, e non\n");
        printf("         piccolo.\n");
    } else {
        printf("         c'e' una scheda ma non dice di che tipo: puo'\n");
        printf("         essere ancora in accensione.\n");
    }

    if (p & P_CICLO) printf("         sta accendendo o spegnendo lo slot.\n");
    if (p & P_5V)    printf("         alimentazione 5 V disponibile.\n");
    if (p & P_3V)    printf("         alimentazione 3,3 V disponibile.\n");
}

static int guarda_slot(void)
{
    MmioZona m;
    volatile unsigned int *s;
    unsigned int p;

    m.fisico = g_socket;
    m.byte   = SOCKET_BYTE;
    m.virt   = 0;

    if (mmio_map(&m) != 0) {
        printf("cardbus: mmio_map(0x%08x) fallita: i registri di socket non\n",
               g_socket);
        printf("         si raggiungono.\n");
        return -1;
    }

    s = (volatile unsigned int *)m.virt;
    p = s[S_PRESENZA / 4];

    /* ! TUTTO A UNO VUOL DIRE CHE NESSUNO RISPONDE, non che tutto e' presente.
     * E' la stessa lezione dei registri video: ff non e' un valore, e' il
     * silenzio del bus. */
    if (p == 0xFFFFFFFFu) {
        printf("cardbus: i registri di socket si rileggono tutti a uno: il\n");
        printf("         ponte non decodifica a 0x%08x. La base e' assegnata\n",
               g_socket);
        printf("         ma la memoria non e' accesa, o l'indirizzo non e'\n");
        printf("         instradato fin li'.\n");
        return -1;
    }

    racconta_presenza(p);
    return 0;
}

/* =============================================================================
 * Accensione
 * ========================================================================== */
static int accendi(int pid_pci)
{
    unsigned int comando, bus_cb;

    if (raccogli_occupate(pid_pci) != 0) {
        printf("cardbus: non riesco a farmi dare l'elenco dei dispositivi.\n");
        return -1;
    }

    g_socket = cfg32(C_SOCKET_BASE) & 0xFFFFFFF0u;

    if (g_base_voluta != 0) {
        g_socket = g_base_voluta & ~0xFFFu;
        printf("cardbus: base data a mano: 0x%08x\n", g_socket);
        cfg32_scrivi(C_SOCKET_BASE, g_socket);
    } else if (g_socket == 0) {
        g_socket = trova_posto();
        if (g_socket == 0) {
            printf("cardbus: fra 0x%08x e 0x%08x non c'e' un vuoto di almeno\n",
                   ZONA_INIZIO, ZONA_FINE);
            printf("         sedici megabyte. Non assegno niente: dammi tu un\n");
            printf("         indirizzo con -base 0xNNNNNNNN se sai che e'\n");
            printf("         libero.\n");
            return -1;
        }
        printf("cardbus: la base dei registri non era assegnata; scelgo\n");
        printf("         0x%08x - il centro del vuoto piu' grande fra le\n",
               g_socket);
        printf("         finestre dei %d dispositivi non-ponte visti.\n",
               g_occupate);
        cfg32_scrivi(C_SOCKET_BASE, g_socket);
    } else {
        printf("cardbus: base dei registri gia' assegnata a 0x%08x.\n",
               g_socket);
    }

    /* ! I NUMERI DI BUS NON SI INVENTANO NEMMENO LORO. Il bus del ponte lo
     * sappiamo; per quello dietro si prende il primo numero che nessuno usa,
     * e su questa macchina i bus sono 0 e 1. Dare a due bus lo stesso numero
     * vuol dire due dispositivi che rispondono alla stessa domanda. */
    bus_cb = (cfg32(C_BUS_PRIMARIO) >> 8) & 0xFF;
    if (bus_cb == 0) {
        bus_cb = 2;
        printf("cardbus: i numeri di bus non erano assegnati; do' %u al bus\n",
               bus_cb);
        printf("         dietro il ponte.\n");
        cfg8_scrivi(C_BUS_PRIMARIO, g_bus);
        cfg8_scrivi(C_BUS_CARDBUS,  bus_cb);
        cfg8_scrivi(C_BUS_SUBORD,   bus_cb);
        cfg8_scrivi(C_LATENZA_CB,   0x40);
    }

    /* ! LA MEMORIA SI ACCENDE PER ULTIMA. Fino a questo punto il ponte non ha
     * decodificato niente, quindi nessuna delle scritture di sopra poteva
     * dare fastidio a qualcuno. Da qui in poi risponde davvero, e se
     * l'indirizzo fosse sbagliato si vedrebbe adesso. */
    comando = cfg32(0x04);
    cfg32_scrivi(0x04, comando | 0x0006u);   /* memoria + bus master */

    printf("cardbus: memoria accesa, guardo nello slot.\n");
    return guarda_slot();
}

/* =============================================================================
 * Il rapporto, senza toccare niente
 * ========================================================================== */
static void racconta(void)
{
    unsigned int comando = cfg32(0x04) & 0xFFFF;
    unsigned int socket  = cfg32(C_SOCKET_BASE) & 0xFFFFFFF0u;
    unsigned int bus     = cfg32(C_BUS_PRIMARIO);

    printf("cardbus: %04x:%04x rev %02x a %02x:%02x.%u\n",
           g_venditore, g_dispositivo, g_rev, g_bus, g_slot, g_funzione);
    printf("         comando %04x: porte %s, memoria %s, bus master %s\n",
           comando,
           (comando & 1) ? "si" : "NO",
           (comando & 2) ? "si" : "NO",
           (comando & 4) ? "si" : "no");
    printf("         registri di socket a 0x%08x%s\n", socket,
           socket ? "" : "   ! non assegnati");
    printf("         bus %u -> CardBus %u, fino a %u%s\n",
           bus & 0xFF, (bus >> 8) & 0xFF, (bus >> 16) & 0xFF,
           ((bus >> 8) & 0xFF) ? "" : "   ! non assegnati");

    if ((comando & 2) == 0 || socket == 0) {
        printf("\n         Questo ponte non e' configurato: il BIOS di questo\n");
        printf("         portatile non lo fa, e finche' resta cosi' quello che\n");
        printf("         c'e' nello slot non lo vede nessuno.\n");
        printf("         `cardbus.drv -accendi` lo configura.\n");
    }
}

/* =============================================================================
 * Ricerca sul bus
 * ========================================================================== */
static int cerca(int *pid_fuori)
{
    int pid = ipc_attendi(PCI_SERVIZIO, ATTESA_PCI_MS);
    unsigned int n;

    if (pid <= 0) {
        printf("cardbus: il servizio '%s' non e' attivo.\n", PCI_SERVIZIO);
        printf("         Avvialo con  /dev/pci.drv &\n");
        return -1;
    }
    *pid_fuori = pid;

    for (n = 0; n < 16; n++) {
        PciRichiesta   r;
        PciDispositivo d;
        IpcMessage     meta;
        unsigned char  buf[IPC_MSG_MAX_DATA];
        int tentativi;

        r.ordinale    = n;
        r.classe      = CLASSE_PONTE;
        r.sottoclasse = SOTTO_CARDBUS;
        r.venditore   = PCI_QUALUNQUE;
        r.dispositivo = PCI_QUALUNQUE;

        if (ipc_send(pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0) return -1;

        for (tentativi = 0; tentativi < 8; tentativi++) {
            if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return -1;
            if ((int)meta.sender_pid == pid) break;
        }
        if ((int)meta.sender_pid != pid) return -1;

        if (meta.tipo == PCI_MSG_FINE) break;
        if (meta.tipo != PCI_MSG_DISPOSITIVO || meta.len < sizeof(d)) return -1;
        memcpy(&d, buf, sizeof(d));

        g_bus         = d.bus;
        g_slot        = d.slot;
        g_funzione    = d.funzione;
        g_venditore   = d.venditore;
        g_dispositivo = d.dispositivo;
        g_rev         = d.revisione;
        return 1;
    }
    return 0;
}

static void uso(void)
{
    printf("cardbus.drv - il ponte CardBus, cioe' lo slot PCMCIA\n\n");
    printf("  cardbus.drv            dice cosa c'e' e in che stato e'\n");
    printf("  cardbus.drv -accendi   lo configura e guarda nello slot\n");
    printf("  cardbus.drv -i         c'e' un ponte CardBus? (0 = si)\n");
    printf("  cardbus.drv -accendi -base 0xE5000000\n");
    printf("                         con l'indirizzo detto da te\n\n");
    printf("! SENZA ARGOMENTI NON TOCCA NIENTE: legge e racconta.\n\n");
    printf("! -accendi SCRIVE nella configurazione del ponte: gli da' i\n");
    printf("  numeri di bus e un indirizzo per i registri di socket, e poi\n");
    printf("  accende la decodifica. L'indirizzo non e' indovinato - si\n");
    printf("  prende l'elenco dei dispositivi dal servizio PCI e si cerca un\n");
    printf("  posto libero.\n\n");
    printf("! UNA SCHEDA CARDBUS (32 bit) diventa un normale dispositivo PCI\n");
    printf("  e la vede netdetect. Una PC Card a 16 bit no: quello e' il\n");
    printf("  PCMCIA vecchio, e questo driver dice che c'e' e si ferma.\n");
}

int main(int argc, char **argv)
{
    int sonda = 0, acc = 0, i, rc, pid = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0)       { sonda = 1; continue; }
        if (strcmp(argv[i], "-accendi") == 0) { acc = 1; continue; }
        if (strcmp(argv[i], "-base") == 0 && i + 1 < argc) {
            /* ! CHI LO SCRIVE SE NE PRENDE LA RESPONSABILITA'. Un
             * indirizzo sbagliato qui accende un ponte che risponde
             * dove risponde qualcun altro. */
            g_base_voluta = (unsigned int)strtoul(argv[++i], NULL, 0);
            continue;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            uso();
            return 0;
        }
        printf("cardbus: non conosco '%s'. Prova -h.\n", argv[i]);
        return 1;
    }

    rc = cerca(&pid);
    if (rc < 0) return 1;
    if (rc == 0) {
        printf("cardbus: nessun ponte CardBus su questa macchina.\n");
        return 1;
    }

    if (sonda) {
        printf("cardbus: %04x:%04x a %02x:%02x.%u\n",
               g_venditore, g_dispositivo, g_bus, g_slot, g_funzione);
        return 0;
    }

    /* Da qui si legge la configurazione, e per farlo servono le porte. */
    if (ioport_bind(PCI_INDIRIZZO, 8) != 0) {
        printf("cardbus: ioport_bind sulle porte PCI rifiutata.\n");
        printf("         mi chiamo *.drv e giro da root? il varco e' quello.\n");
        return 1;
    }

    if (!acc) { racconta(); return 0; }

    racconta();
    printf("\n");
    return accendi(pid) == 0 ? 0 : 1;
}

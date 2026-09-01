/* =============================================================================
 * drivers/hdaudio/hdaudio.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * INTEL HD AUDIO — dove sta il Realtek ALC di qualunque scheda madre di oggi
 *
 *     /dev/hdaudio.drv       cerca il controller e accende il servizio "audio"
 *     /dev/hdaudio.drv -i    la sonda: dice cosa ha trovato ed esce
 *
 * La meta' comune — anello, protocollo, prove, sintetizzatore software — sta
 * in drivers/audio/audio_comune.c. Qui ci sono soltanto i registri.
 *
 * -----------------------------------------------------------------------------
 * ! QUESTO NON E' UN DRIVER DI UNA SCHEDA: E' IL DRIVER DI UN BUS
 *
 * HD Audio (2004) rovescia il modo in cui si e' scritto audio per vent'anni.
 * Prima il driver sapeva com'era fatta la scheda: due canali, un mixer, questo
 * registro per il volume. Qui il controller non sa NIENTE di audio — sposta
 * blocchi di byte e recapita comandi — e cio' che sa suonare sta dall'altra
 * parte di un collegamento seriale, dentro un CODEC che si deve INTERROGARE.
 *
 * Un codec ha dei «widget» numerati, ognuno con un tipo: convertitore di
 * uscita (il DAC vero), presa (il connettore verde sul retro), miscelatore,
 * selettore. Come sono collegati fra loro lo dice il codec stesso, widget per
 * widget. Il driver deve quindi:
 *
 *     1. resettare il controller e vedere quali codec rispondono
 *     2. chiedere al codec quanti gruppi di funzioni ha, e quale e' audio
 *     3. percorrere i widget di quel gruppo
 *     4. sceglierne DUE — un convertitore d'uscita e una presa che esca fuori
 *     5. accenderli, collegarli, alzarne i volumi
 *
 * ! ED E' PER QUESTO CHE SI SCRIVE UN DRIVER SOLO PER TUTTI I REALTEK. Un
 * ALC887 e un ALC1220 hanno topologie diverse, e nessuna delle due sta scritta
 * qui: si chiedono. Lo stesso codice guida il codec Analog Devices o VIA
 * montato sulla stessa scheda madre.
 *
 * -----------------------------------------------------------------------------
 * ! I COMANDI AL CODEC PASSANO DAI REGISTRI IMMEDIATI, NON DA CORB/RIRB
 *
 * La strada maestra sono due anelli in memoria — CORB per i comandi, RIRB per
 * le risposte — che il controller percorre da bus master. Servono due buffer
 * DMA in piu', due indici da tenere allineati e un secondo percorso di
 * interrupt, per fare una cosa che qui succede una manciata di volte in tutto:
 * all'accensione.
 *
 * I registri immediati (0x60/0x64/0x68) fanno lo stesso lavoro un comando per
 * volta, aspettando. Sono la strada che Linux stesso tiene come ripiego
 * (`single_cmd`), e per un driver che interroga il codec solo all'avvio sono
 * la strada giusta, non un compromesso. Se un giorno servira' mandare comandi
 * MENTRE si suona — cambiare presa a caldo, seguire una cuffia che si
 * infila — allora serviranno gli anelli.
 * ============================================================================= */

#include "libc.h"
#include "audio_proto.h"
#include "audio_dorso.h"
#include "pci_proto.h"

/* +0.001 a ogni modifica: `hdaudio.drv -version` la stampa. */
EX_VERSIONE("hdaudio.drv", "0.001");

/* =============================================================================
 * I registri del controller (MMIO, BAR0)
 * ========================================================================== */
#define H_GCAP          0x00    /* 16: quanti flussi ci sono */
#define H_GCTL          0x08    /* 32: bit 0 = fuori dal reset */
#define H_STATESTS      0x0E    /* 16: quali codec hanno risposto */
#define H_INTCTL        0x20    /* 32 */
#define H_INTSTS        0x24    /* 32 */
#define H_ICOI          0x60    /* 32: comando immediato */
#define H_ICII          0x64    /* 32: risposta immediata */
#define H_ICIS          0x68    /* 16: stato del comando immediato */

#define GCTL_CRST       0x00000001u
#define INTCTL_GIE      0x80000000u
#define INTCTL_CIE      0x40000000u

#define ICIS_BUSY       0x0001
#define ICIS_VALIDO     0x0002

/* Un descrittore di flusso: 0x20 byte, il primo a 0x80 */
#define SD_BASE         0x80
#define SD_PASSO        0x20
#define SD_CTL          0x00    /* 24 bit */
#define SD_STS          0x03
#define SD_LPIB         0x04    /* dove sta leggendo, in byte */
#define SD_CBL          0x08    /* lunghezza del giro */
#define SD_LVI          0x0C    /* 16 */
#define SD_FMT          0x12    /* 16 */
#define SD_BDLPL        0x18
#define SD_BDLPU        0x1C

#define SDCTL_SRST      0x000001u
#define SDCTL_RUN       0x000002u
#define SDCTL_IOCE      0x000004u
#define SDCTL_FEIE      0x000008u
#define SDCTL_DEIE      0x000010u
#define SDCTL_STRM_SH   20

#define SDSTS_BCIS      0x04
#define SDSTS_FIFOE     0x08
#define SDSTS_DESE      0x10

/* =============================================================================
 * I verbi del codec — quelli che servono, non tutti
 * ========================================================================== */
#define V_GET_PARAM     0xF0000
#define V_GET_CONN_LIST 0xF0200
#define V_SET_CONN_SEL  0x70100
#define V_SET_POWER     0x70500
#define V_SET_FORMATO   0x20000     /* verbo a 16 bit */
#define V_SET_FLUSSO    0x70600
#define V_SET_AMP       0x30000     /* verbo a 16 bit */
#define V_SET_PIN_CTL   0x70700
#define V_SET_EAPD      0x70C00

#define P_VENDOR        0x00
#define P_NODI          0x04
#define P_TIPO_GRUPPO   0x05
#define P_CAP_WIDGET    0x09
#define P_CAP_PIN       0x0C
#define P_AMP_USCITA    0x12    /* quanti passi ha l'amplificatore d'uscita */
#define P_CONFIG_DEF    0xF1C00     /* e' un verbo, non un parametro */

#define W_TIPO_SH       20
#define W_USCITA        0x0         /* convertitore d'uscita: il DAC */
#define W_PRESA         0x4         /* pin complex */

/* =============================================================================
 * Stato
 * ========================================================================== */
static volatile unsigned char *g_reg = 0;
static unsigned int g_irq   = 0;
static unsigned int g_codec = 0;
static unsigned int g_dac   = 0;    /* il widget che converte */
static unsigned int g_presa = 0;    /* il widget che esce fuori */
static unsigned int g_flusso = 1;   /* il numero di flusso, 1..15 */
static unsigned int g_passi  = 0x7F; /* passi dell'amplificatore, li dice il codec */
static unsigned int g_vol    = 80;
static unsigned int g_sd    = 0;    /* quale descrittore di flusso usiamo */
static char         g_nome[32];
static char         g_codec_nome[32];

static DmaZona      g_zona;
static unsigned int *g_bdl = 0;
static unsigned int  g_bdl_fis = 0;
static unsigned char *g_buf = 0;
static unsigned int  g_buf_fis = 0;
static unsigned int  g_buf_byte = 0;
static unsigned int  g_meta = 0;
static unsigned int  g_suona = 0;

static int g_forz_irq = -1;

/* =============================================================================
 * Accesso ai registri — sempre volatile, sempre della larghezza giusta
 *
 * ! LA LARGHEZZA NON E' UN DETTAGLIO. Alcuni registri di questo controller
 * reagiscono alla scrittura di UN byte in modo diverso da una parola: SD_CTL
 * e' a 24 bit e il byte alto porta il numero di flusso. Scriverlo a 32
 * toccherebbe anche SD_STS, che si azzera scrivendoci sopra — e si
 * azzererebbero interrupt mai visti.
 * ========================================================================== */
static unsigned int  r8 (unsigned int o) { return *(volatile unsigned char *)(g_reg + o); }
static unsigned int  r16(unsigned int o) { return *(volatile unsigned short *)(g_reg + o); }
static unsigned int  r32(unsigned int o) { return *(volatile unsigned int *)(g_reg + o); }
static void w8 (unsigned int o, unsigned int v) { *(volatile unsigned char *)(g_reg + o) = (unsigned char)v; }
static void w16(unsigned int o, unsigned int v) { *(volatile unsigned short *)(g_reg + o) = (unsigned short)v; }
static void w32(unsigned int o, unsigned int v) { *(volatile unsigned int *)(g_reg + o) = v; }

static unsigned int sd(unsigned int off) { return SD_BASE + g_sd * SD_PASSO + off; }

/* =============================================================================
 * Un comando al codec, e la sua risposta
 * ========================================================================== */
static int verbo(unsigned int nodo, unsigned int v, unsigned int dato,
                 unsigned int *risposta)
{
    unsigned int cmd;
    int guard;

    /* Il comando: codec, nodo, verbo, dato. I verbi «a 16 bit» hanno il dato
     * nei sedici bit bassi; gli altri negli otto. La differenza sta nel valore
     * di V_* che si passa, non qui. */
    cmd = ((g_codec & 0x0F) << 28) | ((nodo & 0x7F) << 20) | (v | (dato & 0xFFFF));

    for (guard = 0; guard < 10000; guard++)
        if (!(r16(H_ICIS) & ICIS_BUSY)) break;
    if (r16(H_ICIS) & ICIS_BUSY) return -1;

    w16(H_ICIS, ICIS_VALIDO);       /* azzera «risposta valida» scrivendoci sopra */
    w32(H_ICOI, cmd);
    w16(H_ICIS, ICIS_BUSY);

    for (guard = 0; guard < 100000; guard++) {
        unsigned int st = r16(H_ICIS);
        if (!(st & ICIS_BUSY) && (st & ICIS_VALIDO)) {
            if (risposta) *risposta = r32(H_ICII);
            return 0;
        }
    }
    return -1;
}

static unsigned int passi_amp(unsigned int nodo);
static void         volume_su(unsigned int pct);

static unsigned int parametro(unsigned int nodo, unsigned int p)
{
    unsigned int r = 0;
    if (verbo(nodo, V_GET_PARAM, p, &r) < 0) return 0;
    return r;
}

/* =============================================================================
 * La ricerca dei due widget che servono
 *
 * ! SI SCEGLIE LA PRESA GUARDANDO COM'E' CONFIGURATA, non la prima che
 * capita. Il «configuration default» di ogni presa dice a cosa e' cablata:
 * un connettore sul retro, un jack frontale, o NIENTE — perche' un ALC ha
 * quindici prese e la scheda madre ne collega cinque. Prendere la prima
 * significa, in un caso su tre, mandare il suono a un connettore che sulla
 * scheda non e' saldato.
 * ========================================================================== */
static int trova_widget(void)
{
    unsigned int nodi, primo, i, afg = 0;

    /* I gruppi di funzioni del codec, dal nodo radice. */
    nodi  = parametro(0, P_NODI);
    primo = (nodi >> 16) & 0xFF;
    nodi  = nodi & 0xFF;

    for (i = 0; i < nodi; i++) {
        unsigned int t = parametro(primo + i, P_TIPO_GRUPPO) & 0x7F;
        if (t == 0x01) { afg = primo + i; break; }      /* gruppo audio */
    }
    if (!afg) return -1;

    /* Il gruppo si accende PRIMA di parlare con i suoi widget: un gruppo in
     * risparmio energetico risponde alle domande e non suona. */
    verbo(afg, V_SET_POWER, 0x00, 0);
    usleep(10000);

    nodi  = parametro(afg, P_NODI);
    primo = (nodi >> 16) & 0xFF;
    nodi  = nodi & 0xFF;

    g_dac = g_presa = 0;

    for (i = 0; i < nodi; i++) {
        unsigned int n   = primo + i;
        unsigned int cap = parametro(n, P_CAP_WIDGET);
        unsigned int tipo = (cap >> W_TIPO_SH) & 0x0F;

        if (tipo == W_USCITA && !g_dac) {
            g_dac = n;
            continue;
        }

        if (tipo == W_PRESA && !g_presa) {
            unsigned int pcap = parametro(n, P_CAP_PIN);
            unsigned int cfg  = 0;

            if (!(pcap & 0x10)) continue;               /* non sa uscire */

            verbo(n, P_CONFIG_DEF, 0, &cfg);
            /* Bit 30-31 = «connettivita'»: 01 = nessuna connessione fisica.
             * Quelle si saltano: e' il connettore che la scheda madre non ha
             * saldato. Se il codec non risponde si prende comunque, perche'
             * una presa muta e' meglio di nessuna presa. */
            if (cfg != 0 && ((cfg >> 30) & 0x3) == 1) continue;

            g_presa = n;
        }
    }

    if (!g_dac || !g_presa) return -1;

    /* Se la presa sceglie fra piu' sorgenti, le si dice di ascoltare la
     * prima: sulle topologie semplici e' il nostro DAC. */
    verbo(g_presa, V_SET_CONN_SEL, 0, 0);

    /* Accensione dei due, la presa in uscita, e l'amplificatore esterno —
     * l'EAPD, che sulle schede madri comanda il chip di potenza degli
     * altoparlanti: senza, tutto funziona e non esce un suono. */
    verbo(g_dac,   V_SET_POWER, 0x00, 0);
    verbo(g_presa, V_SET_POWER, 0x00, 0);
    verbo(g_presa, V_SET_PIN_CTL, 0x40, 0);     /* uscita abilitata */
    verbo(g_presa, V_SET_EAPD, 0x02, 0);

    /* Il piu' stretto dei due amplificatori: alzarne uno oltre i suoi passi
     * non serve, e usare quello largo per programmare quello stretto e' come
     * non averli chiesti. */
    {
        unsigned int a = passi_amp(g_dac), b = passi_amp(g_presa);
        g_passi = (a < b) ? a : b;
    }

    return 0;
}

/* Volume: l'amplificatore di un widget si scrive con un verbo solo, e i bit
 * alti dicono a QUALE amplificatore — uscita o ingresso, sinistra o destra. */
static void amp_uscita(unsigned int nodo, unsigned int guadagno, int muto)
{
    unsigned int v = 0x8000 | 0x4000 | 0x2000 |     /* uscita, sinistra+destra */
                     (muto ? 0x0080 : 0) | (guadagno & 0x7F);
    verbo(nodo, V_SET_AMP, v, 0);
}

/* =============================================================================
 * Quanto in alto puo' andare questo amplificatore — lo dice il codec
 *
 * ! NON SI SCRIVE UN NUMERO FISSO. I passi di un amplificatore HD Audio sono
 * da 1 a 127 a seconda del widget, e ogni passo vale una frazione di decibel
 * che pure cambia. Scrivere 0x50 su un amplificatore che di passi ne ha 31
 * non da' errore: il codec tiene i bit che gli servono e ignora gli altri, e
 * quel che si ottiene e' un volume a caso — che e' come e' venuto fuori la
 * prima volta, un tono giusto a un ventesimo del livello che doveva avere.
 * ========================================================================== */
static unsigned int passi_amp(unsigned int nodo)
{
    unsigned int cap = parametro(nodo, P_AMP_USCITA);
    unsigned int n   = (cap >> 8) & 0x7F;

    return n ? n : 0x7F;
}

static void volume_su(unsigned int pct)
{
    unsigned int g;

    if (pct > 100) pct = 100;
    g = (pct * g_passi) / 100;

    amp_uscita(g_dac,   g, pct == 0);
    amp_uscita(g_presa, g, pct == 0);
}

/* =============================================================================
 * PCI
 * ========================================================================== */
#define ATTESA_MS 2000
static int pci_pid = -1;

static int pci_risposta(unsigned int atteso, void *out, unsigned int len)
{
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           giri;

    for (giri = 0; giri < 8; giri++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), ATTESA_MS) < 0) return -1;
        if (meta.sender_pid != (unsigned int)pci_pid) continue;
        if (meta.tipo == PCI_MSG_FINE) return 1;
        if (meta.tipo != atteso) continue;
        if (meta.len < len) return -1;
        memcpy(out, buf, len);
        return 0;
    }
    return -1;
}

static int trova_controller(PciDispositivo *out)
{
    PciRichiesta r;
    unsigned int ord;

    for (ord = 0; ord < 16; ord++) {
        int esito;

        memset(&r, 0, sizeof(r));
        r.ordinale    = ord;
        r.classe      = 0x04;
        r.sottoclasse = 0x03;           /* HD Audio: la sottoclasse basta */
        r.venditore   = PCI_QUALUNQUE;
        r.dispositivo = PCI_QUALUNQUE;

        if (ipc_send((unsigned int)pci_pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0)
            return -1;
        esito = pci_risposta(PCI_MSG_DISPOSITIVO, out, sizeof(*out));
        if (esito != 0) return -1;

        /* ! LA SOTTOCLASSE 0x03 BASTA, e un elenco di modelli sarebbe peggio.
         * HD Audio e' una specifica: i registri sono gli stessi su Intel, AMD,
         * nVidia e VIA. Una tabella di identificativi qui vorrebbe dire
         * rifiutare il controller di domani per non averlo scritto ieri. */
        snprintf(g_nome, sizeof(g_nome), "HD Audio %04x:%04x",
                 out->venditore, out->dispositivo);
        return 0;
    }
    return -1;
}

static int abilita(const PciDispositivo *d)
{
    PciAzione a;
    PciEsito  e;

    memset(&a, 0, sizeof(a));
    a.bus = d->bus; a.slot = d->slot; a.funzione = d->funzione;
    a.bit = PCI_ABIL_MEMORIA | PCI_ABIL_BUSMASTER;

    if (ipc_send((unsigned int)pci_pid, PCI_MSG_ABILITA, &a, sizeof(a)) < 0)
        return -1;
    if (pci_risposta(PCI_MSG_ESITO, &e, sizeof(e)) != 0) return -1;
    return e.codice;
}

/* =============================================================================
 * Memoria: la lista dei descrittori piu' il buffer, in una zona sola
 * ========================================================================== */
#define BUF_BYTE  32768u
#define ZONA_TESTA 4096u

static int prendi_memoria(void)
{
    if (g_buf) return 0;

    memset(&g_zona, 0, sizeof(g_zona));
    g_zona.byte = ZONA_TESTA + BUF_BYTE;
    if (dma_alloc(&g_zona) < 0) {
        printf("hdaudio: dma_alloc(%u) fallita\n", ZONA_TESTA + BUF_BYTE);
        return -1;
    }
    g_bdl     = (unsigned int *)g_zona.virt;
    g_bdl_fis = g_zona.fisico;
    g_buf     = (unsigned char *)(g_zona.virt + ZONA_TESTA);
    g_buf_fis = g_zona.fisico + ZONA_TESTA;
    return 0;
}

/* =============================================================================
 * La sonda
 * ========================================================================== */
static int hd_sonda(AudioInfo *info)
{
    PciDispositivo d;
    MmioZona       m;
    unsigned int   gcap, in_stream, out_stream, stati;
    int            guard, i;

    pci_pid = ipc_attendi("pci", 5000);
    if (pci_pid <= 0) {
        printf("hdaudio: il server PCI non e' attivo.\n");
        printf("         Si accende con  /dev/pci.drv &  - oppure mettendo\n");
        printf("         pci = /dev/pci.drv in [modules] di kernel.cfg.\n");
        return -1;
    }

    if (trova_controller(&d) < 0) {
        printf("hdaudio: nessun controller HD Audio sul bus.\n");
        return -1;
    }

    g_irq = (g_forz_irq > 0) ? (unsigned int)g_forz_irq : d.irq_linea;
    if (g_irq == 0 || g_irq == 0xFF) {
        printf("hdaudio: il BIOS non ha assegnato un IRQ al controller.\n");
        return -1;
    }
    if (!d.bar[0] || d.bar_io[0]) {
        printf("hdaudio: il BAR0 non e' una finestra di memoria.\n");
        return -1;
    }

    if (abilita(&d) < 0) {
        printf("hdaudio: il server PCI non ha acceso memoria e bus master.\n");
        return -1;
    }

    /* ! I REGISTRI SONO IN MEMORIA, NON IN PORTE, ed e' la prima differenza
     * che si vede: niente ioport_bind, una finestra mappata nel processo. Il
     * puntatore e' volatile perche' quei valori cambiano senza che nessuno
     * scriva — vedi mmio_map in lib/include/libc.h. */
    memset(&m, 0, sizeof(m));
    m.fisico = d.bar[0];
    m.byte   = 0x4000;
    if (mmio_map(&m) < 0) {
        printf("hdaudio: mmio_map(0x%x) fallita\n", d.bar[0]);
        return -1;
    }
    g_reg = (volatile unsigned char *)m.virt;

    /* --- fuori dal reset --- */
    w32(H_GCTL, 0);
    for (guard = 0; guard < 1000; guard++) { if (!(r32(H_GCTL) & GCTL_CRST)) break; usleep(100); }
    w32(H_GCTL, GCTL_CRST);
    for (guard = 0; guard < 1000; guard++) { if (r32(H_GCTL) & GCTL_CRST) break; usleep(100); }
    if (!(r32(H_GCTL) & GCTL_CRST)) {
        printf("hdaudio: il controller non esce dal reset.\n");
        return -1;
    }
    /* ! 521 MICROSECONDI DI ATTESA, e stanno sulla specifica. Il collegamento
     * seriale col codec ha bisogno di quel tempo per stabilizzarsi dopo il
     * reset; interrogare prima da' un STATESTS vuoto, cioe' «non c'e' nessun
     * codec» su una macchina che ne ha uno. */
    usleep(1000);

    stati = r16(H_STATESTS);
    if (stati == 0) {
        printf("hdaudio: il controller c'e', ma nessun codec risponde.\n");
        return -1;
    }
    for (i = 0; i < 15; i++) if (stati & (1u << i)) { g_codec = (unsigned int)i; break; }

    gcap       = r16(H_GCAP);
    in_stream  = (gcap >> 8)  & 0x0F;
    out_stream = (gcap >> 12) & 0x0F;
    if (out_stream == 0) {
        printf("hdaudio: il controller non dichiara nessun flusso in uscita.\n");
        return -1;
    }
    /* ! I DESCRITTORI D'USCITA VENGONO DOPO QUELLI D'INGRESSO, sempre. Usare
     * il descrittore 0 senza contare gli ingressi vuol dire programmare un
     * flusso di REGISTRAZIONE e aspettare che suoni. */
    g_sd = in_stream;

    if (trova_widget() < 0) {
        printf("hdaudio: il codec %u non ha un'uscita utilizzabile.\n", g_codec);
        return -1;
    }

    /* Chi e': l'identificativo del venditore del codec. 0x10EC e' Realtek. */
    {
        unsigned int vid = parametro(0, P_VENDOR);
        unsigned int ven = (vid >> 16) & 0xFFFF, dev = vid & 0xFFFF;

        if (ven == 0x10EC) snprintf(g_codec_nome, sizeof(g_codec_nome),
                                    "Realtek ALC%x", dev);
        else if (ven == 0x1013) snprintf(g_codec_nome, sizeof(g_codec_nome),
                                    "Cirrus Logic %04x", dev);
        else if (ven == 0x11D4) snprintf(g_codec_nome, sizeof(g_codec_nome),
                                    "Analog Devices %04x", dev);
        else if (ven == 0x8384) snprintf(g_codec_nome, sizeof(g_codec_nome),
                                    "SigmaTel %04x", dev);
        else snprintf(g_codec_nome, sizeof(g_codec_nome), "codec %04x:%04x", ven, dev);
    }

    if (prendi_memoria() < 0) return -1;

    memset(info, 0, sizeof(*info));
    snprintf(info->nome, sizeof(info->nome), "%s", g_codec_nome);
    strcpy(info->bus, "PCI");
    info->base     = 0;                 /* e' in memoria, non in porte */
    info->irq      = g_irq;
    info->dma8     = AUDIO_DMA_NESSUNO;
    info->dma16    = AUDIO_DMA_NESSUNO;
    /* HD Audio parte da 16 bit. Gli 8 bit non esistono su questo bus, e
     * prometterli vorrebbe dire convertire di nascosto. */
    info->capacita = AUDIO_CAP_PCM16 | AUDIO_CAP_STEREO | AUDIO_CAP_MIXER;
    info->rate_min = 44100;
    info->rate_max = 48000;

    printf("hdaudio: %s, codec %u, flusso %u, %s\n",
           g_nome, g_codec, g_sd, g_codec_nome);
    return 0;
}

/* =============================================================================
 * Il formato, come lo vuole il registro SD_FMT
 *
 * Non e' un numero di hertz: e' base + moltiplicatore + divisore. La base e'
 * 48000 o 44100, e da li' si sale e si scende per rapporti interi. Le
 * frequenze che non si ottengono cosi' NON ESISTONO su questo bus — 22050 si',
 * perche' e' 44100 diviso due; 32000 no.
 * ========================================================================== */
static unsigned int codifica_formato(unsigned int *rate, unsigned int canali)
{
    unsigned int base44 = 0, div = 0, mul = 0, f;

    if (*rate >= 46000)      { *rate = 48000; base44 = 0; div = 0; }
    else if (*rate >= 43000) { *rate = 44100; base44 = 1; div = 0; }
    else if (*rate >= 23000) { *rate = 24000; base44 = 0; div = 1; }
    else                     { *rate = 22050; base44 = 1; div = 1; }

    f  = (base44 ? 0x4000u : 0u);
    f |= (mul & 0x7) << 11;
    f |= (div & 0x7) << 8;
    f |= 0x0010u;                       /* 16 bit */
    f |= ((canali - 1) & 0x0F);
    return f;
}

static int hd_apri(AudioFormato *f, unsigned char **buf, unsigned int *byte)
{
    unsigned int fmt, meta;
    int guard;

    if (!g_buf) return -1;

    f->bit    = 16;
    f->canali = 2;
    fmt = codifica_formato(&f->rate, f->canali);

    {
        unsigned int bps = f->rate * 4;
        meta = 2048;
        while (meta < bps / 20 && meta < BUF_BYTE / 2) meta <<= 1;
    }
    g_meta     = meta;
    g_buf_byte = meta * 2;
    memset(g_buf, 0, g_buf_byte);
    *buf  = g_buf;
    *byte = g_buf_byte;

    /* --- azzeramento del descrittore di flusso --- */
    w8(sd(SD_CTL), SDCTL_SRST);
    for (guard = 0; guard < 1000; guard++) { if (r8(sd(SD_CTL)) & SDCTL_SRST) break; usleep(100); }
    w8(sd(SD_CTL), 0);
    for (guard = 0; guard < 1000; guard++) { if (!(r8(sd(SD_CTL)) & SDCTL_SRST)) break; usleep(100); }

    /* --- la lista: due voci da 16 byte, le due meta', tutte e due con IOC --- */
    g_bdl[0] = g_buf_fis;       g_bdl[1] = 0;
    g_bdl[2] = g_meta;          g_bdl[3] = 1;          /* IOC */
    g_bdl[4] = g_buf_fis + g_meta; g_bdl[5] = 0;
    g_bdl[6] = g_meta;          g_bdl[7] = 1;

    w32(sd(SD_BDLPL), g_bdl_fis);
    w32(sd(SD_BDLPU), 0);
    w32(sd(SD_CBL), g_buf_byte);
    w16(sd(SD_LVI), 1);
    w16(sd(SD_FMT), fmt);

    /* Il numero di flusso va scritto sia nel descrittore sia nel codec: e'
     * l'etichetta con cui i byte viaggiano sul collegamento seriale, e i due
     * capi devono usare la stessa. */
    w32(sd(SD_CTL), (g_flusso << SDCTL_STRM_SH));
    verbo(g_dac, V_SET_FORMATO, fmt, 0);
    verbo(g_dac, V_SET_FLUSSO, (g_flusso << 4) | 0, 0);

    volume_su(g_vol);

    /* Gli interrupt del nostro flusso, e quelli globali. */
    w32(H_INTCTL, INTCTL_GIE | INTCTL_CIE | (1u << g_sd));
    return 0;
}

static void hd_via(void)
{
    w8(sd(SD_STS), SDSTS_BCIS | SDSTS_FIFOE | SDSTS_DESE);
    w32(sd(SD_CTL), (g_flusso << SDCTL_STRM_SH) |
                    SDCTL_RUN | SDCTL_IOCE | SDCTL_FEIE | SDCTL_DEIE);
    g_suona = 1;
}

static void hd_ferma(void)
{
    if (!g_suona) return;
    g_suona = 0;
    w32(sd(SD_CTL), (g_flusso << SDCTL_STRM_SH));
    w8(sd(SD_STS), SDSTS_BCIS | SDSTS_FIFOE | SDSTS_DESE);
}

static void hd_chiudi(void) { hd_ferma(); }

/* =============================================================================
 * L'interrupt
 * ========================================================================== */
static int hd_irq(void)
{
    unsigned int sts = r32(H_INTSTS);
    unsigned int st, pos;

    if (!(sts & (1u << g_sd))) return -1;

    st = r8(sd(SD_STS));
    w8(sd(SD_STS), st & (SDSTS_BCIS | SDSTS_FIFOE | SDSTS_DESE));

    if (!g_suona) return -1;

    /* ! LPIB DICE DOVE STA LEGGENDO, IN BYTE, e si crede a lui e non a un
     * conteggio nostro — stessa scelta della Sound Blaster e dell'AC'97, e
     * per lo stesso motivo: una notifica persa sfaserebbe per sempre chi
     * conta da se'. */
    pos = r32(sd(SD_LPIB));
    return (pos >= g_meta) ? 0 : 1;     /* legge la seconda -> e' libera la prima */
}

static unsigned int hd_avanzamento(void)
{
    return r32(sd(SD_LPIB));
}

/* =============================================================================
 * Volume
 *
 * L'amplificatore di un widget ha un guadagno a sette bit e un passo che il
 * codec dichiara; qui si usa la scala intera senza chiederla, perche' la
 * differenza fra un passo di 0.25 dB e uno di 1.5 dB si sente come «il volume
 * a meta' e' un po' piu' basso», non come un guasto.
 * ========================================================================== */
static void hd_volume(unsigned int pct)
{
    if (pct > 100) pct = 100;
    g_vol = pct;
    volume_su(pct);
}

/* =============================================================================
 * MIDI — non c'e'
 * ========================================================================== */
static int hd_midi(const unsigned char *b, unsigned int n)
{
    (void)b; (void)n;
    /* ! HD AUDIO NON HA NE' SINTETIZZATORE NE' PORTA MIDI, e nessun codec ne
     * ha uno. Il MIDI su questa scheda lo fa il sintetizzatore software della
     * meta' comune, che si accende da solo perche' qui non c'e' ne'
     * AUDIO_CAP_MIDI_FM ne' AUDIO_CAP_MIDI_UART. */
    return 0;
}

static int hd_midi_vivo(void) { return -1; }

static int hd_opzione(const char *arg, const char *valore)
{
    if (!valore) return -1;
    if (strcmp(arg, "-q") == 0) { g_forz_irq = (int)strtol(valore, 0, 0); return 0; }
    return -1;
}

static const AudioDorso g_dorso = {
    "hdaudio",
    hd_opzione,
    hd_sonda,
    hd_apri,
    hd_via,
    hd_ferma,
    hd_chiudi,
    hd_irq,
    hd_avanzamento,
    hd_volume,
    hd_midi,
    hd_midi_vivo
};

const AudioDorso *audio_dorso_questo(void)
{
    return &g_dorso;
}

/* =============================================================================
 * drivers/es1371/es1371.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * SOUND BLASTER PCI 64 / 128 — cioe' Ensoniq ES1370, ES1371, CT5880
 *
 *     /dev/es1371.drv        cerca la scheda e accende il servizio "audio"
 *     /dev/es1371.drv -i     la sonda: dice cosa ha trovato ed esce
 *
 * La meta' comune — anello, protocollo, prove, sintetizzatore software —
 * sta in drivers/audio/audio_comune.c. Qui ci sono soltanto i registri.
 *
 * -----------------------------------------------------------------------------
 * ! LA «SOUND BLASTER 128» NON E' UNA SOUND BLASTER, e non e' un cavillo
 *
 * Creative compro' Ensoniq nel 1998 e ne vendette il chip AudioPCI con il
 * proprio nome: «Sound Blaster PCI 64», «PCI 128», «Sound Blaster 128». Del
 * DSP della Sound Blaster vera non c'e' NIENTE: niente comando 0xE1, niente
 * mixer a 0x224, niente DMA della scheda madre. La compatibilita' con i
 * giochi DOS la faceva un programma residente che intercettava gli accessi
 * alle porte e li traduceva — cioe' non la faceva l'hardware.
 *
 * Chi cerca «il driver della mia Sound Blaster 128» arriva qui, e questo file
 * gliela chiama con il nome che ha sulla scatola. Ma il silicio e' un altro, e
 * confonderli vorrebbe dire cercare in sb.c un guasto che sta qui.
 *
 * -----------------------------------------------------------------------------
 * ! QUESTA E' UN BUS MASTER: NIENTE 8237, NIENTE CONFINE DEI 64 KB
 *
 * La differenza vera con la Sound Blaster ISA non e' il nome: e' CHI legge la
 * memoria. Li' il DMA e' un chip della scheda madre, con i suoi ventiquattro
 * bit di indirizzo e il suo confine di 64 KB (vedi il commento in testa a
 * drivers/sb/sb.c). Qui e' la SCHEDA a leggere, sul bus PCI, a 32 bit: il
 * buffer puo' stare ovunque, purche' fisicamente contiguo — che e' esattamente
 * cio' che SYS_DMA_ALLOC da'.
 *
 * In cambio serve una cosa che sull'ISA non serviva: il bit BUS MASTER nel
 * registro comando della configurazione PCI. Senza, la scheda e' viva, accetta
 * i registri, dichiara di suonare e non legge un byte — e il sintomo e'
 * silenzio con tutti i contatori a posto.
 *
 * -----------------------------------------------------------------------------
 * ! DUE CHIP IN UN DRIVER, E LA DIFFERENZA E' DUE COSE SOLE
 *
 * ES1370 e ES1371 hanno lo STESSO motore DMA: stessi registri di frame, stesso
 * contatore, stessa interruzione. Cambiano:
 *
 *   la frequenza   sull'ES1370 e' un divisore in un campo del registro di
 *                  controllo; sull'ES1371 c'e' un convertitore di frequenza
 *                  (SRC) con una RAM da programmare.
 *   il codec       sull'ES1370 e' un AK4531 con un registro tutto suo;
 *                  sull'ES1371 e' un AC'97 standard.
 *
 * ! DELLE DUE STRADE QUI SE NE PUO' PROVARE UNA SOLA: QEMU emula l'ES1370 e
 * non l'ES1371. Il codice dell'SRC e' scritto sulla sequenza documentata e
 * non e' mai stato eseguito su silicio ne' su emulazione. E' segnato dove
 * comincia, e chi ha la scheda vera comincera' a guardare da li'.
 * ============================================================================= */

#include "libc.h"
#include "audio_proto.h"
#include "audio_dorso.h"
#include "pci_proto.h"

/* +0.001 a ogni modifica: `es1371.drv -version` la stampa. */
EX_VERSIONE("es1371.drv", "0.001");

/* =============================================================================
 * I registri, come spiazzamenti dalla base I/O (BAR0, 64 byte)
 * ========================================================================== */
#define R_CONTROL       0x00
#define R_STATUS        0x04
#define R_UART_DATI     0x08
#define R_UART_STATO    0x09    /* in lettura */
#define R_UART_CTRL     0x09    /* in scrittura */
#define R_MEMPAGE       0x0C
#define R_CODEC_1370    0x10    /* AK4531 */
#define R_SMPRATE_1371  0x10    /* il convertitore di frequenza */
#define R_CODEC_1371    0x14    /* AC'97 */
#define R_SCTRL         0x20
#define R_DAC1_SCOUNT   0x24
#define R_DAC2_SCOUNT   0x28

/* Dentro la pagina 0x0C dei registri di frame */
#define R_FRAME_ADDR    0x38    /* DAC2 */
#define R_FRAME_CNT     0x3C
#define PAGINA_DAC      0x0C

/* CONTROL */
#define C_ADC_STOP      0x80000000u
#define C_PCLKDIV_SH    16
#define C_PCLKDIV_MASK  0x1FFF0000u
#define C_DAC2_EN       0x00000020u
#define C_UART_EN       0x00000008u
#define C_CDC_EN        0x00000002u

/* STATUS */
#define S_INTR          0x80000000u
#define S_CSTAT         0x00000400u
#define S_DAC2          0x00000002u

/* SCTRL */
#define SC_P2_INTEN     0x00000200u
#define SC_P2_PAUSE     0x00001000u
#define SC_P2_LOOPSEL   0x00004000u
#define SC_P2_FMT_SH    2                       /* bit 2 = stereo, bit 3 = 16 bit */
#define SC_P2_FMT_MASK  0x0000000Cu
#define SC_P2_ENDINC_SH 19
#define SC_P2_STINC_SH  16

/* SRC dell'ES1371 */
#define SRC_RAMWE       0x01000000u
#define SRC_BUSY        0x00800000u
#define SRC_DIS         0x00400000u
#define SRC_DDAC1       0x00200000u
#define SRC_DDAC2       0x00100000u
#define SRC_DADC        0x00080000u
#define SRC_ADDR_SH     25
#define SRCREG_DAC1     0x70
#define SRCREG_DAC2     0x74
#define SRCREG_VOL_ADC  0x6C
#define SRCREG_VOL_DAC1 0x7C
#define SRCREG_VOL_DAC2 0x7E
#define SRC_TRUNC_N     0x00
#define SRC_INT_REGS    0x01
#define SRC_VFREQ_FRAC  0x0D

/* Codec AC'97 dell'ES1371 */
#define CODEC_PIRD      0x00800000u
#define CODEC_WIP       0x40000000u
#define CODEC_RDY       0x80000000u

/* =============================================================================
 * Stato
 * ========================================================================== */
static unsigned int g_base = 0;
static unsigned int g_irq  = 0;
static unsigned int g_1371 = 0;         /* 0 = ES1370, 1 = ES1371/CT5880 */
static char         g_nome[32];

static DmaZona      g_zona;
static unsigned char *g_buf = 0;
static unsigned int g_buf_byte = 0;
static unsigned int g_meta = 0;

static unsigned int g_canali = 2;
static unsigned int g_bit    = 16;
static unsigned int g_sctrl  = 0;
static unsigned int g_suona  = 0;

static int g_forz_base = -1, g_forz_irq = -1;

static int dma_alloc_buffer(void);

/* =============================================================================
 * Accesso ai registri
 * ========================================================================== */
static void  put32(unsigned int off, unsigned int v) { ioport_out32(g_base + off, v); }
static unsigned int get32(unsigned int off)
{
    unsigned int v = 0;
    ioport_in32(g_base + off, &v);
    return v;
}
static void put8(unsigned int off, unsigned int v) { ioport_out(g_base + off, v); }
static int  get8(unsigned int off)                 { return ioport_in(g_base + off); }

/* =============================================================================
 * Il convertitore di frequenza dell'ES1371
 *
 * ! DA QUI COMINCIA LA PARTE MAI ESEGUITA. QEMU emula l'ES1370: tutto cio' che
 * tocca l'SRC e il codec AC'97 e' scritto sulla sequenza documentata e non e'
 * mai girato. Su una scheda vera, se il suono esce alla velocita' sbagliata o
 * non esce, si comincia a guardare da queste quaranta righe.
 * ========================================================================== */
static unsigned int src_attendi(void)
{
    int guard;

    for (guard = 0; guard < 100000; guard++) {
        unsigned int r = get32(R_SMPRATE_1371);
        if (!(r & SRC_BUSY)) return r;
    }
    return get32(R_SMPRATE_1371);
}

static unsigned int src_leggi(unsigned int reg)
{
    unsigned int r = src_attendi();

    r &= SRC_DIS | SRC_DDAC1 | SRC_DDAC2 | SRC_DADC;
    r |= reg << SRC_ADDR_SH;
    put32(R_SMPRATE_1371, r);
    return src_attendi() & 0xFFFF;
}

static void src_scrivi(unsigned int reg, unsigned int dato)
{
    unsigned int r = src_attendi();

    r &= SRC_DIS | SRC_DDAC1 | SRC_DDAC2 | SRC_DADC;
    r |= SRC_RAMWE | (reg << SRC_ADDR_SH) | (dato & 0xFFFF);
    put32(R_SMPRATE_1371, r);
}

static void src_accendi(void)
{
    unsigned int i;

    src_attendi();
    put32(R_SMPRATE_1371, SRC_DIS);

    for (i = 0; i < 0x80; i++) src_scrivi(i, 0);

    src_scrivi(SRCREG_DAC1 + SRC_TRUNC_N, 16 << 4);
    src_scrivi(SRCREG_DAC1 + SRC_INT_REGS, 16 << 10);
    src_scrivi(SRCREG_DAC2 + SRC_TRUNC_N, 16 << 4);
    src_scrivi(SRCREG_DAC2 + SRC_INT_REGS, 16 << 10);

    /* I volumi interni del convertitore: 1<<12 e' l'unita'. Lasciarli a zero
     * — come li mette il ciclo di azzeramento qui sopra — vuol dire un
     * convertitore che funziona e un'uscita muta. */
    src_scrivi(SRCREG_VOL_ADC,      1 << 12);
    src_scrivi(SRCREG_VOL_ADC + 1,  1 << 12);
    src_scrivi(SRCREG_VOL_DAC1,     1 << 12);
    src_scrivi(SRCREG_VOL_DAC1 + 1, 1 << 12);
    src_scrivi(SRCREG_VOL_DAC2,     1 << 12);
    src_scrivi(SRCREG_VOL_DAC2 + 1, 1 << 12);

    put32(R_SMPRATE_1371, 0);
}

static void rate_1371(unsigned int rate)
{
    unsigned int freq = ((rate << 15) + 1500) / 3000;
    unsigned int r;

    r = src_attendi() & (SRC_DIS | SRC_DDAC1 | SRC_DADC);
    put32(R_SMPRATE_1371, r | SRC_DDAC2);

    src_scrivi(SRCREG_DAC2 + SRC_INT_REGS,
               (src_leggi(SRCREG_DAC2 + SRC_INT_REGS) & 0x00FF) |
               ((freq >> 5) & 0xFC00));
    src_scrivi(SRCREG_DAC2 + SRC_VFREQ_FRAC, freq & 0x7FFF);

    r = src_attendi() & (SRC_DIS | SRC_DDAC1 | SRC_DADC);
    put32(R_SMPRATE_1371, r);
}

/* --- il codec AC'97 dell'ES1371 ------------------------------------------- */
static void ac97_scrivi(unsigned int reg, unsigned int val)
{
    int guard;

    for (guard = 0; guard < 100000; guard++)
        if (!(get32(R_CODEC_1371) & CODEC_WIP)) break;

    put32(R_CODEC_1371, ((reg & 0x7F) << 16) | (val & 0xFFFF));
}

/* --- il codec AK4531 dell'ES1370 ------------------------------------------ */
static void ak_scrivi(unsigned int reg, unsigned int val)
{
    int guard;

    for (guard = 0; guard < 100000; guard++)
        if (!(get32(R_STATUS) & S_CSTAT)) break;

    put32(R_CODEC_1370, ((reg & 0xFF) << 8) | (val & 0xFF));
}

/* =============================================================================
 * ES1370: la frequenza e' un divisore nel registro di controllo
 *
 * ! IL CLOCK E' 1.411200 MHz, cioe' 44100 x 32, ed e' fisso. La frequenza si
 * ottiene dividendolo, quindi non tutte sono raggiungibili esattamente: si
 * sceglie il divisore piu' vicino e si RIFERISCE la frequenza vera, perche' un
 * file suonato al 2% piu' veloce e' stonato e chi ascolta deve sapere perche'.
 * ========================================================================== */
static unsigned int rate_1370(unsigned int rate)
{
    unsigned int div = (1411200u + rate / 2) / rate;
    unsigned int c;

    if (div < 2)    div = 2;
    if (div > 0x1FFF + 2) div = 0x1FFF + 2;

    c  = get32(R_CONTROL) & ~C_PCLKDIV_MASK;
    c |= ((div - 2) & 0x1FFF) << C_PCLKDIV_SH;
    put32(R_CONTROL, c);

    return 1411200u / div;      /* quella vera */
}

/* =============================================================================
 * Il dialogo col server PCI
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

/* I numeri della famiglia. Il nome e' quello che c'e' scritto sulla scatola:
 * chi cerca il driver della propria scheda cerca quello. */
static const struct { unsigned short v, d; const char *nome; unsigned char e1371; }
g_modelli[] = {
    { 0x1274, 0x5000, "Sound Blaster PCI 64 (ES1370)",  0 },
    { 0x1274, 0x1371, "Sound Blaster PCI 128 (ES1371)", 1 },
    { 0x1274, 0x5880, "Sound Blaster 128 PCI (CT5880)", 1 },
    { 0x1102, 0x8938, "Sound Blaster 128 (EV1938)",     1 },
    { 0, 0, 0, 0 }
};

static int trova_scheda(PciDispositivo *out)
{
    PciRichiesta r;
    unsigned int ord;

    for (ord = 0; ord < 16; ord++) {
        int i, esito;

        memset(&r, 0, sizeof(r));
        r.ordinale    = ord;
        r.classe      = 0x04;               /* multimedia */
        r.sottoclasse = PCI_QUALUNQUE;
        r.venditore   = PCI_QUALUNQUE;
        r.dispositivo = PCI_QUALUNQUE;

        if (ipc_send((unsigned int)pci_pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0)
            return -1;

        esito = pci_risposta(PCI_MSG_DISPOSITIVO, out, sizeof(*out));
        if (esito != 0) return -1;          /* fine elenco, o niente risposta */

        for (i = 0; g_modelli[i].nome; i++) {
            if (g_modelli[i].v == out->venditore &&
                g_modelli[i].d == out->dispositivo) {
                strncpy(g_nome, g_modelli[i].nome, sizeof(g_nome) - 1);
                g_nome[sizeof(g_nome) - 1] = 0;
                g_1371 = g_modelli[i].e1371;
                return 0;
            }
        }
    }
    return -1;
}

static int abilita(const PciDispositivo *d)
{
    PciAzione a;
    PciEsito  e;

    memset(&a, 0, sizeof(a));
    a.bus = d->bus; a.slot = d->slot; a.funzione = d->funzione;
    /* ! IL BIT BUS MASTER E' QUELLO CHE FA LA DIFFERENZA FRA SUONARE E NON
     * SUONARE. Senza, la scheda accetta tutti i registri e non legge un byte
     * di memoria: silenzio con i contatori a posto. */
    a.bit = PCI_ABIL_IO | PCI_ABIL_BUSMASTER;

    if (ipc_send((unsigned int)pci_pid, PCI_MSG_ABILITA, &a, sizeof(a)) < 0)
        return -1;
    if (pci_risposta(PCI_MSG_ESITO, &e, sizeof(e)) != 0) return -1;
    return e.codice;
}

/* =============================================================================
 * La sonda
 * ========================================================================== */
static int es_sonda(AudioInfo *info)
{
    PciDispositivo d;

    /* ! SI ASPETTA IL SERVER PCI INVECE DI PRETENDERLO GIA' ACCESO. In
     * [modules] il kernel avvia tutto insieme e non mette in fila: se questo
     * driver chiedesse subito e trovasse chiuso, morirebbe a ogni avvio su
     * una macchina abbastanza veloce da farceli partire insieme. */
    pci_pid = ipc_attendi("pci", 5000);
    if (pci_pid <= 0) {
        printf("es1371: il server PCI non e' attivo.\n");
        printf("        Si accende con  /dev/pci.drv &  - oppure mettendo\n");
        printf("        pci = /dev/pci.drv in [modules] di kernel.cfg.\n");
        return -1;
    }

    if (trova_scheda(&d) < 0) {
        printf("es1371: nessuna Ensoniq/Creative AudioPCI sul bus.\n");
        return -1;
    }

    g_base = (g_forz_base > 0) ? (unsigned int)g_forz_base : d.bar[0];
    g_irq  = (g_forz_irq  > 0) ? (unsigned int)g_forz_irq  : d.irq_linea;

    if (!g_base || !d.bar_io[0]) {
        printf("es1371: la scheda non ha una finestra di porte I/O (BAR0).\n");
        return -1;
    }
    if (g_irq == 0 || g_irq == 0xFF) {
        printf("es1371: il BIOS non ha assegnato un IRQ alla scheda.\n");
        return -1;
    }

    if (abilita(&d) < 0) {
        printf("es1371: il server PCI non ha acceso I/O e bus master.\n");
        return -1;
    }

    if (ioport_bind(g_base, 64) < 0) {
        printf("es1371: ioport_bind(0x%x, 64) fallita.\n", g_base);
        return -1;
    }

    /* Accensione: tutto spento, poi il codec. */
    put32(R_CONTROL, 0);
    put32(R_SCTRL, 0);
    g_sctrl = 0;

    if (g_1371) {
        src_accendi();
        /* AC'97: reset, poi si aprono master e PCM. Un codec appena acceso ha
         * tutto silenziato, e «tutto silenziato» somiglia moltissimo a «il
         * driver non funziona». */
        ac97_scrivi(0x00, 0x0000);      /* reset */
        usleep(1000);
        ac97_scrivi(0x02, 0x0000);      /* master: 0 = massimo, non minimo */
        ac97_scrivi(0x18, 0x0808);      /* PCM out */
    } else {
        int i;
        /* L'AK4531: si azzera e si aprono i due volumi che servono. */
        put32(R_CONTROL, C_CDC_EN);
        for (i = 0; i < 0x20; i++) ak_scrivi(i, 0);
        ak_scrivi(0x00, 0x00);          /* master sinistro: 0 = massimo */
        ak_scrivi(0x01, 0x00);          /* master destro */
        ak_scrivi(0x02, 0x00);          /* voce sinistra */
        ak_scrivi(0x03, 0x00);          /* voce destra */
        ak_scrivi(0x16, 0x00);          /* mic gain / mux */
        ak_scrivi(0x17, 0x00);
    }

    if (dma_alloc_buffer() < 0) return -1;

    memset(info, 0, sizeof(*info));
    strncpy(info->nome, g_nome, sizeof(info->nome) - 1);
    strcpy(info->bus, "PCI");
    info->base     = g_base;
    info->irq      = g_irq;
    info->dma8     = AUDIO_DMA_NESSUNO;     /* e' la scheda a leggere la RAM */
    info->dma16    = AUDIO_DMA_NESSUNO;
    info->rate_min = 4000;
    info->rate_max = 48000;
    info->capacita = AUDIO_CAP_PCM8 | AUDIO_CAP_PCM16 |
                     AUDIO_CAP_STEREO | AUDIO_CAP_MIXER;

    /* ! LA PORTA MIDI C'E' MA NON E' UN SINTETIZZATORE, e dichiararla come
     * tale sarebbe la bugia piu' facile da fare qui. La UART di questa scheda
     * manda i byte a uno strumento ESTERNO: se non c'e' attaccato niente — che
     * e' il caso normale — non si sente nulla. Il MIDI su questa scheda lo fa
     * il sintetizzatore software della meta' comune, che dichiara da se'
     * AUDIO_CAP_MIDI_ONDA quando vede che qui non c'e' ne' FM ne' UART utile. */

    return 0;
}

/* =============================================================================
 * Il buffer
 * ========================================================================== */
#define BUF_BYTE 32768u

static int dma_alloc_buffer(void)
{
    if (g_buf) return 0;

    memset(&g_zona, 0, sizeof(g_zona));
    g_zona.byte = BUF_BYTE;
    if (dma_alloc(&g_zona) < 0) {
        printf("es1371: dma_alloc(%u) fallita\n", BUF_BYTE);
        return -1;
    }
    g_buf = (unsigned char *)g_zona.virt;
    return 0;
}

/* =============================================================================
 * apri / via / ferma
 * ========================================================================== */
static int es_apri(AudioFormato *f, unsigned char **buf, unsigned int *byte)
{
    unsigned int fmt, campioni;

    if (!g_buf) return -1;

    if (f->canali < 1) f->canali = 1;
    if (f->canali > 2) f->canali = 2;
    if (f->bit != 8)   f->bit = 16;
    if (f->rate < 4000)  f->rate = 4000;
    if (f->rate > 48000) f->rate = 48000;

    if (g_1371) rate_1371(f->rate);
    else        f->rate = rate_1370(f->rate);

    g_canali = f->canali;
    g_bit    = f->bit;

    /* ! IL BUFFER SI ADATTA AL FORMATO PERCHE' IL RITARDO E' IN TEMPO, NON IN
     * BYTE. 32 KB sono un ottavo di secondo a 44100/16/stereo e mezzo secondo
     * a 11025/8/mono: la stessa memoria, tre volte il ritardo. Si prende la
     * potenza di due piu' vicina a 50 ms per meta'. */
    {
        unsigned int bps  = f->rate * f->canali * (f->bit / 8);
        unsigned int meta = 2048;
        while (meta < bps / 20 && meta < BUF_BYTE / 2) meta <<= 1;
        g_meta     = meta;
        g_buf_byte = meta * 2;
    }

    memset(g_buf, (f->bit == 8) ? 0x80 : 0x00, g_buf_byte);
    *buf  = g_buf;
    *byte = g_buf_byte;

    /* Il formato: bit 0 = stereo, bit 1 = 16 bit, messi in SCTRL a partire
     * dal bit 2. */
    fmt = ((f->canali == 2) ? 1u : 0u) | ((f->bit == 16) ? 2u : 0u);

    g_sctrl &= ~SC_P2_FMT_MASK;
    g_sctrl |= (fmt << SC_P2_FMT_SH);

    /* ! ENDINC VUOLE LO STESSO NUMERO DEL FORMATO, non i byte per campione.
     * E' il campo con cui il DAC2 sa di quanto avanzare fra un campione e il
     * successivo, e la codifica e' la stessa del formato (0-3). Ci ho messo il
     * conto dei byte diviso quattro, che per il caso 16 bit stereo da' 1
     * invece di 3: la scheda leggeva il buffer giusto e lo interpretava
     * sfasato — suono forte, distorto, riconoscibile come «qualcosa suona ma
     * non e' quello». STINC resta zero.
     *
     * ! E LOOPSEL VA SPENTO. Il giro continuo lo fa gia' il DMA sul frame
     * (frame address + frame count si ricaricano da soli); LOOPSEL e' un'altra
     * cosa, e acceso qui fa ripetere pezzi. */
    g_sctrl &= ~((0x7u << SC_P2_STINC_SH) | (0x7u << SC_P2_ENDINC_SH) |
                 SC_P2_LOOPSEL);
    g_sctrl |= (fmt << SC_P2_ENDINC_SH);

    put32(R_SCTRL, g_sctrl);

    /* Il frame: indirizzo fisico e lunghezza in DOPPIE PAROLE meno una. */
    put32(R_MEMPAGE, PAGINA_DAC);
    put32(R_FRAME_ADDR, g_zona.fisico);
    put32(R_FRAME_CNT, (g_buf_byte / 4) - 1);

    /* Un interrupt a ogni meta'. Il contatore e' in CAMPIONI meno uno. */
    campioni = g_meta / ((f->bit / 8) * f->canali);
    put32(R_DAC2_SCOUNT, campioni - 1);

    return 0;
}

static void es_via(void)
{
    unsigned int c;

    g_sctrl &= ~SC_P2_PAUSE;
    g_sctrl |= SC_P2_INTEN;
    put32(R_SCTRL, g_sctrl);

    c = get32(R_CONTROL) | C_DAC2_EN;
    put32(R_CONTROL, c);
    g_suona = 1;
}

static void es_ferma(void)
{
    unsigned int c;

    if (!g_suona) return;
    g_suona = 0;

    c = get32(R_CONTROL) & ~C_DAC2_EN;
    put32(R_CONTROL, c);

    g_sctrl &= ~SC_P2_INTEN;
    g_sctrl |= SC_P2_PAUSE;
    put32(R_SCTRL, g_sctrl);
}

static void es_chiudi(void)
{
    es_ferma();
}

/* =============================================================================
 * L'interrupt
 * ========================================================================== */
static int es_irq(void)
{
    unsigned int st = get32(R_STATUS);
    unsigned int cnt, letti, in_corso;

    if (!(st & S_DAC2)) return -1;          /* non e' nostro */

    /* ! L'INTERRUPT SI AZZERA SPEGNENDO E RIACCENDENDO IL SUO PERMESSO, e non
     * c'e' un bit «ho servito». E' brutto e sta sul manuale: senza, la linea
     * resta alta e irq_done() la riaprirebbe su una tempesta. */
    put32(R_SCTRL, g_sctrl & ~SC_P2_INTEN);
    put32(R_SCTRL, g_sctrl);

    if (!g_suona) return -1;

    /* Quale meta' e' libera: la scheda dice a che punto e' del frame.
     *
     * ! IL CONTEGGIO CORRENTE STA NEI SEDICI BIT ALTI, e nei bassi c'e' la
     * MISURA del frame — cioe' quello che ci abbiamo scritto noi. Leggere i
     * bassi da' un numero che non cambia mai, e con un numero che non cambia
     * mai la prova di collaudo dice «il contatore DMA e' fermo» su una scheda
     * che sta suonando benissimo. Conta in doppie parole, e sale. */
    put32(R_MEMPAGE, PAGINA_DAC);
    cnt   = (get32(R_FRAME_CNT) >> 16) & 0xFFFF;
    letti = cnt * 4;
    in_corso = (letti >= g_meta) ? 1 : 0;

    return (int)(1 - in_corso);
}

static unsigned int es_avanzamento(void)
{
    put32(R_MEMPAGE, PAGINA_DAC);
    return (get32(R_FRAME_CNT) >> 16) & 0xFFFF;
}

/* =============================================================================
 * Volume
 * ========================================================================== */
static void es_volume(unsigned int pct)
{
    if (pct > 100) pct = 100;

    if (g_1371) {
        /* AC'97: 0 e' il massimo e 31 il minimo, piu' il bit 15 che silenzia.
         * ! IL VERSO E' ROVESCIATO rispetto a quello che si aspetta chiunque,
         * ed e' l'errore classico su questo codec: scrivere «il volume» invece
         * dell'attenuazione da' una scheda che tace quando la si alza. */
        unsigned int att = ((100 - pct) * 31) / 100;
        unsigned int v   = (att << 8) | att | (pct == 0 ? 0x8000 : 0);
        ac97_scrivi(0x02, v);
        ac97_scrivi(0x18, ((att << 8) | att));
    } else {
        /* AK4531: anche qui e' attenuazione, su cinque bit. */
        unsigned int att = ((100 - pct) * 31) / 100;
        ak_scrivi(0x00, att);
        ak_scrivi(0x01, att);
        ak_scrivi(0x02, att);
        ak_scrivi(0x03, att);
    }
}

/* =============================================================================
 * MIDI — la UART c'e', il sintetizzatore no
 * ========================================================================== */
static int es_midi(const unsigned char *b, unsigned int n)
{
    unsigned int i;
    unsigned int c = get32(R_CONTROL);

    if (!(c & C_UART_EN)) put32(R_CONTROL, c | C_UART_EN);

    for (i = 0; i < n; i++) {
        int guard;
        for (guard = 0; guard < 10000; guard++) {
            int st = get8(R_UART_STATO);
            if (st >= 0 && (st & 0x02)) break;   /* pronto a trasmettere */
        }
        put8(R_UART_DATI, b[i]);
    }
    return (int)n;
}

static int es_midi_vivo(void)
{
    /* Non c'e' modo di sapere se dall'altra parte del cavo MIDI c'e'
     * qualcosa: la MIDI e' unidirezionale su quel connettore. Dire «vivo»
     * sarebbe promettere un suono che dipende da un cavo. */
    return -1;
}

/* =============================================================================
 * Opzioni
 * ========================================================================== */
static int es_opzione(const char *arg, const char *valore)
{
    if (!valore) return -1;
    if (strcmp(arg, "-p") == 0) { g_forz_base = (int)strtol(valore, 0, 0); return 0; }
    if (strcmp(arg, "-q") == 0) { g_forz_irq  = (int)strtol(valore, 0, 0); return 0; }
    return -1;
}

static const AudioDorso g_dorso = {
    "es1371",
    es_opzione,
    es_sonda,
    es_apri,
    es_via,
    es_ferma,
    es_chiudi,
    es_irq,
    es_avanzamento,
    es_volume,
    es_midi,
    es_midi_vivo
};

const AudioDorso *audio_dorso_questo(void)
{
    return &g_dorso;
}

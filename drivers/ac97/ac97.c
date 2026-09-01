/* =============================================================================
 * drivers/ac97/ac97.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * AC'97 — il controller su cui sta un codec Realtek, fino al 2004 circa
 *
 *     /dev/ac97.drv          cerca la scheda e accende il servizio "audio"
 *     /dev/ac97.drv -i       la sonda: dice cosa ha trovato ed esce
 *
 * La meta' comune — anello, protocollo, prove, sintetizzatore software — sta
 * in drivers/audio/audio_comune.c. Qui ci sono soltanto i registri.
 *
 * -----------------------------------------------------------------------------
 * ! «SCHEDA AUDIO REALTEK» E' UN CODEC, NON UNA SCHEDA, ed e' il punto
 *
 * Realtek non ha mai fabbricato un controller audio. Il chip marcato ALC che
 * sta su quasi tutte le schede madri e' il CODEC: converte i numeri in
 * tensione e ha i potenziometri. Chi muove i dati e' un controller di
 * qualcun altro — Intel (ICH), VIA, nVidia, AMD — e AC'97 e' il nome del
 * PROTOCOLLO con cui i due si parlano.
 *
 * Da qui la forma di questo file: il controller si trova sul bus PCI e si
 * guida con i registri di questa specifica; il codec risponde a 64 registri
 * standard e si presenta con un numero di venditore, che qui si legge per
 * poter STAMPARE «Realtek» quando c'e' davvero. Un driver che si fosse
 * chiamato realtek.drv non avrebbe saputo guidare la stessa scheda madre con
 * un codec Analog Devices sopra — ed e' la stessa scheda madre.
 *
 * -----------------------------------------------------------------------------
 * ! DUE FINESTRE DI PORTE, E CONFONDERLE E' L'ERRORE CLASSICO
 *
 *     BAR0   i registri del CODEC — volumi, frequenza, identita'
 *     BAR1   il motore DMA del CONTROLLER — indirizzi, contatori, interrupt
 *
 * Sono due chip diversi che parlano due linguaggi diversi. Scrivere un
 * volume nel motore DMA non da' errore: da' un contatore sballato.
 *
 * -----------------------------------------------------------------------------
 * ! IL DMA E' UNA LISTA DI DESCRITTORI, E CAMBIA IL MODO DI RAGIONARE
 *
 * La Sound Blaster ha un buffer e un contatore. Qui c'e' una BDL — «buffer
 * descriptor list» — di 32 voci, ognuna «questo indirizzo, questo numero di
 * campioni, avvisami quando hai finito». Il controller le percorre in cerchio.
 *
 * Il doppio buffer si ottiene con DUE voci che puntano alle due meta' dello
 * stesso blocco, tutte e due con «avvisami». L'indice corrente (CIV) dice
 * quale sta suonando, e l'altra e' quella da riempire: la stessa domanda della
 * Sound Blaster, con una risposta piu' facile da leggere.
 *
 * ! LVI VA TENUTO AVANTI. Il controller si ferma quando arriva all'«ultima
 * voce valida», e se LVI resta indietro il suono si interrompe dopo due
 * blocchi — con tutti i registri all'aria giusta. Qui LVI si rimette a ogni
 * interrupt, perche' le voci sono due e girano.
 * ============================================================================= */

#include "libc.h"
#include "audio_proto.h"
#include "audio_dorso.h"
#include "pci_proto.h"

/* +0.001 a ogni modifica: `ac97.drv -version` la stampa. */
EX_VERSIONE("ac97.drv", "0.001");

/* =============================================================================
 * BAR0 — i registri del codec (spiazzamenti standard AC'97)
 * ========================================================================== */
#define M_RESET         0x00
#define M_MASTER        0x02
#define M_PCM_OUT       0x18
#define M_EXT_ID        0x28
#define M_EXT_CTRL      0x2A
#define M_RATE_FRONT    0x2C
#define M_VENDOR1       0x7C    /* i primi tre caratteri del venditore */
#define M_VENDOR2       0x7E

#define EXT_VRA         0x0001  /* sa cambiare frequenza */

/* =============================================================================
 * BAR1 — il motore DMA del controller
 * ========================================================================== */
#define B_PCM_OUT       0x10    /* l'inizio della «scatola» dell'uscita PCM */
#define B_BDBAR         0x00    /* + B_PCM_OUT: indirizzo della lista       */
#define B_CIV           0x04    /* voce in corso (sola lettura)             */
#define B_LVI           0x05    /* ultima voce valida                       */
#define B_SR            0x06    /* stato                                    */
#define B_PICB          0x08    /* campioni che restano nella voce in corso */
#define B_PIV           0x0A
#define B_CR            0x0B    /* controllo                                */

#define B_GLOB_CNT      0x2C
#define B_GLOB_STA      0x30

/* SR */
#define SR_DCH          0x0001  /* il motore e' fermo */
#define SR_CELV         0x0002
#define SR_LVBCI        0x0004  /* ha raggiunto l'ultima voce */
#define SR_BCIS         0x0008  /* ha finito una voce         */
#define SR_FIFOE        0x0010  /* la coda si e' svuotata     */

/* CR */
#define CR_RPBM         0x01    /* corri */
#define CR_RR           0x02    /* azzera */
#define CR_LVBIE        0x04
#define CR_FEIE         0x08
#define CR_IOCE         0x10    /* interrompi a fine voce */

/* GLOB_CNT */
#define GC_GIE          0x00000001u
#define GC_COLD         0x00000002u
#define GC_WARM         0x00000004u

/* GLOB_STA */
#define GS_PCR          0x00000100u     /* codec primario pronto */

/* Una voce della lista dei descrittori: indirizzo e lunghezza in CAMPIONI DA
 * 16 BIT — non in byte, ed e' l'errore da non fare: darle i byte fa suonare
 * ogni meta' due volte. */
#define BDL_IOC         0x80000000u
#define BDL_BUP         0x40000000u
#define BDL_VOCI        32

/* =============================================================================
 * Stato
 * ========================================================================== */
static unsigned int g_mix = 0;      /* BAR0 */
static unsigned int g_bus = 0;      /* BAR1 */
static unsigned int g_irq = 0;
static char         g_nome[32];

static DmaZona      g_zona;         /* la lista dei descrittori piu' il buffer */
static unsigned int *g_bdl = 0;
static unsigned int  g_bdl_fis = 0;
static unsigned char *g_buf = 0;
static unsigned int  g_buf_fis = 0;
static unsigned int  g_buf_byte = 0;
static unsigned int  g_meta = 0;
static unsigned int  g_suona = 0;
static unsigned int  g_vra = 0;

static int g_forz_irq = -1;

/* =============================================================================
 * Accesso ai due chip
 * ========================================================================== */
static void mix_w(unsigned int r, unsigned int v) { ioport_out16(g_mix + r, v); }
static int  mix_r(unsigned int r)                 { return ioport_in16(g_mix + r); }

static void bus_w8 (unsigned int r, unsigned int v) { ioport_out(g_bus + r, v); }
static int  bus_r8 (unsigned int r)                 { return ioport_in(g_bus + r); }
static void bus_w16(unsigned int r, unsigned int v) { ioport_out16(g_bus + r, v); }
static int  bus_r16(unsigned int r)                 { return ioport_in16(g_bus + r); }
static void bus_w32(unsigned int r, unsigned int v) { ioport_out32(g_bus + r, v); }
static unsigned int bus_r32(unsigned int r)
{
    unsigned int v = 0;
    ioport_in32(g_bus + r, &v);
    return v;
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

static const struct { unsigned short v, d; const char *nome; } g_modelli[] = {
    { 0x8086, 0x2415, "Intel 82801AA AC'97", },
    { 0x8086, 0x2425, "Intel 82801AB AC'97", },
    { 0x8086, 0x2445, "Intel 82801BA AC'97", },
    { 0x8086, 0x2485, "Intel 82801CA AC'97", },
    { 0x8086, 0x24C5, "Intel 82801DB AC'97", },
    { 0x8086, 0x24D5, "Intel 82801EB AC'97", },
    { 0x8086, 0x266E, "Intel 82801FB AC'97", },
    { 0x1106, 0x3058, "VIA VT82C686 AC'97", },
    { 0x10DE, 0x01B1, "nVidia nForce AC'97", },
    { 0, 0, 0 }
};

static int trova_scheda(PciDispositivo *out)
{
    PciRichiesta r;
    unsigned int ord;

    for (ord = 0; ord < 16; ord++) {
        int i, esito;

        memset(&r, 0, sizeof(r));
        r.ordinale    = ord;
        r.classe      = 0x04;
        r.sottoclasse = PCI_QUALUNQUE;
        r.venditore   = PCI_QUALUNQUE;
        r.dispositivo = PCI_QUALUNQUE;

        if (ipc_send((unsigned int)pci_pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0)
            return -1;
        esito = pci_risposta(PCI_MSG_DISPOSITIVO, out, sizeof(*out));
        if (esito != 0) return -1;

        for (i = 0; g_modelli[i].nome; i++)
            if (g_modelli[i].v == out->venditore &&
                g_modelli[i].d == out->dispositivo) {
                strncpy(g_nome, g_modelli[i].nome, sizeof(g_nome) - 1);
                g_nome[sizeof(g_nome) - 1] = 0;
                return 0;
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
    a.bit = PCI_ABIL_IO | PCI_ABIL_BUSMASTER;

    if (ipc_send((unsigned int)pci_pid, PCI_MSG_ABILITA, &a, sizeof(a)) < 0)
        return -1;
    if (pci_risposta(PCI_MSG_ESITO, &e, sizeof(e)) != 0) return -1;
    return e.codice;
}

/* =============================================================================
 * Chi e' il codec — i tre caratteri che dicono «Realtek»
 *
 * I registri 0x7C e 0x7E tengono l'identificativo del venditore: tre lettere
 * ASCII piu' un numero di modello. «ALG» e' Avance Logic, che e' Realtek —
 * l'azienda ha comprato il nome e i codec ALC sono la sua linea. Leggerlo
 * serve a una cosa sola ma vera: dire all'utente cosa ha, invece di dirgli
 * «AC'97» e lasciarlo cercare.
 * ========================================================================== */
static void nome_codec(char *fuori, unsigned int max)
{
    int a = mix_r(M_VENDOR1), b = mix_r(M_VENDOR2);
    char v[4];

    if (a < 0 || b < 0 || (a == 0 && b == 0)) { fuori[0] = 0; return; }

    v[0] = (char)((a >> 8) & 0x7F);
    v[1] = (char)(a & 0x7F);
    v[2] = (char)((b >> 8) & 0x7F);
    v[3] = 0;
    if (v[0] < 32 || v[1] < 32 || v[2] < 32) { fuori[0] = 0; return; }

    if (strcmp(v, "ALG") == 0 || strcmp(v, "RTK") == 0)
        snprintf(fuori, max, "Realtek ALC%d", b & 0xFF);
    else
        snprintf(fuori, max, "codec %s%02x", v, b & 0xFF);
}

/* =============================================================================
 * Il buffer e la lista dei descrittori, in una zona sola
 *
 * ! STANNO INSIEME PERCHE' SYS_DMA_ALLOC NON SI PUO' CHIAMARE DUE VOLTE con
 * leggerezza: da' memoria fisicamente contigua e NON LIBERABILE, la risorsa
 * piu' scarsa del sistema (vedi kernel/include/syscall.h). Una zona sola, la
 * lista in cima e il buffer dopo.
 * ========================================================================== */
#define BUF_BYTE  32768u
#define BDL_BYTE  (BDL_VOCI * 8u)
#define ZONA_BYTE 4096u                 /* la prima pagina: la lista */

static int prendi_memoria(void)
{
    if (g_buf) return 0;

    memset(&g_zona, 0, sizeof(g_zona));
    g_zona.byte = ZONA_BYTE + BUF_BYTE;
    if (dma_alloc(&g_zona) < 0) {
        printf("ac97: dma_alloc(%u) fallita\n", ZONA_BYTE + BUF_BYTE);
        return -1;
    }

    g_bdl     = (unsigned int *)g_zona.virt;
    g_bdl_fis = g_zona.fisico;
    g_buf     = (unsigned char *)(g_zona.virt + ZONA_BYTE);
    g_buf_fis = g_zona.fisico + ZONA_BYTE;
    return 0;
}

/* =============================================================================
 * La sonda
 * ========================================================================== */
static int ac_sonda(AudioInfo *info)
{
    PciDispositivo d;
    char           codec[24];
    int            guard, ext;

    pci_pid = ipc_attendi("pci", 5000);
    if (pci_pid <= 0) {
        printf("ac97: il server PCI non e' attivo.\n");
        printf("      Si accende con  /dev/pci.drv &  - oppure mettendo\n");
        printf("      pci = /dev/pci.drv in [modules] di kernel.cfg.\n");
        return -1;
    }

    if (trova_scheda(&d) < 0) {
        printf("ac97: nessun controller AC'97 riconosciuto sul bus.\n");
        return -1;
    }

    g_mix = d.bar[0];
    g_bus = d.bar[1];
    g_irq = (g_forz_irq > 0) ? (unsigned int)g_forz_irq : d.irq_linea;

    if (!g_mix || !g_bus || !d.bar_io[0] || !d.bar_io[1]) {
        printf("ac97: la scheda non ha le due finestre di porte attese.\n");
        return -1;
    }
    if (g_irq == 0 || g_irq == 0xFF) {
        printf("ac97: il BIOS non ha assegnato un IRQ alla scheda.\n");
        return -1;
    }

    if (abilita(&d) < 0) {
        printf("ac97: il server PCI non ha acceso I/O e bus master.\n");
        return -1;
    }

    if (ioport_bind(g_mix, 256) < 0 || ioport_bind(g_bus, 64) < 0) {
        printf("ac97: ioport_bind fallita (mixer 0x%x, bus 0x%x)\n", g_mix, g_bus);
        return -1;
    }

    /* --- accensione del collegamento col codec --- */
    bus_w32(B_GLOB_CNT, GC_COLD | GC_GIE);

    /* ! SI ASPETTA CHE IL CODEC DICA DI ESSERE PRONTO. Scrivergli i volumi
     * prima e' l'errore che da' una scheda muta con tutti i registri
     * apparentemente scritti: le scritture arrivano e vengono buttate. */
    for (guard = 0; guard < 1000; guard++) {
        if (bus_r32(B_GLOB_STA) & GS_PCR) break;
        usleep(1000);
    }
    if (!(bus_r32(B_GLOB_STA) & GS_PCR)) {
        printf("ac97: il codec non risponde: GLOB_STA non dice 'primario pronto'.\n");
        return -1;
    }

    mix_w(M_RESET, 0);                  /* un valore qualunque: e' il reset */
    usleep(1000);

    /* ! ZERO E' IL MASSIMO. Nei registri AC'97 il numero e' l'ATTENUAZIONE:
     * scrivere «il volume» invece dell'attenuazione da' una scheda che tace
     * quando la si alza. E' l'errore piu' comune su questo codec. */
    mix_w(M_MASTER,  0x0000);
    mix_w(M_PCM_OUT, 0x0808);

    ext = mix_r(M_EXT_ID);
    g_vra = (ext > 0 && (ext & EXT_VRA)) ? 1 : 0;
    if (g_vra) mix_w(M_EXT_CTRL, mix_r(M_EXT_CTRL) | EXT_VRA);

    if (prendi_memoria() < 0) return -1;

    nome_codec(codec, sizeof(codec));

    memset(info, 0, sizeof(*info));
    if (codec[0]) snprintf(info->nome, sizeof(info->nome), "%s", codec);
    else          snprintf(info->nome, sizeof(info->nome), "%s", g_nome);
    strcpy(info->bus, "PCI");
    info->base     = g_bus;
    info->irq      = g_irq;
    info->dma8     = AUDIO_DMA_NESSUNO;
    info->dma16    = AUDIO_DMA_NESSUNO;
    /* ! I 16 BIT STEREO NON SONO UNA SCELTA: l'AC-link porta campioni a 16
     * bit su due canali e basta. Dichiarare gli 8 bit vorrebbe dire
     * prometterli e poi convertire di nascosto. */
    info->capacita = AUDIO_CAP_PCM16 | AUDIO_CAP_STEREO | AUDIO_CAP_MIXER;
    info->rate_min = g_vra ? 8000  : 48000;
    info->rate_max = 48000;

    printf("ac97: %s, %s, frequenza %s\n", g_nome,
           codec[0] ? codec : "codec non identificato",
           g_vra ? "variabile" : "fissa a 48000 Hz");
    return 0;
}

/* =============================================================================
 * apri / via / ferma
 * ========================================================================== */
static int ac_apri(AudioFormato *f, unsigned char **buf, unsigned int *byte)
{
    unsigned int meta_camp;

    if (!g_buf) return -1;

    /* Il formato non si tratta: l'AC-link e' questo. */
    f->bit    = 16;
    f->canali = 2;

    if (!g_vra) {
        f->rate = 48000;
    } else {
        if (f->rate < 8000)  f->rate = 8000;
        if (f->rate > 48000) f->rate = 48000;
        mix_w(M_RATE_FRONT, f->rate);
        /* ! SI RILEGGE. Un codec con VRA puo' concedere una frequenza vicina
         * invece di quella chiesta, e chi riempie l'anello deve sapere quale:
         * suonare 44100 credendo 48000 e' il 9% piu' lento, cioe' stonato. */
        {
            int letto = mix_r(M_RATE_FRONT);
            if (letto > 0) f->rate = (unsigned int)letto;
        }
    }

    /* Meta' buffer da circa 50 ms, potenza di due. */
    {
        unsigned int bps = f->rate * 4;
        unsigned int m = 2048;
        while (m < bps / 20 && m < BUF_BYTE / 2) m <<= 1;
        g_meta     = m;
        g_buf_byte = m * 2;
    }

    memset(g_buf, 0, g_buf_byte);
    *buf  = g_buf;
    *byte = g_buf_byte;

    /* --- azzeramento del motore --- */
    bus_w8(B_PCM_OUT + B_CR, CR_RR);
    { int g; for (g = 0; g < 1000; g++) { if (!(bus_r8(B_PCM_OUT + B_CR) & CR_RR)) break; usleep(100); } }

    /* --- la lista: DUE voci, le due meta', tutte e due con «avvisami» --- */
    meta_camp = g_meta / 2;             /* campioni da 16 bit */
    g_bdl[0] = g_buf_fis;
    g_bdl[1] = BDL_IOC | (meta_camp & 0xFFFF);
    g_bdl[2] = g_buf_fis + g_meta;
    g_bdl[3] = BDL_IOC | (meta_camp & 0xFFFF);

    bus_w32(B_PCM_OUT + B_BDBAR, g_bdl_fis);
    bus_w8 (B_PCM_OUT + B_LVI, 1);
    bus_w16(B_PCM_OUT + B_SR, SR_LVBCI | SR_BCIS | SR_FIFOE);

    return 0;
}

static void ac_via(void)
{
    bus_w8(B_PCM_OUT + B_LVI, 1);
    bus_w8(B_PCM_OUT + B_CR, CR_RPBM | CR_IOCE | CR_FEIE | CR_LVBIE);
    g_suona = 1;
}

static void ac_ferma(void)
{
    if (!g_suona) return;
    g_suona = 0;
    bus_w8(B_PCM_OUT + B_CR, 0);
    bus_w16(B_PCM_OUT + B_SR, SR_LVBCI | SR_BCIS | SR_FIFOE);
}

static void ac_chiudi(void) { ac_ferma(); }

/* =============================================================================
 * L'interrupt
 * ========================================================================== */
static int ac_irq(void)
{
    int st = bus_r16(B_PCM_OUT + B_SR);
    int civ;

    if (st < 0 || !(st & (SR_BCIS | SR_LVBCI | SR_FIFOE))) return -1;

    /* Lo stato si azzera SCRIVENDOCI SOPRA i bit accesi. */
    bus_w16(B_PCM_OUT + B_SR, (unsigned int)(st & (SR_LVBCI | SR_BCIS | SR_FIFOE)));

    if (!g_suona) return -1;

    civ = bus_r8(B_PCM_OUT + B_CIV);
    if (civ < 0) return -1;

    /* ! LVI SI RIMETTE A OGNI GIRO, e non e' burocrazia: il controller si
     * ferma quando raggiunge l'«ultima voce valida», e con due voci che
     * girano quella soglia va spostata avanti in continuazione. Lasciarla
     * indietro da' un suono che si interrompe dopo due meta' con tutti i
     * registri all'aria giusta. */
    bus_w8(B_PCM_OUT + B_LVI, (unsigned int)((civ + 1) & 1));

    /* La voce in corso e' civ%2; l'altra e' libera. */
    return (int)(1 - (civ & 1));
}

static unsigned int ac_avanzamento(void)
{
    int v = bus_r16(B_PCM_OUT + B_PICB);
    return (v < 0) ? 0 : (unsigned int)v;
}

/* =============================================================================
 * Volume
 * ========================================================================== */
static void ac_volume(unsigned int pct)
{
    unsigned int att;

    if (pct > 100) pct = 100;

    /* Sei bit di attenuazione sul master, cinque sul PCM. Zero = massimo. */
    att = ((100 - pct) * 31) / 100;
    mix_w(M_MASTER, (att << 8) | att | (pct == 0 ? 0x8000u : 0u));

    /* Il PCM ha 0x08 come «unita'»: sopra amplifica, sotto attenua. */
    {
        unsigned int p = ((100 - pct) * 31) / 100;
        mix_w(M_PCM_OUT, (p << 8) | p);
    }
}

/* =============================================================================
 * MIDI — non c'e', e si dice
 * ========================================================================== */
static int ac_midi(const unsigned char *b, unsigned int n)
{
    (void)b; (void)n;
    /* ! UNA AC'97 NON HA NE' SINTETIZZATORE NE' PORTA MIDI. Il MIDI su questa
     * scheda lo fa il sintetizzatore software della meta' comune, che si
     * accende da solo perche' qui non c'e' ne' AUDIO_CAP_MIDI_FM ne'
     * AUDIO_CAP_MIDI_UART. Questa funzione non viene mai chiamata; esiste
     * perche' l'interfaccia la prevede, e rispondere 0 e' l'unica cosa vera. */
    return 0;
}

static int ac_midi_vivo(void) { return -1; }

static int ac_opzione(const char *arg, const char *valore)
{
    if (!valore) return -1;
    if (strcmp(arg, "-q") == 0) { g_forz_irq = (int)strtol(valore, 0, 0); return 0; }
    return -1;
}

static const AudioDorso g_dorso = {
    "ac97",
    ac_opzione,
    ac_sonda,
    ac_apri,
    ac_via,
    ac_ferma,
    ac_chiudi,
    ac_irq,
    ac_avanzamento,
    ac_volume,
    ac_midi,
    ac_midi_vivo
};

const AudioDorso *audio_dorso_questo(void)
{
    return &g_dorso;
}

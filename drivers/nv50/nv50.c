/* =============================================================================
 * drivers/nv50/nv50.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * /dev/nv50.drv — il display delle schede NVIDIA G80 e seguenti (NV50, 8000M,
 * 9000M, le integrate MCP77/MCP79) (23 ottobre 2026, voce 23 della coda)
 *
 * Nasce per il portatile Compaq Presario CQ60: GeForce 8200M G (10de:0845,
 * MCP77), pannello 1366x768. Il BIOS video (VBE) arriva a 1280x720, e il
 * pannello allarga quell'immagine a tutto schermo. Il nativo vuole il display
 * della scheda programmato a mano: l'utente ha scelto questa strada.
 *
 * PASSO 1 (questo): `nv50.drv -stato` LEGGE e basta. Dice quale testa pilota il
 * pannello, a che risoluzione lo pilota (la nativa: la scrive il BIOS quando
 * accende il pannello), e con quale framebuffer, pitch, formato e scala. Serve
 * a sapere dal portatile vero, prima di scrivere un solo registro, se basta
 * cambiare il framebuffer e lo scaler (i tempi del pannello restano quelli del
 * BIOS: niente orologi da ricalcolare) o se bisogna prendersi tutto il display.
 *
 * PASSO 2 (-prova): ci si prende il display e si accende il pannello a
 * 1366x768 nativi con un motivo di prova. Vedi piu' in basso perche' serve il
 * display intero e non quattro metodi.
 *
 * ! NVIDIA NON HA PUBBLICATO QUESTI REGISTRI PER LE G80. Le due fonti sono:
 *
 *   - xf86-video-nv (g80_display.c, g80_sor.c), scritto da NVIDIA stessa e
 *     rilasciato con licenza MIT: i comandi del display (i «metodi» 0x800...
 *     di ogni testa) e il modo di mandarli per registri, 0x610300/0x610304;
 *     e la risoluzione nativa del pannello letta dallo stato lasciato dal
 *     BIOS (0x610050 dice quale testa, 0x610B4C la misura);
 *   - nouveau (nvkm/engine/disp/g84.c, MIT): per ogni metodo, il registro da
 *     cui si rilegge. Ogni metodo ne ha due: quello PREPARATO (al registro
 *     della tabella) e quello IN USO (quattro byte dopo). La seconda testa e'
 *     0x540 piu' in la'.
 *
 * ! QEMU NON HA UNA SCHEDA NVIDIA: questo programma si prova solo sul
 * portatile. Senza scheda NVIDIA dice che non c'e' ed esce.
 * ============================================================================= */
#include "libc.h"
#include "pci_proto.h"

/* +0.001 a ogni modifica: `nv50.drv -version` la stampa. */
EX_VERSIONE("nv50.drv", "0.004");

#define NV_VENDITORE   0x10DE

/* Le finestre della BAR0 che servono: la BAR0 e' di 16 MB, ma i registri del
 * display stanno in pochi kilobyte. Si mappa solo quello. */
typedef struct {
    unsigned int base, byte;
    volatile unsigned int *v;
} Finestra;

static Finestra g_fin[] = {
    { 0x000000, 0x1000, 0 },        /* PMC: il chip */
    { 0x00E000, 0x1000, 0 },        /* PBUS: gli I2C, per l'EDID (passo dopo) */
    { 0x100000, 0x1000, 0 },        /* PFB: quanta memoria video */
    { 0x610000, 0x2000, 0 },        /* il display: stato, comandi, metodi */
    { 0x614000, 0x1000, 0 },        /* gli orologi delle teste e dei SOR */
    { 0x616000, 0x1000, 0 },        /* le capacita' delle teste */
    { 0x619000, 0x1000, 0 },        /* la VGA ereditata */
    { 0x61A000, 0x2000, 0 },        /* i DAC */
    { 0x61C000, 0x2000, 0 },        /* i SOR: LVDS e digitali */
};
#define N_FIN (sizeof(g_fin) / sizeof(g_fin[0]))

static unsigned int g_bar0, g_bar1;

/* -avvio: ZITTO. Gira da /boot/avvio.sh a ogni accensione, e su ogni macchina
 * che non e' il portatile per cui e' scritto (altra scheda, altro pannello)
 * deve uscire senza una parola. Restano gli errori veri e la riga finale. */
static int g_zitto;
#define TRACCIA(...)  do { if (!g_zitto) printf(__VA_ARGS__); } while (0)

static volatile unsigned int *reg_di(unsigned int reg)
{
    unsigned int i;

    for (i = 0; i < N_FIN; i++)
        if (g_fin[i].v && reg >= g_fin[i].base && reg < g_fin[i].base + g_fin[i].byte)
            return &g_fin[i].v[(reg - g_fin[i].base) / 4];
    return 0;
}

static unsigned int rd(unsigned int reg)
{
    volatile unsigned int *p = reg_di(reg);

    return p ? *p : 0xDEADBEEF;     /* fuori dalle finestre: si vede subito */
}

static void wr(unsigned int reg, unsigned int v)
{
    volatile unsigned int *p = reg_di(reg);

    if (p) *p = v;
    else   printf("nv50: ! scrittura a 0x%06x fuori dalle finestre, saltata\n", reg);
}

/* --- la scheda, dal servizio PCI ---------------------------------------------- */

static int chiedi_pci(int pid, unsigned int ordinale, PciDispositivo *out)
{
    PciRichiesta  r;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           t;

    r.ordinale    = ordinale;
    r.classe      = 0x03;           /* schermo */
    r.sottoclasse = PCI_QUALUNQUE;
    r.venditore   = PCI_QUALUNQUE;  /* il servizio cerca per classe O per */
    r.dispositivo = PCI_QUALUNQUE;  /* venditore: NVIDIA si guarda qui sotto */

    if (ipc_send((unsigned int)pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0) return -1;
    for (t = 0; t < 8; t++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return -1;
        if ((int)meta.sender_pid != pid) continue;
        if (meta.tipo == PCI_MSG_FINE) return 0;
        if (meta.tipo == PCI_MSG_DISPOSITIVO && meta.len >= sizeof(*out)) {
            memcpy(out, buf, sizeof(*out));
            return 1;
        }
        return -1;
    }
    return -1;
}

static int trova_scheda(PciDispositivo *d)
{
    int pid = ipc_attendi(PCI_SERVIZIO, 5000);

    if (pid <= 0) {
        printf("nv50: il servizio PCI non c'e'. Avvialo:  /dev/pci.drv &\n");
        return -1;
    }
    {
        unsigned int ord;
        int          n = 0;

        for (ord = 0; ord < 8; ord++) {
            n = chiedi_pci(pid, ord, d);
            if (n != 1 || d->venditore == NV_VENDITORE) break;
        }
        if (n != 1) {
            TRACCIA("nv50: nessuna scheda video NVIDIA su questa macchina\n");
            return -1;
        }
    }
    /* ! LA BAR0 E' DI MEMORIA E SONO I REGISTRI: la BAR1 e' la memoria video
     * (il framebuffer), la BAR3 la finestra RAMIN. */
    if (d->bar[0] == 0 || d->bar_io[0]) {
        printf("nv50: %04x:%04x senza BAR0 di memoria\n", d->venditore, d->dispositivo);
        return -1;
    }
    return 0;
}

static int mappa(void)
{
    unsigned int i;

    for (i = 0; i < N_FIN; i++) {
        MmioZona m;
        int      rc;

        m.fisico = g_bar0 + g_fin[i].base;
        m.byte   = g_fin[i].byte;
        rc = mmio_map(&m);
        if (rc != 0) {
            printf("nv50: mmio_map(0x%08x, 0x%x) rifiutata (%d)\n", m.fisico, m.byte, rc);
            return -1;
        }
        g_fin[i].v = (volatile unsigned int *)(unsigned long)m.virt;
    }
    return 0;
}

/* =============================================================================
 * I METODI DI UNA TESTA E DOVE SI RILEGGONO (nouveau, g84_disp_core_mthd_head)
 * ============================================================================= */
typedef struct {
    unsigned short metodo;
    unsigned int   reg;             /* preparato; +4 in uso; +0x540 testa 1 */
    const char    *nome;
} Metodo;

static const Metodo METODI[] = {
    { 0x0800, 0x610ad8, "?" },
    { 0x0804, 0x610ad0, "orologio (kHz)" },
    { 0x0808, 0x610a48, "interlacciato" },
    { 0x080c, 0x610a78, "?" },
    { 0x0810, 0x610ac0, "?" },
    { 0x0814, 0x610af8, "totale (v<<16|h)" },
    { 0x0818, 0x610b00, "sync fine" },
    { 0x081c, 0x610ae8, "blank fine" },
    { 0x0820, 0x610af0, "blank inizio" },
    { 0x0824, 0x610b08, "skew" },
    { 0x0828, 0x610b10, "?" },
    { 0x082c, 0x610a68, "?" },
    { 0x0830, 0x610a60, "?" },
    { 0x0838, 0x610a40, "?" },
    { 0x0840, 0x610a24, "LUT modo" },
    { 0x0844, 0x610a2c, "LUT indirizzo" },
    { 0x0848, 0x610aa8, "?" },
    { 0x084c, 0x610ab0, "?" },
    { 0x085c, 0x610c5c, "?" },
    { 0x0860, 0x610a84, "framebuffer, offset" },
    { 0x0864, 0x610a90, "framebuffer, ?" },
    { 0x0868, 0x610b18, "framebuffer, misura (h<<16|w)" },
    { 0x086c, 0x610b20, "framebuffer, pitch" },
    { 0x0870, 0x610ac8, "framebuffer, formato" },
    { 0x0874, 0x610a38, "framebuffer, contesto DMA" },
    { 0x0878, 0x610c50, "?" },
    { 0x0880, 0x610a58, "cursore" },
    { 0x0884, 0x610a9c, "cursore, indirizzo" },
    { 0x089c, 0x610c68, "?" },
    { 0x08a0, 0x610a70, "dithering" },
    { 0x08a4, 0x610a50, "scala, controllo" },
    { 0x08a8, 0x610ae0, "?" },
    { 0x08c0, 0x610b28, "vista, posizione" },
    { 0x08c4, 0x610b30, "?" },
    { 0x08c8, 0x610b40, "vista, misura letta" },
    { 0x08d4, 0x610b38, "?" },
    { 0x08d8, 0x610b48, "uscita, misura" },
    { 0x08dc, 0x610b50, "uscita, misura 2" },
    { 0x0900, 0x610a18, "?" },
    { 0x0904, 0x610ab8, "?" },
    { 0, 0, 0 }
};

static unsigned int in_uso(int testa, unsigned short metodo)
{
    const Metodo *m;

    for (m = METODI; m->metodo; m++)
        if (m->metodo == metodo) return rd(m->reg + 4 + (unsigned int)testa * 0x540);
    return 0;
}

static void misura(const char *cosa, unsigned int v)
{
    printf("  %-22s %ux%u\n", cosa, v & 0x3fff, (v >> 16) & 0x3fff);
}

static const char *formato(unsigned int f)
{
    switch (f & 0xff00) {
        case 0x1E00: return "8 bit con tavolozza";
        case 0xE900: return "15 bit";
        case 0xE800: return "16 bit";
        case 0xCF00: return "24/32 bit";
        default:     return "sconosciuto";
    }
}

static void testa(int t, int tutto)
{
    const Metodo *m;
    unsigned int  pitch = in_uso(t, 0x086c);

    printf("\nTesta %d:\n", t);
    misura("pannello (uscita)", in_uso(t, 0x08d8));
    misura("framebuffer", in_uso(t, 0x0868));
    printf("  %-22s %u byte%s\n", "pitch", pitch & 0xfffff,
           (pitch & 0x100000) ? ", lineare" : ", a blocchi");
    printf("  %-22s %s (0x%08x)\n", "formato", formato(in_uso(t, 0x0870)), in_uso(t, 0x0870));
    misura("vista letta", in_uso(t, 0x08c8));
    printf("  %-22s 0x%08x (%s)\n", "scala", in_uso(t, 0x08a4),
           in_uso(t, 0x08a4) ? "accesa" : "spenta");
    printf("  %-22s %u kHz\n", "orologio", in_uso(t, 0x0804) & 0x3fffff);
    printf("  %-22s 0x%08x\n", "framebuffer offset", in_uso(t, 0x0860));

    if (!tutto) return;
    printf("  metodo  preparato   in uso\n");
    for (m = METODI; m->metodo; m++) {
        unsigned int r = m->reg + (unsigned int)t * 0x540;

        printf("  0x%04x  0x%08x  0x%08x  %s\n", m->metodo + t * 0x400, rd(r), rd(r + 4), m->nome);
    }
}

static int stato(int tutto)
{
    unsigned int boot0 = rd(0x000000), sel = rd(0x610050), t;

    printf("Chip: PMC_BOOT_0 = 0x%08x (architettura 0x%02x)\n", boot0, (boot0 >> 20) & 0xff);
    printf("Display:\n");
    printf("  0x610024 interruzioni/supervisore  0x%08x\n", rd(0x610024));
    printf("  0x610050 teste attive              0x%08x\n", sel);
    printf("  0x610200 canale principale         0x%08x\n", rd(0x610200));
    printf("  0x610300 comandi per registri      0x%08x\n", rd(0x610300));
    printf("  0x610304 dato del comando          0x%08x\n", rd(0x610304));
    printf("  SOR0 0x61C004 0x%08x  0x61C030 0x%08x\n", rd(0x61C004), rd(0x61C030));
    printf("  SOR1 0x61C804 0x%08x  0x61C830 0x%08x\n", rd(0x61C804), rd(0x61C830));
    for (t = 0; t < 2; t++)
        printf("  PLL testa %u: 0x%08x 0x%08x 0x%08x\n", t, rd(0x614100 + t * 0x800),
               rd(0x614104 + t * 0x800), rd(0x614108 + t * 0x800));

    /* Come in g80_sor.c (GetLVDSNativeMode): 2 nei bit 0-1 = la testa 0 e'
     * sull'LVDS, 2 nei bit 8-9 = la testa 1. */
    if ((sel & 3) == 2)              printf("Il pannello (LVDS) e' sulla testa 0.\n");
    else if ((sel & 0x300) == 0x200) printf("Il pannello (LVDS) e' sulla testa 1.\n");
    else                             printf("Nessuna testa sull'LVDS (0x610050 = 0x%08x).\n", sel);

    testa(0, tutto);
    testa(1, tutto);
    return 0;
}

/* =============================================================================
 * PASSO 2: nv50.drv -prova — PRENDERE IL DISPLAY E ACCENDERE IL PANNELLO NATIVO
 *
 * Misurato sul portatile (23 ottobre 2026, -tutto): il BIOS mostra il modo
 * VBE dalla strada VGA ereditata, e del canale principale del display ha
 * scritto solo i TEMPI della testa 0 (quelli nativi del pannello: 1366x768,
 * 69,3 MHz) e la misura d'uscita. Framebuffer, vista e scala sono a zero.
 * Quindi non basta cambiare quattro metodi: ci si prende il display intero,
 * come fa xf86-video-nv (G80DispPreInit, G80DispInit, xf86SetDesiredModes per
 * una testa e un SOR), riusando i tempi che il BIOS ha gia' calcolato.
 *
 * ! E' UNA PROVA, e lo schermo di prima NON torna: dopo, il pannello mostra il
 * motivo di prova finche' non si riavvia. Il sistema intanto gira come prima
 * (telnet compreso): il kernel e la scrivania scrivono ancora nel framebuffer
 * del BIOS, all'inizio della memoria video. Per questo il motivo sta a 8 MB e
 * non a zero: altrimenti la console ci scriverebbe sopra col pitch vecchio.
 *
 * ! OGNI ATTESA HA UNA SCADENZA. Sul ferro un bit che non arriva vuol dire una
 * sequenza sbagliata, e un ciclo senza fine vorrebbe dire una sessione telnet
 * appesa e nessuna riga che dica dove: qui si stampa e si va avanti o ci si
 * ferma.
 * ============================================================================= */
#define FB_OFFSET   0x800000u       /* dove sta il framebuffer nuovo, in VRAM */
#define FB_W        1366u
#define FB_H        768u
#define FB_PITCH    5632u           /* 1366*4 = 5464, arrotondato a 256 */

static unsigned int g_vram_kb;      /* xf86-video-nv: videoRam, in KB */
static volatile unsigned char *g_vram;
static int g_scadute;

static int aspetta(unsigned int reg, unsigned int maschera, unsigned int valore,
                   unsigned int ms, const char *cosa)
{
    unsigned int t0 = uptime_ms();

    while ((rd(reg) & maschera) != valore)
        if (uptime_ms() - t0 > ms) {
            printf("nv50: ! scaduta l'attesa: %s (0x%06x = 0x%08x)\n", cosa, reg, rd(reg));
            g_scadute++;
            return -1;
        }
    return 0;
}

/* --- il PLL della testa (G80CalcPLL, architetture <= 0xa0, 0xaa, 0xac) -------- */
static int arrotonda(double x) { return (int)(x + 0.5); }

static void calcola_pll(double pclk, int *pna, int *pma, int *pnb, int *pmb, int *pp)
{
    const double refclk = 27000.0, minVcoA = 100000, maxVcoA = 400000;
    const double minVcoB = 600000, minUA = 2000, maxUA = 400000;
    const double minUB = 50000, maxUB = 200000;
    double maxVcoB = 1400000, vcoB, migliore = 1e30;
    int lowP, highP, na, ma, nb, mb, p;

    *pna = *pma = *pnb = *pmb = *pp = 0;
    if (maxVcoB < pclk + pclk / 200) maxVcoB = pclk + pclk / 200;
    if (minVcoB / (1 << 6) > pclk) pclk = minVcoB / (1 << 6);

    vcoB = maxVcoB - maxVcoB / 200;
    lowP = 0;
    vcoB /= 1 << (lowP + 1);
    while (pclk <= vcoB && lowP < 6) { vcoB /= 2; lowP++; }

    vcoB = maxVcoB + maxVcoB / 200;
    highP = lowP;
    vcoB /= 1 << (highP + 1);
    while (pclk <= vcoB && highP < 6) { vcoB /= 2; highP++; }

    for (p = lowP; p <= highP; p++)
        for (ma = 1; ma <= 255; ma++) {
            if (refclk / ma < minUA) break;
            if (refclk / ma > maxUA) continue;
            for (na = 1; na <= 255; na++) {
                if (refclk * na / ma < minVcoA || refclk * na / ma > maxVcoA) continue;
                for (mb = 1; mb <= 31; mb++) {
                    double f, e;

                    if (refclk * na / ma / mb < minUB) break;
                    if (refclk * na / ma / mb > maxUB) continue;
                    nb = arrotonda(pclk * (1 << p) * (ma / (double)na) * mb / refclk);
                    if (nb > 31) break;
                    if (nb < 1) continue;
                    f = refclk * (na / (double)ma) * (nb / (double)mb) / (1 << p);
                    e = f > pclk ? f - pclk : pclk - f;
                    if (e < migliore) {
                        *pna = na; *pma = ma; *pnb = nb; *pmb = mb; *pp = p;
                        migliore = e;
                    }
                }
            }
        }
}

static unsigned int g_pclk;         /* kHz, della testa 0 */

/* G80CrtcSetPClk per la testa 0, e G80SorSetPClk per il SOR0 */
static void imposta_pll(void)
{
    unsigned int lo = rd(0x614104), hi = rd(0x614108);
    int na, ma, nb, mb, p;

    wr(0x614100, 0x10000610);
    lo &= 0xff00ff00;
    hi &= 0x8000ff00;
    calcola_pll((double)g_pclk, &na, &ma, &nb, &mb, &p);
    lo |= ((unsigned int)ma << 16) | (unsigned int)na;
    hi |= ((unsigned int)p << 28) | ((unsigned int)mb << 16) | (unsigned int)nb;
    wr(0x614104, lo);
    wr(0x614108, hi);
    wr(0x614200, 0);
    wr(0x614300, 0x70000 | (g_pclk > 165000 ? 0x101 : 0));
    TRACCIA("nv50:   PLL testa 0 per %u kHz: A %d/%d, B %d/%d, P %d -> 0x%08x 0x%08x\n",
           g_pclk, na, ma, nb, mb, p, lo, hi);
}

/* G80DispCommand: un metodo del canale principale, per registri. Mentre il
 * comando e' in corso il display puo' chiedere aiuto (il «supervisore»): al
 * secondo passo vuole l'orologio della testa. */
static void C(unsigned int metodo, unsigned int dato)
{
    unsigned int t0 = uptime_ms();

    TRACCIA("nv50:   C(0x%04x, 0x%08x)\n", metodo, dato);
    wr(0x610304, dato);
    wr(0x610300, metodo | 0x80010001);
    while (rd(0x610300) & 0x80000000) {
        unsigned int s = (rd(0x610024) >> 4) & 7, super = 0;

        if (s & 1) super = 1; else if (s & 2) super = 2; else if (s & 4) super = 3;
        if (super) {
            TRACCIA("nv50:   supervisore %u (0x610024 = 0x%08x)\n", super, rd(0x610024));
            if (super == 2 && (rd(0x614200) & 0xc0) == 0x80) imposta_pll();
            wr(0x610024, 8u << super);
            wr(0x610030, 0x80000000);
        }
        if (uptime_ms() - t0 > 2000) {
            printf("nv50: ! il comando 0x%04x non finisce (0x610300 = 0x%08x)\n",
                   metodo, rd(0x610300));
            g_scadute++;
            return;
        }
    }
}

/* G80DispPreInit: le capacita' di teste, DAC e SOR copiate dove il display
 * le guarda. «nouveau guys dont know what this does», dice il commento
 * originale della prima riga. */
static void preinit(void)
{
    unsigned int t;

    wr(0x610184, rd(0x614004));
    for (t = 0; t < 2; t++) {
        wr(0x610190 + t * 0x10, rd(0x616100 + t * 0x800));
        wr(0x610194 + t * 0x10, rd(0x616104 + t * 0x800));
        wr(0x610198 + t * 0x10, rd(0x616108 + t * 0x800));
        wr(0x61019C + t * 0x10, rd(0x61610C + t * 0x800));
    }
    wr(0x6101D0, rd(0x61A000));
    wr(0x6101D4, rd(0x61A800));
    wr(0x6101D8, rd(0x61B000));
    wr(0x6101E0, rd(0x61C000));
    wr(0x6101E4, rd(0x61C800));
    wr(0x6101E8, rd(0x61D000));
    wr(0x6101EC, rd(0x61D800));
    wr(0x61A004, 0x80550000); wr(0x61A010, 1);
    wr(0x61A804, 0x80550000); wr(0x61A810, 1);
    wr(0x61B004, 0x80550000); wr(0x61B010, 1);
}

/* G80DispInit: si spegne la VGA ereditata e si apre il canale principale in
 * modalita' «comandi per registri». */
static int prendi_display(void)
{
    unsigned int val, t0;

    if (rd(0x610024) & 0x100) {
        TRACCIA("nv50: spengo la VGA ereditata\n");
        wr(0x610024, 0x100);
        wr(0x6194E8, rd(0x6194E8) & ~1u);
        aspetta(0x6194E8, 2, 0, 1000, "la VGA si spegne");
    }

    TRACCIA("nv50: apro il canale del display\n");
    wr(0x610200, 0x2b00);
    t0 = uptime_ms();
    do {
        val = rd(0x610200);
        if ((val & 0x9f0000) == 0x20000) wr(0x610200, val | 0x800000);
        if ((val & 0x3f0000) == 0x30000) wr(0x610200, val | 0x200000);
        if (uptime_ms() - t0 > 2000) {
            printf("nv50: ! il canale non si ferma (0x610200 = 0x%08x)\n", val);
            return -1;
        }
    } while ((val & 0x1e0000) != 0);
    wr(0x610300, 1);
    wr(0x610200, 0x1000b03);
    if (aspetta(0x610200, 0x40000000, 0x40000000, 2000, "il canale si apre") != 0) return -1;

    C(0x0084, 0);
    C(0x0088, 0);
    C(0x0874, 0);
    C(0x0800, 0);
    C(0x0810, 0);
    C(0x082C, 0);
    return 0;
}

/* La tavolozza della testa: anche a 24 bit il colore passa da qui, e una
 * tavolozza a zero e' uno schermo nero col modo giusto. Identita', 257 voci da
 * quattro numeri a 16 bit (rosso, verde, blu, niente), valori a 14 bit. */
static void tavolozza(unsigned int dove)
{
    volatile unsigned short *l = (volatile unsigned short *)(g_vram + dove);
    unsigned int i;

    for (i = 0; i < 257; i++) {
        unsigned int c = i > 255 ? 255 : i;
        unsigned short v = (unsigned short)(((c << 8) | c) >> 2);

        l[i * 4 + 0] = v; l[i * 4 + 1] = v; l[i * 4 + 2] = v; l[i * 4 + 3] = 0;
    }
}

/* Il motivo: cornice bianca di un pixel sul bordo esatto (se manca un lato, la
 * misura e' sbagliata), otto barre di colore, una griglia ogni 100 pixel e le
 * due diagonali (se sono storte, il pitch e' sbagliato). */
static void motivo(void)
{
    static const unsigned int barre[8] = {
        0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000
    };
    unsigned int x, y;

    for (y = 0; y < FB_H; y++) {
        volatile unsigned int *r = (volatile unsigned int *)(g_vram + FB_OFFSET + y * FB_PITCH);

        for (x = 0; x < FB_W; x++) {
            unsigned int c = barre[x * 8 / FB_W];

            if (y > FB_H / 2) c = (x % 100 == 0 || y % 100 == 0) ? 0xFFFFFF : 0x202040;
            if (x == 0 || y == 0 || x == FB_W - 1 || y == FB_H - 1) c = 0xFFFFFF;
            if (x * FB_H / FB_W == y || (FB_W - 1 - x) * FB_H / FB_W == y) c = 0xFF8000;
            r[x] = c;
        }
    }
}

/* motivo_si: 1 = -prova (il motivo, la console resta dov'era), 0 = -nativo
 * (framebuffer nero, e la console del kernel ci passa sopra). */
static int prova(int motivo_si)
{
    unsigned int tmp = rd(0x10020C), lut, i;
    unsigned int tempi[4];
    MmioZona     m;

    /* g80_driver.c: la memoria video in KB, meno un mega che tiene il chip. */
    g_vram_kb = (tmp >> 10 | (tmp & 1) << 22);
    if (g_vram_kb <= 1024) { printf("nv50: memoria video sconosciuta (0x%08x)\n", tmp); return 1; }
    g_vram_kb -= 1024;
    if (g_vram_kb > 16384) g_vram_kb = 16384;
    TRACCIA("nv50: memoria video %u KB usabili (0x10020C = 0x%08x)\n", g_vram_kb, tmp);
    if (FB_OFFSET + FB_PITCH * FB_H > g_vram_kb * 1024 - 0x6000) {
        printf("nv50: il framebuffer di prova non ci sta\n");
        return 1;
    }

    /* I tempi del pannello, come li ha lasciati il BIOS (in uso). */
    if ((rd(0x610050) & 3) != 2) { TRACCIA("nv50: il pannello non e' sulla testa 0: mi fermo\n"); return 1; }
    g_pclk = rd(0x610AD4) & 0x3fffff;
    tempi[0] = rd(0x610AFC); tempi[1] = rd(0x610B04);
    tempi[2] = rd(0x610AEC); tempi[3] = rd(0x610AF4);
    TRACCIA("nv50: tempi del BIOS: %u kHz, 0x%08x 0x%08x 0x%08x 0x%08x, uscita 0x%08x\n",
           g_pclk, tempi[0], tempi[1], tempi[2], tempi[3], rd(0x610B4C));
    if (g_pclk == 0 || rd(0x610B4C) != (FB_H << 16 | FB_W)) {
        TRACCIA("nv50: il pannello non e' a %ux%u: mi fermo\n", FB_W, FB_H);
        return 1;
    }

    m.fisico = g_bar1;
    m.byte   = g_vram_kb * 1024;
    if (mmio_map(&m) != 0) { printf("nv50: mmio_map della memoria video rifiutata\n"); return 1; }
    g_vram = (volatile unsigned char *)(unsigned long)m.virt;

    lut = g_vram_kb * 1024 - 0x5000;          /* testa 0 */
    TRACCIA("nv50: framebuffer a 0x%x, tavolozza a 0x%x\n", FB_OFFSET, lut);
    if (motivo_si) motivo();
    else {
        unsigned int y;

        for (y = 0; y < FB_H; y++) {
            volatile unsigned int *r = (volatile unsigned int *)(g_vram + FB_OFFSET + y * FB_PITCH);
            unsigned int x;

            for (x = 0; x < FB_PITCH / 4; x++) r[x] = 0;
        }
    }
    tavolozza(lut);

    /* ! -nativo: PRIMA SI CHIEDE AL KERNEL, POI SI TOCCA LA SCHEDA. Se il
     * kernel dice di no (la scrivania tiene la grafica: -EBUSY) ci si ferma
     * qui, e lo schermo e' ancora quello del BIOS. Nell'ordine inverso il
     * pannello mostrerebbe un framebuffer in cui nessuno disegna. Nel mezzo,
     * per un istante, la console scrive in un framebuffer che il pannello
     * non mostra ancora: si vede quando la scheda ci passa. */
    if (!motivo_si) {
        VideoCambio v;

        v.fisico = g_bar1 + FB_OFFSET;
        v.larghezza = FB_W; v.altezza = FB_H; v.passo = FB_PITCH; v.bit = 32;
        if (video_cambia(&v) != 0) {
            printf("nv50: il kernel non passa al framebuffer nuovo (errno %d%s):\n"
                   "      non tocco la scheda.\n", errno,
                   errno == 16 ? ", la scrivania tiene la grafica: chiudila" : "");
            return 1;
        }
        TRACCIA("nv50: la console del kernel e' passata a %ux%u\n", FB_W, FB_H);
    }

    preinit();
    if (prendi_display() != 0) return 1;

    /* Prepare: le uscite che non si usano, scollegate (tre DAC e il SOR1). */
    for (i = 0; i < 3; i++) C(0x0400 + i * 0x80, 0);
    C(0x0640, 0);

    /* CrtcModeSet, testa 0, coi tempi del BIOS (gia' «fixed»: skipModeFixup) */
    C(0x0804, g_pclk | 0x800000);
    C(0x0808, 0);
    C(0x0810, 0);
    C(0x082C, 0);
    C(0x0814, tempi[0]);
    C(0x0818, tempi[1]);
    C(0x081C, tempi[2]);
    C(0x0820, tempi[3]);
    C(0x0868, FB_H << 16 | FB_W);
    C(0x086C, FB_PITCH | 0x100000);
    C(0x0870, 0xCF00);
    C(0x08A0, 0x11);                            /* dither, come il BIOS */
    C(0x08A8, 0x40000);
    C(0x08C0, 0);
    C(0x08C8, FB_H << 16 | FB_W);
    C(0x08D4, 0);

    /* BlankScreen(FALSE): il framebuffer e il contesto DMA che lo contiene */
    C(0x0860, FB_OFFSET >> 8);
    C(0x0864, 0);
    wr(0x610380, 0);
    wr(0x610384, g_vram_kb * 1024 - 1);
    wr(0x610388, 0x150000);
    wr(0x61038C, 0);
    C(0x0884, (g_vram_kb << 2) - 0x40);
    C(0x089C, 1);
    C(0x0880, 0x05000000);                      /* cursore spento */
    C(0x0840, 0xC0000000);
    C(0x0844, lut >> 8);
    C(0x085C, 1);
    C(0x0874, 1);

    /* SorModeSet: SOR0 acceso, sulla testa 0, LVDS; scala spenta, 1:1 */
    aspetta(0x61C004, 0x80000000, 0, 1000, "SOR0 libero");
    wr(0x61C004, rd(0x61C004) | 0x80000001);
    aspetta(0x61C030, 0x10000000, 0, 1000, "SOR0 acceso");
    C(0x0600, 1);
    C(0x08A4, 0);
    C(0x08D8, FB_H << 16 | FB_W);
    C(0x08DC, FB_H << 16 | FB_W);

    /* Commit: la testa 1 spenta, poi si applica tutto */
    C(0x0C40, 0); C(0x0C44, 0); C(0x0C5C, 0); C(0x0C74, 0); C(0x0C9C, 0);
    C(0x0080, 0);

    if (motivo_si) {
        printf("\nnv50: fatto%s. Sul pannello: cornice bianca intera, barre di\n",
               g_scadute ? " (con attese scadute, vedi sopra)" : "");
        printf("      colore in alto, griglia in basso, due diagonali arancio.\n");
        printf("      Lo schermo di prima torna solo riavviando.\n");
    } else
        printf("\nnv50: pannello a %ux%u nativi%s.\n", FB_W, FB_H,
               g_scadute ? " (con attese scadute, vedi sopra)" : "");
    return g_scadute ? 2 : 0;
}

static void uso(void)
{
    printf("uso: nv50.drv -stato      il display com'e' adesso: teste, pannello,\n");
    printf("                          framebuffer, scala (sola lettura)\n");
    printf("     nv50.drv -tutto      come -stato, con tutti i metodi delle teste\n");
    printf("     nv50.drv -prova      prende il display e accende il pannello a\n");
    printf("                          %ux%u con un motivo di prova. Lo schermo di\n", FB_W, FB_H);
    printf("                          prima torna solo riavviando.\n");
    printf("     nv50.drv -nativo     come -prova, ma la console del kernel passa al\n");
    printf("                          pannello nativo (prima della scrivania)\n");
    printf("     nv50.drv -install    -nativo a ogni accensione: scrive il blocco in\n");
    printf("                          /boot/avvio.sh (-remove lo toglie)\n");
    printf("     nv50.drv -avvio      -nativo zitto, quello di /boot/avvio.sh: su una\n");
    printf("                          macchina senza questa scheda esce e basta\n");
}

/* =============================================================================
 * -install / -remove: il blocco in /boot/avvio.sh (avvio_blocco, libc)
 *
 * ! NON E' svga.drv E NON LO SOSTITUISCE. svga.drv sceglie il modo VESA che
 * Stage 2 accende col BIOS; questo driver arriva DOPO, a sistema avviato, e
 * porta il pannello al nativo partendo da quel modo. E' un modulo in piu',
 * come sis.drv: dove la scheda non c'e' esce, e resta il modo di svga.drv.
 *
 * ! IN PRIMO PIANO, SENZA '&': avvio.sh lo aspetta, e il prompt e la
 * scrivania arrivano dopo, gia' sul framebuffer nuovo. Con la scrivania
 * accesa il kernel direbbe di no (EBUSY). Il servizio PCI serve per trovare
 * la scheda: se avvio.sh non lo accende gia' (la catena della rete), lo si
 * accende nel blocco.
 * ============================================================================= */
static int installa(int togli)
{
    char righe[512];
    int  rc = 0;

    if (getuid() != 0) { printf("nv50: -install e -remove solo da root\n"); return 1; }
    if (togli) rc = avvio_blocco("nv50.drv", NULL);
    else {
        int pci;

        /* ! IL BLOCCO VECCHIO SI TOGLIE PRIMA DI GUARDARE: il pci.drv che ci
         * sta dentro e' il nostro, e non dice che la rete lo accende gia'. */
        if (avvio_blocco("nv50.drv", NULL) < 0) rc = -1;
        /* ! CON E SENZA /dev/: hwconfig scrive `pci.drv &`, gli avvio.sh piu'
         * vecchi `/dev/pci.drv &` (il portatile). */
        pci = avvio_ha_riga("pci.drv") || avvio_ha_riga("/dev/pci.drv");
        snprintf(righe, sizeof(righe),
                 "# Il pannello a %ux%u nativi, prima del prompt e della scrivania.\n"
                 "# Esce zitto dove la scheda non e' una NVIDIA G80 con questo\n"
                 "# pannello. `nv50.drv -remove` toglie questo blocco.\n"
                 "%s"
                 "nv50.drv -avvio\n", FB_W, FB_H, pci ? "" : "pci.drv &\n");
        if (rc == 0) rc = avvio_blocco("nv50.drv", righe);
    }
    if (rc < 0) { printf("nv50: non riesco a scrivere /boot/avvio.sh (errno %d)\n", errno); return 1; }
    if (togli)
        printf(rc ? "nv50: tolto da /boot/avvio.sh: al prossimo avvio resta il modo di svga.drv\n"
                  : "nv50: in /boot/avvio.sh non c'era\n");
    else
        printf("nv50: in /boot/avvio.sh: al prossimo avvio il pannello va a %ux%u\n", FB_W, FB_H);
    return 0;
}

int main(int argc, char **argv)
{
    PciDispositivo d;
    int tutto, prova_si;

    if (argc >= 2 && strcmp(argv[1], "-install") == 0) return installa(0);
    if (argc >= 2 && strcmp(argv[1], "-remove") == 0)  return installa(1);
    if (argc >= 2 && strcmp(argv[1], "-avvio") == 0) { g_zitto = 1; argv[1] = "-nativo"; }
    if (argc < 2 || (strcmp(argv[1], "-stato") != 0 && strcmp(argv[1], "-tutto") != 0 &&
                     strcmp(argv[1], "-prova") != 0 && strcmp(argv[1], "-nativo") != 0)) {
        uso();
        return argc < 2 ? 1 : (strcmp(argv[1], "-h") == 0 ? 0 : 1);
    }
    tutto    = strcmp(argv[1], "-tutto") == 0;
    prova_si = strcmp(argv[1], "-prova") == 0 ? 1 : strcmp(argv[1], "-nativo") == 0 ? 2 : 0;

    if (trova_scheda(&d) != 0) return g_zitto ? 0 : 1;
    g_bar0 = d.bar[0];
    g_bar1 = d.bar[1];
    TRACCIA("Scheda: %04x:%04x in %02x:%02x.%x, registri a 0x%08x, memoria video a 0x%08x\n",
           d.venditore, d.dispositivo, d.bus, d.slot, d.funzione, d.bar[0], d.bar[1]);
    if (mappa() != 0) return 1;
    if (prova_si) {
        if (getuid() != 0) { printf("nv50: %s solo da root\n", argv[1]); return 1; }
        return prova(prova_si == 1);
    }
    return stato(tutto);
}

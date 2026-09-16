/* =============================================================================
 * drivers/sis/sis_2d.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il motore 2D della SiS 6330: riempire e copiare senza la CPU
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' ESISTE, E NON E' «PER ANDARE PIU' VELOCE»
 *
 * Il 16 settembre 2026 bin/fbprova ha misurato questa macchina, e i numeri
 * dicono una cosa precisa (800x600x32, 1.920.000 byte a fotogramma):
 *
 *     riempi MMX (8 byte)           375 MB/s    4,9 ms a fotogramma
 *     copia RAM -> schermo          375 MB/s    4,9 ms
 *     LETTURA dallo schermo          82 MB/s   22,2 ms
 *
 * Scrivere nello schermo e' gia' al limite del bus: il motore 2D li' non fa
 * guadagnare velocita', fa guadagnare la CPU, che resta libera per il resto
 * del sistema. Il numero che fa la differenza e' il TERZO.
 *
 * ! LEGGERE DALLO SCHERMO COSTA QUATTRO VOLTE E MEZZO SCRIVERCI. Una copia
 * schermo -> schermo fatta dalla CPU paga quella lettura: spostare una
 * finestra o far scorrere un terminale vuol dire leggere i pixel e riscriverli,
 * cioe' 22 ms solo per la meta' che nessuno vede. Il motore 2D quella copia la
 * fa DENTRO la scheda, senza far attraversare il bus ai pixel nemmeno una
 * volta. E' li' che sta il guadagno vero, ed e' l'unico che la CPU non puo'
 * prendersi in nessun modo.
 *
 * -----------------------------------------------------------------------------
 * ! DA DOVE VIENE LA MAPPA DEI REGISTRI, E PERCHE' NON E' INDOVINATA
 *
 * Per il cambio di modalita' (sis.c) i valori sono stati LETTI da una macchina
 * vera con sonda.drv, perche' non esiste documentazione pubblica. Qui no: il
 * motore 2D di questa famiglia e' descritto da due sorgenti aperte che lo
 * pilotano da vent'anni — il driver sisfb del kernel Linux
 * (drivers/video/fbdev/sis/, file sis_accel.h e sis_accel.c) e il driver X
 * xf86-video-sis. Sono GPL, come EX-OS, e questo file segue la loro mappa:
 * gli offset, l'ordine delle scritture e il registro di stato sono i loro.
 *
 * ! E LA FAMIGLIA E' QUELLA GIUSTA: il dispositivo PCI e' 1039:6330, cioe'
 * l'integrata dei chipset 661/741/760/761, il cui nucleo grafico discende
 * dalla serie 315. In sisfb e' il ramo SIS_315_VGA, e sono le macro SiS310*
 * — non le SiS300*, che hanno un altro set di registri. Sbagliare ramo qui
 * vuol dire scrivere numeri sensati negli indirizzi sbagliati.
 *
 * -----------------------------------------------------------------------------
 * ! COME SI LAVORA SU UN MOTORE CHE PUO' NON RISPONDERE
 *
 * Tre regole, e tutte e tre vengono dal fatto che questo codice gira su una
 * macchina che sta dall'altra parte di un cavo:
 *
 *   1. SI GUARDA PRIMA DI SCRIVERE. `-2dstato` legge e basta. Se il motore e'
 *      gia' acceso e fermo, il registro di stato lo dice, e si sa che c'e'
 *      qualcuno dall'altra parte prima di ordinargli qualcosa.
 *
 *   2. L'ATTESA HA UN FONDO. Il ciclo che aspetta il motore in sisfb e' un
 *      `while` senza uscita: su un motore che non risponde e' un processo
 *      piantato per sempre, e da telnet sembra la macchina morta. Qui il
 *      giro e' contato e, se scade, si dice e si torna indietro.
 *
 *   3. IL RISULTATO SI RILEGGE DAL FRAMEBUFFER. «Si guarda lo schermo» non e'
 *      una prova: dall'altra parte del cavo non si guarda niente, e comunque
 *      un rettangolo del colore giusto nel posto sbagliato sembra giusto.
 *      sis2d_prova() disegna e poi VA A LEGGERE i pixel che devono essere
 *      cambiati e quelli che NON devono esserlo.
 *
 * -----------------------------------------------------------------------------
 * ! GLI INDIRIZZI DEL MOTORE SONO OFFSET DENTRO LA MEMORIA VIDEO, non indirizzi
 * fisici. SRC_ADDR e DST_ADDR si contano dall'inizio della memoria della
 * scheda, cioe' dal BAR0; il framebuffer visibile puo' cominciare piu' avanti.
 * Il conto lo fa sis2d_apri() una volta sola, sottraendo il BAR0 all'indirizzo
 * fisico che video_info() dichiara. Darglielo fisico vorrebbe dire scrivere
 * fuori dallo schermo — o fuori dalla memoria della scheda.
 * ============================================================================= */

#include "libc.h"
#include "sis_2d.h"

#define SIS_VENDITORE 0x1039
#define SIS_6330      0x6330
#define PCI_INDIRIZZO 0xCF8
#define PCI_DATO      0xCFC

/* Quanta finestra di registri si mappa. I registri del motore stanno fra
 * 0x8200 e 0x8240, la coda di comandi a 0x85C0: 64 KB coprono tutto con
 * abbondanza, e la scheda ne decodifica di piu'. */
#define MMIO_BYTE     0x10000u

/* -----------------------------------------------------------------------------
 * I registri, dalla mappa di sisfb (sis_accel.h, ramo SIS_315_VGA)
 * --------------------------------------------------------------------------- */
#define SRC_ADDR        0x8200      /* 32: offset della sorgente in VRAM   */
#define SRC_PITCH       0x8204      /* 16: passo della sorgente, in byte   */
#define AGP_BASE        0x8206      /* 16: ...e qui ci va la PROFONDITA'   */
#define SRC_Y           0x8208      /* 32: x<<16 | y della sorgente        */
#define DST_Y           0x820C      /* 32: x<<16 | y della destinazione    */
#define DST_ADDR        0x8210      /* 32: offset della destinazione       */
#define DST_PITCH       0x8214      /* 32: altezza<<16 | passo in byte     */
#define RECT_WIDTH      0x8218      /* 32: altezza<<16 | larghezza, pixel  */
#define PAT_FGCOLOR     0x821C      /* 32: il colore del riempimento       */
#define LEFT_CLIP       0x8234      /* 32: alto<<16 | sinistra             */
#define RIGHT_CLIP      0x8238      /* 32: basso<<16 | destra              */
#define COMMAND_READY   0x823C      /* 32: il comando                      */
#define FIRE_TRIGGER    0x8240      /* 32: e questo lo fa partire          */
#define Q_STATUS        0x85CC      /* 32: bit 31 acceso = motore fermo    */

/* I pezzi del registro di comando */
#define CMD_BITBLT      0x00000000u
#define CMD_SRCVIDEO    0x00000000u   /* la sorgente e' la memoria video  */
#define CMD_PATFG       0x00000000u   /* il motivo e' il colore PAT_FGCOLOR */
#define CMD_CLIPENABLE  0x00040000u

/* La profondita', che entra in due posti diversi e con due codifiche diverse:
 * in AGP_BASE come 0xC000, e nel comando come 0x00020000. Non sono la stessa
 * cosa scritta due volte — sono due campi del motore — ed e' esattamente cosi'
 * in sisfb (sisfb_set_vparms piu' SiS310SetupDSTColorDepth). */
#define DSTCOL_8        0x0000u
#define DSTCOL_16       0x8000u
#define DSTCOL_32       0xC000u
#define DEPTH_8         0x00000000u
#define DEPTH_16        0x00010000u
#define DEPTH_32        0x00020000u

/* Le due ROP ternarie che servono, e sono quelle di sempre: PATCOPY mette il
 * motivo, SRCCOPY mette la sorgente. Nelle tavole di sisfb sono
 * sisPatALUConv[GXcopy] e sisALUConv[GXcopy]. */
#define ROP_PATCOPY     0xF0u
#define ROP_SRCCOPY     0xCCu

/* ! IL FONDO DELL'ATTESA. Un fotogramma intero a 800x600 il motore lo fa in
 * qualche millisecondo; questo giro, su una macchina del 2004, sono molti di
 * piu'. Non e' una misura del tempo: e' la garanzia che un motore muto non
 * tenga il processo per sempre. */
#define ATTESA_MAX      20000000u


/* -----------------------------------------------------------------------------
 * Lo stato, trovato una volta sola all'apertura
 * --------------------------------------------------------------------------- */
static volatile unsigned char *g_mmio  = 0;
static unsigned char          *g_fb    = 0;
static unsigned int            g_vram  = 0;   /* BAR0: inizio della VRAM    */
static unsigned int            g_off   = 0;   /* dove comincia lo schermo   */
static unsigned int            g_passo = 0;   /* byte per riga              */
static unsigned int            g_larg  = 0;
static unsigned int            g_alt   = 0;
static unsigned int            g_bit   = 0;
static unsigned int            g_dstcol = 0;
static unsigned int            g_depth  = 0;
static unsigned int            g_scaduti = 0;  /* quante attese sono scadute */


/* ! volatile SU OGNI ACCESSO, e la ragione sta in libc.h sopra mmio_map: un
 * registro cambia senza che nessuno scriva, e senza volatile il compilatore
 * tiene il valore in un registro della CPU e un ciclo che aspetta un bit non
 * finisce mai. */
static void mm32(unsigned int off, unsigned int val)
{
    *(volatile unsigned int *)(g_mmio + off) = val;
}

static unsigned int mm32r(unsigned int off)
{
    return *(volatile unsigned int *)(g_mmio + off);
}

static void mm16(unsigned int off, unsigned int val)
{
    *(volatile unsigned short *)(g_mmio + off) = (unsigned short)val;
}


/* -----------------------------------------------------------------------------
 * Aspettare che il motore abbia finito
 *
 * ! SI GUARDA IL BIT 31 DI Q_STATUS. sisfb lo legge a 16 bit dalla meta' alta
 * (Q_STATUS+2, maschera 0x8000) e lo fa QUATTRO VOLTE DI FILA: non e' una
 * superstizione, e' che quel bit puo' lampeggiare mentre il motore svuota la
 * coda, e una lettura sola puo' cascare nell'istante sbagliato. Qui si fa la
 * stessa cosa contando i giri.
 * --------------------------------------------------------------------------- */
static int attendi(void)
{
    unsigned int giri = 0, buone = 0;

    while (giri < ATTESA_MAX) {
        if (mm32r(Q_STATUS) & 0x80000000u) {
            if (++buone >= 4) return 0;
        } else {
            buone = 0;
        }
        giri++;
    }

    g_scaduti++;
    return -1;
}


/* -----------------------------------------------------------------------------
 * Trovare la scheda e i suoi due BAR
 *
 * ! IL BAR DEI REGISTRI SI RICONOSCE PER ESCLUSIONE, non per numero d'ordine.
 * La scheda ha due finestre di memoria: una e' la memoria video, e quella la
 * conosciamo gia' — video_info() dice a che indirizzo fisico sta il
 * framebuffer. L'altra e' quella dei registri. Prendere «il BAR numero 1»
 * avrebbe funzionato su questo portatile e su nessuna garanzia.
 *
 * ! E «QUALE DELLE DUE CONTIENE IL FRAMEBUFFER» NON SI DECIDE INDOVINANDO LA
 * MISURA DELLA FINESTRA. La prima stesura diceva «sta fra bar e bar+128 MB», e
 * su questa macchina sbagliava: i registri stanno a 0xE2100000, il framebuffer
 * a 0xE8000000, e 0xE8000000 cade dentro i 128 MB dopo 0xE2100000. Il driver
 * prendeva i registri per la memoria video e si fermava dicendo «non trovo i
 * due BAR» — che era vero e inutile insieme.
 *
 * La regola che regge non ha bisogno di sapere quanto e' larga una finestra:
 * fra i BAR di memoria, quello della memoria video e' il PIU' ALTO CHE NON
 * SUPERA l'indirizzo del framebuffer. Una finestra comincia dove comincia, e
 * fra due che stanno tutt'e due sotto vince la piu' vicina. L'altro e' quello
 * dei registri.
 *
 * ! E I BAR A 64 BIT OCCUPANO DUE CASELLE. I bit 2-1 valgono 2 quando
 * l'indirizzo e' a 64 bit: la casella dopo e' la meta' alta, non un altro BAR.
 * Leggerla come BAR darebbe zero (su una macchina a 32 bit) e non farebbe
 * danno, ma il conto delle finestre verrebbe sbagliato il giorno in cui una
 * scheda li usa.
 * --------------------------------------------------------------------------- */
static unsigned int cfg_leggi(unsigned int bus, unsigned int slot,
                              unsigned int fn, unsigned int off)
{
    unsigned int ind = 0x80000000u | (bus << 16) | (slot << 11) |
                       (fn << 8) | (off & 0xFC);
    unsigned int val;

    if (ioport_out32(PCI_INDIRIZZO, ind) != 0) return 0xFFFFFFFFu;
    if (ioport_in32(PCI_DATO, &val) != 0)      return 0xFFFFFFFFu;
    return val;
}

static void cfg_scrivi(unsigned int bus, unsigned int slot,
                       unsigned int fn, unsigned int off, unsigned int val)
{
    unsigned int ind = 0x80000000u | (bus << 16) | (slot << 11) |
                       (fn << 8) | (off & 0xFC);

    if (ioport_out32(PCI_INDIRIZZO, ind) != 0) return;
    ioport_out32(PCI_DATO, val);
}

static int trova_bar(unsigned int fisico_fb, unsigned int *bar_fb,
                     unsigned int *bar_reg, int verboso)
{
    unsigned int bus, slot, fn, v, bar, i;
    unsigned int mem[6];
    int n = 0, k, i_fb;

    *bar_fb = 0;
    *bar_reg = 0;

    for (bus = 0; bus < 8; bus++)
        for (slot = 0; slot < 32; slot++)
            for (fn = 0; fn < 8; fn++) {
                v = cfg_leggi(bus, slot, fn, 0x00);
                if ((v & 0xFFFF) != SIS_VENDITORE) continue;
                if (((v >> 16) & 0xFFFF) != SIS_6330) continue;

                for (i = 0; i < 6; i++) {
                    bar = cfg_leggi(bus, slot, fn, 0x10 + i * 4);
                    if (bar == 0xFFFFFFFFu || bar == 0) continue;
                    if (bar & 1) continue;              /* e' una porta I/O */
                    if (((bar >> 1) & 3u) == 2u) i++;   /* a 64 bit: salta la
                                                           meta' alta */
                    mem[n++] = bar & 0xFFFFFFF0u;
                }

                if (verboso) {
                    printf("sis2d: 1039:6330 a %02x:%02x.%x, %d finestre di "
                           "memoria:", bus, slot, fn, n);
                    for (k = 0; k < n; k++) printf(" 0x%08x", mem[k]);
                    printf("\n       il framebuffer sta a 0x%08x\n", fisico_fb);
                }

                if (n < 2) return -1;

                /* La memoria video: la finestra piu' alta che non supera il
                 * framebuffer. */
                i_fb = -1;
                for (k = 0; k < n; k++) {
                    if (mem[k] > fisico_fb) continue;
                    if (i_fb < 0 || mem[k] > mem[i_fb]) i_fb = k;
                }
                if (i_fb < 0) return -1;
                *bar_fb = mem[i_fb];

                /* I registri: la prima delle altre. */
                for (k = 0; k < n; k++)
                    if (k != i_fb) { *bar_reg = mem[k]; break; }

                return (*bar_fb && *bar_reg) ? 0 : -1;
            }

    if (verboso) printf("sis2d: sul bus PCI non c'e' nessuna 1039:6330.\n");
    return -1;
}


/* -----------------------------------------------------------------------------
 * LA DIAGNOSI, quando la finestra dei registri non risponde
 *
 * ! TUTTI UNO NON VUOL DIRE NIENTE DA SOLO. Un 0xFFFFFFFF su una finestra PCI
 * ha tre spiegazioni diverse, e si curano in tre posti diversi:
 *
 *   1. la mappatura non punta dove crediamo   -> si cura in sis2d_apri
 *   2. il ponte PCI non inoltra quell'intervallo -> si cura nel BIOS, o
 *                                                 rinunciando
 *   3. il chip non decodifica quella finestra finche' qualcosa non e' acceso
 *                                              -> si cura scrivendo quel bit
 *
 * Questa funzione le separa, e lo fa senza scrivere niente di irreversibile:
 *
 *   - mappa la finestra della MEMORIA VIDEO con la stessa mmio_map, e confronta
 *     quel che legge con quel che fb_map() mostra negli stessi byte. Se
 *     combaciano, mmio_map funziona e l'ipotesi 1 e' chiusa.
 *   - legge le finestre del ponte PCI a monte: se l'intervallo dei registri
 *     non ci sta dentro, e' l'ipotesi 2, e nessun bit la risolve.
 *   - sblocca i registri estesi (SR05 = 0x86, la stessa cosa che fa sis.c da
 *     sempre) e rilegge. E' l'unica scrittura, vale per l'ipotesi 3, e non
 *     cambia niente di quel che si vede sullo schermo.
 * --------------------------------------------------------------------------- */
#define SEQ_IDX 0x3C4
#define SEQ_DAT 0x3C5

static void ponti_pci(unsigned int reg)
{
    unsigned int bus, slot, fn, v, cl, b, l, pb, pl;
    int trovato = 0;

    for (bus = 0; bus < 4; bus++)
        for (slot = 0; slot < 32; slot++)
            for (fn = 0; fn < 8; fn++) {
                v = cfg_leggi(bus, slot, fn, 0x00);
                if ((v & 0xFFFF) == 0xFFFF) continue;
                cl = cfg_leggi(bus, slot, fn, 0x08) >> 16;   /* classe/sottocl */
                if ((cl & 0xFF00) != 0x0600) continue;
                if ((cl & 0x00FF) != 0x04) continue;         /* ponte PCI-PCI */

                trovato = 1;
                v  = cfg_leggi(bus, slot, fn, 0x20);         /* mem base/limit */
                b  = (v & 0xFFF0u) << 16;
                l  = ((v >> 16) & 0xFFF0u) << 16 | 0xFFFFFu;
                v  = cfg_leggi(bus, slot, fn, 0x24);         /* prefetch       */
                pb = (v & 0xFFF0u) << 16;
                pl = ((v >> 16) & 0xFFF0u) << 16 | 0xFFFFFu;

                printf("       ponte %02x:%02x.%x  memoria 0x%08x-0x%08x  "
                       "prefetch 0x%08x-0x%08x\n", bus, slot, fn, b, l, pb, pl);

                if (reg >= b && reg <= l)
                    printf("         i registri (0x%08x) CI STANNO dentro la "
                           "finestra di memoria.\n", reg);
                else if (reg >= pb && reg <= pl)
                    printf("         i registri stanno nella finestra "
                           "PREFETCHABLE (strano ma inoltrata).\n");
                else
                    printf("         ! i registri (0x%08x) NON stanno in "
                           "nessuna delle due: il ponte non li inoltra.\n", reg);
            }

    if (!trovato) printf("       nessun ponte PCI-PCI: la scheda e' sul bus 0.\n");
}

void sis2d_diagnosi(void)
{
    VideoInfo v;
    MmioZona  m;
    unsigned int bar_fb, bar_reg, i, vecchio;
    volatile unsigned int *a;
    unsigned int *b;

    if (video_info(&v) != 0 || v.fisico == 0) {
        printf("sis2d: lo schermo e' in modo testo: non c'e' niente da "
               "diagnosticare.\n");
        return;
    }
    if (ioport_bind(PCI_INDIRIZZO, 8) != 0) {
        printf("sis2d: ioport_bind sul PCI rifiutata.\n");
        return;
    }
    if (trova_bar(v.fisico, &bar_fb, &bar_reg, 1) != 0) return;

    printf("\n  -- 1. mmio_map funziona? si mappa la MEMORIA VIDEO e si "
           "confronta --\n");
    m.fisico = bar_fb;
    m.byte   = 0x1000;
    if (mmio_map(&m) != 0) {
        printf("       mmio_map della memoria video rifiutata (%s):\n"
               "       allora il problema e' la syscall, non la scheda.\n",
               strerror(errno));
        return;
    }
    a = (volatile unsigned int *)m.virt;
    b = (unsigned int *)fb_map();
    if (b == 0) {
        printf("       fb_map rifiutata: non posso confrontare.\n");
        return;
    }
    for (i = 0; i < 4; i++)
        printf("       [%u]  mmio_map 0x%08x   fb_map 0x%08x   %s\n",
               i, a[i], b[i], (a[i] == b[i]) ? "uguali" : "DIVERSI");
    printf("       se sono uguali, mmio_map fa il suo mestiere e il problema\n"
           "       e' la finestra dei registri, non la mappatura.\n");

    printf("\n  -- 2. il ponte PCI inoltra 0x%08x? --\n", bar_reg);
    ponti_pci(bar_reg);

    printf("\n  -- 3. la finestra dei registri --\n");
    m.fisico = bar_reg;
    m.byte   = MMIO_BYTE;
    if (mmio_map(&m) != 0) {
        printf("       mmio_map dei registri rifiutata (%s)\n", strerror(errno));
        return;
    }
    a = (volatile unsigned int *)m.virt;
    printf("       mappata a %p\n", (void *)m.virt);
    printf("       [0x0000] %08x\n", a[0]);
    printf("       [0x1000] %08x\n", a[0x1000 / 4]);
    printf("       [0x8200] %08x\n", a[0x8200 / 4]);
    printf("       [0x85cc] %08x\n", a[0x85CC / 4]);

    printf("\n  -- 4. i registri di sequenza, e il comando PCI --\n");
    if (ioport_bind(0x3B0, 0x30) != 0) {
        printf("       ioport_bind sulle porte VGA rifiutata.\n");
        return;
    }
    printf("       porte VGA prese\n");

    ioport_out(SEQ_IDX, 0x05);
    vecchio = (unsigned int)ioport_in(SEQ_DAT);
    printf("       SR05 = 0x%02x (0xa1 = estesi sbloccati)\n", vecchio);
    if (vecchio != 0xA1u) {
        ioport_out(SEQ_IDX, 0x05);
        ioport_out(SEQ_DAT, 0x86);
        printf("       sbloccati adesso\n");
    }

    ioport_out(SEQ_IDX, 0x1E);
    printf("       SR1E = 0x%02x\n", (unsigned int)ioport_in(SEQ_DAT));
    ioport_out(SEQ_IDX, 0x20);
    printf("       SR20 = 0x%02x\n", (unsigned int)ioport_in(SEQ_DAT));
    ioport_out(SEQ_IDX, 0x21);
    printf("       SR21 = 0x%02x\n", (unsigned int)ioport_in(SEQ_DAT));
    ioport_out(SEQ_IDX, 0x26);
    printf("       SR26 = 0x%02x\n", (unsigned int)ioport_in(SEQ_DAT));
    ioport_out(SEQ_IDX, 0x27);
    printf("       SR27 = 0x%02x\n", (unsigned int)ioport_in(SEQ_DAT));

    /* I BAR GREZZI, con i bit di tipo: e' l'unico modo di vedere se una
     * finestra e' prefetchable o a 64 bit, cose che la maschera nasconde. */
    {
        unsigned int bus, slot, fn, w, trovata = 0;

        for (bus = 0; bus < 8 && !trovata; bus++)
            for (slot = 0; slot < 32 && !trovata; slot++)
                for (fn = 0; fn < 8 && !trovata; fn++) {
                    w = cfg_leggi(bus, slot, fn, 0x00);
                    if ((w & 0xFFFF) != SIS_VENDITORE) continue;
                    if (((w >> 16) & 0xFFFF) != SIS_6330) continue;
                    trovata = 1;

                    w = cfg_leggi(bus, slot, fn, 0x04);
                    printf("       comando PCI = 0x%04x  (bit1 = memoria "
                           "decodificata)\n", w & 0xFFFFu);
                    for (i = 0; i < 6; i++)
                        printf("       BAR%u grezzo = 0x%08x\n", i,
                               cfg_leggi(bus, slot, fn, 0x10 + i * 4));
                }
    }

    /* -------------------------------------------------------------------
     * 5. L'UNICA SCRITTURA CHE CONTA, E PERCHE' SI PUO' FARE
     *
     * SR26 e' il registro della coda di comandi (IND_SIS_CMDQUEUE_SET in
     * sisfb) e su questa macchina vale 0x00: nessun modo di coda acceso. Il
     * sospetto e' che sia per questo che la finestra dei registri non
     * risponde — un motore che non ha un modo di lavoro puo' benissimo non
     * decodificare il proprio blocco.
     *
     * ! ACCENDERE IL SOLO BIT MMIO NON FA PARTIRE NIENTE. SIS_MMIO_CMD_ENABLE
     * dice COME si danno i comandi, non ne da' nessuno: il motore si muove
     * quando si tocca FIRE_TRIGGER, e qui non lo si tocca. Percio' e' una
     * scrittura che si puo' fare prima di sapere se serve.
     *
     * ! E SI RIMETTE COM'ERA SE NON SERVE. Lasciare acceso un modo di coda
     * senza averne programmato la base vorrebbe dire lasciare la macchina in
     * uno stato che nessuno ha voluto, e scoprirlo un'ora dopo.
     * ------------------------------------------------------------------- */
    printf("\n  -- 5. e se si accende il modo MMIO della coda comandi? --\n");
    {
        unsigned int sr26_prima, sr27_prima;

        ioport_out(SEQ_IDX, 0x26);
        sr26_prima = (unsigned int)ioport_in(SEQ_DAT);
        ioport_out(SEQ_IDX, 0x27);
        sr27_prima = (unsigned int)ioport_in(SEQ_DAT);

        ioport_out(SEQ_IDX, 0x27);
        ioport_out(SEQ_DAT, 0x1F);          /* COMMAND_QUEUE_THRESHOLD */
        ioport_out(SEQ_IDX, 0x26);
        ioport_out(SEQ_DAT, 0x20);          /* SIS_MMIO_CMD_ENABLE, 512k */

        ioport_out(SEQ_IDX, 0x26);
        printf("       SR26: 0x%02x -> 0x%02x\n", sr26_prima,
               (unsigned int)ioport_in(SEQ_DAT));
        printf("       [0x8200] %08x\n", a[0x8200 / 4]);
        printf("       [0x85cc] %08x\n", a[0x85CC / 4]);

        /* La prova che distingue «finestra morta» da «registri che valgono
         * davvero tutti uno»: si SCRIVE un valore in un registro di dato e
         * lo si rilegge. PAT_FGCOLOR e' un colore, non un comando: metterci
         * un numero non fa muovere niente. */
        a[0x821C / 4] = 0x12345678u;
        printf("       scritto 0x12345678 in PAT_FGCOLOR, rileggo %08x\n",
               a[0x821C / 4]);
        a[0x821C / 4] = 0xA5A5A5A5u;
        printf("       scritto 0xa5a5a5a5 in PAT_FGCOLOR, rileggo %08x\n",
               a[0x821C / 4]);

        if ((a[0x821C / 4] != 0xA5A5A5A5u)) {
            ioport_out(SEQ_IDX, 0x26);
            ioport_out(SEQ_DAT, (unsigned int)sr26_prima);
            ioport_out(SEQ_IDX, 0x27);
            ioport_out(SEQ_DAT, (unsigned int)sr27_prima);
            printf("       non ha risposto: SR26 e SR27 rimessi com'erano.\n");
        } else {
            printf("       ! LA FINESTRA RISPONDE: era il modo MMIO spento.\n");
        }
    }

    /* -------------------------------------------------------------------
     * 6. CHI ALTRO DECODIFICA QUELL'INDIRIZZO?
     *
     * ! UN «TUTTI UNO» VUOL DIRE CHE NESSUNO HA RISPOSTO, ma non dice che la
     * scheda non volesse rispondere: puo' anche voler dire che la richiesta
     * non le e' mai arrivata, perche' qualcuno a monte se l'e' presa prima.
     * Sul bus PCI vince chi decodifica positivamente, e il north bridge
     * decodifica per primo.
     *
     * Il north bridge di questa macchina ha un BAR a 0xE0000000. Se quella e'
     * l'APERTURA AGP — e su un chipset SiS di quell'epoca lo e' quasi sempre —
     * la sua misura predefinita e' 128 MB, cioe' 0xE0000000-0xE7FFFFFF. I
     * registri della scheda stanno a 0xE2100000, che ci casca DENTRO; il
     * framebuffer sta a 0xE8000000, che ci casca FUORI di un soffio.
     *
     * Sarebbe la spiegazione di tutto: la finestra che risponde e' quella
     * fuori dall'apertura, quella muta e' quella dentro.
     *
     * La misura dell'apertura si legge nella capacita' AGP del north bridge,
     * registro APSIZE. Qui si scorre la lista delle capacita' e la si stampa.
     * ------------------------------------------------------------------- */
    printf("\n  -- 6. il north bridge, e l'apertura AGP --\n");
    {
        unsigned int w, cap, id, ver, apsize, apbase, giri;

        w = cfg_leggi(0, 0, 0, 0x00);
        printf("       north bridge 00:00.0 = %04x:%04x\n",
               w & 0xFFFFu, (w >> 16) & 0xFFFFu);
        printf("       suo BAR0 = 0x%08x\n", cfg_leggi(0, 0, 0, 0x10));

        /* La lista delle capacita': si entra da 0x34 e si salta di puntatore
         * in puntatore. Il giro e' contato, perche' una lista rotta e' un
         * anello e un anello e' un processo che non finisce. */
        cap = cfg_leggi(0, 0, 0, 0x34) & 0xFFu;
        for (giri = 0; cap >= 0x40 && giri < 32; giri++) {
            w  = cfg_leggi(0, 0, 0, cap);
            id = w & 0xFFu;
            printf("       capacita' a 0x%02x: id 0x%02x\n", cap, id);

            if (id == 0x02) {               /* AGP */
                ver    = (w >> 16) & 0xFFu;
                apsize = cfg_leggi(0, 0, 0, cap + 0x14);
                apbase = cfg_leggi(0, 0, 0, cap + 0x18);
                printf("         AGP versione 0x%02x\n", ver);
                printf("         APSIZE = 0x%08x\n", apsize);
                printf("         APBASE = 0x%08x\n", apbase);

                /* APSIZE: i bit bassi dicono quali bit dell'indirizzo il
                 * ponte ignora. La codifica classica e': 0xF0 = 4 MB,
                 * 0xE0 = 8, 0xC0 = 16, 0x80 = 32, 0x00 = 64 sul byte basso
                 * a 8 bit, e con il secondo byte si arriva a 256 MB. Invece
                 * di decodificarla a memoria si stampa grezza: il numero si
                 * legge, la tabella si guarda dopo. */
            }
            cap = (w >> 8) & 0xFFu;
        }
    }

    /* -------------------------------------------------------------------
     * 7. IL BAR ESISTE DAVVERO? SI MISURA
     *
     * ! UN BAR CHE NON C'E' LEGGE LO STESSO UN NUMERO. Il valore 0xE2100000
     * puo' essere una finestra vera assegnata dal BIOS, oppure il residuo di
     * un registro che quella scheda non implementa. I due casi si distinguono
     * in un modo solo: si scrivono tutti uno e si rilegge. Un BAR implementato
     * rende la propria MISURA (i bit bassi inchiodati a zero); uno che non
     * c'e' rende zero.
     *
     * ! E SI SPEGNE LA DECODIFICA PRIMA DI SCRIVERE, perche' per un istante
     * quel BAR decodificherebbe un intervallo enorme e ci finirebbe dentro
     * mezzo chipset. E' la regola di sempre per misurare un BAR, e qui vale
     * doppio: una delle finestre di questa scheda e' il framebuffer, cioe'
     * la console.
     *
     * ! FRA SPEGNERE E RIACCENDERE NON SI STAMPA NIENTE. La console di questa
     * macchina DISEGNA NEL FRAMEBUFFER: una printf in mezzo scriverebbe in una
     * finestra che in quel momento non e' decodificata. I valori si raccolgono
     * e si stampano dopo, quando tutto e' tornato com'era.
     * ------------------------------------------------------------------- */
    printf("\n  -- 7. i BAR esistono davvero? si misurano --\n");
    {
        unsigned int bus, slot, fn, w, trovata = 0;
        unsigned int cmd, b0, b1, m0, m1;

        for (bus = 0; bus < 8 && !trovata; bus++)
            for (slot = 0; slot < 32 && !trovata; slot++)
                for (fn = 0; fn < 8 && !trovata; fn++) {
                    w = cfg_leggi(bus, slot, fn, 0x00);
                    if ((w & 0xFFFF) != SIS_VENDITORE) continue;
                    if (((w >> 16) & 0xFFFF) != SIS_6330) continue;
                    trovata = 1;

                    cmd = cfg_leggi(bus, slot, fn, 0x04);
                    b0  = cfg_leggi(bus, slot, fn, 0x10);
                    b1  = cfg_leggi(bus, slot, fn, 0x14);

                    /* da qui in poi, e fino al ripristino, NIENTE printf */
                    cfg_scrivi(bus, slot, fn, 0x04, cmd & ~0x03u);
                    cfg_scrivi(bus, slot, fn, 0x10, 0xFFFFFFFFu);
                    m0 = cfg_leggi(bus, slot, fn, 0x10);
                    cfg_scrivi(bus, slot, fn, 0x10, b0);
                    cfg_scrivi(bus, slot, fn, 0x14, 0xFFFFFFFFu);
                    m1 = cfg_leggi(bus, slot, fn, 0x14);
                    cfg_scrivi(bus, slot, fn, 0x14, b1);
                    cfg_scrivi(bus, slot, fn, 0x04, cmd);
                    /* ...e da qui si puo' di nuovo parlare */

                    printf("       BAR0 %08x, maschera %08x -> %s\n", b0, m0,
                           (m0 == 0) ? "NON IMPLEMENTATO"
                                     : "implementato");
                    if (m0 != 0)
                        printf("         misura = %u KB\n",
                               (~(m0 & 0xFFFFFFF0u) + 1u) / 1024u);

                    printf("       BAR1 %08x, maschera %08x -> %s\n", b1, m1,
                           (m1 == 0) ? "NON IMPLEMENTATO"
                                     : "implementato");
                    if (m1 != 0)
                        printf("         misura = %u KB\n",
                               (~(m1 & 0xFFFFFFF0u) + 1u) / 1024u);

                    printf("       comando rimesso a 0x%04x\n",
                           cfg_leggi(bus, slot, fn, 0x04) & 0xFFFFu);
                }
    }

    /* -------------------------------------------------------------------
     * 8. LA PROVA CHE DECIDE: SI SPOSTA LA FINESTRA FUORI DALL'APERTURA
     *
     * Quel che si sa adesso: il BAR1 e' implementato e largo 128 KB, il ponte
     * lo inoltra, la scheda decodifica la memoria — e la finestra e' muta. Il
     * BAR0, che risponde, sta a 0xE8000000; il BAR1, che non risponde, sta a
     * 0xE2100000; e il north bridge ha un BAR a 0xE0000000.
     *
     * 0xE0000000 + 128 MB = 0xE8000000. L'intervallo che tace e' ESATTAMENTE
     * quello coperto dal BAR del north bridge, e quello che parla comincia
     * dove l'altro finisce. Sul bus vince chi decodifica per primo, e il north
     * bridge viene prima del ponte: le letture ai registri non arrivano mai
     * alla scheda.
     *
     * ! E' UNA SPIEGAZIONE, NON UNA PROVA, finche' non si sposta la finestra.
     * Qui la si sposta a 0xF0000000 — fuori dall'apertura, fuori dal
     * framebuffer, sotto le zone dell'APIC — e si rilegge. Se parla, era
     * quello.
     *
     * ! SI TOCCA LA FINESTRA NON-PREFETCHABLE DEL PONTE (0x20) E NON L'ALTRA.
     * Il framebuffer sta nella finestra PREFETCHABLE (0x24), che non si
     * tocca: la console continua a funzionare per tutta la prova.
     *
     * ! E SI RIMETTE TUTTO COM'ERA, sempre. Una macchina lasciata con un BAR
     * spostato e' una macchina che al prossimo avvio fa una cosa diversa da
     * quella che il suo BIOS credeva, e nessuno si ricorda perche'.
     * ------------------------------------------------------------------- */
    printf("\n  -- 8. e se si sposta la finestra fuori dall'apertura? --\n");
    {
        unsigned int bus, slot, fn, w, trovata = 0;
        unsigned int cmd, b1, ponte20;
        unsigned int nuovo = 0xF0000000u;
        MmioZona     n;
        volatile unsigned int *p;
        unsigned int v0, v1, v2, v3;

        for (bus = 0; bus < 8 && !trovata; bus++)
            for (slot = 0; slot < 32 && !trovata; slot++)
                for (fn = 0; fn < 8 && !trovata; fn++) {
                    w = cfg_leggi(bus, slot, fn, 0x00);
                    if ((w & 0xFFFF) != SIS_VENDITORE) continue;
                    if (((w >> 16) & 0xFFFF) != SIS_6330) continue;
                    trovata = 1;

                    cmd     = cfg_leggi(bus, slot, fn, 0x04);
                    b1      = cfg_leggi(bus, slot, fn, 0x14);
                    ponte20 = cfg_leggi(0, 1, 0, 0x20);

                    cfg_scrivi(bus, slot, fn, 0x04, cmd & ~0x03u);
                    cfg_scrivi(0, 1, 0, 0x20, 0xF000F000u);
                    cfg_scrivi(bus, slot, fn, 0x14, nuovo);
                    cfg_scrivi(bus, slot, fn, 0x04, cmd);

                    printf("       BAR1 spostato a 0x%08x, finestra del ponte "
                           "0x%08x\n", cfg_leggi(bus, slot, fn, 0x14),
                           cfg_leggi(0, 1, 0, 0x20));

                    n.fisico = nuovo;
                    n.byte   = MMIO_BYTE;
                    if (mmio_map(&n) != 0) {
                        printf("       mmio_map di 0x%08x rifiutata (%s)\n",
                               nuovo, strerror(errno));
                    } else {
                        p  = (volatile unsigned int *)n.virt;
                        v0 = p[0];
                        v1 = p[0x8200 / 4];
                        v2 = p[0x85CC / 4];
                        p[0x821C / 4] = 0xA5A5A5A5u;
                        v3 = p[0x821C / 4];

                        printf("       [0x0000] %08x  [0x8200] %08x  "
                               "[0x85cc] %08x\n", v0, v1, v2);
                        printf("       scritto 0xa5a5a5a5 in PAT_FGCOLOR, "
                               "rileggo %08x\n", v3);
                        if (v3 == 0xA5A5A5A5u)
                            printf("       ! E' QUELLO. La finestra era "
                                   "sepolta sotto l'apertura del north "
                                   "bridge.\n");
                        else
                            printf("       niente da fare nemmeno qui: non "
                                   "e' (solo) l'apertura.\n");
                    }

                    /* ! IL RIPRISTINO NON E' CONDIZIONATO AL RISULTATO. */
                    cfg_scrivi(bus, slot, fn, 0x04, cmd & ~0x03u);
                    cfg_scrivi(bus, slot, fn, 0x14, b1);
                    cfg_scrivi(0, 1, 0, 0x20, ponte20);
                    cfg_scrivi(bus, slot, fn, 0x04, cmd);

                    printf("       rimesso: BAR1 0x%08x, ponte 0x%08x, "
                           "comando 0x%04x\n",
                           cfg_leggi(bus, slot, fn, 0x14),
                           cfg_leggi(0, 1, 0, 0x20),
                           cfg_leggi(bus, slot, fn, 0x04) & 0xFFFFu);
                }
    }

    /* -------------------------------------------------------------------
     * 9. LA CONTROPROVA CHE MANCAVA: mmio_map SCRIVE DAVVERO DOVE DICE?
     *
     * ! AL PASSO 1 I DUE VALORI ERANO UGUALI PERCHE' ERANO TUTT'E DUE ZERO, e
     * zero uguale a zero non dimostra niente: lo schermo era nero. La prova
     * vera e' SCRIVERE da una parte e RILEGGERE dall'altra. Se le due
     * mappature sono davvero la stessa memoria, il valore passa.
     *
     * Si scrive in fondo al framebuffer, sotto l'ultima riga visibile, cosi'
     * non si sporca niente di quel che si vede.
     * ------------------------------------------------------------------- */
    printf("\n  -- 9. mmio_map scrive dove dice? si scrive di qua e si legge di la' --\n");
    {
        MmioZona z;
        volatile unsigned int *al;
        unsigned int *fb;
        unsigned int off;

        fb = (unsigned int *)fb_map();
        z.fisico = bar_fb;
        z.byte   = 0x10000;
        if (fb == 0 || mmio_map(&z) != 0) {
            printf("       non riesco a mappare tutt'e due: niente prova.\n");
        } else {
            al  = (volatile unsigned int *)z.virt;
            off = 0x8000 / 4;          /* dentro i primi 64 KB, fuori vista */

            al[off] = 0xCAFEBABEu;
            printf("       scritto 0xcafebabe con mmio_map, fb_map legge %08x\n",
                   fb[off]);
            fb[off] = 0x0BADF00Du;
            printf("       scritto 0x0badf00d con fb_map, mmio_map legge %08x\n",
                   al[off]);
            fb[off] = 0;
            if (al[off] == 0)
                printf("       ! LE DUE MAPPATURE SONO LA STESSA MEMORIA: "
                       "mmio_map funziona.\n");
            else
                printf("       ! NON COMBACIANO: e' mmio_map che non porta "
                       "dove dice.\n");
        }
    }

    /* -------------------------------------------------------------------
     * 10. E L'MMIO DI UN'ALTRA SCHEDA SI LEGGE?
     *
     * ! IL CONTROLLO SU UN TERZO. Se la finestra dei registri di un
     * controller USB si legge e da' un valore sensato, allora leggere MMIO su
     * questa macchina si puo', e il muto e' la SiS. Se non si legge nemmeno
     * quello, il problema non e' la scheda video: e' piu' in basso.
     *
     * Si sceglie un EHCI (classe 0x0C0320) perche' il suo PRIMO registro ha un
     * valore riconoscibile: byte basso = lunghezza delle capacita' (di solito
     * 0x10 o 0x20), byte alto-parola = versione 0x0100. Un numero che «si
     * riconosce» vale piu' di un numero che «non e' tutti uno».
     * ------------------------------------------------------------------- */
    printf("\n  -- 10. l'MMIO di un'altra scheda si legge? --\n");
    {
        unsigned int bus, slot, fn, w, cl, bar, fatto = 0;
        MmioZona z;
        volatile unsigned int *p;

        for (bus = 0; bus < 8 && !fatto; bus++)
            for (slot = 0; slot < 32 && !fatto; slot++)
                for (fn = 0; fn < 8 && !fatto; fn++) {
                    w = cfg_leggi(bus, slot, fn, 0x00);
                    if ((w & 0xFFFF) == 0xFFFF) continue;
                    cl = cfg_leggi(bus, slot, fn, 0x08) >> 8;   /* 24 bit */
                    if ((cl & 0xFFFFFF) != 0x0C0320u) continue; /* EHCI */

                    bar = cfg_leggi(bus, slot, fn, 0x10);
                    if (bar == 0 || (bar & 1)) continue;
                    bar &= 0xFFFFFFF0u;

                    z.fisico = bar;
                    z.byte   = 0x1000;
                    if (mmio_map(&z) != 0) {
                        printf("       EHCI a %02x:%02x.%x, BAR0 0x%08x: "
                               "mmio_map rifiutata (%s)\n", bus, slot, fn, bar,
                               strerror(errno));
                    } else {
                        p = (volatile unsigned int *)z.virt;
                        printf("       EHCI a %02x:%02x.%x, BAR0 0x%08x, "
                               "primo registro = %08x\n",
                               bus, slot, fn, bar, p[0]);
                        printf("         (versione 0x%04x, capacita' lunghe "
                               "%u byte)\n", (p[0] >> 16) & 0xFFFFu,
                               p[0] & 0xFFu);
                    }
                    fatto = 1;
                }
        if (!fatto) printf("       nessun EHCI su questa macchina.\n");
    }

    /* -------------------------------------------------------------------
     * 11. LA CONFIGURAZIONE PCI DELLA SCHEDA, PER INTERO
     *
     * I registri da 0x40 in su sono quelli che il costruttore si tiene per se'
     * e che nessun documento pubblico descrive. Non si cerca un bit da
     * accendere a caso: si mette il contenuto nel diario, perche' il giorno in
     * cui salta fuori un documento — o un altro driver — ci sia qualcosa da
     * confrontare.
     * ------------------------------------------------------------------- */
    printf("\n  -- 11. la configurazione PCI della scheda --\n");
    {
        unsigned int bus, slot, fn, w, trovata = 0, r;

        for (bus = 0; bus < 8 && !trovata; bus++)
            for (slot = 0; slot < 32 && !trovata; slot++)
                for (fn = 0; fn < 8 && !trovata; fn++) {
                    w = cfg_leggi(bus, slot, fn, 0x00);
                    if ((w & 0xFFFF) != SIS_VENDITORE) continue;
                    if (((w >> 16) & 0xFFFF) != SIS_6330) continue;
                    trovata = 1;

                    for (r = 0; r < 0x100; r += 16) {
                        printf("       %02x: %08x %08x %08x %08x\n", r,
                               cfg_leggi(bus, slot, fn, r),
                               cfg_leggi(bus, slot, fn, r + 4),
                               cfg_leggi(bus, slot, fn, r + 8),
                               cfg_leggi(bus, slot, fn, r + 12));
                    }
                }
    }

    /* -------------------------------------------------------------------
     * 12. IL PONTE, PER INTERO — e il sospetto che l'EHCI ha acceso
     *
     * ! L'EHCI RISPONDE A 0xE2004000, cioe' nella stessa fascia in cui la SiS
     * tace, e questo chiude l'ipotesi dell'apertura del north bridge: se
     * quell'intervallo fosse sepolto, sarebbe muto anche lui. Ma l'EHCI sta
     * sul BUS 0, la scheda video dietro il ponte 00:01.0. L'unica differenza
     * rimasta fra chi parla e chi tace e' il ponte.
     *
     * ! E IL FRAMEBUFFER CHE FUNZIONA NON DIMOSTRA CHE IL PONTE INOLTRI. Su un
     * chipset a memoria condivisa la finestra del framebuffer la decodifica il
     * NORTH BRIDGE, che la gira alla RAM di sistema: quei cicli sul bus 1 non
     * ci arrivano mai. Percio' «lo schermo si vede» non dice niente sul ponte,
     * ed e' un errore facile da fare — l'ho fatto.
     *
     * Quel che si guarda qui: il registro di comando del ponte (senza il bit
     * della memoria un ponte non inoltra niente verso il basso), i numeri di
     * bus primario/secondario/subordinato (la scheda e' davvero dietro questo
     * ponte?), e il registro di controllo.
     * ------------------------------------------------------------------- */
    printf("\n  -- 12. il ponte, per intero --\n");
    {
        unsigned int bus, slot, fn, w, cl, r;

        for (bus = 0; bus < 4; bus++)
            for (slot = 0; slot < 32; slot++)
                for (fn = 0; fn < 8; fn++) {
                    w = cfg_leggi(bus, slot, fn, 0x00);
                    if ((w & 0xFFFF) == 0xFFFF) continue;
                    cl = cfg_leggi(bus, slot, fn, 0x08) >> 16;
                    if ((cl & 0xFF00) != 0x0600) continue;
                    if ((cl & 0x00FF) != 0x04) continue;

                    printf("       ponte %02x:%02x.%x = %04x:%04x\n",
                           bus, slot, fn, w & 0xFFFFu, (w >> 16) & 0xFFFFu);

                    w = cfg_leggi(bus, slot, fn, 0x04);
                    printf("         comando 0x%04x  [%s%s%s]  stato 0x%04x\n",
                           w & 0xFFFFu,
                           (w & 0x01) ? "io " : "",
                           (w & 0x02) ? "memoria " : "! SENZA MEMORIA ",
                           (w & 0x04) ? "master" : "! senza master",
                           (w >> 16) & 0xFFFFu);

                    w = cfg_leggi(bus, slot, fn, 0x18);
                    printf("         bus: primario %u, secondario %u, "
                           "subordinato %u\n", w & 0xFFu, (w >> 8) & 0xFFu,
                           (w >> 16) & 0xFFu);

                    w = cfg_leggi(bus, slot, fn, 0x3C);
                    printf("         controllo del ponte 0x%04x  [%s]\n",
                           (w >> 16) & 0xFFFFu,
                           ((w >> 16) & 0x08) ? "VGA abilitata"
                                              : "VGA non abilitata");

                    for (r = 0; r < 0x40; r += 16)
                        printf("         %02x: %08x %08x %08x %08x\n", r,
                               cfg_leggi(bus, slot, fn, r),
                               cfg_leggi(bus, slot, fn, r + 4),
                               cfg_leggi(bus, slot, fn, r + 8),
                               cfg_leggi(bus, slot, fn, r + 12));
                }
    }

    /* -------------------------------------------------------------------
     * 13. LA SEQUENZA DI sisfb, PER INTERO E NELL'ORDINE
     *
     * ! AL PASSO 5 AVEVO SALTATO IL RESET. sisfb_engine_init fa quattro cose in
     * quest'ordine: soglia in SR27, RESET della coda in SR26, un giro sui
     * puntatori della coda, e solo ALLORA SR26 con il bit MMIO. Io avevo
     * scritto direttamente il bit MMIO: se e' il reset a svegliare il blocco,
     * quella prova non provava niente.
     *
     * ! E SI LEGGE ANCHE QUANTA MEMORIA VIDEO C'E'. CR79 bit 7-4 danno la
     * misura della memoria condivisa (2^n megabyte), CR78 bit 5-4 dicono se
     * c'e' anche memoria dedicata. Servono per dare alla coda una base che
     * stia DENTRO la memoria della scheda: una base a caso vorrebbe dire un
     * motore che scrive dove gli pare.
     * ------------------------------------------------------------------- */
    printf("\n  -- 13. la sequenza di sisfb, nell'ordine giusto --\n");
    {
        unsigned int sr26, sr27, cr78, cr79, vram_kb = 0;
        unsigned int q;

        /* CR: indice 0x3D4, dato 0x3D5. Le porte VGA sono gia' prese. */
        ioport_out(0x3D4, 0x79);
        cr79 = (unsigned int)ioport_in(0x3D5);
        ioport_out(0x3D4, 0x78);
        cr78 = (unsigned int)ioport_in(0x3D5);
        printf("       CR79 = 0x%02x, CR78 = 0x%02x\n", cr79, cr78);
        if (cr79 & 0xF0) {
            vram_kb = (1u << ((cr79 & 0xF0u) >> 4)) * 1024u;
            printf("       memoria condivisa (UMA): %u KB\n", vram_kb);
        }
        if ((cr78 & 0x30) == 0x10) { vram_kb += 32768u;
            printf("       piu' 32768 KB dedicati (LFB)\n"); }
        else if ((cr78 & 0x30) == 0x30) { vram_kb += 65536u;
            printf("       piu' 65536 KB dedicati (LFB)\n"); }
        if (vram_kb == 0) printf("       ! misura non riconosciuta\n");

        ioport_out(SEQ_IDX, 0x26);
        sr26 = (unsigned int)ioport_in(SEQ_DAT);
        ioport_out(SEQ_IDX, 0x27);
        sr27 = (unsigned int)ioport_in(SEQ_DAT);

        /* 1. soglia */
        ioport_out(SEQ_IDX, 0x27);
        ioport_out(SEQ_DAT, 0x1F);
        /* 2. RESET della coda — il passo che mancava */
        ioport_out(SEQ_IDX, 0x26);
        ioport_out(SEQ_DAT, 0x01);
        printf("       SR26 = 0x01 (reset), [0x8200] legge %08x\n",
               a[0x8200 / 4]);
        /* 3. il giro sui puntatori */
        q = a[0x85C8 / 4];                  /* READPORT  */
        a[0x85C4 / 4] = q;                  /* WRITEPORT */
        printf("       READPORT = %08x\n", q);
        /* 4. e adesso il bit MMIO, con l'autocorrezione */
        ioport_out(SEQ_IDX, 0x26);
        ioport_out(SEQ_DAT, 0x22);          /* 512k | MMIO_CMD_ENABLE | AUTO */
        ioport_out(SEQ_IDX, 0x26);
        printf("       SR26 = 0x%02x\n", (unsigned int)ioport_in(SEQ_DAT));

        /* 5. la base della coda, in fondo alla memoria video */
        if (vram_kb >= 1024u) {
            unsigned int base = (vram_kb - 512u) * 1024u;

            a[0x85C0 / 4] = base;
            printf("       base della coda = 0x%08x\n", base);
        }

        printf("       [0x0000] %08x  [0x8200] %08x  [0x85cc] %08x\n",
               a[0], a[0x8200 / 4], a[0x85CC / 4]);
        a[0x821C / 4] = 0xA5A5A5A5u;
        printf("       scritto 0xa5a5a5a5 in PAT_FGCOLOR, rileggo %08x\n",
               a[0x821C / 4]);

        if (a[0x821C / 4] == 0xA5A5A5A5u) {
            printf("       ! LA FINESTRA RISPONDE: mancava la sequenza "
                   "completa.\n");
        } else {
            ioport_out(SEQ_IDX, 0x26);
            ioport_out(SEQ_DAT, sr26);
            ioport_out(SEQ_IDX, 0x27);
            ioport_out(SEQ_DAT, sr27);
            printf("       muta anche cosi'. SR26 e SR27 rimessi a 0x%02x e "
                   "0x%02x.\n", sr26, sr27);
        }
    }

    /* -------------------------------------------------------------------
     * 14. I DUE BIT CHE NON SONO MAI STATI ACCESI — E QUI NON SI TOCCANO
     *
     * ! LA DIAGNOSI LI STAMPAVA DAL PRIMO GIORNO E NESSUNO LI HA LETTI. Il
     * passo 4 dice SR1E = 0x20 e SR20 = 0xa0 da quando esiste, e sono finiti
     * cosi' com'erano nel diario, fra i dati «stabiliti sulla macchina vera».
     * Erano due misure, non due conferme.
     *
     * In sisfb (drivers/video/fbdev/sis/sis.h) quei due registri hanno un
     * nome, e i nomi dicono esattamente cio' che qui manca:
     *
     *     IND_SIS_PCI_ADDRESS_SET = 0x20   SR20
     *         SIS_PCI_ADDR_ENABLE     0x80   <- c'e' gia' (0xa0)
     *         SIS_MEM_MAP_IO_ENABLE   0x01   <- MANCA
     *
     *     IND_SIS_MODULE_ENABLE   = 0x1E   SR1E
     *         SIS_ENABLE_2D           0x40   <- MANCA (0x20)
     *
     * e sisfb li accende PRIMA di qualunque altra cosa. Noi eravamo partiti da
     * SR26 e SR27 — la CODA dei comandi — cioe' dall'ultimo passo di
     * un'inizializzazione di cui mancava il primo.
     *
     * ! E SPIEGHEREBBE ANCHE PERCHE' TUTTO IL RESTO E' A POSTO. Il ponte
     * inoltra, il BAR e' implementato e misura 128 KB veri, mmio_map mappa la
     * memoria giusta: la finestra c'e', semplicemente la scheda non la
     * decodifica. 0xffffffff a ogni offset e' quel che il bus rende quando
     * NESSUNO risponde.
     *
     * ! MA QUI NON SI SCRIVONO, E QUESTA E' UNA PROVA GIA' PAGATA.
     * Il 16 settembre 2026 la diagnosi li accendeva tutt'e due in coda a
     * questo passo. La macchina si e' BLOCCATA — non risponde piu' nemmeno al
     * ping, ci vuole un ciclo di alimentazione — e con lei sono spariti il
     * chilobyte di uscita ancora in viaggio nel pty e la risposta alla sola
     * domanda che contava: QUALE DEI DUE. Accesi insieme, un blocco non
     * distingue niente.
     *
     * Adesso stanno in due comandi separati, `-2dsr1e` e `-2dsr20`, che ne
     * toccano UNO SOLO per volta, dicono cosa stanno per fare e ASPETTANO che
     * la riga sia uscita prima di farlo. Vedi sis2d_accendi().
     *
     * ! E LA DIAGNOSI RESTA DI SOLA LETTURA — cioe' resta ripetibile. Uno
     * strumento che puo' spegnere la macchina che sta misurando smette di
     * essere lo strumento con cui si comincia.
     * ------------------------------------------------------------------- */
    printf("\n  -- 14. i due bit di accensione (non li tocco) --\n");
    {
        unsigned int sr1e, sr20;

        ioport_out(SEQ_IDX, 0x1E);
        sr1e = (unsigned int)ioport_in(SEQ_DAT);
        ioport_out(SEQ_IDX, 0x20);
        sr20 = (unsigned int)ioport_in(SEQ_DAT);

        printf("       SR1E = 0x%02x   bit 6 (SIS_ENABLE_2D)       %s\n",
               sr1e, (sr1e & 0x40) ? "acceso" : "SPENTO");
        printf("       SR20 = 0x%02x   bit 0 (SIS_MEM_MAP_IO_ENABLE) %s\n",
               sr20, (sr20 & 0x01) ? "acceso" : "SPENTO");

        if (!(sr1e & 0x40) || !(sr20 & 0x01)) {
            printf("\n       ! E' QUI CHE SI GUARDA ADESSO. sisfb accende questi\n");
            printf("         due prima della coda comandi, e noi siamo partiti\n");
            printf("         dalla coda.\n");
            printf("         Si provano UNO PER VOLTA, e NON da qui:\n");
            printf("             sis.drv -2dsr1e    solo SR1E bit 6\n");
            printf("             sis.drv -2dsr20    solo SR20 bit 0\n");
            printf("         ! il 16 settembre 2026, accesi insieme, hanno\n");
            printf("           BLOCCATO la macchina. Uno per volta, e con\n");
            printf("           qualcuno vicino all'interruttore.\n");
        }
    }

    printf("\n  -- fine diagnosi --\n");
}


int sis2d_apri(int verboso)
{
    VideoInfo v;
    MmioZona  m;
    unsigned int bar_fb, bar_reg;

    if (g_mmio) return 0;                 /* gia' aperto */

    if (video_info(&v) != 0 || v.larghezza == 0 || v.fisico == 0) {
        printf("sis2d: lo schermo e' in modo TESTO: il motore 2D non ha\n");
        printf("       niente su cui lavorare. Scegli una risoluzione:\n");
        printf("           /dev/svga.drv 800x600   e riavvia\n");
        return -1;
    }
    if (v.bit != 32 && v.bit != 16 && v.bit != 8) {
        printf("sis2d: %u bit per pixel: il motore ne conosce 8, 16 e 32.\n",
               v.bit);
        return -1;
    }

    if (ioport_bind(PCI_INDIRIZZO, 8) != 0) {
        printf("sis2d: ioport_bind sul PCI rifiutata.\n");
        printf("       mi chiamo *.drv e giro da root? il varco e' quello.\n");
        return -1;
    }
    if (trova_bar(v.fisico, &bar_fb, &bar_reg, verboso) != 0) {
        printf("sis2d: non trovo i due BAR della 1039:6330.\n");
        return -1;
    }

    m.fisico = bar_reg;
    m.byte   = MMIO_BYTE;
    if (mmio_map(&m) != 0) {
        printf("sis2d: mmio_map di 0x%08x rifiutata (%s).\n",
               bar_reg, strerror(errno));
        return -1;
    }
    g_mmio = (volatile unsigned char *)m.virt;

    g_fb = (unsigned char *)fb_map();
    if (g_fb == 0) {
        printf("sis2d: fb_map rifiutata (%s): senza framebuffer non posso\n"
               "       RILEGGERE quel che il motore disegna, e senza\n"
               "       rilettura non e' una prova.\n", strerror(errno));
        return -1;
    }

    g_vram  = bar_fb;
    g_off   = v.fisico - bar_fb;
    g_passo = v.passo;
    g_larg  = v.larghezza;
    g_alt   = v.altezza;
    g_bit   = v.bit;

    g_dstcol = (v.bit == 32) ? DSTCOL_32 : (v.bit == 16) ? DSTCOL_16 : DSTCOL_8;
    g_depth  = (v.bit == 32) ? DEPTH_32  : (v.bit == 16) ? DEPTH_16  : DEPTH_8;

    if (verboso) {
        printf("sis2d: registri a 0x%08x (visti a %p), VRAM a 0x%08x\n",
               bar_reg, (void *)m.virt, bar_fb);
        printf("       schermo %ux%u a %u bit, passo %u, offset in VRAM %u\n",
               g_larg, g_alt, g_bit, g_passo, g_off);
    }
    return 0;
}


/* -----------------------------------------------------------------------------
 * Guardare e basta
 * --------------------------------------------------------------------------- */
void sis2d_stato(void)
{
    unsigned int q;

    if (g_mmio == 0) return;

    q = mm32r(Q_STATUS);
    printf("sis2d: Q_STATUS   0x%08x  -> il motore e' %s\n", q,
           (q & 0x80000000u) ? "FERMO" : "al lavoro (o non risponde)");
    printf("       SRC_ADDR   0x%08x   DST_ADDR   0x%08x\n",
           mm32r(SRC_ADDR), mm32r(DST_ADDR));
    printf("       SRC_PITCH  0x%08x   DST_PITCH  0x%08x\n",
           mm32r(SRC_PITCH), mm32r(DST_PITCH));
    printf("       SRC_XY     0x%08x   DST_XY     0x%08x\n",
           mm32r(SRC_Y), mm32r(DST_Y));
    printf("       RECT       0x%08x   PAT_FG     0x%08x\n",
           mm32r(RECT_WIDTH), mm32r(PAT_FGCOLOR));
    printf("       COMMAND    0x%08x\n", mm32r(COMMAND_READY));

    /* ! SE Q_STATUS E' 0xFFFFFFFF NON E' UN MOTORE FERMO: e' una finestra che
     * non risponde, cioe' il BAR sbagliato o una mappatura che non e' andata.
     * Un motore muto e uno spento si distinguono solo cosi'. */
    if (q == 0xFFFFFFFFu)
        printf("       ! tutti uno: questa finestra non risponde. Non e' il\n"
               "         motore fermo, e' il posto sbagliato.\n");
}


/* -----------------------------------------------------------------------------
 * Le due operazioni
 *
 * ! L'ORDINE DELLE SCRITTURE E' QUELLO DI sisfb, e non e' indifferente: il
 * comando si scrive in COMMAND_READY e poi si tocca FIRE_TRIGGER, e da quel
 * momento il motore lavora su cio' che trova negli altri registri. Metterne
 * uno dopo il grilletto vuol dire darglielo per l'operazione dopo.
 * --------------------------------------------------------------------------- */
int sis2d_riempi(unsigned int x, unsigned int y,
                 unsigned int w, unsigned int h, unsigned int colore)
{
    unsigned int comando;

    if (g_mmio == 0 || w == 0 || h == 0) return -1;
    if (attendi() != 0) return -1;

    comando = ((unsigned int)ROP_PATCOPY << 8) | CMD_PATFG | CMD_BITBLT | g_depth;

    mm32(PAT_FGCOLOR, colore);
    mm32(DST_PITCH, (0x0FFFu << 16) | (g_passo & 0xFFFFu));
    mm16(AGP_BASE, g_dstcol);
    mm32(DST_ADDR, g_off);
    mm32(DST_Y, (x << 16) | y);
    mm32(RECT_WIDTH, (h << 16) | w);
    mm32(COMMAND_READY, comando);
    mm32(FIRE_TRIGGER, 0);

    return attendi();
}

int sis2d_copia(unsigned int sx, unsigned int sy,
                unsigned int dx, unsigned int dy,
                unsigned int w, unsigned int h)
{
    unsigned int comando;

    if (g_mmio == 0 || w == 0 || h == 0) return -1;
    if (attendi() != 0) return -1;

    comando = ((unsigned int)ROP_SRCCOPY << 8) | CMD_SRCVIDEO | CMD_BITBLT |
              g_depth;

    mm16(AGP_BASE, g_dstcol);
    mm16(SRC_PITCH, g_passo);
    mm32(DST_PITCH, (0x0FFFu << 16) | (g_passo & 0xFFFFu));
    mm32(SRC_ADDR, g_off);
    mm32(DST_ADDR, g_off);
    mm32(RECT_WIDTH, (h << 16) | w);
    mm32(SRC_Y, (sx << 16) | sy);
    mm32(DST_Y, (dx << 16) | dy);
    mm32(COMMAND_READY, comando);
    mm32(FIRE_TRIGGER, 0);

    return attendi();
}


/* -----------------------------------------------------------------------------
 * La prova: disegnare e poi ANDARE A VEDERE
 * --------------------------------------------------------------------------- */
static unsigned int leggi_px(unsigned int x, unsigned int y)
{
    const unsigned char *p = g_fb + y * g_passo + x * (g_bit >> 3);

    if (g_bit == 32) return *(const volatile unsigned int *)p & 0x00FFFFFFu;
    if (g_bit == 16) return *(const volatile unsigned short *)p;
    return *p;
}

/* Il colore come lo vedra' il framebuffer, per poterlo confrontare. */
static unsigned int atteso(unsigned int colore)
{
    if (g_bit == 32) return colore & 0x00FFFFFFu;
    if (g_bit == 16) return colore & 0xFFFFu;
    return colore & 0xFFu;
}

/* Il valore da dare al motore per ottenere quel colore: a 32 bit e' lo stesso,
 * piu' in basso il motore vuole il pixel gia' impacchettato e RIPETUTO. */
static unsigned int impacchetta(unsigned int colore)
{
    if (g_bit == 32) return colore;
    if (g_bit == 16) return (colore & 0xFFFFu) | ((colore & 0xFFFFu) << 16);
    return (colore & 0xFFu) * 0x01010101u;
}

static int errori;

static void male(const char *che, unsigned int x, unsigned int y,
                 unsigned int visto, unsigned int voluto)
{
    printf("  MALE: %s in (%u,%u): c'e' 0x%06x, ci voleva 0x%06x\n",
           che, x, y, visto, voluto);
    errori++;
}

int sis2d_prova(void)
{
    const unsigned int X = 64, Y = 64, W = 128, H = 96;
    unsigned int c1 = impacchetta(0x00FF0000u);   /* rosso  */
    unsigned int c2 = impacchetta(0x0000FF00u);   /* verde  */
    unsigned int i;

    if (g_mmio == 0) return -1;
    errori = 0;

    printf("sis2d: prova 1 - un rettangolo pieno, e poi lo si rilegge\n");

    /* Prima si annerisce una fascia larga, cosi' quel che c'era sotto non
     * puo' far sembrare giusta una prova sbagliata. */
    if (sis2d_riempi(0, 0, g_larg, Y + H + 64, impacchetta(0)) != 0) {
        printf("  ! il motore non ha risposto entro l'attesa.\n");
        printf("    Non e' \"lento\": e' fermo. Guarda `-2dstato`.\n");
        return -1;
    }
    if (sis2d_riempi(X, Y, W, H, c1) != 0) {
        printf("  ! il motore non ha risposto entro l'attesa.\n");
        return -1;
    }

    /* Dentro deve esserci il colore; appena fuori no. Tutti e quattro i
     * bordi, perche' uno scambio fra x e y passa il controllo di uno solo. */
    if (leggi_px(X, Y) != atteso(0x00FF0000u))
        male("l'angolo alto-sinistra", X, Y, leggi_px(X, Y), atteso(0x00FF0000u));
    if (leggi_px(X + W - 1, Y + H - 1) != atteso(0x00FF0000u))
        male("l'angolo basso-destra", X + W - 1, Y + H - 1,
             leggi_px(X + W - 1, Y + H - 1), atteso(0x00FF0000u));
    if (leggi_px(X - 1, Y) != 0)
        male("un pixel FUORI a sinistra", X - 1, Y, leggi_px(X - 1, Y), 0);
    if (leggi_px(X + W, Y) != 0)
        male("un pixel FUORI a destra", X + W, Y, leggi_px(X + W, Y), 0);
    if (leggi_px(X, Y - 1) != 0)
        male("un pixel FUORI sopra", X, Y - 1, leggi_px(X, Y - 1), 0);
    if (leggi_px(X, Y + H) != 0)
        male("un pixel FUORI sotto", X, Y + H, leggi_px(X, Y + H), 0);

    if (errori == 0) printf("  il riempimento e' giusto, bordi compresi.\n");

    /* --- prova 2: la copia schermo -> schermo --- */
    printf("sis2d: prova 2 - la copia schermo -> schermo\n");

    /* Una riga di verde dentro il rosso, per avere un disegno riconoscibile
     * e non una tinta piatta: una copia che sbaglia di qualche pixel su una
     * tinta piatta non si vede. */
    if (sis2d_riempi(X, Y + 8, W, 8, c2) != 0) return -1;

    if (sis2d_copia(X, Y, X + W + 32, Y, W, H) != 0) {
        printf("  ! il motore non ha risposto entro l'attesa.\n");
        return -1;
    }

    for (i = 0; i < H; i++) {
        unsigned int a = leggi_px(X + 4, Y + i);
        unsigned int b = leggi_px(X + W + 32 + 4, Y + i);

        if (a != b) {
            male("la copia, riga", X + W + 32 + 4, Y + i, b, a);
            break;
        }
    }
    if (errori == 0) printf("  la copia e' identica all'originale.\n");

    /* --- prova 3: la copia che si SOVRAPPONE --- */
    printf("sis2d: prova 3 - la copia sovrapposta (e' quella dello "
           "scorrimento)\n");

    /* ! E' LA PROVA CHE CONTA PER wserver. Spostare una finestra e far
     * scorrere un terminale sono copie in cui sorgente e destinazione si
     * accavallano: se il motore non se ne accorge da solo, il risultato e'
     * una striscia ripetuta invece dei pixel spostati. sisfb non gli dice mai
     * da che parte andare, quindi o lo decide lui o quella riga e' sbagliata
     * da vent'anni: qui si guarda. */
    {
        unsigned int colonna[256];
        unsigned int n = (H < 256) ? H : 256;

        for (i = 0; i < n; i++) colonna[i] = leggi_px(X + 4, Y + i);

        if (sis2d_copia(X, Y, X, Y + 8, W, H - 8) != 0) return -1;

        for (i = 0; i + 8 < n; i++) {
            unsigned int b = leggi_px(X + 4, Y + 8 + i);

            if (b != colonna[i]) {
                male("la copia sovrapposta, riga", X + 4, Y + 8 + i,
                     b, colonna[i]);
                printf("    ! il motore NON gestisce la sovrapposizione da\n"
                       "      solo: chi lo usa per spostare deve accorgersene.\n");
                break;
            }
        }
        if (errori == 0)
            printf("  la sovrapposizione e' gestita dal motore.\n");
    }

    printf("\n");
    if (errori == 0) printf("sis2d: ! TUTTO GIUSTO.\n");
    else             printf("sis2d: ! %d ERRORI.\n", errori);
    if (g_scaduti)
        printf("sis2d: e %u attese sono scadute.\n", g_scaduti);

    return errori ? -1 : 0;
}


/* -----------------------------------------------------------------------------
 * Il motore contro la CPU
 *
 * ! SI MISURANO LE DUE COSE CHE wserver FA DAVVERO: riempire (lo sfondo, le
 * cornici, le barre) e copiare schermo su schermo (spostare una finestra, far
 * scorrere un terminale). La seconda e' quella per cui questo file esiste.
 * --------------------------------------------------------------------------- */
#define MS_MINIMI 400u

static void cpu_riempi(unsigned int x, unsigned int y,
                       unsigned int w, unsigned int h, unsigned int colore)
{
    unsigned int j;

    for (j = 0; j < h; j++)
        memset(g_fb + (y + j) * g_passo + x * (g_bit >> 3), (int)colore,
               w * (g_bit >> 3));
}

static void cpu_copia(unsigned int sx, unsigned int sy,
                      unsigned int dx, unsigned int dy,
                      unsigned int w, unsigned int h)
{
    unsigned int j, bpp = g_bit >> 3;

    for (j = 0; j < h; j++)
        memcpy(g_fb + (dy + j) * g_passo + dx * bpp,
               g_fb + (sy + j) * g_passo + sx * bpp, w * bpp);
}

static unsigned long mbs(unsigned int byte_giro, unsigned int giri,
                         unsigned int ms)
{
    unsigned int kib = (byte_giro >> 10) * giri;

    if (!ms || !kib) return 0;
    return (unsigned long)((kib * 125u / 128u) / ms);
}

int sis2d_misura(void)
{
    unsigned int w, h, byte, inizio, ms, giri;
    unsigned long m_cpu, m_mot;

    if (g_mmio == 0) return -1;

    /* Mezzo schermo, per poterlo copiare nell'altra meta' senza uscire. */
    w = g_larg;
    h = g_alt / 2;
    byte = g_passo * h;

    printf("\nsis2d: %ux%u a %u bit - %u byte per operazione\n\n",
           w, h, g_bit, byte);
    printf("  operazione                   CPU     motore   guadagno\n");
    printf("  ------------------------- -------- -------- ----------\n");

    /* --- riempire --- */
    inizio = uptime_ms(); giri = 0;
    do { cpu_riempi(0, 0, w, h, 0x20); giri++; ms = uptime_ms() - inizio; }
    while (ms < MS_MINIMI);
    m_cpu = mbs(byte, giri, ms);

    inizio = uptime_ms(); giri = 0;
    do { sis2d_riempi(0, 0, w, h, impacchetta(0x00204060u)); giri++;
         ms = uptime_ms() - inizio; } while (ms < MS_MINIMI);
    m_mot = mbs(byte, giri, ms);

    printf("  %-25s %6lu MB/s %6lu MB/s   %lu.%lux\n", "riempire",
           m_cpu, m_mot, m_cpu ? (m_mot * 10 / m_cpu) / 10 : 0,
           m_cpu ? (m_mot * 10 / m_cpu) % 10 : 0);

    /* --- copiare schermo su schermo --- */
    inizio = uptime_ms(); giri = 0;
    do { cpu_copia(0, 0, 0, h, w, h); giri++; ms = uptime_ms() - inizio; }
    while (ms < MS_MINIMI);
    m_cpu = mbs(byte, giri, ms);

    inizio = uptime_ms(); giri = 0;
    do { sis2d_copia(0, 0, 0, h, w, h); giri++; ms = uptime_ms() - inizio; }
    while (ms < MS_MINIMI);
    m_mot = mbs(byte, giri, ms);

    printf("  %-25s %6lu MB/s %6lu MB/s   %lu.%lux\n", "copiare schermo->schermo",
           m_cpu, m_mot, m_cpu ? (m_mot * 10 / m_cpu) / 10 : 0,
           m_cpu ? (m_mot * 10 / m_cpu) % 10 : 0);

    printf("\n  ! LA RIGA CHE CONTA E' LA SECONDA. Riempire lo schermo la CPU\n");
    printf("    lo fa gia' al limite del bus; copiare schermo su schermo le\n");
    printf("    costa una LETTURA dal framebuffer, che su questa scheda e'\n");
    printf("    quattro volte e mezzo piu' lenta di una scrittura.\n");

    if (g_scaduti)
        printf("\n  ! %u attese scadute durante la misura: i numeri del\n"
               "    motore non valgono.\n", g_scaduti);
    return 0;
}


/* =============================================================================
 * sis2d_accendi — UN bit per volta, e si dice prima di farlo
 *
 * ! ESISTE PERCHE' UN BLOCCO NON RACCONTA NIENTE SE LA PROVA E' DOPPIA.
 * Il 16 settembre 2026 la diagnosi accendeva SR20 bit 0 e SR1E bit 6 di
 * seguito, in fondo al passo 14. La macchina si e' fermata — niente ping, ciclo
 * di alimentazione — e quel che si e' imparato e' «uno dei due, o la coppia,
 * blocca questa scheda». Cioe' quasi niente, al prezzo di un riavvio fisico.
 *
 * ! E L'USCITA SI PERDE PRIMA DEL BLOCCO, NON DOPO. Quando la macchina si e'
 * fermata, il referto via telnet si era interrotto a META' DEL PASSO 11: circa
 * un chilobyte era ancora nel tubo del pty e nei buffer TCP, e li' e' rimasto.
 * L'ultima riga che si legge NON e' l'ultima riga che il programma ha stampato.
 * Per questo qui si stampa l'intenzione e POI si aspetta: mezzo secondo perche'
 * la riga esca davvero, prima di toccare il registro che potrebbe essere
 * l'ultimo gesto della macchina.
 *
 * ! IL BIT SI RIMETTE COM'ERA, sempre, anche quando la finestra risponde. Un
 * motore 2D acceso a meta' — decodifica accesa, coda comandi mai inizializzata
 * — e' una scheda che puo' fermarsi dopo, lontano da qui, e nessuno
 * collegherebbe le due cose. Quando si sapra' quale bit serve, lo accendera'
 * sis2d_apri() per intero e una volta sola.
 *
 * quale: 0x1E oppure 0x20, cioe' il registro da toccare.
 * ============================================================================= */
int sis2d_accendi(unsigned int quale)
{
    VideoInfo v;
    MmioZona  m;
    unsigned int bar_fb, bar_reg, prima, dopo, letto;
    unsigned char bit;
    const char *nome;
    volatile unsigned int *a;

    if (quale == 0x1E) { bit = 0x40; nome = "SIS_ENABLE_2D"; }
    else               { bit = 0x01; nome = "SIS_MEM_MAP_IO_ENABLE"; }

    if (video_info(&v) != 0 || v.fisico == 0) {
        printf("sis2d: lo schermo e' in modo testo: non c'e' niente da\n"
               "       accendere. Scegli una risoluzione e riavvia.\n");
        return -1;
    }
    if (ioport_bind(PCI_INDIRIZZO, 8) != 0) {
        printf("sis2d: ioport_bind sul PCI rifiutata.\n");
        return -1;
    }
    if (trova_bar(v.fisico, &bar_fb, &bar_reg, 0) != 0) return -1;

    m.fisico = bar_reg;
    m.byte   = MMIO_BYTE;
    if (mmio_map(&m) != 0) {
        printf("sis2d: mmio_map di 0x%08x rifiutata (%s).\n",
               bar_reg, strerror(errno));
        return -1;
    }
    a = (volatile unsigned int *)m.virt;

    if (ioport_bind(0x3B0, 0x30) != 0) {
        printf("sis2d: ioport_bind sulle porte VGA rifiutata.\n");
        return -1;
    }

    ioport_out(SEQ_IDX, (unsigned char)quale);
    prima = (unsigned int)ioport_in(SEQ_DAT);

    printf("sis2d: la finestra dei registri sta a 0x%08x e adesso legge %08x\n",
           bar_reg, a[0x8200 / 4]);
    printf("       SR%02X = 0x%02x, il bit 0x%02x (%s) e' %s\n",
           quale, prima, bit, nome, (prima & bit) ? "GIA' ACCESO" : "spento");

    if (prima & bit) {
        printf("       non c'e' niente da provare: e' gia' cosi'.\n");
        return 0;
    }

    printf("\n       ! STO PER SCRIVERE SR%02X = 0x%02x.\n",
           quale, (unsigned int)(prima | bit));
    printf("       ! SE QUESTA E' L'ULTIMA RIGA CHE LEGGI, E' STATO QUESTO.\n");
    printf("         (16 settembre 2026: i due bit accesi insieme hanno\n");
    printf("          bloccato la macchina. Questo ne tocca UNO.)\n");
    fflush(stdout);

    /* ! MEZZO SECONDO PERCHE' LA RIGA ESCA DAVVERO. fflush() svuota il buffer
     * della libc, non il tubo del pty ne' la finestra TCP: fra «l'ho scritta» e
     * «e' arrivata» ci sono altri due passaggi, e sono quelli che il 16
     * settembre si sono portati via un chilobyte di referto. */
    usleep(500000);

    ioport_out(SEQ_IDX, (unsigned char)quale);
    ioport_out(SEQ_DAT, (unsigned char)(prima | bit));

    ioport_out(SEQ_IDX, (unsigned char)quale);
    dopo = (unsigned int)ioport_in(SEQ_DAT);

    printf("\n       viva, e il registro ha preso: SR%02X = 0x%02x\n", quale, dopo);
    if (dopo == prima)
        printf("       ! il registro NON ha tenuto il bit: e' di sola lettura\n"
               "         su questa scheda, o protetto da SR05.\n");

    /* ! LA SCRITTURA E LA LETTURA SONO DUE GESTI DIVERSI, e si annunciano
     * separatamente. SR20 bit 0 accende una DECODIFICA: fin qui abbiamo
     * toccato solo una porta VGA, che non passa da nessun bus PCI. Il primo
     * accesso alla finestra e' un'altra cosa — e' un ciclo di memoria verso
     * un indirizzo che un attimo fa non rispondeva a nessuno. Se e' quello a
     * fermare la macchina, il messaggio qui sopra basta a saperlo, e mettere
     * le due cose sotto un annuncio solo vorrebbe dire ricomprare un ciclo di
     * alimentazione per la stessa domanda. */
    printf("\n       ! ADESSO LEGGO la finestra a 0x%08x, e non e' lo stesso\n",
           bar_reg);
    printf("         gesto: fin qui era una porta VGA, adesso e' un ciclo di\n");
    printf("         memoria su un bus dove un attimo fa non rispondeva\n");
    printf("         nessuno. Se ti fermi qui, e' stata la LETTURA.\n");
    fflush(stdout);
    usleep(500000);

    letto = a[0x8200 / 4];
    printf("       viva. [0x8200] %08x   [0x0000] %08x   [0x85cc] %08x\n",
           letto, a[0], a[0x85CC / 4]);

    printf("\n       ! E ADESSO SCRIVO nella finestra. Se ti fermi qui, e'\n");
    printf("         stata la SCRITTURA in memoria, non la lettura.\n");
    fflush(stdout);
    usleep(500000);

    a[0x821C / 4] = 0xA5A5A5A5u;
    printf("       viva. scritto 0xa5a5a5a5 in PAT_FGCOLOR, rileggo %08x\n",
           a[0x821C / 4]);

    if (a[0x821C / 4] == 0xA5A5A5A5u)
        printf("\n       ! LA FINESTRA RISPONDE, ED E' BASTATO QUESTO BIT.\n");
    else if (letto != 0xFFFFFFFFu)
        printf("\n       ! non risponde ancora, ma NON legge piu' tutti uno:\n"
               "         qualcosa e' cambiato, il bit serve ma non da solo.\n");
    else
        printf("\n       muta come prima: questo bit da solo non c'entra.\n");

    /* Sempre, anche quando ha risposto: vedi il commento in testa. */
    ioport_out(SEQ_IDX, (unsigned char)quale);
    ioport_out(SEQ_DAT, (unsigned char)prima);
    printf("       SR%02X rimesso a 0x%02x.\n", quale, prima);

    return 0;
}

/* =============================================================================
 * drivers/sonda/sonda.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * IL REFERTO DELLA MACCHINA — quel che serve per scrivere un driver, scritto
 * su un file dalla macchina stessa.
 *
 *     /dev/sonda.drv                 scrive /SONDA.TXT
 *     /dev/sonda.drv /SONDA2.TXT     lo scrive dove dici
 *     /dev/sonda.drv -auto           il primo nome libero fra SONDA1..SONDA9
 *     /dev/sonda.drv -f <file>       accetta di sovrascriverlo
 *     /dev/sonda.drv -ponte <file>   aggiunge il ponte video (vedi sotto)
 *     /dev/sonda.drv -schermo        niente file: l'essenziale a schermo,
 *                                    una schermata per volta, da fotografare
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' ESISTE, E PERCHE' SCRIVE INVECE DI STAMPARE
 *
 * Un driver per una scheda che non si ha sotto mano non si scrive. Il caso che
 * l'ha fatta nascere e' la grafica integrata SiS M760GX di un portatile Acer
 * (1039:6330, sottosistema 1025:0083): QEMU non emula nessuna SiS, quella
 * macchina non ha una porta seriale, e senza seriale un messaggio a schermo
 * dura quanto il prossimo riavvio.
 *
 * Percio' la macchina si avvia da un dischetto, guarda dentro se stessa, e
 * SCRIVE. Il dischetto e' l'unico supporto scrivibile che c'e' di sicuro
 * (dal CD non si scrive, il disco fisso non si tocca), e il file che resta
 * li' sopra si legge con calma da un'altra parte.
 *
 * ! IL NOME DEL FILE STA IN 8.3, e non e' pigrizia: su FAT i nomi lunghi qui
 * sono in sola lettura, e un referto che non si riesce a creare vale zero.
 *
 * -----------------------------------------------------------------------------
 * ! E QUANDO NON C'E' NIENTE SU CUI SCRIVERE: -schermo
 *
 * Il portatile che ha fatto nascere questo file il lettore di dischetti ce
 * l'ha attaccato all'USB, e una macchina che si avvia dal CD non ha nessun
 * supporto scrivibile: il CD non si scrive, il disco fisso e' di Windows, e
 * dietro un floppy USB non c'e' nessun controller 82077 — c'e' un bus che EX-OS
 * oggi non sa parlare.
 *
 * Allora il referto si guarda. -schermo salta il file e stampa SOLO cio' che
 * non si puo' dedurre altrove — il ponte nord, la scheda video, e i banchi di
 * registri — una schermata per volta, che si fotografa. Sono tre schermate per
 * modalita': meno comodo del file, ma non dipende da niente.
 *
 * ! I REGISTRI SI LEGGONO TUTTI PRIMA DI STAMPARE ANCHE QUI, anzi soprattutto
 * qui: stampare muove il cursore, muovere il cursore scrive sul CRTC, e il
 * CRTC e' proprio quello che stiamo leggendo. Prima si legge, poi si parla.
 *
 * -----------------------------------------------------------------------------
 * ! IL PCI SI LEGGE DALLE PORTE, NON DAL SERVIZIO pci.drv
 *
 * Sembra un passo indietro — c'e' un servizio PCI, e ogni altro driver gli
 * parla — ma qui la scelta e' voluta:
 *
 *   1. una sonda non deve dipendere da un servizio che potrebbe non essersi
 *      avviato: se pci.drv non parte proprio su QUELLA macchina, il referto
 *      che spiega perche' non uscirebbe;
 *   2. PCI_MSG_DISPOSITIVO da' i campi che servono a un driver, non i 256
 *      byte grezzi. Qui servono i byte: i registri di configurazione oltre
 *      0x40 sono quelli specifici del chipset — la memoria assegnata alla
 *      grafica integrata sta li' dentro, e nessuna struttura la prevede.
 *
 * -----------------------------------------------------------------------------
 * ! LA COSA PIU' UTILE CHE FA E' PERMETTERE UNA SOTTRAZIONE
 *
 * I registri estesi di una SiS non hanno un datasheet pubblico. Ma la
 * modalita' video la imposta il BIOS, con INT 10h, e il BIOS lascia i registri
 * nello stato che a noi serve saper riprodurre.
 *
 * Quindi il referto si prende DUE VOLTE:
 *
 *     svga.drv testo     riavvio, sonda -> SONDA1.TXT   (modo testo 80x25)
 *     svga.drv 800x600   riavvio, sonda -> SONDA2.TXT   (VESA lineare)
 *
 * e la differenza fra i due file E' la ricetta: dice esattamente quali
 * registri il BIOS cambia per passare dal testo alla grafica, e con quali
 * valori. Un modeset nativo scritto da quella sottrazione non e' indovinato:
 * e' copiato da chi lo sa fare.
 *
 * -----------------------------------------------------------------------------
 * ! COSA TOCCA, VISTO CHE UNA SONDA DOVREBBE SOLO GUARDARE
 *
 * Tre scritture, e sono tutte necessarie per LEGGERE:
 *
 *   SR05 = 0x86   il lucchetto dei registri estesi SiS. Senza, i registri da
 *                 0x06 in su si leggono come 0xFF — cioe' la parte che
 *                 interessa non si vede. Rimessa com'era alla fine.
 *   0x3C4/0x3D4   l'indice di un banco indicizzato: si scrive l'indice per
 *   0x3CE/0x3C0   poter leggere il dato. Non cambia lo stato della scheda.
 *   0x3C7         da quale voce della tavolozza cominciare a leggere.
 *
 * ! E L'INDICE DELL'ATTRIBUTO SI SCRIVE CON IL BIT 5 ACCESO. Quel bit dice
 * «la tavolozza la comanda il CRT, non la CPU»: azzerarlo spegne lo schermo
 * all'istante, e succede leggendo, non scrivendo. E' la trappola classica di
 * chi legge i registri dell'attributo, e costa uno schermo nero su una
 * macchina che non ha una seriale per dire cos'e' successo.
 *
 * -----------------------------------------------------------------------------
 * ! IL CRTC LO TOCCA ANCHE IL KERNEL, E QUINDI SI LEGGE DUE VOLTE
 *
 * Il cursore del testo si muove scrivendo l'indice 0x0A/0x0E/0x0F su 0x3D4
 * (kernel/arch/x86/vga.c). Il kernel e' preemptabile: fra la nostra scrittura
 * dell'indice e la nostra lettura del dato ci puo' entrare il cursore, e il
 * byte che leggiamo e' quello di un altro registro.
 *
 * Non si puo' chiudere la finestra da ring 3 — le interruzioni non si spengono
 * qui. Si legge tutto due volte e si DICHIARA quali indici sono usciti diversi:
 * un valore sospetto marcato vale infinitamente piu' di un valore pulito
 * sbagliato, che verrebbe scritto in un driver e cercato per giorni.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `sonda.drv -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("sonda.drv", "0.009");

#define FILE_PRED     "/SONDA.TXT"

/* La finestra VGA: da 0x3B0 a 0x3DF, cioe' il banco monocromatico, quello a
 * colori e tutto quel che c'e' in mezzo. Si prende intera perche' quale dei
 * due banchi risponda lo dice il registro Misc, e lo si sa solo dopo averlo
 * letto. */
#define VGA_BASE      0x3B0
#define VGA_PORTE     0x30

#define VGA_MISC_R    0x3CC     /* Misc Output, in lettura */
#define VGA_SEQ_IDX   0x3C4
#define VGA_SEQ_DAT   0x3C5
#define VGA_GC_IDX    0x3CE
#define VGA_GC_DAT    0x3CF
#define VGA_AC_IDX    0x3C0
#define VGA_AC_DAT    0x3C1
#define VGA_DAC_RIDX  0x3C7     /* da quale voce leggere la tavolozza */
#define VGA_DAC_DAT   0x3C9

/* Il CRTC risponde a 0x3D4 (colore) o 0x3B4 (mono): lo dice il bit 0 del
 * Misc Output. Riempiti da vga_scegli_crtc(). */
static unsigned int CRTC_IDX = 0x3D4;
static unsigned int CRTC_DAT = 0x3D5;

/* Il bus PCI dalla porta di configurazione. 0xCF8 si scrive a 32 bit e basta:
 * vedi il commento di ioport_out32 in libc.h — 0xCF9 e' il reset di mezzo
 * mondo, e scriverlo a byte riavvia la macchina. */
#define PCI_INDIRIZZO 0xCF8
#define PCI_DATO      0xCFC

/* Quanti bus guardare. Su una macchina come quella dell'Acer i dispositivi
 * stanno sul bus 0 e, se c'e' un AGP, sul bus 1. Sedici e' abbondante e
 * costa qualche migliaio di letture, cioe' niente. */
#define PCI_BUS_MAX   16

/* -----------------------------------------------------------------------------
 * Il file: si scrive a blocchi, non riga per riga
 *
 * Un floppy scrive un settore per volta, e una write() per riga vorrebbe dire
 * migliaia di giri di testina per un referto di venti pagine. Le righe si
 * accumulano qui e si scaricano piene.
 * --------------------------------------------------------------------------- */
#define BUF_DIM       4096
static char          g_buf[BUF_DIM];
static unsigned int  g_usati = 0;
static int           g_fd = -1;
static int           g_guasto = 0;   /* una write() fallita: si dice, e si smette */

/* -schermo: si stampa invece di scrivere, e si va per pagine. */
static int           g_schermo = 0;
static int           g_righe = 0;
#define RIGHE_PAGINA 21

static void svuota(void)
{
    ssize_t n;

    if (g_schermo || g_usati == 0 || g_fd < 0 || g_guasto) return;

    n = write(g_fd, g_buf, g_usati);
    if (n != (ssize_t)g_usati) {
        /* ! LO SCHERMO PUO' NON ESSERCI, MA IL RITORNO SI'. Se il dischetto e'
         * pieno o protetto in scrittura, il referto si ferma qui: meglio un
         * file troncato con un codice di uscita diverso da zero che uno
         * completo solo in apparenza. */
        g_guasto = 1;
    }
    g_usati = 0;
}

/* La pausa fra due schermate.
 *
 * ! SCADE DA SOLA, E QUESTA E' LA STRADA PRINCIPALE. Chi fotografa lo schermo
 * di una macchina che si sta studiando non e' detto che abbia una tastiera che
 * risponde: se l'8042 non c'e' o il driver non e' partito, un'attesa senza
 * scadenza fermerebbe il referto proprio sulla macchina che aveva piu' bisogno
 * di darlo. Venti secondi bastano a inquadrare e scattare.
 *
 * Il tasto e' la scorciatoia, non la condizione. ! E IN QEMU NON ARRIVA: i
 * tasti mandati dal monitor raggiungono la shell, non il figlio in primo
 * piano, e la pausa scade per intero — provato il 9 settembre 2026. Sulla
 * macchina vera, con una tastiera vera, INVIO fa avanzare; se non lo fa, si
 * aspetta e basta, e il referto esce lo stesso. */
#define PAUSA_MS  20000

static void pagina(void)
{
    struct pollfd v;
    char          scarto[32];

    printf("\n-- la schermata dopo fra 20 secondi (INVIO per subito) --\n");

    v.fd      = 0;
    v.events  = POLLIN;
    v.revents = 0;
    if (poll(&v, 1, PAUSA_MS) > 0 && (v.revents & POLLIN))
        (void)read(0, scarto, sizeof(scarto));

    g_righe = 0;
}

static void emetti(const char *fmt, ...)
{
    char    riga[512];
    int     n;
    __builtin_va_list ap;

    __builtin_va_start(ap, fmt);
    n = vsnprintf(riga, sizeof(riga), fmt, ap);
    __builtin_va_end(ap);

    if (n <= 0) return;
    if (n > (int)sizeof(riga) - 1) n = (int)sizeof(riga) - 1;

    if (g_schermo) {
        int k;

        write(1, riga, (unsigned int)n);
        for (k = 0; k < n; k++) if (riga[k] == '\n') g_righe++;
        if (g_righe >= RIGHE_PAGINA) pagina();
        return;
    }

    if (g_usati + (unsigned int)n > BUF_DIM) svuota();
    memcpy(g_buf + g_usati, riga, (unsigned int)n);
    g_usati += (unsigned int)n;
}

/* Un banco di byte in esadecimale, sedici per riga, con l'indice davanti. */
static void emetti_banco(const char *nome, const unsigned char *v,
                         unsigned int n)
{
    unsigned int i, j;

    for (i = 0; i < n; i += 16) {
        emetti("%s %02x:", nome, i);
        for (j = 0; j < 16 && i + j < n; j++) emetti(" %02x", v[i + j]);
        emetti("\n");
    }
}

/* Dice quali indici sono usciti diversi fra le due letture. Nessuna riga
 * quando combaciano tutti, che e' il caso normale. */
static void emetti_differenze(const char *nome, const unsigned char *a,
                              const unsigned char *b, unsigned int n)
{
    unsigned int i;
    int          trovate = 0;

    for (i = 0; i < n; i++) {
        if (a[i] == b[i]) continue;
        if (!trovate) {
            emetti("%s INSTABILI (letto due volte, valori diversi):\n", nome);
            trovate = 1;
        }
        emetti("%s   indice %02x: %02x poi %02x\n", nome, i, a[i], b[i]);
    }
    if (trovate)
        emetti("%s   ! non fidarsi di questi: qualcuno li ha toccati "
               "mentre leggevamo\n", nome);
}

/* -----------------------------------------------------------------------------
 * Il bus PCI
 * --------------------------------------------------------------------------- */

static unsigned int cfg_leggi(unsigned int bus, unsigned int slot,
                              unsigned int fn, unsigned int off)
{
    unsigned int ind, val = 0xFFFFFFFF;

    ind = 0x80000000u | (bus << 16) | (slot << 11) | (fn << 8) | (off & 0xFC);

    if (ioport_out32(PCI_INDIRIZZO, ind) != 0) return 0xFFFFFFFF;
    if (ioport_in32(PCI_DATO, &val) != 0)      return 0xFFFFFFFF;
    return val;
}

/* I 256 byte di configurazione di una funzione, cosi' come sono. */
static void dump_funzione(unsigned int bus, unsigned int slot, unsigned int fn)
{
    unsigned char cfg[256];
    unsigned int  i, v, classe, bar;

    for (i = 0; i < 256; i += 4) {
        v = cfg_leggi(bus, slot, fn, i);
        cfg[i]     = (unsigned char)(v & 0xFF);
        cfg[i + 1] = (unsigned char)((v >> 8) & 0xFF);
        cfg[i + 2] = (unsigned char)((v >> 16) & 0xFF);
        cfg[i + 3] = (unsigned char)((v >> 24) & 0xFF);
    }

    classe = ((unsigned int)cfg[0x0B] << 16) | ((unsigned int)cfg[0x0A] << 8) |
             cfg[0x09];

    emetti("\n");
    emetti("--- %02x:%02x.%u  %02x%02x:%02x%02x  rev %02x  classe %06x\n",
           bus, slot, fn,
           cfg[1], cfg[0], cfg[3], cfg[2], cfg[8], classe);
    emetti("    sottosistema %02x%02x:%02x%02x   irq %02x   comando %02x%02x\n",
           cfg[0x2D], cfg[0x2C], cfg[0x2F], cfg[0x2E], cfg[0x3C],
           cfg[5], cfg[4]);

    /* Le BAR decodificate: e' il primo dato che si va a cercare quando si
     * scrive un driver, e leggerlo dall'esadecimale ogni volta e' un invito
     * a sbagliarsi di un bit. */
    for (i = 0; i < 6; i++) {
        bar = ((unsigned int)cfg[0x10 + i * 4 + 3] << 24) |
              ((unsigned int)cfg[0x10 + i * 4 + 2] << 16) |
              ((unsigned int)cfg[0x10 + i * 4 + 1] << 8) |
               (unsigned int)cfg[0x10 + i * 4];
        if (bar == 0) continue;
        if (bar & 1)
            emetti("    BAR%u  porte   0x%04x\n", i, bar & 0xFFFC);
        else
            emetti("    BAR%u  memoria 0x%08x%s\n", i, bar & 0xFFFFFFF0,
                   (bar & 0x08) ? "  (prefetch)" : "");
    }

    /* A schermo bastano i primi 64 byte: l'intestazione, le BAR e il
     * sottosistema stanno tutti li' dentro. Il resto e' roba di chipset che
     * si legge con calma su un file, non su una fotografia. */
    emetti_banco("    cfg", cfg, g_schermo ? 64 : 256);
}

/* Rende la base I/O rilocata della prima scheda video trovata, 0 se non c'e'.
 * Su una SiS della serie 315 e' la finestra da 128 byte che porta ai
 * registri del ponte video: e' l'unica strada per sapere come e' pilotato lo
 * schermo di un portatile. */
static unsigned int g_vga_io = 0;

/* La finestra di REGISTRI IN MEMORIA della scheda video: la BAR di memoria
 * NON prefetchable. Quella prefetchable e' il framebuffer — grande, e senza
 * effetti collaterali a leggerla — mentre questa e' il blocco dei registri, e
 * su una SiS della serie 315 e' li' che sta il motore 2D. */
static unsigned int g_vga_mmio = 0;

/* E il framebuffer: la BAR di memoria PREFETCHABLE. Non serve a leggere
 * registri, serve a fare da CONTROLLO — vedi dump_mmio(). */
static unsigned int g_vga_fb = 0;

static void scandisci_pci(void)
{
    unsigned int bus, slot, fn, v, hdr, classe, i, bar;
    int          quanti = 0;

    emetti("\n");
    emetti("=============================================================\n");
    if (g_schermo)
        emetti("[PCI] il ponte nord e la scheda video, primi 64 byte\n");
    else
        emetti("[PCI] tutti i dispositivi, 256 byte di configurazione ciascuno\n");
    emetti("=============================================================\n");
    emetti("! il ponte nord e' 00:00.0: e' lui a dire se questo chipset e'\n");
    emetti("  un 661, un 741, un 760 o un 761 - la VGA integrata li chiama\n");
    emetti("  tutti 6330 e da sola non basta a distinguerli.\n");

    for (bus = 0; bus < PCI_BUS_MAX; bus++) {
        for (slot = 0; slot < 32; slot++) {
            v = cfg_leggi(bus, slot, 0, 0);
            if ((v & 0xFFFF) == 0xFFFF) continue;

            /* Bit 7 del tipo di intestazione: 1 = ha piu' funzioni. Senza
             * questo controllo si leggerebbe otto volte lo stesso
             * dispositivo, perche' chi ha una sola funzione risponde uguale
             * a tutte e otto. */
            hdr = (cfg_leggi(bus, slot, 0, 0x0C) >> 16) & 0xFF;

            for (fn = 0; fn < ((hdr & 0x80) ? 8u : 1u); fn++) {
                v = cfg_leggi(bus, slot, fn, 0);
                if ((v & 0xFFFF) == 0xFFFF) continue;

                classe = (cfg_leggi(bus, slot, fn, 0x08) >> 24) & 0xFF;

                /* ! A SCHERMO SI STAMPANO SOLO I DUE CHE CONTANO. Venti
                 * dispositivi per sedici righe l'uno fanno venti fotografie,
                 * e diciotto sono di schede che con la grafica non c'entrano.
                 * Il ponte nord dice quale chipset e'; la scheda video dice
                 * dove risponde. Il resto lo racconta il file, quando c'e'
                 * un posto dove scriverlo. */
                if (!g_schermo || classe == 0x03 ||
                    (bus == 0 && slot == 0 && fn == 0)) {
                    dump_funzione(bus, slot, fn);
                    quanti++;
                }

                if (classe != 0x03 || g_vga_io != 0) continue;

                /* La scheda video: si tengono da parte le sue finestre — a
                 * porte per il capitolo del ponte, in memoria per quello del
                 * motore 2D. */
                for (i = 0; i < 6; i++) {
                    bar = cfg_leggi(bus, slot, fn, 0x10 + i * 4);
                    if (bar == 0 || bar == 0xFFFFFFFFu) continue;

                    if (bar & 1) {
                        if (g_vga_io == 0) g_vga_io = bar & 0xFFFC;
                    } else if ((bar & 0x08) == 0) {
                        /* non prefetchable: i registri, non il framebuffer */
                        if (g_vga_mmio == 0) g_vga_mmio = bar & 0xFFFFFFF0u;
                    } else {
                        if (g_vga_fb == 0) g_vga_fb = bar & 0xFFFFFFF0u;
                    }
                }
            }
        }
    }

    emetti("\n[PCI] %d funzioni trovate\n", quanti);
}

/* -----------------------------------------------------------------------------
 * I registri VGA, standard ed estesi
 * --------------------------------------------------------------------------- */

static void vga_scegli_crtc(void)
{
    unsigned int misc = (unsigned int)ioport_in(VGA_MISC_R);

    if (misc & 0x01) { CRTC_IDX = 0x3D4; CRTC_DAT = 0x3D5; }
    else             { CRTC_IDX = 0x3B4; CRTC_DAT = 0x3B5; }
}

static void banco_indicizzato(unsigned int idx, unsigned int dat,
                              unsigned char *out, unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; i++) {
        ioport_out(idx, i);
        out[i] = (unsigned char)ioport_in(dat);
    }
}

static void banco_attributo(unsigned char *out, unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; i++) {
        /* Il registro di stato azzera il flip-flop che decide se la
         * prossima scrittura su 0x3C0 e' un indice o un dato. Va riletto
         * ogni volta: qui in mezzo puo' esserci passato chiunque. */
        (void)ioport_in(0x3DA);
        (void)ioport_in(0x3BA);

        /* ! IL BIT 5 SI SPEGNE PER LEGGERE, E SI RIACCENDE ALLA FINE.
         *
         * Acceso, la tavolozza interna dell'attributo (AR00..AR0F) NON e'
         * raggiungibile dalla CPU e la scheda rende sempre lo stesso byte:
         * i referti presi prima del 10 settembre 2026 hanno sedici 04 in
         * fila li' dentro, e non e' quello che c'e' nella scheda. Spento,
         * i registri si leggono davvero e lo schermo resta buio finche'
         * non si rimette — cosa che si fa due righe piu' sotto, ed e' il
         * motivo per cui questa sonda legge tutto prima di stampare. */
        ioport_out(VGA_AC_IDX, i);
        out[i] = (unsigned char)ioport_in(VGA_AC_DAT);
    }
    (void)ioport_in(0x3DA);
    (void)ioport_in(0x3BA);
    ioport_out(VGA_AC_IDX, 0x20);
}

static void dump_vga(void)
{
    static unsigned char sr_a[64],  sr_b[64];
    static unsigned char cr_a[256], cr_b[256];
    static unsigned char gr_a[16],  gr_b[16];
    static unsigned char ar_a[32],  ar_b[32];
    static unsigned char dac[768];
    unsigned int  i;
    unsigned int  misc, stato1, stato0;
    unsigned char sr05_prima, sr05_dopo;

    /* =========================================================================
     * PRIMA SI LEGGE TUTTO, POI SI PARLA.
     *
     * Non e' ordine per bellezza: stampare una riga muove il cursore, e il
     * cursore si muove scrivendo l'indice sul CRTC — lo stesso banco che
     * stiamo leggendo. Un emetti() in mezzo alle letture falsifica proprio i
     * numeri che il referto esiste per raccogliere. Su file si vedrebbe poco;
     * con -schermo si vedrebbe subito, perche' li' si stampa davvero.
     * ===================================================================== */
    vga_scegli_crtc();

    misc   = (unsigned int)ioport_in(VGA_MISC_R);
    stato1 = (unsigned int)ioport_in(0x3DA);
    stato0 = (unsigned int)ioport_in(0x3C2);

    /* Il lucchetto. Si annota il valore di prima per rimetterlo, e quello
     * che si rilegge dopo: una SiS sbloccata risponde 0xA1, e se rispondesse
     * altro vorrebbe dire che questa non e' la porta giusta. */
    ioport_out(VGA_SEQ_IDX, 0x05);
    sr05_prima = (unsigned char)ioport_in(VGA_SEQ_DAT);
    ioport_out(VGA_SEQ_IDX, 0x05);
    ioport_out(VGA_SEQ_DAT, 0x86);
    ioport_out(VGA_SEQ_IDX, 0x05);
    sr05_dopo = (unsigned char)ioport_in(VGA_SEQ_DAT);

    banco_indicizzato(VGA_SEQ_IDX, VGA_SEQ_DAT, sr_a, 64);
    banco_indicizzato(CRTC_IDX,    CRTC_DAT,    cr_a, 256);
    banco_indicizzato(VGA_GC_IDX,  VGA_GC_DAT,  gr_a, 16);
    banco_attributo(ar_a, 32);

    banco_indicizzato(VGA_SEQ_IDX, VGA_SEQ_DAT, sr_b, 64);
    banco_indicizzato(CRTC_IDX,    CRTC_DAT,    cr_b, 256);
    banco_indicizzato(VGA_GC_IDX,  VGA_GC_DAT,  gr_b, 16);
    banco_attributo(ar_b, 32);

    /* La tavolozza. In modo testo sono i 16 colori del DOS ripetuti fino a
     * 256; in grafica a 8 bit e' quella vera. Sono 48 righe di referto: a
     * schermo, dove le righe si fotografano una per una, non ci vanno. */
    if (!g_schermo) {
        ioport_out(VGA_DAC_RIDX, 0);
        for (i = 0; i < 768; i++) dac[i] = (unsigned char)ioport_in(VGA_DAC_DAT);
    }

    /* Il lucchetto com'era. Lasciare una scheda sbloccata non fa danno, ma
     * il referto dev'essere la fotografia di una macchina normale, non di
     * una macchina che e' appena stata guardata. */
    ioport_out(VGA_SEQ_IDX, 0x05);
    ioport_out(VGA_SEQ_DAT, sr05_prima);

    /* --- da qui in poi si stampa, e le letture sono finite --------------- */

    emetti("\n");
    emetti("=============================================================\n");
    emetti("[VGA] i registri, come il BIOS li ha lasciati\n");
    emetti("=============================================================\n");
    emetti("Misc Output (0x3CC): %02x    CRTC su 0x%03x\n", misc, CRTC_IDX);
    emetti("Stato 1 (0x3DA): %02x   Stato 0 (0x3C2): %02x\n", stato1, stato0);

    emetti("SR05 prima dello sblocco: %02x   dopo: %02x", sr05_prima, sr05_dopo);
    if (sr05_dopo == 0xA1) emetti("   (0xA1 = SiS sbloccata)\n");
    else                   emetti("   ! non e' 0xA1: registri estesi forse muti\n");

    emetti("\n! SR 00-3f: 00-04 sono VGA standard, da 05 in su sono SiS\n");
    emetti_banco("SR", sr_a, 64);
    emetti("\n! CR 00-18 sono VGA standard, da 19 in su sono SiS\n");
    emetti_banco("CR", cr_a, 256);
    emetti("\n");
    emetti_banco("GR", gr_a, 16);
    emetti("\n");
    emetti_banco("AR", ar_a, 32);

    emetti("\n");
    emetti_differenze("SR", sr_a, sr_b, 64);
    emetti_differenze("CR", cr_a, cr_b, 256);
    emetti_differenze("GR", gr_a, gr_b, 16);
    emetti_differenze("AR", ar_a, ar_b, 32);

    if (!g_schermo) {
        emetti("\n! DAC: 256 voci da tre byte (r,g,b), 6 bit per componente\n");
        emetti_banco("DAC", dac, 768);
    }
}

/* -----------------------------------------------------------------------------
 * La finestra a porte rilocata della scheda video
 * --------------------------------------------------------------------------- */

static void dump_finestra_rilocata(void)
{
    unsigned char v[128];
    unsigned int  i;

    emetti("\n");
    emetti("=============================================================\n");
    emetti("[RILOCATA] la finestra a porte della scheda video\n");
    emetti("=============================================================\n");

    if (g_vga_io == 0) {
        emetti("nessuna BAR a porte sulla scheda video: niente da leggere.\n");
        return;
    }

    if (ioport_bind(g_vga_io, 128) != 0) {
        emetti("base 0x%04x: ioport_bind fallita, saltata.\n", g_vga_io);
        return;
    }

    emetti("base 0x%04x, 128 byte letti cosi' come sono.\n", g_vga_io);
    emetti("! su una SiS 315 qui dentro ci sono i registri del ponte video:\n");
    emetti("  Part1 a +04, Part2 a +10, Part3 a +12, Part4 a +14, Part5 a +16,\n");
    emetti("  e da +30 in poi una seconda copia delle porte VGA. Sono indici\n");
    emetti("  e dati: questa lettura grezza mostra solo l'indice corrente,\n");
    emetti("  i banchi interi li tira fuori -ponte.\n");

    for (i = 0; i < 128; i++) v[i] = (unsigned char)ioport_in(g_vga_io + i);
    emetti_banco("REL", v, 128);
}

/* I banchi del ponte video, e SI FA PER ULTIMO E SOLO SE LO CHIEDI.
 *
 * ! PERCHE' NON E' ACCESO DI SUO. Per leggere un banco indicizzato bisogna
 * SCRIVERE l'indice, e qui la mappa di quali offset siano indici e quali dati
 * viene dal driver di Linux, non da un documento SiS. Se un offset non fosse
 * un indice, quella scrittura andrebbe a finire in un registro vero — su una
 * macchina senza seriale, con lo schermo come unico testimone.
 *
 * Percio' il referto normale non lo fa. Si lancia con -ponte DOPO che il
 * primo referto e' gia' salvato e al sicuro. */
static void dump_ponte(void)
{
    static const struct { unsigned int off; const char *nome; } parti[] = {
        { 0x04, "P1" }, { 0x10, "P2" }, { 0x12, "P3" },
        { 0x14, "P4" }, { 0x16, "P5" }
    };
    unsigned char v[128];
    unsigned int  p, i;

    emetti("\n");
    emetti("=============================================================\n");
    emetti("[PONTE] i banchi Part1..Part5, indice per indice\n");
    emetti("=============================================================\n");

    if (g_vga_io == 0) {
        emetti("nessuna finestra a porte: niente ponte da leggere.\n");
        return;
    }
    emetti("! offset presunti (da sisfb), non da un documento SiS.\n");

    /* ! SI SVUOTA IL BUFFER PRIMA DI TOCCARE IL PONTE, e di nuovo dopo ogni
     * banco. Quello che viene dopo e' l'unico punto del referto in cui si
     * SCRIVE su registri di cui non abbiamo il documento: se una di quelle
     * scritture ferma la macchina, tutto cio' che era gia' stato letto deve
     * essere gia' sul dischetto. Costa cinque write() in piu' e salva un
     * referto intero. */
    svuota();

    for (p = 0; p < sizeof(parti) / sizeof(parti[0]); p++) {
        unsigned int off = parti[p].off;
        unsigned char a, b, c;

        /* ! PRIMA SI CHIEDE SE E' UN BANCO, E COSTA TRE SCRITTURE INVECE DI
         * CENTOVENTOTTO.
         *
         * Un banco indicizzato risponde diversamente a indici diversi. Se a
         * tre indici lontani rende sempre lo stesso byte, quella coppia non e'
         * indice/dato: e' altro, e scriverci dentro centoventotto numeri
         * significa infilarli in registri veri, alla cieca.
         *
         * Su questa scheda e' il caso di Part5: 128 indici, 128 volte 06.
         * Ci abbiamo scritto sopra, e ha spostato lo schermo in un modo che
         * nessun altro registro spiegava. Meglio saltare un banco che non
         * c'e' che leggerne uno che non esiste. */
        ioport_out(g_vga_io + off, 0x00);
        a = (unsigned char)ioport_in(g_vga_io + off + 1);
        ioport_out(g_vga_io + off, 0x01);
        b = (unsigned char)ioport_in(g_vga_io + off + 1);
        ioport_out(g_vga_io + off, 0x30);
        c = (unsigned char)ioport_in(g_vga_io + off + 1);

        if (a == b && b == c) {
            emetti("\n");
            emetti("%s: si rilegge sempre %02x - non e' un banco "
                   "indicizzato, saltato.\n", parti[p].nome, a);
            svuota();
            continue;
        }

        for (i = 0; i < 128; i++) {
            ioport_out(g_vga_io + off, i);
            v[i] = (unsigned char)ioport_in(g_vga_io + off + 1);
        }
        emetti("\n");
        emetti_banco(parti[p].nome, v, 128);
        svuota();
    }
}

/* =============================================================================
 * I REGISTRI IN MEMORIA, E IL MOTORE 2D — si fa solo se lo chiedi
 *
 * ! PERCHE' NON E' ACCESO DI SUO. Qui non si scrive niente, ma si LEGGE una
 * finestra di cui non abbiamo il documento, e una lettura non e' sempre senza
 * conseguenze: certi registri sono code, e leggerli le fa avanzare. Il
 * referto normale non ci mette piede.
 *
 * ! COSA CI ASPETTIAMO DI TROVARE. Su una SiS della serie 315 il motore 2D
 * sta a +0x8200 dentro questa finestra: base e passo della sorgente e della
 * destinazione, rettangolo, colore, operazione, comando, e uno stato che dice
 * se il motore e' fermo. E' quello che serve per far riempire un rettangolo
 * alla scheda invece che alla CPU.
 *
 * ! E CHE COSA SIGNIFICA UN BLOCCO DI ff. Vuol dire che la finestra non
 * decodifica: BAR sbagliata, o scheda spenta. Un blocco di 00 vuol dire che
 * decodifica e il motore e' a riposo. Sono le due risposte che distinguono
 * «non c'e'» da «c'e' e non fa niente», ed e' l'unica cosa che questo
 * capitolo deve stabilire.
 * ========================================================================== */
/* Il registro che accende i moduli della scheda.
 *
 * ! IL NUMERO VIENE DAL DRIVER DI LINUX, NON DA UN DOCUMENTO SiS. In sisfb
 * l'indice 0x1E del sequenziatore si chiama IND_SIS_MODULE_ENABLE e il bit
 * 0x40 SIS_2D_ENABLE. Su questa macchina SR1E si legge 20: il bit non c'e',
 * cioe' il motore 2D e' spento — ed e' per questo che la finestra dei
 * registri in memoria si rilegge tutta ff. */
#define SR_MODULI     0x1E
#define MODULO_2D     0x40

static unsigned char sr_leggi(unsigned int i)
{
    ioport_out(VGA_SEQ_IDX, i);
    return (unsigned char)ioport_in(VGA_SEQ_DAT);
}

static void sr_scrivi(unsigned int i, unsigned int v)
{
    ioport_out(VGA_SEQ_IDX, i);
    ioport_out(VGA_SEQ_DAT, v);
}

static void dump_mmio(int accendi)
{
    static const struct { unsigned int off; const char *che; } zone[] = {
        { 0x0000, "generali"            },
        { 0x8200, "motore 2D"           },
        { 0x8280, "motore 2D (seguito)" },
        { 0x8500, "coda comandi"        },
    };
    MmioZona     m;
    volatile unsigned int *reg;
    unsigned char v[64];
    unsigned int z, i, d;
    unsigned char sr1e_prima, sr1e_dopo, sr05_prima;

    emetti("\n");
    emetti("=============================================================\n");
    emetti("[MMIO] i registri in memoria della scheda video\n");
    emetti("=============================================================\n");

    if (g_vga_mmio == 0) {
        emetti("nessuna BAR di memoria non prefetchable: niente da leggere.\n");
        return;
    }

    /* ! IL LUCCHETTO VA RIAPERTO QUI. dump_vga() lo RIMETTE COM'ERA quando ha
     * finito — e fa bene, una sonda non lascia la macchina diversa da come
     * l'ha trovata — ma da bloccato SR1E si rilegge ff. La prima volta l'ho
     * letto cosi': ff, «motore acceso», e la conclusione era esattamente
     * rovesciata. Il banco letto da sbloccato nello stesso referto diceva 20. */
    sr05_prima = sr_leggi(0x05);
    sr_scrivi(0x05, 0x86);

    sr1e_prima = sr_leggi(SR_MODULI);
    emetti("SR1E (moduli) = %02x: motore 2D %s\n", sr1e_prima,
           (sr1e_prima & MODULO_2D) ? "ACCESO" : "spento");

    if (accendi) {
        /* ! ff NON E' UN VALORE, E' UN NON-RISPOSTA, e non ci si scrive sopra.
         * sr1e_prima | 0x40 con sr1e_prima a ff vuol dire scrivere ff, cioe'
         * accendere OGNI modulo della scheda alla cieca. E' quello che questa
         * sonda ha fatto la prima volta. Se il registro non risponde, la cosa
         * giusta e' dirlo e non toccare niente. */
        if (sr1e_prima == 0xFF) {
            emetti("SR1E si rilegge ff anche da sbloccato: non ci scrivo.\n");
        } else {
            /* Accendere un modulo non cambia la modalita' video: non tocca
             * temporizzazioni, non tocca il pannello. Ma il nome di questo
             * registro ce l'ha dato un altro driver, non un documento. */
            sr_scrivi(SR_MODULI, sr1e_prima | MODULO_2D);
            sr1e_dopo = sr_leggi(SR_MODULI);
            emetti("acceso il bit %02x: SR1E adesso %02x%s\n", MODULO_2D,
                   sr1e_dopo,
                   (sr1e_dopo & MODULO_2D) ? "" : "  (NON ha attecchito!)");
        }
    }

    m.fisico = g_vga_mmio;
    m.byte   = 0x10000;          /* 64 KB: il motore 2D ci sta dentro */
    m.virt   = 0;

    if (mmio_map(&m) != 0) {
        emetti("base 0x%08x: mmio_map fallita.\n", g_vga_mmio);
        sr_scrivi(0x05, sr05_prima);
        return;
    }

    emetti("base 0x%08x, mappata a 0x%08x, 64 KB.\n", g_vga_mmio, m.virt);
    emetti("! tutto ff = la finestra non decodifica; tutto 00 = decodifica\n");
    emetti("  e il motore e' a riposo.\n");

    /* ! SI SVUOTA PRIMA DI LEGGERE, come per il ponte: se una di queste
     * letture ferma la macchina, il referto dev'essere gia' sul dischetto. */
    svuota();

    reg = (volatile unsigned int *)m.virt;

    for (z = 0; z < sizeof(zone) / sizeof(zone[0]); z++) {
        char nome[24];

        /* 16 registri da 32 bit per zona, letti in byte per stamparli con lo
         * stesso emettitore di tutti gli altri banchi. */
        for (i = 0; i < 16; i++) {
            d = reg[(zone[z].off >> 2) + i];
            v[i * 4]     = (unsigned char)(d & 0xFF);
            v[i * 4 + 1] = (unsigned char)((d >> 8) & 0xFF);
            v[i * 4 + 2] = (unsigned char)((d >> 16) & 0xFF);
            v[i * 4 + 3] = (unsigned char)((d >> 24) & 0xFF);
        }

        emetti("\n");
        emetti("--- +0x%04x  %s\n", zone[z].off, zone[z].che);
        snprintf(nome, sizeof(nome), "M%04x", zone[z].off);
        emetti_banco(nome, v, 64);
        svuota();
    }

    /* =========================================================================
     * IL SETACCIO: 64 KB interi, alla ricerca di un solo dword che non sia ff
     *
     * ! QUATTRO FINESTRE DA SESSANTAQUATTRO BYTE NON SONO UNA RISPOSTA. Se il
     * motore 2D non fosse a +0x8200 — l'offset ce l'ha dato sisfb, non un
     * documento SiS — quattro assaggi di ff direbbero «l'apertura e' morta»
     * quando la verita' e' «abbiamo guardato nel posto sbagliato». Sono due
     * conclusioni opposte a partire dallo stesso dato.
     *
     * Sedicimilatrecentottantaquattro letture costano meno di un battito di
     * ciglia e tolgono di mezzo la differenza: se in tutti i 64 KB non c'e' un
     * dword diverso da ffffffff, l'apertura non decodifica e non c'e' niente
     * da cercare. Se invece qualcosa risponde, il referto dice DOVE.
     * ===================================================================== */
    {
        unsigned int vivi = 0, mostrati = 0, blocchi = 0, b;
        unsigned int primo_di_blocco;

        emetti("\n");
        emetti("--- setaccio: tutti i 64 KB, in cerca di qualcosa che non sia ff\n");
        svuota();

        for (b = 0; b < 16; b++) {          /* 16 blocchi da 4 KB */
            primo_di_blocco = 0;

            for (i = 0; i < 1024; i++) {
                d = reg[b * 1024 + i];
                if (d == 0xFFFFFFFFu) continue;

                vivi++;
                if (!primo_di_blocco) { blocchi++; primo_di_blocco = 1; }

                if (mostrati < 24) {
                    emetti("  +0x%04x = %08x\n", (b * 1024 + i) * 4, d);
                    mostrati++;
                }
            }
            svuota();
        }

        if (vivi == 0)
            emetti("  16384 dword su 16384 sono ffffffff: l'apertura non\n"
                   "  decodifica, e non e' questione di offset sbagliato.\n");
        else
            emetti("  %u dword su 16384 rispondono, in %u blocchi da 4 KB su 16.\n",
                   vivi, blocchi);
        svuota();
    }

    /* =========================================================================
     * IL CONTROLLO: si mappa il FRAMEBUFFER e si guarda se si legge.
     *
     * ! SENZA QUESTO, UN BLOCCO DI ff NON DICE NIENTE. Puo' voler dire che la
     * finestra dei registri non decodifica — la cosa che si vuole sapere — ma
     * anche che mmio_map() non ha mappato quello che credevamo, e allora
     * l'intero capitolo sopra e' aria fritta.
     *
     * Il framebuffer e' l'unica finestra della scheda di cui sappiamo per
     * certo che risponde: e' quella in cui il sistema sta disegnando in questo
     * momento. Se questa si legge e quella no, la differenza e' nella scheda e
     * non in noi.
     * ===================================================================== */
    emetti("\n");
    emetti("--- controllo: il framebuffer, che sappiamo rispondere\n");

    if (g_vga_fb == 0) {
        emetti("nessuna BAR prefetchable: controllo non fatto.\n");
        sr_scrivi(0x05, sr05_prima);
        return;
    }

    m.fisico = g_vga_fb;
    m.byte   = 0x1000;
    m.virt   = 0;

    if (mmio_map(&m) != 0) {
        emetti("base 0x%08x: mmio_map fallita.\n", g_vga_fb);
        sr_scrivi(0x05, sr05_prima);
        return;
    }

    reg = (volatile unsigned int *)m.virt;
    for (i = 0; i < 16; i++) {
        d = reg[i];
        v[i * 4]     = (unsigned char)(d & 0xFF);
        v[i * 4 + 1] = (unsigned char)((d >> 8) & 0xFF);
        v[i * 4 + 2] = (unsigned char)((d >> 16) & 0xFF);
        v[i * 4 + 3] = (unsigned char)((d >> 24) & 0xFF);
    }

    emetti("base 0x%08x, mappata a 0x%08x.\n", g_vga_fb, m.virt);
    emetti_banco("FB", v, 64);

    /* ! LEGGERE ZERI NON DIMOSTRA NIENTE, e la prima volta ho preso quaranta
     * righe di 00 per una prova che mmio_map funzionava. Lo zero e' anche
     * quello che si legge da una pagina nuova che non e' mappata dove
     * crediamo, ed e' anche il colore del primo pixel di uno schermo con lo
     * sfondo scuro: tre spiegazioni per lo stesso dato.
     *
     * Si scrive e si rilegge. Un valore che torna indietro e' memoria vera,
     * e nessuna delle altre due spiegazioni lo produce. Poi si rimette quello
     * che c'era: e' un pixel dello schermo, e una sonda non lascia segni. */
    {
        unsigned int orig  = reg[0];
        unsigned int rilet;

        reg[0] = 0xA5C31E7Fu;
        rilet  = reg[0];
        reg[0] = orig;

        emetti("prova scrittura: scritto a5c31e7f, riletto %08x  -> %s\n",
               rilet,
               (rilet == 0xA5C31E7Fu) ? "mmio_map funziona, la finestra dei"
                                        " registri e' spenta dalla scheda"
                                      : "NON e' memoria della scheda: il"
                                        " guasto e' in mmio_map");
    }

    svuota();

    /* Il lucchetto si rimette com'era: dump_vga() lo fa, e questo capitolo
     * non deve essere quello che se ne dimentica. */
    sr_scrivi(0x05, sr05_prima);
}

/* =============================================================================
 * LA ROM DEL BIOS VIDEO — il pezzo che sa gia' fare tutto
 *
 * ! E' IL BERSAGLIO GIUSTO PER IL MODESET, e ci sono voluti dieci referti per
 * arrivarci. Dentro questa ROM ci sono le tabelle di modalita' di QUESTA
 * scheda e il codice che le applica: e' quello che Stage 2 chiama con INT 10h
 * all'accensione, ed e' l'unica cosa su questa macchina che il pannello lo sa
 * pilotare per intero. Sottrarre due referti dice COSA cambia fra due
 * modalita'; qui dentro c'e' scritto PERCHE'.
 *
 * Sono trentadue o sessantaquattro kilobyte — la lunghezza la dichiara la ROM
 * stessa nel terzo byte, in blocchi da 512 — contro i megabyte di un driver
 * Windows, e sono codice a 16 bit che ndisasm disassembla senza storie.
 *
 * ! SI SCRIVE GREZZA, NON IN ESADECIMALE. Sessantaquattro kilobyte in
 * esadecimale sono quasi trecento kilobyte di testo e un dischetto pieno: qui
 * il file e' esattamente la ROM, byte per byte, e tools/scava.py lo legge cosi'
 * com'e'.
 *
 * ! E LEGGERE UNA ROM NON HA EFFETTI COLLATERALI. E' l'unica cosa in questa
 * sonda di cui si possa dirlo con certezza: e' memoria di sola lettura, e
 * nessuno dei suoi byte e' un registro.
 * ========================================================================== */
static int dump_rom(const char *perc)
{
    MmioZona       m;
    volatile unsigned char *rom;
    unsigned char  intestazione[3];
    unsigned int   blocchi, byte, i, scritti = 0;
    static unsigned char pezzo[4096];
    int            fd;

    m.fisico = 0xC0000;
    m.byte   = 0x10000;          /* 64 KB: la ROM video non e' mai piu' lunga */
    m.virt   = 0;

    if (mmio_map(&m) != 0) {
        printf("sonda: mmio_map(0xC0000) fallita: la ROM non si legge.\n");
        return 1;
    }

    rom = (volatile unsigned char *)m.virt;

    for (i = 0; i < 3; i++) intestazione[i] = rom[i];

    /* ! LA FIRMA SI CONTROLLA PRIMA DI SCRIVERE 64 KB. 55 AA e' l'inizio di
     * ogni ROM di espansione dal 1981: se non c'e', quello che sta a 0xC0000
     * non e' una ROM video, e salvarlo vorrebbe dire riempire il dischetto di
     * qualcos'altro. */
    if (intestazione[0] != 0x55 || intestazione[1] != 0xAA) {
        printf("sonda: a 0xC0000 c'e' %02x %02x invece di 55 aa:\n",
               intestazione[0], intestazione[1]);
        printf("       non e' una ROM di espansione. Non scrivo niente.\n");
        return 1;
    }

    blocchi = intestazione[2];
    byte    = blocchi * 512;

    if (byte == 0 || byte > 0x10000) {
        printf("sonda: la ROM dichiara %u blocchi (%u byte): non ha senso.\n",
               blocchi, byte);
        return 1;
    }

    fd = open(perc, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("sonda: non riesco a creare %s (%d)\n", perc, fd);
        return 1;
    }

    printf("sonda: ROM video, %u blocchi = %u byte. Scrivo %s ...\n",
           blocchi, byte, perc);

    /* Si copia in un pezzo normale prima di scrivere: write() vuole memoria
     * sua, e questa e' una finestra mappata su una ROM. */
    while (scritti < byte) {
        unsigned int q = byte - scritti;

        if (q > sizeof(pezzo)) q = sizeof(pezzo);
        for (i = 0; i < q; i++) pezzo[i] = rom[scritti + i];

        if (write(fd, pezzo, q) != (ssize_t)q) {
            printf("sonda: la scrittura si e' fermata a %u byte.\n", scritti);
            printf("       dischetto pieno, o protetto in scrittura?\n");
            close(fd);
            return 1;
        }
        scritti += q;
    }

    close(fd);
    printf("sonda: fatto. %s e' la ROM, byte per byte (%u).\n", perc, byte);
    printf("       Da leggere con tools/scava.py su Linux.\n");
    return 0;
}

/* -----------------------------------------------------------------------------
 * L'intestazione: chi ha scritto il referto, e in quale stato era la macchina
 * --------------------------------------------------------------------------- */

static void dump_sistema(void)
{
    VideoInfo v;
    char      s[256];

    emetti("=============================================================\n");
    emetti("REFERTO DI sonda.drv - EX-OS\n");
    emetti("=============================================================\n");

    if (osversion(s, sizeof(s)) >= 0) emetti("%s\n", s);

    if (video_info(&v) == 0) {
        if (v.fisico == 0) {
            emetti("video: MODO TESTO (nessun framebuffer)\n");
        } else {
            emetti("video: framebuffer 0x%08x  %ux%u  %u bit  passo %u\n",
                   v.fisico, v.larghezza, v.altezza, v.bit, v.passo);
        }
    } else {
        emetti("video: video_info() ha risposto no\n");
    }

    emetti("\n! DUE REFERTI, NON UNO. Questo e' lo stato in cui il BIOS ha\n");
    emetti("  lasciato la scheda per QUESTA modalita'. Prendine un altro\n");
    emetti("  nell'altra (svga.drv testo / svga.drv 800x600, poi riavvio):\n");
    emetti("  la differenza fra i due dice come si passa da una\n");
    emetti("  all'altra, ed e' quella che serve per il modeset nativo.\n");
}

/* -----------------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------------- */

static void aiuto(void)
{
    printf("sonda.drv - scrive su file tutto quel che serve per scrivere\n");
    printf("            un driver per questa macchina.\n\n");
    printf("  sonda.drv [-auto] [-f] [-ponte] [-mmio] [-motore]\n");
    printf("            [-schermo] [file]\n\n");
    printf("  file      dove scrivere (predefinito %s). Nome in 8.3:\n",
           FILE_PRED);
    printf("            su FAT i nomi lunghi sono in sola lettura.\n");
    printf("  -auto     sceglie il primo nome libero fra /SONDA1.TXT e\n");
    printf("            /SONDA9.TXT: un referto per avvio, senza\n");
    printf("            cancellare quelli di prima.\n");
    printf("  -f        sovrascrive un file che c'e' gia'\n");
    printf("  -schermo  non scrive niente: stampa l'essenziale a video,\n");
    printf("            una schermata ogni venti secondi, da fotografare.\n");
    printf("            Serve dove non c'e' un supporto scrivibile -\n");
    printf("            avviando dal CD, per esempio.\n");
    printf("  -ponte    aggiunge i banchi del ponte video. Scrive su\n");
    printf("            registri di cui non abbiamo il documento: si fa\n");
    printf("            DOPO aver messo al sicuro il primo referto.\n");
    printf("  -rom F    salva in F la ROM del BIOS video, grezza. E'\n");
    printf("            trentadue o sessantaquattro kilobyte, e dentro ci\n");
    printf("            sono le tabelle di modalita' di QUESTA scheda: si\n");
    printf("            legge con tools/scava.py su Linux.\n");
    printf("  -mmio     aggiunge i registri in memoria, motore 2D compreso.\n");
    printf("            Non scrive niente, ma legge una finestra di cui non\n");
    printf("            abbiamo il documento: stessa prudenza.\n");
    printf("  -motore   come -mmio, ma prima ACCENDE il modulo 2D (SR1E bit\n");
    printf("            40). Serve perche' da spento quella finestra si\n");
    printf("            rilegge tutta ff. Non cambia la modalita' video.\n");
}

int main(int argc, char **argv)
{
    const char  *perc = FILE_PRED;
    char         autonome[16];
    int          forza = 0, ponte = 0, mmio = 0, motore = 0, rom = 0;
    int          autom = 0, i;
    struct stat  st;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            /* ! `hwconfig -d` sonda ogni *.drv del catalogo con -i, e chi
             * risponde 0 si ritrova installato su ogni macchina. Questa non
             * e' un driver: non guida niente, si lancia a mano, e su un
             * sistema installato non ci deve stare. Stessa risposta di
             * vgaprova.drv, per la stessa ragione. */
            printf("sonda: non sono un driver, sono uno strumento da lanciare\n");
            printf("       a mano: non installarmi.\n");
            return 1;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            aiuto();
            return 0;
        }
        if (strcmp(argv[i], "-schermo") == 0) { g_schermo = 1; continue; }
        if (strcmp(argv[i], "-auto") == 0)  { autom = 1; continue; }
        if (strcmp(argv[i], "-f") == 0)     { forza = 1; continue; }
        if (strcmp(argv[i], "-ponte") == 0) { ponte = 1; continue; }
        if (strcmp(argv[i], "-mmio") == 0)  { mmio = 1; continue; }
        if (strcmp(argv[i], "-rom") == 0)   { rom = 1; continue; }
        if (strcmp(argv[i], "-motore") == 0) { mmio = 1; motore = 1; continue; }
        if (argv[i][0] == '-') {
            printf("sonda: opzione sconosciuta: %s\n", argv[i]);
            return 1;
        }
        perc = argv[i];
    }

    /* ! CON -schermo NON SI APRE NIENTE, e va deciso prima di guardare se il
     * file c'e' gia': su un CD la risposta e' sempre «non si puo' scrivere»,
     * e sarebbe un rifiuto per una cosa che non stiamo nemmeno chiedendo. */
    if (g_schermo) {
        if (ioport_bind(VGA_BASE, VGA_PORTE) != 0 ||
            ioport_bind(PCI_INDIRIZZO, 8) != 0) {
            printf("sonda: ioport_bind rifiutata.\n");
            printf("       mi chiamo *.drv e giro da root? il varco e' quello.\n");
            return 1;
        }

        dump_sistema();
        scandisci_pci();
        dump_vga();
        dump_finestra_rilocata();
        if (ponte) dump_ponte();
        if (mmio)  dump_mmio(motore);

        printf("\n-- fine del referto --\n");
        return 0;
    }

    /* ! UN REFERTO PER AVVIO, SULLO STESSO DISCHETTO. La sottrazione fra due
     * modalita' vuole due referti, e i due avvii usano lo stesso floppy:
     * senza un nome che avanza, il secondo avvio troverebbe occupato il nome
     * del primo e non scriverebbe niente — oppure, peggio, ci scriverebbe
     * sopra. Nove bastano: chi ne ha fatti nove ha altro da rivedere. */
    if (autom) {
        for (i = 1; i <= 9; i++) {
            snprintf(autonome, sizeof(autonome), "/SONDA%d.TXT", i);
            if (stat(autonome, &st) != 0) break;
        }
        if (i > 9) {
            printf("sonda: da /SONDA1.TXT a /SONDA9.TXT ci sono tutti.\n");
            printf("       cancellane qualcuno, o dimmi un nome tu.\n");
            return 1;
        }
        perc  = autonome;
        forza = 1;   /* il nome e' libero per costruzione: nessuno da salvare */
    }

    /* ! IL FILE CHE C'E' GIA' NON SI SOVRASCRIVE DA SOLO. Il VFS tronca
     * all'apertura: aprire il referto di ieri per scrivere quello di oggi lo
     * cancella PRIMA di sapere se questo giro andra' a buon fine. E qui il
     * file di ieri e' l'unica copia che esiste — la macchina che l'ha
     * prodotto e' quella che non ha una seriale. */
    if (!forza && stat(perc, &st) == 0) {
        printf("sonda: %s c'e' gia'. Scegli un altro nome, o -f per\n", perc);
        printf("       sovrascriverlo. Il referto di prima e' l'unica copia.\n");
        return 1;
    }

    g_fd = open(perc, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (g_fd < 0) {
        printf("sonda: non riesco a creare %s (%d)\n", perc, g_fd);
        printf("       il supporto e' scrivibile? dal CD non si scrive.\n");
        return 1;
    }

    if (ioport_bind(VGA_BASE, VGA_PORTE) != 0 ||
        ioport_bind(PCI_INDIRIZZO, 8) != 0) {
        printf("sonda: ioport_bind rifiutata.\n");
        printf("       mi chiamo *.drv e giro da root? il varco e' quello.\n");
        close(g_fd);
        return 1;
    }

    /* ! LA ROM SI PRENDE PRIMA DEL REFERTO. E' il pezzo piu' prezioso e il
     * meno rischioso: se il dischetto si riempie, meglio che si riempia dopo
     * aver salvato quella. */
    if (rom) {
        close(g_fd);
        g_fd = -1;
        return dump_rom(perc);
    }

    printf("sonda: scrivo %s ...\n", perc);

    /* Da qui in poi non si stampa piu' niente fino alla fine: ogni riga a
     * schermo muove il cursore, cioe' scrive sul CRTC, cioe' disturba
     * proprio i registri che stiamo leggendo. */
    dump_sistema();
    scandisci_pci();
    dump_vga();
    dump_finestra_rilocata();

    if (ponte) dump_ponte();
    if (mmio)  dump_mmio(motore);

    svuota();
    close(g_fd);

    if (g_guasto) {
        printf("sonda: la scrittura si e' interrotta - %s e' incompleto.\n",
               perc);
        printf("       dischetto pieno, o protetto in scrittura?\n");
        return 1;
    }

    printf("sonda: fatto. %s e' pronto: ora si puo' spegnere.\n", perc);
    return 0;
}

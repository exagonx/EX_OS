/* =============================================================================
 * drivers/mappa/mappa.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * IL RITRATTO DI UN DISPOSITIVO PCI, per chi deve scriverne il driver
 *
 *     mappa.drv                    elenca cosa c'e' sul bus
 *     mappa.drv 00:04.0 REL.TXT    il ritratto di quel dispositivo
 *     mappa.drv -tutto REL.TXT     il ritratto di tutti
 *
 * -----------------------------------------------------------------------------
 * ! CHE COSA C'ENTRA CON sonda.drv, E PERCHE' SONO DUE
 *
 * sonda.drv fotografa UNA MACCHINA: che chipset ha, come il BIOS ha lasciato
 * la scheda video, dove stanno le finestre. Serve una volta, quando una
 * macchina nuova arriva sul tavolo.
 *
 * Questo guarda UN DISPOSITIVO, e serve tutte le volte che se ne deve
 * scrivere il driver. Sono due domande diverse e due file diversi.
 *
 * -----------------------------------------------------------------------------
 * ! LE TRE REGOLE, E SONO COSTATE DIECI REFERTI
 *
 * Fra il 3 e il 10 settembre 2026, scrivendo i driver del portatile SiS, tre
 * errori si sono ripetuti abbastanza da meritare di stare dentro uno
 * strumento invece che dentro la testa di chi lo usa:
 *
 *   1. UNA LETTURA SOLA NON DICE NIENTE. Di un registro non si sa se il
 *      valore che rende e' un'impostazione o un contatore finche' non lo si
 *      e' letto piu' volte. Su quella scheda video trentatre registri
 *      cambiavano da soli, e per due volte sono finiti dentro una tabella di
 *      modeset come se contassero. Qui si legge tre volte e si dichiara chi
 *      non sta fermo.
 *
 *   2. ff NON E' UN VALORE, E' UN SILENZIO. Un dword di tutti uno e' quello
 *      che il bus rende quando NESSUNO risponde. Trattarlo come un dato porta
 *      a scriverci sopra: `letto | bit_da_accendere` con letto a ff vuol dire
 *      scrivere ff, cioe' accendere tutto alla cieca. Successo, su un
 *      registro che per fortuna era bloccato.
 *
 *   3. UN BANCO CHE SI RILEGGE COSTANTE NON E' UN BANCO. Se a indici diversi
 *      rende sempre lo stesso byte, quella coppia non e' indice/dato: e'
 *      altro, e scriverci dentro significa infilare numeri in registri veri.
 *      Successo anche questo, e ha spostato lo schermo in modo inutilizzabile.
 *
 * -----------------------------------------------------------------------------
 * ! COSA NON PUO' FARE, DETTO SUBITO
 *
 * Non tira fuori un driver, e non tira fuori il SIGNIFICATO di un registro.
 * Un dispositivo senza documentazione lo si capisce guardando cosa ci scrive
 * il driver del costruttore mentre funziona, e questo strumento non e' un
 * tracciatore: e' un fotografo. Quello che rende e' l'inventario di ogni cosa
 * che la macchina puo' dire da sola — dove il dispositivo risponde, con che
 * larghezza, quali bit accettano una scrittura, che cosa si muove da solo — e
 * per un dispositivo di CLASSE STANDARD (USB, IDE, audio, rete) quell'inventario
 * piu' la specifica pubblica bastano a scrivere il driver.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `mappa.drv -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("mappa.drv", "0.003");

#define PCI_INDIRIZZO 0xCF8
#define PCI_DATO      0xCFC
#define PCI_BUS_MAX   16

#define GIRI_PRED     3       /* quante letture per separare il rumore */
#define GIRI_MAX      8
#define IO_MAX        256     /* quanto spazio a porte si legge al massimo */
#define MMIO_MAX      0x10000 /* quanta memoria si mappa: 64 KB */

/* -----------------------------------------------------------------------------
 * Il file: si scrive a blocchi, non riga per riga. Stessa ragione di sonda.c:
 * un floppy scrive un settore per volta.
 * --------------------------------------------------------------------------- */
#define BUF_DIM       4096
static char          g_buf[BUF_DIM];
static unsigned int  g_usati = 0;
static int           g_fd = -1;
static int           g_guasto = 0;
static int           g_schermo = 0;

static void svuota(void)
{
    ssize_t n;

    if (g_schermo || g_usati == 0 || g_fd < 0 || g_guasto) return;

    n = write(g_fd, g_buf, g_usati);
    if (n != (ssize_t)g_usati) g_guasto = 1;
    g_usati = 0;
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

    if (g_schermo) { write(1, riga, (unsigned int)n); return; }

    if (g_usati + (unsigned int)n > BUF_DIM) svuota();
    memcpy(g_buf + g_usati, riga, (unsigned int)n);
    g_usati += (unsigned int)n;
}

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

/* =============================================================================
 * Lo spazio di configurazione
 * ========================================================================== */
static unsigned int cfg32(unsigned int bus, unsigned int slot, unsigned int fn,
                          unsigned int off)
{
    unsigned int ind = 0x80000000u | (bus << 16) | (slot << 11) |
                       (fn << 8) | (off & 0xFC);
    unsigned int val;

    if (ioport_out32(PCI_INDIRIZZO, ind) != 0) return 0xFFFFFFFFu;
    if (ioport_in32(PCI_DATO, &val) != 0)      return 0xFFFFFFFFu;
    return val;
}

static void cfg32_scrivi(unsigned int bus, unsigned int slot, unsigned int fn,
                         unsigned int off, unsigned int val)
{
    unsigned int ind = 0x80000000u | (bus << 16) | (slot << 11) |
                       (fn << 8) | (off & 0xFC);

    if (ioport_out32(PCI_INDIRIZZO, ind) != 0) return;
    ioport_out32(PCI_DATO, val);
}

/* Il nome della classe. Non e' cosmesi: dice se il dispositivo ha una
 * SPECIFICA PUBBLICA — e quindi se il driver si scrive leggendo un documento
 * invece che indovinando. */
static const char *nome_classe(unsigned int classe, unsigned int sotto,
                               unsigned int prog)
{
    switch (classe) {
    case 0x01:
        switch (sotto) {
        case 0x01: return "IDE (specifica pubblica)";
        case 0x06: return "SATA AHCI (specifica pubblica)";
        case 0x08: return "NVMe (specifica pubblica)";
        default:   return "controllore di dischi";
        }
    case 0x02: return (sotto == 0x00) ? "Ethernet" : "rete";
    case 0x03: return "video";
    case 0x04:
        switch (sotto) {
        case 0x01: return "audio compatibile AC97/SB";
        case 0x03: return "audio HD (specifica pubblica)";
        default:   return "multimedia";
        }
    case 0x06:
        switch (sotto) {
        case 0x00: return "ponte nord";
        case 0x01: return "ponte verso ISA/LPC";
        case 0x04: return "ponte PCI-PCI";
        case 0x07: return "ponte CardBus";
        default:   return "ponte";
        }
    case 0x07: return "porta seriale o parallela";
    case 0x0C:
        switch (sotto) {
        case 0x03:
            switch (prog) {
            case 0x00: return "USB UHCI (specifica pubblica)";
            case 0x10: return "USB OHCI (specifica pubblica)";
            case 0x20: return "USB EHCI (specifica pubblica)";
            case 0x30: return "USB xHCI (specifica pubblica)";
            default:   return "USB";
            }
        case 0x05: return "SMBus";
        default:   return "bus di servizio";
        }
    case 0x0D: return "radio (Wi-Fi, Bluetooth)";
    default:   return "";
    }
}

/* =============================================================================
 * L'elenco delle capacita': e' standard, e dice cose che nient'altro dice
 *
 * ! QUESTA E' LA PARTE CHE NON RICHIEDE DI INDOVINARE NIENTE. La lista delle
 * capacita' e' definita dalla specifica PCI: ogni voce ha un identificativo
 * assegnato, e da li' si sa se il dispositivo sa fare MSI (e quindi se il
 * driver puo' evitarsi la linea di interrupt condivisa), a che stati di
 * risparmio energetico risponde, se e' PCI Express e di che generazione.
 * ========================================================================== */
static const char *nome_capacita(unsigned int id)
{
    switch (id) {
    case 0x01: return "gestione energia";
    case 0x02: return "AGP";
    case 0x03: return "VPD (dati del costruttore)";
    case 0x05: return "MSI (interrupt senza linea condivisa)";
    case 0x07: return "PCI-X";
    case 0x09: return "capacita' del costruttore";
    case 0x0A: return "porta di debug";
    case 0x10: return "PCI Express";
    case 0x11: return "MSI-X";
    default:   return "sconosciuta";
    }
}

static void dump_capacita(unsigned int bus, unsigned int slot, unsigned int fn,
                          const unsigned char *cfg)
{
    unsigned int off, quante = 0;

    emetti("\n  capacita':\n");

    /* Il bit 4 dello stato dice se la lista esiste. Senza quel controllo si
     * finisce a seguire un puntatore che non e' un puntatore. */
    if ((cfg[0x06] & 0x10) == 0) {
        emetti("    nessuna (il bit 4 dello stato e' spento)\n");
        return;
    }

    off = cfg[0x34] & 0xFC;

    while (off >= 0x40 && off < 0x100 && quante < 32) {
        unsigned int v  = cfg32(bus, slot, fn, off);
        unsigned int id = v & 0xFF;

        emetti("    +0x%02x  id %02x  %s\n", off, id, nome_capacita(id));

        if (id == 0x01) {
            /* Gli stati di risparmio sono quattro bit dichiarati: dirli per
             * nome evita di doverli ricavare a mano ogni volta. */
            unsigned int pmc = (cfg32(bus, slot, fn, off) >> 16) & 0xFFFF;
            emetti("           versione %u, stati D1 %s, D2 %s\n",
                   pmc & 0x07,
                   (pmc & 0x0200) ? "si" : "no",
                   (pmc & 0x0400) ? "si" : "no");
        }

        off = (v >> 8) & 0xFC;
        quante++;
    }
}

/* =============================================================================
 * Le BAR: dove il dispositivo risponde, e QUANTO E' LARGA la finestra
 *
 * ! LA LARGHEZZA NON SI PUO' LEGGERE, SI MISURA. Nella BAR c'e' solo la base;
 * la dimensione si ricava scrivendoci tutti uno e guardando quanti bit bassi
 * restano a zero — sono quelli che il dispositivo non decodifica. E' la stessa
 * procedura che fa ogni BIOS all'accensione.
 *
 * ! E DURANTE LA MISURA IL DISPOSITIVO NON DEVE DECODIFICARE. Fra la scrittura
 * di tutti uno e il ripristino, quella BAR punta a un indirizzo che non e' il
 * suo: se il dispositivo rispondesse, risponderebbe al posto di qualcun altro.
 * Si spengono i due bit di decodifica nel registro comando e si rimettono dopo.
 *
 * ! PERCIO' NON E' ACCESA DI SUO, e sui ponti non si fa affatto: spegnere la
 * decodifica di un ponte PCI significa staccare per un istante tutto quello che
 * ci sta dietro, compreso — su una macchina qualunque — il disco da cui si sta
 * leggendo.
 * ========================================================================== */
static unsigned int misura_bar(unsigned int bus, unsigned int slot,
                               unsigned int fn, unsigned int i,
                               unsigned int orig)
{
    unsigned int off = 0x10 + i * 4;
    unsigned int cmd, maschera, dim;

    cmd = cfg32(bus, slot, fn, 0x04);
    cfg32_scrivi(bus, slot, fn, 0x04, cmd & ~0x03u);

    cfg32_scrivi(bus, slot, fn, off, 0xFFFFFFFFu);
    maschera = cfg32(bus, slot, fn, off);
    cfg32_scrivi(bus, slot, fn, off, orig);

    cfg32_scrivi(bus, slot, fn, 0x04, cmd);

    if (maschera == 0 || maschera == 0xFFFFFFFFu) return 0;

    maschera &= (orig & 1) ? 0xFFFFFFFCu : 0xFFFFFFF0u;
    dim = (~maschera) + 1;
    return dim;
}

/* =============================================================================
 * Lo spazio a porte, letto piu' volte
 *
 * ! SI LEGGE A BYTE, E VA DETTO PERCHE'. Un registro largo quattro byte letto
 * a byte rende gli stessi quattro byte su quasi tutti i dispositivi, ma non su
 * tutti: qualcuno pretende l'accesso della larghezza giusta e rende ff agli
 * altri. Un ff isolato in mezzo a valori sensati, qui dentro, e' piu'
 * probabilmente questo che un registro assente.
 *
 * ! E LEGGERE NON E' SEMPRE INNOCUO. Certi registri sono code: leggerli le fa
 * avanzare, e un dispositivo che stava lavorando perde un dato. Per questo lo
 * strumento e' un .drv che si lancia a mano su una macchina che si sta
 * studiando, non qualcosa che gira da solo all'avvio.
 * ========================================================================== */
static void dump_porte(unsigned int base, unsigned int dim, unsigned int giri)
{
    static unsigned char v[GIRI_MAX][IO_MAX];
    unsigned int quanti = (dim && dim < IO_MAX) ? dim : IO_MAX;
    unsigned int g, i, instabili = 0, muti = 0;

    if (ioport_bind(base, quanti) != 0) {
        emetti("    ioport_bind(0x%04x, %u) rifiutata: non letto.\n",
               base, quanti);
        return;
    }

    for (g = 0; g < giri; g++)
        for (i = 0; i < quanti; i++)
            v[g][i] = (unsigned char)ioport_in(base + i);

    emetti("    %u byte, letti %u volte:\n", quanti, giri);
    emetti_banco("     ", v[0], quanti);

    for (i = 0; i < quanti; i++) {
        int fermo = 1;

        for (g = 1; g < giri; g++) if (v[g][i] != v[0][i]) fermo = 0;
        if (!fermo) instabili++;
        if (v[0][i] == 0xFF) muti++;
    }

    /* ! I DUE NUMERI CHE CONTANO NON SONO I VALORI, SONO QUESTI. Quanti si
     * muovono da soli dice quanti sono contatori o stato — cioe' quanti NON
     * vanno riscritti da un driver che riproduce una configurazione. Quanti
     * sono ff dice quanto di quella finestra e' davvero implementato. */
    emetti("    fermi %u, si muovono da soli %u, a ff %u\n",
           quanti - instabili, instabili, muti);

    if (instabili) {
        emetti("    si muovono da soli:");
        for (i = 0; i < quanti; i++) {
            int fermo = 1;
            for (g = 1; g < giri; g++) if (v[g][i] != v[0][i]) fermo = 0;
            if (!fermo) emetti(" %02x", i);
        }
        emetti("\n");
    }
}

/* =============================================================================
 * Una finestra in memoria, letta piu' volte
 * ========================================================================== */
static void dump_memoria(unsigned int fisico, unsigned int dim,
                         unsigned int giri, int prefetch)
{
    MmioZona      m;
    volatile unsigned int *reg;
    unsigned int  quanti, g, i, vivi = 0, zeri = 0, instabili = 0;
    unsigned int  mostrati = 0;
    unsigned int  primo[MMIO_MAX / 4];
    unsigned int  fermo_di[MMIO_MAX / 4];

    if (prefetch) {
        /* ! UNA FINESTRA PREFETCHABLE NON E' FATTA DI REGISTRI. Il PCI permette
         * di dichiararla tale solo se leggerla non ha effetti collaterali e se
         * si puo' leggere in anticipo: e' memoria — un framebuffer, una ROM.
         * Setacciarla vorrebbe dire stampare pagine di pixel. */
        emetti("    prefetchable: e' memoria, non registri. Non la setaccio.\n");
        return;
    }

    quanti = (dim && dim < MMIO_MAX) ? dim : MMIO_MAX;
    quanti /= 4;

    m.fisico = fisico;
    m.byte   = quanti * 4;
    m.virt   = 0;

    if (mmio_map(&m) != 0) {
        emetti("    mmio_map(0x%08x, %u) fallita: non letta.\n",
               fisico, quanti * 4);
        return;
    }

    reg = (volatile unsigned int *)m.virt;

    for (i = 0; i < quanti; i++) { primo[i] = reg[i]; fermo_di[i] = 1; }

    for (g = 1; g < giri; g++)
        for (i = 0; i < quanti; i++)
            if (reg[i] != primo[i]) fermo_di[i] = 0;

    for (i = 0; i < quanti; i++) {
        if (primo[i] != 0xFFFFFFFFu) vivi++;
        if (primo[i] == 0) zeri++;
        if (!fermo_di[i]) instabili++;
    }

    emetti("    mappata a 0x%08x, %u dword, lette %u volte.\n",
           m.virt, quanti, giri);

    /* ! SE NON RISPONDE NIENTE SI DICE E BASTA. Stampare 16384 righe di
     * ffffffff riempie il dischetto e non aggiunge un'informazione a
     * «nessuno risponde». */
    if (vivi == 0) {
        emetti("    %u dword su %u sono ffffffff: questa finestra NON\n"
               "    decodifica. Non e' un offset sbagliato: non c'e' nessuno.\n",
               quanti, quanti);
        svuota();
        return;
    }

    emetti("    rispondono %u dword su %u, di cui %u a zero;"
           " si muovono da sole %u.\n", vivi, quanti, zeri, instabili);

    if (vivi == zeri) {
        /* ! TUTTO A ZERO E' UN'INFORMAZIONE, NON UN VUOTO. La finestra
         * decodifica — se non lo facesse si leggerebbe ff — e i registri sono
         * a riposo, o sono di sola scrittura. Sono due cose che un driver deve
         * sapere, ed e' l'opposto di «non c'e' niente». */
        emetti("    la finestra decodifica e ogni registro e' a zero: o il\n"
               "    dispositivo e' a riposo, o qui si scrive e non si legge.\n");
        svuota();
        return;
    }

    /* ! SI ELENCA CIO' CHE NON E' ZERO, e il perche' e' pratico: in
     * un'apertura da 64 KB la stragrande maggioranza dei dword e' zero perche'
     * quel registro non esiste o non e' stato ancora programmato. Stamparli
     * tutti vuol dire seppellire i venti che contano sotto sedicimila che non
     * dicono niente. Quelli che si muovono da soli si stampano comunque, anche
     * se sono zero adesso: che un registro cambi da solo e' un fatto, che in
     * questo istante valga zero e' un caso. */
    for (i = 0; i < quanti && mostrati < 192; i++) {
        if (primo[i] == 0xFFFFFFFFu) continue;
        if (primo[i] == 0 && fermo_di[i]) continue;

        emetti("      +0x%04x = %08x%s\n", i * 4, primo[i],
               fermo_di[i] ? "" : "   <- si muove da solo");
        mostrati++;
        if ((mostrati & 31) == 0) svuota();
    }

    if (mostrati >= 192)
        emetti("      (fermato a 192; gli zeri fermi non sono elencati)\n");
    else
        emetti("      (gli altri %u dword sono zero e stanno fermi)\n", zeri);

    svuota();
}

/* =============================================================================
 * Il ritratto di un dispositivo
 * ========================================================================== */
static void ritratto(unsigned int bus, unsigned int slot, unsigned int fn,
                     unsigned int giri, int misura)
{
    unsigned char cfg[256];
    unsigned int  i, v, classe, sotto, prog, testa;

    for (i = 0; i < 256; i += 4) {
        v = cfg32(bus, slot, fn, i);
        cfg[i]     = (unsigned char)(v & 0xFF);
        cfg[i + 1] = (unsigned char)((v >> 8) & 0xFF);
        cfg[i + 2] = (unsigned char)((v >> 16) & 0xFF);
        cfg[i + 3] = (unsigned char)((v >> 24) & 0xFF);
    }

    classe = cfg[0x0B];
    sotto  = cfg[0x0A];
    prog   = cfg[0x09];
    testa  = cfg[0x0E] & 0x7F;

    emetti("\n");
    emetti("=============================================================\n");
    emetti("%02x:%02x.%u  %02x%02x:%02x%02x  rev %02x  classe %02x%02x%02x  %s\n",
           bus, slot, fn, cfg[1], cfg[0], cfg[3], cfg[2], cfg[8],
           classe, sotto, prog, nome_classe(classe, sotto, prog));
    emetti("=============================================================\n");
    /* ! IL SOTTOSISTEMA NON STA SEMPRE A 0x2C. Ci sta sull'intestazione di
     * tipo 0, cioe' su un dispositivo normale; su un ponte CardBus — tipo 2 —
     * quell'offset e' un limite di memoria, e il sottosistema sta a 0x40.
     * Leggerlo dal posto sbagliato non da' errore: da' un numero, e un numero
     * sbagliato in un referto e' peggio di un buco. Su questo portatile
     * risultava 0000:0000 mentre il vero sottosistema e' 1025:0083, cioe'
     * Acer. */
    if (testa == 0x02)
        emetti("  sottosistema %02x%02x:%02x%02x\n",
               cfg[0x41], cfg[0x40], cfg[0x43], cfg[0x42]);
    else
        emetti("  sottosistema %02x%02x:%02x%02x\n",
               cfg[0x2D], cfg[0x2C], cfg[0x2F], cfg[0x2E]);
    emetti("  comando %02x%02x  stato %02x%02x\n",
           cfg[5], cfg[4], cfg[7], cfg[6]);
    emetti("    porte %s, memoria %s, bus master %s\n",
           (cfg[4] & 0x01) ? "attive" : "SPENTE",
           (cfg[4] & 0x02) ? "attiva" : "SPENTA",
           (cfg[4] & 0x04) ? "attivo" : "spento");

    /* ! LA LINEA DI INTERRUPT E IL PIEDINO SONO DUE COSE DIVERSE, e confonderle
     * costa un driver che non riceve mai niente. Il PIEDINO (0 = nessuno) dice
     * se il dispositivo sa interrompere; la LINEA e' dove il BIOS l'ha
     * instradato, ed e' quella che il driver chiede al kernel. */
    if (cfg[0x3D] == 0)
        emetti("  interrupt: nessun piedino, questo dispositivo non interrompe\n");
    else
        emetti("  interrupt: piedino INT%c, linea IRQ %u\n",
               'A' + cfg[0x3D] - 1, cfg[0x3C]);

    dump_capacita(bus, slot, fn, cfg);

    emetti("\n  configurazione, 256 byte:\n");
    emetti_banco("  cfg", cfg, 256);
    svuota();

    /* ! SU UN PONTE LE BAR NON SI GUARDANO COSI'. Dall'offset 0x18 in poi un
     * ponte non ha BAR: ha i numeri dei bus e le finestre che inoltra. E
     * misurarle spegnendogli la decodifica staccherebbe per un istante tutto
     * quello che ci sta dietro. */
    if (testa == 0x01) {
        emetti("\n  e' un ponte: bus %u -> %u..%u\n",
               cfg[0x18], cfg[0x19], cfg[0x1A]);
        emetti("  finestra memoria      0x%08x - 0x%08x\n",
               ((unsigned int)(cfg[0x21] << 8 | cfg[0x20]) & 0xFFF0u) << 16,
               (((unsigned int)(cfg[0x23] << 8 | cfg[0x22]) & 0xFFF0u) << 16)
                   | 0xFFFFFu);
        emetti("  finestra prefetchable 0x%08x - 0x%08x\n",
               ((unsigned int)(cfg[0x25] << 8 | cfg[0x24]) & 0xFFF0u) << 16,
               (((unsigned int)(cfg[0x27] << 8 | cfg[0x26]) & 0xFFF0u) << 16)
                   | 0xFFFFFu);
        emetti("  controllo ponte %02x%02x: VGA %s\n",
               cfg[0x3F], cfg[0x3E], (cfg[0x3E] & 0x08) ? "inoltrata" : "no");
        emetti("  ! le BAR di un ponte non si misurano: spegnergli la\n");
        emetti("    decodifica staccherebbe tutto quello che ci sta dietro.\n");
        svuota();
        return;
    }

    /* =========================================================================
     * ! UN PONTE CARDBUS NON HA BAR, e leggerle come se le avesse produce
     * finestre che non esistono. L'intestazione di tipo 2 usa quegli offset
     * per tutt'altro: 0x10 e' la base dei registri di socket, 0x18 sono i
     * numeri di bus, e da 0x1C in poi le finestre che il ponte inoltra alla
     * scheda infilata nello slot.
     *
     * Su questo portatile mappa.drv annunciava «BAR1 memoria 0x020000a0», che
     * e' il puntatore alle capacita' e lo stato secondario letti insieme come
     * se fossero un indirizzo.
     * ===================================================================== */
    if (testa == 0x02) {
        unsigned int socket = ((unsigned int)cfg[0x13] << 24) |
                              ((unsigned int)cfg[0x12] << 16) |
                              ((unsigned int)cfg[0x11] << 8) | cfg[0x10];

        emetti("\n  e' un ponte CardBus (lo slot PCMCIA):\n");
        emetti("    registri di socket a 0x%08x%s\n", socket & 0xFFFFFFF0u,
               (socket & 0xFFFFFFF0u) ? "" : "   ! NON ASSEGNATI");
        emetti("    bus  %u -> CardBus %u, fino a %u%s\n",
               cfg[0x18], cfg[0x19], cfg[0x1A],
               (cfg[0x19] == 0) ? "   ! NON ASSEGNATI" : "");
        emetti("    memoria 0  0x%02x%02x%02x%02x - 0x%02x%02x%02x%02x\n",
               cfg[0x1F], cfg[0x1E], cfg[0x1D], cfg[0x1C],
               cfg[0x23], cfg[0x22], cfg[0x21], cfg[0x20]);
        emetti("    memoria 1  0x%02x%02x%02x%02x - 0x%02x%02x%02x%02x\n",
               cfg[0x27], cfg[0x26], cfg[0x25], cfg[0x24],
               cfg[0x2B], cfg[0x2A], cfg[0x29], cfg[0x28]);
        emetti("    porte 0    0x%02x%02x%02x%02x - 0x%02x%02x%02x%02x\n",
               cfg[0x2F], cfg[0x2E], cfg[0x2D], cfg[0x2C],
               cfg[0x33], cfg[0x32], cfg[0x31], cfg[0x30]);
        emetti("    controllo ponte %02x%02x\n", cfg[0x3F], cfg[0x3E]);

        if ((cfg[4] & 0x03) == 0)
            emetti("  ! IL COMANDO E' A ZERO: questo ponte non decodifica\n"
                   "    niente. Il BIOS non l'ha configurato, e finche' resta\n"
                   "    cosi' quello che c'e' nello slot non si vede.\n");

        emetti("  ! le BAR di un ponte non si misurano: spegnergli la\n");
        emetti("    decodifica staccherebbe quello che ci sta dietro.\n");
        svuota();
        return;
    }

    emetti("\n  finestre:\n");

    for (i = 0; i < 6; i++) {
        unsigned int bar = ((unsigned int)cfg[0x10 + i * 4 + 3] << 24) |
                           ((unsigned int)cfg[0x10 + i * 4 + 2] << 16) |
                           ((unsigned int)cfg[0x10 + i * 4 + 1] << 8) |
                            (unsigned int)cfg[0x10 + i * 4];
        unsigned int dim = 0;

        if (bar == 0 || bar == 0xFFFFFFFFu) continue;

        if (misura) dim = misura_bar(bus, slot, fn, i, bar);

        if (bar & 1) {
            emetti("\n  BAR%u  porte 0x%04x", i, bar & 0xFFFC);
            if (dim) emetti("  (%u byte)", dim);
            emetti("\n");
            dump_porte(bar & 0xFFFC, dim, giri);
        } else {
            int pref = (bar & 0x08) ? 1 : 0;

            emetti("\n  BAR%u  memoria 0x%08x%s", i, bar & 0xFFFFFFF0u,
                   pref ? "  (prefetchable)" : "");
            if (dim) emetti("  (%u byte)", dim);
            emetti("\n");

            /* ! UNA BAR A 64 BIT OCCUPA DUE FESSURE, e la seconda contiene i
             * bit alti dell'indirizzo, non un'altra finestra. Stamparla come
             * finestra a se' e' un errore che si vede solo su hardware
             * recente, dove pero' si vede sempre. */
            if ((bar & 0x06) == 0x04) {
                emetti("    a 64 bit: la BAR%u qui sopra ne e' la meta' alta\n",
                       i + 1);
                i++;
            }

            dump_memoria(bar & 0xFFFFFFF0u, dim, giri, pref);
        }
        svuota();
    }
}

/* =============================================================================
 * L'elenco: una riga per dispositivo
 * ========================================================================== */
static int elenco(unsigned int giri, int misura, int tutto,
                  int b_v, int s_v, int f_v)
{
    unsigned int bus, slot, fn, v, hdr, classe, sotto, prog;
    int quanti = 0;

    for (bus = 0; bus < PCI_BUS_MAX; bus++) {
        for (slot = 0; slot < 32; slot++) {
            hdr = 0;

            for (fn = 0; fn < 8; fn++) {
                v = cfg32(bus, slot, fn, 0x00);
                if ((v & 0xFFFF) == 0xFFFF) {
                    if (fn == 0) break;
                    continue;
                }

                if (fn == 0) {
                    hdr = (cfg32(bus, slot, 0, 0x0C) >> 16) & 0xFF;
                    if ((hdr & 0x80) == 0) fn = 8;   /* una funzione sola */
                }

                quanti++;

                {
                    unsigned int c = cfg32(bus, slot,
                                           (fn == 8) ? 0 : fn, 0x08);
                    classe = (c >> 24) & 0xFF;
                    sotto  = (c >> 16) & 0xFF;
                    prog   = (c >> 8) & 0xFF;
                }

                if (tutto || (bus == (unsigned int)b_v &&
                              slot == (unsigned int)s_v &&
                              (fn == 8 ? 0u : fn) == (unsigned int)f_v)) {
                    ritratto(bus, slot, (fn == 8) ? 0 : fn, giri, misura);
                } else if (b_v < 0) {
                    emetti("%02x:%02x.%u  %04x:%04x  classe %02x%02x%02x  %s\n",
                           bus, slot, (fn == 8) ? 0 : fn,
                           v & 0xFFFF, (v >> 16) & 0xFFFF,
                           classe, sotto, prog,
                           nome_classe(classe, sotto, prog));
                }

                if (fn == 8) break;
            }
        }
    }

    return quanti;
}

/* =============================================================================
 * Argomenti
 * ========================================================================== */
static void aiuto(void)
{
    printf("mappa.drv - il ritratto di un dispositivo PCI, per chi ne deve\n");
    printf("            scrivere il driver.\n\n");
    printf("  mappa.drv                     elenca cosa c'e' sul bus\n");
    printf("  mappa.drv 00:04.0 [file]      il ritratto di quel dispositivo\n");
    printf("  mappa.drv -tutto [file]       il ritratto di tutti\n\n");
    printf("  -misura   misura la larghezza delle finestre. Per farlo spegne\n");
    printf("            un istante la decodifica del dispositivo: non si fa\n");
    printf("            sui ponti, e non si fa su cio' da cui si sta\n");
    printf("            leggendo.\n");
    printf("  -giri N   quante letture per separare cio' che sta fermo da\n");
    printf("            cio' che si muove da solo (predefinito %d).\n", GIRI_PRED);
    printf("  -f        sovrascrive un file che c'e' gia'\n\n");
    printf("! NON TIRA FUORI UN DRIVER, e nemmeno il significato di un\n");
    printf("  registro: e' un fotografo, non un tracciatore. Rende\n");
    printf("  l'inventario di cio' che la macchina puo' dire da sola. Per un\n");
    printf("  dispositivo di classe standard - USB, IDE, audio, rete - quello\n");
    printf("  piu' la specifica pubblica bastano a scrivere il driver.\n");
}

static int leggi_bdf(const char *s, int *b, int *sl, int *f)
{
    unsigned int v[3] = { 0, 0, 0 };
    int n = 0, cifre = 0;

    while (*s && n < 3) {
        if (*s >= '0' && *s <= '9')      { v[n] = v[n] * 16 + (unsigned)(*s - '0'); cifre++; }
        else if (*s >= 'a' && *s <= 'f') { v[n] = v[n] * 16 + (unsigned)(*s - 'a' + 10); cifre++; }
        else if (*s >= 'A' && *s <= 'F') { v[n] = v[n] * 16 + (unsigned)(*s - 'A' + 10); cifre++; }
        else if (*s == ':' || *s == '.') { if (!cifre) return -1; n++; cifre = 0; }
        else return -1;
        s++;
    }

    if (n != 2 || !cifre) return -1;

    *b  = (int)v[0];
    *sl = (int)v[1];
    *f  = (int)v[2];
    return 0;
}

int main(int argc, char **argv)
{
    const char *perc = NULL;
    int   giri = GIRI_PRED, misura = 0, tutto = 0, forza = 0, i;
    int   b_v = -1, s_v = 0, f_v = 0;
    struct stat st;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            /* ! hwconfig -d sonda ogni *.drv con -i, e chi risponde 0 si
             * ritrova installato dappertutto. Questo non guida niente: si
             * lancia a mano. Stessa risposta di sonda.drv. */
            printf("mappa: non sono un driver, sono uno strumento da lanciare\n");
            printf("       a mano: non installarmi.\n");
            return 1;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            aiuto();
            return 0;
        }
        if (strcmp(argv[i], "-tutto") == 0)  { tutto = 1; continue; }
        if (strcmp(argv[i], "-misura") == 0) { misura = 1; continue; }
        if (strcmp(argv[i], "-f") == 0)      { forza = 1; continue; }
        if (strcmp(argv[i], "-giri") == 0 && i + 1 < argc) {
            giri = atoi(argv[++i]);
            if (giri < 1) giri = 1;
            if (giri > GIRI_MAX) giri = GIRI_MAX;
            continue;
        }
        if (argv[i][0] == '-') {
            printf("mappa: non conosco '%s'. Prova -h.\n", argv[i]);
            return 1;
        }
        if (b_v < 0 && leggi_bdf(argv[i], &b_v, &s_v, &f_v) == 0) continue;
        perc = argv[i];
    }

    if (ioport_bind(PCI_INDIRIZZO, 8) != 0) {
        printf("mappa: ioport_bind sulle porte PCI rifiutata.\n");
        printf("       mi chiamo *.drv e giro da root? il varco e' quello.\n");
        return 1;
    }

    /* Senza un file si stampa: l'elenco sta in una schermata. */
    if (perc == NULL) {
        g_schermo = 1;
        if (b_v < 0 && !tutto)
            printf("mappa: %d dispositivi.\n", elenco(giri, misura, 0, -1, 0, 0));
        else
            elenco(giri, misura, tutto, b_v, s_v, f_v);
        return 0;
    }

    /* ! IL FILE CHE C'E' GIA' NON SI SOVRASCRIVE DA SOLO. Il VFS tronca
     * all'apertura: aprire il referto di ieri per scrivere quello di oggi lo
     * cancella PRIMA di sapere se questo giro andra' a buon fine, e su una
     * macchina che si sta studiando quello di ieri puo' essere l'unica copia. */
    if (!forza && stat(perc, &st) == 0) {
        printf("mappa: %s c'e' gia'. Scegli un altro nome, o -f.\n", perc);
        return 1;
    }

    g_fd = open(perc, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (g_fd < 0) {
        printf("mappa: non riesco a creare %s (%d)\n", perc, g_fd);
        printf("       il supporto e' scrivibile? dal CD non si scrive.\n");
        return 1;
    }

    printf("mappa: scrivo %s ...\n", perc);

    emetti("=============================================================\n");
    emetti("RITRATTO DI UN DISPOSITIVO - mappa.drv di EX-OS\n");
    emetti("=============================================================\n");
    emetti("! ogni finestra e' letta %d volte: cio' che cambia da solo e'\n",
           giri);
    emetti("  stato o contatore, e un driver che riproduce una\n");
    emetti("  configurazione NON deve riscriverlo.\n");
    emetti("! ffffffff non e' un valore, e' il silenzio del bus: vuol dire\n");
    emetti("  che a quell'indirizzo non risponde nessuno.\n");
    if (!misura)
        emetti("! le finestre non sono state misurate (serve -misura).\n");

    elenco(giri, misura, tutto, b_v, s_v, f_v);

    svuota();
    close(g_fd);

    if (g_guasto) {
        printf("mappa: la scrittura si e' interrotta - %s e' incompleto.\n",
               perc);
        return 1;
    }

    printf("mappa: fatto. %s e' pronto.\n", perc);
    return 0;
}

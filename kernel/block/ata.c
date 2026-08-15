/* =============================================================================
 * kernel/block/ata.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Driver ATA/IDE. Vedi kernel/include/ata.h per il razionale sulle
 * capacita' dichiarate; qui sotto ci sono le trappole del protocollo.
 *
 * DUE STRADE, E LA SECONDA E' UN'OTTIMIZZAZIONE. Il trasferimento passa dal
 * DMA bus master quando il controller IDE si trova sul PCI, e ricade in PIO
 * quando non c'e' o quando fallisce — vedi il blocco «DMA BUS MASTER» piu'
 * avanti. Misurato dentro QEMU, 7,2 MB fra lettura e scrittura:
 *
 *     PIO   17,31 s        DMA   9,89 s        -43%
 *
 * (tre campioni per parte, scarto ±0,02 s; il DMA e' anche piu' regolare)
 *
 * SI USA IL POLLING, NON L'IRQ14/15. Gli stub ci sono gia'
 * (kernel/arch/x86/idt.c), ma il bit nIEN nel registro di controllo
 * disattiva gli interrupt del dispositivo e si aspetta leggendo lo stato.
 * E' la scelta piu' sicura per partire: niente handler da coordinare con
 * il driver FDC, e soprattutto niente dipendenza dal routing degli
 * interrupt su una macchina sconosciuta — che su questo stesso progetto ha
 * gia' fatto perdere un'intera sessione con l'IRQ6 del floppy.
 *
 * LE ATTESE SONO IN TEMPO REALE, NON A CONTEGGIO. E' la lezione gia'
 * pagata tre volte in questo progetto (loop di NOP dell'FDC, KBC_POLL_MAX,
 * usleep sotto il tick): un'attesa ancorata alla velocita' della CPU vale
 * tempi diversi su macchine diverse, e su hardware vero fallisce. Qui si
 * usa g_ticks, che e' il PIT.
 *
 * TRAPPOLE DEL PROTOCOLLO ATA, e come sono trattate:
 *
 *  - Ritardo di 400 ns dopo aver selezionato l'unita' o scritto un
 *    comando: i registri non sono validi subito. Si ottiene leggendo
 *    QUATTRO volte lo stato alternato (0x3F6), che non consuma lo stato
 *    pendente come farebbe leggere 0x1F7.
 *  - Bus "flottante": se non c'e' nulla attaccato, lo stato legge 0xFF.
 *    Va riconosciuto PRIMA di aspettare qualunque cosa, o si aspetta a
 *    vuoto fino al timeout su ogni slot vuoto.
 *  - IDENTIFY su un ATAPI fallisce con ERR: la firma nei registri LBA
 *    mid/high (0x14/0xEB) dice che e' un lettore ottico. Va riconosciuto,
 *    altrimenti lo si scambia per un disco rotto.
 *  - Le stringhe di IDENTIFY sono a byte scambiati dentro ogni parola a
 *    16 bit. Senza de-swap il modello esce come "TSHIAB..." invece di
 *    "HITACHI...".
 *  - Il conteggio settori 0 significa 256 (LBA28) o 65536 (LBA48). Non si
 *    manda mai 0.
 *  - Con LBA48 i registri sono a due livelli: si scrive prima il byte
 *    ALTO poi quello BASSO sulla stessa porta. Invertire l'ordine da
 *    indirizzi silenziosamente sbagliati — cioe' letture della zona
 *    sbagliata del disco senza alcun errore segnalato.
 * ============================================================================= */

#include "kernel.h"
#include "ata.h"
#include "sched.h"      /* g_ticks */

/* Le porte, gli offset dei registri e i bit di stato stanno in ata.h:
 * sono lo stesso bus che usa kernel/block/atapi.c, e una seconda copia
 * sarebbe una copia da tenere allineata a mano. */

/* Comandi */
#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_FLUSH           0xE7
#define ATA_CMD_FLUSH_EXT       0xEA
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1    /* IDENTIFY per i dispositivi ATAPI */
#define ATA_CMD_READ_NATIVE     0xF8
#define ATA_CMD_READ_NATIVE_EXT 0x27
#define ATA_CMD_READ_DMA        0xC8
#define ATA_CMD_READ_DMA_EXT    0x25
#define ATA_CMD_WRITE_DMA       0xCA
#define ATA_CMD_WRITE_DMA_EXT   0x35

/* Scadenze reali, in millisecondi. Generose: uno spin-up puo' durare
 * secondi su un disco dell'epoca, e un timeout troppo corto si
 * presenterebbe come "disco assente". */
#define ATA_TMO_BSY_MS      5000    /* attesa che il comando finisca */
#define ATA_TMO_DRQ_MS      3000    /* attesa dati pronti            */
#define ATA_TMO_IDENT_MS    3000

/* Massimo settori per singolo comando. LBA28 usa un conteggio a 8 bit
 * (0 = 256, ma non lo usiamo), LBA48 a 16 bit. */
#define ATA_MAX_N28         255
#define ATA_MAX_N48         65535

static AtaDevice g_dev[ATA_MAX_DEVICES];
static int       g_trovati = 0;

/* Nomi brevi per l'uso interno: il resto del file era gia' scritto cosi'
 * e rinominare ogni chiamata avrebbe sporcato un diff che deve restare
 * leggibile.
 *
 * ! STANNO QUI E NON PIU' A META' FILE perche' adesso li usa anche il
 * blocco DMA qui sotto, che viene prima. Sono alias puri, senza
 * dipendenze: spostarli in alto non cambia niente per chi li usava gia'. */
#define base_io(c)      ata_base_io(c)
#define base_ctrl(c)    ata_base_ctrl(c)
#define ata_400ns(c)    ata_ritardo(c)

/* =============================================================================
 * DMA BUS MASTER — perche' esiste, e cosa NON fa
 *
 * In PIO ogni parola passa dalla CPU: `rep insw` e' una istruzione sola, ma
 * il processore resta a copiare 256 parole per settore. Con il bus master e'
 * il controller a scrivere in memoria da solo, e la CPU si limita a dire
 * dove e quanto.
 *
 * ! IL GUADAGNO SI VEDE SOPRATTUTTO SOTTO EMULAZIONE, ed e' bene dirlo
 * perche' e' il caso in cui si sviluppa: QEMU senza KVM emula ogni singola
 * `insw`, mentre un trasferimento DMA per lui e' una copia di memoria
 * dell'host. Su ferro vero il guadagno c'e' lo stesso, ma di meno.
 *
 * -----------------------------------------------------------------------------
 * ! IL KERNEL NON HA UN DRIVER PCI, E NON PUO' AVERLO QUI
 *
 * Il PCI di EX-OS e' un driver in spazio utente (drivers/pci/). Ma il
 * controller IDE serve PRIMA: il kernel legge il disco per CARICARE i
 * driver. Chiedere al driver PCI dove sia il bus master sarebbe un cerchio.
 *
 * Percio' qui sotto c'e' la lettura di configurazione PCI ridotta all'osso —
 * due porte, 0xCF8 e 0xCFC — che serve a una cosa sola: trovare la BAR4 del
 * controller IDE. Non e' un secondo driver PCI e non deve diventarlo.
 *
 * -----------------------------------------------------------------------------
 * ! SI PASSA SEMPRE DA UN BUFFER DI RIMBALZO, ANCHE QUANDO SEMBRA INUTILE
 *
 * Il controller scrive in memoria FISICA. Il buffer che arriva a ata_rw puo'
 * essere:
 *
 *   - memoria del kernel: e' identity-mapped (vedi paging_init), quindi
 *     virtuale == fisico e sarebbe usabile direttamente;
 *   - un buffer UTENTE: le sue pagine sono sparse per la RAM, e l'indirizzo
 *     virtuale non dice niente su dove stiano davvero. Darlo al controller
 *     vorrebbe dire scrivere in memoria di qualcun altro.
 *
 * Distinguere i due casi qui dentro vorrebbe dire fidarsi di chi chiama.
 * Rimbalzare sempre costa una copia in memoria — che e' comunque molto meno
 * di 256 `insw` per settore — e non ha un modo di sbagliare.
 *
 * ! IL BUFFER E' UN ARRAY STATICO, non kmalloc: cosi' sta nel BSS del
 * kernel, che e' identity-mapped e fisicamente contiguo per costruzione.
 * Un blocco dello heap potrebbe non esserlo, e il DMA non se ne accorgerebbe:
 * scriverebbe di seguito in memoria fisica leggendo pagine che appartengono
 * a qualcos'altro.
 *
 * ! ALLINEATO A 64 KB perche' una voce del PRDT NON PUO' ATTRAVERSARE un
 * confine di 64 KB. Allineandolo, i 64 KB del buffer sono esattamente una
 * voce sola e il caso di divisione non esiste.
 * ============================================================================= */

/* Registri del bus master IDE, offset dalla base letta in BAR4. Il canale
 * secondario sta 8 byte piu' avanti. */
#define BM_CMD          0x00        /* bit 0 = avvia, bit 3 = leggi          */
#define BM_STATUS       0x02        /* bit 0 = attivo, 1 = errore, 2 = irq   */
#define BM_PRDT         0x04        /* indirizzo fisico della tabella        */

#define BM_CMD_AVVIA    0x01
#define BM_CMD_LEGGI    0x08        /* dal disco verso la memoria            */
#define BM_ST_ATTIVO    0x01
#define BM_ST_ERRORE    0x02
#define BM_ST_IRQ       0x04

#define DMA_BUF_BYTE    65536u
#define DMA_MAX_SETT    (DMA_BUF_BYTE / ATA_SECTOR_SIZE)   /* 128 settori */

/* Una voce del PRDT: indirizzo fisico, quanti byte (0 = 65536), e il bit 15
 * dei flag che marca l'ultima voce. */
typedef struct {
    uint32_t indirizzo;
    uint16_t byte;
    uint16_t flags;
} __attribute__((packed)) Prd;

/* Il kernel non ha una memcpy comune: blk.c se ne scrive una uguale a questa,
 * poche righe accanto. Copiarla e' meno peggio che inventare qui una
 * dipendenza che nessuno degli altri driver di blocco ha. */
static void dma_copia(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    while (n--) *dst++ = *src++;
}

static uint8_t  g_dma_buf[DMA_BUF_BYTE] __attribute__((aligned(65536)));
static Prd      g_prdt[1]               __attribute__((aligned(8)));
static uint16_t g_bm_base = 0;          /* 0 = niente bus master: si va in PIO */

/* --- lettura di configurazione PCI, il minimo indispensabile -------------- */
#define PCI_ADDR    0xCF8
#define PCI_DATA    0xCFC

static uint32_t pci_leggi32(uint8_t bus, uint8_t slot, uint8_t fn, uint8_t off)
{
    uint32_t ind = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11)
                 | ((uint32_t)fn << 8) | (off & 0xFCu);

    port_outl(PCI_ADDR, ind);
    return port_inl(PCI_DATA);
}

static void pci_scrivi32(uint8_t bus, uint8_t slot, uint8_t fn, uint8_t off,
                         uint32_t val)
{
    uint32_t ind = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11)
                 | ((uint32_t)fn << 8) | (off & 0xFCu);

    port_outl(PCI_ADDR, ind);
    port_outl(PCI_DATA, val);
}

/* Cerca il controller IDE (classe 01, sottoclasse 01), ne prende la BAR4 e
 * accende il bus master. Rende 0 se non c'e' niente da usare.
 *
 * ! SENZA IL BIT DI BUS MASTER NEL REGISTRO COMANDI il controller non puo'
 * scrivere in memoria: la tabella la legge, il trasferimento non parte, e lo
 * stato non dice «manca il permesso» — dice solo che non e' successo niente.
 * Su QEMU il bit e' spesso gia' acceso, il che rende la dimenticanza
 * invisibile proprio dove si prova. */
static uint16_t ata_trova_bus_master(void)
{
    uint16_t bus, slot, fn;

    /* ! SI SCANDISCONO TUTTE E OTTO LE FUNZIONI, non solo la zero, e questo
     * pezzo e' costato la prima prova: il controller IDE del PIIX3 — quello
     * che QEMU mette di default, ed e' anche il piu' comune sul ferro
     * dell'epoca — sta in PCI 00:01.**1**, perche' 00:01.0 e' il ponte ISA.
     *
     * Guardando la sola funzione 0 non lo si trova mai, e il messaggio che
     * ne usciva era «nessun bus master IDE sul PCI»: perfettamente vero
     * riguardo a cio' che era stato guardato, e completamente fuorviante.
     *
     * Le funzioni assenti rendono 0xFFFF, quindi provarle tutte non costa
     * niente e toglie la necessita' di leggere il bit di multifunzione. */
    for (bus = 0; bus < 256; bus++) {
        for (slot = 0; slot < 32; slot++) {
            for (fn = 0; fn < 8; fn++) {
                uint32_t id = pci_leggi32((uint8_t)bus, (uint8_t)slot,
                                          (uint8_t)fn, 0x00);
                uint32_t classe, bar4, cmd;

                if ((id & 0xFFFFu) == 0xFFFFu) continue;  /* niente qui */

                classe = pci_leggi32((uint8_t)bus, (uint8_t)slot,
                                     (uint8_t)fn, 0x08);
                if ((classe >> 16) != 0x0101u) continue;  /* non e' IDE */

                bar4 = pci_leggi32((uint8_t)bus, (uint8_t)slot,
                                   (uint8_t)fn, 0x20);
                if ((bar4 & 1u) == 0) continue;           /* non e' in I/O */
                bar4 &= 0xFFFCu;
                if (bar4 == 0) continue;

                cmd = pci_leggi32((uint8_t)bus, (uint8_t)slot,
                                  (uint8_t)fn, 0x04);
                if ((cmd & 0x0004u) == 0) {
                    pci_scrivi32((uint8_t)bus, (uint8_t)slot, (uint8_t)fn,
                                 0x04, cmd | 0x0004u);
                }

                klog(LOG_INFO, "ATA: bus master IDE a 0x%04x (PCI %u:%u.%u)",
                     (uint16_t)bar4, bus, slot, fn);
                return (uint16_t)bar4;
            }
        }
    }

    klog(LOG_INFO, "ATA: nessun bus master IDE sul PCI, si resta in PIO");
    return 0;
}

/* Un trasferimento DMA di al massimo DMA_MAX_SETT settori, dal/nel buffer di
 * rimbalzo. Rende 0, -1 se qualcosa non ha funzionato.
 *
 * ! SI ASPETTA GUARDANDO IL REGISTRO, NON UN INTERRUPT. Legare il DMA
 * all'IRQ 14 vorrebbe dire toccare la catena degli interrupt in un driver
 * che il kernel usa PRIMA che quella catena sia completa — e un
 * trasferimento che non si sveglia e' un avvio che si ferma senza dire
 * niente. Con l'attesa attiva il controller fa comunque tutto il lavoro di
 * copia: e' quello il guadagno, non il non-attendere.
 */
static int ata_dma_blocco(int canale, int unita, uint64_t lba, uint32_t n,
                          int usa48, int scrivi)
{
    uint16_t io = base_io(canale);
    uint16_t bm = (uint16_t)(g_bm_base + (canale ? 8 : 0));
    uint32_t scaduto = 0;
    uint8_t  st;

    if (n == 0 || n > DMA_MAX_SETT) return -1;

    /* La tabella: una voce sola, tutta il trasferimento, ultima della lista.
     * `byte` a 0 significherebbe 65536 — e con n = DMA_MAX_SETT e' proprio
     * quello che serve, quindi il troncamento a 16 bit fa la cosa giusta. */
    g_prdt[0].indirizzo = (uint32_t)(uintptr_t)g_dma_buf;
    g_prdt[0].byte      = (uint16_t)(n * ATA_SECTOR_SIZE);
    g_prdt[0].flags     = 0x8000u;

    /* Fermo, poi pulisco errore e irq scrivendoci sopra 1: sono bit che si
     * azzerano scrivendo, e lasciarli sporchi fa sembrare fallito il
     * trasferimento successivo. */
    port_outb(bm + BM_CMD, 0);
    port_outb(bm + BM_STATUS,
              (uint8_t)(port_inb(bm + BM_STATUS) | BM_ST_ERRORE | BM_ST_IRQ));
    port_outl(bm + BM_PRDT, (uint32_t)(uintptr_t)g_prdt);
    port_outb(bm + BM_CMD, (uint8_t)(scrivi ? 0 : BM_CMD_LEGGI));

    /* Il comando al disco, esattamente come in PIO ma con l'opcode DMA. */
    port_outb(base_ctrl(canale), ATA_CTRL_NIEN);

    if (usa48) {
        port_outb(io + ATA_REG_DRIVE, (uint8_t)(0x40 | (unita ? 0x10 : 0)));
        ata_400ns(canale);
        port_outb(io + ATA_REG_SECCOUNT, (uint8_t)((n >> 8) & 0xFF));
        port_outb(io + ATA_REG_LBA0,     (uint8_t)((lba >> 24) & 0xFF));
        port_outb(io + ATA_REG_LBA1,     (uint8_t)((lba >> 32) & 0xFF));
        port_outb(io + ATA_REG_LBA2,     (uint8_t)((lba >> 40) & 0xFF));
        port_outb(io + ATA_REG_SECCOUNT, (uint8_t)(n & 0xFF));
        port_outb(io + ATA_REG_LBA0,     (uint8_t)(lba & 0xFF));
        port_outb(io + ATA_REG_LBA1,     (uint8_t)((lba >> 8) & 0xFF));
        port_outb(io + ATA_REG_LBA2,     (uint8_t)((lba >> 16) & 0xFF));
        port_outb(io + ATA_REG_COMMAND,
                  scrivi ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT);
    } else {
        port_outb(io + ATA_REG_DRIVE,
                  (uint8_t)(0xE0 | (unita ? 0x10 : 0) | ((lba >> 24) & 0x0F)));
        ata_400ns(canale);
        port_outb(io + ATA_REG_SECCOUNT, (uint8_t)n);
        port_outb(io + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
        port_outb(io + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
        port_outb(io + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
        port_outb(io + ATA_REG_COMMAND,
                  scrivi ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA);
    }

    ata_400ns(canale);

    /* Adesso si accende il motore: prima del comando non avrebbe niente da
     * trasferire, dopo il comando il disco aspetta noi. */
    port_outb(bm + BM_CMD,
              (uint8_t)((scrivi ? 0 : BM_CMD_LEGGI) | BM_CMD_AVVIA));

    for (;;) {
        st = port_inb(bm + BM_STATUS);

        if (st & BM_ST_ERRORE) break;
        /* Finito: il bit ATTIVO cade. L'IRQ si alza anche quando il
         * trasferimento e' andato bene, quindi da solo non basta a dire
         * «finito» — ma la coppia (non piu' attivo) e' il segnale buono. */
        if (!(st & BM_ST_ATTIVO)) break;

        if (++scaduto > 200000u) {          /* ~ qualche secondo di giri */
            klog(LOG_ERROR, "ATA: DMA fermo a lba=%u", (uint32_t)lba);
            port_outb(bm + BM_CMD, 0);
            return -1;
        }
        ata_400ns(canale);
    }

    port_outb(bm + BM_CMD, 0);
    port_outb(bm + BM_STATUS,
              (uint8_t)(port_inb(bm + BM_STATUS) | BM_ST_ERRORE | BM_ST_IRQ));

    if (st & BM_ST_ERRORE) {
        klog(LOG_ERROR, "ATA: DMA errore del bus master a lba=%u",
             (uint32_t)lba);
        return -1;
    }

    /* ! E POI SI GUARDA ANCHE IL DISCO. Il bus master dice se la COPIA e'
     * andata; se il disco ha trovato un settore illeggibile lo dice lui, con
     * ERR nel proprio registro di stato, e il bus master puo' non
     * accorgersene. Fidarsi di uno solo dei due vuol dire restituire dati
     * sbagliati senza errore. */
    {
        int s = ata_attendi_non_bsy(canale, ATA_TMO_BSY_MS);

        if (s < 0 || ((uint8_t)s & (ATA_SR_ERR | ATA_SR_DF))) {
            klog(LOG_ERROR, "ATA: DMA, il disco segnala errore a lba=%u "
                 "(stato=0x%02x)", (uint32_t)lba, (uint8_t)s);
            return -1;
        }
    }

    return 0;
}

/* =============================================================================
 * Helper di basso livello
 * ============================================================================= */

uint16_t ata_base_io(int canale)
{
    return (canale == 0) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
}

uint16_t ata_base_ctrl(int canale)
{
    return (canale == 0) ? ATA_PRIMARY_CTRL : ATA_SECONDARY_CTRL;
}

/* Ritardo di ~400 ns: quattro letture dello stato ALTERNATO.
 * Va letto 0x3F6 e non 0x1F7 perche' lo stato alternato non ha effetti
 * collaterali, mentre leggere il registro di stato normale azzera
 * l'interrupt pendente. */
void ata_ritardo(int canale)
{
    uint16_t c = ata_base_ctrl(canale);
    port_inb(c); port_inb(c); port_inb(c); port_inb(c);
}

/* Attesa in millisecondi ancorata al PIT. Serve ai lettori ottici, che
 * dopo l'inserimento di un disco rispondono "non ancora pronto" per
 * qualche secondo e vanno risondati a intervalli, non a raffica. */
void ata_attesa_ms(uint32_t ms)
{
    uint32_t fine = g_ticks + (ms + 9) / 10;
    while (g_ticks < fine) { /* il PIT avanza sotto interrupt */ }
}

/* Aspetta che BSY si abbassi, entro una scadenza REALE.
 * Ritorna lo stato letto, o -1 su timeout. */
int ata_attendi_non_bsy(int canale, uint32_t timeout_ms)
{
    uint16_t io    = base_io(canale);
    uint32_t scad  = g_ticks + (timeout_ms + 9) / 10;
    uint8_t  st;

    for (;;) {
        st = port_inb(io + ATA_REG_STATUS);

        /* Bus flottante: nessun dispositivo. Non ha senso attendere. */
        if (st == 0xFF) return -1;

        if (!(st & ATA_SR_BSY)) return (int)st;

        if (g_ticks >= scad) {
            klog(LOG_ERROR, "ATA: timeout BSY sul canale %d (stato=0x%02x)",
                 canale, st);
            return -1;
        }
    }
}

/* Aspetta DRQ (dati pronti) dopo che BSY si e' abbassato, SENZA stampare
 * nulla. Ritorna 0, -1 su timeout o bus assente, -2 se il dispositivo ha
 * alzato ERR/DF. Vedi ata.h per il motivo per cui la variante muta e'
 * quella di base: su ATAPI un ERR e' spesso "vassoio vuoto", cioe' una
 * risposta, non un guasto. */
int ata_attendi_drq_muto(int canale, uint32_t timeout_ms, uint8_t *stato_out)
{
    uint16_t io   = base_io(canale);
    uint32_t scad = g_ticks + (timeout_ms + 9) / 10;

    for (;;) {
        uint8_t st = port_inb(io + ATA_REG_STATUS);

        if (stato_out) *stato_out = st;

        if (st == 0xFF) return -1;

        /* ERR e DF vanno controllati PRIMA di DRQ: un comando fallito
         * puo' non alzare mai DRQ, e aspettarlo significherebbe restare
         * fino al timeout invece di riportare subito l'errore vero. */
        if (st & (ATA_SR_ERR | ATA_SR_DF)) return -2;

        if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRQ)) return 0;

        if (g_ticks >= scad) return -1;
    }
}

int ata_attendi_drq(int canale, uint32_t timeout_ms)
{
    uint8_t st = 0;
    int     r  = ata_attendi_drq_muto(canale, timeout_ms, &st);

    if (r == -2) {
        uint8_t err = port_inb(base_io(canale) + ATA_REG_ERROR);
        klog(LOG_ERROR, "ATA: errore canale %d (stato=0x%02x errore=0x%02x)",
             canale, st, err);
        return -1;
    }
    if (r == -1 && st != 0xFF) {
        klog(LOG_ERROR, "ATA: timeout DRQ sul canale %d (stato=0x%02x)",
             canale, st);
    }
    return (r < 0) ? -1 : 0;
}

/* Seleziona master/slave. Il ritardo dopo la selezione non e' opzionale:
 * i registri dell'unita' appena selezionata non sono validi prima. */
void ata_seleziona(int canale, int unita, uint8_t testa_o_lba)
{
    uint16_t io = base_io(canale);
    port_outb(io + ATA_REG_DRIVE,
              (uint8_t)(0xA0 | (unita ? 0x10 : 0x00) | (testa_o_lba & 0x0F)));
    ata_400ns(canale);
}

/* Copia una stringa IDENTIFY de-swappando i byte dentro ogni parola. */
static void ata_stringa(const uint16_t *id, int prima_parola, int n_parole,
                        char *out)
{
    int i;
    int j = 0;

    for (i = 0; i < n_parole; i++) {
        uint16_t w = id[prima_parola + i];
        out[j++] = (char)((w >> 8) & 0xFF);
        out[j++] = (char)(w & 0xFF);
    }
    out[j] = '\0';

    /* Toglie gli spazi finali: IDENTIFY riempie con spazi, non con NUL. */
    while (j > 0 && (out[j - 1] == ' ' || out[j - 1] == '\0')) {
        out[--j] = '\0';
    }
}

/* =============================================================================
 * READ NATIVE MAX ADDRESS — la capacita' di fabbrica
 *
 * E' il comando che smaschera lo spazio nascosto. Ritorna l'LBA PIU' ALTO
 * indirizzabile, quindi il numero di settori e' quel valore + 1 — un
 * fuori-di-uno qui significherebbe sbagliare di un settore la dimensione
 * del disco, ed e' esattamente il genere di errore che in un programma di
 * partizionamento si paga caro.
 * ============================================================================= */
static uint64_t ata_native_max(int canale, int unita, int lba48)
{
    uint16_t io = base_io(canale);
    int      st;

    if (lba48) {
        ata_seleziona(canale, unita, 0);
        port_outb(io + ATA_REG_DRIVE, (uint8_t)(0x40 | (unita ? 0x10 : 0)));
        ata_400ns(canale);
        port_outb(io + ATA_REG_COMMAND, ATA_CMD_READ_NATIVE_EXT);
    } else {
        port_outb(io + ATA_REG_DRIVE, (uint8_t)(0xE0 | (unita ? 0x10 : 0)));
        ata_400ns(canale);
        port_outb(io + ATA_REG_COMMAND, ATA_CMD_READ_NATIVE);
    }

    ata_400ns(canale);
    st = ata_attendi_non_bsy(canale, ATA_TMO_BSY_MS);

    /* Non tutti i dischi supportano questo comando: se fallisce si
     * ritorna 0 e il chiamante usa la sola IDENTIFY. Non e' un errore
     * fatale, e' un'informazione che non si e' potuta ottenere. */
    if (st < 0 || ((uint8_t)st & (ATA_SR_ERR | ATA_SR_DF))) return 0;

    if (lba48) {
        uint64_t lo, hi;
        /* Il registro va letto due volte: prima il byte basso, poi
         * riletto con HOB=1 nel registro di controllo per quello alto. */
        lo  = (uint64_t)port_inb(io + ATA_REG_LBA0);
        lo |= (uint64_t)port_inb(io + ATA_REG_LBA1) << 8;
        lo |= (uint64_t)port_inb(io + ATA_REG_LBA2) << 16;

        port_outb(base_ctrl(canale), 0x80 | ATA_CTRL_NIEN);   /* HOB=1 */
        ata_400ns(canale);
        hi  = (uint64_t)port_inb(io + ATA_REG_LBA0) << 24;
        hi |= (uint64_t)port_inb(io + ATA_REG_LBA1) << 32;
        hi |= (uint64_t)port_inb(io + ATA_REG_LBA2) << 40;
        port_outb(base_ctrl(canale), ATA_CTRL_NIEN);          /* HOB=0 */

        return (lo | hi) + 1;
    } else {
        uint32_t lba;
        lba  = (uint32_t)port_inb(io + ATA_REG_LBA0);
        lba |= (uint32_t)port_inb(io + ATA_REG_LBA1) << 8;
        lba |= (uint32_t)port_inb(io + ATA_REG_LBA2) << 16;
        lba |= (uint32_t)(port_inb(io + ATA_REG_DRIVE) & 0x0F) << 24;
        return (uint64_t)lba + 1;
    }
}

/* =============================================================================
 * Rilevamento di una singola unita'
 * ============================================================================= */
static void ata_rileva(int idx, int canale, int unita)
{
    uint16_t   io = base_io(canale);
    AtaDevice *d  = &g_dev[idx];
    uint16_t   id[256];
    int        st, i;
    uint8_t    cl, ch;

    d->presente = 0;
    d->tipo     = ATA_TYPE_NONE;
    d->canale   = (uint8_t)canale;
    d->unita    = (uint8_t)unita;

    /* Interrupt del dispositivo disabilitati: si lavora in polling. */
    port_outb(base_ctrl(canale), ATA_CTRL_NIEN);

    ata_seleziona(canale, unita, 0);

    /* Bus flottante: slot vuoto, si esce subito senza aspettare nulla. */
    st = port_inb(io + ATA_REG_STATUS);
    if (st == 0xFF || st == 0x00) return;

    /* Azzera i registri prima di IDENTIFY: alcuni dispositivi rispondono
     * male se contengono residui di un comando precedente. */
    port_outb(io + ATA_REG_SECCOUNT, 0);
    port_outb(io + ATA_REG_LBA0, 0);
    port_outb(io + ATA_REG_LBA1, 0);
    port_outb(io + ATA_REG_LBA2, 0);

    port_outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_400ns(canale);

    st = port_inb(io + ATA_REG_STATUS);
    if (st == 0) return;    /* niente qui */

    if (ata_attendi_non_bsy(canale, ATA_TMO_IDENT_MS) < 0) return;

    /* Un ATAPI rifiuta IDENTIFY con ERR, ma lascia la propria firma nei
     * registri LBA mid/high. Senza questo controllo un lettore CD
     * sembrerebbe un disco guasto. */
    cl = port_inb(io + ATA_REG_LBA1);
    ch = port_inb(io + ATA_REG_LBA2);

    if (cl == 0x14 && ch == 0xEB) {
        d->presente = 1;
        d->tipo     = ATA_TYPE_ATAPI;

        /* IDENTIFY PACKET DEVICE: le stringhe stanno nelle stesse parole
         * di IDENTIFY, ma il comando e' un altro perche' l'unita' ha
         * appena RIFIUTATO quello normale. Senza questo passaggio il
         * lettore comparirebbe in `disk` come una riga vuota, e un
         * dispositivo senza nome e' indistinguibile da uno non
         * riconosciuto.
         *
         * Un fallimento qui non toglie il dispositivo: resta un ATAPI, e
         * lo si dira' senza il modello. La capacita' NON si chiede qui —
         * un lettore la conosce solo quando ha un disco dentro, e la
         * risposta cambia a ogni inserimento: la chiede atapi.c, ogni
         * volta che serve. */
        port_outb(io + ATA_REG_SECCOUNT, 0);
        port_outb(io + ATA_REG_LBA0, 0);
        port_outb(io + ATA_REG_LBA1, 0);
        port_outb(io + ATA_REG_LBA2, 0);
        port_outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
        ata_400ns(canale);

        if (ata_attendi_drq_muto(canale, ATA_TMO_IDENT_MS, NULL) == 0) {
            for (i = 0; i < 256; i++) id[i] = port_inw(io + ATA_REG_DATA);

            ata_stringa(id, 27, 20, d->modello);
            ata_stringa(id, 10, 10, d->seriale);
            ata_stringa(id, 23,  4, d->firmware);
        }

        klog(LOG_INFO, "ATA: canale %d %s: lettore ottico ATAPI%s%s",
             canale, unita ? "slave" : "master",
             d->modello[0] ? " " : "", d->modello);
        return;
    }
    if (cl != 0x00 || ch != 0x00) {
        d->presente = 1;
        d->tipo     = ATA_TYPE_UNKNOWN;
        klog(LOG_WARN, "ATA: canale %d %s: firma sconosciuta (0x%02x 0x%02x)",
             canale, unita ? "slave" : "master", cl, ch);
        return;
    }

    if (ata_attendi_drq(canale, ATA_TMO_IDENT_MS) < 0) return;

    for (i = 0; i < 256; i++) {
        id[i] = port_inw(io + ATA_REG_DATA);
    }

    d->presente = 1;
    d->tipo     = ATA_TYPE_ATA;

    ata_stringa(id, 27, 20, d->modello);
    ata_stringa(id, 10, 10, d->seriale);
    ata_stringa(id, 23,  4, d->firmware);

    /* Parola 83 bit 10: comandi a 48 bit supportati.
     * Parola 82 bit 10: Host Protected Area supportata. */
    d->lba48 = (id[83] & (1 << 10)) ? 1 : 0;
    d->hpa   = (id[82] & (1 << 10)) ? 1 : 0;

    /* Capacita' dichiarata: parole 60-61 (28 bit) oppure 100-103 (48 bit).
     * Con LBA48 le 60-61 restano valide ma SATURANO a 0x0FFFFFFF, quindi
     * per un disco grande darebbero 128 GiB qualunque sia la dimensione
     * vera: e' una delle strade classiche per cui un sistema "vede meno
     * disco di quanto ce n'e'". */
    if (d->lba48) {
        d->settori = (uint64_t)id[100]
                   | ((uint64_t)id[101] << 16)
                   | ((uint64_t)id[102] << 32)
                   | ((uint64_t)id[103] << 48);
    } else {
        d->settori = 0;
    }

    if (d->settori == 0) {
        d->settori = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    }

    /* Capacita' di fabbrica, per scoprire lo spazio nascosto. */
    d->settori_nativi = ata_native_max(canale, unita, d->lba48);

    d->clippato = (d->settori_nativi > d->settori) ? 1 : 0;

    klog(LOG_INFO, "ATA: canale %d %s: '%s' %u MB%s%s",
         canale, unita ? "slave" : "master", d->modello,
         (uint32_t)(d->settori / 2048),
         d->lba48 ? " LBA48" : " LBA28",
         d->clippato ? " [SPAZIO NASCOSTO]" : "");

    if (d->clippato) {
        klog(LOG_WARN, "ATA: il disco dichiara %u MB ma di fabbrica ne ha %u "
             "— HPA attiva o jumper di limitazione",
             (uint32_t)(d->settori / 2048),
             (uint32_t)(d->settori_nativi / 2048));
    }
}

int ata_init(void)
{
    int canale, unita, idx = 0;

    g_trovati = 0;

    for (idx = 0; idx < ATA_MAX_DEVICES; idx++) {
        g_dev[idx].presente = 0;
        g_dev[idx].tipo     = ATA_TYPE_NONE;
    }

    /* ! IL BUS MASTER SI CERCA PRIMA DEL RILEVAMENTO, non dopo: ata_rileva()
     * legge gia' dai dischi, e trovare il DMA solo alla fine vorrebbe dire
     * fare in PIO proprio le letture dell'avvio — che sono quelle che
     * l'utente vede come "quanto ci mette ad accendersi". */
    g_bm_base = ata_trova_bus_master();

    klog(LOG_INFO, "ATA: rilevamento unita' sui canali primario e secondario...");

    idx = 0;
    for (canale = 0; canale < 2; canale++) {
        for (unita = 0; unita < 2; unita++) {
            ata_rileva(idx, canale, unita);
            if (g_dev[idx].tipo == ATA_TYPE_ATA) g_trovati++;
            idx++;
        }
    }

    if (g_trovati == 0) {
        klog(LOG_INFO, "ATA: nessun disco rigido trovato");
    }

    return g_trovati;
}

const AtaDevice *ata_get_device(int indice)
{
    if (indice < 0 || indice >= ATA_MAX_DEVICES) return NULL;
    return &g_dev[indice];
}

/* =============================================================================
 * Trasferimento
 *
 * Un solo punto per lettura e scrittura: e' la stessa lezione imparata sul
 * driver FDC, dove due copie quasi identiche avevano fatto sopravvivere a
 * lungo un baco corretto solo in una delle due.
 * ============================================================================= */
static int ata_rw(int indice, uint64_t lba, uint32_t n, void *buf, int scrivi)
{
    const AtaDevice *d;
    uint16_t  io;
    int       canale, unita, usa48;
    uint8_t  *p = (uint8_t *)buf;
    uint32_t  s;

    d = ata_get_device(indice);
    if (d == NULL || !d->presente || d->tipo != ATA_TYPE_ATA) return -1;
    if (n == 0) return -1;

    /* CONTROLLO DI LIMITE — non e' una formalita'.
     * Una lettura oltre la fine del disco ritorna un errore; una
     * SCRITTURA oltre la fine, su un disco che la accettasse per
     * troncamento dell'indirizzo, andrebbe a finire da un'altra parte. Il
     * controllo va fatto qui, una volta, dove passano tutti. */
    if (lba + n > d->settori || lba + n < lba /* overflow */) {
        klog(LOG_ERROR, "ATA: accesso fuori disco (lba=%u+%u, settori=%u)",
             (uint32_t)lba, n, (uint32_t)d->settori);
        return -1;
    }

    canale = d->canale;
    unita  = d->unita;
    io     = base_io(canale);

    /* LBA48 se serve davvero (oltre i 2^28 settori indirizzabili o piu' di
     * 255 settori per comando) e se il disco lo supporta. */
    usa48 = d->lba48 && ((lba + n) > 0x0FFFFFFFull || n > ATA_MAX_N28);

    if (!usa48 && (lba + n) > 0x0FFFFFFFull) {
        klog(LOG_ERROR, "ATA: LBA oltre i 28 bit ma il disco non supporta LBA48");
        return -1;
    }

    /* =====================================================================
     * La strada veloce: bus master, se c'e'.
     *
     * ! SE FALLISCE SI RIPIEGA SU PIO invece di rendere errore, e la scelta
     * e' deliberata: il DMA e' un'ottimizzazione, non una funzione. Un
     * controller che non collabora — o una macchina virtuale con
     * un'emulazione parziale — deve rendere il sistema piu' lento, non
     * inavviabile. Il ripiego si dice una volta sola nel log e poi si tace,
     * perche' su un disco lento sarebbe una riga per settore.
     * ===================================================================== */
    if (g_bm_base != 0) {
        static int gia_detto = 0;
        uint64_t   l = lba;
        uint32_t   r = n;
        uint8_t   *q = p;
        int        ok = 1;

        while (r > 0 && ok) {
            uint32_t b = (r > DMA_MAX_SETT) ? DMA_MAX_SETT : r;
            uint32_t byte = b * ATA_SECTOR_SIZE;
            int      c48 = d->lba48 && (l + b) > 0x0FFFFFFFull;

            if (scrivi) dma_copia(g_dma_buf, q, byte);

            if (ata_dma_blocco(canale, unita, l, b, c48, scrivi) != 0) {
                ok = 0;
                break;
            }

            if (!scrivi) dma_copia(q, g_dma_buf, byte);

            q += byte;
            l += b;
            r -= b;
        }

        if (ok) return 0;

        if (!gia_detto) {
            klog(LOG_ERROR, "ATA: il DMA non funziona, si continua in PIO");
            gia_detto = 1;
        }
        /* ! E SI RIPARTE DA CAPO, non da dove il DMA si e' fermato: una
         * parte del buffer potrebbe essere gia' stata riempita a meta' da un
         * trasferimento interrotto, e ripetere una lettura non costa niente
         * mentre fidarsi di dati parziali costa tutto. */
    }

    while (n > 0) {
        uint32_t blocco = n;
        uint32_t max    = usa48 ? ATA_MAX_N48 : ATA_MAX_N28;
        int      st;

        if (blocco > max) blocco = max;

        port_outb(base_ctrl(canale), ATA_CTRL_NIEN);

        if (usa48) {
            port_outb(io + ATA_REG_DRIVE, (uint8_t)(0x40 | (unita ? 0x10 : 0)));
            ata_400ns(canale);

            /* ORDINE OBBLIGATORIO: prima il byte ALTO, poi quello BASSO
             * sulla stessa porta. Il dispositivo tiene due livelli e
             * invertirli produce indirizzi sbagliati SENZA errore. */
            port_outb(io + ATA_REG_SECCOUNT, (uint8_t)((blocco >> 8) & 0xFF));
            port_outb(io + ATA_REG_LBA0,     (uint8_t)((lba >> 24) & 0xFF));
            port_outb(io + ATA_REG_LBA1,     (uint8_t)((lba >> 32) & 0xFF));
            port_outb(io + ATA_REG_LBA2,     (uint8_t)((lba >> 40) & 0xFF));

            port_outb(io + ATA_REG_SECCOUNT, (uint8_t)(blocco & 0xFF));
            port_outb(io + ATA_REG_LBA0,     (uint8_t)(lba & 0xFF));
            port_outb(io + ATA_REG_LBA1,     (uint8_t)((lba >> 8) & 0xFF));
            port_outb(io + ATA_REG_LBA2,     (uint8_t)((lba >> 16) & 0xFF));

            port_outb(io + ATA_REG_COMMAND,
                      scrivi ? ATA_CMD_WRITE_PIO_EXT : ATA_CMD_READ_PIO_EXT);
        } else {
            port_outb(io + ATA_REG_DRIVE,
                      (uint8_t)(0xE0 | (unita ? 0x10 : 0) | ((lba >> 24) & 0x0F)));
            ata_400ns(canale);

            port_outb(io + ATA_REG_SECCOUNT, (uint8_t)blocco);
            port_outb(io + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
            port_outb(io + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
            port_outb(io + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));

            port_outb(io + ATA_REG_COMMAND,
                      scrivi ? ATA_CMD_WRITE_PIO : ATA_CMD_READ_PIO);
        }

        ata_400ns(canale);

        /* Un settore alla volta: il dispositivo alza DRQ per OGNI settore
         * del blocco, non una volta sola. Trasferire tutto dopo un solo
         * DRQ e' un errore classico che funziona finche' il blocco e' di
         * un settore e poi corrompe i dati. */
        for (s = 0; s < blocco; s++) {
            if (ata_attendi_drq(canale, ATA_TMO_DRQ_MS) < 0) return -1;

            /* ! UNA ISTRUZIONE, NON UN CICLO. Fino ad agosto 2026 qui
             * c'era un ciclo che chiamava port_inw 256 volte per settore:
             * duemila istruzioni per 512 byte, ed e' da li' che venivano
             * gli 0,75 MB/s misurati — non dal controller e non dal disco.
             *
             * L'ordine dei byte in memoria e' lo stesso che il ciclo
             * componeva a mano (basso, poi alto): su x86 `rep insw` fa
             * esattamente quello, quindi i dati sul disco non cambiano. */
            if (scrivi) port_outsw(io + ATA_REG_DATA, p, ATA_SECTOR_SIZE / 2);
            else        port_insw (io + ATA_REG_DATA, p, ATA_SECTOR_SIZE / 2);
            p += ATA_SECTOR_SIZE;
        }

        /* Dopo una scrittura il dispositivo puo' restare occupato: va
         * atteso prima del comando successivo. */
        st = ata_attendi_non_bsy(canale, ATA_TMO_BSY_MS);
        if (st < 0 || ((uint8_t)st & (ATA_SR_ERR | ATA_SR_DF))) {
            klog(LOG_ERROR, "ATA: %s fallita a lba=%u (stato=0x%02x)",
                 scrivi ? "scrittura" : "lettura", (uint32_t)lba, (uint8_t)st);
            return -1;
        }

        lba += blocco;
        n   -= blocco;
    }

    return 0;
}

int ata_read(int indice, uint64_t lba, uint32_t n, void *buf)
{
    return ata_rw(indice, lba, n, buf, 0);
}

int ata_write(int indice, uint64_t lba, uint32_t n, const void *buf)
{
    /* Il cast toglie il const: ata_rw non modifica il buffer quando
     * scrivi != 0, ma condividere una sola funzione vale il cast. */
    return ata_rw(indice, lba, n, (void *)buf, 1);
}

int ata_flush(int indice)
{
    const AtaDevice *d = ata_get_device(indice);
    uint16_t io;
    int      st;

    if (d == NULL || !d->presente || d->tipo != ATA_TYPE_ATA) return -1;

    io = base_io(d->canale);

    port_outb(io + ATA_REG_DRIVE, (uint8_t)(0xE0 | (d->unita ? 0x10 : 0)));
    ata_400ns(d->canale);
    port_outb(io + ATA_REG_COMMAND, d->lba48 ? ATA_CMD_FLUSH_EXT : ATA_CMD_FLUSH);
    ata_400ns(d->canale);

    st = ata_attendi_non_bsy(d->canale, ATA_TMO_BSY_MS);
    if (st < 0 || ((uint8_t)st & (ATA_SR_ERR | ATA_SR_DF))) return -1;

    return 0;
}

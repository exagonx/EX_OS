/* =============================================================================
 * kernel/fs/fat12.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#include "kernel.h"
#include "fat12.h"
#include "rtc.h"
#include "kmalloc.h"
#include "sched.h"
#include "isr.h"    /* irq_register_handler: sincronizzazione su IRQ6 */

/* =============================================================================
 * Costanti FAT12 1.44MB
 * ============================================================================= */
#define BYTES_PER_SECTOR    512
#define SECTORS_PER_CLUSTER 1
#define RESERVED_SECTORS    1
#define NUM_FATS            2
#define ROOT_ENTRY_COUNT    224
#define TOTAL_SECTORS       2880
#define SECTORS_PER_FAT     9
#define SECTORS_PER_TRACK   18
#define NUM_HEADS           2

#define FAT1_LBA            1
#define FAT2_LBA            (FAT1_LBA + SECTORS_PER_FAT)
#define ROOT_DIR_LBA        (FAT2_LBA + SECTORS_PER_FAT)
#define ROOT_DIR_SECTORS    ((ROOT_ENTRY_COUNT * 32) / BYTES_PER_SECTOR)
#define DATA_START_LBA      (ROOT_DIR_LBA + ROOT_DIR_SECTORS)
#define MAX_CLUSTERS        2848

/* Valori speciali cluster FAT12 */
#define FAT12_FREE          0x000
#define FAT12_RESERVED_MIN  0xFF0
#define FAT12_BAD           0xFF7
#define FAT12_END_MIN       0xFF8
#define FAT12_END           0xFFF

/* =============================================================================
 * Cache settori (4 slot, politica LRU semplice)
 * ============================================================================= */
/* Cache dei settori. Portata da 4 a 16 slot (luglio 2026): scrivere un
 * file tocca ciclicamente il settore dati, quello della directory e
 * quelli della FAT, e con soli 4 slot si continuavano a sfrattare a
 * vicenda. 16 x 512 byte = 8KB, trascurabili. */
#define CACHE_SLOTS         16

typedef struct {
    uint16_t lba;           /* LBA del settore in cache */
    uint8_t  valid;         /* Slot valido? */
    uint8_t  dirty;         /* Modificato (da scrivere)? */
    uint32_t last_use;      /* Tick ultimo accesso (per LRU) */
    uint8_t  data[BYTES_PER_SECTOR];
} CacheSlot;

static CacheSlot g_cache[CACHE_SLOTS];

/* =============================================================================
 * Stato FAT
 * ============================================================================= */
static uint8_t  g_fat[SECTORS_PER_FAT * BYTES_PER_SECTOR];  /* FAT1 in RAM */
static uint8_t  g_fat_loaded  = 0;
static uint8_t  g_fat_dirty   = 0;     /* FAT modificata, da riscrivere */
static uint8_t  g_drive       = 0;
static uint8_t  g_initialized = 0;

/* Root directory in RAM (14 settori = 7168 byte) */
static uint8_t g_root_dir[ROOT_DIR_SECTORS * BYTES_PER_SECTOR];
static uint8_t g_root_dirty = 0;

/* =============================================================================
 * Tabella file aperti
 *
 * fat12_open precedentemente ritornava come "handle" l'indice della entry
 * dentro g_root_dir, ma questo funziona solo per file nella root directory.
 * Per file in subdirectory (es. "/bin/sh"), fat12_find_path ritorna un
 * puntatore a una entry copiata su un buffer statico/locale, NON dentro
 * g_root_dir: calcolare l'indice come (entry - g_root_dir) produceva un
 * valore arbitrario fuori range, facendo fallire fat12_open con ENOENT.
 *
 * Soluzione: una vera tabella di file aperti che copia l'intera
 * Fat12DirEntry trovata (ovunque essa viva) in uno slot indipendente.
 * is_root_entry / root_index permettono comunque di scrivere indietro
 * l'entry aggiornata nella root dir quando il file vive lì (necessario
 * per fat12_write che aggiorna first_cluster/file_size).
 * ============================================================================= */
/* Deve stare al passo di VFS_MAX_OPEN: sul floppy ogni handle VFS ne
 * consuma uno di questi, e con il caricamento su richiesta gli eseguibili
 * restano aperti quanto i processi che li usano. */
#define MAX_OPEN_FILES   64

typedef struct {
    uint8_t        used;
    Fat12DirEntry  entry;        /* copia della entry */
    uint8_t        is_root_entry;/* 1 se l'entry vive in g_root_dir */
    uint32_t       root_index;   /* indice in g_root_dir (se is_root_entry) */

    /* Posizione della entry quando il file vive in una SOTTODIRECTORY
     * (luglio 2026). Prima erano assenti, e la conseguenza era che una
     * scrittura su un file di una sottodirectory non aggiornava mai la
     * sua entry: la dimensione restava quella vecchia e il file appariva
     * vuoto o troncato. Il commento nel codice lo chiamava
     * "limitazione nota". */
    uint16_t       dir_lba;      /* settore che contiene la entry */
    uint32_t       dir_slot;     /* indice della entry dentro quel settore (0-15) */
} OpenFile;

#define DIR_ENTRIES_PER_SECTOR  (BYTES_PER_SECTOR / 32)

static OpenFile g_open_files[MAX_OPEN_FILES];

/* =============================================================================
 * Floppy I/O tramite porte hardware (PIO, senza DMA)
 *
 * Il floppy controller 82077AA usa le porte:
 *   0x3F2 : Digital Output Register (DOR) — motor on/off, drive select
 *   0x3F4 : Main Status Register (MSR)    — stato controller
 *   0x3F5 : Data Register (FIFO)          — comandi e dati
 *   0x3F7 : Digital Input Register (DIR)  — detect disk change
 *
 * Sequenza lettura (comando READ SECTOR):
 *   1. Motore ON (DOR)
 *   2. Aspetta motore pronto (~300ms)
 *   3. Invia comando Read Normal Data (0x06 + flags)
 *   4. Invia parametri CHS
 *   5. Leggi settore da porta FIFO
 *   6. Leggi risultati
 *   7. Motore OFF (opzionale)
 * ============================================================================= */

#define FDC_DOR     0x3F2
#define FDC_MSR     0x3F4
#define FDC_FIFO    0x3F5
#define FDC_DIR     0x3F7
#define FDC_CCR     0x3F7  /* stessa porta del DIR, ma in scrittura */

/* Attende che il FDC sia pronto a ricevere/inviare dati */
static int fdc_wait_ready(void)
{
    uint32_t timeout = 100000;
    while (timeout--) {
        uint8_t msr = port_inb(FDC_MSR);
        if (msr & 0x80) return 0;  /* RQM=1: pronto */
    }
    klog(LOG_ERROR, "FAT12: timeout FDC MSR");
    return -1;
}

/* Invia un byte al FIFO del FDC */
static int fdc_send_byte(uint8_t b)
{
    if (fdc_wait_ready() != 0) return -1;
    uint8_t msr = port_inb(FDC_MSR);
    if (msr & 0x40) {
        klog(LOG_ERROR, "FAT12: FDC non in modalita' write (MSR=0x%02x)", msr);
        return -1;
    }
    port_outb(FDC_FIFO, b);
    return 0;
}

/* Legge un byte dal FIFO del FDC */
static int fdc_recv_byte(uint8_t *b)
{
    uint32_t timeout = 100000;
    uint8_t  msr = 0;

    while (timeout--) {
        msr = port_inb(FDC_MSR);
        if ((msr & 0xC0) == 0xC0) {   /* RQM=1, DIO=1: dati pronti */
            *b = port_inb(FDC_FIFO);
            return 0;
        }
    }

    klog(LOG_ERROR, "FAT12: timeout attesa dati FDC (MSR=0x%02x)", msr);
    return -1;
}

/* =============================================================================
 * fdc_sense_interrupt — Comando SENSE INTERRUPT STATUS (0x08)
 *
 * Dopo un reset del controller (o dopo qualunque comando che genera un
 * IRQ, incluso il reset stesso), il FDC resta "armato" in attesa che il
 * software invii questo comando per leggere ST0/PCN e sbloccare il FIFO.
 * Senza questa chiamata, il comando successivo (es. SPECIFY o READ) resta
 * bloccato indefinitamente in attesa di RQM, perche' il controller crede
 * di avere ancora un IRQ pendente da acknowledgiare.
 * Va inviato una volta per ogni IRQ implicito generato dal reset.
 * ============================================================================= */
static void fdc_sense_interrupt(uint8_t *st0_out, uint8_t *pcn_out)
{
    uint8_t st0 = 0, pcn = 0;
    fdc_send_byte(0x08);
    fdc_recv_byte(&st0);
    fdc_recv_byte(&pcn);
    if (st0_out) *st0_out = st0;
    if (pcn_out) *pcn_out = pcn;
}

/* =============================================================================
 * fdc_recalibrate — Comando RECALIBRATE (0x07)
 *
 * Riporta la testina al cilindro fisico 0. Indispensabile prima di
 * qualunque READ/WRITE quando l'implicit seek non e' abilitato (il
 * comando CONFIGURE con EIS non viene usato qui): senza recalibrate,
 * la testina resta dove l'ultima operazione (es. le letture del BIOS
 * durante stage1/stage2, che arrivano fino a cilindri >0) l'ha lasciata,
 * e il comando READ con C=0 nei parametri non muove fisicamente la
 * testina da solo, causando una desincronizzazione testina/parametri.
 * ============================================================================= */

/* =============================================================================
 * fdc_delay_ms — Attesa basata sul tempo reale (g_ticks, PIT a 100Hz)
 *
 * BUG REALE-HARDWARE (giugno 2026): i ritardi qui sotto (motore, seek
 * settling) erano implementati come loop di NOP a conteggio fisso
 * (`for (d=0; d<5000000; d++) nop;`). Il numero di iterazioni necessario
 * per un dato tempo dipende dalla velocita' della CPU — quei valori erano
 * di fatto tarati (anche solo implicitamente, testando) sulla CPU
 * virtuale di QEMU/VirtualBox. Su un Pentium II MMX reale, con un clock
 * e un'esecuzione per-istruzione completamente diversi, lo stesso loop
 * impiega un tempo diverso: se il motore non e' davvero a regime o la
 * testina non si e' davvero assestata, l'FDC puo' restituire dati
 * sbagliati SENZA segnalare un errore I/O — letture silenziosamente
 * corrotte (es. l'ELF della shell letto come tutto-zero, entry_point=0,
 * fault immediato a EIP=0x00000000 non appena lo scheduler ci salta).
 *
 * Fix: usare g_ticks (incrementato dall'IRQ0/PIT a 100Hz, kernel/sched/
 * sched.c — indipendente dalla velocita' della CPU, e' tempo reale,
 * guidato dal cristallo del PIT) invece di contare cicli CPU. Richiede
 * interrupt abilitati: vero per tutte le chiamate reali, dato che
 * fat12_init() (PASSO 13) gira sempre dopo l'abilitazione interrupt
 * (PASSO 12) in kernel_main.c.
 *
 * AGGIORNAMENTO (luglio 2026): la sincronizzazione su IRQ6 c'e' ora, ma
 * NON sostituisce questa funzione ovunque — vedi fdc_wait_irq() qui
 * sotto per quali attese sono diventate vere attese di evento e quali
 * restano necessariamente temporali (lo spin-up del motore non genera
 * nessun interrupt: non c'e' un evento da attendere).
 * ============================================================================= */
static void fdc_delay_ms(uint32_t ms)
{
    uint32_t ticks_needed = (ms + 9) / 10;
    uint32_t target = g_ticks + ticks_needed;
    uint32_t eflags;
    int      need_sti;

    /* Se gli interrupt sono disabilitati (IF=0 in EFLAGS), hlt non
     * termina mai — si sveglia solo su NMI/reset, non su IRQ0.
     * g_ticks non avanzerebbe mai e il loop sarebbe infinito.
     * Soluzione: abilitare gli interrupt temporaneamente per la durata
     * del delay, poi ripristinare lo stato originale.
     * È sicuro: l'FDC è single-threaded (mono-CPU, driver non rientrante).
     * Un eventuale context switch durante la finestra sti è accettabile:
     * il delay diventa più lungo, non più corto — floppy comunque pronto. */
    __asm__ volatile ("pushf; pop %0" : "=r"(eflags));
    need_sti = !(eflags & (1u << 9));   /* bit 9 = IF */

    if (need_sti) __asm__ volatile ("sti");

    while (g_ticks < target) {
        __asm__ volatile ("hlt");
    }

    if (need_sti) __asm__ volatile ("cli");
}

/* =============================================================================
 * Sincronizzazione su IRQ6 (luglio 2026)
 *
 * PERCHÉ SOLO PER SEEK/RECALIBRATE/RESET, E NON PER READ/WRITE
 *
 * Il 82077AA genera IRQ6 in due situazioni molto diverse:
 *
 *   a) fine di un comando SENZA fase di risultato — RECALIBRATE, SEEK, e
 *      l'IRQ implicito del reset. Qui l'interrupt e' l'UNICO segnale di
 *      completamento: il software deve poi inviare SENSE INTERRUPT per
 *      leggere ST0/PCN e disarmare il controller. Questi sono i casi in
 *      cui aspettare l'IRQ e' corretto, ed e' quello che facciamo.
 *
 *   b) durante la fase di esecuzione di READ/WRITE in modalita' PIO
 *      (SPECIFY con NDMA=1, come qui): il controller alza INT a OGNI
 *      byte pronto, non a fine comando. Aspettare "l'IRQ6" li' non
 *      significherebbe "settore trasferito" ma "c'e' un byte": inutile,
 *      dato che lo stesso segnale e' gia' leggibile da MSR/RQM, che e'
 *      cio' che il loop di trasferimento fa. Per READ/WRITE la
 *      sincronizzazione resta quindi il polling di MSR — non e' una
 *      semplificazione, e' il modello giusto per il PIO.
 *
 * BUG REALE CORRETTO DA QUESTO CAMBIAMENTO: prima, dopo RECALIBRATE e
 * SEEK, il codice aspettava `fdc_delay_ms(15)` e poi leggeva ST0/PCN. 15
 * ms bastano in emulazione (il seek e' istantaneo) ma NON su un drive
 * vero: un recalibrate a stroke pieno muove la testina di 80 tracce e
 * richiede centinaia di millisecondi. Passati i 15 ms, SENSE INTERRUPT
 * veniva letto con il seek ancora in corso, il controllo `pcn == 0`
 * falliva, e dopo 3 tentativi (45 ms in tutto, ancora troppo pochi) la
 * funzione usciva in silenzio lasciando la testina dove capitava. Le
 * READ successive usavano parametri C/H/S desincronizzati dalla
 * posizione fisica: dati sbagliati senza errore I/O segnalato — la
 * stessa firma del bug del Pentium II di giugno.
 *
 * L'attesa ha comunque un timeout: se l'IRQ non arriva (controller
 * assente, IRQ6 rubato, hardware rotto) si prosegue con un warning
 * invece di appendere il boot per sempre.
 * ============================================================================= */

/* Alzato dall'handler IRQ6, letto in contesto processo/boot: volatile, o
 * con -O2 il compilatore tiene il valore in un registro attraverso il
 * loop di attesa e non vede mai l'aggiornamento fatto dall'interrupt. */
static volatile uint8_t g_fdc_irq = 0;

/* =============================================================================
 * Posizione corrente della testina, -1 = sconosciuta.
 *
 * Serve a non pagare un SEEK (piu' 15ms di settle) per ogni settore: su
 * un floppy da 1.44MB un cilindro contiene 36 settori (18 per traccia x 2
 * testine), quindi una lettura sequenziale — che e' il caso di gran lunga
 * piu' comune, tutti i file vengono letti cosi' — sposta la testina una
 * volta ogni 36 settori invece che a ogni settore.
 *
 * Va invalidata (-1) ogni volta che la posizione fisica puo' essere
 * cambiata alle nostre spalle o essere incerta: reset, seek fallito,
 * errore di lettura/scrittura. Meglio un seek in piu' che leggere la
 * traccia sbagliata credendo di sapere dove siamo.
 * ============================================================================= */
static int g_fdc_cyl = -1;

/* =============================================================================
 * ! MODO SONDAGGIO: «c'e' un floppy?» NON E' UN GUASTO
 *
 * Questo driver serve anche da SONDA. vfs_init() decide di montare il CD
 * come radice proprio quando fat12_init() fallisce: dietro l'emulazione
 * floppy di El Torito non c'e' nessun controller, e quel fallimento e' il
 * segnale — non un errore.
 *
 * Il problema e' cosa si vedeva mentre lo diceva. La macchina dei
 * ritentativi qui sotto e' giusta per un floppy VERO che sbaglia una
 * lettura, e registra ogni tentativo perche' un disco che funziona solo
 * grazie ai ritentativi sta per morire. Ma su una macchina senza floppy
 * produceva cinque ERROR e quattro WARN a ogni avvio da CD, per una
 * condizione perfettamente normale — e chi legge il registro di un avvio
 * riuscito trovava dieci righe rosse.
 *
 * Con g_sondaggio alzato si fa UN tentativo e si tace: se il drive non
 * c'e', non ci sara' nemmeno al terzo tentativo, e il recalibrate fra uno
 * e l'altro costa mezzo secondo per niente.
 * ============================================================================= */
static int g_sondaggio = 0;

/* =============================================================================
 * ! DATA E ORA DI CREAZIONE, e prima erano zero
 *
 * Ogni file creato da EX-OS nasceva con data e ora a zero. Non dava
 * fastidio finche' nessuno le guardava; da quando `ls -d` le mostra, ogni
 * file appena scritto compariva con dei trattini al posto della data — e
 * quei trattini significano «questo volume non tiene le date», che di un
 * FAT12 e' falso.
 *
 * ! SE L'OROLOGIO NON RISPONDE SI LASCIA ZERO. Su hardware vecchio col
 * CMOS scarico rtc_read() fallisce: mettere una data inventata sarebbe
 * peggio di non metterne nessuna, perche' zero vuol dire «non la so» e
 * 1980 sembra un fatto.
 * ============================================================================= */
/* ! RITORNA I DUE VALORI IN UNA STRUTTURA, non attraverso puntatori ai
 * campi della voce di directory: quella e' PACKED, e prendere l'indirizzo
 * di un suo membro da' un puntatore che l'architettura non garantisce
 * allineato. Su i386 funzionerebbe; il giorno che questo codice girasse
 * altrove sarebbe un fault, e il compilatore lo dice. */
typedef struct { uint16_t data, ora; } Fat12Istante;

static Fat12Istante fat12_ora_corrente(void)
{
    Fat12Istante r = { 0, 0 };
    RtcTime      t;

    if (rtc_read(&t) != 0) return r;
    if (t.anno < 1980u || t.anno > 2107u) return r;

    r.data = (uint16_t)(((t.anno - 1980u) << 9) | (t.mese << 5) | t.giorno);
    r.ora  = (uint16_t)((t.ora << 11) | (t.minuto << 5) | (t.secondo / 2u));
    return r;
}

static void fdc_irq6_handler(InterruptFrame *frame)
{
    (void)frame;
    g_fdc_irq = 1;
    /* Nient'altro qui. Invariante del kernel (vedi irq_handler in
     * kernel/arch/x86/isr.c): l'EOI e' gia' stato inviato PRIMA del
     * dispatch, e qualunque handler puo' non ritornare — quindi un
     * handler deve limitarsi a registrare l'evento. */
}

/* Azzera il flag PRIMA di inviare un comando: un IRQ residuo di
 * un'operazione precedente (tipicamente quello di fine fase di
 * esecuzione di una READ) farebbe altrimenti ritornare subito l'attesa
 * successiva, che crederebbe completato un seek appena iniziato. */
static void fdc_irq_clear(void)
{
    uint32_t eflags;

    /* Preserva IF del chiamante invece di riabilitare incondizionatamente.
     * Oggi tutti i chiamanti girano a interrupt abilitati, ma un
     * interrupts_enable() secco qui sarebbe una trappola per chi in
     * futuro invocasse un comando FDC da dentro una sezione critica: gli
     * riaprirebbe gli interrupt sotto i piedi senza che se ne accorga. */
    __asm__ volatile ("pushf; pop %0" : "=r"(eflags));
    interrupts_disable();
    g_fdc_irq = 0;
    if (eflags & (1u << 9)) interrupts_enable();   /* bit 9 = IF */
}

/* Attende l'IRQ6 di fine comando. Ritorna 0 se arrivato, -1 su timeout.
 * Preserva lo stato IF del chiamante, come fdc_delay_ms. */
static int fdc_wait_irq(uint32_t timeout_ms)
{
    uint32_t target = g_ticks + (timeout_ms + 9) / 10;
    uint32_t eflags;
    int      caller_had_if;
    int      got;

    __asm__ volatile ("pushf; pop %0" : "=r"(eflags));
    caller_had_if = (eflags & (1u << 9)) != 0;   /* bit 9 = IF */

    for (;;) {
        interrupts_disable();
        if (g_fdc_irq)          { got = 1; break; }
        if (g_ticks >= target)  { got = 0; break; }

        /* `sti; hlt` e' atomica rispetto agli interrupt: STI ritarda il
         * riconoscimento degli interrupt fino a DOPO l'istruzione
         * successiva, quindi un IRQ6 che arrivasse "in mezzo" non viene
         * perso — sveglia l'hlt invece di precederlo. Testare il flag a
         * interrupt disabilitati e poi dormire con questa coppia elimina
         * la finestra di lost wakeup. */
        __asm__ volatile ("sti; hlt");
    }

    g_fdc_irq = 0;
    if (caller_had_if) interrupts_enable();
    return got ? 0 : -1;
}

static void fdc_recalibrate(void)
{
    uint8_t st0, pcn;
    int     tries;

    for (tries = 0; tries < 3; tries++) {
        fdc_irq_clear();

        /* Fase di comando protetta da cli, per lo stesso motivo documentato
         * in fdc_rw_sector: il controller ha un timeout interno di ~500us
         * fra un byte di comando e il successivo, e un IRQ0 che cadesse in
         * mezzo glielo farebbe abbandonare. Qui i byte sono due soli e la
         * finestra e' stretta, ma su hardware reale "stretta" non vuol dire
         * "inesistente" — e un RECALIBRATE perso lascia la testina dove
         * capita, che e' esattamente il guasto silenzioso piu' difficile da
         * diagnosticare. In emulazione non si manifesta mai. */
        interrupts_disable();
        fdc_send_byte(0x07);     /* RECALIBRATE */
        fdc_send_byte(g_drive);  /* unita' */
        interrupts_enable();

        /* Un recalibrate a stroke pieno (80 tracce) su drive reale sta
         * sotto il mezzo secondo; 1s di margine copre anche i drive
         * lenti senza appendere il boot se il controller e' morto. */
        if (fdc_wait_irq(1000) != 0) {
            klog(LOG_WARN, "FAT12: timeout IRQ6 su RECALIBRATE (tentativo %d)",
                 tries + 1);
        }

        fdc_sense_interrupt(&st0, &pcn);

        /* ST0 bit 5 (SE) = Seek End, bit 4 (EC) = Equipment Check
         * (testina mai arrivata alla traccia 0). Il solo pcn==0 non
         * basta: e' 0 anche se il comando non e' mai partito. */
        if ((st0 & 0x20) && !(st0 & 0x10) && pcn == 0) {
            g_fdc_cyl = 0;   /* posizione ora nota e confermata */
            return;
        }
    }

    g_fdc_cyl = -1;
    klog(LOG_WARN, "FAT12: RECALIBRATE non confermato dopo 3 tentativi "
         "(ST0=0x%02x PCN=%u) - la testina potrebbe non essere al cilindro 0",
         st0, pcn);
}

/* =============================================================================
 * fdc_seek — Comando SEEK (0x0F): muove la testina al cilindro richiesto
 * ============================================================================= */
static int fdc_seek(uint8_t cyl, uint8_t head)
{
    uint8_t st0 = 0, pcn = 0;

    fdc_irq_clear();

    /* Fase di comando protetta da cli — vedi fdc_recalibrate e fdc_rw_sector. */
    interrupts_disable();
    fdc_send_byte(0x0F);                          /* SEEK */
    fdc_send_byte((uint8_t)(head << 2 | g_drive));
    fdc_send_byte(cyl);
    interrupts_enable();

    /* 500ms: copre uno spostamento a stroke pieno su drive reale. */
    if (fdc_wait_irq(500) != 0) {
        klog(LOG_WARN, "FAT12: timeout IRQ6 su SEEK (cyl=%u head=%u)", cyl, head);
    }

    fdc_sense_interrupt(&st0, &pcn);

    /* SE=1 (seek concluso), EC=0, e la testina e' davvero dove l'abbiamo
     * mandata. Il chiamante decide se procedere comunque. */
    if (!(st0 & 0x20) || (st0 & 0x10) || pcn != cyl) {
        klog(LOG_WARN, "FAT12: SEEK non confermato (cyl richiesto=%u ST0=0x%02x PCN=%u)",
             cyl, st0, pcn);
        return -1;
    }

    /* Head Settle Time: il SEEK termina quando la testina e' arrivata,
     * non quando ha smesso di oscillare. Lo SPECIFY qui usa HLT=4ms; il
     * margine standard per un floppy da 1.44MB e' ~15ms e non ha un
     * interrupt associato — resta necessariamente un'attesa temporale. */
    fdc_delay_ms(15);
    return 0;
}

static int fdc_seek_if_needed(uint8_t cyl, uint8_t head)
{
    if (g_fdc_cyl == (int)cyl) return 0;   /* gia' sul cilindro giusto */

    if (fdc_seek(cyl, head) != 0) {
        g_fdc_cyl = -1;
        return -1;
    }
    g_fdc_cyl = (int)cyl;
    return 0;
}

/* Motore floppy ON/OFF */
/* Il motore e' gia' in rotazione? Non viene mai spento (fdc_motor_off non
 * e' chiamata da nessuno), quindi una volta avviato resta acceso fino al
 * prossimo reset del controller. */
static uint8_t g_motor_running = 0;

/* =============================================================================
 * IL TIMER DI INATTIVITA' DEL MOTORE
 *
 * ! IL MOTORE NON PUO' RESTARE ACCESO, E NON E' UN DETTAGLIO DI CONSUMO. Su un
 * floppy la testina e' APPOGGIATA al supporto mentre il disco gira: un
 * dischetto lasciato in rotazione per ore non si scalda soltanto, si consuma —
 * e il drive di un Pentium II ha vent'anni e non se ne trovano piu'.
 *
 * fat12_sync() lo spegneva gia' a ogni sys_exit, cioe' alla fine di ogni
 * comando. Ma le uscite IN ERRORE lo lasciano acceso di proposito, per non
 * ripagare i 300 ms di spin-up al tentativo successivo — e su un drive che
 * sbaglia sempre «il tentativo successivo» non arriva mai. Il risultato e'
 * esattamente il caso peggiore: il supporto gira per ore proprio sulla
 * macchina il cui drive sta gia' dando errori.
 *
 * ! PERCHE' UN CONTATORE E NON UN SI'/NO. Le operazioni si annidano davvero:
 * una lettura che manca la cache puo' dover riversare prima uno slot sporco,
 * cioe' una SCRITTURA dentro una LETTURA. Con una variabile a due stati la
 * fine di quella interna direbbe «libero» mentre l'esterna sta ancora
 * lavorando, e il tick spegnerebbe il motore in mezzo a un trasferimento.
 *
 * ! E IL CONTROLLO STA NEL TICK, NON NEL DRIVER. E' l'unico posto che continua
 * a girare quando il driver e' fermo in attesa — che e' proprio la situazione
 * da cui bisogna uscire. Il commento che stava qui diceva di NON aggiungere un
 * timer di inattivita' perche' l'idle task avrebbe potuto spegnere il motore
 * durante un trasferimento: e' vero senza un contatore di occupazione, ed e'
 * la ragione per cui il contatore c'e'.
 * ========================================================================== */
#define FDC_INATTIVITA_TICK  200    /* 2 secondi a 100 Hz */

static volatile uint32_t g_fdc_occupato = 0;   /* quante operazioni in corso */
static volatile uint32_t g_fdc_ultimo   = 0;   /* tick dell'ultima attivita' */

static void fdc_entra(void)
{
    g_fdc_occupato++;
    g_fdc_ultimo = g_ticks;
}

static void fdc_esce(void)
{
    if (g_fdc_occupato > 0) g_fdc_occupato--;
    g_fdc_ultimo = g_ticks;
}

/* =============================================================================
 * fdc_motor_on — avvia il motore, aspettando solo se serve davvero
 *
 * PRESTAZIONI (luglio 2026): questa funzione aspettava 300 ms A OGNI
 * CHIAMATA, cioe' a ogni accesso a un settore, nonostante il commento
 * dicesse "no-op se il motore e' gia' in moto". In emulazione il costo si
 * nota poco; su un drive vero significa 300 ms moltiplicati per ogni
 * settore letto.
 *
 * Il conto per un avvio: FAT (9 settori) + root directory (14) +
 * /dev/kbd.drv (~27) + /bin/sh (~27) piu' le ricerche nelle directory,
 * oltre 80 accessi — piu' di 24 secondi di sola attesa del motore, a cui
 * si aggiungono i tempi reali di seek e lettura. Su hardware reale un
 * avvio del genere sembra un sistema bloccato, ed e' facile spegnere
 * prima che la shell compaia.
 *
 * I 300 ms servono davvero, ma UNA volta: sono il tempo di
 * stabilizzazione del motore, non un tempo per operazione.
 * ============================================================================= */
static void fdc_motor_on(void)
{
    if (g_motor_running) return;

    port_outb(FDC_DOR, 0x1C);  /* Drive 0, motor 0 ON, DMA+IRQ enable, !reset */
    fdc_delay_ms(300);         /* stabilizzazione, tempo reale non cicli CPU */
    g_motor_running = 1;
}

static void fdc_motor_off(void)
{
    port_outb(FDC_DOR, 0x0C);  /* Motor OFF, drive 0 */
    g_motor_running = 0;

    /* Posizione della testina non piu' garantita: alcuni drive rilasciano
     * lo stepper quando il motore si ferma. Costa al massimo un SEEK in
     * piu' al prossimo accesso, e vale l'invariante gia' scritto su
     * g_fdc_cyl — meglio un seek in piu' che leggere la traccia sbagliata
     * credendo di sapere dove siamo. */
    g_fdc_cyl = -1;
}

/* =============================================================================
 * fat12_motor_off — spegnimento del motore visibile dall'esterno
 *
 * PERCHÉ ESISTE (2026-07-31). Fino a questa sessione fdc_motor_off() non
 * era chiamata da nessuno — era marcata __attribute__((unused)) — quindi
 * il motore, una volta acceso al primo accesso al disco, restava acceso
 * per sempre. In emulazione e' invisibile. Su una macchina vera il LED del
 * drive resta acceso fisso e il dischetto continua a girare sotto le
 * testine per tutta la sessione: consuma il supporto e il drive, che su un
 * Pentium II hanno vent'anni abbondanti. Segnalato dall'utente come "il
 * drive rimane attivo come se stesse leggendo in continuazione".
 *
 * Lo spegnimento non e' automatico di proposito: non c'e' un timer di
 * inattivita' e NON va aggiunto alla leggera, perche' il driver FDC non e'
 * rientrante e un processo bloccato dentro fdc_wait_irq() lascia girare
 * l'idle task — che spegnerebbe il motore in mezzo a un trasferimento.
 * Si spegne quindi solo da punti sincroni in cui e' certo che il disco sia
 * fermo: fine del boot e fine di una sincronizzazione.
 * ============================================================================= */
void fat12_motor_off(void)
{
    if (!g_motor_running) return;
    fdc_motor_off();
}

/* =============================================================================
 * LA DIAGNOSTICA PASSO PASSO — «la meccanica fa quello che le diciamo?»
 *
 * PERCHE' STA NEL KERNEL E NON IN UN PROGRAMMA. Il controller del floppy e' del
 * kernel: un programma in ring3 potrebbe prendersi le porte 0x3F0-0x3F7 con
 * ioport_bind, ma NON puo' ricevere l'IRQ6 — quella linea ha un gestore
 * kernel, e il dispatcher lo serve prima di guardare i driver ring3. Una
 * diagnostica che non vede l'IRQ6 non puo' dire niente sul problema piu'
 * comune, che e' proprio l'IRQ6 che non arriva. E soprattutto: proverebbe un
 * ALTRO codice, non quello che il sistema usa davvero.
 *
 * Qui invece si chiamano le STESSE funzioni che leggono i file. Se la
 * diagnostica passa e la lettura no, la differenza e' nel supporto, non nel
 * driver.
 *
 * ! SI SCRIVE CON kprintf E NON CON klog. klog obbedisce a `loglevel`: una
 * diagnostica che sparisce perche' il log e' basso e' una diagnostica che non
 * c'e' quando serve. kprintf va sulla console e sulla seriale sempre.
 *
 * ! E SI FA CON IL SISTEMA ACCESO, sulla stessa unita' da cui si sta girando.
 * Percio' ogni passo che muove la testina finisce con una ricalibratura, e
 * alla fine si invalida la posizione creduta: il primo accesso successivo
 * fara' un SEEK in piu' e ripartira' allineato.
 * ========================================================================== */
static int fdc_rw_sector(uint16_t lba, uint8_t *buf, int write);

static void diag_passo(int n, const char *cosa)
{
    kprintf("\n[PASSO %d] %s\n", n, cosa);
}

static void diag_esito(int ok, const char *dettaglio)
{
    kprintf("          %s  %s\n", ok ? "OK   " : "NO   ", dettaglio);
}

/* Registra un passo nel taccuino che finira' nel log. Se il chiamante non ha
 * dato un buffer, la prova si fa lo stesso e qui non si scrive niente. */
static FdPasso    *g_diag_out = 0;
static unsigned int g_diag_max = 0;
static unsigned int g_diag_n   = 0;

static void diag_nota(unsigned int passo, unsigned int codice, int esito,
                      unsigned int a, unsigned int b)
{
    if (!g_diag_out || g_diag_n >= g_diag_max) return;

    g_diag_out[g_diag_n].passo  = passo;
    g_diag_out[g_diag_n].codice = codice;
    g_diag_out[g_diag_n].esito  = esito;
    g_diag_out[g_diag_n].a      = a;
    g_diag_out[g_diag_n].b      = b;
    g_diag_n++;
}

int fat12_diagnostica(FdPasso *out, unsigned int max)
{
    static const uint8_t cilindri[] = { 10, 40, 79, 0 };
    uint8_t  st0 = 0, pcn = 0, msr;
    uint8_t  settore[BYTES_PER_SECTOR];
    int      guasti = 0;
    int      i, r;

    g_diag_out = out;
    g_diag_max = out ? max : 0;
    g_diag_n   = 0;

    fdc_entra();        /* il tick non tocchera' il motore mentre proviamo */

    kprintf("\n=== PROVA DEL FLOPPY, PASSO PASSO ===\n");
    kprintf("Guardare e ASCOLTARE il drive: ogni passo dice cosa deve\n");
    kprintf("succedere fisicamente. Se il rumore non corrisponde, il\n");
    kprintf("problema e' fra il controller e la meccanica.\n");

    /* --- 1 ------------------------------------------------------------- */
    diag_passo(1, "Il controller risponde? (lettura di MSR, niente si muove)");
    msr = port_inb(FDC_MSR);
    kprintf("          MSR=0x%02x  (atteso 0x80: pronto, in attesa di comandi)\n", msr);
    if (msr == 0xFF) {
        diag_esito(0, "nessun controller a 0x3F0-0x3F7");
        diag_nota(1, FD_MSR, -1, msr, 0);
        guasti++;
        fdc_esce();
        return (int)g_diag_n;
    }
    diag_esito((msr & 0x80) != 0, (msr & 0x80) ? "il controller c'e' ed e' pronto"
                                               : "il controller c'e' ma non e' pronto");
    diag_nota(1, FD_MSR, (msr & 0x80) ? 0 : -1, msr, 0);
    if (!(msr & 0x80)) guasti++;

    /* --- 2 ------------------------------------------------------------- */
    diag_passo(2, "Reset del controller: deve arrivare un IRQ6");
    kprintf("          (nessun movimento: e' solo elettronica)\n");
    fdc_irq_clear();
    g_motor_running = 0;
    port_outb(FDC_DOR, 0x00);
    fdc_delay_ms(2);
    port_outb(FDC_DOR, 0x0C);
    r = fdc_wait_irq(500);
    diag_esito(r == 0, r == 0 ? "IRQ6 ricevuto: la linea 6 arriva alla CPU"
                              : "IRQ6 NON ricevuto: e' qui che si rompe tutto il resto");
    diag_nota(2, FD_RESET_IRQ, r == 0 ? 0 : -1, 0, 0);
    if (r != 0) guasti++;

    /* --- 3 ------------------------------------------------------------- */
    diag_passo(3, "SENSE INTERRUPT per le quattro unita' possibili");
    for (i = 0; i < 4; i++) {
        fdc_sense_interrupt(&st0, &pcn);
        kprintf("          unita' %d: ST0=0x%02x PCN=%u\n", i, st0, pcn);
        diag_nota(3, FD_SENSE, 0, (unsigned int)i, st0);
    }
    port_outb(FDC_CCR, 0x00);
    fdc_send_byte(0x03);
    fdc_send_byte(0xDF);
    fdc_send_byte(0x03);
    diag_esito(1, "velocita' 500 kbps e tempi impostati");

    /* --- 4 ------------------------------------------------------------- */
    diag_passo(4, "Motore ACCESO: la spia del drive deve accendersi ORA");
    fdc_motor_on();
    fdc_delay_ms(1500);
    diag_esito(1, "se la spia e' spenta, il DOR non comanda questo drive");
    diag_nota(4, FD_MOTORE_ON, 0, 0, 0);

    /* --- 5 ------------------------------------------------------------- */
    diag_passo(5, "RECALIBRATE: la testina torna a cilindro 0 (rumore secco)");
    fdc_irq_clear();
    fdc_send_byte(0x07);
    fdc_send_byte(0x00);
    r = fdc_wait_irq(3000);
    fdc_sense_interrupt(&st0, &pcn);
    kprintf("          ST0=0x%02x PCN=%u  (atteso ST0 con bit 0x20, PCN=0)\n",
            st0, pcn);
    if (r != 0) {
        diag_esito(0, "nessun IRQ6: la testina puo' essersi mossa lo stesso");
        diag_nota(5, FD_RECAL, -1, st0, pcn);
        guasti++;
    } else if (!(st0 & 0x20) || pcn != 0) {
        diag_esito(0, "il controller dice che la testina NON e' a cilindro 0");
        diag_nota(5, FD_RECAL, -1, st0, pcn);
        guasti++;
    } else {
        diag_esito(1, "testina a cilindro 0, confermato dal controller");
        diag_nota(5, FD_RECAL, 0, st0, pcn);
    }

    /* --- 6 ------------------------------------------------------------- */
    diag_passo(6, "SEEK a 10, 40, 79 e 0: si deve sentire la testina che va");
    for (i = 0; i < (int)(sizeof(cilindri) / sizeof(cilindri[0])); i++) {
        uint8_t c = cilindri[i];

        kprintf("          -> cilindro %u ...", c);
        fdc_irq_clear();
        fdc_send_byte(0x0F);            /* SEEK */
        fdc_send_byte(0x00);            /* testina 0, unita' 0 */
        fdc_send_byte(c);
        r = fdc_wait_irq(3000);
        fdc_sense_interrupt(&st0, &pcn);

        if (r != 0) {
            kprintf(" IRQ6 assente (ST0=0x%02x PCN=%u)\n", st0, pcn);
            diag_nota(6, FD_SEEK, -1, c, pcn);
            guasti++;
        } else if (!(st0 & 0x20)) {
            kprintf(" seek non concluso (ST0=0x%02x)\n", st0);
            diag_nota(6, FD_SEEK, -1, c, pcn);
            guasti++;
        } else if (pcn != c) {
            kprintf(" la testina dice di essere a %u, non a %u\n", pcn, c);
            diag_nota(6, FD_SEEK, -1, c, pcn);
            guasti++;
        } else {
            kprintf(" arrivata (PCN=%u)\n", pcn);
            diag_nota(6, FD_SEEK, 0, c, pcn);
        }
        fdc_delay_ms(300);
    }
    g_fdc_cyl = -1;

    /* --- 7 ------------------------------------------------------------- */
    diag_passo(7, "Lettura del settore di avvio (cilindro 0): la firma 0x55AA");
    r = fdc_rw_sector(0, settore, 0);
    if (r != 0) {
        diag_esito(0, "il settore 0 non si legge");
        diag_nota(7, FD_LETTURA, -1, 0, 0);
        guasti++;
    } else if (settore[510] != 0x55 || settore[511] != 0xAA) {
        kprintf("          ultimi due byte: 0x%02x 0x%02x\n",
                settore[510], settore[511]);
        diag_esito(0, "letto, ma non e' un settore di avvio: supporto sbagliato?");
        diag_nota(7, FD_LETTURA, -1, 0, 0);
        guasti++;
    } else {
        diag_esito(1, "settore 0 letto e riconosciuto");
        diag_nota(7, FD_LETTURA, 0, 0, 1);
    }

    /* --- 8 ------------------------------------------------------------- */
    diag_passo(8, "Dieci letture del cilindro 63 (LBA 2268): la meta' del disco");
    kprintf("          E' la traccia dove i drive stanchi cominciano a sbagliare.\n");
    {
        int falliti = 0;

        for (i = 0; i < 10; i++) {
            if (fdc_rw_sector(2268, settore, 0) != 0) falliti++;
        }
        kprintf("          %d letture su 10 fallite dopo tutti i ritenti\n", falliti);
        diag_nota(8, FD_RIPETUTA, falliti == 0 ? 0 : -1, 2268,
                  (unsigned int)falliti);
        if (falliti == 0) {
            diag_esito(1, "il cilindro 63 si legge sempre");
        } else {
            diag_esito(0, "supporto o testina non affidabili a meta' disco");
            guasti++;
        }
    }

    /* --- 9 ------------------------------------------------------------- */
    diag_passo(9, "Motore SPENTO: la spia deve spegnersi ORA");
    fdc_motor_off();
    fdc_delay_ms(1000);
    diag_esito(1, "se resta accesa, il DOR non spegne questo drive");
    diag_nota(9, FD_MOTORE_OFF, 0, 0, 0);

    /* --- 10 ------------------------------------------------------------ */
    diag_passo(10, "Motore fermo, poi si rilegge: deve riaccendersi da solo");
    kprintf("          E' il percorso che il timer di inattivita' crea a ogni\n");
    kprintf("          pausa: il disco si ferma, e la lettura dopo deve\n");
    kprintf("          rimetterlo in moto, aspettare che sia a regime e\n");
    kprintf("          ritrovare la traccia. Se qui sbaglia, la macchina\n");
    kprintf("          funziona finche' si lavora e sbaglia dopo una pausa —\n");
    kprintf("          il guasto piu' difficile da collegare alla causa.\n");

    fdc_delay_ms(2000);         /* il disco deve fermarsi davvero */
    kprintf("          ora la spia deve essere SPENTA; rileggo il settore 0\n");

    settore[510] = 0;
    settore[511] = 0;
    r = fdc_rw_sector(0, settore, 0);

    if (r != 0) {
        diag_esito(0, "dopo la fermata il settore 0 non si legge piu'");
        diag_nota(10, FD_RIACCESO, -1, 0, 0);
        guasti++;
    } else if (settore[510] != 0x55 || settore[511] != 0xAA) {
        diag_esito(0, "riletto, ma i dati non tornano: rispin troppo corto?");
        diag_nota(10, FD_RIACCESO, -1, 0, 0);
        guasti++;
    } else {
        diag_esito(1, "il motore e' ripartito e la lettura e' giusta");
        diag_nota(10, FD_RIACCESO, 0, 0, 1);
    }

    /* E si rispegne: la prova non deve lasciare il disco a girare. */
    fdc_motor_off();

    /* --- fine ---------------------------------------------------------- */
    kprintf("\n=== %d passi con problemi ===\n", guasti);
    if (guasti == 0) {
        kprintf("La meccanica risponde ai comandi. Se la lettura dei file\n");
        kprintf("sbaglia lo stesso, il sospetto e' il singolo dischetto.\n");
    } else {
        kprintf("Il primo passo con NO e' quello da guardare: i successivi\n");
        kprintf("sbagliano quasi sempre di conseguenza.\n");
    }

    g_fdc_cyl = -1;     /* dove sia la testina non lo sa piu' nessuno */
    fdc_esce();

    /* ! SI RENDE QUANTI PASSI SONO STATI REGISTRATI, non quanti sono andati
     * male. Chi chiama i guasti se li conta guardando `esito`, e cosi' puo'
     * anche dire QUALI: un numero solo direbbe che qualcosa non va senza dire
     * cosa, e questa e' una diagnostica. */
    return (int)g_diag_n;
}

/* =============================================================================
 * fat12_motor_tick — chiamata dal tick del timer, cento volte al secondo
 *
 * Il perche' sta accanto a FDC_INATTIVITA_TICK. Qui c'e' solo la regola: se il
 * motore gira, se nessuno sta usando il controller, e se sono passati due
 * secondi dall'ultima attivita', si spegne.
 *
 * ! GIRA DENTRO L'INTERRUPT DEL TIMER, quindi non puo' fare altro che una
 * out su una porta. fdc_motor_off() e' esattamente questo: una scrittura sul
 * DOR piu' due assegnazioni. Niente attese, niente log — un klog qui
 * stamperebbe una riga ogni volta che il disco si ferma.
 *
 * ! E MENTRE SI E' OCCUPATI SI RINFRESCA IL TIMBRO. Un trasferimento lungo —
 * un seek che ritenta, una traccia che va riletta — supera i due secondi
 * senza che nessuno tocchi g_fdc_ultimo: senza questa riga il conto
 * scadrebbe mentre l'operazione e' in corso, e il motore si spegnerebbe
 * appena il contatore torna a zero, cioe' un istante prima dell'operazione
 * successiva invece che due secondi dopo l'ultima.
 * ========================================================================== */
void fat12_motor_tick(void)
{
    if (!g_motor_running) return;

    if (g_fdc_occupato > 0) {
        g_fdc_ultimo = g_ticks;
        return;
    }

    if ((uint32_t)(g_ticks - g_fdc_ultimo) < FDC_INATTIVITA_TICK) return;

    fdc_motor_off();
}

/* Converti LBA → CHS per floppy 1.44MB */
static void lba_to_chs(uint16_t lba, uint8_t *cyl, uint8_t *head, uint8_t *sec)
{
    *sec  = (uint8_t)((lba % SECTORS_PER_TRACK) + 1);
    *head = (uint8_t)((lba / SECTORS_PER_TRACK) % NUM_HEADS);
    *cyl  = (uint8_t)((lba / SECTORS_PER_TRACK) / NUM_HEADS);
}

/* =============================================================================
 * TRASFERIMENTO SETTORI VIA DMA (canale 2)
 *
 * PERCHÉ IL DMA E NON IL PIO (luglio 2026)
 *
 * Fino a questa sessione il driver trasferiva i settori in PIO: 512
 * letture o scritture della porta FIFO, con SPECIFY ND=1. Le LETTURE
 * funzionavano; le SCRITTURE no, e nessuno se n'era accorto perché
 * nessun programma aveva mai scritto un file — il primo è stato
 * /bin/textline.
 *
 * Misurato sperimentalmente su QEMU 8.2.2: il controller accetta
 * esattamente 512 byte, li scrive davvero sul supporto (verificato
 * confrontando l'immagine floppy prima e dopo: il settore cambia), e poi
 * resta in MSR=0x30 — RQM=0, EXM=1, CMD BSY=1 — per sempre. Non entra
 * mai in fase di risultato, quindi il comando non si conclude e il
 * controller resta appeso per tutte le operazioni successive. QEMU non
 * segnala nulla nemmeno con -d guest_errors: semplicemente non porta a
 * termine una scrittura non-DMA.
 *
 * Il DMA risolve il problema alla radice ed è comunque il modo corretto
 * di usare un FDC:
 *   - il trasferimento non passa dalla CPU, quindi non serve più tenere
 *     gli interrupt disabilitati per non perdere byte (era il `cli` che
 *     proteggeva il ciclo PIO);
 *   - la fine del comando è segnalata dall'IRQ6, che dalla sessione
 *     precedente sappiamo già attendere con fdc_wait_irq();
 *   - toglie l'ostacolo principale al futuro driver floppy in ring3: un
 *     processo utente non può disabilitare gli interrupt, e senza DMA
 *     verrebbe preemptato in mezzo al trasferimento causando un overrun.
 *
 * VINCOLI DEL DMA ISA, e come sono soddisfatti
 *
 * Il controller 8237 indirizza 24 bit (primi 16MB) e non può attraversare
 * un confine di 64KB fisici. Il buffer è quindi:
 *   - statico nel kernel, che vive a 1MB con identità di mapping
 *     (paging_init mappa 0x0-0x3FFFFF): indirizzo virtuale = fisico, e
 *     ampiamente sotto i 16MB;
 *   - allineato a 512 byte: un blocco di 512 byte allineato a 512 non può
 *     attraversare un confine di 64KB, perché 65536 è multiplo di 512.
 * ============================================================================= */

/* =============================================================================
 * fdc_drena — butta via i byte di risultato che nessuno ha letto
 *
 * ! E' LA CURA DELLA VALANGA, e la valanga si riconosce dal log:
 *
 *     FAT12: IRQ6 non ricevuto su READ LBA=2268
 *     FAT12: fase di risultato incompleta su READ LBA=2268
 *     FAT12: FDC non in modalita' write (MSR=0xd0)
 *     ... e da li' in poi sbaglia tutto
 *
 * Un comando READ finisce in FASE DI RISULTATO: sette byte che il software
 * DEVE leggere. Se l'IRQ6 non arriva in tempo e il driver rinuncia a meta',
 * quei byte restano nel FIFO. Il comando successivo trova MSR con DIO=1 —
 * «ho da darti dei dati», non «dammi un comando» — e fallisce. Fallisce anche
 * quello dopo, e quello dopo ancora: un solo errore di lettura diventa una
 * sessione intera di errori, e il primo messaggio non e' piu' riconoscibile
 * in mezzo agli altri.
 *
 * Leggere e buttare i byte rimasti rimette il controller in stato di comando.
 * Costa UNA lettura di MSR quando non c'e' niente da buttare, cioe' sempre
 * tranne che dopo un guasto.
 *
 * ! IL LIMITE C'E' PERCHE' UN CONTROLLER GUASTO PUO' NON SMETTERE MAI. Un
 * ciclo senza tetto qui sarebbe un blocco della macchina dentro il gestore di
 * un errore, che e' il posto peggiore dove metterne uno.
 * ========================================================================== */
static int fdc_drena(void)
{
    int buttati = 0;

    while (buttati < 32) {
        uint8_t msr = port_inb(FDC_MSR);

        /* RQM=1 e DIO=1: il controller ha un byte da darci. */
        if ((msr & 0xC0) != 0xC0) break;

        (void)port_inb(FDC_FIFO);
        buttati++;
    }

    if (buttati > 0) {
        klog(LOG_WARN, "FAT12: buttati %d byte di risultato rimasti nel FIFO "
             "(il comando precedente non era stato concluso)", buttati);
    }
    return buttati;
}

/* =============================================================================
 * fdc_reset_controller — la ripartenza da zero del controller
 *
 * Era scritta dentro fat12_init e non si poteva richiamare. Serve invece
 * proprio quando le cose vanno male: dopo due tentativi falliti il problema
 * non e' piu' la testina fuori posto — quella la rimette il RECALIBRATE — ma
 * il controller in uno stato che nessuna sequenza normale rimette a posto.
 *
 * ! NON SI FA AL PRIMO ERRORE. Costa un reset, quattro SENSE INTERRUPT e 300
 * ms di rispin del motore: su un supporto che sbaglia ogni tanto — il caso
 * normale di un dischetto vecchio — farlo subito trasformerebbe una lettura
 * lenta in una lettura lentissima, e la seconda non e' piu' affidabile della
 * prima.
 * ========================================================================== */
static void fdc_reset_controller(void)
{
    uint8_t st0, pcn;
    int     i;

    fdc_irq_clear();
    g_motor_running = 0;            /* il reset spegne il motore */

    port_outb(FDC_DOR, 0x00);       /* reset asserito */
    fdc_delay_ms(2);
    port_outb(FDC_DOR, 0x0C);       /* rilascio: genera IRQ6 */

    if (fdc_wait_irq(500) != 0) {
        klog(LOG_WARN, "FAT12: nessun IRQ6 dopo il reset del controller");
    }

    /* Quattro volte: il reset arma un IRQ per ciascuna delle quattro unita'
     * possibili, e finche' non si leggono tutti il FIFO resta bloccato. */
    for (i = 0; i < 4; i++) fdc_sense_interrupt(&st0, &pcn);

    port_outb(FDC_CCR, 0x00);       /* 500 kbps: floppy 1.44MB */

    fdc_send_byte(0x03);            /* SPECIFY */
    fdc_send_byte(0xDF);            /* SRT=3ms, HUT=240ms */
    fdc_send_byte(0x03);            /* HLT=2ms */

    g_fdc_cyl = -1;                 /* dove sia la testina non lo sa piu' nessuno */
}

/* Porte del controller DMA 8237 (canale 2 = floppy) */
#define DMA_MASK        0x0A    /* maschera singolo canale        */
#define DMA_MODE        0x0B    /* registro di modo               */
#define DMA_CLEAR_FF    0x0C    /* azzera il flip-flop byte alto/basso */
#define DMA_CH2_ADDR    0x04    /* indirizzo, canale 2            */
#define DMA_CH2_COUNT   0x05    /* conteggio, canale 2            */
#define DMA_CH2_PAGE    0x81    /* registro di pagina, canale 2   */

#define DMA_CH2_MASK_ON   0x06  /* 0x04 | canale 2: maschera      */
#define DMA_CH2_MASK_OFF  0x02  /* canale 2: smaschera            */
#define DMA_MODE_READ     0x46  /* single, incr, scrivi in memoria (= lettura da disco) */
#define DMA_MODE_WRITE    0x4A  /* single, incr, leggi da memoria (= scrittura su disco) */

/* Buffer di trasferimento. Vedi i vincoli sopra per allineamento e
 * posizione. Non è static per caso: deve avere un indirizzo stabile e
 * noto per tutta la vita del sistema. */
static uint8_t g_dma_buf[BYTES_PER_SECTOR] __attribute__((aligned(512)));

/* =============================================================================
 * fdc_dma_prepare — programma il canale 2 per un trasferimento di 1 settore
 *
 * write != 0 → il DMA legge dalla memoria e scrive sul disco.
 * ============================================================================= */
static void fdc_dma_prepare(int write)
{
    uint32_t addr  = (uint32_t)g_dma_buf;   /* identità di mapping: virt = fis */
    uint32_t count = BYTES_PER_SECTOR - 1;  /* il conteggio è "byte meno uno" */

    port_outb(DMA_MASK, DMA_CH2_MASK_ON);   /* canale fermo durante il setup */

    /* Il flip-flop decide se il prossimo byte scritto è quello basso o
     * quello alto: va azzerato prima di OGNI coppia, non una volta sola. */
    port_outb(DMA_CLEAR_FF, 0xFF);
    port_outb(DMA_CH2_ADDR, (uint8_t)(addr & 0xFF));
    port_outb(DMA_CH2_ADDR, (uint8_t)((addr >> 8) & 0xFF));

    /* I bit 16-23 non passano dal registro indirizzo ma da quello di
     * pagina: è così che l'8237, che è a 16 bit, arriva a 24. */
    port_outb(DMA_CH2_PAGE, (uint8_t)((addr >> 16) & 0xFF));

    port_outb(DMA_CLEAR_FF, 0xFF);
    port_outb(DMA_CH2_COUNT, (uint8_t)(count & 0xFF));
    port_outb(DMA_CH2_COUNT, (uint8_t)((count >> 8) & 0xFF));

    port_outb(DMA_MODE, write ? DMA_MODE_WRITE : DMA_MODE_READ);

    port_outb(DMA_MASK, DMA_CH2_MASK_OFF);  /* via: il canale è armato */
}

/* =============================================================================
 * fdc_rw_sector — legge o scrive UN settore, in DMA
 *
 * Unico punto in cui si parla con il controller per i dati: prima
 * lettura e scrittura avevano due copie quasi identiche di questa
 * sequenza, ed è il motivo per cui il baco della scrittura è
 * sopravvissuto tanto a lungo — la correzione della lettura non si era
 * propagata all'altra copia.
 *
 * buf: per la lettura riceve i dati, per la scrittura li fornisce.
 * Ritorna 0 se tutto è andato bene, <0 altrimenti.
 *
 * UN SOLO TENTATIVO: i ritentativi li fa il chiamante fdc_rw_sector(), che
 * è il punto in cui il resto del driver entra. Vedi lì il perché.
 * ============================================================================= */
static int fdc_rw_sector_once(uint16_t lba, uint8_t *buf, int write)
{
    uint8_t cyl, head, sec;
    uint8_t st0 = 0, st1 = 0, st2 = 0, rc = 0, rh = 0, rs = 0, rn = 0;
    uint32_t i;
    int      err = 0;

    lba_to_chs(lba, &cyl, &head, &sec);

    if (write) {
        for (i = 0; i < BYTES_PER_SECTOR; i++) g_dma_buf[i] = buf[i];
    }

/* ! PRIMA DI OGNI COMANDO SI CONTROLLA CHE IL FIFO SIA VUOTO. Costa una
     * lettura di MSR; senza, un risultato rimasto da un guasto precedente fa
     * fallire questo comando per un motivo che non ha niente a che vedere con
     * questo comando. */
    fdc_drena();

    fdc_motor_on();
    fdc_seek_if_needed(cyl, head);

    fdc_dma_prepare(write);
    fdc_irq_clear();

    /* La fase di COMANDO resta protetta da cli.
     *
     * Non è il residuo della vecchia protezione del ciclo PIO: il
     * controller ha un timeout interno di ~500us fra un byte di comando e
     * il successivo, e un IRQ0 che cadesse in mezzo ai 9 byte glielo
     * farebbe abbandonare. La fase DATI invece non ha più bisogno di
     * protezione — la fa il DMA — e la fase di ATTESA non può averla,
     * perché aspettiamo proprio un interrupt. */
    interrupts_disable();

    fdc_send_byte(0x03);            /* SPECIFY */
    fdc_send_byte(0xDF);            /* SRT=13ms, HUT=240ms */
    fdc_send_byte(0x02);            /* HLT=1, ND=0 → modalità DMA */

    fdc_send_byte(write ? 0x45 : 0x46);   /* WRITE DATA / READ DATA, MT=0 MFM=1 */
    fdc_send_byte((uint8_t)(head << 2 | g_drive));
    fdc_send_byte(cyl);
    fdc_send_byte(head);
    fdc_send_byte(sec);
    fdc_send_byte(2);               /* N=2 → 512 byte per settore */
    fdc_send_byte(sec);             /* EOT: fermati a QUESTO settore */
    fdc_send_byte(0x1B);            /* GPL */
    fdc_send_byte(0xFF);            /* DTL (ignorato con N!=0) */

    interrupts_enable();

    /* Fine comando: il controller alza IRQ6. Senza DMA questo segnale non
     * era utilizzabile (in PIO l'IRQ arriva a ogni byte); ora è
     * esattamente ciò che serve. */
    if (fdc_wait_irq(3000) != 0) {
        /* RETE DI SICUREZZA PER HARDWARE SCONOSCIUTO (luglio 2026).
         *
         * L'IRQ6 e' il modo pulito di sapere che il comando e' finito, ma
         * dipende da come il chipset instrada l'interrupt: se per qualche
         * ragione non arriva, senza questo ramo OGNI lettura fallirebbe e
         * il sistema non riuscirebbe nemmeno a caricare la shell.
         *
         * Il trasferimento pero' lo fa il DMA, non l'interrupt: se i dati
         * sono arrivati, il controller e' comunque passato alla fase di
         * risultato e lo si vede da MSR (RQM=1, DIO=1, EXM=0). Provare a
         * leggerla e' gratis, ed e' esattamente ciò che il driver faceva
         * prima di usare IRQ6.
         *
         * Il warning resta: un sistema che funziona solo grazie al ripiego
         * deve dirlo, altrimenti il problema vero non si scopre mai. */
        uint32_t attesa;
        int      pronto = 0;

        for (attesa = 0; attesa < 200000; attesa++) {
            uint8_t m = port_inb(FDC_MSR);
            if ((m & 0xD0) == 0xD0) { pronto = 1; break; }   /* RQM|DIO, EXM=0 */
        }

        if (!pronto) {
            klog(LOG_ERROR, "FAT12: timeout IRQ6 su %s LBA=%u, e nessuna fase "
                 "di risultato: comando perso", write ? "WRITE" : "READ", lba);
            g_fdc_cyl = -1;
            return -1;
        }

        klog(LOG_WARN, "FAT12: IRQ6 non ricevuto su %s LBA=%u - proseguo con "
             "il polling di MSR (controllare il routing dell'IRQ6)",
             write ? "WRITE" : "READ", lba);
    }

    /* Fase di risultato: SETTE byte, non tre. Leggerne meno lascerebbe il
     * controller a metà del risultato, e il comando successivo troverebbe
     * DIO=1 fallendo con "FDC non in modalita' write". La vecchia
     * scrittura ne leggeva solo tre. */
    err |= fdc_recv_byte(&st0);
    err |= fdc_recv_byte(&st1);
    err |= fdc_recv_byte(&st2);
    err |= fdc_recv_byte(&rc);
    err |= fdc_recv_byte(&rh);
    err |= fdc_recv_byte(&rs);
    err |= fdc_recv_byte(&rn);
    (void)rc; (void)rh; (void)rs; (void)rn;

    if (err != 0) {
        klog(LOG_ERROR, "FAT12: fase di risultato incompleta su %s LBA=%u",
             write ? "WRITE" : "READ", lba);
        /* ! E SI RIPULISCE SUBITO, invece di lasciare il resto nel FIFO. E'
         * qui che nasceva la valanga: i byte non letti facevano fallire il
         * comando SUCCESSIVO con «FDC non in modalita' write», e quello dopo
         * ancora. Un errore di lettura deve costare una lettura, non la
         * sessione. Vedi fdc_drena(). */
        fdc_drena();
        g_fdc_cyl = -1;
        return -1;
    }

    /* ST0 bit 7-6: 00 = comando concluso correttamente.
     *
     * Il cilindro compare nel messaggio di proposito: la geometria del
     * floppy di EX-OS mette FAT e root directory tutte sul cilindro 0,
     * mentre /bin/sh sta al 10-11. Sapere a quale cilindro fallisce una
     * lettura distingue subito "il controller non funziona" da "la testina
     * non si posiziona", che sono guasti diversi. */
    if (st0 & 0xC0) {
        klog(g_sondaggio ? LOG_DEBUG : LOG_ERROR,
             "FAT12: errore FDC %s LBA=%u (C=%u H=%u S=%u) "
             "ST0=0x%02x ST1=0x%02x ST2=0x%02x",
             write ? "WRITE" : "READ", lba, cyl, head, sec, st0, st1, st2);
        g_fdc_cyl = -1;
        return -1;
    }

    if (!write) {
        for (i = 0; i < BYTES_PER_SECTOR; i++) buf[i] = g_dma_buf[i];
    }

    return 0;
}

/* =============================================================================
 * fdc_rw_sector — legge o scrive un settore RITENTANDO
 *
 * PERCHÉ SERVE (luglio 2026, dopo il fallimento sul Pentium II reale)
 *
 * Fino a questa sessione ogni settore veniva tentato UNA volta sola: se
 * fdc_rw_sector_once falliva, l'errore risaliva immediatamente fino al
 * chiamante e l'operazione in corso veniva abbandonata. Su QEMU questo non
 * si nota, perché un floppy emulato non sbaglia mai una lettura. Su un
 * drive vero è un difetto grave: un 3.5" restituisce con regolarità errori
 * transitori — ST1 bit 0 (MA, address mark mancante), bit 5 (DE, CRC), bit
 * 4 (OR, overrun del DMA) — tipicamente al PRIMO accesso a un cilindro
 * appena raggiunto, quando la testina non si è ancora completamente
 * assestata. È il motivo per cui il BIOS, il DOS e Linux ritentano tutti
 * da tre a cinque volte con un recalibrate in mezzo: non è prudenza
 * eccessiva, è il modo in cui questo hardware va usato.
 *
 * LA CONSEGUENZA ESATTA CHE SI OSSERVAVA. Caricare /bin/sh significa
 * leggere 27 settori consecutivi: basta che UNO fallisca perché elf_load
 * abortisca e il kernel stampi "'/bin/sh' non trovata". Nel frattempo
 * fat12_init continuava a riuscire, perché FAT (LBA 1-9) e root directory
 * (LBA 19-32) stanno tutte sul cilindro 0, cioè dove il RECALIBRATE ha
 * appena messo la testina: quelle letture non fanno un solo SEEK e sono
 * quindi le più facili del disco. Il risultato era un sistema che
 * annunciava "FAT12 OK" e poi non trovava la shell — una diagnosi che
 * puntava nella direzione sbagliata.
 *
 * Fra un tentativo e l'altro si riporta la testina al cilindro 0 a
 * tentativi alterni: un recalibrate costa (fino a mezzo secondo) ma è
 * l'unico modo di recuperare da una desincronizzazione vera fra posizione
 * fisica e posizione creduta. Nei tentativi pari ci si limita a
 * invalidare g_fdc_cyl, che forza comunque un nuovo SEEK.
 *
 * Ogni ritentativo viene registrato: un disco che funziona solo grazie ai
 * ritentativi è un disco che sta per morire, e deve poterlo dire.
 * ============================================================================= */
#define FDC_MAX_TENTATIVI  5

static int fdc_rw_sector(uint16_t lba, uint8_t *buf, int write)
{
    int tentativo;
    int esito = -1;

    int massimo = g_sondaggio ? 1 : FDC_MAX_TENTATIVI;

    /* Da qui il controller e' nostro: il tick del timer non tocchera' il
     * motore finche' non si esce, ritenti e ricalibrature comprese. */
    fdc_entra();

    for (tentativo = 0; tentativo < massimo; tentativo++) {
        if (tentativo > 0) {
            klog(LOG_WARN, "FAT12: %s LBA=%u fallita, ritento (%d/%d)",
                 write ? "WRITE" : "READ", lba, tentativo + 1, FDC_MAX_TENTATIVI);

            /* ! LA SCALA SALE: prima si sospetta la testina, poi il
             * controller. Un supporto che sbaglia ogni tanto si rimette con
             * un SEEK o una ricalibratura; un controller rimasto a meta' di
             * una fase di risultato non si rimette con niente di meno di un
             * reset — ed e' esattamente lo stato in cui lo lascia un IRQ6 che
             * non arriva. Fare il reset al primo errore costerebbe 300 ms di
             * rispin a ogni graffio del dischetto, per niente. */
            if (tentativo == 1) {
                g_fdc_cyl = -1;         /* forza almeno un nuovo SEEK */
            } else if (tentativo == 2) {
                fdc_recalibrate();      /* riallinea testina e credenza */
            } else {
                klog(LOG_WARN, "FAT12: il controller non risponde piu' come "
                     "dovrebbe, lo azzero");
                fdc_reset_controller();
                fdc_motor_on();
                fdc_recalibrate();
            }
        }

        if (fdc_rw_sector_once(lba, buf, write) == 0) {
            if (tentativo > 0) {
                klog(LOG_WARN, "FAT12: %s LBA=%u riuscita al tentativo %d - "
                     "il supporto o il drive danno errori transitori",
                     write ? "WRITE" : "READ", lba, tentativo + 1);
            }
            esito = 0;
            break;
        }
    }

    if (esito != 0 && !g_sondaggio) {
        klog(LOG_ERROR, "FAT12: %s LBA=%u fallita dopo %d tentativi - rinuncio",
             write ? "WRITE" : "READ", lba, FDC_MAX_TENTATIVI);
    }

    /* ! UN'USCITA SOLA, e non tre `return` sparsi. Ogni via d'uscita deve
     * decrementare il contatore: una che se ne dimentica lascia il
     * controller «occupato» per sempre, cioe' rimette esattamente il difetto
     * che questo contatore serve a togliere — il motore che non si ferma
     * piu'. Con un solo punto di uscita non ci si puo' dimenticare. */
    fdc_esce();
    return esito;
}

/* =============================================================================
 * fat12_read_sector — Legge 1 settore dal floppy in buf
 * Usa la cache se disponibile, altrimenti legge dal disco.
 * ============================================================================= */
static int fat12_read_sector(uint16_t lba, uint8_t *buf)
{
    uint32_t i;
    uint32_t oldest_idx  = 0;
    uint32_t oldest_tick = 0xFFFFFFFF;

    /* Controlla cache */
    for (i = 0; i < CACHE_SLOTS; i++) {
        if (g_cache[i].valid && g_cache[i].lba == lba) {
            g_cache[i].last_use = g_ticks;
            uint8_t *src = g_cache[i].data;
            uint32_t n   = BYTES_PER_SECTOR;
            while (n--) *buf++ = *src++;
            return 0;
        }
        if (!g_cache[i].valid || g_cache[i].last_use < oldest_tick) {
            oldest_tick = g_cache[i].last_use;
            oldest_idx  = i;
        }
    }

    /* Cache miss: se lo slot LRU è sporco va prima riversato sul disco,
     * altrimenti la modifica andrebbe persa. */
    if (g_cache[oldest_idx].valid && g_cache[oldest_idx].dirty) {
        if (fdc_rw_sector(g_cache[oldest_idx].lba,
                           g_cache[oldest_idx].data, 1) != 0) {
            klog(LOG_ERROR, "FAT12: impossibile riversare il settore LBA %u",
                 g_cache[oldest_idx].lba);
            return -1;
        }
        g_cache[oldest_idx].dirty = 0;
    }

    if (fdc_rw_sector(lba, g_cache[oldest_idx].data, 0) != 0) {
        g_cache[oldest_idx].valid = 0;   /* contenuto non attendibile */
        return -1;
    }

    g_cache[oldest_idx].lba      = lba;
    g_cache[oldest_idx].valid    = 1;
    g_cache[oldest_idx].dirty    = 0;
    g_cache[oldest_idx].last_use = g_ticks;

    {
        uint8_t *src = g_cache[oldest_idx].data;
        uint32_t n   = BYTES_PER_SECTOR;
        while (n--) *buf++ = *src++;
    }

    return 0;
}

/* =============================================================================
 * fat12_write_sector — Scrive 1 settore (write-through sulla cache)
 * ============================================================================= */
/* =============================================================================
 * fat12_write_sector — scrive 1 settore, WRITE-BACK sulla cache
 *
 * PRESTAZIONI (luglio 2026): questa funzione era write-through, cioè
 * andava sul floppy a ogni chiamata. Salvare un file da /bin/textline
 * significa toccare ripetutamente gli stessi pochi settori — quello dei
 * dati, quello della directory, quelli della FAT — una volta per riga
 * scritta. Con un accesso reale al floppy per ognuno (motore, seek,
 * comando, attesa IRQ6: decine di millisecondi) salvare 79 righe
 * richiedeva minuti.
 *
 * Ora la scrittura si ferma in cache e marca lo slot sporco. Il disco
 * viene toccato solo quando serve davvero:
 *   - quando lo slot va sfrattato per fare posto (fat12_read_sector);
 *   - alla chiusura del file (fat12_close);
 *   - su fat12_sync(), chiamata anche dalla procedura di arresto
 *     (kernel/arch/x86/power.c) — è il motivo per cui `halt` e
 *     `poweroff` sincronizzano prima di fermare il sistema.
 *
 * Il compromesso è quello classico di ogni cache write-back: dati non
 * ancora sul supporto se si toglie corrente senza arrestare il sistema.
 * Accettabile perché esiste una procedura di arresto che sincronizza, e
 * perché l'alternativa misurata era inutilizzabile.
 * ============================================================================= */
static int fat12_write_sector(uint16_t lba, const uint8_t *buf)
{
    uint32_t i;
    uint32_t vittima      = 0;
    uint32_t piu_vecchio  = 0xFFFFFFFF;

    /* Settore già in cache: aggiornalo sul posto */
    for (i = 0; i < CACHE_SLOTS; i++) {
        if (g_cache[i].valid && g_cache[i].lba == lba) {
            uint8_t       *dst = g_cache[i].data;
            const uint8_t *src = buf;
            uint32_t       n   = BYTES_PER_SECTOR;
            while (n--) *dst++ = *src++;
            g_cache[i].dirty    = 1;
            g_cache[i].last_use = g_ticks;
            return 0;
        }
        if (!g_cache[i].valid || g_cache[i].last_use < piu_vecchio) {
            piu_vecchio = g_cache[i].last_use;
            vittima     = i;
        }
    }

    /* Serve uno slot: se il candidato è sporco va prima riversato. */
    if (g_cache[vittima].valid && g_cache[vittima].dirty) {
        if (fdc_rw_sector(g_cache[vittima].lba, g_cache[vittima].data, 1) != 0) {
            return -1;
        }
    }

    {
        uint8_t       *dst = g_cache[vittima].data;
        const uint8_t *src = buf;
        uint32_t       n   = BYTES_PER_SECTOR;
        while (n--) *dst++ = *src++;
    }
    g_cache[vittima].lba      = lba;
    g_cache[vittima].valid    = 1;
    g_cache[vittima].dirty    = 1;
    g_cache[vittima].last_use = g_ticks;
    return 0;
}

/* =============================================================================
 * Operazioni FAT
 * ============================================================================= */

static uint16_t fat12_get_next(uint16_t cluster)
{
    uint32_t byte_off = cluster + (cluster / 2);
    uint16_t val = (uint16_t)g_fat[byte_off] | ((uint16_t)g_fat[byte_off+1] << 8);
    if (cluster & 1) val >>= 4;
    else             val &= 0x0FFF;
    if (val >= FAT12_END_MIN) return FAT12_END;
    return val;
}

static void fat12_set_next(uint16_t cluster, uint16_t value)
{
    uint32_t byte_off = cluster + (cluster / 2);
    uint16_t existing = (uint16_t)g_fat[byte_off] | ((uint16_t)g_fat[byte_off+1] << 8);
    if (cluster & 1)
        existing = (existing & 0x000F) | (uint16_t)((value & 0x0FFF) << 4);
    else
        existing = (existing & 0xF000) | (value & 0x0FFF);
    g_fat[byte_off]   = (uint8_t)(existing & 0xFF);
    g_fat[byte_off+1] = (uint8_t)((existing >> 8) & 0xFF);
    g_fat_dirty = 1;
}

static uint16_t fat12_alloc_cluster(void)
{
    uint16_t c;
    for (c = 2; c < MAX_CLUSTERS; c++) {
        if (fat12_get_next(c) == FAT12_FREE) {
            fat12_set_next(c, FAT12_END);
            return c;
        }
    }
    return 0;   /* Disco pieno */
}

static void fat12_free_chain(uint16_t first)
{
    uint16_t c = first;
    while (c != FAT12_END && c >= 2 && c < FAT12_RESERVED_MIN) {
        uint16_t next = fat12_get_next(c);
        fat12_set_next(c, FAT12_FREE);
        c = next;
    }
}

static uint16_t fat12_cluster_to_lba(uint16_t cluster)
{
    return (uint16_t)(DATA_START_LBA + (cluster - 2) * SECTORS_PER_CLUSTER);
}

static int fat12_flush_fat(void)
{
    uint32_t i;
    for (i = 0; i < SECTORS_PER_FAT; i++) {
        if (fat12_write_sector((uint16_t)(FAT1_LBA + i),
                                g_fat + i * BYTES_PER_SECTOR) != 0) return -1;
        if (fat12_write_sector((uint16_t)(FAT2_LBA + i),
                                g_fat + i * BYTES_PER_SECTOR) != 0) return -1;
    }
    g_fat_dirty = 0;
    return 0;
}

static int fat12_flush_root(void)
{
    uint32_t i;
    for (i = 0; i < ROOT_DIR_SECTORS; i++) {
        if (fat12_write_sector((uint16_t)(ROOT_DIR_LBA + i),
                                g_root_dir + i * BYTES_PER_SECTOR) != 0)
            return -1;
    }
    g_root_dirty = 0;
    return 0;
}

/* =============================================================================
 * fat12_init — Inizializza il driver FAT12 kernel-side
 * ============================================================================= */
/* =============================================================================
 * fat12_sonda — «c'e' un floppy in questo drive?»
 *
 * Fa esattamente quello che fa fat12_init, ma in silenzio e senza
 * ritentativi. Serve a chi deve DECIDERE, non a chi deve usare il disco:
 * kernel_main la chiama prima di inizializzare davvero, cosi' su una
 * macchina senza floppy — o dietro l'emulazione El Torito di un CD
 * avviabile — l'avvio non stampa dieci righe rosse per una condizione
 * normale.
 *
 * ! SE RIESCE, IL DRIVER RESTA INIZIALIZZATO e non serve richiamare
 * fat12_init: la sonda E' l'inizializzazione, solo zitta. Chiamarla due
 * volte funzionerebbe ma rifarebbe il reset del controller per niente.
 * ============================================================================= */
int fat12_sonda(uint8_t drive)
{
    int r;

    g_sondaggio = 1;
    r = fat12_init(drive);
    g_sondaggio = 0;

    /* Il caso riuscito lo annuncia gia' fat12_init: qui si parla solo
     * quando la risposta e' "non c'e'", che altrimenti nessuno direbbe. */
    if (r != 0) {
        klog(LOG_INFO, "FAT12: nessun floppy nel drive 0x%02x "
             "(normale se si avvia da CD o da disco)", drive);
    }
    return r;
}

int fat12_init(uint8_t drive)
{
    uint32_t i;

    extern void pic_unmask_irq(uint8_t irq);

    /* Handler IRQ6 registrato PRIMA dello smascheramento e prima del
     * reset del controller: il reset stesso genera un IRQ, e vogliamo
     * poterlo attendere invece di tirare a indovinare quanto dura. */
    irq_register_handler(6, fdc_irq6_handler);
    pic_unmask_irq(6);  /* IRQ6 = floppy */

    if (!g_sondaggio) klog(LOG_INFO, "FAT12: inizializzazione driver kernel...");

    g_drive = drive;
    g_initialized = 0;
    g_fat_loaded  = 0;
    g_fat_dirty   = 0;
    g_root_dirty  = 0;

    /* Azzera cache */
    for (i = 0; i < CACHE_SLOTS; i++) {
        g_cache[i].valid    = 0;
        g_cache[i].dirty    = 0;
        g_cache[i].lba      = 0;
        g_cache[i].last_use = 0;
    }

    /* Reset FDC.
     *
     * BUG RESIDUO CORRETTO (luglio 2026): qui c'erano ancora due loop di
     * NOP a conteggio fisso (`for (d=0; d<100000; d++) nop;`), sfuggiti
     * alla bonifica di giugno che aveva sostituito gli altri tre con
     * fdc_delay_ms(). Stesso identico difetto: la durata dipende dalla
     * velocita' della CPU, quindi su hardware diverso da quello su cui
     * erano stati tarati il controller puo' non aver completato il reset
     * quando gli si parla. Ora sono attese in tempo reale. */
    /* ! LA SEQUENZA DI RESET STA IN fdc_reset_controller(), NON PIU' QUI.
     * Era scritta a mano dentro questa funzione e quindi si poteva eseguire
     * una volta sola, all'avvio — mentre il momento in cui serve davvero e'
     * DOPO, quando una fase di risultato interrotta ha lasciato il
     * controller in uno stato che nessun SEEK rimette a posto. Adesso la
     * chiamano tutt'e due: l'avvio e la scala dei ritenti. */
    fdc_reset_controller();

    /* MOTORE ACCESO PRIMA DEL RECALIBRATE (luglio 2026).
     *
     * Prima il RECALIBRATE partiva con DOR=0x0C, cioe' unita' selezionata
     * ma motore fermo. In emulazione funziona: il controller virtuale
     * ignora lo stato del motore. Su un drive vero e' una scommessa —
     * la testina viene mossa da un motore passo-passo distinto da quello
     * del mandrino, ma il rilevamento della traccia 0 e la validita' dei
     * segnali dipendono dal fatto che l'unita' sia davvero attiva, ed e'
     * il motivo per cui BIOS, DOS e Linux accendono sempre il motore
     * prima. Se il recalibrate non riesce qui, ogni SEEK successivo parte
     * da una posizione sbagliata e tutte le letture oltre il cilindro 0
     * (cioe' tutto tranne FAT e root directory) leggono la traccia
     * sbagliata.
     *
     * Non costa nulla in piu': i 300 ms di stabilizzazione andavano
     * comunque pagati alla prima lettura, qui vengono solo anticipati. */
    fdc_entra();
    fdc_motor_on();

    /* Riporta la testina al cilindro 0: il BIOS ha gia' letto vari
     * settori durante stage1/stage2 (fino a cilindro >0), e senza
     * questo passo la posizione fisica della testina resta
     * desincronizzata dai parametri C/H/S che invieremo nei comandi
     * READ successivi. */
    fdc_recalibrate();
    fdc_esce();

    /* Leggi FAT1 in RAM */
    if (!g_sondaggio) klog(LOG_INFO, "FAT12: caricamento FAT in RAM...");
    for (i = 0; i < SECTORS_PER_FAT; i++) {
        if (fat12_read_sector((uint16_t)(FAT1_LBA + i),
                               g_fat + i * BYTES_PER_SECTOR) != 0) {
            klog(g_sondaggio ? LOG_DEBUG : LOG_ERROR,
                 "FAT12: errore lettura FAT settore %u", i);
            return -1;
        }
    }
    g_fat_loaded = 1;
    klog(LOG_INFO, "FAT12: FAT caricata (%u byte)", sizeof(g_fat));

    /* Leggi root directory in RAM */
    klog(LOG_INFO, "FAT12: caricamento root directory...");
    for (i = 0; i < ROOT_DIR_SECTORS; i++) {
        if (fat12_read_sector((uint16_t)(ROOT_DIR_LBA + i),
                               g_root_dir + i * BYTES_PER_SECTOR) != 0) {
            klog(LOG_ERROR, "FAT12: errore lettura root dir settore %u", i);
            return -1;
        }
    }
    klog(LOG_INFO, "FAT12: root directory caricata (%u entry max)", ROOT_ENTRY_COUNT);

    g_initialized = 1;

    /* Azzera tabella file aperti */
    {
        uint32_t fi;
        for (fi = 0; fi < MAX_OPEN_FILES; fi++) g_open_files[fi].used = 0;
    }

    klog(LOG_INFO, "FAT12: driver inizializzato (drive=0x%02x)", drive);
    return 0;
}

/* =============================================================================
 * fat12_dev_read / fat12_dev_write — accesso grezzo per l'astrazione a
 * blocchi. Vedi kernel/include/fat12.h per il perche' passino dalla
 * cache invece di andare diretti all'FDC.
 * ============================================================================= */
int fat12_dev_read(uint32_t lba, void *buf)
{
    if (lba >= TOTAL_SECTORS) return -1;
    return fat12_read_sector((uint16_t)lba, (uint8_t *)buf);
}

int fat12_dev_write(uint32_t lba, const void *buf)
{
    if (lba >= TOTAL_SECTORS) return -1;
    return fat12_write_sector((uint16_t)lba, (const uint8_t *)buf);
}

/* =============================================================================
 * fat12_sync — riversa su disco FAT e root directory se modificate
 *
 * Il driver tiene FAT e root directory in RAM con un flag "dirty" e le
 * riscrive solo quando serve. Prima di spegnere o riavviare vanno
 * riversate, altrimenti si perdono le modifiche e — peggio — il floppy
 * resta con un filesystem incoerente (una FAT che descrive catene di
 * cluster diverse da quelle registrate nelle directory).
 *
 * Richiede interrupt abilitati: le scritture passano dal driver FDC, che
 * usa attese basate su g_ticks e sull'IRQ6. Chiamarla con IF=0 la
 * bloccherebbe per sempre in fdc_delay_ms.
 *
 * Ritorna 0 se tutto è stato scritto (o se non c'era nulla da scrivere),
 * <0 al primo errore.
 * ============================================================================= */
int fat12_sync(void)
{
    if (!g_initialized) return 0;   /* niente montato, niente da salvare */

    if (g_fat_dirty) {
        if (fat12_flush_fat() != 0) {
            klog(LOG_ERROR, "FAT12: sync della FAT fallito");
            return -1;
        }
    }

    if (g_root_dirty) {
        if (fat12_flush_root() != 0) {
            klog(LOG_ERROR, "FAT12: sync della root directory fallito");
            return -1;
        }
    }

    /* Riversa gli slot sporchi DELLA CACHE sul supporto.
     *
     * ATTENZIONE all'ordine e alla funzione usata. Deve venire DOPO i due
     * flush qui sopra, perché fat12_flush_fat/root scrivono con
     * fat12_write_sector, che dalla conversione a write-back si ferma in
     * cache: se si sincronizzasse la cache per prima, FAT e root
     * resterebbero dentro.
     *
     * E deve usare fdc_rw_sector DIRETTAMENTE, non fat12_write_sector:
     * quest'ultima rimetterebbe semplicemente il dato nella cache da cui
     * lo stiamo tirando fuori, azzerando poi il flag "sporco" — il
     * risultato sarebbe un sync che non scrive nulla e giura di averlo
     * fatto. È esattamente il difetto che ho introdotto convertendo la
     * cache e che si manifestava come "textline dice salvato ma il file
     * non esiste". */
    {
        uint32_t i;
        for (i = 0; i < CACHE_SLOTS; i++) {
            if (g_cache[i].valid && g_cache[i].dirty) {
                if (fdc_rw_sector(g_cache[i].lba, g_cache[i].data, 1) != 0) {
                    klog(LOG_ERROR, "FAT12: sync del settore LBA %u fallito",
                         g_cache[i].lba);
                    return -1;
                }
                g_cache[i].dirty = 0;
            }
        }
    }

    /* Disco fermo e coerente: si puo' spegnere il motore. Questo e' uno
     * dei due punti sincroni sicuri (l'altro e' la fine del boot) — vedi
     * fat12_motor_off. fat12_sync() gira a ogni sys_exit, quindi il drive
     * si ferma alla fine di ogni comando, come su qualunque PC dell'epoca.
     * Le uscite in errore qui sopra lasciano il motore acceso di
     * proposito: se una scrittura e' fallita, il tentativo successivo non
     * deve ripagare lo spin-up. */
    fat12_motor_off();

    return 0;
}

/* =============================================================================
 * fat12_find_in_dir — Cerca un file/dir nella root directory
 * name83: 11 byte formato FAT "FILENAME EXT"
 * Ritorna: puntatore all'entry, NULL se non trovato
 * ============================================================================= */
static Fat12DirEntry *fat12_find_in_root(const char *name83)
{
    uint32_t i;
    Fat12DirEntry *entries = (Fat12DirEntry *)g_root_dir;

    for (i = 0; i < ROOT_ENTRY_COUNT; i++) {
        if (entries[i].name[0] == 0x00) break;     /* Fine directory */
        if ((uint8_t)entries[i].name[0] == 0xE5)  continue;  /* Cancellato */
        if (entries[i].attr & 0x08)                continue;  /* Volume label */

        /* Confronta nome 11 byte: name[8] + ext[3] sono campi separati
         * nella struct (anche se PACKED li rende contigui in memoria,
         * accedervi come un unico array name[0..10] e' undefined
         * behavior in C standard — confrontiamo i due campi
         * esplicitamente per correttezza). */
        uint8_t match = 1;
        uint8_t j;
        for (j = 0; j < 8; j++) {
            if (entries[i].name[j] != (uint8_t)name83[j]) { match = 0; break; }
        }
        if (match) {
            for (j = 0; j < 3; j++) {
                if (entries[i].ext[j] != (uint8_t)name83[8 + j]) { match = 0; break; }
            }
        }
        if (match) return &entries[i];
    }
    return NULL;
}

/* =============================================================================
 * Conversione percorso → nome FAT 8.3
 * "/bin/sh" → "SH      " + "   " → "SH         "
 * ============================================================================= */
static void path_to_fat83(const char *path, char *name83)
{
    const char *base;
    const char *ext;
    uint32_t i, n, e;

    /* Prendi solo l'ultima componente del percorso */
    base = path;
    const char *p = path;
    while (*p) { if (*p == '/') base = p + 1; p++; }

    /* Inizializza con spazi */
    for (i = 0; i < 11; i++) name83[i] = ' ';

    /* Trova estensione */
    ext = NULL;
    p = base;
    while (*p) { if (*p == '.') ext = p; p++; }

    /* Copia nome (max 8 char, maiuscolo) */
    n = 0;
    p = base;
    while (*p && *p != '.' && n < 8) {
        char c = *p++;
        if (c >= 'a' && c <= 'z') c -= 32;
        name83[n++] = c;
    }

    /* Copia estensione (max 3 char, maiuscolo) */
    if (ext) {
        ext++;   /* Salta il punto */
        e = 0;
        while (*ext && e < 3) {
            char c = *ext++;
            if (c >= 'a' && c <= 'z') c -= 32;
            name83[8 + e++] = c;
        }
    }
}


/* =============================================================================
 * fat12_find_in_dir_cluster — Cerca un file in una directory dato il cluster
 * ============================================================================= */
static Fat12DirEntry *fat12_find_in_dir_cluster(uint16_t cluster,
                                                  const char *name83)
{
    static uint8_t  dir_buf[32 * 16];  /* buffer per 16 entry alla volta */
    Fat12DirEntry  *entries = (Fat12DirEntry *)dir_buf;
    uint32_t        i;

    while (cluster != FAT12_END && cluster >= 2 && cluster < FAT12_RESERVED_MIN) {
        uint16_t lba = fat12_cluster_to_lba(cluster);
        if (fat12_read_sector(lba, dir_buf) != 0) return NULL;

        for (i = 0; i < BYTES_PER_SECTOR / 32; i++) {
            if (entries[i].name[0] == 0x00) return NULL;
            if ((uint8_t)entries[i].name[0] == 0xE5) continue;
            if (entries[i].attr & 0x08) continue;

            uint8_t match = 1, j;
            for (j = 0; j < 8; j++) {
                if (entries[i].name[j] != (uint8_t)name83[j]) { match = 0; break; }
            }
            if (match) {
                for (j = 0; j < 3; j++) {
                    if (entries[i].ext[j] != (uint8_t)name83[8 + j]) { match = 0; break; }
                }
            }
            if (match) return &entries[i];
        }
        cluster = fat12_get_next(cluster);
    }
    return NULL;
}

/* =============================================================================
 * fat12_find_path — Trova un file seguendo un percorso tipo "/bin/sh"
 * Supporta: root file, /dir/file (un livello di subdir)
 *
 * ! UN PERCORSO CHE QUESTO DRIVER NON SA RAPPRESENTARE DEVE DARE «NON
 * TROVATO», MAI UN ALTRO FILE. Fino ad agosto 2026 succedeva il contrario,
 * per due strade diverse, ed erano entrambe silenziose:
 *
 *   - la ricerca nella root si faceva SEMPRE per prima e sull'ULTIMA
 *     componente, qualunque fosse il percorso: con un `/hello` nella root,
 *     `stat("/bin/hello")` rispondeva con quello della root — un file
 *     diverso, di dimensione diversa, senza un solo avviso;
 *   - il ramo con sottodirectory prendeva la PRIMA componente come
 *     cartella e l'ULTIMA come nome, buttando via tutto il resto:
 *     "/bin/a/b/c" veniva letto come "cerca c dentro /bin".
 *
 * Ora la profondita' si misura prima di cercare, e quello che eccede il
 * livello unico viene rifiutato. Chi estendera' il driver a piu' livelli
 * sostituira' questi controlli con una vera discesa per componenti.
 * ============================================================================= */
static Fat12DirEntry *fat12_find_path(const char *path, Fat12DirEntry *out)
{
    char name83[11];
    Fat12DirEntry *entry;

    if (!path || path[0] != '/') return NULL;

    const char *p = path + 1;  /* salta '/' iniziale */
    const char *slash = p;
    while (*slash && *slash != '/') slash++;

    if (*slash != '/') {
        /* Nessuna sottodirectory: il file sta nella root — ed e' l'UNICO
         * caso in cui cercarlo li' e' la risposta giusta. */
        path_to_fat83(path, name83);
        entry = fat12_find_in_root(name83);
        if (entry && out) {
            uint8_t k; uint8_t *d=(uint8_t*)out, *s=(uint8_t*)entry;
            for(k=0;k<sizeof(Fat12DirEntry);k++) d[k]=s[k];
        }
        return entry;
    }

    /* Oltre il livello unico non si va: "/a/b/c" non esiste per questo
     * driver, e dirlo e' l'unica risposta onesta. Un percorso che finisce
     * con '/' non nomina un file e cade qui insieme agli altri. */
    { const char *resto = slash + 1;
      if (*resto == '\0') return NULL;
      while (*resto) if (*resto++ == '/') return NULL; }

    /* Costruisci nome83 per la directory */
    /* Percorso finto per path_to_fat83 */
    char fake_path[260] = "/";
    uint32_t fi = 1;
    const char *q = p;
    while (q < slash) fake_path[fi++] = *q++;
    fake_path[fi] = '\0';

    char dir83[11];
    path_to_fat83(fake_path, dir83);

    Fat12DirEntry *dir_entry = fat12_find_in_root(dir83);
    if (!dir_entry) return NULL;
    if (!(dir_entry->attr & FAT12_ATTR_DIRECTORY)) {
        return NULL;
    }

    /* Cerca il file dentro la directory */
    char file_path[260] = "/";
    uint32_t fpi = 1;
    const char *fname = slash + 1;
    while (*fname) file_path[fpi++] = *fname++;
    file_path[fpi] = '\0';

    char file83[11];
    path_to_fat83(file_path, file83);

    /* Buffer statico per l'entry trovata */
    static Fat12DirEntry found_entry;
    Fat12DirEntry *fe = fat12_find_in_dir_cluster(dir_entry->first_cluster, file83);
    if (!fe) return NULL;

    uint8_t k; uint8_t *d=(uint8_t*)&found_entry, *s=(uint8_t*)fe;
    for(k=0;k<sizeof(Fat12DirEntry);k++) d[k]=s[k];
    if (out) { d=(uint8_t*)out; s=(uint8_t*)&found_entry;
               for(k=0;k<sizeof(Fat12DirEntry);k++) d[k]=s[k]; }
    return &found_entry;
}
/* =============================================================================
 * fat12_open — Apre un file per lettura o scrittura
 * Ritorna handle >= 0 oppure errore negativo
 * ============================================================================= */
/* =============================================================================
 * fat12_split_path — separa "/dir/file.txt" nella directory e nel nome 8.3
 *
 * *dir_cluster = 0 → il file sta nella root.
 * Ritorna 0 se il percorso è valido e la directory esiste, <0 altrimenti.
 * ============================================================================= */
static int fat12_split_path(const char *path, uint16_t *dir_cluster, char *name83)
{
    const char *last_slash = NULL;
    const char *p;
    char        fake[260];
    uint32_t    i;

    if (!path || path[0] != '/') return -1;

    for (p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    if (!last_slash) return -1;

    /* Il nome del file è ciò che segue l'ultimo '/' */
    fake[0] = '/';
    for (i = 0; last_slash[1 + i] && i < sizeof(fake) - 2; i++) {
        fake[1 + i] = last_slash[1 + i];
    }
    fake[1 + i] = '\0';
    if (i == 0) return -1;                 /* percorso che finisce con '/' */
    path_to_fat83(fake, name83);

    /* Nessuna directory intermedia → root */
    if (last_slash == path) {
        *dir_cluster = 0;
        return 0;
    }

    /* Costruisci il percorso della directory e cercala nella root.
     * Un solo livello, come tutto il resto di questo driver. */
    fake[0] = '/';
    for (i = 0; path + 1 + i < last_slash && i < sizeof(fake) - 2; i++) {
        fake[1 + i] = path[1 + i];
    }
    fake[1 + i] = '\0';

    /* ! LA PARTE-DIRECTORY DEVE ESSERE UNA SOLA COMPONENTE. Senza questo
     * controllo il percorso non veniva rifiutato: veniva APPIATTITO, e in
     * silenzio. path_to_fat83() qui sotto tiene solo l'ultima componente
     * di quello che gli si passa, quindi "/bin/prove" diventava "prove" e
     * lo si cercava nella root — con il risultato che `cat
     * /bin/prove/t.txt` apriva /prove/t.txt, e che QUALUNQUE prefisso
     * inventato funzionava purche' finisse col nome di una directory
     * vera. Un percorso che il driver non sa rappresentare deve dare
     * «non trovato», non un file diverso da quello chiesto. */
    { const char *c;
      for (c = fake + 1; *c; c++) if (*c == '/') return -2; }

    {
        char           dir83[11];
        Fat12DirEntry *dir_entry;

        path_to_fat83(fake, dir83);
        dir_entry = fat12_find_in_root(dir83);
        if (!dir_entry) return -2;
        if (!(dir_entry->attr & FAT12_ATTR_DIRECTORY)) return -2;
        if (dir_entry->first_cluster < 2) return -2;

        *dir_cluster = dir_entry->first_cluster;
    }
    return 0;
}

/* =============================================================================
 * fat12_dir_scan — cerca 'name83' (o uno slot libero se name83 == NULL)
 * nella catena di cluster di una sottodirectory.
 *
 * Su successo scrive settore e posizione della entry, più una copia della
 * entry stessa. Ritorna 0 se trovato, <0 altrimenti.
 * ============================================================================= */
static int fat12_dir_scan(uint16_t dir_cluster, const char *name83,
                           uint16_t *out_lba, uint32_t *out_slot,
                           Fat12DirEntry *out_entry)
{
    uint8_t         buf[BYTES_PER_SECTOR];
    Fat12DirEntry  *entries = (Fat12DirEntry *)buf;
    uint16_t        cluster = dir_cluster;
    uint32_t        i;

    while (cluster >= 2 && cluster < FAT12_RESERVED_MIN) {
        uint16_t lba = fat12_cluster_to_lba(cluster);

        if (fat12_read_sector(lba, buf) != 0) return -1;

        for (i = 0; i < DIR_ENTRIES_PER_SECTOR; i++) {
            uint8_t libera = (entries[i].name[0] == 0x00 ||
                              (uint8_t)entries[i].name[0] == 0xE5);

            if (name83 == NULL) {
                if (!libera) continue;
            } else {
                uint8_t match, j;
                if (libera) {
                    /* 0x00 significa "da qui in poi la directory e' vuota":
                     * inutile proseguire la ricerca oltre. */
                    if (entries[i].name[0] == 0x00) return -2;
                    continue;
                }
                if (entries[i].attr & 0x08) continue;   /* volume label */

                match = 1;
                for (j = 0; j < 8 && match; j++)
                    if (entries[i].name[j] != (uint8_t)name83[j]) match = 0;
                for (j = 0; j < 3 && match; j++)
                    if (entries[i].ext[j] != (uint8_t)name83[8 + j]) match = 0;
                if (!match) continue;
            }

            *out_lba  = lba;
            *out_slot = i;
            if (out_entry) *out_entry = entries[i];
            return 0;
        }

        cluster = fat12_get_next(cluster);
    }
    return -2;
}

/* =============================================================================
 * fat12_writeback_entry — riscrive su disco la entry di un file aperto
 *
 * È ciò che rende persistenti dimensione e primo cluster dopo una
 * scrittura. Per i file nella root si aggiorna la copia in RAM e si
 * segna g_root_dirty (verrà riversata dal flush); per quelli in una
 * sottodirectory si legge il settore, si sostituisce la entry e lo si
 * riscrive subito.
 * ============================================================================= */
static int fat12_writeback_entry(int handle)
{
    OpenFile *of = &g_open_files[handle];

    if (of->is_root_entry) {
        ((Fat12DirEntry *)g_root_dir)[of->root_index] = of->entry;
        g_root_dirty = 1;
        return 0;
    }

    if (of->dir_lba == 0) return -1;   /* posizione sconosciuta */

    {
        uint8_t        buf[BYTES_PER_SECTOR];
        Fat12DirEntry *entries = (Fat12DirEntry *)buf;

        if (fat12_read_sector(of->dir_lba, buf) != 0) return -1;
        entries[of->dir_slot] = of->entry;
        if (fat12_write_sector(of->dir_lba, buf) != 0) return -1;
    }
    return 0;
}

/* =============================================================================
 * fat12_open — Apre un file per lettura o scrittura
 * Ritorna handle >= 0 oppure errore negativo
 *
 * RISCRITTA (luglio 2026) per due difetti che si escludevano a vicenda
 * dalla vista:
 *
 * 1. Per un file GIÀ ESISTENTE nella root, root_index veniva calcolato
 *    come `(uint8_t*)entry - g_root_dir`, ma `entry` era stato appena
 *    riassegnato a `&path_entry` — una COPIA SULLO STACK. La differenza
 *    fra un indirizzo di stack e un array statico dà un indice fuori
 *    scala, che fat12_write usava poi per scrivere
 *    `g_root_dir[indice_assurdo]`: una scrittura fuori dai limiti nella
 *    memoria del kernel. Non emergeva perché il percorso di CREAZIONE
 *    (l'unico esercitato finora) impostava l'indice correttamente.
 *
 * 2. Per un file in una sottodirectory la entry non veniva mai
 *    riscritta: creare o modificare /boot/qualcosa sembrava riuscire ma
 *    non lasciava traccia. La creazione, per giunta, avveniva sempre
 *    nella ROOT ignorando la directory richiesta.
 *
 * Ora la entry viene localizzata esplicitamente (settore + posizione, o
 * indice nella root) invece di essere dedotta da un puntatore.
 * ============================================================================= */

/* =============================================================================
 * fat12_apply_trunc — svuota un file aperto con O_TRUNC
 *
 * BUG CORRETTO (luglio 2026): O_TRUNC era definito negli header ma
 * NESSUNO lo implementava. fat12_open lo ignorava, e siccome fat12_write
 * si accoda a entry->file_size, salvare su un file esistente APPENDEVA il
 * nuovo contenuto a quello vecchio invece di sostituirlo.
 *
 * Sintomo osservato salvando /boot/kernel.cfg da /bin/textline: il file
 * passava da 79 a 88 righe, con l'inizio del vecchio contenuto ricomparso
 * in coda. Qualunque editor, su qualunque file esistente, era inutilizzabile.
 *
 * Troncare significa liberare la catena di cluster, azzerare dimensione e
 * primo cluster, e rendere persistente la entry — altrimenti la FAT
 * continuerebbe a dichiarare occupati cluster che non appartengono più a
 * nessun file.
 * ============================================================================= */
static int fat12_apply_trunc(int slot)
{
    OpenFile *of = &g_open_files[slot];

    if (of->entry.first_cluster >= 2) {
        fat12_free_chain(of->entry.first_cluster);
        g_fat_dirty = 1;
    }
    of->entry.first_cluster = 0;
    of->entry.file_size     = 0;

    return fat12_writeback_entry(slot);
}

/* =============================================================================
 * fat12_mkdir — crea una directory
 *
 * Una directory in FAT12 è un file come gli altri, con l'attributo
 * DIRECTORY e un contenuto speciale: un elenco di entry da 32 byte, di cui
 * le prime due sono per convenzione "." (punta a sé stessa) e ".." (punta
 * al genitore, con cluster 0 quando il genitore è la root). Il resto del
 * cluster va azzerato: un name[0] a 0x00 significa "da qui in poi la
 * directory è vuota", ed è ciò che ferma le scansioni.
 *
 * LIMITE DELIBERATO — solo directory nella ROOT.
 *
 * Il resto di questo driver risolve i percorsi a UN SOLO livello
 * (fat12_find_path cerca la directory genitore esclusivamente nella
 * root). Creare "/boot/sub" produrrebbe una entry corretta sul supporto
 * ma irraggiungibile da qualunque altra funzione: non ci si potrebbe
 * entrare con cd, né aprirci file. Meglio rifiutare con un errore chiaro
 * che creare qualcosa di inutilizzabile — chi in futuro estenderà
 * fat12_find_path a più livelli toglierà anche questo controllo.
 *
 * Ritorna 0, oppure un errore negativo (-17 se esiste già).
 * ============================================================================= */
int fat12_mkdir(const char *path)
{
    char           name83[11];
    uint16_t       dir_cluster = 0;
    uint16_t       nuovo;
    uint8_t        buf[BYTES_PER_SECTOR];
    Fat12DirEntry *entries = (Fat12DirEntry *)buf;
    uint32_t       i;

    if (!g_initialized) return -1;

    if (fat12_split_path(path, &dir_cluster, name83) != 0) return -2;

    if (dir_cluster != 0) {
        klog(LOG_WARN, "FAT12: mkdir('%s') rifiutata - supportate solo "
             "directory nella root (percorsi a un solo livello)", path);
        return -38;   /* ENOSYS */
    }

    /* Esiste già? Vale sia per un file sia per una directory: in FAT12
     * condividono lo stesso spazio di nomi. */
    {
        Fat12DirEntry *root = (Fat12DirEntry *)g_root_dir;
        for (i = 0; i < ROOT_ENTRY_COUNT; i++) {
            uint8_t match, j;
            if (root[i].name[0] == 0x00) break;
            if ((uint8_t)root[i].name[0] == 0xE5) continue;
            if (root[i].attr & FAT12_ATTR_VOLUME) continue;

            match = 1;
            for (j = 0; j < 8 && match; j++)
                if (root[i].name[j] != (uint8_t)name83[j]) match = 0;
            for (j = 0; j < 3 && match; j++)
                if (root[i].ext[j] != (uint8_t)name83[8 + j]) match = 0;
            if (match) return -17;   /* EEXIST */
        }
    }

    /* Cluster che conterrà le entry della nuova directory */
    nuovo = fat12_alloc_cluster();
    if (nuovo == 0) {
        klog(LOG_ERROR, "FAT12: mkdir('%s') - disco pieno", path);
        return -28;   /* ENOSPC */
    }
    g_fat_dirty = 1;

    /* Contenuto iniziale: "." e "..", poi tutto zero. */
    for (i = 0; i < BYTES_PER_SECTOR; i++) buf[i] = 0;

    {
        uint8_t j;

        for (j = 0; j < 8; j++) entries[0].name[j] = ' ';
        for (j = 0; j < 3; j++) entries[0].ext[j]  = ' ';
        entries[0].name[0]        = '.';
        entries[0].attr           = FAT12_ATTR_DIRECTORY;
        entries[0].first_cluster  = nuovo;
        entries[0].file_size      = 0;

        for (j = 0; j < 8; j++) entries[1].name[j] = ' ';
        for (j = 0; j < 3; j++) entries[1].ext[j]  = ' ';
        entries[1].name[0]        = '.';
        entries[1].name[1]        = '.';
        entries[1].attr           = FAT12_ATTR_DIRECTORY;
        entries[1].first_cluster  = 0;   /* 0 = la root, per convenzione FAT */
        entries[1].file_size      = 0;
    }

    if (fat12_write_sector(fat12_cluster_to_lba(nuovo), buf) != 0) {
        fat12_free_chain(nuovo);
        klog(LOG_ERROR, "FAT12: mkdir('%s') - scrittura del cluster fallita", path);
        return -5;
    }

    /* Entry nella root che rende la directory visibile */
    {
        Fat12DirEntry *root = (Fat12DirEntry *)g_root_dir;

        for (i = 0; i < ROOT_ENTRY_COUNT; i++) {
            if (root[i].name[0] == 0x00 || (uint8_t)root[i].name[0] == 0xE5) {
                uint8_t j;
                for (j = 0; j < 8; j++) root[i].name[j] = (uint8_t)name83[j];
                for (j = 0; j < 3; j++) root[i].ext[j]  = (uint8_t)name83[8 + j];
                root[i].attr          = FAT12_ATTR_DIRECTORY;
                root[i].first_cluster = nuovo;
                root[i].file_size     = 0;   /* le directory hanno size 0 */
                { Fat12Istante ist = fat12_ora_corrente();
                  root[i].date = ist.data; root[i].time = ist.ora; }
                g_root_dirty = 1;

                /* Sincronizza subito: la cache è write-back, e una
                 * directory a metà (cluster scritto ma entry solo in RAM)
                 * lascerebbe un cluster occupato e invisibile se il
                 * sistema venisse spento adesso. */
                if (fat12_sync() != 0) {
                    klog(LOG_ERROR, "FAT12: mkdir('%s') - sync fallita", path);
                    return -5;
                }

                klog(LOG_INFO, "FAT12: creata directory '%s' (cluster %u)",
                     path, nuovo);
                return 0;
            }
        }
    }

    /* Nessuno slot libero: restituisci il cluster appena preso, altrimenti
     * resterebbe occupato senza appartenere a nulla. */
    fat12_free_chain(nuovo);
    klog(LOG_ERROR, "FAT12: mkdir('%s') - root directory piena", path);
    return -28;
}

/* =============================================================================
 * fat12_dir_vuota — la directory contiene solo "." e ".."?
 *
 * Ritorna 1 se vuota, 0 se contiene qualcosa, <0 su errore di lettura.
 *
 * Sulla convenzione FAT: name[0] == 0x00 significa "da qui in poi non c'è
 * più nulla" e permette di fermare la scansione; 0xE5 marca una entry
 * cancellata, che va saltata ma NON interrompe la scansione — dopo
 * potrebbero esserci ancora file vivi.
 * ============================================================================= */
static int fat12_dir_vuota(uint16_t cluster)
{
    uint8_t         buf[BYTES_PER_SECTOR];
    Fat12DirEntry  *entries = (Fat12DirEntry *)buf;
    uint32_t        i;

    while (cluster >= 2 && cluster < FAT12_RESERVED_MIN) {
        if (fat12_read_sector(fat12_cluster_to_lba(cluster), buf) != 0) return -1;

        for (i = 0; i < DIR_ENTRIES_PER_SECTOR; i++) {
            if (entries[i].name[0] == 0x00) return 1;      /* fine: era vuota */
            if ((uint8_t)entries[i].name[0] == 0xE5) continue;
            if (entries[i].attr & FAT12_ATTR_VOLUME) continue;

            /* "." e ".." ci sono per costruzione e non contano */
            if (entries[i].name[0] == '.' &&
                (entries[i].name[1] == ' ' || entries[i].name[1] == '.')) {
                continue;
            }

            return 0;   /* trovato un contenuto vero */
        }
        cluster = fat12_get_next(cluster);
    }
    return 1;
}

/* =============================================================================
 * fat12_rmdir — cancella una directory VUOTA
 *
 * Rifiuta se la directory contiene qualcosa: è il comportamento chiesto e
 * anche l'unico sensato qui, perché senza una cancellazione ricorsiva i
 * file rimasti dentro diventerebbero irraggiungibili e i loro cluster
 * resterebbero occupati per sempre — una perdita di spazio silenziosa che
 * solo un controllo del filesystem potrebbe recuperare.
 *
 * Come mkdir, opera solo sulla root: vedi lì la spiegazione del limite a
 * un livello.
 *
 * Ritorna 0, oppure: -2 non trovata, -20 non è una directory,
 * -39 non vuota, -22 percorso non valido.
 * ============================================================================= */
int fat12_rmdir(const char *path)
{
    char           name83[11];
    uint16_t       dir_cluster = 0;
    Fat12DirEntry *root = (Fat12DirEntry *)g_root_dir;
    uint32_t       i;

    if (!g_initialized) return -1;

    /* La root non si cancella. */
    if (path && path[0] == '/' && path[1] == '\0') {
        klog(LOG_WARN, "FAT12: rmdir('/') rifiutata - e' la directory radice");
        return -22;   /* EINVAL */
    }

    if (fat12_split_path(path, &dir_cluster, name83) != 0) return -2;

    if (dir_cluster != 0) {
        klog(LOG_WARN, "FAT12: rmdir('%s') rifiutata - supportate solo "
             "directory nella root (percorsi a un solo livello)", path);
        return -38;   /* ENOSYS */
    }

    for (i = 0; i < ROOT_ENTRY_COUNT; i++) {
        uint8_t match, j;

        if (root[i].name[0] == 0x00) break;
        if ((uint8_t)root[i].name[0] == 0xE5) continue;
        if (root[i].attr & FAT12_ATTR_VOLUME) continue;

        match = 1;
        for (j = 0; j < 8 && match; j++)
            if (root[i].name[j] != (uint8_t)name83[j]) match = 0;
        for (j = 0; j < 3 && match; j++)
            if (root[i].ext[j] != (uint8_t)name83[8 + j]) match = 0;
        if (!match) continue;

        /* Trovata: dev'essere davvero una directory. Cancellare un file
         * con rmdir sarebbe una sorpresa sgradevole. */
        if (!(root[i].attr & FAT12_ATTR_DIRECTORY)) {
            klog(LOG_WARN, "FAT12: rmdir('%s') - non e' una directory", path);
            return -20;   /* ENOTDIR */
        }

        {
            int vuota = fat12_dir_vuota(root[i].first_cluster);
            if (vuota < 0) return -5;
            if (vuota == 0) {
                klog(LOG_WARN, "FAT12: rmdir('%s') - la directory non e' vuota", path);
                return -39;   /* ENOTEMPTY */
            }
        }

        /* Ordine: prima libera i cluster, poi marca la entry. Se si
         * interrompesse a metà, un cluster libero con una entry ancora
         * viva è recuperabile; una entry cancellata con i cluster ancora
         * occupati sarebbe spazio perso senza modo di ritrovarlo. */
        if (root[i].first_cluster >= 2) {
            fat12_free_chain(root[i].first_cluster);
            g_fat_dirty = 1;
        }

        root[i].name[0] = (char)0xE5;   /* entry cancellata */
        g_root_dirty    = 1;

        /* Sincronizza subito: la cache è write-back, e lasciare la
         * cancellazione solo in RAM significherebbe che uno spegnimento
         * improvviso la annulla a metà. */
        if (fat12_sync() != 0) {
            klog(LOG_ERROR, "FAT12: rmdir('%s') - sync fallita", path);
            return -5;
        }

        klog(LOG_INFO, "FAT12: rimossa directory '%s'", path);
        return 0;
    }

    return -2;   /* ENOENT */
}

/* Rilascia lo slot prenotato da fat12_open e riporta l'errore: stesso
 * motivo e stessa forma di apri_fallito() nel VFS. */
static int fat12_open_fallito(int slot, int err)
{
    g_open_files[slot].used = 0;
    return err;
}

int fat12_open(const char *path, uint32_t flags)
{
    char     name83[11];
    int      slot;
    uint16_t dir_cluster = 0;

    if (!g_initialized) return -1;

    for (slot = 0; slot < MAX_OPEN_FILES; slot++) {
        if (!g_open_files[slot].used) break;
    }
    if (slot == MAX_OPEN_FILES) return -24;  /* EMFILE */

    /* ! Prenotazione IMMEDIATA dello slot, prima di qualunque accesso al
     * supporto. Da qui in giu' si legge dal floppy, e leggere dal floppy
     * significa un messaggio IPC al driver in ring3, cioe' un punto in
     * cui lo scheduler puo' dare la CPU a qualcun altro. Se `used`
     * venisse marcato solo al successo, un secondo processo entrato in
     * quella finestra troverebbe lo stesso slot libero e i due file
     * finirebbero nello stesso descrittore. Vedi il commento lungo in
     * vfs_open(): il guasto e' lo stesso, un piano piu' in su. */
    g_open_files[slot].used          = 1;
    g_open_files[slot].is_root_entry = 0;
    g_open_files[slot].root_index    = 0;
    g_open_files[slot].dir_lba       = 0;
    g_open_files[slot].dir_slot      = 0;

    if (fat12_split_path(path, &dir_cluster, name83) != 0)
        return fat12_open_fallito(slot, -2);

    /* --- file nella ROOT ---------------------------------------------- */
    if (dir_cluster == 0) {
        Fat12DirEntry *entries = (Fat12DirEntry *)g_root_dir;
        uint32_t i;

        for (i = 0; i < ROOT_ENTRY_COUNT; i++) {
            uint8_t match, j;
            if (entries[i].name[0] == 0x00) break;          /* fine directory */
            if ((uint8_t)entries[i].name[0] == 0xE5) continue;
            if (entries[i].attr & 0x08) continue;

            match = 1;
            for (j = 0; j < 8 && match; j++)
                if (entries[i].name[j] != (uint8_t)name83[j]) match = 0;
            for (j = 0; j < 3 && match; j++)
                if (entries[i].ext[j] != (uint8_t)name83[8 + j]) match = 0;
            if (!match) continue;

            g_open_files[slot].entry         = entries[i];
            g_open_files[slot].is_root_entry = 1;
            g_open_files[slot].root_index    = i;   /* indice VERO, non dedotto */

            if (flags & 0x0200) {                   /* O_TRUNC */
                if (fat12_apply_trunc(slot) != 0)
                    return fat12_open_fallito(slot, -5);
            }
            return slot;
        }

        /* niente O_CREAT → ENOENT */
        if (!(flags & 0x0040)) return fat12_open_fallito(slot, -2);

        for (i = 0; i < ROOT_ENTRY_COUNT; i++) {
            if (entries[i].name[0] == 0x00 || (uint8_t)entries[i].name[0] == 0xE5) {
                uint8_t j;
                for (j = 0; j < 8; j++) entries[i].name[j] = (uint8_t)name83[j];
                for (j = 0; j < 3; j++) entries[i].ext[j]  = (uint8_t)name83[8 + j];
                entries[i].attr          = 0x20;    /* Archive */
                entries[i].first_cluster = 0;
                entries[i].file_size     = 0;
                { Fat12Istante ist = fat12_ora_corrente();
                  entries[i].date = ist.data; entries[i].time = ist.ora; }
                g_root_dirty = 1;

                g_open_files[slot].entry         = entries[i];
                g_open_files[slot].is_root_entry = 1;
                g_open_files[slot].root_index    = i;
                return slot;
            }
        }
        return fat12_open_fallito(slot, -28);   /* ENOSPC: root dir piena */
    }

    /* --- file in una SOTTODIRECTORY ------------------------------------ */
    {
        uint16_t      lba;
        uint32_t      dslot;
        Fat12DirEntry e;

        if (fat12_dir_scan(dir_cluster, name83, &lba, &dslot, &e) == 0) {
            g_open_files[slot].entry    = e;
            g_open_files[slot].dir_lba  = lba;
            g_open_files[slot].dir_slot = dslot;

            if (flags & 0x0200) {                   /* O_TRUNC */
                if (fat12_apply_trunc(slot) != 0)
                    return fat12_open_fallito(slot, -5);
            }
            return slot;
        }

        if (!(flags & 0x0040)) return fat12_open_fallito(slot, -2);

        /* Creazione dentro la directory richiesta, non nella root. */
        if (fat12_dir_scan(dir_cluster, NULL, &lba, &dslot, NULL) != 0) {
            klog(LOG_ERROR, "FAT12: nessuno slot libero nella directory di '%s'", path);
            return fat12_open_fallito(slot, -28);
        }

        {
            uint8_t        buf[BYTES_PER_SECTOR];
            Fat12DirEntry *entries = (Fat12DirEntry *)buf;
            uint8_t        j;

            if (fat12_read_sector(lba, buf) != 0)
                return fat12_open_fallito(slot, -5);

            for (j = 0; j < 8; j++) entries[dslot].name[j] = (uint8_t)name83[j];
            for (j = 0; j < 3; j++) entries[dslot].ext[j]  = (uint8_t)name83[8 + j];
            entries[dslot].attr          = 0x20;
            entries[dslot].first_cluster = 0;
            entries[dslot].file_size     = 0;
            { Fat12Istante ist = fat12_ora_corrente();
              entries[dslot].date = ist.data; entries[dslot].time = ist.ora; }

            if (fat12_write_sector(lba, buf) != 0)
                return fat12_open_fallito(slot, -5);

            g_open_files[slot].entry    = entries[dslot];
            g_open_files[slot].dir_lba  = lba;
            g_open_files[slot].dir_slot = dslot;
            return slot;
        }
    }
}

/* =============================================================================
 * fat12_read — Legge N byte da un file aperto
 * ============================================================================= */
int fat12_read(int handle, void *buf, uint32_t size, uint32_t offset)
{
    Fat12DirEntry *entry;
    uint8_t       *dst = (uint8_t *)buf;
    uint16_t       cluster;
    uint32_t       cluster_offset;
    uint32_t       bytes_read = 0;
    uint8_t        sector_buf[BYTES_PER_SECTOR];

    if (!g_initialized || handle < 0 || (uint32_t)handle >= MAX_OPEN_FILES
        || !g_open_files[handle].used)
        return -1;

    entry = &g_open_files[handle].entry;
    if (entry->name[0] == 0x00 || (uint8_t)entry->name[0] == 0xE5) return -9;

    if (offset >= entry->file_size) return 0;   /* EOF */
    if (offset + size > entry->file_size)
        size = entry->file_size - offset;

    cluster = entry->first_cluster;
    if (cluster < 2) return 0;

    /* Salta i cluster fino all'offset */
    cluster_offset = 0;
    while (cluster_offset + BYTES_PER_SECTOR <= offset && cluster != FAT12_END) {
        cluster        = fat12_get_next(cluster);
        cluster_offset += BYTES_PER_SECTOR;
    }

    /* Leggi i dati */
    while (bytes_read < size && cluster != FAT12_END && cluster >= 2) {
        uint16_t lba = fat12_cluster_to_lba(cluster);
        if (fat12_read_sector(lba, sector_buf) != 0) return -5;

        uint32_t start = (cluster_offset < offset) ? (offset - cluster_offset) : 0;
        uint32_t end   = BYTES_PER_SECTOR;
        uint32_t avail = end - start;
        uint32_t take  = (avail < size - bytes_read) ? avail : (size - bytes_read);

        uint32_t i;
        for (i = 0; i < take; i++) dst[bytes_read + i] = sector_buf[start + i];

        bytes_read     += take;
        cluster_offset += BYTES_PER_SECTOR;
        cluster         = fat12_get_next(cluster);
    }

    return (int)bytes_read;
}

/* =============================================================================
 * fat12_write — Scrive N byte su un file
 * ============================================================================= */
int fat12_write(int handle, const void *buf, uint32_t size, uint32_t offset)
{
    Fat12DirEntry  *entry;
    const uint8_t  *src = (const uint8_t *)buf;
    uint16_t        cluster;
    uint32_t        written = 0;
    uint8_t         sector_buf[BYTES_PER_SECTOR];

    if (!g_initialized || handle < 0 || (uint32_t)handle >= MAX_OPEN_FILES
        || !g_open_files[handle].used)
        return -1;

    entry = &g_open_files[handle].entry;
    if (entry->name[0] == 0x00 || (uint8_t)entry->name[0] == 0xE5) return -9;

    /* Se file vuoto, alloca primo cluster */
    if (entry->first_cluster < 2) {
        entry->first_cluster = fat12_alloc_cluster();
        if (entry->first_cluster == 0) return -28;
        /* La entry viene riscritta per intero a fine funzione da
         * fat12_writeback_entry(): qui basta aver aggiornato la copia. */
    }

    /* =========================================================================
     * BUG CORRETTO (luglio 2026): questa funzione scriveva SEMPRE
     * dall'inizio dell'ultimo cluster, ignorando la posizione raggiunta
     * nel file. Ogni write() successiva sovrascriveva la precedente
     * invece di accodarsi.
     *
     * Con le quattro write() che /bin/textline fa per salvare due righe
     * ("prima riga", "\n", "seconda riga", "\n") il file finiva per
     * contenere "\neconda riga" con dimensione dichiarata 24 byte: ogni
     * chiamata ripartiva da offset 0 e l'ultima, di un solo byte, lasciava
     * in testa il '\n' sopra la 's'. La dimensione cresceva correttamente
     * perché era l'unica cosa a essere accumulata.
     *
     * Non era mai emerso perché nessun programma aveva mai scritto un
     * file: /bin/textline è il primo. Stessa storia della scrittura FDC
     * corretta poco sopra.
     *
     * Ora la scrittura parte da una POSIZIONE data, percorre la catena
     * dei cluster fino a quello che la contiene (allocandone di nuovi
     * quando serve) e scrive con read-modify-write sui settori riempiti
     * solo in parte.
     *
     * ! SECONDA CORREZIONE (agosto 2026): la posizione era
     * `entry->file_size`, cioe' SEMPRE la fine del file, e l'offset del
     * descrittore non arrivava fin qui — vfs_write_nl non lo passava,
     * "tanto fat12 tiene la propria posizione". Per un programma che
     * scrive dall'inizio alla fine e' lo stesso numero, e per anni non si
     * e' visto niente.
     *
     * Si vede al primo programma che TORNA INDIETRO. Un qualunque
     * scrittore di ELF lo fa — bfd scrive le sezioni, poi si riposiziona
     * a zero e ci mette l'intestazione — e il risultato sul floppy era un
     * oggetto con dentro tutti i pezzi giusti nell'ordine di scrittura e
     * il magic `\x7fELF` a offset 240:
     *
     *     /cdrom/bin/ld: /prova.o: file format not recognized
     *
     * cioe' il primo `as` nativo che assemblava benissimo e produceva un
     * file che nessuno sapeva rileggere. Su ext2 e FAT16/32 non succedeva:
     * li' l'offset il VFS lo passava gia'.
     * ========================================================================= */
    {
        uint32_t pos = offset;

        while (written < size) {
            uint32_t off  = pos + written;
            uint32_t idx  = off / BYTES_PER_SECTOR;   /* 1 cluster = 1 settore su FAT12 1.44MB */
            uint32_t in   = off % BYTES_PER_SECTOR;   /* offset dentro il cluster */
            uint32_t salti;
            uint32_t chunk;
            uint16_t lba;
            int      nuovo = 0;
            uint32_t i;

            /* Raggiungi il cluster numero 'idx' della catena, estendendola
             * se il file deve crescere. */
            cluster = entry->first_cluster;
            for (salti = 0; salti < idx; salti++) {
                uint16_t next = fat12_get_next(cluster);

                if (next == FAT12_END || next < 2) {
                    next = fat12_alloc_cluster();
                    if (next == 0) goto fine_scrittura;   /* disco pieno */
                    fat12_set_next(cluster, next);
                    nuovo = 1;
                }
                cluster = next;
            }

            lba   = fat12_cluster_to_lba(cluster);
            chunk = BYTES_PER_SECTOR - in;
            if (chunk > size - written) chunk = size - written;

            /* Se non si riempie il settore intero bisogna conservare ciò
             * che c'era: leggerlo prima. Per un settore che sta OLTRE la
             * fine attuale del file non c'è nulla da conservare — quel che
             * c'è sul disco è spazzatura di un file cancellato — e si
             * azzera.
             *
             * ! LA CONDIZIONE ERA `nuovo || in == 0`, ed è il secondo
             * difetto che si vede solo tornando indietro: scrivere 52 byte
             * all'inizio di un settore che ne conteneva già 512 azzerava
             * gli altri 460. bfd fa esattamente questo — scrive le
             * sezioni, poi si riposiziona a zero e ci mette
             * l'intestazione ELF — e il risultato era un oggetto con
             * l'intestazione giusta e il contenuto delle sezioni a zero:
             *
             *     ld: /prova.o: local symbol at index 4 (>= sh_info of 4)
             *
             * `in == 0` significa "scrivo dall'inizio del settore", non
             * "il settore e' vuoto". Le due cose coincidevano finché si
             * scriveva solo in coda.
             *
             * ! RESTA UN CASO NON COPERTO: un BUCO, cioè una scrittura
             * che comincia oltre la fine del file lasciando indietro dei
             * byte mai scritti. Quelli restano com'erano sul disco invece
             * di leggersi come zeri. Nessun programma lo fa oggi (as e ld
             * scrivono tutto e poi riscrivono l'intestazione); il giorno
             * che servisse, il posto è questo. */
            if (chunk < BYTES_PER_SECTOR) {
                uint32_t inizio_settore = off - in;

                if (nuovo || inizio_settore >= entry->file_size) {
                    for (i = 0; i < BYTES_PER_SECTOR; i++) sector_buf[i] = 0;
                } else if (fat12_read_sector(lba, sector_buf) != 0) {
                    return -5;
                }
            }

            for (i = 0; i < chunk; i++) sector_buf[in + i] = src[written + i];

            if (fat12_write_sector(lba, sector_buf) != 0) return -5;

            written += chunk;
        }

fine_scrittura:
        /* Il file cresce solo se si e' scritto OLTRE la fine. Riscrivere
         * in mezzo non lo accorcia: e' la differenza fra una scrittura e
         * un troncamento, e prima non si poneva perche' si scriveva solo
         * in coda. */
        if (pos + written > entry->file_size) entry->file_size = pos + written;
    }

    /* Rendi persistente la entry aggiornata (dimensione e primo cluster),
     * ovunque essa viva: root o sottodirectory. */
    if (fat12_writeback_entry(handle) != 0) {
        klog(LOG_ERROR, "FAT12: impossibile aggiornare la directory entry");
        return -5;
    }

    /* NIENTE flush qui — vedi sotto.
     *
     * PRESTAZIONI (luglio 2026): questa funzione riversava FAT e root
     * directory su disco a OGNI chiamata. Un editor salva riga per riga:
     * per un file di 79 righe sono 158 write(), ognuna delle quali
     * scriveva 18 settori di FAT (due copie da 9) più 14 di root
     * directory. Oltre 5000 scritture di settore per salvare 3.6KB —
     * il salvataggio impiegava minuti e sembrava bloccato.
     *
     * FAT e root dir restano marcate "sporche" e vengono riversate da
     * fat12_close() e da fat12_sync() (quest'ultima chiamata anche dalla
     * procedura di arresto, vedi kernel/arch/x86/power.c), che è il
     * momento giusto: una volta sola, a lavoro finito. */
    return (int)written;
}

/* =============================================================================
 * fat12_close — "Chiude" un handle (flush lazy)
 * ============================================================================= */
int fat12_close(int handle)
{
    if (handle >= 0 && (uint32_t)handle < MAX_OPEN_FILES) {
        g_open_files[handle].used = 0;
    }

    /* Sincronizzazione completa: FAT, root directory E cache dei settori.
     * Prima qui c'erano solo i due flush, sufficienti finché la cache era
     * write-through; con la cache write-back i dati del file resterebbero
     * in RAM e chiudere il file non garantirebbe più nulla. */
    return fat12_sync();
}

/* =============================================================================
 * fat12_stat — Informazioni su un file
 * ============================================================================= */
int fat12_stat(const char *path, Fat12Stat *st)
{
    char           name83[11];
    Fat12DirEntry *entry;

    Fat12DirEntry copia;

    if (!g_initialized || !st) return -1;
    (void)name83;

    /* Usa fat12_find_path invece della sola ricerca nella root: così
     * stat("/boot/kernel.cfg") funziona, non solo stat("/boot"). Serve
     * anche a chdir(), che verifica con stat che la destinazione esista e
     * sia una directory. */
    entry = fat12_find_path(path, &copia);
    if (!entry) return -2;
    entry = &copia;

    st->size         = entry->file_size;
    st->first_cluster = entry->first_cluster;
    st->attr         = entry->attr;
    st->date         = entry->date;
    st->time         = entry->time;
    return 0;
}

/* =============================================================================
 * fat12_readdir_path — Elenca una directory
 *
 *   path == NULL, "" oppure "/"  -> elenca la root
 *   path == "/NOME"              -> elenca quella subdirectory (un solo
 *                                    livello, stessa limitazione di
 *                                    fat12_find_path() sopra)
 *
 * A differenza della vecchia fat12_readdir() (che restituiva un puntatore
 * al buffer interno g_root_dir), qui si COPIA sempre in out_buf, fornito
 * dal chiamante: necessario per le subdirectory, che non hanno un buffer
 * permanente in RAM come la root, e comunque piu' corretto per chi chiama
 * da una syscall (non si deve mai restituire allo userspace un puntatore
 * a memoria interna del kernel).
 *
 * Ritorna 0 e *count_out = numero di entry copiate (fino a max_entries),
 * oppure -1 se il path non esiste o non e' una directory.
 * ============================================================================= */
int fat12_readdir_path(const char *path, Fat12DirEntry *out_buf,
                        uint32_t max_entries, uint32_t *count_out,
                        uint32_t start)
{
    uint32_t n       = 0;
    uint32_t saltate = 0;   /* voci valide gia' consegnate in chiamate precedenti */

    if (!g_initialized) return -1;
    if (!out_buf || max_entries == 0) return -1;

    if (!path || path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        /* Root: gia' interamente cache in RAM in g_root_dir */
        Fat12DirEntry *entries = (Fat12DirEntry *)g_root_dir;
        uint32_t i;

        for (i = 0; i < ROOT_ENTRY_COUNT && n < max_entries; i++) {
            if (entries[i].name[0] == 0x00) break;
            if ((uint8_t)entries[i].name[0] == 0xE5) continue;
            if (entries[i].attr & 0x08) continue;  /* Volume label */
            if (saltate++ < start) continue;       /* gia' consegnata */
            out_buf[n++] = entries[i];
        }
        *count_out = n;
        return 0;
    }

    /* Subdirectory: risolvi l'unico componente del path nella root */
    {
        char name83[11];
        path_to_fat83(path, name83);
        Fat12DirEntry *dir_entry = fat12_find_in_root(name83);
        if (!dir_entry || !(dir_entry->attr & FAT12_ATTR_DIRECTORY)) return -1;

        uint16_t cluster = dir_entry->first_cluster;
        while (cluster != FAT12_END && cluster >= 2 &&
               cluster < FAT12_RESERVED_MIN && n < max_entries) {
            uint8_t        buf[BYTES_PER_SECTOR];
            Fat12DirEntry *entries = (Fat12DirEntry *)buf;
            uint16_t       lba  = fat12_cluster_to_lba(cluster);
            uint32_t       i;
            int            done = 0;

            if (fat12_read_sector(lba, buf) != 0) break;

            for (i = 0; i < BYTES_PER_SECTOR / 32 && n < max_entries; i++) {
                if (entries[i].name[0] == 0x00) { done = 1; break; }
                if ((uint8_t)entries[i].name[0] == 0xE5) continue;
                if (entries[i].attr & 0x08) continue;
                if (saltate++ < start) continue;   /* gia' consegnata */
                out_buf[n++] = entries[i];
            }
            if (done) break;
            cluster = fat12_get_next(cluster);
        }
        *count_out = n;
        return 0;
    }
}

/* =============================================================================
 * fat12_format_name — Converte il nome 8.3 raw (padding a spazi, non
 * null-terminated) in una stringa leggibile "NOME.EXT" (o solo "NOME" se
 * senza estensione), null-terminated. `out` deve avere almeno 13 byte.
 * ============================================================================= */
void fat12_format_name(const Fat12DirEntry *entry, char *out)
{
    uint32_t i, n = 0;

    for (i = 0; i < 8 && entry->name[i] != ' '; i++) out[n++] = (char)entry->name[i];
    if (entry->ext[0] != ' ') {
        out[n++] = '.';
        for (i = 0; i < 3 && entry->ext[i] != ' '; i++) out[n++] = (char)entry->ext[i];
    }
    out[n] = '\0';
}

/* =============================================================================
 * fat12_delete — Cancella un file
 * ============================================================================= */
/* =============================================================================
 * fat12_delete — cancella un FILE
 *
 * RISCRITTA (luglio 2026). La versione precedente aveva tre problemi, tutti
 * invisibili perché nessuna syscall la esponeva: era irraggiungibile da
 * userspace fino a /bin/delete.
 *
 * 1. Cercava SOLO nella root (fat12_find_in_root), quindi non poteva
 *    cancellare nulla dentro una sottodirectory.
 * 2. Non controllava l'attributo DIRECTORY: cancellare una directory così
 *    ne avrebbe liberato il cluster lasciando i file contenuti
 *    irraggiungibili, con i loro cluster occupati per sempre. Per le
 *    directory esiste fat12_rmdir, che verifica che siano vuote.
 * 3. Usava fat12_flush_fat/root senza sincronizzare la cache dei settori:
 *    dopo il passaggio a write-back la cancellazione sarebbe rimasta in
 *    RAM.
 *
 * Ritorna 0, -2 non trovato, -21 se è una directory (EISDIR).
 * ============================================================================= */
int fat12_delete(const char *path)
{
    char     name83[11];
    uint16_t dir_cluster = 0;

    if (!g_initialized) return -1;

    if (fat12_split_path(path, &dir_cluster, name83) != 0) return -2;

    /* --- file nella ROOT ---------------------------------------------- */
    if (dir_cluster == 0) {
        Fat12DirEntry *root = (Fat12DirEntry *)g_root_dir;
        uint32_t       i;

        for (i = 0; i < ROOT_ENTRY_COUNT; i++) {
            uint8_t match, j;

            if (root[i].name[0] == 0x00) break;
            if ((uint8_t)root[i].name[0] == 0xE5) continue;
            if (root[i].attr & FAT12_ATTR_VOLUME) continue;

            match = 1;
            for (j = 0; j < 8 && match; j++)
                if (root[i].name[j] != (uint8_t)name83[j]) match = 0;
            for (j = 0; j < 3 && match; j++)
                if (root[i].ext[j] != (uint8_t)name83[8 + j]) match = 0;
            if (!match) continue;

            if (root[i].attr & FAT12_ATTR_DIRECTORY) return -21;   /* EISDIR */

            if (root[i].first_cluster >= 2) {
                fat12_free_chain(root[i].first_cluster);
                g_fat_dirty = 1;
            }
            root[i].name[0] = (char)0xE5;
            g_root_dirty    = 1;

            /* NIENTE fat12_sync() qui: sincronizzare a ogni file
             * significa riscrivere FAT e root directory (32 settori) per
             * ogni cancellazione. /bin/delete con un modello jolly ne
             * cancella decine in un colpo, e il comando diventava
             * inutilizzabile — misurato: 10 file in 60 secondi.
             * La sincronizzazione avviene una volta sola all'uscita del
             * processo (sys_exit) e in fat12_close/power_*. */
            klog(LOG_DEBUG, "FAT12: cancellato '%s'", path);
            return 0;
        }
        return -2;
    }

    /* --- file in una SOTTODIRECTORY ------------------------------------ */
    {
        uint16_t       lba;
        uint32_t       slot;
        Fat12DirEntry  e;
        uint8_t        buf[BYTES_PER_SECTOR];
        Fat12DirEntry *entries = (Fat12DirEntry *)buf;

        if (fat12_dir_scan(dir_cluster, name83, &lba, &slot, &e) != 0) return -2;
        if (e.attr & FAT12_ATTR_DIRECTORY) return -21;

        if (e.first_cluster >= 2) {
            fat12_free_chain(e.first_cluster);
            g_fat_dirty = 1;
        }

        if (fat12_read_sector(lba, buf) != 0) return -5;
        entries[slot].name[0] = (uint8_t)0xE5;
        if (fat12_write_sector(lba, buf) != 0) return -5;

        klog(LOG_DEBUG, "FAT12: cancellato '%s'", path);
        return 0;
    }
}

/* =============================================================================
 * fat12_rename — cambia il NOME di una voce, senza spostare i dati
 *
 * ! PERCHE' ANCHE QUI, sul driver che «non si tocca senza una ragione
 * forte». La ragione forte c'e': fino alla 0.160 la rename() della libc
 * era copia+cancella e funzionava su qualunque volume, floppy compreso.
 * Sostituendola con la syscall — che riscrive la voce di directory e non
 * muove i dati — il floppy sarebbe rimasto senza rename, cioe' una
 * regressione introdotta da una correzione. Meglio implementarla che
 * lasciare un ripiego che copia di nascosto: quel ripiego rimetterebbe in
 * piedi proprio il comportamento che `install` non puo' permettersi.
 *
 * ! I RITORNI SONO CODICI errno, NON la convenzione -1/-2/-3 di fat.c e
 * ext2.c. Questo driver ha sempre risposto cosi' — fat12_delete ritorna -2
 * per "non trovato" e -21 per "e' una directory" — e il VFS li rigira al
 * chiamante senza tradurli. Mescolare le due convenzioni e' esattamente
 * l'errore che ha fatto rispondere «esiste gia'» a una rinomina di un file
 * che non esisteva: -2 vuol dire ENOENT qui e EEXIST la'.
 *
 *   0    riuscita
 *   -2   ENOENT   il file di partenza non c'e'
 *   -17  EEXIST   la destinazione c'e' gia'
 *   -38  ENOSYS   directory diverse: questo non e' uno spostamento
 *
 * ! NIENTE fat12_sync() qui, come per fat12_delete: la sincronizzazione
 * avviene all'uscita del processo. Vedi il commento esteso li'.
 * ============================================================================= */
int fat12_rename(const char *da, const char *a)
{
    char     n_da[11], n_a[11];
    uint16_t d_da = 0, d_a = 0;
    uint8_t  j;

    if (!g_initialized) return -1;

    if (fat12_split_path(da, &d_da, n_da) != 0) return -2;   /* ENOENT */
    if (fat12_split_path(a,  &d_a,  n_a)  != 0) return -2;

    /* Directory diverse: non e' una rinomina. */
    if (d_da != d_a) return -38;                              /* ENOSYS */

    /* --- la destinazione non deve esistere --- */
    if (d_a == 0) {
        Fat12DirEntry *root = (Fat12DirEntry *)g_root_dir;
        uint32_t i;

        for (i = 0; i < ROOT_ENTRY_COUNT; i++) {
            uint8_t match = 1;

            if (root[i].name[0] == 0x00) break;
            if ((uint8_t)root[i].name[0] == 0xE5) continue;
            if (root[i].attr & FAT12_ATTR_VOLUME) continue;

            for (j = 0; j < 8 && match; j++)
                if (root[i].name[j] != (uint8_t)n_a[j]) match = 0;
            for (j = 0; j < 3 && match; j++)
                if (root[i].ext[j] != (uint8_t)n_a[8 + j]) match = 0;
            if (match) return -17;                            /* EEXIST */
        }
    } else {
        uint16_t lba; uint32_t slot; Fat12DirEntry e;
        if (fat12_dir_scan(d_a, n_a, &lba, &slot, &e) == 0) return -17;
    }

    /* --- si riscrive il nome dentro la voce, e basta --- */
    if (d_da == 0) {
        Fat12DirEntry *root = (Fat12DirEntry *)g_root_dir;
        uint32_t i;

        for (i = 0; i < ROOT_ENTRY_COUNT; i++) {
            uint8_t match = 1;

            if (root[i].name[0] == 0x00) break;
            if ((uint8_t)root[i].name[0] == 0xE5) continue;
            if (root[i].attr & FAT12_ATTR_VOLUME) continue;

            for (j = 0; j < 8 && match; j++)
                if (root[i].name[j] != (uint8_t)n_da[j]) match = 0;
            for (j = 0; j < 3 && match; j++)
                if (root[i].ext[j] != (uint8_t)n_da[8 + j]) match = 0;
            if (!match) continue;

            for (j = 0; j < 8; j++) root[i].name[j] = n_a[j];
            for (j = 0; j < 3; j++) root[i].ext[j]  = n_a[8 + j];
            g_root_dirty = 1;

            klog(LOG_DEBUG, "FAT12: rinominato '%s' in '%s'", da, a);
            return 0;
        }
        return -2;
    }

    {
        uint16_t       lba;
        uint32_t       slot;
        Fat12DirEntry  e;
        uint8_t        buf[BYTES_PER_SECTOR];
        Fat12DirEntry *entries = (Fat12DirEntry *)buf;

        if (fat12_dir_scan(d_da, n_da, &lba, &slot, &e) != 0) return -2;

        if (fat12_read_sector(lba, buf) != 0) return -5;
        for (j = 0; j < 8; j++) entries[slot].name[j] = n_a[j];
        for (j = 0; j < 3; j++) entries[slot].ext[j]  = n_a[8 + j];
        if (fat12_write_sector(lba, buf) != 0) return -5;

        klog(LOG_DEBUG, "FAT12: rinominato '%s' in '%s'", da, a);
        return 0;
    }
}

/* =============================================================================
 * fat12_pronto — c'e' davvero un floppy sotto?
 *
 * ! SERVE A DISTINGUERE UN AVVIO DA CD DA UNO DA FLOPPY, e non c'e' altro
 * modo. Con l'emulazione El Torito il BIOS presenta il CD come l'unita'
 * 0x00: dal numero del drive i due casi sono identici. Ma questo driver
 * programma il controller alle porte hardware, e dietro l'emulazione un
 * controller non c'e': fat12_init fallisce, e quel fallimento E' il
 * segnale. Vedi vfs_init() in kernel/fs/vfs.c.
 * ============================================================================= */
int fat12_pronto(void)
{
    return g_initialized ? 1 : 0;
}

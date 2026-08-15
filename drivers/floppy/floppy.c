/* =============================================================================
 * drivers/floppy/floppy.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Driver floppy controller 82077AA per EX-OS.
 * Compilato come modulo ELF (/dev/floppy.drv).
 *
 * Gestisce:
 *   - Reset e inizializzazione FDC
 *   - Lettura/scrittura settori via PIO (no DMA in questa versione)
 *   - Geometria fissa 1.44MB: 80 cil / 2 test / 18 sett
 *   - Interfaccia driver standard: drv_init/read/write/ioctl/exit
 *
 * Comandi FDC usati:
 *   0x03 SPECIFY    — imposta timing stepping/head load/unload
 *   0x07 RECALIBRATE — porta testina a cilindro 0
 *   0x0F SEEK        — sposta testina al cilindro richiesto
 *   0x46 READ DATA   — legge un settore
 *   0xC5 WRITE DATA  — scrive un settore
 *   0x08 SENSE INT   — legge risultato interrupt
 * ============================================================================= */

/* Simboli importati dal kernel (risolti dal drvmgr via tabella kernel exports) */
extern void    kprintf(const char *fmt, ...);
extern void    klog(int level, const char *fmt, ...);
extern unsigned char  port_inb(unsigned short port);
extern void    port_outb(unsigned short port, unsigned char val);
extern void    io_delay(void);
extern void    irq_register_handler(unsigned char irq, void *handler);
extern void    pic_mask_irq(unsigned char irq);
extern void    pic_unmask_irq(unsigned char irq);
extern void    pic_send_eoi(unsigned char irq);
extern void    sched_block(int reason);
extern void    sched_unblock(unsigned int pid);

/* Tipi base (no libc) */
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned int   size_t;
#define NULL ((void*)0)

/* Log levels */
#define LOG_ERROR 1
#define LOG_WARN  2
#define LOG_INFO  3
#define LOG_DEBUG 4

/* =============================================================================
 * Registri FDC
 * ============================================================================= */
#define FDC_DOR     0x3F2   /* Digital Output Register */
#define FDC_MSR     0x3F4   /* Main Status Register */
#define FDC_FIFO    0x3F5   /* Data FIFO */
#define FDC_DIR     0x3F7   /* Digital Input Register */
#define FDC_CCR     0x3F7   /* Configuration Control Register (write) */

/* Bit DOR */
#define DOR_RESET   0x00    /* Reset FDC */
#define DOR_NORESET 0x04    /* Enable FDC */
#define DOR_DMAEN   0x08    /* Enable DMA/IRQ */
#define DOR_MOTA    0x10    /* Motor drive A */

/* Bit MSR */
#define MSR_RQM     0x80    /* Request for Master — FIFO pronto */
#define MSR_DIO     0x40    /* Data I/O direction (1=FDC→CPU) */
#define MSR_NONDMA  0x20    /* Non-DMA mode */
#define MSR_BUSY    0x10    /* FDC occupato */

/* Comandi FDC */
#define CMD_SPECIFY     0x03
#define CMD_SENSE_INT   0x08
#define CMD_RECALIBRATE 0x07
#define CMD_SEEK        0x0F
#define CMD_READ        0x46    /* MT=0, MFM=1, SK=0 */
#define CMD_WRITE       0xC5    /* MT=1, MFM=1 */

/* Geometria 1.44MB */
#define SECTORS_PER_TRACK   18
#define NUM_HEADS           2
#define BYTES_PER_SECTOR    512
#define SECTOR_SIZE_CODE    2   /* 2 → 512 byte */
#define GAP_LENGTH          0x1B
#define DATA_LENGTH         0xFF

/* =============================================================================
 * Stato driver
 * ============================================================================= */
static volatile uint8_t g_irq_fired  = 0;
static volatile uint32_t g_wait_pid  = 0;
static uint8_t  g_current_cylinder   = 0xFF; /* Cilindro corrente (0xFF = sconosciuto) */
static uint8_t  g_motor_on           = 0;
static uint8_t  g_initialized        = 0;

/* =============================================================================
 * IRQ6 handler — segnala completamento operazione FDC
 * ============================================================================= */
static void floppy_irq6_handler(void *frame)
{
    (void)frame;
    g_irq_fired = 1;
    pic_send_eoi(6);
    if (g_wait_pid != 0) {
        sched_unblock(g_wait_pid);
        g_wait_pid = 0;
    }
}

/* =============================================================================
 * Attende che FDC sia pronto (MSR.RQM = 1)
 * ============================================================================= */
static int fdc_wait_rqm(void)
{
    uint32_t timeout = 500000;
    while (timeout--) {
        if (port_inb(FDC_MSR) & MSR_RQM) return 0;
        io_delay();
    }
    klog(LOG_ERROR, "FLOPPY: timeout MSR RQM");
    return -1;
}

/* Invia un byte al FIFO */
static int fdc_write(uint8_t b)
{
    if (fdc_wait_rqm() != 0) return -1;
    uint8_t msr = port_inb(FDC_MSR);
    if (msr & MSR_DIO) { klog(LOG_ERROR, "FLOPPY: FDC in read mode"); return -1; }
    port_outb(FDC_FIFO, b);
    return 0;
}

/* Legge un byte dal FIFO */
static int fdc_read(uint8_t *b)
{
    if (fdc_wait_rqm() != 0) return -1;
    uint8_t msr = port_inb(FDC_MSR);
    if (!(msr & MSR_DIO)) { klog(LOG_ERROR, "FLOPPY: FDC in write mode"); return -1; }
    *b = port_inb(FDC_FIFO);
    return 0;
}

/* Attende IRQ6 (con timeout) */
static int fdc_wait_irq(void)
{
    uint32_t timeout = 1000000;
    while (!g_irq_fired && timeout--) {
        io_delay();
    }
    if (!g_irq_fired) {
        klog(LOG_ERROR, "FLOPPY: timeout IRQ6");
        return -1;
    }
    g_irq_fired = 0;
    return 0;
}

/* Legge risultato SENSE INTERRUPT (dopo operazioni che generano IRQ) */
static int fdc_sense_interrupt(uint8_t *st0, uint8_t *cyl)
{
    if (fdc_write(CMD_SENSE_INT) != 0) return -1;
    if (fdc_read(st0)            != 0) return -1;
    if (fdc_read(cyl)            != 0) return -1;
    return 0;
}

/* =============================================================================
 * Motore ON/OFF con delay stabilizzazione
 * ============================================================================= */
static void fdc_motor_on(void)
{
    if (g_motor_on) return;
    port_outb(FDC_DOR, DOR_NORESET | DOR_DMAEN | DOR_MOTA);
    /* Delay ~300ms per stabilizzazione motore (busy wait) */
    uint32_t i;
    for (i = 0; i < 15000000; i++) __asm__ volatile("nop");
    g_motor_on = 1;
    klog(LOG_DEBUG, "FLOPPY: motore acceso");
}

static void fdc_motor_off(void)
{
    if (!g_motor_on) return;
    port_outb(FDC_DOR, DOR_NORESET | DOR_DMAEN);
    g_motor_on = 0;
    klog(LOG_DEBUG, "FLOPPY: motore spento");
}

/* =============================================================================
 * Reset FDC completo
 * ============================================================================= */
static int fdc_reset(void)
{
    uint8_t st0, cyl;
    uint32_t i;

    klog(LOG_DEBUG, "FLOPPY: reset FDC...");

    /* Reset via DOR */
    port_outb(FDC_DOR, DOR_RESET);
    for (i = 0; i < 100000; i++) __asm__ volatile("nop");
    port_outb(FDC_DOR, DOR_NORESET | DOR_DMAEN);
    for (i = 0; i < 100000; i++) __asm__ volatile("nop");

    /* Attende IRQ reset */
    g_irq_fired = 0;
    if (fdc_wait_irq() != 0) {
        klog(LOG_WARN, "FLOPPY: IRQ reset non ricevuto, continuo");
    }

    /* Sense interrupt per tutti e 4 i drive (richiesto dopo reset) */
    for (i = 0; i < 4; i++) {
        fdc_sense_interrupt(&st0, &cyl);
    }

    /* SPECIFY: SRT=13ms HUT=240ms HLT=4ms NDMA=0 */
    fdc_write(CMD_SPECIFY);
    fdc_write(0xDF);    /* SRT + HUT */
    fdc_write(0x02);    /* HLT + NDMA */

    /* Imposta velocità trasferimento: 500Kbps per 1.44MB */
    port_outb(FDC_CCR, 0x00);

    klog(LOG_DEBUG, "FLOPPY: FDC reset OK");
    return 0;
}

/* =============================================================================
 * Recalibrate — porta la testina al cilindro 0
 * ============================================================================= */
static int fdc_recalibrate(void)
{
    uint8_t st0, cyl;
    uint32_t attempts = 3;

    while (attempts--) {
        g_irq_fired = 0;
        fdc_write(CMD_RECALIBRATE);
        fdc_write(0x00);    /* Drive 0 */
        if (fdc_wait_irq() != 0) continue;
        fdc_sense_interrupt(&st0, &cyl);

        if ((st0 & 0x20) && cyl == 0) {
            g_current_cylinder = 0;
            klog(LOG_DEBUG, "FLOPPY: recalibrate OK (cyl=0)");
            return 0;
        }
        klog(LOG_WARN, "FLOPPY: recalibrate fallita (st0=0x%02x cyl=%u)", st0, cyl);
    }
    return -1;
}

/* =============================================================================
 * Seek — sposta la testina al cilindro richiesto
 * ============================================================================= */
static int fdc_seek(uint8_t cylinder, uint8_t head)
{
    uint8_t st0, cyl;
    uint32_t attempts = 3;

    if (g_current_cylinder == cylinder) return 0;

    while (attempts--) {
        g_irq_fired = 0;
        fdc_write(CMD_SEEK);
        fdc_write((uint8_t)(head << 2));    /* Drive 0, head */
        fdc_write(cylinder);
        if (fdc_wait_irq() != 0) continue;
        fdc_sense_interrupt(&st0, &cyl);

        if ((st0 & 0x20) && cyl == cylinder) {
            g_current_cylinder = cylinder;
            /* Settle time */
            uint32_t i;
            for (i = 0; i < 500000; i++) __asm__ volatile("nop");
            return 0;
        }
        klog(LOG_WARN, "FLOPPY: seek fallito cyl=%u st0=0x%02x", cylinder, st0);
    }
    return -1;
}

/* =============================================================================
 * LBA → CHS conversion
 * ============================================================================= */
static void lba_to_chs(uint16_t lba, uint8_t *cyl, uint8_t *head, uint8_t *sec)
{
    *sec  = (uint8_t)((lba % SECTORS_PER_TRACK) + 1);
    *head = (uint8_t)((lba / SECTORS_PER_TRACK) % NUM_HEADS);
    *cyl  = (uint8_t)((lba / SECTORS_PER_TRACK) / NUM_HEADS);
}

/* =============================================================================
 * Legge un settore LBA in buf (512 byte)
 * Riprova fino a 3 volte su errore.
 * ============================================================================= */
static int floppy_read_sector(uint16_t lba, uint8_t *buf)
{
    uint8_t cyl, head, sec;
    uint8_t st0, st1, st2, c, h, s, bps;
    uint32_t i, attempts = 3;

    lba_to_chs(lba, &cyl, &head, &sec);

    while (attempts--) {
        fdc_motor_on();
        if (fdc_seek(cyl, head) != 0) {
            fdc_recalibrate();
            continue;
        }

        g_irq_fired = 0;

        /* Comando READ */
        fdc_write(CMD_READ);
        fdc_write((uint8_t)(head << 2));    /* HD + DS */
        fdc_write(cyl);
        fdc_write(head);
        fdc_write(sec);
        fdc_write(SECTOR_SIZE_CODE);
        fdc_write(SECTORS_PER_TRACK);
        fdc_write(GAP_LENGTH);
        fdc_write(DATA_LENGTH);

        /* Leggi i 512 byte dal FIFO */
        for (i = 0; i < BYTES_PER_SECTOR; i++) {
            if (fdc_read(&buf[i]) != 0) {
                klog(LOG_ERROR, "FLOPPY: errore lettura byte %u LBA=%u", i, lba);
                break;
            }
        }

        if (fdc_wait_irq() != 0) {
            klog(LOG_WARN, "FLOPPY: timeout IRQ read LBA=%u", lba);
            continue;
        }

        /* Leggi 7 byte di stato */
        fdc_read(&st0); fdc_read(&st1); fdc_read(&st2);
        fdc_read(&c);   fdc_read(&h);   fdc_read(&s);
        fdc_read(&bps);

        if (!(st0 & 0xC0)) {
            klog(LOG_DEBUG, "FLOPPY: lettura OK LBA=%u", lba);
            return 0;
        }

        klog(LOG_WARN, "FLOPPY: errore lettura LBA=%u ST0=0x%02x ST1=0x%02x",
             lba, st0, st1);
        fdc_reset();
        fdc_recalibrate();
    }

    return -1;
}

/* =============================================================================
 * Scrive un settore LBA da buf (512 byte)
 * ============================================================================= */
static int floppy_write_sector(uint16_t lba, const uint8_t *buf)
{
    uint8_t cyl, head, sec;
    uint8_t st0, st1, st2, c, h, s, bps;
    uint32_t i, attempts = 3;

    lba_to_chs(lba, &cyl, &head, &sec);

    while (attempts--) {
        fdc_motor_on();
        if (fdc_seek(cyl, head) != 0) {
            fdc_recalibrate();
            continue;
        }

        g_irq_fired = 0;

        /* Comando WRITE */
        fdc_write(CMD_WRITE);
        fdc_write((uint8_t)(head << 2));
        fdc_write(cyl);
        fdc_write(head);
        fdc_write(sec);
        fdc_write(SECTOR_SIZE_CODE);
        fdc_write(SECTORS_PER_TRACK);
        fdc_write(GAP_LENGTH);
        fdc_write(DATA_LENGTH);

        /* Scrivi i 512 byte nel FIFO */
        for (i = 0; i < BYTES_PER_SECTOR; i++) {
            if (fdc_wait_rqm() != 0) break;
            port_outb(FDC_FIFO, buf[i]);
        }

        if (fdc_wait_irq() != 0) continue;

        fdc_read(&st0); fdc_read(&st1); fdc_read(&st2);
        fdc_read(&c);   fdc_read(&h);   fdc_read(&s);
        fdc_read(&bps);

        if (!(st0 & 0xC0)) {
            klog(LOG_DEBUG, "FLOPPY: scrittura OK LBA=%u", lba);
            return 0;
        }

        klog(LOG_WARN, "FLOPPY: errore scrittura LBA=%u ST0=0x%02x", lba, st0);
        fdc_reset();
        fdc_recalibrate();
    }

    return -1;
}

/* =============================================================================
 * Struttura ioctl per operazioni raw
 * ============================================================================= */
typedef struct {
    uint16_t lba;
    uint8_t *buf;
    uint32_t count;
} FloppyIoctlRaw;

#define FLOPPY_IOCTL_READ_RAW    0x01
#define FLOPPY_IOCTL_WRITE_RAW   0x02
#define FLOPPY_IOCTL_RESET       0x03
#define FLOPPY_IOCTL_MOTOR_OFF   0x04
#define FLOPPY_IOCTL_GET_GEOM    0x05

typedef struct {
    uint16_t cylinders;
    uint8_t  heads;
    uint8_t  sectors;
    uint16_t bytes_per_sector;
} FloppyGeometry;

/* =============================================================================
 * Interfaccia driver standard
 * ============================================================================= */

int drv_init(void)
{
    klog(LOG_INFO, "FLOPPY: inizializzazione driver...");

    g_irq_fired        = 0;
    g_wait_pid         = 0;
    g_current_cylinder = 0xFF;
    g_motor_on         = 0;

    /* Registra IRQ6 */
    irq_register_handler(6, floppy_irq6_handler);
    pic_unmask_irq(6);

    /* Reset e calibrazione iniziale */
    if (fdc_reset() != 0) {
        klog(LOG_ERROR, "FLOPPY: reset FDC fallito");
        return -1;
    }
    if (fdc_recalibrate() != 0) {
        klog(LOG_WARN, "FLOPPY: recalibrate fallita, continuo");
    }

    g_initialized = 1;
    klog(LOG_INFO, "FLOPPY: driver inizializzato (IRQ6, 1.44MB, 80/2/18)");
    return 0;
}

/* drv_read: legge 'n' settori a partire dall'offset nel buf.
 * Usa il campo 'ioctl' per passare l'LBA iniziale.
 * Per semplicità: buf deve avere n*512 byte, il primo uint16_t è l'LBA. */
int drv_read(void *buf, size_t n)
{
    /* Interpreta i primi 2 byte come LBA */
    uint16_t  lba    = *((uint16_t *)buf);
    uint8_t  *data   = (uint8_t *)buf + 2;
    uint32_t  i;

    if (!g_initialized || !buf || n == 0) return -1;

    for (i = 0; i < n; i++) {
        if (floppy_read_sector((uint16_t)(lba + i),
                               data + i * BYTES_PER_SECTOR) != 0)
            return (int)i;
    }
    return (int)n;
}

int drv_write(const void *buf, size_t n)
{
    uint16_t        lba  = *((const uint16_t *)buf);
    const uint8_t  *data = (const uint8_t *)buf + 2;
    uint32_t        i;

    if (!g_initialized || !buf || n == 0) return -1;

    for (i = 0; i < n; i++) {
        if (floppy_write_sector((uint16_t)(lba + i),
                                data + i * BYTES_PER_SECTOR) != 0)
            return (int)i;
    }
    return (int)n;
}

int drv_ioctl(int cmd, void *arg)
{
    if (!g_initialized) return -1;

    switch (cmd) {
        case FLOPPY_IOCTL_RESET:
            return fdc_reset();

        case FLOPPY_IOCTL_MOTOR_OFF:
            fdc_motor_off();
            return 0;

        case FLOPPY_IOCTL_GET_GEOM: {
            FloppyGeometry *g = (FloppyGeometry *)arg;
            if (!g) return -1;
            g->cylinders        = 80;
            g->heads            = NUM_HEADS;
            g->sectors          = SECTORS_PER_TRACK;
            g->bytes_per_sector = BYTES_PER_SECTOR;
            return 0;
        }

        case FLOPPY_IOCTL_READ_RAW: {
            FloppyIoctlRaw *r = (FloppyIoctlRaw *)arg;
            if (!r || !r->buf) return -1;
            uint32_t i;
            for (i = 0; i < r->count; i++) {
                if (floppy_read_sector((uint16_t)(r->lba + i),
                                       r->buf + i * BYTES_PER_SECTOR) != 0)
                    return (int)i;
            }
            return (int)r->count;
        }

        case FLOPPY_IOCTL_WRITE_RAW: {
            FloppyIoctlRaw *r = (FloppyIoctlRaw *)arg;
            if (!r || !r->buf) return -1;
            uint32_t i;
            for (i = 0; i < r->count; i++) {
                if (floppy_write_sector((uint16_t)(r->lba + i),
                                        r->buf + i * BYTES_PER_SECTOR) != 0)
                    return (int)i;
            }
            return (int)r->count;
        }
    }
    return -1;
}

void drv_exit(void)
{
    fdc_motor_off();
    pic_mask_irq(6);
    g_initialized = 0;
    klog(LOG_INFO, "FLOPPY: driver scaricato");
}

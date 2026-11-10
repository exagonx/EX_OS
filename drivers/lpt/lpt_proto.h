/* =============================================================================
 * drivers/lpt/lpt_proto.h - the messages of lpt.drv, the parallel port driver
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * One service per port: "lpt0", "lpt1", "lpt2" (the order of the ports found:
 * 0x378, 0x278, 0x3BC). Every request gets ONE answer, of type
 * LPT_RISPOSTA(request), from the service to the sender.
 *
 * ONE CLIENT AT A TIME: a printer job, the ZIP drive, a scanner, the
 * datasette and a prototype board all use the same wires in different ways,
 * so who wants to change pins or edges first asks LPT_MSG_PRENDI. INFO and
 * PIN_LEGGI are for everybody. The port is free again with LPT_MSG_LASCIA or
 * when the holder's process is gone (checked at every request of another).
 *
 * Who uses this header: drivers/lpt/lpt.c (service and command line), and
 * the programs that drive a device through the port (voce 96, the
 * datasette). Copy nothing of it into another header: include it.
 * ============================================================================= */
#ifndef LPT_PROTO_H
#define LPT_PROTO_H

#define LPT_SERVIZIO        "lpt"           /* + the port's number: lpt0 */

/* The registers, from the port's base. */
#define LPT_DATI            0
#define LPT_STATO           1
#define LPT_CONTROLLO       2
#define LPT_ECR             0x402           /* ECP chips only */

/* Status register (read only). Bit 7 is the Busy pin INVERTED. */
#define LPT_ST_TIMEOUT      0x01            /* EPP timeout */
#define LPT_ST_NERROR       0x08            /* pin 15, nFault */
#define LPT_ST_SELECT       0x10            /* pin 13 */
#define LPT_ST_PE           0x20            /* pin 12, paper end */
#define LPT_ST_NACK         0x40            /* pin 10, the interrupt's pin */
#define LPT_ST_NBUSY        0x80            /* pin 11, inverted */

/* Control register. Bits 0, 1 and 3 are their pins INVERTED. */
#define LPT_CT_STROBE       0x01            /* pin 1, nStrobe */
#define LPT_CT_AUTOFD       0x02            /* pin 14, nAutoFd */
#define LPT_CT_NINIT        0x04            /* pin 16, nInit (not inverted) */
#define LPT_CT_SELECTIN     0x08            /* pin 17, nSelectIn */
#define LPT_CT_IRQ          0x10            /* interrupt on the nACK edge */
#define LPT_CT_INGRESSO     0x20            /* data lines as inputs (PS/2) */
#define LPT_CT_RIPOSO       (LPT_CT_NINIT | LPT_CT_SELECTIN)   /* 0x0C */

/* What the port can do (LptInfo.modi). */
#define LPT_MODO_SPP        0x01
#define LPT_MODO_PS2        0x02            /* bidirectional data lines */
#define LPT_MODO_EPP        0x04            /* see "EPP" in lpt.c */
#define LPT_MODO_ECP        0x08

/* The requests. */
#define LPT_MSG_INFO          0x4C50u       /* -> LptInfo */
#define LPT_MSG_PRENDI        0x4C51u       /* LptPrendi -> LptEsito */
#define LPT_MSG_LASCIA        0x4C52u       /* -> LptEsito */
#define LPT_MSG_SCRIVI        0x4C53u       /* bytes -> LptScritto (compat. mode) */
#define LPT_MSG_PIN_LEGGI     0x4C54u       /* -> LptPin */
#define LPT_MSG_PIN_SCRIVI    0x4C55u       /* LptPinScrivi -> LptPin */
#define LPT_MSG_ID            0x4C56u       /* -> LptId (IEEE 1284 Device ID) */
#define LPT_MSG_FRONTI_ACCENDI 0x4C57u      /* LptFrontiAccendi -> LptEsito */
#define LPT_MSG_FRONTI_SPEGNI 0x4C58u       /* -> LptEsito */
#define LPT_MSG_FRONTI_LEGGI  0x4C59u       /* LptFrontiChiedi -> LptFronti */
#define LPT_MSG_ONDA_AGGIUNGI 0x4C5Au       /* LptOndaPezzo -> LptEsito */
#define LPT_MSG_ONDA_SUONA    0x4C5Bu       /* LptOndaSuona -> LptOndaFatta */
#define LPT_MSG_ONDA_FERMA    0x4C5Cu       /* during SUONA: stop (holder) */
#define LPT_MSG_FINE          0x4C5Du       /* the service ends (root) */
#define LPT_RISPOSTA(t)       ((t) | 0x10000u)

/* What the holder will do: shown by INFO, nothing else depends on it. */
#define LPT_USO_PIN         1               /* raw pins, prototype boards */
#define LPT_USO_STAMPA      2
#define LPT_USO_NASTRO      3               /* datasette */
#define LPT_USO_ZIP         4
#define LPT_USO_SCANNER     5

typedef struct {
    unsigned int  base;             /* 0x378 ... */
    unsigned int  irq;              /* the line of nACK (0 = none) */
    unsigned int  modi;             /* LPT_MODO_* */
    int           chi;              /* holder's pid, 0 = free */
    unsigned int  uso;              /* holder's LPT_USO_* */
    unsigned int  fronti_attivi;    /* 1 = edges are being kept */
    unsigned int  hz;               /* the clock of the edges and waves */
    unsigned char dati, stato, controllo, ecr;
    unsigned int  scritti;          /* bytes printed since the start */
} LptInfo;

typedef struct { unsigned int uso; } LptPrendi;
typedef struct { int rc; } LptEsito;          /* 0, or -errno */

typedef struct {
    int           rc;               /* 0, -ETIMEDOUT (busy too long), -EIO */
    unsigned int  scritti;
    unsigned char stato;            /* the status register at the end */
} LptScritto;

typedef struct { unsigned char dati, stato, controllo, ecr; int rc; } LptPin;

typedef struct {
    unsigned char registro;         /* LPT_DATI or LPT_CONTROLLO */
    unsigned char valore;
    unsigned char maschera;         /* only these bits change */
} LptPinScrivi;

#define LPT_ID_MAX 1024
typedef struct {
    int           rc;               /* 0, -ETIMEDOUT (no IEEE 1284 answer) */
    unsigned int  len;
    char          id[LPT_ID_MAX];   /* "MFG:...;MDL:...;CMD:...;" */
} LptId;

typedef struct {
    unsigned int  quanti;           /* instants the kernel keeps between two
                                     * reads (0 = 16384) */
} LptFrontiAccendi;

typedef struct { unsigned int max; } LptFrontiChiedi;

#define LPT_FRONTI_PEZZO 180
typedef struct {
    int                rc;
    unsigned int       hz;          /* ticks of t[] per second */
    unsigned int       n;           /* instants in t[] (oldest first) */
    unsigned int       persi;       /* edges lost so far (kernel + driver) */
    unsigned int       in_attesa;   /* still kept after these */
    unsigned char      stato;       /* status register now */
    unsigned char      pad[3];
    unsigned int       spuri;       /* of all edges, those the kernel took
                                     * from a "spurious" IRQ 7 (see isr.c) */
    unsigned int       pad2;
    unsigned long long t[LPT_FRONTI_PEZZO];
} LptFronti;
/* What comes before t[]: an answer of n instants is this + n * 8 bytes. */
#define LPT_FRONTI_TESTA ((unsigned int)(sizeof(LptFronti) - LPT_FRONTI_PEZZO * 8u))

/* A wave on data lines: durations of the half-waves in nanoseconds. Each
 * piece is added to what the service keeps; SUONA plays it all. */
#define LPT_ONDA_PEZZO 360
typedef struct {
    unsigned int n;
    unsigned int ns[LPT_ONDA_PEZZO];
} LptOndaPezzo;

typedef struct {
    unsigned char maschera;         /* the data bits that change at each
                                     * half-wave (datasette WRITE: 0x02) */
    unsigned char inizio;           /* data register before the first */
    unsigned char pad[2];
} LptOndaSuona;

typedef struct {
    int                rc;          /* 0, -EINTR (FERMA), -ENODATA */
    unsigned int       suonate;     /* half-waves played */
    unsigned int       ritardo_max_ns;  /* the latest edge, against its instant */
    unsigned int       ritardi_10us;    /* edges later than 10 microseconds */
    unsigned int       processore;  /* the processor reserved (0 = none) */
} LptOndaFatta;

#endif

/* =============================================================================
 * lib/include/extaskbar_abi.h — what the kernel, /bin/extaskbar and the
 * services share (@EXTASKBAR, 16 October 2026)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Plain numbers and structures: the kernel includes it as well as the
 * programs. Two channels:
 *
 *  1. SYS_EXTASKBAR (the kernel): what processes IN THE BACKGROUND write on
 *     their console or terminal - started with '&', or children of one -
 *     goes into a ring in the kernel instead of the screen, as long as a
 *     collector (extaskbar) is registered and alive. Without it, or when it
 *     dies, everything goes back to the screen as before. The same call
 *     draws the status bar on the last line of the screen.
 *
 *  2. IPC to the service "extaskbar": the items of the bar (EXTB_MSG_VOCE),
 *     sent by the services themselves - see lib/include/extaskbar.h.
 * ============================================================================= */
#ifndef EXTASKBAR_ABI_H
#define EXTASKBAR_ABI_H

#define SYS_EXTASKBAR       26      /* ebx = op, ecx, edx: below */

#define EXTB_REGISTRA       0       /* the caller becomes the collector (root) */
#define EXTB_LEGGI          1       /* ecx = buffer, edx = size: whole records, non-blocking */
#define EXTB_LASCIA         2       /* no collector any more: back to the screen */
#define EXTB_BARRA          3       /* ecx = cells (char | attr << 8), edx = n; 0 = hide (root) */
#define EXTB_SOTTOFONDO     4       /* ecx = 1/0: the caller is (not) a background service */

/* One record in the kernel's ring, followed by `len` bytes of text */
typedef struct {
    unsigned int   pid;
    unsigned short len;
    unsigned short persi;           /* records dropped before this one (ring full) */
    char           nome[20];        /* the program: "ar5k", "automount"... */
} ExtbRecord;

#define EXTB_ANELLO_BYTE    65536   /* the kernel's ring */
#define EXTB_TESTO_MAX      1024    /* one record holds at most this much text */

/* --- IPC with the service ---------------------------------------------------- */
#define EXTB_SERVIZIO       "extaskbar"

#define EXTB_MSG_VOCE       40      /* payload = ExtbVoce; no answer */
#define EXTB_MSG_COMANDO    41      /* payload = ExtbComando -> EXTB_MSG_ESITO */
#define EXTB_MSG_ELENCO     42      /* -> one EXTB_MSG_SERVIZIO each, then EXTB_MSG_FINE */
#define EXTB_MSG_LOG        43      /* payload = ExtbChiediLog -> EXTB_MSG_TESTO... EXTB_MSG_FINE */
#define EXTB_MSG_ESITO      160
#define EXTB_MSG_SERVIZIO   161     /* payload = ExtbServizio */
#define EXTB_MSG_TESTO      162     /* payload = text, not terminated */
#define EXTB_MSG_FINE       163

#define EXTB_VOCE_NOME      12
#define EXTB_VOCE_TESTO     40

/* An item of the bar: its name ("WIFI"), the width it wants, its text. */
typedef struct {
    char           nome[EXTB_VOCE_NOME];
    unsigned int   larghezza;       /* columns, the separating space included */
    char           testo[EXTB_VOCE_TESTO];
    unsigned int   attenzione;      /* 1: shown in another colour (an error, no link) */
} ExtbVoce;

#define EXTB_CMD_MOSTRA     1
#define EXTB_CMD_NASCONDI   2
#define EXTB_CMD_FERMA      3
#define EXTB_CMD_RILEGGI    4       /* percorso = the configuration to read again */
#define EXTB_CMD_SALVA      5       /* nome = service, valore = 1/0: its log to /boot/<nome>/log.txt */
#define EXTB_CMD_IMPOSTA    6       /* percorso = "USB,WIFI,...", valore = KB per log: applied at
                                     * once and written to the service's configuration file */

typedef struct {
    unsigned int   cmd;
    unsigned int   valore;
    char           nome[20];
    char           percorso[128];
} ExtbComando;

typedef struct {
    char           nome[20];
    unsigned int   byte;            /* in memory now */
    unsigned int   righe;
    unsigned int   su_file;         /* saved to /boot/<nome>/log.txt */
    unsigned int   pid;             /* the last one seen */
} ExtbServizio;

typedef struct {
    char           nome[20];
    unsigned int   dalla_fine;      /* bytes to give, counted back from the end */
} ExtbChiediLog;

#endif

/* =============================================================================
 * exwin/bin/exbrowser/browser_taffy.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * The C side of libtaffy_exos.so.1 (tools/taffy-exos, P7 of
 * debug/implementazione_BROWSER002.txt). ! TxStile IS THE #[repr(C)] STRUCT
 * OF tools/taffy-exos/taffy_exos/src/lib.rs, field for field: change both,
 * or neither (tx_misura_stile() checks the size at the first use).
 * ============================================================================= */
#ifndef BROWSER_TAFFY_H
#define BROWSER_TAFFY_H

/* a length: tipo 0 auto, 1 px, 2 percent (v from 0 to 1) */
typedef struct { unsigned char tipo; float v; } TxDim;

/* a grid track, minmax(min, max): 0 auto, 1 px, 2 percent (0..1), 3 fr (max
 * only), 4 min-content, 5 max-content, 6 fit-content(px) (max only) */
typedef struct { unsigned char min_tipo, max_tipo; float min_v, max_v; } TxTraccia;

/* a named area: its grid lines, from 1, start and end */
typedef struct { const char *nome; unsigned short r0, r1, c0, c1; } TxArea;

typedef struct {
    const TxTraccia *col, *rig, *auto_col, *auto_rig;
    const TxArea    *aree;
    const char      *nome_col_inizio, *nome_col_fine;     /* a line's name, or 0 */
    const char      *nome_rig_inizio, *nome_rig_fine;
    float  grow, shrink;
    TxDim  basis, w, h, min_w, min_h, max_w, max_h;
    TxDim  margine[4], inset[4];          /* top right bottom left */
    float  padding[4], bordo[4];
    float  gap_col, gap_rig;
    short  col_inizio, col_fine, rig_inizio, rig_fine;   /* 0 auto, a line, 10000+n span n */
    unsigned char display;        /* 0 none, 1 block, 2 flex, 3 grid */
    unsigned char dir;            /* 0 row, 1 column, 2 row-reverse, 3 column-reverse */
    unsigned char wrap;           /* 0 nowrap, 1 wrap, 2 wrap-reverse */
    unsigned char justify_content;/* 0 normal, 1 start, 2 end, 3 center, 4 between, 5 around, 6 evenly, 7 stretch */
    unsigned char align_items;    /* 0 normal, 1 start, 2 end, 3 center, 4 baseline, 5 stretch */
    unsigned char align_self;     /* 0 auto, else as align_items */
    unsigned char align_content;  /* as justify_content */
    unsigned char justify_items, justify_self;
    unsigned char posizione;      /* 0 in the flow, 1 absolute */
    unsigned char flusso;         /* grid-auto-flow: 0 row, 1 column, 2 row dense, 3 column dense */
    unsigned char n_col, n_rig, n_auto_col, n_auto_rig;
    unsigned char col_rip, col_rip_da, col_rip_n;     /* 0, 1 auto-fill, 2 auto-fit */
    unsigned char rig_rip, rig_rip_da, rig_rip_n;
    unsigned char nascosto;       /* overflow other than visible */
    unsigned char n_aree;
    unsigned short aree_righe, aree_colonne;
} TxStile;

#define TX_SPAN(n) ((short)(10000 + (n)))

/* "how big are you": avail >= 0 definite, -1 min-content, -2 max-content;
 * noto NaN = not known. */
typedef void (*TxMisura)(void *utente, unsigned int ctx, float noto_w, float noto_h,
                         float avail_w, float avail_h, float *w, float *h);

#endif

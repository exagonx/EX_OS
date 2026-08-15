/* =============================================================================
 * src/rtlib/exos/fb_private_console.h  (da tools/freebasic-exos/exos/)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later   (parte della runtime di FB)
 * =============================================================================
 *
 * Lo stato della console per il bersaglio EX-OS.
 *
 * ! E' MOLTO PIU' PICCOLO DI QUELLO DI unix/, e la ragione e' che qui non
 * c'e' niente da indovinare: la console di EX-OS e' una VGA in modo testo
 * 80x25, sempre, e non esiste il ridimensionamento di una finestra di
 * terminale. unix/ tiene un buffer di caratteri e attributi e lo
 * riconcilia a ogni stampa perche' deve rincorrere un terminale che puo'
 * cambiare misura sotto i piedi; qui la misura e' un fatto.
 *
 * ! LA POSIZIONE DEL CURSORE LA CONTIAMO NOI, contando i caratteri
 * stampati, invece di chiederla al sistema: EX-OS non ha una syscall che
 * la restituisca (console_info() da' quante console ci sono e quale e'
 * visibile, non dove sta il cursore). E' la stessa strada di unix/, che
 * pure conta, e ha lo stesso limite: chi scrive su stdout senza passare
 * da qui — printf() diretta, per dire — fa perdere il conto. Vale per
 * LOCATE e POS, non per la stampa, che resta giusta comunque.
 * ============================================================================= */

typedef struct _FB_CONSOLE_CTX {
	int inited;
	int w, h;                   /* 80x25, fissi */
	int cur_x, cur_y;           /* 1-based, come in tutta la runtime */
	int fg_color, bg_color;
} FB_CONSOLE_CTX;

extern FB_CONSOLE_CTX __fb_con;

/* =============================================================================
 * kernel/include/vga.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef VGA_H
#define VGA_H

#include "kernel.h"

/* Costanti colore VGA (4 bit) */
typedef enum {
    VGA_COLOR_BLACK         = 0,
    VGA_COLOR_BLUE          = 1,
    VGA_COLOR_GREEN         = 2,
    VGA_COLOR_CYAN          = 3,
    VGA_COLOR_RED           = 4,
    VGA_COLOR_MAGENTA       = 5,
    VGA_COLOR_BROWN         = 6,
    VGA_COLOR_LIGHT_GREY    = 7,
    VGA_COLOR_DARK_GREY     = 8,
    VGA_COLOR_LIGHT_BLUE    = 9,
    VGA_COLOR_LIGHT_GREEN   = 10,
    VGA_COLOR_LIGHT_CYAN    = 11,
    VGA_COLOR_LIGHT_RED     = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_YELLOW        = 14,
    VGA_COLOR_WHITE         = 15,
} VGAColor;

/* =============================================================================
 * Console virtuali
 *
 * Quante ne esistono. Ognuna costa 4000 byte di BSS del kernel per il
 * proprio buffer di schermo, più un processo shell che ci gira sopra:
 * quattro erano il compromesso classico (Alt+F1..F4) fra averne
 * abbastanza e non sprecare RAM su una macchina che ne ha 32 MB.
 *
 * ! CINQUE DA QUANDO LA GRAFICA HA LA SUA. Il server a finestre si prendeva
 * una delle quattro — la seconda — e chi lavorava in testo si ritrovava con
 * tre console invece di quattro, senza averlo chiesto. Alt+F1..F4 restano di
 * chi scrive; la grafica sta sulla quinta, Alt+F5, e non toglie niente a
 * nessuno. Costa 4000 byte e una shell in piu'.
 *
 * ! E DEVE COMBACIARE CON KBD_N_CONSOLE, in drivers/kbd/kbd_proto.h: e' il
 * driver a tradurre Alt+Fn in un cambio di console, e se i due numeri
 * divergono l'ultima console esiste ma non ci si arriva — oppure si chiede al
 * kernel di mostrarne una che non c'e'.
 *
 * La console 0 è quella di SISTEMA: ci finiscono i messaggi del kernel
 * (klog/kprintf) e tutto ciò che viene scritto prima che esista un
 * processo. Vedi kernel/arch/x86/vga.c.
 * ============================================================================= */
#define VGA_N_CONSOLE   5

/* Funzioni aggiuntive vga.c */
uint8_t vga_get_row(void);
uint8_t vga_get_col(void);

/* Scrittura su una console specifica. Chi scrive passa il numero: non
 * esiste una "console corrente" globale, perché il kernel è preemptabile
 * e il timer potrebbe cambiarla in mezzo a una scrittura. */
void     vga_putchar_su(uint32_t n, char c);
void     vga_clear_su(uint32_t n);
void     vga_setcolor_su(uint32_t n, uint8_t fg, uint8_t bg);

/* Passa la console al framebuffer VESA, se Stage 2 ne ha impostato uno.
 * Senza framebuffer non fa niente e la console resta quella di testo.
 *
 * ! VA CHIAMATA DOPO paging_init(): il framebuffer sta fuori dalla RAM e
 * questa funzione lo mappa. Vedi il commento esteso in vga.c. */
void     vga_init_grafica(const BootInfo *info);

/* Commutazione. Ritorna 0, o -1 se il numero non esiste. */
/* =============================================================================
 * Rimettere il modo testo senza il BIOS — la rete di sicurezza della grafica
 *
 * vga_hw_modo3()        parla solo alla scheda (kernel/arch/x86/vga_modo3.c)
 * vga_ripristina_testo() rimette anche la console e ridisegna (vga.c)
 *
 * Chi vuole «rimettere lo schermo» chiama la seconda. Il perche' per esteso,
 * e il limite dichiarato su hardware reale con una VESA attiva, stanno in
 * testa a vga_modo3.c.
 * ============================================================================= */
void     vga_hw_modo3(void);
void     vga_ripristina_testo(void);

int      vga_switch_console(uint32_t n);
uint32_t vga_visible_console(void);

/* Dice dov'e' il framebuffer e che forma ha. Tutti zero = modo testo.
 * Serve a SYS_VIDEO_INFO, cioe' al server grafico in ring 3. */
void vga_info_fb(uint32_t *addr, uint32_t *pitch, uint32_t *w, uint32_t *h,
                 uint32_t *bpp);

#endif /* VGA_H */

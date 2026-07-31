/* =============================================================================
 * drivers/tty/tty.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef TTY_H
#define TTY_H

#include "kernel.h"

/* ioctl comandi TTY */
#define TTY_IOCTL_GETSIZE    0x01
#define TTY_IOCTL_SETRAW     0x02
#define TTY_IOCTL_SETCOOKED  0x03
#define TTY_IOCTL_CLEAR      0x04
#define TTY_IOCTL_SETCOLOR   0x05

/* Struttura dimensioni finestra */
typedef struct {
    uint16_t rows;
    uint16_t cols;
    uint16_t xpixel;
    uint16_t ypixel;
} TtyWinSize;

/* =============================================================================
 * Sorgente dell'input
 *
 * Da luglio 2026 la tastiera può essere servita in due modi diversi, e
 * il TTY deve sapere quale dei due è attivo:
 *
 *   TTY_INPUT_IPC       — /dev/kbd.drv gira come processo ring3, possiede
 *                          IRQ1 e le porte del KBC, e consegna righe
 *                          complete via IPC. È la configurazione normale.
 *   TTY_INPUT_INTERNAL  — nessun driver ring3 disponibile: il TTY
 *                          registra il proprio handler IRQ1 in-kernel e
 *                          fa tutto da sé (il comportamento storico).
 *                          Fallback, così un kbd.drv mancante o crashato
 *                          non lascia il sistema senza console.
 *   TTY_INPUT_NONE      — stato iniziale, fra drv_init() e la scelta
 *                          fatta da kernel_main al PASSO 14b.
 *
 * La scelta è fatta una volta da kernel_main, ma tty_set_input_source()
 * può essere richiamata a runtime: drv_read() la usa per degradare a
 * INTERNAL se il servizio kbd sparisce.
 * ============================================================================= */
#define TTY_INPUT_NONE      0
#define TTY_INPUT_INTERNAL  1
#define TTY_INPUT_IPC       2

void tty_set_input_source(int src);

/* Interfaccia driver standard */
int  drv_init(void);
int  drv_read(void *buf, size_t n);
int  drv_write(const void *buf, size_t n);
int  drv_ioctl(int cmd, void *arg);
void drv_exit(void);

#endif /* TTY_H */

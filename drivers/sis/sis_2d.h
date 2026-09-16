/* =============================================================================
 * drivers/sis/sis_2d.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il motore 2D della SiS 6330 — il contratto, per chi lo chiama da sis.c
 * ============================================================================= */
#ifndef SIS_2D_H
#define SIS_2D_H

/* Apre il motore: trova la scheda sul PCI, mappa i registri, legge la forma
 * dello schermo. Rende 0 se e' pronto. Va chiamata prima di tutto il resto. */
int  sis2d_apri(int verboso);

/* Quando la finestra dei registri non risponde: separa le tre spiegazioni
 * possibili invece di tentare bit a caso. */
void sis2d_diagnosi(void);

/* Racconta cosa c'e' nei registri, senza scrivere niente. */
void sis2d_stato(void);

/* Un rettangolo pieno. Rende 0 se il motore ha finito in tempo. */
int  sis2d_riempi(unsigned int x, unsigned int y,
                  unsigned int w, unsigned int h, unsigned int colore);

/* Una copia schermo -> schermo. Rende 0 se il motore ha finito in tempo. */
int  sis2d_copia(unsigned int sx, unsigned int sy,
                 unsigned int dx, unsigned int dy,
                 unsigned int w, unsigned int h);

/* Le prove: disegna, RILEGGE dal framebuffer e dice se e' venuto giusto. */
int  sis2d_prova(void);

/* Il motore contro la CPU, sugli stessi pixel. */
int  sis2d_misura(void);

/* Accende UN bit di accensione e guarda se la finestra dei registri parla.
 * `quale` e' 0x1E (bit 6, SIS_ENABLE_2D) oppure 0x20 (bit 0,
 * SIS_MEM_MAP_IO_ENABLE). Rimette il registro com'era prima di tornare.
 *
 * ! UNO PER VOLTA, E NON PER PRUDENZA GENERICA: accesi insieme, il 16
 * settembre 2026 hanno bloccato l'Acer, e un blocco non dice quale dei due e'
 * stato. Vedi il commento in testa a sis2d_accendi(). */
int  sis2d_accendi(unsigned int quale);

#endif /* SIS_2D_H */

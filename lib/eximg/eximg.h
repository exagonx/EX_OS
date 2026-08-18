/* =============================================================================
 * lib/eximg/eximg.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ExImg — i formati d'immagine che costano
 *
 * ! E' UNA LIBRERIA A PARTE, E NON DENTRO exwin.so, per la stessa ragione per
 * cui exdlg e' separata: una barra delle applicazioni, un orologio, un
 * pannello non decodificano un PNG mai. Il BMP resta dentro il toolkit perche'
 * e' quaranta righe e non ha bisogno di niente; un decodificatore vero — un
 * DEFLATE completo piu' i filtri — sono migliaia di righe che pagherebbero
 * tutti.
 *
 * ! E NON SI COLLEGA: SI APRE QUANDO SERVE. Chi disegna un'immagine chiama
 * ex_immagine() del toolkit, che prova prima i formati che sa da se' e apre
 * questa libreria solo se non riconosce niente. Un programma che non apre mai
 * un PNG non la carica mai, e non deve saperlo. Vedi exlib_apri_fra().
 *
 * ! I PIXEL SONO ARGB A 32 BIT, come dappertutto nel toolkit. Chi decodifica
 * un formato produce quello e non deve sapere com'e' fatto lo schermo: la
 * conversione la fa il server, in un posto solo.
 * ============================================================================= */
#ifndef EXIMG_H
#define EXIMG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned int  larghezza;
    unsigned int  altezza;
    unsigned int *px;       /* larghezza * altezza pixel ARGB */
} EximgBitmap;

/* Riconosce il formato DAI PRIMI BYTE e decodifica. Rende 1 se ha capito il
 * formato e il bitmap e' pronto, 0 altrimenti — e su 0 il bitmap resta
 * azzerato, quindi non c'e' niente da liberare.
 *
 * ! CHI RENDE 1 HA ALLOCATO, e la memoria e' di chi chiama: va restituita con
 * eximg_libera(). Un decodificatore che allocasse e lasciasse liberare al
 * chiamante con free() legherebbe l'applicazione al modo in cui la libreria
 * alloca oggi. */
int  eximg_carica(const unsigned char *dati, unsigned int n, EximgBitmap *bm);

/* Restituisce il bitmap e lo azzera. Su un bitmap gia' azzerato non fa
 * niente: chiamarla due volte e' innocuo. */
void eximg_libera(EximgBitmap *bm);

#ifdef __cplusplus
}
#endif

#endif /* EXIMG_H */

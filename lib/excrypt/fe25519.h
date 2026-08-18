/* =============================================================================
 * lib/excrypt/fe25519.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * =============================================================================
 *
 * Aritmetica modulo 2^255 - 19: la usano X25519 ed Ed25519. Il perche' sta in
 * fe25519.c. Non e' un'interfaccia pubblica di ExCrypt — chi usa la libreria
 * chiama x25519() ed ed25519_*(), non queste.
 * ============================================================================= */
#ifndef FE25519_H
#define FE25519_H

typedef long long i64;
typedef i64 fe[16];

extern const fe FE_121665;

void fe_zero(fe a);
void fe_uno(fe a);
void fe_copia(fe a, const fe b);
void fe_somma(fe o, const fe a, const fe b);
void fe_sottrai(fe o, const fe a, const fe b);
void fe_riporta(fe o);
void fe_moltiplica(fe o, const fe a, const fe b);
void fe_quadrato(fe o, const fe a);
void fe_scambia(fe p, fe q, i64 b);
void fe_inverti(fe o, const fe i);
void fe_in_byte(unsigned char *fuori, const fe n);
void fe_da_byte(fe o, const unsigned char *n);

#endif /* FE25519_H */

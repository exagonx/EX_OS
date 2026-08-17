/* =============================================================================
 * lib/eximg/inflate.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef EXIMG_INFLATE_H
#define EXIMG_INFLATE_H

/* Decomprime un flusso DEFLATE (RFC 1951) — NON zlib: il chiamante salta da
 * se' i due byte di intestazione zlib e i quattro di Adler-32, che in un PNG
 * stanno intorno ai dati.
 *
 * ! LA MEMORIA E' DEL CHIAMANTE: qui non si alloca niente. Su EX-OS free() non
 * restituisce nulla, quindi una funzione che alloca dentro un ciclo e' una
 * perdita permanente.
 *
 * Rende 0 e riempie *prodotti, oppure -1 — e -1 vuol dire SEMPRE «non mi fido
 * di questi dati», mai «ho prodotto qualcosa a meta'». */
int inflate(const unsigned char *dati, unsigned int n,
            unsigned char *out, unsigned int max, unsigned int *prodotti);

#endif /* EXIMG_INFLATE_H */

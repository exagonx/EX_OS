/* =============================================================================
 * kernel/include/sha256.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Il perche' di una copia nel kernel sta in kernel/crypto/sha256.c.
 * ============================================================================= */
#ifndef KERNEL_SHA256_H
#define KERNEL_SHA256_H

/* ! NIENTE <stdint.h> QUI. Il kernel si compila con `-I lib/include` in
 * coda, quindi quel nome risolve alla stdint della libc, che tira dentro
 * libc.h e con essa un'altra definizione di mezze strutture dell'ABI: si
 * ottengono venti «conflicting types» che non parlano di niente. I tipi li
 * porta kernel.h, come per ogni altro header di qui. */
void sha256(const void *dati, uint32_t len, uint8_t out[32]);

/* L'impronta in esadecimale minuscolo: 64 caratteri piu' il terminatore.
 * E' la forma in cui sta scritta dentro /boot/ombra. */
void sha256_esa(const void *dati, uint32_t len, char out[65]);

#endif /* KERNEL_SHA256_H */

/* =============================================================================
 * kernel/include/iso9660.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ISO 9660 — il filesystem dei CD e dei DVD dati. SOLA LETTURA.
 *
 * SOLA LETTURA E' UNA PROPRIETA' DEL FORMATO, NON UN LIMITE DEL DRIVER.
 * ISO 9660 non ha bitmap di spazio libero, non ha una lista di blocchi
 * liberi e non ha voci di directory di dimensione variabile riutilizzabili:
 * un volume viene calcolato tutto insieme e scritto in un colpo solo. Non
 * esiste "aggiungere un file" — esiste rifare l'immagine. E' il motivo per
 * cui questo header non ha un iso_create() da implementare un giorno.
 *
 * COS'E' UN FILESYSTEM ISO, in tre righe: una catena di descrittori di
 * volume che comincia al blocco 16, uno dei quali (il PVD) contiene il
 * record della directory radice; una directory e' un file come gli altri,
 * fatto di record a lunghezza variabile; un file e' un intervallo CONTIGUO
 * di blocchi, e questa e' la cosa che lo rende semplice — non c'e' una
 * catena da seguire, c'e' un primo blocco e una lunghezza.
 *
 * I DUE ALFABETI. I nomi "veri" di ISO 9660 sono maiuscoli, corti e con un
 * numero di versione appiccicato (`LEGGIMI.TXT;1`). Praticamente ogni
 * disco masterizzato dopo il 1995 porta anche una seconda struttura,
 * JOLIET, con gli stessi file ma nomi lunghi in UCS-2. Questo driver la
 * usa quando c'e': senza, i nomi che l'utente vede non sono quelli che ha
 * masterizzato. Vedi kernel/fs/iso9660.c per come viene scelta.
 *
 * ROCK RIDGE NON E' GESTITO — e' l'estensione Unix (permessi, link
 * simbolici, nomi lunghi in stile POSIX) che si annida dentro i campi di
 * sistema dei record. Un disco Rock Ridge resta leggibile: si vedono i
 * nomi ISO o Joliet, che ci sono sempre. Ignorare un'estensione qui e'
 * sicuro proprio perche' non cambia il significato dei campi che gia' si
 * leggono — al contrario di una funzionalita' incompatibile di ext2.
 * ============================================================================= */

#ifndef ISO9660_H
#define ISO9660_H

#include "kernel.h"

#define ISO_MAX_MOUNT   2

/* Joliet consente 64 caratteri; ISO 9660 livello 2 arriva a 31 col numero
 * di versione. 128 li copre entrambi con margine, e resta sotto i 256 di
 * VfsDirEntry. */
#define ISO_NOME_MAX    128
#define ISO_PERCORSO_MAX 320

typedef struct {
    char     nome[ISO_NOME_MAX];
    uint32_t dimensione;
    uint32_t extent;        /* primo blocco dei dati: un file e' contiguo */
    uint8_t  is_dir;
} IsoDirEntry;

/* Monta il volume sul dispositivo a blocchi `blkdev` (tipicamente un
 * lettore ottico, ma va bene qualunque dispositivo che contenga
 * un'immagine ISO). Ritorna un handle >= 0, o <0. */
int  iso_mount(int blkdev);
int  iso_umount(int mnt);

/* Diagnostica del volume montato. */
const char *iso_etichetta(int mnt);
uint32_t    iso_blocchi(int mnt);
int         iso_joliet(int mnt);

/* `percorso` e' interno al volume ("/" oppure "/doc/leggimi.txt"). Il
 * confronto dei nomi e' INSENSIBILE alle maiuscole: su un volume ISO i
 * nomi sono maiuscoli per obbligo di formato, e pretenderle dall'utente
 * significherebbe rendere indigitabile cio' che `ls` mostra in minuscolo. */
int  iso_stat   (int mnt, const char *percorso, IsoDirEntry *out);
int  iso_readdir(int mnt, const char *percorso, IsoDirEntry *out,
                 uint32_t max, uint32_t start);

/* Legge `size` byte dal file a partire da `offset`.
 * Ritorna i byte letti (0 = fine file), <0 su errore. */
int  iso_read   (int mnt, const char *percorso, void *buf,
                 uint32_t size, uint32_t offset);

#endif /* ISO9660_H */

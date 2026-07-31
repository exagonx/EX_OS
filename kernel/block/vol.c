/* =============================================================================
 * kernel/block/vol.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Riconoscimento del filesystem dentro una partizione, leggendone il BPB.
 *
 * PERCHE' QUESTO MODULO ESISTE DA SOLO, prima del driver FAT vero.
 *
 * La determinazione del tipo di FAT e' il punto in cui quasi tutte le
 * implementazioni sbagliano, e sbagliarlo non da' un errore: da' un
 * filesystem letto con le regole di un altro, cioe' dati corrotti in
 * silenzio. Vale la pena isolarlo, provarlo e vederne il risultato PRIMA
 * di costruirci sopra un driver che monta e scrive.
 *
 * LA REGOLA, che e' l'unica corretta (specifica Microsoft FAT):
 *
 *     il tipo si decide dal NUMERO DI CLUSTER DELL'AREA DATI
 *
 *       < 4085   -> FAT12
 *       < 65525  -> FAT16
 *       altrimenti  FAT32
 *
 * NON dal byte di tipo nella tabella delle partizioni (0x06, 0x0B, 0x0C...),
 * che e' solo un suggerimento e puo' essere sbagliato o obsoleto. NON
 * dalla stringa "FAT16   " nel settore di avvio, che e' testo decorativo
 * e la specifica dice esplicitamente di ignorare. Sono due scorciatoie
 * diffuse ed entrambe portano a leggere una FAT con la larghezza di voce
 * sbagliata.
 *
 * I due confini hanno quei valori esatti e non vanno "arrotondati": sono
 * scelti perche' 4085 = 0xFF5 e 65525 = 0xFFF5, cioe' il primo valore di
 * cluster che collide con i codici riservati di ciascun formato.
 *
 * OGNI DIVISIONE QUI E' PROTETTA. I campi arrivano dal disco e possono
 * essere zero o assurdi: una partizione formattata male, o semplicemente
 * un settore di dati scambiato per un settore di avvio, produrrebbe una
 * divisione per zero — cioe' un'eccezione in ring0, cioe' un kernel panic
 * causato da un disco esterno. Non e' accettabile per un sistema che deve
 * poter guardare dischi sconosciuti.
 * ============================================================================= */

#include "kernel.h"
#include "vol.h"
#include "blk.h"

/* Letture non allineate: il BPB e' una struttura impacchettata e i suoi
 * campi non cadono su confini naturali dentro il settore. */
static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Una dimensione di settore valida e' 512, 1024, 2048 o 4096. */
static int settore_valido(uint16_t b)
{
    return b == 512 || b == 1024 || b == 2048 || b == 4096;
}

/* I settori per cluster devono essere una potenza di due fra 1 e 128. */
static int spc_valido(uint8_t s)
{
    return s != 0 && (s & (s - 1)) == 0 && s <= 128;
}

/* Copia un'etichetta di volume da 11 byte togliendo gli spazi finali. */
static void etichetta(const uint8_t *src, char *out)
{
    int i, n = 11;

    for (i = 0; i < n; i++) out[i] = (char)src[i];
    out[n] = '\0';

    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\0')) out[--n] = '\0';

    /* Un'etichetta puo' contenere byte non stampabili se il settore non e'
     * davvero un settore di avvio: si ripulisce invece di sporcare lo
     * schermo con glifi casuali. */
    for (i = 0; out[i]; i++) {
        if ((unsigned char)out[i] < 32 || (unsigned char)out[i] > 126) out[i] = '?';
    }
}

int vol_identifica(int blkdev, VolumeInfo *out)
{
    uint8_t  s[512];
    uint16_t byts_per_sec, resvd, root_ent, tot16, fatsz16;
    uint32_t tot32, fatsz32, tot, fatsz;
    uint8_t  spc, n_fat;
    uint32_t root_dir_sec, dati, cluster;
    uint64_t n_settori;

    {   /* azzeramento esplicito: il chiamante deve poter leggere il
         * risultato anche quando si esce in errore a meta' */
        uint8_t *z = (uint8_t *)out;
        uint32_t k = sizeof(*out);
        while (k--) *z++ = 0;
    }
    out->tipo = VOL_FS_SCONOSCIUTO;

    {
        const BlkDev *b = blk_get(blkdev);
        if (b == NULL || !b->usato || b->settori == 0) return -1;
        n_settori = b->settori;
    }

    /* Settore 0 RELATIVO al dispositivo: e' il livello a blocchi a
     * tradurlo nell'assoluto giusto. Che questo funzioni e' anche la
     * prova che la finestra della partizione e' calcolata bene. */
    if (blk_read(blkdev, 0, 1, s) != 0) {
        out->tipo = VOL_FS_ILLEGGIBILE;
        return -1;
    }

    /* La firma 0x55AA e' necessaria ma NON sufficiente: la controlliamo
     * per scartare subito i casi banali, e poi verifichiamo comunque
     * tutti i campi. Un settore dati qualunque puo' finire con 55 AA per
     * caso. */
    if (s[510] != 0x55 || s[511] != 0xAA) return 0;   /* sconosciuto */

    byts_per_sec = le16(s + 11);
    spc          = s[13];
    resvd        = le16(s + 14);
    n_fat        = s[16];
    root_ent     = le16(s + 17);
    tot16        = le16(s + 19);
    fatsz16      = le16(s + 22);
    tot32        = le32(s + 32);
    fatsz32      = le32(s + 36);

    /* --- Validazioni. Ognuna evita un calcolo senza senso piu' avanti. --- */
    if (!settore_valido(byts_per_sec)) return 0;
    if (!spc_valido(spc))              return 0;
    if (resvd == 0)                    return 0;   /* almeno il settore di avvio */
    if (n_fat == 0 || n_fat > 4)       return 0;

    /* Un solo campo della coppia deve essere diverso da zero: e' cosi' che
     * il formato distingue i conteggi a 16 e a 32 bit. */
    tot   = (tot16   != 0) ? (uint32_t)tot16   : tot32;
    fatsz = (fatsz16 != 0) ? (uint32_t)fatsz16 : fatsz32;

    if (tot == 0 || fatsz == 0) return 0;

    /* Il volume non puo' dichiarare piu' settori di quanti ne contenga la
     * partizione. Se lo fa, il settore letto non e' un BPB coerente —
     * oppure la tabella delle partizioni e' sbagliata, e in entrambi i
     * casi montarlo sarebbe pericoloso. */
    if ((uint64_t)tot > n_settori) return 0;

    /* Arrotondamento per eccesso come da specifica: la root directory
     * occupa settori interi. */
    root_dir_sec = ((uint32_t)root_ent * 32 + (byts_per_sec - 1)) / byts_per_sec;

    {
        uint32_t meta = resvd + (uint32_t)n_fat * fatsz + root_dir_sec;

        /* Se i metadati occupano tutto il volume non resta area dati: la
         * sottrazione andrebbe sotto zero e, su interi senza segno,
         * diventerebbe un numero enorme — cioe' un conteggio di cluster
         * inventato e un tipo di FAT sbagliato. */
        if (meta >= tot) return 0;

        dati = tot - meta;
    }

    cluster = dati / spc;     /* spc != 0: gia' validato sopra */

    if (cluster == 0) return 0;

    /* --- LA REGOLA. Vedi il commento in testa al file. --- */
    if (cluster < 4085) {
        out->tipo = VOL_FS_FAT12;
    } else if (cluster < 65525) {
        out->tipo = VOL_FS_FAT16;
    } else {
        out->tipo = VOL_FS_FAT32;
    }

    /* Coerenza incrociata: un FAT32 vero ha per forza root_ent = 0,
     * tot16 = 0 e fatsz16 = 0, e viceversa un FAT12/16 ha root_ent != 0.
     * Se il conteggio dei cluster dice una cosa e questi campi un'altra,
     * il volume e' malformato: si segnala invece di procedere, perche' il
     * dubbio qui significa leggere la FAT con la larghezza sbagliata. */
    if (out->tipo == VOL_FS_FAT32) {
        if (root_ent != 0 || fatsz16 != 0) out->incoerente = 1;
    } else {
        if (root_ent == 0) out->incoerente = 1;
    }

    out->byts_per_sec = byts_per_sec;
    out->sett_per_clu = spc;
    out->n_fat        = n_fat;
    out->riservati    = resvd;
    out->sett_per_fat = fatsz;
    out->root_entries = root_ent;
    out->tot_settori  = tot;
    out->n_cluster    = cluster;

    /* L'etichetta sta in due posti diversi a seconda del formato: e' uno
     * dei punti in cui FAT32 non e' un'estensione compatibile di FAT16. */
    if (out->tipo == VOL_FS_FAT32) {
        out->root_cluster = le32(s + 44);
        out->fsinfo       = le16(s + 48);
        etichetta(s + 71, out->etichetta);
    } else {
        etichetta(s + 43, out->etichetta);
    }

    return 0;
}

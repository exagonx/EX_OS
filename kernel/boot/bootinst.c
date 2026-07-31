/* =============================================================================
 * kernel/boot/bootinst.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Installazione dell'avvio su un disco rigido: MBR + settore di avvio della
 * partizione, con la mappa dei settori di Stage 2 e del kernel.
 *
 * -----------------------------------------------------------------------
 * PERCHE' QUESTO CODICE STA NEL KERNEL E NON IN /bin/install
 *
 * E' l'unico punto in cui EX-OS scrive in un settore che non appartiene a
 * nessun filesystem. Un errore qui non corrompe un file: rende
 * inavviabile un disco, o — sbagliando i 64 byte della tabella —
 * irraggiungibili tutti i suoi dati.
 *
 * La strada facile sarebbe una syscall "scrivi questo settore grezzo" e
 * un programma utente che compone i byte. Ma quella syscall, una volta
 * esistita, permette a QUALUNQUE programma di sovrascrivere la tabella
 * delle partizioni, e nessun controllo dentro /bin/install lo
 * impedirebbe: basta non usare /bin/install.
 *
 * Qui invece il kernel compone lui i settori e garantisce due invarianti
 * che nessun chiamante puo' aggirare:
 *
 *   1. dell'MBR si riscrivono SOLO i byte 0..445 (il codice). I 64 byte
 *      della tabella delle partizioni non vengono mai toccati, tranne il
 *      singolo byte del flag "attiva".
 *   2. del settore di avvio della partizione si riscrive tutto TRANNE i
 *      byte 3..89 SU FAT, che sono il BPB del filesystem gia' presente e
 *      vengono riletti dal disco e rimessi al loro posto.
 *
 *      Su ext2 quei byte si riscrivono, ed e' la scelta giusta: li' non
 *      sono metadati. I primi 1024 byte di un volume ext2 sono l'area
 *      RISERVATA al record di avvio — per questo s_first_data_block vale
 *      1 con blocchi da 1024 — e il superblocco comincia solo dopo.
 *      Conservarli bucherebbe il codice del settore di avvio.
 *
 * Senza la (2), installare l'avvio su FAT renderebbe illeggibile il volume
 * che si sta cercando di rendere avviabile.
 *
 * -----------------------------------------------------------------------
 * LA MAPPA, E IL SUO PREZZO
 *
 * Il settore di avvio non sa leggere la FAT (vedi il perche' in
 * bootloader/stage1hd/boothd.asm). Riceve LBA e lunghezza di Stage 2 e
 * del kernel, e li legge come settori.
 *
 * ⚠️ Ne discende che riscrivere quei due file OBBLIGA a rieseguire
 * l'installazione.
 *
 * Su FAT i file devono essere CONTIGUI: fat_estensione() lo verifica e
 * rifiuta, invece di produrre una mappa che comprende cluster di altri
 * file. Su ext2 non possono esserlo — il blocco di puntatori si infila
 * fra i dati — e la mappa del kernel e' una LISTA di intervalli. Stage 2
 * resta un intervallo solo perche' sta nei primi 12 blocchi diretti, dove
 * nessun indiretto e' ancora stato allocato.
 * ============================================================================= */

#include "kernel.h"
#include "bootinst.h"
#include "blk.h"
#include "vfs.h"
#include "fat.h"
#include "ext2.h"
#include "syscall.h"

/* Immagini prodotte da NASM e incorporate a compile time (vedi Makefile).
 * Incorporarle evita che l'installazione dipenda dalla presenza di due
 * file sul supporto di avvio: un floppy a cui manca boothd.bin darebbe un
 * errore a meta' installazione, con il disco gia' modificato. */
extern const unsigned char boot_mbr_bin[];
extern const unsigned int  boot_mbr_bin_len;
extern const unsigned char boot_hd_bin[];
extern const unsigned int  boot_hd_bin_len;

/* Contratto con bootloader/stage1hd/boothd.asm e con Stage 2.
 * Se cambia li' e non qui, l'unico sintomo e' un sistema che non parte. */
#define PATCH_OFF     0x1A0
#define PATCH_MAGIA   0x44485845u      /* 'EXHD' */

/* Deve coincidere con K_MAX_EXT in bootloader/stage1hd/boothd.asm: e' lo
 * spazio che il settore di avvio riserva alla lista. */
#define K_MAX_EXT     12

#define BPB_INIZIO    3
#define BPB_FINE      90               /* [3, 90) */
#define MBR_CODICE    446

static void metti32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void metti16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

static uint32_t prendi32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Indice del dispositivo a blocchi che rappresenta il disco INTERO su cui
 * vive la partizione `part`. Serve per l'MBR: la partizione e' una
 * finestra che comincia dopo il settore 0, e non puo' — di proposito —
 * raggiungerlo. */
static int disco_di(const BlkDev *part)
{
    int i, n = blk_conta();

    for (i = 0; i < n; i++) {
        const BlkDev *d = blk_get(i);
        if (d == NULL || !d->usato) continue;
        if (d->tipo == BLK_TIPO_DISCO && d->disco == part->disco) return i;
    }
    return -1;
}

/* Cerca nella tabella delle partizioni la voce PRIMARIA che comincia a
 * `primo`. Ritorna 0..3, oppure -1.
 *
 * Il confronto e' sull'LBA di partenza e non sull'ordine: la voce N della
 * tabella non e' necessariamente la partizione N in ordine di disco, e
 * marcare attiva quella sbagliata avvierebbe un altro sistema. */
static int voce_primaria(const uint8_t *mbr, uint64_t primo)
{
    int i;

    for (i = 0; i < 4; i++) {
        const uint8_t *v = mbr + MBR_CODICE + i * 16;
        if (prendi32(v + 12) == 0) continue;            /* voce vuota */
        if ((uint64_t)prendi32(v + 8) == primo) return i;
    }
    return -1;
}

int boot_installa(const char *punto, BootInstEsito *esito)
{
    const VfsMount *vm = NULL;
    const BlkDev   *part;
    int             i, n, idisco, ivoce, fsmnt, e_ext2;
    uint32_t        s2_rel, s2_cnt, k_byte;
    uint32_t        k_lba[K_MAX_EXT], k_cnt[K_MAX_EXT], k_next = 0;
    uint32_t        k_tot = 0;
    uint8_t         sett[512];
    uint8_t         bpb[BPB_FINE - BPB_INIZIO];

    if (punto == NULL || esito == NULL) return ERR(EINVAL);
    esito->s2_lba = esito->k_lba = 0;
    esito->s2_cnt = esito->k_cnt = 0;
    esito->disco  = 0;
    esito->voce   = 0;

    /* --- 1. il montaggio, e deve essere un disco --- */
    n = vfs_conta();
    for (i = 0; i < n; i++) {
        const VfsMount *m = vfs_get(i);
        if (m == NULL) continue;
        if (m->punto[0] != punto[0]) continue;
        {
            int k = 0;
            while (m->punto[k] && m->punto[k] == punto[k]) k++;
            if (m->punto[k] == '\0' && punto[k] == '\0') { vm = m; break; }
        }
    }
    if (vm == NULL) return ERR(ENOENT);

    if (vm->tipo != VFS_FS_FAT && vm->tipo != VFS_FS_EXT2) {
        klog(LOG_ERROR, "INSTALL: '%s' e' il floppy di avvio, non un disco", punto);
        return ERR(EINVAL);
    }
    if (vm->sola_lettura) return ERR(EROFS);

    e_ext2 = (vm->tipo == VFS_FS_EXT2);

    fsmnt = vm->mnt;
    part  = blk_get(vm->blkdev);
    if (part == NULL || part->tipo != BLK_TIPO_PART) return ERR(EINVAL);

    idisco = disco_di(part);
    if (idisco < 0) {
        klog(LOG_ERROR, "INSTALL: disco intero non registrato: impossibile scrivere l'MBR");
        return ERR(ENODEV);
    }

    /* --- 2. la mappa dei due file ------------------------------------
     *
     * Stage 2 vuole UN intervallo, il kernel una LISTA. Non e'
     * un'incoerenza: Stage 2 e' ~1 KB, cioe' dentro i primi 12 blocchi
     * diretti di un inode ext2, dove nessun blocco di puntatori si e'
     * ancora infilato in mezzo. E' anche il pezzo che dev'essere trovato
     * da 512 byte di codice, quindi la sua mappa deve stare in sei byte.
     * Vedi il commento esteso in bootloader/stage1hd/boothd.asm. */
    if (e_ext2) {
        uint32_t s2_lba1[2], s2_cnt1[2], s2_n = 0;
        Ext2DirEntry e;
        int r;

        r = ext2_estensioni(fsmnt, "/boot/stage2.bin", s2_lba1, s2_cnt1, 2, &s2_n);
        if (r != 0 || s2_n != 1) {
            klog(LOG_ERROR, "INSTALL: /boot/stage2.bin mancante o spezzato "
                            "in %u intervalli: il settore di avvio ne legge uno",
                 s2_n);
            return (r == 0) ? ERR(ESPIPE) : ERR(ENOENT);
        }
        s2_rel = s2_lba1[0];
        s2_cnt = s2_cnt1[0];

        r = ext2_estensioni(fsmnt, "/boot/kernel.bin", k_lba, k_cnt,
                            K_MAX_EXT, &k_next);
        if (r == -2) {
            klog(LOG_ERROR, "INSTALL: /boot/kernel.bin e' spezzato in piu' di "
                            "%d intervalli", K_MAX_EXT);
            return ERR(ESPIPE);
        }
        if (r != 0) { klog(LOG_ERROR, "INSTALL: /boot/kernel.bin mancante"); return ERR(ENOENT); }

        if (ext2_stat(fsmnt, "/boot/kernel.bin", &e) != 0) return ERR(EIO);
        k_byte = e.dimensione;
    } else {
        uint32_t k_rel, k_sett;
        int r = fat_estensione(fsmnt, "/BOOT/STAGE2.BIN", &s2_rel, &s2_cnt);
        if (r == -2) { klog(LOG_ERROR, "INSTALL: /BOOT/STAGE2.BIN e' frammentato"); return ERR(ESPIPE); }
        if (r != 0)  { klog(LOG_ERROR, "INSTALL: /BOOT/STAGE2.BIN mancante"); return ERR(ENOENT); }

        r = fat_estensione(fsmnt, "/BOOT/KERNEL.BIN", &k_rel, &k_sett);
        if (r == -2) { klog(LOG_ERROR, "INSTALL: /BOOT/KERNEL.BIN e' frammentato"); return ERR(ESPIPE); }
        if (r != 0)  { klog(LOG_ERROR, "INSTALL: /BOOT/KERNEL.BIN mancante"); return ERR(ENOENT); }

        /* Su FAT il file e' contiguo o non si installa: la lista ha una
         * voce sola. Il formato resta lo stesso dei due filesystem, cosi'
         * Stage 2 non deve sapere da dove sta caricando. */
        k_lba[0] = k_rel;
        k_cnt[0] = k_sett;
        k_next   = 1;

        /* La dimensione ESATTA, non i settori: Stage 2 copia il kernel a
         * 0x100000 usando questo numero, e arrotondare per eccesso
         * significherebbe copiare fino a 511 byte di spazzatura oltre la
         * fine dell'immagine. */
        {
            FatDirEntry e;
            if (fat_stat(fsmnt, "/BOOT/KERNEL.BIN", &e) != 0) return ERR(EIO);
            k_byte = e.dimensione;
        }
    }

    /* I conteggi viaggiano in campi a 16 bit del settore di avvio.
     * Accorgersene qui e' meglio che vedere un troncamento silenzioso
     * all'avvio. */
    if (s2_cnt == 0 || s2_cnt > 0xFFFF) return ERR(EFBIG);
    for (i = 0; i < (int)k_next; i++) {
        if (k_cnt[i] == 0 || k_cnt[i] > 0xFFFF) {
            klog(LOG_ERROR, "INSTALL: intervallo %d del kernel non "
                            "rappresentabile (%u settori)", i, k_cnt[i]);
            return ERR(EFBIG);
        }
        k_tot += k_cnt[i];
    }
    if (k_next == 0) return ERR(EIO);

    /* Gli LBA nel settore di avvio sono ASSOLUTI sul disco: il BIOS non
     * sa nulla di partizioni. */
    for (i = 0; i < (int)k_next; i++) k_lba[i] += (uint32_t)part->primo;

    esito->s2_lba = (uint32_t)part->primo + s2_rel;
    esito->s2_cnt = s2_cnt;
    esito->k_lba  = k_lba[0];
    esito->k_cnt  = k_tot;
    esito->k_next = k_next;
    esito->disco  = part->disco;

    /* --- 3. settore di avvio della partizione --- */

    /* IL BPB SI CONSERVA SOLO SU FAT, e la differenza non e' una
     * raffinatezza: e' opposta nei due casi.
     *
     * Su FAT i byte 3..89 del settore 0 sono il BPB del volume:
     * sovrascriverlo rende illeggibile il filesystem che si sta cercando
     * di rendere avviabile.
     *
     * Su ext2 quei byte NON sono metadati. I primi 1024 byte del volume
     * sono l'area riservata al record di avvio — e' per questo che
     * s_first_data_block vale 1 con blocchi da 1024 — e il superblocco
     * comincia solo dopo. Conservarli qui vorrebbe dire bucare il codice
     * del settore di avvio con 87 byte di spazzatura, cioe' installare un
     * avvio che salta dentro il nulla. */
    if (blk_read(vm->blkdev, 0, 1, sett) != 0) return ERR(EIO);
    if (!e_ext2)
        for (i = BPB_INIZIO; i < BPB_FINE; i++) bpb[i - BPB_INIZIO] = sett[i];

    if (boot_hd_bin_len != 512) {
        klog(LOG_ERROR, "INSTALL: immagine del settore di avvio malformata (%u byte)",
             boot_hd_bin_len);
        return ERR(EIO);
    }
    for (i = 0; i < 512; i++) sett[i] = boot_hd_bin[i];
    if (!e_ext2)
        for (i = BPB_INIZIO; i < BPB_FINE; i++) sett[i] = bpb[i - BPB_INIZIO];

    if (prendi32(sett + PATCH_OFF) != PATCH_MAGIA) {
        klog(LOG_ERROR, "INSTALL: magia assente a 0x%x: contratto rotto", PATCH_OFF);
        return ERR(EIO);
    }
    metti32(sett + PATCH_OFF + 4,  esito->s2_lba);
    metti16(sett + PATCH_OFF + 8,  s2_cnt);
    metti32(sett + PATCH_OFF + 10, k_byte);
    metti16(sett + PATCH_OFF + 14, k_next);

    for (i = 0; i < (int)k_next; i++) {
        metti32(sett + PATCH_OFF + 16 + i * 6,     k_lba[i]);
        metti16(sett + PATCH_OFF + 16 + i * 6 + 4, k_cnt[i]);
    }

    if (blk_write(vm->blkdev, 0, 1, sett) != 0) return ERR(EIO);

    /* --- 4. MBR --- */
    if (blk_read(idisco, 0, 1, sett) != 0) return ERR(EIO);

    ivoce = voce_primaria(sett, part->primo);
    if (ivoce < 0) {
        /* Una partizione LOGICA non ha una voce nella tabella dell'MBR e
         * non puo' essere marcata attiva. Avviare da li' richiederebbe un
         * MBR che sa percorrere la catena delle estese: si dice, invece di
         * scrivere un avvio che non parte. */
        klog(LOG_ERROR, "INSTALL: '%s' non e' una partizione primaria: "
                        "non puo' essere marcata avviabile", part->nome);
        return ERR(EINVAL);
    }

    if (boot_mbr_bin_len != 512) return ERR(EIO);

    /* SOLO il codice: la tabella (446..509) e la firma non si toccano. */
    for (i = 0; i < MBR_CODICE; i++) sett[i] = boot_mbr_bin[i];

    /* Una sola partizione attiva: lasciarne due sarebbe una tabella che
     * l'MBR risolve prendendo la prima, cioe' un avvio che dipende
     * dall'ordine. */
    for (i = 0; i < 4; i++) {
        sett[MBR_CODICE + i * 16] = (i == ivoce) ? 0x80 : 0x00;
    }

    sett[510] = 0x55;
    sett[511] = 0xAA;

    if (blk_write(idisco, 0, 1, sett) != 0) return ERR(EIO);
    if (blk_flush(idisco) != 0) return ERR(EIO);
    if (blk_flush(vm->blkdev) != 0) return ERR(EIO);

    esito->voce = (uint32_t)ivoce + 1;

    klog(LOG_INFO, "INSTALL: avvio installato su hd%u (partizione %d): "
                   "stage2 LBA %u x%u, kernel %u settori in %u intervalli",
         part->disco, ivoce + 1, esito->s2_lba, s2_cnt, k_tot, k_next);
    return 0;
}

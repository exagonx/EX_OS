/* =============================================================================
 * kernel/include/vfs.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Strato di montaggio: instrada un percorso assoluto verso il filesystem
 * che lo ospita.
 *
 * PERCHE' ESISTE. Fino alla 0.131 le syscall passavano il percorso
 * risolto direttamente a fat12_*: il filesystem del floppy era cablato
 * come unico e globale. Un disco montato sotto /disk non aveva alcun
 * punto in cui inserirsi.
 *
 * IL VINCOLO CHE NE DETTA LA FORMA: l'avvio da floppy che oggi funziona
 * non deve rompersi. Per questo il montaggio 0 e' SEMPRE la root sul
 * floppy via fat12.c — la strada collaudata, con la sua scrittura — e
 * tutto cio' che non combacia con un altro punto di montaggio finisce li'
 * esattamente come prima.
 *
 * Le decisioni e le trappole stanno in kernel/fs/vfs.c.
 * ============================================================================= */

#ifndef VFS_H
#define VFS_H

#include "kernel.h"
#include "blk.h"

#define VFS_MAX_MOUNT   6
#define VFS_MAX_OPEN    24
#define VFS_PUNTO_MAX   24
#define VFS_PATH_MAX    320

#define VFS_FS_FAT12FD  1   /* kernel/fs/fat12.c   — il floppy di avvio */
#define VFS_FS_FAT      2   /* kernel/fs/fat.c     — FAT12/16/32 su blocchi */
#define VFS_FS_EXT2     3   /* kernel/fs/ext2.c    — ext2 */
#define VFS_FS_ISO      4   /* kernel/fs/iso9660.c — CD/DVD, SOLA LETTURA */

/* Voce di directory neutra: le syscall non devono conoscere il layout
 * 8.3 di FAT, altrimenti aggiungere un filesystem non-FAT costringerebbe
 * a toccarle di nuovo. */
typedef struct {
    /* 256 = il massimo di ext2. Vedi DIRENT_NAME_MAX in syscall.h: un nome
     * troncato non e' un nome accorciato, e' un nome che non apre niente. */
    char     nome[256];
    uint32_t dimensione;
    uint8_t  is_dir;
} VfsDirEntry;

typedef struct {
    uint32_t dimensione;
    uint8_t  is_dir;
    uint8_t  sola_lettura;
} VfsStat;

typedef struct {
    uint8_t usato;
    char    punto[VFS_PUNTO_MAX];   /* "/" oppure "/disk" */
    uint8_t tipo;                   /* VFS_FS_* */
    uint8_t sola_lettura;
    int     mnt;                    /* handle di fat.c; -1 sul floppy */
    int     blkdev;                 /* dispositivo a blocchi; -1 sul floppy */
    char    dev[BLK_NOME_MAX];      /* nome del dispositivo, per `mount` */
} VfsMount;

/* Registra il montaggio 0, cioe' la root.
 *
 * `boot_drive` e' quello passato da Stage 2 (BootInfo): sotto 0x80 e' un
 * floppy e la root e' il volume FAT12 del floppy; da 0x80 in su si e'
 * avviato da disco e la root e' la partizione ATTIVA del primo disco.
 *
 * Va chiamata dopo fat12_init() E dopo blk_init(). */
void vfs_init(uint8_t boot_drive);

/* `dev` e' un nome del livello a blocchi ("hd0p1"), `punto` un percorso
 * assoluto ("/disk"). Con `sola_lettura` a 1 il montaggio rifiuta ogni
 * modifica anche se il driver saprebbe scrivere. Ritorna 0, o un errno
 * negativo. */
int  vfs_mount(const char *dev, const char *punto, int sola_lettura);
int  vfs_umount(const char *punto);

int  vfs_conta(void);
const VfsMount *vfs_get(int i);

/* --- operazioni sui percorsi: `abs` e' sempre assoluto e gia' risolto --- */
int  vfs_open   (const char *abs, uint32_t flags);
int  vfs_read   (int h, void *buf, uint32_t size, uint32_t offset);
int  vfs_write  (int h, const void *buf, uint32_t size, uint32_t offset);
int  vfs_close  (int h);
int  vfs_stat   (const char *abs, VfsStat *st);
int  vfs_readdir(const char *abs, VfsDirEntry *out, uint32_t max,
                 uint32_t *count, uint32_t start);
int  vfs_mkdir  (const char *abs);
int  vfs_rmdir  (const char *abs);
int  vfs_unlink (const char *abs);

/* Cambia la dimensione di un file. Allungare crea un buco che si legge
 * come zeri; accorciare libera i blocchi in coda. Il floppy (fat12.c) non
 * la sa fare e risponde ENOSYS: e' l'unico driver senza troncamento, e
 * dirlo e' meglio che fingere di aver fatto qualcosa. */
int  vfs_truncate(const char *abs, uint32_t nuova_dim);
void vfs_sync   (void);

#endif /* VFS_H */

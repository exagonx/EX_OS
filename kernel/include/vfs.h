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
/* 48 e non 24 (0.149): con il caricamento su richiesta OGNI processo
 * tiene aperto il proprio eseguibile per tutta la vita, quindi il tetto
 * non conta piu' i soli file aperti dai programmi ma anche i programmi
 * stessi — quattro shell e i loro figli ci arrivavano vicino. */
#define VFS_MAX_OPEN    64
#define VFS_PUNTO_MAX   24
#define VFS_PATH_MAX    320

#define VFS_FS_FAT12FD  1   /* kernel/fs/fat12.c   — il floppy di avvio */
#define VFS_FS_FAT      2   /* kernel/fs/fat.c     — FAT12/16/32 su blocchi */
#define VFS_FS_EXT2     3   /* kernel/fs/ext2.c    — ext2 */

/* 1 se la radice montata e' ext2, cioe' un filesystem che ha i proprietari.
 * E' la domanda che decide se il login e' obbligatorio: vedi vfs.c. */
/* Consegna un file a un altro utente. Solo root, e solo su ext2: su un
 * filesystem senza proprietari rende ENOSYS invece di fingere di aver fatto. */
int vfs_chown(const char *abs, uint32_t uid, uint32_t gid);
/* Cambia i permessi. Lo puo' fare il PROPRIETARIO, non solo root: cambiare i
 * propri permessi non toglie niente a nessuno. */
int vfs_chmod(const char *abs, uint32_t modo);

/* Si puo' eseguire? La chiama il caricatore ELF prima di aprire. 0 se si',
 * -EACCES se no. Vedi vfs.c: e' anche cio' che rende il varco *.drv una
 * barriera invece di una definizione. */
int vfs_eseguibile(const char *abs);

int vfs_radice_ext2(void);

/* L'identita' del processo che sta chiedendo. Senza processo corrente — cioe'
 * durante l'avvio — sono 0: il kernel che monta la radice non e' un utente. */
uint32_t vfs_uid_corrente(void);
uint32_t vfs_gid_corrente(void);
#define VFS_FS_ISO      4   /* kernel/fs/iso9660.c — CD/DVD, SOLA LETTURA */

/* Voce di directory neutra: le syscall non devono conoscere il layout
 * 8.3 di FAT, altrimenti aggiungere un filesystem non-FAT costringerebbe
 * a toccarle di nuovo. */
typedef struct {
    /* 256 = il massimo di ext2. Vedi DIRENT_NAME_MAX in syscall.h: un nome
     * troncato non e' un nome accorciato, e' un nome che non apre niente. */
    char     nome[256];
    uint32_t dimensione;

/* La stessa identita' che stat() mette in VfsStat.ident, composta con la
 * stessa macro (VFS_IDENT in vfs.c). Ogni driver ce l'ha gia' pronta —
 * ext2 l'inode, ISO l'extent, FAT il primo cluster — e prima si fermava
 * qui: vfs_readdir_nl la leggeva e non la copiava, quindi la readdir()
 * della libc metteva zero in d_ino. Cosa costa quello zero sta scritto
 * accanto a DirEntry in kernel/include/syscall.h. */
    uint32_t ident;

    uint8_t  is_dir;
} VfsDirEntry;

typedef struct {
    uint32_t dimensione;

/* Identita' dell'oggetto, unica IN TUTTO IL SISTEMA e non solo dentro il
 * suo volume: ci sta dentro anche il montaggio. Vedi stat_interno() in
 * vfs.c per come si compone e per cosa ci si e' fatto male senza. */
    uint32_t ident;

    uint8_t  is_dir;
    uint8_t  sola_lettura;

/* ! DATA E ORA IN FORMATO FAT SU TUTTI I FILESYSTEM, e non e' pigrizia.
 * E' il formato che attraversa gia' la syscall (Stat.st_date/st_time in
 * kernel/include/syscall.h) e che la libc sa gia' convertire in time_t.
 * Farne passare uno diverso vorrebbe dire due conversioni invece di una,
 * e due occasioni di sbagliare il fuso o l'anno di partenza.
 *
 *   data  bit 15-9 anno-1980, 8-5 mese, 4-0 giorno
 *   ora   bit 15-11 ore, 10-5 minuti, 4-0 secondi/2
 *
 * ! ZERO SIGNIFICA «NON LA SO», non «1 gennaio 1980». La libc lo tratta
 * cosi' (vedi stat_da_grezzo) e i programmi stampano dei trattini: un
 * 1980 inventato sembrerebbe una data vera.
 *
 * ! IL LIMITE E' 1980-2107. Un file ext2 datato prima del 1980 esce con
 * data zero invece che con un anno negativo. */
    uint16_t data;
    uint16_t ora;

    /* ! IL PROPRIETARIO, e vale 0 sui filesystem che non ce l'hanno. FAT e ISO
     * 9660 non hanno proprietari: li' tutto risulta di root con permessi
     * pieni, che e' la verita' — su quei volumi chiunque puo' fare tutto, e
     * fingere il contrario sarebbe peggio che dirlo. */
    uint16_t modo;
    uint16_t uid;
    uint16_t gid;
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

/* Aprire SENZA controllo dei permessi, perche' non e' un utente a chiedere ma
 * il kernel per se'. Un solo uso legittimo: `sys_su` che legge `/boot/ombra`
 * per verificare una password, file che l'utente non puo' e non deve poter
 * aprire da solo. Ogni altro uso e' quasi certamente un errore. */
int  vfs_open_autorita(const char *abs, uint32_t flags);
int  vfs_read   (int h, void *buf, uint32_t size, uint32_t offset);
int  vfs_write  (int h, const void *buf, uint32_t size, uint32_t offset);
int  vfs_close  (int h);
int  vfs_stat   (const char *abs, VfsStat *st);

/* Dichiara un descrittore in piu' sullo stesso file aperto: ritorna `h`
 * stesso, non un handle nuovo. Ci vuole una vfs_close() per ognuno, e solo
 * l'ultima arriva al driver — vedi il conteggio dei riferimenti in vfs.c.
 * E' cio' che rende possibile dup()/dup2(). */
int  vfs_dup    (int h);

/* Come vfs_stat ma su un file GIA' APERTO.
 *
 * Serve a lseek(SEEK_END), che deve sapere quanto e' lungo il file e non
 * ha in mano un percorso: ha un handle. Ripassare dal percorso non era
 * un'alternativa — il descrittore lo conserva, ma un file rinominato o
 * cancellato mentre e' aperto darebbe la dimensione sbagliata o nessuna,
 * e la posizione di scrittura verrebbe calcolata su quel numero. */
int  vfs_fstat  (int h, VfsStat *st);
int  vfs_readdir(const char *abs, VfsDirEntry *out, uint32_t max,
                 uint32_t *count, uint32_t start);
int  vfs_mkdir  (const char *abs);
int  vfs_rmdir  (const char *abs);
int  vfs_unlink (const char *abs);
/* Rinomina NELLA STESSA DIRECTORY e nello stesso montaggio, senza spostare
 * i dati. ENOSYS per tutto il resto, EEXIST se la destinazione c'e' gia'.
 * Vedi il commento esteso in kernel/fs/vfs.c. */
int  vfs_rename (const char *da, const char *a);

/* Cambia la dimensione di un file. Allungare crea un buco che si legge
 * come zeri; accorciare libera i blocchi in coda. Il floppy (fat12.c) non
 * la sa fare e risponde ENOSYS: e' l'unico driver senza troncamento, e
 * dirlo e' meglio che fingere di aver fatto qualcosa. */
int  vfs_truncate(const char *abs, uint32_t nuova_dim);
void vfs_sync   (void);

#endif /* VFS_H */

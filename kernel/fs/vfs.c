/* =============================================================================
 * kernel/fs/vfs.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Instradamento dei percorsi verso il filesystem che li ospita.
 *
 * Le scelte qui sotto sono motivate una per una, perche' quasi tutte
 * hanno un'alternativa che sembra equivalente e non lo e'.
 *
 * -----------------------------------------------------------------------
 * 1. IL MONTAGGIO 0 E' LA ROOT SUL FLOPPY, E NON SI PUO' SMONTARE.
 *
 * Non e' una semplificazione: e' il modo di garantire che l'avvio da
 * floppy resti quello collaudato. Un percorso che non combacia con nessun
 * altro punto di montaggio finisce li', e ci arriva attraverso le stesse
 * fat12_* di prima. Se domani il VFS avesse un difetto nell'instradamento,
 * il caso peggiore sarebbe un disco non raggiungibile — non un sistema che
 * non si avvia.
 *
 * -----------------------------------------------------------------------
 * 2. IL CONFINE DEL PREFISSO: "/disk2" NON STA DENTRO "/disk".
 *
 * E' l'errore classico di ogni instradamento a prefisso. Confrontare i
 * primi N caratteri e fermarsi li' fa finire /disk2 sul montaggio /disk,
 * con il percorso interno "2" — cioe' su un disco SBAGLIATO, in silenzio.
 *
 * Il carattere che segue il prefisso deve essere '\0' oppure '/'. Nessuna
 * eccezione, e vale la pena provarlo con un nome che sia prefisso di un
 * altro, perche' e' un caso che non capita da solo.
 *
 * -----------------------------------------------------------------------
 * 3. PREFISSO PIU' LUNGO, NON PRIMO CHE COMBACIA.
 *
 * Con /disk e /disk/dati montati entrambi, il percorso /disk/dati/x
 * combacia con tutti e due. Vince il piu' lungo, altrimenti il montaggio
 * annidato sarebbe irraggiungibile e l'ordine della tabella deciderebbe
 * il significato di un percorso.
 *
 * -----------------------------------------------------------------------
 * 4. I PUNTI DI MONTAGGIO SONO VIRTUALI, E QUINDI NON DEVONO ESISTERE.
 *
 * Su Unix si monta SOPRA una directory esistente, che sparisce dalla
 * vista finche' il montaggio dura. Qui la directory non serve: /disk
 * compare nell'elenco della root perche' vfs_readdir lo aggiunge.
 *
 * Ne discende una regola che e' meglio dell'originale: montare su un nome
 * che ESISTE gia' viene RIFIUTATO. Su Unix quel caso nasconde dei file
 * senza dirlo; qui non puo' succedere, e in cambio l'elenco di una
 * directory non puo' mai contenere due volte lo stesso nome.
 *
 * -----------------------------------------------------------------------
 * 5. SOLA LETTURA DICHIARATA, NON SUBITA.
 *
 * Dalla 0.133 kernel/fs/fat.c sa anche scrivere, quindi i montaggi sono in
 * lettura/scrittura salvo richiesta esplicita (`mount -r`). Quando un
 * montaggio E' in sola lettura, ogni modifica viene respinta con EROFS
 * PRIMA di toccare il filesystem. L'alternativa — lasciar fallire la
 * scrittura piu' in basso — darebbe errori diversi a seconda del punto in
 * cui il driver si arrende, e un giorno una scrittura parziale.
 *
 * -----------------------------------------------------------------------
 * 6. LA PAGINAZIONE DELL'ELENCO: I MONTAGGI VENGONO PRIMA.
 *
 * readdir e' paginata (start, max). Aggiungere i punti di montaggio IN
 * CODA sembra naturale e non funziona: per sapere dove comincia la coda
 * bisognerebbe conoscere quante voci ha il filesystem, e chiedere una
 * pagina oltre la fine li ristamperebbe a ogni chiamata successiva.
 *
 * Messi in TESTA (indici virtuali 0..K-1) il conto e' esatto e locale:
 * sotto K si emettono i montaggi, sopra si chiede al filesystem a partire
 * da start-K.
 * ============================================================================= */

#include "kernel.h"
#include "vfs.h"
#include "fat12.h"
#include "fat.h"
#include "ext2.h"
#include "vol.h"
#include "blk.h"
#include "syscall.h"

/* Il kernel non ha una libreria di stringhe condivisa: ogni file definisce
 * i propri helper statici (vedi cfg_strcmp in fs/cfg.c, str_equal in
 * kernel_main.c). */
static uint32_t v_len(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static int v_uguale(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void v_copia(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static VfsMount g_mnt[VFS_MAX_MOUNT];

typedef struct {
    uint8_t usato;
    int     im;                      /* indice nella tabella di montaggio */
    int     h12;                     /* handle fat12.c, -1 sugli altri */
    char    interno[VFS_PATH_MAX];   /* percorso dentro il filesystem */
} VfsFile;

static VfsFile g_file[VFS_MAX_OPEN];

/* =============================================================================
 * Instradamento
 * ============================================================================= */

/* Lunghezza del prefisso di un punto di montaggio ai fini del confronto.
 * La root vale 0: e' il prefisso vuoto, quello che combacia con tutto e
 * perde contro qualunque altro (punto 3). */
static uint32_t lunghezza_prefisso(const VfsMount *m)
{
    if (m->punto[0] == '/' && m->punto[1] == '\0') return 0;
    return v_len(m->punto);
}

/* Ritorna l'indice del montaggio che ospita `abs` e scrive in `interno` il
 * percorso dentro quel filesystem. Ritorna -1 solo se la tabella e' vuota,
 * cioe' se vfs_init() non e' stata chiamata. */
static int instrada(const char *abs, char *interno, uint32_t max)
{
    int      i, migliore = -1;
    uint32_t len_migliore = 0;

    for (i = 0; i < VFS_MAX_MOUNT; i++) {
        uint32_t l, k;

        if (!g_mnt[i].usato) continue;
        l = lunghezza_prefisso(&g_mnt[i]);

        if (l == 0) {                       /* la root combacia sempre */
            if (migliore < 0) migliore = i;
            continue;
        }

        for (k = 0; k < l; k++) {
            if (abs[k] != g_mnt[i].punto[k]) break;
        }
        if (k < l) continue;

        /* IL CONFINE (punto 2): senza questo "/disk2" finirebbe su
         * "/disk" con percorso interno "2". */
        if (abs[l] != '\0' && abs[l] != '/') continue;

        if (migliore < 0 || l > len_migliore) {
            migliore     = i;
            len_migliore = l;
        }
    }

    if (migliore < 0) return -1;

    {
        uint32_t l = lunghezza_prefisso(&g_mnt[migliore]);
        const char *resto = abs + l;

        if (resto[0] == '\0') v_copia(interno, "/", max);
        else                  v_copia(interno, resto, max);
    }

    return migliore;
}

/* Un punto di montaggio e' figlio DIRETTO della directory `dir` se sta
 * dentro `dir` e non contiene altri '/' dopo di essa. Serve a vfs_readdir
 * per far comparire /disk nell'elenco della root. */
static int e_figlio(const char *punto, const char *dir, const char **nome_out)
{
    uint32_t ld;
    const char *r;

    if (punto[0] == '/' && punto[1] == '\0') return 0;   /* la root, mai */

    if (dir[0] == '/' && dir[1] == '\0') {
        r = punto + 1;
    } else {
        uint32_t k;
        ld = v_len(dir);
        for (k = 0; k < ld; k++) if (punto[k] != dir[k]) return 0;
        if (punto[ld] != '/') return 0;
        r = punto + ld + 1;
    }

    if (*r == '\0') return 0;
    { const char *p = r; while (*p) { if (*p == '/') return 0; p++; } }

    *nome_out = r;
    return 1;
}

/* =============================================================================
 * Tabella di montaggio
 * ============================================================================= */
/* Partizione ATTIVA del disco `disco`, come dispositivo a blocchi.
 * Ritorna -1 se non ce n'e' una.
 *
 * Si legge il settore 0 e si guarda il flag, invece di fidarsi
 * dell'ordine: la voce N della tabella non e' la partizione N in ordine di
 * disco, ed e' esattamente lo stesso confronto che fa l'MBR di
 * bootloader/mbr/mbr.asm. Due pezzi di codice che devono scegliere la
 * STESSA partizione devono usare lo stesso criterio, o il kernel monta
 * come root un volume diverso da quello da cui e' stato caricato. */
static int partizione_attiva(int disco_idx)
{
    uint8_t mbr[512];
    int     i, n;

    if (blk_read(disco_idx, 0, 1, mbr) != 0) return -1;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return -1;

    for (i = 0; i < 4; i++) {
        const uint8_t *v = mbr + 446 + i * 16;
        uint32_t primo;

        if (v[0] != 0x80) continue;

        primo = (uint32_t)v[8] | ((uint32_t)v[9] << 8)
              | ((uint32_t)v[10] << 16) | ((uint32_t)v[11] << 24);

        n = blk_conta();
        {
            int k;
            for (k = 0; k < n; k++) {
                const BlkDev *d = blk_get(k);
                if (d == NULL || !d->usato) continue;
                if (d->tipo != BLK_TIPO_PART) continue;
                if (d->primo == (uint64_t)primo) return k;
            }
        }
    }

    return -1;
}

void vfs_init(uint8_t boot_drive)
{
    int i;

    for (i = 0; i < VFS_MAX_MOUNT; i++) g_mnt[i].usato = 0;
    for (i = 0; i < VFS_MAX_OPEN;  i++) g_file[i].usato = 0;

    v_copia(g_mnt[0].punto, "/", VFS_PUNTO_MAX);
    g_mnt[0].usato        = 1;
    g_mnt[0].sola_lettura = 0;

    /* --- avviati da disco? --- */
    if (boot_drive >= 0x80) {
        /* Il primo disco ATA. E' un'assunzione, e va detta: il BIOS
         * numera 0x80, 0x81... in un ordine che non e' garantito
         * corrispondere a quello dei canali IDE. Finche' si avvia dal
         * primo disco — il caso normale — coincidono. */
        int idisco = blk_trova("hd0");
        int ipart  = (idisco >= 0) ? partizione_attiva(idisco) : -1;
        int mnt    = (ipart  >= 0) ? fat_mount(ipart) : -1;

        if (mnt >= 0) {
            const BlkDev *d = blk_get(ipart);

            /* La root e' il montaggio che non si smonta mai: senza questa
             * riga il disco da cui il sistema sta girando risulterebbe
             * libero, e ripartizionarlo sarebbe permesso. */
            blk_acquisisci(ipart);

            g_mnt[0].tipo   = VFS_FS_FAT;
            g_mnt[0].mnt    = mnt;
            g_mnt[0].blkdev = ipart;
            v_copia(g_mnt[0].dev, d ? d->nome : "hd0", BLK_NOME_MAX);

            klog(LOG_INFO, "VFS: root '/' su %s (FAT%d, avvio da disco 0x%02x)",
                 g_mnt[0].dev, fat_tipo(mnt), boot_drive);
            return;
        }

        /* Non si puo' proseguire in silenzio: senza root non esiste
         * /bin/sh, e il sintomo sarebbe "shell non trovata" a diversi
         * passi di distanza dalla causa vera. */
        klog(LOG_ERROR, "VFS: avvio da disco 0x%02x ma la partizione attiva "
                        "non e' montabile: ripiego sul floppy", boot_drive);
    }

    /* Montaggio 0: la root sul floppy, via fat12.c (punto 1). */
    g_mnt[0].tipo   = VFS_FS_FAT12FD;
    g_mnt[0].mnt    = -1;
    g_mnt[0].blkdev = -1;
    v_copia(g_mnt[0].dev, "fd0", BLK_NOME_MAX);

    klog(LOG_INFO, "VFS: root '/' sul floppy (fat12.c)");
}

int vfs_conta(void)
{
    int i, n = 0;
    for (i = 0; i < VFS_MAX_MOUNT; i++) if (g_mnt[i].usato) n++;
    return n;
}

const VfsMount *vfs_get(int i)
{
    int k, n = 0;
    for (k = 0; k < VFS_MAX_MOUNT; k++) {
        if (!g_mnt[k].usato) continue;
        if (n == i) return &g_mnt[k];
        n++;
    }
    return NULL;
}

int vfs_mount(const char *dev, const char *punto, int sola_lettura)
{
    int        blkdev, slot = -1, i, mnt;
    VolumeInfo vi;
    VfsStat    st;

    if (dev == NULL || punto == NULL)  return ERR(EINVAL);
    if (punto[0] != '/')               return ERR(EINVAL);
    if (v_len(punto) >= VFS_PUNTO_MAX) return ERR(EINVAL);

    /* La root e' il floppy e resta tale (punto 1). */
    if (punto[1] == '\0') return ERR(EBUSY);

    /* Uno slash finale ("/disk/") darebbe un punto che non combacia mai
     * con se stesso: meglio rifiutarlo che accettarlo e non funzionare. */
    if (punto[v_len(punto) - 1] == '/') return ERR(EINVAL);

    for (i = 0; i < VFS_MAX_MOUNT; i++) {
        if (!g_mnt[i].usato) { if (slot < 0) slot = i; continue; }
        if (v_uguale(g_mnt[i].punto, punto)) return ERR(EBUSY);
        if (v_uguale(g_mnt[i].dev,   dev))   return ERR(EBUSY);
    }
    if (slot < 0) return ERR(EMFILE);

    blkdev = blk_trova(dev);
    if (blkdev < 0) return ERR(ENOENT);

    /* Il nome non deve esistere gia' (punto 4): montarci sopra
     * nasconderebbe dei file senza dirlo. */
    if (vfs_stat(punto, &st) == 0) {
        klog(LOG_ERROR, "VFS: '%s' esiste gia': montaggio rifiutato", punto);
        return ERR(EEXIST);
    }

    /* Anche la directory che lo conterra' deve esistere, altrimenti si
     * otterrebbe un /a/b raggiungibile senza che /a esista. */
    {
        char padre[VFS_PUNTO_MAX];
        uint32_t l = v_len(punto);
        while (l > 1 && punto[l - 1] != '/') l--;
        if (l > 1) l--;                     /* togli lo slash */
        for (i = 0; (uint32_t)i < l; i++) padre[i] = punto[i];
        padre[l] = '\0';
        if (l == 0) { padre[0] = '/'; padre[1] = '\0'; }

        if (!(padre[0] == '/' && padre[1] == '\0')) {
            if (vfs_stat(padre, &st) != 0 || !st.is_dir) {
                klog(LOG_ERROR, "VFS: '%s' non esiste: montaggio rifiutato", padre);
                return ERR(ENOENT);
            }
        }
    }

    if (vol_identifica(blkdev, &vi) != 0) return ERR(EIO);
    if (vi.tipo != VOL_FS_FAT12 && vi.tipo != VOL_FS_FAT16 &&
        vi.tipo != VOL_FS_FAT32 && vi.tipo != VOL_FS_EXT2) {
        klog(LOG_ERROR, "VFS: '%s' non contiene un filesystem riconoscibile", dev);
        return ERR(EINVAL);
    }
    if (vi.incoerente) {
        klog(LOG_ERROR, "VFS: '%s': metadati incoerenti, montaggio rifiutato", dev);
        return ERR(EINVAL);
    }

    if (vi.tipo == VOL_FS_EXT2) {
        mnt = ext2_mount(blkdev);
        if (mnt < 0) return ERR(EIO);

        /* ext2 si monta SEMPRE in sola lettura, qualunque cosa abbia
         * chiesto il chiamante: il driver non sa scrivere. Imporlo qui e
         * non fidarsi del flag e' cio' che fa fallire una `mkdir` con
         * EROFS — che l'utente capisce — invece di lasciarla arrivare a un
         * driver che non ha la funzione e fallisce con un errore generico
         * a tre livelli di distanza. */
        sola_lettura = 1;
        g_mnt[slot].tipo = VFS_FS_EXT2;
    } else {
        mnt = fat_mount(blkdev);
        if (mnt < 0) return ERR(EIO);

        g_mnt[slot].tipo = VFS_FS_FAT;
    }

    /* Dichiara al livello a blocchi che questo dispositivo e' in uso: da
     * qui in poi blk_rescan() rifiutera' di rileggere la tabella del suo
     * disco. Vedi il commento in blk.h — e' il motivo per cui esiste il
     * contatore invece di una domanda al VFS. */
    blk_acquisisci(blkdev);

    g_mnt[slot].usato        = 1;
    v_copia(g_mnt[slot].punto, punto, VFS_PUNTO_MAX);
    g_mnt[slot].sola_lettura = sola_lettura ? 1 : 0;
    g_mnt[slot].mnt          = mnt;
    g_mnt[slot].blkdev       = blkdev;
    v_copia(g_mnt[slot].dev, dev, BLK_NOME_MAX);

    if (g_mnt[slot].tipo == VFS_FS_EXT2)
        klog(LOG_INFO, "VFS: montato %s su %s (ext2, sola lettura)", dev, punto);
    else
        klog(LOG_INFO, "VFS: montato %s su %s (FAT%d, %s)",
             dev, punto, fat_tipo(mnt),
             g_mnt[slot].sola_lettura ? "sola lettura" : "lettura/scrittura");
    return 0;
}

int vfs_umount(const char *punto)
{
    int i, k;

    if (punto == NULL) return ERR(EINVAL);

    for (i = 0; i < VFS_MAX_MOUNT; i++) {
        if (!g_mnt[i].usato) continue;
        if (!v_uguale(g_mnt[i].punto, punto)) continue;

        if (i == 0) return ERR(EBUSY);   /* la root non si smonta */

        /* Un file ancora aperto su questo montaggio significa che qualcuno
         * ha in mano un handle che diventerebbe un riferimento a un
         * montaggio libero — cioe' letture da un volume diverso. */
        for (k = 0; k < VFS_MAX_OPEN; k++) {
            if (g_file[k].usato && g_file[k].im == i) return ERR(EBUSY);
        }

        if (g_mnt[i].tipo == VFS_FS_EXT2) ext2_umount(g_mnt[i].mnt);
        else                              fat_umount(g_mnt[i].mnt);

        if (g_mnt[i].blkdev >= 0) blk_rilascia(g_mnt[i].blkdev);
        g_mnt[i].usato = 0;
        klog(LOG_INFO, "VFS: smontato %s", punto);
        return 0;
    }

    return ERR(ENOENT);
}

/* =============================================================================
 * Operazioni
 * ============================================================================= */

/* Vero se il percorso interno e' la radice del suo filesystem. */
static int e_radice(const char *interno)
{
    return interno[0] == '/' && interno[1] == '\0';
}

int vfs_open(const char *abs, uint32_t flags)
{
    char interno[VFS_PATH_MAX];
    int  im, slot = -1, i;

    if (abs == NULL) return ERR(EINVAL);

    im = instrada(abs, interno, sizeof(interno));
    if (im < 0) return ERR(ENOENT);

    /* EROFS PRIMA di toccare il filesystem (punto 5). */
    if (g_mnt[im].sola_lettura &&
        (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND))) {
        return ERR(EROFS);
    }

    for (i = 0; i < VFS_MAX_OPEN; i++) {
        if (!g_file[i].usato) { slot = i; break; }
    }
    if (slot < 0) return ERR(EMFILE);

    if (g_mnt[im].tipo == VFS_FS_FAT12FD) {
        int h = fat12_open(interno, flags);
        if (h < 0) return ERR(ENOENT);
        g_file[slot].h12 = h;
    } else if (g_mnt[im].tipo == VFS_FS_EXT2) {
        Ext2DirEntry e;

        /* Nessun O_CREAT e nessun O_TRUNC da gestire: il montaggio e'
         * forzato in sola lettura, quindi il controllo EROFS in testa a
         * questa funzione ha gia' respinto ogni flag di scrittura. */
        if (ext2_stat(g_mnt[im].mnt, interno, &e) != 0) return ERR(ENOENT);
        if (e.is_dir) return ERR(EISDIR);
        g_file[slot].h12 = -1;
    } else {
        FatDirEntry e;

        if (fat_stat(g_mnt[im].mnt, interno, &e) != 0) {
            if (!(flags & O_CREAT)) return ERR(ENOENT);
            {
                int r = fat_create(g_mnt[im].mnt, interno);
                if (r == -2) return ERR(EEXIST);
                if (r != 0)  return ERR(EIO);
            }
            if (fat_stat(g_mnt[im].mnt, interno, &e) != 0) return ERR(EIO);
        } else if (flags & O_TRUNC) {
            if (fat_truncate(g_mnt[im].mnt, interno, 0) != 0) return ERR(EIO);
        }

        if (e.is_dir) return ERR(EISDIR);
        g_file[slot].h12 = -1;
    }

    g_file[slot].usato = 1;
    g_file[slot].im    = im;
    v_copia(g_file[slot].interno, interno, VFS_PATH_MAX);
    return slot;
}

int vfs_read(int h, void *buf, uint32_t size, uint32_t offset)
{
    if (h < 0 || h >= VFS_MAX_OPEN || !g_file[h].usato) return ERR(EBADF);

    if (g_mnt[g_file[h].im].tipo == VFS_FS_FAT12FD)
        return fat12_read(g_file[h].h12, buf, size, offset);

    if (g_mnt[g_file[h].im].tipo == VFS_FS_EXT2)
        return ext2_read(g_mnt[g_file[h].im].mnt, g_file[h].interno,
                         buf, size, offset);

    return fat_read(g_mnt[g_file[h].im].mnt, g_file[h].interno,
                    buf, size, offset);
}

/* `offset` viene dal descrittore del processo. fat12.c non lo usa: tiene
 * la propria posizione interna nell'handle, e cambiarlo qui vorrebbe dire
 * toccare il driver del floppy — proprio quello che non si vuole toccare.
 * fat.c invece e' senza stato per file e l'offset gli serve. */
int vfs_write(int h, const void *buf, uint32_t size, uint32_t offset)
{
    int im;

    if (h < 0 || h >= VFS_MAX_OPEN || !g_file[h].usato) return ERR(EBADF);
    im = g_file[h].im;
    if (g_mnt[im].sola_lettura) return ERR(EROFS);

    if (g_mnt[im].tipo == VFS_FS_FAT12FD)
        return fat12_write(g_file[h].h12, buf, size);

    {
        int n = fat_write(g_mnt[im].mnt, g_file[h].interno, buf, size, offset);
        return (n < 0) ? ERR(EIO) : n;
    }
}

int vfs_close(int h)
{
    if (h < 0 || h >= VFS_MAX_OPEN || !g_file[h].usato) return ERR(EBADF);

    if (g_mnt[g_file[h].im].tipo == VFS_FS_FAT12FD)
        fat12_close(g_file[h].h12);

    g_file[h].usato = 0;
    return 0;
}

int vfs_stat(const char *abs, VfsStat *st)
{
    char interno[VFS_PATH_MAX];
    int  im;

    if (abs == NULL || st == NULL) return ERR(EINVAL);

    im = instrada(abs, interno, sizeof(interno));
    if (im < 0) return ERR(ENOENT);

    /* La radice di un filesystem non ha una voce di directory da
     * interrogare: fat12_stat("/") e fat_stat("/") falliscono entrambe.
     * Vale per "/" e per ogni punto di montaggio. */
    if (e_radice(interno)) {
        st->dimensione   = 0;
        st->is_dir       = 1;
        st->sola_lettura = g_mnt[im].sola_lettura;
        return 0;
    }

    if (g_mnt[im].tipo == VFS_FS_FAT12FD) {
        Fat12Stat f;
        if (fat12_stat(interno, &f) != 0) return ERR(ENOENT);
        st->dimensione   = f.size;
        st->is_dir       = (f.attr & FAT12_ATTR_DIRECTORY) ? 1 : 0;
        st->sola_lettura = (f.attr & FAT12_ATTR_READONLY)  ? 1 : 0;
    } else if (g_mnt[im].tipo == VFS_FS_EXT2) {
        Ext2DirEntry e;
        if (ext2_stat(g_mnt[im].mnt, interno, &e) != 0) return ERR(ENOENT);
        st->dimensione   = e.dimensione;
        st->is_dir       = e.is_dir;
        st->sola_lettura = 1;
    } else {
        FatDirEntry e;
        if (fat_stat(g_mnt[im].mnt, interno, &e) != 0) return ERR(ENOENT);
        st->dimensione   = e.dimensione;
        st->is_dir       = e.is_dir;
        st->sola_lettura = 1;
    }

    return 0;
}

int vfs_readdir(const char *abs, VfsDirEntry *out, uint32_t max,
                uint32_t *count, uint32_t start)
{
    char     interno[VFS_PATH_MAX];
    int      im, i;
    uint32_t scritti = 0, k_punti = 0, saltati = 0;

    if (abs == NULL || out == NULL || count == NULL || max == 0)
        return ERR(EINVAL);

    *count = 0;
    im = instrada(abs, interno, sizeof(interno));
    if (im < 0) return ERR(ENOENT);

    /* --- I punti di montaggio figli, in TESTA (punto 6) --- */
    for (i = 0; i < VFS_MAX_MOUNT && scritti < max; i++) {
        const char *nome;

        if (!g_mnt[i].usato || i == im) continue;
        if (!e_figlio(g_mnt[i].punto, abs, &nome)) continue;

        k_punti++;
        if (saltati < start) { saltati++; continue; }

        v_copia(out[scritti].nome, nome, sizeof(out[scritti].nome));
        out[scritti].dimensione = 0;
        out[scritti].is_dir     = 1;
        scritti++;
    }

    /* I montaggi gia' emessi (o saltati) non vanno richiesti al
     * filesystem: da qui in poi si conta a partire da start - k_punti. */
    {
        uint32_t s_fs = (start > k_punti) ? (start - k_punti) : 0;

        if (g_mnt[im].tipo == VFS_FS_FAT12FD) {
            while (scritti < max) {
                Fat12DirEntry tmp[8];
                uint32_t cap = max - scritti, n = 0, j;

                if (cap > 8) cap = 8;
                if (fat12_readdir_path(e_radice(interno) ? NULL : interno,
                                       tmp, cap, &n, s_fs) != 0) {
                    /* Se qualche punto di montaggio e' gia' stato scritto,
                     * la directory esiste comunque: non e' un errore. */
                    if (scritti == 0) return ERR(ENOENT);
                    break;
                }
                for (j = 0; j < n; j++) {
                    fat12_format_name(&tmp[j], out[scritti].nome);
                    out[scritti].dimensione = tmp[j].file_size;
                    out[scritti].is_dir =
                        (tmp[j].attr & FAT12_ATTR_DIRECTORY) ? 1 : 0;
                    scritti++;
                }
                s_fs += n;
                if (n < cap) break;
            }
        } else if (g_mnt[im].tipo == VFS_FS_EXT2) {
            while (scritti < max) {
                Ext2DirEntry tmp[4];       /* 4 e non 8: le voci sono piu' grosse */
                uint32_t cap = max - scritti, j;
                int      n;

                if (cap > 4) cap = 4;
                n = ext2_readdir(g_mnt[im].mnt, interno, tmp, cap, s_fs);
                if (n < 0) {
                    if (scritti == 0) return ERR(ENOENT);
                    break;
                }
                for (j = 0; j < (uint32_t)n; j++) {
                    /* I nomi ext2 arrivano a 255 byte, VfsDirEntry ne
                     * tiene 16: v_copia tronca. E' una perdita vera, ed e'
                     * limitata dal formato della struttura che attraversa
                     * la syscall, non dal driver. */
                    v_copia(out[scritti].nome, tmp[j].nome,
                            sizeof(out[scritti].nome));
                    out[scritti].dimensione = tmp[j].dimensione;
                    out[scritti].is_dir     = tmp[j].is_dir;
                    scritti++;
                }
                s_fs += (uint32_t)n;
                if ((uint32_t)n < cap) break;
            }
        } else {
            while (scritti < max) {
                FatDirEntry tmp[8];
                uint32_t cap = max - scritti, j;
                int      n;

                if (cap > 8) cap = 8;
                n = fat_readdir(g_mnt[im].mnt, interno, tmp, cap, s_fs);
                if (n < 0) {
                    if (scritti == 0) return ERR(ENOENT);
                    break;
                }
                for (j = 0; j < (uint32_t)n; j++) {
                    v_copia(out[scritti].nome, tmp[j].nome,
                            sizeof(out[scritti].nome));
                    out[scritti].dimensione = tmp[j].dimensione;
                    out[scritti].is_dir     = tmp[j].is_dir;
                    scritti++;
                }
                s_fs += (uint32_t)n;
                if ((uint32_t)n < cap) break;
            }
        }
    }

    *count = scritti;
    return 0;
}

/* Le tre operazioni di modifica hanno la stessa forma: instrada, respingi
 * se sola lettura, delega. */
static int modifica(const char *abs, int quale)
{
    char interno[VFS_PATH_MAX];
    int  im;

    if (abs == NULL) return ERR(EINVAL);

    im = instrada(abs, interno, sizeof(interno));
    if (im < 0) return ERR(ENOENT);
    if (g_mnt[im].sola_lettura) return ERR(EROFS);

    /* La radice di un montaggio non si crea ne' si cancella. */
    if (e_radice(interno)) return ERR(EBUSY);

    if (g_mnt[im].tipo == VFS_FS_FAT12FD) {
        if (quale == 0) return fat12_mkdir(interno);
        if (quale == 1) return fat12_rmdir(interno);
        return fat12_delete(interno);
    }

    /* fat.c distingue due casi con -2, e il chiamante deve poterli
     * distinguere anche lui: "esiste gia'" e "directory non vuota" sono
     * cose che l'utente puo' correggere, "errore" no. */
    {
        int r;
        if (quale == 0) {
            r = fat_mkdir(g_mnt[im].mnt, interno);
            if (r == -2) return ERR(EEXIST);
        } else if (quale == 1) {
            r = fat_rmdir(g_mnt[im].mnt, interno);
            if (r == -2) return ERR(ENOTEMPTY);
        } else {
            r = fat_unlink(g_mnt[im].mnt, interno);
        }
        return (r != 0) ? ERR(EIO) : 0;
    }
}

int vfs_mkdir (const char *abs) { return modifica(abs, 0); }
int vfs_rmdir (const char *abs) { return modifica(abs, 1); }
int vfs_unlink(const char *abs) { return modifica(abs, 2); }

void vfs_sync(void)
{
    int i;

    fat12_sync();

    /* Ogni montaggio fat.c ha la propria cache sporca. Riversarne solo
     * alcuni lascerebbe un volume coerente e un altro no, senza che
     * l'utente possa sapere quale. */
    for (i = 0; i < VFS_MAX_MOUNT; i++) {
        if (g_mnt[i].usato && g_mnt[i].tipo == VFS_FS_FAT) fat_sync(g_mnt[i].mnt);
    }
}

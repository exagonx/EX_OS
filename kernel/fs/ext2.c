/* =============================================================================
 * kernel/fs/ext2.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Lettura di un filesystem ext2. Vedi kernel/include/ext2.h per il perche'
 * sia in sola lettura.
 *
 * -----------------------------------------------------------------------
 * QUALI VOLUMI ACCETTA, E PERCHE' RIFIUTARE E' PIU' SICURO CHE PROVARCI
 *
 * ext2 dichiara le proprie funzionalita' in tre insiemi, e la differenza
 * fra i tre e' precisamente cosa fare quando non le si conosce:
 *
 *   s_feature_compat     si puo' ignorare del tutto.
 *   s_feature_ro_compat  se non la conosci, monta in SOLA LETTURA.
 *   s_feature_incompat   se non la conosci, NON MONTARE.
 *
 * La terza e' la ragione per cui questo driver rifiuta invece di provarci:
 * una funzionalita' incompatibile cambia il significato dei campi che gia'
 * si sanno leggere. Un volume con COMPRESSION o con extent (ext4) ha
 * i_block che non contiene numeri di blocco: leggerlo "come se" restituisce
 * dati presi da posizioni arbitrarie del disco, in silenzio.
 *
 * Essendo in sola lettura, di ro_compat non ci importa niente: qualunque
 * cosa dichiari, noi non scriviamo comunque.
 *
 * -----------------------------------------------------------------------
 * LE TRAPPOLE DEL FORMATO
 *
 *   1. La dimensione del blocco NON e' fissa: 1024 << s_log_block_size.
 *      /bin/mkfs scrive sempre 1024, ma mke2fs usa 4096 sui volumi grandi,
 *      e un volume creato su Linux e' il caso normale, non l'eccezione.
 *
 *   2. s_first_data_block vale 1 con blocchi da 1024 e 0 con blocchi piu'
 *      grandi. Cambia il calcolo di dove comincia OGNI gruppo, quindi
 *      cablarlo a 1 produce un driver che funziona solo sui volumi che ci
 *      siamo formattati da soli.
 *
 *   3. La dimensione dell'inode e' 128 solo in revisione 0. In revisione 1
 *      la dice s_inode_size, e su ext2 moderni e' spesso 256: usare 128
 *      significa leggere ogni inode dispari a meta' del precedente.
 *
 *   4. i_blocks e' in unita' da 512 byte e NON dice quanti blocchi legge
 *      il file: comprende gli indiretti. La dimensione utile e' i_size.
 *
 *   5. Un file SPARSO ha voci i_block a zero in mezzo, e il blocco 0 non
 *      e' un blocco: significa "qui ci sono zeri". Leggerlo come blocco
 *      restituirebbe il record di avvio del volume dentro il file.
 *
 * -----------------------------------------------------------------------
 * NIENTE CACHE, E IL PREZZO CHE SI PAGA
 *
 * Ogni lettura va al livello a blocchi. Un file letto in sequenza rilegge
 * il blocco indiretto una volta per ogni blocco di dati — su un file da
 * 64 KB con blocchi da 1024 sono 64 letture in piu'.
 *
 * E' lento e volutamente cosi': una cache e' lo strato in cui un driver
 * appena scritto sbaglia in modo difficile da vedere, perche' l'errore si
 * manifesta come dati vecchi molto dopo l'operazione che li ha prodotti.
 * L'unica eccezione e' l'ULTIMO blocco indiretto letto, tenuto da parte:
 * costa quattro righe, elimina il grosso delle riletture, e non puo'
 * restituire dati stantii perche' il volume e' in sola lettura.
 * ============================================================================= */

#include "kernel.h"
#include "ext2.h"
#include "ext2_interno.h"
#include "blk.h"
#include "vol.h"
#include "rtc.h"

/* =============================================================================
 * EXT3 AND EXT4, READ HERE, WRITTEN BY THE MODULE (@EXT4-MODULO, 2026-10)
 *
 * ext4 is backward compatible: a driver that reads ext4 also reads ext2 and
 * ext3, so this file grows instead of getting a twin. What a volume made by
 * Linux today needs, for READING:
 *   - EXTENTS: a file's blocks are runs (start, length) in a small tree
 *     whose root sits where i_block used to hold the pointers (extent_map);
 *   - 64BIT: group descriptors are s_desc_size bytes, not 32;
 *   - FLEX_BG: bitmaps and inode tables are elsewhere, but the descriptors
 *     say where, and that is all reading uses;
 *   - large inodes (256 bytes) were already handled;
 *   - indexed directories (dir_index) are read linearly: the index lives
 *     inside entries whose inode is 0 or whose length covers it, so a
 *     plain scan skips it. Same for the checksum tail of each block.
 *   - the journal (ext3) is read by the module when it mounts the volume
 *     for writing; read-only, a volume marked RECOVER is read as it is on
 *     disk, and the log says the newest changes may be missing.
 *
 * ! WRITING IS NOT HERE AT ALL (kernel 0.263). It is /dev/ext4.mod
 * (kernel/moduli/ext4/), loaded right after the root is mounted. Until it
 * registers, every ext2/3/4 volume is read-only (EXT2_RO_MODULO); when it
 * does, it says which features it can write, and each volume that has only
 * those - and that its monta() accepts - becomes writable (valuta()).
 * The write calls below are stubs: they check the volume is writable HERE,
 * in the kernel, and then call the module.
 *
 * Still refused at mount: COMPRESSION, JOURNAL_DEV (that is a journal, not
 * a file system), META_BG (descriptors scattered over the disk), ENCRYPT,
 * and any bit this list does not know. INLINE_DATA is accepted at mount, and
 * a file whose data is inside its inode is refused when read, with a message.
 * ============================================================================= */
#define INCOMPAT_LETTURA    (INCOMPAT_FILETYPE | INCOMPAT_RECOVER | INCOMPAT_EXTENTS | \
                             INCOMPAT_64BIT | INCOMPAT_MMP | INCOMPAT_FLEX_BG |       \
                             INCOMPAT_EA_INODE | INCOMPAT_CSUM_SEED | INCOMPAT_LARGEDIR | \
                             INCOMPAT_INLINE)

static Ext2Mount g_mnt[EXT2_MAX_MOUNT];

/* =============================================================================
 * I BUFFER DI LAVORO, E PERCHE' SONO CINQUE E NON UNO
 *
 * Sono globali e non per montaggio perche' le syscall di EX-OS non si
 * annidano: un'operazione finisce prima che cominci la successiva (il
 * lucchetto del VFS). Se un giorno il kernel diventasse rientrante
 * andrebbero dentro Ext2Mount, ed e' scritto qui perche' quel giorno sia una
 * modifica e non una caccia.
 *
 * Ma sono CINQUE, uno per ogni genere di blocco, e questa e' la decisione
 * piu' importante di tutto il file.
 *
 * Con un buffer solo, ogni funzione che ne chiama un'altra perde cio' che
 * aveva in mano. Scorrere una directory e leggere l'inode di una voce si
 * pestano i piedi: al ritorno il "blocco della directory" contiene una
 * tabella di inode, e il ciclo continua a scorrerla credendo che siano
 * voci. In lettura il sintomo sono nomi inventati; in SCRITTURA la stessa
 * confusione fa scrivere una voce di directory dentro una tabella di
 * inode, cioe' corrompe il volume in un punto che non c'entra niente con
 * l'operazione richiesta.
 *
 * Con un buffer per genere il problema non puo' presentarsi: nessuna
 * funzione tocca il buffer di un'altra, e chi legge il codice vede dal
 * NOME quale contenuto sta guardando.
 *
 * ! SONO IN UNA STRUTTURA (Ext2Stato) perche' il modulo di scrittura lavora
 * sugli STESSI buffer e sulla stessa cache dell'indiretto: vedi
 * kernel/include/ext2_interno.h. I nomi restano quelli di sempre.
 * ============================================================================= */
static Ext2Stato g_st;

#define b_dati       (g_st.b_dati)
#define b_ind        (g_st.b_ind)
#define b_ino        (g_st.b_ino)
#define b_ext        (g_st.b_ext)
#define b_desc       (g_st.b_desc)
#define g_ind_num    (g_st.ind_num)
#define g_ind_mnt    (g_st.ind_mnt)
#define g_ind_sporco (g_st.ind_sporco)

/* The write module, once it has registered (ext2_registra). */
static const Ext2Ops *g_ops = NULL;

static Ext2Mount *prendi(int mnt)
{
    if (mnt < 0 || mnt >= EXT2_MAX_MOUNT) return NULL;
    if (!g_mnt[mnt].usato) return NULL;
    return &g_mnt[mnt];
}

/* The mount, for an operation that WRITES: NULL also when the volume is
 * read-only. The VFS already refuses with EROFS; this is the second lock,
 * for any path that reaches the driver directly - and it is in the kernel,
 * so a module cannot be asked to write what the kernel has not allowed. */
static Ext2Mount *prendi_scrivibile(int mnt)
{
    Ext2Mount *m = prendi(mnt);

    if (m != NULL && (m->sola_lettura || g_ops == NULL)) {
        klog(LOG_WARN, "EXT2: write refused, the volume is read-only (%s)",
             (m->sola_lettura & EXT2_RO_MODULO) ? "no write module" :
             (m->sola_lettura & EXT2_RO_GIORNALE) ? "journal not replayed" :
             (m->sola_lettura & EXT2_RO_CHIESTO) ? "mounted read-only" :
                                                     "features not writable");
        return NULL;
    }
    return m;
}

/* =============================================================================
 * Accesso ai blocchi
 * ============================================================================= */
static int leggi_blocco(Ext2Mount *m, uint32_t n, void *dst)
{
    if (n == 0 || n >= m->n_blocchi) {
        klog(LOG_ERROR, "EXT2: blocco %u fuori dal volume (%u)", n, m->n_blocchi);
        return -1;
    }
    if (blk_read(m->blkdev, (uint64_t)n * m->sett_per_blocco,
                 m->sett_per_blocco, dst) != 0) return -1;
    if (g_ops != NULL && g_ops->giornale_leggi != NULL)
        g_ops->giornale_leggi(m, n, 1u, dst);       /* ext3: see Ext2Ops */
    return 0;
}

/* ! I DATI DI UN FILE A CORSE (0.235, @EXILLA-DISCO): n blocchi consecutivi
 * sul disco in un solo comando, e senza passare dalla cache dei settori.
 * Firefox carica i suoi 275 MB e i suoi archivi un blocco da 4 KB per
 * comando ATA, ognuno con la sua attesa: le corse ne fanno uno ogni 256 KB.
 * CORSA_MAX sta in ext2_interno.h: la scrittura a corse e' del modulo. */

static int leggi_corsa(Ext2Mount *m, uint32_t n, uint32_t quanti, void *dst)
{
    if (n == 0 || n + quanti > m->n_blocchi) {
        klog(LOG_ERROR, "EXT2: corsa %u+%u fuori dal volume (%u)", n, quanti, m->n_blocchi);
        return -1;
    }
    if (blk_read_diretto(m->blkdev, (uint64_t)n * m->sett_per_blocco,
                         quanti * m->sett_per_blocco, dst) != 0) return -1;
    if (g_ops != NULL && g_ops->giornale_leggi != NULL)
        g_ops->giornale_leggi(m, n, quanti, dst);
    return 0;
}

static int scrivi_blocco(Ext2Mount *m, uint32_t n, const void *src)
{
    if (n == 0 || n >= m->n_blocchi) {
        klog(LOG_ERROR, "EXT2: scrittura del blocco %u fuori dal volume (%u)",
             n, m->n_blocchi);
        return -1;
    }

    /* La cache dei puntatori va invalidata QUI, non nei chiamanti.
     * Metterla nei chiamanti significa che il primo che se ne dimentica
     * legge un blocco indiretto vecchio subito dopo averlo riscritto, e
     * il file finisce a puntare ai blocchi che aveva prima. */
    if (g_ind_num == n && !g_ind_sporco) g_ind_mnt = -1;

    /* ext3: a metadata block goes into the open transaction, and reaches
     * its place on the disk only after the commit (see Ext2Ops). */
    if (g_ops != NULL && g_ops->giornale_scrivi != NULL) {
        int r = g_ops->giornale_scrivi(m, n, src);
        if (r <= 0) return r;
    }
    return blk_write(m->blkdev, (uint64_t)n * m->sett_per_blocco,
                     m->sett_per_blocco, src);
}

/* `quanti` consecutive blocks from `src` in one command (a file's data, see
 * leggi_corsa). The pointer cache is dropped if its block is among them,
 * for the same reason as in scrivi_blocco. */
static int scrivi_corsa(Ext2Mount *m, uint32_t n, uint32_t quanti, const void *src)
{
    if (n == 0 || quanti == 0 || quanti > CORSA_MAX || n + quanti > m->n_blocchi) {
        klog(LOG_ERROR, "EXT2: scrittura della corsa %u+%u fuori dal volume (%u)",
             n, quanti, m->n_blocchi);
        return -1;
    }
    if (g_ind_mnt >= 0 && g_ind_num >= n && g_ind_num < n + quanti) {
        if (g_ind_sporco) {
            klog(LOG_ERROR, "EXT2: a data run over the pending pointer block %u", g_ind_num);
            return -1;
        }
        g_ind_mnt = -1;
    }
    if (g_ops != NULL && g_ops->giornale_dati != NULL &&
        g_ops->giornale_dati(m, n, quanti) != 0) return -1;
    return blk_write(m->blkdev, (uint64_t)n * m->sett_per_blocco,
                     quanti * m->sett_per_blocco, src);
}

/* Blocco di puntatori, con memoria dell'ultimo. Vedi la nota sulla cache
 * in testa al file. */
/* Mette sul disco il blocco di puntatori rimasto in memoria (vedi sopra). */
static int ind_riversa(Ext2Mount *m)
{
    if (!g_ind_sporco) return 0;
    g_ind_sporco = 0;
    if (g_ind_mnt < 0) return -1;       /* non deve succedere: lo sporco e' sempre in cache */
    if (g_ops != NULL && g_ops->giornale_scrivi != NULL) {
        int r = g_ops->giornale_scrivi(m, g_ind_num, b_ind);
        if (r <= 0) return r;
    }
    return blk_write(m->blkdev, (uint64_t)g_ind_num * m->sett_per_blocco,
                     m->sett_per_blocco, b_ind);
}

static int leggi_indiretto(Ext2Mount *m, int mnt, uint32_t n)
{
    if (g_ind_mnt == mnt && g_ind_num == n) return 0;

    if (ind_riversa(m) != 0) { g_ind_mnt = -1; return -1; }

    if (leggi_blocco(m, n, b_ind) != 0) { g_ind_mnt = -1; return -1; }

    g_ind_mnt = mnt;
    g_ind_num = n;
    return 0;
}

/* =============================================================================
 * Inode
 * ============================================================================= */

/* Copia l'inode `numero` in `dst` (dim_inode byte, ma noi ne leggiamo 128:
 * i campi oltre i primi 128 byte sono estensioni che non usiamo).
 *
 * La formula e' quella della specifica, e le due divisioni sono il punto
 * in cui e' facile scambiarsi gli operandi:
 *   gruppo  = (numero - 1) / inode_per_gruppo
 *   indice  = (numero - 1) % inode_per_gruppo
 * L'inode 1 e' il PRIMO, non lo zero: dimenticare il -1 sposta ogni inode
 * di uno e fa leggere il file sbagliato con la massima naturalezza. */
static int leggi_inode(Ext2Mount *m, uint32_t numero, uint8_t *dst)
{
    uint32_t gruppo, indice, desc_blk, desc_off, itab, blk, off, i;

    if (numero == 0 || numero > m->n_inode) return -1;

    gruppo = (numero - 1u) / m->inode_per_gruppo;
    indice = (numero - 1u) % m->inode_per_gruppo;

    if (gruppo >= m->n_gruppi) return -1;

    /* Il descrittore del gruppo: 32 byte, nella tabella dei descrittori. */
    desc_blk = m->gdt_primo + (gruppo * m->dim_desc) / m->dim_blocco;
    desc_off = (gruppo * m->dim_desc) % m->dim_blocco;

    if (leggi_blocco(m, desc_blk, b_desc) != 0) return -1;
    itab = le32(b_desc + desc_off + 8);

    blk = itab + (indice * m->dim_inode) / m->dim_blocco;
    off = (indice * m->dim_inode) % m->dim_blocco;

    if (leggi_blocco(m, blk, b_ino) != 0) return -1;

    for (i = 0; i < 128u; i++) dst[i] = b_ino[off + i];
    return 0;
}

/* Numero del blocco fisico che contiene il blocco logico `n` del file.
 *
 * Ritorna 0 se il blocco non e' allocato: e' un FILE SPARSO, e in ext2 il
 * blocco 0 non e' un blocco ma significa "qui ci sono zeri". Chi chiama
 * deve riempire di zeri, non leggere il blocco 0 — che e' il record di
 * avvio del volume. */
/* =============================================================================
 * extent_map — logical block n of a file whose i_block is an extent tree
 *
 * The tree: a 12-byte header (magic 0xF30A, entries, max, depth) followed
 * by 12-byte entries. At depth 0 the entries are LEAVES: first logical
 * block, length, physical start (48 bits). Above, they are INDEX entries:
 * first logical block and the physical block of the node below. At each
 * level the entry that matters is the last one whose first block is <= n.
 *
 * Returns 0 for a hole, for an extent marked "uninitialized" (allocated
 * but never written: it reads as zeros, length above 32768), and for
 * anything that does not look like a tree - a damaged node must not send
 * a read to a random place of the disk. A physical block above 4 G blocks
 * is refused the same way: this driver counts blocks in 32 bits.
 * ============================================================================= */
static uint32_t extent_map(Ext2Mount *m, const uint8_t *inode, uint32_t n)
{
    const uint8_t *nodo = inode + 40u;          /* the root, inside i_block */
    uint32_t       spazio = 60u;                /* bytes available to this node */
    uint32_t       livelli = 0;

    for (;;) {
        uint32_t voci, prof, i, scelta;

        if (le16(nodo) != EXTENT_MAGIC) return 0;
        voci = le16(nodo + 2);
        prof = le16(nodo + 6);
        if (voci == 0 || 12u + voci * 12u > spazio) return 0;

        /* the last entry starting at or before n (entries are sorted) */
        scelta = voci;
        for (i = 0; i < voci; i++) {
            if (le32(nodo + 12u + i * 12u) <= n) scelta = i;
            else break;
        }
        if (scelta == voci) return 0;           /* n is before the first one: a hole */

        {
            const uint8_t *e = nodo + 12u + scelta * 12u;

            if (prof == 0) {
                uint32_t primo = le32(e);
                uint32_t lung  = le16(e + 4);
                uint32_t alto  = le16(e + 6);
                uint32_t basso = le32(e + 8);

                if (lung > 32768u) return 0;    /* uninitialized: zeros */
                if (n - primo >= lung) return 0;
                if (alto != 0) return 0;        /* beyond 32-bit block numbers */
                return basso + (n - primo);
            } else {
                uint32_t foglia = le32(e + 4);
                uint32_t alto   = le16(e + 8);

                if (alto != 0 || foglia == 0) return 0;
                if (++livelli > 5u) return 0;   /* ext4 trees are at most 5 deep */
                if (leggi_blocco(m, foglia, b_ext) != 0) return 0;
                nodo   = b_ext;
                spazio = m->dim_blocco;
            }
        }
    }
}

static uint32_t mappa_blocco(Ext2Mount *m, int mnt, const uint8_t *inode,
                             uint32_t n)
{
    uint32_t ppb = m->punt_per_blocco;
    uint32_t p;

    if (le32(inode + 32u) & INODE_FL_EXTENTS) return extent_map(m, inode, n);

    /* --- 12 blocchi diretti --- */
    if (n < 12u) return le32(inode + 40u + n * 4u);
    n -= 12u;

    /* --- indiretto semplice --- */
    if (n < ppb) {
        p = le32(inode + 40u + 12u * 4u);
        if (p == 0) return 0;
        if (leggi_indiretto(m, mnt, p) != 0) return 0;
        return le32(b_ind + n * 4u);
    }
    n -= ppb;

    /* --- indiretto doppio --- */
    if (n < ppb * ppb) {
        p = le32(inode + 40u + 13u * 4u);
        if (p == 0) return 0;
        if (leggi_indiretto(m, mnt, p) != 0) return 0;
        p = le32(b_ind + (n / ppb) * 4u);
        if (p == 0) return 0;
        if (leggi_indiretto(m, mnt, p) != 0) return 0;
        return le32(b_ind + (n % ppb) * 4u);
    }
    n -= ppb * ppb;

    /* --- indiretto triplo ---
     * Serve davvero: con blocchi da 1024 il doppio indiretto si ferma a
     * ~64 MB, e /bin/mkfs formatta proprio a 1024. Un driver che si ferma
     * al doppio leggerebbe correttamente i primi 64 MB di un file e poi
     * darebbe zeri, che e' il modo peggiore di fallire. */
    p = le32(inode + 40u + 14u * 4u);
    if (p == 0) return 0;
    if (leggi_indiretto(m, mnt, p) != 0) return 0;
    p = le32(b_ind + (n / (ppb * ppb)) * 4u);
    if (p == 0) return 0;
    if (leggi_indiretto(m, mnt, p) != 0) return 0;
    p = le32(b_ind + ((n / ppb) % ppb) * 4u);
    if (p == 0) return 0;
    if (leggi_indiretto(m, mnt, p) != 0) return 0;
    return le32(b_ind + (n % ppb) * 4u);
}

/* =============================================================================
 * Percorsi
 *
 * A differenza di kernel/fs/fat12.c, che risolve un livello solo, qui i
 * percorsi sono profondi quanto si vuole: ext2 non ha la directory radice
 * a dimensione fissa che rendeva conveniente quella scorciatoia.
 * ============================================================================= */

/* Confronta `nome` (len byte, non terminato) col segmento in `seg`. */
static int nome_uguale(const uint8_t *nome, uint32_t len, const char *seg,
                       uint32_t seg_len)
{
    uint32_t i;

    if (len != seg_len) return 0;
    for (i = 0; i < len; i++) if ((char)nome[i] != seg[i]) return 0;
    return 1;
}

/* Cerca `seg` dentro la directory `inode_dir`. Ritorna il numero di inode,
 * o 0 se non c'e'. */
static uint32_t cerca_voce(Ext2Mount *m, int mnt, const uint8_t *inode_dir,
                           const char *seg, uint32_t seg_len)
{
    uint32_t dim = le32(inode_dir + 4);
    uint32_t off = 0;

    while (off < dim) {
        uint32_t blk = mappa_blocco(m, mnt, inode_dir, off / m->dim_blocco);
        uint32_t p   = 0;

        if (blk == 0) { off += m->dim_blocco; continue; }
        if (leggi_blocco(m, blk, b_dati) != 0) return 0;

        while (p < m->dim_blocco) {
            uint32_t ino = le32(b_dati + p);
            uint32_t rec = le16(b_dati + p + 4);
            uint32_t len = b_dati[p + 6];

            /* Un rec_len di zero non avanza: il ciclo girerebbe per
             * sempre su una directory corrotta. Fermarsi e' l'unica
             * risposta giusta — un kernel che si pianta leggendo una
             * directory e' peggio di uno che dice "non trovato". */
            if (rec < 8u || p + rec > m->dim_blocco) break;

            if (ino != 0 && nome_uguale(b_dati + p + 8, len, seg, seg_len))
                return ino;

            p += rec;
        }

        off += m->dim_blocco;
    }

    return 0;
}

/* Risolve un percorso assoluto interno al volume. Ritorna il numero di
 * inode e lascia l'inode in `out` (128 byte). 0 se non esiste. */
static uint32_t risolvi(Ext2Mount *m, int mnt, const char *percorso,
                        uint8_t *out)
{
    uint32_t ino = INODE_RADICE;
    uint32_t i = 0;

    if (leggi_inode(m, ino, out) != 0) return 0;
    if (percorso == NULL || percorso[0] != '/') return 0;

    i = 1;
    while (percorso[i]) {
        uint32_t inizio = i, len;

        while (percorso[i] && percorso[i] != '/') i++;
        len = i - inizio;

        if (len > 0) {
            /* Ogni segmento intermedio dev'essere una directory: senza
             * questo controllo "/file/altro" cercherebbe dentro i byte di
             * un file normale interpretandoli come voci di directory. */
            if ((le16(out) & MODE_TIPO) != MODE_DIR) return 0;

            ino = cerca_voce(m, mnt, out, percorso + inizio, len);
            if (ino == 0) return 0;
            if (leggi_inode(m, ino, out) != 0) return 0;
        }

        if (percorso[i] == '/') i++;
    }

    return ino;
}

/* =============================================================================
 * CAN THIS VOLUME BE WRITTEN? (kernel 0.263)
 *
 * Three answers, in this order, each one a bit of sola_lettura:
 *   no module registered          EXT2_RO_MODULO
 *   a feature the module does     EXT2_RO_FUNZIONI - every bit of the three
 *   not write                     sets must be among the module's: an
 *                                 unknown compat bit is safe to READ past,
 *                                 not to write past (dir_index is one)
 *   the module's monta() says no  EXT2_RO_GIORNALE - a journal it could not
 *                                 replay, a check that failed
 * ============================================================================= */
static void valuta(int mnt)
{
    Ext2Mount *m  = &g_mnt[mnt];
    uint8_t    ro = 0;

    if (m->ro_chiesto) {
        ro = EXT2_RO_CHIESTO;
    } else if (g_ops == NULL) {
        ro = EXT2_RO_MODULO;
    } else if ((m->compat & ~g_ops->compat) || (m->incompat & ~g_ops->incompat) ||
               (m->ro_compat & ~g_ops->ro_compat)) {
        ro = EXT2_RO_FUNZIONI;
        klog(LOG_WARN, "EXT2: features %s cannot write (compat 0x%x, incompat 0x%x, "
                       "ro_compat 0x%x): READ-ONLY", g_ops->nome,
             m->compat & ~g_ops->compat, m->incompat & ~g_ops->incompat,
             m->ro_compat & ~g_ops->ro_compat);
    } else if (g_ops->monta != NULL && g_ops->monta(mnt) != 0) {
        ro = EXT2_RO_GIORNALE;
        klog(LOG_WARN, "EXT2: %s refused to write volume '%s': READ-ONLY",
             g_ops->nome, m->etichetta);
    }
    m->sola_lettura = ro;
}

void vfs_ext2_rivaluta(void);

/* The write module registers here (through Ext2Interfaccia). Once: a second
 * module for the same job would leave two sets of write code with their own
 * private state on the same volumes. Every mounted volume is asked again,
 * and the VFS lifts read-only where it can. */
static int ext2_registra(const Ext2Ops *ops)
{

    if (ops == NULL || ops->abi != EXT2_IF_ABI) return -1;
    if (g_ops != NULL) {
        klog(LOG_ERROR, "EXT2: a write module (%s) is already registered", g_ops->nome);
        return -1;
    }
    if (!ops->create || !ops->chown || !ops->chmod || !ops->mkdir || !ops->rmdir ||
        !ops->unlink || !ops->rename || !ops->write || !ops->truncate || !ops->sync)
        return -1;

    g_ops = ops;
    klog(LOG_INFO, "EXT2: write module %s registered", ops->nome);

    /* ! UNDER THE VFS LOCK: monta() may read the disk (a journal), and a
     * read in progress on the same volume uses the same buffers. */
    vfs_ext2_rivaluta();
    return 0;
}

/* Every mounted volume asked again (valuta). Only from vfs_ext2_rivaluta,
 * which holds the VFS lock. */
void ext2_rivaluta_montaggi(void)
{
    int i;

    for (i = 0; i < EXT2_MAX_MOUNT; i++)
        if (g_mnt[i].usato) valuta(i);
}

/* =============================================================================
 * Montaggio
 * ============================================================================= */
int ext2_mount(int blkdev)
{
    return ext2_mount_come(blkdev, 0);
}

int ext2_mount_come(int blkdev, int ro_chiesto)
{
    Ext2Mount *m = NULL;
    uint8_t    sb[1024];
    uint32_t   log_bs, incompat, rev, i;
    int        slot = -1;

    for (i = 0; i < EXT2_MAX_MOUNT; i++)
        if (!g_mnt[i].usato) { slot = (int)i; break; }

    if (slot < 0) {
        klog(LOG_ERROR, "EXT2: nessun montaggio libero");
        return -1;
    }

    m = &g_mnt[slot];

    /* Il superblocco sta all'offset 1024 del volume, SEMPRE, qualunque sia
     * la dimensione del blocco. E' l'unica cosa che si puo' leggere prima
     * di sapere com'e' fatto il resto. */
    if (blk_read(blkdev, SUPER_OFFSET / 512u, 1024u / 512u, sb) != 0) {
        klog(LOG_ERROR, "EXT2: superblocco illeggibile");
        return -1;
    }

    if (le16(sb + 56) != EXT2_MAGIC) return -1;    /* non e' un ext2 */

    log_bs = le32(sb + 24);
    if (log_bs > 2u) {
        klog(LOG_ERROR, "EXT2: blocchi da %u byte: oltre il massimo di %u",
             1024u << log_bs, BLOCCO_MAX);
        return -1;
    }

    rev      = le32(sb + 76);
    incompat = (rev >= 1u) ? le32(sb + 96) : 0u;

    /* Vedi in testa al file: una incompat sconosciuta cambia il
     * significato di campi che crediamo di saper leggere. Rifiutare e'
     * l'unica risposta corretta, e va DETTO quale bit ha fermato tutto,
     * altrimenti l'utente vede solo "non montabile". */
    if (incompat & ~(uint32_t)INCOMPAT_LETTURA) {
        klog(LOG_ERROR, "EXT2: incompatible features 0x%x not handled: mount refused",
             incompat & ~(uint32_t)INCOMPAT_LETTURA);
        return -1;
    }

    /* 64BIT: descriptors of s_desc_size bytes, and block counts that may
     * have a high half. A volume that needs it (above 16 TB with 4 KB
     * blocks) is refused: this driver counts blocks in 32 bits. */
    m->dim_desc = 32u;
    if (incompat & INCOMPAT_64BIT) {
        uint32_t dd = le16(sb + 254);

        if (dd >= 64u && dd <= 1024u && (dd & (dd - 1u)) == 0) m->dim_desc = dd;
        if (le32(sb + 0x150) != 0) {
            klog(LOG_ERROR, "EXT2: more than 2^32 blocks: mount refused");
            return -1;
        }
    }

    m->compat    = (rev >= 1u) ? le32(sb + 92)  : 0u;
    m->incompat  = incompat;
    m->ro_compat = (rev >= 1u) ? le32(sb + 100) : 0u;

    m->blkdev             = blkdev;
    m->dim_blocco         = 1024u << log_bs;
    m->sett_per_blocco    = m->dim_blocco / 512u;
    m->punt_per_blocco    = m->dim_blocco / 4u;
    m->n_inode            = le32(sb + 0);
    m->n_blocchi          = le32(sb + 4);
    m->blocchi_liberi     = le32(sb + 12);
    m->inode_liberi       = le32(sb + 16);
    m->primo_dato         = le32(sb + 20);

    /* s_first_ino esiste solo dalla revisione 1. In revisione 0 i primi
     * inode liberi cominciano dall'11 per convenzione fissa. */
    m->primo_inode = (rev >= 1u) ? le32(sb + 84) : 11u;
    if (m->primo_inode < 11u) m->primo_inode = 11u;

    m->blocchi_per_gruppo = le32(sb + 32);
    m->inode_per_gruppo   = le32(sb + 40);

    /* In revisione 0 la dimensione dell'inode non e' scritta e vale 128
     * per definizione. Leggere s_inode_size su un volume rev 0 significa
     * leggere un campo che non esiste. */
    m->dim_inode = (rev >= 1u) ? le16(sb + 88) : 128u;

    if (m->blocchi_per_gruppo == 0 || m->inode_per_gruppo == 0 ||
        m->dim_inode < 128u || m->dim_inode > m->dim_blocco) {
        klog(LOG_ERROR, "EXT2: superblocco incoerente: montaggio rifiutato");
        return -1;
    }

    m->n_gruppi = (m->n_blocchi - m->primo_dato + m->blocchi_per_gruppo - 1u)
                / m->blocchi_per_gruppo;

    /* La tabella dei descrittori sta nel blocco SUBITO DOPO il
     * superblocco. Con blocchi da 1024 il superblocco e' il blocco 1 e la
     * tabella comincia dal 2; con blocchi piu' grandi il superblocco vive
     * dentro il blocco 0 e la tabella comincia dall'1. Cablare "2"
     * funziona solo sui volumi che ci siamo formattati da soli. */
    m->gdt_primo = m->primo_dato + 1u;

    for (i = 0; i < 16u; i++) m->etichetta[i] = (char)sb[120 + i];
    m->etichetta[16] = '\0';

    m->ro_chiesto = ro_chiesto ? 1u : 0u;
    m->usato  = 1;
    g_ind_mnt = -1;

    klog(LOG_INFO, "EXT2: montato dispositivo %d: %u blocchi da %u byte, "
                   "%u inode da %u byte, %u gruppi, '%s'",
         blkdev, m->n_blocchi, m->dim_blocco, m->n_inode, m->dim_inode,
         m->n_gruppi, m->etichetta);

    valuta(slot);
    if (m->sola_lettura && (m->incompat & INCOMPAT_RECOVER))
        klog(LOG_WARN, "EXT2: the volume was not unmounted cleanly and its "
                       "journal is not replayed: the latest changes may be missing");

    return slot;
}

/* Nonzero if the volume is read-only (EXT2_RO_* bits): the VFS mounts it
 * read-only, and asks again when the write module registers. */
int ext2_sola_lettura(int mnt)
{
    Ext2Mount *m = prendi(mnt);
    return m ? m->sola_lettura : 1;
}

int ext2_umount(int mnt)
{
    Ext2Mount *m = prendi(mnt);

    if (m == NULL) return -1;
    if (g_ops != NULL && !m->sola_lettura && g_ops->smonta != NULL) g_ops->smonta(mnt);
    m->usato = 0;
    if (g_ind_mnt == mnt) g_ind_mnt = -1;
    return 0;
}

uint32_t ext2_blocchi(int mnt)
{
    Ext2Mount *m = prendi(mnt);
    return m ? m->n_blocchi : 0;
}

uint32_t ext2_blocchi_liberi(int mnt)
{
    Ext2Mount *m = prendi(mnt);
    return m ? m->blocchi_liberi : 0;
}

uint32_t ext2_dim_blocco(int mnt)
{
    Ext2Mount *m = prendi(mnt);
    return m ? m->dim_blocco : 0;
}

uint32_t ext2_generazione(int mnt)
{
    Ext2Mount *m = prendi(mnt);
    return m ? ext_generazione(m->compat, m->incompat, m->ro_compat) : 2u;
}

const char *ext2_etichetta(int mnt)
{
    Ext2Mount *m = prendi(mnt);
    return m ? m->etichetta : "";
}

/* =============================================================================
 * Operazioni
 * ============================================================================= */
/* =============================================================================
 * Da tempo Unix a data e ora in formato FAT
 *
 * ! NON E' UNA CONVERSIONE APPROSSIMATA: l'algoritmo dei giorni civili e'
 * quello esatto, bisestili compresi (regola dei 400 anni). L'unica perdita
 * e' sui secondi, che in FAT hanno risoluzione di due — e' il formato di
 * destinazione a essere fatto cosi'.
 *
 * ! PRIMA DEL 1980 SI RESTITUISCE ZERO, che significa «non la so». Un
 * anno negativo scritto nei sette bit dell'anno diventerebbe una data del
 * futuro, cioe' una bugia invece di un'assenza.
 * ============================================================================= */
static void data_fat_da_unix(uint32_t t, uint16_t *data, uint16_t *ora)
{
    uint32_t giorni = t / 86400u;
    uint32_t resto  = t % 86400u;
    uint32_t anno, mese, giorno;
    int32_t  era, doe, yoe, doy, mp;

    *data = 0;
    *ora  = 0;
    if (t == 0) return;

    /* Giorni dal 1970-01-01 -> data civile. Si sposta l'origine al
     * 0000-03-01 perche' cosi' il 29 febbraio finisce in fondo all'anno e
     * il conto dei mesi diventa una formula invece di una tabella. */
    {
        int32_t z = (int32_t)giorni + 719468;

        era = (z >= 0 ? z : z - 146096) / 146097;
        doe = z - era * 146097;
        yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        mp  = (5 * doy + 2) / 153;

        giorno = (uint32_t)(doy - (153 * mp + 2) / 5 + 1);
        mese   = (uint32_t)(mp < 10 ? mp + 3 : mp - 9);
        anno   = (uint32_t)(yoe + era * 400 + (mese <= 2 ? 1 : 0));
    }

    if (anno < 1980u || anno > 2107u) return;

    *data = (uint16_t)(((anno - 1980u) << 9) | (mese << 5) | giorno);
    *ora  = (uint16_t)(((resto / 3600u) << 11) |
                       (((resto / 60u) % 60u) << 5) |
                       ((resto % 60u) / 2u));
}

int ext2_stat(int mnt, const char *percorso, Ext2DirEntry *out)
{
    Ext2Mount *m = prendi(mnt);
    uint8_t    inode[128];
    uint32_t   ino;

    if (m == NULL || out == NULL) return -1;

    ino = risolvi(m, mnt, percorso, inode);
    if (ino == 0) return -1;

    out->nome[0]     = '\0';
    out->inode       = ino;
    out->dimensione  = le32(inode + 4);
    out->is_dir      = ((le16(inode) & MODE_TIPO) == MODE_DIR) ? 1 : 0;
    /* i_mtime sta all'offset 16 dell'inode ed e' un tempo Unix. */
    data_fat_da_unix(le32(inode + 16), &out->data, &out->ora);

    /* ! IL PROPRIETARIO, agli offset che ext2 usa dal 1993. mkfs li scrive
     * gia': qui si smette solo di ignorarli. */
    out->modo = le16(inode + 0);
    out->uid  = le16(inode + 2);
    out->gid  = le16(inode + 24);
    return 0;
}

int ext2_readdir(int mnt, const char *percorso, Ext2DirEntry *out,
                 uint32_t max, uint32_t start)
{
    Ext2Mount *m = prendi(mnt);
    uint8_t    inode[128];
    uint32_t   ino, dim, off = 0, visti = 0, scritti = 0;

    if (m == NULL || out == NULL || max == 0) return -1;

    ino = risolvi(m, mnt, percorso, inode);
    if (ino == 0) return -1;
    if ((le16(inode) & MODE_TIPO) != MODE_DIR) return -1;

    dim = le32(inode + 4);

    while (off < dim && scritti < max) {
        uint32_t blk = mappa_blocco(m, mnt, inode, off / m->dim_blocco);
        uint32_t p   = 0;

        if (blk == 0) { off += m->dim_blocco; continue; }
        if (leggi_blocco(m, blk, b_dati) != 0) return -1;

        while (p < m->dim_blocco && scritti < max) {
            uint32_t vino = le32(b_dati + p);
            uint32_t rec  = le16(b_dati + p + 4);
            uint32_t len  = b_dati[p + 6];
            uint32_t tipo = b_dati[p + 7];

            if (rec < 8u || p + rec > m->dim_blocco) break;

            /* Le voci con inode 0 sono spazio libero dentro la directory,
             * non file cancellati da mostrare. */
            if (vino == 0) { p += rec; continue; }

            /* "." e ".." esistono sul volume ma non sono contenuto: farle
             * comparire in `ls` costringerebbe ogni chiamante a filtrarle,
             * e chi se ne dimentica ricorre all'infinito su "..". */
            if ((len == 1u && b_dati[p + 8] == '.') ||
                (len == 2u && b_dati[p + 8] == '.' && b_dati[p + 9] == '.')) {
                p += rec;
                continue;
            }

            if (visti++ < start) { p += rec; continue; }

            {
                Ext2DirEntry *o = &out[scritti++];
                uint32_t k, n = len;

                if (n > EXT2_NOME_MAX - 1u) n = EXT2_NOME_MAX - 1u;
                for (k = 0; k < n; k++) o->nome[k] = (char)b_dati[p + 8 + k];
                o->nome[n] = '\0';

                o->inode = vino;

                /* Tipo e dimensione si prendono dall'INODE, non dalla
                 * voce. Il byte file_type della voce esiste solo se il
                 * volume dichiara FILETYPE — che e' facoltativa, e un
                 * volume senza la dichiara comunque leggibile — e la
                 * dimensione nella voce non c'e' affatto. Fidarsi del
                 * byte darebbe directory scambiate per file sui volumi
                 * che quella funzionalita' non ce l'hanno. */
                {
                    uint8_t sub[128];
                    if (leggi_inode(m, vino, sub) == 0) {
                        o->is_dir     = ((le16(sub) & MODE_TIPO) == MODE_DIR);
                        o->dimensione = le32(sub + 4);
                    } else {
                        o->is_dir     = (tipo == DIRTIPO_DIR);
                        o->dimensione = 0;
                    }
                }

                /* Nessuna rilettura del blocco della directory: leggi_inode()
                 * lavora su b_ino e b_desc, e b_dati resta quello che era.
                 * E' esattamente la ragione per cui i buffer sono cinque. */
            }

            p += rec;
        }

        off += m->dim_blocco;
    }

    return (int)scritti;
}

int ext2_read(int mnt, const char *percorso, void *buf, uint32_t size,
              uint32_t offset)
{
    Ext2Mount *m = prendi(mnt);
    uint8_t    inode[128];
    uint8_t   *dst = (uint8_t *)buf;
    uint32_t   ino, dim, letti = 0;

    if (m == NULL || buf == NULL) return -1;

    ino = risolvi(m, mnt, percorso, inode);
    if (ino == 0) return -1;
    if ((le16(inode) & MODE_TIPO) != MODE_FILE) return -1;
    if (le32(inode + 32u) & INODE_FL_INLINE) {
        klog(LOG_WARN, "EXT2: '%s' keeps its data inside the inode (ext4 "
                       "inline_data), which this driver does not read", percorso);
        return -1;
    }

    dim = le32(inode + 4);
    if (le32(inode + 108u) != 0) dim = 0xFFFFFFFFu;  /* above 4 GB: the first 4 GB */
    if (offset >= dim) return 0;
    if (size > dim - offset) size = dim - offset;

    while (letti < size) {
        uint32_t log  = (offset + letti) / m->dim_blocco;
        uint32_t in   = (offset + letti) % m->dim_blocco;
        uint32_t q    = m->dim_blocco - in;
        uint32_t blk, k;

        if (q > size - letti) q = size - letti;

        blk = mappa_blocco(m, mnt, inode, log);

        if (blk == 0) {
            /* File SPARSO: il blocco non e' allocato e significa zeri.
             * Leggere il blocco 0 restituirebbe il record di avvio del
             * volume dentro il file. */
            for (k = 0; k < q; k++) dst[letti + k] = 0;
        } else if (in == 0 && q == m->dim_blocco) {
            /* Blocchi interi: quanti ne seguono contigui sul disco, e una
             * lettura sola dritta nel buffer di chi chiede. */
            uint32_t n = 1, max = (size - letti) / m->dim_blocco;

            if (max > CORSA_MAX) max = CORSA_MAX;
            while (n < max && mappa_blocco(m, mnt, inode, log + n) == blk + n) n++;
            if (leggi_corsa(m, blk, n, dst + letti) != 0) break;
            letti += n * m->dim_blocco;
            continue;
        } else {
            if (leggi_blocco(m, blk, b_dati) != 0) break;
            for (k = 0; k < q; k++) dst[letti + k] = b_dati[in + k];
        }

        letti += q;
    }

    return (int)letti;
}

/* =============================================================================
 * L'ora corrente come tempo Unix, o zero se l'orologio non risponde.
 *
 * ! QUI PRIMA NON C'ERA NIENTE, e le marche temporali restavano a zero. Il
 * commento che lo spiegava diceva «EX-OS non ha un orologio letto dal
 * kernel» — vero quando e' stato scritto, **falso da quando esiste
 * rtc_read()**, che FAT12 usa gia' per datare i propri file (vedi
 * fat12_ora_corrente in kernel/fs/fat12.c). Era rimasto indietro un
 * filesystem su due.
 *
 * ! E NON ERA UN DETTAGLIO ESTETICO. Un file senza data non e' un file
 * senza un dato accessorio: e' un file che `make` non sa CONFRONTARE. Con
 * tutti gli oggetti a zero, `bin/fbc` (zero) non risulta mai piu' vecchio
 * dei .o (zero), quindi:
 *
 *     make: Nothing to be done for 'compiler'.
 *
 * dopo aver ricompilato tutti e centoquarantacinque i sorgenti. make
 * funzionava soltanto quando il bersaglio NON ESISTEVA — cioe' era un
 * costruttore da zero, non un ricostruttore. Ed e' proprio la cosa per cui
 * make esiste.
 *
 * ! SE L'OROLOGIO NON RISPONDE SI LASCIA ZERO, come fa FAT12: su hardware
 * col CMOS scarico una data inventata sarebbe peggio di nessuna data,
 * perche' zero vuol dire «non la so» e un 1970 sembra un fatto.
 * ============================================================================= */
static uint32_t unix_ora_corrente(void)
{
    RtcTime  t;
    uint32_t a, m, g, giorni;
    int32_t  era, yoe, doy, mp, doe;

    if (rtc_read(&t) != 0) return 0;
    if (t.anno < 1970u || t.anno > 2200u) return 0;

    /* Data civile -> giorni dal 1970-01-01. E' l'inversa esatta del conto
     * in data_fat_da_unix qui sopra: stessa origine spostata al
     * 0000-03-01, cosi' il 29 febbraio cade in fondo all'anno. */
    a = t.anno;
    m = t.mese;
    g = t.giorno;
    if (m <= 2u) a--;

    era = (int32_t)(a / 400u);
    yoe = (int32_t)(a - (uint32_t)era * 400u);
    mp  = (int32_t)(m > 2u ? m - 3u : m + 9u);
    doy = (153 * mp + 2) / 5 + (int32_t)g - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    giorni = (uint32_t)(era * 146097 + doe - 719468);

    return giorni * 86400u + t.ora * 3600u + t.minuto * 60u + t.secondo;
}

/* =============================================================================
 * THE WRITE CALLS: checked here, done by the module.
 *
 * ! THE KERNEL DECIDES WHETHER A VOLUME MAY BE WRITTEN, not the module:
 * prendi_scrivibile() refuses a read-only volume before the module is
 * called at all. The module's own checks are a second lock, not the first.
 * ============================================================================= */
#define VIA_MODULO(chiamata) \
    do { if (prendi_scrivibile(mnt) == NULL) return -1; return g_ops->chiamata; } while (0)

int ext2_create(int mnt, const char *p, uint32_t uid, uint32_t gid)
{ VIA_MODULO(create(mnt, p, uid, gid)); }

int ext2_chown(int mnt, const char *p, uint32_t uid, uint32_t gid)
{ VIA_MODULO(chown(mnt, p, uid, gid)); }

int ext2_chmod(int mnt, const char *p, uint32_t modo)
{ VIA_MODULO(chmod(mnt, p, modo)); }

int ext2_mkdir(int mnt, const char *p, uint32_t uid, uint32_t gid)
{ VIA_MODULO(mkdir(mnt, p, uid, gid)); }

int ext2_rmdir(int mnt, const char *p)
{ VIA_MODULO(rmdir(mnt, p)); }

int ext2_unlink(int mnt, const char *p)
{ VIA_MODULO(unlink(mnt, p)); }

int ext2_rename(int mnt, const char *da, const char *a)
{ VIA_MODULO(rename(mnt, da, a)); }

int ext2_write(int mnt, const char *p, const void *buf, uint32_t size, uint32_t offset)
{ VIA_MODULO(write(mnt, p, buf, size, offset)); }

int ext2_truncate(int mnt, const char *p, uint32_t nuova_dim)
{ VIA_MODULO(truncate(mnt, p, nuova_dim)); }

#undef VIA_MODULO

int ext2_sync(int mnt)
{
    Ext2Mount *m = prendi(mnt);

    if (m == NULL) return -1;

    /* A read-only volume has nothing pending; a writable one may (the
     * module's journal), and the module flushes the disk itself. */
    if (g_ops != NULL && !m->sola_lettura) return g_ops->sync(mnt);
    return blk_flush(m->blkdev);
}

/* fsync's half under the VFS lock (0.285): what the module keeps goes to the
 * disk (blocks set aside for an append, the journal's open transaction -
 * the module's chiudi, i.e. its sync without the flush). Returns the block
 * device to flush, which vfs_fsync does after giving the lock back: the
 * disk emptying its cache is the slow part, and no file operation of any
 * other thread needs to wait for it. -1 for a mount that is not there. */
int ext2_fsync_prepara(int mnt)
{
    Ext2Mount *m = prendi(mnt);

    if (m == NULL) return -1;
    if (g_ops != NULL && !m->sola_lettura && g_ops->chiudi != NULL)
        g_ops->chiudi(mnt);
    return m->blkdev;
}

void ext2_chiudi(int mnt)
{
    Ext2Mount *m = prendi(mnt);

    if (m != NULL && g_ops != NULL && !m->sola_lettura && g_ops->chiudi != NULL)
        g_ops->chiudi(mnt);
}

/* =============================================================================
 * The interface the write module asks for by name ("ext2"): see
 * kernel/include/ext2_interno.h and kernel/loader/modulo.c.
 * ============================================================================= */
static const Ext2Interfaccia g_interfaccia = {
    EXT2_IF_ABI,
    sizeof(Ext2Mount),
    sizeof(Ext2Stato),
    EXT2_MAX_MOUNT,
    g_mnt,
    &g_st,
    leggi_blocco,
    scrivi_blocco,
    scrivi_corsa,
    leggi_indiretto,
    ind_riversa,
    leggi_inode,
    mappa_blocco,
    cerca_voce,
    risolvi,
    blk_read,
    blk_write,
    blk_flush,
    unix_ora_corrente,
    ext2_registra,
};

const Ext2Interfaccia *ext2_interfaccia(void)
{
    return &g_interfaccia;
}

/* =============================================================================
 * La mappa dei settori di un file. Il contratto e il perche' di una LISTA
 * stanno in kernel/include/ext2.h.
 * ============================================================================= */
int ext2_estensioni(int mnt, const char *percorso, uint32_t *lba,
                    uint32_t *cnt, uint32_t max, uint32_t *n_out)
{
    Ext2Mount *m = prendi(mnt);
    uint8_t    inode[128];
    uint32_t   num, dim, n_blocchi, sett_totali, spb, i, usati = 0, emessi = 0;

    if (m == NULL || lba == NULL || cnt == NULL || n_out == NULL) return -1;
    if (max == 0) return -1;

    *n_out = 0;

    num = risolvi(m, mnt, percorso, inode);
    if (num == 0) return -1;
    if ((le16(inode) & MODE_TIPO) != MODE_FILE) return -1;

    dim = le32(inode + 4);
    if (dim == 0) return -1;

    spb         = m->dim_blocco / 512u;
    n_blocchi   = (dim + m->dim_blocco - 1u) / m->dim_blocco;
    sett_totali = (dim + 511u) / 512u;

    for (i = 0; i < n_blocchi; i++) {
        uint32_t b = mappa_blocco(m, mnt, inode, i);
        uint32_t q;

        if (b == 0) {
            klog(LOG_ERROR, "EXT2: '%s' e' sparso: chi legge settori non sa "
                            "che nel buco devono esserci zeri", percorso);
            return -1;
        }

        /* L'ultimo blocco si tronca alla dimensione vera del file: leggere
         * fino in fondo al blocco tirerebbe in memoria i byte che stanno
         * oltre, che appartengono a un altro file. */
        q = spb;
        if (emessi + q > sett_totali) q = sett_totali - emessi;
        if (q == 0) break;

        /* Consecutivo al precedente: si allunga l'intervallo invece di
         * aprirne uno nuovo. E' cio' che fa stare un kernel intero in due
         * voci e non in centoquarantacinque. */
        if (usati > 0 && b * spb == lba[usati - 1u] + cnt[usati - 1u]) {
            cnt[usati - 1u] += q;
            emessi += q;
            continue;
        }

        if (usati >= max) {
            klog(LOG_ERROR, "EXT2: '%s' e' spezzato in piu' di %u intervalli",
                 percorso, max);
            return -2;
        }

        lba[usati] = b * spb;
        cnt[usati] = q;
        usati++;
        emessi += q;
    }

    *n_out = usati;
    return 0;
}

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
#include "blk.h"

#define EXT2_MAGIC          0xEF53u
#define SUPER_OFFSET        1024u   /* il superblocco sta SEMPRE qui */
#define INODE_RADICE        2u
#define BLOCCO_MAX          4096u   /* la piu' grande che accettiamo */

/* Funzionalita' incompatibili che sappiamo gestire: solo FILETYPE, che si
 * limita ad aggiungere un byte gia' presente nella voce di directory.
 * Qualunque altro bit acceso e' un volume che non sappiamo leggere. */
#define INCOMPAT_FILETYPE   0x0002u
#define INCOMPAT_NOTI       INCOMPAT_FILETYPE

/* i_mode: i quattro bit alti sono il tipo */
#define MODE_TIPO           0xF000u
#define MODE_DIR            0x4000u
#define MODE_FILE           0x8000u

/* file_type nelle voci di directory, quando FILETYPE e' attiva */
#define DIRTIPO_DIR         2u

typedef struct {
    uint8_t  usato;
    int      blkdev;

    uint32_t dim_blocco;
    uint32_t sett_per_blocco;
    uint32_t primo_dato;
    uint32_t blocchi_per_gruppo;
    uint32_t inode_per_gruppo;
    uint32_t dim_inode;
    uint32_t n_gruppi;
    uint32_t n_blocchi;
    uint32_t n_inode;
    uint32_t blocchi_liberi;
    uint32_t punt_per_blocco;      /* dim_blocco / 4 */
    uint32_t gdt_primo;            /* primo blocco della tabella descrittori */

    char     etichetta[17];
} Ext2Mount;

static Ext2Mount g_mnt[EXT2_MAX_MOUNT];

/* Buffer di lavoro. Sono globali e non per montaggio perche' le syscall di
 * EX-OS non si annidano: una lettura finisce prima che cominci la
 * successiva. Se un giorno il kernel diventasse rientrante andrebbero
 * spostati dentro Ext2Mount, ed e' scritto qui perche' quel giorno sia una
 * modifica e non una caccia. */
static uint8_t  g_buf[BLOCCO_MAX];      /* blocco generico */
static uint8_t  g_ind[BLOCCO_MAX];      /* blocco di puntatori */
static uint32_t g_ind_num  = 0;         /* quale blocco c'e' dentro g_ind */
static int      g_ind_mnt  = -1;

/* =============================================================================
 * Lettura little-endian non allineata
 * ============================================================================= */
static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static Ext2Mount *prendi(int mnt)
{
    if (mnt < 0 || mnt >= EXT2_MAX_MOUNT) return NULL;
    if (!g_mnt[mnt].usato) return NULL;
    return &g_mnt[mnt];
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
    return blk_read(m->blkdev, (uint64_t)n * m->sett_per_blocco,
                    m->sett_per_blocco, dst);
}

/* Blocco di puntatori, con memoria dell'ultimo. Vedi la nota sulla cache
 * in testa al file: e' l'unica, e non puo' dare dati stantii perche' il
 * volume non si scrive. */
static int leggi_indiretto(Ext2Mount *m, int mnt, uint32_t n)
{
    if (g_ind_mnt == mnt && g_ind_num == n) return 0;

    if (leggi_blocco(m, n, g_ind) != 0) { g_ind_mnt = -1; return -1; }

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
    desc_blk = m->gdt_primo + (gruppo * 32u) / m->dim_blocco;
    desc_off = (gruppo * 32u) % m->dim_blocco;

    if (leggi_blocco(m, desc_blk, g_buf) != 0) return -1;
    itab = le32(g_buf + desc_off + 8);

    blk = itab + (indice * m->dim_inode) / m->dim_blocco;
    off = (indice * m->dim_inode) % m->dim_blocco;

    if (leggi_blocco(m, blk, g_buf) != 0) return -1;

    for (i = 0; i < 128u; i++) dst[i] = g_buf[off + i];
    return 0;
}

/* Numero del blocco fisico che contiene il blocco logico `n` del file.
 *
 * Ritorna 0 se il blocco non e' allocato: e' un FILE SPARSO, e in ext2 il
 * blocco 0 non e' un blocco ma significa "qui ci sono zeri". Chi chiama
 * deve riempire di zeri, non leggere il blocco 0 — che e' il record di
 * avvio del volume. */
static uint32_t mappa_blocco(Ext2Mount *m, int mnt, const uint8_t *inode,
                             uint32_t n)
{
    uint32_t ppb = m->punt_per_blocco;
    uint32_t p;

    /* --- 12 blocchi diretti --- */
    if (n < 12u) return le32(inode + 40u + n * 4u);
    n -= 12u;

    /* --- indiretto semplice --- */
    if (n < ppb) {
        p = le32(inode + 40u + 12u * 4u);
        if (p == 0) return 0;
        if (leggi_indiretto(m, mnt, p) != 0) return 0;
        return le32(g_ind + n * 4u);
    }
    n -= ppb;

    /* --- indiretto doppio --- */
    if (n < ppb * ppb) {
        p = le32(inode + 40u + 13u * 4u);
        if (p == 0) return 0;
        if (leggi_indiretto(m, mnt, p) != 0) return 0;
        p = le32(g_ind + (n / ppb) * 4u);
        if (p == 0) return 0;
        if (leggi_indiretto(m, mnt, p) != 0) return 0;
        return le32(g_ind + (n % ppb) * 4u);
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
    p = le32(g_ind + (n / (ppb * ppb)) * 4u);
    if (p == 0) return 0;
    if (leggi_indiretto(m, mnt, p) != 0) return 0;
    p = le32(g_ind + ((n / ppb) % ppb) * 4u);
    if (p == 0) return 0;
    if (leggi_indiretto(m, mnt, p) != 0) return 0;
    return le32(g_ind + (n % ppb) * 4u);
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
        if (leggi_blocco(m, blk, g_buf) != 0) return 0;

        while (p < m->dim_blocco) {
            uint32_t ino = le32(g_buf + p);
            uint32_t rec = le16(g_buf + p + 4);
            uint32_t len = g_buf[p + 6];

            /* Un rec_len di zero non avanza: il ciclo girerebbe per
             * sempre su una directory corrotta. Fermarsi e' l'unica
             * risposta giusta — un kernel che si pianta leggendo una
             * directory e' peggio di uno che dice "non trovato". */
            if (rec < 8u || p + rec > m->dim_blocco) break;

            if (ino != 0 && nome_uguale(g_buf + p + 8, len, seg, seg_len))
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
 * Montaggio
 * ============================================================================= */
int ext2_mount(int blkdev)
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
    if (incompat & ~(uint32_t)INCOMPAT_NOTI) {
        klog(LOG_ERROR, "EXT2: funzionalita' incompatibili 0x%x non gestite "
                        "(ext3/ext4?): montaggio rifiutato",
             incompat & ~(uint32_t)INCOMPAT_NOTI);
        return -1;
    }

    m->blkdev             = blkdev;
    m->dim_blocco         = 1024u << log_bs;
    m->sett_per_blocco    = m->dim_blocco / 512u;
    m->punt_per_blocco    = m->dim_blocco / 4u;
    m->n_inode            = le32(sb + 0);
    m->n_blocchi          = le32(sb + 4);
    m->blocchi_liberi     = le32(sb + 12);
    m->primo_dato         = le32(sb + 20);
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

    m->usato  = 1;
    g_ind_mnt = -1;

    klog(LOG_INFO, "EXT2: montato dispositivo %d: %u blocchi da %u byte, "
                   "%u inode da %u byte, %u gruppi, '%s'",
         blkdev, m->n_blocchi, m->dim_blocco, m->n_inode, m->dim_inode,
         m->n_gruppi, m->etichetta);

    return slot;
}

int ext2_umount(int mnt)
{
    Ext2Mount *m = prendi(mnt);

    if (m == NULL) return -1;
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

const char *ext2_etichetta(int mnt)
{
    Ext2Mount *m = prendi(mnt);
    return m ? m->etichetta : "";
}

/* =============================================================================
 * Operazioni
 * ============================================================================= */
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
        if (leggi_blocco(m, blk, g_buf) != 0) return -1;

        while (p < m->dim_blocco && scritti < max) {
            uint32_t vino = le32(g_buf + p);
            uint32_t rec  = le16(g_buf + p + 4);
            uint32_t len  = g_buf[p + 6];
            uint32_t tipo = g_buf[p + 7];

            if (rec < 8u || p + rec > m->dim_blocco) break;

            /* Le voci con inode 0 sono spazio libero dentro la directory,
             * non file cancellati da mostrare. */
            if (vino == 0) { p += rec; continue; }

            /* "." e ".." esistono sul volume ma non sono contenuto: farle
             * comparire in `ls` costringerebbe ogni chiamante a filtrarle,
             * e chi se ne dimentica ricorre all'infinito su "..". */
            if ((len == 1u && g_buf[p + 8] == '.') ||
                (len == 2u && g_buf[p + 8] == '.' && g_buf[p + 9] == '.')) {
                p += rec;
                continue;
            }

            if (visti++ < start) { p += rec; continue; }

            {
                Ext2DirEntry *o = &out[scritti++];
                uint32_t k, n = len;

                if (n > EXT2_NOME_MAX - 1u) n = EXT2_NOME_MAX - 1u;
                for (k = 0; k < n; k++) o->nome[k] = (char)g_buf[p + 8 + k];
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

                /* leggi_inode() ha riusato g_buf: il blocco della
                 * directory non c'e' piu'. Va riletto prima di guardare
                 * la voce successiva, altrimenti si scorrono i byte di
                 * una tabella di inode credendo che siano voci. */
                if (leggi_blocco(m, blk, g_buf) != 0) return (int)scritti;
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

    dim = le32(inode + 4);
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
        } else {
            if (leggi_blocco(m, blk, g_buf) != 0) break;
            for (k = 0; k < q; k++) dst[letti + k] = g_buf[in + k];
        }

        letti += q;
    }

    return (int)letti;
}

/* =============================================================================
 * kernel/moduli/ext4/scrittura.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Writing ext2: allocation and freeing of blocks and inodes, directory
 * entries, and the operations (create, mkdir, unlink, rename, rmdir, write,
 * truncate, chown, chmod). Until kernel 0.262 this was the second half of
 * kernel/fs/ext2.c; it moved here, almost line by line, when writing
 * became a module (/dev/ext4.mod): the comments below that say "questo
 * file" or "in testa al file" mean that file, and the read side - block
 * access, inodes, paths, the extent tree - is still there, reached through
 * the kernel's interface (ext4.h).
 * ============================================================================= */

#include "ext4.h"

/* The mount, for a write: the kernel has already checked (ext2.c); a module
 * that is asked to write a volume the kernel did not open for writing is
 * being called by mistake, and says so. */
Ext2Mount *prendi_scrivibile(int mnt)
{
    Ext2Mount *m;

    if (mnt < 0 || (uint32_t)mnt >= K->max_mount) return NULL;
    m = &K->mnt[mnt];
    if (!m->usato) return NULL;
    if (m->sola_lettura) {
        klog(LOG_WARN, "EXT2: write refused, the volume is read-only");
        return NULL;
    }
    return m;
}

/* Confronta `nome` (len byte, non terminato) col segmento in `seg`. La
 * lettura ne ha una uguale in kernel/fs/ext2.c. */
static int nome_uguale(const uint8_t *nome, uint32_t len, const char *seg,
                       uint32_t seg_len)
{
    uint32_t i;

    if (len != seg_len) return 0;
    for (i = 0; i < len; i++) if ((char)nome[i] != seg[i]) return 0;
    return 1;
}

/* Il blocco che serve alle bitmap: e' solo della scrittura. */
static uint8_t  b_bmp [BLOCCO_MAX];     /* bitmap di blocchi o di inode */

/* =============================================================================
 * i_dtime su un inode appena liberato — e perche' non puo' essere 1
 *
 * Su un inode LIBERATO ext2 riusa i_dtime come puntatore al prossimo
 * elemento della catena degli "orfani", cioe' come NUMERO DI INODE. Ne
 * discende una regola che la specifica non enuncia da nessuna parte:
 *
 *     i_dtime di un inode liberato deve essere >= s_inodes_count,
 *
 * altrimenti e2fsck lo legge come un puntatore e conclude che la catena
 * degli orfani e' corrotta. Con i_dtime = 1 — che sembra il valore piu'
 * innocuo del mondo — ogni file cancellato produce una segnalazione.
 *
 * EX-OS non ha un orologio letto dal kernel, quindi non puo' scriverci il
 * momento della cancellazione. Si usa una data fissa e dichiaratamente
 * convenzionale, abbastanza grande da non poter essere scambiata per un
 * numero di inode. La guardia sotto la rende esplicita invece di
 * affidarla al fatto che nessuno formattera' mai un volume con un miliardo
 * di inode.
 * ============================================================================= */
#define DTIME_CANCELLATO    0x40000000u     /* gennaio 2004, convenzionale */

static uint32_t dtime_di(uint32_t n_inode)
{
    return (DTIME_CANCELLATO > n_inode) ? DTIME_CANCELLATO : (n_inode + 1u);
}

/* =============================================================================
 * SCRIVERE COSTAVA NOVE COMANDI DI DISCO PER KILOBYTE (9 ottobre 2026)
 *
 * L'utente, dopo che la rete e' diventata veloce: «la scrittura di cio' che
 * arriva risulta lenta». Contati qui dentro, per ogni blocco da 1 KB aggiunto
 * a un file: leggi il descrittore, leggi la bitmap, scrivi la bitmap, rileggi
 * e riscrivi il descrittore, scrivi il blocco nuovo AZZERATO, leggi e scrivi
 * il blocco dei puntatori - e poi il dato. Su un disco in PIO, con la testina
 * che va dalla bitmap ai dati e ritorno, sono millisecondi a kilobyte.
 *
 * Tre cose, e tutte e tre FINISCONO DENTRO LA STESSA ext2_write: quando la
 * chiamata torna il disco e' coerente come prima, e non c'e' niente in
 * memoria che un'altra operazione debba conoscere.
 *
 *   1. I BLOCCHI SI PRENDONO INSIEME (prenota): chi scrive 64 KB in coda a
 *      un file marca 64 bit della bitmap in una scrittura sola e aggiorna il
 *      descrittore una volta. alloca_blocco li consegna da li'. Quel che
 *      avanza si rilascia prima di tornare - tranne per chi scrive IN CODA,
 *      vedi «LA PRENOTAZIONE RESTA» qui sotto (0.002 del modulo).
 *   2. UN BLOCCO DI DATI NUOVO NON SI AZZERA SUL DISCO se sta per essere
 *      scritto: lo si azzerava e subito dopo lo si copriva. I blocchi di
 *      PUNTATORI si azzerano sempre - i loro byte si leggono come indirizzi -
 *      e un blocco scritto solo in parte ha il resto azzerato in memoria.
 *   3. IL BLOCCO DEI PUNTATORI SI SCRIVE UNA VOLTA, non a ogni puntatore
 *      aggiunto: resta in b_ind «sporco» e va sul disco quando se ne carica
 *      un altro, e comunque prima che ext2_write torni.
 * ============================================================================= */
static uint32_t g_pre_primo = 0;        /* i blocchi prenotati: il prossimo... */
static uint32_t g_pre_n     = 0;        /* ...e quanti ne restano */
static int      g_zero      = 1;        /* 0: il blocco nuovo non si azzera (dati) */
static int      g_ind_diff  = 0;        /* 1: dentro ext2_write, b_ind si scrive dopo */

/* =============================================================================
 * LA PRENOTAZIONE RESTA FRA UNA write() E L'ALTRA (modulo 0.002, @DISCO-VELOCE)
 *
 * Chi copia un file a pezzi da 4 KB faceva, per ogni pezzo, cinque comandi:
 * bitmap, descrittore, dato, inode, superblocco - 5,5 MB/s in QEMU contro i
 * 20 a pezzi da 64 KB. Adesso:
 *
 *   - una write() IN CODA al file prenota 128 KB di blocchi (al massimo
 *     CORSA_MAX), e quelli che non usa restano suoi per la write() dopo, se
 *     e' dello stesso file: bitmap e descrittore si scrivono una volta ogni
 *     32 blocchi;
 *   - i contatori del superblocco si scrivono alla chiusura del file, al
 *     sync, allo smontaggio e alla fine di ogni altra operazione - sono un
 *     riassunto dei descrittori, che si scrivono subito.
 *
 * ! I BLOCCHI PRENOTATI SONO GIA' OCCUPATI SUL DISCO (bitmap e descrittore):
 * nessuna allocazione li puo' dare a un altro, neanche dopo uno spegnimento.
 * Tornano liberi alla chiusura del file, a qualunque altra operazione su
 * qualunque volume (prenotazione_chiudi, chiamata da ext4.c prima di ognuna),
 * a una write() su un altro file, al sync e allo smontaggio. Se la macchina
 * si spegne in mezzo, il peggio sono quei blocchi segnati occupati e di
 * nessuno: e2fsck li ritrova ("block bitmap differences"), e nessun file
 * ne e' toccato - lo stesso che lascia ext2 su Linux.
 * ============================================================================= */
#define ANTICIPO_BYTE   131072u

static int      g_pre_mnt   = -1;       /* di chi sono i prenotati... */
static uint32_t g_pre_ino   = 0;        /* ...montaggio e inode */
static uint8_t  g_super_sporco[EXT2_MAX_MOUNT];

/* =============================================================================
 * ALLOCAZIONE
 *
 * Ogni allocazione e ogni liberazione tocca TRE posti che devono restare
 * d'accordo fra loro:
 *
 *   1. il bit nella bitmap del gruppo;
 *   2. il contatore dei liberi nel descrittore di quel gruppo;
 *   3. il contatore dei liberi nel superblocco.
 *
 * Se uno dei tre resta indietro il volume non e' rotto — i dati ci sono
 * tutti — ma e2fsck lo segnala, e soprattutto l'allocatore comincia a
 * mentire: un contatore piu' alto del vero fa credere che ci sia spazio
 * che non c'e'. Per questo alloca/libera li aggiornano tutti e tre, e non
 * esiste una scorciatoia che ne tocchi solo uno.
 *
 * ! ext2 NON ha un giornale, e questo driver non ne inventa uno. Fra la
 * scrittura della bitmap e quella del superblocco c'e' una finestra in cui
 * un'interruzione lascia i contatori indietro. E' la stessa finestra di
 * ext2 su Linux, e la risposta e' la stessa: e2fsck. Cio' che NON puo'
 * succedere e' che un blocco risulti libero mentre e' in uso, perche' la
 * bitmap si scrive PRIMA di consegnare il blocco a chi lo ha chiesto.
 * ============================================================================= */

/* Legge il descrittore del gruppo `g` in b_desc e ritorna l'offset della
 * sua voce dentro il buffer, oppure -1. */
static int desc_carica(Ext2Mount *m, uint32_t g, uint32_t *off_out,
                       uint32_t *blk_out)
{
    uint32_t blk = m->gdt_primo + (g * m->dim_desc) / m->dim_blocco;
    uint32_t off = (g * m->dim_desc) % m->dim_blocco;

    if (g >= m->n_gruppi) return -1;
    if (leggi_blocco(m, blk, b_desc) != 0) return -1;

    *off_out = off;
    *blk_out = blk;
    return 0;
}

/* Quanti blocchi ha davvero il gruppo `g`: l'ultimo puo' essere corto. */
static uint32_t blocchi_nel_gruppo(Ext2Mount *m, uint32_t g)
{
    uint32_t primo = m->primo_dato + g * m->blocchi_per_gruppo;
    uint32_t resto;

    if (primo >= m->n_blocchi) return 0;
    resto = m->n_blocchi - primo;
    return (resto > m->blocchi_per_gruppo) ? m->blocchi_per_gruppo : resto;
}

/* A counter of the descriptor: 16 bits, 32 with the 64-byte descriptors of
 * 64bit (the high half at `hi`). */
static uint32_t desc_campo(Ext2Mount *m, const uint8_t *d, uint32_t lo, uint32_t hi)
{
    uint32_t v = le16(d + lo);

    if (m->dim_desc >= 64u) v |= (uint32_t)le16(d + hi) << 16;
    return v;
}

static void desc_metti(Ext2Mount *m, uint8_t *d, uint32_t lo, uint32_t hi, uint32_t v)
{
    p16(d + lo, v & 0xFFFFu);
    if (m->dim_desc >= 64u) p16(d + hi, v >> 16);
}

#define BG_INODE_UNINIT     0x0001u     /* bg_flags: the inode bitmap was never written */
#define BG_BLOCK_UNINIT     0x0002u     /* bg_flags: the block bitmap was never written */

/* Somma i tre delta (con segno) ai contatori del gruppo `g` - blocchi
 * liberi, inode liberi, directory - e lo riscrive. Con ext4 anche:
 *   bmp_blocchi / bmp_inode  la bitmap appena scritta: il suo checksum va
 *                            nel descrittore, e il gruppo non e' piu' UNINIT;
 *   inode_usato              l'indice+1 dell'inode appena preso: la parte
 *                            della tabella mai usata (bg_itable_unused) si
 *                            accorcia se lo comprende;
 * e il checksum del descrittore stesso. */
static int desc_aggiorna(Ext2Mount *m, uint32_t g, int d_blocchi, int d_inode, int d_dir,
                         const uint8_t *bmp_blocchi, const uint8_t *bmp_inode,
                         uint32_t inode_usato)
{
    static const uint32_t lo[3] = { 12u, 14u, 16u }, hi[3] = { 0x2Cu, 0x2Eu, 0x30u };
    Ext4Vol  *vol = vol_di(m);
    int       delta[3];
    uint32_t  off, blk, i, v;
    uint8_t  *d;

    delta[0] = d_blocchi; delta[1] = d_inode; delta[2] = d_dir;
    if (desc_carica(m, g, &off, &blk) != 0) return -1;
    d = b_desc + off;

    for (i = 0; i < 3u; i++) {
        if (delta[i] == 0) continue;
        v = desc_campo(m, d, lo[i], hi[i]);
        if (delta[i] < 0 && v < (uint32_t)(-delta[i])) {
            klog(LOG_ERROR, "EXT2: contatore del gruppo %u sotto zero: volume "
                            "incoerente, operazione annullata", g);
            return -1;
        }
        desc_metti(m, d, lo[i], hi[i], (uint32_t)((int)v + delta[i]));
    }
    if (bmp_blocchi) {
        p16(d + 0x12, le16(d + 0x12) & ~BG_BLOCK_UNINIT);
        somma_bitmap(m, d, 0, bmp_blocchi);
    }
    if (bmp_inode) {
        p16(d + 0x12, le16(d + 0x12) & ~BG_INODE_UNINIT);
        somma_bitmap(m, d, 1, bmp_inode);
    }
    if (inode_usato && vol && (vol->meta_csum || vol->gdt_csum)) {
        v = desc_campo(m, d, 0x1Cu, 0x32u);
        if (v > m->inode_per_gruppo) v = m->inode_per_gruppo;
        if (m->inode_per_gruppo - v < inode_usato)
            desc_metti(m, d, 0x1Cu, 0x32u, m->inode_per_gruppo - inode_usato);
    }
    somma_desc(m, g, d);
    return scrivi_blocco(m, blk, b_desc);
}

/* =============================================================================
 * I GRUPPI MAI TOCCATI (ext4: uninit_bg, metadata_csum)
 *
 * mke2fs lascia BLOCK_UNINIT e INODE_UNINIT sui gruppi vuoti e non scrive le
 * loro bitmap: sul disco li' c'e' quel che c'era. Chi alloca per primo in un
 * gruppo cosi' ne COSTRUISCE la bitmap come fa Linux (ext4_init_block_bitmap,
 * ext4_init_inode_bitmap): occupati il superblocco di riserva e i descrittori
 * se il gruppo ne ha una copia, le bitmap e la tabella degli inode del gruppo
 * se stanno dentro di lui (con flex_bg stanno di solito altrove), e i bit
 * oltre la fine del gruppo. Scritta la bitmap, desc_aggiorna toglie il flag.
 * ============================================================================= */
static void bit_segna(uint8_t *b, uint32_t i)
{
    b[i / 8u] |= (uint8_t)(1u << (i % 8u));
}

static int ha_superblocco(Ext2Mount *m, uint32_t g)
{
    Ext4Vol *v = vol_di(m);
    uint32_t base;

    if (g == 0) return 1;
    if (m->compat & COMPAT_SPARSE_SUPER2) return v && (g == v->backup[0] || g == v->backup[1]);
    if (!(m->ro_compat & RO_COMPAT_SPARSE) || g == 1u) return 1;
    for (base = 3u; base <= 7u; base += 2u) {
        uint64_t x = base;

        while (x < g) x *= base;
        if (x == g) return 1;
    }
    return 0;
}

static int con_flag(Ext2Mount *m)
{
    Ext4Vol *v = vol_di(m);

    return v && (v->meta_csum || v->gdt_csum);
}

/* La bitmap dei blocchi del gruppo `g` in `b`, e il suo blocco in *bmp. */
static int bitmap_blocchi(Ext2Mount *m, uint32_t g, uint8_t *b, uint32_t *bmp)
{
    uint32_t off, dblk, inizio, i, k, x, tab;
    const uint8_t *d;

    if (desc_carica(m, g, &off, &dblk) != 0) return -1;
    d = b_desc + off;
    *bmp = le32(d + 0);
    if (m->dim_desc >= 64u && (le32(d + 0x20) | le32(d + 0x24) | le32(d + 0x28)) != 0)
        return -1;                              /* beyond 2^32: not this driver */
    if (!(le16(d + 0x12) & BG_BLOCK_UNINIT) || !con_flag(m)) return leggi_blocco(m, *bmp, b);

    memset(b, 0, m->dim_blocco);
    inizio = m->primo_dato + g * m->blocchi_per_gruppo;
    if (ha_superblocco(m, g)) {
        Ext4Vol *v = vol_di(m);
        uint32_t n = 1u + (m->n_gruppi * m->dim_desc + m->dim_blocco - 1u) / m->dim_blocco +
                     (v ? v->gdt_riservati : 0u);

        for (i = 0; i < n && i < m->blocchi_per_gruppo; i++) bit_segna(b, i);
    }
    tab = (m->inode_per_gruppo * m->dim_inode + m->dim_blocco - 1u) / m->dim_blocco;
    for (k = 0; k < 2u; k++) {
        x = le32(d + k * 4u);
        if (x >= inizio && x - inizio < m->blocchi_per_gruppo) bit_segna(b, x - inizio);
    }
    x = le32(d + 8);
    for (k = 0; k < tab; k++)
        if (x + k >= inizio && x + k - inizio < m->blocchi_per_gruppo) bit_segna(b, x + k - inizio);
    for (i = blocchi_nel_gruppo(m, g); i < m->dim_blocco * 8u; i++) bit_segna(b, i);
    return 0;
}

/* La bitmap degli inode del gruppo `g` in `b`, e il suo blocco in *bmp. */
static int bitmap_inode(Ext2Mount *m, uint32_t g, uint8_t *b, uint32_t *bmp)
{
    uint32_t off, dblk, i;
    const uint8_t *d;

    if (desc_carica(m, g, &off, &dblk) != 0) return -1;
    d = b_desc + off;
    *bmp = le32(d + 4);
    if (!(le16(d + 0x12) & BG_INODE_UNINIT) || !con_flag(m)) return leggi_blocco(m, *bmp, b);

    memset(b, 0, m->dim_blocco);
    for (i = m->inode_per_gruppo; i < m->dim_blocco * 8u; i++) bit_segna(b, i);
    return 0;
}

/* Riscrive i due contatori del superblocco. Su ext3/ext4 il superblocco
 * passa dal giornale come ogni altro metadato (super_leggi/super_scrivi in
 * giornale.c, modulo 0.004), col suo checksum se il volume ne ha. */
static int super_aggiorna(Ext2Mount *m)
{
    uint8_t sb[1024];

    if (super_leggi(m, sb) != 0) return -1;
    p32(sb + 12, m->blocchi_liberi);
    p32(sb + 16, m->inode_liberi);
    somma_super(m, sb);
    return super_scrivi(m, sb);
}

/* Primo bit a zero in b_bmp fra 0 e `limite`. `limite` e' esclusivo.
 * Ritorna l'indice, o `limite` se non ce ne sono. */
static uint32_t primo_libero(uint32_t limite)
{
    uint32_t i;

    for (i = 0; i < limite; i++) {
        /* Un byte pieno si salta in blocco: su una bitmap da 8192 bit quasi
         * tutta occupata la differenza e' fra 8192 controlli e 1024. */
        if ((i % 8u) == 0 && b_bmp[i / 8u] == 0xFF) { i += 7u; continue; }
        if ((b_bmp[i / 8u] & (1u << (i % 8u))) == 0) return i;
    }
    return limite;
}

/* Alloca un blocco, preferendo il gruppo `pref`. Ritorna il numero del
 * blocco, o 0 se il volume e' pieno.
 *
 * Il blocco torna GIA' marcato occupato sulla bitmap: chi lo riceve non
 * deve ricordarsi di farlo, e non esiste un istante in cui il blocco e'
 * stato consegnato ma risulta ancora libero a un'altra allocazione. */
/* Prende fino a `quanti` blocchi liberi CONSECUTIVI, in una scrittura della
 * bitmap e un aggiornamento del descrittore. Restano in g_pre_primo/g_pre_n:
 * li consegna alloca_blocco. Puo' prenderne meno, o nessuno: allora si
 * prosegue un blocco per volta, come sempre. */
static void prenota(Ext2Mount *m, uint32_t pref, uint32_t quanti)
{
    uint32_t giro, g, off, dblk, bmp, lim, i, k;

    g_pre_n = 0;
    if (quanti < 2u) return;
    if (pref >= m->n_gruppi) pref = 0;

    for (giro = 0; giro < m->n_gruppi; giro++) {
        g = (pref + giro) % m->n_gruppi;

        if (desc_carica(m, g, &off, &dblk) != 0) return;
        if (desc_campo(m, b_desc + off, 12u, 0x2Cu) == 0) continue;
        if (bitmap_blocchi(m, g, b_bmp, &bmp) != 0) return;

        lim = blocchi_nel_gruppo(m, g);
        i   = primo_libero(lim);
        if (i >= lim) continue;

        for (k = 0; k < quanti && i + k < lim &&
                    (b_bmp[(i + k) / 8u] & (1u << ((i + k) % 8u))) == 0; k++)
            b_bmp[(i + k) / 8u] |= (uint8_t)(1u << ((i + k) % 8u));

        if (scrivi_blocco(m, bmp, b_bmp) != 0) return;
        if (desc_aggiorna(m, g, -(int)k, 0, 0, b_bmp, NULL, 0) != 0) return;
        m->blocchi_liberi -= k;

        g_pre_primo = m->primo_dato + g * m->blocchi_per_gruppo + i;
        g_pre_n     = k;
        return;
    }
}


/* I prenotati che nessuno ha usato tornano liberi. Sono consecutivi e in un
 * gruppo solo (prenota non esce dal gruppo): una lettura e una scrittura
 * della bitmap, un aggiornamento del descrittore. Se qualcosa non va restano
 * occupati e di nessuno - spazio perso fino a e2fsck, non un errore nei
 * dati - e il superblocco si riscrive comunque con quel che si sa. */
static void prenotati_rilascia(Ext2Mount *m)
{
    uint32_t g, i, k, bmp, liberati = 0;

    if (g_pre_n > 0 && g_pre_primo >= m->primo_dato) {
        g = (g_pre_primo - m->primo_dato) / m->blocchi_per_gruppo;
        i = (g_pre_primo - m->primo_dato) % m->blocchi_per_gruppo;

        if (i + g_pre_n <= m->blocchi_per_gruppo) {
            if (bitmap_blocchi(m, g, b_bmp, &bmp) == 0) {
                for (k = i; k < i + g_pre_n; k++)
                    if (b_bmp[k / 8u] & (1u << (k % 8u))) {
                        b_bmp[k / 8u] &= (uint8_t)~(1u << (k % 8u));
                        liberati++;
                    }
                if (liberati > 0 && scrivi_blocco(m, bmp, b_bmp) == 0 &&
                    desc_aggiorna(m, g, (int)liberati, 0, 0, b_bmp, NULL, 0) == 0)
                    m->blocchi_liberi += liberati;
            }
        }
        if (g_pre_mnt >= 0 && (uint32_t)g_pre_mnt < EXT2_MAX_MOUNT)
            g_super_sporco[g_pre_mnt] = 1;
    }
    g_pre_n   = 0;
    g_pre_mnt = -1;
}

/* Prima di ogni operazione che non sia una write() del file prenotato, e
 * alla chiusura, al sync, allo smontaggio: vedi «LA PRENOTAZIONE RESTA». */
void prenotazione_chiudi(void)
{
    uint32_t k;

    if (g_pre_mnt >= 0 && (uint32_t)g_pre_mnt < K->max_mount &&
        K->mnt[g_pre_mnt].usato)
        prenotati_rilascia(&K->mnt[g_pre_mnt]);
    g_pre_n   = 0;
    g_pre_mnt = -1;

    for (k = 0; k < EXT2_MAX_MOUNT && k < K->max_mount; k++) {
        if (!g_super_sporco[k]) continue;
        g_super_sporco[k] = 0;
        if (K->mnt[k].usato && !K->mnt[k].sola_lettura) super_aggiorna(&K->mnt[k]);
    }
}

static uint32_t alloca_blocco(Ext2Mount *m, uint32_t pref)
{
    uint32_t giro, g, off, dblk, bmp, lim, i, n;

    /* Uno dei prenotati: e' gia' segnato nella bitmap e contato. */
    if (g_pre_n > 0) {
        n = g_pre_primo++;
        g_pre_n--;
        if (g_zero) {
            uint32_t k;
            for (k = 0; k < m->dim_blocco; k++) b_bmp[k] = 0;
            if (scrivi_blocco(m, n, b_bmp) != 0) return 0;
        }
        return n;
    }

    if (pref >= m->n_gruppi) pref = 0;

    for (giro = 0; giro < m->n_gruppi; giro++) {
        g = (pref + giro) % m->n_gruppi;

        if (desc_carica(m, g, &off, &dblk) != 0) return 0;
        if (desc_campo(m, b_desc + off, 12u, 0x2Cu) == 0) continue;     /* gruppo pieno */
        if (bitmap_blocchi(m, g, b_bmp, &bmp) != 0) return 0;

        lim = blocchi_nel_gruppo(m, g);
        i   = primo_libero(lim);

        /* Il descrittore diceva che c'era posto e la bitmap dice di no:
         * i due si contraddicono. Si crede alla BITMAP, che e' la verita'
         * — il contatore e' un riassunto — e si va avanti col gruppo dopo
         * invece di consegnare un blocco che risulta gia' occupato. */
        if (i >= lim) {
            klog(LOG_WARN, "EXT2: gruppo %u: il contatore dice liberi, la "
                           "bitmap no. Ignorato.", g);
            continue;
        }

        b_bmp[i / 8u] |= (uint8_t)(1u << (i % 8u));
        if (scrivi_blocco(m, bmp, b_bmp) != 0) return 0;

        if (desc_aggiorna(m, g, -1, 0, 0, b_bmp, NULL, 0) != 0) return 0;
        m->blocchi_liberi--;

        n = m->primo_dato + g * m->blocchi_per_gruppo + i;

        /* Un blocco nuovo si azzera SEMPRE. Contiene i byte di chi lo
         * usava prima: consegnarlo com'e' significa che il contenuto di un
         * file cancellato ricompare dentro un file nuovo, e come blocco di
         * puntatori quei byte sarebbero indirizzi verso mezzo volume. */
        if (g_zero) {
            uint32_t k;
            for (k = 0; k < m->dim_blocco; k++) b_bmp[k] = 0;
            if (scrivi_blocco(m, n, b_bmp) != 0) return 0;
        }

        return n;
    }

    return 0;
}

/* =============================================================================
 * CANCELLARE COSTAVA CINQUE COMANDI DI DISCO PER KILOBYTE (10 ottobre 2026)
 *
 * Per ogni blocco liberato: leggi il descrittore, leggi la bitmap, scrivi la
 * bitmap, rileggi e riscrivi il descrittore. Un file da 50 MB sono
 * cinquantamila blocchi: `rm` ci metteva piu' di venti secondi anche su un
 * disco emulato.
 *
 * Quando si libera un file INTERO (libera_tutti_i_blocchi) la bitmap del
 * gruppo resta in memoria, in un buffer SUO (b_lib: vedi «perche' sono cinque
 * e non uno»), e va sul disco quando si passa a un altro gruppo e alla fine,
 * col descrittore aggiornato una volta sola. Come per la scrittura, tutto
 * finisce prima che l'operazione torni.
 * ============================================================================= */
static uint8_t  b_lib[BLOCCO_MAX];      /* la bitmap del gruppo che si sta liberando */
static int      g_lib_diff = 0;         /* 1: dentro libera_tutti_i_blocchi */
static int      g_lib_valido = 0;
static uint32_t g_lib_g = 0, g_lib_bmp = 0, g_lib_n = 0;

static int lib_riversa(Ext2Mount *m)
{
    int r = 0;

    if (g_lib_valido && g_lib_n > 0) {
        if (scrivi_blocco(m, g_lib_bmp, b_lib) != 0) r = -1;
        else if (desc_aggiorna(m, g_lib_g, (int)g_lib_n, 0, 0, b_lib, NULL, 0) != 0) r = -1;
    }
    g_lib_valido = 0;
    g_lib_n = 0;
    return r;
}

int libera_blocco(Ext2Mount *m, uint32_t n)
{
    uint32_t g, i, bmp;

    if (n < m->primo_dato || n >= m->n_blocchi) return -1;
    est_dimentica_blocco(n);

    g = (n - m->primo_dato) / m->blocchi_per_gruppo;
    i = (n - m->primo_dato) % m->blocchi_per_gruppo;

    if (g_lib_diff) {
        if (!g_lib_valido || g != g_lib_g) {
            if (lib_riversa(m) != 0) return -1;
            if (bitmap_blocchi(m, g, b_lib, &g_lib_bmp) != 0) return -1;
            g_lib_g = g;
            g_lib_valido = 1;
        }
        if ((b_lib[i / 8u] & (1u << (i % 8u))) == 0) {
            klog(LOG_WARN, "EXT2: blocco %u gia' libero: non lo libero due volte", n);
            return 0;
        }
        b_lib[i / 8u] &= (uint8_t)~(1u << (i % 8u));
        g_lib_n++;
        m->blocchi_liberi++;
        return 0;
    }

    if (bitmap_blocchi(m, g, b_bmp, &bmp) != 0) return -1;

    /* Liberare due volte lo stesso blocco lo farebbe contare due volte fra
     * i liberi, e da li' in poi l'allocatore consegnerebbe lo stesso
     * blocco a due file. Meglio non fare niente e dirlo. */
    if ((b_bmp[i / 8u] & (1u << (i % 8u))) == 0) {
        klog(LOG_WARN, "EXT2: blocco %u gia' libero: non lo libero due volte", n);
        return 0;
    }

    b_bmp[i / 8u] &= (uint8_t)~(1u << (i % 8u));
    if (scrivi_blocco(m, bmp, b_bmp) != 0) return -1;

    if (desc_aggiorna(m, g, +1, 0, 0, b_bmp, NULL, 0) != 0) return -1;
    m->blocchi_liberi++;
    return 0;
}

/* Alloca un inode. `is_dir` serve al contatore delle directory del gruppo,
 * che e2fsck verifica. Ritorna il numero, o 0. */
static uint32_t alloca_inode(Ext2Mount *m, uint32_t pref, int is_dir)
{
    uint32_t giro, g, off, dblk, bmp, i, n;

    if (pref >= m->n_gruppi) pref = 0;

    for (giro = 0; giro < m->n_gruppi; giro++) {
        g = (pref + giro) % m->n_gruppi;

        if (desc_carica(m, g, &off, &dblk) != 0) return 0;
        if (desc_campo(m, b_desc + off, 14u, 0x2Eu) == 0) continue;
        if (bitmap_inode(m, g, b_bmp, &bmp) != 0) return 0;

        i = primo_libero(m->inode_per_gruppo);
        if (i >= m->inode_per_gruppo) continue;

        n = g * m->inode_per_gruppo + i + 1u;    /* gli inode partono da 1 */

        /* Gli inode 1..10 sono riservati. La bitmap del gruppo 0 li ha
         * gia' marcati, ma un volume malformato potrebbe non averlo fatto:
         * consegnarne uno significherebbe dare all'utente l'inode della
         * radice o quello dei blocchi difettosi. */
        if (n < m->primo_inode) continue;

        b_bmp[i / 8u] |= (uint8_t)(1u << (i % 8u));
        if (scrivi_blocco(m, bmp, b_bmp) != 0) return 0;

        if (desc_aggiorna(m, g, 0, -1, is_dir ? 1 : 0, NULL, b_bmp, i + 1u) != 0) return 0;
        m->inode_liberi--;

        return n;
    }

    return 0;
}

static int libera_inode(Ext2Mount *m, uint32_t n, int era_dir)
{
    uint32_t g, i, bmp;

    if (n == 0 || n > m->n_inode) return -1;

    g = (n - 1u) / m->inode_per_gruppo;
    i = (n - 1u) % m->inode_per_gruppo;

    if (bitmap_inode(m, g, b_bmp, &bmp) != 0) return -1;

    if ((b_bmp[i / 8u] & (1u << (i % 8u))) == 0) {
        klog(LOG_WARN, "EXT2: inode %u gia' libero", n);
        return 0;
    }

    b_bmp[i / 8u] &= (uint8_t)~(1u << (i % 8u));
    if (scrivi_blocco(m, bmp, b_bmp) != 0) return -1;

    if (desc_aggiorna(m, g, 0, +1, era_dir ? -1 : 0, NULL, b_bmp, 0) != 0) return -1;
    m->inode_liberi++;
    return 0;
}

/* Riscrive i 128 byte dell'inode `numero`. Lettura-modifica-scrittura del
 * blocco che lo contiene: su un volume con inode da 256 byte i 128 che non
 * tocchiamo appartengono comunque a quell'inode (marche temporali fini,
 * attributi estesi) e azzerarli lo danneggerebbe.
 *
 * ! UN INODE NUOVO (g_inode_nuovo) SI SCRIVE INTERO: i byte oltre i 128
 * possono essere di un file cancellato, o mai azzerati (ext4 lascia la
 * tabella da azzerare a dopo, lazy_itable_init). Si azzerano, e prendono
 * i_extra_isize e la data di nascita (i_crtime) come su Linux. Poi, su
 * metadata_csum, il checksum dell'inode intero. */
static int g_inode_nuovo = 0;

static int scrivi_inode(Ext2Mount *m, uint32_t numero, const uint8_t *src)
{
    uint32_t gruppo, indice, off, dblk, itab, blk, ioff, i;
    Ext4Vol *v = vol_di(m);

    if (numero == 0 || numero > m->n_inode) return -1;

    gruppo = (numero - 1u) / m->inode_per_gruppo;
    indice = (numero - 1u) % m->inode_per_gruppo;

    if (desc_carica(m, gruppo, &off, &dblk) != 0) return -1;
    itab = le32(b_desc + off + 8);

    blk  = itab + (indice * m->dim_inode) / m->dim_blocco;
    ioff = (indice * m->dim_inode) % m->dim_blocco;

    if (leggi_blocco(m, blk, b_ino) != 0) return -1;
    for (i = 0; i < 128u; i++) b_ino[ioff + i] = src[i];

    if (g_inode_nuovo && m->dim_inode > 128u) {
        memset(b_ino + ioff + 128u, 0, m->dim_inode - 128u);
        if (v && v->extra_isize >= 4u) {
            p16(b_ino + ioff + 0x80, v->extra_isize);
            if (v->extra_isize >= 0x14u) p32(b_ino + ioff + 0x90, le32(src + 12));
        }
    }
    somma_inode(m, numero, b_ino + ioff);

    return scrivi_blocco(m, blk, b_ino);
}

static int scrivi_inode_nuovo(Ext2Mount *m, uint32_t numero, const uint8_t *src)
{
    int r;

    g_inode_nuovo = 1;
    r = scrivi_inode(m, numero, src);
    g_inode_nuovo = 0;
    return r;
}

/* =============================================================================
 * SCRITTURA — mappatura con allocazione
 *
 * Come mappa_blocco(), ma quando il blocco logico non esiste lo crea:
 * allocando, se serve, anche i blocchi di puntatori che stanno nel mezzo.
 *
 * Tenere questa funzione SEPARATA da mappa_blocco() e non aggiungerle un
 * parametro "alloca" e' voluto: una lettura non deve poter allocare
 * nemmeno per un errore di battitura del chiamante. Un ext2 montato in
 * sola lettura su cui una `ls` alloca blocchi e' un guasto che nessuno
 * cerca dove sta.
 *
 * `charge` conta i blocchi allocati, indiretti compresi, per aggiornare
 * i_blocks: se ci si dimentica degli indiretti il conto e' basso e e2fsck
 * lo segnala su ogni file che ne ha uno.
 * ============================================================================= */

/* Legge la voce `idx` del blocco di puntatori `blk`; se e' zero ne alloca
 * uno nuovo, lo scrive nella voce e riscrive il blocco. */
static uint32_t punt_o_alloca(Ext2Mount *m, int mnt, uint32_t blk,
                              uint32_t idx, uint32_t pref, uint32_t *charge)
{
    uint32_t p;

    if (leggi_indiretto(m, mnt, blk) != 0) return 0;

    p = le32(b_ind + idx * 4u);
    if (p != 0) return p;

    p = alloca_blocco(m, pref);
    if (p == 0) return 0;

    /* leggi_indiretto() puo' aver perso il buffer: alloca_blocco() scrive
     * il blocco nuovo, e scrivi_blocco() invalida la cache dei puntatori
     * proprio per non lasciare in giro copie vecchie. Va riletto. */
    if (leggi_indiretto(m, mnt, blk) != 0) { libera_blocco(m, p); return 0; }

    b_ind[idx * 4u + 0] = (uint8_t)p;
    b_ind[idx * 4u + 1] = (uint8_t)(p >> 8);
    b_ind[idx * 4u + 2] = (uint8_t)(p >> 16);
    b_ind[idx * 4u + 3] = (uint8_t)(p >> 24);

    if (g_ind_diff) g_ind_sporco = 1;       /* lo scrive ind_riversa: vedi in testa */
    else if (scrivi_blocco(m, blk, b_ind) != 0) { libera_blocco(m, p); return 0; }

    (*charge)++;
    return p;
}

/* Il puntatore di primo livello dentro l'inode (i_block[i]). */
static uint32_t inode_punt_o_alloca(Ext2Mount *m, uint8_t *inode, uint32_t i,
                                    uint32_t pref, uint32_t *charge)
{
    uint32_t p = le32(inode + 40u + i * 4u);

    if (p != 0) return p;

    p = alloca_blocco(m, pref);
    if (p == 0) return 0;

    inode[40u + i * 4u + 0] = (uint8_t)p;
    inode[40u + i * 4u + 1] = (uint8_t)(p >> 8);
    inode[40u + i * 4u + 2] = (uint8_t)(p >> 16);
    inode[40u + i * 4u + 3] = (uint8_t)(p >> 24);

    (*charge)++;
    return p;
}

static int g_dati_zero = 1;     /* 0 dentro ext2_write: vedi «SCRIVERE COSTAVA» */

uint32_t alloca_blocco_per(Ext2Mount *m, uint32_t pref, int dato)
{
    uint32_t r;

    g_zero = dato ? g_dati_zero : 0;
    r = alloca_blocco(m, pref);
    g_zero = 1;
    return r;
}

static uint32_t mappa_o_alloca(Ext2Mount *m, int mnt, uint32_t num, uint8_t *inode,
                               uint32_t n, uint32_t pref, uint32_t *charge)
{
    uint32_t ppb = m->punt_per_blocco;
    uint32_t l1, l2, r;

    /* ext4: the extent tree (estesi.c) */
    if (le32(inode + 32) & INODE_FL_EXTENTS)
        return est_mappa_o_alloca(m, mnt, num, inode, n, pref, charge);

    /* ! L'ULTIMO PASSO E' IL BLOCCO DI DATI, gli altri sono blocchi di
     * puntatori: solo il primo puo' nascere senza essere azzerato sul disco.
     * g_zero torna a 1 subito dopo, qualunque cosa sia successa. */
#define DATO(chiamata) (g_zero = g_dati_zero, r = (chiamata), g_zero = 1, r)

    if (n < 12u) return DATO(inode_punt_o_alloca(m, inode, n, pref, charge));
    n -= 12u;

    if (n < ppb) {
        l1 = inode_punt_o_alloca(m, inode, 12u, pref, charge);
        if (l1 == 0) return 0;
        return DATO(punt_o_alloca(m, mnt, l1, n, pref, charge));
    }
    n -= ppb;

    if (n < ppb * ppb) {
        l1 = inode_punt_o_alloca(m, inode, 13u, pref, charge);
        if (l1 == 0) return 0;
        l2 = punt_o_alloca(m, mnt, l1, n / ppb, pref, charge);
        if (l2 == 0) return 0;
        return DATO(punt_o_alloca(m, mnt, l2, n % ppb, pref, charge));
    }
    n -= ppb * ppb;

    l1 = inode_punt_o_alloca(m, inode, 14u, pref, charge);
    if (l1 == 0) return 0;
    l2 = punt_o_alloca(m, mnt, l1, n / (ppb * ppb), pref, charge);
    if (l2 == 0) return 0;
    l2 = punt_o_alloca(m, mnt, l2, (n / ppb) % ppb, pref, charge);
    if (l2 == 0) return 0;
    return DATO(punt_o_alloca(m, mnt, l2, n % ppb, pref, charge));
#undef DATO
}

/* =============================================================================
 * Liberazione di tutti i blocchi di un inode
 *
 * Ricorsiva sui livelli di indirezione, e libera anche i blocchi di
 * PUNTATORI: dimenticarli lascia blocchi occupati che nessun file usa —
 * spazio che sparisce a ogni cancellazione e non torna mai.
 *
 * Il blocco di puntatori si copia in una variabile locale prima di
 * percorrerlo: b_ind e' uno solo, e scendere di un livello lo sovrascrive.
 * Con la ricorsione sullo stack la copia costa dim_blocco byte per livello,
 * al massimo tre livelli.
 * ============================================================================= */
static int libera_catena(Ext2Mount *m, int mnt, uint32_t blk, int livello,
                         uint32_t *liberati)
{
    uint8_t  copia[BLOCCO_MAX];
    uint32_t i;

    if (blk == 0) return 0;

    if (livello > 0) {
        if (leggi_blocco(m, blk, copia) != 0) return -1;

        for (i = 0; i < m->punt_per_blocco; i++) {
            uint32_t p = le32(copia + i * 4u);
            if (p != 0) libera_catena(m, mnt, p, livello - 1, liberati);
        }
    }

    if (libera_blocco(m, blk) != 0) return -1;
    (*liberati)++;
    return 0;
}

/* =============================================================================
 * THE BLOCK OF EXTENDED ATTRIBUTES (i_file_acl, 0.263)
 *
 * Linux's ext2 has ext_attr: a file's attributes (ACLs, SELinux labels) may
 * sit in one block of their own, which several inodes can share - it counts
 * its users (h_refcount at offset 4, after the magic 0xEA020000). A file
 * deleted without looking at it left that block allocated for ever: e2fsck
 * reports it, and the space is lost. Here: one user less, and the block
 * freed when it was the last.
 * ============================================================================= */
#define EA_MAGIA 0xEA020000u

static int libera_attributi(Ext2Mount *m, uint8_t *inode)
{
    uint32_t b = le32(inode + 104);
    uint8_t  blocco[BLOCCO_MAX];
    uint32_t n;

    if (b == 0) return 0;
    p32(inode + 104, 0);
    if (b >= m->n_blocchi || leggi_blocco(m, b, blocco) != 0) return -1;
    if (le32(blocco) != EA_MAGIA) {
        klog(LOG_WARN, "EXT2: block %u is not an attribute block: left alone", b);
        return 0;
    }
    n = le32(blocco + 4);
    if (n > 1u) {
        p32(blocco + 4, n - 1u);
        somma_attributi(m, b, blocco);
        return scrivi_blocco(m, b, blocco);
    }
    return libera_blocco(m, b);
}

static int libera_tutti_i_blocchi(Ext2Mount *m, int mnt, uint32_t num, uint8_t *inode)
{
    uint32_t i, liberati = 0;
    uint32_t ea = le32(inode + 104) ? m->dim_blocco / 512u : 0u;

    /* ext4: the extent tree, in the same batch as below */
    if (le32(inode + 32) & INODE_FL_EXTENTS) {
        int r;

        g_lib_diff = 1;
        g_lib_valido = 0;
        g_lib_n = 0;
        r = est_pota(m, num, inode, 0, &liberati);
        if (lib_riversa(m) != 0) r = -1;
        g_lib_diff = 0;
        libera_attributi(m, inode);
        est_radice_vuota(inode);
        p32(inode + 28, 0);
        p32(inode + 4, 0);
        return r;
    }

    /* ! i_block HOLDS BLOCK NUMBERS ONLY WHEN THE INODE HAS DATA BLOCKS.
     * A fast symbolic link keeps its target there (up to 60 characters),
     * a device file its numbers: read as pointers, those bytes would free
     * blocks of other files. Linux's test: no blocks besides the attribute
     * block. */
    if (le32(inode + 28) <= ea) {
        for (i = 0; i < 15u; i++) p32(inode + 40u + i * 4u, 0);
        libera_attributi(m, inode);
        p32(inode + 28, 0);
        p32(inode + 4, 0);
        return 0;
    }

    /* Vedi «CANCELLARE COSTAVA»: da qui a lib_riversa() la bitmap del gruppo
     * sta in memoria. */
    g_lib_diff = 1;
    g_lib_valido = 0;
    g_lib_n = 0;

    for (i = 0; i < 12u; i++) {
        uint32_t p = le32(inode + 40u + i * 4u);
        if (p != 0) libera_catena(m, mnt, p, 0, &liberati);
    }

    libera_catena(m, mnt, le32(inode + 40u + 12u * 4u), 1, &liberati);
    libera_catena(m, mnt, le32(inode + 40u + 13u * 4u), 2, &liberati);
    libera_catena(m, mnt, le32(inode + 40u + 14u * 4u), 3, &liberati);

    lib_riversa(m);
    g_lib_diff = 0;
    libera_attributi(m, inode);

    for (i = 0; i < 15u; i++) {
        inode[40u + i * 4u + 0] = 0; inode[40u + i * 4u + 1] = 0;
        inode[40u + i * 4u + 2] = 0; inode[40u + i * 4u + 3] = 0;
    }

    p32(inode + 28, 0);         /* i_blocks */
    p32(inode + 4, 0);          /* i_size */
    return 0;
}

/* =============================================================================
 * POTATURA — liberare la CODA di una catena lasciando intatta la testa
 *
 * E' l'operazione piu' facile da sbagliare del driver, e sbagliarla libera
 * blocchi che il file usa ancora: il danno non si vede subito, si vede
 * quando l'allocatore consegna uno di quei blocchi a un altro file e i due
 * cominciano a scriversi addosso.
 *
 * LA DIFFICOLTA' VERA non sono i blocchi di dati — quelli si liberano per
 * indice — ma i blocchi di PUNTATORI, che hanno tre destini diversi:
 *
 *   interamente oltre il taglio   si libera tutto il sottoalbero, e il
 *                                 blocco di puntatori stesso;
 *   interamente prima del taglio  non si tocca niente;
 *   A CAVALLO del taglio          si scende dentro, si potano solo le voci
 *                                 oltre il taglio, e il blocco di puntatori
 *                                 SOPRAVVIVE perche' le voci prima del
 *                                 taglio servono ancora.
 *
 * Il terzo caso e' quello che si dimentica. Liberare un indiretto perche'
 * "il troncamento lo tocca" significa buttare via i puntatori ai blocchi
 * che restano: il file conserva la sua dimensione e perde i dati in mezzo.
 *
 * La copia locale del blocco di puntatori e' obbligatoria: b_ind e' uno
 * solo e scendere di un livello lo sovrascriverebbe. Costa dim_blocco byte
 * per livello di ricorsione, al massimo tre.
 * ============================================================================= */

/* `blk`     blocco di puntatori da potare
 * `livello` 1 = le sue voci sono blocchi di dati, 2 = sono indiretti, 3 = doppi
 * `base`    blocco logico coperto dalla PRIMA voce di questo blocco
 * `primo`   primo blocco logico da buttare via
 *
 * Ritorna 1 se il blocco di puntatori e' rimasto vuoto — e allora chi
 * chiama deve liberarlo e azzerare il proprio puntatore — 0 se serve
 * ancora, <0 su errore. */
static int pota_indiretto(Ext2Mount *m, int mnt, uint32_t blk, int livello,
                          uint32_t base, uint32_t primo, uint32_t *liberati)
{
    uint8_t  copia[BLOCCO_MAX];
    uint32_t ppb = m->punt_per_blocco;
    uint32_t passo = 1, i;
    int      modificato = 0, resta = 0, k;

    if (blk == 0) return 1;

    /* Quanti blocchi logici copre UNA voce di questo blocco. */
    for (k = 1; k < livello; k++) passo *= ppb;

    if (leggi_blocco(m, blk, copia) != 0) return -1;

    for (i = 0; i < ppb; i++) {
        uint32_t p     = le32(copia + i * 4u);
        uint32_t da    = base + i * passo;
        uint32_t fino  = da + passo;            /* escluso */

        if (p == 0) continue;

        if (fino <= primo) {                    /* tutto prima del taglio */
            resta = 1;
            continue;
        }

        if (da >= primo) {                      /* tutto oltre il taglio */
            if (libera_catena(m, mnt, p, livello - 1, liberati) != 0) return -1;
            p32(copia + i * 4u, 0);
            modificato = 1;
            continue;
        }

        /* A cavallo: si scende. Il sottoalbero puo' svuotarsi del tutto —
         * e allora si libera anche lui — oppure conservare le voci prima
         * del taglio, e in quel caso NON si tocca. */
        {
            int vuoto = pota_indiretto(m, mnt, p, livello - 1, da, primo,
                                       liberati);
            if (vuoto < 0) return -1;

            if (vuoto) {
                if (libera_blocco(m, p) != 0) return -1;
                (*liberati)++;
                p32(copia + i * 4u, 0);
                modificato = 1;
            } else {
                resta = 1;
            }
        }
    }

    if (modificato && scrivi_blocco(m, blk, copia) != 0) return -1;

    return resta ? 0 : 1;
}

/* Libera tutti i blocchi dell'inode dal blocco logico `primo` in poi.
 * Aggiorna i_block e i_blocks; NON tocca i_size, che e' del chiamante. */
static int pota_inode(Ext2Mount *m, int mnt, uint32_t num, uint8_t *inode, uint32_t primo)
{
    uint32_t ppb = m->punt_per_blocco;
    uint32_t liberati = 0;
    uint32_t i, blocchi;
    /* Primo blocco logico coperto da ciascuno dei tre indiretti. */
    uint32_t base1 = 12u;
    uint32_t base2 = base1 + ppb;
    uint32_t base3 = base2 + ppb * ppb;
    int      idx, liv;
    uint32_t basi[3];

    /* ext4: the extent tree, the bitmaps in one batch (see «CANCELLARE») */
    if (le32(inode + 32) & INODE_FL_EXTENTS) {
        int r;

        g_lib_diff = 1;
        g_lib_valido = 0;
        g_lib_n = 0;
        r = est_pota(m, num, inode, primo, &liberati);
        if (lib_riversa(m) != 0) r = -1;
        g_lib_diff = 0;
        if (r != 0) return -1;
        goto conta;
    }

    /* --- i dodici diretti --- */
    for (i = primo; i < 12u; i++) {
        uint32_t p = le32(inode + 40u + i * 4u);
        if (p == 0) continue;
        if (libera_blocco(m, p) != 0) return -1;
        liberati++;
        p32(inode + 40u + i * 4u, 0);
    }

    /* --- i tre indiretti, con lo stesso ragionamento a tre casi --- */
    basi[0] = base1; basi[1] = base2; basi[2] = base3;

    for (idx = 0; idx < 3; idx++) {
        uint32_t voce = 12u + (uint32_t)idx;
        uint32_t p    = le32(inode + 40u + voce * 4u);
        uint32_t base = basi[idx];

        liv = idx + 1;
        if (p == 0) continue;

        if (base >= primo) {
            /* L'indiretto comincia gia' oltre il taglio: via tutto. */
            if (libera_catena(m, mnt, p, liv, &liberati) != 0) return -1;
            p32(inode + 40u + voce * 4u, 0);
        } else {
            int vuoto = pota_indiretto(m, mnt, p, liv, base, primo, &liberati);
            if (vuoto < 0) return -1;
            if (vuoto) {
                if (libera_blocco(m, p) != 0) return -1;
                liberati++;
                p32(inode + 40u + voce * 4u, 0);
            }
        }
    }

conta:
    /* i_blocks e' in unita' da 512 byte e comprende gli indiretti: la
     * sottrazione usa lo stesso conteggio che l'allocazione ha sommato,
     * indiretti inclusi, altrimenti il numero deriva a ogni troncamento. */
    blocchi = le32(inode + 28);
    {
        uint32_t giu = liberati * (m->dim_blocco / 512u);
        blocchi = (blocchi > giu) ? (blocchi - giu) : 0u;
    }
    p32(inode + 28, blocchi);

    return 0;
}

/* =============================================================================
 * SCRITTURA — voci di directory
 *
 * Una directory ext2 e' una sequenza di voci a lunghezza variabile, e la
 * lunghezza dichiarata (rec_len) NON e' quella occupata: l'ultima voce di
 * ogni blocco si allunga fino in fondo, e le voci cancellate vengono
 * assorbite da quella che le precede. Lo spazio libero non e' una lista:
 * e' nascosto dentro i rec_len.
 *
 * Ne discende che aggiungere una voce significa cercare un rec_len piu'
 * lungo del necessario e SPEZZARLO, non cercare un buco.
 * ============================================================================= */


/* Byte davvero occupati da una voce col nome lungo `len`, arrotondati a 4.
 * L'allineamento non e' cosmetico: rec_len deve essere multiplo di 4 o i
 * campi a 32 bit della voce successiva finiscono disallineati, e diversi
 * driver li leggono male. */
static uint32_t voce_lunghezza(uint32_t len)
{
    return (8u + len + 3u) & ~3u;
}

/* =============================================================================
 * A DIRECTORY WITH A HASHED INDEX LOSES IT BEFORE IT IS CHANGED (0.263)
 *
 * Linux's ext2 volumes have dir_index, and a directory of more than one
 * block gets an index (htree) - from mke2fs -d, e2fsck -D, or Linux itself.
 * The index lives inside blocks that also read as ordinary directory blocks
 * (that is how this driver has always READ them), but an entry added or
 * removed without updating it leaves the index pointing to the wrong
 * blocks: Linux then fails to find files that are there.
 *
 * The answer the format gives: clear EXT2_INDEX_FL. The blocks are valid as
 * a plain directory as they are - the root's "." and ".." cover the index
 * data, the inner index blocks are one empty entry each - and Linux, and
 * e2fsck -D, rebuild the index when they want one. Linux's own ext2/ext3
 * code does the same when it changes a directory without htree support.
 *
 * ! THE FLAG GOES FIRST, ON DISK, and only then the directory changes: a
 * crash in between leaves a plain directory with the old entries, which is
 * a valid volume.
 * ============================================================================= */
/* A directory block to the disk: on metadata_csum with its tail and sum. */
static int dir_scrivi(Ext2Mount *m, uint32_t num, const uint8_t *inode, uint32_t blk,
                      uint8_t *b)
{
    if (somma_dir(m, num, inode, b) != 0) return -1;
    return scrivi_blocco(m, blk, b);
}

/* ! ON metadata_csum THE INDEX BLOCKS HAVE NO DIRECTORY TAIL - theirs is a
 * dx tail - and as plain blocks Linux would refuse them ("directory block
 * failed checksum"). Each block gets one (dir_coda: the root's ".." and an
 * inner node's empty entry have room for it) and its sum, in the same
 * transaction as the flag. */
static int dir_togli_indice(Ext2Mount *m, int mnt, uint32_t dir_num, uint8_t *dir_inode)
{
    uint32_t fl = le32(dir_inode + 32);
    Ext4Vol *v = vol_di(m);

    if (!(fl & INODE_FL_INDEX)) return 0;
    p32(dir_inode + 32, fl & ~INODE_FL_INDEX);
    klog(LOG_INFO, "EXT2: directory %u loses its hashed index (it is changed)", dir_num);
    if (v && v->meta_csum) {
        uint32_t off, dim = le32(dir_inode + 4);

        for (off = 0; off < dim; off += m->dim_blocco) {
            uint32_t blk = mappa_blocco(m, mnt, dir_inode, off / m->dim_blocco);

            if (blk == 0) continue;
            if (leggi_blocco(m, blk, b_dati) != 0) return -1;
            if (dir_scrivi(m, dir_num, dir_inode, blk, b_dati) != 0) return -1;
        }
    }
    return scrivi_inode(m, dir_num, dir_inode);
}

/* Aggiunge una voce alla directory `dir_inode` (numero `dir_num`).
 * Ritorna 0, o <0. */
static int dir_aggiungi(Ext2Mount *m, int mnt, uint32_t dir_num,
                        uint8_t *dir_inode, const char *nome, uint32_t len,
                        uint32_t ino, uint32_t tipo)
{
    uint32_t dim     = le32(dir_inode + 4);
    uint32_t serve   = voce_lunghezza(len);
    uint32_t fine    = dir_fine(m);     /* the entries end here: see dir_coda */
    uint32_t off, k;

    if (dir_togli_indice(m, mnt, dir_num, dir_inode) != 0) return -1;

    for (off = 0; off < dim; off += m->dim_blocco) {
        uint32_t blk = mappa_blocco(m, mnt, dir_inode, off / m->dim_blocco);
        uint32_t p   = 0;

        if (blk == 0) continue;
        if (leggi_blocco(m, blk, b_dati) != 0) return -1;
        if (dir_coda(m, b_dati) != 0) continue;

        while (p < fine) {
            uint32_t vino = le32(b_dati + p);
            uint32_t rec  = le16(b_dati + p + 4);
            uint32_t vlen = b_dati[p + 6];
            uint32_t usato;

            if (rec < 8u || p + rec > fine) break;

            /* Una voce con inode 0 e' spazio libero per intero; una voce
             * viva occupa solo i propri byte e il resto del suo rec_len e'
             * spazio riutilizzabile. */
            usato = (vino == 0) ? 0u : voce_lunghezza(vlen);

            if (rec - usato >= serve) {
                uint32_t nuovo = p + usato;

                if (usato > 0) p16(b_dati + p + 4, usato);   /* accorcia */

                p32(b_dati + nuovo, ino);
                p16(b_dati + nuovo + 4, rec - usato);
                b_dati[nuovo + 6] = (uint8_t)len;
                b_dati[nuovo + 7] = (uint8_t)tipo;
                for (k = 0; k < len; k++)
                    b_dati[nuovo + 8 + k] = (uint8_t)nome[k];

                return dir_scrivi(m, dir_num, dir_inode, blk, b_dati);
            }

            p += rec;
        }
    }

    /* Nessun blocco aveva posto: se ne aggiunge uno, con una voce sola che
     * lo occupa tutto. La directory cresce di un blocco, e i_size di una
     * directory e' SEMPRE un multiplo esatto della dimensione del blocco —
     * non la somma delle voci. */
    {
        uint32_t charge = 0;
        uint32_t nuovo_blk = mappa_o_alloca(m, mnt, dir_num, dir_inode,
                                            dim / m->dim_blocco,
                                            (dir_num - 1u) / m->inode_per_gruppo,
                                            &charge);
        uint32_t blocchi;

        if (nuovo_blk == 0) return -1;

        for (k = 0; k < m->dim_blocco; k++) b_dati[k] = 0;

        p32(b_dati, ino);
        p16(b_dati + 4, m->dim_blocco);
        b_dati[6] = (uint8_t)len;
        b_dati[7] = (uint8_t)tipo;
        for (k = 0; k < len; k++) b_dati[8 + k] = (uint8_t)nome[k];

        if (dir_scrivi(m, dir_num, dir_inode, nuovo_blk, b_dati) != 0) return -1;

        p32(dir_inode + 4, dim + m->dim_blocco);
        blocchi = le32(dir_inode + 28) + charge * (m->dim_blocco / 512u);
        p32(dir_inode + 28, blocchi);
        return 0;
    }
}

/* Toglie la voce `nome` dalla directory. Ritorna il numero di inode che
 * conteneva, o 0 se non c'era.
 *
 * La voce non si cancella: si allunga il rec_len di quella PRECEDENTE fino
 * a inglobarla. Se e' la prima del blocco si azzera il suo inode, che e'
 * il modo previsto dal formato per dire "qui non c'e' niente" senza
 * spostare il resto. */
static uint32_t dir_rimuovi(Ext2Mount *m, int mnt, uint32_t dir_num,
                            uint8_t *dir_inode, const char *nome, uint32_t len)
{
    uint32_t dim = le32(dir_inode + 4);
    uint32_t fine = dir_fine(m);
    uint32_t off;

    if (dir_togli_indice(m, mnt, dir_num, dir_inode) != 0) return 0;

    for (off = 0; off < dim; off += m->dim_blocco) {
        uint32_t blk  = mappa_blocco(m, mnt, dir_inode, off / m->dim_blocco);
        uint32_t p    = 0;
        uint32_t prec = 0xFFFFFFFFu;

        if (blk == 0) continue;
        if (leggi_blocco(m, blk, b_dati) != 0) return 0;
        if (dir_coda(m, b_dati) != 0) continue;

        while (p < fine) {
            uint32_t vino = le32(b_dati + p);
            uint32_t rec  = le16(b_dati + p + 4);
            uint32_t vlen = b_dati[p + 6];

            if (rec < 8u || p + rec > fine) break;

            if (vino != 0 && nome_uguale(b_dati + p + 8, vlen, nome, len)) {
                if (prec == 0xFFFFFFFFu) {
                    p32(b_dati + p, 0);
                } else {
                    p16(b_dati + prec + 4, le16(b_dati + prec + 4) + rec);
                }
                if (dir_scrivi(m, dir_num, dir_inode, blk, b_dati) != 0) return 0;
                return vino;
            }

            prec = p;
            p += rec;
        }
    }

    return 0;
}

/* Vero se la directory contiene solo "." e "..". */
static int dir_vuota(Ext2Mount *m, int mnt, uint8_t *dir_inode)
{
    uint32_t dim = le32(dir_inode + 4);
    uint32_t off;

    for (off = 0; off < dim; off += m->dim_blocco) {
        uint32_t blk = mappa_blocco(m, mnt, dir_inode, off / m->dim_blocco);
        uint32_t p   = 0;

        if (blk == 0) continue;
        if (leggi_blocco(m, blk, b_dati) != 0) return 0;

        while (p < m->dim_blocco) {
            uint32_t vino = le32(b_dati + p);
            uint32_t rec  = le16(b_dati + p + 4);
            uint32_t vlen = b_dati[p + 6];

            if (rec < 8u || p + rec > m->dim_blocco) break;

            if (vino != 0 &&
                !(vlen == 1u && b_dati[p + 8] == '.') &&
                !(vlen == 2u && b_dati[p + 8] == '.' && b_dati[p + 9] == '.'))
                return 0;

            p += rec;
        }
    }

    return 1;
}

/* =============================================================================
 * Separazione di un percorso in "directory padre" + "ultimo nome"
 *
 * "/a/b/c" -> padre "/a/b", nome "c".  "/c" -> padre "/", nome "c".
 * Ritorna la lunghezza del nome, o 0 se il percorso non ne ha uno (cioe'
 * e' la radice, che non si crea e non si cancella).
 * ============================================================================= */
static uint32_t separa(const char *percorso, char *padre, uint32_t padre_max,
                       const char **nome)
{
    uint32_t len = 0, ultimo = 0, i;

    if (percorso == NULL || percorso[0] != '/') return 0;

    while (percorso[len]) len++;

    /* Uno slash finale non fa parte del nome: "/a/b/" e' "/a/b". */
    while (len > 1u && percorso[len - 1u] == '/') len--;
    if (len <= 1u) return 0;                     /* e' la radice */

    for (i = 0; i < len; i++) if (percorso[i] == '/') ultimo = i;

    if (ultimo == 0) {
        padre[0] = '/'; padre[1] = '\0';
    } else {
        if (ultimo >= padre_max) return 0;
        for (i = 0; i < ultimo; i++) padre[i] = percorso[i];
        padre[ultimo] = '\0';
    }

    *nome = percorso + ultimo + 1u;
    return len - ultimo - 1u;
}

/* =============================================================================
 * Operazioni di scrittura
 * ============================================================================= */

/* Prepara una creazione: risolve il padre, verifica che il nome sia libero
 * e utilizzabile. Ritorna 0 e riempie tutto, o un errno negativo. */
static int prepara(Ext2Mount *m, int mnt, const char *percorso,
                   char *padre, const char **nome, uint32_t *len,
                   uint32_t *padre_ino, uint8_t *padre_inode)
{
    *len = separa(percorso, padre, EXT2_PERCORSO_MAX, nome);
    if (*len == 0) return -1;
    if (*len > EXT2_NOME_MAX - 1u) return -1;

    *padre_ino = risolvi(m, mnt, padre, padre_inode);
    if (*padre_ino == 0) return -1;
    if ((le16(padre_inode) & MODE_TIPO) != MODE_DIR) return -1;

    if (cerca_voce(m, mnt, padre_inode, *nome, *len) != 0) return -2;  /* esiste */
    return 0;
}

/* =============================================================================
 * ! IL PROPRIETARIO DI CIO' CHE SI CREA, dal 17 agosto 2026.
 *
 * Prima `inode_nuovo` azzerava tutto e non ci scriveva sopra: ogni file nato su
 * EX-OS era di uid 0. Finche' nessuno guardava quei campi non faceva
 * differenza; nel momento in cui un controllo li guarda, un utente normale non
 * riuscirebbe a rileggere il file che ha appena scritto — e il difetto
 * sembrerebbe del controllo, mentre sarebbe qui.
 *
 * ! CHI CREA E' UN PARAMETRO, NON UNO STATO NASCOSTO. Un «uid corrente» tenuto
 * in una variabile del modulo andrebbe impostato prima di ogni chiamata, e la
 * volta che qualcuno se ne dimenticasse il file nascerebbe di root senza che
 * nessuno lo dica. Un parametro non si puo' dimenticare: il compilatore lo
 * chiede.
 * ============================================================================= */
static void inode_nuovo(uint8_t *inode, uint32_t modo, uint32_t link,
                        uint32_t dim, uint32_t blocchi_512,
                        uint32_t uid, uint32_t gid)
{
    uint32_t i;
    uint32_t adesso = unix_ora_corrente();

    for (i = 0; i < 128u; i++) inode[i] = 0;

    p16(inode +  0, modo);
    p16(inode +  2, uid);       /* i_uid  */
    p16(inode + 24, gid);       /* i_gid  */
    p32(inode +  4, dim);
    p16(inode + 26, link);
    p32(inode + 28, blocchi_512);

    /* i_atime, i_ctime, i_mtime: offset 8, 12, 16. Zero resta zero se
     * l'orologio non ha risposto — vedi unix_ora_corrente. */
    p32(inode +  8, adesso);
    p32(inode + 12, adesso);
    p32(inode + 16, adesso);
}

/* =============================================================================
 * ext2_chown — cambiare il proprietario di un file gia' esistente
 *
 * ! SENZA QUESTA, «ogni utente ha la sua directory sotto /home» NON SI PUO'
 * FARE. Chi crea diventa proprietario, e basta: /home appartiene a root con
 * 0755, quindi un utente normale non ci puo' creare dentro la propria casa —
 * e se la crea root, il padrone non ci puo' scrivere. Serve qualcuno che crei
 * e poi consegni, ed e' esattamente quello che fa `adduser` su ogni Unix.
 *
 * ! IL CONTROLLO DI CHI PUO' NON STA QUI, sta nella VFS. Questo file sa
 * scrivere un inode; decidere a chi e' permesso e' una politica, e tenerla in
 * un posto solo — vfs_permesso() — e' quello che impedisce di dimenticarla in
 * uno dei chiamanti.
 * ============================================================================= */
int ext2_chown(int mnt, const char *percorso, uint32_t uid, uint32_t gid)
{
    Ext2Mount *m = prendi_scrivibile(mnt);
    uint8_t    inode[128];
    uint32_t   ino;

    if (m == NULL) return -1;

    ino = risolvi(m, mnt, percorso, inode);
    if (ino == 0) return -1;

    p16(inode +  2, uid);
    p16(inode + 24, gid);

    if (scrivi_inode(m, ino, inode) != 0) return -1;
    return super_aggiorna(m);
}

/* Cambia i nove bit di permesso, lasciando intatti i quattro alti che dicono
 * il TIPO: sovrascrivere i_mode per intero trasformerebbe una directory in un
 * file agli occhi di chiunque la legga dopo. */
int ext2_chmod(int mnt, const char *percorso, uint32_t modo)
{
    Ext2Mount *m = prendi_scrivibile(mnt);
    uint8_t    inode[128];
    uint32_t   ino, vecchio;

    if (m == NULL) return -1;

    ino = risolvi(m, mnt, percorso, inode);
    if (ino == 0) return -1;

    vecchio = le16(inode + 0);
    p16(inode + 0, (vecchio & ~0777u) | (modo & 0777u));

    if (scrivi_inode(m, ino, inode) != 0) return -1;
    return super_aggiorna(m);
}

int ext2_create(int mnt, const char *percorso, uint32_t uid, uint32_t gid)
{
    Ext2Mount *m = prendi_scrivibile(mnt);
    char       padre[EXT2_PERCORSO_MAX];
    const char *nome;
    uint8_t    pino[128], nino[128];
    uint32_t   len, pnum, num;
    int        r;

    if (m == NULL) return -1;

    r = prepara(m, mnt, percorso, padre, &nome, &len, &pnum, pino);
    if (r != 0) return r;

    num = alloca_inode(m, (pnum - 1u) / m->inode_per_gruppo, 0);
    if (num == 0) return -1;

    /* ! 0644: chi lo possiede legge e scrive, gli altri leggono soltanto. E'
     * il valore che si aspetta chiunque abbia usato un Unix, e vale finche'
     * non ci sara' una umask — che e' proprio il meccanismo con cui si cambia
     * questa scelta senza toccare il codice. */
    inode_nuovo(nino, MODE_FILE | 0644u, 1, 0, 0, uid, gid);
    if (vol_di(m)->estesi) est_radice_vuota(nino);
    if (scrivi_inode_nuovo(m, num, nino) != 0) { libera_inode(m, num, 0); return -1; }

    if (dir_aggiungi(m, mnt, pnum, pino, nome, len, num, DIRTIPO_FILE) != 0) {
        /* La voce non c'e': l'inode allocato non lo raggiunge nessuno, e
         * lasciarlo occupato sarebbe una perdita permanente. Si torna
         * indietro invece di fallire a meta'. */
        libera_inode(m, num, 0);
        return -1;
    }

    if (scrivi_inode(m, pnum, pino) != 0) return -1;
    return super_aggiorna(m);
}

/* =============================================================================
 * dir_nlink (ext4): a directory's link count is "." + its name + one ".."
 * per subdirectory, and at 65000 it would overflow. Linux then writes 1 -
 * "more than it can count" - and never touches it again; here the same.
 * ============================================================================= */
static void link_piu(Ext2Mount *m, uint8_t *dino)
{
    Ext4Vol *v = vol_di(m);
    uint32_t l = le16(dino + 26);

    if (v && v->dir_nlink && (l == 1u || l >= 64999u)) l = 1u;
    else l++;
    p16(dino + 26, l);
}

static void link_meno(Ext2Mount *m, uint8_t *dino)
{
    Ext4Vol *v = vol_di(m);
    uint32_t l = le16(dino + 26);

    if (v && v->dir_nlink && l == 1u) return;
    if (l > 0) l--;
    p16(dino + 26, l);
}

int ext2_mkdir(int mnt, const char *percorso, uint32_t uid, uint32_t gid)
{
    Ext2Mount *m = prendi_scrivibile(mnt);
    char       padre[EXT2_PERCORSO_MAX];
    const char *nome;
    uint8_t    pino[128], nino[128];
    uint32_t   len, pnum, num, blk, gruppo, k;
    int        r;

    if (m == NULL) return -1;

    r = prepara(m, mnt, percorso, padre, &nome, &len, &pnum, pino);
    if (r != 0) return r;

    gruppo = (pnum - 1u) / m->inode_per_gruppo;

    num = alloca_inode(m, gruppo, 1);
    if (num == 0) return -1;

    blk = alloca_blocco(m, gruppo);
    if (blk == 0) { libera_inode(m, num, 1); return -1; }

    /* Il contenuto: "." e "..", con l'ultima voce che arriva fino in fondo
     * al blocco. i_size di una directory e' un blocco intero. */
    for (k = 0; k < m->dim_blocco; k++) b_dati[k] = 0;

    p32(b_dati, num);  p16(b_dati + 4, 12); b_dati[6] = 1; b_dati[7] = DIRTIPO_DIR;
    b_dati[8] = '.';

    p32(b_dati + 12, pnum); p16(b_dati + 16, m->dim_blocco - 12u);
    b_dati[18] = 2; b_dati[19] = DIRTIPO_DIR;
    b_dati[20] = '.'; b_dati[21] = '.';

    /* DUE collegamenti: il nome nel padre e il "." dentro di se'. */
    /* ! 0755 SU UNA DIRECTORY, e la differenza con 0644 di un file conta: il
     * bit x su una directory non vuol dire «eseguibile», vuol dire
     * «attraversabile». Senza, nessuno potrebbe entrarci nemmeno per leggere
     * un file che gli appartiene. */
    inode_nuovo(nino, MODE_DIR | 0755u, 2, m->dim_blocco, m->dim_blocco / 512u,
                uid, gid);
    if (vol_di(m)->estesi) {
        /* ext4: one extent, {0, 1, blk} */
        est_radice_vuota(nino);
        p16(nino + 42, 1u);
        p32(nino + 52, 0);
        p16(nino + 56, 1u);
        p32(nino + 60, blk);
    } else {
        p32(nino + 40, blk);
    }

    /* the block is summed with the new inode's seed (its number, generation 0) */
    if (dir_scrivi(m, num, nino, blk, b_dati) != 0) {
        libera_blocco(m, blk); libera_inode(m, num, 1);
        return -1;
    }

    if (scrivi_inode_nuovo(m, num, nino) != 0) {
        libera_blocco(m, blk); libera_inode(m, num, 1);
        return -1;
    }

    if (dir_aggiungi(m, mnt, pnum, pino, nome, len, num, DIRTIPO_DIR) != 0) {
        libera_blocco(m, blk); libera_inode(m, num, 1);
        return -1;
    }

    /* Il padre guadagna un collegamento: il ".." della directory nuova
     * punta a lui. Dimenticarlo fa segnalare a e2fsck un conteggio
     * sbagliato su OGNI directory che contenga sottodirectory. */
    link_piu(m, pino);
    if (scrivi_inode(m, pnum, pino) != 0) return -1;

    return super_aggiorna(m);
}

int ext2_unlink(int mnt, const char *percorso)
{
    Ext2Mount *m = prendi_scrivibile(mnt);
    char       padre[EXT2_PERCORSO_MAX];
    const char *nome;
    uint8_t    pino[128], vino[128];
    uint32_t   len, pnum, num, link;

    if (m == NULL) return -1;

    len = separa(percorso, padre, EXT2_PERCORSO_MAX, &nome);
    if (len == 0) return -1;

    pnum = risolvi(m, mnt, padre, pino);
    if (pnum == 0) return -1;
    if ((le16(pino) & MODE_TIPO) != MODE_DIR) return -1;

    num = cerca_voce(m, mnt, pino, nome, len);
    if (num == 0) return -1;

    if (leggi_inode(m, num, vino) != 0) return -1;
    if ((le16(vino) & MODE_TIPO) == MODE_DIR) return -1;   /* per le dir c'e' rmdir */

    if (dir_rimuovi(m, mnt, pnum, pino, nome, len) == 0) return -1;

    /* I collegamenti si contano: un file puo' avere piu' nomi, e liberare
     * i blocchi al primo che sparisce cancellerebbe i dati sotto gli altri.
     * Questo driver non crea collegamenti multipli, ma un volume scritto
     * da Linux ne ha, e la regola vale comunque. */
    link = le16(vino + 26);
    if (link > 0) link--;
    p16(vino + 26, link);

    if (link == 0) {
        libera_tutti_i_blocchi(m, mnt, num, vino);
        p32(vino + 20, dtime_di(m->n_inode));    /* i_dtime: vedi sopra */
        if (scrivi_inode(m, num, vino) != 0) return -1;
        if (libera_inode(m, num, 0) != 0) return -1;
    } else {
        if (scrivi_inode(m, num, vino) != 0) return -1;
    }

    return super_aggiorna(m);
}

/* The ".." of directory `dino` (its first block, second entry) now names
 * `padre`. 0, or -1. */
static int dir_punta_padre(Ext2Mount *m, int mnt, uint32_t num, const uint8_t *dino,
                           uint32_t padre)
{
    uint32_t blk = mappa_blocco(m, mnt, dino, 0);
    uint32_t p;

    if (blk == 0 || leggi_blocco(m, blk, b_dati) != 0) return -1;
    p = le16(b_dati + 4);                       /* past "." */
    if (p < 12u || p + 12u > m->dim_blocco) return -1;
    if (b_dati[p + 6] != 2 || b_dati[p + 8] != '.' || b_dati[p + 9] != '.') return -1;
    p32(b_dati + p, padre);
    return dir_scrivi(m, num, dino, blk, b_dati);
}

/* ext2_rename across two directories (see the comment below). */
static int rinomina_altrove(Ext2Mount *m, int mnt, const char *da, const char *a,
                            uint32_t pnum, uint8_t *pino, const char *nom_da, uint32_t len_da,
                            uint32_t anum, uint8_t *aino, const char *nom_a, uint32_t len_a)
{
    uint8_t  vino[128];
    uint32_t num, tipo;
    int      e_dir;

    if (cerca_voce(m, mnt, aino, nom_a, len_a) != 0) return -2;
    num = cerca_voce(m, mnt, pino, nom_da, len_da);
    if (num == 0) return -1;
    if (leggi_inode(m, num, vino) != 0) return -1;
    e_dir = (le16(vino) & MODE_TIPO) == MODE_DIR;
    tipo  = e_dir ? DIRTIPO_DIR : DIRTIPO_FILE;

    /* A directory cannot go inside itself: "a" into "a/b/c" would cut the
     * whole subtree off the tree. The internal paths are normalised. */
    if (e_dir) {
        uint32_t k = 0;

        while (da[k] && da[k] == a[k]) k++;
        if (da[k] == '\0' && a[k] == '/') return -4;
    }

    if (dir_aggiungi(m, mnt, anum, aino, nom_a, len_a, num, tipo) != 0) return -1;
    if (e_dir) {
        if (dir_punta_padre(m, mnt, num, vino, anum) != 0) return -1;
        link_piu(m, aino);                      /* the new parent gains ".." */
    }
    if (scrivi_inode(m, anum, aino) != 0) return -1;

    if (dir_rimuovi(m, mnt, pnum, pino, nom_da, len_da) == 0) return -1;
    if (e_dir) link_meno(m, pino);              /* the old one loses it */
    if (scrivi_inode(m, pnum, pino) != 0) return -1;
    return super_aggiorna(m);
}

/* =============================================================================
 * ext2_rename — cambia il NOME di una voce, senza spostare i dati
 *
 * ! ANCHE FRA DIRECTORY DIVERSE dal kernel 0.260 (17 October 2026). Until
 * then it was -3: libgit2 writes every object to a temporary file in
 * .git/objects and MOVES it into .git/objects/xx/, and every commit failed
 * with ENOSYS. Moving a directory also rewrites its ".." and moves one link
 * from the old parent to the new one; a directory cannot go inside itself
 * (-4, EINVAL). The order is the same as below: the new name is written
 * (and the new parent's inode) before the old name goes, so a crash in
 * between leaves two names, never none.
 *
 * ! SI AGGIUNGE PRIMA E SI TOGLIE DOPO, e l'ordine e' l'unica cosa che
 * conta davvero qui. Fra le due operazioni il file ha DUE nomi: se la
 * corrente cade in quel momento, si ritrova con un nome di troppo — un
 * fastidio, riparabile. Nell'ordine opposto, in quella stessa finestra non
 * ne avrebbe NESSUNO: l'inode resterebbe allocato e irraggiungibile, cioe'
 * dati persi che occupano spazio.
 *
 * ! IL CONTEGGIO DEI COLLEGAMENTI NON SI TOCCA: si aggiunge un nome e se
 * ne toglie un altro, quindi alla fine sono sempre uno. Incrementarlo e
 * decrementarlo "per simmetria" darebbe lo stesso risultato solo se
 * entrambe le operazioni riuscissero.
 *
 * Il senso di tutto questo sta in `install`: i dati non si spostano,
 * quindi la mappa dei settori verificata prima della rinomina resta valida
 * dopo. Vedi kernel/fs/fat.c per la versione FAT e lo stesso ragionamento.
 * ============================================================================= */
int ext2_rename(int mnt, const char *da, const char *a)
{
    Ext2Mount  *m = prendi_scrivibile(mnt);
    char        pad_da[EXT2_PERCORSO_MAX], pad_a[EXT2_PERCORSO_MAX];
    const char *nom_da, *nom_a;
    uint8_t     pino[128], vino[128];
    uint32_t    len_da, len_a, pnum, num, tipo;
    int         i;

    if (m == NULL) return -1;

    len_da = separa(da, pad_da, EXT2_PERCORSO_MAX, &nom_da);
    len_a  = separa(a,  pad_a,  EXT2_PERCORSO_MAX, &nom_a);
    if (len_da == 0 || len_a == 0) return -1;

    pnum = risolvi(m, mnt, pad_da, pino);
    if (pnum == 0) return -1;
    if ((le16(pino) & MODE_TIPO) != MODE_DIR) return -1;

    for (i = 0; pad_da[i] || pad_a[i]; i++) {
        if (pad_da[i] != pad_a[i]) break;
    }
    if (pad_da[i] || pad_a[i]) {
        uint8_t  aino[128];
        uint32_t anum = risolvi(m, mnt, pad_a, aino);

        if (anum == 0) return -1;
        if ((le16(aino) & MODE_TIPO) != MODE_DIR) return -1;
        /* Two spellings of the same directory: an ordinary rename, on ONE
         * copy of its inode. */
        if (anum != pnum) return rinomina_altrove(m, mnt, da, a, pnum, pino,
                                                  nom_da, len_da, anum, aino,
                                                  nom_a, len_a);
    }

    /* La destinazione non deve esistere: chi vuole sostituire cancella
     * prima, cosi' la perdita e' una scelta e non un effetto collaterale. */
    if (cerca_voce(m, mnt, pino, nom_a, len_a) != 0) return -2;

    num = cerca_voce(m, mnt, pino, nom_da, len_da);
    if (num == 0) return -1;

    if (leggi_inode(m, num, vino) != 0) return -1;
    tipo = ((le16(vino) & MODE_TIPO) == MODE_DIR) ? DIRTIPO_DIR : DIRTIPO_FILE;

    if (dir_aggiungi(m, mnt, pnum, pino, nom_a, len_a, num, tipo) != 0) return -1;
    if (dir_rimuovi(m, mnt, pnum, pino, nom_da, len_da) == 0) return -1;

    if (scrivi_inode(m, pnum, pino) != 0) return -1;
    return super_aggiorna(m);
}

int ext2_rmdir(int mnt, const char *percorso)
{
    Ext2Mount *m = prendi_scrivibile(mnt);
    char       padre[EXT2_PERCORSO_MAX];
    const char *nome;
    uint8_t    pino[128], dino[128];
    uint32_t   len, pnum, num;

    if (m == NULL) return -1;

    len = separa(percorso, padre, EXT2_PERCORSO_MAX, &nome);
    if (len == 0) return -1;

    pnum = risolvi(m, mnt, padre, pino);
    if (pnum == 0) return -1;

    num = cerca_voce(m, mnt, pino, nome, len);
    if (num == 0) return -1;

    if (leggi_inode(m, num, dino) != 0) return -1;
    if ((le16(dino) & MODE_TIPO) != MODE_DIR) return -1;

    /* Non vuota: -2, che il VFS traduce in ENOTEMPTY. Cancellare
     * ricorsivamente da qui renderebbe irraggiungibili i file dentro senza
     * che nessuno lo abbia chiesto. */
    if (!dir_vuota(m, mnt, dino)) return -2;

    if (dir_rimuovi(m, mnt, pnum, pino, nome, len) == 0) return -1;

    libera_tutti_i_blocchi(m, mnt, num, dino);
    p16(dino + 26, 0);
    p32(dino + 20, dtime_di(m->n_inode));       /* i_dtime: vedi sopra */
    if (scrivi_inode(m, num, dino) != 0) return -1;
    if (libera_inode(m, num, 1) != 0) return -1;

    /* Il padre perde il collegamento che era il ".." della directory
     * cancellata. */
    link_meno(m, pino);
    if (scrivi_inode(m, pnum, pino) != 0) return -1;

    return super_aggiorna(m);
}

/* =============================================================================
 * SIZES THIS DRIVER CAN WRITE (0.263)
 *
 * It counts a file's size in 32 bits. A file already above 4 GB (written
 * by Linux: i_size_high, offset 108, is not zero) is refused - writing it
 * would cut its size to the low half. And a volume without LARGE_FILE may
 * not hold a file of 2 GB or more: e2fsck would report every one.
 * ============================================================================= */
static int dimensione_scrivibile(Ext2Mount *m, const uint8_t *inode, uint64_t fine)
{
    uint64_t max = (m->ro_compat & RO_COMPAT_LARGEFILE) ? 0xFFFFFFFFull : 0x7FFFFFFFull;

    if (le32(inode + 108) != 0 || le16(inode + 0x74) != 0) {
        klog(LOG_WARN, "EXT2: a file above 4 GB is not written by this driver");
        return 0;
    }
    if (le32(inode + 32) & 0x00040000u) {       /* EXT4_HUGE_FILE_FL: i_blocks in blocks */
        klog(LOG_WARN, "ext4: a huge_file inode is not written by this driver");
        return 0;
    }
    if (fine > max) {
        klog(LOG_WARN, "EXT2: %s: the file would pass %s", "write refused",
             max == 0xFFFFFFFFull ? "4 GB" : "2 GB (the volume has no large_file)");
        return 0;
    }
    return 1;
}

int ext2_write(int mnt, const char *percorso, const void *buf, uint32_t size,
               uint32_t offset)
{
    Ext2Mount     *m = prendi_scrivibile(mnt);
    const uint8_t *src = (const uint8_t *)buf;
    uint8_t        inode[128];
    uint32_t       num, dim, scritti = 0, charge = 0, gruppo;
    int            esito_ind = 0, in_coda;

    if (m == NULL || buf == NULL) return -1;
    if (size == 0) return 0;

    num = risolvi(m, mnt, percorso, inode);
    if (num == 0) return -1;
    if ((le16(inode) & MODE_TIPO) != MODE_FILE) return -1;
    if (!dimensione_scrivibile(m, inode, (uint64_t)offset + size)) return -1;

    dim    = le32(inode + 4);
    gruppo = (num - 1u) / m->inode_per_gruppo;
    in_coda = (uint64_t)offset + size > dim;

    /* I prenotati di un altro file, o di un altro volume, tornano liberi
     * prima: alloca_blocco li consegnerebbe a questo. */
    if (g_pre_mnt >= 0 && (g_pre_mnt != mnt || g_pre_ino != num)) prenotazione_chiudi();

    /* Vedi «SCRIVERE COSTAVA NOVE COMANDI» in testa al file. Da qui in poi
     * NON SI ESCE CON return: si va a `fine`, che rimette tutto a posto. */
    {
        uint32_t primo_log  = offset / m->dim_blocco;
        uint32_t ultimo_log = (offset + size - 1u) / m->dim_blocco;
        uint32_t gia        = (dim + m->dim_blocco - 1u) / m->dim_blocco;  /* blocchi che il file ha */
        uint32_t servono, voglia;

        if (primo_log < gia) primo_log = gia;
        servono = ultimo_log >= primo_log ? ultimo_log - primo_log + 1u : 0;
        if (servono > CORSA_MAX) servono = CORSA_MAX;

        /* In coda si prenota anche per le write() che verranno: vedi «LA
         * PRENOTAZIONE RESTA». Se ne restano abbastanza dalla volta prima,
         * non si tocca la bitmap affatto. */
        voglia = servono;
        if (in_coda && servono > 0) {
            uint32_t anticipo = ANTICIPO_BYTE / m->dim_blocco;

            if (anticipo > CORSA_MAX) anticipo = CORSA_MAX;
            if (voglia < anticipo) voglia = anticipo;
        }
        if (servono > 0 && g_pre_n < servono) {
            prenotati_rilascia(m);
            prenota(m, gruppo, voglia);
            if (g_pre_n > 0) { g_pre_mnt = mnt; g_pre_ino = num; }
        }
    }
    g_dati_zero = 0;
    g_ind_diff  = 1;

    while (scritti < size) {
        uint32_t log = (offset + scritti) / m->dim_blocco;
        uint32_t in  = (offset + scritti) % m->dim_blocco;
        uint32_t q   = m->dim_blocco - in;
        uint32_t blk, k, prima = charge;
        int      nuovo;

        if (q > size - scritti) q = size - scritti;

        blk = mappa_o_alloca(m, mnt, num, inode, log, gruppo, &charge);
        if (blk == 0) break;                    /* volume pieno */
        nuovo = (charge != prima);              /* nato adesso: sul disco non e' azzerato */

        /* Blocchi interi contigui sul disco: un comando solo, dritti dal
         * buffer di chi scrive (0.235). */
        if (in == 0 && q == m->dim_blocco) {
            uint32_t n = 1, max = (size - scritti) / m->dim_blocco;

            if (max > CORSA_MAX) max = CORSA_MAX;
            while (n < max) {
                uint32_t b2 = mappa_o_alloca(m, mnt, num, inode, log + n, gruppo, &charge);
                if (b2 != blk + n) {
                    /* Allocato (o gia' li') ma non contiguo: lo scrivera'
                     * il giro dopo, che lo ritrova nella mappa. */
                    break;
                }
                n++;
            }
            if (scrivi_corsa(m, blk, n, src + scritti) != 0) break;
            scritti += n * m->dim_blocco;
            continue;
        }

        /* Lettura-modifica-scrittura solo se si scrive un pezzo di
         * blocco. Un blocco intero si sovrascrive e basta: rileggerlo
         * prima sarebbe una lettura buttata su ogni blocco di ogni file. */
        if (q < m->dim_blocco && !nuovo) {
            if (leggi_blocco(m, blk, b_dati) != 0) break;
        } else {
            /* Intero, o appena nato: il resto del blocco sono zeri, e li
             * mettiamo noi - il blocco nuovo non e' stato azzerato sul disco. */
            for (k = 0; k < m->dim_blocco; k++) b_dati[k] = 0;
        }

        for (k = 0; k < q; k++) b_dati[in + k] = src[scritti + k];

        /* A file's DATA: straight to the disk, not into an ext3
         * transaction (ordered mode, giornale.c) - scrivi_corsa of one. */
        if (scrivi_corsa(m, blk, 1u, b_dati) != 0) break;
        scritti += q;
    }

    /* ! QUI SI RIMETTE IL DISCO COERENTE, comunque sia andata: i puntatori
     * rimasti in memoria vanno scritti, i blocchi prenotati e non usati
     * tornano liberi, e chi alloca dopo di noi ritrova i blocchi azzerati. */
    g_dati_zero = 1;
    g_ind_diff  = 0;
    if (ind_riversa(m) != 0) esito_ind = -1;
    /* In coda e andata bene: i prenotati restano per la write() dopo. */
    if (!in_coda || scritti < size || esito_ind != 0) prenotati_rilascia(m);
    if (g_pre_n == 0) g_pre_mnt = -1;

    if (scritti == 0) return -1;
    if (esito_ind != 0) return -1;

    {
        uint32_t prima_mtime = le32(inode + 16), adesso = unix_ora_corrente();
        int      cambiato = charge != 0 || offset + scritti > dim ||
                            (adesso != 0 && adesso != prima_mtime);

        /* ! L'INODE E IL SUPERBLOCCO SI RISCRIVONO SOLO SE SONO CAMBIATI
         * (0.235): erano due scritture in piu' per OGNI write(), e una
         * pagina di SQLite riscritta al suo posto non tocca ne' la lunghezza
         * ne' i blocchi liberi. La data si guarda al secondo, come la tiene
         * ext2. */
        if (!cambiato) return (int)scritti;
    }

    if (offset + scritti > dim) p32(inode + 4, offset + scritti);
    p32(inode + 28, le32(inode + 28) + charge * (m->dim_blocco / 512u));

    /* ! LA DATA DI MODIFICA SI AGGIORNA QUI, e non dentro scrivi_inode():
     * quella riscrive l'inode anche per le directory padre e per operazioni
     * che il contenuto del file non lo toccano. Datare li' vorrebbe dire
     * che creare un file dentro una directory "modifica" ogni file che c'e'
     * dentro, e make ricostruirebbe il mondo a ogni giro.
     *
     * Zero resta zero se l'orologio non ha risposto: vedi
     * unix_ora_corrente. */
    {
        uint32_t adesso = unix_ora_corrente();

        if (adesso != 0) p32(inode + 16, adesso);
    }

    if (scrivi_inode(m, num, inode) != 0) return -1;
    /* Il superblocco alla chiusura: vedi «LA PRENOTAZIONE RESTA». */
    if (charge) g_super_sporco[mnt] = 1;

    return (int)scritti;
}

int ext2_truncate(int mnt, const char *percorso, uint32_t nuova_dim)
{
    Ext2Mount *m = prendi_scrivibile(mnt);
    uint8_t    inode[128];
    uint32_t   num, dim, tenere, coda;

    if (m == NULL) return -1;

    num = risolvi(m, mnt, percorso, inode);
    if (num == 0) return -1;
    if ((le16(inode) & MODE_TIPO) != MODE_FILE) return -1;
    if (!dimensione_scrivibile(m, inode, nuova_dim)) return -1;

    dim = le32(inode + 4);

    /* --- ALLUNGARE ---------------------------------------------------
     * Non si allocano blocchi: basta dichiarare la dimensione nuova. Lo
     * spazio in mezzo diventa un BUCO, e i buchi in ext2 sono legittimi —
     * mappa_blocco() ritorna 0 e ext2_read() consegna zeri. Allocare
     * davvero occuperebbe spazio per contenuto che nessuno ha scritto. */
    if (nuova_dim >= dim) {
        if (nuova_dim == dim) return 0;
        p32(inode + 4, nuova_dim);
        return scrivi_inode(m, num, inode);
    }

    /* --- ACCORCIARE ---------------------------------------------------
     * `tenere` e' il numero di blocchi logici che restano: quelli da
     * `tenere` in poi se ne vanno. L'arrotondamento e' in SU, non in giu':
     * con nuova_dim = 1500 e blocchi da 1024 servono DUE blocchi, perche'
     * il secondo contiene i byte da 1024 a 1499. Arrotondare in giu'
     * libererebbe un blocco ancora dentro il file. */
    tenere = (nuova_dim + m->dim_blocco - 1u) / m->dim_blocco;

    if (pota_inode(m, mnt, num, inode, tenere) != 0) return -1;

    /* La CODA dell'ultimo blocco che resta va azzerata. Quei byte sono
     * oltre i_size e nessuno li legge — finche' il file non viene
     * riallungato, e allora ricomparirebbero come contenuto. Un file
     * troncato e poi riesteso deve dare zeri, non i propri dati vecchi. */
    coda = nuova_dim % m->dim_blocco;
    if (coda != 0) {
        uint32_t blk = mappa_blocco(m, mnt, inode, nuova_dim / m->dim_blocco);

        if (blk != 0) {
            uint32_t k;
            if (leggi_blocco(m, blk, b_dati) != 0) return -1;
            for (k = coda; k < m->dim_blocco; k++) b_dati[k] = 0;
            if (scrivi_corsa(m, blk, 1u, b_dati) != 0) return -1;     /* data */
        }
    }

    p32(inode + 4, nuova_dim);

    if (scrivi_inode(m, num, inode) != 0) return -1;
    return super_aggiorna(m);
}

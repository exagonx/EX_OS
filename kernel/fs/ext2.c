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
#include "rtc.h"

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
    uint32_t inode_liberi;
    uint32_t primo_inode;          /* s_first_ino: sotto sono riservati */
    uint32_t punt_per_blocco;      /* dim_blocco / 4 */
    uint32_t gdt_primo;            /* primo blocco della tabella descrittori */

    char     etichetta[17];
} Ext2Mount;

static Ext2Mount g_mnt[EXT2_MAX_MOUNT];

/* =============================================================================
 * I BUFFER DI LAVORO, E PERCHE' SONO CINQUE E NON UNO
 *
 * Sono globali e non per montaggio perche' le syscall di EX-OS non si
 * annidano: un'operazione finisce prima che cominci la successiva. Se un
 * giorno il kernel diventasse rientrante andrebbero dentro Ext2Mount, ed
 * e' scritto qui perche' quel giorno sia una modifica e non una caccia.
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
 * ============================================================================= */
static uint8_t  b_dati[BLOCCO_MAX];     /* dati di file, o blocco di directory */
static uint8_t  b_ind [BLOCCO_MAX];     /* blocco di puntatori */
static uint8_t  b_ino [BLOCCO_MAX];     /* blocco della tabella degli inode */
static uint8_t  b_bmp [BLOCCO_MAX];     /* bitmap di blocchi o di inode */
static uint8_t  b_desc[BLOCCO_MAX];     /* tabella dei descrittori di gruppo */

static uint32_t g_ind_num  = 0;         /* quale blocco c'e' dentro b_ind */
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

static void p16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

static void p32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
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
    if (g_ind_num == n) g_ind_mnt = -1;

    return blk_write(m->blkdev, (uint64_t)n * m->sett_per_blocco,
                     m->sett_per_blocco, src);
}

/* Blocco di puntatori, con memoria dell'ultimo. Vedi la nota sulla cache
 * in testa al file. */
static int leggi_indiretto(Ext2Mount *m, int mnt, uint32_t n)
{
    if (g_ind_mnt == mnt && g_ind_num == n) return 0;

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
    desc_blk = m->gdt_primo + (gruppo * 32u) / m->dim_blocco;
    desc_off = (gruppo * 32u) % m->dim_blocco;

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
    uint32_t blk = m->gdt_primo + (g * 32u) / m->dim_blocco;
    uint32_t off = (g * 32u) % m->dim_blocco;

    if (g >= m->n_gruppi) return -1;
    if (leggi_blocco(m, blk, b_desc) != 0) return -1;

    *off_out = off;
    *blk_out = blk;
    return 0;
}

/* Somma `d` (con segno) a un contatore a 16 bit del descrittore di gruppo
 * e lo riscrive. `campo` e' l'offset dentro la voce: 12 blocchi liberi,
 * 14 inode liberi, 16 directory. */
static int desc_somma(Ext2Mount *m, uint32_t g, uint32_t campo, int d)
{
    uint32_t off, blk, v;

    if (desc_carica(m, g, &off, &blk) != 0) return -1;

    v = le16(b_desc + off + campo);
    if (d < 0 && v < (uint32_t)(-d)) {
        klog(LOG_ERROR, "EXT2: contatore del gruppo %u sotto zero: volume "
                        "incoerente, operazione annullata", g);
        return -1;
    }
    v = (uint32_t)((int)v + d);

    b_desc[off + campo]     = (uint8_t)v;
    b_desc[off + campo + 1] = (uint8_t)(v >> 8);

    return scrivi_blocco(m, blk, b_desc);
}

/* Riscrive i due contatori del superblocco. Il superblocco sta all'offset
 * 1024 del volume, che con blocchi da 1024 e' il blocco 1 e con blocchi
 * piu' grandi e' DENTRO il blocco 0: si lavora sui settori, non sui
 * blocchi, cosi' il caso non si pone. */
static int super_aggiorna(Ext2Mount *m)
{
    uint8_t sb[1024];

    if (blk_read(m->blkdev, SUPER_OFFSET / 512u, 2u, sb) != 0) return -1;

    sb[12] = (uint8_t)m->blocchi_liberi;
    sb[13] = (uint8_t)(m->blocchi_liberi >> 8);
    sb[14] = (uint8_t)(m->blocchi_liberi >> 16);
    sb[15] = (uint8_t)(m->blocchi_liberi >> 24);

    sb[16] = (uint8_t)m->inode_liberi;
    sb[17] = (uint8_t)(m->inode_liberi >> 8);
    sb[18] = (uint8_t)(m->inode_liberi >> 16);
    sb[19] = (uint8_t)(m->inode_liberi >> 24);

    return blk_write(m->blkdev, SUPER_OFFSET / 512u, 2u, sb);
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
static uint32_t alloca_blocco(Ext2Mount *m, uint32_t pref)
{
    uint32_t giro, g, off, dblk, bmp, lim, i, n;

    if (pref >= m->n_gruppi) pref = 0;

    for (giro = 0; giro < m->n_gruppi; giro++) {
        g = (pref + giro) % m->n_gruppi;

        if (desc_carica(m, g, &off, &dblk) != 0) return 0;
        if (le16(b_desc + off + 12) == 0) continue;     /* gruppo pieno */

        bmp = le32(b_desc + off + 0);
        if (leggi_blocco(m, bmp, b_bmp) != 0) return 0;

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

        if (desc_somma(m, g, 12, -1) != 0) return 0;
        m->blocchi_liberi--;

        n = m->primo_dato + g * m->blocchi_per_gruppo + i;

        /* Un blocco nuovo si azzera SEMPRE. Contiene i byte di chi lo
         * usava prima: consegnarlo com'e' significa che il contenuto di un
         * file cancellato ricompare dentro un file nuovo, e come blocco di
         * puntatori quei byte sarebbero indirizzi verso mezzo volume. */
        {
            uint32_t k;
            for (k = 0; k < m->dim_blocco; k++) b_bmp[k] = 0;
            if (scrivi_blocco(m, n, b_bmp) != 0) return 0;
        }

        return n;
    }

    return 0;
}

static int libera_blocco(Ext2Mount *m, uint32_t n)
{
    uint32_t g, i, off, dblk, bmp;

    if (n < m->primo_dato || n >= m->n_blocchi) return -1;

    g = (n - m->primo_dato) / m->blocchi_per_gruppo;
    i = (n - m->primo_dato) % m->blocchi_per_gruppo;

    if (desc_carica(m, g, &off, &dblk) != 0) return -1;
    bmp = le32(b_desc + off + 0);

    if (leggi_blocco(m, bmp, b_bmp) != 0) return -1;

    /* Liberare due volte lo stesso blocco lo farebbe contare due volte fra
     * i liberi, e da li' in poi l'allocatore consegnerebbe lo stesso
     * blocco a due file. Meglio non fare niente e dirlo. */
    if ((b_bmp[i / 8u] & (1u << (i % 8u))) == 0) {
        klog(LOG_WARN, "EXT2: blocco %u gia' libero: non lo libero due volte", n);
        return 0;
    }

    b_bmp[i / 8u] &= (uint8_t)~(1u << (i % 8u));
    if (scrivi_blocco(m, bmp, b_bmp) != 0) return -1;

    if (desc_somma(m, g, 12, +1) != 0) return -1;
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
        if (le16(b_desc + off + 14) == 0) continue;

        bmp = le32(b_desc + off + 4);
        if (leggi_blocco(m, bmp, b_bmp) != 0) return 0;

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

        if (desc_somma(m, g, 14, -1) != 0) return 0;
        if (is_dir && desc_somma(m, g, 16, +1) != 0) return 0;
        m->inode_liberi--;

        return n;
    }

    return 0;
}

static int libera_inode(Ext2Mount *m, uint32_t n, int era_dir)
{
    uint32_t g, i, off, dblk, bmp;

    if (n == 0 || n > m->n_inode) return -1;

    g = (n - 1u) / m->inode_per_gruppo;
    i = (n - 1u) % m->inode_per_gruppo;

    if (desc_carica(m, g, &off, &dblk) != 0) return -1;
    bmp = le32(b_desc + off + 4);

    if (leggi_blocco(m, bmp, b_bmp) != 0) return -1;

    if ((b_bmp[i / 8u] & (1u << (i % 8u))) == 0) {
        klog(LOG_WARN, "EXT2: inode %u gia' libero", n);
        return 0;
    }

    b_bmp[i / 8u] &= (uint8_t)~(1u << (i % 8u));
    if (scrivi_blocco(m, bmp, b_bmp) != 0) return -1;

    if (desc_somma(m, g, 14, +1) != 0) return -1;
    if (era_dir && desc_somma(m, g, 16, -1) != 0) return -1;
    m->inode_liberi++;
    return 0;
}

/* Riscrive i 128 byte dell'inode `numero`. Lettura-modifica-scrittura del
 * blocco che lo contiene: su un volume con inode da 256 byte i 128 che non
 * tocchiamo appartengono comunque a quell'inode (marche temporali fini,
 * attributi estesi) e azzerarli lo danneggerebbe. */
static int scrivi_inode(Ext2Mount *m, uint32_t numero, const uint8_t *src)
{
    uint32_t gruppo, indice, off, dblk, itab, blk, ioff, i;

    if (numero == 0 || numero > m->n_inode) return -1;

    gruppo = (numero - 1u) / m->inode_per_gruppo;
    indice = (numero - 1u) % m->inode_per_gruppo;

    if (desc_carica(m, gruppo, &off, &dblk) != 0) return -1;
    itab = le32(b_desc + off + 8);

    blk  = itab + (indice * m->dim_inode) / m->dim_blocco;
    ioff = (indice * m->dim_inode) % m->dim_blocco;

    if (leggi_blocco(m, blk, b_ino) != 0) return -1;
    for (i = 0; i < 128u; i++) b_ino[ioff + i] = src[i];

    return scrivi_blocco(m, blk, b_ino);
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

    if (scrivi_blocco(m, blk, b_ind) != 0) { libera_blocco(m, p); return 0; }

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

static uint32_t mappa_o_alloca(Ext2Mount *m, int mnt, uint8_t *inode,
                               uint32_t n, uint32_t pref, uint32_t *charge)
{
    uint32_t ppb = m->punt_per_blocco;
    uint32_t l1, l2;

    if (n < 12u) return inode_punt_o_alloca(m, inode, n, pref, charge);
    n -= 12u;

    if (n < ppb) {
        l1 = inode_punt_o_alloca(m, inode, 12u, pref, charge);
        if (l1 == 0) return 0;
        return punt_o_alloca(m, mnt, l1, n, pref, charge);
    }
    n -= ppb;

    if (n < ppb * ppb) {
        l1 = inode_punt_o_alloca(m, inode, 13u, pref, charge);
        if (l1 == 0) return 0;
        l2 = punt_o_alloca(m, mnt, l1, n / ppb, pref, charge);
        if (l2 == 0) return 0;
        return punt_o_alloca(m, mnt, l2, n % ppb, pref, charge);
    }
    n -= ppb * ppb;

    l1 = inode_punt_o_alloca(m, inode, 14u, pref, charge);
    if (l1 == 0) return 0;
    l2 = punt_o_alloca(m, mnt, l1, n / (ppb * ppb), pref, charge);
    if (l2 == 0) return 0;
    l2 = punt_o_alloca(m, mnt, l2, (n / ppb) % ppb, pref, charge);
    if (l2 == 0) return 0;
    return punt_o_alloca(m, mnt, l2, n % ppb, pref, charge);
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

static int libera_tutti_i_blocchi(Ext2Mount *m, int mnt, uint8_t *inode)
{
    uint32_t i, liberati = 0;

    for (i = 0; i < 12u; i++) {
        uint32_t p = le32(inode + 40u + i * 4u);
        if (p != 0) libera_catena(m, mnt, p, 0, &liberati);
    }

    libera_catena(m, mnt, le32(inode + 40u + 12u * 4u), 1, &liberati);
    libera_catena(m, mnt, le32(inode + 40u + 13u * 4u), 2, &liberati);
    libera_catena(m, mnt, le32(inode + 40u + 14u * 4u), 3, &liberati);

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
static int pota_inode(Ext2Mount *m, int mnt, uint8_t *inode, uint32_t primo)
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
            if (leggi_blocco(m, blk, b_dati) != 0) break;
            for (k = 0; k < q; k++) dst[letti + k] = b_dati[in + k];
        }

        letti += q;
    }

    return (int)letti;
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

#define DIRTIPO_FILE    1u

/* Byte davvero occupati da una voce col nome lungo `len`, arrotondati a 4.
 * L'allineamento non e' cosmetico: rec_len deve essere multiplo di 4 o i
 * campi a 32 bit della voce successiva finiscono disallineati, e diversi
 * driver li leggono male. */
static uint32_t voce_lunghezza(uint32_t len)
{
    return (8u + len + 3u) & ~3u;
}

/* Aggiunge una voce alla directory `dir_inode` (numero `dir_num`).
 * Ritorna 0, o <0. */
static int dir_aggiungi(Ext2Mount *m, int mnt, uint32_t dir_num,
                        uint8_t *dir_inode, const char *nome, uint32_t len,
                        uint32_t ino, uint32_t tipo)
{
    uint32_t dim     = le32(dir_inode + 4);
    uint32_t serve   = voce_lunghezza(len);
    uint32_t off, k;

    for (off = 0; off < dim; off += m->dim_blocco) {
        uint32_t blk = mappa_blocco(m, mnt, dir_inode, off / m->dim_blocco);
        uint32_t p   = 0;

        if (blk == 0) continue;
        if (leggi_blocco(m, blk, b_dati) != 0) return -1;

        while (p < m->dim_blocco) {
            uint32_t vino = le32(b_dati + p);
            uint32_t rec  = le16(b_dati + p + 4);
            uint32_t vlen = b_dati[p + 6];
            uint32_t usato;

            if (rec < 8u || p + rec > m->dim_blocco) break;

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

                return scrivi_blocco(m, blk, b_dati);
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
        uint32_t nuovo_blk = mappa_o_alloca(m, mnt, dir_inode,
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

        if (scrivi_blocco(m, nuovo_blk, b_dati) != 0) return -1;

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
static uint32_t dir_rimuovi(Ext2Mount *m, int mnt, uint8_t *dir_inode,
                            const char *nome, uint32_t len)
{
    uint32_t dim = le32(dir_inode + 4);
    uint32_t off;

    for (off = 0; off < dim; off += m->dim_blocco) {
        uint32_t blk  = mappa_blocco(m, mnt, dir_inode, off / m->dim_blocco);
        uint32_t p    = 0;
        uint32_t prec = 0xFFFFFFFFu;

        if (blk == 0) continue;
        if (leggi_blocco(m, blk, b_dati) != 0) return 0;

        while (p < m->dim_blocco) {
            uint32_t vino = le32(b_dati + p);
            uint32_t rec  = le16(b_dati + p + 4);
            uint32_t vlen = b_dati[p + 6];

            if (rec < 8u || p + rec > m->dim_blocco) break;

            if (vino != 0 && nome_uguale(b_dati + p + 8, vlen, nome, len)) {
                if (prec == 0xFFFFFFFFu) {
                    p32(b_dati + p, 0);
                } else {
                    p16(b_dati + prec + 4, le16(b_dati + prec + 4) + rec);
                }
                if (scrivi_blocco(m, blk, b_dati) != 0) return 0;
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

/* Compone un inode nuovo. `modo` comprende il tipo. */
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
    Ext2Mount *m = prendi(mnt);
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
    Ext2Mount *m = prendi(mnt);
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
    Ext2Mount *m = prendi(mnt);
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
    if (scrivi_inode(m, num, nino) != 0) { libera_inode(m, num, 0); return -1; }

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

int ext2_mkdir(int mnt, const char *percorso, uint32_t uid, uint32_t gid)
{
    Ext2Mount *m = prendi(mnt);
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

    if (scrivi_blocco(m, blk, b_dati) != 0) {
        libera_blocco(m, blk); libera_inode(m, num, 1);
        return -1;
    }

    /* DUE collegamenti: il nome nel padre e il "." dentro di se'. */
    /* ! 0755 SU UNA DIRECTORY, e la differenza con 0644 di un file conta: il
     * bit x su una directory non vuol dire «eseguibile», vuol dire
     * «attraversabile». Senza, nessuno potrebbe entrarci nemmeno per leggere
     * un file che gli appartiene. */
    inode_nuovo(nino, MODE_DIR | 0755u, 2, m->dim_blocco, m->dim_blocco / 512u,
                uid, gid);
    p32(nino + 40, blk);

    if (scrivi_inode(m, num, nino) != 0) {
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
    p16(pino + 26, le16(pino + 26) + 1u);
    if (scrivi_inode(m, pnum, pino) != 0) return -1;

    return super_aggiorna(m);
}

int ext2_unlink(int mnt, const char *percorso)
{
    Ext2Mount *m = prendi(mnt);
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

    if (dir_rimuovi(m, mnt, pino, nome, len) == 0) return -1;

    /* I collegamenti si contano: un file puo' avere piu' nomi, e liberare
     * i blocchi al primo che sparisce cancellerebbe i dati sotto gli altri.
     * Questo driver non crea collegamenti multipli, ma un volume scritto
     * da Linux ne ha, e la regola vale comunque. */
    link = le16(vino + 26);
    if (link > 0) link--;
    p16(vino + 26, link);

    if (link == 0) {
        libera_tutti_i_blocchi(m, mnt, vino);
        p32(vino + 20, dtime_di(m->n_inode));    /* i_dtime: vedi sopra */
        if (scrivi_inode(m, num, vino) != 0) return -1;
        if (libera_inode(m, num, 0) != 0) return -1;
    } else {
        if (scrivi_inode(m, num, vino) != 0) return -1;
    }

    return super_aggiorna(m);
}

/* =============================================================================
 * ext2_rename — cambia il NOME di una voce, senza spostare i dati
 *
 * ! SOLO NELLA STESSA DIRECTORY (-3 altrimenti). Attraversare directory
 * significherebbe anche aggiornare ".." per le directory e il conteggio
 * dei collegamenti di due padri: piu' cose che devono riuscire tutte, in
 * un filesystem senza journal. Il caso che serve e' rinominare sul posto.
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
    Ext2Mount  *m = prendi(mnt);
    char        pad_da[EXT2_PERCORSO_MAX], pad_a[EXT2_PERCORSO_MAX];
    const char *nom_da, *nom_a;
    uint8_t     pino[128], vino[128];
    uint32_t    len_da, len_a, pnum, num, tipo;
    int         i;

    if (m == NULL) return -1;

    len_da = separa(da, pad_da, EXT2_PERCORSO_MAX, &nom_da);
    len_a  = separa(a,  pad_a,  EXT2_PERCORSO_MAX, &nom_a);
    if (len_da == 0 || len_a == 0) return -1;

    for (i = 0; pad_da[i] || pad_a[i]; i++) {
        if (pad_da[i] != pad_a[i]) return -3;
    }

    pnum = risolvi(m, mnt, pad_da, pino);
    if (pnum == 0) return -1;
    if ((le16(pino) & MODE_TIPO) != MODE_DIR) return -1;

    /* La destinazione non deve esistere: chi vuole sostituire cancella
     * prima, cosi' la perdita e' una scelta e non un effetto collaterale. */
    if (cerca_voce(m, mnt, pino, nom_a, len_a) != 0) return -2;

    num = cerca_voce(m, mnt, pino, nom_da, len_da);
    if (num == 0) return -1;

    if (leggi_inode(m, num, vino) != 0) return -1;
    tipo = ((le16(vino) & MODE_TIPO) == MODE_DIR) ? DIRTIPO_DIR : DIRTIPO_FILE;

    if (dir_aggiungi(m, mnt, pnum, pino, nom_a, len_a, num, tipo) != 0) return -1;
    if (dir_rimuovi(m, mnt, pino, nom_da, len_da) == 0) return -1;

    if (scrivi_inode(m, pnum, pino) != 0) return -1;
    return super_aggiorna(m);
}

int ext2_rmdir(int mnt, const char *percorso)
{
    Ext2Mount *m = prendi(mnt);
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

    if (dir_rimuovi(m, mnt, pino, nome, len) == 0) return -1;

    libera_tutti_i_blocchi(m, mnt, dino);
    p16(dino + 26, 0);
    p32(dino + 20, dtime_di(m->n_inode));       /* i_dtime: vedi sopra */
    if (scrivi_inode(m, num, dino) != 0) return -1;
    if (libera_inode(m, num, 1) != 0) return -1;

    /* Il padre perde il collegamento che era il ".." della directory
     * cancellata. */
    {
        uint32_t l = le16(pino + 26);
        if (l > 0) l--;
        p16(pino + 26, l);
    }
    if (scrivi_inode(m, pnum, pino) != 0) return -1;

    return super_aggiorna(m);
}

int ext2_write(int mnt, const char *percorso, const void *buf, uint32_t size,
               uint32_t offset)
{
    Ext2Mount     *m = prendi(mnt);
    const uint8_t *src = (const uint8_t *)buf;
    uint8_t        inode[128];
    uint32_t       num, dim, scritti = 0, charge = 0, gruppo;

    if (m == NULL || buf == NULL) return -1;
    if (size == 0) return 0;

    num = risolvi(m, mnt, percorso, inode);
    if (num == 0) return -1;
    if ((le16(inode) & MODE_TIPO) != MODE_FILE) return -1;

    dim    = le32(inode + 4);
    gruppo = (num - 1u) / m->inode_per_gruppo;

    while (scritti < size) {
        uint32_t log = (offset + scritti) / m->dim_blocco;
        uint32_t in  = (offset + scritti) % m->dim_blocco;
        uint32_t q   = m->dim_blocco - in;
        uint32_t blk, k;

        if (q > size - scritti) q = size - scritti;

        blk = mappa_o_alloca(m, mnt, inode, log, gruppo, &charge);
        if (blk == 0) break;                    /* volume pieno */

        /* Lettura-modifica-scrittura solo se si scrive un pezzo di
         * blocco. Un blocco intero si sovrascrive e basta: rileggerlo
         * prima sarebbe una lettura buttata su ogni blocco di ogni file. */
        if (q < m->dim_blocco) {
            if (leggi_blocco(m, blk, b_dati) != 0) break;
        } else {
            for (k = 0; k < m->dim_blocco; k++) b_dati[k] = 0;
        }

        for (k = 0; k < q; k++) b_dati[in + k] = src[scritti + k];

        if (scrivi_blocco(m, blk, b_dati) != 0) break;
        scritti += q;
    }

    if (scritti == 0) return -1;

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
    if (super_aggiorna(m) != 0) return -1;

    return (int)scritti;
}

int ext2_truncate(int mnt, const char *percorso, uint32_t nuova_dim)
{
    Ext2Mount *m = prendi(mnt);
    uint8_t    inode[128];
    uint32_t   num, dim, tenere, coda;

    if (m == NULL) return -1;

    num = risolvi(m, mnt, percorso, inode);
    if (num == 0) return -1;
    if ((le16(inode) & MODE_TIPO) != MODE_FILE) return -1;

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

    if (pota_inode(m, mnt, inode, tenere) != 0) return -1;

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
            if (scrivi_blocco(m, blk, b_dati) != 0) return -1;
        }
    }

    p32(inode + 4, nuova_dim);

    if (scrivi_inode(m, num, inode) != 0) return -1;
    return super_aggiorna(m);
}

int ext2_sync(int mnt)
{
    Ext2Mount *m = prendi(mnt);

    if (m == NULL) return -1;

    /* Non c'e' niente da riversare: questo driver scrive subito, e l'unica
     * cosa tenuta da parte e' l'ultimo blocco di puntatori LETTO, che non
     * contiene modifiche. Esiste per completare l'interfaccia — un VFS che
     * deve sapere quali filesystem hanno un sync e quali no e' un VFS che
     * ci prova a indovinare. */
    return blk_flush(m->blkdev);
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

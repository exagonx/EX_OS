/* =============================================================================
 * bin/mkfs/ext2.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Creazione di un filesystem ext2, ext3 o ext4, scritta DALLA SPECIFICA.
 *
 * -----------------------------------------------------------------------
 * PERCHE' NON E' UN PORTING DI e2fsprogs O DEL DRIVER DI LINUX
 *
 * La licenza non c'entra: EX-OS e' GPL-2.0-or-later, `libext2fs` e'
 * LGPL-2.0 e `mke2fs` e il driver ext2 di Linux sono GPL-2.0. Tutto
 * compatibile, e si sarebbe potuto copiare.
 *
 * Il problema e' ingegneristico.
 *
 * Il driver ext2 di Linux (fs/ext2/) non e' un modulo che "legge ext2":
 * e' un modulo che TRADUCE ext2 nel VFS di Linux. E' cucito addosso a
 * buffer_head, alla page cache, a struct super_block. Portarlo significa
 * portare quel VFS, cioe' un pezzo di Linux molto piu' grande di EX-OS.
 *
 * e2fsprogs trascina libext2fs + libcom_err + libuuid + libblkid, decine
 * di file, open/pread/ioctl, allocazione dinamica ovunque, tipi a 64 bit.
 * Adattarlo a questa libc costa piu' che scrivere da zero.
 *
 * Il formato ext2, invece, e' pubblicamente documentato, e la
 * documentazione non e' coperta da quelle licenze. Questo file e' scritto
 * dalla specifica: sono ~600 righe, meno del porting, e si capisce ogni
 * byte che si scrive — che con un porting non succede.
 *
 * -----------------------------------------------------------------------
 * THE BLOCK SIZE
 *
 * ext2 is always 1024 bytes, as it has been since this file was born.
 * ext3 and ext4 follow mke2fs: 1024 under 512 MB, 4096 from there up. With
 * 1024 s_first_data_block is 1 and the superblock fills block 1; above, it is
 * 0 and the superblock lives INSIDE block 0, at byte 1024, next to the boot
 * record. Every position below comes from inizio_gruppo() and meta_gruppo().
 *
 * -----------------------------------------------------------------------
 * ext3 AND ext4 (mkfs 0.002)
 *
 * ext3 is ext2 with inodes of 256 bytes, dir_index and a journal: inode 8, a
 * contiguous file holding a jbd2 superblock (see piano_giornale).
 *
 * ext4 adds extents, huge_file, dir_nlink, extra_isize and metadata_csum. Not
 * flex_bg, 64bit or resize_inode: those three are about very large or
 * growing volumes, and a partition seen by this formatter never passes 2^32
 * sectors. With metadata_csum every structure carries a crc32c: superblock,
 * descriptors, bitmaps, inodes, directory blocks.
 *
 * -----------------------------------------------------------------------
 * COSA IMPONE e2fsck, E CHE NON E' OVVIO DALLA SPECIFICA
 *
 * Sono le cose che un formattatore scritto "leggendo le struct" sbaglia,
 * perche' la specifica descrive i campi e non gli invarianti:
 *
 *   1. i_blocks e' in unita' da 512 BYTE, non in blocchi del filesystem.
 *      Una directory da un blocco da 1024 ha i_blocks = 2.
 *   2. I BIT DI RIEMPIMENTO delle bitmap — quelli oltre la fine reale del
 *      gruppo — devono essere a UNO. Lasciarli a zero significa dichiarare
 *      liberi dei blocchi che non esistono.
 *   3. Gli inode riservati 1..10 vanno marcati USATI nella bitmap anche se
 *      non descrivono niente. s_first_ino vale 11 proprio per questo.
 *   4. /lost+found deve esistere. e2fsck lo pretende e si offre di
 *      crearlo: un filesystem appena formattato che fa gia' proporre una
 *      riparazione e' un filesystem su cui, il giorno che il problema e'
 *      vero, nessuno guardera' piu'.
 *   5. i_links_count della radice e' 3, non 2: le voci che puntano alla
 *      radice sono "." dentro di se', ".." dentro di se' (la radice e'
 *      padre di se stessa) e ".." dentro lost+found.
 *   6. La somma dei contatori liberi dei descrittori di gruppo deve
 *      combaciare ESATTAMENTE con quella nel superblocco.
 * ============================================================================= */
#include "libc.h"
#include "ext2.h"
#include "crc32c.h"

#define BS_MAX          4096u
#define BYTE_PER_INODE  16384u      /* stesso rapporto predefinito di mke2fs */
#define PRIMO_INODE     11u         /* 1..10 sono riservati */
#define INODE_RADICE    2u
#define INODE_GIORNALE  8u
#define MAGIC           0xEF53u
#define GIORNALE_MIN    1024u       /* JBD2_MIN_JOURNAL_BLOCKS */
#define GIORNALE_MAGIA  0xC03B3998u
#define ESTESI_MAGIA    0xF30Au

/* Funzionalita' dichiarate.
 *
 * FILETYPE mette il tipo dentro la voce di directory, cosi' elencare una
 * directory non costringe a leggere un inode per voce. Linux lo si aspetta
 * su qualunque ext2 moderno.
 *
 * SPARSE_SUPER limita le copie di superblocco e tabella dei descrittori ai
 * gruppi 0, 1 e alle potenze di 3, 5, 7. Senza, ogni gruppo ne porta una
 * copia: su un volume da 400 MB sono 50 copie di una tabella che cambia
 * tutta insieme, cioe' 50 posti da tenere allineati invece di 7. */
#define COMPAT_HAS_JOURNAL      0x0004u
#define COMPAT_DIR_INDEX        0x0020u
#define INCOMPAT_FILETYPE       0x0002u
#define INCOMPAT_EXTENTS        0x0040u
#define RO_COMPAT_SPARSE_SUPER  0x0001u
#define RO_COMPAT_LARGE_FILE    0x0002u
#define RO_COMPAT_HUGE_FILE     0x0008u
#define RO_COMPAT_DIR_NLINK     0x0020u
#define RO_COMPAT_EXTRA_ISIZE   0x0040u
#define RO_COMPAT_METADATA_CSUM 0x0400u

#define FL_EXTENTS              0x00080000u
#define BG_ITABLE_ZEROED        0x0004u

#define TIPO_DIR    2u              /* file_type nelle voci di directory */

/* The geometry of the volume being made. They are globals because every
 * helper below needs them, and ext2_piano sets them before anything else. */
static unsigned int BS;             /* byte per blocco */
static unsigned int SETT_PER_BLOCCO;
static unsigned int PRIMO_DATO;     /* 1 with 1 KB blocks, 0 above */
static unsigned int INODE_SIZE;

static unsigned char blocco[BS_MAX];
static unsigned char sb[1024];
static unsigned char zeri[32768];   /* 32 KB per chiamata */

/* ext4 only: the seed of every sum, and the two bitmap sums of each group,
 * computed when the bitmaps are written and needed by the descriptors. */
static unsigned int    seme_vol;
static unsigned short *somme_bm;

/* =============================================================================
 * Utilita'
 * ============================================================================= */

static void p16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}

static void p32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v;         p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static unsigned int le32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void pbe32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static void azzera_buffer(unsigned char *p, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) p[i] = 0;
}

static unsigned int su(unsigned int a, unsigned int b)   /* ceil(a/b) */
{
    return (a + b - 1u) / b;
}

/* Vero se il gruppo `g` porta una copia di superblocco e tabella dei
 * descrittori. Con SPARSE_SUPER: 0, 1, e le potenze di 3, 5, 7. */
static int ha_copia(unsigned int g)
{
    unsigned int b;

    if (g <= 1) return 1;
    if ((g & 1u) == 0) return 0;        /* le potenze di 3,5,7 sono dispari */

    for (b = 3; b <= 7; b += 2) {
        unsigned int v = b;
        while (v < g) {
            /* Il controllo di traboccamento serve davvero: su un volume
             * enorme v*b puo' girare e far sembrare potenza un gruppo che
             * non lo e', cioe' scrivere una copia dove non va. */
            if (v > 0xFFFFFFFFu / b) { v = 0; break; }
            v *= b;
        }
        if (v == g) return 1;
    }
    return 0;
}

/* =============================================================================
 * Scrittura a blocchi
 * ============================================================================= */

static int scrivi_blocco(const char *dev, unsigned int nb, const void *buf)
{
    int r = blkwrite(dev, nb * SETT_PER_BLOCCO, SETT_PER_BLOCCO, buf);
    if (r < 0) return r;
    return (r == (int)SETT_PER_BLOCCO) ? 0 : -1;
}

static int azzera_blocchi(const char *dev, unsigned int primo, unsigned int n)
{
    unsigned int max = (unsigned int)sizeof(zeri) / BS;

    while (n > 0) {
        unsigned int q = (n > max) ? max : n;
        int r = blkwrite(dev, primo * SETT_PER_BLOCCO, q * SETT_PER_BLOCCO, zeri);

        if (r < 0) return r;
        if ((unsigned int)r != q * SETT_PER_BLOCCO) return -1;

        primo += q;
        n     -= q;
    }
    return 0;
}

/* =============================================================================
 * Geometria
 * ============================================================================= */

static void imposta(unsigned int bs, unsigned int dim_inode)
{
    BS              = bs;
    SETT_PER_BLOCCO = bs / 512u;
    PRIMO_DATO      = (bs == 1024u) ? 1u : 0u;
    INODE_SIZE      = dim_inode;
}

/* Blocchi di metadati che ogni gruppo si porta via. */
static unsigned int spesa_gruppo(const Ext2Geo *g, unsigned int gruppo)
{
    unsigned int s = 2u + g->itab_blocchi;      /* le due bitmap + gli inode */
    if (ha_copia(gruppo)) s += 1u + g->gdt_blocchi;
    return s;
}

static unsigned int inizio_gruppo(const Ext2Geo *g, unsigned int gruppo)
{
    return PRIMO_DATO + gruppo * g->blocchi_per_gruppo;
}

/* Il primo blocco del gruppo dopo superblocco e descrittori: la bitmap. */
static unsigned int meta_gruppo(const Ext2Geo *g, unsigned int gruppo)
{
    unsigned int m = inizio_gruppo(g, gruppo);

    if (ha_copia(gruppo)) m += 1u + g->gdt_blocchi;
    return m;
}

static unsigned int blocchi_del_gruppo(const Ext2Geo *g, unsigned int gruppo)
{
    unsigned int primo = inizio_gruppo(g, gruppo);
    unsigned int resto = g->blocchi - primo;
    return (resto > g->blocchi_per_gruppo) ? g->blocchi_per_gruppo : resto;
}

/* Used blocks at the start of a group, the journal not counted. */
static unsigned int usati_gruppo(const Ext2Geo *g, unsigned int gruppo)
{
    return spesa_gruppo(g, gruppo) + ((gruppo == 0) ? 2u : 0u);
}

/* The journal's whole span in its group: pointer blocks and journal. */
static unsigned int spesa_giornale(const Ext2Geo *g, unsigned int gruppo)
{
    return (g->giornale && gruppo == g->giornale_gruppo)
           ? g->giornale + g->giornale_ind : 0u;
}

/* Pointer blocks an ext3 journal of `n` blocks needs. ext4 needs none: one
 * extent, because the journal is contiguous. */
static unsigned int ind_per(unsigned int n, unsigned int gen)
{
    unsigned int p = BS / 4u;

    if (gen >= 4u || n <= 12u) return 0;
    if (n <= 12u + p) return 1u;
    return 2u + su(n - 12u - p, p);
}

/* mke2fs's table (ext2fs_default_journal_size), by the size of the volume. */
static unsigned int giornale_voluto(unsigned int blocchi)
{
    if (blocchi < 32768u)        return 1024u;
    if (blocchi < 256u * 1024u)  return 4096u;
    if (blocchi < 512u * 1024u)  return 8192u;
    if (blocchi < 4096u * 1024u) return 16384u;
    return 32768u;
}

/* =============================================================================
 * THE JOURNAL STAYS INSIDE ONE GROUP
 *
 * ! Contiguous, so ext4 describes it with a single extent and ext3 with a
 * plain run of pointers. A group is 32768 blocks at 4 KB, so the journal can
 * reach 128 MB less the group's metadata: mke2fs goes up to 1 GB on huge
 * disks by crossing groups, which is worth nothing here and would need a
 * tree of extents. The middle group is mke2fs's choice too: from there the
 * disk head has the least way to go.
 * ============================================================================= */
static int piano_giornale(Ext2Geo *g, unsigned int gen)
{
    unsigned int voluto = giornale_voluto(g->blocchi);
    unsigned int gr = g->gruppi / 2u, migliore = 0, spazio_max = 0;

    for (;;) {
        unsigned int tot = blocchi_del_gruppo(g, gr);
        unsigned int spesa = usati_gruppo(g, gr);
        unsigned int spazio = (tot > spesa) ? tot - spesa : 0u;

        if (spazio > spazio_max) { spazio_max = spazio; migliore = gr; }
        if (gr == 0 || spazio >= voluto + ind_per(voluto, gen)) break;
        gr--;
    }

    if (spazio_max < voluto + ind_per(voluto, gen)) {
        voluto = spazio_max;
        while (voluto > 0 && voluto + ind_per(voluto, gen) > spazio_max) voluto--;
    }
    if (voluto < GIORNALE_MIN) {
        printf("mkfs: il volume e' troppo piccolo per il giornale di ext%u:\n", gen);
        printf("servono %u blocchi contigui, ce ne sono %u. Per una\n",
               GIORNALE_MIN, spazio_max);
        printf("partizione cosi' piccola usa: mkfs -t ext2\n");
        return -1;
    }

    g->giornale        = voluto;
    g->giornale_ind    = ind_per(voluto, gen);
    g->giornale_gruppo = migliore;
    g->giornale_primo  = meta_gruppo(g, migliore) + 2u + g->itab_blocchi +
                         ((migliore == 0) ? 2u : 0u);
    return 0;
}

int ext2_piano(Ext2Geo *g, unsigned int settori, unsigned int gen)
{
    unsigned int i, usati = 0;

    azzera_buffer((unsigned char *)g, sizeof(Ext2Geo));

    /* ext2 stays as it always was. ext3 and ext4 take mke2fs's rule: 4 KB
     * from 512 MB up, and inodes of 256 bytes. */
    if (gen == 2u)                     imposta(1024u, 128u);
    else if (settori >= 512u * 2048u)  imposta(4096u, 256u);
    else                               imposta(1024u, 256u);

    g->gen                = gen;
    g->bs                 = BS;
    g->dim_inode          = INODE_SIZE;
    g->blocchi            = settori / SETT_PER_BLOCCO;
    g->blocchi_per_gruppo = 8u * BS;

    /* Sotto questa soglia non ci sta nemmeno un gruppo con i suoi
     * metadati, la radice e lost+found. */
    if (g->blocchi < 64u) {
        printf("mkfs: %u settori sono troppo pochi per un ext%u.\n", settori, gen);
        printf("Servono almeno ~64 KB, e sotto qualche MB il filesystem\n");
        printf("sarebbe fatto quasi solo di metadati.\n");
        return -1;
    }

    g->gruppi = su(g->blocchi - PRIMO_DATO, g->blocchi_per_gruppo);

    /* Numero di inode: stesso rapporto di mke2fs, un inode ogni 16 KB.
     * Arrotondato a multiplo di 8 perche' la bitmap lavora a byte, e
     * limitato a quanti ne descrive una bitmap da un blocco. */
    g->inode_totali = (g->blocchi / (BYTE_PER_INODE / BS));
    if (g->inode_totali < PRIMO_INODE + 1u) g->inode_totali = PRIMO_INODE + 1u;

    g->inode_per_gruppo = su(g->inode_totali, g->gruppi);
    g->inode_per_gruppo = su(g->inode_per_gruppo, 8u) * 8u;
    if (g->inode_per_gruppo > 8u * BS) g->inode_per_gruppo = 8u * BS;
    if (g->inode_per_gruppo < 16u)     g->inode_per_gruppo = 16u;

    g->itab_blocchi = su(g->inode_per_gruppo * INODE_SIZE, BS);
    g->gdt_blocchi  = su(g->gruppi * 32u, BS);

    /* L'ULTIMO GRUPPO PUO' NON STARCI. Un gruppo troppo corto per i propri
     * metadati non e' utilizzabile, e lasciarlo dentro produrrebbe un
     * descrittore che dichiara blocchi liberi dentro la propria tabella
     * inode. Si accorcia il filesystem: qualche blocco perso vale meno di
     * un filesystem che si contraddice. */
    {
        unsigned int ultimo = g->gruppi - 1u;
        while (g->gruppi > 1u &&
               blocchi_del_gruppo(g, ultimo) <= spesa_gruppo(g, ultimo) + 1u) {
            g->gruppi--;
            g->blocchi  = PRIMO_DATO + g->gruppi * g->blocchi_per_gruppo;
            g->gdt_blocchi = su(g->gruppi * 32u, BS);
            ultimo = g->gruppi - 1u;
        }
    }

    g->inode_totali = g->inode_per_gruppo * g->gruppi;

    if (gen >= 3u && piano_giornale(g, gen) != 0) return -1;

    /* Conteggio dei blocchi occupati, gruppo per gruppo. Va fatto ORA e
     * non a scrittura finita: e' il numero che finisce nel superblocco, e
     * se non combacia con la somma dei descrittori e2fsck lo segnala. */
    for (i = 0; i < g->gruppi; i++) usati += spesa_gruppo(g, i);
    usati += 2u;    /* il blocco della radice e quello di lost+found */
    usati += g->giornale + g->giornale_ind;

    if (usati + 8u >= g->blocchi) {
        printf("mkfs: su %u settori i metadati ext%u occuperebbero tutto\n",
               settori, gen);
        printf("lo spazio. Serve una partizione piu' grande.\n");
        return -1;
    }

    g->liberi    = g->blocchi - PRIMO_DATO - usati;
    g->riservati = g->blocchi / 20u;    /* 5%, come mke2fs */

    return 0;
}

void ext2_mostra(const Ext2Geo *g)
{
    printf("\nFilesystem ext%u (revisione 1)\n", g->gen);
    printf("  byte per blocco        : %u\n", g->bs);
    printf("  blocchi                : %u\n", g->blocchi);
    printf("  gruppi di blocchi      : %u da %u blocchi\n",
           g->gruppi, g->blocchi_per_gruppo);
    printf("  inode                  : %u da %u byte (%u per gruppo)\n",
           g->inode_totali, g->dim_inode, g->inode_per_gruppo);
    printf("  tabella descrittori    : %u blocchi\n", g->gdt_blocchi);
    printf("  tabella inode          : %u blocchi per gruppo\n", g->itab_blocchi);
    if (g->giornale)
        printf("  giornale               : %u blocchi (%u MB), gruppo %u\n",
               g->giornale, g->giornale / (1048576u / g->bs), g->giornale_gruppo);
    printf("  blocchi liberi         : %u\n", g->liberi);
    printf("  riservati a root       : %u (5%%)\n", g->riservati);
    if (g->gen == 2u)
        printf("  funzionalita'          : filetype, sparse_super\n");
    else if (g->gen == 3u)
        printf("  funzionalita'          : has_journal, dir_index, filetype,\n"
               "                           sparse_super, large_file\n");
    else
        printf("  funzionalita'          : has_journal, dir_index, filetype, extent,\n"
               "                           sparse_super, large_file, huge_file,\n"
               "                           dir_nlink, extra_isize, metadata_csum\n");
}

/* =============================================================================
 * Composizione delle strutture
 * ============================================================================= */

/* UUID. Non e' crittografia: serve a distinguere due volumi, e senza un
 * generatore di casualita' si compone da un seme (i millisecondi dall'avvio)
 * e dalla geometria, che due volumi diversi hanno diversa. Meglio di sedici
 * zeri, che renderebbero indistinguibili tutti i volumi formattati da EX-OS. */
static void componi_uuid(unsigned char *u, const Ext2Geo *g, unsigned int uuid_seme)
{
    unsigned int a = uuid_seme ? uuid_seme : 0x45584F53u;

    p32(u + 0,  a);
    p32(u + 4,  a ^ g->blocchi);
    p32(u + 8,  (a << 13) ^ g->inode_totali);
    p32(u + 12, (a >> 7)  ^ (g->gruppi * 2654435761u));

    /* Versione 4 e variante DCE: due strumenti su tre li guardano, e un
     * UUID che non li rispetta viene mostrato come malformato. */
    u[6] = (unsigned char)((u[6] & 0x0F) | 0x40);
    u[8] = (unsigned char)((u[8] & 0x3F) | 0x80);
}

/* Superblocco. `nr_gruppo` e' il gruppo in cui questa copia sta scritta:
 * s_block_group_nr serve a e2fsck per capire, trovando una copia, da dove
 * l'ha presa. `jblocks` is the journal inode's i_block, kept as a backup. */
static void componi_super(const Ext2Geo *g, const char *etichetta,
                          unsigned int uuid_seme, unsigned int nr_gruppo,
                          const unsigned char *jblocks)
{
    unsigned int i, log_bs = 0;
    unsigned int compat = 0, incompat = INCOMPAT_FILETYPE;
    unsigned int ro = RO_COMPAT_SPARSE_SUPER;

    while ((1024u << log_bs) < BS) log_bs++;

    azzera_buffer(sb, sizeof(sb));

    p32(sb +  0, g->inode_totali);
    p32(sb +  4, g->blocchi);
    p32(sb +  8, g->riservati);
    p32(sb + 12, g->liberi);
    p32(sb + 16, g->inode_totali - PRIMO_INODE);
    p32(sb + 20, PRIMO_DATO);
    p32(sb + 24, log_bs);
    p32(sb + 28, log_bs);                   /* frammenti = blocchi */
    p32(sb + 32, g->blocchi_per_gruppo);
    p32(sb + 36, g->blocchi_per_gruppo);
    p32(sb + 40, g->inode_per_gruppo);

    /* Nessun orologio: EX-OS non ha un RTC letto dal kernel, e inventare
     * una data significa scriverne una sbagliata. Zero vale 1 gennaio
     * 1970, che e' evidentemente convenzionale e non fa credere a nessuno
     * di sapere quando il volume e' stato creato. Una data nel FUTURO,
     * che e' l'altro esito possibile tirando a indovinare, farebbe
     * lamentare ogni fsck da qui in avanti. */
    p32(sb + 44, 0);                        /* s_mtime */
    p32(sb + 48, 0);                        /* s_wtime */
    p16(sb + 52, 0);                        /* s_mnt_count */
    p16(sb + 54, 0xFFFFu);                  /* -1: nessun controllo forzato */
    p16(sb + 56, MAGIC);
    p16(sb + 58, 1);                        /* s_state: pulito */
    p16(sb + 60, 1);                        /* s_errors: continua */
    p16(sb + 62, 0);                        /* revisione minore */
    p32(sb + 64, 0);                        /* s_lastcheck */
    p32(sb + 68, 0);                        /* s_checkinterval: mai */
    p32(sb + 72, 0);                        /* creatore: Linux */
    p32(sb + 76, 1);                        /* revisione 1 (dinamica) */
    p16(sb + 80, 0);                        /* uid riservato */
    p16(sb + 82, 0);                        /* gid riservato */

    /* --- campi della revisione 1 --- */
    if (g->gen >= 3u) {
        compat |= COMPAT_HAS_JOURNAL | COMPAT_DIR_INDEX;
        ro     |= RO_COMPAT_LARGE_FILE;
    }
    if (g->gen >= 4u) {
        incompat |= INCOMPAT_EXTENTS;
        ro       |= RO_COMPAT_HUGE_FILE | RO_COMPAT_DIR_NLINK |
                    RO_COMPAT_EXTRA_ISIZE | RO_COMPAT_METADATA_CSUM;
    }

    p32(sb + 84, PRIMO_INODE);
    p16(sb + 88, INODE_SIZE);
    p16(sb + 90, nr_gruppo);
    p32(sb + 92, compat);
    p32(sb + 96, incompat);
    p32(sb + 100, ro);

    componi_uuid(sb + 104, g, uuid_seme);

    for (i = 0; i < 16u; i++) {
        char c = etichetta[i];
        if (c == '\0') break;
        sb[120 + i] = (unsigned char)c;
    }

    if (g->gen >= 3u) {
        p32(sb + 224, INODE_GIORNALE);

        /* The htree hash: a seed of its own, half_md4, and chars taken as
         * signed, as on every x86. */
        p32(sb + 236, le32(sb + 104) ^ 0x5A5A5A5Au);
        p32(sb + 240, le32(sb + 108) ^ 0x5A5A5A5Au);
        p32(sb + 244, le32(sb + 112) ^ 0x5A5A5A5Au);
        p32(sb + 248, le32(sb + 116) ^ 0x5A5A5A5Au);
        sb[252] = 1u;                       /* s_def_hash_version */
        sb[253] = 1u;                       /* s_jnl_backup_type */
        for (i = 0; i < 60u; i++) sb[268 + i] = jblocks[i];
        p32(sb + 268 + 64, g->giornale * BS);
        p16(sb + 0x15C, 32u);               /* s_min_extra_isize */
        p16(sb + 0x15E, 32u);               /* s_want_extra_isize */
        p32(sb + 0x160, 1u);                /* s_flags: signed hash */
    }

    if (g->gen >= 4u) {
        sb[0x175] = 1u;                     /* s_checksum_type: crc32c */
        p32(sb + 0x3FC, crc32c(~0u, sb, 0x3FCu));
    }
}

/* The seed of one inode: its number and i_generation, which is 0 here. */
static unsigned int seme_inode(unsigned int num)
{
    unsigned char b[4];

    p32(b, num);
    return crc32c(crc32c(seme_vol, b, 4), "\0\0\0\0", 4);
}

/* Un descrittore di gruppo, 32 byte, dentro `dst`. */
static void componi_descrittore(unsigned char *dst, const Ext2Geo *g,
                                unsigned int gruppo)
{
    unsigned int meta = meta_gruppo(g, gruppo);
    unsigned int liberi, dirs = 0;

    azzera_buffer(dst, 32u);

    p32(dst + 0, meta);                 /* bitmap dei blocchi */
    p32(dst + 4, meta + 1u);            /* bitmap degli inode */
    p32(dst + 8, meta + 2u);            /* tabella degli inode */

    liberi = blocchi_del_gruppo(g, gruppo) - usati_gruppo(g, gruppo)
             - spesa_giornale(g, gruppo);

    /* Nel gruppo 0 due blocchi sono gia' occupati dalla radice e da
     * lost+found, e le due directory contano nel proprio contatore. */
    if (gruppo == 0) dirs = 2u;

    p16(dst + 12, liberi);
    p16(dst + 14, (gruppo == 0) ? (g->inode_per_gruppo - PRIMO_INODE)
                                : g->inode_per_gruppo);
    p16(dst + 16, dirs);

    if (g->gen >= 4u) {
        unsigned char gl[4];
        unsigned int  c;

        p16(dst + 18, BG_ITABLE_ZEROED);
        p16(dst + 24, somme_bm[gruppo * 2u]);
        p16(dst + 26, somme_bm[gruppo * 2u + 1u]);

        /* metadata_csum: the group's number, then the descriptor with
         * bg_checksum (offset 30) counted as zero. */
        p32(gl, gruppo);
        c = crc32c(seme_vol, gl, 4);
        c = crc32c(c, dst, 30u);
        c = crc32c(c, "\0\0", 2);
        p16(dst + 30, c & 0xFFFFu);
    }
}

/* Riempie `blocco` con la bitmap dei blocchi del gruppo. */
static void componi_bitmap_blocchi(const Ext2Geo *g, unsigned int gruppo)
{
    unsigned int usati = usati_gruppo(g, gruppo) + spesa_giornale(g, gruppo);
    unsigned int reali = blocchi_del_gruppo(g, gruppo);
    unsigned int i;

    azzera_buffer(blocco, BS);

    for (i = 0; i < usati; i++) blocco[i / 8u] |= (unsigned char)(1u << (i % 8u));

    /* I BIT DI RIEMPIMENTO. Oltre `reali` non ci sono blocchi: lasciarli a
     * zero significa dichiarare liberi dei blocchi che non esistono, e
     * l'allocatore ci proverebbe. e2fsck lo segnala come "padding at end
     * of block bitmap is not set". */
    for (i = reali; i < g->blocchi_per_gruppo; i++)
        blocco[i / 8u] |= (unsigned char)(1u << (i % 8u));
}

static void componi_bitmap_inode(const Ext2Geo *g, unsigned int gruppo)
{
    unsigned int i;

    azzera_buffer(blocco, BS);

    /* Gli inode 1..10 sono RISERVATI e vanno marcati usati anche se non
     * descrivono niente: s_first_ino vale 11 proprio perche' nessuno deve
     * allocarli. Nel gruppo 0 sono i bit 0..9, piu' il bit 10 che e'
     * lost+found (inode 11). */
    if (gruppo == 0)
        for (i = 0; i < PRIMO_INODE; i++)
            blocco[i / 8u] |= (unsigned char)(1u << (i % 8u));

    for (i = g->inode_per_gruppo; i < 8u * BS; i++)
        blocco[i / 8u] |= (unsigned char)(1u << (i % 8u));
}

/* An extent header with one extent, in the i_block of `dst`. */
static void estensione(unsigned char *dst, unsigned int primo, unsigned int n)
{
    unsigned char *e = dst + 40;

    p16(e + 0, ESTESI_MAGIA);
    p16(e + 2, 1u);                     /* eh_entries */
    p16(e + 4, 4u);                     /* eh_max: quattro stanno nell'inode */
    p16(e + 6, 0u);                     /* eh_depth */
    p32(e + 12, 0u);                    /* ee_block */
    p16(e + 16, n);                     /* ee_len */
    p16(e + 18, 0u);                    /* ee_start_hi */
    p32(e + 20, primo);                 /* ee_start_lo */
}

/* Un inode dentro `dst`: quello che hanno in comune una directory e il
 * giornale. */
static void componi_inode(unsigned char *dst, const Ext2Geo *g, unsigned int modo,
                          unsigned int link, unsigned int dim, unsigned int blocchi)
{
    azzera_buffer(dst, INODE_SIZE);

    p16(dst +  0, modo);
    p16(dst +  2, 0);                   /* uid root */
    p32(dst +  4, dim);
    p32(dst +  8, 0);                   /* atime */
    p32(dst + 12, 0);                   /* ctime */
    p32(dst + 16, 0);                   /* mtime */
    p32(dst + 20, 0);                   /* dtime: non cancellato */
    p16(dst + 24, 0);                   /* gid root */
    p16(dst + 26, link);

    /* i_blocks e' in unita' da 512 BYTE, non in blocchi del filesystem.
     * E' l'errore piu' facile di tutto il formato: un blocco da 1024 vale
     * DUE. Con il valore sbagliato e2fsck segnala ogni singolo inode. */
    p32(dst + 28, blocchi * SETT_PER_BLOCCO);

    if (g->gen >= 4u) p32(dst + 32, FL_EXTENTS);
    if (INODE_SIZE > 128u) p16(dst + 128, 32u);     /* i_extra_isize */
}

/* The inode's sum, last: over the whole inode, with both halves of the
 * field counted as zero (they are, componi_inode cleared them). */
static void somma_inode(unsigned char *ino, unsigned int num)
{
    unsigned int c = crc32c(seme_inode(num), ino, INODE_SIZE);

    p16(ino + 0x7C, c & 0xFFFFu);
    p16(ino + 0x82, c >> 16);
}

/* A directory of one block. */
static void inode_dir(unsigned char *tab, const Ext2Geo *g, unsigned int num,
                      unsigned int modo, unsigned int link, unsigned int b)
{
    unsigned char *dst = tab + (num - 1u) * INODE_SIZE;

    componi_inode(dst, g, modo, link, BS, 1u);
    if (g->gen >= 4u) { estensione(dst, b, 1u); somma_inode(dst, num); }
    else              p32(dst + 40, b);
}

/* The journal's inode. On ext3 the pointers go in i_block and in the pointer
 * blocks, which ext2_scrivi writes in giornale_mappa(). */
static void inode_giornale(unsigned char *tab, const Ext2Geo *g)
{
    unsigned char *dst = tab + (INODE_GIORNALE - 1u) * INODE_SIZE;
    unsigned int   dati = g->giornale_primo + g->giornale_ind;
    unsigned int   i, p = BS / 4u;

    componi_inode(dst, g, 0x8180u, 1u, g->giornale * BS,
                  g->giornale + g->giornale_ind);

    if (g->gen >= 4u) {
        estensione(dst, dati, g->giornale);
        somma_inode(dst, INODE_GIORNALE);
        return;
    }
    for (i = 0; i < 12u && i < g->giornale; i++) p32(dst + 40 + i * 4u, dati + i);
    if (g->giornale > 12u)     p32(dst + 40 + 12u * 4u, g->giornale_primo);
    if (g->giornale > 12u + p) p32(dst + 40 + 13u * 4u, g->giornale_primo + 1u);
}

/* ext3 only: the pointer blocks, in the order ind_per() counted them: the
 * single indirect, the double, and the double's children. */
static int giornale_mappa(const char *dev, const Ext2Geo *g)
{
    unsigned int dati = g->giornale_primo + g->giornale_ind;
    unsigned int p = BS / 4u, n = g->giornale, i, sotto;
    int r;

    if (g->giornale_ind == 0) return 0;

    azzera_buffer(blocco, BS);
    for (i = 0; i < p && 12u + i < n; i++) p32(blocco + i * 4u, dati + 12u + i);
    r = scrivi_blocco(dev, g->giornale_primo, blocco);
    if (r < 0) return r;
    if (g->giornale_ind == 1u) return 0;

    azzera_buffer(blocco, BS);
    for (sotto = 0; sotto < g->giornale_ind - 2u; sotto++)
        p32(blocco + sotto * 4u, g->giornale_primo + 2u + sotto);
    r = scrivi_blocco(dev, g->giornale_primo + 1u, blocco);
    if (r < 0) return r;

    for (sotto = 0; sotto < g->giornale_ind - 2u; sotto++) {
        unsigned int base = 12u + p + sotto * p;

        azzera_buffer(blocco, BS);
        for (i = 0; i < p && base + i < n; i++) p32(blocco + i * 4u, dati + base + i);
        r = scrivi_blocco(dev, g->giornale_primo + 2u + sotto, blocco);
        if (r < 0) return r;
    }
    return 0;
}

/* The journal's superblock, version 2, in big-endian as all of jbd2. An
 * empty journal: s_start 0 is what says so. */
static int giornale_super(const char *dev, const Ext2Geo *g, const unsigned char *uuid)
{
    unsigned int i;

    azzera_buffer(blocco, BS);
    pbe32(blocco +  0, GIORNALE_MAGIA);
    pbe32(blocco +  4, 4u);             /* JBD2_SUPERBLOCK_V2 */
    pbe32(blocco + 12, BS);
    pbe32(blocco + 16, g->giornale);    /* s_maxlen */
    pbe32(blocco + 20, 1u);             /* s_first */
    pbe32(blocco + 24, 1u);             /* s_sequence */
    pbe32(blocco + 28, 0u);             /* s_start */
    for (i = 0; i < 16u; i++) blocco[48 + i] = uuid[i];
    pbe32(blocco + 64, 1u);             /* s_nr_users */
    return scrivi_blocco(dev, g->giornale_primo + g->giornale_ind, blocco);
}

/* Aggiunge una voce di directory. `avanzo` e' quanto resta nel blocco:
 * l'ULTIMA voce di un blocco deve avere rec_len che arriva fino in fondo,
 * altrimenti chi legge trova un buco e si ferma prima. */
static unsigned int voce_dir(unsigned char *dst, unsigned int inode,
                             const char *nome, unsigned int len,
                             unsigned int rec_len)
{
    unsigned int i;

    p32(dst + 0, inode);
    p16(dst + 4, rec_len);
    dst[6] = (unsigned char)len;
    dst[7] = (unsigned char)TIPO_DIR;
    for (i = 0; i < len; i++) dst[8 + i] = (unsigned char)nome[i];

    return rec_len;
}

/* Where the entries of a directory block end: ext4 keeps 12 bytes for the
 * block's sum, a fake entry of inode 0 and file type 0xDE. */
static unsigned int fine_dir(const Ext2Geo *g)
{
    return (g->gen >= 4u) ? BS - 12u : BS;
}

static void coda_dir(const Ext2Geo *g, unsigned int num)
{
    unsigned char *t = blocco + BS - 12u;

    if (g->gen < 4u) return;
    p16(t + 4, 12u);
    t[7] = 0xDEu;
    p32(t + 8, crc32c(seme_inode(num), blocco, BS - 12u));
}

/* =============================================================================
 * Scrittura
 * ============================================================================= */
int ext2_scrivi(const char *dev, const Ext2Geo *g, const char *etichetta,
                unsigned int uuid_seme, int *toccato)
{
    static unsigned char tab[BS_MAX * 3u];  /* 11 inode: lo spazio piu' largo */
    unsigned char uuid[16], jblocks[60];
    unsigned int gruppo, i;
    unsigned int radice_blk, lostfound_blk;
    unsigned int meta0 = meta_gruppo(g, 0);
    int r;

    /* Il blocco 0 e i due blocchi di dati del gruppo 0. La radice e
     * lost+found stanno subito dopo i metadati del gruppo 0. */
    radice_blk    = meta0 + 2u + g->itab_blocchi;
    lostfound_blk = radice_blk + 1u;

    componi_uuid(uuid, g, uuid_seme);
    seme_vol = crc32c(~0u, uuid, 16);

    if (g->gen >= 4u) {
        somme_bm = (unsigned short *)calloc(g->gruppi * 2u, sizeof(unsigned short));
        if (somme_bm == NULL) {
            printf("mkfs: memoria insufficiente per %u gruppi\n", g->gruppi);
            return -1;
        }
    }

    /* --- 1. il volume vecchio smette di essere riconoscibile ------------
     * Stesso ragionamento del ramo FAT: il superblocco vecchio se ne va
     * per primo, cosi' un'interruzione lascia un volume che nessuno monta
     * invece di uno che descrive strutture ormai azzerate. */
    printf("  azzero il superblocco precedente\n");
    r = azzera_blocchi(dev, 0, (BS == 1024u) ? 2u : 1u);
    if (r < 0) return r;
    *toccato = 1;

    /* --- 2. le tabelle inode ------------------------------------------
     * Vanno azzerate tutte: i byte che c'erano prima verrebbero letti come
     * inode con modo, dimensione e puntatori a blocchi qualunque. */
    for (gruppo = 0; gruppo < g->gruppi; gruppo++) {
        if ((gruppo % 16u) == 0 || gruppo == g->gruppi - 1u)
            printf("  azzero la tabella inode del gruppo %u/%u\n",
                   gruppo + 1u, g->gruppi);

        r = azzera_blocchi(dev, meta_gruppo(g, gruppo) + 2u, g->itab_blocchi);
        if (r < 0) return r;
    }

    /* --- 3. bitmap di ogni gruppo -------------------------------------- */
    printf("  scrivo le bitmap dei %u gruppi\n", g->gruppi);
    for (gruppo = 0; gruppo < g->gruppi; gruppo++) {
        unsigned int meta = meta_gruppo(g, gruppo);

        componi_bitmap_blocchi(g, gruppo);
        r = scrivi_blocco(dev, meta, blocco);
        if (r < 0) return r;
        if (somme_bm)
            somme_bm[gruppo * 2u] = (unsigned short)
                crc32c(seme_vol, blocco, g->blocchi_per_gruppo / 8u);

        componi_bitmap_inode(g, gruppo);
        r = scrivi_blocco(dev, meta + 1u, blocco);
        if (r < 0) return r;
        if (somme_bm)
            somme_bm[gruppo * 2u + 1u] = (unsigned short)
                crc32c(seme_vol, blocco, g->inode_per_gruppo / 8u);
    }

    /* --- 4. il giornale ----------------------------------------------- */
    if (g->giornale) {
        printf("  azzero il giornale (%u blocchi)\n", g->giornale);
        r = azzera_blocchi(dev, g->giornale_primo + g->giornale_ind, g->giornale);
        if (r < 0) return r;
        r = giornale_mappa(dev, g);
        if (r < 0) return r;
        r = giornale_super(dev, g, uuid);
        if (r < 0) return r;
    }

    /* --- 5. gli inode: radice, giornale, lost+found --------------------
     * L'inode n occupa il byte (n-1)*INODE_SIZE della tabella del proprio
     * gruppo, e il blocco in cui cade dipende da quanto sono grandi blocchi
     * e inode. Si compongono tutti in `tab`, che riproduce l'inizio della
     * tabella, e si scrivono interi i blocchi toccati: i tre inode possono
     * cadere in un blocco solo, in due o in tre. */
    {
        unsigned int blk_tab = su(PRIMO_INODE * INODE_SIZE, BS);

        azzera_buffer(tab, sizeof(tab));

        /* La radice, 0755, e TRE collegamenti — "." dentro di se', ".."
         * dentro di se' (la radice e' padre di se stessa) e ".." dentro
         * lost+found. Il valore 2, che verrebbe naturale, fa segnalare a
         * e2fsck un conteggio sbagliato. */
        inode_dir(tab, g, INODE_RADICE, 0x41EDu, 3u, radice_blk);

        /* lost+found, 0700. Ci finiscono i file che hanno perso il nome, e
         * i loro contenuti non devono diventare leggibili a tutti solo
         * perche' il filesystem si e' rotto. */
        inode_dir(tab, g, PRIMO_INODE, 0x41C0u, 2u, lostfound_blk);

        if (g->giornale) inode_giornale(tab, g);

        printf("  scrivo gli inode della radice e di /lost+found\n");
        for (i = 0; i < blk_tab; i++) {
            r = scrivi_blocco(dev, meta0 + 2u + i, tab + i * BS);
            if (r < 0) return r;
        }

        azzera_buffer(jblocks, sizeof(jblocks));
        if (g->giornale)
            for (i = 0; i < 60u; i++)
                jblocks[i] = tab[(INODE_GIORNALE - 1u) * INODE_SIZE + 40u + i];
    }

    /* --- 6. il contenuto delle due directory --------------------------- */
    {
        unsigned int off = 0, fine = fine_dir(g);

        azzera_buffer(blocco, BS);
        off += voce_dir(blocco + off, INODE_RADICE, ".",  1, 12);
        off += voce_dir(blocco + off, INODE_RADICE, "..", 2, 12);
        /* L'ultima voce arriva fino in fondo al blocco: chi legge si
         * ferma quando la somma dei rec_len raggiunge la dimensione, e un
         * rec_len corto lascerebbe uno spazio che verrebbe interpretato
         * come una voce con inode 0 di lunghezza 0 — cioe' un ciclo. */
        voce_dir(blocco + off, PRIMO_INODE, "lost+found", 10, fine - off);
        coda_dir(g, INODE_RADICE);

        printf("  scrivo la directory radice\n");
        r = scrivi_blocco(dev, radice_blk, blocco);
        if (r < 0) return r;

        azzera_buffer(blocco, BS);
        off  = voce_dir(blocco, PRIMO_INODE, ".", 1, 12);
        voce_dir(blocco + off, INODE_RADICE, "..", 2, fine - off);
        coda_dir(g, PRIMO_INODE);

        printf("  scrivo /lost+found\n");
        r = scrivi_blocco(dev, lostfound_blk, blocco);
        if (r < 0) return r;
    }

    /* --- 7. la tabella dei descrittori, in ogni copia ------------------- */
    printf("  scrivo la tabella dei descrittori\n");
    for (gruppo = 0; gruppo < g->gruppi; gruppo++) {
        unsigned int primo, blocco_gdt, n;

        if (!ha_copia(gruppo)) continue;

        primo = inizio_gruppo(g, gruppo);

        for (blocco_gdt = 0; blocco_gdt < g->gdt_blocchi; blocco_gdt++) {
            azzera_buffer(blocco, BS);

            for (n = 0; n < BS / 32u; n++) {
                unsigned int idx = blocco_gdt * (BS / 32u) + n;
                if (idx >= g->gruppi) break;
                componi_descrittore(blocco + n * 32u, g, idx);
            }

            r = scrivi_blocco(dev, primo + 1u + blocco_gdt, blocco);
            if (r < 0) return r;
        }
    }

    free(somme_bm);
    somme_bm = NULL;

    /* --- 8. i superblocchi, PER ULTIMI --------------------------------
     * Come nel ramo FAT: finche' non c'e' il superblocco il volume non e'
     * un ext2 per nessuno, e una formattazione interrotta si riconosce
     * per quello che e'. La copia primaria si scrive per ultima fra le
     * ultime: e' quella che tutti guardano.
     *
     * ! THE PRIMARY IS ALWAYS AT BYTE 1024, not at the start of a block:
     * with 4 KB blocks it sits inside block 0. The copies start their block. */
    for (i = g->gruppi; i > 0; i--) {
        unsigned int gr = i - 1u;
        unsigned int lba = (gr == 0) ? 2u
                                     : inizio_gruppo(g, gr) * SETT_PER_BLOCCO;

        if (!ha_copia(gr)) continue;

        componi_super(g, etichetta, uuid_seme, gr, jblocks);
        r = blkwrite(dev, lba, 2u, sb);
        if (r < 0) return r;
        if (r != 2) return -1;
    }

    printf("  scritti superblocco e copie\n");
    return 0;
}

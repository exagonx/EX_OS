/* =============================================================================
 * bin/mkfs/ext2.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Creazione di un filesystem ext2, scritta DALLA SPECIFICA.
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
 * BLOCCHI DA 1024 BYTE, SEMPRE
 *
 * mke2fs sceglie 4096 sui volumi grandi, ed e' la scelta giusta per le
 * prestazioni. Qui no, e per una ragione precisa: con blocchi da 1024 il
 * campo s_first_data_block vale 1 e il superblocco occupa il blocco 1
 * intero; con blocchi piu' grandi vale 0 e il superblocco vive DENTRO il
 * blocco 0, all'offset 1024, insieme al record di avvio.
 *
 * Sono due disposizioni diverse, e supportarle entrambe in un
 * formattatore che nessuno ha ancora provato significa avere due strade e
 * provarne una. Con 1024 il filesystem e' legale a qualunque dimensione —
 * solo piu' lento — e la strada e' una sola.
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

#define BS              1024u       /* byte per blocco */
#define SETT_PER_BLOCCO 2u          /* 1024 / 512 */
#define PRIMO_DATO      1u          /* s_first_data_block, con blocchi da 1024 */
#define BLK_PER_GRUPPO  8192u       /* 8 * BS bit: una bitmap copre un gruppo */
#define INODE_SIZE      128u
#define BYTE_PER_INODE  16384u      /* stesso rapporto predefinito di mke2fs */
#define PRIMO_INODE     11u         /* 1..10 sono riservati */
#define MAGIC           0xEF53u

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
#define INCOMPAT_FILETYPE       0x0002u
#define RO_COMPAT_SPARSE_SUPER  0x0001u

#define TIPO_DIR    2u              /* file_type nelle voci di directory */

static unsigned char blocco[BS];
static unsigned char zeri[32u * BS];    /* 32 blocchi per chiamata */

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
    while (n > 0) {
        unsigned int q = (n > 32u) ? 32u : n;
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

/* Blocchi di metadati che ogni gruppo si porta via. */
static unsigned int spesa_gruppo(const Ext2Geo *g, unsigned int gruppo)
{
    unsigned int s = 2u + g->itab_blocchi;      /* le due bitmap + gli inode */
    if (ha_copia(gruppo)) s += 1u + g->gdt_blocchi;
    return s;
}

static unsigned int blocchi_del_gruppo(const Ext2Geo *g, unsigned int gruppo)
{
    unsigned int primo = PRIMO_DATO + gruppo * g->blocchi_per_gruppo;
    unsigned int resto = g->blocchi - primo;
    return (resto > g->blocchi_per_gruppo) ? g->blocchi_per_gruppo : resto;
}

int ext2_piano(Ext2Geo *g, unsigned int settori)
{
    unsigned int i, usati = 0;

    azzera_buffer((unsigned char *)g, sizeof(Ext2Geo));

    g->blocchi            = settori / SETT_PER_BLOCCO;
    g->blocchi_per_gruppo = BLK_PER_GRUPPO;

    /* Sotto questa soglia non ci sta nemmeno un gruppo con i suoi
     * metadati, la radice e lost+found. */
    if (g->blocchi < 64u) {
        printf("mkfs: %u settori sono troppo pochi per un ext2.\n", settori);
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

    /* Conteggio dei blocchi occupati, gruppo per gruppo. Va fatto ORA e
     * non a scrittura finita: e' il numero che finisce nel superblocco, e
     * se non combacia con la somma dei descrittori e2fsck lo segnala. */
    for (i = 0; i < g->gruppi; i++) usati += spesa_gruppo(g, i);
    usati += 2u;    /* il blocco della radice e quello di lost+found */

    if (usati + 8u >= g->blocchi) {
        printf("mkfs: su %u settori i metadati ext2 occuperebbero tutto\n",
               settori);
        printf("lo spazio. Serve una partizione piu' grande.\n");
        return -1;
    }

    g->liberi    = g->blocchi - PRIMO_DATO - usati;
    g->riservati = g->blocchi / 20u;    /* 5%, come mke2fs */

    return 0;
}

void ext2_mostra(const Ext2Geo *g)
{
    printf("\nFilesystem ext2 (revisione 1)\n");
    printf("  byte per blocco        : %u\n", BS);
    printf("  blocchi                : %u\n", g->blocchi);
    printf("  gruppi di blocchi      : %u da %u blocchi\n",
           g->gruppi, g->blocchi_per_gruppo);
    printf("  inode                  : %u (%u per gruppo)\n",
           g->inode_totali, g->inode_per_gruppo);
    printf("  tabella descrittori    : %u blocchi\n", g->gdt_blocchi);
    printf("  tabella inode          : %u blocchi per gruppo\n", g->itab_blocchi);
    printf("  blocchi liberi         : %u\n", g->liberi);
    printf("  riservati a root       : %u (5%%)\n", g->riservati);
    printf("  funzionalita'          : filetype, sparse_super\n");
}

/* =============================================================================
 * Composizione delle strutture
 * ============================================================================= */

/* Superblocco. `nr_gruppo` e' il gruppo in cui questa copia sta scritta:
 * s_block_group_nr serve a e2fsck per capire, trovando una copia, da dove
 * l'ha presa. */
static void componi_super(const Ext2Geo *g, const char *etichetta,
                          unsigned int uuid_seme, unsigned int nr_gruppo,
                          unsigned int liberi, unsigned int inode_liberi)
{
    unsigned int i;

    azzera_buffer(blocco, BS);

    p32(blocco +  0, g->inode_totali);
    p32(blocco +  4, g->blocchi);
    p32(blocco +  8, g->riservati);
    p32(blocco + 12, liberi);
    p32(blocco + 16, inode_liberi);
    p32(blocco + 20, PRIMO_DATO);
    p32(blocco + 24, 0);                    /* log2(1024/1024) = 0 */
    p32(blocco + 28, 0);                    /* frammenti = blocchi */
    p32(blocco + 32, g->blocchi_per_gruppo);
    p32(blocco + 36, g->blocchi_per_gruppo);
    p32(blocco + 40, g->inode_per_gruppo);

    /* Nessun orologio: EX-OS non ha un RTC letto dal kernel, e inventare
     * una data significa scriverne una sbagliata. Zero vale 1 gennaio
     * 1970, che e' evidentemente convenzionale e non fa credere a nessuno
     * di sapere quando il volume e' stato creato. Una data nel FUTURO,
     * che e' l'altro esito possibile tirando a indovinare, farebbe
     * lamentare ogni fsck da qui in avanti. */
    p32(blocco + 44, 0);                    /* s_mtime */
    p32(blocco + 48, 0);                    /* s_wtime */
    p16(blocco + 52, 0);                    /* s_mnt_count */
    p16(blocco + 54, 0xFFFFu);              /* -1: nessun controllo forzato */
    p16(blocco + 56, MAGIC);
    p16(blocco + 58, 1);                    /* s_state: pulito */
    p16(blocco + 60, 1);                    /* s_errors: continua */
    p16(blocco + 62, 0);                    /* revisione minore */
    p32(blocco + 64, 0);                    /* s_lastcheck */
    p32(blocco + 68, 0);                    /* s_checkinterval: mai */
    p32(blocco + 72, 0);                    /* creatore: Linux */
    p32(blocco + 76, 1);                    /* revisione 1 (dinamica) */
    p16(blocco + 80, 0);                    /* uid riservato */
    p16(blocco + 82, 0);                    /* gid riservato */

    /* --- campi della revisione 1 --- */
    p32(blocco + 84, PRIMO_INODE);
    p16(blocco + 88, INODE_SIZE);
    p16(blocco + 90, nr_gruppo);
    p32(blocco + 92, 0);                    /* compat: nessuna */
    p32(blocco + 96, INCOMPAT_FILETYPE);
    p32(blocco + 100, RO_COMPAT_SPARSE_SUPER);

    /* UUID. Non e' crittografia: serve a distinguere due volumi, e senza
     * un generatore di casualita' si compone da un seme (i millisecondi
     * dall'avvio) e dalla geometria, che due volumi diversi hanno diversa.
     * Meglio di sedici zeri, che renderebbero indistinguibili tutti i
     * volumi formattati da EX-OS. */
    {
        unsigned int a = uuid_seme ? uuid_seme : 0x45584F53u;
        unsigned int b = a ^ g->blocchi;
        unsigned int c = (a << 13) ^ g->inode_totali;
        unsigned int d = (a >> 7)  ^ (g->gruppi * 2654435761u);

        p32(blocco + 104, a);
        p32(blocco + 108, b);
        p32(blocco + 112, c);
        p32(blocco + 116, d);

        /* Versione 4 e variante DCE: due strumenti su tre li guardano, e
         * un UUID che non li rispetta viene mostrato come malformato. */
        blocco[104 + 6] = (unsigned char)((blocco[104 + 6] & 0x0F) | 0x40);
        blocco[104 + 8] = (unsigned char)((blocco[104 + 8] & 0x3F) | 0x80);
    }

    for (i = 0; i < 16u; i++) {
        char c = etichetta[i];
        if (c == '\0') break;
        blocco[120 + i] = (unsigned char)c;
    }
}

/* Un descrittore di gruppo, 32 byte, dentro `dst`. */
static void componi_descrittore(unsigned char *dst, const Ext2Geo *g,
                                unsigned int gruppo)
{
    unsigned int primo = PRIMO_DATO + gruppo * g->blocchi_per_gruppo;
    unsigned int meta  = primo;
    unsigned int liberi, dirs = 0;

    if (ha_copia(gruppo)) meta += 1u + g->gdt_blocchi;

    p32(dst + 0, meta);                 /* bitmap dei blocchi */
    p32(dst + 4, meta + 1u);            /* bitmap degli inode */
    p32(dst + 8, meta + 2u);            /* tabella degli inode */

    liberi = blocchi_del_gruppo(g, gruppo) - spesa_gruppo(g, gruppo);

    /* Nel gruppo 0 due blocchi sono gia' occupati dalla radice e da
     * lost+found, e le due directory contano nel proprio contatore. */
    if (gruppo == 0) { liberi -= 2u; dirs = 2u; }

    p16(dst + 12, liberi);
    p16(dst + 14, (gruppo == 0) ? (g->inode_per_gruppo - PRIMO_INODE)
                                : g->inode_per_gruppo);
    p16(dst + 16, dirs);
}

/* Riempie `blocco` con la bitmap dei blocchi del gruppo. */
static void componi_bitmap_blocchi(const Ext2Geo *g, unsigned int gruppo)
{
    unsigned int usati = spesa_gruppo(g, gruppo);
    unsigned int reali = blocchi_del_gruppo(g, gruppo);
    unsigned int i;

    azzera_buffer(blocco, BS);

    if (gruppo == 0) usati += 2u;       /* radice e lost+found */

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

/* Un inode di directory dentro `dst`. */
static void componi_inode_dir(unsigned char *dst, unsigned int modo,
                              unsigned int link, unsigned int blocco_dati)
{
    azzera_buffer(dst, INODE_SIZE);

    p16(dst +  0, modo);
    p16(dst +  2, 0);                   /* uid root */
    p32(dst +  4, BS);                  /* dimensione: un blocco */
    p32(dst +  8, 0);                   /* atime */
    p32(dst + 12, 0);                   /* ctime */
    p32(dst + 16, 0);                   /* mtime */
    p32(dst + 20, 0);                   /* dtime: non cancellato */
    p16(dst + 24, 0);                   /* gid root */
    p16(dst + 26, link);

    /* i_blocks e' in unita' da 512 BYTE, non in blocchi del filesystem.
     * E' l'errore piu' facile di tutto il formato: un blocco da 1024 vale
     * DUE. Con il valore sbagliato e2fsck segnala ogni singolo inode. */
    p32(dst + 28, BS / 512u);

    p32(dst + 32, 0);                   /* i_flags */
    p32(dst + 40, blocco_dati);         /* i_block[0] */
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

/* =============================================================================
 * Scrittura
 * ============================================================================= */
int ext2_scrivi(const char *dev, const Ext2Geo *g, const char *etichetta,
                unsigned int uuid_seme, int *toccato)
{
    unsigned int gruppo, i;
    unsigned int inode_liberi = g->inode_totali - PRIMO_INODE;
    unsigned int radice_blk, lostfound_blk;
    unsigned int meta0 = PRIMO_DATO + 1u + g->gdt_blocchi;
    int r;

    /* Il blocco 0 e i due blocchi di dati del gruppo 0. La radice e
     * lost+found stanno subito dopo i metadati del gruppo 0. */
    radice_blk    = meta0 + 2u + g->itab_blocchi;
    lostfound_blk = radice_blk + 1u;

    /* --- 1. il volume vecchio smette di essere riconoscibile ------------
     * Stesso ragionamento del ramo FAT: il superblocco vecchio se ne va
     * per primo, cosi' un'interruzione lascia un volume che nessuno monta
     * invece di uno che descrive strutture ormai azzerate. */
    printf("  azzero il superblocco precedente\n");
    r = azzera_blocchi(dev, 0, 2);
    if (r < 0) return r;
    *toccato = 1;

    /* --- 2. le tabelle inode ------------------------------------------
     * Vanno azzerate tutte: i byte che c'erano prima verrebbero letti come
     * inode con modo, dimensione e puntatori a blocchi qualunque. */
    for (gruppo = 0; gruppo < g->gruppi; gruppo++) {
        unsigned int primo = PRIMO_DATO + gruppo * g->blocchi_per_gruppo;
        unsigned int meta  = primo;

        if (ha_copia(gruppo)) meta += 1u + g->gdt_blocchi;

        if ((gruppo % 16u) == 0 || gruppo == g->gruppi - 1u)
            printf("  azzero la tabella inode del gruppo %u/%u\n",
                   gruppo + 1u, g->gruppi);

        r = azzera_blocchi(dev, meta + 2u, g->itab_blocchi);
        if (r < 0) return r;
    }

    /* --- 3. bitmap di ogni gruppo -------------------------------------- */
    printf("  scrivo le bitmap dei %u gruppi\n", g->gruppi);
    for (gruppo = 0; gruppo < g->gruppi; gruppo++) {
        unsigned int primo = PRIMO_DATO + gruppo * g->blocchi_per_gruppo;
        unsigned int meta  = primo;

        if (ha_copia(gruppo)) meta += 1u + g->gdt_blocchi;

        componi_bitmap_blocchi(g, gruppo);
        r = scrivi_blocco(dev, meta, blocco);
        if (r < 0) return r;

        componi_bitmap_inode(g, gruppo);
        r = scrivi_blocco(dev, meta + 1u, blocco);
        if (r < 0) return r;
    }

    /* --- 4. gli inode della radice e di lost+found ---------------------
     * L'inode n occupa il byte (n-1)*INODE_SIZE della tabella del proprio
     * gruppo. Con inode da 128 byte in blocchi da 1024 ce ne stanno OTTO
     * per blocco, e i due che servono NON cadono nello stesso: la radice
     * e' l'inode 2, indice 1, primo blocco; lost+found e' l'11, indice
     * 10, cioe' il SECONDO blocco all'offset (10-8)*128 = 256.
     *
     * Darlo per scontato — scriverli entrambi nel primo blocco — significa
     * scrivere 256 byte oltre la fine del buffer e lasciare lost+found
     * senza inode. */
    {
        unsigned int itab0    = meta0 + 2u;
        unsigned int per_blk  = BS / INODE_SIZE;    /* 8 */
        unsigned int idx_root = 2u - 1u;            /* inode 2  */
        unsigned int idx_lf   = PRIMO_INODE - 1u;   /* inode 11 */

        /* Primo blocco della tabella: la radice, 0755, e TRE collegamenti
         * — "." dentro di se', ".." dentro di se' (la radice e' padre di
         * se stessa) e ".." dentro lost+found. Il valore 2, che verrebbe
         * naturale, fa segnalare a e2fsck un conteggio sbagliato. */
        azzera_buffer(blocco, BS);
        componi_inode_dir(blocco + (idx_root % per_blk) * INODE_SIZE,
                          0x41EDu, 3, radice_blk);

        printf("  scrivo l'inode della radice\n");
        r = scrivi_blocco(dev, itab0 + idx_root / per_blk, blocco);
        if (r < 0) return r;

        /* Secondo blocco: lost+found, 0700. Ci finiscono i file che hanno
         * perso il nome, e i loro contenuti non devono diventare
         * leggibili a tutti solo perche' il filesystem si e' rotto. */
        azzera_buffer(blocco, BS);
        componi_inode_dir(blocco + (idx_lf % per_blk) * INODE_SIZE,
                          0x41C0u, 2, lostfound_blk);

        printf("  scrivo l'inode di /lost+found\n");
        r = scrivi_blocco(dev, itab0 + idx_lf / per_blk, blocco);
        if (r < 0) return r;
    }

    /* --- 5. il contenuto delle due directory --------------------------- */
    {
        unsigned int off = 0;

        azzera_buffer(blocco, BS);
        off += voce_dir(blocco + off, 2, ".",  1, 12);
        off += voce_dir(blocco + off, 2, "..", 2, 12);
        /* L'ultima voce arriva fino in fondo al blocco: chi legge si
         * ferma quando la somma dei rec_len raggiunge la dimensione, e un
         * rec_len corto lascerebbe uno spazio che verrebbe interpretato
         * come una voce con inode 0 di lunghezza 0 — cioe' un ciclo. */
        voce_dir(blocco + off, PRIMO_INODE, "lost+found", 10, BS - off);

        printf("  scrivo la directory radice\n");
        r = scrivi_blocco(dev, radice_blk, blocco);
        if (r < 0) return r;

        azzera_buffer(blocco, BS);
        off  = voce_dir(blocco, 11, ".", 1, 12);
        voce_dir(blocco + off, 2, "..", 2, BS - off);

        printf("  scrivo /lost+found\n");
        r = scrivi_blocco(dev, lostfound_blk, blocco);
        if (r < 0) return r;
    }

    /* --- 6. la tabella dei descrittori, in ogni copia ------------------- */
    printf("  scrivo la tabella dei descrittori\n");
    for (gruppo = 0; gruppo < g->gruppi; gruppo++) {
        unsigned int primo, blocco_gdt, n;

        if (!ha_copia(gruppo)) continue;

        primo = PRIMO_DATO + gruppo * g->blocchi_per_gruppo;

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

    /* --- 7. i superblocchi, PER ULTIMI --------------------------------
     * Come nel ramo FAT: finche' non c'e' il superblocco il volume non e'
     * un ext2 per nessuno, e una formattazione interrotta si riconosce
     * per quello che e'. La copia primaria si scrive per ultima fra le
     * ultime: e' quella che tutti guardano. */
    for (i = g->gruppi; i > 0; i--) {
        unsigned int gr = i - 1u;

        if (!ha_copia(gr)) continue;

        componi_super(g, etichetta, uuid_seme, gr, g->liberi, inode_liberi);
        r = scrivi_blocco(dev, PRIMO_DATO + gr * g->blocchi_per_gruppo, blocco);
        if (r < 0) return r;
    }

    printf("  scritti superblocco e copie\n");
    return 0;
}

/* =============================================================================
 * bin/chkdsk/chkdsk.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Controllo e riparazione di un volume FAT12, FAT16, FAT32, ext2, ext3 o ext4.
 *
 *   chkdsk <partizione>          controlla e riferisce, non tocca niente
 *   chkdsk -r <partizione>       controlla e CORREGGE
 *
 * -----------------------------------------------------------------------------
 * ! LAVORA SOLO SU UNA PARTIZIONE NON MONTATA, e non e' un fastidio da
 * aggirare: e' la condizione perche' il risultato voglia dire qualcosa.
 *
 * Sopra un volume montato c'e' una cache write-back (vedi kernel/fs/fat.c):
 * meta' delle modifiche recenti sta in RAM e non sul supporto. Un
 * controllore che leggesse i settori grezzi vedrebbe uno stato che non e'
 * ne' quello vecchio ne' quello nuovo, segnalerebbe incoerenze inventate,
 * e — riparando — riscriverebbe settori che il primo `sync` del driver
 * ricoprirebbe subito dopo. Il kernel rifiuta blkread/blkwrite su una
 * partizione montata, e questo programma non prova nemmeno ad aggirarlo:
 * chiede di smontare.
 *
 * -----------------------------------------------------------------------------
 * ! NON CORREGGE SE NON GLIELO SI CHIEDE. Senza `-r` non scrive un solo
 * settore. Un controllore che ripara di sua iniziativa e' il modo piu'
 * rapido di trasformare un volume danneggiato in un volume danneggiato
 * DIVERSAMENTE, senza che nessuno abbia visto com'era prima.
 *
 * -----------------------------------------------------------------------------
 * COSA CONTROLLA, e in quest'ordine — ogni passo si fida solo di quelli
 * gia' fatti:
 *
 *   1. il BPB: i numeri devono essere coerenti fra loro, e da loro si
 *      ricava il TIPO (12/16/32). ! Il tipo NON si legge dall'etichetta
 *      "FAT16   " nel settore di avvio: quella e' una stringa decorativa
 *      che chiunque puo' aver scritto sbagliata. Si ricava dal NUMERO DI
 *      CLUSTER, che e' l'unico criterio del formato.
 *   2. le copie della FAT: devono essere identiche;
 *   3. le catene di cluster, percorrendo tutte le directory: cluster
 *      condivisi da due file, catene che escono dal volume, anelli;
 *   4. la dimensione dichiarata di ogni file contro quella della sua
 *      catena;
 *   5. i cluster perduti: allocati nella FAT e nominati da nessuno.
 *
 * -----------------------------------------------------------------------------
 * ! L'ORDINE DELLE RIPARAZIONI NON E' CASUALE
 *
 * Prima si TRONCANO le catene rotte, poi si liberano i perduti. Al
 * contrario, una catena che esce dal volume verrebbe percorsa fino in
 * fondo per contare i suoi cluster, e "in fondo" non c'e'.
 *
 * I cluster condivisi NON si riparano da soli. Sapere quale dei due file
 * abbia diritto ai dati non e' una cosa che un programma possa dedurre:
 * si segnala e si lascia decidere. Ripararli d'ufficio significherebbe
 * scegliere a caso quale dei due rovinare.
 * ============================================================================= */
#include "libc.h"
#include "../mkfs/crc32c.h"

/* +0.001 a ogni modifica: `chkdsk -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("chkdsk", "0.003");

#define SETT            512u
#define MAX_PROF        16      /* annidamento massimo delle directory */
#define TRANCHE         32u     /* settori per blkread: sotto BLKIO_MAX_SETT */

/* --- Il volume, letto una volta e poi usato da tutti --------------------- */
static char         dev[64];
static int          ripara = 0;
/* ! LA SUPERFICIE SI LEGGE SOLO SE LA SI CHIEDE, e non e' prudenza: su un
 * volume da 57 GB sono ore, mentre il controllo delle strutture sono minuti.
 * Chi ha fretta di sapere se il filesystem regge non deve pagare la lettura
 * di ogni settore; chi cerca i settori andati la chiede. */
static int          scansiona = 0;
static unsigned int problemi = 0, corretti = 0;

static unsigned int sett_per_clus, sett_riservati, n_fat, voci_root;
static unsigned int sett_per_fat, sett_totali, primo_dato, n_cluster;
static unsigned int root_clus;          /* FAT32 */
static unsigned int tipo;               /* 12, 16 o 32 */
static unsigned int fine_catena;        /* primo valore che significa "fine" */

/* =============================================================================
 * I CLUSTER CHE NON SI LEGGONO, SU FAT
 *
 * ! QUI L'UNITA' E' IL CLUSTER, non il settore, e non e' un dettaglio: la FAT
 * sa dire «difettoso» di un cluster e basta. Un settore andato in mezzo a un
 * cluster da 32 KB porta via tutto il cluster — e' il formato a decidere la
 * grana, non noi.
 *
 * ! E IL VALORE «DIFETTOSO» E' fine_catena - 1 IN TUTT'E TRE I FORMATI:
 * 0xFF7 su FAT12, 0xFFF7 su FAT16, 0x0FFFFFF7 su FAT32. Scriverlo cosi' invece
 * di tre costanti vuol dire che chi aggiungesse un quarto formato non se lo
 * dimentica in un ramo.
 * ============================================================================= */
static unsigned char *fat_bad;
static unsigned int   fat_bad_n, fat_bad_in_uso;

/* Dove sta il puntatore al PRIMO cluster di un file: nella sua voce di
 * directory. Li' riempie controlla_voce, e servono a spostarlo. */
static unsigned int   fat_dir_lba, fat_dir_idx;

typedef struct {
    unsigned int cluster;      /* quello andato                              */
    unsigned int prec;         /* 0 = e' il primo: il puntatore sta nella voce */
    unsigned int dir_lba;      /* la voce di directory, se prec e' 0          */
    unsigned int dir_idx;
} FatSpostare;

#define FAT_SPOSTARE_MAX 4096
static FatSpostare  fat_da_spostare[FAT_SPOSTARE_MAX];
static unsigned int fat_da_spostare_n;

/* Bitmap dei cluster gia' visti, uno per bit: serve a scoprire i condivisi
 * e i perduti. Su un volume da 512 MB con cluster da 4 KB sono 16 KB. */
static unsigned char *visto;
static unsigned int   visto_byte;

/* Un settore della FAT tenuto in mano, piu' quello successivo.
 *
 * ! I DUE SETTORI SERVONO PER FAT12, e chi li omette scrive un
 * controllore che sbaglia proprio dove il formato e' piu' scomodo: li' una
 * voce occupa DODICI BIT, quindi puo' cominciare nell'ultimo byte di un
 * settore e finire nel primo del seguente. Con un buffer da un settore
 * solo, quelle voci si leggono meta' giuste. */
static unsigned char fatbuf[SETT * 2];
static unsigned int  fatbuf_sett = 0xFFFFFFFFu;

static unsigned int le16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int le32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void guasto(const char *msg)
{
    printf("  ! %s\n", msg);
    problemi++;
}

/* =============================================================================
 * Accesso alla FAT
 *
 * Si legge a due settori per volta e si tiene l'ultima coppia: le catene
 * si percorrono in avanti e i cluster vicini stanno quasi sempre nello
 * stesso settore, quindi una cache di uno basta a evitare la stragrande
 * maggioranza delle letture.
 * ============================================================================= */
static int fat_carica(unsigned int s)
{
    if (s == fatbuf_sett) return 0;

    if (blkread(dev, sett_riservati + s, 2, fatbuf) < 0) {
        /* L'ultimo settore della FAT non ha un successivo: si rilegge da
         * solo, e il secondo mezzo buffer resta com'era. Non serve
         * azzerarlo: nessuna voce valida ci cade dentro. */
        if (blkread(dev, sett_riservati + s, 1, fatbuf) < 0) return -1;
    }
    fatbuf_sett = s;
    return 0;
}

/* Ritorna il valore della voce `c`, o 0xFFFFFFFF se non si e' potuta
 * leggere. Per FAT12 gestisce la voce a cavallo di due settori. */
static unsigned int fat_leggi(unsigned int c)
{
    unsigned int off, s, dentro;

    if (tipo == 12) {
        off = c + (c / 2);                  /* c * 1.5 */
    } else if (tipo == 16) {
        off = c * 2;
    } else {
        off = c * 4;
    }

    s      = off / SETT;
    dentro = off % SETT;

    if (fat_carica(s) != 0) return 0xFFFFFFFFu;

    if (tipo == 12) {
        unsigned int v = (unsigned int)fatbuf[dentro] |
                         ((unsigned int)fatbuf[dentro + 1] << 8);
        return (c & 1u) ? (v >> 4) : (v & 0x0FFFu);
    }
    if (tipo == 16) return le16(fatbuf + dentro);
    return le32(fatbuf + dentro) & 0x0FFFFFFFu;
}

/* Scrive una voce in TUTTE le copie della FAT: tenerle allineate e' il
 * senso stesso di averne due. */
static int fat_scrivi(unsigned int c, unsigned int val)
{
    unsigned char s[SETT];
    unsigned int  off, sett_rel, dentro, f;

    if (tipo == 12)      off = c + (c / 2);
    else if (tipo == 16) off = c * 2;
    else                 off = c * 4;

    sett_rel = off / SETT;
    dentro   = off % SETT;

    /* ! FAT12 A CAVALLO: se la voce comincia nell'ultimo byte del settore
     * non si puo' scrivere con una lettura-modifica di un settore solo.
     * Si rinuncia e lo si dice, invece di scrivere meta' valore — che
     * sarebbe un danno nuovo, causato dallo strumento che doveva
     * ripararne uno. */
    if (tipo == 12 && dentro == SETT - 1) {
        printf("    (voce FAT12 %u a cavallo di due settori: non corretta)\n", c);
        return -1;
    }

    for (f = 0; f < n_fat; f++) {
        unsigned int lba = sett_riservati + f * sett_per_fat + sett_rel;

        if (blkread(dev, lba, 1, s) < 0) return -1;

        if (tipo == 12) {
            unsigned int v = (unsigned int)s[dentro] |
                             ((unsigned int)s[dentro + 1] << 8);
            if (c & 1u) v = (v & 0x000Fu) | ((val & 0x0FFFu) << 4);
            else        v = (v & 0xF000u) | (val & 0x0FFFu);
            s[dentro]     = (unsigned char)v;
            s[dentro + 1] = (unsigned char)(v >> 8);
        } else if (tipo == 16) {
            s[dentro]     = (unsigned char)val;
            s[dentro + 1] = (unsigned char)(val >> 8);
        } else {
            /* ! I QUATTRO BIT ALTI NON SONO NOSTRI. Su FAT32 una voce usa
             * 28 bit; gli altri quattro appartengono all'implementazione e
             * vanno conservati, non azzerati per comodita'. */
            unsigned int v = le32(s + dentro);
            v = (v & 0xF0000000u) | (val & 0x0FFFFFFFu);
            s[dentro]     = (unsigned char)v;
            s[dentro + 1] = (unsigned char)(v >> 8);
            s[dentro + 2] = (unsigned char)(v >> 16);
            s[dentro + 3] = (unsigned char)(v >> 24);
        }

        if (blkwrite(dev, lba, 1, s) < 0) return -1;
    }

    fatbuf_sett = 0xFFFFFFFFu;      /* la cache non vale piu' */
    return 0;
}

static int cluster_valido(unsigned int c)
{
    return c >= 2 && c < n_cluster + 2;
}

static unsigned int primo_settore_di(unsigned int c)
{
    return primo_dato + (c - 2) * sett_per_clus;
}

/* =============================================================================
 * 1. Il settore di avvio
 *
 * ! IL TIPO SI RICAVA DAL NUMERO DI CLUSTER, non dalla stringa nel
 * settore di avvio. "FAT16   " a offset 54 e' decorativa: la scrive chi
 * formatta e nessuno la verifica mai, quindi su un volume malandato e'
 * proprio uno dei campi di cui non ci si puo' fidare. I due confini —
 * 4085 e 65525 — sono quelli del formato, non una convenzione locale.
 * ============================================================================= */
static int fat_apri(void)
{
    unsigned char s[SETT];
    unsigned int  radice_sett, dati_sett;

    if (blkread(dev, 0, 1, s) < 0) {
        printf("  ! non riesco a leggere il settore di avvio\n");
        return -1;
    }

    if (s[510] != 0x55 || s[511] != 0xAA) {
        guasto("firma 0x55AA assente: non sembra un settore di avvio");
    }

    if (le16(s + 11) != SETT) {
        printf("  ! byte per settore = %u: questo strumento gestisce solo 512\n",
               le16(s + 11));
        return -1;
    }

    sett_per_clus  = s[13];
    sett_riservati = le16(s + 14);
    n_fat          = s[16];
    voci_root      = le16(s + 17);
    sett_totali    = le16(s + 19);
    sett_per_fat   = le16(s + 22);

    if (sett_totali == 0)  sett_totali  = le32(s + 32);
    if (sett_per_fat == 0) sett_per_fat = le32(s + 36);

    /* I controlli che devono passare, o il resto non ha senso. */
    if (sett_per_clus == 0 || (sett_per_clus & (sett_per_clus - 1)) != 0) {
        printf("  ! settori per cluster = %u: non e' una potenza di due\n",
               sett_per_clus);
        return -1;
    }
    if (sett_riservati == 0) { printf("  ! settori riservati = 0\n"); return -1; }
    if (n_fat == 0 || n_fat > 4) {
        printf("  ! numero di FAT = %u: fuori da ogni uso reale\n", n_fat);
        return -1;
    }
    if (sett_per_fat == 0)  { printf("  ! settori per FAT = 0\n"); return -1; }
    if (sett_totali == 0)   { printf("  ! settori totali = 0\n"); return -1; }

    radice_sett = ((voci_root * 32u) + SETT - 1u) / SETT;
    primo_dato  = sett_riservati + n_fat * sett_per_fat + radice_sett;

    if (primo_dato >= sett_totali) {
        printf("  ! l'area dati comincia a %u ma il volume ha %u settori\n",
               primo_dato, sett_totali);
        return -1;
    }

    dati_sett = sett_totali - primo_dato;
    n_cluster = dati_sett / sett_per_clus;

    if (n_cluster < 4085u)       tipo = 12;
    else if (n_cluster < 65525u) tipo = 16;
    else                         tipo = 32;

    if (tipo == 32) {
        root_clus   = le32(s + 44);
        fine_catena = 0x0FFFFFF8u;
        if (voci_root != 0)
            guasto("FAT32 con voci di root diverse da zero nel BPB");
        if (!cluster_valido(root_clus)) {
            printf("  ! cluster della root = %u: fuori dal volume\n", root_clus);
            return -1;
        }
    } else if (tipo == 16) {
        fine_catena = 0xFFF8u;
        if (voci_root == 0) { guasto("FAT16 senza voci di root nel BPB"); }
    } else {
        fine_catena = 0x0FF8u;
        if (voci_root == 0) { guasto("FAT12 senza voci di root nel BPB"); }
    }

    printf("  tipo FAT%u - %u cluster da %u settori, %u FAT da %u settori\n",
           tipo, n_cluster, sett_per_clus, n_fat, sett_per_fat);

    /* ! La FAT dev'essere abbastanza grande da contenere tutte le voci.
     * Una FAT corta e' un guasto che si manifesta solo sui cluster alti,
     * cioe' quando il volume comincia a riempirsi. */
    {
        unsigned int serve;
        if (tipo == 12)      serve = ((n_cluster + 2u) * 3u + 1u) / 2u;
        else if (tipo == 16) serve = (n_cluster + 2u) * 2u;
        else                 serve = (n_cluster + 2u) * 4u;
        serve = (serve + SETT - 1u) / SETT;

        if (sett_per_fat < serve) {
            printf("  ! la FAT e' di %u settori, per %u cluster ne servono %u\n",
                   sett_per_fat, n_cluster, serve);
            problemi++;
        }
    }

    return 0;
}

/* =============================================================================
 * 2. Le copie della FAT
 *
 * ! SI CONFRONTANO, NON SI ALLINEANO ALLA CIECA. Quando differiscono, la
 * prima non e' "quella giusta" per definizione: e' solo quella che i
 * driver usano. Si dice quanti settori differiscono e, con -r, si copia la
 * prima sulle altre — che e' la scelta convenzionale e va detta, non
 * sottintesa.
 * ============================================================================= */
static void controlla_copie(void)
{
    unsigned char a[SETT * 4], b[SETT * 4];
    unsigned int  f, s, diverse = 0;

    if (n_fat < 2) return;

    printf("\nCopie della FAT\n");

    for (f = 1; f < n_fat; f++) {
        for (s = 0; s < sett_per_fat; s += 4) {
            unsigned int n = (sett_per_fat - s < 4) ? sett_per_fat - s : 4;
            unsigned int i;

            if (blkread(dev, sett_riservati + s, n, a) < 0) break;
            if (blkread(dev, sett_riservati + f * sett_per_fat + s, n, b) < 0) break;

            for (i = 0; i < n * SETT; i++) {
                if (a[i] != b[i]) { diverse++; break; }
            }
        }
    }

    if (diverse == 0) {
        printf("  = le %u copie sono identiche\n", n_fat);
        return;
    }

    printf("  ! %u tranche differiscono fra la prima FAT e le altre\n", diverse);
    problemi++;

    if (!ripara) return;

    for (f = 1; f < n_fat; f++) {
        for (s = 0; s < sett_per_fat; s += 4) {
            unsigned int n = (sett_per_fat - s < 4) ? sett_per_fat - s : 4;
            if (blkread(dev, sett_riservati + s, n, a) < 0) break;
            if (blkwrite(dev, sett_riservati + f * sett_per_fat + s, n, a) < 0) break;
        }
    }
    printf("  + copiata la prima FAT sulle altre %u\n", n_fat - 1);
    corretti++;
}

/* =============================================================================
 * 3-4. Le catene e le directory
 * ============================================================================= */
static int gia_visto(unsigned int c)
{
    return (visto[c >> 3] >> (c & 7)) & 1;
}

static void segna(unsigned int c)
{
    visto[c >> 3] |= (unsigned char)(1u << (c & 7));
}

/* Percorre la catena che comincia in `primo`, marcando i cluster.
 * Riporta in *quanti quanti cluster ha contato.
 *
 * ! TRE MODI DI FINIRE MALE, e vanno distinti perche' la riparazione e'
 * diversa: uscire dal volume, girare in tondo, incrociare la catena di un
 * altro file. Trattarli come un solo "catena rotta" toglierebbe a chi
 * legge l'unica informazione utile.
 *
 * Ritorna 0 se la catena e' sana, -1 se e' rotta (e in quel caso
 * *ultimo_buono contiene l'ultimo cluster percorribile, per il
 * troncamento).
 */
static int percorri(unsigned int primo, const char *nome,
                    unsigned int *quanti, unsigned int *ultimo_buono)
{
    unsigned int c = primo, n = 0, prec = 0;

    *quanti = 0;
    *ultimo_buono = 0;

    while (cluster_valido(c)) {
        unsigned int succ;

        if (gia_visto(c)) {
            printf("  ! %s: il cluster %u appartiene gia' a un altro file\n",
                   nome, c);
            printf("    (non si ripara da solo: quale dei due abbia diritto\n");
            printf("     ai dati non e' una cosa che si possa dedurre)\n");
            problemi++;
            *ultimo_buono = prec;
            return -1;
        }
        /* ! IL FILE DANNEGGIATO SI NOMINA QUI, mentre gli si cammina sopra:
         * la mappa dei cluster andati e' gia' pronta (la scansione viene
         * prima) e il cluster PRECEDENTE lo sappiamo solo adesso — dopo, per
         * risalirci, bisognerebbe ripercorrere la catena da capo. */
        if (fat_bad && (fat_bad[c >> 3] >> (c & 7)) & 1) {
            fat_bad_in_uso++;
            if (fat_bad_in_uso <= 20u)
                printf("  ! %s: il cluster %u non si legge\n", nome, c);
            else if (fat_bad_in_uso == 21u)
                printf("  ! (gli altri file danneggiati non li elenco)\n");
            problemi++;

            if (fat_da_spostare_n < FAT_SPOSTARE_MAX) {
                fat_da_spostare[fat_da_spostare_n].cluster = c;
                fat_da_spostare[fat_da_spostare_n].prec    = prec;
                fat_da_spostare[fat_da_spostare_n].dir_lba = fat_dir_lba;
                fat_da_spostare[fat_da_spostare_n].dir_idx = fat_dir_idx;
                fat_da_spostare_n++;
            }
        }

        segna(c);
        n++;
        *quanti = n;
        prec = c;

        /* Un anello si riconosce dalla lunghezza: una catena non puo'
         * essere piu' lunga del numero di cluster del volume. */
        if (n > n_cluster) {
            printf("  ! %s: la catena non finisce (anello)\n", nome);
            problemi++;
            *ultimo_buono = prec;
            return -1;
        }

        succ = fat_leggi(c);
        if (succ == 0xFFFFFFFFu) {
            printf("  ! %s: FAT illeggibile al cluster %u\n", nome, c);
            problemi++;
            return -1;
        }
        if (succ >= fine_catena) { *ultimo_buono = prec; return 0; }   /* fine */

        if (!cluster_valido(succ)) {
            printf("  ! %s: dal cluster %u la catena punta a %u, fuori dal volume\n",
                   nome, c, succ);
            problemi++;
            *ultimo_buono = prec;
            return -1;
        }
        c = succ;
    }

    printf("  ! %s: primo cluster %u non valido\n", nome, primo);
    problemi++;
    return -1;
}

/* =============================================================================
 * I NOMI LUNGHI (VFAT)
 *
 * Un nome lungo non e' un campo: e' una FILA di voci finte che PRECEDONO
 * quella 8.3 vera, in ordine ROVESCIATO — l'ultimo pezzo del nome sta per
 * primo. Ogni voce ha attributo 0x0F (che sui sistemi vecchi la fa
 * ignorare: era il trucco per restare compatibili), il numero d'ordine nel
 * primo byte, e un CHECKSUM del nome 8.3 nel byte 13.
 *
 * ! IL CHECKSUM E' L'UNICA COSA CHE LEGA I DUE PEZZI. Non c'e' nessun
 * puntatore: la fila e la voce 8.3 stanno vicine e basta. Se qualcuno
 * rinomina il file toccando solo la voce 8.3 — e il nostro fat_rename fa
 * esattamente questo — il checksum non torna piu' e il nome lungo comincia
 * a nominare un file che non e' piu' quello. E' il motivo per cui questo
 * controllo serve davvero qui: non e' un caso di scuola, e' un difetto che
 * questo sistema puo' produrre.
 *
 * Cosa si controlla:
 *   - frammenti ORFANI: una fila che non finisce su una voce 8.3;
 *   - checksum che non corrisponde al nome 8.3 che segue;
 *   - numerazione rotta: deve scendere N, N-1, ... 1, e il primo della
 *     fila deve avere il bit 0x40 (fine del nome);
 *   - primo cluster diverso da zero, che in una voce di nome lungo non ha
 *     nessun significato.
 * ============================================================================= */
static unsigned char checksum_83(const unsigned char *n83)
{
    unsigned char s = 0;
    int i;

    for (i = 0; i < 11; i++)
        s = (unsigned char)(((s & 1u) ? 0x80u : 0u) + (s >> 1) + n83[i]);
    return s;
}

/* Stato del giro di UNA directory. ! Deve vivere piu' a lungo di un
 * settore: una fila di frammenti puo' cominciare nell'ultimo settore di un
 * cluster e finire nel primo del successivo, e chi tiene lo stato dentro
 * il ciclo dei settori la vede spezzata. */
typedef struct {
    unsigned int  clus_proprio;   /* primo cluster di questa directory */
    unsigned int  clus_padre;     /* ...e del suo padre (0 = root) */
    unsigned int  indice;         /* quante voci si sono gia' viste */
    /* Fila di nomi lunghi in corso */
    int           lfn_aperta;     /* 1 se stiamo dentro una fila */
    unsigned int  lfn_attesa;     /* numero d'ordine che ci si aspetta ora */
    unsigned char lfn_somma;      /* checksum dichiarato dalla fila */
    unsigned int  lfn_pezzi;      /* quanti frammenti si sono contati */
    int           lfn_rotta;      /* la fila e' gia' stata segnalata */
} GiroDir;

static void nome_leggibile(const unsigned char *v, char *out)
{
    int i, k = 0;

    for (i = 0; i < 8 && v[i] != ' '; i++) out[k++] = (char)v[i];
    if (v[8] != ' ') {
        out[k++] = '.';
        for (i = 8; i < 11 && v[i] != ' '; i++) out[k++] = (char)v[i];
    }
    out[k] = '\0';
}

static void controlla_dir(unsigned int clus, unsigned int clus_padre,
                          const char *percorso, int prof);

/* =============================================================================
 * "." e ".." — le due voci che ogni sottodirectory deve avere
 *
 * ! SONO LE UNICHE VOCI DI CUI SI CONOSCE IN ANTICIPO IL CONTENUTO, e per
 * questo sono le uniche su cui un controllore possa dire qualcosa di
 * definitivo. "." deve puntare alla directory stessa e ".." al padre; un
 * ".." sbagliato non impedisce di leggere i file, ma manda fuori strada
 * chiunque risalga l'albero — compreso un altro controllore.
 *
 * ! IL ".." DELLA ROOT VALE ZERO ANCHE SU FAT32, dove la root un cluster
 * ce l'ha. E' una convenzione del formato, non una svista: zero significa
 * "la root", e scriverci il numero vero sarebbe l'errore.
 * ============================================================================= */
static void controlla_punti(const unsigned char *v, int e_punto_punto,
                            unsigned int atteso, const char *percorso)
{
    unsigned int p = le16(v + 26);

    if (tipo == 32) p |= le16(v + 20) << 16;

    if ((v[11] & 0x10) == 0) {
        printf("  ! %s/%s: non e' marcata come directory\n",
               percorso, e_punto_punto ? ".." : ".");
        problemi++;
        return;
    }

    if (p != atteso) {
        printf("  ! %s/%s punta al cluster %u, dovrebbe essere %u\n",
               percorso, e_punto_punto ? ".." : ".", p, atteso);
        problemi++;
    }
}

/* Controlla una voce di directory gia' letta. */
static void controlla_voce(unsigned char *v, unsigned int lba, unsigned int idx,
                           const char *percorso, int prof, GiroDir *g)
{
    char         nome[16], pieno[128];
    unsigned int primo, dim, quanti = 0, ultimo = 0;
    int          e_dir;

    if (v[0] == 0x00) return;

    /* Una voce cancellata interrompe comunque una fila di nomi lunghi in
     * corso: i frammenti rimasti non nominano piu' niente. */
    if (v[0] == 0xE5) { g->lfn_aperta = 0; g->lfn_pezzi = 0; return; }

    /* --- frammento di nome lungo ------------------------------------- */
    if ((v[11] & 0x0F) == 0x0F) {
        unsigned int ord = v[0] & 0x3Fu;
        int          ultimo_pezzo = (v[0] & 0x40u) != 0;

        if (le16(v + 26) != 0) {
            printf("  ! %s: voce di nome lungo con primo cluster %u "
                   "(dovrebbe essere 0)\n", percorso, le16(v + 26));
            problemi++;
        }

        if (ultimo_pezzo) {
            /* Comincia una fila nuova: e' il pezzo FINALE del nome, che
             * nel formato sta per PRIMO. */
            g->lfn_aperta = 1;
            g->lfn_attesa = ord;
            g->lfn_somma  = v[13];
            g->lfn_pezzi  = 0;
            g->lfn_rotta  = 0;
        }

        if (!g->lfn_aperta) {
            if (!g->lfn_rotta) {
                printf("  ! %s: frammento di nome lungo senza inizio fila\n",
                       percorso);
                problemi++;
                g->lfn_rotta = 1;
            }
            return;
        }

        if (ord != g->lfn_attesa && !g->lfn_rotta) {
            printf("  ! %s: nome lungo con numerazione rotta "
                   "(atteso %u, trovato %u)\n", percorso, g->lfn_attesa, ord);
            problemi++;
            g->lfn_rotta = 1;
        }
        if (v[13] != g->lfn_somma && !g->lfn_rotta) {
            printf("  ! %s: frammenti di nome lungo con checksum diversi\n",
                   percorso);
            problemi++;
            g->lfn_rotta = 1;
        }

        if (g->lfn_attesa > 0) g->lfn_attesa--;
        g->lfn_pezzi++;
        return;
    }

    /* --- da qui in giu' e' una voce 8.3 vera -------------------------- */
    if (v[11] & 0x08) {                          /* etichetta di volume */
        g->lfn_aperta = 0; g->lfn_pezzi = 0;
        return;
    }

    /* ! IL CHECKSUM E' L'UNICO LEGAME fra la fila e questa voce: se non
     * torna, il nome lungo sta nominando un file diverso da quello che
     * segue. Succede a chi rinomina toccando solo la voce 8.3 — cosa che
     * il fat_rename di EX-OS fa — quindi non e' un caso di scuola. */
    if (g->lfn_aperta && !g->lfn_rotta) {
        if (g->lfn_attesa != 0) {
            printf("  ! %s: fila di nome lungo incompleta (mancano %u pezzi)\n",
                   percorso, g->lfn_attesa);
            problemi++;
        } else if (checksum_83(v) != g->lfn_somma) {
            char n83[16];
            nome_leggibile(v, n83);
            printf("  ! %s/%s: il nome lungo non corrisponde a questo nome 8.3\n",
                   percorso, n83);
            printf("    (checksum %u nella fila, %u calcolato: il nome lungo\n",
                   (unsigned)g->lfn_somma, (unsigned)checksum_83(v));
            printf("     nomina un file che non e' piu' questo)\n");
            problemi++;
        }
    }
    g->lfn_aperta = 0;
    g->lfn_pezzi  = 0;

    nome_leggibile(v, nome);

    /* "." e ".." si controllano invece di saltarli, ma NON si percorrono:
     * i loro cluster appartengono a questa directory e al padre, e
     * contarli di nuovo darebbe un "cluster gia' di un altro file" su ogni
     * sottodirectory del volume. */
    if (nome[0] == '.') {
        if (nome[1] == '\0')
            controlla_punti(v, 0, g->clus_proprio, percorso);
        else if (nome[1] == '.' && nome[2] == '\0') {
            /* ! IL ".." CHE PUNTA ALLA ROOT CONTIENE ZERO ANCHE SU FAT32,
             * dove la root un cluster ce l'ha davvero (di solito il 2).
             * E' una convenzione del formato, non una svista di chi
             * formatta: zero significa "la root". Confrontare con il numero
             * vero fa segnalare come guasta ogni directory di primo livello
             * di ogni volume FAT32 sano — che e' esattamente cio' che
             * questa prova ha fatto emergere al primo colpo. */
            unsigned int atteso = g->clus_padre;
            if (tipo == 32 && atteso == root_clus) atteso = 0;
            controlla_punti(v, 1, atteso, percorso);
        }
        else {
            printf("  ! %s/%s: nome che comincia per punto e non e' . ne' ..\n",
                   percorso, nome);
            problemi++;
        }
        return;
    }

    e_dir = (v[11] & 0x10) != 0;
    dim   = le32(v + 28);
    primo = le16(v + 26);
    if (tipo == 32) primo |= le16(v + 20) << 16;

    {
        int i = 0, j;
        for (j = 0; percorso[j] && i < 100; j++) pieno[i++] = percorso[j];
        if (i > 0 && pieno[i - 1] != '/') pieno[i++] = '/';
        for (j = 0; nome[j] && i < 126; j++) pieno[i++] = nome[j];
        pieno[i] = '\0';
    }

    /* ! Una directory con dimensione diversa da zero non e' un errore da
     * correggere: su FAT le directory hanno SEMPRE dimensione 0 nella loro
     * voce, e la lunghezza sta nella catena. Un valore diverso e' il segno
     * che qualcuno ha scritto li' dentro qualcosa che non doveva. */
    if (e_dir && dim != 0) {
        printf("  ! %s: e' una directory ma dichiara %u byte\n", pieno, dim);
        problemi++;
        if (ripara) {
            unsigned char s[SETT];
            if (blkread(dev, lba, 1, s) == 0) {
                s[idx * 32 + 28] = s[idx * 32 + 29] = 0;
                s[idx * 32 + 30] = s[idx * 32 + 31] = 0;
                if (blkwrite(dev, lba, 1, s) == 0) {
                    printf("  + %s: dimensione riportata a 0\n", pieno);
                    corretti++;
                }
            }
        }
    }

    /* Un file vuoto non ha catena, ed e' legittimo. */
    if (primo == 0) {
        if (!e_dir && dim != 0) {
            printf("  ! %s: dichiara %u byte ma non ha nessun cluster\n",
                   pieno, dim);
            problemi++;
        }
        return;
    }

    /* Dove sta scritto il primo cluster di questo file: serve a spostarlo se
     * proprio quello e' andato. Vedi FatSpostare. */
    fat_dir_lba = lba;
    fat_dir_idx = idx;

    if (percorri(primo, pieno, &quanti, &ultimo) != 0) {
        if (ripara && ultimo != 0) {
            if (fat_scrivi(ultimo, fine_catena | 0x7u) == 0) {
                printf("  + %s: catena troncata a %u cluster\n", pieno, quanti);
                corretti++;
            }
        }
        return;
    }

    if (e_dir) {
        /* Il padre della sottodirectory e' QUESTA directory. ! Per la root
         * di FAT12/16 vale 0, che e' anche cio' che il formato pretende
         * dentro il suo "..": le due cose combaciano da sole. */
        controlla_dir(primo, g->clus_proprio, pieno, prof + 1);
        return;
    }

    /* ! LA DIMENSIONE SI CONFRONTA CON UN INTERVALLO, non con un numero.
     * Un file di N byte occupa ceil(N / byte_per_cluster) cluster: qualunque
     * dimensione dentro quell'ultimo cluster e' coerente. Pretendere
     * l'uguaglianza esatta segnalerebbe come guasto ogni file che non
     * finisce su un confine di cluster, cioe' quasi tutti. */
    {
        unsigned int byte_clus = sett_per_clus * SETT;
        unsigned int minimo    = (quanti - 1) * byte_clus + 1;
        unsigned int massimo   = quanti * byte_clus;

        if (dim < minimo || dim > massimo) {
            printf("  ! %s: dichiara %u byte, la catena ne tiene %u\n",
                   pieno, dim, massimo);
            problemi++;

            if (ripara) {
                unsigned char s[SETT];
                if (blkread(dev, lba, 1, s) == 0) {
                    unsigned int nuovo = massimo;
                    s[idx * 32 + 28] = (unsigned char)nuovo;
                    s[idx * 32 + 29] = (unsigned char)(nuovo >> 8);
                    s[idx * 32 + 30] = (unsigned char)(nuovo >> 16);
                    s[idx * 32 + 31] = (unsigned char)(nuovo >> 24);
                    if (blkwrite(dev, lba, 1, s) == 0) {
                        printf("  + %s: dimensione portata a %u\n", pieno, nuovo);
                        corretti++;
                    }
                }
            }
        }
    }
}

/* Percorre una directory. `clus` == 0 significa la root di FAT12/16, che
 * non e' un cluster ma un'area fissa. */
static void controlla_dir(unsigned int clus, unsigned int clus_padre,
                          const char *percorso, int prof)
{
    unsigned char s[SETT];
    unsigned int  lba, resta, i;
    GiroDir       g;

    g.clus_proprio = clus;
    g.clus_padre   = clus_padre;
    g.indice       = 0;
    g.lfn_aperta   = 0;
    g.lfn_attesa   = 0;
    g.lfn_somma    = 0;
    g.lfn_pezzi    = 0;
    g.lfn_rotta    = 0;

    if (prof > MAX_PROF) {
        printf("  ! %s: annidamento oltre %d livelli, non scendo oltre\n",
               percorso, MAX_PROF);
        problemi++;
        return;
    }

    if (clus == 0) {
        /* Root di FAT12/16: settori consecutivi subito dopo le FAT. */
        lba   = sett_riservati + n_fat * sett_per_fat;
        resta = (voci_root * 32u + SETT - 1u) / SETT;

        while (resta > 0) {
            if (blkread(dev, lba, 1, s) < 0) return;
            for (i = 0; i < SETT / 32; i++)
                controlla_voce(s + i * 32, lba, i, percorso, prof, &g);
            lba++;
            resta--;
        }
        return;
    }

    /* Sottodirectory (e root di FAT32): si segue la catena.
     *
     * ! I cluster della directory sono gia' stati marcati da percorri()
     * prima di arrivare qui, quindi NON si rimarcano: rifarlo darebbe un
     * "cluster gia' di un altro file" su ogni directory del volume. */
    while (cluster_valido(clus)) {
        unsigned int succ, k;

        lba = primo_settore_di(clus);
        for (k = 0; k < sett_per_clus; k++) {
            if (blkread(dev, lba + k, 1, s) < 0) return;
            for (i = 0; i < SETT / 32; i++)
                controlla_voce(s + i * 32, lba + k, i, percorso, prof, &g);
        }

        succ = fat_leggi(clus);
        if (succ >= fine_catena || succ == 0xFFFFFFFFu) break;
        if (!cluster_valido(succ)) break;
        clus = succ;
    }

    /* ! Una fila di frammenti rimasta aperta alla fine della directory non
     * nomina niente: la voce 8.3 che avrebbe dovuto chiuderla non c'e'. */
    if (g.lfn_aperta && !g.lfn_rotta) {
        printf("  ! %s: la directory finisce con una fila di nome lungo "
               "senza il file che dovrebbe nominare\n",
               percorso[0] ? percorso : "/");
        problemi++;
    }
}

/* =============================================================================
 * 5. I cluster perduti
 *
 * Allocati nella FAT e nominati da nessuna directory. Non sono un danno in
 * se': sono spazio che il volume crede occupato e non lo e'. Nascono da
 * una scrittura interrotta — corrente tolta, supporto sfilato — ed e' la
 * cosa piu' comune che un controllore trova.
 *
 * ! SI LIBERANO, NON SI RECUPERANO IN FILE. Il `FOUND.000` di MS-DOS
 * raccoglieva ogni catena perduta in un file, ed era utile quando un
 * settore costava caro; qui sarebbe una collezione di frammenti senza nome
 * ne' struttura, che nessuno aprira' mai e che occupa lo stesso spazio che
 * si voleva recuperare. Liberarli e' onesto: quei dati non erano
 * raggiungibili prima e non lo sarebbero dopo.
 * ============================================================================= */
static void controlla_perduti(void)
{
    unsigned int c, perduti = 0, liberati = 0;

    printf("\nCluster perduti\n");

    for (c = 2; c < n_cluster + 2; c++) {
        unsigned int v;

        if (gia_visto(c)) continue;

        v = fat_leggi(c);
        if (v == 0xFFFFFFFFu) continue;
        if (v == 0) continue;                    /* libero: e' giusto cosi' */

        /* ! UN CLUSTER MARCATO DIFETTOSO NON E' PERDUTO, ED E' LA STESSA
         * TRAPPOLA DELL'INODE 1 SU ext2, vista dall'altra parte. Non e' libero
         * e non e' di nessun file — per definizione, e' proprio quello il suo
         * mestiere — quindi questo giro lo contava come «occupato che nessuno
         * nomina», e con -r lo avrebbe LIBERATO: cioe' rimesso in circolazione
         * un settore rotto, col programma che si chiama «controllo del disco».
         * Si e' visto il 22 settembre 2026, nella corsa di verifica subito
         * dopo la prima marcatura riuscita. */
        if (v == fine_catena - 1u) continue;

        perduti++;
        if (ripara && fat_scrivi(c, 0) == 0) liberati++;
    }

    if (perduti == 0) {
        printf("  = nessuno\n");
        return;
    }

    printf("  ! %u cluster risultano occupati ma nessun file li nomina\n",
           perduti);
    printf("    (%u KB)\n", perduti * sett_per_clus * SETT / 1024u);
    problemi++;

    if (liberati > 0) {
        printf("  + liberati %u cluster\n", liberati);
        corretti++;
    }
}

/* =============================================================================
 * Le due voci riservate
 *
 * FAT[0] contiene il descrittore di supporto, FAT[1] un valore di fine
 * catena. Non li usa nessuno per leggere, ma un valore assurdo li' dentro
 * e' il primo segno che il settore non e' una FAT.
 * ============================================================================= */
static void controlla_riservate(void)
{
    unsigned int f0 = fat_leggi(0), f1 = fat_leggi(1);

    printf("\nVoci riservate\n");

    if (f0 == 0xFFFFFFFFu || f1 == 0xFFFFFFFFu) {
        guasto("le prime due voci della FAT non si leggono");
        return;
    }

    if ((f0 & 0xFFu) < 0xF0u) {
        printf("  ! FAT[0] = 0x%x: il byte basso dovrebbe essere il "
               "descrittore di supporto (>= 0xF0)\n", f0);
        problemi++;
    } else {
        printf("  = FAT[0] = 0x%x, FAT[1] = 0x%x\n", f0, f1);
    }
}

/* =============================================================================
 * I MOTORI — un formato, cinque funzioni
 *
 * ! QUESTA FORMA E' SCELTA PER UN MOTIVO CHE OGGI NON SI VEDE. Il modo
 * naturale di aggiungere ext2 sarebbe stato un `if` in mezzo al codice;
 * invece ogni formato e' un oggetto con la stessa faccia. Costa qualche
 * riga in piu' adesso e rende MECCANICO, il giorno che EX-OS avesse il
 * caricamento dinamico per i programmi utente, spostare i motori in
 * librerie separate: la forma e' gia' quella di un plugin.
 *
 * Oggi non lo si fa perche' quel caricamento NON ESISTE — PT_DYNAMIC in
 * kernel/loader/elf.c e' solo un #define, e dynlink.c non lo chiama
 * nessuno — e introdurlo per un controllore di dischi significherebbe
 * collaudare una capacita' delicata sullo strumento che scrive sui
 * filesystem. Nel frattempo i due motori stanno nello stesso binario:
 * 20 KB in piu' su 763 KB liberi sul floppy.
 *
 * ! `riconosce` NON DEVE FIDARSI DI NIENTE. Gli si passa un volume che
 * potrebbe essere danneggiato proprio nei campi che dicono cosa sia: deve
 * guardare il minimo indispensabile (una firma, un paio di numeri
 * coerenti) e rispondere si' o no senza scrivere e senza allocare.
 * ============================================================================= */
/* =============================================================================
 * MOTORE ext2
 *
 * ! COSA CONTROLLA UN CONTROLLORE ext2, E PERCHE' NON SONO LE STESSE COSE
 * DI FAT. Su FAT l'allocazione E' la catena: una voce della FAT dice a chi
 * appartiene un cluster e qual e' il successivo, quindi un giro solo
 * risponde a tutto. Su ext2 le due informazioni sono SEPARATE — i blocchi
 * di un file stanno nel suo inode, e quali blocchi siano occupati sta in
 * una BITMAP a parte — e possono contraddirsi. Meta' del lavoro e'
 * proprio confrontarle.
 *
 * Da qui i cinque controlli, in quest'ordine:
 *
 *   1. il superblocco: firma, numeri coerenti, funzionalita' che non
 *      sappiamo leggere;
 *   2. i descrittori di gruppo: bitmap e tabella degli inode devono stare
 *      DENTRO il loro gruppo;
 *   3. l'albero delle directory da /: voci malformate, "." e "..",
 *      inode nominati che risultano liberi;
 *   4. i blocchi davvero usati (percorrendo gli inode) contro la BITMAP
 *      DEI BLOCCHI;
 *   5. i conteggi di liberi nel superblocco e nei descrittori contro la
 *      realta'.
 *
 * ! SI RIFIUTA DI RIPARARE UN VOLUME CON `incompat` SCONOSCIUTE. Un bit
 * di incompatibilita' che non conosciamo cambia il significato di campi
 * che crediamo di saper leggere. Un controllore che "ripara" in quelle
 * condizioni scrive spazzatura con la massima convinzione. Si legge, si
 * riferisce, e si dice che non si tocca.
 *
 * ext3 AND ext4 (chkdsk 0.003). This engine reads them all, and what they
 * add is handled here:
 *   - the journal: with needs_recovery set the volume on disk is not the real
 *     one. -r replays it first, as e2fsck does (e2_giornale); without -r
 *     nothing is written, so the check says what it saw and that it is
 *     provisional;
 *   - extents (e2_percorri_estesi, e2_mappa);
 *   - descriptors of s_desc_size bytes, all in memory (e2_gdt);
 *   - flex_bg: bitmaps and tables may be anywhere, so nothing assumes a
 *     position, every address comes from the descriptor;
 *   - groups marked BLOCK_UNINIT / INODE_UNINIT: their bitmap on disk means
 *     nothing, what counts is what they would hold;
 *   - metadata_csum and gdt_csum: checked, and written again with whatever
 *     -r rewrites. A wrong sum on an inode is reported and left alone: the
 *     inode's content may be the part that is wrong;
 *   - the inodes no directory names: quota, the orphan file;
 *   - extended attributes: their block is one more block of the file, and
 *     several files may share it.
 * What stays read-only (the reason is printed): inline data, bigalloc,
 * meta_bg, xattrs in inodes, an external journal, a journal with the older
 * checksums or fast commits, a non-empty orphan file. Blocks of an extent
 * file cannot be moved off a bad sector: it is reported, not touched.
 * ============================================================================= */
#define E2_MAGIC        0xEF53u
#define E2_SUPER_OFF    1024u
#define E2_COMPAT_JOURNAL  0x0004u
#define E2_COMPAT_SPARSE2  0x0200u
#define E2_INC_FILETYPE    0x0002u
#define E2_INC_RECOVER     0x0004u
#define E2_INC_EXTENTS     0x0040u
#define E2_INC_64BIT       0x0080u
#define E2_INC_MMP         0x0100u
#define E2_INC_FLEX_BG     0x0200u
#define E2_INC_CSUM_SEED   0x2000u
#define E2_INC_LARGEDIR    0x4000u
#define E2_INCOMPAT_NOTI   (E2_INC_FILETYPE | E2_INC_RECOVER | E2_INC_EXTENTS | \
                            E2_INC_64BIT | E2_INC_MMP | E2_INC_FLEX_BG | \
                            E2_INC_CSUM_SEED | E2_INC_LARGEDIR)
#define E2_RO_HUGE_FILE    0x0008u
#define E2_RO_GDT_CSUM     0x0010u
#define E2_RO_DIR_NLINK    0x0020u
#define E2_RO_EXTRA_ISIZE  0x0040u
#define E2_RO_BIGALLOC     0x0200u
#define E2_RO_METADATA     0x0400u
#define E2_RO_ORPHAN       0x10000u
/* every ro_compat that does not change where blocks are */
#define E2_RO_NOTE         (0x0001u | 0x0002u | 0x0004u | E2_RO_HUGE_FILE | \
                            E2_RO_GDT_CSUM | E2_RO_DIR_NLINK | E2_RO_EXTRA_ISIZE | \
                            0x0100u | E2_RO_METADATA | 0x1000u | 0x2000u | 0x8000u | \
                            E2_RO_ORPHAN)
#define E2_FL_EXTENTS      0x00080000u
#define E2_FL_INLINE       0x10000000u
#define E2_BG_INODE_UNINIT 0x0001u
#define E2_BG_BLOCK_UNINIT 0x0002u
#define E2_ESTESI_MAGIA    0xF30Au
#define E2_DOVE_ESTESO     0xFFFFFFFFu  /* e2_dove_cont: this block is in an extent */
#define E2_SPECIALI        5u
#define E2_MODE_TIPO    0xF000u
#define E2_MODE_DIR     0x4000u

static unsigned int e2_dim_blocco, e2_n_inode, e2_n_blocchi;
static unsigned int e2_liberi_b, e2_liberi_i, e2_primo_dato;
static unsigned int e2_b_per_gruppo, e2_i_per_gruppo, e2_dim_inode;
static unsigned int e2_n_gruppi, e2_incompat, e2_primo_inode;
static unsigned int e2_desc_blocco, e2_desc_quanti, e2_sparse;
static unsigned int e2_gdt_riserva;     /* resize_inode: s_reserved_gdt_blocks */
static unsigned int e2_compat, e2_ro, e2_gen, e2_dim_desc;
static unsigned int e2_somme, e2_gdtsomma, e2_seme, e2_sparse2, e2_backup[2];
static unsigned char e2_uuid[16];
static unsigned char *e2_gdt;           /* every descriptor, as on disk */
static unsigned char *e2_meta;          /* the blocks that are metadata */
static unsigned int e2_speciale[E2_SPECIALI];   /* inodes no directory names */
static unsigned int e2_riscrivere;      /* sums to rewrite even if the rest is fine */
static unsigned int e2_somme_ko;        /* inodes whose sum is wrong */

/* ! QUANTE VOLTE NON SI E' POTUTO SAPERE. Ogni lettura fallita, ogni
 * blocco fuori scala, ogni funzionalita' non capita lo incrementa — e con
 * un solo punto di incertezza la RIPARAZIONE DELLE BITMAP non parte.
 * Riscrivere una bitmap significa dichiarare "questi e solo questi blocchi
 * sono occupati": se non si e' visto tutto, e' una dichiarazione falsa che
 * libera dati vivi. */
static unsigned int e2_incerto;

/* Le due mappe che questo controllore costruisce e poi confronta con
 * quelle scritte sul volume. ! Sono la ragione per cui il programma alloca:
 * su un volume da 512 MB con blocchi da 1024 sono 64 KB di bitmap. */
static unsigned char *e2_blocchi_usati;
static unsigned char *e2_inode_visti;
static unsigned short *e2_link_conta;    /* riferimenti trovati, per inode */
/* Quali inode sono directory: serve a bg_used_dirs_count, che il
 * descrittore di gruppo tiene e che nessuno ricalcola mai. */
static unsigned char  *e2_inode_dir;

/* I blocchi che il disco non rende: li riempie la scansione della superficie,
 * li legge chi attribuisce i blocchi ai file e chi ripara. */
static unsigned char  *e2_blocchi_bad;
static unsigned int    e2_bad_n;        /* quanti non si leggono           */
static unsigned int    e2_bad_in_uso;   /* di quelli, quanti stanno in file */

/* Il nome del file di cui si stanno percorrendo i blocchi: serve solo a dire
 * QUALE file sta sopra un blocco andato, che e' la cosa che interessa a chi
 * legge il referto. Un numero di inode non si puo' andare a cercare. */
static const char     *e2_nome_corrente = "";

/* =============================================================================
 * DOVE STA IL PUNTATORE, che e' l'unica cosa che permette di SPOSTARE un blocco
 *
 * ! SAPERE CHE UN FILE E' DANNEGGIATO NON BASTA A RIPARARLO: per mettere i dati
 * altrove bisogna cambiare il puntatore, e il puntatore sta o dentro l'inode
 * (i primi dodici) o dentro un blocco di indiretti. Chi percorre l'albero lo
 * sa mentre ci passa sopra, e dopo non lo sa piu' nessuno — percio' si segna
 * li', non lo si ricostruisce.
 * ============================================================================= */
typedef struct {
    unsigned int ino;          /* di chi e' il file                        */
    unsigned int contenitore;  /* 0 = dentro l'inode, altrimenti il blocco  */
    unsigned int idx;          /* quale puntatore, li' dentro               */
    unsigned int blocco;       /* quello difettoso                          */
} Spostare;

#define SPOSTARE_MAX 4096
static Spostare     e2_da_spostare[SPOSTARE_MAX];
static unsigned int e2_da_spostare_n;

/* Riempiti da e2_percorri_blocchi appena prima di ogni e2_usa_blocco. */
static unsigned int e2_dove_cont, e2_dove_idx;

/* ! NIENTE BUFFER GLOBALE PER I BLOCCHI DI DIRECTORY. La prima stesura ne
 * aveva uno solo, condiviso: scendendo in una sottodirectory il contenuto
 * del padre veniva sovrascritto, e al ritorno il ciclo continuava a
 * leggere il blocco della FIGLIA credendolo quello del padre.
 *
 * Il sintomo era una voce con rec_len 0 su un volume perfettamente sano —
 * cioe' il peggior difetto possibile in un controllore: gridare al lupo su
 * un disco che sta bene insegna a ignorare i suoi messaggi. Ora il buffer
 * e' locale a e2_controlla_dir, uno per livello di ricorsione. */

static int e2_scrivi_blocco(unsigned int b, const unsigned char *src)
{
    unsigned int sett = e2_dim_blocco / SETT;
    if (b == 0 || b >= e2_n_blocchi) return -1;
    return (blkwrite(dev, b * sett, sett, src) < 0) ? -1 : 0;
}

static int e2_leggi_blocco(unsigned int b, unsigned char *dst)
{
    unsigned int sett = e2_dim_blocco / SETT;
    if (b == 0 || b >= e2_n_blocchi) return -1;
    return (blkread(dev, b * sett, sett, dst) < 0) ? -1 : 0;
}

static int e2_bit(const unsigned char *m, unsigned int i)
{
    return (m[i >> 3] >> (i & 7)) & 1;
}

static void e2_segna(unsigned char *m, unsigned int i)
{
    m[i >> 3] |= (unsigned char)(1u << (i & 7));
}

static int e2_riconosce(void)
{
    unsigned char sb[SETT * 2];

    if (blkread(dev, E2_SUPER_OFF / SETT, 2, sb) < 0) return 0;
    if (le16(sb + 56) != E2_MAGIC) return 0;
    if (le32(sb + 24) > 2u) return 0;              /* blocchi oltre 4096 */
    if (le32(sb + 4) == 0) return 0;               /* nessun blocco */
    if (le32(sb + 32) == 0) return 0;              /* blocchi per gruppo */
    return 1;
}

static unsigned char *e2_desc(unsigned int g)
{
    return e2_gdt + g * e2_dim_desc;
}

/* All the descriptors in memory: flex_bg and the wider ones are read through
 * this one table, and -r writes it back whole. */
static int e2_carica_desc(void)
{
    unsigned int i;

    free(e2_gdt);
    e2_gdt = (unsigned char *)calloc(e2_desc_quanti + 1u, e2_dim_blocco);
    if (e2_gdt == NULL) {
        printf("  ! memoria insufficiente per i descrittori di %u gruppi\n",
               e2_n_gruppi);
        return -1;
    }
    for (i = 0; i < e2_desc_quanti; i++)
        if (e2_leggi_blocco(e2_desc_blocco + i, e2_gdt + i * e2_dim_blocco) != 0) {
            printf("  ! i descrittori di gruppo non si leggono\n");
            return -1;
        }
    return 0;
}

static void e2_p16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}

static void e2_p32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v;         p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static unsigned int e2_be32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8)  |  (unsigned int)p[3];
}

static void e2_pbe32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

/* --- the sums, as kernel/moduli/ext4/somme.c ------------------------------ */
static unsigned int e2_crc16(unsigned int crc, const unsigned char *p, unsigned int n)
{
    unsigned int i, k;

    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (k = 0; k < 8u; k++) crc = (crc & 1u) ? (crc >> 1) ^ 0xA001u : crc >> 1;
    }
    return crc & 0xFFFFu;
}

static unsigned int e2_somma_desc(unsigned int g)
{
    const unsigned char *d = e2_desc(g);
    unsigned char gl[4];
    unsigned int  c;

    e2_p32(gl, g);
    if (e2_somme) {
        c = crc32c(crc32c(e2_seme, gl, 4), d, 0x1Eu);
        c = crc32c(c, "\0\0", 2);
        if (e2_dim_desc > 0x20u) c = crc32c(c, d + 0x20, e2_dim_desc - 0x20u);
        return c & 0xFFFFu;
    }
    c = e2_crc16(0xFFFFu, e2_uuid, 16);
    c = e2_crc16(c, gl, 4);
    c = e2_crc16(c, d, 0x1Eu);
    if (e2_dim_desc > 0x20u) c = e2_crc16(c, d + 0x20, e2_dim_desc - 0x20u);
    return c;
}

/* The sum of a whole inode, seeded by its number and i_generation. */
static unsigned int e2_somma_inode(unsigned char *ino, unsigned int num)
{
    unsigned char b[4];
    unsigned int  c;
    int alto = e2_dim_inode > 128u && le16(ino + 0x80) >= 4u;

    e2_p32(b, num);
    c = crc32c(crc32c(e2_seme, b, 4), ino + 100, 4);
    e2_p16(ino + 0x7C, 0);
    if (alto) e2_p16(ino + 0x82, 0);
    c = crc32c(c, ino, e2_dim_inode);
    e2_p16(ino + 0x7C, c & 0xFFFFu);
    if (alto) e2_p16(ino + 0x82, c >> 16);
    return c;
}

static int e2_giornale(void);

static int e2_apri(void)
{
    unsigned char sb[SETT * 2];
    unsigned int  rev;

    if (blkread(dev, E2_SUPER_OFF / SETT, 2, sb) < 0) {
        printf("  ! superblocco illeggibile\n");
        return -1;
    }

    e2_dim_blocco   = 1024u << le32(sb + 24);
    e2_n_inode      = le32(sb + 0);
    e2_n_blocchi    = le32(sb + 4);
    e2_liberi_b     = le32(sb + 12);
    e2_liberi_i     = le32(sb + 16);
    e2_primo_dato   = le32(sb + 20);
    e2_b_per_gruppo = le32(sb + 32);
    e2_i_per_gruppo = le32(sb + 40);

    rev         = le32(sb + 76);
    e2_incompat = (rev >= 1u) ? le32(sb + 96) : 0u;
    e2_dim_inode  = (rev >= 1u) ? le16(sb + 88) : 128u;
    e2_primo_inode = (rev >= 1u) ? le32(sb + 84) : 11u;
    e2_compat   = (rev >= 1u) ? le32(sb + 92) : 0u;
    e2_ro       = (rev >= 1u) ? le32(sb + 100) : 0u;

    /* Which ext: the features say, the same rule as kernel/block/vol.c. */
    e2_gen = 2u;
    if (e2_compat & E2_COMPAT_JOURNAL) e2_gen = 3u;
    if ((e2_incompat & (E2_INC_EXTENTS | E2_INC_64BIT | E2_INC_FLEX_BG)) ||
        (e2_ro & (E2_RO_HUGE_FILE | E2_RO_GDT_CSUM | E2_RO_DIR_NLINK |
                  E2_RO_EXTRA_ISIZE | E2_RO_METADATA)))
        e2_gen = 4u;

    if ((e2_incompat & E2_INC_64BIT) && le32(sb + 0x150) != 0) {
        printf("  ! piu' di 2^32 blocchi: questo controllore non ci arriva\n");
        return -1;
    }

    e2_dim_desc = 32u;
    if ((e2_incompat & E2_INC_64BIT) && le16(sb + 254) >= 64u)
        e2_dim_desc = le16(sb + 254);
    if (e2_dim_desc > e2_dim_blocco || (e2_dim_desc & (e2_dim_desc - 1u))) {
        printf("  ! descrittori di gruppo da %u byte: superblocco rotto\n",
               e2_dim_desc);
        return -1;
    }

    for (rev = 0; rev < 16u; rev++) e2_uuid[rev] = sb[104 + rev];
    e2_somme     = (e2_ro & E2_RO_METADATA) != 0;
    e2_gdtsomma  = !e2_somme && (e2_ro & E2_RO_GDT_CSUM);
    e2_seme      = (e2_incompat & E2_INC_CSUM_SEED) ? le32(sb + 0x270)
                                                    : crc32c(~0u, e2_uuid, 16);
    e2_sparse2   = (e2_compat & E2_COMPAT_SPARSE2) != 0;
    e2_backup[0] = le32(sb + 0x24C);
    e2_backup[1] = le32(sb + 0x250);

    /* The inodes no directory names: their blocks are in use all the same. */
    e2_speciale[0] = (e2_compat & E2_COMPAT_JOURNAL) ? le32(sb + 224) : 0u;
    e2_speciale[1] = le32(sb + 0x240);          /* quota: user */
    e2_speciale[2] = le32(sb + 0x244);          /* quota: group */
    e2_speciale[3] = le32(sb + 0x26C);          /* quota: project */
    e2_speciale[4] = (e2_compat & 0x1000u) ? le32(sb + 0x280) : 0u;   /* orphan file */

    if (e2_somme && le32(sb + 0x3FC) != crc32c(~0u, sb, 0x3FCu)) {
        printf("  ! la somma del superblocco e' sbagliata\n");
        problemi++;
        e2_riscrivere++;
    }

    if (e2_b_per_gruppo == 0 || e2_i_per_gruppo == 0) {
        printf("  ! blocchi o inode per gruppo a zero: superblocco rotto\n");
        return -1;
    }
    /* ! A BITMAP IS ONE BLOCK, so a group cannot pass 8 bits per byte of it.
     * With bigalloc it counts clusters, and a broken superblock can say
     * anything: past this every loop on a group runs off its buffer. */
    if (e2_b_per_gruppo > e2_dim_blocco * 8u || e2_i_per_gruppo > e2_dim_blocco * 8u) {
        printf("  ! %u blocchi e %u inode per gruppo non stanno in una bitmap:\n",
               e2_b_per_gruppo, e2_i_per_gruppo);
        printf("    bigalloc, o un superblocco rotto. Non so controllarlo.\n");
        return -1;
    }
    if (e2_dim_inode < 128u || e2_dim_inode > e2_dim_blocco) {
        printf("  ! dimensione dell'inode = %u: fuori da ogni uso reale\n",
               e2_dim_inode);
        return -1;
    }

    e2_n_gruppi = (e2_n_blocchi - e2_primo_dato + e2_b_per_gruppo - 1u)
                  / e2_b_per_gruppo;

    /* ! I descrittori stanno nel blocco SUBITO DOPO il superblocco, e
     * "subito dopo" dipende dalla dimensione del blocco: con blocchi da
     * 1024 il superblocco e' il blocco 1 e i descrittori il 2; con blocchi
     * piu' grandi il superblocco sta DENTRO il blocco 0 e i descrittori
     * sono il blocco 1. Sbagliare qui fa leggere dati come descrittori. */
    e2_desc_blocco = (e2_dim_blocco == 1024u) ? 2u : 1u;
    e2_desc_quanti = (e2_n_gruppi * e2_dim_desc + e2_dim_blocco - 1u) / e2_dim_blocco;

    /* sparse_super: se c'e', le copie di riserva stanno solo in alcuni
     * gruppi invece che in tutti. Vedi gruppo_ha_backup(). */
    e2_sparse = e2_ro & 0x0001u;

    /* resize_inode (compat 0x0010): dopo i descrittori, in ogni gruppo che
     * ne ha la copia, ci sono blocchi tenuti da parte per farli crescere.
     * Vedi �I BLOCCHI PER CRESCERE� dove si segnano i metadati. */
    e2_gdt_riserva = (e2_compat & 0x0010u) ? le16(sb + 206) : 0u;

    printf("  ext%u - %u blocchi da %u byte, %u inode, %u grupp%s\n",
           e2_gen, e2_n_blocchi, e2_dim_blocco, e2_n_inode, e2_n_gruppi,
           (e2_n_gruppi == 1) ? "o" : "i");

    if (e2_incompat & ~(unsigned int)E2_INCOMPAT_NOTI) {
        printf("  ! funzionalita' incompatibili 0x%x che non so leggere\n",
               e2_incompat & ~(unsigned int)E2_INCOMPAT_NOTI);
        printf("    Con quei bit attivi i campi che credo di saper leggere\n");
        printf("    vogliono dire altro: riferisco, ma NON tocco niente\n");
        printf("    nemmeno con -r.\n");
        problemi++;
        e2_incerto++;
        ripara = 0;          /* ! si disarma la riparazione, di proposito */
        return (e2_carica_desc() == 0) ? 0 : -1;
    }

    if (e2_ro & ~(unsigned int)E2_RO_NOTE) {
        printf("  ! funzionalita' 0x%x che cambiano dove stanno i blocchi\n",
               e2_ro & ~(unsigned int)E2_RO_NOTE);
        printf("    (bigalloc, per esempio): riferisco, ma NON tocco niente.\n");
        problemi++;
        e2_incerto++;
        ripara = 0;
    }

    if (e2_ro & E2_RO_ORPHAN) {
        printf("  ! il file degli orfani non e' vuoto: lo sa svuotare solo chi\n");
        printf("    monta il volume con Linux. Riferisco, ma NON tocco niente.\n");
        problemi++;
        e2_incerto++;
        ripara = 0;
    }

    if (e2_carica_desc() != 0) return -1;

    /* ! THE JOURNAL COMES FIRST. With needs_recovery set the bitmaps, the
     * inodes, even the descriptors on disk may be older than the volume: all
     * the rest of this check must look at the volume after the replay. */
    if (e2_incompat & E2_INC_RECOVER) {
        if (!ripara) {
            printf("  ! il giornale ha delle modifiche da applicare: quello che\n");
            printf("    segue guarda il disco com'e', e con -r puo' cambiare.\n");
            problemi++;
            e2_incerto++;
        } else if (e2_giornale() != 0) {
            printf("  ! il giornale non si riesce ad applicare: non tocco niente.\n");
            problemi++;
            e2_incerto++;
            ripara = 0;
        } else {
            if (e2_carica_desc() != 0) return -1;
            if (blkread(dev, E2_SUPER_OFF / SETT, 2, sb) < 0) return -1;
            e2_incompat   = le32(sb + 96);
            e2_liberi_b   = le32(sb + 12);
            e2_liberi_i   = le32(sb + 16);
        }
    }

    return 0;
}

/* =============================================================================
 * ! LE COPIE DI RISERVA DEL SUPERBLOCCO NON SONO DI NESSUN FILE
 *
 * ext2 tiene un superblocco e una tabella di descrittori di riserva
 * all'inizio di alcuni gruppi. Quei blocchi risultano OCCUPATI nella
 * bitmap ma non appartengono a nessun inode: un controllore che non li
 * conosce li conta come differenza fra "cio' che i file usano" e "cio' che
 * la bitmap dice", su OGNI volume sano.
 *
 * E' successo alla prima prova: 26 blocchi di differenza su un disco
 * appena formattato — 2 dei descrittori primari (che pure avevo
 * dimenticato) piu' 24 delle otto copie di riserva.
 *
 * ! CON `sparse_super` LE COPIE STANNO SOLO nei gruppi 0 e 1 e in quelli
 * il cui numero e' una potenza di 3, 5 o 7. Senza, in tutti. Indovinare
 * la regola sbagliata non da' un errore: da' esattamente lo stesso falso
 * allarme di prima, su meno blocchi.
 * ============================================================================= */
static int potenza_di(unsigned int g, unsigned int base)
{
    unsigned int p = base;

    while (p < g) p *= base;
    return p == g;
}

static int gruppo_ha_backup(unsigned int g)
{
    if (e2_sparse2) return g == 0 || g == e2_backup[0] || g == e2_backup[1];
    if (!e2_sparse) return 1;
    if (g == 0 || g == 1) return 1;
    return potenza_di(g, 3u) || potenza_di(g, 5u) || potenza_di(g, 7u);
}

/* The last inode read, whole: e2_leggi_inode hands out the first 128 bytes,
 * the sum covers all of it. */
static unsigned char e2_ino_pieno[4096];

/* Legge l'inode `num` (1-based) nel buffer dato. */
static int e2_leggi_inode(unsigned int num, unsigned char *dst)
{
    unsigned int g, idx, tab, off, blocco;
    unsigned char d[4096];

    if (num == 0 || num > e2_n_inode) return -1;

    g   = (num - 1u) / e2_i_per_gruppo;
    idx = (num - 1u) % e2_i_per_gruppo;
    tab = le32(e2_desc(g) + 8);

    off    = idx * e2_dim_inode;
    blocco = tab + off / e2_dim_blocco;

    if (e2_leggi_blocco(blocco, d) != 0) return -1;
    {
        unsigned int k, base = off % e2_dim_blocco;
        for (k = 0; k < 128u; k++) dst[k] = d[base + k];
        for (k = 0; k < e2_dim_inode; k++) e2_ino_pieno[k] = d[base + k];
    }
    return 0;
}

/* Marca un blocco come usato, segnalando se lo era gia' o se e' fuori
 * dal volume. */
/* Quanti blocchi sono stati attribuiti a QUALCUNO: serve a dire quanti ne
 * dichiara l'inode 1, senza contare due volte quelli gia' visti. */
static unsigned int e2_blocchi_contati;

static void e2_usa_blocco(unsigned int b, unsigned int ino);

/* The block of extended attributes: several inodes may share one, so meeting
 * it again is no double use. */
static void e2_usa_attributi(unsigned int b, unsigned int ino)
{
    if (b == 0) return;
    if (b < e2_n_blocchi && e2_bit(e2_blocchi_usati, b)) return;
    e2_dove_cont = E2_DOVE_ESTESO;
    e2_usa_blocco(b, ino);
}

static void e2_usa_blocco(unsigned int b, unsigned int ino)
{
    if (b == 0) return;

    if (b >= e2_n_blocchi) {
        printf("  ! inode %u: blocco %u fuori dal volume\n", ino, b);
        problemi++;
        e2_incerto++;      /* non si sa quali blocchi volesse davvero */
        return;
    }
    if (e2_bit(e2_blocchi_usati, b)) {
        printf("  ! blocco %u usato da piu' di un inode (l'ultimo e' il %u)\n",
               b, ino);
        printf("    (non si ripara da solo: quale inode abbia diritto ai\n");
        printf("     dati non e' una cosa che si possa dedurre)\n");
        problemi++;
        e2_incerto++;      /* la mappa non e' piu' una descrizione fedele */
        return;
    }
    e2_segna(e2_blocchi_usati, b);
    e2_blocchi_contati++;

    /* ! E QUI SI SCOPRE CHI E' DANNEGGIATO. La mappa dei blocchi andati e'
     * gia' pronta quando si percorre l'albero — la scansione viene prima
     * apposta — quindi il file lo si puo' NOMINARE mentre lo si incontra,
     * senza un secondo giro e senza tenere in memoria chi possiede cosa: su
     * un volume da 57 GB una mappa blocco->inode sarebbe un quarto di giga. */
    if (e2_blocchi_bad && e2_bit(e2_blocchi_bad, b)) {
        e2_bad_in_uso++;
        if (e2_bad_in_uso <= 20u)
            printf("  ! %s (inode %u): il blocco %u non si legge\n",
                   e2_nome_corrente[0] ? e2_nome_corrente : "(senza nome)",
                   ino, b);
        else if (e2_bad_in_uso == 21u)
            printf("  ! (gli altri file danneggiati non li elenco)\n");
        problemi++;

        /* In an extent the block cannot be swapped by changing one pointer:
         * the extent would have to be cut. It stays in the file, and out of
         * the bad list too, or it would have two owners. */
        if (e2_dove_cont == E2_DOVE_ESTESO) {
            e2_blocchi_bad[b >> 3] &= (unsigned char)~(1u << (b & 7));
            printf("    (sta in un extent: non lo sposto, va copiato altrove)\n");
        } else if (e2_da_spostare_n < SPOSTARE_MAX) {
            e2_da_spostare[e2_da_spostare_n].ino         = ino;
            e2_da_spostare[e2_da_spostare_n].contenitore = e2_dove_cont;
            e2_da_spostare[e2_da_spostare_n].idx         = e2_dove_idx;
            e2_da_spostare[e2_da_spostare_n].blocco      = b;
            e2_da_spostare_n++;
        }
    }
}

/* =============================================================================
 * I blocchi di un inode: diretti, indiretto, doppio, TRIPLO
 *
 * ! I TRE BUFFER SONO GLOBALI, ED E' SICURO QUI E SOLO QUI. Il buffer dei
 * blocchi di directory e' locale apposta, perche' e2_controlla_dir ricorre
 * e ogni livello ha bisogno del suo (vedi il commento la'). Questa
 * funzione invece NON ricorre e non e' mai attiva durante la ricorsione:
 * e2_voce la chiama e ASPETTA che finisca, poi eventualmente scende. Tre
 * buffer da 4 KB sullo stack sarebbero 12 KB per livello sprecati.
 *
 * ! IL TRIPLO INDIRETTO SI PERCORRE. Con blocchi da 1024 comincia oltre i
 * 16 GB, cioe' oltre qualunque volume che questo sistema monti oggi — ma
 * finche' non lo si percorreva, la RIPARAZIONE DELLE BITMAP non era
 * sicura: riscriverle senza aver visto tutti i blocchi di tutti gli inode
 * significa marcare come liberi blocchi vivi. Non e' un caso di scuola:
 * e' la differenza fra un controllore che ripara e uno che dice di non
 * poterlo fare.
 * ============================================================================= */
static unsigned char e2_ind1[4096], e2_ind2[4096], e2_ind3[4096];

static void e2_percorri_puntatori(const unsigned char *ino_buf, unsigned int ino)
{
    unsigned int i, j, k, punt = e2_dim_blocco / 4u;

    for (i = 0; i < 12u; i++) {
        e2_dove_cont = 0;                    /* dentro l'inode */
        e2_dove_idx  = i;
        e2_usa_blocco(le32(ino_buf + 40 + i * 4), ino);
    }

    /* --- indiretto semplice --- */
    {
        unsigned int b = le32(ino_buf + 40 + 12 * 4);
        if (b != 0) {
            e2_dove_cont = 0; e2_dove_idx = 12u;
            e2_usa_blocco(b, ino);
            if (e2_leggi_blocco(b, e2_ind1) == 0)
                for (i = 0; i < punt; i++) {
                    e2_dove_cont = b;        /* il puntatore sta qui dentro */
                    e2_dove_idx  = i;
                    e2_usa_blocco(le32(e2_ind1 + i * 4), ino);
                }
            else e2_incerto++;
        }
    }

    /* --- doppio --- */
    {
        unsigned int b = le32(ino_buf + 40 + 13 * 4);
        if (b != 0) {
            e2_usa_blocco(b, ino);
            if (e2_leggi_blocco(b, e2_ind1) == 0) {
                for (i = 0; i < punt; i++) {
                    unsigned int b2 = le32(e2_ind1 + i * 4);
                    if (b2 == 0) continue;
                    e2_dove_cont = b; e2_dove_idx = i;
                    e2_usa_blocco(b2, ino);
                    if (e2_leggi_blocco(b2, e2_ind2) == 0)
                        for (j = 0; j < punt; j++) {
                            e2_dove_cont = b2;
                            e2_dove_idx  = j;
                            e2_usa_blocco(le32(e2_ind2 + j * 4), ino);
                        }
                    else e2_incerto++;
                }
            } else e2_incerto++;
        }
    }

    /* --- triplo ---
     *
     * ! e2_ind1 SI RILEGGE A OGNI GIRO DEL CICLO ESTERNO, e non e' uno
     * spreco: il secondo livello lo sovrascrive. Tenere il primo livello
     * in un buffer e riusarlo senza rileggerlo e' l'errore che fa
     * percorrere lo stesso ramo tre volte e saltarne altri due. */
    {
        unsigned int b = le32(ino_buf + 40 + 14 * 4);
        if (b != 0) {
            e2_usa_blocco(b, ino);
            if (e2_leggi_blocco(b, e2_ind1) != 0) { e2_incerto++; return; }

            for (i = 0; i < punt; i++) {
                unsigned int b2 = le32(e2_ind1 + i * 4);
                if (b2 == 0) continue;
                e2_usa_blocco(b2, ino);
                if (e2_leggi_blocco(b2, e2_ind2) != 0) { e2_incerto++; continue; }

                for (j = 0; j < punt; j++) {
                    unsigned int b3 = le32(e2_ind2 + j * 4);
                    if (b3 == 0) continue;
                    e2_usa_blocco(b3, ino);
                    if (e2_leggi_blocco(b3, e2_ind3) != 0) { e2_incerto++; continue; }

                    for (k = 0; k < punt; k++)
                        e2_usa_blocco(le32(e2_ind3 + k * 4), ino);
                }

                /* Il primo livello e' stato sovrascritto da e2_ind2 solo se
                 * i due buffer coincidessero: sono distinti, quindi
                 * e2_ind1 e' ancora valido e il ciclo puo' proseguire. */
            }
        }
    }
}

/* =============================================================================
 * EXTENTS
 *
 * i_block is no longer pointers but the root of a small tree: a 12-byte
 * header (magic, entries, max, depth) and entries of 12 bytes. At depth 0
 * they are runs (first logical block, length, first physical block), above
 * they point to the next level. The length of a run allocated but not yet
 * written has 32768 added.
 *
 * ! ONE BUFFER PER LEVEL: the walk comes back to a level after its child. A
 * tree is at most 5 deep, and a deeper one is refused.
 * ============================================================================= */
#define E2_LIVELLI 6u
static unsigned char e2_livello[E2_LIVELLI][4096];

/* Header sound? Entries within the space it lives in. */
static int e2_testa_ok(const unsigned char *p, unsigned int spazio)
{
    return le16(p) == E2_ESTESI_MAGIA && le16(p + 2) <= le16(p + 4) &&
           12u + 12u * (unsigned int)le16(p + 2) <= spazio;
}

static void e2_percorri_estesi(const unsigned char *p, unsigned int spazio,
                               unsigned int ino, unsigned int liv)
{
    unsigned int i, n, prof;

    if (liv >= E2_LIVELLI || !e2_testa_ok(p, spazio)) {
        printf("  ! inode %u: albero di extent rotto\n", ino);
        problemi++;
        e2_incerto++;
        return;
    }
    n    = le16(p + 2);
    prof = le16(p + 6);

    for (i = 0; i < n; i++) {
        const unsigned char *e = p + 12u + i * 12u;

        e2_dove_cont = E2_DOVE_ESTESO;
        if (prof == 0) {
            unsigned int lung = le16(e + 4), primo = le32(e + 8), k;

            if (lung > 32768u) lung -= 32768u;
            if (le16(e + 6) != 0) {
                printf("  ! inode %u: extent oltre 2^32 blocchi\n", ino);
                problemi++;
                e2_incerto++;
                continue;
            }
            for (k = 0; k < lung; k++) {
                e2_dove_cont = E2_DOVE_ESTESO;
                e2_usa_blocco(primo + k, ino);
            }
        } else {
            unsigned int b = le32(e + 4);

            e2_usa_blocco(b, ino);
            if (le16(e + 8) != 0 || e2_leggi_blocco(b, e2_livello[liv]) != 0) {
                printf("  ! inode %u: blocco di extent %u illeggibile\n", ino, b);
                problemi++;
                e2_incerto++;
                continue;
            }
            e2_percorri_estesi(e2_livello[liv], e2_dim_blocco, ino, liv + 1u);
        }
    }
}

/* Every block of an inode. Only regular files, directories and long symbolic
 * links have blocks: a device keeps its numbers in i_block, a short link its
 * text, and reading those as pointers makes up blocks that do not exist. */
static void e2_percorri_blocchi(const unsigned char *ib, unsigned int ino)
{
    unsigned int tipo  = le16(ib) & E2_MODE_TIPO;
    unsigned int flags = le32(ib + 32);

    e2_usa_attributi(le32(ib + 104), ino);

    if (tipo == 0x1000u || tipo == 0x2000u || tipo == 0x6000u || tipo == 0xC000u)
        return;
    if (flags & E2_FL_INLINE) return;
    if (flags & E2_FL_EXTENTS) {
        e2_percorri_estesi(ib + 40, 60u, ino, 0u);
        return;
    }
    if (tipo == 0xA000u && le32(ib + 4) < 60u) return;
    e2_percorri_puntatori(ib, ino);
}

/* Logical block `n` of an inode: its physical block, or 0 for a hole. Used
 * for directories and the journal. Failing is counted: whoever asks cannot
 * then claim to have seen everything. */
static unsigned int e2_mappa(const unsigned char *ib, unsigned int n)
{
    static unsigned char m[4096];
    unsigned int punt = e2_dim_blocco / 4u;

    if (le32(ib + 32) & E2_FL_EXTENTS) {
        const unsigned char *p = ib + 40;
        unsigned int spazio = 60u, liv;

        for (liv = 0; liv < E2_LIVELLI; liv++) {
            unsigned int i, q;

            if (!e2_testa_ok(p, spazio)) break;
            q = le16(p + 2);

            if (le16(p + 6) == 0) {
                for (i = 0; i < q; i++) {
                    const unsigned char *e = p + 12u + i * 12u;
                    unsigned int lb = le32(e), lung = le16(e + 4), scritto = 1u;

                    if (lung > 32768u) { lung -= 32768u; scritto = 0; }
                    if (n >= lb && n - lb < lung)
                        return scritto ? le32(e + 8) + (n - lb) : 0u;
                }
                return 0;
            }

            /* the last index entry that starts at or before n */
            {
                const unsigned char *scelto = NULL;

                for (i = 0; i < q; i++) {
                    const unsigned char *x = p + 12u + i * 12u;
                    if (le32(x) <= n) scelto = x;
                }
                if (scelto == NULL) return 0;
                if (e2_leggi_blocco(le32(scelto + 4), m) != 0) break;
                p = m;
                spazio = e2_dim_blocco;
            }
        }
        e2_incerto++;
        return 0;
    }

    if (n < 12u) return le32(ib + 40 + n * 4u);
    n -= 12u;

    if (n < punt) {
        unsigned int b = le32(ib + 40 + 12u * 4u);
        if (b == 0) return 0;
        if (e2_leggi_blocco(b, m) != 0) { e2_incerto++; return 0; }
        return le32(m + n * 4u);
    }
    n -= punt;

    if (n < punt * punt) {
        unsigned int b = le32(ib + 40 + 13u * 4u);
        if (b == 0) return 0;
        if (e2_leggi_blocco(b, m) != 0) { e2_incerto++; return 0; }
        b = le32(m + (n / punt) * 4u);
        if (b == 0) return 0;
        if (e2_leggi_blocco(b, m) != 0) { e2_incerto++; return 0; }
        return le32(m + (n % punt) * 4u);
    }
    n -= punt * punt;

    {
        unsigned int b = le32(ib + 40 + 14u * 4u);
        if (b == 0) return 0;
        if (e2_leggi_blocco(b, m) != 0) { e2_incerto++; return 0; }
        b = le32(m + (n / (punt * punt)) * 4u);
        if (b == 0) return 0;
        if (e2_leggi_blocco(b, m) != 0) { e2_incerto++; return 0; }
        b = le32(m + ((n / punt) % punt) * 4u);
        if (b == 0) return 0;
        if (e2_leggi_blocco(b, m) != 0) { e2_incerto++; return 0; }
        return le32(m + (n % punt) * 4u);
    }
}

/* =============================================================================
 * THE JOURNAL: jbd2's three passes, as kernel/moduli/ext4/giornale.c
 *
 *   SCAN    from s_start, as long as the sequence is the expected one: the
 *           last committed transaction + 1 is where it ends;
 *   REVOKE  a block revoked by transaction R is not replayed from any
 *           transaction up to R: it was freed, and may hold other data now;
 *   REPLAY  every block of every committed transaction, in order.
 * Then the journal is empty and needs_recovery goes off. CSUM_V3 is verified;
 * the older checksums and fast commits are refused, as in the module.
 * ============================================================================= */
#define GIO_MAGIA        0xC03B3998u
#define GIO_DESCRITTORE  1u
#define GIO_COMMIT       2u
#define GIO_REVOCA       5u
#define GIO_INC_REVOCA   0x01u
#define GIO_INC_64BIT    0x02u
#define GIO_INC_CSUM_V3  0x10u
#define GIO_TAG_ESCAPE   0x01u
#define GIO_TAG_UUID     0x02u
#define GIO_TAG_ULTIMO   0x08u

typedef struct { unsigned int blk, seq; } E2Revoca;

static unsigned char e2_gio_b[4096], e2_gio_d[4096], e2_gio_ib[128];
static unsigned int  e2_gio_max, e2_gio_primo, e2_gio_inc, e2_gio_tag, e2_gio_somme;
static unsigned int  e2_gio_seme, e2_rev_conta, e2_rev_n, e2_gio_copie;
static E2Revoca     *e2_rev;

static int e2_gio_leggi(unsigned int log, unsigned char *dst)
{
    unsigned int f = e2_mappa(e2_gio_ib, log);

    return (f == 0) ? -1 : e2_leggi_blocco(f, dst);
}

static unsigned int e2_gio_avanti(unsigned int log)
{
    return (++log >= e2_gio_max) ? e2_gio_primo : log;
}

static unsigned int e2_gio_somma(const unsigned char *b, unsigned int buco)
{
    unsigned int c = crc32c(e2_gio_seme, b, buco);

    c = crc32c(c, "\0\0\0\0", 4);
    return crc32c(c, b + buco + 4u, e2_dim_blocco - buco - 4u);
}

static unsigned int e2_gio_somma_commit(const unsigned char *b)
{
    unsigned int c = crc32c(e2_gio_seme, b, 0x0Cu);

    c = crc32c(c, "\0\0", 2);
    c = crc32c(c, b + 0x0E, 2);
    c = crc32c(c, "\0\0\0\0", 4);
    return crc32c(c, b + 0x14, e2_dim_blocco - 0x14u);
}

static int e2_gio_revocato(unsigned int blk, unsigned int seq)
{
    unsigned int i;

    for (i = 0; i < e2_rev_n; i++)
        if (e2_rev[i].blk == blk) return (int)(seq - e2_rev[i].seq) <= 0;
    return 0;
}

static void e2_gio_revoca(unsigned int blk, unsigned int seq)
{
    unsigned int i;

    for (i = 0; i < e2_rev_n; i++)
        if (e2_rev[i].blk == blk) {
            if ((int)(seq - e2_rev[i].seq) > 0) e2_rev[i].seq = seq;
            return;
        }
    if (e2_rev_n < e2_rev_conta) {
        e2_rev[e2_rev_n].blk = blk;
        e2_rev[e2_rev_n].seq = seq;
        e2_rev_n++;
    }
}

/* 0 SCAN, 1 REVOKE, 2 REPLAY. `fine` is written by SCAN, read by the others. */
static int e2_gio_passo(int quale, unsigned int inizio, unsigned int seq0,
                        unsigned int *fine)
{
    unsigned int pos = inizio, seq = seq0, giri = 0;
    unsigned int dim_rev = (e2_gio_inc & GIO_INC_64BIT) ? 8u : 4u;

    for (;;) {
        unsigned int tipo;

        if (quale != 0 && seq == *fine) break;
        if (++giri > e2_gio_max) break;
        if (e2_gio_leggi(pos, e2_gio_b) != 0) return -1;
        if (e2_be32(e2_gio_b) != GIO_MAGIA || e2_be32(e2_gio_b + 8) != seq) break;
        tipo = e2_be32(e2_gio_b + 4);

        if (tipo == GIO_DESCRITTORE) {
            unsigned int off = 12u, qui = e2_gio_avanti(pos);
            unsigned int lim = e2_dim_blocco - (e2_gio_somme ? 4u : 0u);

            if (e2_gio_somme && e2_be32(e2_gio_b + e2_dim_blocco - 4u) !=
                                e2_gio_somma(e2_gio_b, e2_dim_blocco - 4u))
                break;

            while (off + e2_gio_tag <= lim) {
                unsigned int blk   = e2_be32(e2_gio_b + off);
                unsigned int flags = ((unsigned int)e2_gio_b[off + 6] << 8) |
                                     e2_gio_b[off + 7];

                if ((e2_gio_inc & GIO_INC_64BIT) && e2_be32(e2_gio_b + off + 8) != 0)
                    return -1;
                if (quale == 2 && !e2_gio_revocato(blk, seq)) {
                    unsigned int sett = e2_dim_blocco / SETT;

                    if (e2_gio_leggi(qui, e2_gio_d) != 0) return -1;
                    if (e2_gio_somme) {
                        unsigned char s[4];
                        unsigned int  c;

                        e2_pbe32(s, seq);
                        c = crc32c(crc32c(e2_gio_seme, s, 4), e2_gio_d, e2_dim_blocco);
                        if (e2_be32(e2_gio_b + off + 12) != c) return -1;
                    }
                    if (flags & GIO_TAG_ESCAPE) e2_pbe32(e2_gio_d, GIO_MAGIA);

                    /* ! BLOCK 0 IS LEGAL ABOVE 1 KB: it holds the superblock
                     * and Linux journals it. Hence no e2_scrivi_blocco. */
                    if (blk >= e2_n_blocchi || (blk == 0 && e2_dim_blocco == 1024u))
                        return -1;
                    if (blkwrite(dev, blk * sett, sett, e2_gio_d) != (int)sett)
                        return -1;
                    e2_gio_copie++;
                }
                qui = e2_gio_avanti(qui);
                off += e2_gio_tag;
                if (!(flags & GIO_TAG_UUID)) off += 16u;
                if (flags & GIO_TAG_ULTIMO) break;
            }
            pos = qui;
            continue;
        }
        if (tipo == GIO_COMMIT) {
            if (e2_gio_somme && e2_be32(e2_gio_b + 0x10) != e2_gio_somma_commit(e2_gio_b))
                break;
            seq++;
            pos = e2_gio_avanti(pos);
            continue;
        }
        if (tipo == GIO_REVOCA) {
            unsigned int usati = e2_be32(e2_gio_b + 12), off = 16u;

            if (usati > e2_dim_blocco - (e2_gio_somme ? 4u : 0u))
                usati = e2_dim_blocco - (e2_gio_somme ? 4u : 0u);
            if (quale == 0 && usati > off) e2_rev_conta += (usati - off) / dim_rev;
            if (quale == 1)
                for (; off + dim_rev <= usati; off += dim_rev)
                    e2_gio_revoca(e2_be32(e2_gio_b + off + dim_rev - 4u), seq);
            pos = e2_gio_avanti(pos);
            continue;
        }
        break;
    }
    if (quale == 0) *fine = seq;
    return 0;
}

static int e2_giornale(void)
{
    unsigned char sb[SETT * 2];
    unsigned int  ino = e2_speciale[0], tipo, inizio, seq, fine, compat_gio;

    printf("\nGiornale\n");

    if (ino == 0 || e2_leggi_inode(ino, e2_gio_ib) != 0 ||
        e2_gio_leggi(0u, e2_gio_b) != 0)
        return -1;

    tipo = e2_be32(e2_gio_b + 4);
    if (e2_be32(e2_gio_b) != GIO_MAGIA || (tipo != 3u && tipo != 4u)) return -1;

    e2_gio_max   = e2_be32(e2_gio_b + 16);
    e2_gio_primo = e2_be32(e2_gio_b + 20);
    seq          = e2_be32(e2_gio_b + 24);
    inizio       = e2_be32(e2_gio_b + 28);
    compat_gio   = (tipo == 4u) ? e2_be32(e2_gio_b + 36) : 0u;
    e2_gio_inc   = (tipo == 4u) ? e2_be32(e2_gio_b + 40) : 0u;

    if (compat_gio & 0x01u) return -1;                 /* the v1 CHECKSUM */
    if (e2_gio_inc & ~(unsigned int)(GIO_INC_REVOCA | GIO_INC_64BIT | GIO_INC_CSUM_V3)) {
        printf("  ! formato del giornale che non conosco (0x%x)\n", e2_gio_inc);
        return -1;
    }
    if (e2_gio_primo == 0 || e2_gio_max == 0) return -1;

    e2_gio_somme = (e2_gio_inc & GIO_INC_CSUM_V3) != 0;
    e2_gio_tag   = e2_gio_somme ? 16u : (e2_gio_inc & GIO_INC_64BIT) ? 12u : 8u;
    e2_gio_seme  = crc32c(~0u, e2_gio_b + 48, 16);

    if (inizio != 0) {
        e2_rev_conta = 0;
        fine = seq;
        if (e2_gio_passo(0, inizio, seq, &fine) != 0) return -1;

        if (fine != seq) {
            e2_rev_n = 0;
            e2_rev = (E2Revoca *)calloc(e2_rev_conta + 1u, sizeof(E2Revoca));
            if (e2_rev == NULL) return -1;
            if (e2_gio_passo(1, inizio, seq, &fine) != 0) return -1;
            e2_gio_copie = 0;
            if (e2_gio_passo(2, inizio, seq, &fine) != 0) return -1;
            free(e2_rev);
            e2_rev = NULL;
            seq = fine;
        }
    }

    /* the journal is empty again... */
    if (e2_gio_leggi(0u, e2_gio_b) != 0) return -1;
    e2_pbe32(e2_gio_b + 24, seq);
    e2_pbe32(e2_gio_b + 28, 0u);
    if (e2_gio_somme) {
        unsigned int c = crc32c(~0u, e2_gio_b, 0xFCu);

        c = crc32c(c, "\0\0\0\0", 4);
        e2_pbe32(e2_gio_b + 0xFC, crc32c(c, e2_gio_b + 0x100, 1024u - 0x100u));
    }
    if (e2_scrivi_blocco(e2_mappa(e2_gio_ib, 0u), e2_gio_b) != 0) return -1;

    /* ...and only then the volume says so. The superblock is read again: the
     * replay may have just written it. */
    if (blkread(dev, E2_SUPER_OFF / SETT, 2, sb) < 0) return -1;
    e2_p32(sb + 96, le32(sb + 96) & ~(unsigned int)E2_INC_RECOVER);
    if (e2_somme) e2_p32(sb + 0x3FC, crc32c(~0u, sb, 0x3FCu));
    if (blkwrite(dev, E2_SUPER_OFF / SETT, 2, sb) < 0) return -1;

    printf("  + giornale applicato: %u blocchi rimessi al loro posto\n", e2_gio_copie);
    corretti++;
    problemi++;
    return 0;
}

static void e2_controlla_dir(unsigned int ino, unsigned int padre,
                             const char *percorso, int prof);

/* Percorre una voce di directory. */
static void e2_voce(const unsigned char *v, unsigned int vino,
                    const char *nome, unsigned int len,
                    unsigned int ino_dir, unsigned int padre,
                    const char *percorso, int prof)
{
    unsigned char ib[128];
    char          pieno[192];
    unsigned int  i, k = 0;

    (void)v;

    /* "." e ".." si controllano ma non si scendono. */
    if (len == 1 && nome[0] == '.') {
        if (vino != ino_dir) {
            printf("  ! %s/. punta all'inode %u, dovrebbe essere %u\n",
                   percorso, vino, ino_dir);
            problemi++;
        }
        return;
    }
    if (len == 2 && nome[0] == '.' && nome[1] == '.') {
        if (vino != padre) {
            printf("  ! %s/.. punta all'inode %u, dovrebbe essere %u\n",
                   percorso, vino, padre);
            problemi++;
        }
        return;
    }

    for (i = 0; percorso[i] && k < 150u; i++) pieno[k++] = percorso[i];
    pieno[k++] = '/';
    for (i = 0; i < len && k < 190u; i++) pieno[k++] = nome[i];
    pieno[k] = '\0';

    if (vino == 0 || vino > e2_n_inode) {
        printf("  ! %s: numero di inode %u fuori scala\n", pieno, vino);
        problemi++;
        return;
    }

    /* ! SI CONTANO I RIFERIMENTI, non si marca "visto". Su ext2 un file
     * puo' avere piu' nomi (collegamenti fisici): trovarlo due volte e'
     * legittimo, e il controllo vero e' che il CONTEGGIO combaci con
     * i_links_count. Marcarlo come gia' visto darebbe un falso allarme su
     * ogni volume con un collegamento. */
    if (e2_link_conta[vino] < 0xFFFFu) e2_link_conta[vino]++;

    if (e2_bit(e2_inode_visti, vino)) return;   /* blocchi gia' contati */
    e2_segna(e2_inode_visti, vino);

    if (e2_leggi_inode(vino, ib) != 0) {
        printf("  ! %s: inode %u illeggibile\n", pieno, vino);
        problemi++;
        e2_incerto++;
        return;
    }

    if (e2_somme) {
        unsigned int lo = le16(e2_ino_pieno + 0x7C), hi = le16(e2_ino_pieno + 0x82);
        int alto = e2_dim_inode > 128u && le16(e2_ino_pieno + 0x80) >= 4u;
        unsigned int c = e2_somma_inode(e2_ino_pieno, vino);

        if (lo != (c & 0xFFFFu) || (alto && hi != (c >> 16))) {
            if (e2_somme_ko < 10u)
                printf("  ! %s: la somma dell'inode %u e' sbagliata\n", pieno, vino);
            e2_somme_ko++;
        }
    }

    e2_nome_corrente = pieno;
    e2_percorri_blocchi(ib, vino);
    e2_nome_corrente = "";

    if ((le16(ib) & E2_MODE_TIPO) == E2_MODE_DIR) {
        e2_segna(e2_inode_dir, vino);
        e2_controlla_dir(vino, ino_dir, pieno, prof + 1);
    }
}

static void e2_controlla_dir(unsigned int ino, unsigned int padre,
                             const char *percorso, int prof)
{
    unsigned char ib[128];
    unsigned int  dim, off;

    if (prof > MAX_PROF) {
        printf("  ! %s: annidamento oltre %d livelli, non scendo oltre\n",
               percorso[0] ? percorso : "/", MAX_PROF);
        problemi++;
        e2_incerto++;      /* what is below was not seen */
        return;
    }

    if (e2_leggi_inode(ino, ib) != 0) { e2_incerto++; return; }
    dim = le32(ib + 4);

    for (off = 0; off < dim; off += e2_dim_blocco) {
        unsigned int  idx = off / e2_dim_blocco, b, p = 0;
        unsigned char blocco[4096];         /* ! LOCALE: vedi sopra */

        /* ! A DIRECTORY CAN BE ANY SIZE. This stopped at the twelfth block,
         * silently: on a big directory the files beyond it were not seen, and
         * -r then declared their blocks free. */
        b = e2_mappa(ib, idx);
        if (b == 0) continue;
        if (e2_leggi_blocco(b, blocco) != 0) { e2_incerto++; continue; }

        while (p + 8u <= e2_dim_blocco) {
            unsigned int vino = le32(blocco + p);
            unsigned int rec  = le16(blocco + p + 4);
            unsigned int len  = blocco[p + 6];

            /* ! UN rec_len DI ZERO NON AVANZA: il ciclo girerebbe per
             * sempre sulla stessa voce. E' il modo piu' facile di far
             * bloccare un controllore su un volume rotto, quindi si
             * controlla per primo. */
            if (rec < 8u || (rec & 3u) != 0 || p + rec > e2_dim_blocco) {
                printf("  ! %s: voce di directory con rec_len %u a offset %u\n",
                       percorso[0] ? percorso : "/", rec, p);
                problemi++;
                e2_incerto++;  /* the entries after it were not seen */
                break;
            }

            if (vino != 0 && len > 0 && p + 8u + len <= e2_dim_blocco) {
                e2_voce(blocco + p, vino, (const char *)(blocco + p + 8), len,
                        ino, padre, percorso[0] ? percorso : "", prof);
            }
            p += rec;
        }
    }
}

/* =============================================================================
 * RIPARAZIONE DELLE BITMAP
 *
 * ! NON SI RIPARA UNA BITMAP "AGGIUSTANDO LE DIFFERENZE": si RISCRIVE da
 * cio' che si e' visto. La differenza e' sostanziale. Una bitmap dice
 * "questi e solo questi blocchi sono occupati": correggerne alcuni bit
 * lasciandone altri come stanno produce una terza versione, che non e' ne'
 * quella del volume ne' quella reale.
 *
 * ! E PER QUESTO SI RIPARA SOLO SE NON C'E' NEMMENO UN PUNTO DI
 * INCERTEZZA. Un inode illeggibile, un blocco fuori scala, una
 * funzionalita' non capita: basta uno e non si scrive. Riscrivere una
 * bitmap avendo saltato anche un solo inode significa marcare come liberi
 * i suoi blocchi — cioe' consegnarli alla prossima scrittura, che ci
 * scrivera' sopra. Il danno sarebbe causato dallo strumento chiamato a
 * ripararlo, ed e' peggio del guasto di partenza.
 *
 * ! I BIT DI RIEMPIMENTO DELL'ULTIMO GRUPPO RESTANO A UNO. La bitmap di
 * un gruppo copre sempre blocchi_per_gruppo bit, anche quando il gruppo e'
 * piu' corto: i bit che non corrispondono a nessun blocco vanno marcati
 * occupati, o l'allocatore proverebbe a usare blocchi che non esistono.
 * ============================================================================= */
/* =============================================================================
 * LA SUPERFICIE: LEGGERE OGNI BLOCCO, E SEGNARE QUELLI CHE NON TORNANO
 *
 * ! SI LEGGE A BLOCCHI GROSSI E SI SCENDE SOLO DOVE FA MALE. Un volume da
 * 57 GB sono cinquantotto milioni di blocchi da 1 KB: chiederli uno per uno
 * vuol dire un giorno di lavoro. Si leggono trentadue kilobyte per volta — il
 * massimo che blkread accetta — e SOLO quando quel pezzo non torna lo si
 * rilegge blocco per blocco.
 *
 * ! ED E' LI' CHE STA LA PRECISIONE: un pezzo da 32 KB che fallisce non vuol
 * dire trentadue blocchi andati, quasi sempre e' UNO. Marcarli tutti vorrebbe
 * dire buttare trentun blocchi sani per ogni settore rotto — su un disco che
 * si sta cercando di salvare.
 *
 * ! E LA SCANSIONE VIENE PRIMA DELL'ALBERO, non dopo. Cosi' quando si
 * percorrono i file la mappa dei blocchi andati c'e' gia', e il file
 * danneggiato lo si NOMINA mentre lo si incontra: senza un secondo giro e
 * senza tenere in memoria chi possiede cosa, che su un volume cosi' sarebbe
 * un quarto di gigabyte di tabella.
 * ============================================================================= */
static unsigned char e2_scan_buf[BLKIO_MAX_SETT * SETT];

static void e2_scansiona_superficie(void)
{
    unsigned int sett_b = e2_dim_blocco / SETT;
    unsigned int per_volta, b, tappa = 0;

    if (sett_b == 0) return;
    per_volta = BLKIO_MAX_SETT / sett_b;
    if (per_volta == 0) per_volta = 1;

    printf("\nSuperficie: leggo tutti i %u blocchi. Ogni punto e' un mega.\n  ",
           e2_n_blocchi);

    for (b = 1; b < e2_n_blocchi; ) {
        unsigned int n = per_volta;

        if (b + n > e2_n_blocchi) n = e2_n_blocchi - b;

        /* ! SI CONFRONTA COL CONTO, NON COL SEGNO, e questo e' costato la
         * prima prova. `blkread` rende QUANTI settori ha trasferito: rende un
         * numero negativo solo se non ne ha letto nemmeno uno, e un pezzo che
         * fallisce a meta' torna con un conto PARZIALE e positivo. Controllare
         * `< 0` vuol dire vedere solo i guasti che cominciano sul primo
         * settore — e nella prova con gli errori iniettati la scansione disse
         * «tutti i blocchi si leggono» mentre il registro del kernel elencava
         * le letture fallite. */
        if (blkread(dev, b * sett_b, n * sett_b, e2_scan_buf) != (int)(n * sett_b)) {
            unsigned int k;

            for (k = 0; k < n; k++)
                if (blkread(dev, (b + k) * sett_b, sett_b, e2_scan_buf)
                        != (int)sett_b) {
                    e2_segna(e2_blocchi_bad, b + k);
                    e2_bad_n++;
                }
        }
        b += n;

        if ((b * e2_dim_blocco) / (1024u * 1024u) > tappa) {
            tappa = (b * e2_dim_blocco) / (1024u * 1024u);
            printf(".");
        }
    }

    if (e2_bad_n == 0) printf("\n  = tutti i blocchi si leggono\n");
    else {
        printf("\n  ! %u blocchi non si leggono (%u ogni diecimila)\n",
               e2_bad_n, (e2_bad_n * 10000u) / e2_n_blocchi);
        problemi++;
    }
}

/* Scrive i 128 byte di un inode. Specchio di e2_leggi_inode: stessa strada
 * per trovarlo, legge il blocco, sostituisce e riscrive. */
static int e2_scrivi_inode(unsigned int num, const unsigned char *src)
{
    unsigned int g, idx, tab, off, blocco, base, k;
    unsigned char d[4096];

    if (num == 0 || num > e2_n_inode) return -1;

    g   = (num - 1u) / e2_i_per_gruppo;
    idx = (num - 1u) % e2_i_per_gruppo;
    tab = le32(e2_desc(g) + 8);

    off    = idx * e2_dim_inode;
    blocco = tab + off / e2_dim_blocco;
    base   = off % e2_dim_blocco;

    if (e2_leggi_blocco(blocco, d) != 0) return -1;
    for (k = 0; k < 128u; k++) d[base + k] = src[k];
    if (e2_somme) e2_somma_inode(d + base, num);
    return e2_scrivi_blocco(blocco, d);
}

/* Un blocco libero per gli indiretti dell'elenco. ! NON PUO' ESSERE UNO DEI
 * DIFETTOSI: si sta scrivendo la lista dei blocchi da non usare, metterla
 * sopra uno di quelli sarebbe la prima cosa a non tornare. */
static unsigned int e2_alloca_blocco(void)
{
    unsigned int b;

    for (b = e2_primo_dato; b < e2_n_blocchi; b++) {
        if (e2_bit(e2_blocchi_usati, b)) continue;
        if (e2_bit(e2_blocchi_bad, b))   continue;
        e2_segna(e2_blocchi_usati, b);
        return b;
    }
    return 0;
}

/* =============================================================================
 * SPOSTARE QUEL CHE SI RIESCE A LEGGERE
 *
 * ! UN SETTORE DEBOLE NON E' SEMPRE UN SETTORE PERSO. Il disco ritenta al suo
 * interno e a volte, al terzo o al quinto colpo, il dato torna: e' la ragione
 * per cui un recupero serio RILEGGE invece di arrendersi alla prima risposta
 * negativa. Ogni tentativo pero' costa — su questo sistema una scadenza intera
 * — quindi il numero e' una scelta scritta qui e non una speranza infinita.
 *
 * ! E QUEL CHE TORNA SI METTE AL SICURO SUBITO, altrove. Lasciare i dati dove
 * sono vorrebbe dire sperare che la prossima lettura vada altrettanto bene, e
 * un settore che ha gia' fatto fatica non migliora. Si alloca un blocco sano,
 * ci si scrive dentro, e si cambia il puntatore nel file: da quel momento il
 * file e' intero e il blocco andato non lo usa piu' nessuno.
 *
 * ! SE NON TORNA, IL BUCO SI DICHIARA. Il blocco nuovo si riempie di zeri e il
 * referto dice quale file e quanti byte: un file col buco dichiarato si puo'
 * ancora usare per quel che resta, mentre un file che non si apre piu' e' un
 * file perso due volte — una dal disco e una da chi lo stava salvando.
 * ============================================================================= */
#define RILETTURE 6

static int e2_sposta_uno(const Spostare *sp, unsigned int *recuperati,
                         unsigned int *bucati)
{
    unsigned char dati[4096], ib[128], ind[4096];
    unsigned int  sett_b = e2_dim_blocco / SETT;
    unsigned int  nuovo, t;
    int           letto = 0;

    for (t = 0; t < RILETTURE && !letto; t++)
        if (blkread(dev, sp->blocco * sett_b, sett_b, dati) == (int)sett_b)
            letto = 1;

    if (!letto) {
        unsigned int k;
        for (k = 0; k < e2_dim_blocco; k++) dati[k] = 0;
    }

    nuovo = e2_alloca_blocco();
    if (nuovo == 0) {
        printf("  ! inode %u: non c'e' un blocco libero dove spostare il %u\n",
               sp->ino, sp->blocco);
        return -1;
    }

    if (e2_scrivi_blocco(nuovo, dati) != 0) {
        printf("  ! inode %u: il blocco nuovo %u non si scrive\n", sp->ino, nuovo);
        return -1;
    }

    /* Il puntatore: o dentro l'inode, o dentro un blocco di indiretti. */
    if (sp->contenitore == 0) {
        if (e2_leggi_inode(sp->ino, ib) != 0) return -1;
        ib[40 + sp->idx * 4 + 0] = (unsigned char)(nuovo);
        ib[40 + sp->idx * 4 + 1] = (unsigned char)(nuovo >> 8);
        ib[40 + sp->idx * 4 + 2] = (unsigned char)(nuovo >> 16);
        ib[40 + sp->idx * 4 + 3] = (unsigned char)(nuovo >> 24);
        if (e2_scrivi_inode(sp->ino, ib) != 0) return -1;
    } else {
        if (e2_leggi_blocco(sp->contenitore, ind) != 0) return -1;
        ind[sp->idx * 4 + 0] = (unsigned char)(nuovo);
        ind[sp->idx * 4 + 1] = (unsigned char)(nuovo >> 8);
        ind[sp->idx * 4 + 2] = (unsigned char)(nuovo >> 16);
        ind[sp->idx * 4 + 3] = (unsigned char)(nuovo >> 24);
        if (e2_scrivi_blocco(sp->contenitore, ind) != 0) return -1;
    }

    if (letto) {
        (*recuperati)++;
        printf("  = inode %u: blocco %u recuperato alla %u lettura, ora e' il %u\n",
               sp->ino, sp->blocco, t, nuovo);
    } else {
        (*bucati)++;
        printf("  ! inode %u: blocco %u NON recuperato: %u byte di zeri nel "
               "file (ora e' il %u)\n", sp->ino, sp->blocco, e2_dim_blocco, nuovo);
    }
    return 0;
}

static void e2_sposta_i_danneggiati(void)
{
    unsigned int i, recuperati = 0, bucati = 0, falliti = 0;

    if (e2_da_spostare_n == 0) return;

    printf("\nDati sopra i blocchi andati: provo a spostarli\n");
    printf("  (fino a %d riletture per blocco: puo' volerci)\n", RILETTURE);

    for (i = 0; i < e2_da_spostare_n; i++)
        if (e2_sposta_uno(&e2_da_spostare[i], &recuperati, &bucati) != 0)
            falliti++;

    printf("  %u recuperati interi, %u con un buco, %u non spostati\n",
           recuperati, bucati, falliti);
    if (recuperati) corretti++;

    /* ! E I BLOCCHI VECCHI NON SONO PIU' DI NESSUNO: il puntatore adesso guarda
     * altrove, quindi vanno tolti dalla mappa degli occupati — o la bitmap li
     * dichiarerebbe di un file che non li usa piu', e l'inode 1 non potrebbe
     * prenderseli. Se li riprende e2_marca_difettosi, subito dopo. */
    for (i = 0; i < e2_da_spostare_n; i++) {
        unsigned int b = e2_da_spostare[i].blocco;
        e2_blocchi_usati[b >> 3] &= (unsigned char)~(1u << (b & 7));
    }
}

/* =============================================================================
 * SCRIVERE L'ELENCO: I BLOCCHI ANDATI DIVENTANO L'INODE 1
 *
 * ! NON BASTA MARCARLI OCCUPATI NELLA BITMAP. Un blocco occupato che non
 * appartiene a nessun inode e' un blocco PERSO, e il prossimo controllo lo
 * libera — restituendolo all'allocatore, cioe' rimettendo un settore rotto in
 * circolazione. L'unico posto dove un blocco puo' stare occupato per sempre
 * senza essere di un file e' l'inode 1, che ext2 tiene apposta.
 *
 * ! E GLI INDIRETTI SI ALLOCANO. Dodici blocchi stanno nei puntatori diretti;
 * il duecentosessantottesimo vuole l'indiretto semplice, e oltre i 268 il
 * doppio. I settecentoquarantaquattro dell'Acer arrivano fin la'.
 * ============================================================================= */
static int e2_marca_difettosi(void)
{
    unsigned char ino[128];
    unsigned int  punt = e2_dim_blocco / 4u;
    unsigned int  b, n = 0, usati_meta = 0;
    unsigned int  ind1 = 0, ind2 = 0, ind2_sotto = 0;
    unsigned int  i_ind1 = 0, i_ind2 = 0;
    unsigned int  k;

    for (k = 0; k < 128u; k++) ino[k] = 0;

    /* ! MODO E CONTEGGIO DI COLLEGAMENTI A ZERO, come lo scrive mke2fs:
     * l'inode 1 non e' un file, non si apre e non si cancella. Cio' che conta
     * e' l'elenco dei blocchi e i_blocks. */
    for (k = 0; k < e2_dim_blocco; k++) e2_ind1[k] = 0;
    for (k = 0; k < e2_dim_blocco; k++) e2_ind2[k] = 0;
    for (k = 0; k < e2_dim_blocco; k++) e2_ind3[k] = 0;

    for (b = 1; b < e2_n_blocchi; b++) {
        if (!e2_bit(e2_blocchi_bad, b)) continue;

        if (n < 12u) {                       /* diretti */
            ino[40 + n * 4 + 0] = (unsigned char)(b);
            ino[40 + n * 4 + 1] = (unsigned char)(b >> 8);
            ino[40 + n * 4 + 2] = (unsigned char)(b >> 16);
            ino[40 + n * 4 + 3] = (unsigned char)(b >> 24);
        } else if (n < 12u + punt) {         /* indiretto semplice */
            if (ind1 == 0) {
                ind1 = e2_alloca_blocco();
                if (ind1 == 0) return -1;
                usati_meta++;
                ino[40 + 12 * 4 + 0] = (unsigned char)(ind1);
                ino[40 + 12 * 4 + 1] = (unsigned char)(ind1 >> 8);
                ino[40 + 12 * 4 + 2] = (unsigned char)(ind1 >> 16);
                ino[40 + 12 * 4 + 3] = (unsigned char)(ind1 >> 24);
            }
            e2_ind1[i_ind1 * 4 + 0] = (unsigned char)(b);
            e2_ind1[i_ind1 * 4 + 1] = (unsigned char)(b >> 8);
            e2_ind1[i_ind1 * 4 + 2] = (unsigned char)(b >> 16);
            e2_ind1[i_ind1 * 4 + 3] = (unsigned char)(b >> 24);
            i_ind1++;
        } else {                             /* doppio indiretto */
            if (ind2 == 0) {
                ind2 = e2_alloca_blocco();
                if (ind2 == 0) return -1;
                usati_meta++;
                ino[40 + 13 * 4 + 0] = (unsigned char)(ind2);
                ino[40 + 13 * 4 + 1] = (unsigned char)(ind2 >> 8);
                ino[40 + 13 * 4 + 2] = (unsigned char)(ind2 >> 16);
                ino[40 + 13 * 4 + 3] = (unsigned char)(ind2 >> 24);
            }
            if (i_ind2 == 0 || i_ind2 >= punt) {   /* serve un sotto-indiretto */
                unsigned int slot;

                if (ind2_sotto != 0 &&
                    e2_scrivi_blocco(ind2_sotto, e2_ind2) != 0) return -1;
                for (k = 0; k < e2_dim_blocco; k++) e2_ind2[k] = 0;

                ind2_sotto = e2_alloca_blocco();
                if (ind2_sotto == 0) return -1;
                usati_meta++;
                slot = (n - 12u - punt) / punt;
                if (slot >= punt) return -1;       /* oltre il doppio: no */
                /* Il puntatore al sotto-indiretto va nel blocco DOPPIO, che
                 * si tiene in e2_ind3 — qui non serve ad altro — e si scrive
                 * una volta sola, alla fine. */
                e2_ind3[slot * 4 + 0] = (unsigned char)(ind2_sotto);
                e2_ind3[slot * 4 + 1] = (unsigned char)(ind2_sotto >> 8);
                e2_ind3[slot * 4 + 2] = (unsigned char)(ind2_sotto >> 16);
                e2_ind3[slot * 4 + 3] = (unsigned char)(ind2_sotto >> 24);
                i_ind2 = 0;
            }
            e2_ind2[i_ind2 * 4 + 0] = (unsigned char)(b);
            e2_ind2[i_ind2 * 4 + 1] = (unsigned char)(b >> 8);
            e2_ind2[i_ind2 * 4 + 2] = (unsigned char)(b >> 16);
            e2_ind2[i_ind2 * 4 + 3] = (unsigned char)(b >> 24);
            i_ind2++;
        }

        e2_segna(e2_blocchi_usati, b);   /* occupato: la bitmap lo dira' */
        n++;
    }

    if (n == 0) return 0;

    if (ind1 != 0 && e2_scrivi_blocco(ind1, e2_ind1) != 0) return -1;
    if (ind2_sotto != 0 && e2_scrivi_blocco(ind2_sotto, e2_ind2) != 0) return -1;
    if (ind2 != 0 && e2_scrivi_blocco(ind2, e2_ind3) != 0) return -1;

    /* i_blocks e' in unita' da 512 byte e comprende gli indiretti. */
    {
        unsigned int settori = (n + usati_meta) * (e2_dim_blocco / SETT);
        ino[28] = (unsigned char)(settori);
        ino[29] = (unsigned char)(settori >> 8);
        ino[30] = (unsigned char)(settori >> 16);
        ino[31] = (unsigned char)(settori >> 24);
    }

    if (e2_scrivi_inode(1u, ino) != 0) return -1;

    printf("  = %u blocchi difettosi scritti nell'inode 1", n);
    if (usati_meta) printf(" (piu' %u blocchi di elenco)", usati_meta);
    printf("\n");
    return 0;
}

static int e2_ripara_bitmap(void)
{
    unsigned char bm[4096], sb[SETT * 2];
    unsigned int  g, i, tot_lib_b = 0, tot_lib_i = 0, scritte = 0;
    int           sbagliato = 0;

    for (g = 0; g < e2_n_gruppi; g++) {
        unsigned char *d = e2_desc(g);
        unsigned int b_bm, i_bm, n_b, lib_b = 0, lib_i = 0, dirs = 0;
        unsigned int flags = (e2_somme || e2_gdtsomma) ? le16(d + 18) : 0u;
        unsigned int ultimo_ino = 0, salta_i;
        unsigned int c;

        b_bm = le32(d + 0);
        i_bm = le32(d + 4);
        if (b_bm >= e2_n_blocchi || i_bm >= e2_n_blocchi) { sbagliato = 1; continue; }

        n_b = e2_b_per_gruppo;
        if (g == e2_n_gruppi - 1u) {
            unsigned int resto = e2_n_blocchi - e2_primo_dato -
                                 g * e2_b_per_gruppo;
            if (resto < n_b) n_b = resto;
        }

        /* --- bitmap dei blocchi --- */
        for (i = 0; i < e2_dim_blocco; i++) bm[i] = 0;
        for (i = 0; i < e2_b_per_gruppo; i++) {
            unsigned int blocco = e2_primo_dato + g * e2_b_per_gruppo + i;

            if (i >= n_b) {                  /* riempimento: sempre occupato */
                e2_segna(bm, i);
                continue;
            }
            if (e2_bit(e2_blocchi_usati, blocco)) e2_segna(bm, i);
            else                                   lib_b++;
        }
        /* ! OLTRE L'ULTIMO DEL GRUPPO I BIT VALGONO 1, in tutt'e due le
         * bitmap: e2fsck lo chiama <<padding>> e lo vuole cosi'. */
        for (i = e2_b_per_gruppo; i < e2_dim_blocco * 8u; i++) e2_segna(bm, i);
        if (e2_scrivi_blocco(b_bm, bm) == 0) scritte++;
        else sbagliato = 1;
        flags &= ~(unsigned int)E2_BG_BLOCK_UNINIT;     /* now it is real */
        if (e2_somme) {
            c = crc32c(e2_seme, bm, e2_b_per_gruppo / 8u);
            e2_p16(d + 0x18, c & 0xFFFFu);
            if (e2_dim_desc >= 0x3Au) e2_p16(d + 0x38, c >> 16);
        }

        /* --- bitmap degli inode --- */
        for (i = 0; i < e2_dim_blocco; i++) bm[i] = 0;
        for (i = 0; i < e2_i_per_gruppo; i++) {
            unsigned int num = g * e2_i_per_gruppo + i + 1u;

            if (num > e2_n_inode) { e2_segna(bm, i); continue; }

            /* ! Gli inode riservati sono occupati per definizione e non
             * compaiono in nessuna directory: senza questa riga li si
             * dichiarerebbe liberi, e il primo file creato ci scriverebbe
             * sopra il superblocco di riserva. */
            if (num < e2_primo_inode || e2_bit(e2_inode_visti, num)) {
                e2_segna(bm, i);
                ultimo_ino = i + 1u;
                if (e2_bit(e2_inode_dir, num)) dirs++;
            } else {
                lib_i++;
            }
        }
        for (i = e2_i_per_gruppo; i < e2_dim_blocco * 8u; i++) e2_segna(bm, i);

        /* ! AN EMPTY GROUP MARKED INODE_UNINIT STAYS SO. Its inode table may
         * never have been zeroed: making the bitmap real would hand garbage
         * to whoever looks at it. Nothing is lost, it is all free. */
        salta_i = (flags & E2_BG_INODE_UNINIT) && ultimo_ino == 0;
        if (!salta_i) {
            if (e2_scrivi_blocco(i_bm, bm) == 0) scritte++;
            else sbagliato = 1;
            flags &= ~(unsigned int)E2_BG_INODE_UNINIT;
            if (e2_somme) {
                c = crc32c(e2_seme, bm, e2_i_per_gruppo / 8u);
                e2_p16(d + 0x1A, c & 0xFFFFu);
                if (e2_dim_desc >= 0x3Cu) e2_p16(d + 0x3A, c >> 16);
            }
        }

        /* --- i conteggi del descrittore --- */
        e2_p16(d + 12, lib_b & 0xFFFFu);
        e2_p16(d + 14, lib_i & 0xFFFFu);
        e2_p16(d + 16, dirs & 0xFFFFu);
        if (e2_dim_desc >= 64u) {
            e2_p16(d + 0x2C, lib_b >> 16);
            e2_p16(d + 0x2E, lib_i >> 16);
            e2_p16(d + 0x30, dirs >> 16);
        }

        if (e2_somme || e2_gdtsomma) {
            /* itable_unused may only shrink: past it nothing is ever read */
            unsigned int inus = le16(d + 28);

            if (e2_dim_desc >= 64u) inus |= (unsigned int)le16(d + 0x32) << 16;
            if (inus > e2_i_per_gruppo - ultimo_ino) inus = e2_i_per_gruppo - ultimo_ino;
            e2_p16(d + 28, inus & 0xFFFFu);
            if (e2_dim_desc >= 64u) e2_p16(d + 0x32, inus >> 16);
            e2_p16(d + 18, flags);
            e2_p16(d + 30, e2_somma_desc(g));
        }

        tot_lib_b += lib_b;
        tot_lib_i += lib_i;
    }

    for (i = 0; i < e2_desc_quanti; i++)
        if (e2_scrivi_blocco(e2_desc_blocco + i, e2_gdt + i * e2_dim_blocco) != 0)
            sbagliato = 1;

    /* --- i conteggi del superblocco --- */
    if (blkread(dev, E2_SUPER_OFF / SETT, 2, sb) >= 0) {
        e2_p32(sb + 12, tot_lib_b);
        e2_p32(sb + 16, tot_lib_i);

        /* The orphan list: its inodes were freed above with everything no
         * directory names, and a list pointing at them would be followed at
         * the next mount. */
        if (le32(sb + 232) != 0) {
            printf("  + lista degli orfani svuotata\n");
            e2_p32(sb + 232, 0u);
        }
        if (e2_somme) e2_p32(sb + 0x3FC, crc32c(~0u, sb, 0x3FCu));
        if (!sbagliato && blkwrite(dev, E2_SUPER_OFF / SETT, 2, sb) >= 0) {
            printf("  + bitmap e conteggi riscritti (%u bitmap): "
                   "%u blocchi e %u inode liberi\n",
                   scritte, tot_lib_b, tot_lib_i);
            return 0;
        }
    }

    printf("  ! la riscrittura non e' riuscita del tutto: rilancia il controllo\n");
    return -1;
}

/* Confronta le bitmap del volume con quelle appena costruite. */
static void e2_controlla_bitmap(void)
{
    unsigned char bm[4096];
    unsigned int  g, i, diff_b = 0, diff_i = 0, usati_veri = 0, ino_veri = 0;
    unsigned int  desc_ko = 0, somme_ko = 0, riempimento = 0;
    /* ! QUANTI PROBLEMI ERANO GIA' NOTI PRIMA DI QUESTO GIRO. Una sola
     * riscrittura delle bitmap ne sana PIU' DI UNO — le bitmap dei
     * blocchi, quelle degli inode, i conteggi dei descrittori e quelli del
     * superblocco — e il resoconto deve dirlo. La prima versione contava
     * "1 corretto" su "2 problemi" perche' incrementava corretti una volta
     * sola: sembrava che qualcosa fosse rimasto rotto, e non era vero. */
    unsigned int  prima = problemi;

    printf("\nBitmap dei blocchi e degli inode\n");

    for (g = 0; g < e2_n_gruppi; g++) {
        const unsigned char *d = e2_desc(g);
        unsigned int b_bm, i_bm, n_b, n_i, lib_b = 0, lib_i = 0, d_b, d_i;
        unsigned int flags = (e2_somme || e2_gdtsomma) ? le16(d + 18) : 0u;

        b_bm = le32(d + 0);
        i_bm = le32(d + 4);

        /* ! La bitmap di un gruppo deve stare DENTRO il volume: un numero
         * fuori intervallo non e' un dettaglio, e' il segno che il
         * descrittore non e' un descrittore. Con flex_bg non sta per forza
         * nel suo gruppo. */
        if (b_bm < e2_primo_dato || b_bm >= e2_n_blocchi ||
            i_bm < e2_primo_dato || i_bm >= e2_n_blocchi) {
            printf("  ! gruppo %u: bitmap a %u/%u, fuori dal volume\n",
                   g, b_bm, i_bm);
            problemi++;
            e2_incerto++;
            continue;
        }

        if ((e2_somme || e2_gdtsomma) && le16(d + 30) != e2_somma_desc(g)) {
            if (somme_ko < 10u)
                printf("  ! gruppo %u: la somma del descrittore e' sbagliata\n", g);
            somme_ko++;
        }

        n_b = e2_b_per_gruppo;
        if (g == e2_n_gruppi - 1u) {
            unsigned int resto = e2_n_blocchi - e2_primo_dato -
                                 g * e2_b_per_gruppo;
            if (resto < n_b) n_b = resto;
        }
        n_i = e2_i_per_gruppo;

        /* ! A GROUP MARKED UNINIT HAS NO BITMAP ON DISK: what is there means
         * nothing, and the bitmap it stands for holds its metadata alone. */
        if ((flags & E2_BG_BLOCK_UNINIT) || e2_leggi_blocco(b_bm, bm) == 0) {
            if (!(flags & E2_BG_BLOCK_UNINIT) && e2_somme) {
                unsigned int c = crc32c(e2_seme, bm, e2_b_per_gruppo / 8u);
                unsigned int atteso = le16(d + 0x18);

                if (e2_dim_desc >= 0x3Au) atteso |= (unsigned int)le16(d + 0x38) << 16;
                else                      c &= 0xFFFFu;
                if (atteso != c) {
                    if (somme_ko < 10u)
                        printf("  ! gruppo %u: la somma della bitmap dei blocchi "
                               "e' sbagliata\n", g);
                    somme_ko++;
                }
            }
            for (i = 0; i < n_b; i++) {
                unsigned int blocco = e2_primo_dato + g * e2_b_per_gruppo + i;
                int nel_volume = e2_bit(e2_blocchi_usati, blocco);
                int nella_bm   = (flags & E2_BG_BLOCK_UNINIT) ? e2_bit(e2_meta, blocco)
                                                              : e2_bit(bm, i);

                if (nel_volume) usati_veri++;
                else            lib_b++;
                if (nel_volume != nella_bm) diff_b++;
            }
            if (!(flags & E2_BG_BLOCK_UNINIT))
                for (i = n_b; i < e2_dim_blocco * 8u; i++)
                    if (!e2_bit(bm, i)) { riempimento++; break; }
        } else {
            e2_incerto++;
        }

        if ((flags & E2_BG_INODE_UNINIT) || e2_leggi_blocco(i_bm, bm) == 0) {
            if (!(flags & E2_BG_INODE_UNINIT) && e2_somme) {
                unsigned int c = crc32c(e2_seme, bm, e2_i_per_gruppo / 8u);
                unsigned int atteso = le16(d + 0x1A);

                if (e2_dim_desc >= 0x3Cu) atteso |= (unsigned int)le16(d + 0x3A) << 16;
                else                      c &= 0xFFFFu;
                if (atteso != c) {
                    if (somme_ko < 10u)
                        printf("  ! gruppo %u: la somma della bitmap degli inode "
                               "e' sbagliata\n", g);
                    somme_ko++;
                }
            }
            for (i = 0; i < n_i; i++) {
                unsigned int num = g * e2_i_per_gruppo + i + 1u;
                int visto_qui = e2_bit(e2_inode_visti, num);
                int nella_bm  = (flags & E2_BG_INODE_UNINIT) ? 0 : e2_bit(bm, i);

                if (visto_qui || num < e2_primo_inode) ino_veri++;
                else                                   lib_i++;
                /* ! Gli inode riservati (1..primo_inode-1) sono occupati
                 * per definizione e non compaiono in nessuna directory:
                 * confrontarli con "visto" darebbe una differenza su ogni
                 * volume sano. */
                if (num < e2_primo_inode) continue;
                if (visto_qui != nella_bm) diff_i++;
            }
            if (!(flags & E2_BG_INODE_UNINIT))
                for (i = n_i; i < e2_dim_blocco * 8u; i++)
                    if (!e2_bit(bm, i)) { riempimento++; break; }
        } else {
            e2_incerto++;
        }

        d_b = le16(d + 12);
        d_i = le16(d + 14);
        if (e2_dim_desc >= 64u) {
            d_b |= (unsigned int)le16(d + 0x2C) << 16;
            d_i |= (unsigned int)le16(d + 0x2E) << 16;
        }
        if (d_b != lib_b || d_i != lib_i) desc_ko++;
    }

    if (somme_ko) {
        printf("  ! %u somme sbagliate tra descrittori e bitmap\n", somme_ko);
        problemi++;
    }
    if (riempimento) {
        printf("  ! %u bitmap: i bit oltre la fine del gruppo non sono a uno\n",
               riempimento);
        problemi++;
    }
    if (desc_ko) {
        printf("  ! %u gruppi: il descrittore conta i liberi in modo sbagliato\n",
               desc_ko);
        problemi++;
    }

    /* --- prima si RIFERISCE tutto, poi si ripara una volta sola --------- */
    if (diff_b) {
        printf("  ! %u blocchi: la bitmap dice una cosa e i file un'altra\n",
               diff_b);
        problemi++;
    }
    if (diff_i) {
        printf("  ! %u inode: la bitmap dice una cosa e le directory "
               "un'altra\n", diff_i);
        problemi++;
    }

    {
        unsigned int liberi_veri = (e2_n_blocchi - e2_primo_dato) - usati_veri;

        if (e2_liberi_b != liberi_veri) {
            printf("  ! il superblocco dichiara %u blocchi liberi, ne conto %u\n",
                   e2_liberi_b, liberi_veri);
            problemi++;
        }
        if (e2_liberi_i != e2_n_inode - ino_veri) {
            printf("  ! il superblocco dichiara %u inode liberi, ne conto %u\n",
                   e2_liberi_i, e2_n_inode - ino_veri);
            problemi++;
        }
    }

    if (problemi == prima && e2_riscrivere == 0) {
        printf("  = le bitmap e i conteggi corrispondono a cio' che i file "
               "usano davvero\n");
        return;
    }

    /* ! UNA SOLA RISCRITTURA SANA TUTTO QUELLO CHE E' STATO APPENA
     * ELENCATO: bitmap dei blocchi, degli inode, conteggi dei descrittori
     * e del superblocco vengono ricalcolati insieme dalla stessa mappa.
     * Per questo `corretti` cresce di quanti problemi ha sanato, non di
     * uno: un resoconto che dicesse "1 corretto su 3" farebbe credere che
     * due siano rimasti. */
    if (!ripara) {
        printf("    (con -r le bitmap e i conteggi si riscrivono da cio' che\n");
        printf("     i file usano davvero)\n");
        return;
    }

    if (e2_incerto > 0) {
        printf("    ! NON riscrivo niente: ci sono %u punti in cui non ho\n",
               e2_incerto);
        printf("       potuto sapere cosa c'era (righe '!' qui sopra).\n");
        printf("       Riscrivere una bitmap avendo saltato anche un solo\n");
        printf("       inode vuol dire dichiarare liberi i suoi blocchi,\n");
        printf("       cioe' consegnarli alla prossima scrittura.\n");
        return;
    }

    if (e2_ripara_bitmap() == 0) corretti += (problemi - prima) + e2_riscrivere;
}

/* I conteggi dei collegamenti trovati contro quelli dichiarati. */
static void e2_controlla_link(void)
{
    unsigned char ib[128];
    unsigned int  i, diversi = 0;

    printf("\nConteggi dei collegamenti\n");

    /* ! From the first ordinary inode on: the reserved ones and the special
     * ones are named by no directory, so "1 link, 0 found" would appear on
     * every ext3. */
    for (i = e2_primo_inode; i <= e2_n_inode; i++) {
        unsigned int dichiarato, k, speciale = 0;

        for (k = 0; k < E2_SPECIALI; k++)
            if (e2_speciale[k] == i) speciale = 1;
        if (speciale) continue;

        if (!e2_bit(e2_inode_visti, i)) continue;
        if (e2_leggi_inode(i, ib) != 0) continue;

        dichiarato = le16(ib + 26);

        /* ! Per una directory il conteggio comprende il suo "." e il ".."
         * di ogni figlia, che questo giro non conta: si salta invece di
         * segnalare una differenza che non c'e'. */
        if ((le16(ib) & E2_MODE_TIPO) == E2_MODE_DIR) continue;

        if (dichiarato != e2_link_conta[i]) {
            if (diversi < 10u)
                printf("  ! inode %u: dichiara %u collegamenti, ne trovo %u\n",
                       i, dichiarato, (unsigned int)e2_link_conta[i]);
            diversi++;
        }
    }

    if (diversi == 0) {
        printf("  = tutti coerenti\n");
    } else {
        printf("  ! %u inode con il conteggio sbagliato\n", diversi);
        problemi++;
    }
}

static void e2_controlla(void)
{
    unsigned int bb = (e2_n_blocchi + 7u) / 8u;
    unsigned int ib = (e2_n_inode + 2u + 7u) / 8u;

    e2_incerto = 0;

    e2_blocchi_usati = (unsigned char *)calloc(bb, 1);
    e2_inode_visti   = (unsigned char *)calloc(ib, 1);
    e2_inode_dir     = (unsigned char *)calloc(ib, 1);
    e2_link_conta    = (unsigned short *)calloc(e2_n_inode + 2u, 2);
    e2_meta          = (unsigned char *)calloc(bb, 1);
    e2_blocchi_bad   = scansiona ? (unsigned char *)calloc(bb, 1) : NULL;
    e2_bad_n = e2_bad_in_uso = 0;
    e2_blocchi_contati = 0;
    e2_da_spostare_n = 0;

    if (e2_blocchi_usati == NULL || e2_inode_visti == NULL ||
        e2_inode_dir == NULL || e2_link_conta == NULL || e2_meta == NULL ||
        (scansiona && e2_blocchi_bad == NULL)) {
        printf("\n  ! memoria insufficiente per le mappe di %u blocchi e "
               "%u inode\n", e2_n_blocchi, e2_n_inode);
        problemi++;
        free(e2_blocchi_usati); free(e2_inode_visti);
        free(e2_inode_dir); free(e2_link_conta); free(e2_blocchi_bad);
        e2_blocchi_bad = NULL;
        return;
    }

    /* I metadati sono occupati per definizione: superblocco, descrittori,
     * bitmap e tabelle degli inode. Non appartengono a nessun file, e
     * senza marcarli risulterebbero tutti "liberi ma occupati". */
    {
        unsigned int  g, b;

        for (b = 0; b < e2_primo_dato + 1u && b < e2_n_blocchi; b++)
            e2_segna(e2_blocchi_usati, b);

        /* I descrittori PRIMARI: dimenticarli e' la meta' del falso
         * allarme descritto sopra. */
        for (b = 0; b < e2_desc_quanti; b++)
            e2_usa_blocco(e2_desc_blocco + b, 0);

        /* ! I BLOCCHI PER CRESCERE (22 ottobre 2026). mke2fs mette di serie
         * resize_inode: dopo i descrittori ci sono s_reserved_gdt_blocks
         * blocchi vuoti, in ogni gruppo con la copia, e li tiene l'inode 7.
         * Non segnati qui, `chkdsk -r` li dichiarava LIBERI (256 per gruppo
         * su un disco da 64 MB) e il primo file scritto dopo ci finiva
         * sopra: l'inode 7 punta li', e e2fsck trova blocchi di due padroni.
         * Visto quando il controllo e' diventato automatico all'avvio. */
        for (b = 0; b < e2_gdt_riserva; b++)
            e2_usa_blocco(e2_desc_blocco + e2_desc_quanti + b, 0);

        for (g = 0; g < e2_n_gruppi; g++) {
            const unsigned char *d = e2_desc(g);
            unsigned int tab, k;

            /* ...e le copie di riserva, dove ci sono. */
            if (g > 0 && gruppo_ha_backup(g)) {
                unsigned int base = e2_primo_dato + g * e2_b_per_gruppo;
                for (k = 0; k < e2_desc_quanti + 1u + e2_gdt_riserva; k++)
                    e2_usa_blocco(base + k, 0);
            }

            e2_usa_blocco(le32(d + 0), 0);
            e2_usa_blocco(le32(d + 4), 0);
            tab = le32(d + 8);
            for (k = 0; k < (e2_i_per_gruppo * e2_dim_inode + e2_dim_blocco - 1u)
                             / e2_dim_blocco; k++)
                e2_usa_blocco(tab + k, 0);
        }

        if (e2_incompat & E2_INC_MMP) {
            unsigned char sb[SETT * 2];

            if (blkread(dev, E2_SUPER_OFF / SETT, 2, sb) >= 0)
                e2_usa_blocco(le32(sb + 360), 0);
            else
                e2_incerto++;
        }

        /* What an uninitialized group's bitmap stands for. */
        for (b = 0; b < bb; b++) e2_meta[b] = e2_blocchi_usati[b];
    }

    /* =====================================================================
     * ! L'INODE 1 E' QUELLO DEI BLOCCHI DIFETTOSI, E FINO AL 22 SETTEMBRE 2026
     * QUESTO CONTROLLORE NON LO GUARDAVA.
     *
     * Su ext2 i settori che il disco non sa piu' leggere non si buttano: si
     * elencano nell'inode 1, che non e' un file e non sta in nessuna directory
     * — esiste perche' quei blocchi risultino OCCUPATI e nessuno ci allochi
     * sopra un file. E' il modo in cui un disco con qualche cicatrice continua
     * a lavorare per anni.
     *
     * ! MA UN CONTROLLORE CHE NON LO PERCORRE LI DICHIARA LIBERI. Le bitmap
     * qui si ricostruiscono da cio' che si e' VISTO: i blocchi dell'inode 1,
     * non essendo raggiungibili da nessuna directory, non venivano visti da
     * nessuno — e la riparazione li avrebbe restituiti all'allocatore. Cioe'
     * il primo file creato dopo una riparazione sarebbe finito esattamente
     * sopra i settori rotti, ed e' il modo piu' rapido di perdere dei dati
     * con un programma che si chiama «controllo del disco».
     *
     * Percio' si percorre PRIMA della root: quel che dichiara occupato deve
     * esserci gia' quando si contano i doppioni.
     * ===================================================================== */
    if (scansiona) e2_scansiona_superficie();

    printf("\nBlocchi difettosi dichiarati (inode 1)\n");
    {
        unsigned char bb[128];
        unsigned int  prima = e2_blocchi_contati;

        e2_segna(e2_inode_visti, 1u);
        if (e2_leggi_inode(1u, bb) == 0) {
            e2_percorri_blocchi(bb, 1u);
            if (e2_blocchi_contati > prima)
                printf("  = %u blocchi elencati come difettosi: restano occupati\n",
                       e2_blocchi_contati - prima);
            else
                printf("  = nessuno\n");
        } else {
            printf("  ! l'inode 1 non si legge\n");
            problemi++;
        }
    }

    /* ! GLI ALTRI INODE RISERVATI HANNO BLOCCHI LORO, e nessuna directory li
     * nomina: il journal di ext3 (inode 8), per esempio, che un volume ext3
     * senza bit incompatibili porta con se'. Si percorrono come l'inode 1.
     * Il 7 (resize_inode) no: i blocchi a cui punta sono quelli per crescere,
     * gia' segnati sopra; resta da segnare il suo doppio indiretto. */
    {
        unsigned char ib[128];
        unsigned int  num, k;

        for (num = 3u; num < e2_primo_inode && num <= e2_n_inode; num++) {
            if (e2_leggi_inode(num, ib) != 0) continue;
            e2_segna(e2_inode_visti, num);
            if (num == 7u) e2_usa_blocco(le32(ib + 40 + 13 * 4), 7u);
            else           e2_percorri_blocchi(ib, num);
        }

        /* Those above the reserved range: the orphan file, the quota files. */
        for (k = 0; k < E2_SPECIALI; k++) {
            num = e2_speciale[k];
            if (num < e2_primo_inode || num > e2_n_inode) continue;
            if (e2_leggi_inode(num, ib) != 0) { e2_incerto++; continue; }
            e2_segna(e2_inode_visti, num);
            e2_percorri_blocchi(ib, num);
        }
    }

    printf("\nAlbero delle directory\n");
    {
        unsigned char rb[128];
        e2_segna(e2_inode_visti, 2u);            /* la root */
        e2_segna(e2_inode_dir, 2u);
        if (e2_leggi_inode(2u, rb) == 0) {
            e2_percorri_blocchi(rb, 2u);
            e2_controlla_dir(2u, 2u, "", 0);
            printf("  = percorso\n");
        } else {
            printf("  ! l'inode della root (2) non si legge\n");
            problemi++;
        }
    }

    /* ! PRIMA DELLE BITMAP, E NON E' UN DETTAGLIO D'ORDINE: e2_marca_difettosi
     * aggiunge i blocchi andati alla mappa degli occupati, e le bitmap si
     * riscrivono DA QUELLA MAPPA. Invertire i due passi vorrebbe dire scrivere
     * l'elenco nell'inode 1 e poi dichiarare liberi, nella bitmap, proprio i
     * blocchi appena elencati. */
    if (scansiona && e2_bad_n > 0) {
        printf("\nBlocchi difettosi da segnare\n");
        if (!ripara) {
            printf("  ! %u blocchi non si leggono", e2_bad_n);
            if (e2_bad_in_uso)
                printf(", e %u di questi stanno dentro dei file", e2_bad_in_uso);
            printf("\n  (con -fix provo a salvarli e li segno nell'inode 1,\n");
            printf("   cosi' nessuno ci scrive piu' sopra)\n");
        } else if (e2_incerto) {
            printf("  ! non li segno: %u punti di incertezza in questo volume.\n",
                   e2_incerto);
            printf("    Scrivere un elenco di blocchi partendo da una mappa che\n");
            printf("    non e' fedele vuol dire perdere dei dati veri.\n");
        } else if ((e2_sposta_i_danneggiati(), e2_marca_difettosi()) != 0) {
            printf("  ! l'elenco non si e' potuto scrivere\n");
            problemi++;
        } else {
            corretti++;
        }
    }

    if (e2_somme_ko) {
        printf("\n  ! %u inode con la somma sbagliata: li riferisco e non li\n",
               e2_somme_ko);
        printf("    tocco, puo' essere sbagliato il contenuto. Li vede e2fsck.\n");
        problemi++;
    }

    e2_controlla_bitmap();
    e2_controlla_link();

    free(e2_meta); e2_meta = NULL;
    free(e2_blocchi_usati); free(e2_inode_visti);
    free(e2_inode_dir); free(e2_link_conta); free(e2_blocchi_bad);
    e2_blocchi_usati = NULL; e2_inode_visti = NULL;
    e2_inode_dir = NULL; e2_link_conta = NULL; e2_blocchi_bad = NULL;
}

/* --- Motore FAT: riconoscimento e giro completo ------------------------- */

/* ! Non si guarda la stringa "FAT16   ": e' decorativa. Si controllano i
 * pochi numeri che DEVONO essere coerenti perche' un settore sia un BPB, e
 * la firma. Un volume ext2 non li supera: i suoi primi 1024 byte sono
 * l'area riservata all'avvio, di solito zeri. */
/* =============================================================================
 * FAT: LA SUPERFICIE, E I CLUSTER CHE NON TORNANO
 *
 * ! SI LEGGE UN CLUSTER PER VOLTA, e qui non serve scendere di misura come su
 * ext2: la grana con cui questo formato sa dire «difettoso» E' il cluster, e
 * leggere piu' fine darebbe una precisione che poi non si puo' scrivere da
 * nessuna parte.
 *
 * ! E L'AREA RISERVATA SI GUARDA MA NON SI MARCA. Settore di avvio, tabelle e
 * root di FAT12/16 non appartengono a nessun cluster: se non si leggono e' un
 * guasto piu' grave — non c'e' nessun posto in cui scriverlo, e il volume si
 * salva copiandolo altrove, non marcandolo. Percio' si dice e basta.
 * ============================================================================= */
static unsigned char fat_scan_buf[BLKIO_MAX_SETT * SETT];

static void fat_scansiona_superficie(void)
{
    unsigned int c, tappa = 0, riservati_rotti = 0, lba;
    unsigned int fine_ris = primo_dato;

    printf("\nSuperficie: leggo tutti i %u cluster. Ogni punto e' un mega.\n  ",
           n_cluster);

    for (lba = 0; lba < fine_ris; lba++)
        if (blkread(dev, lba, 1, fat_scan_buf) != 1) riservati_rotti++;

    if (riservati_rotti) {
        printf("\n  ! %u settori dell'area riservata non si leggono "
               "(avvio, tabelle, root)\n", riservati_rotti);
        printf("    Non si possono marcare: non appartengono a nessun cluster.\n  ");
        problemi++;
    }

    for (c = 2; c < n_cluster + 2u; c++) {
        unsigned int s0 = primo_settore_di(c);

        if (blkread(dev, s0, sett_per_clus, fat_scan_buf) != (int)sett_per_clus) {
            fat_bad[c >> 3] |= (unsigned char)(1u << (c & 7));
            fat_bad_n++;
        }

        if ((c * sett_per_clus * SETT) / (1024u * 1024u) > tappa) {
            tappa = (c * sett_per_clus * SETT) / (1024u * 1024u);
            printf(".");
        }
    }

    if (fat_bad_n == 0) printf("\n  = tutti i cluster si leggono\n");
    else {
        printf("\n  ! %u cluster non si leggono (%u ogni diecimila)\n",
               fat_bad_n, (fat_bad_n * 10000u) / n_cluster);
        problemi++;
    }
}

/* Un cluster libero dove spostare, che non sia a sua volta difettoso. */
static unsigned int fat_alloca_cluster(void)
{
    unsigned int c;

    for (c = 2; c < n_cluster + 2u; c++) {
        if ((fat_bad[c >> 3] >> (c & 7)) & 1) continue;
        if (gia_visto(c)) continue;
        if (fat_leggi(c) != 0) continue;
        segna(c);
        return c;
    }
    return 0;
}

static int fat_sposta_uno(const FatSpostare *sp, unsigned int *recuperati,
                          unsigned int *bucati)
{
    unsigned char s[SETT];
    unsigned int  nuovo, succ, t, k;
    int           letto = 0;

    for (t = 0; t < RILETTURE && !letto; t++)
        if (blkread(dev, primo_settore_di(sp->cluster), sett_per_clus,
                    fat_scan_buf) == (int)sett_per_clus) letto = 1;

    if (!letto)
        for (k = 0; k < sett_per_clus * SETT; k++) fat_scan_buf[k] = 0;

    nuovo = fat_alloca_cluster();
    if (nuovo == 0) {
        printf("  ! non c'e' un cluster libero dove spostare il %u\n",
               sp->cluster);
        return -1;
    }

    if (blkwrite(dev, primo_settore_di(nuovo), sett_per_clus, fat_scan_buf)
            != (int)sett_per_clus) {
        printf("  ! il cluster nuovo %u non si scrive\n", nuovo);
        return -1;
    }

    /* ! LA CATENA SI RICUCE PRIMA DI MARCARE, e in quest'ordine: il nuovo
     * punta dove puntava il vecchio, poi chi precede guarda il nuovo. Al
     * contrario, un'interruzione fra i due passi lascerebbe la catena tagliata
     * — e il file perderebbe tutto quel che viene dopo, non solo il cluster
     * rotto. */
    succ = fat_leggi(sp->cluster);
    if (succ == 0xFFFFFFFFu) return -1;
    if (fat_scrivi(nuovo, succ) != 0) return -1;

    if (sp->prec != 0) {
        if (fat_scrivi(sp->prec, nuovo) != 0) return -1;
    } else {
        /* Il primo cluster di un file: il puntatore sta nella sua voce di
         * directory, non nella FAT. */
        if (blkread(dev, sp->dir_lba, 1, s) != 1) return -1;
        s[sp->dir_idx * 32 + 26] = (unsigned char)(nuovo);
        s[sp->dir_idx * 32 + 27] = (unsigned char)(nuovo >> 8);
        s[sp->dir_idx * 32 + 20] = (unsigned char)(nuovo >> 16);
        s[sp->dir_idx * 32 + 21] = (unsigned char)(nuovo >> 24);
        if (blkwrite(dev, sp->dir_lba, 1, s) != 1) return -1;
    }

    if (letto) {
        (*recuperati)++;
        printf("  = cluster %u recuperato alla %u lettura, ora e' il %u\n",
               sp->cluster, t, nuovo);
    } else {
        (*bucati)++;
        printf("  ! cluster %u NON recuperato: %u byte di zeri nel file "
               "(ora e' il %u)\n", sp->cluster, sett_per_clus * SETT, nuovo);
    }
    return 0;
}

static void fat_sposta_i_danneggiati(void)
{
    unsigned int i, recuperati = 0, bucati = 0, falliti = 0;

    if (fat_da_spostare_n == 0) return;

    printf("\nDati sopra i cluster andati: provo a spostarli\n");
    printf("  (fino a %d riletture per cluster: puo' volerci)\n", RILETTURE);

    for (i = 0; i < fat_da_spostare_n; i++)
        if (fat_sposta_uno(&fat_da_spostare[i], &recuperati, &bucati) != 0)
            falliti++;

    printf("  %u recuperati interi, %u con un buco, %u non spostati\n",
           recuperati, bucati, falliti);
    if (recuperati) corretti++;
}

/* ! E ADESSO SI MARCANO, DOPO AVER SPOSTATO. Il valore e' fine_catena - 1 in
 * tutt'e tre i formati. Un cluster marcato difettoso non e' libero e non e' di
 * nessuno: l'allocatore lo salta per sempre, ed e' esattamente cio' che serve. */
static void fat_marca_difettosi(void)
{
    unsigned int c, n = 0;

    for (c = 2; c < n_cluster + 2u; c++) {
        if (!((fat_bad[c >> 3] >> (c & 7)) & 1)) continue;
        if (fat_scrivi(c, fine_catena - 1u) == 0) n++;
    }

    printf("  = %u cluster marcati difettosi (0x%X)\n", n, fine_catena - 1u);
    if (n) corretti++;
}

static int fat_riconosce(void)
{
    unsigned char s[SETT];
    unsigned int  bps, spc, ris, nf;

    if (blkread(dev, 0, 1, s) < 0) return 0;
    if (s[510] != 0x55 || s[511] != 0xAA) return 0;

    bps = le16(s + 11);
    spc = s[13];
    ris = le16(s + 14);
    nf  = s[16];

    if (bps != 512u) return 0;
    if (spc == 0 || (spc & (spc - 1u)) != 0) return 0;
    if (ris == 0) return 0;
    if (nf == 0 || nf > 4u) return 0;
    return 1;
}

static void fat_controlla(void)
{
    visto_byte = (n_cluster + 2u + 7u) / 8u;
    visto = (unsigned char *)calloc(visto_byte, 1);
    fat_bad = scansiona ? (unsigned char *)calloc(visto_byte, 1) : NULL;
    fat_bad_n = fat_bad_in_uso = fat_da_spostare_n = 0;

    if (visto == NULL || (scansiona && fat_bad == NULL)) {
        printf("\n  ! memoria insufficiente per la mappa di %u cluster\n",
               n_cluster);
        problemi++;
        free(visto); free(fat_bad);
        visto = NULL; fat_bad = NULL;
        return;
    }

    controlla_riservate();
    controlla_copie();

    /* ! PRIMA DELLE CATENE, come su ext2 e per la stessa ragione: cosi' il
     * file che sta sopra un cluster andato lo si nomina mentre lo si
     * incontra, invece di tornarci sopra con un secondo giro. */
    if (scansiona) fat_scansiona_superficie();

    printf("\nCatene e directory\n");
    if (tipo == 32) {
        unsigned int q = 0, u = 0;
        if (percorri(root_clus, "/", &q, &u) == 0)
            controlla_dir(root_clus, 0, "", 0);
    } else {
        controlla_dir(0, 0, "", 0);
    }

    controlla_perduti();

    if (scansiona && fat_bad_n > 0) {
        unsigned int c, gia = 0;

        /* ! QUELLI GIA' MARCATI NON SONO UN PROBLEMA, SONO IL RIMEDIO. Senza
         * questo conto, ogni corsa dopo una marcatura riuscita ripeteva «ci
         * sono N cluster da segnare» e chiudeva con «problemi trovati» su un
         * volume a posto: un controllore che grida al lupo insegna a non
         * leggerlo piu'. */
        for (c = 2; c < n_cluster + 2u; c++)
            if (((fat_bad[c >> 3] >> (c & 7)) & 1) &&
                fat_leggi(c) == fine_catena - 1u) gia++;

        printf("\nCluster difettosi\n");
        if (gia) {
            printf("  = %u gia' marcati difettosi: nessuno ci scrive sopra\n", gia);
            problemi--;                  /* li aveva contati la scansione */
        }
        if (gia == fat_bad_n) {
            /* Tutti gia' a posto: non c'e' niente da fare. */
        } else if (!ripara) {
            printf("  ! %u cluster non si leggono e non sono segnati",
                   fat_bad_n - gia);
            if (fat_bad_in_uso)
                printf(", e %u stanno dentro dei file", fat_bad_in_uso);
            printf("\n  (con -fix provo a salvarli e li marco, cosi' nessuno\n");
            printf("   ci scrive piu' sopra)\n");
        } else {
            fat_sposta_i_danneggiati();
            fat_marca_difettosi();
        }
    }

    free(visto); free(fat_bad);
    visto = NULL; fat_bad = NULL;
}

typedef struct {
    const char *nome;
    int  (*riconosce)(void);    /* 1 se il volume e' di questo formato */
    int  (*apri)(void);         /* legge le strutture, 0 se riuscito */
    void (*controlla)(void);    /* il giro completo */
} MotoreFs;

static const MotoreFs g_motori[] = {
    { "FAT12/16/32", fat_riconosce, fat_apri, fat_controlla },
    { "ext2/ext3/ext4", e2_riconosce, e2_apri, e2_controlla },
};
#define N_MOTORI (int)(sizeof(g_motori) / sizeof(g_motori[0]))

int main(int argc, char **argv)
{
    const char     *nome = NULL;
    const MotoreFs *m = NULL;
    int             i;

    for (i = 1; i < argc; i++) {
        /* ! DUE NOMI PER LA STESSA COSA, E NON E' DISORDINE: `-r` c'era e i
         * comandi vecchi continuano a funzionare; `-fix` e' il nome che si
         * ricorda accanto a -badblock, dove non si sta «riparando un
         * filesystem» ma «cercando di salvare dei dati». */
        if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "-fix") == 0)
            ripara = 1;
        else if (strcmp(argv[i], "-badblock") == 0 ||
                 strcmp(argv[i], "-bad") == 0)
            scansiona = 1;
        else nome = argv[i];
    }

    if (nome == NULL) {
        printf("uso: chkdsk <partizione> [-badblock] [-fix]\n\n");
        printf("Controlla un volume e riferisce cosa non torna. Riconosce da\n");
        printf("solo il formato: FAT12/16/32, ext2, ext3 o ext4.\n\n");
        printf("Con -r corregge; senza, non scrive un solo settore.\n\n");
        printf("La partizione dev'essere SMONTATA: sopra un volume montato\n");
        printf("c'e' una cache, e meta' delle modifiche recenti non e'\n");
        printf("ancora sul supporto. Un controllo li' non direbbe niente\n");
        printf("di vero.\n\n");
        printf("  disk                 elenca i dispositivi\n");
        printf("  umount /disk         smonta\n");
        printf("  chkdsk hd0p1            controlla\n");
        printf("  chkdsk -r hd0p1         controlla e corregge\n");
        printf("  chkdsk hd0p1 -badblock       legge anche OGNI blocco e dice\n");
        printf("                               quali non tornano e in quali file\n");
        printf("  chkdsk hd0p1 -badblock -fix  e li segna difettosi, perche'\n");
        printf("                               nessuno ci scriva piu' sopra\n\n");
        printf("! -bad legge tutto il volume: su un disco grande sono ore.\n");
        printf("  Serve quando il disco ha dei settori andati, non prima.\n");
        return 1;
    }

    {
        int k;
        for (k = 0; nome[k] && k < 62; k++) dev[k] = nome[k];
        dev[k] = '\0';
    }

    printf("chkdsk - controllo di %s%s\n\n", dev,
           ripara ? "  [CORREZIONE ATTIVA]" : "  (sola lettura)");

    /* ! SI SONDA, NON SI CHIEDE. Chi ha un volume malandato spesso non sa
     * che filesystem ci sia sopra — e a volte il volume e' danneggiato
     * PROPRIO nei campi che lo direbbero. Uno strumento che pretende di
     * saperlo in anticipo e' inutile nel momento in cui serve. */
    printf("Riconoscimento\n");
    for (i = 0; i < N_MOTORI; i++) {
        if (g_motori[i].riconosce()) { m = &g_motori[i]; break; }
    }

    if (m == NULL) {
        printf("  ! non riconosco il formato di %s.\n\n", dev);
        printf("Non e' ne' FAT ne' ext2/3/4 - o le strutture che lo direbbero\n");
        printf("sono danneggiate. Se il volume e' MONTATO, smontalo prima:\n");
        printf("i settori grezzi di un volume montato non si leggono.\n");
        return 1;
    }

    printf("  = %s\n\n", m->nome);
    printf("Strutture di testa\n");

    if (m->apri() != 0) {
        printf("\nLe strutture di testa non si leggono: mi fermo qui.\n");
        return 1;
    }

    m->controlla();

    printf("\n");
    if (problemi == 0) {
        printf("Nessun problema trovato.\n");
        return 0;
    }

    printf("%u problem%s trovat%s", problemi,
           (problemi == 1) ? "a" : "i", (problemi == 1) ? "o" : "i");
    if (ripara) printf(", %u corrett%s", corretti, (corretti == 1) ? "o" : "i");
    printf(".\n");

    if (!ripara) {
        printf("\nNiente e' stato scritto. Per correggere: chkdsk -r %s\n", dev);
    } else if (corretti < problemi) {
        printf("\nQualcosa non si e' potuto correggere: rileggi le righe '!'.\n");
    }

    return (problemi == corretti) ? 0 : 1;
}

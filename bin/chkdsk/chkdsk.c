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
 * Controllo e riparazione di un volume FAT12, FAT16 o FAT32.
 *
 *   chkdsk <partizione>          controlla e riferisce, non tocca niente
 *   chkdsk -r <partizione>       controlla e CORREGGE
 *
 * -----------------------------------------------------------------------------
 * ⚠️ LAVORA SOLO SU UNA PARTIZIONE NON MONTATA, e non e' un fastidio da
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
 * ⚠️ NON CORREGGE SE NON GLIELO SI CHIEDE. Senza `-r` non scrive un solo
 * settore. Un controllore che ripara di sua iniziativa e' il modo piu'
 * rapido di trasformare un volume danneggiato in un volume danneggiato
 * DIVERSAMENTE, senza che nessuno abbia visto com'era prima.
 *
 * -----------------------------------------------------------------------------
 * COSA CONTROLLA, e in quest'ordine — ogni passo si fida solo di quelli
 * gia' fatti:
 *
 *   1. il BPB: i numeri devono essere coerenti fra loro, e da loro si
 *      ricava il TIPO (12/16/32). ⚠️ Il tipo NON si legge dall'etichetta
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
 * ⚠️ L'ORDINE DELLE RIPARAZIONI NON E' CASUALE
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

#define SETT            512u
#define MAX_PROF        16      /* annidamento massimo delle directory */
#define TRANCHE         32u     /* settori per blkread: sotto BLKIO_MAX_SETT */

/* --- Il volume, letto una volta e poi usato da tutti --------------------- */
static char         dev[64];
static int          ripara = 0;
static unsigned int problemi = 0, corretti = 0;

static unsigned int sett_per_clus, sett_riservati, n_fat, voci_root;
static unsigned int sett_per_fat, sett_totali, primo_dato, n_cluster;
static unsigned int root_clus;          /* FAT32 */
static unsigned int tipo;               /* 12, 16 o 32 */
static unsigned int fine_catena;        /* primo valore che significa "fine" */

/* Bitmap dei cluster gia' visti, uno per bit: serve a scoprire i condivisi
 * e i perduti. Su un volume da 512 MB con cluster da 4 KB sono 16 KB. */
static unsigned char *visto;
static unsigned int   visto_byte;

/* Un settore della FAT tenuto in mano, piu' quello successivo.
 *
 * ⚠️ I DUE SETTORI SERVONO PER FAT12, e chi li omette scrive un
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

    /* ⚠️ FAT12 A CAVALLO: se la voce comincia nell'ultimo byte del settore
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
            /* ⚠️ I QUATTRO BIT ALTI NON SONO NOSTRI. Su FAT32 una voce usa
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
 * ⚠️ IL TIPO SI RICAVA DAL NUMERO DI CLUSTER, non dalla stringa nel
 * settore di avvio. "FAT16   " a offset 54 e' decorativa: la scrive chi
 * formatta e nessuno la verifica mai, quindi su un volume malandato e'
 * proprio uno dei campi di cui non ci si puo' fidare. I due confini —
 * 4085 e 65525 — sono quelli del formato, non una convenzione locale.
 * ============================================================================= */
static int leggi_bpb(void)
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

    printf("  tipo FAT%u — %u cluster da %u settori, %u FAT da %u settori\n",
           tipo, n_cluster, sett_per_clus, n_fat, sett_per_fat);

    /* ⚠️ La FAT dev'essere abbastanza grande da contenere tutte le voci.
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
 * ⚠️ SI CONFRONTANO, NON SI ALLINEANO ALLA CIECA. Quando differiscono, la
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
 * ⚠️ TRE MODI DI FINIRE MALE, e vanno distinti perche' la riparazione e'
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
 * ⚠️ IL CHECKSUM E' L'UNICA COSA CHE LEGA I DUE PEZZI. Non c'e' nessun
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

/* Stato del giro di UNA directory. ⚠️ Deve vivere piu' a lungo di un
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
 * ⚠️ SONO LE UNICHE VOCI DI CUI SI CONOSCE IN ANTICIPO IL CONTENUTO, e per
 * questo sono le uniche su cui un controllore possa dire qualcosa di
 * definitivo. "." deve puntare alla directory stessa e ".." al padre; un
 * ".." sbagliato non impedisce di leggere i file, ma manda fuori strada
 * chiunque risalga l'albero — compreso un altro controllore.
 *
 * ⚠️ IL ".." DELLA ROOT VALE ZERO ANCHE SU FAT32, dove la root un cluster
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

    /* ⚠️ IL CHECKSUM E' L'UNICO LEGAME fra la fila e questa voce: se non
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
            /* ⚠️ IL ".." CHE PUNTA ALLA ROOT CONTIENE ZERO ANCHE SU FAT32,
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

    /* ⚠️ Una directory con dimensione diversa da zero non e' un errore da
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
        /* Il padre della sottodirectory e' QUESTA directory. ⚠️ Per la root
         * di FAT12/16 vale 0, che e' anche cio' che il formato pretende
         * dentro il suo "..": le due cose combaciano da sole. */
        controlla_dir(primo, g->clus_proprio, pieno, prof + 1);
        return;
    }

    /* ⚠️ LA DIMENSIONE SI CONFRONTA CON UN INTERVALLO, non con un numero.
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
     * ⚠️ I cluster della directory sono gia' stati marcati da percorri()
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

    /* ⚠️ Una fila di frammenti rimasta aperta alla fine della directory non
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
 * ⚠️ SI LIBERANO, NON SI RECUPERANO IN FILE. Il `FOUND.000` di MS-DOS
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

int main(int argc, char **argv)
{
    const char *nome = NULL;
    int         i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) ripara = 1;
        else nome = argv[i];
    }

    if (nome == NULL) {
        printf("uso: chkdsk [-r] <partizione>\n\n");
        printf("Controlla un volume FAT12/16/32 e riferisce cosa non torna.\n");
        printf("Con -r corregge; senza, non scrive un solo settore.\n\n");
        printf("La partizione dev'essere SMONTATA: sopra un volume montato\n");
        printf("c'e' una cache, e meta' delle modifiche recenti non e'\n");
        printf("ancora sul supporto. Un controllo li' non direbbe niente\n");
        printf("di vero.\n\n");
        printf("  disk                 elenca i dispositivi\n");
        printf("  umount /disk         smonta\n");
        printf("  chkdsk hd0p1         controlla\n");
        printf("  chkdsk -r hd0p1      controlla e corregge\n");
        return 1;
    }

    {
        int k;
        for (k = 0; nome[k] && k < 62; k++) dev[k] = nome[k];
        dev[k] = '\0';
    }

    printf("chkdsk — controllo di %s%s\n\n", dev,
           ripara ? "  [CORREZIONE ATTIVA]" : "  (sola lettura)");

    printf("Settore di avvio\n");
    if (leggi_bpb() != 0) {
        printf("\nIl volume non e' leggibile come FAT: mi fermo qui.\n");
        printf("Se e' montato, smontalo: `umount <punto>`.\n");
        return 1;
    }

    visto_byte = (n_cluster + 2u + 7u) / 8u;
    visto = (unsigned char *)calloc(visto_byte, 1);
    if (visto == NULL) {
        printf("\n  ! memoria insufficiente per la mappa di %u cluster\n",
               n_cluster);
        return 1;
    }

    controlla_riservate();
    controlla_copie();

    printf("\nCatene e directory\n");
    if (tipo == 32) {
        unsigned int q = 0, u = 0;
        if (percorri(root_clus, "/", &q, &u) == 0)
            controlla_dir(root_clus, 0, "", 0);
    } else {
        controlla_dir(0, 0, "", 0);
    }

    controlla_perduti();

    printf("\n");
    if (problemi == 0) {
        printf("Nessun problema trovato.\n");
        free(visto);
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
        printf("I cluster condivisi fra due file non si riparano da soli.\n");
    }

    free(visto);
    return (problemi == corretti) ? 0 : 1;
}

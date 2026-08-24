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

/* +0.001 a ogni modifica: `chkdsk -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("chkdsk", "0.001");

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
 * che crediamo di saper leggere — con gli extent di ext4, per dirne una,
 * i_block non contiene numeri di blocco. Un controllore che "ripara" in
 * quelle condizioni scrive spazzatura con la massima convinzione. Si
 * legge, si riferisce, e si dice che non si tocca.
 * ============================================================================= */
#define E2_MAGIC        0xEF53u
#define E2_SUPER_OFF    1024u
#define E2_INCOMPAT_NOTI 0x0002u    /* FILETYPE: l'unico che sappiamo leggere */
#define E2_MODE_TIPO    0xF000u
#define E2_MODE_DIR     0x4000u

static unsigned int e2_dim_blocco, e2_n_inode, e2_n_blocchi;
static unsigned int e2_liberi_b, e2_liberi_i, e2_primo_dato;
static unsigned int e2_b_per_gruppo, e2_i_per_gruppo, e2_dim_inode;
static unsigned int e2_n_gruppi, e2_incompat, e2_primo_inode;
static unsigned int e2_desc_blocco, e2_desc_quanti, e2_sparse;

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

    if (e2_b_per_gruppo == 0 || e2_i_per_gruppo == 0) {
        printf("  ! blocchi o inode per gruppo a zero: superblocco rotto\n");
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
    e2_desc_quanti = (e2_n_gruppi * 32u + e2_dim_blocco - 1u) / e2_dim_blocco;

    /* sparse_super: se c'e', le copie di riserva stanno solo in alcuni
     * gruppi invece che in tutti. Vedi gruppo_ha_backup(). */
    e2_sparse = (rev >= 1u) ? (le32(sb + 100) & 0x0001u) : 0u;

    printf("  ext2 - %u blocchi da %u byte, %u inode, %u grupp%s\n",
           e2_n_blocchi, e2_dim_blocco, e2_n_inode, e2_n_gruppi,
           (e2_n_gruppi == 1) ? "o" : "i");

    if (e2_incompat & ~(unsigned int)E2_INCOMPAT_NOTI) {
        printf("  ! funzionalita' incompatibili 0x%x che non so leggere "
               "(ext3/ext4?)\n", e2_incompat & ~(unsigned int)E2_INCOMPAT_NOTI);
        printf("    Con quei bit attivi i campi che credo di saper leggere\n");
        printf("    vogliono dire altro: riferisco, ma NON tocco niente\n");
        printf("    nemmeno con -r.\n");
        problemi++;
        e2_incerto++;
        ripara = 0;          /* ! si disarma la riparazione, di proposito */
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
    if (!e2_sparse) return 1;
    if (g == 0 || g == 1) return 1;
    return potenza_di(g, 3u) || potenza_di(g, 5u) || potenza_di(g, 7u);
}

/* Legge l'inode `num` (1-based) nel buffer dato. */
static int e2_leggi_inode(unsigned int num, unsigned char *dst)
{
    unsigned int g, idx, tab, off, blocco;
    unsigned char d[4096];

    if (num == 0 || num > e2_n_inode) return -1;

    g   = (num - 1u) / e2_i_per_gruppo;
    idx = (num - 1u) % e2_i_per_gruppo;

    if (e2_leggi_blocco(e2_desc_blocco + (g * 32u) / e2_dim_blocco, d) != 0)
        return -1;
    tab = le32(d + ((g * 32u) % e2_dim_blocco) + 8);

    off    = idx * e2_dim_inode;
    blocco = tab + off / e2_dim_blocco;

    if (e2_leggi_blocco(blocco, d) != 0) return -1;
    {
        unsigned int k, base = off % e2_dim_blocco;
        for (k = 0; k < 128u; k++) dst[k] = d[base + k];
    }
    return 0;
}

/* Marca un blocco come usato, segnalando se lo era gia' o se e' fuori
 * dal volume. */
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

static void e2_percorri_blocchi(const unsigned char *ino_buf, unsigned int ino)
{
    unsigned int i, j, k, punt = e2_dim_blocco / 4u;

    for (i = 0; i < 12u; i++) e2_usa_blocco(le32(ino_buf + 40 + i * 4), ino);

    /* --- indiretto semplice --- */
    {
        unsigned int b = le32(ino_buf + 40 + 12 * 4);
        if (b != 0) {
            e2_usa_blocco(b, ino);
            if (e2_leggi_blocco(b, e2_ind1) == 0)
                for (i = 0; i < punt; i++)
                    e2_usa_blocco(le32(e2_ind1 + i * 4), ino);
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
                    e2_usa_blocco(b2, ino);
                    if (e2_leggi_blocco(b2, e2_ind2) == 0)
                        for (j = 0; j < punt; j++)
                            e2_usa_blocco(le32(e2_ind2 + j * 4), ino);
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

    e2_percorri_blocchi(ib, vino);

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
        return;
    }

    if (e2_leggi_inode(ino, ib) != 0) return;
    dim = le32(ib + 4);

    for (off = 0; off < dim; off += e2_dim_blocco) {
        unsigned int  idx = off / e2_dim_blocco, b, p = 0;
        unsigned char blocco[4096];         /* ! LOCALE: vedi sopra */

        if (idx < 12u)      b = le32(ib + 40 + idx * 4);
        else                break;          /* directory oltre 12 blocchi */
        if (b == 0) continue;
        if (e2_leggi_blocco(b, blocco) != 0) continue;

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
static int e2_ripara_bitmap(void)
{
    unsigned char d[4096], bm[4096], sb[SETT * 2];
    unsigned int  g, i, tot_lib_b = 0, tot_lib_i = 0, scritte = 0;

    for (g = 0; g < e2_n_gruppi; g++) {
        unsigned int off = g * 32u;
        unsigned int dblk = e2_desc_blocco + off / e2_dim_blocco;
        unsigned int dpos = off % e2_dim_blocco;
        unsigned int b_bm, i_bm, n_b, lib_b = 0, lib_i = 0, dirs = 0;

        if (e2_leggi_blocco(dblk, d) != 0) continue;
        b_bm = le32(d + dpos + 0);
        i_bm = le32(d + dpos + 4);
        if (b_bm >= e2_n_blocchi || i_bm >= e2_n_blocchi) continue;

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
        if (e2_scrivi_blocco(b_bm, bm) == 0) scritte++;

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
                if (e2_bit(e2_inode_dir, num)) dirs++;
            } else {
                lib_i++;
            }
        }
        if (e2_scrivi_blocco(i_bm, bm) == 0) scritte++;

        /* --- i conteggi del descrittore --- */
        d[dpos + 12] = (unsigned char)lib_b;
        d[dpos + 13] = (unsigned char)(lib_b >> 8);
        d[dpos + 14] = (unsigned char)lib_i;
        d[dpos + 15] = (unsigned char)(lib_i >> 8);
        d[dpos + 16] = (unsigned char)dirs;
        d[dpos + 17] = (unsigned char)(dirs >> 8);
        e2_scrivi_blocco(dblk, d);

        tot_lib_b += lib_b;
        tot_lib_i += lib_i;
    }

    /* --- i conteggi del superblocco --- */
    if (blkread(dev, E2_SUPER_OFF / SETT, 2, sb) >= 0) {
        sb[12] = (unsigned char)tot_lib_b;
        sb[13] = (unsigned char)(tot_lib_b >> 8);
        sb[14] = (unsigned char)(tot_lib_b >> 16);
        sb[15] = (unsigned char)(tot_lib_b >> 24);
        sb[16] = (unsigned char)tot_lib_i;
        sb[17] = (unsigned char)(tot_lib_i >> 8);
        sb[18] = (unsigned char)(tot_lib_i >> 16);
        sb[19] = (unsigned char)(tot_lib_i >> 24);
        if (blkwrite(dev, E2_SUPER_OFF / SETT, 2, sb) >= 0) {
            printf("  + bitmap e conteggi riscritti (%u bitmap): "
                   "%u blocchi e %u inode liberi\n",
                   scritte, tot_lib_b, tot_lib_i);
            return 0;
        }
    }

    printf("  ! la riscrittura non e' riuscita: il volume e' rimasto com'era\n");
    return -1;
}

/* Confronta le bitmap del volume con quelle appena costruite. */
static void e2_controlla_bitmap(void)
{
    unsigned char d[4096], bm[4096];
    unsigned int  g, i, diff_b = 0, diff_i = 0, usati_veri = 0, ino_veri = 0;
    /* ! QUANTI PROBLEMI ERANO GIA' NOTI PRIMA DI QUESTO GIRO. Una sola
     * riscrittura delle bitmap ne sana PIU' DI UNO — le bitmap dei
     * blocchi, quelle degli inode, i conteggi dei descrittori e quelli del
     * superblocco — e il resoconto deve dirlo. La prima versione contava
     * "1 corretto" su "2 problemi" perche' incrementava corretti una volta
     * sola: sembrava che qualcosa fosse rimasto rotto, e non era vero. */
    unsigned int  prima = problemi;

    printf("\nBitmap dei blocchi e degli inode\n");

    for (g = 0; g < e2_n_gruppi; g++) {
        unsigned int off = g * 32u;
        unsigned int b_bm, i_bm, n_b, n_i;

        if (e2_leggi_blocco(e2_desc_blocco + off / e2_dim_blocco, d) != 0) continue;
        b_bm = le32(d + (off % e2_dim_blocco) + 0);
        i_bm = le32(d + (off % e2_dim_blocco) + 4);

        /* ! La bitmap di un gruppo deve stare DENTRO quel gruppo: un
         * numero fuori intervallo non e' un dettaglio, e' il segno che il
         * descrittore non e' un descrittore. */
        if (b_bm < e2_primo_dato || b_bm >= e2_n_blocchi ||
            i_bm < e2_primo_dato || i_bm >= e2_n_blocchi) {
            printf("  ! gruppo %u: bitmap a %u/%u, fuori dal volume\n",
                   g, b_bm, i_bm);
            problemi++;
            continue;
        }

        n_b = e2_b_per_gruppo;
        if (g == e2_n_gruppi - 1u) {
            unsigned int resto = e2_n_blocchi - e2_primo_dato -
                                 g * e2_b_per_gruppo;
            if (resto < n_b) n_b = resto;
        }
        n_i = e2_i_per_gruppo;

        if (e2_leggi_blocco(b_bm, bm) == 0) {
            for (i = 0; i < n_b; i++) {
                unsigned int blocco = e2_primo_dato + g * e2_b_per_gruppo + i;
                int nel_volume = e2_bit(e2_blocchi_usati, blocco);
                int nella_bm   = e2_bit(bm, i);

                if (nel_volume) usati_veri++;
                if (nel_volume != nella_bm) diff_b++;
            }
        }

        if (e2_leggi_blocco(i_bm, bm) == 0) {
            for (i = 0; i < n_i; i++) {
                unsigned int num = g * e2_i_per_gruppo + i + 1u;
                int visto_qui = e2_bit(e2_inode_visti, num);
                int nella_bm  = e2_bit(bm, i);

                if (visto_qui) ino_veri++;
                /* ! Gli inode riservati (1..primo_inode-1) sono occupati
                 * per definizione e non compaiono in nessuna directory:
                 * confrontarli con "visto" darebbe una differenza su ogni
                 * volume sano. */
                if (num < e2_primo_inode) continue;
                if (visto_qui != nella_bm) diff_i++;
            }
        }
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
    }

    if (problemi == prima) {
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

    if (e2_ripara_bitmap() == 0) corretti += (problemi - prima);
}

/* I conteggi dei collegamenti trovati contro quelli dichiarati. */
static void e2_controlla_link(void)
{
    unsigned char ib[128];
    unsigned int  i, diversi = 0;

    printf("\nConteggi dei collegamenti\n");

    for (i = 1; i <= e2_n_inode; i++) {
        unsigned int dichiarato;

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

    if (e2_blocchi_usati == NULL || e2_inode_visti == NULL ||
        e2_inode_dir == NULL || e2_link_conta == NULL) {
        printf("\n  ! memoria insufficiente per le mappe di %u blocchi e "
               "%u inode\n", e2_n_blocchi, e2_n_inode);
        problemi++;
        free(e2_blocchi_usati); free(e2_inode_visti);
        free(e2_inode_dir); free(e2_link_conta);
        return;
    }

    /* I metadati sono occupati per definizione: superblocco, descrittori,
     * bitmap e tabelle degli inode. Non appartengono a nessun file, e
     * senza marcarli risulterebbero tutti "liberi ma occupati". */
    {
        unsigned char d[4096];
        unsigned int  g, b;

        for (b = 0; b < e2_primo_dato + 1u && b < e2_n_blocchi; b++)
            e2_segna(e2_blocchi_usati, b);

        /* I descrittori PRIMARI: dimenticarli e' la meta' del falso
         * allarme descritto sopra. */
        for (b = 0; b < e2_desc_quanti; b++)
            e2_usa_blocco(e2_desc_blocco + b, 0);

        for (g = 0; g < e2_n_gruppi; g++) {
            unsigned int off = g * 32u, tab, k;

            /* ...e le copie di riserva, dove ci sono. */
            if (g > 0 && gruppo_ha_backup(g)) {
                unsigned int base = e2_primo_dato + g * e2_b_per_gruppo;
                for (k = 0; k < e2_desc_quanti + 1u; k++)
                    e2_usa_blocco(base + k, 0);
            }

            if (e2_leggi_blocco(e2_desc_blocco + off / e2_dim_blocco, d) != 0)
                continue;
            e2_usa_blocco(le32(d + (off % e2_dim_blocco) + 0), 0);
            e2_usa_blocco(le32(d + (off % e2_dim_blocco) + 4), 0);
            tab = le32(d + (off % e2_dim_blocco) + 8);
            for (k = 0; k < (e2_i_per_gruppo * e2_dim_inode + e2_dim_blocco - 1u)
                             / e2_dim_blocco; k++)
                e2_usa_blocco(tab + k, 0);
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

    e2_controlla_bitmap();
    e2_controlla_link();

    free(e2_blocchi_usati); free(e2_inode_visti);
    free(e2_inode_dir); free(e2_link_conta);
    e2_blocchi_usati = NULL; e2_inode_visti = NULL;
    e2_inode_dir = NULL; e2_link_conta = NULL;
}

/* --- Motore FAT: riconoscimento e giro completo ------------------------- */

/* ! Non si guarda la stringa "FAT16   ": e' decorativa. Si controllano i
 * pochi numeri che DEVONO essere coerenti perche' un settore sia un BPB, e
 * la firma. Un volume ext2 non li supera: i suoi primi 1024 byte sono
 * l'area riservata all'avvio, di solito zeri. */
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
    if (visto == NULL) {
        printf("\n  ! memoria insufficiente per la mappa di %u cluster\n",
               n_cluster);
        problemi++;
        return;
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
    free(visto);
    visto = NULL;
}

typedef struct {
    const char *nome;
    int  (*riconosce)(void);    /* 1 se il volume e' di questo formato */
    int  (*apri)(void);         /* legge le strutture, 0 se riuscito */
    void (*controlla)(void);    /* il giro completo */
} MotoreFs;

static const MotoreFs g_motori[] = {
    { "FAT12/16/32", fat_riconosce, fat_apri, fat_controlla },
    { "ext2",        e2_riconosce,  e2_apri,  e2_controlla  },
};
#define N_MOTORI (int)(sizeof(g_motori) / sizeof(g_motori[0]))

int main(int argc, char **argv)
{
    const char     *nome = NULL;
    const MotoreFs *m = NULL;
    int             i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) ripara = 1;
        else nome = argv[i];
    }

    if (nome == NULL) {
        printf("uso: chkdsk [-r] <partizione>\n\n");
        printf("Controlla un volume e riferisce cosa non torna. Riconosce da\n");
        printf("solo il formato: FAT12/16/32 oppure ext2.\n\n");
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
        printf("Non e' ne' FAT ne' ext2 - o le strutture che lo direbbero\n");
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

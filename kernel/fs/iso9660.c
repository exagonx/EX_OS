/* =============================================================================
 * kernel/fs/iso9660.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Vedi kernel/include/iso9660.h per cos'e' ISO 9660 e perche' e' in sola
 * lettura. Qui ci sono le trappole del formato, che sono poche ma tutte
 * silenziose: nessuna da' un errore, tutte danno dati sbagliati.
 *
 *  1. I NUMERI SONO SCRITTI DUE VOLTE, una in little e una in big endian,
 *     uno di seguito all'altro ("both-endian"). Un campo a 32 bit occupa
 *     8 byte. Chi legge la coppia come un unico intero a 64 bit ottiene
 *     numeri assurdi; chi sbaglia di quattro byte l'offset del campo
 *     successivo legge la meta' big endian del precedente. Qui si legge
 *     SEMPRE la meta' little, che e' la prima.
 *
 *  2. IL PRIMO BLOCCO DEI DATI NON E' `extent`, e' `extent +
 *     ext_attr_len`. Il record degli attributi estesi e' quasi sempre
 *     assente (lunghezza 0) ed e' per questo che l'errore sopravvive: si
 *     manifesta solo sui dischi che lo usano, cioe' raramente e senza
 *     preavviso.
 *
 *  3. UN RECORD DI DIRECTORY NON ATTRAVERSA MAI IL CONFINE DI UN BLOCCO.
 *     Quando nel blocco non ci sta piu' un record intero, il resto e'
 *     riempito di zeri e il record successivo comincia dal blocco dopo.
 *     Un byte di lunghezza a zero significa quindi "salta al prossimo
 *     blocco", NON "fine della directory": trattarlo come fine tronca
 *     l'elenco al primo blocco pieno, e su una directory con molti file
 *     spariscono i file dopo il primo 2 KB.
 *
 *  4. IL NOME PORTA UN NUMERO DI VERSIONE (`;1`) e spesso un punto
 *     finale su un nome senza estensione (`DIR.`). Sono parte del
 *     formato, non del nome: lasciarli significa che `cat leggimi.txt`
 *     non trova `LEGGIMI.TXT;1`.
 *
 *  5. LE VOCI "." E ".." NON HANNO UN NOME: hanno lunghezza 1 e come
 *     unico byte 0x00 e 0x01. Non sono caratteri stampabili e non vanno
 *     mostrate — e ".." confrontato con un byte 0x01 non combacia mai con
 *     due punti, quindi va riconosciuto da quel byte e non dal testo.
 *
 * -----------------------------------------------------------------------
 * PERCHE' JOLIET VINCE SU ISO QUANDO C'E'
 *
 * Un disco masterizzato con nomi lunghi contiene DUE alberi completi: la
 * struttura ISO, con i nomi troncati a 8.3 maiuscoli, e quella Joliet, con
 * i nomi veri. Non sono due viste dello stesso albero: sono due catene di
 * directory separate che puntano agli STESSI dati.
 *
 * Montare la prima e' quello che fa un lettore che non conosce Joliet, ed
 * e' anche il modo di far vedere all'utente `LEGGIM~1.TXT` al posto di
 * `Leggimi importante.txt`. Si sceglie la seconda quando c'e', e si dice
 * quale si e' scelta.
 * ============================================================================= */

#include "kernel.h"
#include "iso9660.h"
#include "blk.h"

#define ISO_BLOCCO          2048u
#define ISO_SETT_PER_BLOCCO (ISO_BLOCCO / 512u)

/* Il primo descrittore di volume sta al blocco 16: i primi 16 (32 KB)
 * sono l'area di sistema, che ISO 9660 non guarda affatto — e' li' che i
 * dischi avviabili mettono il proprio settore di avvio. */
#define PRIMO_DESCRITTORE   16u
#define MAX_DESCRITTORI     32u      /* tetto: una catena rotta deve finire */

#define VD_PRIMARIO         1u
#define VD_SUPPLEMENTARE    2u
#define VD_TERMINATORE      255u

/* Bit del campo flag di un record di directory. */
#define REC_NASCOSTO        0x01
#define REC_DIRECTORY       0x02
#define REC_ASSOCIATO       0x04
#define REC_MULTI_EXTENT    0x80

typedef struct {
    uint8_t  usato;
    int      blkdev;

    uint32_t volume_blocchi;    /* dimensione dichiarata del volume */
    uint32_t root_extent;       /* primo blocco della radice (gia' sommato) */
    uint32_t root_dim;

    uint8_t  joliet;            /* 1 = i nomi sono UCS-2 */
    char     etichetta[33];
} IsoMount;

static IsoMount g_mnt[ISO_MAX_MOUNT];

/* =============================================================================
 * I DUE BUFFER, e perche' non uno solo
 *
 * Stessa ragione dei cinque di kernel/fs/ext2.c: le syscall di EX-OS non
 * si annidano, quindi i buffer possono essere globali, ma un buffer solo
 * si perderebbe fra funzioni che si chiamano a vicenda. Qui i generi di
 * blocco sono due — record di directory e dati di file — e la lettura di
 * un file non deve poter sovrascrivere la directory che si sta scorrendo.
 * ============================================================================= */
static uint8_t b_dir [ISO_BLOCCO];
static uint8_t b_dati[ISO_BLOCCO];

/* =============================================================================
 * Letture non allineate
 *
 * Dei campi both-endian si legge SEMPRE la meta' little, che e' la prima.
 * Vedi la trappola 1 in testa al file.
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

static IsoMount *prendi(int mnt)
{
    if (mnt < 0 || mnt >= ISO_MAX_MOUNT) return NULL;
    if (!g_mnt[mnt].usato) return NULL;
    return &g_mnt[mnt];
}

static int leggi_blocco(IsoMount *m, uint32_t blocco, uint8_t *dst)
{
    if (m->volume_blocchi != 0 && blocco >= m->volume_blocchi) {
        klog(LOG_ERROR, "ISO: blocco %u fuori dal volume (%u)",
             blocco, m->volume_blocchi);
        return -1;
    }
    return blk_read(m->blkdev, (uint64_t)blocco * ISO_SETT_PER_BLOCCO,
                    ISO_SETT_PER_BLOCCO, dst);
}

/* =============================================================================
 * Nomi
 * ============================================================================= */
static char minuscolo(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Confronto insensibile alle maiuscole. Vedi iso9660.h: i nomi ISO sono
 * maiuscoli per obbligo di formato e vengono mostrati in minuscolo, quindi
 * cercare per il nome mostrato deve funzionare. */
static int nome_uguale(const char *a, const char *b)
{
    while (*a && *b) {
        if (minuscolo(*a) != minuscolo(*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* Copia il nome di un record nella forma che vede l'utente.
 *
 * Ritorna 0 se il record e' "." o "..", che non sono contenuto e non
 * vanno mai mostrati (trappola 5).
 *
 * `joliet` cambia solo l'alfabeto: i nomi UCS-2 sono BIG endian, due byte
 * per carattere. I caratteri fuori dal latino di base diventano '?': non
 * c'e' un modo migliore su una console in code page 437, e inventare una
 * traslitterazione darebbe nomi che non riaprono il file. */
static int nome_da_record(const uint8_t *rec, int joliet, char *out)
{
    uint32_t len = rec[32];
    uint32_t i, j = 0;

    if (len == 0) return 0;

    /* "." e ".." — un byte, e non e' un punto: e' 0x00 o 0x01. */
    if (len == 1u && (rec[33] == 0x00 || rec[33] == 0x01)) return 0;

    if (joliet) {
        for (i = 0; i + 1u < len && j < ISO_NOME_MAX - 1u; i += 2u) {
            uint16_t c = (uint16_t)(((uint16_t)rec[33 + i] << 8) | rec[34 + i]);

            if (c == 0) break;
            out[j++] = (c < 0x80u) ? (char)c : '?';
        }
    } else {
        for (i = 0; i < len && j < ISO_NOME_MAX - 1u; i++) {
            out[j++] = minuscolo((char)rec[33 + i]);
        }
    }

    out[j] = '\0';

    /* Il numero di versione e il punto finale sono formato, non nome
     * (trappola 4). Si tolgono in quest'ordine: `DIR.;1` esiste. */
    for (i = 0; i < j; i++) {
        if (out[i] == ';') { out[i] = '\0'; j = i; break; }
    }
    if (j > 1u && out[j - 1u] == '.') out[--j] = '\0';

    return (j > 0) ? 1 : 0;
}

/* =============================================================================
 * Scorrimento di una directory
 *
 * `extent`/`dim` descrivono la directory come un file qualunque, perche'
 * e' esattamente cio' che e'. Il ciclo salta al blocco successivo quando
 * incontra un record di lunghezza zero: e' riempimento, non fine
 * (trappola 3).
 *
 * `cerca` != NULL  -> si ferma sul nome richiesto e riempie `out`
 * `cerca` == NULL  -> elenca, saltando le prime `start` voci
 *
 * Le due modalita' stanno nella stessa funzione perche' devono
 * interpretare i record allo stesso modo: due cicli separati sono due
 * occasioni di trattare diversamente lo stesso disco, e la prima
 * differenza sarebbe un file che `ls` mostra e `cat` non apre.
 * ============================================================================= */
static int dir_scorri(IsoMount *m, uint32_t extent, uint32_t dim,
                      const char *cerca, IsoDirEntry *out, uint32_t max,
                      uint32_t start)
{
    uint32_t letti = 0, visti = 0, scritti = 0;

    while (letti < dim) {
        uint32_t p = 0;

        if (leggi_blocco(m, extent + letti / ISO_BLOCCO, b_dir) != 0) return -1;

        while (p + 33u <= ISO_BLOCCO) {
            const uint8_t *rec = b_dir + p;
            uint32_t       len = rec[0];
            char           nome[ISO_NOME_MAX];
            IsoDirEntry    voce;

            /* Riempimento fino a fine blocco: si passa al successivo. */
            if (len == 0) break;

            /* Un record che uscirebbe dal blocco e' un volume malformato:
             * proseguire significherebbe interpretare come record dei byte
             * del blocco seguente. */
            if (len < 33u || p + len > ISO_BLOCCO) break;
            if (33u + (uint32_t)rec[32] > len)     break;

            p += len;

            /* I file "associati" sono metadati di un altro file (la
             * risorsa dei Mac): mostrarli darebbe due voci per lo stesso
             * contenuto. */
            if (rec[25] & REC_ASSOCIATO) continue;

            if (!nome_da_record(rec, m->joliet, nome)) continue;

            /* =========================================================
             * Data e ora del record: sette byte a partire dall'offset 18,
             * subito dopo la lunghezza dei dati e prima dei flag.
             *
             *   18 anni dal 1900   19 mese   20 giorno
             *   21 ore   22 minuti   23 secondi   24 fuso (quarti d'ora)
             *
             * ! IL FUSO SI IGNORA DI PROPOSITO. EX-OS non sa in che fuso
             * si trova — localtime() e gmtime() fanno la stessa cosa, e lo
             * dice il commento in libc.h — quindi spostare l'ora di
             * qualche quarto d'ora non la renderebbe piu' giusta, la
             * renderebbe diversa da quella che il CD dichiara.
             *
             * ! SI CONTROLLA CHE LA DATA SIA PLAUSIBILE. Sono byte scritti
             * da chi ha masterizzato il disco, e un mese 0 o 13 e' comune
             * sui CD prodotti male: la data va a zero, che significa «non
             * la so», invece di finire nel formato FAT come un valore che
             * sembra vero.
             * ========================================================= */
            {
                uint32_t anno   = 1900u + rec[18];
                uint32_t mese   = rec[19];
                uint32_t giorno = rec[20];
                uint32_t ore    = rec[21];
                uint32_t minuti = rec[22];
                uint32_t sec    = rec[23];

                voce.data = 0;
                voce.ora  = 0;

                if (mese >= 1u && mese <= 12u && giorno >= 1u && giorno <= 31u &&
                    ore < 24u && minuti < 60u && sec < 60u &&
                    anno >= 1980u && anno <= 2107u) {
                    voce.data = (uint16_t)(((anno - 1980u) << 9) |
                                           (mese << 5) | giorno);
                    voce.ora  = (uint16_t)((ore << 11) | (minuti << 5) |
                                           (sec / 2u));
                }
            }

            voce.is_dir     = (rec[25] & REC_DIRECTORY) ? 1 : 0;
            voce.dimensione = le32(rec + 10);
            /* Trappola 2: i dati cominciano DOPO gli attributi estesi. */
            voce.extent     = le32(rec + 2) + (uint32_t)rec[1];

            {
                uint32_t k = 0;
                while (k < ISO_NOME_MAX - 1u && nome[k]) {
                    voce.nome[k] = nome[k];
                    k++;
                }
                voce.nome[k] = '\0';
            }

            /* Un file spezzato in piu' extent avrebbe qui solo il primo
             * pezzo, e leggerlo darebbe un file troncato SENZA errore. Si
             * dice e si salta: capita solo oltre i 4 GB per file. */
            if (rec[25] & REC_MULTI_EXTENT) {
                klog(LOG_WARN, "ISO: '%s' e' in piu' extent: non gestito",
                     voce.nome);
                continue;
            }

            if (cerca != NULL) {
                if (nome_uguale(voce.nome, cerca)) {
                    if (out) *out = voce;
                    return 1;
                }
                continue;
            }

            if (visti++ < start) continue;

            out[scritti++] = voce;
            if (scritti >= max) return (int)scritti;
        }

        letti += ISO_BLOCCO;
    }

    return (cerca != NULL) ? 0 : (int)scritti;
}

/* =============================================================================
 * Risoluzione di un percorso
 *
 * Ritorna 1 e riempie `out` con la voce trovata, 0 se non esiste, <0 su
 * errore di lettura. La radice non ha una voce di directory che la
 * descriva — sta nel PVD — e viene sintetizzata qui.
 * ============================================================================= */
static int risolvi(IsoMount *m, const char *percorso, IsoDirEntry *out)
{
    uint32_t extent = m->root_extent;
    uint32_t dim    = m->root_dim;
    uint32_t i      = 0;

    if (percorso == NULL || percorso[0] != '/') return 0;

    out->extent     = extent;
    out->dimensione = dim;
    out->is_dir     = 1;
    out->nome[0]    = '/';
    out->nome[1]    = '\0';

    i = 1;
    while (percorso[i] != '\0') {
        char     seg[ISO_NOME_MAX];
        uint32_t j = 0;
        int      r;

        while (percorso[i] == '/') i++;
        if (percorso[i] == '\0') break;

        while (percorso[i] != '\0' && percorso[i] != '/') {
            if (j < ISO_NOME_MAX - 1u) seg[j++] = percorso[i];
            i++;
        }
        seg[j] = '\0';

        /* Un segmento dentro un file, non dentro una directory: e'
         * "/leggimi.txt/altro", che non esiste e non e' un errore di
         * lettura. */
        if (!out->is_dir) return 0;

        r = dir_scorri(m, out->extent, out->dimensione, seg, out, 1, 0);
        if (r <= 0) return r;
    }

    return 1;
}

/* =============================================================================
 * Montaggio
 * ============================================================================= */
static void copia_etichetta(const uint8_t *src, int joliet, char *out)
{
    int i, n = 0;

    if (joliet) {
        /* L'etichetta di una SVD Joliet e' UCS-2: 16 caratteri in 32 byte. */
        for (i = 0; i < 32; i += 2) {
            uint16_t c = (uint16_t)(((uint16_t)src[i] << 8) | src[i + 1]);
            out[n++] = (c != 0 && c < 0x80u) ? (char)c : (char)(c ? '?' : ' ');
        }
    } else {
        for (i = 0; i < 32; i++) out[n++] = (char)src[i];
    }

    out[n] = '\0';

    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\0')) out[--n] = '\0';

    /* Un descrittore letto da un disco qualunque puo' contenere byte non
     * stampabili: si ripuliscono invece di sporcare lo schermo. */
    for (i = 0; out[i]; i++) {
        if ((unsigned char)out[i] < 32 || (unsigned char)out[i] > 126) out[i] = '?';
    }
}

/* Una sequenza di escape Joliet e' '%' '/' seguito da '@', 'C' o 'E' —
 * i tre livelli di UCS-2 che la specifica ammette. Qualunque altra SVD
 * descrive un altro alfabeto e non la si sa leggere. */
static int e_joliet(const uint8_t *esc)
{
    return esc[0] == 0x25 && esc[1] == 0x2F &&
           (esc[2] == 0x40 || esc[2] == 0x43 || esc[2] == 0x45);
}

int iso_mount(int blkdev)
{
    IsoMount *m    = NULL;
    int       slot = -1;
    uint32_t  i;
    int       trovato_pvd = 0;

    for (i = 0; i < ISO_MAX_MOUNT; i++)
        if (!g_mnt[i].usato) { slot = (int)i; break; }

    if (slot < 0) {
        klog(LOG_ERROR, "ISO: nessun montaggio libero");
        return -1;
    }

    m = &g_mnt[slot];
    m->blkdev         = blkdev;
    m->volume_blocchi = 0;      /* non ancora noto: leggi_blocco non limita */
    m->joliet         = 0;
    m->etichetta[0]   = '\0';

    /* --- La catena dei descrittori di volume ---
     *
     * Si scorre tutta anche dopo aver trovato il PVD: la SVD Joliet viene
     * DOPO, e fermarsi al primo descrittore utile significherebbe non
     * vedere mai i nomi lunghi. Il terminatore chiude; il tetto sui giri
     * esiste perche' un disco corrotto non deve poter far scorrere il
     * kernel fino alla fine del supporto. */
    for (i = 0; i < MAX_DESCRITTORI; i++) {
        uint8_t *d = b_dir;

        if (blk_read(blkdev, (uint64_t)(PRIMO_DESCRITTORE + i) * ISO_SETT_PER_BLOCCO,
                     ISO_SETT_PER_BLOCCO, d) != 0) {
            if (!trovato_pvd) return -1;
            break;
        }

        if (d[1] != 'C' || d[2] != 'D' || d[3] != '0' ||
            d[4] != '0' || d[5] != '1') {
            /* Nessuna firma: se non abbiamo ancora un PVD, non e' un ISO. */
            if (!trovato_pvd) return -1;
            break;
        }

        if (d[0] == VD_TERMINATORE) break;

        if (d[0] == VD_PRIMARIO && !trovato_pvd) {
            uint32_t dim_blocco = le16(d + 128);

            /* Un blocco logico diverso da 2048 esiste sulla carta ma non
             * su un CD dati. Rifiutare e' meglio che leggere tutto con la
             * geometria sbagliata, che non darebbe errori: darebbe file
             * pieni dei byte di qualcun altro. */
            if (dim_blocco != ISO_BLOCCO) {
                klog(LOG_ERROR, "ISO: blocchi logici da %u byte: non gestito",
                     dim_blocco);
                return -1;
            }

            m->volume_blocchi = le32(d + 80);
            m->root_extent    = le32(d + 156 + 2) + (uint32_t)d[156 + 1];
            m->root_dim       = le32(d + 156 + 10);
            copia_etichetta(d + 40, 0, m->etichetta);
            trovato_pvd = 1;
            continue;
        }

        if (d[0] == VD_SUPPLEMENTARE && e_joliet(d + 88)) {
            m->joliet      = 1;
            m->root_extent = le32(d + 156 + 2) + (uint32_t)d[156 + 1];
            m->root_dim    = le32(d + 156 + 10);
            copia_etichetta(d + 40, 1, m->etichetta);
            /* Non si esce: se dopo arrivasse un terminatore va consumato,
             * e comunque il PVD e' gia' stato letto. */
        }
    }

    if (!trovato_pvd) return -1;

    if (m->root_dim == 0 || m->root_extent == 0) {
        klog(LOG_ERROR, "ISO: directory radice vuota o assente");
        return -1;
    }

    m->usato = 1;

    klog(LOG_INFO, "ISO: volume '%s' montato (%u blocchi, nomi %s)",
         m->etichetta, m->volume_blocchi, m->joliet ? "Joliet" : "ISO 9660");

    return slot;
}

int iso_umount(int mnt)
{
    IsoMount *m = prendi(mnt);

    if (m == NULL) return -1;
    m->usato = 0;
    return 0;
}

const char *iso_etichetta(int mnt)
{
    IsoMount *m = prendi(mnt);
    return (m != NULL) ? m->etichetta : "";
}

uint32_t iso_blocchi(int mnt)
{
    IsoMount *m = prendi(mnt);
    return (m != NULL) ? m->volume_blocchi : 0;
}

int iso_joliet(int mnt)
{
    IsoMount *m = prendi(mnt);
    return (m != NULL) ? m->joliet : 0;
}

/* =============================================================================
 * Operazioni
 * ============================================================================= */
int iso_stat(int mnt, const char *percorso, IsoDirEntry *out)
{
    IsoMount *m = prendi(mnt);

    if (m == NULL || out == NULL) return -1;
    return (risolvi(m, percorso, out) == 1) ? 0 : -1;
}

int iso_readdir(int mnt, const char *percorso, IsoDirEntry *out,
                uint32_t max, uint32_t start)
{
    IsoMount   *m = prendi(mnt);
    IsoDirEntry voce;

    if (m == NULL || out == NULL || max == 0) return -1;

    if (risolvi(m, percorso, &voce) != 1) return -1;
    if (!voce.is_dir) return -1;

    return dir_scorri(m, voce.extent, voce.dimensione, NULL, out, max, start);
}

int iso_read(int mnt, const char *percorso, void *buf,
             uint32_t size, uint32_t offset)
{
    IsoMount   *m = prendi(mnt);
    IsoDirEntry voce;
    uint8_t    *p = (uint8_t *)buf;
    uint32_t    fatti = 0;

    if (m == NULL || buf == NULL) return -1;
    if (risolvi(m, percorso, &voce) != 1) return -1;
    if (voce.is_dir) return -1;

    if (offset >= voce.dimensione) return 0;
    if (size > voce.dimensione - offset) size = voce.dimensione - offset;

    /* Un file ISO e' CONTIGUO: niente catene, niente blocchi indiretti.
     * Il blocco che contiene un offset e' una divisione. */
    while (fatti < size) {
        uint32_t blocco = voce.extent + (offset + fatti) / ISO_BLOCCO;
        uint32_t dentro = (offset + fatti) % ISO_BLOCCO;
        uint32_t quanti = ISO_BLOCCO - dentro;
        uint32_t k;

        if (quanti > size - fatti) quanti = size - fatti;

        if (leggi_blocco(m, blocco, b_dati) != 0) return -1;

        for (k = 0; k < quanti; k++) p[fatti + k] = b_dati[dentro + k];

        fatti += quanti;
    }

    return (int)fatti;
}

/* =============================================================================
 * kernel/fs/fat.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Driver FAT12/16/32 guidato dal BPB, sopra il livello a blocchi.
 *
 * Le scelte qui sotto non sono state prese "come fanno tutti": sono
 * dedotte, e ognuna e' motivata. Diverse nascono da bug documentati di
 * implementazioni reali, che vale la pena non ripetere.
 *
 * -----------------------------------------------------------------------
 * 1. LA FAT NON STA IN RAM. NON E' UNA PREFERENZA, E' UN VINCOLO.
 *
 * kernel/fs/fat12.c tiene l'intera FAT in un array statico: 4608 byte per
 * un floppy. La FAT di un volume FAT32 misura 4 byte per cluster: una
 * partizione da 1 GB con cluster da 4 KB ne ha ~262000, cioe' 1 MB di
 * FAT; una da 64 GB arriva a ~64 MB. L'heap del kernel e' 4 MB. Non e'
 * "meno efficiente" tenerla in RAM: e' impossibile.
 *
 * Da qui discende tutto il resto: le voci della FAT si leggono a richiesta
 * da una cache di settori, e questo obbliga ad affrontare il punto 2.
 *
 * -----------------------------------------------------------------------
 * 2. LA VOCE FAT12 PUO' ATTRAVERSARE IL CONFINE FRA DUE SETTORI.
 *
 * E' il classico documentato. Le voci FAT12 sono a 12 bit, quindi due
 * voci stanno in tre byte e la voce N comincia al byte N + N/2. Quando
 * quell'offset e' l'ULTIMO byte del settore, il nibble alto della voce
 * vive nel settore SUCCESSIVO. Un driver che legge un settore e fa
 * `buf[off] | buf[off+1] << 8` legge fuori dal proprio buffer, e ottiene
 * un numero di cluster arbitrario.
 *
 * Due procedure possibili:
 *   (a) leggere sempre DUE settori consecutivi;
 *   (b) leggere un settore, riconoscere il caso a cavallo e leggere il
 *       successivo solo per quel byte.
 *
 * (b) e' migliore, e non per abitudine: su settori da 512 byte solo 1
 * offset su 512 sta a cavallo, quindi (a) raddoppierebbe l'I/O per il
 * 99,8% degli accessi che non ne hanno bisogno. A parita' di correttezza,
 * si sceglie quella che non paga sempre un costo che serve quasi mai. Con
 * la cache dei settori, per giunta, la lettura in piu' di (b) e' quasi
 * sempre un colpo in cache. Qui si usa (b).
 *
 * -----------------------------------------------------------------------
 * 3. FINE CATENA: ESSERE "PRUDENTI" QUI SIGNIFICA TRONCARE I FILE.
 *
 * Nel 1986 una implementazione molto diffusa comincio' a trattare il
 * valore 0xFF0 come fine catena, per uniformita' con il descrittore di
 * media 0xF0. Ma 0xFF0 era un numero di cluster legittimo: i file scritti
 * prima, la cui catena passava di li', diventarono leggibili solo fino a
 * quel punto. La voce di directory continuava a dichiarare 35821 byte e
 * il file ne restituiva 8192, senza alcun errore.
 * (brutman.com, "The FAT12 File Truncation Bug")
 *
 * Regola dedotta, e la si applica alla lettera:
 *   - fine catena e' ESATTAMENTE >= 0xFF8 / 0xFFF8 / 0x0FFFFFF8;
 *   - il valore "cluster danneggiato" 0xFF7 / 0xFFF7 / 0x0FFFFFF7 e' un
 *     ERRORE, non una fine;
 *   - i valori riservati sotto quella soglia NON sono fine catena.
 *
 * Regola dedotta ancora piu' utile: **se la catena finisce prima di
 * quanto dichiari la dimensione del file, e' un errore da segnalare, non
 * una fine da accettare in silenzio.** Quel bug del 1986 sarebbe stato
 * visibile subito con questo controllo, che costa un confronto.
 *
 * -----------------------------------------------------------------------
 * 4. FAT32 USA 28 BIT: I 4 ALTI NON SONO NOSTRI.
 *
 * In lettura si maschera con 0x0FFFFFFF. In scrittura si PRESERVANO.
 * Motivo logico: quei bit sono riservati, cioe' appartengono a qualcun
 * altro; azzerarli distrugge informazione che non si e' in grado di
 * interpretare. E il costo di preservarli e' nullo, perche' per
 * modificare una voce il settore va letto comunque.
 *
 * -----------------------------------------------------------------------
 * 5. FINE DIRECTORY: 0x00 E 0xE5 NON SONO LA STESSA COSA.
 *
 * 0xE5 = voce libera, si continua. 0x00 = voce libera E nessuna voce
 * allocata dopo, si puo' smettere.
 * Fermarsi su 0xE5 fa sparire ogni file che segue il primo cancellato:
 * errore di CORRETTEZZA. Non fermarsi su 0x00 costa una scansione inutile
 * di voci vuote: errore di PRESTAZIONE. Le due direzioni di sbaglio non
 * sono simmetriche, e la scelta va fatta sapendolo.
 *
 * Inoltre 0x05 come primo byte significa che il primo carattere vero e'
 * 0xE5 (era necessario perche' 0xE5 marca le voci libere). Va sostituito
 * in lettura, o quel file risulta con il nome sbagliato.
 *
 * -----------------------------------------------------------------------
 * 6. I CONFINI DEL TIPO: SEVERI IN LETTURA, PRUDENTI IN SCRITTURA.
 *
 * La regola e' <4085 FAT12, <65525 FAT16, altrimenti FAT32. Ma la
 * documentazione nota che le implementazioni reali non concordano di
 * ±1 sui confini, e che la specifica stessa differisce da altra
 * documentazione dello stesso produttore e dal comportamento dei suoi
 * strumenti (elm-chan.org/docs/fat_e.html).
 *
 * Ne discende un'asimmetria, che vale la pena enunciare:
 *   - LEGGENDO si segue la regola alla lettera. Qualunque scostamento
 *     farebbe interpretare male volumi creati da strumenti conformi.
 *   - FORMATTANDO (in futuro, mkfs) si dovra' EVITARE di produrre volumi
 *     con un numero di cluster vicino ai confini, perche' li' gli altri
 *     sistemi non sono d'accordo fra loro e il volume risulterebbe
 *     leggibile da alcuni e non da altri.
 *
 * -----------------------------------------------------------------------
 * 7. LA FIRMA STA A 510, NON "IN FONDO AL SETTORE".
 *
 * Coincidono solo con settori da 512 byte. Scrivere "in fondo" e' un bug
 * latente che si manifesta solo su supporti a settore grande.
 * ============================================================================= */

#include "kernel.h"
#include "fat.h"
#include "blk.h"

/* =============================================================================
 * Cache dei settori
 *
 * Condivisa fra i montaggi e indicizzata da (dispositivo, settore): un
 * solo insieme di slot da spartirsi e' meglio di N cache separate che si
 * dividono la memoria a priori senza sapere chi ne avra' bisogno.
 * ============================================================================= */
#define FAT_CACHE_SLOT   24

typedef struct {
    int      dev;        /* -1 = slot libero */
    uint32_t lba;
    uint32_t uso;        /* per LRU */
    uint8_t  sporco;     /* modificato e non ancora riversato */
    uint8_t  dati[512];
} CacheSettore;

static CacheSettore g_cache[FAT_CACHE_SLOT];
static uint32_t     g_uso = 0;

static void cache_init(void)
{
    int i;
    for (i = 0; i < FAT_CACHE_SLOT; i++) {
        g_cache[i].dev    = -1;
        g_cache[i].sporco = 0;
    }
}

/* Riversa uno slot. Ritorna 0 anche se non c'era niente da riversare. */
static int slot_riversa(int i)
{
    if (g_cache[i].dev < 0 || !g_cache[i].sporco) return 0;

    if (blk_write(g_cache[i].dev, g_cache[i].lba, 1, g_cache[i].dati) != 0) {
        klog(LOG_ERROR, "FAT: scrittura del settore %u fallita", g_cache[i].lba);
        return -1;
    }
    g_cache[i].sporco = 0;
    return 0;
}

/* Sceglie lo slot da riusare e ci porta dentro il settore richiesto.
 * Ritorna l'indice dello slot, o -1.
 *
 * ! LO SFRATTO DI UNO SLOT SPORCO DEVE RIVERSARLO PRIMA.
 * Finche' la cache era di sola lettura, sfrattare significava
 * sovrascrivere. Da quando si scrive, sovrascrivere uno slot sporco
 * BUTTA VIA una modifica gia' accettata dal chiamante — e la butta via in
 * silenzio, perche' nessuno sta piu' guardando quel settore. E' il modo
 * piu' diretto di perdere una voce di FAT a meta' di una catena. */
static int cache_slot(int dev, uint32_t lba)
{
    int i, vittima = -1;
    uint32_t piu_vecchio = 0xFFFFFFFFu;

    for (i = 0; i < FAT_CACHE_SLOT; i++) {
        if (g_cache[i].dev == dev && g_cache[i].lba == lba) {
            g_cache[i].uso = ++g_uso;
            return i;
        }
    }

    for (i = 0; i < FAT_CACHE_SLOT; i++) {
        if (g_cache[i].dev < 0) { vittima = i; break; }
        if (g_cache[i].uso < piu_vecchio) {
            piu_vecchio = g_cache[i].uso;
            vittima = i;
        }
    }
    if (vittima < 0) return -1;

    if (slot_riversa(vittima) != 0) return -1;

    if (blk_read(dev, lba, 1, g_cache[vittima].dati) != 0) {
        g_cache[vittima].dev    = -1;   /* contenuto non attendibile */
        g_cache[vittima].sporco = 0;
        return -1;
    }

    g_cache[vittima].dev    = dev;
    g_cache[vittima].lba    = lba;
    g_cache[vittima].sporco = 0;
    g_cache[vittima].uso    = ++g_uso;
    return vittima;
}

/* Ritorna un puntatore ai dati del settore, o NULL su errore di lettura.
 * Il puntatore resta valido finche' non si chiede un altro settore. */
static const uint8_t *settore(int dev, uint32_t lba)
{
    int i = cache_slot(dev, lba);
    return (i < 0) ? NULL : g_cache[i].dati;
}

/* Come settore(), ma il chiamante ha intenzione di MODIFICARE: lo slot
 * viene marcato sporco subito, non dopo.
 *
 * Marcarlo dopo la modifica sembra equivalente e non lo e': fra le due
 * cose il chiamante puo' chiedere un altro settore, quello sfratta
 * proprio questo slot, e la modifica sparisce prima di essere registrata
 * come tale. Marcandolo prima, lo sfratto la riversa. */
static uint8_t *settore_mut(int dev, uint32_t lba)
{
    int i = cache_slot(dev, lba);
    if (i < 0) return NULL;
    g_cache[i].sporco = 1;
    return g_cache[i].dati;
}

/* Porta in cache un settore AZZERATO senza leggerlo dal supporto.
 * Serve ai cluster appena allocati: leggerli prima di sovrascriverli
 * sarebbe un giro di I/O per dati che vengono buttati, e su una directory
 * nuova il contenuto vecchio sembrerebbe una fila di voci valide. */
static uint8_t *settore_azzera(int dev, uint32_t lba)
{
    int i, vittima = -1, k;
    uint32_t piu_vecchio = 0xFFFFFFFFu;

    for (i = 0; i < FAT_CACHE_SLOT; i++) {
        if (g_cache[i].dev == dev && g_cache[i].lba == lba) { vittima = i; break; }
    }

    if (vittima < 0) {
        for (i = 0; i < FAT_CACHE_SLOT; i++) {
            if (g_cache[i].dev < 0) { vittima = i; break; }
            if (g_cache[i].uso < piu_vecchio) {
                piu_vecchio = g_cache[i].uso;
                vittima = i;
            }
        }
        if (vittima < 0) return NULL;
        if (slot_riversa(vittima) != 0) return NULL;
    }

    for (k = 0; k < 512; k++) g_cache[vittima].dati[k] = 0;

    g_cache[vittima].dev    = dev;
    g_cache[vittima].lba    = lba;
    g_cache[vittima].sporco = 1;
    g_cache[vittima].uso    = ++g_uso;
    return g_cache[vittima].dati;
}

/* Riversa tutto cio' che e' sporco di un dispositivo. */
static int cache_riversa(int dev)
{
    int i, esito = 0;

    for (i = 0; i < FAT_CACHE_SLOT; i++) {
        if (g_cache[i].dev == dev && slot_riversa(i) != 0) esito = -1;
    }

    if (blk_flush(dev) != 0) esito = -1;
    return esito;
}

/* Invalida ogni slot di un dispositivo: serve allo smontaggio, altrimenti
 * un montaggio successivo dello stesso indice vedrebbe i settori del
 * volume precedente.
 *
 * Riversa PRIMA di invalidare: buttare via modifiche non scritte allo
 * smontaggio significherebbe che `umount` corrompe il volume. */
static void cache_invalida(int dev)
{
    int i;

    cache_riversa(dev);
    for (i = 0; i < FAT_CACHE_SLOT; i++) {
        if (g_cache[i].dev == dev) { g_cache[i].dev = -1; g_cache[i].sporco = 0; }
    }
}

/* =============================================================================
 * Stato di un montaggio
 * ============================================================================= */
typedef struct {
    uint8_t  usato;
    int      dev;
    uint32_t tipo;            /* 12, 16, 32 */

    uint32_t byts_per_sec;
    uint32_t sec_per_clus;
    uint32_t n_fat;

    uint32_t fat_inizio;      /* settore relativo della prima FAT */
    uint32_t fat_sett;        /* settori di UNA FAT */
    uint32_t fat_attiva;      /* indice della FAT da leggere */

    uint32_t root_inizio;     /* FAT12/16: settore relativo della root */
    uint32_t root_sett;       /* FAT12/16 */
    uint32_t root_clus;       /* FAT32 */

    uint32_t dati_inizio;     /* primo settore dell'area dati */
    uint32_t n_cluster;       /* cluster dell'area dati */

    /* --- scrittura --- */
    uint32_t mirroring;       /* 1 = aggiorna TUTTE le FAT, 0 = solo l'attiva */
    uint32_t prossimo_libero; /* suggerimento per la ricerca di un cluster */
    uint32_t fsinfo_lba;      /* FAT32: settore FSInfo, 0 = assente */

    char     etichetta[12];
} FatMount;

static FatMount g_mnt[FAT_MAX_MOUNT];
static int      g_init = 0;

/* --- letture non allineate --- */
static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* =============================================================================
 * Valori speciali, per tipo.
 *
 * Vedi il punto 3 in testa al file: questi confronti sono il punto in cui
 * un errore diventa "file troncato senza errore".
 * ============================================================================= */
static uint32_t eoc_min(uint32_t tipo)
{
    if (tipo == 12) return 0x0FF8;
    if (tipo == 16) return 0xFFF8;
    return 0x0FFFFFF8u;
}

static uint32_t bad_clus(uint32_t tipo)
{
    if (tipo == 12) return 0x0FF7;
    if (tipo == 16) return 0xFFF7;
    return 0x0FFFFFF7u;
}

/* =============================================================================
 * fat_voce — legge la voce `clus` della FAT
 *
 * Ritorna il valore, oppure 0xFFFFFFFF se non e' stato possibile leggere
 * (che e' distinguibile da qualunque valore valido perche' i 4 bit alti
 * sono sempre azzerati anche su FAT32).
 *
 * Qui vive il caso a cavallo del settore descritto al punto 2.
 * ============================================================================= */
#define FAT_ERR   0xFFFFFFFFu

static uint32_t fat_voce(const FatMount *m, uint32_t clus)
{
    uint32_t off, sett, dentro;
    const uint8_t *s;

    if (clus < 2 || clus >= m->n_cluster + 2) return FAT_ERR;

    if (m->tipo == 12) {
        off = clus + (clus / 2);          /* due voci in tre byte */
    } else if (m->tipo == 16) {
        off = clus * 2;
    } else {
        off = clus * 4;
    }

    sett   = m->fat_inizio + m->fat_attiva * m->fat_sett + off / m->byts_per_sec;
    dentro = off % m->byts_per_sec;

    s = settore(m->dev, sett);
    if (s == NULL) return FAT_ERR;

    if (m->tipo == 16) return le16(s + dentro);
    if (m->tipo == 32) return le32(s + dentro) & 0x0FFFFFFFu;

    /* --- FAT12 ---
     * Il secondo byte puo' stare nel settore successivo. Leggerlo dal
     * buffer corrente significherebbe prendere un byte che non appartiene
     * a questa voce — e ottenere un numero di cluster arbitrario. */
    {
        uint8_t  b0 = s[dentro];
        uint8_t  b1;

        if (dentro + 1 < m->byts_per_sec) {
            b1 = s[dentro + 1];
        } else {
            const uint8_t *s2 = settore(m->dev, sett + 1);
            if (s2 == NULL) return FAT_ERR;
            b1 = s2[0];
            /* Attenzione: `s` ora puo' essere stato sfrattato dalla cache
             * per fare posto a `s2`. b0 e' gia' stato copiato, quindi non
             * si rilegge nulla da `s` dopo questo punto. */
        }

        {
            uint16_t v = (uint16_t)b0 | ((uint16_t)b1 << 8);
            return (clus & 1) ? (uint32_t)(v >> 4) : (uint32_t)(v & 0x0FFF);
        }
    }
}

/* Primo settore di un cluster dati. */
static uint32_t clus_lba(const FatMount *m, uint32_t clus)
{
    return m->dati_inizio + (clus - 2) * m->sec_per_clus;
}

/* =============================================================================
 * SCRITTURA — fat_voce_scrivi
 *
 * -----------------------------------------------------------------------
 * 8. LE COPIE DELLA FAT VANNO AGGIORNATE TUTTE.
 *
 * Un volume ha quasi sempre due FAT. Scriverne una sola non produce alcun
 * errore visibile: il volume continua a funzionare qui, perche' qui si
 * legge sempre la stessa copia. Il danno si vede altrove — un chkdsk, o
 * un altro sistema che legge la copia 1, trova due mappe di allocazione
 * che non concordano, e a quel punto NON si puo' piu' sapere quale sia
 * quella giusta.
 *
 * L'unica eccezione e' il mirroring disattivato su FAT32 (BPB_ExtFlags
 * bit 7), dove la specifica dice che l'attiva e' UNA sola e le altre non
 * vanno toccate.
 *
 * -----------------------------------------------------------------------
 * 9. SU FAT32 I 4 BIT ALTI SI PRESERVANO (punto 4, in scrittura).
 *
 * Sono riservati: appartengono a qualcun altro. Azzerarli distrugge
 * informazione che non si e' in grado di interpretare, e non costa nulla
 * conservarli perche' per modificare la voce il settore va letto comunque.
 * ============================================================================= */
static int fat_voce_scrivi(const FatMount *m, uint32_t clus, uint32_t val)
{
    uint32_t off, copia;

    if (clus < 2 || clus >= m->n_cluster + 2) return -1;

    if (m->tipo == 12)      off = clus + (clus / 2);
    else if (m->tipo == 16) off = clus * 2;
    else                    off = clus * 4;

    for (copia = 0; copia < m->n_fat; copia++) {
        uint32_t sett, dentro;
        uint8_t *s;

        if (!m->mirroring && copia != m->fat_attiva) continue;

        sett   = m->fat_inizio + copia * m->fat_sett + off / m->byts_per_sec;
        dentro = off % m->byts_per_sec;

        s = settore_mut(m->dev, sett);
        if (s == NULL) return -1;

        if (m->tipo == 16) {
            s[dentro]     = (uint8_t)(val & 0xFF);
            s[dentro + 1] = (uint8_t)((val >> 8) & 0xFF);
            continue;
        }

        if (m->tipo == 32) {
            uint32_t nuovo = (le32(s + dentro) & 0xF0000000u)
                           | (val & 0x0FFFFFFFu);
            s[dentro]     = (uint8_t)(nuovo);
            s[dentro + 1] = (uint8_t)(nuovo >> 8);
            s[dentro + 2] = (uint8_t)(nuovo >> 16);
            s[dentro + 3] = (uint8_t)(nuovo >> 24);
            continue;
        }

        /* --- FAT12 ---
         * Due voci in tre byte: metà del byte condiviso appartiene alla
         * voce vicina e va conservata. Sbagliare qui non corrompe il file
         * che si sta scrivendo: corrompe QUELLO ACCANTO.
         *
         * Il secondo byte puo' stare nel settore successivo (punto 2). Il
         * primo settore e' gia' marcato sporco, quindi se chiedere il
         * secondo lo sfratta, la modifica viene riversata invece che
         * persa — ma il puntatore `s` non e' piu' valido, e infatti da
         * qui in poi non lo si usa. */
        {
            uint32_t v12 = val & 0x0FFF;

            if (clus & 1) {
                s[dentro] = (uint8_t)((s[dentro] & 0x0F) | ((v12 << 4) & 0xF0));
            } else {
                s[dentro] = (uint8_t)(v12 & 0xFF);
            }

            if (dentro + 1 < m->byts_per_sec) {
                if (clus & 1) s[dentro + 1] = (uint8_t)(v12 >> 4);
                else          s[dentro + 1] = (uint8_t)((s[dentro + 1] & 0xF0)
                                                       | ((v12 >> 8) & 0x0F));
            } else {
                uint8_t *s2 = settore_mut(m->dev, sett + 1);
                if (s2 == NULL) return -1;
                if (clus & 1) s2[0] = (uint8_t)(v12 >> 4);
                else          s2[0] = (uint8_t)((s2[0] & 0xF0)
                                               | ((v12 >> 8) & 0x0F));
            }
        }
    }

    return 0;
}

/* Il valore di fine catena da SCRIVERE. In lettura la fine e' un
 * intervallo (>= eoc_min); in scrittura si sceglie un valore solo, il
 * canonico: tutti i bit del formato a uno. */
static uint32_t eoc_scrittura(uint32_t tipo)
{
    if (tipo == 12) return 0x0FFF;
    if (tipo == 16) return 0xFFFF;
    return 0x0FFFFFFFu;
}

/* =============================================================================
 * clus_alloca — trova un cluster libero, lo marca come fine catena e lo
 *               restituisce. Ritorna 0 se il volume e' pieno.
 *
 * ! LO MARCA SUBITO, PRIMA DI RESTITUIRLO. Non e' un dettaglio: fra
 * "l'ho trovato libero" e "qualcuno lo aggancia alla sua catena" il
 * cluster risulterebbe ancora libero, e una seconda allocazione — la
 * successiva richiesta dello stesso chiamante, per un file che ne vuole
 * due — lo assegnerebbe una seconda volta. Due file che condividono un
 * cluster e' esattamente il danno che chkdsk chiama "cross-linked", e non
 * e' riparabile senza perdere dati.
 *
 * La ricerca parte dal suggerimento e gira in tondo: ripartire sempre da 2
 * renderebbe ogni allocazione tanto piu' lenta quanto piu' pieno e' il
 * volume, e su una FAT32 da 275000 cluster significa rileggere megabyte di
 * FAT per ogni cluster di ogni file.
 * ============================================================================= */
static uint32_t clus_alloca(FatMount *m)
{
    uint32_t tot = m->n_cluster, i, partenza = m->prossimo_libero;

    if (partenza < 2 || partenza >= tot + 2) partenza = 2;

    for (i = 0; i < tot; i++) {
        uint32_t c = partenza + i;
        uint32_t v;

        if (c >= tot + 2) c -= tot;

        v = fat_voce(m, c);
        if (v == FAT_ERR) return 0;
        if (v != 0) continue;

        if (fat_voce_scrivi(m, c, eoc_scrittura(m->tipo)) != 0) return 0;

        m->prossimo_libero = (c + 1 >= tot + 2) ? 2 : c + 1;
        return c;
    }

    klog(LOG_ERROR, "FAT: volume pieno, nessun cluster libero");
    return 0;
}

/* Azzera tutti i settori di un cluster. Obbligatorio per una directory
 * nuova: il contenuto precedente sembrerebbe una fila di voci valide. */
static int clus_azzera(const FatMount *m, uint32_t clus)
{
    uint32_t base = clus_lba(m, clus), s;

    for (s = 0; s < m->sec_per_clus; s++) {
        if (settore_azzera(m->dev, base + s) == NULL) return -1;
    }
    return 0;
}

/* Libera una catena a partire da `primo`. */
static int catena_libera(FatMount *m, uint32_t primo)
{
    uint32_t c = primo, passi = 0;

    while (c >= 2 && c < m->n_cluster + 2) {
        uint32_t p = fat_voce(m, c);

        if (p == FAT_ERR) return -1;
        if (fat_voce_scrivi(m, c, 0) != 0) return -1;
        if (c < m->prossimo_libero) m->prossimo_libero = c;

        if (p >= eoc_min(m->tipo)) break;          /* fine regolare */
        if (p == bad_clus(m->tipo)) {
            klog(LOG_ERROR, "FAT: cluster danneggiato nella catena da liberare");
            return -1;
        }

        c = p;

        /* Una FAT corrotta puo' descrivere una catena CICLICA. Senza
         * questo controllo il kernel ci girerebbe dentro per sempre, in
         * ring0, per colpa dei metadati di un disco esterno. */
        if (++passi > m->n_cluster) {
            klog(LOG_ERROR, "FAT: catena ciclica, liberazione interrotta");
            return -1;
        }
    }

    return 0;
}

/* Aggiunge un cluster in coda a `ultimo` e lo restituisce.
 *
 * ! L'ORDINE CONTA: il cluster nuovo e' gia' marcato fine catena da
 * clus_alloca PRIMA che lo si agganci. Agganciarlo per primo e marcarlo
 * poi lascerebbe, in caso di interruzione fra le due scritture, una
 * catena che punta a un cluster ancora libero — cioe' un file che
 * cresceva e un'allocazione futura che gli passa sopra. */
static uint32_t catena_estendi(FatMount *m, uint32_t ultimo)
{
    uint32_t nuovo = clus_alloca(m);

    if (nuovo == 0) return 0;

    if (ultimo >= 2 && ultimo < m->n_cluster + 2) {
        if (fat_voce_scrivi(m, ultimo, nuovo) != 0) {
            fat_voce_scrivi(m, nuovo, 0);   /* non lasciarlo occupato a vuoto */
            return 0;
        }
    }

    return nuovo;
}

/* =============================================================================
 * Montaggio
 * ============================================================================= */
int fat_mount(int blkdev)
{
    const uint8_t *s;
    FatMount m;
    int      i, slot = -1;
    uint16_t bps, resvd, root_ent, tot16, fatsz16;
    uint32_t tot32, fatsz32, tot, fatsz, root_dir_sec, meta, dati;
    uint8_t  spc, n_fat;

    if (!g_init) { cache_init(); g_init = 1; }

    for (i = 0; i < FAT_MAX_MOUNT; i++) {
        if (!g_mnt[i].usato) { slot = i; break; }
    }
    if (slot < 0) return -1;

    s = settore(blkdev, 0);
    if (s == NULL) return -1;

    /* La firma sta a 510 SEMPRE, non "in fondo al settore" (punto 7). */
    if (s[510] != 0x55 || s[511] != 0xAA) return -1;

    bps      = le16(s + 11);
    spc      = s[13];
    resvd    = le16(s + 14);
    n_fat    = s[16];
    root_ent = le16(s + 17);
    tot16    = le16(s + 19);
    fatsz16  = le16(s + 22);
    tot32    = le32(s + 32);
    fatsz32  = le32(s + 36);

    /* Questo driver lavora su settori da 512: il livello a blocchi
     * trasferisce settori di quella dimensione. Un volume con settori
     * diversi va rifiutato, non letto male. */
    if (bps != 512)                    return -1;
    if (spc == 0 || (spc & (spc - 1))) return -1;   /* potenza di due */
    if (spc > 128)                     return -1;
    if (resvd == 0)                    return -1;
    if (n_fat == 0 || n_fat > 4)       return -1;

    tot   = (tot16   != 0) ? (uint32_t)tot16   : tot32;
    fatsz = (fatsz16 != 0) ? (uint32_t)fatsz16 : fatsz32;
    if (tot == 0 || fatsz == 0) return -1;

    root_dir_sec = ((uint32_t)root_ent * 32 + (bps - 1)) / bps;
    meta         = resvd + (uint32_t)n_fat * fatsz + root_dir_sec;
    if (meta >= tot) return -1;
    dati = tot - meta;

    m.n_cluster = dati / spc;
    if (m.n_cluster == 0) return -1;

    /* LA REGOLA, alla lettera (punto 6). */
    if (m.n_cluster < 4085)       m.tipo = 12;
    else if (m.n_cluster < 65525) m.tipo = 16;
    else                          m.tipo = 32;

    m.usato        = 1;
    m.dev          = blkdev;
    m.byts_per_sec = bps;
    m.sec_per_clus = spc;
    m.n_fat        = n_fat;
    m.fat_inizio   = resvd;
    m.fat_sett     = fatsz;
    m.fat_attiva   = 0;
    m.root_sett    = root_dir_sec;
    m.root_inizio  = resvd + (uint32_t)n_fat * fatsz;
    m.root_clus    = 0;
    m.dati_inizio  = m.root_inizio + root_dir_sec;

    /* Su FAT12/16 le copie della FAT sono sempre tutte da aggiornare: non
     * esiste un campo che dica altrimenti. */
    m.mirroring       = 1;
    m.prossimo_libero = 2;
    m.fsinfo_lba      = 0;

    if (m.tipo == 32) {
        /* BPB_ExtFlags: bit 7 = mirroring DISATTIVO, bit 0-3 = FAT attiva.
         * Va onorato: leggere (e ora SCRIVERE) la FAT sbagliata su un
         * volume con mirroring disattivo significa usare una copia che il
         * sistema che l'ha creato non aggiorna. */
        uint16_t ext = le16(s + 40);
        if (ext & 0x0080) {
            m.mirroring  = 0;
            m.fat_attiva = ext & 0x000F;
            if (m.fat_attiva >= n_fat) m.fat_attiva = 0;
        }
        m.root_clus = le32(s + 44) & 0x0FFFFFFFu;
        if (m.root_clus < 2 || m.root_clus >= m.n_cluster + 2) return -1;

        /* FSInfo: contiene il numero di cluster liberi e un suggerimento su
         * dove cercarne uno. La specifica dice esplicitamente che sono
         * INDICAZIONI e possono essere sbagliate, quindi qui il valore si
         * usa solo come punto di partenza della ricerca — mai come verita'.
         * Un suggerimento fuori range viene semplicemente ignorato. */
        {
            uint16_t fsi = le16(s + 48);
            if (fsi != 0 && fsi != 0xFFFF) {
                const uint8_t *f;
                m.fsinfo_lba = fsi;
                f = settore(blkdev, fsi);
                if (f && le32(f) == 0x41615252u && le32(f + 484) == 0x61417272u
                      && f[510] == 0x55 && f[511] == 0xAA) {
                    uint32_t hint = le32(f + 492);
                    if (hint >= 2 && hint < m.n_cluster + 2)
                        m.prossimo_libero = hint;
                } else {
                    m.fsinfo_lba = 0;   /* firme sbagliate: non e' un FSInfo */
                }
            }
        }

        for (i = 0; i < 11; i++) m.etichetta[i] = (char)s[71 + i];
    } else {
        if (root_ent == 0) return -1;   /* incoerente col tipo dedotto */
        for (i = 0; i < 11; i++) m.etichetta[i] = (char)s[43 + i];
    }
    m.etichetta[11] = '\0';
    for (i = 10; i >= 0 && (m.etichetta[i] == ' ' || m.etichetta[i] == 0); i--) {
        m.etichetta[i] = '\0';
    }

    g_mnt[slot] = m;

    klog(LOG_INFO, "FAT: montato dispositivo %d: FAT%u, %u cluster da %u settori, '%s'",
         blkdev, m.tipo, m.n_cluster, m.sec_per_clus, m.etichetta);

    return slot;
}

int fat_umount(int mnt)
{
    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return -1;
    cache_invalida(g_mnt[mnt].dev);
    g_mnt[mnt].usato = 0;
    return 0;
}

int fat_tipo(int mnt)
{
    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return -1;
    return (int)g_mnt[mnt].tipo;
}

uint32_t fat_n_cluster(int mnt)
{
    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return 0;
    return g_mnt[mnt].n_cluster;
}

const char *fat_etichetta(int mnt)
{
    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return "";
    return g_mnt[mnt].etichetta;
}

/* =============================================================================
 * Percorrenza di una directory
 *
 * Una directory e' o l'area root fissa (FAT12/16) o una catena di cluster
 * (FAT32, e ogni sottodirectory di qualunque tipo). Le due cose si
 * percorrono diversamente, e questo iteratore nasconde la differenza.
 * ============================================================================= */
/* Dove sta fisicamente una voce di directory: settore e indice dentro il
 * settore. Senza questo si puo' leggere una voce ma non aggiornarla. */
typedef struct {
    uint32_t lba;
    uint32_t idx;
} PosVoce;

typedef struct {
    const FatMount *m;
    uint32_t clus;        /* 0 = area root fissa */
    uint32_t lba;         /* settore corrente */
    uint32_t rimasti;     /* settori rimasti nell'area root fissa */
    uint32_t in_clus;     /* settori gia' consumati del cluster corrente */
    uint32_t idx;         /* indice della voce dentro il settore */
    int      fine;
} DirIter;

static void dir_apri(DirIter *it, const FatMount *m, uint32_t clus)
{
    it->m       = m;
    it->clus    = clus;
    it->idx     = 0;
    it->in_clus = 0;
    it->fine    = 0;

    if (clus == 0) {
        it->lba     = m->root_inizio;
        it->rimasti = m->root_sett;
        if (it->rimasti == 0) it->fine = 1;
    } else {
        it->lba     = clus_lba(m, clus);
        it->rimasti = 0;
    }
}

/* Avanza al settore successivo. Ritorna 0 se c'e', -1 se finita. */
static int dir_prossimo_settore(DirIter *it)
{
    const FatMount *m = it->m;

    if (it->clus == 0) {
        if (it->rimasti <= 1) return -1;
        it->rimasti--;
        it->lba++;
        return 0;
    }

    it->in_clus++;
    if (it->in_clus < m->sec_per_clus) {
        it->lba++;
        return 0;
    }

    {
        uint32_t p = fat_voce(m, it->clus);

        if (p == FAT_ERR) return -1;
        if (p == bad_clus(m->tipo)) {
            klog(LOG_ERROR, "FAT: cluster danneggiato nella catena di una directory");
            return -1;
        }
        if (p >= eoc_min(m->tipo)) return -1;      /* fine, regolare */
        if (p < 2 || p >= m->n_cluster + 2) {
            klog(LOG_ERROR, "FAT: cluster %u fuori range in una directory", p);
            return -1;
        }

        it->clus    = p;
        it->in_clus = 0;
        it->lba     = clus_lba(m, p);
        return 0;
    }
}

/* Legge la prossima voce grezza da 32 byte. Ritorna:
 *   1 = voce restituita in `dst`
 *   0 = directory finita
 *  -1 = errore di lettura                                              */
static int dir_prossima(DirIter *it, uint8_t *dst)
{
    const FatMount *m = it->m;

    while (!it->fine) {
        const uint8_t *s = settore(m->dev, it->lba);
        uint32_t per_sett = m->byts_per_sec / 32;
        uint32_t k;

        if (s == NULL) return -1;

        while (it->idx < per_sett) {
            const uint8_t *v = s + it->idx * 32;

            it->idx++;

            /* 0x00: libera E nessuna voce allocata dopo. Ci si puo'
             * fermare. Vedi il punto 5 in testa al file. */
            if (v[0] == 0x00) { it->fine = 1; return 0; }

            /* 0xE5: libera, ma si CONTINUA. Fermarsi qui farebbe sparire
             * ogni file che segue il primo cancellato. */
            if (v[0] == 0xE5) continue;

            for (k = 0; k < 32; k++) dst[k] = v[k];
            return 1;
        }

        it->idx = 0;
        if (dir_prossimo_settore(it) != 0) { it->fine = 1; return 0; }
    }

    return 0;
}

/* =============================================================================
 * Nomi
 * ============================================================================= */

/* Converte il nome 8.3 grezzo in stringa leggibile. */
static void nome_da_83(const uint8_t *v, char *out)
{
    int i, j = 0;

    for (i = 0; i < 8 && v[i] != ' '; i++) out[j++] = (char)v[i];

    /* 0x05 al primo byte significa che il primo carattere vero e' 0xE5:
     * la sostituzione esiste perche' 0xE5 marca le voci libere. Senza
     * questo, quel file avrebbe il nome sbagliato. */
    if (j > 0 && (uint8_t)out[0] == 0x05) out[0] = (char)0xE5;

    if (v[8] != ' ') {
        out[j++] = '.';
        for (i = 8; i < 11 && v[i] != ' '; i++) out[j++] = (char)v[i];
    }
    out[j] = '\0';
}

static char su_maiuscolo(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

/* Confronta un nome utente con il nome 8.3 di una voce, senza distinguere
 * maiuscole e minuscole. */
/* =============================================================================
 * NOMI LUNGHI (VFAT)
 *
 * Un nome piu' lungo di 8.3 non sta in una voce di directory: sta in una
 * CATENA di voci con attributo 0x0F, ognuna da 13 caratteri UTF-16,
 * scritte PRIMA della voce 8.3 e in ordine INVERSO. La voce con il bit
 * 0x40 nell'ordinale e' l'ultima del nome, cioe' la prima che si incontra
 * leggendo la directory in avanti.
 *
 * Fino ad agosto 2026 questo driver le saltava tutte: un file creato da
 * Windows o da Linux come "appunti di riunione.txt" compariva come
 * APPUNT~1.TXT — e siccome anche la RICERCA usava solo l'8.3, quello era
 * anche l'unico nome con cui si poteva aprire.
 *
 * ! LA SOMMA DI CONTROLLO NON E' FACOLTATIVA. Ogni voce LFN porta la
 * somma del nome 8.3 a cui appartiene. Serve a riconoscere le catene
 * ORFANE: un sistema che non conosce i nomi lunghi puo' cancellare la
 * voce 8.3 lasciando indietro le sue LFN, e attaccare quei frammenti al
 * primo nome 8.3 che capita darebbe a un file il nome di un altro. Se la
 * somma non combacia, il nome lungo si butta e resta l'8.3.
 *
 * ! SOLO LETTURA. Creare un file con un nome lungo vorrebbe dire
 * allocare piu' voci consecutive e inventare un alias 8.3 unico
 * (NOME~1, NOME~2...): e' un'altra cosa, e non c'e'. Un file creato da
 * EX-OS ha un nome 8.3, e si vede.
 *
 * ! SOLO ASCII. I caratteri sopra 0x7F diventano '?'. EX-OS non ha una
 * tabella di caratteri: inventare una traduzione qui vorrebbe dire
 * scegliere una codifica per tutto il sistema, che e' una decisione
 * diversa e piu' grande di questo file.
 * ============================================================================= */
typedef struct {
    char     nome[FAT_NOME_MAX];
    uint8_t  checksum;
    uint8_t  valido;        /* la catena e' completa */
    uint8_t  attesa;        /* prossimo ordinale atteso, 0 = nessuna catena */
} Lfn;

static void lfn_azzera(Lfn *l)
{
    l->nome[0] = '\0';
    l->valido  = 0;
    l->attesa  = 0;
}

/* La somma di controllo del nome 8.3, come la calcola la specifica. */
static uint8_t somma83(const uint8_t *v)
{
    uint8_t s = 0;
    int     i;

    for (i = 0; i < 11; i++)
        s = (uint8_t)(((s & 1u) ? 0x80u : 0u) + (s >> 1) + v[i]);
    return s;
}

static void lfn_raccogli(Lfn *l, const uint8_t *v)
{
    /* Le posizioni dei 13 caratteri dentro la voce: non sono contigui,
     * perche' il layout deve lasciare al loro posto attributo, somma e
     * primo cluster della voce 8.3 che stanno negli stessi offset. */
    static const uint8_t off[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    uint32_t ord = (uint32_t)(v[0] & 0x1Fu);
    uint32_t base, i;

    /* Voce cancellata, o ordinale fuori dai 20 possibili: la catena non
     * vale piu' niente. */
    if (v[0] == 0xE5u || ord == 0u || ord > 20u) { lfn_azzera(l); return; }

    if (v[0] & 0x40u) {                 /* ultima del nome = prima che vediamo */
        for (i = 0; i < FAT_NOME_MAX; i++) l->nome[i] = '\0';
        l->checksum = v[13];
        l->attesa   = (uint8_t)ord;
        l->valido   = 0;
    } else if (l->attesa == 0u || ord != l->attesa || v[13] != l->checksum) {
        /* Frammento che non continua la catena che stavamo leggendo. */
        lfn_azzera(l);
        return;
    }

    base = (ord - 1u) * 13u;
    for (i = 0; i < 13u; i++) {
        uint16_t c = (uint16_t)(v[off[i]] | ((uint16_t)v[off[i] + 1] << 8));

        if (base + i >= FAT_NOME_MAX - 1u) break;
        if (c == 0u || c == 0xFFFFu) break;     /* riempimento di fine nome */
        l->nome[base + i] = (c < 0x80u) ? (char)c : '?';
    }

    l->attesa = (uint8_t)(ord - 1u);
    if (l->attesa == 0u) l->valido = 1;
}

/* Il nome lungo che appartiene a questa voce 8.3, oppure NULL. */
static const char *lfn_per(const Lfn *l, const uint8_t *v83)
{
    if (!l->valido) return NULL;
    if (l->checksum != somma83(v83)) return NULL;   /* catena orfana */
    if (l->nome[0] == '\0') return NULL;
    return l->nome;
}

/* Confronto di due nomi senza distinzione fra maiuscole e minuscole: e' la
 * regola di FAT, e vale anche per i nomi lunghi. */
static int nomi_uguali(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a++, y = *b++;

        if (x >= 'a' && x <= 'z') x = (char)(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z') y = (char)(y - 'a' + 'A');
        if (x != y) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static int nome_uguale(const char *utente, const uint8_t *v)
{
    char n[FAT_NOME_MAX];
    int  i;

    nome_da_83(v, n);

    for (i = 0; ; i++) {
        char a = su_maiuscolo(utente[i]);
        char b = su_maiuscolo(n[i]);
        if (a != b) return 0;
        if (a == '\0') return 1;
    }
}

/* =============================================================================
 * Risoluzione di un percorso
 *
 * Percorre i componenti separati da '/'. Non c'e' un limite di
 * profondita' come nel vecchio FAT12: una sottodirectory e' una catena di
 * cluster come un'altra, quindi non c'e' ragione di fermarsi al primo
 * livello.
 * ============================================================================= */
static int risolvi(const FatMount *m, const char *percorso,
                   uint8_t *voce_out, uint32_t *clus_dir_out,
                   PosVoce *pos_out)
{
    uint32_t clus = 0;              /* 0 = root (FAT12/16) */
    const char *p = percorso;
    uint8_t voce[32];
    int     trovato_qualcosa = 0;
    PosVoce pos = { 0, 0 };

    if (m->tipo == 32) clus = m->root_clus;

    if (clus_dir_out) *clus_dir_out = clus;
    if (pos_out) { pos_out->lba = 0; pos_out->idx = 0; }

    while (*p == '/') p++;

    while (*p) {
        char comp[FAT_NOME_MAX];
        int  n = 0;
        DirIter it;
        Lfn     lfn;
        int     esito;
        int     trovato = 0;

        while (*p && *p != '/' && n < FAT_NOME_MAX - 1) comp[n++] = *p++;
        comp[n] = '\0';
        while (*p == '/') p++;

        if (n == 0) break;

        dir_apri(&it, m, clus);
        lfn_azzera(&lfn);

        while ((esito = dir_prossima(&it, voce)) == 1) {
            const char *lungo;
            int         combacia;

            /* Le voci di nome lungo sono FRAMMENTI, non file: si
             * accumulano, e il nome che ne esce vale per la voce 8.3 che
             * viene subito dopo. */
            if ((voce[11] & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
                lfn_raccogli(&lfn, voce);
                continue;
            }
            if (voce[11] & FAT_ATTR_VOLID) { lfn_azzera(&lfn); continue; }

            /* ! SI CONFRONTA COL NOME LUNGO SE C'E', ALTRIMENTI CON L'8.3.
             * Confrontare solo col lungo renderebbe impossibile aprire un
             * file col suo alias corto, che e' un nome legittimo e che i
             * programmi vecchi usano. */
            lungo    = lfn_per(&lfn, voce);
            combacia = lungo ? nomi_uguali(comp, lungo) : nome_uguale(comp, voce);
            if (!combacia && lungo) combacia = nome_uguale(comp, voce);
            lfn_azzera(&lfn);

            if (combacia) {
                /* dir_prossima ha gia' incrementato l'indice: la voce
                 * appena restituita sta in quella precedente. Serve per
                 * poterla RISCRIVERE (dimensione, primo cluster,
                 * cancellazione) senza ricercarla una seconda volta. */
                pos.lba = it.lba;
                pos.idx = it.idx - 1;
                trovato = 1;
                break;
            }
        }

        if (esito < 0) return -1;
        if (!trovato)  return -1;

        trovato_qualcosa = 1;
        if (pos_out) *pos_out = pos;

        {
            uint32_t primo = (uint32_t)le16(voce + 26);
            if (m->tipo == 32) primo |= ((uint32_t)le16(voce + 20)) << 16;

            if (*p == '\0') {
                if (voce_out) {
                    int k;
                    for (k = 0; k < 32; k++) voce_out[k] = voce[k];
                }
                if (clus_dir_out) *clus_dir_out = primo;
                return 0;
            }

            /* Non e' l'ultimo componente: deve essere una directory. */
            if (!(voce[11] & FAT_ATTR_DIR)) return -1;

            /* ".." della root punta a cluster 0: e' la convenzione, e va
             * riportato alla root vera del volume. */
            clus = primo;
            if (m->tipo == 32 && clus == 0) clus = m->root_clus;
        }
    }

    /* Percorso vuoto o solo "/": e' la root. */
    if (!trovato_qualcosa) {
        if (clus_dir_out) *clus_dir_out = (m->tipo == 32) ? m->root_clus : 0;
        if (voce_out) voce_out[0] = 0;
        return 0;
    }

    return -1;
}

/* =============================================================================
 * API pubblica
 * ============================================================================= */
static void riempi(FatDirEntry *o, const FatMount *m, const uint8_t *v,
                   const char *lungo)
{
    if (lungo != NULL) {
        uint32_t k = 0;

        while (k < FAT_NOME_MAX - 1u && lungo[k]) { o->nome[k] = lungo[k]; k++; }
        o->nome[k] = '\0';
    } else {
        nome_da_83(v, o->nome);
    }
    o->attributi     = v[11];
    o->is_dir        = (v[11] & FAT_ATTR_DIR) ? 1 : 0;
    o->dimensione    = le32(v + 28);
    o->primo_cluster = (uint32_t)le16(v + 26);
    if (m->tipo == 32) o->primo_cluster |= ((uint32_t)le16(v + 20)) << 16;
    /* Ora e data di ultima scrittura: byte 22-23 e 24-25 della voce, gia'
     * nel formato che attraversa la syscall. Qui non c'e' niente da
     * convertire — e' FAT che ha dato il formato a tutti gli altri. */
    o->ora  = le16(v + 22);
    o->data = le16(v + 24);
}

int fat_readdir(int mnt, const char *percorso, FatDirEntry *out,
                uint32_t max, uint32_t start)
{
    FatMount *m;
    uint32_t  clus;
    DirIter   it;
    uint8_t   voce[32];
    Lfn       lfn;
    uint32_t  visti = 0, scritti = 0;
    int       esito;

    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return -1;
    if (out == NULL || max == 0) return -1;
    m = &g_mnt[mnt];

    if (risolvi(m, percorso, NULL, &clus, NULL) != 0) return -1;
    if (m->tipo != 32 && clus == m->root_clus) clus = 0;

    dir_apri(&it, m, clus);

    lfn_azzera(&lfn);

    while (scritti < max && (esito = dir_prossima(&it, voce)) == 1) {
        const char *lungo;

        if ((voce[11] & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
            lfn_raccogli(&lfn, voce);
            continue;
        }
        if (voce[11] & FAT_ATTR_VOLID) { lfn_azzera(&lfn); continue; }

        lungo = lfn_per(&lfn, voce);

        /* ! L'AZZERAMENTO VA FATTO ANCHE QUANDO SI SALTA LA VOCE per la
         * paginazione: altrimenti il nome lungo di una voce saltata
         * resterebbe attaccato alla successiva, che in una directory
         * paginata significa un file con il nome di un altro. */
        if (visti++ < start) { lfn_azzera(&lfn); continue; }

        riempi(&out[scritti], m, voce, lungo);
        lfn_azzera(&lfn);
        scritti++;
    }

    if (esito < 0 && scritti == 0) return -1;
    return (int)scritti;
}

int fat_stat(int mnt, const char *percorso, FatDirEntry *out)
{
    FatMount *m;
    uint8_t   voce[32];

    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return -1;
    m = &g_mnt[mnt];

    if (risolvi(m, percorso, voce, NULL, NULL) != 0) return -1;
    if (voce[0] == 0) return -1;    /* era la root, non un file */

    /* NULL e non il nome lungo: risolvi() non lo riporta indietro, e
     * nessuno lo legge da qui — vfs.c usa fat_stat per dimensione e tipo,
     * il nome ce l'ha gia' chi ha chiesto. Riportarlo vorrebbe dire far
     * tornare a risolvi() un dato che serve a un solo chiamante. */
    if (out) riempi(out, m, voce, NULL);
    return 0;
}

int fat_read(int mnt, const char *percorso, void *buf,
             uint32_t size, uint32_t offset)
{
    FatMount    *m;
    FatDirEntry  e;
    uint32_t     clus, salti, i;
    uint32_t     letti = 0;
    uint8_t     *dst = (uint8_t *)buf;
    uint32_t     clus_byte;

    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return -1;
    if (buf == NULL) return -1;
    m = &g_mnt[mnt];

    if (fat_stat(mnt, percorso, &e) != 0) return -1;
    if (e.is_dir) return -1;

    if (offset >= e.dimensione) return 0;
    if (size > e.dimensione - offset) size = e.dimensione - offset;
    if (size == 0) return 0;

    clus_byte = m->sec_per_clus * m->byts_per_sec;
    clus      = e.primo_cluster;
    salti     = offset / clus_byte;

    /* Salto fino al cluster che contiene l'offset richiesto. */
    for (i = 0; i < salti; i++) {
        uint32_t p;

        if (clus < 2 || clus >= m->n_cluster + 2) return -1;
        p = fat_voce(m, clus);
        if (p == FAT_ERR) return -1;
        if (p == bad_clus(m->tipo)) return -1;

        /* LA CATENA FINISCE PRIMA DI QUANTO DICA LA DIMENSIONE.
         * Questo e' un errore, non una fine: e' esattamente il caso in
         * cui il bug del 1986 restituiva silenziosamente un file
         * troncato. Vedi il punto 3 in testa al file. */
        if (p >= eoc_min(m->tipo)) {
            klog(LOG_ERROR, "FAT: '%s' dichiara %u byte ma la catena finisce prima",
                 percorso, e.dimensione);
            return -1;
        }
        clus = p;
    }

    {
        uint32_t dentro = offset % clus_byte;

        while (letti < size) {
            uint32_t base, s, primo_s, off_s, quanti;

            if (clus < 2 || clus >= m->n_cluster + 2) return -1;

            base    = clus_lba(m, clus);
            primo_s = dentro / m->byts_per_sec;
            off_s   = dentro % m->byts_per_sec;

            for (s = primo_s; s < m->sec_per_clus && letti < size; s++) {
                const uint8_t *sd = settore(m->dev, base + s);
                if (sd == NULL) return -1;

                quanti = m->byts_per_sec - off_s;
                if (quanti > size - letti) quanti = size - letti;

                for (i = 0; i < quanti; i++) dst[letti + i] = sd[off_s + i];

                letti += quanti;
                off_s  = 0;
            }

            if (letti >= size) break;

            {
                uint32_t p = fat_voce(m, clus);
                if (p == FAT_ERR || p == bad_clus(m->tipo)) return -1;
                if (p >= eoc_min(m->tipo)) {
                    klog(LOG_ERROR, "FAT: '%s': catena finita a %u byte su %u attesi",
                         percorso, offset + letti, e.dimensione);
                    return -1;
                }
                clus = p;
            }
            dentro = 0;
        }
    }

    return (int)letti;
}

/* =============================================================================
 * SCRITTURA — API pubblica
 *
 * -----------------------------------------------------------------------
 * 10. L'ORDINE FRA DATI E DIMENSIONE NON E' INDIFFERENTE.
 *
 * Scrivendo si fanno due cose: si mettono i byte nei cluster e si aggiorna
 * la dimensione nella voce di directory. Se l'alimentazione va via in
 * mezzo, l'ordine decide che cosa resta.
 *
 *   dati prima, dimensione dopo -> il file dichiara MENO di quanto e'
 *       stato scritto. Si perdono byte appena scritti: spiacevole, ma il
 *       file e' coerente e leggibile.
 *   dimensione prima, dati dopo -> il file dichiara byte che non sono mai
 *       stati scritti, e restituisce il contenuto PRECEDENTE di quei
 *       cluster: dati di un altro file, a un utente che non doveva
 *       vederli.
 *
 * Le due direzioni di sbaglio non si equivalgono, quindi: prima i dati.
 *
 * -----------------------------------------------------------------------
 * 11. DATA E ORA: NON SI INVENTANO.
 *
 * Il sistema non ha ancora un orologio hardware (nessun driver RTC). Le
 * alternative erano scrivere una data finta plausibile o una costante
 * riconoscibile. Una data finta plausibile e' peggio: renderebbe
 * indistinguibile "non lo sappiamo" da "e' stato scritto allora", e
 * qualunque strumento che ordini per data darebbe un risultato
 * inventato. Si scrive una costante, e quando ci sara' l'RTC bastera'
 * cambiare queste due funzioni.
 * ============================================================================= */

#define FAT_DATA_IGNOTA   0x5C21u   /* 2026-01-01 in formato FAT */
#define FAT_ORA_IGNOTA    0x0000u   /* 00:00:00 */

/* Puntatore modificabile a una voce di directory di cui si conosce la
 * posizione. Vale finche' non si chiede un altro settore. */
static uint8_t *voce_mut(const FatMount *m, const PosVoce *pos)
{
    uint8_t *s = settore_mut(m->dev, pos->lba);
    if (s == NULL) return NULL;
    return s + pos->idx * 32;
}

static void scrivi16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

static void scrivi32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* =============================================================================
 * nome_a_83 — converte "prova.txt" in "PROVA   TXT"
 *
 * ! RIFIUTA invece di storpiare. Un nome troppo lungo o con caratteri
 * vietati potrebbe essere troncato o ripulito in silenzio, e molte
 * implementazioni lo fanno; il risultato e' che l'utente chiede un file e
 * ne ottiene un altro, con un nome che non ha scelto e che non ritrovera'.
 * Meglio un errore.
 *
 * L'elenco dei caratteri vietati non e' arbitrario: sono quelli che hanno
 * un significato nel formato o nei percorsi. 0xE5 come primo byte va
 * sostituito con 0x05 perche' 0xE5 marca le voci libere (punto 5).
 * ============================================================================= */
static int carattere_vietato(char c)
{
    const char *v = "\"*+,/:;<=>?[\\]|";
    int i;

    if ((uint8_t)c < 0x20) return 1;
    for (i = 0; v[i]; i++) if (c == v[i]) return 1;
    return 0;
}

static int nome_a_83(const char *nome, uint8_t out[11])
{
    int i, n = 0, e = 0, punti = 0;
    const char *p = nome, *punto = NULL;

    if (nome == NULL || nome[0] == '\0') return -1;

    /* "." e ".." non sono nomi creabili. */
    if (nome[0] == '.' && (nome[1] == '\0' ||
                          (nome[1] == '.' && nome[2] == '\0'))) return -1;

    for (i = 0; nome[i]; i++) {
        if (nome[i] == '.') { punti++; punto = nome + i; continue; }
        if (carattere_vietato(nome[i])) return -1;
    }
    if (punti > 1) return -1;
    if (i > 12)    return -1;          /* 8 + '.' + 3 */

    for (i = 0; i < 11; i++) out[i] = ' ';

    while (*p && *p != '.') {
        if (n >= 8) return -1;
        out[n++] = (uint8_t)su_maiuscolo(*p++);
    }

    if (punto) {
        p = punto + 1;
        if (*p == '\0') return -1;     /* "nome." non e' un nome */
        while (*p) {
            if (e >= 3) return -1;
            out[8 + e] = (uint8_t)su_maiuscolo(*p++);
            e++;
        }
    }

    if (n == 0) return -1;
    if (out[0] == 0xE5) out[0] = 0x05;
    return 0;
}

/* =============================================================================
 * dir_slot_libero — trova una voce libera nella directory, estendendola se
 *                   serve. Ritorna 0 e riempie `pos`.
 *
 * Percorre le voci GREZZE: dir_prossima() salta quelle libere, che qui
 * sono proprio cio' che si cerca.
 *
 * L'area root fissa di FAT12/16 NON puo' crescere: ha un numero di voci
 * deciso alla formattazione. Restituire "pieno" e' l'unica risposta
 * corretta — estenderla scriverebbe sopra l'inizio dell'area dati.
 * ============================================================================= */
static int dir_slot_libero(FatMount *m, uint32_t clus_dir, PosVoce *pos)
{
    uint32_t per_sett = m->byts_per_sec / 32;
    uint32_t clus = clus_dir;
    uint32_t lba, rimasti, in_clus = 0, passi = 0;

    if (clus == 0) { lba = m->root_inizio; rimasti = m->root_sett; }
    else           { lba = clus_lba(m, clus); rimasti = 0; }

    for (;;) {
        const uint8_t *s = settore(m->dev, lba);
        uint32_t i;

        if (s == NULL) return -1;

        for (i = 0; i < per_sett; i++) {
            if (s[i * 32] == 0x00 || s[i * 32] == 0xE5) {
                pos->lba = lba;
                pos->idx = i;
                return 0;
            }
        }

        /* --- settore successivo --- */
        if (clus == 0) {
            if (rimasti <= 1) {
                klog(LOG_ERROR, "FAT: root fissa piena (%u voci): non puo' crescere",
                     m->root_sett * per_sett);
                return -1;
            }
            rimasti--;
            lba++;
            continue;
        }

        in_clus++;
        if (in_clus < m->sec_per_clus) { lba++; continue; }

        {
            uint32_t p = fat_voce(m, clus);

            if (p == FAT_ERR || p == bad_clus(m->tipo)) return -1;

            if (p >= eoc_min(m->tipo)) {
                /* Fine della catena: la directory va allungata. */
                uint32_t nuovo = catena_estendi(m, clus);
                if (nuovo == 0) return -1;
                if (clus_azzera(m, nuovo) != 0) return -1;
                pos->lba = clus_lba(m, nuovo);
                pos->idx = 0;
                return 0;
            }

            if (p < 2 || p >= m->n_cluster + 2) return -1;
            clus    = p;
            in_clus = 0;
            lba     = clus_lba(m, clus);
        }

        if (++passi > m->n_cluster) {
            klog(LOG_ERROR, "FAT: catena di directory ciclica");
            return -1;
        }
    }
}

/* Divide "/A/B/NOME" in padre "/A/B" e nome "NOME".
 * Ritorna 0, -1 se il percorso non ha un nome finale (e' una radice). */
static int percorso_dividi(const char *percorso, char *padre, uint32_t max_padre,
                           char *nome, uint32_t max_nome)
{
    uint32_t len = 0, taglio = 0, i;

    while (percorso[len]) len++;
    while (len > 1 && percorso[len - 1] == '/') len--;   /* slash finale */
    if (len == 0 || (len == 1 && percorso[0] == '/')) return -1;

    taglio = len;
    while (taglio > 0 && percorso[taglio - 1] != '/') taglio--;

    if (len - taglio == 0 || len - taglio >= max_nome) return -1;
    for (i = taglio; i < len; i++) nome[i - taglio] = percorso[i];
    nome[len - taglio] = '\0';

    if (taglio <= 1) { padre[0] = '/'; padre[1] = '\0'; return 0; }
    if (taglio - 1 >= max_padre) return -1;
    for (i = 0; i < taglio - 1; i++) padre[i] = percorso[i];
    padre[taglio - 1] = '\0';
    return 0;
}

/* Cluster della directory indicata da `percorso`. 0 = root fissa. */
static int dir_di(const FatMount *m, const char *percorso, uint32_t *clus_out)
{
    uint8_t  voce[32];
    uint32_t clus;

    if (percorso[0] == '/' && percorso[1] == '\0') {
        *clus_out = (m->tipo == 32) ? m->root_clus : 0;
        return 0;
    }

    if (risolvi(m, percorso, voce, &clus, NULL) != 0) return -1;
    if (!(voce[11] & FAT_ATTR_DIR)) return -1;

    /* Una sottodirectory con primo cluster 0 e' la root per convenzione. */
    if (clus == 0 && m->tipo == 32) clus = m->root_clus;
    *clus_out = clus;
    return 0;
}

/* =============================================================================
 * voce_nuova — crea una voce di directory. Uso comune a file e directory.
 * ============================================================================= */
static int voce_nuova(FatMount *m, const char *percorso, uint8_t attr,
                      uint32_t primo_clus, PosVoce *pos_out)
{
    char     padre[FAT_PERCORSO_MAX], nome[FAT_NOME_MAX];
    uint8_t  n83[11];
    uint32_t clus_dir;
    PosVoce  pos;
    uint8_t *v;
    FatDirEntry gia;

    if (percorso_dividi(percorso, padre, sizeof(padre),
                        nome, sizeof(nome)) != 0) return -1;
    if (nome_a_83(nome, n83) != 0) {
        klog(LOG_ERROR, "FAT: '%s' non e' un nome 8.3 valido", nome);
        return -1;
    }
    if (dir_di(m, padre, &clus_dir) != 0) return -1;

    /* Un nome che esiste gia' non si sovrascrive in silenzio: due voci
     * con lo stesso nome renderebbero il file precedente irraggiungibile
     * e i suoi cluster non piu' liberabili. */
    if (fat_stat((int)(m - g_mnt), percorso, &gia) == 0) return -2;

    if (dir_slot_libero(m, clus_dir, &pos) != 0) return -1;

    v = voce_mut(m, &pos);
    if (v == NULL) return -1;

    {
        int i;
        for (i = 0; i < 32; i++) v[i] = 0;
        for (i = 0; i < 11; i++) v[i] = n83[i];
    }
    v[11] = attr;
    scrivi16(v + 14, FAT_ORA_IGNOTA);      /* creazione */
    scrivi16(v + 16, FAT_DATA_IGNOTA);
    scrivi16(v + 18, FAT_DATA_IGNOTA);     /* ultimo accesso */
    scrivi16(v + 20, primo_clus >> 16);    /* alto: solo FAT32 */
    scrivi16(v + 22, FAT_ORA_IGNOTA);      /* modifica */
    scrivi16(v + 24, FAT_DATA_IGNOTA);
    scrivi16(v + 26, primo_clus & 0xFFFF);
    scrivi32(v + 28, 0);

    if (pos_out) *pos_out = pos;
    return 0;
}

int fat_create(int mnt, const char *percorso)
{
    FatMount *m;

    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return -1;
    m = &g_mnt[mnt];

    return voce_nuova(m, percorso, FAT_ATTR_ARCHIVE, 0, NULL);
}

int fat_mkdir(int mnt, const char *percorso)
{
    FatMount *m;
    uint32_t  clus_padre, nuovo;
    PosVoce   pos;
    uint8_t  *v;

    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return -1;
    m = &g_mnt[mnt];

    {
        char padre[FAT_PERCORSO_MAX], nome[FAT_NOME_MAX];
        if (percorso_dividi(percorso, padre, sizeof(padre),
                            nome, sizeof(nome)) != 0) return -1;
        if (dir_di(m, padre, &clus_padre) != 0) return -1;
    }

    /* Il cluster della directory va allocato e AZZERATO prima di creare la
     * voce che lo nomina: se la voce esistesse gia' e il contenuto no,
     * chiunque elencasse quella directory leggerebbe il contenuto
     * precedente di quel cluster come se fossero file. */
    nuovo = clus_alloca(m);
    if (nuovo == 0) return -1;
    if (clus_azzera(m, nuovo) != 0) { fat_voce_scrivi(m, nuovo, 0); return -1; }

    /* "." e ".." — la seconda punta al padre, e per la root vale 0 per
     * convenzione anche su FAT32, dove la root ha un cluster vero. */
    v = settore_mut(m->dev, clus_lba(m, nuovo));
    if (v == NULL) { fat_voce_scrivi(m, nuovo, 0); return -1; }
    {
        int i;
        uint32_t padre_clus = clus_padre;

        if (m->tipo == 32 && padre_clus == m->root_clus) padre_clus = 0;

        for (i = 0; i < 11; i++) { v[i] = ' '; v[32 + i] = ' '; }
        v[0] = '.';
        v[32] = '.'; v[33] = '.';
        v[11] = FAT_ATTR_DIR;  v[32 + 11] = FAT_ATTR_DIR;
        scrivi16(v + 20,      nuovo >> 16);
        scrivi16(v + 26,      nuovo & 0xFFFF);
        scrivi16(v + 32 + 20, padre_clus >> 16);
        scrivi16(v + 32 + 26, padre_clus & 0xFFFF);
        scrivi16(v + 22,      FAT_ORA_IGNOTA);
        scrivi16(v + 24,      FAT_DATA_IGNOTA);
        scrivi16(v + 32 + 22, FAT_ORA_IGNOTA);
        scrivi16(v + 32 + 24, FAT_DATA_IGNOTA);
    }

    {
        int r = voce_nuova(m, percorso, FAT_ATTR_DIR, nuovo, &pos);
        if (r != 0) {
            catena_libera(m, nuovo);    /* non lasciare un cluster orfano */
            /* ! IL -2 VA PROPAGATO, e prima si perdeva. voce_nuova
             * distingue "esiste gia'" (-2) da "errore" (-1), e il VFS
             * traduce il primo in EEXIST e il secondo in EIO; schiacciando
             * tutto a -1, una mkdir su una directory che c'e' gia'
             * rispondeva "errore di I/O".
             *
             * Non e' cosmetico: chi installa vede tre righe di errore di
             * I/O su un disco perfettamente sano e crede che il supporto
             * stia cedendo. E' esattamente cio' che faceva `install` a
             * ogni reinstallazione su FAT. */
            return r;
        }
    }

    return 0;
}

/* =============================================================================
 * fat_rename — cambia il NOME di una voce, senza spostare i dati
 *
 * ! SOLO NELLA STESSA DIRECTORY. Attraversare directory vorrebbe dire
 * togliere una voce e aggiungerne un'altra altrove, cioe' un momento in
 * cui il file non e' raggiungibile da nessun nome: senza journal, una
 * caduta di corrente li' in mezzo lo perde. Il caso che serve — e l'unico
 * che si concede — e' rinominare sul posto. Si ritorna -3 per il resto,
 * cosi' il VFS puo' dire ENOSYS invece di fingere un errore generico.
 *
 * ! PERCHE' ESISTE, ed e' il punto: i dati NON SI SPOSTANO. Si riscrivono
 * undici byte dentro la voce di directory e basta. E' cio' che permette a
 * `install` di scrivere il kernel nuovo con un nome temporaneo, verificare
 * che sia mappabile in un solo tratto, e solo allora dargli il nome
 * definitivo — con la certezza che la mappa appena verificata resta
 * valida. La rename() della libc, che copia e cancella, non lo permette:
 * ricopiando rifarebbe l'allocazione e la verifica non varrebbe piu'.
 *
 * ! NON tocca il nome lungo (LFN). Se la voce ne aveva uno, quei
 * frammenti restano e continuano a nominare il file con il nome vecchio.
 * Per i file di sistema — 8.3 puri — non succede; per gli altri e' un
 * limite dichiarato, non un caso da scoprire.
 * ============================================================================= */
int fat_rename(int mnt, const char *da, const char *a)
{
    FatMount   *m;
    char        pad_da[FAT_PERCORSO_MAX], nom_da[FAT_NOME_MAX];
    char        pad_a[FAT_PERCORSO_MAX],  nom_a[FAT_NOME_MAX];
    uint8_t     n83[11], voce[32], *v;
    PosVoce     pos;
    FatDirEntry gia;
    int         i;

    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return -1;
    m = &g_mnt[mnt];

    if (percorso_dividi(da, pad_da, sizeof(pad_da),
                        nom_da, sizeof(nom_da)) != 0) return -1;
    if (percorso_dividi(a,  pad_a,  sizeof(pad_a),
                        nom_a,  sizeof(nom_a))  != 0) return -1;

    /* Stessa directory, confronto byte per byte: i due percorsi arrivano
     * gia' normalizzati dal VFS. */
    for (i = 0; pad_da[i] || pad_a[i]; i++) {
        if (pad_da[i] != pad_a[i]) return -3;
    }

    if (nome_a_83(nom_a, n83) != 0) return -1;

    /* La destinazione non deve esistere: due voci con lo stesso nome
     * renderebbero il file precedente irraggiungibile e i suoi cluster non
     * piu' liberabili. Chi vuole sostituire cancella prima. */
    if (fat_stat(mnt, a, &gia) == 0) return -2;

    if (risolvi(m, da, voce, NULL, &pos) != 0) return -1;
    if (pos.lba == 0) return -1;

    v = voce_mut(m, &pos);
    if (v == NULL) return -1;

    for (i = 0; i < 11; i++) v[i] = n83[i];

    klog(LOG_DEBUG, "FAT: rinominato '%s' in '%s' (voce a LBA %u idx %u)",
         da, a, pos.lba, pos.idx);
    return 0;
}

/* Vera se la directory contiene solo "." e "..". */
static int dir_vuota(const FatMount *m, uint32_t clus)
{
    DirIter it;
    uint8_t voce[32];
    int     esito;

    dir_apri(&it, m, clus);
    while ((esito = dir_prossima(&it, voce)) == 1) {
        if ((voce[11] & FAT_ATTR_LFN) == FAT_ATTR_LFN) continue;
        if (voce[0] == '.' && (voce[1] == ' ' ||
                              (voce[1] == '.' && voce[2] == ' '))) continue;
        return 0;
    }
    return (esito < 0) ? -1 : 1;
}

/* Cancellazione comune a file e directory.
 *
 * ! La catena si libera PRIMA di marcare la voce libera. All'inverso, una
 * interruzione fra le due cose lascerebbe cluster occupati da un file che
 * non esiste piu': spazio perso per sempre, che nessuno sa a chi
 * apparteneva. Nell'ordine scelto, l'interruzione lascia al massimo una
 * voce che punta a cluster liberi — visibile e riparabile. */
static int rimuovi(FatMount *m, const char *percorso, int vuole_dir)
{
    uint8_t  voce[32];
    PosVoce  pos;
    uint32_t primo;
    uint8_t *v;

    if (risolvi(m, percorso, voce, NULL, &pos) != 0) return -1;
    if (pos.lba == 0) return -1;                 /* era una radice */

    if (vuole_dir && !(voce[11] & FAT_ATTR_DIR)) return -1;
    if (!vuole_dir && (voce[11] & FAT_ATTR_DIR)) return -1;
    if (voce[11] & FAT_ATTR_RDONLY) return -1;

    primo = (uint32_t)le16(voce + 26);
    if (m->tipo == 32) primo |= ((uint32_t)le16(voce + 20)) << 16;

    if (vuole_dir) {
        int q = dir_vuota(m, primo);
        if (q < 0) return -1;
        if (q == 0) return -2;                   /* non vuota */
    }

    if (primo >= 2 && primo < m->n_cluster + 2) {
        if (catena_libera(m, primo) != 0) return -1;
    }

    v = voce_mut(m, &pos);
    if (v == NULL) return -1;
    v[0] = 0xE5;

    return 0;
}

int fat_unlink(int mnt, const char *percorso)
{
    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return -1;
    return rimuovi(&g_mnt[mnt], percorso, 0);
}

int fat_rmdir(int mnt, const char *percorso)
{
    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return -1;
    return rimuovi(&g_mnt[mnt], percorso, 1);
}

/* =============================================================================
 * fat_write
 * ============================================================================= */
int fat_write(int mnt, const char *percorso, const void *buf,
              uint32_t size, uint32_t offset)
{
    FatMount    *m;
    uint8_t      voce[32];
    PosVoce      pos;
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t     primo, dim, clus_byte, clus, salti, i;
    uint32_t     scritti = 0, dentro;

    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return -1;
    if (buf == NULL) return -1;
    m = &g_mnt[mnt];

    if (size == 0) return 0;

    if (risolvi(m, percorso, voce, NULL, &pos) != 0) return -1;
    if (pos.lba == 0) return -1;
    if (voce[11] & FAT_ATTR_DIR)    return -1;
    if (voce[11] & FAT_ATTR_RDONLY) return -1;

    dim   = le32(voce + 28);
    primo = (uint32_t)le16(voce + 26);
    if (m->tipo == 32) primo |= ((uint32_t)le16(voce + 20)) << 16;

    clus_byte = m->sec_per_clus * m->byts_per_sec;

    /* Un file vuoto non ha cluster: il primo va allocato ora. */
    if (primo < 2 || primo >= m->n_cluster + 2) {
        primo = clus_alloca(m);
        if (primo == 0) return -1;
    }

    /* Scrivere oltre la fine lascia un buco: i cluster in mezzo esistono
     * ma contengono il loro contenuto precedente, cioe' dati di qualcun
     * altro. Vanno azzerati, non solo allocati. */
    clus  = primo;
    salti = offset / clus_byte;
    for (i = 0; i < salti; i++) {
        uint32_t p = fat_voce(m, clus);

        if (p == FAT_ERR || p == bad_clus(m->tipo)) return -1;
        if (p >= eoc_min(m->tipo)) {
            p = catena_estendi(m, clus);
            if (p == 0) return -1;
            if (offset > dim && clus_azzera(m, p) != 0) return -1;
        }
        clus = p;
    }

    dentro = offset % clus_byte;

    while (scritti < size) {
        uint32_t base, s, primo_s, off_s, quanti;

        if (clus < 2 || clus >= m->n_cluster + 2) return -1;

        base    = clus_lba(m, clus);
        primo_s = dentro / m->byts_per_sec;
        off_s   = dentro % m->byts_per_sec;

        for (s = primo_s; s < m->sec_per_clus && scritti < size; s++) {
            uint8_t *sd;

            quanti = m->byts_per_sec - off_s;
            if (quanti > size - scritti) quanti = size - scritti;

            /* Un settore riscritto per intero non va letto prima: sarebbe
             * un giro di I/O per dati che vengono comunque sostituiti. */
            if (quanti == m->byts_per_sec) sd = settore_azzera(m->dev, base + s);
            else                           sd = settore_mut   (m->dev, base + s);
            if (sd == NULL) return -1;

            for (i = 0; i < quanti; i++) sd[off_s + i] = src[scritti + i];

            scritti += quanti;
            off_s    = 0;
        }

        if (scritti >= size) break;

        {
            uint32_t p = fat_voce(m, clus);

            if (p == FAT_ERR || p == bad_clus(m->tipo)) return -1;
            if (p >= eoc_min(m->tipo)) {
                p = catena_estendi(m, clus);
                if (p == 0) {
                    /* Volume pieno a meta' scrittura: i byte gia' scritti
                     * sono validi, e la dimensione va aggiornata per
                     * quelli. Restituire un errore secco li perderebbe. */
                    break;
                }
            }
            clus = p;
        }
        dentro = 0;
    }

    /* --- I DATI SONO SCRITTI: SOLO ORA LA VOCE (punto 10). --- */
    {
        uint8_t *v = voce_mut(m, &pos);
        if (v == NULL) return -1;

        scrivi16(v + 20, primo >> 16);
        scrivi16(v + 26, primo & 0xFFFF);
        if (offset + scritti > dim) scrivi32(v + 28, offset + scritti);
        scrivi16(v + 22, FAT_ORA_IGNOTA);
        scrivi16(v + 24, FAT_DATA_IGNOTA);
        v[11] |= FAT_ATTR_ARCHIVE;
    }

    return (int)scritti;
}

/* Porta il file a `nuova_dim`. Solo accorciamento: allungare si fa
 * scrivendo, ed e' l'unico modo in cui i byte nuovi hanno un contenuto
 * definito. */
int fat_truncate(int mnt, const char *percorso, uint32_t nuova_dim)
{
    FatMount *m;
    uint8_t   voce[32];
    PosVoce   pos;
    uint32_t  primo, dim, clus_byte, tenuti, clus, i;

    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return -1;
    m = &g_mnt[mnt];

    if (risolvi(m, percorso, voce, NULL, &pos) != 0) return -1;
    if (pos.lba == 0) return -1;
    if (voce[11] & FAT_ATTR_DIR)    return -1;
    if (voce[11] & FAT_ATTR_RDONLY) return -1;

    dim   = le32(voce + 28);
    if (nuova_dim >= dim) return 0;

    primo = (uint32_t)le16(voce + 26);
    if (m->tipo == 32) primo |= ((uint32_t)le16(voce + 20)) << 16;

    clus_byte = m->sec_per_clus * m->byts_per_sec;
    tenuti    = (nuova_dim + clus_byte - 1) / clus_byte;

    if (tenuti == 0) {
        /* Niente da tenere: si libera tutto e il file resta senza cluster,
         * che e' la rappresentazione corretta di un file vuoto. */
        if (primo >= 2 && primo < m->n_cluster + 2) {
            if (catena_libera(m, primo) != 0) return -1;
        }
        primo = 0;
    } else {
        clus = primo;
        for (i = 1; i < tenuti; i++) {
            uint32_t p = fat_voce(m, clus);
            if (p == FAT_ERR || p == bad_clus(m->tipo)) return -1;
            if (p >= eoc_min(m->tipo)) break;    /* gia' piu' corto */
            clus = p;
        }
        {
            uint32_t coda = fat_voce(m, clus);
            if (coda == FAT_ERR) return -1;
            if (coda < eoc_min(m->tipo) && coda >= 2) {
                if (catena_libera(m, coda) != 0) return -1;
            }
            if (fat_voce_scrivi(m, clus, eoc_scrittura(m->tipo)) != 0) return -1;
        }
    }

    {
        uint8_t *v = voce_mut(m, &pos);
        if (v == NULL) return -1;
        scrivi32(v + 28, nuova_dim);
        scrivi16(v + 20, primo >> 16);
        scrivi16(v + 26, primo & 0xFFFF);
    }

    return 0;
}

/* =============================================================================
 * fat_sync — riversa la cache e aggiorna FSInfo.
 *
 * ! IL CONTEGGIO DEI CLUSTER LIBERI SI DICHIARA IGNOTO, NON SI INVENTA.
 * Questo driver non tiene un conto esatto dei cluster liberi: per averlo
 * bisognerebbe scandire l'intera FAT al montaggio (megabyte, su un volume
 * grande) e mantenerlo a ogni allocazione. La specifica FAT32 prevede
 * 0xFFFFFFFF per "non noto", ed e' la risposta onesta. Scriverci un numero
 * plausibile ma sbagliato sarebbe peggio del non saperlo: gli altri
 * sistemi lo userebbero come se fosse vero.
 *
 * Il suggerimento su dove cercare il prossimo libero, invece, e' un
 * suggerimento per definizione: quello si scrive.
 * ============================================================================= */
int fat_sync(int mnt)
{
    FatMount *m;

    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return -1;
    m = &g_mnt[mnt];

    if (m->tipo == 32 && m->fsinfo_lba != 0) {
        uint8_t *f = settore_mut(m->dev, m->fsinfo_lba);
        if (f != NULL) {
            scrivi32(f + 488, 0xFFFFFFFFu);          /* liberi: non noto */
            scrivi32(f + 492, m->prossimo_libero);   /* dove ricominciare */
        }
    }

    return cache_riversa(m->dev);
}

/* =============================================================================
 * fat_estensione — dove stanno FISICAMENTE i settori di un file
 *
 * Serve all'installatore dell'avvio: il settore di avvio non sa leggere la
 * FAT, quindi gli si scrive dove cominciano i settori e quanti sono.
 *
 * ! RICHIEDE UN FILE CONTIGUO, E LO VERIFICA. Un file frammentato ha una
 * catena che salta, e riassumerla con "primo settore + quanti" darebbe un
 * intervallo che comprende cluster di ALTRI file. Il sistema si
 * avvierebbe caricando un kernel mescolato a dati altrui — cioe' non si
 * avvierebbe, con un sintomo incomprensibile.
 *
 * Ritorna 0, -2 se il file e' frammentato (il chiamante deve poterlo dire
 * all'utente: si risolve ricopiandolo su un volume con spazio contiguo),
 * -1 negli altri casi. `lba_rel` e' RELATIVO all'inizio del volume.
 * ============================================================================= */
int fat_estensione(int mnt, const char *percorso, uint32_t *lba_rel,
                   uint32_t *n_sett)
{
    FatMount *m;
    uint8_t   voce[32];
    uint32_t  primo, dim, clus, passi = 0;

    if (mnt < 0 || mnt >= FAT_MAX_MOUNT || !g_mnt[mnt].usato) return -1;
    if (lba_rel == NULL || n_sett == NULL) return -1;
    m = &g_mnt[mnt];

    if (risolvi(m, percorso, voce, NULL, NULL) != 0) return -1;
    if (voce[11] & FAT_ATTR_DIR) return -1;

    dim   = le32(voce + 28);
    if (dim == 0) return -1;

    primo = (uint32_t)le16(voce + 26);
    if (m->tipo == 32) primo |= ((uint32_t)le16(voce + 20)) << 16;
    if (primo < 2 || primo >= m->n_cluster + 2) return -1;

    clus = primo;

    for (;;) {
        uint32_t p = fat_voce(m, clus);

        if (p == FAT_ERR || p == bad_clus(m->tipo)) return -1;
        if (p >= eoc_min(m->tipo)) break;               /* fine catena */

        /* La contiguita' e' esattamente questo: ogni cluster deve essere
         * il precedente piu' uno. */
        if (p != clus + 1) return -2;
        if (p >= m->n_cluster + 2) return -1;

        clus = p;
        if (++passi > m->n_cluster) return -1;           /* catena ciclica */
    }

    *lba_rel = clus_lba(m, primo);
    *n_sett  = (dim + m->byts_per_sec - 1) / m->byts_per_sec;
    return 0;
}

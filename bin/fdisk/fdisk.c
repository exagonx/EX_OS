/* =============================================================================
 * bin/fdisk/fdisk.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Partizionatore MBR interattivo.
 *
 *   fdisk              elenca i dischi (a schermo: li fa scegliere)
 *   fdisk hd0          apre la sessione sul disco 0
 *   fdisk -t hd0       la sessione a righe di comando, anche sulla console
 *
 * -----------------------------------------------------------------------
 * ! A SCHERMO INTERO, CON LE FRECCE (0.004, 21 ottobre 2026). L'utente: «la
 * procedura di creazione e gestione partizioni sarebbe opportuno gestirla
 * graficamente usando i tasti freccia», e nella scelta del tipo l'elenco di
 * tutti i tipi che servono - mancava lo 0x82 dello scambio, che si poteva
 * solo scrivere in esadecimale sapendolo gia'. Sulla console si apre la
 * tabella: su e giu' scelgono una partizione o uno spazio libero, sinistra
 * e destra l'azione, Invio la fa. La sessione a righe di comando resta
 * com'era - via telnet (un pty: la tastiera in raw e' quella della
 * console, non la nostra), con -t, e quando il servizio 'kbd' non c'e'.
 * Le regole sono le stesse, e cosi' le funzioni che toccano la proposta.
 *
 * -----------------------------------------------------------------------
 * PERCHE' NON E' /bin/disk CHE CRESCE
 *
 * `disk` e' dichiarato in sola lettura in testa al proprio sorgente, ed e'
 * una proprieta' che serve: e' il comando che si lancia senza pensarci su
 * un disco a cui si tiene. Un programma che a seconda degli argomenti
 * guarda o riscrive la tabella delle partizioni perde quella garanzia, e
 * la perde per tutti gli usi, non solo per quello nuovo.
 *
 * -----------------------------------------------------------------------
 * LA TABELLA SI MODIFICA IN MEMORIA E SI SCRIVE UNA VOLTA SOLA
 *
 * Ogni comando lavora su una copia in memoria. Niente tocca il disco fino
 * a `w`, e `q` se ne va senza scrivere. Non e' solo comodita': una
 * tabella scritta un pezzo alla volta passa per stati in cui le
 * partizioni si sovrappongono, e in quegli istanti e' una tabella
 * sbagliata a tutti gli effetti — se la macchina si spegne li' in mezzo,
 * resta sbagliata.
 *
 * -----------------------------------------------------------------------
 * CHI DECIDE COSA
 *
 * Qui stanno le SCELTE: allineamento a 1 MiB, il primo settore utile,
 * i valori predefiniti, i tipi che si possono proporre. Sono politiche —
 * ragionevoli, discutibili, cambiabili.
 *
 * Nel kernel stanno le REGOLE: niente sovrapposizioni, niente partizioni
 * oltre la fine del disco, niente scrittura su un disco GPT o su una
 * partizione montata (kernel/block/mbr.c, kernel/block/blk.c). Quelle non
 * si aggirano, e questo programma non e' l'unico modo di chiamarle.
 *
 * I controlli qui dentro servono a dare un errore SUBITO, mentre l'utente
 * sta ancora componendo la tabella e puo' correggerla, invece che alla
 * `w` quando pensa di aver finito. Non sostituiscono quelli del kernel:
 * li anticipano.
 *
 * -----------------------------------------------------------------------
 * COSA NON FA
 *
 * Non formatta. Una partizione appena creata contiene i byte che c'erano
 * prima in quei settori — non e' vuota, e' non inizializzata: `mount`
 * fallira' finche' non ci si scrive un filesystem.
 *
 * Non tocca le partizioni LOGICHE e non scrive EBR. Le mostra, perche'
 * nasconderle renderebbe incomprensibile la mappa del disco, ma la voce
 * estesa che le contiene qui e' bloccata: spostarla lascerebbe la catena
 * viva sul disco e irraggiungibile.
 * ============================================================================= */
#include "libc.h"
#include "kbd_proto.h"

/* =============================================================================
 * Identità del programma
 *
 * ▲ INCREMENTARE FD_VERSION DI 0.001 A OGNI MODIFICA ▲
 * Stessa regola del kernel e di /bin/textline: stringa con tre decimali,
 * non un numero — EX-OS non usa la virgola mobile.
 * ============================================================================= */
#define FD_NAME     "fdisk"
#define FD_VERSION  "0.004"


/* ! LA VERSIONE STA IN UN POSTO SOLO, ed e' FD_VERSION qui sopra:
 * `fdisk -version` la stampa dall'avvio della libc (vedi EX_VERSIONE in
 * libc.h) e il banner la stampa da dentro. Due letterali uguali
 * diventano due letterali diversi al primo incremento. */
EX_VERSIONE("fdisk", FD_VERSION);
/* =============================================================================
 * Politiche
 *
 * ALLINEAMENTO a 2048 settori = 1 MiB. Il motivo non e' estetico: i dischi
 * moderni hanno settori fisici da 4 KiB e le memorie flash blocchi di
 * cancellazione ben piu' grandi. Una partizione che comincia a un multiplo
 * dispari costringe ogni scrittura del filesystem a cadere a cavallo di
 * due settori fisici, che il disco deve leggere-modificare-riscrivere
 * entrambi. Si perde in prestazioni senza che niente lo segnali.
 *
 * PRIMO_UTILE e' lo stesso valore, e per una ragione in piu': il settore 0
 * contiene la tabella. Una partizione che comincia da li' contiene l'MBR
 * che la descrive, e il primo filesystem che ci scrive lo cancella.
 * ============================================================================= */
#define SETT_PER_MB     2048u
#define ALLINEAMENTO    2048u
#define PRIMO_UTILE     2048u

#define RIGA_MAX        64

/* Tipo predefinito per una partizione nuova: FAT32 con accesso LBA. E'
 * quello che EX-OS sapra' formattare e montare, ed e' quello che ogni
 * altro sistema riconosce senza discutere. */
#define TIPO_PREDEFINITO    0x0C

/* =============================================================================
 * Stato della sessione
 * ============================================================================= */
static unsigned int g_disco;        /* indice ATA */
static unsigned int g_settori;     /* capacita' in settori */
static PartTabella  g_tab;          /* LA PROPOSTA: non e' ancora sul disco */
static int          g_modificata;
static int          g_n_logiche;    /* logiche presenti sul disco */
static int          g_montata;      /* 1 se una partizione di qui e' montata */
static const char  *g_nota;         /* cosa ha visto carica(), per lo schermo */

/* =============================================================================
 * Utilita'
 * ============================================================================= */

/* I tipi che si possono PROPORRE, nell'ordine in cui si propongono: prima
 * quelli che EX-OS usa (FAT32 per i dati, Linux per ext2/3/4, lo scambio),
 * poi quelli degli altri sistemi. Le estese e il GPT protettivo hanno un
 * nome ma non stanno qui: questo programma non li crea (vedi cmd_nuova). */
typedef struct {
    unsigned char t;
    const char   *nome;
    const char   *uso;
} Tipo;

static const Tipo TIPI[] = {
    { 0x0C, "FAT32 LBA",  "dati; la leggono tutti i sistemi" },
    { 0x83, "Linux",      "ext2, ext3, ext4" },
    { 0x82, "swap Linux", "scambio (memoria virtuale): poi mkswap" },
    { 0x0B, "FAT32 CHS",  "FAT32 per BIOS molto vecchi" },
    { 0x0E, "FAT16 LBA",  "FAT16 fino a 2 GB" },
    { 0x06, "FAT16",      "FAT16 per BIOS vecchi" },
    { 0x04, "FAT16 <32M", "FAT16 sotto i 32 MB" },
    { 0x01, "FAT12",      "FAT12, come i floppy" },
    { 0x07, "NTFS/exFAT", "Windows" },
    { 0xEF, "EFI (FAT)",  "partizione di sistema EFI" },
};
#define N_TIPI ((int)(sizeof(TIPI) / sizeof(TIPI[0])))

static const char *nome_tipo(unsigned int t)
{
    int i;

    for (i = 0; i < N_TIPI; i++) if (TIPI[i].t == t) return TIPI[i].nome;
    switch (t) {
        case 0x00: return "-";
        case 0x05: return "estesa CHS";
        case 0x0F: return "estesa LBA";
        case 0x85: return "estesa Linux";
        case 0xEE: return "GPT protett.";
        default:   return "sconosciuto";
    }
}

/* L'elenco, per la sessione a righe di comando. */
static void elenca_tipi(void)
{
    int i;

    for (i = 0; i < N_TIPI; i++)
        printf("  %02X  %-11s %s\n", TIPI[i].t, TIPI[i].nome, TIPI[i].uso);
}

static int tipo_esteso(unsigned int t)
{
    return t == 0x05 || t == 0x0F || t == 0x85;
}

static unsigned int in_mb(unsigned int settori)
{
    return settori / SETT_PER_MB;
}

/* Arrotonda in su al multiplo di ALLINEAMENTO. Satura invece di
 * traboccare: un arrotondamento che passa da 0xFFFFF800 a 0 darebbe una
 * partizione che comincia dal settore 0, cioe' sopra la tabella. */
static unsigned int allinea_su(unsigned int s)
{
    unsigned int r = s % ALLINEAMENTO;
    if (r == 0) return s;
    if (s > 0xFFFFFFFFu - (ALLINEAMENTO - r)) return 0xFFFFFFFFu;
    return s + (ALLINEAMENTO - r);
}

/* Legge una riga dallo standard input. Come in /bin/textline si usa
 * read() diretta e non gets(): il TTY di EX-OS e' orientato alla riga e
 * consegna tutto insieme, quindi un carattere per volta costerebbe una
 * syscall a carattere. Ritorna la lunghezza, o -1 su fine input. */
static int leggi_riga(char *buf, int max)
{
    int n = (int)read(0, buf, (unsigned int)(max - 1));

    if (n <= 0) { buf[0] = '\0'; return -1; }

    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
    return n;
}

static void chiedi(const char *testo, char *buf, int max)
{
    printf("%s", testo);
    if (leggi_riga(buf, max) < 0) buf[0] = '\0';
}

/* Numero decimale senza segno. Ritorna 0 se la stringa non e' un numero:
 * i chiamanti trattano sempre la stringa vuota come "valore predefinito"
 * prima di arrivare qui. */
static unsigned int a_numero(const char *s)
{
    unsigned int v = 0;
    int i = 0;

    while (s[i] == ' ') i++;
    for (; s[i] >= '0' && s[i] <= '9'; i++) v = v * 10u + (unsigned int)(s[i] - '0');
    return v;
}

/* Numero esadecimale con o senza "0x". */
static unsigned int a_esa(const char *s)
{
    unsigned int v = 0;
    int i = 0;

    while (s[i] == ' ') i++;
    if (s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) i += 2;

    for (; ; i++) {
        char c = s[i];
        if      (c >= '0' && c <= '9') v = v * 16u + (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') v = v * 16u + (unsigned int)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = v * 16u + (unsigned int)(c - 'A' + 10);
        else break;
    }
    return v;
}

static int vuota(const char *s)
{
    int i;
    for (i = 0; s[i]; i++) if (s[i] != ' ' && s[i] != '\t') return 0;
    return 1;
}

/* =============================================================================
 * Accesso alla proposta
 * ============================================================================= */

static unsigned int inizio_di(int i)  { return g_tab.voce[i].inizio_lo;  }
static unsigned int settori_di(int i) { return g_tab.voce[i].settori_lo; }
static unsigned int fine_di(int i)    { return g_tab.voce[i].inizio_lo +
                                               g_tab.voce[i].settori_lo; }
static int usata(int i) { return g_tab.voce[i].tipo != 0 &&
                                 g_tab.voce[i].settori_lo != 0; }

/* =============================================================================
 * Spazio libero
 *
 * Calcolato sulla PROPOSTA, non sul disco: e' cio' che serve per proporre
 * un valore predefinito sensato mentre si compone la tabella.
 * ============================================================================= */

/* Primo settore libero e allineato a partire da `da`. 0 se non ce n'e'. */
static unsigned int prossimo_libero(unsigned int da)
{
    int mosso = 1;

    if (da < PRIMO_UTILE) da = PRIMO_UTILE;
    da = allinea_su(da);

    /* Si ripete finche' non si sposta piu': saltare oltre una partizione
     * puo' far finire dentro la successiva. */
    while (mosso) {
        int i;
        mosso = 0;
        for (i = 0; i < PARTWRITE_MAX_VOCI; i++) {
            if (!usata(i)) continue;
            if (da >= inizio_di(i) && da < fine_di(i)) {
                da = allinea_su(fine_di(i));
                mosso = 1;
            }
        }
    }

    if (da >= g_settori) return 0;
    return da;
}

/* Ultimo settore (escluso) del blocco libero che comincia a `inizio`. */
static unsigned int fine_blocco(unsigned int inizio)
{
    unsigned int fine = g_settori;
    int i;

    for (i = 0; i < PARTWRITE_MAX_VOCI; i++) {
        if (!usata(i)) continue;
        if (inizio_di(i) >= inizio && inizio_di(i) < fine) fine = inizio_di(i);
    }
    return fine;
}

/* Vero se [inizio, inizio+lung) tocca una partizione gia' nella proposta,
 * escludendo lo slot `me`. */
static int si_sovrappone(int me, unsigned int inizio, unsigned int lung)
{
    int i;

    for (i = 0; i < PARTWRITE_MAX_VOCI; i++) {
        if (i == me || !usata(i)) continue;
        if (inizio < fine_di(i) && inizio_di(i) < inizio + lung) return 1;
    }
    return 0;
}

/* =============================================================================
 * Caricamento dello stato del disco
 * ============================================================================= */
static int carica(unsigned int idx)
{
    DiskInfo d;
    unsigned int i;
    int r;

    r = diskinfo(idx, &d);
    if (r < 0) {
        printf("fdisk: lettura di hd%u fallita (errore %d)\n", idx, r);
        return -1;
    }
    if (!d.presente) {
        printf("fdisk: hd%u non esiste.\n", idx);
        return -1;
    }
    if (d.tipo != 1) {
        printf("fdisk: hd%u non e' un disco rigido.\n", idx);
        return -1;
    }

    if (d.schema == PT_SCHEMA_GPT) {
        printf("fdisk: hd%u e' un disco GPT.\n\n", idx);
        printf("La sua tabella MBR e' PROTETTIVA: una sola voce di tipo 0xEE\n");
        printf("che copre tutto il disco per impedire agli strumenti vecchi di\n");
        printf("credere che sia libero. Riscriverla e' il modo classico di\n");
        printf("rendere irraggiungibile un disco GPT, e il kernel la rifiuta.\n");
        return -1;
    }

    g_disco = idx;

    /* Oltre i 2 TiB l'MBR non arriva: i suoi campi sono a 32 bit. Meglio
     * limitarsi allo spazio esprimibile e dirlo, che offrire settori che
     * la tabella non saprebbe indirizzare. */
    if (d.settori_hi != 0) {
        g_settori = 0xFFFFFFFFu;
        printf("fdisk: hd%u supera i 2 TiB. L'MBR indirizza 32 bit di settori:\n", idx);
        printf("       oltre i primi 2 TiB lo spazio non e' partizionabile qui.\n\n");
    } else {
        g_settori = d.settori_lo;
    }

    memset(&g_tab, 0, sizeof(g_tab));
    g_n_logiche = 0;

    for (i = 0; i < d.n_part; i++) {
        const PartInfo *p = &d.part[i];

        if (p->logica) { g_n_logiche++; continue; }
        if (p->numero < 1 || p->numero > PARTWRITE_MAX_VOCI) continue;

        g_tab.voce[p->numero - 1].attiva     = p->attiva;
        g_tab.voce[p->numero - 1].tipo       = p->tipo;
        g_tab.voce[p->numero - 1].inizio_lo  = p->inizio_lo;
        g_tab.voce[p->numero - 1].settori_lo = p->settori_lo;
    }

    /* Un disco senza firma 0x55AA NON e' un disco con la tabella rotta: e'
     * un disco mai inizializzato, cioe' esattamente il caso per cui si
     * lancia fdisk. Chiamarlo "anomalia" spaventerebbe l'utente proprio
     * mentre sta per fare la cosa giusta — e a forza di allarmi che non
     * lo sono, quello vero passa inosservato il giorno che arriva. */
    if (d.schema == PT_SCHEMA_NESSUNO && (d.problemi & ~PT_PROB_FIRMA) == 0) {
        printf("hd%u non ha una tabella delle partizioni: e' un disco vergine,\n",
               idx);
        printf("o comunque mai inizializzato. `n` per creare la prima partizione.\n\n");
        g_nota = "Disco senza tabella: e' vergine. Nuova crea la prima partizione.";
    } else if (d.problemi != 0) {
        printf("ATTENZIONE: la tabella attuale di hd%u ha delle anomalie.\n", idx);
        printf("Lancia `disk` per vederle in dettaglio PRIMA di modificarla:\n");
        printf("una tabella malata puo' descrivere dati ancora recuperabili.\n\n");
        g_nota = "! La tabella ha anomalie: guardale con `disk` prima di cambiarla.";
    }

    return 0;
}

/* Cerca fra i montaggi attivi uno che stia su questo disco. Il kernel
 * rifiuterebbe comunque la scrittura, ma dirlo all'inizio e' diverso dal
 * dirlo dopo che l'utente ha composto la tabella. */
static void controlla_montaggi(void)
{
    MountInfo m[4];
    unsigned int start = 0;
    int n, i;

    g_montata = 0;

    for (;;) {
        n = mountinfo(m, 4, start);
        if (n <= 0) break;

        for (i = 0; i < n; i++) {
            /* "hd0p1" -> confronta il prefisso "hd<disco>p" */
            if (m[i].dev[0] != 'h' || m[i].dev[1] != 'd') continue;
            if (m[i].dev[2] != (char)('0' + g_disco))     continue;
            if (m[i].dev[3] != 'p')                       continue;

            if (!g_montata) {
                printf("ATTENZIONE: hd%u ha partizioni MONTATE:\n", g_disco);
                g_montata = 1;
            }
            printf("  %-8s su %s\n", m[i].dev, m[i].punto);
        }

        start += (unsigned int)n;
        if (n < 4) break;
    }

    if (g_montata) {
        printf("Il kernel rifiutera' di riscrivere la tabella finche' lo sono.\n");
        printf("Smontale con `umount`, oppure - se una di quelle e' la root -\n");
        printf("questo disco non e' ripartizionabile mentre ci giri sopra.\n\n");
    }
}

/* =============================================================================
 * Visualizzazione
 * ============================================================================= */
static void mostra(void)
{
    unsigned int libero_tot = 0, s;
    int i, n_usate = 0;

    printf("\nhd%u: %u settori, %u MB", g_disco, g_settori, in_mb(g_settori));
    if (g_modificata) printf("   [proposta NON ancora scritta]");
    printf("\n\n");

    printf("  %-6s%-4s%-14s%12s%12s%10s\n",
           "disp.", "avv", "tipo", "inizio", "settori", "MB");
    printf("  ------------------------------------------------------------\n");

    for (i = 0; i < PARTWRITE_MAX_VOCI; i++) {
        if (!usata(i)) {
            printf("  hd%up%-2d%-4s%-14s%12s%12s%10s\n",
                   g_disco, i + 1, "", "-", "-", "-", "-");
            continue;
        }
        n_usate++;
        printf("  hd%up%-2d%-4s%-14s%12u%12u%10u\n",
               g_disco, i + 1,
               (g_tab.voce[i].attiva == 0x80) ? "si" : "",
               nome_tipo(g_tab.voce[i].tipo),
               inizio_di(i), settori_di(i), in_mb(settori_di(i)));
    }

    if (g_n_logiche > 0) {
        printf("\n  + %d partizioni LOGICHE dentro l'estesa. Non sono modificabili\n",
               g_n_logiche);
        printf("    da qui: `fdisk` non scrive EBR. Vedile con `disk`.\n");
    }

    /* Lo spazio libero e' l'informazione che serve per il comando dopo, e
     * calcolarlo a mente da inizio e lunghezza e' esattamente il genere di
     * conto in cui si sbaglia. */
    printf("\n  Spazio libero:\n");
    s = prossimo_libero(PRIMO_UTILE);
    while (s != 0) {
        unsigned int f = fine_blocco(s);
        if (f > s) {
            printf("    settori %u..%u  (%u MB)\n", s, f - 1, in_mb(f - s));
            libero_tot += f - s;
        }
        if (f >= g_settori) break;
        s = prossimo_libero(f);
    }
    if (libero_tot == 0) printf("    nessuno\n");
    else                 printf("    totale %u MB\n", in_mb(libero_tot));

    if (n_usate == 0 && g_n_logiche == 0)
        printf("\n  Il disco non ha partizioni: `n` per crearne una.\n");

    printf("\n");
}

/* =============================================================================
 * Comandi
 * ============================================================================= */

/* Chiede uno slot 1..4. Ritorna l'indice 0..3, o -1. */
static int chiedi_slot(const char *testo, int deve_essere_usato)
{
    char r[RIGA_MAX];
    unsigned int v;

    chiedi(testo, r, RIGA_MAX);
    if (vuota(r)) return -1;

    v = a_numero(r);
    if (v < 1 || v > PARTWRITE_MAX_VOCI) {
        printf("Gli slot delle primarie sono 1..%d.\n", PARTWRITE_MAX_VOCI);
        return -1;
    }

    if (deve_essere_usato && !usata((int)v - 1)) {
        printf("hd%up%u e' gia' libero.\n", g_disco, v);
        return -1;
    }
    if (!deve_essere_usato && usata((int)v - 1)) {
        printf("hd%up%u esiste gia': cancellala prima con `d`.\n", g_disco, v);
        return -1;
    }

    return (int)v - 1;
}

static void cmd_nuova(void)
{
    char r[RIGA_MAX];
    int  slot, i;
    unsigned int inizio, fine, lung, tipo, mb;

    /* Uno slot libero deve esserci prima di far comporre tutto il resto. */
    slot = -1;
    for (i = 0; i < PARTWRITE_MAX_VOCI; i++) if (!usata(i)) { slot = i; break; }
    if (slot < 0) {
        printf("Tutti e quattro gli slot delle primarie sono occupati.\n");
        printf("Cancellane uno con `d`.\n");
        return;
    }

    printf("Slot 1..%d (Invio = %d): ", PARTWRITE_MAX_VOCI, slot + 1);
    if (leggi_riga(r, RIGA_MAX) < 0) return;
    if (!vuota(r)) {
        unsigned int v = a_numero(r);
        if (v < 1 || v > PARTWRITE_MAX_VOCI) {
            printf("Gli slot delle primarie sono 1..%d.\n", PARTWRITE_MAX_VOCI);
            return;
        }
        if (usata((int)v - 1)) {
            printf("hd%up%u esiste gia': cancellala prima con `d`.\n", g_disco, v);
            return;
        }
        slot = (int)v - 1;
    }

    /* --- inizio --- */
    inizio = prossimo_libero(PRIMO_UTILE);
    if (inizio == 0) {
        printf("Non c'e' spazio libero sul disco.\n");
        return;
    }

    printf("Primo settore (Invio = %u): ", inizio);
    if (leggi_riga(r, RIGA_MAX) < 0) return;
    if (!vuota(r)) {
        unsigned int v = a_numero(r);
        unsigned int a = allinea_su(v);

        if (v < PRIMO_UTILE) {
            printf("Il primo settore utile e' %u: sotto c'e' la tabella delle\n",
                   PRIMO_UTILE);
            printf("partizioni, e una partizione che la contiene si autodistrugge.\n");
            return;
        }
        if (a != v) {
            printf("Allineato a %u (multiplo di %u = 1 MiB).\n", a, ALLINEAMENTO);
        }
        inizio = a;
    }

    if (inizio >= g_settori) {
        printf("Il settore %u e' oltre la fine del disco (%u).\n", inizio, g_settori);
        return;
    }

    fine = fine_blocco(inizio);
    if (fine <= inizio) {
        printf("Il settore %u e' dentro una partizione esistente.\n", inizio);
        return;
    }

    /* --- dimensione --- */
    mb = in_mb(fine - inizio);
    printf("Dimensione in MB (Invio = %u, tutto il blocco): ", mb);
    if (leggi_riga(r, RIGA_MAX) < 0) return;

    if (vuota(r)) {
        lung = fine - inizio;
    } else {
        unsigned int v = a_numero(r);
        if (v == 0) {
            printf("Dimensione nulla: annullato.\n");
            return;
        }
        if (v > in_mb(0xFFFFFFFFu)) {
            printf("Dimensione fuori scala.\n");
            return;
        }
        lung = v * SETT_PER_MB;
        if (lung > fine - inizio) {
            printf("Nel blocco libero ci stanno %u MB, non %u.\n",
                   in_mb(fine - inizio), v);
            return;
        }
        /* La lunghezza si arrotonda in giu' al MiB: e' gia' un multiplo
         * di ALLINEAMENTO, quindi la partizione DOPO potra' cominciare
         * allineata senza lasciare un buco. */
    }

    /* --- tipo --- */
    printf("Tipo (Invio = 0x%02X %s):\n", TIPO_PREDEFINITO,
           nome_tipo(TIPO_PREDEFINITO));
    elenca_tipi();
    printf("Tipo: ");
    if (leggi_riga(r, RIGA_MAX) < 0) return;

    if (vuota(r)) {
        tipo = TIPO_PREDEFINITO;
    } else {
        tipo = a_esa(r);
        if (tipo == 0 || tipo > 0xFF) {
            printf("Il tipo e' un byte 0x01..0xFF. Il tipo 0 significa "
                   "'slot libero'.\n");
            return;
        }
        if (tipo == 0xEE) {
            printf("0xEE e' il tipo dell'MBR PROTETTIVO di un disco GPT.\n");
            printf("Scriverlo su un disco che GPT non e' fa credere a ogni\n");
            printf("altro sistema che ci sia una mappa GPT da qualche parte.\n");
            return;
        }
        if (tipo_esteso(tipo)) {
            printf("Questo programma non gestisce le partizioni estese: non\n");
            printf("scrive EBR, quindi creerebbe un contenitore che nessuno\n");
            printf("qui dentro sa riempire.\n");
            return;
        }
    }

    /* Ultimo controllo prima di accettare: dare l'errore ora, mentre i
     * numeri sono ancora sotto gli occhi, e' diverso dal darlo alla `w`. */
    if (si_sovrappone(slot, inizio, lung)) {
        printf("Si sovrappone a una partizione esistente: annullato.\n");
        return;
    }

    g_tab.voce[slot].attiva     = 0;
    g_tab.voce[slot].tipo       = tipo;
    g_tab.voce[slot].inizio_lo  = inizio;
    g_tab.voce[slot].inizio_hi  = 0;
    g_tab.voce[slot].settori_lo = lung;
    g_tab.voce[slot].settori_hi = 0;
    g_modificata = 1;

    printf("Creata hd%up%d: %s, settori %u..%u (%u MB).\n",
           g_disco, slot + 1, nome_tipo(tipo), inizio, inizio + lung - 1,
           in_mb(lung));
    printf("Non e' formattata: `mount` fallira' finche' non ci sara' un "
           "filesystem.\n");
}

static void cmd_cancella(void)
{
    int slot = chiedi_slot("Slot da cancellare (1..4): ", 1);
    if (slot < 0) return;

    if (tipo_esteso(g_tab.voce[slot].tipo) && g_n_logiche > 0) {
        printf("hd%up%d e' l'estesa che contiene %d partizioni logiche.\n",
               g_disco, slot + 1, g_n_logiche);
        printf("Cancellarla lascerebbe la loro catena di EBR viva sul disco ma\n");
        printf("irraggiungibile, senza che niente lo dica. Il kernel lo rifiuta.\n");
        return;
    }

    memset(&g_tab.voce[slot], 0, sizeof(PartVoce));
    g_modificata = 1;

    printf("hd%up%d cancellata dalla proposta.\n", g_disco, slot + 1);
    printf("I DATI sul disco ci sono ancora: sparisce la voce che li descrive,\n");
    printf("e finche' non scrivi con `w` non e' successo niente.\n");
}

static void cmd_tipo(void)
{
    char r[RIGA_MAX];
    unsigned int tipo;
    int slot = chiedi_slot("Slot di cui cambiare il tipo (1..4): ", 1);

    if (slot < 0) return;

    if (tipo_esteso(g_tab.voce[slot].tipo) && g_n_logiche > 0) {
        printf("hd%up%d e' l'estesa che contiene le logiche: cambiarle tipo le\n",
               g_disco, slot + 1);
        printf("renderebbe irraggiungibili.\n");
        return;
    }

    printf("Tipo attuale: 0x%02X %s\n", g_tab.voce[slot].tipo,
           nome_tipo(g_tab.voce[slot].tipo));
    elenca_tipi();
    chiedi("Nuovo tipo: ", r, RIGA_MAX);
    if (vuota(r)) return;

    tipo = a_esa(r);
    if (tipo == 0 || tipo > 0xFF) {
        printf("Il tipo e' un byte 0x01..0xFF.\n");
        return;
    }
    if (tipo == 0xEE || tipo_esteso(tipo)) {
        printf("Tipo non proponibile da qui (GPT protettivo o estesa).\n");
        return;
    }

    g_tab.voce[slot].tipo = tipo;
    g_modificata = 1;

    printf("hd%up%d ora e' 0x%02X %s.\n", g_disco, slot + 1, tipo, nome_tipo(tipo));
    printf("Il byte di tipo e' solo un SUGGERIMENTO: non cambia un solo byte\n");
    printf("dentro la partizione, e non converte niente.\n");
}

static void cmd_attiva(void)
{
    int slot = chiedi_slot("Slot da rendere avviabile (1..4): ", 1);
    int i;

    if (slot < 0) return;

    if (g_tab.voce[slot].attiva == 0x80) {
        g_tab.voce[slot].attiva = 0;
        g_modificata = 1;
        printf("hd%up%d non e' piu' avviabile.\n", g_disco, slot + 1);
        return;
    }

    /* Una sola attiva. L'MBR prende la PRIMA che trova col flag acceso:
     * con due, quale parte dipende dall'ordine nella tabella, che non e'
     * l'ordine sul disco. Un avvio che dipende da quel dettaglio e' un
     * avvio che un giorno parte dal sistema sbagliato. */
    for (i = 0; i < PARTWRITE_MAX_VOCI; i++) {
        if (i != slot && g_tab.voce[i].attiva == 0x80) {
            g_tab.voce[i].attiva = 0;
            printf("hd%up%d non e' piu' avviabile: ne puo' essere attiva una sola.\n",
                   g_disco, i + 1);
        }
    }

    g_tab.voce[slot].attiva = 0x80;
    g_modificata = 1;
    printf("hd%up%d e' avviabile.\n", g_disco, slot + 1);
}

/* Traduce la maschera PT_PROB_* del rifiuto. Senza questo, un -22 e'
 * indistinguibile da qualunque altro argomento non valido. */
static void spiega_problemi(unsigned int pb)
{
    if (pb == 0) return;

    printf("Il kernel ha rifiutato la tabella. Cosa non andava:\n");
    if (pb & PT_PROB_SOVRAPP)
        printf("  - due partizioni si sovrappongono\n");
    if (pb & PT_PROB_OLTRE_FINE)
        printf("  - una partizione finisce oltre l'ultimo settore del disco,\n"
               "    oppure oltre i 2 TiB che l'MBR sa indirizzare\n");
    if (pb & PT_PROB_SETTORE0)
        printf("  - una partizione comincia dall'LBA 0, dove sta la tabella\n");
    if (pb & PT_PROB_BOOTFLAG)
        printf("  - un flag di avvio diverso da 0x00/0x80\n");
    if (pb & PT_PROB_VUOTA)
        printf("  - una voce con tipo valido ma dimensione zero\n");
    if (pb & PT_PROB_TROPPE_EXT)
        printf("  - piu' di una partizione estesa\n");
    if (pb & PT_PROB_GPT)
        printf("  - il disco e' GPT: la sua tabella MBR non si tocca\n");
}

/* =============================================================================
 * `z` — azzera l'intera tabella e si ricomincia
 *
 * ! ESISTE PER UN VICOLO CIECO VERO, trovato il 14 settembre 2026 su un disco
 * che andava svuotato per intero. Un'ESTESA che contiene partizioni LOGICHE
 * non si puo' cancellare con `d` ne' cambiare con `t`: tutti e due si
 * rifiutano, e a ragione — togliere la voce dell'estesa lascerebbe la catena
 * di EBR viva sul disco e irraggiungibile, e nessuno saprebbe piu' che c'e'.
 *
 * ! MA LE LOGICHE QUESTO PROGRAMMA NON LE SA CANCELLARE, perche' non scrive
 * EBR. E il kernel non espone nemmeno l'estesa come dispositivo — c'e' hd0p5,
 * la logica, ma hd0p3 no — quindi non si puo' nemmeno formattarla sopra per
 * spezzare la catena. Chi arriva li' con un disco da svuotare NON HA UNA VIA
 * D'USCITA: ogni strada e' chiusa da un rifiuto sensato preso da solo.
 *
 * ! QUESTA E' LA VIA, ED E' ONESTA SU COSA FA. Non finge di cancellare le
 * logiche: dice che la catena di EBR resta li' dov'e', orfana, e che sara' il
 * filesystem nuovo a passarci sopra. E' la differenza fra «cancello una
 * partizione», che deve restare prudente, e «ricomincio da capo su questo
 * disco», che e' un'altra decisione e va chiesta una volta sola.
 *
 * ! E NON SCRIVE NIENTE DA SE'. Come ogni altro comando qui dentro, tocca solo
 * la proposta: il disco cambia con `w`, e non prima.
 * ========================================================================== */
static void cmd_azzera(void)
{
    char r[RIGA_MAX];
    int  i, n_usate = 0, c_e_estesa = 0;

    for (i = 0; i < PARTWRITE_MAX_VOCI; i++) {
        if (!usata(i)) continue;
        n_usate++;
        if (tipo_esteso(g_tab.voce[i].tipo)) c_e_estesa = 1;
    }

    if (n_usate == 0) {
        printf("La tabella e' gia' vuota.\n");
        return;
    }

    printf("\n");
    mostra();
    printf("Questo toglie TUTTE E QUATTRO le voci della tabella.\n");

    if (c_e_estesa && g_n_logiche > 0) {
        printf("\n! C'E' UN'ESTESA CON %d PARTIZIONI LOGICHE DENTRO, e la loro\n",
               g_n_logiche);
        printf("  catena di EBR RESTA SCRITTA SUL DISCO: questo programma non\n");
        printf("  la sa togliere. Diventa orfana: nessuno la trovera' piu',\n");
        printf("  perche' sparisce la voce che diceva dove comincia, e ci\n");
        printf("  passera' sopra il primo filesystem che creerai li'.\n");
        printf("  Va bene se stai svuotando il disco. Non va bene se speravi\n");
        printf("  di tenerti quello che c'e' dentro le logiche.\n");
    }

    printf("\nI DATI non vengono cancellati adesso: sparisce la mappa che\n");
    printf("dice dove sono, e senza quella non si raggiungono piu'.\n\n");

    chiedi("Azzerare la tabella? (scrivi `azzera` per confermare): ",
           r, RIGA_MAX);
    if (strcmp(r, "azzera") != 0) {
        printf("Annullato. La proposta e' rimasta com'era.\n");
        return;
    }

    for (i = 0; i < PARTWRITE_MAX_VOCI; i++)
        memset(&g_tab.voce[i], 0, sizeof(PartVoce));
    g_modificata = 1;

    printf("Tabella azzerata NELLA PROPOSTA. Fino a `w` il disco e' intatto.\n");

    /* ! CON LE LOGICHE DI MEZZO SI SCRIVE IN DUE GIRI, e va detto qui o non lo
     * scopre nessuno. Il kernel concede di far sparire un'estesa che contiene
     * logiche SOLO se la tabella che gli si propone e' completamente VUOTA:
     * una proposta che la cancella e intanto crea altro e' un
     * ripartizionamento, e li' il rifiuto vale ancora. Quindi prima si scrive
     * il vuoto, e le nuove si fanno rientrando. */
    if (g_n_logiche > 0) {
        printf("\n! CI SONO PARTIZIONI LOGICHE, quindi ci vogliono DUE GIRI:\n");
        printf("    adesso `w` e poi `q`, che scrive la tabella VUOTA;\n");
        printf("    poi rientri con `fdisk hd%u` e fai `n` per le nuove.\n",
               g_disco);
        printf("  Il kernel lascia sparire un'estesa con logiche dentro solo\n");
        printf("  verso una tabella vuota: creare e cancellare nello stesso\n");
        printf("  colpo lo rifiuta, ed e' giusto cosi'.\n");
    } else {
        printf("Adesso `n` per creare le nuove, e `w` quando sei convinto.\n");
    }
}

static void cmd_scrivi(void)
{
    char r[RIGA_MAX];
    int  res, i, n_usate = 0;

    if (!g_modificata) {
        printf("Non hai cambiato niente: non c'e' niente da scrivere.\n");
        return;
    }

    for (i = 0; i < PARTWRITE_MAX_VOCI; i++) if (usata(i)) n_usate++;

    printf("\n");
    mostra();

    printf("Questa tabella sostituira' quella sul disco hd%u.\n", g_disco);
    if (n_usate == 0) {
        printf("NON CI SONO PARTIZIONI: il disco risultera' vuoto.\n");
    }
    printf("I dati dentro le partizioni non vengono toccati, ma quelli che\n");
    printf("stavano in una partizione che hai cancellato diventano\n");
    printf("irraggiungibili: sparisce la voce che dice dove sono.\n\n");

    chiedi("Scrivere? (scrivi `si` per confermare): ", r, RIGA_MAX);
    if (strcmp(r, "si") != 0) {
        printf("Annullato. Il disco non e' stato toccato.\n");
        return;
    }

    g_tab.problemi = 0;
    res = partwrite(g_disco, &g_tab);

    if (res == 0) {
        printf("\nTabella scritta. I dispositivi a blocchi sono gia' aggiornati:\n");
        printf("non serve riavviare.\n\n");
        for (i = 0; i < PARTWRITE_MAX_VOCI; i++) {
            if (!usata(i)) continue;
            printf("  hd%up%d  %u MB  %s\n", g_disco, i + 1,
                   in_mb(settori_di(i)), nome_tipo(g_tab.voce[i].tipo));
        }
        printf("\nLe partizioni NON sono formattate: `mount` fallira' finche'\n");
        printf("non ci sara' dentro un filesystem.\n");
        g_modificata = 0;
        return;
    }

    printf("\nScrittura fallita (errore %d).\n", res);

    switch (-res) {
        case 16:
            printf("Una partizione di hd%u e' MONTATA. Il kernel non ripartiziona\n",
                   g_disco);
            printf("sotto un filesystem montato: smontala con `umount`.\n");
            printf("Se e' la root, questo disco non e' ripartizionabile mentre\n");
            printf("il sistema ci gira sopra: avvia da floppy.\n");
            printf("\nStesso errore anche se stai spostando l'estesa che contiene\n");
            printf("delle partizioni logiche.\n");
            break;
        case 1:
            printf("Il disco e' GPT: la sua tabella MBR e' protettiva e non si\n");
            printf("riscrive.\n");
            break;
        case 19:
            printf("hd%u non risponde piu'.\n", g_disco);
            break;
        case 5:
            printf("Errore di I/O: il disco non ha accettato la scrittura.\n");
            break;
        case 22:
            spiega_problemi(g_tab.problemi);
            break;
        default:
            break;
    }

    printf("\nLa tabella sul disco NON e' stata modificata: il kernel valida\n");
    printf("tutto prima di scrivere, e rifiuta in blocco.\n");
}

static void cmd_aiuto(void)
{
    printf("\n");
    printf("  p   mostra la tabella e lo spazio libero\n");
    printf("  n   crea una partizione\n");
    printf("  d   cancella una partizione\n");
    printf("  z   azzera TUTTA la tabella e si ricomincia\n");
    printf("  t   cambia il tipo di una partizione\n");
    printf("  a   commuta il flag avviabile\n");
    printf("  w   SCRIVE la tabella sul disco (chiede conferma)\n");
    printf("  q   esce senza scrivere\n");
    printf("  h   questo aiuto\n");
    printf("\n");
    printf("  Niente tocca il disco fino a `w`.\n");
    printf("\n");
}

/* =============================================================================
 * Elenco dei dischi, quando non e' stato indicato quale
 * ============================================================================= */
static int elenca_dischi(void)
{
    DiskInfo d;
    unsigned int i;
    int trovati = 0;

    printf("Dischi rigidi:\n");

    for (i = 0; i < 4; i++) {
        if (diskinfo(i, &d) < 0) continue;
        if (!d.presente || d.tipo != 1) continue;

        trovati++;
        printf("  hd%u  %-42s %u MB  %s\n", i, d.modello,
               in_mb(d.settori_lo),
               (d.schema == PT_SCHEMA_GPT) ? "GPT (non modificabile)"
             : (d.schema == PT_SCHEMA_MBR) ? "MBR"
             : "nessuna tabella");
    }

    if (trovati == 0) {
        printf("  nessuno.\n\n");
        printf("Se ti aspettavi un disco, controlla che sia collegato a un canale\n");
        printf("IDE: `disk` mostra cosa vede il sistema.\n");
        return 1;
    }

    printf("\nuso: fdisk hd<n>        (sulla console a schermo intero)\n");
    printf("     fdisk -t hd<n>     a righe di comando\n");
    return 0;
}

/* =============================================================================
 * A SCHERMO INTERO (0.004)
 *
 * Lo stesso schema di bin/cdinstall: si scrive con write(1, ...) e sequenze
 * ANSI, i tasti arrivano dal servizio 'kbd' in modo raw, la cornice e' ASCII
 * perche' la stessa schermata puo' finire sulla seriale. Le azioni chiamano
 * le stesse regole della sessione a righe (usata, prossimo_libero,
 * fine_blocco, si_sovrappone): cambia solo il modo di chiedere.
 * ============================================================================= */
#define RIGHE_MIN   25
#define COLONNE_MAX 160

#define C_NORM   "\033[0m"
#define C_TIT    "\033[1;37;44m"
#define C_BOX    "\033[0;37;44m"
#define C_SCELTO "\033[0;30;47m"
#define C_AVVISO "\033[1;33;44m"
#define C_SPENTO "\033[0;36;44m"

static int      g_righe = 25, g_cols = 80;
static int      g_kbd = -1;
static unsigned g_console = 0;
static char     g_msg[2][100];      /* le due righe sotto la tabella */

static void o(const char *t)
{
    unsigned int n = (unsigned int)strlen(t);
    if (n) write(1, t, n);
}

static void sc_va(int r, int c)
{
    char b[24];
    snprintf(b, sizeof(b), "\033[%d;%dH", r, c);
    o(b);
}

static void sc_scrivi(int r, int c, const char *t) { sc_va(r, c); o(t); }

/* ! L'ULTIMA CELLA DELL'ULTIMA RIGA NON SI TOCCA: scriverci manda a capo,
 * e in fondo allo schermo a capo vuol dire far scorrere tutto (vedi
 * sc_barra in bin/cdinstall). Qui non ci arriva nessuna riga piena. */
static void sc_ripeti(int r, int c, char ch, int n)
{
    char b[COLONNE_MAX + 1];
    int  i;

    if (n > g_cols - c + 1) n = g_cols - c + 1;
    if (n <= 0) return;
    for (i = 0; i < n; i++) b[i] = ch;
    b[n] = '\0';
    sc_scrivi(r, c, b);
}

static void sc_riquadro(int r, int c, int w, int h, const char *titolo)
{
    int i;

    sc_scrivi(r, c, "+");
    sc_ripeti(r, c + 1, '-', w - 2);
    sc_scrivi(r, c + w - 1, "+");
    for (i = 1; i < h - 1; i++) {
        sc_scrivi(r + i, c, "|");
        sc_ripeti(r + i, c + 1, ' ', w - 2);
        sc_scrivi(r + i, c + w - 1, "|");
    }
    sc_scrivi(r + h - 1, c, "+");
    sc_ripeti(r + h - 1, c + 1, '-', w - 2);
    sc_scrivi(r + h - 1, c + w - 1, "+");
    if (titolo && titolo[0]) {
        sc_scrivi(r, c + (w - (int)strlen(titolo) - 2) / 2, " ");
        o(titolo);
        o(" ");
    }
}

static void sc_misura(void)
{
    TtyWinSize ws;

    if (ioctl(1, TTY_IOCTL_GETSIZE, &ws) == 0 && ws.rows > 0 && ws.cols > 0) {
        g_righe = ws.rows;
        g_cols  = ws.cols;
    }
    if (g_righe < RIGHE_MIN)   g_righe = RIGHE_MIN;
    if (g_cols  > COLONNE_MAX) g_cols  = COLONNE_MAX;
}

static void kbd_modo(unsigned modo)
{
    KbdSetMode m;

    if (g_kbd <= 0) return;
    m.modo    = modo;
    m.console = g_console;
    ipc_send((unsigned)g_kbd, KBD_MSG_SETMODE, &m, sizeof(m));
}

/* ! LA MODALITA' SI RIAFFERMA A OGNI SCADENZA, come in cdinstall: chi si
 * ferma a leggere non deve perdere il raw. Rende 0 se 'kbd' non risponde. */
static unsigned tasto(void)
{
    IpcMessage meta;
    unsigned   k = 0;
    int        i;

    if (g_kbd <= 0) return 0;
    for (;;) {
        if (ipc_send((unsigned)g_kbd, KBD_MSG_READKEY,
                     &g_console, sizeof(g_console)) < 0) return 0;
        for (i = 0; i < 8; i++) {
            if (ipc_recv_timeout(&meta, &k, sizeof(k), 2000) < 0) break;
            if ((int)meta.sender_pid != g_kbd) continue;
            if (meta.tipo == KBD_MSG_KEY && meta.len >= sizeof(k))
                return k & KBD_KEY_MASK;
        }
        kbd_modo(KBD_MODE_RAW);
    }
}

/* Lo schermo si puo' usare? ! NON SU UN PTY: via telnet console_info non
 * risponde, e la tastiera in raw sarebbe quella della console, non di chi
 * scrive. NE' IN BACKGROUND: l'ultimo che chiede un tasto e' quello servito,
 * e la shell resterebbe senza (stessa verifica di bin/help). */
static int puo_schermo(void)
{
    ConsoleInfo ci;

    if (console_info(&ci) != 0) return 0;
    if (ci.fg != 0 && ci.fg != (unsigned)getpid()) return 0;
    if (ci.mia != ci.visibile) return 0;
    g_console = ci.mia;
    g_kbd = ipc_lookup(KBD_SERVICE_NAME);
    return g_kbd > 0;
}

static void msg(const char *a, const char *b)
{
    snprintf(g_msg[0], sizeof(g_msg[0]), "%s", a ? a : "");
    snprintf(g_msg[1], sizeof(g_msg[1]), "%s", b ? b : "");
}

static void mb_testo(char *d, unsigned int n, unsigned int mb)
{
    if (mb >= 10240) snprintf(d, n, "%u,%u GB", mb / 1024, (mb % 1024) * 10 / 1024);
    else             snprintf(d, n, "%u MB", mb);
}

/* Un menu in un riquadro al centro: frecce, Invio, Esc (-1). */
static int scegli(const char *titolo, const char *const *voci, int n, int scelto, int w)
{
    int r = (g_righe - n - 2) / 2, c = (g_cols - w) / 2, i;

    o(C_BOX);
    sc_riquadro(r, c, w, n + 2, titolo);
    for (;;) {
        for (i = 0; i < n; i++) {
            o(i == scelto ? C_SCELTO : C_BOX);
            sc_ripeti(r + 1 + i, c + 1, ' ', w - 2);
            sc_scrivi(r + 1 + i, c + 2, voci[i]);
        }
        o(C_BOX);
        sc_va(r + 1 + scelto, c + 1);
        switch (tasto()) {
        case KBD_K_UP:   if (scelto > 0)     scelto--; break;
        case KBD_K_DOWN: if (scelto < n - 1) scelto++; break;
        case KBD_K_HOME: scelto = 0;     break;
        case KBD_K_END:  scelto = n - 1; break;
        case '\r': case '\n': return scelto;
        case 0x1B: case 0: return -1;
        default: break;
        }
    }
}

/* Una domanda con un campo da scrivere (decimale, o esadecimale se esa).
 * Il campo parte da quel che c'e' in b: chi va bene preme Invio.
 * Rende la lunghezza, -1 per Esc. */
static int chiedi_campo(const char *titolo, const char *riga, char *b, int max, int esa)
{
    int w = 64, r = (g_righe - 7) / 2, c = (g_cols - w) / 2, n = (int)strlen(b);

    o(C_BOX);
    sc_riquadro(r, c, w, 7, titolo);
    sc_scrivi(r + 2, c + 2, riga);
    sc_scrivi(r + 5, c + 2, "Invio conferma, Esc annulla");
    for (;;) {
        unsigned k;

        o(C_SCELTO);
        sc_ripeti(r + 3, c + 2, ' ', 14);
        sc_scrivi(r + 3, c + 3, b);
        o(C_BOX);
        sc_va(r + 3, c + 3 + n);
        k = tasto();
        if (k == '\r' || k == '\n') return n;
        if (k == 0x1B || k == 0) return -1;
        if ((k == '\b' || k == 0x7F) && n > 0) { b[--n] = '\0'; continue; }
        if (n < max && ((k >= '0' && k <= '9') ||
                        (esa && (k | 0x20) >= 'a' && (k | 0x20) <= 'f'))) {
            b[n++] = (char)k;
            b[n] = '\0';
        }
    }
}

/* Si' o no, con tre righe di spiegazione. ! SI PARTE DAL NO: chi preme
 * Invio senza leggere non deve aver scritto un disco. */
static int conferma(const char *titolo, const char *a, const char *b, const char *c3)
{
    static const char *const SN[2] = { "No", "Si'" };
    int w = 64, h = 10, r = (g_righe - h) / 2, c = (g_cols - w) / 2, s, i;
    const char *l[3];

    l[0] = a; l[1] = b; l[2] = c3;
    o(C_BOX);
    sc_riquadro(r, c, w, h, titolo);
    for (i = 0; i < 3; i++) if (l[i]) sc_scrivi(r + 2 + i, c + 2, l[i]);
    for (s = 0;;) {
        for (i = 0; i < 2; i++) {
            o(i == s ? C_SCELTO : C_BOX);
            sc_scrivi(r + 6, c + 18 + i * 16, "            ");
            sc_scrivi(r + 6, c + 22 + i * 16, SN[i]);
        }
        o(C_BOX);
        switch (tasto()) {
        case KBD_K_LEFT:  case KBD_K_UP:   s = 0; break;
        case KBD_K_RIGHT: case KBD_K_DOWN: s = 1; break;
        case 's': case 'S': return 1;
        case 'n': case 'N': case 0x1B: case 0: return 0;
        case '\r': case '\n': return s;
        default: break;
        }
    }
}

/* -----------------------------------------------------------------------------
 * Le righe della tabella: le partizioni e gli spazi liberi, in ordine di
 * disco. Lo spazio libero e' una riga come le altre perche' e' li' che si
 * crea: «scegli dove, poi Nuova».
 * --------------------------------------------------------------------------- */
typedef struct {
    int          slot;              /* -1 = spazio libero */
    unsigned int inizio, fine;
} Riga;

#define RIGHE_TAB (PARTWRITE_MAX_VOCI * 2 + 1)
static Riga g_riga[RIGHE_TAB];
static int  g_n_righe;

static void fai_righe(void)
{
    unsigned int s;
    int i, j;

    g_n_righe = 0;
    for (i = 0; i < PARTWRITE_MAX_VOCI; i++) {
        if (!usata(i)) continue;
        g_riga[g_n_righe].slot   = i;
        g_riga[g_n_righe].inizio = inizio_di(i);
        g_riga[g_n_righe].fine   = fine_di(i);
        g_n_righe++;
    }
    s = prossimo_libero(PRIMO_UTILE);
    while (s != 0 && g_n_righe < RIGHE_TAB) {
        unsigned int f = fine_blocco(s);

        if (f > s && in_mb(f - s) > 0) {
            g_riga[g_n_righe].slot   = -1;
            g_riga[g_n_righe].inizio = s;
            g_riga[g_n_righe].fine   = f;
            g_n_righe++;
        }
        if (f >= g_settori) break;
        s = prossimo_libero(f);
    }
    for (i = 1; i < g_n_righe; i++) {
        Riga t = g_riga[i];
        for (j = i; j > 0 && g_riga[j - 1].inizio > t.inizio; j--) g_riga[j] = g_riga[j - 1];
        g_riga[j] = t;
    }
}

static const char *const AZIONI[] = {
    "Nuova", "Cancella", "Tipo", "Avvio", "Azzera", "Scrivi", "Esci"
};
enum { AZ_NUOVA, AZ_CANCELLA, AZ_TIPO, AZ_AVVIO, AZ_AZZERA, AZ_SCRIVI, AZ_ESCI, N_AZ };

static void disegna(int sel, int az)
{
    char b[COLONNE_MAX + 1], m1[16], m2[16];
    int  i, c;
    const Riga *g = g_n_righe ? &g_riga[sel] : 0;

    o(C_BOX);
    o("\033[2J");
    o(C_TIT);
    sc_ripeti(1, 1, ' ', g_cols);
    mb_testo(m1, sizeof(m1), in_mb(g_settori));
    snprintf(b, sizeof(b), "fdisk %s  -  hd%u, %s%s", FD_VERSION, g_disco, m1,
             g_modificata ? "   [modifiche NON ancora scritte]" : "");
    sc_scrivi(1, 3, b);

    o(C_BOX);
    sc_scrivi(3, 3, "Partizione  Avvio  Tipo                 Inizio     Dimensione");
    sc_ripeti(4, 3, '-', 64);
    for (i = 0; i < g_n_righe; i++) {
        const Riga *x = &g_riga[i];

        mb_testo(m1, sizeof(m1), in_mb(x->inizio));
        mb_testo(m2, sizeof(m2), in_mb(x->fine - x->inizio));
        if (x->slot >= 0)
            snprintf(b, sizeof(b), "hd%up%-7d %-6s %02X %-14s %10s %14s", g_disco,
                     x->slot + 1, g_tab.voce[x->slot].attiva == 0x80 ? "si" : "",
                     g_tab.voce[x->slot].tipo, nome_tipo(g_tab.voce[x->slot].tipo), m1, m2);
        else
            snprintf(b, sizeof(b), "%-10s  %-6s %-17s %10s %14s", "", "", "(spazio libero)", m1, m2);
        o(i == sel ? C_SCELTO : (x->slot < 0 ? C_SPENTO : C_BOX));
        sc_ripeti(5 + i, 2, ' ', 66);
        sc_scrivi(5 + i, 3, b);
    }
    o(C_BOX);
    if (g_n_righe == 0) sc_scrivi(5, 3, "(nessuno spazio utilizzabile)");
    if (g_n_logiche > 0) {
        snprintf(b, sizeof(b), "+ %d partizioni logiche dentro l'estesa: si vedono con `disk`,",
                 g_n_logiche);
        sc_scrivi(5 + RIGHE_TAB, 3, b);
        sc_scrivi(6 + RIGHE_TAB, 3, "  non si cambiano da qui (fdisk non scrive EBR).");
    }

    /* Cosa e' la riga scelta, e a cosa serve. */
    if (g) {
        if (g->slot < 0) {
            snprintf(b, sizeof(b), "Spazio libero: Nuova ci crea una partizione.");
        } else {
            unsigned int t = g_tab.voce[g->slot].tipo;
            const char  *uso = "";

            for (i = 0; i < N_TIPI; i++) if (TIPI[i].t == t) uso = TIPI[i].uso;
            if (tipo_esteso(t)) uso = "contiene le partizioni logiche";
            snprintf(b, sizeof(b), "hd%up%d: %s", g_disco, g->slot + 1, uso);
        }
        sc_scrivi(8 + RIGHE_TAB, 3, b);
    }
    o(C_AVVISO);
    sc_scrivi(10 + RIGHE_TAB, 3, g_msg[0]);
    sc_scrivi(11 + RIGHE_TAB, 3, g_msg[1]);

    /* Le azioni, una riga di pulsanti. */
    for (i = 0, c = 3; i < N_AZ; i++) {
        o(i == az ? C_SCELTO : C_BOX);
        snprintf(b, sizeof(b), "[ %s ]", AZIONI[i]);
        sc_scrivi(g_righe - 2, c, b);
        c += (int)strlen(b) + 1;
    }
    o(C_TIT);
    sc_ripeti(g_righe, 1, ' ', g_cols - 1);
    sc_scrivi(g_righe, 2, "Su/Giu' partizione   Sinistra/Destra azione   Invio esegui   Esc esce");
    o(C_BOX);
    sc_va(g_righe - 2, 3);
}

/* Il tipo, da un elenco. Rende il byte, o -1. */
static int scegli_tipo(unsigned int attuale)
{
    static char        righe[N_TIPI + 1][64];
    static const char *voci[N_TIPI + 1];
    char b[8];
    int  i, s = 0, t;

    for (i = 0; i < N_TIPI; i++) {
        snprintf(righe[i], sizeof(righe[i]), "%02X  %-11s %s", TIPI[i].t, TIPI[i].nome, TIPI[i].uso);
        voci[i] = righe[i];
        if (TIPI[i].t == attuale) s = i;
    }
    snprintf(righe[N_TIPI], sizeof(righe[N_TIPI]), "..  Altro: il numero, in esadecimale");
    voci[N_TIPI] = righe[N_TIPI];

    s = scegli("Tipo della partizione", voci, N_TIPI + 1, s, 64);
    if (s < 0) return -1;
    if (s < N_TIPI) return TIPI[s].t;

    b[0] = '\0';
    if (chiedi_campo("Altro tipo", "Il byte del tipo, in esadecimale (01..FF):", b, 2, 1) <= 0)
        return -1;
    t = (int)a_esa(b);
    if (t == 0) { msg("Il tipo 0 vuol dire 'slot libero'.", 0); return -1; }
    if (t == 0xEE || tipo_esteso((unsigned)t)) {
        msg("Estese e GPT protettivo non si creano da qui:",
            "fdisk non scrive EBR, e 0xEE e' la tabella finta dei dischi GPT.");
        return -1;
    }
    return t;
}

static void az_nuova(const Riga *g)
{
    char b[12], m[100];
    int  slot = -1, i, t;
    unsigned int max, v, lung;

    if (!g || g->slot >= 0) { msg("Scegli con le frecce uno spazio libero, poi Nuova.", 0); return; }
    for (i = 0; i < PARTWRITE_MAX_VOCI; i++) if (!usata(i)) { slot = i; break; }
    if (slot < 0) { msg("I quattro posti delle primarie sono tutti occupati:", "cancellane una."); return; }

    max = in_mb(g->fine - g->inizio);
    snprintf(b, sizeof(b), "%u", max);
    snprintf(m, sizeof(m), "Dimensione in MB (al massimo %u, cioe' tutto lo spazio):", max);
    if (chiedi_campo("Nuova partizione", m, b, 7, 0) <= 0) return;
    v = a_numero(b);
    if (v == 0 || v > max) {
        snprintf(m, sizeof(m), "Ci stanno da 1 a %u MB, non %s.", max, b);
        msg(m, 0);
        return;
    }
    /* Tutto lo spazio = fino all'ultimo settore, non al MB intero: il resto
     * di meno di un MB resterebbe un buco che nessuno usera'. */
    lung = (v == max) ? g->fine - g->inizio : v * SETT_PER_MB;

    t = scegli_tipo(TIPO_PREDEFINITO);
    if (t < 0) return;
    if (si_sovrappone(slot, g->inizio, lung)) { msg("Si sovrappone a una partizione: annullato.", 0); return; }

    g_tab.voce[slot].attiva     = 0;
    g_tab.voce[slot].tipo       = (unsigned char)t;
    g_tab.voce[slot].inizio_lo  = g->inizio;
    g_tab.voce[slot].inizio_hi  = 0;
    g_tab.voce[slot].settori_lo = lung;
    g_tab.voce[slot].settori_hi = 0;
    g_modificata = 1;

    snprintf(m, sizeof(m), "Creata hd%up%d (%s, %u MB). Si scrive sul disco con Scrivi.",
             g_disco, slot + 1, nome_tipo((unsigned)t), in_mb(lung));
    if (t == 0x82) {
        char m2[100];
        snprintf(m2, sizeof(m2), "Poi: mkswap hd%up%d, e swap = hd%up%d in /boot/kernel.cfg.",
                 g_disco, slot + 1, g_disco, slot + 1);
        msg(m, m2);
    } else {
        msg(m, "Non e' formattata: dopo Scrivi, mkfs.");
    }
}

static void az_cancella(const Riga *g)
{
    char m[64];

    if (!g || g->slot < 0) { msg("Scegli una partizione da cancellare.", 0); return; }
    if (tipo_esteso(g_tab.voce[g->slot].tipo) && g_n_logiche > 0) {
        msg("E' l'estesa che contiene le logiche: cancellarla le lascerebbe",
            "irraggiungibili. Per svuotare il disco c'e' Azzera.");
        return;
    }
    snprintf(m, sizeof(m), "Cancellare hd%up%d?", g_disco, g->slot + 1);
    if (!conferma("Cancella", m,
                  "Fino a Scrivi il disco non cambia. Dopo, i dati che",
                  "conteneva non si raggiungono piu'.")) return;
    memset(&g_tab.voce[g->slot], 0, sizeof(PartVoce));
    g_modificata = 1;
    msg("Cancellata dalla proposta: il disco cambia con Scrivi.", 0);
}

static void az_tipo(const Riga *g)
{
    int t;

    if (!g || g->slot < 0) { msg("Scegli una partizione di cui cambiare il tipo.", 0); return; }
    if (tipo_esteso(g_tab.voce[g->slot].tipo)) {
        msg("Il tipo di un'estesa non si cambia da qui.", 0);
        return;
    }
    t = scegli_tipo(g_tab.voce[g->slot].tipo);
    if (t < 0) return;
    g_tab.voce[g->slot].tipo = (unsigned char)t;
    g_modificata = 1;
    msg("Tipo cambiato. E' solo un'etichetta: dentro la partizione non cambia",
        "niente (per lo scambio serve comunque mkswap).");
}

static void az_avvio(const Riga *g)
{
    int i;

    if (!g || g->slot < 0) { msg("Scegli la partizione da cui si avvia.", 0); return; }
    if (g_tab.voce[g->slot].attiva == 0x80) {
        g_tab.voce[g->slot].attiva = 0;
        msg("Non e' piu' avviabile.", 0);
    } else {
        /* Una sola attiva: vedi cmd_attiva. */
        for (i = 0; i < PARTWRITE_MAX_VOCI; i++) g_tab.voce[i].attiva = 0;
        g_tab.voce[g->slot].attiva = 0x80;
        msg("E' la partizione avviabile (ne puo' essere una sola).", 0);
    }
    g_modificata = 1;
}

static void az_azzera(void)
{
    int i;

    if (!conferma("Azzera", "Togliere TUTTE le partizioni della tabella?",
                  g_n_logiche > 0 ? "! La catena delle logiche resta orfana sul disco." :
                                    "I dati non si cancellano: sparisce la mappa.",
                  "Fino a Scrivi il disco non cambia.")) return;
    for (i = 0; i < PARTWRITE_MAX_VOCI; i++) memset(&g_tab.voce[i], 0, sizeof(PartVoce));
    g_modificata = 1;
    if (g_n_logiche > 0)
        msg("Con le logiche si scrive in due giri: ora Scrivi (tabella vuota),",
            "poi si rientra in fdisk per creare le nuove. Vedi cmd_azzera.");
    else
        msg("Tabella azzerata nella proposta.", 0);
}

static void az_scrivi(void)
{
    char m[100];
    int  res;

    if (!g_modificata) { msg("Non e' cambiato niente: non c'e' niente da scrivere.", 0); return; }
    snprintf(m, sizeof(m), "Scrivere la nuova tabella su hd%u?", g_disco);
    if (!conferma("Scrivi", m,
                  "Sostituisce quella sul disco. I dati di una partizione",
                  "cancellata non si raggiungono piu'.")) return;

    g_tab.problemi = 0;
    res = partwrite(g_disco, &g_tab);
    if (res == 0) {
        g_modificata = 0;
        msg("Tabella scritta: le partizioni ci sono gia', senza riavviare.",
            "Quelle nuove vanno formattate: mkfs (dati), mkswap (scambio).");
        return;
    }
    switch (-res) {
    case 16: msg("Rifiutata: una partizione del disco e' montata. Smontala, o",
                 "se e' la radice avvia da floppy o da CD."); break;
    case 1:  msg("Rifiutata: il disco e' GPT.", 0); break;
    case 19: msg("Il disco non risponde piu'.", 0); break;
    case 5:  msg("Errore di I/O: il disco non ha accettato la scrittura.", 0); break;
    case 22:
        msg((g_tab.problemi & PT_PROB_SOVRAPP)    ? "Rifiutata: due partizioni si sovrappongono." :
            (g_tab.problemi & PT_PROB_OLTRE_FINE) ? "Rifiutata: una partizione va oltre la fine del disco." :
            (g_tab.problemi & PT_PROB_TROPPE_EXT) ? "Rifiutata: piu' di una partizione estesa." :
                                                    "Rifiutata dal kernel: la tabella non e' valida.",
            "Il disco non e' stato toccato.");
        break;
    default:
        snprintf(m, sizeof(m), "Scrittura fallita (errore %d): il disco non e' stato toccato.", res);
        msg(m, 0);
        break;
    }
}

/* Rende 1 se si esce. */
static int esegui(int az, const Riga *g)
{
    switch (az) {
    case AZ_NUOVA:    az_nuova(g);    break;
    case AZ_CANCELLA: az_cancella(g); break;
    case AZ_TIPO:     az_tipo(g);     break;
    case AZ_AVVIO:    az_avvio(g);    break;
    case AZ_AZZERA:   az_azzera();    break;
    case AZ_SCRIVI:   az_scrivi();    break;
    case AZ_ESCI:
        return !g_modificata ||
               conferma("Esci", "Uscire senza scrivere?",
                        "Le modifiche non scritte si perdono:",
                        "il disco resta com'era.");
    }
    return 0;
}

static void schermo_fine(void)
{
    kbd_modo(KBD_MODE_COOKED);
    o(C_NORM);
    o("\033[2J");
    sc_va(1, 1);
}

static int a_schermo(void)
{
    int sel = 0, az = AZ_NUOVA;

    sc_misura();
    kbd_modo(KBD_MODE_RAW);
    if (g_montata)
        msg("! Il disco ha partizioni montate: il kernel non lo riscrive",
            "finche' lo sono (smontale, o avvia da floppy o da CD).");
    else if (g_nota)
        msg(g_nota, 0);

    for (;;) {
        unsigned k;

        fai_righe();
        if (sel >= g_n_righe) sel = g_n_righe - 1;
        if (sel < 0) sel = 0;
        /* Sullo spazio libero l'azione che serve e' quasi sempre Nuova. */
        disegna(sel, az);
        k = tasto();
        switch (k) {
        case KBD_K_UP:    if (sel > 0) sel--; break;
        case KBD_K_DOWN:  if (sel < g_n_righe - 1) sel++; break;
        case KBD_K_LEFT:  az = (az + N_AZ - 1) % N_AZ; break;
        case KBD_K_RIGHT: az = (az + 1) % N_AZ; break;
        case KBD_K_HOME:  sel = 0; break;
        case KBD_K_END:   sel = g_n_righe - 1; break;
        case 0x1B:        az = AZ_ESCI; /* e si chiede come con il pulsante */
                          /* fallthrough */
        case '\r': case '\n':
            msg(0, 0);
            if (esegui(az, g_n_righe ? &g_riga[sel] : 0)) { schermo_fine(); return 0; }
            break;
        case 0:           schermo_fine(); return 1;    /* la tastiera non risponde */
        default: break;
        }
    }
}

/* fdisk senza disco, sulla console: si sceglie dall'elenco. -1 = Esc. */
static int scegli_disco(void)
{
    static char        righe[4][72];
    static const char *voci[4];
    unsigned int       idx[4], i;
    int                n = 0, s;
    DiskInfo           d;

    for (i = 0; i < 4; i++) {
        char m[16];

        if (diskinfo(i, &d) < 0 || !d.presente || d.tipo != 1) continue;
        mb_testo(m, sizeof(m), in_mb(d.settori_lo));
        snprintf(righe[n], sizeof(righe[n]), "hd%u  %-10s %-28.28s %s", i, m, d.modello,
                 d.schema == PT_SCHEMA_GPT ? "GPT" : d.schema == PT_SCHEMA_MBR ? "MBR" : "vuoto");
        voci[n] = righe[n];
        idx[n++] = i;
    }
    if (n == 0) return elenca_dischi() ? -1 : -1;

    sc_misura();
    kbd_modo(KBD_MODE_RAW);
    o(C_BOX);
    o("\033[2J");
    o(C_TIT);
    sc_ripeti(1, 1, ' ', g_cols);
    sc_scrivi(1, 3, "fdisk " FD_VERSION "  -  quale disco?");
    s = scegli("Dischi", voci, n, 0, 70);
    schermo_fine();
    return s < 0 ? -1 : (int)idx[s];
}

/* =============================================================================
 * main
 * ============================================================================= */
int main(int argc, char **argv)
{
    char riga[RIGA_MAX];
    unsigned int idx;
    const char  *disco = 0;
    int          testo = 0, schermo, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) testo = 1;
        else disco = argv[i];
    }
    schermo = !testo && puo_schermo();

    if (!disco) {
        int s;

        if (!schermo) return elenca_dischi();
        s = scegli_disco();
        if (s < 0) return 0;
        idx = (unsigned int)s;
    } else {
        const char *a = disco;
        if (a[0] == 'h' && a[1] == 'd') a += 2;
        if (a[0] < '0' || a[0] > '9' || a[1] != '\0') {
            printf("fdisk: '%s' non e' un disco. Usa hd0, hd1, ...\n", disco);
            printf("Lancia `fdisk` senza argomenti per l'elenco.\n");
            return 1;
        }
        idx = (unsigned int)(a[0] - '0');
    }

    if (idx >= 4) {
        printf("fdisk: gli slot IDE sono hd0..hd3.\n");
        return 1;
    }

    printf("%s %s - partizionatore MBR di EX-OS\n", FD_NAME, FD_VERSION);
    printf("Niente viene scritto sul disco fino al comando `w`.\n\n");

    if (carica(idx) != 0) return 1;
    controlla_montaggi();
    if (schermo) return a_schermo();

    mostra();
    printf("`h` per l'elenco dei comandi.\n");

    for (;;) {
        printf("fdisk hd%u> ", g_disco);
        if (leggi_riga(riga, RIGA_MAX) < 0) {
            printf("\nFine input: esco senza scrivere.\n");
            return 0;
        }
        if (vuota(riga)) continue;

        switch (riga[0]) {
            case 'p': mostra();       break;
            case 'n': cmd_nuova();    break;
            case 'd': cmd_cancella(); break;
            case 'z': cmd_azzera();   break;
            case 't': cmd_tipo();     break;
            case 'a': cmd_attiva();   break;
            case 'w': cmd_scrivi();   break;
            case 'h':
            case '?': cmd_aiuto();    break;

            case 'q':
                if (g_modificata) {
                    printf("Hai modifiche non scritte: esco senza toccare il "
                           "disco.\n");
                }
                printf("Uscita.\n");
                return 0;

            default:
                printf("Comando '%c' sconosciuto. `h` per l'elenco.\n", riga[0]);
                break;
        }
    }
}

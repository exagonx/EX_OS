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
 *   fdisk              elenca i dischi
 *   fdisk hd0          apre la sessione sul disco 0
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

/* =============================================================================
 * Identità del programma
 *
 * ▲ INCREMENTARE FD_VERSION DI 0.001 A OGNI MODIFICA ▲
 * Stessa regola del kernel e di /bin/textline: stringa con tre decimali,
 * non un numero — EX-OS non usa la virgola mobile.
 * ============================================================================= */
#define FD_NAME     "fdisk"
#define FD_VERSION  "0.001"

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

/* =============================================================================
 * Utilita'
 * ============================================================================= */

static const char *nome_tipo(unsigned int t)
{
    switch (t) {
        case 0x00: return "-";
        case 0x01: return "FAT12";
        case 0x04: return "FAT16 <32M";
        case 0x05: return "estesa CHS";
        case 0x06: return "FAT16";
        case 0x07: return "NTFS/exFAT";
        case 0x0B: return "FAT32 CHS";
        case 0x0C: return "FAT32 LBA";
        case 0x0E: return "FAT16 LBA";
        case 0x0F: return "estesa LBA";
        case 0x82: return "swap Linux";
        case 0x83: return "Linux";
        case 0x85: return "estesa Linux";
        case 0xEE: return "GPT protett.";
        case 0xEF: return "EFI (FAT)";
        default:   return "sconosciuto";
    }
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
    } else if (d.problemi != 0) {
        printf("ATTENZIONE: la tabella attuale di hd%u ha delle anomalie.\n", idx);
        printf("Lancia `disk` per vederle in dettaglio PRIMA di modificarla:\n");
        printf("una tabella malata puo' descrivere dati ancora recuperabili.\n\n");
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
        printf("Smontale con `umount`, oppure — se una di quelle e' la root —\n");
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
    printf("  06 FAT16   0B FAT32 CHS   0C FAT32 LBA   0E FAT16 LBA   83 Linux\n");
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
    printf("  06 FAT16   0B FAT32 CHS   0C FAT32 LBA   0E FAT16 LBA   83 Linux\n");
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

    printf("\nuso: fdisk hd<n>\n");
    return 0;
}

/* =============================================================================
 * main
 * ============================================================================= */
int main(int argc, char **argv)
{
    char riga[RIGA_MAX];
    unsigned int idx;

    if (argc < 2) return elenca_dischi();

    /* "hd0", oppure "0" */
    {
        const char *a = argv[1];
        if (a[0] == 'h' && a[1] == 'd') a += 2;
        if (a[0] < '0' || a[0] > '9' || a[1] != '\0') {
            printf("fdisk: '%s' non e' un disco. Usa hd0, hd1, ...\n", argv[1]);
            printf("Lancia `fdisk` senza argomenti per l'elenco.\n");
            return 1;
        }
        idx = (unsigned int)(a[0] - '0');
    }

    if (idx >= 4) {
        printf("fdisk: gli slot IDE sono hd0..hd3.\n");
        return 1;
    }

    printf("%s %s — partizionatore MBR di EX-OS\n", FD_NAME, FD_VERSION);
    printf("Niente viene scritto sul disco fino al comando `w`.\n\n");

    if (carica(idx) != 0) return 1;
    controlla_montaggi();

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

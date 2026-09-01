/* =============================================================================
 * drivers/audio/audio_comune.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LA META' COMUNE DI OGNI DRIVER AUDIO — anello, protocollo, prove, main()
 *
 * Qui c'e' tutto cio' che non dipende dalla scheda: la zona di memoria
 * condivisa, la copia dall'anello al buffer del DMA, il ciclo del server, e
 * le prove di collaudo che `audio -i` chiede appena trovata la scheda.
 * I registri stanno nell'altra meta' — vedi audio_dorso.h.
 *
 * -----------------------------------------------------------------------------
 * ! IL PERCORSO CALDO, per intero, in sei righe
 *
 *   il client scrive i campioni nell'anello e alza `scritto`
 *   la scheda finisce una meta' del proprio buffer e alza l'IRQ
 *   il kernel lo consegna come messaggio IPC
 *   il dorso azzera lo stato della scheda e dice quale meta' si e' liberata
 *   il comune ci copia dentro i byte dell'anello e alza `suonato`
 *   irq_done() riapre la linea
 *
 * Non c'e' altro, e non deve entrarci altro: ogni cosa aggiunta qui la si
 * paga a ogni meta' buffer, decine di volte al secondo, dentro il tempo che
 * separa il suono dal ronzio.
 *
 * -----------------------------------------------------------------------------
 * ! DURANTE UNA PROVA IL DRIVER E' ANCHE IL PROPRIO CLIENT
 *
 * `scritto` e' il campo del client, e nel resto della vita del driver non si
 * tocca. Durante una prova di collaudo pero' il client non esiste: `audio -i`
 * gira sulla macchina appena accesa, dove non c'e' un file .wav da suonare
 * ne' un programma che lo suoni. Allora il driver genera i campioni e li
 * immette nell'anello esattamente come farebbe un gioco — stesse formule,
 * stesso ordine, stesso anello. E' apposta: una prova che seguisse un
 * percorso diverso da quello vero non proverebbe il percorso vero.
 * ============================================================================= */

#include "libc.h"
#include "audio_proto.h"
#include "audio_dorso.h"

/* =============================================================================
 * Stato del servizio
 * ========================================================================== */
static const AudioDorso *D;

static AudioInfo      g_info;
static AudioFormato   g_fmt;

static AudioAnello   *g_anello = 0;     /* in cima alla zona condivisa */
static unsigned char *g_dati   = 0;     /* i campioni, dentro la stessa zona */
static void          *g_zona   = 0;     /* l'inizio della zona, per shm_chiudi */

static unsigned char *g_dma      = 0;   /* il buffer che legge la scheda */
static unsigned int   g_dma_byte = 0;
static unsigned int   g_meta     = 0;   /* g_dma_byte / 2 */

static unsigned int   g_stato   = AUDIO_ST_CHIUSO;
static unsigned int   g_client  = 0;    /* chi ha aperto l'anello */
static unsigned int   g_irq_n   = 0;    /* interrupt contati da via() */
static unsigned int   g_via_ms  = 0;
static unsigned int   g_volume  = 80;

/* =============================================================================
 * IL SINTETIZZATORE SOFTWARE — perche' il MIDI non e' un privilegio dell'ISA
 *
 * Una Sound Blaster ha l'OPL a bordo: il MIDI lo suona il silicio. Una scheda
 * PCI — ES1371, AC'97, HD Audio — NON HA NESSUN SINTETIZZATORE. Ha una porta
 * MIDI (una UART) a cui si puo' attaccare una tastiera, e basta: se non c'e'
 * niente attaccato, i byte escono e non li sente nessuno.
 *
 * ! ALLORA IL MIDI SU UNA SCHEDA MODERNA O LO FA IL PROCESSORE O NON ESISTE.
 * E' cosi' su qualunque sistema operativo di oggi, ed e' il motivo per cui
 * questa parte sta nella meta' COMUNE e non dentro un driver: le note si
 * traducono in campioni, e i campioni li sa suonare qualunque scheda che
 * faccia PCM — cioe' tutte.
 *
 * Quello che c'e' qui e' minimo e dichiarato: otto voci, un'onda di seno con
 * una terza armonica per non sembrare un fischio, attacco immediato e
 * spegnimento esponenziale. Non e' un campionatore e non legge SoundFont: e'
 * la differenza fra «il MIDI si sente» e «il MIDI non si sente», che e' la
 * sola differenza che conta finche' qualcuno non scrive la tavola d'onde.
 *
 * ! IL SINTETIZZATORE SI PRENDE L'USCITA PCM, e non puo' essere altrimenti:
 * la scheda ha un flusso solo. Se un programma sta gia' suonando, il MIDI
 * viene ignorato invece di interrompere cio' che si sente — mescolarli
 * vorrebbe dire un mixer, che e' un lavoro vero e non una riga.
 * ========================================================================== */
#define VOCI_MAX        8
#define SINT_RATE       22050
#define SINT_CODA_MS    1500    /* dopo l'ultima nota, quanto si resta accesi */

typedef struct {
    unsigned int attiva;
    unsigned int nota;
    unsigned int canale;
    unsigned int fase;          /* 16.16 dentro la tavola del seno */
    unsigned int passo;
    unsigned int ampiezza;      /* 0..65535, scende da sola */
    unsigned int calo;          /* quanto scende a ogni campione */
} Voce;

static Voce         g_voci[VOCI_MAX];
static unsigned int g_sint_attivo = 0;
static unsigned int g_sint_ultimo = 0;      /* uptime dell'ultimo evento */

/* =============================================================================
 * Il silenzio non e' zero — dipende da come sono fatti i campioni
 *
 * A 8 bit i campioni sono SENZA SEGNO: il centro e' 0x80, e riempire di zeri
 * vorrebbe dire tenere il cono dell'altoparlante tutto da una parte. Si sente
 * come uno schiocco all'inizio e uno alla fine di ogni suono. A 16 bit sono
 * con segno e il centro e' davvero 0.
 * ========================================================================== */
static int silenzio(void)
{
    return (g_fmt.bit == 8) ? 0x80 : 0x00;
}

/* =============================================================================
 * riempi_meta — dall'anello del client al buffer della scheda
 *
 * ! DUE memcpy E NON UN CICLO DI BYTE. L'anello gira, quindi i byte che
 * servono possono stare a cavallo della fine: sono al massimo due tratti
 * contigui. Copiarli byte per byte con un AND a ogni passo costava, misurato
 * su una meta' da 16 KB, piu' del tempo che separa due interrupt a 44100 Hz
 * su una macchina lenta — cioe' il ronzio.
 * ========================================================================== */
static void sint_genera(unsigned char *dst, unsigned int byte);
static void sint_tutte_giu(void);

static void riempi_meta(unsigned int h)
{
    unsigned char *dst = g_dma + h * g_meta;
    unsigned int   n   = g_meta;
    unsigned int   pronti, primo, testa;

    /* ! IL SINTETIZZATORE SCAVALCA L'ANELLO. Quando suona lui non c'e' un
     * client che riempie: le note diventano campioni qui, un pezzo per volta,
     * e l'anello non c'entra. Vedi il blocco del sintetizzatore piu' sopra. */
    if (g_sint_attivo) { sint_genera(dst, n); return; }

    if (!g_anello) { memset(dst, silenzio(), n); return; }

    pronti = AUDIO_PIENO(g_anello);
    if (pronti > n) pronti = n;

    if (pronti > 0) {
        testa = g_anello->suonato & (g_anello->byte - 1);
        primo = g_anello->byte - testa;          /* fino alla fine dell'anello */
        if (primo > pronti) primo = pronti;

        memcpy(dst, g_dati + testa, primo);
        if (pronti > primo) memcpy(dst + primo, g_dati, pronti - primo);

        /* ! SI ALZA DOPO AVER COPIATO. `suonato` e' il permesso che il driver
         * da' al client di riscrivere quei byte: alzarlo prima vorrebbe dire
         * autorizzarlo a sovrascrivere campioni che stiamo ancora leggendo. */
        g_anello->suonato += pronti;
    }

    if (pronti < n) {
        memset(dst + pronti, silenzio(), n - pronti);
        /* Sottoflusso: il client non ha tenuto il passo. Si conta e si va
         * avanti — fermare il DMA farebbe uno schiocco e costringerebbe a
         * riavviarlo, che e' peggio di un buco di silenzio. */
        if (g_stato == AUDIO_ST_SUONA) g_anello->sottoflussi++;
    }
}

/* =============================================================================
 * La zona condivisa
 * ========================================================================== */
static int zona_apri(void)
{
    ShmZona z;

    if (g_anello) return 0;                     /* gia' aperta */

    memset(&z, 0, sizeof(z));
    strcpy(z.nome, AUDIO_ZONA);
    z.byte = AUDIO_ZONA_BYTE;
    z.flag = SHM_CREA;

    if (shm_apri(&z) < 0) return -1;

    g_zona   = (void *)z.virt;
    g_anello = (AudioAnello *)z.virt;
    g_dati   = (unsigned char *)z.virt + AUDIO_DATI_OFF;

    memset(g_anello, 0, sizeof(*g_anello));
    g_anello->magia    = AUDIO_ANELLO_MAGIA;
    g_anello->byte     = AUDIO_ANELLO_BYTE;
    g_anello->dati_off = AUDIO_DATI_OFF;
    memset(g_dati, 0x80, AUDIO_ANELLO_BYTE);
    return 0;
}

static void zona_chiudi(void)
{
    if (!g_zona) return;
    shm_chiudi(g_zona);
    g_zona = 0; g_anello = 0; g_dati = 0;
}

/* =============================================================================
 * apri / via / ferma / chiudi — il ciclo di vita di una riproduzione
 * ========================================================================== */
static int fai_apri(AudioFormato *f, AudioEsito *out)
{
    memset(out, 0, sizeof(*out));

    /* ! IL SINTETIZZATORE MOLLA L'USCITA A CHI APRE DAVVERO. Senza questa
     * riga un programma che chiede di suonare mentre una nota MIDI sta
     * ancora spegnendosi otteneva l'anello, ci scriveva dentro, e non
     * sentiva niente: riempi_meta continuava a generare le voci invece di
     * copiare i suoi campioni. Il buffer era pieno di musica di qualcun
     * altro, e nessun contatore lo diceva. */
    if (g_sint_attivo) {
        g_sint_attivo = 0;
        sint_tutte_giu();
    }

    if (g_stato == AUDIO_ST_SUONA) D->ferma();

    g_fmt = *f;
    if (D->apri(&g_fmt, &g_dma, &g_dma_byte) < 0) {
        out->esito = -EINVAL;
        return -1;
    }
    g_meta = g_dma_byte / 2;

    if (zona_apri() < 0) {
        out->esito = -ENOMEM;
        return -1;
    }

    /* I contatori ripartono: l'anello e' lo stesso, la riproduzione no. */
    g_anello->scritto     = 0;
    g_anello->suonato     = 0;
    g_anello->sottoflussi = 0;
    g_anello->rate        = g_fmt.rate;
    g_anello->canali      = g_fmt.canali;
    g_anello->bit         = g_fmt.bit;
    g_anello->stato       = AUDIO_ST_PRONTO;
    memset(g_dati, silenzio(), AUDIO_ANELLO_BYTE);
    memset(g_dma,  silenzio(), g_dma_byte);

    g_stato = AUDIO_ST_PRONTO;
    g_irq_n = 0;

    out->esito       = 0;
    out->formato     = g_fmt;
    strcpy(out->zona, AUDIO_ZONA);
    out->zona_byte   = AUDIO_ZONA_BYTE;
    out->anello_byte = AUDIO_ANELLO_BYTE;
    out->dati_off    = AUDIO_DATI_OFF;
    return 0;
}

static void fai_via(void)
{
    if (g_stato == AUDIO_ST_CHIUSO || g_stato == AUDIO_ST_SUONA) return;

    /* ! LE DUE META' SI RIEMPIONO PRIMA DI PARTIRE. Avviare il DMA su un
     * buffer non ancora riempito vuol dire che la prima cosa che esce
     * dall'altoparlante e' cio' che c'era in quella pagina — e non e'
     * silenzio, e' rumore, ed e' forte. */
    riempi_meta(0);
    riempi_meta(1);

    g_irq_n  = 0;
    g_via_ms = uptime_ms();
    g_stato  = AUDIO_ST_SUONA;
    g_anello->stato = AUDIO_ST_SUONA;
    D->via();
}

static void fai_ferma(void)
{
    if (g_stato != AUDIO_ST_SUONA) return;
    D->ferma();
    g_stato = AUDIO_ST_FERMO;
    if (g_anello) g_anello->stato = AUDIO_ST_FERMO;
}

static void fai_chiudi(void)
{
    fai_ferma();
    D->chiudi();
    g_stato = AUDIO_ST_CHIUSO;
    if (g_anello) g_anello->stato = AUDIO_ST_CHIUSO;
    g_client = 0;
}

/* =============================================================================
 * IL GENERATORE — i campioni della prova, senza virgola mobile
 *
 * ! NIENTE sin(). Un driver e' collegato staticamente alla libc: tirarsi
 * dentro la matematica in virgola mobile per un tono di prova significa
 * qualche decina di KB in piu' su un floppy da 1.44 MB, e una FPU che sulle
 * macchine a cui questo sistema punta puo' anche non esserci.
 *
 * Al suo posto una tavola di 64 punti di seno, in numeri interi da -100 a
 * +100, percorsa con un accumulatore di fase in virgola FISSA: la frequenza
 * si ottiene sommando (64 * 65536 * hz / rate) a ogni campione e guardando i
 * 6 bit alti della parte intera. E' la stessa aritmetica di un sintetizzatore
 * vero, e sbaglia l'intonazione di meno di un centesimo di semitono.
 * ========================================================================== */
static const signed char g_seno[64] = {
       0,   10,   20,   29,   38,   47,   56,   63,
      71,   77,   83,   88,   92,   96,   98,  100,
     100,  100,   98,   96,   92,   88,   83,   77,
      71,   63,   56,   47,   38,   29,   20,   10,
       0,  -10,  -20,  -29,  -38,  -47,  -56,  -63,
     -71,  -77,  -83,  -88,  -92,  -96,  -98, -100,
    -100, -100,  -98,  -96,  -92,  -88,  -83,  -77,
     -71,  -63,  -56,  -47,  -38,  -29,  -20,  -10,
};

static unsigned int g_fase;     /* 16.16 dentro la tavola */
static unsigned int g_passo;
static unsigned int g_ampiezza; /* 0..100 */

static void gen_inizia(unsigned int hz, unsigned int ampiezza)
{
    g_fase     = 0;
    g_ampiezza = ampiezza;

    /* passo = 64 * 65536 * hz / rate.
     *
     * ! TUTTO A 32 BIT, E NON PER ELEGANZA. Un driver e' collegato senza
     * libgcc: una divisione a 64 bit diventa una chiamata a __udivdi3 che
     * non esiste, e il collegamento fallisce. Qui non serve: 64*65536 e'
     * 4194304, e per un tono di prova sotto i 1000 Hz il prodotto sta
     * largamente dentro i 32 bit. Sopra si taglia invece di traboccare —
     * un tono di prova a piu' di 1 kHz non l'ha chiesto nessuno. */
    if (hz > 1000) hz = 1000;
    g_passo    = (hz * 4194304u) / g_fmt.rate;
}

/* Immette `byte` campioni nell'anello, o quanti ce ne stanno. Rende quanti
 * ne ha immessi davvero. */
static unsigned int gen_versa(unsigned int byte)
{
    unsigned int libero = AUDIO_LIBERO(g_anello);
    unsigned int passo_campione = (g_fmt.bit / 8) * g_fmt.canali;
    unsigned int fatti = 0;
    unsigned int idx;
    int          v;

    if (byte > libero) byte = libero;
    byte -= byte % passo_campione;          /* mai mezzo campione */

    while (fatti < byte) {
        v = g_seno[(g_fase >> 16) & 63] * (int)g_ampiezza;   /* -10000..10000 */
        g_fase += g_passo;

        idx = (g_anello->scritto + fatti) & (g_anello->byte - 1);

        if (g_fmt.bit == 8) {
            int c = 128 + (v / 100);                          /* 28..228 */
            g_dati[idx] = (unsigned char)c;
            fatti++;
            if (g_fmt.canali == 2) {
                g_dati[(idx + 1) & (g_anello->byte - 1)] = (unsigned char)c;
                fatti++;
            }
        } else {
            int c = v * 3;                                    /* -30000..30000 */
            unsigned int i2 = idx;
            unsigned int k;
            for (k = 0; k < g_fmt.canali; k++) {
                g_dati[i2 & (g_anello->byte - 1)]       = (unsigned char)(c & 0xFF);
                g_dati[(i2 + 1) & (g_anello->byte - 1)] = (unsigned char)((c >> 8) & 0xFF);
                i2 += 2;
                fatti += 2;
            }
        }
    }

    /* ! SI ALZA ALLA FINE, quando i campioni ci sono gia'. E' la meta' di
     * client della regola scritta in audio_proto.h, e vale anche quando il
     * client e' il driver stesso. */
    g_anello->scritto += fatti;
    return fatti;
}

/* =============================================================================
 * IL SINTETIZZATORE SOFTWARE — il corpo
 *
 * La frequenza di una nota MIDI: 440 Hz e' la nota 69, e ogni semitono
 * moltiplica per la radice dodicesima di due. Senza virgola mobile si fa con
 * UNA TABELLA DI DODICI e uno spostamento di bit: la tabella tiene le dodici
 * note dell'ottava piu' alta in millesimi di Hz, e per scendere di un'ottava
 * si divide per due. La precisione che resta e' meglio di un centesimo di
 * semitono, cioe' meglio di quanto un orecchio distingua.
 * ========================================================================== */

/* do..si dell'ottava che contiene la nota 120 (do9), in centesimi di Hz.
 * Da qui in giu' si divide per due a ogni ottava. */
static const unsigned int g_hz_ottava[12] = {
     837202,  886984,  939727,  995606, 1054808, 1117530,
    1183982, 1254385, 1328975, 1408000, 1491724, 1580427
};

static unsigned int nota_hz(unsigned int nota)
{
    unsigned int hz;
    int          giu;

    if (nota > 127) nota = 127;
    hz  = g_hz_ottava[nota % 12] / 100;         /* Hz nell'ottava di do9 */
    giu = 10 - (int)(nota / 12);                /* quante ottave scendere */
    while (giu > 0 && hz > 1) { hz >>= 1; giu--; }
    return hz ? hz : 1;
}

static void sint_nota_su(unsigned int canale, unsigned int nota, unsigned int vel)
{
    unsigned int i, libera = VOCI_MAX, hz;

    /* La stessa nota sullo stesso canale ricomincia invece di sommarsi:
     * un file che manda due «nota su» senza il «nota giu' » in mezzo — e
     * ce ne sono — raddoppierebbe il volume di quella nota sola. */
    for (i = 0; i < VOCI_MAX; i++) {
        if (g_voci[i].attiva && g_voci[i].nota == nota &&
            g_voci[i].canale == canale) { libera = i; break; }
    }
    if (libera == VOCI_MAX)
        for (i = 0; i < VOCI_MAX; i++) if (!g_voci[i].attiva) { libera = i; break; }
    if (libera == VOCI_MAX) {
        /* Tutte occupate: si scaccia la piu' spenta, che e' quella che si
         * sente di meno — l'unica scelta che non fa sparire una nota viva. */
        unsigned int peggio = 0;
        for (i = 1; i < VOCI_MAX; i++)
            if (g_voci[i].ampiezza < g_voci[peggio].ampiezza) peggio = i;
        libera = peggio;
    }

    hz = nota_hz(nota);
    /* ! IL PASSO SI CALCOLA SULLA FREQUENZA CONCESSA, non su SINT_RATE. Una
     * AC'97 non fa 22050 Hz: e' fissa a 48000, e chiedere 22050 ottiene 48000.
     * Un passo calcolato sul numero CHIESTO invece che su quello CONCESSO fa
     * suonare tutto una quinta piu' in basso — cioe' stonato in modo
     * uniforme, che e' il tipo di errore che si sente e non si spiega. */
    if (hz > g_fmt.rate / 3) hz = g_fmt.rate / 3;   /* niente alias */

    g_voci[libera].attiva   = 1;
    g_voci[libera].nota     = nota;
    g_voci[libera].canale   = canale;
    g_voci[libera].fase     = 0;
    g_voci[libera].passo    = (hz * 4194304u) / (g_fmt.rate ? g_fmt.rate : SINT_RATE);
    g_voci[libera].ampiezza = 8000 + vel * 400;              /* 0..~60000 */
    /* Spegnimento in circa un secondo e mezzo: una nota che non finisce mai
     * impasta tutto, una che finisce subito sembra un clic. */
    g_voci[libera].calo     = 20;

    g_sint_ultimo = uptime_ms();
}

static void sint_nota_giu(unsigned int canale, unsigned int nota)
{
    unsigned int i;

    for (i = 0; i < VOCI_MAX; i++) {
        if (g_voci[i].attiva && g_voci[i].nota == nota &&
            g_voci[i].canale == canale) {
            /* ! NON SI SPEGNE DI COLPO. Troncare un'onda a meta' periodo fa
             * un gradino, e un gradino e' uno schiocco: si accelera il calo
             * e la nota si spegne in qualche millisecondo. */
            g_voci[i].calo = 400;
        }
    }
    g_sint_ultimo = uptime_ms();
}

static void sint_tutte_giu(void)
{
    unsigned int i;
    for (i = 0; i < VOCI_MAX; i++) g_voci[i].attiva = 0;
}

/* ! SCRIVE NEL FORMATO CONCESSO, NON IN UNO SUO. Il sintetizzatore chiede
 * 22050 Hz mono a 8 bit perche' gli basta, ma una AC'97 concede 48000 stereo
 * a 16 bit e non tratta. Scrivere byte a 8 bit dentro un buffer che la scheda
 * legge a 16 produce rumore alla velocita' sbagliata — ed e' il genere di
 * guasto che sembra un problema del driver della scheda invece che di chi ha
 * riempito il buffer. */
static void sint_genera(unsigned char *dst, unsigned int byte)
{
    unsigned int i = 0, v;
    unsigned int bpc = (g_fmt.bit / 8) * g_fmt.canali;   /* byte per campione */

    if (bpc == 0) bpc = 1;

    while (i + bpc <= byte) {
        int somma = 0;
        unsigned int c;

        for (v = 0; v < VOCI_MAX; v++) {
            int  onda;
            unsigned int f;

            if (!g_voci[v].attiva) continue;

            f = g_voci[v].fase;
            /* La fondamentale piu' un terzo di terza armonica: costa una
             * seconda lettura di tabella e toglie al suono l'aria di fischio
             * che ha un seno puro. */
            onda  = g_seno[(f >> 16) & 63];
            onda += g_seno[((f * 3) >> 16) & 63] / 3;

            /* ! IL DIVISORE TIENE CONTO DELLA POLIFONIA, e non e' un numero
             * a caso. L'onda arriva a ±133 e l'ampiezza a 255: senza freno una
             * voce sola satura da sola, e le note tenute insieme — cioe' un
             * accordo, cioe' quello che c'e' in qualunque file MIDI — escono
             * come un raschio. Con questo, una voce sta a poco piu' di un
             * terzo del fondo scala e tre ci stanno dentro. Il taglio qui
             * sotto resta lo stesso: e' l'ultima difesa, non la prima. */
            somma += (onda * (int)(g_voci[v].ampiezza >> 8)) / 700;

            g_voci[v].fase += g_voci[v].passo;
            if (g_voci[v].ampiezza > g_voci[v].calo)
                g_voci[v].ampiezza -= g_voci[v].calo;
            else
                { g_voci[v].ampiezza = 0; g_voci[v].attiva = 0; }
        }

        /* ! LA SOMMA SI TAGLIA, NON SI LASCIA TRABOCCARE. Otto voci insieme
         * superano il fondo scala, e un valore che gira da +127 a -128 non
         * suona forte: suona ROTTO, con un raschio che si sente piu' della
         * musica. */
        if (somma >  127) somma =  127;
        if (somma < -127) somma = -127;

        if (g_fmt.bit == 8) {
            for (c = 0; c < g_fmt.canali; c++) dst[i + c] = (unsigned char)(128 + somma);
        } else {
            int s16 = somma * 256;
            for (c = 0; c < g_fmt.canali; c++) {
                dst[i + c * 2]     = (unsigned char)(s16 & 0xFF);
                dst[i + c * 2 + 1] = (unsigned char)((s16 >> 8) & 0xFF);
            }
        }
        i += bpc;
    }

    /* La coda che non fa un campione intero: silenzio, non avanzo. */
    while (i < byte) dst[i++] = (unsigned char)silenzio();
}

/* Accende l'uscita PCM per il sintetizzatore, se non la sta usando nessuno. */
static int sint_accendi(void)
{
    AudioFormato f;
    AudioEsito   e;

    if (g_sint_attivo) return 0;

    /* ! UN PROGRAMMA CHE STA SUONANDO HA LA PRECEDENZA. La scheda ha un flusso
     * solo: il MIDI che si prende l'uscita interromperebbe cio' che si sente,
     * e chi ha mandato il file MIDI non lo saprebbe. */
    if (g_client != 0 && g_stato != AUDIO_ST_CHIUSO) return -1;

    f.rate = SINT_RATE; f.canali = 1; f.bit = 8;
    if (fai_apri(&f, &e) < 0) return -1;

    sint_tutte_giu();
    g_sint_attivo = 1;
    g_sint_ultimo = uptime_ms();
    fai_via();
    return 0;
}

static void sint_spegni_se_finito(void)
{
    unsigned int i;

    if (!g_sint_attivo) return;

    for (i = 0; i < VOCI_MAX; i++) if (g_voci[i].attiva) return;
    if (uptime_ms() - g_sint_ultimo < SINT_CODA_MS) return;

    g_sint_attivo = 0;
    fai_chiudi();
}

/* Il decodificatore MIDI comune: gli stessi messaggi che sb.c gira all'OPL,
 * qui diventano voci. Il running status c'e' perche' i file MIDI lo usano
 * quasi sempre — un file che sottintende lo stato e' la regola, non un caso. */
static unsigned char g_ms_stato = 0;
static unsigned char g_ms_arg[2];
static unsigned int  g_ms_n = 0;

static void sint_midi(const unsigned char *b, unsigned int n)
{
    unsigned int i;

    if (sint_accendi() < 0) return;

    for (i = 0; i < n; i++) {
        unsigned char c = b[i];

        if (c & 0x80) {
            if (c >= 0xF8) continue;            /* tempo reale: si ignora */
            g_ms_stato = c;
            g_ms_n     = 0;
            continue;
        }
        if (g_ms_stato == 0) continue;

        g_ms_arg[g_ms_n++] = c;
        {
            unsigned int quanti = ((g_ms_stato & 0xF0) == 0xC0 ||
                                   (g_ms_stato & 0xF0) == 0xD0) ? 1 : 2;
            if (g_ms_n < quanti) continue;
            g_ms_n = 0;

            switch (g_ms_stato & 0xF0) {
            case 0x90:
                if (g_ms_arg[1] == 0) sint_nota_giu(g_ms_stato & 0x0F, g_ms_arg[0]);
                else sint_nota_su(g_ms_stato & 0x0F, g_ms_arg[0], g_ms_arg[1]);
                break;
            case 0x80:
                sint_nota_giu(g_ms_stato & 0x0F, g_ms_arg[0]);
                break;
            case 0xB0:
                if (g_ms_arg[0] == 123 || g_ms_arg[0] == 120) sint_tutte_giu();
                break;
            default:
                break;
            }
        }
    }
}

/* =============================================================================
 * midi_manda — al silicio se ce n'e', al processore altrimenti
 *
 * ! IL BIVIO STA QUI E NON NEI DRIVER, apposta. Se ogni driver decidesse da
 * se' cosa fare di un byte MIDI, quello di una scheda senza sintetizzatore
 * dovrebbe portarsi dentro una copia del sintetizzatore software — e sarebbero
 * tre copie che divergono. Qui la domanda e' una sola: questa scheda ha un
 * sintetizzatore suo? Se si', i byte sono suoi; se no, diventano campioni.
 * ========================================================================== */
static void midi_manda(const unsigned char *b, unsigned int n)
{
    if (g_info.capacita & (AUDIO_CAP_MIDI_FM | AUDIO_CAP_MIDI_UART))
        D->midi(b, n);
    else
        sint_midi(b, n);
}

/* =============================================================================
 * Il servizio dell'interrupt
 * ========================================================================== */
static void servi_irq(unsigned int irq)
{
    int h = D->irq();

    if (h >= 0) {
        g_irq_n++;
        if (g_stato == AUDIO_ST_SUONA) riempi_meta((unsigned int)h);
    }
    irq_done(irq);

    /* Il sintetizzatore si spegne da solo quando le note sono finite e la
     * coda e' passata: qui e' l'unico posto che viene attraversato di
     * sicuro mentre suona. */
    sint_spegni_se_finito();
}

/* =============================================================================
 * LE PROVE DI COLLAUDO
 *
 * Un solo motore per tutte: apri il formato, genera, suona per N ms
 * continuando a servire gli interrupt, ferma, e riferisci cio' che
 * l'hardware ha detto di se'.
 * ========================================================================== */
static void prova_pcm(const AudioProva *p, AudioProvaEsito *e)
{
    AudioFormato f;
    AudioEsito   ap;
    unsigned int fine, inizio, ultimo_av, cambi = 0, av;
    unsigned int chunk;
    IpcMessage   meta;
    unsigned char buf[64];

    if (p->quale == AUDIO_PROVA_PCM16) {
        f.rate = 44100; f.canali = 2; f.bit = 16;
    } else {
        f.rate = 22050; f.canali = 1; f.bit = 8;
    }

    if (fai_apri(&f, &ap) < 0) {
        e->esito = ap.esito;
        strcpy(e->nota, "la scheda non accetta questo formato");
        return;
    }

    gen_inizia(440, p->muta ? 0 : 60);

    /* ! L'ANELLO SI RIEMPIE PRIMA DI PARTIRE, tutto. Una prova che parte con
     * l'anello vuoto misura il ritardo del generatore invece della scheda. */
    gen_versa(g_anello->byte);
    fai_via();

    inizio    = uptime_ms();
    ultimo_av = D->avanzamento();
    fine      = inizio + (p->ms ? p->ms : 600);

    /* Il chunk di rialimentazione: un decimo di anello per volta, cioe' il
     * comportamento di un gioco che scrive quando gli avanza tempo. */
    chunk = g_anello->byte / 8;

    while (uptime_ms() < fine) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 5) == 0) {
            if (meta.sender_pid == IPC_SENDER_KERNEL &&
                meta.tipo == IPC_TYPE_IRQ_NOTIFY) {
                unsigned int irq = 0;
                if (meta.len >= sizeof(irq)) memcpy(&irq, buf, sizeof(irq));
                servi_irq(irq);
            }
            /* Le richieste di altri client, durante una prova, si perdono.
             * E' accettabile: la prova dura meno di un secondo e succede
             * una volta sola, all'installazione, quando client non ce ne
             * sono. Rispondere «occupato» costerebbe di piu' di quanto
             * valga. */
        }

        gen_versa(chunk);

        av = D->avanzamento();
        if (av != ultimo_av) { cambi++; ultimo_av = av; }
    }

    e->ms          = uptime_ms() - inizio;
    e->irq         = g_irq_n;
    e->avanzato    = cambi;
    e->sottoflussi = g_anello->sottoflussi;

    fai_ferma();

    /* ! IL GIUDIZIO STA QUI, E NON NEL PROGRAMMA CHE CHIEDE. Chi ha scritto
     * nei registri e' l'unico che sa cosa doveva succedere; `audio` stampa
     * quello che gli si dice. */
    if (e->irq == 0 && e->avanzato == 0) {
        e->esito = -EIO;
        strcpy(e->nota, "la scheda non si e' mossa: ne' interrupt ne' DMA");
    } else if (e->irq == 0) {
        e->esito = -EIO;
        strcpy(e->nota, "il DMA gira ma l'interrupt non arriva: IRQ sbagliato");
    } else if (e->avanzato == 0) {
        e->esito = -EIO;
        strcpy(e->nota, "interrupt presenti ma il contatore DMA e' fermo");
    } else if (e->sottoflussi > 0) {
        e->esito = 0;
        strcpy(e->nota, "suona, ma la macchina fatica a tenere il passo");
    } else {
        e->esito = 0;
        strcpy(e->nota, "suona");
    }
}

/* Una scaletta breve: do, mi, sol, do. Se c'e' una MPU-401 la suona il
 * sintetizzatore esterno; se no la fa l'OPL del dorso. */
static void prova_midi(const AudioProva *p, AudioProvaEsito *e)
{
    static const unsigned char note[4] = { 60, 64, 67, 72 };
    unsigned char m[3];
    unsigned int  i, durata, inizio;

    if (!(g_info.capacita & (AUDIO_CAP_MIDI_FM | AUDIO_CAP_MIDI_UART |
                             AUDIO_CAP_MIDI_ONDA))) {
        e->esito = -ENODEV;
        strcpy(e->nota, "questa scheda non ha un sintetizzatore");
        return;
    }

    /* ! LA SONDA SI FA SOLO SE IL SINTETIZZATORE E' DI SILICIO. Chiedere a
     * quello software se «risponde» sarebbe chiedere al driver se e' se
     * stesso: una domanda che non puo' dire di no, cioe' una prova che non
     * prova niente. Per il software la prova vera e' che dall'altoparlante
     * escano le note, e quella la fa la parte PCM. */
    if ((g_info.capacita & (AUDIO_CAP_MIDI_FM | AUDIO_CAP_MIDI_UART)) &&
        D->midi_vivo() < 0) {
        e->esito = -EIO;
        strcpy(e->nota, "il sintetizzatore non risponde alla sonda");
        return;
    }

    inizio = uptime_ms();
    durata = (p->ms ? p->ms : 800) / 4;

    /* Program change: 0x50 = «lead 1 (square)», che si sente su qualunque
     * sintetizzatore e non si confonde con il silenzio. */
    m[0] = 0xC0; m[1] = 0x50;
    midi_manda(m, 2);

    for (i = 0; i < 4; i++) {
        m[0] = 0x90; m[1] = note[i]; m[2] = p->muta ? 0 : 100;   /* nota su */
        midi_manda(m, 3);

        /* ! MENTRE LA NOTA SUONA SI CONTINUA A SERVIRE GLI INTERRUPT. Con il
         * sintetizzatore software le note SONO campioni: una usleep che non
         * riempie le meta' del buffer lascerebbe suonare la stessa meta' per
         * tutta la nota, e si sentirebbe un ronzio invece della scaletta. */
        {
            unsigned int fino = uptime_ms() + durata;
            IpcMessage    m2;
            unsigned char b2[64];

            while (uptime_ms() < fino) {
                if (ipc_recv_timeout(&m2, b2, sizeof(b2), 5) == 0 &&
                    m2.sender_pid == IPC_SENDER_KERNEL &&
                    m2.tipo == IPC_TYPE_IRQ_NOTIFY && m2.len >= 4) {
                    unsigned int q = 0;
                    memcpy(&q, b2, 4);
                    servi_irq(q);
                }
            }
        }

        m[0] = 0x80; m[1] = note[i]; m[2] = 0;                   /* nota giu' */
        midi_manda(m, 3);
    }

    e->ms    = uptime_ms() - inizio;
    e->esito = 0;
    if (g_info.capacita & AUDIO_CAP_MIDI_UART)
        strcpy(e->nota, "quattro note mandate alla MPU-401");
    else if (g_info.capacita & AUDIO_CAP_MIDI_FM)
        strcpy(e->nota, "quattro note sulla sintesi FM");
    else
        strcpy(e->nota, "quattro note sul sintetizzatore software");
}

static void prova_esegui(const AudioProva *p, AudioProvaEsito *e)
{
    memset(e, 0, sizeof(*e));
    e->quale = p->quale;

    switch (p->quale) {
    case AUDIO_PROVA_PCM8:
    case AUDIO_PROVA_PCM16:
    case AUDIO_PROVA_FLUSSO:
        prova_pcm(p, e);
        break;
    case AUDIO_PROVA_MIDI:
        prova_midi(p, e);
        break;
    default:
        e->esito = -EINVAL;
        strcpy(e->nota, "prova sconosciuta");
        break;
    }
}

/* =============================================================================
 * main — comune a tutti i driver audio
 * ========================================================================== */
static void uso(void)
{
    printf("uso: /dev/%s.drv [-i] [opzioni della scheda]\n", D->nome);
    printf("     -i   sonda e basta: dice cosa ha trovato ed esce\n");
}

int main(int argc, char **argv)
{
    IpcMessage    meta;
    unsigned char payload[IPC_MSG_MAX_DATA];
    int  rc, sonda_e_basta = 0, i;

    D = audio_dorso_questo();

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : 0;

        if (strcmp(a, "-i") == 0) { sonda_e_basta = 1; continue; }
        if (strcmp(a, "-h") == 0) { uso(); return 0; }

        if (D->opzione && D->opzione(a, v) == 0) {
            if (v && a[0] == '-' && a[1] != '\0' && a[2] == '\0') i++;
            continue;
        }
        printf("%s: opzione sconosciuta '%s'\n", D->nome, a);
        uso();
        return 1;
    }

    memset(&g_info, 0, sizeof(g_info));
    g_info.dma8  = AUDIO_DMA_NESSUNO;
    g_info.dma16 = AUDIO_DMA_NESSUNO;

    if (D->sonda(&g_info) < 0) {
        printf("%s: nessuna scheda trovata.\n", D->nome);
        return 1;
    }

    /* ! LA TAVOLA D'ONDE SOFTWARE SI DICHIARA QUI, DOPO LA SONDA, e solo se
     * la scheda non ha un sintetizzatore suo. Non e' una bugia gentile: da
     * questo momento un file MIDI su questa scheda SI SENTE, ed e' esattamente
     * cio' che AUDIO_CAP_MIDI_ONDA promette a chi lo legge. Il perche' stia
     * nel processore e non nella scheda e' scritto in cima al blocco del
     * sintetizzatore. */
    /* ! LA CONDIZIONE E' «SA SUONARE CAMPIONI», non «sa suonare a 8 bit». Una
     * AC'97 e una HD Audio dichiarano solo i 16 bit — sull'AC-link gli 8 bit
     * non esistono — e chiedere AUDIO_CAP_PCM8 le escludeva tutte e due: il
     * collaudo rispondeva «la scheda non ha un sintetizzatore» proprio sulle
     * schede che ne hanno bisogno, perche' sono quelle che di suo non ne
     * hanno nessuno. */
    if (!(g_info.capacita & (AUDIO_CAP_MIDI_FM | AUDIO_CAP_MIDI_UART)) &&
        (g_info.capacita & (AUDIO_CAP_PCM8 | AUDIO_CAP_PCM16)))
        g_info.capacita |= AUDIO_CAP_MIDI_ONDA;

    printf("%s: %s (%s)", D->nome, g_info.nome, g_info.bus);
    if (g_info.base) printf(" a 0x%x", g_info.base);
    printf(" IRQ%u", g_info.irq);
    if (g_info.dma8  != AUDIO_DMA_NESSUNO) printf(" DMA%u", g_info.dma8);
    if (g_info.dma16 != AUDIO_DMA_NESSUNO) printf("/%u", g_info.dma16);
    printf("\n");

    if (sonda_e_basta) return 0;

    /* ! L'IRQ SI RIVENDICA DOPO LA SONDA, non prima: su una scheda ISA
     * anteriore alla SB16 il numero lo scopre la sonda stessa, provocando un
     * interrupt e guardando quale linea si alza. Rivendicarlo prima
     * vorrebbe dire saperlo gia'. */
    rc = irq_bind(g_info.irq);
    if (rc < 0) {
        printf("%s: irq_bind(%u) fallita (%d) - esco\n", D->nome, g_info.irq, rc);
        return 1;
    }

    rc = ipc_register(AUDIO_SERVIZIO);
    if (rc < 0) {
        printf("%s: ipc_register('%s') fallita (%d).\n",
               D->nome, AUDIO_SERVIZIO, rc);
        printf("%s: c'e' gia' un driver audio attivo. Uno solo alla volta.\n",
               D->nome);
        return 1;
    }

    D->volume(g_volume);
    printf("%s: servizio '%s' attivo\n", D->nome, AUDIO_SERVIZIO);

    for (;;) {
        if (ipc_recv(&meta, payload, sizeof(payload)) < 0) continue;

        if (meta.sender_pid == IPC_SENDER_KERNEL &&
            meta.tipo == IPC_TYPE_IRQ_NOTIFY) {
            unsigned int irq = 0;
            if (meta.len >= sizeof(irq)) memcpy(&irq, payload, sizeof(irq));
            servi_irq(irq);
            continue;
        }

        switch (meta.tipo) {

        case AUDIO_MSG_INFO:
            ipc_send(meta.sender_pid, AUDIO_MSG_INFO_R, &g_info, sizeof(g_info));
            break;

        case AUDIO_MSG_APRI: {
            AudioFormato f;
            AudioEsito   e;

            memset(&f, 0, sizeof(f));
            if (meta.len >= sizeof(f)) memcpy(&f, payload, sizeof(f));

            /* ! UN SOLO CLIENT ALLA VOLTA, e si dice invece di mescolare.
             * Mescolare due flussi vorrebbe dire un mixer software: e' un
             * lavoro vero, non una riga, e va fatto quando serve — non
             * inventato di nascosto dentro il driver della scheda. */
            if (g_client != 0 && g_client != meta.sender_pid &&
                g_stato != AUDIO_ST_CHIUSO) {
                memset(&e, 0, sizeof(e));
                e.esito = -EBUSY;
                ipc_send(meta.sender_pid, AUDIO_MSG_ESITO, &e, sizeof(e));
                break;
            }

            fai_apri(&f, &e);
            if (e.esito == 0) g_client = meta.sender_pid;
            ipc_send(meta.sender_pid, AUDIO_MSG_ESITO, &e, sizeof(e));
            break;
        }

        case AUDIO_MSG_VIA:
            if (meta.sender_pid == g_client) fai_via();
            break;

        case AUDIO_MSG_FERMA:
            if (meta.sender_pid == g_client) fai_ferma();
            break;

        case AUDIO_MSG_CHIUDI:
            if (meta.sender_pid == g_client) { fai_chiudi(); zona_chiudi(); }
            break;

        case AUDIO_MSG_VOLUME: {
            AudioVolume v;
            if (meta.len >= sizeof(v)) {
                memcpy(&v, payload, sizeof(v));
                if (v.percento > 100) v.percento = 100;
                g_volume = v.percento;
                D->volume(g_volume);
            }
            break;
        }

        case AUDIO_MSG_MIDI:
            if (meta.len > 0) midi_manda(payload, meta.len);
            break;

        case AUDIO_MSG_STATO: {
            AudioStato s;
            memset(&s, 0, sizeof(s));
            s.stato = g_stato;
            if (g_anello) {
                s.scritto     = g_anello->scritto;
                s.suonato     = g_anello->suonato;
                s.sottoflussi = g_anello->sottoflussi;
            }
            s.ms = (g_stato == AUDIO_ST_SUONA) ? (uptime_ms() - g_via_ms) : 0;
            ipc_send(meta.sender_pid, AUDIO_MSG_STATO_R, &s, sizeof(s));
            break;
        }

        case AUDIO_MSG_PROVA: {
            AudioProva      p;
            AudioProvaEsito e;

            memset(&p, 0, sizeof(p));
            if (meta.len >= sizeof(p)) memcpy(&p, payload, sizeof(p));

            prova_esegui(&p, &e);
            ipc_send(meta.sender_pid, AUDIO_MSG_PROVA_R, &e, sizeof(e));

            /* Dopo una prova il driver torna come era: l'anello era suo, non
             * di un client, e lasciarlo aperto farebbe rispondere -EBUSY al
             * primo programma che chiede di suonare. */
            fai_chiudi();
            break;
        }

        default:
            break;
        }
    }
}

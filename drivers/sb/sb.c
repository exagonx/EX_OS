/* =============================================================================
 * drivers/sb/sb.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * SOUND BLASTER — dalla 1.0 del 1989 alla AWE64, con lo stesso codice
 *
 *     /dev/sb.drv           cerca la scheda e accende il servizio "audio"
 *     /dev/sb.drv -i        la sonda: dice cosa ha trovato ed esce
 *     /dev/sb.drv -p 0x240 -q 7 -d 1 -D 5     se la sonda sbaglia
 *
 * La meta' comune — anello, protocollo, prove — sta in
 * drivers/audio/audio_comune.c. Qui ci sono soltanto i registri.
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' UN DRIVER SOLO PER QUATTRO GENERAZIONI DI SCHEDE
 *
 * Perche' sono compatibili all'indietro nei registri, e la differenza sta
 * tutta in TRE cose che si leggono dalla versione del DSP:
 *
 *     DSP 1.x   SB 1.0/1.5   8 bit mono, DMA a ciclo singolo: il blocco
 *                            va riordinato a ogni interrupt
 *     DSP 2.x   SB 2.0       compare l'auto-init (comando 0x1C): il DMA
 *                            gira da solo e il driver riempie e basta
 *     DSP 3.x   SB Pro       stereo (bit nel mixer), OPL3
 *     DSP 4.x   SB 16/AWE32/AWE64   16 bit con segno, frequenza in Hz
 *                            invece che come «costante di tempo», e —
 *                            la cosa che conta di piu' — IRQ e DMA
 *                            SCRITTI IN UN REGISTRO che si puo' leggere
 *
 * Una AWE32 e una AWE64 sono, per la riproduzione PCM, esattamente una
 * SB16: cambia il sintetizzatore a bordo (EMU8000), non il DSP. Qui si
 * riconoscono e si dichiarano per nome — perche' chi guarda vuole sapere
 * cos'ha — e si guidano con lo stesso codice.
 *
 * -----------------------------------------------------------------------------
 * ! IL BUFFER DEL DMA NON PUO' STARE DOVE CAPITA, E CI SONO TRE VINCOLI
 *
 * Il DMA ISA e' il controller 8237 della SCHEDA MADRE, un chip del 1976
 * attaccato al bus a 24 bit. Quindi:
 *
 *   1. l'indirizzo dev'essere SOTTO I 16 MB. Sopra, i bit non ci sono
 *      proprio: il registro di pagina e' a 8 bit e l'indirizzo a 16, in
 *      tutto 24. Un buffer a 20 MB verrebbe letto a 4 MB, e a 4 MB c'e'
 *      qualcun altro.
 *
 *   2. il blocco NON PUO' ATTRAVERSARE UN CONFINE DI 64 KB. Il registro
 *      di pagina non e' sommato all'indirizzo: e' concatenato. Quando i
 *      16 bit bassi traboccano, l'indirizzo TORNA all'inizio della stessa
 *      pagina invece di passare alla successiva. Un buffer a cavallo del
 *      confine suona la prima meta' e poi ripete cio' che sta a 64 KB
 *      indietro, per sempre.
 *
 *   3. per i canali a 16 bit (5-7) vale lo stesso con 128 KB, perche'
 *      l'indirizzo li' e' contato in PAROLE e non in byte.
 *
 * SYS_DMA_ALLOC da' pagine fisicamente contigue e il loro indirizzo
 * fisico, che e' quanto serve per il vincolo 1; per il 2 e il 3 non c'e'
 * una syscall, e non serve: si chiede il DOPPIO dello spazio e dentro si
 * sceglie il tratto allineato. Un blocco di 32 KB allineato a 32 KB non
 * attraversa ne' un confine di 64 KB ne' uno di 128 KB, mai — ed e' una
 * proprieta' dell'aritmetica, non una speranza.
 *
 * -----------------------------------------------------------------------------
 * ! COME SI SCOPRE L'IRQ DI UNA SCHEDA CHE NON LO DICE
 *
 * Su una SB16 IRQ e DMA stanno nei registri 0x80 e 0x81 del mixer: si
 * leggono e si e' finito. Su tutto cio' che viene prima, il numero e' un
 * PONTICELLO, e nessun registro lo riporta.
 *
 * L'unico modo e' provocare un interrupt e guardare quale linea si alza.
 * Il DSP ha un comando apposta — 0xF2, «alza l'interrupt a 8 bit» — che
 * esiste da sempre proprio per questo. Quindi: si rivendicano tutti i
 * candidati (5, 7, 9, 10, 3), si manda 0xF2, e si guarda quale notifica
 * arriva; le altre linee si restituiscono con irq_unbind(). E' l'unica
 * ragione per cui quella syscall esiste — vedi il suo commento in
 * kernel/include/syscall.h.
 *
 * Il canale DMA si scopre in modo simile ma senza interrupt: si programma
 * un candidato, si fa suonare un blocco di silenzio, e si guarda se il
 * CONTATORE del controller e' sceso. Se non e' sceso, quel canale non e'
 * il suo. Il silenzio si puo' suonare su un canale sbagliato senza
 * conseguenze: il DMA legge memoria nostra e la manda a una scheda che
 * non c'e'.
 * ============================================================================= */

#include "libc.h"
#include "audio_proto.h"
#include "audio_dorso.h"

/* +0.001 a ogni modifica: `sb.drv -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("sb.drv", "0.001");

/* =============================================================================
 * I registri della scheda, come spiazzamenti dalla base
 * ========================================================================== */
#define SB_FM_ADDR      0x00    /* OPL2 «sinistro» (e OPL3 banco 0)          */
#define SB_FM_DATA      0x01
#define SB_MIX_ADDR     0x04    /* indice del mixer                          */
#define SB_MIX_DATA     0x05
#define SB_RESET        0x06
#define SB_FM2_ADDR     0x08    /* compatibile AdLib, come 0x388             */
#define SB_FM2_DATA     0x09
#define SB_LEGGI        0x0A    /* dato dal DSP                              */
#define SB_SCRIVI       0x0C    /* comando al DSP (bit 7 = occupato)         */
#define SB_STATO        0x0E    /* bit 7 = c'e' un dato; leggerlo = ack 8bit */
#define SB_ACK16        0x0F    /* leggerlo = ack dell'interrupt a 16 bit    */

#define SB_PORTE        16

/* Comandi del DSP che servono qui */
#define DSP_TC          0x40    /* costante di tempo (DSP < 4)               */
#define DSP_RATE_OUT    0x41    /* frequenza in Hz (DSP 4.x)                 */
#define DSP_BLOCCO      0x48    /* lunghezza del blocco (DSP < 4)            */
#define DSP_SC_8        0x14    /* uscita 8 bit a ciclo singolo (DSP 1.x)    */
#define DSP_AI_8        0x1C    /* uscita 8 bit auto-init (DSP >= 2)         */
#define DSP_ALTOP_ON    0xD1    /* altoparlante acceso                       */
#define DSP_ALTOP_OFF   0xD3
#define DSP_PAUSA_8     0xD0
#define DSP_RIPRENDI_8  0xD4
#define DSP_PAUSA_16    0xD5
#define DSP_RIPRENDI_16 0xD6
#define DSP_VERSIONE    0xE1
#define DSP_IRQ_FINTO   0xF2    /* alza l'interrupt a 8 bit, senza suonare   */

/* Mixer */
#define MIX_RESET       0x00
#define MIX_STEREO_PRO  0x0E    /* SB Pro: bit 1 = stereo                    */
#define MIX_MASTER_PRO  0x22
#define MIX_MASTER_L    0x30    /* SB16: 5 bit alti                          */
#define MIX_MASTER_R    0x31
#define MIX_VOCE_L      0x32
#define MIX_VOCE_R      0x33
#define MIX_IRQ         0x80    /* SB16: quale IRQ e' cablato                */
#define MIX_DMA         0x81    /* SB16: quali canali DMA                    */

/* MPU-401 */
#define MPU_DATI        0
#define MPU_STATO       1       /* in lettura: bit 6 = non posso scrivere,
                                 *             bit 7 = non c'e' niente da leggere */
#define MPU_CMD         1       /* in scrittura */

/* =============================================================================
 * Stato
 * ========================================================================== */
static unsigned int g_base   = 0;
static unsigned int g_irq    = 0;
static unsigned int g_dma8   = AUDIO_DMA_NESSUNO;
static unsigned int g_dma16  = AUDIO_DMA_NESSUNO;
static unsigned int g_mpu    = 0;
static unsigned int g_fm     = 0;
static unsigned int g_dsp    = 0;       /* (maggiore << 8) | minore */

/* imposti a mano dalla riga di comando: la sonda non li tocca */
static int g_base_forz = -1, g_irq_forz = -1, g_dma8_forz = -1, g_dma16_forz = -1;

static DmaZona      g_zona;             /* il doppio, per poter allineare */
static unsigned int g_buf_fis  = 0;     /* l'indirizzo fisico ALLINEATO */
static unsigned char *g_buf    = 0;     /* lo stesso, visto dal processo */
static unsigned int g_buf_byte = 0;     /* quanto se ne usa davvero */

static unsigned int g_canale   = AUDIO_DMA_NESSUNO;  /* quello in uso adesso */
static unsigned int g_a16      = 0;     /* la riproduzione in corso e' a 16 bit */
static unsigned int g_canali   = 1;
static unsigned int g_meta     = 0;
static unsigned int g_dsp_bloc = 0;     /* lunghezza del blocco DSP, in byte */
static unsigned int g_ai       = 0;     /* il DSP sa fare auto-init */
static unsigned int g_suona    = 0;
static unsigned int g_meta_prossima = 0;

/* =============================================================================
 * Accesso alle porte
 * ========================================================================== */
static void  fuori(unsigned int off, unsigned int v) { ioport_out(g_base + off, v); }
static int   dentro(unsigned int off)                { return ioport_in(g_base + off); }

/* Una pausa brevissima fra due accessi allo stesso chip. Su hardware vero
 * l'8237 e il DSP vogliono qualche centinaio di nanosecondi fra una scrittura
 * e la successiva; una lettura da una porta inerte e' il modo classico di
 * aspettarli senza un timer. */
static void attimo(void)
{
    ioport_in(0x80);
}

/* =============================================================================
 * Il DSP
 * ========================================================================== */
static int dsp_scrivi(unsigned int val)
{
    int guard;

    /* Bit 7 acceso = il DSP non e' pronto a ricevere. */
    for (guard = 0; guard < 65535; guard++) {
        int st = dentro(SB_SCRIVI);
        if (st < 0) return -1;
        if (!(st & 0x80)) { fuori(SB_SCRIVI, val); return 0; }
    }
    return -1;
}

static int dsp_leggi(void)
{
    int guard;

    for (guard = 0; guard < 65535; guard++) {
        int st = dentro(SB_STATO);
        if (st < 0) return -1;
        if (st & 0x80) return dentro(SB_LEGGI);
    }
    return -1;
}

/* =============================================================================
 * dsp_reset — l'unica sonda possibile per una scheda ISA
 *
 * ! SCRIVERE SU UNA PORTA PER SAPERE SE C'E' QUALCOSA e' una cosa che si fa
 * solo quando non c'e' alternativa, ed e' il caso: una scheda ISA non ha uno
 * spazio di configurazione da leggere. Il danno possibile e' limitato dal
 * fatto che i sette indirizzi provati sono quelli che la Creative ha usato e
 * nessun altro, e che si scrive UN byte nel registro di reset — che su una
 * scheda diversa e' quasi sempre un registro inerte.
 *
 * La sequenza e' quella del manuale: 1 nel registro di reset, tre microsecondi
 * abbondanti, 0, e poi la scheda deve rispondere 0xAA. Nient'altro risponde
 * 0xAA a questa sequenza.
 * ========================================================================== */
static int dsp_reset(void)
{
    int guard;

    fuori(SB_RESET, 1);
    usleep(10);
    fuori(SB_RESET, 0);

    for (guard = 0; guard < 200; guard++) {
        int st = dentro(SB_STATO);
        if (st >= 0 && (st & 0x80)) {
            if (dentro(SB_LEGGI) == 0xAA) return 0;
        }
        usleep(20);
    }
    return -1;
}

static unsigned int dsp_versione(void)
{
    int ma, mi;

    if (dsp_scrivi(DSP_VERSIONE) < 0) return 0;
    ma = dsp_leggi();
    mi = dsp_leggi();
    if (ma < 0 || mi < 0) return 0;
    return ((unsigned int)ma << 8) | (unsigned int)mi;
}

/* =============================================================================
 * Il mixer
 * ========================================================================== */
static void mix_scrivi(unsigned int reg, unsigned int val)
{
    fuori(SB_MIX_ADDR, reg);
    attimo();
    fuori(SB_MIX_DATA, val);
    attimo();
}

static int mix_leggi(unsigned int reg)
{
    fuori(SB_MIX_ADDR, reg);
    attimo();
    return dentro(SB_MIX_DATA);
}

/* =============================================================================
 * IL CONTROLLER DMA 8237 DELLA SCHEDA MADRE
 *
 * Due chip in cascata: il primo (canali 0-3) muove byte, il secondo (4-7)
 * muove parole, e il canale 4 non si usa perche' e' la cascata fra i due.
 * I registri del secondo stanno a indirizzi doppi e distanti, che e' il
 * motivo per cui le tre tabelle qui sotto esistono invece di una formula.
 * ========================================================================== */
static const unsigned char dma_pagina[8] = {
    0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A
};
static const unsigned char dma_indirizzo[8] = {
    0x00, 0x02, 0x04, 0x06, 0xC0, 0xC4, 0xC8, 0xCC
};
static const unsigned char dma_conteggio[8] = {
    0x01, 0x03, 0x05, 0x07, 0xC2, 0xC6, 0xCA, 0xCE
};

static unsigned int dma_maschera_porta(unsigned int ch) { return (ch < 4) ? 0x0A : 0xD4; }
static unsigned int dma_modo_porta(unsigned int ch)     { return (ch < 4) ? 0x0B : 0xD6; }
static unsigned int dma_flip_porta(unsigned int ch)     { return (ch < 4) ? 0x0C : 0xD8; }

static void dma_ferma(unsigned int ch)
{
    if (ch == AUDIO_DMA_NESSUNO) return;
    ioport_out(dma_maschera_porta(ch), 0x04 | (ch & 3));
}

/* =============================================================================
 * dma_programma — punta il controller sul buffer e lascialo girare
 *
 * `auto_init` acceso significa che alla fine del blocco il controller
 * RICARICA indirizzo e conteggio da solo e ricomincia, senza che nessuno
 * intervenga. E' cio' che permette a un driver in ring 3 di esistere: senza,
 * fra la fine di un blocco e la riprogrammazione ci sarebbe il tempo di uno
 * scheduling, e si sentirebbe.
 * ========================================================================== */
static void dma_programma(unsigned int ch, unsigned int fisico,
                          unsigned int byte, int auto_init)
{
    unsigned int modo, ind, cnt;

    /* 0x48 = «modo singolo» + «leggi dalla memoria»: la memoria va alla
     * scheda, non viceversa. Con 0x10 in piu' e' auto-init. */
    modo = 0x48 | (auto_init ? 0x10 : 0x00) | (ch & 3);

    if (ch < 4) {
        ind = fisico & 0xFFFF;
        cnt = byte - 1;
    } else {
        /* ! I CANALI 5-7 CONTANO PAROLE, NON BYTE, e l'indirizzo che
         * vogliono e' quello fisico DIVISO DUE. Darglielo in byte manda il
         * DMA a leggere al doppio dell'indirizzo giusto: sul serio, in
         * memoria di qualcun altro. */
        ind = (fisico >> 1) & 0xFFFF;
        cnt = (byte / 2) - 1;
    }

    ioport_out(dma_maschera_porta(ch), 0x04 | (ch & 3));   /* maschera */
    ioport_out(dma_flip_porta(ch), 0x00);                  /* azzera il flip-flop */
    ioport_out(dma_modo_porta(ch), modo);

    ioport_out(dma_indirizzo[ch],  ind & 0xFF);
    ioport_out(dma_indirizzo[ch], (ind >> 8) & 0xFF);

    ioport_out(dma_pagina[ch], (fisico >> 16) & 0xFF);

    ioport_out(dma_flip_porta(ch), 0x00);
    ioport_out(dma_conteggio[ch],  cnt & 0xFF);
    ioport_out(dma_conteggio[ch], (cnt >> 8) & 0xFF);

    ioport_out(dma_maschera_porta(ch), ch & 3);            /* via */
}

/* Il conteggio che resta, letto dal controller. E' l'unico numero che viene
 * dall'hardware e non da noi: la prova di collaudo ci si appoggia. */
static unsigned int dma_resta(unsigned int ch)
{
    int lo, hi;

    if (ch == AUDIO_DMA_NESSUNO) return 0;

    ioport_out(dma_flip_porta(ch), 0x00);
    lo = ioport_in(dma_conteggio[ch]);
    hi = ioport_in(dma_conteggio[ch]);
    if (lo < 0 || hi < 0) return 0;
    return (unsigned int)((hi << 8) | lo);
}

/* =============================================================================
 * Il buffer: chiedere il doppio per poterne allineare la meta'
 *
 * Il perche' sta nel commento in testa al file, vincolo 2. Qui c'e' il conto:
 * si chiedono 64 KB, si sale al primo multiplo di 32 KB, e da li' si usano al
 * massimo 32 KB. Il tratto scelto sta tutto dentro un blocco di 32 KB
 * allineato, quindi non attraversa ne' 64 KB ne' 128 KB.
 * ========================================================================== */
#define BUF_CHIESTO   65536u
#define BUF_ALLINEA   32768u

static int buffer_prendi(void)
{
    unsigned int off;

    if (g_buf) return 0;

    memset(&g_zona, 0, sizeof(g_zona));
    g_zona.byte = BUF_CHIESTO;
    if (dma_alloc(&g_zona) < 0) {
        printf("sb: dma_alloc(%u) fallita\n", BUF_CHIESTO);
        return -1;
    }

    off = (BUF_ALLINEA - (g_zona.fisico % BUF_ALLINEA)) % BUF_ALLINEA;

    g_buf_fis = g_zona.fisico + off;
    g_buf     = (unsigned char *)(g_zona.virt + off);

    /* ! IL CONTROLLO DEI 16 MB VA FATTO, ANCHE SE OGGI PASSA SEMPRE.
     * pmm_alloc_pages cerca dal basso, quindi su una macchina piccola il
     * buffer capita sempre in basso. «Capita sempre» non e' una garanzia:
     * il giorno che l'allocatore cambia politica, il sintomo sarebbe una
     * scheda che scrive a un indirizzo troncato — cioe' memoria di
     * qualcun altro rovinata a caso, senza un messaggio. */
    if (g_buf_fis + BUF_ALLINEA > 0x01000000u) {
        printf("sb: il buffer DMA e' finito a 0x%x, sopra i 16 MB che il\n",
               g_buf_fis);
        printf("    bus ISA sa indirizzare. La scheda non si puo' usare.\n");
        g_buf = 0;
        return -1;
    }

    return 0;
}

/* =============================================================================
 * LA SONDA
 * ========================================================================== */
static const unsigned short g_basi[] = { 0x220, 0x240, 0x260, 0x280,
                                         0x210, 0x230, 0x250, 0 };

static int cerca_base(void)
{
    int i;

    if (g_base_forz > 0) {
        g_base = (unsigned int)g_base_forz;
        return dsp_reset();
    }

    for (i = 0; g_basi[i]; i++) {
        g_base = g_basi[i];
        if (dsp_reset() == 0) return 0;
    }
    g_base = 0;
    return -1;
}

/* --- IRQ e DMA su una SB16: si leggono, e basta --------------------------- */
static int legge_irq_dma_16(void)
{
    int r_irq = mix_leggi(MIX_IRQ);
    int r_dma = mix_leggi(MIX_DMA);

    if (r_irq < 0 || r_dma < 0) return -1;

    g_irq = 0;
    if (r_irq & 0x01) g_irq = 2;        /* «IRQ2» sulle macchine AT e' il 9 */
    if (r_irq & 0x02) g_irq = 5;
    if (r_irq & 0x04) g_irq = 7;
    if (r_irq & 0x08) g_irq = 10;
    if (g_irq == 2)   g_irq = 9;

    g_dma8 = AUDIO_DMA_NESSUNO;
    if (r_dma & 0x01) g_dma8 = 0;
    if (r_dma & 0x02) g_dma8 = 1;
    if (r_dma & 0x08) g_dma8 = 3;

    g_dma16 = AUDIO_DMA_NESSUNO;
    if (r_dma & 0x20) g_dma16 = 5;
    if (r_dma & 0x40) g_dma16 = 6;
    if (r_dma & 0x80) g_dma16 = 7;

    return (g_irq != 0 && g_dma8 != AUDIO_DMA_NESSUNO) ? 0 : -1;
}

/* --- IRQ su tutto cio' che viene prima: si provoca e si guarda ------------ */
static const unsigned char g_irq_cand[] = { 5, 7, 9, 10, 3, 0 };

static int scopre_irq(void)
{
    IpcMessage    meta;
    unsigned char buf[32];
    unsigned int  presi[8];
    int i, n = 0, trovato = -1;

    if (g_irq_forz > 0) { g_irq = (unsigned int)g_irq_forz; return 0; }

    for (i = 0; g_irq_cand[i]; i++) {
        if (irq_bind(g_irq_cand[i]) == 0) presi[n++] = g_irq_cand[i];
    }
    if (n == 0) return -1;

    /* ! IL COMANDO 0xF2 ESISTE APPOSTA. Alza l'interrupt a 8 bit senza
     * muovere un campione: e' la domanda «su quale filo sei attaccato?»
     * fatta alla scheda invece che all'utente. */
    dsp_scrivi(DSP_IRQ_FINTO);

    for (i = 0; i < 8 && trovato < 0; i++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 120) < 0) break;
        if (meta.sender_pid == IPC_SENDER_KERNEL &&
            meta.tipo == IPC_TYPE_IRQ_NOTIFY && meta.len >= 4) {
            unsigned int q = 0;
            memcpy(&q, buf, 4);
            trovato = (int)q;
            dentro(SB_STATO);           /* ack all'interrupt a 8 bit */
            irq_done(q);
        }
    }

    /* Le linee prese in prestito si restituiscono: erano una domanda, non
     * un possesso. Vedi irq_unbind_uno() in kernel/arch/x86/isr.c. */
    for (i = 0; i < n; i++) {
        if ((int)presi[i] != trovato) irq_unbind(presi[i]);
    }

    if (trovato < 0) return -1;
    g_irq = (unsigned int)trovato;
    return 0;
}

/* --- Il canale DMA: si prova, e si guarda se il contatore scende ---------- */
static const unsigned char g_dma_cand[] = { 1, 3, 0, 0xFF };

static int scopre_dma(void)
{
    int i, guard;

    if (g_dma8_forz >= 0) { g_dma8 = (unsigned int)g_dma8_forz; return 0; }
    if (!g_buf) return -1;

    memset(g_buf, 0x80, 4096);          /* silenzio a 8 bit */

    for (i = 0; g_dma_cand[i] != 0xFF; i++) {
        unsigned int ch = g_dma_cand[i];
        unsigned int prima, dopo, cambiato = 0;

        dma_programma(ch, g_buf_fis, 4096, 0);
        prima = dma_resta(ch);

        dsp_scrivi(DSP_ALTOP_ON);
        dsp_scrivi(DSP_TC);
        dsp_scrivi(256 - (1000000 / 11025));
        dsp_scrivi(DSP_SC_8);
        dsp_scrivi((4096 - 1) & 0xFF);
        dsp_scrivi(((4096 - 1) >> 8) & 0xFF);

        for (guard = 0; guard < 40; guard++) {
            usleep(2000);
            dopo = dma_resta(ch);
            if (dopo != prima) { cambiato = 1; break; }
        }

        dsp_reset();                    /* ferma tutto, qualunque cosa fosse */
        dma_ferma(ch);

        if (cambiato) { g_dma8 = ch; return 0; }
    }
    return -1;
}

/* --- L'OPL: la prova del timer, l'unica che risponde davvero -------------- */
static void opl_scrivi(unsigned int porta, unsigned int reg, unsigned int val)
{
    int i;

    ioport_out(porta, reg);
    for (i = 0; i < 6; i++) ioport_in(porta);    /* l'OPL2 vuole 3.3 us */
    ioport_out(porta + 1, val);
    for (i = 0; i < 35; i++) ioport_in(porta);   /* e 23 us dopo il dato */
}

static int opl_c_e(unsigned int porta)
{
    int st1, st2;

    opl_scrivi(porta, 0x04, 0x60);      /* azzera i due timer */
    opl_scrivi(porta, 0x04, 0x80);      /* azzera il segnale */
    st1 = ioport_in(porta);
    opl_scrivi(porta, 0x02, 0xFF);      /* timer 1 al massimo */
    opl_scrivi(porta, 0x04, 0x21);      /* fallo partire */
    usleep(200);
    st2 = ioport_in(porta);
    opl_scrivi(porta, 0x04, 0x60);
    opl_scrivi(porta, 0x04, 0x80);

    if (st1 < 0 || st2 < 0) return 0;

    /* ! LA RISPOSTA E' NEI DUE BIT ALTI, e la coppia dev'essere ESATTAMENTE
     * questa: 0x00 prima di far partire il timer e 0xC0 dopo. Un indirizzo
     * senza chip legge 0xFF tutte e due le volte; un chip che c'e' ma non e'
     * un OPL non fa scattare il timer. E' la sonda AdLib del 1987, e non ne
     * e' mai stata trovata una migliore. */
    return ((st1 & 0xE0) == 0x00 && (st2 & 0xE0) == 0xC0);
}

/* --- La MPU-401 in modo UART --------------------------------------------- */
static int mpu_pronto_a_scrivere(void)
{
    int guard;

    for (guard = 0; guard < 10000; guard++) {
        int st = ioport_in(g_mpu + MPU_STATO);
        if (st < 0) return 0;
        if (!(st & 0x40)) return 1;
    }
    return 0;
}

static int mpu_accendi(unsigned int porta)
{
    int guard, st;

    /* Reset: 0xFF nel registro comandi, e la MPU risponde 0xFE. */
    ioport_out(porta + MPU_CMD, 0xFF);

    for (guard = 0; guard < 500; guard++) {
        st = ioport_in(porta + MPU_STATO);
        if (st >= 0 && !(st & 0x80)) {
            if (ioport_in(porta + MPU_DATI) == 0xFE) {
                ioport_out(porta + MPU_CMD, 0x3F);   /* modo UART */
                usleep(1000);
                return 1;
            }
        }
        usleep(100);
    }
    return 0;
}

/* =============================================================================
 * Il nome della scheda — e la AWE, che si riconosce dal suo sintetizzatore
 *
 * L'EMU8000 di una AWE32/AWE64 risponde a 0x620/0x622/0xA20/0xA22/0xE20/0xE22,
 * cioe' base+0x400 e seguenti. La sonda ufficiale legge il registro di
 * identificazione e si aspetta 0x001C nei bit bassi.
 *
 * ! NON SI SONDA, PER ORA, e va detto invece che finto: l'EMU8000 sta fuori
 * dalla finestra di porte che questo driver rivendica (0x210-0x28F), e
 * aggiungerne una settima per stampare un nome piu' bello sarebbe pagare un
 * permesso vero per un dettaglio estetico. Il giorno che qualcuno scrive la
 * sintesi a tavola d'onde, quella finestra la chiedera' perche' gli serve.
 * ========================================================================== */
static const char *nome_scheda(void)
{
    unsigned int ma = g_dsp >> 8;

    if (ma >= 4) return "Sound Blaster 16 / AWE";
    if (ma == 3) return "Sound Blaster Pro";
    if (ma == 2) return "Sound Blaster 2.0";
    return "Sound Blaster 1.x";
}

static int sb_sonda(AudioInfo *info)
{
    /* Le sei finestre di porte. Il perche' siano sei e non una sta accanto a
     * IO_FINESTRE_MAX in kernel/include/sched.h. */
    if (ioport_bind(0x000, 0x10) < 0 ||     /* DMA 8 bit                  */
        ioport_bind(0x080, 0x10) < 0 ||     /* registri di pagina         */
        ioport_bind(0x0C0, 0x20) < 0 ||     /* DMA 16 bit                 */
        ioport_bind(0x210, 0x80) < 0 ||     /* la scheda, dovunque sia    */
        ioport_bind(0x330, 0x04) < 0 ||     /* MPU-401                    */
        ioport_bind(0x388, 0x02) < 0) {     /* OPL2/OPL3                  */
        printf("sb: ioport_bind fallita - questo processo e' un driver?\n");
        return -1;
    }

    if (cerca_base() < 0) {
        printf("sb: nessun DSP risponde a 0x220/0x240/0x260/0x280"
               "/0x210/0x230/0x250\n");
        return -1;
    }

    g_dsp = dsp_versione();
    if (g_dsp == 0) {
        printf("sb: il DSP a 0x%x non dice la propria versione\n", g_base);
        return -1;
    }
    g_ai = (g_dsp >> 8) >= 2;

    if (buffer_prendi() < 0) return -1;

    /* IRQ e DMA: sulla 16 si leggono, prima si scoprono. */
    if ((g_dsp >> 8) >= 4 && legge_irq_dma_16() == 0) {
        if (g_irq_forz  > 0) g_irq   = (unsigned int)g_irq_forz;
        if (g_dma8_forz >= 0) g_dma8 = (unsigned int)g_dma8_forz;
        if (g_dma16_forz >= 0) g_dma16 = (unsigned int)g_dma16_forz;
    } else {
        if (scopre_irq() < 0) {
            printf("sb: DSP %u.%02u a 0x%x, ma nessun IRQ risponde.\n",
                   g_dsp >> 8, g_dsp & 0xFF, g_base);
            printf("    Provare a dirlo a mano:  /dev/sb.drv -q 5 -d 1\n");
            return -1;
        }
        if (scopre_dma() < 0) {
            printf("sb: IRQ%u trovato, ma nessun canale DMA muove byte.\n", g_irq);
            printf("    Provare a dirlo a mano:  /dev/sb.drv -q %u -d 1\n", g_irq);
            return -1;
        }
        if (g_dma16_forz >= 0) g_dma16 = (unsigned int)g_dma16_forz;
    }

    /* Il sintetizzatore: prima la porta MIDI vera, poi la sintesi FM. */
    /* ! SOLO 0x330, E NON ANCHE 0x300. L'altro indirizzo classico della
     * MPU-401 e' 0x300, ma 0x300 e' anche l'indirizzo predefinito di una
     * NE2000 ISA: sondarlo vuol dire scrivere 0xFF nel registro comandi di
     * una scheda di rete che non se lo aspetta. Una porta MIDI in piu' non
     * vale una scheda di rete azzoppata — e chi ce l'ha a 0x300 puo' dirlo,
     * il giorno che serve, con un'opzione. */
    g_mpu = 0;
    if (mpu_accendi(0x330)) g_mpu = 0x330;

    g_fm = 0;
    if (opl_c_e(0x388))          g_fm = 0x388;
    else if (opl_c_e(g_base))    g_fm = g_base;

    /* --- la carta d'identita' --- */
    memset(info, 0, sizeof(*info));
    strcpy(info->nome, nome_scheda());
    strcpy(info->bus, "ISA");
    info->base         = g_base;
    info->irq          = g_irq;
    info->dma8         = g_dma8;
    info->dma16        = g_dma16;
    info->mpu_base     = g_mpu;
    info->fm_base      = g_fm;
    info->dsp_versione = g_dsp;

    info->capacita = AUDIO_CAP_PCM8;
    info->rate_min = 4000;
    info->rate_max = 22050;

    if ((g_dsp >> 8) >= 3) { info->capacita |= AUDIO_CAP_STEREO | AUDIO_CAP_MIXER; }
    if ((g_dsp >> 8) >= 4) {
        info->capacita |= AUDIO_CAP_PCM16 | AUDIO_CAP_STEREO | AUDIO_CAP_MIXER;
        info->rate_max  = 44100;
    }
    if (g_fm)  info->capacita |= AUDIO_CAP_MIDI_FM;
    if (g_mpu) info->capacita |= AUDIO_CAP_MIDI_UART;

    return 0;
}

/* =============================================================================
 * apri / via / ferma
 * ========================================================================== */
static unsigned int potenza_di_due(unsigned int v, unsigned int min, unsigned int max)
{
    unsigned int p = min;
    while (p < v && p < max) p <<= 1;
    return p;
}

static int sb_apri(AudioFormato *f, unsigned char **buf, unsigned int *byte)
{
    unsigned int bps, meta;

    if (!g_buf) return -1;

    /* --- il formato, stretto a cio' che questa generazione sa fare --- */
    if (f->canali < 1) f->canali = 1;
    if (f->canali > 2) f->canali = 2;
    if (f->bit != 16)  f->bit = 8;

    if ((g_dsp >> 8) < 4) {
        f->bit = 8;                                 /* i 16 bit sono della 16 */
        if ((g_dsp >> 8) < 3) f->canali = 1;        /* lo stereo e' della Pro */
        /* ! NIENTE «ALTA VELOCITA'» SOPRA I 22 kHz sulle schede vecchie. Il
         * modo high-speed (comando 0x90) blocca il DSP: da li' in poi non
         * accetta piu' comandi e l'unico modo di riprenderlo e' un reset.
         * Un driver che ci entra per suonare meglio smette di poter fermare
         * cio' che ha avviato. */
        if (f->rate > 22050) f->rate = 22050;
    }
    if (f->bit == 16 && g_dma16 == AUDIO_DMA_NESSUNO) f->bit = 8;
    if (f->rate < 4000)  f->rate = 4000;
    if (f->rate > 44100) f->rate = 44100;

    g_a16    = (f->bit == 16);
    g_canali = f->canali;
    g_canale = g_a16 ? g_dma16 : g_dma8;

    /* --- quanto grande il buffer: mezzo decimo di secondo per meta' ---
     * Una meta' troppo grande e' ritardo che si sente in un gioco; una
     * troppo piccola e' un interrupt ogni pochi millisecondi, cioe' un
     * driver ring3 che non ce la fa. 50 ms per meta' e' il compromesso. */
    bps  = f->rate * f->canali * (f->bit / 8);
    meta = potenza_di_due(bps / 20, 2048, 16384);

    g_buf_byte = meta * 2;
    g_meta     = meta;
    g_dsp_bloc = meta;

    memset(g_buf, (f->bit == 8) ? 0x80 : 0x00, g_buf_byte);

    *buf  = g_buf;
    *byte = g_buf_byte;

    dsp_reset();
    dsp_scrivi(DSP_ALTOP_ON);

    if ((g_dsp >> 8) >= 4) {
        dsp_scrivi(DSP_RATE_OUT);
        dsp_scrivi((f->rate >> 8) & 0xFF);
        dsp_scrivi(f->rate & 0xFF);
    } else {
        /* ! LA COSTANTE DI TEMPO CONTA I CANALI. Su una SB Pro in stereo il
         * DSP tira due campioni per ogni «tempo», quindi la costante va
         * calcolata sul doppio della frequenza: darle quella mono fa suonare
         * tutto a meta' velocita', e la cosa si sente come un rallentamento
         * generale invece che come un errore di configurazione. */
        unsigned int hz = f->rate * f->canali;
        dsp_scrivi(DSP_TC);
        dsp_scrivi(256 - (1000000 / hz));
        if ((g_dsp >> 8) == 3)
            mix_scrivi(MIX_STEREO_PRO, (f->canali == 2) ? 0x02 : 0x00);
    }

    return 0;
}

static void sb_via(void)
{
    unsigned int lung;

    if (g_canale == AUDIO_DMA_NESSUNO) return;

    /* Il DMA gira sull'INTERO buffer in auto-init; il DSP lavora a blocchi di
     * meta'. Cosi' l'interrupt arriva a ogni meta' e il controller non si
     * ferma mai. */
    dma_programma(g_canale, g_buf_fis, g_buf_byte, 1);

    g_meta_prossima = 0;
    g_suona = 1;

    if ((g_dsp >> 8) >= 4) {
        /* ! LA LUNGHEZZA E' IN CAMPIONI, NON IN BYTE, ed e' l'errore che si
         * fa una volta sola: a 16 bit un campione sono due byte, quindi
         * passare i byte fa suonare ogni blocco DUE VOLTE — la seconda
         * meta' del suono e' la ripetizione della prima, e sembra un difetto
         * del buffer invece che un conto sbagliato. */
        unsigned int modo = (g_a16 ? 0x10 : 0x00) |     /* 16 bit = con segno */
                            ((g_canali == 2) ? 0x20 : 0x00);

        lung = g_a16 ? (g_dsp_bloc / 2) : g_dsp_bloc;

        dsp_scrivi(g_a16 ? 0xB6 : 0xC6);    /* D/A, auto-init, con FIFO */
        dsp_scrivi(modo);
        dsp_scrivi((lung - 1) & 0xFF);
        dsp_scrivi(((lung - 1) >> 8) & 0xFF);
    } else if (g_ai) {
        dsp_scrivi(DSP_BLOCCO);
        dsp_scrivi((g_dsp_bloc - 1) & 0xFF);
        dsp_scrivi(((g_dsp_bloc - 1) >> 8) & 0xFF);
        dsp_scrivi(DSP_AI_8);
    } else {
        /* DSP 1.x: un blocco per volta, riordinato a ogni interrupt. */
        dsp_scrivi(DSP_SC_8);
        dsp_scrivi((g_dsp_bloc - 1) & 0xFF);
        dsp_scrivi(((g_dsp_bloc - 1) >> 8) & 0xFF);
    }
}

static void sb_ferma(void)
{
    if (!g_suona) return;
    g_suona = 0;

    dsp_scrivi(g_a16 ? DSP_PAUSA_16 : DSP_PAUSA_8);
    dsp_reset();                        /* toglie anche l'auto-init */
    dma_ferma(g_canale);
}

static void sb_chiudi(void)
{
    sb_ferma();
    dsp_scrivi(DSP_ALTOP_OFF);
}

/* =============================================================================
 * L'interrupt
 * ========================================================================== */
static int sb_irq(void)
{
    int  st;
    unsigned int resta, posizione, in_corso;

    st = dentro(SB_STATO);              /* leggerlo E' l'ack a 8 bit */
    if (g_a16) dentro(SB_ACK16);        /* e questo quello a 16 bit  */
    (void)st;

    if (!g_suona) return -1;

    /* ! QUALE META' SI E' LIBERATA LO DICE IL CONTATORE, NON UN CONTEGGIO
     * NOSTRO. Alternare 0,1,0,1 a ogni interrupt sembra equivalente e non lo
     * e': basta perdere una notifica — la mailbox e' profonda quattro
     * messaggi e si puo' riempire — e da li' in poi il driver riempie
     * sempre la meta' che la scheda sta suonando. Il rumore che ne esce e'
     * continuo e non assomiglia a «un interrupt perso». */
    resta = dma_resta(g_canale);
    if (g_canale >= 4) resta = (resta + 1) * 2; else resta = resta + 1;

    posizione = (resta <= g_buf_byte) ? (g_buf_byte - resta) : 0;
    in_corso  = (posizione >= g_meta) ? 1 : 0;

    if (!g_ai && (g_dsp >> 8) < 4) {
        /* DSP 1.x: il blocco successivo va ordinato a mano. */
        dsp_scrivi(DSP_SC_8);
        dsp_scrivi((g_dsp_bloc - 1) & 0xFF);
        dsp_scrivi(((g_dsp_bloc - 1) >> 8) & 0xFF);
    }

    return (int)(1 - in_corso);         /* si e' liberata l'altra */
}

static unsigned int sb_avanzamento(void)
{
    return dma_resta(g_canale);
}

/* =============================================================================
 * Volume
 * ========================================================================== */
static void sb_volume(unsigned int pct)
{
    unsigned int ma = g_dsp >> 8;

    if (pct > 100) pct = 100;

    if (ma >= 4) {
        unsigned int v = (pct * 255) / 100;
        v &= 0xF8;                      /* solo i 5 bit alti contano */
        mix_scrivi(MIX_MASTER_L, v);
        mix_scrivi(MIX_MASTER_R, v);
        mix_scrivi(MIX_VOCE_L,   v);
        mix_scrivi(MIX_VOCE_R,   v);
    } else if (ma == 3) {
        unsigned int v = (pct * 15) / 100;
        mix_scrivi(MIX_MASTER_PRO, (v << 4) | v);
        mix_scrivi(0x04, (v << 4) | v);         /* voce */
    }
    /* SB 1.x e 2.0 non hanno un mixer: il volume e' la manopola sulla
     * staffa, ed e' giusto non fingere di poterlo cambiare. */
}

/* =============================================================================
 * MIDI — la MPU-401 se c'e', altrimenti la sintesi FM
 *
 * ! LA SINTESI FM NON E' UN RIPIEGO ELEGANTE: e' quello che facevano i
 * giochi. Un file MIDI suonato sull'OPL2 di una Sound Blaster suona come
 * suonava nel 1992, che e' esattamente cio' che ci si aspetta da questa
 * scheda. Chi vuole di meglio attacca un sintetizzatore alla MPU-401.
 *
 * Qui l'OPL riceve i messaggi MIDI tradotti al volo: nota su, nota giu',
 * cambio strumento (che sull'OPL2 e' un timbro solo, dichiarato).
 * ========================================================================== */
static const unsigned short g_fnum[12] = {
    345, 365, 387, 410, 435, 460, 488, 517, 547, 580, 614, 651
};

static unsigned int g_opl_nota[9];      /* quale nota tiene ogni canale FM */

static void opl_timbro(unsigned int ch)
{
    static const unsigned char op[9] = { 0x00, 0x01, 0x02, 0x08, 0x09,
                                         0x0A, 0x10, 0x11, 0x12 };
    unsigned int m = op[ch % 9];        /* modulante */
    unsigned int c = m + 3;             /* portante  */

    if (!g_fm) return;

    opl_scrivi(g_fm, 0x20 + m, 0x01);   /* moltiplicatore 1, niente vibrato */
    opl_scrivi(g_fm, 0x20 + c, 0x01);
    opl_scrivi(g_fm, 0x40 + m, 0x10);   /* la modulante piano */
    opl_scrivi(g_fm, 0x40 + c, 0x00);   /* la portante a tutto volume */
    opl_scrivi(g_fm, 0x60 + m, 0xF0);   /* attacco rapido, decadimento lento */
    opl_scrivi(g_fm, 0x60 + c, 0xF0);
    opl_scrivi(g_fm, 0x80 + m, 0x77);
    opl_scrivi(g_fm, 0x80 + c, 0x77);
    opl_scrivi(g_fm, 0xC0 + (ch % 9), 0x0E);  /* FM, tutti e due gli altoparlanti */
}

static void opl_nota_su(unsigned int ch, unsigned int nota, unsigned int vel)
{
    unsigned int blocco, f;

    if (!g_fm || nota < 12) return;
    ch %= 9;

    blocco = (nota / 12);
    blocco = (blocco > 1) ? (blocco - 1) : 0;
    if (blocco > 7) blocco = 7;
    f = g_fnum[nota % 12];

    opl_timbro(ch);
    /* La velocita' diventa attenuazione della portante: 0 = forte, 63 = muta */
    {
        static const unsigned char op[9] = { 0x00, 0x01, 0x02, 0x08, 0x09,
                                             0x0A, 0x10, 0x11, 0x12 };
        unsigned int att = (vel >= 127) ? 0 : ((127 - vel) * 40) / 127;
        opl_scrivi(g_fm, 0x40 + op[ch] + 3, att & 0x3F);
    }

    opl_scrivi(g_fm, 0xA0 + ch, f & 0xFF);
    opl_scrivi(g_fm, 0xB0 + ch, 0x20 | ((blocco & 7) << 2) | ((f >> 8) & 3));
    g_opl_nota[ch] = nota;
}

static void opl_nota_giu(unsigned int ch)
{
    if (!g_fm) return;
    ch %= 9;
    opl_scrivi(g_fm, 0xB0 + ch, 0x00);      /* spegne il bit di «tasto premuto» */
    g_opl_nota[ch] = 0;
}

/* Lo stato del decodificatore MIDI: i messaggi arrivano a pezzi e il byte di
 * stato si puo' sottintendere (running status). */
static unsigned char g_midi_st = 0;
static unsigned char g_midi_arg[2];
static unsigned int  g_midi_n = 0;

static void midi_messaggio(unsigned char st, unsigned char a, unsigned char b)
{
    unsigned int ch = st & 0x0F;

    switch (st & 0xF0) {
    case 0x90:                                  /* nota su */
        if (b == 0) opl_nota_giu(ch);
        else        opl_nota_su(ch, a, b);
        break;
    case 0x80:                                  /* nota giu' */
        opl_nota_giu(ch);
        break;
    default:
        /* Cambio strumento, controlli, pitch bend: l'OPL2 qui ha un timbro
         * solo, e fingere di cambiarlo sarebbe peggio che dichiararlo. */
        break;
    }
}

static int sb_midi(const unsigned char *b, unsigned int n)
{
    unsigned int i;

    /* Con una MPU-401 i byte passano come sono: dall'altra parte c'e' un
     * sintetizzatore vero che li capisce meglio di noi. */
    if (g_mpu) {
        for (i = 0; i < n; i++) {
            if (!mpu_pronto_a_scrivere()) return (int)i;
            ioport_out(g_mpu + MPU_DATI, b[i]);
        }
        return (int)n;
    }

    if (!g_fm) return 0;

    for (i = 0; i < n; i++) {
        unsigned char c = b[i];

        if (c & 0x80) {
            if (c >= 0xF8) continue;            /* tempo reale: si ignora */
            g_midi_st = c;
            g_midi_n  = 0;
            continue;
        }
        if (g_midi_st == 0) continue;           /* dato senza stato: si butta */

        g_midi_arg[g_midi_n++] = c;

        {
            unsigned int quanti = ((g_midi_st & 0xF0) == 0xC0 ||
                                   (g_midi_st & 0xF0) == 0xD0) ? 1 : 2;
            if (g_midi_n >= quanti) {
                midi_messaggio(g_midi_st, g_midi_arg[0],
                               (quanti > 1) ? g_midi_arg[1] : 0);
                g_midi_n = 0;
            }
        }
    }
    return (int)n;
}

static int sb_midi_vivo(void)
{
    if (g_mpu) return mpu_pronto_a_scrivere() ? 0 : -1;
    if (g_fm)  return opl_c_e(g_fm) ? 0 : -1;
    return -1;
}

/* =============================================================================
 * Le opzioni della riga di comando
 * ========================================================================== */
static int sb_opzione(const char *arg, const char *valore)
{
    if (!valore) return -1;

    if (strcmp(arg, "-p") == 0) { g_base_forz  = (int)strtol(valore, 0, 0); return 0; }
    if (strcmp(arg, "-q") == 0) { g_irq_forz   = (int)strtol(valore, 0, 0); return 0; }
    if (strcmp(arg, "-d") == 0) { g_dma8_forz  = (int)strtol(valore, 0, 0); return 0; }
    if (strcmp(arg, "-D") == 0) { g_dma16_forz = (int)strtol(valore, 0, 0); return 0; }
    return -1;
}

/* =============================================================================
 * Il dorso
 * ========================================================================== */
static const AudioDorso g_dorso = {
    "sb",
    sb_opzione,
    sb_sonda,
    sb_apri,
    sb_via,
    sb_ferma,
    sb_chiudi,
    sb_irq,
    sb_avanzamento,
    sb_volume,
    sb_midi,
    sb_midi_vivo
};

const AudioDorso *audio_dorso_questo(void)
{
    return &g_dorso;
}

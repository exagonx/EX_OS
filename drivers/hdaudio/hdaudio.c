/* =============================================================================
 * drivers/hdaudio/hdaudio.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * INTEL HD AUDIO — dove sta il Realtek ALC di qualunque scheda madre di oggi
 *
 *     /dev/hdaudio.drv       cerca il controller e accende il servizio "audio"
 *     /dev/hdaudio.drv -i    la sonda: dice cosa ha trovato ed esce
 *
 * La meta' comune — anello, protocollo, prove, sintetizzatore software — sta
 * in drivers/audio/audio_comune.c. Qui ci sono soltanto i registri.
 *
 * -----------------------------------------------------------------------------
 * ! QUESTO NON E' UN DRIVER DI UNA SCHEDA: E' IL DRIVER DI UN BUS
 *
 * HD Audio (2004) rovescia il modo in cui si e' scritto audio per vent'anni.
 * Prima il driver sapeva com'era fatta la scheda: due canali, un mixer, questo
 * registro per il volume. Qui il controller non sa NIENTE di audio — sposta
 * blocchi di byte e recapita comandi — e cio' che sa suonare sta dall'altra
 * parte di un collegamento seriale, dentro un CODEC che si deve INTERROGARE.
 *
 * Un codec ha dei «widget» numerati, ognuno con un tipo: convertitore di
 * uscita (il DAC vero), presa (il connettore verde sul retro), miscelatore,
 * selettore. Come sono collegati fra loro lo dice il codec stesso, widget per
 * widget. Il driver deve quindi:
 *
 *     1. resettare il controller e vedere quali codec rispondono
 *     2. chiedere al codec quanti gruppi di funzioni ha, e quale e' audio
 *     3. percorrere i widget di quel gruppo
 *     4. sceglierne DUE — un convertitore d'uscita e una presa che esca fuori
 *     5. accenderli, collegarli, alzarne i volumi
 *
 * ! ED E' PER QUESTO CHE SI SCRIVE UN DRIVER SOLO PER TUTTI I REALTEK. Un
 * ALC887 e un ALC1220 hanno topologie diverse, e nessuna delle due sta scritta
 * qui: si chiedono. Lo stesso codice guida il codec Analog Devices o VIA
 * montato sulla stessa scheda madre.
 *
 * -----------------------------------------------------------------------------
 * ! I COMANDI AL CODEC PASSANO DAI REGISTRI IMMEDIATI, NON DA CORB/RIRB
 *
 * La strada maestra sono due anelli in memoria — CORB per i comandi, RIRB per
 * le risposte — che il controller percorre da bus master. Servono due buffer
 * DMA in piu', due indici da tenere allineati e un secondo percorso di
 * interrupt, per fare una cosa che qui succede una manciata di volte in tutto:
 * all'accensione.
 *
 * I registri immediati (0x60/0x64/0x68) fanno lo stesso lavoro un comando per
 * volta, aspettando. Sono la strada che Linux stesso tiene come ripiego
 * (`single_cmd`), e per un driver che interroga il codec solo all'avvio sono
 * la strada giusta, non un compromesso. Se un giorno servira' mandare comandi
 * MENTRE si suona — cambiare presa a caldo, seguire una cuffia che si
 * infila — allora serviranno gli anelli.
 * ============================================================================= */

#include "libc.h"
#include "audio_proto.h"
#include "audio_dorso.h"
#include "pci_proto.h"

/* +0.001 a ogni modifica: `hdaudio.drv -version` la stampa. */
EX_VERSIONE("hdaudio.drv", "0.005");

/* =============================================================================
 * I registri del controller (MMIO, BAR0)
 * ========================================================================== */
#define H_GCAP          0x00    /* 16: quanti flussi ci sono */
#define H_GCTL          0x08    /* 32: bit 0 = fuori dal reset */
#define H_STATESTS      0x0E    /* 16: quali codec hanno risposto */
#define H_INTCTL        0x20    /* 32 */
#define H_INTSTS        0x24    /* 32 */
#define H_ICOI          0x60    /* 32: comando immediato */
#define H_ICII          0x64    /* 32: risposta immediata */
#define H_ICIS          0x68    /* 16: stato del comando immediato */

#define GCTL_CRST       0x00000001u
#define INTCTL_GIE      0x80000000u
#define INTCTL_CIE      0x40000000u

#define ICIS_BUSY       0x0001
#define ICIS_VALIDO     0x0002

/* Un descrittore di flusso: 0x20 byte, il primo a 0x80 */
#define SD_BASE         0x80
#define SD_PASSO        0x20
#define SD_CTL          0x00    /* 24 bit */
#define SD_STS          0x03
#define SD_LPIB         0x04    /* dove sta leggendo, in byte */
#define SD_CBL          0x08    /* lunghezza del giro */
#define SD_LVI          0x0C    /* 16 */
#define SD_FMT          0x12    /* 16 */
#define SD_BDLPL        0x18
#define SD_BDLPU        0x1C

#define SDCTL_SRST      0x000001u
#define SDCTL_RUN       0x000002u
#define SDCTL_IOCE      0x000004u
#define SDCTL_FEIE      0x000008u
#define SDCTL_DEIE      0x000010u
#define SDCTL_STRM_SH   20

#define SDSTS_BCIS      0x04
#define SDSTS_FIFOE     0x08
#define SDSTS_DESE      0x10

/* =============================================================================
 * I verbi del codec — quelli che servono, non tutti
 * ========================================================================== */
#define V_GET_PARAM     0xF0000
#define V_GET_CONN_LIST 0xF0200
#define V_SET_CONN_SEL  0x70100
#define V_SET_POWER     0x70500
#define V_SET_FORMATO   0x20000     /* verbo a 16 bit */
#define V_SET_FLUSSO    0x70600
#define V_SET_AMP       0x30000     /* verbo a 16 bit */
#define V_SET_PIN_CTL   0x70700
#define V_SET_EAPD      0x70C00

#define P_VENDOR        0x00
#define P_NODI          0x04
#define P_TIPO_GRUPPO   0x05
#define P_CAP_WIDGET    0x09
#define P_CAP_PIN       0x0C
#define P_AMP_USCITA    0x12    /* quanti passi ha l'amplificatore d'uscita */
#define P_CONFIG_DEF    0xF1C00     /* e' un verbo, non un parametro */

#define W_TIPO_SH       20
#define W_USCITA        0x0         /* convertitore d'uscita: il DAC */
#define W_PRESA         0x4         /* pin complex */

/* =============================================================================
 * Stato
 * ========================================================================== */
static volatile unsigned char *g_reg = 0;
static unsigned int g_irq   = 0;
static unsigned int g_codec = 0;
static unsigned int g_dac   = 0;    /* il widget che converte */
static unsigned int g_presa = 0;    /* il widget che esce fuori */

/* Tutte le prese d'uscita accese e i convertitori che le servono: vedi
 * trova_widget(). g_dac e g_presa restano i primi dei due elenchi. */
#define PRESE_MAX 8
#define DAC_MAX   6
static unsigned int g_prese[PRESE_MAX], g_n_prese = 0;
static unsigned int g_dacs[DAC_MAX],    g_n_dac = 0;
static unsigned int g_flusso = 1;   /* il numero di flusso, 1..15 */
static unsigned int g_passi  = 0x7F; /* passi dell'amplificatore, li dice il codec */
static unsigned int g_vol    = 80;
static unsigned int g_sd    = 0;    /* quale descrittore di flusso usiamo */
static char         g_nome[32];
static char         g_codec_nome[32];

static DmaZona      g_zona;
static unsigned int *g_bdl = 0;
static unsigned int  g_bdl_fis = 0;
static unsigned char *g_buf = 0;
static unsigned int  g_buf_fis = 0;
static unsigned int  g_buf_byte = 0;
static unsigned int  g_meta = 0;
static unsigned int  g_suona = 0;

static int g_forz_irq = -1;
static int g_guarda    = 0;     /* -d 1: si legge e si stampa, non si scrive niente */
static int g_coerenza  = 1;     /* -c 0: non si toccano i bit di coerenza nVidia */
static int g_forz_codec = -1;   /* -k N: il codec all'indirizzo N, non il primo */

/* =============================================================================
 * Accesso ai registri — sempre volatile, sempre della larghezza giusta
 *
 * ! LA LARGHEZZA NON E' UN DETTAGLIO. Alcuni registri di questo controller
 * reagiscono alla scrittura di UN byte in modo diverso da una parola: SD_CTL
 * e' a 24 bit e il byte alto porta il numero di flusso. Scriverlo a 32
 * toccherebbe anche SD_STS, che si azzera scrivendoci sopra — e si
 * azzererebbero interrupt mai visti.
 * ========================================================================== */
static unsigned int  r8 (unsigned int o) { return *(volatile unsigned char *)(g_reg + o); }
static unsigned int  r16(unsigned int o) { return *(volatile unsigned short *)(g_reg + o); }
static unsigned int  r32(unsigned int o) { return *(volatile unsigned int *)(g_reg + o); }
static void w8 (unsigned int o, unsigned int v) { *(volatile unsigned char *)(g_reg + o) = (unsigned char)v; }
static void w16(unsigned int o, unsigned int v) { *(volatile unsigned short *)(g_reg + o) = (unsigned short)v; }
static void w32(unsigned int o, unsigned int v) { *(volatile unsigned int *)(g_reg + o) = v; }

static unsigned int sd(unsigned int off) { return SD_BASE + g_sd * SD_PASSO + off; }

/* =============================================================================
 * Un comando al codec, e la sua risposta
 * ========================================================================== */
static int verbo(unsigned int nodo, unsigned int v, unsigned int dato,
                 unsigned int *risposta)
{
    unsigned int cmd;
    int guard;

    /* Il comando: codec, nodo, verbo, dato. I verbi «a 16 bit» hanno il dato
     * nei sedici bit bassi; gli altri negli otto. La differenza sta nel valore
     * di V_* che si passa, non qui. */
    cmd = ((g_codec & 0x0F) << 28) | ((nodo & 0x7F) << 20) | (v | (dato & 0xFFFF));

    for (guard = 0; guard < 10000; guard++)
        if (!(r16(H_ICIS) & ICIS_BUSY)) break;
    /* ! UN COMANDO RIMASTO SENZA RISPOSTA NON DEVE BLOCCARE TUTTI QUELLI DOPO.
     * Il codec di QEMU a certe domande non risponde affatto, e «occupato»
     * restava acceso per sempre: dopo un `-d 1` il driver non riusciva piu'
     * a dare un verbo, nemmeno il volume. Se e' ancora occupato lo si spegne
     * a mano e si va avanti: quel comando e' perso, il prossimo no. */
    if (r16(H_ICIS) & ICIS_BUSY) w16(H_ICIS, 0);

    w16(H_ICIS, ICIS_VALIDO);       /* azzera «risposta valida» scrivendoci sopra */
    w32(H_ICOI, cmd);
    w16(H_ICIS, ICIS_BUSY);

    for (guard = 0; guard < 100000; guard++) {
        unsigned int st = r16(H_ICIS);
        if (!(st & ICIS_BUSY) && (st & ICIS_VALIDO)) {
            if (risposta) *risposta = r32(H_ICII);
            return 0;
        }
    }
    w16(H_ICIS, 0);         /* nessuna risposta: si libera il posto per il prossimo */
    return -1;
}

static unsigned int passi_amp(unsigned int nodo);
static void         volume_su(unsigned int pct);

/* =============================================================================
 * Le voci del mixer (10 ottobre 2026): ogni presa d'uscita e ogni ingresso,
 * coi suoi due canali
 *
 * Un'USCITA e' una presa con la sua strada fino a un convertitore; il suo
 * volume sta nel primo nodo della strada che ha un amplificatore a passi
 * (su un ALC888 il miscelatore davanti alla presa, sul codec di QEMU il
 * convertitore). Perche' i volumi siano davvero uno per presa, ogni presa
 * prende un convertitore suo finche' ce ne sono: vedi apri_strada().
 *
 * Un INGRESSO e' una presa (microfono, linea in, CD) che il codec sa
 * mescolare nelle uscite: un ingresso del «miscelatore analogico», col suo
 * amplificatore. Nasce a zero - muto - come lo lascia il codec.
 * ========================================================================== */
#define VOCI_MAX 16
typedef struct {
    unsigned char tipo;         /* AUDIO_MIX_* */
    unsigned char presa;        /* il widget della presa */
    unsigned char nodo;         /* dove sta il volume (0 = solo acceso/spento sulla presa) */
    unsigned char ing;          /* ingresso: l'indice dentro `nodo` */
    unsigned char passi;        /* quanti passi ha quell'amplificatore */
    unsigned char presa_amp;    /* la presa ha un amplificatore d'uscita suo */
    unsigned char sin, des;     /* 0..100 */
    char          nome[40];
} Voce;
static Voce         g_voci[VOCI_MAX];
static int          g_n_voci = 0;

/* I miscelatori attraversati dalle strade d'uscita, e per ognuno l'ingresso
 * da cui entra il miscelatore analogico: si apre quando un ingresso non e'
 * a zero, si richiude quando lo sono tutti. */
#define PONTI_MAX 8
static unsigned char g_ponte_mix[PONTI_MAX], g_ponte_idx[PONTI_MAX];
static int           g_n_ponti = 0;

static unsigned int  g_strada_vol = 0;      /* lo riempie apri_strada() */
static unsigned char g_strada_mix[8];
static int           g_strada_n_mix = 0;

static unsigned int parametro(unsigned int nodo, unsigned int p)
{
    unsigned int r = 0;
    if (verbo(nodo, V_GET_PARAM, p, &r) < 0) return 0;
    return r;
}

/* =============================================================================
 * La ricerca dei due widget che servono
 *
 * ! SI SCEGLIE LA PRESA GUARDANDO COM'E' CONFIGURATA, non la prima che
 * capita. Il «configuration default» di ogni presa dice a cosa e' cablata:
 * un connettore sul retro, un jack frontale, o NIENTE — perche' un ALC ha
 * quindici prese e la scheda madre ne collega cinque. Prendere la prima
 * significa, in un caso su tre, mandare il suono a un connettore che sulla
 * scheda non e' saldato.
 * ========================================================================== */
#define P_CONN_LEN      0x0E
#define P_AMP_INGRESSO  0x0D

/* L'elenco delle connessioni di un nodo: da chi puo' prendere il suono. Rende
 * quante sono. La forma corta (un byte per voce) copre tutti i codec comuni;
 * il bit 7 di una voce vuol dire «da quella di prima fino a questa». */
static int connessioni(unsigned int n, unsigned int *v, int max)
{
    unsigned int len = parametro(n, P_CONN_LEN), q = len & 0x7F, i;
    int tot = 0;

    if (len & 0x80) return 0;                   /* forma lunga: non la si incontra */
    for (i = 0; i < q && tot < max; i += 4) {
        unsigned int r = 0, k;

        if (verbo(n, V_GET_CONN_LIST, i, &r) < 0) break;
        for (k = 0; k < 4 && i + k < q && tot < max; k++) {
            unsigned int e = (r >> (k * 8)) & 0xFF;

            if ((e & 0x80) && tot > 0) {
                unsigned int da = v[tot - 1] + 1, a = e & 0x7F;

                while (da <= a && tot < max) v[tot++] = da++;
            } else {
                v[tot++] = e & 0x7F;
            }
        }
    }
    return tot;
}

/* Dal nodo `n` all'indietro fino a un convertitore d'uscita, aprendo ogni
 * passo. Rende il convertitore raggiunto, 0 se da qui non ci si arriva. */
static unsigned int apri_strada(unsigned int n, int prof, int nuovi)
{
    unsigned int cap  = parametro(n, P_CAP_WIDGET);
    unsigned int tipo = (cap >> W_TIPO_SH) & 0x0F;
    unsigned int c[16];
    int k, i;

    if (tipo == W_USCITA) {
        /* `nuovi`: un convertitore gia' dato a un'altra presa non vale. Cosi'
         * ogni presa ha la sua strada e il suo volume, finche' ce ne sono. */
        if (nuovi) {
            unsigned int j;

            for (j = 0; j < g_n_dac; j++) if (g_dacs[j] == n) return 0;
        }
        verbo(n, V_SET_POWER, 0x00, 0);
        if ((cap & 0x4) && ((parametro(n, P_AMP_USCITA) >> 8) & 0x7F)) g_strada_vol = n;
        return n;
    }
    if (prof > 5 || tipo == 0x1 || tipo > W_PRESA) return 0;   /* ne' ingressi ne' altro */
    if (prof > 0 && tipo == W_PRESA) return 0;  /* una presa in mezzo e' un ingresso */

    k = connessioni(n, c, 16);
    for (i = 0; i < k; i++) {
        unsigned int d = apri_strada(c[i], prof + 1, nuovi);

        if (!d) continue;
        verbo(n, V_SET_POWER, 0x00, 0);
        if (tipo == 0x2) {
            /* Un miscelatore: ogni ingresso ha il suo amplificatore, e nasce
             * muto. Si apre quello da cui arriva il nostro suono, a 0 dB. */
            unsigned int zero = parametro(n, P_AMP_INGRESSO) & 0x7F;

            verbo(n, V_SET_AMP, 0x4000 | 0x2000 | 0x1000 | ((unsigned int)i << 8) | zero, 0);
            if (g_strada_n_mix < 8) g_strada_mix[g_strada_n_mix++] = (unsigned char)n;
        } else if (k > 1) {
            verbo(n, V_SET_CONN_SEL, (unsigned int)i, 0);   /* un selettore, o una presa */
        }
        /* E l'amplificatore d'uscita del nodo, se ne ha uno e non e' la
         * presa (quello della presa lo regola il volume): a 0 dB, non muto. */
        if ((cap & 0x4) && tipo != W_PRESA) {
            unsigned int zero = parametro(n, P_AMP_USCITA) & 0x7F;

            verbo(n, V_SET_AMP, 0x8000 | 0x2000 | 0x1000 | zero, 0);
            /* Il piu' vicino alla presa che abbia dei passi: li' sta il volume. */
            if ((parametro(n, P_AMP_USCITA) >> 8) & 0x7F) g_strada_vol = n;
        }
        return d;
    }
    return 0;
}

/* Il nome di una presa, dal suo «configuration default»: che cos'e', dove
 * sta sulla macchina, di che colore e' il connettore. */
static void nome_presa(char *out, unsigned int max, unsigned int cfg, int ingresso)
{
    static const char *colori[16] = { 0, "nera", "grigia", "blu", "verde", "rossa", "arancio",
                                      "gialla", "viola", "rosa", 0, 0, 0, 0, "bianca", 0 };
    unsigned int dev = (cfg >> 20) & 0xF, dove = (cfg >> 24) & 0x3F, col = (cfg >> 12) & 0xF;
    const char *cosa, *posto = "";

    switch (dev) {
    case 0x0: cosa = "Linea";        break;
    case 0x1: cosa = "Altoparlanti"; break;
    case 0x2: cosa = "Cuffie";       break;
    case 0x3: cosa = "CD";           break;
    case 0x8: cosa = "Linea in";     break;
    case 0x9: cosa = "Aux";          break;
    case 0xA: cosa = "Microfono";    break;
    default:  cosa = ingresso ? "Ingresso" : "Uscita"; break;
    }
    if ((dove & 0x30) == 0x10)     posto = " interno";
    else if ((dove & 0x0F) == 0x1) posto = " dietro";
    else if ((dove & 0x0F) == 0x2) posto = " davanti";

    if (colori[col]) snprintf(out, max, "%s%s (%s)", cosa, posto, colori[col]);
    else             snprintf(out, max, "%s%s", cosa, posto);
}

/* Porta al codec una voce com'e' scritta in g_voci. */
static void voce_applica(const Voce *v)
{
    unsigned int gs = ((unsigned int)v->sin * v->passi) / 100u;
    unsigned int gd = ((unsigned int)v->des * v->passi) / 100u;
    unsigned int ms = v->sin ? 0 : 0x80, md = v->des ? 0 : 0x80;

    if (v->tipo == AUDIO_MIX_USCITA) {
        if (v->nodo && v->nodo != v->presa) {
            verbo(v->nodo, V_SET_AMP, 0x8000 | 0x2000 | ms | gs, 0);
            verbo(v->nodo, V_SET_AMP, 0x8000 | 0x1000 | md | gd, 0);
            gs = gd = 0;                    /* la presa ha solo il muto */
        }
        if (!v->nodo) gs = gd = 0;
        /* ! SOLO SE LA PRESA UN AMPLIFICATORE CE L'HA. Scrivere il verbo a un
         * widget che non lo dichiara dovrebbe essere ignorato; il codec di
         * QEMU invece lo applica al convertitore, e il volume appena messo
         * tornava a zero. Non si mandano verbi a chi non li ha chiesti. */
        if (v->presa_amp) {
            verbo(v->presa, V_SET_AMP, 0x8000 | 0x2000 | ms | gs, 0);
            verbo(v->presa, V_SET_AMP, 0x8000 | 0x1000 | md | gd, 0);
        }
    } else {
        int k, aperto = 0;

        verbo(v->nodo, V_SET_AMP, 0x4000 | 0x2000 | ((unsigned int)v->ing << 8) | ms | gs, 0);
        verbo(v->nodo, V_SET_AMP, 0x4000 | 0x1000 | ((unsigned int)v->ing << 8) | md | gd, 0);
        if (v->sin || v->des) {
            /* La presa deve ascoltare: si ACCENDE l'ingresso lasciando il
             * resto come l'ha messo il BIOS (la tensione del microfono). */
            unsigned int ctl = 0;

            if (verbo(v->presa, 0xF0700, 0, &ctl) == 0 && !(ctl & 0x20))
                verbo(v->presa, V_SET_PIN_CTL, (ctl & 0xFF) | 0x20, 0);
        }
        /* Il ponte verso le uscite: aperto se almeno un ingresso non e' a zero. */
        for (k = 0; k < g_n_voci; k++)
            if (g_voci[k].tipo == AUDIO_MIX_INGRESSO && (g_voci[k].sin || g_voci[k].des)) aperto = 1;
        for (k = 0; k < g_n_ponti; k++) {
            unsigned int zero = parametro(g_ponte_mix[k], P_AMP_INGRESSO) & 0x7F;

            verbo(g_ponte_mix[k], V_SET_AMP, 0x4000 | 0x2000 | 0x1000 |
                  ((unsigned int)g_ponte_idx[k] << 8) | (aperto ? zero : 0x80), 0);
        }
    }
}

/* Gli ingressi che si possono sentire: per ogni miscelatore attraversato da
 * un'uscita, i suoi ALTRI ingressi che sono a loro volta un miscelatore; e
 * di quello, le prese che sanno entrare e che la scheda madre ha collegato. */
static void trova_ingressi(void)
{
    int m;

    for (m = 0; m < g_strada_n_mix; m++) {
        unsigned int c[16], j;
        int k = connessioni(g_strada_mix[m], c, 16);

        for (j = 0; (int)j < k; j++) {
            unsigned int b = c[j], cb[16], q;
            int kb, gia = 0, x;

            if (((parametro(b, P_CAP_WIDGET) >> W_TIPO_SH) & 0x0F) != 0x2) continue;
            if (!(parametro(b, P_CAP_WIDGET) & 0x2)) continue;     /* senza amplificatori d'ingresso */
            for (x = 0; x < g_n_ponti; x++)
                if (g_ponte_mix[x] == g_strada_mix[m] && g_ponte_idx[x] == j) gia = 1;
            if (!gia && g_n_ponti < PONTI_MAX) {
                g_ponte_mix[g_n_ponti] = g_strada_mix[m];
                g_ponte_idx[g_n_ponti] = (unsigned char)j;
                g_n_ponti++;
            }

            kb = connessioni(b, cb, 16);
            for (q = 0; (int)q < kb && g_n_voci < VOCI_MAX; q++) {
                unsigned int pin = cb[q], cfg = 0, dev;
                Voce *v;

                if (((parametro(pin, P_CAP_WIDGET) >> W_TIPO_SH) & 0x0F) != W_PRESA) continue;
                if (!(parametro(pin, P_CAP_PIN) & 0x20)) continue;         /* non sa entrare */
                verbo(pin, P_CONFIG_DEF, 0, &cfg);
                if (cfg == 0 || ((cfg >> 30) & 0x3) == 1) continue;        /* non collegata */
                dev = (cfg >> 20) & 0xF;
                if (dev != 0x3 && dev != 0x8 && dev != 0x9 && dev != 0xA) continue;
                for (x = 0, gia = 0; x < g_n_voci; x++)
                    if (g_voci[x].tipo == AUDIO_MIX_INGRESSO && g_voci[x].nodo == b &&
                        g_voci[x].ing == q) gia = 1;
                if (gia) continue;

                v = &g_voci[g_n_voci++];
                memset(v, 0, sizeof(*v));
                v->tipo  = AUDIO_MIX_INGRESSO;
                v->presa = (unsigned char)pin;
                v->nodo  = (unsigned char)b;
                v->ing   = (unsigned char)q;
                v->passi = (unsigned char)((parametro(b, P_AMP_INGRESSO) >> 8) & 0x7F);
                nome_presa(v->nome, sizeof(v->nome), cfg, 1);
            }
        }
    }
}

static int trova_widget(void)
{
    unsigned int nodi, primo, i, afg = 0;

    /* I gruppi di funzioni del codec, dal nodo radice. */
    nodi  = parametro(0, P_NODI);
    primo = (nodi >> 16) & 0xFF;
    nodi  = nodi & 0xFF;

    for (i = 0; i < nodi; i++) {
        unsigned int t = parametro(primo + i, P_TIPO_GRUPPO) & 0x7F;
        if (t == 0x01) { afg = primo + i; break; }      /* gruppo audio */
    }
    if (!afg) return -1;

    /* Il gruppo si accende PRIMA di parlare con i suoi widget: un gruppo in
     * risparmio energetico risponde alle domande e non suona. */
    verbo(afg, V_SET_POWER, 0x00, 0);
    usleep(10000);

    nodi  = parametro(afg, P_NODI);
    primo = (nodi >> 16) & 0xFF;
    nodi  = nodi & 0xFF;

    g_dac = g_presa = 0;
    g_n_dac = g_n_prese = 0;
    g_passi = 0x7F;
    g_n_voci = g_n_ponti = g_strada_n_mix = 0;

    /* =========================================================================
     * ! SI APRE TUTTA LA STRADA, DALLA PRESA AL CONVERTITORE (9 ottobre 2026).
     *
     * Fino a ieri si prendevano «il primo convertitore» e «la prima presa» e
     * si accendevano quei due. Va bene su un codec dove sono collegati
     * direttamente, come quello di QEMU. Su un Realtek ALC888 (il PC di
     * prova) fra i due c'e' un MISCELATORE, che nasce muto: il collaudo
     * diceva «suona» — i campioni partivano davvero — e dalle casse non
     * usciva niente.
     *
     * Adesso per ogni presa d'uscita (linea, altoparlanti, cuffie) si
     * percorre l'elenco delle connessioni all'indietro fino a un convertitore,
     * e a ogni passo si fa quel che serve a QUEL tipo di nodo: si accende, si
     * sceglie l'ingresso giusto, si toglie il muto. E si accendono TUTTE le
     * prese d'uscita collegate, ognuna col suo convertitore: lo stesso suono
     * esce da ogni presa, e non bisogna indovinare in quale sta il cavo.
     * ========================================================================= */
    for (i = 0; i < nodi && g_n_prese < PRESE_MAX; i++) {
        unsigned int n    = primo + i;
        unsigned int cap  = parametro(n, P_CAP_WIDGET);
        unsigned int tipo = (cap >> W_TIPO_SH) & 0x0F;
        unsigned int pcap, cfg = 0, dev, d, k;

        if (tipo != W_PRESA) continue;
        pcap = parametro(n, P_CAP_PIN);
        if (!(pcap & 0x10)) continue;                   /* non sa uscire */

        verbo(n, P_CONFIG_DEF, 0, &cfg);
        /* Bit 30-31 = «connettivita'»: 01 = nessuna connessione fisica, il
         * connettore che la scheda madre non ha saldato. Bit 20-23 = che
         * cos'e': 0 linea, 1 altoparlanti, 2 cuffie; il resto (CD, SPDIF,
         * microfono) non e' una presa da cui si ascolta. */
        if (cfg != 0 && ((cfg >> 30) & 0x3) == 1) continue;
        dev = (cfg >> 20) & 0xF;
        if (cfg != 0 && dev > 2) continue;

        /* Prima un convertitore che nessun'altra presa ha gia'; se sono
         * finiti, uno in comune: il suono arriva lo stesso, il volume e'
         * quello della presa con cui lo divide. */
        g_strada_vol = 0;
        d = apri_strada(n, 0, 1);
        if (!d) d = apri_strada(n, 0, 0);
        if (!d) continue;
        if ((cap & 0x4) && ((parametro(n, P_AMP_USCITA) >> 8) & 0x7F) && !g_strada_vol)
            g_strada_vol = n;                   /* il volume sta sulla presa stessa */

        if (g_n_voci < VOCI_MAX) {
            Voce *v = &g_voci[g_n_voci++];

            memset(v, 0, sizeof(*v));
            v->tipo  = AUDIO_MIX_USCITA;
            v->presa = (unsigned char)n;
            v->nodo  = (unsigned char)g_strada_vol;
            v->presa_amp = (cap & 0x4) ? 1 : 0;
            v->passi = g_strada_vol ? (unsigned char)((parametro(g_strada_vol, P_AMP_USCITA) >> 8) & 0x7F) : 0;
            v->sin = v->des = (unsigned char)g_vol;
            nome_presa(v->nome, sizeof(v->nome), cfg, 0);
        }

        verbo(n, V_SET_PIN_CTL, (dev == 2) ? 0xC0 : 0x40, 0);   /* uscita; cuffie col loro amplificatore */
        verbo(n, V_SET_EAPD, 0x02, 0);
        g_prese[g_n_prese++] = n;

        for (k = 0; k < g_n_dac; k++) if (g_dacs[k] == d) break;
        if (k == g_n_dac && g_n_dac < DAC_MAX) g_dacs[g_n_dac++] = d;

        { unsigned int a = passi_amp(d), b = passi_amp(n);
          if (a < g_passi) g_passi = a;
          if (b < g_passi) g_passi = b; }
    }

    if (g_n_prese == 0 || g_n_dac == 0) return -1;
    g_dac   = g_dacs[0];
    g_presa = g_prese[0];
    trova_ingressi();
    {
        int k, ing = 0;

        for (k = 0; k < g_n_voci; k++) if (g_voci[k].tipo == AUDIO_MIX_INGRESSO) ing++;
        printf("hdaudio: %u prese d'uscita accese, %u convertitori, %d ingressi\n",
               g_n_prese, g_n_dac, ing);
    }
    return 0;
}

/* ! IL VERBO DELL'AMPLIFICATORE: BIT 15 = USCITA, 14 = INGRESSO, 13 = SINISTRA,
 * 12 = DESTRA, 8-11 l'indice dell'ingresso, 7 il muto, 0-6 il guadagno. Fino
 * al 10 ottobre 2026 una funzione qui metteva 0x4000 dove serviva 0x1000: si
 * regolava il canale sinistro e il DESTRO restava come nasce - muto, sulle
 * prese di un ALC888. Ora i due canali si scrivono uno per volta, in
 * voce_applica(). */

/* =============================================================================
 * Quanto in alto puo' andare questo amplificatore — lo dice il codec
 *
 * ! NON SI SCRIVE UN NUMERO FISSO. I passi di un amplificatore HD Audio sono
 * da 1 a 127 a seconda del widget, e ogni passo vale una frazione di decibel
 * che pure cambia. Scrivere 0x50 su un amplificatore che di passi ne ha 31
 * non da' errore: il codec tiene i bit che gli servono e ignora gli altri, e
 * quel che si ottiene e' un volume a caso — che e' come e' venuto fuori la
 * prima volta, un tono giusto a un ventesimo del livello che doveva avere.
 * ========================================================================== */
static unsigned int passi_amp(unsigned int nodo)
{
    unsigned int cap = parametro(nodo, P_AMP_USCITA);
    unsigned int n   = (cap >> 8) & 0x7F;

    return n ? n : 0x7F;
}

static void volume_su(unsigned int pct)
{
    int k;

    /* Il volume generale porta tutte le uscite a quel valore, i due canali
     * insieme. Chi le vuole diverse usa le voci del mixer. */
    if (pct > 100) pct = 100;
    for (k = 0; k < g_n_voci; k++) {
        if (g_voci[k].tipo != AUDIO_MIX_USCITA) continue;
        g_voci[k].sin = g_voci[k].des = (unsigned char)pct;
        voce_applica(&g_voci[k]);
    }
}

/* Le voci com'erano: dopo un'apertura, che rimette mano al codec. */
static void voci_riapplica(void)
{
    int k;

    for (k = 0; k < g_n_voci; k++) voce_applica(&g_voci[k]);
}

static int hd_mix(int scrivi, AudioMixVoce *v)
{
    Voce *q;

    if (v->indice >= (unsigned int)g_n_voci) return -1;
    q = &g_voci[v->indice];
    if (scrivi) {
        q->sin = (unsigned char)(v->sin > 100 ? 100 : v->sin);
        q->des = (unsigned char)(v->des > 100 ? 100 : v->des);
        /* Senza passi c'e' solo acceso e spento: lo si dice nella risposta. */
        if (!q->passi) { if (q->sin) q->sin = 100; if (q->des) q->des = 100; }
        voce_applica(q);
    }
    v->tipo = q->tipo;
    v->sin  = q->sin;
    v->des  = q->des;
    memset(v->nome, 0, sizeof(v->nome));
    strncpy(v->nome, q->nome, sizeof(v->nome) - 1);
    return g_n_voci;
}

/* =============================================================================
 * PCI
 * ========================================================================== */
#define ATTESA_MS 2000
static int pci_pid = -1;

static int pci_risposta(unsigned int atteso, void *out, unsigned int len)
{
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           giri;

    for (giri = 0; giri < 8; giri++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), ATTESA_MS) < 0) return -1;
        if (meta.sender_pid != (unsigned int)pci_pid) continue;
        if (meta.tipo == PCI_MSG_FINE) return 1;
        if (meta.tipo != atteso) continue;
        if (meta.len < len) return -1;
        memcpy(out, buf, len);
        return 0;
    }
    return -1;
}

static int trova_controller(PciDispositivo *out)
{
    PciRichiesta r;
    unsigned int ord;

    for (ord = 0; ord < 16; ord++) {
        int esito;

        memset(&r, 0, sizeof(r));
        r.ordinale    = ord;
        r.classe      = 0x04;
        r.sottoclasse = 0x03;           /* HD Audio: la sottoclasse basta */
        r.venditore   = PCI_QUALUNQUE;
        r.dispositivo = PCI_QUALUNQUE;

        if (ipc_send((unsigned int)pci_pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0)
            return -1;
        esito = pci_risposta(PCI_MSG_DISPOSITIVO, out, sizeof(*out));
        if (esito != 0) return -1;

        /* ! LA SOTTOCLASSE 0x03 BASTA, e un elenco di modelli sarebbe peggio.
         * HD Audio e' una specifica: i registri sono gli stessi su Intel, AMD,
         * nVidia e VIA. Una tabella di identificativi qui vorrebbe dire
         * rifiutare il controller di domani per non averlo scritto ieri. */
        snprintf(g_nome, sizeof(g_nome), "HD Audio %04x:%04x",
                 out->venditore, out->dispositivo);
        return 0;
    }
    return -1;
}

static int abilita(const PciDispositivo *d)
{
    PciAzione a;
    PciEsito  e;

    memset(&a, 0, sizeof(a));
    a.bus = d->bus; a.slot = d->slot; a.funzione = d->funzione;
    a.bit = PCI_ABIL_MEMORIA | PCI_ABIL_BUSMASTER;

    if (ipc_send((unsigned int)pci_pid, PCI_MSG_ABILITA, &a, sizeof(a)) < 0)
        return -1;
    if (pci_risposta(PCI_MSG_ESITO, &e, sizeof(e)) != 0) return -1;
    return e.codice;
}

/* =============================================================================
 * Memoria: la lista dei descrittori piu' il buffer, in una zona sola
 * ========================================================================== */
#define BUF_BYTE  32768u
#define ZONA_TESTA 4096u

static int prendi_memoria(void)
{
    if (g_buf) return 0;

    memset(&g_zona, 0, sizeof(g_zona));
    g_zona.byte = ZONA_TESTA + BUF_BYTE;
    if (dma_alloc(&g_zona) < 0) {
        printf("hdaudio: dma_alloc(%u) fallita\n", ZONA_TESTA + BUF_BYTE);
        return -1;
    }
    g_bdl     = (unsigned int *)g_zona.virt;
    g_bdl_fis = g_zona.fisico;
    g_buf     = (unsigned char *)(g_zona.virt + ZONA_TESTA);
    g_buf_fis = g_zona.fisico + ZONA_TESTA;
    return 0;
}

/* =============================================================================
 * La sonda
 * ========================================================================== */
/* =============================================================================
 * ! I CONTROLLER NVIDIA VOGLIONO TRE BIT ACCESI, O IL SUONO E' SILENZIO
 * (10 ottobre 2026)
 *
 * Sul PC di prova (nVidia MCP73, codec ALC888) tutto rispondeva: il codec, il
 * collaudo, gli interrupt, il contatore della posizione che avanzava. E dalle
 * casse niente. Sui ponti nVidia il controller HD Audio legge la memoria del
 * flusso SENZA guardare le cache del processore, finche' non gli si dice di
 * farlo: quel che prende dal nostro buffer non e' quel che ci abbiamo scritto
 * un attimo prima. I bit stanno nello spazio di configurazione PCI, fuori
 * dalla specifica HD Audio:
 *     0x4C bit 0   coerenza dei flussi in uscita
 *     0x4D bit 0   coerenza dei flussi in ingresso
 *     0x4E bit 0-3 coerenza del canale dei comandi
 * Lo fanno tutti i sistemi che hanno un driver per questi ponti; la prima
 * regola di trova_controller() - «la sottoclasse basta» - resta vera per i
 * registri, e questa e' l'eccezione che sta FUORI dai registri.
 *
 * Il server PCI non scrive la configurazione, e lo spiega in pci_proto.h:
 * come ehci.drv, qui si aprono 0xCF8/0xCFC per conto proprio, per questi tre
 * byte e basta. Il caso peggiore se il rimedio fosse sbagliato: il suono
 * resta muto com'era. `-c 0` non li tocca, per provare la differenza.
 * ========================================================================== */
static unsigned int g_cfg_ind = 0;

static int cfg_apri(const PciDispositivo *d)
{
    g_cfg_ind = 0x80000000u | ((unsigned int)d->bus << 16) |
                ((unsigned int)d->slot << 11) | ((unsigned int)d->funzione << 8);
    return ioport_bind(0xCF8, 8);
}

static unsigned int cfg_leggi(unsigned int off)
{
    unsigned int v = 0xFFFFFFFFu;

    if (ioport_out32(0xCF8, g_cfg_ind | (off & 0xFC)) != 0) return 0xFFFFFFFFu;
    if (ioport_in32(0xCFC, &v) != 0) return 0xFFFFFFFFu;
    return v;
}

static void cfg_scrivi(unsigned int off, unsigned int val)
{
    if (ioport_out32(0xCF8, g_cfg_ind | (off & 0xFC)) != 0) return;
    (void)ioport_out32(0xCFC, val);
}

#define NV_COERENZA 0x000F0101u     /* 0x4C bit 0, 0x4D bit 0, 0x4E bit 0-3 */

static void nvidia_coerenza(const PciDispositivo *d)
{
    unsigned int prima, dopo;

    if (d->venditore != 0x10DE || !g_coerenza) return;
    if (cfg_apri(d) != 0) {
        printf("hdaudio: ioport_bind(0xCF8) rifiutata, i bit di coerenza restano come sono\n");
        return;
    }
    prima = cfg_leggi(0x4C);
    if (prima == 0xFFFFFFFFu) return;
    if ((prima & NV_COERENZA) != NV_COERENZA) cfg_scrivi(0x4C, prima | NV_COERENZA);
    dopo = cfg_leggi(0x4C);
    printf("hdaudio: nVidia, coerenza della memoria: 0x4C era %08x, ora %08x\n", prima, dopo);
}

/* =============================================================================
 * -d 1: com'e' fatto il codec e in che stato e', SENZA SCRIVERE NIENTE
 *
 * Si puo' lanciare mentre il driver vero e' acceso: non azzera il controller,
 * non accende widget, manda solo domande. E' quello che serve quando un codec
 * «risponde ma non suona»: chi e' collegato a chi, quale amplificatore e'
 * muto, quale presa e' accesa, che flusso ascolta ogni convertitore.
 * ========================================================================== */
static unsigned int chiedi_v(unsigned int nodo, unsigned int v, unsigned int dato)
{
    unsigned int r = 0;

    if (verbo(nodo, v, dato, &r) < 0) return 0xFFFFFFFFu;
    return r;
}

static void guarda_codec(unsigned int codec)
{
    static const char *tipi[16] = { "DAC", "ADC", "mix", "sel", "presa", "power",
                                    "volume", "beep", "?", "?", "?", "?", "?", "?", "?", "vend" };
    unsigned int nodi, primo, i, afg = 0;

    g_codec = codec;
    printf("codec %u: id %08x rev %08x\n", codec, parametro(0, P_VENDOR), parametro(0, 0x02));
    nodi  = parametro(0, P_NODI);
    primo = (nodi >> 16) & 0xFF;
    nodi &= 0xFF;
    for (i = 0; i < nodi; i++) {
        unsigned int t = parametro(primo + i, P_TIPO_GRUPPO);

        printf("  gruppo %02x tipo %02x\n", primo + i, t & 0xFF);
        if ((t & 0x7F) == 0x01 && !afg) afg = primo + i;
    }
    if (!afg) { printf("  nessun gruppo audio\n"); return; }

    printf("  afg %02x: power %08x gpio n %08x dati %08x maschera %08x dir %08x\n", afg,
           chiedi_v(afg, 0xF0500, 0), parametro(afg, 0x11),
           chiedi_v(afg, 0xF1500, 0), chiedi_v(afg, 0xF1600, 0), chiedi_v(afg, 0xF1700, 0));
    printf("  afg amp usc %08x amp ing %08x\n", parametro(afg, P_AMP_USCITA), parametro(afg, P_AMP_INGRESSO));

    nodi  = parametro(afg, P_NODI);
    primo = (nodi >> 16) & 0xFF;
    nodi &= 0xFF;
    for (i = 0; i < nodi; i++) {
        unsigned int n = primo + i, cap = parametro(n, P_CAP_WIDGET);
        unsigned int tipo = (cap >> W_TIPO_SH) & 0x0F, c[16];
        int k, j;

        printf("%02x %-6s cap %08x pw %x", n, tipi[tipo], cap, chiedi_v(n, 0xF0500, 0) & 0xFF);
        k = connessioni(n, c, 16);
        if (k > 0) {
            printf(" da");
            for (j = 0; j < k; j++) printf(" %02x", c[j]);
            if (k > 1 && tipo != 0x2) printf(" scelto %u", chiedi_v(n, 0xF0100, 0) & 0xFF);
        }
        printf("\n");
        if (cap & 0x4)
            printf("     amp usc cap %08x  S %02x D %02x\n", parametro(n, P_AMP_USCITA),
                   chiedi_v(n, 0xB0000, 0x8000 | 0x2000) & 0xFF, chiedi_v(n, 0xB0000, 0x8000) & 0xFF);
        if (cap & 0x2) {
            printf("     amp ing cap %08x ", parametro(n, P_AMP_INGRESSO));
            for (j = 0; j < (k > 0 ? k : 1) && j < 10; j++)
                printf(" [%d] %02x/%02x", j, chiedi_v(n, 0xB0000, 0x2000 | (unsigned int)j) & 0xFF,
                       chiedi_v(n, 0xB0000, (unsigned int)j) & 0xFF);
            printf("\n");
        }
        if (tipo == W_PRESA)
            printf("     presa cap %08x cfg %08x ctl %02x eapd %02x sente %08x\n", parametro(n, P_CAP_PIN),
                   chiedi_v(n, P_CONFIG_DEF, 0), chiedi_v(n, 0xF0700, 0) & 0xFF,
                   chiedi_v(n, 0xF0C00, 0) & 0xFF, chiedi_v(n, 0xF0900, 0));
        if (tipo == W_USCITA)
            printf("     flusso %02x formato %04x\n", chiedi_v(n, 0xF0600, 0) & 0xFF,
                   chiedi_v(n, 0xA0000, 0) & 0xFFFF);
    }
}

static void guarda_tutto(const PciDispositivo *d)
{
    unsigned int gcap = r16(H_GCAP), stati = r16(H_STATESTS), in_s = (gcap >> 8) & 0x0F, i;

    printf("hdaudio -d: %04x:%04x  GCAP %04x GCTL %08x STATESTS %04x INTCTL %08x INTSTS %08x\n",
           d->venditore, d->dispositivo, gcap, r32(H_GCTL), stati, r32(H_INTCTL), r32(0x24));
    if (cfg_apri(d) == 0)
        printf("  pci 0x04 %08x 0x44 %08x 0x4C %08x\n", cfg_leggi(0x04), cfg_leggi(0x44), cfg_leggi(0x4C));
    g_sd = in_s;
    printf("  flusso d'uscita %u: CTL %08x LPIB %08x CBL %08x LVI %04x FMT %04x BDL %08x\n", g_sd,
           r32(sd(SD_CTL)), r32(sd(0x04)), r32(sd(SD_CBL)), r16(sd(SD_LVI)), r16(sd(SD_FMT)),
           r32(sd(SD_BDLPL)));
    /* STATESTS si azzera leggendolo su qualche controller: si provano tutti. */
    for (i = 0; i < 4; i++) {
        g_codec = i;
        if (parametro(0, P_VENDOR) != 0 && parametro(0, P_VENDOR) != 0xFFFFFFFFu) guarda_codec(i);
    }
}

static int hd_sonda(AudioInfo *info)
{
    PciDispositivo d;
    MmioZona       m;
    unsigned int   gcap, in_stream, out_stream, stati;
    int            guard, i;

    pci_pid = ipc_attendi("pci", 5000);
    if (pci_pid <= 0) {
        printf("hdaudio: il server PCI non e' attivo.\n");
        printf("         Si accende con  /dev/pci.drv &  - oppure mettendo\n");
        printf("         pci = /dev/pci.drv in [modules] di kernel.cfg.\n");
        return -1;
    }

    if (trova_controller(&d) < 0) {
        printf("hdaudio: nessun controller HD Audio sul bus.\n");
        return -1;
    }

    g_irq = (g_forz_irq > 0) ? (unsigned int)g_forz_irq : d.irq_linea;
    if (g_irq == 0 || g_irq == 0xFF) {
        printf("hdaudio: il BIOS non ha assegnato un IRQ al controller.\n");
        return -1;
    }
    if (!d.bar[0] || d.bar_io[0]) {
        printf("hdaudio: il BAR0 non e' una finestra di memoria.\n");
        return -1;
    }

    if (abilita(&d) < 0) {
        printf("hdaudio: il server PCI non ha acceso memoria e bus master.\n");
        return -1;
    }

    /* ! I REGISTRI SONO IN MEMORIA, NON IN PORTE, ed e' la prima differenza
     * che si vede: niente ioport_bind, una finestra mappata nel processo. Il
     * puntatore e' volatile perche' quei valori cambiano senza che nessuno
     * scriva — vedi mmio_map in lib/include/libc.h. */
    memset(&m, 0, sizeof(m));
    m.fisico = d.bar[0];
    m.byte   = 0x4000;
    if (mmio_map(&m) < 0) {
        printf("hdaudio: mmio_map(0x%x) fallita\n", d.bar[0]);
        return -1;
    }
    g_reg = (volatile unsigned char *)m.virt;

    if (g_guarda) { guarda_tutto(&d); exit(0); }

    nvidia_coerenza(&d);

    /* --- fuori dal reset --- */
    w32(H_GCTL, 0);
    for (guard = 0; guard < 1000; guard++) { if (!(r32(H_GCTL) & GCTL_CRST)) break; usleep(100); }
    w32(H_GCTL, GCTL_CRST);
    for (guard = 0; guard < 1000; guard++) { if (r32(H_GCTL) & GCTL_CRST) break; usleep(100); }
    if (!(r32(H_GCTL) & GCTL_CRST)) {
        printf("hdaudio: il controller non esce dal reset.\n");
        return -1;
    }
    /* ! 521 MICROSECONDI DI ATTESA, e stanno sulla specifica. Il collegamento
     * seriale col codec ha bisogno di quel tempo per stabilizzarsi dopo il
     * reset; interrogare prima da' un STATESTS vuoto, cioe' «non c'e' nessun
     * codec» su una macchina che ne ha uno. */
    usleep(1000);

    stati = r16(H_STATESTS);
    if (stati == 0) {
        printf("hdaudio: il controller c'e', ma nessun codec risponde.\n");
        return -1;
    }
    for (i = 0; i < 15; i++) if (stati & (1u << i)) { g_codec = (unsigned int)i; break; }
    if (g_forz_codec >= 0) g_codec = (unsigned int)g_forz_codec;

    gcap       = r16(H_GCAP);
    in_stream  = (gcap >> 8)  & 0x0F;
    out_stream = (gcap >> 12) & 0x0F;
    if (out_stream == 0) {
        printf("hdaudio: il controller non dichiara nessun flusso in uscita.\n");
        return -1;
    }
    /* ! I DESCRITTORI D'USCITA VENGONO DOPO QUELLI D'INGRESSO, sempre. Usare
     * il descrittore 0 senza contare gli ingressi vuol dire programmare un
     * flusso di REGISTRAZIONE e aspettare che suoni. */
    g_sd = in_stream;

    if (trova_widget() < 0) {
        printf("hdaudio: il codec %u non ha un'uscita utilizzabile.\n", g_codec);
        return -1;
    }

    /* Chi e': l'identificativo del venditore del codec. 0x10EC e' Realtek. */
    {
        unsigned int vid = parametro(0, P_VENDOR);
        unsigned int ven = (vid >> 16) & 0xFFFF, dev = vid & 0xFFFF;

        if (ven == 0x10EC) snprintf(g_codec_nome, sizeof(g_codec_nome),
                                    "Realtek ALC%x", dev);
        else if (ven == 0x1013) snprintf(g_codec_nome, sizeof(g_codec_nome),
                                    "Cirrus Logic %04x", dev);
        else if (ven == 0x11D4) snprintf(g_codec_nome, sizeof(g_codec_nome),
                                    "Analog Devices %04x", dev);
        else if (ven == 0x8384) snprintf(g_codec_nome, sizeof(g_codec_nome),
                                    "SigmaTel %04x", dev);
        else snprintf(g_codec_nome, sizeof(g_codec_nome), "codec %04x:%04x", ven, dev);
    }

    if (prendi_memoria() < 0) return -1;

    memset(info, 0, sizeof(*info));
    snprintf(info->nome, sizeof(info->nome), "%s", g_codec_nome);
    strcpy(info->bus, "PCI");
    info->base     = 0;                 /* e' in memoria, non in porte */
    info->irq      = g_irq;
    info->dma8     = AUDIO_DMA_NESSUNO;
    info->dma16    = AUDIO_DMA_NESSUNO;
    /* HD Audio parte da 16 bit. Gli 8 bit non esistono su questo bus, e
     * prometterli vorrebbe dire convertire di nascosto. */
    info->capacita = AUDIO_CAP_PCM16 | AUDIO_CAP_STEREO | AUDIO_CAP_MIXER;
    info->rate_min = 44100;
    info->rate_max = 48000;

    printf("hdaudio: %s, codec %u, flusso %u, %s\n",
           g_nome, g_codec, g_sd, g_codec_nome);
    return 0;
}

/* =============================================================================
 * Il formato, come lo vuole il registro SD_FMT
 *
 * Non e' un numero di hertz: e' base + moltiplicatore + divisore. La base e'
 * 48000 o 44100, e da li' si sale e si scende per rapporti interi. Le
 * frequenze che non si ottengono cosi' NON ESISTONO su questo bus — 22050 si',
 * perche' e' 44100 diviso due; 32000 no.
 * ========================================================================== */
static unsigned int codifica_formato(unsigned int *rate, unsigned int canali)
{
    unsigned int base44 = 0, div = 0, mul = 0, f;

    if (*rate >= 46000)      { *rate = 48000; base44 = 0; div = 0; }
    else if (*rate >= 43000) { *rate = 44100; base44 = 1; div = 0; }
    else if (*rate >= 23000) { *rate = 24000; base44 = 0; div = 1; }
    else                     { *rate = 22050; base44 = 1; div = 1; }

    f  = (base44 ? 0x4000u : 0u);
    f |= (mul & 0x7) << 11;
    f |= (div & 0x7) << 8;
    f |= 0x0010u;                       /* 16 bit */
    f |= ((canali - 1) & 0x0F);
    return f;
}

static int hd_apri(AudioFormato *f, unsigned char **buf, unsigned int *byte)
{
    unsigned int fmt, meta;
    int guard;

    if (!g_buf) return -1;

    f->bit    = 16;
    f->canali = 2;
    fmt = codifica_formato(&f->rate, f->canali);

    {
        unsigned int bps = f->rate * 4;
        meta = 2048;
        while (meta < bps / 20 && meta < BUF_BYTE / 2) meta <<= 1;
    }
    g_meta     = meta;
    g_buf_byte = meta * 2;
    memset(g_buf, 0, g_buf_byte);
    *buf  = g_buf;
    *byte = g_buf_byte;

    /* --- azzeramento del descrittore di flusso --- */
    w8(sd(SD_CTL), SDCTL_SRST);
    for (guard = 0; guard < 1000; guard++) { if (r8(sd(SD_CTL)) & SDCTL_SRST) break; usleep(100); }
    w8(sd(SD_CTL), 0);
    for (guard = 0; guard < 1000; guard++) { if (!(r8(sd(SD_CTL)) & SDCTL_SRST)) break; usleep(100); }

    /* --- la lista: due voci da 16 byte, le due meta', tutte e due con IOC --- */
    g_bdl[0] = g_buf_fis;       g_bdl[1] = 0;
    g_bdl[2] = g_meta;          g_bdl[3] = 1;          /* IOC */
    g_bdl[4] = g_buf_fis + g_meta; g_bdl[5] = 0;
    g_bdl[6] = g_meta;          g_bdl[7] = 1;

    w32(sd(SD_BDLPL), g_bdl_fis);
    w32(sd(SD_BDLPU), 0);
    w32(sd(SD_CBL), g_buf_byte);
    w16(sd(SD_LVI), 1);
    w16(sd(SD_FMT), fmt);

    /* Il numero di flusso va scritto sia nel descrittore sia nel codec: e'
     * l'etichetta con cui i byte viaggiano sul collegamento seriale, e i due
     * capi devono usare la stessa. */
    w32(sd(SD_CTL), (g_flusso << SDCTL_STRM_SH));
    {
        unsigned int k;

        /* Lo stesso flusso a ogni convertitore: lo stesso suono da ogni presa. */
        for (k = 0; k < g_n_dac; k++) {
            verbo(g_dacs[k], V_SET_FORMATO, fmt, 0);
            verbo(g_dacs[k], V_SET_FLUSSO, (g_flusso << 4) | 0, 0);
        }
    }

    voci_riapplica();

    /* Gli interrupt del nostro flusso, e quelli globali. */
    w32(H_INTCTL, INTCTL_GIE | INTCTL_CIE | (1u << g_sd));
    return 0;
}

/* I conti di una riproduzione, scritti nel registro del kernel alla
 * chiusura (dmesg hdaudio): servono a capire un suono che salta. */
static unsigned int g_st_irq = 0, g_st_confine = 0, g_st_ritardo = 0, g_st_doppie = 0;
static int          g_st_ultima = -1;

static void hd_via(void)
{
    g_st_ultima = -1;
    w8(sd(SD_STS), SDSTS_BCIS | SDSTS_FIFOE | SDSTS_DESE);
    w32(sd(SD_CTL), (g_flusso << SDCTL_STRM_SH) |
                    SDCTL_RUN | SDCTL_IOCE | SDCTL_FEIE | SDCTL_DEIE);
    g_suona = 1;
}

static void hd_ferma(void)
{
    if (!g_suona) return;
    g_suona = 0;
    w32(sd(SD_CTL), (g_flusso << SDCTL_STRM_SH));
    w8(sd(SD_STS), SDSTS_BCIS | SDSTS_FIFOE | SDSTS_DESE);
}

static void hd_chiudi(void)
{
    hd_ferma();
    if (g_st_irq) {
        char r[160];

        snprintf(r, sizeof(r), "hdaudio: %u interrupt, %u letti prima del confine, "
                 "%u meta' saltate, ritardo massimo %u byte su %u",
                 g_st_irq, g_st_confine, g_st_doppie, g_st_ritardo, g_meta);
        log_seriale(r);
    }
    g_st_irq = g_st_confine = g_st_ritardo = g_st_doppie = 0;
    g_st_ultima = -1;
}

/* =============================================================================
 * L'interrupt
 * ========================================================================== */
static int hd_irq(void)
{
    unsigned int sts = r32(H_INTSTS);
    unsigned int st, pos;

    if (!(sts & (1u << g_sd))) return -1;

    st = r8(sd(SD_STS));
    w8(sd(SD_STS), st & (SDSTS_BCIS | SDSTS_FIFOE | SDSTS_DESE));

    if (!g_suona) return -1;

    /* ! LPIB DICE DOVE STA LEGGENDO, IN BYTE, e si crede a lui e non a un
     * conteggio nostro — stessa scelta della Sound Blaster e dell'AC'97, e
     * per lo stesso motivo: una notifica persa sfaserebbe per sempre chi
     * conta da se'. */
    pos = r32(sd(SD_LPIB));

    /* ! MA AL CONFINE LPIB PUO' ESSERE ANCORA UN PELO INDIETRO (10 ottobre
     * 2026). L'interrupt dice «ho finito una meta'»; su un controller vero
     * il contatore, letto subito dopo, puo' segnare ancora gli ultimi byte
     * di quella meta', non i primi della successiva. Con la regola secca
     * «pos >= meta» si riempiva allora la meta' SBAGLIATA: quella in cui la
     * scheda stava entrando, e la meta' appena finita restava coi campioni
     * vecchi. Sul PC vero (nVidia MCP73) erano buchi e pezzi ripetuti a
     * caso; in QEMU, dove il contatore e' esatto, mai.
     *
     * Si sposta il confine indietro di un quarto di meta': una lettura fino
     * a un quarto PRIMA del confine vale gia' come «finita», e resta buono
     * un ritardo nel servirlo fino a tre quarti di meta'. */
    {
        unsigned int giro = g_meta * 2, q = g_meta / 4;
        unsigned int spostato = (pos + q) % giro;
        int libera = (spostato >= g_meta) ? 0 : 1;
        unsigned int dentro = (spostato >= g_meta) ? spostato - g_meta : spostato;

        g_st_irq++;
        if (libera != ((pos >= g_meta) ? 0 : 1)) g_st_confine++;
        if (dentro > q && dentro - q > g_st_ritardo) g_st_ritardo = dentro - q;
        if (libera == g_st_ultima) g_st_doppie++;       /* la stessa due volte: una persa */
        g_st_ultima = libera;
        return libera;
    }
}

static unsigned int hd_avanzamento(void)
{
    return r32(sd(SD_LPIB));
}

/* =============================================================================
 * Volume
 *
 * L'amplificatore di un widget ha un guadagno a sette bit e un passo che il
 * codec dichiara; qui si usa la scala intera senza chiederla, perche' la
 * differenza fra un passo di 0.25 dB e uno di 1.5 dB si sente come «il volume
 * a meta' e' un po' piu' basso», non come un guasto.
 * ========================================================================== */
static void hd_volume(unsigned int pct)
{
    if (pct > 100) pct = 100;
    g_vol = pct;
    volume_su(pct);
}

/* =============================================================================
 * MIDI — non c'e'
 * ========================================================================== */
static int hd_midi(const unsigned char *b, unsigned int n)
{
    (void)b; (void)n;
    /* ! HD AUDIO NON HA NE' SINTETIZZATORE NE' PORTA MIDI, e nessun codec ne
     * ha uno. Il MIDI su questa scheda lo fa il sintetizzatore software della
     * meta' comune, che si accende da solo perche' qui non c'e' ne'
     * AUDIO_CAP_MIDI_FM ne' AUDIO_CAP_MIDI_UART. */
    return 0;
}

static int hd_midi_vivo(void) { return -1; }

static int hd_opzione(const char *arg, const char *valore)
{
    if (!valore) return -1;
    if (strcmp(arg, "-q") == 0) { g_forz_irq = (int)strtol(valore, 0, 0); return 0; }
    if (strcmp(arg, "-d") == 0) { g_guarda = (int)strtol(valore, 0, 0); return 0; }
    if (strcmp(arg, "-c") == 0) { g_coerenza = (int)strtol(valore, 0, 0); return 0; }
    if (strcmp(arg, "-k") == 0) { g_forz_codec = (int)strtol(valore, 0, 0); return 0; }
    return -1;
}

static const AudioDorso g_dorso = {
    "hdaudio",
    hd_opzione,
    hd_sonda,
    hd_apri,
    hd_via,
    hd_ferma,
    hd_chiudi,
    hd_irq,
    hd_avanzamento,
    hd_volume,
    hd_midi,
    hd_midi_vivo,
    hd_mix
};

const AudioDorso *audio_dorso_questo(void)
{
    return &g_dorso;
}

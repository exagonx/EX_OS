/* =============================================================================
 * lib/exsuono/exsuono.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * exsuono — vedi exsuono.h per che cosa offre. Qui c'e' il come.
 *
 *   - Il file si legge tutto in memoria. Un WAV e' gia' campioni; un MP3 si
 *     decodifica un pezzo per volta con minimp3 (lib/terze/minimp3), e
 *     all'apertura se ne scorrono le intestazioni per sapere quanto dura e
 *     dove comincia ogni pezzo: e' quell'indice che permette di saltare.
 *   - I campioni vanno al driver per l'anello condiviso di audio_proto.h, come
 *     fa `audio`. La posizione la dice il driver: quanti byte ha suonato.
 *   - Saltare vuol dire chiudere l'anello e riaprirlo da un altro punto: cosi'
 *     quel che c'era dentro non si sente, e il conto dei byte riparte.
 *
 * ! LE RISPOSTE DEL DRIVER SI ASPETTANO CON UN FILTRO (ipc_scegli). Chi usa
 * questa libreria e' quasi sempre un programma con una finestra, e nella sua
 * cassetta postale arrivano anche gli eventi del server: leggerli qui vorrebbe
 * dire perderli.
 * ============================================================================= */

#include "libc.h"
#include "audio_proto.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

#include "exsuono.h"

#define PEZZO_CAMPIONI  1152            /* per canale: un pezzo MP3 */
#define USCITA_MAX      (PEZZO_CAMPIONI * 8 * 2 * 2)    /* fino a 6 volte piu' fitto */
#define ATTESA_MS       2000

/* --- il brano ------------------------------------------------------------ */
static unsigned char *g_file = 0;
static unsigned int   g_len = 0;
static int            g_mp3 = 0;
static unsigned int   g_hz = 0, g_canali = 0, g_bit = 16;
static unsigned int   g_durata_ms = 0;

/* WAV: dove stanno i campioni e quanti sono (in «quadri»: un campione per
 * ogni canale). */
static unsigned int   g_wav_da = 0, g_wav_quadri = 0, g_wav_qui = 0;

/* MP3: l'indice dei pezzi, e a quale si e' arrivati. */
static mp3dec_t       g_dec;
static unsigned int  *g_pezzi = 0;      /* dove comincia ogni pezzo nel file */
static unsigned int   g_n_pezzi = 0, g_pezzo_qui = 0, g_per_pezzo = PEZZO_CAMPIONI;

/* --- la scheda ----------------------------------------------------------- */
static int            g_pid = -1;
static AudioAnello   *g_anello = 0;
static unsigned char *g_dati = 0;
static void          *g_zona = 0;
static unsigned int   g_o_hz = 0, g_o_canali = 0;   /* il formato CONCESSO */
static int            g_aperto = 0, g_partito = 0, g_pausa = 1, g_finito = 0;
static unsigned int   g_base_ms = 0;    /* dove si era quando si e' aperto l'anello */

/* --- i campioni pronti per l'anello, gia' nel formato della scheda ------- */
static short          g_in[PEZZO_CAMPIONI * 2];
static unsigned char  g_out[USCITA_MAX];
static unsigned int   g_out_n = 0, g_out_dati = 0;
static unsigned int   g_resto = 0;      /* la frazione del ricampionamento */

/* La fine del brano: il silenzio in coda e' stato messo, e da quando il
 * contatore dei byte suonati e' fermo. Vedi exsuono_passo(). */
static int            g_coda = 0;
static unsigned int   g_fermo_da = 0, g_fermo_a = 0;

/* a * b / c senza passare da 64 bit (la divisione a 64 bit qui non c'e'):
 * vale finche' (c - 1) * b sta in 32 bit, cioe' per tutti i conti di questo
 * file, che hanno b o c non piu' grandi di qualche centinaio di migliaia. */
static unsigned int muldiv(unsigned int a, unsigned int b, unsigned int c)
{
    return (a / c) * b + (a % c) * b / c;
}

static unsigned int le16(const unsigned char *p) { return (unsigned int)p[0] | ((unsigned int)p[1] << 8); }
static unsigned int le32(const unsigned char *p) { return le16(p) | (le16(p + 2) << 16); }

/* -----------------------------------------------------------------------------
 * Il file
 * --------------------------------------------------------------------------- */
/* Quanto leggere di un file: 0 = tutto. exsuono_durata() ne legge solo la
 * testa (vedi li'); g_vero e' la misura intera del file, letto o no. */
static unsigned int g_tetto = 0, g_vero = 0;

static void file_lascia(void)
{
    if (g_file)  free(g_file);
    if (g_pezzi) free(g_pezzi);
    g_file = 0; g_pezzi = 0; g_len = 0; g_n_pezzi = 0;
}

static int file_leggi(const char *nome)
{
    struct stat st;
    int fd, n, tot = 0;

    file_lascia();
    if (stat(nome, &st) != 0 || st.st_size <= 0) return EXSUONO_NO_FILE;
    g_len  = g_vero = (unsigned int)st.st_size;
    if (g_tetto && g_len > g_tetto) g_len = g_tetto;
    g_file = (unsigned char *)malloc(g_len);
    if (!g_file) return EXSUONO_NO_MEMORIA;
    fd = open(nome, O_RDONLY);
    if (fd < 0) { file_lascia(); return EXSUONO_NO_FILE; }
    while (tot < (int)g_len && (n = (int)read(fd, g_file + tot, g_len - (unsigned int)tot)) > 0)
        tot += n;
    close(fd);
    if ((unsigned int)tot < g_len) g_vero = (unsigned int)tot;     /* file piu' corto del detto */
    g_len = (unsigned int)tot;
    return 0;
}

/* Un WAV: si cercano i pezzi «fmt » e «data», che possono averne altri in
 * mezzo. Solo PCM non compresso, a 8 o 16 bit. */
static int wav_esamina(void)
{
    unsigned int p = 12, fmt = 0;

    if (g_len < 44 || memcmp(g_file, "RIFF", 4) != 0 || memcmp(g_file + 8, "WAVE", 4) != 0)
        return EXSUONO_NO_FILE;
    while (p + 8 <= g_len) {
        unsigned int len = le32(g_file + p + 4);

        if (memcmp(g_file + p, "fmt ", 4) == 0 && len >= 16 && p + 24 <= g_len) {
            if (le16(g_file + p + 8) != 1) return EXSUONO_NO_FILE;     /* compresso */
            g_canali = le16(g_file + p + 10);
            g_hz     = le32(g_file + p + 12);
            g_bit    = le16(g_file + p + 22);
            fmt = 1;
        } else if (memcmp(g_file + p, "data", 4) == 0) {
            if (!fmt || g_hz == 0 || g_canali < 1 || g_canali > 2 ||
                (g_bit != 8 && g_bit != 16)) return EXSUONO_NO_FILE;
            if (len > g_vero - (p + 8)) len = g_vero - (p + 8);
            g_wav_da     = p + 8;
            g_wav_quadri = len / (g_canali * (g_bit / 8));
            g_wav_qui    = 0;
            g_durata_ms  = muldiv(g_wav_quadri, 1000u, g_hz);
            return 0;
        }
        p += 8 + len + (len & 1);
    }
    return EXSUONO_NO_FILE;
}

/* Un MP3: si scorrono le intestazioni di tutti i pezzi, senza decodificarli,
 * per contarli e segnare dove comincia ciascuno. */
static int mp3_esamina(void)
{
    mp3dec_frame_info_t info;
    unsigned int p = 0, posti = 4096;

    g_pezzi = (unsigned int *)malloc(posti * sizeof(unsigned int));
    if (!g_pezzi) return EXSUONO_NO_MEMORIA;
    g_n_pezzi = 0;
    mp3dec_init(&g_dec);

    while (p < g_len) {
        int campioni = mp3dec_decode_frame(&g_dec, g_file + p, (int)(g_len - p), 0, &info);

        if (info.frame_bytes <= 0) break;
        if (campioni > 0) {
            if (g_n_pezzi == 0) {
                g_hz = (unsigned int)info.hz;
                g_canali = (unsigned int)info.channels;
                g_per_pezzo = (unsigned int)campioni;
            }
            if (g_n_pezzi == posti) {
                unsigned int *piu = (unsigned int *)realloc(g_pezzi, posti * 2 * sizeof(unsigned int));

                if (!piu) break;
                g_pezzi = piu; posti *= 2;
            }
            g_pezzi[g_n_pezzi++] = p;
        }
        p += (unsigned int)info.frame_bytes;
    }
    if (g_n_pezzi == 0 || g_hz == 0) return EXSUONO_NO_FILE;

    g_bit = 16;
    g_durata_ms = muldiv(g_n_pezzi * g_per_pezzo, 1000u, g_hz);
    g_pezzo_qui = 0;
    mp3dec_init(&g_dec);
    return 0;
}

static int e_mp3(const char *nome)
{
    unsigned int n = (unsigned int)strlen(nome);

    return n > 4 && nome[n - 4] == '.' && (nome[n - 3] | 0x20) == 'm' &&
           (nome[n - 2] | 0x20) == 'p' && nome[n - 1] == '3';
}

static int brano_carica(const char *nome)
{
    int r = file_leggi(nome);

    if (r != 0) return r;
    g_mp3 = e_mp3(nome);
    r = g_mp3 ? mp3_esamina() : wav_esamina();
    /* Un nome che mente: si prova l'altro formato prima di arrendersi. */
    if (r == EXSUONO_NO_FILE) {
        g_mp3 = !g_mp3;
        r = g_mp3 ? mp3_esamina() : wav_esamina();
    }
    if (r != 0) file_lascia();
    return r;
}

/* Il prossimo blocco di campioni del brano, a 16 bit, in g_in. Rende i quadri. */
static unsigned int brano_prossimo(void)
{
    if (g_mp3) {
        mp3dec_frame_info_t info;

        while (g_pezzo_qui < g_n_pezzi) {
            unsigned int p = g_pezzi[g_pezzo_qui++];
            int campioni = mp3dec_decode_frame(&g_dec, g_file + p, (int)(g_len - p), g_in, &info);

            if (campioni > 0) return (unsigned int)campioni;
        }
        return 0;
    } else {
        unsigned int n = g_wav_quadri - g_wav_qui, i, tot;
        const unsigned char *s;

        if (n > PEZZO_CAMPIONI) n = PEZZO_CAMPIONI;
        if (n == 0) return 0;
        tot = n * g_canali;
        s = g_file + g_wav_da + g_wav_qui * g_canali * (g_bit / 8);
        if (g_bit == 16) for (i = 0; i < tot; i++) g_in[i] = (short)le16(s + i * 2);
        else             for (i = 0; i < tot; i++) g_in[i] = (short)(((int)s[i] - 128) << 8);
        g_wav_qui += n;
        return n;
    }
}

static void brano_vai(unsigned int ms)
{
    if (ms > g_durata_ms) ms = g_durata_ms;
    if (g_mp3) {
        unsigned int k = muldiv(ms, g_hz, 1000u) / g_per_pezzo;

        if (k > g_n_pezzi) k = g_n_pezzi;
        g_pezzo_qui = k;
        mp3dec_init(&g_dec);
    } else {
        g_wav_qui = muldiv(ms, g_hz, 1000u);
        if (g_wav_qui > g_wav_quadri) g_wav_qui = g_wav_quadri;
    }
    g_out_n = g_out_dati = 0;
    g_resto = 0;
    g_finito = 0;
    g_coda = 0;
}

/* Da g_in (formato del file) a g_out (formato della scheda): si ripete o si
 * salta qualche campione per cambiare frequenza, si raddoppia o si media per
 * cambiare canali. Non e' alta fedelta'; e' la velocita' giusta. */
static void converti(unsigned int quadri)
{
    short *o = (short *)g_out;
    unsigned int i = 0, n = 0, max = USCITA_MAX / (g_o_canali * 2);

    while (i < quadri && n < max) {
        int s = g_in[i * g_canali];
        int d = (g_canali == 2) ? g_in[i * 2 + 1] : s;

        if (g_o_canali == 2) { o[n * 2] = (short)s; o[n * 2 + 1] = (short)d; }
        else                 o[n] = (short)((s + d) / 2);
        n++;
        g_resto += g_hz;
        while (g_resto >= g_o_hz) { g_resto -= g_o_hz; i++; }
    }
    g_out_n    = n * g_o_canali * 2;
    g_out_dati = 0;
}

/* -----------------------------------------------------------------------------
 * La scheda
 * --------------------------------------------------------------------------- */
typedef struct { int pid; unsigned int tipo; } Aspetto;

static int filtro(const IpcMessage *m, void *dato)
{
    const Aspetto *a = (const Aspetto *)dato;

    return ((int)m->sender_pid == a->pid && m->tipo == a->tipo) ? IPC_MIO : IPC_ALTRUI;
}

static int chiedi(unsigned int tipo, const void *dati, unsigned int len,
                  unsigned int risposta, void *out, unsigned int out_len)
{
    static unsigned char buf[IPC_MSG_MAX_DATA];
    IpcMessage meta;
    Aspetto a;

    if (ipc_send((unsigned int)g_pid, tipo, dati, len) < 0) return -1;
    if (!risposta) return 0;
    a.pid = g_pid; a.tipo = risposta;
    if (ipc_scegli(filtro, &a, &meta, buf, sizeof(buf), ATTESA_MS) < 0) return -1;
    if (meta.len < out_len) return -1;
    memcpy(out, buf, out_len);
    return 0;
}

static void scheda_chiudi(void)
{
    if (!g_aperto) return;
    ipc_send((unsigned int)g_pid, AUDIO_MSG_CHIUDI, 0, 0);
    if (g_zona) shm_chiudi(g_zona);
    g_zona = 0; g_anello = 0; g_dati = 0;
    g_aperto = 0; g_partito = 0;
}

static int scheda_apri(void)
{
    AudioFormato f;
    AudioEsito   e;
    ShmZona      z;

    g_pid = ipc_lookup(AUDIO_SERVIZIO);
    if (g_pid <= 0) return EXSUONO_NO_SCHEDA;

    f.rate = g_hz; f.canali = g_canali; f.bit = 16;
    if (chiedi(AUDIO_MSG_APRI, &f, sizeof(f), AUDIO_MSG_ESITO, &e, sizeof(e)) < 0)
        return EXSUONO_NO_SCHEDA;
    if (e.esito != 0) return EXSUONO_OCCUPATA;      /* c'e', ma e' di un altro */
    if (e.formato.bit != 16 || e.formato.rate == 0 ||
        e.formato.canali < 1 || e.formato.canali > 2)
        return EXSUONO_NO_SCHEDA;

    memset(&z, 0, sizeof(z));
    strcpy(z.nome, e.zona);
    z.byte = e.zona_byte;
    z.flag = 0;
    if (shm_apri(&z) < 0) {
        ipc_send((unsigned int)g_pid, AUDIO_MSG_CHIUDI, 0, 0);
        return EXSUONO_NO_SCHEDA;
    }
    g_zona     = (void *)z.virt;
    g_anello   = (AudioAnello *)z.virt;
    g_dati     = (unsigned char *)z.virt + g_anello->dati_off;
    g_o_hz     = e.formato.rate;
    g_o_canali = e.formato.canali;
    g_aperto   = 1;
    g_partito  = 0;
    return 0;
}

/* =============================================================================
 * Quel che si esporta
 * ============================================================================= */
int exsuono_durata(const char *file, unsigned int *ms)
{
    /* ! IL BRANO APERTO NON SI TOCCA: lo si mette da parte, si guarda l'altro
     * file con le stesse funzioni, e lo si rimette com'era. Chi fa un elenco
     * vuole le durate anche mentre suona. La scheda non c'entra: qui si legge
     * solo il file. */
    unsigned char *s_file = g_file;
    unsigned int  *s_pezzi = g_pezzi;
    unsigned int   s_len = g_len, s_hz = g_hz, s_canali = g_canali, s_bit = g_bit,
                   s_dur = g_durata_ms, s_wda = g_wav_da, s_wq = g_wav_quadri,
                   s_wqui = g_wav_qui, s_np = g_n_pezzi, s_pqui = g_pezzo_qui,
                   s_per = g_per_pezzo;
    int            s_mp3 = g_mp3, r;
    static mp3dec_t s_dec;

    s_dec = g_dec;
    g_file = 0; g_pezzi = 0; g_len = 0; g_n_pezzi = 0;

    /* ! SOLO LA TESTA DEL FILE (9 ottobre 2026). Si leggeva tutto il file per
     * dirne la durata, e chi fa un elenco lo chiede per ogni brano: da una
     * chiavetta erano secondi a brano, con la finestra ferma. Di un WAV basta
     * l'intestazione; di un MP3 si contano i pezzi della testa e si fa la
     * proporzione sul resto, che e' giusta a velocita' costante e vicina al
     * vero con quella variabile. La durata esatta la da' exsuono_apri(), che
     * il file lo legge tutto comunque. Se nella testa non c'e' un solo pezzo
     * (una copertina grande nell'etichetta ID3) si rilegge il file intero. */
    g_tetto = 128u * 1024u;
    r = brano_carica(file);
    g_tetto = 0;
    if (r != 0 && g_vero > 128u * 1024u) r = brano_carica(file);
    /* In kilobyte: muldiv() non regge due fattori cosi' grandi in byte. */
    if (r == 0 && g_mp3 && g_vero > g_len && g_n_pezzi > 0 &&
        ((g_len - g_pezzi[0]) >> 10) > 0)
        g_durata_ms = muldiv(g_durata_ms, (g_vero - g_pezzi[0]) >> 10,
                             (g_len - g_pezzi[0]) >> 10);
    if (r == 0 && ms) *ms = g_durata_ms;
    file_lascia();

    g_file = s_file; g_pezzi = s_pezzi; g_len = s_len; g_hz = s_hz;
    g_canali = s_canali; g_bit = s_bit; g_durata_ms = s_dur; g_wav_da = s_wda;
    g_wav_quadri = s_wq; g_wav_qui = s_wqui; g_n_pezzi = s_np;
    g_pezzo_qui = s_pqui; g_per_pezzo = s_per; g_mp3 = s_mp3;
    g_dec = s_dec;
    return r;
}

int exsuono_apri(const char *file, ExSuonoInfo *info)
{
    int r;

    exsuono_chiudi();
    r = brano_carica(file);
    if (r != 0) return r;
    r = scheda_apri();
    if (r != 0) { file_lascia(); return r; }

    brano_vai(0);
    g_base_ms = 0;
    g_pausa   = 1;
    if (info) {
        info->hz = g_hz; info->canali = g_canali;
        info->durata_ms = g_durata_ms; info->mp3 = g_mp3;
    }
    return 0;
}

void exsuono_chiudi(void)
{
    scheda_chiudi();
    file_lascia();
    g_pausa = 1; g_finito = 0; g_base_ms = 0;
}

int exsuono_suona(void)
{
    if (!g_file) return EXSUONO_NO_FILE;
    if (!g_aperto) {
        int r = scheda_apri();

        if (r != 0) return r;
    }
    if (g_pausa && g_partito) ipc_send((unsigned int)g_pid, AUDIO_MSG_VIA, 0, 0);
    g_pausa = 0;
    return 0;
}

void exsuono_pausa(void)
{
    if (!g_aperto || g_pausa) return;
    if (g_partito) ipc_send((unsigned int)g_pid, AUDIO_MSG_FERMA, 0, 0);
    g_pausa = 1;
}

unsigned int exsuono_posizione(void)
{
    unsigned int ms = g_base_ms;

    if (g_aperto && g_anello && g_o_hz)
        ms += muldiv(g_anello->suonato, 1000u, g_o_hz * g_o_canali * 2u);
    if (ms > g_durata_ms) ms = g_durata_ms;
    return ms;
}

int exsuono_vai(unsigned int ms)
{
    int era_in_pausa = g_pausa, r;

    if (!g_file) return EXSUONO_NO_FILE;
    if (ms > g_durata_ms) ms = g_durata_ms;

    /* Chiudere e riaprire: quel che era nell'anello non si deve sentire, e il
     * conto dei byte suonati riparte da zero da qui. */
    scheda_chiudi();
    brano_vai(ms);
    g_base_ms = ms;
    r = scheda_apri();
    if (r != 0) return r;
    g_pausa = era_in_pausa;
    return 0;
}

int exsuono_in_corso(void)
{
    return g_aperto && !g_pausa && !g_coda;
}

int exsuono_passo(void)
{
    if (!g_aperto || !g_anello) return 0;
    if (g_pausa) return 1;

    for (;;) {
        unsigned int libero = AUDIO_LIBERO(g_anello);

        if (g_out_dati == g_out_n) {
            unsigned int q;

            if (g_finito) break;
            q = brano_prossimo();
            if (q == 0) { g_finito = 1; break; }
            converti(q);
        }
        if (libero == 0) break;
        while (g_out_dati < g_out_n && libero > 0) {
            unsigned int testa  = g_anello->scritto & (g_anello->byte - 1);
            unsigned int tratto = g_anello->byte - testa;

            if (tratto > libero)               tratto = libero;
            if (tratto > g_out_n - g_out_dati) tratto = g_out_n - g_out_dati;
            memcpy(g_dati + testa, g_out + g_out_dati, tratto);
            g_anello->scritto += tratto;        /* i campioni prima, il contatore dopo */
            g_out_dati += tratto;
            libero     -= tratto;
        }
    }

    /* ! LA FINE NON E' «L'ANELLO E' VUOTO». Una scheda consuma a blocchi
     * interi e gli ultimi byte, meno di un blocco, non li prende mai: chi
     * aspetta l'anello vuoto aspetta per sempre. In fondo si mette del
     * silenzio, perche' l'ultimo blocco si completi, e il brano e' finito
     * quando il contatore dei byte suonati sta fermo da un terzo di secondo. */
    if (g_finito && g_out_dati == g_out_n && !g_coda) {
        unsigned int libero = AUDIO_LIBERO(g_anello), zeri = 16384;

        if (zeri > libero) zeri = libero;
        while (zeri > 0) {
            unsigned int testa  = g_anello->scritto & (g_anello->byte - 1);
            unsigned int tratto = g_anello->byte - testa;

            if (tratto > zeri) tratto = zeri;
            memset(g_dati + testa, 0, tratto);
            g_anello->scritto += tratto;
            zeri -= tratto;
        }
        g_coda = 1;
        g_fermo_a  = g_anello->suonato;
        g_fermo_da = uptime_ms();
    }

    if (!g_partito && (AUDIO_PIENO(g_anello) > g_anello->byte / 2 || g_coda)) {
        ipc_send((unsigned int)g_pid, AUDIO_MSG_VIA, 0, 0);
        g_partito = 1;
    }

    if (!g_coda) return 1;
    if (AUDIO_PIENO(g_anello) == 0) return 0;
    if (g_anello->suonato != g_fermo_a) {
        g_fermo_a  = g_anello->suonato;
        g_fermo_da = uptime_ms();
        return 1;
    }
    return (uptime_ms() - g_fermo_da) < 350u;
}

void exsuono_volume(unsigned int percento)
{
    AudioVolume v;
    int pid = ipc_lookup(AUDIO_SERVIZIO);

    if (pid <= 0) return;
    v.percento = percento > 100 ? 100 : percento;
    ipc_send((unsigned int)pid, AUDIO_MSG_VOLUME, &v, sizeof(v));
}

/* Una domanda al driver sul mixer, senza toccare il brano aperto: il driver
 * si cerca per nome ogni volta, perche' qui puo' non esserci niente di aperto. */
static int mix_chiedi(unsigned int tipo, unsigned int indice, unsigned int sin,
                      unsigned int des, ExSuonoVoce *v)
{
    static unsigned char buf[IPC_MSG_MAX_DATA];
    AudioMixVoce m;
    IpcMessage   meta;
    Aspetto      a;
    int pid = ipc_lookup(AUDIO_SERVIZIO);

    if (pid <= 0) return EXSUONO_NO_SCHEDA;
    memset(&m, 0, sizeof(m));
    m.indice = indice; m.sin = sin; m.des = des;
    if (ipc_send((unsigned int)pid, tipo, &m, sizeof(m)) < 0) return EXSUONO_NO_SCHEDA;
    a.pid = pid; a.tipo = AUDIO_MSG_MIX_R;
    /* Un driver di prima del 10 ottobre 2026 non risponde: mezzo secondo e via. */
    if (ipc_scegli(filtro, &a, &meta, buf, sizeof(buf), 500) < 0) return EXSUONO_NO_SCHEDA;
    if (meta.len < sizeof(m)) return EXSUONO_NO_SCHEDA;
    memcpy(&m, buf, sizeof(m));
    if (m.indice != indice) return EXSUONO_NO_FILE;         /* quella voce non c'e' */
    if (v) {
        v->tipo = m.tipo; v->sin = m.sin; v->des = m.des;
        memcpy(v->nome, m.nome, sizeof(v->nome));
        v->nome[sizeof(v->nome) - 1] = 0;
    }
    return (int)m.totale;
}

int exsuono_mix_leggi(unsigned int indice, ExSuonoVoce *v)
{
    return mix_chiedi(AUDIO_MSG_MIX_LEGGI, indice, 0, 0, v);
}

int exsuono_mix_metti(unsigned int indice, unsigned int sin, unsigned int des, ExSuonoVoce *v)
{
    return mix_chiedi(AUDIO_MSG_MIX_METTI, indice, sin > 100 ? 100 : sin,
                      des > 100 ? 100 : des, v);
}

int exsuono_avvia_file(const char *file)
{
    static const char *const dove[] = { "/bin/audio", "/cdrom/bin/audio" };
    struct stat st;
    unsigned int i;

    if (ipc_lookup(AUDIO_SERVIZIO) <= 0) return EXSUONO_NO_SCHEDA;
    if (stat(file, &st) != 0) return EXSUONO_NO_FILE;
    for (i = 0; i < sizeof(dove) / sizeof(dove[0]); i++) {
        if (stat(dove[i], &st) == 0) {
            char *const argv[] = { (char *)dove[i], (char *)file, 0 };
            int pid = spawn(dove[i], argv);

            return pid > 0 ? pid : EXSUONO_NO_FILE;
        }
    }
    return EXSUONO_NO_FILE;
}

void exsuono_ferma_file(int pid)
{
    if (pid > 1) interrompi(pid);
}

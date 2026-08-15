/* =============================================================================
 * bin/gfedit/gf_term.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * GF Edit — strato terminale: ciò che in GF_TEXTEDITOR faceva ncurses.
 *
 * Due metà, entrambe dovute al fatto che EX-OS consegna testo a righe e
 * non ha una libreria di schermo:
 *
 * USCITA — un FRAME OMBRA. Il resto del programma disegna in un array
 * 25x80 di coppie (carattere, attributo) senza mai emettere un byte;
 * gf_term_flush() confronta il frame appena composto con quello a video
 * e manda al TTY solo i tratti cambiati.
 *
 * Non è un'ottimizzazione facoltativa. Una schermata piena sono 2000
 * celle, e ogni cella attraversa write() -> sys_write -> vga_putchar,
 * con due out sulle porte del CRTC per aggiornare il cursore. Ridisegnare
 * tutto a ogni tasto significherebbe far lampeggiare lo schermo e
 * spendere millisecondi per una lettera; con il diff, premere un tasto
 * costa una manciata di celle. È la stessa ragione per cui ncurses
 * mantiene curscr, e qui pesa di più perché sotto non c'è un terminale
 * ma la memoria video vera.
 *
 * INGRESSO — i tasti uno per uno. Il TTY di EX-OS consegna una riga solo
 * su Invio, quindi non serve a niente per un editor. La console viene
 * messa in modalità raw parlando DIRETTAMENTE al servizio "kbd" via IPC
 * (KBD_MSG_SETMODE / KBD_MSG_READKEY, vedi drivers/kbd/kbd_proto.h):
 * la line discipline vive nel driver, non nel kernel, quindi è al
 * driver che bisogna chiedere.
 * ============================================================================= */

#include "gfedit.h"

/* =============================================================================
 * Frame ombra
 * ============================================================================= */
static unsigned char nuovo_ch[GF_ROWS][GF_COLS];
static unsigned char nuovo_at[GF_ROWS][GF_COLS];
static unsigned char video_ch[GF_ROWS][GF_COLS];
static unsigned char video_at[GF_ROWS][GF_COLS];

/* 1 = il contenuto di video_* non è attendibile e va riscritto tutto */
static int forza_ridisegno = 1;

/* Cursore desiderato dopo il flush */
static int cur_r = 0, cur_c = 0, cur_vis = 1;

/* =============================================================================
 * Buffer di uscita
 *
 * Una sola write() per fotogramma. Una write() per tratto costerebbe una
 * syscall ogni pochi caratteri, e il guadagno del diff se ne andrebbe
 * tutto lì.
 * ============================================================================= */
#define OUTBUF 4096
static char out[OUTBUF];
static int  out_len = 0;

static void out_flush(void)
{
    if (out_len > 0) {
        write(1, out, (size_t)out_len);
        out_len = 0;
    }
}

static void out_c(char c)
{
    if (out_len >= OUTBUF) out_flush();
    out[out_len++] = c;
}

static void out_s(const char *s)
{
    while (*s) out_c(*s++);
}

static void out_num(int v)
{
    char tmp[12];
    int  i = 0;

    if (v < 0) { out_c('-'); v = -v; }
    if (v == 0) { out_c('0'); return; }

    while (v > 0 && i < (int)sizeof(tmp)) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) out_c(tmp[--i]);
}

/* ESC[<r>;<c>H — le coordinate ANSI sono 1-based, le nostre 0-based */
static void out_vai(int r, int c)
{
    out_s("\x1B[");
    out_num(r + 1);
    out_c(';');
    out_num(c + 1);
    out_c('H');
}

/* =============================================================================
 * Emissione di un attributo VGA come sequenza SGR
 *
 * Il byte VGA è fg nei 4 bit bassi, bg nei 4 alti. In ANSI il primo
 * piano scuro è 30-37 e quello chiaro 90-97; lo sfondo solo 40-47,
 * perché vga.c non interpreta la serie 100-107 e sul VGA in modo testo
 * il bit alto dello sfondo è il lampeggio (vedi GF_AT in gfedit.h).
 *
 * ESC[0m in testa a ogni cambio: senza, un attributo che spegne il
 * "chiaro" non avrebbe modo di dirlo, perché il bit di intensità in
 * ANSI si accende ma non si spegne se non con un reset.
 * ============================================================================= */
static const unsigned char vga_a_ansi[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

static void out_attr(unsigned char attr)
{
    unsigned char fg = (unsigned char)(attr & 0x0F);
    unsigned char bg = (unsigned char)((attr >> 4) & 0x07);

    out_s("\x1B[0;");
    if (fg & 0x08) out_num(90 + vga_a_ansi[fg & 0x07]);
    else           out_num(30 + vga_a_ansi[fg]);
    out_c(';');
    out_num(40 + vga_a_ansi[bg]);
    out_c('m');
}

/* =============================================================================
 * gf_term_flush — riversa a video le sole celle cambiate
 *
 * L'ULTIMA CELLA DELLO SCHERMO (24,79) non viene mai scritta, ed è una
 * rinuncia voluta: scriverci porta il cursore fuori dalla riga 24, e
 * vga_putchar reagisce facendo scorrere lo schermo di una riga. Tutto il
 * disegno slitterebbe di una posizione a ogni fotogramma. È lo stesso
 * angolo che le librerie di schermo lasciano libero da sempre, per lo
 * stesso identico motivo.
 * ============================================================================= */
void gf_term_flush(void)
{
    int r, c;
    int attr_valido = 0;
    unsigned char attr_corrente = 0;
    int pos_r = -1, pos_c = -1;

    for (r = 0; r < GF_ROWS; r++) {
        c = 0;
        while (c < GF_COLS) {
            /* L'angolo in basso a destra è terra di nessuno */
            if (r == GF_ROWS - 1 && c == GF_COLS - 1) break;

            if (!forza_ridisegno &&
                nuovo_ch[r][c] == video_ch[r][c] &&
                nuovo_at[r][c] == video_at[r][c]) {
                c++;
                continue;
            }

            /* Riposiziona solo se non stiamo già scrivendo di seguito */
            if (pos_r != r || pos_c != c) {
                out_vai(r, c);
                pos_r = r;
                pos_c = c;
            }

            /* Scrive il tratto contiguo di celle da aggiornare. Un
             * salto di una o due celle uguali non vale la sequenza di
             * riposizionamento (che ne costa da sei a otto): si
             * riscrivono e si tira dritto. */
            while (c < GF_COLS) {
                int salto;

                if (r == GF_ROWS - 1 && c == GF_COLS - 1) break;

                if (!attr_valido || attr_corrente != nuovo_at[r][c]) {
                    out_attr(nuovo_at[r][c]);
                    attr_corrente = nuovo_at[r][c];
                    attr_valido   = 1;
                }

                out_c((char)nuovo_ch[r][c]);
                video_ch[r][c] = nuovo_ch[r][c];
                video_at[r][c] = nuovo_at[r][c];
                c++;
                pos_c = c;

                if (forza_ridisegno) continue;

                /* Quante celle uguali di seguito? */
                salto = 0;
                while (c + salto < GF_COLS &&
                       nuovo_ch[r][c + salto] == video_ch[r][c + salto] &&
                       nuovo_at[r][c + salto] == video_at[r][c + salto]) {
                    salto++;
                    if (salto > 3) break;
                }
                if (salto > 3) break;   /* conviene riposizionare */
            }
        }
    }

    /* Cursore hardware dove lo vuole l'editor */
    out_s(cur_vis ? "\x1B[?25h" : "\x1B[?25l");
    out_vai(cur_r, cur_c);

    out_flush();
    forza_ridisegno = 0;
}

/* =============================================================================
 * Composizione del frame
 * ============================================================================= */
void gf_term_pulisci(unsigned char attr)
{
    int r, c;

    for (r = 0; r < GF_ROWS; r++) {
        for (c = 0; c < GF_COLS; c++) {
            nuovo_ch[r][c] = ' ';
            nuovo_at[r][c] = attr;
        }
    }
}

void gf_term_cella(int r, int c, char ch, unsigned char attr)
{
    if (r < 0 || r >= GF_ROWS || c < 0 || c >= GF_COLS) return;

    /* I caratteri di controllo diventano spazi: la code page 437 li
     * renderebbe come glifi casuali (una nota musicale al posto di un
     * carattere nullo), e un file binario aperto per sbaglio
     * riempirebbe lo schermo di simboli. */
    if ((unsigned char)ch < 32) ch = ' ';

    nuovo_ch[r][c] = (unsigned char)ch;
    nuovo_at[r][c] = attr;
}

void gf_term_scrivi_n(int r, int c, const char *s, int n, unsigned char attr)
{
    int i;

    for (i = 0; i < n && s[i]; i++) {
        gf_term_cella(r, c + i, s[i], attr);
    }
}

void gf_term_scrivi(int r, int c, const char *s, unsigned char attr)
{
    gf_term_scrivi_n(r, c, s, GF_COLS, attr);
}

void gf_term_riempi(int r, int c, int n, char ch, unsigned char attr)
{
    int i;

    for (i = 0; i < n; i++) gf_term_cella(r, c + i, ch, attr);
}

/* =============================================================================
 * gf_term_riquadro — cornice a linea singola, in code page 437
 *
 * I codici sono quelli del set grafico dell'IBM PC (0xDA angolo alto a
 * sinistra e compagnia): la VGA in modo testo li disegna come segmenti
 * di linea, ed è così che i programmi DOS hanno sempre fatto le
 * finestre. Non serve nessuna libreria.
 * ============================================================================= */
void gf_term_riquadro(int r, int c, int h, int w, unsigned char attr)
{
    int i;

    if (h < 2 || w < 2) return;

    gf_term_cella(r, c, (char)0xDA, attr);
    gf_term_cella(r, c + w - 1, (char)0xBF, attr);
    gf_term_cella(r + h - 1, c, (char)0xC0, attr);
    gf_term_cella(r + h - 1, c + w - 1, (char)0xD9, attr);

    for (i = 1; i < w - 1; i++) {
        gf_term_cella(r, c + i, (char)0xC4, attr);
        gf_term_cella(r + h - 1, c + i, (char)0xC4, attr);
    }
    for (i = 1; i < h - 1; i++) {
        gf_term_cella(r + i, c, (char)0xB3, attr);
        gf_term_cella(r + i, c + w - 1, (char)0xB3, attr);
        gf_term_riempi(r + i, c + 1, w - 2, ' ', attr);
    }
}

void gf_term_cursore(int r, int c, int visibile)
{
    if (r < 0) r = 0;
    if (r >= GF_ROWS) r = GF_ROWS - 1;
    if (c < 0) c = 0;
    if (c >= GF_COLS) c = GF_COLS - 1;

    cur_r   = r;
    cur_c   = c;
    cur_vis = visibile;
}

/* =============================================================================
 * INGRESSO — dialogo diretto con il servizio "kbd"
 * ============================================================================= */
static int      kbd_pid = -1;
static unsigned mia_console = 0;

/* La modalità della tastiera è PER CONSOLE, quindi va detto al driver
 * quale: mentre l'editor tiene la propria in raw, la shell di un'altra
 * console deve continuare a ricevere righe intere con l'eco e il
 * Backspace. Vedi KbdSetMode in drivers/kbd/kbd_proto.h. */
static void kbd_modo(unsigned modo)
{
    KbdSetMode m;

    if (kbd_pid <= 0) return;

    m.modo    = modo;
    m.console = mia_console;
    ipc_send((unsigned)kbd_pid, KBD_MSG_SETMODE, &m, sizeof(m));
}

int gf_term_init(void)
{
    int          pid = ipc_lookup(KBD_SERVICE_NAME);
    ConsoleInfo  ci;

    /* Su quale console stiamo girando. Serve al driver per sapere a chi
     * appartengono le nostre richieste di tasti: se sbagliassimo,
     * l'editor su una console leggerebbe i tasti battuti su un'altra. */
    if (console_info(&ci) == 0) {
        mia_console = ci.mia;

        /* =================================================================
         * L'editor deve essere in PRIMO PIANO, e lo verifica da solo.
         *
         * La modalita' raw si ottiene parlando direttamente al servizio
         * 'kbd' via IPC, e quella strada NON passa da sys_read: la
         * guardia che il kernel mette sullo stdin dei processi in
         * background qui non arriva. Lanciato con '&', l'editor
         * diventerebbe l'ultimo ad aver chiesto un tasto — e il driver
         * serve l'ultimo — quindi ruberebbe la tastiera alla shell, che
         * resterebbe bloccata in attesa di una riga che non arriverebbe
         * mai. Il prompt sparirebbe e la console con lui.
         *
         * ci.fg == 0 significa che nessuno ha dichiarato il primo piano
         * (nessuna shell in mezzo): si procede, perche' in quel caso non
         * c'e' nessuno a cui rubare niente.
         * ================================================================= */
        if (ci.fg != 0 && ci.fg != (unsigned)getpid()) return -2;
    } else {
        mia_console = 0;
    }

    if (pid <= 0) {
        /* Senza il driver ring3 la console è servita dalla tastiera
         * in-kernel (TTY_INPUT_INTERNAL), che conosce solo le righe:
         * un editor a schermo intero non ha modo di funzionare, e
         * partire lo stesso lascerebbe l'utente davanti a una schermata
         * che non risponde. */
        return -1;
    }
    kbd_pid = pid;

    /* Spegne lo specchio seriale dell'output: vedi TTY_IOCTL_SETRAW in
     * drivers/tty/tty.c — su hardware reale è la differenza fra un
     * ridisegno immediato e mezzo secondo di attesa. */
    tty_raw(1);
    kbd_modo(KBD_MODE_RAW);

    forza_ridisegno = 1;
    return 0;
}

void gf_term_fine(void)
{
    kbd_modo(KBD_MODE_COOKED);
    tty_raw(0);

    /* Lascia la console come l'ha trovata: colori di sistema, cursore
     * acceso, schermo pulito. Senza il reset il prompt della shell
     * erediterebbe lo sfondo blu dell'editor. */
    out_s("\x1B[0m\x1B[?25h\x1B[2J\x1B[H");
    out_flush();
}

/* =============================================================================
 * gf_getkey — un tasto, bloccante
 *
 * Una richiesta, una risposta. I messaggi di altro tipo si scartano
 * segnalandoli: nessuno oggi scrive nella mailbox dell'editor, ma
 * restituire come tasto premuto un messaggio arrivato per sbaglio
 * sarebbe un input inventato.
 * ============================================================================= */
unsigned gf_getkey(void)
{
    return gf_getkey_timeout(0);
}

/* =============================================================================
 * gf_getkey_timeout — un tasto, o GF_KEY_SCADUTA se non arriva in tempo
 *
 * timeout_ms == 0 aspetta per sempre, ed è quello che vogliono i
 * dialoghi: lì non c'è niente da aggiornare mentre si aspetta.
 *
 * Il ciclo principale invece usa una scadenza, ed è l'unico modo di
 * avere un orologio che avanza: senza, l'editor resta fermo dentro
 * ipc_recv finché l'utente non preme un tasto, e la barra di stato si
 * aggiorna solo in quell'istante. Vedi ipc_recv_timeout in
 * lib/include/libc.h.
 *
 * LA RICHIESTA PENDENTE NON SI ANNULLA. Se la scadenza passa, il driver
 * ha comunque la nostra READKEY in coda e ci consegnerà il prossimo
 * tasto appena premuto: la richiesta successiva ne creerebbe una
 * seconda, e il primo tasto arriverebbe doppio. Da qui
 * 'richiesta_pendente', che tiene il conto di quante ne abbiamo in giro
 * e non ne manda una nuova finché quella vecchia non è stata onorata.
 * ============================================================================= */
static int richiesta_pendente = 0;

unsigned gf_getkey_timeout(unsigned timeout_ms)
{
    IpcMessage meta;
    unsigned   key = 0;

    if (kbd_pid <= 0) return GF_KEY_ERRORE;

    if (!richiesta_pendente) {
        if (ipc_send((unsigned)kbd_pid, KBD_MSG_READKEY,
                     &mia_console, sizeof(mia_console)) < 0)
            return GF_KEY_ERRORE;
        richiesta_pendente = 1;
    }

    for (;;) {
        int r = ipc_recv_timeout(&meta, &key, sizeof(key), timeout_ms);

        if (r == -110) return GF_KEY_SCADUTA;    /* ETIMEDOUT */
        if (r < 0)     return GF_KEY_ERRORE;

        if (meta.tipo == KBD_MSG_KEY && meta.len >= sizeof(key)) {
            richiesta_pendente = 0;
            return key;
        }
        /* messaggio estraneo: si torna in attesa di quello giusto */
    }
}

/* =============================================================================
 * gf_ora — l'ora del giorno, formattata
 *
 * Scrive "HH:MM:SS" in buf, o "--:--:--" se l'orologio non risponde.
 * Il trattino non è un ripiego elegante: è l'unica cosa onesta da
 * mostrare quando il CMOS ha la batteria scarica e consegna una data
 * impossibile, invece di un orario inventato che sembra vero.
 * ============================================================================= */
void gf_ora(char *buf, int size)
{
    RtcTime t;

    if (time_now(&t) != 0) {
        gf_strlcpy(buf, "--:--:--", size);
        return;
    }

    gf_fmt(buf, size, "%02d:%02d:%02d",
           (int)t.ora, (int)t.minuto, (int)t.secondo);
}

void gf_data(char *buf, int size)
{
    RtcTime t;

    if (time_now(&t) != 0) {
        gf_strlcpy(buf, "--/--/----", size);
        return;
    }

    gf_fmt(buf, size, "%02d/%02d/%d",
           (int)t.giorno, (int)t.mese, (int)t.anno);
}

/* =============================================================================
 * Utilità di formattazione
 *
 * La libc di EX-OS ha printf ma non snprintf, e printf scrive su fd 1 —
 * qui l'uscita non deve mai passare direttamente dal TTY, o scavalcherebbe
 * il frame ombra e lascerebbe a video caratteri che il diff non conosce.
 * Da qui questo piccolo formatter che scrive in un buffer.
 *
 * Copre %s %d %c %% e la larghezza minima con riempimento a spazi o a
 * zeri (%5d, %05d, %-5s). Non è un printf: non ha %x, né i modificatori
 * di lunghezza, e non ne ha bisogno.
 * ============================================================================= */
/* Scrive al più 'size' byte e ritorna QUANTI NE HA SCRITTI DAVVERO —
 * mai quanti ne avrebbe voluti scrivere. Il chiamante usa il valore di
 * ritorno per avanzare nel buffer, e una stima ottimistica lo porterebbe
 * a scrivere il terminatore oltre la fine. */
static int fmt_num(char *dst, int size, int v, int larghezza, char riempi, int sinistra)
{
    char cifre[12];
    int  n = 0, len, i, p = 0;
    int  negativo = 0;
    unsigned uv;

    if (size <= 0) return 0;

    if (v < 0) { negativo = 1; uv = (unsigned)(-v); } else uv = (unsigned)v;

    if (uv == 0) cifre[n++] = '0';
    while (uv > 0 && n < (int)sizeof(cifre)) { cifre[n++] = (char)('0' + uv % 10); uv /= 10; }

    len = n + (negativo ? 1 : 0);

    if (sinistra) {
        if (negativo && p < size) dst[p++] = '-';
        for (i = 0; i < n && p < size; i++) dst[p++] = cifre[n - 1 - i];
        while (p < larghezza && p < size) dst[p++] = ' ';
        return p;
    }

    /* Con riempimento a zeri il segno va PRIMA degli zeri, o si
     * otterrebbe "00-7" invece di "-007". */
    if (riempi == '0' && negativo && p < size) dst[p++] = '-';
    {
        int riempimento = (larghezza > len) ? (larghezza - len) : 0;
        while (riempimento-- > 0 && p < size) dst[p++] = riempi;
    }
    if (riempi != '0' && negativo && p < size) dst[p++] = '-';
    for (i = 0; i < n && p < size; i++) dst[p++] = cifre[n - 1 - i];
    return p;
}

int gf_fmt(char *buf, int size, const char *fmt, ...)
{
    __builtin_va_list ap;
    int p = 0;

    if (size <= 0) return 0;
    __builtin_va_start(ap, fmt);

    while (*fmt && p < size - 1) {
        int  larghezza = 0;
        int  sinistra  = 0;
        char riempi    = ' ';

        if (*fmt != '%') { buf[p++] = *fmt++; continue; }
        fmt++;

        if (*fmt == '%') { buf[p++] = '%'; fmt++; continue; }
        if (*fmt == '-') { sinistra = 1; fmt++; }
        if (*fmt == '0') { riempi = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') larghezza = larghezza * 10 + (*fmt++ - '0');

        if (*fmt == 'd') {
            p += fmt_num(buf + p, size - 1 - p, __builtin_va_arg(ap, int),
                         larghezza, riempi, sinistra);
            fmt++;
        } else if (*fmt == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            int len = 0, i;
            if (!s) s = "(null)";
            while (s[len]) len++;
            if (!sinistra) { for (i = len; i < larghezza && p < size - 1; i++) buf[p++] = ' '; }
            for (i = 0; i < len && p < size - 1; i++) buf[p++] = s[i];
            if (sinistra)  { for (i = len; i < larghezza && p < size - 1; i++) buf[p++] = ' '; }
            fmt++;
        } else if (*fmt == 'c') {
            buf[p++] = (char)__builtin_va_arg(ap, int);
            fmt++;
        } else {
            buf[p++] = '%';     /* specificatore ignoto: reso alla lettera */
        }
    }

    __builtin_va_end(ap);
    buf[p] = '\0';
    return p;
}

void gf_strlcpy(char *dst, const char *src, int size)
{
    int i = 0;

    if (size <= 0) return;
    while (src[i] && i < size - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

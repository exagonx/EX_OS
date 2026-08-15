/* =============================================================================
 * kernel/arch/x86/kprintf.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#include "kernel.h"
#include "vga.h"

/* Livello di log corrente (impostato da kernel.cfg → kernel_main) */
static int g_loglevel = LOG_INFO;

/* Prefissi per i livelli di log */
static const char *log_prefix[] = {
    "",           /* LOG_NONE */
    "[ERROR] ",   /* LOG_ERROR */
    "[WARN]  ",   /* LOG_WARN */
    "[INFO]  ",   /* LOG_INFO */
    "[DEBUG] ",   /* LOG_DEBUG */
};

/* Colori per i livelli di log */
static const uint8_t log_colors[] = {
    VGA_COLOR_WHITE,        /* NONE */
    VGA_COLOR_LIGHT_RED,    /* ERROR */
    VGA_COLOR_YELLOW,       /* WARN */
    VGA_COLOR_LIGHT_CYAN,   /* INFO */
    VGA_COLOR_LIGHT_GREY,   /* DEBUG */
};

/* =============================================================================
 * Registro dei problemi — errori e avvisi sopravvivono al boot silenzioso
 *
 * Con verboseboot=0 il kernel cancella lo schermo al PASSO 13c, perché i
 * messaggi dei PASSI 1-13 sono stati stampati prima che la configurazione
 * fosse leggibile e non si possono sopprimere a posteriori. Quella
 * cancellazione però distruggerebbe anche le prove di un problema
 * avvenuto durante l'inizializzazione: un driver non caricato, un timeout
 * dell'FDC, il ripiego sulla tastiera in-kernel.
 *
 * Ogni messaggio di livello ERROR o WARN viene quindi copiato qui MENTRE
 * viene emesso, indipendentemente dal livello di log corrente — anche se
 * il filtro lo sta nascondendo. kernel_main lo ripropone dopo aver pulito
 * lo schermo, così l'avvio resta silenzioso ma nessun problema sparisce.
 *
 * Dimensione volutamente piccola e fissa: è un riassunto per l'utente,
 * non un log completo — quello è sulla console seriale, che riceve tutto
 * comunque (vga.c la specchia a monte di questo filtro).
 * ============================================================================= */
#define PROB_SLOTS   8      /* messaggi conservati (i più recenti) */
#define PROB_LEN     120    /* caratteri per messaggio, terminatore incluso */

static char     g_prob_text[PROB_SLOTS][PROB_LEN];
static uint8_t  g_prob_level[PROB_SLOTS];
static uint32_t g_prob_next  = 0;   /* prossimo slot da scrivere (ring) */
static uint32_t g_prob_total = 0;   /* quanti problemi in tutto, mai azzerato */

/* Redirezione dell'output: se non NULL, kout() scrive qui invece che a
 * video. Usata solo da klog() per catturare il testo già formattato. */
static char    *g_capture     = NULL;
static uint32_t g_capture_pos = 0;
static uint32_t g_capture_max = 0;

/* =============================================================================
 * kout — unico punto di uscita per tutto l'output di questo file.
 *
 * Tutte le funzioni di formattazione qui sotto passano da qui invece di
 * chiamare vga_putchar direttamente: è ciò che rende possibile catturare
 * un messaggio già formattato senza duplicare il formattatore.
 * ============================================================================= */
static void kout(char c)
{
    if (g_capture != NULL) {
        if (g_capture_pos + 1 < g_capture_max) {
            g_capture[g_capture_pos++] = c;
            g_capture[g_capture_pos]   = '\0';
        }
        return;
    }
    vga_putchar(c);
}

static void kout_str(const char *s)
{
    while (*s) kout(*s++);
}

/* =============================================================================
 * Funzioni helper interne
 * ============================================================================= */

static void print_uint(uint32_t val, uint32_t base, int uppercase,
                        int width, char pad)
{
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_upper : digits_lower;
    char buf[32];
    int  i = 0;

    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = digits[val % base];
            val /= base;
        }
    }

    /* Padding */
    while (i < width) {
        buf[i++] = pad;
    }

    /* Stampa invertito */
    while (i > 0) {
        kout(buf[--i]);
    }
}

static void print_int(int32_t val, int width, char pad)
{
    if (val < 0) {
        kout('-');
        if (width > 0) width--;
        print_uint((uint32_t)(-val), 10, 0, width, pad);
    } else {
        print_uint((uint32_t)val, 10, 0, width, pad);
    }
}

static void print_str(const char *s, int width, int left_align)
{
    int len = 0;
    const char *p;

    /* Il controllo NULL deve precedere qualunque dereferenziazione: prima
     * `p` veniva inizializzato con `s` *prima* della sostituzione con
     * "(null)", quindi il conteggio della lunghezza dereferenziava il
     * puntatore nullo — kprintf("%s", NULL) causava un #PF nel kernel. */
    if (!s) s = "(null)";
    p = s;

    while (*p++) len++;

    if (!left_align) {
        /* Padding a sinistra (allineamento a destra) */
        while (len < width) {
            kout(' ');
            width--;
        }
    }

    while (*s) kout(*s++);

    if (left_align) {
        /* Padding a destra (allineamento a sinistra) */
        while (len < width) {
            kout(' ');
            width--;
        }
    }
}

/* =============================================================================
 * kvprintf — Implementazione interna con va_list
 * Usa __builtin_va_list di GCC (disponibile anche in freestanding)
 * ============================================================================= */
static void kvprintf(const char *fmt, __builtin_va_list args)
{
    char c;
    int  width;
    char pad;

    while ((c = *fmt++) != '\0') {
        if (c != '%') {
            kout(c);
            continue;
        }

        /* Leggi specificatore di formato */
        c = *fmt++;

        /* Padding */
        pad   = ' ';
        width = 0;

        int left_align = 0;
        if (c == '-') {
            left_align = 1;
            c = *fmt++;
        }

        if (c == '0') {
            pad = '0';
            c   = *fmt++;
        }

        /* Larghezza campo */
        while (c >= '0' && c <= '9') {
            width = width * 10 + (c - '0');
            c     = *fmt++;
        }

        switch (c) {
            case 'd':
            case 'i':
                print_int(__builtin_va_arg(args, int32_t), width, pad);
                break;

            case 'u':
                print_uint(__builtin_va_arg(args, uint32_t), 10, 0, width, pad);
                break;

            case 'x':
                print_uint(__builtin_va_arg(args, uint32_t), 16, 0, width, pad);
                break;

            case 'X':
                print_uint(__builtin_va_arg(args, uint32_t), 16, 1, width, pad);
                break;

            case 'o':
                print_uint(__builtin_va_arg(args, uint32_t), 8, 0, width, pad);
                break;

            case 'b':
                print_uint(__builtin_va_arg(args, uint32_t), 2, 0, width, pad);
                break;

            case 'p': {
                uint32_t ptr = __builtin_va_arg(args, uint32_t);
                kout_str("0x");
                print_uint(ptr, 16, 0, 8, '0');
                break;
            }

            case 's':
                print_str(__builtin_va_arg(args, const char *), width, left_align);
                break;

            case 'c':
                kout((char)__builtin_va_arg(args, int));
                break;

            case '%':
                kout('%');
                break;

            case '\0':
                return;

            default:
                kout('%');
                kout(c);
                break;
        }
    }
}

/* =============================================================================
 * kprintf — Output formattato kernel (come printf, senza buffering)
 * ============================================================================= */
void kprintf(const char *fmt, ...)
{
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    kvprintf(fmt, args);
    __builtin_va_end(args);
}

/* =============================================================================
 * klog — Output con livello di log e prefisso colorato
 *
 * DUE GARANZIE, indipendenti dal livello di log configurato:
 *
 *  1. Un messaggio di livello ERROR viene stampato SEMPRE. Il livello
 *     regola quanto il sistema è loquace, non se può nascondere i propri
 *     guasti: un errore invisibile non è un sistema silenzioso, è un
 *     sistema che mente. Vale anche per un `loglevel = 0` esplicito.
 *
 *  2. Un messaggio ERROR o WARN viene comunque REGISTRATO nel ring dei
 *     problemi, anche quando il filtro lo sta nascondendo a video. Serve
 *     al boot silenzioso: kernel_main pulisce lo schermo al PASSO 13c e
 *     poi ripropone questo registro, così cancellare il log di avvio non
 *     cancella anche le prove di ciò che è andato storto.
 *
 * Il messaggio viene formattato UNA sola volta: quando è un problema si
 * formatta dentro lo slot del ring e poi si stampa quel testo già pronto,
 * invece di eseguire due volte il formattatore sulla stessa va_list.
 * ============================================================================= */
void klog(int level, const char *fmt, ...)
{
    __builtin_va_list args;
    int   is_problem;
    int   visible;
    char *slot = NULL;

    if (level < 0 || level > LOG_DEBUG) level = LOG_INFO;

    is_problem = (level <= LOG_WARN && level > LOG_NONE);
    visible    = (level <= g_loglevel) || (level == LOG_ERROR);

    if (!is_problem && !visible) return;

    if (is_problem) {
        slot = g_prob_text[g_prob_next];
        g_prob_level[g_prob_next] = (uint8_t)level;
        g_prob_next  = (g_prob_next + 1) % PROB_SLOTS;
        g_prob_total++;

        /* Formatta nel ring: kout() vede g_capture != NULL e devia lì. */
        g_capture     = slot;
        g_capture_pos = 0;
        g_capture_max = PROB_LEN;
        slot[0]       = '\0';

        __builtin_va_start(args, fmt);
        kvprintf(fmt, args);
        __builtin_va_end(args);

        g_capture = NULL;   /* da qui kout() torna a scrivere a video */
    }

    if (!visible) return;

    vga_setcolor(log_colors[level], VGA_COLOR_BLACK);
    kout_str(log_prefix[level]);
    vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    if (is_problem) {
        kout_str(slot);          /* già formattato sopra */
    } else {
        __builtin_va_start(args, fmt);
        kvprintf(fmt, args);
        __builtin_va_end(args);
    }

    kout('\n');
}

/* =============================================================================
 * klog_problem_count — quanti ERROR/WARN sono stati emessi finora
 *
 * Conta TUTTI i problemi visti, anche quelli che il ring non conserva più
 * (ne tiene solo gli ultimi PROB_SLOTS) e anche quelli nascosti dal
 * filtro di livello. Serve a kernel_main per decidere se ha qualcosa da
 * dire dopo aver pulito lo schermo.
 * ============================================================================= */
uint32_t klog_problem_count(void)
{
    return g_prob_total;
}

/* =============================================================================
 * klog_replay_problems — ristampa i problemi registrati
 *
 * Usata dal boot silenzioso dopo vga_clear(). Stampa nell'ordine in cui
 * si sono verificati, con il prefisso e il colore originali. Se ne sono
 * accaduti più di quanti il ring ne conservi, lo dichiara invece di far
 * credere che fossero tutti lì: una lista troncata in silenzio è
 * esattamente il tipo di bugia che questa funzione esiste per evitare.
 * ============================================================================= */
void klog_replay_problems(void)
{
    uint32_t stored = (g_prob_total < PROB_SLOTS) ? g_prob_total : PROB_SLOTS;
    uint32_t i;

    if (g_prob_total == 0) return;

    if (g_prob_total > stored) {
        vga_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        kout_str("[WARN]  ");
        vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        kprintf("(%u problemi precedenti non conservati, vedi console seriale)\n",
                g_prob_total - stored);
    }

    /* Il ring è circolare: il più vecchio conservato è quello subito dopo
     * l'ultimo scritto, quando il buffer ha già girato almeno una volta. */
    for (i = 0; i < stored; i++) {
        uint32_t idx = (g_prob_total <= PROB_SLOTS)
                       ? i
                       : (g_prob_next + i) % PROB_SLOTS;
        int lvl = g_prob_level[idx];

        vga_setcolor(log_colors[lvl], VGA_COLOR_BLACK);
        kout_str(log_prefix[lvl]);
        vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        kout_str(g_prob_text[idx]);
        kout('\n');
    }
}

/* =============================================================================
 * kpanic — Kernel panic: stampa messaggio e ferma il sistema
 * Non ritorna mai.
 * ============================================================================= */
void kpanic(const char *fmt, ...)
{
    __builtin_va_list args;

    interrupts_disable();

    vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
    vga_clear();

    kprintf("\n");
    kprintf("  *** KERNEL PANIC ***\n\n");
    kprintf("  Messaggio: ");

    __builtin_va_start(args, fmt);
    kvprintf(fmt, args);
    __builtin_va_end(args);

    kprintf("\n\n");
    kprintf("  Il sistema e' stato fermato.\n");
    kprintf("  Riavvia il computer.\n");

    /* Loop infinito con HLT */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/* =============================================================================
 * klog_set_level — Imposta il livello di log (chiamato da kernel_main
 * dopo aver letto kernel.cfg)
 * ============================================================================= */
void klog_set_level(int level)
{
    if (level >= LOG_NONE && level <= LOG_DEBUG) {
        g_loglevel = level;
    }
}

/* =============================================================================
 * bin/fbprova/fbprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Quanto costa scrivere nel framebuffer
 *
 *     fbprova              misura, e alla fine rimette lo schermo nero
 *     fbprova -giri N      almeno N passate per prova (predefinito: quante
 *                          ne servono a superare i 400 ms)
 *     fbprova -tieni       non ripulisce lo schermo alla fine
 *
 * -----------------------------------------------------------------------------
 * ! A CHE SERVE UN NUMERO, QUANDO «SI VEDE CHE SCATTA»
 *
 * Perche' «scatta» non dice DOVE. Un compositore che ridipinge 800x600x32 —
 * 1,83 MB a fotogramma — puo' essere lento per tre ragioni diverse, e si
 * curano in tre posti diversi:
 *
 *   - la CPU e' lenta a comporre       -> si guarda il codice del compositore
 *   - il BUS verso la scheda e' lento  -> si guarda il tipo di memoria (WC)
 *   - si ridipinge troppo spesso       -> si guardano le regioni sporche
 *
 * Questa misura separa le prime due, che a occhio sono indistinguibili. La
 * copia RAM->RAM dice quanto va la macchina; la stessa copia RAM->framebuffer
 * dice quanto ne resta passando dal bus della scheda. Il rapporto fra le due
 * e' il numero che conta.
 *
 * ! IL SOSPETTO CHE QUESTA PROVA DEVE CONFERMARE O SMENTIRE: il framebuffer e'
 * mappato con PG_PRESENT | PG_WRITABLE e nessuno ha mai toccato gli MTRR
 * (paging_mappa_framebuffer, kernel/mm/paging.c). Se il BIOS lascia
 * quell'intervallo UC — non combinabile in scrittura — ogni store di quattro
 * byte diventa una transazione sul bus per conto suo, e nemmeno MMX aiuta: i
 * buffer di combinazione della CPU su memoria UC non si usano. Il segno e' una
 * scrittura nel framebuffer dieci o venti volte piu' lenta della stessa
 * scrittura in RAM, e MMX che non cambia niente.
 *
 * Se invece i numeri sono vicini, il bus non c'entra e la lentezza sta nel
 * compositore o in quanto spesso lo si chiama: si guarda la'.
 *
 * -----------------------------------------------------------------------------
 * ! SI MISURA LA SUPERFICIE VERA, non un blocco tondo: passo * altezza, cioe'
 * esattamente quello che il compositore riscrive a ogni fotogramma. Un numero
 * preso su 1 MB tondo non si potrebbe moltiplicare per i fotogrammi al secondo
 * senza rifare il conto.
 *
 * ! E SI RIPETE FINCHE' LA DURATA NON E' ABBASTANZA LUNGA. uptime_ms() conta i
 * millisecondi, e una passata sola puo' starci dentro in pochi: misurare una
 * cosa che dura come il tick dell'orologio vuol dire misurare l'orologio.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `fbprova -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("fbprova", "0.001");

#define MS_MINIMI   400u        /* sotto questa durata la misura non vale */
#define GIRI_MAX    4000u       /* rete di sicurezza: non girare all'infinito */

/* -----------------------------------------------------------------------------
 * CPUID, per sapere quali strade si possono provare su questa macchina
 *
 * ! NON SI DA' PER SCONTATO NIENTE: MMX c'e' dal Pentium MMX e SSE2 dal
 * Pentium 4 / Pentium M, ma EX-OS gira anche su macchine piu' vecchie di
 * tutt'e due, e una istruzione che non c'e' e' un'eccezione, non un numero
 * brutto.
 * --------------------------------------------------------------------------- */
static void cpuid1(unsigned int *edx, unsigned int *ecx)
{
    unsigned int a, b, c, d;

    __asm__ __volatile__("cpuid"
                         : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                         : "a"(1));
    *edx = d;
    *ecx = c;
}

static int ha_mmx(void)  { unsigned d, c; cpuid1(&d, &c); return (d & (1u << 23)) != 0; }
static int ha_sse2(void) { unsigned d, c; cpuid1(&d, &c); return (d & (1u << 26)) != 0; }

/* -----------------------------------------------------------------------------
 * Le quattro strade che si misurano
 * --------------------------------------------------------------------------- */

/* Riempimento a 32 bit: quello che fa un programma qualunque. */
static void riempi32(volatile unsigned int *dst, unsigned int n_dword,
                     unsigned int colore)
{
    unsigned int i;

    for (i = 0; i < n_dword; i++) dst[i] = colore;
}

/* Riempimento MMX: otto byte per store, la strada del compositore. */
static void riempiMMX(void *dst, unsigned int n_byte, unsigned int colore)
{
    unsigned long long q = ((unsigned long long)colore << 32) | colore;
    unsigned int blocchi = n_byte / 64;

    __asm__ __volatile__(
        "movq (%2), %%mm0\n\t"
        "1:\n\t"
        "movq %%mm0,  0(%0)\n\t"
        "movq %%mm0,  8(%0)\n\t"
        "movq %%mm0, 16(%0)\n\t"
        "movq %%mm0, 24(%0)\n\t"
        "movq %%mm0, 32(%0)\n\t"
        "movq %%mm0, 40(%0)\n\t"
        "movq %%mm0, 48(%0)\n\t"
        "movq %%mm0, 56(%0)\n\t"
        "addl $64, %0\n\t"
        "decl %1\n\t"
        "jnz 1b\n\t"
        "emms"
        : "+r"(dst), "+r"(blocchi)
        : "r"(&q)
        : "memory");
}

/* Riempimento SSE2 non temporale: sedici byte per store, e SALTA LA CACHE.
 *
 * ! E' LA STRADA CHE SU MEMORIA WC VA PIU' FORTE E SU MEMORIA UC NON CAMBIA
 * NIENTE, e per questo sta qui: e' meta' della diagnosi. movntdq usa i buffer
 * di combinazione della CPU, che su un intervallo dichiarato UC non entrano
 * in gioco. */
static void riempiNT(void *dst, unsigned int n_byte, unsigned int colore)
{
    unsigned int quattro[4];
    unsigned int blocchi = n_byte / 64;
    void *p = dst;

    quattro[0] = quattro[1] = quattro[2] = quattro[3] = colore;

    __asm__ __volatile__(
        "movups (%2), %%xmm0\n\t"
        "1:\n\t"
        "movntdq %%xmm0,  0(%0)\n\t"
        "movntdq %%xmm0, 16(%0)\n\t"
        "movntdq %%xmm0, 32(%0)\n\t"
        "movntdq %%xmm0, 48(%0)\n\t"
        "addl $64, %0\n\t"
        "decl %1\n\t"
        "jnz 1b\n\t"
        "sfence"
        : "+r"(p), "+r"(blocchi)
        : "r"(quattro)
        : "memory");
}

/* =============================================================================
 * LE DUE STRADE DEL COMPOSITORE, rifatte qui uguali
 *
 * ! NON SONO UN DOPPIONE DELLE ALTRE MISURE: sono LA misura. Le prime dicono
 * quanto puo' andare il bus; queste dicono quanto ci mette davvero il server a
 * finestre, perche' sono il suo codice, copiato riga per riga da
 * drivers/wserver/wserver.c.
 *
 * ! LA COPIA DI RIGA NON E' SROTOLATA, e non per svista: nel compositore e'
 * cosi'. Un movq di carico e uno di scarico per ogni otto byte, con la
 * dipendenza in mezzo, e' un'altra cosa dagli otto movq in fila della misura
 * qui sopra — e la differenza fra i due numeri dice quanto varrebbe srotolarla.
 *
 * ! E IL PIXEL PER PIXEL E' LA STRADA DI px(), che nel compositore disegna
 * tutto quello che non e' l'area di un client: sfondo, cornici, prese,
 * contorni, puntatore. E' una CHIAMATA per pixel, e la chiamata non si puo'
 * togliere con l'inline perche' e' li' che vive il ritaglio.
 * ========================================================================== */
static void copia_come_compositore(unsigned int *d, const unsigned int *s,
                                   unsigned int n_pixel)
{
    unsigned int coppie = n_pixel >> 1;

    if (!coppie) return;
    __asm__ __volatile__(
        "1:\n\t"
        "movq      (%1), %%mm0\n\t"
        "movq      %%mm0, (%0)\n\t"
        "addl      $8, %0\n\t"
        "addl      $8, %1\n\t"
        "decl      %2\n\t"
        "jnz       1b\n\t"
        "emms"
        : "+r"(d), "+r"(s), "+r"(coppie)
        :
        : "memory", "cc");
}

/* ! __attribute__((noinline)) PERCHE' LA CHIAMATA FA PARTE DI CIO' CHE SI
 * MISURA. Lasciando che il compilatore la sciolga si misurerebbe un
 * compositore che non esiste. */
static unsigned int *g_px_base;
static unsigned int  g_px_passo;

__attribute__((noinline))
static void px(unsigned int x, unsigned int y, unsigned int colore)
{
    g_px_base[y * g_px_passo + x] = colore;
}

static void pixel_per_pixel(unsigned int w, unsigned int h)
{
    unsigned int x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            px(x, y, 0x00102030u);
}

/* Lettura: quanto costa RILEGGERE dal framebuffer.
 *
 * ! SI MISURA PERCHE' E' LA TRAPPOLA PIU' CARA E LA MENO VISIBILE. Un
 * compositore che legge lo sfondo per fonderlo, o che fa una copia
 * schermo->schermo, paga questo numero — e sulla memoria video e' sempre il
 * peggiore dei quattro, anche quando la scrittura e' a posto. */
static unsigned int leggi(const volatile unsigned int *src, unsigned int n_dword)
{
    unsigned int i, s = 0;

    for (i = 0; i < n_dword; i += 4) s += src[i];
    return s;
}

/* -----------------------------------------------------------------------------
 * Il cronometro
 *
 * Rende i MB/s, e scrive in *giri quante passate ha dovuto fare.
 * --------------------------------------------------------------------------- */
typedef void (*Passata)(void *bersaglio, void *aiuto, unsigned int byte);

static unsigned long misura(Passata p, void *bersaglio, void *aiuto,
                            unsigned int byte, unsigned int giri_min,
                            unsigned int *giri_fatti, unsigned int *ms_fatti)
{
    unsigned int inizio, passati = 0, giri = 0, kib;

    inizio = uptime_ms();
    while ((passati < MS_MINIMI || giri < giri_min) && giri < GIRI_MAX) {
        p(bersaglio, aiuto, byte);
        giri++;
        passati = uptime_ms() - inizio;
    }

    *giri_fatti = giri;
    *ms_fatti   = passati ? passati : 1;

    /* ! IL CONTO STA IN 32 BIT APPOSTA, e non e' pigrizia: qui non c'e'
     * libgcc, quindi una divisione a 64 bit e' un __udivdi3 che nessuno
     * definisce e il collegamento si ferma. Si lavora in KiB: (byte/1024) *
     * giri sta largamente sotto i quattro miliardi anche a 4000 passate, e un
     * KiB al millisecondo e' un MiB al secondo a meno del fattore 1000/1024,
     * che si applica prima di dividere per non perdere le cifre. */
    kib = (byte >> 10) * giri;
    return (unsigned long)((kib * 125u / 128u) / *ms_fatti);
}

/* Gli involucri, per avere tutti la stessa forma. */
static void p_riempi32(void *b, void *a, unsigned int n) { (void)a; riempi32((volatile unsigned int *)b, n / 4, 0x00204060u); }
static void p_riempiMMX(void *b, void *a, unsigned int n) { (void)a; riempiMMX(b, n, 0x00204060u); }
static void p_riempiNT(void *b, void *a, unsigned int n)  { (void)a; riempiNT(b, n, 0x00204060u); }
static void p_copia(void *b, void *a, unsigned int n)      { memcpy(b, a, n); }
static void p_leggi(void *b, void *a, unsigned int n)      { (void)a; leggi((const volatile unsigned int *)b, n / 4); }
static void p_copia_comp(void *b, void *a, unsigned int n) { copia_come_compositore((unsigned int *)b, (const unsigned int *)a, n / 4); }
static unsigned int g_pw, g_ph;
static void p_pixel(void *b, void *a, unsigned int n)      { (void)b; (void)a; (void)n; pixel_per_pixel(g_pw, g_ph); }

/* ! IL NUMERO CHE SI CAPISCE E' IL SECONDO. «190 MB/s» non dice se
 * l'interfaccia scatta; «10 ms a fotogramma» si confronta con l'occhio: sotto
 * i 16 ms sono sessanta fotogrammi al secondo, sopra i 50 si vede muovere a
 * scatti. Si stampano tutt'e due perche' il primo serve a capire il perche' e
 * il secondo a decidere. */
static void riga(const char *nome, unsigned long mbs, unsigned int giri,
                 unsigned int ms)
{
    unsigned int per_frame = giri ? (ms * 10u + giri / 2u) / giri : 0u;

    printf("  %-26s %6lu MB/s   %u.%u ms a fotogramma\n",
           nome, mbs, per_frame / 10u, per_frame % 10u);
}

int main(int argc, char **argv)
{
    VideoInfo     v;
    unsigned char *fb, *ram;
    unsigned int   byte, giri, ms, giri_min = 0;
    unsigned long  mbs_ram, mbs_fb;
    int            tieni = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-tieni") == 0) tieni = 1;
        else if (strcmp(argv[i], "-giri") == 0 && i + 1 < argc)
            giri_min = (unsigned int)atoi(argv[++i]);
        else {
            printf("uso: fbprova [-giri N] [-tieni]\n");
            return 1;
        }
    }

    if (video_info(&v) != 0) {
        printf("fbprova: video_info non risponde.\n");
        return 1;
    }
    if (v.larghezza == 0 || v.fisico == 0) {
        printf("fbprova: lo schermo e' in modo TESTO: non c'e' niente da\n"
               "         misurare. Scegli una risoluzione e riavvia:\n"
               "             /dev/svga.drv 800x600\n");
        return 1;
    }

    fb = (unsigned char *)fb_map();
    if (fb == 0) {
        printf("fbprova: non riesco a mappare il framebuffer (%s).\n",
               strerror(errno));
        return 1;
    }

    byte = v.passo * v.altezza;
    byte &= ~63u;                       /* i cicli vanno a blocchi di 64 */

    ram = (unsigned char *)malloc(byte);
    if (ram == 0) {
        printf("fbprova: non c'e' memoria per %u byte di confronto.\n", byte);
        return 1;
    }
    memset(ram, 0x20, byte);

    printf("\nfbprova: %ux%u a %u bit, passo %u — %u byte per fotogramma\n",
           v.larghezza, v.altezza, v.bit, v.passo, byte);
    printf("         framebuffer fisico 0x%08x, mappato a %p\n\n",
           v.fisico, (void *)fb);

    /* ! PRIMA LA RAM, E NON E' UN DI PIU': e' il metro. Senza, un numero
     * basso puo' voler dire «il bus della scheda e' lento» oppure «questa
     * macchina e' lenta», e sono due lavori diversi. */
    printf("  -- in RAM, per sapere quanto va questa macchina --\n");
    mbs_ram = misura(p_copia, ram, ram, byte, giri_min, &giri, &ms);
    riga("copia RAM -> RAM", mbs_ram, giri, ms);

    printf("\n  -- nel framebuffer --\n");
    mbs_fb = misura(p_riempi32, fb, 0, byte, giri_min, &giri, &ms);
    riga("riempi 32 bit", mbs_fb, giri, ms);

    if (ha_mmx()) {
        unsigned long m = misura(p_riempiMMX, fb, 0, byte, giri_min, &giri, &ms);
        riga("riempi MMX (8 byte)", m, giri, ms);
    } else {
        printf("  %-26s %s\n", "riempi MMX (8 byte)", "questa CPU non ha MMX");
    }

    if (ha_sse2()) {
        unsigned long m = misura(p_riempiNT, fb, 0, byte, giri_min, &giri, &ms);
        riga("riempi SSE2 non temp.", m, giri, ms);
    } else {
        printf("  %-26s %s\n", "riempi SSE2 non temp.", "questa CPU non ha SSE2");
    }

    {
        unsigned long m = misura(p_copia, fb, ram, byte, giri_min, &giri, &ms);
        riga("copia RAM -> schermo", m, giri, ms);
    }
    {
        unsigned long m = misura(p_leggi, fb, 0, byte, giri_min, &giri, &ms);
        riga("LETTURA dallo schermo", m, giri, ms);
    }

    printf("\n  -- come disegna davvero il server a finestre --\n");
    {
        unsigned long m = misura(p_copia_comp, fb, ram, byte, giri_min, &giri, &ms);
        riga("riga MMX (come wserver)", m, giri, ms);
    }
    {
        unsigned long m;

        g_px_base = (unsigned int *)fb;
        g_px_passo = v.passo / 4;
        g_pw = v.larghezza;
        g_ph = v.altezza;
        m = misura(p_pixel, fb, 0, byte, giri_min, &giri, &ms);
        riga("pixel per pixel (px)", m, giri, ms);
    }

    /* Il verdetto, detto in una riga, perche' il rapporto e' il punto. */
    printf("\n");
    if (mbs_fb > 0 && mbs_ram / (mbs_fb ? mbs_fb : 1) >= 8) {
        printf("  ! LO SCHERMO VA %lu VOLTE PIU' PIANO DELLA RAM. Un divario\n",
               mbs_ram / mbs_fb);
        printf("    cosi' e' il segno della memoria non combinabile in\n");
        printf("    scrittura (UC): si cura negli MTRR, non nel compositore.\n");
    } else if (mbs_fb > 0) {
        printf("  Lo schermo va %lu volte piu' piano della RAM: e' il costo\n",
               mbs_ram / (mbs_fb ? mbs_fb : 1));
        printf("  normale del bus di una scheda video. Se scatta, la ragione\n");
        printf("  sta in quanto spesso si ridipinge, non in quanto costa.\n");
    }

    if (!tieni) {
        memset(ram, 0, byte);
        memcpy(fb, ram, byte);
    }
    free(ram);
    return 0;
}

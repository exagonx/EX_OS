/* =============================================================================
 * bin/memprova/memprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Quanto vale la libc nuova, e se e' ancora giusta
 *
 *     memprova              controlla e misura
 *     memprova -solocheck   solo il controllo di correttezza
 *     memprova -solomisura  solo i numeri
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' ESISTE. Il 16 settembre 2026 bin/fbprova ha misurato che la memcpy
 * della libc copiava UN BYTE PER VOLTA: 47 MB/s verso il framebuffer contro i
 * 375 della stessa copia a otto byte. La riscrittura che ne e' seguita tocca
 * cinque funzioni che TUTTO il sistema chiama, e due cose andavano dette con un
 * numero e non con un'opinione:
 *
 *   1. che sia ancora GIUSTA. Una copia a parole sbaglia in silenzio: un byte
 *      di coda perso, una sovrapposizione gestita al contrario, e il difetto
 *      salta fuori tre settimane dopo dentro un programma che non c'entra.
 *   2. che sia davvero PIU' VELOCE, e di quanto, e DA QUALE MISURA IN POI.
 *      Le due soglie di libc.c — 32 byte per le parole, 256 per MMX — non sono
 *      numeri tondi scelti a occhio: sono il punto in cui queste curve si
 *      incrociano, e chi le cambia rifa' questa misura.
 *
 * ! LA VECCHIA VERSIONE E' QUI DENTRO, e non e' un doppione: e' il METRO.
 * «180 MB/s» non dice niente da solo; «180 contro 47» dice tutto. Le copie di
 * vecchia_* sono quelle che stavano in lib/libc.c fino al 16 settembre 2026,
 * riportate riga per riga.
 *
 * ! SOTTO QEMU I BLOCCHI PICCOLI MENTONO, E VA SAPUTO PRIMA DI LEGGERE LA
 * TABELLA. Le funzioni nuove stanno in libc.so e si raggiungono con un `jmp`
 * INDIRETTO attraverso la tabella dei ponti; le vecchie sono qui dentro e si
 * chiamano dritte. Su una CPU vera la differenza e' qualche ciclo. Sotto la
 * traduzione dinamica di QEMU un salto indiretto rompe la catena dei blocchi
 * tradotti e costa cento volte tanto, quindi a 16 byte per chiamata la
 * tabella dice che la libc nuova e' PIU' LENTA — e non e' vero: e' QEMU.
 * Da 1024 byte in su il costo per chiamata e' diluito e i numeri tornano a
 * dire qualcosa. Le righe corte si leggono SOLO su ferro vero.
 *
 * ! E IL FRAMEBUFFER NON SI MISURA QUI: lo misura gia' bin/fbprova, con la
 * riga «copia RAM -> schermo». E' la stessa memcpy, quindi il prima e il dopo
 * si leggono li' senza scrivere niente di nuovo.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `memprova -version` la stampa. */
EX_VERSIONE("memprova", "0.001");

#define MS_MINIMI    300u       /* sotto questa durata la misura non vale */
#define GIRI_MAX  200000u       /* rete di sicurezza                      */
#define BLOCCO     65536u       /* byte spostati da un «giro», a ogni misura */

#define TAM_MAX   262144u       /* il buffer piu' grande che si prova */
#define CODA         128u       /* aria in fondo, per i disallineamenti */


/* =============================================================================
 * LE VERSIONI VECCHIE — il metro, non un doppione
 *
 * ! noinline PERCHE' LA CHIAMATA FA PARTE DI CIO' CHE SI MISURA, e perche'
 * senza, -O2 riconosce il ciclo a byte e lo puo' rifare a modo suo: si
 * misurerebbe il compilatore invece della libreria che c'era.
 * ============================================================================= */

__attribute__((noinline))
static void *vecchia_memcpy(void *dst, const void *src, unsigned int n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    while (n--) *d++ = *s++;
    return dst;
}

__attribute__((noinline))
static void *vecchia_memset(void *dst, int c, unsigned int n)
{
    unsigned char *d = (unsigned char *)dst;

    while (n--) *d++ = (unsigned char)c;
    return dst;
}

__attribute__((noinline))
static void *vecchia_memmove(void *dst, const void *src, unsigned int n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

__attribute__((noinline))
static int vecchia_memcmp(const void *a, const void *b, unsigned int n)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;

    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

__attribute__((noinline))
static unsigned int vecchia_strlen(const char *s)
{
    unsigned int n = 0;

    while (*s++) n++;
    return n;
}


/* =============================================================================
 * IL CONTROLLO DI CORRETTEZZA
 *
 * ! SI CONFRONTA CON LE VECCHIE, NON CON UN RISULTATO ATTESO SCRITTO A MANO.
 * Le vecchie erano lente ma giuste, e sono l'unica definizione di «giusto» di
 * cui questo sistema disponga. Un valore atteso scritto a mano avrebbe lo
 * stesso autore dell'implementazione nuova, e quindi lo stesso sbaglio.
 *
 * ! E SI PROVANO TUTTI I DISALLINEAMENTI, perche' e' li' che la copia a parole
 * sbaglia: il prologo che allinea la destinazione, la coda che resta, e il caso
 * in cui il blocco e' cosi' corto che il prologo se lo mangia tutto. Una prova
 * su blocchi tondi e allineati passa sempre, anche quando il codice e' rotto.
 * ============================================================================= */

static int g_errori = 0;

static void male(const char *che, unsigned int n, int da, int ds)
{
    printf("  MALE: %s  n=%u  dst+%d  src+%d\n", che, n, da, ds);
    g_errori++;
}

/* Un confronto che NON usa memcmp: memcmp e' fra gli imputati. */
static int diverso(const void *a, const void *b, unsigned int n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;

    while (n--) { if (*x != *y) return 1; x++; y++; }
    return 0;
}

#define CTAM  1024u

static void controlla(unsigned char *a, unsigned char *b, unsigned char *seme)
{
    unsigned int n, i;
    int da, ds;

    printf("  controllo su %u misure x 8 x 8 disallineamenti...\n", 400u);

    for (n = 0; n <= 400u; n++) {
        for (da = 0; da < 8; da++) {
            for (ds = 0; ds < 8; ds++) {
                /* memcpy */
                vecchia_memset(a, 0xAA, CTAM);
                vecchia_memset(b, 0xAA, CTAM);
                vecchia_memcpy(a + da, seme + ds, n);
                memcpy(b + da, seme + ds, n);
                if (diverso(a, b, CTAM)) male("memcpy", n, da, ds);

                /* memset */
                vecchia_memset(a, 0xAA, CTAM);
                vecchia_memset(b, 0xAA, CTAM);
                vecchia_memset(a + da, 0x5C, n);
                memset(b + da, 0x5C, n);
                if (diverso(a, b, CTAM)) male("memset", n, da, ds);

                /* memcmp: uguali, e poi diverso a meta' */
                if ((memcmp(a + da, a + da, n) != 0)) male("memcmp uguali", n, da, ds);
                if (n >= 2u) {
                    unsigned int k = n / 2u;
                    int r1, r2;

                    vecchia_memcpy(b, a, CTAM);
                    b[da + k] = (unsigned char)(a[da + k] ^ 0x80u);
                    r1 = vecchia_memcmp(a + da, b + da, n);
                    r2 = memcmp(a + da, b + da, n);
                    if ((r1 < 0) != (r2 < 0) || (r1 > 0) != (r2 > 0))
                        male("memcmp segno", n, da, ds);
                }

                if (g_errori > 10) { printf("  troppi errori, smetto.\n"); return; }
            }
        }
    }

    /* memmove, con la sovrapposizione nei due versi e a cavallo di zero */
    printf("  controllo memmove sulle sovrapposizioni...\n");
    for (n = 0; n <= 400u; n++) {
        for (da = 0; da < 8; da++) {
            int sp;

            for (sp = -66; sp <= 66; sp += 6) {
                vecchia_memcpy(a, seme, CTAM);
                vecchia_memcpy(b, seme, CTAM);
                vecchia_memmove(a + 500 + da, a + 500 + da + sp, n);
                memmove(b + 500 + da, b + 500 + da + sp, n);
                if (diverso(a, b, CTAM)) male("memmove", n, da, sp);
            }
            if (g_errori > 10) { printf("  troppi errori, smetto.\n"); return; }
        }
    }

    /* strlen, con lo zero in ogni posizione e a ogni disallineamento */
    printf("  controllo strlen...\n");
    for (da = 0; da < 8; da++) {
        for (n = 0; n <= 400u; n++) {
            vecchia_memset(a, 'x', CTAM);
            a[da + n] = '\0';
            if (strlen((char *)a + da) != (unsigned int)vecchia_strlen((char *)a + da))
                male("strlen", n, da, 0);
        }
    }

    for (i = 0; i < 1; i++) { }     /* zitto, compilatore */

    if (g_errori == 0) printf("  ! tutto giusto.\n");
}


/* =============================================================================
 * IL CRONOMETRO
 *
 * ! UN «GIRO» SPOSTA SEMPRE CIRCA 64 KB, qualunque sia la misura del blocco, e
 * non e' un dettaglio di comodo: a 64 byte per chiamata servirebbero milioni di
 * giri per superare i 300 ms, e il conto dei byte totali non starebbe piu' in
 * 32 bit. Ripetendo la chiamata dentro il giro, il numero di giri resta piccolo
 * e la divisione resta a 32 bit — che qui e' obbligatorio, perche' senza libgcc
 * una divisione a 64 bit e' un __udivdi3 che nessuno definisce.
 *
 * ! E LA RIPETIZIONE INTERNA FA PARTE DELLA MISURA GIUSTA, non la falsa: un
 * blocco da 64 byte nel caso vero viene copiato tante volte di fila, non una
 * volta ogni tanto. E' proprio la cache calda di quel caso che si vuole
 * misurare.
 * ============================================================================= */

/* ! IL POZZO — senza, meta' di questa tabella non misura NIENTE.
 *
 * memcmp e strlen non scrivono niente e rendono un valore: GCC le riconosce
 * PURE, e una chiamata pura il cui risultato nessuno guarda si puo' cancellare.
 * Con -O2 lo fa, e __attribute__((noinline)) NON lo impedisce — noinline dice
 * «non aprirla qui dentro», non «chiamala per forza». La prima stesura di
 * questo file rendeva 23489 MB/s per la memcmp vecchia: non era una memcmp
 * velocissima, era una memcmp che non veniva mai chiamata.
 *
 * ! E IL POZZO E' volatile PERCHE' SIA UN POZZO: una variabile normale, letta
 * da nessuno, si fa cancellare insieme a cio' che la riempie. */
static volatile int g_pozzo;

/* ! E NON BASTA GUARDARE IL RISULTATO: VA RESO OPACO ANCHE L'ARGOMENTO.
 * Dentro il ciclo gli argomenti non cambiano mai, quindi una funzione pura
 * chiamata mille volte con gli stessi puntatori ha mille volte lo stesso
 * risultato — e GCC la tira FUORI dal ciclo e moltiplica. Il sintomo e' lo
 * stesso di prima (numeri impossibili) ma la cura e' un'altra: leggere i
 * puntatori da variabili volatile, che il compilatore deve rileggere a ogni
 * giro e sulle quali non puo' ragionare. Costa un accesso in memoria per
 * chiamata, cioe' niente accanto a cio' che si misura. */
static void *volatile g_vd;
static void *volatile g_vs;

typedef void (*Passata)(void *d, void *s, unsigned int n);

static unsigned int g_rip = 1;      /* quante chiamate dentro un giro */

static unsigned long misura(Passata p, void *d, void *s, unsigned int n,
                            unsigned int *ms_fatti)
{
    unsigned int inizio, passati = 0, giri = 0, kib, byte_giro;

    g_rip = n ? (BLOCCO / n) : 1u;
    if (g_rip == 0) g_rip = 1u;
    byte_giro = n * g_rip;

    inizio = uptime_ms();
    while (passati < MS_MINIMI && giri < GIRI_MAX) {
        p(d, s, n);
        giri++;
        passati = uptime_ms() - inizio;
    }

    *ms_fatti = passati ? passati : 1u;

    /* Si lavora in KiB per non uscire dai 32 bit, e il fattore 1000/1024 si
     * applica PRIMA di dividere per non perdere le cifre. */
    kib = (byte_giro >> 10) * giri;
    if (kib == 0) return 0;
    return (unsigned long)((kib * 125u / 128u) / *ms_fatti);
}

/* Gli involucri: tutti la stessa forma, e la ripetizione interna dentro. */
static void p_vecchia_copia(void *d, void *s, unsigned int n)
{ unsigned int i; g_vd = d; g_vs = s; for (i = 0; i < g_rip; i++) vecchia_memcpy(g_vd, g_vs, n); }

static void p_nuova_copia(void *d, void *s, unsigned int n)
{ unsigned int i; g_vd = d; g_vs = s; for (i = 0; i < g_rip; i++) memcpy(g_vd, g_vs, n); }

static void p_vecchia_riempi(void *d, void *s, unsigned int n)
{ unsigned int i; (void)s; g_vd = d; for (i = 0; i < g_rip; i++) vecchia_memset(g_vd, 0x5C, n); }

static void p_nuova_riempi(void *d, void *s, unsigned int n)
{ unsigned int i; (void)s; g_vd = d; for (i = 0; i < g_rip; i++) memset(g_vd, 0x5C, n); }

static void p_vecchia_sposta(void *d, void *s, unsigned int n)
{ unsigned int i; (void)s; g_vd = d; for (i = 0; i < g_rip; i++) vecchia_memmove((char *)g_vd + 8, g_vd, n); }

static void p_nuova_sposta(void *d, void *s, unsigned int n)
{ unsigned int i; (void)s; g_vd = d; for (i = 0; i < g_rip; i++) memmove((char *)g_vd + 8, g_vd, n); }

static void p_vecchia_confronta(void *d, void *s, unsigned int n)
{ unsigned int i; int a = 0; g_vd = d; g_vs = s;
  for (i = 0; i < g_rip; i++) a += vecchia_memcmp(g_vd, g_vs, n); g_pozzo = a; }

static void p_nuova_confronta(void *d, void *s, unsigned int n)
{ unsigned int i; int a = 0; g_vd = d; g_vs = s;
  for (i = 0; i < g_rip; i++) a += memcmp(g_vd, g_vs, n); g_pozzo = a; }

static void p_vecchia_lung(void *d, void *s, unsigned int n)
{ unsigned int i; int a = 0; (void)s; (void)n; g_vd = d;
  for (i = 0; i < g_rip; i++) a += (int)vecchia_strlen((char *)g_vd); g_pozzo = a; }

static void p_nuova_lung(void *d, void *s, unsigned int n)
{ unsigned int i; int a = 0; (void)s; (void)n; g_vd = d;
  for (i = 0; i < g_rip; i++) a += (int)strlen((char *)g_vd); g_pozzo = a; }


/* ! IL NUMERO CHE DECIDE E' IL TERZO: il rapporto. I MB/s dicono quanto va
 * questa macchina, il rapporto dice quanto e' servito il lavoro — ed e' l'unico
 * dei tre che si puo' confrontare fra macchine diverse. */
static void riga(const char *nome, unsigned int n, unsigned long v, unsigned long nu)
{
    unsigned long r10 = v ? (nu * 10ul + v / 2ul) / v : 0ul;

    printf("  %-9s %7u  %6lu  %6lu   %lu.%lux\n",
           nome, n, v, nu, r10 / 10ul, r10 % 10ul);
}

static const unsigned int MISURE[] = { 16u, 64u, 256u, 1024u, 4096u, 65536u,
                                       262144u, 0u };

static void misura_tutto(unsigned char *a, unsigned char *b)
{
    unsigned int i, ms;

    printf("\n  funzione  blocco  vecchia  nuova   guadagno\n");
    printf("  --------- ------- -------- ------- ---------\n");

    for (i = 0; MISURE[i]; i++) {
        unsigned int n = MISURE[i];
        unsigned long v, nu;

        v  = misura(p_vecchia_copia, a, b, n, &ms);
        nu = misura(p_nuova_copia,   a, b, n, &ms);
        riga("memcpy", n, v, nu);
    }

    printf("\n");
    for (i = 0; MISURE[i]; i++) {
        unsigned int n = MISURE[i];
        unsigned long v, nu;

        v  = misura(p_vecchia_riempi, a, 0, n, &ms);
        nu = misura(p_nuova_riempi,   a, 0, n, &ms);
        riga("memset", n, v, nu);
    }

    printf("\n");
    for (i = 0; MISURE[i]; i++) {
        unsigned int n = MISURE[i];
        unsigned long v, nu;

        if (n > TAM_MAX - 64u) continue;        /* +8 di sfalsamento, sta largo */
        v  = misura(p_vecchia_sposta, a, 0, n, &ms);
        nu = misura(p_nuova_sposta,   a, 0, n, &ms);
        riga("memmove", n, v, nu);
    }

    printf("\n");
    for (i = 0; MISURE[i]; i++) {
        unsigned int n = MISURE[i];
        unsigned long v, nu;

        /* ! I DUE BUFFER SI FANNO UGUALI PRIMA, se no memcmp si ferma al primo
         * byte e misura il nulla. */
        memcpy(b, a, n);
        v  = misura(p_vecchia_confronta, a, b, n, &ms);
        nu = misura(p_nuova_confronta,   a, b, n, &ms);
        riga("memcmp", n, v, nu);
    }

    printf("\n");
    for (i = 0; MISURE[i]; i++) {
        unsigned int n = MISURE[i];
        unsigned long v, nu;

        if (n > TAM_MAX - CODA) continue;
        vecchia_memset(a, 'x', n);
        a[n] = '\0';
        v  = misura(p_vecchia_lung, a, 0, n, &ms);
        nu = misura(p_nuova_lung,   a, 0, n, &ms);
        riga("strlen", n, v, nu);
    }
}


int main(int argc, char **argv)
{
    unsigned char *a, *b, *seme;
    int            solo_check = 0, solo_misura = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-solocheck") == 0) solo_check = 1;
        else if (strcmp(argv[i], "-solomisura") == 0) solo_misura = 1;
        else {
            printf("uso: memprova [-solocheck] [-solomisura]\n");
            return 1;
        }
    }

    a    = (unsigned char *)malloc(TAM_MAX + CODA);
    b    = (unsigned char *)malloc(TAM_MAX + CODA);
    seme = (unsigned char *)malloc(TAM_MAX + CODA);
    if (a == 0 || b == 0 || seme == 0) {
        printf("memprova: non c'e' memoria per tre buffer da %u byte.\n",
               TAM_MAX + CODA);
        return 1;
    }

    /* Un riempimento che NON sia costante: una copia rotta su dati tutti
     * uguali passa lo stesso. */
    for (i = 0; i < (int)(TAM_MAX + CODA); i++)
        seme[i] = (unsigned char)((i * 37) ^ (i >> 5));
    vecchia_memcpy(a, seme, TAM_MAX + CODA);
    vecchia_memcpy(b, seme, TAM_MAX + CODA);

    printf("\nmemprova: la libc nuova contro quella a un byte per volta\n");

    if (!solo_misura) controlla(a, b, seme);
    if (!solo_check)  misura_tutto(a, b);

    printf("\n");
    if (g_errori) {
        printf("  ! CI SONO %d ERRORI DI CORRETTEZZA: la libc nuova NON va\n",
               g_errori);
        printf("    messa in circolazione finche' non sono spiegati.\n");
    }

    free(seme);
    free(b);
    free(a);
    return g_errori ? 1 : 0;
}

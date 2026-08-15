/* =============================================================================
 * tools/openlibm-exos/nearbyintl-exos.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * nearbyintl — la funzione che manca a openlibm 0.8.7.
 *
 * ! NON E' UNA NOSTRA CONFIGURAZIONE SBAGLIATA: E' UNA LACUNA A MONTE.
 * openlibm genera nearbyint e nearbyintf da una macro in src/s_nearbyint.c:
 *
 *     DECL(double, nearbyint, rint)
 *     DECL(float, nearbyintf, rintf)
 *
 * e la terza riga — `DECL(long double, nearbyintl, rintl)` — non c'e'.
 * `rintl` invece c'e' ed e' pure in assembly x87 (i387/s_rintl.S), quindi
 * il pezzo che manca e' solo l'involucro.
 *
 * -----------------------------------------------------------------------------
 * CHI LA CHIEDE, e perche' la sua assenza costa cara
 *
 * Il configure della libstdc++ compila UN SOLO programma che usa tutte le
 * funzioni C99 di <math.h>, e da quello decide `_GLIBCXX_USE_C99_MATH_FUNCS`.
 * ! Una sola assenza fa dichiarare non conforme l'header intero, e la
 * libreria rinuncia a mettere in `namespace std` decine di funzioni che
 * invece ci sono. L'errore che si vede dopo e' lontanissimo dalla causa:
 *
 *     std.cc: error: 'cbrt' has not been declared in 'std'
 *
 * cioe' «manca cbrt» quando cbrt c'e' e a mancare e' nearbyintl.
 *
 * -----------------------------------------------------------------------------
 * ! LA DIFFERENZA FRA rint E nearbyint E' UNA SOLA, E STA TUTTA QUI
 *
 * Danno lo stesso numero. `rint` pero' solleva l'eccezione INEXACT quando
 * l'argomento non era gia' intero; `nearbyint` no — e' l'unica ragione per
 * cui esistono entrambe. Chi vuole arrotondare senza sporcare i flag di
 * stato della FPU usa nearbyint.
 *
 * Il modo di ottenerlo e' quello di FreeBSD: si salva l'ambiente della
 * FPU, si chiama rintl, si rimette l'ambiente com'era — buttando via
 * l'INEXACT appena alzata insieme a tutto il resto.
 *
 * ! fnstenv MASCHERA TUTTE LE ECCEZIONI come effetto collaterale (lo dice
 * il manuale Intel), quindi la control word va rimessa subito: senza,
 * `rintl` girerebbe in un ambiente diverso da quello del chiamante. Su
 * EX-OS le eccezioni sono mascherate comunque e non si noterebbe, ma un
 * codice che dipende da un dettaglio del sistema invece che dal contratto
 * e' esattamente cio' che poi si rompe altrove.
 * ============================================================================= */

long double rintl(long double);
long double nearbyintl(long double);

long double nearbyintl(long double x)
{
    /* L'ambiente x87 in modo protetto a 32 bit occupa 28 byte. */
    unsigned char env[28];
    unsigned short cw;
    long double r;

    __asm__ __volatile__ ("fnstenv %0" : "=m" (env) : : "memory");

    /* La control word e' la prima parola dell'ambiente salvato: si
     * rimette subito, perche' fnstenv l'ha appena sostituita con "tutto
     * mascherato". */
    __builtin_memcpy(&cw, &env[0], sizeof(cw));
    __asm__ __volatile__ ("fldcw %0" : : "m" (cw));

    r = rintl(x);

    /* ! IL RISULTATO VA FORZATO IN MEMORIA PRIMA DI fldenv, E QUESTA
     * RIGA NON E' FACOLTATIVA.
     *
     * `fldenv` ripristina anche la TAG WORD, cioe' quali registri dell'x87
     * risultano occupati. Un `long double` restituito da una funzione sta
     * in st(0), e GCC lo tiene volentieri li' invece di scriverlo in
     * memoria: al `fldenv` quel registro viene rimarcato come VUOTO e il
     * valore sparisce. Il sintomo e' che nearbyintl(2.5) ritorna 0 —
     * numero sbagliato, nessun errore, nessun avviso.
     *
     * L'asm vuoto con "+m" (r) dice al compilatore: «da qui in poi `r` sta
     * in memoria e potrei averla modificata». Costa una `fstpt`, ed e'
     * esattamente cio' che fa la versione di FreeBSD, dove il valore passa
     * per una variabile prima di fesetenv().
     *
     * ! Chi tocca questa funzione non sposti la riga sotto il fldenv. */
    __asm__ __volatile__ ("" : "+m" (r));

    /* Rimette l'ambiente com'era, INEXACT compresa: e' questo che
     * distingue nearbyintl da rintl. */
    __asm__ __volatile__ ("fldenv %0" : : "m" (env) : "memory");

    return r;
}

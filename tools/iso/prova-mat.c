/* =============================================================================
 * tools/iso/prova-mat.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Prova che la libm gira davvero dentro EX-OS.
 *
 *     i386-exos-gcc -O2 -o provamat prova-mat.c -lm
 *
 * ⚠️ SERVE -lm. Dietro questi nomi c'e' openlibm (la `msun` di FreeBSD in
 * versione autonoma), che sta in $sysroot/lib/libm.a e si costruisce con
 * tools/openlibm-exos/prepara-libm.sh. Le uniche funzioni matematiche che
 * NON vogliono -lm sono sqrt, fabs, ldexp e frexp: quelle stanno nella
 * libc. Vedi lib/include/math.h.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ PERCHE' I RISULTATI SONO MOLTIPLICATI PER MILLE E STAMPATI COME INTERI
 *
 * La printf di EX-OS non formatta i `double`: non c'e' %f. Stampare i
 * risultati in virgola mobile proverebbe la printf, non la libm — e
 * fallirebbe per un motivo che con la matematica non c'entra niente.
 * Moltiplicare per mille e troncare a intero prova esattamente cio' che
 * si vuole provare: che il calcolo e' stato fatto e che il numero e'
 * quello giusto.
 *
 * Il +0.5 prima del cast e' un arrotondamento e non un trucco: senza,
 * sin(pi/2) — che vale 0,9999999999999999 e non 1 — darebbe 999.
 * ============================================================================= */

#include "stdio.h"
#include "math.h"

/* Arrotonda al millesimo e ritorna un intero, cosi' e' stampabile con %d. */
static long mille(double v)
{
    return (long)(v * 1000.0 + (v < 0 ? -0.5 : 0.5));
}

int main(void)
{
    printf("openlibm dentro EX-OS\n\n");

    printf("sin(pi/2)=%ld cos(0)=%ld pow(2,10)=%ld log(e)=%ld sqrt(16)=%ld\n",
           mille(sin(M_PI / 2.0)),
           mille(cos(0.0)),
           mille(pow(2.0, 10.0)),
           mille(log(M_E)),
           mille(sqrt(16.0)));

    printf("atan2(1,1)=%ld hypot(3,4)=%ld isnan(0/0.)=%d\n",
           mille(atan2(1.0, 1.0)),
           mille(hypot(3.0, 4.0)),
           isnan(0.0 / 0.0) ? 1 : 0);

    /* Le varianti `f` e `l` non sono scorciatoie: sono implementazioni
     * distinte, e vanno provate a parte o non si sa se ci sono davvero. */
    printf("sinf(pi/2)=%ld  sinl(pi/2)=%ld  exp2(10)=%ld\n",
           mille((double)sinf((float)(M_PI / 2.0))),
           mille((double)sinl((long double)(M_PI / 2.0))),
           mille(exp2(10.0)));

    /* -----------------------------------------------------------------
     * nearbyintl — la funzione che a openlibm 0.8.7 MANCA, e che
     * tools/openlibm-exos/nearbyintl-exos.c aggiunge all'archivio.
     *
     * ⚠️ LA PROVA CHE CONTA NON E' IL NUMERO. `rintl` e `nearbyintl`
     * danno lo stesso risultato: quello che le distingue e' che rintl
     * ALZA il flag INEXACT quando l'argomento non era gia' intero, e
     * nearbyintl no. Confrontare solo i valori proverebbe la meta'
     * sbagliata.
     *
     * Il flag si legge dalla status word dell'x87 con `fnstsw`: bit 5.
     * ----------------------------------------------------------------- */
    {
        unsigned short sw;
        long double    a, b;
        int            inex_dopo_rint, inex_dopo_nearby;

        printf("\n  nearbyintl (aggiunta: openlibm 0.8.7 non ce l'ha)\n");

        printf("    nearbyintl(2.5)=%ld  nearbyintl(-2.5)=%ld  nearbyintl(3.7)=%ld\n",
               (long)nearbyintl(2.5L), (long)nearbyintl(-2.5L),
               (long)nearbyintl(3.7L));

        /* Si azzerano i flag, si chiama rintl su un valore non intero, si
         * guarda INEXACT: deve essere alzato. */
        __asm__ __volatile__ ("fnclex");
        a = rintl(3.7L);
        __asm__ __volatile__ ("fnstsw %0" : "=am" (sw));
        inex_dopo_rint = (sw & 0x20) != 0;

        /* Stessa cosa con nearbyintl: NON deve essere alzato. */
        __asm__ __volatile__ ("fnclex");
        b = nearbyintl(3.7L);
        __asm__ __volatile__ ("fnstsw %0" : "=am" (sw));
        inex_dopo_nearby = (sw & 0x20) != 0;

        printf("    stesso risultato: %s\n", (a == b) ? "si" : "NO");
        printf("    rintl alza INEXACT: %s   nearbyintl: %s\n",
               inex_dopo_rint ? "si" : "no",
               inex_dopo_nearby ? "SI (SBAGLIATO)" : "no");
    }

    printf("\nLa libm risponde.\n");
    return 0;
}

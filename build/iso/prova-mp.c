/* =============================================================================
 * tools/iso/prova-mp.c — GMP, MPFR e MPC dentro EX-OS
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Le tre librerie a cui `cc1` si linka, provate dentro il sistema per cui
 * sono state compilate. Non e' una dimostrazione di calcolo: e' la prova
 * che il codice di terzi PIU' PESANTE che EX-OS abbia ospitato finora —
 * aritmetica a precisione arbitraria, con allocazioni continue e un uso
 * fitto di longjmp, qsort e stdio — gira davvero.
 *
 * Ognuno dei tre conti ha una risposta che si conosce a memoria, cosi' un
 * errore si vede senza andarsela a cercare:
 *
 *   GMP    2^128, che finisce per ...211456
 *   MPFR   pi greco a 50 cifre, che comincia per 3.14159265358979323846
 *   MPC    la radice di i, che e' (1+i)/sqrt(2) = 0.7071... + 0.7071...i
 *
 * Compilazione (dal cross, con le tre librerie gia' installate nel
 * sysroot da tools/gcclibs-exos/prepara-gcclibs.sh):
 *
 *     i386-exos-gcc -O2 -o prova-mp prova-mp.c -lmpc -lmpfr -lgmp
 *
 * ⚠️ L'ORDINE DELLE -l CONTA, ed e' quello: il linker risolve i simboli
 * scorrendo gli archivi da sinistra a destra e non torna indietro. MPC
 * chiama MPFR che chiama GMP, quindi vanno in quest'ordine — con -lgmp
 * per primo, `ld` scarterebbe libgmp.a perche' in quel momento nessuno ne
 * ha ancora chiesto niente.
 * ============================================================================= */

#include <stdio.h>
#include <gmp.h>
#include <mpfr.h>
#include <mpc.h>

int main(void)
{
    printf("GMP %s, MPFR %s, MPC %s — dentro EX-OS\n\n",
           gmp_version, mpfr_get_version(), mpc_get_version());

    /* --- GMP: interi grandi ------------------------------------------- */
    {
        mpz_t n;
        char *s;

        mpz_init(n);
        mpz_ui_pow_ui(n, 2, 128);          /* 2^128 */
        s = mpz_get_str(NULL, 10, n);
        printf("GMP   2^128 = %s\n", s);
        free(s);
        mpz_clear(n);
    }

    /* --- MPFR: virgola mobile a precisione arbitraria ------------------ */
    {
        mpfr_t pi;

        mpfr_init2(pi, 200);               /* 200 bit ~ 60 cifre decimali */
        mpfr_const_pi(pi, MPFR_RNDN);
        printf("MPFR  pi     = ");
        mpfr_out_str(stdout, 10, 50, pi, MPFR_RNDN);
        printf("\n");
        mpfr_clear(pi);
    }

    /* --- MPC: complessi ------------------------------------------------ */
    {
        mpc_t z;

        mpc_init2(z, 100);
        mpc_set_ui_ui(z, 0, 1, MPC_RNDNN); /* z = i */
        mpc_sqrt(z, z, MPC_RNDNN);         /* sqrt(i) = (1+i)/sqrt(2) */
        printf("MPC   sqrt(i)= ");
        mpc_out_str(stdout, 10, 20, z, MPC_RNDNN);
        printf("\n");
        mpc_clear(z);
    }

    /* MPFR tiene delle cache interne: senza questa, la memoria resta
     * occupata fino all'uscita del processo. Qui non cambia niente, ma e'
     * cio' che deve fare chi la usa dentro un programma piu' lungo. */
    mpfr_free_cache();

    printf("\nTutte e tre le librerie rispondono.\n");
    return 0;
}

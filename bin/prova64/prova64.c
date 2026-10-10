/* =============================================================================
 * bin/prova64/prova64.c — la libc alla prova, un pezzo per volta (@EXOS-64)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     prova64 [-shell]
 *
 * E' il gemello di tools/prove64/primo.c con la libc sotto: ogni riga prova
 * una cosa che dipende dalla larghezza dei registri o dall'accordo fra kernel
 * e programmi, e dice «giusto» o «SBAGLIATO». Si compila anche a 32 bit, dove
 * deve dire le stesse cose: e' il confronto.
 *
 * Con -shell (o messo al posto di /bin/sh, da cui il kernel lo avvia senza
 * argomenti) alla fine legge una riga dalla tastiera e la ripete: e' la prova
 * che la lettura dalla console arriva a un programma.
 * ============================================================================= */
#include "libc.h"
#include "pthread.h"

EX_VERSIONE("prova64", "0.001");

static int g_male = 0;

static void dice(const char *cosa, int bene)
{
    printf("  %-44s %s\n", cosa, bene ? "giusto" : "SBAGLIATO");
    if (!bene) g_male++;
}

static __thread int g_del_filo = 41;

/* --- i segnali -------------------------------------------------------------- */
static volatile int g_segnali = 0;
static volatile long g_r12_nel_gestore = 0;
static sigjmp_buf    g_fuori;

static void gestore(int sig)
{
    volatile double sporco = 3.25;      /* usa i registri SSE: chi e' interrotto non deve accorgersene */

    g_segnali += (sig == SIGUSR1) ? (int)(sporco * 4) : 1000;      /* 13 */
}

static void gestore_guasto(int sig, siginfo_t *info, void *contesto)
{
    (void)sig; (void)contesto;
    g_segnali += 100;
    g_r12_nel_gestore = (long)info->si_addr;
    siglongjmp(g_fuori, 7);
}

static void *filo(void *arg)
{
    g_del_filo += 100;                  /* la copia di QUESTO filo: parte da 41 */
    return (void *)(long)(g_del_filo + (int)(long)arg);
}

int main(int argc, char **argv, char **envp)
{
    char  b[128];
    char *p, *q;
    int   i, n;
    long  grande;
    jmp_buf salto;
    volatile int giri = 0;

    printf("\nprova64: la libc a %d bit\n", (int)sizeof(void *) * 8);
    printf("  argc=%d argv=%p envp=%p\n", argc, (void *)argv, (void *)envp);
    for (i = 0; i < argc && i < 4; i++) printf("    argv[%d] = \"%s\"\n", i, argv[i]);

    dice("sizeof(long) e' quello di un puntatore", sizeof(long) == sizeof(void *));
    dice("getpid() rende un numero", getpid() > 0);

    snprintf(b, sizeof(b), "%d %u %x %s %c %ld", -7, 4000000000u, 0xCAFE, "str", 'k', -123456789L);
    dice("snprintf con interi e stringhe", strcmp(b, "-7 4000000000 cafe str k -123456789") == 0);

    grande = (long)1 << (sizeof(long) * 8 - 2);
    snprintf(b, sizeof(b), "%lld", (long long)0x100000001LL * 16);
    dice("un numero di 64 bit stampato", strcmp(b, "68719476752") == 0);
    dice("un long largo come dice", grande > 0);

    p = malloc(100);
    q = malloc(300000);
    dice("malloc piccola e grande", p != NULL && q != NULL && p != q);
    if (p && q) {
        memset(q, 0x5A, 300000);
        strcpy(p, "ciao");
        for (i = 0, n = 0; i < 300000; i++) n += (q[i] == 0x5A);
        dice("memset e rilettura di 300000 byte", n == 300000 && strcmp(p, "ciao") == 0);
        q = realloc(q, 600000);
        dice("realloc che conserva", q != NULL && q[299999] == 0x5A);
        free(p);
        free(q);
    }

    if (setjmp(salto) == 0) {
        giri = 1;
        longjmp(salto, 5);
    } else {
        giri += 10;
    }
    dice("setjmp / longjmp", giri == 11);

    g_del_filo++;
    dice("una variabile __thread", g_del_filo == 42);

    {
        struct sigaction az;
        volatile double  conto = 1.5;
        int              r;

        memset(&az, 0, sizeof(az));
        az.sa_handler = gestore;
        sigaction(SIGUSR1, &az, NULL);
        raise(SIGUSR1);
        conto *= 2;
        dice("un segnale preso, e si riprende dopo", g_segnali == 13 && conto == 3.0);

        memset(&az, 0, sizeof(az));
        az.sa_sigaction = gestore_guasto;
        az.sa_flags     = SA_SIGINFO;
        sigaction(SIGSEGV, &az, NULL);
        r = sigsetjmp(g_fuori, 1);
        if (r == 0) *(volatile int *)0x123456780 = 1;       /* oltre i 4 GB a 64 bit */
        dice("un guasto di pagina preso, e se ne esce", r == 7 && g_segnali == 113);
        signal(SIGSEGV, SIG_DFL);
    }

    {
        pthread_t t;
        void     *reso = NULL;
        int       fatto = pthread_create(&t, NULL, filo, (void *)(long)5) == 0 &&
                          pthread_join(t, &reso) == 0;

        dice("un filo, con la sua copia di __thread", fatto && (long)reso == 146 && g_del_filo == 42);
    }

    errno = 0;
    dice("aprire un file che non c'e' da' ENOENT", open("/non/esiste", 0) < 0 && errno == ENOENT);

    {
        int   fd = open("/doc/leggimi.txt", 0);
        char  r[64];
        int   k = (fd >= 0) ? (int)read(fd, r, sizeof(r) - 1) : -1;

        if (fd >= 0) close(fd);
        dice("leggere un file dal CD", k > 0);
    }

    {
        double d = 2.0;
        volatile double m = 1.5;

        snprintf(b, sizeof(b), "%.3f", sqrt(d) * m);
        dice("virgola mobile e sqrt", strcmp(b, "2.121") == 0);
    }

    printf(g_male == 0 ? "PROVA64-LIBC-OK\n" : "PROVA64-LIBC: %d cose non tornano\n", g_male);

    if (argc == 0 || (argc > 1 && strcmp(argv[1], "-shell") == 0)) {
        printf("scrivi una riga e premi Invio: ");
        fflush(stdout);
        n = (int)read(0, b, sizeof(b) - 1);
        if (n > 0) {
            b[n] = 0;
            printf("letti %d byte: %s", n, b);
            printf("PROVA64-TASTIERA-OK\n");
        } else {
            printf("read ha reso %d\n", n);
        }
    }
    return g_male;
}

/* example_main.c
 * Dimostrazione d'uso di GF_BASIC, vicina all'esempio richiesto:
 *
 *   GF_BASIC myprog[10];
 *   myprog[1].set_program(...)   -> in C: gf_basic_set_program(myprog[1], ...)
 *   myprog[1].add_function(...)  -> gf_basic_add_function(myprog[1], ...)
 *   myprog[1].run_program()      -> gf_basic_run(myprog[1])
 *   myprog[1].sysreturn_string() -> gf_basic_get_return_string(myprog[1])
 */
#include "gf_basic.h"
#include <stdio.h>
#include <pthread.h>

/* ---- funzioni C "interne" da collegare al BASIC -------------------- */
static GfbValue myfunctionA(int argc, GfbValue *argv, void *user_data){
    (void)user_data;
    long a = argc > 0 ? gfb_to_int_pub(&argv[0]) : 0;
    long b = argc > 1 ? gfb_to_int_pub(&argv[1]) : 0;
    return gfb_val_int(a + b);
}
static GfbValue myfunctionB(int argc, GfbValue *argv, void *user_data){
    (void)user_data;
    long a = argc > 0 ? gfb_to_int_pub(&argv[0]) : 0;
    long b = argc > 1 ? gfb_to_int_pub(&argv[1]) : 0;
    return gfb_val_int(a * b);
}

static const char *mycode =
    "DIM a AS STRING\n"
    "DIM x AS INTEGER\n"
    "DIM y AS INTEGER\n"
    "\n"
    "a = \"Hello\"\n"
    "PRINT a\n"
    "x = myfunctionA(10, 11)\n"
    "y = myfunctionB(5, 2)\n"
    "PRINT x\n"
    "PRINT y\n"
    "SYSRETURN_STRING(\"Message\")\n"
    "SYSRETURN_VALUE(721)\n";

/* thread di esempio: ogni thread ha la sua istanza -> nessuna necessita'
 * di sincronizzazione esterna */
static void *worker(void *arg){
    (void)arg;
    GF_BASIC *p = gf_basic_create();
    gf_basic_add_function(p, "myfunctionA", myfunctionA, NULL);
    gf_basic_add_function(p, "myfunctionB", myfunctionB, NULL);
    gf_basic_set_program(p, mycode);
    gf_basic_run(p);
    printf("[thread] sysreturn_value = %ld\n", gf_basic_get_return_value(p));
    gf_basic_destroy(p);
    return NULL;
}

int main(void){
    /* array di 10 "oggetti" come nel tuo esempio */
    GF_BASIC *myprog[10] = {0};

    myprog[1] = gf_basic_create();
    gf_basic_add_function(myprog[1], "myfunctionA", myfunctionA, NULL);
    gf_basic_add_function(myprog[1], "myfunctionB", myfunctionB, NULL);

    if (gf_basic_set_program(myprog[1], mycode) != 0) {
        fprintf(stderr, "Errore di caricamento: %s\n", gf_basic_get_error(myprog[1]));
        return 1;
    }

    if (gf_basic_run(myprog[1]) != 0) {
        fprintf(stderr, "Errore di esecuzione: %s\n", gf_basic_get_error(myprog[1]));
        return 1;
    }

    const char *sr = gf_basic_get_return_string(myprog[1]);
    long        vr = gf_basic_get_return_value(myprog[1]);
    printf("sysreturn_string = \"%s\"\n", sr);
    printf("sysreturn_value  = %ld\n", vr);

    gf_basic_destroy(myprog[1]);

    /* dimostrazione multi-thread: 4 istanze indipendenti in parallelo */
    pthread_t th[4];
    for (int k = 0; k < 4; k++) pthread_create(&th[k], NULL, worker, NULL);
    for (int k = 0; k < 4; k++) pthread_join(th[k], NULL);

    return 0;
}

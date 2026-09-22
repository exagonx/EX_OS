/*
 * gf_basic.h
 * ----------
 * Interprete QBASIC (sottoinsieme esteso) in C puro, thread-safe.
 * Ogni oggetto GF_BASIC e' indipendente e possiede il proprio mutex
 * interno: istanze diverse possono girare in thread diversi senza
 * alcuna sincronizzazione esterna. La stessa istanza puo' anche essere
 * chiamata da piu' thread (le chiamate vengono serializzate dal mutex),
 * ma NON e' pensata per essere eseguita in parallelo da piu' thread
 * contemporaneamente sullo stesso oggetto: il mutex la rende sicura,
 * non concorrente.
 *
 * Licenza: GNU GPL v2 (come da progetto EX-OS di riferimento).
 */
#ifndef GF_BASIC_H
#define GF_BASIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Tipi di valore manipolati dall'interprete                           */
/* ------------------------------------------------------------------ */
typedef enum {
    GFB_INT = 0,   /* intero (long)                                    */
    GFB_DBL = 1,   /* virgola mobile (double)                          */
    GFB_STR = 2    /* stringa (char* posseduta dal valore)             */
} GfbType;

typedef struct {
    GfbType type;
    long    i;     /* valido se type == GFB_INT (o come booleano)      */
    double  d;     /* valido se type == GFB_DBL                        */
    char   *s;     /* valido se type == GFB_STR, malloc'ata            */
} GfbValue;

/* Firma richiesta per una funzione C collegabile al codice BASIC.
 * argc/argv sono gli argomenti passati dal codice BASIC nella chiamata
 * funzione(a,b,...). La funzione ritorna un GfbValue (usare le helper
 * gfb_val_int/gfb_val_dbl/gfb_val_str per costruirlo). */
typedef GfbValue (*GfExternFn)(int argc, GfbValue *argv, void *user_data);

/* Costruttori comodi per GfbValue (usali dentro le tue funzioni extern) */
GfbValue gfb_val_int(long v);
GfbValue gfb_val_dbl(double v);
GfbValue gfb_val_str(const char *v);     /* fa una copia interna        */

/* Helper per leggere un GfbValue ricevuto come argomento dentro una tua
 * funzione extern (fanno la coercizione di tipo automaticamente). */
long        gfb_to_int_pub(const GfbValue *v);
double      gfb_to_dbl_pub(const GfbValue *v);
const char *gfb_to_str_pub(const GfbValue *v); /* valido solo se v->type==GFB_STR */

/* ------------------------------------------------------------------ */
/* Oggetto opaco                                                       */
/* ------------------------------------------------------------------ */
typedef struct GF_BASIC GF_BASIC;

/* Crea/distrugge un interprete. Ogni istanza e' completamente
 * indipendente (proprio stato, proprio mutex). */
GF_BASIC *gf_basic_create(void);
void      gf_basic_destroy(GF_BASIC *obj);

/* Carica (o sostituisce) il sorgente BASIC da eseguire.
 * Ritorna 0 se ok, -1 se errore di sintassi/preprocessing
 * (usa gf_basic_get_error per il dettaglio). */
int gf_basic_set_program(GF_BASIC *obj, const char *code);

/* Registra una funzione C richiamabile dal codice BASIC con il nome
 * indicato (case-insensitive, come tutte le identificatore QBASIC).
 * user_data e' un puntatore opaco che ti viene ripassato nella call,
 * utile per contesto (puoi anche passare NULL). */
int gf_basic_add_function(GF_BASIC *obj, const char *name,
                           GfExternFn fn, void *user_data);

/* Esegue il programma caricato. Ritorna 0 se terminato regolarmente,
 * -1 in caso di errore runtime (vedi gf_basic_get_error). Thread-safe:
 * internamente prende il mutex dell'oggetto per tutta la durata. */
int gf_basic_run(GF_BASIC *obj);

/* Valori impostati dal programma BASIC con SYSRETURN_STRING(expr) /
 * SYSRETURN_VALUE(expr). La stringa resta valida fino alla prossima
 * gf_basic_run() o gf_basic_destroy(). */
const char *gf_basic_get_return_string(GF_BASIC *obj);
long        gf_basic_get_return_value(GF_BASIC *obj);

/* Lettura/scrittura dirette di variabili BASIC dall'esterno (utile per
 * passare parametri d'ingresso o leggere risultati senza SYSRETURN). */
int gf_basic_set_var_int(GF_BASIC *obj, const char *name, long v);
int gf_basic_set_var_dbl(GF_BASIC *obj, const char *name, double v);
int gf_basic_set_var_str(GF_BASIC *obj, const char *name, const char *v);
long   gf_basic_get_var_int(GF_BASIC *obj, const char *name);
double gf_basic_get_var_dbl(GF_BASIC *obj, const char *name);
const char *gf_basic_get_var_str(GF_BASIC *obj, const char *name);

/* Ultimo messaggio di errore (vuoto se nessun errore). */
const char *gf_basic_get_error(GF_BASIC *obj);

#ifdef __cplusplus
}
#endif

#endif /* GF_BASIC_H */

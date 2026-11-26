/* =============================================================================
 * exwin/bin/exbrowser/browser_wasm.c — WebAssembly over WAMR (P12)
 *
 * Copyright (C) 2026 Graziano Falcone
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * WAMR 2.4.6 (tools/wamr-exos, libiwasm.so.2: the fast interpreter, no JIT,
 * no executable memory) opened with dlopen, through the standard C API of
 * wasm.h. One native, __exos_wasm(op, ...), for browser_wasm.js:
 *   "valida", bytes              -> true/false
 *   "modulo", bytes              -> a module id, or -1 ("errore" says why)
 *   "descrivi", mod              -> JSON {imports: [...], exports: [...]}
 *   "prenota"                    -> an instance id, before instantiating
 *   "istanza", id, mod, imports  -> 1 or 0; imports: an array, one entry per
 *                                   import of the module: a number for a
 *                                   function (JS keeps the function), or
 *                                   [instance, export] for a memory, or
 *                                   a number value for a global
 *   "chiama", id, export, args...-> the result (an array if several);
 *                                   a trap: the string "\001message"
 *   "memoria", id, export        -> an ArrayBuffer ON the module's memory
 *   "pagine", id, export         -> its size in 64 KB pages
 *   "cresci", id, export, delta  -> the old size, or -1
 *   "globale", id, export[, v]   -> a global's value (and sets it)
 *   "stacca", buffer             -> detaches an old memory.buffer
 *   "errore"                     -> the last error's text
 * A function import, called by the module, calls the JavaScript dispatcher
 * __exos_wasm_su(instance, import, args...) of browser_wasm.js.
 *
 * ! i64 BECOMES A NUMBER, not a BigInt: exact up to 2^53. ! ONE STORE for
 * the whole browser: instances of different pages never meet, a page's are
 * dropped when it goes (wasm_tutti_via).
 * ========================================================================== */
#include "browser_priv.h"
#include "browser_wasm_js.h"

/* --- the few declarations of wasm.h (WAMR's vectors carry two more fields) */
typedef struct { size_t size; void *data; size_t num_elems; size_t size_of_elem; void *lock; } WVec;
typedef struct { unsigned char kind; unsigned char pad[7]; union { int i32; long long i64; float f32; double f64; void *ref; } of; } WVal;
typedef struct { size_t size; WVal *data; size_t num_elems; size_t size_of_elem; void *lock; } WValVec;
typedef struct { unsigned int min, max; } WLimiti;
enum { K_I32, K_I64, K_F32, K_F64, K_V128, K_EXTERNREF = 128, K_FUNCREF };
enum { E_FUNC, E_GLOBAL, E_TABLE, E_MEMORY };

static void *(*w_engine_new)(void);
static void *(*w_store_new)(void *);
static void  (*w_byte_vec_new)(WVec *, size_t, const char *);
static void  (*w_byte_vec_delete)(WVec *);
static int   (*w_module_validate)(void *, const WVec *);
static void *(*w_module_new)(void *, const WVec *);
static void  (*w_module_delete)(void *);
static void  (*w_module_imports)(const void *, WVec *);
static void  (*w_module_exports)(const void *, WVec *);
static void  (*w_importtype_vec_delete)(WVec *);
static void  (*w_exporttype_vec_delete)(WVec *);
static const WVec *(*w_importtype_module)(const void *);
static const WVec *(*w_importtype_name)(const void *);
static const void *(*w_importtype_type)(const void *);
static const WVec *(*w_exporttype_name)(const void *);
static const void *(*w_exporttype_type)(const void *);
static unsigned char (*w_externtype_kind)(const void *);
static const void *(*w_externtype_as_functype)(const void *);
static const void *(*w_externtype_as_globaltype)(const void *);
static const WVec *(*w_functype_params)(const void *);
static const WVec *(*w_functype_results)(const void *);
static unsigned char (*w_valtype_kind)(const void *);
static const void *(*w_globaltype_content)(const void *);
static void *(*w_func_new_with_env)(void *, const void *, void *(*)(void *, const WValVec *, WValVec *), void *, void (*)(void *));
static void *(*w_func_as_extern)(void *);
static void *(*w_global_new)(void *, const void *, const WVal *);
static void *(*w_global_as_extern)(void *);
static void *(*w_instance_new)(void *, const void *, const WVec *, void **, unsigned int, unsigned int);
static void  (*w_instance_delete)(void *);
static void  (*w_instance_exports)(const void *, WVec *);
static void  (*w_extern_vec_delete)(WVec *);
static unsigned char (*w_extern_kind)(const void *);
static void *(*w_extern_as_func)(void *);
static void *(*w_extern_as_memory)(void *);
static void *(*w_extern_as_global)(void *);
static void *(*w_func_call)(const void *, const WValVec *, WValVec *);
static void *(*w_func_type)(const void *);
static void  (*w_functype_delete)(void *);
static void  (*w_trap_message)(const void *, WVec *);
static void  (*w_trap_delete)(void *);
static void *(*w_trap_new)(void *, const WVec *);
static char *(*w_memory_data)(void *);
static size_t (*w_memory_data_size)(const void *);
static unsigned int (*w_memory_size)(const void *);
static int   (*w_memory_grow)(void *, unsigned int);
static void  (*w_global_get)(const void *, WVal *);
/* ! memory.grow FROM THE HOST: WAMR's wasm.h refuses it ("only via the
 * opcode memory.grow"), its own API does it: the instance and the index of
 * the memory are read from WAMR's wasm_memory_t (wasm_c_api_internal.h of
 * 2.4.6: store, module_name, name, kind, host_info, type, memory_idx_rt,
 * inst_comm_rt), mirrored here */
typedef struct {
    void *store, *module_name, *name;
    unsigned short kind;
    void *hi_info; void (*hi_fin)(void *);
    void *type;
    unsigned short memory_idx_rt;
    void *inst_comm_rt;
} WMemInterna;
static void *(*w_rt_get_memory)(void *, unsigned int);
static int   (*w_mem_enlarge)(void *, unsigned long long);
static void  (*w_global_set)(void *, const WVal *);

static int   g_cercata, g_pronta;
static void *g_motore, *g_negozio;
static char  g_errore[256];

static void *sim(void *l, const char *n, int *ok)
{
    void *p = dlsym(l, n);

    if (!p) { *ok = 0; printf("exbrowser: wasm: manca %s\n", n); }
    return p;
}

static int wamr_pronta(void)
{
    void *l;
    int   ok = 1;

    if (g_cercata) return g_pronta;
    g_cercata = 1;
    l = dlopen("libiwasm.so.2", RTLD_NOW);
    if (!l) return 0;
#define S(v, n) *(void **)&v = sim(l, n, &ok)
    S(w_engine_new, "wasm_engine_new"); S(w_store_new, "wasm_store_new");
    S(w_byte_vec_new, "wasm_byte_vec_new"); S(w_byte_vec_delete, "wasm_byte_vec_delete");
    S(w_module_validate, "wasm_module_validate"); S(w_module_new, "wasm_module_new");
    S(w_module_delete, "wasm_module_delete"); S(w_module_imports, "wasm_module_imports");
    S(w_module_exports, "wasm_module_exports"); S(w_importtype_vec_delete, "wasm_importtype_vec_delete");
    S(w_exporttype_vec_delete, "wasm_exporttype_vec_delete"); S(w_importtype_module, "wasm_importtype_module");
    S(w_importtype_name, "wasm_importtype_name"); S(w_importtype_type, "wasm_importtype_type");
    S(w_exporttype_name, "wasm_exporttype_name"); S(w_exporttype_type, "wasm_exporttype_type");
    S(w_externtype_kind, "wasm_externtype_kind"); S(w_externtype_as_functype, "wasm_externtype_as_functype_const");
    S(w_externtype_as_globaltype, "wasm_externtype_as_globaltype_const");
    S(w_functype_params, "wasm_functype_params"); S(w_functype_results, "wasm_functype_results");
    S(w_valtype_kind, "wasm_valtype_kind"); S(w_globaltype_content, "wasm_globaltype_content");
    S(w_func_new_with_env, "wasm_func_new_with_env"); S(w_func_as_extern, "wasm_func_as_extern");
    S(w_global_new, "wasm_global_new"); S(w_global_as_extern, "wasm_global_as_extern");
    S(w_instance_new, "wasm_instance_new_with_args"); S(w_instance_delete, "wasm_instance_delete");
    S(w_instance_exports, "wasm_instance_exports"); S(w_extern_vec_delete, "wasm_extern_vec_delete");
    S(w_extern_kind, "wasm_extern_kind"); S(w_extern_as_func, "wasm_extern_as_func");
    S(w_extern_as_memory, "wasm_extern_as_memory"); S(w_extern_as_global, "wasm_extern_as_global");
    S(w_func_call, "wasm_func_call"); S(w_func_type, "wasm_func_type"); S(w_functype_delete, "wasm_functype_delete");
    S(w_trap_message, "wasm_trap_message"); S(w_trap_delete, "wasm_trap_delete"); S(w_trap_new, "wasm_trap_new");
    S(w_memory_data, "wasm_memory_data"); S(w_memory_data_size, "wasm_memory_data_size");
    S(w_memory_size, "wasm_memory_size"); S(w_memory_grow, "wasm_memory_grow");
    S(w_global_get, "wasm_global_get"); S(w_global_set, "wasm_global_set");
    S(w_rt_get_memory, "wasm_runtime_get_memory"); S(w_mem_enlarge, "wasm_memory_enlarge");
#undef S
    if (!ok) return 0;
    g_motore = w_engine_new();
    g_negozio = g_motore ? w_store_new(g_motore) : 0;
    g_pronta = g_negozio != 0;
    return g_pronta;
}

/* ---------------------------------------------------------- the tables */
#define MOD_MAX  64
#define IST_MAX  64
#define IMP_MAX  1024
typedef struct { void *m; const void *vista; } Mod;
typedef struct Ist Ist;
typedef struct { Ist *ist; int indice; const void *tipo; } Ponte;
struct Ist {
    int         usato;
    const void *vista;
    void       *i;
    WVec        esporti;             /* wasm_extern_vec_t */
    int         ha_esporti;
    Ponte      *ponti;               /* one per function import */
    int         n_ponti, id;
    ExJsCtx    *js;
};
static Mod g_mod[MOD_MAX];
static Ist g_ist[IST_MAX];

static void errore(const char *m) { snprintf(g_errore, sizeof(g_errore), "%s", m); }

static const char *nome_tipo(unsigned char k)
{
    switch (k) {
    case K_I32: return "i32"; case K_I64: return "i64"; case K_F32: return "f32";
    case K_F64: return "f64"; case K_V128: return "v128"; case K_FUNCREF: return "anyfunc";
    default: return "externref";
    }
}

static int json_tipi(char *o, int k, int max, const WVec *v)
{
    size_t i;

    k += snprintf(o + k, (size_t)(max - k), "[");
    for (i = 0; v && i < v->num_elems && k < max - 16; i++)
        k += snprintf(o + k, (size_t)(max - k), "%s\"%s\"", i ? "," : "", nome_tipo(w_valtype_kind(((void **)v->data)[i])));
    return k + snprintf(o + k, (size_t)(max - k), "]");
}

static int json_nome(char *o, int k, int max, const WVec *n)
{
    size_t i;

    k += snprintf(o + k, (size_t)(max - k), "\"");
    for (i = 0; n && i < n->size && k < max - 8; i++) {
        unsigned char c = ((const unsigned char *)n->data)[i];

        if (c == '"' || c == '\\') { o[k++] = '\\'; o[k++] = (char)c; }
        else if (c >= 0x20) o[k++] = (char)c;
    }
    return k + snprintf(o + k, (size_t)(max - k), "\"");
}

static int json_tipo_esterno(char *o, int k, int max, const void *et)
{
    static const char *K[] = { "function", "global", "table", "memory" };
    unsigned char      kk = w_externtype_kind(et);

    k += snprintf(o + k, (size_t)(max - k), ",\"kind\":\"%s\"", kk < 4 ? K[kk] : "?");
    if (kk == E_FUNC) {
        const void *ft = w_externtype_as_functype(et);

        k += snprintf(o + k, (size_t)(max - k), ",\"params\":");
        k = json_tipi(o, k, max, w_functype_params(ft));
        k += snprintf(o + k, (size_t)(max - k), ",\"results\":");
        k = json_tipi(o, k, max, w_functype_results(ft));
    } else if (kk == E_GLOBAL) {
        const void *gt = w_externtype_as_globaltype(et);

        k += snprintf(o + k, (size_t)(max - k), ",\"type\":\"%s\"", nome_tipo(w_valtype_kind(w_globaltype_content(gt))));
    }
    return k;
}

/* ---------------------------------------------------- values both ways */
static double val_a_num(const WVal *v)
{
    switch (v->kind) {
    case K_I32: return v->of.i32;
    case K_I64: return (double)v->of.i64;
    case K_F32: return v->of.f32;
    case K_F64: return v->of.f64;
    default:    return 0;
    }
}

static void num_a_val(WVal *v, unsigned char k, double d)
{
    v->kind = k;
    switch (k) {
    case K_I32: v->of.i32 = (int)(long long)d; break;      /* ToInt32, as a wrap */
    case K_I64: v->of.i64 = (long long)d; break;
    case K_F32: v->of.f32 = (float)d; break;
    case K_F64: v->of.f64 = d; break;
    default:    v->of.ref = 0; break;
    }
}

/* a function import: the module calls JavaScript */
static void *ponte_chiamato(void *env, const WValVec *args, WValVec *ris)
{
    Ponte     *p = (Ponte *)env;
    ExJsCtx   *c = p->ist->js;
    ExJsVal    a[34], f, r;
    ExJsErrore err;
    size_t     i, n = args ? args->num_elems : 0;

    if (n > 32) n = 32;
    a[0] = exjs_numero(c, p->ist->id);
    a[1] = exjs_numero(c, p->indice);
    for (i = 0; i < n; i++) a[2 + i] = exjs_numero(c, val_a_num(&args->data[i]));
    f = exjs_prendi(c, exjs_globale(c), "__exos_wasm_su");
    memset(&err, 0, sizeof(err));
    r = exjs_chiama(c, f, exjs_indefinito(), a, (int)n + 2, &err);
    if (err.messaggio[0]) {
        WVec m;

        w_byte_vec_new(&m, strlen(err.messaggio) + 1, err.messaggio);
        {
            void *t = w_trap_new(g_negozio, &m);

            w_byte_vec_delete(&m);
            return t;
        }
    }
    if (ris && ris->num_elems > 0) {
        const WVec *rt = w_functype_results(p->tipo);

        if (ris->num_elems == 1) num_a_val(&ris->data[0], w_valtype_kind(((void **)rt->data)[0]), exjs_a_numero(c, r));
        else
            for (i = 0; i < ris->num_elems; i++)
                num_a_val(&ris->data[i], w_valtype_kind(((void **)rt->data)[i]), exjs_a_numero(c, exjs_indice_prendi(c, r, (unsigned int)i)));
    }
    return 0;
}

static Ist *ist_di(ExJsCtx *c, ExJsVal v)
{
    int i = (int)exjs_a_numero(c, v);

    return (i >= 0 && i < IST_MAX && g_ist[i].usato && g_ist[i].i && g_ist[i].vista == g_vi) ? &g_ist[i] : 0;
}

static void *esporto(Ist *t, int k, unsigned char tipo)
{
    void *e;

    if (!t || k < 0 || (size_t)k >= t->esporti.num_elems) return 0;
    e = ((void **)t->esporti.data)[k];
    if (w_extern_kind(e) != tipo) return 0;
    return tipo == E_FUNC ? w_extern_as_func(e) : tipo == E_MEMORY ? w_extern_as_memory(e) : w_extern_as_global(e);
}

static void ist_via(Ist *t)
{
    if (t->ha_esporti) w_extern_vec_delete(&t->esporti);
    if (t->i) w_instance_delete(t->i);
    free(t->ponti);
    memset(t, 0, sizeof(*t));
}

void wasm_tutti_via(void)
{
    int i;

    if (!g_pronta) return;
    for (i = 0; i < IST_MAX; i++) if (g_ist[i].usato && g_ist[i].vista == g_vi) ist_via(&g_ist[i]);
    for (i = 0; i < MOD_MAX; i++) if (g_mod[i].m && g_mod[i].vista == g_vi) { w_module_delete(g_mod[i].m); g_mod[i].m = 0; }
}

/* ---------------------------------------------------------------- ops */
static ExJsVal op_istanza(ExJsCtx *c, const ExJsVal *a, int n_arg)
{
    Ist        *t;
    int         m = (int)exjs_a_numero(c, a[2]), k, np = 0;
    WVec        imp, ext;
    void      **lista;
    void       *trap = 0;

    if (n_arg < 4) return exjs_numero(c, 0);
    {
        int i = (int)exjs_a_numero(c, a[1]);

        if (i < 0 || i >= IST_MAX || !g_ist[i].usato || g_ist[i].i) return exjs_numero(c, 0);
        t = &g_ist[i];
    }
    if (m < 0 || m >= MOD_MAX || !g_mod[m].m) { errore("modulo sconosciuto"); return exjs_numero(c, 0); }
    w_module_imports(g_mod[m].m, &imp);
    lista = (void **)calloc(imp.num_elems + 1, sizeof(void *));
    t->ponti = (Ponte *)calloc(imp.num_elems + 1, sizeof(Ponte));
    if (!lista || !t->ponti) { w_importtype_vec_delete(&imp); free(lista); errore("memoria"); return exjs_numero(c, 0); }
    for (k = 0; k < (int)imp.num_elems; k++) {
        const void   *it = ((void **)imp.data)[k], *et = w_importtype_type(it);
        unsigned char kk = w_externtype_kind(et);
        ExJsVal       x = exjs_indice_prendi(c, a[3], (unsigned int)k);

        if (kk == E_FUNC) {
            Ponte *p = &t->ponti[np++];
            void  *f;

            p->ist = t; p->indice = (int)exjs_a_numero(c, x); p->tipo = w_externtype_as_functype(et);
            f = w_func_new_with_env(g_negozio, p->tipo, ponte_chiamato, p, 0);
            lista[k] = f ? w_func_as_extern(f) : 0;
        } else if (kk == E_MEMORY) {
            /* [instance, export] of a memory an instance exported */
            Ist  *s = ist_di(c, exjs_indice_prendi(c, x, 0));
            void *e = s ? ((void **)s->esporti.data)[(int)exjs_a_numero(c, exjs_indice_prendi(c, x, 1))] : 0;

            lista[k] = e;
        } else if (kk == E_GLOBAL) {
            WVal  v;
            void *g;
            const void *gt = w_externtype_as_globaltype(et);

            num_a_val(&v, w_valtype_kind(w_globaltype_content(gt)), exjs_a_numero(c, x));
            g = w_global_new(g_negozio, gt, &v);
            lista[k] = g ? w_global_as_extern(g) : 0;
        }
        if (!lista[k]) {
            snprintf(g_errore, sizeof(g_errore), "import %d non collegabile", k);
            w_importtype_vec_delete(&imp); free(lista);
            return exjs_numero(c, 0);
        }
    }
    t->n_ponti = np;
    t->js = c;
    ext.size = ext.num_elems = imp.num_elems; ext.data = lista; ext.size_of_elem = sizeof(void *); ext.lock = 0;
    t->i = w_instance_new(g_negozio, g_mod[m].m, &ext, &trap, 256 * 1024, 0);
    w_importtype_vec_delete(&imp);
    free(lista);
    if (!t->i) {
        if (trap) {
            WVec msg;

            w_trap_message(trap, &msg);
            snprintf(g_errore, sizeof(g_errore), "%.*s", (int)msg.size, (const char *)msg.data);
            w_byte_vec_delete(&msg);
            w_trap_delete(trap);
        } else errore("l'istanza non si crea");
        return exjs_numero(c, 0);
    }
    w_instance_exports(t->i, &t->esporti);
    t->ha_esporti = 1;
    return exjs_numero(c, 1);
}

static ExJsVal op_chiama(ExJsCtx *c, const ExJsVal *a, int n_arg)
{
    Ist        *t = ist_di(c, a[1]);
    void       *f = esporto(t, (int)exjs_a_numero(c, a[2]), E_FUNC), *ft, *trap;
    const WVec *pt, *rt;
    WVal        av[32], rv[8];
    WValVec     args, ris;
    size_t      i;

    if (!f) return exjs_stringa(c, "\001funzione sconosciuta", -1);
    ft = w_func_type(f);
    pt = w_functype_params(ft); rt = w_functype_results(ft);
    if (pt->num_elems > 32 || rt->num_elems > 8) { w_functype_delete(ft); return exjs_stringa(c, "\001troppi argomenti", -1); }
    for (i = 0; i < pt->num_elems; i++)
        num_a_val(&av[i], w_valtype_kind(((void **)pt->data)[i]), (int)i + 3 < n_arg ? exjs_a_numero(c, a[i + 3]) : 0);
    args.size = args.num_elems = pt->num_elems; args.data = av; args.size_of_elem = sizeof(WVal); args.lock = 0;
    ris.size = ris.num_elems = rt->num_elems; ris.data = rv; ris.size_of_elem = sizeof(WVal); ris.lock = 0;
    for (i = 0; i < rt->num_elems; i++) rv[i].kind = w_valtype_kind(((void **)rt->data)[i]);
    t->js = c;
    trap = w_func_call(f, &args, &ris);
    w_functype_delete(ft);
    if (trap) {
        WVec msg;
        char b[300];

        w_trap_message(trap, &msg);
        snprintf(b, sizeof(b), "\001%.*s", (int)(msg.size > 0 && ((char *)msg.data)[msg.size - 1] == 0 ? msg.size - 1 : msg.size), (const char *)msg.data);
        w_byte_vec_delete(&msg);
        w_trap_delete(trap);
        return exjs_stringa(c, b, -1);
    }
    if (ris.num_elems == 0) return exjs_indefinito();
    if (ris.num_elems == 1) return exjs_numero(c, val_a_num(&rv[0]));
    {
        ExJsVal v = exjs_vettore(c);

        for (i = 0; i < ris.num_elems; i++) exjs_indice_metti(c, v, (unsigned int)i, exjs_numero(c, val_a_num(&rv[i])));
        return v;
    }
}

static ExJsVal nat_wasm(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    const char *op;
    char        o[16];

    (void)questo; (void)dato;
    if (n_arg < 1 || !(op = exjs_a_stringa(c, a[0]))) return exjs_indefinito();
    snprintf(o, sizeof(o), "%s", op);
    if (!wamr_pronta()) { errore("WebAssembly non c'e' (libiwasm.so.2)"); return exjs_indefinito(); }
    if (!strcmp(o, "errore")) return exjs_stringa(c, g_errore, -1);
    if (!strcmp(o, "valida") || !strcmp(o, "modulo")) {
        unsigned int n;
        void        *b = n_arg > 1 ? exjs_buffer_dati(c, a[1], &n) : 0;
        WVec         v;
        int          i;

        if (!b) { errore("i byte del modulo non sono un BufferSource"); return o[0] == 'v' ? exjs_booleano(0) : exjs_numero(c, -1); }
        w_byte_vec_new(&v, n, (const char *)b);
        if (o[0] == 'v') { int ok = w_module_validate(g_negozio, &v); w_byte_vec_delete(&v); return exjs_booleano(ok); }
        for (i = 0; i < MOD_MAX && g_mod[i].m; i++) {}
        if (i == MOD_MAX) { w_byte_vec_delete(&v); errore("troppi moduli"); return exjs_numero(c, -1); }
        g_mod[i].m = w_module_new(g_negozio, &v);
        w_byte_vec_delete(&v);
        if (!g_mod[i].m) { errore("il modulo non e' valido"); return exjs_numero(c, -1); }
        g_mod[i].vista = g_vi;
        return exjs_numero(c, i);
    }
    if (!strcmp(o, "descrivi")) {
        static char j[65536];
        int         m = n_arg > 1 ? (int)exjs_a_numero(c, a[1]) : -1, k = 0;
        WVec        imp, esp;
        size_t      i;

        if (m < 0 || m >= MOD_MAX || !g_mod[m].m) return exjs_stringa(c, "", -1);
        w_module_imports(g_mod[m].m, &imp);
        w_module_exports(g_mod[m].m, &esp);
        k += snprintf(j + k, sizeof(j) - (size_t)k, "{\"imports\":[");
        for (i = 0; i < imp.num_elems && k < (int)sizeof(j) - 600; i++) {
            const void *it = ((void **)imp.data)[i];

            k += snprintf(j + k, sizeof(j) - (size_t)k, "%s{\"module\":", i ? "," : "");
            k = json_nome(j, k, sizeof(j), w_importtype_module(it));
            k += snprintf(j + k, sizeof(j) - (size_t)k, ",\"name\":");
            k = json_nome(j, k, sizeof(j), w_importtype_name(it));
            k = json_tipo_esterno(j, k, sizeof(j), w_importtype_type(it));
            k += snprintf(j + k, sizeof(j) - (size_t)k, "}");
        }
        k += snprintf(j + k, sizeof(j) - (size_t)k, "],\"exports\":[");
        for (i = 0; i < esp.num_elems && k < (int)sizeof(j) - 600; i++) {
            const void *et = ((void **)esp.data)[i];

            k += snprintf(j + k, sizeof(j) - (size_t)k, "%s{\"name\":", i ? "," : "");
            k = json_nome(j, k, sizeof(j), w_exporttype_name(et));
            k = json_tipo_esterno(j, k, sizeof(j), w_exporttype_type(et));
            k += snprintf(j + k, sizeof(j) - (size_t)k, "}");
        }
        k += snprintf(j + k, sizeof(j) - (size_t)k, "]}");
        w_importtype_vec_delete(&imp);
        w_exporttype_vec_delete(&esp);
        return exjs_stringa(c, j, k);
    }
    if (!strcmp(o, "prenota")) {
        int i;

        for (i = 0; i < IST_MAX && g_ist[i].usato; i++) {}
        if (i == IST_MAX) { errore("troppe istanze"); return exjs_numero(c, -1); }
        memset(&g_ist[i], 0, sizeof(g_ist[i]));
        g_ist[i].usato = 1; g_ist[i].vista = g_vi; g_ist[i].id = i; g_ist[i].js = c;
        return exjs_numero(c, i);
    }
    if (!strcmp(o, "istanza")) return op_istanza(c, a, n_arg);
    if (!strcmp(o, "chiama") && n_arg >= 3) return op_chiama(c, a, n_arg);
    if (!strcmp(o, "memoria") || !strcmp(o, "pagine") || !strcmp(o, "cresci")) {
        void *m = n_arg >= 3 ? esporto(ist_di(c, a[1]), (int)exjs_a_numero(c, a[2]), E_MEMORY) : 0;

        if (!m) return exjs_indefinito();
        if (o[0] == 'm') return exjs_buffer_esterno(c, w_memory_data(m), (unsigned int)w_memory_data_size(m));
        if (o[0] == 'p') return exjs_numero(c, w_memory_size(m));
        {
            unsigned int prima = w_memory_size(m);

            const WMemInterna *mi = (const WMemInterna *)m;
            void              *mm = mi->inst_comm_rt ? w_rt_get_memory(mi->inst_comm_rt, mi->memory_idx_rt) : 0;
            double             d = n_arg > 3 ? exjs_a_numero(c, a[3]) : 0;

            if (d == 0) return exjs_numero(c, prima);
            return exjs_numero(c, mm && d > 0 && w_mem_enlarge(mm, (unsigned long long)d) ? (double)prima : -1);
        }
    }
    if (!strcmp(o, "globale") && n_arg >= 3) {
        void *g = esporto(ist_di(c, a[1]), (int)exjs_a_numero(c, a[2]), E_GLOBAL);
        WVal  v;

        if (!g) return exjs_indefinito();
        w_global_get(g, &v);
        if (n_arg > 3) { num_a_val(&v, v.kind, exjs_a_numero(c, a[3])); w_global_set(g, &v); }
        return exjs_numero(c, val_a_num(&v));
    }
    if (!strcmp(o, "stacca") && n_arg > 1) { exjs_buffer_stacca(c, a[1]); return exjs_indefinito(); }
    return exjs_indefinito();
}

void wasm_registra(ExJsCtx *js)
{
    ExJsErrore err;
    ExJsVal    r;

    /* ! WAMR IS OPENED AT THE FIRST USE (wamr_pronta), not here: most
     * pages never touch WebAssembly */
    exjs_metti(js, exjs_globale(js), "__exos_wasm", exjs_nativa(js, nat_wasm, 0, "__exos_wasm"));
    memset(&err, 0, sizeof(err));
    if (!exjs_esegui(js, WASM_JS, (unsigned int)(sizeof(WASM_JS) - 1), &r, &err))
        printf("exbrowser: WebAssembly, riga %d: %s\n", err.riga, err.messaggio);
}

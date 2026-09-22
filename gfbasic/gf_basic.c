/*
 * gf_basic.c
 * ----------
 * Implementazione dell'interprete QBASIC (sottoinsieme) descritto in
 * gf_basic.h.
 *
 * SUPPORTATO:
 *   - DIM var AS INTEGER|DOUBLE|STRING [ (n) ]        (scalari e array 1D)
 *   - assegnazione: var = expr / arr(i) = expr
 *   - PRINT expr[; expr ...][;]                        (';' finale = niente newline)
 *   - INPUT ["prompt";] var
 *   - IF expr THEN stmt [ELSE stmt]                     (forma su riga singola)
 *   - IF expr THEN / [ELSE] / END IF                    (forma a blocco, no ELSEIF)
 *   - FOR var = a TO b [STEP s] ... NEXT [var]
 *   - WHILE expr ... WEND
 *   - DO [WHILE|UNTIL expr] ... LOOP [WHILE|UNTIL expr]
 *   - GOTO label / GOSUB label / RETURN / label:
 *   - END / STOP
 *   - REM e commenti con '
 *   - SYSRETURN_STRING(expr) / SYSRETURN_VALUE(expr)
 *   - chiamata a funzioni esterne C registrate: y = miafunzione(a,b,...)
 *   - operatori: + - * / \ MOD ^  = <> < > <= >=  AND OR NOT XOR  & (concat)
 *   - funzioni builtin: LEN, MID$, LEFT$, RIGHT$, INSTR, CHR$, ASC, VAL,
 *       STR$, UCASE$, LCASE$, LTRIM$, RTRIM$, SPACE$, STRING$,
 *       ABS, INT, FIX, SQR, SGN, SIN, COS, TAN, ATN, EXP, LOG, RND, TIMER
 *
 * NON supportato (fuori scope in questa prima versione, ma il codice e'
 * organizzato per rendere l'aggiunta semplice): grafica (SCREEN/LINE/
 * CIRCLE/PSET), PEEK/POKE, DATA/READ/RESTORE, TYPE...END TYPE, DEF FN,
 * SELECT CASE, ELSEIF, file I/O random-access, PRINT USING.
 *
 * Licenza: GNU GPL v2.
 */
#define _POSIX_C_SOURCE 200809L
#include "gf_basic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
#define GFB_MAX_STMTS   8192
#define GFB_MAX_VARS     512
#define GFB_MAX_FUNCS    128
#define GFB_MAX_LABELS   512
#define GFB_FORSTACK      64
#define GFB_GOSUBSTACK   128
#define GFB_IFSTACK       64
#define GFB_LOOPSTACK     64
#define GFB_ERRBUF       256

/* ------------------------------------------------------------------ */
/* Variabile BASIC (scalare o array 1D)                                */
typedef struct {
    char    name[64];
    GfbType type;
    int     is_array;
    int     arr_size;
    /* scalare: */
    long    i;
    double  d;
    char   *s;
    /* array: */
    long   *arr_i;
    double *arr_d;
    char  **arr_s;
} GfbVar;

typedef struct {
    char       name[64];
    GfExternFn fn;
    void      *user_data;
} GfbFuncEntry;

typedef struct {
    char text[512];  /* statement gia' "trimmato"                     */
    int  jmp1;       /* uso dipende dal tipo di statement              */
    int  jmp2;
} GfbStmt;

typedef struct {
    char name[64];
    int  idx;
} GfbLabel;

struct GF_BASIC {
    pthread_mutex_t lock;

    GfbStmt  stmts[GFB_MAX_STMTS];
    int      nstmts;

    GfbLabel labels[GFB_MAX_LABELS];
    int      nlabels;

    GfbVar   vars[GFB_MAX_VARS];
    int      nvars;

    GfbFuncEntry funcs[GFB_MAX_FUNCS];
    int          nfuncs;

    char    *ret_string;
    long     ret_value;

    char     error[GFB_ERRBUF];
    int      has_error;

    /* stato di runtime (valido durante gf_basic_run) */
    int running;
};

/* ==================================================================== */
/* Helper valori                                                        */
/* ==================================================================== */
GfbValue gfb_val_int(long v) { GfbValue r; r.type = GFB_INT; r.i = v; r.d = (double)v; r.s = NULL; return r; }
GfbValue gfb_val_dbl(double v){ GfbValue r; r.type = GFB_DBL; r.d = v; r.i = (long)v; r.s = NULL; return r; }
GfbValue gfb_val_str(const char *v){
    GfbValue r; r.type = GFB_STR; r.i = 0; r.d = 0;
    r.s = strdup(v ? v : "");
    return r;
}
long gfb_to_int_pub(const GfbValue *v){
    if (v->type == GFB_INT) return v->i;
    if (v->type == GFB_DBL) return (long)v->d;
    return atol(v->s ? v->s : "0");
}
double gfb_to_dbl_pub(const GfbValue *v){
    if (v->type == GFB_INT) return (double)v->i;
    if (v->type == GFB_DBL) return v->d;
    return atof(v->s ? v->s : "0");
}
const char *gfb_to_str_pub(const GfbValue *v){
    return (v->type == GFB_STR && v->s) ? v->s : "";
}

static void gfb_val_free(GfbValue *v){
    if (v->type == GFB_STR && v->s) { free(v->s); v->s = NULL; }
}
static double gfb_to_num(const GfbValue *v){
    if (v->type == GFB_INT) return (double)v->i;
    if (v->type == GFB_DBL) return v->d;
    return atof(v->s ? v->s : "0");
}
static long gfb_to_int(const GfbValue *v){
    if (v->type == GFB_INT) return v->i;
    if (v->type == GFB_DBL) return (long)v->d;
    return atol(v->s ? v->s : "0");
}
static char *gfb_to_str_alloc(const GfbValue *v){
    char buf[64];
    if (v->type == GFB_STR) return strdup(v->s ? v->s : "");
    if (v->type == GFB_INT) { snprintf(buf, sizeof buf, "%ld", v->i); return strdup(buf); }
    /* GFB_DBL: stampa "pulita" come QBASIC (senza zeri superflui) */
    snprintf(buf, sizeof buf, "%g", v->d);
    return strdup(buf);
}

/* ==================================================================== */
/* Utility stringhe                                                     */
/* ==================================================================== */
static char *xstrdup(const char *s){ char *r = strdup(s ? s : ""); return r; }

static char *str_trim(char *s){
    char *end;
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    if (*s == 0) return s;
    end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r')) { *end = 0; end--; }
    return s;
}

static int str_ieq(const char *a, const char *b){
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}
static int str_ieq_n(const char *a, const char *b, size_t n){
    for (size_t k = 0; k < n; k++) {
        if (tolower((unsigned char)a[k]) != tolower((unsigned char)b[k])) return 0;
    }
    return 1;
}
static void str_upper(char *dst, const char *src){
    while (*src) *dst++ = (char)toupper((unsigned char)*src++);
    *dst = 0;
}

/* ==================================================================== */
/* Gestione errori                                                      */
/* ==================================================================== */
static void gfb_seterr(GF_BASIC *obj, const char *fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(obj->error, sizeof obj->error, fmt, ap);
    va_end(ap);
    obj->has_error = 1;
}

/* ==================================================================== */
/* Variabili                                                            */
/* ==================================================================== */
static GfbVar *var_find(GF_BASIC *obj, const char *name){
    for (int k = 0; k < obj->nvars; k++)
        if (str_ieq(obj->vars[k].name, name)) return &obj->vars[k];
    return NULL;
}

static GfbVar *var_create(GF_BASIC *obj, const char *name, GfbType type, int arr_size){
    GfbVar *v = var_find(obj, name);
    if (v) return v; /* gia' dichiarata: idempotente */
    if (obj->nvars >= GFB_MAX_VARS) { gfb_seterr(obj, "troppe variabili"); return NULL; }
    v = &obj->vars[obj->nvars++];
    memset(v, 0, sizeof *v);
    strncpy(v->name, name, sizeof v->name - 1);
    v->type = type;
    if (arr_size > 0) {
        v->is_array = 1;
        v->arr_size = arr_size;
        if (type == GFB_INT) v->arr_i = calloc((size_t)arr_size + 1, sizeof(long));
        else if (type == GFB_DBL) v->arr_d = calloc((size_t)arr_size + 1, sizeof(double));
        else { v->arr_s = calloc((size_t)arr_size + 1, sizeof(char*));
               for (int j = 0; j <= arr_size; j++) v->arr_s[j] = xstrdup(""); }
    } else {
        if (type == GFB_STR) v->s = xstrdup("");
    }
    return v;
}

/* Variabile "implicita": se non dichiarata con DIM, viene creata al
 * primo uso come DOUBLE (numero) oppure STRING se il nome finisce
 * per '$', comportamento simile al QBASIC classico. */
static GfbVar *var_auto(GF_BASIC *obj, const char *name){
    GfbVar *v = var_find(obj, name);
    if (v) return v;
    size_t n = strlen(name);
    GfbType t = (n > 0 && name[n-1] == '$') ? GFB_STR : GFB_DBL;
    return var_create(obj, name, t, 0);
}

/* ==================================================================== */
/* Funzioni esterne                                                     */
/* ==================================================================== */
static GfbFuncEntry *func_find(GF_BASIC *obj, const char *name){
    for (int k = 0; k < obj->nfuncs; k++)
        if (str_ieq(obj->funcs[k].name, name)) return &obj->funcs[k];
    return NULL;
}

int gf_basic_add_function(GF_BASIC *obj, const char *name, GfExternFn fn, void *user_data){
    if (!obj || !name || !fn) return -1;
    if (func_find(obj, name)) { gfb_seterr(obj, "funzione '%s' gia' registrata", name); return -1; }
    if (obj->nfuncs >= GFB_MAX_FUNCS) { gfb_seterr(obj, "troppe funzioni registrate"); return -1; }
    GfbFuncEntry *e = &obj->funcs[obj->nfuncs++];
    strncpy(e->name, name, sizeof e->name - 1);
    e->fn = fn;
    e->user_data = user_data;
    return 0;
}

/* ==================================================================== */
/* ===============  LEXER / PARSER PER ESPRESSIONI  ===================*/
/* ==================================================================== */
typedef enum {
    TK_END, TK_NUM, TK_STR, TK_IDENT, TK_OP, TK_LP, TK_RP, TK_COMMA
} TokKind;

typedef struct {
    TokKind kind;
    char    text[256];
    double  num;
} Token;

typedef struct {
    const char *p;
    Token cur;
    GF_BASIC *obj;
} Lexer;

static void lex_next(Lexer *lx);

static void lex_init(Lexer *lx, const char *s, GF_BASIC *obj){
    lx->p = s;
    lx->obj = obj;
    lex_next(lx);
}

static int is_ident_start(char c){ return isalpha((unsigned char)c) || c == '_'; }
static int is_ident_char(char c){ return isalnum((unsigned char)c) || c == '_' || c == '$'; }

static void lex_next(Lexer *lx){
    while (*lx->p == ' ' || *lx->p == '\t') lx->p++;
    if (*lx->p == 0) { lx->cur.kind = TK_END; lx->cur.text[0] = 0; return; }

    char c = *lx->p;

    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)lx->p[1]))) {
        const char *start = lx->p;
        while (isdigit((unsigned char)*lx->p) || *lx->p == '.') lx->p++;
        size_t len = (size_t)(lx->p - start);
        if (len >= sizeof lx->cur.text) len = sizeof lx->cur.text - 1;
        memcpy(lx->cur.text, start, len); lx->cur.text[len] = 0;
        lx->cur.kind = TK_NUM;
        lx->cur.num = atof(lx->cur.text);
        return;
    }
    if (c == '"') {
        lx->p++;
        char buf[256]; int bi = 0;
        while (*lx->p && *lx->p != '"') {
            if (bi < (int)sizeof buf - 1) buf[bi++] = *lx->p;
            lx->p++;
        }
        if (*lx->p == '"') lx->p++;
        buf[bi] = 0;
        strncpy(lx->cur.text, buf, sizeof lx->cur.text - 1);
        lx->cur.text[sizeof lx->cur.text - 1] = 0;
        lx->cur.kind = TK_STR;
        return;
    }
    if (is_ident_start(c)) {
        const char *start = lx->p;
        while (is_ident_char(*lx->p)) lx->p++;
        size_t len = (size_t)(lx->p - start);
        if (len >= sizeof lx->cur.text) len = sizeof lx->cur.text - 1;
        memcpy(lx->cur.text, start, len); lx->cur.text[len] = 0;
        lx->cur.kind = TK_IDENT;
        return;
    }
    if (c == '(') { lx->p++; lx->cur.kind = TK_LP; strcpy(lx->cur.text, "("); return; }
    if (c == ')') { lx->p++; lx->cur.kind = TK_RP; strcpy(lx->cur.text, ")"); return; }
    if (c == ',') { lx->p++; lx->cur.kind = TK_COMMA; strcpy(lx->cur.text, ","); return; }

    /* operatori (anche a due caratteri) */
    if ((c == '<' && lx->p[1] == '>') || (c == '<' && lx->p[1] == '=') || (c == '>' && lx->p[1] == '=')) {
        lx->cur.text[0] = lx->p[0]; lx->cur.text[1] = lx->p[1]; lx->cur.text[2] = 0;
        lx->p += 2; lx->cur.kind = TK_OP; return;
    }
    lx->cur.text[0] = c; lx->cur.text[1] = 0;
    lx->p++;
    lx->cur.kind = TK_OP;
}

/* ------------------------------------------------------------------ */
/* Parser ricorsivo-discendente delle espressioni.                     */
/* Precedenza (dalla piu' bassa alla piu' alta):                       */
/*   OR > AND > NOT > confronto > concat(&) > + - > * / \ MOD > unario > ^ */
/* ------------------------------------------------------------------ */
static GfbValue parse_or(Lexer *lx);

static GfbValue call_builtin(GF_BASIC *obj, const char *fname, GfbValue *args, int nargs, int *handled);
static GfbValue call_extern(GF_BASIC *obj, const char *fname, GfbValue *args, int nargs, int *found);

static GfbValue parse_primary(Lexer *lx){
    GF_BASIC *obj = lx->obj;
    if (lx->cur.kind == TK_NUM) {
        double n = lx->cur.num;
        lex_next(lx);
        if (n == (long)n) return gfb_val_int((long)n);
        return gfb_val_dbl(n);
    }
    if (lx->cur.kind == TK_STR) {
        GfbValue v = gfb_val_str(lx->cur.text);
        lex_next(lx);
        return v;
    }
    if (lx->cur.kind == TK_LP) {
        lex_next(lx);
        GfbValue v = parse_or(lx);
        if (lx->cur.kind == TK_RP) lex_next(lx);
        return v;
    }
    if (lx->cur.kind == TK_OP && (lx->cur.text[0] == '-' )) {
        lex_next(lx);
        GfbValue v = parse_primary(lx);
        double n = gfb_to_num(&v);
        gfb_val_free(&v);
        return gfb_val_dbl(-n);
    }
    if (lx->cur.kind == TK_IDENT) {
        char name[256];
        strncpy(name, lx->cur.text, sizeof name - 1); name[sizeof name -1]=0;
        lex_next(lx);

        /* raccogli eventuali argomenti tra parentesi (funzione o indice array) */
        GfbValue args[16]; int nargs = 0;
        int has_parens = (lx->cur.kind == TK_LP);
        if (has_parens) {
            lex_next(lx);
            if (lx->cur.kind != TK_RP) {
                for (;;) {
                    if (nargs < 16) args[nargs++] = parse_or(lx);
                    else parse_or(lx);
                    if (lx->cur.kind == TK_COMMA) { lex_next(lx); continue; }
                    break;
                }
            }
            if (lx->cur.kind == TK_RP) lex_next(lx);
        }

        /* 1) variabile array conosciuta -> indicizzazione */
        GfbVar *v = var_find(obj, name);
        if (v && v->is_array && has_parens) {
            long idx = nargs > 0 ? gfb_to_int(&args[0]) : 0;
            for (int a = 0; a < nargs; a++) gfb_val_free(&args[a]);
            if (idx < 0 || idx > v->arr_size) { gfb_seterr(obj, "indice fuori range per '%s'", name); return gfb_val_int(0); }
            if (v->type == GFB_INT) return gfb_val_int(v->arr_i[idx]);
            if (v->type == GFB_DBL) return gfb_val_dbl(v->arr_d[idx]);
            return gfb_val_str(v->arr_s[idx]);
        }
        /* 2) variabile scalare -> valore diretto (ignora eventuali parentesi vuote) */
        if (v && !has_parens) {
            if (v->type == GFB_INT) return gfb_val_int(v->i);
            if (v->type == GFB_DBL) return gfb_val_dbl(v->d);
            return gfb_val_str(v->s);
        }
        if (v && !v->is_array && has_parens) {
            for (int a = 0; a < nargs; a++) gfb_val_free(&args[a]);
            if (v->type == GFB_INT) return gfb_val_int(v->i);
            if (v->type == GFB_DBL) return gfb_val_dbl(v->d);
            return gfb_val_str(v->s);
        }

        /* 3) funzione builtin */
        int handled = 0;
        GfbValue r = call_builtin(obj, name, args, nargs, &handled);
        if (handled) { for (int a = 0; a < nargs; a++) gfb_val_free(&args[a]); return r; }

        /* 4) funzione esterna registrata dall'host C */
        int found = 0;
        r = call_extern(obj, name, args, nargs, &found);
        if (found) { for (int a = 0; a < nargs; a++) gfb_val_free(&args[a]); return r; }

        /* 5) variabile non dichiarata -> creala implicitamente (0 / "") */
        for (int a = 0; a < nargs; a++) gfb_val_free(&args[a]);
        GfbVar *av = var_auto(obj, name);
        if (!av) return gfb_val_int(0);
        if (av->type == GFB_INT) return gfb_val_int(av->i);
        if (av->type == GFB_DBL) return gfb_val_dbl(av->d);
        return gfb_val_str(av->s);
    }
    /* token inatteso */
    lex_next(lx);
    return gfb_val_int(0);
}

static GfbValue parse_pow(Lexer *lx){
    GfbValue base = parse_primary(lx);
    while (lx->cur.kind == TK_OP && lx->cur.text[0] == '^') {
        lex_next(lx);
        GfbValue exp = parse_primary(lx);
        double r = pow(gfb_to_num(&base), gfb_to_num(&exp));
        gfb_val_free(&base); gfb_val_free(&exp);
        base = gfb_val_dbl(r);
    }
    return base;
}

static GfbValue parse_mul(Lexer *lx){
    GfbValue l = parse_pow(lx);
    for (;;) {
        if (lx->cur.kind == TK_OP && (lx->cur.text[0]=='*' || lx->cur.text[0]=='/' || lx->cur.text[0]=='\\')) {
            char op = lx->cur.text[0];
            lex_next(lx);
            GfbValue r = parse_pow(lx);
            double a = gfb_to_num(&l), b = gfb_to_num(&r);
            gfb_val_free(&l); gfb_val_free(&r);
            if (op == '*') l = gfb_val_dbl(a*b);
            else if (op == '/') l = gfb_val_dbl(b != 0 ? a/b : 0);
            else l = gfb_val_int(b != 0 ? (long)a / (long)b : 0); /* divisione intera \ */
            continue;
        }
        if (lx->cur.kind == TK_IDENT && str_ieq(lx->cur.text, "MOD")) {
            lex_next(lx);
            GfbValue r = parse_pow(lx);
            long a = gfb_to_int(&l), b = gfb_to_int(&r);
            gfb_val_free(&l); gfb_val_free(&r);
            l = gfb_val_int(b != 0 ? a % b : 0);
            continue;
        }
        break;
    }
    return l;
}

static GfbValue parse_add(Lexer *lx){
    GfbValue l = parse_mul(lx);
    for (;;) {
        if (lx->cur.kind == TK_OP && (lx->cur.text[0]=='+' || lx->cur.text[0]=='-')) {
            char op = lx->cur.text[0];
            lex_next(lx);
            GfbValue r = parse_mul(lx);
            if (op == '+' && (l.type == GFB_STR || r.type == GFB_STR)) {
                char *a = gfb_to_str_alloc(&l), *b = gfb_to_str_alloc(&r);
                char *cat = malloc(strlen(a)+strlen(b)+1);
                strcpy(cat, a); strcat(cat, b);
                gfb_val_free(&l); gfb_val_free(&r);
                l.type = GFB_STR; l.s = cat; free(a); free(b);
            } else {
                double a = gfb_to_num(&l), b = gfb_to_num(&r);
                gfb_val_free(&l); gfb_val_free(&r);
                l = gfb_val_dbl(op == '+' ? a+b : a-b);
            }
            continue;
        }
        break;
    }
    return l;
}

static GfbValue parse_concat(Lexer *lx){
    GfbValue l = parse_add(lx);
    while (lx->cur.kind == TK_OP && lx->cur.text[0] == '&') {
        lex_next(lx);
        GfbValue r = parse_add(lx);
        char *a = gfb_to_str_alloc(&l), *b = gfb_to_str_alloc(&r);
        char *cat = malloc(strlen(a)+strlen(b)+1);
        strcpy(cat, a); strcat(cat, b);
        gfb_val_free(&l); gfb_val_free(&r);
        l.type = GFB_STR; l.s = cat; free(a); free(b);
    }
    return l;
}

static GfbValue parse_compare(Lexer *lx){
    GfbValue l = parse_concat(lx);
    if (lx->cur.kind == TK_OP &&
        (str_ieq(lx->cur.text,"=") || str_ieq(lx->cur.text,"<>") ||
         str_ieq(lx->cur.text,"<") || str_ieq(lx->cur.text,">") ||
         str_ieq(lx->cur.text,"<=") || str_ieq(lx->cur.text,">="))) {
        char op[3]; strncpy(op, lx->cur.text, 3);
        lex_next(lx);
        GfbValue r = parse_concat(lx);
        int res;
        if (l.type == GFB_STR || r.type == GFB_STR) {
            char *a = gfb_to_str_alloc(&l), *b = gfb_to_str_alloc(&r);
            int c = strcmp(a, b);
            free(a); free(b);
            if (!strcmp(op,"=")) res = (c==0);
            else if (!strcmp(op,"<>")) res = (c!=0);
            else if (!strcmp(op,"<")) res = (c<0);
            else if (!strcmp(op,">")) res = (c>0);
            else if (!strcmp(op,"<=")) res = (c<=0);
            else res = (c>=0);
        } else {
            double a = gfb_to_num(&l), b = gfb_to_num(&r);
            if (!strcmp(op,"=")) res = (a==b);
            else if (!strcmp(op,"<>")) res = (a!=b);
            else if (!strcmp(op,"<")) res = (a<b);
            else if (!strcmp(op,">")) res = (a>b);
            else if (!strcmp(op,"<=")) res = (a<=b);
            else res = (a>=b);
        }
        gfb_val_free(&l); gfb_val_free(&r);
        return gfb_val_int(res ? -1 : 0); /* QBASIC: TRUE = -1 */
    }
    return l;
}

static GfbValue parse_not(Lexer *lx){
    if (lx->cur.kind == TK_IDENT && str_ieq(lx->cur.text, "NOT")) {
        lex_next(lx);
        GfbValue v = parse_not(lx);
        long r = gfb_to_int(&v);
        gfb_val_free(&v);
        return gfb_val_int(~r);
    }
    return parse_compare(lx);
}

static GfbValue parse_and(Lexer *lx){
    GfbValue l = parse_not(lx);
    while (lx->cur.kind == TK_IDENT && str_ieq(lx->cur.text, "AND")) {
        lex_next(lx);
        GfbValue r = parse_not(lx);
        long res = (gfb_to_int(&l) != 0) && (gfb_to_int(&r) != 0);
        gfb_val_free(&l); gfb_val_free(&r);
        l = gfb_val_int(res ? -1 : 0);
    }
    return l;
}

static GfbValue parse_or(Lexer *lx){
    GfbValue l = parse_and(lx);
    for (;;) {
        if (lx->cur.kind == TK_IDENT && str_ieq(lx->cur.text, "OR")) {
            lex_next(lx);
            GfbValue r = parse_and(lx);
            long res = (gfb_to_int(&l) != 0) || (gfb_to_int(&r) != 0);
            gfb_val_free(&l); gfb_val_free(&r);
            l = gfb_val_int(res ? -1 : 0);
            continue;
        }
        if (lx->cur.kind == TK_IDENT && str_ieq(lx->cur.text, "XOR")) {
            lex_next(lx);
            GfbValue r = parse_and(lx);
            long res = (gfb_to_int(&l) != 0) ^ (gfb_to_int(&r) != 0);
            gfb_val_free(&l); gfb_val_free(&r);
            l = gfb_val_int(res ? -1 : 0);
            continue;
        }
        break;
    }
    return l;
}

/* Valuta un'espressione contenuta nella stringa 'expr' */
static GfbValue eval_expr(GF_BASIC *obj, const char *expr){
    Lexer lx;
    lex_init(&lx, expr, obj);
    return parse_or(&lx);
}

/* ==================================================================== */
/* Funzioni builtin (stringa / matematiche)                             */
/* ==================================================================== */
static GfbValue call_builtin(GF_BASIC *obj, const char *fname, GfbValue *a, int n, int *handled){
    (void)obj;
    *handled = 1;
    char up[64]; str_upper(up, fname);

    if (!strcmp(up,"LEN"))    { char *s=gfb_to_str_alloc(&a[0]); long r=(long)strlen(s); free(s); return gfb_val_int(r); }
    if (!strcmp(up,"MID$") || !strcmp(up,"MID")) {
        char *s = gfb_to_str_alloc(&a[0]);
        long start = n>1 ? gfb_to_int(&a[1]) : 1;
        long len   = n>2 ? gfb_to_int(&a[2]) : (long)strlen(s);
        long sl = (long)strlen(s);
        if (start < 1) start = 1;
        if (start > sl) { free(s); return gfb_val_str(""); }
        if (len < 0) len = 0;
        if (start - 1 + len > sl) len = sl - (start - 1);
        char *r = malloc((size_t)len + 1);
        memcpy(r, s + start - 1, (size_t)len); r[len] = 0;
        free(s);
        GfbValue v; v.type=GFB_STR; v.s=r; v.i=0; v.d=0;
        return v;
    }
    if (!strcmp(up,"LEFT$") || !strcmp(up,"LEFT")) {
        char *s = gfb_to_str_alloc(&a[0]); long ln = n>1?gfb_to_int(&a[1]):0;
        long sl=(long)strlen(s); if (ln<0) ln=0; if (ln>sl) ln=sl;
        char *r=malloc((size_t)ln+1); memcpy(r,s,(size_t)ln); r[ln]=0; free(s);
        GfbValue v; v.type=GFB_STR; v.s=r; v.i=0; v.d=0; return v;
    }
    if (!strcmp(up,"RIGHT$") || !strcmp(up,"RIGHT")) {
        char *s = gfb_to_str_alloc(&a[0]); long ln = n>1?gfb_to_int(&a[1]):0;
        long sl=(long)strlen(s); if (ln<0) ln=0; if (ln>sl) ln=sl;
        char *r=malloc((size_t)ln+1); memcpy(r,s+sl-ln,(size_t)ln); r[ln]=0; free(s);
        GfbValue v; v.type=GFB_STR; v.s=r; v.i=0; v.d=0; return v;
    }
    if (!strcmp(up,"INSTR")) {
        /* INSTR(s, sub) oppure INSTR(start, s, sub) */
        char *s, *sub; long start = 1;
        if (n >= 3) { start = gfb_to_int(&a[0]); s = gfb_to_str_alloc(&a[1]); sub = gfb_to_str_alloc(&a[2]); }
        else { s = gfb_to_str_alloc(&a[0]); sub = gfb_to_str_alloc(&a[1]); }
        long sl=(long)strlen(s);
        if (start<1) start=1;
        long res = 0;
        if (start <= sl+1) {
            char *found = strstr(s + start - 1, sub);
            if (found) res = (long)(found - s) + 1;
        }
        free(s); free(sub);
        return gfb_val_int(res);
    }
    if (!strcmp(up,"CHR$") || !strcmp(up,"CHR")) {
        char buf[2]; buf[0] = (char)gfb_to_int(&a[0]); buf[1]=0;
        return gfb_val_str(buf);
    }
    if (!strcmp(up,"ASC")) {
        char *s = gfb_to_str_alloc(&a[0]);
        long r = s[0] ? (unsigned char)s[0] : 0;
        free(s); return gfb_val_int(r);
    }
    if (!strcmp(up,"VAL")) {
        char *s = gfb_to_str_alloc(&a[0]);
        double r = atof(s); free(s);
        return gfb_val_dbl(r);
    }
    if (!strcmp(up,"STR$") || !strcmp(up,"STR")) {
        char *r = gfb_to_str_alloc(&a[0]);
        GfbValue v; v.type=GFB_STR; v.s=r; v.i=0; v.d=0; return v;
    }
    if (!strcmp(up,"UCASE$") || !strcmp(up,"UCASE")) {
        char *s = gfb_to_str_alloc(&a[0]); char *r = malloc(strlen(s)+1);
        for (size_t k=0;s[k];k++) r[k]=(char)toupper((unsigned char)s[k]); r[strlen(s)]=0;
        free(s); GfbValue v; v.type=GFB_STR; v.s=r; v.i=0; v.d=0; return v;
    }
    if (!strcmp(up,"LCASE$") || !strcmp(up,"LCASE")) {
        char *s = gfb_to_str_alloc(&a[0]); char *r = malloc(strlen(s)+1);
        for (size_t k=0;s[k];k++) r[k]=(char)tolower((unsigned char)s[k]); r[strlen(s)]=0;
        free(s); GfbValue v; v.type=GFB_STR; v.s=r; v.i=0; v.d=0; return v;
    }
    if (!strcmp(up,"LTRIM$") || !strcmp(up,"LTRIM")) {
        char *s = gfb_to_str_alloc(&a[0]); char *p=s; while(*p==' ')p++;
        GfbValue v = gfb_val_str(p); free(s); return v;
    }
    if (!strcmp(up,"RTRIM$") || !strcmp(up,"RTRIM")) {
        char *s = gfb_to_str_alloc(&a[0]); size_t l=strlen(s);
        while (l>0 && s[l-1]==' ') { s[l-1]=0; l--; }
        GfbValue v = gfb_val_str(s); free(s); return v;
    }
    if (!strcmp(up,"SPACE$") || !strcmp(up,"SPACE")) {
        long cnt = gfb_to_int(&a[0]); if (cnt<0) cnt=0;
        char *r = malloc((size_t)cnt+1); memset(r,' ',(size_t)cnt); r[cnt]=0;
        GfbValue v; v.type=GFB_STR; v.s=r; v.i=0; v.d=0; return v;
    }
    if (!strcmp(up,"STRING$") || !strcmp(up,"STRING")) {
        long cnt = gfb_to_int(&a[0]); if (cnt<0) cnt=0;
        char ch = ' ';
        if (n>1) { if (a[1].type==GFB_STR && a[1].s && a[1].s[0]) ch=a[1].s[0]; else ch=(char)gfb_to_int(&a[1]); }
        char *r = malloc((size_t)cnt+1); memset(r,ch,(size_t)cnt); r[cnt]=0;
        GfbValue v; v.type=GFB_STR; v.s=r; v.i=0; v.d=0; return v;
    }
    if (!strcmp(up,"ABS")) { double x=gfb_to_num(&a[0]); return gfb_val_dbl(fabs(x)); }
    if (!strcmp(up,"INT")) { double x=gfb_to_num(&a[0]); return gfb_val_int((long)floor(x)); }
    if (!strcmp(up,"FIX")) { double x=gfb_to_num(&a[0]); return gfb_val_int((long)x); }
    if (!strcmp(up,"SGN")) { double x=gfb_to_num(&a[0]); return gfb_val_int(x>0?1:(x<0?-1:0)); }
    if (!strcmp(up,"SQR")) { double x=gfb_to_num(&a[0]); return gfb_val_dbl(x>=0?sqrt(x):0); }
    if (!strcmp(up,"SIN")) { return gfb_val_dbl(sin(gfb_to_num(&a[0]))); }
    if (!strcmp(up,"COS")) { return gfb_val_dbl(cos(gfb_to_num(&a[0]))); }
    if (!strcmp(up,"TAN")) { return gfb_val_dbl(tan(gfb_to_num(&a[0]))); }
    if (!strcmp(up,"ATN")) { return gfb_val_dbl(atan(gfb_to_num(&a[0]))); }
    if (!strcmp(up,"EXP")) { return gfb_val_dbl(exp(gfb_to_num(&a[0]))); }
    if (!strcmp(up,"LOG")) { double x=gfb_to_num(&a[0]); return gfb_val_dbl(x>0?log(x):0); }
    if (!strcmp(up,"RND")) { return gfb_val_dbl((double)rand() / ((double)RAND_MAX + 1.0)); }
    if (!strcmp(up,"TIMER")) { return gfb_val_dbl((double)time(NULL)); }

    *handled = 0;
    return gfb_val_int(0);
}

static GfbValue call_extern(GF_BASIC *obj, const char *fname, GfbValue *args, int nargs, int *found){
    GfbFuncEntry *e = func_find(obj, fname);
    if (!e) { *found = 0; return gfb_val_int(0); }
    *found = 1;
    return e->fn(nargs, args, e->user_data);
}

/* ==================================================================== */
/* Assegnazione di un GfbValue a una variabile/array, con coercizione   */
/* ==================================================================== */
static void var_assign_scalar(GfbVar *v, GfbValue val){
    if (v->type == GFB_INT) v->i = gfb_to_int(&val);
    else if (v->type == GFB_DBL) v->d = gfb_to_num(&val);
    else { free(v->s); v->s = gfb_to_str_alloc(&val); }
}
static void var_assign_array(GfbVar *v, long idx, GfbValue val){
    if (idx < 0 || idx > v->arr_size) return;
    if (v->type == GFB_INT) v->arr_i[idx] = gfb_to_int(&val);
    else if (v->type == GFB_DBL) v->arr_d[idx] = gfb_to_num(&val);
    else { free(v->arr_s[idx]); v->arr_s[idx] = gfb_to_str_alloc(&val); }
}

/* ==================================================================== */
/* PREPROCESSING: spezza il sorgente in statement, risolve label e      */
/* i "salti" per IF/FOR/WHILE/DO                                        */
/* ==================================================================== */
typedef struct { int kind; int idx; int idx2; } StackEnt; /* kind: 0=IF,1=FOR,2=WHILE,3=DO; idx2: solo per IF, indice dell'ELSE (-1 se assente) */

static int gfb_preprocess(GF_BASIC *obj, const char *code){
    obj->nstmts = 0;
    obj->nlabels = 0;

    char *buf = xstrdup(code);
    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);

    while (line) {
        char linecopy[2048];
        strncpy(linecopy, line, sizeof linecopy - 1);
        linecopy[sizeof linecopy - 1] = 0;

        /* rimuovi commento ' (fuori da stringhe) */
        {
            int instr = 0;
            for (char *p = linecopy; *p; p++) {
                if (*p == '"') instr = !instr;
                else if (*p == '\'' && !instr) { *p = 0; break; }
            }
        }

        char *trimmed = str_trim(linecopy);
        if (*trimmed == 0) { line = strtok_r(NULL, "\n", &saveptr); continue; }

        /* individua le label: riga del tipo "IDENT:" (senza altro),
           PRIMA di spezzare per ':' (lo split scrive '\0' sui ':' e
           corromperebbe questo controllo se fatto dopo). */
        {
            size_t tl = strlen(trimmed);
            if (tl > 1 && trimmed[tl-1] == ':') {
                int instr = 0, hascolon_inside = 0;
                for (size_t k = 0; k < tl - 1; k++) {
                    if (trimmed[k]=='"') instr = !instr;
                    if (trimmed[k]==':' && !instr) hascolon_inside = 1;
                }
                if (!hascolon_inside) {
                    char labelname[64];
                    size_t nl = tl-1 < sizeof labelname -1 ? tl-1 : sizeof labelname -1;
                    strncpy(labelname, trimmed, nl);
                    labelname[nl] = 0;
                    if (obj->nlabels < GFB_MAX_LABELS) {
                        strncpy(obj->labels[obj->nlabels].name, labelname, 63);
                        obj->labels[obj->nlabels].idx = obj->nstmts; /* punta al prossimo stmt */
                        obj->nlabels++;
                    }
                    line = strtok_r(NULL, "\n", &saveptr);
                    continue;
                }
            }
        }

        /* dividi per ':' fuori dalle stringhe, in piu' statement */
        char *segs[32]; int nseg = 0;
        {
            int instr = 0; char *start = trimmed; char *p = trimmed;
            for (;; p++) {
                if (*p == '"') instr = !instr;
                if ((*p == ':' && !instr) || *p == 0) {
                    char saved = *p; *p = 0;
                    char *seg = str_trim(start);
                    if (*seg && nseg < 32) segs[nseg++] = seg;
                    if (saved == 0) break;
                    start = p + 1;
                }
            }
        }

        for (int s = 0; s < nseg; s++) {
            if (obj->nstmts >= GFB_MAX_STMTS) { free(buf); gfb_seterr(obj,"programma troppo lungo"); return -1; }
            GfbStmt *st = &obj->stmts[obj->nstmts++];
            strncpy(st->text, segs[s], sizeof st->text - 1);
            st->text[sizeof st->text -1] = 0;
            st->jmp1 = -1; st->jmp2 = -1;
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(buf);

    /* seconda passata: risolvi blocchi IF/END IF, FOR/NEXT, WHILE/WEND, DO/LOOP */
    StackEnt stack[128]; int sp = 0;
    for (int k = 0; k < obj->nstmts; k++) {
        char *t = obj->stmts[k].text;
        char up[512]; str_upper(up, t);

        if (str_ieq_n(up, "IF ", 3)) {
            /* blocco IF solo se la riga termina (case-insensitive) con "THEN"
               senza nient'altro dopo */
            char *thenpos = strstr(up, "THEN");
            if (thenpos) {
                char *after = thenpos + 4;
                while (*after == ' ') after++;
                if (*after == 0) {
                    if (sp < 128) stack[sp++] = (StackEnt){0, k, -1};
                }
            }
            continue;
        }
        if (str_ieq(up, "ELSE")) {
            if (sp > 0 && stack[sp-1].kind == 0) {
                obj->stmts[stack[sp-1].idx].jmp1 = k + 1; /* IF falso -> dopo ELSE (inizio ramo false) */
                stack[sp-1].idx2 = k; /* ricorda posizione ELSE per fixup a END IF */
            }
            continue;
        }
        if (str_ieq(up, "END IF") || str_ieq(up, "ENDIF")) {
            if (sp > 0 && stack[sp-1].kind == 0) {
                StackEnt e = stack[--sp];
                if (e.idx2 == -1) {
                    /* niente ELSE: se condizione falsa, salta direttamente dopo END IF */
                    obj->stmts[e.idx].jmp1 = k + 1;
                } else {
                    /* con ELSE: quando il ramo vero finisce e "cade" sulla riga
                       ELSE, deve saltare dopo END IF invece di eseguire il ramo falso */
                    obj->stmts[e.idx2].jmp2 = k + 1;
                }
            }
            continue;
        }
        if (str_ieq_n(up, "FOR ", 4)) {
            if (sp < 128) stack[sp++] = (StackEnt){1, k, -1};
            continue;
        }
        if (str_ieq_n(up, "NEXT", 4)) {
            if (sp > 0 && stack[sp-1].kind == 1) {
                int idx = stack[--sp].idx;
                obj->stmts[idx].jmp2 = k;      /* FOR sa dove sta il suo NEXT */
                obj->stmts[k].jmp1 = idx;      /* NEXT sa dove sta il suo FOR */
            }
            continue;
        }
        if (str_ieq_n(up, "WHILE ", 6) || str_ieq(up, "WHILE")) {
            if (sp < 128) stack[sp++] = (StackEnt){2, k, -1};
            continue;
        }
        if (str_ieq(up, "WEND")) {
            if (sp > 0 && stack[sp-1].kind == 2) {
                int idx = stack[--sp].idx;
                obj->stmts[idx].jmp1 = k;   /* WHILE: se falsa -> dopo WEND (=k+1 gestito a runtime) */
                obj->stmts[k].jmp1 = idx;   /* WEND -> torna a WHILE */
            }
            continue;
        }
        if (str_ieq_n(up, "DO", 2) && (up[2]==0 || up[2]==' ')) {
            if (sp < 128) stack[sp++] = (StackEnt){3, k, -1};
            continue;
        }
        if (str_ieq_n(up, "LOOP", 4)) {
            if (sp > 0 && stack[sp-1].kind == 3) {
                int idx = stack[--sp].idx;
                obj->stmts[idx].jmp1 = k;  /* DO -> ricorda il suo LOOP (per DO WHILE in testa) */
                obj->stmts[k].jmp1 = idx;  /* LOOP -> torna a DO */
            }
            continue;
        }
    }
    if (sp != 0) {
        gfb_seterr(obj, "blocco non chiuso (IF/FOR/WHILE/DO senza terminatore corrispondente)");
        return -1;
    }
    return 0;
}

/* ==================================================================== */
/* Ricerca label                                                        */
/* ==================================================================== */
static int label_find(GF_BASIC *obj, const char *name){
    for (int k = 0; k < obj->nlabels; k++)
        if (str_ieq(obj->labels[k].name, name)) return obj->labels[k].idx;
    return -1;
}

/* ==================================================================== */
/* Parsing statement DIM                                                */
/* ==================================================================== */
static void handle_dim(GF_BASIC *obj, const char *rest){
    /* rest: "nome [(size)] AS TIPO" oppure "nome AS EXTERNFUNCTION(fname)" */
    char name[64] = {0};
    const char *p = rest;
    while (*p == ' ') p++;
    int i = 0;
    while (is_ident_char(*p) && i < 63) name[i++] = *p++;
    name[i] = 0;

    int arrsize = 0;
    while (*p == ' ') p++;
    if (*p == '(') {
        p++;
        char numbuf[32]; int ni=0;
        while (*p && *p != ')' && ni < 31) numbuf[ni++] = *p++;
        numbuf[ni] = 0;
        if (*p == ')') p++;
        GfbValue v = eval_expr(obj, numbuf);
        arrsize = (int)gfb_to_int(&v);
        gfb_val_free(&v);
    }
    while (*p == ' ') p++;
    if (str_ieq_n(p, "AS", 2)) p += 2;
    while (*p == ' ') p++;

    char typebuf[64]; int ti=0;
    while (is_ident_char(*p) && ti < 63) typebuf[ti++] = *p++;
    typebuf[ti] = 0;

    if (str_ieq(typebuf, "INTEGER") || str_ieq(typebuf, "LONG")) {
        var_create(obj, name, GFB_INT, arrsize);
    } else if (str_ieq(typebuf, "DOUBLE") || str_ieq(typebuf, "SINGLE")) {
        var_create(obj, name, GFB_DBL, arrsize);
    } else if (str_ieq(typebuf, "STRING")) {
        var_create(obj, name, GFB_STR, arrsize);
    } else if (str_ieq(typebuf, "EXTERNFUNCTION")) {
        /* DIM f AS EXTERNFUNCTION(nomefunzionecregistrata)
           puramente dichiarativo: la funzione va comunque registrata via
           gf_basic_add_function(); qui non serve fare nulla, e' solo
           un modo per rendere il sorgente auto-documentante. */
    } else {
        /* tipo sconosciuto: default double */
        var_create(obj, name, GFB_DBL, arrsize);
    }
}

/* ==================================================================== */
/* Assegnazione "nome[(idx)] = expr"                                    */
/* ritorna 1 se la riga era un'assegnazione e l'ha gestita               */
/* ==================================================================== */
static int handle_assignment(GF_BASIC *obj, const char *stmt){
    const char *eq = NULL; int instr = 0;
    for (const char *p = stmt; *p; p++) {
        if (*p == '"') instr = !instr;
        else if (*p == '=' && !instr) { eq = p; break; }
    }
    if (!eq) return 0;

    char lhs[128]; size_t ll = (size_t)(eq - stmt);
    if (ll >= sizeof lhs) ll = sizeof lhs - 1;
    memcpy(lhs, stmt, ll); lhs[ll] = 0;
    char *lhs_t = str_trim(lhs);

    /* deve essere IDENT oppure IDENT(expr) */
    if (!is_ident_start(lhs_t[0])) return 0;
    char name[64]; int i=0; const char *p = lhs_t;
    while (is_ident_char(*p) && i < 63) name[i++] = *p++;
    name[i] = 0;
    while (*p == ' ') p++;

    long idx = -1;
    if (*p == '(') {
        p++;
        char idxbuf[128]; int ni=0;
        int d = 1;
        while (*p && d > 0 && ni < 127) {
            if (*p == '(') d++;
            if (*p == ')') { d--; if (d==0) break; }
            idxbuf[ni++] = *p++;
        }
        idxbuf[ni] = 0;
        GfbValue iv = eval_expr(obj, idxbuf);
        idx = gfb_to_int(&iv);
        gfb_val_free(&iv);
    }

    GfbValue rhs = eval_expr(obj, eq + 1);
    GfbVar *v = var_find(obj, name);
    if (!v) v = var_auto(obj, name);
    if (!v) { gfb_val_free(&rhs); return 1; }

    if (idx >= 0 && v->is_array) var_assign_array(v, idx, rhs);
    else var_assign_scalar(v, rhs);

    gfb_val_free(&rhs);
    return 1;
}

/* ==================================================================== */
/* PRINT                                                                */
/* ==================================================================== */
static void handle_print(GF_BASIC *obj, const char *args){
    if (*args == 0) { printf("\n"); return; }
    /* spezza per ';' fuori da stringhe/parentesi */
    const char *p = args;
    int instr = 0, depth = 0;
    const char *start = p;
    int trailing_semicolon = 0;
    char seg[512];

    while (1) {
        if (*p == '"') instr = !instr;
        if (!instr) { if (*p == '(') depth++; if (*p == ')') depth--; }
        if ((*p == ';' && !instr && depth == 0) || *p == 0) {
            size_t len = (size_t)(p - start);
            if (len >= sizeof seg) len = sizeof seg - 1;
            memcpy(seg, start, len); seg[len] = 0;
            char *segt = str_trim(seg);
            if (*segt) {
                GfbValue v = eval_expr(obj, segt);
                char *s = gfb_to_str_alloc(&v);
                fputs(s, stdout);
                free(s);
                gfb_val_free(&v);
            }
            if (*p == 0) { trailing_semicolon = (len==0 && p>start) ; break; }
            /* se e' l'ultimo carattere della stringa, e' un ';' finale -> niente newline */
            if (*(p+1) == 0) { trailing_semicolon = 1; p++; break; }
            start = p + 1;
        }
        if (*p == 0) break;
        p++;
    }
    if (!trailing_semicolon) printf("\n");
}

/* ==================================================================== */
/* INPUT (semplice, da stdin)                                           */
/* ==================================================================== */
static void handle_input(GF_BASIC *obj, const char *args){
    char buf[256];
    const char *p = args;
    while (*p == ' ') p++;
    if (*p == '"') {
        p++;
        const char *start = p;
        while (*p && *p != '"') p++;
        fwrite(start, 1, (size_t)(p-start), stdout);
        printf("? ");
        if (*p == '"') p++;
        while (*p == ' ' || *p == ',' || *p == ';') p++;
    }
    char name[64]; int i=0;
    while (is_ident_char(*p) && i<63) name[i++]=*p++;
    name[i]=0;
    if (!fgets(buf, sizeof buf, stdin)) buf[0]=0;
    buf[strcspn(buf, "\n")] = 0;
    GfbVar *v = var_find(obj, name);
    if (!v) v = var_auto(obj, name);
    if (v) {
        if (v->type == GFB_STR) { free(v->s); v->s = xstrdup(buf); }
        else if (v->type == GFB_INT) v->i = atol(buf);
        else v->d = atof(buf);
    }
}

/* ==================================================================== */
/* Esecuzione principale (macchina a stati sul program counter)         */
/* ==================================================================== */
typedef struct { char var[64]; double limit, step; int for_idx; } ForFrame;

int gf_basic_run(GF_BASIC *obj){
    if (!obj) return -1;
    pthread_mutex_lock(&obj->lock);

    obj->has_error = 0;
    obj->error[0] = 0;
    if (obj->ret_string) { free(obj->ret_string); obj->ret_string = NULL; }
    obj->ret_value = 0;

    ForFrame forstack[GFB_FORSTACK]; int forsp = 0;
    int gosubstack[GFB_GOSUBSTACK]; int gosp = 0;

    int pc = 0;
    int steps = 0;
    const long MAX_STEPS = 200L * 1000L * 1000L; /* fuse anti-loop infinito */

    while (pc < obj->nstmts && !obj->has_error) {
        if (++steps > MAX_STEPS) { gfb_seterr(obj, "troppi step eseguiti (possibile loop infinito)"); break; }

        char *t = obj->stmts[pc].text;
        char up[512]; str_upper(up, t);

        if (str_ieq(up, "END") || str_ieq(up, "STOP")) break;
        if (str_ieq(up, "END IF") || str_ieq(up, "ENDIF")) { pc++; continue; }
        if (str_ieq(up, "ELSE")) { pc = obj->stmts[pc].jmp2 > 0 ? obj->stmts[pc].jmp2 : pc+1; continue; }

        if (str_ieq_n(up, "REM", 3)) { pc++; continue; }

        if (str_ieq_n(up, "DIM ", 4)) { handle_dim(obj, t+4); pc++; continue; }

        if (str_ieq_n(up, "PRINT", 5)) {
            const char *rest = t + 5;
            while (*rest == ' ') rest++;
            handle_print(obj, rest);
            pc++; continue;
        }
        if (str_ieq_n(up, "INPUT", 5)) {
            const char *rest = t + 5;
            while (*rest == ' ') rest++;
            handle_input(obj, rest);
            pc++; continue;
        }

        if (str_ieq_n(up, "GOTO ", 5)) {
            char lbl[64]; sscanf(t+5, "%63s", lbl);
            int tgt = label_find(obj, lbl);
            if (tgt < 0) { gfb_seterr(obj, "label '%s' non trovata (GOTO)", lbl); break; }
            pc = tgt; continue;
        }
        if (str_ieq_n(up, "GOSUB ", 6)) {
            char lbl[64]; sscanf(t+6, "%63s", lbl);
            int tgt = label_find(obj, lbl);
            if (tgt < 0) { gfb_seterr(obj, "label '%s' non trovata (GOSUB)", lbl); break; }
            if (gosp >= GFB_GOSUBSTACK) { gfb_seterr(obj, "stack GOSUB esaurito"); break; }
            gosubstack[gosp++] = pc + 1;
            pc = tgt; continue;
        }
        if (str_ieq(up, "RETURN")) {
            if (gosp <= 0) { gfb_seterr(obj, "RETURN senza GOSUB"); break; }
            pc = gosubstack[--gosp]; continue;
        }

        if (str_ieq_n(up, "IF ", 3)) {
            /* trova "THEN" (case-insensitive) fuori da stringhe */
            const char *thenpos = NULL;
            {
                int instr = 0;
                for (const char *q = up + 3; *q; q++) {
                    if (*q == '"') instr = !instr;
                    if (!instr && !strncmp(q, "THEN", 4)) { thenpos = q; break; }
                }
            }
            if (!thenpos) { gfb_seterr(obj, "IF senza THEN: '%s'", t); break; }

            size_t condlen = (size_t)(thenpos - (up + 3));
            char cond[512];
            if (condlen >= sizeof cond) condlen = sizeof cond - 1;
            memcpy(cond, t + 3, condlen); cond[condlen] = 0;

            GfbValue cv = eval_expr(obj, cond);
            long truth = gfb_to_int(&cv);
            gfb_val_free(&cv);

            const char *after = thenpos + 4;
            while (*after == ' ') after++;

            if (*after == 0) {
                if (truth) { pc++; }
                else { pc = obj->stmts[pc].jmp1 > 0 ? obj->stmts[pc].jmp1 : pc + 1; }
                continue;
            } else {
                size_t off = (size_t)(after - up);
                const char *orig_after = t + off;
                const char *elsepos = NULL;
                {
                    int instr = 0;
                    for (const char *q = up + off; *q; q++) {
                        if (*q == '"') instr = !instr;
                        if (!instr && !strncmp(q, "ELSE", 4)) { elsepos = up + (q - up); break; }
                    }
                }
                char branch[512];
                if (truth) {
                    size_t blen = elsepos ? (size_t)((elsepos) - (up + off)) : strlen(orig_after);
                    if (blen >= sizeof branch) blen = sizeof branch - 1;
                    memcpy(branch, orig_after, blen); branch[blen] = 0;
                } else if (elsepos) {
                    const char *elseorig = t + (elsepos - up) + 4;
                    while (*elseorig == ' ') elseorig++;
                    strncpy(branch, elseorig, sizeof branch - 1);
                    branch[sizeof branch - 1] = 0;
                } else {
                    branch[0] = 0;
                }
                char *bt = str_trim(branch);
                if (*bt) {
                    char upbt[512]; str_upper(upbt, bt);
                    if (str_ieq_n(upbt, "GOTO ", 5)) {
                        char lbl[64]; sscanf(bt+5, "%63s", lbl);
                        int tgt = label_find(obj, lbl);
                        if (tgt < 0) { gfb_seterr(obj, "label '%s' non trovata", lbl); break; }
                        pc = tgt; continue;
                    }
                    if (str_ieq_n(upbt, "GOSUB ", 6)) {
                        char lbl[64]; sscanf(bt+6, "%63s", lbl);
                        int tgt = label_find(obj, lbl);
                        if (tgt < 0) { gfb_seterr(obj, "label '%s' non trovata", lbl); break; }
                        if (gosp < GFB_GOSUBSTACK) gosubstack[gosp++] = pc + 1;
                        pc = tgt; continue;
                    }
                    if (str_ieq_n(upbt, "PRINT", 5)) {
                        const char *r2 = bt + 5; while (*r2==' ') r2++;
                        handle_print(obj, r2);
                    } else if (!handle_assignment(obj, bt)) {
                        gfb_seterr(obj, "statement inline non riconosciuto: '%s'", bt);
                        break;
                    }
                }
                pc++; continue;
            }
        }

        if (str_ieq_n(up, "FOR ", 4)) {
            const char *eq = strchr(t, '=');
            if (!eq) { gfb_seterr(obj, "FOR malformato: '%s'", t); break; }
            char varname[64]; size_t vl = (size_t)(eq - (t+4));
            if (vl >= sizeof varname) vl = sizeof varname - 1;
            memcpy(varname, t+4, vl); varname[vl]=0;
            char *varname_t = str_trim(varname);

            char up_after_eq[512]; str_upper(up_after_eq, eq+1);
            const char *topos = strstr(up_after_eq, " TO ");
            if (!topos) { gfb_seterr(obj, "FOR senza TO: '%s'", t); break; }
            size_t off_to = (size_t)(topos - up_after_eq);
            char startexpr[256];
            size_t sl = off_to; if (sl >= sizeof startexpr) sl = sizeof startexpr -1;
            memcpy(startexpr, eq+1, sl); startexpr[sl]=0;

            const char *rest_after_to = eq + 1 + off_to + 4;
            char up_after_to[512]; str_upper(up_after_to, rest_after_to);
            const char *steppos = strstr(up_after_to, "STEP");
            char limitexpr[256], stepexpr[64];
            if (steppos) {
                size_t ll = (size_t)(steppos - up_after_to);
                if (ll >= sizeof limitexpr) ll = sizeof limitexpr -1;
                memcpy(limitexpr, rest_after_to, ll); limitexpr[ll]=0;
                strncpy(stepexpr, rest_after_to + ll + 4, sizeof stepexpr -1);
                stepexpr[sizeof stepexpr -1]=0;
            } else {
                strncpy(limitexpr, rest_after_to, sizeof limitexpr -1);
                limitexpr[sizeof limitexpr -1]=0;
                strcpy(stepexpr, "1");
            }

            GfbValue sv = eval_expr(obj, startexpr);
            GfbVar *v = var_find(obj, varname_t);
            if (!v) v = var_auto(obj, varname_t);
            var_assign_scalar(v, sv);
            gfb_val_free(&sv);

            GfbValue lv = eval_expr(obj, limitexpr);
            GfbValue stv = eval_expr(obj, stepexpr);
            double limit = gfb_to_num(&lv);
            double step = gfb_to_num(&stv);
            gfb_val_free(&lv); gfb_val_free(&stv);

            double curval = (v->type==GFB_INT)? (double)v->i : v->d;
            int cont = step >= 0 ? (curval <= limit) : (curval >= limit);
            if (!cont) {
                int nextidx = obj->stmts[pc].jmp2;
                pc = nextidx >= 0 ? nextidx + 1 : pc + 1;
                continue;
            }
            if (forsp < GFB_FORSTACK) {
                ForFrame *f = &forstack[forsp++];
                strncpy(f->var, varname_t, sizeof f->var -1); f->var[sizeof f->var-1]=0;
                f->limit = limit; f->step = step; f->for_idx = pc;
            }
            pc++; continue;
        }

        if (str_ieq_n(up, "NEXT", 4)) {
            if (forsp <= 0) { gfb_seterr(obj, "NEXT senza FOR"); break; }
            ForFrame *f = &forstack[forsp-1];
            GfbVar *v = var_find(obj, f->var);
            double curval = (v->type==GFB_INT) ? (double)v->i : v->d;
            curval += f->step;
            if (v->type==GFB_INT) v->i = (long)curval; else v->d = curval;
            int cont = f->step >= 0 ? (curval <= f->limit) : (curval >= f->limit);
            if (cont) { pc = f->for_idx + 1; }
            else { forsp--; pc++; }
            continue;
        }

        if (str_ieq_n(up, "WHILE", 5) && (up[5]==0 || up[5]==' ')) {
            const char *cond = t + 5;
            while (*cond==' ') cond++;
            GfbValue cv = eval_expr(obj, cond);
            long truth = gfb_to_int(&cv);
            gfb_val_free(&cv);
            if (truth) pc++;
            else {
                int wend = obj->stmts[pc].jmp1;
                pc = wend >= 0 ? wend + 1 : pc + 1;
            }
            continue;
        }
        if (str_ieq(up, "WEND")) {
            int wh = obj->stmts[pc].jmp1;
            pc = wh >= 0 ? wh : pc + 1;
            continue;
        }

        if (str_ieq(up, "DO") || str_ieq_n(up, "DO ", 3)) {
            const char *r = t + 2;
            while (*r==' ') r++;
            char upr[512]; str_upper(upr, r);
            if (str_ieq_n(upr, "WHILE", 5)) {
                GfbValue cv = eval_expr(obj, r+5);
                long truth = gfb_to_int(&cv); gfb_val_free(&cv);
                if (!truth) { int loopidx = obj->stmts[pc].jmp1; pc = loopidx>=0?loopidx+1:pc+1; continue; }
            } else if (str_ieq_n(upr, "UNTIL", 5)) {
                GfbValue cv = eval_expr(obj, r+5);
                long truth = gfb_to_int(&cv); gfb_val_free(&cv);
                if (truth) { int loopidx = obj->stmts[pc].jmp1; pc = loopidx>=0?loopidx+1:pc+1; continue; }
            }
            pc++; continue;
        }
        if (str_ieq_n(up, "LOOP", 4)) {
            const char *r = t + 4;
            while (*r==' ') r++;
            char upr[512]; str_upper(upr, r);
            int back = 1;
            if (str_ieq_n(upr, "WHILE", 5)) {
                GfbValue cv = eval_expr(obj, r+5);
                back = gfb_to_int(&cv) != 0; gfb_val_free(&cv);
            } else if (str_ieq_n(upr, "UNTIL", 5)) {
                GfbValue cv = eval_expr(obj, r+5);
                back = gfb_to_int(&cv) == 0; gfb_val_free(&cv);
            }
            if (back) { int doidx = obj->stmts[pc].jmp1; pc = doidx>=0?doidx+1:pc+1; }
            else pc++;
            continue;
        }

        if (str_ieq_n(up, "SYSRETURN_STRING", 16)) {
            const char *p = t + 16;
            while (*p==' ') p++;
            if (*p=='(') {
                p++;
                char inner[512]; int il=0; int d=1;
                while (*p && d>0 && il < 511) {
                    if (*p=='(') d++;
                    if (*p==')') { d--; if(d==0) break; }
                    inner[il++]=*p++;
                }
                inner[il]=0;
                GfbValue v = eval_expr(obj, inner);
                char *s = gfb_to_str_alloc(&v);
                if (obj->ret_string) free(obj->ret_string);
                obj->ret_string = s;
                gfb_val_free(&v);
            }
            pc++; continue;
        }
        if (str_ieq_n(up, "SYSRETURN_VALUE", 15)) {
            const char *p = t + 15;
            while (*p==' ') p++;
            if (*p=='(') {
                p++;
                char inner[512]; int il=0; int d=1;
                while (*p && d>0 && il < 511) {
                    if (*p=='(') d++;
                    if (*p==')') { d--; if(d==0) break; }
                    inner[il++]=*p++;
                }
                inner[il]=0;
                GfbValue v = eval_expr(obj, inner);
                obj->ret_value = gfb_to_int(&v);
                gfb_val_free(&v);
            }
            pc++; continue;
        }

        if (str_ieq_n(up, "CALL ", 5)) {
            GfbValue v = eval_expr(obj, t+5);
            gfb_val_free(&v);
            pc++; continue;
        }

        if (handle_assignment(obj, t)) { pc++; continue; }
        {
            GfbValue v = eval_expr(obj, t);
            gfb_val_free(&v);
            if (obj->has_error) break;
        }
        pc++;
    }

    (void)forstack;
    pthread_mutex_unlock(&obj->lock);
    return obj->has_error ? -1 : 0;
}

/* ==================================================================== */
/* API pubblica: create / destroy / set_program / getters/setters       */
/* ==================================================================== */
GF_BASIC *gf_basic_create(void){
    GF_BASIC *obj = calloc(1, sizeof *obj);
    if (!obj) return NULL;
    pthread_mutex_init(&obj->lock, NULL);
    srand((unsigned)time(NULL) ^ (unsigned)(size_t)obj);
    return obj;
}

void gf_basic_destroy(GF_BASIC *obj){
    if (!obj) return;
    pthread_mutex_lock(&obj->lock);
    for (int k = 0; k < obj->nvars; k++) {
        GfbVar *v = &obj->vars[k];
        if (v->is_array) {
            if (v->arr_s) { for (int j=0;j<=v->arr_size;j++) free(v->arr_s[j]); free(v->arr_s); }
            free(v->arr_i); free(v->arr_d);
        } else {
            free(v->s);
        }
    }
    free(obj->ret_string);
    pthread_mutex_unlock(&obj->lock);
    pthread_mutex_destroy(&obj->lock);
    free(obj);
}

int gf_basic_set_program(GF_BASIC *obj, const char *code){
    if (!obj || !code) return -1;
    pthread_mutex_lock(&obj->lock);
    obj->has_error = 0; obj->error[0] = 0;
    int r = gfb_preprocess(obj, code);
    pthread_mutex_unlock(&obj->lock);
    return r;
}

const char *gf_basic_get_return_string(GF_BASIC *obj){
    if (!obj) return "";
    return obj->ret_string ? obj->ret_string : "";
}
long gf_basic_get_return_value(GF_BASIC *obj){
    if (!obj) return 0;
    return obj->ret_value;
}

int gf_basic_set_var_int(GF_BASIC *obj, const char *name, long v){
    if (!obj) return -1;
    pthread_mutex_lock(&obj->lock);
    GfbVar *var = var_find(obj, name);
    if (!var) var = var_create(obj, name, GFB_INT, 0);
    if (var) { var->type = GFB_INT; var->i = v; }
    pthread_mutex_unlock(&obj->lock);
    return var ? 0 : -1;
}
int gf_basic_set_var_dbl(GF_BASIC *obj, const char *name, double v){
    if (!obj) return -1;
    pthread_mutex_lock(&obj->lock);
    GfbVar *var = var_find(obj, name);
    if (!var) var = var_create(obj, name, GFB_DBL, 0);
    if (var) { var->type = GFB_DBL; var->d = v; }
    pthread_mutex_unlock(&obj->lock);
    return var ? 0 : -1;
}
int gf_basic_set_var_str(GF_BASIC *obj, const char *name, const char *v){
    if (!obj) return -1;
    pthread_mutex_lock(&obj->lock);
    GfbVar *var = var_find(obj, name);
    if (!var) var = var_create(obj, name, GFB_STR, 0);
    if (var) { var->type = GFB_STR; free(var->s); var->s = xstrdup(v); }
    pthread_mutex_unlock(&obj->lock);
    return var ? 0 : -1;
}
long gf_basic_get_var_int(GF_BASIC *obj, const char *name){
    if (!obj) return 0;
    pthread_mutex_lock(&obj->lock);
    GfbVar *v = var_find(obj, name);
    long r = v ? gfb_to_int(&(GfbValue){v->type, v->i, v->d, v->s}) : 0;
    pthread_mutex_unlock(&obj->lock);
    return r;
}
double gf_basic_get_var_dbl(GF_BASIC *obj, const char *name){
    if (!obj) return 0;
    pthread_mutex_lock(&obj->lock);
    GfbVar *v = var_find(obj, name);
    double r = v ? gfb_to_num(&(GfbValue){v->type, v->i, v->d, v->s}) : 0;
    pthread_mutex_unlock(&obj->lock);
    return r;
}
const char *gf_basic_get_var_str(GF_BASIC *obj, const char *name){
    if (!obj) return "";
    pthread_mutex_lock(&obj->lock);
    GfbVar *v = var_find(obj, name);
    static __thread char buf[256];
    if (!v) { pthread_mutex_unlock(&obj->lock); return ""; }
    if (v->type == GFB_STR) strncpy(buf, v->s ? v->s : "", sizeof buf -1);
    else { GfbValue tmp = {v->type, v->i, v->d, NULL}; char *s = gfb_to_str_alloc(&tmp); strncpy(buf, s, sizeof buf -1); free(s); }
    buf[sizeof buf -1] = 0;
    pthread_mutex_unlock(&obj->lock);
    return buf;
}

const char *gf_basic_get_error(GF_BASIC *obj){
    if (!obj) return "";
    return obj->error;
}

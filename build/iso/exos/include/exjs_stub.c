/* =============================================================================
 * lib/exjs/exjs_stub.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Lo stub di ExJs. Stessa forma di lib/exhtml/exhtml_stub.c.
 *
 * ! QUI SI GRIDA E SI MUORE, come per exwin e per exhtml, e non si rende zero
 * come fa eximg. La regola e' sempre la stessa: cosa puo' fare il programma
 * senza. Chi non sa leggere un PNG sa ancora disegnare; chi apre pagine e non
 * ha il motore JavaScript mostrerebbe pagine mezze vuote senza saperlo, e una
 * pagina sbagliata che sembra giusta e' peggio di un errore.
 *
 * ! TRENTADUE PONTI SONO TANTI, e non e' un segno che l'interfaccia sia
 * gonfia: sono tutta exjs.h, che e' l'interfaccia di un linguaggio. Ognuno e'
 * una riga, e la riga esiste perche' il programma che chiama non deve sapere
 * niente di come il motore e' fatto — che e' l'unica cosa che permettera' di
 * mettere QuickJS al suo posto senza toccare chi lo usa.
 * ============================================================================= */

#include "exjs.h"
#include "exlib.h"

#define SYS_WRITE       4
#define SYS_EXIT        1
#define SYS_LIB_TROVA   238

/* ! LA SYSCALL SI CHIAMA A MANO, come tutto in questo file: lo stub non
 * dipende dalla libc — e' l'unico pezzo che deve funzionare PRIMA che una
 * libreria qualunque sia aperta. Vedi la nota in testa a exlib.c. */
static int lib_gia_aperta(const char *percorso)
{
    int r;

    __asm__ volatile ("int $0x80"
                      : "=a"(r)
                      : "a"(SYS_LIB_TROVA), "b"(percorso)
                      : "memory");
    return r > 0;
}

static void grida_e_muori(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    __asm__ volatile ("int $0x80" :: "a"(SYS_WRITE), "b"(2), "c"(s), "d"(n) : "memory");
    __asm__ volatile ("int $0x80" :: "a"(SYS_EXIT), "b"(1) : "memory");
    for (;;) { }
}

/* I due motori, e i due posti per ognuno: sul sistema installato e sul CD. */
static const char *const g_dove_exjs[] = {
    "/exwin/lib/exjs.so",
    "/cdrom/exwin/lib/exjs.so"
};

static const char *const g_dove_qjs[] = {
    "/exwin/lib/quickjs.so",
    "/cdrom/exwin/lib/quickjs.so"
};

/* Quale si vuole, e quale si e' aperto davvero. Vedi exjs.h. */
static int g_voluto   = 0;      /* 0 = ExJs, 1 = QuickJS */
static int g_caricato = -1;     /* -1 = nessuno ancora */

void exjs_motore(int quickjs)
{
    /* ! DOPO L'APERTURA NON CAMBIA NIENTE, e non e' un errore: la libreria e'
     * gia' mappata nel processo, e un secondo motore accanto al primo vorrebbe
     * dire due interpreti che non si vedono. Chi vuole cambiare motore chiude
     * la pagina e riapre — che e' esattamente quel che fa il browser. */
    g_voluto = quickjs ? 1 : 0;
}

int exjs_motore_ora(void)
{
    return g_caricato;
}

static struct {
    int pronto;
    unsigned int (*quanto_serve)(unsigned int, unsigned int);
    ExJsCtx *(*apri)(void *, unsigned int, unsigned int, unsigned int);
    void      (*chiudi)(ExJsCtx *);
    int (*esegui)(ExJsCtx *, const char *, unsigned int, ExJsVal *, ExJsErrore *);
    ExJsVal (*indefinito)(void);
    ExJsVal (*nullo)(void);
    ExJsVal (*booleano)(int);
    ExJsVal (*numero)(ExJsCtx *, double);
    ExJsVal (*stringa)(ExJsCtx *, const char *, int);
    ExJsVal (*oggetto)(ExJsCtx *);
    ExJsVal (*vettore)(ExJsCtx *);
    ExJsVal (*nativa)(ExJsCtx *, ExJsNativa, void *, const char *);
    ExJsVal (*costruttore)(ExJsCtx *, ExJsNativa, void *, const char *);
    int (*tipo)(ExJsCtx *, ExJsVal);
    double (*a_numero)(ExJsCtx *, ExJsVal);
    int (*a_booleano)(ExJsCtx *, ExJsVal);
    const char *(*a_stringa)(ExJsCtx *, ExJsVal);
    int (*metti)(ExJsCtx *, ExJsVal, const char *, ExJsVal);
    ExJsVal (*prendi)(ExJsCtx *, ExJsVal, const char *);
    int (*indice_metti)(ExJsCtx *, ExJsVal, unsigned int, ExJsVal);
    ExJsVal (*indice_prendi)(ExJsCtx *, ExJsVal, unsigned int);
    unsigned int (*lunghezza)(ExJsCtx *, ExJsVal);
    ExJsVal (*esotico)(ExJsCtx *, ExJsLeggiProp, ExJsScriviProp, void *);
    void *(*esotico_dato)(ExJsCtx *, ExJsVal);
    int (*proto_metti)(ExJsCtx *, ExJsVal, ExJsVal);
    void (*uscita_metti)(ExJsCtx *, ExJsUscita, void *);
    ExJsVal (*globale)(ExJsCtx *);
    ExJsVal (*chiama)(ExJsCtx *, ExJsVal, ExJsVal, const ExJsVal *, int, ExJsErrore *);
    ExJsVal (*invoca)(ExJsCtx *, ExJsVal, ExJsVal, const ExJsVal *, int, ExJsErrore *);
    unsigned int (*accoda)(ExJsCtx *, ExJsVal, unsigned int, unsigned int);
    void (*disdici)(ExJsCtx *, unsigned int);
    int (*pompa)(ExJsCtx *, unsigned int);
    int (*lavori_in_attesa)(ExJsCtx *);
    void (*memoria)(ExJsCtx *, unsigned int *, unsigned int *, unsigned int *, unsigned int *);
} P;

static void *chiedi(const ExLibTesta *t, const char *nome)
{
    void *p = exlib_simbolo(t, nome);

    (void)nome;
    if (p == 0)
        grida_e_muori("exjs: la libreria condivisa non esporta un nome che "
                      "serve a questo programma\n");
    return p;
}

static void assicura(void)
{
    const ExLibTesta *t;

    if (P.pronto) return;

    /* =========================================================================
     * ! PRIMA SI GUARDA CHE COSA C'E' GIA' IN QUESTO PROCESSO, e questa e' la
     * riga che tiene insieme tutto il resto.
     *
     * Ogni programma e ogni libreria si portano dentro una copia di questo
     * stub, con i propri dati: il browser ha il suo, exdom.so ha il suo. Se
     * ognuno scegliesse per conto proprio, nello stesso processo girerebbero
     * DUE motori che non si vedono — e il ponte lavorerebbe su un contesto
     * costruito dall'altro. Il sintomo, quando e' successo, e' stato un page
     * fault dentro exjs.so con in mano un contesto di QuickJS: non somigliava
     * affatto alla sua causa.
     *
     * ! CHI ARRIVA PER PRIMO DECIDE PER TUTTI, ed e' l'unica regola che due
     * stub separati possono seguire senza parlarsi. Nel browser il primo e'
     * lui — apre il motore prima di aprire il ponte — quindi la scelta
     * dell'utente vale; per chiunque arrivi dopo, «quella che c'e' gia'» E'
     * la scelta.
     * ===================================================================== */
    t = 0;
    if (lib_gia_aperta(g_dove_exjs[0]) || lib_gia_aperta(g_dove_exjs[1])) {
        t = exlib_apri_fra(g_dove_exjs,
                           (int)(sizeof g_dove_exjs / sizeof g_dove_exjs[0]));
        if (t) g_caricato = 0;
    } else if (lib_gia_aperta(g_dove_qjs[0]) || lib_gia_aperta(g_dove_qjs[1])) {
        t = exlib_apri_fra(g_dove_qjs,
                           (int)(sizeof g_dove_qjs / sizeof g_dove_qjs[0]));
        if (t) g_caricato = 1;
    }

    /* ! SI PROVA QUELLA VOLUTA, POI L'ALTRA. Un sistema senza quickjs.so —
     * che e' un file da mezzo megabyte e puo' benissimo non essere stato
     * installato — deve continuare a eseguire gli script con ExJs, non
     * fermarsi. Quale sia finita davvero lo dice exjs_motore_ora(). */
    if (t == 0 && g_voluto) {
        t = exlib_apri_fra(g_dove_qjs,
                           (int)(sizeof g_dove_qjs / sizeof g_dove_qjs[0]));
        if (t) g_caricato = 1;
    }
    if (t == 0) {
        t = exlib_apri_fra(g_dove_exjs,
                           (int)(sizeof g_dove_exjs / sizeof g_dove_exjs[0]));
        if (t) g_caricato = 0;
    }
    if (t == 0 && !g_voluto) {
        t = exlib_apri_fra(g_dove_qjs,
                           (int)(sizeof g_dove_qjs / sizeof g_dove_qjs[0]));
        if (t) g_caricato = 1;
    }

    if (t == 0)
        grida_e_muori("exjs: non trovo nessuna libreria del JavaScript.\n"
                      "      Cercate /exwin/lib/exjs.so e "
                      "/exwin/lib/quickjs.so (anche sotto /cdrom).\n");

    P.quanto_serve = (unsigned int (*)(unsigned int, unsigned int))
        chiedi(t, "exjs_quanto_serve");
    P.apri = (ExJsCtx *(*)(void *, unsigned int, unsigned int, unsigned int))
        chiedi(t, "exjs_apri");
    P.chiudi = (void (*)(ExJsCtx *))
        chiedi(t, "exjs_chiudi");
    P.esegui = (int (*)(ExJsCtx *, const char *, unsigned int, ExJsVal *, ExJsErrore *))
        chiedi(t, "exjs_esegui");
    P.indefinito = (ExJsVal (*)(void))
        chiedi(t, "exjs_indefinito");
    P.nullo = (ExJsVal (*)(void))
        chiedi(t, "exjs_nullo");
    P.booleano = (ExJsVal (*)(int))
        chiedi(t, "exjs_booleano");
    P.numero = (ExJsVal (*)(ExJsCtx *, double))
        chiedi(t, "exjs_numero");
    P.stringa = (ExJsVal (*)(ExJsCtx *, const char *, int))
        chiedi(t, "exjs_stringa");
    P.oggetto = (ExJsVal (*)(ExJsCtx *))
        chiedi(t, "exjs_oggetto");
    P.vettore = (ExJsVal (*)(ExJsCtx *))
        chiedi(t, "exjs_vettore");
    P.nativa = (ExJsVal (*)(ExJsCtx *, ExJsNativa, void *, const char *))
        chiedi(t, "exjs_nativa");
    P.costruttore = (ExJsVal (*)(ExJsCtx *, ExJsNativa, void *, const char *))
        chiedi(t, "exjs_costruttore");
    P.tipo = (int (*)(ExJsCtx *, ExJsVal))
        chiedi(t, "exjs_tipo");
    P.a_numero = (double (*)(ExJsCtx *, ExJsVal))
        chiedi(t, "exjs_a_numero");
    P.a_booleano = (int (*)(ExJsCtx *, ExJsVal))
        chiedi(t, "exjs_a_booleano");
    P.a_stringa = (const char *(*)(ExJsCtx *, ExJsVal))
        chiedi(t, "exjs_a_stringa");
    P.metti = (int (*)(ExJsCtx *, ExJsVal, const char *, ExJsVal))
        chiedi(t, "exjs_metti");
    P.prendi = (ExJsVal (*)(ExJsCtx *, ExJsVal, const char *))
        chiedi(t, "exjs_prendi");
    P.indice_metti = (int (*)(ExJsCtx *, ExJsVal, unsigned int, ExJsVal))
        chiedi(t, "exjs_indice_metti");
    P.indice_prendi = (ExJsVal (*)(ExJsCtx *, ExJsVal, unsigned int))
        chiedi(t, "exjs_indice_prendi");
    P.lunghezza = (unsigned int (*)(ExJsCtx *, ExJsVal))
        chiedi(t, "exjs_lunghezza");
    P.esotico = (ExJsVal (*)(ExJsCtx *, ExJsLeggiProp, ExJsScriviProp, void *))
        chiedi(t, "exjs_esotico");
    P.esotico_dato = (void *(*)(ExJsCtx *, ExJsVal))
        chiedi(t, "exjs_esotico_dato");
    P.proto_metti = (int (*)(ExJsCtx *, ExJsVal, ExJsVal))
        chiedi(t, "exjs_proto_metti");
    P.uscita_metti = (void (*)(ExJsCtx *, ExJsUscita, void *))
        chiedi(t, "exjs_uscita_metti");
    P.globale = (ExJsVal (*)(ExJsCtx *))
        chiedi(t, "exjs_globale");
    P.chiama = (ExJsVal (*)(ExJsCtx *, ExJsVal, ExJsVal, const ExJsVal *, int, ExJsErrore *))
        chiedi(t, "exjs_chiama");
    P.invoca = (ExJsVal (*)(ExJsCtx *, ExJsVal, ExJsVal, const ExJsVal *, int, ExJsErrore *))
        chiedi(t, "exjs_invoca");
    P.accoda = (unsigned int (*)(ExJsCtx *, ExJsVal, unsigned int, unsigned int))
        chiedi(t, "exjs_accoda");
    P.disdici = (void (*)(ExJsCtx *, unsigned int))
        chiedi(t, "exjs_disdici");
    P.pompa = (int (*)(ExJsCtx *, unsigned int))
        chiedi(t, "exjs_pompa");
    P.lavori_in_attesa = (int (*)(ExJsCtx *))
        chiedi(t, "exjs_lavori_in_attesa");
    P.memoria = (void (*)(ExJsCtx *, unsigned int *, unsigned int *, unsigned int *, unsigned int *))
        chiedi(t, "exjs_memoria");

    P.pronto = 1;
}

unsigned int exjs_quanto_serve(unsigned int oggetti, unsigned int arena_byte)
{ assicura(); return P.quanto_serve(oggetti, arena_byte); }

ExJsCtx *exjs_apri(void *memoria, unsigned int byte, unsigned int oggetti,
                   unsigned int arena_byte)
{ assicura(); return P.apri(memoria, byte, oggetti, arena_byte); }

void exjs_chiudi(ExJsCtx *c)
{ assicura(); P.chiudi(c); }

int exjs_esegui(ExJsCtx *c, const char *sorgente, unsigned int n,
                ExJsVal *risultato, ExJsErrore *err)
{ assicura(); return P.esegui(c, sorgente, n, risultato, err); }

ExJsVal exjs_indefinito(void)
{ assicura(); return P.indefinito(); }

ExJsVal exjs_nullo(void)
{ assicura(); return P.nullo(); }

ExJsVal exjs_booleano(int v)
{ assicura(); return P.booleano(v); }

ExJsVal exjs_numero(ExJsCtx *c, double v)
{ assicura(); return P.numero(c, v); }

ExJsVal exjs_stringa(ExJsCtx *c, const char *s, int n)
{ assicura(); return P.stringa(c, s, n); }

ExJsVal exjs_oggetto(ExJsCtx *c)
{ assicura(); return P.oggetto(c); }

ExJsVal exjs_vettore(ExJsCtx *c)
{ assicura(); return P.vettore(c); }

ExJsVal exjs_costruttore(ExJsCtx *c, ExJsNativa f, void *dato, const char *nome)
{ assicura(); return P.costruttore(c, f, dato, nome); }

ExJsVal exjs_nativa(ExJsCtx *c, ExJsNativa f, void *dato, const char *nome)
{ assicura(); return P.nativa(c, f, dato, nome); }

int exjs_tipo(ExJsCtx *c, ExJsVal v)
{ assicura(); return P.tipo(c, v); }

double exjs_a_numero(ExJsCtx *c, ExJsVal v)
{ assicura(); return P.a_numero(c, v); }

int exjs_a_booleano(ExJsCtx *c, ExJsVal v)
{ assicura(); return P.a_booleano(c, v); }

const char *exjs_a_stringa(ExJsCtx *c, ExJsVal v)
{ assicura(); return P.a_stringa(c, v); }

int exjs_metti(ExJsCtx *c, ExJsVal ogg, const char *nome, ExJsVal v)
{ assicura(); return P.metti(c, ogg, nome, v); }

ExJsVal exjs_prendi(ExJsCtx *c, ExJsVal ogg, const char *nome)
{ assicura(); return P.prendi(c, ogg, nome); }

int exjs_indice_metti(ExJsCtx *c, ExJsVal vet, unsigned int i, ExJsVal v)
{ assicura(); return P.indice_metti(c, vet, i, v); }

ExJsVal exjs_indice_prendi(ExJsCtx *c, ExJsVal vet, unsigned int i)
{ assicura(); return P.indice_prendi(c, vet, i); }

unsigned int exjs_lunghezza(ExJsCtx *c, ExJsVal vet)
{ assicura(); return P.lunghezza(c, vet); }

ExJsVal exjs_esotico(ExJsCtx *c, ExJsLeggiProp leggi, ExJsScriviProp scrivi,
                     void *dato)
{ assicura(); return P.esotico(c, leggi, scrivi, dato); }

void *exjs_esotico_dato(ExJsCtx *c, ExJsVal v)
{ assicura(); return P.esotico_dato(c, v); }

int exjs_proto_metti(ExJsCtx *c, ExJsVal ogg, ExJsVal proto)
{ assicura(); return P.proto_metti(c, ogg, proto); }

void exjs_uscita_metti(ExJsCtx *c, ExJsUscita f, void *dato)
{ assicura(); P.uscita_metti(c, f, dato); }

ExJsVal exjs_globale(ExJsCtx *c)
{ assicura(); return P.globale(c); }

ExJsVal exjs_chiama(ExJsCtx *c, ExJsVal f, ExJsVal questo,
                    const ExJsVal *arg, int n_arg, ExJsErrore *err)
{ assicura(); return P.chiama(c, f, questo, arg, n_arg, err); }

ExJsVal exjs_invoca(ExJsCtx *c, ExJsVal f, ExJsVal questo,
                    const ExJsVal *arg, int n_arg, ExJsErrore *err)
{ assicura(); return P.invoca(c, f, questo, arg, n_arg, err); }

unsigned int exjs_accoda(ExJsCtx *c, ExJsVal f, unsigned int quando_ms,
                         unsigned int ripeti_ms)
{ assicura(); return P.accoda(c, f, quando_ms, ripeti_ms); }

void exjs_disdici(ExJsCtx *c, unsigned int id)
{ assicura(); P.disdici(c, id); }

int exjs_pompa(ExJsCtx *c, unsigned int ora_ms)
{ assicura(); return P.pompa(c, ora_ms); }

int exjs_lavori_in_attesa(ExJsCtx *c)
{ assicura(); return P.lavori_in_attesa(c); }

void exjs_memoria(ExJsCtx *c, unsigned int *caselle_usate,
                  unsigned int *caselle_max, unsigned int *arena_usata,
                  unsigned int *arena_max)
{ assicura(); P.memoria(c, caselle_usate, caselle_max, arena_usata, arena_max); }


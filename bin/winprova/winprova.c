/* =============================================================================
 * bin/winprova/winprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La prova del toolkit: una finestra con tutti i controlli che esistono
 *
 *     /bin/winprova            apre la finestra
 *     /bin/winprova -s FILE    apre anche uno sfondo con quell'immagine
 *     /bin/winprova -n         i controlli aggiunti dopo: spunta, radio,
 *                              barra di scorrimento, elenco a discesa, tab
 *     /bin/winprova -c         l'area del codice: testo colorato, l'elenco
 *                              delle funzioni, e il cursore che ci salta
 *     /bin/winprova -m         il contenitore MDI: tre finestre dentro una
 *
 * ! NON E' UN ESEMPIO, E' UNA PROVA. Serve a far vedere che ogni controllo si
 * disegna dove e' stato messo e che un clic su un pulsante torna indietro come
 * EXM_COMANDO con l'id giusto — cioe' che la catena
 * server -> IPC -> libreria -> procedura sia intera. Un esempio si guarda; una
 * prova dice se qualcosa si e' rotto.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"
#include "exwin.h"

/* +0.001 a ogni modifica: `winprova -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("winprova", "0.006");

#define ID_OK       101
#define ID_ANNULLA  102

/* I controlli nuovi, tutti nella stessa finestra: vedi finestra_nuovi(). */
#define ID_TAB      201
#define ID_SP1      202
#define ID_SP2      203
#define ID_RAD1     204
#define ID_RAD2     205
#define ID_RAD3     206
#define ID_COMBO    207
#define ID_SCV      208
#define ID_SCO      209

#define ID_FUNZ     301     /* l'elenco delle funzioni, a sinistra */
#define ID_CODICE   302

/* Le tre finestre dentro il contenitore MDI, e i loro controlli. */
#define ID_BOT_A    401
#define ID_SPU_A    402
#define ID_LIS_B    403
#define ID_BOT_C    404

#define PASSO       40      /* di quanto cresce a ogni freccia */

static ExFinestra g_etichetta;
static ExFinestra g_int, g_sep, g_riq, g_t1, g_t2, g_ok, g_ann;
static unsigned int g_premuti = 0;
static int g_w = 360, g_h = 220;        /* la misura che la finestra ha ADESSO */

/* =============================================================================
 * ! LA PROVA DEL RIDIMENSIONAMENTO SI FA A TASTI, NON COL MOUSE, e non e' un
 * ripiego: i movimenti relativi grandi del monitor di QEMU si perdono per
 * strada, quindi una prova che dipende da DOVE si clicca non e' ripetibile.
 * Con le frecce la misura la decide QUESTO file, e quella che arriva indietro
 * in EXM_MISURA e' quella che il server ha davvero concesso — cioe' un numero
 * che si confronta, non un'impressione.
 *
 * ! E SI SCRIVE SULLA SERIALE, non con printf: un'applicazione grafica gira su
 * una console che non e' quella della shell, e cio' che stampa li' non lo legge
 * nessuno da fuori.
 * ============================================================================= */
static void rifai_disposizione(int w, int h)
{
    ex_misura(g_int, w, 22);

    ex_misura(g_t1, w - 110, 22);
    ex_misura(g_t2, w - 110, 22);

    ex_misura(g_sep, w - 28, 2);

    ex_misura(g_riq, w - 28, 50);
    ex_misura(g_etichetta, w - 56, 16);

    ex_sposta(g_ok,  w - 190, h - 42);
    ex_sposta(g_ann, w - 100, h - 42);
}

/* =============================================================================
 * LA PROVA DEI CONTROLLI AGGIUNTI IL 3 SETTEMBRE 2026
 *
 * ! OGNI COMANDO SI SCRIVE SULLA SERIALE CON L'ID E IL VALORE, e non e' un
 * di piu': una spunta che si accende si vede anche a occhio, ma «l'ho premuta e
 * mi e' arrivato l'id 202 con valore 1» e' un numero che si confronta. La
 * differenza fra una prova e una dimostrazione sta tutta li'.
 *
 * ! E I TRE RADIO STANNO DENTRO UN RIQUADRO APPOSTA: il gruppo di un radio sono
 * i fratelli, cioe' i controlli con lo stesso padre. Il riquadro che si vede E'
 * il gruppo che vale, e questa finestra e' anche la prova che le due cose
 * coincidono — accendendone uno si spegne solo chi sta nella stessa cornice.
 * ========================================================================== */
static ExFinestra g_nuovi_eco, g_nuovi_scv, g_nuovi_sco, g_nuovi_combo;
static ExFinestra g_nuovi_sp1, g_nuovi_r1, g_nuovi_r2, g_nuovi_r3;

static long procedura_nuovi(ExFinestra f, unsigned int msg, unsigned int wp,
                            long lp)
{
    char riga[96];

    switch (msg) {
    case EXM_COMANDO:
        sprintf(riga, "winprova: comando id=%u valore=%ld", wp, lp);
        log_seriale(riga);
        ex_testo_metti(g_nuovi_eco, riga + 10);

        /* La spunta accende e spegne la barra orizzontale: e' il modo di far
         * vedere che ex_accendi e ex_scorri_* fanno davvero quel che dicono. */
        if (wp == ID_SP1)
            ex_scorri_limiti(g_nuovi_sco, lp ? 300u : 0u, 40u);
        return 0;

    case EXM_CHIUDI:
        ex_esci(0);
        return 0;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

static int finestra_nuovi(void)
{
    ExFinestra f, riq, tab;

    f = ex_crea("finestra", "Controlli nuovi",
                EX_TITOLO | EX_BORDO | EX_CHIUDI, 100, 70, 440, 300,
                0, 0, procedura_nuovi);
    if (f == 0) {
        printf("winprova: il server a finestre non risponde.\n");
        printf("          Avvialo con:  exwin\n");
        return 1;
    }

    tab = ex_crea("tab", "", EX_FIGLIO, 0, 0, 440, 24, f, ID_TAB, 0);
    ex_voce_aggiungi(tab, "Generale");
    ex_voce_aggiungi(tab, "Aspetto");
    ex_voce_aggiungi(tab, "Avanzate");

    g_nuovi_sp1 = ex_crea("spunta", "barra orizzontale accesa", EX_FIGLIO,
                          16, 40, 220, 20, f, ID_SP1, 0);
    ex_crea("spunta", "salva uscendo", EX_FIGLIO, 16, 66, 220, 20, f, ID_SP2, 0);

    riq = ex_crea("riquadro", "Allineamento", EX_FIGLIO,
                  16, 96, 200, 96, f, 0, 0);
    g_nuovi_r1 = ex_crea("radio", "a sinistra", EX_FIGLIO,
                         12, 22, 170, 20, riq, ID_RAD1, 0);
    g_nuovi_r2 = ex_crea("radio", "in mezzo",   EX_FIGLIO,
                         12, 46, 170, 20, riq, ID_RAD2, 0);
    g_nuovi_r3 = ex_crea("radio", "a destra",   EX_FIGLIO,
                         12, 70, 170, 20, riq, ID_RAD3, 0);
    ex_accendi(g_nuovi_r1, 1);

    ex_crea("etichetta", "Carattere:", EX_FIGLIO, 232, 44, 90, 16, f, 0, 0);
    g_nuovi_combo = ex_crea("combo", "", EX_FIGLIO,
                            232, 64, 170, 22, f, ID_COMBO, 0);
    ex_voce_aggiungi(g_nuovi_combo, "serif");
    ex_voce_aggiungi(g_nuovi_combo, "sans");
    ex_voce_aggiungi(g_nuovi_combo, "monospazio");
    ex_voce_aggiungi(g_nuovi_combo, "di sistema");

    /* Verticale perche' e' piu' alta che larga; orizzontale l'altra. Non c'e'
     * nessun bit da mettere d'accordo con la misura. */
    g_nuovi_scv = ex_crea("scorrimento", "", EX_FIGLIO,
                          412, 96, 16, 120, f, ID_SCV, 0);
    ex_scorri_limiti(g_nuovi_scv, 200, 20);

    g_nuovi_sco = ex_crea("scorrimento", "", EX_FIGLIO,
                          16, 226, 390, 16, f, ID_SCO, 0);

    g_nuovi_eco = ex_crea("etichetta", "nessun comando ancora", EX_FIGLIO,
                          16, 252, 400, 16, f, 0, 0);

    ex_procedura_base(f, EXM_DISEGNA, 0, 0);
    printf("winprova: controlli nuovi. Ogni comando finisce sulla seriale;\n"
           "          Tab gira fra i controlli, spazio scatta le spunte.\n");
    return 0;
}

/* =============================================================================
 * LA PROVA DELL'AREA DEL CODICE
 *
 * ! IL SORGENTE DI PROVA STA QUI DENTRO, e non si legge da un file: su un CD
 * non ci sono sorgenti, e una prova che dipende da un file che potrebbe non
 * esserci e' una prova che a volte non parte — e quando non parte sembra rotto
 * il controllo. Queste righe sono scelte per toccare ogni ruolo: preprocessore,
 * commento a blocco SU PIU' RIGHE, commento di riga, stringa, carattere,
 * numeri, chiavi, tipi e nomi seguiti da parentesi.
 *
 * ! E L'ELENCO DELLE FUNZIONI E' LA PROVA DI ex_area_vai. Si riconosce una
 * definizione con una regola grossolana — comincia a colonna zero, contiene una
 * parentesi, non finisce con il punto e virgola — che e' esattamente quel che
 * fara' l'IDE finche' non avra' di meglio. Cliccando una voce, il cursore ci
 * salta: senza ex_area_vai quell'elenco si poteva disegnare e non poteva fare
 * niente.
 * ========================================================================== */
static const char *const g_sorgente[] = {
"/* Un pezzo di C per provare i colori: questo commento",
"   continua sulla riga dopo, e deve restare verde fino a qui. */",
"#include <stdio.h>",
"#include \"exwin.h\"",
"",
"#define TETTO   4096        /* quanti byte al massimo */",
"",
"static const char *g_nome = \"EX-OS\";",
"static unsigned int g_conto = 0;",
"",
"/* Rende quanti caratteri ha la stringa, senza contare lo zero. */",
"unsigned int lunghezza(const char *s)",
"{",
"    unsigned int n = 0;",
"",
"    while (s[n] != '\\0') n++;      // si conta e basta",
"    return n;",
"}",
"",
"int somma_pari(const int *v, int quanti)",
"{",
"    int i, somma = 0;",
"",
"    for (i = 0; i < quanti; i++) {",
"        if (v[i] % 2 == 0) somma += v[i];",
"    }",
"    return somma;",
"}",
"",
"void saluta(void)",
"{",
"    printf(\"ciao da %s, giro numero %u\\n\", g_nome, g_conto);",
"    g_conto++;",
"}",
"",
"int main(int argc, char **argv)",
"{",
"    int numeri[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };",
"    double x = 3.14;",
"",
"    (void)argc; (void)argv;",
"    saluta();",
"    printf(\"pari: %d, nome lungo %u, x = %f\\n\",",
"           somma_pari(numeri, 8), lunghezza(g_nome), x);",
"    return 0;",
"}",
    0
};

static ExFinestra g_cod, g_funz;
static unsigned int g_funz_riga[64];    /* a quale riga porta la voce i-esima */
static unsigned int g_funz_n = 0;

/* La regola grossolana di cui sopra: una definizione comincia a colonna zero,
 * ha una parentesi, e non e' una dichiarazione (niente punto e virgola). */
static int sembra_una_funzione(const char *r)
{
    unsigned int n = (unsigned int)strlen(r);

    if (n == 0 || r[0] == ' ' || r[0] == '\t' || r[0] == '#') return 0;
    if (r[0] == '/' || r[0] == '*' || r[0] == '}' || r[0] == '{') return 0;
    if (strchr(r, '(') == 0) return 0;
    if (n > 0 && r[n - 1] == ';') return 0;
    return 1;
}

static long procedura_codice(ExFinestra f, unsigned int msg, unsigned int wp,
                             long lp)
{
    char riga[96];

    switch (msg) {
    case EXM_COMANDO:
        if (wp == ID_FUNZ) {
            unsigned int i = ex_lista_scelta(g_funz);

            if (i < g_funz_n) {
                ex_area_vai(g_cod, g_funz_riga[i], 0);
                ex_fuoco(g_cod);
                sprintf(riga, "winprova: salto alla riga %u",
                        g_funz_riga[i] + 1);
                log_seriale(riga);
                ex_procedura_base(f, EXM_DISEGNA, 0, 0);
            }
        }
        return 0;

    case EXM_CHIUDI:
        ex_esci(0);
        return 0;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

static int finestra_codice(void)
{
    ExFinestra f;
    unsigned int i;

    f = ex_crea("finestra", "Area del codice",
                EX_TITOLO | EX_BORDO | EX_CHIUDI, 40, 40, 720, 420,
                0, 0, procedura_codice);
    if (f == 0) {
        printf("winprova: il server a finestre non risponde.\n");
        printf("          Avvialo con:  exwin\n");
        return 1;
    }

    g_funz = ex_crea("lista", "", EX_FIGLIO, 6, 6, 180, 400, f, ID_FUNZ, 0);
    g_cod  = ex_crea("areacodice", "", EX_FIGLIO,
                     192, 6, 520, 400, f, ID_CODICE, 0);
    if (g_cod == 0) {
        printf("winprova: l'area del codice non si e' aperta\n");
        return 1;
    }

    /* ! IL COLORITORE SI ATTACCA PRIMA DI CARICARE, e non dopo: attaccarlo dopo
     * funzionerebbe lo stesso — la catena degli stati si rifa' da sola — ma
     * questo e' l'ordine che si legge, ed e' quello che l'IDE seguira'. */
    ex_area_colora(g_cod, ex_colora_c, 0);

    ex_area_svuota(g_cod);
    for (i = 0; g_sorgente[i]; i++) {
        if (!ex_area_aggiungi(g_cod, g_sorgente[i])) {
            printf("winprova: l'area e' piena alla riga %u\n", i);
            break;
        }
        if (sembra_una_funzione(g_sorgente[i]) &&
            g_funz_n < sizeof(g_funz_riga) / sizeof(g_funz_riga[0])) {
            g_funz_riga[g_funz_n++] = i;
            ex_lista_aggiungi(g_funz, g_sorgente[i]);
        }
    }
    ex_area_pulita(g_cod);
    ex_fuoco(g_cod);

    ex_procedura_base(f, EXM_DISEGNA, 0, 0);
    printf("winprova: area del codice, %u righe, %u funzioni nell'elenco.\n"
           "          Clicca una funzione a sinistra: il cursore ci salta.\n",
           ex_area_righe(g_cod), g_funz_n);
    return 0;
}

/* =============================================================================
 * LA PROVA DEL CONTENITORE MDI
 *
 * ! OGNI FINESTRA FIGLIA HA LA SUA PROCEDURA, ed e' la cosa che questa prova
 * deve far vedere: il comando di un pulsante dentro la finestra A arriva alla
 * procedura di A, non a quella dell'applicazione. Sulla seriale si legge QUALE
 * procedura ha ricevuto cosa, che e' un fatto e non un'impressione.
 *
 * ! E LA CHIUSURA SI INTERCETTA. La finestra C dice di no la prima volta e si
 * lascia chiudere la seconda: e' il caso vero — «hai delle modifiche non
 * salvate» — e la prova che EXM_CHIUDI arriva alla figlia e non spegne il
 * programma.
 * ========================================================================== */
static ExFinestra g_mdi_cont, g_mdi_a, g_mdi_b, g_mdi_c, g_mdi_eco;
static int g_c_insiste = 0;

static void mdi_dico(const char *chi, unsigned int wp, long lp)
{
    char riga[96];

    sprintf(riga, "winprova: %s: comando id=%u valore=%ld", chi, wp, lp);
    log_seriale(riga);
    ex_testo_metti(g_mdi_eco, riga + 10);
}

static long proc_a(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_COMANDO) { mdi_dico("finestra A", wp, lp); return 0; }
    return ex_procedura_base(f, msg, wp, lp);
}

static long proc_b(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_COMANDO) { mdi_dico("finestra B", wp, lp); return 0; }
    return ex_procedura_base(f, msg, wp, lp);
}

static long proc_c(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_COMANDO) { mdi_dico("finestra C", wp, lp); return 0; }

    if (msg == EXM_CHIUDI && !g_c_insiste) {
        g_c_insiste = 1;
        log_seriale("winprova: finestra C: la prima chiusura si rifiuta");
        ex_testo_metti(g_mdi_eco, "la finestra C ha detto di no: riprova");
        return 0;                       /* gestito: non si chiude */
    }
    return ex_procedura_base(f, msg, wp, lp);
}

static long procedura_mdi(ExFinestra f, unsigned int msg, unsigned int wp,
                          long lp)
{
    if (msg == EXM_COMANDO) { mdi_dico("applicazione", wp, lp); return 0; }
    if (msg == EXM_CHIUDI)  { ex_esci(0); return 0; }

    /* ! F6 GIRA FRA LE FINESTRE, e lo fa l'APPLICAZIONE con ex_mdi_attiva: il
     * toolkit non si prende una scorciatoia di tastiera per conto suo. Quale
     * tasto sia — F6, Ctrl+Tab, una voce di menu — e' una decisione di chi
     * scrive il programma, e il toolkit non deve indovinarla. */
    if (msg == EXM_TASTO && (wp & KBD_KEY_MASK) == KBD_K_F(6)) {
        ExFinestra ora = ex_mdi_attivo(g_mdi_cont);
        ExFinestra giro[3];
        int i, k = 0;

        giro[0] = g_mdi_a; giro[1] = g_mdi_b; giro[2] = g_mdi_c;
        for (i = 0; i < 3; i++) if (giro[i] == ora) k = i;
        for (i = 1; i <= 3; i++)
            if (giro[(k + i) % 3] != 0) {
                ex_mdi_attiva(giro[(k + i) % 3]);
                log_seriale("winprova: F6: girata la finestra attiva");
                break;
            }
        return 0;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

static int finestra_mdi(void)
{
    ExFinestra f;

    f = ex_crea("finestra", "Contenitore MDI",
                EX_TITOLO | EX_BORDO | EX_CHIUDI, 30, 30, 700, 470,
                0, 0, procedura_mdi);
    if (f == 0) {
        printf("winprova: il server a finestre non risponde.\n");
        printf("          Avvialo con:  exwin\n");
        return 1;
    }

    g_mdi_cont = ex_crea("mdi", "", EX_FIGLIO, 6, 6, 688, 430, f, 0, 0);
    if (g_mdi_cont == 0) {
        printf("winprova: il contenitore MDI non si e' aperto\n");
        return 1;
    }

    g_mdi_eco = ex_crea("etichetta", "nessun comando ancora", EX_FIGLIO,
                        8, 444, 680, 16, f, 0, 0);

    g_mdi_a = ex_crea("mdifiglio", "A - sorgente.c", EX_TITOLO | EX_CHIUDI,
                      10, 10, 320, 180, g_mdi_cont, 0, proc_a);
    ex_crea("pulsante", "compila", EX_FIGLIO, 12, 12, 90, 26,
            g_mdi_a, ID_BOT_A, 0);
    ex_crea("spunta", "salva prima", EX_FIGLIO, 12, 48, 180, 20,
            g_mdi_a, ID_SPU_A, 0);

    g_mdi_b = ex_crea("mdifiglio", "B - proprieta'", EX_TITOLO | EX_CHIUDI,
                      150, 90, 300, 200, g_mdi_cont, 0, proc_b);
    {
        /* ! LA LISTA E' PIU' ALTA DELL'AREA DEL CLIENT, ed e' apposta: senza
         * ritaglio si disegnerebbe oltre il telaio della finestra B e sopra a
         * quel che c'e' sotto. Tagliata al bordo di dentro, e' la prova che il
         * ritaglio del disegno funziona — e la prova che serve, perche' e'
         * l'unico modo di sbagliare che non si vede finche' non capita. */
        ExFinestra l = ex_crea("lista", "", EX_FIGLIO, 8, 8, 280, 220,
                               g_mdi_b, ID_LIS_B, 0);

        ex_lista_aggiungi(l, "nome");
        ex_lista_aggiungi(l, "posizione");
        ex_lista_aggiungi(l, "misura");
        ex_lista_aggiungi(l, "colore");
    }

    g_mdi_c = ex_crea("mdifiglio", "C - uscita", EX_TITOLO | EX_CHIUDI,
                      60, 230, 360, 150, g_mdi_cont, 0, proc_c);
    ex_crea("etichetta", "chiudimi: la prima volta dico di no", EX_FIGLIO,
            12, 14, 330, 16, g_mdi_c, 0, 0);
    ex_crea("pulsante", "esegui", EX_FIGLIO, 12, 40, 90, 26,
            g_mdi_c, ID_BOT_C, 0);

    ex_procedura_base(f, EXM_DISEGNA, 0, 0);
    printf("winprova: tre finestre dentro un contenitore MDI.\n"
           "          Trascina una barra del titolo, clicca una finestra\n"
           "          dietro, prova a chiudere la C.\n");
    return 0;
}

static long procedura(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    /* ! LE FRECCE ARRIVANO QUI ANCHE CON IL FUOCO IN UNA CASELLA, ed e'
     * voluto: una casella di testo consuma i caratteri stampabili, non i tasti
     * speciali. Vedi tasto_al_fuoco() in exwin.c. */
    case EXM_TASTO:
        switch (wp & KBD_KEY_MASK) {
        case KBD_K_RIGHT: ex_misura(f, g_w + PASSO, g_h); return 0;
        case KBD_K_LEFT:  ex_misura(f, g_w - PASSO, g_h); return 0;
        case KBD_K_DOWN:  ex_misura(f, g_w, g_h + PASSO); return 0;
        case KBD_K_UP:    ex_misura(f, g_w, g_h - PASSO); return 0;
        }
        break;

    case EXM_MISURA: {
        char riga[80];

        g_w = EX_X(lp);
        g_h = EX_Y(lp);
        rifai_disposizione(g_w, g_h);

        sprintf(riga, "winprova: adesso e' %dx%d", g_w, g_h);
        log_seriale(riga);
        return 0;
    }

    case EXM_COMANDO:
        g_premuti++;
        if (wp == ID_OK)      ex_testo_metti(g_etichetta, "premuto OK");
        if (wp == ID_ANNULLA) ex_testo_metti(g_etichetta, "premuto Annulla");
        printf("winprova: comando %u (premuti finora: %u)\n", wp, g_premuti);
        return 0;

    case EXM_CHIUDI:
        printf("winprova: chiusura chiesta dall'utente\n");
        ex_esci(0);
        return 0;
    }

    /* ! CIO' CHE NON SI GESTISCE VA ALLA BASE, e non si lascia cadere: e' la
     * base a disegnare i controlli e a ridisegnare la finestra. Una procedura
     * che restituisse sempre 0 darebbe una finestra vuota. */
    return ex_procedura_base(f, msg, wp, lp);
}

int main(int argc, char **argv)
{
    ExFinestra f, riq;
    ExMsg m;
    const char *sfondo = 0;
    int i, terminale = 0, nuovi = 0, codice = 0, mdi = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) sfondo = argv[++i];
        if (strcmp(argv[i], "-t") == 0) terminale = 1;
        if (strcmp(argv[i], "-n") == 0) nuovi = 1;
        if (strcmp(argv[i], "-c") == 0) codice = 1;
        if (strcmp(argv[i], "-m") == 0) mdi = 1;
    }

    if (nuovi) {
        if (finestra_nuovi() != 0) return 1;
        while (ex_prendi_msg(&m)) ex_smista(&m);
        return 0;
    }

    if (codice) {
        if (finestra_codice() != 0) return 1;
        while (ex_prendi_msg(&m)) ex_smista(&m);
        return 0;
    }

    if (mdi) {
        if (finestra_mdi() != 0) return 1;
        while (ex_prendi_msg(&m)) ex_smista(&m);
        return 0;
    }

    /* ! UNA SHELL DENTRO UNA FINESTRA, e non e' un esempio in piu': e' la
     * prova che una shell puo' vivere su una PIPE invece che sul tty della
     * console — cioe' che due shell grafiche non si contendono niente. */
    if (terminale) {
        ExFinestra ft, t;

        ft = ex_crea("finestra", "Terminale", EX_TITOLO | EX_BORDO | EX_CHIUDI,
                     60, 50, 640, 400, 0, 0, 0);
        if (ft == 0) {
            printf("winprova: il server a finestre non risponde.\n");
            return 1;
        }
        t = ex_crea("terminale", "/bin/sh", EX_FIGLIO, 2, 2, 636, 396, ft, 0, 0);
        if (t == 0) {
            printf("winprova: non riesco ad avviare la shell nella finestra\n");
            return 1;
        }

        ex_procedura_base(ft, EXM_DISEGNA, 0, 0);
        printf("winprova: terminale aperto, la shell gira dentro la finestra\n");

        while (ex_prendi_msg(&m)) ex_smista(&m);
        return 0;
    }

    /* Lo sfondo e' una finestra come le altre, con lo stile che la tiene
     * sotto: e' il motivo per cui un'immagine di scrivania non ha avuto
     * bisogno di un meccanismo suo. */
    if (sfondo) {
        unsigned int sw = 0, sh = 0;
        ExFinestra s;

        ex_schermo(&sw, &sh);
        s = ex_crea("finestra", "", EX_SFONDO, 0, 0, (int)sw, (int)sh,
                    0, 0, 0);
        if (s) {
            if (!ex_immagine(s, sfondo, 0, 0))
                printf("winprova: %s: formato non riconosciuto\n", sfondo);
            ex_aggiorna(s);
        }
    }

    /* ! EX_RIDIM SI CHIEDE, e chi lo chiede si impegna a rifare la propria
     * disposizione in EXM_MISURA. Qui e' anche la prova che quella catena —
     * presa, zona nuova, messaggio, ridisegno — sia intera. */
    f = ex_crea("finestra", "Prova del toolkit",
                EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_RIDIM,
                80, 60, g_w, g_h, 0, 0, procedura);
    if (f == 0) {
        /* ! IL CONSIGLIO E' «exwin», e le due cose che diceva prima erano
         * tutt'e due sbagliate: il percorso /cdrom/... non esiste quando si
         * avvia DAL CD (li' la radice e' il CD), e avviare wserver.drv a mano
         * lo fa nascere sulla console della shell, dove si contende la
         * tastiera con lei. Chi seguiva questo consiglio non arrivava da
         * nessuna parte, e dava la colpa al server. */
        printf("winprova: il server a finestre non risponde.\n");
        printf("          Avvialo con:  exwin\n");
        return 1;
    }

    g_int = ex_crea("intestazione", "Anagrafica", EX_FIGLIO,
                    0, 0, g_w, 22, f, 0, 0);

    ex_crea("etichetta", "Nome:", EX_FIGLIO,  14,  38,  60, 16, f, 0, 0);
    g_t1 = ex_crea("testo", "Graziano", EX_FIGLIO, 80, 34, g_w - 110, 22, f, 0, 0);

    ex_crea("etichetta", "Sistema:", EX_FIGLIO, 14,  70,  70, 16, f, 0, 0);
    g_t2 = ex_crea("testo", "EX-OS", EX_FIGLIO,  80,  66, g_w - 110, 22, f, 0, 0);

    g_sep = ex_crea("separatore", "", EX_FIGLIO, 14, 100, g_w - 28, 2, f, 0, 0);

    riq = ex_crea("riquadro", "Esito", EX_FIGLIO, 14, 112, g_w - 28, 50, f, 0, 0);
    g_riq = riq;
    g_etichetta = ex_crea("etichetta", "nessun pulsante premuto", EX_FIGLIO,
                          12, 22, g_w - 56, 16, riq, 0, 0);

    g_ok  = ex_crea("pulsante", "OK",      EX_FIGLIO,
                    g_w - 190, g_h - 42, 80, 26, f, ID_OK, 0);
    g_ann = ex_crea("pulsante", "Annulla", EX_FIGLIO,
                    g_w - 100, g_h - 42, 80, 26, f, ID_ANNULLA, 0);

    /* Il primo disegno: da qui in poi lo rifa' il ciclo dei messaggi. */
    ex_procedura_base(f, EXM_DISEGNA, 0, 0);

    printf("winprova: finestra aperta %dx%d. Le frecce la ridimensionano;\n"
           "          premi un pulsante o chiudi la finestra.\n", g_w, g_h);

    while (ex_prendi_msg(&m)) ex_smista(&m);

    ex_distruggi(f);
    printf("winprova: uscita, %u comandi ricevuti\n", g_premuti);
    return 0;
}

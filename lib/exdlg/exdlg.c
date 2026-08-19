/* =============================================================================
 * lib/exdlg/exdlg.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * I dialoghi «Apri» e «Salva con nome»
 *
 * ! IL CICLO DEI MESSAGGI E' ANNIDATO, non separato. Dentro c'e' lo stesso
 * ex_prendi_msg()/ex_smista() dell'applicazione, che continua a consegnare gli
 * eventi anche alle ALTRE finestre: senza, aprire un dialogo lascerebbe il
 * resto del programma senza ridisegni finche' non lo si chiude, e chi guarda
 * penserebbe che si e' bloccato.
 *
 * ! ED E' MODALE DAVVERO, dal 18 agosto 2026. Fino a quel giorno qui c'era
 * scritto che il server non sapeva cosa fosse una finestra modale, e che il
 * dialogo stava sopra ma i clic sotto arrivavano lo stesso. Adesso c'e'
 * WIN_ST_MODALE nel protocollo: finche' questa finestra e' aperta, le altre
 * dello STESSO processo non ricevono ne' clic ne' tasti, e chi clicca altrove
 * se la vede portare davanti invece di trovare un'applicazione sorda.
 *
 * ! L'ELENCO E' DISEGNATO A MANO, come nel file manager e nell'editor. E' la
 * TERZA volta, ed e' la conferma che al toolkit manca un controllo lista: qui
 * pero' la ripetizione finisce, perche' d'ora in poi chi vuole scegliere un
 * file chiama questa funzione invece di riscriverne una sua.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"

#define DLG_W       420
#define DLG_H       300
#define VOCI_MAX    256
#define PERC_MAX    192

#define AREA_X      6
#define AREA_Y      26
#define AREA_W      (DLG_W - 12)
#define AREA_H      192

#define ID_OK       1
#define ID_ANNULLA  2
#define ID_SU       3
#define ID_LISTA    4

/* ! SI TIENE SOLO CIO' CHE LA LISTA NON SA: per ogni voce, se e' una
 * directory — che e' quello che decide cosa succede all'Invio. Il testo e la
 * riga scelta li conserva il controllo. */
static unsigned char g_dir_flag[VOCI_MAX];
static char          g_voce_nome[VOCI_MAX][DIRENT_NAME_MAX];
static ExFinestra    g_lista;
static char         g_dir[PERC_MAX];
static char         g_nome[DIRENT_NAME_MAX];

static ExFinestra   g_f, g_casella;
static int          g_fatto;        /* 0 = ancora aperto, 1 = OK, 2 = annullato */
static int          g_salva;        /* 1 = dialogo di salvataggio */

/* -----------------------------------------------------------------------------
 * Leggere la directory. Le directory in cima, come nel file manager: in una
 * directory con cento file, quelle in cui si vuole entrare sarebbero sparse.
 * --------------------------------------------------------------------------- */
static void leggi(void)
{
    DirEntry v[32];
    int start = 0, n, i;
    unsigned int quante = 0, d = 0;

    ex_lista_svuota(g_lista);

    while ((n = listdir_from(g_dir, v, 32, start)) > 0) {
        for (i = 0; i < n && quante < VOCI_MAX; i++) {
            if (v[i].name[0] == '.' && v[i].name[1] == '\0') continue;
            strncpy(g_voce_nome[quante], v[i].name, DIRENT_NAME_MAX - 1);
            g_voce_nome[quante][DIRENT_NAME_MAX - 1] = '\0';
            g_dir_flag[quante] = v[i].is_dir;
            quante++;
        }
        start += n;
        if (n < 32) break;
    }

    /* Le directory in cima, tenendo l'ordine fra pari: in una directory con
     * cento file, quelle in cui si vuole entrare sarebbero sparse in mezzo. */
    for (i = 0; i < (int)quante; i++)
        if (g_dir_flag[i]) {
            char nome[DIRENT_NAME_MAX];
            int k;

            strcpy(nome, g_voce_nome[i]);
            for (k = i; k > (int)d; k--) {
                strcpy(g_voce_nome[k], g_voce_nome[k - 1]);
                g_dir_flag[k] = g_dir_flag[k - 1];
            }
            strcpy(g_voce_nome[d], nome);
            g_dir_flag[d] = 1;
            d++;
        }

    for (i = 0; i < (int)quante; i++) {
        char riga[80];

        if (g_dir_flag[i]) sprintf(riga, "[%s]", g_voce_nome[i]);
        else               sprintf(riga, " %s", g_voce_nome[i]);

        ex_lista_aggiungi(g_lista, riga);
    }
}

static void entra(const char *nome)
{
    if (strcmp(nome, "..") == 0) {
        int i = (int)strlen(g_dir);
        while (i > 1 && g_dir[i - 1] != '/') i--;
        if (i > 1) i--;
        if (i == 0) i = 1;
        g_dir[i] = '\0';
    } else {
        if (g_dir[strlen(g_dir) - 1] != '/') strcat(g_dir, "/");
        strncat(g_dir, nome, PERC_MAX - strlen(g_dir) - 1);
    }
    leggi();
}

/* Unisce directory corrente e nome in un percorso solo. */
static void componi(char *out, unsigned int max)
{
    unsigned int l;

    strncpy(out, g_dir, max - 1);
    out[max - 1] = '\0';
    l = (unsigned int)strlen(out);
    if (l && out[l - 1] != '/') { strcat(out, "/"); l++; }
    strncat(out, g_nome, max - l - 1);
}

/* ! L'ELENCO NON E' PIU' DISEGNATO A MANO. Fino al 17 agosto 2026 queste
 * trenta righe disegnavano le voci, la barra della scelta e lo scorrimento —
 * le stesse trenta che stavano nel file manager e, in forma appena diversa,
 * nell'editor. Adesso e' il controllo «lista» di ExWin, e qui resta solo il
 * percorso corrente, che e' roba del dialogo e non dell'elenco. */
static void percorso_disegna(void)
{
    ex_riempi(g_f, AREA_X, AREA_Y + AREA_H + 4, AREA_W, 16, EX_GRIGIO);
    ex_scrivi(g_f, AREA_X + 2, AREA_Y + AREA_H + 4, g_dir, EX_NERO);
}

static void ridisegna(void)
{
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    percorso_disegna();
    ex_aggiorna(g_f);
}

/* Il nome scelto va nella casella: e' li' che l'utente lo vede e lo corregge. */
static void nome_metti(const char *s)
{
    strncpy(g_nome, s, DIRENT_NAME_MAX - 1);
    g_nome[DIRENT_NAME_MAX - 1] = '\0';
    ex_testo_metti(g_casella, g_nome);
}

static void conferma(void)
{
    const char *t = ex_testo_prendi(g_casella);

    /* ! SI PRENDE QUELLO CHE C'E' NELLA CASELLA, non quello che era
     * selezionato: l'utente puo' averlo battuto a mano, ed e' l'unico modo di
     * dare un nome a un file che ancora non esiste. */
    if (t && t[0]) nome_metti(t);
    if (g_nome[0] == '\0') return;      /* niente nome, niente conferma */

    g_fatto = 1;
}

static void scegli(void)
{
    unsigned int s = ex_lista_scelta(g_lista);

    if (s >= ex_lista_quante(g_lista)) return;

    if (g_dir_flag[s]) {
        entra(g_voce_nome[s]);
        nome_metti("");
        return;
    }
    nome_metti(g_voce_nome[s]);
    conferma();
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_COMANDO:
        if (wp == ID_OK)      { conferma();      break; }
        if (wp == ID_ANNULLA) { g_fatto = 2; return 0; }
        if (wp == ID_SU)      { entra(".."); nome_metti(""); break; }

        /* ! LA LISTA MANDA IL SUO id, come un pulsante, e lp dice COME. Un clic
         * semplice sceglie e basta — un clic che entrasse subito renderebbe
         * impossibile scegliere senza aprire. Invio e DOPPIO CLIC invece
         * aprono: sono lo stesso gesto detto con due dispositivi diversi, e
         * EX_APRIRE(lp) li rende indistinguibili apposta.
         *
         * ! PRIMA DI OGGI L'INVIO QUI NON FACEVA NIENTE. Con la lista che
         * aveva i tasti, l'Invio diventava questo EXM_COMANDO — e lp veniva
         * buttato via. Chi sceglieva una directory e batteva Invio vedeva il
         * dialogo restare fermo, senza nemmeno un errore: la strada verso il
         * ramo EXM_TASTO qui sotto era chiusa dalla lista stessa. */
        if (wp == ID_LISTA) {
            unsigned int s = ex_lista_scelta(g_lista);

            if (s >= ex_lista_quante(g_lista)) break;

            /* Il nome di una directory non va nella casella: li' ci sta il
             * nome del FILE, e una directory non e' un nome da confermare. */
            if (!g_dir_flag[s]) nome_metti(g_voce_nome[s]);

            if (EX_APRIRE(lp)) scegli();
            break;
        }
        return 0;

    case EXM_TASTO:
        /* ! INVIO: SI CONFERMA QUELLO CHE SI VEDE SCRITTO. Se la casella ha un
         * nome, quello vince — e' l'unico modo di dare un nome a un file che
         * ancora non esiste. Se e' vuota, si apre cio' che e' scelto
         * nell'elenco. Le frecce non arrivano fin qui: le mangia la lista. */
        if ((wp & 0xFFFF) == '\n' || (wp & 0xFFFF) == '\r') {
            const char *s = ex_testo_prendi(g_casella);
            if (s && s[0]) { nome_metti(s); conferma(); }
            else           scegli();
            if (g_fatto) return 0;
            break;
        }
        return ex_procedura_base(f, msg, wp, lp);

    case EXM_CHIUDI:
        g_fatto = 2;
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }

    ridisegna();
    return 0;
}

/* -----------------------------------------------------------------------------
 * Il dialogo, che e' uno solo con due titoli
 * --------------------------------------------------------------------------- */
static int dialogo(char *percorso, unsigned int max, int salva)
{
    ExMsg        m;
    unsigned int sw = 0, sh = 0;
    int          x, y, i;

    g_salva = salva;
    g_fatto = 0;

    /* La directory di partenza viene da cio' che il chiamante propone. Un
     * dialogo che ricomincia sempre da / farebbe ripercorrere ogni volta la
     * stessa strada. */
    if (percorso && percorso[0]) {
        strncpy(g_dir, percorso, PERC_MAX - 1);
        g_dir[PERC_MAX - 1] = '\0';
        i = (int)strlen(g_dir);
        while (i > 1 && g_dir[i - 1] != '/') i--;
        nome_metti(g_dir + i);
        if (i > 1) i--;
        if (i == 0) i = 1;
        g_dir[i] = '\0';
    } else {
        strcpy(g_dir, "/");
        g_nome[0] = '\0';
    }

    /* In mezzo allo schermo: un dialogo nell'angolo si cerca. */
    ex_schermo(&sw, &sh);
    x = sw > DLG_W ? (int)(sw - DLG_W) / 2 : 0;
    y = sh > DLG_H ? (int)(sh - DLG_H) / 2 : 0;

    g_f = ex_crea("finestra", salva ? "Salva con nome" : "Apri",
                  EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_SOPRA | EX_MODALE,
                  x, y, DLG_W, DLG_H, 0, 0, proc);
    if (g_f == 0) return 0;

    g_lista = ex_crea("lista", "", EX_FIGLIO,
                      AREA_X, AREA_Y, AREA_W, AREA_H, g_f, ID_LISTA, 0);
    if (g_lista == 0) { ex_distruggi(g_f); return 0; }

    ex_crea("pulsante", "Su", EX_FIGLIO, AREA_X, 2, 44, 20, g_f, ID_SU, 0);
    ex_crea("etichetta", salva ? "Salva come:" : "Nome:", EX_FIGLIO,
            AREA_X, AREA_Y + AREA_H + 26, 90, 16, g_f, 0, 0);

    g_casella = ex_crea("testo", "", EX_FIGLIO,
                        AREA_X + 92, AREA_Y + AREA_H + 22,
                        AREA_W - 92, 22, g_f, 0, 0);

    ex_crea("pulsante", salva ? "Salva" : "Apri", EX_FIGLIO,
            DLG_W - 180, DLG_H - 32, 80, 24, g_f, ID_OK, 0);
    ex_crea("pulsante", "Annulla", EX_FIGLIO,
            DLG_W - 92, DLG_H - 32, 80, 24, g_f, ID_ANNULLA, 0);

    /* ! IL FUOCO SI CHIEDE, non si ottiene creando i controlli in un ordine
     * strano. Fino al 17 agosto 2026 la casella si creava PRIMA del pulsante
     * «Su» soltanto perche' ExWin dava il fuoco al primo controllo che lo
     * accettava: le righe qui sopra erano in un ordine che non e' quello in
     * cui si legge, e il commento prometteva di rimetterle a posto il giorno
     * che ci fosse stata ex_fuoco(). E' quel giorno.
     *
     * E il fuoco va alla casella e non al pulsante perche' in un dialogo di
     * scelta file la prima cosa che si fa e' battere un nome. */
    ex_fuoco(g_casella);

    ex_testo_metti(g_casella, g_nome);
    leggi();
    ridisegna();

    /* ! IL CICLO ANNIDATO. ex_prendi_msg() rende 0 solo se qualcuno ha chiamato
     * ex_esci(), cioe' se l'applicazione intera se ne sta andando: allora si
     * esce anche di qui, e si esce senza risultato. */
    while (!g_fatto && ex_prendi_msg(&m)) ex_smista(&m);

    ex_distruggi(g_f);
    g_f = 0;

    if (g_fatto != 1) return 0;

    componi(percorso, max);
    return 1;
}

int ex_dlg_apri(char *percorso, unsigned int max)  { return dialogo(percorso, max, 0); }
int ex_dlg_salva(char *percorso, unsigned int max) { return dialogo(percorso, max, 1); }

/* -----------------------------------------------------------------------------
 * L'avviso: una finestra, un testo, un pulsante
 * --------------------------------------------------------------------------- */
static int g_av_fatto;

static long av_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_COMANDO || msg == EXM_CHIUDI) { g_av_fatto = 1; return 0; }

    /* ! INVIO ED Esc CHIUDONO, e fino al 18 agosto 2026 non lo facevano: questo
     * dialogo aveva SOLO il pulsante OK, cioe' si chiudeva solo col mouse.
     * Su un avviso — dove non c'e' niente da decidere — il tasto che si batte
     * senza guardare e' proprio l'Invio, e non succedeva niente. Le altre due
     * finestre di ExDlg lo facevano gia': era questa a essere l'eccezione, e
     * un'eccezione che nessuno aveva scelto. */
    if (msg == EXM_TASTO) {
        unsigned int c = wp & 0xFFFF;

        if (c == '\n' || c == '\r' || c == 27) { g_av_fatto = 1; return 0; }
    }
    return ex_procedura_base(f, msg, wp, lp);
}

/* =============================================================================
 * ! UN AVVISO CHE TRONCA IL TESTO E' PEGGIO DI NESSUN AVVISO, e fino al 18
 * agosto 2026 questo lo troncava: un'etichetta sola, larga quanto la finestra,
 * e tutto quello che non ci stava spariva. Si e' visto la prima volta che
 * qualcuno ci ha messo dentro due frasi — le istruzioni di un programma — e
 * sullo schermo ne e' comparsa mezza, senza nessun segno che ce ne fosse
 * dell'altra. Chi legge non ha modo di sapere che manca qualcosa.
 *
 * ! SI SPEZZA SULLE PAROLE, NON SUI CARATTERI. Tagliare a meta' una parola e'
 * leggibile ma sembra un difetto; e quando una parola sola e' piu' lunga della
 * riga si taglia lo stesso, perche' l'alternativa e' farla uscire dal bordo.
 * ============================================================================= */
#define AVVISO_W        420
#define AVVISO_COL      ((AVVISO_W - 24) / 8)    /* caratteri per riga */
/* ! DODICI RIGHE E NON PIU' SEI. Sei bastavano finche' un avviso era una
 * frase; con «Informazioni su» — nome, a cosa serve, chi l'ha scritto, quanta
 * memoria occupa — il testo si troncava a meta' parola, e un dialogo che
 * TRONCA e' peggio di uno che non c'e': quello che si legge sembra tutto.
 * La finestra si misura gia' sul testo, quindi alzarlo non allarga niente per
 * chi ne usa due. */
#define AVVISO_RIGHE    12

static unsigned int spezza(const char *t, char righe[][AVVISO_COL + 1],
                           unsigned int max)
{
    unsigned int n = 0, i = 0;

    if (!t) return 0;

    while (t[i] && n < max) {
        unsigned int q = 0, ultimo = 0;
        int          a_capo = 0;

        while (t[i + q] && q < AVVISO_COL) {
            /* ! UN «\n» E' UN A CAPO VOLUTO, e va rispettato invece che
             * disegnato. Prima finiva nel testo come un carattere qualunque:
             * il font gli dava un glifo, e in mezzo alla frase comparivano due
             * pallini. Chi scrive un avviso su piu' righe se le aspetta. */
            if (t[i + q] == '\n') { a_capo = 1; break; }
            if (t[i + q] == ' ') ultimo = q;
            q++;
        }

        /* Se il testo continua e c'e' uno spazio dove tagliare, si taglia li'. */
        if (!a_capo && t[i + q] && ultimo > 0) q = ultimo;

        memcpy(righe[n], t + i, q);
        righe[n][q] = '\0';
        n++;

        i += q;
        if (a_capo) i++;                    /* si salta il «\n» stesso */
        while (t[i] == ' ') i++;
    }
    return n;
}

int ex_dlg_avviso(const char *titolo, const char *testo)
{
    ExFinestra   f;
    ExMsg        m;
    unsigned int sw = 0, sh = 0;
    int          x, y, alt;
    char         righe[AVVISO_RIGHE][AVVISO_COL + 1];
    unsigned int n, k;

    n = spezza(testo, righe, AVVISO_RIGHE);
    if (n == 0) n = 1;

    /* ! LA FINESTRA SI MISURA SUL TESTO, non il testo sulla finestra. */
    alt = 24 + (int)n * 16 + 12 + 24 + 16;

    ex_schermo(&sw, &sh);
    x = (int)sw > AVVISO_W ? (int)(sw - AVVISO_W) / 2 : 0;
    y = (int)sh > alt      ? (int)(sh - (unsigned int)alt) / 2 : 0;

    g_av_fatto = 0;
    f = ex_crea("finestra", titolo ? titolo : "Avviso",
                EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_SOPRA | EX_MODALE,
                x, y, AVVISO_W, alt, 0, 0, av_proc);
    if (f == 0) return 1;

    for (k = 0; k < n; k++)
        ex_crea("etichetta", righe[k], EX_FIGLIO,
                12, 20 + (int)k * 16, AVVISO_W - 24, 16, f, 0, 0);

    ex_crea("pulsante", "OK", EX_FIGLIO,
            (AVVISO_W - 80) / 2, alt - 40, 80, 24, f, 1, 0);

    ex_procedura_base(f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(f);

    while (!g_av_fatto && ex_prendi_msg(&m)) ex_smista(&m);

    ex_distruggi(f);
    return 1;
}

/* -----------------------------------------------------------------------------
 * La domanda: due pulsanti, e la risposta prudente
 * --------------------------------------------------------------------------- */
#define ID_SI   1
#define ID_NO   2

static int g_cf_fatto;      /* 0 = ancora niente, 1 = si', 2 = no */

/* Quanto dev'essere largo un pulsante per contenere la sua scritta. Il passo
 * del carattere e' otto pixel, e ai lati serve un po' d'aria. */
static int larghezza_pulsante(const char *t)
{
    int n = 0, w;

    while (t && t[n]) n++;
    w = n * 8 + 24;
    return w < 80 ? 80 : w;     /* mai piu' stretto di un pulsante normale */
}

static long cf_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_COMANDO:
        g_cf_fatto = (wp == ID_SI) ? 1 : 2;
        return 0;

    /* ! CHIUDERE LA FINESTRA E' «NO», NON «ANNULLA LA DOMANDA». La domanda
     * l'ha fatta il programma perche' deve decidere qualcosa: se questa
     * funzione potesse tornare senza risposta, ogni chiamante dovrebbe
     * inventarsi cosa fare in quel caso, e la meta' di loro sceglierebbe di
     * andare avanti. */
    case EXM_CHIUDI:
        g_cf_fatto = 2;
        return 0;

    case EXM_TASTO:
        /* Invio conferma, Esc rifiuta: sono le due che si battono senza
         * guardare, ed e' il motivo per cui i tasti vanno alla modale invece
         * di essere buttati. Un pulsante col fuoco NON consuma Invio (vedi
         * tasto_al_fuoco in exwin.c), quindi arriva fin qui. */
        if ((wp & 0xFFFF) == '\n' || (wp & 0xFFFF) == '\r') g_cf_fatto = 1;
        else if ((wp & 0xFFFF) == 27)                        g_cf_fatto = 2;
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }
}

int ex_dlg_conferma(const char *titolo, const char *testo,
                    const char *si, const char *no)
{
    ExFinestra   f;
    ExMsg        m;
    unsigned int sw = 0, sh = 0;
    int          x, y;

    ex_schermo(&sw, &sh);
    x = sw > 380 ? (int)(sw - 380) / 2 : 0;
    y = sh > 130 ? (int)(sh - 130) / 2 : 0;

    g_cf_fatto = 0;
    f = ex_crea("finestra", titolo ? titolo : "Conferma",
                EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_SOPRA | EX_MODALE,
                x, y, 380, 130, 0, 0, cf_proc);
    if (f == 0) return 0;       /* niente finestra, nessun consenso */

    ex_crea("etichetta", testo ? testo : "", EX_FIGLIO, 12, 28, 356, 16, f, 0, 0);

    /* ! I PULSANTI SI MISURANO SULLA LORO SCRITTA, e non e' pignoleria: la
     * prima prova di questo dialogo aveva «Torna al testo» che usciva dal
     * riquadro e finiva sopra lo sfondo della finestra. Una misura fissa va
     * bene finche' le scritte le sceglie chi ha scritto il dialogo — ma qui le
     * sceglie il CHIAMANTE, che non sa quanto e' largo il pulsante. */
    {
        const char *ts = si ? si : "Si'";
        const char *tn = no ? no : "No";
        int ws = larghezza_pulsante(ts);
        int wn = larghezza_pulsante(tn);
        int gap = 12;
        int x0  = (380 - (ws + gap + wn)) / 2;

        /* ! «NO» STA A DESTRA E PRENDE IL FUOCO. Il pulsante che ha il fuoco e'
         * quello che risponde a chi batte Invio senza leggere, e in un dialogo
         * che si apre prima di perdere qualcosa dev'essere quello che non
         * perde niente. */
        ex_crea("pulsante", ts, EX_FIGLIO, x0, 76, ws, 26, f, ID_SI, 0);
        ex_crea("pulsante", tn, EX_FIGLIO, x0 + ws + gap, 76, wn, 26, f, ID_NO, 0);
    }

    ex_procedura_base(f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(f);

    /* Come gli altri dialoghi: il ciclo e' suo, e continua a smistare agli
     * altri messaggi dell'applicazione — che il server, intanto, non manda
     * piu' a nessun'altra finestra di questo processo. */
    while (!g_cf_fatto && ex_prendi_msg(&m)) ex_smista(&m);

    ex_distruggi(f);
    return g_cf_fatto == 1;
}

/* -----------------------------------------------------------------------------
 * Una riga da scrivere: il dialogo piu' piccolo che serva a qualcosa
 *
 * ! NON E' ex_dlg_salva CON UN'ALTRA ETICHETTA. Quello mostra le directory,
 * perche' chi salva sceglie DOVE; qui si chiede una parola — un pezzo di nome
 * da cercare, un'etichetta, un numero — e un elenco di file accanto non
 * aiuterebbe, distrarrebbe.
 *
 * ! IL VALORE DI PARTENZA ENTRA ED ESCE DALLO STESSO BUFFER, come in
 * ex_dlg_apri: chi chiama ci mette quello che propone e ci ritrova quello che
 * e' stato battuto. Rende 0 se si e' annullato, e allora il buffer NON e'
 * stato toccato — chi non lo guarda si ritrova il valore di prima, che e'
 * l'unica cosa che non fa danni.
 * --------------------------------------------------------------------------- */
#define ID_RG_OK    1
#define ID_RG_NO    2

static int        g_rg_fatto;   /* 0 = niente, 1 = ok, 2 = annullato */
static ExFinestra g_rg_casella;

static long rg_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_COMANDO:
        g_rg_fatto = (wp == ID_RG_OK) ? 1 : 2;
        return 0;

    case EXM_CHIUDI:
        g_rg_fatto = 2;
        return 0;

    case EXM_TASTO:
        /* Invio conferma, Esc annulla. Una casella di testo NON consuma
         * l'Invio (vedi tasto_al_fuoco in exwin.c) proprio perche' arrivi
         * qui: e' il tasto con cui si finisce di scrivere. */
        if ((wp & 0xFFFF) == '\n' || (wp & 0xFFFF) == '\r') g_rg_fatto = 1;
        else if ((wp & 0xFFFF) == 27)                        g_rg_fatto = 2;
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }
}

int ex_dlg_riga(const char *titolo, const char *domanda,
                char *valore, unsigned int max)
{
    ExFinestra   f;
    ExMsg        m;
    unsigned int sw = 0, sh = 0;
    int          x, y;

    if (!valore || max == 0) return 0;

    ex_schermo(&sw, &sh);
    x = sw > 420 ? (int)(sw - 420) / 2 : 0;
    y = sh > 150 ? (int)(sh - 150) / 2 : 0;

    g_rg_fatto = 0;
    f = ex_crea("finestra", titolo ? titolo : "Scrivi",
                EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_SOPRA | EX_MODALE,
                x, y, 420, 150, 0, 0, rg_proc);
    if (f == 0) return 0;

    ex_crea("etichetta", domanda ? domanda : "", EX_FIGLIO,
            12, 28, 396, 16, f, 0, 0);

    g_rg_casella = ex_crea("testo", valore, EX_FIGLIO,
                           12, 50, 396, 24, f, 0, 0);

    {
        int ws = larghezza_pulsante("Va bene");
        int wn = larghezza_pulsante("Annulla");
        int gap = 12;
        int x0  = (420 - (ws + gap + wn)) / 2;

        ex_crea("pulsante", "Va bene", EX_FIGLIO, x0, 96, ws, 26, f, ID_RG_OK, 0);
        ex_crea("pulsante", "Annulla", EX_FIGLIO,
                x0 + ws + gap, 96, wn, 26, f, ID_RG_NO, 0);
    }

    /* ! IL FUOCO ALLA CASELLA, o si aprirebbe un dialogo in cui si chiede di
     * scrivere e battere non scrive niente. */
    ex_fuoco(g_rg_casella);

    ex_procedura_base(f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(f);

    while (!g_rg_fatto && ex_prendi_msg(&m)) ex_smista(&m);

    if (g_rg_fatto == 1) {
        const char *t = ex_testo_prendi(g_rg_casella);
        unsigned int i = 0;

        if (t) { for (i = 0; i + 1 < max && t[i]; i++) valore[i] = t[i]; }
        valore[i] = '\0';
    }

    ex_distruggi(f);
    return g_rg_fatto == 1;
}

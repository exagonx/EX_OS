/* =============================================================================
 * lib/exwin/extema.h
 * EX-OS — Extensible Operating System
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * I TEMI DI ExWin — come si disegnano i controlli e le finestre
 *
 * Oggi ogni controllo si disegna dentro disegna_oggetto_nudo() (exwin.c) con
 * colori fissi (EX_GRAY, EX_BLUE...) e il telaio delle finestre dentro
 * cornice() (wserver.c) con altri colori fissi (C_TELAIO, C_BARRA_ATT...).
 * Questo file separa le due cose:
 *
 *   - COSA si disegna e DOVE resta al toolkit e al server (misure, stato,
 *     testo, fuoco, ritagli);
 *   - COME si disegna lo decide un ExTema: una tavolozza, una forma (raggi,
 *     ombre, sfumature, stili) e, se serve, funzioni che sostituiscono quelle
 *     del motore per un controllo solo.
 *
 * ! IL MOTORE NON CONOSCE ExWindow NE' Oggetto. Lavora su una ExTela: pixel a
 * 32 bit (0x00RRGGBB, come dappertutto nel toolkit), passo, ritaglio. Cosi' lo
 * stesso codice gira:
 *     - nel client (exwin.c): la tela e' la zona condivisa della finestra,
 *       Oggetto->pix / passo_px, ritaglio = quello di ritaglia();
 *     - nel server (wserver.c): la tela e' il framebuffer composto, per il
 *       telaio e la barra del titolo;
 *     - sull'host (debug/grafica/anteprima.c): un buffer in memoria che
 *       diventa un PNG. Le immagini tema_NN_*.png sono uscite da QUESTO
 *       codice, non da un programma di disegno: cio' che si vede e' cio' che
 *       il toolkit fara'.
 *
 * ! NIENTE VIRGOLA MOBILE. Antialias degli angoli per sovracampionamento 4x4,
 * ombre con radice quadrata intera: il codice compila freestanding come il
 * resto di exwin.so, e non tocca lo stato della FPU di chi disegna.
 * ============================================================================= */
#ifndef EXTEMA_H
#define EXTEMA_H

#define EXTEMA_ABI 1u

/* -----------------------------------------------------------------------------
 * La tela
 * --------------------------------------------------------------------------- */
typedef struct {
    unsigned int *pix;      /* 0x00RRGGBB */
    int           w, h;     /* misura del buffer */
    int           passo;    /* pixel fra una riga e la successiva */
    int           cx0, cy0, cx1, cy1;   /* ritaglio: [cx0,cx1) x [cy0,cy1) */
} ExTela;

void ext_tela(ExTela *t, unsigned int *pix, int w, int h, int passo);
void ext_ritaglio(ExTela *t, int x, int y, int w, int h);   /* intersecato col buffer */
void ext_ritaglio_tutto(ExTela *t);

/* -----------------------------------------------------------------------------
 * Gli stati di un controllo (si combinano)
 * --------------------------------------------------------------------------- */
#define EXT_SOPRA       0x0001u     /* il mouse e' sopra (EXM_MOUSE_OVER)    */
#define EXT_PREMUTO     0x0002u     /* tenuto giu'                          */
#define EXT_SPENTO      0x0004u     /* EX_DISABLED                          */
#define EXT_FUOCO       0x0008u     /* ha la tastiera                       */
#define EXT_PREDEFINITO 0x0010u     /* il pulsante che Invio preme          */
#define EXT_SCELTO      0x0020u     /* spunta messa, radio acceso, riga scelta */
#define EXT_ATTIVA      0x0040u     /* finestra in primo piano              */
#define EXT_APERTO      0x0080u     /* combo aperto, menu aperto            */

/* -----------------------------------------------------------------------------
 * La tavolozza: tutti i colori che il motore usa. Nessun colore e' scritto
 * nel motore: se ne manca uno qui, manca il campo, non si inventa.
 * --------------------------------------------------------------------------- */
typedef struct {
    unsigned int desktop_a, desktop_b;      /* sfondo della scrivania (sfumato) */
    unsigned int fondo;                     /* area client, pannelli            */
    unsigned int fondo_alt;                 /* righe alterne, barra menu        */
    unsigned int testo, testo_spento;
    unsigned int campo, campo_testo;        /* caselle, liste                   */
    unsigned int campo_spento;
    unsigned int bordo;                     /* contorno dei controlli piatti    */
    unsigned int bordo_sopra;               /* contorno col mouse sopra         */
    unsigned int luce, ombra;               /* rilievo 3D (bordo3d = 1)         */
    unsigned int luce2, ombra2;             /* secondo anello del rilievo       */
    unsigned int accento, accento_testo;    /* selezione forte, predefinito     */
    unsigned int accento_chiaro;            /* selezione debole, sopra          */
    unsigned int fuoco;                     /* anello del fuoco                 */
    unsigned int puls_a, puls_b;            /* pulsante: sfumatura alto->basso  */
    unsigned int puls_sopra_a, puls_sopra_b;
    unsigned int puls_giu_a, puls_giu_b;
    unsigned int puls_testo;
    unsigned int barra_a, barra_b;          /* barra del titolo attiva          */
    unsigned int barra_ina_a, barra_ina_b;
    unsigned int titolo, titolo_ina;
    unsigned int telaio;                    /* il bordo della finestra          */
    unsigned int chiudi, chiudi_sopra;      /* pulsante di chiusura             */
    unsigned int riduci, ingrandisci;       /* colori dei due tondi (stile 1)   */
    unsigned int scorri_fondo, scorri_cursore, scorri_cursore_sopra;
    unsigned int menu_fondo, menu_testo, menu_sopra, menu_sopra_testo;
    unsigned int ombra_finestra;            /* colore dell'ombra (di solito nero) */
    unsigned int suggerimento, suggerimento_testo;
    unsigned int progresso_a, progresso_b;
} ExTavolozza;

/* -----------------------------------------------------------------------------
 * La forma
 * --------------------------------------------------------------------------- */
#define EXT_CHIUDI_QUADRO   0   /* quadrato in rilievo col quadratino rosso (oggi) */
#define EXT_CHIUDI_TONDI    1   /* tre tondi colorati a sinistra                   */
#define EXT_CHIUDI_PIATTO   2   /* X piatta, il fondo si accende col mouse sopra    */
#define EXT_CHIUDI_SEGNI    3   /* _ [] X disegnati con tratti spessi, in rilievo   */

#define EXT_LING_CLASSICA   0   /* linguette in rilievo, quella attiva piu' alta     */
#define EXT_LING_SOTTOLINEA 1   /* testo piatto, l'attiva sottolineata dall'accento  */
#define EXT_LING_PILLOLA    2   /* l'attiva e' una pillola piena                    */

#define EXT_FUOCO_PUNTINI   0   /* rettangolo punteggiato dentro il controllo       */
#define EXT_FUOCO_ANELLO    1   /* anello di 2 px del colore `fuoco` fuori          */
#define EXT_FUOCO_BORDO     2   /* il bordo stesso prende il colore `fuoco`         */

#define EXT_SFONDO_SFUMATO  0
#define EXT_SFONDO_RIGHE    1   /* righe orizzontali sottili (tessuto)              */
#define EXT_SFONDO_SCANSIONE 2  /* righe di scansione di un CRT                     */
#define EXT_SFONDO_SPAZZOLATO 3 /* metallo spazzolato (rumore orizzontale)          */
#define EXT_SFONDO_PUNTI    4   /* griglia di punti (carta millimetrata)            */

typedef struct {
    int raggio;             /* angoli di pulsanti, caselle, liste              */
    int raggio_finestra;    /* angoli alti della finestra                      */
    int bordo3d;            /* 1 = rilievo a due anelli, 0 = piatto con contorno */
    int spessore;           /* spessore del contorno (piatto) o del telaio     */
    int ombra_finestra;     /* px di ombra morbida, 0 = nessuna                */
    int ombra_alfa;         /* 0..255, quanto e' scura all'attacco             */
    int ombra_pulsante;     /* px di ombra sotto i pulsanti                    */
    int sfumato;            /* 1 = pulsanti e barra sfumati                    */
    int lucido;             /* 1 = riflesso bianco sulla meta' alta            */
    int barra_h;            /* altezza della barra del titolo                  */
    int titolo_centrato;
    int titolo_grassetto;
    int chiudi_stile;       /* EXT_CHIUDI_*                                    */
    int spunta_raggio;      /* angoli della casella di spunta                  */
    int scorri_largo;       /* larghezza della barra di scorrimento            */
    int scorri_frecce;      /* 1 = frecce agli estremi                         */
    int scorri_tondo;       /* 1 = cursore a pillola                           */
    int linguette;          /* EXT_LING_*                                      */
    int fuoco_stile;        /* EXT_FUOCO_*                                     */
    int sfondo;             /* EXT_SFONDO_* per la scrivania                   */
    int maiuscolo;          /* 1 = testo dei pulsanti in maiuscolo             */
    int bagliore;           /* px di alone dell'accento attorno al fuoco (neon) */
} ExForma;

/* -----------------------------------------------------------------------------
 * Il tema. Le funzioni sono facoltative: NULL = quella del motore.
 * --------------------------------------------------------------------------- */
struct ExTema;
typedef void (*ExtPulsanteFn)(ExTela *t, const struct ExTema *m, int x, int y,
                              int w, int h, const char *testo, unsigned int st);
typedef void (*ExtFinestraFn)(ExTela *t, const struct ExTema *m, int x, int y,
                              int w, int h, const char *titolo, unsigned int st);
typedef void (*ExtSfondoFn)(ExTela *t, const struct ExTema *m, int x, int y,
                            int w, int h);

typedef struct ExTema {
    unsigned int  abi;          /* EXTEMA_ABI */
    const char   *nome;         /* "scuro": il nome nel file di configurazione */
    const char   *titolo;       /* "Piatto scuro": il nome nel pannello        */
    ExTavolozza   c;
    ExForma       f;
    ExtPulsanteFn pulsante;
    ExtFinestraFn finestra;
    ExtSfondoFn   sfondo;
} ExTema;

/* -----------------------------------------------------------------------------
 * Le primitive (pubbliche: le usano anche i temi per le loro funzioni)
 * --------------------------------------------------------------------------- */
unsigned int ext_mescola(unsigned int a, unsigned int b, int alfa);   /* alfa 0..255 su a->b */
unsigned int ext_schiarisci(unsigned int c, int quanto);              /* +/- 0..255 */
void ext_punto(ExTela *t, int x, int y, unsigned int c, int alfa);
void ext_riempi(ExTela *t, int x, int y, int w, int h, unsigned int c);
void ext_riempi_alfa(ExTela *t, int x, int y, int w, int h, unsigned int c, int alfa);
void ext_contorno(ExTela *t, int x, int y, int w, int h, unsigned int c);
void ext_sfuma_v(ExTela *t, int x, int y, int w, int h, unsigned int a, unsigned int b);
void ext_sfuma_o(ExTela *t, int x, int y, int w, int h, unsigned int a, unsigned int b);
void ext_rilievo(ExTela *t, int x, int y, int w, int h, unsigned int luce, unsigned int ombra);
/* rettangoli arrotondati con antialias; r = 0 equivale a ext_riempi */
void ext_tondo_pieno(ExTela *t, int x, int y, int w, int h, int r,
                     unsigned int a, unsigned int b);          /* sfumato a->b */
void ext_tondo_bordo(ExTela *t, int x, int y, int w, int h, int r, int sp,
                     unsigned int c);
void ext_tondo_alfa(ExTela *t, int x, int y, int w, int h, int r,
                    unsigned int c, int alfa);
void ext_ombra(ExTela *t, int x, int y, int w, int h, int r, int raggio_ombra,
               int alfa, unsigned int c, int dy);
void ext_cerchio(ExTela *t, int cx, int cy, int r, unsigned int c);   /* pieno, AA */
void ext_anello(ExTela *t, int cx, int cy, int r, int sp, unsigned int c);
void ext_linea(ExTela *t, int x0, int y0, int x1, int y1, int sp, unsigned int c);
void ext_linea_alfa(ExTela *t, int x0, int y0, int x1, int y1, int sp,
                    unsigned int c, int alfa);
void ext_esagono(ExTela *t, int cx, int cy, int rx, int ry, int sp, unsigned int c, int alfa);
void ext_esagono_pieno(ExTela *t, int cx, int cy, int rx, int ry, unsigned int c, int alfa);
void ext_ellisse(ExTela *t, int cx, int cy, int rx, int ry, int da, int a, int sp,
                 unsigned int c, int alfa);
void ext_scintilla(ExTela *t, int cx, int cy, int r, unsigned int c);
void ext_triangolo(ExTela *t, int cx, int cy, int lato, int verso, unsigned int c); /* 0 giu',1 su,2 sx,3 dx */
int  ext_larg(const char *s);
void ext_testo(ExTela *t, int x, int y, const char *s, unsigned int c);
void ext_testo_grassetto(ExTela *t, int x, int y, const char *s, unsigned int c);
void ext_testo_centrato(ExTela *t, int x, int y, int w, int h, const char *s,
                        unsigned int c, int grassetto);

/* -----------------------------------------------------------------------------
 * I controlli (cio' che disegna_oggetto_nudo e cornice chiameranno)
 * --------------------------------------------------------------------------- */
void ext_scrivania(ExTela *t, const ExTema *m, int x, int y, int w, int h);
void ext_finestra(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                  const char *titolo, unsigned int st);
/* misura dell'area client dentro una finestra di (w, h) esterni */
void ext_finestra_client(const ExTema *m, int x, int y, int w, int h,
                         int *cx, int *cy, int *cw, int *ch);
void ext_pulsante(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                  const char *testo, unsigned int st);
/* the pieces the toolkit uses when it writes the text itself (exwin.so 0.026):
 * the button face (returns the caption colour), a field (text box, list,
 * combo: background and border), the two-ring relief */
unsigned int ext_pulsante_fondo(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                                unsigned int st);
void ext_campo(ExTela *t, const ExTema *m, int x, int y, int w, int h, unsigned int st);
void ext_bordo3d(ExTela *t, const ExTema *m, int x, int y, int w, int h, int incavato);
void ext_casella(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                 const char *testo, int cursore, unsigned int st); /* cursore: -1 = no */
void ext_spunta(ExTela *t, const ExTema *m, int x, int y, const char *testo,
                unsigned int st);
void ext_radio(ExTela *t, const ExTema *m, int x, int y, const char *testo,
               unsigned int st);
void ext_combo(ExTela *t, const ExTema *m, int x, int y, int w, int h,
               const char *testo, unsigned int st);
void ext_lista(ExTela *t, const ExTema *m, int x, int y, int w, int h,
               const char *const *righe, int n, int scelta, int sopra,
               int prima, unsigned int st);
void ext_scorri(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                int verticale, int pos, int pagina, int totale, unsigned int st);
void ext_linguette(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                   const char *const *voci, int n, int attiva, int sopra);
void ext_riquadro(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                  const char *titolo);
void ext_intestazione(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                      const char *testo);
void ext_separatore(ExTela *t, const ExTema *m, int x, int y, int w);
void ext_barra_menu(ExTela *t, const ExTema *m, int x, int y, int w,
                    const char *const *voci, int n, int aperta);
void ext_tendina(ExTela *t, const ExTema *m, int x, int y, int w,
                 const char *const *voci, int n, int sopra);   /* "-" = separatore */
void ext_progresso(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                   int permille);
void ext_suggerimento(ExTela *t, const ExTema *m, int x, int y, const char *testo);

/* altezza di una voce di menu e di una riga di lista */
#define EXT_RIGA_H   20
#define EXT_MENU_H   22

/* -----------------------------------------------------------------------------
 * I temi compilati dentro (debug/grafica/tema_NN_*.c)
 * --------------------------------------------------------------------------- */
extern const ExTema extema_classico, extema_cristallo, extema_ghiaccio,
                    extema_circuito, extema_chiaro, extema_scuro, extema_vetro,
                    extema_pastello, extema_ambra, extema_metallo;

/* seno intero: ext_seno(gradi) in millesimi (-1000..1000), per orbite,
 * esagoni e animazioni senza virgola mobile */
int ext_seno(int gradi);
int ext_coseno(int gradi);

const ExTema *extema_per_nome(const char *nome);   /* NULL se non c'e' */
const ExTema *extema_elenco(int i);                /* i-esimo, NULL dopo l'ultimo */

#endif /* EXTEMA_H */

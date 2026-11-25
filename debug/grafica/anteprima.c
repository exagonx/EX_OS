/* =============================================================================
 * debug/grafica/anteprima.c — le immagini dei temi, fatte dal codice dei temi
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Gira sull'HOST (gcc normale). Disegna con extema.c la stessa scena per ogni
 * tema e la scrive come PPM (P6); fa_anteprime.sh la converte in PNG.
 *
 *     anteprima <indice 0..9> <uscita.ppm>
 *
 * La scena prova ogni controllo in ogni stato che il toolkit conosce:
 * finestra attiva e inattiva, barra dei menu con una tendina aperta,
 * linguette (attiva, sotto il mouse), riquadro con spunte e radio (scelti,
 * sotto il mouse, spenti), caselle (col fuoco e il cursore, spenta),
 * elenco a discesa, lista con riga scelta e riga sotto il mouse, barre di
 * scorrimento verticale e orizzontale, intestazione, progresso,
 * separatore, pulsanti (predefinito col fuoco, normale, sotto il mouse,
 * premuto, spento) e un suggerimento.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "extema.h"
#include "colori/excolori.h"

#define W 860
#define H 600

static unsigned int g_pix[W * H];

static void scena(const ExTema *m)
{
    ExTela t;
    int cx, cy, cw, ch;
    static const char *const menu[] = { "File", "Modifica", "Vista", "Strumenti", "?" };
    static const char *const tend[] = { "Copia", "Incolla", "-", "Proprieta'" };
    static const char *const ling[] = { "Generale", "Aspetto", "Avanzate" };
    static const char *const righe[] = { "documento.txt", "foto_vacanze.png", "progetto.dis",
        "relazione.rtf", "musica.ogg", "archivio.zip", "note.md", "rubrica.csv", "script.sh" };

    ext_tela(&t, g_pix, W, H, W);
    ext_scrivania(&t, m, 0, 0, W, H);

    /* la finestra inattiva, dietro */
    ext_finestra(&t, m, 24, 20, 330, 150, "Finestra inattiva", 0);
    ext_finestra_client(m, 24, 20, 330, 150, &cx, &cy, &cw, &ch);
    ext_testo(&t, cx + 12, cy + 12, "Il fuoco e' sull'altra.", m->c.testo_spento);

    /* la finestra attiva; il pulsante di chiusura col mouse sopra */
    ext_finestra(&t, m, 70, 60, 740, 500, "Prova del tema", EXT_ATTIVA | EXT_SOPRA);
    ext_finestra_client(m, 70, 60, 740, 500, &cx, &cy, &cw, &ch);

    ext_barra_menu(&t, m, cx, cy, cw, menu, 5, 1);

    ext_linguette(&t, m, cx + 12, cy + 34, cw - 24, 28, ling, 3, 1, 2);

    /* colonna sinistra */
    ext_riquadro(&t, m, cx + 12, cy + 76, 260, 150, "Opzioni");
    ext_spunta(&t, m, cx + 26, cy + 100, "Mostra le icone", EXT_SCELTO);
    ext_spunta(&t, m, cx + 26, cy + 124, "Suoni di sistema", EXT_SOPRA);
    ext_spunta(&t, m, cx + 26, cy + 148, "Non disponibile", EXT_SPENTO | EXT_SCELTO);
    ext_radio(&t, m, cx + 26, cy + 176, "Piccole", EXT_SCELTO);
    ext_radio(&t, m, cx + 136, cy + 176, "Grandi", EXT_SOPRA);
    ext_radio(&t, m, cx + 26, cy + 198, "Spento", EXT_SPENTO);

    ext_lista(&t, m, cx + 12, cy + 238, 260 - 14, 168, righe, 9, 2, 4, 0, 0);
    ext_scorri(&t, m, cx + 12 + 260 - 14 - (m->f.scorri_largo + 2), cy + 240,
               m->f.scorri_largo, 164, 1, 2, 7, 9, EXT_SOPRA);

    /* colonna destra */
    ext_testo(&t, cx + 300, cy + 84, "Nome:", m->c.testo);
    ext_casella(&t, m, cx + 380, cy + 78, 300, 26, "Mario Rossi", 11, EXT_FUOCO);
    ext_testo(&t, cx + 300, cy + 118, "Codice:", m->c.testo);
    ext_casella(&t, m, cx + 380, cy + 112, 300, 26, "non modificabile", -1, EXT_SPENTO);
    ext_testo(&t, cx + 300, cy + 152, "Lingua:", m->c.testo);
    ext_combo(&t, m, cx + 380, cy + 146, 300, 26, "Italiano", EXT_SOPRA);

    ext_intestazione(&t, m, cx + 300, cy + 190, 380, 26, "Avanzamento");
    ext_progresso(&t, m, cx + 300, cy + 226, 380, 22, 620);
    ext_separatore(&t, m, cx + 300, cy + 262, 380);
    ext_scorri(&t, m, cx + 300, cy + 274, 380, m->f.scorri_largo, 0, 3, 10, 30, 0);

    ext_testo(&t, cx + 300, cy + 304, "Tema:", m->c.testo);
    ext_testo_grassetto(&t, cx + 350, cy + 304, m->titolo, m->c.accento);

    /* i pulsanti, in basso a destra */
    {
        int by = cy + ch - 46, bx = cx + 28;
        ext_pulsante(&t, m, bx,       by, 100, 30, "OK", EXT_PREDEFINITO | EXT_FUOCO);
        ext_pulsante(&t, m, bx + 116, by, 100, 30, "Annulla", 0);
        ext_pulsante(&t, m, bx + 232, by, 100, 30, "Applica", EXT_SOPRA);
        ext_pulsante(&t, m, bx + 348, by, 100, 30, "Premuto", EXT_PREMUTO);
        ext_pulsante(&t, m, bx + 464, by, 100, 30, "Spento", EXT_SPENTO);
        ext_suggerimento(&t, m, bx + 250, by - 30, "Salva senza chiudere");
    }

    /* un menu contestuale (clic destro), sopra a tutto, con la seconda voce
     * sotto il mouse: messo dove non copre altri controlli della prova */
    ext_tendina(&t, m, cx + 500, cy + 292, 170, tend, 4, 1);
}

/* Solo la scrivania, con una finestra piccola in un angolo: per i temi il
 * cui sfondo e' un disegno (cristallo, ghiaccio, circuito), che nella scena
 * piena resta coperto. */
static void scrivania(const ExTema *m)
{
    ExTela t;
    int cx, cy, cw, ch;

    ext_tela(&t, g_pix, W, H, W);
    ext_scrivania(&t, m, 0, 0, W, H);
    ext_finestra(&t, m, 24, 24, 260, 120, "Benvenuto in EX-OS", EXT_ATTIVA);
    ext_finestra_client(m, 24, 24, 260, 120, &cx, &cy, &cw, &ch);
    ext_testo(&t, cx + 12, cy + 12, "Sfondo del tema:", m->c.testo);
    ext_testo_grassetto(&t, cx + 12, cy + 32, m->titolo, m->c.accento);
}

int main(int argc, char **argv)
{
    const ExTema *m;
    FILE *f;
    int i;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "uso: anteprima <0..9> <uscita.ppm> [scrivania]\n");
        return 2;
    }
    /* un file .colori: il tema classico coi colori di quello schema */
    if (strlen(argv[1]) > 7 && !strcmp(argv[1] + strlen(argv[1]) - 7, ".colori")) {
        static ExTema vivo;
        static ExColori sc;
        static char buf[4096];
        FILE *cf = fopen(argv[1], "rb");
        size_t n;

        if (!cf) { perror(argv[1]); return 1; }
        n = fread(buf, 1, sizeof(buf) - 1, cf);
        fclose(cf);
        buf[n] = 0;
        excol_da_tema(&sc, &extema_classico);
        if (excol_leggi(&sc, buf, (int)n)) fprintf(stderr, "%s: righe non capite\n", argv[1]);
        excol_applica(&sc, &extema_classico, &vivo);
        m = &vivo;
    } else {
        m = extema_elenco(atoi(argv[1]));
    }
    if (!m) { fprintf(stderr, "tema %s non c'e'\n", argv[1]); return 1; }
    if (argc == 4) {
        if (!m->sfondo) return 1;               /* sfondo semplice: niente da mostrare */
        scrivania(m);
    } else {
        scena(m);
    }
    f = fopen(argv[2], "wb");
    if (!f) { perror(argv[2]); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (i = 0; i < W * H; i++) {
        unsigned char p[3] = { (unsigned char)(g_pix[i] >> 16), (unsigned char)(g_pix[i] >> 8),
                               (unsigned char)g_pix[i] };
        fwrite(p, 1, 3, f);
    }
    fclose(f);
    printf("%s\n", m->nome);
    return 0;
}

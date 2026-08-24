/* =============================================================================
 * bin/gfedit/gf_main.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * GF Edit — costruttore, ciclo principale, avvio.
 *
 * Il modello a "oggetto" è quello dell'originale: gf_costruttore()
 * restituisce un GfEdit con dentro i puntatori a funzione run e
 * newopenfile, e da fuori si usa così:
 *
 *     GfEdit *ed = gf_costruttore();
 *     ed->newopenfile(ed, "/prova.txt");
 *     ed->run(ed);
 *     gf_distruttore(ed);
 * ============================================================================= */

#include "gfedit.h"

/* +0.001 a ogni modifica: `gfedit -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("gfedit", "0.001");

/* =============================================================================
 * L'oggetto vive in BSS, non nello heap.
 *
 * Non è un'economia di malloc: la free() di EX-OS è un no-op, quindi un
 * oggetto allocato e "distrutto" resterebbe comunque lì. Tenerlo in BSS
 * lo rende esplicito — e la BSS non occupa spazio sul floppy, il
 * caricatore ELF la mappa azzerata (vedi p_memsz in kernel/loader/elf.c).
 * I 120 KB per area, che sono il vero peso, restano su malloc perché
 * vanno pagati solo dalle aree davvero aperte.
 * ============================================================================= */
static GfEdit g_editor;

/* =============================================================================
 * gf_run — il ciclo principale
 *
 * Disegna, aspetta un tasto, lo esegue. Il disegno viene PRIMA
 * dell'attesa e non dopo l'esecuzione: così lo schermo è già corretto
 * mentre l'utente pensa, e i dialoghi — che disegnano da soli prima di
 * aprirsi — non lasciano mai un fotogramma vecchio a video.
 * ============================================================================= */
void gf_run(GfEdit *self)
{
    self->running = 1;
    self->ultimo_autosave_ms = uptime_ms();

    while (self->running) {
        unsigned key;

        gf_disegna(self);

        /* =================================================================
         * Attesa con scadenza, non attesa e basta.
         *
         * Con una ipc_recv senza scadenza l'editor restava fermo finche'
         * l'utente non premeva qualcosa, e la barra di stato — orologio
         * compreso — si aggiornava solo in quell'istante. Mezzo secondo
         * e' abbastanza fitto perche' i secondi non saltino mai e
         * abbastanza rado da costare nulla: fra un risveglio e l'altro
         * il processo e' BLOCKED e non consuma un tick.
         * ================================================================= */
        key = gf_getkey_timeout(GF_TICK_MS);

        if (key == GF_KEY_SCADUTA) continue;   /* si ridisegna e si riaspetta */

        if (key == GF_KEY_ERRORE) {
            /* Il servizio tastiera non risponde più: continuare
             * significherebbe un ciclo a vuoto con lo schermo pieno e
             * nessun modo di uscire. */
            self->running = 0;
            break;
        }

        gf_tasto(self, key);

        /* =================================================================
         * Autosalvataggio a orologio, non a thread.
         *
         * L'originale contava i secondi in un thread dedicato. Qui il
         * controllo sta nel ciclo dei tasti, e la differenza si vede: se
         * l'utente non tocca nulla per un'ora, il salvataggio non
         * scatta. È coerente con ciò che l'autosalvataggio serve a
         * proteggere — il lavoro appena fatto — e non c'è un secondo
         * flusso di esecuzione da cui potrebbe scattare.
         *
         * L'aritmetica è su DIFFERENZE senza segno: uptime_ms() torna a
         * zero dopo ~24,8 giorni e 'ora - inizio' attraversa il wrap
         * correttamente (vedi il commento su uptime_ms in libc.h).
         * ================================================================= */
        if (self->opz.autosave_sec > 0) {
            unsigned ora = uptime_ms();

            if (ora - self->ultimo_autosave_ms >=
                (unsigned)self->opz.autosave_sec * 1000u) {
                int i, salvate = 0;

                for (i = 0; i < GF_MAX_TABS; i++) {
                    GfTab *t = &self->tabs[i];
                    if (!t->in_use || !t->modified || !t->has_path) continue;
                    if (gf_salva(t) == 0) salvate++;
                }

                self->ultimo_autosave_ms = ora;
                if (salvate > 0) {
                    char m[GF_COLS];
                    gf_fmt(m, sizeof(m), "Autosalvataggio: %d file salvati", salvate);
                    gf_msg(self, m);
                }
            }
        }
    }
}

/* =============================================================================
 * gf_newopenfile — apre un file, o ne prepara uno nuovo con quel nome
 *
 * Se il file non esiste NON è un errore: si prepara un'area vuota già
 * destinata a quel percorso, che verrà creato al primo salvataggio. È
 * il comportamento dell'originale, ed è quello che ci si aspetta
 * scrivendo `gfedit note.txt` per un file che ancora non c'è.
 * ============================================================================= */
void gf_newopenfile(GfEdit *self, const char *filepath)
{
    int    idx = gf_tab_libera_indice(self);
    GfTab *t;
    int    r;

    if (idx < 0) return;

    t = &self->tabs[idx];
    r = gf_carica(t, filepath);
    if (r == -2) return;              /* memoria esaurita */

    gf_strlcpy(t->filepath, filepath, GF_MAX_PATH);
    gf_strlcpy(t->filename, gf_basename(filepath), GF_MAX_NAME);
    t->has_path = 1;
    t->lingua   = gf_rileva_lingua(filepath);
    t->in_use   = 1;

    self->tab_corrente = idx;
    self->n_aperte++;

    if (r == -1) gf_msg(self, "File nuovo: verra' creato al primo salvataggio");

    gf_ricalcola_commenti(t);
}

/* =============================================================================
 * Costruttore e distruttore
 * ============================================================================= */
GfEdit *gf_costruttore(void)
{
    GfEdit *self = &g_editor;
    int     i;

    memset(self, 0, sizeof(*self));

    self->opz.tab_width    = GF_DEFAULT_TAB;
    self->opz.developing   = 1;
    self->opz.autosave_sec = 0;

    for (i = 0; i < GF_MAX_TABS; i++) self->tabs[i].in_use = 0;

    self->tab_corrente = 0;
    self->n_aperte     = 0;
    self->find.ignora_caso = 1;

    if (!getcwd(self->directory, GF_MAX_PATH)) {
        gf_strlcpy(self->directory, "/", GF_MAX_PATH);
    }

    self->run         = gf_run;
    self->newopenfile = gf_newopenfile;
    return self;
}

void gf_distruttore(GfEdit *self)
{
    /* Niente free(): quella di EX-OS è un no-op dichiarato, e fingere di
     * liberare darebbe l'impressione sbagliata che la memoria torni
     * disponibile. Il processo sta per uscire e il kernel smonta la sua
     * page directory per intero: è quello il vero rilascio. */
    (void)self;
}

/* =============================================================================
 * Avvio
 * ============================================================================= */
static void uso(void)
{
    printf("%s %s - editor di testo a schermo intero\n", GF_NAME, GF_VERSION);
    printf("Uso:\n");
    printf("  gfedit                apre un'area vuota\n");
    printf("  gfedit <file> [...]   apre uno o piu' file (max %d)\n", GF_MAX_TABS);
    printf("\n");
    printf("Dentro l'editor: F1 aiuto, F10 menu, Alt+X per uscire.\n");
}

int main(int argc, char **argv)
{
    GfEdit *ed;
    int     i;

    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        uso();
        return 0;
    }

    /* La console va messa in modalità raw PRIMA di costruire l'oggetto:
     * se il servizio tastiera non c'è, l'editor non deve nemmeno
     * allocare — si esce dicendo perché, invece di riempire lo schermo
     * di un'interfaccia che non risponderebbe a un tasto. */
    {
        int r = gf_term_init();

        if (r == -2) {
            printf("%s: non posso girare in background.\n", GF_NAME);
            printf("Prendo la tastiera per intero, e da '&' la ruberei alla\n");
            printf("shell, che resterebbe bloccata senza piu' un prompt.\n");
            printf("Lancialo in primo piano, oppure aprilo su un'altra\n");
            printf("console con Alt+F2..F4.\n");
            return 1;
        }

        if (r != 0) {
            printf("%s: il servizio tastiera '%s' non e' disponibile.\n",
                   GF_NAME, KBD_SERVICE_NAME);
            printf("Un editor a schermo intero ha bisogno dei tasti uno per uno,\n");
            printf("e la tastiera in-kernel di ripiego consegna solo righe intere.\n");
            printf("Usa /bin/textline, che e' fatto apposta per quel modello.\n");
            return 1;
        }
    }

    ed = gf_costruttore();

    for (i = 1; i < argc && ed->n_aperte < GF_MAX_TABS; i++) {
        ed->newopenfile(ed, argv[i]);
    }

    if (ed->n_aperte == 0) gf_az_nuovo(ed);

    ed->run(ed);

    gf_distruttore(ed);
    gf_term_fine();

    printf("%s %s - sessione terminata.\n", GF_NAME, GF_VERSION);
    return 0;
}

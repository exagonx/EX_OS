/* ============================================================================
 * gf_texteditor.c
 *
 * Parte dell'implementazione di GF_TEXTEDITOR - vedi gf_texteditor.h per
 * la descrizione dell'oggetto e gf_internal.h per le strutture e i
 * prototipi condivisi tra i moduli.
 *
 * Lettura tasti di basso livello (incluse le sequenze di escape non
 * standard), stato hardware CAPS/NUM lock, thread orologio (autosave
 * incluso), ciclo principale, costruttore e distruttore dell'oggetto.
 *
 * Autore : Graziano Falcone  <exagonx@hotmail.com>
 * Licenza: GNU GPL v2 (vedi Help->License nel programma, e il file
 *          COPYING distribuito insieme al codice sorgente)
 * ==========================================================================*/

#define _GNU_SOURCE
#include "gf_internal.h"

/* localtime "thread-safe" portabile: POSIX ha localtime_r(t, out), Windows
 * ha localtime_s(out, t) con gli argomenti invertiti. Usata dal thread
 * orologio e dal costruttore per il valore iniziale sincrono. */
#if defined(GF_ON_WINDOWS)
static void gf_localtime_safe(const time_t *t, struct tm *out) { localtime_s(out, t); }
#else
static void gf_localtime_safe(const time_t *t, struct tm *out) { localtime_r(t, out); }
#endif

int gf_getch_extended(void)
{
    int ch = wgetch(stdscr);
    if (ch != 27) return ch;

    wtimeout(stdscr, 0);      /* lettura immediata non bloccante */
    int c1 = wgetch(stdscr);
    if (c1 == ERR) { wtimeout(stdscr, GF_MAIN_TIMEOUT_MS); /* ripristina il timeout periodico */ return 27; }

    if (c1 == '[') {
        char seq[16]; int n = 0, c;
        while (n < 15 && (c = wgetch(stdscr)) != ERR) {
            seq[n++] = (char)c;
            if (isalpha(c) || c == '~') break;
        }
        seq[n] = '\0';
        wtimeout(stdscr, GF_MAIN_TIMEOUT_MS); /* ripristina il timeout periodico */

        if (!strcmp(seq, "1;5H")) return GF_KEY_CTRL_HOME;
        if (!strcmp(seq, "1;5F")) return GF_KEY_CTRL_END;
        if (!strcmp(seq, "1;2H")) return GF_KEY_SHIFT_HOME;
        if (!strcmp(seq, "1;2F")) return GF_KEY_SHIFT_END;
        if (!strcmp(seq, "1;2R")) return GF_KEY_SHIFT_F3;
        return 27;
    }

    /* Alt+lettera: sui terminali xterm-compatibili (xterm, gnome-terminal,
     * konsole, alacritty, kitty, ecc.) con la modalita' "8-bit meta"
     * disattivata - che e' l'impostazione predefinita - Alt+tasto viene
     * inviato come ESC seguito immediatamente dal carattere del tasto.
     * Il nodelay() sopra distingue questo caso da una pressione di solo
     * Esc seguita, istanti dopo, dalla digitazione libera della stessa
     * lettera: in quel caso il secondo carattere non arriva abbastanza
     * in fretta e viene semplicemente letto al giro successivo. */
    wtimeout(stdscr, GF_MAIN_TIMEOUT_MS); /* ripristina il timeout periodico */
    switch (c1) {
        case 'f': case 'F': return GF_KEY_ALT_F;
        case 'm': case 'M': return GF_KEY_ALT_M;
        case 'o': case 'O': return GF_KEY_ALT_O;
        case 'h': case 'H': return GF_KEY_ALT_H;
        default: return 27;
    }
}

/* ============================================================================
 *  STATO TASTI DI STATO (NUM/CAPS LOCK) VIA ioctl SULLA CONSOLE LINUX
 * ==========================================================================*/
#if defined(GF_ON_LINUX)
int gf_get_kb_led_state(int *caps_on, int *num_on)
{
    char led;
    if (ioctl(STDIN_FILENO, KDGKBLED, &led) < 0) return 0;
    *caps_on = (led & LED_CAP) ? 1 : 0;
    *num_on  = (led & LED_NUM) ? 1 : 0;
    return 1;
}
#elif defined(GF_ON_WINDOWS)
int gf_get_kb_led_state(int *caps_on, int *num_on)
{
    /* Su Windows lo stato dei tasti toggle si legge con GetKeyState():
     * il bit meno significativo del valore restituito e' acceso quando
     * il tasto e' "attivo" (toggled on). Funziona in modo affidabile
     * anche da console, a differenza dell'equivalente Linux via ioctl. */
    *caps_on = (GetKeyState(VK_CAPITAL) & 0x0001) ? 1 : 0;
    *num_on  = (GetKeyState(VK_NUMLOCK) & 0x0001) ? 1 : 0;
    return 1;
}
#else
int gf_get_kb_led_state(int *caps_on, int *num_on)
{
    (void)caps_on; (void)num_on;
    return 0; /* piattaforma non riconosciuta: mostrato come N/D */
}
#endif

static void *gf_clock_thread_func(void *arg)
{
    GF_TEXTEDITOR *self = (GF_TEXTEDITOR *)arg;
    while (!self->thread_should_exit) {
        time_t t = time(NULL);
        struct tm tm_info;
        gf_localtime_safe(&t, &tm_info);

        pthread_mutex_lock(&self->buffer_mutex);
        strftime(self->clock_string, sizeof(self->clock_string),
                 "%d/%m/%Y %H:%M:%S", &tm_info);
        pthread_mutex_unlock(&self->buffer_mutex);

        /* Autosalvataggio: se attivo (autosave_seconds > 0), ogni N secondi
         * salva su disco tutte le aree modificate che hanno gia' un
         * percorso. I file "senza nome" (mai salvati) non vengono toccati:
         * salvarli richiederebbe interrompere l'utente con un dialog per
         * chiedere il percorso, il che snaturerebbe un autosalvataggio
         * silenzioso in background. */
        int interval = self->options.autosave_seconds;
        if (interval > 0) {
            self->autosave_elapsed++;
            if (self->autosave_elapsed >= interval) {
                self->autosave_elapsed = 0;
                int ai;
                pthread_mutex_lock(&self->buffer_mutex);
                for (ai = 0; ai < GF_MAX_TABS; ai++) {
                    GF_FILEAREA *area = &self->tabs[ai];
                    if (area->in_use && area->modified && area->has_path)
                        gf_save_area_to_disk(area);
                }
                pthread_mutex_unlock(&self->buffer_mutex);
            }
        } else {
            self->autosave_elapsed = 0;
        }

        int i;
        for (i = 0; i < 10 && !self->thread_should_exit; i++)
            usleep(100000); /* 10 x 100ms = 1 secondo, ma interrompibile */
    }
    return NULL;
}

void gf_texteditor_run(GF_TEXTEDITOR *self)
{
    setlocale(LC_ALL, "");  /* necessario a ncursesw per l'UTF-8 */
    initscr();
    if (has_colors()) gf_init_colors();
    raw();     /* disabilita la generazione di segnali (Ctrl+C/Z/\) dal
                * terminale: servono come TASTI (es. Ctrl+Z per Undo),
                * non per sospendere/terminare il processo. Standard per
                * un editor a schermo intero (stesso approccio di vim/nano). */
    noecho();
    keypad(stdscr, TRUE);
    curs_set(1);
    wtimeout(stdscr, GF_MAIN_TIMEOUT_MS); /* risveglio periodico per l'orologio */
    self->options.text_color_pair = 15; /* bianco su nero di default */

    self->running = 1;
    gf_draw_all(self);
    while (self->running) {
        int ch = gf_getch_extended();

        if (ch == ERR) {
            gf_refresh_clock(self); /* nessun tasto: aggiorna solo l'orologio, niente erase() */
            continue;
        } else if (ch == KEY_F(10)) {
            gf_run_menu(self, 0);
        } else if (ch == GF_KEY_ALT_F) {
            gf_run_menu(self, 0);
        } else if (ch == GF_KEY_ALT_M) {
            gf_run_menu(self, 1);
        } else if (ch == GF_KEY_ALT_O) {
            gf_run_menu(self, 2);
        } else if (ch == GF_KEY_ALT_H) {
            gf_run_menu(self, 3);
        } else if (ch == KEY_F(9)) {   /* Ctrl+PgUp non standard -> F9/F8 cambiano scheda */
            gf_prev_tab(self);
        } else if (ch == KEY_F(8)) {
            gf_next_tab(self);
        } else {
            gf_handle_edit_key(self, ch);
        }
        gf_draw_all(self);
    }

    endwin();
}

int gf_texteditor_open_file(GF_TEXTEDITOR *self, const char *filepath)
{
    int idx = gf_find_free_area_index(self);
    if (idx < 0) return 0;
    pthread_mutex_lock(&self->buffer_mutex);
    gf_area_reset(&self->tabs[idx], GF_LANG_NONE);
    int ok = gf_load_file_into_area(&self->tabs[idx], filepath);
    if (ok) self->tabs[idx].language = gf_detect_language(filepath);
    pthread_mutex_unlock(&self->buffer_mutex);
    if (!ok) {
        gf_doc_free(&self->tabs[idx].doc);
        memset(&self->tabs[idx], 0, sizeof(self->tabs[idx]));
        return 0;
    }
    self->current_tab = idx;
    self->num_open++;
    gf_request_rehighlight(self);
    return 1;
}

void gf_texteditor_newopenfile(GF_TEXTEDITOR *self, const char *filepath)
{
    int idx = gf_find_free_area_index(self);
    if (idx < 0) return;

    FILE *probe = fopen(filepath, "r");
    int esiste = (probe != NULL);
    if (probe) fclose(probe);

    pthread_mutex_lock(&self->buffer_mutex);
    gf_area_reset(&self->tabs[idx], gf_detect_language(filepath));
    if (esiste) {
        gf_load_file_into_area(&self->tabs[idx], filepath);
        self->tabs[idx].language = gf_detect_language(filepath);
    } else {
        strncpy(self->tabs[idx].filepath, filepath, GF_MAX_FILEPATH - 1);
        self->tabs[idx].has_path = 1;
        const char *slash = strrchr(filepath, '/');
        strncpy(self->tabs[idx].filename, slash ? slash + 1 : filepath, GF_MAX_FILENAME - 1);
    }
    pthread_mutex_unlock(&self->buffer_mutex);

    self->current_tab = idx;
    self->num_open++;
    gf_request_rehighlight(self);
}

GF_TEXTEDITOR *gf_texteditor_constructor(void)
{
    GF_TEXTEDITOR *self = (GF_TEXTEDITOR *)calloc(1, sizeof(GF_TEXTEDITOR));
    if (!self) return NULL;

    self->current_tab = 0;
    self->num_open = 0;

    self->options.tab_width = GF_DEFAULT_TABWIDTH;
    self->options.text_color_pair = 15;
    self->options.developing_mode = 0;
    self->options.autosave_seconds = 0; /* disattivo di default */
    self->autosave_elapsed = 0;

    if (!getcwd(self->current_directory, sizeof(self->current_directory)))
        strncpy(self->current_directory, ".", sizeof(self->current_directory) - 1);

    self->clipboard.capacity = 256;
    self->clipboard.data = (char *)malloc((size_t)self->clipboard.capacity);
    self->clipboard.data[0] = '\0';
    self->clipboard.length = 0;

    memset(&self->find, 0, sizeof(self->find));

    self->running = 0;
    self->chars_typed = 0;

    pthread_mutex_init(&self->buffer_mutex, NULL);
    pthread_cond_init(&self->highlight_cond, NULL);
    self->highlight_dirty = 0;
    self->thread_should_exit = 0;

    {   /* valore iniziale sincrono, cosi' clock_string non e' mai vuota */
        time_t t0 = time(NULL);
        struct tm tm0;
        gf_localtime_safe(&t0, &tm0);
        strftime(self->clock_string, sizeof(self->clock_string), "%d/%m/%Y %H:%M:%S", &tm0);
    }

    pthread_create(&self->highlight_thread, NULL, gf_highlight_thread_func, self);
    pthread_create(&self->clock_thread, NULL, gf_clock_thread_func, self);

    self->run = gf_texteditor_run;
    self->newopenfile = gf_texteditor_newopenfile;
    return self;
}

void gf_texteditor_destroy(GF_TEXTEDITOR *self)
{
    if (!self) return;

    pthread_mutex_lock(&self->buffer_mutex);
    self->thread_should_exit = 1;
    pthread_cond_signal(&self->highlight_cond);
    pthread_mutex_unlock(&self->buffer_mutex);
    pthread_join(self->highlight_thread, NULL);
    pthread_join(self->clock_thread, NULL);

    int i;
    for (i = 0; i < GF_MAX_TABS; i++)
        if (self->tabs[i].in_use) {
            gf_doc_free(&self->tabs[i].doc);
            gf_area_free_undo_stack(&self->tabs[i]);
        }

    free(self->clipboard.data);
    pthread_mutex_destroy(&self->buffer_mutex);
    pthread_cond_destroy(&self->highlight_cond);
    free(self);
}


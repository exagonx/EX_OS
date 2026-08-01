/* ============================================================================
 * gf_fileio.c
 *
 * Parte dell'implementazione di GF_TEXTEDITOR - vedi gf_texteditor.h per
 * la descrizione dell'oggetto e gf_internal.h per le strutture e i
 * prototipi condivisi tra i moduli.
 *
 * Caricamento/salvataggio dei file su disco, rilevamento del linguaggio
 * dall'estensione del file, elenco del contenuto di una directory (usato
 * dallo sfoglia-directory di Open e Save As in gf_ui.c).
 *
 * Autore : Graziano Falcone  <exagonx@hotmail.com>
 * Licenza: GNU GPL v2 (vedi Help->License nel programma, e il file
 *          COPYING distribuito insieme al codice sorgente)
 * ==========================================================================*/

#define _GNU_SOURCE
#include "gf_internal.h"

/* ---------------------------------------------------------------------- *
 * Voci restituite dallo sfoglia-directory: la struct GF_DIRENTRY e' definita
 * in gf_internal.h (condivisa con gf_ui.c, che la usa nel dialog Sfoglia).
 * ---------------------------------------------------------------------- */

GF_LANGUAGE gf_detect_language(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return GF_LANG_NONE;
    if (!strcmp(dot, ".c") || !strcmp(dot, ".h")) return GF_LANG_C;
    if (!strcmp(dot, ".cpp") || !strcmp(dot, ".cc") ||
        !strcmp(dot, ".cxx") || !strcmp(dot, ".hpp")) return GF_LANG_CPP;
    if (!strcasecmp(dot, ".bas")) return GF_LANG_BASIC;
    return GF_LANG_NONE;
}

static int gf_direntry_cmp(const void *a, const void *b)
{
    const GF_DIRENTRY *ea = (const GF_DIRENTRY *)a;
    const GF_DIRENTRY *eb = (const GF_DIRENTRY *)b;
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;
    return strcasecmp(ea->name, eb->name);
}

int gf_list_directory(const char *path, GF_DIRENTRY **out_entries)
{
    DIR *d = opendir(path);
    if (!d) return -1;

    int capacity = 64, count = 0;
    GF_DIRENTRY *entries = (GF_DIRENTRY *)malloc(sizeof(GF_DIRENTRY) * (size_t)capacity);
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (count >= capacity) {
            capacity *= 2;
            entries = (GF_DIRENTRY *)realloc(entries, sizeof(GF_DIRENTRY) * (size_t)capacity);
        }
        char fullpath[GF_MAX_FILEPATH];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, de->d_name);
        struct stat st;
        int isdir = 0;
        if (stat(fullpath, &st) == 0) isdir = S_ISDIR(st.st_mode);
        strncpy(entries[count].name, de->d_name, sizeof(entries[count].name) - 1);
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';
        entries[count].is_dir = isdir;
        count++;
    }
    closedir(d);
    qsort(entries, (size_t)count, sizeof(GF_DIRENTRY), gf_direntry_cmp);
    *out_entries = entries;
    return count;
}

void gf_path_parent(char *path)
{
    char *slash = strrchr(path, '/');
    if (!slash) { strcpy(path, "."); return; }
    if (slash == path) { path[1] = '\0'; return; } /* radice "/" */
    *slash = '\0';
}

int gf_load_file_into_area(GF_FILEAREA *area, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    while (area->doc.num_lines > 0) gf_doc_delete_line(&area->doc, area->doc.num_lines - 1);

    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
        gf_doc_insert_line(&area->doc, area->doc.num_lines, buf);
    }
    if (area->doc.num_lines == 0) gf_doc_insert_line(&area->doc, 0, "");
    fclose(f);

    strncpy(area->filepath, path, GF_MAX_FILEPATH - 1);
    const char *slash = strrchr(path, '/');
    strncpy(area->filename, slash ? slash + 1 : path, GF_MAX_FILENAME - 1);
    area->has_path = 1;
    area->modified = 0;
    area->cursor_row = area->cursor_col = 0;
    return 1;
}

int gf_save_area_to_disk(GF_FILEAREA *area)
{
    if (!area->has_path) return 0;
    FILE *f = fopen(area->filepath, "w");
    if (!f) return 0;
    int i;
    for (i = 0; i < area->doc.num_lines; i++)
        fprintf(f, "%s\n", area->doc.lines[i].text);
    fclose(f);
    area->modified = 0;
    return 1;
}


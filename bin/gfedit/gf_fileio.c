/* =============================================================================
 * bin/gfedit/gf_fileio.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * GF Edit — disco: caricamento, salvataggio, percorsi.
 *
 * L'originale usava FILE*, fopen/fgets, stat e dirent. Qui ci sono
 * open/read/write e listdir, che è tutto ciò che la libc di EX-OS
 * espone; lo sfoglia-directory sta in gf_ui.c perché è un dialogo, non
 * un'operazione di file.
 * ============================================================================= */

#include "gfedit.h"

/* =============================================================================
 * Percorsi
 * ============================================================================= */
const char *gf_basename(const char *path)
{
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

/* Toglie l'ultimo componente. "/a/b" -> "/a", "/a" -> "/", "/" -> "/" */
void gf_path_padre(char *path)
{
    char *p;

    if (!path[0]) { gf_strlcpy(path, "/", 2); return; }

    p = strrchr(path, '/');
    if (!p) { gf_strlcpy(path, "/", 2); return; }
    if (p == path) { path[1] = '\0'; return; }   /* resta la radice */
    *p = '\0';
}

void gf_path_unisci(char *dst, int size, const char *dir, const char *nome)
{
    int n = 0;
    int i;

    for (i = 0; dir[i] && n < size - 1; i++) dst[n++] = dir[i];

    /* Una sola barra fra i due pezzi: "/" + "bin" non deve dare "//bin",
     * che il VFS tratterebbe come un componente vuoto. */
    if (n > 0 && dst[n - 1] != '/' && n < size - 1) dst[n++] = '/';

    for (i = 0; nome[i] && n < size - 1; i++) dst[n++] = nome[i];
    dst[n] = '\0';
}

/* =============================================================================
 * Riconoscimento del linguaggio dall'estensione
 * ============================================================================= */
GfLingua gf_rileva_lingua(const char *path)
{
    const char *nome = gf_basename(path);
    const char *ext  = strrchr(nome, '.');

    if (!ext) return GF_LANG_NONE;
    ext++;

    if (gf_str_uguale_ci(ext, "c")   || gf_str_uguale_ci(ext, "h"))   return GF_LANG_C;
    if (gf_str_uguale_ci(ext, "cpp") || gf_str_uguale_ci(ext, "cc")  ||
        gf_str_uguale_ci(ext, "hpp") || gf_str_uguale_ci(ext, "cxx")) return GF_LANG_CPP;
    if (gf_str_uguale_ci(ext, "bas") || gf_str_uguale_ci(ext, "bi"))  return GF_LANG_BASIC;
    if (gf_str_uguale_ci(ext, "asm") || gf_str_uguale_ci(ext, "s")   ||
        gf_str_uguale_ci(ext, "inc"))                                 return GF_LANG_ASM;

    return GF_LANG_NONE;
}

/* =============================================================================
 * gf_carica — legge un file nell'area
 *
 * Ritorna 0 se il file è stato letto, -1 se non esiste (che NON è un
 * errore: aprire un nome inesistente significa cominciare un file
 * nuovo, come fa textline e come faceva l'originale), -2 se l'area non
 * ha memoria.
 *
 * Il troncamento — riga troppo lunga, o file con più righe di quante
 * l'area ne tenga — non fa fallire il caricamento ma alza t->troncato,
 * e da lì la barra di stato lo dice a chiare lettere. Un editor che
 * tronca in silenzio è un editor che al primo salvataggio distrugge il
 * file: l'utente deve saperlo PRIMA di premere F2, non dopo.
 * ============================================================================= */
int gf_carica(GfTab *t, const char *path)
{
    char rd[1024];
    char cur[GF_LINE_STRIDE];
    int  fd, n, cl = 0;
    int  pieno = 0;

    if (gf_tab_prepara(t) != 0) return -2;

    gf_tab_azzera(t, gf_rileva_lingua(path));
    t->num_lines   = 0;
    t->undo_attivo = 0;      /* il caricamento non è una modifica da disfare */

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        t->num_lines   = 1;
        t->undo_attivo = 1;
        t->modified    = 0;
        gf_ricalcola_commenti(t);
        return -1;
    }

    while ((n = (int)read(fd, rd, sizeof(rd))) > 0) {
        int i;

        for (i = 0; i < n; i++) {
            char c = rd[i];

            if (c == '\r') continue;            /* tollera i fine riga CRLF */

            if (c == '\n') {
                cur[cl] = '\0';
                if (t->num_lines >= GF_MAX_LINES) { pieno = 1; break; }
                gf_strlcpy(t->d->text[t->num_lines], cur, GF_MAX_COL + 1);
                t->num_lines++;
                cl = 0;
                continue;
            }

            if (cl < GF_MAX_COL) cur[cl++] = c;
            else                 t->troncato = 1;
        }
        if (pieno) break;
    }

    /* Ultima riga senza '\n' finale */
    if (cl > 0 && t->num_lines < GF_MAX_LINES) {
        cur[cl] = '\0';
        gf_strlcpy(t->d->text[t->num_lines], cur, GF_MAX_COL + 1);
        t->num_lines++;
    }

    close(fd);

    if (pieno) t->troncato = 1;
    if (t->num_lines == 0) t->num_lines = 1;

    t->undo_attivo = 1;
    t->modified    = 0;
    gf_ricalcola_commenti(t);
    return 0;
}

/* =============================================================================
 * gf_salva — riscrive il file da capo
 *
 * Le righe vengono accumulate in un buffer e scritte a blocchi: una
 * write() per riga significherebbe una syscall e un accesso al floppy
 * ogni poche decine di byte.
 *
 * Ritorna 0, oppure il valore negativo restituito dalla open/write —
 * che è un errno del kernel e viene mostrato all'utente così com'è.
 *
 * RIFIUTA DI SALVARE un'area troncata al caricamento: il file su disco
 * contiene più di quanto l'editor abbia in memoria, e riscriverlo
 * significherebbe cancellare la parte mai vista. Chi vuole davvero
 * quel taglio usa Salva con nome su un altro file.
 * ============================================================================= */
#define GF_WBUF 1024

int gf_salva(GfTab *t)
{
    char buf[GF_WBUF];
    int  fd, i, n = 0;

    if (!t->has_path) return -22;    /* EINVAL: nessun percorso a cui salvare */
    if (t->troncato)  return -27;    /* EFBIG: vedi il commento qui sopra */

    fd = open(t->filepath, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return fd;

    for (i = 0; i < t->num_lines; i++) {
        const char *riga = gf_riga(t, i);
        int len = (int)strlen(riga);
        int j;

        for (j = 0; j < len; j++) {
            if (n == GF_WBUF) {
                if (write(fd, buf, GF_WBUF) < 0) { close(fd); return -5; }
                n = 0;
            }
            buf[n++] = riga[j];
        }

        if (n == GF_WBUF) {
            if (write(fd, buf, GF_WBUF) < 0) { close(fd); return -5; }
            n = 0;
        }
        buf[n++] = '\n';
    }

    if (n > 0 && write(fd, buf, (size_t)n) < 0) { close(fd); return -5; }

    close(fd);
    t->modified = 0;
    return 0;
}

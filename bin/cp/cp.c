/* =============================================================================
 * bin/cp/cp.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Copia file, directory e pattern con caratteri jolly.
 *
 *   cp [-r] [-y] <sorgente>    <destinazione>
 *   cp [-r] [-y] <sorgente…>   <directory>
 *   cp [-r] [-y] <jolly>       <directory>
 *   cp -h
 *
 * Opzioni
 *   -r   copia le directory in modo ricorsivo
 *   -y   sovrascrive i file esistenti senza chiedere
 *   -h   aiuto ed esempi
 *
 * =============================================================================
 *
 * File esistenti: si CHIEDE, non si rifiuta
 * ─────────────────────────────────────────
 * Fino ad agosto 2026 un file gia' presente faceva fallire la copia e
 * basta: «esiste gia'», e chi voleva sostituirlo doveva cancellarlo prima.
 * Su una copia ricorsiva di un albero era la risposta sbagliata — bastava
 * un file in comune per costringere a rifare tutto a mano.
 *
 * Ora la destinazione esistente apre una domanda, e -y risponde «si'» a
 * tutte in anticipo. Rispondere `t` a una qualunque vale come -y da li' in
 * poi: e' la stessa scorciatoia, decisa a meta' strada quando ci si accorge
 * che i file in comune sono cento.
 *
 * ! SALTARE NON E' FALLIRE. Un file che l'utente ha deciso di non
 * sovrascrivere non conta fra gli errori e non cambia il codice d'uscita:
 * la copia ha fatto esattamente quello che le e' stato detto di fare.
 *
 * Copia ricorsiva: si vede cosa sta succedendo
 * ────────────────────────────────────────────
 * `cp -r` stampa ogni file mentre lo copia. Su un floppy un albero di
 * qualche decina di file sono minuti in cui prima non compariva niente, e
 * un programma muto e un programma bloccato si somigliano troppo.
 * =============================================================================
 *
 * Logica jolly
 * ─────────────
 * L'espansione la fa cp stesso: la shell di EX-OS non fa globbing, quindi
 * *.txt arriva qui letteralmente. cp separa la parte di directory dal
 * pattern di nome (tutto dopo l'ultimo '/'), apre la directory con opendir
 * e fa corrispondere ogni entry al pattern.
 *
 * ! '.' e '..' vengono SEMPRE SALTATI — sia in readdir per la copia
 * ricorsiva, sia nell'espansione dei jolly — altrimenti un `cp -r` su
 * se stesso entrerebbe in un ciclo.
 * ============================================================================= */
#include "libc.h"

#define BLOCCO       4096
#define PERCORSO_MAX 320
#define FONTI_MAX    64

static char buf_copia[BLOCCO];
static int  opt_r = 0;
static int  opt_y = 0;   /* sovrascrivi tutto senza chiedere */

/* Esiti di copia_file(): «saltato» e' distinto da «fallito» perche' i
 * chiamanti contano solo i secondi. Vedi il commento in testa al file. */
#define COPIA_OK       0
#define COPIA_ERRORE  (-1)
#define COPIA_SALTATO  1


/* ─────────────────────────────────────────────────────────────────────────────
 * Utilità generiche
 * ───────────────────────────────────────────────────────────────────────────── */

/* Corrispondenza jolly: * = qualunque sequenza (anche vuota),
 *                        ? = un carattere qualunque.
 * Ricorsione sul '*': profondità massima = numero di '*' nel pattern,
 * trascurabile per nomi di file. */
static int jolly_match(const char *pat, const char *nome)
{
    for (;;) {
        if (*pat == '*') {
            while (*pat == '*') pat++;      /* asterischi consecutivi */
            if (!*pat) return 1;            /* pattern finisce con * → match */
            while (*nome) {
                if (jolly_match(pat, nome)) return 1;
                nome++;
            }
            return 0;
        }
        if (!*nome) return (*pat == '\0');
        if (*pat != '?' && *pat != *nome) return 0;
        pat++; nome++;
    }
}

static int ha_jolly(const char *s)
{
    while (*s) { if (*s == '*' || *s == '?') return 1; s++; }
    return 0;
}

/* Puntatore al nome base di un percorso (dopo l'ultimo '/'). */
static const char *nome_base(const char *p)
{
    const char *u = p;
    while (*p) { if (*p++ == '/') u = p; }
    return u;
}

/* Controlla se il nome è '.' o '..'. */
static int punto_punto(const char *n)
{
    return (n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0')));
}

/* Costruisce dst = dir "/" nome in buf[size].
 * Ritorna 0 ok, -1 se non ci sta. */
static int path_join(char *dst, int size, const char *dir, const char *nome)
{
    int ld = 0, ln = 0;
    const char *p;

    for (p = dir;  *p; p++) ld++;
    for (p = nome; *p; p++) ln++;

    if (ld + 1 + ln + 1 > size) return -1;

    for (p = dir; *p; p++) *dst++ = *p;
    if (ld > 0 && dst[-1] != '/') *dst++ = '/';
    for (p = nome; *p; p++) *dst++ = *p;
    *dst = '\0';
    return 0;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Copia di un singolo file
 * ───────────────────────────────────────────────────────────────────────────── */

/* Chiede se sovrascrivere `dst`. Ritorna 1 per sì, 0 per no.
 *
 * ! UNA RISPOSTA ILLEGGIBILE VALE «NO». Con lo standard input chiuso —
 * dentro uno script, o con l'input rediretto — read() rende 0 subito, e
 * prendere quel silenzio per un sì farebbe sovrascrivere un albero intero
 * senza che nessuno l'abbia chiesto. Chi vuole il sì automatico ha -y, che
 * è esplicito e si legge nella riga di comando. */
static int chiedi_sovrascrittura(const char *dst)
{
    char risposta[16];
    int  r;

    printf("cp: %s esiste gia'. Sovrascrivere? (s/n, `t` = tutti): ", dst);

    r = (int)read(0, risposta, sizeof(risposta) - 1);
    if (r <= 0) { printf("\n"); return 0; }
    risposta[r] = '\0';
    while (r > 0 && (risposta[r - 1] == '\n' || risposta[r - 1] == '\r'))
        risposta[--r] = '\0';

    /* `t` decide per tutti i file che restano: è -y scelto a metà strada. */
    if (risposta[0] == 't' || risposta[0] == 'T') { opt_y = 1; return 1; }
    return (risposta[0] == 's' || risposta[0] == 'S');
}

/* Copia src → dst (percorsi completi).
 * Se dst esiste chiede conferma, salvo -y. Gestisce la write parziale:
 * torna finché tutti i byte non sono scritti.
 * Ritorna COPIA_OK, COPIA_ERRORE o COPIA_SALTATO. */
static int copia_file(const char *src, const char *dst)
{
    int fs, fd, n, tot = 0;

    /* La destinazione si controlla PRIMA di aprire la sorgente: aprirla e
     * poi scoprire che non si può scrivere sarebbe lavoro buttato, e su un
     * floppy anche qualche secondo di motore. */
    fd = open(dst, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        if (!opt_y && !chiedi_sovrascrittura(dst)) {
            printf("cp: %s: saltato\n", dst);
            return COPIA_SALTATO;
        }
    }

    fs = open(src, O_RDONLY);
    if (fs < 0) {
        printf("cp: %s: %s\n", src, strerror(errno));
        return COPIA_ERRORE;
    }

    /* O_TRUNC è ciò che rende la sovrascrittura una sostituzione: senza,
     * un file nuovo più corto del vecchio ne lascerebbe in coda la parte
     * che avanza, e il risultato non sarebbe una copia di niente. */
    fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf("cp: %s: %s\n", dst, strerror(errno));
        close(fs);
        return COPIA_ERRORE;
    }

    while ((n = (int)read(fs, buf_copia, BLOCCO)) > 0) {
        int scritti = 0;

        /* write() può scrivere meno di quanto chiesto — per esempio se il
         * volume si riempie a metà blocco. */
        while (scritti < n) {
            int w = (int)write(fd, buf_copia + scritti,
                               (unsigned int)(n - scritti));
            if (w <= 0) {
                printf("cp: scrittura fallita su %s dopo %d byte", dst,
                       tot + scritti);
                if (w < 0) printf(": %s", strerror(errno));
                printf("\n");
                close(fs); close(fd);
                return COPIA_ERRORE;
            }
            scritti += w;
        }
        tot += scritti;
    }

    close(fs); close(fd);

    if (n < 0) {
        printf("cp: lettura fallita su %s dopo %d byte: %s\n",
               src, tot, strerror(errno));
        return COPIA_ERRORE;
    }
    return COPIA_OK;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Copia ricorsiva di una directory
 * ───────────────────────────────────────────────────────────────────────────── */

/* Copia ricorsiva dell'albero src → dst (dst deve già esistere).
 * Ritorna il numero di errori incontrati. */
static int copia_dir(const char *src, const char *dst)
{
    DIR           *d;
    struct dirent *e;
    char           p_src[PERCORSO_MAX];
    char           p_dst[PERCORSO_MAX];
    int            errori = 0;

    d = opendir(src);
    if (!d) { printf("cp: %s: %s\n", src, strerror(errno)); return 1; }

    while ((e = readdir(d)) != NULL) {
        if (punto_punto(e->d_name)) continue;

        if (path_join(p_src, PERCORSO_MAX, src, e->d_name) < 0 ||
            path_join(p_dst, PERCORSO_MAX, dst, e->d_name) < 0) {
            printf("cp: percorso troppo lungo, saltato: %s/%s\n",
                   src, e->d_name);
            errori++;
            continue;
        }

        if (e->d_type == DT_DIR) {
            struct stat sd;
            if (stat(p_dst, &sd) != 0) {
                if (mkdir(p_dst, 0755) != 0) {
                    printf("cp: mkdir %s: %s\n", p_dst, strerror(errno));
                    errori++;
                    continue;
                }
                printf("  %s/\n", p_dst);
            } else if (!S_ISDIR(sd.st_mode)) {
                printf("cp: %s: esiste gia' come file\n", p_dst);
                errori++;
                continue;
            }
            errori += copia_dir(p_src, p_dst);
        } else {
            /* ! SI STAMPA PRIMA DI COPIARE, non dopo. Il nome che si legge
             * è quello su cui cp sta lavorando in questo momento: su un
             * floppy un file può prendersi secondi, e a stampare dopo si
             * vedrebbe l'elenco di ciò che è già finito mentre la riga in
             * corso — l'unica che serve quando qualcosa si pianta — non
             * compare mai. È anche l'ordine giusto per la domanda di
             * sovrascrittura, che arriva subito sotto il proprio file. */
            printf("  %s -> %s\n", p_src, p_dst);
            if (copia_file(p_src, p_dst) < 0) errori++;
        }
    }

    closedir(d);
    return errori;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Dispatcher: copia src con il nome esatto dst
 * ───────────────────────────────────────────────────────────────────────────── */

/* copia_come: copia src esattamente come dst (dst è il nome finale, non
 * la directory padre). Distingue file da directory, controlla -r.
 * Rende gli stessi tre esiti di copia_file(). */
static int copia_come(const char *src, const char *dst)
{
    struct stat st;

    if (stat(src, &st) != 0) {
        printf("cp: %s: %s\n", src, strerror(errno));
        return COPIA_ERRORE;
    }

    if (S_ISDIR(st.st_mode)) {
        struct stat sd;
        if (!opt_r) {
            printf("cp: %s: e' una directory (usa -r per le directory)\n", src);
            return COPIA_ERRORE;
        }
        if (stat(dst, &sd) != 0) {
            if (mkdir(dst, 0755) != 0) {
                printf("cp: mkdir %s: %s\n", dst, strerror(errno));
                return COPIA_ERRORE;
            }
            printf("  %s/\n", dst);
        } else if (!S_ISDIR(sd.st_mode)) {
            printf("cp: %s: esiste gia' come file\n", dst);
            return COPIA_ERRORE;
        }
        return copia_dir(src, dst) > 0 ? COPIA_ERRORE : COPIA_OK;
    }

    return copia_file(src, dst);
}

/* copia_in_dir: copia src nella directory dst_dir mantenendo il nome base. */
static int copia_in_dir(const char *src, const char *dst_dir)
{
    char dst[PERCORSO_MAX];
    const char *base = nome_base(src);

    if (path_join(dst, PERCORSO_MAX, dst_dir, base) < 0) {
        printf("cp: percorso troppo lungo: %s/%s\n", dst_dir, base);
        return -1;
    }
    return copia_come(src, dst);
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Espansione dei caratteri jolly
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * pat può contenere * e ?. Si separa la parte di directory dal pattern di
 * nome (tutto dopo l'ultimo '/'), si apre la directory con opendir e si
 * copia ogni entry che corrisponde al pattern.
 *
 * Esempi di separazione:
 *   "/bin/x.c"   -> dir="/bin"  pattern="x.c"
 *   "docs/file?" -> dir="docs"  pattern="file?"
 *   "*.txt"      -> dir="."     pattern="*.txt"   (dir corrente)
 * ───────────────────────────────────────────────────────────────────────────── */
static int espandi_e_copia(const char *pat, const char *dst_dir)
{
    char           dir_apri[PERCORSO_MAX];  /* per opendir */
    char           dir_src[PERCORSO_MAX];   /* prefisso per i percorsi sorgente */
    const char    *nome_pat;
    const char    *ultima_slash;
    const char    *p;
    DIR           *d;
    struct dirent *e;
    int            trovati = 0, errori = 0;

    /* Trova l'ultimo '/' nel pattern (strrchr inline per evitare il cast
     * e per non dipendere dall'header dello stub di test). */
    ultima_slash = NULL;
    for (p = pat; *p; p++) if (*p == '/') ultima_slash = p;

    if (ultima_slash) {
        int ld = (int)(ultima_slash - pat); /* lunghezza prima del '/' */

        if (ld == 0) {
            /* pattern del tipo "/pat" con slash iniziale */
            dir_apri[0] = '/'; dir_apri[1] = '\0';
            dir_src[0]  = '/'; dir_src[1]  = '\0';
        } else {
            int i;
            if (ld >= PERCORSO_MAX) {
                printf("cp: percorso troppo lungo\n");
                return 1;
            }
            for (i = 0; i < ld; i++) dir_apri[i] = pat[i];
            dir_apri[ld] = '\0';
            for (i = 0; i < ld; i++) dir_src[i]  = pat[i];
            dir_src[ld]  = '\0';
        }
        nome_pat = ultima_slash + 1;
    } else {
        /* Pattern relativo senza directory: apri la dir corrente. */
        dir_apri[0] = '.'; dir_apri[1] = '\0';
        dir_src[0]  = '\0';                    /* path_join con "" = solo nome */
        nome_pat = pat;
    }

    d = opendir(dir_apri);
    if (!d) {
        printf("cp: %s: %s\n", dir_apri, strerror(errno));
        return 1;
    }

    while ((e = readdir(d)) != NULL) {
        char p_src[PERCORSO_MAX];

        if (punto_punto(e->d_name))              continue;
        if (!jolly_match(nome_pat, e->d_name))   continue;
        trovati++;

        if (path_join(p_src, PERCORSO_MAX, dir_src, e->d_name) < 0) {
            printf("cp: percorso troppo lungo: %s/%s\n", dir_src, e->d_name);
            errori++;
            continue;
        }
        if (copia_in_dir(p_src, dst_dir) < 0) errori++;
    }
    closedir(d);

    if (!trovati) {
        printf("cp: %s: nessun file corrisponde\n", pat);
        errori++;
    }
    return errori;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Aiuto
 * ───────────────────────────────────────────────────────────────────────────── */

static void mostra_aiuto(void)
{
    printf("uso:\n");
    printf("  cp [-r] [-y] <sorgente>  <destinazione>\n");
    printf("  cp [-r] [-y] <sorgente>  <directory>\n");
    printf("  cp [-r] [-y] <sorgente1> <sorgente2> ... <directory>\n");
    printf("  cp [-r] [-y] <jolly>     <directory>\n");
    printf("  cp -h\n");
    printf("\n");
    printf("opzioni:\n");
    printf("  -r   copia le directory in modo ricorsivo\n");
    printf("  -y   sovrascrive i file esistenti senza chiedere\n");
    printf("  -h   questo aiuto\n");
    printf("\n");
    printf("note:\n");
    printf("  Se il file di destinazione esiste, cp chiede se sovrascriverlo:\n");
    printf("    s = questo si'   n = questo no   t = tutti i prossimi si'\n");
    printf("  Con -y non chiede niente e sovrascrive tutto.\n");
    printf("  Un file saltato non e' un errore: il codice d'uscita resta 0.\n");
    printf("  Con -r ogni file copiato viene stampato mentre si copia.\n");
    printf("  I jolly * e ? vengono espansi da cp (non dalla shell).\n");
    printf("  Con piu' sorgenti o con jolly la destinazione deve essere\n");
    printf("  una directory esistente.\n");
    printf("\n");
    printf("esempi:\n");
    printf("  cp nota.txt /disk/nota.txt       copia un file con nuovo nome\n");
    printf("  cp nota.txt /disk/               copia in una directory\n");
    printf("  cp -r /docs /disk/docs           copia un'intera directory\n");
    printf("  cp -r -y /docs /disk/docs        ... sovrascrivendo senza chiedere\n");
    printf("  cp *.txt /disk/                  copia tutti i .txt\n");
    printf("  cp /bin/sh /bin/ls /backup/      copia piu' file in una dir\n");
    printf("  cp /src/*.c /src/*.h /build/     jolly multipli\n");
    printf("  cp -r /bin/*.drv /backup/        driver ricorsivi con jolly\n");
}


/* ─────────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    char       *fonti[FONTI_MAX];
    int         n_fonti = 0;
    const char *dst;
    int         i, errori = 0, n_jolly = 0;
    struct stat st_dst;
    int         dst_e_dir;

    /* ── 1. Parsing dei flag ─────────────────────────────────────────────── */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            const char *f = argv[i] + 1;
            while (*f) {
                switch (*f) {
                    case 'r': opt_r = 1;          break;
                    case 'y': opt_y = 1;          break;
                    case 'h': mostra_aiuto();     return 0;
                    default:
                        printf("cp: -%c: opzione sconosciuta\n", *f);
                        printf("    cp -h per l'aiuto\n");
                        return 1;
                }
                f++;
            }
        } else {
            if (n_fonti >= FONTI_MAX) {
                printf("cp: troppi argomenti (max %d sorgenti)\n", FONTI_MAX - 1);
                return 1;
            }
            fonti[n_fonti++] = argv[i];
        }
    }

    /* ── 2. Validazione argomenti ────────────────────────────────────────── */
    if (n_fonti < 2) {
        printf("cp: uso: cp [-r] <sorgente> <destinazione>\n");
        printf("         cp -h  per esempi\n");
        return 1;
    }

    /* L'ultimo argomento non-flag è la destinazione. */
    dst = fonti[--n_fonti];

    /* ── 3. Conta i pattern con jolly ────────────────────────────────────── */
    for (i = 0; i < n_fonti; i++)
        if (ha_jolly(fonti[i])) n_jolly++;

    /* ── 4. Stato della destinazione ─────────────────────────────────────── */
    dst_e_dir = (stat(dst, &st_dst) == 0 && S_ISDIR(st_dst.st_mode));

    /* Con più sorgenti o con jolly la destinazione DEVE essere una directory:
     * non c'è un modo ragionevole di rinominare molti file in uno solo. */
    if (n_fonti > 1 || n_jolly > 0) {
        if (!dst_e_dir) {
            printf("cp: %s: deve essere una directory esistente\n", dst);
            printf("    (con piu' sorgenti o con jolly la destinazione\n");
            printf("     deve gia' esistere come directory)\n");
            return 1;
        }
    }

    /* ── 5. Copia ─────────────────────────────────────────────────────────── */

    if (n_fonti == 1 && !n_jolly && !dst_e_dir) {
        /* Caso semplice: una sola fonte, destinazione è un nome nuovo
         * (o un file esistente — copia_file se ne occuperà). */
        errori = (copia_come(fonti[0], dst) < 0) ? 1 : 0;
    } else {
        /* Fonti multiple e/o jolly → tutto finisce nella directory dst. */
        for (i = 0; i < n_fonti; i++) {
            if (ha_jolly(fonti[i])) {
                errori += espandi_e_copia(fonti[i], dst);
            } else {
                if (copia_in_dir(fonti[i], dst) < 0) errori++;
            }
        }
    }

    if (errori > 0 && (n_fonti > 1 || n_jolly > 0)) {
        printf("cp: completato con %d error%s\n",
               errori, errori == 1 ? "e" : "i");
    }

    return errori > 0 ? 1 : 0;
}

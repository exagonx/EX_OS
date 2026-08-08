/* =============================================================================
 * bin/delete/delete.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * delete — cancella uno o più file, con caratteri jolly.
 *
 *   delete <modello> [modello2 ...]
 *
 *   ?   un carattere qualsiasi, esattamente uno
 *   *   una sequenza qualsiasi di caratteri, anche vuota
 *
 * Esempi:
 *   delete nota.txt        un file preciso
 *   delete *               tutto il contenuto della directory corrente
 *   delete /temp/tmp*      tutti i file che iniziano per "tmp" dentro /temp
 *   delete dati?.log       DATI1.LOG, DATI2.LOG, ...
 *
 * ---------------------------------------------------------------------------
 * DOVE AVVIENE L'ESPANSIONE, e perché qui
 *
 * I caratteri jolly sono espansi da QUESTO programma, non dal kernel e non
 * dalla shell:
 *
 *   - non dal kernel, perché una syscall che cancella "tutto ciò che
 *     assomiglia a X" è molto più difficile da rendere sicura di una che
 *     cancella un nome preciso. sys_unlink prende un nome e basta;
 *   - non dalla shell, perché la shell di EX-OS non fa espansione e
 *     aggiungerla cambierebbe il comportamento di TUTTI i comandi.
 *
 * È lo stesso modello di MS-DOS, e ha un vantaggio pratico: il programma
 * sa quali file ha selezionato e può dirlo prima di cancellarli.
 * ---------------------------------------------------------------------------
 * ============================================================================= */

#include "libc.h"

/* Voci lette per ogni chiamata a listdir_from. Il kernel ha comunque un
 * tetto proprio (READDIR_MAX_BATCH), quindi questo numero è solo la
 * dimensione del blocco: la directory viene percorsa a blocchi finché
 * non finisce. */
#define BLOCCO       LISTDIR_MAX_BATCH

/* Quanti file al massimo si possono cancellare in un colpo solo.
 * La root FAT12 ne contiene al più 224; una sottodirectory può crescere,
 * ma oltre questo limite il programma lo DICE invece di fermarsi in
 * silenzio. 256 nomi x 13 byte = 3.3KB, in BSS. */
#define MAX_SELEZIONE 256

#define MAX_PATH     160

/* Codici errno restituiti dal kernel, in negativo. */
#define E_NOENT      2
#define E_ISDIR     21
#define E_INVAL     22

/* =============================================================================
 * minuscolo/maiuscolo — il confronto è insensibile al caso perché FAT12
 * conserva i nomi sempre in maiuscolo: chi digita "tmp*" si aspetta che
 * TMP1.TXT venga trovato.
 * ============================================================================= */
static char up(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

/* =============================================================================
 * corrisponde — confronto di 'nome' con un modello contenente ? e *
 *
 * '*' può assorbire una sequenza di lunghezza qualsiasi, quindi serve
 * poter TORNARE INDIETRO: se il resto del modello non si aggancia, si
 * riprova facendo assorbire al '*' un carattere in più. Senza questo,
 * "*.txt" fallirebbe su "NOTA.TXT" — il '*' si mangerebbe tutto e non
 * resterebbe nulla per ".txt".
 *
 * L'algoritmo tiene memoria dell'ultimo '*' incontrato (stella) e della
 * posizione nel nome in quel momento (ripresa): è la versione iterativa
 * del backtracking, senza ricorsione e senza allocazioni.
 * ============================================================================= */
static int corrisponde(const char *modello, const char *nome)
{
    const char *stella  = NULL;   /* ultimo '*' visto nel modello */
    const char *ripresa = NULL;   /* dove riprendere nel nome */

    while (*nome) {
        if (*modello == '?' || up(*modello) == up(*nome)) {
            modello++;
            nome++;
        } else if (*modello == '*') {
            stella  = modello++;   /* ricorda dove tornare */
            ripresa = nome;        /* per ora il '*' assorbe zero caratteri */
        } else if (stella) {
            /* Non combacia: fai assorbire al '*' un carattere in più */
            modello = stella + 1;
            nome    = ++ripresa;
        } else {
            return 0;
        }
    }

    /* Nome finito: restano ammessi solo '*' nel modello */
    while (*modello == '*') modello++;
    return *modello == '\0';
}

/* =============================================================================
 * dividi — separa "/temp/tmp*" in directory ("/temp") e modello ("tmp*")
 *
 * Un modello senza '/' riguarda la directory corrente, che si ottiene con
 * getcwd(): serve perché listdir() vuole un percorso, non "qui".
 * ============================================================================= */
static void dividi(const char *arg, char *dir, int dirmax,
                    char *modello, int modmax)
{
    const char *ultimo = NULL;
    const char *p;
    int i;

    for (p = arg; *p; p++) {
        if (*p == '/') ultimo = p;
    }

    if (!ultimo) {
        /* Nessuna directory nel modello: quella corrente */
        if (!getcwd(dir, (size_t)dirmax)) strncpy(dir, "/", (size_t)dirmax);
        strncpy(modello, arg, (size_t)modmax);
        modello[modmax - 1] = '\0';
        return;
    }

    /* "/tmp*" → directory "/" ; "/temp/tmp*" → directory "/temp" */
    i = (int)(ultimo - arg);
    if (i == 0) {
        strncpy(dir, "/", (size_t)dirmax);
    } else {
        if (i >= dirmax) i = dirmax - 1;
        memcpy(dir, arg, (size_t)i);
        dir[i] = '\0';
    }

    strncpy(modello, ultimo + 1, (size_t)modmax);
    modello[modmax - 1] = '\0';
}

/* Costruisce "dir/nome" evitando la doppia barra quando dir è "/" */
static void unisci(char *out, int max, const char *dir, const char *nome)
{
    int i = 0;

    while (dir[i] && i < max - 1) { out[i] = dir[i]; i++; }
    if (i > 0 && out[i - 1] != '/' && i < max - 1) out[i++] = '/';
    while (*nome && i < max - 1) out[i++] = *nome++;
    out[i] = '\0';
}

/* ⚠️ PRENDE errno, NON IL VALORE DI RITORNO (agosto 2026). unlink(),
 * mkdir() e rmdir() rendono -1 come vuole POSIX, e `switch (-err)` su
 * quel -1 finiva sempre nel ramo predefinito stampando «errore -1»: un
 * messaggio che non dice niente proprio quando serve. Il codice vero sta
 * in errno. Vedi il commento su errno in testa a lib/libc.c. */
static void spiega_errore(const char *nome, int err)
{
    switch (err) {
        case E_ISDIR:
            printf("delete: '%s' e' una directory — usa rmdir\n", nome);
            break;
        case E_NOENT:
            printf("delete: '%s' non esiste\n", nome);
            break;
        case E_INVAL:
            printf("delete: '%s' non e' un nome valido\n", nome);
            break;
        default:
            printf("delete: '%s': %s\n", nome, strerror(errno));
            break;
    }
}

/* Chiede conferma prima di una cancellazione di massa. Ritorna 1 per
 * procedere. */
static int conferma(const char *dir, int quanti)
{
    char buf[64];
    int  n;

    printf("delete: stai per cancellare %d file da '%s'. Procedere? (s/n) ",
           quanti, dir);

    n = (int)read(0, buf, sizeof(buf) - 1);
    if (n <= 0) return 0;
    buf[n] = '\0';

    return (buf[0] == 's' || buf[0] == 'S');
}

static void uso(void)
{
    printf("Uso: delete <modello> [modello2 ...]\n");
    printf("Cancella uno o piu' file. Caratteri jolly:\n");
    printf("  ?   un carattere qualsiasi\n");
    printf("  *   una sequenza qualsiasi di caratteri\n");
    printf("Esempi: delete nota.txt | delete * | delete /temp/tmp*\n");
}

/* =============================================================================
 * elabora — gestisce un singolo argomento. Ritorna quanti file NON sono
 * stati cancellati.
 *
 * DUE FASI, e non è una complicazione gratuita.
 *
 * Fase 1: percorre TUTTA la directory (a blocchi) e raccoglie i nomi che
 *         corrispondono al modello.
 * Fase 2: cancella i nomi raccolti.
 *
 * Non si può cancellare mentre si elenca. La cancellazione marca la entry
 * come libera, e readdir salta le entry libere: le voci successive
 * scalerebbero all'indietro rispetto all'indice di paginazione, e a ogni
 * blocco si perderebbero tanti file quanti ne sono stati cancellati nel
 * blocco precedente. Raccogliere prima e cancellare dopo elimina il
 * problema alla radice.
 *
 * Raccogliere prima serve anche alla richiesta di conferma, che deve poter
 * dire un numero esatto invece di "forse tanti".
 * ============================================================================= */
static int elabora(const char *arg)
{
    static DirEntry voci[BLOCCO];
    static char     scelti[MAX_SELEZIONE][DIRENT_NAME_MAX];
    char dir[MAX_PATH];
    char modello[64];
    char completo[MAX_PATH];
    int  n, i;
    int  trovati = 0, cancellati = 0, falliti = 0;
    int  start = 0;
    int  troncato = 0;
    int  jolly;

    dividi(arg, dir, sizeof(dir), modello, sizeof(modello));

    if (modello[0] == '\0') {
        printf("delete: '%s' non indica alcun file\n", arg);
        return 1;
    }

    jolly = (strchr(modello, '*') != NULL) || (strchr(modello, '?') != NULL);

    /* Senza jolly non serve elencare nulla: si cancella il nome dato. */
    if (!jolly) {
        int r;
        unisci(completo, sizeof(completo), dir, modello);
        r = unlink(completo);
        if (r == 0) {
            printf("delete: cancellato '%s'\n", completo);
            return 0;
        }
        spiega_errore(completo, errno);
        return 1;
    }

    /* --- Fase 1: raccogli i nomi che corrispondono ---------------------- */
    for (;;) {
        n = listdir_from(dir, voci, BLOCCO, start);

        if (n < 0) {
            printf("delete: impossibile elencare '%s' (errore %d)\n", dir, n);
            return 1;
        }
        if (n == 0) break;

        for (i = 0; i < n; i++) {
            if (voci[i].is_dir) continue;              /* per quelle c'è rmdir */
            if (!corrisponde(modello, voci[i].name)) continue;

            if (trovati >= MAX_SELEZIONE) { troncato = 1; break; }
            strncpy(scelti[trovati], voci[i].name, DIRENT_NAME_MAX);
            scelti[trovati][DIRENT_NAME_MAX - 1] = '\0';
            trovati++;
        }

        if (troncato) break;
        if (n < BLOCCO) break;      /* ultima pagina */
        start += n;
    }

    if (troncato) {
        /* Dichiarato, non silenzioso: l'utente deve sapere che dovrà
         * ripetere il comando. */
        printf("delete: attenzione — piu' di %d file corrispondono; "
               "verranno cancellati i primi %d, ripeti il comando per gli altri\n",
               MAX_SELEZIONE, MAX_SELEZIONE);
    }

    if (trovati == 0) {
        printf("delete: nessun file corrisponde a '%s' in '%s'\n", modello, dir);
        return 1;
    }

    /* Conferma per le cancellazioni di massa. `delete *` su /bin
     * renderebbe il sistema inservibile: una domanda costa poco.
     * Un modello mirato come "tmp*" non la chiede. */
    if (strcmp(modello, "*") == 0 && trovati > 1) {
        if (!conferma(dir, trovati)) {
            printf("delete: annullato\n");
            return 0;
        }
    }

    /* --- Fase 2: cancella ---------------------------------------------- */
    for (i = 0; i < trovati; i++) {
        int r;

        unisci(completo, sizeof(completo), dir, scelti[i]);
        r = unlink(completo);
        if (r == 0) {
            printf("delete: cancellato '%s'\n", completo);
            cancellati++;
        } else {
            spiega_errore(completo, errno);
            falliti++;
        }
    }

    printf("delete: %d file cancellati da '%s'\n", cancellati, dir);
    return falliti;
}

int main(int argc, char **argv)
{
    int i;
    int falliti = 0;

    if (argc < 2) {
        uso();
        return 1;
    }

    for (i = 1; i < argc; i++) {
        falliti += elabora(argv[i]);
    }

    return falliti ? 1 : 0;
}

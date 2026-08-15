/* =============================================================================
 * bin/mkdir/mkdir.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * mkdir — crea una directory.
 *
 *   mkdir [-p] <nome> [nome2 ...]
 *
 *   -p   crea anche le directory intermedie, e non e' un errore se la
 *        directory c'e' gia'
 *
 * Accetta percorsi assoluti ("/dati") o relativi alla directory corrente
 * ("dati"): la risoluzione la fa il kernel, vedi resolve_path() in
 * kernel/syscall/syscall_impl.c.
 *
 * LIMITE DA CONOSCERE: il driver FAT12 di EX-OS risolve i percorsi a un
 * solo livello, quindi si possono creare directory solo nella root. Un
 * "mkdir /boot/sub" viene rifiutato con un messaggio esplicito invece di
 * creare una directory che poi nessuna altra funzione saprebbe
 * raggiungere. Vedi fat12_mkdir() in kernel/fs/fat12.c.
 * ============================================================================= */

#include "libc.h"

/* Codici errno restituiti dal kernel, in negativo. Duplicati qui con la
 * stessa convenzione usata altrove nel progetto: userspace e kernel non
 * condividono header. */
#define E_EXIST     17
#define E_NOSYS     38
#define E_NOSPC     28
#define E_NOENT      2
#define E_INVAL     22

static void uso(void)
{
    printf("Uso: mkdir [-p] <nome> [nome2 ...]\n");
    printf("Crea una o piu' directory.\n");
    printf("  -p  crea anche i livelli intermedi, e tace se esiste gia'\n");
    printf("Nota: su FAT le directory stanno solo nella root (un livello);\n");
    printf("      su ext2 no.\n");
}

/* Definita piu' sotto, accanto agli altri messaggi: qui serve solo il nome. */
static void spiega_errore(const char *nome, int err);

/* =============================================================================
 * -p: crea la fila di directory che porta a `percorso`, e tace su cio' che
 * c'e' gia'.
 *
 * ! SENZA QUESTO NON SI COSTRUISCE NIENTE. Ogni makefile del mondo crea le
 * proprie directory degli oggetti con `mkdir -p`, e conta su DUE cose che il
 * mkdir nudo non fa:
 *
 *   1. i livelli intermedi. `src/rtlib/obj/exos-x86` sono quattro livelli, e
 *      il mkdir nudo fallisce sul primo che manca;
 *   2. l'esito ZERO su una directory che esiste gia'. E' il caso NORMALE —
 *      la seconda volta che si lancia `make` ci sono tutte — e un esito
 *      diverso da zero li' ferma la ricetta.
 *
 * ! E TACE, a differenza della forma nuda che stampa «creata '...'». Quel
 * messaggio serve a chi digita al prompt; dentro una costruzione sarebbe una
 * riga di rumore per ogni directory, e POSIX chiede il silenzio.
 *
 * ! «ESISTE GIA'» SI ACCETTA SOLO SE E' UNA DIRECTORY. Se al posto suo c'e'
 * un FILE, POSIX dice che e' un errore, ed e' giusto: proseguire vorrebbe
 * dire che la ricetta dopo prova a scriverci dentro degli oggetti e fallisce
 * parlando d'altro.
 *
 * ! SI CHIEDE PRIMA `stat`, INVECE DI PROVARE mkdir E PERDONARE EEXIST.
 * La seconda strada sembra piu' diretta ed e' fragile, perche' presume di
 * sapere QUALE errore significa «c'e' gia'» — e non e' sempre EEXIST: su un
 * PUNTO DI MONTAGGIO il kernel risponde EBUSY. Con `mkdir -p /disk/a/b/c` su
 * un volume montato la fila si fermava sul primo pezzo, `/disk`, dicendo
 * «risorsa occupata»: un messaggio giusto per una domanda che non andava
 * fatta. Guardare se c'e' non richiede di indovinare niente.
 * ============================================================================= */
static int crea_uno(const char *percorso)
{
    struct stat st;

    if (stat(percorso, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;      /* c'e' gia', va bene cosi' */
        printf("mkdir: '%s' esiste e non e' una directory\n", percorso);
        return -1;
    }

    if (mkdir(percorso, 0755) == 0) return 0;

    /* Fra lo stat e la mkdir puo' essersela creata qualcun altro: se adesso
     * c'e' ed e' una directory, il risultato voluto c'e' comunque. */
    if (errno == E_EXIST && stat(percorso, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;

    spiega_errore(percorso, errno);
    return -1;
}

static int crea_fila(char *percorso)
{
    char *p;

    /* Si cammina lungo il percorso piantando un terminatore a ogni barra:
     * ogni giro guarda il prefisso letto fin li'. */
    for (p = percorso; *p; p++) {
        char salva;
        int  r;

        if (*p != '/' || p == percorso) continue;

        salva = *p;
        *p    = '\0';
        r     = crea_uno(percorso);
        *p    = salva;

        if (r != 0) return -1;
    }

    return crea_uno(percorso);
}

/* Traduce l'errore del kernel in una spiegazione utile. Un numero e basta
 * non direbbe niente a chi lo legge sullo schermo. */
/* ! PRENDE errno, NON IL VALORE DI RITORNO (agosto 2026). unlink(),
 * mkdir() e rmdir() rendono -1 come vuole POSIX, e `switch (-err)` su
 * quel -1 finiva sempre nel ramo predefinito stampando «errore -1»: un
 * messaggio che non dice niente proprio quando serve. Il codice vero sta
 * in errno. Vedi il commento su errno in testa a lib/libc.c. */
static void spiega_errore(const char *nome, int err)
{
    switch (err) {
        case E_EXIST:
            printf("mkdir: '%s' esiste gia'\n", nome);
            break;
        case E_NOSYS:
            printf("mkdir: '%s' — EX-OS supporta directory solo nella root; "
                   "un percorso annidato non sarebbe raggiungibile\n", nome);
            break;
        case E_NOSPC:
            printf("mkdir: '%s' — spazio esaurito (disco pieno o root directory "
                   "piena)\n", nome);
            break;
        case E_NOENT:
            printf("mkdir: percorso '%s' non valido\n", nome);
            break;
        case E_INVAL:
            printf("mkdir: nome '%s' non valido\n", nome);
            break;
        default:
            printf("mkdir: '%s': %s\n", nome, strerror(errno));
            break;
    }
}

int main(int argc, char **argv)
{
    int i = 1;
    int falliti = 0;
    int fila    = 0;    /* -p */

    for (; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "--") == 0) { i++; break; }
        if (a[0] != '-' || a[1] == '\0') break;

        {
            int k;

            for (k = 1; a[k]; k++) {
                switch (a[k]) {
                case 'p': fila = 1; break;
                default:
                    printf("mkdir: opzione sconosciuta: -%c\n", a[k]);
                    uso();
                    return 2;
                }
            }
        }
    }

    if (i >= argc) {
        uso();
        return 1;
    }

    for (; i < argc; i++) {
        int r;

        if (fila) {
            if (crea_fila(argv[i]) != 0) falliti++;
            continue;
        }

        r = mkdir(argv[i], 0755);
        if (r == 0) {
            printf("mkdir: creata '%s'\n", argv[i]);
        } else {
            spiega_errore(argv[i], errno);
            falliti++;
        }
    }

    /* Come i mkdir tradizionali: esito diverso da zero se almeno una
     * directory non e' stata creata, cosi' uno script puo' accorgersene. */
    return falliti ? 1 : 0;
}

/* =============================================================================
 * bin/install/install.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Installa EX-OS su un disco rigido.
 *
 *   install <punto>        installa sul volume gia' montato in <punto>
 *
 * Esempio d'uso completo, dal floppy di avvio:
 *
 *   disk                   guarda i dispositivi: hd0, hd0p1, ...
 *   mount hd0p1 /disk      il volume dev'essere in lettura/scrittura
 *   install /disk
 *
 * -----------------------------------------------------------------------
 * L'ORDINE DI COPIA NON E' CASUALE
 *
 * STAGE2.BIN e KERNEL.BIN vengono copiati PER PRIMI. Il settore di avvio
 * non sa leggere la FAT: riceve il primo settore e la lunghezza di quei
 * due file, quindi devono essere CONTIGUI sul volume. Su un volume con
 * spazio libero contiguo — quello appena formattato, il caso normale — un
 * file scritto per primo lo e' senz'altro; scritto dopo altre venti copie,
 * puo' non esserlo piu'.
 *
 * Se nonostante tutto risultano frammentati, il kernel RIFIUTA di
 * installare l'avvio invece di scrivere una mappa che comprende cluster
 * di altri file. Vedi kernel/boot/bootinst.c.
 *
 * -----------------------------------------------------------------------
 * I NOMI SI CREANO IN MINUSCOLO, E SU ext2 NON E' UN DETTAGLIO
 *
 * Il kernel cerca "/bin/sh", "/boot/kernel.cfg", "/dev/kbd.drv": tutto
 * minuscolo. Su FAT non importa — il driver mette in maiuscolo sia cio'
 * che scrive sia cio' che cerca, quindi "BIN" e "bin" sono la stessa
 * directory. Su ext2 sono due directory diverse, e un sistema installato
 * in "BIN" non troverebbe la propria shell.
 *
 * Creare in minuscolo funziona su entrambi: su FAT il driver converte da
 * solo, su ext2 il nome resta quello che il kernel cerchera'.
 *
 * -----------------------------------------------------------------------
 * REINSTALLARE SOSTITUISCE (dal 0.148)
 *
 * Ogni file viene riscritto anche se sulla destinazione ce n'e' gia' uno,
 * kernel compreso, e riletto per controllarne la dimensione. Prima i file
 * esistenti venivano saltati: aggiornare un sistema gia' installato
 * copiava solo i file nuovi e lasciava indietro il kernel, mentre la mappa
 * dei settori veniva riscritta per quello vecchio. Il disco ripartiva con
 * la versione di prima e l'installatore diceva "completata".
 *
 * Le directory no: esistono o non esistono, e ricrearle non vuol dire
 * niente. Quel che c'e' dentro e non fa parte del sistema resta dov'e' —
 * `install` aggiorna, non azzera il volume.
 *
 * Nel resoconto: '+' creato, '~' sostituito, '!' errore.
 *
 * -----------------------------------------------------------------------
 * COSA NON FA
 *
 * Non partiziona e non formatta. Sono due operazioni che distruggono dati
 * e vanno fatte con strumenti dedicati, con l'utente che sa cosa sta
 * cancellando — non come effetto collaterale di "installa".
 * ============================================================================= */
#include "libc.h"

#define BLOCCO      4096
#define PERC_MAX    128

static char buf[BLOCCO];
static int  errori = 0;

static const char *spiega(int e)
{
    switch (-e) {
        case 2:  return "non trovato";
        case 5:  return "errore di I/O";
        case 17: return "esiste gia'";
        case 19: return "dispositivo assente";
        case 21: return "e' una directory";
        case 22: return "argomento non valido";
        case 27: return "file troppo grande";
        case 28: return "spazio esaurito";
        case 29: return "file frammentato: ricopialo su un volume con spazio contiguo";
        case 30: return "montato in sola lettura";
        default: return "errore";
    }
}

/* Da FAT12 i nomi arrivano SEMPRE in maiuscolo: e' come il formato li
 * conserva, e l'informazione sul caso originale non esiste piu'. Copiarli
 * cosi' com'e' su ext2 — dove il caso conta — produrrebbe "/bin/SH" mentre
 * il kernel cerca "/bin/sh". Il minuscolo e' l'unica ricostruzione
 * sensata, ed e' quella che il sistema si aspetta. */
static void minuscolo(char *s)
{
    int i;
    for (i = 0; s[i]; i++)
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] - 'A' + 'a');
}

/* Concatena "punto" + "/" + "resto" senza sforare. */
static void unisci(char *out, const char *a, const char *b)
{
    int i = 0, j;

    for (j = 0; a[j] && i < PERC_MAX - 2; j++) out[i++] = a[j];
    if (i > 0 && out[i - 1] != '/') out[i++] = '/';
    for (j = 0; b[j] && i < PERC_MAX - 1; j++) out[i++] = b[j];
    out[i] = '\0';
}

/* Copia un file, SOSTITUENDO la destinazione se esiste. Ritorna 0, o <0.
 *
 * ⚠️ FINO AL 0.148 QUESTA FUNZIONE NON SOVRASCRIVEVA: se la destinazione
 * c'era gia', la considerava fatta e andava avanti. Sembrava prudenza —
 * "reinstallare sopra non deve fallire a meta'" — ed era invece il difetto
 * peggiore che un installatore possa avere: `install` su un sistema gia'
 * installato aggiornava i file NUOVI e lasciava indietro tutti gli altri,
 * kernel compreso. Il disco continuava ad avviare la versione vecchia
 * mentre l'installatore stampava "Installazione completata", e la mappa
 * dei settori veniva riscritta per quel kernel vecchio — cioe' il
 * risultato piu' convincente possibile di un aggiornamento che non e'
 * avvenuto.
 *
 * Ora si riscrive sempre. Un installatore che salta i file esistenti non
 * e' un installatore: e' una copia condizionale.
 *
 * La verifica finale sulla dimensione non e' pignoleria: e' cio' che
 * distingue "riscritto" da "riscritto a meta'" quando il volume si riempie
 * o il supporto ha un settore che non risponde — e senza, un kernel
 * troncato si scoprirebbe al riavvio successivo, che e' il momento in cui
 * non si puo' piu' fare niente. */
static int copia(const char *da, const char *a)
{
    int fs, fd, n, tot = 0;
    int esisteva;

    fd = open(a, O_RDONLY);
    esisteva = (fd >= 0);
    if (esisteva) close(fd);

    fs = open(da, O_RDONLY);
    if (fs < 0) {
        printf("  ! %s: %s\n", da, spiega(fs));
        errori++;
        return fs;
    }

    fd = open(a, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        close(fs);
        printf("  ! %s: %s\n", a, spiega(fd));
        errori++;
        return fd;
    }

    while ((n = (int)read(fs, buf, BLOCCO)) > 0) {
        int scritti = 0;
        while (scritti < n) {
            int w = (int)write(fd, buf + scritti, (unsigned int)(n - scritti));
            if (w <= 0) {
                printf("  ! %s: scrittura interrotta a %d byte\n", a, tot + scritti);
                close(fs); close(fd);
                errori++;
                return -1;
            }
            scritti += w;
        }
        tot += scritti;
    }

    close(fs);
    close(fd);

    if (n < 0) { printf("  ! %s: lettura interrotta\n", da); errori++; return n; }

    /* Rilettura di controllo: la destinazione deve esistere e avere
     * esattamente i byte che ci abbiamo scritto. */
    {
        int  v = open(a, O_RDONLY);
        long dim;

        if (v < 0) {
            printf("  ! %s: riscritto ma non rileggibile: %s\n", a, spiega(v));
            errori++;
            return v;
        }
        dim = fsize(v);
        close(v);

        if (dim != (long)tot) {
            printf("  ! %s: scritti %d byte, sul volume ce ne sono %ld\n",
                   a, tot, dim);
            errori++;
            return -1;
        }
    }

    printf("  %c %s  (%d byte)\n", esisteva ? '~' : '+', a, tot);
    return 0;
}

/* Crea una directory ignorando "esiste gia'". */
static void crea_dir(const char *p)
{
    int r = mkdir(p, 0755);
    if (r == 0)        printf("  + %s/\n", p);
    else if (r == -17) printf("  = %s/ (gia' presente)\n", p);
    else {
        printf("  ! %s/: %s\n", p, spiega(r));
        errori++;
    }
}

/* Copia tutto il contenuto di una directory del volume di avvio. */
static void copia_dir(const char *sorgente, const char *punto, const char *dest)
{
    DirEntry voci[LISTDIR_MAX_BATCH];
    char     pdest[PERC_MAX];
    unsigned int start = 0;
    int n;

    unisci(pdest, punto, dest);
    crea_dir(pdest);

    while ((n = listdir_from(sorgente, voci, LISTDIR_MAX_BATCH, start)) > 0) {
        int i;
        for (i = 0; i < n; i++) {
            char da[PERC_MAX], a[PERC_MAX];

            if (voci[i].is_dir) continue;          /* un livello solo */
            if (voci[i].name[0] == '.') continue;

            unisci(da, sorgente, voci[i].name);
            unisci(a,  pdest,    voci[i].name);
            minuscolo(a + strlen(pdest));   /* solo il NOME, non il punto */
            copia(da, a);
        }
        start += (unsigned int)n;
        if (n < LISTDIR_MAX_BATCH) break;
    }
}

int main(int argc, char **argv)
{
    char            p[PERC_MAX], q[PERC_MAX];
    BootInstallInfo info;
    int             r;

    if (argc != 2) {
        printf("uso: install <punto di montaggio>\n\n");
        printf("Il volume dev'essere gia' montato in lettura/scrittura:\n");
        printf("  disk                 elenca i dispositivi\n");
        printf("  mount hd0p1 /disk    monta\n");
        printf("  install /disk        installa\n\n");
        printf("Non partiziona e non formatta: sono operazioni distruttive\n");
        printf("e vanno fatte di proposito, non come effetto collaterale.\n");
        return 1;
    }

    printf("Installazione di EX-OS in %s\n", argv[1]);
    printf("  +  creato    ~  sostituito    !  errore\n\n");

    /* --- 1. i due file dell'avvio, PER PRIMI (vedi in testa al file) --- */
    printf("Avvio (copiati per primi: devono restare contigui)\n");
    unisci(p, argv[1], "boot");
    crea_dir(p);

    unisci(q, p, "stage2.bin");
    if (copia("/LOADER.BIN", q) < 0) {
        printf("\nInstallazione interrotta: senza Stage 2 il disco non parte.\n");
        return 1;
    }

    unisci(q, p, "kernel.bin");
    if (copia("/KERNEL.BIN", q) < 0) {
        printf("\nInstallazione interrotta: senza kernel il disco non parte.\n");
        return 1;
    }

    unisci(q, p, "kernel.cfg");
    copia("/boot/kernel.cfg", q);

    /* --- 2. il resto del sistema --- */
    printf("\nSistema\n");
    copia_dir("/bin", argv[1], "bin");
    copia_dir("/lib", argv[1], "lib");
    copia_dir("/dev", argv[1], "dev");

    /* --- 3. l'avvio vero e proprio --- */
    printf("\nSettori di avvio\n");
    r = bootinstall(argv[1], &info);
    if (r != 0) {
        printf("  ! installazione dell'avvio fallita: %s (errore %d)\n",
               spiega(r), r);
        printf("\nI file sono stati copiati, ma il disco NON e' avviabile.\n");
        return 1;
    }

    printf("  + MBR di hd%u, partizione %u marcata attiva\n",
           info.disco, info.voce);
    printf("  + settore di avvio della partizione\n");
    printf("    stage2: LBA %u, %u settori\n", info.s2_lba, info.s2_cnt);
    printf("    kernel: %u settori in %u intervall%s, dal LBA %u\n",
           info.k_cnt, info.k_next, (info.k_next == 1) ? "o" : "i", info.k_lba);
    if (info.k_next > 1) {
        printf("            (su ext2 il blocco di puntatori sta in mezzo ai\n");
        printf("             dati: il file e' contiguo a tratti, non in blocco)\n");
    }

    printf("\n");
    if (errori > 0) {
        printf("Completata con %d errori: controlla le righe con '!'.\n", errori);
        return 1;
    }

    printf("Installazione completata. Togli il floppy e riavvia.\n\n");
    printf("Ricorda: la mappa dei settori vale finche' quei file non si\n");
    printf("spostano. Se ricopi kernel o stage2 sul disco, rilancia install.\n");
    return 0;
}

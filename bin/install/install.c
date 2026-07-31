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

/* Concatena "punto" + "/" + "resto" senza sforare. */
static void unisci(char *out, const char *a, const char *b)
{
    int i = 0, j;

    for (j = 0; a[j] && i < PERC_MAX - 2; j++) out[i++] = a[j];
    if (i > 0 && out[i - 1] != '/') out[i++] = '/';
    for (j = 0; b[j] && i < PERC_MAX - 1; j++) out[i++] = b[j];
    out[i] = '\0';
}

/* Copia un file. Ritorna 0, o <0. Non sovrascrive: se la destinazione
 * esiste gia' la considera fatta e va avanti — reinstallare sopra
 * un'installazione precedente non deve fallire a meta'. */
static int copia(const char *da, const char *a)
{
    int fs, fd, n, tot = 0;

    fd = open(a, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        printf("  = %s (gia' presente)\n", a);
        return 0;
    }

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

    printf("  + %s  (%d byte)\n", a, tot);
    return 0;
}

/* Crea una directory ignorando "esiste gia'". */
static void crea_dir(const char *p)
{
    int r = mkdir(p);
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
    DirEntry voci[32];
    char     pdest[PERC_MAX];
    unsigned int start = 0;
    int n;

    unisci(pdest, punto, dest);
    crea_dir(pdest);

    while ((n = listdir_from(sorgente, voci, 32, start)) > 0) {
        int i;
        for (i = 0; i < n; i++) {
            char da[PERC_MAX], a[PERC_MAX];

            if (voci[i].is_dir) continue;          /* un livello solo */
            if (voci[i].name[0] == '.') continue;

            unisci(da, sorgente, voci[i].name);
            unisci(a,  pdest,    voci[i].name);
            copia(da, a);
        }
        start += (unsigned int)n;
        if (n < 32) break;
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

    printf("Installazione di EX-OS in %s\n\n", argv[1]);

    /* --- 1. i due file dell'avvio, PER PRIMI (vedi in testa al file) --- */
    printf("Avvio (copiati per primi: devono restare contigui)\n");
    unisci(p, argv[1], "BOOT");
    crea_dir(p);

    unisci(q, p, "STAGE2.BIN");
    if (copia("/LOADER.BIN", q) < 0) {
        printf("\nInstallazione interrotta: senza Stage 2 il disco non parte.\n");
        return 1;
    }

    unisci(q, p, "KERNEL.BIN");
    if (copia("/KERNEL.BIN", q) < 0) {
        printf("\nInstallazione interrotta: senza kernel il disco non parte.\n");
        return 1;
    }

    unisci(q, p, "KERNEL.CFG");
    copia("/boot/kernel.cfg", q);

    /* --- 2. il resto del sistema --- */
    printf("\nSistema\n");
    copia_dir("/bin", argv[1], "BIN");
    copia_dir("/lib", argv[1], "LIB");
    copia_dir("/dev", argv[1], "DEV");

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
    printf("    kernel: LBA %u, %u settori\n", info.k_lba,  info.k_cnt);

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

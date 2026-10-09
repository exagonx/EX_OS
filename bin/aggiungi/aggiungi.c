/* =============================================================================
 * bin/aggiungi/aggiungi.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * aggiungi — porta su un sistema installato i driver e i programmi del
 * supporto da cui viene lanciato (8 ottobre 2026)
 *
 *     /USB/HDD0p1/aggiungi        dalla chiavetta fatta con `make support`
 *     /cdrom/aggiungi             o dal CD dist/support.iso
 *
 * Il supporto ha accanto a questo programma due directory: dev/ (i driver,
 * che vanno in /dev) e bin/ (i programmi, che vanno in /bin). Li copia tutti,
 * sostituendo quelli che ci sono, poi guarda /boot/avvio.sh (o, dove non
 * c'e' `login`, /boot/autoexec.sh): se le
 * chiavette USB o la rete non vi sono accese aggiunge le righe che mancano. Al riavvio
 * `netdetect -c` trova la scheda e ne carica il driver.
 *
 * ! NON CHIEDE NIENTE. `cp` domanda prima di sostituire un file, e in uno
 * script la risposta se la prende dalla riga dopo: per questo e' un
 * programma e non un elenco di cp.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `aggiungi -version` la stampa. */
EX_VERSIONE("aggiungi", "0.002");

#define BLOCCO        4096
#define PERCORSO_MAX  256
/* ! DUE FILE, E NON SONO LA STESSA COSA (8 ottobre 2026). Su un sistema
 * installato c'e' `login`, e /boot/autoexec.sh lo esegue la shell DOPO che
 * qualcuno e' entrato dalla console: rete e telnetd messi li' non partono
 * finche' nessuno si siede alla tastiera. /boot/avvio.sh lo esegue `login`
 * da root, una volta, PRIMA dell'accesso: e' quello il posto. Senza `login`
 * (floppy, CD) c'e' solo l'autoexec. */
#define AUTOEXEC_SH   "/boot/autoexec.sh"
#define AVVIO_SH      "/boot/avvio.sh"
static const char *AUTOEXEC = AUTOEXEC_SH;
#define AUTOEXEC_MAX  8192
#define AGGIUNTE_MAX  1024

static char g_buf[BLOCCO];
static char g_auto[AUTOEXEC_MAX + AGGIUNTE_MAX];

/* Copia un file. 0 nuovo, 1 sostituito, -1 errore. */
static int copia(const char *src, const char *dst)
{
    struct stat st;
    int fs, fd, n, c_era = (stat(dst, &st) == 0);

    fs = open(src, O_RDONLY);
    if (fs < 0) { printf("  %s: %s\n", src, strerror(errno)); return -1; }
    fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("  %s: %s\n", dst, strerror(errno)); close(fs); return -1; }

    while ((n = (int)read(fs, g_buf, BLOCCO)) > 0) {
        int fatti = 0;

        while (fatti < n) {
            int w = (int)write(fd, g_buf + fatti, (unsigned int)(n - fatti));

            if (w <= 0) {
                printf("  %s: scrittura fallita (disco pieno?)\n", dst);
                close(fs); close(fd);
                return -1;
            }
            fatti += w;
        }
    }
    close(fs);
    close(fd);
    return c_era;
}

/* Tutti i file di <supporto>/<da> in <verso>. Rende quanti ne ha copiati. */
static int copia_directory(const char *supporto, const char *da, const char *verso,
                           int *errori)
{
    char           dir[PERCORSO_MAX], src[PERCORSO_MAX], dst[PERCORSO_MAX];
    struct stat    st;
    struct dirent *e;
    DIR           *d;
    int            fatti = 0, k;

    snprintf(dir, sizeof(dir), "%s/%s", supporto, da);
    d = opendir(dir);
    if (d == NULL) {
        printf("aggiungi: %s non c'e' su questo supporto\n", dir);
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        int r;

        if (e->d_name[0] == '.') continue;
        snprintf(src, sizeof(src), "%s/%s", dir, e->d_name);
        snprintf(dst, sizeof(dst), "%s/%s", verso, e->d_name);
        /* ! IN MINUSCOLO. Una chiavetta FAT rende i nomi corti in maiuscolo
         * (RTL8169.DRV), e sul disco, che distingue, sarebbe un altro file
         * accanto a quello vero. I nomi di sistema sono tutti minuscoli. */
        for (k = (int)strlen(verso) + 1; dst[k]; k++)
            if (dst[k] >= 'A' && dst[k] <= 'Z') dst[k] = (char)(dst[k] + 32);
        if (stat(src, &st) != 0 || S_ISDIR(st.st_mode)) continue;

        r = copia(src, dst);
        if (r < 0) { (*errori)++; continue; }
        printf("  %-28s %s\n", dst, r ? "sostituito" : "nuovo");
        fatti++;
    }
    closedir(d);
    return fatti;
}

/* La rete nell'autoexec: se manca `netdetect -c` si aggiungono in coda le
 * righe che la accendono. Rende 1 se ha scritto, 0 se c'era gia', -1 errore. */
static int sistema_autoexec(void)
{
    static const char righe[] =
        "\n# rete: aggiunta da `aggiungi` (make support)\n"
        "netdetect -c\n"
        "/dev/ip.drv &\n"
        "dhcp\n";
    static const char usb[] =
        "\n# chiavette USB: aggiunte da `aggiungi` (make support)\n"
        "/dev/ehci.drv -avvio &\n"
        "/dev/ohci.drv -avvio &\n"
        "/dev/uhci.drv -avvio &\n"
        "automount &\n";
    int fd, n, tot = 0;

    fd = open(AUTOEXEC, O_RDONLY);
    if (fd >= 0) {
        while (tot < AUTOEXEC_MAX && (n = (int)read(fd, g_auto + tot, (unsigned int)(AUTOEXEC_MAX - tot))) > 0)
            tot += n;
        close(fd);
    }
    g_auto[tot] = 0;
    int ha_rete, ha_usb;

    ha_rete = strstr(g_auto, "netdetect -c") != NULL;
    ha_usb  = strstr(g_auto, "automount") != NULL;
    if (ha_rete && ha_usb) return 0;

    if (strstr(g_auto, "pci.drv") == NULL) {
        memcpy(g_auto + tot, "\n/dev/pci.drv &", 15);
        tot += 15;
    }
    /* Le chiavette: senza queste righe un sistema avviato dal disco non
     * vede nemmeno il supporto da cui si e' appena copiato. */
    if (!ha_usb) {
        memcpy(g_auto + tot, usb, sizeof(usb) - 1);
        tot += (int)sizeof(usb) - 1;
    }
    if (!ha_rete) {
        memcpy(g_auto + tot, righe, sizeof(righe) - 1);
        tot += (int)sizeof(righe) - 1;
    }

    fd = open(AUTOEXEC, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("aggiungi: %s: %s\n", AUTOEXEC, strerror(errno)); return -1; }
    n = (int)write(fd, g_auto, (unsigned int)tot);
    close(fd);
    return n == tot ? 1 : -1;
}

int main(int argc, char **argv)
{
    char supporto[PERCORSO_MAX];
    char *barra;
    int  driver, programmi, errori = 0, a;

    if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
        printf("uso: <supporto>/aggiungi [directory del supporto]\n");
        printf("  copia dev/ in /dev e bin/ in /bin, e accende la rete in %s\n", AUTOEXEC);
        return 2;
    }

    /* Il supporto e' la directory da cui si viene lanciati. */
    strncpy(supporto, argc == 2 ? argv[1] : argv[0], sizeof(supporto) - 1);
    supporto[sizeof(supporto) - 1] = 0;
    if (argc != 2) {
        barra = strrchr(supporto, '/');
        if (barra == NULL) {
            printf("aggiungi: lanciami col percorso intero (es. /cdrom/aggiungi),\n"
                   "          o dimmi dov'e' il supporto:  aggiungi /cdrom\n");
            return 2;
        }
        *barra = 0;
    }

    printf("aggiungi: dal supporto %s\n", supporto[0] ? supporto : "/");
    printf("Driver, in /dev:\n");
    driver = copia_directory(supporto, "dev", "/dev", &errori);
    printf("Programmi, in /bin:\n");
    programmi = copia_directory(supporto, "bin", "/bin", &errori);

    {
        struct stat st;

        if (stat("/bin/login", &st) == 0) AUTOEXEC = AVVIO_SH;
    }
    a = sistema_autoexec();
    if (a > 0)       printf("%s: aggiunte le righe che accendono chiavette e rete\n", AUTOEXEC);
    else if (a == 0) printf("%s: chiavette e rete ci sono gia', non lo tocco\n", AUTOEXEC);
    else             errori++;

    printf("\naggiungi: %d driver e %d programmi copiati", driver, programmi);
    if (errori) printf(", %d ERRORI (vedi sopra)", errori);
    printf(".\n");
    if (driver + programmi > 0 && !errori)
        printf("Adesso: shutdown, togli il supporto e riaccendi. All'avvio\n"
               "`netdetect -c` sceglie la scheda di rete e ne carica il driver.\n");
    return errori ? 1 : 0;
}

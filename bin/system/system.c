/* =============================================================================
 * bin/system/system.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * =============================================================================
 *
 * Togliere e rimettere il disco di sistema a macchina accesa (@RDBOOT passo 5,
 * 23 ottobre 2026)
 *
 *   system -status          chi fa da radice: il disco o il sistema di base
 *   system -unload [-yes]   chiude i programmi partiti dal disco e lo smonta:
 *                           / torna il sistema di base (/rdboot)
 *   system -load [-check] [<partizione>]
 *                           lo rimonta su / e riparte dalle configurazioni:
 *                           i driver di [modules] e i montaggi di [mount]
 *                           di /boot/kernel.cfg, poi /boot/avvio.sh
 *
 * ! A COSA SERVE. Con il disco di sistema montato su / non si puo' ne'
 * ripartizionarlo (fdisk), ne' controllarlo (chkdsk -r vuole un volume non
 * montato), ne' riscriverne l'avvio a mano. Il sistema di base in RAM ha
 * tutto quello che serve per farlo: shell, login, ps, kill, mount, fdisk,
 * chkdsk. -unload lascia la macchina li' sopra, -load la riporta com'era.
 *
 * ! CHI SOPRAVVIVE. Tutto cio' che e' partito da /rdboot: la tastiera, il
 * login e la shell delle console (dal 23 ottobre kernel e login prendono la
 * copia della base), e questo programma. Chi e' partito dal disco - la rete,
 * telnetd, la scrivania, extaskbar, i driver USB installati - si chiude:
 * prima chiedendo (TERM), dopo cinque secondi senza chiedere (KILL). Una
 * sessione telnet quindi CADE: -load la riaccende con avvio.sh.
 *
 * ! LA DIRECTORY CORRENTE non blocca lo smontaggio: chi stava in una
 * directory che la base non ha (la casa di root, /root) si ritrova in /. Lo
 * fa il kernel, in sys_umount.
 *
 * ! LA PARTIZIONE SI RICORDA IN /rdboot/tmp/system.dev: e' sul sistema di
 * base, quindi c'e' anche a disco tolto. -load senza partizione usa quella.
 *
 * Sta in /rdboot/bin, ovviamente: dal disco si chiuderebbe da solo.
 * ============================================================================= */
#include "libc.h"

/* +0.001 a ogni modifica: `system -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("system", "0.001");

#define FILE_DEV     "/rdboot/tmp/system.dev"
#define KERNEL_CFG   "/boot/kernel.cfg"
#define AVVIO_SH     "/boot/avvio.sh"
#define SHELL_BASE   "/rdboot/bin/sh"
#define CHKDSK_BASE  "/rdboot/bin/chkdsk"
#define SYSTEM_BASE  "/rdboot/bin/system"
#define MAX_PROC     128

/* il dispositivo montato su un punto, "" se nessuno */
static void dev_di(const char *punto, char *dev, unsigned int max)
{
    MountInfo m[4];
    unsigned int start = 0;
    int n, i;

    dev[0] = '\0';
    while ((n = mountinfo(m, 4, start)) > 0) {
        for (i = 0; i < n; i++)
            if (strcmp(m[i].punto, punto) == 0) {
                strncpy(dev, m[i].dev, max - 1);
                dev[max - 1] = '\0';
                return;
            }
        start += (unsigned int)n;
    }
}

/* la radice e' il sistema di base? */
static int radice_e_base(char *dev, unsigned int max)
{
    dev_di("/", dev, max);
    return strcmp(dev, "rdb0") == 0;
}

/* i processi che usano "/" (il disco) e vanno chiusi: hanno li' il programma
 * o dei file aperti. Una directory corrente da sola non conta. */
static int chi_usa(ProcUso *out, int max)
{
    ProcUso b[16];
    unsigned int start = 0;
    int n, i, k = 0, io = getpid();

    while ((n = proc_uso(b, 16, start, "/")) > 0) {
        for (i = 0; i < n; i++) {
            if ((int)b[i].pid == io || b[i].pid <= 2) continue;
            if (!(b[i].perche & (PROCUSO_PROGRAMMA | PROCUSO_FILE))) continue;
            if (k < max) out[k++] = b[i];
        }
        start += (unsigned int)n;
    }
    return k;
}

static void elenca(const ProcUso *p, int n)
{
    int i;

    for (i = 0; i < n; i++)
        printf("  %-5u %-22s %s%s%s\n", p[i].pid, p[i].name,
               (p[i].perche & PROCUSO_PROGRAMMA) ? "il programma " : "",
               (p[i].perche & PROCUSO_FILE) ? "file aperti " : "",
               p[i].state == 4 ? "(terminato, aspetta il padre)" : "");
}

static int scarica(int senza_chiedere)
{
    static ProcUso p[MAX_PROC];
    char dev[32];
    int  n, i, giri, rc;
    FILE *f;

    if (radice_e_base(dev, sizeof(dev))) {
        printf("system: il disco di sistema non c'e' gia': / e' il sistema di base\n");
        return 0;
    }
    if (dev[0] == '\0') { printf("system: niente montato su /\n"); return 1; }

    n = chi_usa(p, MAX_PROC);
    printf("Disco di sistema: %s su /\n", dev);
    if (n > 0) {
        printf("Questi programmi stanno sul disco, o ci tengono file aperti, e\n"
               "vanno chiusi (la rete e una sessione telnet cadono: `system\n"
               "-load` le riaccende):\n");
        elenca(p, n);
    } else
        printf("Nessun programma lo usa.\n");

    if (!senza_chiedere) {
        char r[16];
        int  l;

        printf("Procedo? [si/no] ");
        l = (int)read(0, r, sizeof(r) - 1);
        if (l < 0) l = 0;
        r[l] = '\0';
        if (r[0] != 's' && r[0] != 'S') { printf("Annullato.\n"); return 1; }
    }

    /* prima si chiede, poi si impone */
    for (i = 0; i < n; i++) if (p[i].state != 4) kill((int)p[i].pid, SIGTERM);
    for (giri = 0; giri < 50 && (n = chi_usa(p, MAX_PROC)) > 0; giri++) usleep(100000);
    if (n > 0) {
        for (i = 0; i < n; i++) if (p[i].state != 4) kill((int)p[i].pid, SIGKILL);
        for (giri = 0; giri < 30 && (n = chi_usa(p, MAX_PROC)) > 0; giri++) usleep(100000);
    }

    rc = umount("/");                       /* -errno, senza toccare errno */
    if (rc != 0) {
        errno = -rc;
        printf("system: %s non si smonta (errno %d). Chi lo usa ancora:\n", dev, errno);
        n = chi_usa(p, MAX_PROC);
        elenca(p, n);
        if (n == 0) printf("  (nessun processo: file aperti dal kernel, riprova fra poco)\n");
        return 1;
    }

    f = fopen(FILE_DEV, "w");
    if (f) { fprintf(f, "%s\n", dev); fclose(f); }

    printf("\nFatto: %s non e' piu' montato, / e' il sistema di base.\n", dev);
    printf("Ora si puo', per esempio:  chkdsk -r %s   fdisk ...\n", dev);
    printf("`system -load` lo rimette su / e riaccende il resto.\n");
    return 0;
}

/* --- -load ------------------------------------------------------------------ */

static const char *nome_base(const char *p)
{
    const char *s = strrchr(p, '/');

    return s ? s + 1 : p;
}

/* c'e' gia' un processo vivo che gira quel programma? ! Il kernel chiama i
 * driver di [modules] col NOME della voce ("kbd"), una shell col percorso
 * ("/dev/pci.drv"): si guardano tutti e due. */
static int gira_gia(const char *nome, const char *percorso)
{
    ProcUso b[16];
    unsigned int start = 0;
    int n, i;

    while ((n = proc_uso(b, 16, start, NULL)) > 0) {
        for (i = 0; i < n; i++)
            if (b[i].state != 4 && (strcmp(nome_base(b[i].name), nome_base(percorso)) == 0 ||
                                    strcmp(b[i].name, nome) == 0))
                return 1;
        start += (unsigned int)n;
    }
    return 0;
}

static void togli_spazi(char *s)
{
    char *a = s, *z;

    while (*a == ' ' || *a == '\t') a++;
    if (a != s) memmove(s, a, strlen(a) + 1);
    z = s + strlen(s);
    while (z > s && (z[-1] == ' ' || z[-1] == '\t' || z[-1] == '\r' || z[-1] == '\n')) *--z = '\0';
}

/* [modules] e [mount] di kernel.cfg, come li usa il kernel all'avvio */
static void riparti_da_cfg(void)
{
    FILE *f = fopen(KERNEL_CFG, "r");
    char  riga[256], sezione[32] = "";

    if (!f) { printf("system: %s non si legge: niente driver ne' montaggi\n", KERNEL_CFG); return; }
    while (fgets(riga, sizeof(riga), f)) {
        char *uguale, chiave[128], valore[128];

        togli_spazi(riga);
        if (riga[0] == '#' || riga[0] == '\0') continue;
        if (riga[0] == '[') {
            char *z = strchr(riga, ']');

            if (z) *z = '\0';
            strncpy(sezione, riga + 1, sizeof(sezione) - 1);
            continue;
        }
        uguale = strchr(riga, '=');
        if (!uguale) continue;
        *uguale = '\0';
        strncpy(chiave, riga, sizeof(chiave) - 1); chiave[sizeof(chiave) - 1] = '\0';
        strncpy(valore, uguale + 1, sizeof(valore) - 1); valore[sizeof(valore) - 1] = '\0';
        togli_spazi(chiave); togli_spazi(valore);

        if (strcmp(sezione, "modules") == 0) {
            char *argv[2];

            if (gira_gia(chiave, valore)) continue;
            argv[0] = valore; argv[1] = NULL;
            if (spawn(valore, argv) < 0)
                printf("system: il driver %s (%s) non parte (errno %d)\n", chiave, valore, errno);
            else
                printf("  driver %s\n", valore);
        } else if (strcmp(sezione, "mount") == 0) {
            char dev[32];
            unsigned int fl = 0;
            char *virgola = strchr(valore, ',');

            if (virgola) { if (strcmp(virgola, ",ro") == 0) fl = MNT_SOLA_LETTURA; *virgola = '\0'; }
            dev_di(chiave, dev, sizeof(dev));
            if (dev[0]) continue;
            if (mount(valore, chiave, fl) == 0) printf("  montato %s su %s\n", valore, chiave);
            /* un CD senza disco dentro non e' un errore da dire */
        }
    }
    fclose(f);
}

static int carica(int controlla, const char *chiesto)
{
    char dev[32], ora[32];
    int  rc;

    if (!radice_e_base(ora, sizeof(ora))) {
        printf("system: il disco di sistema c'e' gia' (%s su /)\n", ora);
        return 0;
    }
    dev[0] = '\0';
    if (chiesto) strncpy(dev, chiesto, sizeof(dev) - 1), dev[sizeof(dev) - 1] = '\0';
    else {
        FILE *f = fopen(FILE_DEV, "r");

        if (f) { if (fgets(dev, sizeof(dev), f)) togli_spazi(dev); fclose(f); }
    }
    if (dev[0] == '\0') {
        printf("system: quale partizione? (system -load <partizione>; `disk` le elenca)\n");
        return 1;
    }

    if (controlla) {
        char *argv[4];
        int   pid, stato = 0;

        argv[0] = "chkdsk"; argv[1] = "-r"; argv[2] = dev; argv[3] = NULL;
        printf("Controllo %s (chkdsk -r)...\n", dev);
        pid = spawn(CHKDSK_BASE, argv);
        if (pid < 0) printf("system: %s non parte: monto senza controllo\n", CHKDSK_BASE);
        else waitpid(pid, &stato, 0);
    }

    rc = mount(dev, "/", 0);
    if (rc != 0) {
        errno = -rc;
        printf("system: %s non si monta su / (errno %d). Resti sul sistema di base;\n"
               "        prova `chkdsk -r %s`, o `system -load -check`.\n", dev, errno, dev);
        return 1;
    }
    unlink(FILE_DEV);
    printf("%s di nuovo su /. Riaccendo:\n", dev);
    riparti_da_cfg();

    if (access(AVVIO_SH, F_OK) == 0) {
        char *argv[3];
        int   pid, stato = 0;

        argv[0] = "sh"; argv[1] = AVVIO_SH; argv[2] = NULL;
        printf("  %s\n", AVVIO_SH);
        pid = spawn(SHELL_BASE, argv);
        if (pid < 0) printf("system: %s non parte (errno %d)\n", AVVIO_SH, errno);
        else waitpid(pid, &stato, 0);
    }
    printf("Fatto.\n");
    return 0;
}

static void uso(void)
{
    printf("uso: system -status                 chi fa da radice\n");
    printf("     system -unload [-yes]          chiude i programmi del disco di\n");
    printf("                                    sistema e lo smonta: / torna il\n");
    printf("                                    sistema di base (/rdboot)\n");
    printf("     system -load [-check] [<part>] lo rimonta su / e riaccende driver,\n");
    printf("                                    montaggi e /boot/avvio.sh; -check\n");
    printf("                                    prima lo controlla (chkdsk -r)\n");
}

int main(int argc, char **argv)
{
    char dev[32];
    int  i, si = 0, controlla = 0;
    const char *part = NULL;

    if (argc < 2) { uso(); return 1; }
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-yes") == 0 || strcmp(argv[i], "-y") == 0) si = 1;
        else if (strcmp(argv[i], "-check") == 0) controlla = 1;
        else if (argv[i][0] != '-') part = argv[i];
        else { uso(); return 1; }
    }

    if (strcmp(argv[1], "-status") == 0) {
        if (radice_e_base(dev, sizeof(dev)))
            printf("/ e' il sistema di base: il disco di sistema non e' montato (system -load)\n");
        else
            printf("/ e' il disco di sistema (%s); il sistema di base e' su /rdboot\n",
                   dev[0] ? dev : "?");
        return 0;
    }
    if (strcmp(argv[1], "-unload") != 0 && strcmp(argv[1], "-load") != 0) {
        uso();
        return strcmp(argv[1], "-h") == 0 ? 0 : 1;
    }
    if (getuid() != 0) { printf("system: %s solo da root\n", argv[1]); return 1; }

    /* ! SE QUESTO PROGRAMMA E' QUELLO DEL DISCO, SI PASSA A QUELLO DELLA BASE.
     * Il PATH mette /bin prima di /rdboot/bin, e /bin/system c'e' (netupdate
     * lo installa come ogni altro): partito da li', terrebbe occupato il disco
     * che deve smontare - ed e' l'unico processo che non puo' chiudere. */
    {
        ProcUso b[16];
        unsigned int start = 0;
        int n, k, io = getpid(), dal_disco = 0;

        while (!dal_disco && (n = proc_uso(b, 16, start, "/")) > 0) {
            for (k = 0; k < n; k++)
                if ((int)b[k].pid == io && (b[k].perche & PROCUSO_PROGRAMMA)) dal_disco = 1;
            start += (unsigned int)n;
        }
        if (dal_disco && access(SYSTEM_BASE, X_OK) == 0) {
            execv(SYSTEM_BASE, argv);
            printf("system: non riesco a passare a %s (errno %d)\n", SYSTEM_BASE, errno);
            return 1;
        }
    }
    dev_di("/rdboot", dev, sizeof(dev));
    if (dev[0] == '\0') {
        printf("system: questa macchina non ha il sistema di base (/rdboot): il disco\n"
               "        di sistema non si puo' togliere. Vedi `netupdate -avvio`.\n");
        return 1;
    }
    if (strcmp(argv[1], "-unload") == 0) return scarica(si);
    return carica(controlla, part);
}

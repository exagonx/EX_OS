/* =============================================================================
 * bin/cpu/cpu.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * cpu - the processors, and which process runs where (kernel 0.319, voce 92)
 *
 *     cpu                     the processors at work, and who has one reserved
 *     cpu -p PID              where the threads of process PID may run
 *     cpu -p PID LISTA        PID's threads only on LISTA ("1,3-5", "tutti")
 *     cpu -r PID N            processor N only for PID, whose threads go
 *                             there; nobody else runs on it (root)
 *     cpu -l N                processor N back to everybody (root)
 *     cpu -x LISTA COMANDO... starts COMANDO already on LISTA
 *     cpu -R N COMANDO...     starts COMANDO with processor N reserved to it,
 *                             and waits (root): `cpu -R 2 /bin/servizio &` in
 *                             /boot/avvio.sh gives a service its own core
 *
 * Numbers are the kernel's: 0 is the boot processor, the others in the order
 * they began to work (hwinfo shows each one's APIC). Processor 0 cannot be
 * reserved: it runs the kernel's own tasks and the devices' interrupts.
 *
 * ! -p ACTS ON EVERY THREAD OF THE PROCESS, read from /proc/PID/task: a
 * program with sixty threads (Firefox) moved one thread at a time would
 * leave fifty-nine where they were. Threads born later inherit the set of
 * the thread that creates them.
 * ============================================================================= */

#include "libc.h"

EX_VERSIONE("cpu", "0.001");

static unsigned int g_n;        /* processors at work */
static unsigned int g_parole;   /* 32-bit words of a set */

static void uso(void)
{
    printf("uso: cpu                      i processori al lavoro e chi ne ha uno riservato\n"
           "     cpu -p PID               dove possono girare i fili del processo PID\n"
           "     cpu -p PID LISTA         i fili di PID solo sui processori di LISTA\n"
           "                              (es. 1,3-5 oppure tutti)\n"
           "     cpu -r PID N             il processore N solo per PID, e PID solo li' (root)\n"
           "     cpu -l N                 il processore N di nuovo di tutti (root)\n"
           "     cpu -x LISTA COMANDO...  lancia COMANDO gia' fissato su LISTA\n"
           "     cpu -R N COMANDO...      lancia COMANDO col processore N riservato (root)\n"
           "  0 e' il processore d'avvio e non si puo' riservare.\n");
}

static const char *errore(int r)
{
    switch (-r) {
    case 1:  return "serve root (o il processo non e' tuo)";
    case 3:  return "nessun processo con quel numero";
    case 16: return "il processore e' gia' riservato a un altro processo";
    case 22: return "non resterebbe nessun processore utilizzabile, o numero non valido";
    case 38: return "il kernel non lo sa fare (serve la 0.319)";
    default: return "errore";
    }
}

/* "1,3-5" or "tutti" into a set of g_parole words. 0 = good. */
static int lista(const char *s, unsigned int *set)
{
    unsigned int i;

    for (i = 0; i < g_parole; i++) set[i] = 0;
    if (strcmp(s, "tutti") == 0 || strcmp(s, "all") == 0) {
        for (i = 0; i < g_n; i++) set[i / 32] |= 1u << (i % 32);
        return 0;
    }
    while (*s) {
        unsigned int a, b;

        if (*s < '0' || *s > '9') return -1;
        a = (unsigned int)atoi(s);
        while (*s >= '0' && *s <= '9') s++;
        b = a;
        if (*s == '-') {
            s++;
            if (*s < '0' || *s > '9') return -1;
            b = (unsigned int)atoi(s);
            while (*s >= '0' && *s <= '9') s++;
        }
        if (b < a || b >= g_n) {
            printf("cpu: processore %u: ce ne sono %u al lavoro (0-%u)\n", b, g_n, g_n - 1);
            return -1;
        }
        for (i = a; i <= b; i++) set[i / 32] |= 1u << (i % 32);
        if (*s == ',') s++;
        else if (*s) return -1;
    }
    return 0;
}

static void stampa_set(const unsigned int *set)
{
    unsigned int i, primo = 1;

    for (i = 0; i < g_n; i++) {
        unsigned int j;

        if (!(set[i / 32] & (1u << (i % 32)))) continue;
        for (j = i; j + 1 < g_n && (set[(j + 1) / 32] & (1u << ((j + 1) % 32))); j++) ;
        printf(primo ? "%u" : ",%u", i);
        if (j > i) printf("-%u", j);
        primo = 0;
        i = j;
    }
    if (primo) printf("(nessuno)");
}

/* The name of process pid, from /proc/PID/status. */
static void nome_di(unsigned int pid, char *nome, size_t max)
{
    char  p[48], riga[128];
    FILE *f;

    snprintf(nome, max, "?");
    snprintf(p, sizeof(p), "/proc/%u/status", pid);
    f = fopen(p, "r");
    if (f == NULL) return;
    while (fgets(riga, sizeof(riga), f)) {
        if (strncmp(riga, "Name:", 5) == 0) {
            char *s = riga + 5;
            size_t l;

            while (*s == ' ' || *s == '\t') s++;
            l = strlen(s);
            while (l > 0 && (s[l - 1] == '\n' || s[l - 1] == '\r')) s[--l] = 0;
            snprintf(nome, max, "%s", s);
            break;
        }
    }
    fclose(f);
}

static int elenco(void)
{
    CpuInfo      *ci = cpu_info_tutti();
    unsigned int *chi = (unsigned int *)calloc(g_n, sizeof(unsigned int));
    unsigned int  i, k;

    if (ci == NULL || chi == NULL) { printf("cpu: non leggo i processori\n"); return 1; }
    (void)cpu_riserve(chi, g_n);
    printf("processori al lavoro: %u (elencati %u)\n", g_n, ci->n);
    for (k = 0; k < g_n; k++) {
        unsigned int apic = 0;

        for (i = 0; i < ci->n; i++)
            if (ci->cpu[i].numero == k &&
                (ci->cpu[i].stato == CPU_STATO_AVVIO || ci->cpu[i].stato == CPU_STATO_LAVORA))
                apic = ci->cpu[i].apic_id;
        printf("  cpu %-3u APIC %-3u ", k, apic);
        if (k == 0)
            printf("d'avvio: kernel e periferiche, e programmi\n");
        else if (chi[k] != 0) {
            char nome[64];

            nome_di(chi[k], nome, sizeof(nome));
            printf("RISERVATO a PID %u (%s)\n", chi[k], nome);
        } else
            printf("di tutti\n");
    }
    free(chi);
    free(ci);
    return 0;
}

/* Every thread of pid: its tids from /proc/PID/task, or pid alone. Calls
 * fa(tid, set) on each; returns how many failed, the last error in *err. */
static int su_ogni_filo(unsigned int pid, int (*fa)(int, unsigned int *), unsigned int *set, int *err)
{
    char  p[48];
    DIR  *d;
    struct dirent *e;
    int   male = 0, visti = 0;

    snprintf(p, sizeof(p), "/proc/%u/task", pid);
    d = opendir(p);
    if (d != NULL) {
        while ((e = readdir(d)) != NULL) {
            int r;

            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
            visti++;
            r = fa(atoi(e->d_name), set);
            if (r < 0) { male++; *err = r; }
        }
        closedir(d);
    }
    if (visti == 0) {
        int r = fa((int)pid, set);

        if (r < 0) { male++; *err = r; }
    }
    return male;
}

static int scrivi(int tid, unsigned int *set) { return cpu_aff_scrivi(tid, set, g_parole); }
static int leggi(int tid, unsigned int *set)  { return cpu_aff_leggi(tid, set, g_parole); }

int main(int argc, char **argv)
{
    CpuInfo      *ci;
    unsigned int *set;
    unsigned int  i;
    int           r, err = 0;

    ci = cpu_info_tutti();
    if (ci == NULL) { printf("cpu: non leggo i processori (kernel troppo vecchio?)\n"); return 1; }
    g_n = 0;
    for (i = 0; i < ci->n; i++)
        if (ci->cpu[i].stato == CPU_STATO_AVVIO || ci->cpu[i].stato == CPU_STATO_LAVORA) g_n++;
    if (g_n == 0) g_n = 1;
    free(ci);
    g_parole = (g_n + 31) / 32;
    set = (unsigned int *)calloc(g_parole, sizeof(unsigned int));
    if (set == NULL) return 1;

    if (argc == 1) return elenco();
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) { uso(); return 0; }

    if (strcmp(argv[1], "-p") == 0 && argc >= 3) {
        unsigned int pid = (unsigned int)atoi(argv[2]);

        if (argc >= 4) {
            if (lista(argv[3], set) != 0) { uso(); return 1; }
            if (su_ogni_filo(pid, scrivi, set, &err) > 0) {
                printf("cpu: PID %u: %s\n", pid, errore(err));
                return 1;
            }
        }
        r = leggi((int)pid, set);
        if (r < 0) { printf("cpu: PID %u: %s\n", pid, errore(r)); return 1; }
        printf("PID %u: processori ", pid);
        stampa_set(set);
        printf("\n");
        return 0;
    }
    if (strcmp(argv[1], "-r") == 0 && argc >= 4) {
        r = cpu_riserva(atoi(argv[2]), (unsigned int)atoi(argv[3]));
        if (r < 0) { printf("cpu: %s\n", errore(r)); return 1; }
        printf("processore %s riservato a PID %s\n", argv[3], argv[2]);
        return 0;
    }
    if (strcmp(argv[1], "-l") == 0 && argc >= 3) {
        r = cpu_libera((unsigned int)atoi(argv[2]));
        if (r < 0) { printf("cpu: %s\n", errore(r)); return 1; }
        printf("processore %s di nuovo di tutti\n", argv[2]);
        return 0;
    }
    if (strcmp(argv[1], "-x") == 0 && argc >= 4) {
        int pid, stato = 0;

        if (lista(argv[2], set) != 0) { uso(); return 1; }
        /* this thread first: the child inherits its set */
        r = cpu_aff_scrivi(0, set, g_parole);
        if (r < 0) { printf("cpu: %s\n", errore(r)); return 1; }
        pid = spawn(argv[3], argv + 3);
        if (pid < 0) { printf("cpu: non riesco a lanciare %s\n", argv[3]); return 1; }
        waitpid(pid, &stato, 0);
        return stato;
    }
    if (strcmp(argv[1], "-R") == 0 && argc >= 4) {
        int pid, stato = 0;

        pid = spawn(argv[3], argv + 3);
        if (pid < 0) { printf("cpu: non riesco a lanciare %s\n", argv[3]); return 1; }
        /* ! AFTER THE START, NOT BEFORE: the reservation names a process,
         * and it exists only now. Its first instants may run elsewhere; from
         * here on it runs only on N, and nobody else does. */
        r = cpu_riserva(pid, (unsigned int)atoi(argv[2]));
        if (r < 0) printf("cpu: processore %s non riservato: %s (il comando gira lo stesso)\n",
                          argv[2], errore(r));
        waitpid(pid, &stato, 0);
        return stato;
    }
    uso();
    return 1;
}

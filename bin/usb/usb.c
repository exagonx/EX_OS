/* =============================================================================
 * bin/usb/usb.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * =============================================================================
 *
 * usb — le porte USB, cosa c'e' attaccato, togliere e riprendere una
 * chiavetta (23 ottobre 2026, l'utente)
 *
 *     usb -status            le porte di ogni controller e cosa c'e' sopra,
 *                            le chiavette e dove sono montate (anche
 *                            -showstatus, o senza niente)
 *     usb -unplug [chi]      smonta e ritira la chiavetta: poi la si stacca.
 *                            chi = usb0, o /USB/USB0; senza, l'unica che c'e'
 *     usb -refresh           riguarda le porte occupate: una chiavetta tolta
 *                            con -unplug e lasciata infilata torna, senza
 *                            staccarla e riattaccarla
 *
 * ! LO STATO DELLE PORTE LO SANNO I DRIVER, e si chiede a loro: ognuno apre
 * uno sportello «usb.<nome>» (drivers/usb/usb_sportello.h). Qui non si tocca
 * nessun registro, e nessun driver si avvia: se non ce n'e', lo si dice.
 *
 * ! -unplug E' `eject` CON LA FRASE FINALE, e la sequenza e' la stessa per la
 * stessa ragione: prima si smonta tutto quel che sta sul supporto, poi lo si
 * ritira (blk_espelli), e il driver torna a guardare le porte. Al posto del
 * solo «occupato» dice CHI lo tiene (ps -m, passo 4 del sistema di base).
 *
 * Sta nel sistema di base (/rdboot/bin): una chiavetta si toglie anche col
 * disco di sistema smontato.
 * ============================================================================= */
#include "libc.h"
#include "../../drivers/usb/usb_sportello.h"

/* +0.001 a ogni modifica: `usb -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("usb", "0.001");

#define E_BUSY      16
#define MAX_PUNTI   8
#define MAX_SPORT   12

/* --- gli sportelli dei driver ------------------------------------------------ */

static const char *const TIPI[] = { "uhci", "ohci", "ehci", "xhci" };

typedef struct {
    char nome[IPC_NAME_LEN];
    int  pid;
} Sportello;

static int trova_sportelli(Sportello *s, int max)
{
    unsigned int t;
    int k, n = 0;

    for (t = 0; t < sizeof(TIPI) / sizeof(TIPI[0]); t++)
        for (k = 1; k <= 4 && n < max; k++) {
            int pid;

            if (k == 1) snprintf(s[n].nome, sizeof(s[n].nome), "%s%s", USB_SPORTELLO_PREFISSO, TIPI[t]);
            else        snprintf(s[n].nome, sizeof(s[n].nome), "%s%s%d", USB_SPORTELLO_PREFISSO, TIPI[t], k);
            pid = ipc_lookup(s[n].nome);
            if (pid <= 0) break;
            s[n].pid = pid;
            n++;
        }
    return n;
}

/* Manda `tipo` e aspetta la risposta `atteso` di quel pid. Rende i byte. */
static int chiedi(int pid, unsigned int tipo, unsigned int atteso, void *buf, unsigned int max)
{
    IpcMessage   meta;
    unsigned int giro;

    if (ipc_send((unsigned int)pid, tipo, NULL, 0) < 0) return -1;
    for (giro = 0; giro < 20; giro++) {
        if (ipc_recv_timeout(&meta, buf, max, 100) < 0) continue;
        if (meta.sender_pid == (unsigned int)pid && meta.tipo == atteso)
            return (int)meta.len;
    }
    return -1;              /* due secondi: il driver e' fermo altrove */
}

/* --- dispositivi a blocchi e montaggi ---------------------------------------- */

static int e_del_supporto(const char *dev, const char *base)
{
    unsigned int l = (unsigned int)strlen(base);

    if (strcmp(dev, base) == 0) return 1;
    return strncmp(dev, base, l) == 0 && dev[l] == 'p';
}

/* I punti su cui e' montato `base` (o una sua partizione), separati da ", ". */
static void punti_di(const char *base, char *out, unsigned int max)
{
    MountInfo    m[4];
    unsigned int start = 0;
    int          n, i;

    out[0] = '\0';
    while ((n = mountinfo(m, 4, start)) > 0) {
        for (i = 0; i < n; i++) {
            if (!e_del_supporto(m[i].dev, base)) continue;
            if (out[0]) strncat(out, ", ", max - strlen(out) - 1);
            strncat(out, m[i].punto, max - strlen(out) - 1);
        }
        start += (unsigned int)n;
        if (n < 4) break;
    }
}

/* Le chiavette servite (dispositivi a blocchi di tipo 5, offerti da un
 * processo) che si chiamano usb<n>. Rende quante, e i nomi in `nomi`. */
static int chiavette(char nomi[][16], int max)
{
    BlkInfo      b[8];
    unsigned int start = 0;
    int          n, i, k = 0;

    while ((n = blkinfo(b, 8, start)) > 0) {
        for (i = 0; i < n; i++)
            if (b[i].tipo == 5 && strncmp(b[i].nome, "usb", 3) == 0 && k < max) {
                strncpy(nomi[k], b[i].nome, 15);
                nomi[k][15] = '\0';
                k++;
            }
        start += (unsigned int)n;
        if (n < 8) break;
    }
    return k;
}

/* =============================================================================
 * usb -status
 * ============================================================================= */
static const char *come(unsigned int s)
{
    switch (s) {
        case USB_P_COLLEGATA:  return "collegato, non ancora guardato";
        case USB_P_ERRORE:     return "non risponde (staccalo e riattaccalo)";
        case USB_P_MASSA:      return "memoria di massa";
        case USB_P_ESPULSA:    return "tolta con -unplug, ancora infilata";
        case USB_P_NON_PRONTA: return "memoria di massa che non diventa pronta "
                                      "(lettore vuoto?)";
        case USB_P_MOUSE:      return "mouse";
        case USB_P_TASTIERA:   return "tastiera";
        case USB_P_ALTRO:      return "dispositivo che nessun driver usa";
        default:               return "vuota";
    }
}

static int stato(void)
{
    Sportello s[MAX_SPORT];
    UsbStato  st;
    char      nomi[8][16], punti[96];
    int       n = trova_sportelli(s, MAX_SPORT), i, k, nc;

    if (n == 0) {
        printf("Nessun driver USB attivo: nessuno sportello usb.* registrato.\n");
        printf("(all'avvio li lancia /boot/avvio.sh: ehci.drv -avvio, ...)\n");
    }

    for (i = 0; i < n; i++) {
        unsigned int j;

        if (chiedi(s[i].pid, USB_MSG_STATO, USB_MSG_STATO_R, &st, sizeof(st)) < (int)sizeof(st)) {
            printf("%s (pid %d): non risponde\n", s[i].nome + 4, s[i].pid);
            continue;
        }
        printf("%s: %u controller, %u porte\n", st.chi, st.controller, st.porte);
        for (j = 0; j < st.n && j < USB_SPORTELLO_PORTE; j++) {
            UsbPortaStato *p = &st.p[j];

            if (st.controller > 1) printf("  controller %u ", p->ctl + 1);
            else                   printf("  ");
            printf("porta %u: %s", p->porta, come(p->stato));
            if (p->venditore || p->prodotto)
                printf("  [%04x:%04x]", p->venditore, p->prodotto);
            if (p->nome[0]) printf("  %s", p->nome);
            if (p->blk[0]) {
                punti_di(p->blk, punti, sizeof(punti));
                printf("\n      -> %s, %s", p->blk, punti[0] ? punti : "non montata");
            }
            printf("\n");
        }
        printf("  %s\n", st.n ? "le altre porte sono vuote" : "tutte le porte sono vuote");
    }

    nc = chiavette(nomi, 8);
    if (nc) {
        printf("\nChiavette da togliere con  usb -unplug <nome>:\n");
        for (k = 0; k < nc; k++) {
            punti_di(nomi[k], punti, sizeof(punti));
            printf("  %-6s %s\n", nomi[k], punti[0] ? punti : "(non montata)");
        }
    }
    return 0;
}

/* =============================================================================
 * usb -unplug
 * ============================================================================= */
static void chi_tiene(const char *punto)
{
    ProcUso      v[PROCINFO_MAX_BATCH];
    unsigned int start = 0, io = (unsigned int)getpid();
    int          n, i;

    while ((n = proc_uso(v, PROCINFO_MAX_BATCH, start, punto)) > 0) {
        for (i = 0; i < n; i++) {
            if (v[i].pid == io) continue;
            printf("      pid %-5u %-14s", v[i].pid, v[i].name);
            if (v[i].perche & PROCUSO_PROGRAMMA) printf("il programma ");
            if (v[i].perche & PROCUSO_FILE)      printf("file aperti ");
            if (v[i].perche & PROCUSO_CARTELLA)  printf("la directory corrente");
            printf("\n");
        }
        start += (unsigned int)n;
    }
}

static int smonta_sopra(const char *base)
{
    char         punti[MAX_PUNTI][MOUNTINFO_PUNTO_MAX];
    MountInfo    m[4];
    unsigned int start = 0;
    int          n, i, np = 0, guai = 0;

    while ((n = mountinfo(m, 4, start)) > 0) {
        for (i = 0; i < n && np < MAX_PUNTI; i++) {
            if (!e_del_supporto(m[i].dev, base)) continue;
            strncpy(punti[np], m[i].punto, MOUNTINFO_PUNTO_MAX - 1);
            punti[np][MOUNTINFO_PUNTO_MAX - 1] = '\0';
            np++;
        }
        start += (unsigned int)n;
        if (n < 4) break;
    }

    for (i = 0; i < np; i++) {
        int rc = umount(punti[i]);

        if (rc == 0) { printf("  smontato %s\n", punti[i]); continue; }
        printf("  %s non si smonta (%d)\n", punti[i], rc);
        if (rc == -E_BUSY) {
            printf("    lo usano:\n");
            chi_tiene(punti[i]);
            printf("    chiudili (kill <pid>) o esci dalla directory, e riprova\n");
        }
        guai = 1;
    }
    return guai ? -1 : 0;
}

static int unplug(const char *chi)
{
    char nome[16], nomi[8][16];
    int  rc;

    if (chi == NULL) {
        int nc = chiavette(nomi, 8), k;

        if (nc == 0) {
            printf("Non c'e' nessuna chiavetta da togliere.\n");
            return 1;
        }
        if (nc > 1) {
            printf("Ce n'e' piu' di una: quale?\n");
            for (k = 0; k < nc; k++) printf("  usb -unplug %s\n", nomi[k]);
            return 1;
        }
        strncpy(nome, nomi[0], sizeof(nome));
    } else if (chi[0] == '/') {
        /* Un punto di montaggio: il dispositivo e' quello montato li', senza
         * il «pN» della partizione. */
        MountInfo    m[4];
        unsigned int start = 0;
        int          n, i;

        nome[0] = '\0';
        while (!nome[0] && (n = mountinfo(m, 4, start)) > 0) {
            for (i = 0; i < n; i++)
                if (strcmp(m[i].punto, chi) == 0) {
                    char *p;

                    strncpy(nome, m[i].dev, sizeof(nome) - 1);
                    nome[sizeof(nome) - 1] = '\0';
                    p = strrchr(nome, 'p');
                    if (p && p > nome && p[1] >= '0' && p[1] <= '9') *p = '\0';
                    break;
                }
            start += (unsigned int)n;
            if (n < 4) break;
        }
        if (!nome[0]) { printf("usb: su %s non e' montato niente\n", chi); return 1; }
    } else {
        strncpy(nome, chi, sizeof(nome) - 1);
        nome[sizeof(nome) - 1] = '\0';
    }

    printf("Tolgo %s:\n", nome);
    if (smonta_sopra(nome) != 0) {
        printf("NON staccarla: qualcosa e' rimasto montato.\n");
        return 1;
    }

    rc = blk_espelli(nome);
    if (rc != 0) {
        if (rc == -1 || rc == -E_BUSY) printf("usb: %s e' ancora in uso (%d)\n", nome, rc);
        else printf("usb: %s non si ritira (%d): e' davvero una chiavetta?\n", nome, rc);
        return 1;
    }

    printf("\n*** Puoi scollegare %s. ***\n", nome);
    printf("(se la lasci infilata, `usb -refresh` la riprende)\n");
    return 0;
}

/* =============================================================================
 * usb -refresh
 * ============================================================================= */
static int refresh(void)
{
    Sportello    s[MAX_SPORT];
    char         prima[8][16], dopo[8][16], punti[96];
    unsigned int quante = 0, q;
    int          n = trova_sportelli(s, MAX_SPORT), i, k, j, np, nd, attesa;

    if (n == 0) {
        printf("Nessun driver USB attivo: non c'e' niente da riguardare.\n");
        return 1;
    }
    np = chiavette(prima, 8);

    for (i = 0; i < n; i++) {
        if (chiedi(s[i].pid, USB_MSG_RIPROVA, USB_MSG_RIPROVA_R, &q, sizeof(q)) < (int)sizeof(q)) {
            printf("%s: non risponde\n", s[i].nome + 4);
            continue;
        }
        quante += q;
    }
    if (quante == 0) {
        printf("Nessuna porta da riguardare: le chiavette infilate sono gia'\n");
        printf("servite (usb -status).\n");
        return 0;
    }
    printf("Riguardo %u port%s...\n", quante, quante == 1 ? "a" : "e");

    /* Il driver la riprende al prossimo giro (un secondo), poi la chiavetta
     * deve svegliarsi e automount montarla: si aspetta fino a dieci secondi
     * che compaia qualcosa di nuovo, e un paio ancora per il montaggio. */
    for (attesa = 0; attesa < 10; attesa++) {
        sleep(1);
        nd = chiavette(dopo, 8);
        if (nd > np) break;
    }
    if (attesa == 10) {
        printf("Nessuna chiavetta nuova. `usb -status` dice cosa c'e' sulle porte.\n");
        return 1;
    }
    sleep(2);
    nd = chiavette(dopo, 8);
    for (k = 0; k < nd; k++) {
        int vecchia = 0;

        for (j = 0; j < np; j++) if (strcmp(dopo[k], prima[j]) == 0) vecchia = 1;
        if (vecchia) continue;
        punti_di(dopo[k], punti, sizeof(punti));
        printf("  %s di nuovo presente: %s\n", dopo[k],
               punti[0] ? punti : "non ancora montata (automount gira?)");
    }
    return 0;
}

static void uso(void)
{
    printf("uso: usb -status            porte USB e cosa c'e' attaccato\n");
    printf("     usb -unplug [usb0]     smonta e ritira la chiavetta, poi\n");
    printf("                            la si puo' staccare\n");
    printf("     usb -refresh           riprende le chiavette tolte con\n");
    printf("                            -unplug e lasciate infilate\n");
}

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "-status") == 0 || strcmp(argv[1], "-showstatus") == 0)
        return stato();
    if (strcmp(argv[1], "-unplug") == 0)  return unplug(argc > 2 ? argv[2] : NULL);
    if (strcmp(argv[1], "-refresh") == 0) return refresh();
    uso();
    return strcmp(argv[1], "-h") == 0 ? 0 : 1;
}

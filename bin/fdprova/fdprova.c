/* =============================================================================
 * bin/fdprova/fdprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LA PROVA DEL FLOPPY, PASSO PASSO — si guarda E si ascolta
 *
 *     fdprova        esegue la prova e dice quanti passi hanno dato problemi
 *
 * Serve a rispondere a una domanda sola: la MECCANICA fa quello che il
 * software le dice? Ogni passo annuncia cosa deve succedere fisicamente —
 * la spia che si accende, la testina che si sposta, il motore che si ferma —
 * e poi dice cosa ha risposto il controller. Se il rumore non corrisponde
 * all'annuncio, il guasto e' fra il controller e il drive; se corrisponde ma
 * le letture sbagliano lo stesso, e' il dischetto.
 *
 * -----------------------------------------------------------------------------
 * ! I PASSI LI STAMPA IL KERNEL, NON QUESTO PROGRAMMA, e non e' pigrizia
 *
 * La prova vera la esegue il kernel (SYS_FDPROVA), perche' il controller e'
 * suo: l'IRQ6 ha un gestore kernel-space, e un programma in ring3 non lo
 * riceverebbe mai — cioe' non potrebbe dire niente sul guasto piu' comune,
 * che e' proprio l'interrupt che non arriva. E proverebbe un codice diverso
 * da quello che legge i file davvero.
 *
 * Le righe escono quindi da kprintf, sulla console e sulla seriale, MENTRE la
 * prova va avanti. Raccoglierle qui e stamparle alla fine sarebbe peggio:
 * meta' della prova la fa l'orecchio di chi sta davanti alla macchina, e
 * «adesso la testina si sposta a cilindro 40» va letto PRIMA che il rumore
 * arrivi, non dopo dentro un riassunto.
 *
 * -----------------------------------------------------------------------------
 * ! LA PROVA DISTURBA IL DISCO DA CUI STAI GIRANDO, e va detto prima
 *
 * Azzera il controller, muove la testina e ferma il motore: sulla stessa
 * unita' da cui il sistema legge i propri file. Non rompe niente — alla fine
 * la posizione della testina viene dichiarata ignota e il primo accesso
 * successivo fa un SEEK in piu' — ma un programma che stesse scrivendo in
 * quel momento vedrebbe una lentezza improvvisa. Si da' a macchina ferma.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `fdprova -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("fdprova", "0.001");

/* =============================================================================
 * Da FdPasso a una riga leggibile
 *
 * ! IL TESTO STA QUI E NON NEL KERNEL. Il kernel rende misure; le frasi le
 * compone chi le deve mostrare. Le stesse misure finiscono in un file di log,
 * a schermo in forma ridotta quando il file non si scrive, e domani magari
 * altrove: un kernel che rendesse righe gia' impaginate costringerebbe
 * chiunque a disfarle.
 * ========================================================================== */
static void riga_di(const FdPasso *p, char *out, unsigned int max)
{
    const char *segno = (p->esito == 0) ? "ok" : "NO";

    switch (p->codice) {
    case FD_MSR:
        snprintf(out, max, "%s passo %u  controller: MSR=0x%02x (atteso 0x80)",
                 segno, p->passo, p->a);
        break;
    case FD_RESET_IRQ:
        snprintf(out, max, "%s passo %u  reset: IRQ6 %s",
                 segno, p->passo,
                 p->esito == 0 ? "ricevuto" : "NON ricevuto");
        break;
    case FD_SENSE:
        snprintf(out, max, "%s passo %u  unita' %u: ST0=0x%02x",
                 segno, p->passo, p->a, p->b);
        break;
    case FD_MOTORE_ON:
        snprintf(out, max, "%s passo %u  motore acceso (la spia deve accendersi)",
                 segno, p->passo);
        break;
    case FD_RECAL:
        snprintf(out, max, "%s passo %u  ricalibratura: ST0=0x%02x PCN=%u",
                 segno, p->passo, p->a, p->b);
        break;
    case FD_SEEK:
        snprintf(out, max, "%s passo %u  testina a %u: il controller dice %u",
                 segno, p->passo, p->a, p->b);
        break;
    case FD_LETTURA:
        snprintf(out, max, "%s passo %u  settore %u: %s",
                 segno, p->passo, p->a,
                 p->esito == 0 ? "letto, firma 0x55AA presente"
                               : "non letto o firma assente");
        break;
    case FD_RIPETUTA:
        snprintf(out, max, "%s passo %u  LBA %u: %u letture su 10 fallite",
                 segno, p->passo, p->a, p->b);
        break;
    case FD_RIACCESO:
        snprintf(out, max, "%s passo %u  fermo e poi riletto: %s",
                 segno, p->passo,
                 (p->esito == 0) ? "il motore riparte e i dati tornano"
                                 : "dopo la fermata la lettura non torna");
        break;
    case FD_MOTORE_OFF:
        snprintf(out, max, "%s passo %u  motore spento (la spia deve spegnersi)",
                 segno, p->passo);
        break;
    default:
        snprintf(out, max, "%s passo %u  codice %u (a=%u b=%u)",
                 segno, p->passo, p->codice, p->a, p->b);
        break;
    }
}

/* =============================================================================
 * IL LOG, E COSA FARE QUANDO NON SI PUO' SCRIVERE
 *
 * ! IL CASO IN CUI IL LOG NON SI SCRIVE E' IL CASO NORMALE DI QUESTO
 * PROGRAMMA, non l'eccezione. Si prova il drive floppy perche' il drive
 * floppy da' errori; il disco su cui scrivere il log e' quello stesso drive.
 * Un programma che si limitasse a dire «non riesco a salvare» avrebbe buttato
 * via la diagnosi proprio nel caso per cui e' stato scritto.
 *
 * Allora si stampano le righe a schermo. VENTI, non tutte: uno schermo ne
 * tiene venticinque e le prime scorrerebbero via, che e' come non averle. Le
 * venti si scelgono cosi': prima TUTTE quelle con un problema, poi le altre
 * finche' c'e' posto — ma si STAMPANO in ordine di esecuzione, perche' in una
 * diagnostica l'ordine e' l'informazione: il primo NO spiega quasi sempre
 * tutti quelli dopo.
 * ========================================================================== */
#define RIGHE_A_SCHERMO 20

static void mostra_ridotto(const FdPasso *p, int n)
{
    static unsigned char scelto[64];
    char  riga[96];
    int   i, quante = 0;

    for (i = 0; i < n && i < 64; i++) scelto[i] = 0;

    /* Prima i problemi: se sono piu' di venti, il resto non serve comunque. */
    for (i = 0; i < n && i < 64 && quante < RIGHE_A_SCHERMO; i++)
        if (p[i].esito != 0) { scelto[i] = 1; quante++; }

    /* Poi il contorno, finche' c'e' posto. */
    for (i = 0; i < n && i < 64 && quante < RIGHE_A_SCHERMO; i++)
        if (!scelto[i]) { scelto[i] = 1; quante++; }

    printf("\n--- i passi che contano, in ordine di esecuzione ---\n");
    for (i = 0; i < n && i < 64; i++) {
        if (!scelto[i]) continue;
        riga_di(&p[i], riga, sizeof(riga));
        printf("  %s\n", riga);
    }
    if (n > quante)
        printf("  (%d passi non mostrati per non riempire lo schermo)\n",
               n - quante);
}

static int salva(const char *percorso, const FdPasso *p, int n)
{
    FILE *f = fopen(percorso, "w");
    char  riga[96];
    int   i;

    if (!f) return -1;

    fprintf(f, "Prova del drive floppy di EX-OS\n");
    fprintf(f, "%d passi registrati\n\n", n);

    for (i = 0; i < n; i++) {
        riga_di(&p[i], riga, sizeof(riga));
        fprintf(f, "%s\n", riga);
    }

    /* ! SI CONTROLLA LA CHIUSURA, non solo la scrittura. Su un supporto che
     * sbaglia, fwrite puo' riuscire contro la cache e fallire al riversamento:
     * un log dato per salvato e mai arrivato sul disco e' peggio di nessun
     * log, perche' nessuno lo va a cercare a schermo. */
    if (fclose(f) != 0) return -1;
    return 0;
}

static void uso(void)
{
    printf("uso: fdprova [-s] [-o <file>]\n\n");
    printf("  Prova il drive floppy passo passo: reset del controller,\n");
    printf("  motore, ricalibratura, spostamenti della testina, letture.\n");
    printf("  Ogni passo dice cosa deve succedere FISICAMENTE prima di\n");
    printf("  farlo: si guarda la spia e si ascolta il drive.\n\n");
    printf("  -s          salta l'avviso e parte subito\n");
    printf("  -o <file>   dove scrivere il log (predefinito /fdprova.log)\n\n");
    printf("  Se il log non si puo' scrivere - ed e' il caso normale, visto\n");
    printf("  che si sta provando proprio quel disco - le righe che contano\n");
    printf("  finiscono a schermo.\n\n");
    printf("  Va data da root e a macchina ferma: muove la testina e\n");
    printf("  azzera il controller del disco da cui stai girando.\n");
}

int main(int argc, char **argv)
{
    static FdPasso passi[48];
    const char *log = "/fdprova.log";
    int n, i, guasti = 0, subito = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) { subito = 1; continue; }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { log = argv[++i]; continue; }
        uso();
        return (strcmp(argv[i], "-h") == 0) ? 0 : 1;
    }

    if (!subito) {
        printf("Prova del drive floppy.\n\n");
        printf("Sta per muovere la testina e fermare il motore dell'unita' da\n");
        printf("cui questo sistema sta girando. Dura una decina di secondi.\n\n");
        printf("GUARDA la spia del drive e ASCOLTA la testina: ogni passo\n");
        printf("dice cosa deve succedere prima di farlo.\n\n");
    }

    n = fdprova(passi, (unsigned int)(sizeof(passi) / sizeof(passi[0])));

    if (n < 0) {
        /* ! L'UNICO ERRORE POSSIBILE E' -EPERM, e si spiega invece di
         * stampare un numero: chi legge «-1» va a cercare un guasto del
         * drive, che non c'e'. */
        printf("\nfdprova: serve essere root (esito %d).\n", n);
        printf("         Con `sudo fdprova`, oppure entrando come root.\n");
        return 1;
    }

    for (i = 0; i < n; i++) if (passi[i].esito != 0) guasti++;

    printf("\n");
    if (salva(log, passi, n) == 0) {
        printf("fdprova: %d passi, %d con problemi. Log in %s\n", n, guasti, log);
    } else {
        printf("fdprova: il log non si scrive in %s.\n", log);
        printf("         E' quasi sempre cosi': il disco che si sta provando\n");
        printf("         e' quello su cui bisognerebbe scrivere.\n");
        mostra_ridotto(passi, n);
    }

    printf("\n");
    if (guasti == 0) {
        printf("Il controller e la meccanica si capiscono. Se la lettura dei\n");
        printf("file sbaglia lo stesso, il sospetto e' il singolo dischetto:\n");
        printf("riformattarlo o cambiarlo.\n");
        return 0;
    }

    printf("Guardare il PRIMO passo marcato NO: quelli dopo sbagliano\n");
    printf("quasi sempre di conseguenza.\n");
    return 1;
}

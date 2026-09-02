/* =============================================================================
 * bin/kbprova/kbprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LA PROVA DELLA TASTIERA — dove si ferma, fra il tasto e il programma
 *
 *     kbprova            con l'avviso, poi la prova
 *     kbprova -s         parte subito
 *     kbprova -o <file>  dove scrivere il log
 *
 * Fra un tasto premuto e una lettera sullo schermo ci sono sei anelli, e
 * quando non compare niente sono tutti sospettati insieme. La prova taglia la
 * catena in due: legge il controller 8042 A TAPPETO e insieme conta gli IRQ1.
 * Da li' la risposta e' una di tre, e sono tre guasti con tre cure diverse —
 * il dettaglio sta in testa a kernel/arch/x86/kbdprova.c.
 *
 * ! SI ESEGUE DA dist/diagnostic.img, e non e' una preferenza: quell'immagine
 * non carica /dev/kbd.drv, e questa prova ha bisogno che nessuno stia leggendo
 * il buffer dell'8042. Due lettori si rubano i byte a vicenda, e la prova
 * misurerebbe male proprio la cosa che deve misurare. Sul floppy normale la
 * prova se ne accorge da sola e si ferma dicendolo.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `kbprova -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("kbprova", "0.001");

static void riga_di(const FdPasso *p, char *out, unsigned int max)
{
    const char *segno = (p->esito == 0) ? "ok" : "NO";

    switch (p->codice) {
    case KB_OCCUPATO:
        snprintf(out, max, "NO passo 0  l'IRQ1 e' gia' di un driver: prova non fatta");
        break;
    case KB_STATO:
        snprintf(out, max, "%s passo %u  8042: stato=0x%02x (0xFF = assente)",
                 segno, p->passo, p->a);
        break;
    case KB_SELFTEST:
        snprintf(out, max, "%s passo %u  autodiagnosi 8042: 0x%02x (atteso 0x55)",
                 segno, p->passo, p->a);
        break;
    case KB_PORTA:
        snprintf(out, max, "%s passo %u  prova della porta: 0x%02x (atteso 0x00)",
                 segno, p->passo, p->a);
        break;
    case KB_CONFIG:
        snprintf(out, max, "%s passo %u  configurazione=0x%02x (bit0 IRQ1, bit4 clock)",
                 segno, p->passo, p->a);
        break;
    case KB_RESET:
        snprintf(out, max, "%s passo %u  reset tastiera: ACK=0x%02x BAT=0x%02x",
                 segno, p->passo, p->a, p->b);
        break;
    case KB_SCANSIONE:
        snprintf(out, max, "%s passo %u  scansione accesa: 0x%02x (atteso 0xFA)",
                 segno, p->passo, p->a);
        break;
    case KB_PIC:
        snprintf(out, max, "%s passo %u  maschera PIC=0x%02x (bit1 = IRQ1 chiusa)",
                 segno, p->passo, p->a);
        break;
    case KB_SORGENTE:
        snprintf(out, max, "%s passo %u  strada=%s, servizio 'kbd' PID %u",
                 segno, p->passo,
                 (p->a == 2) ? "ring3 (IPC)" :
                 (p->a == 1) ? "ripiego interno" : "nessuna",
                 p->b);
        break;
    case KB_TASTI:
        snprintf(out, max, "%s passo %u  tasti: %u codici, %u interrupt",
                 segno, p->passo, p->a, p->b);
        break;
    default:
        snprintf(out, max, "%s passo %u  codice %u (a=%u b=%u)",
                 segno, p->passo, p->codice, p->a, p->b);
        break;
    }
}

/* Le venti righe che contano quando il log non si scrive: prima i problemi,
 * poi il contorno, ma stampate in ordine di esecuzione. Il perche' sta in
 * bin/fdprova/fdprova.c, che fa la stessa cosa per il floppy. */
#define RIGHE_A_SCHERMO 20

static void mostra_ridotto(const FdPasso *p, int n)
{
    static unsigned char scelto[64];
    char riga[96];
    int  i, quante = 0;

    for (i = 0; i < n && i < 64; i++) scelto[i] = 0;
    for (i = 0; i < n && i < 64 && quante < RIGHE_A_SCHERMO; i++)
        if (p[i].esito != 0) { scelto[i] = 1; quante++; }
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

    fprintf(f, "Prova della tastiera di EX-OS\n");
    fprintf(f, "%d passi registrati\n\n", n);
    for (i = 0; i < n; i++) {
        riga_di(&p[i], riga, sizeof(riga));
        fprintf(f, "%s\n", riga);
    }
    if (fclose(f) != 0) return -1;
    return 0;
}

static void uso(void)
{
    printf("uso: kbprova [-s] [-o <file>]\n\n");
    printf("  Prova la tastiera passo passo: controller 8042, autodiagnosi,\n");
    printf("  byte di configurazione, reset della tastiera, maschera del\n");
    printf("  PIC, e dieci secondi in cui bisogna PREMERE DEI TASTI.\n\n");
    printf("  Conta due cose diverse: i codici che arrivano nel buffer e gli\n");
    printf("  interrupt che li accompagnano. La differenza fra le due dice\n");
    printf("  dove si ferma la catena.\n\n");
    printf("  -s          parte subito\n");
    printf("  -stato      solo lo stato: quale strada serve la tastiera,\n");
    printf("              se il servizio 'kbd' c'e', se l'IRQ1 e' aperta.\n");
    printf("              ! Questa si puo' dare anche con kbd.drv caricato:\n");
    printf("              guarda e basta, non parla con l'8042.\n");
    printf("  -o <file>   dove scrivere il log (predefinito /kbprova.log)\n\n");
    printf("  Va data da root, e da un sistema che NON ha caricato\n");
    printf("  /dev/kbd.drv: usare dist/diagnostic.img.\n");
}

int main(int argc, char **argv)
{
    static FdPasso passi[48];
    const char *log = "/kbprova.log";

    int n, i, guasti = 0, subito = 0, solo_stato = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) { subito = 1; continue; }
        if (strcmp(argv[i], "-stato") == 0) { solo_stato = 1; subito = 1; continue; }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { log = argv[++i]; continue; }
        uso();
        return (strcmp(argv[i], "-h") == 0) ? 0 : 1;
    }

    if (!subito && !solo_stato) {
        printf("Prova della tastiera.\n\n");
        printf("Dura una quindicina di secondi. Verso la fine chiede di\n");
        printf("PREMERE DEI TASTI: e' quello il momento che conta.\n\n");
    }

    n = solo_stato
        ? kbstato(passi, (unsigned int)(sizeof(passi) / sizeof(passi[0])))
        : kbprova(passi, (unsigned int)(sizeof(passi) / sizeof(passi[0])));

    if (n < 0) {
        printf("\nkbprova: serve essere root (esito %d).\n", n);
        return 1;
    }

    for (i = 0; i < n; i++) if (passi[i].esito != 0) guasti++;

    printf("\n");
    if (salva(log, passi, n) == 0) {
        printf("kbprova: %d passi, %d con problemi. Log in %s\n", n, guasti, log);
    } else {
        printf("kbprova: il log non si scrive in %s.\n", log);
        mostra_ridotto(passi, n);
    }

    return guasti ? 1 : 0;
}

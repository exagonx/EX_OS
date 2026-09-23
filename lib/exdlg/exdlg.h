/* =============================================================================
 * lib/exdlg/exdlg.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ExDlg — i dialoghi di scelta file, in una libreria SEPARATA da ExWin
 *
 *     #include <exdlg.h>
 *
 * ! PERCHE' NON DENTRO exwin.so. Perche' non tutti la vogliono: la barra delle
 * applicazioni, un orologio, un pannello non aprono file mai, e si
 * porterebbero dietro un elenco di directory e una finestra modale per niente.
 * Tenerle separate vuol dire che chi non apre file non paga il dialogo — ed e'
 * il motivo per cui esistono piu' librerie invece di una sola grande.
 *
 * ! E PERCHE' SERVE A DUE PROGRAMMI. L'editor deve poter aprire e salvare con
 * nome; il file manager fa la stessa scelta con lo stesso aspetto. Scriverlo
 * due volte vorrebbe dire due dialoghi che si somigliano finche' qualcuno non
 * corregge uno solo dei due.
 *
 * ! ExDlg USA ExWin, e la usa nello stesso modo di un'applicazione: attraverso
 * i nomi risolti a caricamento. Non se ne porta dentro una copia — due copie
 * del toolkit nello stesso processo vorrebbero dire due tabelle di finestre, e
 * una finestra creata dall'una sarebbe invisibile all'altra.
 * ============================================================================= */

#ifndef EXDLG_H
#define EXDLG_H

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------------
 * ! IL DIALOGO E' BLOCCANTE, e la finestra sotto resta viva. Dentro c'e' un
 * ciclo dei messaggi suo, che continua a smistare gli eventi alle altre
 * finestre dell'applicazione: senza, aprire un dialogo lascerebbe il resto del
 * programma nero finche' non lo si chiude.
 *
 * `percorso` riceve il risultato e puo' arrivare gia' pieno: e' cio' che viene
 * proposto. Rende 1 se l'utente ha confermato, 0 se ha annullato.
 * --------------------------------------------------------------------------- */
int ex_dlg_apri(char *percorso, unsigned int max);
int ex_dlg_salva(char *percorso, unsigned int max);

/* The same dialog as ex_dlg_salva with its three words chosen from outside:
 * the window title, the label beside the box and the caption of the confirming
 * button. Passing 0 for any of them gives the "Salva con nome" wording.
 *
 * ! IT EXISTS BECAUSE THIS DIALOG DOES NOT ONLY PICK FILES. Creating a project
 * means choosing a directory and giving it a name: the same gesture - browse,
 * enter, make a folder, type a name - but a window titled "Salva con nome"
 * holding a "Salva" button describes a different action. The words of a dialog
 * are known to the caller; that is already true of the two buttons of
 * ex_dlg_conferma.
 *
 * ! AND THE "NUOVA CARTELLA" BUTTON IS IN BOTH, because it belongs to the save
 * dialog and not to whoever opens it: someone choosing where to put a thing
 * finds out right there that the place does not exist yet. It answers to the
 * mouse or to Ctrl+N, which is the only way in without one. */
int ex_dlg_percorso(const char *titolo, const char *etichetta, const char *ok,
                    char *percorso, unsigned int max);

/* Un avviso con un solo pulsante. Rende sempre 1, ed esiste perche' finora
 * l'unico modo che un'applicazione grafica aveva di dire qualcosa era la riga
 * di stato — che chi guarda un'altra finestra non legge. */
int ex_dlg_avviso(const char *titolo, const char *testo);

/* Una domanda con due pulsanti: rende 1 per «si'», 0 per «no».
 *
 * ! LA RISPOSTA PREDEFINITA E' «NO», E NON E' UNA CORTESIA. Chiudere la
 * finestra col pulsante di chiusura, premere Esc, o veder morire il dialogo
 * per qualunque ragione danno tutti 0. Un dialogo che in caso di dubbio
 * rispondesse «si'» sarebbe un dialogo che cancella il lavoro di qualcuno
 * quando qualcosa va storto — e queste domande si fanno proprio prima di
 * perdere qualcosa.
 *
 * `si` e `no` sono le scritte dei due pulsanti; passare 0 mette «Si'» e «No».
 * Servono perche' «Salva / Non salvare» dice all'utente cosa succede, mentre
 * «Si' / No» lo costringe a ricostruirlo dalla domanda. */
/* -----------------------------------------------------------------------------
 * Una riga da scrivere
 *
 * ! NON E' ex_dlg_salva CON UN'ALTRA ETICHETTA. Quello mostra le directory,
 * perche' chi salva sceglie DOVE; qui si chiede una parola — un pezzo di nome
 * da cercare, un'etichetta — e un elenco di file accanto distrarrebbe.
 *
 * `valore` entra col testo proposto ed esce con quello battuto. Rende 0 se si
 * e' annullato, e allora NON e' stato toccato.
 * --------------------------------------------------------------------------- */
int ex_dlg_riga(const char *titolo, const char *domanda,
                char *valore, unsigned int max);

/* The same, with the caption of the confirming button chosen by the caller
 * ("Crea", "Rinomina", "Cerca"): passing 0 puts "Va bene" there. Whoever knows
 * which action is about to happen is whoever opens the dialog. */
int ex_dlg_chiedi(const char *titolo, const char *domanda, const char *ok,
                  char *valore, unsigned int max);

int ex_dlg_conferma(const char *titolo, const char *testo,
                    const char *si, const char *no);

/* =============================================================================
 * DOWNLOADS, AND THE WINDOW THAT SHOWS THEM (23 September 2026)
 *
 * Asked for EXBrowser, and put here so that any program made with exide has
 * it: a download goes on in its own process (/bin/scarica -avanza) while the
 * program stays free, and a «Download» window shows every one with its
 * percentage, the size, the average speed and the time left.
 *
 *   ex_scarico_avvia(url, file)   starts it, and OPENS THE WINDOW by itself;
 *                                  returns an id >= 0, or an EX_SC_ERR_*
 *   ex_scarichi_finestra()        opens it again (a menu entry «Download»).
 *                                  Closing it stops nothing
 *   ex_scarico_info(id, &s)       where it has got to
 *   ex_scarico_ferma(id)          stops it and removes the half file
 *   ex_scarichi_alla_fine(fn, d)  fn(d, id, stato) when one finishes:
 *                                  to open the file, to say it is there
 *   ex_scarichi_in_corso()        how many are running
 *   ex_scarichi_ferma_tutti()     ! CALL IT BEFORE EXITING: the downloads are
 *                                  child processes and would go on alone
 *
 * The progress arrives through the message loop (ex_guarda_fd in exwin), so
 * the program must be in ex_prendi_msg — which every ExWin program is.
 *
 * ! ExScarico CROSSES THE LIBRARY BOUNDARY, SO ITS LAYOUT IS A PROMISE: it is
 * never changed. Something new goes into a new function.
 * ============================================================================= */
#define EX_SC_IN_CORSO   0
#define EX_SC_FATTO      1
#define EX_SC_FALLITO    2
#define EX_SC_FERMATO    3

#define EX_SC_ERR_PROGRAMMA  -1     /* /bin/scarica missing, or no pipe */
#define EX_SC_ERR_USO        -2     /* address or file name too long */
#define EX_SC_ERR_PIENO      -3     /* sixteen running at once */
#define EX_SC_ERR_TOOLKIT    -4     /* exwin.so older than this: no ex_guarda_fd */
#define EX_SC_ERR_VECCHIA    -5     /* exdlg.so older than this */

#define EXSC_URL_MAX   512
#define EXSC_DOVE_MAX  256

typedef struct {
    int           stato;        /* EX_SC_* */
    unsigned long fatti;        /* bytes written so far */
    unsigned long attesi;       /* bytes expected, 0 = the server did not say */
    unsigned int  ms;           /* since it started (until it ended) */
    char          url[EXSC_URL_MAX];
    char          dove[EXSC_DOVE_MAX];
    char          motivo[96];   /* why it failed */
} ExScarico;

typedef void (*ExScaricoFine)(void *dato, int id, int stato);

int  ex_scarico_avvia(const char *url, const char *dove);
int  ex_scarico_info(int id, ExScarico *s);
void ex_scarico_ferma(int id);
int  ex_scarichi_in_corso(void);
void ex_scarichi_ferma_tutti(void);
void ex_scarichi_finestra(void);
void ex_scarichi_alla_fine(ExScaricoFine fn, void *dato);

/* One question, from one to four answers: returns the index of the button
 * pressed, or -1 for Esc and for a closed window. The LAST answer is the
 * prudent one — «Annulla» — and has the focus. (23 September 2026) */
int ex_dlg_scegli(const char *titolo, const char *testo,
                  const char *const *voci, int n);

#ifdef __cplusplus
}
#endif

#endif /* EXDLG_H */

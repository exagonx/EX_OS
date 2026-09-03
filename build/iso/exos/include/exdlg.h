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

int ex_dlg_conferma(const char *titolo, const char *testo,
                    const char *si, const char *no);

#ifdef __cplusplus
}
#endif

#endif /* EXDLG_H */

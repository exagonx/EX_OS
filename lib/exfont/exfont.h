/* =============================================================================
 * lib/exfont/exfont.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ExFont — i font che stanno in un file
 *
 * ! QUESTO NON DISEGNA, LEGGE. Il ciclo che accende i pixel di un glifo sta
 * dove stanno la finestra, il ritaglio e il colore — cose che un lettore di
 * font non deve conoscere. Qui si trasforma un file in metriche e glifi.
 *
 * ! E NON E' UNA LIBRERIA CONDIVISA A PARTE, ED E' UNA CORREZIONE. Il criterio
 * di questo sistema e' che un programma non carichi cio' che non usa, ed e'
 * per quello che eximg sta fuori: un orologio non decodifica un PNG mai. Ma
 * TUTTI i programmi grafici scrivono del testo, e tutti caricano gia' exwin.so
 * — mettere queste centocinquanta righe in un .so a parte farebbe aprire a
 * ognuno una libreria in piu' per averle. Sarebbe il criterio applicato al
 * contrario.
 *
 * Il file resta separato perche' il giorno che arriva qualcosa che COSTA — i
 * contorni da rasterizzare, l'antialiasing, una cache dei glifi — quello si',
 * andra' in una libreria sua, aperta quando serve come eximg. L'interfaccia
 * verso le applicazioni non cambiera': chiedono un font al toolkit.
 *
 * ! IL FONT DI SISTEMA NON PASSA DA UN FILE, E NON DEVE. L'8x16 e' compilato
 * dentro exwin.so e dentro il server a finestre: un server che non riuscisse a
 * scrivere il titolo di una finestra perche' manca un file sarebbe un guasto
 * molto peggiore di un carattere brutto. E' pero' DESCRITTO da questa stessa
 * struttura — larghezza fissa 8, altezza 16 — quindi il ciclo che disegna e'
 * uno solo per il font di sistema e per quelli che si caricano.
 *
 * =============================================================================
 * IL FORMATO «EXFN»
 *
 * ! I GLIFI SONO BITMAP, NON CURVE, e non e' un ripiego da correggere piu'
 * avanti. Il bersaglio dichiarato e' un Pentium 133: rasterizzare contorni con
 * le curve di Bezier a ogni ridisegno vorrebbe dire una cache dei glifi, la
 * gestione del suo esaurimento e l'aritmetica in virgola fissa per gli
 * accostamenti — tre cose che si possono sbagliare — per un risultato che a 16
 * pixel di corpo si distingue appena da una bitmap disegnata a mano.
 *
 * ! IL PASSO E' UGUALE PER TUTTI I GLIFI, LA LARGHEZZA NO. Ogni glifo occupa
 * `altezza * passo` byte, con `passo` calcolato dalla larghezza MASSIMA del
 * font; la larghezza VERA di ognuno sta nella sua tabella e serve a sapere di
 * quanto avanzare. Costa qualche byte sprecato sui glifi stretti e in cambio
 * l'indirizzo di un glifo si CALCOLA invece di cercarlo in una tabella di
 * scostamenti — che sarebbe l'ennesima struttura da validare leggendo un file
 * che puo' venire da chiunque.
 *
 * ! LA CODIFICA E' LATIN-1, come il font 8x16 e come dichiara la libc in
 * <wchar.h>. Un font in un'altra codifica disegnerebbe le lettere accentate
 * sbagliate proprio dove servono.
 *
 *     scostamento  misura  campo
 *      0            4      magia  "EXFN"
 *      4            2      versione (1)
 *      6            2      bandiere: bit 0 = larghezza fissa
 *      8            2      altezza — l'interlinea, in pixel
 *     10            2      base    — dalla cima alla linea di base
 *     12            2      primo   — codice del primo glifo
 *     14            2      quanti  — quanti glifi seguono
 *     16            2      larg_max
 *     18            2      riservato (zero)
 *     20        quanti     larghezze[], un byte per glifo
 *      ...                 riempimento fino a un multiplo di 4
 *      ...  quanti*altezza*passo   i glifi, riga per riga, bit piu' alto a
 *                                  sinistra, passo = (larg_max + 7) / 8
 * ============================================================================= */
#ifndef EXFONT_H
#define EXFONT_H

#ifdef __cplusplus
extern "C" {
#endif

#define EXFONT_MAGIA        "EXFN"
#define EXFONT_VERSIONE     1

#define EXFONT_FISSO        0x0001u     /* tutti i glifi larghi uguale */

/* Un font aperto. E' tutto qui dentro e non c'e' niente da nascondere: chi
 * disegna ha bisogno dei glifi, e una funzione per ogni pixel costerebbe piu'
 * della lettura. I puntatori guardano DENTRO i dati che il chiamante ha
 * passato a exfont_apri(), quindi valgono finche' valgono quelli. */
typedef struct {
    unsigned int         altezza;       /* interlinea */
    unsigned int         base;          /* dalla cima alla linea di base */
    unsigned int         primo;         /* codice del primo glifo */
    unsigned int         quanti;
    unsigned int         larg_max;
    unsigned int         passo;         /* byte per riga di un glifo */
    unsigned int         fisso;         /* 1 = larghezza unica */
    const unsigned char *larghezze;     /* quanti byte */
    const unsigned char *glifi;         /* quanti * altezza * passo byte */
} ExFontDati;

/* Legge l'intestazione e prepara i puntatori. Rende 1 se il file e' un EXFN
 * valido e COMPLETO, 0 altrimenti — e su 0 la struttura resta azzerata.
 *
 * ! CONTROLLA CHE I DATI CI SIANO DAVVERO, non solo che l'intestazione sia
 * plausibile. Un file troncato a meta' ha un'intestazione perfetta: senza il
 * controllo sulla lunghezza, il primo glifo oltre la fine sarebbe una lettura
 * fuori dal buffer — e in un browser quel file lo sceglie chi sta dall'altra
 * parte della rete.
 *
 * ! NON COPIA E NON ALLOCA. I dati restano dove sono; chi chiama li tiene vivi
 * finche' usa il font. E' l'opposto di eximg_carica(), e la ragione e' che li'
 * si produce qualcosa di nuovo — i pixel decodificati — mentre qui si legge e
 * basta. */
int exfont_apri(const unsigned char *dati, unsigned int n, ExFontDati *f);

/* La larghezza di un glifo, cioe' di quanto avanzare dopo averlo disegnato.
 * Per un codice che il font non ha rende la larghezza dello spazio, che e' la
 * cosa meno sbagliata: un buco largo quanto un carattere invece di una
 * sovrapposizione o di un salto. */
unsigned int exfont_larghezza_car(const ExFontDati *f, unsigned char c);

/* La larghezza di una stringa intera, sommando. */
unsigned int exfont_larghezza(const ExFontDati *f, const char *s);

/* I byte del glifo: `altezza` righe da `passo` byte. Rende 0 se il font non ha
 * quel codice, e chi disegna in quel caso non disegna niente. */
const unsigned char *exfont_glifo(const ExFontDati *f, unsigned char c);

#ifdef __cplusplus
}
#endif

#endif /* EXFONT_H */

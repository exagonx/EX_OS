/* =============================================================================
 * lib/exsuono/exsuono.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * exsuono — suonare un file WAV o MP3 da un programma (9 ottobre 2026)
 *
 * E' la libreria condivisa /exwin/lib/exsuono.so: chi la usa collega lo stub
 * (exsuono_stub.c) e include questo file. Serve a due mestieri diversi.
 *
 * 1. «FAI QUESTO SUONO» — un avviso dopo un'azione, l'intro all'avvio della
 *    scrivania. Una chiamata, e non ci si pensa piu':
 *
 *        exsuono_avvia_file("/exwin/sound/intro_exos_benvenuto10s.wav");
 *
 *    Il suono lo porta avanti un altro processo (`audio`): chi chiama non
 *    deve fare niente, e puo' anche uscire.
 *
 * 2. UN LETTORE — aprire un brano, farlo partire, metterlo in pausa, andare a
 *    un punto, sapere a che punto e'. Il brano lo porta avanti CHI CHIAMA,
 *    dando la parola alla libreria con exsuono_passo() qualche decina di
 *    volte al secondo (da una sveglia della finestra): la libreria non ha un
 *    filo suo. Un brano per volta.
 *
 *        ExSuonoInfo i;
 *        if (exsuono_apri("/home/brano.mp3", &i) == 0) {
 *            exsuono_suona();
 *            ... a ogni sveglia:  if (!exsuono_passo()) il brano e' finito
 *            ... exsuono_posizione(), exsuono_vai(ms), exsuono_pausa()
 *            exsuono_chiudi();
 *        }
 *
 * Se la scheda non fa la frequenza o i canali del file, la libreria converte:
 * il brano suona alla velocita' giusta comunque.
 * ============================================================================= */
#ifndef EXSUONO_H
#define EXSUONO_H

typedef struct {
    unsigned int hz;            /* del file */
    unsigned int canali;
    unsigned int durata_ms;
    int          mp3;           /* 1 = MP3, 0 = WAV */
} ExSuonoInfo;

/* Codici d'errore (negativi). */
#define EXSUONO_NO_FILE     (-1)    /* non si apre, o non e' un WAV ne' un MP3 */
#define EXSUONO_NO_SCHEDA   (-2)    /* il servizio audio non c'e' (audio -i) */
#define EXSUONO_NO_MEMORIA  (-3)
#define EXSUONO_OCCUPATA    (-4)    /* la scheda sta suonando per un altro programma */

/* Quanto dura un file, senza toccare la scheda ne' il brano aperto. 0, o un
 * errore. Legge solo la testa del file, per fare in fretta un elenco: di un
 * WAV la durata e' esatta, di un MP3 e' una stima (quella esatta la da'
 * exsuono_apri in ExSuonoInfo). */
int          exsuono_durata(const char *file, unsigned int *ms);

/* Apre un brano: pronto a suonare dall'inizio, fermo. 0, o un errore. */
int          exsuono_apri(const char *file, ExSuonoInfo *info);
void         exsuono_chiudi(void);

/* Parte, o riparte dopo una pausa. */
int          exsuono_suona(void);
void         exsuono_pausa(void);

/* Va a un punto del brano, in millisecondi dall'inizio. */
int          exsuono_vai(unsigned int ms);

/* Da chiamare spesso mentre si suona. 1 = c'e' ancora brano, 0 = finito. */
int          exsuono_passo(void);

/* A che punto e', in millisecondi. */
unsigned int exsuono_posizione(void);

/* 1 se sta suonando (non in pausa, non finito). */
int          exsuono_in_corso(void);

/* Il volume della scheda, 0..100. */
void         exsuono_volume(unsigned int percento);

/* -----------------------------------------------------------------------------
 * Il mixer (10 ottobre 2026): ogni uscita e ogni ingresso della scheda, coi
 * due canali. Non serve avere un brano aperto.
 *
 * exsuono_mix_leggi(i, &v) riempie la voce numero i e rende quante voci ci
 * sono in tutto (si comincia da 0 e si va avanti finche' i < quel numero);
 * exsuono_mix_metti(i, sin, des, &v) la porta a quei due valori, 0..100, e
 * in v (se non e' 0) la ridà com'e' rimasta DAVVERO: una scheda puo' non
 * avere i canali separati. Rendono un numero negativo se la scheda non
 * risponde o la voce non esiste. Una scheda semplice ha una voce sola,
 * «Volume».
 * --------------------------------------------------------------------------- */
#define EXSUONO_USCITA    0
#define EXSUONO_INGRESSO  1

typedef struct {
    unsigned int tipo;          /* EXSUONO_USCITA o EXSUONO_INGRESSO */
    unsigned int sin, des;      /* 0..100; 0 = muto */
    char         nome[40];
} ExSuonoVoce;

int          exsuono_mix_leggi(unsigned int indice, ExSuonoVoce *v);
int          exsuono_mix_metti(unsigned int indice, unsigned int sin, unsigned int des,
                               ExSuonoVoce *v);

/* Fa suonare un file a un altro processo e torna subito. Rende il suo PID
 * (da dare a exsuono_ferma_file), o un valore negativo. */
int          exsuono_avvia_file(const char *file);
void         exsuono_ferma_file(int pid);

#endif /* EXSUONO_H */

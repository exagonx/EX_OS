/* =============================================================================
 * lib/exftp/exftp.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * exftp — un client FTP da mettere dentro un programma
 *
 * ! STA IN UNA LIBRERIA E NON DENTRO UN COMANDO, ED E' IL PUNTO. `bin/ftp`
 * aveva il suo client scritto in casa, e finche' il programma era uno solo
 * andava bene. Da quando ce n'e' un secondo che parla FTP — `ftpswap`, che
 * tiene allineata una directory con un server — due copie della stessa cosa
 * sarebbero due posti dove sbagliare la stessa risposta a tre cifre. E'
 * la stessa ragione per cui l'archivio degli utenti e' finito in lib/exuser
 * il 19 agosto 2026, quando i programmi che lo leggevano sono diventati tre.
 *
 * ! SI COMPILA DENTRO CHI LA USA, come exuser: non e' una .so. Un client FTP
 * e' qualche chilobyte di codice, e una libreria dinamica in piu' costa un
 * file, una riga nel caricatore e un modo nuovo di rompersi.
 *
 * ! bin/ftp NON CI E' ANCORA PASSATO SOPRA, e va detto qui perche' e' il posto
 * dove si guarda: quel programma continua ad avere il suo. Portarcelo e' un
 * lavoro a se', da fare con il programma sotto mano e provandolo contro un
 * server vero — non di straforo mentre si scrive un comando nuovo.
 *
 * -----------------------------------------------------------------------------
 * QUEL CHE SA FARE, E PERCHE' PROPRIO QUESTO
 *
 *   - PASV e PORT: la passiva e l'attiva. La passiva funziona dietro un
 *     router, l'attiva no, ma certi server la vogliono e EX-OS sa gia'
 *     ascoltare e accettare (lo fa telnetd), quindi non costa una riga in piu'
 *     di stack.
 *   - MLSD, e LIST come ripiego: MLSD da' un elenco fatto per le macchine
 *     («type=file;size=1234;modify=20260915T...»), LIST da' quello che il
 *     server ritiene bello da leggere per un umano — e il formato cambia da
 *     server a server. Chi deve DECIDERE qualcosa su quei nomi vuole il primo.
 *   - MDTM e MFMT: leggere e SCRIVERE la data di un file remoto. La seconda e'
 *     quella che rende stabile un allineamento: caricato un file, gli si
 *     timbra la data del locale, e al giro dopo le due date combaciano invece
 *     di dire «il server e' piu' nuovo» per sempre.
 *   - SIZE, RETR, STOR, CWD, MKD, DELE, RMD.
 *
 * ! LE DATE FTP SONO IN UTC, e questa libreria le rende come time_t senza
 * toccarle. Chi confronta con le date locali deve sapere che EX-OS non sa in
 * che fuso si trova — localtime() e gmtime() sono la stessa funzione, vedi
 * libc.h — quindi «uguale» va inteso con una tolleranza, non al secondo.
 * ============================================================================= */
#ifndef EXFTP_H
#define EXFTP_H

#include "libc.h"

#define EXFTP_RIGA_MAX     512    /* una riga di risposta                     */
#define EXFTP_ACC_MAX     2048    /* l'accumulatore del canale di controllo   */
#define EXFTP_NOME_MAX     256    /* un nome di file remoto                   */

typedef struct {
    int           ctrl;              /* connessione di controllo, <=0 = chiusa */
    unsigned char ip[4];             /* il server                              */
    unsigned int  porta;
    int           passiva;           /* 1 = PASV (predefinita), 0 = PORT       */
    int           binario;           /* 1 = TYPE I, 0 = TYPE A                 */
    int           verboso;           /* stampa il dialogo, per capire i guai   */

    int           codice;            /* codice dell'ultima risposta            */
    char          risposta[EXFTP_RIGA_MAX];   /* la sua ultima riga            */

    char          acc[EXFTP_ACC_MAX];         /* righe non ancora consumate    */
    unsigned int  acc_len;
} ExFtp;

/* Una voce di elenco, come la rende MLSD (o LIST, alla meglio).
 *
 * `quando` e' un time_t UTC, oppure -1 se il server non l'ha detto. */
typedef struct {
    char nome[EXFTP_NOME_MAX];
    int  directory;
    long dimensione;
    long quando;
} ExFtpVoce;

/* Il gancio dell'elenco: rende 1 per continuare, 0 per smettere.
 *
 * ! L'ELENCO SI CONSEGNA A PEZZI e non si accumula, per la stessa ragione per
 * cui exhttp ha un verso: una directory con mille file non deve entrare nella
 * memoria di chi la legge. */
typedef int (*ExFtpElenca)(void *dato, const ExFtpVoce *v);

/* -----------------------------------------------------------------------------
 * Aprire e chiudere
 *
 * `host` puo' essere un nome o un indirizzo: la risoluzione la fa dns_risolvi.
 * Rende 0, o -errno. In caso di rifiuto del server, `risposta` dice la sua.
 * --------------------------------------------------------------------------- */
int  exftp_apri(ExFtp *f, const char *host, unsigned int porta,
                const char *utente, const char *password);
void exftp_chiudi(ExFtp *f);

/* Modo dei dati: 1 binario (TYPE I), 0 testo (TYPE A). */
int  exftp_binario(ExFtp *f, int si);

/* Canale dati: 1 passiva (PASV), 0 attiva (PORT). Non parla col server: la
 * scelta si applica al prossimo trasferimento. */
void exftp_passiva(ExFtp *f, int si);

/* -----------------------------------------------------------------------------
 * Le directory e i file
 *
 * Tutte rendono 0 (o >= 0 dove indicato) e -errno in caso di guaio; il perche'
 * a parole sta in `f->risposta`.
 * --------------------------------------------------------------------------- */
int  exftp_cd(ExFtp *f, const char *dir);
int  exftp_mkd(ExFtp *f, const char *dir);      /* -EEXIST se c'e' gia'        */
int  exftp_rmd(ExFtp *f, const char *dir);
int  exftp_dele(ExFtp *f, const char *file);

long exftp_size(ExFtp *f, const char *file);    /* byte, o -errno              */
long exftp_mdtm(ExFtp *f, const char *file);    /* time_t UTC, o -errno        */
int  exftp_mfmt(ExFtp *f, const char *file, long quando);

/* L'elenco della directory `dir` (0 o "" = quella corrente). */
int  exftp_elenco(ExFtp *f, const char *dir, ExFtpElenca g, void *dato);

/* I trasferimenti. `locale` e' un percorso su questa macchina. */
int  exftp_scarica(ExFtp *f, const char *remoto, const char *locale);
int  exftp_carica(ExFtp *f, const char *locale, const char *remoto);

#endif /* EXFTP_H */

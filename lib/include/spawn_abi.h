/* =============================================================================
 * lib/include/spawn_abi.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il blocco che il chiamante di spawn() passa al kernel — UNA definizione sola
 *
 * ! PERCHE' QUESTO FILE ESISTE, il 17 agosto 2026.
 *
 * Di questa struttura c'erano QUATTRO copie: kernel/include/syscall.h,
 * lib/include/libc.h, lib/libc.c e bin/sh/shell.c. Il 14 agosto ne sono state
 * aggiornate tre — aggiunti `flag` e `console`, magia da 'SPNY' a 'SPNZ' — e
 * la quarta, quella della shell, e' rimasta indietro.
 *
 * La magia ha fatto il suo mestiere: il kernel non ha riconosciuto il blocco e
 * l'ha IGNORATO invece di leggerlo storto. Ma «ignorato» vuol dire che per tre
 * giorni la shell non ha piu' avuto ne' redirezioni ne' ambiente:
 *
 *     ex-os:/> hello > /red.txt
 *     Ciao da /bin/hello!            <- a video, non nel file
 *     ex-os:/> ls /red.txt
 *     red.txt      0                 <- creato dalla shell, e vuoto
 *
 * ! UNA MAGIA PROTEGGE DAL DANNO, NON DALLO SFASAMENTO. Dice «questo blocco
 * non lo capisco» e limita il guaio; non dice a nessuno che c'e' una copia da
 * aggiornare, e un silenzio non si nota. Quattro copie di una struttura che
 * attraversa il confine fra ring 3 e ring 0 sono tre occasioni di dimenticarne
 * una: la difesa vera e' che la definizione sia una.
 *
 * ! NIENTE TIPI A LARGHEZZA FISSA QUI DENTRO, e apposta: `unsigned int` e
 * `int` bastano (su i386 sono 32 bit) e non obbligano chi include a portarsi
 * dietro <stdint.h> ne' i typedef del kernel. E' cio' che permette a
 * bin/sh/shell.c — che non usa la libc e si dichiara i tipi da se' — di
 * includere questo file invece di ricopiarlo.
 *
 * ! SE SI CAMBIA LA DISPOSIZIONE si cambia anche SPAWN_EXTRA_MAGIA, sempre. E
 * l'asserzione in fondo fa fallire la COMPILAZIONE se la misura cambia senza
 * che qualcuno se ne accorga: un errore del compilatore invece di tre giorni
 * di redirezioni che non redirigono.
 * ============================================================================= */

#ifndef SPAWN_ABI_H
#define SPAWN_ABI_H

/* -----------------------------------------------------------------------------
 * La magia
 *
 *   'SPNX'  la prima forma
 *   'SPNY'  agosto 2026: SpawnAzione prende due campi
 *   'SPNZ'  14 agosto 2026: SpawnExtra prende `flag` e `console`
 *   'SPO0'  24 agosto 2026: SpawnExtra prende `uid` e `gid`
 *
 * Un blocco con la magia sbagliata il kernel lo IGNORA. E' il modo meno
 * dannoso di sbagliare: una redirezione letta storta scriverebbe nel file
 * sbagliato.
 * --------------------------------------------------------------------------- */
#define SPAWN_EXTRA_MAGIA    0x53504F30u   /* 'SPO0' */

/* Il figlio nasce sulla console indicata invece che su quella del padre. */
#define SPAWN_F_CONSOLE      0x00000001u

/* Il figlio nasce con l'identita' indicata invece che con quella del padre.
 * ! SOLO root PUO' CHIEDERLO: vedi il commento sui campi, in fondo. */
#define SPAWN_F_UTENTE       0x00000002u

#define SPAWN_MAX_AZIONI     4
#define SPAWN_RED_PATH_MAX   128

/* Le due cose che si possono fare a un descrittore del figlio. */
#define SPAWN_AZ_FILE   0   /* apri `percorso` e mettilo su `fd` */
#define SPAWN_AZ_FD     1   /* ! dai al figlio il descrittore `fd_padre` DEL PADRE */

typedef struct {
    unsigned int tipo;                     /* SPAWN_AZ_FILE / SPAWN_AZ_FD */
    unsigned int fd;                       /* descrittore del FIGLIO da sostituire */
    unsigned int flags;                    /* O_RDONLY/O_WRONLY/O_CREAT/... */
    int          fd_padre;                 /* SPAWN_AZ_FD: quale fd del padre */
    char         percorso[SPAWN_RED_PATH_MAX];
} SpawnAzione;

typedef struct {
    unsigned int magia;                    /* SPAWN_EXTRA_MAGIA, o il blocco e' ignorato */
    char       **envp;                     /* NULL-terminato; NULL = nessun ambiente */
    unsigned int n_azioni;
    SpawnAzione  azioni[SPAWN_MAX_AZIONI];

    /* ! LA CONSOLE SU CUI NASCE IL FIGLIO — aggiunta il 14 agosto 2026.
     *
     * Serve al server grafico: deve poter stare su una console tutta sua,
     * mentre su un'altra continua a girare una shell, e con Alt+Fn si passa
     * dall'una all'altra. Senza, il figlio eredita e basta, quindi il server
     * nasce dove gira chi l'ha lanciato — cioe' addosso alla shell, e i due si
     * contendono lo schermo.
     *
     * ! IL VALORE E' GUARDATO SOLO SE IL FLAG C'E', e non e' pedanteria. Se
     * «console 0» fosse il modo di dire «eredita», un chiamante che azzera la
     * struttura con memset — cioe' il modo normale di riempirla — chiederebbe
     * la console 0 senza volerlo. Con il flag, azzerare vuol dire eredita, che
     * e' il comportamento di sempre. */
    unsigned int flag;                     /* SPAWN_F_* */
    unsigned int console;                  /* valido solo con SPAWN_F_CONSOLE */

    /* ! CHI E' IL FIGLIO — aggiunti il 24 agosto 2026, e sono nati da un
     * difetto preciso.
     *
     * L'identita' si e' sempre EREDITATA, e chi voleva un figlio di un altro
     * utente aveva una strada sola: scendere con setuid() e poi lanciarlo. Va
     * bene per `sudo`, che dopo il comando muore; NON va bene per `login`, che
     * e' un CICLO. Scendendo, login diventava l'utente per sempre: al primo
     * `exit` tornava alla richiesta di accesso senza poter piu' leggere
     * /boot/ombra — che e' 0600 di root — e da li' in poi nessuno entrava piu'
     * su quella console. Con root non si vedeva, perche' per uid 0 il setuid
     * non si faceva nemmeno.
     *
     * ! E NON APRE NESSUNA PORTA, perche' la regola e' la stessa di setuid():
     * la puo' chiedere solo un processo che gia' e' root, e serve a SCENDERE.
     * Un processo di un utente che mettesse questo flag prende EPERM e non
     * parte niente — non «parte come prima», che sarebbe il modo silenzioso di
     * sbagliare.
     *
     * Validi solo con SPAWN_F_UTENTE: senza il flag, azzerare la struttura
     * chiederebbe uid 0, cioe' il contrario di quel che si vuole. E' la stessa
     * ragione per cui `console` ha il suo. */
    unsigned int uid;                      /* validi solo con SPAWN_F_UTENTE */
    unsigned int gid;
} SpawnExtra;

/* -----------------------------------------------------------------------------
 * ! LA MISURA E' PARTE DELL'ABI, e questa riga la inchioda.
 *
 *     magia 4 + envp 4 + n_azioni 4 + azioni 4*144 + flag 4 + console 4
 *     + uid 4 + gid 4 = 604
 *
 * Se qualcuno aggiunge un campo senza cambiare la magia, la compilazione si
 * ferma qui invece di lasciare in giro binari che non si capiscono fra loro.
 * L'array di misura negativa e' il modo che funziona anche senza C11.
 * --------------------------------------------------------------------------- */
typedef char spawn_abi_misura_invariata[(sizeof(SpawnExtra) == 604) ? 1 : -1];

#endif /* SPAWN_ABI_H */

/* =============================================================================
 * src/rtlib/exos/hinit.c  (da tools/freebasic-exos/exos/)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later   (parte della runtime di FB)
 * =============================================================================
 *
 * Accensione e spegnimento della runtime di FreeBASIC su EX-OS.
 *
 * ⚠️ QUANTO POCO C'E' QUI E' IL PUNTO. La versione unix/ di questo file e'
 * di 576 righe: mette il terminale in modo raw con termios, installa i
 * gestori di SIGINT e SIGWINCH, avvia un thread di sfondo per la
 * tastiera, interroga terminfo. Di quelle quattro cose EX-OS non ha
 * nessuna, e fingere di averle sarebbe peggio che non averle.
 *
 * ⚠️ NIENTE _control87: la precisione della FPU resta quella predefinita.
 * La porta DOS la forza a 64 bit «come in QB»; qui non si tocca perche'
 * il resto del sistema — cc1, openlibm, i programmi in C — assume la
 * parola di controllo standard, e cambiarla sotto di loro darebbe
 * arrotondamenti diversi a seconda che un programma FB sia partito prima
 * o no. Un programma non deve poter cambiare l'aritmetica di un altro.
 * ============================================================================= */

#include "../fb.h"
#include "fb_private_console.h"
#include <unistd.h>

FB_CONSOLE_CTX __fb_con;
char *__fb_startup_cwd;

void fb_hInit( void )
{
	memset( &__fb_con, 0, sizeof( FB_CONSOLE_CTX ) );

	/* La console di EX-OS e' una VGA in modo testo, e queste due misure
	 * sono un fatto del sistema, non una risposta di un terminale. */
	__fb_con.w = 80;
	__fb_con.h = 25;
	__fb_con.cur_x = 1;
	__fb_con.cur_y = 1;
	__fb_con.fg_color = FB_COLOR_WHITE;
	__fb_con.bg_color = FB_COLOR_BLACK;
	__fb_con.inited = TRUE;

	/* ⚠️ SI PRENDE UNA VOLTA SOLA, all'avvio: CURDIR puo' cambiare mentre
	 * il programma gira, ma EXEPATH e i percorsi relativi risolti alla
	 * partenza devono restare quelli di allora. */
	__fb_startup_cwd = malloc( MAX_PATH );
	if( __fb_startup_cwd != NULL ) {
		if( getcwd( __fb_startup_cwd, MAX_PATH ) == NULL ) {
			/* Senza directory corrente si continua lo stesso, con la
			 * radice: e' l'unico percorso che esiste di sicuro. */
			__fb_startup_cwd[0] = '/';
			__fb_startup_cwd[1] = '\0';
		}
	}
}

void fb_hEnd( int errlevel )
{
	/* ⚠️ NON si libera __fb_startup_cwd: fb_hEnd() puo' essere chiamata
	 * dal cammino di uscita mentre altro codice della runtime sta ancora
	 * per leggerla, e liberare qualcosa che il sistema sta per buttare
	 * comunque non guadagna niente e puo' costare un puntatore penzolante.
	 *
	 * L'argomento e' il livello d'errore di END: lo consegna chi ci
	 * chiama, non noi. */
	(void)errlevel;
}

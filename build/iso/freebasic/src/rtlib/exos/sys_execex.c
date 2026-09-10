/* =============================================================================
 * src/rtlib/exos/sys_execex.c  (da tools/freebasic-exos/exos/)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later   (parte della runtime di FB)
 * =============================================================================
 *
 * EXEC — lancia UN programma con una lista di argomenti gia' divisi.
 *
 * ! NON E' SHELL, e la differenza va tenuta: qui nessuno interpreta
 * virgolette, asterischi o redirezioni. Un programma FB che scrive
 *
 *     EXEC "/bin/ls", "*.bas"
 *
 * si aspetta che l'asterisco arrivi al programma cosi' com'e', e arriva.
 * Con SHELL sarebbe la shell a espanderlo — vedi sys_hshell.c.
 *
 * ! QUESTA E' LA FUNZIONE CON CUI fbc LANCIA `as` E `ld`. Se non
 * funziona, il compilatore compila e non produce niente — e lo scopre
 * solo al link, con un messaggio che parla d'altro.
 *
 * ! IL NOME DEL FILE NON E' LIBERO, e questo file esiste perche' lo era
 * sembrato. Fino all'11 agosto 2026 EXEC e SHELL stavano insieme in
 * `exos/sys_exec.c` — un nome che COLLIDE con `src/rtlib/sys_exec.c`, il
 * file comune che definisce fb_Exec e CHIAMA fb_ExecEx.
 *
 * Il makefile di FreeBASIC appiattisce gli oggetti in una directory sola:
 *
 *     LIBFB_C := $(sort $(foreach i,$(RTLIB_DIRS), ... %.c -> obj/%.o))
 *     VPATH   = $(RTLIB_DIRS)
 *
 * Due sorgenti con lo stesso nome danno lo STESSO oggetto, `$(sort)` li
 * fonde in uno, e VPATH cerca in ordine — trovando prima `src/rtlib/`.
 * Risultato: il nostro file non veniva compilato MAI, e libfb.a usciva
 * senza fb_ExecEx e senza fb_hShell. Il link di fbc si fermava su
 *
 *     libfb.a(sys_exec.o): undefined reference to `fb_ExecEx'
 *
 * cioe' accusando il file comune, che era innocente.
 *
 * ! E NON SI VEDEVA COSTRUENDO IN CROCE: prepara-fb.sh non usa il
 * makefile: compila a mano ogni .c dello strato e agli oggetti mette il prefisso
 * `exos_`. Lo scontro di nomi li' non esiste, quindi la libfb.a del CD e'
 * completa e quella costruita dal makefile no. Due strade per lo stesso
 * risultato, e una sola sbagliata.
 *
 * unix, dos, win32 e xbox dividono tutti in sys_execex.c + sys_hshell.c.
 * Adesso anche noi.
 * ============================================================================= */

#include "../fb.h"
#include <unistd.h>

FBCALL int fb_ExecEx( FBSTRING *program, FBSTRING *args, int do_wait )
{
	char    *argomenti, **argv, *p;
	int      i, argc = 0, pid, stato;
	ssize_t  len_argomenti;

	if( ( program == NULL ) || ( program->data == NULL ) ) {
		fb_hStrDelTemp( args );
		fb_hStrDelTemp( program );
		return -1;
	}

	if( args == NULL ) {
		argomenti = (char *)"";
	} else {
		len_argomenti = FB_STRSIZE( args );
		argomenti = alloca( len_argomenti + 1 );
		argomenti[len_argomenti] = 0;
		if( len_argomenti )
			argc = fb_hParseArgs( argomenti, args->data, len_argomenti );
	}

	/* ! IL PERCORSO SI COPIA PRIMA DI LIBERARE LE FBSTRING TEMPORANEE:
	 * `program->data` e' memoria della runtime, e fb_hStrDelTemp puo'
	 * riprendersela. Passarla a spawn() dopo averla liberata funziona
	 * quasi sempre — che e' il modo peggiore in cui un difetto puo'
	 * comportarsi. */
	{
		char percorso[MAX_PATH];
		ssize_t lp = (ssize_t)strlen( program->data );

		if( lp >= MAX_PATH ) lp = MAX_PATH - 1;
		memcpy( percorso, program->data, (size_t)lp );
		percorso[lp] = '\0';

		FB_STRLOCK();
		fb_hStrDelTemp_NoLock( args );
		fb_hStrDelTemp_NoLock( program );
		FB_STRUNLOCK();

		if( argc == -1 ) return -1;

		argc++;                     /* piu' uno per il nome del programma */
		argv = alloca( sizeof( char * ) * ( argc + 1 ) );
		argv[0] = percorso;

		p = argomenti;
		for( i = 1; i < argc; i++ ) {
			argv[i] = p;
			while( *p++ ) { }       /* al carattere dopo il prossimo NUL */
		}
		argv[argc] = NULL;

		pid = spawn( percorso, argv );
		if( pid < 0 ) return -1;

		/* ! do_wait == 0 NON E' execv(): su EX-OS non esiste il
		 * rimpiazzo dell'immagine di un processo. Il figlio parte e noi
		 * torniamo subito, il che e' quanto di piu' vicino ci sia — e
		 * cambia il valore di ritorno, che diventa il PID invece del
		 * codice di uscita. */
		if( !do_wait ) return pid;

		if( waitpid( pid, &stato, 0 ) < 0 ) return -1;
		return stato;
	}
}

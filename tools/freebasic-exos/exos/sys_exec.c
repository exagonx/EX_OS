/* =============================================================================
 * src/rtlib/exos/sys_exec.c  (da tools/freebasic-exos/exos/)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later   (parte della runtime di FB)
 * =============================================================================
 *
 * EXEC e SHELL.
 *
 * ⚠️ SONO DUE COSE DIVERSE E VANNO TENUTE DIVERSE. EXEC lancia UN
 * programma con una lista di argomenti gia' divisi: nessuno interpreta
 * virgolette, asterischi o redirezioni. SHELL passa una riga alla shell,
 * che le interpreta. Un programma FB che scrive
 *
 *     EXEC "/bin/ls", "*.bas"
 *
 * si aspetta che l'asterisco arrivi al programma cosi' com'e', e infatti
 * arriva; con SHELL sarebbe la shell a espanderlo.
 *
 * ⚠️ QUESTA E' LA FUNZIONE CON CUI fbc LANCIA `as` E `ld`. Se non
 * funziona, il compilatore compila e non produce niente — e lo scopre
 * solo al link, con un messaggio che parla d'altro.
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

	/* ⚠️ IL PERCORSO SI COPIA PRIMA DI LIBERARE LE FBSTRING TEMPORANEE:
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

		/* ⚠️ do_wait == 0 NON E' execv(): su EX-OS non esiste il
		 * rimpiazzo dell'immagine di un processo. Il figlio parte e noi
		 * torniamo subito, il che e' quanto di piu' vicino ci sia — e
		 * cambia il valore di ritorno, che diventa il PID invece del
		 * codice di uscita. */
		if( !do_wait ) return pid;

		if( waitpid( pid, &stato, 0 ) < 0 ) return -1;
		return stato;
	}
}

/* SHELL: la riga la interpreta /bin/sh, non noi. Vedi system() in
 * lib/libc.c per il perche' non si spezza il comando qui dentro. */
int fb_hShell( char *program )
{
	if( program == NULL ) return -1;
	return system( program );
}

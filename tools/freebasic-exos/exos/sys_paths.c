/* =============================================================================
 * src/rtlib/exos/sys_paths.c  (da tools/freebasic-exos/exos/)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later   (parte della runtime di FB)
 * =============================================================================
 *
 * CURDIR ed EXEPATH.
 *
 * ⚠️ IL SEPARATORE RESTA '/', e non si converte in '\' come fa la porta
 * DOS. Su EX-OS la barra rovesciata non e' un separatore: e' un carattere
 * come un altro, ammesso nei nomi. Convertire darebbe percorsi che il
 * sistema non sa aprire — e li darebbe proprio a chi li ha appena chiesti
 * per aprirli.
 * ============================================================================= */

#include "../fb.h"
#include <unistd.h>

ssize_t fb_hGetCurrentDir( char *dst, ssize_t maxlen )
{
	if( getcwd( dst, (size_t)maxlen ) == NULL ) {
		dst[0] = '\0';
		return 0;
	}
	return (ssize_t)strlen( dst );
}

/* Il percorso del PROPRIO eseguibile, senza il nome del file.
 *
 * ⚠️ SI RICAVA DA argv[0], e questo ha un limite che va detto: EX-OS non
 * ha /proc/self/exe ne' un modo di chiedere al kernel «da che file sono
 * partito?». La shell lancia sempre i programmi con argv[0] uguale al
 * percorso completo (vedi run_program in bin/sh/shell.c), quindi in
 * pratica funziona; un programma lanciato da qualcuno che passi un
 * argv[0] diverso otterrebbe la directory corrente, che e' il ripiego
 * ragionevole.
 *
 * ⚠️ SE UN GIORNO SERVISSE ESATTO, la strada e' una syscall che renda il
 * percorso dell'eseguibile del processo: il kernel ce l'ha gia' aperto
 * (`p->exe_handle`), quindi e' informazione che possiede — non c'e'
 * niente da indovinare. */
char *fb_hGetExePath( char *dst, ssize_t maxlen )
{
	const char *arg0 = ( __fb_ctx.argc > 0 && __fb_ctx.argv != NULL )
	                   ? __fb_ctx.argv[0] : NULL;
	ssize_t     len;
	char       *ultima;

	if( arg0 == NULL || arg0[0] == '\0' ) {
		return ( fb_hGetCurrentDir( dst, maxlen ) > 0 ) ? dst : NULL;
	}

	len = (ssize_t)strlen( arg0 );
	if( len >= maxlen ) len = maxlen - 1;
	memcpy( dst, arg0, (size_t)len );
	dst[len] = '\0';

	ultima = strrchr( dst, '/' );
	if( ultima == NULL ) {
		/* Nessuna barra: il programma e' stato nominato per nome nudo, e
		 * la sua directory e' quella corrente. */
		return ( fb_hGetCurrentDir( dst, maxlen ) > 0 ) ? dst : NULL;
	}

	/* La radice resta "/", non diventa la stringa vuota. */
	if( ultima == dst ) ultima[1] = '\0';
	else                *ultima   = '\0';

	return dst;
}

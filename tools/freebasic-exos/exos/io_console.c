/* =============================================================================
 * src/rtlib/exos/io_console.c  (da tools/freebasic-exos/exos/)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later   (parte della runtime di FB)
 * =============================================================================
 *
 * La stampa su console, e le due domande che la runtime le fa: quanto e'
 * grande e dove sta il cursore.
 *
 * ! NIENTE SEQUENZE DI ESCAPE. La versione unix/ emette \e%G per entrare
 * in UTF-8 quando deve stampare un carattere di controllo; la console di
 * EX-OS non interpreta escape e disegna il glifo della code page per ogni
 * byte, compresi quelli sotto il 32 — che e' proprio cio' che un
 * programma BASIC si aspetta quando stampa CHR(1). Qui i byte si passano
 * come sono.
 *
 * ! SI CONTA IL CURSORE MENTRE SI STAMPA. Vedi il commento in testa a
 * fb_private_console.h: EX-OS non ha una syscall che dica dov'e', e chi
 * scrive su stdout senza passare di qui fa perdere il conto.
 * ============================================================================= */

#include "../fb.h"
#include "fb_private_console.h"

FBCALL void fb_ConsoleGetSize( int *cols, int *rows )
{
	/* 80x25 anche prima di fb_hInit: e' un fatto del sistema, non uno
	 * stato da inizializzare. */
	if( cols ) *cols = __fb_con.inited ? __fb_con.w : 80;
	if( rows ) *rows = __fb_con.inited ? __fb_con.h : 25;
}

int fb_ConsoleGetX( void )
{
	return __fb_con.inited ? __fb_con.cur_x : 1;
}

void fb_ConsolePrintBufferEx( const void *buffer, size_t len, int mask )
{
	const unsigned char *p = (const unsigned char *)buffer;
	size_t i;

	(void)mask;

	if( !__fb_con.inited ) {
		fwrite( buffer, len, 1, stdout );
		fflush( stdout );
		return;
	}

	for( i = 0; i < len; i++ ) {
		unsigned char c = p[i];

		/* ! Lo ZERO diventa spazio, e non e' una stranezza nostra: in
		 * BASIC una stringa puo' contenere byte nulli, e su un terminale
		 * un NUL non stampa niente — il testo che segue si sposterebbe a
		 * sinistra rispetto a dove il programma crede di averlo messo. */
		if( c == 0 ) c = 32;

		fputc( c, stdout );

		if( c == '\n' ) {
			__fb_con.cur_x = 1;
			if( __fb_con.cur_y < __fb_con.h ) __fb_con.cur_y++;
		} else if( c == '\r' ) {
			__fb_con.cur_x = 1;
		} else {
			__fb_con.cur_x++;
			if( __fb_con.cur_x > __fb_con.w ) {
				__fb_con.cur_x = 1;
				if( __fb_con.cur_y < __fb_con.h ) __fb_con.cur_y++;
			}
		}
	}

	fflush( stdout );
}

void fb_ConsolePrintBuffer( const char *buffer, int mask )
{
	fb_ConsolePrintBufferEx( buffer, strlen( buffer ), mask );
}

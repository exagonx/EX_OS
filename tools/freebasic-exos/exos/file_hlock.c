/* =============================================================================
 * src/rtlib/exos/file_hlock.c  (da tools/freebasic-exos/exos/)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later   (parte della runtime di FB)
 * =============================================================================
 *
 * LOCK e UNLOCK.
 *
 * ⚠️ NON BLOCCANO NIENTE, E RIESCONO LO STESSO. EX-OS non ha un lucchetto
 * di regione su file: il VFS ha un solo lucchetto globale, che dura una
 * operazione e non e' esponibile a un programma.
 *
 * La scelta e' fra due mali, e questo e' il minore:
 *
 *   - fallire farebbe cadere ogni programma che blocca per prudenza, cioe'
 *     la maggior parte di quelli che aprono un file in scrittura, anche
 *     dove nessun altro processo tocchera' mai quel file;
 *   - riuscire senza bloccare lascia passare un accesso concorrente che il
 *     programma crede impedito.
 *
 * Il secondo caso richiede due processi sullo stesso file nello stesso
 * istante — raro qui, dove non ci sono demoni — e il primo si presenta
 * subito e sempre. E' la stessa convenzione, gia' presa e gia' scritta,
 * di chmod/umask in lib/libc.c: il nome c'e', il commento dice forte che
 * e' inerte.
 *
 * ⚠️ IL GIORNO CHE SERVISSE DAVVERO, il posto e' il VFS: servirebbe una
 * lista di regioni bloccate per inode e una syscall che la interroghi.
 * Non e' molto lavoro, ma e' lavoro vero — non si ottiene da qui.
 * ============================================================================= */

#include "../fb.h"

int fb_hFileLock( FILE *f, fb_off_t inipos, fb_off_t size )
{
	(void)f; (void)inipos; (void)size;
	return fb_ErrorSetNum( FB_RTERROR_OK );
}

int fb_hFileUnlock( FILE *f, fb_off_t inipos, fb_off_t size )
{
	(void)f; (void)inipos; (void)size;
	return fb_ErrorSetNum( FB_RTERROR_OK );
}

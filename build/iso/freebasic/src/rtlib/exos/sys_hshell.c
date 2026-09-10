/* =============================================================================
 * src/rtlib/exos/sys_hshell.c  (da tools/freebasic-exos/exos/)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later   (parte della runtime di FB)
 * =============================================================================
 *
 * SHELL — la riga la interpreta /bin/sh, non noi.
 *
 * Vedi system() in lib/libc.c per il perche' non si spezza il comando qui
 * dentro: virgolette, redirezioni e pipe le sa leggere la shell, e una
 * seconda mezza implementazione divergerebbe dalla prima il giorno stesso.
 *
 * ! NON E' EXEC: vedi sys_execex.c qui accanto.
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

int fb_hShell( char *program )
{
	if( program == NULL ) return -1;
	return system( program );
}

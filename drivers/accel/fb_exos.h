/* =============================================================================
 * src/rtlib/exos/fb_exos.h  (da tools/freebasic-exos/exos/)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * ! LGPL e non GPL: questo file entra nella runtime di FreeBASIC
 * (src/rtlib/), che e' sotto LGPL v2.1 con l'eccezione di collegamento.
 * Il compilatore fbc e' GPL v2 e sta altrove. Vedi doc/lgpl.txt
 * nell'albero di FreeBASIC.
 * =============================================================================
 *
 * L'intestazione di sistema della runtime, per il bersaglio EX-OS.
 *
 * ! MODELLATA SU dos/, NON SU unix/. Sembra il contrario di quel che si
 * farebbe — EX-OS ha una libc POSIX-ish — ma unix/ presuppone termios,
 * i segnali, i thread e ncurses, e nessuna delle quattro c'e' qui. La
 * porta DOS di FreeBASIC e' l'unica scritta per un sistema a un solo
 * flusso di esecuzione, con la console scritta direttamente: e' la nostra
 * stessa forma.
 * ============================================================================= */

#include <unistd.h>

#define FBCALL

/* Fine riga della console e dei file di testo.
 *
 * ! "\n" e non "\r\n" come DOS: la console di EX-OS tratta '\n' come
 * ritorno a capo completo (vga_putchar), e i suoi filesystem non hanno
 * una convenzione di riga propria. Mettere "\r\n" farebbe comparire un
 * carattere in piu' in ogni file scritto da un programma FB e letto da
 * un programma nostro. */
#define FB_NEWLINE "\n"
#define FB_NEWLINE_WSTR _LC("\n")

/* Sulla stampante, invece, "\r\n": e' quello che si aspetta chi legge il
 * flusso da fuori, ed e' la stessa scelta di tutte le altre porte. */
#define FB_BINARY_NEWLINE "\r\n"
#define FB_BINARY_NEWLINE_WSTR _LC("\r\n")

#define FB_LL_FMTMOD "ll"

/* Una pagina sola. Le console virtuali di EX-OS (Alt+F1..F4) sono del
 * SISTEMA, non del programma: un processo non puo' disegnarci sopra a
 * piacere, e fingere piu' pagine vorrebbe dire prometterlo. */
#define FB_CONSOLE_MAXPAGES 1

/* Niente caricamento dinamico per i programmi: il caricatore ELF del
 * kernel non riloca i binari normali. Il tipo deve comunque esistere,
 * perche' fb_private_hdynload.h lo nomina. */
#define FB_DYLIB void*

/* ! OFFSET A 32 BIT, ED E' UN LIMITE VERO: 2 GB per file.
 *
 * unix/ pretende _FILE_OFFSET_BITS=64 e fseeko/ftello; EX-OS ha lseek()
 * con un `long`, quindi qui si fa come DOS — si rimappano su fseek/ftell
 * e si dichiara la verita' invece di nasconderla dietro un typedef largo
 * che poi non regge. */
typedef long fb_off_t;
#define fseeko(stream, offset, whence) fseek(stream, offset, whence)
#define ftello(stream)                 ftell(stream)

/* Il lucchetto che protegge il disegno di sfondo dal thread di input.
 * Qui non c'e' nessun secondo flusso: sono due macro vuote, come su DOS.
 * ! Devono restare macro e non funzioni vuote — il codice comune le usa
 * dentro cicli stretti di stampa. */
#define BG_LOCK()
#define BG_UNLOCK()

/* I colori della console, nell'ordine dell'attributo VGA — che e' lo
 * stesso di DOS perche' e' lo stesso hardware. */
#define FB_COLOR_BLACK     (0)
#define FB_COLOR_BLUE      (1)
#define FB_COLOR_GREEN     (2)
#define FB_COLOR_CYAN      (3)
#define FB_COLOR_RED       (4)
#define FB_COLOR_MAGENTA   (5)
#define FB_COLOR_BROWN     (6)
#define FB_COLOR_WHITE     (7)
#define FB_COLOR_GREY      (8)
#define FB_COLOR_LBLUE     (9)
#define FB_COLOR_LGREEN    (10)
#define FB_COLOR_LCYAN     (11)
#define FB_COLOR_LRED      (12)
#define FB_COLOR_LMAGENTA  (13)
#define FB_COLOR_YELLOW    (14)
#define FB_COLOR_BWHITE    (15)

/* La directory corrente al momento dell'avvio. La tiene hinit.c: serve a
 * EXEPATH e a CURDIR, e va presa una volta sola perche' il programma puo'
 * cambiarla mentre gira. */
extern char *__fb_startup_cwd;

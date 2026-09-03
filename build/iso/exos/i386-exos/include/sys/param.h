/* =============================================================================
 * lib/include/sys/param.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <sys/param.h> — le macro che tutti danno per scontate.
 *
 * Non e' di nessuno standard: e' un'eredita' di BSD che ogni Unix si porta
 * dietro, e che il codice di terzi include per avere MIN, MAX e roundup
 * senza riscriverli. libctf di binutils lo fa in due file.
 *
 * ! MIN E MAX VALUTANO GLI ARGOMENTI DUE VOLTE, come su ogni sistema che
 * le definisce cosi'. `MAX(i++, n)` incrementa una volta o due a seconda
 * del confronto, ed e' un difetto che compila senza dire niente. Sono
 * scritte in questa forma perche' e' quella che il codice di terzi si
 * aspetta di trovare; nel codice di EX-OS si preferisca una funzione.
 * ============================================================================= */

#ifndef EXOS_SYS_PARAM_H
#define EXOS_SYS_PARAM_H

#include "../libc.h"

#ifndef MIN
#define MIN(a, b)   (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)   (((a) > (b)) ? (a) : (b))
#endif

/* Arrotonda `x` al multiplo di `y` superiore, e quante volte `y` sta in
 * `x` per eccesso. `y` deve essere positivo; roundup non richiede che sia
 * una potenza di due, a differenza della variante a maschera di bit che si
 * trova in giro con lo stesso nome. */
#ifndef roundup
#define roundup(x, y)   ((((x) + ((y) - 1)) / (y)) * (y))
#endif
#ifndef howmany
#define howmany(x, y)   (((x) + ((y) - 1)) / (y))
#endif

/* La lunghezza massima di un percorso. E' quella vera del kernel —
 * VFS_PATH_MAX in kernel/include/vfs.h, da cui syscall_impl.c ricava
 * PERCORSO_MAX — e le due devono restare uguali: un programma che
 * dimensiona un buffer su questa costante e poi riceve un percorso piu'
 * lungo dalla syscall scrive fuori. */
#ifndef MAXPATHLEN
#define MAXPATHLEN  320
#endif
#ifndef PATH_MAX
#define PATH_MAX    MAXPATHLEN
#endif

/* Il nome di un file da solo, senza il percorso: 255 caratteri piu' il
 * NUL, che e' il massimo di ext2. Su FAT i nomi restano 8.3. */
#ifndef NAME_MAX
#define NAME_MAX    255
#endif

#endif /* EXOS_SYS_PARAM_H */

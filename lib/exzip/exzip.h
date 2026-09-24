/* =============================================================================
 * lib/exzip/exzip.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ExZip — the ZIP format, and nothing else
 *
 *     #include <exzip.h>
 *
 * ! THIS LIBRARY KNOWS ABOUT ZIP AND KNOWS NOTHING ABOUT WINDOWS, and that is
 * the whole point of it being a library. Two programs were asked for - one for
 * the command line and one for ExWin - and the format has to be in one place,
 * or the second program rewrites it and from that day the two drift apart. It
 * is the same shape as the rest of the system: eximg decodes and the programs
 * draw, exhtml lays out and the browser shows.
 *
 * ! IT IS OPAQUE ON PURPOSE. ExZip is a pointer you never look inside, and
 * every field comes out through a function. The bridge table of a shared
 * library is a promise about NAMES, not about layouts: a struct that crosses
 * it must never change again, or every binary already built reads the wrong
 * bytes and nothing says so. One struct does cross - ExZipVoce, below - and it
 * is the one that has to be got right the first time.
 *
 * ! WHAT IT DOES NOT DO, said here rather than discovered later:
 *
 *   - it WRITES "deflate" when that makes a file smaller, "store" when it
 *     does not (a JPEG, a ZIP inside a ZIP). Until 23 September 2026 it
 *     wrote only "store"; the compressor is lib/exzip/deflate.c, and it
 *     streams like the rest;
 *   - it READS "store" and "deflate", which is what real archives are made of.
 *     Deflate goes through lib/eximg/inflate.c, which has been decoding PNG,
 *     GIF and fonts for months - no third-party source came into the system;
 *   - it does not encrypt, does not span volumes, does not do zip64. An
 *     archive above 2 GB, or one made of several files, is refused with a
 *     sentence instead of being half-read.
 *
 * ! AND MEMORY IS THE ONE PLACE WHERE IT IS NOT A STREAM. Storing and reading
 * "store" entries go through a fixed buffer, both ways, exactly so that an
 * archive bigger than memory is not a problem (that is the lesson of
 * @DIF-GROSSI). Deflate cannot: inflate() wants its whole output buffer from
 * the caller and does not stream. So a deflated entry is extracted through a
 * buffer as big as the entry, and when that cannot be allocated ex_zip_errore()
 * says the number instead of the library dying.
 * ============================================================================= */

#ifndef EXZIP_H
#define EXZIP_H

#ifdef __cplusplus
extern "C" {
#endif

#define EXZIP_NOME_MAX   256    /* a name inside the archive, '\0' included */
/* ! 65535 SINCE 24 SEPTEMBER 2026, and 1024 before: an archive with more
 * files did not open at all. 65535 is the format's own limit without ZIP64 —
 * the count in the end record is 16 bits — so no ordinary .zip is refused
 * any more. The tables it sizes live inside ExZip (about 2 MB), allocated
 * while an archive is open. */
#define EXZIP_VOCI_MAX  65535   /* entries in one archive, reading or writing */

/* The two methods this system knows. The number is the one in the file: it is
 * the format's, not ours. */
#define EXZIP_STORE     0
#define EXZIP_DEFLATE   8

typedef struct ExZip ExZip;

/* One entry, as the central directory describes it.
 *
 * ! THIS IS THE ONE STRUCT THAT CROSSES THE BRIDGE, so it is also the one
 * thing here that can never change shape. Anything that turns out to be
 * missing is added as a FUNCTION, never as a field. */
typedef struct {
    char          nome[EXZIP_NOME_MAX];
    unsigned long dim;          /* uncompressed size */
    unsigned long dim_c;        /* compressed size */
    unsigned long crc;          /* CRC-32 as the archive declares it */
    unsigned int  metodo;       /* EXZIP_STORE or EXZIP_DEFLATE */
    unsigned int  anno, mese, giorno, ore, minuti;
    int           directory;    /* 1 when the name ends with '/' */
} ExZipVoce;

/* -----------------------------------------------------------------------------
 * Reading
 * --------------------------------------------------------------------------- */

/* Opens an archive for reading. 0 when it cannot, and ex_zip_errore() says why. */
ExZip *ex_zip_apri(const char *percorso);

unsigned int ex_zip_quante(ExZip *z);

/* Fills *v with entry number i. Returns 1, or 0 if there is no such entry. */
int ex_zip_voce(ExZip *z, unsigned int i, ExZipVoce *v);

/* Extracts entry i into the file `dove`, which is created and overwritten.
 *
 * ! A DIRECTORY ENTRY IS NOT A FILE: for one of those this creates the
 * directory and writes nothing. Returns 1, or 0 with ex_zip_errore() set.
 *
 * ! AND IT DOES NOT INVENT THE NAME. `dove` is decided by the caller, because
 * the caller is the one who knows where the user wants it and what the
 * filesystem underneath will accept - on FAT a long name cannot be created at
 * all, and a library that silently shortened it would hand back a file with a
 * name nobody asked for. */
int ex_zip_estrai(ExZip *z, unsigned int i, const char *dove);

/* -----------------------------------------------------------------------------
 * Writing
 * --------------------------------------------------------------------------- */

/* Creates a new archive. 0 when it cannot. */
ExZip *ex_zip_crea(const char *percorso);

/* Adds `file` to the archive under the name `nome` (which is the name the
 * archive will carry: the caller decides whether it keeps a path). Returns 1,
 * or 0 with ex_zip_errore() set - and a failure here leaves the archive usable:
 * the entry simply is not in it. */
int ex_zip_aggiungi(ExZip *z, const char *file, const char *nome);

/* A whole directory, with everything under it (23 September 2026): `nome` is
 * what it is called inside the archive, and its files become "nome/...".
 * Every directory gets its own "nome/" entry, so empty ones survive. Returns
 * how many FILES went in, or -1 if it could not start. A file that cannot be
 * read does not stop the rest: `saltati` (may be 0) says how many were left
 * out, and then ex_zip_errore() says the last reason. */
int ex_zip_aggiungi_albero(ExZip *z, const char *cartella, const char *nome,
                           int *saltati);

/* Writes the central directory and closes. ! WITHOUT THIS THE FILE IS NOT AN
 * ARCHIVE: the entries are all there but nothing points at them, and every
 * unzip in the world will say the file is broken. Returns 1, or 0. */
int ex_zip_finisci(ExZip *z);

void ex_zip_chiudi(ExZip *z);

/* The last error, as a sentence to show someone. Never 0. */
const char *ex_zip_errore(void);

#ifdef __cplusplus
}
#endif

#endif /* EXZIP_H */

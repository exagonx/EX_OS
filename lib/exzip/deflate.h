/* =============================================================================
 * lib/exzip/deflate.h — the DEFLATE compressor (RFC 1951), as a stream
 *
 * Written on 23 September 2026 because archivi and zip stored everything
 * uncompressed: the library could only WRITE "store". Reading deflate was
 * already there (lib/eximg/inflate.c); this is the other half.
 *
 * ! IT IS A STREAM, BOTH WAYS. Data goes in by pieces (defl_dati) and comes
 * out by pieces through the function given to defl_apri. An archive bigger
 * than memory stays possible, which is the lesson of @DIF-GROSSI: the state is
 * a fixed ~330 KB, whatever the size of the file.
 *
 * ! AND IT IS DETERMINISTIC, WHICH exzip.c RELIES ON. The same bytes in give
 * the same bytes out, so the size can be measured on a first pass that
 * writes nothing (scrivi == 0) and then the data written on a second pass,
 * after a local header that already carries that size.
 *
 * ! ONE AT A TIME. The state is static and reused: EX-OS's free() gives
 * nothing back to the system, and 330 KB allocated for every file of an
 * archive would be memory lost for every file.
 * ============================================================================= */
#ifndef EXZIP_DEFLATE_H
#define EXZIP_DEFLATE_H

/* Where the compressed bytes go. Returns 1 if written, 0 on failure. */
typedef int (*DeflScrivi)(void *chi, const unsigned char *p, unsigned int n);

/* Starts a stream. scrivi == 0 means «count only». 0 if there is no memory. */
int           defl_apri(DeflScrivi scrivi, void *chi);

/* More data. 0 if writing failed. */
int           defl_dati(const unsigned char *p, unsigned int n);

/* The end: last block, flushed. 0 if writing failed. */
int           defl_fine(void);

/* Bytes produced so far (all of them, after defl_fine). */
unsigned long defl_prodotti(void);

#endif

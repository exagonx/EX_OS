/* =============================================================================
 * tools/libgit2-exos/exos-compat.h — force-included into every libgit2 file
 * built for EX-OS (gcc -include). Also meant for libgit2-sys (cargo).
 *
 * - st_rdev: EX-OS's struct stat has no device number for special files (it
 *   has no special files). Adding the field would change the struct that
 *   the 32-bit libc.so fills for programs already built; st_dev is always 0,
 *   which is what st_rdev would say. Used once, in index.c.
 * - SSL_get_peer_certificate is SSL_get1_peer_certificate (libgit2 frees the
 *   certificate it gets, so "get1" is right): the old name is deprecated.
 *   (Until 25 October 2026 ASN1_STRING_length went to ASN1_STRING_length_ex
 *   too: that was the development snapshot of OpenSSL; 4.1.0, built by
 *   tools/openssl-exos/costruisci.sh with the deprecated calls in, has the
 *   old ASN1_STRING_length and no _ex.)
 * ============================================================================= */
#ifndef EXOS_LIBGIT2_COMPAT_H
#define EXOS_LIBGIT2_COMPAT_H
#define st_rdev st_dev
#define SSL_get_peer_certificate SSL_get1_peer_certificate
#endif

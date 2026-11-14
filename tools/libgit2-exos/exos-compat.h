/* =============================================================================
 * tools/libgit2-exos/exos-compat.h — force-included into every libgit2 file
 * built for EX-OS (gcc -include). Also meant for libgit2-sys (cargo).
 *
 * - (until 14 November 2026 st_rdev was st_dev here: the libc's struct
 *   stat has had its own st_rdev since 7 November.)
 * - SSL_get_peer_certificate is SSL_get1_peer_certificate (libgit2 frees the
 *   certificate it gets, so "get1" is right): the old name is deprecated.
 *   (Until 25 October 2026 ASN1_STRING_length went to ASN1_STRING_length_ex
 *   too: that was the development snapshot of OpenSSL; 4.1.0, built by
 *   tools/openssl-exos/costruisci.sh with the deprecated calls in, has the
 *   old ASN1_STRING_length and no _ex.)
 * ============================================================================= */
#ifndef EXOS_LIBGIT2_COMPAT_H
#define EXOS_LIBGIT2_COMPAT_H
#define SSL_get_peer_certificate SSL_get1_peer_certificate
#endif

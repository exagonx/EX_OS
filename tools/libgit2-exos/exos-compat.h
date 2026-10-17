/* =============================================================================
 * tools/libgit2-exos/exos-compat.h — force-included into every libgit2 file
 * built for EX-OS (gcc -include). Also meant for libgit2-sys (cargo).
 *
 * - st_rdev: EX-OS's struct stat has no device number for special files (it
 *   has no special files). Adding the field would change the struct that
 *   the 32-bit libc.so fills for programs already built; st_dev is always 0,
 *   which is what st_rdev would say. Used once, in index.c.
 * - The EX-OS OpenSSL is 4.1 built with no-deprecated: SSL_get_peer_certificate
 *   is SSL_get1_peer_certificate (libgit2 frees the certificate it gets, so
 *   "get1" is right) and ASN1_STRING_length is ASN1_STRING_length_ex.
 * ============================================================================= */
#ifndef EXOS_LIBGIT2_COMPAT_H
#define EXOS_LIBGIT2_COMPAT_H
#define st_rdev st_dev
#define SSL_get_peer_certificate SSL_get1_peer_certificate
/* The header still declares ASN1_STRING_length (only the library lacks it),
 * so the NAME is redirected to an inline of ours: the header's declaration
 * then redeclares it, with the same type, and keeps it internal. */
struct asn1_string_st;
__SIZE_TYPE__ ASN1_STRING_length_ex(const struct asn1_string_st *x);
static __inline__ int exos_asn1_string_length(const struct asn1_string_st *x)
{
    return (int)ASN1_STRING_length_ex(x);
}
#define ASN1_STRING_length exos_asn1_string_length
#endif

/* =============================================================================
 * tools/prove/tlsprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Il banco di prova di HMAC, HKDF e RSA-PSS, che gira SULL'HOST.
 *
 * Legge comandi da stdin, uno per riga, e stampa il risultato in esadecimale.
 * A confrontare e' tlsprova.py: qui non si decide niente.
 *
 *     hmac <chiave-hex> <dati-hex>
 *     extract <sale-hex|-> <ikm-hex>
 *     expand <prk-hex> <info-hex|-> <quanti>
 *     label <segreto-hex> <etichetta-hex> <contesto-hex|-> <quanti>
 *
 * ! L'ETICHETTA VA IN ESADECIMALE ANCHE SE E' TESTO, e la prima versione non
 * lo faceva: le etichette di TLS 1.3 CONTENGONO SPAZI — «c hs traffic»,
 * «res master» — e un `%s` si ferma al primo. La prova diceva «MALE» su un
 * codice giusto, perche' gli stava mandando meta' riga.
 *     pss <modulo-hex> <esponente-hex> <impronta-hex> <firma-hex> <sale|-1>
 *
 * ! SHA-256 LO METTE OPENSSL, come in certprova.c: dentro EX-OS la mette la
 * libc. Qui si provano HMAC, HKDF e PSS, non l'impronta.
 * ============================================================================= */

#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include "extls.h"

void sha256(const void *dati, unsigned int len, unsigned char out[32])
{
    SHA256((const unsigned char *)dati, len, out);
}

static unsigned int da_hex(const char *s, unsigned char *b, unsigned int max)
{
    unsigned int n = 0, i;
    size_t len;

    if (s[0] == '-' && s[1] == 0) return 0;
    len = strlen(s);
    for (i = 0; i + 1 < len && n < max; i += 2) {
        unsigned int v;
        if (sscanf(s + i, "%2x", &v) != 1) break;
        b[n++] = (unsigned char)v;
    }
    return n;
}

static void stampa(const unsigned char *b, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}

int main(void)
{
    static char riga[70000];

    while (fgets(riga, sizeof(riga), stdin)) {
        char cmd[16], a[9000], b[9000], c[9000], d[9000], e[16];
        int  campi = sscanf(riga, "%15s %8999s %8999s %8999s %8999s %15s",
                            cmd, a, b, c, d, e);

        if (campi < 2) continue;

        if (strcmp(cmd, "hmac") == 0 && campi >= 3) {
            static unsigned char k[4096], m[4096], out[32];
            unsigned int kn = da_hex(a, k, sizeof(k));
            unsigned int mn = da_hex(b, m, sizeof(m));

            extls_hmac(k, kn, m, mn, out);
            stampa(out, 32);

        } else if (strcmp(cmd, "extract") == 0 && campi >= 3) {
            static unsigned char s[4096], ikm[4096], out[32];
            unsigned int sn  = da_hex(a, s, sizeof(s));
            unsigned int in_ = da_hex(b, ikm, sizeof(ikm));

            extls_hkdf_extract(sn ? s : 0, sn, ikm, in_, out);
            stampa(out, 32);

        } else if (strcmp(cmd, "expand") == 0 && campi >= 4) {
            static unsigned char prk[32], info[512], out[4096];
            unsigned int pn = da_hex(a, prk, sizeof(prk));
            unsigned int in_ = da_hex(b, info, sizeof(info));
            unsigned int quanti = 0;

            sscanf(c, "%u", &quanti);
            if (pn != 32 || quanti > sizeof(out)) { printf("NO\n"); continue; }
            if (extls_hkdf_expand(prk, info, in_, out, quanti) != 0)
                printf("NO\n");
            else stampa(out, quanti);

        } else if (strcmp(cmd, "label") == 0 && campi >= 5) {
            static unsigned char seg[32], ctx[512], out[4096], et[256];
            unsigned int sn = da_hex(a, seg, sizeof(seg));
            unsigned int en = da_hex(b, et, sizeof(et) - 1);
            unsigned int cn = da_hex(c, ctx, sizeof(ctx));
            unsigned int quanti = 0;

            et[en] = 0;
            sscanf(d, "%u", &quanti);
            if (sn != 32 || quanti > sizeof(out)) { printf("NO\n"); continue; }
            if (extls_expand_label(seg, (const char *)et, ctx, cn, out, quanti) != 0)
                printf("NO\n");
            else stampa(out, quanti);

        } else if (strcmp(cmd, "pss") == 0 && campi >= 6) {
            static unsigned char n[1024], es[16], imp[32], f[1024];
            unsigned int nn = da_hex(a, n, sizeof(n));
            unsigned int en = da_hex(b, es, sizeof(es));
            unsigned int hn = da_hex(c, imp, sizeof(imp));
            unsigned int fn = da_hex(d, f, sizeof(f));
            int sale = -1;

            sscanf(e, "%d", &sale);
            if (hn != 32) { printf("NO\n"); continue; }
            printf("%s\n", extls_rsa_pss_verifica(n, nn, es, en, imp, f, fn,
                                                  sale) == 0 ? "SI" : "NO");
        } else {
            printf("?\n");
        }
    }
    return 0;
}

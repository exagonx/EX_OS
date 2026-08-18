/* =============================================================================
 * bin/crypttest/crypttest.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * crypttest — i vettori degli RFC, girati sulla macchina vera
 *
 * ! LA MATEMATICA E' GIA' PROVATA A TERRA, E NON BASTA. A terra gira su x86-64
 * con il compilatore del sistema ospite; qui gira su i386, con interi a 64 bit
 * emulati in coppie di registri e un compilatore diverso. Un numero che viene
 * giusto la' e storto qui vuol dire un trabocco o uno spostamento oltre la
 * larghezza — cose che il linguaggio non promette e che ogni macchina risolve
 * a modo suo.
 *
 * ! E I VETTORI NON SONO «UN ESEMPIO CHE FUNZIONA»: sono i numeri che calcola
 * il resto del mondo. Una crittografia che funziona solo con se stessa e' una
 * crittografia con cui non si parla con nessuno.
 * ============================================================================= */

#include "libc.h"
#include "excrypt.h"

static int falliti = 0, passati = 0;

static void esito(const char *che, int ok)
{
    printf("  %-52s %s\n", che, ok ? "ok" : "FALLITA");
    if (ok) passati++; else falliti++;
}

static void esa(const char *s, unsigned char *o, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        int a = s[2*i], b = s[2*i+1];

        a = (a >= 'a') ? a - 'a' + 10 : a - '0';
        b = (b >= 'a') ? b - 'a' + 10 : b - '0';
        o[i] = (unsigned char)((a << 4) | b);
    }
}

int main(void)
{
    printf("crypttest — i vettori degli RFC su questa macchina\n\n");

    /* --- ChaCha20, RFC 8439 2.4.2 ---------------------------------------- */
    {
        unsigned char chiave[32], nonce[12], out[114], indietro[114], att[16];
        const char *testo =
            "Ladies and Gentlemen of the class of '99: If I could offer you "
            "only one tip for the future, sunscreen would be it.";
        int i;

        for (i = 0; i < 32; i++) chiave[i] = (unsigned char)i;
        for (i = 0; i < 12; i++) nonce[i] = 0;
        nonce[7] = 0x4a;

        esa("6e2e359a2568f98041ba0728dd0d6981", att, 16);
        chacha20(chiave, 1, nonce, (const unsigned char *)testo, out, 114);
        esito("ChaCha20 (RFC 8439)", memcmp(out, att, 16) == 0);

        chacha20(chiave, 1, nonce, out, indietro, 114);
        esito("e due volte riporta al testo", memcmp(indietro, testo, 114) == 0);
    }

    /* --- Poly1305, RFC 8439 2.5.2 ---------------------------------------- */
    {
        unsigned char chiave[32], tag[16], att[16];
        const char *m = "Cryptographic Forum Research Group";

        esa("85d6be7857556d337f4452fe42d506a801038"
            "08afb0db2fd4abff6af4149f51b", chiave, 32);
        esa("a8061dc1305136c6c22b8baf0c0127a9", att, 16);

        poly1305(chiave, (const unsigned char *)m, (unsigned int)strlen(m), tag);
        esito("Poly1305 (RFC 8439)", memcmp(tag, att, 16) == 0);
    }

    /* --- X25519, RFC 7748 5.2 e 6.1 -------------------------------------- */
    {
        unsigned char k[32], u[32], out[32], att[32];

        esa("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", k, 32);
        esa("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", u, 32);
        esa("c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", att, 32);
        x25519(out, k, u);
        esito("X25519 (RFC 7748)", memcmp(out, att, 32) == 0);

        {
            unsigned char apriv[32], apub[32], bpriv[32], bpub[32], sa[32], sb[32];

            esa("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", apriv, 32);
            esa("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", bpriv, 32);
            esa("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", att, 32);

            x25519_pubblica(apub, apriv);
            x25519_pubblica(bpub, bpriv);
            x25519(sa, apriv, bpub);
            x25519(sb, bpriv, apub);

            esito("e i due capi arrivano allo stesso segreto",
                  memcmp(sa, sb, 32) == 0 && memcmp(sa, att, 32) == 0);
        }

        {
            unsigned char zero[32], priv[32], s[32];
            int i;

            for (i = 0; i < 32; i++) { zero[i] = 0; priv[i] = (unsigned char)(i+1); }
            esito("un punto di ordine piccolo si fa riconoscere",
                  x25519(s, priv, zero) == -1);
        }
    }

    /* --- SHA-512, FIPS 180-4 --------------------------------------------- */
    {
        unsigned char o[64], att[64];

        esa("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
            "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", att, 64);
        sha512((const unsigned char *)"abc", 3, o);
        esito("SHA-512 (FIPS 180-4)", memcmp(o, att, 64) == 0);
    }

    /* --- Ed25519, RFC 8032 7.1 ------------------------------------------- */
    {
        unsigned char seme[32], pub[32], pub_att[32], firma[64], firma_att[64];
        unsigned char m[3];

        esa("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seme, 32);
        esa("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", pub_att, 32);
        esa("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e0652249015"
            "55fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", firma_att, 64);

        ed25519_pubblica(pub, seme);
        esito("Ed25519: la chiave pubblica (RFC 8032)", memcmp(pub, pub_att, 32) == 0);

        ed25519_firma(firma, (const unsigned char *)"", 0, seme, pub_att);
        esito("la firma del messaggio vuoto", memcmp(firma, firma_att, 64) == 0);
        esito("e si verifica",
              ed25519_verifica(firma_att, (const unsigned char *)"", 0, pub_att) == 0);

        m[0] = 'a'; m[1] = 'b'; m[2] = 'c';
        ed25519_firma(firma, m, 3, seme, pub_att);
        esito("una firma nostra si verifica",
              ed25519_verifica(firma, m, 3, pub_att) == 0);
        m[1] ^= 1;
        esito("e cambiando un bit del messaggio, no",
              ed25519_verifica(firma, m, 3, pub_att) != 0);
    }

    printf("\n%d prove superate, %d fallite\n", passati, falliti);
    return falliti == 0 ? 0 : 1;
}

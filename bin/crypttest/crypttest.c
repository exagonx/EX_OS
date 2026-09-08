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

/* +0.001 a ogni modifica: `crypttest -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("crypttest", "0.001");

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
    printf("crypttest - i vettori degli RFC su questa macchina\n\n");

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

    /* --- SHA-256 della libc, che usa anche lo scambio di chiavi di SSH ---- */
    {
        unsigned char o[32], att[32];

        esa("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", att, 32);
        sha256((const unsigned char *)"abc", 3, o);
        esito("SHA-256 della libc (FIPS 180-4)", memcmp(o, att, 32) == 0);

        /* ! UN'IMPRONTA SI PROVA ANCHE SU PIU' DI UN BLOCCO. Con tre byte si
         * esercita un blocco solo e il riempimento facile: il ciclo sui
         * blocchi e la lunghezza in coda restano fuori dalla prova. E' proprio
         * li' che stava il difetto che ha tenuto ferma la firma di SSH. */
        esa("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", att, 32);
        sha256((const unsigned char *)
               "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, o);
        esito("SHA-256 su 56 byte (due blocchi)", memcmp(o, att, 32) == 0);

        {
            static unsigned char lungo[1949];
            unsigned int i;

            for (i = 0; i < sizeof(lungo); i++)
                lungo[i] = (unsigned char)((i * 7 + i / 251) % 256);

            esa("4642d3ec6c293109ab118c476a0a9099c96a6158775c92515f84a98470b1ec78", att, 32);
            sha256(lungo, sizeof(lungo), o);
            esito("SHA-256 su 1949 byte (quanti ne ha lo scambio SSH)",
                  memcmp(o, att, 32) == 0);
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

    /* --- l'entropia del kernel, che a una sessione cifrata serve prima di
     *     tutto il resto --- */
    {
        unsigned char b[32];
        int r = getentropy(b, 32);

        if (r == 0) {
            int i, diversi = 0;

            for (i = 1; i < 32; i++) if (b[i] != b[0]) diversi++;
            esito("il kernel da' 32 byte imprevedibili", diversi > 20);
            printf("      primi byte: %02x %02x %02x %02x\n", b[0], b[1], b[2], b[3]);
        } else {
            esito("il kernel da' 32 byte imprevedibili", 0);
            printf("      getentropy rende %d, errno %d\n", r, errno);
        }
    }

    /* =====================================================================
     * I PEZZI DEL Wi-Fi: AES-CCM e la PSK di WPA
     *
     * ! GLI STESSI VETTORI DI `make prova-wpa`, MA QUI GIRANO SUL BERSAGLIO.
     * A terra si compila con gcc a 64 bit e le stesse righe possono
     * comportarsi diversamente: uno spostamento su un tipo largo, un accesso
     * non allineato, una struttura impacchettata in un altro modo. La prova
     * sull'host dice che la matematica e' giusta; questa dice che e' giusta
     * DOVE deve girare.
     * ===================================================================== */
    {
        static const unsigned char k[16] = {
            0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,
            0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf };
        static const unsigned char nonce[13] = {
            0x00,0x00,0x00,0x03,0x02,0x01,0x00,0xa0,
            0xa1,0xa2,0xa3,0xa4,0xa5 };
        static const unsigned char aad[8] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07 };
        static const unsigned char msg[23] = {
            0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
            0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
            0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e };
        static const unsigned char att[31] = {
            0x58,0x8c,0x97,0x9a,0x61,0xc6,0x63,0xd2,
            0xf0,0x66,0xd0,0xc2,0xc0,0xf9,0x89,0x80,
            0x6d,0x5f,0x6b,0x61,0xda,0xc3,0x84,
            0x17,0xe8,0xd1,0x2c,0xfd,0xf9,0x26,0xe0 };
        static const unsigned char aes128_k[16] = {
            0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
            0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c };
        static const unsigned char aes128_in[16] = {
            0x32,0x43,0xf6,0xa8,0x88,0x5a,0x30,0x8d,
            0x31,0x31,0x98,0xa2,0xe0,0x37,0x07,0x34 };
        static const unsigned char aes128_att[16] = {
            0x39,0x25,0x84,0x1d,0x02,0xdc,0x09,0xfb,
            0xdc,0x11,0x85,0x97,0x19,0x6a,0x0b,0x32 };
        static const unsigned char psk_att[32] = {
            0xf4,0x2c,0x6f,0xc5,0x2d,0xf0,0xeb,0xef,0x9e,0xbb,0x4b,0x90,
            0xb3,0x8a,0x5f,0x90,0x2e,0x83,0xfe,0x1b,0x13,0x5a,0x70,0xe2,
            0x3a,0xed,0x76,0x2e,0x97,0x10,0xa1,0x2e };
        static const unsigned char sha1_att[20] = {
            0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,
            0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d };
        AesChiave     ac;
        unsigned char cif[23], tagc[8], chiaro[23], blocco[16], psk[32], imp[20];

        printf("\n  -- i pezzi del Wi-Fi --\n");

        aes_chiave(&ac, aes128_k, 16);
        aes_cifra(&ac, aes128_in, blocco);
        esito("AES-128 (FIPS-197)", memcmp(blocco, aes128_att, 16) == 0);

        sha1("abc", 3, imp);
        esito("SHA-1 (RFC 3174)", memcmp(imp, sha1_att, 20) == 0);

        aes_chiave(&ac, k, 16);
        aes_ccm_cifra(&ac, nonce, 13, aad, 8, msg, cif, 23, tagc, 8);
        esito("AES-CCM, il cifrato (RFC 3610)", memcmp(cif, att, 23) == 0);
        esito("AES-CCM, il tag (RFC 3610)", memcmp(tagc, att + 23, 8) == 0);
        esito("e si decifra",
              aes_ccm_decifra(&ac, nonce, 13, aad, 8, cif, chiaro, 23,
                              tagc, 8) == 0 && memcmp(chiaro, msg, 23) == 0);

        cif[7] ^= 0x01;
        esito("un bit cambiato non si decifra",
              aes_ccm_decifra(&ac, nonce, 13, aad, 8, cif, chiaro, 23,
                              tagc, 8) != 0);

        /* ! QUESTA E' LENTA, ED E' GIUSTO CHE LO SIA: 4096 giri di HMAC-SHA1
         * sono cio' che rende costoso provare le password a tappeto. Se un
         * giorno questa riga diventasse istantanea, vorrebbe dire che
         * qualcuno ha abbassato i giri — e la rete si aprirebbe in un
         * pomeriggio invece che in un anno. */
        pbkdf2_sha1((const unsigned char *)"password", 8,
                    (const unsigned char *)"IEEE", 4, 4096, psk, 32);
        esito("la PSK di WPA (802.11-2007 H.4)",
              memcmp(psk, psk_att, 32) == 0);
    }

    /* =====================================================================
     * QUANTO COSTA UN BLOCCO CHE NON SI PUO' SPEZZARE
     *
     * ! NON E' UNA PROVA, E' UNA MISURA, e serve a una domanda precisa:
     * @DIF-TLS dice che dentro un x25519 o una verifica di firma «non si
     * respira» — la stretta TLS chiama il gancio di avanzamento FRA un passo e
     * l'altro, mai dentro. La domanda e' quanto duri il piu' lungo di quei
     * blocchi, perche' e' quello il tempo in cui un programma grafico resta
     * fermo senza poter ridisegnare.
     *
     * Il numero che girava — 150 ms — non era mai stato misurato: sta scritto
     * in extls.h «perche' chi misurera' i tempi sappia che cosa sta
     * guardando».
     *
     * ! DI QUESTE DUE, SOLO LA PRIMA STA NELLA STRETTA TLS, e la distinzione
     * costa cara a chi la salta. x25519 e' lo scambio di chiave di TLS 1.3 e
     * il numero qui sotto e' quello vero: la stretta misurata l'8 settembre
     * 2026 con `scarica -tempi` da' 30 ms fra «il server ha risposto» e
     * «concordo il segreto», e in mezzo non c'e' altro che x25519.
     *
     * ED25519 NON C'ENTRA CON https: e' la firma di SSH (le chiavi d'ospite di
     * sshd). TLS 1.3 qui verifica RSA-PSS oppure ECDSA su P-256 e P-384, e la
     * catena dei certificati usa PKCS#1 v1.5 o le stesse due curve — Ed25519
     * dentro un certificato lib/excert NON lo gestisce nemmeno. Il numero
     * resta perche' misura SSH, non perche' misuri l'https.
     *
     * ! E IL BLOCCO PIU' LUNGO NON E' NESSUNO DEI DUE: e' la verifica della
     * catena, 850 ms su una catena tutta ECDSA. Non si misura da qui, perche'
     * qui non c'e' una catena vera: si misura con `scarica -tempi <url>`, e il
     * risultato sta in @DIF-TLS.
     *
     * ! SI RIPETE PIU' VOLTE E SI DIVIDE, perche' l'orologio di EX-OS batte
     * ogni 10 ms: una misura sola su un'operazione da 20 ms avrebbe due cifre
     * significative e mezza.
     * ===================================================================== */
    {
        unsigned char priv[32], altrui[32], segreto[32];
        unsigned char seme[32], vpub[32], firma[64];
        const char   *msg = "abc";
        clock_t       t0;
        int           i, giri;

        printf("\nQuanto costa un blocco che non si puo' spezzare\n");

        for (i = 0; i < 32; i++) { priv[i] = (unsigned char)(i + 1);
                                   altrui[i] = (unsigned char)(0x40 + i); }
        for (i = 0; i < 32; i++) seme[i] = (unsigned char)(0x80 + i);

        giri = 20;
        t0 = clock();
        for (i = 0; i < giri; i++) x25519(segreto, priv, altrui);
        printf("  x25519 (lo scambio di chiave)      %ld ms\n",
               (long)((clock() - t0) * 1000 / CLOCKS_PER_SEC) / giri);

        ed25519_pubblica(vpub, seme);
        ed25519_firma(firma, (const unsigned char *)msg, 3, seme, vpub);

        giri = 20;
        t0 = clock();
        for (i = 0; i < giri; i++)
            ed25519_verifica(firma, (const unsigned char *)msg, 3, vpub);
        printf("  ed25519, verifica di una firma     %ld ms  (e' di SSH)\n",
               (long)((clock() - t0) * 1000 / CLOCKS_PER_SEC) / giri);

        printf("  la stretta TLS fa due volte il primo (la chiave e il\n");
        printf("  segreto); il secondo in TLS non c'entra. Il pezzo lungo\n");
        printf("  e' la catena: scarica -tempi <url>, e vedi @DIF-TLS.\n");
    }

    printf("\n%d prove superate, %d fallite\n", passati, falliti);
    return falliti == 0 ? 0 : 1;
}

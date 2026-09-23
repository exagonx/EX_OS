#!/bin/bash
# =============================================================================
# tools/prova_gcm.sh — AES-GCM di lib/excrypt, provato sull'host
#
# gcm.c e' scritto da zero per il TLS (23 settembre 2026), e un GCM sbagliato
# non si vede: il server chiude la connessione con un allarme e basta. Quindi
# si prova qui, in due modi:
#
#   1. i vettori del documento di GCM (McGrew e Viega), casi 1-4 e 13-16:
#      chiave a zero, testo vuoto, un blocco, l'AAD, e le chiavi da 256 bit;
#   2. cinquemila casi a caso contro `cryptography` di Python (AESGCM), con
#      chiavi da 16 e 32 byte e misure che non sono multipli di 16 — e per
#      ognuno anche la decifratura, e un byte guastato che deve farla fallire.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-gcm}"
mkdir -p "$D"

cat > "$D/gcm_banco.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "excrypt.h"

static int esa(const char *s, unsigned char *out)
{
    int n = 0;
    while (s[0] && s[1]) { sscanf(s, "%2hhx", &out[n++]); s += 2; }
    return n;
}

/* chiave iv aad testo  ->  stampa cifrato:tag, e controlla il ritorno */
int main(int argc, char **argv)
{
    static unsigned char k[32], iv[12], a[4096], p[65536], c[65536], d[65536], t[16];
    int kn, an, pn, i;
    GcmChiave g;

    kn = esa(argv[1], k); esa(argv[2], iv);
    an = esa(argc > 3 ? argv[3] : "", a);
    pn = esa(argc > 4 ? argv[4] : "", p);

    if (gcm_chiave(&g, k, (unsigned int)kn) != 0) { puts("CHIAVE"); return 1; }
    aes_gcm_cifra(&g, iv, a, (unsigned int)an, p, c, (unsigned int)pn, t);
    for (i = 0; i < pn; i++) printf("%02x", c[i]);
    printf(":");
    for (i = 0; i < 16; i++) printf("%02x", t[i]);

    if (aes_gcm_decifra(&g, iv, a, (unsigned int)an, c, d, (unsigned int)pn, t) != 0 ||
        memcmp(d, p, (size_t)pn) != 0) printf(":DECIFRA-NO");
    t[3] ^= 1;
    if (aes_gcm_decifra(&g, iv, a, (unsigned int)an, c, d, (unsigned int)pn, t) == 0)
        printf(":GUASTO-ACCETTATO");
    printf("\n");
    return 0;
}
EOF

gcc -O2 -Wall -I lib/excrypt "$D/gcm_banco.c" lib/excrypt/aes.c lib/excrypt/gcm.c \
    -o "$D/gcm_banco" || exit 1

python3 - "$D/gcm_banco" <<'PY'
import os, random, subprocess, sys
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
banco = sys.argv[1]

def nostro(k, iv, a, p):
    return subprocess.run([banco, k.hex(), iv.hex(), a.hex(), p.hex()],
                          capture_output=True, text=True).stdout.strip()

male = 0
# --- 1. i vettori del documento di GCM -------------------------------------
V = [
 ("00"*16, "00"*12, "", "", "", "58e2fccefa7e3061367f1d57a4e7455a"),
 ("00"*16, "00"*12, "", "00"*16, "0388dace60b6a392f328c2b971b2fe78", "ab6e47d42cec13bdf53a67b21257bddf"),
 ("feffe9928665731c6d6a8f9467308308", "cafebabefacedbaddecaf888", "",
  "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
  "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985",
  "4d5c2af327cd64a62cf35abd2ba6fab4"),
 ("feffe9928665731c6d6a8f9467308308", "cafebabefacedbaddecaf888", "feedfacedeadbeeffeedfacedeadbeefabaddad2",
  "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39",
  "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091",
  "5bc94fbc3221a5db94fae95ae7121a47"),
 ("00"*32, "00"*12, "", "", "", "530f8afbc74536b9a963b4f1c4cb738b"),
 ("00"*32, "00"*12, "", "00"*16, "cea7403d4d606b6e074ec5d3baf39d18", "d0d1c8a799996bf0265b98b5d48ab919"),
]
for i, (k, iv, a, p, c, t) in enumerate(V):
    r = nostro(bytes.fromhex(k), bytes.fromhex(iv), bytes.fromhex(a), bytes.fromhex(p))
    ok = (r == c + ":" + t)
    male += not ok
    print("%-4s vettore %d%s" % ("si'" if ok else "NO", i + 1, "" if ok else ": " + r))

# --- 2. a caso, contro cryptography ------------------------------------------
random.seed(23)
casi = 5000
for i in range(casi):
    kn = random.choice((16, 32))
    k, iv = os.urandom(kn), os.urandom(12)
    a = os.urandom(random.choice((0, 5, 13, 16, 21, 64)))
    p = os.urandom(random.randint(0, 300))
    ct = AESGCM(k).encrypt(iv, p, a)
    atteso = ct[:-16].hex() + ":" + ct[-16:].hex()
    r = nostro(k, iv, a, p)
    if r != atteso:
        male += 1
        if male < 5: print("NO   caso %d: %s\n     atteso %s" % (i, r, atteso))
print("%s %d casi a caso contro cryptography, con decifratura e guasto" % ("si'" if male == 0 else "NO ", casi))
print("ESITO: %s" % ("BUONO" if male == 0 else "%d sbagliati" % male))
sys.exit(1 if male else 0)
PY

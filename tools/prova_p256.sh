#!/bin/bash
# =============================================================================
# tools/prova_p256.sh — l'ECDH su P-256 di lib/excrypt, provato sull'host
#
# p256.c e' scritto da zero, a tempo costante, per lo scambio di chiavi del TLS
# (23 settembre 2026: www.poste.it non accetta x25519). Si confronta con
# `cryptography` di Python:
#
#   1. mille chiavi private a caso: la chiave pubblica deve essere la stessa;
#   2. mille scambi: il segreto condiviso con una chiave altrui deve essere lo
#      stesso che calcola cryptography;
#   3. un punto fuori dalla curva, e uno con una coordinata oltre p, devono
#      essere RIFIUTATI (e' l'attacco «invalid curve»).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-p256}"
mkdir -p "$D"

cat > "$D/p256_banco.c" <<'EOC'
#include <stdio.h>
#include "excrypt.h"
static void esa(const char *s, unsigned char *o, int n)
{ int i; for (i = 0; i < n; i++) sscanf(s + 2 * i, "%2hhx", &o[i]); }
int main(int argc, char **argv)
{
    unsigned char k[32], pub[65], punto[65], seg[32];
    int i;
    esa(argv[1], k, 32);
    if (argc == 2) {
        if (p256_pubblica(k, pub) != 0) { puts("NO"); return 0; }
        for (i = 0; i < 65; i++) printf("%02x", pub[i]);
    } else {
        esa(argv[2], punto, 65);
        if (p256_condiviso(k, punto, 65, seg) != 0) { puts("RIFIUTATO"); return 0; }
        for (i = 0; i < 32; i++) printf("%02x", seg[i]);
    }
    puts("");
    return 0;
}
EOC
gcc -O2 -Wall -I lib/excrypt "$D/p256_banco.c" lib/excrypt/p256.c -o "$D/p256_banco" || exit 1

python3 - "$D/p256_banco" <<'PY'
import os, subprocess, sys
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import serialization
banco = sys.argv[1]
N = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551
def gira(*a): return subprocess.run([banco] + list(a), capture_output=True, text=True).stdout.strip()
def pub_di(priv):
    return priv.public_key().public_bytes(serialization.Encoding.X962,
                                          serialization.PublicFormat.UncompressedPoint)
male = 0
for i in range(1000):
    d = int.from_bytes(os.urandom(32), "big") % (N - 1) + 1
    if i == 0: d = 1
    if i == 1: d = N - 1
    priv = ec.derive_private_key(d, ec.SECP256R1())
    if gira(d.to_bytes(32, "big").hex()) != pub_di(priv).hex():
        male += 1
        if male < 4: print("NO   chiave pubblica per d =", hex(d))
print("%s mille chiavi pubbliche" % ("si'" if male == 0 else "NO "))
m2 = 0
for i in range(1000):
    a = ec.generate_private_key(ec.SECP256R1()); b = ec.generate_private_key(ec.SECP256R1())
    da = a.private_numbers().private_value.to_bytes(32, "big")
    atteso = a.exchange(ec.ECDH(), b.public_key()).hex()
    if gira(da.hex(), pub_di(b).hex()) != atteso:
        m2 += 1
        if m2 < 4: print("NO   scambio", i)
print("%s mille segreti condivisi" % ("si'" if m2 == 0 else "NO "))
k = os.urandom(32).hex()
fuori = "04" + "00" * 31 + "01" + "00" * 31 + "02"
oltre = "04" + "ff" * 32 + "00" * 31 + "01"
r1, r2 = gira(k, fuori), gira(k, oltre)
m3 = (r1 != "RIFIUTATO") + (r2 != "RIFIUTATO")
print("%s il punto fuori dalla curva e la coordinata oltre p rifiutati" % ("si'" if m3 == 0 else "NO "))
tot = male + m2 + m3
print("ESITO: %s" % ("BUONO" if tot == 0 else "%d sbagliati" % tot))
sys.exit(1 if tot else 0)
PY

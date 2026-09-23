#!/bin/bash
# =============================================================================
# tools/prova_deflate.sh — il compressore DEFLATE di exzip, provato sull'host
#
# lib/exzip/deflate.c e' scritto da zero, e un compressore sbagliato non si
# vede finche' qualcuno non prova ad aprire l'archivio. Quindi si prova qui,
# sull'host, prima di entrare in EX-OS, e contro DUE decodificatori:
#
#   - zlib di Python (zlib.decompress con wbits=-15, cioe' DEFLATE nudo): e'
#     quello che non e' nostro, e dice se il flusso rispetta l'RFC 1951;
#   - lib/eximg/inflate.c, il nostro: e' quello che dentro EX-OS lo rilegge.
#
# E si controlla anche la promessa su cui exzip.c si appoggia: il giro «solo
# conteggio» (scrivi == 0) deve dare esattamente la misura del giro vero.
#
# I file di prova sono scelti per i casi scomodi: vuoto, un byte, tutti zeri
# (copie lunghe e distanza 1), casuale (niente da comprimere), testo, binari,
# e i dati passati a pezzi di misura casuale, perche' il compressore e' a
# flusso e le giunture sono il posto dove sbaglia.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-deflate}"
mkdir -p "$D/shim"

# Il compressore include "libc.h": sull'host quella di sistema basta.
printf '#include <stdlib.h>\n#include <string.h>\n' > "$D/shim/libc.h"

cat > "$D/prova.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deflate.h"
#include "inflate.h"

static FILE *g_out;
static int scrivi(void *chi, const unsigned char *p, unsigned int n)
{ (void)chi; return fwrite(p, 1, n, g_out) == n; }

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    unsigned char *dati, *c, *r;
    long n, i, contati, veri;
    unsigned int prodotti = 0;
    unsigned int seme = 12345;

    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    dati = malloc(n + 1); fread(dati, 1, n, f); fclose(f);

    /* 1. solo conteggio, a pezzi di misura casuale */
    defl_apri(0, 0);
    for (i = 0; i < n; ) {
        long k;
        seme = seme * 1103515245u + 12345u;
        k = 1 + (long)((seme >> 8) % 70000);
        if (k > n - i) k = n - i;
        defl_dati(dati + i, (unsigned int)k);
        i += k;
    }
    defl_fine();
    contati = (long)defl_prodotti();

    /* 2. il giro vero, a pezzi di misura DIVERSA: il risultato deve essere
     *    lo stesso, perche' non dipende da come arrivano i dati. */
    g_out = fopen(argv[2], "wb");
    defl_apri(scrivi, 0);
    for (i = 0; i < n; ) {
        long k = 4096;
        if (k > n - i) k = n - i;
        defl_dati(dati + i, (unsigned int)k);
        i += k;
    }
    defl_fine();
    veri = (long)defl_prodotti();
    fclose(g_out);

    /* 3. il nostro inflate */
    f = fopen(argv[2], "rb");
    c = malloc(veri + 1); fread(c, 1, veri, f); fclose(f);
    r = malloc(n + 1);
    if (inflate(c, (unsigned int)veri, r, (unsigned int)n, &prodotti) != 0 ||
        (long)prodotti != n || memcmp(r, dati, n) != 0) {
        printf("INFLATE-NOSTRO-NO %ld %ld\n", contati, veri);
        return 1;
    }
    printf("%ld %ld %ld\n", n, contati, veri);
    return 0;
}
EOF

gcc -O2 -Wall -I "$D/shim" -I lib/exzip -I lib/eximg \
    "$D/prova.c" lib/exzip/deflate.c lib/eximg/inflate.c -o "$D/prova" || exit 1

# I file di prova
: > "$D/vuoto"
printf 'a' > "$D/uno"
head -c 300000 /dev/zero > "$D/zeri"
head -c 200000 /dev/urandom > "$D/caso"
python3 -c "
import random; random.seed(7)
parole = open('README.md', encoding='utf-8').read().split()
print(' '.join(random.choice(parole) for _ in range(120000)))" > "$D/parole"

esito=0
for f in "$D/vuoto" "$D/uno" "$D/zeri" "$D/caso" "$D/parole" README.md \
         lib/libc.c build/exwin/bin/exbrowser dist/floppy.img; do
    [ -f "$f" ] || continue
    riga=$("$D/prova" "$f" "$D/uscita.deflate") || { echo "NO   $f: $riga"; esito=1; continue; }
    set -- $riga
    if [ "$2" != "$3" ]; then
        echo "NO   $f: il conteggio ($2) non e' la misura vera ($3)"; esito=1; continue
    fi
    if ! python3 -c "
import sys, zlib
d = zlib.decompress(open(sys.argv[2], 'rb').read(), -15)
sys.exit(0 if d == open(sys.argv[1], 'rb').read() else 1)" "$f" "$D/uscita.deflate"; then
        echo "NO   $f: zlib di Python non lo riapre uguale"; esito=1; continue
    fi
    zl=$(python3 -c "
import sys, zlib
c = zlib.compressobj(6, zlib.DEFLATED, -15)
print(len(c.compress(open(sys.argv[1], 'rb').read()) + c.flush()))" "$f")
    printf "si'  %-32s %9s -> %9s   (zlib -6: %s)\n" "$(basename "$f")" "$1" "$3" "$zl"
done
echo "ESITO: $([ $esito = 0 ] && echo BUONO || echo SBAGLIATO)"
exit $esito

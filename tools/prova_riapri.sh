#!/bin/bash
# =============================================================================
# tools/prova_riapri.sh — aggiungere a un archivio finito, e i livelli
#
# Prova, SULL'HOST e senza QEMU, le due cose aggiunte a lib/exzip il 26
# settembre 2026 per @ARCHIVI-VISTA:
#
#   1. ex_zip_riapri: si crea un archivio con due file, lo si finisce, lo si
#      riapre, ci si aggiunge un terzo file e si rifinisce. Poi lo giudicano
#      `unzip -t` (se c'e') e zipfile di Python: tre voci, tutte coi byte
#      giusti — anche le due VECCHIE, che non sono state ricopiate;
#   2. ex_zip_livello: lo stesso testo con i livelli 0..3. Il livello 0 deve
#      dare «store»; 1, 2 e 3 deflate, e il 3 non piu' grande del 1. Il 2 deve
#      essere IDENTICO a prima (e' il predefinito): lo confronta
#      tools/prova_deflate.sh con zlib -6.
#
# ! exzip.c SI COMPILA COSI' COM'E', con una libc.h di ripiego che porta
# quella di Linux e l'orologio. Se qui serve cambiare il sorgente, la prova
# non prova piu' il codice che gira su EX-OS.
#
#     tools/prova_riapri.sh [directory-di-lavoro]      (default /tmp/exos-riapri)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-riapri}"
rm -rf "$D"; mkdir -p "$D/shim" "$D/src"

cat > "$D/shim/libc.h" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
typedef struct { int anno, mese, giorno, ora, minuto, secondo; } RtcTime;
static inline int time_now(RtcTime *t)
{
    time_t s = time(0); struct tm *m = localtime(&s);
    t->anno = m->tm_year + 1900; t->mese = m->tm_mon + 1; t->giorno = m->tm_mday;
    t->ora = m->tm_hour; t->minuto = m->tm_min; t->secondo = m->tm_sec;
    return 0;
}
EOF

cat > "$D/prova.c" <<'EOF'
#include "libc.h"
#include "exzip.h"
static int no(const char *cosa) { printf("NO %s: %s\n", cosa, ex_zip_errore()); return 1; }
int main(int argc, char **argv)
{
    const char *d = argv[1];
    char a[512], f1[512], f2[512], f3[512];
    ExZip *z;
    int l;

    snprintf(a,  sizeof a,  "%s/a.zip", d);
    snprintf(f1, sizeof f1, "%s/src/uno.txt", d);
    snprintf(f2, sizeof f2, "%s/src/due.bin", d);
    snprintf(f3, sizeof f3, "%s/src/tre.txt", d);

    /* 1. crea, finisci, riapri, aggiungi, finisci */
    if (!(z = ex_zip_crea(a)))                    return no("crea");
    if (!ex_zip_aggiungi(z, f1, "uno.txt"))       return no("aggiungi uno");
    if (!ex_zip_aggiungi(z, f2, "cartella/due.bin")) return no("aggiungi due");
    if (!ex_zip_finisci(z))                       return no("finisci");
    ex_zip_chiudi(z);

    if (!(z = ex_zip_riapri(a)))                  return no("riapri");
    if (!ex_zip_aggiungi(z, f3, "tre.txt"))       return no("aggiungi tre");
    if (!ex_zip_finisci(z))                       return no("rifinisci");
    ex_zip_chiudi(z);

    /* 2. i livelli: lo stesso file, un archivio per livello */
    for (l = 0; l <= 3; l++) {
        char p[512];
        snprintf(p, sizeof p, "%s/liv%d.zip", d, l);
        ex_zip_livello((unsigned int)l);
        if (!(z = ex_zip_crea(p)) || !ex_zip_aggiungi(z, f1, "uno.txt") ||
            !ex_zip_finisci(z)) return no("livello");
        ex_zip_chiudi(z);
    }
    /* 3. trecento file: le tabelle crescono (partono da 64) */
    {
        char p[512], dir[512];
        int saltati = 0, n;
        snprintf(p, sizeof p, "%s/molti.zip", d);
        snprintf(dir, sizeof dir, "%s/src/molti", d);
        ex_zip_livello(2);
        if (!(z = ex_zip_crea(p))) return no("crea molti");
        n = ex_zip_aggiungi_albero(z, dir, "molti", &saltati);
        if (n != 300 || !ex_zip_finisci(z)) return no("albero di 300");
        ex_zip_chiudi(z);
        if (!(z = ex_zip_riapri(p)) || !ex_zip_aggiungi(z, f3, "tre.txt") ||
            !ex_zip_finisci(z)) return no("riapri molti");
        ex_zip_chiudi(z);
    }
    printf("FATTO\n");
    return 0;
}
EOF

python3 - "$D/src" <<'EOF'
import sys, random
d = sys.argv[1]
random.seed(7)
parole = "archivio catalogo voce deflate store riapri livello cartella file".split()
open(d + "/uno.txt", "w").write(" ".join(random.choice(parole) for _ in range(40000)))
open(d + "/due.bin", "wb").write(bytes(random.getrandbits(8) for _ in range(30000)))
open(d + "/tre.txt", "w").write("il terzo, aggiunto dopo\n" * 500)
import os
os.makedirs(d + "/molti")
for i in range(300):
    open(d + "/molti/f%03d.txt" % i, "w").write(("file %d\n" % i) * (i + 1))
EOF

if ! cc -Wall -O2 -o "$D/prova" -I "$D/shim" -I lib/exzip -I lib/eximg \
        "$D/prova.c" lib/exzip/exzip.c lib/exzip/deflate.c lib/eximg/inflate.c \
        > "$D/cc.log" 2>&1; then
    echo "NON RIUSCITO: non compila (vedi $D/cc.log)"; cat "$D/cc.log" | head -20; exit 1
fi
"$D/prova" "$D" || exit 1

esito=0
echo "=== 1. riaperto e aggiunto: chi non l'ha scritto lo apre? ==="
if python3 - "$D" <<'EOF'
import sys, zipfile
d = sys.argv[1]
z = zipfile.ZipFile(d + "/a.zip")
nomi = z.namelist()
print("        dentro:", " ".join(nomi))
assert nomi == ["uno.txt", "cartella/due.bin", "tre.txt"], nomi
assert z.testzip() is None
for n, f in (("uno.txt", "uno.txt"), ("cartella/due.bin", "due.bin"), ("tre.txt", "tre.txt")):
    assert z.read(n) == open(d + "/src/" + f, "rb").read(), n
EOF
then echo "  [OK]  python zipfile: tre voci, byte giusti, anche le due vecchie"
else echo "  [NO]  l'archivio riaperto non torna"; esito=1; fi

if command -v unzip > /dev/null; then
    if unzip -t "$D/a.zip" > "$D/unzip.log" 2>&1; then
        echo "  [OK]  unzip -t di Info-ZIP: nessun errore"
    else
        echo "  [NO]  unzip -t lo rifiuta (vedi $D/unzip.log)"; esito=1
    fi
fi

echo "=== 3. trecento file, riaperto: le tabelle crescono ==="
if python3 - "$D" <<'EOF'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1] + "/molti.zip")
n = z.namelist()
print("        voci:", len(n))
assert z.testzip() is None and len(n) == 302 and n[-1] == "tre.txt"
EOF
then echo "  [OK]  302 voci (cartella, 300 file, e quello aggiunto dopo), tutte buone"
else echo "  [NO]  le tabelle non crescono bene"; esito=1; fi

echo "=== 2. i livelli ==="
if python3 - "$D" <<'EOF'
import sys, zipfile
d = sys.argv[1]
r = []
for l in range(4):
    i = zipfile.ZipFile("%s/liv%d.zip" % (d, l)).getinfo("uno.txt")
    r.append((i.compress_type, i.compress_size, i.file_size))
    print("        livello %d: metodo %d, %d -> %d" % (l, i.compress_type, i.file_size, i.compress_size))
    assert zipfile.ZipFile("%s/liv%d.zip" % (d, l)).testzip() is None
assert r[0][0] == 0 and r[0][1] == r[0][2], "livello 0 non e' store"
assert all(x[0] == 8 for x in r[1:]), "1..3 non sono deflate"
assert r[3][1] <= r[2][1] <= r[1][1], "piu' livello, piu' grande?"
EOF
then echo "  [OK]  0 = store; 1, 2, 3 deflate, e piu' si cerca meno occupa"
else echo "  [NO]  i livelli non si comportano"; esito=1; fi

exit $esito

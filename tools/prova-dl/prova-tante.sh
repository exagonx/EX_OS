#!/bin/bash
# =============================================================================
# tools/prova-dl/prova-tante.sh - the loader has no fixed number of libraries
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova-dl/prova-tante.sh            64 bit (dist/exos64.iso)
#     tools/prova-dl/prova-tante.sh 32         32 bit (dist/exos.iso)
#
# (voce 85, 4 November 2026.) lib/libc_dl.c had 32 modules, 16 DT_NEEDED,
# 32 TLS modules and 256 thread blocks; VLC with Qt loads more. This links a
# program to N=40 libraries (N= changes it), each with a function and a
# __thread variable, and 8 threads read every one: 40 modules, 40 DT_NEEDED
# in the program, 40 TLS modules, 320 blocks. Then dlopen of one more,
# dlsym, dlclose. Against a libc.a built HERE from today's lib/libc.c
# (as tools/prova_dl.sh does), not the one installed in the toolchain.
# Must print "prova-tante: 0 NO".
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
N=${N:-40}
if [ "${1:-64}" = 32 ]; then
    T=i386-exos; ARCH=""
    export PATH="$RADICE/cross_build/$MACCHINA/exos-cross/bin:$PATH"
    CF="-m32 -march=pentium-mmx -mtune=pentium-mmx -fno-asynchronous-unwind-tables"
else
    T=x86_64-exos; ARCH=x86_64
    export PATH="$RADICE/cross_build/$MACCHINA/exos-cross64/bin:$PATH"
    CF="-m64"
fi
D="$RADICE/cross_build/$MACCHINA/costruzione-prova-tante-$T"
rm -rf "$D"; mkdir -p "$D/lib" "$D/so"
set -e
# the libc of the sysroot (EXOS_LIBC_PORTATI), the options of cross64.sh
gcc $CF -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -Wall -O2 -std=c11 \
    -ffunction-sections -fdata-sections -DEXOS_LIBC_PORTATI -Ilib/include -c lib/libc.c -o "$D/lib/libc.o"
ar rcs "$D/lib/libc.a" "$D/lib/libc.o"
LIBS=""; DECL=""; CHIAMA=""
for i in $(seq 1 $N); do
    printf '__thread int t_%d = %d;\nint f_%d(void) { return t_%d++; }\n' $i $i $i $i > "$D/so/l$i.c"
    $T-gcc -O2 -fPIC -shared -Wl,-soname,libl$i.so -o "$D/so/libl$i.so" "$D/so/l$i.c"
    LIBS="$LIBS -ll$i"; DECL="${DECL}int f_$i(void);"; CHIAMA="${CHIAMA}s += f_$i() == $i ? 0 : 1;"
done
printf 'int f_piu(void) { return 4242; }\n' > "$D/so/piu.c"
$T-gcc -O2 -fPIC -shared -Wl,-soname,libpiu.so -o "$D/so/libpiu.so" "$D/so/piu.c"
cat > "$D/prova-tante.c" <<C
#include <stdio.h>
#include <pthread.h>
#include <dlfcn.h>
$DECL
static int sbagli[8];
static void *filo(void *a) { int s = 0; $CHIAMA sbagli[(long)a] = s; return 0; }
int main(void)
{
    pthread_t f[8]; int i, no = 0, s = 0; void *h; int (*p)(void);
    for (i = 0; i < 8; i++) pthread_create(&f[i], 0, filo, (void *)(long)i);
    for (i = 0; i < 8; i++) { pthread_join(f[i], 0); s += sbagli[i]; }
    printf("  [%s] $N librerie, 8 fili, ogni __thread al suo valore (%d sbagli)\n", s ? "NO" : "OK", s); no += !!s;
    h = dlopen("/disk/libpiu.so", RTLD_NOW);
    p = h ? (int (*)(void))dlsym(h, "f_piu") : 0;
    printf("  [%s] dlopen della $((N + 1))a e dlsym\n", p && p() == 4242 ? "OK" : "NO"); no += !(p && p() == 4242);
    if (h) dlclose(h);
    printf("prova-tante: %d NO\n", no);
    return no;
}
C
$T-gcc -O2 -o "$D/prova-tante" "$D/prova-tante.c" -L"$D/lib" -L"$D/so" $LIBS -lpthread
set +e
n=$($T-readelf -d "$D/prova-tante" | grep -c NEEDED)
echo "  (il programma chiede $n librerie)"
uscita=$(EXOS_ARCH=$ARCH SECONDI=20 DISCO_MB=64 ALTRI="$(ls "$D"/so/*.so | tr '\n' ' ')" \
         AMBIENTE="LD_LIBRARY_PATH=/disk" tools/exilla/esegui-in-exos.sh "$D/prova-tante")
echo "$uscita" | grep -a -E "\[OK\]|\[NO\]|prova-tante|too many|FAULT|cannot"
echo "$uscita" | grep -aq "prova-tante: 0 NO" && { echo "=== [OK] ==="; exit 0; }
echo "=== [NO] ==="; exit 1

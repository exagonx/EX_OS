#!/bin/bash
# =============================================================================
# tools/costruisci-ld.sh - the shared libc /lib/ld-exos.so (voce 105)
#
#     tools/costruisci-ld.sh 32|64 <directory>
#
# Writes in <directory>:
#     ld-exos.so         the libc built -fPIC, also the program interpreter
#                        (lib/libc_ld.c): /lib/ld-exos.so on EX-OS
#     libexos_avvio.a    the program's start (lib/libc_avvio.c): main and the
#                        program's own constructors cannot be in a library;
#                        and, a member of its own, the hidden
#                        __stack_chk_fail_local of i386 (lib/libc_chk_locale.c)
#                        that libraries take too, without the start
#     libc.so            the linker script for -lc: GROUP of the two
#
# The same flags as the libc.a of the sysroot (EXOS_LIBC_PORTATI), plus -fPIC.
# ! -Bsymbolic-functions: the libc's calls to itself stay inside it, as in
# glibc and musl; its variables go through the GOT, so the program's COPY
# (environ, optarg, stdout...) is the one both use. Without it the first
# instruction of the interpreter would already need relocating.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
bit="$1"; D="$2"
[ -n "$D" ] && { [ "$bit" = 32 ] || [ "$bit" = 64 ]; } || { echo "uso: $0 32|64 <directory>" >&2; exit 2; }
mkdir -p "$D" || exit 1
if [ "$bit" = 64 ]; then
    m=-m64
    CF="-m64 -ffreestanding -fno-builtin -fno-stack-protector \
        -Wall -O2 -std=c11 -ffunction-sections -fdata-sections -DEXOS_LIBC_PORTATI"
else
    m=-m32
    CF="-m32 -march=pentium-mmx -mtune=pentium-mmx -ffreestanding -fno-builtin \
        -fno-stack-protector -fno-asynchronous-unwind-tables \
        -Wall -O2 -std=c11 -ffunction-sections -fdata-sections -DEXOS_LIBC_PORTATI"
fi
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
gcc $CF -fPIC -DEXOS_LIBC_ELF -Ilib/include -c lib/libc.c -o "$T/libc.o" 2> "$T/cc.log" ||
    { cat "$T/cc.log" >&2; echo "[NO] lib/libc.c -fPIC a $bit bit" >&2; exit 1; }
gcc $m -shared -nostdlib -Wl,-soname,ld-exos.so -Wl,-Bsymbolic-functions -Wl,-e,_ld_avvio \
    -Wl,--hash-style=both -Wl,-z,noexecstack "$T/libc.o" -lgcc -o "$T/ld-exos.so" ||
    { echo "[NO] il collegamento di ld-exos.so a $bit bit" >&2; exit 1; }
# ! NOTHING UNDEFINED but the two weak names of the old export table
u=$(nm -D --undefined-only "$T/ld-exos.so" | awk '$1 != "w" {print $2}')
[ -z "$u" ] || { echo "[NO] ld-exos.so a $bit bit chiede simboli che non ha: $u" >&2; exit 1; }
# ! and no PLT: before relocating itself it could not jump through one
readelf -rW "$T/ld-exos.so" | grep -q JUMP_SLOT &&
    { echo "[NO] ld-exos.so a $bit bit ha una PLT" >&2; exit 1; }
gcc $CF -fno-pic -fno-pie -DEXOS_AVVIO_PROGRAMMA -c lib/libc_avvio.c -o "$T/avvio.o" 2> /dev/null ||
    { echo "[NO] lib/libc_avvio.c a $bit bit" >&2; exit 1; }
gcc $CF -fPIC -c lib/libc_chk_locale.c -o "$T/chk_locale.o" 2> /dev/null ||
    { echo "[NO] lib/libc_chk_locale.c a $bit bit" >&2; exit 1; }
rm -f "$D/libexos_avvio.a"
ar rcs "$D/libexos_avvio.a" "$T/avvio.o" "$T/chk_locale.o" || exit 1
cp "$T/ld-exos.so" "$D/ld-exos.so" || exit 1
printf '/* -lc: the shared libc /lib/ld-exos.so and the program start (voce 105) */\nGROUP ( ld-exos.so libexos_avvio.a )\n' > "$D/libc.so"
echo "[OK] ld-exos.so a $bit bit: $(wc -c < "$D/ld-exos.so") byte, in $D"

#!/bin/sh
# =============================================================================
# tools/gcc-exos/specs-condivise.sh — `gcc -shared` for EX-OS (14 October 2026)
#
#     tools/gcc-exos/specs-condivise.sh <path of the cross or native gcc>
#
# The EX-OS targets (exos64.h, and the i386 one) refuse -shared in their
# built-in link spec: until lib/libc_dl.c there was nothing to load a shared
# object. GCC reads a file called `specs` in its library directory and lets
# it replace the built-in specs, so this writes one there instead of
# rebuilding the compiler:
#
#   with -shared     ld -shared, no crt0/crtbegin/crtend and no -lc: the
#                    library takes the libc from the program that opens it
#                    (lib/libc_esportati.h), so it must not carry a copy;
#                    crtbeginS.o first and crtendS.o last, built here from
#                    tools/gcc-exos/crtstuff-condivise.c: they register the
#                    library's unwind tables (16 October 2026, see there);
#   without          the built-in spec, but -static only when asked for
#                    (3 November 2026): `gcc prog.c -lz` links libz.so if the
#                    sysroot has it, and the program loads it at start
#                    (lib/libc_dl.c, __exos_dl_avvio_programma). With no .so
#                    in the link ld still makes a static program, as before.
#                    PT_INTERP says /lib/ld-exos.so: the kernel ignores it,
#                    the loader is the libc inside the program. Programs that
#                    must not depend on /lib (the boot) pass -static.
#   libgcc           if the sysroot has libgcc_s.so (4 November 2026, made by
#                    tools/librerie-exos/cxx-condivise.sh): a link that is not
#                    -static takes the exception unwinder from libgcc_s.so.1,
#                    --as-needed, so every C++ library shares one unwinder.
#
# ! COMPILE THE LIBRARY WITH -fPIC. Without it the objects carry absolute
# relocations (R_X86_64_32) that a shared object cannot have.
# =============================================================================
GCC="${1:?usage: $0 <gcc>}"
DIR=$("$GCC" -print-libgcc-file-name | xargs dirname)
[ -d "$DIR" ] || { echo "specs-condivise: no library directory for $GCC" >&2; exit 1; }
rm -f "$DIR/specs"
# libgcc_s.so in the sysroot (4 November 2026, voce 85): see the libgcc spec below
GCC_S=no
[ -f "$("$GCC" -print-sysroot 2>/dev/null)/lib/libgcc_s.so" ] && GCC_S=si
[ -f "$DIR/../../../../$("$GCC" -dumpmachine)/lib/libgcc_s.so" ] && GCC_S=si
# crtbeginS.o / crtendS.o: compiled by this very gcc (its -m and ABI), PIC.
SORG="$(cd "$(dirname "$0")" && pwd)/crtstuff-condivise.c"
"$GCC" -O2 -fPIC -fno-asynchronous-unwind-tables -DINIZIO -c "$SORG" -o "$DIR/crtbeginS.o" &&
"$GCC" -O2 -fPIC -fno-asynchronous-unwind-tables -DFINE -c "$SORG" -o "$DIR/crtendS.o" ||
    { echo "specs-condivise: crtbeginS.o/crtendS.o did not compile" >&2; exit 1; }
"$GCC" -dumpspecs | python3 -c '
import re, sys
t = sys.stdin.read()
def sezione(nome, fn):
    global t
    m = re.search(r"(\*" + nome + r":\n)(.*?)\n", t)
    if not m: sys.exit("specs-condivise: no *" + nome)
    t = t[:m.start(2)] + fn(m.group(2)) + t[m.end(2):]
def link(v):
    v = re.sub(r"\s*%\{shared:%e[^}]*\}", "", v)
    emu = re.match(r"(-m \S+)", v).group(1)
    # a program: static only with -static; otherwise ld links the .so it
    # finds, and with none it still makes a static program
    v = v.replace("-static", "%{static:-static} %{!static:-dynamic-linker /lib/ld-exos.so}")
    return "%{shared:" + emu + " -shared} %{!shared:" + v.strip() + "}"
sezione("link", link)
sezione("startfile", lambda v: "%{shared:crtbeginS.o%s} %{!shared:" + v + "}")
sezione("endfile", lambda v: "%{shared:crtendS.o%s} %{!shared:" + v + "}")
sezione("lib", lambda v: "%{!shared:" + v + "}")
# the shared unwinder (tools/librerie-exos/cxx-condivise.sh), where the
# sysroot has it: not with -static or -static-libgcc; --as-needed, so a
# program or library that throws nothing gets no DT_NEEDED
if sys.argv[1] == "si":
    sezione("libgcc", lambda v: "%{static|static-libgcc:" + v + "} %{!static:%{!static-libgcc:"
            "--push-state --as-needed -lgcc_s --pop-state " + v + "}}")
sys.stdout.write(t)
' "$GCC_S" > "$DIR/specs" || exit 1
echo "specs-condivise: $DIR/specs (-shared allowed)"

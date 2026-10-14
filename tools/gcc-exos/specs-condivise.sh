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
#   without          exactly the built-in spec.
#
# ! COMPILE THE LIBRARY WITH -fPIC. Without it the objects carry absolute
# relocations (R_X86_64_32) that a shared object cannot have.
# =============================================================================
GCC="${1:?usage: $0 <gcc>}"
DIR=$("$GCC" -print-libgcc-file-name | xargs dirname)
[ -d "$DIR" ] || { echo "specs-condivise: no library directory for $GCC" >&2; exit 1; }
rm -f "$DIR/specs"
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
    return "%{shared:" + emu + " -shared} %{!shared:" + v.strip() + "}"
sezione("link", link)
sezione("startfile", lambda v: "%{!shared:" + v + "}")
sezione("endfile", lambda v: "%{!shared:" + v + "}")
sezione("lib", lambda v: "%{!shared:" + v + "}")
sys.stdout.write(t)
' > "$DIR/specs" || exit 1
echo "specs-condivise: $DIR/specs (-shared allowed)"

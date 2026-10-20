#!/usr/bin/env python3
# =============================================================================
# tools/libc-esportati.py — the libc's symbols for libraries opened by dlopen
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/libc-esportati.py          writes lib/libc_esportati.h
#
# A shared object opened by dlopen (lib/libc_dl.c) needs malloc, printf...
# from the program, and an EX-OS program is static: no symbol table at run
# time. So the libc carries one: every global symbol of lib/libc.c, as name
# and address. This script compiles the libc the four ways it is built and
# writes the four lists:
#
#     64 bit, ported software   (tools/gcc-exos/cross64.sh, -DEXOS_LIBC_PORTATI)
#     64 bit, the system        (tools/costruisci-utente64.sh)
#     32 bit, ported software   (tools/gcc-exos/prepara-cross.sh)
#     32 bit, the system        (the Makefile's static libc.a)
#
# The table is written in ASSEMBLY, not in C: an entry is the symbol's name
# for the assembler, so no C declaration is needed and a function written in
# inline assembly (setjmp) is listed like any other.
#
# ! RUN IT AGAIN WHEN A FUNCTION IS ADDED TO THE LIBC. A name that is no
# longer there stops the build (undefined reference) - which is the check; a
# new one that is not listed is simply not offered to libraries.
# =============================================================================
import os
import re
import subprocess
import sys
import tempfile

RADICE = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
BASE = "-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -O2 -std=c11 -w"
CONFIG = [
    ("defined(__x86_64__) && defined(EXOS_LIBC_PORTATI)", "-m64 -DEXOS_LIBC_PORTATI", ".quad", 8),
    ("defined(__x86_64__)",                                "-m64", ".quad", 8),
    ("defined(EXOS_LIBC_PORTATI)",                         "-m32 -march=pentium-mmx -DEXOS_LIBC_PORTATI", ".long", 4),
    ("1",                                                  "-m32 -march=pentium-mmx", ".long", 4),
]


def simboli(flag):
    with tempfile.TemporaryDirectory() as d:
        o = os.path.join(d, "libc.o")
        r = subprocess.run(["gcc"] + BASE.split() + flag.split() +
                           ["-DEXOS_DL_SENZA_TABELLA", "-I", os.path.join(RADICE, "lib", "include"),
                            "-c", os.path.join(RADICE, "lib", "libc.c"), "-o", o],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"libc-esportati: the libc does not compile ({flag}):\n{r.stderr[-2000:]}")
        nm = subprocess.run(["nm", "-g", "--defined-only", o], capture_output=True, text=True).stdout
    nomi = set()
    for riga in nm.splitlines():
        p = riga.split()
        if len(p) == 3 and p[1] in "TWDBRVi" and not p[2].startswith("g_dl_libc"):
            nomi.add(p[2])
    return sorted(nomi)


def blocco(nomi, parola, alli):
    righe = ['__asm__(".pushsection .rodata.g_dl_libc_str,\\"aMS\\",@progbits,1\\n"']
    for i, n in enumerate(nomi):
        righe.append(f'        ".Ldl_n{i}: .asciz \\"{n}\\"\\n"')
    righe.append(f'        ".popsection\\n"')
    righe.append(f'        ".pushsection .rodata.g_dl_libc,\\"a\\"\\n"')
    righe.append(f'        ".balign {alli}\\n"')
    righe.append(f'        "g_dl_libc:\\n"')
    for i, n in enumerate(nomi):
        righe.append(f'        "{parola} .Ldl_n{i}, {n}\\n"')
    righe.append(f'        "g_dl_libc_n: {parola} {len(nomi)}\\n"')
    righe.append(f'        ".popsection\\n");')
    return "\n".join(righe)


def main():
    out = ["/* lib/libc_esportati.h - WRITTEN BY tools/libc-esportati.py, do not edit.",
           " * Every global symbol of lib/libc.c, as name and address, for the shared",
           " * objects lib/libc_dl.c loads. Four lists: the libc is built four ways.",
           " * Included by lib/libc_dl.c only. */",
           "extern const DlEsportato g_dl_libc[];",
           "extern const unsigned long g_dl_libc_n;",
           "#if defined(EXOS_DL_SENZA_TABELLA)",
           '__asm__(".pushsection .rodata.g_dl_libc,\\"a\\"\\n.balign 8\\ng_dl_libc:\\n'
           'g_dl_libc_n: " DL_PAROLA " 0\\n.popsection\\n");']
    for cond, flag, parola, alli in CONFIG:
        nomi = simboli(flag)
        print(f"{flag:45s} {len(nomi)} symbols")
        out.append(f"#elif {cond}")
        out.append(blocco(nomi, parola, alli))
    out.append("#endif")
    open(os.path.join(RADICE, "lib", "libc_esportati.h"), "w").write("\n".join(out) + "\n")
    print("written lib/libc_esportati.h")


if __name__ == "__main__":
    main()

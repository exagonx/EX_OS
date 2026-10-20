#!/usr/bin/env python3
# =============================================================================
# tools/rust-exos/std/verifica-abi.py — Rust's libc structs against the C ones
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/rust-exos/std/verifica-abi.py           write abi-exos.rs
#
# libc-exos.rs repeats in Rust the structs of lib/include. A field out of
# place links fine and reads garbage, so this script asks the TARGET C
# compilers (i386-exos-gcc and x86_64-exos-gcc) for the size, the alignment
# and every public field's offset of each struct in libc-exos.rs, and writes
# them as compile-time assertions in abi-exos.rs:
#
#     #[cfg(target_pointer_width = "64")]
#     const _: () = { assert!(size_of::<stat>() == 72); ... };
#
# (core::mem::offset_of! by its full path: the libc crate has an offset_of
# macro of its own, and the short name is ambiguous there.)
#
# exos_libc.aggiungi_libc() appends that file to the crate's module, so a
# mismatch STOPS the Rust build instead of reaching a program.
#
# The numbers are read from the compiler's assembly (-S), not from running a
# program: the compilers build for EX-OS, and nothing here runs on the host.
#
# ! A struct that lib/include does not have (statvfs: EX-OS has no statvfs())
# is listed and skipped: Rust has it only because the crate names it.
# =============================================================================
import os
import re
import subprocess
import sys
import tempfile

QUI = os.path.dirname(os.path.abspath(__file__))
RADICE = os.path.abspath(os.path.join(QUI, "..", "..", ".."))
CROSS = os.path.join(RADICE, "cross_build")
MACCHINA = os.environ.get("MACCHINA", os.uname().nodename.lower())


def compilatore(cartella, nome):
    # ! THIS MACHINE'S COMPILER FIRST (cross_build/<machine>/...): the shared
    # one may have been built on the other PC against a newer glibc, and then
    # it does not even start here ("GLIBC_2.38 not found").
    for c in (os.path.join(CROSS, MACCHINA, cartella, "bin", nome),
              os.path.join(CROSS, cartella, "bin", nome)):
        if os.access(c, os.X_OK) and subprocess.run([c, "--version"], capture_output=True).returncode == 0:
            return c
    return os.path.join(CROSS, cartella, "bin", nome)


COMPILATORI = {
    "32": compilatore("exos-cross", "i386-exos-gcc"),
    "64": compilatore("exos-cross64", "x86_64-exos-gcc"),
}
INTESTAZIONI = ["libc.h", "pthread.h", "semaphore.h", "dirent.h", "time.h", "poll.h",
                "pwd.h", "signal.h", "termios.h", "locale.h", "dlfcn.h",
                "sys/socket.h", "netinet/in.h", "netdb.h", "sys/un.h", "sys/stat.h"]
# Rust name -> how C spells the type, where it is not `struct <name>`
TIPO_C = {"Dl_info": "Dl_info", "fd_set": "fd_set", "sem_t": "sem_t"}


def strutture():
    """(name, [public fields]) of every struct in the s! block of libc-exos.rs"""
    t = open(os.path.join(QUI, "libc-exos.rs"), encoding="utf-8").read()
    fuori = []
    for m in re.finditer(r"pub struct (\w+) \{(.*?)\n    \}", t, re.S):
        campi = re.findall(r"^\s*pub (\w+):", m.group(2), re.M)
        fuori.append((m.group(1), campi))
    return fuori


def tipo_c(nome):
    if nome in TIPO_C or nome.endswith("_t"):
        return TIPO_C.get(nome, nome)
    return "struct " + nome


def misura(cc, strutt):
    """{name: (size, align, {field: offset})}, and the names C does not have"""
    inc = "".join(f'#include "{h}"\n' for h in INTESTAZIONI
                  if os.path.exists(os.path.join(RADICE, "lib", "include", h)))
    buone, mancano = [], []
    # one struct at a time first, to tell "C has no such struct" from a typo
    for nome, campi in strutt:
        prova = inc + f"unsigned long x = sizeof({tipo_c(nome)});\n"
        prova += "".join(f"unsigned long y{i} = __builtin_offsetof({tipo_c(nome)}, {c});\n"
                         for i, c in enumerate(campi))
        if compila(cc, prova) is None:
            if compila(cc, inc + f"unsigned long x = sizeof({tipo_c(nome)});\n") is None:
                mancano.append(nome)
                continue
            sys.exit(f"verifica-abi: {nome}: a field of libc-exos.rs is not in the C struct "
                     f"(compile with {os.path.basename(cc)} to see which)")
        buone.append((nome, campi))
    valori = []
    for nome, campi in buone:
        t = tipo_c(nome)
        valori += [f"sizeof({t})", f"_Alignof({t})"] + [f"__builtin_offsetof({t}, {c})" for c in campi]
    asm = compila(cc, inc + "const unsigned long long abi_valori[] = {\n    "
                  + ",\n    ".join(valori) + "\n};\n")
    if asm is None:
        sys.exit("verifica-abi: the table does not compile")
    numeri = [int(n, 0) for n in re.findall(r"\.(?:quad|long|value|byte)\s+(-?\w+)", asm)]
    if len(numeri) < len(valori):
        sys.exit(f"verifica-abi: read {len(numeri)} numbers, expected {len(valori)}")
    fuori, k = {}, 0
    for nome, campi in buone:
        dim, ali = numeri[k], numeri[k + 1]
        k += 2
        off = {}
        for c in campi:
            off[c] = numeri[k]
            k += 1
        fuori[nome] = (dim, ali, off)
    return fuori, mancano


def compila(cc, testo):
    with tempfile.TemporaryDirectory() as d:
        sorg = os.path.join(d, "a.c")
        open(sorg, "w").write(testo)
        r = subprocess.run([cc, "-S", "-O0", "-ffreestanding", "-std=gnu11",
                            "-I", os.path.join(RADICE, "lib", "include"), sorg,
                            "-o", os.path.join(d, "a.s")],
                           capture_output=True, text=True)
        if r.returncode != 0:
            if os.environ.get("VERIFICA_ABI_DICE"):
                print(r.stderr[-3000:])
            return None
        # a .quad of a 32-bit compiler is two .long: ask for 64-bit words only
        # where the target has them; at 32 bit the array is still `long long`,
        # emitted as .long pairs (low, high) - fold them back
        asm = open(os.path.join(d, "a.s")).read()
    if ".quad" not in asm:
        pezzi = re.findall(r"\.long\s+(-?\w+)", asm)
        coppie = [str(int(pezzi[i], 0) + (int(pezzi[i + 1], 0) << 32)) for i in range(0, len(pezzi) - 1, 2)]
        return "\n".join("\t.quad " + p for p in coppie)
    return asm


def main():
    strutt = strutture()
    righe = ["// tools/rust-exos/std/abi-exos.rs - WRITTEN BY verifica-abi.py, do not edit.",
             "// The size, alignment and field offsets of each struct of libc-exos.rs as",
             "// the EX-OS C compilers see them (lib/include). A mismatch stops the build.",
             "// Appended to the libc crate's EX-OS module by exos_libc.aggiungi_libc().", ""]
    for bit, cc in COMPILATORI.items():
        if not os.access(cc, os.X_OK):
            sys.exit(f"verifica-abi: {cc} is missing (tools/gcc-exos)")
        m, mancano = misura(cc, strutt)
        print(f"{bit} bit: {len(m)} structs checked; not in lib/include: {', '.join(mancano) or 'none'}")
        righe.append(f'#[cfg(target_pointer_width = "{bit}")]')
        righe.append("const _: () = {")
        righe.append("    use core::mem::{align_of, size_of};")
        for nome, (dim, ali, off) in m.items():
            righe.append(f"    assert!(size_of::<{nome}>() == {dim}, \"{nome}: size differs from C\");")
            righe.append(f"    assert!(align_of::<{nome}>() == {ali}, \"{nome}: alignment differs from C\");")
            for c, o in off.items():
                righe.append(f"    assert!(core::mem::offset_of!({nome}, {c}) == {o}, \"{nome}.{c}: offset differs from C\");")
        righe.append("};")
        righe.append("")
    open(os.path.join(QUI, "abi-exos.rs"), "w", encoding="utf-8").write("\n".join(righe))
    print("written tools/rust-exos/std/abi-exos.rs")


if __name__ == "__main__":
    main()

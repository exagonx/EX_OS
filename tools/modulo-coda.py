#!/usr/bin/env python3
# =============================================================================
# tools/modulo-coda.py — turns a linked module into a kernel module file
#
#     tools/modulo-coda.py IN.so OUT.mod VERSIONE ARCH
#
# Checks what kernel/loader/modulo.c will check anyway - an ET_DYN of the
# right class with only RELATIVE relocations - so a module the kernel would
# refuse fails the BUILD, not the boot. Then appends the 64-byte trailer:
# "EXOSMOD1", the kernel version (16 bytes), the architecture (8 bytes),
# and the SHA-256 of everything before it. See kernel/include/modulo.h.
#
# Standard library only.
# =============================================================================
import hashlib
import struct
import sys


def errore(msg):
    sys.stderr.write("modulo-coda: %s\n" % msg)
    sys.exit(1)


def controlla(dati, arch):
    if dati[:4] != b"\x7fELF":
        errore("non e' ELF")
    classe = dati[4]
    attesa = 2 if arch == "x86_64" else 1
    if classe != attesa:
        errore("ELF%d per l'architettura %s" % (32 * classe, arch))
    if classe == 1:
        e_type, e_machine = struct.unpack_from("<HH", dati, 16)
        e_phoff, = struct.unpack_from("<I", dati, 28)
        e_shoff, = struct.unpack_from("<I", dati, 32)
        e_shentsize, e_shnum = struct.unpack_from("<HH", dati, 46)
    else:
        e_type, e_machine = struct.unpack_from("<HH", dati, 16)
        e_shoff, = struct.unpack_from("<Q", dati, 40)
        e_shentsize, e_shnum = struct.unpack_from("<HH", dati, 58)
    if e_type != 3:
        errore("non e' ET_DYN (oggetto condiviso)")
    if e_machine != (62 if classe == 2 else 3):
        errore("macchina %d" % e_machine)
    # The relocations, from the section headers: only RELATIVE (8 on both).
    n_rel = 0
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        if classe == 1:
            sh_type, = struct.unpack_from("<I", dati, o + 4)
            sh_offset, sh_size, _, _, _, sh_entsize = struct.unpack_from("<IIIIII", dati, o + 16)
        else:
            sh_type, = struct.unpack_from("<I", dati, o + 4)
            sh_offset, sh_size = struct.unpack_from("<QQ", dati, o + 24)
            sh_entsize, = struct.unpack_from("<Q", dati, o + 56)
        if sh_type == 11:  # SHT_DYNSYM
            # an undefined symbol other than the null one means the module
            # calls something by name
            ent = 16 if classe == 1 else 24
            for k in range(1, sh_size // ent):
                b = sh_offset + k * ent
                shndx, = struct.unpack_from("<H", dati, b + (14 if classe == 1 else 6))
                if shndx == 0:
                    errore("simbolo non definito nella tabella dinamica")
        if sh_type not in (4, 9):  # SHT_RELA, SHT_REL
            continue
        if sh_entsize == 0:
            continue
        for k in range(sh_size // sh_entsize):
            b = sh_offset + k * sh_entsize
            if classe == 1:
                _, info = struct.unpack_from("<II", dati, b)
                tipo = info & 0xFF
            else:
                _, info = struct.unpack_from("<QQ", dati, b)
                tipo = info & 0xFFFFFFFF
            if tipo != 8:
                errore("rilocazione di tipo %d (solo RELATIVE e' ammessa)" % tipo)
            n_rel += 1
    return n_rel


def main():
    if len(sys.argv) != 5:
        errore("uso: modulo-coda.py IN.so OUT.mod VERSIONE ARCH")
    ingresso, uscita, versione, arch = sys.argv[1:]
    dati = open(ingresso, "rb").read()
    n_rel = controlla(dati, arch)
    v = versione.encode("ascii")
    a = arch.encode("ascii")
    if len(v) > 15 or len(a) > 7:
        errore("versione o architettura troppo lunga")
    corpo = dati + b"EXOSMOD1" + v.ljust(16, b"\0") + a.ljust(8, b"\0")
    corpo += hashlib.sha256(corpo).digest()
    open(uscita, "wb").write(corpo)
    print("[OK] %s: %d byte, %d rilocazioni RELATIVE, kernel %s %s"
          % (uscita, len(corpo), n_rel, versione, arch))


main()

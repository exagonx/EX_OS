#!/usr/bin/env python3
# =============================================================================
# tools/lz4exos.py — compress a file for EX-OS: the 'EXLZ' format (@RDBOOT 1b)
#
#     tools/lz4exos.py IN OUT        compress, then decompress and compare
#     tools/lz4exos.py -d IN OUT     decompress (to check a file by hand)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The format, the contract with kernel/boot/rdlz4.c:
#     +0   'EXLZ'
#     +4   bytes of the original, 32 bit little endian
#     +8   bytes of the compressed block that follows
#     +12  sum of the original's bytes, 32 bit wrapping (LZ4 alone does
#          not notice a literal byte changed on the way: this does)
#     +16  one LZ4 BLOCK (the documented block format, not the frame)
#
# ! WRITTEN HERE AND NOT TAKEN FROM A PACKAGE: the build machine has neither
# the lz4 command nor the Python module, and the system must build from what
# a plain Debian has. The compressor is greedy with one hash table - slower
# and a little worse than the reference one, and that is fine: it runs once
# per build, and the decoder is the same.
#
# ! IT CHECKS ITS OWN WORK: every file it writes is decompressed again here
# with an independent decoder and compared byte for byte. A compressor bug
# would otherwise be found only by a machine that does not boot.
# =============================================================================
import struct
import sys

MINMATCH = 4
LASTLITERALS = 5       # the block format wants the last 5 bytes as literals
MFLIMIT = 12           # and no match starting in the last 12
MAXOFF = 65535


def _lunghezza(out, n):
    while n >= 255:
        out.append(255)
        n -= 255
    out.append(n)


def _sequenza(out, lit, mlen, off):
    tl = min(len(lit), 15)
    tm = 0 if mlen is None else min(mlen - MINMATCH, 15)
    out.append((tl << 4) | tm)
    if len(lit) >= 15:
        _lunghezza(out, len(lit) - 15)
    out += lit
    if mlen is not None:
        out += struct.pack('<H', off)
        if mlen - MINMATCH >= 15:
            _lunghezza(out, mlen - MINMATCH - 15)


def comprimi(d):
    n = len(d)
    out = bytearray()
    tab = {}
    anchor = 0
    i = 0
    limite = n - MFLIMIT
    while i < limite:
        chiave = d[i:i + 4]
        cand = tab.get(chiave)
        tab[chiave] = i
        if cand is None or i - cand > MAXOFF:
            i += 1
            continue
        # extend the match: 64 bytes at a time, then byte by byte
        fine = n - LASTLITERALS
        l = MINMATCH
        while i + l + 64 <= fine and d[cand + l:cand + l + 64] == d[i + l:i + l + 64]:
            l += 64
        while i + l < fine and d[cand + l] == d[i + l]:
            l += 1
        _sequenza(out, d[anchor:i], l, i - cand)
        # a few positions inside the match go into the table too
        for k in range(i + 1, min(i + l, limite), max(1, l // 8)):
            tab[d[k:k + 4]] = k
        i += l
        anchor = i
    _sequenza(out, d[anchor:], None, 0)
    return bytes(out)


def decomprimi(s, attesi):
    out = bytearray()
    i = 0
    while True:
        tok = s[i]; i += 1
        lit = tok >> 4
        if lit == 15:
            while True:
                b = s[i]; i += 1
                lit += b
                if b != 255:
                    break
        out += s[i:i + lit]; i += lit
        if i == len(s):
            break
        off = s[i] | (s[i + 1] << 8); i += 2
        if off == 0 or off > len(out):
            raise ValueError("distanza fuori dal gia' scritto")
        ml = tok & 15
        if ml == 15:
            while True:
                b = s[i]; i += 1
                ml += b
                if b != 255:
                    break
        ml += MINMATCH
        for _ in range(ml):
            out.append(out[-off])
    if len(out) != attesi:
        raise ValueError("lunghezza %d invece di %d" % (len(out), attesi))
    return bytes(out)


def main():
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == '-d':
        s = open(a[1], 'rb').read()
        if s[:4] != b'EXLZ':
            sys.exit("non e' un file EXLZ")
        orig, comp = struct.unpack('<II', s[4:12])
        open(a[2], 'wb').write(decomprimi(s[16:16 + comp], orig))
        return
    if len(a) != 2:
        sys.exit("uso: lz4exos.py IN OUT  |  lz4exos.py -d IN OUT")
    d = open(a[0], 'rb').read()
    c = comprimi(d)
    if decomprimi(c, len(d)) != d:
        sys.exit("lz4exos: la decompressione non ridà l'originale - NON scritto")
    somma = sum(d) & 0xFFFFFFFF
    open(a[1], 'wb').write(b'EXLZ' + struct.pack('<III', len(d), len(c), somma) + c)
    print("lz4exos: %d -> %d byte (%d%%)" % (len(d), len(c) + 16, (len(c) + 16) * 100 // max(1, len(d))))


if __name__ == '__main__':
    main()

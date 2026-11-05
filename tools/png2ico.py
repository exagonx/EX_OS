#!/usr/bin/env python3
# =============================================================================
# tools/png2ico.py - an RGBA PNG to the .ico of ExWin (voce 79)
#
#     python3 tools/png2ico.py ingresso.png uscita.ico LATO
#
# The icons of ExWin are ICO files with ONE image, a 40-byte BITMAPINFOHEADER
# (height doubled, as ICO wants), 32 bits per pixel BGRA bottom-up, and no AND
# mask: the alpha says it (see exwin/icon/baseapp/*_64.ico, 16424 bytes for
# 64x64). The PNG must be RGBA, 8 bits, not interlaced, and its side a
# multiple of LATO: it is reduced by averaging square blocks, with the colour
# weighted by the alpha so that the transparent edge does not darken it.
# Pure Python (no PIL on the build machine).
# =============================================================================
import struct
import sys
import zlib


def leggi_png(nome):
    d = open(nome, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("%s: non e' un PNG" % nome)
    i, idat, w = 8, b"", None
    while i < len(d):
        n, t = struct.unpack(">I4s", d[i:i + 8])
        c = d[i + 8:i + 8 + n]
        if t == b"IHDR":
            w, h, bit, tipo, _, _, interl = struct.unpack(">IIBBBBB", c)
            if bit != 8 or tipo != 6 or interl:
                sys.exit("%s: serve RGBA a 8 bit non interlacciato" % nome)
        elif t == b"IDAT":
            idat += c
        i += 12 + n
    raw, bpp, stride = zlib.decompress(idat), 4, w * 4
    out, prima, p = bytearray(), bytearray(stride), 0
    for _ in range(h):
        f, riga = raw[p], bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for x in range(stride):
            a = riga[x - bpp] if x >= bpp else 0
            b = prima[x]
            c = prima[x - bpp] if x >= bpp else 0
            if f == 1:
                riga[x] = (riga[x] + a) & 255
            elif f == 2:
                riga[x] = (riga[x] + b) & 255
            elif f == 3:
                riga[x] = (riga[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                riga[x] = (riga[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += riga
        prima = riga
    return w, h, out


def riduci(w, h, px, lato):
    if w != h or w % lato:
        sys.exit("il lato %d non divide %dx%d" % (lato, w, h))
    k, res = w // lato, bytearray()
    for y in range(lato):
        for x in range(lato):
            r = g = b = a = 0
            for yy in range(y * k, y * k + k):
                for xx in range(x * k, x * k + k):
                    o = (yy * w + xx) * 4
                    al = px[o + 3]
                    r += px[o] * al; g += px[o + 1] * al; b += px[o + 2] * al; a += al
            if a:
                res += bytes((r // a, g // a, b // a, a // (k * k)))
            else:
                res += b"\0\0\0\0"
    return res


def main():
    if len(sys.argv) != 4:
        sys.exit("uso: png2ico.py ingresso.png uscita.ico LATO")
    w, h, px = leggi_png(sys.argv[1])
    lato = int(sys.argv[3])
    px = riduci(w, h, px, lato)
    bmp = bytearray()
    for y in range(lato - 1, -1, -1):           # bottom-up
        for x in range(lato):
            o = (y * lato + x) * 4
            bmp += bytes((px[o + 2], px[o + 1], px[o], px[o + 3]))
    testa = struct.pack("<IiiHHIIiiII", 40, lato, lato * 2, 1, 32, 0, len(bmp), 3780, 3780, 0, 0)
    dati = testa + bmp
    ico = struct.pack("<HHH", 0, 1, 1)
    ico += struct.pack("<BBBBHHII", lato & 255, lato & 255, 0, 0, 1, 32, len(dati), 22)
    open(sys.argv[2], "wb").write(ico + dati)


main()

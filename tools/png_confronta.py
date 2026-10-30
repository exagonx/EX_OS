#!/usr/bin/env python3
# tools/png_confronta.py FILE.png [FOTO.ppm]
#
# Decodes a PNG with the standard library alone (no ImageMagick needed) and
# prints "LxA" and, with a P6 picture from QEMU, how many pixels differ.
# Non-interlaced, 8 bits, RGB or RGBA: what lib/eximg/scrivi.c writes.
import struct, sys, zlib

def png(path):
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit("non e' un PNG: %s" % path)
    p, z = 8, b''
    while p < len(d):
        n, t = struct.unpack('>I4s', d[p:p+8])
        c = d[p+8:p+8+n]
        if struct.unpack('>I', d[p+8+n:p+12+n])[0] != zlib.crc32(t + c):
            sys.exit('CRC sbagliato in %s' % t)
        if t == b'IHDR':
            w, h, bit, tipo, _, _, il = struct.unpack('>IIBBBBB', c)
        elif t == b'IDAT':
            z += c
        p += 12 + n
    if bit != 8 or il or tipo not in (2, 6):
        sys.exit('formato non previsto: %d %d %d' % (bit, tipo, il))
    bpp = 3 if tipo == 2 else 4
    raw, righe, prec, r = zlib.decompress(z), [], bytearray(w * bpp), 0
    for y in range(h):
        f, s = raw[r], bytearray(raw[r+1:r+1+w*bpp])
        r += 1 + w * bpp
        for i in range(len(s)):
            a = s[i-bpp] if i >= bpp else 0
            b = prec[i]
            c = prec[i-bpp] if i >= bpp else 0
            if f == 1: s[i] = (s[i] + a) & 255
            elif f == 2: s[i] = (s[i] + b) & 255
            elif f == 3: s[i] = (s[i] + (a + b) // 2) & 255
            elif f == 4:
                q = a + b - c
                pa, pb, pc = abs(q - a), abs(q - b), abs(q - c)
                s[i] = (s[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        righe.append(bytes(s) if bpp == 3 else bytes(v for k, v in enumerate(s) if k % 4 != 3))
        prec = s
    return w, h, righe

def ppm(path):
    d = open(path, 'rb').read()
    campi, p = [], 0
    while len(campi) < 4:
        while d[p:p+1].isspace(): p += 1
        q = p
        while not d[p:p+1].isspace(): p += 1
        campi.append(d[q:p])
    p += 1
    w, h = int(campi[1]), int(campi[2])
    return w, h, [d[p+y*w*3:p+(y+1)*w*3] for y in range(h)]

w, h, a = png(sys.argv[1])
print('%dx%d' % (w, h), end='')
if len(sys.argv) > 2:
    w2, h2, b = ppm(sys.argv[2])
    if (w, h) != (w2, h2):
        print(' misura diversa da %dx%d' % (w2, h2))
        sys.exit(1)
    print(' diversi %d' % sum(a[y][i:i+3] != b[y][i:i+3]
                              for y in range(h) for i in range(0, w * 3, 3)), end='')
print()

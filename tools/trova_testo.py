#!/usr/bin/env python3
"""Where a string is drawn in a screendump of ExWin, found by its pixels.

    python3 tools/trova_testo.py shot.ppm "nota.txt"        ->  "x y" (top left)
    python3 tools/trova_testo.py -a shot.ppm "prova"        ->  every match
    python3 tools/trova_testo.py -z 0,0,200,600 shot.ppm "exwin"
                                     only where the top left is in that box

ExWin draws its system font with the glyphs of kernel/arch/x86/font8x16.c
(8x16, one solid colour, no smoothing), at any pixel position. A string is
there when every lit pixel of its glyphs has the same colour and no unlit
pixel touching a lit one has it: so it is found on any background, white on
the blue of a selection, or with the black shadow of the desktop captions.
! ONLY THE UNLIT PIXELS NEXT TO THE INK COUNT, not the whole cell: the blue
bar of a chosen row in a list is one pixel higher than the cells, and the
cell's last row is white like the text.

! A TEST THAT CLICKS BY NAME DOES NOT BREAK WHEN THE LAYOUT CHANGES. The
desktop got the Documents link and the rdboot icon, the file manager's tree
got rdboot and USB, and every test that clicked at a fixed row clicked the
wrong thing (30 October 2026). tools/qemu_drive.py uses this for its
`punta:` step. Exit code 1 when the string is not there.
"""
import os
import re
import sys

RADICE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(RADICE, "kernel", "arch", "x86", "font8x16.c")
W, H = 8, 16

_glifi = None


def glifi():
    """The 256 glyphs, 16 bytes each."""
    global _glifi
    if _glifi is None:
        testo = open(FONT).read()
        corpo = testo[testo.index("font8x16[256 * 16]"):]
        corpo = re.sub(r"/\*.*?\*/", "", corpo[corpo.index("{") + 1:corpo.index("};")], flags=re.S)
        b = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", corpo)]
        _glifi = [b[c * H:(c + 1) * H] for c in range(256)]
    return _glifi


def leggi_ppm(percorso):
    d = open(percorso, "rb").read()
    parti = d.split(b"\n", 3)
    if parti[0] != b"P6":
        raise ValueError("%s non e' un PPM binario" % percorso)
    w, h = map(int, parti[1].split())
    return w, h, parti[3]


def maschera(testo):
    """(lit, unlit): the pixel offsets (dx, dy) of the ink, and of the unlit
    pixels of the cells next to it (left, right, above, below)."""
    g = glifi()
    acceso = set()
    for i, ch in enumerate(testo.encode("cp437", "replace")):
        for r in range(H):
            riga = g[ch][r]
            for b in range(W):
                if riga & (0x80 >> b):
                    acceso.add((i * W + b, r))
    larg = len(testo) * W
    spento = set()
    for x, y in acceso:
        for v in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if v not in acceso and 0 <= v[0] < larg and 0 <= v[1] < H:
                spento.add(v)
    return sorted(acceso, key=lambda p: (p[1], p[0])), sorted(spento)


def trova(w, h, px, testo, tutti=False, zona=None):
    """The top left corners where `testo` is drawn: the first, or all; with
    `zona` (x0, y0, x1, y1) only corners inside it (the same name can be in
    the tree and in the list of the file manager)."""
    acceso, spento = maschera(testo)
    if not acceso:
        return []
    larg = len(testo) * W
    x0, y0, x1, y1 = zona if zona else (0, 0, w, h)
    a0x, a0y = acceso[0]
    resto = [(dx, dy) for dx, dy in acceso[1:]]
    trovati = []
    for y in range(max(0, y0), min(h - H + 1, y1)):
        for x in range(max(0, x0), min(w - larg + 1, x1)):
            o = ((y + a0y) * w + x + a0x) * 3
            c = px[o:o + 3]
            buono = True
            for dx, dy in resto:
                o = ((y + dy) * w + x + dx) * 3
                if px[o:o + 3] != c:
                    buono = False
                    break
            if not buono:
                continue
            for dx, dy in spento:
                o = ((y + dy) * w + x + dx) * 3
                if px[o:o + 3] == c:
                    buono = False
                    break
            if buono:
                trovati.append((x, y))
                if not tutti:
                    return trovati
    return trovati


def main():
    args = sys.argv[1:]
    tutti, zona = False, None
    while args and args[0] in ("-a", "-z"):
        if args[0] == "-a":
            tutti, args = True, args[1:]
        else:
            zona, args = tuple(int(v) for v in args[1].split(",")), args[2:]
    if len(args) != 2:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    w, h, px = leggi_ppm(args[0])
    r = trova(w, h, px, args[1], tutti, zona)
    for x, y in r:
        print(x, y)
    return 0 if r else 1


if __name__ == "__main__":
    sys.exit(main())

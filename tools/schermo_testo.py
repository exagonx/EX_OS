#!/usr/bin/env python3
"""The text of a QEMU screendump of the graphics console, read exactly.

    python3 tools/schermo_testo.py shot.ppm            the text, one line per row
    python3 tools/schermo_testo.py -i shot.ppm         and a second block that
                                                       marks reverse cells with #

tools/schermo.py only says whether a cell has lit pixels (enough for text
mode layouts). In graphics the console draws every cell with the glyphs of
kernel/arch/x86/font8x16.c (8x16, one foreground and one background colour),
so a cell can be matched to its glyph bit for bit: this gives the real
characters, and which cells are in reverse video (the light colour is the
background). The cursor (the bottom two pixel rows in the foreground colour)
is tolerated. The CP437 line drawing characters (curses' boxes) come out as
+ - |, any other character outside ASCII and a cell no glyph matches as '?'.
"""
import os
import re
import sys

RADICE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(RADICE, "kernel", "arch", "x86", "font8x16.c")
W, H = 8, 16
# CP437 line drawing (what curses draws with the "vga" acsc) shown as ASCII
LINEE = {0xB3: "|", 0xBA: "|", 0xC4: "-", 0xCD: "-"}
for _c in (0xDA, 0xBF, 0xC0, 0xD9, 0xC3, 0xB4, 0xC1, 0xC2, 0xC5,
           0xC9, 0xBB, 0xC8, 0xBC, 0xCC, 0xB9, 0xCA, 0xCB, 0xCE):
    LINEE[_c] = "+"


def leggi_font():
    testo = open(FONT).read()
    corpo = testo[testo.index("font8x16[256 * 16]"):]
    corpo = re.sub(r"/\*.*?\*/", "", corpo[corpo.index("{") + 1:corpo.index("};")], flags=re.S)
    byte = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", corpo)]
    glifi = {}
    for ch in range(256):
        g = tuple(byte[ch * H:(ch + 1) * H])
        glifi.setdefault(g, ch)       # the first character with that glyph
    return glifi


def leggi_ppm(path):
    dati = open(path, "rb").read()
    campi, i = [], 0
    while len(campi) < 4:
        while dati[i:i + 1].isspace():
            i += 1
        if dati[i:i + 1] == b"#":
            while dati[i:i + 1] != b"\n":
                i += 1
            continue
        j = i
        while not dati[j:j + 1].isspace():
            j += 1
        campi.append(dati[i:j])
        i = j
    return int(campi[1]), int(campi[2]), dati[i + 1:]


def luce(c):
    return c[0] * 3 + c[1] * 6 + c[2]


def cella(px, w, cx, cy, glifi):
    """(character, reverse) of the cell at column cx, row cy."""
    righe = []
    colori = {}
    for r in range(H):
        riga = []
        for b in range(W):
            o = ((cy * H + r) * w + cx * W + b) * 3
            c = (px[o], px[o + 1], px[o + 2])
            riga.append(c)
            colori[c] = colori.get(c, 0) + 1
        righe.append(riga)
    if len(colori) == 1:
        return " ", False
    if len(colori) > 2:
        return "?", False
    a, b = sorted(colori, key=luce)          # a dark, b light
    for fg, inverso in ((b, False), (a, True)):
        maschera = tuple(sum(0x80 >> k for k in range(W) if righe[r][k] == fg)
                         for r in range(H))
        for prova in (maschera, maschera[:H - 2] + (0, 0)):
            if prova in glifi:
                ch = glifi[prova]
                if ch in LINEE:
                    return LINEE[ch], inverso
                return (chr(ch) if 32 <= ch < 127 else "?"), inverso
        # the cursor on a blank cell: only the bottom two rows lit
        if maschera[:H - 2] == (0,) * (H - 2):
            return " ", inverso
    return "?", False


def testo(path):
    glifi = leggi_font()
    w, h, px = leggi_ppm(path)
    righe, inversi = [], []
    for cy in range(h // H):
        t, m = "", ""
        for cx in range(w // W):
            ch, inv = cella(px, w, cx, cy, glifi)
            t += ch
            m += "#" if inv else " "
        righe.append(t.rstrip())
        inversi.append(m.rstrip())
    return righe, inversi


def main():
    args = sys.argv[1:]
    mostra_inversi = "-i" in args
    args = [a for a in args if a != "-i"]
    if not args:
        print(__doc__.strip().splitlines()[0], file=sys.stderr)
        return 2
    righe, inversi = testo(args[0])
    for n, r in enumerate(righe, 1):
        print("%2d|%s" % (n, r))
    if mostra_inversi:
        for n, r in enumerate(inversi, 1):
            if r:
                print("%2d#%s" % (n, r))
    return 0


if __name__ == "__main__":
    sys.exit(main())

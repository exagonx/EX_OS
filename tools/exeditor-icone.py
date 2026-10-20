#!/usr/bin/env python3
# =============================================================================
# tools/exeditor-icone.py - the toolbar icons of ExEditor (9 October 2026)
#
#     python3 tools/exeditor-icone.py
#
# Draws each icon at 128 pixels, scales it to 32 and writes
# exwin/icon/strumenti/<name>.ico - 32-bit BMP inside, written by hand as in
# tools/giochi-icone.py (Pillow would put a PNG inside). The toolbar shows
# them at 16: one drawing serves both.
# =============================================================================
import os
import struct
from PIL import Image, ImageDraw, ImageFont

RADICE = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
USCITA = os.path.join(RADICE, "exwin", "icon", "strumenti")
L = 128
NERO = (20, 20, 20, 255)
BLU = (30, 70, 160, 255)
GRIGIO = (110, 110, 110, 255)
CARTA = (255, 255, 255, 255)
GIALLO = (240, 200, 70, 255)
FONT = "/usr/share/fonts/truetype/liberation/LiberationSerif-%s.ttf"


def scrivi_ico(img, percorso):
    w, h = img.size
    px = img.convert("RGBA").load()
    righe = []
    for y in range(h - 1, -1, -1):
        righe.append(b"".join(struct.pack("BBBB", px[x, y][2], px[x, y][1], px[x, y][0], px[x, y][3])
                              for x in range(w)))
    colore = b"".join(righe)
    passo = ((w + 31) // 32) * 4
    maschera = bytearray()
    for y in range(h - 1, -1, -1):
        riga = bytearray(passo)
        for x in range(w):
            if px[x, y][3] == 0:
                riga[x // 8] |= 0x80 >> (x % 8)
        maschera += riga
    dib = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, len(colore) + len(maschera), 0, 0, 0, 0)
    dati = dib + colore + bytes(maschera)
    with open(percorso, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, 1) +
                struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(dati), 22) + dati)


def tela():
    img = Image.new("RGBA", (L, L), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def lettera(ch, stile, sotto=False):
    img, d = tela()
    f = ImageFont.truetype(FONT % stile, 112)
    b = d.textbbox((0, 0), ch, font=f)
    d.text(((L - (b[2] - b[0])) / 2 - b[0], (L - (b[3] - b[1])) / 2 - b[1] - (8 if sotto else 0)),
           ch, font=f, fill=NERO)
    if sotto:
        d.rectangle((20, 112, 108, 122), fill=NERO)
    return img


def righe(allinea):
    img, d = tela()
    lunghe = [96, 64, 96, 48, 96]
    if allinea == "giu":
        lunghe = [96, 96, 96, 96, 96]
    for i, w in enumerate(lunghe):
        y = 14 + i * 22
        x = {"sin": 16, "des": 112 - w, "cen": (L - w) // 2, "giu": 16}[allinea]
        d.rectangle((x, y, x + w, y + 11), fill=NERO)
    return img


def rientro(piu):
    img, d = tela()
    for i in range(5):
        y = 14 + i * 22
        x = 16 if i in (0, 4) else 56
        d.rectangle((x, y, 112, y + 11), fill=NERO)
    if piu:
        d.polygon([(12, 40), (12, 88), (42, 64)], fill=BLU)
    else:
        d.polygon([(42, 40), (42, 88), (12, 64)], fill=BLU)
    return img


def foglio(d, x0=28, y0=10, x1=100, y1=118, piega=24):
    d.polygon([(x0, y0), (x1 - piega, y0), (x1, y0 + piega), (x1, y1), (x0, y1)], fill=CARTA, outline=NERO)
    d.line([(x0, y0), (x1 - piega, y0), (x1, y0 + piega), (x1, y1), (x0, y1), (x0, y0)], fill=NERO, width=6)
    d.line([(x1 - piega, y0), (x1 - piega, y0 + piega), (x1, y0 + piega)], fill=NERO, width=6)


def nuovo():
    img, d = tela()
    foglio(d)
    return img


def apri():
    img, d = tela()
    d.polygon([(10, 34), (48, 34), (58, 46), (112, 46), (112, 108), (10, 108)], fill=GIALLO, outline=NERO)
    d.line([(10, 34), (48, 34), (58, 46), (112, 46), (112, 108), (10, 108), (10, 34)], fill=NERO, width=6)
    d.polygon([(10, 108), (30, 62), (124, 62), (104, 108)], fill=(250, 225, 120, 255))
    d.line([(10, 108), (30, 62), (124, 62), (104, 108), (10, 108)], fill=NERO, width=6)
    return img


def salva():
    img, d = tela()
    d.rectangle((12, 12, 116, 116), fill=BLU, outline=NERO, width=6)
    d.rectangle((32, 12, 96, 50), fill=CARTA, outline=NERO, width=5)
    d.rectangle((74, 20, 86, 42), fill=BLU)
    d.rectangle((28, 68, 100, 116), fill=CARTA, outline=NERO, width=5)
    return img


def taglia():
    img, d = tela()
    d.line([(40, 10), (78, 84)], fill=NERO, width=9)
    d.line([(88, 10), (50, 84)], fill=NERO, width=9)
    d.ellipse((22, 78, 58, 114), outline=BLU, width=9)
    d.ellipse((70, 78, 106, 114), outline=BLU, width=9)
    return img


def copia():
    img, d = tela()
    foglio(d, 12, 8, 76, 92, 18)
    foglio(d, 48, 34, 116, 122, 18)
    return img


def incolla():
    img, d = tela()
    d.rectangle((14, 20, 104, 120), fill=(190, 140, 80, 255), outline=NERO, width=6)
    d.rectangle((40, 8, 78, 32), fill=GRIGIO, outline=NERO, width=5)
    foglio(d, 46, 50, 118, 124, 18)
    return img


ICONE = {
    "nuovo": nuovo, "apri": apri, "salva": salva,
    "taglia": taglia, "copia": copia, "incolla": incolla,
    "grassetto": lambda: lettera("G", "Bold"),
    "corsivo": lambda: lettera("C", "Italic"),
    "sottolineato": lambda: lettera("S", "Regular", True),
    "sinistra": lambda: righe("sin"), "centro": lambda: righe("cen"),
    "destra": lambda: righe("des"), "giustifica": lambda: righe("giu"),
    "rientro_meno": lambda: rientro(False), "rientro_piu": lambda: rientro(True),
}


def main():
    os.makedirs(USCITA, exist_ok=True)
    for nome, fa in ICONE.items():
        img = fa().resize((32, 32), Image.LANCZOS)
        # A hard edge: a half-transparent pixel would mix with whatever the
        # reader assumes is behind it.
        px = img.load()
        for y in range(32):
            for x in range(32):
                r, g, b, a = px[x, y]
                px[x, y] = (r, g, b, 255) if a >= 96 else (0, 0, 0, 0)
        scrivi_ico(img, os.path.join(USCITA, nome + ".ico"))
    print("%d icone in %s" % (len(ICONE), USCITA))


if __name__ == "__main__":
    main()

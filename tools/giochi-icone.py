#!/usr/bin/env python3
# =============================================================================
# tools/giochi-icone.py — the icons of the four ExWin games (@GIOCHI, 3 October 2026)
#
#     python3 tools/giochi-icone.py
#
# Draws EXKlondike, EXSpider, EXMajong and EXGO at 512 pixels, scales them to
# 128 and 64 and writes exwin/icon/baseapp/<name>_64.ico and _128.ico.
#
# ! THE ICO IS WRITTEN BY HAND, 32-bit BMP inside, as the other icons of the
# system (16958 bytes at 64 pixels): Pillow would write a PNG inside, and
# that is a format eximg does not have to know for an icon.
# =============================================================================
import os
import struct
from PIL import Image, ImageDraw, ImageFilter

RADICE = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
USCITA = os.path.join(RADICE, "exwin", "icon", "baseapp")
L = 512


def scrivi_ico(img, percorso):
    w, h = img.size
    px = img.convert("RGBA").load()
    righe = []
    for y in range(h - 1, -1, -1):
        righe.append(b"".join(struct.pack("BBBB", *(px[x, y][2], px[x, y][1], px[x, y][0], px[x, y][3]))
                              for x in range(w)))
    colore = b"".join(righe)
    passo_maschera = ((w + 31) // 32) * 4
    maschera = bytearray()
    for y in range(h - 1, -1, -1):
        riga = bytearray(passo_maschera)
        for x in range(w):
            if px[x, y][3] == 0:
                riga[x // 8] |= 0x80 >> (x % 8)
        maschera += riga
    dib = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, len(colore) + len(maschera), 0, 0, 0, 0)
    dati = dib + colore + bytes(maschera)
    testa = struct.pack("<HHH", 0, 1, 1)
    voce = struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(dati), 6 + 16)
    with open(percorso, "wb") as f:
        f.write(testa + voce + dati)


def ombra(img, spost=10, raggio=12, forza=110):
    a = img.split()[3].filter(ImageFilter.GaussianBlur(raggio))
    o = Image.new("RGBA", img.size, (0, 0, 0, 0))
    nero = Image.new("RGBA", img.size, (0, 0, 0, forza))
    o.paste(nero, (spost, spost), a)
    o.alpha_composite(img)
    return o


# --- cards ------------------------------------------------------------------
ROSSO = (200, 30, 30, 255)
NERO = (20, 20, 20, 255)


def cuore(d, cx, cy, s, c):
    r = s * 0.26
    d.ellipse([cx - s * 0.48, cy - s * 0.42, cx - s * 0.48 + 2 * r, cy - s * 0.42 + 2 * r], fill=c)
    d.ellipse([cx + s * 0.48 - 2 * r, cy - s * 0.42, cx + s * 0.48, cy - s * 0.42 + 2 * r], fill=c)
    d.polygon([(cx - s * 0.47, cy - s * 0.1), (cx + s * 0.47, cy - s * 0.1), (cx, cy + s * 0.5)], fill=c)


def picche(d, cx, cy, s, c):
    r = s * 0.25
    d.ellipse([cx - s * 0.48, cy - s * 0.05, cx - s * 0.48 + 2 * r, cy - s * 0.05 + 2 * r], fill=c)
    d.ellipse([cx + s * 0.48 - 2 * r, cy - s * 0.05, cx + s * 0.48, cy - s * 0.05 + 2 * r], fill=c)
    d.polygon([(cx, cy - s * 0.5), (cx - s * 0.47, cy + s * 0.2), (cx + s * 0.47, cy + s * 0.2)], fill=c)
    d.polygon([(cx - s * 0.05, cy + s * 0.1), (cx + s * 0.05, cy + s * 0.1),
               (cx + s * 0.2, cy + s * 0.5), (cx - s * 0.2, cy + s * 0.5)], fill=c)


def carta(dim, seme, valore, angolo):
    w, h = dim
    c = Image.new("RGBA", (w + 40, h + 40), (0, 0, 0, 0))
    d = ImageDraw.Draw(c)
    d.rounded_rectangle([20, 20, 20 + w, 20 + h], radius=w // 9, fill=(255, 255, 255, 255),
                        outline=(120, 120, 120, 255), width=4)
    col = ROSSO if seme == "cuori" else NERO
    disegna = cuore if seme == "cuori" else picche
    disegna(d, 20 + w / 2, 20 + h / 2, w * 0.55, col)
    disegna(d, 20 + w * 0.16, 20 + h * 0.22, w * 0.16, col)
    try:
        from PIL import ImageFont
        f = ImageFont.truetype("DejaVuSans-Bold.ttf", int(w * 0.2))
        d.text((20 + w * 0.08, 20 + h * 0.02), valore, font=f, fill=col)
    except OSError:
        pass
    return c.rotate(angolo, resample=Image.BICUBIC, expand=True)


def incolla(base, pezzo, cx, cy):
    base.alpha_composite(pezzo, (int(cx - pezzo.size[0] / 2), int(cy - pezzo.size[1] / 2)))


def klondike():
    img = Image.new("RGBA", (L, L), (0, 0, 0, 0))
    incolla(img, carta((230, 320), "picche", "A", 14), 200, 250)
    incolla(img, carta((230, 320), "cuori", "K", -10), 310, 270)
    return ombra(img)


def ragno(d, cx, cy, s):
    nero = (25, 25, 25, 255)
    for lato in (-1, 1):
        for k, (dx, dy) in enumerate([(0.55, -0.45), (0.65, -0.12), (0.62, 0.18), (0.5, 0.48)]):
            gx, gy = cx + lato * s * 0.18, cy + s * (k - 1.5) * 0.08
            mx, my = cx + lato * s * dx * 0.7, cy + s * dy * 0.6 - s * 0.12
            fx, fy = cx + lato * s * dx, cy + s * dy
            d.line([(gx, gy), (mx, my), (fx, fy)], fill=nero, width=int(s * 0.05), joint="curve")
    d.ellipse([cx - s * 0.2, cy - s * 0.05, cx + s * 0.2, cy + s * 0.45], fill=nero)
    d.ellipse([cx - s * 0.13, cy - s * 0.3, cx + s * 0.13, cy - s * 0.02], fill=nero)
    d.ellipse([cx - s * 0.07, cy + s * 0.1, cx + s * 0.07, cy + s * 0.3], fill=(200, 30, 30, 255))


def spider():
    img = Image.new("RGBA", (L, L), (0, 0, 0, 0))
    for k, ang in enumerate((24, 8, -8)):
        incolla(img, carta((210, 290), "picche", "A" if k == 2 else "K", ang), 210 + k * 50, 270)
    d = ImageDraw.Draw(img)
    ragno(d, 300, 250, 260)
    return ombra(img)


# --- tiles -------------------------------------------------------------------
def tessera(w, h, disegno):
    t = Image.new("RGBA", (w + 60, h + 60), (0, 0, 0, 0))
    d = ImageDraw.Draw(t)
    sp = int(w * 0.12)
    d.rounded_rectangle([sp, sp, w + sp, h + sp], radius=w // 7, fill=(156, 132, 86, 255))
    d.rounded_rectangle([sp // 2, sp // 2, w + sp // 2, h + sp // 2], radius=w // 7, fill=(194, 169, 112, 255))
    d.rounded_rectangle([0, 0, w, h], radius=w // 7, fill=(251, 246, 230, 255),
                        outline=(140, 122, 80, 255), width=5)
    disegno(d, w, h)
    return t


def cerchi(d, w, h):
    colori = [(42, 93, 176, 255), (46, 139, 62, 255), (192, 40, 45, 255)]
    for k, (fx, fy) in enumerate([(0.27, 0.22), (0.73, 0.22), (0.5, 0.5), (0.27, 0.78), (0.73, 0.78)]):
        r = w * 0.15
        cx, cy = w * fx, h * fy
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=colori[k % 3])
        d.ellipse([cx - r * 0.55, cy - r * 0.55, cx + r * 0.55, cy + r * 0.55], fill=(251, 246, 230, 255))
        d.ellipse([cx - r * 0.25, cy - r * 0.25, cx + r * 0.25, cy + r * 0.25], fill=colori[k % 3])


def drago(d, w, h):
    rosso = (192, 40, 45, 255)
    d.rounded_rectangle([w * 0.2, h * 0.15, w * 0.8, h * 0.62], radius=w // 12, fill=rosso)
    d.rectangle([w * 0.46, h * 0.08, w * 0.54, h * 0.9], fill=rosso)
    d.rectangle([w * 0.3, h * 0.36, w * 0.7, h * 0.41], fill=(251, 246, 230, 255))


def majong():
    img = Image.new("RGBA", (L, L), (0, 0, 0, 0))
    incolla(img, tessera(200, 270, cerchi), 205, 235)
    incolla(img, tessera(200, 270, drago), 320, 300)
    return ombra(img)


# --- go ----------------------------------------------------------------------
def pietra(img, cx, cy, r, bianca):
    p = Image.new("RGBA", (int(2 * r) + 4, int(2 * r) + 4), (0, 0, 0, 0))
    d = ImageDraw.Draw(p)
    base = (238, 238, 232) if bianca else (30, 30, 30)
    for k in range(int(r), 0, -1):
        t = k / r
        luce = (1 - t) * (90 if not bianca else 17)
        c = tuple(min(255, int(v + luce)) for v in base) + (255,)
        ox = (1 - t) * r * -0.35
        d.ellipse([r + 2 - k + ox, r + 2 - k + ox, r + 2 + k + ox, r + 2 + k + ox], fill=c)
    m = Image.new("L", p.size, 0)
    ImageDraw.Draw(m).ellipse([2, 2, 2 + 2 * r, 2 + 2 * r], fill=255)
    p.putalpha(m)
    img.alpha_composite(p, (int(cx - r - 2), int(cy - r - 2)))


def go():
    img = Image.new("RGBA", (L, L), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    x0, y0, x1, y1 = 56, 56, 456, 456
    for y in range(y0, y1):
        t = (y - y0) / (y1 - y0)
        c = (int(230 - 20 * t), int(188 - 28 * t), int(108 - 30 * t), 255)
        d.line([(x0, y), (x1, y)], fill=c)
    d.rectangle([x0, y0, x1, y1], outline=(120, 80, 30, 255), width=6)
    passo = (x1 - x0 - 80) / 4
    for k in range(5):
        v = x0 + 40 + k * passo
        d.line([(v, y0 + 40), (v, y1 - 40)], fill=(42, 26, 8, 255), width=6)
        d.line([(x0 + 40, v), (x1 - 40, v)], fill=(42, 26, 8, 255), width=6)
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([x0, y0, x1, y1], radius=30, fill=255)
    img.putalpha(mask)
    g = lambda i: x0 + 40 + i * passo
    for (i, j, b) in [(1, 1, False), (2, 1, True), (2, 2, False), (3, 2, True), (1, 3, True), (3, 3, False)]:
        pietra(img, g(i), g(j), passo * 0.47, b)
    return ombra(img)


def main():
    os.makedirs(USCITA, exist_ok=True)
    for nome, fn in (("exklondike", klondike), ("exspider", spider), ("exmajong", majong), ("exgo", go)):
        grande = fn()
        for lato in (128, 64):
            scrivi_ico(grande.resize((lato, lato), Image.LANCZOS),
                       os.path.join(USCITA, "%s_%d.ico" % (nome, lato)))
        print("%s_64.ico, %s_128.ico" % (nome, nome))


if __name__ == "__main__":
    main()

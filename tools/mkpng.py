#!/usr/bin/env python3
# =============================================================================
# tools/mkpng.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
# This file is part of EX-OS, distributed under the GNU GPL v2.
# See the LICENSE file in the project root for the full license text.
# =============================================================================
#
# Genera i PNG con cui si prova lib/eximg, e accanto a ognuno il file dei pixel
# attesi (.raw, ARGB a 32 bit).
#
# ! I FILTRI SI ESERCITANO TUTTI E CINQUE, UNA RIGA PER TIPO A GIRO. Un
#   generatore che scrivesse tutte le righe col filtro 0 produrrebbe un file
#   che passa senza aver provato niente: e' il filtro di Paeth quello che si
#   sbaglia, e con filtro 0 non viene nemmeno chiamato.
#
# ! E IL PATTERN NON E' CASUALE: e' una funzione degli indici, cosi' i pixel
#   attesi si ricavano dalla stessa formula invece che da un secondo
#   decodificatore di cui bisognerebbe fidarsi.
# =============================================================================
import struct, zlib, sys, os


def pattern(x, y):
    """Il colore atteso in (x,y): rosso cresce a destra, verde in basso, blu
    a scacchiera. Nessun canale resta costante, cosi' uno scambio fra canali
    si vede."""
    return ((x * 7) & 0xFF, (y * 5) & 0xFF, 0xFF if ((x ^ y) & 8) else 0x20)


def chunk(tipo, dati):
    return (struct.pack(">I", len(dati)) + tipo + dati +
            struct.pack(">I", zlib.crc32(tipo + dati) & 0xFFFFFFFF))


def filtra(riga, sopra, bpp, tipo):
    """Applica il filtro PNG `tipo` a una riga gia' in byte."""
    out = bytearray()
    for i, b in enumerate(riga):
        a = riga[i - bpp] if i >= bpp else 0
        b_ = sopra[i] if sopra else 0
        c = sopra[i - bpp] if (sopra and i >= bpp) else 0
        if   tipo == 0: v = b
        elif tipo == 1: v = b - a
        elif tipo == 2: v = b - b_
        elif tipo == 3: v = b - ((a + b_) >> 1)
        else:
            p = a + b_ - c
            pa, pb, pc = abs(p - a), abs(p - b_), abs(p - c)
            pr = a if (pa <= pb and pa <= pc) else (b_ if pb <= pc else c)
            v = b - pr
        out.append(v & 0xFF)
    return out


def scrivi(percorso, larg, alt, tipo_colore, tavolozza=None):
    canali = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[tipo_colore]

    righe = []
    for y in range(alt):
        riga = bytearray()
        for x in range(larg):
            r, g, b = pattern(x, y)
            if   tipo_colore == 2: riga += bytes((r, g, b))
            elif tipo_colore == 6: riga += bytes((r, g, b, 255))
            elif tipo_colore == 0: riga.append(r)
            elif tipo_colore == 4: riga += bytes((r, 255))
            else:                  riga.append((x + y) % len(tavolozza))
        righe.append(riga)

    grezzo = bytearray()
    sopra = None
    for y, riga in enumerate(righe):
        f = y % 5                       # tutti e cinque, a giro
        grezzo.append(f)
        grezzo += filtra(riga, sopra, canali, f)
        sopra = riga

    ihdr = struct.pack(">IIBBBBB", larg, alt, 8, tipo_colore, 0, 0, 0)
    dati = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
    if tipo_colore == 3:
        dati += chunk(b"PLTE", b"".join(bytes(c) for c in tavolozza))

    # ! GLI IDAT SI SPEZZANO APPOSTA IN PIU' PEZZI: la specifica dice di
    #   concatenarli prima di decomprimere, e un decodificatore che li tratta
    #   uno per uno legge un flusso troncato. Con un pezzo solo quel difetto
    #   non si vedrebbe.
    compresso = zlib.compress(bytes(grezzo), 9)
    taglio = max(1, len(compresso) // 3)
    for i in range(0, len(compresso), taglio):
        dati += chunk(b"IDAT", compresso[i:i + taglio])
    dati += chunk(b"IEND", b"")

    with open(percorso, "wb") as f:
        f.write(dati)

    # I pixel attesi, ARGB a 32 bit come li rende eximg.
    raw = bytearray()
    for y in range(alt):
        for x in range(larg):
            r, g, b = pattern(x, y)
            if tipo_colore == 0:            # grigio: un canale replicato
                r = g = b = r
            elif tipo_colore == 4:
                r = g = b = r
            elif tipo_colore == 3:
                r, g, b = tavolozza[(x + y) % len(tavolozza)]
            raw += struct.pack("<I", (r << 16) | (g << 8) | b)
    with open(percorso[:-4] + ".raw", "wb") as f:
        f.write(raw)

    return len(dati)


def main():
    fuori = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(fuori, exist_ok=True)

    tav = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0),
           (0, 255, 255), (255, 0, 255), (16, 16, 16), (240, 240, 240)]

    prove = [("rgb.png", 64, 48, 2, None),
             ("rgba.png", 40, 24, 6, None),
             ("grigio.png", 33, 17, 0, None),
             ("grigioalfa.png", 20, 20, 4, None),
             ("tavolozza.png", 50, 30, 3, tav)]

    for nome, w, h, tipo, t in prove:
        n = scrivi(os.path.join(fuori, nome), w, h, tipo, t)
        print("%-16s %3dx%-3d tipo %d  %5d byte" % (nome, w, h, tipo, n))


if __name__ == "__main__":
    main()

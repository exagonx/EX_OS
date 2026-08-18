#!/usr/bin/env python3
# tools/righe_lista.py — quante righe ha davvero una lista, in una fotografia
#
# ! UNA LISTA CHE SI E' APERTA LO DICONO I PIXEL, NON IL LOG. Premere il «+»
#   dell'albero del file manager deve far comparire delle righe SOTTO quella
#   premuta: se non compaiono, il clic non e' arrivato dove si credeva, o e'
#   arrivato e non ha fatto niente — e dal serale non si distinguono, perche'
#   il file manager di quello che disegna non dice una parola.
#
# ! NON SI LEGGONO I CARATTERI, SI CONTA L'INCHIOSTRO. Riconoscere i glifi
#   vorrebbe dire portarsi dietro il font: qui basta sapere quali righe sono
#   VUOTE e quali no, e quale ha la barra della scelta. Tre stati distinti da
#   due numeri:
#
#     riga vuota      il fondo bianco, e basta          -> 0 non-bianchi
#     riga con testo  qualche centinaio di pixel neri   -> 100..1500
#     riga scelta     tutta la barra blu (o grigia)     -> migliaia
#
#     python3 tools/righe_lista.py foto.ppm X Y LARG QUANTE
#
#       riga  0   inchiostro   3776   <- scelta
#       riga  1   inchiostro    248
#       riga  2   inchiostro      0
"""Conta l'inchiostro riga per riga dentro una lista di ExWin."""
import sys

RIGA_H = 16                      # LISTA_RIGA_H, in lib/exwin/exwin.c
BIANCO = (255, 255, 255)
BLU    = (48, 90, 138)           # EX_BLU: la barra della scelta col fuoco
GRIGIO = (128, 128, 128)         # EX_GRIGIO_SC: la barra senza il fuoco


def leggi_ppm(percorso):
    d = open(percorso, "rb").read()
    parti = d.split(b"\n", 3)
    if parti[0] != b"P6":
        raise ValueError("%s non e' un PPM binario" % percorso)
    w, h = map(int, parti[1].split())
    return w, h, parti[3]


def righe(percorso, x0, y0, larg, quante):
    w, h, px = leggi_ppm(percorso)
    fuori = []

    for k in range(quante):
        # +2: il testo della lista comincia due pixel sotto il bordo, come lo
        # disegna CL_LISTA. La banda della riga k parte li'.
        y = y0 + 2 + k * RIGA_H
        inch = 0
        barra = 0

        for yy in range(y, min(y + RIGA_H, h)):
            base = yy * w
            for xx in range(x0, min(x0 + larg, w)):
                i = (base + xx) * 3
                c = (px[i], px[i + 1], px[i + 2])
                if c != BIANCO:
                    inch += 1
                if c in (BLU, GRIGIO):
                    barra += 1

        fuori.append((k, inch, barra))
    return fuori


if __name__ == "__main__":
    if len(sys.argv) != 6:
        print(__doc__)
        sys.exit(1)

    foto = sys.argv[1]
    x0, y0, larg, quante = (int(v) for v in sys.argv[2:6])

    for k, inch, barra in righe(foto, x0, y0, larg, quante):
        nota = "   <- scelta" if barra > larg * RIGA_H // 2 else ""
        print("riga %2d   inchiostro %6d%s" % (k, inch, nota))

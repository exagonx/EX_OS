# tools/misura_finestre.py — quanto sono grandi le finestre, in una fotografia
#
# ! IL NUMERO STA NEI PIXEL, NON NEL LOG. Un'applicazione puo' dire di essere
#   diventata 480x300 e il compositore averla disegnata di un'altra misura: le
#   due cose stanno in due processi diversi, e finche' non si contano i pixel
#   del framebuffer si e' creduto a una sola delle due parti.
#
# ! E I COLORI SONO QUELLI DEL SERVER, non una scelta di qui: stanno in cima a
#   drivers/wserver/wserver.c. Se li si cambia la', qui non si trova piu'
#   niente — ed e' giusto cosi': meglio un attrezzo che dice «zero finestre»
#   di uno che indovina.
#
#     python3 tools/misura_finestre.py foto.ppm
#
#     cornice   525x335 in (77,46)   ->  client 523x315
#     contorno  525x335 in (77,46)   ->  un ridimensionamento in corso
#     griglia   155648 pixel neri
"""Misura le finestre di EX-OS in una fotografia PPM di qemu_drive."""
import sys

# ! I COLORI DA CUI SI MISURA SONO QUELLI **UNICI**, e la scelta e' cambiata il
#   18 agosto 2026 con il telaio in rilievo. Prima la cornice era di un grigio
#   quasi nero che non c'era da nessun'altra parte; adesso e' bianca e nera —
#   cioe' degli stessi colori del testo — e non si distingue piu' contando i
#   pixel. Restano unici:
#
#     la BARRA DEL TITOLO   da' x, y e la LARGHEZZA della finestra, esatte
#     la PRESA nell'angolo  da' il suo spigolo in basso a destra, cioe' l'ALTEZZA
#
#   (E la scrivania del program manager non e' piu' dello stesso blu della
#   barra: fondo e barra uguali rendevano la misura ambigua, oltre che il
#   bordo di una finestra invisibile.)
C_BARRA    = (30, 77, 125)      # C_BARRA_ATT: la barra del titolo ATTIVA,
                                # e non e' il blu del toolkit apposta
C_PRESA    = (64, 64, 64)       # i segni in diagonale della presa d'angolo
C_CONTORNO = (255, 255, 128)    # il contorno di un ridimensionamento in corso
C_NERO     = (0, 0, 0)          # il fondo di un controllo «terminale»
BARRA_H    = 20
BORDO      = 2


def leggi_ppm(percorso):
    d = open(percorso, "rb").read()
    parti = d.split(b"\n", 3)
    if parti[0] != b"P6":
        raise ValueError("%s non e' un PPM binario" % percorso)
    w, h = map(int, parti[1].split())
    return w, h, parti[3]


def riquadro(w, h, px, colore):
    """Il rettangolo che contiene tutti i pixel di quel colore, o None."""
    xs, ys = [], []
    for y in range(h):
        riga = y * w
        for x in range(w):
            i = (riga + x) * 3
            if (px[i], px[i + 1], px[i + 2]) == colore:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1


def conta(w, h, px, colore):
    n = 0
    for i in range(0, w * h * 3, 3):
        if (px[i], px[i + 1], px[i + 2]) == colore:
            n += 1
    return n


def descrivi(percorso):
    w, h, px = leggi_ppm(percorso)
    fuori = []

    barra = riquadro(w, h, px, C_BARRA)
    presa = riquadro(w, h, px, C_PRESA)

    if barra is None:
        fuori.append("finestra  nessuna barra del titolo attiva")
    else:
        bx, by, bw, bh = barra
        # ! IL RIQUADRO DEL COLORE E' UN PIXEL PIU' STRETTO DELLA BARRA, da
        #   ogni lato: la barra sporge, e il suo bordo e' luce e ombra, non
        #   colore della barra. Senza questo l'area del client risultava larga
        #   due pixel meno del vero — un errore piccolo e sempre uguale, cioe'
        #   quello che si crede.
        cx, cy = bx - 1, by - 1 + BARRA_H
        riga = "finestra  client %d x " % (bw + 2)
        if presa is None:
            riga += "?  (niente presa: non e' ridimensionabile)"
        else:
            gx, gy, gw, gh = presa
            # I segni della presa arrivano a due pixel dallo spigolo in basso.
            riga += "%d" % (gy + gh + 1 - cy)
        fuori.append(riga + "   in (%d,%d)" % (cx, cy))

    r = riquadro(w, h, px, C_CONTORNO)
    if r is None:
        fuori.append("contorno  assente")
    else:
        x, y, bw, bh = r
        fuori.append("contorno  %dx%d in (%d,%d)  ->  client %dx%d"
                     % (bw, bh, x, y, bw - BORDO * 2,
                        bh - BORDO * 2 - BARRA_H))

    fuori.append("griglia   %d pixel neri" % conta(w, h, px, C_NERO))
    return "\n".join(fuori)


# ! LA PRESA SI CHIEDE A LUI, NON SI CALCOLA A MANO NELLO SCRIPT. La prova col
#   mouse deve sapere DOVE tirare, e il posto giusto dipende da dove il server
#   ha messo la finestra — che dal 18 agosto 2026 e' una cascata, cioe' non e'
#   piu' un numero che si possa scrivere in uno script. Chiederlo alla
#   fotografia e' l'unico modo che resta vero quando la disposizione cambia.
#
#       python3 tools/misura_finestre.py --presa foto.ppm     ->  "639 419"
def presa_centro(percorso):
    w, h, px = leggi_ppm(percorso)
    r = riquadro(w, h, px, C_PRESA)

    if r is None:
        return None
    x, y, bw, bh = r
    return (x + bw // 2, y + bh // 2)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    if sys.argv[1] == "--presa":
        c = presa_centro(sys.argv[2])
        if c is None:
            sys.exit(1)
        print("%d %d" % c)
        sys.exit(0)

    for p in sys.argv[1:]:
        print("--- %s" % p)
        print(descrivi(p))

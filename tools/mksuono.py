#!/usr/bin/env python3
"""Genera i due file di prova del suono: un WAV e un MIDI.

    python3 tools/mksuono.py <directory>

! I FILE SI GENERANO, NON SI TENGONO NEL REPOSITORY. Stessa scelta di
tools/mkimg.py e per lo stesso motivo: un .wav committato e' un blob di cui
nessuno sa piu' com'e' fatto, mentre qui la formula sta scritta accanto e dice
sia cosa si sente sia cosa si sta mettendo alla prova.

! E SENZA UN FILE SUL SUPPORTO IL LETTORE NON E' PROVABILE DA DENTRO. `audio
prova.wav` legge un'intestazione RIFF vera, riempie l'anello condiviso a pezzi
mentre il driver lo svuota, e aspetta la coda: e' il percorso ESATTO di un
gioco che suona un effetto. Senza un file da dargli, quel percorso non lo
esegue nessuno finche' non lo esegue un utente.

Il WAV e' un ARPEGGIO, non un tono fisso, ed e' una scelta: un tono solo si
confonde con quello che il driver genera da se' durante il collaudo, e chi
ascolta non saprebbe dire se ha suonato il file o la prova interna.
"""
import math
import struct
import sys
import os

RATE = 22050
DURATA = 1.0
NOTE = [261.63, 329.63, 392.00, 523.25]     # do, mi, sol, do — un accordo salito


def wav(percorso):
    n = int(RATE * DURATA)
    camp = []
    for i in range(n):
        # Quale nota: il secondo si divide in quattro.
        k = min(len(NOTE) - 1, int(i * len(NOTE) / n))
        f = NOTE[k]

        # Una campana su ogni nota, cosi' non ci sono scatti fra l'una e
        # l'altra: un salto di ampiezza si sente come uno schiocco e
        # sembrerebbe un difetto del driver.
        pos = (i * len(NOTE) / n) - k
        inv = math.sin(math.pi * pos)

        v = math.sin(2.0 * math.pi * f * i / RATE) * inv * 0.55
        camp.append(int(v * 32767))

    dati = struct.pack("<%dh" % len(camp), *camp)
    with open(percorso, "wb") as fh:
        fh.write(b"RIFF")
        fh.write(struct.pack("<I", 36 + len(dati)))
        fh.write(b"WAVEfmt ")
        fh.write(struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE * 2, 2, 16))
        fh.write(b"data")
        fh.write(struct.pack("<I", len(dati)))
        fh.write(dati)
    return len(dati) + 44


def vlq(v):
    """Il tempo nei file MIDI e' a sette bit per byte, il piu' alto dice
    «continua». Un delta di 480 sono due byte, non due cifre."""
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    return bytes(reversed(out))


def midi(percorso):
    div = 480                       # impulsi per semiminima
    ev = bytearray()

    ev += vlq(0) + bytes([0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20])   # 500000 us
    ev += vlq(0) + bytes([0xC0, 0x50])                            # lead 1

    # La stessa scaletta del WAV, cosi' si riconosce che sono lo stesso motivo
    # suonato da due strade diverse: una in campioni, l'altra in note.
    for nota in (60, 64, 67, 72):
        ev += vlq(0)   + bytes([0x90, nota, 100])
        ev += vlq(div) + bytes([0x80, nota, 0])

    ev += vlq(0) + bytes([0xFF, 0x2F, 0x00])                      # fine traccia

    with open(percorso, "wb") as fh:
        fh.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, div))
        fh.write(b"MTrk" + struct.pack(">I", len(ev)) + bytes(ev))
    return 22 + len(ev)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    d = sys.argv[1]
    os.makedirs(d, exist_ok=True)
    a = wav(os.path.join(d, "prova.wav"))
    b = midi(os.path.join(d, "prova.mid"))
    print("prova.wav %d byte, prova.mid %d byte" % (a, b))

#!/usr/bin/env python3
# =============================================================================
# tools/prove/doc95-genera.py - a Word 6 file written by hand (9 October 2026)
#
#     python3 tools/prove/doc95-genera.py out.doc [-complesso]
#
# ! WHY BY HAND. LibreOffice on the build machine still READS Word 6/95 files
# and no longer WRITES them, so there was no way to make a test file with a
# program. This script writes one from the description of the format, the same
# description lib/exrtf/doc95.c was written from - and that alone would prove
# nothing: two programs agreeing with their author. The proof is the third
# reader: the file is given to LibreOffice, and what LibreOffice shows is
# compared with what doc95.c reads (see the commit of 9 October 2026).
#
# -complesso writes the text as the two pieces of a "fast save", in the wrong
# order in the file, to try the piece table.
# =============================================================================
import struct
import sys

SETT = 512


def u16(v): return struct.pack("<H", v & 0xFFFF)
def u32(v): return struct.pack("<I", v & 0xFFFFFFFF)


# ---- the document: paragraphs of runs ----------------------------------------
# a run: (text, list of character sprm bytes); a paragraph: (istd, sprms, runs)
def bold(v=1): return bytes([85, v])
def italic(v=1): return bytes([86, v])
def kul(v=1): return bytes([94, v])
def hps(pt): return bytes([99]) + u16(pt * 2)
def ico(i): return bytes([98, i])
def ftc(i): return bytes([93]) + u16(i)
def jc(v): return bytes([5, v])
def left(tw): return bytes([17]) + u16(tw)
def right(tw): return bytes([16]) + u16(tw)
def left1(tw): return bytes([19]) + u16(tw)
def after(tw): return bytes([22]) + u16(tw)
def before(tw): return bytes([21]) + u16(tw)
def line(v, mult=1): return bytes([20]) + struct.pack("<hh", v, mult)


def tabs(*stops):
    corpo = bytes([0, len(stops)]) + b"".join(u16(s) for s in stops) + bytes(len(stops))
    return bytes([15, len(corpo)]) + corpo


PARAGRAFI = [
    (1, b"", [("Titolo del documento", b"")]),
    (0, jc(3) + left(567) + left1(284) + after(120),
     [("Testo normale con ", b""), ("grassetto", bold()), (" e ", b""),
      ("corsivo sottolineato", italic() + kul()), (" e ", b""),
      ("grande rosso", hps(18) + ico(6)), (". La citt\xe0 costa 5 \x80.", b"")]),
    (0, tabs(2268, 4536), [("uno\tdue\ttre", b"")]),
    (0, jc(2) + right(1134) + before(240) + line(360), [("A destra, una riga e mezza.", b""), ("x", bytes([92, 1]))]),
    (0, b"", [("Mono", ftc(2)), (" e fine.", b"")]),
]


def costruisci(complesso):
    # the text and where each run and paragraph ends
    testo = bytearray()
    corse = []          # (fc_end_relative, grpprl)
    parag = []          # (fc_end_relative, istd, grpprl)
    for istd, sp, runs in PARAGRAFI:
        for i, (t, g) in enumerate(runs):
            b = t.encode("latin-1")       # the bytes as written: \x80 is the euro of cp1252
            if i == len(runs) - 1:
                b += b"\r"
            testo += b
            corse.append((len(testo), g))
        parag.append((len(testo), istd, sp))

    FC_MIN = 0x300
    flusso = bytearray(FC_MIN)

    # where the bytes of the text really are
    if complesso:
        meta = len(testo) // 2
        # second half first, then a gap, then the first half
        fc_b = FC_MIN
        flusso += testo[meta:]
        flusso += b"\x00" * 7
        fc_a = len(flusso)
        flusso += testo[:meta]

        def fc_di(cp): return fc_a + cp if cp < meta else fc_b + (cp - meta)
        pezzi = [(0, meta, fc_a), (meta, len(testo), fc_b)]
    else:
        flusso += testo

        def fc_di(cp): return FC_MIN + cp
        pezzi = [(0, len(testo), FC_MIN)]
    fc_mac = len(flusso)

    def allinea(n):
        while len(flusso) % n:
            flusso.append(0)

    # runs and paragraphs in FILE order: a property page lists file offsets
    def per_file(fine_cp):
        # split the cp ranges at the piece borders, return (fc0, fc1, data) sorted by fc
        fuori = []
        inizio = 0
        for fine, *dati in fine_cp:
            a = inizio
            for c0, c1, fc in pezzi:
                x0, x1 = max(a, c0), min(fine, c1)
                if x0 < x1:
                    fuori.append((fc + (x0 - c0), fc + (x1 - c0), dati))
            inizio = fine
        fuori.sort()
        return fuori

    # --- CHPX page
    allinea(SETT)
    pn_chp = len(flusso) // SETT
    cf = per_file(corse)
    pag = bytearray(SETT)
    n = len(cf)
    for i, (f0, f1, _) in enumerate(cf):
        pag[i * 4:i * 4 + 4] = u32(f0)
    pag[n * 4:n * 4 + 4] = u32(cf[-1][1])
    fine = SETT - 1
    for i, (f0, f1, (g,)) in enumerate(cf):
        if not g:
            continue
        chpx = bytes([len(g)]) + g
        fine -= len(chpx)
        if fine % 2:
            fine -= 1
        pag[fine:fine + len(chpx)] = chpx
        pag[(n + 1) * 4 + i] = fine // 2
    pag[SETT - 1] = n
    fc_primo_chp, fc_ultimo_chp = cf[0][0], cf[-1][1]
    flusso += pag

    # --- PAPX page: an entry is one offset byte and six bytes of height
    pn_pap = len(flusso) // SETT
    pf = per_file(parag)
    pag = bytearray(SETT)
    n = len(pf)
    for i, (f0, f1, _) in enumerate(pf):
        pag[i * 4:i * 4 + 4] = u32(f0)
    pag[n * 4:n * 4 + 4] = u32(pf[-1][1])
    fine = SETT - 1
    for i, (f0, f1, (istd, g)) in enumerate(pf):
        corpo = u16(istd) + g
        if len(corpo) % 2:
            corpo += b"\x00"
        papx = bytes([len(corpo) // 2]) + corpo
        fine -= len(papx)
        if fine % 2:
            fine -= 1
        pag[fine:fine + len(papx)] = papx
        pag[(n + 1) * 4 + i * 7] = fine // 2
    pag[SETT - 1] = n
    fc_primo_pap, fc_ultimo_pap = pf[0][0], pf[-1][1]
    flusso += pag

    # --- the style sheet: Normal, and a heading that is big, bold, sans, centred
    def std(sti, nome, base, papx, chpx):
        c = u16(sti) + u16(1 | (base << 4)) + u16(2 | (0 << 4)) + u16(0)
        c += bytes([len(nome)]) + nome.encode() + b"\x00"
        if len(c) % 2:
            c += b"\x00"
        c += u16(len(papx)) + papx
        if len(c) % 2:
            c += b"\x00"
        c += u16(len(chpx)) + chpx
        if len(c) % 2:
            c += b"\x00"
        return u16(len(c)) + c
    stshi = u16(2) + u16(8) + u16(1) + u16(91) + u16(15) + u16(0) + u16(0)
    stsh = u16(len(stshi)) + stshi
    stsh += std(0, "Normal", 0xFFF, u16(0), hps(11))
    stsh += std(1, "Heading 1", 0, u16(1) + jc(1) + after(240), bold() + hps(16) + ftc(1))
    fc_stsh = len(flusso)
    flusso += stsh
    lcb_stsh = len(stsh)

    # --- the two tables of pages
    allinea(2)
    fc_bte_chp = len(flusso)
    flusso += u32(fc_primo_chp) + u32(fc_ultimo_chp) + u16(pn_chp)
    fc_bte_pap = len(flusso)
    flusso += u32(fc_primo_pap) + u32(fc_ultimo_pap) + u16(pn_pap)

    # --- fonts: family in bits 4-6 of the second byte
    def ffn(nome, ff):
        c = bytes([(ff << 4) | 2 | 4]) + struct.pack("<h", 400) + bytes([0, 0]) + nome.encode() + b"\x00"
        return bytes([len(c)]) + c
    fonti = ffn("Times New Roman", 1) + ffn("Arial", 2) + ffn("Courier New", 3)
    fc_ffn = len(flusso)
    flusso += u16(len(fonti) + 2) + fonti
    lcb_ffn = len(fonti) + 2

    # --- one section: A5 paper, margins
    allinea(2)
    fc_sepx = len(flusso)
    sep = bytes([164]) + u16(8391) + bytes([165]) + u16(11906) + bytes([166]) + u16(1134) + \
          bytes([167]) + u16(850) + bytes([168]) + u16(1417) + bytes([169]) + u16(1700)
    flusso += u16(len(sep)) + sep
    allinea(2)
    fc_sed = len(flusso)
    flusso += u32(0) + u32(len(testo)) + u16(0) + u32(fc_sepx) + u16(0) + u32(0xFFFFFFFF)
    lcb_sed = 20

    # --- the pieces, for a fast save
    fc_clx = lcb_clx = 0
    if complesso:
        allinea(2)
        fc_clx = len(flusso)
        plc = b"".join(u32(c0) for c0, c1, fc in pezzi) + u32(pezzi[-1][1])
        plc += b"".join(u16(0) + u32(fc) + u16(0) for c0, c1, fc in pezzi)
        flusso += bytes([2]) + u32(len(plc)) + plc
        lcb_clx = len(flusso) - fc_clx

    # --- document properties: 84 bytes of defaults
    allinea(2)
    fc_dop = len(flusso)
    dop = bytearray(84)
    dop[10:12] = u16(720)       # default tab
    flusso += dop

    while len(flusso) < 4096 or len(flusso) % SETT:
        flusso.append(0)

    # --- the FIB
    def metti(o, b): flusso[o:o + len(b)] = b
    metti(0, u16(0xA5DC)); metti(2, u16(101)); metti(4, u16(0)); metti(6, u16(0x0410))
    metti(0x0A, u16(0x0004 if complesso else 0)); metti(0x0C, u16(101))
    metti(0x18, u32(FC_MIN)); metti(0x1C, u32(fc_mac)); metti(0x20, u32(len(flusso)))
    metti(0x34, u32(len(testo)))
    metti(0x58, u32(fc_stsh)); metti(0x5C, u32(lcb_stsh))
    metti(0x60, u32(fc_stsh)); metti(0x64, u32(lcb_stsh))
    metti(0x88, u32(fc_sed)); metti(0x8C, u32(lcb_sed))
    metti(0xB8, u32(fc_bte_chp)); metti(0xBC, u32(10))
    metti(0xC0, u32(fc_bte_pap)); metti(0xC4, u32(10))
    metti(0xD0, u32(fc_ffn)); metti(0xD4, u32(lcb_ffn))
    metti(0x160, u32(fc_clx)); metti(0x164, u32(lcb_clx))
    metti(0x192, u32(fc_dop)); metti(0x196, u32(84))
    metti(0x18A, u16(pn_chp)); metti(0x18C, u16(pn_pap))      # first pages, and how many
    metti(0x18E, u16(1)); metti(0x190, u16(1))
    return bytes(flusso)


def ole(flusso):
    n_sett = len(flusso) // SETT
    testa = bytearray(SETT)
    testa[0:8] = bytes([0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1])
    testa[0x18:0x1A] = u16(0x3E); testa[0x1A:0x1C] = u16(3); testa[0x1C:0x1E] = u16(0xFFFE)
    testa[0x1E:0x20] = u16(9); testa[0x20:0x22] = u16(6)
    testa[0x2C:0x30] = u32(1)               # one sector of allocation table
    testa[0x30:0x34] = u32(1)               # the directory
    testa[0x38:0x3C] = u32(4096)
    testa[0x3C:0x40] = u32(0xFFFFFFFE); testa[0x44:0x48] = u32(0xFFFFFFFE)
    testa[0x4C:0x50] = u32(0)
    for i in range(1, 109):
        testa[0x4C + i * 4:0x50 + i * 4] = u32(0xFFFFFFFF)

    fat = bytearray(b"\xff" * SETT)
    fat[0:4] = u32(0xFFFFFFFD)
    fat[4:8] = u32(0xFFFFFFFE)
    for i in range(n_sett):
        s = 2 + i
        fat[s * 4:s * 4 + 4] = u32(s + 1 if i + 1 < n_sett else 0xFFFFFFFE)

    def voce(nome, tipo, figlio, inizio, lungo):
        v = bytearray(128)
        b = nome.encode("utf-16-le")
        v[0:len(b)] = b
        v[0x40:0x42] = u16(len(b) + 2)
        v[0x42] = tipo; v[0x43] = 1
        v[0x44:0x48] = u32(0xFFFFFFFF); v[0x48:0x4C] = u32(0xFFFFFFFF); v[0x4C:0x50] = u32(figlio)
        v[0x74:0x78] = u32(inizio); v[0x78:0x7C] = u32(lungo)
        return v
    cartella = voce("Root Entry", 5, 1, 0xFFFFFFFE, 0) + voce("WordDocument", 2, 0xFFFFFFFF, 2, len(flusso))
    cartella += bytearray(SETT - len(cartella))
    for i in (2, 3):                        # the two empty entries
        cartella[i * 128 + 0x44:i * 128 + 0x50] = u32(0xFFFFFFFF) * 3
    return bytes(testa + fat + cartella) + flusso


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("uso: doc95-genera.py out.doc [-complesso]")
    with open(sys.argv[1], "wb") as f:
        f.write(ole(costruisci("-complesso" in sys.argv)))

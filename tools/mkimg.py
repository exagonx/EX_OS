#!/usr/bin/env python3
# =============================================================================
# tools/mkimg.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
# This file is part of EX-OS, distributed under the GNU GPL v2.
# See the LICENSE file in the project root for the full license text.
# =============================================================================
#
# Genera le immagini con cui si prova lib/eximg — PNG e ICO — e accanto a
# ognuna il file dei pixel attesi (.raw, ARGB a 32 bit).
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
import struct, zlib, sys, os, math


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


# -----------------------------------------------------------------------------
# ICO — un contenitore di piu' immagini
#
# ! LE VOCI SI SCRIVONO IN ORDINE SPARSO APPOSTA, e la piu' grande non e' ne'
#   la prima ne' l'ultima: e' l'unico modo di provare che il lettore SCEGLIE
#   invece di prendere quella che capita. Con le voci ordinate, un lettore che
#   piglia la prima e uno che piglia la piu' grande danno lo stesso risultato.
#
# ! E L'ALTEZZA NELL'INTESTAZIONE E' IL DOPPIO DI QUELLA VERA, perche' il DIB
#   descrive i colori e sotto la maschera di trasparenza. E' il dettaglio che
#   si sbaglia leggendo, quindi il generatore lo fa per bene: se lo sbagliasse
#   anche lui, i due errori si annullerebbero e la prova passerebbe.
# -----------------------------------------------------------------------------
def dib(larg, alt, bit, tavolozza=None):
    """Il pezzo di BMP che sta dentro un ICO: intestazione da 40 byte,
    tavolozza, pixel sottosopra, maschera AND tutta opaca."""
    n_tav = len(tavolozza) if tavolozza else 0
    intest = struct.pack("<IiiHHIIiiII", 40, larg, alt * 2, 1, bit,
                         0, 0, 0, 0, n_tav, 0)

    tav = b""
    if tavolozza:
        for (r, g, b) in tavolozza:
            tav += bytes((b, g, r, 0))          # BGRA, come vuole il DIB

    riga = ((larg * bit + 31) // 32) * 4
    px = bytearray()
    for y in range(alt - 1, -1, -1):            # sottosopra
        linea = bytearray()
        if bit == 32 or bit == 24:
            for x in range(larg):
                r, g, b = pattern(x, y)
                linea += bytes((b, g, r)) + (b"\x00" if bit == 32 else b"")
        elif bit == 8:
            for x in range(larg):
                linea.append((x + y) % n_tav)
        elif bit == 4:
            for x in range(0, larg, 2):
                a = (x + y) % n_tav
                b_ = (x + 1 + y) % n_tav if x + 1 < larg else 0
                linea.append((a << 4) | b_)
        else:                                   # 1 bit
            for x in range(0, larg, 8):
                v = 0
                for k in range(8):
                    if x + k < larg and ((x + k + y) % n_tav):
                        v |= 1 << (7 - k)
                linea.append(v)
        linea += b"\x00" * (riga - len(linea))
        px += linea

    # La maschera AND: un bit per pixel, tutta a zero = tutto opaco.
    riga_m = ((larg + 31) // 32) * 4
    maschera = b"\x00" * (riga_m * alt)

    return intest + tav + bytes(px) + maschera


def pixel_dib(larg, alt, bit, tavolozza=None):
    raw = bytearray()
    for y in range(alt):
        for x in range(larg):
            if bit in (24, 32):
                r, g, b = pattern(x, y)
            elif bit == 8:
                r, g, b = tavolozza[(x + y) % len(tavolozza)]
            elif bit == 4:
                r, g, b = tavolozza[(x + y) % len(tavolozza)]
            else:
                r, g, b = tavolozza[1 if ((x + y) % len(tavolozza)) else 0]
            raw += struct.pack("<I", (r << 16) | (g << 8) | b)
    return bytes(raw)


def scrivi_ico(percorso, voci, quale_attesa):
    """`voci` e' una lista di byte gia' pronti (DIB o PNG); `quale_attesa` e'
    l'indice di quella che il lettore deve scegliere."""
    testa = struct.pack("<HHH", 0, 1, len(voci))
    off = 6 + 16 * len(voci)
    dir_ = b""
    corpo = b""
    for (w, h, bit, dati) in voci:
        dir_ += struct.pack("<BBBBHHII",
                            w if w < 256 else 0, h if h < 256 else 0,
                            0, 0, 1, bit, len(dati), off)
        corpo += dati
        off += len(dati)

    with open(percorso, "wb") as f:
        f.write(testa + dir_ + corpo)
    return len(testa + dir_ + corpo)


# -----------------------------------------------------------------------------
# JPEG baseline
#
# ! QUI IL CONFRONTO NON PUO' ESSERE «IDENTICO», E VA DETTO PERCHE'. Il JPEG
#   perde: la trasformata si arrotonda all'andata e al ritorno, e con il
#   sottocampionamento il colore viene proprio buttato via. Quello che si puo'
#   pretendere e' che l'immagine torni VICINA a quella di partenza, e quanto
#   vicina lo decide la tabella di quantizzazione — che qui si mette a uno, cosi'
#   l'unico errore rimasto e' quello di arrotondamento.
#
# ! E L'ENCODER E' SCRITTO QUI INVECE DI USARNE UNO FATTO, per la stessa ragione
#   per cui il pattern e' una formula: un file prodotto da una libreria che non
#   si legge prova che i due programmi si capiscono, non che il nostro e'
#   giusto. Questo scrive esattamente cio' che dice la specifica, e si vede.
# -----------------------------------------------------------------------------
DC_LUM_BITS = [0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0]
DC_LUM_VAL  = list(range(12))
DC_CRO_BITS = [0,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0]
DC_CRO_VAL  = list(range(12))

AC_LUM_BITS = [0,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d]
AC_LUM_VAL  = [
 0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
 0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
 0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
 0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
 0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
 0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
 0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
 0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
 0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
 0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
 0xf9,0xfa]

AC_CRO_BITS = [0,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77]
AC_CRO_VAL  = [
 0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
 0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
 0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
 0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
 0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
 0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
 0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
 0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
 0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
 0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
 0xf9,0xfa]

ZIGZAG = [ 0, 1, 8,16, 9, 2, 3,10,17,24,32,25,18,11, 4, 5,
          12,19,26,33,40,48,41,34,27,20,13, 6, 7,14,21,28,
          35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
          58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63]


def codici(bits, vals):
    """Da «quanti codici per lunghezza» piu' l'elenco dei valori alla tabella
    valore -> (codice, quanti bit). E' la stessa costruzione che il lettore fa
    al contrario con mincode/maxcode: se una delle due sbaglia, non si capiscono
    e la prova lo dice subito."""
    fuori, codice, k = {}, 0, 0
    for l in range(1, 17):
        for _ in range(bits[l]):
            fuori[vals[k]] = (codice, l)
            codice += 1
            k += 1
        codice <<= 1
    return fuori


class Bits:
    """! IL RIEMPIMENTO FINALE E' A UNO, NON A ZERO, e i byte 0xFF vogliono uno
    zero dietro. Sono le due cose che un encoder scritto in fretta sbaglia, e
    tutt'e due si vedono solo all'ultimo blocco o su certe immagini."""
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def scrivi(self, valore, quanti):
        for i in range(quanti - 1, -1, -1):
            self.acc = (self.acc << 1) | ((valore >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc & 0xFF)
                if (self.acc & 0xFF) == 0xFF:
                    self.out.append(0)
                self.acc = 0
                self.n = 0

    def chiudi(self):
        while self.n:
            self.scrivi(1, 1)
        return bytes(self.out)


def fdct(blocco):
    """La DCT diretta, dalla definizione. Lenta e leggibile: e' un generatore
    di prove, non un encoder da usare."""
    fuori = [0.0] * 64
    for v in range(8):
        for u in range(8):
            s = 0.0
            for y in range(8):
                for x in range(8):
                    s += (blocco[y*8+x] - 128) * \
                         math.cos((2*x+1)*u*math.pi/16) * \
                         math.cos((2*y+1)*v*math.pi/16)
            cu = 1/math.sqrt(2) if u == 0 else 1.0
            cv = 1/math.sqrt(2) if v == 0 else 1.0
            fuori[v*8+u] = s * cu * cv / 4
    return fuori


def categoria(v):
    a, s = abs(v), 0
    while a:
        a >>= 1
        s += 1
    return s


def scrivi_blocco(bw, blocco, dc_prec, tab_dc, tab_ac):
    coef = fdct(blocco)
    q = [int(round(coef[ZIGZAG[i]])) for i in range(64)]   # quantizzazione a 1

    diff = q[0] - dc_prec
    s = categoria(diff)
    c, l = tab_dc[s]
    bw.scrivi(c, l)
    if s:
        bw.scrivi(diff if diff > 0 else diff + (1 << s) - 1, s)

    corsa = 0
    for i in range(1, 64):
        if q[i] == 0:
            corsa += 1
            continue
        while corsa > 15:
            c, l = tab_ac[0xF0]
            bw.scrivi(c, l)
            corsa -= 16
        s = categoria(q[i])
        c, l = tab_ac[(corsa << 4) | s]
        bw.scrivi(c, l)
        bw.scrivi(q[i] if q[i] > 0 else q[i] + (1 << s) - 1, s)
        corsa = 0
    if corsa:
        c, l = tab_ac[0x00]
        bw.scrivi(c, l)

    return q[0]


def scrivi_jpg(percorso, larg, alt, sotto=False, grigio=False, restart=0):
    """`sotto` mette il croma a meta' risoluzione in tutt'e due i versi (4:2:0);
    `restart` scrive un marcatore di ripartenza ogni tante MCU."""
    # I piani, in YCbCr.
    Y = [[0]*larg for _ in range(alt)]
    Cb = [[128]*larg for _ in range(alt)]
    Cr = [[128]*larg for _ in range(alt)]
    for y in range(alt):
        for x in range(larg):
            r, g, b = pattern(x, y)
            Y[y][x]  = int(round( 0.299*r + 0.587*g + 0.114*b))
            Cb[y][x] = int(round(-0.168736*r - 0.331264*g + 0.5*b + 128))
            Cr[y][x] = int(round( 0.5*r - 0.418688*g - 0.081312*b + 128))

    hY, vY = (2, 2) if sotto else (1, 1)
    comps = [(1, hY, vY, 0, Y)] if grigio else \
            [(1, hY, vY, 0, Y), (2, 1, 1, 1, Cb), (3, 1, 1, 1, Cr)]
    hmax = max(c[1] for c in comps)
    vmax = max(c[2] for c in comps)

    def campiona(piano, cx, cy, h, v):
        """Prende un campione del piano tenendo conto del campionamento."""
        x = min(larg - 1, cx * hmax // h)
        y = min(alt - 1, cy * vmax // v)
        return piano[y][x]

    mcux = (larg + hmax*8 - 1) // (hmax*8)
    mcuy = (alt + vmax*8 - 1) // (vmax*8)

    bw = Bits()
    tdc = [codici(DC_LUM_BITS, DC_LUM_VAL), codici(DC_CRO_BITS, DC_CRO_VAL)]
    tac = [codici(AC_LUM_BITS, AC_LUM_VAL), codici(AC_CRO_BITS, AC_CRO_VAL)]
    prec = [0] * len(comps)
    pezzi = []
    rst = 0
    contate = 0

    for my in range(mcuy):
        for mx in range(mcux):
            if restart and contate == restart:
                pezzi.append(bw.chiudi())
                pezzi.append(bytes((0xFF, 0xD0 + (rst & 7))))
                rst += 1
                bw = Bits()
                prec = [0] * len(comps)
                contate = 0

            for ci, (cid, h, v, tq, piano) in enumerate(comps):
                for by in range(v):
                    for bx in range(h):
                        blocco = []
                        for yy in range(8):
                            for xx in range(8):
                                cx = (mx*h + bx)*8 + xx
                                cy = (my*v + by)*8 + yy
                                blocco.append(campiona(piano, cx, cy, h, v))
                        prec[ci] = scrivi_blocco(
                            bw, blocco, prec[ci],
                            tdc[0 if ci == 0 else 1],
                            tac[0 if ci == 0 else 1])
            contate += 1
    pezzi.append(bw.chiudi())
    dati = b"".join(pezzi)

    # I marcatori.
    out = b"\xff\xd8"
    out += b"\xff\xdb" + struct.pack(">HB", 2 + 65, 0) + bytes([1]*64)
    if not grigio:
        out += b"\xff\xdb" + struct.pack(">HB", 2 + 65, 1) + bytes([1]*64)

    sof = struct.pack(">BHHB", 8, alt, larg, len(comps))
    for (cid, h, v, tq, _) in comps:
        sof += bytes((cid, (h << 4) | v, tq))
    out += b"\xff\xc0" + struct.pack(">H", 2 + len(sof)) + sof

    for (classe, id_, bits, vals) in ((0, 0, DC_LUM_BITS, DC_LUM_VAL),
                                      (1, 0, AC_LUM_BITS, AC_LUM_VAL),
                                      (0, 1, DC_CRO_BITS, DC_CRO_VAL),
                                      (1, 1, AC_CRO_BITS, AC_CRO_VAL)):
        if grigio and id_ == 1:
            continue
        t = bytes(((classe << 4) | id_,)) + bytes(bits[1:17]) + bytes(vals)
        out += b"\xff\xc4" + struct.pack(">H", 2 + len(t)) + t

    if restart:
        out += b"\xff\xdd" + struct.pack(">HH", 4, restart)

    sos = bytes((len(comps),))
    for ci, (cid, h, v, tq, _) in enumerate(comps):
        sos += bytes((cid, 0x00 if ci == 0 else 0x11))
    sos += bytes((0, 63, 0))
    out += b"\xff\xda" + struct.pack(">H", 2 + len(sos)) + sos
    out += dati + b"\xff\xd9"

    with open(percorso, "wb") as f:
        f.write(out)

    # I pixel di partenza: il confronto e' a tolleranza, non identico.
    raw = bytearray()
    for y in range(alt):
        for x in range(larg):
            r, g, b = pattern(x, y)
            if grigio:
                r = g = b = Y[y][x]
            raw += struct.pack("<I", (r << 16) | (g << 8) | b)
    with open(percorso[:-4] + ".raw", "wb") as f:
        f.write(raw)

    return len(out)


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

    # --- gli ICO ---------------------------------------------------------
    # Tre voci in ordine sparso: la piu' grande sta in mezzo.
    ico = [(16, 16, 8,  dib(16, 16, 8, tav)),
           (48, 48, 24, dib(48, 48, 24)),
           (32, 32, 32, dib(32, 32, 32))]
    n = scrivi_ico(os.path.join(fuori, "icona.ico"), ico, 1)
    open(os.path.join(fuori, "icona.raw"), "wb").write(pixel_dib(48, 48, 24))
    print("%-16s %3dx%-3d 3 voci  %5d byte  (attesa: la 48x48 a 24 bit)"
          % ("icona.ico", 48, 48, n))

    # Le profondita' basse, che nei file veri sono le voci piccole.
    ico4 = [(24, 24, 4, dib(24, 24, 4, tav)),
            (12, 12, 1, dib(12, 12, 1, tav[:2]))]
    n = scrivi_ico(os.path.join(fuori, "icona4.ico"), ico4, 0)
    open(os.path.join(fuori, "icona4.raw"), "wb").write(pixel_dib(24, 24, 4, tav))
    print("%-16s %3dx%-3d 2 voci  %5d byte  (attesa: la 24x24 a 4 bit)"
          % ("icona4.ico", 24, 24, n))

    # --- i JPEG ----------------------------------------------------------
    for nome, w, h, kw in (("jpg444.jpg", 32, 24, {}),
                           ("jpg420.jpg", 32, 24, {"sotto": True}),
                           ("jpggrigio.jpg", 24, 16, {"grigio": True}),
                           ("jpgrst.jpg", 32, 24, {"restart": 2}),
                           ("jpgbordo.jpg", 21, 13, {"sotto": True})):
        n = scrivi_jpg(os.path.join(fuori, nome), w, h, **kw)
        note = ",".join(kw.keys()) if kw else "4:4:4"
        print("%-16s %3dx%-3d %-10s %5d byte" % (nome, w, h, note, n))

    # Un PNG dentro un ICO: dal 2007 e' come si fanno le icone grandi.
    dentro = open(os.path.join(fuori, "rgb.png"), "rb").read()
    icop = [(16, 16, 8, dib(16, 16, 8, tav)),
            (64, 48, 32, dentro)]
    n = scrivi_ico(os.path.join(fuori, "iconapng.ico"), icop, 1)
    open(os.path.join(fuori, "iconapng.raw"), "wb").write(
        open(os.path.join(fuori, "rgb.raw"), "rb").read())
    print("%-16s %3dx%-3d 2 voci  %5d byte  (attesa: il PNG dentro)"
          % ("iconapng.ico", 64, 48, n))


if __name__ == "__main__":
    main()

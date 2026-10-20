#!/usr/bin/env python3
# =============================================================================
# tools/fette.py — la mappa delle librerie, LETTA invece che scritta a mano
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
#     python3 tools/fette.py            controlla e stampa la mappa
#     python3 tools/fette.py --libera   stampa solo il prossimo indirizzo libero
#
# ! ESISTE PERCHE' LA MAPPA SCRITTA A MANO HA SBAGLIATO TRE VOLTE. Stava in
# testa a lib/exwin/exwin.ld, diceva quali fette erano occupate, e per tre
# volte ha detto «libere» di fette che erano gia' assegnate — l'ultima il 26
# agosto 2026, con excss.so dentro «0x05C00000 libere». Una fetta assegnata due
# volte non da' un errore di collegamento: da' due librerie che si
# sovrascrivono dentro il processo che le apre entrambe, cioe' un guasto che si
# manifesta lontanissimo da dove e' stato fatto.
#
# ! LA VERITA' STA NEI FILE .ld E NEGLI ELF COSTRUITI, non in un commento. Qui
# si LEGGE: la base da ogni .ld, la misura vera dal .so quando c'e'. Un
# commento non si puo' controllare; questi due si'.
#
# ! E CONTROLLA LA MISURA, NON SOLO LA BASE. Due basi distinte non bastano: una
# libreria che cresce oltre la propria fetta entra in quella dopo, e il primo
# processo che le apre tutt'e due si porta via mezza libreria. E' l'unico
# controllo che diventera' importante da solo, quando arrivera' una libreria
# grossa — un motore JavaScript non sta in quattro megabyte per definizione.
# =============================================================================

import os
import re
import struct
import sys

RADICE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# I confini della fascia: gli stessi di kernel/loader/lib.c, e se qui e la'
# divergessero questo strumento direbbe che va tutto bene mentre il kernel
# rifiuta di caricare. Si leggono da li'.
def confini():
    testo = open(os.path.join(RADICE, "kernel/loader/lib.c"),
                 encoding="utf-8", errors="replace").read()
    def prendi(nome):
        m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+)" % nome, testo)
        if not m:
            sys.exit("fette: %s non si trova in kernel/loader/lib.c" % nome)
        return int(m.group(1), 16)
    return prendi("LIB_SPAZIO_BASE"), prendi("LIB_SPAZIO_FINE")


def librerie():
    """Ogni .ld sotto lib/ che dichiara una base dentro la fascia."""
    base_f, fine_f = confini()
    fuori = []
    dentro = []

    for radice, _, file in os.walk(os.path.join(RADICE, "lib")):
        for f in file:
            if not f.endswith(".ld"):
                continue
            perc = os.path.join(radice, f)
            testo = open(perc, encoding="utf-8", errors="replace").read()
            m = re.search(r"^\s*\.\s*=\s*(0x[0-9A-Fa-f]+)\s*;", testo, re.M)
            if not m:
                continue
            base = int(m.group(1), 16)
            nome = f[:-3]
            (dentro if base_f <= base < fine_f else fuori).append((base, nome, perc))

    return sorted(dentro), fuori


def misura(nome):
    """Quanto occupa DAVVERO la libreria costruita: l'ultimo indirizzo di un
    PT_LOAD meno il primo. Se non e' stata costruita rende None — e allora si
    controlla la sola base, dicendolo."""
    # ! L'ORDINE CONTA, e la prima stesura lo aveva sbagliato: guardando prima
    # in build/lib si leggevano copie VECCHIE e lo strumento gridava a
    # sovrapposizioni che non c'erano piu'. Prima le directory dove il
    # collegamento scrive adesso, poi le altre.
    for dove in ("build/exwin/lib", "build/lib", "build",
                 "build/iso-exos/exwin/lib", "build/iso-exos/lib"):
        perc = os.path.join(RADICE, dove, nome + ".so")
        if os.path.exists(perc):
            break
    else:
        return None

    with open(perc, "rb") as fh:
        d = fh.read()
    if d[:4] != b"\x7fELF":
        return None
    phoff, = struct.unpack_from("<I", d, 28)
    phentsize, phnum = struct.unpack_from("<HH", d, 42)
    lo, hi = None, 0
    for i in range(phnum):
        o = phoff + i * phentsize
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz = \
            struct.unpack_from("<IIIIII", d, o)
        if p_type != 1 or p_memsz == 0:      # PT_LOAD
            continue
        lo = p_vaddr if lo is None else min(lo, p_vaddr)
        hi = max(hi, p_vaddr + p_memsz)
    if lo is None:
        return None
    return lo, hi


def main():
    base_f, fine_f = confini()
    dentro, fuori = librerie()
    guai = 0

    if "--libera" in sys.argv:
        # Il primo indirizzo allineato a 1 MB dopo l'ultima libreria.
        ultimo = base_f
        for base, nome, _ in dentro:
            m = misura(nome)
            fine = m[1] if m else base + 0x400000
            ultimo = max(ultimo, fine)
        print("0x%08X" % ((ultimo + 0xFFFFF) & ~0xFFFFF))
        return 0

    print("Fascia delle librerie: 0x%08X - 0x%08X (%d MB)"
          % (base_f, fine_f, (fine_f - base_f) // (1024 * 1024)))
    print()
    print("  %-10s %-12s %-12s %s" % ("libreria", "base", "occupa", "nota"))

    occupato = 0
    prec_fine, prec_nome = None, None

    for base, nome, perc in dentro:
        m = misura(nome)
        if m is None:
            nota = "non costruita: controllata la sola base"
            fine = base
        else:
            lo, hi = m
            fine = hi
            occupato += hi - lo
            nota = ""
            if lo != base:
                nota = "! il .so parte a 0x%08X, il .ld dice 0x%08X" % (lo, base)
                guai += 1

        if prec_fine is not None and base < prec_fine:
            nota = "! SI SOVRAPPONE a %s (che finisce a 0x%08X)" % (prec_nome,
                                                                    prec_fine)
            guai += 1
        if fine > fine_f:
            nota = "! ESCE DALLA FASCIA"
            guai += 1

        print("  %-10s 0x%08X   %-12s %s"
              % (nome, base,
                 "-" if m is None else "%d KB" % ((m[1] - m[0] + 1023) // 1024),
                 nota))
        prec_fine, prec_nome = fine, nome

    for base, nome, perc in fuori:
        print("  %-10s 0x%08X   %-12s (fuori dalla fascia: e' un programma?)"
              % (nome, base, "-"))

    print()
    libero = fine_f - (prec_fine if prec_fine else base_f)
    print("  librerie in fascia: %d    spazio in coda: %d MB"
          % (len(dentro), libero // (1024 * 1024)))

    if guai:
        print()
        print("  ! %d problema/i: vedi le righe con '!'" % guai)
        return 1

    print("  [OK] nessuna sovrapposizione, tutte dentro la fascia")
    return 0


if __name__ == "__main__":
    sys.exit(main())

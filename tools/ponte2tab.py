#!/usr/bin/env python3
# =============================================================================
# tools/ponte2tab.py - la ricetta del PONTE VIDEO, presa da due referti
#
# EX-OS - Extensible Operating System
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Stessa idea di sonda2tab.py, applicata all'altra meta' della scheda.
#
#   ponte2tab.py <bersaglio.TXT> <partenza.TXT> <nome>  >  ponte_tab.h
#
# Legge i banchi Part1..Part5 di due referti presi con `sonda.drv -ponte` e
# rende i registri che CAMBIANO, con il valore del bersaglio. Cinque banchi da
# 128 indici sono 640 registri: scriverli tutti vorrebbe dire riscrivere anche
# i contatori e i registri di stato, e nessuno sa quali siano.
#
# ! QUI IL RUMORE NON E' STATO MISURATO, e va detto. Per i banchi VGA ci sono
# voluti sei referti per capire che trentatre registri cambiano da soli; del
# ponte ne abbiamo uno per stato, quindi la lista che esce di qui e' «cio' che
# e' diverso», non ancora «cio' che conta». Quattro sono gia' sospetti perche'
# cambiano anche fra due letture dello STESSO stato (P131, P132, P146, P147,
# confrontando PONTEX con PONTEG): sono segnati.
#
# ! E GLI OFFSET VENGONO DA sisfb, NON DA UN DOCUMENTO SiS. Part1 e' la coppia
# indice/dato a +04/+05 nella finestra rilocata, Part2 a +10, Part3 a +12,
# Part4 a +14, Part5 a +16.
import re
import sys

SOSPETTI = {"P131", "P132", "P146", "P147"}

# ! UNDICI DI QUESTI LA SCHEDA NON LI ACCETTA, ed e' una misura, non un
# sospetto: si e' scritta la tabella, si e' riletto il ponte (PONTEY.TXT) e
# questi undici si rileggono al valore di prima. Sono registri di sola
# lettura, o protetti da uno sblocco che non conosciamo — nel driver di Linux
# c'e' una SiS_UnLockCRT2() che accende un bit di Part1 prima di scrivere, e
# potrebbe essere quella la porta.
#
# Si scrivono lo stesso, perche' scriverli non fa niente e il giorno che lo
# sblocco si trova devono essere gia' qui. Ma sono segnati: finche' restano
# segnati, il ponte e' impostato a meta', e mezza scalatura non e' meglio di
# nessuna scalatura — e' peggio.
NON_ACCETTATI = {"P102", "P130", "P131", "P132", "P133", "P134",
                 "P146", "P147", "P14a", "P14b", "P403"}


def banchi(percorso):
    d = {}
    for r in open(percorso, errors="replace"):
        m = re.match(r"^(P[1-5]) ([0-9a-f]{2}): (.*)$", r.strip())
        if m:
            b, base, vals = m.group(1), int(m.group(2), 16), m.group(3).split()
            for k, v in enumerate(vals):
                d["%s%02x" % (b, base + k)] = int(v, 16)
    return d


def e_un_banco(d, parte):
    """Un banco indicizzato rende valori diversi a indici diversi.

    ! PART5 NON E' UN BANCO SU QUESTA SCHEDA, e la lettura lo grida: 128
    indici, 128 volte lo stesso byte. La coppia a +16/+17 e' altro - due
    registri normali, o niente. Scriverci «indice 0x37, dato 0x06» significa
    infilare quei due byte in due registri veri, alla cieca. E' successo, e
    ha spostato lo schermo in modo inutilizzabile.

    Percio' un banco che si rilegge costante si scarta, e la regola sta qui
    invece che in una lista di eccezioni: vale anche per la prossima scheda.
    """
    v = {x for k, x in d.items() if k.startswith(parte)}
    return len(v) > 1


def main():
    if len(sys.argv) != 4:
        print("uso: ponte2tab.py <bersaglio.TXT> <partenza.TXT> <nome>",
              file=sys.stderr)
        return 1
    nome = sys.argv[3]

    ber = banchi(sys.argv[1])
    par = banchi(sys.argv[2])
    if not ber or not par:
        print("ponte2tab: nessun banco Part1..Part5 nei referti",
              file=sys.stderr)
        return 1

    print("/* Generato da tools/ponte2tab.py: NON si modifica a mano. */")
    print("/* Bersaglio: %s   Partenza: %s */" % (sys.argv[1], sys.argv[2]))
    print()
    print("static const SisPonte ponte_%s[] = {" % nome)

    scartati = [p for p in ("P1", "P2", "P3", "P4", "P5")
                if not e_un_banco(ber, p) or not e_un_banco(par, p)]
    if scartati:
        print("/* Scartati perche' si rileggono costanti, cioe' non sono")
        print(" * banchi indicizzati: %s */" % " ".join(scartati))

    n = 0
    for k in sorted(ber):
        if k not in par or ber[k] == par[k]:
            continue
        if k[:2] in scartati:
            continue
        parte, idx = int(k[1]), int(k[2:], 16)
        if k in NON_ACCETTATI:
            nota = "   /* la scheda non lo accetta: serve lo sblocco */"
        elif k in SOSPETTI:
            nota = "   /* cambia da solo: sospetto */"
        else:
            nota = ""
        print("    { %d, 0x%02x, 0x%02x },%s" % (parte, idx, ber[k], nota))
        n += 1

    print("};")
    print("#define PONTE_%s_N "
          "((int)(sizeof(ponte_%s) / sizeof(ponte_%s[0])))"
          % (nome.upper(), nome, nome))
    print()
    print("/* %d registri su 640. */" % n)
    return 0


sys.exit(main())

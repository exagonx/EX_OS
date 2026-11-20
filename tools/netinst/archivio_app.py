#!/usr/bin/env python3
# =============================================================================
# tools/netinst/archivio_app.py - the archive by application (netupdate 0.050,
# voce 107), written by tools/mknetinst.sh
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     archivio_app.py <radice> <versione-del-sistema> <app.txt-di-prima|-> <adesso>
#
# Reads <radice>/catalogo.txt and <radice>/elenco.txt as mknetinst made them
# (every package, every file) and writes:
#
#   app/<id>.txt   the files of one application: percorso TAB byte TAB sha256
#                  TAB id [TAB essenziale | TAB pezzi=N], as elenco.txt
#   app.txt        the catalogue blocks, each with four keys more:
#                    versione    the application's version (3.0.24): the
#                                `versione` key of the block, or the first
#                                number in `dice`, or the system's
#                    build       YYYYMMDDhhmm (UTC) of the last time the files
#                                of the application changed: the one of the
#                                app.txt before if app/<id>.txt is the same
#                    elenco      app/<id>.txt, elimpronta its sha256
#                    grafica     si, for an application with a menu entry
#
# ! THE BUILD IS WHAT MAKES A REBUILD ARRIVE. On 19 November 2026 every port
# was rebuilt on ld-exos.so with the same version (VLC 3.0.24): a client that
# compared versions only would have kept the old binaries, which do not run
# on the new kernel. The build moves when the list moves, by itself.
#
# ! AND THE OLD catalogo.txt AND elenco.txt SHRINK to [avvio] and [sistema]
# (the user, 20 November 2026): a netupdate before 0.050 brings the system up
# to date - the new netupdate is in it - and from the next round the new one
# reads app.txt. Kernel and system stay whole, with every file.
# =============================================================================
import hashlib
import os
import re
import sys

VECCHI = ("avvio", "sistema")     # what the netupdates before 0.050 still get


def blocchi(testo):
    """The catalogue as (header lines, [(id, [lines])])."""
    testa, v, cur = [], [], None
    for riga in testo.splitlines():
        m = re.match(r"^\[([^]]+)\]\s*$", riga)
        if m:
            cur = (m.group(1).strip(), [])
            v.append(cur)
            continue
        (testa if cur is None else cur[1]).append(riga)
    return testa, v


def chiave(righe, k):
    for r in righe:
        m = re.match(r"^\s*" + re.escape(k) + r"\s*=\s*(.*?)\s*$", r)
        if m:
            return m.group(1)
    return None


def main():
    radice, sistema, prima, adesso = sys.argv[1:5]
    cat = os.path.join(radice, "catalogo.txt")
    ele = os.path.join(radice, "elenco.txt")
    testa, pac = blocchi(open(cat, encoding="utf-8").read())

    # the build of every application in the app.txt before, with its list's sha
    vecchie = {}
    if prima != "-" and os.path.isfile(prima):
        for pid, righe in blocchi(open(prima, encoding="utf-8").read())[1]:
            vecchie[pid] = (chiave(righe, "elimpronta"), chiave(righe, "build"))

    righe_ele = [r for r in open(ele, encoding="utf-8").read().splitlines()
                 if r and not r.startswith("#")]
    per_pac = {}
    for r in righe_ele:
        c = r.split("\t")
        if len(c) >= 4:
            per_pac.setdefault(c[3], []).append(r)

    os.makedirs(os.path.join(radice, "app"), exist_ok=True)
    fuori, cambiate = [], []
    for pid, righe in pac:
        suo = os.path.join(radice, "pacchetti", pid + ".txt")
        dove = os.path.join(radice, "app", pid + ".txt")
        if os.path.isfile(suo):             # Exilla, font-cjk: a list of their own
            corpo = [r for r in open(suo, encoding="utf-8").read().splitlines()
                     if r and not r.startswith("#")]
        else:
            corpo = per_pac.get(pid, [])
        testo = ("# percorso<TAB>byte<TAB>sha256<TAB>pacchetto[<TAB>essenziale|pezzi=N]\n"
                 + "".join(r + "\n" for r in corpo))
        open(dove, "w", encoding="utf-8").write(testo)
        imp = hashlib.sha256(testo.encode("utf-8")).hexdigest()

        ver = chiave(righe, "versione")
        if not ver and pid not in VECCHI:
            m = re.search(r"\b(\d+\.\d+(?:\.\d+)*)", chiave(righe, "dice") or "")
            ver = m.group(1) if m else None
        ver = ver or sistema
        prec = vecchie.get(pid)
        if prec and prec[0] == imp and prec[1]:
            build = prec[1]
        else:
            build = adesso
            cambiate.append(pid)

        tieni = [r for r in righe
                 if not re.match(r"^\s*(elenco|elimpronta|versione|build|grafica)\s*=", r)]
        while tieni and not tieni[-1].strip():
            tieni.pop()
        blocco = ["[%s]" % pid] + tieni + [
            "versione   = %s" % ver,
            "build      = %s" % build,
            "elenco     = app/%s.txt" % pid,
            "elimpronta = %s" % imp,
        ]
        if chiave(righe, "menu"):
            blocco.append("grafica    = si")
        fuori.append("\n".join(blocco))

    app = ["# =============================================================",
           "# app.txt - the applications, for netupdate 0.050 and later",
           "#",
           "# The blocks of catalogo.txt, each with versione, build (when its",
           "# files last changed), elenco (app/<id>.txt, its files) and",
           "# elimpronta (that list's sha256). versione.txt carries the sha256",
           "# of this file (key `app`). See tools/netinst/archivio_app.py.",
           "# ============================================================="]
    open(os.path.join(radice, "app.txt"), "w", encoding="utf-8").write(
        "\n".join(app) + "\n\n" + "\n\n".join(fuori) + "\n")

    # the old archive: kernel and system only, for the netupdates before 0.050
    vecchio = [b for b in pac if b[0] in VECCHI]
    open(cat, "w", encoding="utf-8").write(
        "\n".join(testa).rstrip("\n") + "\n\n"
        + "\n\n".join("[%s]\n%s" % (pid, "\n".join(r).rstrip("\n")) for pid, r in vecchio) + "\n")
    open(ele, "w", encoding="utf-8").write(
        "# percorso<TAB>byte<TAB>sha256<TAB>pacchetto[<TAB>essenziale]\n"
        + "".join(r + "\n" for r in righe_ele if r.split("\t")[3] in VECCHI))

    print("     app.txt: %d applicazioni, build nuova per %d%s" % (
        len(pac), len(cambiate),
        (": " + " ".join(cambiate)) if 0 < len(cambiate) <= 12 else ""))


if __name__ == "__main__":
    main()

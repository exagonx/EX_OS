#!/usr/bin/env python3
# =============================================================================
# tools/netinst/controlla_dipendenze.py - does every application's chain of
# `vuole` bring the shared libraries its files ask for? (20 November 2026)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     controlla_dipendenze.py <radice>        a repository with app.txt
#
# For each application: the DT_NEEDED of its ELF files (readelf -d), and the
# package that has a file of that name. Said when that package is not in the
# chain of `vuole` (with avvio and sistema), or when no package has it.
# Exit 1 if anything is said: mknetinst prints it and goes on.
#
# ! WHY: Qt 5 linked libzstd, libpng16, libfreetype and libharfbuzz, [qt5]
# said `vuole = ssl`, and three of them had no package at all. The VLC tests
# copied them by hand; a VLC installed by netupdate did not start.
# =============================================================================
import os
import re
import subprocess
import sys

SEMPRE = {"avvio", "sistema"}


def main():
    r = sys.argv[1]
    testo = open(os.path.join(r, "app.txt"), encoding="utf-8").read()
    vuole, elenchi = {}, {}
    for m in re.finditer(r"(?ms)^\[([^]\n]+)\]\n(.*?)(?=^\[|\Z)", testo):
        pid, corpo = m.group(1), m.group(2)
        vuole[pid] = re.findall(r"(?m)^vuole\s*=\s*(\S+)", corpo)
        e = re.search(r"(?m)^elenco\s*=\s*(\S+)", corpo)
        if e:
            elenchi[pid] = os.path.join(r, e.group(1))

    file_di, dove = {}, {}
    for pid, p in elenchi.items():
        if not os.path.isfile(p):
            continue
        for riga in open(p, encoding="utf-8"):
            if riga.startswith("#") or "\t" not in riga:
                continue
            perc = riga.split("\t")[0]
            file_di.setdefault(pid, []).append(perc)
            dove.setdefault(os.path.basename(perc), set()).add(pid)

    def catena(pid, visti=None):
        visti = set() if visti is None else visti
        for v in vuole.get(pid, []):
            if v not in visti:
                visti.add(v)
                catena(v, visti)
        return visti

    detto = 0
    env = dict(os.environ, LC_ALL="C")
    for pid in sorted(file_di):
        ce = catena(pid) | SEMPRE | {pid}
        manca, ignote = {}, {}
        for perc in file_di[pid]:
            f = os.path.join(r, "file", perc)
            if not os.path.isfile(f) or os.path.islink(f):
                continue
            with open(f, "rb") as h:
                if h.read(4) != b"\x7fELF":
                    continue
            o = subprocess.run(["readelf", "-d", f], capture_output=True, text=True, env=env).stdout
            for lib in re.findall(r"Shared library: \[(.*?)\]", o):
                if lib == "ld-exos.so":
                    continue
                chi = dove.get(lib)
                if not chi:
                    ignote.setdefault(lib, perc)
                elif not chi & ce:
                    manca.setdefault(sorted(chi)[0], (lib, perc))
        for p, (lib, perc) in sorted(manca.items()):
            print("     ! %s: %s vuole %s, che e' di [%s]: manca `vuole = %s`" % (pid, perc, lib, p, p))
            detto += 1
        for lib, perc in sorted(ignote.items()):
            print("     ! %s: %s vuole %s, che nessun pacchetto ha" % (pid, perc, lib))
            detto += 1
    if detto == 0:
        print("     dipendenze: ogni libreria chiesta arriva con la sua catena di vuole")
    return 1 if detto else 0


if __name__ == "__main__":
    sys.exit(main())

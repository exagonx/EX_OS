#!/usr/bin/env python3
# tools/exwin-inglese/rinomina.py — l'API di ExWin in inglese (3 ottobre 2026)
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exwin-inglese/rinomina.py [--prova] [file...]
#
# Applica tools/exwin-inglese/mappa.txt (italiano -> inglese) ai sorgenti che
# usano ExWin: solo identificatori interi, nel codice e nei commenti. Le classi
# dei controlli passate a ex_create("...") come letterale diventano inglesi.
# Senza file, cerca da se' i sorgenti (C, C++, FreeBASIC, i manuali HTML),
# saltando cio' che e' storia: i diari, scambio.txt, le copie «pristino».
# Con --prova dice cosa cambierebbe, senza scrivere.
import os, re, sys

RADICE = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
os.chdir(RADICE)

mappa = [l.split() for l in open("tools/exwin-inglese/mappa.txt")
         if l.strip() and not l.startswith("#")]
nomi = dict(mappa)
rx = re.compile(r"(?<![A-Za-z0-9_])(" +
                "|".join(sorted(map(re.escape, nomi), key=len, reverse=True)) +
                r")(?![A-Za-z0-9_])")

CLASSI = {"finestra": "window", "pulsante": "button", "etichetta": "label",
          "testo": "textbox", "riquadro": "frame", "separatore": "separator",
          "intestazione": "header", "terminale": "terminal", "lista": "list",
          "areatesto": "textarea", "areacodice": "codearea", "spunta": "checkbox",
          "scorrimento": "scrollbar", "mdifiglio": "mdichild", "immagine": "image"}
rx_classe = re.compile(r'(ex_create\s*\(\s*")(' + "|".join(CLASSI) + r')(")')

SALTA_DIR = ("./cross_build", "./firefox-main", "./build", "./dist", "./.git",
             "./tools/exwin-inglese", "./tools/locali/estranei/pristino")
SALTA_FILE = {"./scambio.txt", "./in_lavorazione.txt", "./diario_browser.txt",
              "./RIPRENDERE.md", "./messaggio-commit.txt", "./README.md",
              "./README.en.md"}
EST = (".c", ".h", ".cpp", ".hpp", ".bi", ".bas", ".html", ".ld")

def candidati():
    for d, ds, fs in os.walk("."):
        if d.startswith(SALTA_DIR):
            ds[:] = []
            continue
        for f in fs:
            p = os.path.join(d, f)
            if p in SALTA_FILE:
                continue
            if f.endswith(EST) or d.startswith("./tools/locali/estranei/pezzi"):
                yield p

prova = "--prova" in sys.argv
file = [a for a in sys.argv[1:] if not a.startswith("--")] or list(candidati())
tot = 0
for p in file:
    s = open(p, encoding="utf-8", errors="surrogateescape").read()
    n = len(rx.findall(s))
    t = rx.sub(lambda m: nomi[m.group(1)], s)
    t, k = rx_classe.subn(lambda m: m.group(1) + CLASSI[m.group(2)] + m.group(3), t)
    if t != s:
        tot += 1
        print(f"{n:5d} nomi, {k:3d} classi  {p}")
        if not prova:
            open(p, "w", encoding="utf-8", errors="surrogateescape").write(t)
print(f"{tot} file {'da cambiare' if prova else 'cambiati'}")

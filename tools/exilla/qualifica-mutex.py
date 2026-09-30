#!/usr/bin/env python3
# =============================================================================
# tools/exilla/qualifica-mutex.py — «Mutex» diventa «js::Mutex» nei file dati
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exilla/qualifica-mutex.py firefox-main/js/src/...cpp [...]
#
# ! PERCHE': lib/include/libc.h di EX-OS dichiara nel namespace globale il tipo
# del lucchetto della libc, `Mutex` (e `Condizione`, `Semaforo`). Dentro
# SpiderMonkey, con `using namespace js`, il nome `Mutex` da solo e' allora
# ambiguo fra ::Mutex e js::Mutex. La cura vera e' in libc.h (nascondere quei
# nomi a chi vuole solo POSIX); finche' quel file e' prenotato da altri, si
# qualificano i nomi nei pochi file di SpiderMonkey che li usano scoperti.
# Si tocca solo `Mutex` isolato, non gia' qualificato, fuori dagli #include.
# =============================================================================
import os
import re
import subprocess
import sys

RADICE = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NOME = re.compile(r"(?<![\w:])Mutex(?![\w])")
for f in sys.argv[1:]:
    subprocess.run([os.path.join(RADICE, "tools", "exilla", "tocca.sh"), f], check=True, cwd=RADICE)
    p = os.path.join(RADICE, f)
    righe = open(p, encoding="utf-8").read().split("\n")
    n = 0
    for i, r in enumerate(righe):
        if r.lstrip().startswith("#"):
            continue
        nuova, k = NOME.subn("js::Mutex", r)
        righe[i] = nuova
        n += k
    open(p, "w", encoding="utf-8").write("\n".join(righe))
    print(f"{f}: {n}")

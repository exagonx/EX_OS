#!/usr/bin/env python3
# tools/exilla/campioni.py — i campioni di `regs:` (qemu_drive.py) in funzioni
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exilla/campioni.py <regs.txt> <nm del programma> <nm del kernel>
#
# Conta gli EIP e li traduce con le tabelle di `nm -n`: ring 3 col programma,
# ring 0 col kernel. Il CR3 dice di chi e' il campione (0x160000 = il kernel
# senza processo, cioe' idle).
import bisect, collections, re, subprocess, sys

def tabella(percorso):
    a, n = [], []
    for riga in open(percorso, errors="replace"):
        p = riga.split()
        if len(p) >= 3 and p[1] in "tTwW":
            a.append(int(p[0], 16)); n.append(p[2])
    return a, n

def nome(tab, x):
    a, n = tab
    i = bisect.bisect_right(a, x) - 1
    return n[i] if i >= 0 else "?"

regs = open(sys.argv[1], errors="replace").read().split("====")
prog, kern = tabella(sys.argv[2]), tabella(sys.argv[3])
conta = collections.Counter()
for b in regs:
    e = re.search(r"EIP=([0-9a-f]+)", b)
    c = re.search(r"\nCS =([0-9a-f]+)", b)
    r = re.search(r"CR3=([0-9a-f]+)", b)
    if not e:
        continue
    eip, cs, cr3 = int(e.group(1), 16), (c.group(1) if c else ""), (r.group(1) if r else "")
    if cr3 == "00160000":
        conta["[idle] " + nome(kern, eip)] += 1
    elif cs == "001b":
        conta[nome(prog, eip)] += 1
    else:
        conta["[kernel] " + nome(kern, eip)] += 1
tot = sum(conta.values())
nomi = list(conta)
dem = subprocess.run(["c++filt"], input="\n".join(nomi), capture_output=True, text=True).stdout.split("\n")
for (k, v), d in zip(conta.most_common(), [dem[nomi.index(k)] for k, _ in conta.most_common()]):
    print(f"{v:4d} {100*v/tot:5.1f}%  {d[:150]}")

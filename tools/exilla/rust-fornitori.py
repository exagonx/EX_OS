#!/usr/bin/env python3
# =============================================================================
# tools/exilla/rust-fornitori.py — EX-OS nei crate che Firefox porta con se'
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exilla/rust-fornitori.py
#
# Firefox non scarica i crate: li tiene in firefox-main/third_party/rust, e
# cargo ne controlla i file con .cargo-checksum.json. Qui si mette EX-OS in
# quelli che lo devono conoscere — la stessa regola della std, «EX-OS si
# comporta come NuttX» (tools/rust-exos/std/exos_libc.py) — e si rifanno i
# checksum dei crate toccati. Ogni file si segna prima con tools/exilla/tocca.sh,
# cosi' finisce nelle patch di tools/exilla/patch/. Si puo' rilanciare: cio'
# che e' gia' fatto non si rifa'.
# =============================================================================
import hashlib
import json
import os
import subprocess
import sys

RADICE = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FORN = os.path.join(RADICE, "firefox-main", "third_party", "rust")
sys.path.insert(0, os.path.join(RADICE, "tools", "rust-exos", "std"))
import exos_libc  # noqa: E402


def tocca_albero(cartella):
    """Segna tutti i .rs di una cartella (i nuovi li segnera' il giro dopo)."""
    file = []
    for d, _, ff in os.walk(cartella):
        file += [os.path.relpath(os.path.join(d, f), RADICE) for f in ff if f.endswith(".rs")]
    if file:
        subprocess.run([os.path.join(RADICE, "tools", "exilla", "tocca.sh")] + file, check=True)


def rifai_checksum(crate):
    """! I CHECKSUM SI RIFANNO, NON SI TOLGONO: svuotare «files» funziona, ma
    poi cargo non si accorgerebbe piu' di un file cambiato per sbaglio."""
    p = os.path.join(crate, ".cargo-checksum.json")
    subprocess.run([os.path.join(RADICE, "tools", "exilla", "tocca.sh"),
                    os.path.relpath(p, RADICE)], check=True)
    dati = json.load(open(p))
    files = {}
    for d, _, ff in os.walk(crate):
        for f in ff:
            if f == ".cargo-checksum.json":
                continue
            q = os.path.join(d, f)
            files[os.path.relpath(q, crate)] = hashlib.sha256(open(q, "rb").read()).hexdigest()
    dati["files"] = dict(sorted(files.items()))
    json.dump(dati, open(p, "w"), separators=(",", ":"))


# --- libc ----------------------------------------------------------------------
libc = os.path.join(FORN, "libc")
tocca_albero(os.path.join(libc, "src"))
subprocess.run([os.path.join(RADICE, "tools", "exilla", "tocca.sh"),
                "firefox-main/third_party/rust/libc/src/unix/exos/mod.rs"], check=True)
n = exos_libc.aggiungi_libc(os.path.join(libc, "src"))
rifai_checksum(libc)
print(f"libc: {n} punti NuttX ora valgono anche per EX-OS")

# --- getrandom: la casualita' da getentropy, errno da __errno_dove ---------------
gr = os.path.join(FORN, "getrandom")
tocca_albero(os.path.join(gr, "src"))
b = os.path.join(gr, "src", "backends.rs")
if 'target_os = "exos"' not in exos_libc.leggi(b):
    exos_libc.cambia(b, '''        target_os = "vita",
        target_os = "emscripten",
    ))] {
        mod getentropy;''', '''        target_os = "vita",
        target_os = "emscripten",
        target_os = "exos",
    ))] {
        mod getentropy;''')
u = os.path.join(gr, "src", "util_libc.rs")
if 'target_os = "exos"' not in exos_libc.leggi(u):
    exos_libc.cambia(u, '''    } else if #[cfg(target_os = "aix")] {
        use libc::_Errno as errno_location;''', '''    } else if #[cfg(target_os = "aix")] {
        use libc::_Errno as errno_location;
    } else if #[cfg(target_os = "exos")] {
        use libc::__errno_dove as errno_location;''')
# e la dipendenza da libc, che Cargo.toml attiva solo per un elenco di sistemi
c = os.path.join(gr, "Cargo.toml")
subprocess.run([os.path.join(RADICE, "tools", "exilla", "tocca.sh"),
                os.path.relpath(c, RADICE)], check=True)
if 'target_os = "exos"' not in exos_libc.leggi(c):
    exos_libc.cambia(c, 'target_os = "vita", target_os = "emscripten"))\'.dependencies.libc]',
                     'target_os = "vita", target_os = "emscripten", target_os = "exos"))\'.dependencies.libc]')
rifai_checksum(gr)
print("getrandom: getentropy, __errno_dove e la dipendenza da libc")

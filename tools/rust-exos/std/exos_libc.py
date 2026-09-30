# =============================================================================
# tools/rust-exos/std/exos_libc.py — EX-OS dentro un crate libc (e gli attrezzi
# per cambiare i sorgenti di Rust in modo controllato)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Lo usano aggiungi-exos.py (la std e il suo crate libc) e
# tools/exilla/rust-fornitori.py (i crate che Firefox porta con se'). La regola
# e le eccezioni sono spiegate in testa ad aggiungi-exos.py.
# =============================================================================
import os
import re
import sys

QUI = os.path.dirname(os.path.abspath(__file__))

NUTTX = 'target_os = "nuttx"'
TUTTI_E_DUE = 'any(target_os = "nuttx", target_os = "exos")'


def leggi(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def scrivi(p, t):
    with open(p, "w", encoding="utf-8") as f:
        f.write(t)


def cambia(p, prima, dopo, volte=1):
    t = leggi(p)
    n = t.count(prima)
    if n != volte:
        sys.exit(f"aggiungi-exos: in {p} '{prima[:60]}...' compare {n} volte, "
                 f"ne aspettavo {volte}")
    scrivi(p, t.replace(prima, dopo))


def cambia_re(p, modello, sostituto, volte):
    t = leggi(p)
    nuovo, n = re.subn(modello, sostituto, t)
    if n != volte:
        sys.exit(f"aggiungi-exos: in {p} '{modello[:60]}' compare {n} volte, "
                 f"ne aspettavo {volte}")
    scrivi(p, nuovo)


def regola(radice, salta=()):
    n = 0
    for cartella, _, file in os.walk(radice):
        for nome in file:
            if not nome.endswith(".rs"):
                continue
            p = os.path.join(cartella, nome)
            if p in salta:
                continue
            t = leggi(p)
            if NUTTX in t and TUTTI_E_DUE not in t:
                n += t.count(NUTTX)
                scrivi(p, t.replace(NUTTX, TUTTI_E_DUE))
    return n


# --- il crate libc -------------------------------------------------------------
# ! E' UNA FUNZIONE perche' serve due volte: al crate libc della std (qui sotto)
# e a quello che Firefox porta con se' in third_party/rust/libc
# (tools/exilla/rust-fornitori.py, 30 settembre 2026).
def aggiungi_libc(libc_src):
    """Mette EX-OS nel crate libc la cui cartella src/ e' `libc_src`."""
    # Il modulo nostro va scelto PRIMA di quello di NuttX: cfg_if prende il
    # primo ramo che vale, e dopo la regola anche quello di NuttX varrebbe.
    m = os.path.join(libc_src, "unix", "mod.rs")
    if '#[cfg(target_os = "exos")]' not in leggi(m):
        cambia(m, '    } else if #[cfg(target_os = "nuttx")] {\n        mod nuttx;',
               '    } else if #[cfg(target_os = "exos")] {\n        mod exos;\n'
               '        pub use self::exos::*;\n'
               '    } else if #[cfg(target_os = "nuttx")] {\n        mod nuttx;')
    os.makedirs(os.path.join(libc_src, "unix", "exos"), exist_ok=True)
    scrivi(os.path.join(libc_src, "unix", "exos", "mod.rs"),
           leggi(os.path.join(QUI, "libc-exos.rs")))
    return regola(libc_src)



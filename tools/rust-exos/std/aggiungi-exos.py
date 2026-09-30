#!/usr/bin/env python3
# =============================================================================
# tools/rust-exos/std/aggiungi-exos.py — EX-OS dentro la std e il crate libc
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     aggiungi-exos.py <cartella di lavoro>     (la chiama prepara.sh)
#
# Lavora sulle COPIE che prepara.sh mette in <lavoro>/library e <lavoro>/libc,
# mai sui sorgenti del toolchain.
#
# ! LA REGOLA E' UNA: EX-OS SI COMPORTA COME NUTTX. NuttX e' un sistema POSIX
# con i pthread e senza fork, cioe' la forma di EX-OS, e la std lo nomina gia'
# in una cinquantina di punti. Ogni `target_os = "nuttx"` diventa
#
#     any(target_os = "nuttx", target_os = "exos")
#
# che vale ovunque un predicato valga (dentro any, all, not, cfg_select). Cosi'
# lo script non dipende dai numeri di riga e regge le versioni nuove della std.
#
# ! LE ECCEZIONI sono i punti dove EX-OS NON e' NuttX, e si scrivono qui sotto
# una per una, ognuna con il suo perche'. Un'eccezione che non trova piu' il
# testo da cambiare ferma lo script: meglio un errore che una std sbagliata.
# =============================================================================
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from exos_libc import QUI, leggi, scrivi, cambia, cambia_re, regola, aggiungi_libc  # noqa: E402

LAVORO = sys.argv[1]
STD = os.path.join(LAVORO, "library", "std", "src")
LIBC = os.path.join(LAVORO, "libc", "src")
SALTA = {
    os.path.join(STD, "sys", "env_consts.rs"),
    os.path.join(STD, "sys", "random", "mod.rs"),
    os.path.join(STD, "os", "mod.rs"),
    os.path.join(STD, "os", "unix", "mod.rs"),
}
print(f"libc: {aggiungi_libc(LIBC)} punti NuttX ora valgono anche per EX-OS")

# ! errno: il crate lo cerca per nome di funzione, e ogni sistema ha il suo. Il
# nostro e' __errno_dove (lib/libc.c), lo stesso che usa la macro errno in C.
# (lo aggancia la std, in sys/pal/unix/os.rs: vedi sotto)

# --- la std ----------------------------------------------------------------------
print(f"std:  {regola(STD, SALTA)} punti NuttX ora valgono anche per EX-OS")

# Il nome del sistema, per std::env::consts::OS.
cambia(os.path.join(STD, "sys", "env_consts.rs"),
       '#[cfg(target_os = "nuttx")]\npub mod os {',
       '#[cfg(target_os = "exos")]\npub mod os {\n'
       '    pub const FAMILY: &str = "unix";\n'
       '    pub const OS: &str = "exos";\n'
       '    pub const DLL_PREFIX: &str = "lib";\n'
       '    pub const DLL_SUFFIX: &str = ".so";\n'
       '    pub const DLL_EXTENSION: &str = "so";\n'
       '    pub const EXE_SUFFIX: &str = "";\n'
       '    pub const EXE_EXTENSION: &str = "";\n'
       '}\n\n#[cfg(target_os = "nuttx")]\npub mod os {')

# La casualita': EX-OS ha getentropy (lib/libc.c), non arc4random.
cambia(os.path.join(STD, "sys", "random", "mod.rs"),
       '    target_os = "emscripten" => {\n        mod getentropy;',
       '    any(target_os = "emscripten", target_os = "exos") => {\n        mod getentropy;')

# ! LA RETE RESTA QUELLA DI NUTTX, cioe' i socket BSD della libc. EX-OS non li
# ha ancora: le funzioni ci sono e rispondono ENOSYS (lib/libc.c, @RUST-STD),
# cosi' la std si compila intera. Il ramo «non supportato» della std non si
# puo' prendere: con la famiglia unix, os::unix::net e os::fd::net danno i
# socket per scontati.

# ! DOVE EX-OS E' COME VITA E ESP-IDF, e non come NuttX:
#   - i tempi dei file sono al secondo: struct stat ha st_mtime, non st_mtim;
#   - non ci sono openat/unlinkat/fdopendir: remove_dir_all e Dir usano la
#     versione comune, che cammina per nomi;
#   - FD_CLOEXEC non ha effetto (i figli non ereditano per exec: EX-OS non ha
#     exec), quindi set_cloexec non fa niente e dup usa F_DUPFD.
FS = os.path.join(STD, "sys", "fs", "unix.rs")
cambia_re(FS, r'(target_os = "hurd",\s*)any\(target_os = "nuttx", target_os = "exos"\)',
          r'\1target_os = "nuttx"', 2)
cambia_re(FS, r'(target_os = "espidf",\s*target_os = "vita",\s*target_os = "rtems")',
          r'\1, target_os = "exos"', 2)
cambia_re(FS, r'(target_os = "vxworks",\s*target_os = "l4re",)',
          r'\1 target_os = "exos",', 3)
FD = os.path.join(STD, "sys", "fd", "unix.rs")
cambia(FD, '#[cfg(any(target_os = "espidf", target_os = "horizon", target_os = "vita"))]\n'
           '    pub fn set_cloexec',
       '#[cfg(any(target_os = "espidf", target_os = "horizon", target_os = "vita", target_os = "exos"))]\n'
       '    pub fn set_cloexec')
cambia(FD, '    #[cfg(not(any(\n        target_env = "newlib",\n        target_os = "solaris",',
       '    #[cfg(not(any(\n        target_env = "newlib",\n        target_os = "exos",\n'
       '        target_os = "solaris",')
cambia(os.path.join(STD, "os", "fd", "owned.rs"),
       'target_os = "espidf", target_os = "vita"',
       'target_os = "espidf", target_os = "vita", target_os = "exos"', 2)

# std::os::exos: le estensioni dei metadati, sui campi della NOSTRA struct stat.
os.makedirs(os.path.join(STD, "os", "exos"), exist_ok=True)
for nome in ("mod.rs", "fs.rs", "raw.rs"):
    scrivi(os.path.join(STD, "os", "exos", nome),
           leggi(os.path.join(QUI, "os-exos", nome)))
cambia(os.path.join(STD, "os", "mod.rs"),
       '#[cfg(target_os = "nuttx")]\npub mod nuttx;',
       '#[cfg(target_os = "nuttx")]\npub mod nuttx;\n#[cfg(target_os = "exos")]\npub mod exos;')
cambia(os.path.join(STD, "os", "unix", "mod.rs"),
       '    #[cfg(target_os = "nuttx")]\n    pub use crate::os::nuttx::*;',
       '    #[cfg(target_os = "nuttx")]\n    pub use crate::os::nuttx::*;\n'
       '    #[cfg(target_os = "exos")]\n    pub use crate::os::exos::*;')

# errno: la std lo chiede per nome di funzione, e il nostro e' __errno_dove
# (lib/libc.c), non il __errno di NuttX.
ERR = os.path.join(STD, "sys", "io", "error", "unix.rs")
cambia(ERR, '            any(target_os = "nuttx", target_os = "exos"),\n            target_env = "newlib"',
       '            target_os = "nuttx",\n            target_env = "newlib"')
cambia(ERR, '    #[cfg_attr(any(target_os = "solaris", target_os = "illumos"), link_name = "___errno")]',
       '    #[cfg_attr(target_os = "exos", link_name = "__errno_dove")]\n'
       '    #[cfg_attr(any(target_os = "solaris", target_os = "illumos"), link_name = "___errno")]')

# L'elenco dei sistemi che la std conosce: senza, ogni programma dovrebbe
# chiedere #![feature(restricted_std)].
cambia(os.path.join(LAVORO, "library", "std", "build.rs"),
       '        || target_os == "nuttx"\n',
       '        || target_os == "nuttx"\n        || target_os == "exos"\n')

# Il Cargo.toml della library: il crate libc e' la NOSTRA copia.
cambia(os.path.join(LAVORO, "library", "Cargo.toml"),
       '[patch.crates-io]\n',
       '[patch.crates-io]\nlibc = { path = "../libc" }\n')
print("aggiungi-exos: fatto")

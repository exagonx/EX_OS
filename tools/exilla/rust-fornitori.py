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

# --- rustix: EX-OS si comporta come ESP-IDF (tappa 6, 30 settembre 2026) --------
# ! LA STESSA REGOLA DELLA std, CON UN ALTRO MODELLO. rustix non conosce NuttX;
# nel suo ramo libc tratta da Linux ogni sistema che non ha nominato, e chiede
# al crate libc duecento nomi di Linux (AF_APPLETALK, EADV, statfs...). ESP-IDF
# e' il sistema POSIX piccolo che rustix esclude di piu' (404 punti) e quello
# con meno codice suo (5 punti, tutti innocui: F_DUPFD, il tipo dell'opcode di
# ioctl). Lo usano neqo_glue (HTTP/3) e tempfile.
def come(nome, modello, eccezioni=()):
    """In un crate, ogni target_os = "<modello>" vale anche per EX-OS, tranne nei
    file elencati (percorsi relativi al crate), che tornano come erano."""
    vecchio = 'target_os = "%s"' % modello
    nuovo = 'any(target_os = "%s", target_os = "exos")' % modello
    cr = os.path.join(FORN, nome)
    tocca_albero(cr)
    n = 0
    for d, _, ff in os.walk(cr):
        for f in ff:
            if not f.endswith(".rs"):
                continue
            q = os.path.join(d, f)
            t = exos_libc.leggi(q)
            if os.path.relpath(q, cr) in eccezioni:
                if nuovo in t:
                    open(q, "w", encoding="utf-8").write(t.replace(nuovo, vecchio))
                continue
            if vecchio in t and nuovo not in t:
                n += t.count(vecchio)
                open(q, "w", encoding="utf-8").write(t.replace(vecchio, nuovo))
    rifai_checksum(cr)
    print(f"{nome}: {n} punti {modello} ora valgono anche per EX-OS")


def come_espidf(nome, eccezioni=()):
    come(nome, "espidf", eccezioni)


# ! TRANNE I sockaddr di rustix: quelli di ESP-IDF (lwIP) sono alla BSD, con
# sa_len e la famiglia a 8 bit; quelli di EX-OS sono alla Linux (sa_family_t a
# 16 bit, niente lunghezza). In questi file ogni ramo ESP-IDF riguarda proprio
# quella forma, e EX-OS resta nel ramo generico.
come_espidf("rustix", {os.path.join("src", "backend", "libc", "net", f)
                       for f in ("addr.rs", "read_sockaddr.rs", "write_sockaddr.rs", "ext.rs")})
# socket2 (i socket di neqo, HTTP/3): i suoi rami ESP-IDF non toccano i sockaddr.
come_espidf("socket2")

# --- quinn-udp e mtu (HTTP/3 di neqo): EX-OS come Redox ---------------------------
# Tutti i punti di Redox in questi due crate sono esclusioni: niente opzioni
# avanzate dei socket UDP (pktinfo, ECN, GSO) e nessuna ricerca dell'MTU
# dell'interfaccia (mtu risponde con un errore, e neqo usa il valore prudente).
come("quinn-udp", "redox")
come("mtu", "redox")

# --- libloading: EX-OS come Redox ---------------------------------------------
# Ha una tabella di RTLD_* per sistema, con un compile_error per gli altri. I
# valori di Redox (1, 2, 0x100, 0) sono quelli di lib/include/libc.h (Linux).
# ! TRANNE src/os/unix/mod.rs, dove Redox sta fra i sistemi con dlerror sicuro
# fra fili: quella di EX-OS non lo garantisce, e li' si prende la via prudente.
come("libloading", "redox", {os.path.join("src", "os", "unix", "mod.rs")})

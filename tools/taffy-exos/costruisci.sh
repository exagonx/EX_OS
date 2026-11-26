#!/bin/bash
# =============================================================================
# tools/taffy-exos/costruisci.sh — Taffy (the layout library) for EX-OS, 32 and 64
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/taffy-exos/costruisci.sh [32|64]   both by default
#
# libtaffy_exos.so.1: Taffy 0.16.0 (Rust, MIT; flexbox, CSS grid, block)
# behind the C interface of taffy_exos/src/lib.rs. What it is for:
# debug/implementazione_BROWSER002.txt, P7. EXBrowser opens it with dlopen
# for its flex and grid containers (exwin/bin/exbrowser/browser_taffy.c);
# without it those keep the old layout.
#
# The archives, checked (sha256 equal to crates.io's index, SHA256SUMS):
# terze-parti/taffy/taffy-0.16.0.crate and crate/ (slotmap 1.1.1,
# rustc-hash 2.1.3, smallvec 1.16.6, version_check 0.9.5).
#
# ! OFFLINE: the .crate files become a vendor directory (with the
# .cargo-checksum.json cargo wants) and cargo is told to use only that.
# ! NO std: core and alloc from the nightly's sources (-Z build-std), panic
# = abort. The staticlib becomes a shared object with gcc, as every other
# library of comune.sh, exporting only tx_* (a version script): the Rust
# runtime inside stays private.
# ! The Rust toolchain is the one of tools/rust-exos (rustup in
# cross_build/<machine>/rust-macchina, the targets i686/x86_64-unknown-exos).
# =============================================================================
NOME=taffy_exos
VER=0.1.0
. "$(dirname "$0")/../librerie-exos/comune.sh"
TAF=$TP/taffy
ARCHIVIO=$TAF/taffy-0.16.0.crate

vendor() {
    local c n d somma
    rm -rf "$B/vendor"; mkdir -p "$B/vendor"
    for c in "$TAF"/taffy-0.16.0.crate "$TAF"/crate/*.crate; do
        n=$(basename "$c" .crate)
        tar xzf "$c" -C "$B/vendor" || exit 1
        somma=$(sha256sum "$c" | cut -d' ' -f1)
        grep -q "$somma" "$TAF/SHA256SUMS" 2>/dev/null || grep -q "$somma" "$c.sha256" 2>/dev/null \
            || { echo "taffy-exos: $c non corrisponde alle somme"; exit 1; }
        # the files themselves are not listed: the package checksum is enough
        printf '{"files":{},"package":"%s"}\n' "$somma" > "$B/vendor/$n/.cargo-checksum.json"
    done
}

costruisci() {
    local bersaglio macchina rb ldm
    lib_per "$1"
    case $BIT in 32) bersaglio=i686-unknown-exos ;; 64) bersaglio=x86_64-unknown-exos ;; esac
    macchina=$(hostname | tr 'A-Z' 'a-z')
    rb=$TC/$macchina
    [ -d "$rb/rust-macchina/rustup" ] || manca "Rust in $rb/rust-macchina" "tools/rust-exos/leggimi.md"
    vendor
    rm -rf "$B/crate"; cp -r "$R/tools/taffy-exos/taffy_exos" "$B/crate"
    # ! A PATCH, NOT A SOURCE REPLACEMENT: -Z build-std resolves the std's own
    # dependencies through the same sources, and a vendor directory standing
    # for crates.io hid them (rustc-literal-escaper of proc_macro). Those come
    # from cargo's local registry; ours from here.
    {
        echo
        echo "[patch.crates-io]"
        for d in "$B"/vendor/*/; do
            d=${d%/}
            echo "$(basename "$d" | sed 's/-[0-9][0-9.]*$//') = { path = \"$d\" }"
        done
    } >> "$B/crate/Cargo.toml"
    (
        export RUSTUP_HOME="$rb/rust-macchina/rustup" CARGO_HOME="$rb/rust-macchina/cargo"
        export CARGO_TARGET_DIR="$B/obj"
        export __CARGO_TESTS_ONLY_SRC_ROOT="$rb/costruzione-std/library"
        export RUSTC="$R/tools/rust-exos/rustc-exos"
        cd "$B/crate" && "$CARGO_HOME/bin/cargo" +nightly build --release --offline \
            -Z build-std=core,alloc -Z json-target-spec --target "$bersaglio" > "$B/costruisci.log" 2>&1
    ) || { grep -m20 -B3 -A10 'error' "$B/costruisci.log"; exit 1; }
    cp "$B/obj/$bersaglio/release/libtaffy_exos.a" "$DEST/lib/"
    # the shared object: only tx_* outside
    printf '{ global: tx_*; local: *; };\n' > "$B/esporta.ver"
    "$CROSS/bin/$T-gcc" -shared -Wl,-soname,libtaffy_exos.so.1 -o "$DEST/lib/libtaffy_exos.so.1" \
        -Wl,--whole-archive "$DEST/lib/libtaffy_exos.a" -Wl,--no-whole-archive $NONSHARED \
        -Wl,--version-script,"$B/esporta.ver" -Wl,--gc-sections > "$B/condivisa.log" 2>&1 \
        || { cat "$B/condivisa.log"; exit 1; }
    "$CROSS/bin/$T-strip" --strip-unneeded "$DEST/lib/libtaffy_exos.so.1"
    lib_controlla taffy_exos 1 ""
    mkdir -p "$DEST/include"
    cp -p "$R/tools/taffy-exos/taffy_exos/src/lib.rs" "$DEST/include/taffy_exos.rs"
    lib_fine
}
lib_tutte "$1"

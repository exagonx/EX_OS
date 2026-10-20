#!/bin/bash
# =============================================================================
# tools/rust-exos/rustc-nativo/prepara-sorgenti.sh — rustc's sources with EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/rust-exos/rustc-nativo/prepara-sorgenti.sh
#
# @RUST-64 R64-2 step c, on cross_build/terze-parti/rust-src/rustc-nightly-src
# (the nightly's own commit, so its library/ is the one the cross std is
# built from):
#   - library/ is put back as it came (a pristine copy, library.orig, is made
#     the first time) and the EX-OS std is added with the same script the
#     cross std uses (tools/rust-exos/std/aggiungi-exos.py): it wants the
#     libc crate in <tree>/libc, taken from the version library/Cargo.lock
#     names, out of vendor/;
#   - the target goes into the compiler (applica.py, idempotent).
# Run it again after any change to tools/rust-exos/std or to this directory.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."
RADICE="$PWD"
T="${RUSTC_SORGENTI:-$RADICE/cross_build/terze-parti/rust-src/rustc-nightly-src}"
[ -f "$T/x.py" ] || { echo "no rustc sources in $T" >&2; exit 1; }

[ -d "$T/library.orig" ] || cp -a "$T/library" "$T/library.orig"
rsync -a --delete "$T/library.orig/" "$T/library/"

VER=$(grep -A1 '^name = "libc"$' "$T/library/Cargo.lock" | sed -n 's/^version = "\(.*\)"/\1/p')
[ -d "$T/vendor/libc-$VER" ] || { echo "vendor/libc-$VER is missing" >&2; exit 1; }
rsync -a --delete "$T/vendor/libc-$VER/" "$T/libc/"

python3 tools/rust-exos/std/aggiungi-exos.py "$T"
# ! THE LOCK FILE FOLLOWS THE PATCH: library/Cargo.toml now takes libc from
# <tree>/libc, and x.py builds the std with --frozen - "cannot update the
# lock file" (16 October 2026). Updated here, offline, from vendor/.
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
export RUSTUP_HOME="${RUSTUP_HOME:-$RADICE/cross_build/$MACCHINA/rust-macchina/rustup}"
export CARGO_HOME="${CARGO_HOME:-$RADICE/cross_build/$MACCHINA/rust-macchina/cargo}"
(cd "$T/library" && RUSTC_BOOTSTRAP=1 "$HOME/.cargo/bin/cargo" +nightly update -p "libc@$VER" --offline \
    --config 'source.crates-io.replace-with="vendored-sources"' \
    --config 'source.vendored-sources.directory="../vendor"' > /dev/null 2>&1) ||
    { echo "prepara-sorgenti: library/Cargo.lock could not be updated" >&2; exit 1; }
python3 tools/rust-exos/rustc-nativo/applica.py "$T"
# the same for the compiler's workspace, whose crates now use <tree>/libc
(cd "$T" && RUSTC_BOOTSTRAP=1 "$HOME/.cargo/bin/cargo" +nightly update -p "libc@$VER" --offline \
    --config 'source.crates-io.replace-with="vendored-sources"' \
    --config 'source.vendored-sources.directory="vendor"' > /dev/null 2>&1) ||
    { echo "prepara-sorgenti: Cargo.lock could not be updated" >&2; exit 1; }
echo "prepara-sorgenti: $T has x86_64-unknown-exos (compiler and std, libc $VER)"

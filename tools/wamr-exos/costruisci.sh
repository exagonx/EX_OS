#!/bin/bash
# =============================================================================
# tools/wamr-exos/costruisci.sh — WAMR 2.4.6 as libiwasm.so.2 for EX-OS
#
# Copyright (C) 2026 Graziano Falcone
# SPDX-License-Identifier: GPL-2.0-or-later (this script; WAMR: Apache-2.0 WITH LLVM-exception)
#
#     tools/wamr-exos/costruisci.sh [32|64]   both by default
#
# The WebAssembly runtime of EXBrowser (debug/implementazione_BROWSER002.txt,
# P12, 4.7 d): the fast interpreter, no AOT and no JIT (no executable
# memory), no WASI. EXBrowser opens libiwasm.so.2 with dlopen and uses the
# standard C API of wasm.h (wasm_c_api.c): host functions with an
# environment, which JavaScript imports need. The archive: the GitHub tag
# WAMR-2.4.6 (PROVENIENZA.txt). CMakeLists.txt next to this: WAMR's own
# runtime_lib.cmake with the options.
#
# ! THE PLATFORM IS WAMR'S "linux", the POSIX one (mmap, pthread, clock),
# as for every library here (comune.sh, lib_cmake_toolchain).
# =============================================================================
NOME=wamr
VER=2.4.6
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/wasm-micro-runtime/wasm-micro-runtime-$VER.tar.gz

costruisci() {
    local obiettivo=X86_32
    lib_per "$1"
    [ "$1" = 64 ] && obiettivo=X86_64
    lib_sorgente
    lib_cmake_toolchain
    cmake -S "$R/tools/wamr-exos" -B "$B/obj" -DCMAKE_TOOLCHAIN_FILE="$B/exos.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DWAMR_ROOT_DIR="$SRC" -DWAMR_BUILD_TARGET=$obiettivo \
        -DCMAKE_C_FLAGS="-fPIC -I$R/tools/wamr-exos/ripieghi -include $R/tools/wamr-exos/ripieghi/exos_wamr.h" > "$B/configura.log" 2>&1 || { tail -20 "$B/configura.log"; exit 1; }
    make -C "$B/obj" -j"$(nproc)" > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    cp "$B/obj/libiwasm.a" "$DEST/lib/"
    cp -p "$SRC/core/iwasm/include/wasm_c_api.h" "$SRC/core/iwasm/include/wasm_export.h" "$DEST/include/"
    lib_condivisa iwasm 2 "-lm -lpthread"
    lib_fine "$R/tools/wamr-exos/CMakeLists.txt"
}
lib_tutte "$1"

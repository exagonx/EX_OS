# =============================================================================
# tools/mesa-exos/librerie-prova.sh - the shared objects a test program needs
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     . tools/mesa-exos/librerie-prova.sh
#     altri=$(librerie_prova <program> <T> <sysroot prefix> <bit> <dir>)
#
# Follows DT_NEEDED from the program through the sysroot and libm-exos,
# copies each library into <dir> and prints their paths there, for
# esegui-in-exos.sh's ALTRI. No list written by hand: a new dependency of
# Mesa comes along by itself. Used by prova-egl.sh and prova-finestra.sh.
# =============================================================================
librerie_prova() {
    local prog=$1 T=$2 X=$3 BIT=$4 D=$5 cerca altri="" fatti=" " coda f n p c
    cerca="$X/$T/lib $RADICE/cross_build/libm-exos$BIT/exos/lib"
    coda="$prog"
    while [ -n "$coda" ]; do
        f=${coda%% *}; [ "$coda" = "$f" ] && coda="" || coda=${coda#* }
        for n in $("$X/bin/$T-readelf" -d "$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
            case "$fatti" in *" $n "*) continue ;; esac
            fatti="$fatti$n "
            p=""; for c in $cerca; do [ -f "$c/$n" ] && { p="$c/$n"; break; }; done
            [ -n "$p" ] || { echo "=== [NO] $n (chiesta da $(basename "$f")) non c'e' in $cerca ===" >&2; return 1; }
            cp "$(readlink -f "$p")" "$D/$n"
            altri="$altri $D/$n"; coda="${coda:+$coda }$D/$n"
        done
    done
    echo "librerie:$fatti" >&2
    echo "$altri"
}

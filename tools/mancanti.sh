# =============================================================================
# tools/mancanti.sh - the parts a build left out, and the command that makes
# each one (voce 47, 17 November 2026)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Sourced (POSIX sh: su-cd.sh runs under sh), never run:
#
#     . tools/mancanti.sh
#     manca "OpenSSL a 64 bit" "tools/openssl-exos/costruisci.sh 64"
#
# manca prints one uniform line on stderr, "MANCA nome | comando", and when
# EXOS_MANCANTI names a file it adds the line there too, once. Until then
# mktools64.sh, mknetinst.sh and the su-cd.sh printed "no ...: comando" in
# four ways and went on: the package came out without that part.
#
#     mancanti_chiedi FILE      (bash; repo-update.sh)
#
# On a terminal asks for each line s(i) / n(o) / t(utte) / f(ine) and runs the
# commands said yes to, from the repository root; returns 0 if it ran at
# least one (the caller empties FILE, builds again and asks again), 1
# otherwise. A no is
# written in FILE.no and not asked again. Without a terminal (cron, -guarda)
# it lists and asks nothing.
# =============================================================================

manca() {
    _riga="MANCA $1 | $2"
    echo "$_riga" >&2
    if [ -n "$EXOS_MANCANTI" ]; then
        grep -qxF "$_riga" "$EXOS_MANCANTI" 2>/dev/null || echo "$_riga" >> "$EXOS_MANCANTI"
    fi
}

mancanti_chiedi() {
    local f="$1" riga nome cmd r tutte=0 fatto=1 n
    [ -s "$f" ] || return 1
    touch "$f.no"
    n=$(grep -cvxFf "$f.no" "$f")
    [ "$n" -gt 0 ] || return 1
    echo
    if [ "$n" = 1 ]; then echo "Manca una parte, col comando che la fa:"
    else echo "Mancano $n parti, ognuna col comando che la fa:"; fi
    grep -vxFf "$f.no" "$f" | sed 's/^MANCA /  /'
    if [ ! -t 0 ]; then
        echo "(senza terminale non chiedo: lancia i comandi a mano, poi rifai)"
        return 1
    fi
    echo "Per ognuna: s = la faccio, n = no, t = tutte quelle che restano, f = basta"
    # fd 3: the answers come from the terminal, the lines from the file
    while IFS= read -r riga <&3; do
        grep -qxF "$riga" "$f.no" && continue
        nome=${riga#MANCA }; nome=${nome%% | *}; cmd=${riga#* | }
        if [ $tutte = 0 ]; then
            read -r -p "  $nome  ($cmd)  [s/n/t/f] " r
            case "$r" in
                t|T) tutte=1 ;;
                s|S) ;;
                f|F) break ;;
                *)   echo "$riga" >> "$f.no"; continue ;;
            esac
        fi
        echo "=== $cmd"
        if bash -c "$cmd"; then fatto=0
        else echo "  ! '$cmd' e' fallito: $nome resta fuori"; echo "$riga" >> "$f.no"; fi
    done 3< "$f"
    return $fatto
}

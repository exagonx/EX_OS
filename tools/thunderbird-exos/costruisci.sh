#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/thunderbird-exos/costruisci.sh - Thunderbird for EX-OS (voce 103)
#
#     tools/thunderbird-exos/costruisci.sh [configure|build|package|...]
#     EXOS_ARCH=x86_64 tools/thunderbird-exos/costruisci.sh ...     64 bit
#
# Thunderbird is comm/ (mail, calendar, address book) on top of the same
# Gecko as Exilla. The sources: tools/exilla/sorgenti.sh -scarica, -prova,
# -applica thunderbird <version> - the release tarball (mozilla-central and
# comm together, signature checked) with tools/thunderbird-exos/patch, the
# whole set against that tarball (tools/exilla/patch-crea.sh thunderbird;
# the first port started from Exilla's), into thunderbird-main/. A file
# changed by hand: tools/exilla/tocca.sh thunderbird-main/... first.
# The build is Exilla's: the same compilers, Rust, clang and Node
# (tools/exilla/gecko-costruisci.sh), with mozconfig-thunderbird[64] and the
# tree thunderbird-main. Objects in cross_build/thunderbird-obj/tb[64].
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
[ -d thunderbird-main/comm/mail ] || {
    echo "manca thunderbird-main/comm/mail: prima tools/exilla/sorgenti.sh -scarica/-prova/-applica thunderbird" >&2
    exit 1
}
mkdir -p "${EXOS_FUORI}/cross_build/thunderbird-obj"
if [ "$EXOS_ARCH" = x86_64 ]; then
    M="$PWD/tools/thunderbird-exos/mozconfig-thunderbird64"
else
    M="$PWD/tools/thunderbird-exos/mozconfig-thunderbird"
fi
export EXILLA_ALBERO=thunderbird-main EXILLA_MOZCONFIG="$M"
exec tools/exilla/gecko-costruisci.sh "$@"

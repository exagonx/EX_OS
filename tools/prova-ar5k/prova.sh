#!/bin/sh
# =============================================================================
# tools/prova-ar5k/prova.sh — drivers/ar5k/ar5k_conn.c checked on the host
#
# Builds ar5k_conn.c with lib/exwifi and lib/excrypt for the host, with the
# pretend chip of finto/ar5k_hw.h instead of ath5k, and runs prova.c: a
# pretend access point that scans, authenticates, associates, runs the 4-way
# and group handshakes and exchanges encrypted frames. Exit status 0 = good.
#
# ar5k_conn.c is copied next to the pretend header first: a quoted #include
# is looked up in the including file's own directory before any -I, so
# compiled where it is it would take the real drivers/ar5k/ar5k_hw.h.
# =============================================================================
set -e
cd "$(dirname "$0")/../.."
D="${TMPDIR:-/tmp}/prova_ar5k.$$"
mkdir -p "$D"
cp drivers/ar5k/ar5k_conn.c drivers/ar5k/ar5k_conn.h "$D/"
gcc -O2 -Wall -Wno-unused-parameter -Wno-unused-function -I tools/prova-ar5k/finto -I "$D" -I drivers/net -I lib/exwifi -I lib/excrypt \
    "$D/ar5k_conn.c" tools/prova-ar5k/prova.c lib/exwifi/exwifi.c lib/excrypt/aes.c lib/excrypt/sha1.c -o "$D/prova"
r=0
"$D/prova" || r=$?
rm -rf "$D"
exit $r

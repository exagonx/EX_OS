#!/bin/sh
# =============================================================================
# tools/prova_exwifi.sh — lib/exwifi (WPA/WPA2/WEP) checked on the host
#
# Compiles lib/exwifi with lib/excrypt for the host and runs
# tools/prova_exwifi.c: the IEEE 802.11 and RFC test vectors, and the 4-way
# and group handshakes against a small authenticator. Exit status 0 = all good.
# =============================================================================
set -e
cd "$(dirname "$0")/.."
OUT="${TMPDIR:-/tmp}/prova_exwifi.$$"
gcc -O2 -Wall -Wextra -Wno-unused-parameter -I lib/excrypt -I lib/exwifi \
    tools/prova_exwifi.c lib/exwifi/exwifi.c \
    lib/excrypt/aes.c lib/excrypt/sha1.c -o "$OUT"
r=0
"$OUT" || r=$?
rm -f "$OUT"
exit $r

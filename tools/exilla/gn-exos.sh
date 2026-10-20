#!/bin/bash
# =============================================================================
# tools/exilla/gn-exos.sh — EX-OS nei moz.build generati da GN (tappa 6)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exilla/gn-exos.sh firefox-main/third_party/angle [...]
#
# I moz.build di ANGLE, abseil e WebRTC li scrive GN, con un ramo per ogni
# sistema (file generati, script, definizioni). EX-OS prende quello di OpenBSD,
# come nel resto del sistema di costruzione: CONFIG["OS_TARGET"] == "OpenBSD"
# diventa CONFIG["OS_TARGET"] in ("OpenBSD", "EXOS"). Si passa una directory
# alla volta, solo quelle che si costruiscono davvero; i file passano da
# tools/exilla/tocca.sh, quindi finiscono nelle patch.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
VECCHIO='CONFIG["OS_TARGET"] == "OpenBSD"'
for d in "$@"; do
    grep -rlF --include=moz.build "$VECCHIO" "$d" | while read -r f; do
        tools/exilla/tocca.sh "$f"
        python3 - "$f" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
s = s.replace('CONFIG["OS_TARGET"] == "OpenBSD"', 'CONFIG["OS_TARGET"] in ("OpenBSD", "EXOS")')
open(p, "w", encoding="utf-8").write(s)
PYEOF
        echo "gn-exos: $f"
    done
done

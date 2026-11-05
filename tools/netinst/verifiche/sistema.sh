# pacchetto = sistema
# =============================================================================
# tools/netinst/verifiche/sistema.sh -- every machine, after every netupdate
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Run by netupdate 0.044 with /bin/sh (see LE VERIFICHE in netupdate.c):
# 0 all is well, 1 something to do by hand (said here), 2 fixed it itself.
# ! THE KERNEL THAT RUNS MUST BE THE ONE INSTALLED. After an update that
# changed it the machine wants a reboot, and netupdate says so; this is for
# the reboot that was forgotten, or that came back on the old kernel because
# the boot sector still pointed there (voce 75, 4 November 2026).
# =============================================================================
!silenced
if [ "$NETUPDATE_RIAVVIO" = 1 ]; then
    exit 0
fi
if [ "$NETUPDATE_KERNEL" != "$NETUPDATE_SISTEMA" ]; then
    echo "  ! Gira il kernel $NETUPDATE_KERNEL, ma il sistema installato e' il $NETUPDATE_SISTEMA."
    echo "    Riavvia. Se dopo il riavvio gira ancora il $NETUPDATE_KERNEL, il settore"
    echo "    di avvio punta al kernel vecchio: netupdate -avvio, poi riavvia."
    exit 1
fi
exit 0

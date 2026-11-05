# pacchetto = valkey
# =============================================================================
# tools/netinst/verifiche/valkey.sh -- Valkey 9.1.3 (voce 33 i)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# ! VALKEY WANTS KERNEL 0.317 OR LATER: before it a pipe with O_NONBLOCK
# waited, and the server stopped answering after its first BGSAVE. The kernel
# comes with the same update: what matters is that it is the one running.
# The data directory must exist (the repository carries files, not
# directories: dati/.tieni brings it; if it went away, it is made again).
# =============================================================================
!silenced
esito=0
if [ ! -d /exos/valkey/dati ]; then
    mkdir /exos/valkey/dati
    if [ -d /exos/valkey/dati ]; then
        echo "  Valkey: creata /exos/valkey/dati (i suoi dati e dump.rdb)."
        esito=2
    else
        echo "  ! Valkey: non riesco a creare /exos/valkey/dati: crealo a mano."
        exit 1
    fi
fi
if [ ! -f /lib/libm.so.4 ]; then
    echo "  ! Valkey vuole /lib/libm.so.4, che qui non c'e': netupdate -check -yesall"
    exit 1
fi
if [ "$NETUPDATE_RIAVVIO" = 1 ] || [ "$NETUPDATE_KERNEL" != "$NETUPDATE_SISTEMA" ]; then
    echo "  ! Valkey vuole il kernel $NETUPDATE_SISTEMA (gira il $NETUPDATE_KERNEL): riavvia"
    echo "    prima di accenderlo. Con un kernel piu' vecchio della 0.317 il server"
    echo "    smette di rispondere dopo il primo BGSAVE."
    exit 1
fi
exit $esito

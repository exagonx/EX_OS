# pacchetto = vlc
# =============================================================================
# tools/netinst/verifiche/vlc.sh -- VLC 3.0.24 as on Linux (20 November 2026)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Since voce 105 (step 4) /exos/bin/vlc uses /exos/lib/libvlc.so.5 and
# libvlccore.so.9, and every module is a .so in /exos/lib/vlc/plugins. A
# disk that still has the old single-program vlc has none of them. The list
# of the modules, plugins.dat, is written on the machine by vlc-cache-gen
# (a cross build cannot run it): without it VLC opens every module at each
# start. Made here when it is missing.
# =============================================================================
!silenced
if [ ! -f /exos/lib/libvlccore.so.9 ]; then
    echo "  ! vlc: /exos/lib/libvlccore.so.9 non c'e': netupdate -install:vlc"
    exit 1
fi
if [ ! -f /exos/lib/vlc/plugins/audio_output/libaout_exos_plugin.so ]; then
    echo "  ! vlc: mancano i moduli in /exos/lib/vlc/plugins: netupdate -install:vlc"
    exit 1
fi
if [ ! -f /exos/lib/vlc/plugins/plugins.dat ]; then
    echo "  vlc: scrivo l'elenco dei moduli (vlc-cache-gen)"
    /exos/lib/vlc/vlc-cache-gen /exos/lib/vlc/plugins
fi
if [ ! -f /exos/lib/vlc/plugins/plugins.dat ]; then
    echo "  ! vlc: vlc-cache-gen non ha scritto plugins.dat: VLC parte, ma lento"
    exit 1
fi
exit 0

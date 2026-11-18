# pacchetto = qt5
# =============================================================================
# tools/netinst/verifiche/qt5.sh -- Qt 5.15.19 (18 November 2026)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Qt lives in /exos/qt5 (tools/qt5-exos). A Qt program that finds the
# libraries but not plugins/platforms/libqexwin.so stops with "Could not find
# the Qt platform plugin exwin"; without lib/fonts it opens windows with no
# text. VLC's interface (vlc -I qt) needs all three.
# =============================================================================
!silenced
if [ ! -f /exos/qt5/lib/libQt5Widgets.so.5 ]; then
    echo "  ! qt5: /exos/qt5/lib/libQt5Widgets.so.5 non c'e': netupdate -install:qt5"
    exit 1
fi
if [ ! -f /exos/qt5/plugins/platforms/libqexwin.so ]; then
    echo "  ! qt5: manca il plugin exwin (/exos/qt5/plugins/platforms): netupdate -install:qt5"
    exit 1
fi
if [ ! -f /exos/qt5/lib/fonts/DejaVuSans.ttf ]; then
    echo "  ! qt5: mancano i caratteri in /exos/qt5/lib/fonts: netupdate -install:qt5"
    exit 1
fi
exit 0

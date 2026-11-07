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
# ! THE IMAGE AND FONT LIBRARIES (6 November 2026): until then the 64-bit
# system had no /exwin/lib/eximg.so and exfont.so, and a JPG or PNG
# wallpaper, the images in Immagini and the TrueType fonts were not read.
# netupdate brings them; if they are still missing after it, it says so.
# (the EX-OS sh has if, not for: one block per library)
if [ -f /exwin/bin/pm ]; then
    if [ ! -f /exwin/lib/eximg.so ]; then
        echo "  ! Manca /exwin/lib/eximg.so: la scrivania non legge JPG e PNG."
        echo "    netupdate -check -yes, poi esci da exwin e rientra."
        exit 1
    fi
    if [ ! -f /exwin/lib/exfont.so ]; then
        echo "  ! Manca /exwin/lib/exfont.so: niente font TrueType."
        echo "    netupdate -check -yes, poi esci da exwin e rientra."
        exit 1
    fi
fi
# ! THE START MENU IN GROUPS (voce 80, 7 November 2026, netupdate 0.046):
# Applicazioni, Internet, Strumenti, Multimedia, Giochi. The old menu had
# the everyday programs at the top, outside the groups ("File manager |"
# at the start of a line); netupdate replaces the file and keeps what the
# user added (menu_ricupera), so that line cannot come back after it.
if [ -f /exwin/lib/applicazioni.txt ]; then
    if [ -f /bin/grep ]; then
        grep "^File manager  *|" /exwin/lib/applicazioni.txt > /dev/null
        if [ $? = 0 ]; then
            echo "  ! Il menu di avvio e' ancora quello senza il gruppo Applicazioni."
            echo "    netupdate -check -yes, poi riapri il menu (le tue voci restano)."
            exit 1
        fi
    fi
fi
# ! EVERY PROCESSOR AT WORK (voce 92, kernel 0.319, 7 November 2026): smp
# = 2 is the default and the cpu command places processes on processors.
# A machine updated without it has the kernel but not the command; a
# kernel.cfg with smp = 1 or 0 keeps the extra cores parked, which is right
# only on a board that hangs with them: said, not judged.
if [ -f /bin/hwinfo ]; then
    if [ ! -f /bin/cpu ]; then
        echo "  ! Manca /bin/cpu (i processi sui processori, kernel 0.319)."
        echo "    netupdate -check -yes."
        exit 1
    fi
fi
if [ -f /boot/kernel.cfg ]; then
    if [ -f /bin/grep ]; then
        grep "^ *smp *= *[01]" /boot/kernel.cfg > /dev/null
        if [ $? = 0 ]; then
            echo "  - /boot/kernel.cfg tiene fermi i core in piu' (smp = 0 o 1). Se la"
            echo "    scheda non si blocca, togli la riga: dal kernel 0.319 lavorano tutti."
        fi
    fi
fi
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

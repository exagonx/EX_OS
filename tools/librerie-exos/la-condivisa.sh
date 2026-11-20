#!/bin/bash
# =============================================================================
# tools/librerie-exos/la-condivisa.sh - a libtool .la that tells the truth
# EX-OS - Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/librerie-exos/la-condivisa.sh LIB.la SONAME
#
# A library built by a libtool that did not know exos (GCC's libstdc++,
# before lib_libtool_exos in comune.sh) has a .la with library_names='':
# "static only". When its shared object is made afterwards (cxx-condivise.sh)
# the .la must say it: libtool believes the .la over the directory, and links
# the archive into every shared object that names it.
# dlname = SONAME, library_names = SONAME and the name of its ld script
# (libNAME.so, next to it). And dependency_libs without the -L of the build
# tree it came from: an installed .la points at nothing but installed
# files, and GCC's src/.libs holds only the non-PIC libstdc++.a, which
# libtool's relink at install time found first (VLC's adaptive plugin).
# =============================================================================
LA=${1:?uso: $0 LIB.la SONAME}
SO=${2:?uso: $0 LIB.la SONAME}
[ -f "$LA" ] || { echo "la-condivisa: manca $LA" >&2; exit 1; }
D=$(dirname "$LA")
[ -f "$D/$SO" ] || { echo "la-condivisa: manca $D/$SO" >&2; exit 1; }
SCRIPT=${SO%.so.*}.so
sed -i -e "s/^dlname=.*/dlname='$SO'/" \
       -e "s/^library_names=.*/library_names='$SO $SCRIPT'/" "$LA"
grep -q "^library_names='$SO $SCRIPT'" "$LA" || { echo "la-condivisa: $LA non cambiato" >&2; exit 1; }
sed -i -E "/^dependency_libs=/ s# -L[^ ']*##g" "$LA"
grep -q "^dependency_libs=.*-L" "$LA" && { echo "la-condivisa: $LA ha ancora dei -L" >&2; exit 1; }
exit 0

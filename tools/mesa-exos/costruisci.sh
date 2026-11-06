#!/bin/bash
# =============================================================================
# tools/mesa-exos/costruisci.sh — Mesa (OpenGL, EGL, GLES) for EX-OS (voce 88)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/mesa-exos/costruisci.sh [32|64]     both by default
#
# The sources: the clone mesa/ at the project root, on the branch exos-$VER
# from the tag mesa-$VER, checked against the signed archive in
# cross_build/terze-parti/mesa (tools/aggiorna-sorgenti.sh -scarica mesa,
# then -clone mesa). The signing key is Eric Engestrom's, the Mesa release
# manager: cross_build/terze-parti/mesa/mesa-keys.asc.
#
# ! NOT WRITTEN YET (6 November 2026). First the list of what Mesa asks of
# libc, of the other libraries and of the kernel (voce 88 b), and its
# implementation; this script comes after, and until then it says so instead
# of pretending to build.
# =============================================================================
VER=26.2.6
echo "mesa-exos: la costruzione non c'e' ancora (voce 88): prima l'elenco di cio' che Mesa chiede." >&2
exit 1

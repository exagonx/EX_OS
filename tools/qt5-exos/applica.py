#!/usr/bin/env python3
# =============================================================================
# tools/qt5-exos/applica.py - teaches Qt 5.15 (qtbase) the EX-OS system
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     python3 tools/qt5-exos/applica.py <qtbase tree>
#
# (voce 85.) Qt refuses an OS it does not know ("Qt has not been ported to
# this OS"). Here: __exos__ (what the EX-OS gcc defines) becomes Q_OS_EXOS,
# and Q_OS_UNIX follows by itself, as for Haiku or the Hurd; the mkspec
# exos-g++ is copied in. Idempotent: a change already there is left alone.
#
# ! WHAT EX-OS LACKS IS IMPLEMENTED IN EX-OS, NOT PATCHED OUT HERE (the
# user's rule, 4 November 2026). This file only says "this is EX-OS".
# Qt's configure finds by itself which system functions are there.
# =============================================================================
import os
import shutil
import sys

QUI = os.path.dirname(os.path.abspath(__file__))

MODIFICHE = [
    ("src/corelib/global/qsystemdetection.h",
     "#elif defined(__HAIKU__)\n#  define Q_OS_HAIKU\n",
     "#elif defined(__HAIKU__)\n#  define Q_OS_HAIKU\n"
     "#elif defined(__exos__)\n"
     "/* EX-OS (tools/qt5-exos/applica.py): a POSIX system, Q_OS_UNIX below */\n"
     "#  define Q_OS_EXOS\n"),
]


def applica(albero):
    male = 0
    for rel, vecchio, nuovo in MODIFICHE:
        p = os.path.join(albero, rel)
        t = open(p, encoding="utf-8").read()
        if nuovo in t:
            continue
        if t.count(vecchio) != 1:
            print("  [NON APPLICABILE] %s" % rel)
            male = 1
            continue
        open(p, "w", encoding="utf-8").write(t.replace(vecchio, nuovo))
        print("  [applicata] %s" % rel)
    d = os.path.join(albero, "mkspecs", "exos-g++")
    if os.path.isdir(d):
        shutil.rmtree(d)
    shutil.copytree(os.path.join(QUI, "mkspecs", "exos-g++"), d)
    return male


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("uso: applica.py <albero di qtbase>")
    sys.exit(applica(sys.argv[1]))

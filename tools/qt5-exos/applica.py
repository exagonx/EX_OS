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
# user's rule, 4 November 2026). This file says "this is EX-OS", and
# fixes Qt where it is behind the libraries of today, which are the latest
# ones (the user's rule): OpenSSL 4 (18 November 2026, below).
# Qt's configure finds by itself which system functions are there.
# =============================================================================
import os
import shutil
import sys

QUI = os.path.dirname(os.path.abspath(__file__))

MODIFICHE = [
    # the platform plugin of ExWin (tools/qt5-exos/qpa-exwin, copied below)
    ("src/plugins/platforms/platforms.pro",
     "haiku {\n    SUBDIRS += haiku\n}\n",
     "haiku {\n    SUBDIRS += haiku\n}\n\n"
     "# EX-OS (tools/qt5-exos/applica.py): the windows of ExWin\n"
     "exos: SUBDIRS += exwin\n"),
    # OpenSSL 4 (18/11/2026): getters return const pointers, and
    # ASN1_STRING is opaque. A cast to the type Qt declares in the linked
    # wrappers (-openssl-linked), and the accessors OpenSSL has had since 1.1.
    ("src/network/ssl/qsslsocket_openssl_symbols_p.h",
     "    ret q_##func(arg) { funcret func(a); }\n",
     "    ret q_##func(arg) { funcret (ret)func(a); }\n"),
    ("src/network/ssl/qsslsocket_openssl_symbols_p.h",
     "    ret q_##func(arg1, arg2) { funcret func(a, b); }\n",
     "    ret q_##func(arg1, arg2) { funcret (ret)func(a, b); }\n"),
    ("src/network/ssl/qsslsocket_openssl_symbols.cpp",
     "    size_t lTimeLength = aTime->length;\n    char *pString = (char *) aTime->data;\n",
     "    size_t lTimeLength = ASN1_STRING_length(aTime);\n    char *pString = (char *) ASN1_STRING_get0_data(aTime);\n"),
    ("src/network/ssl/qsslsocket_openssl_symbols.cpp",
     "    if (aTime->type == V_ASN1_UTCTIME) {\n",
     "    if (ASN1_STRING_type(aTime) == V_ASN1_UTCTIME) {\n"),
    ("src/network/ssl/qsslsocket_openssl_symbols.cpp",
     "    } else if (aTime->type == V_ASN1_GENERALIZEDTIME) {\n",
     "    } else if (ASN1_STRING_type(aTime) == V_ASN1_GENERALIZEDTIME) {\n"),
    ("src/network/ssl/qsslcertificate_openssl.cpp",
     "        hexString.reserve(serialNumber->length * 3);\n"
     "        for (int a = 0; a < serialNumber->length; ++a) {\n"
     "            hexString += QByteArray::number(serialNumber->data[a], 16).rightJustified(2, '0');\n",
     "        hexString.reserve(ASN1_STRING_length(serialNumber) * 3);\n"
     "        for (int a = 0; a < ASN1_STRING_length(serialNumber); ++a) {\n"
     "            hexString += QByteArray::number(ASN1_STRING_get0_data(serialNumber)[a], 16).rightJustified(2, '0');\n"),
    ("src/network/ssl/qsslcertificate_openssl.cpp",
     "qFromBigEndian(*reinterpret_cast<quint32 *>(genName->d.iPAddress->data))",
     "qFromBigEndian(*reinterpret_cast<const quint32 *>(ASN1_STRING_get0_data(genName->d.iPAddress)))"),
    ("src/network/ssl/qsslcertificate_openssl.cpp",
     "QHostAddress(reinterpret_cast<quint8 *>(genName->d.iPAddress->data))",
     "QHostAddress(reinterpret_cast<const quint8 *>(ASN1_STRING_get0_data(genName->d.iPAddress)))"),
    ("src/network/ssl/qsslcertificate_openssl.cpp",
     "QByteArray keyid(reinterpret_cast<const char *>(auth_key->keyid->data),\n"
     "                                 auth_key->keyid->length);\n",
     "QByteArray keyid(reinterpret_cast<const char *>(ASN1_STRING_get0_data(auth_key->keyid)),\n"
     "                                 ASN1_STRING_length(auth_key->keyid));\n"),
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
    d = os.path.join(albero, "src", "plugins", "platforms", "exwin")
    if os.path.isdir(d):
        shutil.rmtree(d)
    shutil.copytree(os.path.join(QUI, "qpa-exwin"), d)
    d = os.path.join(albero, "mkspecs", "exos-g++")
    if os.path.isdir(d):
        shutil.rmtree(d)
    shutil.copytree(os.path.join(QUI, "mkspecs", "exos-g++"), d)
    return male


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("uso: applica.py <albero di qtbase>")
    sys.exit(applica(sys.argv[1]))

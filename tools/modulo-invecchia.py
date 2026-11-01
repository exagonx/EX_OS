#!/usr/bin/env python3
# =============================================================================
# tools/modulo-invecchia.py — make a kernel module say another kernel version
#
#     tools/modulo-invecchia.py FILE.mod VERSIONE
#
# Rewrites the version in the 64-byte trailer (see tools/modulo-coda.py) and
# nothing else, and its SHA-256, so the file keeps its size and gets another hash: a module
# left over from an older kernel, for the tests.
# =============================================================================
import hashlib
import sys

nome, versione = sys.argv[1], sys.argv[2].encode()
dati = bytearray(open(nome, "rb").read())
c = len(dati) - 64
if dati[c:c + 8] != b"EXOSMOD1":
    sys.exit("modulo-invecchia: %s non ha la coda di un modulo" % nome)
dati[c + 8:c + 24] = versione.ljust(16, b"\0")
dati[c + 32:c + 64] = hashlib.sha256(bytes(dati[:c + 32])).digest()
open(nome, "wb").write(dati)

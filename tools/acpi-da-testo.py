#!/usr/bin/env python3
# =============================================================================
# tools/acpi-da-testo.py — rebuild ACPI tables from `sonda.drv -acpi` output
#
#     tools/acpi-da-testo.py capture.txt outdir/
#
# Writes outdir/<SIG>[_n].dat for every "=== TABELLA" block, checks each
# table's length and checksum, and says which fail. Then: iasl -d outdir/*.dat
# =============================================================================
import os
import sys


def main():
    if len(sys.argv) != 3:
        print(__doc__ if __doc__ else "uso: acpi-da-testo.py cattura.txt dir/")
        return 2
    os.makedirs(sys.argv[2], exist_ok=True)
    visti = {}
    corr = None
    for riga in open(sys.argv[1], errors="replace"):
        riga = riga.strip()
        if riga.startswith("=== TABELLA"):
            parti = riga.split()
            corr = [parti[2], int(parti[3], 16), int(parti[4]), bytearray()]
        elif riga.startswith("=== FINE") and corr:
            sig, ind, lun, dati = corr
            n = visti.get(sig, 0)
            visti[sig] = n + 1
            nome = sig + (f"_{n}" if n else "") + ".dat"
            ok_lun = len(dati) == lun
            ok_somma = sum(dati) & 0xFF == 0
            open(os.path.join(sys.argv[2], nome), "wb").write(dati)
            print(f"{nome:12s} 0x{ind:08x} {lun:7d} byte  "
                  f"{'ok' if ok_lun and ok_somma else 'ROTTA (lunghezza ' + str(len(dati)) + ', somma ' + str(sum(dati) & 0xFF) + ')'}")
            corr = None
        elif corr is not None and riga and all(c in "0123456789abcdef" for c in riga):
            corr[3] += bytes.fromhex(riga)
    return 0


if __name__ == "__main__":
    sys.exit(main())

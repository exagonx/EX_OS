#!/usr/bin/env python3
"""Trasforma un binario in un array C incorporabile nel kernel.

Serve per i settori di avvio (MBR e settore di partizione): incorporarli
evita che l'installazione dipenda dalla presenza di due file sul supporto
di avvio. Un floppy a cui manca boothd.bin darebbe un errore a meta'
installazione, con il disco gia' modificato.

uso: bin2c.py <ingresso.bin> <uscita.c> <nome_simbolo>
"""
import sys

if len(sys.argv) != 4:
    sys.exit(__doc__)

sorgente, destinazione, nome = sys.argv[1], sys.argv[2], sys.argv[3]
dati = open(sorgente, "rb").read()

righe = []
for i in range(0, len(dati), 12):
    righe.append("    " + " ".join("0x%02x," % b for b in dati[i:i + 12]))

with open(destinazione, "w") as f:
    f.write("/* Generato da tools/bin2c.py da %s — NON modificare a mano. */\n"
            % sorgente)
    f.write("const unsigned char %s[] = {\n%s\n};\n" % (nome, "\n".join(righe)))
    f.write("const unsigned int %s_len = %u;\n" % (nome, len(dati)))

#!/usr/bin/env python3
"""Applica il bersaglio i386-exos a un albero dei sorgenti GCC.

    python3 tools/gcc-exos/applica.py /percorso/di/gcc
    python3 tools/gcc-exos/applica.py /percorso/di/gcc --togli

PERCHE' UNO SCRIPT E NON UNA PATCH. L'albero di GCC non sta in questo
repository (vedi .gitignore: sono 692 MB di codice di terzi che cambiano
di continuo), quindi le nostre modifiche vanno conservate a parte. Una
patch a contesto contro il *trunk* di GCC scade in giorni: basta che
qualcuno tocchi una riga vicina. Una sostituzione di stringhe esatte
sopravvive a tutto cio' che non tocca proprio quelle righe, e quando
invece le tocca lo dice chiaramente invece di applicarsi a meta'.

E' IDEMPOTENTE: rilanciarlo su un albero gia' modificato non fa danni e
riporta "gia' presente" per ogni pezzo.

--- LICENZE -----------------------------------------------------------

I file che questo script modifica appartengono a GCC e sono distribuiti
sotto GNU GPL versione 3 o successiva. Le modifiche qui descritte sono
anch'esse GPLv3+, come impone la licenza dell'opera che modificano, e
lo script marca ogni file toccato con una riga che dichiara la modifica
e la data — e' cio' che chiede la GPLv3 §5(a) a chi distribuisce una
versione modificata.

EX-OS di suo e' GPL-2.0-or-later: il "or later" e' cio' che rende
possibile la combinazione, e non obbliga a cambiare la licenza del resto
del sistema.

Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
"""

import os
import shutil
import sys
import time

QUI = os.path.dirname(os.path.abspath(__file__))

# Ogni voce: (file relativo all'albero GCC, testo originale, testo nuovo)
#
# Il testo originale e' abbastanza lungo da essere unico nel file: una
# stringa corta rischierebbe di combaciare in un punto che non c'entra, e
# una sostituzione nel posto sbagliato dentro config.gcc non da' un errore
# di sintassi — da' un compilatore che costruisce il bersaglio sbagliato.
MODIFICHE = [
    (
        "config.sub",
        "\t     | nsk* | powerunix* | genode* | zvmoe* | qnx* | emx* | zephyr* \\\n"
        "\t     | fiwix* )",
        "\t     | nsk* | powerunix* | genode* | zvmoe* | qnx* | emx* | zephyr* \\\n"
        "\t     | fiwix* | exos* )",
    ),
    (
        "gcc/config.gcc",
        'i[34567]86-*-elf*)\n'
        '\ttm_file="${tm_file} i386/unix.h i386/att.h elfos.h newlib-stdint.h i386/i386elf.h"\n'
        '\t;;',
        'i[34567]86-*-elf*)\n'
        '\ttm_file="${tm_file} i386/unix.h i386/att.h elfos.h newlib-stdint.h i386/i386elf.h"\n'
        '\t;;\n'
        'i[34567]86-*-exos*)\n'
        '\t# EX-OS — Extensible Operating System.  Un bersaglio ELF a 32 bit\n'
        '\t# con la propria libc: si parte da i386elf.h e si aggiunge exos.h,\n'
        '\t# che porta i predefiniti del sistema e le specs di link.\n'
        '\t#\n'
        '\t# ARCHITETTURA PREDEFINITA i486, e non i386.  Con -march=i386 la\n'
        '\t# libgcc NON COMPILA: il 386 non ha bswap, e il ripiego che GCC\n'
        '\t# emette al suo posto e\' "rolw $8, %eax", cioe\' un suffisso a 16\n'
        '\t# bit su un registro a 32 bit, che gas rifiuta.  E\' una strada che\n'
        '\t# quasi nessun bersaglio percorre piu\', e infatti e\' rotta.\n'
        '\t#\n'
        '\t# i486 e\' anche onesto rispetto alla realta\': i programmi di /bin\n'
        '\t# oggi si compilano con il gcc di sistema senza -march, quindi\n'
        '\t# assumono gia\' molto piu\' di un 386.  Questa e\' la scelta piu\'\n'
        '\t# conservativa che compili, non un innalzamento dei requisiti.\n'
        '\twith_arch=${with_arch:-i486}\n'
        '\ttm_file="${tm_file} i386/unix.h i386/att.h elfos.h newlib-stdint.h i386/i386elf.h i386/exos.h"\n'
        '\t;;',
    ),
    (
        "libgcc/config.host",
        "i[34567]86-*-elf*)",
        "i[34567]86-*-exos*)\n"
        "\t# EX-OS: identico al caso elf — stessa ABI, stesso formato.\n"
        "\textra_parts=\"$extra_parts crti.o crtbegin.o crtend.o crtn.o\"\n"
        "\ttmake_file=\"$tmake_file i386/t-crtstuff t-crtstuff-pic t-libgcc-pic\"\n"
        "\t;;\n"
        "i[34567]86-*-elf*)",
    ),
]

# File nuovi: (sorgente in questa directory, destinazione nell'albero GCC)
NUOVI = [
    ("exos.h", "gcc/config/i386/exos.h"),
]

def marca_modifica(percorso):
    """GPLv3 §5(a): una versione modificata deve portare notizie IN VISTA
    del fatto di essere stata modificata, con la data.

    La riga va in TESTA e non in coda: in coda a un file di 2000 righe non
    la vede nessuno, e "prominent notices" e' proprio il punto della
    clausola. Sotto lo shebang, se c'e', perche' quello deve restare la
    prima riga o il file smette di essere eseguibile."""
    testo = open(percorso, encoding="utf-8", errors="surrogateescape").read()
    if "Modificato per EX-OS" in testo:
        return

    riga = ("# Modificato per EX-OS (bersaglio i386-exos) il %s.\n"
            "# Le modifiche sono descritte in tools/gcc-exos/ del progetto EX-OS.\n"
            % time.strftime("%Y-%m-%d"))

    righe = testo.splitlines(keepends=True)
    dove = 1 if righe and righe[0].startswith("#!") else 0
    righe.insert(dove, riga)

    open(percorso, "w", encoding="utf-8", errors="surrogateescape").writelines(righe)


def applica(albero):
    problemi = 0

    for rel, vecchio, nuovo in MODIFICHE:
        percorso = os.path.join(albero, rel)
        if not os.path.isfile(percorso):
            print("  [ASSENTE] %s" % rel)
            problemi += 1
            continue

        testo = open(percorso, encoding="utf-8", errors="surrogateescape").read()

        if nuovo in testo:
            print("  [gia' presente] %s" % rel)
            continue

        if testo.count(vecchio) != 1:
            print("  [NON APPLICABILE] %s: il testo di riferimento compare "
                  "%d volte invece di una" % (rel, testo.count(vecchio)))
            print("     upstream ha cambiato quelle righe: va aggiornato "
                  "tools/gcc-exos/applica.py")
            problemi += 1
            continue

        open(percorso, "w", encoding="utf-8", errors="surrogateescape").write(
            testo.replace(vecchio, nuovo, 1))
        marca_modifica(percorso)
        print("  [applicato] %s" % rel)

    for sorgente, rel in NUOVI:
        percorso = os.path.join(albero, rel)
        os.makedirs(os.path.dirname(percorso), exist_ok=True)
        shutil.copyfile(os.path.join(QUI, sorgente), percorso)
        print("  [copiato] %s" % rel)

    return problemi


def togli(albero):
    """Riporta l'albero com'era. Serve per rigenerare le modifiche da capo
    dopo un aggiornamento di upstream, senza dover riclonare 692 MB."""
    problemi = 0

    for rel, vecchio, nuovo in MODIFICHE:
        percorso = os.path.join(albero, rel)
        if not os.path.isfile(percorso):
            continue
        testo = open(percorso, encoding="utf-8", errors="surrogateescape").read()
        if nuovo not in testo:
            print("  [non c'era] %s" % rel)
            continue
        testo = testo.replace(nuovo, vecchio, 1)
        # Via anche la marca di modifica — TUTTE E DUE le righe, altrimenti
        # resta un rimando a modifiche che non ci sono piu'.
        righe = [r for r in testo.splitlines(keepends=True)
                 if "Modificato per EX-OS" not in r
                 and "tools/gcc-exos/ del progetto EX-OS" not in r]
        open(percorso, "w", encoding="utf-8", errors="surrogateescape").writelines(righe)
        print("  [tolto] %s" % rel)

    for _, rel in NUOVI:
        percorso = os.path.join(albero, rel)
        if os.path.isfile(percorso):
            os.remove(percorso)
            print("  [rimosso] %s" % rel)

    return problemi


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    albero = sys.argv[1]
    smonta = "--togli" in sys.argv[2:]

    if not os.path.isfile(os.path.join(albero, "gcc", "config.gcc")):
        print("applica: '%s' non sembra un albero di GCC "
              "(manca gcc/config.gcc)" % albero)
        return 1

    print("%s il bersaglio i386-exos in %s"
          % ("Tolgo" if smonta else "Applico", albero))

    problemi = togli(albero) if smonta else applica(albero)

    if problemi:
        print("\n%d problema/i: l'albero NON e' pronto." % problemi)
        return 1

    if not smonta:
        print("""
Fatto. Per costruire il cross-compilatore:

    sudo apt install flex bison gperf texinfo m4 \\
                     libgmp-dev libmpfr-dev libmpc-dev

    mkdir ~/gcc-build && cd ~/gcc-build      # NON /tmp: e' una tmpfs, e la
                                             # build la riempie all'ultimo link
    PATH=$HOME/exos-cross/bin:$PATH %s/configure \\
        --target=i386-exos --prefix=$HOME/exos-cross \\
        --enable-languages=c --without-headers --with-newlib \\
        --disable-nls --disable-shared --disable-threads \\
        --disable-libssp --disable-libgomp --disable-libquadmath \\
        --disable-libatomic --disable-libvtv --disable-libstdcxx \\
        --disable-bootstrap
    make -j$(nproc) all-gcc && make install-gcc

I wrapper dei binutils (i386-exos-as, -ld, ...) e l'ambiente del bersaglio
(crt0.o, libc.a, header) si preparano con tools/gcc-exos/prepara-cross.sh.
""" % albero)
    return 0


if __name__ == "__main__":
    sys.exit(main())

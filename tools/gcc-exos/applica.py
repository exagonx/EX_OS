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

    # =========================================================================
    # pex-exos.c anche QUI: GCC ha la PROPRIA copia di libiberty
    #
    # Non e' la stessa di binutils, e non basta averla sistemata la'. E qui
    # conta di piu': `pex` e' il modo in cui il DRIVER lancia i propri
    # figli — cc1, as, ld — quindi senza, un gcc ospitato non compila
    # niente, non fallisce a meta': non arriva a cominciare.
    #
    # Il file e' lo stesso, e sta in tools/binutils-exos/ perche' e' li'
    # che e' nato. Il perche' esteso — spawn_ex al posto di fork, i tre
    # NULL che dichiarano l'assenza delle pipe — sta in testa a quel file.
    # =========================================================================
    (
        "libiberty/configure",
        "     *)\t\t\t\tpexecute=pex-unix   ;;\n",
        "     *-*-exos*)\t\t\tpexecute=pex-exos   ;;\n"
        "     *)\t\t\t\tpexecute=pex-unix   ;;\n",
    ),
    (
        "libiberty/configure.ac",
        "     *)\t\t\t\tpexecute=pex-unix   ;;\n",
        "     *-*-exos*)\t\t\tpexecute=pex-exos   ;;\n"
        "     *)\t\t\t\tpexecute=pex-unix   ;;\n",
    ),
    # ⚠️ La regola di compilazione va scritta a mano: la regola implicita
    # di libiberty per i .c e' `false`, apposta, perche' ogni oggetto deve
    # dichiarare le proprie dipendenze. Senza questa, il build fallisce con
    # un messaggio che dice solo "false".
    (
        "libiberty/Makefile.in",
        "./pex-unix.$(objext): $(srcdir)/pex-unix.c",
        "./pex-exos.$(objext): $(srcdir)/pex-exos.c config.h $(INCDIR)/ansidecl.h \\\n"
        "\t$(INCDIR)/libiberty.h $(srcdir)/pex-common.h\n"
        "\tif [ x\"$(PICFLAG)\" != x ]; then \\\n"
        "\t  $(COMPILE.c) $(PICFLAG) $(srcdir)/pex-exos.c -o pic/$@; \\\n"
        "\telse true; fi\n"
        "\tif [ x\"$(NOASANFLAG)\" != x ]; then \\\n"
        "\t  $(COMPILE.c) $(PICFLAG) $(NOASANFLAG) $(srcdir)/pex-exos.c -o noasan/$@; \\\n"
        "\telse true; fi\n"
        "\t$(COMPILE.c) $(srcdir)/pex-exos.c $(OUTPUT_OPTION)\n"
        "\n"
        "./pex-unix.$(objext): $(srcdir)/pex-unix.c",
    ),

    # =========================================================================
    # libstdc++: os/generic invece di os/newlib
    #
    # Il cross si configura con --with-newlib, e per la libstdc++ quel
    # flag non vuol dire "usa newlib": vuol dire "NON sei su glibc, non
    # fare i test di collegamento, prendi questa tabella di risposte". La
    # tabella e' quasi tutta giusta anche per noi — le funzioni `f` della
    # matematica ci sono (openlibm), strtof c'e', hypot c'e'.
    #
    # UNA RIGA E' SBAGLIATA, ed e' os_include_dir. `os/newlib` contiene un
    # ctype_base.h scritto sui MACRO INTERNI di newlib (_U, _L, _N, e la
    # tabella `_ctype_`), che nella nostra <ctype.h> non esistono e non
    # esisteranno: sono un dettaglio di implementazione di quella libc,
    # non un'interfaccia. `os/generic` invece non chiede niente a nessuno,
    # definisce le sue maschere da se' e funziona con qualunque libc.
    #
    # Il resto del ramo --with-newlib resta valido, quindi si cambia solo
    # questa riga invece di aggiungere un ramo intero: meno superficie di
    # contatto con l'upstream, meno cose che scadono.
    #
    # ⚠️ CONFIGURE E CONFIGURE.AC INSIEME. Il primo e' quello che gira, il
    # secondo e' quello da cui il primo si rigenera: cambiarne uno solo
    # significa che al prossimo autoreconf la modifica sparisce (o
    # ricompare) senza che nessuno l'abbia chiesto.
    #
    # NOTA: il modello di locale va scelto a mano con --enable-clocale=
    # generic, perche' --with-newlib lo porta a `newlib` e quello tira
    # dentro config/locale/newlib/ctype_members.cc, che ha lo stesso
    # problema del ctype_base.h di sopra.
    # =========================================================================
    # =========================================================================
    # fixincludes NON si costruisce per un ospite EX-OS
    #
    # ⚠️ NON E' UN RIPIEGO PER FARLO COMPILARE: e' che quel programma non ha
    # niente da fare qui. fixincludes esiste per CORREGGERE GLI HEADER DI
    # SISTEMA rotti dell'ospite — i vecchi <sys/*.h> di SunOS, HP-UX,
    # IRIX — riscrivendoli in una copia privata di GCC. EX-OS si configura
    # con --without-headers: header di sistema da correggere non ce ne
    # sono, e infatti gcc/configure.ac mette gia' STMP_FIXINC='' quando
    # glielo si dice.
    #
    # Resta pero' che il Makefile di primo livello lo COSTRUISCE lo stesso,
    # perche' `fixincludes` sta in host_tools e nessuna opzione lo toglie
    # da li'. E fixincl.c usa fork():
    #
    #     fixincl.c:816: error: implicit declaration of function 'fork'
    #
    # fork() su EX-OS non c'e' e non ci sara' — duplicare uno spazio di
    # indirizzamento per buttarlo via alla exec successiva, senza
    # copy-on-write, e' la cosa piu' costosa che si possa fare (vedi il
    # commento su spawn in lib/include/libc.h). Quindi si toglie la
    # directory dall'elenco, che e' la risposta giusta e non un aggiramento.
    # =========================================================================
    (
        "configure",
        '# Don\'t compile the bundled readline/libreadline.a if --with-system-readline\n'
        '# is provided.\n'
        'if test x$with_system_readline = xyes ; then\n'
        '  noconfigdirs="$noconfigdirs readline"\n'
        'fi\n',
        '# Don\'t compile the bundled readline/libreadline.a if --with-system-readline\n'
        '# is provided.\n'
        'if test x$with_system_readline = xyes ; then\n'
        '  noconfigdirs="$noconfigdirs readline"\n'
        'fi\n'
        '\n'
        '# EX-OS: niente fixincludes (usa fork, e non c\'e\' niente da correggere).\n'
        'case "${host}" in\n'
        '  *-exos*) noconfigdirs="$noconfigdirs fixincludes" ;;\n'
        'esac\n',
    ),
    (
        "configure.ac",
        '# Don\'t compile the bundled readline/libreadline.a if --with-system-readline\n'
        '# is provided.\n'
        'if test x$with_system_readline = xyes ; then\n'
        '  noconfigdirs="$noconfigdirs readline"\n'
        'fi\n',
        '# Don\'t compile the bundled readline/libreadline.a if --with-system-readline\n'
        '# is provided.\n'
        'if test x$with_system_readline = xyes ; then\n'
        '  noconfigdirs="$noconfigdirs readline"\n'
        'fi\n'
        '\n'
        '# EX-OS: niente fixincludes (usa fork, e non c\'e\' niente da correggere).\n'
        'case "${host}" in\n'
        '  *-exos*) noconfigdirs="$noconfigdirs fixincludes" ;;\n'
        'esac\n',
    ),
    (
        "libstdc++-v3/configure",
        '  if test "x${with_newlib}" = "xyes"; then\n'
        '    os_include_dir="os/newlib"\n',
        '  if test "x${with_newlib}" = "xyes"; then\n'
        '    case "${host}" in\n'
        '      *-exos*)\n'
        '        os_include_dir="os/generic"\n'
        '        $as_echo "#define HAVE_STRTOLD 1" >>confdefs.h\n'
        '        ;;\n'
        '      *) os_include_dir="os/newlib" ;;\n'
        '    esac\n',
    ),
    (
        "libstdc++-v3/configure.ac",
        '  if test "x${with_newlib}" = "xyes"; then\n'
        '    os_include_dir="os/newlib"\n',
        '  if test "x${with_newlib}" = "xyes"; then\n'
        '    case "${host}" in\n'
        '      *-exos*)\n'
        '        os_include_dir="os/generic"\n'
        '        AC_DEFINE(HAVE_STRTOLD)\n'
        '        ;;\n'
        '      *) os_include_dir="os/newlib" ;;\n'
        '    esac\n',
    ),
]

# File nuovi: (sorgente in questa directory, destinazione nell'albero GCC)
#
# pex-exos.c non sta qui ma in tools/binutils-exos/: e' nato per binutils,
# ed e' lo STESSO file. Copiarlo in due posti vorrebbe dire due copie da
# tenere allineate a mano — che e' esattamente il genere di cosa che si
# scopre rotta sei mesi dopo.
NUOVI = [
    ("exos.h", "gcc/config/i386/exos.h"),
    ("../binutils-exos/pex-exos.c", "libiberty/pex-exos.c"),
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
        --enable-languages=c,c++ --without-headers --with-newlib \\
        --disable-nls --disable-shared --disable-threads \\
        --disable-libssp --disable-libgomp --disable-libquadmath \\
        --disable-libatomic --disable-libvtv --disable-bootstrap \\
        --enable-clocale=generic --disable-libstdcxx-pch
    make -j1 all-gcc && make -j1 install-gcc
    make -j1 all-target-libgcc && make -j1 install-target-libgcc
    make -j1 all-target-libstdc++-v3 && make -j1 install-target-libstdc++-v3

⚠️ -j1 E NON -j$(nproc): la macchina di sviluppo ha 4 GB, e i file
gimple-match-*.cc di GCC arrivano a 1,5 GB di picco CIASCUNO. Con -j2 il
sistema va in OOM a meta' strada, cioe' dopo un'ora di lavoro buttata.

⚠️ --enable-clocale=generic E' OBBLIGATORIO. Senza, --with-newlib porta il
modello di locale a `newlib`, che tira dentro
config/locale/newlib/ctype_members.cc — scritto sui macro interni di
newlib, che la nostra libc non ha e non avra'.

⚠️ PRIMA della libstdc++ ci vuole libm nel sysroot: il suo <cmath> scrive
`using ::sin;` per centottanta nomi e quei nomi devono esistere. Si
prepara con tools/openlibm-exos/prepara-libm.sh.

I wrapper dei binutils (i386-exos-as, -ld, ...) e l'ambiente del bersaglio
(crt0.o, libc.a, header) si preparano con tools/gcc-exos/prepara-cross.sh.
""" % albero)
    return 0


if __name__ == "__main__":
    sys.exit(main())

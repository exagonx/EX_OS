#!/bin/sh
# =============================================================================
# tools/costruisci-utente64.sh — la libc e i comandi di EX-OS a 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/costruisci-utente64.sh            tutto: libc, librerie, /bin
#     tools/costruisci-utente64.sh ls cp sh   solo questi comandi
#
# (@EXOS-64, tappa 4.) Compila gli STESSI sorgenti della versione a 32 bit -
# lib/libc.c, lib/ex*, bin/* - col gcc dell'host in -m64, e mette il risultato
# in build-64/: lib/ gli oggetti, bin/ i programmi. `make iso64` li raccoglie.
#
# ! UNA REGOLA SOLA PER TUTTI I COMANDI, dove il Makefile a 32 bit ne ha una
# scritta a mano per ciascuno (con il suo .ld, e l'elenco delle librerie che
# gli servono). Qui ogni comando e': tutti i .c della sua directory, l'avvio,
# la libc, e l'ARCHIVIO di tutte le librerie del progetto - da cui il
# collegatore prende solo cio' che il comando chiama (--gc-sections toglie il
# resto). Niente elenchi da tenere uguali.
#
# ! LA LIBC E' COLLEGATA DENTRO OGNI PROGRAMMA (statica), non condivisa come
# a 32 bit (libc.so e i «ponti»). La libreria condivisa vuole il caricatore
# ELF64 delle librerie (kernel/loader/lib.c), che e' la tappa 5. I programmi
# vengono piu' grossi; a 64 bit non c'e' un dischetto da far bastare.
#
# ! CHI NON COMPILA NON FERMA GLI ALTRI: alla fine si dice quanti sono
# riusciti e quali no, col registro di ciascuno in build-64/registri/.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1

FUORI=build-64
LIBD=$FUORI/lib
BIND=$FUORI/bin
OGG=$FUORI/obj
REG=$FUORI/registri
mkdir -p "$LIBD" "$BIND" "$OGG" "$REG"

CF="-m64 -ffreestanding -fno-builtin -fstack-protector-strong -mstack-protector-guard=global \
    -fno-pic -fno-pie -fno-asynchronous-unwind-tables -Wall -O2 -std=c11 -nostdlib \
    -ffunction-sections -fdata-sections"
INC="-Ilib/include -Ilib"
for d in lib/ex* drivers/* bin/gfedit lib/terze/* gfbasic; do [ -d "$d" ] && INC="$INC -I$d"; done
LD="ld -m elf_x86_64 -nostdlib --gc-sections -z max-page-size=4096 -z noexecstack -T lib/programma64.ld"

# --- 1. la libc e l'avvio ------------------------------------------------------
gcc $CF $INC -c lib/libc.c -o "$LIBD/libc.o" > "$REG/libc.log" 2>&1 || {
    echo "costruisci-utente64: la libc non compila ($REG/libc.log)"; grep -m5 ' error' "$REG/libc.log"; exit 1; }
gcc -m64 -c lib/start.S -o "$LIBD/start.o" || exit 1

# --- 2. le librerie del progetto, in un archivio --------------------------------
rm -f "$LIBD/libexos.a"
fatte=0; saltate=""; rimandate=""
# (font8x16.c e' il carattere di sistema: sta col kernel, e a 32 bit entra in
# exwin.so; qui entra nell'archivio)
# gfbasic/gf_basic.c is the BASIC interpreter `runbas` runs.
for s in lib/rete.c lib/dns.c lib/audio.c lib/wifi.c kernel/arch/x86/font8x16.c gfbasic/gf_basic.c lib/ex*/*.c; do
    # gli «stub» sono le controfigure di una libreria per chi a 32 bit la
    # carica condivisa: qui la libreria vera e' nell'archivio, e i due
    # insieme sarebbero due definizioni della stessa funzione
    # ! THE JAVASCRIPT ENGINES ARE SHARED OBJECTS (14 October 2026), as at 32
    # bit: ExJs and QuickJS export the same names, so they cannot both be in
    # the archive. They are built at step 6 as exjs.so and quickjs.so, and
    # the archive gets the stub that opens one of them with dlopen
    # (lib/libc_dl.c). exjs_fisso.c, the one-engine stand-in, is no longer
    # used; exjs_esporta.c is the 32-bit EX-OS library table.
    case "$s" in lib/exjs/exjs_stub.c) ;; *_stub.c) continue ;; esac
    case "$s" in lib/exjs/exjs_stub.c) ;; lib/exjs/*|lib/exqjs/*) continue ;; esac
    o="$OGG/$(echo "$s" | tr '/' '_' | sed 's/\.c$/.o/')"
    piu=""; case "$s" in kernel/*) piu="-Ikernel/include -fno-stack-protector" ;; esac
    if gcc $CF $INC $piu -I"$(dirname "$s")" -c "$s" -o "$o" > "$REG/$(basename "$o").log" 2>&1; then
        ar rc "$LIBD/libexos.a" "$o"; fatte=$((fatte + 1))
    else
        saltate="$saltate $s"
    fi
done
echo "librerie: $fatte sorgenti in $LIBD/libexos.a"
[ -z "$saltate" ]   || echo "  non compilano a 64 bit:$saltate"
[ -z "$rimandate" ] || echo "  rimandate:$rimandate"

# --- 3. i comandi ----------------------------------------------------------------
if [ $# -gt 0 ]; then QUALI="$*"; else QUALI=$(ls bin); fi
bene=0; male=""
for n in $QUALI; do
    [ -d "bin/$n" ] || continue
    ls bin/$n/*.c > /dev/null 2>&1 || continue
    r="$REG/bin-$n.log"; : > "$r"
    ogg=""; ok=1
    for s in bin/$n/*.c; do
        o="$OGG/bin_${n}_$(basename "$s" .c).o"
        gcc $CF $INC -Ibin/$n -c "$s" -o "$o" >> "$r" 2>&1 || { ok=0; break; }
        ogg="$ogg $o"
    done
    if [ $ok -eq 1 ]; then
        if [ -f "bin/$n/start.S" ]; then
            # un comando col suo avvio e senza libc (la shell)
            gcc -m64 -c "bin/$n/start.S" -o "$OGG/bin_${n}_start.o" >> "$r" 2>&1 &&
            $LD "$OGG/bin_${n}_start.o" $ogg -o "$BIND/$n" >> "$r" 2>&1 || ok=0
        else
            $LD "$LIBD/start.o" $ogg "$LIBD/libc.o" "$LIBD/libexos.a" -o "$BIND/$n" >> "$r" 2>&1 || ok=0
        fi
    fi
    if [ $ok -eq 1 ]; then bene=$((bene + 1)); else male="$male $n"; rm -f "$BIND/$n"; fi
done
echo "comandi: $bene costruiti in $BIND"
[ -z "$male" ] || echo "  NON costruiti:$male   (registri in $REG/)"

# --- 4. i driver ------------------------------------------------------------------
# Un driver di EX-OS e' un programma: gira in ring 3, il kernel lo carica come
# ogni altro ELF (kernel.cfg, [modules]) e gli parla con i messaggi. Si
# costruiscono come i comandi; il codice che piu' driver USB hanno in comune
# (drivers/usb) sta in un archivio suo. Solo quando si costruisce tutto.
if [ $# -eq 0 ]; then
    DRVD=$FUORI/drivers
    mkdir -p "$DRVD"
    rm -f "$LIBD/libusb.a"
    # The code several drivers share goes in one archive: the USB core, the
    # common half of the sound drivers (it holds their main), the SiS 2D part.
    for s in drivers/usb/*.c drivers/audio/audio_comune.c drivers/sis/sis_2d.c; do
        o="$OGG/$(echo "$s" | tr '/' '_' | sed 's/\.c$/.o/')"
        gcc $CF $INC -c "$s" -o "$o" > "$REG/$(basename "$o").log" 2>&1 && ar rc "$LIBD/libusb.a" "$o"
    done
    dbene=0; dmale=""
    for d in drivers/*/; do
        n=$(basename "$d")
        [ -f "drivers/$n/$n.c" ] || continue
        case "$n" in tty|usb|audio) continue ;; esac  # tty sta nel kernel, usb e audio sono l'archivio
        # floppy.drv is a module the kernel loads into itself, for the floppy
        # controller: the 64-bit version has no floppy medium and no such
        # loader yet. Left out on purpose, not a failure.
        case "$n" in floppy) continue ;; esac
        r="$REG/drv-$n.log"
        o="$OGG/drv_$n.o"
        if gcc $CF $INC -Idrivers/$n -c "drivers/$n/$n.c" -o "$o" > "$r" 2>&1 &&
           $LD "$LIBD/start.o" "$o" "$LIBD/libc.o" "$LIBD/libusb.a" "$LIBD/libexos.a" -o "$DRVD/$n.drv" >> "$r" 2>&1; then
            dbene=$((dbene + 1))
        else
            dmale="$dmale $n"; rm -f "$DRVD/$n.drv"
        fi
    done
    echo "driver: $dbene costruiti in $DRVD"
    [ -z "$dmale" ] || echo "  NON costruiti:$dmale   (registri in $REG/)"
fi

# --- 5. la scrivania: i programmi di ExWin ----------------------------------------
# Stessa regola dei comandi (i .c della directory, l'avvio, la libc, l'archivio
# delle librerie - dove stanno anche exwin, exdlg, exfont, eximg...), in
# build-64/exwin/bin. Il server grafico e' un driver (drivers/wserver) e si
# costruisce al passo 4.
if [ $# -eq 0 ]; then
    EXWD=$FUORI/exwin/bin
    mkdir -p "$EXWD"
    ebene=0; emale=""
    for d in exwin/bin/*/; do
        n=$(basename "$d")
        ls exwin/bin/$n/*.c > /dev/null 2>&1 || continue
        r="$REG/exwin-$n.log"; : > "$r"
        ogg=""; ok=1
        for s in exwin/bin/$n/*.c; do
            o="$OGG/exwin_${n}_$(basename "$s" .c).o"
            gcc $CF $INC -Iexwin/bin/$n -c "$s" -o "$o" >> "$r" 2>&1 || { ok=0; break; }
            ogg="$ogg $o"
        done
        [ $ok -eq 1 ] && { $LD "$LIBD/start.o" $ogg "$LIBD/libc.o" "$LIBD/libexos.a" -o "$EXWD/$n" >> "$r" 2>&1 || ok=0; }
        if [ $ok -eq 1 ]; then ebene=$((ebene + 1)); else emale="$emale $n"; rm -f "$EXWD/$n"; fi
    done
    echo "scrivania: $ebene programmi costruiti in $EXWD"
    [ -z "$emale" ] || echo "  NON costruiti:$emale   (registri in $REG/)"
fi

# --- 6. the shared objects: the two JavaScript engines ------------------------------
# Ordinary ELF .so files (-fPIC, ld -shared) for the libc's dlopen
# (lib/libc_dl.c), in build-64/exwin/lib - where the 32-bit CD has its
# exjs.so and quickjs.so. No libc inside: a library takes it from the program
# that opens it (lib/libc_esportati.h). QuickJS carries openlibm (built PIC
# by tools/gcc-exos/cross64.sh, step libm) and libgcc.
if [ $# -eq 0 ]; then
    SOD=$FUORI/exwin/lib
    mkdir -p "$SOD"
    CFPIC=$(echo "$CF" | sed 's/-fno-pic -fno-pie/-fPIC/; s/-fstack-protector-strong -mstack-protector-guard=global/-fno-stack-protector/')
    LDSO="ld -m elf_x86_64 -shared --gc-sections -z max-page-size=4096 -z noexecstack"
    ogg=""; ok=1; r="$REG/exjs.so.log"; : > "$r"
    for s in lib/exjs/*.c; do
        case "$s" in *_stub.c|*_fisso.c|*_esporta.c) continue ;; esac
        o="$OGG/so_exjs_$(basename "$s" .c).o"
        gcc $CFPIC -Ilib/include -Ilib/exjs -c "$s" -o "$o" >> "$r" 2>&1 || { ok=0; break; }
        ogg="$ogg $o"
    done
    [ $ok -eq 1 ] && $LDSO -soname exjs.so -o "$SOD/exjs.so" $ogg >> "$r" 2>&1 && echo "librerie: $SOD/exjs.so" ||
        echo "  exjs.so NON costruita ($r)"
    LIBM64=cross_build/exos-cross64/x86_64-exos/lib/libm.a
    LIBGCC64=$(ls cross_build/exos-cross64/lib/gcc/x86_64-exos/*/libgcc.a 2>/dev/null | head -1)
    if [ -f quickjs/quickjs.c ] && grep -q __EXOS__ quickjs/cutils.h && [ -f "$LIBM64" ] && [ -f "$LIBGCC64" ]; then
        ogg=""; ok=1; r="$REG/quickjs.so.log"; : > "$r"
        for s in quickjs/quickjs.c quickjs/libregexp.c quickjs/libunicode.c quickjs/dtoa.c; do
            o="$OGG/so_qjs_$(basename "$s" .c).o"
            gcc $CFPIC -Os -nostdinc -D__EXOS__ -I "$(gcc -m64 -print-file-name=include)" \
                -Ilib/include -Iquickjs -c "$s" -o "$o" >> "$r" 2>&1 || { ok=0; break; }
            ogg="$ogg $o"
        done
        [ $ok -eq 1 ] && gcc $CFPIC -Os -Ilib/include -Ilib/exjs -Iquickjs -c lib/exqjs/exqjs.c \
            -o "$OGG/so_qjs_exqjs.o" >> "$r" 2>&1 || ok=0
        [ $ok -eq 1 ] && $LDSO -soname quickjs.so -o "$SOD/quickjs.so" "$OGG/so_qjs_exqjs.o" $ogg \
            "$LIBM64" "$LIBGCC64" >> "$r" 2>&1 && echo "librerie: $SOD/quickjs.so" ||
            echo "  quickjs.so NON costruita ($r)"
    else
        echo "  quickjs.so rimandata: servono quickjs/ (tools/quickjs-exos/applica.py) e la libm di cross64.sh"
    fi
fi

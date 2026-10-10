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
for d in lib/ex* drivers/* bin/gfedit; do [ -d "$d" ] && INC="$INC -I$d"; done
LD="ld -m elf_x86_64 -nostdlib --gc-sections -z max-page-size=4096 -z noexecstack -T lib/programma64.ld"

# --- 1. la libc e l'avvio ------------------------------------------------------
gcc $CF $INC -c lib/libc.c -o "$LIBD/libc.o" > "$REG/libc.log" 2>&1 || {
    echo "costruisci-utente64: la libc non compila ($REG/libc.log)"; grep -m5 ' error' "$REG/libc.log"; exit 1; }
gcc -m64 -c lib/start.S -o "$LIBD/start.o" || exit 1

# --- 2. le librerie del progetto, in un archivio --------------------------------
rm -f "$LIBD/libexos.a"
fatte=0; saltate=""
for s in lib/rete.c lib/dns.c lib/audio.c lib/wifi.c lib/ex*/*.c; do
    # gli «stub» sono le controfigure di una libreria per chi a 32 bit la
    # carica condivisa: qui la libreria vera e' nell'archivio, e i due
    # insieme sarebbero due definizioni della stessa funzione
    case "$s" in *_stub.c) continue ;; esac
    o="$OGG/$(echo "$s" | tr '/' '_' | sed 's/\.c$/.o/')"
    if gcc $CF $INC -I"$(dirname "$s")" -c "$s" -o "$o" > "$REG/$(basename "$o").log" 2>&1; then
        ar rc "$LIBD/libexos.a" "$o"; fatte=$((fatte + 1))
    else
        saltate="$saltate $s"
    fi
done
echo "librerie: $fatte sorgenti in $LIBD/libexos.a"
[ -z "$saltate" ] || echo "  non compilano a 64 bit:$saltate"

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
    for s in drivers/usb/*.c; do
        o="$OGG/$(echo "$s" | tr '/' '_' | sed 's/\.c$/.o/')"
        gcc $CF $INC -c "$s" -o "$o" > "$REG/$(basename "$o").log" 2>&1 && ar rc "$LIBD/libusb.a" "$o"
    done
    dbene=0; dmale=""
    for d in drivers/*/; do
        n=$(basename "$d")
        [ -f "drivers/$n/$n.c" ] || continue
        case "$n" in tty|usb) continue ;; esac      # tty sta nel kernel, usb e' l'archivio
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

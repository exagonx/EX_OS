#!/bin/sh
# =============================================================================
# tools/freebasic-exos/prepara-fb.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Costruisce fbc — il compilatore FreeBASIC — PER EX-OS.
#
#     tools/freebasic-exos/prepara-fb.sh [albero-fb] [uscita]
#
# Default: ./FreeBASIC-1.07.3-source-bootstrap e ~/fb-build-exos
#
# -----------------------------------------------------------------------------
# ⚠️ NON SERVE UN fbc PER COSTRUIRE fbc, e questo e' tutto il trucco.
#
# Il compilatore FreeBASIC e' scritto in FreeBASIC: normalmente per
# costruirlo ne serve gia' uno. Il pacchetto «source-bootstrap» rompe il
# cerchio portandosi dietro i sorgenti del compilatore GIA' TRADOTTI —
# per i bersagli a 32 bit in assembly (bootstrap/linux-x86/*.asm), per
# quelli a 64 bit in C.
#
# Quell'assembly e' i386 GAS, sintassi intel, e chiama solo la runtime e
# la libc: il nostro `as` lo assembla tale e quale, tutti e 145 i file.
# Percio' il compilatore per EX-OS si costruisce senza avere niente.
#
# -----------------------------------------------------------------------------
# ⚠️ SI USA bootstrap/linux-x86 E fbc CREDERA' DI PRODURRE PER LINUX.
#
# Va detto perche' e' una mezza verita' che si scoprirebbe dopo. Quei .asm
# sono stati generati con `-target linux-x86`, quindi l'fbc che ne esce ha
# «linux-x86» scritto dentro come bersaglio predefinito. Regge quasi tutto
# — EX-OS condivide con Linux l'ABI ELF32 i386 — ma le specs di link sono
# le sue: entry, indirizzo di caricamento, niente PIE.
#
# Il passo successivo, quando servira', e' aggiungere un bersaglio `exos`
# ai sorgenti .bas del compilatore e rigenerare: per quello serve un fbc
# che giri su Linux, e si costruisce dallo stesso pacchetto con
# `make bootstrap` (bootstrap/linux-x86_64, che e' in C).
#
# -----------------------------------------------------------------------------
# ⚠️ DUE FILE DELLA RUNTIME RESTANO FUORI, e si dice quali:
#
#   thread_call.c   vuole libffi, per costruire chiamate con firma decisa
#                   a tempo di esecuzione. Su EX-OS libffi non c'e'.
#   thread_ctx.c    NO, questo entra: ha gia' un ramo senza thread.
#
# Quindi fuori ce n'e' UNO solo. Le funzioni dei thread veri (THREADCREATE
# e compagne) non ci sono: un programma che le usa non si collega, e il
# messaggio nomina la funzione — che e' meglio di uno stub che ritorna
# NULL e lascia credere di aver creato qualcosa.
# =============================================================================

set -e

RADICE=$(cd "$(dirname "$0")/../.." && pwd)
ALBERO="${1:-$RADICE/FreeBASIC-1.07.3-source-bootstrap}"
USCITA="${2:-$HOME/fb-build-exos}"
PREFISSO="${PREFISSO:-$HOME/exos-cross}"
SYSROOT="$PREFISSO/i386-exos"

if [ ! -f "$ALBERO/src/rtlib/fb_config.h" ]; then
    echo "prepara-fb: '$ALBERO' non e' un albero di FreeBASIC" >&2
    echo "  si scarica da freebasic.net (pacchetto source-bootstrap)" >&2
    exit 1
fi
ALBERO=$(cd "$ALBERO" && pwd)

if [ ! -x "$PREFISSO/bin/i386-exos-gcc" ]; then
    echo "prepara-fb: manca il cross i386-exos-gcc" >&2
    echo "  si prepara con tools/gcc-exos/prepara-cross.sh" >&2
    exit 1
fi

PATH="$PREFISSO/bin:$PATH"
export PATH

echo "=== FreeBASIC per EX-OS ==="
echo "  sorgenti : $ALBERO"
echo "  uscita   : $USCITA"

# --- 1. il bersaglio dentro l'albero -----------------------------------------
python3 "$RADICE/tools/freebasic-exos/applica.py" "$ALBERO"

OGG="$USCITA/obj"
rm -rf "$OGG"
mkdir -p "$OGG" "$USCITA"

# --- 2. la runtime: libfb.a ---------------------------------------------------
echo
echo "=== libfb.a (runtime) ==="
cd "$ALBERO/src/rtlib"

n=0
for f in *.c; do
    [ "$f" = "thread_call.c" ] && continue      # vuole libffi: vedi sopra
    i386-exos-gcc -O2 -c -I. "$f" -o "$OGG/${f%.c}.o"
    n=$((n + 1))
done
echo "  comuni      : $n file"

for f in exos/*.c; do
    i386-exos-gcc -O2 -c -I. -Iexos "$f" -o "$OGG/exos_$(basename "${f%.c}").o"
done
echo "  strato exos : $(ls exos/*.c | wc -l) file"

# ⚠️ cpudetect.s NON e' opzionale: fb_CpuDetect e' scritta in assembly
# perche' fa CPUID, e senza di lei il link non si chiude.
i386-exos-gcc -c x86/cpudetect.s -o "$OGG/cpudetect.o"

# ⚠️ fbrt0.o E' L'AVVIO DEI PROGRAMMI FB, e non sta dentro libfb.a: e' un
# oggetto a se' che si mette PER PRIMO nel link. Definisce main(), che
# accende la runtime (fb_Init), chiama il codice del programma e la
# spegne. Senza, un programma FreeBASIC si collega solo se per caso
# nessuno cerca main — e se si collega, parte senza runtime accesa e
# muore alla prima PRINT.
i386-exos-gcc -O2 -c -I. static/fbrt0.c -o "$USCITA/fbrt0.o"
echo "  fbrt0.o     : avvio dei programmi FB"

rm -f "$USCITA/libfb.a"
i386-exos-ar rcs "$USCITA/libfb.a" "$OGG"/*.o
echo "  [OK] libfb.a: $(wc -c < "$USCITA/libfb.a") byte"

# --- 3. il compilatore: i 145 .asm gia' tradotti ------------------------------
echo
echo "=== fbc (dal bootstrap linux-x86) ==="
cd "$ALBERO/bootstrap/linux-x86"

rm -rf "$OGG/boot"
mkdir -p "$OGG/boot"
for f in *.asm; do
    i386-exos-as --strip-local-absolute "$f" -o "$OGG/boot/${f%.asm}.o"
done
echo "  assemblati  : $(ls "$OGG"/boot/*.o | wc -l) file"

# ⚠️ -z noexecstack A MANO: x86/cpudetect.s di FreeBASIC non ha la nota
# .note.GNU-stack, e senza quella il linker deduce «stack eseguibile» per
# tutto il binario. E' un file di terzi e non lo si tocca; l'opzione dice
# la stessa cosa dal lato giusto.
i386-exos-ld -static -z noexecstack -e _start -Ttext-segment=0x08000000 \
    -o "$USCITA/fbc" \
    "$SYSROOT/lib/crt0.o" \
    "$OGG"/boot/*.o \
    "$USCITA/libfb.a" \
    "$SYSROOT/lib/libc.a" \
    "$SYSROOT/lib/libm.a" \
    "$PREFISSO"/lib/gcc/i386-exos/*/libgcc.a

i386-exos-strip -o "$USCITA/fbc.strip" "$USCITA/fbc"
mv "$USCITA/fbc.strip" "$USCITA/fbc"

# --- 4. verifica --------------------------------------------------------------
#
# Non ci si fida del codice di uscita — la stessa regola di GCC e binutils
# (vedi tools/gcc-exos/leggimi.md): si guarda che il binario ci sia, che
# sia un ELF per i386 e che non abbia simboli irrisolti.
if [ ! -x "$USCITA/fbc" ]; then
    echo "  ! fbc non e' stato prodotto" >&2
    exit 1
fi

case "$(file -b "$USCITA/fbc")" in
    *"ELF 32-bit"*"Intel i386"*) ;;
    *) echo "  ! $USCITA/fbc non e' un ELF32 i386" >&2; exit 1;;
esac

echo "  [OK] fbc: $(wc -c < "$USCITA/fbc") byte"
echo
echo "Finisce sul CD degli strumenti con \`make iso\`, in /bin/fbc."
echo "⚠️ Provarlo lanciandolo con '&': in primo piano un errore non si vede."

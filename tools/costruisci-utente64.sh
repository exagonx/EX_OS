#!/bin/sh
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
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
# scritta a mano per ciascuno (con l'elenco delle librerie che gli servono).
# Qui ogni comando e': tutti i .c della sua directory, l'avvio, l'ARCHIVIO
# delle librerie del progetto - da cui il collegatore prende solo cio' che il
# comando chiama (--gc-sections toglie il resto) - e le librerie condivise
# che gli servono davvero (--as-needed). Niente elenchi da tenere uguali.
#
# ! I PROGRAMMI SONO SU /lib/ld-exos.so DAL 19 NOVEMBRE 2026 (voce 105), come
# a 32 bit: la libc condivisa, e le librerie di EX-OS (exwin.so, exdlg.so,
# exhttp.so...) ET_DYN vere, in build-64/exwin/lib. Fino ad allora a 64 bit
# ogni programma aveva dentro la libc e le librerie: exwin in ogni programma
# della scrivania. STATICI restano quelli con cui si entra e si ripara (login,
# install, soccorso, sh che ha il suo avvio), quelli che lo sono a 32 bit, e i
# driver: partono prima che ci sia un filesystem da cui leggere una libreria.
#
# ! CHI NON COMPILA NON FERMA GLI ALTRI: alla fine si dice quanti sono
# riusciti e quali no, col registro di ciascuno in build-64/registri/.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1

FUORI=${EXOS_FUORI}/build-64
LIBD=$FUORI/lib
BIND=$FUORI/bin
OGG=$FUORI/obj
REG=$FUORI/registri
SOD=$FUORI/exwin/lib
LDD=$FUORI/ld
mkdir -p "$LIBD" "$BIND" "$OGG" "$REG" "$SOD"

CF="-m64 -ffreestanding -fno-builtin -fstack-protector-strong -mstack-protector-guard=global \
    -fno-pic -fno-pie -fno-asynchronous-unwind-tables -Wall -O2 -std=c11 -nostdlib \
    -ffunction-sections -fdata-sections"
CFPIC=$(echo "$CF" | sed 's/-fno-pic -fno-pie/-fPIC/')
INC="-Ilib/include -Ilib"
for d in lib/ex* drivers/* bin/gfedit lib/terze/* gfbasic; do [ -d "$d" ] && INC="$INC -I$d"; done
# static: login, install, the drivers... (lib/programma64.ld)
LD="ld -m elf_x86_64 -nostdlib --gc-sections -z max-page-size=4096 -z noexecstack -T lib/programma64.ld"
# on /lib/ld-exos.so: the default link script of ld, the same base, as LD_DIN
# in the Makefile (a .ld that throws away .gnu* and .rel* cannot be dynamic)
LDDIN="ld -m elf_x86_64 -nostdlib --gc-sections -z max-page-size=4096 -z noexecstack -z separate-code \
    -Ttext-segment=0x08000000 --hash-style=both -dynamic-linker /lib/ld-exos.so"
LDSO="ld -m elf_x86_64 -shared --gc-sections -z max-page-size=4096 -z noexecstack -z separate-code \
    --hash-style=both -Bsymbolic-functions -z defs -z text"
LIBC_DIN="$LDD/libexos_avvio.a $LIBD/ld-exos.so"
# ld-exos.so before the archive: a library takes only __stack_chk_fail_local
LIBC_SO_DIN="$LIBD/ld-exos.so $LDD/libexos_avvio.a"

# The programs that stay static: the same as at 32 bit (STATICI_OBBLIGATI and
# the rules that compile lib/libc.c in), plus every command with its own start.
# telnetd, sshd, netupdate: they must work halfway through an update (20/11)
STATICI="install login soccorso extaskbar wifid prova64 readlink hello telnetd sshd netupdate"
statico() { for x in $STATICI; do [ "$x" = "$1" ] && return 0; done; return 1; }

# --- 1. la libc: statica (libc.o) e condivisa (ld-exos.so) -----------------------
gcc $CF $INC -c lib/libc.c -o "$LIBD/libc.o" > "$REG/libc.log" 2>&1 || {
    echo "costruisci-utente64: la libc non compila ($REG/libc.log)"; grep -m5 ' error' "$REG/libc.log"; exit 1; }
gcc -m64 -c lib/start.S -o "$LIBD/start.o" || exit 1
bash tools/costruisci-ld.sh 64 "$LDD" > "$REG/ld-exos.log" 2>&1 || {
    echo "costruisci-utente64: ld-exos.so non si costruisce ($REG/ld-exos.log)"; tail -5 "$REG/ld-exos.log"; exit 1; }
cp "$LDD/ld-exos.so" "$LIBD/ld-exos.so" || exit 1
echo "libc: $LIBD/libc.o (statica) e $LIBD/ld-exos.so (condivisa)"

# --- 2. le librerie di EX-OS, condivise --------------------------------------------
# The same sources as the 32-bit Makefile, -fPIC, with the version script of
# each (lib/<name>/<name>.ver): what it offers, and nothing else.
#     so_fai <name> <version script> "<sources>" "<the .so it needs>"
so_ok=""; so_male=""
so_fai() {
    n=$1; v=$2; ogg=""; ok=1; r="$REG/$n.so.log"; : > "$r"
    for s in $3; do
        o="$OGG/so_${n}_$(echo "$s" | tr '/' '_' | sed 's/\.c$/.o/')"
        piu=""; case "$s" in kernel/*) piu="-Ikernel/include" ;; esac
        gcc $CFPIC $INC $piu -I"$(dirname "$s")" -c "$s" -o "$o" >> "$r" 2>&1 || { ok=0; break; }
        ogg="$ogg $o"
    done
    dip=""; for d in $4; do dip="$dip $SOD/$d"; done
    if [ $ok -eq 1 ] && $LDSO -soname "$n.so" --version-script "$v" -o "$SOD/$n.so" $ogg $dip $5 \
           $LIBC_SO_DIN >> "$r" 2>&1; then
        so_ok="$so_ok $n"
    else
        so_male="$so_male $n"; rm -f "$SOD/$n.so"
    fi
}
EXTLS="lib/extls/extls_client.c lib/extls/extls_pem.c lib/extls/extls_kdf.c lib/extls/extls_pss.c \
    lib/excert/excert.c lib/exasn1/exasn1.c lib/exbig/exbig.c lib/excurva/excurva.c \
    lib/excrypt/chacha20.c lib/excrypt/poly1305.c lib/excrypt/x25519.c lib/excrypt/fe25519.c \
    lib/excrypt/sha512.c lib/excrypt/aes.c lib/excrypt/gcm.c lib/excrypt/p256.c lib/excrypt/sha1.c"
if [ $# -eq 0 ]; then
    so_fai exwin   lib/exwin/exwin.ver     "lib/exwin/exwin.c lib/exfont/exfont.c kernel/arch/x86/font8x16.c" ""
    so_fai exdlg   lib/exdlg/exdlg.ver     "lib/exdlg/exdlg.c lib/exdlg/scarichi.c" "exwin.so"
    so_fai eximg   lib/eximg/eximg.ver     "lib/eximg/eximg.c lib/eximg/png.c lib/eximg/ico.c lib/eximg/jpg.c \
        lib/eximg/gif.c lib/eximg/bmp.c lib/eximg/inflate.c lib/eximg/webp.c lib/eximg/vp8.c" ""
    so_fai exfont  lib/exfont/exfont.ver   "lib/exfont/exfont_ttf.c lib/exfont/ttf.c lib/exfont/raster.c" ""
    so_fai exhttp  lib/exhttp/exhttp.ver   "lib/exhttp/exhttp.c lib/exhttp/http.c lib/dns.c lib/rete.c $EXTLS" ""
    so_fai exhtml  lib/exhtml/exhtml.ver   "lib/exhtml/html.c" ""
    so_fai excss   lib/excss/excss.ver     "lib/excss/css.c" "exhtml.so"
    so_fai exjs    lib/exjs/exjs.ver       "lib/exjs/lex.c lib/exjs/parse.c lib/exjs/val.c lib/exjs/run.c \
        lib/exjs/base.c lib/exjs/libreria.c lib/exjs/regexp.c" ""
    so_fai exdom   lib/exdom/exdom.ver     "lib/exdom/exdom.c lib/exjs/exjs_stub.c" "exhtml.so"
    so_fai exzip   lib/exzip/exzip.ver     "lib/exzip/exzip.c lib/eximg/inflate.c lib/exzip/deflate.c" ""
    so_fai exsuono lib/exsuono/exsuono.ver "lib/exsuono/exsuono.c" ""
    # QuickJS carries openlibm, shared (libm.so.4) as at 32 bit, and libgcc
    LIBM64=${EXOS_FUORI}/cross_build/exos-cross64/x86_64-exos/lib/libm.so
    LIBGCC64=$(ls ${EXOS_FUORI}/cross_build/exos-cross64/lib/gcc/x86_64-exos/*/libgcc.a 2>/dev/null | head -1)
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
        if [ $ok -eq 1 ] && $LDSO -soname quickjs.so --version-script lib/exjs/exjs.ver -o "$SOD/quickjs.so" \
               "$OGG/so_qjs_exqjs.o" $ogg "$LIBM64" "$LIBGCC64" $LIBC_SO_DIN >> "$r" 2>&1; then
            so_ok="$so_ok quickjs"
        else
            so_male="$so_male quickjs"; rm -f "$SOD/quickjs.so"
        fi
    else
        echo "  quickjs.so rimandata: servono quickjs/ adattato e libgcc/libm di x86_64-exos"
        echo "    rimedio: python3 tools/quickjs-exos/applica.py quickjs; tools/gcc-exos/cross64.sh (cosa manca: tools/ambiente.sh -per utente64)"
    fi
    echo "librerie condivise in $SOD:$so_ok"
    [ -z "$so_male" ] || echo "  NON costruite:$so_male   (registri in $REG/)"
fi
# what a program on ld-exos.so may need of them: the six it cannot live
# without, as the 32-bit Makefile links them. The others (eximg, exfont,
# excss, exsuono, the JavaScript engines) are opened when wanted, by exlib.
SO_PROGRAMMI=""
for n in exdlg exwin exdom exhtml exhttp exzip; do
    [ -f "$SOD/$n.so" ] && SO_PROGRAMMI="$SO_PROGRAMMI $SOD/$n.so"
done

# --- 3. le altre librerie del progetto, in un archivio ------------------------------
# libexos.a: everything, for the static programs and the drivers.
# libexos_din.a: without what is in the shared libraries above, for the
# programs on ld-exos.so (a second copy of exwin.c inside the program would
# win over exwin.so, and the program would draw with a toolkit of its own).
rm -f "$LIBD/libexos.a" "$LIBD/libexos_din.a"
fatte=0; saltate=""
# (font8x16.c e' il carattere di sistema: sta col kernel, e entra in exwin.so;
# qui entra nell'archivio dei programmi statici)
# gfbasic/gf_basic.c is the BASIC interpreter `runbas` runs.
for s in lib/rete.c lib/dns.c lib/audio.c lib/wifi.c kernel/arch/x86/font8x16.c gfbasic/gf_basic.c lib/ex*/*.c; do
    # The stubs of the six libraries are gone (they are linked as .so); the
    # optional ones stay - exjs_stub.c, excss_stub.c, exsuono_stub.c - and open
    # their library with dlopen. ExJs and QuickJS export the same names: they
    # are only .so (step 2).
    case "$s" in lib/exjs/exjs_stub.c) ;; lib/exjs/*|lib/exqjs/*) continue ;; esac
    o="$OGG/$(echo "$s" | tr '/' '_' | sed 's/\.c$/.o/')"
    piu=""; case "$s" in kernel/*) piu="-Ikernel/include -fno-stack-protector" ;; esac
    if gcc $CF $INC $piu -I"$(dirname "$s")" -c "$s" -o "$o" > "$REG/$(basename "$o").log" 2>&1; then
        ar rc "$LIBD/libexos.a" "$o"   # the stubs open with dlopen, static too
        case "$s" in
        lib/exwin/exwin.c|lib/exfont/exfont.c|lib/exfont/exfont_ttf.c|lib/exfont/ttf.c|lib/exfont/raster.c|\
        lib/exdlg/exdlg.c|lib/exdlg/scarichi.c|lib/exhttp/exhttp.c|lib/exhttp/http.c|lib/exhtml/html.c|\
        lib/excss/css.c|lib/exdom/exdom.c|lib/exzip/exzip.c|lib/exsuono/exsuono.c|\
        lib/eximg/eximg.c|lib/eximg/png.c|lib/eximg/ico.c|lib/eximg/jpg.c|lib/eximg/gif.c|lib/eximg/bmp.c|\
        lib/eximg/webp.c|lib/eximg/vp8.c) ;;
        *) ar rc "$LIBD/libexos_din.a" "$o" ;;
        esac
        fatte=$((fatte + 1))
    else
        saltate="$saltate $s"
    fi
done
echo "librerie: $fatte sorgenti in $LIBD/libexos.a e libexos_din.a"
[ -z "$saltate" ]   || echo "  non compilano a 64 bit:$saltate"

# a program: static, or on ld-exos.so with the shared libraries it calls
#     collega <output> <log> <objects...>
collega() {
    fuori=$1; r=$2; shift 2
    if statico "$(basename "$fuori" .drv)"; then
        $LD "$LIBD/start.o" "$@" "$LIBD/libc.o" "$LIBD/libexos.a" -o "$fuori" >> "$r" 2>&1
    else
        $LDDIN "$LIBD/start.o" "$@" "$LIBD/libexos_din.a" --as-needed $SO_PROGRAMMI --no-as-needed \
            $LIBC_DIN -o "$fuori" >> "$r" 2>&1
    fi
}

# --- 4. i comandi ----------------------------------------------------------------
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
            collega "$BIND/$n" "$r" $ogg || ok=0
        fi
    fi
    if [ $ok -eq 1 ]; then bene=$((bene + 1)); else male="$male $n"; rm -f "$BIND/$n"; fi
done
echo "comandi: $bene costruiti in $BIND"
[ -z "$male" ] || echo "  NON costruiti:$male   (registri in $REG/)"

# --- 5. i driver ------------------------------------------------------------------
# Un driver di EX-OS e' un programma: gira in ring 3, il kernel lo carica come
# ogni altro ELF (kernel.cfg, [modules]) e gli parla con i messaggi. Si
# costruiscono come i comandi, STATICI; il codice che piu' driver USB hanno in
# comune (drivers/usb) sta in un archivio suo. Solo quando si costruisce tutto.
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
        r="$REG/drv-$n.log"; : > "$r"
        o="$OGG/drv_$n.o"
        # A driver made of several files (15 October 2026): ar5k carries
        # Linux's ath5k hardware code (drivers/ar5k/ath5k, ISC) with its own
        # compatibility headers, built as GNU C and without warnings - the
        # files are kept as Linux has them.
        piu=""; piu_ok=1
        case "$n" in
        ar5k)
            for s in drivers/ar5k/ar5k_scan.c drivers/ar5k/ar5k_radio.c drivers/ar5k/ar5k_servizio.c drivers/ar5k/ar5k_conn.c drivers/ar5k/glue.c drivers/ar5k/glue_libero.c drivers/ar5k/ath5k/*.c; do
                oo="$OGG/drv_ar5k_$(basename "$s" .c).o"
                gcc $(echo "$CF" | sed 's/-std=c11/-std=gnu11/; s/-Wall//') -w -Idrivers/ar5k/compat -Idrivers/ar5k \
                    -Idrivers/ar5k/ath5k -Ilib/include -Idrivers/net -Ilib/exwifi -c "$s" -o "$oo" >> "$r" 2>&1 || piu_ok=0
                piu="$piu $oo"
            done ;;
        esac
        if [ $piu_ok -eq 1 ] && gcc $CF $INC -Idrivers/$n -c "drivers/$n/$n.c" -o "$o" >> "$r" 2>&1 &&
           $LD "$LIBD/start.o" "$o" $piu "$LIBD/libc.o" "$LIBD/libusb.a" "$LIBD/libexos.a" -o "$DRVD/$n.drv" >> "$r" 2>&1; then
            dbene=$((dbene + 1))
        else
            dmale="$dmale $n"; rm -f "$DRVD/$n.drv"
        fi
    done
    echo "driver: $dbene costruiti in $DRVD"
    [ -z "$dmale" ] || echo "  NON costruiti:$dmale   (registri in $REG/)"
fi

# --- 6. la scrivania: i programmi di ExWin ----------------------------------------
# Stessa regola dei comandi, in build-64/exwin/bin: su ld-exos.so con
# exwin.so, exdlg.so e le altre che chiamano. Il server grafico e' un driver
# (drivers/wserver) e si costruisce al passo 5.
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
        [ $ok -eq 1 ] && { collega "$EXWD/$n" "$r" $ogg || ok=0; }
        if [ $ok -eq 1 ]; then ebene=$((ebene + 1)); else emale="$emale $n"; rm -f "$EXWD/$n"; fi
    done
    echo "scrivania: $ebene programmi costruiti in $EXWD"
    [ -z "$emale" ] || echo "  NON costruiti:$emale   (registri in $REG/)"
fi

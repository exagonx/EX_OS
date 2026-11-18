#!/bin/bash
# =============================================================================
# tools/qt5-exos/costruisci.sh — Qt 5.15 (qtbase, qtsvg) for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/qt5-exos/costruisci.sh            32 and 64 bit
#     tools/qt5-exos/costruisci.sh 32|64      one only
#     QT_SOLO=configura|compila|installa|controlla|fine ...  one stage, on a tree already made
#
# (voce 85.) QtCore, QtGui, QtWidgets, QtNetwork, QtSql, QtXml,
# QtConcurrent, QtPrintSupport, QtTest and QtSvg as shared libraries, with
# the platform plugin "exwin" (tools/qt5-exos/qpa-exwin: the windows of
# ExWin), for VLC's interface and every Qt program.
#
# The archives: qtbase- and qtsvg-everywhere-opensource-src-5.15.19.tar.xz,
# the last public Qt 5, from https://download.qt.io/archive/qt/5.15/
# (tools/aggiorna-sorgenti.sh -scarica qt5 / qtsvg). Qt does not sign them:
# they are checked with the .sha256 the same server publishes.
#
# ! HOW IT IS BUILT:
#   - -xplatform exos-g++ (tools/qt5-exos/mkspecs, copied in by applica.py,
#     which also teaches Qt that __exos__ is Q_OS_EXOS); qmake, moc, rcc,
#     uic are built for THIS Linux (-hostprefix);
#   - -no-pkg-config: the host's pkg-config would answer for this Linux's
#     libraries (as it did for nano). FreeType, HarfBuzz, libpng, zlib,
#     PCRE2 and OpenSSL are the ones of the sysroot, built by their own
#     tools/<name>-exos;
#   - no fontconfig: the fonts are DejaVu 2.37 in /exos/qt5/lib/fonts, where
#     Qt's own font database looks (QT_QPA_FONTDIR changes it);
#   - no OpenGL for now (Mesa has EGL and GLES2: later), no D-Bus, no ICU.
#   - no QDnsLookup (MX and SRV records): EX-OS has no resolver library, the
#     libc asks for A records itself (lib/include/resolv.h);
#   - OpenSSL is 4.x, newer than Qt 5.15 knows: applica.py fixes the few
#     places (const getters, opaque ASN1_STRING).
# Result: cross_build/qt5-exos<bit>/exos/qt5 laid out as /exos/qt5, the host
# tools in cross_build/costruzione-qt5/exos<bit>/host.
# =============================================================================
NOME=qt5
VER=5.15.19
VER_qtsvg=5.15.19       # the same release as qtbase (tools/aggiorna-sorgenti.sh)
VER_dejavu=2.37
QUI="$(cd "$(dirname "$0")" && pwd)"
. "$QUI/../librerie-exos/comune.sh"
ARCHIVIO=$TP/qt5/qtbase-everywhere-opensource-src-$VER.tar.xz
SVG=$TP/qtsvg/qtsvg-everywhere-opensource-src-$VER_qtsvg.tar.xz
DEJAVU=$TP/dejavu/dejavu-fonts-ttf-$VER_dejavu.tar.bz2

configura() {
    local S=$CROSS/$T
    python3 "$QUI/applica.py" "$SRC" || exit 1
    mkdir -p "$B/obj"
    # shellcheck disable=SC2086
    # ! -no-sse2 AT 32 BIT: Qt 5 turns SSE2 on in all its libraries by
    # itself and refuses to start without it ("Incompatible processor",
    # then the stack ran out); EX-OS at 32 bit is for the Pentium MMX
    local SIMD=""
    [ "$T" = i386-exos ] && SIMD="-no-sse2"
    ( cd "$B/obj" && EXOS_T=$T EXOS_MARCH="$MARCH" "$SRC/configure" \
        -xplatform exos-g++ -platform linux-g++ \
        -prefix /exos/qt5 -extprefix "$DEST/qt5" -hostprefix "$B/host" \
        -opensource -confirm-license -release -shared -strip \
        -no-pch -no-pkg-config -no-use-gold-linker -no-opengl -no-dbus -no-glib -no-icu \
        -no-cups -no-fontconfig -no-gtk -no-xcb -no-evdev -no-libinput -no-mtdev -no-tslib \
        -no-linuxfb -no-kms -no-gbm -no-eglfs -no-directfb -no-vulkan \
        -system-freetype -system-harfbuzz -system-libpng -system-zlib -system-pcre \
        -qt-libjpeg -openssl-linked -no-sql-mysql -no-sql-psql -no-sql-odbc \
        -nomake examples -nomake tests -qpa exwin -no-feature-dnslookup $SIMD \
        -I "$S/include/freetype2" -I "$S/include/harfbuzz" -I "$S/include/libpng16" \
        ) > "$B/configura.log" 2>&1 || { tail -40 "$B/configura.log"; exit 1; }
    grep -a -A200 "^Configure summary" "$B/obj/config.summary" > "$B/riassunto.txt" 2>/dev/null || true
}

compila() {
    EXOS_T=$T EXOS_MARCH="$MARCH" make -C "$B/obj" -j4 > "$B/costruisci.log" 2>&1 || {
        EXOS_T=$T EXOS_MARCH="$MARCH" make -C "$B/obj" > "$B/costruisci2.log" 2>&1 || {
            grep -a -m20 -B2 -A6 " error\|Error [0-9]" "$B/costruisci2.log"; exit 1; }; }
}

installa() {
    EXOS_T=$T EXOS_MARCH="$MARCH" make -C "$B/obj" install > "$B/installa.log" 2>&1 \
        || { tail -20 "$B/installa.log"; exit 1; }
    # qtsvg: qmake of the host tools on its .pro, into the same prefix
    # its sources and its build in two places, made again every time (with
    # one, a second run found the build's bin/ as the sources)
    rm -rf "$B/svg-src" "$B/svg"; mkdir -p "$B/svg-src" "$B/svg"
    tar -xf "$SVG" -C "$B/svg-src"
    local SS; SS=$(ls -d "$B"/svg-src/*/ | head -1)
    ( cd "$B/svg" && EXOS_T=$T EXOS_MARCH="$MARCH" "$B/host/bin/qmake" "$SS" && \
      EXOS_T=$T EXOS_MARCH="$MARCH" make -j4 && EXOS_T=$T EXOS_MARCH="$MARCH" make install ) \
        > "$B/svg.log" 2>&1 || { grep -a -m10 -B2 -A6 " error" "$B/svg.log"; tail "$B/svg.log"; exit 1; }
    # the fonts: DejaVu, where Qt's font database looks without fontconfig
    mkdir -p "$DEST/qt5/lib/fonts" "$B/dejavu"
    tar -xjf "$DEJAVU" -C "$B/dejavu"
    cp "$B"/dejavu/*/ttf/DejaVu{Sans,Sans-Bold,Sans-Oblique,Sans-BoldOblique,SansMono,SansMono-Bold,Serif,Serif-Bold}.ttf \
        "$DEST/qt5/lib/fonts/"
    cp "$B"/dejavu/*/LICENSE "$DEST/qt5/lib/fonts/LICENSE-DejaVu"
    # the host tools stay here; what the sysroots need to build a Qt program
    # with the cross compiler is the tree itself (qmake -query names it)
    rm -rf "$DEST/qt5/doc"
    # ! NO SYMBOLIC LINKS (EX-OS has none): the real file takes its soname,
    # libQt5X.so.5, and libQt5X.so is an ld script for the linker (lib_per_ld)
    local f so base
    for f in "$DEST"/qt5/lib/libQt5*.so.$VER; do
        [ -f "$f" ] || continue
        so=$("$CROSS/bin/$T-readelf" -d "$f" | sed -n 's/.*Library soname: \[\(.*\)\]/\1/p')
        base=${so%%.so.*}
        rm -f "$DEST/qt5/lib/$base.so" "$DEST/qt5/lib/$base.so.5" "$DEST/qt5/lib/$base.so.5.15"
        mv "$f" "$DEST/qt5/lib/$so"
        lib_per_ld "$DEST/qt5/lib" "${base#lib}" "$so"
    done
    f=$(find "$DEST/qt5" -type l | head -3)
    [ -z "$f" ] || { echo "qt5: collegamenti simbolici rimasti: $f" >&2; exit 1; }
    cp "$SRC/LICENSE.LGPL3" "$SRC/LICENSE.GPL3" "$DEST/qt5/" 2>/dev/null || true
}

# Every undefined symbol of the libraries and plugins has an owner: the
# libc's table (lib/libc_esportati.h, the program gives it), a .so of the
# sysroot (libstdc++, libm, zlib, FreeType...) or another Qt library; every
# DT_NEEDED is a file that will be there. The linker no longer says it
# (QMAKE_LFLAGS_NOUNDEF is empty in the mkspec), so this does.
controlla() {
    python3 - "$R/lib/libc_esportati.h" "$CROSS/bin/$T-nm" "$CROSS/bin/$T-readelf" \
        "$DEST/qt5" "$CROSS/$T/lib" <<'PY' || { echo "qt5: simboli senza padrone" >&2; exit 1; }
import glob, os, re, subprocess, sys
tabella, nm, readelf, qt, sys_lib = sys.argv[1:6]
definiti = set(re.findall(r'asciz \\"([^\\]+)\\"', open(tabella).read()))
def simboli(f, quali):
    r = subprocess.run([nm, "-D", quali, f], capture_output=True, text=True).stdout
    # the version goes (Qt's libraries have one, Qt_5: name@Qt_5 asked,
    # name@@Qt_5 defined); the libc's loader looks names up without it
    return [l.split()[:-1] + [l.split()[-1].split("@")[0]] for l in r.splitlines() if l.strip()]
nostre = sorted(set(glob.glob(qt + "/lib/*.so.*") + glob.glob(qt + "/plugins/*/*.so")))
nostre = [f for f in nostre if not os.path.islink(f)]
sistema = [f for f in glob.glob(sys_lib + "/*.so*") if os.path.isfile(f)]
nomi = {os.path.basename(f) for f in glob.glob(qt + "/lib/*.so*") + glob.glob(sys_lib + "/*.so*")}
for f in nostre + sistema:
    definiti |= {p[-1] for p in simboli(f, "--defined-only")}
male = 0
for f in nostre:
    for p in simboli(f, "--undefined-only"):
        if p[-1] not in definiti and p[-2] not in ("w", "v"):
            print("  %s: %s non ha padrone" % (os.path.basename(f), p[-1])); male += 1
    d = subprocess.run([readelf, "-d", f], capture_output=True, text=True).stdout
    for n in re.findall(r"\(NEEDED\).*\[(.*)\]", d):
        if n not in nomi:
            print("  %s: chiede %s, che non c'e'" % (os.path.basename(f), n)); male += 1
print("  %d librerie e plugin di Qt: %s" % (len(nostre),
      "ogni simbolo ha un padrone" if not male else "%d mancanti" % male))
sys.exit(1 if male else 0)
PY
}

# the sources, the script and the patches in exos/src/qt5 (for the CD)
fine() { lib_fine "$SVG" "$DEJAVU" "$QUI/applica.py"; }

costruisci() {
    lib_per "$1"
    for f in "$SVG" "$DEJAVU"; do [ -f "$f" ] || manca "$f" "tools/aggiorna-sorgenti.sh -scarica qtsvg (o dejavu)"; done
    lib_sorgente
    configura
    compila
    installa
    controlla
    fine
}

if [ -n "${QT_SOLO:-}" ]; then
    # one stage on the tree of a previous run (lib_per would empty it)
    BIT=${1:-64}
    case $BIT in
        64) T=x86_64-exos; CROSS=$TC/nasi5deb/exos-cross64; MARCH="" ;;
        32) T=i386-exos;   CROSS=$TC/nasi5deb/exos-cross;   MARCH="-march=pentium-mmx -mtune=pentium-mmx" ;;
    esac
    B=$TC/costruzione-$NOME/exos$BIT; OUT=$TC/$NOME-exos$BIT; DEST=$OUT/exos
    SRC=$(ls -d "$B"/src/*/ | head -1); SRC=${SRC%/}
    export PATH="$B/pic:$CROSS/bin:$PATH"
    "$QT_SOLO"
    exit
fi
lib_tutte "$1"

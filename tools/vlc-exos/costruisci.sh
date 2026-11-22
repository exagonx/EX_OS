#!/bin/bash
# =============================================================================
# tools/vlc-exos/costruisci.sh - VLC media player for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/vlc-exos/costruisci.sh [32|64]      both by default
#     VLC_SOLO_COLLEGA=1 tools/vlc-exos/costruisci.sh 64
#                     only the outputs of EX-OS and make install, on a tree built
#
# The archive: vlc-3.0.24.tar.xz with .sha256 from
# https://download.videolan.org/pub/videolan/vlc/3.0.24/
# VLC decodes through tools/ffmpeg-exos.
# (18 November 2026, voce 85) The Qt interface: Qt 5 from tools/qt5-exos
# (cross_build/qt5-exos<bit>, built first), shared, linked to bin/vlc. The
# video plays inside it, in a child window of ExWin (applica.py, voce 105
# step 5); with -I dummy or ncurses vout_exos makes a window of its own.
# ! VLC AS ON LINUX (voce 105, step 4, 20 November 2026): bin/vlc on
# lib/libvlc.so.5 and lib/libvlccore.so.9, and every plugin a .so in
# lib/vlc/plugins/<kind>/, opened with dlopen. Until then EX-OS had no
# dlopen of ordinary shared objects and every plugin was linked into one
# program: three plugins of the same name were lost (VLC names a module
# after its source file: flac, vc1, file), two tables had to be made static
# by sed, and the FLAC packetizer was missing, so no FLAC played.
# On the disk: /exos/bin/vlc, /exos/lib/libvlc*.so.N, /exos/lib/vlc/plugins
# (VLC's PKGLIBDIR, --prefix=/exos), /exos/lib/vlc/vlc-cache-gen.
# =============================================================================
NOME=vlc
VER=3.0.24
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/vlc/vlc-$VER.tar.xz

# make install into $B/inst, the two outputs of EX-OS as plugins next to
# VLC's own, then DEST: the program, the two libraries, the plugins, the
# headers of libvlc.
raccogli() {
    local O=$B/obj I=$B/inst P m n d f so x
    [ -f "$O/src/.libs/libvlccore.so" ] && [ -f "$O/lib/.libs/libvlc.so" ] && [ -f "$O/bin/.libs/vlc" ] \
        || { grep -a -E " error:" "$B/costruisci.log" | sort -u | head -30; exit 1; }
    rm -rf "$I"
    # only the parts that go on the disk: share/ wants the AppStream file
    # that VLC 3.0.24 generates in the build tree and installs from the
    # source tree (out-of-tree builds stop there), and EX-OS has no use for it
    for d in compat src modules lib bin; do
        LC_ALL=C make -C "$O/$d" install DESTDIR="$I" >> "$B/installa.log" 2>&1 \
            || { tail -20 "$B/installa.log"; exit 1; }
    done
    P=$I/exos/lib/vlc/plugins
    [ -d "$P/codec" ] || { echo "vlc: make install non ha messo i plugin"; tail -20 "$B/installa.log"; exit 1; }
    # --- the outputs of EX-OS: sound on the "audio" service, pictures in
    # ExWin; built as VLC builds its plugins (common.am: __PLUGIN__, only
    # the vlc_entry symbols exported, on libvlccore)
    printf '{ global: vlc_entry*; local: *; };\n' > "$B/plugin.ver"
    for m in aout_exos:audio_output vout_exos:video_output; do
        n=${m%%:*}; d=${m#*:}
        $CC $CFLAGS -D__PLUGIN__ -D__unix__ -DEXOS_SOLO_POSIX -D_GNU_SOURCE -std=gnu11 -DHAVE_CONFIG_H \
            -I"$O" -I"$SRC/include" -I"$O/include" -I"$SRC/modules/audio_output" -I"$B/libc/include" \
            -DMODULE_STRING=\"$n\" -c "$R/tools/vlc-exos/src/$n.c" -o "$B/$n.o" || exit 1
        mkdir -p "$P/$d"
        $CC -shared -o "$P/$d/lib${n}_plugin.so" "$B/$n.o" $NONSHARED -L"$B/libc" -L"$O/src/.libs" -lvlccore -lm \
            -Wl,--version-script="$B/plugin.ver" || exit 1
    done
    # --- DEST
    rm -rf "$DEST/bin" "$DEST/lib/vlc" "$DEST"/lib/libvlc* "$DEST/include/vlc"
    mkdir -p "$DEST/bin" "$DEST/lib/vlc" "$DEST/include"
    cp "$I/exos/bin/vlc" "$DEST/bin/vlc"
    # the real file under its soname, as lib_condivisa_sua: no symbolic
    # links on EX-OS
    for f in "$I"/exos/lib/libvlc.so.*.*.* "$I"/exos/lib/libvlccore.so.*.*.*; do
        so=$($T-readelf -d "$f" | sed -n 's/.*Library soname: \[\(.*\)\]/\1/p')
        [ -n "$so" ] || { echo "vlc: $f senza soname"; exit 1; }
        cp "$f" "$DEST/lib/$so"
    done
    ( cd "$I/exos/lib/vlc" && find plugins -name '*.so' -exec cp --parents {} "$DEST/lib/vlc/" \; )
    cp "$I/exos/lib/vlc/vlc-cache-gen" "$DEST/lib/vlc/"
    cp -r "$I/exos/include/vlc" "$DEST/include/"
    # ! NO TEXT RELOCATIONS: every page of code is shared by the kernel
    # (kernel/loader/condivise.c); a TEXTREL would make it a private copy
    x=$(find "$DEST/lib" -name '*.so*' -exec sh -c "$T-readelf -d \"\$1\" | grep -q TEXTREL && echo \"\$1\"" _ {} \;)
    [ -z "$x" ] || { echo "vlc: TEXTREL in"; echo "$x"; exit 1; }
    n=$(find "$DEST/lib/vlc/plugins" -name '*.so' | wc -l)
    echo "    bin/vlc, $(cd "$DEST/lib" && ls libvlc*.so.* | tr '\n' ' ')e $n plugin in lib/vlc/plugins"
}

costruisci() {
    # VLC_SOLO_COLLEGA keeps the built tree (see below)
    [ "${VLC_SOLO_COLLEGA:-}" = 1 ] && LIB_TIENI=1
    lib_per "$1"
    BIT=$1
    local Q=$TC/qt5-exos$BIT/exos/qt5
    [ -f "$Q/lib/pkgconfig/Qt5Widgets.pc" ] || manca "$Q" "tools/qt5-exos/costruisci.sh $BIT (the Qt interface)"
    # ! VLC_SOLO_COLLEGA=1: only aout_exos/vout_exos and make install, on a
    # tree already configured and built (minutes instead of the whole VLC)
    if [ "${VLC_SOLO_COLLEGA:-}" = 1 ] && [ -f "$B/obj/src/.libs/libvlccore.so" ]; then
        SRC=$(ls -d "$B"/src/*/ | head -1); SRC=${SRC%/}
        lib_libc_di_oggi
        raccogli
        lib_fine "$R"/tools/vlc-exos/src/*.c
        return
    fi
    lib_sorgente
    lib_config_sub
    lib_libtool_exos
    # ! the libc of today (comune.sh): wcswidth for the ncurses interface
    # came on 18 November 2026, after the sysroots. C only: the C++ headers
    # of libstdc++ must stay in front of the libc's (<cstdlib> and
    # #include_next <stdlib.h>), so the C++ files see the sysroot's
    lib_libc_di_oggi
    # ! VLC'S OWN POSIX THREADS, error.c and netconf.c (voce 105, 19 November
    # 2026): until kernel 0.349 EX-OS had no cancellation of a thread that
    # waits, no clock_nanosleep, strerror_l, posix_spawn, and VLC was built
    # on its Android subset with futex stand-ins and a netconf of its own
    # bin/override.c AS IT IS (voce 105): putenv, setenv, setlocale... of
    # vlc win over the libc's for the plugins, and reach the real ones with
    # dlsym(RTLD_NEXT), now that the libc is /lib/ld-exos.so. With libc.a
    # inside they were two definitions of one name, and override.c was
    # replaced by an empty vlc_enable_override.
    # ! THE VIDEO INSIDE THE INTERFACE (voce 105 step 5, 21 November 2026):
    # VLC learns the windows of ExWin (applica.py: VOUT_WINDOW_TYPE_EXWIN,
    # the Qt platform "exwin"), vout_exos makes a child window in the Qt
    # video widget. Until then embedded-video was set to 0 by sed and the
    # video had a window of its own.
    python3 "$R/tools/vlc-exos/applica.py" "$SRC" || exit 1
    # QPlatformNativeInterface, for the id of the video widget's window:
    # Qt 5 keeps qpa/ under the version (VLC finds it with qmake only for Qt 6)
    QPA=$(ls -d "$Q"/include/QtGui/5.*/QtGui | head -1)
    [ -f "$QPA/qpa/qplatformnativeinterface.h" ] || { echo "vlc: manca qpa/qplatformnativeinterface.h in $Q"; exit 1; }
    mkdir -p "$B/obj" "$B/pc"
    S=$CROSS/$T
    pc() { printf 'Name: %s\nDescription: %s\nVersion: %s\nLibs: %s\nCflags:\n' "$1" "$1" "$2" "$3" > "$B/pc/$1.pc"; }
    pc ogg 1.3.6 "-logg"
    pc vorbis 1.3.7 "-lvorbis -logg -lm"
    pc vorbisenc 1.3.7 "-lvorbisenc -lvorbis -logg -lm"
    pc opus 1.6.1 "-lopus -lm"
    sed -i "s#^Cflags:.*#Cflags: -I$S/include/opus#" "$B/pc/opus.pc"
    pc flac 1.5.0 "-lFLAC -logg -lm"
    pc dav1d 1.5.4 "-ldav1d -lpthread -lm"
    pc libavutil 60.0.0 "-lavutil -lm -lpthread"
    pc libavcodec 62.0.0 "-lavcodec -lvpx -ldav1d -lopus -lvorbisenc -lvorbis -logg -lz -lswresample -lavutil -lm -lpthread"
    pc libavformat 62.0.0 "-lavformat -lavcodec -lvpx -ldav1d -lopus -lvorbisenc -lvorbis -logg -lz -lswresample -lavutil -lm -lpthread"
    pc libswscale 9.0.0 "-lswscale -lavutil -lm"
    pc libswresample 6.0.0 "-lswresample -lavutil -lm"
    # ! the ncurses interface (vlc -I ncurses, nvlc) asks for ncursesw, but
    # only for the name: it calls no wide curses function, only mbstowcs,
    # wcwidth and wcswidth of the libc. Ours is the byte libncurses
    # (tools/ncurses-exos, voce 30): a UTF-8 title shows its accents as two
    # signs, as in nano
    pc ncursesw 6.6 "-lncurses"
    # -rpath-link: a cross ld looks for what a .so needs (libvlc.so needs
    # libvlccore.so.9, not installed yet) only there, never in -rpath
    ( cd "$B/obj" && PKG_CONFIG_LIBDIR="$B/pc:$Q/lib/pkgconfig" "$SRC/configure" --host=$T --build=x86_64-pc-linux-gnu \
        --prefix=/exos CC=$CC CXX=$CXX AR=$AR RANLIB=$RANLIB \
        CFLAGS="$CFLAGS -I$B/libc/include -D__unix__ -DEXOS_SOLO_POSIX -D_GNU_SOURCE -std=gnu11 -Wno-error=incompatible-pointer-types" \
        CXXFLAGS="$CFLAGS -fPIC -D__unix__ -DEXOS_SOLO_POSIX -D_GNU_SOURCE -I$QPA" LDFLAGS="-L$B/libc $NONSHARED -Wl,-rpath,/exos/qt5/lib -Wl,-rpath-link,$Q/lib -Wl,-rpath-link,$CROSS/$T/lib -Wl,-rpath-link,$B/obj/src/.libs" \
        --enable-shared --disable-static --disable-nls --disable-lua --enable-qt --disable-skins2 \
        --disable-xcb --disable-xvideo --disable-alsa --disable-pulse --disable-jack --disable-oss \
        --disable-dbus --disable-a52 --disable-mad --disable-libgcrypt --disable-gnutls --disable-vlm \
        --disable-sout --disable-addonmanagermodules --disable-avahi --disable-udev --disable-mtp \
        --disable-upnp --disable-libva --disable-vdpau --disable-vcd --disable-dvdread \
        --disable-dvdnav --disable-bluray --disable-v4l2 --disable-freetype --disable-fribidi \
        --disable-fontconfig --disable-svg --disable-svgdec --disable-libxml2 --disable-sftp \
        --disable-smbclient --disable-libcddb --disable-screen --disable-vnc --disable-archive \
        --disable-chromecast --disable-secret --disable-kwallet --disable-notify --disable-taglib \
        --disable-libass --disable-aribsub --disable-opencv --disable-goom --disable-projectm \
        --disable-vsxu --disable-postproc --disable-libmpeg2 \
        --enable-avcodec --enable-avformat --enable-swscale --enable-ogg --enable-opus --enable-vorbis \
        --enable-flac --enable-run-as-root --enable-ncurses \
        ) > "$B/configura.log" 2>&1 || { tail -20 "$B/configura.log"; exit 1; }
    grep -q "^QT_LIBS = .*Qt5Widgets" "$B/obj/Makefile" || { echo "vlc: configure non ha trovato Qt"; grep -a -A3 "Qt5Core" "$B/configura.log" | tail -8; exit 1; }
    # ! NO glspectrum: Mesa installs GL/gl.h, so configure says HAVE_GL,
    # but EX-OS has only EGL and GLES2, no libGL to link -lGL with (the
    # OpenGL output, libgl_plugin, takes its functions through EGL and is
    # built). Its line of the generated Makefile goes; nothing else may fail
    sed -i 's/^\(am__append_[0-9]* = libglspectrum_plugin.la\)$/# \1 (no libGL on EX-OS: tools\/vlc-exos\/costruisci.sh)/' "$B/obj/modules/Makefile"
    grep -q '^# am__append_[0-9]* = libglspectrum_plugin.la' "$B/obj/modules/Makefile" || { echo "vlc: glspectrum non trovato nel Makefile"; exit 1; }
    make -C "$B/obj" -j8 > "$B/costruisci.log" 2>&1 \
        || { grep -a -E " error:|undefined reference|\*\*\* \[" "$B/costruisci.log" | sort -u | head -30; exit 1; }
    [ -f "$B/obj/modules/.libs/libqt_plugin.so" ] || { echo "vlc: l'interfaccia Qt non si e' costruita"; grep -a " error" "$B/costruisci.log" | grep -a "gui/qt" | sort -u | head -20; exit 1; }
    raccogli
    lib_fine "$R"/tools/vlc-exos/src/*.c
}
lib_tutte "$1"

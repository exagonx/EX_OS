#!/bin/bash
# =============================================================================
# tools/vlc-exos/costruisci.sh - VLC media player for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/vlc-exos/costruisci.sh [32|64]      both by default
#     VLC_SOLO_COLLEGA=1 tools/vlc-exos/costruisci.sh 64
#                     only the outputs of EX-OS and the link, on a tree built
#
# The archive: vlc-3.0.24.tar.xz with .sha256 from
# https://download.videolan.org/pub/videolan/vlc/3.0.24/
# VLC decodes through tools/ffmpeg-exos. EX-OS has no dlopen: every plugin is
# linked into one program.
# =============================================================================
NOME=vlc
VER=3.0.24
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/vlc/vlc-$VER.tar.xz

# The link: aout_exos and vout_exos compiled, every plugin listed, one vlc.
collega() {
    # ! vlc-static does not link (no modules list): only the libraries and
    # the plugins must be there
    [ -f "$B/obj/src/.libs/libvlccore.a" ] && [ -f "$B/obj/lib/.libs/libvlc.a" ] \
        || { grep -a -E " error:" "$B/costruisci.log" | sort -u | head -30; exit 1; }
    # --- one program: every plugin, listed by its entry point -------------
    local P=$B/obj/modules/.libs nomi
    # the outputs of EX-OS: sound on the "audio" service, pictures in ExWin
    local m propri=""
    for m in aout_exos vout_exos; do
        $CC $CFLAGS -D__unix__ -DEXOS_SOLO_POSIX -D_GNU_SOURCE -std=gnu11 -DHAVE_CONFIG_H \
            -I"$B/obj" -I"$SRC/include" -I"$B/obj/include" \
            -DMODULE_STRING=\"$m\" -DMODULE_NAME=$m \
            -c "$R/tools/vlc-exos/src/$m.c" -o "$B/$m.o" || exit 1
        propri="$propri $B/$m.o"
    done
    nomi=$( { for a in "$P"/lib*_plugin.a; do $T-nm "$a" | sed -n 's/^[0-9a-f]* T vlc_entry__\(.*\)$/\1/p'; done
              echo aout_exos; echo vout_exos; } | sort -u)
    {
        echo '/* tools/vlc-exos/costruisci.sh: every plugin linked into vlc */'
        echo 'typedef int (*vlc_set_cb) (void *, void *, int, ...);'
        echo 'typedef int (*vlc_plugin_cb) (vlc_set_cb, void *);'
        for m in $nomi; do echo "int vlc_entry__$m(vlc_set_cb, void *);"; done
        echo 'vlc_plugin_cb vlc_static_modules[] = {'
        for m in $nomi; do echo "    vlc_entry__$m,"; done
        echo '    0 };'
    } > "$B/moduli.c"
    $CC $CFLAGS -c "$B/moduli.c" -o "$B/moduli.o"
    # ! NOT -static (3 November 2026): FFmpeg, libvpx, dav1d, FLAC, Opus,
    # Vorbis, Ogg and zlib are shared libraries, found at start in /exos/lib
    # (lib/libc_dl.c); VLC's own plugins stay inside, there is no dlopen of
    # VLC modules
    $CXX -o "$B/vlc" "$B/obj/bin/vlc-vlc.o" "$B/obj/bin/vlc-override.o" "$B/moduli.o" $propri \
        -Wl,--start-group "$P"/lib*_plugin.a $(ls "$B"/obj/modules/.libs/lib*.a | grep -v _plugin.a) \
        "$B/obj/lib/.libs/libvlc.a" "$B/obj/src/.libs/libvlccore.a" \
        $(ls "$B"/obj/compat/.libs/libcompat.a 2>/dev/null) \
        -lavformat -lavcodec -lswscale -lswresample -lavutil -lvpx -ldav1d \
        -lFLAC -lopus -lvorbisenc -lvorbis -logg -lz -lm -lpthread -Wl,--end-group \
        > "$B/collega.log" 2>&1 || { grep -a -E "undefined reference|multiple definition" "$B/collega.log" \
            | sed -E 's/.*(undefined reference to .*|multiple definition of [^;]*).*/\1/' | sort | uniq -c | sort -rn | head -30; exit 1; }
    mkdir -p "$DEST/bin"
    cp "$B/vlc" "$DEST/bin/vlc"
    echo "    $(echo $nomi | wc -w) moduli dentro bin/vlc"
    # our own sources go with the archive: the thread calls, the proxy, and
    # the two outputs of EX-OS
}

costruisci() {
    lib_per "$1"
    # ! VLC_SOLO_COLLEGA=1: only aout_exos/vout_exos and the link, on a tree
    # already configured and built (minutes instead of the whole VLC)
    if [ "${VLC_SOLO_COLLEGA:-}" = 1 ] && [ -f "$B/obj/src/.libs/libvlccore.a" ]; then
        SRC=$(ls -d "$B"/src/*/ | head -1); SRC=${SRC%/}
        collega
        lib_fine "$R"/tools/vlc-exos/src/*.c
        return
    fi
    lib_sorgente
    lib_config_sub
    # ! no shared objects here: the plugins come out as static archives and
    # are joined in one program, as VLC's mobile builds do
    sed -i -E '/VLC is based on plugins/ s/as_fn_error .*/:/' "$SRC/configure"
    # ! no pthread_cancel: the threads of VLC's Android port, which has the
    # same subset; the futex calls they need are in src/indirizzi.c
    sed -i 's/^#elif defined (__ANDROID__)      \/\* pthreads subset/#elif defined (__ANDROID__) || defined (__exos__) \/* pthreads subset/' \
        "$SRC/include/vlc_threads.h"
    grep -q '__exos__' "$SRC/include/vlc_threads.h" || { echo "vlc_threads.h: riga non trovata"; exit 1; }
    cp "$SRC/src/android/thread.c" "$SRC/src/posix/thread.c"
    cat "$R/tools/vlc-exos/src/indirizzi.c" >> "$SRC/src/posix/thread.c"
    cp "$SRC/src/android/error.c" "$SRC/src/posix/error.c"
    cp "$R/tools/vlc-exos/src/netconf.c" "$SRC/src/posix/netconf.c"
    # override.c wraps libc functions that are not thread-safe on Linux: in one
    # static program they are two definitions of the same name
    echo '/* EX-OS: nothing to override in a static program */ void vlc_enable_override(void) {}' > "$SRC/bin/override.c"
    # two plugins export the same table name: in one program it must be local
    sed -i 's/^const char \*const ppsz_mode_descriptions\[\]/static &/' \
        "$SRC/modules/video_chroma/swscale.c" "$SRC/modules/video_filter/puzzle.c"
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
    ( cd "$B/obj" && PKG_CONFIG_LIBDIR="$B/pc" "$SRC/configure" --host=$T --build=x86_64-pc-linux-gnu \
        --prefix=/exos/vlc CC=$CC CXX=$CXX AR=$AR RANLIB=$RANLIB \
        CFLAGS="$CFLAGS -D__unix__ -DEXOS_SOLO_POSIX -D_GNU_SOURCE -std=gnu11 -Wno-error=incompatible-pointer-types" \
        CXXFLAGS="$CFLAGS -D__unix__ -DEXOS_SOLO_POSIX -D_GNU_SOURCE" LDFLAGS=-static \
        --disable-shared --enable-static --disable-nls --disable-lua --disable-qt --disable-skins2 \
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
        --enable-flac --enable-run-as-root \
        ) > "$B/configura.log" 2>&1 || { tail -20 "$B/configura.log"; exit 1; }
    make -C "$B/obj" -k -j8 > "$B/costruisci.log" 2>&1 || true
    collega
    lib_fine "$R"/tools/vlc-exos/src/*.c
}
lib_tutte "$1"

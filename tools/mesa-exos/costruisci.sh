#!/bin/bash
# =============================================================================
# tools/mesa-exos/costruisci.sh — Mesa (OpenGL, EGL, GLES) for EX-OS (voce 88)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/mesa-exos/costruisci.sh [32|64]     both by default
#
# Builds libEGL.so.1 and libGLESv2.so.2 with the softpipe driver, in
# cross_build/mesa-exos<bit>/exos (lib/, include/EGL GLES2 GLES3 KHR) and in
# the sysroots, as every library of tools/librerie-exos/comune.sh.
#
# The sources: the signed archive cross_build/terze-parti/mesa/mesa-$VER.tar.xz
# (tools/aggiorna-sorgenti.sh -scarica mesa; key: mesa-keys.asc, Eric
# Engestrom, the release manager) plus exos-$VER.patch here: the "exos"
# platform (system 'exos' in meson, src/egl/drivers/exos, the winsys
# src/gallium/winsys/sw/exos). The patch is made on the clone mesa/ at the
# project root, branch exos-$VER (tools/aggiorna-sorgenti.sh -clone mesa):
#     cd mesa && git diff > ../tools/mesa-exos/exos-$VER.patch
#     (and git diff --no-index /dev/null FILE >> ... for the new files)
#
# What it needs on the build machine: meson, ninja, bison, flex, glslang is
# NOT needed; the Python modules mako and pyyaml in
# cross_build/costruzione-mesa/pymod (PYTHONPATH), taken from PyPI as wheels
# checked with their sha256 (the host has no pip).
#
# ! system = 'exos' IN THE CROSS FILE, not 'linux' as for the other libraries
# (comune.sh): for "linux" Mesa wants DRM/KMS and libdrm, and EGL only
# through DRI. The patch teaches Mesa what 'exos' is.
#
# ! ONLY THE SURFACES IN MEMORY for now (the user, 7 November 2026): pbuffer
# and surfaceless; EGL windows on ExWin come later. Test: prova-egl.sh.
# =============================================================================
NOME=mesa
VER=26.2.6
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/mesa/mesa-$VER.tar.xz
PATCH=$R/tools/mesa-exos/exos-$VER.patch
PYMOD=$TC/costruzione-mesa/pymod

costruisci() {
    for p in meson ninja bison flex; do
        command -v $p > /dev/null || manca "$p" "apt install $p (sulla macchina di costruzione)"
    done
    PYTHONPATH=$PYMOD python3 -c 'import mako, yaml' 2> /dev/null ||
        manca "mako e pyyaml in $PYMOD" "le ruote di PyPI (mako, markupsafe, pyyaml) scompattate li', con lo sha256 controllato"
    lib_per "$1"
    lib_sorgente
    patch -d "$SRC" -p1 --quiet < "$PATCH" || { echo "la patch $PATCH non si applica"; exit 1; }

    local cpu=${T%-exos} fam=x86_64 f=""
    [ "$cpu" = i386 ] && fam=x86
    for x in $MARCH -D__unix__ -DEXOS_SOLO_POSIX; do f="$f'$x', "; done
    cat > "$B/exos.meson" <<FINE
[binaries]
c = '$B/pic/$CC'
cpp = '$B/pic/$CXX'
ar = '$CROSS/bin/$AR'
strip = '$CROSS/bin/$STRIP'
pkg-config = 'pkg-config'
[properties]
pkg_config_libdir = '$B/pc'
[built-in options]
c_args = [${f%, }]
cpp_args = [${f%, }]
c_link_args = [$( [ -n "$NONSHARED" ] && echo "'$NONSHARED'" )]
cpp_link_args = [$( [ -n "$NONSHARED" ] && echo "'$NONSHARED'" )]
[host_machine]
system = 'exos'
cpu_family = '$fam'
cpu = '$cpu'
endian = 'little'
FINE
    lib_pc "$B/pc" zlib 1.3.1 "" "-lz"

    # ! -Db_lundef=false: a .so of EX-OS leaves the libc undefined (malloc,
    # __tls_get_addr...): lib/libc_dl.c binds it to the program's table at
    # dlopen. lib_controlla checks afterwards that every name has an owner.
    PYTHONPATH=$PYMOD meson setup "$B/obj" "$SRC" --cross-file "$B/exos.meson" \
        --prefix=/exos --libdir=lib --buildtype=release -Db_ndebug=true -Db_lundef=false \
        -Dplatforms=exos -Dgallium-drivers=softpipe -Dvulkan-drivers= -Dllvm=disabled \
        -Degl=enabled -Dgles1=disabled -Dgles2=enabled -Dglx=disabled -Dgbm=disabled \
        -Dglvnd=disabled -Dshader-cache=disabled -Dzstd=disabled -Dlibunwind=disabled \
        -Dvalgrind=disabled -Dxmlconfig=disabled -Dlmsensors=disabled -Dexpat=disabled \
        -Dbuild-tests=false -Dtools= -Dvideo-codecs= -Dgallium-va=disabled \
        -Dgallium-rusticl=false -Dteflon=false \
        > "$B/configura.log" 2>&1 || { tail -30 "$B/configura.log"; exit 1; }
    PYTHONPATH=$PYMOD ninja -C "$B/obj" -j"$(nproc)" > "$B/costruisci.log" 2>&1 ||
        { grep -m10 -B2 -A8 'rror' "$B/costruisci.log"; exit 1; }
    DESTDIR="$OUT" ninja -C "$B/obj" install > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    # meson installs in $OUT/exos: the layout comune.sh wants
    rm -rf "$DEST/lib/pkgconfig" "$DEST/share"
    # Mesa's own build makes the shared objects (the glapi tables are shared
    # between them): the real file under its soname, no symbolic links
    lib_condivisa_sua EGL 1 "-lstdc++ -lgcc_s -lz -lm"
    lib_condivisa_sua GLESv2 2 "-lEGL -lstdc++ -lgcc_s -lm"
    lib_fine "$PATCH" "$R/tools/mesa-exos/prova-egl.c"
}
lib_tutte "$1"

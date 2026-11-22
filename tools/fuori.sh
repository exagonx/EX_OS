#!/bin/sh
# =============================================================================
# tools/fuori.sh - where the build puts what it makes: OUTSIDE the source tree
#
#     sh tools/fuori.sh            prints the root, makes its folders
#     EXOS_FUORI=/altrove sh tools/fuori.sh
#
# The source tree (synced by MEGA) keeps only sources, the finished ISOs and
# dist/netinst, dist/netinst_64. Everything else is in <home of the user>/ex_os:
#
#     $R/cross_build         toolchains, third-party builds, Firefox objects
#     $R/build               objects at 32 bit (make)
#     $R/build-64            objects at 64 bit (make ARCH=x86_64)
#     $R/dischi              disk images of the QEMU tests
#     $R/stress/risultati    results of the load tests
#
# ! NO LINKS IN THE TREE: MEGA chokes on them (22/11/2026). The Makefile and
# the scripts use $EXOS_FUORI directly; each script finds it by itself with
# the same rule as here, so it works without make too.
#
# ! THE USER IS THE ONE WHO BUILDS, NOT root: under sudo $HOME is /root.
# SUDO_USER first, then id -un; the home from the password database, $HOME
# only if that says nothing.
# =============================================================================
if [ -z "${EXOS_FUORI:-}" ]; then
    U="${SUDO_USER:-$(id -un)}"
    H="$(getent passwd "$U" 2>/dev/null | cut -d: -f6)"
    EXOS_FUORI="${H:-$HOME}/ex_os"
fi

for d in cross_build build build-64 dischi stress/risultati; do
    mkdir -p "$EXOS_FUORI/$d" 2>/dev/null
done

echo "$EXOS_FUORI"

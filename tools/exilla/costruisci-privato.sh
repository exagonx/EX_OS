#!/bin/sh
# =============================================================================
# tools/exilla/costruisci-privato.sh — make in una copia PRIVATA dell'albero
#
# ! PERCHE' ESISTE (28 settembre 2026). Il `make` nella cartella condivisa lo
# lancia solo l'altro PC (ISTRUZIONI-SECONDO-PROFILO.md, 3.1). E NON BASTA
# `make BUILD_DIR=... DIST_DIR=...`: tools/mkfloppy.sh e gli altri script
# scrivono dist/ e leggono build/ PER NOME FISSO — provandolo, il floppy
# condiviso e' stato riscritto (e rimesso a posto dal CD). Quindi si lavora in
# una COPIA VERA dei sorgenti, in cross_build/<macchina>/costruzione-sistema/
# albero (esclusa da MEGA), con build/ e dist/ suoi.
#
#     tools/exilla/costruisci-privato.sh [bersaglio...]   (default: iso-exos)
#
# Prima allinea la copia ai sorgenti condivisi (rsync, senza toccare build/ e
# dist/ della copia), poi lancia make la' dentro. Le modifiche ai sorgenti si
# fanno nel progetto, con PRENDO/LASCIO in scambio.txt: la copia non si tocca.
#
# ! UN ADATTAMENTO DELLA COPIA: in build/bin sei programmi (chown, halt,
# poweroff, reboot, whoami, sh) non stanno sul floppy dell'altro PC, dove non
# hanno il bit di esecuzione (MEGA non porta i permessi); qui si toglie anche
# nella copia, o il floppy da 1,44 MB trabocca. Vedi @FLOPPY-PIENO.
# =============================================================================
set -e
cd "$(dirname "$0")/../.."
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
COPIA="$RADICE/cross_build/$MACCHINA/costruzione-sistema/albero"
mkdir -p "$COPIA"

rsync -a --delete \
    --exclude=/firefox-main --exclude=/rust --exclude=/gcc --exclude=/dischi \
    --exclude=/cross_build --exclude=/.git --exclude=/dist --exclude=/build --exclude=/build-64 \
    ./ "$COPIA/"
mkdir -p "$COPIA/dist" "$COPIA/build/bin"
# ! I PROGRAMMI CHE L'ALBERO VERO NON HA PIU' IN build/bin SI TOLGONO DALLA COPIA
# (30 settembre 2026): rsync non tocca build/, e un programma spostato sul CD
# (shmtest) restava qui e finiva sul floppy — verifica-dipendenze-floppy lo
# fermava come dipendenza mancante.
for f in "$COPIA"/build/bin/*; do
    [ -f "$f" ] || continue
    [ -e "build/bin/$(basename "$f")" ] || rm -f "$f"
done
for p in chown halt poweroff reboot whoami sh; do
    [ -f "$COPIA/build/bin/$p" ] && chmod -x "$COPIA/build/bin/$p"
done

# Exilla per `netinst`: la copia non ha cross_build, e tools/mknetinst.sh la
# prende da qui.
export EXILLA_DIST="$RADICE/cross_build/exilla-obj/gecko/dist/firefox"

cd "$COPIA"
[ $# -eq 0 ] && set -- iso-exos
make -j"$(nproc)" "$@"

# =============================================================================
# tools/toolchain.sh — DOVE STANNO TOOLCHAIN E COSTRUZIONI DI TERZI
#
# Si include (con «.») dagli script prepara-*.sh e dagli altri che leggono o
# scrivono la toolchain incrociata, i binutils e i make nativi, OpenSSL,
# FreeBASIC, GCC costruiti. Mette in TC la directory che li contiene.
#
# ! LA REGOLA, DAL 27 SETTEMBRE 2026: sorgenti e strumenti restano DENTRO la
# directory del progetto, anche quando git li ignora. Il posto e' cross_build/.
# La cartella del progetto si copia fra le macchine; la home no — e su un PC
# nuovo la toolchain mancava senza che niente lo dicesse fino al primo errore.
#
# ! UNA MACCHINA CHE NON HA ANCORA SPOSTATO LE SUE CARTELLE CONTINUA A
# FUNZIONARE: se cross_build/ non c'e' e nella home ci sono exos-cross o
# exos-native, TC e' la home, come prima. Altrimenti e' cross_build/ (creata
# qui). E' la stessa scelta della funzione `dentro` del Makefile.
#
# TC si puo' dare da fuori: TC=/altro/posto tools/.../prepara-x.sh
# =============================================================================

_d=$(cd "$(dirname "$0")" && pwd)
while [ "$_d" != "/" ] && [ ! -f "$_d/Makefile" ]; do _d=$(dirname "$_d"); done
EXOS_RADICE="$_d"

if [ -z "$TC" ]; then
    if [ -d "$EXOS_RADICE/cross_build" ]; then
        TC="$EXOS_RADICE/cross_build"
    elif [ -d "$HOME/exos-cross" ] || [ -d "$HOME/exos-native" ]; then
        TC="$HOME"
        echo "  (toolchain nella home, $TC: spostala in $EXOS_RADICE/cross_build — vedi SVILUPPO.md)" >&2
    else
        TC="$EXOS_RADICE/cross_build"
        mkdir -p "$TC"
    fi
fi

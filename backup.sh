#!/bin/bash
#
# Copia di sicurezza del progetto in un archivio solo:
#
#     ./backup.sh            crea /backup/localcopy/graziano/ex-os.tar.gz
#     ./backup.sh -guarda    dice cosa entrerebbe e quanto pesa, senza scrivere
#
# L'archivio conserva permessi (anche +x), collegamenti simbolici, date e i
# file nascosti (.git compreso). Si ripristina con:
#
#     tar -xzpf /backup/localcopy/graziano/ex-os.tar.gz -C <dove>
#
# e poi <dove>/cross_build va spostata in ~/cross_build (vedi sotto).
#
# ! SI RIFA' INTERO OGNI VOLTA. Un tar.gz non si puo' aggiornare (tar -u e -r
# non funzionano sui compressi), e gli incrementali di tar sono una catena di
# archivi che servono tutti per ripristinare: l'utente ha scelto la sicurezza
# (20 ottobre 2026).
#
# ! L'ARCHIVIO VECCHIO NON SI TOCCA FINCHE' IL NUOVO NON E' PROVATO. Si scrive
# in un file temporaneo nella stessa cartella, si controlla (gzip -t e
# l'elenco completo con tar -t), e solo allora il vecchio diventa
# ex-os.precedente.tar.gz e il nuovo prende il suo nome. Un backup interrotto
# o rotto lascia le copie di prima come stavano.
#
# ! ISTRUZIONI E SORGENTI, NIENT'ALTRO (25 ottobre 2026, l'utente): l'archivio
# deve bastare a ricostruire tutto su un sistema nuovo che ha solo il gcc di
# base. Quindi DENTRO: il progetto e TUTTI i sorgenti di terzi, anche quelli
# che stanno in cross_build/ (binutils, gmp, mpfr, mpc, openlibm, acpica,
# ath5k, libgit2, minimp3, rustc, con gli archivi originali); gcc/, rust/,
# openssl/, i FreeBASIC e gli altri alberi stanno gia' nel progetto. FUORI,
# elencato in ESCLUSI: tutto cio' che si rifa' compilando — gli oggetti
# (build/, build-64/, gli oggetti di Firefox), le cartelle di compilazione, le
# toolchain INSTALLATE (exos-cross*, gli alberi build-* dei nativi, Rust di
# questa macchina), Node e il clang di Mozilla (scaricati, non sorgenti), le
# ISO e gli alberi di netinst in dist/. Le toolchain funzionano solo su
# questa macchina: copiarle non serve, si rifanno.
#
# ! cross_build E' UN COLLEGAMENTO (a ~/cross_build, fuori da MEGA, dal 21
# ottobre 2026), e tar un collegamento lo salva come collegamento, senza il
# contenuto. Allora si archivia anche la cartella vera, come voce a parte in
# cima all'archivio: cross_build/... accanto a exa_os/. Al ripristino va
# rimessa al suo posto (~/cross_build) e il collegamento la ritrova.
#
# ! I SORGENTI DI TERZI ENTRANO: firefox-main/, mariadb-server/, httpd/,
# apr/, apr-util/, libexpat/ e php-src/ (25, 27 e 28 ottobre 2026, l'utente: «aggiungerlo a backup.sh cosi' che entra
# automaticamente»),
# con la loro .git, che serve a tenerli al passo coi repository ufficiali.
# Non vanno in ESCLUSI; ci vanno solo le loro compilazioni.
#
# ! ~/.cargo NON C'E': sta nella home ed e' un collegamento a
# cross_build/nasi5deb/rust-macchina/cargo. Dopo un ripristino su un altro PC:
#     ln -s <progetto>/cross_build/<macchina>/rust-macchina/cargo ~/.cargo

set -uo pipefail

DEST_DIR="/backup/localcopy/graziano"
NOME="ex-os.tar.gz"

# La cartella del progetto e' quella di questo script; si archivia col suo
# nome (exa_os/...), partendo dalla cartella che la contiene.
PROGETTO="$(cd "$(dirname "$0")" && pwd)"
PADRE="$(dirname "$PROGETTO")"
BASE="$(basename "$PROGETTO")"

# Percorsi relativi alla radice del progetto, oppure nomi validi ovunque
# (quelli senza "/"). Per aggiungerne uno basta una riga.
# ! UNA CARTELLA DELLA RADICE SI SCRIVE CON LA BARRA DAVANTI ("/build"): un
# nome senza barra vale a ogni profondita', e "build" si portava via anche
# firefox-main/build, xpcom/build, nsprpub/build, gcc/libgo/go/go/build e le
# build/ di Rust, che sono SORGENTI (25 ottobre 2026).
ESCLUSI=(
    # oggetti del sistema, rifatti da make
    "/build"
    "/build-64"
    # Firefox: 76 GB di oggetti, rifatti da tools/exilla/gecko-costruisci.sh
    "cross_build/exilla-obj"
    # cartelle di compilazione delle toolchain (quelle installate restano)
    "cross_build/gcc-build"
    "cross_build/gcc-build-64"
    "cross_build/gcc-build-canadian"
    "cross_build/gcc-build-cpp"
    "cross_build/gcc-build-cxx"
    "cross_build/gcc-build-rel"
    "cross_build/binutils-build-64"
    # cartelle di compilazione e di prova di ogni macchina; costruzione-strumenti
    # (Node, clang di Mozilla) no: si riscaricano, non si rifanno
    "cross_build/*/costruzione-gcc*"
    "cross_build/*/costruzione-binutils"
    "cross_build/*/costruzione-rust"
    "cross_build/*/costruzione-std"
    "cross_build/*/costruzione-crate"
    "cross_build/*/costruzione-cbindgen"
    "cross_build/*/costruzione-mozbuild"
    "cross_build/*/costruzione-prova*"
    "cross_build/*/costruzione-sistema"
    # Node e il clang di Mozilla: scaricati, non sorgenti
    "cross_build/*/costruzione-strumenti"
    # le toolchain installate e gli alberi di compilazione dei nativi
    "cross_build/exos-cross"
    "cross_build/exos-cross64"
    "cross_build/*/exos-cross"
    "cross_build/*/exos-cross64"
    "cross_build/*/rust-macchina"
    "cross_build/exos-native/build-*"
    "cross_build/exos-native64"
    "cross_build/openlibm-build-64"
    "cross_build/openssl-build-exos"
    "cross_build/openssl-build-exos64"
    "cross_build/fb64"
    "cross_build/fb-build-*"
    # immagini e copie vecchie, non sorgenti: un disco di prova, la copia della
    # chiavetta del 10 ottobre, gli elenchi del recupero 0.263
    "cross_build/exos-native/backup"
    "cross_build/chiave-10ott"
    "cross_build/recupero-0.263"
    # MariaDB: le compilazioni a 32 e 64 bit (i sorgenti, mariadb-server/, restano)
    "cross_build/*/costruzione-mariadb*"
    # le compilazioni delle librerie di terzi (mariadb-*, ncurses...) e cio'
    # che ne esce: gli archivi in cross_build/terze-parti restano, e
    # tools/<libreria>-exos/costruisci.sh rifa' tutto in un minuto
    "cross_build/costruzione-*"
    "cross_build/ncurses-exos32"
    "cross_build/ncurses-exos64"
    "cross_build/zlib-exos32"
    "cross_build/zlib-exos64"
    "cross_build/bzip2-exos32"
    "cross_build/bzip2-exos64"
    "cross_build/xz-exos32"
    "cross_build/xz-exos64"
    "cross_build/lz4-exos32"
    "cross_build/lz4-exos64"
    "cross_build/zstd-exos32"
    "cross_build/zstd-exos64"
    "cross_build/pcre2-exos32"
    "cross_build/pcre2-exos64"
    "cross_build/fmt-exos32"
    "cross_build/fmt-exos64"
    "cross_build/openssl-exos32"
    "cross_build/openssl-exos64"
    # i dischi delle prove (hd.img, usb-*.img...): li rifanno gli script
    "/dischi"
    # in dist/: le ISO e gli alberi di netinst, rifatti da make
    "dist/*.iso"
    "dist/netinst"
    "dist/netinst_64"
    # rifiuti di Python e degli editor
    "__pycache__"
    "*.swp"
    "*.swo"
)

GUARDA=0
case "${1:-}" in
    "")      ;;
    -guarda) GUARDA=1 ;;
    -h|--help|-aiuto) sed -n '3,9p' "$0" | sed 's/^# \?//'; exit 0 ;;
    *)       echo "uso: $0 [-guarda]" >&2; exit 2 ;;
esac

# Le esclusioni per tar: i percorsi si ancorano a BASE/, i nomi valgono ovunque.
# Quelli di cross_build valgono anche per la voce a parte (vedi sotto).
OPZ=(--anchored)
for e in "${ESCLUSI[@]}"; do
    case "$e" in
        /*) OPZ+=(--exclude="$BASE$e") ;;
        cross_build/*) OPZ+=(--exclude="$BASE/$e" --exclude="$e") ;;
        */*) OPZ+=(--exclude="$BASE/$e") ;;
        *)   OPZ+=(--no-anchored --exclude="$e" --anchored) ;;
    esac
done

# La cartella vera di cross_build, se nel progetto c'e' solo il collegamento.
# Si archivia col nome cross_build, dalla cartella che la contiene.
VOCI=(-C "$PADRE" "$BASE")
if [ -L "$PROGETTO/cross_build" ]; then
    CROSS="$(readlink -f "$PROGETTO/cross_build")"
    if [ -d "$CROSS" ] && [ "$(basename "$CROSS")" = "cross_build" ]; then
        VOCI+=(-C "$(dirname "$CROSS")" "cross_build")
    else
        echo "backup: cross_build punta a '$CROSS', che non e' una cartella cross_build: i suoi sorgenti NON entrano" >&2
    fi
fi

if [ $GUARDA -eq 1 ]; then
    echo "progetto : $PROGETTO"
    echo "archivio : $DEST_DIR/$NOME"
    echo "esclusi  :"; printf '    %s\n' "${ESCLUSI[@]}"
    echo "misuro quanto entra (prima della compressione)..."
    echo "voci    : ${VOCI[*]}"
    tar -cf - "${OPZ[@]}" "${VOCI[@]}" 2>/dev/null | wc -c |
        awk '{ printf "  circa %.1f GB da comprimere\n", $1 / 1024 / 1024 / 1024 }'
    exit 0
fi

mkdir -p "$DEST_DIR" || { echo "backup: non posso creare $DEST_DIR" >&2; exit 1; }
TEMP="$DEST_DIR/.$NOME.in-corso.$$"
trap 'rm -f "$TEMP"' EXIT INT TERM

# pigz comprime su tutti i core; senza, gzip. Il formato e' lo stesso.
COMPRIMI="gzip"; command -v pigz > /dev/null && COMPRIMI="pigz"

echo "backup di $PROGETTO"
echo "      in $DEST_DIR/$NOME  (compressione: $COMPRIMI)"
INIZIO=$(date +%s)

# ! L'USCITA 1 DI tar E' UN AVVISO, NON UN ERRORE: «file changed as we read
# it» (MEGA che sincronizza, un editor che salva). L'archivio e' buono, quel
# file e' nella versione di quell'istante. Da 2 in su e' un errore vero.
tar -c -p "${OPZ[@]}" "${VOCI[@]}" | "$COMPRIMI" -6 > "$TEMP"
STATI=("${PIPESTATUS[@]}")
if [ "${STATI[0]}" -gt 1 ] || [ "${STATI[1]}" -ne 0 ]; then
    echo "backup: FALLITO (tar=${STATI[0]}, $COMPRIMI=${STATI[1]}). L'archivio di prima e' intatto." >&2
    exit 1
fi
[ "${STATI[0]}" -eq 1 ] && echo "  avviso: qualche file e' cambiato durante la lettura (MEGA?)."

echo "controllo l'archivio..."
if ! "$COMPRIMI" -t "$TEMP" 2> /dev/null; then
    echo "backup: l'archivio nuovo e' ROTTO (gzip -t). L'archivio di prima e' intatto." >&2
    exit 1
fi
N=$(tar -tzf "$TEMP" 2> /dev/null | wc -l)
if [ "${PIPESTATUS[0]}" -ne 0 ] || [ "$N" -eq 0 ]; then
    echo "backup: l'archivio nuovo non si legge (tar -t). L'archivio di prima e' intatto." >&2
    exit 1
fi

# Tutto a posto: il vecchio diventa il precedente, il nuovo prende il nome.
[ -f "$DEST_DIR/$NOME" ] && mv -f "$DEST_DIR/$NOME" "$DEST_DIR/${NOME%.tar.gz}.precedente.tar.gz"
mv -f "$TEMP" "$DEST_DIR/$NOME"
trap - EXIT INT TERM

DURATA=$(( $(date +%s) - INIZIO ))
echo "fatto: $N voci, $(du -h "$DEST_DIR/$NOME" | cut -f1), in $((DURATA / 60)) min $((DURATA % 60)) s"
echo "       $DEST_DIR/$NOME"
[ -f "$DEST_DIR/${NOME%.tar.gz}.precedente.tar.gz" ] &&
    echo "       (la copia di prima: ${NOME%.tar.gz}.precedente.tar.gz)"
exit 0

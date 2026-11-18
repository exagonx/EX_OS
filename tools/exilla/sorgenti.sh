#!/bin/bash
# =============================================================================
# tools/exilla/sorgenti.sh - the Mozilla sources: the latest official release,
# signature checked, and our patches on it (18 November 2026)
#
#     tools/exilla/sorgenti.sh [firefox|thunderbird]          ours and theirs
#     tools/exilla/sorgenti.sh -scarica [PRODOTTO] [VER|release|esr|beta]
#     tools/exilla/sorgenti.sh -prova   [PRODOTTO] [VER]      patches on a copy
#     tools/exilla/sorgenti.sh -applica [PRODOTTO] [VER]      the copy becomes the tree
#
# The same four steps as tools/aggiorna-sorgenti.sh, for the trees too big
# for it: Firefox (firefox-main/, the patches in tools/exilla/patch) and
# Thunderbird (thunderbird-main/, voce 103: Exilla's patches, then those of
# tools/thunderbird-exos/patch). PRODOTTO defaults to firefox, the version
# to the latest release (product-details.mozilla.org).
#
#   -scarica  archive.mozilla.org/pub/PRODOTTO/releases/VER/source/: the
#             .source.tar.xz into cross_build/terze-parti/PRODOTTO/VER/, its
#             .asc checked with Mozilla's KEY and the sha512 with SHA512SUMS.
#             ! THE KEY IS PINNED in tools/exilla/mozilla-chiave.txt (its
#             fingerprint): a different key stops the download, trusting it
#             is a person's decision.
#   -prova    the archive in cross_build/aggiorna/PRODOTTO-VER, the originals
#             of every file the patches touch saved aside (for
#             tools/exilla/patch-crea.sh), then the patches in order and for
#             real: "ok", "spost" (went in with offset/fuzz: reread it), "NO"
#             (a .rej). The NO ones are the adaptation: fix the copy by hand
#             until no .rej is left. A .cargo-checksum.json is not patched
#             but recomputed for the crates we touch (its sums are of a version).
#   -applica  refuses while the copy has a .rej. The old tree and its
#             originals go to cross_build/aggiorna/vecchi/ (out of MEGA, not
#             deleted), the copy takes their place, tools/exilla/base.txt says
#             the new base. Then: patch-crea.sh, a clean build (mach clobber)
#             at 32 and 64 bit, the Exilla tests in QEMU.
#
# ! A PATCH THAT APPLIES IS NOT A PORT THAT WORKS (as in aggiorna-sorgenti.sh):
# the proof is the build and the tests.
# =============================================================================
set -u
cd "$(dirname "$0")/../.." || exit 1
R=$PWD
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
TP=$R/cross_build/terze-parti
LAVORO=$R/cross_build/aggiorna
CHIAVE=$R/tools/exilla/mozilla-chiave.txt
ARCHIVIO_M=https://archive.mozilla.org/pub
DETTAGLI=https://product-details.mozilla.org/1.0

uso() { sed -n 2,36p "$0" | cut -c3-; exit 2; }

prodotto() {    # $1: firefox|thunderbird -> P ALBERO ORIG PATCHES
    P=${1:-firefox}
    case "$P" in
        firefox)     ALBERO=$R/firefox-main;     VERFILE=browser/config/version_display.txt
                     PATCHES="$R/tools/exilla/patch" ;;
        thunderbird) ALBERO=$R/thunderbird-main; VERFILE=comm/mail/config/version_display.txt
                     PATCHES="$R/tools/exilla/patch $R/tools/thunderbird-exos/patch" ;;
        *) uso ;;
    esac
    ORIG=$R/cross_build/$MACCHINA/$P-originali
}

# the official version of a channel: release (default), esr, beta
ufficiale() {   # $1 = channel
    local j k
    j=$(curl -fsS --max-time 30 "$DETTAGLI/${P}_versions.json") || return 1
    case "$P:${1:-release}" in
        firefox:release)     k=LATEST_FIREFOX_VERSION ;;
        firefox:esr)         k=FIREFOX_ESR ;;
        firefox:beta)        k=LATEST_FIREFOX_DEVEL_VERSION ;;
        thunderbird:release) k=LATEST_THUNDERBIRD_VERSION ;;
        thunderbird:esr)     k=THUNDERBIRD_ESR ;;
        thunderbird:beta)    k=LATEST_THUNDERBIRD_DEVEL_VERSION ;;
        *) return 1 ;;
    esac
    echo "$j" | sed -n "s/.*\"$k\": *\"\([^\"]*\)\".*/\1/p"
}

nostra() {
    [ -f "$ALBERO/$VERFILE" ] && cat "$ALBERO/$VERFILE" || echo "-"
}

elenco() {
    local c
    echo "$P: albero $(basename "$ALBERO") $(nostra)"
    [ -f "$R/tools/exilla/base.txt" ] && echo "  base delle patch: $(head -1 "$R/tools/exilla/base.txt")"
    for c in release esr beta; do printf '  %-8s %s\n' "$c" "$(ufficiale $c)"; done
    ls -d "$TP/$P"/*/ 2>/dev/null | while read -r d; do
        [ -f "$d/.verificata" ] && echo "  scaricata e verificata: $(basename "$d")"
    done
}

versione() {    # $1: VER or channel -> V
    case "${1:-release}" in
        release|esr|beta) V=$(ufficiale "${1:-release}") ;;
        *)                V=$1 ;;
    esac
    [ -n "$V" ] || { echo "! nessuna versione per $P ${1:-release}"; exit 1; }
}

# ! INTO A FOLDER OF ITS OWN, moved into place only when both checks pass
scarica() {
    local b t file g fpr voluta
    b=$ARCHIVIO_M/$P/releases/$V
    file=$P-$V.source.tar.xz
    mkdir -p "$TP/$P" "$LAVORO"
    [ -f "$TP/$P/$V/.verificata" ] && { echo "$P $V: c'e' gia' ($TP/$P/$V)"; return 0; }
    t=$(mktemp -d "$LAVORO/scarica-$P.XXXXXX")
    echo "$P $V: $b/source/$file"
    for f in KEY SHA512SUMS source/$file.asc; do
        curl -fsSL --max-time 120 -o "$t/$(basename "$f")" "$b/$f" ||
            { echo "! $b/$f non si scarica"; rm -rf "$t"; exit 1; }
    done
    g=$(mktemp -d); chmod 700 "$g"
    GNUPGHOME=$g gpg -q --import "$t/KEY" > /dev/null 2>&1
    fpr=$(GNUPGHOME=$g gpg --with-colons --fingerprint 2>/dev/null | awk -F: '$1=="fpr"{print $10; exit}')
    voluta=$(grep -v '^#' "$CHIAVE" 2>/dev/null | tr -d ' ' | head -1)
    if [ -z "$voluta" ] || [ "$fpr" != "$voluta" ]; then
        echo "! la chiave di Mozilla e' $fpr"
        [ -n "$voluta" ] && echo "  e in $CHIAVE c'e' $voluta: diversa, non vado avanti" ||
            echo "  e $CHIAVE non c'e': fidarsi e' una decisione di una persona"
        echo "  i file restano in $t"; rm -rf "$g"; exit 1
    fi
    echo "  chiave $fpr (quella di $(basename "$CHIAVE"))"
    curl -fL --max-time 3600 -o "$t/$file" "$b/source/$file" || { echo "! scaricamento fallito"; rm -rf "$g"; exit 1; }
    if ! GNUPGHOME=$g gpg --status-fd 1 --verify "$t/$file.asc" "$t/$file" 2>/dev/null | grep -q '^\[GNUPG:\] GOODSIG'; then
        echo "! firma NON buona: i file restano in $t"; rm -rf "$g"; exit 1
    fi
    rm -rf "$g"
    echo "  firma buona"
    [ "$(sha512sum "$t/$file" | cut -d' ' -f1)" = "$(awk -v f="source/$file" '$2==f{print $1}' "$t/SHA512SUMS")" ] ||
        { echo "! sha512 diverso da SHA512SUMS: i file restano in $t"; exit 1; }
    echo "  sha512 come in SHA512SUMS"
    mkdir -p "$TP/$P/$V"
    mv -f "$t"/* "$TP/$P/$V/"; rmdir "$t"
    echo "$V $file" > "$TP/$P/$V/.verificata"
}

# the path of a patch's file: its "+++ b/..." line
percorso() { sed -n 's#^+++ b/\([^\t]*\).*#\1#p' "$1" | head -1; }

prova() {
    local d c o f r bene=0 sposta=0 male=0 log
    d=$TP/$P/$V
    [ -f "$d/.verificata" ] || { echo "! prima: $0 -scarica $P $V"; exit 1; }
    c=$LAVORO/$P-$V
    [ -e "$c" ] && { echo "! $c c'e' gia' (forse l'adattamento in corso): toglilo a mano per rifare"; exit 1; }
    mkdir -p "$c"
    echo "$P $V: estraggo in $c (qualche minuto)"
    tar -xf "$d/$P-$V.source.tar.xz" -C "$c" || exit 1
    o=$c/originali; mkdir -p "$o"
    c=$(ls -d "$c"/*/ | grep -v '/originali/$' | head -1); c=${c%/}
    for f in $(for p in $PATCHES; do ls "$p"/*.patch 2>/dev/null; done); do
        r=$(percorso "$f"); [ -n "$r" ] || continue
        [ -e "$o/$r" ] || [ -e "$o/$r.nuovo" ] && continue
        mkdir -p "$(dirname "$o/$r")"
        if [ -f "$c/$r" ]; then cp -p "$c/$r" "$o/$r"; else : > "$o/$r.nuovo"; fi
    done
    log=$LAVORO/$P-$V.log; : > "$log"; somme=""
    # ! IN ORDER AND FOR REAL: a later patch may need an earlier one
    for f in $(for p in $PATCHES; do ls "$p"/*.patch 2>/dev/null; done); do
        # ! A .cargo-checksum.json IS NOT PATCHED: its sha256 are of the
        # crate of THAT version. It is recomputed below, after the patches.
        case "$(percorso "$f")" in
            */.cargo-checksum.json) somme="$somme $(dirname "$(percorso "$f")")"; continue ;;
        esac
        if ! patch -p1 -N --no-backup-if-mismatch -d "$c" < "$f" > "$LAVORO/ultimo.log" 2>&1; then
            echo "  NO    $(basename "$f")"; male=$((male + 1))
        elif grep -q 'offset\|fuzz' "$LAVORO/ultimo.log"; then
            echo "  spost $(basename "$f")"; sposta=$((sposta + 1))
        else
            bene=$((bene + 1))
        fi
        { echo "== $(basename "$f")"; cat "$LAVORO/ultimo.log"; } >> "$log"
    done
    for r in $somme; do
        python3 -I - "$c/$r" <<'PY' && echo "  somme $r/.cargo-checksum.json (ricalcolate)" || male=$((male + 1))
import hashlib, json, os, sys
d = sys.argv[1]; f = os.path.join(d, ".cargo-checksum.json")
j = json.load(open(f))
for k in j["files"]:
    j["files"][k] = hashlib.sha256(open(os.path.join(d, k), "rb").read()).hexdigest()
open(f, "w").write(json.dumps(j, separators=(",", ":")))
PY
    done
    echo "$P $V: $bene patch esatte, $sposta spostate, $male rifiutate (dettagli in $log)"
    [ $male -eq 0 ] || echo "  l'adattamento: i .rej in $c (find $c -name '*.rej'), poi $0 -applica $P $V"
}

applica() {
    local c o rej vecchia v=$LAVORO/vecchi
    c=$(ls -d "$LAVORO/$P-$V"/*/ 2>/dev/null | grep -v '/originali/$' | head -1); c=${c%/}
    o=$LAVORO/$P-$V/originali
    [ -n "$c" ] && [ -d "$o" ] || { echo "! prima: $0 -prova $P $V"; exit 1; }
    rej=$(find "$c" -name '*.rej' | wc -l)
    [ "$rej" -eq 0 ] || { echo "! $c ha ancora $rej .rej: l'adattamento non e' finito"; exit 1; }
    vecchia=$(nostra)
    mkdir -p "$v"
    if [ -d "$ALBERO" ]; then
        [ -e "$v/$(basename "$ALBERO")-$vecchia" ] && { echo "! $v/$(basename "$ALBERO")-$vecchia c'e' gia'"; exit 1; }
        mv "$ALBERO" "$v/$(basename "$ALBERO")-$vecchia" || exit 1
        echo "  $(basename "$ALBERO") $vecchia -> $v/$(basename "$ALBERO")-$vecchia"
    fi
    [ -d "$ORIG" ] && mv "$ORIG" "$v/$(basename "$ORIG")-$vecchia"
    mv "$c" "$ALBERO" || exit 1
    mkdir -p "$(dirname "$ORIG")"; mv "$o" "$ORIG"
    rmdir "$LAVORO/$P-$V" 2>/dev/null
    [ "$P" = firefox ] && printf '%s (firefox-%s.source.tar.xz, %s)\n' "$V" "$V" "$(date +%Y-%m-%d)" > "$R/tools/exilla/base.txt"
    echo "$P: $(basename "$ALBERO") ora e' $V"
    echo "resta da fare: tools/exilla/patch-crea.sh ; mach clobber e costruzione a 32 e 64 bit ;"
    echo "le prove di Exilla in QEMU ; i manuali con la versione nuova ; poi netupdate"
}

case "${1:-}" in
    -scarica) prodotto "${2:-}"; versione "${3:-}"; scarica ;;
    -prova)   prodotto "${2:-}"; versione "${3:-}"; prova ;;
    -applica) prodotto "${2:-}"; versione "${3:-}"; applica ;;
    -h|-help|--help) uso ;;
    ""|firefox|thunderbird) prodotto "${1:-}"; elenco ;;
    *) uso ;;
esac

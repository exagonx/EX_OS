#!/bin/bash
# =============================================================================
# tools/aggiorna-sorgenti.sh — third-party sources against their official ones
#
#     tools/aggiorna-sorgenti.sh                 which libraries are behind
#     tools/aggiorna-sorgenti.sh -scarica NOME   the new archive, signature checked
#     tools/aggiorna-sorgenti.sh -prova NOME     the EX-OS patches on a copy of it
#     tools/aggiorna-sorgenti.sh -applica NOME   VER= in tools/NOME-exos/costruisci.sh
#
# The list is tools/sorgenti-terzi.txt. Each step stops at what it can prove:
# -scarica writes only in cross_build/terze-parti/NOME and removes nothing,
# -prova works in cross_build/aggiorna, -applica refuses without both. After
# -applica the library is NOT updated: it wants costruisci.sh 32 and 64, its
# dependents rebuilt and its prove in QEMU, as the script prints.
#
# ! A PATCH THAT APPLIES IS NOT A PORT THAT WORKS. costruisci.sh also edits the
# tree with sed, and a sed that no longer finds its line says nothing. The
# proof is the build and the tests, not this script.
#
# ! A NEW SIGNING KEY STOPS THE DOWNLOAD. The keys are the ones already next to
# the archives: trusting another one is a decision for a person.
# =============================================================================
set -u
cd "$(dirname "$0")/.." || exit 1
LISTA=tools/sorgenti-terzi.txt
TP=cross_build/terze-parti
LAVORO=cross_build/aggiorna

riga_di() { grep -v '^#' "$LISTA" | grep "^$1|"; }

campi() {   # NOME: NOME GIT REGEX SED ARCH
    local r
    r=$(riga_di "$1") || { echo "! $1 non e' in $LISTA" >&2; exit 2; }
    IFS='|' read -r NOME GIT REGEX SEDV ARCH <<< "$r"
}

ver_nostra() {
    sed -n 's/^VER=//p' "tools/$1-exos/costruisci.sh" | head -1 | tr -d "\"'"
}

# ! sort -V, not the order of the server: 2.4.9 comes before 2.4.69
ver_ufficiale() {
    git ls-remote --tags --refs "$GIT" 2>/dev/null | sed 's#.*refs/tags/##' |
        grep -E "$REGEX" | sed -E "$SEDV" | sort -V | tail -1
}

url_di() {
    local u=${2//./_}
    echo "$ARCH" | sed "s/@V@/$2/g; s/@U@/$u/g"
}

elenco() {
    local n nostra loro stato
    printf '%-24s %-12s %-12s\n' libreria nostra ufficiale
    grep -v '^#' "$LISTA" | cut -d'|' -f1 | while read -r n; do
        campi "$n"
        nostra=$(ver_nostra "$n")
        loro=$(timeout 60 bash -c "$(declare -f ver_ufficiale); GIT='$GIT' REGEX='$REGEX' SEDV='$SEDV' ver_ufficiale")
        if [ -z "$loro" ]; then stato="! non so"
        elif [ "$loro" = "$nostra" ]; then stato="a posto"
        elif [ "$(printf '%s\n%s\n' "$nostra" "$loro" | sort -V | tail -1)" = "$nostra" ]; then stato="piu' nuova la nostra"
        else stato="DA AGGIORNARE"; fi
        printf '%-24s %-12s %-12s %s\n' "$n" "$nostra" "${loro:--}" "$stato"
    done
}

# The keys already in the library's folder, in a keyring used only here
verifica_firma() {
    local dir=$1 file=$2 g f firma= sha
    for f in "$file.asc" "$file.sig"; do [ -s "$dir/$f" ] && firma=$dir/$f; done
    [ -n "$firma" ] || { echo "! nessuna firma per $file"; return 1; }
    g=$(mktemp -d)
    chmod 700 "$g"
    for f in "$dir"/KEYS "$dir"/*.gpg "$dir"/pubkeys.asc "$dir"/*keys*.asc "$dir"/*key*.asc; do
        [ -f "$f" ] && GNUPGHOME=$g gpg -q --import "$f" > /dev/null 2>&1
    done
    if GNUPGHOME=$g gpg --status-fd 1 --verify "$firma" "$dir/$file" 2>/dev/null | grep -q '^\[GNUPG:\] GOODSIG'; then
        echo "firma buona: $(basename "$firma")"
        rm -rf "$g"
        return 0
    fi
    rm -rf "$g"
    echo "! firma NON verificata con le chiavi di $dir"
    return 1
}

# ! INTO A FOLDER OF ITS OWN, moved next to the others only when the signature
# is good: a failed download must not touch the archive already there.
scarica() {
    local v url file dir t e f
    campi "$1"
    [ "$ARCH" != - ] || { echo "! $1: l'indirizzo cambia a ogni versione, va scaricato a mano"; exit 1; }
    v=$(ver_ufficiale)
    [ -n "$v" ] || { echo "! nessuna versione per $1"; exit 1; }
    dir=$TP/$1
    mkdir -p "$dir" "$LAVORO"
    t=$(mktemp -d "$LAVORO/scarica.XXXXXX")
    url=$(url_di "$1" "$v")
    file=$(basename "$url")
    echo "$1 $v: $url"
    curl -fsSL -o "$t/$file" "$url" || { rm -rf "$t"; echo "! scaricamento fallito"; exit 1; }
    for e in asc sig sha256 sha512; do
        curl -fsSL -o "$t/$file.$e" "$url.$e" 2>/dev/null || rm -f "$t/$file.$e"
    done
    for f in "$dir"/KEYS "$dir"/*.gpg "$dir"/*.asc; do
        case "$f" in *"$file".asc) ;; *) [ -f "$f" ] && cp "$f" "$t/" ;; esac
    done
    if ! verifica_firma "$t" "$file"; then
        echo "  i file restano in $t"
        rm -f "$dir/.verificata"
        exit 1
    fi
    for f in "$t/$file"*; do mv -f "$f" "$dir/"; done
    rm -rf "$t"
    echo "$v $file" > "$dir/.verificata"
}

prova() {
    local v file dir c f bene=0 male=0
    campi "$1"
    dir=$TP/$1
    [ -f "$dir/.verificata" ] || { echo "! prima: $0 -scarica $1 (con la firma buona)"; exit 1; }
    read -r v file < "$dir/.verificata"
    c=$LAVORO/$1-$v
    rm -rf "$c"
    mkdir -p "$c"
    case "$file" in
        *.zip) unzip -q "$dir/$file" -d "$c" ;;
        *)     tar -xf "$dir/$file" -C "$c" ;;
    esac || exit 1
    c=$(ls -d "$c"/*/ | head -1)
    rm -f "$dir/.provata"
    # ! IN ORDER AND FOR REAL, as costruisci.sh does: a later patch may need
    # an earlier one, which a --dry-run of each would call a failure
    for f in tools/"$1"-exos/patch/*.diff; do
        [ -f "$f" ] || continue
        if ! patch -p1 -N -d "$c" < "$f" > "$LAVORO/ultimo.log" 2>&1; then
            echo "  NO    $(basename "$f") (i .rej in $c)"; male=$((male + 1))
        elif grep -q 'offset\|fuzz' "$LAVORO/ultimo.log"; then
            echo "  spost $(basename "$f") (entra, ma le righe si sono mosse: rileggerla)"; bene=$((bene + 1))
        else
            echo "  ok    $(basename "$f")"; bene=$((bene + 1))
        fi
    done
    echo "$1 $v: $bene patch entrate, $male no"
    if [ $male -eq 0 ]; then echo "$v" > "$dir/.provata"; else return 1; fi
}

applica() {
    local v f=tools/$1-exos/costruisci.sh dir=$TP/$1
    campi "$1"
    [ -f "$dir/.provata" ] || { echo "! prima: $0 -prova $1, senza patch rifiutate"; exit 1; }
    v=$(cat "$dir/.provata")
    sed -i "s/^VER=.*/VER=$v/" "$f"
    echo "$f: VER=$v"
    echo "resta da fare: $f 32 ; $f 64 ; chi dipende da $1 ; le prove in QEMU ;"
    echo "i commenti con la versione vecchia ; exagonx/repo-update.sh -strumenti"
}

case "${1:-}" in
    "")        elenco ;;
    -scarica)  scarica "${2:?NOME}" ;;
    -prova)    prova "${2:?NOME}" ;;
    -applica)  applica "${2:?NOME}" ;;
    *)         sed -n 2,12p "$0" | cut -c3-; exit 2 ;;
esac

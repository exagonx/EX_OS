#!/bin/bash
# =============================================================================
# tools/ripara-cloni.sh — puts back in the git clones at the project root what
# a copy through MEGA takes away
#
#     tools/ripara-cloni.sh               every clone of tools/sorgenti-terzi.txt
#     tools/ripara-cloni.sh NOME...       only those (name in the list, or folder)
#     tools/ripara-cloni.sh -n [NOME...]  only says what it would do (exit 1 if
#                                         anything; no network)
#
# The clones (mesa/, mariadb-server/, httpd/, php-src/...) stay in MEGA with
# the project (the user, 22/11). But MEGA syncs files, not git: symbolic links
# never arrive (mesa has 10, the duckdb of mariadb-server 2), the modes come
# out 600 or 777, and with the old .megaignore (-:.*) .git stayed home. Then
# `git diff > patch` carries deleted links and mode changes, and
# aggiorna-sorgenti.sh -clone refuses the clone as "modifiche non salvate".
#
# Per clone, and per submodule that is checked out:
#   - folder missing: cloned again, at the commit written in tools/cloni.txt;
#   - .git missing: made again from that commit WITHOUT touching the files
#     (git reset, not checkout): the EX-OS work shows up as changes;
#   - core.fileMode false, like the project: modes are MEGA's business;
#   - +x on the files git says are executable;
#   - the symbolic links that are missing, or arrived as a copy of the target.
# ! IT NEVER THROWS AWAY WORK: a file sitting where a link should be, and
# that is not what the link points to, is only reported.
#
# tools/cloni.txt is in git, so it travels with MEGA: folder|url|branch|commit,
# rewritten at each run for the clones that have .git. A submodule is
# "folder/path" with no branch.
# =============================================================================
set -u
cd "$(dirname "$0")/.." || exit 1
R=$(pwd -P)
LISTA=tools/sorgenti-terzi.txt
REG=tools/cloni.txt
SECCO=no
[ "${1:-}" = -n ] && { SECCO=si; shift; }
GUAI=0
DAFARE=0
NUOVO=$(mktemp)
trap 'rm -f "$NUOVO"' EXIT

fai() { if [ $SECCO = si ]; then echo "    (farei) $*"; DAFARE=$((DAFARE + 1)); else "$@"; fi; }

reg_di() { [ -f "$REG" ] && grep -v '^#' "$REG" | awk -F'|' -v d="$1" '$1 == d' | head -1; }

# true only if $1 is the top of its OWN repository: without mesa/.git,
# git -C mesa happily answers for the EX-OS repository around it
git_suo() {
    [ -e "$1/.git" ] || return 1
    [ "$(git -C "$1" rev-parse --show-toplevel 2>/dev/null)" = "$(cd "$1" && pwd -P)" ] &&
        git -C "$1" rev-parse -q --verify HEAD > /dev/null
}

# $1 folder $2 url $3 branch $4 commit: a .git for files that are already there
rifai_git() {
    local d=$1 url=$2 ramo=$3 c=$4
    echo "  $d: manca .git, lo rifaccio da $url (${c:-${ramo:-HEAD}}), i file restano"
    [ $SECCO = si ] && { DAFARE=$((DAFARE + 1)); return 0; }
    # a submodule's .git is a file pointing into the parent's .git, gone with
    # it; a .git without a commit is a repair that stopped halfway: aside
    if [ -e "$d/.git" ]; then
        mv "$d/.git" "$d/.git.rotto.$$" && echo "  $d: il .git che non funzionava e' in $d/.git.rotto.$$"
    fi
    git init -q "$d" && git -C "$d" remote add origin "$url" || return 1
    if [ -n "$c" ] && git -C "$d" fetch -q --filter=blob:none origin "$c" 2>/dev/null; then :
    elif git -C "$d" fetch -q --filter=blob:none origin "${ramo:-HEAD}"; then c=FETCH_HEAD
    else echo "! $d: non riesco a prendere ${c:-${ramo:-HEAD}} da $url"; return 1; fi
    c=$(git -C "$d" rev-parse "$c^{commit}") || return 1
    # ! THE BLOBS IN ONE GO, BEFORE THE RESET: with blob:none the reset looks
    # at every file's original (.gitattributes, end of line) and fetches them
    # ONE BY ONE - libexpat: 42 round trips, 10 minutes, then GitHub dropped
    # the line ("Error in the HTTP2 framing layer"). One batch: 14 seconds.
    git -C "$d" ls-tree -r --format='%(objecttype) %(objectname)' "$c" | awk '$1 == "blob" {print $2}' |
        git -C "$d" -c fetch.negotiationAlgorithm=noop fetch -q --no-tags --filter=blob:none --stdin origin || return 1
    git -C "$d" reset -q "$c" || return 1
    [ -n "$ramo" ] && git -C "$d" checkout -q -B "$ramo"
    # the tags: aggiorna-sorgenti -clone and php's git archive look for them
    git -C "$d" fetch -q --filter=blob:none --tags origin || true
}

# the folder is not there at all: a fresh clone at the same point
riclona() {
    local d=$1 url=$2 ramo=$3 c=$4
    echo "  $d: non c'e', lo clono da $url (${c:-${ramo:-HEAD}})"
    [ $SECCO = si ] && { DAFARE=$((DAFARE + 1)); return 0; }
    git clone -q --filter=blob:none --no-checkout "$url" "$d" || return 1
    if [ -n "$c" ] && ! git -C "$d" cat-file -e "$c^{commit}" 2>/dev/null; then
        git -C "$d" fetch -q --filter=blob:none origin "$c" 2>/dev/null || c=
    fi
    if [ -n "$ramo" ]; then git -C "$d" checkout -q -B "$ramo" ${c:+"$c"}
    else git -C "$d" checkout -q ${c:-HEAD}; fi
}

# modes and links of a tree that has its own .git
ripara_albero() {
    local d=$1 modo sha p meta x=0 l=0 t
    [ "$(git -C "$d" config core.fileMode)" = false ] || fai git -C "$d" config core.fileMode false
    while IFS=$'\t' read -r meta p; do
        read -r modo sha _ <<< "$meta"
        case $modo in
        100755)
            if [ -f "$d/$p" ] && [ ! -L "$d/$p" ] && [ ! -x "$d/$p" ]; then
                fai chmod +x "$d/$p"; x=$((x + 1))
            fi ;;
        120000)
            [ -L "$d/$p" ] && continue
            if [ ! -e "$d/$p" ]; then
                fai git -C "$d" checkout -q -- "$p"; l=$((l + 1)); continue
            fi
            # MEGA on the other PC may write the target's content in its place
            t=$(git -C "$d" cat-file -p "$sha")
            if [ "$(cat "$d/$p" 2>/dev/null)" = "$t" ] ||
               { [ -f "$(dirname "$d/$p")/$t" ] && cmp -s "$d/$p" "$(dirname "$d/$p")/$t"; }; then
                fai rm -f "$d/$p"; fai git -C "$d" checkout -q -- "$p"; l=$((l + 1))
            else
                echo "! $d/$p: dovrebbe essere un collegamento a $t, c'e' un file diverso: non lo tocco"
                GUAI=$((GUAI + 1))
            fi ;;
        esac
    done < <(git -C "$d" -c core.quotePath=false ls-files -s | awk '$1 == "100755" || $1 == "120000"')
    [ $x -gt 0 ] && echo "  $d: +x a $x file"
    [ $l -gt 0 ] && echo "  $d: $l collegamenti rimessi"
    return 0
}

# $1 folder $2 url $3 branch [$4 commit when tools/cloni.txt has none]: the
# whole job for one clone, then its submodules
ripara() {
    local d=$1 url=$2 ramo=$3 c r p sha nome surl meta
    r=$(reg_di "$d")
    c=$(echo "$r" | cut -d'|' -f4)
    [ -z "$c" ] && c=${4:-}
    [ -z "$ramo" ] && ramo=$(echo "$r" | cut -d'|' -f3)
    [ -z "$url" ] && url=$(echo "$r" | cut -d'|' -f2)
    if [ ! -e "$d" ]; then
        [ -n "$url" ] || { echo "! $d: manca e non so da dove clonarlo"; GUAI=$((GUAI + 1)); return; }
        riclona "$d" "$url" "$ramo" "$c" || { GUAI=$((GUAI + 1)); return; }
    elif ! git_suo "$d"; then
        [ -n "$url" ] || { echo "! $d: senza .git e non so da dove rifarlo"; GUAI=$((GUAI + 1)); return; }
        rifai_git "$d" "$url" "$ramo" "$c" || { GUAI=$((GUAI + 1)); return; }
    fi
    [ $SECCO = si ] && ! git_suo "$d" && return
    ripara_albero "$d"
    echo "$d|$(git -C "$d" remote get-url origin 2>/dev/null)|$(git -C "$d" branch --show-current)|$(git -C "$d" rev-parse HEAD)" >> "$NUOVO"
    # submodules: only the ones checked out (an empty folder was never asked for)
    while IFS=$'\t' read -r meta p; do
        sha=$(echo "$meta" | cut -d' ' -f2)
        if [ -z "$(ls -A "$d/$p" 2>/dev/null)" ]; then
            # empty: it was never asked for, unless tools/cloni.txt has it (a
            # fresh clone of mariadb-server wants libmariadb and duckdb)
            r=$(reg_di "$d/$p"); [ -n "$r" ] || continue
            echo "  $d/$p: submodule vuota, la prendo"
            fai git -C "$d" submodule -q update --init --filter=blob:none -- "$p" || { GUAI=$((GUAI + 1)); continue; }
            c=$(echo "$r" | cut -d'|' -f4)
            if [ $SECCO = no ] && [ -n "$c" ] && [ "$(git -C "$d/$p" rev-parse HEAD)" != "$c" ]; then
                git -C "$d/$p" fetch -q --filter=blob:none origin "$c" 2>/dev/null
                git -C "$d/$p" checkout -q "$c" || { echo "! $d/$p: non riesco a portarla a $c"; GUAI=$((GUAI + 1)); }
            fi
        fi
        [ -n "$(ls -A "$d/$p" 2>/dev/null)" ] || continue
        nome=$(git config -f "$d/.gitmodules" --get-regexp '^submodule\..*\.path$' |
               awk -v p="$p" '$2 == p {sub(/^submodule\./, "", $1); sub(/\.path$/, "", $1); print $1}')
        surl=$(git config -f "$d/.gitmodules" "submodule.$nome.url")
        case $surl in ../*|./*) surl=$(echo "$url" | sed 's#/[^/]*$##')/${surl#./} ;; esac
        # the commit of tools/cloni.txt wins over the parent's index: a
        # submodule moved by hand (libmariadb) is somewhere else
        ripara "$d/$p" "$surl" "" "$sha"
    done < <(git -C "$d" ls-files -s | awk '$1 == "160000"')
}

# the clones: column 6 of the list
cloni() { grep -v '^#' "$LISTA" | awk -F'|' 'NF >= 6 && $6 != "" {print $1 "|" $2 "|" $6}'; }

scelti=$(cloni)
if [ $# -gt 0 ]; then
    scelti=$(for n in "$@"; do n=${n%/}; echo "$scelti" | awk -F'|' -v n="$n" '$1 == n || $3 == n'; done)
    [ -n "$scelti" ] || { echo "! $*: non e' fra i cloni di $LISTA"; exit 2; }
fi
while IFS='|' read -r nome url d; do
    [ -n "$d" ] || continue
    echo "$nome ($d)"
    ripara "$d" "$url" ""
    if git_suo "$d"; then
        p=$(git -C "$d" status --porcelain --untracked-files=no | grep -c '^.[DT]')
        [ "$p" -gt 0 ] && { echo "! $d: ancora $p file cancellati o cambiati di tipo (git -C $d status)"; GUAI=$((GUAI + 1)); }
    fi
done <<< "$scelti"

# the register: the old lines of the clones not looked at this time stay
if [ $SECCO = no ] && [ -s "$NUOVO" ]; then
    {
        echo "# tools/cloni.txt - written by tools/ripara-cloni.sh: where each clone at the"
        echo "# root stands, so that a copy without .git can be put back. folder|url|branch|commit"
        [ -f "$REG" ] && grep -v '^#' "$REG" | while IFS= read -r r; do
            grep -q "^${r%%|*}|" "$NUOVO" || echo "$r"; done
        cat "$NUOVO"
    } > "$REG.nuovo"
    if cmp -s "$REG.nuovo" "$REG"; then rm -f "$REG.nuovo"; else mv "$REG.nuovo" "$REG"; fi
fi
[ $GUAI -eq 0 ] || { echo "! $GUAI cose da guardare a mano"; exit 1; }
# -n: exit 1 when there is something to do (tools/ambiente.sh asks this way)
[ $DAFARE -eq 0 ] || { echo "da fare: $DAFARE (tools/ripara-cloni.sh $*)"; exit 1; }
echo "cloni a posto"

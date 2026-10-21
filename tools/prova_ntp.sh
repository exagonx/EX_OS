#!/bin/bash
# =============================================================================
# tools/prova_ntp.sh — date -ntp (date 0.002), in QEMU, booting from the CD
#
#     tools/prova_ntp.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_ntp.sh [dir]    64 bit
#
# tools/prova_ntp.py on the host answers with instants chosen here; with no
# /boot/fuso.cfg (the CD) the zone is UTC+1 with European summer time:
#   summer 10:00:00.250 UTC -> 12:00:01 (ora legale)
#   winter 10:00:00.250 UTC -> 11:00:01
#   last Sunday of March   00:59:59.600 UTC -> 03:00:00 (the change)
#   last Sunday of October 00:59:59.600 UTC -> 02:00:00 (back)
#   a Kiss-o'-Death, and a port nobody answers on: refused, clock untouched
# Then pool.ntp.org for real, against this machine's clock in Europe/Rome.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/ntp-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=256M; EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; export EXOS_RAM=64M; EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
PORTA=$((12300 + RANDOM % 500))

ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

python3 tools/prova_ntp.py $PORTA 2026-07-15T10:00:00.250 2026-01-15T10:00:00.250 \
    2026-03-29T00:59:59.600 2026-10-25T00:59:59.600 2026-07-15T10:00:00,kod \
    > "$D/server.log" 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT

export EXOS_ISTANZA=ntp$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"
export EXOS_QEMU_EXTRA="$EXTRA -nic user,model=e1000"
S=10.0.2.2:$PORTA
timeout 600 python3 tools/qemu_drive.py "date -fuso@3" \
    "echo R1@1" "date -ntp:$S@8" "echo R2@1" "date -npt:$S@8" \
    "echo R3@1" "date -ntp:$S@8" "echo R4@1" "date -ntp:$S@8" \
    "date -set-time:07:07:07@3" "echo R5@1" "date -ntp:$S@8" "date -t@2" \
    "echo R6@1" "date -ntp:10.0.2.2:$((PORTA + 1))@20" "date -t@2" \
    "echo R7@1" "date -ntp@15" "date -t@2" \
    > "$D/exos.log" 2>&1
ORA_HOST=$(TZ=Europe/Rome date +%H:%M:%S)
tr -d '\r' < "$D/exos.log" > "$D/pulito.log"

blocco() { awk -v a="R$1" -v b="R$(( $1 + 1 ))" '$0 ~ "^"a"$" {s=1; next} $0 ~ "^"b"$" {s=0} s' "$D/pulito.log"; }

grep -q "fuso: UTC+1, ora legale europea, server pool.ntp.org  (supposto" "$D/pulito.log" \
    && si "senza /boot/fuso.cfg: UTC+1, ora legale europea, e lo dice" || no "-fuso non dice il fuso supposto"
blocco 1 | grep -qE "orologio rimesso: mercoledi' 15 luglio 2026, 12:00:0[12] \(ora legale\)" \
    && si "estate: 10:00 UTC -> 12:00 (ora legale)" || no "estate (vedi $D/pulito.log)"
blocco 2 | grep -qE "orologio rimesso: giovedi' 15 gennaio 2026, 11:00:0[12]$" \
    && si "inverno: 10:00 UTC -> 11:00 (-npt vale come -ntp)" || no "inverno"
blocco 3 | grep -qE "orologio rimesso: domenica 29 marzo 2026, 03:00:0[01] \(ora legale\)" \
    && si "29 marzo, 00:59:59,6 UTC -> 03:00 (il cambio cade sul secondo scritto)" || no "cambio di marzo"
blocco 4 | grep -qE "orologio rimesso: domenica 25 ottobre 2026, 02:00:0[01]$" \
    && si "25 ottobre, 00:59:59,6 UTC -> 02:00 (fine dell'ora legale)" || no "cambio di ottobre"
blocco 5 | grep -q "risponde ma non da' l'ora (ci chiede di smettere)" \
    && blocco 5 | grep -q "^07:07:" \
    && si "Kiss-o'-Death rifiutato, l'orologio non si tocca" || no "Kiss-o'-Death"
blocco 6 | grep -q "non risponde (porta $((PORTA + 1)) UDP, tre tentativi)" \
    && blocco 6 | grep -q "^07:07:" \
    && si "un server muto: tre tentativi, l'orologio non si tocca" || no "server muto"
if blocco 7 | grep -q "orologio rimesso"; then
    G=$(blocco 7 | grep -E "^[0-9]{2}:[0-9]{2}:[0-9]{2}$" | tail -1)
    D_S=$(python3 -c "
import sys
a=[int(x) for x in '$G'.split(':')]; b=[int(x) for x in '$ORA_HOST'.split(':')]
print((b[0]*3600+b[1]*60+b[2])-(a[0]*3600+a[1]*60+a[2]))")
    # ! L'ora di qui si legge DOPO che QEMU si e' spento: e' piu' avanti di
    # quella dell'ultimo `date -t` di qualche secondo, mai indietro.
    [ "$D_S" -ge 0 ] && [ "$D_S" -le 20 ] \
        && si "pool.ntp.org: EX-OS $G all'ultimo date -t, qui $ORA_HOST a QEMU spento (Europe/Rome)" \
        || no "pool.ntp.org: EX-OS $G contro $ORA_HOST"
else
    no "pool.ntp.org non ha rimesso l'orologio (rete esterna? vedi $D/pulito.log)"
fi
echo "=== $ok riusciti, $no no ==="
[ "$no" -eq 0 ]

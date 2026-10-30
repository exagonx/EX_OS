#!/bin/bash
# =============================================================================
# tools/prova_proc.sh — /proc dentro EX-OS (voce 45)
#
# ! cat e' interno alla shell: per /proc/self si usa grep, un processo vero.
#
# Legge ogni file, poi controlla: i numeri giusti per il processo che chiede,
# il contatore di CPU che cresce, il formato a 52 campi di stat, i fili di un
# programma multi-filo sotto task/, readlink di exe e cwd, e che scrivere,
# creare e smontare siano rifiutati.
#
#     tools/prova_proc.sh [directory-di-lavoro]
#     EXOS_ARCH=x86_64 tools/prova_proc.sh ...       (a 64 bit)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos/proc}"
mkdir -p "$D"
if [ "${EXOS_ARCH:-i386}" = x86_64 ]; then
    export EXOS_CDROM=dist/exos64.iso EXOS_QEMU=qemu-system-x86_64
else
    export EXOS_CDROM=dist/exos.iso
fi
[ -f "$EXOS_CDROM" ] || { echo "manca $EXOS_CDROM" >&2; exit 1; }
export EXOS_ISTANZA=proc EXOS_NO_FLOPPY=1 EXOS_RAM="${EXOS_RAM:-256M}"
timeout 240 python3 tools/qemu_drive.py \
    "ls /@2" "ls /proc@2" "cat /proc/version@2" "cat /proc/meminfo@2" \
    "cat /proc/cpuinfo@2" "cat /proc/stat@2" "cat /proc/uptime@2" \
    "cat /proc/mounts@2" "grep -e Name -e Threads /proc/self/status@2" "grep . /proc/self/stat@2" \
    "cat /proc/self/limits@2" "readlink /proc/self/exe@2" \
    "cd /boot@1" "readlink /proc/self/cwd@2" "cd /@1" \
    "ls /proc/self@2" "ls /proc/self/fd@2" "ls /proc/1@2" "cat /proc/1/stat@2" \
    "cat /proc/1/task/1/stat@2" "cat /proc/manca@2" "mkdir /proc/x@2" \
    "cat /proc/version > /proc/meminfo@2" "rm /proc/version@2" "umount /proc@2" \
    "cat /proc/uptime@2" "echo FINE@1" > "$D/qemu.log" 2>&1
sed 's/\x1b\[[0-9;]*m//g' /tmp/exos/serialproc.txt | tr -d '\r' > "$D/seriale.txt" 2>/dev/null
S="$D/seriale.txt"
esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }
c() { if grep -a -q -E -- "$1" "$S"; then ok "$2"; else no "$2 ($1)"; fi; }
c '^EX-OS version 0\.[0-9]+' version
c '^MemTotal: +[1-9][0-9]+ kB' meminfo
c '^model name' cpuinfo
c '^cpu  [0-9]+ 0 0 [0-9]+' stat
c '^[0-9]+\.[0-9]{2} [0-9]+\.[0-9]{2}$' uptime
c '^proc /proc proc ro 0 0' 'mounts, /proc c'"'"'e'
c '^Name:.grep' 'status del processo che chiede'
c '^Threads:.1$' 'status: i fili'
c '^[0-9]+ \(grep\) R ' 'stat: il nome e lo stato'
c '^Max open files +[0-9]+' limits
c '^/.*readlink$' 'readlink /proc/self/exe'
c '^/boot$' 'readlink /proc/self/cwd'
n=$(grep -a -E '^[0-9]+ \(grep\) R ' "$S" | head -1 | wc -w)
[ "$n" = 52 ] && ok "stat ha 52 campi" || no "stat ha $n campi"
c 'task' 'ls /proc/1 mostra task'
c '^1 \([a-z]+\) [RS] ' '/proc/1/stat'
grep -a -A2 'mkdir /proc/x' "$S" | grep -a -q -i 'sola lettura\|errore\|impossibile\|non' && ok "mkdir rifiutato" || no "mkdir"
grep -a -A2 'rm /proc/version' "$S" | grep -a -q -i 'sola lettura\|errore\|impossibile\|non' && ok "rm rifiutato" || no "rm"
grep -a -A2 'umount /proc' "$S" | grep -a -q -i 'occupat\|usato\|errore\|impossibile\|non' && ok "umount rifiutato" || no "umount"
grep -a -c '^EX-OS version' "$S" | grep -q '^1$' && ok "la scrittura non ha cambiato niente" || no "scrittura"
[ $esito -eq 0 ] && echo "TUTTO A POSTO" || echo "QUALCOSA NON VA (vedi $D)"
exit $esito

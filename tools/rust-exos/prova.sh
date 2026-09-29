#!/bin/bash
# =============================================================================
# tools/rust-exos/prova.sh — @RUST-0: un programma Rust no_std dentro EX-OS
#
# Fatto il 28 settembre 2026 (tappa 2 di Exilla). Compila tools/rust-exos/prova
# col bersaglio i686-unknown-exos.json (Rust nightly con rust-src), lo collega
# come un programma C — crt0.o, libc.a, libgcc.a della toolchain i386-exos — e
# lo esegue dentro EX-OS in QEMU. Il verdetto e' la sua uscita sulla seriale.
#
#     tools/rust-exos/prova.sh
#
# Dove sta cosa (regola: dentro il progetto, fuori da MEGA cio' che e' di una
# macchina sola — vedi .megaignore):
#   cross_build/<macchina>/rust-macchina     rustup e il nightly (RUSTUP_HOME)
#   cross_build/<macchina>/costruzione-rust  gli oggetti di cargo
#   cross_build/<macchina>/exos-cross        il ld i386-exos di questa macchina
#   cross_build/exos-cross                   crt0.o, libc.a, libgcc.a (condivisi)
#
# ! NON LANCIA make: il CD e' quello di dist/ (ISTRUZIONI-SECONDO-PROFILO.md).
#
# Preparazione, una volta per macchina:
#   RUSTUP_HOME=cross_build/<macchina>/rust-macchina/rustup \
#   CARGO_HOME=cross_build/<macchina>/rust-macchina/cargo \
#     rustup toolchain install nightly --profile minimal -c rust-src
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$RADICE/cross_build/$MACCHINA"
T="$RADICE/cross_build/exos-cross"
D="$B/costruzione-prova"
RUSTUP="${RUSTUP:-$(command -v rustup || echo "$HOME/.cargo/bin/rustup")}"
CARGO="$(dirname "$RUSTUP")/cargo"
mkdir -p "$D"

export RUSTUP_HOME="$B/rust-macchina/rustup" CARGO_HOME="$B/rust-macchina/cargo"
export CARGO_TARGET_DIR="$B/costruzione-rust"

# --- 1. Rust: core e compiler_builtins per il bersaglio, e il programma -------
# ! -Z json-target-spec: dal 2026 un bersaglio da file .json va dichiarato.
# ! compiler-builtins-mem: memcpy e compagni li porta compiler_builtins, cosi'
#   il collegamento non dipende da come li chiama la libc.
(cd tools/rust-exos/prova && "$CARGO" +nightly build --release \
    -Z build-std=core,alloc,compiler_builtins \
    -Z build-std-features=compiler-builtins-mem -Z json-target-spec \
    --target ../i686-unknown-exos.json) || exit 1

# --- 2. il collegamento: la stessa strada dei programmi C ----------------------
# ! libprova.a DENTRO il gruppo: main sta li', ma lo chiede libc.a (_libc_start)
# che viene dopo; fuori dal gruppo ld l'avrebbe gia' scartata.
"$B/exos-cross/bin/i386-exos-ld" -m elf_i386 -static -Ttext-segment=0x08000000 \
    --gc-sections -e _start "$T/i386-exos/lib/crt0.o" \
    --start-group "$CARGO_TARGET_DIR/i686-unknown-exos/release/libprova.a" \
    "$T/i386-exos/lib/libc.a" "$T/lib/gcc/i386-exos/17.0.0/libgcc.a" --end-group \
    -o "$D/rustprova" || exit 1

# ! NIENTE SSE NE' MMX: il bersaglio li spegne come il Makefile per il C.
n=$("$B/exos-cross/bin/i386-exos-objdump" -d "$D/rustprova" | grep -c 'xmm\|%mm[0-7]')
echo "collegato: $(stat -c %s "$D/rustprova") byte, istruzioni SSE/MMX: $n"
[ "$n" = 0 ] || { echo "[NO] il codice usa SSE o MMX"; exit 1; }

# --- 3. dentro EX-OS -----------------------------------------------------------
IMG="$D/prova-hd.img"
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$(command -v sfdisk || echo /sbin/sfdisk)" "$IMG" > /dev/null 2>&1
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=exilla EXOS_NO_FLOPPY=1 EXOS_CDROM="${EXOS_CDROM:-dist/exos.iso}"
export EXOS_QEMU_EXTRA="$KVM -drive file=$IMG,format=raw,if=ide"
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@30" > "$D/0.log" 2>&1
"$(command -v debugfs || echo /sbin/debugfs)" -w -R "write $D/rustprova rustprova" \
    "$IMG?offset=1048576" > /dev/null 2>&1
SER=/tmp/exos/serialexilla.txt
rm -f "$SER"
timeout 300 python3 tools/qemu_drive.py "mount hd0p1 /disk@5" \
    "/disk/rustprova uno due@4" "echo FINE@2" > "$D/1.log" 2>&1

uscita=$(tr -d '\r' < "$SER" 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | \
         sed -n '/rust-exos: un/,/rust-exos: [tQ]/p')
echo "$uscita"
if echo "$uscita" | grep -q "rust-exos: tutto a posto" && echo "$uscita" | grep -q "argc        3"; then
    echo "=== [OK] Rust gira dentro EX-OS ==="
    exit 0
fi
echo "=== [NO] vedi $D/1.log e $SER ==="
exit 1

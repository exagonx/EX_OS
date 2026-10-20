#!/bin/bash
# =============================================================================
# shell.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
# This file is part of EX-OS, distributed under the GNU GPL v2.
# See the LICENSE file in the project root for the full license text.
# =============================================================================
#
# Installa su Debian (e derivate: Ubuntu, Mint, Devuan, Raspbian x86...)
# tutto cio' che serve a costruire EX-OS, e poi VERIFICA che serva davvero:
# non basta che apt dica di aver installato i pacchetti, deve compilare un
# oggetto a 32 bit e collegarlo con l'emulazione elf_i386.
#
#     ./shell.sh                 INSTALLA TUTTO: sistema, prove, strumenti,
#                                sviluppo (G++, Fortran, cmake...) e GUI
#                                (GTK 3, GTK 4, Qt 5, Qt 6)
#     ./shell.sh --senza-gui     tutto tranne GTK e Qt
#     ./shell.sh --minimo        solo `make all` e `make run`: niente cross,
#                                niente sviluppo extra, niente GUI
#     ./shell.sh --controlla     non installa niente, dice solo cosa manca
#
# -----------------------------------------------------------------------------
# ! PERCHE' NON SERVE UN CROSS-COMPILATORE PER IL SISTEMA
#
# Il Makefile compila con il GCC di sistema:
#
#     CC := gcc
#     CFLAGS := -m32 -march=pentium-mmx -ffreestanding -fno-builtin ...
#     LDFLAGS := -m elf_i386 -nostdlib
#
# Cioe' il GCC x86_64 di Debian in modalita' 32 bit, senza libreria
# standard e senza runtime. Su una macchina amd64 questo vuol dire il
# pacchetto `gcc-multilib`: senza, `gcc -m32` fallisce, e fallisce in un
# modo che non nomina il pacchetto mancante.
#
# Il cross-compilatore i386-exos (~/exos-cross) e' un'ALTRA cosa e serve
# solo per `make iso`, il CD degli strumenti: quello si costruisce con
# tools/gcc-exos/prepara-cross.sh, e le sue dipendenze sono il gruppo
# "strumenti" qui sotto (GMP, MPFR, MPC, texinfo, bison, flex).
#
# ! IL PACCHETTO QEMU NON SI CHIAMA `qemu-system-i386`. Su Debian quel
# nome non esiste: il binario qemu-system-i386 sta dentro
# `qemu-system-x86`. Chiederlo per nome fa fallire l'intera riga di
# apt-get, e con -y non lo si vede nemmeno passare.
# =============================================================================

set -u

# ! IL PATH DI `su` SENZA TRATTINO NON HA LE DIRECTORY sbin. Con `su` (e non
# `su -`) l'ambiente dell'utente di partenza resta com'e': /sbin e /usr/sbin
# non ci sono, e comandi come sfdisk, ldconfig o usermod sembrano "non
# presenti" anche se sono installati. Si aggiungono qui, una volta per tutte.
export PATH="$PATH:/usr/local/sbin:/usr/sbin:/sbin"

# --- Colori -------------------------------------------------------------------
if [ -t 1 ]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
    CYAN='\033[0;36m'; NC='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; CYAN=''; NC=''
fi

log_info() { echo -e "${CYAN}[INFO]${NC} $1"; }
log_ok()   { echo -e "${GREEN}[ OK ]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_err()  { echo -e "${RED}[ERR ]${NC} $1" >&2; }
log_step() { echo ""; echo -e "${CYAN}=== $1 ===${NC}"; }

muori() { log_err "$1"; exit 1; }

# --- Argomenti ----------------------------------------------------------------
# ! IL DEFAULT E' «TUTTO», E NON E' UN CAPRICCIO. Il gruppo strumenti sono
# le dipendenze del cross-compilatore i386-exos, cioe' di `make iso`: se
# mancano non si scopre adesso, si scopre a meta' della costruzione di GCC,
# venti minuti dopo, con un configure che si ferma su un header. Il gruppo
# costa una manciata di pacchetti; chi non li vuole dice --minimo.
VUOLE_STRUMENTI=1
VUOLE_SVILUPPO=1   # suite GCC completa e strumenti di build (cmake, meson...)
VUOLE_GUI=1        # GTK 3, GTK 4, Qt 5, Qt 6 (intestazioni, strumenti, esempi)
SOLO_CONTROLLO=0

for arg in "$@"; do
    case "$arg" in
        --minimo|--base)     VUOLE_STRUMENTI=0; VUOLE_SVILUPPO=0; VUOLE_GUI=0 ;;
        --strumenti)         VUOLE_STRUMENTI=1 ;;   # gia' il default: accettato
        --tutto)             VUOLE_STRUMENTI=1; VUOLE_SVILUPPO=1; VUOLE_GUI=1 ;;
        --senza-gui)         VUOLE_GUI=0 ;;
        --gui)               VUOLE_GUI=1 ;;         # gia' il default: accettato
        --controlla|-n)      SOLO_CONTROLLO=1 ;;
        --aiuto|-h|--help)
            sed -n '13,/^# -----/p' "$0" | sed '$d' | sed 's/^# \?//'
            exit 0 ;;
        *) muori "argomento sconosciuto: $arg (prova --aiuto)" ;;
    esac
done

echo ""
echo "  EX-OS — dipendenze di costruzione per Debian e derivate"
echo "  Graziano Falcone <exagonx@hotmail.com>"
echo ""

# =============================================================================
# 1. Il sistema e' quello giusto?
# =============================================================================
log_step "Controllo del sistema"

# ! $OSTYPE E $EUID SONO VARIABILI DI BASH, NON COMANDI. Per sapere su che
# sistema si e' e chi si e' non servono uname e id: meno comandi da cui
# dipende lo script, meno cose che possono mancare prima ancora di partire.
case "${OSTYPE:-}" in
    linux*) ;;
    *) muori "questo script e' per Linux" ;;
esac
command -v apt-get >/dev/null 2>&1 || muori "apt-get non c'e': questo script e' per Debian e derivate"
command -v dpkg    >/dev/null 2>&1 || muori "dpkg non c'e': questo script e' per Debian e derivate"

if [ -r /etc/os-release ]; then
    . /etc/os-release
    log_info "distribuzione: ${PRETTY_NAME:-$ID}"
fi

ARCH=$(dpkg --print-architecture)
log_info "architettura: $ARCH"

# ! SOLO SU amd64 SERVE IL MULTILIB. Su una i386 il compilatore produce
# gia' codice a 32 bit e `gcc-multilib` li' non esiste nemmeno: chiederlo
# farebbe fallire l'installazione su una macchina dove non manca niente.
MULTILIB=""
case "$ARCH" in
    amd64) MULTILIB="gcc-multilib" ;;
    i386)  log_info "architettura a 32 bit: -m32 e' il default, niente multilib" ;;
    *)     log_warn "architettura $ARCH: EX-OS si costruisce per x86 a 32 bit,"
           log_warn "qui servira' un cross-compilatore i686 che questo script non installa" ;;
esac

# Chi lancia il comando: root direttamente, oppure sudo.
# Con --controlla non si installa niente, quindi non serve ne' root ne' sudo.
if [ "$EUID" -eq 0 ]; then
    SUDO=""
elif command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
elif [ "$SOLO_CONTROLLO" -eq 1 ]; then
    SUDO=""
else
    log_err "non sei root e sudo non c'e'. Due strade:"
    log_err "  1) come root:   su -   e poi rilancia questo script"
    log_err "  2) installa sudo:  su -c 'apt-get install -y sudo && usermod -aG sudo ${USER:-il_tuo_utente}'"
    log_err "     poi esci e rientra (il gruppo si applica al nuovo accesso)"
    exit 1
fi

# apt-get update una volta sola: serve prima di installare i comandi che
# mancano allo script e poi prima dei pacchetti veri.
export DEBIAN_FRONTEND=noninteractive
APT_AGGIORNATO=0
aggiorna_apt() {
    [ "$APT_AGGIORNATO" -eq 1 ] && return 0
    log_info "aggiornamento dell'elenco dei pacchetti..."
    $SUDO apt-get update -qq || log_warn "apt-get update non e' riuscito: si prova lo stesso"
    APT_AGGIORNATO=1
}

# =============================================================================
# 1b. I comandi che LO SCRIPT STESSO usa
# =============================================================================
# ! PRIMA DI USARLI SI VERIFICA CHE CI SIANO. Su una Debian appena installata,
# o in un contenitore, puo' mancare qualcosa che lo script da' per scontato
# (mktemp, wc, tail, sed...). Senza questo controllo si scopre a meta'
# dell'installazione, con un "comando non trovato" e un risultato a meta'.
# Il formato e' comando:pacchetto, cioe' "quel comando lo da' quel pacchetto".
# Non compaiono uname e id (qui sono sostituiti da $OSTYPE e $EUID di bash),
# ne' sudo (controllato sopra), ne' dpkg e apt-get (se mancano non e' Debian).
log_step "Comandi usati dallo script"

CMD_SCRIPT="mktemp:coreutils wc:coreutils tail:coreutils head:coreutils basename:coreutils cat:coreutils rm:coreutils ls:coreutils sed:sed"
MANCA_CMD=""
MANCA_PKG=""
for voce in $CMD_SCRIPT; do
    c=${voce%%:*}
    p=${voce##*:}
    command -v "$c" >/dev/null 2>&1 && continue
    log_warn "$c non trovato (lo fornisce: $p)"
    MANCA_CMD="$MANCA_CMD $c"
    case " $MANCA_PKG " in *" $p "*) ;; *) MANCA_PKG="$MANCA_PKG $p" ;; esac
done

if [ -z "$MANCA_CMD" ]; then
    log_ok "tutti i comandi dello script sono presenti"
elif [ "$SOLO_CONTROLLO" -eq 1 ]; then
    muori "mancano:$MANCA_CMD — installa:$MANCA_PKG (sudo apt-get install$MANCA_PKG)"
else
    aggiorna_apt
    log_info "installo:$MANCA_PKG"
    $SUDO apt-get install -y -qq $MANCA_PKG >/dev/null 2>&1 \
        || muori "installazione di$MANCA_PKG non riuscita: controlla la rete e i repository"
    hash -r    # il PATH e' lo stesso ma bash ricorda cio' che non trovava
    for c in $MANCA_CMD; do
        command -v "$c" >/dev/null 2>&1 || muori "$c manca ancora dopo l'installazione di$MANCA_PKG"
    done
    log_ok "comandi mancanti installati:$MANCA_CMD"
fi

# =============================================================================
# 2. I pacchetti, divisi per cio' che servono a fare
# =============================================================================
#
# BASE — `make all`, cioe' bootloader, kernel, driver, programmi, floppy:
#   build-essential   gcc, g++, make, libc6-dev: il compilatore e il motore
#   $MULTILIB         gcc -m32 su amd64 (vedi la nota in testa)
#   binutils          ld -m elf_i386, objcopy, objdump, nm, readelf, size,
#                     strip, ar, ranlib — il Makefile li usa tutti
#   nasm              i due stage del bootloader e entry.asm (e ndisasm,
#                     che serve a `make disasm-stage1`)
#   mtools            mformat, mmd, mcopy, mattrib, mdir: dist/floppy.img si
#                     riempie senza montare niente e senza root
#   dosfstools        FAT12/16 quando serve fuori da mtools
#   python3           tools/mkiso.py, mkimg.py, genlibc.py, bin2c.py,
#                     qemu_drive.py e le prove: SOLO libreria standard,
#                     niente pip, niente virtualenv
#   util-linux        sfdisk, che tools/mkhd.sh usa per la tabella delle
#                     partizioni del disco rigido
#   xxd               letture di byte grezzi negli script di collaudo
#   file, patch, bc   contorno degli script
#
PKG_BASE="build-essential binutils nasm mtools dosfstools python3 util-linux file patch bc"
[ -n "$MULTILIB" ] && PKG_BASE="$PKG_BASE $MULTILIB"

# PROVE — `make run`, `make run-hd`, `make debug`:
#   qemu-system-x86   contiene qemu-system-i386 (vedi la nota in testa)
#   qemu-utils        qemu-img, che tools/mkhd.sh usa per creare il disco
#   gdb               `make debug`, stub sulla porta 1234
PKG_PROVE="qemu-system-x86 qemu-utils gdb"

# STRUMENTI — il CD `make iso`: cross-compilatore i386-exos, binutils
# nativi, OpenSSL, QuickJS, FreeBASIC. Sono le dipendenze di
# tools/gcc-exos/, tools/binutils-exos/, tools/openssl-exos/ eccetera.
#   libgmp-dev libmpfr-dev libmpc-dev libisl-dev   GCC non si costruisce senza
#   texinfo           makeinfo: senza, binutils e GCC si fermano sulla doc
#   bison flex gawk   generatori usati dai sorgenti di GCC e binutils
#   perl              il Configure di OpenSSL e' uno script Perl
#   autoconf automake libtool pkg-config   i pacchetti GNU portati
#                     (coreutils, sed, grep, awk, make) rigenerano il configure
#   zlib1g-dev        richiesta ricorrente dei configure sull'host
#   zip unzip         `make iso-parti` e `make iso-unisci`
#   git curl wget ca-certificates   i sorgenti di GCC, binutils e OpenSSL
#                     NON stanno nel repository: si scaricano
PKG_STRUMENTI="libgmp-dev libmpfr-dev libmpc-dev libisl-dev texinfo bison flex gawk perl \
autoconf automake libtool pkg-config zlib1g-dev zip unzip git curl wget ca-certificates"

# SVILUPPO — il resto della suite GCC e gli strumenti di compilazione:
#   g++ gfortran      i front-end C++ e Fortran (g++ e' gia' in build-essential)
#   g++-multilib      g++ -m32 su amd64 (solo li')
#   cmake ninja-build meson   i sistemi di build che GTK 4 e Qt 6 si aspettano
#   ccache            ricompilazioni veloci
#   valgrind          perdite di memoria e accessi non validi
# Altre librerie di sviluppo (SQLite, OpenSSL, MariaDB...) si aggiungono qui.
PKG_SVILUPPO="g++ gfortran cmake ninja-build meson ccache valgrind"
[ "$ARCH" = "amd64" ] && PKG_SVILUPPO="$PKG_SVILUPPO g++-multilib"

# GTK — intestazioni, strumenti ed esempi:
#   libgtk-3-dev libgtk-4-dev      GTK 3 e GTK 4 (portano GLib, Cairo, Pango)
#   libadwaita-1-dev               libadwaita per GTK 4
#   libgtksourceview-4-dev/-5-dev  il widget editor di codice per GTK 3 e GTK 4
#   libgtkmm-3.0-dev/-4.0-dev      i binding C++
#   gtk-3-examples gtk-4-examples  gtk3-demo, gtk4-demo: prova rapida che gira
#   glade             il disegnatore di interfacce per GTK 3
PKG_GTK="libgtk-3-dev libgtk-4-dev libadwaita-1-dev libgtksourceview-4-dev libgtksourceview-5-dev libgtkmm-3.0-dev libgtkmm-4.0-dev gtk-3-examples gtk-4-examples glade"

# QT — Qt 6 e Qt 5, con gli strumenti (moc, uic, rcc, Designer, Linguist):
#   qt6-base-dev qt6-base-dev-tools    Core, Gui, Widgets, Network, Sql
#   qt6-tools-dev(-tools) qt6-l10n-tools   Designer, Linguist, lrelease
#   qt6-declarative-dev   QML e Quick
#   qt6-svg-dev qt6-multimedia-dev     SVG e audio/video
#   qtcreator         l'IDE
#   qtbase5-dev ecc.  la stessa cosa per Qt 5, che molti progetti usano ancora
PKG_QT="qt6-base-dev qt6-base-dev-tools qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools qt6-declarative-dev qt6-svg-dev qt6-multimedia-dev qtcreator qtbase5-dev qtbase5-dev-tools qttools5-dev qttools5-dev-tools qtdeclarative5-dev libqt5svg5-dev"

PACCHETTI="$PKG_BASE $PKG_PROVE"
[ "$VUOLE_STRUMENTI" -eq 1 ] && PACCHETTI="$PACCHETTI $PKG_STRUMENTI"
[ "$VUOLE_SVILUPPO" -eq 1 ]  && PACCHETTI="$PACCHETTI $PKG_SVILUPPO"
[ "$VUOLE_GUI" -eq 1 ]       && PACCHETTI="$PACCHETTI $PKG_GTK $PKG_QT"

# =============================================================================
# 3. Installazione
# =============================================================================
if [ "$SOLO_CONTROLLO" -eq 1 ]; then
    log_step "Solo controllo (--controlla): non viene installato niente"
else
    log_step "Installazione dei pacchetti"

    GRUPPI="sistema + prove"
    [ "$VUOLE_STRUMENTI" -eq 1 ] && GRUPPI="$GRUPPI + strumenti"
    [ "$VUOLE_SVILUPPO" -eq 1 ]  && GRUPPI="$GRUPPI + sviluppo"
    [ "$VUOLE_GUI" -eq 1 ]       && GRUPPI="$GRUPPI + GTK/Qt"
    log_info "gruppi: $GRUPPI ($(echo $PACCHETTI | wc -w) pacchetti)"

    aggiorna_apt

    # ! UN PACCHETTO PER VOLTA, E NON TUTTI INSIEME. Con una sola riga
    # apt-get -y, un nome sbagliato o assente su questa distribuzione fa
    # fallire l'INTERA riga: nessuno degli altri viene installato, e con
    # -y in mezzo a centinaia di righe di output non lo si nota. Il ciclo
    # costa qualche secondo in piu' e dice esattamente quale nome manca.
    #
    # ! NIENTE DOMANDE A META' STRADA. Qualche pacchetto (per esempio i
    # dati di configurazione di alcune librerie) apre un dialogo testuale
    # e resta li' ad aspettare: dentro uno script lanciato e lasciato
    # andare vuol dire un'installazione ferma senza che nessuno guardi.
    export DEBIAN_FRONTEND=noninteractive

    REGISTRO=$(mktemp -t exos-apt-XXXXXX.log)
    MANCATI=""
    NUOVI=0

    for p in $PACCHETTI; do
        if dpkg -s "$p" >/dev/null 2>&1; then
            echo "  = $p: gia' installato"
            continue
        fi
        printf "  + %s ... " "$p"
        if $SUDO apt-get install -y -o Dpkg::Use-Pty=0 "$p" >> "$REGISTRO" 2>&1; then
            echo "fatto"
            NUOVI=$((NUOVI + 1))
        else
            echo "NON RIUSCITO"
            MANCATI="$MANCATI $p"
            # La ragione vera sta nelle ultime righe di apt, non nel suo
            # codice di uscita: nome inesistente su questa distribuzione,
            # disco pieno, repository non raggiungibile sono guasti diversi
            # e si risolvono in modi diversi.
            tail -3 "$REGISTRO" | sed 's/^/        /'
        fi
    done

    log_info "$NUOVI pacchetti nuovi installati; registro completo: $REGISTRO"

    # xxd: pacchetto a se' sulle Debian recenti, dentro vim-common su
    # quelle piu' vecchie. Si prova il primo nome e si ripiega sul secondo.
    if ! command -v xxd >/dev/null 2>&1; then
        $SUDO apt-get install -y -qq xxd >/dev/null 2>&1 \
            || $SUDO apt-get install -y -qq vim-common >/dev/null 2>&1 \
            || MANCATI="$MANCATI xxd"
    fi

    if [ -n "$MANCATI" ]; then
        log_warn "pacchetti non installati:$MANCATI"
        log_warn "il controllo qui sotto dice se erano indispensabili"
    fi
fi

# =============================================================================
# 4. Verifica: non i pacchetti, ma cio' che devono saper fare
# =============================================================================
# ! SI CONTROLLA IL RISULTATO, NON L'ELENCO. `dpkg -s gcc-multilib` puo'
# dire di si' e `gcc -m32` fallire lo stesso (multilib a meta', header a
# 32 bit assenti). L'unica prova che conta e' compilare e collegare come
# fa il Makefile.
log_step "Verifica"

GUAI=0            # cio' che ferma `make all`
GUAI_STRUMENTI=0  # cio' che ferma solo il CD degli strumenti
GUAI_SVIL=0       # cio' che manca di sviluppo e GUI: non ferma `make all`

controlla() {  # controlla <comando> <pacchetto>
    if command -v "$1" >/dev/null 2>&1; then
        log_ok "$1"
    else
        log_err "$1 manca — sudo apt install $2"
        GUAI=$((GUAI + 1))
    fi
}

controlla gcc              build-essential
controlla ld               binutils
controlla objcopy          binutils
controlla objdump          binutils
controlla nm               binutils
controlla make             build-essential
controlla nasm             nasm
controlla ndisasm          nasm
controlla mformat          mtools
controlla mcopy            mtools
controlla mattrib          mtools
controlla python3          python3
controlla qemu-system-i386 qemu-system-x86
controlla qemu-img         qemu-utils
controlla gdb              gdb

# sfdisk vive in /sbin, che nel PATH di un utente normale non c'e':
# tools/mkhd.sh lo sa e ripiega su /usr/sbin/sfdisk. Qui si guarda nello
# stesso modo, altrimenti si annuncia un guaio che non esiste.
if command -v sfdisk >/dev/null 2>&1 || [ -x /usr/sbin/sfdisk ] || [ -x /sbin/sfdisk ]; then
    log_ok "sfdisk"
else
    log_err "sfdisk manca — sudo apt install util-linux"
    GUAI=$((GUAI + 1))
fi

# --- La prova vera: compilare e collegare come il Makefile --------------------
PROVA=$(mktemp -d)
trap 'rm -rf "$PROVA"' EXIT

cat > "$PROVA/p.c" <<'SORGENTE'
/* Le stesse condizioni del kernel: niente libreria, niente runtime. */
void _start(void) { for (;;) { } }
SORGENTE

if ! command -v gcc >/dev/null 2>&1; then
    log_warn "gcc assente: la prova di compilazione e' saltata"
elif gcc -m32 -march=pentium-mmx -ffreestanding -fno-builtin -fno-stack-protector \
       -fno-pic -fno-pie -c "$PROVA/p.c" -o "$PROVA/p.o" 2> "$PROVA/cc.log"; then
    log_ok "gcc -m32 -march=pentium-mmx compila"
else
    log_err "gcc -m32 NON compila:"
    sed 's/^/       /' "$PROVA/cc.log" | head -5 >&2
    [ -n "$MULTILIB" ] && log_err "       manca quasi certamente: sudo apt install gcc-multilib"
    GUAI=$((GUAI + 1))
fi

if [ -f "$PROVA/p.o" ]; then
    if ! command -v ld >/dev/null 2>&1; then
        log_warn "ld assente: la prova di collegamento e' saltata"
    elif ld -m elf_i386 -nostdlib -e _start -Ttext 0x100000 \
          "$PROVA/p.o" -o "$PROVA/p.elf" 2> "$PROVA/ld.log"; then
        log_ok "ld -m elf_i386 collega"
    else
        log_err "ld -m elf_i386 NON collega:"
        sed 's/^/       /' "$PROVA/ld.log" | head -5 >&2
        GUAI=$((GUAI + 1))
    fi
fi

# nasm deve produrre sia il binario piatto (stage1) sia l'ELF32 (entry.asm).
if command -v nasm >/dev/null 2>&1; then
printf 'bits 16\norg 0x7c00\njmp $\n' > "$PROVA/s.asm"
if nasm -f bin "$PROVA/s.asm" -o "$PROVA/s.bin" 2>/dev/null; then
    log_ok "nasm -f bin"
else
    log_err "nasm -f bin non funziona"; GUAI=$((GUAI + 1))
fi
printf 'bits 32\nglobal _e\n_e:\n  ret\n' > "$PROVA/e.asm"
if nasm -f elf32 "$PROVA/e.asm" -o "$PROVA/e.o" 2>/dev/null; then
    log_ok "nasm -f elf32"
else
    log_err "nasm -f elf32 non funziona"; GUAI=$((GUAI + 1))
fi
else
    log_warn "nasm assente: le prove di assemblaggio sono saltate"
fi

# python3: gli script del progetto usano solo la libreria standard, ma
# vogliono un interprete abbastanza recente.
if command -v python3 >/dev/null 2>&1; then
    if python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)'; then
        log_ok "python3 $(python3 -c 'import sys; print("%d.%d" % sys.version_info[:2])')"
    else
        log_err "python3 troppo vecchio (serve 3.8 o piu')"; GUAI=$((GUAI + 1))
    fi
fi

if [ "$VUOLE_STRUMENTI" -eq 1 ]; then
    echo ""
    log_info "gruppo strumenti (cross-compilatore i386-exos e ISO):"
    for c in makeinfo bison flex perl autoconf automake libtool git; do
        if command -v "$c" >/dev/null 2>&1; then
            log_ok "$c"
        else
            log_err "$c manca"; GUAI_STRUMENTI=$((GUAI_STRUMENTI + 1))
        fi
    done
    for h in /usr/include/gmp.h /usr/include/mpfr.h /usr/include/mpc.h; do
        if [ -f "$h" ] || ls /usr/include/*/"$(basename "$h")" >/dev/null 2>&1; then
            log_ok "$(basename "$h")"
        else
            log_err "$(basename "$h") manca — GCC non si costruisce senza"
            GUAI_STRUMENTI=$((GUAI_STRUMENTI + 1))
        fi
    done
fi

controlla_extra() {  # controlla_extra <comando> <pacchetto>: non ferma `make all`
    if command -v "$1" >/dev/null 2>&1; then
        log_ok "$1"
    else
        log_err "$1 manca — sudo apt install $2"
        GUAI_SVIL=$((GUAI_SVIL + 1))
    fi
}

# prova_pkg <modulo pkg-config> <nome> <pacchetto> <c|cxx> <sorgente>
# ! COME PER IL SISTEMA, SI PROVA A COMPILARE E COLLEGARE davvero un programma
# minimo: pkg-config puo' dire di si' e mancare un'intestazione o una libreria.
prova_pkg() {
    local mod=$1 nome=$2 pkg=$3 lang=$4 src=$5 cc=gcc ext=c extra=""
    if ! pkg-config --exists "$mod" 2>/dev/null; then
        log_err "$nome manca — sudo apt install $pkg"
        GUAI_SVIL=$((GUAI_SVIL + 1))
        return
    fi
    if [ "$lang" = "cxx" ]; then cc=g++; ext=cpp; extra="-fPIC"; fi
    if ! command -v "$cc" >/dev/null 2>&1; then
        log_err "$nome c'e' ma $cc manca — sudo apt install build-essential"
        GUAI_SVIL=$((GUAI_SVIL + 1))
        return
    fi
    printf '%s\n' "$src" > "$PROVA/gui.$ext"
    if $cc $extra "$PROVA/gui.$ext" -o "$PROVA/gui.bin" $(pkg-config --cflags --libs "$mod") 2> "$PROVA/gui.log"; then
        log_ok "$nome $(pkg-config --modversion "$mod") compila e collega"
    else
        log_err "$nome presente ma il programma di prova NON compila:"
        sed 's/^/       /' "$PROVA/gui.log" | head -5 >&2
        GUAI_SVIL=$((GUAI_SVIL + 1))
    fi
}

if [ "$VUOLE_SVILUPPO" -eq 1 ]; then
    echo ""
    log_info "gruppo sviluppo (suite GCC e strumenti di build):"
    for voce in g++:g++ gfortran:gfortran cmake:cmake ninja:ninja-build \
                meson:meson ccache:ccache valgrind:valgrind pkg-config:pkg-config; do
        controlla_extra "${voce%%:*}" "${voce##*:}"
    done
    # g++ -m32 non si prova a compilare: servirebbe libstdc++ a 32 bit anche
    # per un programma vuoto, e non e' un requisito di `make all`.
fi

if [ "$VUOLE_GUI" -eq 1 ]; then
    echo ""
    log_info "gruppo GUI (GTK 3, GTK 4, Qt 6, Qt 5):"
    if ! command -v pkg-config >/dev/null 2>&1; then
        log_err "pkg-config manca — sudo apt install pkg-config (serve per controllare GTK e Qt)"
        GUAI_SVIL=$((GUAI_SVIL + 1))
    else
        SRC_GTK='#include <gtk/gtk.h>
int main(void) { return gtk_get_major_version() ? 0 : 1; }'
        SRC_QT='#include <QtCore/QCoreApplication>
int main(int argc, char **argv) { QCoreApplication a(argc, argv); return 0; }'
        prova_pkg gtk+-3.0 "GTK 3" libgtk-3-dev  c   "$SRC_GTK"
        prova_pkg gtk4     "GTK 4" libgtk-4-dev  c   "$SRC_GTK"
        prova_pkg Qt6Core  "Qt 6"  qt6-base-dev  cxx "$SRC_QT"
        prova_pkg Qt5Core  "Qt 5"  qtbase5-dev   cxx "$SRC_QT"
    fi
    controlla_extra qtcreator qtcreator
    controlla_extra glade     glade
fi

# =============================================================================
# 5. Esito
# =============================================================================
echo ""
if [ "$GUAI" -eq 0 ] && [ "$GUAI_STRUMENTI" -gt 0 ]; then
    log_warn "$GUAI_STRUMENTI controlli del gruppo strumenti falliti."
    log_warn "\`make all\` e \`make run\` funzionano lo stesso: manca solo"
    log_warn "cio' che serve a costruire il cross-compilatore i386-exos."
    echo ""
fi

if [ "$GUAI" -eq 0 ] && [ "$GUAI_SVIL" -gt 0 ]; then
    log_warn "$GUAI_SVIL controlli del gruppo sviluppo/GUI falliti."
    log_warn "\`make all\` funziona lo stesso: mancano solo compilatori, strumenti"
    log_warn "di build, GTK o Qt in piu'. Le righe in rosso qui sopra dicono quali."
    echo ""
fi

if [ "$GUAI" -eq 0 ]; then
    if [ "$GUAI_STRUMENTI" -eq 0 ] && [ "$GUAI_SVIL" -eq 0 ]; then
        echo -e "${GREEN}Tutto a posto. Da qui:${NC}"
    else
        echo -e "${GREEN}Il sistema si costruisce. Da qui:${NC}"
    fi
    echo ""
    echo "    make all        costruisce il sistema e dist/floppy.img"
    echo "    make run        lo avvia in QEMU"
    echo "    make iso-tutte  le due ISO (sistema e strumenti)"
    echo "    make help       tutti i bersagli"
    echo ""
    if [ "$VUOLE_GUI" -eq 1 ]; then
        echo "Per compilare con le librerie grafiche:"
        echo ""
        echo "    gcc prog.c -o prog \$(pkg-config --cflags --libs gtk4)"
        echo "    gcc prog.c -o prog \$(pkg-config --cflags --libs gtk+-3.0)"
        echo "    g++ -fPIC prog.cpp -o prog \$(pkg-config --cflags --libs Qt6Widgets)"
        echo "    qtcreator       l'IDE per Qt   |   glade   il disegnatore GTK 3"
        echo ""
    fi
    if [ "$VUOLE_STRUMENTI" -eq 0 ]; then
        echo "Hai chiesto --minimo: le dipendenze del cross-compilatore i386-exos"
        echo "NON sono installate, e senza quelle \`make iso\` non arriva in fondo."
        echo "Quando servira':  ./shell.sh"
        echo ""
    else
        echo "Il cross-compilatore i386-exos (il CD degli strumenti: gcc, g++,"
        echo "fbc, as, ld, OpenSSL dentro EX-OS) ha adesso tutte le sue"
        echo "dipendenze. Si costruisce con:"
        echo ""
        echo "    tools/gcc-exos/prepara-cross.sh      -> ~/exos-cross"
        echo ""
        echo "I sorgenti di GCC, binutils e OpenSSL non stanno nel repository:"
        echo "vanno scaricati, e i rispettivi tools/*-exos/leggimi.md dicono come."
        echo ""
    fi
    exit 0
else
    echo -e "${RED}$GUAI controlli falliti: la costruzione non partirebbe.${NC}"
    echo "Rilancia ./shell.sh dopo aver risolto, oppure installa a mano"
    echo "i pacchetti nominati qui sopra."
    exit 1
fi

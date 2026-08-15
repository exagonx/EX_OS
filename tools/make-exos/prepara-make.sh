#!/bin/sh
# =============================================================================
# tools/make-exos/prepara-make.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Costruisce GNU make PER EX-OS: un `make` che gira DENTRO il sistema.
#
#     tools/make-exos/prepara-make.sh [sorgenti] [uscita]
#
#     sorgenti   albero di GNU make gia' scompattato  (default: ./make)
#     uscita     dove lasciare il binario             (default: ~/exos-native/build-make)
#
# Il risultato finisce sul CD degli strumenti come /exos/bin/make: lo copia
# la regola di dist/exos-tools.iso nel Makefile.
#
# -----------------------------------------------------------------------------
# PERCHE' SERVE UN make NATIVO
#
# Dentro EX-OS ci sono gia' gcc, g++, cpp, as, ld, ar e fbc: si compila e si
# collega. Quello che manca per costruire un PROGRAMMA VERO — e il primo che
# si vuole costruire e' FreeBASIC — non e' un altro compilatore: e' chi
# decide COSA ricompilare e in che ordine. Senza make, l'unico modo di
# costruire FreeBASIC dentro EX-OS sarebbe battere a mano trecento comandi.
#
# -----------------------------------------------------------------------------
# ! TRE COSE CHE IL CONFIGURE INCROCIATO NON PUO' SAPERE, E CHE DIAMO NOI
#
#   -std=gnu17    GNU make 4.2 e' del 2016 e dichiara ancora funzioni alla
#                 K&R (`extern char *getenv ();`). Il nostro gcc e' il 17 e
#                 come C23 legge `()` come `(void)`: la dichiarazione va in
#                 conflitto con quella vera e la compilazione si ferma su
#                 getenv, che non c'entra niente. Non e' un problema di
#                 EX-OS ed e' giusto che non stia in applica.py: e' uno
#                 sfasamento fra un sorgente e uno standard piu' nuovo di
#                 lui, e si risolve dicendo al compilatore in che anno
#                 leggere.
#
#   -DNO_OUTPUT_SYNC
#                 la sincronizzazione dell'output fra lavori paralleli
#                 (--output-sync) e' costruita sui LOCK DI RECORD di fcntl:
#                 struct flock, F_SETLKW. La fcntl di EX-OS conosce solo
#                 F_GETFL e F_SETFL, e i lock POSIX sui file sono un pezzo
#                 di kernel, non una riga da aggiungere. ! Con questo,
#                 `make --output-sync=target` SI RIFIUTA di partire invece di
#                 accettare l'opzione e ignorarla — vedi la modifica a main.c
#                 in applica.py.
#
#   --without-guile
#                 make sa incorporare l'interprete Guile per la funzione
#                 $(guile ...). Su EX-OS non c'e', e senza questo il
#                 configure lo cercherebbe con pkg-config sulla macchina che
#                 COSTRUISCE — trovando quello di Linux e provando a
#                 collegarlo dentro un binario per i386-exos.
#
# ! E UNA CHE INVECE SA GIA': fork(). Il configure incrociato lo dichiara
# assente da solo (AC_FUNC_FORK non prova a eseguire niente quando l'ospite
# non e' la macchina che costruisce, e i suoi test di compilazione falliscono
# perche' la libc di EX-OS non ha fork). Non serve nessuna variabile di cache:
# si controlla in config.h dopo il configure, e questo script lo fa.
#
# -----------------------------------------------------------------------------
# ! DOVE PRENDERE I SORGENTI. Questo script non scarica niente, per la
# stessa ragione scritta in tools/binutils-exos/prepara-binutils.sh: un
# download silenzioso dentro una build si scopre quando fallisce. Con Debian:
#
#     apt-get source make            # oppure
#     wget https://ftp.gnu.org/gnu/make/make-4.2.tar.gz && tar xf make-4.2.tar.gz
#
# Provato con GNU make 4.2.
# =============================================================================

set -e

RADICE=$(cd "$(dirname "$0")/../.." && pwd)
SORGENTI="${1:-$RADICE/make}"
USCITA="${2:-$HOME/exos-native/build-make}"
PREFISSO="${PREFISSO:-$HOME/exos-cross}"

if [ ! -f "$SORGENTI/job.c" ] || [ ! -f "$SORGENTI/configure" ]; then
    echo "prepara-make: '$SORGENTI' non e' un albero di GNU make" >&2
    echo "  vedi il commento in testa a questo script per dove prenderlo" >&2
    exit 1
fi
SORGENTI=$(cd "$SORGENTI" && pwd)

if [ ! -x "$PREFISSO/bin/i386-exos-gcc" ]; then
    echo "prepara-make: manca il cross i386-exos-gcc" >&2
    echo "  si prepara con tools/gcc-exos/prepara-cross.sh" >&2
    exit 1
fi

PATH="$PREFISSO/bin:$PATH"
export PATH

echo "=== GNU make per EX-OS ==="
echo "  sorgenti : $SORGENTI"
echo "  uscita   : $USCITA"
echo

# --- 1. il bersaglio dentro l'albero -----------------------------------------
python3 "$RADICE/tools/make-exos/applica.py" "$SORGENTI"

# --- 2. configure incrociato --------------------------------------------------
#
# ! SI COSTRUISCE FUORI DALL'ALBERO. Il repository di EX-OS contiene i
# sorgenti di make; riempirlo di oggetti e di config.h vorrebbe dire che
# `git status` non distingue piu' una modifica da un residuo di build.
echo
echo "=== configure ==="
mkdir -p "$USCITA"
cd "$USCITA"

# ! `sh config.guess` e non `./config.guess`: nel pacchetto sorgente il bit
# di esecuzione puo' non esserci (dipende da come e' stato scompattato), e
# senza la `sh` davanti il comando fallisce, --build resta vuoto e autoconf
# tira a indovinare. Indovina bene, di solito — ed e' proprio per questo che
# l'errore passerebbe inosservato.
"$SORGENTI/configure" \
    --host=i386-exos \
    --build="$(sh "$SORGENTI/config/config.guess")" \
    --prefix=/exos \
    --disable-nls \
    --without-guile \
    CC=i386-exos-gcc \
    CFLAGS="-O2 -std=gnu17" \
    CPPFLAGS="-DNO_OUTPUT_SYNC" \
    > configure.log 2>&1 || { tail -20 configure.log >&2; exit 1; }

# ! SI CONTROLLA CHE fork() SIA DICHIARATO ASSENTE, e si controlla QUI.
# Se un giorno il configure lo trovasse presente — per un cambio nella libc,
# o per una variabile di cache lasciata in giro — job.c prenderebbe il ramo
# POSIX con vfork() dentro, e il fallimento arriverebbe al PRIMO COMANDO che
# make prova a lanciare: «Segmentation fault» dentro EX-OS, che manda a
# cercare il guasto nel kernel. Meglio dirlo adesso.
if grep -q '^#define HAVE_FORK' config.h; then
    echo "prepara-make: il configure crede che EX-OS abbia fork()." >&2
    echo "  job.c prenderebbe il ramo con vfork() e make morirebbe al primo" >&2
    echo "  comando lanciato. Controllare config.log." >&2
    exit 1
fi
echo "  fork() dichiarato assente: si usa il ramo con spawn_ex"

# --- 3. costruzione -----------------------------------------------------------
echo
echo "=== costruzione ==="
make -j"${J:-2}" > build.log 2>&1 || { tail -30 build.log >&2; exit 1; }

if [ ! -x "$USCITA/make" ]; then
    echo "prepara-make: la costruzione non ha prodotto $USCITA/make" >&2
    exit 1
fi

# --- 4. la prova che e' per il bersaglio giusto -------------------------------
#
# ! NON E' UNA FORMALITA'. Un configure incrociato che ricade sul compilatore
# di sistema produce un binario che gira benissimo — su Linux — e se ne
# accorgerebbe solo chi prova ad avviarlo dentro EX-OS, dove il messaggio
# sarebbe «non e' un programma eseguibile».
if ! i386-exos-readelf -h "$USCITA/make" | grep -q "Intel 80386"; then
    echo "prepara-make: $USCITA/make non e' un ELF per i386" >&2
    exit 1
fi

# Quanto pesa con e senza i simboli di debug: sul CD ci va senza (lo fa la
# regola dell'ISO), e sapere le due misure evita di doverle andare a cercare
# per capire quale delle due si sta guardando.
i386-exos-strip -o "$USCITA/make.stripped" "$USCITA/make"
echo
echo "  make: $(du -h "$USCITA/make" | cut -f1) con i simboli,"
echo "        $(du -h "$USCITA/make.stripped" | cut -f1) senza (e' cosi' che va sul CD)"
echo
echo "[OK] GNU make per EX-OS: $USCITA/make"
echo "Finisce sul CD degli strumenti con \`make iso\`, in /exos/bin/make."

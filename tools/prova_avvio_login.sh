#!/bin/bash
# =============================================================================
# tools/prova_avvio_login.sh — la pagina d'accesso non si fa coprire
#
# Prova quel che e' stato chiesto il 22 settembre 2026 (@AVVIO-LOGIN): che i
# messaggi dell'avvio non arrivino SOPRA la schermata d'accesso.
#
# ! LA PROVA SI PAGA UN RITARDATARIO, e senza non proverebbe niente. Su questa
# macchina l'ultimo messaggio dell'avvio — `automount` — arriva un istante
# PRIMA che la pagina venga stampata anche con il difetto presente: una prova
# che si limitasse a guardare l'ordine direbbe «a posto» su un sistema rotto.
# Percio' si aggiunge in fondo a /boot/avvio.sh una riga che parla in ritardo:
#
#     sh /boot/rit.sh &     con dentro  sleep 900  ed  echo RITARDATARIO
#
# 900 ms e' scelto apposta in mezzo: piu' del ritardo naturale dei driver, meno
# dell'attesa di login (ATTESA_MS, 1500 ms in bin/login/login.c). Con il
# difetto RITARDATARIO cade dentro la riga «utente:»; senza, arriva prima e la
# pagina resta intera.
#
# ! E SI PROVA SU UN SISTEMA INSTALLATO DAL CD. Da CD `login` non gira nemmeno
# — il kernel lancia la shell — e un disco installato dal FLOPPY non ha i
# driver di rete, cioe' non ha l'avvio che si vuole provare.
#
# ! IL DISCO SI RIFA' SOLO SE MANCA: costa qualche minuto in QEMU. Per rifarlo
# da capo basta cancellarlo.
#
#     tools/prova_avvio_login.sh [directory-di-lavoro]   (default /tmp/exos-log)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-log}"
mkdir -p "$D"
IMG="$D/hd-login.img"

[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }

export EXOS_ISTANZA=log

# --- 1. Il disco, installato dal CD ------------------------------------------
if [ ! -f "$IMG" ]; then
    echo "=== 1. disco installato dal CD ($IMG) — qualche minuto ==="
    sed "s|^IMG=dischi/hd.img|IMG=$IMG|; s|^MB=\"\${1:-512}\"|MB=\"\${1:-256}\"|; \
         s|/tmp/exos-mkhd.log|$D/mkhd.log|g" tools/mkhd.sh > "$D/mkhd.sh"
    EXOS_SUPPORTO=cd bash "$D/mkhd.sh" > "$D/1-disco.log" 2>&1 || {
        echo "NON RIUSCITO: il disco non si e' installato (vedi $D/1-disco.log)"
        exit 1
    }
else
    echo "=== 1. disco gia' pronto ($IMG) ==="
fi

export EXOS_NO_FLOPPY=1
export EXOS_MARCA="utente:"
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

# --- 2. Il ritardatario ------------------------------------------------------
#
# ! `keymap us` PER PRIMO: il disco e' installato in italiano e qemu_drive
# batte scancode americani. Senza, le virgolette e la «&» di queste righe
# diventano altri caratteri e il file esce storto.
echo "=== 2. una riga che parla in ritardo, in fondo a /boot/avvio.sh ==="
timeout 400 python3 tools/qemu_drive.py \
    "root@2" "root@5" "keymap us@3" \
    "echo sleep 900 > /boot/rit.sh@2" \
    "echo echo RITARDATARIO >> /boot/rit.sh@2" \
    "echo \"sh /boot/rit.sh &\" >> /boot/avvio.sh@2" \
    "cat /boot/rit.sh@3" > "$D/2-ritardatario.log" 2>&1

grep -q "echo RITARDATARIO" "$D/2-ritardatario.log" || {
    echo "NON RIUSCITO: il ritardatario non si e' scritto (vedi $D/2-ritardatario.log)"
    exit 1
}

# --- 3. Si riavvia, e si guarda l'ORDINE -------------------------------------
echo "=== 3. riavvio: chi parla per ultimo? ==="
timeout 400 python3 tools/qemu_drive.py "foto:$D/3-accesso.ppm@3" \
    > "$D/3-avvio.log" 2>&1
cp "/tmp/exos/serial${EXOS_ISTANZA}.txt" "$D/3-seriale.txt"

# --- Il verdetto -------------------------------------------------------------
#
# La pagina e' l'ULTIMA cosa stampata: la riga «EX-OS - accesso» deve venire
# DOPO l'ultimo RITARDATARIO. Si contano le righe, che e' un numero e non
# un'impressione.
rit=$(grep -n "RITARDATARIO" "$D/3-seriale.txt" | tail -1 | cut -d: -f1)
pag=$(grep -n "EX-OS - accesso" "$D/3-seriale.txt" | tail -1 | cut -d: -f1)

echo ""
if [ -z "$rit" ] || [ -z "$pag" ]; then
    echo "  [NO]  non trovo il ritardatario o la pagina nel registro"
    echo "        (guarda $D/3-seriale.txt)"
    exit 1
fi

if [ "$pag" -gt "$rit" ]; then
    echo "  [OK]  la pagina d'accesso e' l'ultima cosa sullo schermo"
    echo "        (RITARDATARIO alla riga $rit, la pagina alla $pag)"
    esito=0
else
    echo "  [NO]  il ritardatario e' finito SOPRA la pagina d'accesso"
    echo "        (la pagina alla riga $pag, RITARDATARIO alla $rit)"
    echo "        e' il difetto di @AVVIO-LOGIN: vedi aspetta_che_taccia()"
    echo "        in bin/login/login.c"
    esito=1
fi

echo ""
echo "  La fotografia: $D/3-accesso.ppm — tools/ppm2png.py la converte."
exit $esito

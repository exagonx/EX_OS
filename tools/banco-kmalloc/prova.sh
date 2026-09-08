#!/bin/sh
# =============================================================================
# tools/banco-kmalloc/prova.sh
#
# ! STA QUI E NON IN tools/locali/, che e' ignorata da git: questo non e' un
# attrezzo che si riscrive in un minuto, e' la PROVA di una correzione. Un
# `git clean -xdf` se lo porterebbe via, e con lui l'unico modo di rifare a
# comando un difetto che dal vivo si vede una volta su quattordici.
#
# Mette alla prova kernel/mm/kmalloc.c SULL'OSPITE, senza QEMU e senza
# aspettare che il caso si ripeta. La domanda e' una sola: quando lo heap e'
# fatto di tratti NON attaccati — che e' sempre, perche' ogni espansione
# chiede le pagine da capo a pmm_alloc_pages_kernel — kfree legge fuori?
#
#     tools/banco-kmalloc/prova.sh              la versione dell'albero
#     tools/banco-kmalloc/prova.sh altro.c      una versione qualunque
#
# ! SERVE gcc -m32 (su Debian: gcc-multilib). Senza, non parte e lo dice.
#
# COSA STAMPA, E COME SI LEGGE
#
#   «esito 139» = SIGSEGV dentro kfree. E' la stessa cosa del «Page Fault non
#   gestito in ring0» di @DIF-PANIC: la pagina dopo ogni tratto qui e'
#   PROT_NONE, dentro EX-OS in cima alla RAM non e' mappata.
#
#   «Blocchi lib.» dopo aver liberato tutto DAL PRIMO dice quanto heap si
#   rimette insieme: uno per regione e' il massimo possibile, quaranta e passa
#   vuol dire che la coalescenza all'indietro non ha funzionato.
#
#   Il caso 5 e' il CANARINO, ed e' la parte di @DIF-PANIC rimasta aperta fino
#   all'8 settembre 2026: l'intestazione rotta l'aveva scritta la lettura fuori
#   regione, o qualcuno che scrive oltre il proprio blocco? Adesso, se e'
#   qualcuno, lo si sa mentre succede — e si sa CHI, perche' ogni blocco si
#   porta dietro l'indirizzo di chi l'ha allocato.
# =============================================================================
set -e
RADICE=$(cd "$(dirname "$0")/../.." && pwd)
BANCO="$(dirname "$0")"
SORGENTE="${1:-$RADICE/kernel/mm/kmalloc.c}"

echo 'int main(void){return 0;}' > /tmp/banco-m32-$$.c
if ! gcc -m32 /tmp/banco-m32-$$.c -o /tmp/banco-m32-$$ 2>/dev/null; then
    rm -f /tmp/banco-m32-$$.c
    echo "banco: serve gcc -m32 (pacchetto gcc-multilib)" >&2
    exit 1
fi
rm -f /tmp/banco-m32-$$.c /tmp/banco-m32-$$

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cp "$SORGENTE" "$TMP/kmalloc.c"
gcc -m32 -O1 -g -w -I "$BANCO/finti" -I "$TMP" "$BANCO/banco.c" -o "$TMP/banco"

echo "=== 1. si libera DALL'ULTIMO, e la pagina dopo il tratto e' cieca ==="
echo "    (qui esce la coalescenza in avanti sul bordo)"
"$TMP/banco" || echo ">>> esito $?"

echo
echo "=== 2. si libera DAL PRIMO, e la pagina dopo il tratto e' cieca ==="
echo "    (qui esce la scansione all'indietro, che partiva da g_heap_start)"
BANCO_ORDINE=primo "$TMP/banco" || echo ">>> esito $?"

echo
echo "=== 3. lo stesso, ma con le guardie LEGGIBILI: quanto heap si rimette insieme ==="
BANCO_CIECA_LEGGIBILE=1 BANCO_ORDINE=primo "$TMP/banco" -v 2>&1 | tail -8

echo
echo "=== 4. @DIF-PANIC alla lettera: un'intestazione con una misura inventata ==="
echo "    (prima del 7 settembre 2026: page fault in ring0 dentro kfree)"
BANCO_CIECA_LEGGIBILE=1 BANCO_MODO=rotta "$TMP/banco" -v 2>&1 | tail -12 || echo ">>> esito $?"

echo
echo "=== 5. IL CANARINO: qualcuno scrive UN byte oltre quel che ha chiesto ==="
echo "    (senza canarino non se ne accorge nessuno: il byte finisce"
echo "     nell'arrotondamento a otto e non tocca nessuna intestazione)"
BANCO_CIECA_LEGGIBILE=1 BANCO_MODO=oltre "$TMP/banco" -v 2>&1 \
    | grep -E "scrivo|OLTRE LA FINE|controllo completo|controllati|canarini" \
    || echo ">>> esito $?"

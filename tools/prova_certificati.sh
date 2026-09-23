#!/bin/bash
# =============================================================================
# tools/prova_certificati.sh — quali siti https EX-OS rifiuta, e PERCHE'
#
# Segnalato il 23 settembre 2026: «molti siti in https non vengono accettati
# per via del certificato». Prima di cambiare qualcosa si misura: EX-OS in
# QEMU con la rete vera (il NAT di QEMU), e `scarica -i` su una serie di siti
# comuni. `scarica` passa dalla stessa strada del navigatore — exhttp, extls,
# excert — e quando rifiuta dice quale dei nove casi di excert_perche() e su
# quale anello.
#
# E per ogni sito si guarda dall'host, con openssl, cosa manda il server:
# quanti certificati, chi li ha emessi, con che chiave. E' la parte che dice
# se il guasto e' nostro (un algoritmo che non sappiamo verificare) o del
# server (una catena incompleta, che i navigatori grandi rattoppano da soli).
#
# Uso:  tools/prova_certificati.sh [dir] [sito...]
# Vuole il sistema costruito (make iso-exos) e la rete su questa macchina.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-cert}"
shift
mkdir -p "$D"

SITI=("$@")
[ ${#SITI[@]} -eq 0 ] && SITI=(
    www.google.com www.wikipedia.org github.com duckduckgo.com
    www.debian.org www.kernel.org www.mozilla.org letsencrypt.org
    www.cloudflare.com stackoverflow.com www.reddit.com www.bbc.co.uk
    www.microsoft.com www.apple.com www.amazon.it www.ebay.it www.paypal.com
    www.repubblica.it www.corriere.it www.ansa.it www.ilpost.it
    www.gazzetta.it www.libero.it www.aruba.it www.poste.it www.tim.it
    www.agenziaentrate.gov.it www.inps.it www.istat.it www.salvatore-aranzulla.it
)

# --- Dentro EX-OS ------------------------------------------------------------
A=("date@2")          # la rete la accende gia' avvio.sh
for s in "${SITI[@]}"; do A+=("echo SITO $s@1" "scarica -i https://$s/@25"); done
A+=("echo FINE-SITI@2")

EXOS_ISTANZA=crt EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso EXOS_RAM=64M \
EXOS_QEMU_EXTRA="-netdev user,id=n1 -device ne2k_pci,netdev=n1" \
    timeout 3000 python3 tools/qemu_drive.py "${A[@]}" > "$D/exos.log" 2>&1

# --- Dall'host ----------------------------------------------------------------
: > "$D/host.txt"
for s in "${SITI[@]}"; do
    {
        echo "== $s"
        timeout 15 openssl s_client -connect "$s:443" -servername "$s" -showcerts \
            </dev/null 2>/dev/null |
            awk '/^ *[0-9]+ s:/{print} /^ *i:/{print} /a:PKEY/{print}'
    } >> "$D/host.txt"
done

# --- Il riassunto ---------------------------------------------------------------
python3 - "$D" <<'PY'
import re, sys
D = sys.argv[1]
t = open(D + "/exos.log", errors="replace").read()
t = re.sub(r"\x1b\[[0-9;]*m", "", t)
t = t.split("=== seriale dal prompt in poi ===")[-1]
sito, esiti, ordine = None, {}, []
for r in t.splitlines():
    r = r.strip()
    if r.startswith("SITO "):
        sito = r[5:].strip(); ordine.append(sito); esiti[sito] = []
    elif sito and r and not r.startswith("ex-os") and "ENTROPIA" not in r:
        esiti[sito].append(r)
buoni = 0
for s in ordine:
    righe = esiti[s]
    esito = next((r for r in righe if r.startswith("scarica:")), " / ".join(righe)[:110] or "(niente)")
    ok = re.match(r"scarica: [23]\d\d\b", esito) is not None
    buoni += ok
    print("%-4s %-30s %s" % ("si'" if ok else "NO", s, esito[:120]))
print("\n%d siti su %d aperti" % (buoni, len(ordine)))
PY
echo "(le catene viste dall'host sono in $D/host.txt)"

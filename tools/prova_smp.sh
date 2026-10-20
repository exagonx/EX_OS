#!/bin/bash
# =============================================================================
# tools/prova_smp.sh — i processori in piu' si trovano, si svegliano e restano
# in attesa col loro timer (7 ottobre 2026, tappe 1 e 2 dell'SMP, kernel 0.238)
#
# EX-OS si avvia in QEMU con 1, 2 e 4 processori (e con 4 sotto KVM, se c'e':
# li' i processori sono veri e partono davvero insieme). A ogni giro:
#
#   1. `hwinfo` dice quanti ne ha trovati il kernel: tanti quanti ne ha dati
#      QEMU;
#   2. quelli in piu' sono «IN ATTESA», tutti, e nessuno «MUTO»; il timer di
#      ognuno cammina e ognuno riceve i messaggi (tappa 2, kernel 0.238);
#   3. QEMU stesso conferma: i processori in piu' sono fermi (halted) e in
#      modo protetto con le pagine accese (CR0 = 8xxxxxxx), cioe' sono
#      arrivati in fondo al trampolino;
#   4. con un processore solo non cambia niente: «trovati 1»;
#      e senza ACPI (-machine acpi=off) l'elenco viene dalla tabella MP;
#   5. il sistema dopo lavora: la shell risponde, nessun [FAULT].
#
#     tools/prova_smp.sh [directory-di-lavoro]     (default /tmp/exos-smp)
#
# Variabili: PROVA_SMP_GIRI="1 2 4" per scegliere i giri.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-smp}"
mkdir -p "$D"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }

export EXOS_ISTANZA=smp
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
export EXOS_RAM="${EXOS_RAM:-64M}"
SER=/tmp/exos/serialsmp.txt

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

giro() {        # giro <processori> <opzioni di QEMU in piu'> <nome>
    local n=$1 extra=$2 nome=$3 L="$D/$3.txt" trovati fermi muti fermi_qemu pagine

    echo "=== $nome: $n processor$([ "$n" = 1 ] && echo e || echo i) ==="
    export EXOS_QEMU_EXTRA="-smp $n$extra"
    : > "$D/$nome-cpu.txt"
    timeout 300 python3 tools/qemu_drive.py \
        "hwinfo@12" "mona:$D/$nome-cpu.txt=info cpus@1" \
        "mona:$D/$nome-cpu.txt=info registers -a@1" \
        "mem@3" "echo FINE-SMP@2" \
        > "$D/$nome-qemu.log" 2>&1
    tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$L"

    trovati=$(sed -n 's/^ *trovati  *\([0-9][0-9]*\).*/\1/p' "$L" | head -1)
    fermi=$(grep -c "IN ATTESA - acceso" "$L")
    muti=$(grep -c "MUTO  *- svegliato" "$L")
    [ "$trovati" = "$n" ] && ok "hwinfo: trovati $n" \
                          || no "hwinfo dice «trovati ${trovati:-niente}», QEMU ne ha $n"
    [ "$fermi" = $((n - 1)) ] && [ "$muti" = 0 ] \
        && ok "$((n - 1)) in piu' accesi e in attesa, nessuno muto" \
        || no "in attesa $fermi (attesi $((n - 1))), muti $muti"
    # Tappa 2: ogni processore in attesa ha il suo timer che cammina e riceve
    # i messaggi mandati da dentro una chiamata di sistema.
    if [ "$n" -gt 1 ]; then
        [ "$(grep -c "timer .*: CAMMINA" "$L")" = $((n - 1)) ] \
            && ok "il timer di ognuno cammina (100 battiti al secondo)" \
            || no "timer: $(grep -c 'timer .*: CAMMINA' "$L") su $((n - 1)) camminano: $(grep -m1 'timer  ' "$L")"
        [ "$(grep -c "messaggi  *RICEVE " "$L")" = $((n - 1)) ] \
            && ok "ognuno riceve i messaggi (IPI) mandati da una chiamata di sistema" \
            || no "messaggi: $(grep -c 'messaggi  *RICEVE ' "$L") su $((n - 1)) li ricevono"
    fi
    grep -q "IN USO  *- e' quello su cui gira EX-OS" "$L" \
        && ok "il processore d'avvio e' riconosciuto" || no "nessuna voce «IN USO»"

    if [ "$n" -gt 1 ]; then
        # Lo dice anche QEMU: gli altri processori sono fermi, con le pagine.
        # ! FERMO (HLT=1) lo e' anche un processore mai svegliato: QEMU lo
        # tiene li' in attesa del SIPI. La prova vera e' CR0: chi ha percorso il
        # trampolino ha le pagine accese (bit 31), chi non e' mai partito ha
        # il CR0 del reset (60000010).
        fermi_qemu=$(grep -o "HLT=1" "$D/$nome-cpu.txt" | wc -l)
        pagine=$(grep -o "CR0=[0-9a-f]*" "$D/$nome-cpu.txt" | grep -c "CR0=[89a-f]")
        [ "$pagine" = "$n" ] \
            && ok "QEMU: tutti e $n i processori hanno le pagine accese (CR0)" \
            || no "QEMU: $pagine processori su $n con le pagine accese"
        [ "$fermi_qemu" -ge $((n - 1)) ] \
            && ok "QEMU: $fermi_qemu processori in hlt" \
            || no "QEMU vede $fermi_qemu processori fermi, attesi almeno $((n - 1))"
    fi
    grep -q "^FINE-SMP" "$L" \
        && ok "dopo, il sistema lavora (la shell risponde)" \
        || no "dopo l'avvio il sistema non risponde"
    grep -a -q "\[FAULT\]\|PANIC\|panic" "$L" && no "un [FAULT] o un panic sulla seriale ($L)" \
                                             || ok "nessun [FAULT]"
}

for n in ${PROVA_SMP_GIRI:-1 2 4}; do giro "$n" "" "cpu$n"; done

# Senza ACPI resta la tabella MP, quella delle schede a due processori degli
# anni Novanta: e' l'altra strada di smp.c, e va percorsa anche lei.
# ! DUE ZOCCOLI, NON DUE NUCLEI: la tabella MP di SeaBIOS elenca un processore
# per zoccolo (e' cosi' che la specifica del 1997 li intende), e da qualche
# versione `-smp 2` da solo vuol dire uno zoccolo con due nuclei.
giro 2 ",sockets=2,cores=1,threads=1 -machine acpi=off" "mp2"
grep -q "elenco da  *tabella MultiProcessor" "$D/mp2.txt" \
    && ok "senza ACPI l'elenco viene dalla tabella MP" \
    || no "senza ACPI l'elenco non viene dalla tabella MP: $(grep 'elenco da' "$D/mp2.txt")"
if [ -w /dev/kvm ]; then
    giro 4 " -enable-kvm" "kvm4"
else
    echo "        (/dev/kvm non c'e': salto il giro con i processori veri)"
fi

echo
[ $esito = 0 ] && echo "  prova_smp: TUTTO BENE" \
               || echo "  prova_smp: qualcosa non va (file in $D)"
exit $esito

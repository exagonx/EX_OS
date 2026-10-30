#!/bin/bash
# =============================================================================
# tools/prova_doppioclic.sh — il doppio clic e il «+» dell'albero, nei pixel
#
# Prova le tre cose che sono cambiate il 18 agosto 2026 nel file manager, e le
# prova in modo che si escludano a vicenda:
#
#   1. un clic SEMPLICE sul nome di una directory chiusa la SCEGLIE e basta
#      — le righe sotto restano quelle di prima;
#   2. un DOPPIO clic sullo stesso nome la APRE — sotto compaiono i figli;
#   3. un clic semplice sul SEGNO «+» la apre e la richiude lo stesso, senza
#      doppio clic, perche' quel segno e' li' per essere premuto;
#   4. e nel dialogo «Apri» — quello dell'editor, che e' lo stesso di ogni
#      programma — una directory scelta si apre con l'Invio E col doppio clic.
#
# ! LA PROVA STA NEL CONFRONTO, NON NELLA SINGOLA FOTOGRAFIA. «Si e' aperta»
# non e' un fatto osservabile in un'immagine sola: lo diventa guardando le
# righe SOTTO quella toccata prima e dopo. Percio' ogni passo si fotografa e
# tools/righe_lista.py conta l'inchiostro riga per riga.
#
# ! IL PUNTATORE SI PILOTA A PASSI PICCOLI, come in prova_ridimensiona.sh: si
# sbatte in un angolo con movimenti enormi (li' il troncamento non fa danno) e
# da li' si conta a passi di dieci.
#
# ! E I DUE CLIC DEL DOPPIO VANNO IN UN `mon:` SOLO, separati da «;». Mandati
# come quattro argomenti distinti costerebbero un quarto di secondo l'uno:
# arriverebbero a tre quarti di secondo di distanza, cioe' come due clic
# separati — e la prova direbbe «non funziona» provando un'altra cosa.
#
# ! UNA MACCHINA SOLA PER VOLTA.
#
# Vuole il sistema costruito in grafica:  make SVGA=800x600 && make iso-exos
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos}"
mkdir -p "$D"
pkill -9 -f qemu-system 2>/dev/null
sleep 2

# --- La geometria dell'albero, che sta nel sorgente del file manager ---------
#
# exwin/bin/filemgr/filemgr.c:  ex_crea("lista", ..., 4, MENU_H + 4, ALBERO_W, ...)
# lib/exwin/exwin.c:            le righe alte 16, il testo a +4 dal bordo
ALB_X=4;  ALB_Y=24;  ALB_W=240
RIGA_H=16; CAR_W=8

# Da (0,0) a (x,y) a passi di dieci: i movimenti grandi si troncano.
#
# ! SI VA IN DIAGONALE FINCHE' SI PUO', POI DIRITTI, e non «in diagonale per
# quanto e' alto e poi in orizzontale»: quando il bersaglio e' piu' in basso
# che a destra — un segno «+» vicino al bordo sinistro, per dirne una — la
# diagonale supera la x voluta e il resto viene NEGATIVO. Un ciclo bash che
# gira zero volte non se ne lamenta: il puntatore finisce quaranta pixel piu'
# in la', dentro il nome invece che sul segno, e la prova dice «il segno non
# funziona» avendo cliccato da un'altra parte. Costato un giro di prove.
passi_a() {
    local dx=$1 dy=$2 i d rx ry
    d=$(( (dx < dy ? dx : dy) / 10 ))
    for ((i = 0; i < d; i++)); do printf '%s\n' "mon:mouse_move 10 10@0"; done
    rx=$(( dx - d * 10 )); ry=$(( dy - d * 10 ))
    for ((i = 10; i <= rx; i += 10)); do printf '%s\n' "mon:mouse_move 10 0@0"; done
    for ((i = 10; i <= ry; i += 10)); do printf '%s\n' "mon:mouse_move 0 10@0"; done
    printf '%s\n' "mon:mouse_move $(( rx % 10 )) $(( ry % 10 ))@0"
}

casa() { local i; for ((i = 0; i < 5; i++)); do printf '%s\n' "mon:mouse_move -600 -600@0"; done; }

# --- Primo giro: aprire il file manager e guardare DOVE e' finito ------------
#
# La posizione la sceglie il server, a cascata: uno script che se la ricorda a
# memoria si rompe la prima volta che sulla scrivania c'e' una finestra in piu'.
{
    echo "exwin@12"
    echo "key:alt-f1@3"
    echo "/exwin/bin/filemgr &@8"
    echo "key:alt-f5@3"
    echo "foto:$D/dc_apert.ppm@2"
} > "$D/dc_args.txt"

mapfile -t A < "$D/dc_args.txt"
EXOS_ISTANZA=dc EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso \
    timeout 300 python3 tools/qemu_drive.py "${A[@]}" > "$D/dc0.log" 2>&1

CLIENT=$(python3 tools/misura_finestre.py --client "$D/dc_apert.ppm") || {
    echo "NON RIUSCITO: nella fotografia non c'e' nessuna barra del titolo attiva."
    echo "              (il file manager non si e' aperto: vedi $D/dc0.log)"
    exit 1
}
CX=${CLIENT% *}; CY=${CLIENT#* }
echo "=== l'area del client del file manager comincia in ($CX,$CY) ==="

# --- Secondo giro: i tre gesti, uno dopo l'altro -----------------------------
#
# ! LA DIRECTORY SU CUI SI PROVA DEVE AVERE DELLE FIGLIE, o la prova non prova
# niente. L'albero mostra SOLO directory: aprire /bin — che di file ne ha
# tanti e di directory nessuna — cambia il segno da «+» a «-» e nient'altro,
# cioe' qualche decina di pixel che si confondono col rumore. /exwin invece ha
# bin e lib dentro, e aprirla sposta in giu' tutto il resto dell'albero: quello
# si' che si vede.
#
# ! LA RIGA DI /exwin SI CERCA PER NOME (30 ottobre 2026): era la sesta, poi
# nell'albero sono arrivati rdboot e USB e la sesta e' diventata /boot, che
# di figlie non ne ha. Il clic lo porta `punta:` (tools/trova_testo.py); il
# segno sta due colonne prima del nome, cioe' 32 pixel a sinistra del centro
# di «exwin». La riga serve solo a leggere i risultati.
# ! SOLO NELL'ALBERO, la colonna di sinistra: «exwin» c'e' anche nell'elenco
# di destra, piu' in alto, e il primo dall'alto era quello.
ZONA_ALBERO="0,0,$(( CX + 200 )),600"
Y_EXWIN=$(python3 tools/trova_testo.py -z "$ZONA_ALBERO" "$D/dc_apert.ppm" exwin | cut -d' ' -f2)
[ -n "$Y_EXWIN" ] || { echo "NON RIUSCITO: nell'albero non c'e' «exwin» ($D/dc_apert.ppm)"; exit 1; }
RIGA_PROVA=$(( (Y_EXWIN - CY - ALB_Y - 2 + 8) / RIGA_H ))

{
    echo "exwin@12"
    echo "key:alt-f1@3"
    echo "/exwin/bin/filemgr &@8"
    echo "key:alt-f5@3"
    echo "foto:$D/dc_0_partenza.ppm@2"

    # 1. clic semplice sul NOME: sceglie e basta
    echo "punta:exwin|0,0,0,$ZONA_ALBERO@1"
    echo "mon:mouse_button 1;mouse_button 0@2"
    echo "foto:$D/dc_1_clic.ppm@2"

    # 2. DOPPIO clic sullo stesso punto: apre
    echo "mon:mouse_button 1;mouse_button 0;mouse_button 1;mouse_button 0@2"
    echo "foto:$D/dc_2_doppio.ppm@2"

    # 3. e un clic sul SEGNO la richiude, da solo: il segno adesso e' un «-»,
    #    e premerlo una volta sola deve bastare a chiudere quello che il
    #    doppio clic ha aperto.
    echo "punta:exwin|-32,0,0,$ZONA_ALBERO@1"
    echo "mon:mouse_button 1;mouse_button 0@2"
    echo "foto:$D/dc_3_segno.ppm@2"
} > "$D/dc_args.txt"

pkill -9 -f qemu-system 2>/dev/null
sleep 2
mapfile -t B < "$D/dc_args.txt"
EXOS_ISTANZA=dc EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso \
    timeout 400 python3 tools/qemu_drive.py "${B[@]}" > "$D/dc.log" 2>&1

LX=$(( CX + ALB_X )); LY=$(( CY + ALB_Y ))
for f in dc_0_partenza dc_1_clic dc_2_doppio dc_3_segno; do
    echo
    echo "--- $f"
    python3 tools/righe_lista.py "$D/$f.ppm" "$LX" "$LY" "$ALB_W" $(( RIGA_PROVA + 6 ))
done

echo
echo
echo "=== come si legge (la riga di prova e' la $RIGA_PROVA) ==="
echo "  0 -> 1  la barra della scelta si sposta sulla riga $RIGA_PROVA, e le righe"
echo "          sotto restano IDENTICHE: un clic semplice sceglie, non apre."
echo "  1 -> 2  sotto la riga $RIGA_PROVA compaiono le figlie, e cio' che c'era"
echo "          dopo scivola in giu': il doppio clic ha aperto."
echo "  2 -> 3  quelle righe spariscono di nuovo: il segno premuto UNA volta"
echo "          sola ha richiuso."

# =============================================================================
# PARTE 2 — il dialogo «Apri»
#
# ! LA SUA POSIZIONE NON SI CHIEDE ALLA FOTOGRAFIA, e qui e' giusto cosi': il
# dialogo non nasce a cascata come le finestre delle applicazioni, si mette in
# MEZZO allo schermo (vedi dialogo() in lib/exdlg/exdlg.c). Con 800x600 e un
# dialogo di 420x300 l'angolo del client e' (190,150), sempre.
#
# ! E SI APRE COI TASTI, non col mouse: Ctrl+O, la scorciatoia di «Apri...».
# Fino al 30 ottobre 2026 era F10, giu', Invio: il menu File ha avuto
# «Nuovo documento RTF» al secondo posto, e la prova apriva una scheda nuova.
# =============================================================================
DLG_X=190; DLG_Y=150
LST_X=$(( DLG_X + 6 )); LST_Y=$(( DLG_Y + 26 )); LST_W=408

punto_dlg() {   # RIGA COLONNA -> "x y"
    echo $(( LST_X + 4 + $2 * CAR_W + CAR_W / 2 )) \
         $(( LST_Y + 2 + $1 * RIGA_H + RIGA_H / 2 ))
}

P_D1=$(punto_dlg 1 6)
# Il pulsante «Su» del dialogo: sta in (6,2) dentro il client, 44x20.
P_SU=$(echo $(( DLG_X + 28 )) $(( DLG_Y + 12 )))

{
    echo "exwin@12"
    echo "key:alt-f1@3"
    echo "/exwin/bin/exeditor &@8"
    echo "key:alt-f5@3"
    echo "key:ctrl-o@4"
    echo "foto:$D/dl_0_aperto.ppm@2"

    # 1. clic semplice sulla riga 1, che e' una directory: sceglie e basta
    casa
    passi_a $P_D1
    echo "mon:mouse_button 1;mouse_button 0@2"
    echo "foto:$D/dl_1_clic.ppm@2"

    # 2. Invio: ci si entra
    echo "key:ret@3"
    echo "foto:$D/dl_2_invio.ppm@2"

    # 3. il pulsante «Su»: si torna alla radice, per riprovare col mouse
    casa
    passi_a $P_SU
    echo "mon:mouse_button 1;mouse_button 0@3"
    echo "foto:$D/dl_3_su.ppm@2"

    # 4. DOPPIO clic sulla stessa riga 1: ci si entra senza toccare i tasti
    casa
    passi_a $P_D1
    echo "mon:mouse_button 1;mouse_button 0;mouse_button 1;mouse_button 0@3"
    echo "foto:$D/dl_4_doppio.ppm@2"
} > "$D/dl_args.txt"

pkill -9 -x qemu-system-i386 2>/dev/null
sleep 2
mapfile -t C < "$D/dl_args.txt"
EXOS_ISTANZA=dl EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso \
    timeout 400 python3 tools/qemu_drive.py "${C[@]}" > "$D/dl.log" 2>&1

for f in dl_0_aperto dl_1_clic dl_2_invio dl_3_su dl_4_doppio; do
    echo
    echo "--- $f"
    python3 tools/righe_lista.py "$D/$f.ppm" "$LST_X" "$LST_Y" "$LST_W" 8
done

echo
echo "=== come si legge il dialogo ==="
echo "  0 -> 1  la barra della scelta passa alla riga 1: il clic sceglie e"
echo "          basta, l'elenco resta quello."
echo "  1 -> 2  l'elenco cambia TUTTO e la scelta torna alla riga 0:"
echo "          l'Invio e' entrato nella directory."
echo "  2 -> 3  l'elenco torna quello di partenza: «Su» e' risalito."
echo "  3 -> 4  l'elenco e' di nuovo quello del passo 2, e stavolta senza"
echo "          toccare la tastiera: il doppio clic e' entrato."

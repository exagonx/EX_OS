# =============================================================================
# /boot/fixsys.sh — rimettere in sesto il sistema installato sul disco
#
#     sh -f /boot/fixsys.sh
#
# Si lancia DA UN ALTRO SUPPORTO — il floppy o il CD — con il disco da riparare
# attaccato ma NON avviato. Monta la prima partizione, aggiorna il sistema di
# base con quello del supporto, e smonta.
#
# -----------------------------------------------------------------------------
# ! A CHE COSA SERVE, E PERCHE' ESISTE
#
# Un aggiornamento interrotto a meta' puo' lasciare sul disco i PROGRAMMI nuovi
# e la LIBC vecchia. I programmi di EX-OS chiamano la libc per NOME, quindi:
#
#     binario VECCHIO + libc NUOVA   ->  funziona
#     binario NUOVO   + libc VECCHIA ->  non parte NIENTE
#
# Nel secondo caso la macchina si accende, ha la rete a posto, e non ha un solo
# comando con cui rimediare: `scarica`, `dhcp`, `ipcfg`, `netupdate` rispondono
# tutti «la libreria condivisa non ha la funzione ...». E' successo il 15
# settembre 2026 su una macchina vera.
#
# Da un altro supporto, invece, i programmi e la libc sono quelli del supporto —
# coerenti fra loro per costruzione — e si puo' lavorare.
#
# ! NON E' UN PROGRAMMA NUOVO, E VOLUTAMENTE. `install -a` fa gia' la cosa
# giusta: confronta, elenca che cosa cambierebbe, e copia solo quello. Questo
# file mette in fila i tre passi perche' non si sbaglino nell'ordine o nel
# nome del dispositivo, non perche' servisse altro codice. Sul floppy, poi, un
# binario in piu' non ci starebbe: restano undicimila byte liberi.
#
# -----------------------------------------------------------------------------
# ! SE IL DISCO NON E' hd0p1
#
# Qui sotto c'e' scritto hd0p1, che e' il caso normale: primo disco, prima
# partizione. Se la macchina e' fatta in un altro modo, `disk` lo dice, e i tre
# comandi si battono a mano cambiando il nome:
#
#     disk                     elenca dischi e partizioni
#     mount hd1p2 /disk        monta quella giusta
#     install -a /disk         aggiorna il sistema di base
#     umount /disk
#
# ! E `/disk` NON DEVE ESISTERE sul supporto da cui si e' partiti: i punti di
# montaggio sono virtuali, e montare su un nome che esiste gia' viene rifiutato
# apposta — su Unix quel caso nasconde dei file senza dirlo.
#
# -----------------------------------------------------------------------------
# ! DOPO, FINIRE IL LAVORO
#
# `install -a` rimette il sistema MINIMALE, cioe' quello che sta sul floppy: la
# libc, la shell, i comandi di base. Tutto il resto — driver di rete, programmi
# di rete, ExWin — sul disco resta com'era, e a quel punto la macchina e'
# coerente e si aggiorna da sola:
#
#     netupdate -check
#
# =============================================================================

echo Riparazione del sistema installato.
echo Monto la prima partizione del primo disco.

mount hd0p1 /disk

echo Aggiorno il sistema di base. Elenca prima che cosa cambia.

install -a /disk

umount /disk

# ! LA FRASE FINALE NON PUO' DIRE «FATTO», e il motivo e' che questa shell non
# ha un «se il comando prima e' fallito». Lo script arriva qui comunque: se il
# disco non si e' montato, `install` lo ha detto e si e' fermato da solo (non
# scrive dove non c'e' un volume, dalla 0.005), ma queste righe vengono
# stampate lo stesso. Meglio una frase vera in tutt'e due i casi che un «fatto»
# che a volte mente.
echo
echo Se qui sopra install ha elencato e copiato i file, e' andata:
echo togli il dischetto e riavvia.
echo Se invece ha detto che non e' un volume montato, guarda con  disk
echo come si chiama la partizione e rifai i tre passi a mano.
echo
echo Dopo il riavvio, dal sistema:  netupdate -check

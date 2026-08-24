# =============================================================================
# /boot/avvio.sh — i comandi del SISTEMA, non di chi entra
#
# Lo esegue `login`, PRIMA dell'accesso, da root e sulla sola console 0, e
# aspetta che finisca prima di mostrare il prompt.
#
# ! LE DUE PAROLE CHE LO DISTINGUONO DALL'AUTOEXEC SONO «PRIMA» E «ROOT».
# L'autoexec lo esegue la shell della prima console: nasce a OGNI accesso e con
# l'identita' di CHI ENTRA — quindi i driver si riaccendevano a ogni rientro, e
# non si accendevano affatto se il primo a entrare non era root. Qui invece si
# e' soli e si e' root, una volta per avvio.
#
# ! E L'ORDINE E' QUELLO DELLE RIGHE, che e' il motivo per cui la rete sta qui
# e non in [modules] di kernel.cfg: le voci di [modules] il kernel le avvia
# TUTTE INSIEME, e ognuna deve poi stare ad aspettare il proprio fornitore. Su
# una macchina lenta quelle attese scadono e la rete resta spenta — il difetto
# peggiore da cercare, perche' al riavvio dopo funziona.
#
# La sintassi e' quella dell'autoexec: una riga = un comando, '&' per il
# background, '#' commento, '@' esegue una riga senza stamparla, !silenced e
# !verbose accendono e spengono l'eco dei comandi.
#
# Se una riga qui dentro si blocca: Alt+F2 da' una shell pulita, e cancellare
# questo file lo salta del tutto.
#
# Questo file sta sul CD di EX-OS, che e' di sola lettura: su un sistema
# installato lo riscrive `hwconfig`, con il driver della scheda che c'e'.
# =============================================================================

!silenced

echo Accendo la rete...

# L'ordine non e' modificabile: ogni passo serve al successivo.
# `netdetect -c` sceglie da solo il driver giusto per la scheda e aspetta
# che registri il proprio servizio.
/dev/pci.drv &
netdetect -c
/dev/ip.drv &

# Un indirizzo dal DHCP, se c'e' un server. Senza, restano i valori
# predefiniti dello stack e la rete locale funziona lo stesso.
# Quello che stampa — l'indirizzo ottenuto — si vede: e' il risultato,
# non il comando.
dhcp

echo Rete pronta: 'ipcfg' mostra la configurazione, 'ping' la prova.

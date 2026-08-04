# =============================================================================
# /boot/autoexec.sh — comandi eseguiti dalla shell all'avvio
#
# Una riga = un comando, esattamente come se fosse digitato: stessi
# built-in, stesse virgolette, stesso '&' per il background.
#
#   #           riga di commento
#   @           esegue UNA riga senza stamparla
#   !silenced   da qui in poi i comandi non si vedono piu'
#   !verbose    si tornano a vedere
#
# !silenced e' l'`echo off` di autoexec.bat: zittisce il COMANDO, non il
# suo risultato. E' la distinzione che lo rende utile — quello che un
# comando stampa e' il motivo per cui lo si e' messo qui, mentre la riga
# di comando la si e' gia' scritta.
#
# ATTENZIONE: lo esegue SOLO la shell della prima console. Le altre tre
# (Alt+F2, Alt+F3, Alt+F4) partono pulite — ed e' la via d'uscita se un
# comando qui dentro si blocca. L'altra e' `autoexec=0` in kernel.cfg.
#
# Questo file sta sul CD di EX-OS, che e' di sola lettura: per cambiarlo
# su un sistema installato si modifica quello sul disco.
# =============================================================================

!silenced

echo Avvio automatico: accendo la rete...

# L'ordine non e' modificabile: ogni passo serve al successivo.
# `netdetect -c` aspetta da solo che il driver registri il servizio.
/dev/pci.drv &
netdetect -c
/dev/ip.drv &

# Un indirizzo dal DHCP, se c'e' un server. Senza, restano i valori
# predefiniti dello stack e la rete locale funziona lo stesso.
# Quello che stampa — l'indirizzo ottenuto — si vede: e' il risultato,
# non il comando.
dhcp

echo Rete pronta. `ipcfg` mostra la configurazione, `ping` la prova.

# =============================================================================
# /boot/autoexec.sh — comandi eseguiti dalla shell all'avvio
#
# Una riga = un comando, esattamente come se fosse digitato: stessi
# built-in, stesse virgolette, stesso '&' per il background.
#
#   #           riga di commento
#   @           esegue UNA riga senza stamparla
#   !silenced

# ! LA RETE NON STA PIU' QUI, dal 24 agosto 2026: sta in /boot/avvio.sh, che
# `login` esegue da root PRIMA dell'accesso. Questo file lo esegue la shell di
# chi entra, e un utente normale un driver non lo carica.
#
# Qui dentro vanno le cose di CHI USA la macchina: un alias, una variabile,
# un programma da aprire appena si e' dentro.

@echo Sistema pronto.

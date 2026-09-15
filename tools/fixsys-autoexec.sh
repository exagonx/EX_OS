# =============================================================================
# L'autoexec del dischetto di SOCCORSO (dist/fixsys.img)
#
# ! NON E' L'autoexec DEL SISTEMA, ed e' un file a parte apposta: chi avvia
# questo dischetto ha gia' un guaio fra le mani, e la prima cosa che deve
# leggere e' che cosa battere — non «Sistema pronto».
#
# ! E NELLE RIGHE ESEGUITE NON CI VA UN SOLO APOSTROFO, che e' la ragione per
# cui questo testo e' scritto in un italiano cosi' asciutto. La shell tratta
# l'apostrofo come APICE DI QUOTATURA — in /boot/avvio.sh si legge infatti
# `echo Rete pronta: 'ipcfg' mostra la configurazione`, a coppie — quindi un
# «e'» da solo apre una quotatura che non si chiude piu' e il dischetto
# accoglie con «sh: manca la apice di chiusura». Visto su una macchina vera il
# 15 settembre 2026. Nei COMMENTI invece gli apostrofi vanno benissimo: le
# righe che cominciano per # la shell non le esegue.
# =============================================================================

!silenced

@echo
@echo ===========================================================
@echo   EX-OS - DISCHETTO DI SOCCORSO
@echo ===========================================================
@echo
@echo   Serve quando un aggiornamento si interrompe a meta e sul
@echo   disco restano i programmi nuovi con la libc vecchia: in
@echo   quel caso la macchina si accende e nessun comando parte.
@echo
@echo   Per ripararla:
@echo
@echo       sh -f /boot/fixsys.sh
@echo
@echo   Fa tre cose: monta hd0p1 su /disk, ci rimette il sistema
@echo   di base con install -a, che prima elenca cosa cambia, e
@echo   poi smonta. Alla fine togli il dischetto e riavvia.
@echo
@echo   Se la partizione non si chiama hd0p1, guarda con  disk
@echo   e rifai i tre passi a mano col nome giusto.
@echo

# =============================================================================
# L'autoexec del dischetto di SOCCORSO (dist/fixsys.img)
#
# ! NON E' L'autoexec DEL SISTEMA, ed e' un file a parte apposta: chi avvia
# questo dischetto ha gia' un guaio fra le mani, e la prima cosa che deve
# leggere e' che cosa battere — non «Sistema pronto».
# =============================================================================

!silenced

@echo
@echo ===========================================================
@echo   EX-OS - DISCHETTO DI SOCCORSO
@echo ===========================================================
@echo
@echo   Serve quando un aggiornamento si e' interrotto e sul
@echo   disco i programmi sono nuovi ma la libc e' vecchia: in
@echo   quel caso la' non parte piu' nessun comando.
@echo
@echo   Per ripararlo:
@echo
@echo       sh -f /boot/fixsys.sh
@echo
@echo   Fa tre cose: monta hd0p1 su /disk, ci rimette il sistema
@echo   di base con  install -a  (che prima elenca cosa cambia),
@echo   e smonta. Poi si toglie il dischetto e si riavvia.
@echo
@echo   Se la partizione non e' hd0p1, guarda con  disk  e fai i
@echo   tre passi a mano cambiando il nome.
@echo

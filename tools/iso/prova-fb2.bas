' =============================================================================
' tools/iso/prova-fb2.bas
' EX-OS — Extensible Operating System
'
' Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
' SPDX-License-Identifier: GPL-2.0-or-later
' =============================================================================
'
' Il gemello di prova-fb.bas CON un #include, da compilare dentro EX-OS:
'
'     cd /src
'     /cdrom/exos/bin/fbc prova-fb2.bas
'
' -----------------------------------------------------------------------------
' ⚠️ PERCHE' NON BASTAVA prova-fb.bas
'
' Quello non ha un solo #include, apposta: risponde a una domanda sola —
' «fbc compila?» — e a un fallimento si sa a cosa ha risposto no. Ma un
' compilatore che compila solo cio' che non include niente non serve, ed e'
' esattamente la meta' che mancava: fino al 6 agosto 2026 i .bi di
' FreeBASIC non stavano proprio sul CD.
'
' Questo file fa la domanda che quello evita, e la fa alla catena intera:
'
'   - `#include "crt.bi"` prova la RICERCA DEGLI HEADER. fbc la fa da solo,
'     senza opzioni: calcola il prefisso da dove sta lui (<prefisso>/bin/fbc
'     -> <prefisso>/include/freebasic) e cerca li'. Se il .bi non c'e', si
'     ferma qui e lo dice.
'
'   - `strlen` viene da crt.bi, cioe' e' la NOSTRA libc vista da FreeBASIC:
'     se la trova e risponde giusto, i due mondi si parlano davvero.
'
'   - `print` e la concatenazione di stringhe sono la runtime di FreeBASIC,
'     cioe' libfb.a: quella il compilatore deve trovarla per conto proprio
'     in <prefisso>/lib/freebasic/<target>/.
'
' ⚠️ I VALORI SONO NOTI IN ANTICIPO, come in prova-gcc.c: 385 e' la somma
' dei quadrati da 1 a 10 e 5 e' la lunghezza di "EX-OS". Un programma che
' stampa un numero senza che nessuno sappia quale fosse quello giusto non
' prova niente.
'
' Uscita 0 se tutto torna, 1 se un valore non torna: cosi' la prova si legge
' anche dal codice di uscita, senza rileggere lo schermo.
' =============================================================================

#include "crt.bi"

dim as zstring * 16 nome = "EX-OS"

' strlen: arriva da crt.bi, cioe' dalla libc di EX-OS.
dim as long lung = strlen(@nome)

' Un conto che il compilatore non puo' precalcolare tutto in testa.
dim as long somma = 0
for i as long = 1 to 10
    somma += i * i
next

' La runtime di FreeBASIC: stringhe dinamiche, concatenazione, print.
dim as string saluto = "Gli header di " + "FreeBASIC" + " dentro " + nome

print saluto
print
print "  strlen da crt.bi   :"; lung; "   (atteso 5)"
print "  somma dei quadrati :"; somma; "   (atteso 385)"
print "  lunghezza stringa  :"; len(saluto); "   (atteso 36)"

if lung <> 5 or somma <> 385 or len(saluto) <> 36 then
    print
    print "Un valore non torna: la catena ha prodotto un programma"
    print "che si collega ma calcola male."
    end 1
end if

print
print "Compilato, assemblato e collegato qui dentro."
end 0

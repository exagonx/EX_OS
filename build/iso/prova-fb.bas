' =============================================================================
' tools/iso/prova-fb.bas
' EX-OS — Extensible Operating System
'
' Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
' SPDX-License-Identifier: GPL-2.0-or-later
' =============================================================================
'
' Il programma piu' piccolo che dica qualcosa a fbc, da compilare DENTRO
' EX-OS:
'
'     fbc /prova-fb.bas
'
' ⚠️ NIENTE #include, PER LA STESSA RAGIONE di tools/iso/prova-cc1.c: un
' solo include farebbe due domande insieme — «fbc funziona?» e «fbc trova
' gli header al posto giusto?» — e a un fallimento non si saprebbe a quale
' delle due ha risposto no.
'
' Cosa c'e' dentro, e perche' proprio questo: le poche cose che si rompono
' per prime se manca un pezzo della runtime.
'
'   - PRINT di una stringa      fb_PrintString, cioe' la console
'   - una STRING concatenata    l'allocatore dei descrittori temporanei
'   - un ciclo con LONGINT      l'aritmetica a 64 bit, che passa da libgcc
'   - una FUNCTION              lo stack frame e il valore di ritorno
' =============================================================================

function somma_quadrati( byval n as integer ) as longint
    dim as longint s = 0
    for i as integer = 1 to n
        s += cast(longint, i) * i
    next
    return s
end function

dim as string saluto = "EX-OS" + " e " + "FreeBASIC"

print "prova-fb — compilato dentro EX-OS"
print "  saluto      "; saluto
print "  quadrati    "; somma_quadrati(10)

' 385 e' la somma dei quadrati da 1 a 10: un valore che si controlla a
' mano, non un "ha stampato qualcosa".
if somma_quadrati(10) = 385 then
    print "  esito       tutto a posto"
    end 0
else
    print "  esito       ARITMETICA SBAGLIATA"
    end 1
end if

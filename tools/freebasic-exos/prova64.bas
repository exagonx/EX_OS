' tools/freebasic-exos/prova64.bas - the first FreeBASIC program for 64-bit
' EX-OS (@FB-64): strings, integers, floating point, arrays, a file, sizes.
Dim As Integer i, somma
Dim As String s
Dim a(1 To 10) As Double

Print "FreeBASIC su EX-OS a 64 bit"
Print "SizeOf(Integer) ="; SizeOf(Integer); "  SizeOf(Any Ptr) ="; SizeOf(Any Ptr)
For i = 1 To 10
    somma += i * i
    a(i) = Sqr(i)
Next
Print "somma dei quadrati 1..10 ="; somma
Print "radice di 10 ="; a(10)
s = "Exa" & "OS"
Print "stringa: "; s; " lunga"; Len(s); " al contrario: ";
For i = Len(s) To 1 Step -1
    Print Mid(s, i, 1);
Next
Print
Open ExePath() & "/prova64.txt" For Output As #1
Print #1, "riga scritta da FreeBASIC"
Close #1
Open ExePath() & "/prova64.txt" For Input As #1
Line Input #1, s
Close #1
Print "letto dal file: "; s
Print "FINE"

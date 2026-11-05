# tools/prova-sh/controlli.sh - what sh 0.010 learned, checked from inside
# EX-OS (tools/prova-sh/prova-sh.sh puts it on a disk and runs it).
# Every check prints [OK] or [NO]; the host counts them.
!silenced
X=uno
if [ "$X" = uno ]; then echo "[OK] 1 variabile e if su una riga"; else echo "[NO] 1 variabile e if"; fi
if [ "${X}" != uno ]
then
    echo "[NO] 2 if su piu' righe"
elif [ "$X" = uno ]
then
    echo "[OK] 2 if su piu' righe, elif"
else
    echo "[NO] 2 else"
fi
if false; then echo "[NO] 3 false"; elif false; then echo "[NO] 3 elif"; else echo "[OK] 3 else"; fi
if true; then
    if [ 2 -lt 10 ]; then echo "[OK] 4 if annidato, -lt sui numeri"; else echo "[NO] 4 -lt"; fi
else
    if true; then echo "[NO] 4 ramo spento eseguito"; fi
fi
if false; then
    exit 9
fi
echo "[OK] 5 exit in un ramo spento non esce"
false
if [ $? -eq 1 ]; then echo "[OK] 6 \$? dopo false"; else echo "[NO] 6 \$?"; fi
/bin/sh -c "exit 42"
if [ $? = 42 ]; then echo "[OK] 7 exit con due cifre"; else echo "[NO] 7 exit 42 rende $?"; fi
false && echo "[NO] 8 &&" || echo "[OK] 8 && e ||"
if ! false; then echo "[OK] 9 ! nega"; else echo "[NO] 9 !"; fi
if [ "${NESSUNA:-predefinito}" = predefinito ]; then echo "[OK] 10 \${NOME:-altro}"; else echo "[NO] 10"; fi
Y='a|b>c;d&e'
if [ "$Y" = 'a|b>c;d&e' ]; then echo "[OK] 11 un valore con | > ; & resta una parola"; else echo "[NO] 11 [$Y]"; fi
Z='x"y'
if [ "$Z" = 'x"y' ]; then echo "[OK] 12 una virgoletta nel valore"; else echo "[NO] 12 [$Z]"; fi
echo '$X' > /a/apici.txt
if [ -s /a/apici.txt ]; then echo "[OK] 13 test -s"; else echo "[NO] 13 -s"; fi
cat /a/apici.txt
if [ -f /a/controlli.sh ] && [ -d /a ] && ! [ -e /a/non-c-e ]; then echo "[OK] 14 -f -d -e"; else echo "[NO] 14 -f -d -e"; fi
if [ -x /bin/ls ]; then echo "[OK] 15 -x"; else echo "[NO] 15 -x /bin/ls"; fi
if [ -z "" ] && [ -n "$X" ]; then echo "[OK] 16 -z -n"; else echo "[NO] 16"; fi
[ 3 -ge x ]
if [ $? = 2 ]; then echo "[OK] 17 un errore di test rende 2"; else echo "[NO] 17"; fi
P=$$
if [ "$P" -gt 0 ]; then echo "[OK] 18 \$\$ e' il pid"; else echo "[NO] 18 [$P]"; fi
NUM=7
if [ $NUM -eq 7 ]; then echo "[OK] 19 una variabile senza virgolette"; else echo "[NO] 19"; fi
source /a/aperto.sh
echo "[OK] 20 dopo uno script senza fi si continua"
if true; then echo "[OK] 21 l'if dopo lo script rotto funziona"; fi
echo "[OK] 22 fra virgolette \$X e' un dollaro, \"x\" una virgoletta" > /a/barre.txt
cat /a/barre.txt
echo "prova-sh: fine"

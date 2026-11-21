# EX-OS — La shell e i comandi

[🇬🇧 English](comandi.md) · **🇮🇹 Italiano**

[← README](../README.it.md) · [Tutti i manuali](../README.it.md#i-manuali)

**Indice**

- [/bin/mkdir e /bin/rmdir](#binmkdir-e-binrmdir)
- [/bin/delete](#bindelete)
- [Job control: `&`, `jobs`, `fg`](#job-control--jobs-fg)
- [Console virtuali — Alt+F1 … Alt+F5](#console-virtuali--altf1--altf5)
- [Data e ora](#data-e-ora)
- [/bin/textline — editor di testo lineare](#bintextline--editor-di-testo-lineare)
- [/bin/gfedit — editor a schermo intero](#bingfedit--editor-a-schermo-intero)
- [`/boot/autoexec.sh` — comandi all'avvio](#bootautoexecsh--comandi-allavvio)
- [`!silenced` — l'`echo off` degli script](#silenced--lecho-off-degli-script)
- [Cronologia dei comandi e modifica della riga](#cronologia-dei-comandi-e-modifica-della-riga)
- [Nomi con spazi: le virgolette](#nomi-con-spazi-le-virgolette)
- [`date` — che ore sono, e rimetterle](#date--che-ore-sono-e-rimetterle)
- [`ls` — modi di visualizzazione](#ls--modi-di-visualizzazione)

---

## /bin/mkdir e /bin/rmdir

```
mkdir <nome> [nome2 ...]    crea una o piu' directory
rmdir <nome> [nome2 ...]    cancella una o piu' directory VUOTE
```

Accettano percorsi assoluti o relativi alla directory corrente. **Solo
directory nella root**: il driver FAT12 risolve i percorsi a un livello, quindi
una directory annidata sarebbe corretta sul supporto ma irraggiungibile —
entrambi rifiutano con un messaggio esplicito invece di creare o cancellare
qualcosa di inutilizzabile.

`rmdir` rifiuta le directory non vuote, e non è una limitazione temporanea:
senza cancellazione ricorsiva i file rimasti dentro diventerebbero
irraggiungibili e i loro cluster resterebbero occupati per sempre. La root è
protetta, e `rmdir` su un file viene rifiutato.

---

## /bin/delete

```
delete <modello> [modello2 ...]

  ?   un carattere qualsiasi
  *   una sequenza qualsiasi di caratteri
```

```
delete nota.txt        un file preciso
delete *               tutto il contenuto della directory corrente
delete /temp/tmp*      i file che iniziano per "tmp" dentro /temp
delete dati?.log       DATI1.LOG, DATI2.LOG, ...
delete *.txt           per estensione
```

L'espansione dei jolly la fa il programma, non il kernel né la shell — come in
MS-DOS. Il confronto è insensibile al caso (FAT12 conserva i nomi in
maiuscolo). Le directory vengono saltate: per quelle c'è `rmdir`.

`delete` lavora in due fasi: prima raccoglie tutti i nomi corrispondenti
percorrendo l'intera directory, poi cancella. Non si può cancellare mentre si
elenca — le entry liberate vengono saltate da `readdir` e le voci successive
scalerebbero, facendo perdere file a ogni blocco.

`delete *` chiede conferma quando i file sono più di uno, dicendo quanti e da
dove. Un modello mirato come `tmp*` non la chiede.

---

## Job control: `&`, `jobs`, `fg`

```
comando &     esegue in background e torna subito al prompt
jobs          elenca i job ancora in esecuzione
fg [n]        riporta in primo piano il job n (l'ultimo se omesso)
```

Un job terminato viene annunciato al prompt successivo — `[1] terminato: tsleep
(codice 0)` — ed è anche il momento in cui il suo slot di processo viene
liberato: un figlio resta `ZOMBIE` finché il padre non lo raccoglie (il reaper di
init si occupa solo degli orfani).

**Non c'è `bg`**, e non è una dimenticanza: `bg` riprende un processo *sospeso*, e
per sospenderlo servirebbe un Ctrl+Z — cioè un segnale che fermi un processo e lo
lasci ripartire, che EX-OS non ha (dalla 0.227 i segnali ci sono, ma SIGSTOP e
SIGCONT no, e la shell non ne manda). Un job
qui o gira o è finito, non esiste lo stato in mezzo.

**L'output si mescola.** Un job in background scrive sulla stessa console della
shell, quindi le sue righe finiscono in mezzo al prompt e a ciò che stai
digitando. È il comportamento di qualunque shell Unix; se il programma ha bisogno
dello schermo tutto per sé, si lancia su un'altra console con Alt+Fn invece che
con `&`.

**L'input invece è protetto**, e serviva davvero. Il driver tastiera serve
l'*ultimo* che ha chiesto una riga: senza difese, un job in background che legge
`stdin` sostituirebbe la shell come lettore e il prompt non riceverebbe mai più
un comando — la console morirebbe. Due meccanismi lo impediscono:

1. `sys_read` su `stdin` restituisce la **fine dell'input** a chi non è il
   processo in primo piano della propria console. La shell dichiara il primo
   piano con `SYS_CONSOLE_SETFG` (sé stessa al prompt, il figlio quando lo
   aspetta). Unix qui userebbe `SIGTTIN`; senza quel segnale, l'EOF è l'unica risposta
   possibile — ed è comunque vera, quel programma input non ne avrà mai.
2. Chi prende la tastiera parlando **direttamente** al servizio `kbd` via IPC —
   la modalità raw di `gfedit` — non passa da `sys_read`, quindi controlla da sé
   `ConsoleInfo.fg` e si rifiuta di partire in background, spiegando perché.

**Ctrl+C** (dal 28 settembre 2026, kernel 0.221). Sulla console di testo, in
modo riga, chiede `[Ctrl+C] Fermo "textline" (PID 20)? s/n` e ferma il
programma solo con «s»; al prompt svuota la riga. Chi fermare lo dice la
shell con `pty_ctl(0, PTY_CTL_FG, pid)` — il figlio che aspetta, 0 al prompt —
e non `SYS_CONSOLE_SETFG`, perché al prompt il primo piano è la shell stessa.
Nel terminale in finestra Ctrl+C ferma subito, senza chiedere: lì lo gestisce
la disciplina del pty, nel kernel.

---

## Console virtuali — Alt+F1 … Alt+F5

**Cinque** schermi indipendenti (`VGA_N_CONSOLE`), uno solo visibile per volta,
ognuno con la propria shell — o il proprio `login` — avviata al boot.
**Alt+F1..F5** commuta: il programma che stava girando non viene sospeso né
chiuso, continua a lavorare e a disegnare nel proprio buffer, e si ritrova lo
schermo intatto quando ci si torna sopra.

È la risposta alla domanda "come lancio un'altra cosa senza chiudere questa":
apri `gfedit` sulla console 2, premi Alt+F3, hai un prompt pulito, e Alt+F2 ti
riporta all'editor esattamente dove l'avevi lasciato.

! **La grafica vive sulla console 5.** `exwin` fa ripartire il server a
finestre là sopra e lo dice all'avvio (`grafica accesa sulla console 5`): da
una console di testo ci si va con **Alt+F5**, e con **Alt+F1** si torna alla
shell. È anche il motivo per cui una prova che pilota la scrivania batte
`Alt+F1` prima di lanciare un'applicazione e `Alt+F5` subito dopo.

| | |
|---|---|
| Console 0 (Alt+F1) | è anche la console di **sistema**: i messaggi del kernel (`klog`) escono qui, accanto al prompt. Le altre restano pulite. |
| Ereditarietà | un programma nasce sulla console del padre (`sys_spawn`), quindi resta dove è stato lanciato |
| Tastiera | i tasti vanno **solo** alla console in primo piano; le shell delle altre restano ferme al proprio prompt con la richiesta di lettura pendente |
| Modalità raw | è **per console**: mentre gfedit tiene la 2 in raw, la shell della 1 continua a ricevere righe intere con eco e Backspace |

Alt+Fn è intercettato dal driver tastiera **prima** di qualunque altra
elaborazione e non viene consegnato a nessuno: è un comando all'interfaccia, non
input per il programma in esecuzione. Senza quella precedenza basterebbe un
editor che usa Alt+F per il menu File per rendere impossibile cambiare schermo —
cioè proprio nel caso in cui serve di più.

### Il Backspace e le colonne che non ci sono

La disciplina di riga «cooked» — quella che accumula i caratteri e li
consegna su Invio — cancellava **una colonna per ogni carattere nel
buffer**. Sembra ovvio e non lo è: i caratteri di controllo entrano nella
riga ma non vengono ecoati (ESC ci va, `/bin/textline` lo usa per annullare
una riga), e le frecce ci entrano come sequenza `ESC [ A`, di cui due byte
su tre sono stampabili e nessuno dei tre è stato disegnato.

> ! Risultato: due ESC battuti per sbaglio, due Backspace, e le due
> colonne cancellate erano **le ultime del prompt**. Nel registro seriale
> si vedeva `^H ^H^H ^H` e l'asterisco di textline sparire.

Ora ogni carattere del buffer porta con sé un bit — *questa l'ho disegnata
io oppure no* — e il Backspace cancella una colonna solo se quella colonna
è nostra. Il prompt è fuori portata per costruzione, non per un controllo
in più. Nello stesso giro sono cadute due asimmetrie della stessa
famiglia: a riga piena il carattere veniva ecoato ma non accumulato (si
eseguiva meno di quello che si leggeva), e il tab veniva disegnato pur
essendo impossibile da disfare — avanza fino alla prossima tabulazione,
che dipende da dove comincia il prompt.

**Arrivati al limite la riga si azzera del tutto.** Se non resta più
niente di visibile, quello che eventualmente sopravvive nel buffer sono
caratteri invisibili, pronti a finire dentro il comando successivo: chi
cancella fino in fondo si aspetta una riga vuota e la trova vuota davvero.

Vale per `drivers/kbd/kbd.c` e per il TTY interno di ripiego
(`drivers/tty/tty.c`). La modifica di riga della shell — quella con le
frecce e la cronologia — non era coinvolta: lavora in raw e accetta solo
caratteri stampabili.

Costo: 4 KB di BSS del kernel per console (il buffer di schermo) più un processo
shell da ~14 KB. Il numero è `VGA_N_CONSOLE` in `kernel/include/vga.h`, e deve
restare uguale a `KBD_N_CONSOLE` in `drivers/kbd/kbd_proto.h`.

---

## Data e ora

`time_now()` legge l'orologio CMOS della macchina (MC146818, porte 0x70/0x71) e
restituisce data e ora vere — quelle che l'orologio a batteria continua a contare
a macchina spenta. Da non confondere con `uptime_ms()`, che misura *durate* e non
sa che ora sia.

Ritorna `-ENODEV` se l'orologio non risponde o consegna una data impossibile
(succede su hardware vecchio con la batteria del CMOS scarica): in quel caso il
chiamante deve dire "ora ignota" invece di mostrare un orario inventato — gfedit
scrive `--:--:--`.

**E dal kernel 0.218 si può anche SCRIVERE**: `time_set()` (`SYS_TIME_SET`, 213)
rimette l'orologio, è di root, e accetta gli anni dal 1980 al 2099. Il comando
che la usa è `date`, con `-set-date:` e `-set-time:`; il perché di ogni scelta
sta in **[`date`](#date--che-ore-sono-e-rimetterle)**. Fino ad allora l'orologio
si leggeva e basta, e su una macchina con la data sbagliata non c'era modo di
rimediare da dentro EX-OS.

! In QEMU il RTC parte in **UTC**, non in ora locale: EX-OS lo legge come ora locale
e l'ora risulta spostata del fuso. Le prove (`tools/qemu_drive.py`) e `make run`
passano `-rtc base=localtime`, come su una macchina vera (kernel 0.340).

---

## /bin/textline — editor di testo lineare

Modello edlin: si opera per numero di riga, non con un cursore. Resta il modo
più rapido di correggere una riga sola, e l'unico che funziona anche quando
`/dev/kbd.drv` non è disponibile e la console è servita dalla tastiera
in-kernel di ripiego.

```
textline <file>              apre il file per l'editing
textline <file> -v           visualizza il contenuto
textline <file> -vp          visualizza a pagine
textline <file> -c:<file2>   copia <file> in <file2>
```

Comandi: `h`/`help`, `l`, `lNN`, `lNN,MM`, `lp…` (a pagine), `m`, `mNN`, `n`,
`dNN`, `cNN,MM`, `w` (salva), `e` (salva ed esce), `q` (esce). ESC annulla la
riga in inserimento e riporta al prompt.

---

## /bin/gfedit — editor a schermo intero

Riscrittura per EX-OS di **GF_TEXTEDITOR**, l'editor ncurses+pthread dello
stesso autore (sorgenti originali in `gftexteditor/`). Non è un porting: di
ncurses, dei thread, di stdio POSIX e di una `free()` vera EX-OS non ha
niente. Quello che resta uguale è il programma — menu a tendina in stile
MS-DOS EDIT, otto aree aperte insieme, find/replace, annullamento,
evidenziazione sintattica.

```
gfedit                apre un'area vuota
gfedit <file> [...]   apre fino a 8 file
gfedit -h             elenco delle scorciatoie
```

| | |
|---|---|
| Movimento | frecce, Home/Fine, Ctrl+Home/Fine, PagSu/PagGiu, Ctrl+G (vai a riga) |
| Selezione | Shift+movimento, Ctrl+A, ESC per abbandonarla |
| Modifica | Ins, Ctrl+Z, Ctrl+X/C/V |
| File | Ctrl+N, Ctrl+O, F2 o Ctrl+S, Ctrl+W, Alt+X |
| Ricerca | Ctrl+F, F3, Shift+F3, Ctrl+H |
| Aree | F6, Shift+F6, Alt+1…Alt+8 |
| Menu | F10 o ESC, oppure Alt+F M C O A |

La barra di stato mostra l'ora del giorno vera, che **avanza da sola** anche a
tastiera ferma: il ciclo principale non aspetta più un tasto all'infinito ma si
risveglia ogni mezzo secondo (`ipc_recv_timeout`). Fra un risveglio e l'altro il
processo è `BLOCKED` e non consuma un tick di CPU.

Linguaggi evidenziati: C, C++, BASIC, assembly, riconosciuti dall'estensione e
cambiabili da *Opzioni → Linguaggio*.

**Limiti, e perché sono lì.** 512 righe per file, 200 caratteri per riga, 8
aree. Le righe sono slot a lunghezza fissa perché la `free()` di EX-OS è un
no-op dichiarato (allocatore a bump su `sbrk`): con stringhe riallocate ogni
tasto premuto perderebbe per sempre la memoria della riga precedente. Un file
più grande dei limiti viene caricato **in parte**, la barra di stato lo dice, e
il salvataggio su quel file resta bloccato — per non cancellare la parte mai
letta. *Salva con nome* su un file diverso è invece permesso.

**Serve `/dev/kbd.drv`.** Un editor a schermo intero ha bisogno dei tasti uno
per uno, e la modalità raw vive nel driver tastiera. Senza quel servizio
gfedit non parte e rimanda a textline, invece di mostrare un'interfaccia che
non risponderebbe.

---

## `/boot/autoexec.sh` — comandi all'avvio

Una riga = un comando, eseguito **esattamente come se fosse digitato**:
stessi built-in, stesse virgolette, stesso `&` per il background. Le righe
vuote e quelle che cominciano con `#` si saltano; una riga che comincia
con `@` viene eseguita senza essere stampata, come nell'autoexec del DOS.

Sul CD di EX-OS ce n'è uno che accende la rete da solo:

```
autoexec> /dev/pci.drv &
autoexec> netdetect -c
autoexec> /dev/ip.drv &
autoexec> dhcp
```

Dopo l'avvio `ping` e `ftp` funzionano senza toccare niente.

! **Lo esegue solo la shell della PRIMA console.** EX-OS ne avvia una per
ognuna delle cinque console virtuali: senza questo controllo l'autoexec
girerebbe quattro volte, e per `/dev/pci.drv &` significherebbe quattro
processi che si contendono lo stesso servizio.

! **La via d'uscita esiste prima di servire.** Un autoexec con dentro un
comando che si blocca renderebbe il sistema inutilizzabile, e il file per
correggerlo sta sul supporto che non si raggiunge più. Quindi:

| | |
|---|---|
| `autoexec=0` in `kernel.cfg` | lo salta (il file si modifica da un'altra macchina) |
| **Alt+F2, Alt+F3, Alt+F4** | danno sempre una shell pulita, anche mentre la prima è impegnata |

Il secondo è quello che conta davvero: non richiede di poter modificare
nulla.

## `!silenced` — l'`echo off` degli script

```
!silenced      da qui in poi i comandi non si vedono piu'
!verbose       si tornano a vedere
@comando       zittisce UNA riga sola
```

! **Zittisce il comando, non il suo risultato**, ed è la distinzione che
rende l'opzione utile: quello che un comando stampa è il motivo per cui lo
si è messo nello script, mentre la riga di comando la si è già scritta.
L'autoexec del CD comincia con `!silenced` e mostra solo l'indirizzo
ottenuto, non i quattro comandi che sono serviti a ottenerlo.

Vale **da dove sta in poi**, non per tutto il file: si può zittire la parte
rumorosa e lasciar vedere quella che interessa. La riga della direttiva non
si stampa mai.

Gli script non sono più solo l'autoexec:

```
source /prova.sh      esegue in QUESTA shell
/prova.sh             lo stesso, per nome
```

! **`source` e non una spawn**: i comandi devono girare nella shell
corrente, altrimenti un `cd` o un `export` dentro lo script sparirebbero
insieme al processo figlio. Un nome che finisce in `.sh` si riconosce
*prima* di provare a lanciarlo, non dopo che la spawn è fallita: la spawn
fallisce per molti motivi, e trattarli tutti come «sarà uno script»
trasforma un errore preciso in un secondo errore che parla d'altro.

## Cronologia dei comandi e modifica della riga

Le frecce **su** e **giù** ripercorrono i comandi già dati (24 di
cronologia); **sinistra**, **destra**, **Home**, **Fine**, **Backspace** e
**Canc** modificano la riga in corso. `Ctrl+C` la abbandona.

! **La riga in corso non si perde.** Chi ha scritto mezzo comando e va a
cercarne uno vecchio con la freccia in su la ritrova scendendo fino in
fondo.

Righe vuote e doppioni consecutivi non entrano in cronologia: chi ripete
lo stesso comando dieci volte non vuole dieci voci da riattraversare.

! **Serve la modalità raw della tastiera**, perché in cooked il driver
assembla la riga e la consegna su Invio — le frecce non hanno modo di
attraversare un flusso di testo. La shell prende quindi la disciplina di
riga su di sé: eco, backspace, cursore.

! **Se il servizio `kbd` non risponde si torna a leggere righe intere**:
si perde la cronologia, non la shell. E il driver torna in cooked da solo
ogni volta che un programma legge da stdin, quindi la modalità si
riafferma a ogni prompt — il che la rende anche autoriparante.

! **Solo la console in primo piano** prende i tasti. Senza quel
controllo tutte e quattro le shell si contendevano la tastiera, e quelle
non visibili la riportavano in cooked togliendola a chi stava scrivendo.

! Il ridisegno usa **solo Backspace**, perché il TTY di EX-OS non ha un
linguaggio di posizionamento del cursore. Conseguenza: su una riga più
lunga della larghezza dello schermo la modifica si vede male — ma la riga
resta corretta, e quello che si legge è ciò che verrà eseguito.

## Nomi con spazi: le virgolette

```
ex-os:/> cat "/disco/appunti di riunione.txt"
contenuto di prova
ex-os:/> cp '/disco/appunti di riunione.txt' /disco/copia.txt
copiati 19 byte in /disco/copia.txt
```

! **Apici singoli e doppi fanno la stessa cosa.** Su una shell Unix la
differenza esiste perché fra virgolette doppie `$VAR` viene espansa e fra
apici singoli no. Qui non c'è nessuna espansione — né di variabili né di
caratteri jolly — quindi le due forme non avrebbero niente da
distinguere. Accettarle entrambe e trattarle uguale è onesto; accettarne
una sola costringerebbe a ricordare quale.

Una virgoletta non chiusa viene **segnalata**: prima l'argomento si
prendeva fino a fine riga in silenzio, e il comando falliva lamentando un
file inesistente dal nome assurdo — il difetto era nella riga, non nel
file. Lo stesso vale per gli argomenti oltre il sedicesimo, che prima
sparivano senza dire niente.

## `date` — che ore sono, e rimetterle

```
date                        giorno, data e ora per esteso
date -d                     solo la data, 2026-09-16
date -t                     solo l'ora, 17:52:31
date -set-date:2026/09/16   rimette la data   (serve root)
date -set-time:17:52:30     rimette l'ora     (serve root)
date -ntp                   l'ora da pool.ntp.org   (serve root)
date -fuso:+1,ue            il fuso per -ntp: Italia (/boot/fuso.cfg)
```

```
ex-os:/> date
mercoledi' 16 settembre 2026, 16:09:20
ex-os:/> date -set-time:10:11:12
orologio rimesso: mercoledi' 16 settembre 2026, 10:11:12
```

**L'orologio si può rimettere dal kernel 0.218, e prima no.** Da quando
esiste — agosto 2026 — l'orologio CMOS di EX-OS si **leggeva e basta**: c'era
`SYS_TIME` e non c'era il suo gemello. Non era un buco teorico: l'Acer Aspire
3000 di prova segnava il **2005**, e ogni file che caricava su un server FTP
arrivava datato «Feb 9 2005». L'unico modo di rimetterlo era entrare nel BIOS.

Adesso c'è `SYS_TIME_SET` (213), che chiama `rtc_write()` in
`kernel/arch/x86/rtc.c`.

! **È di root, come `mount`.** L'ora di sistema non è un'impostazione
personale: chi la sposta cambia la data di ogni file che chiunque altro
scriverà, e con essa il giudizio di `netupdate -check` su che cosa è più
recente. Da un utente qualunque si passa da `sudo date ...`.

! **La scrittura ha una trappola in più delle tre della lettura.** Non si
scrive mentre il chip conta: se l'aggiornamento cade in mezzo ai sei registri,
il chip riscrive sopra a metà di quel che si è appena messo, e si ottiene una
data mezza vecchia e mezza nuova. Il bit `SET` del registro B ferma il
conteggio per la durata della scrittura.

! **E si scrive nel formato che c'è già** — BCD o binario, 12 o 24 ore, come
dice il registro B. Cambiarlo sarebbe più comodo da programmare e romperebbe
la lettura del BIOS, che quel formato lo conosce dall'accensione.

! **Il registro del secolo si aggiorna solo dove c'è già.** `rtc_read` non lo
legge affatto (usa la convenzione «sotto 70 = 2000+»), quindi per EX-OS
sarebbe inutile; ma un BIOS che invece lo usa, e che lo trovasse fermo al 20
mentre le due cifre dicono 26, ripartirebbe da un anno sbagliato. Si scrive
0x32 **solo se quel che c'è dentro somiglia già a un secolo** (19 o 20): su una
macchina dove quel registro serve ad altro, il controllo lo lascia in pace.

! **L'anno sta fra 1980 e 2099**, mentre la lettura ne accetta fino al 2199.
Il chip tiene DUE cifre, e la lettura le interpreta con quella convenzione:
chiedere il 2150 vorrebbe dire rileggere il 2050 senza dirlo a nessuno, e un
errore silenzioso è peggio di un rifiuto.

! **Le due metà si rimettono separate.** Chi cambia solo l'ora — l'ora legale,
o un orologio che ha derivato di due minuti — non deve ribattere anche la data,
e soprattutto non deve rischiare di sbagliarla. I campi che non si toccano
vengono riletti dall'orologio, perché `time_set()` vuole una `RtcTime` intera:
il chip non ha un modo di scrivere «solo i minuti».

! **Non c'è un comando `time`, ed è voluto.** Su qualunque sistema Unix `time
comando` MISURA quanto ci mette un comando a girare. Spendere quel nome per
stampare l'ora vorrebbe dire non averlo più il giorno in cui servirà davvero —
e a quel punto o si rompe chi lo usa, o si sceglie un nome peggiore. Un binario
solo dice tutt'e due le cose, e sul floppy ne costa uno invece di due: quando
`date` è stato scritto, sul floppy restavano 63488 byte liberi e il più piccolo
programma di EX-OS ne pesa 13,5 KB.

! **Il fuso orario** (kernel 0.340). L'orologio della macchina è ora locale; il
kernel conosce il fuso di `/boot/fuso.cfg` e i secondi contati dal 1970 sono
UTC, confrontabili con l'istante di un'altra macchina. `date -fuso:` lo cambia
subito; da un CD vale fino allo spegnimento.

## `ls` — modi di visualizzazione

```
ls -h              elenca tutte le opzioni
ls                 data e ora, <DIR> o <FILE>, dimensione e nome
ls -d              lo stesso: c'e' per chi lo ha nelle dita
ls -s              solo nome e dimensione, senza interrogare i file
ls -mc /bin        a colonne: solo i nomi, il piu' compatto
ls -l              permessi, proprietario, gruppo, misura e data
ls -md             come il modo normale, piu' gli attributi
ls -a              mostra anche i nomi che cominciano con un punto
ls -p              una pagina per volta (Invio avanza, q smette)
```

```
ex-os:/> ls
data        ora    tipo    dimensione  nome
2026-09-16  18:10  <DIR>            -  BOOT
2026-09-16  18:10  <DIR>            -  BIN
2026-09-16  18:10  <FILE>        2631  LOADER.BIN
2026-09-16  18:10  <FILE>      270376  KERNEL.BIN

2 file, 273007 byte    5 directory
```

**Dal 16 settembre 2026 `ls` nudo dice anche QUANDO e CHE COSA.** Prima
stampava due cose, il nome e la dimensione. La data c'era già — ma solo con
`-d`, `-md` o `-l`, cioè solo per chi sapeva che esistevano; e il tipo si
deduceva dalla barra finale del nome, che è una convenzione da conoscere. Le
due domande più comuni davanti a un elenco di file sono «di quando è?» e «è
una cartella?», e per tutt'e due la risposta era dietro un'opzione che nessuno
batte.

! **E costa una `stat()` per voce**, che il modo nudo prima non faceva. Su un
floppy si sente, ed è esattamente per questo che `-s` esiste e non è un
ripiego: è il modo da usare quando si vuole solo sapere quali nomi ci sono, su
un disco lento o su una directory molto grande.

! **La data è quella di MODIFICA, e quella di creazione non c'è.** Non è una
scelta: la voce di directory che i tre filesystem consegnano al VFS tiene UNA
coppia data/ora, e `struct stat` infatti pone `st_ctime` e `st_atime` uguali a
`st_mtime`, dichiarandolo. Mostrare la stessa data sotto due intestazioni
diverse sarebbe peggio che mostrarne una sola.

! **Una directory non ha una dimensione da mostrare**, e adesso che la colonna
accanto dice `<DIR>` non serve nemmeno scriverlo due volte. Il numero che i
filesystem tengono lì è la misura della voce di directory sul disco — zero su
FAT — non quanto pesa il contenuto. Un trattino dice «la domanda non si
applica»; uno zero direbbe il falso.

! **`-d` qui significa «dettagli»**, non quello che significa su Unix (dove
`ls -d` mostra la directory invece del contenuto). È una scelta di questo
progetto, e l'aiuto la dichiara perché nessuno la scopra per tentativi.

! Senza `-a` si nascondono i nomi che cominciano con un punto, `.` e `..`
compresi. È un cambiamento rispetto a prima, quando venivano sempre
mostrati. Su un CD `.` e `..` non compaiono comunque: ISO 9660 non li
consegna (sono due record con nome `0x00` e `0x01`, e il driver li salta).

! Il bit «nascosto» di FAT si **vede** con `-md` ma non nasconde niente.
Guardarlo costerebbe una `statraw()` per ogni voce anche quando si
stampano solo i nomi, e su un floppy si sente; a nascondere è il punto
iniziale, che è la convenzione di tutti i filesystem che EX-OS monta.

**Le date arrivano davvero dal filesystem** (kernel 0.168). Prima
`sys_stat` scriveva zero e nessun programma poteva mostrarle. Ora:

| | |
|---|---|
| FAT12 / FAT16 / FAT32 | il formato è quello nativo, nessuna conversione |
| ext2 | da `i_mtime` (tempo Unix) al formato FAT |
| ISO 9660 | dai sette byte del record di directory |

! Una data **zero significa «questo volume non la tiene»** e i programmi
stampano dei trattini: un 1980 inventato sembrerebbe una data vera. Il
formato copre 1980-2107 — un file ext2 datato prima del 1980 esce senza
data invece che con un anno sbagliato.

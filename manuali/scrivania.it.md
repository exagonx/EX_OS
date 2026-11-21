# EX-OS — La scrivania grafica (ExWin)

[🇬🇧 English](scrivania.md) · **🇮🇹 Italiano**

[← README](../README.it.md) · [Tutti i manuali](../README.it.md#i-manuali)

**Indice**

- [L'editor](#leditor)
- [Il file manager](#il-file-manager)
- [Il terminale in finestra](#il-terminale-in-finestra)
- [EXBrowser, il navigatore](#exbrowser-il-navigatore)
- [EX-IDE — l'ambiente di sviluppo visuale](#ex-ide--lambiente-di-sviluppo-visuale)
- [La prova dei font](#la-prova-dei-font)
- [L'orologio](#lorologio)

---

## L'interfaccia grafica in pratica

```
exwin                       accende la grafica sulla console 5
                            Alt+F5 ci va, Alt+F1 torna alla shell

/exwin/bin/pm               la scrivania (la avvia exwin da sola)
/exwin/bin/filemgr [DIR]    il file manager
/exwin/bin/edit [FILE...]   l'editor di testo: un file per scheda
/exwin/bin/term [PROG]      il terminale in finestra (senza PROG: la shell)
/exwin/bin/exbrowser [URL]  EXBrowser, il navigatore (un percorso assoluto diventa file:)
/exwin/bin/exide [DIR]      l'ambiente di sviluppo visuale
/exwin/bin/archivi [ARCH]   gli archivi ZIP, TAR e TAR.GZ: apre, estrae, crea
/exwin/bin/calctor          la calcolatrice: normale, scientifica, programmatore
/exwin/bin/pennello [FILE]  il programma di disegno: apre BMP PNG JPG GIF ICO, salva PNG e BMP
/exwin/bin/immagini [FILE]  il visualizzatore di immagini, con lo zoom e la cartella
/exwin/bin/fontprova        la prova dei font TrueType, fatta per essere vista
/exwin/bin/orologio         data e ora nell'angolo della barra
```

Avviata la grafica, la shell **resta viva sulla console 0**: si continua a
lavorare da lì e con `Alt+F5` si passa alla scrivania.

**Dalla scrivania si aprono dal menu Avvio**, che legge le voci da
`/exwin/lib/applicazioni.txt`. Una riga per applicazione, con un terzo campo
**facoltativo** per l'icona:

```
Nome mostrato          | /percorso/eseguibile | /percorso/icona.ico
Strumenti/Editor       | /exwin/bin/edit      | /exwin/icon/baseapp/edit_64.ico
```

! **Una barra nel nome fa una categoria.** `Strumenti/Editor` è la voce
«Editor» dentro «Strumenti»: nel menu le categorie stanno in cima e aprono un
elenco **di fianco**, con le loro icone. Non si dichiarano da nessun'altra
parte — esistono finché c'è una voce che le nomina.

Dal 7 novembre 2026 (voce 80, pm 0.028) il menu pubblicato ha cinque gruppi:
**Applicazioni** (File manager, Archivi, Calctor, Pennello, Immagini),
**Internet**, **Strumenti**, **Multimedia** (Lettore audio, Volume, VLC) e
**Giochi**. Su una macchina già installata netupdate (0.046) porta il menu nuovo e
rimette in fondo le voci che l'utente aveva aggiunto: una voce si riconosce dal
programma che avvia, quindi quelle del sistema prendono il loro gruppo nuovo e
le altre restano (`menu_ricupera`). Il menu Avvio si chiude quando si sceglie un
programma o si fa clic sulla scrivania; le icone accanto alle voci si possono
spegnere (Impostazioni, scheda Scrivania, «Icone accanto alle voci del menu»,
accesa per difetto).

Il pulsante **Applicazioni del menu...** nella scheda Sistema di Impostazioni
(fino a pm 0.027 la voce «Applicazioni...» del menu) aggiunge e toglie righe da quel
file (e rimette al loro posto categoria e icona di ciò che non mostra), e la
direttiva `@avvio <percorso>` dice quale programma parte da solo con la
scrivania (è così che l'orologio si trova già lì).

! **AGGIUNGERE UN'APPLICAZIONE È UNA RIGA, NON UNA RICOMPILAZIONE**, e il file
resta leggibile e modificabile a mano apposta: un file di configurazione che
solo un programma sa scrivere è un file che non si può riparare quando quel
programma non parte.

! **DALLA SHELL SI LANCIANO COL COMANDO, MA PRIMA DI COMMUTARE.** Battendo il
comando *dopo* `Alt+F5` i tasti vanno al server grafico, non alla shell — e
sembra che il sistema si sia bloccato. È la stessa separazione che rende
possibile tutto il resto, vista dal lato scomodo.

### L'editor

| tasto | cosa fa |
|---|---|
| frecce, Home/End, PgSu/PgGiù | muovono il cursore |
| Backspace, Canc | cancellano indietro e avanti |
| Invio | spezza la riga |
| clic del mouse | posiziona il cursore |
| Shift + frecce, Ctrl+A | scelgono il testo |
| Ctrl+X, Ctrl+C, Ctrl+V | tagliano, copiano, incollano |
| Ctrl+Z | annulla (taglia, incolla, cancella) |
| Ctrl+F, F3, Shift+F3, Ctrl+H | cerca, il successivo, il precedente, sostituisce |
| Ctrl+N, Ctrl+O | un file nuovo, un file aperto: ognuno nella sua scheda |
| Ctrl+Tab, Ctrl+Shift+Tab | la scheda dopo, quella prima |
| Ctrl+W | chiude la scheda; se il file è modificato chiede |
| Ctrl+S | salva; senza nome apre il dialogo **Salva con nome** |
| Ctrl+Q | esce; se ci sono file modificati dice quanti e chiede |

Le stesse cose stanno nei menu **File** e **Modifica**. «Apri» e «Salva con
nome» stanno in `exdlg.so`, la libreria condivisa dei dialoghi, che usa anche
il file manager. `edit a.txt b.txt` apre due schede.

Manca, dichiarato: il **ripeti** dopo un annulla, e annullare la
digitazione (si annullano i comandi che cambiano il testo, non i tasti
battuti). Un file oltre le 512 righe si apre in parte e non si salva.

### Il file manager

Elenco che scorre, **directory in cima**, pulsanti `Su` e `Apri`, riga di
stato col percorso. Frecce e Invio oltre al mouse. «Apri» su un file lo apre
col programma che gli spetta (`/exwin/lib/tipi.txt`: le immagini con
Immagini, gli archivi con Archivi, le pagine con EXBrowser, il resto con
l'editor), cercandolo in `/exwin/bin` e poi in `/cdrom/exwin/bin`.

Il **tasto destro** apre un menu: Apri, Nuova cartella, Nuovo file,
Rinomina, Copia, Taglia, Incolla, Cancella. Si scelgono **più righe** con
Ctrl+clic e Shift+clic, o segnandole con la barra spaziatrice; trascinate su
una cartella dell'albero a sinistra, una finestra chiede se copiarle,
spostarle o lasciar perdere.

**Dal 16 settembre 2026 l'elenco ha quattro colonne e si ordina cliccando**
(`filemgr` 0.002):

```
Cartelle     |[Nome ^][Tipo][Dimensione][Data]
- /          | bin         <DIR>          -  2026-09-16 20:57
  + bin      | boot.img    <FILE>   1474560  2026-09-16 20:57
  + boot     | KERNEL.BIN  <FILE>    270376  2026-09-16 20:57
```

Un clic sceglie la colonna, un secondo clic sulla stessa **rovescia il verso**,
e la freccia nell'etichetta dice qual è: «per che cosa è ordinato» e «in che
verso» sono due domande, e la seconda senza indizi si risponde indovinando. Il
verso se lo ricorda **ogni colonna per conto suo** — chi ordina per data vuole
il più recente in cima, chi ordina per nome vuole la A.

! **L'intestazione non è un controllo nuovo del toolkit**: sono quattro
pulsanti allineati alle colonne, che escono dalle **stesse costanti** da cui
esce la riga. È la stessa scelta dell'albero a sinistra, che è «una lista con
dentro l'indentazione» e non un controllo albero. Scritte due volte, le
larghezze si scollerebbero alla prima colonna allargata — e il sintomo sarebbe
un'intestazione che indica la colonna sbagliata, cioè una bugia.

! **LE DIRECTORY VENGONO PRIMA, E NON È ESTETICA:** in una directory con cento
file, quelle in cui si vuole entrare sarebbero sparse in mezzo. Restano in cima
**qualunque colonna si scelga**; l'unica che le mescola è «Tipo», dove
separarle è esattamente quel che si è chiesto cliccando.

! **Fra due directory non si ordina per dimensione.** La colonna mostra un
trattino — il numero che i filesystem tengono lì è la misura della voce sul
disco, non quanto pesa il contenuto — ma il numero c'è lo stesso, e ordinandoci
sopra le cartelle uscivano in un ordine che chi guarda non può spiegare. Fra
due trattini si ordina per nome, che è l'unica cosa che si vede.

! **La data costa una `stat()` per voce**, che la voce di directory non porta.
Qui il prezzo si può pagare: una directory si legge quando qualcuno ci entra,
non in un ciclo. È la stessa data di `ls`, cioè quella di **modifica**: quella
di creazione i filesystem non la tengono.

### Il terminale in finestra

`/exwin/bin/term` apre una shell dentro una finestra; `term /bin/gfedit` ci
apre quel programma invece della shell. La finestra è un multiplo esatto della
cella del font 8x16, perché il controllo «terminale» del toolkit calcola le
colonne come larghezza/8 e le righe come altezza/16.

! **LA SHELL GIRA SU UNA PIPE, NON SUL `tty` DELLA CONSOLE**, ed è tutto il
punto del terminale in finestra. Una shell che legge il descrittore 0 della
console si contende la tastiera con chiunque altro stia su quella console;
dietro una pipe quella domanda non esiste — i tasti li dà il server alla
finestra col fuoco, e da lì vanno nella pipe di *quella* shell. È così che se
ne possono aprire due senza che si disturbino.

### EXBrowser, il navigatore

`/exwin/bin/exbrowser [URL]`. Senza argomento parte dalla pagina di casa; con un
percorso assoluto (`exbrowser /exwin/doc/exbrowser.html`) lo trasforma in un
`file:`, che è la sola forma che il resto del programma conosce.

Nella barra ci sono **due caselle**: l'indirizzo e **Cerca**, che compone da
sola l'interrogazione del motore scelto in **File > Impostazioni**
(duckduckgo, wikipedia, marginalia). Quel che sa fare la pagina — testo che si
spezza e scorre, collegamenti, immagini, fogli di stile, tabelle, moduli,
HTTPS, biscotti, JavaScript — sta nelle voci di «Novità» qui sopra, che sono
il posto dove quel lavoro è raccontato per intero.

| tasti | che cosa fanno |
|---|---|
| Ctrl+T, `target="_blank"`, `window.open()` | una scheda nuova |
| Ctrl+clic su un collegamento | una scheda nuova, dietro |
| Ctrl+N, Shift+clic | una finestra nuova (un secondo EXBrowser) |
| tasto destro su un collegamento | Apri, in una scheda nuova, in una finestra nuova, Copia l'indirizzo |
| Ctrl+Tab, Ctrl+W | la scheda dopo; chiude la scheda (l'ultima chiude la finestra) |

Le schede che non si guardano tengono l'indirizzo e la posizione, non la
pagina: tornandoci la pagina si ricarica, dalla cache se c'è.

### EX-IDE — l'ambiente di sviluppo visuale

`/exwin/bin/exide [DIR]`. Con un argomento apre subito il progetto che sta in
quella directory. Tre aree come in Visual Basic: a sinistra gli strumenti, in
mezzo la maschera su cui si dispongono, a destra le proprietà di quello scelto;
doppio clic su un controllo e si apre l'editor dentro la funzione che l'evento
chiamerà.

! **LA GIUNTURA FRA IL DISEGNO E IL CODICE È L'ID**, e c'era già: nel disegno
il pulsante *è* `ID_PULSANTE1`, nel sorgente c'è `case ID_PULSANTE1:`. exide è
fattibile qui più che altrove perché ExWin era già fatto a forma di VB6 — non
c'è niente da inventare, c'è da *scrivere* quel che il disegno dice.

Un progetto sono quattro file, e la regola che decide se sopravvive è che il
generato e lo scritto non si tocchino mai:

| file | chi lo scrive |
|---|---|
| `finestra.dis` | solo exide: il disegno |
| `finestra.h` | solo exide: gli id, i puntatori, i prototipi |
| `finestra_gen.c` | solo exide: crea i controlli e smista gli eventi |
| `finestra.c` | **solo tu**: exide ci *aggiunge* gli handler che mancano, in fondo, e non riscrive mai quel che c'è |

Gli altri sorgenti del progetto (`src/`) si aprono da **Strumenti > Files**, in
una finestra con le **schede**: un file per scheda, «Apri...» o Ctrl+O per
aggiungerne, e cambiando scheda il file si salva.

### La prova dei font

`/exwin/bin/fontprova` disegna le stesse righe in TrueType a corpi diversi, ed
è **fatta per essere fotografata**: le prove grafiche di questo sistema si
misurano nei pixel. Il rasterizzatore è già confrontato con FreeType, ma quel
confronto gira sull'host — dice che i glifi vengono giusti e non dice niente su
`exfont.so` caricata a caldo, sulla cache, sulla fusione col fondo o sul fatto
che i file dei font siano leggibili dal CD. Questa finestra prova il giro
intero.

! **IN CIMA C'È LA RIGA COL FONT DI SISTEMA, e serve da metro:** se il TrueType
non si carica restano solo quella e le scritte di errore, e si capisce subito
dove si è fermato invece di guardare una finestra vuota.

### L'orologio

`/exwin/bin/orologio` mette data e ora nell'angolo della barra, e sta **sopra a
tutte** le finestre perché è un pezzo della barra. È un **processo a parte**, e
non un filo dentro il program manager: quel che si vuole è che l'ora si
aggiorni qualunque cosa faccia il resto del sistema, e un filo condividerebbe
con lui la connessione al server e la coda dei messaggi — un program manager
occupato sarebbe un orologio fermo.

! **L'ORA È QUELLA LOCALE**, del fuso di `/boot/fuso.cfg` (kernel 0.340): fino
alla 0.339 la libc non aveva fusi e `localtime()` era identica a `gmtime()`.

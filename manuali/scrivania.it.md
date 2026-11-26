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

Da pm 0.032 (exwin.so 0.029, voce 122) ogni riga ha l'icona in un piccolo
pulsante quadrato a sinistra e il nome accanto, fuori dal pulsante; un gruppo ha
un triangolino a destra. La riga si accende sotto il puntatore o scelta con le
frecce; Invio o la freccia a destra aprono il gruppo, Esc o la freccia a
sinistra tornano indietro.

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
l'editor), cercandolo in `/exwin/bin` e poi in `/cdrom/exwin/bin`. Il programma
di ogni tipo si sceglie in [Impostazioni > Applicazioni](#con-che-programma-si-apre-un-file-impostazioni--applicazioni-pm-0029).

Il file manager si apre nella cartella dell'utente (`$HOME`); come ogni
programma avviato dal menu Avvio parte da lì (filemgr 0.017, pm 0.033).
Il **tasto destro** apre un menu: Apri, Nuova cartella, Nuovo file,
Rinomina, Copia, Taglia, Incolla, Cancella, **Apri una shell qui** (anche nel
menu File: un Terminale con la shell nella cartella mostrata). Si scelgono **più righe** con
Ctrl+clic e Shift+clic, o segnandole con la barra spaziatrice; trascinate su
una cartella dell'albero a sinistra, una finestra chiede se copiarle,
spostarle o lasciar perdere. Mentre si trascina, accanto al puntatore c'è un
**foglio**, **tre fogli** uno sopra l'altro se le voci sono più d'una
(exwin.so 0.027).

**Opzioni > Visualizzazione** (filemgr 0.016) ha due scelte, accese di serie:
mostrare i file e le cartelle nascosti (quelli che cominciano con `.`), e
mostrare l'estensione (spenta: `nota` invece di `nota.txt`). Restano in
`~/.exwin/config/filemgr.cfg` (`nascosti = 0|1`, `estensione = 0|1`) e cambiano
solo cosa si vede: copiare e cancellare una cartella prendono anche i nascosti.

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

### Con che programma si apre un file: Impostazioni > Applicazioni (pm 0.029)

La scheda **Applicazioni** di Impostazioni (Avvio > Impostazioni...) dice con
che programma si apre ogni tipo di file e di indirizzo. **Tipo** sceglie il
gruppo (Immagini, Testi e documenti, Pagine web, Audio, Video, Archivi,
Indirizzi); la lista ha una riga per tipo, `.png  immagini`, `http://
exbrowser`, e dice `(scelto)` per una scelta dell'utente, `(non installato)`
per un programma che sul disco non c'è, `(nessun programma)` per un tipo che
ancora nessuno apre. **Scegli il programma...** apre il dialogo Apri sul
programma della riga; **Predefinito** torna a quello del sistema. La scelta
vale dal doppio clic dopo, nel file manager e sulla scrivania, senza
riavviare niente.

| File | Che cosa tiene | Chi lo scrive |
|---|---|---|
| `/exwin/lib/tipi.txt` | i predefiniti: righe `[Gruppo]`, poi `est est \| programma`; un tipo che finisce coi due punti è un indirizzo (`http:`), un programma vuoto vuol dire nessuno | il sistema: lo riscrive netupdate |
| `/boot/tipi.cfg` | le scelte dell'utente, `tipo \| programma`, lette **prima** | la scheda |

! **Le scelte non stanno in `tipi.txt`**: lo riscrive netupdate, e una scelta
scritta lì se ne andrebbe al primo aggiornamento. Le scelte vogliono un disco:
dal CD `/boot` non si scrive, e la scheda lo dice.

La regola si legge in un posto solo, il toolkit (`exwin.so` 0.023):
`ex_open_file` per il file manager e la scrivania, e `ex_types`, `ex_type_set`,
`ex_type_program` per chi vuole la lista. **Non ancora** (voce 109): i
programmi non aprono ancora gli indirizzi (`http://`, `ftp://`, `smb://` si
scelgono, nessuno li usa), Firefox non si propone come navigatore
predefinito, e un'applicazione installata da netupdate o ExPackage non si
prende da sola i suoi tipi.

### La risoluzione: Impostazioni > Video (pm 0.031)

Un modo della lista, poi **Prova...** (riavvia in quel modo e chiede conferma
entro 30 s) o **Applica** (dal prossimo avvio). Il modo lo sceglie il
caricatore col BIOS, prima che esistano kernel e dischi, quindi `svga.drv` lo
scrive due volte: la voce `svga` di `/boot/kernel.cfg`, e un byte dentro
`/boot/stage2.bin` che il caricatore legge da sé; a ogni avvio il kernel li
confronta.

Tutti e due i file sono di root e la scrivania gira col suo utente (kernel
0.359): la scrivania chiede una **password come sudo**, la tua se sei in
`/boot/amministratori`, altrimenti quella di root, e diventa root solo
`svga.drv`. Lo stesso per *Spegni lo schermo* qui sotto (`dpms.drv`). Provato
con `tools/prova_root_video.sh`.

### Il salvaschermo: Impostazioni > Video (pm 0.031, voce 116)

Nella scheda **Video** di Impostazioni, sotto i modi dello schermo e una
linea che separa le due parti, il gruppo **Salvaschermo** ha:

- **Tipo**: *Nessuno*, *Scritta* (un testo che attraversa lo schermo, ogni
  passaggio col suo lato, la sua altezza, grandezza e colore) o *Cerchio* (un
  cerchio che rimbalza sui bordi, da 200 pixel di diametro a un punto e
  ritorno, e lascia la scia finche' lo schermo e' coperto, poi ricomincia di
  un altro colore);
- **Dopo**: dopo quanti minuti senza toccare tastiera e mouse (da 1 a 60);
- **Scritta**: cosa scrive *Scritta* (EX-OS se vuoto);
- **Spegni lo schermo**: *Non cambiare* (di serie), *Mai*, o *dopo 5-60
  minuti*; e' `dpms.drv -dopo:M`, scritto nell'avvio con `-install M`
  (`-remove` per *Mai*). Serve root: da utente la scheda dice "solo root" e
  salva il resto;
- **Prova** lo fa partire subito (*Cerchio* se il tipo e' *Nessuno*);
  **Applica** salva.

Il primo tasto, clic o movimento del mouse lo chiude. I minuti li conta il
kernel per tutta la macchina (`attivita_da()`, lo stesso orologio di
dpms.drv), quindi conta anche il mouse mosso sopra la barra. Si lancia anche
da un terminale: `/exwin/bin/salvaschermo cerchio` o
`/exwin/bin/salvaschermo scritta "TESTO"`.

| File | Cosa contiene |
|---|---|
| `~/.exwin/config/pm.cfg` | `salvaschermo = Nessuno\|Scritta\|Cerchio`, `salvaschermo_minuti = N`, `salvaschermo_scritta = TESTO` |

**Non ancora**: al ritorno non chiede la password (da decidere). Lo prova
`tools/prova_salvaschermo.sh` (32 e 64 bit).

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

### Finestre dentro una finestra: le finestre figlie (wserver 0.016)

Un programma può mettere un rettangolo di pixel suo **dentro** una delle sue
finestre: una finestra figlia (`WIN_MSG_CREA_FIGLIA` in
`drivers/wserver/win_proto.h`), quella che X chiama subwindow e Windows HWND
figlio. È una zona di memoria condivisa come ogni finestra, a x, y dell'area
utile della madre, e si disegna subito sopra la madre, tagliata ai suoi bordi.
Segue la madre: si sposta, si copre, si riduce e si chiude con lei. Una figlia
di una figlia si può fare (otto livelli).

! **UNA FIGLIA NON PRENDE INPUT:** un clic o un tasto sopra di lei va alla
finestra di primo livello che c'è sotto, come se la figlia non ci fosse. Che
cosa vuol dire il clic lo decide il programma della madre. Una figlia non ha
cornice, titolo né posto nella barra, e la può fare solo il padrone della
madre.

! **NASCOSTA, NON CHIUSA:** `WIN_MSG_MOSTRA` nasconde una figlia e la
rimostra con lo **stesso numero**. Qt nasconde e rimostra i widget mentre
dispone una finestra. Quando questo chiudeva e rifaceva la figlia, il numero
dato a VLC non era più di nessuno.

! **IL SERVER NON ASPETTA NESSUN CLIENT, NEMMENO PER UNA MISURA NUOVA:** una
casella tiene quattro messaggi. `WIN_MSG_MISURATA` (la zona nuova) e
`WIN_MSG_POSTA` (il posto nuovo) partivano con un invio che, a casella piena,
riprovava per dieci secondi. L'interfaccia di VLC ridimensiona una dozzina di
finestre figlie di fila senza leggere nel frattempo, e il server restava fermo
per minuti. Ora partono senza attesa, come gli eventi da wserver 0.010. Se la
casella è piena il server riprova a ogni giro, e contano solo l'ultima misura
e l'ultimo posto.

**Chi la usa:** i programmi Qt e VLC.
- I widget figli nativi di Qt (`WA_NativeWindow`) sono finestre figlie;
  prima ognuno diventava una seconda finestra, con una barra del titolo sua.
- VLC suona il video **dentro la sua finestra**, come su Linux: l'interfaccia
  Qt dà il numero del suo widget video (la risorsa `exwin` di
  `QPlatformNativeInterface`), e l'uscita video di EX-OS
  (`tools/vlc-exos/src/vout_exos.c`) ne fa una finestra figlia. VLC ci mette
  l'immagine al suo posto, con il nero intorno.
- Con `vlc -I dummy` o `-I ncurses` non c'è un'interfaccia che dia una
  finestra, e il video ha una finestra sua, come prima.

La prova è `tools/prova_figlie.sh 32|64` (`finestreprova -f`). Fotografa una
finestra con tre figlie mentre si sposta, cresce e si chiude.

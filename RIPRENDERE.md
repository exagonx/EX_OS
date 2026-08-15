# DOVE RIPRENDERE — 14 agosto 2026

## Lo stato in una riga

Il **gradino 1 e' cominciato e regge**: un server a finestre in ring 3 su una
console sua, il toolkit ExWin con header C, C++ e FreeBASIC, e la tastiera che
arriva alle finestre. E una shell gira dentro una finestra, su due pipe.

Il **gradino 0** (mappatura framebuffer, memoria
condivisa, poll/select, ritorno al modo testo) e l'input funziona da PS/2,
seriale e USB — tastiera compresa, hub compresi.

## COSA E' COMMITTATO

    fb464ad  "Rimettere il modo testo senza BIOS: il gradino 0 e' chiuso"

! **QUEL COMMIT CONTIENE PIU' DI QUELLO CHE DICE.** E' stato fatto con
gitupdate.sh mentre il lavoro proseguiva, e ha portato dentro TUTTI i sorgenti
nuovi della giornata — shmtest, polltest, testo, mouse, mouseser, uhci,
vgaprova, shm.c, vga_modo3.c — sotto un messaggio che parla solo del modo
testo. Il codice era al sicuro; il messaggio non lo descriveva.

Il resto della giornata sta nel commit successivo, questa volta con un
messaggio che lo descrive davvero:

 - gli **hub USB** in uhci.drv;
 - la **dipendenza dell'ISO** che mancava, piu' la guardia
   `verifica-dipendenze-cd`;
 - i **driver di input spostati sul floppy** (pci, uhci, mouseser);
 - la **ripulitura dei simboli grafici**, questa volta fino in fondo;
 - **le tre immagini che adesso sanno dire quando sono scadute** (floppy, CD
   di sistema, CD strumenti);
 - **xHCI completo**: un mouse USB guida il puntatore, anche dietro un hub;
 - **la meta' comune dello stack USB**, tirata fuori in `drivers/usb/` e
   condivisa dai due controller.

 - **il gradino 1 e' cominciato**: server a finestre in ring 3, toolkit ExWin
   con header C, C++ e FreeBASIC, `SYS_VIDEO_INFO`.

L'albero e' pulito.

## La coda, in ordine

 1. **Il file manager**, poi **l'editor**: sono le due voci del menu che oggi
    puntano a programmi che non esistono. `/exwin/bin/filemgr` e
    `/exwin/bin/edit`.
 2. **Utenti e permessi**, nell'ordine scritto nella voce del 14 agosto: uid
    nel PCB, proprietari in ext2, controlli nella VFS, `install`, `login`.
    Chiude anche il difetto del varco `*.drv`.
 2. **`Ctrl+C` in un terminale in finestra** — oggi non c'e' e non puo'
    esserci cosi': un segnale attraverso una pipe non si manda. Serve un modo
    di chiedere al kernel «interrompi quel processo».
 2. **I lettori di immagini**: JPG, PNG e ICO. La tabella dove aggiungerli e'
    gia' in `exwin.c` e il BMP funziona: ognuno e' un lettore piu' una riga, e
    il server non se ne accorge nemmeno.
 3. **Ridimensionare le finestre**: la zona condivisa ha misura fissa, quindi
    vuole una stretta di mano ordinata. `WIN_EV_MISURA` e' gia' nel protocollo.
 4. **La lista delle regioni sporche**: `WIN_MSG_AGGIORNA` porta gia' una
    `WinRegione` che oggi si ignora. Ha senso quando le finestre saranno tante
    abbastanza da farlo pesare — ed e' la struttura piu' facile da sbagliare
    di un server grafico.

~~Il clic nell'area del client non da' il fuoco~~: **fatto il 14 agosto 2026**,
era `origine()` che sommava la posizione della finestra sullo schermo.

~~Le dipendenze del bersaglio `floppy:`~~, ~~l'audit del CD degli strumenti~~,
~~xHCI~~, ~~la meta' comune dello stack USB~~, ~~gli hub dietro xHCI~~ e
~~Evaluate Context~~: **fatte il 13 agosto 2026**, vedi le voci qui sotto.

Restano aperti, sullo stack USB: **piu' di un livello di hub**, **piu' di un
dispositivo per volta** (lo stesso limite che ha uhci.drv), e **un tetto di
quattro alloggiamenti** in xhci.drv — dichiarato, non scoperto dopo.

## Difetti aperti, dichiarati

- il varco dei driver (`*.drv`) **non e' una barriera**: senza proprietari dei
  file un programma puo' copiarsi in `x.drv`. E' una definizione, non una
  difesa;
- il **blocco temporaneo** della memoria condivisa non c'e': serve solo quando
  ci sara' uno swapper, e il conteggio dei riferimenti — la parte che non si
  puo' aggiungere dopo — e' fatto;
- su ferro vero con una VESA attiva, il ritorno al modo testo puo' non
  bastare: manca l'interfaccia in modo protetto di VBE 2.0.

## Come si prova quello che c'e'

    make -j2 all && make iso-exos
    python3 tools/qemu_drive.py "libctest@200" "/bin/polltest@60" "/bin/shmtest@45"

    # mouse USB dietro un hub
    EXOS_QEMU_EXTRA="-usb -device usb-hub,port=1 -device usb-mouse,port=1.2 \
        -drive file=dist/exos.iso,media=cdrom,if=ide,index=2" \
    python3 tools/qemu_drive.py "/dev/pci.drv &@8" "/dev/uhci.drv &@16" \
        "mouse -n 3@2" "mon:mouse_move 12 4@1" "mon:mouse_move 12 4@4"

    # tastiera USB su una macchina senza 8042: prima mettere in kernel.cfg
    #   modules = kbd, pci, uhci   con pci=/dev/pci.drv e uhci=/dev/uhci.drv
    EXOS_QEMU_EXTRA="-usb -device usb-kbd" python3 tools/qemu_drive.py "hello@10"

! **Dopo aver toccato un driver del CD**: la guardia `verifica-dipendenze-cd`
adesso ferma la costruzione se un `.drv` resta fuori dalle dipendenze
dell'ISO. Se scatta, il rimedio e' aggiungerlo a `DRIVER_SOLO_CD_OUT`.

---

# I dischi rigidi non stanno in dist/ (14 agosto 2026)

    make distclean
    [OK] Dist directory rimossa
         dischi/ resta: i dischi non si distribuiscono

`dist/hd.img` era un disco da 512 MB dentro la directory che **`make distclean`
cancella per intero**. Adesso sta in `dischi/hd.img`.

! **dist/ E' CIO' CHE SI DISTRIBUISCE, E SI RIFA' DA SOLO**: il floppy e i due
CD. Un disco rigido non e' un prodotto, e' uno **stato** — ci sta dentro un
sistema installato con i file di chi lo usa, e rifarlo costa un giro completo
in QEMU perche' formattazione e installazione le fa EX-OS stesso.

! **E IL GUASTO NON SAREBBE SEMBRATO UN GUASTO.** `distclean` si lancia per
liberare spazio dalle ISO, che sono 176 MB: si fa senza pensarci, e nessuno si
aspetta che porti via anche il disco su cui aveva installato il sistema. Un
comando di pulizia che cancella cose che non sa rifare non e' una pulizia.

Provato: creato un `dischi/hd.img`, lanciato `distclean`, il disco c'e' ancora
e `dist/` e' vuota.

# Utenti e permessi: le decisioni prese (14 agosto 2026)

Prese prima di scrivere una riga, perche' toccano kernel, filesystem,
installer e login insieme — e cambiarle dopo vorrebbe dire rifarli tutti.

## Il regime dipende da DA DOVE SI E' AVVIATI

| avvio | chi sei | login |
|---|---|---|
| floppy o CD | **root, senza password** | nessuno |
| sistema installato | l'utente che ha fatto l'accesso | **sempre obbligatorio** |

! **IL SUPPORTO DI AVVIO E' IL PASSAPORTO, ed e' una scelta consapevole.** Chi
ha in mano il floppy o il CD ha in mano la macchina: chiedergli una password
sarebbe teatro, perche' potrebbe montare il disco e leggerlo comunque. Il vero
motivo pero' e' un altro: **da li' si lancia `install`**, e un installatore che
chiede una password prima che l'utente esista non ha modo di funzionare.

! **E SUL SISTEMA INSTALLATO IL LOGIN NON HA ECCEZIONI.** Nessun «se il file
degli utenti non c'e' allora entra libero»: un'eccezione del genere si attiva
proprio nel caso in cui il file e' stato cancellato, cioe' quando si vorrebbe
che non si attivasse.

## I permessi vogliono ext2, e FAT resta compatibilita'

! **FAT12, 16 e 32 NON HANNO DOVE SCRIVERE UN PROPRIETARIO.** Non e' una
limitazione da aggirare: e' il formato. Inventarci sopra dei permessi darebbe
una sicurezza che non esiste — e la sicurezza finta e' peggio della sua
assenza, perche' ci si conta.

Quindi: **i permessi sono veri su ext2**, e una FAT montata dichiara di non
averne. `install` pretende ext2 per il sistema; le FAT restano per scambiare
file con altre macchine.

## `install` crea root con userid e password

Durante l'installazione si chiede la creazione dell'utente **root**, con
identificativo e password. Da quel momento il sistema installato chiede
l'accesso a ogni avvio.

## Cosa manca, ed e' un sottosistema

Il pezzo che c'e': `/bin/login` esiste, cicla, legge `/boot/utenti`, e quando
la shell esce si torna alla richiesta di accesso.

Il pezzo che NON c'e', e va costruito in quest'ordine:

 1. **un `uid` nel PCB**, ereditato dallo `spawn` — oggi in `sched.h` non c'e'
    niente del genere;
 2. **proprietario e permessi per file in ext2**, che i campi ce li ha gia':
    e' il nostro driver che li ignora;
 3. **i controlli nella VFS** su `open`, `exec`, `unlink`, `rename`;
 4. **`install`** che crea root e scrive `/boot/utenti` sul disco;
 5. **`login`** che diventa obbligatorio quando la radice e' ext2.

! **E QUESTO E' CIO' CHE RENDE IL VARCO `*.drv` UNA BARRIERA VERA.** Oggi e'
una definizione: senza proprietari dei file, un programma puo' copiarsi in
`x.drv` e ripartire da li'. Con i proprietari, `/dev` appartiene a root e la
copia non si puo' fare. E' lo stesso lavoro, e chiude un difetto dichiarato da
giorni.

# La scrivania: /exwin, barra e menu di avvio (14 agosto 2026)

    pm: scrivania attiva, 3 applicazioni nel menu

    scrivania           #305a8a 95%   la scrivania
                        #c0c0c0  5%   la barra, 28 pixel su 600
    premuto «Avvio»     grigi 22031 -> 37672   (+15641 = il menu, 220x80)

Barra delle applicazioni in basso, pulsante di avvio, menu con l'elenco delle
applicazioni.

## ! LE APPLICAZIONI GRAFICHE NON STANNO IN /bin

    /exwin/bin    le applicazioni
    /exwin/lib    l'elenco, e cio' che condividono
    /exwin/dev    i pezzi grafici che vogliono il varco dei driver

I programmi di `/bin` si lanciano da una shell e parlano con un terminale;
questi vogliono il server a finestre, e lanciati da una shell senza server non
fanno niente. Mescolarli vorrebbe dire un `ls /bin` in cui meta' dei nomi non
si puo' usare li' dove si sta guardando. E' la stessa ragione per cui i driver
stanno in `/dev`.

## ! L'ELENCO E' UN FILE, NON UNA TABELLA COMPILATA

`/exwin/lib/applicazioni.txt`, una riga per applicazione: `nome | percorso`.
Aggiungerne una e' una riga. Un elenco dentro il binario vorrebbe dire rifare
il program manager per ogni applicazione nuova — **e chi installa un programma
non ha i sorgenti**.

! **SI CERCA IN DUE POSTI, E L'ORDINE CONTA.** Su un sistema installato
l'albero sta in `/exwin`; avviando dal CD sta sotto `/cdrom`. Alla prima prova
il menu era vuoto e non diceva perche': cercare solo il primo posto vuol dire
una scrivania senza applicazioni ogni volta che si prova il CD. E i percorsi
delle voci seguono l'elenco: trovato su `/cdrom`, anche i programmi stanno li'.

## ! LA BARRA STA SOPRA A TUTTO, E IL SERVER HA UN BIT NUOVO

`WIN_ST_SOPRA` / `EX_SOPRA`: e' lo sfondo girato dall'altra parte. Se una
finestra qualunque potesse coprire la barra, l'unico modo di tornare al menu
sarebbe spostare quella finestra — e con una a schermo intero non si potrebbe
affatto.

## ! IL MENU E' UNA FINESTRA, NON UN DISEGNO SULLA BARRA

Cosi' sta sopra alle altre senza casi particolari, si chiude distruggendola, e
i clic sulle voci arrivano come `EXM_COMANDO` — con lo stesso meccanismo di
tutto il resto, invece che con un calcolo di coordinate a mano.

## Cosa manca

Le tre voci del menu puntano a `filemgr`, `edit` e `term`, che **non esistono
ancora**. Premerle scrive sulla seriale che non si e' riusciti ad avviarle: il
prossimo lavoro e' il file manager, poi l'editor.

# Il terminale in finestra, e la shell che legge altrove (14 agosto 2026)

    term: shell '/bin/sh' PID 11, fd_in 4 fd_out 5
    term: read rende 29                        <- il prompt ARRIVA
    term: write 3 byte -> 3; waitpid(11) = 0   <- scritto, e la shell e' VIVA

Il controllo `"terminale"` c'e': una finestra con dentro una griglia di testo,
una shell avviata su due pipe, l'editing di riga fatto dal controllo, e il
ciclo dei messaggi che aspetta **mailbox IPC e pipe nella stessa `poll()`** —
il caso per cui `SYS_POLL` e' nata.

Quello che si batte compare. Quello che la shell risponde **no**.

## ! LA CAUSA NON E' NELLE PIPE, ED E' MISURATA

Le tre righe qui sopra dicono tutto: la shell **scrive** nella nostra pipe (29
byte di prompt), noi le **scriviamo** dentro, e `waitpid` con `WNOHANG` rende 0
— cioe' il figlio e' vivo. Le pipe funzionano nei due versi.

## ! LA SHELL HA DUE STRADE ANCHE PER L'INPUT

L'uscita va sul descrittore 1, quindi il prompt arriva dove gli diciamo. Ma
l'ingresso interattivo **non passa da `read()`**: `riga_interattiva()` in
`bin/sh/shell.c` chiama `kbd_trova()`, mette la propria console in modo raw e
prende i tasti dal **servizio `kbd` via IPC**, per avere editing di riga e
cronologia.

Il nostro `"ls\n"` resta nella pipe, non letto da nessuno: la shell sta
aspettando tasti da un'altra parte.

## ! E' LA TERZA VOLTA CHE UN DIFETTO HA QUESTA FORMA

| | il sintomo indicava | la causa era |
|---|---|---|
| la tastiera del server | il modo raw di `kbd.drv` | il tty del kernel, che ha una strada sua |
| il clic che non dava il fuoco | gli eventi del mouse | `origine()`, cioe' aritmetica |
| il terminale muto | le pipe, o il toolkit | la shell, che legge dove vuole lei |

Ogni volta il sintomo indicava il pezzo NUOVO e la causa stava in un pezzo
VECCHIO che aveva due strade. Vale la pena tenerlo a mente prima di guardare
il codice appena scritto.

## La correzione: una condizione, e la rete c'era gia'

    pixel bianchi prima:  664
    dopo 'ls':           1135      delta +471

L'eco delle due lettere sono ~34 pixel: 471 e' l'ELENCO, disegnato dentro la
finestra. **Una shell gira in una finestra di EX-OS.**

`riga_modifica()` adesso comincia cosi':

    if (!stdin_e_console()) return -1;

! **LA RETE ERA GIA' TESA, BASTAVA FARLA SCATTARE.** Il ciclo del prompt
ripiegava gia' su `sh_read(STDIN, ...)` quando `riga_modifica()` rendeva -1 —
era li' per il caso «il servizio kbd non risponde». Serviva solo dirle che
anche «stdin non e' una console» e' uno di quei casi. Zero codice nuovo nel
percorso di lettura.

! **LA DOMANDA E' `ioctl`, NON UNA SYSCALL NUOVA.** `TIOCGWINSZ` riesce solo su
un terminale e rende ENOTTY su tutto il resto, pipe comprese: e' la stessa
prova che fa `isatty()` nella libc, rifatta dentro `shell.c` perche' quella
shell chiama le syscall dirette e non si porta dietro la libc.

! **SI PERDONO CRONOLOGIA E FRECCE, ED E' GIUSTO COSI'.** Dietro una pipe non
esistono: le fa il controllo «terminale», che l'editing di riga se lo fa da
solo. Quello che non si perde e' la shell.

Provato in tutt'e due i versi: la shell sulla console continua ad avere
cronologia ed editing (libctest 294/294 passa battendo i comandi), e quella in
finestra risponde.

## Cosa manca comunque al terminale, oltre a quello

! **IL Ctrl+C NON C'E', E NON PUO' ESSERCI COSI'.** Mandare un segnale
attraverso una pipe non si puo': finche' non c'e' un modo di chiedere al kernel
«interrompi quel processo», una shell in finestra non si interrompe. Chi la usa
deve saperlo.

! **E UNA PIPE NON E' UN tty**, che e' il motivo per cui l'eco, il Backspace e
il cursore li fa il controllo. Sono le cose che farebbe la line discipline, e
dietro una pipe non c'e' nessuna line discipline.

# Nascere su un'altra console (14 agosto 2026)

    ex-os:/> /cdrom/dev/wserver.drv -v -c 1 &
    wserver: riparto sulla console 1 (PID 9); con Alt+F2 ci si va

Il blocco EXTRA di `SYS_SPAWN` ha due campi in piu': `flag` e `console`. Un
figlio puo' nascere su una console diversa da quella del padre, che e' il
prerequisito per avere **il server grafico su una console e la shell su
un'altra**, con Alt+Fn per passare.

## ! IL VALORE SI GUARDA SOLO SE IL FLAG C'E', e non e' pedanteria

Se «console 0» fosse il modo di dire «eredita», un chiamante che azzera la
struttura con `memset` — cioe' il modo normale di riempirla — chiederebbe la
console 0 senza volerlo. Con il flag, azzerare vuol dire ereditare, che e' il
comportamento di sempre. E la libc adesso **azzera il blocco prima di
riempirlo**: da quando c'e' un campo `flag`, lasciarlo come capita sullo stack
vuol dire chiedere per caso una console che non si voleva.

## ! LA MAGIA CAMBIA QUANDO CAMBIA LA DISPOSIZIONE: 'SPNY' -> 'SPNZ'

E' la terza volta. Un blocco compilato per la forma vecchia e' piu' corto, e
leggerlo con la disposizione nuova vorrebbe dire prendere per «console»
qualcosa che il chiamante non ha nemmeno scritto. Con la magia nuova il kernel
non lo riconosce e lo ignora — il modo meno dannoso di sbagliare.

## ! E' UNA FUNZIONE IN PIU', NON UN PARAMETRO IN PIU'

`spawn_su_console()` accanto a `spawn_ex()`, che adesso e' un involucro con
console = -1. Cambiare la firma di una funzione che i programmi gia' chiamano
vuol dire ricostruire tutto cio' che la usa — ed e' esattamente cio' che il
blocco EXTRA e' stato inventato per evitare.

## SpawnExtra e' entrata nell'impronta ABI, e mancava

Attraversa il confine nel verso opposto a `ShmZona`: la riempie il chiamante e
la legge il kernel. Ed e' peggio, perche' un campo letto storto qui non da' un
valore sbagliato — da' una **redirezione** sbagliata, cioe' un programma che
scrive nel file di un altro. Aveva gia' cambiato forma due volte senza che la
guardia se ne accorgesse.

## Cosa e' provato e cosa NO

| | |
|---|---|
| il figlio nasce sulla console chiesta | si': il padre riporta il PID, e il kernel avvisa se la console non esiste — non l'ha fatto |
| lo spawn normale non e' cambiato | si': `hello`, `uname`, libctest 294/294 |
| il server dipinge su quella console | si': dopo Alt+F2 la scrivania al 100%, dopo Alt+F1 il prompt |
| e ci si aprono finestre | si': 8 colori, area del client al 12%, barra e intestazione al 2,9% |

## La risoluzione si sceglie a costruzione: `make SVGA=800x600`

    make SVGA=800x600        una volta, e da li' in poi vale per tutti
    make SVGA=                si torna al testo

Serviva perche' l'impostazione **vive dentro l'immagine** — la scrive
`/dev/svga.drv` dentro `LOADER.BIN` — quindi ogni floppy ricostruito ripartiva
in modo testo, e il server grafico moriva dicendo «lo schermo e' in modo
TESTO». E' successo due volte in un giorno.

! **IL PREDEFINITO RESTA IL TESTO.** Chi costruisce EX-OS non deve ritrovarsi
in grafica senza averlo chiesto, e chi lavora sulla seriale non se ne
accorgerebbe nemmeno.

## ! DUE DIFETTI NELLA MIA STESSA CORREZIONE, TROVATI PROVANDOLA

**1. La modalita' non era fra le prerequisite di Stage 2.** `make SVGA=800x600`
e poi `make` dava ancora l'800x600: `loader.asm` non era cambiato e make non ha
modo di sapere che e' cambiata una **variabile**. E' lo stesso modo di
sbagliare del floppy e delle due ISO, in un posto nuovo. Il rimedio e' un segno
il cui NOME contiene la modalita': cambiandola il file non c'e' piu' e Stage 2
si rifa'.

**2. Bisognava passare `SVGA=` a OGNI comando.** Bastava un `make iso-exos`
senza, e Stage 2 tornava in modo testo disfacendo la scelta — **in silenzio**,
perche' nessuno dei due comandi sbagliava. Adesso la scelta si scrive in
`.svga` e vale finche' non la si cambia.

! **E STA FUORI DA `build/`**, o un `make clean` se la porterebbe via: e' una
scelta di chi costruisce, non un prodotto della costruzione. Stessa ragione per
cui i dischi non stanno in `dist/`.

Provato in tutt'e tre i versi: si sceglie, resta ai comandi successivi, e si
torna indietro dicendolo.

## ! E LA TASTIERA SI E' CHIUSA DA SOLA, SENZA TOCCARE kbd.c

    con il server sulla console 1, battuto «XY» dentro una casella:

    nero   +35        le due lettere, disegnate in nero sul bianco
    bianco -35        gli stessi pixel, tolti dal fondo
    blu     +0        il bordo del fuoco non si muove
    testo di console: NESSUNO   (prima erano +4776 pixel)

Il conflitto non era mai stato fra il modo raw e la tastiera: era che **sulla
stessa console c'erano due lettori** — il server via IPC e la shell via
`read()` sul tty del kernel. Separate le console, il secondo non c'e' piu' e il
primo riceve.

! **LA CORREZIONE GIUSTA NON ERA DOVE CERCAVO.** Avevo passato una serata su
`kbd.c`, sul modo raw, sulla READLINE pendente e sul type-ahead — tutte cose
vere e nessuna era la causa. La causa era «chi altro sta leggendo quella
console», e si e' risolta cambiando **dove nasce un processo**, che sembrava
un'altra faccenda.

E' la stessa forma dei difetti di questa settimana: il sintomo indicava un
posto, la causa stava un piano piu' in basso.

## ! LO STRUMENTO CIECO, E COSA HA DETTO APPENA APERTO GLI OCCHI

All'inizio non dipingeva, e non si capiva perche': i messaggi del server
finivano sulla console 1, cioe' proprio quella che non si riusciva a guardare
finche' il problema non era risolto.

Da li' `SYS_LOG` (247) e `log_seriale()`: **una riga che arriva anche se
nessuno guarda la tua console.** Non e' un doppione di `printf` — `printf`
scrive sulla console del processo, e se quella non e' a video quel messaggio
non lo legge nessuno MAI.

Ha risolto il mistero al primo colpo:

    [PID 8] wserver: lo schermo e' in modo TESTO

Il figlio **non moriva per la console**: moriva perche' il floppy era stato
ricostruito e aveva perso l'impostazione `svga = 800x600`, che vive dentro
l'immagine e non nel repository. Il messaggio c'era da sempre — andava solo
dove non si poteva leggere.

! **E klog NON BASTAVA.** Passa dalla console CORRENTE; solo `vga_putchar()`
specchia sempre sulla seriale. La prima versione di `sys_log` usava klog e non
si sentiva niente, il che sembrava un difetto della syscall nuova.

! **E IL PRIMO MESSAGGIO DICEVA «console 0» DI UN PROCESSO SULLA 1**, perche'
leggevo `g_console` prima di assegnarlo. Corretto: un numero sbagliato dentro
una diagnostica costa piu' di nessun numero, perche' lo si crede.

Aggiunto per strada: il server **ridipinge quando la sua console torna
visibile**. Senza, Alt+Fn e ritorno lasciava il prompt della shell disegnato
sopra le finestre — la console di testo scrive nello stesso framebuffer.

# Il server a finestre: il gradino 1 e' cominciato (13 agosto 2026)

    wserver: schermo 800x600 a 32 bit, framebuffer fisico 0xfd000000
    wserver: servizio 'wserver' attivo
    wserver: finestra 1 per il PID 9, 360x220 «Prova del toolkit»
    winprova: finestra aperta.

    angolo del bordo PRIMA: (79, 41)
    angolo del bordo DOPO : (229, 161)      spostata di (150, 120)

**Una finestra con intestazione, etichette, caselle di testo, separatore,
riquadro e due pulsanti, presa per la barra del titolo e spostata.** Lo
spostamento e' ESATTAMENTE quello iniettato dal monitor di QEMU: non
«sembra», e' lo stesso numero.

## Cosa e' nato

| | |
|---|---|
| `SYS_VIDEO_INFO` (246) | dov'e' il framebuffer e che forma ha |
| `drivers/wserver/` | il server: compone, muove il puntatore, trascina |
| `lib/exwin/` | il toolkit: `exwin.h`, `exwin.hpp`, `exwin.bi` |
| `bin/winprova/` | la prova: tutti e sei i controlli |

## ! LO STILE E' Win32, E LA RAGIONE NON E' IL GUSTO

Il ciclo dei messaggi **e' gia' la forma di questo sistema**: `ex_prendi_msg()`
e' `poll()` su `FD_IPC` piu' `ipc_recv`. Un ciclo principale in stile glib
andrebbe costruito SOPRA questo, non al posto suo. In piu' non c'e' un sistema
a oggetti da scrivere — segnali, tipi, conteggio dei riferimenti sono un
mini-glib da mantenere per sempre — e **FreeBASIC si lega senza trucchi**: una
maniglia e' un intero, un messaggio e' una struttura di interi.

## ! IL SERVER NON DISEGNA IL CONTENUTO, LO COMPONE

Ogni finestra e' una zona di memoria condivisa che il client riempie. Tre
ragioni, e la terza spiega perche' la memoria condivisa e' arrivata prima:

 1. un client che sbaglia rovina la PROPRIA finestra, non lo schermo;
 2. i formati di immagine restano **fuori** dal server. Un lettore JPG e'
    migliaia di righe che interpretano dati venuti da fuori: nel server
    sarebbe un difetto di tutte le applicazioni insieme;
 3. una finestra 640x480x32 e' 1,2 MB: via IPC sarebbero ~800 messaggi a
    fotogramma. Non e' lentezza, e' la struttura sbagliata.

! **I PIXEL SONO SEMPRE ARGB A 32 BIT, ANCHE SE LO SCHERMO NON LO E'.** La
conversione sta in un posto solo — il server — ed e' l'unico che sa se lo
schermo e' a 16, 24 o 32 bit.

## ! TRE DIFETTI DEL KERNEL, TROVATI PERCHE' QUALCUNO HA PROVATO A USARLO

**1. `SYSCALL_COUNT` valeva 246 e la syscall nuova era la 246.** Succedevano
DUE cose insieme: la syscall veniva rifiutata come inesistente, e la riga che
la registra scriveva **un elemento oltre la fine dell'array**. Il sintomo era
«video_info non risponde», che non somiglia per niente a una scrittura fuori
limite.

**2. `mmio_map` aveva un tetto di 1 MB**, «piu' di ogni BAR sensato». Vero per
i registri di una scheda di rete, falso per il primo caso d'uso scritto in
`DIREZIONE.md`: un 800x600 a 32 bit sono 1,83 MB.

**3. Il server leggeva la mailbox in DUE POSTI.** `mouse_leggi()` aveva una
`ipc_recv` sua e prendeva **qualunque** messaggio trovasse — compresa la
richiesta di un client, che spariva interpretata come stato del mouse.

! **ED E' IL DIFETTO CHE INSEGNA DI PIU'.** Il sintomo era un'applicazione che
diceva «il server non risponde» **una volta su tre**, e solo se avviata in
SFONDO: in primo piano la shell e' ferma, i tempi cambiano e la corsa la
vinceva sempre il lettore giusto. Prima di trovarlo ho corretto due cose che
non erano la causa — la pazienza del client e la velocita' del compositore —
e tutt'e due «miglioravano» la percentuale di successo. **Un difetto di corsa
si nasconde dietro ogni cambiamento che sposta i tempi.**

La regola, scritta perche' varra' anche dopo: **una mailbox, un solo posto che
la legge.** Due funzioni che pescano dalla stessa coda si rubano i messaggi, e
chi perde dipende dal caso.

## ! E UNA CORREZIONE CHE STAVO PER FARE ERA PEGGIO DEL DIFETTO

Per il client che si arrendeva avevo scritto un ritenta-la-richiesta. Se la
prima risposta fosse solo IN RITARDO, il secondo invio avrebbe fatto creare al
server una **seconda finestra** di cui nessuno sa niente — che resta sullo
schermo e non si chiude piu'. Si manda una volta sola e si aspetta a lungo,
non il contrario.

## Le prove

| | |
|---|---|
| la finestra si disegna | 8 colori distinti, e i conteggi combaciano con la geometria: il bordo e' 1200 pixel su un perimetro di 1204 |
| in sfondo e in primo piano danno la STESSA immagine | 393060 / 62693 / 12019 / 8490 pixel, identici |
| si trascina | spostata di (150, 120), lo spostamento esatto iniettato |
| l'avvio in sfondo e' stabile | 4 corse su 4 dopo la correzione della mailbox |

libctest 294/294, `polltest`, `shmtest`, e le quattro prove USB: tutte a posto
dopo i cambiamenti al kernel.


## La tastiera: la strada e' aperta, il traguardo NO

    wserver: console 0 in modo raw          <- verificato, ci arriva

`KBD_MSG_SETMODE` passa e la console entra davvero in raw. Ma **i tasti
continuano ad arrivare alla shell**: battendo con il server attivo, il testo
della console compare sopra le finestre (misurato — 4673 pixel neri in piu'
nella foto, che sono caratteri di console disegnati nello stesso framebuffer).

! **E NON E' LA PARTE CHE HO SCRITTO A NON FUNZIONARE.** Il server chiede il
modo raw e lo ottiene; il pezzo che manca e' cosa succede alla `READLINE` che
la shell ha GIA' in attesa quando il modo cambia. Il modo si applica alla
console, ma una richiesta gia' in coda e' stata fatta con le regole di prima.

## ! UN DIFETTO SOLO CON DUE SINTOMI: LE COORDINATE DEI FIGLI

`origine()` sommava anche la posizione della finestra **sullo schermo**, ma i
figli sono relativi all'area del client, che comincia a (0,0) dentro la zona di
pixel condivisa. Da li' venivano DUE cose che sembravano scollegate:

 - i controlli **disegnati spostati** di quanto la finestra distava
   dall'angolo dello schermo. Con la finestra a (80,60) sembrava quasi giusto:
   l'intestazione era larga e il taglio a destra si notava poco;
 - il **clic che non trovava niente**, perche' il server manda coordinate gia'
   relative al client e li' si cercava 80 pixel piu' in la'. Sembrava un
   problema di eventi, ed era aritmetica.

I numeri, dopo la correzione:

    l'intestazione non e' piu' tagliata   +1760 pixel blu = (360-280) x 22
    il clic da' il fuoco                  +536 pixel blu, contro un perimetro
                                          di 2x(250+22) = 544 meno gli angoli

! **CHE SEMBRASSE QUASI GIUSTO E' LA PARTE ISTRUTTIVA.** La prima foto era
stata guardata e i conteggi «tornavano»: tornavano perche' li avevo confrontati
con una geometria che davo per buona, non con quella dichiarata nel codice
dell'applicazione. Un controllo che parte da cio' che si vede conferma cio' che
si vede.

Cio' che invece resta verificato dopo questi cambiamenti: libctest 294/294,
`polltest`, e il trascinamento ancora esatto.

## ! LA TASTIERA: LA CAUSA E' TROVATA, ED E' PIU' GROSSA DI UN FLAG

Non e' il modo raw e non e' il server. **Ci sono due strade per la tastiera, e
il modo raw ne governa una sola.**

Le prove, in ordine:

    DIAG: mia=0 visibile=0 fg=4 iopid=8      la console e' quella giusta
    wserver: console 0 in modo raw           il raw viene applicato
    DIAG: READKEY -> PID 3 console 0         e accettata: se fosse cooked,
                                             kbd.c stampa «ignorata». Non l'ha
                                             stampato
    AB                                       ...e la shell fa ECO ed ESEGUE

L'eco esiste **solo** in cooked. Quindi il raw era attivo su `g_c[0]` e i tasti
sono arrivati alla shell lo stesso.

! **PERCHE': LA SHELL NON PARLA CON IL SERVIZIO 'kbd'.** Legge con
`sh_read(STDIN, ...)`, cioe' `read()` sul descrittore 0, che passa da
`sys_read` e dal **tty del kernel**. Il servizio `kbd` consegna le righe con
`KBD_MSG_LINE` via IPC a chi ha chiesto con `KBD_MSG_READLINE` — ed e' la
strada che usa `gfedit`, non la shell.

Il modo raw di `kbd.drv` spegne la propria line discipline. **Non spegne quella
del kernel**, che e' quella che alimenta la shell.

! **E QUESTO SPIEGA PERCHE' `gfedit` FUNZIONA E IL SERVER NO.** Sembravano lo
stesso caso d'uso — un programma a schermo intero che prende i tasti — e non lo
sono: `gfedit` gira in PRIMO PIANO, quindi la shell non sta leggendo. Il server
grafico convive con una shell che sta aspettando una riga, e li' le due strade
si vedono.

## Cosa serve, e non e' una riga

Tre strade possibili, in ordine di onesta':

 1. **il server prende una console tutta sua**, dove non gira nessuna shell.
    E' la piu' pulita e usa cio' che c'e' gia' (`console_switch`, Alt+Fn), ma
    vuole un modo di far nascere un processo su una console diversa da quella
    del padre — che oggi non c'e': `child->console = parent->console`;
 2. **il tty del kernel impara a tacere** su una console messa in raw, cioe' il
    raw diventa una proprieta' della CONSOLE e non del solo `kbd.drv`. E' il
    posto giusto per l'informazione, ed e' una modifica al kernel;
 3. il server gira in primo piano come `gfedit`, e allora funziona subito — ma
    vuol dire che non si puo' avere una shell mentre la grafica e' accesa, che
    e' proprio cio' che il gradino 1 vuole permettere.

Non ho scelto: e' una decisione di struttura, non un difetto da correggere.

## Cosa non fa ancora, dichiarato

- ~~le caselle di testo non si possono riempire~~: **fatto il 14 agosto 2026**,
  bastava dare al server una console sua. Resta che `kbd_set_mode()` viene chiamata davvero — il prossimo passo e'
  guardare cosa fa `kbd.c` della READLINE che la shell ha gia' in attesa e del
  type-ahead accumulato prima del cambio;
- **niente ridimensionamento** e nessuna lista delle regioni sporche: si
  ricompone tutto quando qualcosa cambia;
- **solo BMP**. La tabella dei lettori in `exwin.c` e' gia' quella giusta:
  JPG, PNG e ICO sono una riga in piu' li' dentro, e il server non se ne
  accorge nemmeno.

# Gli hub dietro xHCI, e un contesto che si corregge (13 agosto 2026)

    xhci: dispositivo  USB 1.10  venditore 0409 prodotto 55aa  maxp0 8
    xhci: hub con 8 porte
    xhci: dispositivo  USB 2.00  venditore 0627 prodotto 0001  maxp0 8
    xhci: mouse USB «boot», endpoint 1, pacchetto 4
    xhci: servizio 'mouse' attivo (mouse USB)

      dx   +12  dy    +4   totale +12,+4
      dx   +12  dy    +4   totale +24,+8

**Un mouse dietro un hub guida il puntatore anche su xHCI.** Le quattro
combinazioni, tutte con numeri decisi prima:

| | |
|---|---|
| UHCI diretto | +30,+12 |
| UHCI dietro un hub | +24,+8 |
| xHCI diretto | +30,+12 |
| xHCI dietro un hub | +24,+8 |

## ! SU xHCI UN HUB VUOLE DUE COSE CHE SU UHCI NON SERVIVANO

Su UHCI un dispositivo dietro un hub si indirizzava **esattamente come uno
diretto**: era il driver a tenere il conto degli indirizzi, e all'hardware non
importava da dove arrivassero i pacchetti. Qui e' il **controller** a
instradare, e per farlo gli servono due cose che non ha modo di indovinare:

 1. **che quel dispositivo sia un hub.** Finche' non gli si accende il bit
    «hub» nel contesto dello slot e non gli si dice quante porte ha, per lui
    e' un dispositivo come un altro — e la stringa di percorso di qualcosa che
    sta dietro un hub che lui non sa essere un hub non porta da nessuna parte;
 2. **da quale porta dell'hub si arriva**, cioe' la **stringa di percorso**:
    quattro bit per livello, fino a cinque livelli, dentro il contesto dello
    slot. Senza, il comando di indirizzamento **riesce** e i trasferimenti
    finiscono nel vuoto.

! **E LA PORTA DELLA RADICE RESTA QUELLA DELL'HUB.** Non e' la porta dell'hub
a cui il dispositivo e' attaccato: quella sta nel percorso. Confonderle da' un
indirizzamento che riesce e un dispositivo che non risponde — che e' il modo
piu' scomodo di sbagliare.

! **IL TRADUTTORE VA DICHIARATO, e serve solo a chi va piano dietro un hub
veloce.** Un mouse full speed attaccato a un hub high speed parla attraverso il
«transaction translator» dell'hub: senza dirlo al controller, la banda viene
programmata per la velocita' sbagliata.

## ! UN CONTESTO PER ALLOGGIAMENTO, e con gli hub non e' piu' un lusso

Un mouse dietro un hub sono **due dispositivi indirizzati insieme**: l'hub, a
cui si continua a parlare per interrogare le sue porte, e il mouse. Con un
contesto e un anello soli, il secondo indirizzamento cancellava il primo — e
il sintomo sarebbe stato un hub che smette di rispondere **appena** ci si
trova attaccato qualcosa.

Il tetto e' quattro alloggiamenti, ed e' **nostro e non del controller**: il
driver lo dice invece di scrivere fuori dalla zona DMA.

## Evaluate Context: il contesto si corregge invece di rifarlo

    xhci: l'endpoint 0 vuole 64 byte invece di 8: lo correggo

! **A FULL SPEED IL maxPacketSize NON SI PUO' SAPERE PRIMA.** La specifica
prescrive di partire da 8, leggere i primi byte del descrittore e poi
**rivalutare** il contesto. Solo a high speed il valore e' noto in anticipo.
Senza questo comando, ogni trasferimento piu' lungo di 8 byte verso un
dispositivo full speed si spezza in modo che **non somiglia** a un errore di
dimensione.

! **SI PARTE DA CIO' CHE IL CONTROLLER HA GIA' SCRITTO**, cambiando un campo
solo. Ricostruire il contesto da zero vuol dire indovinare tutti gli altri
campi — e indovinarne uno male e' peggio che non correggere niente.

## ! E' STATO PROVATO CON UN GUASTO A COMANDO, perche' su QEMU non scatta

Un mouse su qemu-xhci va a high speed, dove il valore dichiarato e' gia'
giusto: la correzione non sarebbe mai stata eseguita, e **un codice di
emergenza mai eseguito e' un codice che non si sa se funziona** — la stessa
regola per cui esiste `/dev/vgaprova.drv`.

Dichiarando di proposito 8 byte dove ne servono 64:

    xhci: l'endpoint 0 vuole 64 byte invece di 8: lo correggo
    xhci: dispositivo  USB 2.00  venditore 0627 prodotto 0001  maxp0 64
      dx   +15  dy    +6   totale +30,+12

Il mouse funziona lo stesso, e i 18 byte del descrittore lungo — che con un
endpoint 0 da 8 byte dichiarato male sarebbero arrivati storti — arrivano
giusti. **E' la correzione che li fa arrivare**, non la fortuna.

# La meta' comune dello stack USB, e un mouse su xHCI (13 agosto 2026)

    ex-os:/> /dev/xhci.drv &
    xhci: dispositivo  USB 2.00  venditore 0627 prodotto 0001  maxp0 64
    xhci: mouse USB «boot», endpoint 1, pacchetto 4
    xhci: servizio 'mouse' attivo (mouse USB)

    ex-os:/> mouse -n 3
      dx   +15  dy    +6   totale +15,+6
      dx   +15  dy    +6   totale +30,+12

**Un mouse USB su xHCI guida il puntatore di EX-OS**, e lo fa con lo stesso
codice di enumerazione che gia' serviva l'UHCI.

## Cosa e' andato in `drivers/usb/usb_comune.c`

| resta nel driver | e' diventato comune |
|---|---|
| registri, anelli, TD, contesti | descrittori di dispositivo e configurazione |
| assegnare l'indirizzo (UHCI) o l'alloggiamento (xHCI) | la catena della configurazione, scorsa con le lunghezze |
| l'endpoint di interruzione, che i due fanno in modi opposti | HID «boot»: SET_CONFIGURATION, SET_PROTOCOL, SET_IDLE |
| | le richieste di classe agli hub |
| | la tavola da uso HID a scancode del set 1 |
| | il rapporto del mouse |

! **LA CUCITURA E' UN PUNTATORE A FUNZIONE**, e la scelta va spiegata. La
meta' comune ha bisogno di UNA cosa sola dal controller: saper fare un
trasferimento di controllo. Chiederla con un `UsbControllo` — invece che con
un simbolo che ogni driver definisce a modo suo — vuol dire che
`usb_comune.c` **non nomina nessun controller**, non ha un `#ifdef`, e si
compila identico dentro tutt'e due i driver.

! **E `dev` E' OPACO DI PROPOSITO.** Su UHCI e' l'indirizzo USB, che assegna
il driver; su xHCI e' il numero di alloggiamento, che assegna il controller.
Sono due cose che non si assomigliano nemmeno, e l'unico modo di scrivere una
volta sola il codice che ci sta sopra e' non guardarci dentro.

! **NON E' UNA LIBRERIA E NON C'E' UN `.a`.** Ogni driver e' un eseguibile
statico a se', quindi il file si compila una volta per driver con un nome di
oggetto diverso — lo stesso trattamento che ha gia' `libc.c`, per la stessa
ragione.

## ! LA PROVA DELL'ESTRAZIONE NON E' CHE xHCI FUNZIONI: E' CHE UHCI NON SIA CAMBIATO

Una riscrittura che fa funzionare il caso nuovo e rompe quello vecchio in modi
sottili e' il difetto tipico di questo lavoro, e si vede mesi dopo. Percio' le
prove che contano sono quelle di prima, rifatte identiche:

    UHCI, mouse diretto        totale +15,+6  ->  +30,+12
    UHCI, mouse dietro un hub  totale +12,+4  ->  +24,+8
    xHCI, mouse                totale +15,+6  ->  +30,+12

Gli stessi numeri di ieri, con gli stessi comandi.

## ! UN TRB SI CONSUMA, UN TD NO — ed e' la differenza che conta

Su UHCI il TD dell'endpoint di interruzione resta nella lista dei frame **per
sempre**: il controller lo ripercorre mille volte al secondo e ritenta da
solo. Su xHCI un TRB e' consumato appena il dispositivo risponde, quindi la
lettura **va rimessa ogni volta**. Il ciclo di servizio ne tiene sempre una in
coda, che e' l'equivalente della regola gia' imparata su UHCI: non lasciare
mai la coda vuota.

## ! L'INTERVALLO NON E' IL `bInterval` DEL DESCRITTORE

L'xHCI vuole il **logaritmo in base 2 dei microframe**; il descrittore, per
full e low speed, lo dice in **millisecondi**. Passarlo pari pari da' un
endpoint interrogato migliaia di volte piu' spesso o piu' di rado del dovuto:
nel primo caso si vede come bus occupato, nel secondo come un mouse che
scatta.

## ! UN ENDPOINT SI DICHIARA, NON SI USA E BASTA

Su UHCI bastava mettere un TD nella lista dei frame. Qui il controller deve
prima SAPERE che quell'endpoint esiste, con che pacchetto e ogni quanto —
glielo dice `Configure Endpoint` — e senza, il campanello suona nel vuoto. E
il contesto dello slot va **ricopiato da quello che il controller ha scritto**
cambiando solo il numero di voci: e' l'unico modo di non disfare cio' che lui
sa gia' del dispositivo.

## Cosa non fa ancora

- **gli hub dietro xHCI**: le richieste di classe ci sono gia' e su UHCI
  funzionano, ma qui un dispositivo dietro un hub vuole un alloggiamento con
  la «stringa di percorso». Il driver **lo dice** invece di rispondere
  «nessun HID» su un hub pieno di roba;
- **`Evaluate Context`**, per il `maxPacketSize` dell'endpoint 0 a full speed;
- **un dispositivo per volta**, com'e' gia' per UHCI: reggerne due insieme
  vuole uno stato per dispositivo invece che globale.

# xHCI: la meta' «controller» e' in piedi (13 agosto 2026)

    ex-os:/> /dev/xhci.drv
    xhci: controller in 00:04.0, registri a 0xfebf0000, IRQ 11
    xhci: versione 1.00, registri operativi a +0x40
    xhci: 8 porte, 64 alloggiamenti, contesti da 32 byte
    xhci: anelli di comando e di evento a posto
    xhci: porta 5  high  (480 Mbit)
    xhci: alloggiamento 1 per la porta 5
    xhci: dispositivo indirizzato dal controller
    xhci: dispositivo  USB 2.00  venditore 0627 prodotto 0001  maxp0 64

**Venditore e prodotto sono gli stessi che `uhci.drv` riporta** per il mouse di
QEMU. E' la controprova che conta: due controller diversi, due strade
completamente diverse, lo stesso dispositivo che dice di se' le stesse cose.

`drivers/xhci/xhci.c`, sul floppy accanto a `pci` e `uhci` — perche' su una
macchina senza legacy il controller e' questo, e la regola resta quella: cio'
che serve per avere una tastiera deve stare sul supporto di avvio.

## Cosa fa e cosa non fa ancora

| fa | non fa |
|---|---|
| trova il controller sul PCI (interfaccia 0x30), mappa i registri | il ciclo su tutte le porte |
| toglie il controller al BIOS, lo ferma, lo azzera | gli hub |
| anelli di comando, di evento, buffer di appoggio | Configure Endpoint |
| alloggiamento, indirizzamento, trasferimenti di controllo | l'anello di interruzione, cioe' il flusso dei rapporti HID |

! **NON REGISTRA NESSUN SERVIZIO, ed e' una scelta.** Un driver che
registrasse `mouse` senza saper leggere i rapporti sarebbe peggio di uno
assente: i client lo troverebbero e aspetterebbero per sempre.

## Le tre cose che questo codice deve fare bene

! **GLI SPIAZZAMENTI DEI TRE SPAZI DI REGISTRI SI LEGGONO, NON SI SANNO.**
CAPLENGTH vale 0x40 sul qemu-xhci, 0x20 su altri, 0x80 su certi ferri; RTSOFF
e DBOFF lo stesso. Un driver che li da' per noti funziona sulla macchina su
cui e' stato scritto — ed e' il genere di errore che indovina abbastanza a
lungo da sembrare corretto.

! **I PUNTATORI SONO A 64 BIT ANCHE SU UNA MACCHINA A 32, e la meta' alta va
AZZERATA.** E' proprio perche' siamo a 32 bit che nessuno la scriverebbe per
caso: se il BIOS o un reset ci ha lasciato dentro qualcosa, il controller
legge un indirizzo che non esiste e fa DMA nel vuoto. O peggio, non nel vuoto.

! **IL BIT 0 DEI PUNTATORI AGLI ANELLI E' IL CICLO, NON PARTE
DELL'INDIRIZZO.** Gli anelli sono allineati a 64 byte, quindi i sei bit bassi
sono liberi per i flag: scriverci l'indirizzo nudo lascia il ciclo a zero e il
controller aspetta dei comandi che, per lui, non sono ancora suoi. Vale per
CRCR e per il puntatore dentro il contesto dell'endpoint.

## ! IL «NO OP» NON E' UNA PROVA INUTILE: E' L'UNICA PULITA

Fa il giro completo — si scrive un TRB, si suona il campanello, il controller
lo legge, esegue e scrive un evento che si rilegge — **senza che ci sia di
mezzo un dispositivo che possa essere lui quello rotto**. Se passa, anello dei
comandi, campanello, anello degli eventi e bit di ciclo sono tutti a posto; se
non passa, non ha senso guardare piu' in la'.

E' il motivo per cui questo driver ha funzionato al primo colpo: quando e'
arrivato il momento di indirizzare un dispositivo, meta' delle cose che
potevano essere sbagliate erano gia' state escluse.

## ! L'INDIRIZZO NON LO ASSEGNA IL DRIVER, ed e' la differenza vera

Su UHCI si manda `SET_ADDRESS` e ci si tiene il conto. Qui si compila un
«contesto d'ingresso» — una scheda che descrive il dispositivo e il suo
endpoint 0 — e si chiede al controller di indirizzarlo lui. Da qui in poi il
dispositivo si nomina con il numero di **alloggiamento**, non con l'indirizzo.

Non e' una complicazione gratuita: e' cio' che permette al controller di
gestire da solo la larghezza di banda e i sessantaquattro dispositivi che
dichiara di reggere.

## ! LA PROVA CON LA TASTIERA NON SI PUO' ANCORA FARE, e non e' un difetto

`tools/qemu_drive.py` inietta i comandi con `sendkey` del monitor, cioe' **sui
tasti**. Con `-device usb-kbd` QEMU li consegna alla tastiera USB, che nessuno
ancora legge: il comando non viene mai battuto. E' il problema dell'uovo gia'
descritto per UHCI — dove infatti fallisce identico — e si chiude nello stesso
modo, caricando il driver come modulo da `kernel.cfg`. Ma perche' serva a
qualcosa il driver deve prima saper tradurre i rapporti HID in scancode, che
e' la meta' non ancora collegata.

## Il pezzo che manca, e dov'e' gia' scritto

Enumerazione, descrittori, configurazioni, classe HID e hub sono **gia'
scritti e provati** in `drivers/uhci/uhci.c`. Quello che manca fra qui e loro:

 1. estrarli da `uhci.c` in modo che stiano sopra tutt'e due i controller;
 2. `Configure Endpoint`, per dichiarare l'endpoint di interruzione;
 3. `Evaluate Context`, che serve a full speed quando il `maxPacketSize`
    dell'endpoint 0 non e' quello dichiarato a scatola chiusa. Oggi il driver
    **lo dice** invece di andare avanti come se niente fosse.

# Tre immagini che sanno dire quando sono scadute (13 agosto 2026)

    make -j2 all   a vuoto, prima:  rifaceva sempre il floppy
    make -j2 all   a vuoto, dopo:   0 ricette eseguite

    make iso       dopo aver cambiato i sorgenti FreeBASIC:
                   prima  «Nessuna operazione da eseguire»
                   dopo   ricostruisce

Il difetto di `uhci.drv` — l'ISO che non si rifaceva e tre corse buttate sullo
stesso binario — non era un caso isolato: era **un modo di sbagliare** presente
in tutt'e tre le immagini. Cercato apposta, si e' trovato altre due volte.

## Il floppy: dipendeva da nomi FINTI

`floppy:` era un bersaglio `.PHONY` che dipendeva da altri bersagli `.PHONY`
(`shell`, `ls`, `kbd_drv`). Nessuno di quelli ha una data, quindi make li
considera sempre piu' recenti dell'immagine e la ricetta girava **a ogni
invocazione**.

! **Non e' pericoloso come il caso dell'ISO, ed e' peggio in un altro modo.**
Un floppy rifatto senza motivo e' corretto, solo lento. Ma vuol dire che make
NON SA rispondere alla domanda «e' scaduta?»: la risposta era «sempre si'»
perche' non c'era modo di calcolarla, non perche' fosse vera. Uno strumento che
risponde sempre la stessa cosa non e' uno strumento.

Adesso il bersaglio e' **il file** `dist/floppy.img` e dipende dai file veri
(`PROGRAMMI_FLOPPY_OUT`); `floppy` resta come nome comodo da digitare, con i
`.PHONY` fra le sue prerequisite — perche' chi digita `make floppy` vuole che
cio' che manca si costruisca, mentre il file deve solo sapere quand'e' scaduto.
**Sono due domande diverse, e prima avevano una risposta sola.**

La guardia gemella e' `verifica-dipendenze-floppy`: guarda cosa `mkfloppy.sh`
COPIEREBBE — tutto cio' che trova in `build/bin`, `build/drivers/*.drv`,
`build/lib/*.so` — e si ferma se qualcosa non e' fra le dipendenze
dell'immagine. Provata mettendo un binario di troppo in `build/bin`: si ferma.

## Il CD strumenti: cinque sorgenti fuori, e l'albero FreeBASIC intero

L'elenco dei sorgenti di prova era battuto a mano e ne mancavano cinque —
`prova-fb.bas`, `prova-fb2.bas`, `prova-gcc.c`, `prova-ssl.c`,
`strumenti.txt` — piu' `KERNEL_CORE_NOTES.md` fra i documenti.

! **LA CURA NON E' AGGIUNGERE I CINQUE CHE MANCANO.** Quella lista si e'
staccata dalla realta' una volta e si stacchera' ancora: ogni prova nuova e'
un'occasione di dimenticarsene, e il sintomo non somiglia a un errore.
`tools/iso/` esiste SOLO per riempire quel CD, quindi la lista giusta e' **la
directory**. Una lista sola non puo' divergere da se stessa.

Stessa cura per i documenti: `ISO_DOC` sta sia fra le prerequisite sia nella
riga che li copia. Scritti due volte, i due elenchi divergono — ed e' cosi' che
`KERNEL_CORE_NOTES.md` era finito sul CD senza esserne una dipendenza.

## ! E IL TERZO L'HA TROVATO LA RIPULITURA, IN DIRETTA

Ripulito il porting EX-OS della runtime FreeBASIC — `src/rtlib/exos/`, che e'
roba nostra anche se vive in un albero di terzi — `make iso` ha risposto
**«nessuna operazione da eseguire»**. Il CD imbarca quei 1100 file con `tar` e
non ne dipendeva: avrebbe continuato a portare i sorgenti di prima, in
silenzio.

E' il difetto di `uhci.drv` in un posto dove nessuno lo cercava, trovato
perche' si stava facendo un'altra cosa. Adesso `FB_CD_FILE` li elenca con
`find` — **non a mano**, che sarebbe di nuovo la lista destinata a divergere —
filtrando `obj/` come gia' fa il `tar` della ricetta. Costa 40 ms a
invocazione di make.

## Le prove, e perche' sono prove

| | |
|---|---|
| il floppy si rifa' quando serve | toccato `bin/hello/hello.c`: ricompila e rifa' l'immagine |
| e NON si rifa' quando non serve | secondo `make -j2 all` a vuoto: **0 ricette** |
| la guardia vede un intruso | un binario in piu' in `build/bin`: si ferma, e dice dove rimediare |
| il CD strumenti si accorge | data dell'ISO falsificata in avanti, poi `touch prova-fb.bas`: da «aggiornato» a «da rifare» |

! **Quella del CD e' stata fatta falsificando la data dell'immagine, non
costruendola.** Rifare 171 MB per rispondere a una domanda su una data
avrebbe misurato la pazienza, non la dipendenza.

`dist/exos-tools.iso` e' stato comunque rifatto — 7 secondi — e adesso porta i
sorgenti ripuliti: il difetto dichiarato «il CD strumenti e' delle 11:53» e'
chiuso.

libctest 294/294, `polltest` e `shmtest` a posto.

# I driver di input sul floppy (13 agosto 2026)

    pci.drv       18844
    uhci.drv      21480
    mouseser.drv  16956
                            spazio libero: 160256 -> 102400 byte

    ! avviato da SOLO FLOPPY, con la SOLA tastiera USB, senza nessun CD:

    pci: servizio 'pci' attivo, 7 dispositivi
    uhci: tastiera USB -> scancode al servizio 'kbd' (PID 3)
    ex-os:/> hello
    Ciao da /bin/hello!

! **`uhci` senza `pci` non serve a niente**, quindi i tre viaggiano insieme:
il driver USB trova il proprio controller chiedendolo al servizio PCI, e senza
quello non parte.

E' il punto di tutta la giornata sull'input: una macchina senza 8042 deve
trovare **sul supporto di avvio** tutto cio' che le serve per avere una
tastiera. Dipendere dal CD vorrebbe dire che chi si avvia da floppy non puo'
nemmeno battere il comando per montarlo.

Lo spostamento e' bastato cambiare la directory di uscita: `tools/mkfloppy`
prende tutto quello che trova in `build/drivers/`.

# La dipendenza dell'ISO che non c'era (13 agosto 2026)

    build/drivers-cd/uhci.drv   13:12:25
    dist/exos.iso               12:53:59

Il codice degli hub USB era giusto al primo colpo e per **tre prove di fila**
ha detto «non e' un HID». Non era il codice: `DRIVER_SOLO_CD_OUT` — la lista
da cui dipende l'ISO — non elencava `uhci.drv`, quindi l'immagine non si
rifaceva e ogni prova girava sul driver di venti minuti prima.

! **Sono due elenchi della stessa cosa.** `DRIVER_CD` dice COSA COSTRUIRE
(bersagli .PHONY), `DRIVER_SOLO_CD_OUT` dice DA COSA DIPENDE L'IMMAGINE (nomi
di file). Un driver aggiunto solo al primo si costruisce e non entra mai in
un'ISO nuova.

! **E `verifica-programmi` non lo prendeva**: quella controlla che ogni
sorgente finisca su un'immagine, cioe' la PRESENZA. Qui mancava la DIPENDENZA,
e le due cose si somigliano abbastanza da far credere che una copra l'altra.

La guardia nuova, `verifica-dipendenze-cd`, confronta i due elenchi e ferma la
costruzione se divergono. Provata togliendo `$(UHCI_OUT)` dalla lista: si
ferma con un errore che dice anche dove rimediare.

! E' la quinta volta su questo progetto che uno strumento di misura sbagliato
costa piu' del lavoro. La regola resta quella scritta a proposito del DMA: due
misure che non combaciano con l'attesa vanno guardate PRIMA di cambiare il
codice.

# Via i caratteri Unicode dai sorgenti (13 agosto 2026)

    prima passata:  155 file (.c, .h, .md)
    seconda:         16 file (.asm, .bas, .cpp, .s, .conf, makefile, .gitignore)
                                                    residuo fuori da build/: 0

Il triangolo di avvertimento e le spunte verdi sono spariti da tutto l'albero:
al loro posto un `!`. Restano le lettere accentate e le virgolette basse, che
sono lo stile del progetto, e restano le frecce e i caratteri di riquadro, che
disegnano gli schemi.

! **LA PRIMA PASSATA DICHIARAVA «ZERO RIMASTI» E NON ERA VERO.** Guardava solo
le estensioni piu' ovvie, e fuori restavano i quattro `.asm` dell'avvio e del
kernel, i sorgenti di prova del CD strumenti, la configurazione di OpenSSL e
il `.gitignore` — 153 simboli in 16 file. Una ripulitura che si misura sulle
estensioni che si e' pensato di guardare misura se stessa.

! **I SIMBOLI CHE PORTANO INFORMAZIONE NON POSSONO DIVENTARE `!`.** Una
crocetta in una tabella vuol dire «no», e sostituirla con un punto esclamativo
direbbe il CONTRARIO. Quelli sono stati riscritti a mano — «no», «non fatto»,
«MAI raggiunto» — mentre solo i decorativi sono diventati `!`.

! **E UNO ERA DENTRO UNA STRINGA STAMPATA**, non in un commento: in
`tools/iso/prova-cpp.cpp` finiva sulla console di EX-OS, che quei caratteri
non sa disegnarli. In un `.asm` la differenza fra un commento e un byte
assemblato non e' un dettaglio: verificato file per file prima di sostituire.

! **Non e' estetica, e' costo.** Toglierli a mano da un progetto intero costa
tempo a chi lo mantiene, e un `!` avrebbe evitato il problema fin dall'inizio.
Da qui in avanti: solo ASCII nei commenti, nei .md e nei messaggi di commit.

La sostituzione ha toccato anche i `.c` e gli `.h`, quindi e' stata seguita da
ricostruzione completa: libctest 294/294, polltest e shmtest a posto.

> ! **PRIMA DI SCRIVERE CODICE NUOVO, LEGGI [`DIREZIONE.md`](DIREZIONE.md).**
> Dal 12 agosto 2026 ci sono quattro vincoli che valgono per ogni servizio,
> driver, server e applicazione — anche quelli che con la grafica non c'entrano:
> tutto il nuovo nasce in ring 3, la grafica va in spazio utente, il pagefile
> e' un vincolo da subito anche se arriva dopo, e ci sono tre primitive
> mancanti senza le quali il lavoro grafico poggia sul vuoto.
>
> Questo file resta il diario: si legge dall'alto, e le voci vecchie scorrono.
> Quello e' la direzione, e non scorre.

# Gli hub USB, e uno strumento che mentiva (13 agosto 2026)

    uhci: porta 0  USB 1.10  venditore 0409 prodotto 55aa  maxp 8
    uhci: hub con 8 porte
    uhci: porta 102  USB 2.0  venditore 0627 prodotto 0001  maxp 8
    uhci: porta 0: mouse USB «boot», endpoint 1

    mouse -n 3
      dx   +12  dy    +4   totale +12,+4
      dx   +12  dy    +4   totale +24,+8
      dx   +12  dy    +4   totale +36,+12

Un mouse attaccato **dietro un hub** guida il puntatore.

## ! SENZA HUB SI VEDE MENO DI META' DEL BUS

Un UHCI ha due porte, e su una macchina vera almeno una finisce quasi sempre
in un hub — quello del pannello frontale, quello dentro un monitor, quello di
una tastiera con le prese. Fermarsi alle porte del controller vuol dire dire
«non c'e' nessun mouse» a chi ce l'ha attaccato.

Un hub e' un dispositivo USB come gli altri: si enumera con le stesse
richieste, e SOLO DOPO espone porte proprie — che non hanno registri, si
interrogano con richieste di classe.

! **Un hub si riconosce dalla CLASSE DEL DISPOSITIVO, non dall'interfaccia.**
E' l'unico caso in cui quel byte dice qualcosa invece di essere zero, e va
guardato PRIMA di cercare un'interfaccia HID — altrimenti si conclude «non e'
un HID» su un hub che ha un mouse attaccato dietro. E' proprio il messaggio
che si vedeva.

! **Le porte vanno alimentate, e poi bisogna ASPETTARE.** Quanto lo dice il
descrittore dell'hub, in unita' da 2 ms: chiedere lo stato prima risponde
«niente attaccato» anche quando c'e'.

! **Un solo livello.** Gli hub si incatenano fino a cinque, ma ogni livello in
piu' e' ricorsione con lo stato del dispositivo in variabili globali. Quando
servira' si ripartira' da uno stato per dispositivo.

## ! LO STRUMENTO MENTIVA: l'ISO non si ricostruiva

Il codice degli hub era giusto al primo colpo e per tre prove di fila ha
detto «non e' un HID». Non era il codice:

    build/drivers-cd/uhci.drv   13:12:25
    dist/exos.iso               12:53:59

`make iso-exos` non ha fra le proprie dipendenze i driver del CD, quindi
lasciava l'immagine vecchia — e ogni prova girava sul driver di venti minuti
prima. Le tre corse «fallite» erano tre corse dello stesso binario superato.

**Finche' non si sistema quella regola, dopo aver toccato un driver del CD va
fatto `rm -f dist/exos.iso && make iso-exos`.** E' la quinta volta che su
questo progetto uno strumento di misura sbagliato costa piu' del lavoro: la
regola resta quella scritta a proposito del DMA — due misure che non
combaciano con l'attesa vanno guardate PRIMA di cambiare il codice.

# La tastiera USB guida la shell (13 agosto 2026)

    ! avviato con la SOLA tastiera USB: -device usb-kbd

    [PASSO 14b] moduli: kbd, pci, uhci
    pci: servizio 'pci' attivo, 7 dispositivi
    uhci: controller in 00:01.2, porte 0xc040, IRQ 11
    uhci: porta 0: tastiera USB «boot», endpoint 1
    uhci: tastiera USB -> scancode al servizio 'kbd' (PID 3)

    ex-os:/> hello
    Ciao da /bin/hello!
    ex-os:/> uname
    EX-OS

Quei due comandi sono stati BATTUTI su una tastiera USB, su una macchina dove
la tastiera legacy non consegna niente.

## ! NON SI REGISTRA IL SERVIZIO "kbd": SI MANDANO SCANCODE A CHI LO SERVE

E' la decisione che vale tutto il lavoro. Un driver USB che registrasse "kbd"
per conto proprio dovrebbe rifare mappe di tastiera, editing di riga, modo
raw, eco e commutazione con Alt+Fn: meta' di `kbd.c`, e la meta' sottile.
Traducendo gli usi HID in **scancode set 1** — prefisso `0xE0` compreso —
tutto quello continua a funzionare senza sapere che l'USB esista.

E' anche cio' che fa il firmware di una scheda madre quando emula il legacy,
solo che qui si vede invece di succedere di nascosto in SMM.

! **Chiunque puo' iniettare tasti** con `KBD_MSG_SCANCODE`, e va detto: un
servizio ring 3 non ha modo di sapere se chi gli scrive e' un driver — il
varco `*.drv` lo conosce il kernel, non l'IPC.

## ! IL RAPPORTO «BOOT» DICE CHI E' PREMUTO ADESSO, NON COSA E' CAMBIATO

Otto byte: modificatori, un byte da ignorare, e fino a sei tasti tenuti giu'.
Pressioni e rilasci si ricavano confrontando con il rapporto PRECEDENTE — non
c'e' altro modo, e chi se lo dimentica ottiene una tastiera che ripete
all'infinito l'ultimo tasto.

## ! IL PROBLEMA DELL'UOVO, e come si chiude

La prima prova non e' fallita: **non e' partita**. Con `-device usb-kbd` non si
riesce a battere nemmeno il comando che avvia il driver — e' la tastiera che
quel driver deve far funzionare. E' esattamente la macchina senza legacy di cui
si parlava, riprodotta per sbaglio.

La chiusura e' caricarlo come MODULO all'avvio, insieme al PCI da cui dipende.
La ricetta sta adesso dentro `boot/kernel.cfg`, dove la si cerca:

    modules     = kbd, pci, uhci
    pci         = /cdrom/dev/pci.drv
    uhci        = /cdrom/dev/uhci.drv

! E i moduli partono **tutti insieme**: `uhci` puo' cercare il servizio `pci`
prima che questo abbia registrato il proprio nome. Adesso lo aspetta fino a
cinque secondi — fallire li' vorrebbe dire una macchina senza input per una
corsa persa di qualche millisecondo.

# USB: controller UHCI, enumerazione e HID «boot» (13 agosto 2026)

    ex-os:/> /cdrom/dev/uhci.drv -v &
    uhci: controller in 00:01.2, porte 0xc040, IRQ 11
    uhci: porta 0  USB 2.0  venditore 0627 prodotto 0001  maxp 8
    uhci: porta 0: mouse USB «boot», endpoint 1
    uhci: servizio 'mouse' attivo (mouse USB)

    ex-os:/> mouse -n 3
    sorgente: servizio 'mouse' (driver dedicato)
      dx   +15  dy    +6   bottoni ---   totale +15,+6
      dx   +15  dy    +6   bottoni ---   totale +30,+12

**Un mouse USB guida il puntatore di EX-OS.** Controller trovato sul PCI,
porta resettata, dispositivo indirizzato, descrittori letti, interfaccia HID
«boot» riconosciuta, endpoint di interruzione interrogato. Tutto in ring 3.

## ! PERCHE' UHCI PER PRIMO, VISTO CHE LE SCHEDE MODERNE HANNO xHCI

Lo stack USB si divide in due meta' di dimensione molto diversa:

| | |
|---|---|
| il controller | UHCI: porte I/O, una lista di frame, descrittori da 32 byte. xHCI: MMIO, anelli di comando e di evento, contesti. Mondi diversi |
| **tutto il resto** | enumerazione, indirizzi, descrittori, configurazioni, classe HID. **Identico su qualunque controller** |

«Tutto il resto» e' il grosso, ed e' la parte piu' facile da sbagliare in modi
silenziosi. Farlo nascere contro il controller PIU' SEMPLICE e' il modo di
avere **una sola incognita per volta**. Quando arrivera' xHCI cambiera' solo
la meta' piccola.

## Le tre cose che questo codice deve fare bene

! **La QH va in tutti i 1024 frame, non in quello «corrente».** Il numero di
frame avanza da solo mille volte al secondo: metterla in uno solo vorrebbe
dire sperare di indovinare quale sara' fra un istante.

! **La lunghezza nel token e' «uno in meno», e zero byte si scrive 0x7FF.** E'
la codifica dell'hardware: scriverci la lunghezza vera darebbe un byte in piu'
a ogni trasferimento, e per lo stato di un controllo — lungo zero — darebbe un
byte invece di niente.

! **Si scorre la catena dei descrittori con la lunghezza di ogni pezzo, non a
passi fissi.** Una configurazione e' una fila di blocchi di misura diversa;
saltare di misura fissa vuol dire leggere campi a caso appena un dispositivo
ne infila uno in mezzo.

E il protocollo «boot» esiste apposta per non dover interpretare il
descrittore di rapporto — che e' un piccolo linguaggio, ed e' il pezzo di USB
che costa piu' di tutti.

## IL DIFETTO: UN NAK NON E' UN ERRORE

Alla prima prova si perdeva **un movimento su due**. La causa e' nella
specifica, non nel codice che sembrava sbagliato: **un NAK non consuma il
contatore dei tentativi**. Il TD di una lettura a cui il mouse non ha niente
da rispondere resta ATTIVO e viene ritentato ogni frame — cioe' si comporta
esattamente come deve.

Io lo trattavo come un guasto, e per giunta dopo un'attesa di due secondi: il
ciclo restava fermo dentro l'attesa mentre il mouse si muoveva. Adesso
`esegui()` rende un codice a parte per «ancora attivo», l'attesa e' di 15 ms,
e chi legge un endpoint di interruzione lo interpreta come «non ha niente da
dire».

## L'ENDPOINT DI INTERRUZIONE, e il difetto chiuso subito dopo

La prima versione interrogava a **colpo singolo**: ogni lettura costruiva un
TD, lo metteva in lista, aspettava qualche millisecondo e lo toglieva. Fra un
tentativo e l'altro la coda era **vuota**, e un rapporto che arrivava li'
dentro non aveva dove andare.

    cinque movimenti a raffica, prima:   totale +30,+12   (due persi su cinque)
    cinque movimenti a raffica, dopo:    totale +75,+30   (5 x 15, 5 x 6)

! **E la cosa peggiore era che con movimenti lenti andava benissimo**, cioe'
la prova facile passava. E' la differenza fra «funziona» e «funziona sempre»,
e le due si somigliano troppo perche' una prova comoda le distingua.

Adesso il TD sta nella lista dei frame e **non se ne va mai**: il controller
lo ripercorre a ogni frame e ritenta da solo finche' il dispositivo non
risponde — che e' esattamente cio' che un endpoint di interruzione deve
essere. Il driver non manda piu' niente sul bus: guarda un bit in memoria.

! **L'elemento della QH va rimesso a ogni giro.** Quando il TD finisce, il
controller fa avanzare quel puntatore al TD successivo — che non c'e' — e la
coda resta vuota per sempre. Non rimetterlo dava un mouse che funzionava per
UN rapporto e poi taceva.

! **Il contatore dei tentativi e' zero, cioe' infinito**, ed e' voluto: un NAK
non lo consuma comunque, ma un errore di trasmissione si'. Con un tetto basso
un solo disturbo sul cavo ritirerebbe il TD, e il dispositivo smetterebbe di
essere ascoltato senza che nessuno lo dica.

## Cosa manca

- **xHCI**, che e' quello che ha davvero una scheda madre senza legacy.

# Il mouse seriale (13 agosto 2026)

    ex-os:/> /cdrom/dev/mouseser.drv &
    mouseser: servizio 'mouse' attivo su 0x2f8 IRQ3 (il mouse si e' presentato)

    ex-os:/> mouse -n 3
    sorgente: servizio 'mouse' (driver dedicato)
      dx   +20  dy    +7   bottoni ---   totale +20,+7
      dx   +20  dy    +7   bottoni ---   totale +40,+14
      dx    +0  dy    +0   bottoni S--   totale +40,+14

Iniettati con `mouse_move 20 7` due volte: numeri decisi prima.

Protocollo Microsoft, 1200 baud 7N1. **Hardware separato dalla tastiera,
quindi driver separato** — al contrario del PS/2, che condivide l'8042 con lei
e sta dentro `kbd.drv`.

## ! IL PREDEFINITO E' COM2, E NON E' UNA PREFERENZA

COM1 e' la console di log del kernel: `serial_putchar()` ci scrive il log di
avvio, e **tutte le prove automatiche di questo progetto lo leggono da li'**.
Un mouse su COM1 spegnerebbe lo strumento con cui si verifica che il mouse
funziona. Il driver accetta `com1` lo stesso, e in quel caso lo dice.

## ! LA Y NON SI GIRA, e qui sta la trappola

Il PS/2 manda la Y positiva verso l'ALTO e il driver la gira. Il protocollo
Microsoft la manda gia' positiva verso il BASSO. Girarla «per simmetria» con
l'altro driver darebbe un puntatore che va dalla parte sbagliata **solo con il
mouse seriale** — cioe' un difetto che si vede su una macchina su tre.

## ! UN MOUSE SERIALE NON SI PUO' SONDARE

Non risponde a nessuna domanda: parla solo quando lo si muove. L'unica cosa
rilevabile e' la UART, con la prova del registro di appunti — che dice se il
chip c'e', **non** se ci sia attaccato un mouse. Alla riaccensione di RTS un
mouse Microsoft manda una `M` per presentarsi, e se arriva la si riferisce; ma
NON e' un requisito, perche' pretenderla vorrebbe dire rifiutare mouse veri che
non la mandano. (Quello di QEMU la manda.)

RTS spento e poi acceso **e'** il reset: il mouse seriale si alimenta dalle
linee di controllo.

## Due sorgenti, una regola sola

Adesso il mouse puo' arrivare da due posti, e la regola di scelta sta in
`kbd_proto.h` — non nei client, che saranno piu' di uno:

| servizio | chi lo serve |
|---|---|
| `"mouse"` | un driver dedicato: seriale oggi, USB domani |
| `"kbd"` | il PS/2, dentro il driver della tastiera |

! **Si cerca prima quello dedicato.** Se c'e' un driver che fa SOLO il mouse
vuol dire che qualcuno l'ha avviato apposta. Il PS/2 c'e' sempre, quindi
vincerebbe sempre se lo si guardasse per primo.

## Come si prova

    EXOS_QEMU_EXTRA="-serial msmouse -drive file=dist/exos.iso,media=cdrom,if=ide,index=2" \
    python3 tools/qemu_drive.py "/cdrom/dev/mouseser.drv &@6" "mouse -n 3@2" \
        "mon:mouse_move 20 7@1" "mon:mouse_move 20 7@1"

`-serial msmouse` e' il secondo `-serial`, quindi diventa COM2: il primo resta
il file di log.

# Il mouse PS/2 (13 agosto 2026) — primo passo dell'input

    ex-os:/> mouse -n 4
    mouse PS/2 presente. dy positivo = verso il BASSO.
      dx   +10  dy    +5   bottoni ---   totale +10,+5
      dx   +10  dy    +5   bottoni ---   totale +20,+10
      dx    +0  dy    +0   bottoni S--   totale +20,+10
      dx    +0  dy    +0   bottoni ---   totale +20,+10

    totale letto: dx +20  dy +10

Iniettati dal monitor di QEMU con `mouse_move 10 5` due volte e
`mouse_button 1`: i numeri erano decisi PRIMA, e sono quelli letti dentro.

## ! NON E' UN DRIVER A PARTE, E NON PUO' ESSERLO

Il mouse PS/2 non ha porte sue: e' la SECONDA PORTA dello stesso 8042 della
tastiera, e i suoi byte escono dallo stesso registro `0x60`. Chi legge quella
porta consuma il byte **per tutti** — due processi che la leggono si rubano i
byte a vicenda, e il guasto sarebbe una tastiera che perde tasti mentre si
muove il mouse. Un controller, un driver: sta dentro `/dev/kbd.drv`.

`kbd.c` i byte del mouse li leggeva **gia'**, e li buttava: andavano comunque
letti, o avrebbero tenuto alto OBF bloccando la tastiera. Adesso si montano in
pacchetti.

(Per un mouse seriale o USB il ragionamento non vale: quelli sono hardware
separato e avranno driver separati. E' allora che servira' un servizio che
unifichi le sorgenti — non adesso.)

## Le tre decisioni

! **Lo spostamento si accumula e si azzera leggendo.** La mailbox IPC e'
profonda QUATTRO messaggi: un mouse che manda un evento per movimento la
riempirebbe in un decimo di secondo, e da li' in poi il puntatore si
fermerebbe mentre il mouse si muove. Sommando, un client lento riceve MENO
messaggi ma lo spostamento GIUSTO — che per un puntatore e' cio' che conta.

! **La Y si gira nel driver, una volta sola.** Il PS/2 la manda positiva verso
l'alto; lo schermo la vuole positiva verso il basso. Un client che se lo
dimenticasse avrebbe un puntatore che va su quando il mouse va giu', e non lo
scoprirebbe leggendo il proprio codice.

! **Si azzera solo se la consegna e' riuscita.** Azzerare prima vorrebbe dire
buttare uno spostamento che il client non ha mai ricevuto.

## Due difetti trovati per strada

**1. Il primo byte si riconosce dal bit 3, che vale sempre 1.** Senza quel
controllo un solo byte perso sfaserebbe TUTTI i pacchetti successivi, e il
puntatore andrebbe a caso per sempre invece che per un istante.

**2. ! `kbd_set_leds()` NON legge i due ACK che la tastiera risponde**, e
restano nel buffer. Il primo comando al mouse li leggeva al posto delle
proprie risposte: siccome un ACK vale `0xFA` come quello del mouse, il reset
«riusciva» e poi il controllo del self-test trovava il secondo `0xFA` invece
di `0xAA`. Sintomo: **«nessun mouse ha risposto» su una macchina che il mouse
ce l'ha.** Adesso si svuota il buffer prima di parlare al mouse.

**3. Si riapre l'IRQ che e' arrivato, non sempre l'1.** Da quando ce ne sono
due, riaprire sempre il primo lascerebbe l'IRQ12 mascherato per sempre dopo il
primo movimento: il mouse funzionerebbe per un pacchetto e poi basta. Il
numero sta nel payload della notifica.

## Lo strumento

`tools/qemu_drive.py` accetta adesso `mon:<comando>`, che manda un comando al
monitor di QEMU invece che alla shell. E' cosi' che si inietta input
dall'esterno — `mouse_move`, `mouse_button` — cioe' come si prova un driver di
input con numeri decisi qui e verificabili la' dentro.

## Cosa manca, e in che ordine

| | |
|---|---|
| mouse seriale | protocollo Microsoft su COM. QEMU lo emula (`-chardev msmouse`), quindi e' provabile |
| USB | serve uno **stack**: controller host, enumerazione, descrittori, classe HID «boot». Il grosso — enumerazione e HID — e' COMUNE a tutti i controller |
| UHCI | il controller piu' semplice (porte I/O). QEMU: `-usb` da' un piix3-usb-uhci. E' il posto giusto dove far nascere lo stack |
| xHCI | quello che ha una scheda madre **senza legacy** davvero. Molto piu' grosso: MMIO, anelli di comando/evento, contesti. `SYS_MMIO_MAP` — fatta oggi — e' il suo prerequisito |

! **UHCI prima non e' lavoro buttato verso xHCI**: cambia solo il driver del
controller, mentre enumerazione, descrittori e HID restano gli stessi. E' il
modo di far nascere lo stack contro l'hardware piu' semplice invece che contro
il piu' difficile.

libctest 294/294, `polltest` e la tastiera: tutto a posto.

# Rimettere il modo testo senza BIOS — IL GRADINO 0 E' CHIUSO (13 agosto 2026)

    schermo di testo buono                 720x400   629 pixel accesi,  4 colori
    dopo /dev/vgaprova.drv                 640x400   132512 accesi,    47 colori
    dopo  testo                            720x400   611 pixel accesi,  4 colori

    pixel diversi fra il PRIMA e il DOPO:  18 su 288000  (0,0063%)

I 18 sono il cursore. Lo schermo dopo il ripristino non «somiglia» a uno
schermo di testo: **e' lo stesso schermo, pixel per pixel.**

`SYS_MODO_TESTO` (245) e `/bin/testo`. Con questa il gradino 0 di
`DIREZIONE.md` e' finito: mappatura del framebuffer, memoria condivisa,
attesa su piu' sorgenti, ritorno al testo.

## Il problema, e perche' non basta «riavviare il server»

Una modalita' video si imposta con `INT 10h`, cioe' con il BIOS, cioe' in modo
REALE: l'unico che puo' farlo e' Stage 2, prima del passaggio a modo protetto.
Dopo, quella porta e' chiusa.

Se il server grafico muore con la scheda in modalita' grafica il sistema e'
vivo — kernel, scheduler, seriale e tastiera funzionano — ma lo schermo resta
congelato su cio' che c'era. Il comando digitato alla cieca viene eseguito e
non se ne vede l'effetto. Senza questo, l'unica uscita e' il riavvio.

! **Sta nel kernel, ed e' una delle pochissime eccezioni alla direttiva 1.**
Deve funzionare QUANDO IL SERVER E' MORTO, e un processo che risponde solo
finche' un altro processo e' vivo non e' una rete di sicurezza. Stessa ragione
per cui c'e' l'ATA.

! **E non e' riservata ai driver**, contro l'istinto: rimettere il video tocca
tutto lo schermo, quindi somiglia a un'operazione da privilegiati. Ma serve
proprio quando il processo privilegiato e' morto, e una rete che si possa
tirare solo dai driver e' una rete che non c'e' nel momento in cui serve.

## Il pezzo che si dimentica: il carattere

In modo testo i disegni dei caratteri NON stanno nella ROM. Stanno nel **piano
2** della memoria video, dove il BIOS li copia all'avvio, e una modalita'
grafica ci scrive sopra i propri pixel. Rimettere i registri del modo 3 senza
ricaricarli da' 80x25 di simboli casuali: uno schermo che risponde e non si
legge, che e' quasi peggio di uno spento.

Il carattere c'era gia': `font8x16`, lo stesso che la console disegna nel
framebuffer quando la grafica c'e'. Un carattere solo per le due strade,
quindi lo schermo si legge uguale prima e dopo — ed e' anche il motivo per cui
il confronto pixel per pixel puo' esistere.

! **Il passo e' 32 byte, non 16.** Ogni disegno occupa un'area fissa da 32
byte qualunque sia l'altezza del carattere: scriverli di seguito darebbe i
caratteri sfalsati a partire dal secondo.

! **E va rimessa la tavolozza.** I 16 colori del testo non sono valori RGB:
sono INDICI che il controllore degli attributi manda al DAC, e vanno agli
indici 0..7 e 0x38..0x3F — non 0..15. Scriverli in 0..15 darebbe una
tavolozza giusta a cui nessuno guarda.

## Le due trappole dei registri

! **I primi otto registri del CRTC sono protetti da un lucchetto, e il
lucchetto sta nel registro 0x11** — cioe' dentro l'insieme che si sta per
scrivere. Senza toglierlo prima, le scritture a 0..7 non hanno effetto e non
danno errore: si ottiene una temporizzazione mista fra il modo vecchio e
quello nuovo, che e' il modo piu' facile di avere uno schermo nero credendo di
aver fatto tutto.

! **Il controllore degli attributi ha una porta sola per indice e dato**, con
un flip-flop interno che decide quale dei due sta ricevendo. Si azzera
LEGGENDO 0x3DA. Se qualcuno l'ha lasciato a meta', il primo valore finisce nel
registro sbagliato e tutti gli altri slittano.

## IL DIFETTO CHE L'HA PRESO LO SCREENDUMP, NON IL CODICE

Da un modo grafico VGA il ripristino funzionava. Da una **VESA 800x600** no:
la risoluzione restava 800x600 e non c'era **nessun errore da nessuna parte**.

La finestra a porte delle schede Bochs/QEMU/VirtualBox e' a SEDICI bit —
indice a 0x1CE, dato a 0x1CF — e la stavo leggendo a byte, credendo che 0x1D0
ne fosse la meta' alta. Non lo e': e' un'altra porta. L'identificativo non
combaciava, la funzione usciva senza toccare niente, e cambiare i registri VGA
mentre la VBE e' accesa non porta via da nessuna parte.

! **Sarebbe passata inosservata con qualunque prova basata sulla seriale.** Il
programma stampa «Modo testo 80x25 ripristinato» e il comando dopo funziona:
tutto sembra a posto. L'unica cosa che ha detto la verita' e' il numero
nell'intestazione del PPM.

## Come si prova, e perche' cosi'

`tools/qemu_drive.py` accetta adesso `foto:/percorso.ppm`, che chiede a QEMU
un'istantanea dello schermo. Cio' che conta non e' il testo sulla seriale ma
la RISOLUZIONE, ed e' scritta nell'intestazione del file: un numero, non
un'impressione.

`/dev/vgaprova.drv` mette la scheda in un modo grafico a 256 colori e **esce
subito** — il processo che ha rotto lo schermo non c'e' piu', che e' il punto.
Non e' un driver: si chiama `.drv` perche' dal 13 agosto e' l'unico modo di
ottenere le porte I/O.

! **Un codice di emergenza mai eseguito e' un codice che non si sa se
funziona**, cioe' la stessa cosa di non averlo. Per questo il guasto e'
riproducibile a comando.

Le tre misure che chiudono il discorso:

| | |
|---|---|
| lo schermo si rompe davvero | 51,76% di pixel accesi e **47 colori distinti** — piu' di sedici, quindi e' un modo a 256 colori e non un 640x400 con il testo ancora leggibile |
| torna identico | 18 pixel diversi su 288000 rispetto a uno schermo di testo buono |
| **le due strade coincidono** | partendo da un modo grafico VGA e partendo da una VESA 800x600 si arriva a schermi identici fra loro (18 pixel, sempre il cursore) |

libctest 294/294, `shmtest` e `polltest`: tutto a posto.

## Il limite, dichiarato

Su schede Bochs/QEMU/VirtualBox la VESA si spegne dalla finestra 0x1CE/0x1CF,
e questo codice lo fa riconoscendola dal proprio identificativo. **Su ferro
vero l'equivalente e' l'interfaccia in modo protetto di VBE 2.0, che EX-OS non
ha**: li' i registri VGA da soli possono non bastare. E' scritto in testa a
`vga_modo3.c` invece che scoperto dopo.

L'impronta ABI non cambia: `modo_testo()` e' una funzione in piu', non un tipo
nuovo — ed e' esattamente il caso che `tools/abi-bersaglio.c` dice di non
dover far gridare.

# Aspettare piu' sorgenti insieme: poll() e select() (13 agosto 2026)

    ex-os:/> /bin/polltest
    Attesa su piu' sorgenti: poll() e select()

      stdout e' sempre scrivibile, con scadenza 0    ok
      pipe vuota: la scadenza rende 0                ok
      e ci ha messo almeno la scadenza               ok
      pipe con dati: POLLIN subito                   ok
      un fd negativo si salta in silenzio            ok
      un fd mai aperto da' POLLNVAL                  ok
      pipe senza piu' scrittori: POLLHUP             ok
      svegliato da un ALTRO processo, su 2 sorgenti  ok
      e mentre aspettavo ero BLOCKED, non in attesa attiva ok
      svegliato da una PIPE scritta da un altro processo ok
      stdin senza nessuno che digita: scade e basta  ok
      select() vede la pipe pronta                   ok
      select() su pipe vuota rende 0                 ok

    esito   tutto a posto

Gradino 0 punto 3 di `DIREZIONE.md`, e con questo **del gradino 0 resta solo
il ritorno al modo testo senza BIOS**. `SYS_POLL` (244), piu' `select()` nella
libc.

    struct pollfd v[3];
    v[0].fd = FD_IPC;    v[0].events = POLLIN;   /* la mailbox */
    v[1].fd = fd_client; v[1].events = POLLIN;   /* una pipe   */
    v[2].fd = 0;         v[2].events = POLLIN;   /* la tastiera */
    poll(v, 3, -1);

## La prova che conta non e' che poll() torni

! **Un'attesa attiva darebbe gli stessi identici risultati su ogni altra riga
di quell'elenco** — stessi `revents`, stessi valori di ritorno — solo con la
CPU bruciata. Le prove che si scrivono d'istinto qui non provano niente.

Percio' `polltest` fa una cosa diversa: il **figlio**, prima di svegliare il
padre, legge lo stato del padre con `procinfo()` e glielo manda **dentro il
messaggio che lo sveglia**. Il padre verifica di essere stato `BLOCKED` mentre
credeva di aspettare. Il messaggio e' insieme la sveglia e la prova.

## La mailbox IPC si nomina con FD_IPC, e non diventa un descrittore

Su questo sistema meta' degli eventi non passa dai file: driver, servizi e
notifiche di IRQ arrivano tutti nella mailbox. Un poll che sapesse guardare
solo i descrittori lascerebbe fuori **proprio le sorgenti per cui e' stato
chiesto**.

`FD_IPC` vale 32 — il primo numero che un descrittore vero non puo' avere —
cosi' i valori NEGATIVI conservano il significato POSIX di «salta questa
voce», che e' come un server smette di badare a un client senza rifare
l'elenco a ogni giro.

! Farne un descrittore vero sarebbe stato piu' elegante da fuori e falso da
dentro: la mailbox non si apre, non si chiude, non si eredita e non si
duplica. Un fd che non fa niente di cio' che fanno gli fd e' una bugia comoda
per una riga sola di codice.

## Le tre cose che rendono corretto il blocco

**1. Guardare e bloccarsi devono essere indivisibili, su TUTTE le sorgenti.**
E' la race del risveglio perduto — quella che nel luglio 2026 teneva la shell
ferma al prompt — moltiplicata per N. Se fra il «non e' pronto niente» e il
`sched_block()` gli interrupt fossero aperti, basterebbe che UNA qualunque
delle sorgenti diventasse pronta in quella finestra. Con una sorgente sola e'
improbabile; con N e' N volte piu' probabile.

**2. La memoria dell'utente si tocca solo fuori dalla sezione critica.** Le
pagine di un processo possono non essere presenti — EX-OS carica su richiesta
— e leggerle con il `cli` in mano vorrebbe dire un page fault a interrupt
disabilitati. Da qui la copia dell'elenco dentro il kernel, che e' lo stesso
rimbalzo gia' documentato in `pipe.c`.

**3. Chi non trova posto in attesa si ricontrolla al tick dopo, invece di
rubarlo.** Il posto e' uno solo per pipe e uno solo per il tty: e' cosi' da
sempre, perche' finora ad aspettare era uno solo. Sovrascriverlo farebbe
dormire per sempre chi c'era prima — molto peggio di un risveglio ogni 10 ms.
`pipe_attesa_registra_locked` e `tty_attesa_registra_locked` rendono 0 invece
di prendersi il posto, e poll ripiega su una scadenza breve.

! **La mailbox non si registra da nessuna parte, e non e' una dimenticanza:**
`ipc_send()` sveglia il destinatario cercandolo per PID, qualunque cosa stia
aspettando. La sorgente piu' importante di questo sistema e' quella che
funziona gratis.

## select() sta nella libc, sopra poll()

! Non e' una seconda implementazione, ed e' il punto: un'attesa su piu'
sorgenti scritta due volte vuol dire sbagliare due volte la race del risveglio
perduto, e la seconda si scopre mesi dopo. Nel kernel c'e' solo `poll()`.

! `select()` non puo' nominare `FD_IPC`: un `fd_set` e' una maschera a 32 bit
e FD_IPC e' il 32esimo. Chi deve aspettare anche i messaggi usa `poll()` — che
e' anche il motivo per cui `select()` e' la forma vecchia.

## Cosa risponde «pronto» e perche'

| sorgente | pronto quando |
|---|---|
| `FD_IPC` | c'e' un messaggio in mailbox |
| pipe in lettura | ci sono byte, **oppure** non ci sono piu' scrittori (POLLHUP) |
| pipe in scrittura | c'e' spazio; POLLERR se non ci sono piu' lettori |
| `stdin` | il ring buffer del tty ha una riga pronta |
| file, stdout, stderr, driver | **sempre** |

! **I file normali sono sempre pronti, e non e' una scorciatoia: e' POSIX.**
Una read su un file non blocca mai, al piu' arriva alla fine. Un poll che
aspettasse su un file aspetterebbe per sempre qualcosa che e' gia' successo.

! **Senza piu' scrittori la pipe e' PRONTA, non morta.** Una read li' sopra
rende 0 — la fine dei dati — e ci arriva subito. Dire «non e' pronta»
vorrebbe dire far aspettare per sempre qualcosa che non puo' piu' arrivare.

! **POLLERR, POLLHUP e POLLNVAL arrivano anche se non si chiedono.** Sono
condizioni, non desideri: chi aspetta solo POLLOUT su una pipe i cui lettori
sono spariti deve saperlo, o scriverebbe per sempre nel vuoto.

## La sorgente piu' delicata e' stdin, e la prova vera non e' nel programma

Il posto in attesa del tty e' **uno solo per tutto il sistema**, ed e' lo
stesso che usa la read della shell. Se poll lo lasciasse sporco uscendo, la
shell resterebbe muta al prompt per sempre.

La verifica non e' la riga «stdin senza nessuno che digita: scade e basta» —
e' il comando che si riesce a battere DOPO che `polltest` e' uscito:

    ex-os:/> hello
    Ciao da /bin/hello!

libctest: 294 prove superate, 0 fallite. `shmtest`: tutto a posto.

## L'impronta ABI

    [OK] impronta registrata: 5b7bdbaf5b0e36c2b7ab4c731da8cf90808f0f1b0a61be0012dd33e02a878ae8

`struct pollfd` e' entrata in `tools/abi-bersaglio.c`: il kernel scrive
`revents` dentro l'array del chiamante, quindi attraversa il confine come
`ShmZona` e `DirEntry`. Qui il rischio ha una forma sua — i campi sono `short`
per compatibilita' POSIX, e uno short che diventasse int porterebbe la
struttura da 8 a 12 byte: il kernel scriverebbe i revents della voce N nel
campo events della voce N+1, cioe' **risposte plausibili sulle domande
sbagliate**.

Nessun tipo esistente ha cambiato forma; e' stato rifatto il solo sysroot
(`tools/ricostruisci-bersaglio.sh libc`).

# Memoria condivisa fra processi (13 agosto 2026)

    ex-os:/> /bin/shmtest
    Memoria condivisa fra processi

      aprire una zona inesistente da' -ENOENT        ok
      creare la zona                                 ok
      riaprirla nello stesso processo da' -EEXIST    ok
    figlio: 8192 byte letti e riscritti, esco SENZA chiudere
      il figlio ha letto e verificato quello che avevo scritto ok
      vedo le modifiche del figlio (stessa memoria fisica) ok
      la zona e' viva dopo la morte del figlio       ok
      chiuderla rende 0                              ok
      chiuderla due volte da' -EINVAL                ok
      il nome e' tornato libero (ora da' -ENOENT)    ok
      la memoria libera e' quella di partenza        ok

    esito   tutto a posto

Gradino 0 punto 2 di `DIREZIONE.md`. `SYS_SHM_APRI` (242) e `SYS_SHM_CHIUDI`
(243): due processi, le stesse pagine fisiche, aperte **per nome** come i
servizi IPC.

    ShmZona z;
    strcpy(z.nome, "schermo");
    z.byte = 1228800;
    z.flag = SHM_CREA;          /* il primo la crea, gli altri mettono 0 */
    shm_apri(&z);

Prima due processi potevano scambiarsi dati solo con i messaggi IPC, che sono
da 1536 byte: una finestra 640x480x32 sarebbero ~800 messaggi **a
fotogramma**. Non e' lentezza, e' la struttura sbagliata.

## Il conteggio dei riferimenti sta nel PMM, ed e' la decisione che conta

Non nel gestore delle zone: **per pagina fisica**, dentro `pmm_free_page()`.

    if (g_rif[page] > 0) { g_rif[page]--; return; }   /* perde un proprietario */
    BITMAP_CLR(page);                                  /* era l'ultimo */

Li' passa OGNI strada che libera una pagina — `munmap`, la morte del
processo, un errore a meta' di `elf_load` — e nessuna di quelle sa che la
condivisione esiste. Un conteggio tenuto piu' in alto avrebbe richiesto di
correggere ogni sito di liberazione, e **ne basta uno dimenticato** perche' il
guasto sia una pagina riusata mentre qualcuno ci sta scrivendo dentro.

! La nota che chiedeva esattamente questo stava in `kernel/mm/paging.c` da
mesi: *«se in futuro si introduce memoria condivisa fra processi, questa
funzione andra' rivista per non liberare pagine ancora in uso altrove (serve
un conteggio riferimenti per pagina)»*. Con il conteggio nel PMM,
`paging_destroy_directory()` **non ha dovuto cambiare**: continua a liberare
tutto cio' che trova, e cio' che chiede al PMM e' «togli questo processo», non
«butta la pagina».

! IL VALORE MEMORIZZATO E' «QUANTI IN PIU' DEL PRIMO»: zero vuol dire un solo
proprietario, cioe' il caso normale. Cosi' allocare non deve toccare niente e
l'array nasce azzerato con il significato giusto. Un byte per pagina, subito
dopo la bitmap: 16 KB su una macchina da 64 MB, e li paga anche chi la
condivisione non la usa.

! IL TETTO E' UN RIFIUTO, NON UNA SATURAZIONE. Un conteggio che si ferma a 255
e poi non sale piu' fa liberare la pagina al 255esimo che se ne va, mentre gli
altri ce l'hanno ancora mappata.

## Le tre decisioni che si potevano sbagliare

**1. La zona muore con l'ultimo che la tiene aperta.** Niente `shm_unlink`: su
un sistema dove un processo che muore non lascia niente dietro, una zona che
sopravvive a tutti i suoi utenti e' memoria che nessuno liberera' mai piu'.
Conseguenza da sapere: **non si puo' creare una zona, riempirla e uscire**
perche' qualcun altro la trovi dopo.

**2. Chi si attacca NON sceglie la dimensione, la riceve.** `byte` e' anche un
campo di USCITA. Due processi con due idee diverse della stessa memoria sono
il difetto che questa interfaccia deve rendere impossibile — uno scrive dove
l'altro non guarda, o legge oltre la fine. Il figlio della prova chiede
apposta `byte = 1` e riceve 8192.

**3. `smonta` e' falso quando il processo e' MORTO.** Alla morte,
`paging_destroy_directory` ha gia' smontato tutto e gia' chiamato
`pmm_free_page` per ogni pagina. Rifarlo in `shm_cleanup_process` calerebbe il
conteggio **due volte**, e la pagina tornerebbe al PMM mentre un altro
processo ce l'ha ancora mappata. E' il difetto piu' probabile di tutto questo
lavoro, ed e' il motivo per cui quella funzione ha un parametro invece di due
copie.

## Due cose trovate scrivendo, che non erano nel progetto

! **`exec` doveva chiudere le zone, e non lo faceva.** Le pagine le mollava
gia' `paging_destroy_directory` sulla vecchia page directory, ma il PCB avrebbe
continuato a dire che il processo teneva aperte quelle zone — con indirizzi che
nella page directory nuova non vogliono dire niente. La zona sarebbe restata
viva per un utente che non la vede, e il suo nome occupato fino alla morte del
processo.

! **La morte di un driver stampava un errore vero su un'operazione senza
errori.** `paging_destroy_directory` liberava anche le finestre MMIO: non sono
RAM, non sono mai state allocate da noi, e il PMM rispondeva
«pagina fuori range» una volta per pagina. Adesso le salta, come gia' faceva
per il framebuffer. Non c'entra con la memoria condivisa: e' venuto fuori
guardando quella funzione, ed e' il genere di messaggio che insegna a ignorare
proprio la riga che un giorno servira'.

## Il blocco temporaneo per il pagefile NON c'e', ed e' una scelta

La direttiva 3 chiede di pensarci adesso. Ci si e' pensato: cio' che **non si
puo' aggiungere dopo** e' sapere che una pagina ha piu' di un proprietario, e
quello e' fatto. Un contatore di blocco che nessuno consulta sarebbe codice
morto oggi e non renderebbe piu' facile lo swap domani — quando ci sara' uno
swapper si aggiunge un campo a `ShmZonaK` e le due chiamate che lo muovono,
senza toccare niente di cio' che c'e' adesso.

## I numeri, e perche' sono una prova

`bin/shmtest` si rilancia da solo con `-f`: senza `fork()` e' l'unico modo di
avere due processi che si conoscono. La zona si riempie con **funzioni dei
soli indici** — `i*7+0x5A` per il padre, `i*13+0xA5` per il figlio — quindi
ogni byte ha una risposta giusta nota in anticipo e «sembra giusto» non e'
possibile.

Le due prove che valgono piu' delle altre:

- **la zona e' viva dopo la morte del figlio**, che muore APPOSTA senza
  chiudere. Se il conteggio non funzionasse, quelle pagine sarebbero tornate
  al sistema e il padre leggerebbe la memoria di qualcun altro;
- **`meminfo().free_kb` e' identico prima e dopo**. Non si perde una pagina.

libctest: 294 prove superate, 0 fallite.

## L'impronta ABI e' stata dichiarata, e qui c'e' il perche'

    tools/ricostruisci-bersaglio.sh --impronta
    [OK] impronta registrata: 1ff0600af3b122d528cbf99ac3e1e26f4ab7b2436f130c7eafbb5cf0c83dae82

`ShmZona` e' entrata in `tools/abi-bersaglio.c` **il giorno in cui nasce**,
invece che dopo un guasto: la regola scritta in testa a quel file dice «ogni
tipo che attraversa il confine fra la libc e un programma», e questa il kernel
la riempie per conto del chiamante come `DirEntry`.

! **L'impronta e' diventata rossa perche' la guardia si e' allargata, non
perche' qualcosa si sia rotto.** Nessun tipo esistente ha cambiato forma:
`ShmZona` e' nuova, e la libc ha due funzioni in piu' — cosa che non invalida
nessun binario gia' collegato. Sono stati comunque rifatti `libc`, `gcclibs`
(GMP/MPFR/MPC), `libm` e `libgcc`; NON `libstdc++`, i binutils nativi, `cc1` e
`openssl`, che restano collegati alla libc di prima ed e' corretto, perche' fra
le due l'ABI e' identica.

# Chi puo' toccare l'hardware: il varco rifatto (13 agosto 2026)

    /prova         (copia di e1000.drv)   mmio_map    -> RIFIUTATO
    /prova.drv     (la stessa copia)      mmio_map    -> servizio 'rete0' attivo

    /pprova        (copia di pci.drv)     ioport_bind -> RIFIUTATO
    /cdrom/dev/pci.drv                    ioport_bind -> 6 dispositivi

**Stesso binario, byte per byte, nella stessa macchina e nello stesso
istante.** Cambia solo il nome del file da cui e' stato caricato.

`ioport_bind`, `dma_alloc` e `mmio_map` — le tre syscall con cui un processo
ring 3 tocca un dispositivo invece di chiedere a qualcuno di toccarlo per lui
— passano adesso da un varco solo: il flag `is_driver` del PCB, che mette il
**caricatore** (`percorso_di_driver` in `kernel/loader/elf.c`) guardando il
nome dell'eseguibile.

## Cos'era, e perche' non teneva da NESSUNO dei due lati

    if (proc->io_port_count == 0) return ERR(EPERM);   /* «non e' un driver» */

! **Non teneva chiuso, ed era circolare.** Le porte le dava `ioport_bind`,
che non ha mai controllato niente. Due righe:

    ioport_bind(0x3F8, 1);      /* nessuno chiede chi sei */
    mmio_map(&zona);            /* e adesso sei un driver */

e un programma qualunque mappava i registri di un dispositivo qualunque. Non
era una difesa: era un passaggio in piu'. Da fuori pero' *sembrava* un
controllo, ed e' per questo che e' rimasto li'.

! **E non teneva aperto, che e' il motivo per cui e' stato rifatto ADESSO.**
Un framebuffer non ha NESSUNA porta I/O. Il primo utente vero di `mmio_map`
dopo l'e1000 — il server grafico, gradino 1 di `DIREZIONE.md` — sarebbe stato
respinto dal controllo messo li' per proteggerlo. L'e1000 passava solo perche'
una BAR I/O ce l'ha: il driver la prendeva **senza usarla**, per superare un
controllo. Quella `ioport_bind` adesso non c'e' piu', ed e' la misura del
lavoro.

## ! LA REGOLA E' IL NOME, NON LA DIRECTORY, e l'ha decisa una prova

La prima versione chiedeva `/dev/<nome>.drv`. Sarebbe stata piu' stretta e
avrebbe respinto **tutti i driver di rete**: si lanciano da `/cdrom/dev/`, e
sul sistema installato ci sono anche `/exos/dev` e `/cdrom/drivers`. Un EPERM
che parla di permessi mentre il problema e' il percorso.

Vale come metodo: la regola sbagliata l'ha trovata il comando con cui si era
provato il driver la sessione prima, non un ragionamento.

## ! E NON E' UNA BARRIERA. Va detto adesso, non scoperto dopo

Senza proprietari dei file, un programma puo' copiarsi in `x.drv` e ripartire
da li' — la riga `cp /cdrom/dev/e1000.drv /prova.drv` qui sopra e' esattamente
quello. **La barriera vera vuole un concetto di proprietario, e arrivera' con
quello.**

Quello che si guadagna, e che il criterio di prima non dava:

- non e' circolare: non si ottiene chiamando una syscall;
- non esclude chi driver lo e' davvero ma porte I/O non ne ha;
- la domanda non e' piu' posta a chi ha interesse a mentire — il nome e' un
  fatto fissato prima che il programma parta, letto dal caricatore quando il
  processo non esiste ancora.

## Tre dettagli che erano facili da sbagliare

! **Si ASSEGNA, non si aggiunge.** Un driver che esegue `/bin/sh` diventa
`/bin/sh`, e il varco si chiude dietro di lui. Fosse un OR, basterebbe un
driver qualunque per fare da scala a un programma qualunque.

! **Si assegna a caricamento RIUSCITO**, non in cima a `elf_carica`:
`sys_exec` su un errore rimette la vecchia page directory e il processo
prosegue con l'immagine di prima. Deciderlo prima vorrebbe dire togliere — o
dare — il privilegio a un processo che sta ancora eseguendo un altro
programma.

! **Ci passa anche `ioport_bind`, che prima non aveva NESSUN controllo.** Una
porta I/O e' pericolosa quanto un registro mappato in memoria: con 0xCF8 si
riprogrammano le BAR di qualunque dispositivo PCI, con 0x1F0 si scrive sul
disco sotto il filesystem.

## Le prove

Tutte in QEMU, avviando da floppy con il CD montato:

| | |
|---|---|
| rete | DHCP 10.0.2.15, `ping 10.0.2.2` **4/4, 0% persi** — con l'e1000 che non prende piu' nessuna porta |
| tastiera | `kbd.drv` all'avvio serve la console: `ipc_register('kbd')` di un secondo esemplare rende -17, quindi il primo e' vivo **oltre** la propria `ioport_bind` |
| libctest | **294 superate, 0 fallite** |

! La prova negativa su `ioport_bind` va fatta con **`pci.drv`, non con
`kbd.drv`**: quest'ultimo chiama `ipc_register` PRIMA di `ioport_bind`, quindi
una copia si ferma sul nome del servizio gia' preso e non arriva mai al varco.
Il primo tentativo e' finito li' e non dimostrava niente.

# La rete di QEMU funziona: SYS_MMIO_MAP e il driver e1000 (13 agosto 2026)

    ping 10.0.2.2 con 32 byte di dati
      60 byte da 10.0.2.2: seq=1 ttl=255 tempo<10 ms
    4 inviati, 4 ricevuti, 0% persi

La scheda predefinita di QEMU — Intel 82540EM — ora ha un driver, **in ring
3**, e con essa DHCP e ping funzionano su una macchina appena avviata.

## Il muro, e perché era vero

NE2000 e PCnet hanno i registri nello spazio I/O: `ioport_bind` più in/out, e
un driver in spazio utente basta a sé. L'e1000 no: **BAR0 è spazio di
memoria**, e mappare memoria fisica in un processo era una syscall che non
c'era.

Intel documenta un'alternativa — la finestra a porte `IOADDR`/`IODATA` della
BAR1 — e il driver è nato su quella. Non funziona, e la verifica è stata:

    scritto 0x00001234 in IOADDR, riletto 0x00000000  -> finestra MORTA

! **Il caso cattivo non era `0xFFFFFFFF`.** Una finestra assente su bus
aperto legge tutti *uno*; questa legge tutti **zero**, che somiglia a un
valore. Il primo controllo cercava `0xFFFFFFFF` e non lo prendeva: il reset
«riusciva» subito (`CTRL_RST` letto 0 sembra già finito), il MAC veniva
`00:00:00:00:00:00`, il servizio `rete0` si registrava, e la rete non avrebbe
funzionato mai senza che niente lo dicesse.

Poi confermato sul sorgente di QEMU, `hw/net/e1000.c`: `e1000_io_read` rende
0 ed `e1000_io_write` scarta tutto. Sono stub. Non c'era modo di girarci
intorno.

## SYS_MMIO_MAP (241)

    MmioZona m = { .fisico = d.bar[0], .byte = 0x8000 };
    mmio_map(&m);
    g_reg = (volatile unsigned char *)m.virt;

Tre cose che questo codice **deve** fare, e sono facili da sbagliare:

1. **rifiutare la RAM** — o è un buco verso il kernel. La finestra deve stare
   interamente sopra `pmm_get_total_pages() * PAGE_SIZE`, che è l'indirizzo
   più alto della memoria usabile (viene da E820).
2. **pagine non cacheabili** (`PG_CACHE_DIS | PG_WRITE_THRU`) — o le scritture
   ai registri restano in cache e le letture rendono valori vecchi. Il driver
   sembrerebbe funzionare e la scheda non riceverebbe mai i comandi.
3. **conservare l'offset dentro la pagina** — un BAR non è per forza allineato
   a 4096; arrotondare in giù e tacere darebbe un puntatore che sembra giusto.

E non si azzera la pagina, al contrario di `dma_alloc`: quelli sono
**registri**, scriverci zeri azzererebbe il dispositivo.

## ! Un difetto preso dal compilatore, e uno che resta

`SYSCALL_COUNT` era 241 (indici 0..240) e la syscall nuova è la 241:
`-Warray-bounds` l'ha detto. Dimenticarlo **non dà un errore a runtime**,
corrompe ciò che sta dietro l'array e il guasto si vede altrove. Ora è 242 e
il commento lo dice.

! **IL VARCO DI `mmio_map` È SBAGLIATO PER IL PROSSIMO CASO** — **chiuso
poche ore dopo, vedi la voce in cima.** Pretendeva che il processo avesse già
delle porte I/O: era così che il kernel distingueva «un driver» da «un
programma qualunque», lo stesso varco di `dma_alloc`. L'e1000 passava perché
una BAR I/O ce l'ha, inutile ma sua — il driver la prendeva senza usarla, solo
per superare il controllo.

**Un framebuffer non ha nessuna porta I/O**, quindi il server grafico ci
sbatteva contro il primo giorno. Adesso il criterio è il nome dell'eseguibile
(`*.drv`), letto dal caricatore.

## Gli offset dei registri erano giusti

Li avevo scritti a memoria, senza datasheet, e non erano mai stati eseguiti.
La prova che sono corretti non è che il driver parte: è che il **MAC letto
dalla EEPROM** è `52:54:00:12:34:56`, cioè quello che QEMU assegna, e che
`STATUS` vale `0x80080783` con i bit di link-up e full-duplex al posto giusto.
Valori sbagliati avrebbero dato numeri plausibili ma diversi.

# Il disco va in DMA bus master (12 agosto 2026)

    7,2 MB fra lettura e scrittura, dentro QEMU senza KVM:

        PIO   17,30  17,31  17,32  s      mediana 17,31
        DMA    9,90   9,90   9,88  s      mediana  9,89

    -43%, cioe' 1,75x. Scarto ±0,02 s su tre campioni per parte.

    [INFO] ATA: bus master IDE a 0xc040 (PCI 0:1.1)

E la prova che i dati sono giusti non e' un confronto di dimensioni: il file
copiato — 1.837.860 byte passati interamente dal DMA — **si esegue**.

    /disk/c1 -version
    FreeBASIC Compiler - Version 1.07.3 (2026-08-12), built for exos-x86

Un solo byte sbagliato in un eseguibile da 1,8 MB non lo farebbe partire.
libctest: 294 prove superate, 0 fallite.

## Come e' fatto

! **Il kernel non ha un driver PCI e non puo' averlo.** Il PCI di EX-OS sta
in spazio utente (drivers/pci/), ma il controller IDE serve PRIMA: il kernel
legge il disco per CARICARE i driver. Percio' in ata.c c'e' una lettura di
configurazione PCI ridotta all'osso — due porte, 0xCF8 e 0xCFC — che serve a
una cosa sola: trovare la BAR4. Non e' un secondo driver PCI e non deve
diventarlo.

! **Si passa sempre da un buffer di rimbalzo**, anche quando sembra inutile.
Il controller scrive in memoria FISICA; il buffer che arriva puo' essere del
kernel (identity-mapped, quindi utilizzabile) oppure UTENTE, con le pagine
sparse per la RAM. Distinguere i due casi vorrebbe dire fidarsi di chi
chiama. Rimbalzare costa una copia — molto meno di 256 `insw` per settore —
e non ha un modo di sbagliare.

! **Il buffer e' un array statico, non kmalloc**: sta nel BSS, che e'
identity-mapped e contiguo per costruzione. Un blocco dello heap potrebbe non
esserlo, e il DMA non se ne accorgerebbe — scriverebbe di seguito in memoria
fisica su pagine di qualcun altro. Allineato a 64 KB perche' una voce del
PRDT non puo' attraversare quel confine.

! **Se il DMA fallisce si ripiega su PIO**, e si riparte da capo con la
richiesta intera: un trasferimento interrotto puo' aver riempito il buffer a
meta', e ripetere una lettura non costa niente mentre fidarsi di dati
parziali costa tutto. Il ripiego si dice una volta sola nel log.

! **Si guardano DUE stati, non uno.** Il bus master dice se la copia e'
andata; se il disco ha trovato un settore illeggibile lo dice lui, con ERR
nel proprio registro. Fidarsi di uno solo vuol dire restituire dati sbagliati
senza errore.

## Il difetto: si scandiva solo la funzione 0 del PCI

    [INFO] ATA: nessun bus master IDE sul PCI, si resta in PIO

Vero riguardo a cio' che era stato guardato, e completamente fuorviante: il
controller IDE del PIIX3 — quello di QEMU, e il piu' comune sul ferro
dell'epoca — sta in **00:01.1**, perche' 00:01.0 e' il ponte ISA. E' un
dispositivo multifunzione. Ora si provano tutte e otto le funzioni: quelle
assenti rendono 0xFFFF, quindi non costa niente.

## ! E QUATTRO STRUMENTI DI MISURA SBAGLIATI PRIMA DI UNO GIUSTO

Vale la pena scriverlo, perche' e' costato piu' del driver:

 1. **Cronometrare le attese fisse del pilota** invece del lavoro: `@60` per
    quattro comandi sono 240 s di `sleep` dentro la misura. Uscivano 278 s.
 2. **Cercare `^marcatore` senza togliere i \r**: non combacia mai, e si
    finiva ad aspettare la fine dell'attesa. Uscivano **657 s identici** nelle
    due configurazioni — e due misure identiche a 0,02% avrebbero dovuto
    fermarmi subito, invece ci ho costruito sopra un'ipotesi.
 3. **Leggere la seriale della corsa PRECEDENTE**, che il marcatore ce
    l'aveva gia': 13 millisecondi.
 4. **Un campione per lato**: 21,6 contro 15,5 sembrava -28%, poi la stessa
    configurazione ripetuta ha dato 10,5. Il rumore era grande quanto
    l'effetto — perche' i miei `make` giravano insieme alle misure.

Adesso lo strumento cancella la seriale, aspetta che rinasca, e rifiuta un
risultato implausibile. ! E la soglia di plausibilita' l'avevo messa a 10 s,
tarata sui numeri rotti: appena il difetto e' sparito ha cominciato a
scartare le misure BUONE, perche' il DMA sta a 9,9. Una guardia calibrata su
un difetto diventa sbagliata il giorno che il difetto non c'e' piu'.

## Cosa resta da guadagnare sul disco

La cache a blocchi serve richieste fino a **8 settori** (CACHE_MAX_N), e `cp`
lavora a 4 KB: al disco arrivano richieste da 8 settori, mentre il buffer DMA
ne tiene 128. Leggere piu' avanti (readahead) userebbe il DMA per quello che
sa fare, invece che per trasferimenti da 4 KB.

# La directory corrente e' PER PROCESSO (12 agosto 2026)

    ex-os:/disk/uno> sh -c pwd
    /disk/uno                          <- il figlio EREDITA

    ex-os:/disk/uno> sh -c "cd /disk/uno/due; pwd"
    /disk/uno/due                      <- il figlio si sposta

    ex-os:/disk/uno> pwd
    /disk/uno                          <- e il padre NON si e' mosso

Prima l'ultima riga diceva `/disk/uno/due`.

## Cos'era

    kernel/syscall/syscall_impl.c:60
    static char g_cwd[PERCORSO_MAX] = "/";

! **UNA SOLA PER TUTTO IL SISTEMA.** Reggeva finche' a usarla era una shell
sola, e sotto quella soglia il difetto non si vede: e' cio' che lo rendeva
pericoloso. Bastano due processi che lavorano in posti diversi, e sono casi
normali:

    make -C sotto        spostava ANCHE la shell che l'aveva lanciato
    ( cd altrove; ... )  un subshell muoveva tutti
    make -j2             il secondo compilatore risolveva i propri percorsi
                         relativi contro la directory del primo

! Il terzo e' il peggiore perche' **non da' un errore**: da' un file aperto
nel posto sbagliato.

## Cos'e' adesso

`Process.cwd[VFS_PATH_MAX]` nel PCB, accanto a `console` e con la stessa
regola: **si eredita dal padre in sys_spawn**. Un programma lanciato dalla
shell parte da dove si trovava chi l'ha lanciato — senza, `gcc prova.c` dopo
un `cd /lavoro` cercherebbe /prova.c.

! Finche' la directory era di tutti, quella riga di eredita' non serviva: il
figlio la trovava gia' buona perche' era condivisa. E' l'altra faccia dello
stesso difetto.

! **NON e' un cambio di ABI**: il PCB e' memoria del kernel e nessun
programma lo vede. L'impronta resta valida, non serve ricostruire il
bersaglio. Costa 320 byte per PCB, 20 KB in tutto.

! `cwd_corrente()` rende "/" fuori da un contesto di processo, e non e' un
ripiego: al PASSO 13 il kernel monta i volumi e carica i driver girando nel
task IDLE, prima che esista un processo con una directory sua. La radice e'
l'unica risposta che non inventa niente. Un PCB nasce azzerato, quindi un
processo senza padre (init, i driver) parte da li'.

libctest: 294 prove superate, 0 fallite.

## Perche' adesso

E' il primo prerequisito di qualunque discorso su piu' CPU. Con SMP quella
variabile sarebbe stata una corsa vera; ma era gia' un difetto con un core
solo, e andava chiuso comunque.

! Restano **36 siti** che usano `interrupts_disable()` come mutua esclusione:
funziona solo su un processore. Sono il vero lavoro dell'SMP, insieme a 112
variabili globali mutabili nel kernel. La strada praticabile e' quella di
Linux 2.0 — avvio SMP con UN lucchetto unico del kernel, cosi' lo spazio
utente gira in parallelo (che e' dove `cc1` passa il tempo) mentre il kernel
resta serializzato — e poi spezzare il lucchetto dove il profilo lo chiede.

# FreeBASIC e' AUTOSUFFICIENTE dentro EX-OS — punto fisso (12 agosto 2026)

    gen0  cross su Linux, sul CD        1.588.524   costruisce gen1
    gen1  make dentro EX-OS             1.837.832   costruisce gen2
    gen2  make dentro EX-OS             1.837.860   costruisce gen3
    gen3  make dentro EX-OS             1.837.860   <- IDENTICA alla gen2

    ex-os:/disk/fb/freebasic> bin/fbc -version
    FreeBASIC Compiler - Version 1.07.3 (2026-08-12), built for exos-x86 (32bit)
    ex-os:/disk/fb/freebasic> bin/fbc /disk/p.bas -x /disk/p3
    ex-os:/disk/fb/freebasic> /disk/p3
      quadrati     385
      esito       tutto a posto

! **I 28 byte fra gen1 e gen2 NON erano un difetto**, ed e' stato un
esperimento a dirlo invece di un ragionamento. gen1 e' collegata contro la
libfb.a del CD, dove `fb_ExecEx` e `fb_hShell` stanno in UN oggetto
(`exos_sys_exec.o`); gen2 contro quella costruita dal makefile, dove stanno in
DUE. Riempimento fra le sezioni diverso. Se l'ipotesi fosse stata sbagliata,
gen3 sarebbe uscita con un terzo numero: e' uscita uguale a gen2.

! E gen3 dice `2026-08-12` dove gen2 diceva `2026-08-11` — **data diversa,
dimensione identica**. `fb.bi` fa `const FB_BUILD_DATE = __DATE__`, quindi i
due binari non sono uguali byte a byte e non possono esserlo; `__DATE_ISO__`
ha lunghezza fissa, ed e' per questo che il confronto giusto e' la dimensione.

## Il comando

    make TARGET_OS=exos TARGET_ARCH=x86 DISABLE_MT=1 \
         CFLAGS="-O2 -DDISABLE_FFI" rtlib
    make TARGET_OS=exos TARGET_ARCH=x86 DISABLE_MT=1 FBC=bin/fbc compiler

! `FBC=bin/fbc` e' la riga che rende la prova una prova: il valore
predefinito e' `fbc`, che il PATH risolve su `/cdrom/exos/bin/fbc` — la gen0.
Senza, si ricostruirebbe sempre con il compilatore del CD.

## QUATTRO difetti in piu', trovati arrivando in fondo

Tutti e quattro con la stessa forma: **una prova piccola che passa e un carico
vero che no.**

### 1. `rm -r` ne cancellava SEDICI

Su venti file ne toglieva sedici e lasciava gli altri. La readdir() della libc
non tiene un cursore: chiede al kernel «le voci da START in poi» e ne riceve un
blocco di LISTDIR_MAX_BATCH (sedici). Cancellare sposta le voci successive,
quindi al blocco dopo lo START punta oltre quelle rimaste.

! **Sotto i diciassette file non si vede**, perche' l'elenco intero entra nel
primo blocco — letto PRIMA che si cancelli qualcosa. La prova con sei file
passava. Ora si rilegge la directory da capo dopo ogni cancellazione: nessuna
iterazione attraversa una modifica.

### 2. ! ext2 NON SCRIVEVA LE DATE, e make non poteva funzionare

`inode_nuovo()` lasciava i_atime/i_ctime/i_mtime a zero, con un commento che
diceva «EX-OS non ha un orologio letto dal kernel». **Vero quando fu scritto,
falso da quando esiste `rtc_read()`** — che FAT12 usa gia' (fat12_ora_corrente).
Era rimasto indietro un filesystem su due.

Con tutte le date a zero, `bin/fbc` (zero) non risulta mai piu' vecchio dei .o
(zero):

    make: Nothing to be done for 'compiler'.

dopo aver ricompilato tutti e 145 i sorgenti. **make funzionava solo quando il
bersaglio non esisteva** — un costruttore da zero, non un ricostruttore.

! E io avevo scritto il contrario: quando make disse «'prova' is up to date»
l'ho preso come prova che il confronto delle date funzionasse. Con tutte le
date a zero si ottiene la stessa identica risposta. La prova vera —
`touch prova.h` e vedere se ricompila — era scritta nel commento di
tools/iso/prova-make/prova.h e non l'avevo mai eseguita. Adesso passa.

! La data si scrive in ext2_write, NON dentro scrivi_inode(): quella riscrive
l'inode anche per le directory padre, e datare li' vorrebbe dire che creare un
file "modifica" ogni file della directory.

### 3. `mv` non sostituiva la destinazione

`rename()` di EX-OS si rifiuta di sovrascrivere — e per LEI e' giusto, vedi
bin/rename/rename.c. Ma `mv` e' l'altro comando, e la sostituzione E' la
richiesta: `mv bin/fbc-new bin/fbc` e' l'ultima riga della ricetta del
compilatore. Si otteneva

    mv: bin/fbc-new -> bin/fbc: esiste gia'

DOPO che il collegamento era riuscito. Ora toglie e riprova. ! Non e'
atomico, e sta scritto: su POSIX `rename()` sostituisce in un colpo solo, qui
c'e' un istante in cui il nome non punta a niente.

### 4. Uno scontro di NOMI nel nostro strato di porting

`tools/freebasic-exos/exos/sys_exec.c` aveva lo stesso nome di
`src/rtlib/sys_exec.c`. Il makefile di FreeBASIC appiattisce gli oggetti in una
directory sola e usa VPATH: due sorgenti con lo stesso nome danno lo stesso
oggetto, `$(sort)` li fonde, e VPATH trova prima `src/rtlib/`. **Il nostro file
non veniva compilato mai**, e libfb.a usciva senza `fb_ExecEx` e `fb_hShell`.

! **E non si vedeva costruendo in croce**: prepara-fb.sh compila `exos/*.c` a
mano con il prefisso `exos_` sugli oggetti, quindi li' lo scontro non esiste.
La libfb.a del CD era completa e quella del makefile no — due strade per lo
stesso risultato, e una sola sbagliata.

Diviso in `sys_execex.c` + `sys_hshell.c`, che e' come si chiamano in unix/,
dos/, win32/ e xbox/: la convenzione c'era gia'. E `applica.py` adesso
**rifiuta di applicare** se un nome dello strato collide con il livello comune.

## ! Senza segnali, un bersaglio scritto a meta' sopravvive

Terminando la macchina mentre girava `ar`, e' rimasta una `libfb.a` di 8 byte —
la sola firma `!<arch>` — **piu' recente dei propri ingredienti**. La
costruzione successiva l'ha accettata come buona ed e' andata al link con un
archivio vuoto.

Su Linux make intercetta il Ctrl-C e cancella il bersaglio a meta'. Qui non
puo': EX-OS non consegna segnali. E' il rovescio concreto di quella scelta, e
va ricordato ogni volta che si interrompe una costruzione: **cancellare a mano
il file che si stava scrivendo.**

# FreeBASIC si ricostruisce DENTRO EX-OS (11 agosto 2026)

    ex-os:/disk/fb/freebasic> bin/fbc -version
    FreeBASIC Compiler - Version 1.07.3 (2026-08-11), built for exos-x86 (32bit)

    ex-os:/disk/fb/freebasic> bin/fbc /disk/p.bas -x /disk/pnew
    ex-os:/disk/fb/freebasic> /disk/pnew
    prova-fb — compilato dentro EX-OS
      saluto      EX-OS e FreeBASIC
      quadrati     385
      esito       tutto a posto

Non e' l'fbc del CD: **1.837.832 byte contro 1.588.524**. E' quello che `make`
ha collegato dentro EX-OS, e compila.

    427 file C  -> gcc      -> libfb.a (567.810 byte) via `ar rcs` con ~200 argomenti
    145 .bas    -> fbc      -> bin/fbc (1.837.832 byte)

I sorgenti stanno sul CD in `/freebasic` (7,9 MB, 1022 file). Il comando:

    make TARGET_OS=exos TARGET_ARCH=x86 DISABLE_MT=1 \
         CFLAGS="-O2 -DDISABLE_FFI" rtlib
    make TARGET_OS=exos TARGET_ARCH=x86 DISABLE_MT=1 compiler

! `DISABLE_MT=1` non e' un'ottimizzazione: senza, il makefile costruisce anche
`libfbmt.a`, cioe' altri 427 file (~2,7 ore) per una runtime con i thread che
EX-OS non ha. Il makefile ha il flag apposta — lo usa gia' per Xbox — e
`prepara-fb.sh`, il cross su Linux, `libfbmt.a` non l'ha mai costruita.

## Due difetti, e li ha trovati il carico VERO

! Entrambi sono usciti **dopo** che tutto il resto era passato. E' il modo in
cui si presentano i tetti tarati su «quello che si e' visto finora».

### 1. Un argomento non poteva superare 319 byte — dopo 427 file su 427

    CPPAS src/rtlib/obj/exos-x86/cpudetect.o
    [ERROR] SYSCALL spawn('/bin/sh'): argomento 2 illeggibile o piu' lungo di 319 byte
    make: fork: argomento non valido

Avevo alzato i tetti dello spawn **a meta'**: il NUMERO di argomenti (64→512) e
l'arena (4→32 KB) si', la lunghezza del SINGOLO argomento no. Era
`PERCORSO_MAX`, 320 byte, con la motivazione scritta nel kernel:

> «Un argomento e' quasi sempre un PERCORSO, quindi il tetto e' lo stesso.»

«Quasi» era la parola sbagliata. L'argomento 2 di `/bin/sh -c <riga>` non e' un
percorso: **e' una riga di comando intera**, e make ne passa una ogni volta che
la ricetta contiene un metacarattere. Le ricette di FreeBASIC cominciano tutte
con `@echo "CC $@";` — c'e' un punto e virgola, quindi passano tutte dalla
shell.

! **E' per questo che 426 file su 427 sono passati**: stavamo appena sotto il
tetto per l'intera costruzione. La riga di `cpudetect.s` ha in piu'
`-x assembler-with-cpp`, ventidue caratteri, e ha traboccato. Un limite che si
supera in funzione della LUNGHEZZA DEL NOME di un file si scopre a caso, mesi
dopo, su un file che non c'entra niente.

Ora `MAX_ARG_LEN` e' `SPAWN_ARENA_BYTES`: un singolo argomento non puo'
comunque superare lo spazio che li tiene tutti, quindi un secondo numero piu'
piccolo aggiungeva un modo di fallire e nessuna protezione.

### 2. Sei header di FreeBASIC mancavano dal CD

    src/compiler/fbc.bas(13) error 23: File not found, "file.bi"

La regola del CD dice di copiare «cio' che corrisponde a cio' che c'e'»,
escludendo i binding ad allegro, GTK, SDL. Giusta — applicata male:
`file.bi`, `datetime.bi`, `string.bi`, `dir.bi`, `vbcompat.bi` e `utf_conv.bi`
**non sono binding a niente**: dichiarano funzioni che stanno dentro `libfb.a`,
cioe' dentro una libreria che il CD porta gia'. Per quella regola dovevano
esserci dal primo giorno.

! E il difetto era piu' largo del sintomo: mancavano anche a chi scrive un
programma proprio. `#include "file.bi"` e' la riga piu' comune di FreeBASIC
dopo `print`. Sono 7,7 KB.

## Quanto ci mette, misurato

    gcc dentro EX-OS   ~2,7 file al minuto   -> 427 file = quasi 3 ore
    fbc dentro EX-OS   ~9   file al minuto   -> 145 .bas = un quarto d'ora

! E si puo' interrompere: gli oggetti stanno su ext2 e `make` riprende da
dove era. E' servito quattro volte in questa sessione.

## L'impronta ABI e' stata DICHIARATA, e qui c'e' il perche'

    tools/ricostruisci-bersaglio.sh --impronta
    [OK] impronta registrata: 8b406d009816714840ef121ad256d3f5b57950dd973770f5110380c368ee155d

! **Non e' stata fatta la ricostruzione completa.** `DirEntry` e' passata da
264 a 268 byte; sono stati rifatti la libc e RICOLLEGATI binutils, cc1, gcc,
g++, cpp, collect2, fbc e make. NON sono stati rifatti `gcclibs` (GMP, MPFR,
MPC), `libm` (openlibm), `libgcc`, `libstdc++` e `openssl`.

Lo script avverte che quella dichiarazione «vale quanto chi la fa». Ecco su
cosa poggia, verificato e non supposto:

**1. Nessuna libreria di terzi incorpora la forma di `DirEntry`.** L'unica API
che la fa attraversare e' `listdir_from()`, e cercando chi la riferisce:

    libc.a        SI (la definisce)
    libgmp.a  libmpfr.a  libmpc.a  libm.a
    libstdc++.a  libsupc++.a  libgcc.a  libcrypto.a      -> nessuna

**2. Chi legge directory passa da `opendir`/`readdir`, la cui ABI non e'
cambiata.** `libstdc++.a` (4 riferimenti) e `libcrypto.a` (2) le usano, ma come
simboli ESTERNI: tengono un `DIR*`, che e' opaco e lo alloca la libc, e
`struct dirent` non e' stata toccata — la sua forma e' identica prima e dopo.

**3. Gli eseguibili che le collegano sono stati tutti ricollegati** contro la
libc nuova. `provassl`, `provamp`, `provamat` e `provacpp` non compaiono
nell'elenco perche' la regola dell'ISO li RICOMPILA a ogni `make iso`.

! Se un giorno cambiasse `struct dirent` invece di `DirEntry`, questo
ragionamento NON varrebbe: quella sta dentro il codice di chi la usa. Allora
si ricostruisce tutto davvero.

## L'errore da non rifare

! **Non lanciare `make all` mentre QEMU gira.** Riscrive `dist/floppy.img`
sotto la macchina in esecuzione, e le ricette caricano `/bin/sh` da li':

    [ERROR] ELF: magic non valido
    [ERROR] SYSCALL spawn: ELF load fallito per '/bin/sh' (err=-1)

Tre ore di compilazione fermate a meta'. (Non perse: make riprende.) Vale
anche per `hd.img` con due QEMU insieme — quello corrompe davvero.

# Il kit di costruzione dentro EX-OS: make, ar, ranlib (11 agosto 2026)

    /disk/pm> make
    gcc -O2 -c main.c -o main.o
    gcc -O2 -c conta.c -o conta.o
    gcc -O2 -c somma.c -o somma.o
    rm -f libprova.a; ar rcs libprova.a conta.o somma.o
    ranlib libprova.a
    gcc -O2 -o prova main.o libprova.a

    /disk/pm> ./prova
      somma dei quadrati : 385   (atteso 385)
      esito              : tutto a posto

    /disk/pm> make
    make: 'prova' is up to date.

385 e' noto in anticipo. L'ultima riga vale quanto le altre: il confronto
delle date funziona, quindi e' `make` e non uno script che rifa' tutto.

Sul CD degli strumenti ci sono adesso **GNU make 4.2**, **ar, ranlib, nm,
strip, objcopy, objdump** (erano gia' compilati in `~/exos-native` da giorni
e ne copiavamo due su otto), e i **sorgenti di FreeBASIC** in `/freebasic`.
Nel sistema di base: **rm**, **mv**, **uname**, e `mkdir -p`.

Come si rifa': `tools/make-exos/prepara-make.sh`, poi `make iso`. Il racconto
completo del porting sta in `tools/make-exos/leggimi.md`.

## Il porting di make e' UNA riga; il resto era sotto

`child_execute_job` di GNU make e' `vfork()` + `dup2` dei tre descrittori +
`exec`: cioe' la definizione a parole di `spawn_ex`. Trenta righe.

! **NON si usa il ramo `__MSDOS__`**, che pure c'era ed e' la strada che un
sistema senza fork sembra dover prendere: quello esegue il comando con
`P_WAIT` dentro `child_execute_job` e poi finge di avere un figlio da
raccogliere. Costa il parallelismo. Qui il figlio e' un figlio vero e
`reap_children` resta quella di sempre — `make -j2` funziona.

## SEI difetti trovati per strada, e tutti accusavano qualcun altro

**1. La shell eseguiva UN comando per riga.** Niente `;`, `&&`, `>`, `|`.
make non spezza le ricette: consegna la riga intera a `/bin/sh -c`, e la
prima della runtime di FreeBASIC e' `rm -f $@; $(AR) rcs $@ $^`. La shell
cercava un programma chiamato «rm -f libfb.a; ar».

**2. `O_APPEND` era accettato e ignorato**: `>>` sovrascriveva. `vfs_open` lo
ammetteva fra i flag validi, `sys_open` lo copiava nel descrittore, e poi la
posizione partiva da zero come per ogni altro file.

**3. `_libc_start` chiamava `main` con DUE argomenti.** La terza forma —
`main(argc, argv, envp)` — non e' nello standard C ma esiste su ogni Unix, e
make la usa: la prima cosa che fa dopo l'avvio e' scorrere `envp`. Riceveva
quello che c'era sullo stack:

    [FAULT] page fault a 0x00000000 (protezione, lettura, EIP=0x08000450)

prima di stampare una riga. Su i386 la convenzione e' cdecl, quindi passarne
tre a un main che ne dichiara due non rompe niente.

**4. ! `readdir()` metteva `d_ino = 0` su OGNI voce.** E' il gemello di
`st_first_clus`, e la lezione e' la stessa detta la seconda volta: **un campo
di identita' riempito con una costante non e' "non implementato", e'
un'informazione FALSA**, e chi la legge non ha modo di accorgersene.

Su Unix `d_ino == 0` marca una voce CANCELLATA, e il codice di terzi salta
quelle voci — GNU make lo fa in `dir.c` con `REAL_DIR_ENTRY`. Quindi make
vedeva **ogni directory vuota**, e il sintomo non parlava di directory:

    make: *** No targets specified and no makefile found.  Stop.

dentro una directory con dentro un `makefile` che `ls` elencava e che
`make -f makefile` leggeva senza storie. La differenza fra le due strade e'
che quella senza `-f` passa dalla lettura della directory.

Ora `DirEntry` porta `ident`, composto con `VFS_IDENT` — la stessa macro di
`stat_interno()`, cosi' `d_ino` e `st_ino` combaciano per lo stesso file. Ogni
driver l'identita' ce l'aveva gia' pronta: ext2 l'inode, ISO l'extent, FAT il
primo cluster; si fermava dentro il VFS.

! **Ha cambiato l'ABI** (264 -> 268 byte), e `DirEntry` e' entrata
nell'impronta di `tools/abi-bersaglio.c` **da questa volta**: `struct dirent`
c'era gia', ma quella la costruisce la libc per conto suo — la struttura che
attraversa la syscall e' `DirEntry`, ed era fuori.

**5. `make clean` non cancellava niente e non lo diceva.** `rm -f *.o`: la
shell non espandeva i jolly (per scelta — li espandono `delete` e `cp`, come
su MS-DOS) e `rm` e' il comando POSIX, che non espande perche' su POSIX lo fa
la shell. Con il `-f` che tace sui file assenti, il risultato era silenzio e
oggetti vecchi riusati alla compilazione dopo.

**6. `mkdir` non conosceva `-p`,** e `uname` era solo un built-in della
shell. Su quest'ultimo il ragionamento sbagliato era: «`$(shell ...)` passa da
`/bin/sh -c`, quindi il built-in basta». **GNU make non passa dalla shell per
un comando senza metacaratteri**, lo lancia diretto. Cercava un eseguibile e
rispondeva «fork: file o directory inesistente» — un errore che parla di fork
mentre a mancare e' un programma.

## Due trappole nel farlo, che valgono per la prossima volta

! **`mkdir -p` NON si scrive come «prova mkdir e perdona EEXIST».** Quella
strada presume di sapere quale errore significa «c'e' gia'», e non e' sempre
EEXIST: su un **punto di montaggio** il kernel risponde EBUSY.
`mkdir -p /disk/a/b/c` si fermava sul primo pezzo. Si chiede `stat` prima.

! **`uname -s` NON viene da OSNAME di kernel.cfg**, anche se sta li' ed e'
configurabile. OSNAME vale «EX OS» — con uno spazio, perche' e' un nome da
mostrare — e `uname -s` e' un token che gli script tagliano. Peggio: se
venisse da un file, cambiarlo cambierebbe di nascosto il ramo che ogni
makefile del disco imbocca. L'identita' del sistema non e' una preferenza.

## I tetti del kernel che la riga di comando di FreeBASIC ha sfondato

    ar rcs libfb.a <~200 oggetti>      la libreria della runtime
    fbc -x fbc <145 oggetti>           il link del compilatore

- `MAX_SPAWN_ARGS` da 64 a **512**, e gli array che lo dimensionano sono
  passati dallo stack del kernel allo heap, in una struttura sola
  (`SpawnBuf`) per non moltiplicare le nove uscite d'errore;
- l'arena degli argomenti da 4 a **32 KB**;
- ! e soprattutto lo **stack iniziale del figlio adesso si dimensiona sulla
  riga di comando vera** (`elf_load_argv`), invece di essere la costante
  `USER_STACK_INIT`. Le stringhe di argv le scrive `sys_spawn` dall'esterno,
  con `paging_get_physical`: una pagina non mappata non genera nessun fault
  che possa mapparla, perche' non c'e' nessun processo in esecuzione a cui il
  fault possa capitare. Lo spazio dev'esserci PRIMA.

E nella shell c'era un **secondo tetto, piu' basso e nascosto**: `run_program`
copiava argv in un `char *spawn_argv[32]` fermandosi a 31 **senza dirlo** —
cioe' proprio il troncamento silenzioso che il kernel aveva smesso di fare,
rifatto un livello piu' su. Adesso non c'e' nessuna copia.

## Rimasto aperto

! **L'impronta ABI e' rossa e va dichiarata a mano.** Dopo il cambio di
`DirEntry` sono stati ricostruiti la libc e ricollegati binutils, cc1/gcc/g++/
cpp, fbc e make. NON sono stati rifatti `gcclibs`, `libm`, `libgcc`,
`openssl`: sono librerie matematiche e crittografiche che `DirEntry` non la
nominano, e `struct dirent` non e' cambiata. Se il ragionamento regge:

    tools/ricostruisci-bersaglio.sh --impronta

! Quella dichiarazione **vale quanto chi la fa**, e questa nota e' l'unica
cosa che la rende verificabile.

# `toolinst -a`: aggiorna solo cio' che sul CD e' cambiato (10 agosto 2026)

    toolinst -a /disco
    Confronto di /disco/exos con /cdrom/exos
      cambiati   92 file (92 nuovi, 0 sostituiti)
      invariati  220 file: non li tocco
      copio      2314 KB

Prima ogni rilancio ricopiava l'albero intero — 52 MB per il solo gruppo C,
139 con tutto. Ora si rilancia dopo un `make iso` e si paga solo il
delta. Elenca prima, chiede poi, copia per ultimo: la stessa forma di
`install -a`.

## ! LE DATE NON SERVONO A NIENTE, SU UN CD

È il punto che decide il progetto, e non era ovvio. `install -a` confronta
dimensione **e data**, e lì è giusto. Sul CD no: `tools/mkiso.py` scrive in
**ogni** record la data di costruzione dell'ISO — non quella del file — e
c'è pure il commento che spiega perché (`_COSTRUZIONE`, «meglio un dato
vero che uno plausibile»).

Conseguenza: dopo ogni `make iso` tutti i file risultano più recenti di
tutto. Un aggiornamento che si fidasse delle date ricopierebbe 139 MB per
un binario cambiato, **e direbbe di aver aggiornato senza aver distinto
niente** — cioè il tipo di risposta peggiore, perché sembra giusta.

Resta il contenuto. Quando le dimensioni combaciano si leggono i due file
per intero e ci si ferma alla prima differenza. Costa qualche minuto e si
paga una volta sola, nella passata che precede la domanda.

**Provato in entrambe le direzioni**, che è ciò che rende il numero
credibile:

- gruppo `base` già installato e CD rifatto → **0 cambiati, 220 invariati**,
  0 KB copiati. Le date avrebbero detto 220 su 220;
- gruppo `fb` mai installato → **92 nuovi**, 2314 KB, e i 220 di prima non
  toccati.

Poi, dal disco: `fbc /t2.bas -x /t2` → 5 / 385 / 36, i valori attesi.

---

# FreeBASIC gira dentro EX-OS — bootstrap CHIUSO (10 agosto 2026)

    fbc prova.bas -x prova           (codice 0)
    prova
      quadrati     385               (atteso 385)

`fbc -version` dentro EX-OS dice **`built for exos-x86 (32bit)`**: il
bersaglio `exos` è nei sorgenti del compilatore e il bootstrap è stato
rigenerato da zero. Compila, assembla e collega in un comando solo.

Il racconto, i quattro passi per rifarlo e i tre difetti trovati per strada
stanno in **`tools/freebasic-exos/bootstrap-exos.md`**.

## ! Quello che manca: gli header `.bi` del C

Un `.bas` che non include niente compila e gira. Uno con `#include
"crt.bi"` no:

    crt/sys/types.bi(21) error: Platform unsupported

Sono 16 header in `inc/crt/` che si diramano su `__FB_LINUX__`,
`__FB_DOS__`, `__FB_WIN32__` e non conoscono `__FB_EXOS__`.

! **E NON si chiude aliasando `__FB_EXOS__` a `__FB_LINUX__`.** `time_t`
qui è `long long` (8 byte) e su Linux x86 è `long` (4); `dev_t` è 4 contro
8. Un `.bi` che sbaglia un tipo compila benissimo e dà numeri sbagliati —
è la stessa famiglia del difetto che è già costato due giorni quando
`struct stat` è cresciuta di 12 byte. La tabella completa dei tipi, presa
da `lib/include/libc.h`, è in `bootstrap-exos.md`.

La prova che chiude quel lavoro esiste già: `tools/iso/prova-fb2.bas`, il
gemello di `prova-fb.bas` **con** l'include.

---

# Dove riprendere — 10 agosto 2026, notte fonda

> ! **SUPERATO POCHE ORE DOPO, e resta perché la diagnosi serve.** Il
> punto 1 (`-o` è l'oggetto, `-x` l'eseguibile) vale ancora ed è
> permanente. Il punto 2 — il link che fallisce — è **chiuso**: vedi in
> cima. E i percorsi `lib/freebasic/linux-x86/` qui sotto ora sono
> `lib/freebasic/exos-x86/`, perché la directory si chiama come il
> bersaglio.

## FreeBASIC: `fbc prog.bas -o prog` non dà un eseguibile, e ci sono DUE ragioni

La prima non è un difetto di EX-OS. Dall'aiuto di `fbc` stesso:

    -o <file>   Set .o (or -pp .bas) file name for prev/next input file
    -x <file>   Set output executable/library file name

**In FreeBASIC `-o` è il nome dell'OGGETTO**, non dell'eseguibile. Con `-o
prog` quello che resta sul disco è un ELF rilocabile, e infatti:

    [ERROR] ELF: tipo non supportato (type=1, atteso ET_EXEC=2)
    exec: /prog: non e' un programma eseguibile

Il `-v` lo dice chiaro, e va letto: `assembling: … -o "/t"` — il nome
finale finisce all'ASSEMBLATORE.

### La seconda: nemmeno `-x` basta, e la nota vecchia sbagliava la causa

! **`fbc` NON affida il link a `gcc`**, come si è scritto qui fino a oggi:
chiama `ld` direttamente (`fbcRunBin("linking", FBCTOOL_LD, …)`). Il
problema è la riga di comando che gli costruisce per il bersaglio Linux —
`crt1.o` (noi abbiamo `crt0.o`) e `-dynamic-linker /lib/ld-linux.so.2`.

Il primo `hFindLib()` che fallisce esce **prima di stampare la riga
`linking:`**: con `-v` si vede `assembling:` e poi più niente, e il sintomo
manda a cercare il guasto nell'assemblatore, che invece ha finito bene
(uscita 0).

### ! E il comando di riferimento nel leggimi era ROTTO

Puntava a `/cdrom/exos/lib/fbrt0.o` e `/cdrom/exos/lib/libfb.a`: quei due
file si sono spostati il 6 agosto in `lib/freebasic/linux-x86/` con
l'adozione della disposizione non-standalone, e la ricetta non è stata
aggiornata. Chi la copiava otteneva

    ld: cannot find /cdrom/exos/lib/fbrt0.o: file o directory inesistente

**Una ricetta di riferimento che non funziona è peggio di nessuna
ricetta**: fa credere che sia rotto il sistema. Corretta e riprovata per
intero il 10 agosto — dà 385, il valore atteso:

    fbc -c -m t /t.bas -o /t.o
    ld -static -e _start -Ttext-segment=0x08000000 -o /t \
       /cdrom/exos/lib/crt0.o \
       /cdrom/exos/lib/freebasic/linux-x86/fbrt0.o \
       /t.o \
       /cdrom/exos/lib/freebasic/linux-x86/libfb.a \
       /cdrom/exos/lib/libc.a /cdrom/exos/lib/libm.a /cdrom/exos/lib/libgcc.a

Ora sta anche in `help fbc`, che è dove uno la cerca.

### La via breve per chiudere davvero il link

`fbcFindBin()` (fbc.bas, riga 362) prende il percorso del linker dalla
variabile d'ambiente `LD` **prima** di cercarlo nella propria bin. Un
programmino che riceve la riga Linux, la riscrive (via `crt1.o` e
`-dynamic-linker`, dentro `crt0.o` e `-static`) e chiama il vero `ld`
renderebbe `fbc prog.bas -x prog` un comando solo, senza toccare i
sorgenti di FreeBASIC. È un cerotto, ma sta tutto dentro EX-OS. La cura
vera resta il bersaglio `exos` nei `.bas` e il bootstrap rigenerato, che
vuole un `fbc` che giri su Linux.

---

# Dove riprendere — 10 agosto 2026, notte

## `install -tools`: l'installatore degli strumenti

    install -tools               nel sistema in esecuzione
    install -tools /disco        nel sistema montato in /disco
    install -tools -n            dice cosa farebbe
    install -tools -y -g base    solo il gruppo C, senza chiedere

! **Non è una seconda implementazione**: lancia `/bin/toolinst` e gli passa
gli argomenti. Le due installazioni si somigliano da fuori e non hanno
niente in comune dentro — `install` scrive MBR, settore di avvio e mappa
dei settori, `toolinst` copia un albero di 139 MB conservandone la forma.
L'opzione sta in `install` perché è lì che la si cerca.

### Il catalogo lo porta il CD

`/cdrom/exos/strumenti.txt` (sorgente: `tools/iso/strumenti.txt`) dichiara i
gruppi: nome, come riconoscerli sul CD, da cosa dipendono, quanto pesano, e
i percorsi che sono **solo** loro. Aggiungere uno strumento al CD non
richiede più di ricompilare l'installatore.

    [cpp]
    nome  = C++
    prova = bin/g++
    vuole = base
    mbyte = 77
    solo  = include/c++
    file  = cc1plus

! **Si dice cosa è SOLO di un gruppo, non cosa copiare.** Tutto ciò che
nessun blocco rivendica sta nella base e si copia comunque: un file nuovo
sul CD finisce sul disco invece di restare indietro in silenzio. È la
regola che c'era già dentro `da_saltare()`, spostata nel catalogo.

Un CD senza catalogo si installa lo stesso, con quello di scorta compilato
dentro — i CD già masterizzati non ce l'hanno.

## ! TRE DIFETTI, e tutti e tre rendevano il sistema installato inservibile

Nessuno era in `toolinst`. Sono venuti fuori provando su un disco vero
(`make hd`), non leggendo.

### 1. I driver si installavano MAIUSCOLI su ext2

`hwconfig -d` copiava con il nome che il filesystem sorgente consegna: da
un floppy FAT arriva `KBD.DRV`. Scritto tale e quale su ext2 diventa
`/dev/KBD.DRV`, e il kernel cerca `/dev/kbd.drv` — due file diversi.

    [WARN] Driver 'kbd': '/dev/kbd.drv' non caricato (err=-2)
    [WARN] Tastiera: ripiego sull'handler IRQ1 in-kernel

**Ogni EX-OS installato su ext2 si avviava senza il proprio driver di
tastiera**, mentre `ls /dev` mostrava il file al suo posto. La regola era
già scritta in testa a `bin/install/install.c` e applicata ai suoi file:
questa copia le era sfuggita perché è l'unica che non passa di lì.

### 2. Le domande dell'installatore si rispondevano da sole

`install` lanciava `toolinst` senza cedergli il primo piano. `sys_read`
serve **solo** il processo in primo piano: ogni `read()` di toolinst
rendeva zero, e zero per `chiedi_si()` vuol dire «no».

    C++ (cc1plus, libstdc++)? [si/no]   FreeBASIC? [si/no]   OpenSSL? [si/no]

tutte e tre su una riga, e installava il solo gruppo C. Nessun errore da
nessuna parte: **le scelte erano state fatte da nessuno.**

Ora `console_setfg()` sta nella libc (`lib/libc.c`), documentata in
`libc.h`, e la usano `install` e `login`. Prima ognuno se la scriveva in
assembly inline — quella di login senza dichiarare eax fra i clobber.

! **La regola vale per chiunque lanci un programma interattivo**, non solo
per la shell: `pid = spawn(...)`, `console_setfg(pid)`, `waitpid`,
`console_setfg(getpid())`.

### 3. `/cdrom/bin/gcc` non poteva funzionare, ed era il primo nel PATH

Il difetto che spiega perché gli strumenti «non si installavano a mano».
Sul CD c'era una copia di gcc, g++ e cpp anche in `/cdrom/bin`, dove ci
arrivano perché è da lì che si copiano dentro `/cdrom/exos/bin`. Ma da lì
**non possono funzionare**: il driver calcola il proprio prefisso come
`<dir del binario>/..`, cioè `/cdrom`, dove `libexec/` e `lib/gcc/` non
esistono.

E `/cdrom/bin` veniva prima di tutto nel PATH. Quindi:

    gcc -v -o /pg /pg.c
    COLLECT_GCC=/cdrom/bin/gcc          <- non quello installato
    gcc: fatal error: cannot execute 'cc1'

**su una macchina dove gli strumenti erano installati e funzionanti.** La
prova che lo dimostra, sullo stesso disco e nello stesso istante:

    /exos/bin/gcc -o /pg /pg.c   &&  /pg
    somma dei quadrati 1..10 : 385   (atteso 385)
    lunghezza del nome       : 5     (atteso 5)
    divisione a 64 bit       : 64   (atteso 64)

Stesso sorgente, stesso disco, stesso momento: con il percorso intero
compilava, con `gcc` nudo no. La differenza era solo QUALE gcc rispondeva.

Tre correzioni insieme:

- `make iso` **toglie gcc/g++/cpp da `/cdrom/bin`** dopo averli copiati in
  `/cdrom/exos/bin`. `as` e `ld` restano: quelli funzionano da qualunque
  directory, non avendo un prefisso da ricostruire;
- il PATH predefinito diventa `/bin:/dev:/cdrom/exos/bin:/cdrom/bin`, così
  il `gcc` del CD è raggiungibile e funziona;
- `toolinst` inserisce la propria bin **PRIMA** delle voci `/cdrom`, non in
  coda. In coda, `gcc` avrebbe continuato a risolversi sul CD: si sarebbe
  usato il lettore credendo di usare il disco, e togliendo il CD un sistema
  «installato» avrebbe smesso di compilare.

! **Un binario che non può che fallire, messo dove viene trovato per
primo, è peggio di un binario assente.**

### La prova che chiude il giro

Disco formattato ext2, `install` da floppy, poi, dentro il sistema
installato e avviato **da disco senza floppy**:

    install -tools -y -g base        220 file, 51668 KB
      PATH   = /bin:/dev:/exos/bin:/cdrom/exos/bin:/cdrom/bin
      TMPDIR = /tmp  (voce nuova)

riavvio, e con `gcc` **nudo**, senza percorso e senza opzioni:

    gcc -o /pg /pg.c
    /pg
    La catena intera dentro EX-OS
      somma dei quadrati 1..10 : 385   (atteso 385)
      lunghezza del nome       : 5     (atteso 5)
      divisione a 64 bit       : 64   (atteso 64)
    Compilato, assemblato e collegato qui dentro.

I numeri sono noti in anticipo (`tools/iso/prova-gcc.c`): non «sembra
giusto».

## TMPDIR, che mancava

`toolinst` scriveva solo il PATH. Ora scrive anche `TMPDIR` (e crea la
directory: una TMPDIR che punta al nulla è peggio di nessuna TMPDIR).
Senza, i file di passaggio fra cc1 e as finiscono nella directory
corrente, e da una radice in sola lettura non ci finiscono affatto —
«Cannot create temporary file in ./», che parla di un file temporaneo e
manda a cercare il guasto in GCC.

PATH e TMPDIR si scrivono **in una passata sola**: con due giri il `.bak`
del secondo sarebbe il file già modificato dal primo, e chi tornasse
indietro troverebbe una configurazione a metà che nessuno ha mai deciso.

## Come si rifà la prova

    make && make floppy
    make hd                      # ~6 min: formatta e installa dentro QEMU
    rm -f dist/exos-tools.iso && PATH=$HOME/exos-cross/bin:$PATH make iso

    EXOS_NO_FLOPPY=1 EXOS_RAM=256M \
    EXOS_QEMU_EXTRA="-drive file=dischi/hd.img,format=raw,if=ide \
                     -drive file=dist/exos-tools.iso,media=cdrom,if=ide,index=2" \
    python3 tools/qemu_drive.py "install -tools -y -g base@600" \
                                "gcc -o /pg /pg.c@300" "/pg@15"

! La copia del gruppo C sono 50 MB e ci mette minuti: `cc1` da solo pesa
35 MB. Con `-g base,cpp` si arriva a 127 MB.

## Rimasto aperto

- **Lo spazio libero non lo sa nessuno.** `ext2_blocchi_liberi()` esiste in
  `kernel/fs/ext2.c` e non c'è syscall che lo consegni: `toolinst` dichiara
  quanto copierà e dice esplicitamente che non può controllare se ci sta.
  Una syscall nuova sarebbe additiva — nessun tipo condiviso cambia, quindi
  nessuna ricostruzione del bersaglio.
- **`kbd: READLINE su console N in raw, ripristino cooked`** compare ancora
  a ogni programma interattivo lanciato dalla shell (`mkfs`, `toolinst`):
  la shell tiene la console in raw per il proprio editor di riga e non la
  rimette in cooked prima di lanciare un figlio in primo piano. Stessa
  famiglia del difetto chiuso stamattina in `sh_exit`, e stessa cura:
  `kbd_modo(COOKED)` in `avvia_figlio` prima della `waitpid`.
- **Il C++ e FreeBASIC non sono stati provati installati**: il gruppo `cpp`
  sono altri 77 MB e la prova di oggi si è fermata al C, che è quello da
  cui dipendono gli altri. Il link finale di FreeBASIC resta rotto a monte
  (vedi la nota del 6 agosto): il catalogo lo dichiara con `nota =`.

---

# Dove riprendere — 10 agosto 2026, sera

I tre lavori del mattino sono chiusi e provati dentro QEMU. Qui resta
quello che si è capito facendoli, e quello che è rimasto aperto.

## login FUNZIONA — e non c'era niente da correggere in login

Il giro completo, provato con `tools/qemu_drive.py`:

    primo avvio senza /boot/utenti  -> crea l'utente, password in asterischi
    accesso con le credenziali      -> shell
    `exit` dalla shell              -> torna al prompt d'accesso
    password sbagliata              -> "Accesso non riuscito"
    utente inesistente              -> LO STESSO messaggio

! **La diagnosi scritta stamattina era su un albero vecchio.** Il
sospetto principale — `reader_pid` che ammette un solo lettore per
console — non c'entrava: la voce `login = /bin/login` attivata su questo
albero funziona al primo tentativo. Qualcosa fra i commit del 9-10 agosto
(la console grafica, svga diventato driver) l'ha chiuso per strada.

! **Prima di rileggere il codice, riprovare il caso semplice.** È la
stessa lezione del `-B` di GCC, scritta più sotto nella nota del 6 agosto,
e per la seconda volta ha risparmiato mezza giornata di lettura.

### Quello che si è imparato guardandoci dentro, e che resta vero

**La shell non usa `read(0)` per il proprio prompt.** Usa
`riga_modifica()`, cioè la modalità raw e `KBD_MSG_READKEY`; `sh_read` è
solo il ripiego. Quindi il percorso cooked/`KBD_MSG_READLINE` —
`drv_read` → tty_read_ipc → il driver — **lo esercita quasi solo login**.
È l'unica strada del sistema con un solo utilizzatore: quando qualcosa lì
si rompe, non c'è una seconda prova che lo segnali.

**La shell adesso restituisce la console in cooked quando esce**
(`sh_exit` in `bin/sh/shell.c`). Prima non lo faceva e non si notava,
perché dopo `exit` la console restava morta. Con login davanti si vedeva
eccome: a ogni uscita dalla shell il driver stampava

    kbd: READLINE su console 0 in raw, ripristino cooked

! **Un avviso che scatta nel caso NORMALE smette di segnalare quello
anomalo.** Quella riga esiste per il programma a schermo intero morto
senza rimettere a posto: se compare a ogni `exit` non la guarda più
nessuno. Chi muore per un fault non passa da `sh_exit`, ed è giusto —
quello è il caso per cui la rete del driver serve.

### La voce resta COMMENTATA, di proposito

`login = /bin/login` funziona ma è spenta: accendendola ogni prova
automatica va autenticata, e gli script di `qemu_drive.py` si aspettano
un prompt di shell. Si accende togliendo il cancelletto; il file utenti
se lo crea da solo al primo giro.

## kernel.cfg: da 8116 a 2007 byte

Le spiegazioni sono in **`/boot/kernel.txt`**, che il kernel non legge e
può crescere quanto serve. Il `.cfg` ora ha 6100 byte di margine sul
tetto degli 8191, e `hwconfig` — che il file lo riscrive per intero —
rimanda allo stesso posto.

! **kernel.cfg, kernel.txt e help.txt sono di SOLO ASCII adesso.** Due
motivi, e il secondo è quello che conta:

1. il tetto si misura in byte e un accento UTF-8 ne pesa due, che è la
   trappola in cui si è cascati tre volte in un'ora;
2. **lo schermo di EX-OS è in code page 437.** Un file con accenti veri,
   letto con `cat` sulla macchina, mostra glifi casuali al loro posto — e
   questi sono file fatti apposta per essere letti lì dentro. Si scrive
   `e'`, non `è`.

## `help` è un file di testo a blocchi

    /boot/help.txt      il testo: 57 blocchi
    /bin/help           lo sfoglia

    help              l'indice
    help ls           solo il blocco 'ls'
    help -t           tutto di seguito, per leggerlo dalla seriale

Su e giù, PgSu/PgGiu (e la barra spaziatrice), Inizio/Fine, `q` esce. Il
meccanismo dei tasti è copiato da `bin/gfedit/gf_term.c`.

Un blocco è `[nome altri-nomi]` in prima colonna; la prima riga non vuota
è il sommario che compare nell'indice. **Documentare un comando nuovo non
richiede di ricompilare niente.**

Il builtin `help` della shell è rimasto, ma ora delega: cerca `/bin/help`
nel PATH e, solo se non c'è, stampa un aiuto di riserva di dieci righe
che dice dove sta quello vero. Non è un secondo elenco da tenere
allineato — sarebbe la solita coppia di liste che divergono.

### Tre difetti trovati provando, non leggendo

Nessuno dei tre si vedeva sulla seriale, perché `tty_raw(1)` ne spegne lo
specchio: sono usciti da uno **screendump del monitor di QEMU** convertito
in griglia 80x25. Lo strumento è in
`tools/schermo.py` — vale la pena rifarlo se serve di nuovo.

1. **La barra larga 80 colonne faceva scorrere lo schermo.** Scrivere
   nell'ottantesima colonna dell'ultima riga manda il cursore oltre il
   bordo, il VGA va a capo e scorre di una riga: il titolo spariva a ogni
   ridisegno. Sembrava che non venisse disegnato, e invece veniva
   disegnato e subito buttato fuori. Le barre si fermano a 79.
2. **`ESC[7m` non esiste.** `vga.c` riconosce 0, 1, 30-37, 39, 40-47, 49,
   90-97 e basta: il video inverso passava senza fare niente e le barre
   uscivano identiche al testo. Si usano i colori espliciti
   (`ESC[30;47m`), come fa già gfedit.
3. **Una riga di testo poteva creare un blocco.** L'intestazione era
   «riga che comincia con `[` e ha un `]` da qualche parte», e dentro il
   blocco `export` la riga `[env] di /boot/kernel.cfg - vedi ...`
   diventava un secondo blocco chiamato `env`. Ora il `]` deve chiudere
   la riga. ! **Il testo che si scrive non deve poter cambiare la
   struttura del file.**

## Rimasto lì, da decidere

- **`dist/KERNEL.CFG` è tracciato da git, non lo usa nessuno, ed è
  DIVERSO da `boot/kernel.cfg`.** Nessuna regola del Makefile e nessuno
  script lo nomina: è una copia vecchia rimasta indietro. Due file con lo
  stesso contenuto apparente e valori diversi sono il modo classico di
  perdere un pomeriggio. Va cancellato, ma è un file tracciato e la
  decisione non è mia.
- **Le ISO non sono state rifatte.** Le regole ci sono e sono verificate
  con `make -n iso-exos` (copiano kernel.txt e help.txt), ma
  `make iso`/`make iso-exos` non sono stati eseguiti: servono minuti e
  una corsa interrotta lascia `build/iso*/` a metà senza ripararsi da
  sola — vedi la nota del 6 agosto più sotto.
- **`libctest`: 294 prove superate, 0 fallite**, dopo tutte le modifiche
  di oggi.

## I prossimi passi

1. Il link finale di FreeBASIC (vedi la nota del 6 agosto).
2. `helpconfig` è rimasto dentro la shell perché mostra lo STATO dei
   servizi chiedendolo al registro IPC, non solo delle istruzioni.
   Volendo, il blocco `[helpconfig]` di help.txt e quel comando possono
   convivere così come sono — ma sono due testi sullo stesso argomento in
   due posti.

---

# Dove riprendere — 6 agosto 2026

Appunti su un lavoro a metà. Non è documentazione: è lo stato di ciò che
sta girando adesso e di ciò che si è appena capito.

## Il difetto «cc1 legge zero byte» è CHIUSO, e la causa non era dove si cercava

`cc1` dentro EX-OS apriva il sorgente, lo riconosceva, e produceva
un'unità di traduzione **vuota** con uscita 0. Su ext2 lo diceva anche:

    cc1: warning: /src/prova.c is shorter than expected

Non era il filesystem, non erano le syscall, non era iconv. Era un
**cambio di ABI nella libc, subìto da binari solo ricollegati**.

Il 4 agosto `time_t` è passato da `long` a `long long` — riga giusta, per
non finire nel 2038. Ma `struct stat` contiene tre `time_t` in coda, e da
48 byte è passata a 60. Il giorno dopo il sysroot aveva la libc nuova e
`cc1` era ancora fatto di oggetti del giorno prima, che di `struct stat`
ne conoscevano 48. Quindi:

```c
struct stat st;   /* 48 byte per cc1, 60 per la libc che ci scrive dentro */
size_t limit;     /* calpestato */
off_t  offset;    /* calpestato */
int    fd;        /* CALPESTATO dalla metà bassa di st_mtime  →  0 */
```

`fd` diventava **0**, cioè stdin; e un processo lanciato con `&` che legge
da stdin trova la fine dell'input (è il job control, vedi `sys_read`).
Quindi `read()` rendeva zero byte **senza errore** — il sintomo che non
somigliava per niente alla causa.

! **La misura che ha risolto**: strumentare `read_file_guts()` in
`gcc/libcpp/files.cc` con `write(2, ...)`. Con `fprintf(stderr, ...)` non
si vedeva niente, perché il processo moriva subito dopo e il buffer se ne
andava con lui. Su un programma che si schianta l'unica stampa che vale è
quella già consegnata al kernel.

## La ricostruzione del bersaglio è FATTA, e verificata

    tools/ricostruisci-bersaglio.sh        (log: ~/ricostruzione-bersaglio.log)

Ricostruisce **tutto** il codice di terzi per `i386-exos` nell'ordine in
cui dipende da sé stesso: libc → gmp/mpfr/mpc → libm → libgcc/libstdc++ →
binutils nativi → cc1 → openssl → CD. Sono ore: `cc1` va a `-j1` su 4 GB
di RAM.

Alla fine scrive l'impronta della libc in `$SYSROOT/.abi-libc`.
`tools/ricostruisci-bersaglio.sh --verifica` la confronta con quella
corrente e dice se il bersaglio è disallineato — è il controllo che, se
fosse esistito, avrebbe risparmiato due giorni.

## Come rifare il disco di prova ext2

! `mke2fs` non è installato: formatta EX-OS stesso, come `tools/mkhd.sh`.

    qemu-img create -f raw /tmp/ext2disk.img 512M
    printf 'label: dos\nstart=2048, type=83, bootable\n' | /usr/sbin/sfdisk /tmp/ext2disk.img

poi dentro QEMU, con il CD degli strumenti attaccato:

    mkfs -t ext2 -L src hd0p1     <- CHIEDE CONFERMA: rispondere `si`
    mount hd0p1 /src
    cp /cdrom/prova-cc1.c /src/prova.c

! Senza la risposta a `mkfs`, il comando dopo viene mangiato come
risposta e il montaggio fallisce con un `-2` che sembra tutt'altro.

La riga per QEMU:

    EXOS_RAM=512M EXOS_QEMU_EXTRA="-drive file=/tmp/ext2disk.img,format=raw,if=ide,index=0 -drive file=dist/exos-tools.iso,media=cdrom,if=ide,index=2" python3 tools/qemu_drive.py "mount hd0p1 /src@5" "/cdrom/bin/cc1 /src/prova.c -O2 -o /src/prova.s &@240" "ls /src@8"

! **Lanciare cc1 con `&`**, non in primo piano: in primo piano il
messaggio d'errore e il codice di uscita non si vedono, e una morte sembra
una lentezza.

## FreeBASIC gira su EX-OS (5 agosto, sera)

`fbc` 1.07.3 compila dentro EX-OS, `as` assembla, `ld` collega, il
programma parte e stampa i numeri giusti. La strada, i limiti e il passo
che manca stanno in `tools/freebasic-exos/leggimi.md`.

! **Il link finale si fa a mano**: `fbc` crede di produrre per Linux (il
bootstrap è `-target linux-x86`) e affida il link a `gcc` con opzioni
Linux. Il comando che funziona è nel leggimi.

Tre cose entrate nella libc per questo, che **non riguardano solo
FreeBASIC**:

- `system()`, `popen()`, `pclose()` — prima erano `ENOSYS`. Ora passano da
  `/bin/sh -c`, che è potuto nascere perché la shell adesso vede i propri
  argomenti (`bin/sh/start.S`).
- `spawn()` cerca nel **PATH** un nome senza barre, come `execvp`. Serviva
  a `fbc` per trovare `as`; serve allo stesso modo al driver di GCC e a
  `make`.
- `<wchar.h>` e `<wctype.h>` veri: 26 funzioni, codifica Latin-1
  dichiarata (non UTF-32 finto).

## La catena in C con cc1 a mano è chiusa, e provata

`cc1 /src/prova.c -O2 -o /src/prova.s` dentro EX-OS produce **1113 byte**,
gli stessi che produce il `cc1` del cross sullo stesso sorgente con lo
stesso nome di file. Poi `as` → `ld` → esecuzione, **uscita 70**, che è il
valore calcolato a mano (`385 & 0x7F` più `'E'`).

## Gli header: FATTI (6 agosto, notte)

    cd /src
    /cdrom/exos/bin/gcc -B/cdrom/exos/libexec/gcc/i386-exos/17.0.0/ \
        -O2 -c inc.c
    -> inc.o, 872 byte, uscita 0

`inc.c` ha `#include <stdio.h>` e `<string.h>`, e il driver li trova DA
SOLO — nessun `-I`. Ci sono voluti quattro difetti del sistema, non di GCC:
il racconto sta piu' avanti, in «Perche' non funzionava».

! Allora il `-B` serviva, su **libexec**. Dal 6 agosto non piu': vedi
«Il -B non serve piu'» piu' sopra.

## IL `-B` NON SERVE PIU' (6 agosto, sera)

    cd /src
    /cdrom/exos/bin/gcc -O2 -o nb pg.c      <- nessuna opzione di percorso
    /src/nb
    somma dei quadrati 1..10 : 385   (atteso 385)
    ...
    -> nb, 63324 byte: IDENTICO al pg prodotto col -B

Il `-B` era la stampella per una rilocazione che non funzionava. Il driver
ricalcola il proprio prefisso da dove sta lui e lo passa ai figli in
`GCC_EXEC_PREFIX` — ma quell'ambiente non arrivava, perche' `pex-exos.c`
girava a spawn_ex l'env NULL di pex_run() come «nessun ambiente» invece che
come «eredita».

! **Non e' stato tolto: e' sparito.** Le cinque correzioni servivano tutte
allo stesso scopo senza che si vedesse — l'ambiente, il `..` nei percorsi,
st_ino che distingue le directory, il -1 al posto di -errno, gli argomenti
non troncati — e il `-B` ha smesso di servire da solo. Era il sintomo, non
la malattia.

! **Il piano per provarlo era un altro, e piu' costoso**: installare
l'albero /exos (140 MB) sul disco rigido, per far combaciare i percorsi
assoluti. Sarebbe stato inutile: bastava riprovare senza `-B` dopo le
correzioni. **Prima di costruire un banco di prova, riprovare il caso
semplice** — il difetto potrebbe essere gia' chiuso da un'altra parte.

## LA CATENA INTERA FUNZIONA — un comando solo (6 agosto)

    cd /src
    /cdrom/exos/bin/gcc -B/cdrom/exos/libexec/gcc/i386-exos/17.0.0/ \
        -O2 -o pg pg.c
    -> pg, 63324 byte, uscita 0

    /src/pg
    La catena intera dentro EX-OS
      somma dei quadrati 1..10 : 385   (atteso 385)
      lunghezza del nome       : 5     (atteso 5)
      divisione a 64 bit       : 64   (atteso 64)
    Compilato, assemblato e collegato qui dentro.

`pg.c` e' `tools/iso/prova-gcc.c`: `#include <stdio.h>` e `<string.h>`,
`printf` con `%lld`, `memcpy` dalla libc, una divisione a 64 bit che chiama
`__divdi3` in libgcc, e una struttura restituita per valore. **I numeri
sono noti in anticipo** — non «sembra giusto».

Il driver ha lanciato da solo cc1, `as`, `collect2` e `ld`, ha trovato gli
header senza un `-I`, e ha collegato con crt0, libc e libgcc. Ci sono
voluti cinque difetti del sistema (nessuno in GCC), raccontati qui sotto.

## Il page fault della catena a piu' stadi: CHIUSO — era strncpy

Il sintomo:

    /cdrom/exos/bin/gcc -B/cdrom/exos/libexec/gcc/i386-exos/17.0.0/ \
        -O2 -o pg pg.c
    [FAULT] PID 12 '/cdrom/exos/libexec/gcc/i386-ex': page fault a
            0x0813b000 (pagina assente, scrittura, EIP=0x080aeef0)

**La causa era in `strncpy` della nostra libc**, e non c'entrava niente
con l'allocatore, con sbrk o con i percorsi:

    while (n-- && (*d++ = *src++));
    while (n--) *d++ = '\0';

Con una sorgente piu' corta di n il conto torna. Con una sorgente lunga
almeno n — cioe' **il caso per cui strncpy esiste** — il primo ciclo esce
perche' `n--` VALE 0, ma il post-decremento scatta lo stesso: n e' un
size_t e passa a SIZE_MAX. Il secondo ciclo si mette a scrivere zeri per
quattro miliardi di byte e si ferma solo sulla prima pagina non mappata.

! **La gemella larga `wcsncpy` era gia' scritta bene** (`while (n && ...)`
con il decremento dentro il corpo). Quando due funzioni fanno la stessa
cosa su tipi diversi, la differenza fra le due e' il primo posto dove
guardare.

Provato prima sull'ospite con ASan (`stack-buffer-overflow` sulla versione
vecchia, pulito sulla nuova), poi in `libctest` con quattro prove nuove —
compresa una **sentinella dopo il buffer**, senza la quale un riempimento
che sfora di poco resterebbe verde.

### Come si e' arrivati: la strumentazione, non il ragionamento

! Due sospetti scritti qui ieri erano **entrambi sbagliati**
(`heap_restituisci`, `sys_sbrk` negativo), e leggere il codice non li
smontava: sulla carta erano a posto. A chiudere la partita in una corsa
sola sono stati due klog aggiunti al kernel, che adesso restano:

1. **La mappa del processo sotto il page fault** (`kernel/mm/paging.c`):
   heap, stack e VMA, con `<-- QUI` sulla VMA che contiene l'indirizzo.
   Il nome del processo sta in PROCESS_NAME_LEN byte e i percorsi lunghi
   ci si troncano dentro — `/cdrom/exos/libexec/gcc/i386-ex` puo' essere
   cc1 come collect2 — mentre le VMA si confrontano con `readelf -l` e non
   lasciano dubbi: era collect2.
2. **`sbrk` che restituisce pagine** lo dice a LOG_INFO
   (`kernel/syscall/syscall_impl.c`): e' raro e smappa memoria sotto i
   piedi di un processo vivo.

Poi il colpo decisivo, che non costa niente e va ricordato:

    i386-exos-nm -n <binario> | awk 'strtonum("0x"$1) <= 0x080aeef0' | tail -3
    -> 080aeea0 T strncpy

**L'EIP del fault, cercato nella tavola dei simboli.** Dice in quale
funzione si e' rotto, che e' la domanda a cui tutto il resto girava
intorno.

## IL C++ GIRA DENTRO EX-OS (6 agosto)

    cd /src
    /cdrom/exos/bin/g++ -B/cdrom/exos/libexec/gcc/i386-exos/17.0.0/ \
        -O2 -o pp pp.cpp
    -> pp, 1 266 568 byte, uscita 0

    /src/pp
    La libreria standard del C++ dentro EX-OS

      vector+sort : 1 3 5 7 9
      string      : "std::string concatenata" (23 caratteri)
      cerchio     : area = 12566 (x1000)
      quadrato    : area = 9000 (x1000)
      eccezione   : lanciata e ripresa
      out_of_range : presa da dentro la libreria

    La libreria standard risponde.

`pp.cpp` e' `tools/iso/prova-cpp.cpp`. Non e' un «hello world in C++»:
contenitori con `<algorithm>`, `std::string` (cioe' `operator new` sopra la
nostra malloc), polimorfismo con distruttore virtuale, e **le eccezioni** —
compresa una lanciata da dentro libstdc++ e ripresa attraverso piu' livelli
di stack, che e' il pezzo che ha bisogno del maggior numero di cose
funzionanti insieme.

! Resta sul disco un `ccHm016b.s` da 4096 byte: il driver **non cancella
il proprio file temporaneo**. Da guardare — non fa danno subito, ma una
directory di lavoro che si riempie a ogni compilazione lo fara'.

### I due pezzi che mancavano sul CD

`g++` non c'era proprio: il Makefile copiava `cpp` e `xgcc`, non `xg++`.

! **Il nome del driver non e' un'etichetta**: lo stesso binario decide da
COME E' STATO CHIAMATO se compilare in C o in C++, quale cc1 lanciare e se
collegare libstdc++. Con il solo `gcc` sul CD, cc1plus c'era e non
esisteva un modo di arrivarci.

Messo `g++`, la catena e' arrivata fino in fondo e si e' fermata al **link**:

    ld: cannot find -lstdc++
    ld: have you installed the static version of the stdc++ library ?

Gli header C++ erano sul CD, la LIBRERIA no. E' l'errore piu' tardi che
potesse uscire — dopo che cc1plus ha compilato e as ha assemblato — e dice
una cosa sola: `libstdc++.a` (24 MB) va in `/exos/lib`, accanto a libc.a e
libgcc.a, dove ld guarda gia'.

! **E anche `xg++` andava ricollegato**: quando ho forzato il rilink dopo
la correzione di `strncpy` avevo cancellato `cc1 cc1plus collect2 xgcc cpp`
e NON `xg++`, che e' rimasto indietro di una versione. La prova con il
driver vecchio sarebbe stata inconcludente comunque. Il controllo che lo
smaschera e' la data del binario contro quella di `$SYSROOT/lib/libc.a` —
non quella della copia sul CD, che `make iso` rifa' ogni volta.

## Una prova che misurava la macchina, non il sistema (6 agosto)

`libctest` falliva su «sbrk finisce per rifiutare»... **a 512 MB**, e
passava a 64. Cresceva di 1 MB per 64 volte e pretendeva un rifiuto: su una
macchina piccola a fallire era la RAM, su una grande i 64 MB ci stavano e la
prova diventava rossa su un sistema perfettamente sano.

E non poteva funzionare: il tetto vero e' `heap_max`, a quasi 3 GB da
heap_start. A 1 MB per volta non lo si raggiunge nemmeno in 64 passi.

Ora il tetto si prova con **una richiesta piu' grande dello spazio di
indirizzamento** (`sbrk(0x7FFF0000)`), che dev'essere rifiutata su
qualunque macchina. La crescita a blocchi resta come **misura stampata**,
non come verdetto:

    64 MB  -> (cresciuto di 60 MB, poi la RAM e' finita)     294/0
    512 MB -> (cresciuto di 64 MB, senza esaurire la RAM)    294/0

! **Una prova che dipende dall'ambiente e' peggio di una prova assente**:
la sua riga rossa fa cercare un difetto che non c'e'. Se una prova ha
bisogno di una macchina particolare, deve dirlo o non essere una prova.

## Tutti i binari del bersaglio sono allineati (6 agosto)

Dopo la correzione di `strncpy` sono stati ricollegati **tutti**, e il
controllo e' la data contro `$SYSROOT/lib/libc.a`:

    cc1  cc1plus  collect2  xgcc  xg++  cpp      (gcc-build-cpp, rilink forzato)
    as-new  ld-new                                (exos-native/build-nativi)
    fbc                                           (prepara-fb.sh, ricostruito)

! **Non guardare la data della copia sul CD**: `make iso` la rifa' a ogni
giro e risulta sempre fresca, anche quando il binario sorgente e' vecchio
di giorni. E' cosi' che `xg++` e' passato inosservato.

## ! `make iso` interrotto lascia l'albero a meta', e non si ripara da solo

Una corsa di `make iso` uccisa a meta' lascia `build/iso/` monco — comincia
con `rm -rf $(ISO_ROOT)` e ricostruisce. Il guaio e' il giro dopo:

    make iso
    make: Nessuna operazione da eseguire per «iso».

perche' `dist/exos-tools.iso` risulta piu' recente dei suoi prerequisiti.
L'ISO in `dist/` e' giusta e l'albero in `build/` no, e make non se ne
accorge. Siccome `build/` e' tracciato da git, la cosa si vede come una
frana di file cancellati.

    rm -f dist/exos-tools.iso && make iso    <- l'unico modo di riallinearli

Da tenere presente ogni volta che una prova viene interrotta.

## FreeBASIC ha lib e header come il C, e ora ce li ha davvero (6 agosto)

Mancava un pezzo intero: **i .bi di FreeBASIC non stavano sul CD**. C'erano
fbc e libfb.a, quindi un `.bas` senza `#include` compilava — ed e' proprio
cio' che `prova-fb.bas` prova, di proposito. Con un `#include` non si
andava da nessuna parte, e nessuno se n'era accorto perche' nessuna prova
lo chiedeva.

### FreeBASIC vuole il layout Unix, e lo dice il suo makefile

Prevede due disposizioni. La nostra fbc usa la **non-standalone**:

    <prefisso>/bin/fbc
    <prefisso>/include/freebasic/        i .bi
    <prefisso>/lib/freebasic/<target>/   libfb.a, fbrt0.o

Non e' dedotto dai sorgenti, si legge dal comportamento: con fbc in /bin,
`fbc -v` stampava `assembling: /cdrom/bin/../bin/as`. **Il prefisso e' la
directory dell'eseguibile meno `bin`**, ricalcolata ogni volta.

! **Ed e' la differenza vera con GCC**: i percorsi di GCC sono COMPILATI
DENTRO (`--prefix=/exos`, dodici stringhe dentro cc1) e per spostarli
servono GCC_EXEC_PREFIX o `-B`. FreeBASIC e' rilocabile per costruzione.
Percio' fbc e' passato da `/bin` a **`/exos/bin`**: da li' trova i propri
inc e lib da solo, ovunque sia montato il CD.

### Dei 31 MB di inc/ se ne copiano 452 KB

Il resto sono binding ad allegro, GTK, SDL, X11, zlib — librerie che su
EX-OS non esistono. ! **Peggio di un header assente e' un header che
promette**: compilerebbe e il link fallirebbe con simboli mai visti. Si
copia cio' che corrisponde a cio' che c'e': `crt.bi` e `crt/` (che mappano
sulla nostra libc), `fb*.bi` e `fbc-int/` (la runtime).

### Provato: la ricerca degli header FUNZIONA

`tools/iso/prova-fb2.bas` e' il gemello di `prova-fb.bas` CON `#include
"crt.bi"`, sulla falsariga della coppia prova-cc1.c / prova-gcc.c. Dentro
EX-OS:

    /cdrom/exos/bin/fbc -v f2.bas
    compiling:    f2.bas -o f2.asm (main module)     <- l'include e' stato TROVATO
    assembling:   as --32 --strip-local-absolute "f2.asm" -o "f2.o"

    f2.bas   2945      il sorgente con #include "crt.bi"
    f2.asm   4010      fbc ha compilato
    f2.o     2516      as ha assemblato
    [1] terminato: fbc -v f2.bas (codice 1)     <- cade al link

Nessuna opzione, nessun `-i`: fbc ha risolto
`/cdrom/exos/include/freebasic/crt.bi` da solo. Il sorgente e' verificato
prima su Linux (uscita 0, valori attesi 5/385/36), cosi' un errore di
sintassi non si confonde con un difetto di EX-OS.

! **Il link resta quello di sempre**: fbc crede di produrre per Linux e
affida il link a `gcc` con opzioni Linux — vedi
`tools/freebasic-exos/leggimi.md`. Non c'entra con la disposizione delle
directory: e' il passo che mancava gia' prima.

### `as` e `ld` stanno in due posti, e adesso di proposito

fbc cerca `as` in `<prefisso>/bin/as` — cioe' `/exos/bin/as` — mentre GCC
lo vuole in `/exos/i386-exos/bin/`, che e' il `gcc_tooldir` del suo
configure. Con la sola copia sotto i386-exos, fbc non lo trovava, ripiegava
sul nome nudo e lo faceva cercare al PATH.

! **Funzionava, ma per coincidenza** — e una coincidenza smette di
funzionare il giorno che il PATH cambia, senza che nessuno colleghi le due
cose. Ora `make iso` li copia in entrambi i posti: 2,9 MB in piu' sul CD
per togliere di mezzo un ripiego silenzioso.

### E il prefisso? `/usr` o `/exos`

La struttura che si voleva c'e' gia': `/exos/bin`, `/exos/lib`,
`/exos/include` **sono** la disposizione Unix, con un prefisso diverso dal
solito. Rinominare `/exos` in `/usr` e' una decisione separata e non
gratuita: GCC ha il prefisso compilato dentro, quindi vuole configure e
ricostruzione da capo — ore — mentre FreeBASIC seguirebbe da solo. Il
guadagno e' la familiarita', il costo e' quello. Non e' stato fatto.

! E se si fa: e' `include/`, non `inc/`. `inc/` e' la disposizione
standalone di FreeBASIC, che non e' quella che usiamo.

## ! Il progetto sta dentro ~/MEGA/, e MEGAsync ci mette le mani

Durante il lavoro del 6 agosto i file sotto `build/iso/` sono spariti da
sotto le mani piu' volte: 869 header di libstdc++ riscritti a ogni
`make iso` sono esattamente cio' su cui un client di sincronizzazione si
accanisce. Sommato al fatto che un `make iso` interrotto non si ripara da
solo, ha bruciato diverse corse di prova.

Il dubbio serio non e' pero' che CANCELLI: e' che **RIPRISTINI**. Un
binario vecchio risorto dentro un'immagine gia' dichiarata buona renderebbe
falsa una prova passata, e nel modo peggiore — silenziosamente.

**La verifica, fatta a MEGAsync spento** (e da rifare cosi' ogni volta che
il dubbio si ripresenta):

1. **I build dir stanno FUORI da ~/MEGA** (`~/gcc-build-cpp`,
   `~/exos-native`, `~/fb-build-exos`, `~/exos-cross`): i binari veri non
   sono mai stati a rischio. Il rischio era confinato al repository.
2. **I sorgenti**: ogni correzione e' ancora al suo posto (strncpy,
   err_posix, path_normalizza, MAX_SPAWN_ARGS, VFS_IDENT, l'env di
   pex-exos, la mappa nel page fault, le prove nuove).
3. **I binari spediti**: ! sul CD sono SPOGLIATI DEI SIMBOLI, quindi
   cercarci dentro `strncpy` per nome non funziona. Si strippa il binario
   sorgente allo stesso modo e si confronta byte per byte:

       i386-exos-strip -o /tmp/x ~/gcc-build-cpp/gcc/cc1
       cmp /tmp/x build/iso/exos/libexec/.../cc1

   cc1, cc1plus, collect2, gcc, g++, as, ld, fbc: tutti identici.
4. **L'ISO contro l'albero**, letta a mano dal descrittore Joliet e
   confrontata con sha256: 13 file campione, tutti uguali.
   ! I nomi nell'ISO portano il suffisso di versione `;1`: un confronto
   per nome esatto non trova NIENTE e sembra un CD vuoto.
5. **Il floppy**: KERNEL.BIN identico a `build/kernel.bin`, e i 14 file di
   `/BIN` identici a `build/bin/`.
6. **Le due prove decisive rifatte**: libctest 294/0, e la catena C con un
   `delete pg` davanti — cosi' l'eseguibile e' ricostruito da zero e non
   riletto.

**Nessun risultato era falso.** Ma il controllo va rifatto, non dato per
scontato, ogni volta che si lavora con la sincronizzazione accesa.

## Il file temporaneo di g++: NON era un difetto (6 agosto)

Dopo `g++ -o pp pp.cpp` restava `ccHm016b.s` in `/src`, e sembrava che il
driver non cancellasse i propri temporanei. **Non e' cosi'**: ripulita la
directory dai `cc*.s` e rifatta una compilazione RIUSCITA, non resta
niente. Il driver pulisce.

! **Il residuo veniva dalla corsa FALLITA** — quella morta su «cannot find
-lstdc++» perche' libstdc++.a non era ancora sul CD. Era rimasto li' e la
compilazione successiva, che creava un temporaneo con un nome diverso, lo
lasciava in vista: sembrava suo.

! **La lezione e' sul metodo, non su GCC**: la prova iniziale guardava una
directory che conteneva gia' i rifiuti di un tentativo precedente. Una
prova che non parte da uno stato noto misura anche il passato. Il `delete
cc*.s` prima del comando, e un `ls` a confermare, sono costati due righe.

Resta da guardare, se un giorno da' fastidio: sulla via del FALLIMENTO il
temporaneo sopravvive, e secondo gcc.cc non dovrebbe — `record_temp_file`
lo mette in entrambe le code, `always_delete_queue` e `failure_delete_queue`.

## Il prossimo passo (6 agosto)

1. **Il link finale di FreeBASIC.** `fbc` compila e assembla da solo
   (provato: f2.asm, f2.o), ma crede di produrre per Linux e passa opzioni
   Linux a `gcc`. E' l'ultimo terzo che manca perche' anche il BASIC sia
   una catena chiusa come C e C++.
2. Poi i linguaggi oltre il C. **Ada** vuole `gnat1` e un `libgnat` — e
   vuole un compilatore Ada già funzionante per costruirsi, che è il vero
   ostacolo.

*(Chiusi il 6 agosto, erano in questo elenco: `fbc` ricollegato, `fstat()`
convertita a -1, il file temporaneo di g++ — che non era un difetto —
`as`/`ld` messi dove entrambi i compilatori li cercano, e il `-B`, sparito
da solo.)*

## Rimasto indietro di proposito

- `libssl` non si costruisce: `ssl/rio/` di OpenSSL 4.x vuole `fd_set` e
  il polling su socket. Serve un BIO sopra lo stack IPC — lavoro vero, non
  uno stub.
- `RAND_bytes` rifiuta su macchina appena accesa: il serbatoio è 32 byte e
  una sola chiamata lo svuota. La via giusta è ChaCha20 in kernel per
  espandere il seme (vettori di prova in RFC 8439), non allargare il
  serbatoio.

## Grafica VESA — analizzata, non iniziata (5 agosto 2026)

Richiesta messa da parte dall'utente, che vuole studiarla. Qui resta il
ragionamento, per non rifarlo.

**Si può fare, e non serve un driver per scheda.** Da VBE 2.0 in poi S3
Trio, Mystique, i740 e successive espongono tutte un *linear framebuffer*
dalla stessa interfaccia; gli emulatori la implementano tutti. Un driver
solo.

Il lavoro si divide in tre, e **uno solo tocca il kernel**:

1. **`stage2`**, in modo reale: `INT 10h` si può chiamare solo lì. Sceglie
   il modo e lascia indirizzo e geometria in una struttura. Non è né
   kernel né userspace — è codice di avvio, che il BIOS lo chiama già.
2. **Kernel, ~50 righe**: portare avanti quella struttura, e una syscall
   che **mappi il framebuffer** nello spazio di un processo. Il precedente
   giusto è `SYS_DMA_ALLOC`. Nessun font, nessun disegno.
3. **`/dev/vesa.drv`, ring3**: tutto il resto. Non gli servono nemmeno le
   porte I/O — dopo il mode set basta il framebuffer mappato.

! **Il limite da accettare in partenza**: la risoluzione si sceglie
all'avvio. Cambiarla a runtime vuole un monitor v8086 o un emulatore di
modo reale nel kernel.

! **La decisione da prendere PRIMA di scrivere codice**: oggi `klog` e i
panici li stampa il kernel con `vga_putchar_su`. Se la console diventa un
servizio userspace, un panico — o la morte del driver stesso — non ha più
dove scrivere, e il sintomo è **uno schermo fermo senza spiegazione**. Le
due uscite oneste: un disegnatore di testo minimo nel kernel per i soli
klog e panici, oppure la console di emergenza che resta in modo testo.

Il lavoro grosso non è il mode set — sono venti righe — è che il testo in
modo grafico va disegnato: font, blit, scorrimento, cursore. Tutto ciò che
oggi passa da `vga_putchar_su`.

1024x768x32 sono 3 MB di framebuffer da mappare (1,5 MB a 16 bit), su un
sistema che punta a girare in 32 MB.

## I percorsi /exos — chiusi (6 agosto, notte)

Il CD ora ha l'albero che il driver cerca davvero. Non e' una scelta
nostra: si legge dal binario, `strings gcc/cc1 | grep /exos`.

    /exos/libexec/gcc/i386-exos/17.0.0/   cc1, cc1plus, collect2
    /exos/lib/gcc/i386-exos/17.0.0/       libgcc.a, crt*.o, include/
    /exos/i386-exos/include               header di sistema del bersaglio
    /exos/include                         la libc
    /exos/include/c++/17.0.0[/i386-exos]  libstdc++
    /exos/i386-exos/bin/                  as, ld

! cc1 e cc1plus NON stanno piu' in /bin: tenerli in due posti raddoppiava
il CD (190 MB invece di 118). Le prove dirette vanno fatte col percorso
lungo, /cdrom/exos/libexec/gcc/i386-exos/17.0.0/cc1.

**Tre cose imparate provando, in ordine:**

1. `gcc` lanciato da `/` muore con «Cannot create temporary file in ./».
   Gli serve una directory scrivibile: `cd /src` prima, oppure TMPDIR.
2. Poi: «cannot execute 'cc1': spawn: operazione non permessa». Il
   permesso non c'entra — era un DIFETTO in tools/binutils-exos/pex-exos.c,
   ora corretto: leggeva l'errore da `-pid` invece che da errno.
   ! Questa nota diceva anche «spawn_ex ritorna -1 e mette errno»: era
   FALSO quando e' stata scritta — ritornava -errno — ed e' diventato vero
   solo il 6 agosto, con il terzo difetto qui sotto. Il messaggio EPERM
   nasceva proprio da quel malinteso.
   ! La correzione ha effetto solo dopo aver ricollegato GCC (libiberty).
3. Il `-B` puntava alla directory sbagliata: cc1 sta sotto **libexec**, non
   sotto lib/gcc. La prossima prova va fatta con entrambi:

       cd /src
       gcc -B/cdrom/exos/libexec/gcc/i386-exos/17.0.0/ \
           -B/cdrom/exos/lib/gcc/i386-exos/17.0.0/ -O2 -o inc inc.c

! I percorsi compilati dentro sono ASSOLUTI (/exos/...): combaciano quando
il CD e' la radice o quando l'albero viene installato sul disco. Montato su
/cdrom serve il -B, e questa e' la ragione per cui esiste.

### Perche' non funzionava: QUATTRO difetti, uno dietro l'altro

! **Nessuno dei due sospetti scritti qui sopra era quello giusto**, e la
pista «e' `-o`, e' `make_temp_file`» era sbagliata: `gcc -c` senza `-o`
falliva identico. Quello che c'era sotto erano quattro difetti in fila,
ognuno nascosto dal precedente, e ognuno con un messaggio che accusava
qualcun altro.

**1. Il kernel troncava gli argomenti a 16, in silenzio.**
`MAX_SPAWN_ARGS` valeva 16. Il driver lancia cc1 con DICIASSETTE argomenti
(diciotto con `-v`) e il taglio cadeva esattamente fra `-o` e il nome del
file:

    cc1 -quiet -v -iprefix <p> inc.c -fno-pic -fno-asynchronous-unwind-tables
        -quiet -dumpbase inc.c -dumpbase-ext .c -mtune=i386 -march=i486
        -O2 -version -o ./ccXXXXXX.s
                        ^ il sedicesimo

Da cui «missing filename after '-o'», che manda a cercare il difetto nella
generazione dei nomi temporanei — dove non c'era.
Ora il tetto e' 64, le stringhe stanno in un'arena sullo heap invece che
sullo stack del kernel, e **superarlo e' un errore (E2BIG) con un klog**:
un comando accorciato di nascosto e' il difetto peggiore che quella
syscall possa produrre. Stesso tetto anche in `bin/sh` (era 16 pure li').

**2. Il kernel non risolveva `..` dentro un percorso assoluto.**
`resolve_path` copiava un percorso assoluto «così com'è». I percorsi di
ricerca di GCC hanno il `..` NEL MEZZO per costruzione —
`/exos/lib/gcc/i386-exos/17.0.0/../../../../i386-exos/include` — perche' e'
cosi' che un compilatore ritrova le proprie cose dopo essere stato spostato.
Il VFS cercava una directory chiamata davvero `..`, non la trovava, cc1
scartava in silenzio ogni directory e rispondeva «no include path in which
to search for stdio.h».
Ora c'e' `path_normalizza()`: `.`, `..` e le barre doppie si risolvono
ovunque, in assoluti e relativi. Diciannove casi provati su Linux con ASan
prima di metterla nel kernel.

**3. La libc rendeva `-errno` invece di `-1`.**
`open()` su un file assente rendeva `-2`. Il codice di terzi non scrive
`< 0`, scrive `!= -1`, perche' e' cio' che lo standard promette:

    file->fd = open (file->path, O_RDONLY, 0666);   /* libcpp/files.cc */
    if (file->fd != -1)
        { fstat (file->fd, &file->st); ... }

cc1 proseguiva con «descrittore» -2, faceva `fstat(-2)` — EBADF — e moriva
con «fatal error: stdio.h: descrittore non valido» su un header che
semplicemente stava nella directory successiva.
Ora le funzioni con un **nome POSIX** rendono -1 e parlano per errno
(`err_posix` in lib/libc.c); quelle **nostre** tengono il -errno, dove
nessuno arriva con un'aspettativa da standard. Aggiornati `cp`, `trunc` e
`install`, che stampavano il numero.

**4. `stat()` dava a ogni file la stessa identita'.**
`st_first_clus` era 0 sempre, «per non inventare un numero», e la libc ci
costruiva sopra `st_ino`. Ma `remove_duplicates()` di GCC confronta
st_dev/st_ino per togliere le directory ripetute dalla lista degli include:
con st_ino tutti uguali ha concluso che le sue sei directory erano una
sola.

    ignoring duplicate directory ".../i386-exos/include"
    #include <...> search starts here:
     /cdrom/exos/lib/gcc/i386-exos/17.0.0/include
    End of search list.

Ne ha tenuta una — quella degli header del compilatore — e buttata proprio
quella con la libc.
Ora il campo si chiama `st_ident` (stessa parola, stessa posizione: nessuna
ABI cambia) e il VFS lo compone come montaggio nei 4 bit alti +
inode/extent/cluster negli altri 28. Vedi `stat_interno()` in
kernel/fs/vfs.c.

! **E un quinto, corretto per strada**: `pex-exos.c` girava a spawn_ex
l'`env` NULL che `pex_run()` passa sempre. Su Unix quel NULL vuol dire
«eredita» (execv invece di execve); qui voleva dire «ambiente vuoto». GCC
parla ai propri stadi PROPRIO per variabili d'ambiente — GCC_EXEC_PREFIX,
COMPILER_PATH, LIBRARY_PATH, TMPDIR — quindi senza quelle non poteva
funzionare niente di quello che viene dopo cc1.

### Cosa ha richiesto, e cosa NO

Nessuno di questi cambia un tipo condiviso: l'impronta ABI e' rimasta
identica e i binari del bersaglio si **RICOLLEGANO**, non si ricostruiscono.

    tools/ricostruisci-bersaglio.sh libc     # ~1 minuto
    make -C ~/gcc-build-cpp -j2 all-gcc      # ~5 minuti: rilink di cc1
    make && make floppy && make iso

! `make iso` vuole il cross nel PATH:
`PATH=$HOME/exos-cross/bin:$PATH make iso`.

### La prova che le correzioni non hanno rotto niente

    libctest      ->  290 prove superate, 0 fallite

! Due erano **gia'** rosse prima di stanotte, e non per un difetto del
sistema: `system()` ha imparato a passare da `/bin/sh -c` il 5 agosto, e le
prove continuavano a pretendere ENOSYS. Ora dicono la verita'. Una suite
che fallisce su un sistema che funziona smette di essere creduta, ed e' il
modo piu' rapido di perdere una rete di sicurezza.

! Per lanciarla mentre gira gia' un'altra macchina serve
`EXOS_ISTANZA=<n>`: senza, le due condividono `/tmp/exos/serial.txt` e la
seconda cancella l'output della prima da sotto a QEMU — che continua a
scrivere su un file scollegato, e il risultato e' un file vuoto senza
nessun errore.

### La libc adesso sta in DUE posti sul CD

`/exos/include` c'era gia'. Adesso c'e' anche `/exos/i386-exos/include`,
che e' **TOOL_INCLUDE_DIR** — «un altro posto dove potrebbero stare gli
header del sistema bersaglio», gcc/cppdefault.cc — ed e' l'unico dei due
che la rilocazione con `-iprefix` sappia raggiungere. Finche' e' rimasta
una directory vuota, `gcc -c` rispondeva «no include path» con gli header
a due passi. Sono 26 file di testo.


# DOVE RIPRENDERE — 26 agosto 2026

## 26 agosto 2026 — il disco nuovo in un percorso solo

`install -prepara hd0`: mostra il disco e cio' che c'e' sopra, chiede quanto
dare al sistema, quanto allo scambio e se farne una terza per i dati, mostra il
piano, e dopo un «si» scrive la tabella, formatta in ext2 e prepara l'area di
scambio. Poi `install /disco` mette la riga `swap` in kernel.cfg da solo.

! **E' UN COMANDO A PARTE, NON UN COMPORTAMENTO DI `install`.** In testa a
install.c c'e' scritto da sempre che «non partiziona e non formatta: sono
operazioni distruttive e vanno fatte di proposito, non come effetto
collaterale». Resta vero — `install /disco` non e' cambiato di una riga.

Quello che mancava non era la distruzione automatica: era il **percorso**. Le
tre cose si potevano gia' fare con `fdisk`, `mkfs` e `mkswap`, e chi installava
doveva ricordarsi tutti e tre i comandi, il loro ordine, e che lo scambio va poi
dichiarato in kernel.cfg.

! **E NON RIFA' NESSUNO DEI TRE.** La tabella la scrive il kernel (`partwrite`,
l'unica porta al partizionamento: valida tutto o rifiuta tutto e rilegge i
dispositivi da se'), il filesystem lo fa `mkfs`, la firma la mette `mkswap`.
Qui c'e' solo l'ordine e le domande da fare prima. E' la stessa scelta della
finestra Impostazioni di ExWin, che chiama `/dev/svga.drv` invece di imitarlo.

### La riga `swap` in kernel.cfg se la trova da sola

`install` cerca l'area di scambio **come la cerca il kernel: dalla firma dentro
la partizione**, non dal byte di tipo nella tabella. Sono due sorgenti diverse
per la stessa domanda, e fidandosi del tipo si proporrebbe come area una
partizione che il kernel poi rifiuta — cioe' una riga che promette memoria
virtuale e non la accende.

Vale la regola di `CFG_NECESSARIE`: voce assente si aggiunge, voce presente si
lascia, perche' quella e' una decisione di chi usa il sistema.

### Provato dall'inizio alla fine, su un disco vergine da 128 MB

    install -prepara hd0   ->  hd0p1  64 MB ext2 (avviabile)
                               hd0p2  32 MB scambio
                               hd0p3  31 MB ext2

Letto dall'host a macchina spenta: tipi 0x83/0x82/0x83, inizi 2048 / 133120 /
198656 (tutti allineati a 1 MiB), e `EXOSSWAP` nel primo settore della seconda.
Poi `install -m /disco` ha scritto `+ [kernel] swap = hd0p2`, e il sistema
avviato **dal proprio disco** dice:

    SWAP: 'hd0p2' attiva: 8191 slot da 4 KB (31 MB), elenco di 1024 byte

### Due cose che sono cambiate per far posto

! **`mkfs` ha imparato `-f`**, e non e' per chi ha fretta: e' per chi la domanda
la fa gia' al posto suo. L'installatore mostra il piano intero e fa scrivere
«si» una volta sola; arrivato a mkfs, quella sarebbe la SECONDA conferma sulla
stessa decisione — e una conferma ripetuta e' una conferma che si impara a dare
senza leggere.

! **`libctest` e' passato sul CD.** Sono cinquanta kilobyte di sole prove su un
supporto da 1,44 MB che era arrivato a quattromila byte liberi. Stessa regola di
`swaptest`: chi PREPARA una macchina la avvia dal floppy e ha bisogno di fdisk,
mkfs, mkswap, install; chi PROVA ha il CD. Adesso il floppy ha 50 KB liberi.

## 26 agosto 2026 — la memoria virtuale, su una partizione dedicata

Il gradino sotto a tutto il resto: senza, un motore JavaScript su una macchina
da 32 MB non ha dove stare, e i file grandi non si leggono.

### Cosa c'e' adesso

    mkswap -f hd0p1              prepara la partizione (ci scrive la firma)
    [kernel] swap = hd0p1        in /boot/kernel.cfg
    swaptest 40                  la prova: 40 MB su una macchina da 32

    SWAP: 'hd0p1' attiva: 16127 slot da 4 KB (62 MB), elenco di 2016 byte
    swaptest: RAM libera 29000 KB, chiedo 40 MB (10240 pagine)
    swaptest: RAM libera prima 29000 KB, dopo 0 KB
    swaptest: tutte e 10240 le pagine sono tornate identiche.

Sul disco, contati dall'host a macchina spenta: **6021 slot scritti, 23 MB**.

### Le tre decisioni

! **SU UNA PARTIZIONE, NON DENTRO UN FILE.** Un file di scambio vorrebbe dire
passare dal filesystem per liberare memoria — allocare blocchi, aggiornare
inode, magari leggere metadati — e farlo proprio quando la memoria e' finita,
cioe' quando ogni allocazione puo' fallire. E' una dipendenza circolare: il
codice che serve a trovare memoria ne chiede. Una partizione e' una finestra di
settori che il livello a blocchi controlla gia': scriverci una pagina e'
«settore N, otto settori».

! **SI RICONOSCE DALLA FIRMA, NON DAL TIPO NELLA TABELLA.** Il byte 0x82 lo
scrive chiunque e non lo controlla nessuno: fidarsene vorrebbe dire che un
errore di fdisk basta a far scrivere pagine di memoria sopra un filesystem. La
firma «EXOSSWAP» sta nel primo settore DELLA partizione, ce la mette `mkswap`, e
senza quella il kernel non tocca niente.

! **E LA PARTIZIONE SI DICHIARA, NON SI CERCA.** Un kernel che andasse a caccia
di aree utilizzabili prima o poi ne troverebbe una su un disco altrui.

### Il limite, dichiarato: solo pagine con UN proprietario

Mandare via una pagina vuol dire segnare TUTTE le tabelle che la mappano, e
EX-OS non ha una mappa all'indietro. Quindi le pagine condivise — il codice
delle librerie, le zone di memoria condivisa — non si toccano: il PMM sa
contare i proprietari, e si sfratta solo chi ne ha uno.

Non e' una perdita grave: cio' che occupa RAM sotto pressione sono heap, stack
e dati privati. Il codice condiviso e' anche quello che si rileggerebbe piu'
facilmente dal file da cui e' venuto — ed e' il prossimo gradino, non questo.

### Come si sceglie chi se ne va

Seconda chance. Il bit Accessed lo accende la CPU da sola: una pagina che ce
l'ha acceso e' stata toccata da poco, e mandarla via vuol dire rileggerla
subito. Al primo incontro le si spegne il bit; se al giro dopo e' ancora
spento, quella pagina non la usa nessuno.

! **E SI RIPARTE DA DOVE SI ERA ARRIVATI, non dal primo processo.** Ricominciare
da capo vorrebbe dire scegliere la vittima in base all'ORDINE nella tabella dei
processi invece che all'uso — e il primo processo e' la shell.

### Il difetto che si e' visto solo provando, e che c'era gia' prima

La prima esecuzione di `swaptest 40` e' fallita **dopo aver sfrattato con
successo**: sessanta pagine erano gia' sul disco (contate aprendo l'immagine
dall'host) e poi

    [ERROR] PAGING: OOM durante map_page virt=0x0a000000

Una tabella delle pagine deve venire dalla **fascia kernel** — le pagine basse,
raggiungibili con qualunque CR3 caricato — e `pmm_alloc_page()` quella fascia la
regala come ULTIMA RISORSA anche alle pagine utente. Uno heap che cresce fino a
riempire la macchina se la mangia tutta, e la tabella successiva (una ogni 1024
pagine) non trova piu' niente.

! **NON E' UN DIFETTO DELLO SWAP: E' UN DIFETTO CHE SOLO LO SWAP POTEVA FAR
VEDERE.** Prima, una macchina piena moriva di OOM molto prima di arrivare li'.
Adesso `paging_map_page` sfratta anche lui, e in quella situazione le pagine
utente SONO anche quelle della fascia: qualcuna torna libera dove serve.

### Cosa NON e' cambiato

Senza `swap` in kernel.cfg il sistema si comporta esattamente come prima —
provato: `swaptest 40` risponde «malloc rifiutata» e la macchina continua a
girare. La memoria virtuale e' un di piu', non un requisito: un kernel che si
rifiutasse di partire senza swap sarebbe inutilizzabile proprio sulle macchine
piccole per cui lo swap serve.

! **E swaptest STA SUL CD, NON SUL FLOPPY.** Non e' una scelta di stile: il
floppy era a 17 KB liberi e due programmi nuovi non ci stavano. `tools/mkfloppy.sh`
copia TUTTO cio' che trova in `build/bin/` senza consultare nessun elenco,
quindi l'unico modo di tenere un programma fuori dal floppy e' non costruirlo
li' dentro (`BUILD_BIN_CD`). Chi PREPARA una macchina la avvia dal floppy e ha
`mkswap`; chi PROVA ha il CD.

## 26 agosto 2026 — ExWin: il fuoco, i colori casuali, e una risoluzione su tre

Tre guasti, e nessuno dei tre era dove sembrava. Il primo era nel driver di
tastiera, il secondo nell'allocatore di pagine fisiche, il terzo in una
costante scelta nel 2026 per la risoluzione del 2026. In ordine.

### 1. La scrivania si vedeva e la tastiera restava alla shell (commit `0f0e348`)

Digitando `exwin` la grafica compariva e i tasti no: cio' che si batteva
ricompariva tutto insieme tornando indietro con Alt+F1, che era l'unico
momento in cui la cosa si spiegava.

! **ERA UNA COPIA CHE NESSUNO AGGIORNAVA.** `g_attiva`, la console in primo
piano secondo `drivers/kbd/kbd.c`, si muoveva SOLO dentro `kbd_commuta()` —
cioe' solo quando a commutare era Alt+Fn. Vero finche' nessun altro chiamava
`console_switch()`. Ma `exwin` lo chiama, ed e' voluto: chi digita `exwin`
vuole la grafica adesso, non vuole leggere come arrivarci. Da quel momento il
kernel mostrava la console 4 e il driver consegnava i tasti alla 0.

E valeva anche al contrario, uscendo: e' `exwin --attendi` a riportare lo
schermo alla console di partenza, e i tasti restavano su quella della grafica,
che non c'e' piu'. Si vedeva il prompt e non si poteva scrivere.

La correzione non e' un messaggio in piu' — un «ho commutato» da mandare al
driver metterebbe la correttezza in mano a chi chiama, e il prossimo programma
che commuta senza saperlo rifarebbe lo stesso guasto. E' una domanda: il driver
chiede al kernel chi sia davanti, una volta per messaggio ricevuto, in cima al
ciclo (`attiva_segui()`). Una domanda non si puo' dimenticare.

! **ERA GIA' SCRITTO IN QUESTO FILE COME SE FOSSE UNA REGOLA DEL GIOCO:** «dopo
`exwin` i tasti vanno ancora alla shell, con `Alt+F2` si passa di la'». Era un
guasto, annotato per mesi come un fatto della vita.

### 2. Colori e disegni casuali uscendo dalla scrivania (commit `710f653`)

Su VirtualBox, chiudendo la scrivania con «Esci», poco dopo lo schermo si
riempiva di colori e disegni casuali. Non era la grafica: era il PMM.

`pmm_init`, Passo 1, cercava «l'indirizzo fisico piu' alto tra le regioni
**usabili**» — cosi' diceva il commento — mentre il ciclo prendeva il massimo
su TUTTE le voci E820, riservate comprese:

    PMM: RAM massima rilevata: 0xfee01000 (4078 MB)      <- l'APIC locale

! **E QUEL NUMERO NON STAVA SOLO IN UN LOG.** `g_total_pages` e' la risposta
del PMM alla domanda «questo indirizzo e' RAM?», e su quella si regge
`paging_destroy_directory`: quando un processo muore, le pagine OLTRE la RAM si
saltano, perche' sono finestre MMIO di `mmio_map`/`fb_map` e non le abbiamo mai
allocate noi. Col tetto a 0xFEE01000 il framebuffer di VirtualBox — 0xE0000000,
cioe' **sotto** — non era piu' riconosciuto: alla morte del server grafico le
sue 469 pagine di memoria video entravano fra le pagine libere. L'allocatore e'
next-fit: da li' in poi le consegnava come RAM qualunque.

Le due facce erano la stessa cosa:

    [FAULT] PID 17 '/bin/mem': page fault a 0xbfff0f52 (pagina assente)

e lo schermo pieno di colori — i dati del programma, letti dalla scheda come
celle di testo. Su QEMU il framebuffer sta piu' in alto e la stessa mappa lo
salvava per caso: ecco perche' non si riproduceva.

    prima:  RAM massima 0xfee01000, 1043969 pagine, 15585 libere
    dopo:   RAM massima 0x03ff0000,   16368 pagine, 15867 libere

! **E SI SONO RECUPERATI 1,1 MB DI RAM VERI** su una macchina da 64: bitmap e
elenco dei riferimenti erano dimensionati su un milione di pagine inesistenti,
un byte l'una.

### 3. A 1024x768 la scrivania non partiva affatto

Il sintomo non nominava la memoria: `pm: il server a finestre non risponde` —
cioe' il messaggio che la scrivania stampa quando NON TROVA il server, mentre
il server c'era, si era registrato, e aveva appena detto di no a una zona.

`SHM_PAGINE_MAX` valeva 512 pagine, 2 MB, con accanto scritto «una finestra
640x480x32 ne usa 300». Era vero, ed era tutto il difetto: il numero era stato
scelto guardando la risoluzione di allora.

    640x480   -> 283 pagine
    800x600   -> 447 pagine      ci passava per sessantacinque pagine
    1024x768  -> 740 pagine      rifiutata

Il tetto serve ancora — senza, un processo chiede una zona da tutta la RAM e la
ottiene — ma il limite giusto non e' un numero, e' una frase: **la cosa piu'
grande che una zona deve contenere e' una finestra grande quanto lo schermo.**
Piu' grandi non ne esistono, perche' `crea()` in `wserver.c` le stringe gia' al
framebuffer. Quindi il tetto ora e' lo schermo, e si sposta da solo il giorno
che si cambia risoluzione. `SHM_PAGINE_MIN` resta come pavimento, per quando
uno schermo grafico non c'e'.

### 4. I pulsanti: il comando parte al RILASCIO, e si vedono premuti

Due richieste che sono una modifica sola. Prima `EXM_COMANDO` partiva alla
PRESSIONE: e' il comportamento che si scrive per primo perche' e' il piu'
corto, ed e' anche quello che toglie a chi usa il programma l'unica
possibilita' di ripensarci. Su «Esci» o «Spegni» non e' estetica.

Adesso premere ARMA e alzare il dito SPARA, e scivolare via prima di alzarlo
annulla — come ovunque, dai Macintosh in poi. Il disegno segue il dito: il
pulsante si rialza appena il puntatore esce e si riabbassa se torna, perche'
deve dire in ogni istante cosa succedera' alzando il dito adesso.

E il premuto adesso si VEDE: `ex_rilievo` e `ex_incavo` c'erano gia' — sono lo
stesso `bordo3d` coi colori scambiati — e qui non le usava nessuno; l'unica
differenza fra su e giu' era il colore del riempimento, che si notava solo
confrontando due fotografie. Con l'ombra scambiata piu' la scritta spostata di
un pixel in giu' e a destra, il pulsante SI MUOVE.

! LA PROVA E' CHE UN COMANDO NON PARTE. Premuto «Spegni», trascinato fuori,
rilasciato: la macchina e' ancora accesa e nel registro non c'e' nessuno
spegnimento. Una prova che un'azione NON avviene vale piu' di una fotografia.

### 5. «Riavvia» nel menu di avvio

Sta fra «Esci» e «Spegni», in ordine di gravita'. E oggi serve piu' del solito:
uscendo dalla scrivania la scheda torna in modo testo e `exwin` rifiuta di
ripartire — la modalita' grafica la imposta Stage 2 col BIOS — quindi per
rivedere la scrivania bisogna riavviare, e quel comando deve stare dove sta chi
guarda la grafica, non in una console di testo.

! E NON CHIAMA reboot() SUBITO. Chiede alla scrivania di spegnersi — come
«Esci» — si segna che dopo si riavvia, e riavvia da `main()`, DOPO che il
ciclo dei messaggi e' finito e DOPO aver aspettato che il kernel dica che la
console della grafica e' libera. Cosi' le applicazioni fanno in tempo a
salvare e lo schermo e' gia' testo: se il riavvio venisse rifiutato si resta
davanti a una console leggibile.

    pm: riavvio chiesto dal menu
    wserver: spegnimento chiesto
    [WARN] SYSCALL modo_testo: PID 11 rimette lo schermo
    wserver: spento
    pm: la scrivania e' uscita, riavvio la macchina

La prima stesura riavviava subito dopo `ex_spegni_scrivania()` — che e' un
MESSAGGIO, non una chiamata — e la seriale lo ha detto: `modo_testo` non
compariva affatto. Il commento prometteva un'attesa che il codice non faceva.

### 6. www.bing.com: mancava RSA con SHA-384 (e come si e' trovato)

Il sintomo era `certificato non verificabile`, e per tre giri di prove ha
mandato nel posto sbagliato. La catena di bing presa con `openssl s_client`
senza opzioni e' ECDSA, e passata al banco `certprova` sull'host **valeva**.

! LA STESSA SCHEDA DA' CATENE DIVERSE A CLIENTI DIVERSI. Chiesta con i NOSTRI
parametri — ChaCha20-Poly1305, X25519, le nostre tre firme — bing risponde con
una catena RSA il cui leaf e' firmato `sha384WithRSAEncryption`. E
`excert_firma_valida` accettava, per RSA, il solo SHA-256:

    if (figlio->alg_firma != EXASN1_ALG_RSA_SHA256) return EXCERT_ALG_RIFIUTATO;

`exasn1` quegli OID li riconosceva gia' da sempre — RSA_SHA384 e RSA_SHA512
sono nella sua tabella — era `excert` a rifiutarli. Adesso i DigestInfo sono
tre e l'impronta la sceglie l'algoritmo. Il vettore dell'impronta e' passato da
32 a 64 byte: lasciarlo a 32 e scriverci uno SHA-512 sarebbe stato uno
sfondamento di stack in una funzione che tocca i dati di chiunque risponda su
una 443.

    prima:  scarica: certificato non verificabile
    dopo:   scarica: 200, text/html; charset=utf-8, 66645 byte

! E CI SI E' ARRIVATI SOLO DOPO AVER FATTO PARLARE L'ERRORE. Il codice esatto
c'era gia' — extls lo tiene in `motivo` apposta, e accanto alla chiamata c'e'
scritto perche' — ma non lo leggeva nessuno: usciva sempre e solo «certificato
non verificabile», che vale per uno scaduto, per una radice che manca e per una
firma falsa. Adesso escono il motivo e QUALE ANELLO:

    certificato non verificabile: algoritmo di firma non gestito (anello 0)

ed e' quella riga che ha detto dove guardare. Nello stesso giro si e' sdoppiato
`EXCERT_ALG_RIFIUTATO`: la firma del figlio e la chiave del padre sono due
guasti che si riparano in due posti diversi.

### E due commenti che dicevano il falso

! `extls.h` dichiarava in intestazione: «un sito che offre SOLO certificati
ECDSA non si apre, la P-256 non c'e'». Era vero quando fu scritto e falso dal
25 agosto — venti righe piu' in basso, nel codice, il client annuncia
ecdsa_secp256r1 ed ecdsa_secp384r1. Chi leggeva l'intestazione e chi leggeva il
codice avevano due risposte diverse, e l'intestazione si legge per prima.
Corretta, non cancellata.

! E in `tools/prove/certprova.c` mancava `sha512`, cosi' il banco non compilava
piu' appena excert ha imparato SHA-512. E' *esattamente* il difetto che il
Makefile ha gia' scritto accanto a quel bersaglio, capitato di nuovo: «una
prova che non parte non fallisce, tace».

### Due porte trovate aperte cercando la seconda, e chiuse

! **DICHIARATO: NON SONO LA CAUSA DI NIENTE DI QUANTO SOPRA.** Non si sono mai
viste sbattere, e i commenti nel codice lo dicono a chiare lettere per non far
credere a chi legge domani di aver trovato il colpevole.

- `vga_ripristina_testo()` girava interrompibile. Le syscall passano da un TRAP
  gate — `idt.c` lo scrive apposta — quindi il timer puo' dare la CPU a un
  altro processo in mezzo al cambio di modalita'. Adesso gira a interrupt
  chiusi, e `g_fb` si azzera PRIMA di toccare la scheda.
- `wserver` diceva «la grafica e' finita» (`console_grafica(2)`) PRIMA di
  rimettere il testo. Quel segnale e' cio' che `exwin --attendi` aspetta per
  chiamare `console_switch()`. Adesso prima si rimette lo schermo.

### Come si sono provati, e con che macchine

Il guasto 2 **non si riproduce su QEMU**: e' servito VirtualBox, che qui non
parte finche' `kvm_intel` e' caricato. VBoxManage la tastiera la inietta ma il
mouse no, e «Esci» si preme — il mouse e' arrivato dall'API Python (`vboxapi`,
`IMouse.putMouseEvent`).

! **E IL MOUSE VA MOSSO A PASSI PICCOLI, come sotto QEMU.** Il server chiede lo
stato del mouse una volta per giro — venti millisecondi — e una richiesta per
volta: con passi da cento pixel il puntatore arrivava a due terzi di strada, il
clic cadeva su «Navigatore» invece che su «Esci», e la prova diceva «l'uscita
non funziona» avendo aperto il browser.

### Un errore mio, di quelli che questo file tiene scritti

! **HO DATO UNA CAUSA PRIMA DI AVERLA MISURATA.** Per il guasto 2 avevo
concluso che l'identificativo VBE di VirtualBox cadesse fuori dall'elenco
accettato da `vbe_spegni()`, e l'ho scritto in un commento come fatto. Poi ho
ricostruito un'immagine col codice precedente e l'ho provata: su VBoxVGA quel
ripristino **funzionava gia'**. La sostituzione dell'elenco con la domanda
scritta-e-riletta resta — un elenco di numeri noti e' sempre incompleto — ma il
commento adesso dice che si toglie perche' non c'era ragione di fidarsene, non
perche' lo si sia visto tradire.

E' della stessa famiglia dei tre errori del 25 agosto: una conclusione tratta
da qualcosa che assomigliava a una prova. La differenza fra le due volte e'
solo che stavolta la prova l'ho fatta prima di lasciarla scritta.

! **E IL PASSO 1 DI `pmm_init` E' LO STESSO ERRORE VISTO DALL'ALTRO LATO:** il
commento diceva la cosa giusta — «solo regioni usabili» — sopra un ciclo che le
prendeva tutte, da sempre. Come `/bin/svga` ieri: li' il commento mentiva e il
codice aveva ragione, qui il contrario. **La prova sta sempre nel codice, mai
accanto.**

## 25 agosto 2026 — il browser: impaginazione, immagini, caratteri, moduli

Quattro giri in un giorno, e tre di loro sono nati da una misura che ha
smentito il nome della cosa. In ordine.

### 1. Il posto tenuto in anticipo (commit `a16832c`)

Una pagina di Wikipedia con nove immagini impiegava minuti, e non era piu' la
rete: a **ogni** immagine che arrivava si rifaceva `impagina()` su
ventiquattromila pezzi. Quando l'`<img>` dichiara `width` e `height` la misura
finale si sa PRIMA di scaricare un byte: si tiene il posto gia' giusto, e
quando l'immagine arriva ci entra dentro senza spostare niente — allora si
**ridisegna** soltanto.

    fra la prima immagine e la dodicesima      7,0 s  ->  3,3 s
    per immagine                             ~0,45 s  -> ~0,06 s

E la prova che non si sposta niente e' pixel per pixel: fra la schermata coi
soli riquadri riservati e quella con tutte le immagini arrivate cambiano 60800
pixel DENTRO la banda delle immagini e **zero** fuori. La stessa pagina senza
`width`/`height` ne cambia 7191 — cioe' la misura sa vedere il movimento.

! **E QUELLA CORREZIONE NE HA SVELATA UN'ALTRA.** Confrontando le due build
sulla stessa voce, quella che reimpaginava di MENO mostrava PIU' contenuto.
`impagina()` azzerava tutto quel che produce — pezzi, collegamenti, moduli,
sfondi — ma **non** `g_ctrl_n`: ogni giro accodava una copia dei controlli, e
oltre `CTRL_MAX` (64) `impagina_nodo` rinunciava al controllo **e a tutto il
sottoalbero sotto di lui**. Spariva pagina anche lontano dai moduli.

### 2. Le immagini non le conta piu' una costante (commit `f5b2b1a`)

`IMM_MAX` valeva dodici e sembrava il limite. Non lo era: una casella di quel
elenco costa l'indirizzo che ci sta dentro (600 byte), non i pixel — quelli li
contava `IMM_PX_TOT`. Sulla voce «Operating system» le immagini con le misure
dichiarate sono venticinque e, dopo il cap alla finestra, ne vogliono 2,2
milioni: con mezzo milione ne entravano **cinque**.

Alzando quella costante a un milione la macchina da 32 MB finiva le pagine
fisiche (`[ERROR] PMM: OUT OF MEMORY`). Un tetto fisso e' sbagliato in tutt'e
due i versi, quindi adesso **lo sceglie la macchina**: un sedicesimo della
memoria libera letta con `meminfo()`, fra 256K e 2048K pixel. E il posto si
controlla PRIMA di scaricare, non dopo.

### 3. L'arena non era piena di testo (commit `f1d96bc`)

Si chiamava «l'arena del testo». Misurata:

    il testo dei nodi     71 KB   15%
    i nomi dei tag        20 KB    4%
    gli ATTRIBUTI        386 KB   81%

Si troncava una pagina di 70 KB di parole perche' non c'era posto per gli
attributi. E alzare la sola arena non sarebbe bastato: quella voce ha ~13.600
attributi contro un `ATTR_MAX` di 12.000. Adesso arena 1 MB e ATTR_MAX 16000 —
la voce di Wikipedia **non si tronca piu'** (11234 nodi) e le immagini
raggiungibili passano da 15 a tutte e 25.

Nello stesso giro: **`font-family`**, che il CSS non leggeva affatto. Ogni
pagina usciva in serif e il monospazio arrivava solo dal tag. Adesso l'elenco
si scorre e ci si ferma al primo nome che si conosce, riducendolo a Serif /
Sans / Mono. Il tag batte il foglio solo dove il foglio tace.

### 4. Caratteri, tabelle e moduli — da commettere

**Il ripiego dei font.** Liberation copre 2327 codici, DejaVu 5918. Non si
sostituisce il font: si **ripiega carattere per carattere**, come fa ogni
sistema con un fontconfig — il testo resta com'e' e si riempiono solo i buchi.
`exttf_ha_glifo` e' nuova in `exfont.so`, e non e' fra i simboli obbligatori:
una exfont.so piu' vecchia non ce l'ha e semplicemente non ripiega.

**Le entita' sopra il Latin-1** diventavano `?`: `&#8212;`, le virgolette
curve, i puntini. La ragione era vera quando fu scritta — il font di sistema ha
256 glifi — ma il testo delle pagine lo disegna un TrueType. Il posto giusto
per perdere un carattere e' il disegno, non il lettore: adesso l'arena tiene il
codice in UTF-8.

**colspan e rowspan.** Senza, ogni tabella con un'intestazione che scavalca due
colonne mandava fuori posto tutte le celle dopo di lei. L'altezza di una cella
che scavalca si divide fra le righe che occupa, e serve una mappa di cio' che
e' gia' occupato — non un contatore di celle.

**E i moduli non ricevevano i tasti.** La voce diceva «la `<textarea>` non ha
un cursore»: non era il cursore a mancare, erano i **tasti**. `ex_fuoco(g_url)`
all'avvio da il fuoco a un controllo del toolkit, e da quel momento ogni tasto
e' suo — i controlli della pagina sono rettangoli disegnati, non finestre,
quindi non potevano averlo. Si scriveva nella barra dell'indirizzo credendo di
scrivere nel modulo. Ora c'e' `ex_fuoco_via()` nel toolkit.

! **E SOTTO C'ERA UN DIFETTO DEL TOOLKIT**, che riguarda ogni programma e non
solo il browser: quando una casella consumava un tasto, `exwin` rifaceva sfondo
e controlli **senza avvisare l'applicazione** — «per non svegliarla a ogni
lettera». Giusto per una finestra di soli controlli; per una che disegna anche
del suo, quel disegno spariva a ogni tasto. Bastava scrivere nella barra
dell'indirizzo per far sparire la pagina. Adesso il messaggio arriva: costa un
`EXM_DISEGNA` per tasto battuto, non per pixel di mouse mosso.

### 5. Le tabelle coi bordi, e le connessioni di un processo morto

`<table border="1">` disegna un filo intorno a ogni cella e alla tabella. E'
l'attributo e non il CSS, perche' e' cosi' che si sono disegnate le tabelle per
vent'anni e le pagine che lo usano sono quelle senza foglio di stile. I bordi
stanno nello stesso elenco degli sfondi: un bordo e' un rettangolo sotto il
testo e sopra lo sfondo, come uno sfondo.

! **«RIAPRI IL BROWSER E NON NAVIGA PIU'»: -23, cioe' ENFILE.** Il driver IP
tiene una tabella di connessioni e ogni voce ricorda il PID di chi l'ha aperta.
Un programma chiuso con la crocetta non chiude niente, e il suo slot restava
occupato **per sempre**: con otto slot bastavano poche aperture.

! **E IL PROGRAMMA NON PUO' RIPARARE DA SE'**: se e' morto non gira piu' nessun
suo codice. Adesso, quando la tabella e' piena, prima di rispondere -ENFILE si
guarda chi e' morto con `procinfo()` — e uno zombie non conta come vivo. Il
tetto passa da 8 a 24 perche' otto erano pochi anche senza la perdita: una
pagina tiene aperta la connessione per riusarla mentre un servitore ascolta e
un altro programma scarica.

### 6. Il sistema: lingua, hardware, console della grafica

**`lingua` in [kernel] di kernel.cfg**, chiesta dall'installatore. Il kernel non
la usa per niente — tradurre e' lavoro dei programmi — ma la conserva e la
riconsegna con `getenv`, esattamente come `keymap`: cosi' non ci sono due
elenchi di lingue che divergono. `hwconfig` non la cancella.

**`install` lancia `hwconfig` in fondo**, e gli passa la radice del disco
appena installato: senza argomento riscriverebbe il kernel.cfg del sistema che
sta *girando* — quello del CD — lasciando il disco con l'elenco di driver
sbagliato, cioe' il caso che si voleva evitare.

**Le console diventano cinque.** La grafica se ne prendeva una delle quattro e
chi lavorava in testo ne aveva tre senza averlo chiesto. Adesso Alt+F1..F4
restano di chi scrive e la grafica sta in fondo; `exwin` ci passa da solo e
torna indietro quando la grafica si spegne.

! **E LA CONSOLE DELLA GRAFICA ESISTE SOLO MENTRE LA GRAFICA C'E'**
(`SYS_CONSOLE_GRAFICA`, kernel 0.207): finche' il server non gira, Alt+F5 non
fa niente — li' ci sarebbe uno schermo nero con una shell che nessuno guarda.
Lo stato sta nel **kernel** e non nel server, cosi' un server ucciso libera la
console da solo.

**`Esci` dal program manager spegne la scrivania.** Alle applicazioni si
CHIEDE, non le si uccide: a ognuna arriva la stessa chiusura della crocetta.

### 7. La risoluzione, e una conclusione sbagliata da cui imparare

! **AVEVO SCRITTO CHE IL MECCANISMO NON C'ERA, E MI ERO SBAGLIATO.** Avevo
letto solo il ciclo che scorre i modi VESA; sopra c'e' il cancello — `svgamodo`,
un byte dentro Stage 2 marcato dalla firma `SVGAMODE`. E il programma che lo
scrive esiste: e' `/dev/svga.drv`, non `/bin/svga`.

! **OTTO COMMENTI DICEVANO IL NOME SBAGLIATO**, e mi ci sono fidato. Un nome
sbagliato in un commento e' il modo in cui si conclude che una cosa non esista:
e' costato un giro di lavoro speso a progettare una cosa gia' fatta. Adesso
sono corretti, e in `cfg.h` resta scritto **apposta** che «/bin/svga» non e' mai
esistito, con la ragione per cui quel programma sta in /dev (l'estensione .drv
lo fa entrare nel catalogo dei driver). Chi cerchera' il vecchio nome finira'
sulla spiegazione invece che in un vicolo cieco.

Mancava solo l'interfaccia: **Avvio > Impostazioni...** mostra la risoluzione di
adesso (chiesta al server, non indovinata) e i quattro modi.

### 8. Copia e incolla dentro una pagina

I campi di un modulo HTML sono rettangoli disegnati, non controlli del toolkit:
per questo `ex_area_copia` non li riguardava, e la scrivania aveva gli appunti
dappertutto tranne che dentro una pagina. Adesso c'e' la selezione, e **due**
scorciatoie perche' nessuna e' «quella giusta»:

    Ctrl+C   Ctrl+X   Ctrl+V   Ctrl+A      Windows e desktop moderni
    Ctrl+Ins   Shift+Ins   Shift+Canc      CUA: DOS, OS/2, terminali Unix

Gli appunti sono quelli di **tutta la scrivania**: `ex_appunti_metti` e
`ex_appunti_prendi` (nuove in exwin.so) usano la stessa memoria condivisa di
`ex_area`, quindi si copia da una casella e si incolla in un editor. Stanno
nella libreria condivisa perche' serviranno anche all'editor RTF e all'IDE.

! **IL BROWSER BUTTAVA VIA I MODIFICATORI**: `wp & 0xFFFF` in cima al gestore
dei tasti. Il sintomo di Ctrl+C e' stato una «c» scritta dentro la casella.

---

## 25 agosto 2026 — tre errori miei, e cosa insegnano

Vale la pena tenerli scritti: sono tutti e tre della stessa famiglia.

**1. Un numero di syscall preso senza contare i doppioni.**
`SYS_CONSOLE_GRAFICA` messo a 233, che era gia' `SYS_IOPORT_IN16`. Il sintomo
non somigliava alla causa: il driver di rete leggeva la propria firma con
`in16` e si vedeva tornare «00 00», quindi dichiarava che la scheda non
c'era. Mezzo pomeriggio dentro la rete per una riga scritta altrove. Il
controllo costa un comando, ed e' scritto accanto alla voce:

    grep -oE '#define SYS_[A-Z_0-9]+ +[0-9]+' kernel/include/syscall.h \
      | awk '{print $3}' | sort -n | uniq -d

**2. Una patch fallita a meta', controllata a meta'.**
`exwin` doveva lanciare una copia di se' con `--attendi`; il ramo che riconosce
quell'opzione non e' mai stato aggiunto perche' la patch era fallita, e io
avevo guardato solo che la SECONDA meta' fosse andata a posto. Ogni copia si
comportava da exwin normale e ne lanciava un'altra: schermo blu, e in fondo
alla seriale «SCHED: PCB pool esaurito». Una ricorsione che si vede solo
dall'esaurimento non somiglia a una ricorsione — e l'avevo pure scambiata per
un difetto dell'incolla, poi per uno screendump preso a meta' di un ridisegno.
Un `grep` del nome che avrei dovuto aver aggiunto lo diceva in un secondo.

**3. Una conclusione tratta da un commento invece che dal codice.**
Vedi il punto 7 qui sopra: `/bin/svga`.

! **E UNA COSA SUL BUILD, che vale per chi tocca /exwin:** `make` da solo NON
ricostruisce i programmi grafici — si ferma su un file bandiera. Un errore di
compilazione in `pm.c` e' rimasto invisibile finche' non ho lanciato `make pm`.
Chi tocca `exwin/bin/*` lanci il bersaglio suo (`make pm`, `make browser`,
`make exwincmd`).

---

# DOVE RIPRENDERE — 24 agosto 2026

## Lo stato in una riga

Il **gradino 1 e' cominciato e regge**: un server a finestre in ring 3 su una
console sua, il toolkit ExWin con header C, C++ e FreeBASIC, e la tastiera che
arriva alle finestre — dal 26 agosto 2026 **senza dover premere Alt+Fn**, e a
tutt'e tre le risoluzioni che Stage 2 sa impostare. E una shell gira dentro una
finestra, su due pipe.

Il **gradino 0** (mappatura framebuffer, memoria
condivisa, poll/select, ritorno al modo testo) e l'input funziona da PS/2,
seriale e USB — tastiera compresa, hub compresi.

## COSA E' COMMITTATO

    696f728  "La rete si accende prima dell'accesso, il browser tiene le
              pagine, install fa tre domande"

Kernel **0.205**. Vedi la sezione sui tre posti della rete, qui sotto.

In attesa nell'albero — **PROVATO, DA COMMETTERE**:

  - **IL RIUSO DELLE CONNESSIONI**, e il puntatore appeso che ha svelato — un
    difetto che ha funzionato per mesi perche' nessuno usava quella memoria
    piu' tardi. Piu' POST, l'elenco a tendina dei `<select>`, la `<textarea>`
    che prende gli a capo, e le immagini in formati che non sappiamo leggere
    che non si scaricano piu'. Sezioni qui sotto;
  - **LA CODA DELL'https, SVUOTATA**: i tetti del browser scelti su una pagina
    vera, i suggerimenti di presentazione (la barra arancione di HN), i moduli
    che si MANDANO in GET, i margini in linea, il `<select>` usabile, e
    `genera()` che non rende piu' un offset valido per dire «non c'e' posto».
    Piu' il BIO di OpenSSL, che non era un bug. Sezione qui sotto;
  - **IL WEB VERO**: ECDSA su P-256 e P-384 (wikipedia.org, HN, github.com si
    aprono), la barra di scorrimento, i moduli disegnati, le GIF, i font che
    non si esauriscono piu' a otto, e `/bin/shutdown` per `sudo`. Sezione qui
    sotto;
  - **`https` NEL BROWSER.** TLS 1.3 scritto in casa: X25519,
    ChaCha20-Poly1305, catena dei certificati verificata contro un magazzino
    di 150 CA e nome del sito confrontato col `subjectAltName`. La sezione qui
    sotto racconta i tre difetti che ci sono voluti per arrivarci;
  - **`shutdown` NON E' PIU' SOLO DI root.** Lo puo' chiedere chi sta a una
    console di questa macchina; una sessione remota no. Sezione sotto;
  - **il motore degli script non tronca piu' a 2 KB**: leggeva tutto in una
    volta in un buffer da 2 KB e tagliava il resto — su `/boot/avvio.sh` il
    taglio cadeva in mezzo all'ultima riga;
  - **`PROC_NASCENTE`**: un processo che nasce non e' un processo bloccato, e
    finche' lo era ogni risveglio per PID poteva farlo partire con EIP=0;
  - **`libctest` prova la stdio su un file grande e di sola lettura**, e ha
    SCARTATO un sospetto scritto in `in_lavorazione.txt`.

**326 prove su 326** con `libctest`; **9 su 9** con `make prova-cliente-tls`.

## Il puntatore appeso che ha aspettato mesi

! **UN DIFETTO CHE FUNZIONAVA.** `extls_stretta` non copia il trasporto di
sotto: si tiene il PUNTATORE, e lo usa a ogni record per tutta la vita della
connessione. `exhttp_tls` gli passava una **variabile locale**, che muore quando
la funzione esce.

Per mesi non si e' visto, e la ragione e' istruttiva: la connessione si usava
SUBITO dopo la stretta, dalla stessa profondita' di stack, e quei byte erano
ancora quelli giusti. Il difetto e' comparso il giorno del riuso — la seconda
immagine di una pagina, presa piu' tardi da un'altra parte del programma,
leggeva funzioni da spazzatura e saltava dentro il nulla.

! **E LA CACCIA E' PARTITA DAL POSTO SBAGLIATO, DUE VOLTE.** Prima il sospetto
e' andato al riuso delle connessioni, che era appena arrivato: spento, il
difetto restava. Poi a un `sprintf` su un buffer da 160 byte, che era davvero
troppo piccolo ma non c'entrava. Il colpevole si e' trovato solo mettendo una
traccia dentro il ciclo delle immagini — **la prima immagine passava, la
seconda no** — che e' esattamente la firma di uno stato riusato.

Un puntatore appeso non si vede finche' non cambia chi cammina sopra quella
memoria.

## Il riuso delle connessioni

! **SU https LA STRETTA DI MANO E' TUTTO IL COSTO**: chiave effimera, catena di
certificati, firma — su un 386 emulato sono venti secondi. Una pagina di
Wikipedia con dieci immagini li pagava dieci volte, e la barra di stato diceva
«immagine 9 di 9» per due minuti.

Adesso la connessione resta aperta e la richiesta dopo, se e' per lo stesso
posto, non paga niente. Le tre regole:

  - **si riusa solo se si sa dove finisce il corpo** (Content-Length o pezzi).
    Senza, la fine E' la chiusura: non c'e' niente da riusare;
  - **si riprova una volta**. L'altra parte puo' chiudere in qualunque momento
    senza dirlo — e' normale, non e' un errore;
  - **`Connection: close` dal server si rispetta**, e si cerca dentro l'elenco:
    «keep-alive, close» e' valido, e chi confronta tutta la riga si perde
    proprio il caso in cui il server sta dicendo che chiude.

! **E LE IMMAGINI CHE NON SAPREMMO DECODIFICARE NON SI SCARICANO PIU'.** Otto
delle undici immagini della voce «Operating system» sono SVG: due minuti passati
a scaricare file che finiscono comunque nel cestino. La regola e'
sull'estensione, ed e' un'approssimazione dichiarata — il tipo vero lo direbbe
il Content-Type, che pero' arriva dopo aver aperto la connessione, cioe' dopo
aver speso quello che si voleva risparmiare.

## La coda dell'https, svuotata

Tutte le voci che erano in `in_lavorazione.txt` sotto «per una pagina web
moderna», tranne quelle dichiarate fuori.

### I tetti, scelti da una pagina vera e non a occhio

La voce «Operating system» di Wikipedia e' 676 KB di HTML e ~11000 tag. Con i
tetti di prima — mezzo megabyte, quattromila nodi — si troncava a meta', e il
troncamento di una pagina non e' una pagina corta: e' un albero senza chiusure.
Adesso 1 MB, 24000 nodi, 24000 pezzi, e il conto di cosa costa e' scritto in
cima a `browser.c` — 5,5 MB su 32.

! **E GLI INDIRIZZI DEI LINK STANNO IN UN'ARENA**: erano 512 caselle da 600
byte (300 KB quasi tutti vuoti, un indirizzo medio sta in sessanta). Duemila
link costano quanto sono lunghi davvero.

### `#Main page` sovrapposto a `Main page`

! **ZERO NON PUO' VOLER DIRE «NON C'E' POSTO».** `genera()` — che scrive i segni
degli elenchi in coda all'arena — rendeva 0 quando l'arena era piena. Ma zero e'
un OFFSET VALIDO: e' l'inizio dell'arena, cioe' il primo testo della pagina. Il
segno di ogni voce veniva disegnato con quel testo, e su Wikipedia si vedeva
`#Main page` sovrapposto a `Main page`.

Sembrava che l'impaginazione disegnasse due volte. Non disegnava due volte:
disegnava **una volta la cosa sbagliata**. Adesso l'impossibile e' un valore che
non puo' essere un offset.

### I suggerimenti di presentazione

! **MEZZO WEB SCRIVE ANCORA I COLORI NEGLI ATTRIBUTI**: `<table
bgcolor="#ff6600">` e' la barra arancione di Hacker News, `<td align="right">`
e' come si incolonnavano i numeri prima del CSS. Stanno al gradino piu' BASSO
della cascata, quindi si applicano DOPO `css_calcola` e solo dove il foglio non
ha detto niente — applicarli prima vorrebbe dire che un `bgcolor` batte una
regola CSS.

! **E LO SFONDO DELLA TABELLA E' SUO, NON DELLE CELLE**: la strada delle tabelle
salta tutta la logica dei blocchi, e con lei il riquadro di sfondo. La barra
arancione era proprio quel caso.

### I moduli si mandano

GET: la query si costruisce dai `name`, con la codifica percento — senza,
cercare «pane & vino» manda due campi invece di uno, e il secondo si chiama
« vino». I radio si spengono per GRUPPO (`name`), non tutti insieme. L'Invio
dentro una casella manda il modulo, che e' come si usa una casella di ricerca.
POST no, ed e' dichiarato: vuole un `http_richiesta` capace di un corpo.

### E i margini in linea

Su un blocco un margine e' un rientro del lato; su uno `<span>` non c'e' nessun
lato a cui attaccarsi — e' spazio orizzontale prima e dopo, ed e' cosi' che i
siti separano le voci di un menu. Senza, quelle voci si toccano.

## Il BIO di file di OpenSSL: NON era un bug

Per settimane l'accusa era alla nostra stdio. Due passi per scagionarla:

1. `libctest` apre QUEL file — 224 KB su ISO 9660 — con la sola stdio e lo
   legge fino in fondo. Il sospetto scritto in `in_lavorazione.txt` era
   sbagliato.
2. `crypto/o_fopen.c`: con **OPENSSL_NO_STDIO** definito, `openssl_fopen` rende
   NULL **sempre** — e `BIO_C_SET_FILENAME` alza esattamente quell'errore. La
   nostra OpenSSL e' configurata `no-stdio` (`tools/openssl-exos/`).

Non c'e' niente da riparare: leggere il file con open/read e passarlo a un BIO
di memoria e' la strada giusta per una libreria costruita senza FILE*. Sta
scritto anche in cima a `tools/iso/prova-tls.c`, perche' nessuno ricominci.

## Il web vero: ECDSA, i moduli, la barra e le GIF

! **SEI COSE CHIESTE INSIEME, E UNA SOLA ERA GROSSA.** Vale la pena dire quale,
perche' la stima a occhio era sbagliata: il TLS ellittico sembrava il lavoro di
mezza giornata e i font una sciocchezza, ed e' andata quasi al contrario.

### `sudo shutdown` diceva «comando non trovato»

! **UN BUILTIN NON SI PUO' PASSARE A `sudo`.** La shell aveva gia' `shutdown`,
`poweroff`, `reboot` e `halt`, e funzionavano — ma `sudo` fa uno `spawn`, e di
un builtin non c'e' nessun file da eseguire. Adesso c'e' `/bin/shutdown`, con i
suoi tre nomi alternativi come id/whoami. **Il builtin resta**: la shell di
recovery e' statica apposta e deve poter spegnere una macchina il cui `/bin`
non si legge piu'.

### ECDSA: due curve, non una — e la seconda si e' scoperta guardando

Con le sole firme RSA meta' del web rispondeva «allarme 40». Aggiunta la P-256,
`wikipedia.org` rispondeva ancora no e `example.com` — che prima funzionava —
ha smesso: annunciando ECDSA i server mandano la catena ellittica, e quella e'
firmata **ecdsa-with-SHA384** con chiavi su **P-384**.

    wikipedia.org:  *.wikipedia.org (P-256)  <- YE2 (P-384)  <- ISRG Root YE
                    tutte le firme ecdsa-with-SHA384
    example.com:    leaf ecdsa-with-SHA256, intermedia ecdsa-with-SHA384

! **LA CURVA DELLA CHIAVE E L'IMPRONTA DELLA FIRMA SONO INDIPENDENTI**, ed e' la
cosa che si sbaglia: una chiave P-256 firmata con SHA-384 e' normalissima.
Legare le due — «P-256 quindi SHA-256» — vuol dire rifiutare catene valide.
E dell'impronta si prendono i bit piu' a sinistra, tanti quanti `n`.

Quindi: `lib/excurva` (P-256 e P-384, solo verifica), `sha384` in excrypt (stesso
motore di SHA-512, altro valore iniziale, 48 byte), gli OID nuovi in exasn1, il
ramo in excert e i due algoritmi annunciati in extls.

! **SI VERIFICA E BASTA, NON SI FIRMA.** Firmare vuol dire generare un numero
segreto per ogni firma, e un generatore appena debole rivela la chiave privata.
Un browser non ha niente da firmare; scrivere quel codice «per completezza»
vorrebbe dire mettere in casa un'arma carica.

Prove: `make prova-excurva` — 13, con le combinazioni miste (P-256 con SHA-384,
P-384 con SHA-256) e i rifiuti (bit girato in r, in s, nell'impronta, nella
chiave; r o s a zero; punto fuori dalla curva).

### Due difetti che il TLS ha soltanto SVELATO

! **`if (len > max) len = max` IN `tcp_leggi`, e il resto del pezzo si
BUTTAVA.** Con l'HTTP sopra non si vedeva mai — l'HTTP chiede sempre un buffer
grande — ma il record di TLS legge PRIMA cinque byte e poi il resto.

! **E IL CONTROLLO DEL BUFFER DELLE INTESTAZIONI STAVA PRIMA DELLA LETTURA.**
TCP consegna pezzi da un chilo e mezzo, quindi quel buffer da 16 KB non si
riempiva mai in una volta; il TLS consegna un RECORD INTERO, che e' esattamente
16 KB. Al giro dopo si usciva con «intestazioni troppo lunghe» senza aver mai
guardato le intestazioni, che stavano nei primi settecento byte. Si e' visto su
news.ycombinator.com.

! **E UN TERZO NEL Makefile, che ha fatto perdere mezz'ora**: `EXHTTP_SRC` era
definito DUECENTO RIGHE DOPO la regola che lo usa, e make espande le dipendenze
quando legge la regola. `exhttp.so` non dipendeva da `exhttp.c`: la correzione
qui sopra «non aveva effetto», e il CD conteneva la libreria di prima.

### Il browser: barra, moduli, immagini

**La barra di scorrimento** con il pollice proporzionato alla parte visibile —
l'unica cosa che dice QUANTO manca. Il posto si riserva sempre, anche quando la
barra non serve: se la larghezza cambiasse a seconda della lunghezza del
documento, un documento al limite oscillerebbe.

! **E DISEGNANDOLA E' VENUTO FUORI CHE IL TESTO NON SI RITAGLIAVA.** `ex_scrivi`
taglia alla FINESTRA, non all'area del documento: una riga che cominciava sopra
il bordo veniva dipinta SOPRA LA BARRA DELL'INDIRIZZO. C'era da sempre; si
vedeva solo con un documento piu' lungo della finestra.

**I moduli** — caselle, password, spunte, radio, scelte, pulsanti, aree — si
disegnano coi rilievi del toolkit e non sono finestre figlie: un `<input>` in
mezzo a un paragrafo scorre col testo, e farne una finestra vorrebbe dire
spostarla a ogni riga di scorrimento. Si cliccano e ci si scrive. **Non si
mandano**, ed e' dichiarato: il pulsante lo DICE nella barra di stato.

**Le GIF**: PNG e JPEG c'erano gia', mancava LZW — che non e' il deflate del
PNG. Dizionario che cresce mentre si legge, larghezza del codice che cambia
sotto i piedi, codici a cavallo dei blocchi da 255 byte. Confrontata pixel per
pixel con ImageMagick su cinque immagini, intreccio e trasparenza compresi.

### I font: non erano i file

`fontprova` diceva «NON aperto» su quattro file su dodici, e sembravano quattro
file guasti. Erano gli **otto slot** della tabella dei font del toolkit.

! **MA IL NUMERO NON ERA IL PUNTO: OGNI VOCE TENEVA UNA COPIA DEL FILE.** Un
Liberation pesa fra i 280 e i 410 KB, e il browser apre la STESSA faccia a
cinque o sei corpi — cinque copie degli stessi quattrocento kilobyte su una
macchina che ne ha trentadue milioni. Alzare il limite e basta avrebbe
trasformato un difetto visibile in un esaurimento di memoria.

Adesso i byte stanno in una riserva a parte con un contatore: stessa faccia a
sei corpi = un file in memoria. Slot 48. E `ex_font_trova(famiglia, corpo,
grassetto, corsivo)` toglie i percorsi scritti dentro i programmi.

## https: TLS 1.3 scritto qui dentro

! **NON C'ERA QUASI NIENTE DA INVENTARE, E QUELLO E' IL PUNTO.** X25519,
ChaCha20 e Poly1305 stanno in `lib/excrypt` dai tempi di sshd, provati contro
OpenSSL; SHA-256 lo da' la libc; HMAC, HKDF e RSA-PSS stanno in `lib/extls`;
il DER e la catena in `lib/exasn1` e `lib/excert`. `extls_client.c` e' solo
l'ORDINE in cui quelle cose si chiamano — che e' esattamente cio' che TLS 1.3
e', e anche il posto dove si sbaglia.

**Una sola strada, scelta apposta**: X25519, `TLS_CHACHA20_POLY1305_SHA256`,
`rsa_pss_rsae_sha256`. Ogni combinazione in piu' sarebbe un'altra
implementazione da provare, e una crittografia provata a meta' e' peggio di una
che non c'e' — la barra scrive `https://` lo stesso.

! **IL PREZZO E' DICHIARATO**: un sito che serve SOLO certificati ECDSA non si
apre (wikipedia.org, news.ycombinator.com: allarme 40). Serve la P-256, ed e' la
voce 1 di `in_lavorazione.txt`.

### I tre difetti, perche' nessuno era dove sembrava

1. **Il cursore della trascrizione.** La trascrizione e' UNA e ci vanno dentro
   tutt'e due le parti del dialogo: senza far avanzare `hs_off` anche sui
   messaggi che mandiamo NOI, il lettore ripescava il nostro ClientHello e lo
   trattava come la risposta del server. «Risposta che non e' TLS 1.3» su un
   server che aveva risposto benissimo.

2. **`testo_n >> 32` su un `unsigned int`.** Le due lunghezze in coda al dato
   di Poly1305 sono a 64 bit; scorrendo un numero a 32 bit oltre i 32 bit non
   si ottiene zero, si ottiene comportamento indefinito — e su x86 il
   processore usa solo i cinque bit bassi del conto, cioe' rifa' `>> 0`. La
   coda usciva `1d 00 00 00 1d 00 00 00` invece di `1d 00 00 00 00 00 00 00`.

3. **`hs_off` contro `trascr_n`, e questo si vedeva SOLO SU UN SITO VERO.**
   `trascr_n` e' quanto e' ARRIVATO, `hs_off` quanto e' stato LETTO. Coincidono
   finche' ogni record porta un messaggio solo — ed e' cosi' che si comporta un
   `openssl s_server` — ma un server vero impacchetta Certificate,
   CertificateVerify e Finished nello stesso record. L'impronta «fino al
   Certificate» comprendeva anche la firma: la firma non tornava mai.
   Banco di prova verde, `example.com` rosso.

! **E UN QUARTO NON ERA NEL TLS**: `tcp_leggi` di exhttp faceva `if (len > max)
len = max` e BUTTAVA il resto del pezzo. Con l'HTTP sopra non si vedeva mai —
l'HTTP chiede sempre un buffer grande — ma il record di TLS legge PRIMA cinque
byte e poi il resto: quei cinque arrivavano e tutto il resto del segmento
spariva. Adesso l'avanzo si tiene, ed e' cio' che rende quel trasporto un
FLUSSO invece di una fila di pacchetti.

### Come si prova

    make prova-cliente-tls     # contro un openssl s_server vero, 9 prove

Il caso buono non prova niente da solo: un cliente che accetta qualunque
certificato completa la stretta esattamente come uno corretto. Le prove che
contano sono le altre — nome sbagliato, radice sconosciuta, certificato
scaduto, jolly che non deve allargarsi a due etichette ne' al dominio nudo.

Dentro EX-OS: `scarica https://example.com/` (meno di otto secondi), e il
browser su `https://example.com/`.

## Spegnere non e' un privilegio amministrativo

`sys_reboot` era `solo_root`, ed era la risposta sbagliata alla domanda giusta.
Spegnere e' il gesto di chi ha il computer davanti — e su quella macchina c'e'
comunque il pulsante dell'alimentazione. Negarlo all'utente normale non protegge
niente: lo costringe a staccare la corrente, cioe' a saltare la
sincronizzazione del filesystem che quella syscall fa.

! **MA UNA SESSIONE REMOTA NON SPEGNE LA MACCHINA DI QUALCUN ALTRO**, ed e' la
distinzione che conta. Chi entra via telnet non ha il pulsante, non vede chi sta
lavorando alla console, e spegnendo interrompe il lavoro di una persona che non
ha chiesto niente.

! **IL CRITERIO E' IL DESCRITTORE 0, NON «DA DOVE VIENE LA CONNESSIONE»**. Una
console vera e' `FD_STDIN`; una sessione remota o un terminale in finestra
leggono da uno pseudo-terminale. Chiedere al descrittore invece che al
protocollo vuol dire che il giorno che arriva ssh la regola vale gia'.
root resta root: da remoto un amministratore spegne, perche' quello si' che e'
amministrazione.

Provato in tutt'e due i versi su disco ext2, con l'utente `prova` (uid 1001):
`poweroff` dalla console spegne davvero; `echo poweroff | sh` — stessa persona,
descrittore 0 che non e' una console — riceve il rifiuto con la spiegazione.

## Un processo che nasce non e' un processo bloccato

! **IL DUMP CHE NON SOMIGLIAVA A NIENTE.** Avviando da CD, ogni tanto il primo
driver di `/boot/avvio.sh` moriva cosi':

        [FAULT] PID 6 '/dev/pci.drv': page fault a 0x00000000
                (protezione, lettura, EIP=0x00000000)
        PF:   heap 0x08007000..0x08007000 (tetto 0xbffbe000),
              stack 0x00000000..0x00000000
        PF:   vma[0] 0x08000000..0x08004000  file+0x1000
        PF:   vma[1] 0x08004000..0x08007000  file+0x5000

Tre cose che insieme non stanno in piedi: **EIP=0** (il processo non ha mai
eseguito una sua istruzione), **stack 0x0..0x0** (non ne ha uno), e un **heap
con un tetto valido** piu' due VMA — cioe' i resti di un ALTRO programma
rimasti nello stesso slot.

! **LA FINESTRA E' FRA `proc_create()` E `proc_set_ready()`.** Un processo
utente si crea con entry=0, poi `elf_load` gira **con gli interrupt
abilitati** — deve, perche' legge dal disco — e solo alla fine arrivano entry
point e stack. In quella finestra il processo ha gia' un PID vivo e un nome, e
si chiamava `PROC_BLOCKED`.

! **E CHIUNQUE SVEGLI UN BLOCCATO LO FA PER PID.** `ipc_send` e `irq_notify`
in `kernel/ipc/ipc.c`, la lista di attesa del VFS in `kernel/fs/vfs.c`, i
`pid_att_in`/`pid_att_out` dei pty. Un PID appena riciclato in mano a uno di
questi, e il neonato parte: ring 3, EIP=0, nessuno stack. Il dump qui sopra,
esattamente.

Adesso quello stato e' **`PROC_NASCENTE`** (`kernel/include/sched.h`): non e' in
nessuna coda, `sched_unblock_locked` non lo trova, e l'unico modo di farlo
partire e' `proc_set_ready()` — cioe' chi l'ha creato, quando ha finito di
caricarlo. Un messaggio IPC per lui resta in cassetta e lo legge quando parte.

! **NON E' PROVATO CHE FOSSE QUELLA, E VA DETTO.** Il fault e' raro — tre volte
su una cinquantina di avvii — e **diciotto avvii senza la correzione non
l'hanno riprodotto**: la misura non distingue. Quello che si sa e' che la
strada esiste nel codice e che il dump che produrrebbe e' quello osservato.
Per questo `sched_unblock_locked` lascia un **[WARN]** quando qualcuno prova a
svegliare un nascente: se il difetto ricompare, il log dira' se e' quella
strada o un'altra. E' l'unico modo onesto di chiudere una caccia che non si e'
potuta concludere.

## Il motore degli script non ha piu' un tetto

Leggeva lo script **tutto in una volta** in `char buf[2048]` e troncava il
resto, con scritto accanto che leggerlo a pezzi sarebbe stata «complicazione
per un caso che non si presenta». Il caso si e' presentato al primo file di
avvio con dentro le sue spiegazioni.

! **UN TRONCAMENTO IN UN FILE DI COMANDI NON E' UNA RIGA PERSA: E' UNA RIGA
ESEGUITA A META'**, che e' un comando diverso. Qui il taglio cadeva su `echo
Rete pronta...` e la macchina stampava `Ret`. Poteva cadere altrove.

Adesso il buffer si ricarica: si consumano le righe intere, la coda incompleta
si sposta in testa, si legge il pezzo dopo. Stessa memoria, nessun tetto sul
file; il tetto resta sulla singola riga (MAX_LINE), e una riga piu' lunga del
buffer viene saltata dicendolo.

## I tre posti della rete, e perche' i primi due erano sbagliati

! **LA RETE E' STATA MESSA IN TRE POSTI DIVERSI IN DUE SETTIMANE**, e ognuno
falliva per una ragione che l'altro non aveva. Vale la pena tenerne il conto,
perche' e' la forma tipica dell'errore in un sistema con l'autenticazione: non
si sbaglia il COMANDO, si sbaglia CHI lo esegue.

**Primo posto: `autoexec.sh`.** Lo esegue la shell della prima console. Con
`login` acceso quella shell nasce a OGNI accesso e con l'identita' di CHI ENTRA:
i driver si riaccendevano a ogni rientro (una fila di `ipc_register` fallite con
-17) e non partivano affatto se il primo a entrare non era root — un utente
normale un driver non lo carica.

**Secondo posto: `[modules]` di `kernel.cfg`.** Il kernel li carica lui, una
volta e da root: giusto su chi, sbagliato su quando. **Le voci di `[modules]` il
kernel le avvia TUTTE INSIEME**, e la catena e' `pci` -> driver della scheda ->
`ip` -> `dhcp`, dove ogni anello deve trovare REGISTRATO il servizio di quello
prima. Partendo in parallelo ognuno si mette ad aspettare, e su una macchina
lenta quelle attese scadono. E' il difetto peggiore da cercare, perche' al
riavvio dopo funziona.

**Terzo posto: `/boot/avvio.sh`.** Lo esegue `login`, da root, prima
dell'accesso, sulla sola console 0, e aspetta che finisca. Prima dell'accesso e
da root e' cio' che manca al primo posto; l'ordine delle righe e' cio' che manca
al secondo.

! **SU CD E FLOPPY `login` NON C'E'**, perche' la radice non e' ext2: nessun
proprietario da far rispettare, e il kernel lancia direttamente la shell — da
root. Li' avvio.sh lo esegue la SHELL, prima dell'autoexec. Le due strade non si
incrociano mai perche' `login` mette **`EXOS_LOGIN=1`** nell'ambiente della
shell che avvia: trovarla significa «l'ha gia' fatto qualcuno». Togliere quel
segno riporta esattamente al difetto del primo posto.

! **E NEL RECOVERY LA RETE SI ACCENDE LO STESSO.** Quando `login` non si carica,
il kernel apre una shell di riparazione: `EXOS_LOGIN` non c'e', quindi
avvio.sh gira. Si ripara una macchina rotta avendo `ping` che funziona.

Contorno che serve sapere:

  - **`sh -f FILE`** esegue uno script e esce. E' cosi' che `login` chiama
    avvio.sh;
  - `hwconfig` scrive avvio.sh accanto a kernel.cfg e autoexec.sh, con il
    driver della scheda che ha trovato, e i due file spiegano dov'e' andata la
    rete;
  - **snprintf su un buffer da 256 byte tagliava il blocco a meta' di una
    frase**, e il file usciva monco senza dirlo — un troncamento non e' un
    errore, per snprintf. La riga di `componi_avvio` e' 1024 byte, col perche'
    scritto accanto;
  - niente backtick in cio' che si stampa: la riga usciva come «Ret».

**Come si prova.** Da disco ext2: `ipcfg` deve dare indirizzo, gateway e DNS
subito dopo l'accesso, e nel log seriale «Accendo la rete...» deve stare PRIMA
di «EX-OS - accesso». Uscendo e rientrando, «Accendo la rete» deve comparire UNA
volta sola. Da CD la catena e' la stessa, eseguita dalla shell — ma **serve
`-m 64M`**: a 32 MB `pci.drv` muore di page fault a EIP=0 appena avviato, ed e'
un difetto suo, non di questo lavoro.

## La coda, in ordine

! **LA CODA E' SOLO RIFINITURE**, e questo e' un fatto da guardare in faccia:
il prossimo lavoro grosso non e' scritto qui sotto. Va scelto da DIREZIONE.md,
non pescato dall'elenco. Le sezioni che c'erano — «cose che si vedono», «cose
che il toolkit ha chiesto tre volte», «cose che vogliono un pezzo di kernel
nuovo» — si sono svuotate fra il 17 e il 18 agosto e sono state tolte: i
lettori di immagini, il toolkit, i permessi, il dialogo «si'/no», le finestre
modali, il ridimensionamento, SSH, TSC e PSE.

### FATTO — LE TRE DEL 24 AGOSTO: accesso, kernel.cfg e la rete all'avvio

Erano le tre voci di `in_lavorazione.txt`, e hanno una radice sola: **`login`
ha cambiato chi possiede la prima console**, e tre pezzi scritti prima di lui
sono rimasti indietro.

#### `exit` chiudeva la console dietro di se'

Entrare come utente, `exit`, riprovare: «Accesso non riuscito» con qualunque
password, per sempre. Da root no. Il difetto stava in una parola: **login e' un
CICLO**, e per lanciare la shell scendeva con `setuid()`. Dopo, quel processo
E' l'utente — e `/boot/ombra` e' 0600 di root, quindi al giro dopo non poteva
nemmeno leggerlo. Da giu' non si torna su: EX-OS non ha il bit setuid sui file.

! **LA CORREZIONE NON E' RIMEDIARE DOPO, E' NON SCENDERE.** `spawn()` ha
imparato `SPAWN_F_UTENTE`: il figlio nasce con l'identita' che gli si dice, e
**solo un processo gia' root puo' chiederlo**. Chi non lo e' prende `EPERM` e
non parte niente — non «parte come prima», che sarebbe il modo silenzioso di
aprire una shell con i privilegi di un altro.

! **LA MAGIA DEL BLOCCO EXTRA E' PASSATA A `'SPO0'` (596 -> 604 byte): SI
RICOMPILA TUTTO.** Un binario vecchio passa `'SPNZ'`, il kernel non lo
riconosce e lo IGNORA, cioe' perde redirezioni e ambiente senza dirlo. E' gia'
successo il 14 agosto.

! **IL CASO PARTICOLARE E' CIO' CHE HA TENUTO NASCOSTO IL DIFETTO**: `if (uid
!= 0)` saltava il setuid per root, e root e' l'utente con cui si prova.

#### `hwconfig` toglieva `login` da kernel.cfg

Riscrive il file per intero, ed e' giusto — rispecchia l'hardware, e l'hardware
lo ha appena guardato. Ma il kernel lancia `/bin/login` **solo se la riga c'e'**
(PASSO 15): un `hwconfig` su una macchina installata la toglieva, e al riavvio
dopo si entrava senza password. Ora `login` e `svga` si riportano avanti — se
c'erano restano com'erano, e `login` si aggiunge anche se mancava. Le righe
commentate non contano come voci, e la sezione si guarda.

#### La rete non e' roba da `autoexec.sh`

L'autoexec lo esegue la shell della prima console: con `login` in mezzo nasce a
**ogni accesso** e con l'identita' di chi entra. I driver si riaccendevano a
ogni rientro, e se il primo a entrare non era root non si accendevano affatto.
Sono passati in `[modules]` di kernel.cfg, dove li carica il kernel: una volta,
da root, prima di qualunque console.

! **MA IL KERNEL NON METTE I MODULI IN FILA: LI AVVIA TUTTI INSIEME.** In uno
script l'ordine lo garantisce chi lo scrive; li' no, e un driver che chiede il
proprio fornitore mezzo secondo troppo presto ESCE — e all'avvio non lo
rilancia nessuno. A metterli in fila e' `ipc_attendi()`: la scheda aspetta il
bus, lo stack aspetta la scheda, `dhcp` aspetta lo stack.

! **E LA TABELLA «SCHEDA -> DRIVER» SI ERA SFASATA DA SOLA, SENZA NESSUNA
COPIA.** Stava dentro `netdetect.c` con accanto scritto di non duplicarla; poi
e' stato scritto `/dev/e1000.drv` e la riga `8086:100E` e' rimasta a «driver da
scrivere» — su QEMU, dove quella scheda e' la predefinita, `netdetect -c`
diceva che il driver non c'era mentre stava nel CD accanto. Ora sta in
`lib/rete.c` e la leggono in due. **Il difetto non era la copia: era che
l'elenco stesse dentro un programma.**

#### Come si e' provato

Disco ext2 vero (`make hd`, root/root e mario/mario), non un ragionamento:
mario entra, `id` da' `uid=1000`, `exit` torna al prompt, mario rientra, root
entra dopo di lui, una password sbagliata e' ancora rifiutata, `sudo id` da'
`uid=0`. `hwconfig -n` mostra `login = /bin/login` sia sul file
dell'installatore sia su quello che ha scritto lui (idempotente). Riavviato: la
catena si accende da sola e `ipcfg` mostra 10.0.2.15 preso dal DHCP. Il CD
accende la rete da solo. `libctest` dava 315 su 316, e la riga rossa era la
PROVA rimasta indietro, non il sistema: vedi la sezione su `rename`. Adesso
321 su 321.

! **`tools/mkhd.sh` NON RISPONDEVA PIU' ALL'INSTALLATORE** da quando `install`
chiede i due conti (19 agosto): restava fermo su «password di root:» e finiva
con «l'installazione non e' arrivata in fondo» — un messaggio che accusa
l'installatore mentre il difetto era nello script. Senza correggerlo non
c'era nessun disco su cui provare l'accesso.

! **E `boot/autoexec.sh` HA UN TETTO DI 2 KB**: la shell lo legge in una volta
sola e tronca il resto. Aggiungendoci nove righe di commento il file e' passato
a 2230 byte e la riga `dhcp` e' finita oltre il taglio — la rete del CD si
fermava allo stack, in silenzio. L'avviso c'e' («script piu' lungo di 2 KB»),
ma scorre via nell'avvio.

### LA MAGIA DEL BLOCCO EXTRA DICE LA FORMA, NON «CAPISCO / NON CAPISCO»

! **LA 0.203 AVEVA ROTTO IL `gcc` CHE GIRA DENTRO EX-OS, e non se ne accorgeva
nessuno.** Per far nascere la shell con l'identita' dell'utente, `SpawnExtra`
ha preso due campi e la magia e' passata da `'SPNZ'` a `'SPO0'`. Regola scritta
in `spawn_abi.h` e rispettata alla lettera — solo che «il kernel ignora un
blocco che non riconosce» vuol dire che ogni binario compilato contro la libc
del 14 agosto parte **senza redirezioni e senza ambiente**. In silenzio.

E i binari compilati contro quella libc sono tutto il CD degli strumenti. Il
driver `gcc` redirige l'uscita di `cc1` su un file temporaneo: da quel commit
quell'uscita finiva a video, e la compilazione dentro EX-OS non arrivava in
fondo. Il difetto e' uscito da `make abi`, che e' il guardiano scritto apposta
il giorno in cui la stessa cosa era gia' successa.

! **BUMPARE LA MAGIA BUTTANDO VIA LA VECCHIA TRASFORMA UN MECCANISMO DI
COMPATIBILITA' IN UNA ROTTURA.** La magia esisteva per non far leggere ESI ai
programmi della forma a tre argomenti — «la vecchia forma continua a funzionare
esattamente come prima», dice `syscall.h`. Fra una forma del blocco e l'altra
quella promessa non c'era.

Adesso la magia dice **quale forma**, e il kernel le conosce tutte: legge tanti
byte quanti ne dichiara, azzera i campi che quella forma non aveva, e **spegne
i bit di `flag` che allora non volevano dire niente** — senza quest'ultima
riga, un programma vecchio con il bit 0x2 acceso per caso farebbe nascere un
figlio con l'identita' che capita.

! **E LA VERIFICA DEL PUNTATORE ERA DELLA MISURA SBAGLIATA**: 604 byte
leggibili chiesti a un blocco che ne ha 596. Un chiamante legittimo la cui
struttura finisce a ridosso di una pagina veniva rifiutato — una volta ogni
mille, sul programma sbagliato. Adesso si legge la magia (quattro byte), e da
quella si sa quanti verificarne.

! **LA PROVA MANDA AL KERNEL LA FORMA DEL 14 AGOSTO**, con una copia CONGELATA
della struttura di allora e la syscall chiamata a mano — `spawn_ex()` non
serve, manderebbe sempre la forma di adesso. Verificata anche al contrario:
togliendo il ramo `'SPNZ'` dal kernel, `spawn` riesce lo stesso e la
redirezione sparisce, che e' esattamente il modo silenzioso in cui il difetto
si era presentato.

#### E `login` adesso e' statico PERCHE' QUALCUNO LO CONTROLLA

`login`, `install`, `sh` e tutti i driver non collegano la libc condivisa: sono
i programmi con cui si ENTRA, si RIPARA e si AVVIA, e dipendere da
`/lib/libc.so` vuol dire non poter entrare proprio il giorno che quel file e'
rotto — che e' il giorno in cui serve.

! **ERA VERO SOLO PERCHE' LE REGOLE DEL Makefile ERANO SCRITTE COSI'.** Bastava
copiare la regola sbagliata mentre si aggiungeva un programma, e il sistema si
sarebbe avviato benissimo fino al giorno sbagliato. `make verifica-statici`
guarda dentro i binari — `__libc_ponti_tabella` c'e' solo in chi e' collegato
ai ponti — e ferma la build. Gira dentro `make all`.

! **E SI GUARDA IL SIMBOLO, NON IL NOME DELLA REGOLA**: il nome dice cosa
volevamo, il simbolo dice cosa e' venuto fuori.

### IL GUARDIANO DELL'ABI DICE COSA E' CAMBIATO

`make abi` confronta la forma dei tipi che libc e programmi si scambiano con
quella registrata nel sysroot all'ultima ricostruzione completa. Era **un solo
sha256**: quando non combaciava poteva dire soltanto «qualcosa e' cambiato,
ricostruisci tutto» — ore di macchina chieste senza dire per cosa.

! **E UN GUARDIANO CHE RESTA ROSSO SI IMPARA A SALTARE.** Il 24 agosto la sola
forma cambiata era `SpawnExtra` (596 -> 604), che il kernel adesso gestisce da
solo: il rosso diceva una cosa piu' forte del vero, e sarebbe rimasto li' per
giorni. E' esattamente cio' che era successo la stessa settimana con la riga
`[FALLITO]` di `rename` in libctest — quattro giorni sotto gli occhi di
nessuno, perche' un avviso che non si puo' verificare in dieci secondi diventa
rumore. E allora la volta che ha ragione non lo sente nessuno.

Adesso l'impronta e' un ELENCO: una riga per forma, con la misura in decimale.
Quando non combacia si stampano le sole righe che differiscono, con il valore
registrato e quello di adesso; le forme sparite e quelle nuove si vedono
etichettate. Gli `abi_off_*` portano offset + 1 (vedi `CAMPO` in
`tools/abi-bersaglio.c`) e il numero si ritoglie prima di stamparlo, o il
messaggio direbbe una posizione che nel sorgente non esiste.

! **E I SYSROOT GIA' IN GIRO SI CONTINUANO A LEGGERE.** Un `.abi-libc` scritto
prima del 24 agosto e' un solo hash: `--verifica` lo riconosce (una riga, 64
esadecimali), lo confronta come si faceva prima, e se e' rosso lo dice
chiaramente — «l'impronta registrata non puo' dire QUALE forma e' cambiata,
dopo la prossima ricostruzione lo dira'». Cambiare formato a un file di stato
non deve mandare a ricostruire tutto per il formato.

Provato nei quattro casi che contano: uguale (verde, con il numero di forme
confrontate), una misura cambiata, una forma sparita, una forma nuova.

### `login -a` DICEVA IL FALSO, E SOTTO C'ERA DI PEGGIO

Il difetto dichiarato era il cartello: `login -a` su una macchina piena di
utenti annunciava «Sistema nuovo: non c'e' ancora nessun utente», perche'
riusava la funzione del primo avvio. Il comportamento era giusto — l'uid lo
sceglie `exuser_prossimo_uid()` — ma il messaggio no, e **un messaggio falso e'
peggio di uno assente**: chi lo legge si chiede cosa sia successo all'archivio.
Adesso la funzione prende un parametro e dice quale delle due cose sta facendo.

! **MA GUARDANDO QUELLA STRADA NE E' USCITA UN'ALTRA: UN NOME GIA' PRESO NON
VENIVA RIFIUTATO.** `aggiungi_utente()` AGGIUNGE una riga in fondo a
`/boot/utenti` e a `/boot/ombra`; `cerca_riga()` prende la PRIMA che combacia.
Quindi `login -a mario` su un `mario` che c'e' gia' scriveva una seconda coppia
di righe, stampava «Utente 'mario' creato» — e vinceva la vecchia: **la
password nuova non funzionava, quella vecchia si', e l'uid restava quello di
prima**. Il programma diceva di aver fatto una cosa e ne aveva fatta un'altra,
che e' peggio di un rifiuto perche' chi legge non ha motivo di controllare.

! **IL CONTROLLO STA DENTRO `aggiungi_utente()`, non nei chiamanti.** I
chiamanti sono tre — il primo avvio, `login -a`, l'installatore — e un
controllo ripetuto tre volte e' un controllo che prima o poi ne ha due. Rende
`-2`, che NON e' `-1`: «c'e' gia'» e «non riesco a scrivere» mandano a cercare
in due posti diversi, e dirli con lo stesso messaggio manda nel disco un
problema che sta nell'archivio.

! **E CAMBIARE UNA PASSWORD NON E' QUESTO.** Riscrivere una riga in mezzo a un
file vuol dire rifarlo per intero: e' un'altra funzione, il giorno che serve.
Sovrascrivere qui vorrebbe dire che si cambia la password di qualcun altro
digitandone il nome per sbaglio.

#### E IL BANNER D'ACCESSO ERA TRE GLIFI A CASO

`printf("  EX-OS — accesso")` — con il trattino lungo UTF-8. La console
indicizza il font per BYTE (`font8x16[ch * CELLA_H]`), quindi quei tre byte
diventano tre glifi presi da dove capita, **su ogni accesso, sulla prima cosa
che si vede**. Il kernel aveva reso ASCII le proprie stringhe il 19 agosto per
questa identica ragione; nessuno aveva guardato i programmi. Le tre di `login`
sono a posto; le altre cinquantasette sono nell'elenco dei difetti aperti.

#### Come si e' provato, e cosa manca

Il rifiuto del doppione e' provato sul CODICE VERO estratto in un banco per
l'host — `perc`, `cerca_riga`, `trova_utente` compilati dal sorgente, non
riscritti — su sette casi: i tre nomi presenti, un prefisso (`mari`), una
sovra-stringa (`marioo`), un nome libero e uno con la maiuscola. Il prefisso e
la sovra-stringa sono l'errore classico di un controllo d'accesso, e non c'e'.

! **QUELLO CHE MANCA E' IL GIRO DENTRO EX-OS** — `login -a` battuto davvero,
con il cartello giusto e il rifiuto a video — e manca perche' la macchina sta
ricostruendo il bersaglio: provarlo adesso vuol dire contendersi la CPU con
`cc1` e leggere tempi che non sono quelli veri.

### NOVANTATRE STRINGHE CHE SULLA CONSOLE NON SI LEGGEVANO

La console disegna un carattere per BYTE: `font8x16[ch * CELLA_H]`
(kernel/arch/x86/vga.c), nessuna decodifica UTF-8. Un trattino lungo sono TRE
byte, quindi tre glifi presi da dove capita. Il kernel aveva reso ASCII le
proprie stringhe il 19 agosto — voce 17 del changelog di `version.h` — e li' si
era fermato: **nessuno aveva guardato i programmi**, che sulla stessa console
scrivono molto piu' del kernel.

Fotografato prima e dopo, sulla stessa schermata di `hwinfo`:

    prima:   assurdo ZCO sul controller ATA vuol dire perdere
    dopo:    assurdo -   sul controller ATA vuol dire perdere

! **93, NON 57.** Il primo conto veniva da un `grep` sulle righe che contengono
`printf`, e perdeva tutto cio' che sta nelle righe di continuazione e nelle
tabelle di dati — `hwinfo` da solo ne aveva 23, quasi tutte in due elenchi di
modelli. Il conto giusto lo da' un piccolo tokenizzatore che salta i commenti e
guarda solo i letterali: contare con `grep` cio' che ha una struttura e' come
cercare una parentesi con una regola.

! **E I COMMENTI NON SI TOCCANO**: 1110 trattini lunghi restano dove stanno,
perche' i commenti non li stampa nessuno. E' la ragione per cui la sostituzione
e' passata da un tokenizzatore invece che da `sed`: `sed` non sa la differenza
fra una stringa e la prosa che le sta accanto.

Tre sostituzioni: `—` diventa `-`, `«»` diventano apostrofi — la forma che il
kernel usa gia' per i nomi, `'nome'` — e l'unica lettera accentata diventa
`a'`, come si scrive nel resto del progetto.

! **E UNA TABELLA SI E' PURE RADDRIZZATA.** `netdetect` stampa i modelli con
`%-44s`, che conta BYTE: le righe con il trattino lungo erano sfalsate di due
caratteri rispetto alle altre. Il commento accanto alla tabella lo diceva gia',
ed era rimasto una nota invece di una correzione.

### LE LETTERE ACCENTATE SEMBRAVANO LETTERE CON UNA MACCHIA SOPRA

Segnalato guardando lo schermo: `àèìòù` non somigliavano alle lettere
accentate, ma a lettere normali con un accento strano. Guardando i bit del font
si vede subito perche':

    prima                     adesso
    ..##....   riga 2         ........
    ....##..   riga 3         ..##....   riga 3
    ........   riga 4         ...##...   riga 4
    ........   riga 5         ........
    ..####..   riga 6         ..####..   il corpo della lettera

Due cose insieme. **Il segno stava DUE righe sopra la lettera** — mentre il
punto della `i`, nello stesso font, ne sta una sola: l'occhio lo legge come un
segno che galleggia, non come parte della lettera. E **il grave e l'acuto erano
una scaletta spezzata** su quattro colonne (`..##....` poi `....##..`): a otto
pixel di larghezza due quadratini staccati si leggono come due macchie, non
come un accento. Adesso sono un tratto continuo su tre colonne.

28 glifi: le minuscole accentate e l'anello di `å` scendono di una riga; le
maiuscole con segno (`Ä É Ö Ü Ñ`) pure, perche' il loro corpo comincia gia'
alla riga 4. `Å` resta com'e': il suo anello arriva alla riga 3 e scendendo
toccherebbe la lettera.

! **VERIFICATO SULLO SCHERMO, NON SUI BIT.** La prima prova e' passata
mostrando i glifi VECCHI, e non perche' la modifica fosse sbagliata: avevo
ricostruito il kernel e non l'immagine del floppy, quindi QEMU avviava quello
di prima. I pixel si sono letti dal framebuffer — `keymap it`, i cinque tasti
accentati, screendump, e le celle 8x16 stampate come testo — invece di
fidarsi di una compilazione riuscita.

! **E IL COMMENTO IN TESTA AL FONT DICEVA IL FALSO:** «l'ordine e' Latin-1, non
CP437», con tanto di spiegazione del perche' sarebbe stata una scelta. I dati
sono sempre stati CP437: il byte 0xC0 disegna un angolo di cornice e la `a`
accentata e' 0x85, non 0xE0. Ed e' CP437 che ci vuole, perche' in modo TESTO i
glifi li tiene la scheda video e quelli sono CP437 per costruzione — se il font
grafico fosse Latin-1, la stessa lettera comparirebbe in due posti diversi a
seconda della modalita'. La tastiera lo dichiara gia' (`drivers/kbd/keymaps.h`):
erano d'accordo font e tastiera, era il commento a essere sbagliato. **Chi si
fosse fidato di quella riga avrebbe "corretto" la tastiera e rotto ogni tasto
accentato.**

! **E CHI RIGENERA IL FONT DAL .psf PERDE QUESTO LAVORO.** Sta scritto adesso in
testa al file, accanto al comando che lo rigenera: era il posto dove serviva.

### OGNI STRUMENTO HA LA SUA VERSIONE, E LA DICE CON `-v`

Chiesto: una versione per programma, +0.001 a ogni modifica, `-v` da riga di
comando e «Informazioni su» nelle applicazioni. Fatto per **59 programmi** —
tutto `bin/` e tutto `exwin/bin/` — piu' la shell, che va per la sua strada.

! **L'OPZIONE E' `-version`, PER ESTESO, E NON `-v`.** Una lettera sola era
gia' presa da tre programmi con tre significati diversi — «parla di piu'» in
`sshd` e `telnetd`, «visualizza il file» in `textline` — e una regola che vale
per tutti tranne tre non e' una regola. Con la parola intera non si sovrappone
a niente, nessun programma cambia le proprie opzioni, e chi legge `-version` in
uno script capisce cosa chiede senza andare a vedere il programma.

! **IL PROGRAMMA NON GUARDA `argv`, E NON DEVE.** La domanda «che versione
sei?» e' identica per tutti: cinquantanove `if (strcmp(argv[i], "-version"))`
sono cinquantanove occasioni di scriverla in modo diverso — una stampa il nome,
una no, una esce con 0 e un'altra con 1, tre si dimenticano di metterla nella
pagina d'uso. Risponde **l'avvio della libc** (`lib/libc_avvio.c`), che vede
argv prima di `main`. Il programma dichiara una riga:

    EX_VERSIONE("browser", "0.001");

! **I DUE SIMBOLI SONO DEBOLI, E QUESTO E' CIO' CHE LO RENDE INNOCUO.** Chi non
usa la macro non ha `-version`: il codice di terzi che si collega alla nostra
libc — `make`, `gcc`, `fbc`, che un `--version` loro ce l'hanno — non si accorge
di niente, perche' un simbolo debole non definito vale zero.

! **E DEV'ESSERE L'UNICO ARGOMENTO.** `prog -version` e' una domanda;
`prog -version qualcosa` e' un comando malformato, e rispondergli vorrebbe dire
far finta che sia una domanda.

! **LA SHELL SE LA STAMPA DA SE'**, e non e' un'eccezione capricciosa: `sh` non
usa la libc — le syscall se le scrive — quindi quell'avvio li' non c'e'. Stessa
forma di risposta. E la prima versione della riga tornava con `return 0`: la
console moriva con «eccezione 13 a EIP=0x08000014», perche' da `shell_main` non
si torna da nessuna parte — `start.S` mette uno zero come indirizzo di ritorno
finto. Si esce con `sh_exit()`.

**Nelle applicazioni** la versione sta in «Informazioni su», sulla riga del
nome — «File manager 0.001» — dove non costa niente: il dialogo mostra dodici
righe, e una riga in piu' sarebbe una riga in meno di descrizione. `exinfo_testo`
prende la versione come parametro, e le tre applicazioni che hanno il dialogo le
passano la **stessa macro** che stampa `-v`: due letterali uguali diventano due
letterali diversi al primo incremento, e allora la finestra e la riga di comando
direbbero due versioni dello stesso programma.

**Provato**: `ls -version`, `sh -version`, `textline -version`, `sudo -version`
dentro EX-OS, e `textline <file> -v` che continua a voler dire «visualizza»; e
«Informazioni su» del file manager fotografato sulla scrivania, aprendo il menu
col mouse.

! **E LA REGOLA ADESSO SI CONTROLLA**: `make verifica-versioni` — dentro
`make all` — ferma la build se un programma non dichiara la propria versione.
Non controlla che sia stata INCREMENTATA: quello lo sa solo chi ha scritto la
modifica. Ma il numero di versione del kernel e' rimasto fermo per tredici
commit proprio perche' nessuno guardava, e questo e' il pezzo che si puo'
guardare a macchina.

**Anche i driver**, dal 24 agosto: dodici, e si chiamano col nome che si digita
— `/dev/kbd.drv -version` risponde «kbd.drv 0.001». Fuori restano `hello`
(l'esempio del programma senza libc), `floppy.drv` (driver dinamico, non ha un
`main`) e `net`, `tty`, `usb`, che non sono programmi ma codice condiviso.

**E le quattro applicazioni che non avevano «Informazioni su» adesso ce
l'hanno**, ognuna dove ha senso:

  - `term` e `fontprova` hanno preso una barra dei menu con «Info» —
    e con lei venti pixel in meno di area utile, che vanno tolti a mano: il
    toolkit la mette in cima e larga quanto la finestra, ma il posto glielo
    deve lasciare il programma. In `term` il conto va rifatto anche nel
    ridimensionamento, o la griglia cresce oltre il bordo di sotto;
  - la **scrivania** ce l'ha nel menu «Avvio», sotto la riga che separa le
    applicazioni da cio' che spegne le cose: non ha una barra dei menu dove
    metterla — la sua barra e' quella delle finestre — e quello e' l'unico menu
    che ha. Provata col mouse: «Scrivania 0.001»;
  - l'**orologio** con un clic, e questa e' l'unica eccezione: e' alto VENTI
    PIXEL e non ha nemmeno la barra del titolo. Una barra dei menu sarebbe piu'
    alta dell'orologio — rispettare la forma della regola ne romperebbe il
    senso. E il clic e' l'unico gesto che quella finestra puo' ricevere: il
    fuoco della tastiera non lo prende.

#### E IL PRIMO `make -j2` DOPO IL CAMBIO E' FALLITO IN UN POSTO CHE NON C'ENTRAVA

«undefined reference a `printf`» collegando `/bin/chmod`. Il difetto era in una
riga di Makefile vecchia di mesi, che si vede solo quando i ponti si rigenerano:

    $(GEN_ESPORTA) $(GEN_PONTI): $(LIBC_SO_OBJ) tools/genlibc.py

! **DUE BERSAGLI E UNA RICETTA SONO DUE REGOLE, NON UNA.** GNU make la legge
come «per fare A esegui questo» e «per fare B esegui questo»: con `-j2` le due
partono INSIEME, e la seconda riscrive i due file mentre la prima li sta gia'
leggendo. Il risultato era un `.S` da 62 KB assemblato in un `.o` da **280 byte
senza un simbolo**, e il guasto compariva molto piu' in la', in un programma che
con i ponti non c'entra niente. Si scrive `&:` (make 4.3 in avanti), che dice
«questa ricetta produce tutt'e due, eseguila una volta sola».

! **E IL PRIMO ERRORE L'AVEVO NASCOSTO IO**, filtrando l'uscita di make con un
grep che cercava «error:» ed «Errore»: make scrive «Error 1». La build era
rossa e io leggevo la riga verde di `verifica-versioni` che le stava sopra.

### IL BERSAGLIO E' RICOSTRUITO, E LA PROVA E' UN PROGRAMMA CHE GIRA

Il giro completo di `tools/ricostruisci-bersaglio.sh` — libc, gcclibs, libm,
libgcc, binutils, cc1, openssl, i due CD — e' finito. `make abi` e' verde, e
adesso lo dice contando: «69 forme confrontate, nessuna cambiata».

Ma il verde di un guardiano non e' una prova. Questa lo e', dentro EX-OS su
disco ext2, con il CD degli strumenti nel lettore:

    /cdrom/exos/bin/gcc /cdrom/prova-gcc.c -o /root/prova
    /root/prova
        La catena intera dentro EX-OS
          somma dei quadrati 1..10 : 385   (atteso 385)
          lunghezza del nome       : 5     (atteso 5)
          divisione a 64 bit       : 64   (atteso 64)
        Compilato, assemblato e collegato qui dentro.

! **E' ESATTAMENTE CIO' CHE LA 0.203 AVEVA ROTTO.** Il driver `gcc` spawna cc1
e as REDIRIGENDO l'uscita su file temporanei: con la magia del blocco EXTRA
cambiata e la vecchia buttata via, quel blocco veniva ignorato e l'uscita di
cc1 finiva a video. Adesso il kernel capisce tutt'e due le forme e la catena
gira — con i binari nuovi, che mandano 'SPO0'.

#### L'ALBERO DI OpenSSL AVEVA 822 FILE SVUOTATI, E NON LO SAPEVA NESSUNO

La ricostruzione si e' fermata sull'ultima fase, il CD degli strumenti:
`provassl` non si collegava piu'. Il motivo non era la ricostruzione.

! **822 file di `openssl/` erano di ZERO BYTE dal 3 agosto** — `crypto/aes/*.c`,
`crypto/sm3/*`, `crypto/slh_dsa/*`, `crypto/x509/*` e i `build.info` di quindici
directory. Nome giusto, data giusta, dentro niente: un'estrazione andata a
meta'. `openssl/` e' fuori dal repository (`.gitignore`), quindi e' stato
danno locale silenzioso.

! **NON SE N'ERA ACCORTO NESSUNO PERCHE' `libcrypto.a` NON VENIVA PIU' RIFATTO
DA ZERO.** L'archivio conteneva ancora gli oggetti del 5 agosto, compilati
quando i sorgenti erano interi; l'elenco dei membri era vecchio e giusto, i
sorgenti nuovi e vuoti. La ricostruzione ha rifatto l'archivio dall'elenco di
ADESSO e i pezzi mancanti sono venuti a galla: **813 membri prima, 957 dopo**.
Cioe' il giro completo non ha rotto niente — ha scoperto una cosa gia' rotta,
che e' precisamente il suo mestiere.

Riparato dalla copia intatta che stava gia' sul disco (`~/Scaricati/openssl`),
confrontando i due alberi file per file: 819 differenze, 818 delle quali file
vuoti da noi, e la 819esima e' `Configurations/50-exos.conf`, che e' nostro e
la' non c'e'. Gli unici quattro rimasti vuoti si chiamano `empty.txt` e
`smcont_zero.txt`: sono vuoti di mestiere.

### IL BROWSER SU UNA PAGINA VERA: tre difetti trovati GUARDANDO, non pensando

Chiesto: rendere il browser capace di mostrare una pagina web moderna. Il primo
passo non e' stato aggiungere niente — e' stato **servire una pagina vera** da
questa macchina (Hacker News, con il suo CSS) e fotografare cosa succede.

Tre difetti, tutti visibili nella prima fotografia:

#### 1. UTF-8: «RISC-V â» dove c'era un trattino lungo

! **IL DISEGNO DEL TESTO PRENDEVA UN BYTE PER VOLTA**, e tutto il web di oggi e'
UTF-8: i tre byte di un trattino lungo diventavano tre glifi presi da dove
capita. Adesso `ex_scrivi_con` e `ex_larghezza_testo` decodificano UTF-8 —
tutt'e due, con lo stesso decodificatore, perche' se la misura conta i byte e
il disegno conta i caratteri l'impaginazione e il disegno non sono piu'
d'accordo e il difetto sembra dell'impaginazione.

! **MA LE STRINGHE DI EX-OS NON SONO UTF-8**: la tastiera emette CP437, la `a`
accentata e' il byte 0x85. La regola percio' e' INDULGENTE, come quella di un
browser vero: **sequenza valida si decodifica, byte isolato vale per se'**. Un
byte CP437 non forma quasi mai una sequenza valida, e le due cose convivono
senza che nessuno debba dichiarare la codifica.

! **E IL FONT DI SISTEMA HA 256 GLIFI**, quindi per lui si SCEGLIE cosa perdere:
le accentate ci sono davvero (tabella verso CP437), i segni tipografici del web
— trattini lunghi, virgolette curve, puntini — diventano il loro parente ASCII.
Meglio un trattino corto di tre glifi a caso. Col TrueType invece il codice
passa dritto alla cmap, e Liberation ce li ha tutti.

**Vale per tutte le applicazioni**, non solo per il browser: l'editor che apre
un file UTF-8 adesso lo legge.

#### 2. La pagina era tutta centrata

Hacker News chiude tutto dentro un `<center>` per centrare la TABELLA. Il
`text-align` ereditava fin dentro le celle, e ogni titolo finiva in mezzo alla
colonna: si leggeva, ma sembrava scritto da un ubriaco.

! **UNA TABELLA NON EREDITA L'ALLINEAMENTO DA CHI LA CONTIENE**, ed e' la regola
dei browser veri, non una nostra invenzione: l'effetto di `<center>` si ferma al
bordo della tabella, proprio perche' quel modo di centrare e' vecchio quanto il
web. Una riga nel foglio predefinito — `table, td { text-align: left }` — e chi
vuole centrare una cella lo dice sulla cella, vincendo per cascata.

#### 3. Gli a capo non erano spazi

Fuori da `<pre>` si guardava solo `' '`. L'HTML fra un tag e l'altro va a capo
di continuo: quei nodi di testo fatti di un solo a capo non avanzavano la penna
e diventavano parole vuote. E una sequenza di bianchi vale **uno** spazio solo —
prima ognuno ne aggiungeva uno, quindi il sorgente indentato apriva buchi larghi
quanto il rientro.

Provato su una pagina scritta apposta: `uno due tre — quattro` da tre elementi
separati da a capo, `molti spazi nel sorgente` collassati, e
`perché città può — «virgolette» … → fine` tutto giusto.

#### Cosa manca ancora, in ordine di quanto pesa

 1. **`https`**, ed e' il muro: quasi ogni sito vero oggi e' solo TLS. `exbig`,
    `exasn1`, `excert` e i tre mattoni di `extls` ci sono; mancano il record e
    l'handshake.
 2. **I tetti**: 512 KB di pagina e 4096 nodi. Wikipedia ne vuole 676 KB e
    l'albero si tronca — il browser lo DICE («pagina troncata», «albero
    troncato»), che e' il modo giusto di fallire, ma la pagina non si vede
    intera. Un nodo pesa 32 byte: alzarli e' un conto di memoria da fare in
    faccia, non di codice.
 3. **I margini sugli elementi in linea**: `margin-right` su uno `<span>` non si
    applica, ed e' cosi' che i siti separano le voci di un menu — l'«Hacker
    Newsnew» della fotografia NON era un difetto degli spazi: li' spazi non ce
    ne sono, c'e' un margine.
 4. **Lo sfondo delle celle**: la barra arancione di HN non si dipinge.
 5. Moduli, `colspan`/`rowspan`, JavaScript: dichiarati fuori.

### FATTO — HTTPS DENTRO EX-OS, con la libssl di OpenSSL

    /cdrom/bin/provatls example.com 443 /

    provatls: example.com e' 172.66.147.243, porta 443
    provatls: TCP aperta (id 1)
    provatls: 150 certificati nel magazzino
    provatls: magazzino /cdrom/exos/ssl/certi.pem, il certificato SI VERIFICA
    provatls: HANDSHAKE FATTO
              protocollo TLSv1.3
              cifrario   TLS_AES_256_GCM_SHA384
              soggetto   /CN=example.com
              emittente  /C=US/O=SSL Corporation/CN=Cloudflare TLS Issuing ECC CA 3
              verifica   OK (0)
    --- risposta ---
    HTTP/1.1 200 OK
    ...
    --- 868 byte in chiaro ---

! **E LA VERIFICA NON E' UN TIMBRO**: contro un server TLS locale con un
certificato autofirmato, lo stesso programma dice

    provatls: handshake fallito
              error:0A000086:SSL routines::certificate verify failed

#### La nota che diceva «libssl non si costruisce» era falsa da tre settimane

`ricostruisci-bersaglio.sh` spiegava che si costruisce solo `libcrypto.a`
perche' «ssl/rio di OpenSSL 4.x vuole fd_set e il polling sui socket». Era vero
su un albero dei sorgenti a cui mancavano **822 file** — e fra quelli c'era
proprio `ssl/rio/build.info`. Riparato l'albero, `libssl.a` si costruisce
**senza un errore**, 53 oggetti, 900 KB.

! **E I SOCKET NON SERVIVANO DAVVERO.** OpenSSL vuole leggere e scrivere byte,
non un descrittore: con due BIO di memoria il facchinaggio lo fa il programma —
cio' che la libreria vuole spedire si manda con `IP_MSG_TCP_INVIA`, cio' che
arriva glielo si mette dentro. E' la strada documentata, non un aggiramento, ed
e' quella che usa chiunque metta TLS sopra un trasporto che non e' un socket.

#### Il difetto che e' uscito, e che non e' stato aggirato in silenzio

`SSL_CTX_load_verify_locations` sul magazzino del CD rende

    error:05880020:x509 certificate routines::BIO lib

Il file c'e', `ls` lo vede, `cp` lo copia — ma il **BIO di file** di OpenSSL non
ci arriva. E' un difetto della nostra stdio (o di cosa quel BIO si aspetta da
`fseek`/`ftell`), sta in `in_lavorazione.txt`, e nel frattempo `provatls` legge
il magazzino con `open`/`read` e lo carica da un BIO di memoria — con il perche'
scritto accanto. **Un aggiramento che nessuno scrive diventa il motivo per cui,
fra sei mesi, «openssl non legge i file» sembra normale.**

#### E allora `extls` a cosa serve?

! **A far navigare il BROWSER, che sta sul CD di sistema.** `libcrypto.a` piu'
`libssl.a` sono sei megabyte e stanno sul CD degli STRUMENTI, che ne pesa 167:
il browser vive sui nove del CD di sistema, e li' dentro OpenSSL non ci sta.
`exbig`, `exasn1`, `excert` ed `extls` insieme sono qualche decina di
chilobyte.

! **MA ADESSO C'E' UN RIFERIMENTO CONTRO CUI PROVARLO, e non e' poco**: un
server TLS 1.3 vero raggiungibile dalla macchina virtuale, e la stessa
connessione fatta in due modi. Quando l'handshake nostro sbagliera' un byte del
key schedule, ci sara' qualcosa con cui confrontare invece di indovinare.

### FATTO — HMAC, HKDF e RSA-PSS: i tre mattoni che TLS 1.3 chiede

`lib/extls`, il primo pezzo della terza libreria. Sono tre cose piccole e
tutt'e tre indispensabili:

  - **HMAC-SHA256**, il mattone di tutto il resto;
  - **HKDF** (RFC 5869) con **HKDF-Expand-Label** (RFC 8446), cioe' il modo in
    cui TLS 1.3 tira fuori tutte le chiavi da un segreto solo;
  - **RSA-PSS**, verifica.

! **PSS NON E' UN DOPPIONE DI PKCS#1 v1.5.** TLS 1.3 ha TOLTO v1.5 dalla
CertificateVerify: i certificati restano firmati quasi sempre in v1.5 — e
quello sta in `lib/excert` — mentre la firma che il server fa sul dialogo in
corso e' PSS. Un TLS che sa fare solo v1.5 non completa nessun handshake 1.3.

! **E PSS NON SI PUO' RICOSTRUIRE INTERO**, perche' porta un sale casuale: si
ricostruisce l'IMPRONTA FINALE — che dal sale dipende — e si confronta quella,
mentre il resto della busta si controlla pezzo per pezzo. Il punto delicato e'
`emBits = bit(modulo) - 1`: sbagliarlo di uno fa rifiutare firme buone **solo
su alcune chiavi**, quelle la cui misura in bit non e' multipla di otto — cioe'
il modo peggiore di sbagliare, perche' sembra funzionare.

#### Provato contro le RFC, contro Python e contro openssl

    make prova-extls

    10 vettori RFC 4231/5869, 300 HMAC a caso, 6 expand-label
    12 firme PSS di openssl verificate, 36 varianti rovinate rifiutate
    nessuna differenza

! **I VETTORI DELLE RFC SONO IL RIFERIMENTO PIU' SOLIDO CHE ESISTA**: numeri
stampati dentro uno standard, calcolati da altri, controllati da vent'anni di
implementazioni. Ma sono pochi e tutti corti, quindi accanto ci sono trecento
HMAC su dati a caso confrontati con il modulo `hmac` di Python — chiavi piu'
lunghe del blocco, messaggi vuoti, chiavi da duecento byte: casi che in nessuna
RFC compaiono.

! **E PER PSS IL RIFERIMENTO E' CHI LE FIRME LE FA**: si firma con `openssl
pkeyutl -pkeyopt rsa_padding_mode:pss` e si chiede a noi di verificarle. Poi
ogni firma buona si rovina in tre modi — un bit girato, il messaggio cambiato,
il sale di lunghezza diversa da quella pretesa — perche' un verificatore che
dice sempre «si'» passa tutte le firme buone.

! **E LA PROVA HA SBAGLIATO PRIMA DEL CODICE, di nuovo.** Le etichette di TLS
1.3 contengono SPAZI — «c hs traffic», «res master» — e il banco le leggeva con
uno `%s`, che si ferma al primo. Diceva «MALE» su un codice giusto perche' gli
stava mandando meta' riga. Adesso l'etichetta viaggia in esadecimale anche se
e' testo.

**Restano, per un handshake vero**: il record (ChaCha20-Poly1305 c'e' gia'), la
macchina a stati dell'handshake, l'ECDSA su P-256 — 39 dei 151 certificati
radice sono gia' a curva — e il magazzino delle CA da mettere sul CD.

### FATTO — `excert`: la catena, cioe' l'unica domanda che conta

`exbig` fa il conto di una firma, `exasn1` dice quali numeri metterci. Questo
risponde a **«posso fidarmi di chi mi ha risposto?»** — ed e' la parte che, se
manca, rende il TLS PEGGIO del testo in chiaro: cifrare con chiunque risponda
vuol dire cifrare con chi sta in mezzo, e la barra intanto scrive `https://`.

Una catena e' buona se, per ogni anello: la **firma torna** con la chiave di
chi lo ha emesso; l'emittente del figlio e' il soggetto del padre **byte per
byte**; il padre e' una **CA** per basicConstraints; la data di oggi sta dentro
la validita' di **tutti** gli anelli, intermedi compresi; e l'ultimo e' firmato
da una radice **del magazzino**.

! **LA RADICE NON SI VERIFICA CONTRO SE STESSA.** Un certificato autofirmato
dimostra soltanto di possedere la propria chiave — vero anche per quello che si
e' fatto in casa chi attacca. La radice vale perche' E' NEL MAGAZZINO.

! **E IL NOME NON BASTA MAI.** Il nome dell'emittente lo scrive chi manda il
certificato: cercare per nome e fermarsi li' vorrebbe dire farsi indicare da
chi attacca quale radice usare. Il nome serve a SCEGLIERE il candidato fra le
duecento del magazzino; poi si fa il conto.

! **PKCS#1 v1.5 SI RICOSTRUISCE E SI CONFRONTA, NON SI ANALIZZA.** Guardare
dentro la busta con un lettore vuol dire accettare tutto cio' che quel lettore
lascia passare — byte in piu' in coda, lunghezze scritte lunghe, parametri
diversi. Sono le firme di Bleichenbacher del 2006, e funzionavano proprio
contro chi analizzava invece di confrontare. Qui la busta che DEVE esserci si
costruisce e si confronta tutta.

! **SHA-1 SI RIFIUTA PER NOME.** Le collisioni su SHA-1 si comprano, e una
firma di CA e' esattamente il posto dove servono. Riconoscerlo serve a dire
PERCHE' invece di dire «non lo capisco».

#### Provato su una PKI vera, costruita e poi rovinata

`make prova-excert` costruisce con openssl radice, intermedia e sito, e poi:

    ok   catena buona                       OK
    ok   radice non nel magazzino           SENZA RADICE
    ok   magazzino vuoto                    SENZA RADICE
    ok   senza l'intermedia                 SENZA RADICE
    ok   un certificato di sito che firma   NON E' UNA CA
    ok   emittente e soggetto non combaciano NOME DIVERSO
    ok   scaduto (data spostata avanti)     SCADUTO
    ok   non ancora valido (data indietro)  NON ANCORA VALIDO
    ok   solo la radice, che sta nel magazzino OK
    ok   un byte cambiato nel certificato   FIRMA SBAGLIATA

! **UNA PROVA CHE GUARDA SOLO IL «SI'» NON PROVA NIENTE**: un verificatore che
risponde sempre OK passa il caso buono. Sono i casi cattivi a smascherarlo — e
il MOTIVO conta quanto il rifiuto, perche' «scaduto» e «non mi fido» mandano
chi legge in due posti diversi.

! **E UNA DELLE DIECI ASPETTATIVE ERA SBAGLIATA, NON IL CODICE.** Davo per
rifiutata una catena fatta della sola radice che sta nel magazzino: e' valida,
e lo e' anche per openssl. Non prova che chi risponde possieda la chiave — ma
non e' compito della catena, e' della CertificateVerify dell'handshake, e un
server che manda una radice quella chiave non ce l'ha.

#### E l'impronta non e' qui dentro

`excert.c` dichiara `sha256()` e non la contiene: dentro EX-OS la mette la
libc, che ce l'ha gia'. Una seconda copia della stessa funzione e' la cosa che
questo progetto ha imparato a temere di piu' — e nella prova sull'host la mette
OpenSSL, cioe' l'implementazione di riferimento: qui si sta provando la CATENA,
non l'impronta.

**Restano, per `extls`**: HMAC e HKDF, RSA-PSS — che TLS 1.3 vuole al posto di
PKCS#1 v1.5 per la CertificateVerify — l'ECDSA su P-256, e poi il record e
l'handshake. E il magazzino sul CD, che e' un file da scrivere.

### FATTO — `exasn1`: DER e X.509, letti con diffidenza

Il secondo dei tre pezzi. `exbig` sa fare il conto di una firma; questo sa dire
QUALI numeri mettere in quel conto e da dove prenderli.

! **QUESTI BYTE ARRIVANO DALLA RETE, E CHI LI MANDA NON E' AMICO**, ed e' la
differenza fra questo lettore e tutti gli altri di EX-OS. Un font malformato sta
su un CD masterizzato da noi; un certificato lo sceglie chi risponde al posto
del sito che si voleva. Quindi: ogni misura controllata contro la fine del
buffer PRIMA di guardarci dentro, e i confronti scritti come sottrazioni sul
residuo — `n > d->n - off` — perche' una somma puo' TRABOCCARE e allora il
controllo dice di si' proprio quando dovrebbe dire di no.

! **NON SI COPIA NIENTE.** Ogni campo e' una fetta — puntatore e misura — dentro
il buffer di chi chiama: niente allocazione, niente buffer da dimensionare,
niente `memcpy` da sbagliare. E il TBSCertificate e' la fetta ORIGINALE, non
ricostruita: la firma copre quei byte esatti, e rigenerarli da una struttura
analizzata vorrebbe dire verificare una cosa diversa da quella firmata ogni
volta che il nostro codificatore sceglie una forma diversa dal mittente.

! **LA LUNGHEZZA IN FORMA INDEFINITA SI RIFIUTA**: e' legale in BER, non in
DER, e accettarla vuol dire due codifiche dello stesso certificato — cioe' due
impronte diverse della stessa cosa.

! **E GLI OID NON SI DECODIFICANO, SI CONFRONTANO PER BYTE.** Un OID e' una
costante: tenerne il DER e confrontarlo e' meno codice, non ha casi limite, e
soprattutto non aggiunge un ciclo di decodifica che gira su input ostile.

#### Provato su centocinquantuno certificati veri, non su casi inventati

    make prova-exasn1

    151 certificati letti, 0 rifiutati  (108 RSA, 4 EC P-256, 39 altro)
    82 firme verificate con i campi letti da exasn1 e il conto di exbig
    nessuna differenza con openssl
    1500 certificati rovinati, nessuno ha fatto uscire il lettore dal proprio buffer

I certificati sono quelli di `/etc/ssl/certs`: centocinquanta CA diverse,
scritte da programmi diversi, con versioni ed estensioni che nessuno si
inventerebbe scrivendo casi di prova a mano. Il riferimento e' `openssl`, e si
confrontano modulo, esponente, numero di serie, le due date, l'algoritmo della
firma e il bit di CA.

! **MA LA PROVA CHE CONTA NON CONFRONTA: VERIFICA.** Presi modulo, esponente,
firma e TBSCertificate **dal nostro lettore**, si rifa' il conto con `exbig` e
si guarda se ne esce PKCS#1 con l'impronta giusta. Se combacia, quei quattro
campi sono giusti INSIEME — ed e' precisamente cio' che dovra' fare extls.

! **E POI I CERTIFICATI GUASTI.** Quelli veri sono scritti bene per definizione:
chi li ha firmati voleva farli leggere. Millecinquecento copie rovinate —
tagliate in ogni punto, un byte cambiato a caso, e le LUNGHEZZE portate a valori
assurdi, che e' il campo con cui si fa uscire un lettore dal proprio buffer. La
prova non e' «li rifiuta»: un byte cambiato dentro un nome da' un nome diverso,
non un errore. La prova e' che **non si schianta**, e quello si vede dal
segnale.

! **UNA PROVA CHE SBAGLIA DOMANDA DA' UNA RISPOSTA SBAGLIATA**: la prima
passata diceva «modulo diverso» su quarantatre certificati, e li leggeva
benissimo — erano a curva ellittica, e `openssl x509 -modulus` un modulo non lo
stampa perche' non ce n'e' uno. Era la prova a chiedere la cosa sbagliata.

#### Cosa non c'e', dichiarato

Niente BER, niente stringhe convertite (le date si leggono e si normalizzano,
i nomi restano DER e si confrontano BYTE PER BYTE — le regole di equivalenza di
X.509 sono altrettanti modi di far sembrare uguali due nomi che non lo sono).
Le chiavi EC si leggono solo su P-256: le altre curve si dicono «ignote», che e'
diverso da «rifiutate» — il certificato si legge lo stesso, semplicemente non
sappiamo ancora verificarne la firma. Niente CRL e niente OCSP: sono richieste
in rete, non lettura di byte.

**Restano, prima di `extls`**: il magazzino delle CA — che e' un file da
leggere, non un formato da capire — e le curve, che stanno con l'ECDSA dentro
extls.

### FATTO — `exbig`: gli interi lunghi, e solo quelli che servono a VERIFICARE

E' il primo dei tre pezzi che mancano all'https, e l'ordine non e' negoziabile:
`exbig`, poi `exasn1`, poi `extls`. Un TLS che cifra senza sapere con chi sta
parlando e' peggio del testo in chiaro — con `http://` chi guarda sa di essere
scoperto, con `https://` gli si dice che e' al sicuro.

**Cosa c'e'**: `lib/exbig/exbig.c`, 300 righe. Interi senza segno fino a 4096
bit a misura FISSA (516 byte, sullo stack di chi chiama: una libreria che alloca
in mezzo a un handshake e' una libreria che puo' fallire a meta' verifica), i
byte in ordine di rete come stanno in un DER, e **una sola operazione vera**:
`r = base^e mod m`.

! **SOLO VERIFICA, E LA PAROLA VA PRESA ALLA LETTERA.** Niente generazione di
chiavi, niente primalita', niente CRT, niente esponenti segreti. Non e' una
tappa: e' il confine. E cio' che non c'e' non puo' perdere segreti — un codice
che maneggia solo numeri pubblici non ha niente da far trapelare col tempo che
impiega. Il giorno che ci entrasse una chiave privata, ogni `if` di quel file
diventerebbe un canale laterale da chiudere.

! **NON ESISTE LA DIVISIONE A 64 BIT, ED E' IL VINCOLO CHE HA SCELTO
L'ALGORITMO.** Una libreria di EX-OS si collega senza libgcc: `__udivdi3` non
c'e' e il collegamento FALLISCE — lo stesso muro di tsc.c e del rasterizzatore
dei font. La riduzione modulare della scuola vuole proprio quella divisione;
**Montgomery** invece il modulo non lo divide mai, lo somma. L'unica operazione
lunga resta la moltiplicazione, che su i386 e' `mull`, due istruzioni. Anche il
numero magico -m^-1 mod 2^32 si calcola senza dividere: Newton su interi,
cinque giri da 3 bit a 32.

#### I due difetti, e sono lo stesso difetto in due posti

Tutt'e due li ha trovati la prova, e nessuno dei due si vedeva sui numeri a
caso:

  1. **la coda del prodotto di Montgomery.** Il risultato sta in n parole piu'
     un bit, e con quel bit acceso il numero vero e' 2^(32n) + r con r piu'
     PICCOLO di m: sottrarre m dal solo r va sotto zero. Sintomo: `(m-1)^2 mod m`
     rendeva **zero** invece di uno. L'ha trovato il caso scritto a mano
     `a = m-1`, non i quattrocento casuali;
  2. **il raddoppio nel calcolo di R^2.** Con un modulo di 4096 bit — il tetto
     dichiarato — il riporto uscente non ha piu' dove stare, e buttarlo cambia
     il numero. Si vedeva solo alla misura massima, cioe' nel caso che si prova
     per ultimo e si spedisce per primo.

! **ADESSO E' UNA FUNZIONE SOLA**, `togli_m()`, chiamata da tutt'e due i posti:
erano due copie della stessa sottrazione, e una delle due era sbagliata.

#### Provato contro un'aritmetica di qualcun altro, e poi contro il mondo

`make prova-exbig` (`tools/prove/bigprova.py`) fa due cose diverse:

    2013 prove, 0 sbagliate
    5 casi malformati, tutti rifiutati
    12 firme di certificati veri verificate con exbig

Il riferimento sono **gli interi di Python**: `pow(a, e, m)` e' la risposta
giusta per definizione. Confrontare exbig con exbig direbbe solo che e'
coerente con se stesso — e' lo stesso patto di zlib per inflate e di FreeType
per i font.

! **E I CASI NON SONO SOLO «A CASO»**, perche' i numeri casuali non passano mai
per i bordi: modulo di una parola sola, base 0, base 1, base m-1, esponenti 0 e
1, moduli con la parola alta piena, il tetto di 4096 bit. Sono quelli che hanno
trovato tutt'e due i difetti.

! **POI LE FIRME VERE**, che sono l'unica prova che l'aritmetica serva a
qualcosa: i certificati radice di questa macchina, `m = firma^e mod n`, e
dentro ci si ritrova PKCS#1 v1.5 — `00 01 FF..FF 00` e il DigestInfo con
l'impronta SHA-256 del TBSCertificate che combacia. Autofirmati apposta: la
chiave che li firma e' la loro, quindi non serve un magazzino di CA per fare il
conto. Decidere DI CHI FIDARSI e' un'altra domanda, ed e' di extls.

#### Perche' non e' (ancora) una `.so`

La regola di questo sistema e' che una libreria condivisa conviene quando due
programmi la usano. exbig oggi ne ha **zero**. Una .so senza utenti e' peso
morto sul CD e, peggio, un artefatto che nessuno esercita: si scopre rotta il
giorno che serve. Sta in `lib/` come `lib/excrypt`, compilata dentro chi la
usera'. **Ma si compila a ogni build** — `make verifica-exbig`, dentro
`make all` — perche' una libreria che non entra in nessun eseguibile smette di
compilare senza che nessuno se ne accorga.

**Il prossimo passo e' `exasn1`**: DER, X.509, il magazzino delle CA.

### FATTO — le immagini nel browser

Era il terzo di tre lavori scelti insieme, e chiude la serie. Il browser
riconosce `<img>`, legge `src`, `width`, `height` e `alt`, scarica, decodifica
con eximg.so, riduce e colloca.

! **LE IMMAGINI ARRIVANO DOPO IL TESTO, ED E' LA DECISIONE CHE CONTA.** La
pagina si impagina e si disegna con le sole parole; solo allora si prende
un'immagine per volta, e a ognuna che arriva si reimpagina e si ridisegna. Il
testo si sposta sotto gli occhi, ed e' il prezzo giusto: prenderle prima
vorrebbe dire una finestra vuota finche' l'ultima non risponde, e una che non
risponde costa otto secondi da sola.

! **I PIXEL SONO DEL BROWSER, NON DI eximg.** Si decodifica, si copia nella
misura con cui si disegnera' — col vicino piu' vicino — e il bitmap naturale si
restituisce SUBITO. Tenerlo vorrebbe dire lasciar scegliere alla pagina quanta
memoria prendere: centoventotto chilobyte di PNG possono essere 4000x3000
pixel, cioe' 48 MB su una macchina che ne ha 32. I tetti sono tre e dichiarati:
dodici immagini, 128 KB per file, 512 K pixel IN TUTTO.

! **E QUELLA CHE NON ARRIVA NON FERMA NIENTE**: si salta, e al suo posto resta
il suo `alt` impaginato come testo normale — che e' esattamente il motivo per
cui quell'attributo esiste. Il valore sta gia' nell'arena del documento, quindi
si spezza in parole con lo stesso codice di tutto il resto.

! **`data:` NON SI SEGUE, E VA RICONOSCIUTO PRIMA DI RISOLVERE.** Uno schema
qualunque attaccato in coda all'indirizzo di adesso produrrebbe una richiesta
lunga un chilometro verso il sito sbagliato. La regola: due punti prima di
qualunque `/` sono uno schema. E `//host/x` invece E' un indirizzo — «lo stesso
schema della pagina» — e le immagini dei siti veri sono scritte cosi' molto
piu' spesso dei collegamenti.

! **UN'IMMAGINE SI RITAGLIA A MANO, e non e' pignoleria**: `ex_pixmap` ritaglia
alla FINESTRA, non all'area del documento. Il testo se la cava perche' e' alto
venti punti e sborda di poco; un'immagine alta duecentocinquanta scorsa in su
dipingerebbe sopra la casella dell'indirizzo.

#### IL DIFETTO CHE E' USCITO: `LIB_MAX` era 4, ED E' DI TUTTO IL SISTEMA

    [ERROR] LIB: gia' 4 librerie caricate, '/exwin/lib/eximg.so' non entra

! **NON E' UN TETTO PER PROCESSO, E' UNA CACHE UNICA.** Il browser da solo ne
apre gia' quattro — libc, exwin, exhttp, e exfont che exwin apre da se' per il
TrueType — quindi la quinta non entrava per nessuno. Il messaggio c'era e
diceva la verita'; la verita' era che il numero era troppo piccolo.

Alzato a **12**: le librerie di oggi sono sei (libc, exwin, exdlg, exfont,
exhttp, eximg) e ne sono dichiarate altre tre per l'https (exbig, exasn1,
extls). Costa solo kernel `.bss` — una voce inutilizzata e' `usata = 0` e non
ha nessuna pagina dietro — circa 1,6 KB l'una, tredici in tutto.

#### Come si e' provato, e con che numeri

    python3 <server locale con una pagina e tre PNG generati a mano>
    EXOS_QEMU_EXTRA="-netdev user,id=n1 -device ne2k_pci,netdev=n1" \
    EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso \
    python3 tools/qemu_drive.py "netdetect -c@14" "exwin@14" "key:alt-f2@3" \
        "http://10.0.2.2:8080/@40" "foto:/tmp/pagina.ppm@2"

! **SORPASSATO IL 26 AGOSTO 2026, e si tiene scritto perche' era un GUASTO
annotato come una regola.** Qui c'era: «la grafica sta sulla console 1: dopo
`exwin` i tasti vanno ancora alla shell, con `Alt+F2` si passa di la'». Non era
il funzionamento previsto, era il driver di tastiera che non seguiva chi
commutava la console — vedi il punto 1 del 26 agosto. Adesso dopo `exwin` i
tasti arrivano alla scrivania da soli, e il `key:alt-f2@3` qui sopra non serve
piu' (lasciarlo non fa danno: porta a una console di testo e torna).

Resta vero il resto: il browser mette il fuoco sulla casella dell'indirizzo
all'avvio, quindi si scrive l'indirizzo e basta. Per lanciarlo da solo serve
`@avvio /exwin/bin/browser` in `applicazioni.txt` — `@avvio` non prende
argomenti.

I numeri della foto, contati e non guardati: la stessa `quadranti.png` (120x80,
quattro quadranti pieni) messa due volte, una al naturale e una dichiarata
`width=60 height=40`. Attesi 60*40 + 30*20 = **3000 pixel esatti** per ciascuno
dei tre colori. Trovati 3000 rossi, 3000 verdi, 3000 blu. La terza, 1200x400,
ridotta a 744x248 — la larghezza dell'area, con le proporzioni tenute. La
quarta rende 404 e mostra «QUESTA IMMAGINE MANCA»; la quinta e' una `data:` e
non ha prodotto **nessuna richiesta** al server.

! **E `libctest` DA' 196 SU 211 PRIMA E DOPO IL CAMBIO DI `LIB_MAX`**, con gli
stessi quindici falliti: sono tutte scritture, e da CD non c'e' niente di
scrivibile. Misurato nei due sensi apposta, perche' «gli stessi errori di
prima» e' un fatto solo se si e' guardato prima.


### Rifiniture, quando conviene

! **QUESTA E' UNA CODA, NON UN ARCHIVIO**, e va tenuta corta. Fino al 19 agosto
le voci fatte restavano qui barrate, con dentro tutto il loro testo storico:
il risultato era che rileggendo la coda ricomparivano ogni volta le stesse
cose, e per trovare cio' che manca davvero bisognava saltare quattro voci
chiuse. **Una voce fatta esce di qui**: il racconto di com'e' andata sta nella
sua sezione piu' sotto, che e' il posto giusto per una cosa che non si deve
piu' fare.

Chiuse il 19 agosto e tolte da questo elenco: l'annullamento nell'editor,
`ls -l`, `-i` ai driver, e le regioni sporche (per il solo movimento del
puntatore — vedi la sua sezione). Chiusa il 20 agosto: **`su`**, che si chiama
`sudo` ed e' venuto come questa voce prevedeva — capacita' stretta (`SYS_SU`),
SHA-256 nel kernel, nessun bit setuid. Il racconto sta nella sezione «`sudo`
ESEGUE UN COMANDO», e comprende l'unica cosa che la voce non aveva previsto:
che il kernel non poteva leggere `/boot/ombra`.

 1. **`login` e `install` sulla libc condivisa** — **misurato il 19 agosto, e
    la raccomandazione e' DI NON FARLO**, ma la decisione e' di chi possiede il
    sistema:

        login   statico   32.112 byte
        install statico   39.768 byte
        ls      dinamico  18.420 byte   (per paragone)

    Si guadagnerebbero forse trentacinque chilobyte in tutto. Si perderebbe che
    i due programmi con cui si ENTRA e con cui si RIPARA non dipendano da un
    file che potrebbe essere proprio quello rotto. La shell e' statica per la
    stessa ragione: e' una scelta che questo sistema ha gia' fatto altrove.

    ! **IL PREREQUISITO SCRITTO IN QUESTA VOCE — «accorgersi che manca» — E'
    FATTO**, e ha trovato dell'altro: vedi la sezione sulla console che non si
    apre.
 2. **Risolvere per hash invece che per nome** — altri ~3 KB per programma, col
    generatore che verifica a costruzione che non ci siano collisioni.
 3. **Le regioni sporche, gli altri undici casi** — oggi si stringe solo il
    movimento del puntatore; una finestra che si aggiorna, che si sposta o che
    nasce dichiara ancora tutto lo schermo. Ognuno va fatto guardando i pixel,
    perche' una regione sbagliata per difetto lascia roba vecchia a video.

Restano aperti, sullo stack USB: **piu' di un livello di hub**, **piu' di un
dispositivo per volta** (lo stesso limite che ha uhci.drv), e **un tetto di
quattro alloggiamenti** in xhci.drv — dichiarato, non scoperto dopo.

### Il lavoro grosso che resta: `extls`

`https://www.w3c.org` e' rifiutato con «https non ancora: manca il TLS», che e'
la verita'. Per toglierlo servono, in quest'ordine:

    exbig       FATTO il 24 agosto 2026 — vedi la sezione qui sotto. Interi
                lunghi, SOLO verifica: niente generazione di chiavi, niente
                primalita', niente CRT
    exasn1      FATTO il 24 agosto 2026 — DER e X.509. Il magazzino delle CA
                resta da fare: e' un file da leggere, non un formato da capire
    excert      FATTO il 24 agosto 2026 — la catena: chi firma chi, chi e' una
                CA, cosa e' scaduto, e quale radice sta nel magazzino
    extls.so    TLS 1.3 — HMAC, HKDF e RSA-PSS FATTI il 24 agosto 2026.
                Restano il record, l'handshake, l'ECDSA su P-256 e il
                magazzino delle CA da mettere sul CD

! **SSH HA EVITATO RSA APPOSTA, PER NON SCRIVERE UNA LIBRERIA DI INTERI
LUNGHI. L'HTTPS NON PUO' EVITARLO.** I certificati del web sono RSA ed ECDSA
P-256; quelli Ed25519 in pratica non esistono.

! **LA DECISIONE E' STATA PRESA IL 19 AGOSTO 2026: LA CIFRATURA ASPETTA LA
VERIFICA.** Non si spedisce un TLS che cifra e non sa dire con chi sta
parlando. Senza verifica del certificato la connessione e' cifrata con CHIUNQUE
risponda: chi sta in mezzo si presenta come il sito che vuole, il browser
accetta, e la barra scrive `https://`. Non e' «meno sicuro del testo in
chiaro», e' PEGGIO — con `http://` chi guarda sa di essere scoperto, con
`https://` gli si dice che e' al sicuro. Il lucchetto mentirebbe.

! **QUINDI L'ORDINE NON E' NEGOZIABILE**: prima `exbig` e `exasn1`, poi
`extls`. Niente scorciatoia «intanto cifriamo e lo diciamo nella barra»: e' la
strada su cui l'etichetta si dimentica, e allora resta solo la bugia. Serve
anche il magazzino delle CA sul CD, che fa parte del lavoro e non e' un
dettaglio di contorno.


### L'INSTALLATORE TOGLIEVA L'AUTENTICAZIONE — e nessuno lo vedeva

! **UN AGGIORNAMENTO LASCIAVA UNA RADICE ext2 SENZA ACCESSO.** Il kernel lancia
`/bin/login` solo se `kernel.cfg` ha la voce `login` (kernel_main.c, PASSO 15);
quella voce esiste dal 17 agosto 2026; e l'installatore, quando trovava un
`kernel.cfg` gia' presente, **lo lasciava intatto e basta**. Quindi un sistema
installato prima di quella data e poi aggiornato si riavviava con la radice
ext2 e senza autenticazione — mentre due schermate piu' su l'installatore
stampava «al primo avvio l'accesso sara' OBBLIGATORIO».

! **NON ERA UNA SOVRASCRITTURA, ERA UN'OMISSIONE**, ed e' la parte da tenere a
mente: «non tocco niente» sembra sempre la scelta prudente, e su un file di
configurazione dell'utente quasi sempre lo e'. Non lo e' per una voce che nel
file vecchio **non c'e'**: quella non e' una decisione di nessuno, e' una voce
che non esisteva ancora quando quel file e' stato scritto.

La regola nuova, in due righe:

    voce ASSENTE e necessaria  ->  si aggiunge, e si dice che si e' aggiunta
    voce PRESENTE ma diversa   ->  si lascia e si SUGGERISCE, perche' quella
                                   si' e' una decisione di chi usa il sistema

E in fondo si elencano le voci che il `kernel.cfg` spedito ha e quello
installato no: non si toccano, si mostrano. L'elenco da allungare quando il
kernel impara una voce nuova e' `CFG_NECESSARIE[]` in bin/install/install.c —
e ci va **solo** una voce la cui assenza e' un difetto, non una preferenza.

! **LE RIGHE COMMENTATE NON CONTANO COME VOCI.** `kernel.cfg` e' pieno di
esempi spenti (`# svga = 800x600`): scambiarne uno per una voce presente
vorrebbe dire non aggiungere mai la voce vera. E' uno dei sette casi del banco
di prova.

! **E IL TETTO DI 8191 BYTE VALE ANCHE QUI**: un file che sforerebbe NON si
scrive, perche' oltre quel tetto le sezioni finali spariscono in silenzio e la
macchina si presenta senza tastiera. Meglio dire «aggiungi questa riga a mano».

#### Come si e' provato — riproducendo il difetto, non solo correggendolo

    1. ISO costruita TOGLIENDO `login` da boot/kernel.cfg (il «prima»)
    2. disco da 64 MB, fdisk (83), mkfs -t ext2, mount, install -t /disco
    3. avvio DAL DISCO  ->  prompt della shell, `whoami` rende root
                            NESSUNA autenticazione: difetto riprodotto
    4. ISO normale, mount, `install -a /disco`, e poi avvio dal disco
       ->  «nome utente:», utente creato, e da li' in poi si entra solo
           autenticati

    /boot/kernel.cfg      3058 byte
    /boot/kernel.cfg.bak  3033 byte      (+25 = la riga aggiunta, e basta)
    cmp                   differiscono al byte 1887, riga 49

! **PIU' SETTE CASI SULL'HOST**, perche' il parser e' la parte che sbaglia:
voce assente, voce presente, voce presente ma diversa, voce solo commentata,
sezione mancante, file al tetto, e la voce che deve finire DENTRO `[boot]` e
non dopo `[modules]`. **Il banco ha trovato un difetto mio**: il percorso del
`.bak` veniva TRONCATO invece che rifiutato, e un `.bak` troncato e' il nome di
un altro file su cui `copia()` avrebbe scritto sopra.

### La cache su disco del browser

`$HOME/.app/browser/cache`, e la convenzione e' `$HOME/.app/<programma>/`.

! **IL POSTO NON SI SCRIVE NEL CODICE, SI RICAVA DA `HOME`.** La casa e'
`/root` solo per root e `/home/<utente>` per tutti gli altri — la regola sta in
bin/login/login.c — e un percorso costante nel sorgente funzionerebbe per una
persona sola.

! **SE NON SI PUO' SCRIVERE NON SI MUORE.** Avviando da CD la radice e' in sola
lettura: il browser lo dice una volta e lavora in memoria come prima.

! **IL NOME DEL FILE E' L'IMPRONTA DELL'INDIRIZZO, MA A DECIDERE E'
L'INDIRIZZO SCRITTO DENTRO.** Un'impronta a 32 bit ogni tanto collide, e una
collisione servirebbe l'immagine SBAGLIATA — un difetto silenzioso, il peggiore
che una cache possa avere. Con l'indirizzo nella testa del file la collisione
diventa un buco: si riscarica.

! **«INDIETRO» SI SERVE DALLA CACHE, TUTTO IL RESTO VA IN RETE.** Tornare
indietro deve mostrare la pagina che si e' vista; battere un indirizzo o premere
un collegamento e' una richiesta nuova e vuole la pagina di adesso.

! **ED E' UNA DIRECTORY TEMPORANEA: SI SVUOTA ALL'AVVIO**, e si cancellano solo
i file col nostro nome (otto cifre esadecimali piu' `.dat`). Svuotare una
directory cancellando tutto quello che ci si trova dentro e' come si perdono i
file di qualcun altro il giorno che il percorso e' sbagliato di un livello.

#### Cosa e' provato e cosa no — detto per intero

**Sull'host, 12 casi sul codice ESTRATTO TALE E QUALE** dal browser: creazione
della catena di directory, scrittura e rilettura byte per byte, miss su
indirizzo mai visto, **collisione che non serve il file sbagliato**, tetto per
sessione, svuotamento che lascia in pace i file altrui, e `HOME` assente.

**Dentro EX-OS**: `browser: cache in /disco/root/.app/browser/cache` su una
casa scrivibile ext2, e `browser: niente cache in /.app (filesystem in sola
lettura), lavoro in memoria` avviando da CD.

**E il colpo a segno, provato in volo il 19 agosto**: una pagina con DUE
`<img>` che hanno lo stesso `src`, e il registro del server dice

    /
    /quadranti.png          <- una volta sola, non due

mentre nella foto i pixel sono 4800 per colore, cioe' 2400 per due immagini:
tutt'e due si vedono, e la rete e' stata toccata una volta.

! **IL SERVER DI PROVA LO LANCIA QEMU, NON LA SHELL**, e senza questo la prova
non si poteva fare: quando i processi dell'host non condividono lo spazio di
rete, un server che ascolta su una porta non e' raggiungibile dalla macchina
virtuale. Con `guestfwd` il comando NASCE DENTRO QEMU, quindi la rete e' la
stessa. Il server parla su stdin/stdout, una connessione per esecuzione, e
annota ogni richiesta in un file — che e' poi come si CONTANO le richieste
invece di guardarle:

    -netdev user,id=n1,guestfwd=tcp:10.0.2.100:80-cmd:/percorso/srv.sh

! **E IL COMANDO NON PUO' AVERE SPAZI**: `EXOS_QEMU_EXTRA` viene spezzato sugli
spazi da qemu_drive, quindi `-cmd:python3 srv.py` arriva a QEMU tagliato a
meta'. Serve uno script senza argomenti.

### LA SCRIVANIA BUTTAVA VIA L'AMBIENTE — sei `spawn` con `envp` nullo

! **`envp == NULL` NON VUOL DIRE «EREDITA», VUOL DIRE «VUOTO».** Sta in
kernel/syscall/syscall_impl.c: se il puntatore e' nullo il figlio nasce con
zero variabili, e gli resta solo il blocco `[env]` di `kernel.cfg` — dove
`HOME = /`.

Erano nulli in tutti e sei i punti da cui nasce la scrivania:

    bin/exwin/exwin.c            il server e il program manager
    drivers/wserver/wserver.c    la RINASCITA sulla console 1
    exwin/bin/pm/pm.c            il menu, l'orologio, l'avvio automatico

Effetto: un utente entrava come `tizio`, `login` gli metteva
`HOME=/home/tizio`, e **ogni applicazione grafica vedeva `HOME=/`**. Nessun
programma della scrivania sapeva dov'era la casa di chi lo stava usando.

! **NON SI VEDEVA PERCHE' NESSUNO GUARDAVA `HOME`**, ed e' saltato fuori solo
quando il browser ha provato a tenersi la cache in `$HOME/.app/browser/cache` e
se l'e' ritrovata nella radice. Un difetto che aspettava il primo programma
grafico con dei file suoi.

Provato A/B, stesso CD e stessi comandi, cambiati solo i sei argomenti:

    prima   HOME=/disco/root esportato -> «niente cache in /.app»
    dopo    HOME=/disco/root esportato -> «cache in /disco/root/.app/browser/cache»

### L'INSTALLATORE COPIAVA LA DESTINAZIONE DENTRO SE STESSA

Saltato fuori preparando la prova dell'utente normale:

    ~ /disco/disco/boot/kernel.cfg
    ~ /disco/disco/disco/boot/kernel.cfg
    ~ /disco/disco/disco/disco/boot/kernel.cfg      ... e cosi' via

**398 file inutili, sei livelli di profondita'**, fermati solo da
`ALBERO_LIVELLI_MAX` — che e' un tetto contro i cicli, non una scelta. Ed erano
anche i «6 errori» che ogni installazione dichiarava alla fine senza che
nessuno ne cercasse la causa.

`cerca_componenti` chiama componente ogni directory di primo livello che non
sia di sistema; il punto di montaggio del disco su cui si sta installando E'
una directory di primo livello, quindi finiva nell'elenco — e `install -t`,
che prende tutti i componenti, se lo copiava dentro.

! **E NON SI RIPARA ALLUNGANDO L'ELENCO DEI NOMI.** In `DIR_NON_COMPONENTI`
c'era gia' `disk`, che e' il punto di montaggio usato negli esempi della
documentazione; questa ricetta usa `/disco`, e sono bastate due lettere. Il
nome lo sceglie chi installa, quindi qualunque elenco indovinato sbaglia al
primo che non c'e'. L'installatore il bersaglio ce l'ha in mano: si salta
QUELLO. Cinque casi sull'host — con la barra, senza, maiuscolo, con la barra in
coda, e un punto che non esiste.

Dopo: **zero annidamenti**, e «Installazione completata» senza contatore di
errori.

### IL SERVER A FINESTRE NON E' UN DRIVER, E ADESSO NON LO SEMBRA PIU'

Il 19 agosto la prova mancante ha dato questo:

    uid=1000(tizio) gid=1000
    exwin
    [WARN]  ELF: '/dev/wserver.drv' non eseguibile da questo utente (err=-13)

! **E LA RIPARAZIONE OVVIA ERA QUELLA SBAGLIATA.** Allargare i permessi di
`wserver.drv` avrebbe dato a chiunque `is_driver`, cioe' `ioport_bind` e
`dma_alloc`: la capacita' larga al posto di quella stretta, che e' esattamente
il difetto che il 17 agosto era stato chiuso.

! **IL PUNTO E' CHE QUEL FILE NON AVEVA PIU' MOTIVO DI CHIAMARSI `.drv`.** Si
chiamava cosi' per una ragione sola — `mmio_map()` e' riservata agli
eseguibili caricati da un `*.drv` — e dal 17 agosto il framebuffer si mappa con
`fb_map()`, che non prende argomenti, non da' nessuna porta e **non ha nessuna
guardia**: e' la capacita' stretta costruita apposta. In `wserver.c` non c'e'
una sola chiamata a `mmio_map`, `ioport_bind`, `dma_alloc` o `irq_bind`.

Restava solo la confezione: il nome e l'indirizzo di quando serviva. Ed era la
confezione a tenere la grafica fuori dalla multiutenza — perche' `/dev` e' di
root, e il nome di servizio PER UTENTE che `win_proto.h` descrive dal 17 agosto
(«root registra `wserver`, chiunque altro `<uid>:wserver`») descriveva una
situazione che non si poteva verificare.

    /dev/wserver.drv      ->   /exwin/bin/wserver

! **E NON E' UN SERVIZIO D'AVVIO UNICO**, che era la prima idea e sarebbe stata
sbagliata: avrebbe dato a tutti gli utenti lo stesso server, buttando via
proprio l'isolamento che il nome per utente costruisce. Ogni utente si avvia il
SUO.

Provato: `tizio` (uid 1000) entra, batte `exwin`, e la scrivania si apre —
wserver mappa il framebuffer, rinasce sulla console 1, pm elenca cinque
applicazioni. E root dal CD funziona come prima, senza regressioni.

! **IL MAKEFILE HA RIPETUTO UN VECCHIO INCIAMPO**, e vale la pena scriverlo: la
regola nuova usava `$(BUILD_EXWIN_BIN)` PRIMA che fosse definita, e con `:=`
make espande subito — quindi il collegamento scriveva in `/wserver`, la radice
del disco. E' lo stesso errore che il Makefile documenta gia' per `exwin.so`,
venti righe piu' in la'. Il blocco e' stato spostato sotto le dichiarazioni.

### exhtml.so — E IL CRITERIO DEI «DUE UTENTI» SCAVALCATO DI PROPOSITO

    /exwin/lib/exhtml.so     base 0x05800000, la settima fetta

! **VA DETTO CHE QUESTA VOLTA LA REGOLA E' STATA MESSA DA PARTE**, invece di
far finta che fosse soddisfatta. Il criterio di EX-OS e' che una libreria
condivisa conviene quando gli utenti sono DUE, e il lettore di HTML ne aveva
uno solo — il browser. C'era scritto perfino nella regola del browser: «html.c
resta collegato dentro, e non e' una dimenticanza».

La decisione e' stata di renderlo comunque disponibile: un albero di marcatore
non serve solo a impaginare una pagina, e tenerlo dentro un eseguibile vuol
dire che il secondo utente non nasce perche' e' scomodo, non perche' non serve.

! **E LA FORMA ERA GIA' QUELLA GIUSTA PER UNA .so**, il che e' anche il motivo
per cui e' costata poco: `html_prepara()` riceve i buffer da chi chiama, quindi
la libreria NON TIENE STATO. Due programmi che analizzano due documenti nello
stesso momento non si toccano, e non c'e' stato niente da rendere rientrante.

! **SI ESPORTANO ANCHE I TRE LETTORI** — `html_nome`, `html_testo`,
`html_attr` — e non sono un di piu': sono l'unico modo di guardare dentro un
nodo senza conoscere l'arena. Chi li riscrivesse nell'applicazione dipenderebbe
dalla disposizione interna di `HtmlDoc`, che e' esattamente cio' che una
libreria deve poter cambiare.

Il browser e' passato da 36788 a **28512 byte**; la tabella di esportazione sta
esattamente a `0x05800000` (`readelf`). Provato in volo: la pagina di prova
rende gli stessi 4800 pixel per colore di prima, cioe' l'albero e' identico.

! **E LA MAPPA DELLE FETTE ERA INDIETRO DI DUE**: `exwin.ld` diceva
«0x05000000 libere» mentre exfont e exhttp erano gia' li'. Una fetta assegnata
due volte non da' un errore di collegamento — da' due librerie che si
sovrascrivono dentro il processo che le apre tutt'e due. Adesso la mappa e'
completa e c'e' scritto perche' va tenuta in pari.

### excss.so — I FOGLI DI STILE

    /exwin/lib/excss.so      base 0x05C00000, l'ottava fetta

! **E' LA PRIMA LIBRERIA DI EX-OS CHE NE USA UN'ALTRA**: `css.c` chiama
`html_nome` e `html_attr`, quindi dentro excss.so si collega lo STUB di exhtml,
esattamente come farebbe un programma. L'alternativa era una seconda copia di
html.c — cioe' il difetto che le librerie condivise esistono per togliere.

! **LO STILE SI CALCOLA A RICHIESTA, NON SI TIENE IN CACHE, ED E' UNA SCELTA
FATTA GUARDANDO AL JAVASCRIPT.** Il giorno che ci sara' un motore, il documento
diventera' modificabile: un nodo cambia classe, un altro compare. Uno stile
calcolato una volta e conservato accanto al nodo sarebbe, da quel giorno, un
valore che invecchia senza che nessuno se ne accorga. Si ricalcola.

! **E `CSS_ORIGINE_JS` E' GIA' DICHIARATA E NON LA USA NESSUNO**, sopra
l'attributo `style`: e' il posto dove finiranno le assegnazioni di exjs, ed e'
scritto adesso perche' dopo si sarebbe messo accanto invece che sopra.

! **L'EREDITARIETA' LA PASSA IL CHIAMANTE.** Chi impagina scende gia'
ricorsivamente e ha in mano lo stile del padre nel momento in cui serve; farla
risalire alla libreria vorrebbe dire ripercorrere la catena dei padri per ogni
elemento.

! **QUELLO CHE SI SCARTA INVECE DI INDOVINARE**, che e' la regola con cui e'
scritto tutto il lettore: un selettore con `>` non diventa una discendenza —
`div > p` e `div p` non sono la stessa cosa, e confonderli colorerebbe i
nipoti. Un selettore piu' lungo del tetto si butta invece di essere accorciato,
perche' tenerne gli ultimi pezzi lo renderebbe piu' LARGO dell'originale. `2em`
si rifiuta invece di valere due pixel. **Meno stile, mai stile sbagliato.**

#### I tag di aspetto sono diventati CSS

`h1`, `h2`, `h3`, `<b>`, `<i>`, `<strong>`, `<em>` e il colore dei collegamenti
stanno adesso in un foglio predefinito dentro il browser, con l'origine piu'
bassa della cascata. Prima `h1` era grande e in neretto per via di un `if`, e
nessuna pagina poteva dire altrimenti; adesso e' una regola, quindi si
sovrascrive. **E `<b>` e `<i>` prima non c'erano affatto**: sono arrivati come
due righe di foglio invece che come due casi nel motore.

#### Provato

40 casi sull'host (`tools/prove/cssprova.c`), quasi tutti su fogli MALFATTI:
`@media` annidati, commenti in mezzo a un selettore e dentro un valore,
dichiarazioni senza i due punti, graffe mai chiuse, `!important`, un foglio piu'
grande dei buffer. **Due difetti trovati dal banco e non guardando il codice**,
tutt'e due della stessa forma — un commento dentro un selettore faceva scartare
la regola INTERA, e un commento dentro un valore la spegneva — e tutt'e due
MUTI: nessun errore, solo una regola che non si applicava.

In volo, contando i pixel di una foto:

    #ff0000 dal blocco <style>        2950
    #00ff00 dal <link> esterno        3367
    #0000ff dall'attributo style      1724
    #ff00ff dentro un display:none       0   <- e deve essere zero

### LE TRE PROPRIETA' CHE VENIVANO CALCOLATE E BUTTATE VIA

Un anello lasciato aperto dal commit prima: `excss` leggeva e calcolava
`text-align`, i quattro margini e `background-color`, e il browser non le
guardava. Tre proprieta' su otto promesse dalla libreria e scartate da chi la
usa — il tipo di divario che non da' nessun errore e che si scopre solo
provando una pagina che le usa.

! **L'ALLINEAMENTO NON SI PUO' APPLICARE MENTRE SI SCRIVE**, ed e' la ragione
per cui serve segnare dove comincia la riga: per centrare bisogna sapere quanto
e' larga, e lo si sa solo quando e' finita. Si segna il primo pezzo, e al
momento di andare a capo si spostano tutti quelli della riga.

! **I MARGINI SI ACCUMULANO E VANNO RIMESSI COM'ERANO USCENDO**, come il
collegamento in corso: un `blockquote` dentro un altro rientra due volte.
E il margine dichiarato SOSTITUISCE il predefinito invece di sommarcisi —
`margin-top: 0` deve poter togliere lo spazio, e sommando non lo toglierebbe
mai.

! **UNO SFONDO NON E' UN PEZZO, E' CIO' CHE STA SOTTO I PEZZI.** Vive in un
elenco suo e si disegna PRIMA di tutto il testo: metterlo fra i pezzi vorrebbe
dire dipingere sopra le parole ogni volta che un blocco colorato viene dopo,
perche' l'ordine dei pezzi e' quello del documento e non ha niente a che fare
con la profondita'. E la sua altezza si sa solo quando il blocco e' finito: si
segna la y entrando e si chiude uscendo.

Nel foglio predefinito sono entrati anche i rientri che rendono i margini
visibili senza che la pagina dica niente: `blockquote`, `ul`, `ol`, `dd`, e
`center` come allineamento.

#### Misurato sui pixel, non guardato

    scatola con margin-left:60 margin-right:40
        bordo sinistro   atteso  68 (+2 di cornice)   trovato  70
        bordo destro     atteso 711 (+2)              trovato 713
    testo con text-align:center
        centro           atteso 382                   trovato 381
    testo con text-align:right
        fine             atteso 754                   trovato 751

I due pixel di scarto sono la cornice della finestra dentro la foto dello
schermo, e sono gli stessi da tutt'e due i lati; l'uno del centro e' la
divisione per due; i tre della destra sono la spalla dell'ultimo glifo.

### LE TABELLE, E L'UNICO POSTO CHE VUOLE DUE PASSATE

! **LA LARGHEZZA DI UNA COLONNA NON SI SA FINCHE' NON SI E' GUARDATO OGNI
CONTENUTO DI QUELLA COLONNA.** Il resto della pagina si impagina in avanti, una
parola dopo l'altra, senza tornare indietro; qui no. Prima si misura ogni cella
come se avesse tutta la riga, poi si decide quanto e' larga ogni colonna, e
solo allora si impagina davvero. I pezzi della prima passata SI BUTTANO — si
segna dove arrivava `g_pez_n` e ci si torna — o si disegnerebbe due volte ogni
cella, la seconda nel posto giusto e la prima dove capita.

! **E MENTRE SI MISURA NON SI ALLINEA.** Il difetto trovato dai pixel: il
foglio predefinito centra i `<th>`, quindi la passata di misura spingeva i
pezzi in mezzo alla riga e la larghezza tornava «meta' pagina» invece che
«quanto la parola». Il risultato erano tre colonne quasi uguali, con quella
lunga stretta e quella di due lettere larghissima. L'allineamento decide DOVE
mettere una riga; la misura chiede QUANTO occupa: tenerli separati e' la
correzione, non un caso particolare.

Provato coi bordi delle colonne: destri identici su tutte le righe (232, 469,
753) e distacchi di 8 px esatti, con la somma che fa 744 = la larghezza
dell'area. Niente `colspan`/`rowspan` e niente bordi: dichiarato.

### L'OROLOGIO CHE SPARIVA AL PRIMO CLIC — non spariva, andava SOTTO

Segnalato cosi': «data e ora nella barra spariscono appena faccio clic».

! **LA PRIMA DIAGNOSI ERA SBAGLIATA, E L'A/B L'HA SMENTITA.** Avevo trovato che
`ex_procedura_base` ridipingeva la finestra su QUALUNQUE messaggio non gestito
(`case EXM_DISEGNA: default:` uniti) e concluso che fosse quello. Rimesso il
codice di prima, l'orologio **non spariva lo stesso**: il difetto era altrove.
La correzione al toolkit e' rimasta perche' e' giusta di suo — ogni movimento
del mouse sopra una finestra che non ascolta il mouse ne ripitturava il fondo e
faceva ricomporre lo schermo — ma non c'entra con questo.

Il difetto vero, trovato facendo stampare l'ordine di impilamento:

    creata idx=2 (orologio)   ordine: 0 1 2     orologio SOPRA la barra
                              ordine: 0 2 1     dopo un clic sulla barra

La barra delle applicazioni e l'orologio sono tutt'e due `WIN_ST_SOPRA` e si
sovrappongono **per disegno** — l'angolo destro della barra E' dell'orologio,
che e' un processo a parte. Al primo clic `in_cima()` portava avanti la barra,
e siccome il riordino tiene l'ordine RELATIVO fra le «sopra», da quel momento
la barra copriva l'orologio per sempre.

! **LA REGOLA NUOVA: UNA FINESTRA «SOPRA» GIA' IN PILA NON SI RIALZA.** Fra
loro l'ordine lo decide chi e' nato prima. Il fuoco si da' lo stesso — chi
clicca la barra deve poterci scrivere — quello che non deve cambiare e' CHI
COPRE CHI. Una «sopra» NUOVA passa e si aggiunge normalmente: e' cosi' che il
menu di avvio nasce davanti alla barra.

### «INFORMAZIONI SU» — lib/exinfo

Nome, a cosa serve, l'autore, l'indirizzo, e la memoria del processo. Su
browser (pulsante `?`), editor, file manager, e nella shell con `ver`.

! **LA MEMORIA DI UN PROCESSO NON LA DICE NESSUNA CHIAMATA, e va composta**:
`meminfo()` racconta la RAM della MACCHINA, `procinfo()` da' gli stack e basta.
Le tre parti si ricavano tutte — immagine da `_start` a `_bss_end`, heap da li'
(arrotondato alla pagina) a `sbrk(0)`, pila come `ustack_top - ustack_base`,
che sono le pagine DAVVERO toccate. Le librerie condivise non ci sono dentro, e
si dichiara: sono di tutti, e attribuirle a chi le apre le conterebbe una volta
per programma.

! **LA SHELL SE LA RIFA', E NON E' UNA DUPLICAZIONE PER SBADATAGGINE**: non si
collega alla libc apposta — e' il programma con cui si ripara un sistema, deve
partire anche senza `/lib/libc.so`. Di `exinfo.h` prende solo i `#define`, cosi'
l'autore e l'indirizzo restano scritti in un file solo.

! **E `SYS_PROCINFO` VUOLE QUATTRO ARGOMENTI**, il quarto e' `sizeof`: il
kernel rifiuta con EINVAL se non combacia. Con tre la chiamata falliva e la
pila si leggeva «0 KB» — un numero plausibile e sbagliato. Visto perche' zero
non e' possibile: uno stack toccato occupa almeno una pagina.

#### E il dialogo degli avvisi ha imparato due cose

`ex_dlg_avviso` disegnava i `\n` come glifi — in mezzo alla frase comparivano
due pallini — e si fermava a sei righe, troncando a meta' parola. Adesso un
`\n` e' un a capo voluto e le righe sono dodici. ! **UN DIALOGO CHE TRONCA E'
PEGGIO DI UNO CORTO**: quello che si legge sembra tutto.

! **E I TESTI SONO SOLO ASCII.** Il font della console non ha i trattini
lunghi: al loro posto compaiono tre caratteri di spazzatura in mezzo a una
frase, e si e' visto proprio qui.

#### Tre difetti di Makefile, tutti della stessa forma

1. **`$(WSERVER_OUT)` non era fra le prerequisite dell'ISO** — regressione del
   trasloco di wserver fuori dai driver: il CD non si rifaceva piu' quando il
   server cambiava, e si provava un binario vecchio credendolo nuovo.
2. **`EXINFO_SRC` non era fra le prerequisite** di browser, editor e file
   manager: cambiare exinfo.c non li ricostruiva.
3. **`EXINFO_HDR` era definita DOPO la regola della shell**, e con `:=` make
   espande subito: la shell aveva un prerequisito VUOTO. **Terza volta che
   questo Makefile inciampa nella stessa cosa** — dopo exwin.so e il blocco di
   wserver. Le definizioni adesso stanno in cima, col perche' accanto.

### `<hr>`, LE LISTE E `<pre>` — e uno dei tre stava nel posto sbagliato

`<hr>` era solo uno stacco: adesso e' una riga, disegnata come uno sfondo alto
due pixel, perche' e' esattamente cio' che e'. `<ul>` e `<ol>` hanno il loro
segno — e per `<ol>` il numero si conta fra i fratelli, non con un contatore
globale, o due liste annidate si darebbero i numeri a vicenda.

! **IL SEGNO DI UNA LISTA NON STA NELLA PAGINA**, e un pezzo sa indicare solo
un punto dell'arena del documento. Si scrive in CODA a quell'arena, dopo il
segno lasciato da `html_analizza`, e si riparte da quel segno a ogni
impaginazione — altrimenti la coda crescerebbe di un giro per volta, e la
pagina si reimpagina a ogni immagine che arriva.

#### `<pre>`: la correzione andava in exhtml, non nel browser

Il primo tentativo l'ho fatto nel browser, e non funzionava: **gli a capo non
gli arrivavano nemmeno**. `html.c` riduce a uno spazio qualunque sequenza di
bianchi — che e' la regola dell'HTML e va bene per tutto il resto.

! **DENTRO `<pre>` GLI SPAZI E GLI A CAPO SONO IL CONTENUTO**, ed e' tutta la
ragione per cui quel tag esiste. Ridurli nel lettore vuol dire che nessun
utilizzatore, per quanto attento, puo' piu' rimetterli: l'informazione e' persa
prima di arrivargli. La deve tenere chi analizza, perche' e' l'unico che ce
l'ha ancora. Quattro casi nuovi in `tools/prove/htmlprova.c` — 31 in tutto, 0
falliti.

! **E IL DISEGNO SI FERMAVA AL SOLO SPAZIO.** `disegna()` ricopia dall'arena
«fino allo spazio»: bastava finche' gli a capo non arrivavano fin li'. Dentro
`<pre>` arrivano, e venivano DISEGNATI — un rettangolino in coda a ogni riga.
Adesso ci si ferma a qualunque bianco.

Piu' il carattere a larghezza fissa per `<pre>`, `<code>`, `<tt>`, `<kbd>` e
`<samp>` — con un contatore e non un si'/no, perche' si annidano.

### L'ANNULLAMENTO NELL'EDITOR

! **SI TIENE IL TESTO INTERO, NON LE OPERAZIONI**, e per una volta la soluzione
grossolana e' quella giusta. Un elenco di operazioni e' piu' piccolo ma va
tenuto d'accordo con ogni cosa che modifica il testo: sbagliarne una vuol dire
un annullamento che ricostruisce un testo mai esistito, cioe' un difetto che si
scopre dopo aver perso del lavoro. L'area sta in 512 righe da 200 colonne, il
caso peggiore e' cento chilobyte: si copia e non si sbaglia.

Tetto su tutt'e due le cose — sedici passi e 192 KB — e quando si sfora si
butta il piu' vecchio.

Provato coi pixel: 14990 di testo, 1559 dopo Ctrl+A e Ctrl+X, 14949 dopo
Ctrl+Z. I 245 pixel di differenza rispetto a prima sono l'evidenziazione della
selezione, non il testo.

### `ls -l`, E DUE DIFETTI TROVATI SCRIVENDOLO

    statperm(percorso, &p)      modo, uid, gid — SYS_STATPERM (253)

! **UNA CHIAMATA IN PIU', NON UN CAMPO IN PIU' A `Stat`**, che e' la regola gia'
scritta accanto a `spawn_su_console`: cambiare una struttura che i programmi si
passano gia' vuol dire ricostruire tutto cio' che la usa, e `Stat` la usa
chiunque apra un file. `st_uid` e `st_gid` di `struct stat` restano a zero e
continuano a dichiararlo.

! **E LA TRADUZIONE uid -> NOME E' PASSATA NELLA libc**, perche' adesso gli
utenti sono due: stava dentro `/bin/id`, e `ls -l` ne avrebbe fatto una seconda
copia. Due copie di un parser di un formato di file divergono al primo campo
aggiunto. `nome_utente()`.

#### Il difetto grosso: `VfsStat` non inizializzata

Su un CD `ls -l` mostrava permessi diversi per ogni file e proprietari con
numeri a cinque cifre. Non era `ls`:

! **`stat_interno()` LASCIAVA `modo`, `uid` E `gid` A QUELLO CHE C'ERA NELLO
STACK.** Li scriveva solo il ramo ext2 (e il FAT generico, a zero); il ramo
ISO 9660, quello del floppy FAT12 e la RADICE non li toccavano affatto.

! **E NON ERA ESTETICA: `vfs_permesso()` DECIDE CON `st->modo`.** Zero vuol dire
«questo volume non ha proprietari, passa»; qualunque altra cosa vuol dire
«guarda i bit». Con `modo` casuale, **il permesso di leggere un file su un CD o
su un floppy si decideva in base a memoria non inizializzata** — a volte si', a
volte no, e niente lo faceva notare.

! **LA RIPARAZIONE VA ALL'INGRESSO, NON NEI DUE RAMI CHE MANCAVANO**: si azzera
tutta la struttura appena si entra, cosi' il PROSSIMO filesystem nasce giusto
senza che nessuno debba ricordarselo.

Adesso su ISO si legge `-????????? -  -`, che e' la verita' («questo volume non
ha proprietari»), e su ext2:

    drwxr-xr-x tizio    tizio    <DIR>   /home/tizio
    -rw------- root     root     175     /boot/ombra

#### E `vgaprova` veniva ESEGUITO a ogni installazione

`hwconfig -d` sonda ogni `*.drv` del catalogo lanciandolo con `-i`, e `install`
chiama `hwconfig`. `vgaprova` gli argomenti non li guardava nemmeno: veniva
eseguito per davvero — con lo schermo commutato in 320x200 nel mezzo — e usciva
con **0**, che per la sonda vuol dire «servo qui». Risultato:
`/dev/vgaprova.drv` finiva installato su ogni sistema.

! **E LA RISPOSTA GIUSTA E' NO, NON «SI'»**: quello non e' un driver. Non guida
niente e non serve nessuna periferica — e' una prova che si lancia a mano.
Stesso difetto di `wserver.drv`, stessa riparazione.

Per il resto il `-i` ce l'hanno tutti: `pci`, `svga`, `kbd`, `xhci`, i cinque
del CD, piu' `mouseser` e `uhci`. `tty` era in un elenco per sbaglio — non e'
un `.drv`, e' un pezzo compilato dentro il kernel.

### LA SCIA DEL CURSORE SULLA CONSOLE

Segnalato cosi': «ogni rigo termina con `_`, non causa problemi ma esteticamente
non e' gradevole; rimuoverlo causa problemi?».

! **SI', NE CAUSEREBBE: QUEL TRATTINO E' IL CURSORE.** In grafica il cursore e'
due righe di pixel in fondo alla cella — cioe' un trattino basso — e senza non
si vede piu' dove si sta scrivendo. Il difetto non era la sua presenza: era che
ne restava uno su OGNI riga.

Lo scorrimento della console, in grafica, non ridisegna tutto: fa scorrere i
pixel del framebuffer di una riga di celle, che e' la ragione per cui il
sistema arriva al prompt subito invece che in decine di secondi. Ma i pixel del
cursore erano gia' sullo schermo, quindi **salivano insieme al resto** — e la
riga dopo `g_cur_disegnato = 0` diceva «non c'e' nessun cursore da cancellare»,
che a quel punto era vero e inutile: il segno era gia' impresso.

Si cancella PRIMA di far scorrere, mentre e' ancora suo.

### LE REGIONI SPORCHE — un caso solo, e quello che costava

! **IL RITAGLIO STA NELLE DUE PRIMITIVE, NON NEI CHIAMANTI.** `px()` e
`riempi()` sono le sole due strade per arrivare al framebuffer, quindi tutto
cio' che disegna — cornici, prese, contorni, il puntatore — eredita il ritaglio
senza sapere che esiste. Metterlo dentro `componi()` vorrebbe dire ricordarselo
a ogni funzione nuova, e prima o poi qualcuno non se lo ricorda.

! **LA COPIA DELLA ZONA DEL CLIENT E' L'ECCEZIONE**, e ce l'ha per forza: non
passa dalle primitive apposta, perche' va per righe intere con MMX ed e' quello
che la rende veloce. Li' il ritaglio si applica a mano — colonne una volta
prima del ciclo, righe dentro — e c'e' scritto perche'.

! **IL PREDEFINITO E' «TUTTO», E VA TENUTO COSI'.** Una regione sporca
sbagliata per difetto lascia pixel vecchi sullo schermo: un difetto che non si
vede dove e' stato fatto e che si manifesta come «ogni tanto resta un pezzo di
finestra». Ogni ragione per ricomporre che non sappia dire ESATTAMENTE cosa ha
cambiato dichiara tutto lo schermo — sono dodici punti, e pagano quello che
pagavano prima.

! **SI E' STRETTO UN CASO SOLO, ED E' QUELLO CHE COSTA**: il puntatore e' 8x12
e si muove in continuazione. Tutto il resto — una finestra che si aggiorna, una
che si sposta, una che nasce — passa ancora da «tutto», e restringerlo e' un
lavoro a se' da fare un caso per volta guardando i pixel.

! **E LE DUE POSIZIONI VANNO SPORCATE TUTT'E DUE**: dove il puntatore ERA (per
cancellarlo) e dove E' (per disegnarlo). Sporcare solo la seconda lascia una
scia — lo stesso difetto che aveva il cursore della console, per un'altra
ragione.

#### Misurato, non stimato

    ricomposizioni durante il movimento del puntatore
        29 da   260 pixel
        17 da   240
         2 da   117
        39 da 480.000   (avvio, finestre nuove, pressioni di bottone)

cioe' **260 pixel invece di 480.000**, circa 1850 volte meno, per il caso che
si ripete a ogni movimento della mano.

! **E LO SCHERMO E' IDENTICO AL PIXEL.** Confronto fra il build di prima e
quello dopo, stessa scena e stessi movimenti: **32 pixel diversi, tutti dentro
una cella di carattere sola** — l'ultima cifra dell'orologio. La controprova:
due esecuzioni dello STESSO build differiscono nella stessa cella (26 pixel),
perche' fra una foto e l'altra passa un minuto. Fuori da quella cella: **zero
pixel diversi**.

### UNA CONSOLE CHE NON SI APRE ADESSO DICE PERCHE'

Se `elf_load` del programma di console fallisce — `/bin/login` troncato da
un'installazione andata male, un settore che non risponde — quella console
resta chiusa. Succede oggi, non in teoria.

! **E QUI MI ERO SBAGLIATO SCRIVENDO LA PRIMA VERSIONE**: avevo scritto che con
`verboseboot = 0` il klog non si vede e che quindi la console restava «nera e
muta». **Non e' vero**: `verboseboot = 0` abbassa il livello a `LOG_WARN`, non
lo spegne, quindi la riga c'era gia'. Verificato prima di scriverlo nel commit,
e il commento nel codice e' stato corretto.

Quello che mancava non era la NOTIZIA, era che dicesse qualcosa a chi la legge:

    [WARN] [PASSO 15] Console 0: caricamento di '/bin/login' fallito
    (err=-2; causa nella riga ELF: qui sopra, se stampata)

Chi si trova davanti una macchina che non si apre non sa cos'e' `err=-2` ne'
cos'e' il PASSO 15 — e la riga ELF a cui quel testo rimanda spesso non c'e',
perche' «file assente» e' sceso a LOG_INFO apposta. Adesso, sotto, c'e':

      Questa console non si apre: non riesco a caricare
        /bin/login
      Il file manca, e' troncato, o il disco non lo consegna.
      Avvia dal CD di EX-OS e reinstalla per rimetterlo a posto.

! **CHI LEGGE NON HA UNA SHELL**, quindi non gli si puo' dire «prova questo
comando»: gli si dice cosa manca e da dove ripartire, che e' l'unica azione che
ha davvero.

Provato per davvero: `/bin/login` cancellato da un sistema installato, avvio
dal disco, foto della console. Poi rimesso, e il disco riparte normale.

! **DECISO IL 19 AGOSTO: SI APRE UN RECOVERY COME root**, e la ragione e' che
il difetto E' nell'autenticazione. Una console che non riesce ad aprire `login`
non sta proteggendo niente: e' una console il cui sistema di autenticazione e'
CORROTTO. Lasciarla chiusa non difende — chi ha la macchina davanti avvia da CD
e monta il disco in trenta secondi — e toglie l'unico modo di ripararla da
dentro.

! **E LO DICE A CHIARE LETTERE PRIMA DI APRIRSI**: «questa shell gira come root
e NESSUNO ha fatto l'accesso». Non e' un accesso, e' una macchina rotta che si
apre per essere riparata.

Provato: `/bin/login` cancellato da un sistema installato, avvio dal disco, e la
shell si apre con il cartello e `whoami` che rende root.

### I DUE CONTI LI CREA L'INSTALLATORE — lib/exuser

Prima li creava `login` al primo avvio, e ne creava **uno solo**: root. Da li'
in poi si lavorava sempre da amministratore, che e' il modo in cui un errore
qualunque diventa un danno qualunque. Adesso l'installatore ne chiede due —
root per riparare, il tuo per lavorare (uid 1000) — con la casa gia' creata e
consegnata.

! **IL COMMENTO CHE STAVA IN QUEL PUNTO ERA VERO A META'.** Diceva che
l'installatore non puo' chiedere una password perche' servirebbe il modo raw
della tastiera e la console e' della shell che l'ha lanciato. Ma
l'installatore E' gia' il processo in primo piano — ci parla per chiedere
«Procedo?» — e la lettura senza eco adesso sta in `lib/exuser`, lo stesso
codice che usa login. Non c'era da inventare: c'era da spostare.

! **E TRE COPIE DI UN ARCHIVIO DI PASSWORD SONO TRE MODI DI DIVERGERE.** Il
codice e' stato SPOSTATO da login.c, non riscritto: riscrivere un archivio di
password vuol dire rifare gli stessi errori con numeri diversi. L'unica
aggiunta e' la `radice`, il prefisso dei due file, che serve all'installatore
per scrivere sul disco che sta preparando invece che sul proprio.

Provato: installazione da zero, i due conti chiesti e creati, poi avvio dal
disco e accesso come utente normale — `uid=1000(graziano)`, casa
`/home/graziano` di sua proprieta', e nessuna richiesta di «primo utente»
perche' l'archivio c'e' gia'.

### `sudo` ESEGUE UN COMANDO — e la shell si chiede a parte

Conseguenza diretta dei due conti: da adesso si lavora da utente normale, e
serve un modo di alzarsi a root conoscendo la password.

    sudo <comando> [argomenti]   esegue quel comando come root
    sudo -s                      apre una shell di root
    sudo                         non fa niente e spiega

! **ESEGUIRE UN COMANDO E APRIRE UNA SHELL NON SONO LA STESSA COSA DETTA IN DUE
MODI**, ed e' il motivo per cui la forma senza argomenti non apre piu' niente.
Con `sudo comando` i poteri durano quanto il comando e finiscono da soli; con
una shell durano finche' qualcuno si ricorda di uscire. La seconda si ottiene
— `-s` — ma bisogna CHIEDERLA. Dare la piu' pericolosa delle due a chi ha
battuto `sudo` e invio sarebbe il verso sbagliato.

! **CHIEDE LA TUA PASSWORD SE SEI AMMINISTRATORE, QUELLA DI root SE NON LO
SEI.** L'elenco e' `/boot/amministratori`, un nome per riga, 0644, scritto
dall'installatore che a fine installazione chiede se il conto principale deve
poterlo fare. Chi non e' in elenco non e' tagliato fuori: deve pero' sapere la
password di root, che e' un'altra cosa dal saper la propria.

! **IL PROGRAMMA `sudo` NON HA NESSUN POTERE**, e questa e' la parte che tiene
in piedi tutto il resto. Non e' setuid — il bit setuid sui file non esiste in
EX-OS, di proposito — e non decide niente: legge una password, la passa a
`SYS_SU` (254) e il KERNEL decide. Un `sudo` sostituito con un programma
qualunque non e' un'escalation, e' un programma qualunque.

! **E LA PASSWORD SI CANCELLA DAL BUFFER SUBITO DOPO L'USO**, prima dello
`spawn`. Il figlio eredita la memoria del padre solo se qualcuno gliela mette
in mano, ma un buffer che resta pieno e' un buffer che finisce in un dump.

#### Il difetto che e' saltato fuori: il kernel non poteva leggere `/boot/ombra`

`sudo` con la password GIUSTA rispondeva «non se ne fa niente». La spia messa
sui punti di uscita di `sys_su` ha detto tutto in tre righe: `/boot/utenti`
letto (28 byte), `/boot/ombra` **-1**.

! **E IL PERMESSO STAVA FACENDO IL SUO LAVORO.** `/boot/ombra` e' 0600 di
root, e DEVE restarlo — se fosse leggibile chiunque si porterebbe via le
impronte e le proverebbe con comodo su un'altra macchina. Ma `sys_su` gira nel
processo di CHI CHIAMA, uid 1000: il VFS guardava le credenziali del processo,
non di chi stava davvero leggendo, e diceva no. E' lo stesso identico motivo
per cui su Unix `su` e' setuid root.

La riparazione e' `vfs_open_autorita()`, in `kernel/fs/vfs.c`: lo stesso
`apri_nl()` con in piu' un parametro `autorita` che salta il controllo dei
permessi.

! **NON E' UN BIT DENTRO `flags`, ED E' UNA DIFFERENZA DI SOSTANZA.** Un flag
arriverebbe da `sys_open`, cioe' da un numero scelto dall'utente: basterebbe
indovinarlo per leggersi le password. Cosi' invece l'unico modo di ottenere
l'autorita' e' chiamare un simbolo C che nessuna syscall espone.

! **E NON E' UNO STATO GLOBALE**, che era la prima idea e sarebbe stata la
peggiore. Una variabile «adesso il kernel ha i poteri» accesa attorno alla
lettura varrebbe per TUTTI i processi finche' e' accesa, e il VFS qui dentro
riscadenza — i driver stanno in ring 3, una lettura e' un IPC. Un altro
processo che entrasse in quella finestra si troverebbe root senza averlo mai
chiesto. Il permesso viaggia come argomento e finisce con la chiamata.

! **IL CONTROLLO SI FA UNA VOLTA SOLA, ALL'APERTURA.** `vfs_read` non
ricontrolla — regola di Unix — quindi l'autorita' serve solo attorno a
`vfs_open` e non attorno alla lettura.

#### L'ambiente del figlio dice root

`id` avvisava: «$USER dice 'graziano', il kernel dice 'root'». Adesso `sudo`
costruisce l'ambiente del figlio con `spawn_ex`, riscrivendo `USER` e
`LOGNAME`.

! **`HOME` SI CAMBIA SOLO CON `-s`.** `sudo un-comando` deve poter lavorare sui
file di chi lo chiama — e' quasi sempre il motivo per cui lo si chiama —
mentre con `-s` si e' chiesto proprio di andare dall'altra parte. E non e'
pedanteria: un programma che si fida di `$HOME` scriverebbe i file di root
dentro la casa dell'utente, di proprieta' di root, e da domani quello non puo'
piu' cancellarseli.

#### Provato, tutti e cinque i casi

Installazione da zero su ext2 con `graziano` amministratore, poi avvio dal
disco e accesso come `graziano`:

- `sudo` da solo — stampa l'uso e non apre niente;
- `sudo id` con la password di graziano — `uid=0(root) gid=0`, e `id` non
  avvisa piu' di nessuna discordanza;
- `sudo id` con una password sbagliata — rifiutato;
- `sudo -s`, poi `id` (root), poi `exit` e di nuovo `id` — `uid=1000`: i
  poteri finiscono davvero;
- `trunc /boot/amministratori 0` per togliersi dall'elenco, poi `sudo id` —
  chiede la password di **root**, rifiuta quella di graziano (e il kernel
  registra «PID 12 (uid 1000) ha sbagliato la password di 'root'»), accetta
  quella di root.

! **UNA COSA CHE E' COSTATA MEZZ'ORA DUE VOLTE: `cp /boot/kernel.bin` SUL
DISCO MONTATO NON BASTAVA.** Il kernel nuovo non partiva e le prove giravano
su quello vecchio, con i sintomi identici a un difetto non riparato. Il modo
che funziona e' `install -a /disco` dal CD.

### TUTTE LE STRINGHE DEL KERNEL SONO ASCII

Sessantotto stringhe contenevano trattini lunghi, virgolette basse, lettere
accentate e caratteri di riquadro. Il font della console e' indicizzato per
BYTE: un carattere UTF-8 da tre byte diventa tre glifi di spazzatura in mezzo a
una frase. Si vedeva nel messaggio del recovery che avevo appena scritto — e
c'era gia' in cinquanta punti, compresi i riquadri del **kernel panic** e del
**page fault**, cioe' le due schermate che contano proprio quando tutto il
resto e' andato storto.

## Difetti aperti, dichiarati

- il **blocco temporaneo** della memoria condivisa non c'e': serve solo quando
  ci sara' uno swapper, e il conteggio dei riferimenti — la parte che non si
  puo' aggiungere dopo — e' fatto;
- su ferro vero con una VESA attiva, il ritorno al modo testo puo' non
  bastare: manca l'interfaccia in modo protetto di VBE 2.0;
- niente NX (vuole PAE, cioe' Pentium 4 in avanti: fuori dal bersaglio),
  SHA-256 senza irrobustimento della chiave, nessuna quota di memoria o disco;
- ~~un utente normale non puo' avviare la pila grafica~~ — **RISOLTO il 19
  agosto**, e la riparazione non e' quella che sembrava. Vedi la sezione «Il
  server a finestre non e' un driver» qui sopra.

- ~~`login -a` stampa «Sistema nuovo: non c'e' ancora nessun utente» anche
  quando gli utenti ci sono gia'~~ — **RISOLTO il 24 agosto 2026**, e sotto il
  messaggio falso c'era di peggio: vedi la sezione «`login -a` diceva il falso»
  qui sopra.

- ~~le stringhe dei programmi non sono ASCII, e la console indicizza il font per
  BYTE~~ — **RISOLTO il 24 agosto 2026**: 93 letterali in 29 file fra `bin/` e
  `drivers/`. Vedi la sezione «Novantatre stringhe che sulla console non si
  leggevano» qui sopra.

## COME SI PROVA QUELLO CHE C'E' (aggiornato il 20 agosto 2026)

    make -j2 all && make iso-exos
    python3 tools/qemu_drive.py "libctest@260"     321 prove, e ci sono dentro
                                                   pty e interruzione. Da CD
                                                   (EXOS_NO_FLOPPY=1) sono
                                                   196 con 15 fallite: manca
                                                   dove scrivere, non e' una
                                                   regressione
    tools/prova_ssh.sh                             una sessione SSH vera, con
                                                   OpenSSH dell'ospite — e
                                                   dentro c'e' window-change
    tools/prova_telnet.sh                          telnet vero, e NAWS
    tools/prova_ridimensiona.sh                    le finestre che cambiano
                                                   misura, misurate nei pixel
    tools/prova_doppioclic.sh                      il doppio clic e il «+»
                                                   dell'albero, nel file
                                                   manager e nel dialogo Apri
    /exwin/bin/fontprova                           sei facce TrueType a sei
                                                   corpi, dentro EX-OS

Per i conti e `sudo` serve un disco installato, quindi ext2 e non il floppy:

    dd if=/dev/zero of=/tmp/exos/hd.img bs=1M count=64
    DISCO="-drive file=/tmp/exos/hd.img,format=raw,if=ide,index=0"
    EXOS_QEMU_EXTRA="$DISCO" EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso \
      python3 tools/qemu_drive.py "fdisk hd0@6" ... "install -t /disco@330" ...

! **PER RIPROVARE CON UN KERNEL NUOVO SU QUEL DISCO SI USA `install -a
/disco`**, non `cp /boot/kernel.bin`: la copia non fa ripartire il kernel
nuovo, e le prove girano su quello vecchio senza dirlo.
    python3 tools/righe_lista.py f.ppm X Y W N     quante righe ha una lista e
                                                   quale e' scelta, contando
                                                   l'inchiostro
    python3 tools/misura_finestre.py foto.ppm      quanto e' grande una
                                                   finestra, DENTRO una foto
    EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso \
      python3 tools/qemu_drive.py "crypttest@50"   i vettori degli RFC

    # due macchine e un cavo virtuale (listen/accept, telnet)
    EXOS_ISTANZA=srv EXOS_MAC=52:54:00:12:34:01 EXOS_NET_SOCKET=listen:12345 ...
    EXOS_ISTANZA=cli EXOS_MAC=52:54:00:12:34:02 EXOS_NET_SOCKET=connect:12345 ...

! **UNA MACCHINA SOLA PER VOLTA, E SI CONTROLLA PRIMA.** Un QEMU rimasto acceso
tiene il file del log e la porta: si finisce per leggere l'output di un giro
vecchio credendolo nuovo. Il 18 agosto e' costato mezz'ora — sembrava che
l'entropia fosse deterministica, e invece era la macchina che non era ripartita.
`pgrep -a qemu-system` prima di ogni prova, e `tools/prova_ssh.sh` lo fa da se'.

! **IL PUNTATORE SI PILOTA, MA A PASSI PICCOLI**, e il 18 agosto qui c'era
scritto il contrario. Si perdono i movimenti relativi GRANDI: a dieci pixel per
volta il puntatore arriva dove lo si manda, **al pixel** — misurato. La ricetta
sta in `tools/prova_ridimensiona.sh` ed e' di due righe: si satura in un angolo
con qualche `mouse_move -600 -600` (li' il troncamento non fa danno, si sbatte
contro il bordo) e da li' si conta a passi di dieci. Le prove grafiche che
dipendono da dove si clicca sono ripetibili.

Per guardare una fotografia: `python3 tools/ppm2png.py foto.ppm out.png`. Per
contarci le finestre: `python3 tools/misura_finestre.py foto.ppm`; `--presa` da'
la presa d'angolo e `--client` l'angolo dell'area del client, che e' da dove si
contano le coordinate dei controlli.

! **PIU' COMANDI AL MONITOR DI QEMU DENTRO UN `mon:` SOLO, separati da «;»**,
quando l'intervallo fra loro conta — un doppio clic. Un `mon:` per conto suo
costa **piu' di due secondi**: non per il `settle`, ma perche' `Monitor.drain()`
aspetta il timeout del socket. Ci sono volute due ore per scoprirlo, e nel
frattempo sembrava un difetto del sistema provato.

---

# IL BROWSER, E LA STRADA PER ARRIVARCI — 19 agosto 2026

Sette commit, da 601c88e a c206fb2, che sono un arco solo: l'orologio, l'HTTP,
`ipc_rimetti`, il browser, il `rename` allineato, i README. Qui c'e' solo cio'
che non si ricava leggendo il codice.

## L'ordine non era negoziabile, e la ragione e' una sola

! **`strlen(s) * 8` E' IL CONTO DA TOGLIERE PRIMA CHE QUALCUNO NE SCRIVA UN
ALTRO SOPRA.** I font sono venuti prima del browser per questo: un motore
d'impaginazione nato su quel presupposto andrebbe riscritto il giorno che si
sceglie un proporzionale — cioe' subito. Nel browser l'impaginazione chiama
`ex_larghezza_testo`, e funziona identica col bitmap e col TrueType.

! **E `ipc_rimetti` E' VENUTO PRIMA DEL BROWSER PER LA STESSA FORMA DI
RAGIONE.** La mailbox e' una sola per processo: chi aspetta una risposta dello
stack IP scorre i messaggi, e prima BUTTAVA quelli degli altri. In un comando
di console non si nota; in una finestra quei messaggi sono i clic dell'utente.

## Le trappole, che sono la parte che vale

! **`Monitor.drain()` DI qemu_drive COSTA DUE SECONDI A COMANDO**, e non per il
`settle`: legge dal socket del monitor finche' non va in timeout, e il timeout
e' di due secondi. Un doppio clic mandato come quattro `mon:` separati arriva a
quattro secondi di distanza — cioe' non e' un doppio clic, qualunque soglia si
scelga. Due ore per scoprirlo, e nel frattempo sembrava un difetto del sistema
provato. Rimedio: piu' comandi dentro un `mon:` solo, separati da «;».

! **`timeout_ms == 0` VUOL DIRE ATTESA SENZA SCADENZA**, non «non aspettare» —
lo dice libc.h. Scritto zero credendo il contrario, la funzione che svuota la
posta si e' piantata per sempre alla prima mailbox vuota. Il sintomo era
«scarica non risponde piu'».

! **UN SISTEMA INSTALLATO NON ARRIVA MAI AL PROMPT DELLA SHELL**: si ferma su
«nome utente:» finche' non si e' creato il primo utente. `qemu_drive`
aspettava i marcatori d'avvio, rinunciava dopo novanta secondi e non mandava
MAI i comandi — e il sintomo era «la macchina non risponde». Adesso c'e'
`EXOS_MARCA` per scegliere la riga da aspettare. Senza quella riga la prova del
salvataggio su ext2 non si poteva fare.

! **LO SCAFFALE DI `ipc_rimetti` SI SERVE PRIMA DELLA CODA DEL KERNEL**, quindi
rimettere un messaggio e rileggere subito rende lo STESSO messaggio
all'infinito. Cio' che non e' nostro si mette da parte e si restituisce alla
FINE, nell'ordine di arrivo. E cio' che viene dallo stack ma non e' il tipo
atteso si butta lo stesso: e' una risposta vecchia alla nostra domanda, e
rimetterla vorrebbe dire ritrovarsela davanti alla prossima.

## Il pcap che ha chiuso una caccia sbagliata

Il sintomo: la redirezione rendeva «non riesco a connettermi (-104)», cioe'
ECONNRESET. Ho incolpato prima un messaggio rimasto in mailbox, poi una corsa
nella chiusura: **sbagliate tutt'e due**.

Trenta secondi di cattura, e il pacchetto 18 diceva tutto: il SYN se ne andava
a `10.0.2.2:80` invece che a `:8099`. Ricostruendo l'indirizzo assoluto da una
`Location` relativa mettevo host e percorso e **non la porta**.

! **LA LEZIONE NON E' SUL BUG, E' SUL METODO.** Due ipotesi ragionevoli e
sbagliate contro trenta secondi di misura. Lo stesso e' successo col
salvataggio: «non salvato» non diceva niente finche' non ho fatto stampare
QUALE passo fallisce e con quale errno — «non rinomino (17)», e da li' e' stato
immediato.

## `rename` e la finestra che solo il kernel puo' chiudere

! **«CANCELLA PRIMA» NON E' EQUIVALENTE A UNO SCAMBIO CHE SOSTITUISCE.** Lo
schema per salvare senza rischiare di perdere e' uno solo: scrivi accanto, poi
scambia. Se lo scambio non sostituisce bisogna cancellare prima, e fra le due
cose il file NON ESISTE. `vfs_rename` tiene il lucchetto del filesystem per
tutta l'operazione: in spazio utente quella garanzia non si puo' avere.

! **E LA SOSTITUZIONE STA NEL VFS, PRIMA DELLO SMISTAMENTO**, quindi vale per
ext2, FAT12 e FAT16/32 senza che nessun driver la reimplementi. Su questo mi
ero sbagliato dicendo che fat12 teneva il comportamento vecchio: avevo guardato
lo smistamento e non dove stava il blocco. Provato su tutt'e tre.

! **E LA PROVA E' RIMASTA AL CONTRATTO DI PRIMA — quattro giorni, con la riga
rossa sotto gli occhi.** `/bin/libctest` chiedeva ancora che `rename` FALLISSE
con `EEXIST` su una destinazione occupata, cioe' la regola cambiata il 20
agosto. Il conto diceva «315 prove su 316», e il `[FALLITO]` accusava il kernel
di un difetto che era della prova: verificato a mano dentro EX-OS che
`rename a b` con `b` gia' presente riesce e lascia in `b` il contenuto di `a`,
su FAT12 come su ext2.

! **UNA PROVA CHE SBAGLIA COSTA PIU' DI UNA PROVA CHE MANCA.** Quella riga
rossa era in fondo a una schermata che scorre, e chi la vedeva imparava a
saltarla — che e' il modo in cui la volta dopo si salta anche quella vera.
Adesso la prova chiede il contratto nuovo, e in piu' due casi che prima
nessuno guardava: che il nome di partenza sparisca insieme al contenuto, e che
`rename(x, x)` NON cancelli `x` — sostituire vuol dire togliere di mezzo la
destinazione, e li' la destinazione e' il file stesso. **321 su 321.**

## Come si prova la catena della rete

    EXOS_QEMU_EXTRA="-netdev user,id=n1 -device ne2k_pci,netdev=n1" \
    EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso \
    python3 tools/qemu_drive.py "netdetect -c@12" "scarica http://example.com@25"

    cc -o /tmp/httpprova tools/prove/httpprova.c lib/exhttp/http.c -I lib/exhttp
    cc -o /tmp/htmlprova tools/prove/htmlprova.c lib/exhtml/html.c -I lib/exhtml

39 casi per l'HTTP, 30 per l'HTML, tutti sull'host e tutti contro casi scritti
a mano — perche' i difetti che contano stanno nei documenti MALFATTI, e quelli
si scrivono, non si trovano.

## Il giro completo per provare su un disco installato

Serve quando cio' che si prova ha bisogno di un filesystem SCRIVIBILE: da CD
non c'e' niente di scrivibile, e il floppy e' FAT dove i nomi lunghi sono in
sola lettura.

    dd if=/dev/zero of=/tmp/exos/hd.img bs=1M count=64
    fdisk hd0 -> n, Invio, Invio, Invio, 83, w, «si», q
    mkfs -t ext2 -L SISTEMA hd0p1 -> «si»
    mount hd0p1 /disco        (il punto di montaggio NON deve esistere)
    install -t /disco
    poi si avvia dal disco con EXOS_MARCA="utente:"

! **LE CONFERME VOGLIONO LA PAROLA `si` INTERA**, non «s». E il kernel sul
disco e' quello installato: cambiando il kernel bisogna RIFARE `install`, non
basta copiare i binari.


# I FONT: DAL BITMAP AL TRUETYPE — 19 agosto 2026

Tre commit di seguito, che sono un arco solo: 2d316cb porta il livello dei font
e i file sull'ISO, d316875 il rasterizzatore e la resa a schermo.

## Perche' PRIMA del browser e non dopo

! **`strlen(s) * 8` E' IL CONTO DA TOGLIERE PRIMA CHE QUALCUNO NE SCRIVA UN
ALTRO SOPRA.** E' vero finche' il font e' uno e a larghezza fissa: un motore
d'impaginazione nato su quel presupposto andrebbe riscritto tutto il giorno che
arriva un font proporzionale. Sostituito in sei punti del toolkit.

! **IL FONT DI SISTEMA E' DESCRITTO DALLA STESSA STRUTTURA DI QUELLI CHE SI
CARICANO**, e questo tiene UNO SOLO il ciclo che accende i pixel. Con due
strade la seconda sarebbe nata copiando la prima. L'8x16 e' semplicemente un
font fisso largo 8 e alto 16 che sta in memoria invece che su disco; la sua
linea di base e' 14, MISURATA dai glifi (A, H, X finiscono a riga 13, la coda
di Q scende a 14).

## Dove sta cosa, e perche' la divisione e' li'

    lib/exfont/exfont.c    il lettore dei font BITMAP (formato EXFN)
                           -> DENTRO exwin.so: lo usano tutti
    lib/exfont/ttf.c       il contenitore TrueType
    lib/exfont/raster.c    contorni -> copertura
    lib/exfont/exfont_ttf.c  istanza e cache
                           -> in exfont.so, a 0x05000000, aperta A RICHIESTA

! **IL CRITERIO E' «UN PROGRAMMA NON CARICA CIO' CHE NON USA», E VA APPLICATO
NEL VERSO GIUSTO.** Tutti i programmi grafici scrivono del testo e caricano
gia' exwin.so: mettere il lettore bitmap in un .so a parte avrebbe fatto aprire
a ognuno una libreria in piu' per centocinquanta righe. Il TrueType invece —
contenitore, curve, rasterizzatore, cache — e' «qualcosa che COSTA», e un
orologio non ne apre uno mai.

## Le regole del rasterizzatore

Interi in 26.6; riempimento a CONTEGGIO NON NULLO (la parita' sbaglierebbe i
glifi con tre contorni annidati); sedici campioni in verticale ed esatto in
orizzontale; la piattezza di una curva si MISURA sul punto di controllo invece
di spezzarla a passi fissi.

! **NON ESISTE LA DIVISIONE A 64 BIT.** Una libreria di EX-OS si collega senza
libgcc: `__divdi3` non c'e' e il collegamento FALLISCE — lo stesso muro di
tsc.c. Le moltiplicazioni lunghe invece vanno bene, sono due istruzioni. I due
conti che ne avevano bisogno stanno in 32 bit con la loro ipotesi scritta
accanto: la scala perche' il corpo ha un tetto, l'incrocio perche' le
coordinate si limitano e perche' il prodotto si fa SENZA SEGNO.

## Provato contro FreeType, e non contro me stesso

`tools/prove/ttfprova.c` gira SULL'HOST — ttf.c e raster.c non includono la
libc apposta — e chiama FreeType via ctypes per confrontare pixel per pixel.
4 font, 5 corpi, 73 caratteri:

    riquadro IDENTICO         1460 su 1460   (100%)
    differenza media/pixel    0,94 livelli su 255   (0,37%)

E la cmap contro `fc-query --format %{charset}`: Sans 2327 codici, Serif Bold
2321, Mono 2305, zero differenze nei due versi. hmtx contro l'invariante del
monospazio: Mono da' 1229 per tutti e 94 i glifi ASCII, Sans ne da' 82 diversi.

! **IL CONFRONTO HA TROVATO UN DIFETTO CHE NESSUNA IPOTESI AVREBBE TROVATO.**
Alla prima passata i riquadri identici erano il 95,4%, e i mancanti avevano una
FORMA: a 16 pixel quasi ogni maiuscola veniva alta un pixel meno. La scala
troncava invece di arrotondare. Mezzo sessantaquattresimo perso che diventa un
pixel intero — il genere di cosa che «sembra giusta» guardandola.

## Tre trappole trovate, da non ricalpestare

! **`x << 6` SU UN NEGATIVO E' COMPORTAMENTO INDEFINITO**, e i negativi ci
sono: la 'j' comincia a sinistra della penna, un glifo tutto sotto la linea di
base ha la cima negativa. Trovato da UBSan. E la divisione fra interi tronca
verso ZERO: -1/64 fa 0, non -1, cioe' mezza lettera tagliata su un bordo a -0,5.

! **LA MISURA DI UN FILE SI CHIEDE AL FILE.** `ex_font_apri` aveva un tetto di
256 KB copiato da `ex_immagine()`, e i Liberation pesano da 280 a 420: si
leggevano TRONCATI e non se ne apriva nessuno. Il sintomo non somigliava alla
causa — «nessun font si carica», non «buffer piccolo».

! **FONDERE VUOL DIRE LEGGERE IL PIXEL CHE C'E' GIA'**, e prima di oggi il
disegno di questo toolkit non leggeva mai niente. Conseguenza da ricordare:
cio' che sta sotto una lettera dev'essere gia' disegnato quando la lettera
arriva. La divisione per 255 non si fa: l'identita' usata e' ESATTA, verificata
su tutte le combinazioni, errore massimo zero livelli.

## I font sull'ISO

Dodici facce Liberation 2.1.5 in `exwin/font/`, col LICENSE accanto — la OFL
obbliga a spedirlo, e tenerlo nella stessa directory e' cio' che rende
impossibile dimenticarlo. `IMPRONTE.txt` fissa le SHA-256: senza, due macchine
costruirebbero due ISO diverse senza dirlo. I SORGENTI dei font non entrano in
git (`.gitignore`): andranno sull'ISO degli strumenti, accanto a quelli di GCC
e FreeBASIC. Il perche' per esteso in `exwin/font/leggimi.md`.

Dodici e non sedici e' un numero RICAVATO: tre famiglie per quattro stili e'
esattamente cio' che una pagina HTML puo' chiedere.

## Come si prova

    /exwin/bin/fontprova            sei facce a sei corpi, dentro EX-OS,
                                    con una riga bianca su blu per la fusione
    cc -o /tmp/ttfprova tools/prove/ttfprova.c lib/exfont/ttf.c \
       lib/exfont/raster.c -I lib/exfont
    /tmp/ttfprova <font.ttf> [riassunto|charset|monospazio|disegna|pgm]

## Cosa manca ai font

    l'hinting            niente. A 12-14 pixel Liberation e' piu' MORBIDO del
                         bitmap 8x16: per questo l'interfaccia usa ancora
                         l'8x16, e il TrueType e' per i documenti e il browser
    il crenatura         `kern` e GPOS non si leggono: le coppie come «AV»
                         restano un po' larghe
    le legature          GSUB non si legge
    oltre il piano base  solo cmap formato 4, cioe' fino a FFFF. Il sistema
                         dichiara Latin-1, quindi non morde
    CFF / OpenType       un file «OTTO» si RIFIUTA apposta: contorni cubici in
                         un formato diverso, con un suo interprete PostScript.
                         Meglio «non lo so leggere» che leggere male


# IL DOPPIO CLIC, E IL «+» DELL'ALBERO — 18 agosto 2026

Chiesto: nel dialogo «Apri»/«Salva con nome», una directory scelta deve aprirsi
con l'Invio o col doppio clic; nel file manager, il doppio clic deve aprire, e
il segno «+» dell'albero deve rispondere a un clic solo.

## Cosa dice `lp` adesso, per un EXM_COMANDO che viene da una lista

    EX_APRIRE(lp)   1 = si e' chiesto di APRIRE: Invio, oppure doppio clic
    EX_COL(lp)      la colonna del clic dentro la riga, in caratteri,
                    oppure -1 se il comando e' arrivato dalla tastiera

! **LA COLONNA SERVE A CHI DISEGNA DENTRO LA RIGA.** Una lista e' testo, e
un'applicazione ci mette dei segni con un significato suo — il «+» e il «-»
dell'albero. Senza sapere DOVE e' caduto il clic, quel segno si potrebbe solo
guardare, mai premere. Il toolkit non sa cosa significhino quei caratteri, e non
deve saperlo: dice la colonna e basta. Il file manager conta 2 caratteri per
livello e sa che il segno del nodo di livello n sta nella colonna 2n.

! **DALLA TASTIERA LA COLONNA E' -1, NON ZERO.** Zero e' una colonna vera, la
prima: confonderla con «non c'e'» vorrebbe dire un Invio che si comporta come un
clic sul primo carattere — cioe' sul segno, sempre.

## Il doppio clic lo riconosce il TOOLKIT, ma con l'ora del SERVER

Il server manda pressioni e rilasci; quanto vicini debbano essere due clic per
contare come uno e' una convenzione dell'interfaccia, non un fatto
dell'hardware. Sta percio' in `exwin.c` (400 ms, 4 pixel), non nel server: cosi'
non c'e' un messaggio in piu' per ogni clic in una mailbox profonda quattro, e
ogni programma ne ha una copia sua.

! **MA L'OROLOGIO NON PUO' ESSERE QUELLO DEL CLIENT.** La prima versione
chiamava `uptime_ms()` quando l'evento veniva LETTO. Fra un clic e l'altro pero'
il client ha lavorato — al primo clic su una directory il file manager la LEGGE,
e da un CD sono decimi di secondo — e quel lavoro finiva dentro l'intervallo
misurato. Adesso `WinEvento` porta il campo `tempo`, scritto dal server
nell'istante in cui l'evento nasce.

## Il pomeriggio buttato, e cosa lo ha causato

Per due ore la prova ha detto «il doppio clic non fa niente», e i pixel non
mentivano: le due pressioni arrivavano alla macchina a **quattro secondi** l'una
dall'altra. La colpa non era del sistema provato ma di `tools/qemu_drive.py`:
`Monitor.drain()` legge dal socket del monitor finche' non va in **timeout**, e
il timeout e' di **due secondi**. Ogni `mon.cmd()` costa percio' due secondi
buoni, qualunque `settle` gli si passi.

Misurato per gradi, e vale la pena tenerlo: strumentando il driver `kbd`, il
server e il toolkit si e' visto che il pacchetto PS/2 arriva al driver, e da li'
al server, in **10-30 ms** — la pila di EX-OS non c'entrava niente. `componi()`
a 800x600 costa **0-20 ms**, non secondi: anche quel sospetto era sbagliato.

Rimedio: `Monitor.rapidi()`, che scrive i comandi uno dietro l'altro **senza
drenare** in mezzo. Nella sintassi degli argomenti, piu' comandi dentro un
`mon:` solo separati da «;» passano di li'.

## Cosa c'e' adesso per provarlo

`tools/prova_doppioclic.sh` — due parti, tutte e due misurate nei pixel:

  1. **il file manager**: clic semplice sceglie e non apre; doppio clic apre;
     clic sul segno apre e richiude;
  2. **il dialogo «Apri»** dell'editor: clic sceglie, Invio entra, «Su» risale,
     doppio clic entra di nuovo — e l'elenco del passo 4 e' NUMERICAMENTE
     identico a quello del passo 2.

`tools/righe_lista.py` conta l'inchiostro riga per riga dentro una lista: riga
vuota 0, riga con testo qualche centinaio, riga scelta qualche migliaio. Non
riconosce i glifi e non deve.

`tools/misura_finestre.py --client foto.ppm` da' l'angolo dell'area del client,
come `--presa` da' la presa: da li' in poi le coordinate dei controlli sono
numeri fissi che stanno nel sorgente dell'applicazione.

! **UNA TRAPPOLA DA NON RIFARE**: `passi_a` in `prova_ridimensiona.sh` va in
diagonale per quanto e' ALTO il bersaglio e poi in orizzontale. Con un bersaglio
piu' in basso che a destra — il segno «+», vicino al bordo sinistro — la
diagonale supera la x e il resto viene negativo; il ciclo bash gira zero volte e
il puntatore finisce quaranta pixel piu' in la', dentro il nome invece che sul
segno. Corretto in tutt'e due gli script.

! **E LA DIRECTORY SU CUI SI PROVA DEVE AVERE DELLE FIGLIE.** L'albero mostra
solo directory: aprire `/bin` — tanti file, nessuna directory — cambia il segno
e nient'altro, cioe' qualche decina di pixel che si confondono col rumore.
`/exwin` ha `bin` e `lib` dentro, e aprirla sposta in giu' tutto il resto.

## Altre due cose sistemate per strada

`rimappa()` in `exwin.c` buttava via la posizione che il server manda dentro
`WIN_MSG_MISURATA`: la finestra sapeva la misura nuova e la posizione vecchia.

Un trascinamento dentro una lista muoveva la barra della scelta senza dirlo a
nessuno. Adesso al rilascio parte un EXM_COMANDO, ma **solo se la riga e'
davvero cambiata**: dirlo a ogni riga attraversata vorrebbe dire, in un file
manager, rileggere una directory per ogni voce sfiorata dal puntatore.

# I 64 MB: un sospetto vecchio di un giorno, chiuso provandolo (18 agosto 2026)

Stava scritto come «trovato leggendo, MAI VERIFICATO»: `paging_init` mappa per
identita' tutta la RAM fino a `total_ram` **senza cappare a `USER_SPACE_BASE`**,
quindi con piu' di 64 MB quella mappatura entra nella fascia utente. Le prove
girano tutte a 32 MB, percio' non si era mai visto.

Provato: `libctest` a 128 MB e a 32 MB da' **196 superate e 15 fallite in
tutt'e due i casi**, cifra per cifra — e le 15 sono scritture su un CD in sola
lettura, non memoria.

! **NON MORDE PERCHE' LA FASCIA UTENTE NON VIENE COPIATA.**
`paging_create_directory` copia nelle directory dei processi solo le PDE
`0..PD_INDEX(USER_SPACE_BASE)-1`, cioe' 0..15. Le mappature sopra i 64 MB
restano nella sola `kernel_page_directory`, che nessun processo utente usa mai.

Resta un'inesattezza, non un difetto: il cappo non c'e'. Metterlo costa una
riga e toglie una domanda a chi legge — ma non c'e' niente da riparare.


# Il rilievo, i menu, e un difetto del kernel vecchio di mesi (18 agosto 2026)

Tre cose chieste insieme — l'aspetto in rilievo stile Workbench, i menu a
tendina, il file manager a due aree — e in mezzo, trovato inseguendo un
sintomo, **il difetto piu' grave di questa giornata**, che non era in nessuna
delle tre.

## ! IL DIFETTO DEL KERNEL: APRIRE DUE VOLTE UNA LIBRERIA LE AZZERA I DATI

    lib_apri()  ->  aggancia()  ->  per ogni pagina scrivibile: COPIA FRESCA

`aggancia()` rimappa ogni pagina della libreria e, per quelle scrivibili,
prende una copia nuova dell'originale. Chiamata **due volte sullo stesso
processo** riporta `.data` e `.bss` al valore che avevano alla compilazione:
per `exwin.so` vuol dire **la tabella delle finestre azzerata**, con le
finestre ancora vive sullo schermo e il server che continua a mandare loro gli
eventi.

! **E NON SERVE CHE QUALCUNO APRA NIENTE DUE VOLTE DI PROPOSITO.** Basta
un'applicazione che usi `exwin.so` **ed** `exdlg.so`: exdlg.so a sua volta e'
legata a exwin.so, e al primo dialogo il suo stub la apre — che per quel
processo e' la seconda volta. Cioe' succedeva a `edit` da sempre, alla prima
finestra «Apri...».

! **IL SINTOMO NON SOMIGLIAVA A UN DIFETTO DI MEMORIA.** La finestra restava
disegnata, riceveva i tasti, il programma continuava a girare e a stampare — e
non cambiava piu' un pixel. Nessun fault, nessun errore, nessun messaggio.
Quello che si vedeva era «il file manager si e' bloccato dopo la ricerca», e la
ricerca non c'entrava niente.

Come si e' trovato, ed e' il metodo che vale piu' del difetto:

    1. l'applicazione dice di aver ridisegnato        (log sulla seriale)
    2. il server non riceve nessun WIN_MSG_AGGIORNA   (log sulla seriale)
    3. dentro exwin.so, ex_aggiorna() SCARTA il messaggio: radice(f) e' NULL
    4. la maniglia della finestra e' 1, e la 1 e' libera
    5. l'ha liberata ex_distruggi() del DIALOGO, che aveva anche lui la 1

Il passo 4 e' quello che ha girato la domanda: se il dialogo ha preso la stessa
maniglia della finestra principale, le due non stanno guardando la stessa
tabella. E la tabella e' unica per processo — a meno che qualcuno non l'abbia
riazzerata.

! **IL RIMEDIO NON HA VOLUTO UN ELENCO NEL PCB.** La domanda «questo processo
ce l'ha gia'?» la risponde la sua stessa tavola delle pagine: se l'indirizzo
della tabella di esportazione e' gia' mappato, la libreria e' gia' li'.
`paging_get_physical()` e tre righe in `lib_apri()`.

! **E VA GUARDATO ANCHE DA UN'ALTRA PARTE**: era anche un modo, per chi
controlla un percorso di libreria, di **azzerare lo stato di un'altra libreria
condivisa** dello stesso processo. Non e' il difetto che si stava cercando, ma
e' quello che valeva la pena chiudere per primo.

## Il rilievo: sporge cio' che si preme, rientra cio' in cui si scrive

! **LA LUCE VIENE DA SOPRA A SINISTRA, SEMPRE**, ed e' l'unica convenzione che
conta: se due controlli la prendessero da parti diverse, uno dei due
sembrerebbe premuto senza esserlo. Da qui discende tutto il resto:

| | |
|---|---|
| pulsante | sporge; premuto rientra **e la scritta si sposta di un pixel** — solo invertire il rilievo si vede appena |
| casella, lista, area, terminale | rientrano **sempre**: sono buchi nel pannello, non cose da premere |
| il fuoco | non cambia il rilievo, aggiunge una riga blu dentro. Un controllo scelto che cambiasse forma sembrerebbe un pulsante |
| telaio della finestra | due pixel, e ognuno ha un mestiere: quello di fuori sporge dallo sfondo, quello di dentro disegna il buco in cui sta il client |
| barra del titolo, pulsante di chiusura, presa d'angolo | sporgono: si riconoscono come cose da premere invece che come decorazioni |

! **GLI ANGOLI APPARTENGONO ALLA LUCE**, e va detto perche' e' l'unica cosa che
si sbaglia scrivendo `bordo3d()`: le due righe si incontrano in due angoli, e
chi ci arriva per ultimo decide come si vede lo spigolo. Prima l'ombra, poi la
luce.

! **L'ARITMETICA STA IN DUE POSTI E NON SI PUO' CONDIVIDERE**: `bordo3d()` nel
server e `bordo3d()` nel toolkit sono due processi diversi. Sono venti righe
identiche, e quando si tocca una va toccata l'altra — scritto in tutte e due.

## I menu a tendina, e perche' stanno DENTRO la finestra

    ExFinestra mb = ex_menu(finestra);
    ex_menu_voce(mb, "File", "Salva\tCtrl+S", ID_SALVA);
    ex_menu_voce(mb, "File", "-", 0);              un solco

! **LA SCELTA ARRIVA COME EXM_COMANDO, con lo stesso id di un pulsante.** Non e'
pigrizia: premere «Salva» fra i pulsanti e sceglierlo dal menu File sono **la
stessa decisione presa in due modi**, e chi scrive l'applicazione non deve
imparare due meccanismi. E' il motivo per cui aggiungere i menu all'editor non
ha voluto **una riga di codice nuovo** nella sua procedura.

! **LA TENDINA SI DISEGNA NELLA ZONA DI PIXEL DELLA FINESTRA.** Una tendina che
possa uscire dal bordo dovrebbe essere una FINESTRA a se': una zona di memoria
condivisa in piu' per ogni menu aperto, un giro di richieste al server ogni
volta che si preme «File», e la domanda «chi la chiude se il programma muore
mentre e' aperta?». Dentro la finestra nessuna di queste domande esiste. Il
prezzo e' dichiarato: una tendina piu' alta della finestra viene tagliata, e
una troppo a destra si sposta a sinistra invece di uscire.

! **E SI DISEGNA PER ULTIMA.** I figli si disegnano in ordine di creazione e il
menu si crea per primo: disegnando la tendina insieme alla barra finirebbe
sotto tutti i controlli che deve coprire.

! **IL MENU NON CATTURA LE SCORCIATOIE.** Le SCRIVE — un tab nel testo della
voce allinea a destra `Ctrl+S` — e le esegue l'applicazione. Un menu che se le
prendesse da solo se le prenderebbe anche mentre si scrive in una casella di
testo, e da dentro il toolkit non c'e' modo di sapere se in quel momento ha
senso.

Coi tasti: **F10 apre**, le frecce girano fra titoli e voci saltando i solchi,
Invio sceglie, Esc chiude — e **qualunque altro tasto chiude**, invece di
sparire nel nulla. A menu chiuso solo F10 e' del menu: un menu che si prendesse
le frecce sempre renderebbe muta la lista sotto.

## Gli appunti sono di tutta la scrivania, e non potevano essere una variabile

! **I DATI DI UNA LIBRERIA CONDIVISA SONO UNA COPIA PER PROCESSO.** Un buffer
dentro exwin.so avrebbe dato appunti per applicazione: copiare in un editor e
incollare nell'altro non avrebbe fatto niente — ed e' la prima cosa che
qualcuno prova, ora che l'editor si apre due volte. Stanno in una **zona di
memoria condivisa** (`exappunti`, 4 KB).

! **NON SERVE SAPERE CHI L'HA CREATA**: il kernel consegna pagine azzerate, e
una zona azzerata ha lunghezza zero, cioe' appunti vuoti. Chiedersi «l'ho
creata io o mi ci sono attaccato?» sarebbe una domanda in piu' con due risposte
da tenere d'accordo.

! **E VIVONO FINCHE' C'E' UN'APPLICAZIONE GRAFICA APERTA**: chiuse tutte, la
zona muore con l'ultima. E' la semantica della memoria condivisa di EX-OS, non
una scelta di qui.

**Provato**: due editor separati, «appunti condivisi» battuto e copiato in uno,
incollato nell'altro.

## Il file manager: l'albero non e' un controllo nuovo

! **E' UNA LISTA CON DENTRO L'INDENTAZIONE.** Un «controllo albero» vorrebbe
dire nodi, figli, un modello da tenere aggiornato e un disegno tutto suo dentro
`exwin.so` — cioe' un pezzo di toolkit che UNA sola applicazione usa. Qui
l'albero e' un vettore di nodi **in ordine di visualizzazione**: espandere vuol
dire infilare i figli subito dopo il padre, chiudere vuol dire toglierli. La
lista non sa che sia un albero.

! **IL PERCORSO DI UN NODO SI RICOSTRUISCE ALL'INDIETRO**, senza puntatori al
padre: con l'inserimento e la rimozione in mezzo al vettore un indice del padre
sarebbe da correggere in tutti i nodi ogni volta. Il livello, invece, non cambia
mai — il primo nodo di livello n-1 che si incontra tornando indietro **e' per
forza** il padre.

! **IL CLIC MOSTRA, L'APERTURA ESPANDE**, e per distinguerli e' servito un dato
in piu' nel protocollo del toolkit: per un EXM_COMANDO che viene da una lista,
`lp` dice **come** e' arrivato — `EX_APRIRE(lp)`, che vale 1 per l'Invio e per
il doppio clic. Senza, l'albero si sarebbe aperto sotto le dita di chi voleva
solo dare un'occhiata. (Il nome era `EX_DA_INVIO` finche' l'Invio era l'unico
modo di dire «apri»; col doppio clic sarebbe diventato una bugia.)

Copia file, copia directory ricorsiva e ricerca. La destinazione si chiede con
un dialogo nuovo, `ex_dlg_riga()` — **non e' ex_dlg_salva con un'altra
etichetta**: quello mostra le directory perche' chi salva sceglie DOVE, qui si
chiede una parola e un elenco di file accanto distrarrebbe.

## Aprire due volte lo stesso programma

! **LA POSIZIONE LA SCEGLIE IL SERVER, NON L'APPLICAZIONE** (`EX_AUTO` come x e
y). Un programma che nasce sempre nello stesso punto va bene finche' e' uno
solo: la seconda copia si sovrappone alla prima **esattamente**, e chi guarda
crede che non si sia aperta — un difetto che non da' nessun messaggio d'errore.
E la scelta e' del server perche' e' l'unico a sapere quante finestre ci sono
gia': le mette a cascata, con lo scostamento **uguale all'altezza della barra
del titolo**, che e' esattamente quanto serve perche' della finestra sotto resti
visibile il nome.

I processi separati funzionavano gia'; era la sovrapposizione a farli sembrare
uno solo. **Provato**: due terminali, due editor e un file manager insieme.

## Quattro difetti minori, tutti trovati provando

  1. **la barra del titolo attiva non lo era mai.** «Attiva» voleva dire
     «ultima della pila», e la barra delle applicazioni ha `WIN_ST_SOPRA`,
     quindi sta sempre in cima: nessuna finestra normale risultava mai attiva.
     Adesso vuol dire «ha il fuoco», che e' quello che si voleva dire;
  2. **ex_dlg_avviso non si chiudeva con Invio.** Aveva solo il pulsante OK,
     cioe' si chiudeva solo col mouse — e su un avviso, dove non c'e' niente da
     decidere, il tasto che si batte senza guardare e' proprio l'Invio. Le
     altre due finestre di ExDlg lo facevano gia': era questa l'eccezione, e
     nessuno l'aveva scelta;
  3. **ex_dlg_avviso troncava il testo.** Un'etichetta sola: due frasi
     diventavano una frase e mezza, senza nessun segno che ci fosse dell'altro.
     Adesso spezza sulle parole e la finestra si misura sul testo;
  4. **un nome di file lungo traboccava dallo stack del file manager.** Un
     `sprintf` su un buffer di 80 byte con un nome che ne puo' avere 255. Non
     era mai successo perche' i nomi di prova sono corti — che e' il modo in cui
     questi difetti restano nascosti per mesi.

E una scelta di colore che era anche una scelta di misurabilita': il blu della
scrivania e quello della barra del titolo erano **lo stesso numero**. A occhio
confondeva una riga scelta con una barra; e nelle prove che contano i pixel
rendeva impossibile dire quale blu fosse quale.

## Cosa manca, dichiarato

    niente annullamento         un taglio sbagliato non si rimette a posto:
                                e' quello che manca di piu' ora che c'e' un
                                «Taglia»
    ~~la selezione e' a tasti~~ FATTO il 18 agosto: il server manda anche il
                                movimento col bottone premuto (WIN_EV_MOUSE_-
                                MOSSO), e trascinare sul testo seleziona
    gli appunti sono 4 KB       e solo testo. Un servizio degli appunti con
                                piu' formati e senza tetto e' un'altra cosa
    niente sposta/cancella      copiare non distrugge niente; le altre si', e
                                ci vogliono con la domanda giusta davanti
    la copia non dice a che     il ciclo dei messaggi e' fermo dentro
    punto e'                    copia_albero(): la finestra non risponde
    ricerca: 8 livelli, 512     due tetti dichiarati, non scoperti dopo
    l'albero tiene 128 nodi     e' un vettore, e un vettore ha una fine

---

# Le finestre si ridimensionano, e la misura arriva fino al pty (18 agosto 2026)

    presa nell'angolo -> contorno mentre si tira -> zona condivisa nuova ->
    WIN_MSG_MISURATA -> il client rimappa -> EXM_MISURA -> ridisegno ->
    WIN_MSG_AGGIORNA, che e' la ricevuta

Misurato nei pixel del framebuffer, non nel log — sono due processi diversi e
credere a uno solo non basta:

| | |
|---|---|
| il terminale, tirato per la presa | cornice 646x424 -> **525x336**, e la griglia da **256198** pixel neri a **156023**: 80x25 celle diventano 64x19 |
| mentre si tira si muove solo il contorno | la cornice resta 646x424 e il contorno e' gia' 525x336 — cioe' **esattamente** dove finira' la finestra |
| una finestra spostata non torna indietro | trascinata in (200,180), allargata coi tasti 360->440: l'angolo resta **(199,161)** |
| SSH | `sshd: il terminale adesso e' 120x40`, con OpenSSH 10.0p2 dall'altra parte |
| telnet | `telnetd: il terminale adesso e' 120x40`, col telnet dell'ospite |
| niente regressioni | libctest **316/316**, filemgr ed edit si aprono e disegnano |

## Una zona condivisa non si allarga, e non serve che si allarghi

! **ERA IN CODA SOTTO «COSE CHE VOGLIONO UN PEZZO DI KERNEL NUOVO», E NON NE HA
VOLUTO NESSUNO.** Le pagine di una zona condivisa stanno in due spazi di
indirizzi a due indirizzi diversi: allungarle vorrebbe dire trovare spazio
libero DOPO di esse in tutt'e due, e non si puo' garantire. Ma la domanda
giusta non era «come si allarga»: era «chi resta senza pixel, e quando». E la
risposta e' nessuno, se le due zone convivono per il tempo di una consegna.

    1. il server crea la zona NUOVA e ci copia dentro quella vecchia
    2. il server chiude la sua vecchia e compone gia' dalla nuova
       (la vecchia non muore: il client la tiene ancora aperta)
    3. il server manda WIN_MSG_MISURATA
    4. il client apre la nuova, chiude la vecchia — che adesso muore —,
       si ridisegna e manda WIN_MSG_AGGIORNA con la misura che ha ADESSO
    5. quella misura e' la ricevuta

! **IL NOME DELLA ZONA PORTA DENTRO IL GIRO**, ed e' cio' che rende il passo 4
possibile: `win7.0`, poi `win7.1`. Due zone con lo stesso nome sono la STESSA
zona — ricrearne una mentre il client tiene aperta la vecchia renderebbe la
vecchia, con la misura di prima e **nessun errore**.

! **E LA RICEVUTA NON E' UN MESSAGGIO NUOVO.** `WIN_MSG_AGGIORNA` porta gia' la
misura che il client crede di avere: se e' quella nuova, ha per forza aperto la
zona nuova — non c'e' altro modo di saperla. Un messaggio in piu' per dire una
cosa che si sa gia' e' un messaggio in piu' da tenere sincronizzato per sempre.

! **PERCHE' SI RIPETE.** Un evento del mouse perso e' un clic perso; un
`WIN_MSG_MISURATA` perso e' un client che disegna **per sempre** dentro una zona
che il server non guarda piu' — una finestra congelata che pero' risponde ai
tasti. E la mailbox e' profonda quattro messaggi, quindi non e' un'ipotesi
teorica. Si ripete cinque volte al secondo finche' la ricevuta non arriva, e
dopo dieci tentativi si smette dicendolo.

## Si ridimensiona solo chi l'ha chiesto (WIN_ST_RIDIM / EX_RIDIM)

! **NON E' PRUDENZA, E' L'UNICA SCELTA ONESTA.** Una finestra piu' grande ha una
zona di pixel piu' grande con dentro quella vecchia in un angolo: chi non sa
rispondere a `EXM_MISURA` si ritrova meta' finestra col colore di partenza e i
propri controlli fermi dov'erano. Cioe' **una finestra rotta, fatta rompere dal
server**. Quindi la presa nell'angolo compare solo se l'applicazione l'ha
chiesta, ed e' l'opposto di quello che farebbe un sistema che ridimensiona tutto
e lascia a ognuno il compito di accorgersene.

Oggi lo chiedono `term` e `winprova`. `filemgr`, `edit` e `pm` no, e non e' una
dimenticanza.

## La presa sta dentro l'area del client, e si paga

! **IL BORDO E' SPESSO UN PIXEL, E UN PIXEL NON SI ACCHIAPPA COL MOUSE.**
Allargare il bordo per farci stare una presa vorrebbe dire rifare l'aritmetica
della cornice dappertutto — e sottrarre comunque quello spazio al client, solo
su tutti e quattro i lati invece che in un angolo. Quindi la presa e' un
quadrato di 14 pixel disegnato **sopra** i pixel del client, dopo averli
copiati: se si disegnasse insieme alla cornice, la copia della zona la
cancellerebbe subito.

## Si ridimensiona a RILASCIO, non mentre si trascina

! Ogni cambio di misura e' una zona condivisa da creare, riempire e consegnare.
Farlo a ogni movimento del mouse vorrebbe dire decine di zone al secondo e
altrettanti messaggi a un client che non fa in tempo a leggerli. Mentre si tira
si disegna il contorno di dove andra' a finire — quello che facevano tutti
quando la memoria costava, e per la stessa ragione, che qui vale ancora.

## Il difetto che ho trovato rileggendo, non provando

! **`ex_misura()` SU UNA FINESTRA MANDAVA ANCHE LA POSIZIONE.** Riusava
`WIN_MSG_SPOSTA`, che il protocollo descriveva gia' come «cambia
posizione/misura»: sembrava la strada gia' aperta. Ma **la posizione vera la sa
solo il server** — se l'utente ha trascinato la finestra per la barra del
titolo, quel movimento il client non lo sente — e il risultato sarebbe stato una
finestra che, ridimensionandosi, **torna da sola dove e' nata**.

La prova non l'aveva visto perche' la finestra era ferma dove era nata. Adesso
la misura ha un messaggio suo, `WIN_MSG_MISURA`, che di x e y non sa niente — e
la prova trascina la finestra PRIMA di allargarla.

! **E' il genere di difetto che si trova rileggendo cosa SIGNIFICA un
messaggio**, non guardandolo funzionare.

## Il terminale: le colonne cambiano davvero

Il controllo «terminale» rialloca la griglia e la ricopia **dal fondo**, non
dalla cima: la riga che interessa e' sempre l'ultima scritta — il prompt, la
risposta all'ultimo comando — e allineando le griglie in alto quella finirebbe
fuori dal bordo inferiore appena la finestra si stringe. E' quello che fa xterm.

E la misura nuova va nel **pty**, o l'avrebbe cambiata solo la finestra: un
programma a schermo pieno la chiede con `PTY_CTL_LEGGI_MISURA`.

! **`term` ARROTONDA ALLA CELLA.** Il font e' 8x16: un'area larga 645 pixel fa
80 colonne e avanza una striscia nera di 5 pixel, che sembra un difetto di
disegno e invece e' aritmetica.

## SSH e telnet: la misura aveva dove andare, adesso

Erano tutt'e due dichiarati nel codice — «NAWS, che non trattiamo: e'
dichiarato» in telnetd, e in sshd `pty-req` era l'unica occasione. Sono lo
stesso difetto visto da due protocolli:

    sshd      SSH_CHANNEL_REQUEST «window-change» durante la sessione
              (RFC 4254 6.7). want_reply dovrebbe essere FALSE, ma si risponde
              lo stesso a chi la chiede: un client che aspetta una risposta che
              non arriva si ferma
    telnetd   IAC DO NAWS nell'apertura — LA MISURA SI CHIEDE, non si aspetta:
              un client la manda solo se qualcuno gliel'ha domandata — e poi
              IAC SB 31 w1 w0 h1 h0 IAC SE, con IAC IAC per un 255 vero

! **ZERO VUOL DIRE «NON LO SO», NON «ZERO»**, in tutt'e due: si lascia stare
invece di dare a un programma una geometria in cui dividere per zero.

## Il puntatore SI pilota: la regola di ieri era troppo pessimista

! Il 18 agosto avevo scritto che le prove col mouse non sono ripetibili perche'
i movimenti relativi del monitor di QEMU si perdono. **Si perdono quelli
grandi.** A dieci pixel per volta il puntatore arriva dove lo si manda, al
pixel: la ricetta e' saturare in un angolo con qualche `mouse_move -600 -600`,
dove il troncamento non fa danno perche' si sbatte contro il bordo, e da li'
contare a passi di dieci. Sta in `tools/prova_ridimensiona.sh`, ed e' cio' che
rende provabile la presa nell'angolo — cioe' proprio la parte che senza mouse
non si tocca.

## Cosa manca, dichiarato

    niente SIGWINCH             un programma dentro un pty non viene svegliato
                                quando la misura cambia: la rilegge quando gli
                                pare. La shell lo fa al prompt
    il testo non rifluisce      stringendo un terminale le righe gia' scritte
                                restano tagliate dov'erano: rifluirle vorrebbe
                                dire tenere il testo per righe logiche e non
                                per celle, cioe' un terminale diverso
    si tira solo l'angolo       non i lati, e non si sposta il bordo in alto a
                                sinistra
    la posizione non torna      il client non sa dove il server ha messo la sua
    indietro al client          finestra dopo un trascinamento. Oggi non serve
                                a nessuno; il giorno che servira' e' un evento
    `ls -mc` impagina a 80      la larghezza se la ricorda a memoria, e adesso
                                che una c'e' davvero potrebbe chiederla

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

# PROSSIMO LAVORO — la mappa, non il lavoro (17 agosto 2026)

Il contesto e' finito prima di cominciare. Qui c'e' l'analisi gia' fatta, cosi'
chi riprende non la rifa'.

## L'ordine deciso, e perche'

 1. **Il toolkit** (`ex_fuoco()`, lista a scorrimento, area di testo
    multiriga). E' l'abilitante: quattro applicazioni su quattro si sono
    disegnate l'elenco a mano, e il fuoco ha gia' morso una volta. Sta tutto
    dentro `exwin.so`, che e' gia' condivisa — nessun lavoro di infrastruttura.
 2. **Utenti e permessi.** La giornata vera, e la specifica e' gia' scritta
    nella voce «Utenti, permessi e installazione a componenti».
 3. **I lettori di immagini**, e vanno in una **libreria nuova**: `eximg.so`.
    Non dentro `exwin.so` — per la stessa ragione per cui `exdlg` e' separata:
    una barra delle applicazioni non decodifica JPEG mai, e un decodificatore
    baseline sono ~1500 righe che pagherebbero tutti. Fetta libera:
    **0x04C00000** (vedi la mappa in `lib/exwin/exwin.ld`).

## `lib/exwin/exwin.c` in numeri — analisi gia' fatta

    1109 righe

    classi: CL_ETICHETTA CL_FINESTRA CL_INTESTAZIONE CL_PULSANTE
            CL_RIQUADRO CL_SEPARATORE CL_TERMINALE CL_TESTO

    Oggetto: usato classe id win_id padre h stile titolo proc pix
             passo_px premuto fuoco cursore

I tre punti da toccare per una classe nuova:

  - `classe_da_nome()` — la stringa diventa un `CL_`;
  - `disegna_oggetto()` — lo switch che disegna;
  - `tasto_al_fuoco()` — chi consuma i tasti (oggi solo `CL_TESTO` e
    `CL_TERMINALE`);
  - piu' `accetta_fuoco()`, che decide chi puo' avere il fuoco.

! **`ex_fuoco()` E' LA PIU' PICCOLA E VA FATTA PER PRIMA.** `fuoco_metti()`
esiste gia' ed e' `static`: serve solo esportarla, aggiungerla a
`exwin_esporta.c` e allo stub. Sblocca subito il dialogo di ExDlg, dove oggi la
casella si crea per prima **solo** per prendersi il fuoco — e c'e' un commento
che promette di rimettere le righe in ordine quando questa funzione ci sara'.

! **LA LISTA E L'AREA DI TESTO HANNO BISOGNO DI PIU' STATO DI QUANTO `Oggetto`
NE ABBIA.** Il terminale ha risolto lo stesso problema con una tabella a parte
(`g_term[TERM_MAX]`, e `term_di()` che la trova dall'oggetto): e' lo schema da
copiare, non da reinventare.

! **E OGNI NOME NUOVO VA IN `exwin_esporta.c` E NELLO STUB.** Aggiungere una
funzione alla libreria e dimenticarsi lo stub da' un simbolo che non si
risolve — e il messaggio lo dice, col nome, ma solo a chi lo esegue.

# SSH funziona: una sessione cifrata con un client vero (18 agosto 2026)

    ssh -p 2225 root@127.0.0.1        (OpenSSH 10.0, da un'altra macchina)

      Warning: Permanently added '[127.0.0.1]:2225' (ED25519) to known hosts.
      root@127.0.0.1's password:
      ex-os:/> id
      uid=0(root) gid=0
        root: i controlli sui permessi non si applicano
      ex-os:/> exit

    e dal lato di EX-OS:

      sshd: chiavi in vigore, da qui si cifra
      sshd: utente 'root' ammesso al terminale
      sshd: terminale 80x24, avvio /bin/sh

**Scambio di chiavi, firma dell'host, cifratura, autenticazione, pty, shell,
comando eseguito e uscita pulita** — con OpenSSH dall'altra parte, non con un
client scritto da noi.

## I tre difetti che stavano fra il codice e la sessione

### 1. Il driver IP consegnava piu' byte di quanti ne stiano in un messaggio

    tcp_consegna:  n = c->rx_len;  if (n > TCP_BUF) n = TCP_BUF;   /* 4096 */
    ma IPC_MSG_MAX_DATA e' 1536

! **PER MESI NESSUNO AVEVA MAI RICEVUTO TANTI BYTE IN UNA VOLTA.** telnet manda
righe, ftp manda comandi, il rimbalzo di tcpserv manda quello che gli arriva:
tutti sotto il migliaio. L'ha tirato fuori il primo KEXINIT di OpenSSH, che di
byte ne ha **1570**.

! **E IL SINTOMO ERA PERFIDO: `len` DICEVA LA VERITA'.** Il messaggio annunciava
1559 byte, ma dentro ce n'erano meno, e chi leggeva copiava la coda **dal
proprio buffer** — spazzatura. L'inizio dei dati era giusto, la lunghezza era
giusta, sbagliava solo il fondo. Da fuori sembrava che il client mandasse dati
diversi da quelli sul cavo, e su SSH questo voleva dire **una firma dello
scambio che non tornava**: il sospetto cadeva sulla crittografia, che era sana.

! **E QUELLO CHE AVANZA ADESSO RESTA NEL BUFFER**, non si butta: chi legge
riprenota e lo prende al giro dopo.

### 2. Nel ciclo della sessione non si prenotava la lettura

! **LO STACK CONSEGNA A CHI HA CHIESTO, E QUI SI ASPETTAVA SENZA CHIEDERE.** Il
ciclo guardava la mailbox con poll() aspettando che diventasse pronta — e non
poteva diventarlo, perche' nessuno aveva prenotato niente. Il sintomo: la
sessione si apriva, mostrava il prompt e **non rispondeva ai tasti**. Sembrava
la shell bloccata, era il tubo mai aperto. `telnetd` lo faceva; riscrivendo il
ciclo qui, si era perso.

### 3. Chi muore lascia byte nel tubo

! **USCENDO APPENA IL FIGLIO TERMINA SI PERDE L'ULTIMA COSA CHE HA STAMPATO** —
e l'ultima cosa e' quasi sempre quella che interessava: la risposta al comando
appena battuto. Si vedeva `id` scritto e mai risposto. Adesso, quando la shell
finisce, il pty si svuota prima di chiudere.

## Come si e' trovato il primo, che era invisibile

La firma non tornava e tutto sembrava a posto. La strada che ha funzionato:

 1. **catturare il traffico** (QEMU `filter-dump`) e ricomporre i due flussi TCP
    dai numeri di sequenza;
 2. **rifare i conti da fuori**, in Python, con la stessa formula dell'RFC;
 3. quando anche quelli non tornavano, **confrontare i BUFFER invece degli
    hash**: un hash dice «diverso», un buffer dice **dove**. La composizione di
    sshd si e' rivelata identica a quella di riferimento, byte per byte;
 4. quindi il difetto era nei DATI: far stampare al server l'impronta di ogni
    pezzo e confrontarla con quella presa dal filo. Sei pezzi su sette
    coincidevano. **I_C no** — stessa lunghezza, stesso inizio, fondo diverso.

! **UNO SCARTO CHE COMINCIA A META' DI UN BLOCCO E' UN PROBLEMA DI TRASPORTO,
NON DI CONTENUTO.** Se il client avesse mandato altro, sarebbe stato diverso
dall'inizio.

## Cosa parla, e cosa no

    kex          curve25519-sha256
    host key     ssh-ed25519
    cifrario     chacha20-poly1305@openssh.com
    auth         password (la verifica la fa login sul pty, non sshd)
    canale       session, pty-req, shell

! **SI PARLA UNA LINGUA SOLA, ED E' VOLUTO.** Un algoritmo per mestiere: non
c'e' negoziazione da fare, e se il client non li conosce la connessione finisce
subito. Offrire piu' scelte vorrebbe dire piu' codice crittografico di cui una
parte non verrebbe mai eseguita — e un codice crittografico mai eseguito e' il
posto peggiore dove tenere un difetto.

! **LA PASSWORD NON LA GUARDA sshd.** Il metodo «password» viene accettato e
poi e' `login`, sul pty, a chiedere e verificare: il formato di `/boot/ombra`,
il sale, le liste di chi puo' entrare stanno in un posto solo. Due copie
divergono al primo cambiamento.

! **UN TERMINALE DI MISURA ZERO NON ESISTE**: un client che manda 0x0 sta
dicendo «non lo so», e prenderlo alla lettera darebbe una shell che crede di
avere zero righe. Si mette 80x24.

! **E LA MISURA CAMBIA ANCHE DOPO**, dal 18 agosto 2026: `window-change` arriva
durante la sessione e finisce nel pty. Prima `pty-req` era l'unica occasione, e
chi allargava la finestra restava con una shell convinta di essere 80x24.

## Cosa manca, dichiarato

    una sessione per volta      come telnetd: servirne piu' d'una vuole uno
                                spawn per sessione
    niente rinnovo delle chiavi una sessione lunghissima non le cambia mai
    niente chiavi pubbliche     solo password; le chiavi vogliono anche il
                                formato dei file authorized_keys
    la chiave dell'host sul CD  non si puo' salvare: se ne fa una nuova a ogni
                                avvio, e il client si lamenta — giustamente

# Il protocollo SSH: arriva alla firma e li' si ferma (18 agosto 2026)

    ssh -p 2222 root@127.0.0.1     (OpenSSH 10.0 vero, da un'altra macchina)

      debug1: Remote protocol version 2.0, remote software version EXOS_0.1
      debug1: kex: algorithm: curve25519-sha256
      debug1: kex: host key algorithm: ssh-ed25519
      debug1: kex: server->client cipher: chacha20-poly1305@openssh.com
      debug1: SSH2_MSG_KEX_ECDH_REPLY received
      debug1: Server host key: ssh-ed25519 SHA256:LK0W1v8davRAWwQgdhQ...
      ssh_dispatch_run_fatal: incorrect signature          <- QUI SI FERMA

! **IL LAVORO E' A META', E QUESTA VOCE LO DICE PERCHE' LA META' FATTA E'
GRANDE E QUELLA CHE MANCA E' PICCOLA E PRECISA.** `bin/sshd/sshd.c` sono 1070
righe: scambio delle versioni, binary packet protocol con
chacha20-poly1305@openssh.com, KEXINIT, curve25519-sha256, userauth, canale con
pty-req e shell, e l'inoltro dei byte. Un client vero ci arriva fino alla firma.

## Quello che gia' funziona con OpenSSH dall'altra parte

    la negoziazione     il client sceglie i nostri tre algoritmi
    la chiave dell'host la legge e ne mostra l'impronta
    X25519              IL SEGRETO CONDIVISO COINCIDE — verificato rifacendo i
                        conti fuori, sul traffico catturato: K e' identico da
                        tutt'e due le parti

! **CHE IL SEGRETO COINCIDA E' LA NOTIZIA PIU' IMPORTANTE**: vuol dire che lo
scambio di chiavi con un'implementazione vera funziona, e che la matematica
provata contro i vettori parla davvero con il resto del mondo.

## Quello che manca: l'impronta dello scambio

Il client rifiuta la firma. Non e' la firma in se' — Ed25519 e' provato contro i
vettori e la firma verifica sul nostro H — ed e' che **il nostro H non e' il
suo**. Rifacendo i conti da fuori, sul traffico catturato:

    K            coincide
    V_C V_S      coincidono (39 e 16 byte)
    I_C I_S      lunghezze giuste (1559 e 156)
    K_S Q_C Q_S  presi dal filo
    il totale    1949 byte, ed e' quello che sshd dichiara

...e H differisce lo stesso. Il pezzo che non torna e' uno di quelli, nel
CONTENUTO e non nella lunghezza — e la prossima cosa da fare e' far stampare a
sshd l'impronta di ogni pezzo e confrontarla con quella presa dal filo. Il
codice per farlo e' gia' li', dietro `-v`.

## Due difetti veri trovati per strada, e sono piu' importanti del resto

### getentropy() puo' fallire, e io non guardavo

    sshd: DIAGNOSI privata effimera 0000000000000000000000000000000000000000

! **UNA CHIAVE EFFIMERA DI SOLI ZERI NON DA' NESSUN ERRORE**: lo scambio
riesce, la sessione parte, e chiunque ascolti puo' rifare gli stessi conti. E'
il modo peggiore in cui possa rompersi una cosa del genere — **funzionando**.

La libc lo diceva accanto al prototipo: «entrambe possono fallire con EAGAIN, e
non e' un caso da ignorare». L'ho chiamata senza guardare l'esito. Adesso c'e'
una `casuale()` che insiste e, se proprio non riesce, **rifiuta di servire**:
meglio nessuna connessione che una chiave che si puo' indovinare.

### E il kernel non aveva entropia da dare

    Entropia: RDRAND assente, 0 bit all'avvio

! **OGNI INTERRUPT NON-TIMER VALEVA UN BIT E LA SOGLIA ERA 128.** Su una
macchina senza RDRAND, appena accesa, il serbatoio resta vuoto: un servizio che
parte all'avvio chiede la sua chiave proprio nel momento peggiore. E nelle prove
automatiche i comandi arrivano dalla seriale, quindi la tastiera non genera mai
eventi — il serbatoio non si riempiva **mai**.

Da qui `entropia_jitter()`: misura quanto tempo ci mette la CPU a fare la stessa
cosa due volte, col contatore di cicli aggiunto ieri. Su una macchina vera quel
tempo non e' mai identico — cache, predizione dei salti, rinfresco della DRAM.

! **LA STIMA E' DELIBERATAMENTE AVARA: UN BIT OGNI OTTO CAMPIONI.** Contarne di
piu' sarebbe facile e sarebbe la bugia peggiore che si possa scrivere in un
sistema: dichiarare entropia che non c'e' vuol dire che qualcuno ci costruira'
sopra una chiave.

! **E SU QEMU VALE MENO CHE SU FERRO, ma funziona**: l'identita' della macchina
cambia a ogni avvio, e questo si e' verificato. Il numero vero si misura sul
Pentium.

## Un errore di metodo, che vale la pena non rifare

Per mezz'ora ho letto l'output di una macchina che credevo nuova e invece era la
stessa di prima: un QEMU rimasto vivo teneva aperto il file del log, e ogni
«nuova» esecuzione mostrava la stessa chiave effimera. Ne ho quasi concluso che
l'entropia fosse deterministica.

! **DUE ESECUZIONI CHE DANNO LO STESSO NUMERO CASUALE SONO O UN DIFETTO GRAVE O
UNA PROVA CHE NON E' RIPARTITA**, e la seconda e' molto piu' probabile. La
regola era gia' scritta il 17 agosto — «niente build con QEMU acceso», «due
qemu_drive insieme si cancellano l'output» — e questa e' la sua terza faccia.

# Tre difetti trovati usando il sistema (18 agosto 2026)

Segnalati dall'utente dopo aver provato `exwin` a mano — non da una prova
automatica, e nessuno dei tre si vedeva dai messaggi.

    prima:  /exwin/bin/edit &
            edit: il server a finestre non risponde.
    dopo:   edit: file nuovo, senza nome

## 1. La terza applicazione non partiva, e il messaggio accusava la parte sana

! **`SHM_PER_PROC` VALEVA QUATTRO, E IL SERVER APRE UNA ZONA PER FINESTRA.** Le
sue finestre sono anche la scrivania e la barra delle applicazioni: due prima
ancora che l'utente apra qualcosa. Alla terza applicazione `shm_apri()` rendeva
`-EMFILE`, il server non rispondeva alla richiesta di creazione, e il programma
diceva **«il server a finestre non risponde»** — un messaggio che accusa la
parte sana. Chi lo leggeva pensava a un blocco della scrivania.

! **E IL COMMENTO ACCANTO ALLA COSTANTE DICEVA LA COSA SBAGLIATA**: «un client
grafico ne usa una o due, e per il server il tetto vero e' SHM_MAX_ZONE». La
prima meta' e' giusta; la seconda no — il tetto per processo lo tocca proprio
lui. Un limite messo pensando a un uso e poi usato in un altro.

Adesso `SHM_PER_PROC` vale 20 (le finestre del server sono 16, piu' margine) e
`SHM_MAX_ZONE` 24. Costa 240 byte per PCB, 15 KB su 64 processi; le pagine vere
si allocano solo quando la zona esiste davvero.

## 2. Il clic sceglieva la riga sbagliata, e di sempre lo stesso scarto

! **`origine()` RENDE LA POSIZIONE DEL PADRE, E CHI LA USA AGGIUNGE LA
PROPRIA.** Il disegno e la ricerca del controllo sotto il puntatore scrivono
`ox + o->x`; il clic sulla lista e quello sull'area di testo **non lo
facevano**. Il risultato: il clic veniva diviso per l'altezza della riga come se
la lista cominciasse in cima alla finestra, e si sceglieva una riga piu' in
basso di quante ne stanno nello spazio sopra la lista — nel file manager tre o
quattro, sempre le stesse.

! **UNO SCARTO COSTANTE E' LA FIRMA DI UN'ORIGINE SBAGLIATA**, e vale la pena
saperlo riconoscere: se fosse stato un problema di eventi o di scala, lo scarto
sarebbe cambiato col punto in cui si clicca.

E lo stesso difetto stava nell'area di testo dell'editor: cliccare su una riga
ne portava il cursore su un'altra.

## 3. Un pulsante si prendeva la tastiera e non la ridava

! **PREMUTO «SU» COL MOUSE, LE FRECCE NON MUOVEVANO PIU' LA SELEZIONE.** Il clic
dava il fuoco al controllo cliccato — tutti, pulsanti compresi — e un pulsante i
tasti non li usa: non consuma nemmeno l'Invio. Da li' il fuoco non tornava
indietro da solo, e la lista restava sorda finche' non ci si cliccava sopra.

! **IL FUOCO VA A CHI SE NE FA QUALCOSA**: una casella, una lista, un'area, un
terminale. Un pulsante lo si raggiunge con Tab, che e' il modo di chiederlo
apposta invece di ottenerlo per caso premendolo.

## Cosa e' provato, e cosa no

    la terza applicazione parte      provato: edit apre la sua finestra
    le frecce dopo un clic           provato: la selezione si muove ancora
    l'allineamento del clic          NON provato a macchina

! **IL PUNTATORE NON SI PILOTA DALL'ESTERNO IN MODO AFFIDABILE**, e va detto
invece di far finta: i movimenti relativi grandi del monitor di QEMU si perdono
per strada — il mouse PS/2 manda pacchetti con delta piccoli — e una prova che
riesce a volte non e' una prova. La correzione dell'allineamento e' verificata
per lettura: `origine()` ha tre chiamanti, due aggiungevano la propria posizione
e due no, e i due che non lo facevano sono esattamente quelli che sbagliavano.

# La matematica di SSH, provata contro gli RFC (18 agosto 2026)

    crypttest — i vettori degli RFC su questa macchina

      ChaCha20 (RFC 8439)                                  ok
      e due volte riporta al testo                         ok
      Poly1305 (RFC 8439)                                  ok
      X25519 (RFC 7748)                                    ok
      e i due capi arrivano allo stesso segreto            ok
      un punto di ordine piccolo si fa riconoscere         ok
      SHA-512 (FIPS 180-4)                                 ok
      Ed25519: la chiave pubblica (RFC 8032)               ok
      la firma del messaggio vuoto                         ok
      e si verifica                                        ok
      una firma nostra si verifica                         ok
      e cambiando un bit del messaggio, no                 ok

      12 prove superate, 0 fallite

E le stesse a terra, sotto AddressSanitizer e UBSan.

`lib/excrypt/`, 1180 righe: **la matematica di una sessione cifrata, finita.
Il protocollo no** — quello e' il prossimo lavoro, e adesso ha su cosa poggiare.

## Perche' questa strada e non RSA

! **E' LA STRADA SENZA GRANDI NUMERI, ED E' UNA SCELTA FATTA UNA VOLTA SOLA.**
Curve25519 per lo scambio ed Ed25519 per la firma sono aritmetica su numeri di
lunghezza FISSA, 32 byte, che stanno in vettori di parole. RSA vorrebbe un
modulo esponenziale su interi da 2048 bit: una libreria di grandi numeri da
scrivere, provare e mantenere — un sottosistema, non un file.

! **E ChaCha20 AL POSTO DI AES PER UNA RAGIONE CHE RIGUARDA QUESTA MACCHINA.**
AES veloce vuole le istruzioni AES-NI, che un Pentium non ha; AES in software
vuole tabelle in memoria, e le tabelle in memoria sono la strada per cui si e'
scoperto che la cache puo' raccontare la chiave. ChaCha20 e' somme, XOR e
rotazioni: nessuna tabella, nessun ramo che dipenda dalla chiave.

## I vettori degli RFC non sono «un esempio che funziona»

! **SONO I NUMERI CHE CALCOLA IL RESTO DEL MONDO.** Una crittografia che
funziona solo con se stessa e' una crittografia con cui non si parla con
nessuno: la prima connessione da un client vero fallirebbe senza dire perche',
e non ci sarebbe modo di sapere quale dei cinque pezzi ha torto.

! **E UN ERRORE QUI NON SI VEDE.** Non da' un risultato storto: da' «connessione
fallita» — oppure, molto peggio, una connessione che funziona e non protegge
niente. E' il motivo per cui questa e' l'unica parte del sistema in cui le prove
sono venute prima di qualunque uso.

## Il difetto che il canarino ha trovato, e che a terra era passato

    *** stack rotto: una scrittura e' andata oltre un buffer locale.

Alla PRIMA esecuzione di `crypttest` dentro EX-OS. Era in Poly1305: il bit che
chiude un blocco veniva scritto sempre, e su un blocco pieno `blocco[16]` cade
un byte oltre il vettore.

! **A TERRA ERA PASSATO PERCHE' IL BANCO NON GIRAVA SOTTO SANITIZZATORE**, e la
lezione e' la mia, non del codice: avevo provato i decodificatori di immagini
con ASan e poi, su codice piu' delicato, mi ero fidato dei valori giusti. **I
numeri erano tutti esatti** — i vettori passavano — mentre la memoria accanto
veniva scritta.

! **E L'HA TROVATO IL CANARINO MESSO IL 17 AGOSTO**, quello che allora si era
detto «non impedisce l'overflow: lo fa ACCORGERE». E' esattamente cio' che ha
fatto, sul primo codice nuovo abbastanza delicato da meritarselo.

## Le cose che rendono questa matematica sicura, e che si perdono scrivendola «in modo naturale»

! **NESSUN RAMO GUARDA UN SEGRETO.** La scala di Montgomery fa le stesse
operazioni per ogni bit dello scalare; quale coppia di punti venga scambiata lo
decide una MASCHERA, non un `if`. Un ramo renderebbe il tempo — o la cache —
dipendente dalla chiave privata, e da li' si risale un bit per volta.

! **IL CONFRONTO DELLE IMPRONTE NON SI FERMA AL PRIMO BYTE DIVERSO.** Un memcmp
che esce presto dice, col tempo che impiega, quanti byte erano giusti: chi prova
indovina l'impronta un byte alla volta invece che tutta insieme — 256 tentativi
per byte al posto di 2^128.

! **LA CHIAVE DI Poly1305 E' USA E GETTA.** Due messaggi autenticati con lo
stesso `r` permettono di ricavarlo e di falsificare tutto: la chiave la genera
ChaCha20 dal contatore zero, una per pacchetto.

! **LO SCALARE SI POTA, E LA FIRMA E' DETERMINISTICA.** I tre bit bassi a zero
tengono il risultato nel sottogruppo giusto e rendono innocui i punti di ordine
piccolo mandati apposta; e il numero segreto di ogni firma non si sorteggia, si
calcola — e' la difesa contro l'errore che ha svelato le chiavi di piu' di un
sistema famoso.

! **E UN SEGRETO CONDIVISO TUTTO ZERI SI SEGNALA**: vuol dire che il punto
ricevuto era di ordine piccolo, cioe' che qualcuno sta provando a forzare un
segreto che conosce gia'. `x25519()` rende -1 e chi chiama deve rifiutare.

## Un difetto mio, e la prova che l'ha smascherato

La verifica delle firme falliva su firme GIUSTE — identiche ai vettori. Non era
la firma: era `spacchetta()`, che deve rendere il punto **negato**. Il controllo
di Ed25519 e' «S per la base MENO h per A fa R», e avendo il punto gia' negato
quella sottrazione diventa una somma — l'unica operazione che si e' scritta.

! **SENZA I CASI NEGATIVI NON L'AVREI VISTO SUBITO.** Le prove non guardano solo
che una firma buona passi: guardano che una con un bit cambiato — nel messaggio,
nella firma, nella chiave — NON passi. Una verifica che dicesse sempre «no»
supererebbe tre prove su quattro.

## Cosa manca per SSH, ed e' il protocollo

    scambio delle versioni      SSH-2.0-... e una riga di testo
    binary packet protocol      lunghezza, riempimento, e da NEWKEYS in poi
                                tutto cifrato e autenticato
    KEXINIT                     la negoziazione degli algoritmi
    curve25519-sha256           lo scambio, con la firma dell'host sopra
    userauth                    il metodo «password», che login sa gia' fare
    canale, pty-req, shell      e li' si riusa telnetd quasi per intero

! **LA MATEMATICA ERA LA META' DIFFICILE DA SBAGLIARE IN SILENZIO**, il
protocollo e' la meta' lunga: molte strutture, molti campi, e ogni errore da'
«connessione chiusa» senza dire altro. Ma adesso i pezzi sotto sono provati, e
quando qualcosa non tornera' si sapra' che non e' li'.

# Una sessione remota vera: telnetd (18 agosto 2026)

    la macchina che serve                    quella che si collega

    telnetd -v                               telnet 10.0.0.1
    telnetd: in ascolto sulla porta 23,      Connesso.
             /bin/login
    telnetd: solo da 10.0.0.0/24
    telnetd: qualcuno si e' collegato
             da 10.0.0.2 (connessione 2)
    telnetd: sessione aperta, /bin/login
             ha il PID 15
                                             EX-OS — accesso
                                             utente: _

    e con la shell diretta (-s):
                                             ex-os:/> id
                                             uid=0(root) gid=0
                                             ex-os:/> uname

    con «da = 192.168.1.0/24» in configurazione:
    telnetd: rifiutato: 10.0.0.2 non e' fra   Connessione chiusa dal server.
             gli indirizzi ammessi

! **NON E' UN PEZZO NUOVO: E' L'ASSEMBLAGGIO DI QUATTRO GIA' PROVATI.** listen e
accept, il pty con la sua disciplina di linea, `login` che autentica e scende
con `setuid`, e l'interruzione. `telnetd` mette in fila i byte fra una
connessione e uno pseudo-terminale, e non fa nient'altro — se qualcosa non
funziona, il difetto e' in uno dei quattro.

! **ED E' IN CHIARO, CON TUTTO QUELLO CHE COMPORTA.** Sta qui perche' PROVA
L'IMPIANTO — accettare, dare un terminale, autenticare, ripulire — senza la
crittografia in mezzo: se qualcosa non torna, si vede in chiaro. Su una rete di
cui non ci si fida non si accende.

## La configurazione: /boot/telnetd.cfg

    porta   = 23
    shell   = /bin/login        (oppure /bin/sh, che entra senza chiedere)
    utenti  = root, mario       (vuoto = chiunque abbia una password valida)
    nega    = root              (vince sulla riga sopra)
    da      = 10.0.0.0/24       (indirizzi singoli o reti; vuoto = da ovunque)

! **SI RILEGGE A OGNI CONNESSIONE, non solo all'avvio**, e non e' uno spreco: il
caso che conta e' quello di chi si accorge che sta entrando qualcuno che non
dovrebbe. Correggere il file e vedere la regola in vigore alla connessione dopo,
senza fermare il servizio, e' cio' che rende un elenco di permessi utile
davvero.

! **UNA LISTA VUOTA VUOL DIRE «NESSUN FILTRO», che e' il valore permissivo.**
L'alternativa — vuoto uguale «nessuno» — sembra piu' prudente e in pratica e'
peggio: chi accende il servizio senza configurazione si trova un programma che
rifiuta tutti senza un indizio sul perche', e la prima cosa che fa e' spegnere i
controlli.

! **E LA RIGA DI COMANDO VINCE SUL FILE.** E' cio' che permette di provare una
configurazione senza scriverla, e di aprire una porta diversa per un istante
senza toccare quella di sempre.

## Chi fa rispettare che cosa, e perche' proprio li'

! **GLI INDIRIZZI LI GUARDA telnetd, GLI UTENTI LI GUARDA login.** Quando il
servitore decide che programma lanciare, un nome utente non e' ancora stato
battuto: solo `login` sa chi ha bussato. Passargli le liste per argomento
(`-c`, `-n`) le rende anche visibili in un elenco dei processi — chi guarda vede
quale regola sta girando. E un domani un server ssh passera' le stesse due liste
alla stessa funzione.

! **IL CONTROLLO SUGLI UTENTI VIENE DOPO LA PASSWORD, ED E' VOLUTO.** Fatto
prima direbbe a chi prova quali nomi sono ammessi senza che ne conosca nemmeno
uno: si vedrebbe la differenza fra «non sei nella lista» e «password sbagliata».
Cosi' invece chi non e' ammesso paga lo stesso prezzo di chi sbaglia la
password, e non impara niente.

! **E A CHI VIENE RIFIUTATO PER L'INDIRIZZO NON SI SPIEGA NIENTE**: si chiude e
basta. Un «non sei nella lista» direbbe che dietro quella porta c'e' qualcosa e
che il filtro e' per indirizzo. Nel registro locale invece si scrive per esteso.

! **IL CONFRONTO E' SUI BIT PER GLI INDIRIZZI E SUL NOME INTERO PER GLI
UTENTI.** «10.0.0.7» dentro «10.0.0.70», e «mario» dentro «mariolino», sono veri
come testo e falsi come identita': un controllo d'accesso che si sbaglia in quel
verso lascia entrare chi non deve. Ventitre prove a terra coprono proprio quei
casi, `/0` e `/32` compresi.

## Le due cose che il protocollo telnet obbliga a fare

! **IL CLIENT VA MESSO IN MODO CARATTERE, O SI VEDE DOPPIO.** Appena collegato
fa l'eco da se' e manda una riga per volta; ma l'eco qui la fa gia' la
disciplina del pty. Si negoziano ECHO e SUPPRESS GO AHEAD per dirgli «ci penso
io, mandami i tasti». E si risponde SEMPRE, anche di no: un client che chiede e
non riceve risposta richiede, e due che si aspettano restano fermi.

! **UN INVIO SUL CAVO E' DUE BYTE, E IL SECONDO NON VA CONSEGNATO.** Il
protocollo dice che un CR viaggia seguito da LF o da NUL. Passarli tutt'e due
vuol dire una riga vuota dopo ogni comando — e con NUL, un byte zero all'inizio
della riga SEGUENTE. Il sintomo era **«comando non trovato: id» su un id battuto
giusto**, con un carattere invisibile attaccato.

## Il difetto che ha portato a galla: l'autoexec girava due volte

La prima sessione remota ha risposto con «Avvio automatico: accendo la rete...»
e una fila di `ipc_register` fallite con -17.

! **LA SHELL EREDITA LA CONSOLE DI CHI L'HA LANCIATA**, e l'autoexec si eseguiva
«solo sulla console 0» — che dentro telnetd e' vera di riflesso. Succedeva anche
nel terminale in finestra, e li' si era scambiato per rumore dell'avvio.

! **IL CRITERIO GIUSTO E' «HO UN pty», NON «SONO REMOTA»**: l'autoexec e' l'avvio
DEL SISTEMA, e il sistema si avvia su una console vera. Dentro un pty c'e'
sempre qualcun altro che quel lavoro l'ha gia' fatto.

## E login impara a lavorare senza tastiera

! **DENTRO UNO PSEUDO-TERMINALE LA TASTIERA NON C'ENTRA.** Il driver kbd serve
la tastiera FISICA di una console: chiedergli il modo raw da dentro un telnet
vorrebbe dire spegnere l'eco a chi sta seduto davanti alla macchina, e non a chi
sta battendo la password dall'altra parte del cavo. Su un pty l'eco la fa la
disciplina, e si spegne dicendolo a lei — un byte per volta, con l'asterisco
stampato da login come ha sempre fatto.

## Cosa manca, dichiarato

    una sessione per volta   telnetd ne serve una e poi torna ad accettare.
                             Servirne piu' d'una vuole uno spawn per sessione,
                             ed e' un cambiamento suo
    NAWS                     il ridimensionamento della finestra del client non
                             si tratta: la misura resta 80x24
    la crittografia          e' il prossimo lavoro, ed e' «lo stesso impianto
                             piu' la matematica»: Curve25519 e ChaCha20-Poly1305

# listen e accept: il TCP impara ad aspettare (18 agosto 2026)

    due macchine EX-OS e un cavo virtuale:

      la prima                          la seconda
      ipcfg -a 10.0.0.1                 ipcfg -a 10.0.0.2
      tcpserv 7                         tcptest 10.0.0.1 7 ciao-da-exos

      tcpserv: in ascolto sulla porta 7
      tcpserv: nessuno si e' collegato   <- la scadenza di accetta funziona
      tcpserv: connessione 2 accettata   <- la stretta di mano passiva e' finita
      tcpserv: rimbalzati 16 byte
                                        connessa (id 1)
                                        mandati 16 byte
                                        ciao-da-exos   <- tornato indietro

      e sulla stessa macchina:
      tcpserv: ascolto sulla porta 7 rifiutato (-98):
               c'e' gia' qualcuno in ascolto li'

! **FINO A IERI IL TCP DI EX-OS SAPEVA SOLO CHIAMARE.** C'era `tcp_apri()`,
cioe' connect, e nient'altro: nessun programma poteva ASPETTARE una
connessione, e quindi **nessun servizio di rete poteva esistere** — ne' telnet,
ne' un giorno ssh. Era il primo mattone, e adesso c'e'.

## L'ascoltatore non e' una connessione

! **OCCUPA UNO SLOT MA NON HA UN ALTRO CAPO**: niente numeri di sequenza, non
si legge e non si scrive. Sta nella stessa tabella delle connessioni per non
avere una seconda tabella con le sue regole di vita, e le operazioni che non
hanno senso lo rifiutano guardando lo stato. Chiuderlo lo libera e basta: non
c'e' nessuno a cui mandare un FIN, e tenerlo `S_MORTA` come una connessione
vera terrebbe occupata la porta finche' qualcuno non legge dati che non
esistono.

! **E LE CONNESSIONI CHE HA GENERATO GLI SOPRAVVIVONO.** Sono gia' loro, non
sue: chiudere l'ascoltatore vuol dire smettere di accettarne di nuove, non
buttare giu' chi sta parlando.

## La coda sta dalla parte sbagliata, apposta

! **E' LA CONNESSIONE NATA DA UN SYN A RICORDARE CHI ASCOLTAVA, non
l'ascoltatore a tenere una lista.** Una coda dentro l'ascoltatore vorrebbe dire
un vettore con una lunghezza da scegliere, e la scelta sbagliata sarebbe
silenziosa: le connessioni in eccesso sparirebbero senza che nessuno lo dica.
Cosi' il limite e' uno solo — la tabella — ed e' lo stesso che si vede gia'
rifiutando una connessione in uscita.

! **E L'ATTESA DI `accetta` STA SULL'ASCOLTATORE**, non su una connessione:
quella che arrivera' non esiste ancora, e quando nascera' sara' lei a cercare
li' chi la stava aspettando.

## Le tre cose che si scoprono scrivendo la stretta di mano passiva

! **IL MAC SI PRENDE DAL FRAME ARRIVATO, E NON SI FA UN ARP.** Chi ci ha appena
parlato ha per forza un MAC valido — e' il mittente del pacchetto che stiamo
leggendo. Chiedere un ARP qui vorrebbe dire un'attesa dentro la stretta di
mano, che e' proprio cio' che `tcp_apri` evita rifiutando con `-EAGAIN`.

! **UN SYN RIPETUTO VUOL DIRE CHE IL NOSTRO SYN+ACK E' ANDATO PERSO**, e va
rimandato. E' l'unica ritrasmissione che questo lato fa, e senza di lei una
connessione persa in apertura non si riprende: il cliente ritrasmette, noi
restiamo zitti, e lui rinuncia.

! **SENZA POSTO SI LASCIA CADERE IL SYN E NON SI RISPONDE NIENTE.** Chi bussa
lo ritrasmette da se' — e' TCP che lo fa — e se nel frattempo si e' liberato
uno slot entra al secondo tentativo. Rispondere RST vorrebbe dire dirgli «non
c'e' nessuno», che e' falso.

## Si consegnano solo connessioni gia' aperte

! **MAI QUELLE A META' STRETTA DI MANO.** Chi riceve un id si aspetta di
poterci scrivere subito: consegnare una `S_SYN_RICEV` vorrebbe dire un id
valido su cui la prima scrittura finisce nel vuoto — e il difetto si vedrebbe
dall'altra parte, non qui. E se il segmento che completa la stretta porta gia'
dei dati, quelli non si buttano: succede, con un cliente che scrive subito.

## Le connessioni passano da quattro a otto

! **UN ASCOLTATORE OCCUPA UNO SLOT SENZA ESSERE UNA CONNESSIONE**: un servitore
con due clienti collegati ne usa gia' tre, e con quattro in tutto il primo che
bussa mentre gli altri parlano si sente rifiutare. Otto per 8 KB di buffer sono
64 KB.

## `/bin/tcpserv`, e perche' fa l'eco

Sta a `listen`/`accept` come `tcptest` sta a `connect`: quando un servizio di
rete non funzionera', la domanda sara' se sia rotto il servizio o l'ascolto.

! **UN'ECO PROVA TRE COSE IN UN COLPO SOLO**: che la stretta di mano passiva e'
finita davvero, che i dati arrivano nel verso giusto, e che la risposta esce
dalla connessione GIUSTA. Sono esattamente le tre che listen e accept devono
garantire.

## Dove siamo, sulla strada della sessione remota

    listen e accept       fatti, oggi
    pty                   fatti, oggi (vedi la voce sopra)
    un server telnet      il prossimo: accettare, avviare login, scendere con
                          setuid, dare la shell su un pty, ripulire alla
                          chiusura. Tutti i pezzi ci sono.
    ssh                   dopo, ed e' «lo stesso impianto piu' la
                          crittografia»: Curve25519 e ChaCha20-Poly1305, non
                          RSA — vedi «Multiutenza remota e SSH»

! **E NIENTE SSH PRIMA CHE I PERMESSI SIANO SOLIDI**, che resta vero e scritto
li' dal 17 agosto.

# I pty, e Ctrl+C che finalmente morde (18 agosto 2026)

    libctest, 316 prove su 316:

        pty_apri() riesce
        l'eco torna dal master
        Backspace fa eco «indietro, spazio, indietro»
        lo slave riceve la riga solo all'Invio
        l'uscita del programma esce dal master
        la misura si imposta, e si rilegge
        in modo grezzo il byte arriva subito
        una shell parte sullo slave, la si dichiara in primo piano
        Ctrl+C la interrompe -> uscita 130

    nel terminale in finestra:

        ex-os:/> ^C            <- Ctrl+C al prompt: eco, e la shell VIVE
        ex-os:/> id
        uid=0(root) gid=0      <- l'uscita dei comandi si vede, finalmente

E' il lavoro che la coda chiamava «il piu' grosso rimasto», ed era in coda da
giorni con una riga sola: «un segnale attraverso una pipe non si manda».

## Prima l'interruzione, che era il pezzo mancante

`SYS_INTERROMPI` (250). Prima di oggi Ctrl+C era **il carattere 3 e basta**:
arrivava nel buffer come una lettera qualunque e nessuno lo guardava.

! **NON E' UN SEGNALE, E NON DIVENTERA' UN SEGNALE PER SBAGLIO.** Chi viene
interrotto **esce**, con codice 130 — 128 piu' 2, come una shell Unix riporta un
Ctrl+C — e non c'e' modo di intercettarlo. I segnali veri vogliono lo stack del
processo riscritto al ritorno da trap per farlo saltare in un gestore e poi
tornare indietro: e' un sottosistema, e non e' quello che serve per far
funzionare Ctrl+C.

! **E NON SI MUORE DOVE SI E', MA AL PRIMO POSTO SICURO.** Ammazzare un processo
mentre e' dentro il kernel — con una pagina mappata a meta', un lucchetto preso,
un settore in volo — vuol dire lasciare quello stato li' per sempre. Il flag si
alza, il processo si sveglia, la sua attesa rende `-EINTR`, e chi muore lo fa in
`syscall_handler`, dopo che la syscall e' finita e prima di tornare in ring 3.

! **I PUNTI DI ATTESA DEVONO SAPERLO, UNO PER UNO.** Svegliare e basta non
serve: chi dorme in una `read` farebbe un giro del ciclo, troverebbe il buffer
ancora vuoto e si riaddormenterebbe — senza passare mai dal punto in cui si
muore. Sono quattro: la pipe in lettura e in scrittura, `ipc_recv`, e la
tastiera.

! **UN CICLO CHE NON CHIAMA MAI IL KERNEL NON SI FERMA**, ed e' dichiarato.
Fermarlo vorrebbe dire guardare il flag nel gestore del timer, cioe' terminare
un processo da dentro un IRQ: `proc_exit()` fa `vfs_sync()`, che aspetta il
disco.

## Poi il pty, che e' una pipe CON una disciplina di linea

! **E' IL PEZZO CHE SI SOTTOVALUTA SEMPRE.** Una shell su una pipe nuda e'
esattamente la shell dentro una finestra prima di oggi: niente eco, niente
Backspace, niente Ctrl+C, niente misura dello schermo. Quelle cose non le fa la
shell e non le fa il terminale — le fa la disciplina, e una pipe non ne ha.

Le due estremita' non sono simmetriche, e non e' un dettaglio:

    MASTER   lo tiene chi FA il terminale. Ci scrive i tasti battuti e ci legge
             quello che il programma stampa.
    SLAVE    lo eredita la shell come stdin/stdout/stderr, e per lei e' un
             terminale come un altro: non sa di essere dentro un pty.

! **SCRIVERE NEL MASTER VUOL DIRE BATTERE UN TASTO, NON STAMPARE.** Quei byte
passano dalla disciplina; quelli scritti nello slave sono l'uscita di un
programma e non li tocca nessuno. Sono due `FDType` distinti apposta: con un
tipo solo e un flag si potrebbero scambiare, e lo scambio non darebbe un
errore — darebbe una shell che risponde all'eco di se stessa.

! **L'ECO VA VERSO IL MASTER, NON VERSO LO SCHERMO.** Il kernel non sa che
aspetto abbia il terminale: mette i caratteri nel tubo di ritorno e chi li legge
li disegna come vuole. E' la stessa ragione per cui il server a finestre non
disegna il contenuto delle finestre.

! **CANCELLARE E' TRE CARATTERI, NON UNO**: indietro, uno spazio sopra quello
che c'era, indietro di nuovo. Mandare solo il Backspace sposta il cursore e
lascia la lettera dov'era.

! **E FINE DEI DATI E' UNO STATO, NON UN BYTE.** Ctrl+D non si mette nel buffer:
se ci finisse dentro, un programma lo leggerebbe come carattere 4 e la fine dei
dati non arriverebbe mai.

## Chi muore con Ctrl+C: la regola che la prima prova ha smentito

Sembrava naturale che il terminale dichiarasse la shell come primo piano. E' la
prima cosa che ho scritto, e la prima prova l'ha bocciata: **un Ctrl+C battuto
al prompt chiudeva la sessione**, e nella finestra restava l'eco che funzionava
con nessuno dall'altra parte a rispondere.

! **SU UNIX LA SHELL RESTA IN PRIMO PIANO E IGNORA IL SEGNALE. Qui non ci sono
gestori, quindi la stessa idea si ottiene NON DICHIARANDOSI**: al prompt il
primo piano e' nessuno, Ctrl+C cancella la riga in corso e non ammazza niente;
quando la shell lancia un programma dichiara lui, e allora Ctrl+C prende lui.

! **E LA DICHIARAZIONE E' DOPPIA, IN UN POSTO SOLO.** `sh_setfg()` lo dice alla
console — dove «primo piano» vuol dire chi puo' leggere la tastiera — e al pty,
dove vuol dire chi muore. Sono due significati diversi della stessa parola, e la
shell li dice insieme perche' cambiano insieme; su uno stdin che non e' un pty
la seconda chiamata rende `-ENOTTY` e non fa niente.

## Il difetto che il pty ha portato a galla: i figli non ereditavano niente

Battendo `id` nel terminale in finestra si vedeva il prompt tornare e **nessuna
risposta**: l'uscita finiva sulla console di testo.

! **`proc_create()` METTEVA SEMPRE LE TRE STANDARD ALLA CONSOLE**, e l'eredita'
dal padre avveniva solo per le redirezioni esplicite. Con le pipe non si vedeva
— la shell le passa una per una — ma dentro uno pseudo-terminale ci parlerebbe
soltanto la shell, e ogni programma che lancia scriverebbe da un'altra parte.
**Un terminale in cui l'output dei comandi non si vede non e' un terminale.**

Adesso le tre standard si ereditano, e le redirezioni esplicite vengono dopo e
vincono. Un file aperto invece non si eredita: il figlio si ritroverebbe la
posizione di lettura del padre e un riferimento da chiudere — chi vuole passare
un file lo fa con una redirezione, che quella strada la percorre per intero.

## Il terminale in finestra dimagrisce

`term_tasto()` era una disciplina di linea scritta dentro un'applicazione
grafica: teneva una riga sua, la correggeva col Backspace, ne faceva l'eco a
mano e la spediva all'Invio. **Adesso sono tre righe: manda il byte e basta.**
Quel lavoro sta nel pty, che lo fa per chiunque — il terminale oggi, un telnet
domani.

! **E I Ctrl VANNO GUARDATI PRIMA DEL FILTRO.** Il toolkit scarta le
combinazioni con Ctrl perche' sono scorciatoie dell'applicazione; per un
terminale sono CARATTERI (Ctrl+C e' il byte 3), e senza quell'eccezione il
Ctrl+C non sarebbe mai arrivato al pty — tutto il lavoro sulla disciplina
sarebbe rimasto irraggiungibile proprio da dove serviva.

## Cosa e' provato, e dove

    in libctest      tutta la disciplina, e Ctrl+C che interrompe una shell
                     dichiarata in primo piano: uscita 130. E' deterministico,
                     e gira a ogni prova.
    nella finestra   l'eco, il Backspace, l'uscita dei comandi esterni, e
                     Ctrl+C al prompt che non ammazza la sessione.

! **L'INTERRUZIONE DI UN PROGRAMMA DENTRO LA FINESTRA NON E' PROVATA A MANO**, e
va detto: pilotare i tasti dall'esterno perde i primi battuti dopo un cambio di
console, e una prova che riesce a volte non e' una prova. Il meccanismo sotto e'
lo stesso che libctest esercita a ogni giro.

## Cosa manca ancora, dichiarato

    frecce e cronologia     no: i tasti oltre il byte non entrano nella riga
    piu' di un attendente   un pty ha UN lettore per direzione (le pipe hanno
                            una lista: il giorno che serve si copia da li')
    un ciclo senza syscall   non si interrompe (vedi sopra)
    ~~listen/accept in TCP~~ FATTO lo stesso giorno: vedi la voce «listen e
                             accept: il TCP impara ad aspettare». E' cosi' che
                             sshd apre la porta (IP_MSG_TCP_ASCOLTA)

# Il dialogo diventa una domanda, e il modale morde (18 agosto 2026)

    editor, testo modificato, Ctrl+Q:

        appare «Modifiche non salvate — Uscire senza salvare?»
        battuti b, c, d col dialogo aperto:
            la fotografia PRIMA e DOPO e' IDENTICA BYTE PER BYTE
        clic su «Nuovo» dell'editor dietro:
            il testo intatto, il dialogo ancora li'
        Esc   -> l'editor resta, col suo testo
        Invio -> [3] terminato: /exwin/bin/edit (codice 0)

Fino a ieri «vuoi perdere le modifiche?» si chiedeva facendo premere due volte
lo stesso pulsante, perche' ExDlg aveva un avviso con un pulsante solo e il
server non sapeva cosa fosse una finestra modale. Adesso c'e' `ex_dlg_conferma()`
e c'e' `WIN_ST_MODALE`.

## Modale vuol dire due cose, e una si dimentica sempre

! **NON BASTA STARE SOPRA.** Una finestra che copre e basta lascia cliccare
quello che si vede intorno, e un dialogo a cui si puo' rispondere continuando a
scrivere nel testo non sta chiedendo niente. `EX_SOPRA` c'era gia' da giorni: e'
un'altra cosa.

! **E I TASTI SEGUONO LA STESSA REGOLA DEI CLIC.** Dimenticarlo e' il modo piu'
facile di fare un modale finto: si blocca il mouse, si prova col mouse, sembra
fatto — e intanto nel testo dietro si continua a scrivere. La prova che conta e'
proprio quella: tre lettere battute col dialogo aperto, e le due fotografie
identiche byte per byte.

I tasti non si buttano, si **dirottano** alla modale: e' lei l'unica che possa
farci qualcosa (Invio, Esc). E il clic su una finestra bloccata non si perde in
silenzio: **porta davanti la modale**, cosi' chi ha cliccato vede dove deve
rispondere invece di trovarsi un'applicazione sorda.

## Modale per l'applicazione, non per lo schermo

! **E' LA DECISIONE CHE CONTA, ed e' scritta nel protocollo.** Blocca solo le
finestre dello STESSO processo. Un modale di sistema bloccherebbe tutto — e il
giorno che il client muore col dialogo aperto, lo schermo resta bloccato e non
c'e' modo di rimediare se non ammazzando il server. Cosi' invece muore il
client, le sue finestre se ne vanno con lui, e il resto non se n'e' accorto.

! **E LA RICERCA E' PER PROCESSO, NON PER FINESTRA PADRE.** Il server non sa
niente di parentele: dal suo punto di vista un'applicazione e' un pid con delle
finestre. Legare il blocco alla parentela vorrebbe dire portare l'albero delle
finestre dentro il protocollo per usarlo li', e li' soltanto.

## La risposta prudente e' «no»

! **CHIUDERE LA FINESTRA, BATTERE Esc O VEDER MORIRE IL DIALOGO DANNO TUTTI 0.**
Un dialogo che in caso di dubbio rispondesse «si'» cancellerebbe il lavoro di
qualcuno quando qualcosa va storto — e queste domande si fanno **proprio prima
di perdere qualcosa**.

Le scritte dei due pulsanti le sceglie il chiamante: «Esci / Torna al testo»
dice cosa succede, «Si' / No» costringe a ricostruirlo dalla domanda.

! **E I PULSANTI SI MISURANO SULLA LORO SCRITTA.** La prima prova aveva «Torna
al testo» che usciva dal riquadro: una misura fissa va bene finche' le scritte
le sceglie chi ha scritto il dialogo, ma qui le sceglie chi lo chiama, che non
sa quanto e' largo un pulsante.

## Tre difetti trovati dalle prove, e nessuno era nel lavoro di oggi

**1. Le finestre dei client morti restavano sullo schermo.** Dopo Ctrl+Q la
shell diceva «terminato» e il rettangolo bianco dell'editor era ancora li'.

! **SI CHIEDE AL KERNEL CHI E' VIVO, NON SI ASPETTA CHE `ipc_send` FALLISCA.**
Aspettare l'errore vuol dire accorgersene solo quando c'e' qualcosa da mandare,
cioe' quando qualcuno clicca sul fantasma: la finestra sparirebbe al primo clic,
che e' **peggio** di non farla sparire — sembrerebbe che il clic abbia fatto
qualcosa. Il server chiama `procinfo()` una volta al secondo, e gli zombie
contano come morti. Se l'elenco non ci sta nel buffer non si raccoglie niente:
un elenco troncato vuol dire distruggere finestre **vive**.

**2. Un clic sullo sfondo cancellava la scrivania**, immagine compresa. La causa
non era in `pm`: era in `ex_smista()`, che dopo un messaggio gestito ridisegnava
chiamando `ex_procedura_base()` — che riempie di grigio e ridisegna i controlli.

! **UNA FINESTRA CHE DISEGNA I PROPRI PIXEL NON AVEVA NESSUN MODO DI
DIFENDERSI**, perche' il ridisegno non le veniva nemmeno chiesto. Adesso passa
dalla sua procedura; chi non gestisce `EXM_DISEGNA` ricade sulla base da se', e
per quelle finestre non cambia niente.

! **E LA SCRIVANIA ADESSO HA UNA PROCEDURA.** Disegnava colore e immagine una
volta sola, subito dopo la creazione: **funziona perfettamente finche' nessuno
chiede di ridisegnare**, ed e' la forma piu' facile di questo errore. Chi
possiede dei pixel deve saperli rifare su richiesta.

**3. Il titolo del terminale si leggeva «Terminale ZCO /bin/sh».** Era un
trattino lungo in UTF-8.

! **IL FONT DEL SERVER HA 256 CARATTERI, UNO PER BYTE**: una stringa UTF-8 non
viene «resa male», esce come i suoi byte. Da qui `make verifica-testi`, che
guarda le stringhe delle applicazioni grafiche — non i commenti, dove
l'italiano si scrive per intero, e non i messaggi per la console di testo, che
in 45 file usano «» e i trattini lunghi ed e' la convenzione del progetto.

! **E SI E' VISTO GUARDANDO UNA FOTOGRAFIA, NON COMPILANDO.** E' il genere di
difetto che nessun avviso dara' mai.

# JPEG e ICO: i lettori di immagini sono finiti (18 agosto 2026)

    a terra, contro i pixel di partenza:

        jpg444.jpg    32x24  4:4:4       scarto max 3, medio 0,58
        jpg420.jpg    32x24  4:2:0       scarto max 7, medio 1,75
        jpggrigio.jpg 24x16  un piano    scarto max 1, medio 0,04
        jpgrst.jpg    32x24  ripartenze  scarto max 3, medio 0,58
        jpgbordo.jpg  21x13  4:2:0       scarto max 6, medio 1,74

        icona.ico     48x48  3 voci      0 pixel diversi
        icona4.ico    24x24  4 bit       0 pixel diversi
        iconapng.ico  64x48  PNG dentro  0 pixel diversi

    in volo, dentro EX-OS, exwin -s /exwin/prova.jpg:
        scarto max 3, medio 0,58  — GLI STESSI NUMERI

    7.500 file guasti sotto AddressSanitizer + UBSan:  0 guasti

! **GLI STESSI NUMERI A TERRA E IN VOLO NON SONO UNA COINCIDENZA, SONO LA
PROVA CHE NON C'E' NIENTE DI INDEFINITO.** Lo stesso decodificatore gira su
x86-64 con GCC e su i386 dentro EX-OS: se ci fosse un trabocco con segno, uno
spostamento oltre la larghezza o una lettura non allineata, le due macchine non
darebbero lo stesso scarto fino al centesimo.

## JPEG: il formato che non somiglia agli altri

PNG e BMP hanno i pixel dentro. Un JPEG ha i **coefficienti** di una
trasformata, quantizzati e poi compressi con Huffman, e per arrivare a un pixel
bisogna rifare la strada al contrario tutta intera:

    bit -> Huffman -> zigzag -> dequantizza -> IDCT -> YCbCr->RGB

I punti dove si sbaglia, e come si vede quando succede:

! **UN `0xFF` NEI DATI E' SEGUITO DA UNO `0x00` CHE NON ESISTE.** Byte stuffing:
`0xFF` apre un marcatore, quindi un `0xFF` che fa parte dei dati si scrive
`FF 00` e il secondo byte va buttato. Chi non lo fa perde l'allineamento dei
bit al primo `0xFF` — cioe' su quasi ogni foto, e a meta' immagine.

! **I NUMERI NON SONO IN COMPLEMENTO A DUE.** E' il codice EXTEND: con `s` bit,
i valori col bit alto a zero sono NEGATIVI. Senza quel passaggio meta' delle
differenze di luminosita' ha il segno sbagliato e l'immagine viene a bande.

! **I PIANI SI ALLOCANO PER MCU INTERE, NON PER LA MISURA DELL'IMMAGINE.** Una
21x13 in 4:2:0 ha MCU da 16x16: l'ultima colonna e l'ultima riga di blocchi
escono dal bordo, e sono blocchi che ESISTONO nel file e vanno decodificati.
Allocare la misura vera vuol dire scrivere fuori proprio sull'ultimo blocco —
per questo fra le prove c'e' una 21x13.

! **I MARCATORI DI RIPARTENZA AZZERANO IL CONTINUO E BUTTANO I BIT AVANZATI.**
Ignorarli su un file che li usa da' la foto giusta fino al primo e storta dopo.
`jpgrst.jpg` rende esattamente gli stessi numeri del file senza: e' cosi' che
si sa che sono gestiti e non solo saltati.

## L'IDCT, e il trabocco che la scala nascondeva

E' la versione **separabile e diretta**, non la piu' veloce, ed e' una scelta:
le IDCT veloci (AAN, Loeffler) fanno lo stesso lavoro con un quinto delle
moltiplicazioni ma non somigliano piu' alla formula — se sbagliano, nessuna
lettura del codice lo rivela. Sta in una funzione sua: il giorno che un Pentium
133 vero dicesse che e' troppo lenta, si sostituisce senza toccare altro.

! **LA TABELLA DEI COSENI E' A 4096 E NON A 8192, E IL MOTIVO E' LA SECONDA
PASSATA.** Le due passate moltiplicano per la tabella una volta ciascuna, quindi
la scala si applica **due volte**: con 8192 la somma delle colonne esce dai 32
bit su coefficienti che un file guasto puo' benissimo contenere. Un trabocco con
segno non e' un numero sbagliato, e' comportamento indefinito. Con 4096 e i
coefficienti limitati, il margine e' 16 volte sulle righe e 2 sulle colonne —
**calcolato, non sperato**.

! **E I DUE FATTORI DELLA DEQUANTIZZAZIONE VENGONO TUTT'E DUE DAL FILE**: il
coefficiente dalla scansione, il passo dalla tabella. 32767 per 65535 sono piu'
di due miliardi, quindi quel prodotto si fa a 64 bit e si limita subito. E' il
solo posto del file dove serve una moltiplicazione larga.

! **IL CONTINUO SI ACCUMULA DA UN BLOCCO ALL'ALTRO**, ed e' una somma di
differenze che nessuno controlla: su un file guasto cresce senza fermarsi.
Limitarlo costa un confronto e gli toglie l'unico modo che ha di diventare
enorme.

## Cosa il JPEG non legge, dichiarato

    progressivo (SOF2)    no — non e' una variante, e' una SECONDA decodifica:
                          i coefficienti stanno sparsi su piu' passate da
                          ricomporre prima di trasformare alcunche'. Vale piu' o
                          meno quanto tutto il resto del file
    aritmetico            no — era brevettato, non si incontra
    12 bit per campione   no
    CMYK / Adobe          no — vuole anche la trasformazione dei colori
    croma interpolato     no: si ripete. Sono i bordi a scaletta che si vedono
                          negli ingrandimenti, e quando servira' si tocca una
                          funzione sola

Tutti si rifiutano **per nome**, guardando il marcatore: senza, un progressivo
arriverebbe alla scansione e i suoi coefficienti parziali verrebbero letti come
completi — rumore invece di un messaggio che dice cosa fare.

## ICO: un contenitore, non un formato

! **UN `.ico` NON E' UN'IMMAGINE, E' UN ELENCO DI IMMAGINI.** La stessa icona a
16x16, 32x32 e 48x48, ognuna magari con una profondita' diversa. Prendere la
prima vorrebbe dire lasciare la scelta all'ordine in cui l'ha scritta il
programma che l'ha prodotta — cioe' a nessuno. **Si prende la piu' grande, e a
pari misura quella con piu' colori**: ingrandire una 16x16 si vede,
rimpicciolire una 48x48 no. Il file di prova ha la voce buona **in mezzo** alle
altre, che e' l'unico modo di provare che la scelta avviene davvero.

! **E DENTRO NON C'E' UN BMP, C'E' MEZZO BMP**: manca l'intestazione di file (i
14 byte con `BM`). Passare quei byte al lettore BMP darebbe «non e' mio».

! **L'ALTEZZA DICHIARATA E' IL DOPPIO DI QUELLA VERA.** Il DIB descrive due
bitmap impilate: i colori, e sotto la maschera di trasparenza a un bit. Chi la
prende per buona ottiene un'icona alta il doppio con la meta' inferiore piena
di spazzatura in bianco e nero — e a colpo d'occhio sembra un difetto dei
filtri, non della misura.

! **E DAL 2007 DENTRO PUO' ESSERCI UN PNG**, che per le icone grandi e' la
regola: quei byte si passano al lettore che abbiamo gia'. Un formato dentro
l'altro non e' un caso strano da tollerare.

! **I CONFINI SI CONTROLLANO CON UNA SOTTRAZIONE, NON CON UNA SOMMA.**
`off + len > n` con due numeri presi dal file puo' traboccare e diventare
**falso quando dovrebbe essere vero** — cioe' lasciar passare proprio il caso
che si voleva fermare. Si scrive `len > n - off`.

## Il generatore, e perche' l'encoder JPEG e' scritto in casa

`tools/mkimg.py` (era `mkpng.py`) adesso produce anche gli ICO e i JPEG, e per
i secondi c'e' dentro un **encoder baseline completo**: DCT diretta dalla
definizione, tabelle di Huffman standard, byte stuffing, marcatori di
ripartenza.

! **UN FILE PRODOTTO DA UNA LIBRERIA CHE NON SI LEGGE PROVA CHE I DUE PROGRAMMI
SI CAPISCONO, NON CHE IL NOSTRO E' GIUSTO.** L'encoder qui scrive esattamente
quello che dice la specifica e si vede leggendolo; la costruzione delle tabelle
di Huffman e' la stessa che il lettore fa al contrario con mincode/maxcode, e se
una delle due sbaglia non si capiscono.

! **E IL CONFRONTO NON PUO' ESSERE «IDENTICO», PERCHE' IL JPEG PERDE.** La
tabella di quantizzazione si mette **tutta a uno**, cosi' l'unico errore che
resta e' quello di arrotondamento: quello che si pretende e' che l'immagine
torni vicina all'originale, e quanto vicina lo dicono i numeri in cima — 0,58
su 4:4:4, dove il croma non viene buttato via.

# eximg.so: il PNG entra, e nessuno lo paga (17 agosto 2026)

    exwin -s /exwin/prova.png

    l'immagine sullo schermo, confrontata coi pixel attesi:
        3072 pixel su 3072 IDENTICI

    a terra, i cinque tipi di colore:
        rgb 64x48  rgba 40x24  grigio 33x17  grigio+alfa 20x20  tavolozza 50x30
        tutti identici, 0 pixel diversi

    600 file guasti sotto AddressSanitizer + UBSan:  0 guasti

    exwin -s /boot/help.txt
        pm: /boot/help.txt: formato non riconosciuto   <- e la scrivania parte

`inflate.c` e `png.c` erano scritti da ieri e **non erano collegati a niente**:
non comparivano nel Makefile e la tabella dei lettori in `exwin.c` era ancora
`{ leggi_bmp, 0 }`. Adesso c'e' la libreria, ed e' la quarta fetta:
**0x04C00000**, 17.976 byte.

## Non si collega: si apre quando serve

! **UN PANNELLO, UN OROLOGIO, UNA BARRA DELLE APPLICAZIONI NON DISEGNANO UN PNG
MAI.** Se il toolkit si collegasse a eximg, la pagherebbero anche loro — ed e'
esattamente la ragione per cui e' una libreria a parte invece di due funzioni
dentro `exwin.c`. Per questo `leggi_eximg()` e' **l'ultimo** della tabella e
apre la libreria con `exlib_apri_fra()` solo davanti a byte che nessun altro ha
riconosciuto. Aperta una volta, l'indirizzo resta.

! **E PER QUESTO NON C'E' UNO STUB.** exdlg ne ha uno perche' chi apre file lo
sa quando lo si compila; qui no: se ne accorge `ex_immagine()` guardando i
primi byte di un file, a programma gia' partito.

! **SE eximg.so NON C'E', NON SI MUORE.** Gli stub di exwin e exdlg gridano e
muoiono, e li' e' giusto: un programma che chiama `ex_finestra()` senza toolkit
non puo' fare niente. Qui il programma sa disegnare, sa leggere i BMP, e gli
manca un formato — farlo morire vorrebbe dire che installare mezza libreria
grafica spegne applicazioni che funzionerebbero. Si rende 0, che vuol dire
«questo formato non lo so leggere», ed e' la stessa risposta di un file guasto.

## La memoria: chi decodifica non libera

Un decodificatore ha molte uscite — un pezzo troncato, una profondita' che non
si sa leggere, un albero di Huffman incoerente — e il PNG ne ha otto.

! **CHIEDERE A OGNI USCITA DI RICORDARSI COSA LIBERARE VUOL DIRE CHE PRIMA O
POI UNA SE NE DIMENTICA.** Cosi' invece `eximg_memoria()` **annota** ogni
blocco, e quando la decodifica finisce `eximg_carica()` li restituisce tutti
tranne quello dei pixel. Il decodificatore non libera niente e non puo'
sbagliare: non e' lui a farlo.

Il limite e' otto blocchi, dichiarato: il PNG ne chiede tre.

## Le prove, e perche' tre livelli

**A terra**, compilando `lib/eximg/` per Linux: cinque PNG generati da
`tools/mkpng.py`, uno per tipo di colore, confrontati coi pixel attesi.

! **IL PATTERN NON E' CASUALE, E' UNA FORMULA DEGLI INDICI.** Cosi' i pixel
attesi si ricavano dalla stessa formula invece che da un secondo
decodificatore di cui bisognerebbe fidarsi — e «giusto» vuol dire identico,
non «sembra giusto».

! **E OGNI FILE ESERCITA TUTTI E CINQUE I FILTRI, UNA RIGA PER TIPO A GIRO.**
Un generatore che scrivesse tutte le righe col filtro 0 produrrebbe un file che
passa senza aver provato niente: **Paeth e' quello che si sbaglia**, e col
filtro 0 non viene nemmeno chiamato. Gli IDAT sono spezzati in tre pezzi
apposta, perche' un decodificatore che li tratta uno per uno legge un flusso
troncato e con un pezzo solo quel difetto non si vedrebbe.

**Sotto sanitizzatore**, 600 file guastati a mano — troncati, con byte
cambiati, con le misure in IHDR falsate, con le lunghezze dei pezzi assurde:
nessun accesso fuori dai limiti, nessun trabocco, nessuna perdita.

! **QUESTO E' IL LIVELLO CHE CONTA DI PIU', E VA DETTO PERCHE'.** Un
decodificatore di immagini legge **file che arrivano da fuori**: e' la
superficie d'attacco piu' esposta che ci sia in un'applicazione grafica. E su
EX-OS non c'e' il bit NX — un trabocco di buffer e' direttamente sfruttabile.
Provarlo solo con file corretti vorrebbe dire provarlo proprio dove non serve.

**In volo**, dentro EX-OS: `exwin -s /exwin/prova.png`, foto dello schermo, e i
64x48 pixel confrontati con gli attesi. **3072 su 3072 identici** — e in mezzo
c'erano l'apertura pigra della libreria, l'inflate, i filtri, `ex_pixmap`, il
compositore MMX e il framebuffer.

## L'immagine di prova sta sul CD, e si genera

! **SENZA UN'IMMAGINE SUL SUPPORTO IL LETTORE NON E' PROVABILE DA DENTRO**, e
`winprova -s` era un'opzione che non era mai stata eseguita per mancanza di un
file da darle. 1,6 KB sul CD.

! **E NON E' COMMITTATA: SI GENERA.** Un PNG nel repository e' un blob di cui
nessuno sa piu' com'e' fatto; `tools/mkpng.py` lo produce da una formula, che
dice sia il pattern sia quali filtri esercita — e accanto scrive i pixel
attesi, con cui la foto si confronta.

## Quello che il PNG non legge, dichiarato

    interlacciamento Adam7      no — sono SETTE immagini da ricomporre, e un
                                pezzo che si esercita quasi mai e' un pezzo di
                                cui non si sa se funziona
    16 bit per canale           no — si troncherebbero a 8 per andare a schermo
    il canale alfa              letto e ignorato: il server compone finestre
                                opache, non c'e' niente su cui fondere

# Il TSC misura e PSE si accende (17 agosto 2026)

    [INFO]  TSC: 1896.978 MHz (94848944 cicli in 50 ms)
    [INFO]  PAGING: PSE attivo — 7 blocchi da 4 MB (0x00400000-0x02000000),
                    il resto a 4 KB
    [INFO]  PAGING: spezzamento provato su 0x01c00000 — le traduzioni coincidono

    (qemu) info registers
    CR0=80000033  CR4=00000610        <- bit 4 = PSE, acceso davvero

    libctest   294 su 294      polltest 13 su 13      shmtest 11 su 11
    da CD: wserver, MMX, scrivania, tre applicazioni nel menu — nessun fault

Delle tre cose che il Pentium MMX comprava, **due sono adesso in uso**: MMX era
gia' nel compositore, TSC e PSE arrivano qui. Resta fuori solo il bit NX, che
non arriva col Pentium MMX e non arrivera' mai.

## Il TSC: 50 millisecondi contro il canale 2 del PIT

Fino a qui l'unico orologio del kernel era `g_ticks`, il PIT a 100 Hz: unita' 10
millisecondi. Misurare MMX nel compositore con quello dava «0 tick» in entrambi
i casi.

! **LA CALIBRAZIONE NON PUO' USARE `g_ticks`, ED E' IL PUNTO MENO OVVIO.**
`tsc_init()` gira durante l'avvio, e in EX-OS l'IRQ0 resta mascherato fino a
`sched_start()`, che e' l'ultima riga di `kernel_main`. `g_ticks` vale zero e li'
resterebbe: il ciclo di attesa non finirebbe mai e la macchina si pianterebbe
all'avvio, **prima di scrivere la riga di log che spiega perche'**.

Si usa il **canale 2** del PIT, l'unico dei tre leggibile senza interrupt: il suo
piedino di uscita e' nel bit 5 della porta 0x61. Nato per l'altoparlante, serve
qui da cronometro — e il bit dell'altoparlante va spento a mano, o una macchina
che l'aveva acceso fischierebbe per 50 ms a ogni avvio.

! **IL NUMERO E' STATO CONTROLLATO CONTRO LA MACCHINA VERA**: 1897,091 MHz
misurati contro 1895,845 dichiarati dall'host. Errore dello **0,066%** — cioe'
il numero e' giusto, non soltanto plausibile. Una calibrazione che rende un
valore verosimile e sbagliato e' peggio di una che non rende niente.

## PSE: la fascia kernel in sedici voci di TLB invece di sedicimila

La fascia kernel e' identita' pura, permessi uguali dappertutto, e non cambia
mai. Descriverla pagina per pagina vuol dire fino a 16.384 PTE — e soprattutto
**una voce di TLB per ogni 4 KB toccati**.

! **IL GUADAGNO E' NEL TLB, NON NELLA MEMORIA RISPARMIATA.** Le tabelle in meno
sono 28 KB su 32 MB: niente. Il punto e' che il Pentium ha **32 voci** di TLB
per i dati, e un kernel che ne consuma una ogni 4 KB le esaurisce attraversando
una struttura appena piu' grande di 128 KB. Le pagine da 4 MB hanno un TLB
separato, dedicato: la fascia kernel smette di competere con i dati del
processo.

! **I PRIMI 4 MB RESTANO A PAGINE DA 4 KB, SEMPRE.** Dentro c'e' la finestra
(`PAGING_FINESTRA_VIRT`, l'ultima pagina dei primi 4 MB), che per mestiere cambia
mappatura una pagina per volta. Un blocco da 4 MB li' verrebbe spezzato al primo
uso della finestra, cioe' subito: si pagherebbe un'allocazione per tornare
esattamente dove si era.

! **E VA ACCESO IN CR4 PRIMA DI SCRIVERE LA PRIMA PDE COL BIT 7.** Senza
CR4.PSE quel bit e' **riservato**, e una PDE riservata non da' errore: la CPU la
legge come una tabella il cui indirizzo ha bit di troppo. Una mappatura verso
memoria a caso, silenziosa.

## Il ramo difensivo si esegue a ogni avvio

`spezza_4mb()` riporta un blocco da 4 MB alla tabella equivalente. Esiste perche'
il giorno che qualcuno chiedesse una pagina sola dentro un blocco,
`paging_map_page` prenderebbe il campo indirizzo della PDE per un puntatore a
tabella e **scriverebbe la PTE dentro la memoria mappata** — dentro il kernel
stesso, se il blocco e' il primo.

Oggi non lo chiama nessuno.

! **QUINDI E' CODICE DI CUI NON SI SAPREBBE SE FUNZIONA**, ed e' la stessa
ragione per cui il compositore ha `-nommx`. `prova_spezzamento()` lo esegue su un
blocco vero a ogni avvio: spezza l'ultimo blocco, controlla che quattro
traduzioni scelte — primo byte, seconda pagina, meta', ultimo byte — dicano
esattamente quello che dicevano prima, **rimette il blocco e restituisce la
tabella**. Non lascia niente per terra e costa una pagina presa e resa.

! **E GIRA PRIMA DI CR0.PG, DELIBERATAMENTE.** Con la paginazione ancora spenta
non c'e' TLB da invalidare e nessuna traduzione in uso: se lo spezzamento fosse
sbagliato si scopre li', invece di scoprirlo mentre la CPU sta usando quella
mappa per eseguire la funzione che la sta cambiando.

## I due limiti, dichiarati

! **SPEZZARE UN BLOCCO DOPO L'AVVIO NON SI PROPAGA ALLE PD GIA' CREATE.**
`paging_create_directory` **copia** le entry sotto `USER_SPACE_BASE`: finche'
sono puntatori a tabella la tabella e' la stessa e una modifica si vede da
tutti; un blocco da 4 MB invece **e' il valore stesso**, e chi l'ha gia' copiato
continua a usarlo. Non morde oggi — la fascia kernel si mappa una volta sola in
`paging_init`, prima che esista una PD di processo, e nessuno la rimappa — ma
il giorno che servisse, la strada e' spezzare il blocco **all'avvio**, non dopo.

! **NIENTE `PG_GLOBAL`.** Il bit che tiene una voce nel TLB attraverso un cambio
di CR3 vuole CR4.PGE, cioe' un Pentium Pro. Sulla CPU dichiarata non c'e', e
scriverlo senza PGE vorrebbe dire mettere un bit che questa CPU ignora e una
futura no.

## Cosa non e' stato misurato, e perche'

**Il guadagno in tempo reale**, per la stessa ragione di MMX: QEMU traduce con un
JIT e non ha il TLB di un Pentium 133. Cio' che si puo' affermare e'
strutturale — sette voci di TLB al posto di settemila — e che il sistema fa le
stesse cose: 294 prove della libc, poll, memoria condivisa e la scrivania
grafica intera.

Il numero vero si prende su ferro. **E adesso c'e' con cosa prenderlo**: e' il
motivo per cui il TSC e PSE stanno nello stesso lavoro.

# MMX nel compositore (17 agosto 2026)

    [PID 16] wserver: MMX attivo, otto byte per volta

    exwin            -> foto  1.440.015 byte
    exwin -nommx     -> foto  1.440.015 byte
    cmp              IDENTICI byte per byte

Le due strade disegnano **esattamente lo stesso schermo**.

## Perche' proprio qui e in nessun altro posto

! **IL COMPOSITORE E' L'UNICO POSTO DEL SISTEMA DOVE LA LARGHEZZA DELLA COPIA
SI VEDE.** A 800x600x32 un fotogramma sono **1,83 MB** scritti nel framebuffer,
e si riscrive tutto a ogni cambiamento — e' gia' stato il difetto che teneva il
server occupato tanto da far scadere le richieste dei client. Ovunque altro in
EX-OS si copiano decine di byte e non conta niente.

I due cicli caldi sono il riempimento dello sfondo e la copia di ogni finestra:
prima quattro byte per istruzione, adesso otto.

## Le tre cose che rendono MMX sicuro qui

! **I REGISTRI MMX SONO QUELLI DELL'x87**, e il kernel salva lo stato FPU al
cambio di contesto (`fnsave`/`fxsave`, con commutazione pigra via `CR0_TS`).
Senza quel salvataggio — che esiste da agosto per tutt'altra ragione, TCC —
due processi che usassero MMX si calpesterebbero i registri a vicenda.

! **`emms` ALLA FINE DI OGNI GIRO.** Dopo un'istruzione MMX i registri x87
restano marcati «in uso»: la prima istruzione in virgola mobile che arriva
dopo — anche in un ALTRO processo, se lo scheduler entra prima — trova uno
stack che non e' suo. E' l'errore classico di MMX, e **non da' nessun sintomo
finche' qualcuno non usa la virgola mobile**.

! **SI CONTROLLA CON CPUID E SI RIPIEGA.** La CPU di base dichiarata ha MMX,
ma un Pentium liscio no: senza il controllo, su quella macchina il server
morirebbe con un'istruzione non valida invece di andare piu' piano.

## `-nommx`, e perche' non e' un'opzione per curiosi

! **UN CODICE DI EMERGENZA MAI ESEGUITO E' UN CODICE DI CUI NON SI SA SE
FUNZIONA.** La strada senza MMX esiste per le macchine che non ce l'hanno, e
su una macchina con MMX non verrebbe eseguita mai. Il flag la rende
provabile — ed e' cosi' che si e' potuto confrontare le due fotografie byte per
byte, che e' l'unica prova che conta per un compositore.

! **E `exwin` PASSA AL SERVER CIO' CHE NON RICONOSCE.** Elencare le opzioni del
server anche nel lanciatore sarebbe una seconda verita' accanto a quella vera,
e le due divergono alla prima opzione aggiunta.

## Cosa NON e' stato misurato, e perche'

**Il guadagno in tempo reale.** QEMU traduce le istruzioni con un JIT: un
cronometro li' dentro non dice niente su un Pentium 133. Quello che si puo'
affermare e' strutturale — **le scritture in memoria sono dimezzate**, 240.000
`movq` invece di 480.000 `mov` per un riempimento a schermo intero — e che il
risultato e' identico.

Il numero vero si prende su ferro, ed e' una delle cose che varra' la pena
misurare quando ci sara' una macchina d'epoca sotto mano.

## Cosa resta del Pentium MMX

    PSE   pagine da 4 MB per la mappatura del kernel: meno tabelle, meno TLB
    TSC   misure di tempo precise — servirebbe proprio per misurare MMX

**Fatti tutti e due**, lo stesso giorno: vedi «Il TSC misura e PSE si accende».

# La CPU di base diventa vera, e il codice smette di essere scrivibile (17 agosto 2026)

    make verifica-cpu
    [OK] nessuna istruzione oltre il Pentium MMX in 66 file

## No, non gira su 486 — e non girava nemmeno sul Pentium MMX

L'utente ha alzato la CPU di base a **Pentium 133 MMX**. La prima cosa e' stata
misurare cosa i binari contenessero davvero:

    -march=  i686        (il default di GCC, che nessuno aveva cambiato)
    cmov                 591 occorrenze nei binari

! **`cmov` E' DEL PENTIUM PRO.** Non ce l'hanno ne' il 486, ne' il Pentium, ne'
il Pentium MMX. Il sistema **non girava sulla CPU appena dichiarata**: si
sarebbe fermato con un'eccezione di istruzione non valida al primo programma —
su ferro vero, non in QEMU, che emula una CPU moderna e non se ne accorge.

! **UN REQUISITO CHE NESSUNO VERIFICA NON E' UN REQUISITO.** Da qui il
bersaglio `verifica-cpu`, che **rilegge i binari prodotti** invece di
controllare i flag: controllare i flag direbbe solo che li abbiamo scritti.

## E il bit NX non arriva col Pentium MMX

Va detto perche' la CPU era stata alzata anche per quello: **il non-execute
richiede PAE (Pentium Pro) e in pratica arriva coi Pentium 4 / Athlon 64**. Sul
P55C una pagina di dati resta eseguibile per sempre.

Ma **meta' della difesa si poteva avere lo stesso, e non era la CPU**:

    prima:  LOAD ... RWE      un segmento solo: IL CODICE ERA SCRIVIBILE
    dopo:   LOAD ... R E      il codice
            LOAD ... RW       i dati

! **ERANO I NOSTRI LINKER SCRIPT.** Il kernel rispettava gia' `PF_W`
correttamente; erano gli script a mettere `.text` e `.data` nella stessa
pagina, cosi' il collegatore ne faceva un segmento solo coi permessi
dell'unione. L'avviso si vedeva a ogni collegamento e nessuno lo leggeva:

    ld: warning: build/bin/sh has a LOAD segment with RWX permissions

64 script corretti con un `. = ALIGN(4096)`. Costo: **+12,9 KB su tutto /bin**.

## Il canarino dello stack

Acceso nei programmi utente (`-fstack-protector-strong`). Non impedisce
l'overflow: lo fa **accorgere**, e costa qualche istruzione per funzione senza
chiedere niente al processore — l'unica difesa di questa classe che si possa
avere qui.

! **IL CANARINO E' UNA VARIABILE, QUINDI STA NEL PROGRAMMA**, non nella libc
condivisa: e' la stessa trappola di `errno`. Sta in `lib/libc_avvio.c`, accanto
a `_libc_start`, per la stessa ragione.

! **E SENZA `-mstack-protector-guard=global` NON PARTIVA NIENTE.** Su i386 GCC
cerca il canarino in `%gs:0x14` — lo slot TLS di Linux — e su EX-OS quel
segmento non c'e':

    [FAULT] PID 6 'sh2': page fault a 0x00000014 (lettura)

**Il numero lo diceva.** Ed e' arrivato insieme al cambio di architettura senza
esserne una conseguenza: era la CONVENZIONE su dove sta il canarino. Le due
cose si somigliano solo perche' capitano nello stesso momento — conviene
guardare l'indirizzo prima della teoria.

La shell il canarino se lo definisce da se' (non collega la libc), ed e' il
programma che piu' lo merita: legge righe scritte da una persona e le spezza in
buffer di lunghezza fissa.

## `memcpy` comparsa dal nulla

Cambiando `-march`, GCC ha deciso che per certe copie conviene **chiamare**
`memcpy` invece di aprirla in istruzioni, e il kernel si e' fermato su
`undefined reference to memcpy`. Il codice del kernel non era cambiato di una
riga.

! **`-ffreestanding` NON VUOL DIRE «nessuna funzione di libreria»**: il
compilatore resta libero di generare chiamate a `memcpy`, `memmove`, `memset` e
`memcmp`. Sono le quattro che ogni ambiente freestanding deve fornire, e adesso
ci sono in `kernel/arch/x86/memfun.c`.

## ! SETTIMA E OTTAVA VOLTA: «un'uscita che non sa di essere scaduta»

 7. **I programmi non dipendono dal Makefile**, quindi cambiare `CFLAGS` non
    ricostruisce niente. La costruzione passava e nei binari restavano 273
    `cmov`. Da qui il **segnaposto dei flag**: il nome del file contiene
    l'impronta delle opzioni, e cambiarle lo fa sparire.
 8. **Il segnaposto viene creato PRIMA dei suoi dipendenti**: una costruzione
    fallita a meta' lo lascia soddisfatto, e al giro dopo make crede che sia
    tutto in ordine. Ora la sua ricetta **butta gli oggetti** — cio' che manca
    non torna a esistere da solo — compresi quelli del kernel, che non stanno
    in `build/obj` e per questo avevano conservato 207 `cmov` mentre tutti i
    programmi erano gia' a posto.

## Cosa compra davvero il Pentium MMX, e cosa no

    NO   il bit NX (serve PAE, e in pratica un Pentium 4)
    SI'  MMX: otto byte per volta nel compositore del server grafico
    SI'  PSE: pagine da 4 MB per la mappatura del kernel
    SI'  TSC: misure di tempo precise
    (gia' usati: CPUID per riconoscere le funzionalita')

**Tutti e tre sono adesso in uso**, e in quest'ordine: MMX nel compositore,
poi TSC e PSE insieme. Il bit NX resta l'unica cosa che questa CPU non puo'
dare.

# Audit di sicurezza del kernel: i permessi erano decorativi (17 agosto 2026)

Chiesto dall'utente subito dopo aver acceso i permessi sui file. La prima cosa
che l'audit ha detto:

    syscall in tutto:                                    78
    che guardavano l'uid:                                 1   (setuid)
    che scavalcavano il filesystem senza chiedere:        6

! **UN CONTROLLO ACCURATO ACCANTO A UNA PORTA CHE NON CHIEDE NIENTE NON E' UN
CONTROLLO.** `SYS_BLKWRITE` scriveva settori grezzi di una partizione senza
sapere chi fosse: un utente normale non poteva scrivere `/boot/ombra` passando
dalla VFS, ma poteva riscrivere il settore che lo contiene — o `/bin/sh`, o
l'intero filesystem.

! **ED E' LA DOMANDA GIUSTA DA FARSI DOPO OGNI CONTROLLO NUOVO**: non «questo
controllo e' giusto?», ma **«esiste un'altra strada per ottenere la stessa
cosa?»**. Qui ce n'erano sei, piu' una settima piu' grave.

## Le sette porte chiuse

    blkread / blkwrite    settori grezzi di una partizione
    partwrite             la tabella delle partizioni
    bootinstall           l'MBR e il settore di avvio
    mount / umount        cosa si vede e da dove
    reboot                spegnere la macchina

## E l'ottava, che era la piu' grave: il varco dei driver

`is_driver` lo accendeva il caricatore guardando **solo il nome del file**, e i
driver installati sono 0755. Un utente normale poteva eseguire `/dev/ide.drv`,
ottenere il varco, e con `ioport_bind` parlare **direttamente al controller del
disco** — leggere e scrivere qualunque settore senza passare da un solo
controllo.

Era la stessa forma del difetto di `blkwrite`, un piano piu' sotto.

! **`mmio_map` GIA' SI DIFENDEVA**, e va detto: rifiuta qualunque indirizzo
sotto il limite della RAM, quindi le tabelle delle pagine non erano mappabili.
Il pericolo erano le porte I/O.

## La correzione non e' stata solo chiudere: `SYS_FB_MAP`

! **UNA CAPACITA' STRETTA AL POSTO DI UNA LARGA.** `mmio_map()` mappa un
indirizzo fisico QUALUNQUE scelto da chi chiama, e per questo dev'essere di
root. Ma il server grafico non vuole un indirizzo qualunque: vuole **il**
framebuffer, che il kernel conosce gia'.

`fb_map()` non prende argomenti. Chi chiama non puo' sbagliare indirizzo e chi
attacca non puo' sceglierne uno. **Non serve nessun privilegio.**

! **E HA AVUTO UN EFFETTO CHE NON CERCAVO:** `wserver` non ha piu' bisogno del
varco dei driver, quindi puo' girare come utente normale. Senza, o il server
era di root — e ogni utente che vuole le finestre ha bisogno
dell'amministratore — oppure il varco restava aperto a tutti, e allora i
permessi sui file non valevano niente.

## I nomi dei servizi IPC, e la grafica per utente

`ipc_register` rifiutava di rubare un nome a un servizio **vivo**, ma non di
prenderlo prima che nascesse o dopo che era morto. Chi registra `kbd` riceve i
tasti che tutti gli mandano; chi registra `wserver` riceve le finestre.

! **LA REGOLA E' MECCANICA E NON HA UN ELENCO**: un utente normale puo'
registrare solo nomi che cominciano col proprio uid — `1000:mio`. Un elenco di
nomi riservati sarebbe una seconda verita' accanto ai servizi che esistono
davvero, e le due divergono al primo servizio aggiunto.

! **ED E' COSI' CHE LA GRAFICA DIVENTA MULTIUTENTE.** root registra `wserver`,
chiunque altro `<uid>:wserver`. Ogni utente ha il suo server, sulla sua
console, e non vede quello degli altri — non perche' glielo si impedisca con un
controllo in piu', ma **perche' non ne conosce il nome**. Server e toolkit
compongono il nome con la stessa funzione: se divergessero, il client
cercherebbe un servizio che nessuno ha registrato.

## Gli ultimi otto slot di processo sono di root

I processi sono 64 e non c'era nessun limite per utente: un ciclo che fa spawn
di se' stesso li riempie in un istante, e da quel momento **nessuno puo' piu'
avviare niente, l'amministratore compreso**.

! **LA RISERVA E' PER root, NON «PER IL SISTEMA»**, e la differenza conta: slot
tenuti liberi genericamente si riempirebbero al primo processo di sistema che
parte. Riservarli a chi puo' spegnere e riparare e' l'unica regola che
garantisce il rimedio.

## Cosa resta aperto, e va detto

! **NIENTE NX: OGNI PAGINA ESEGUIBILE E' ANCHE SCRIVIBILE.** Su i386 senza PAE
il bit non esiste, e il collegatore lo dice a ogni programma («LOAD segment
with RWX permissions»). Un overflow di buffer e' direttamente sfruttabile.
Toglierlo vuol dire PAE, cioe' tabelle delle pagine a tre livelli: un lavoro di
kernel a se'.

! **SHA-256 SENZA IRROBUSTIMENTO** per le password — gia' dichiarato in
login.c. E' veloce per costruzione: chi si porta via `/boot/ombra` prova molti
candidati al secondo. Il sale impedisce le tabelle precalcolate, non la forza
bruta.

! **NESSUNA QUOTA DI MEMORIA NE' DI DISCO.** Lo heap ha un tetto per processo,
ma un utente puo' aprire piu' processi; e nessuno gli impedisce di riempire il
disco.

! **`blkread`/`blkwrite` NON VERIFICANO IL PUNTATORE UTENTE** (le uniche due
su 78). Adesso sono di root, quindi la gravita' scende, ma un puntatore
sbagliato di un programma di root fa cadere il kernel invece di dare EFAULT.

! **SU FAT E ISO 9660 I PERMESSI NON ESISTONO**, per costruzione. Montare un
volume FAT e' montare uno spazio in cui tutti possono tutto — ed e' per questo
che `mount` e' diventata di root.

## Multiutenza remota e SSH — cosa c'e' e in che ordine va fatto

Chiesto dall'utente insieme all'audit. **L'analisi dice che l'ordine ovvio e'
sbagliato**: la parte difficile non e' la crittografia, e' l'impianto della
sessione.

### Cosa c'e' oggi

    TCP            SOLO CLIENTE: nel driver ip c'e' tcp_apri(), cioe' connect.
                   Non esistono listen ne' accept — niente puo' SERVIRE.
    pseudo-terminali  non esistono affatto
    SHA-256        c'e', nella libc (la usa login)
    cifrari        nessuno

### L'ordine, e perche'

 1. **`listen` e `accept` nel driver IP.** Senza, non c'e' niente da discutere:
    nessun servizio puo' accettare una connessione. E' il primo mattone.

 2. **Uno strato di pseudo-terminali (pty).** ! **E' IL PEZZO CHE SI SOTTOVALUTA
    SEMPRE.** Una shell remota su una pipe nuda e' esattamente la shell dentro
    una finestra prima del 14 agosto: niente eco, niente Backspace, niente
    Ctrl+C, niente misura dello schermo. Quelle cose le fa la *line discipline*,
    e una pipe non ne ha. Un pty e' una pipe CON una line discipline attaccata,
    ed e' cio' che rende una connessione una sessione.

    E serve **anche al terminale in finestra**, che oggi ha lo stesso buco
    dichiarato (`Ctrl+C` impossibile). Un solo lavoro, due difetti chiusi.

 3. **Un server telnet.** Prova tutto l'impianto — accettare, avviare `login`,
    scendere con `setuid`, dare la shell, ripulire alla chiusura — **senza
    crittografia**. Se qualcosa non torna, si vede in chiaro.

 4. **SSH**, che a quel punto e' «lo stesso impianto piu' la crittografia»:
    scambio di chiavi, un cifrario, un MAC, le chiavi d'host, il protocollo
    binario.

! **E SE SI FA SSH, SI SCEGLIE LA STRADA SENZA BIGNUM.** Curve25519 per lo
scambio e ChaCha20-Poly1305 per il resto sono aritmetica a 32 bit su numeri di
lunghezza fissa; RSA vuole un modulo esponenziale su interi da 2048 bit, cioe'
una libreria di grandi numeri da scrivere e mantenere. Con SHA-256 gia' in
casa, la prima strada e' qualche centinaio di righe di matematica chiusa; la
seconda e' un sottosistema.

! **E NIENTE SSH PRIMA CHE I PERMESSI SIANO SOLIDI.** Un servizio che accetta
connessioni da fuori e' la prima cosa che qualcuno prova: ha senso aprirlo
quando dentro c'e' qualcosa che regge, non prima. L'audit di oggi e' stato il
primo passo di quel lavoro, non l'ultimo — vedi «cosa resta aperto».

# Il controllo sull'esecuzione, e tre messaggi che mentivano (17 agosto 2026)

    mario, nella sua casa:

    ex-os:/home/mario> cp /bin/hello mio
    ex-os:/home/mario> /home/mario/mio
    exec: /home/mario/mio: non hai il permesso di eseguirlo
          Se il file e' tuo:  chmod 755 /home/mario/mio
    ex-os:/home/mario> chmod 755 /home/mario/mio
    ex-os:/home/mario> /home/mario/mio
    Ciao da /bin/hello!

**Le sette regole dettate il 17 agosto sono tutte vere.**

## `vfs_eseguibile()`, chiamata dal caricatore

! **IL PERMESSO SI CHIEDE PRIMA DI APRIRE.** Aprire e poi rifiutare vorrebbe
dire aver gia' toccato il filesystem — e su EX-OS aprire un file e' un giro di
IPC verso un driver in ring 3, cioe' un punto di riscadenzamento. Chiedere
prima costa una stat e non lascia niente a meta'.

! **ED E' LA RIGA CHE CHIUDE IL DIFETTO DEL VARCO `*.drv`**, dichiarato da
giorni. Il varco diceva che `mmio_map()` la puo' chiedere solo un eseguibile
caricato da un file `.drv`, ed era **una definizione, non una difesa**: bastava
copiarsi in `x.drv`. Adesso `/dev` appartiene a root, un utente normale non ci
puo' scrivere dentro, e un `.drv` fatto altrove non e' eseguibile se non lo si
possiede.

! **ESEGUIRE RICHIEDE ANCHE DI LEGGERE, ed e' un limite dichiarato.** Su Unix
basta il bit x, perche' e' il kernel a leggere il file per conto di exec; qui il
caricatore apre con `vfs_open`, che chiede il permesso di lettura. Un file 0711
non parte. Non ne esiste nessuno su EX-OS, e sistemarlo vorrebbe dire un
percorso di apertura privilegiato: piu' superficie d'attacco di quanta ne
tolga.

## `/bin/chmod` e `/bin/chown`

Un binario con due nomi, come `id`/`whoami`.

! **ESISTONO PERCHE' SENZA NON C'ERA MODO DI SISTEMARE UN PERMESSO A MANO.** Un
sistema di permessi senza gli attrezzi per governarli e' un sistema in cui il
primo errore e' definitivo. E servono davvero: su un disco installato prima di
oggi tutti i programmi sono 0644.

`chmod` legge l'ottale e lo dice se non lo e' — `755` in decimale sarebbe bit
sparsi a caso. `chown` accetta il nome o il numero.

## ! TRE MESSAGGI CHE MENTIVANO, E LA CATENA CHE LI PRODUCEVA

Il primo tentativo funzionava e diceva la cosa sbagliata:

    exec: /home/mario/mio: non e' un programma eseguibile

**Lo era.** Mancava il permesso. Il difetto era in tre punti diversi, uno
dentro l'altro:

 1. **`sys_spawn` schiacciava ogni motivo in `ENOEXEC`.** Il commento sopra
    quella riga diceva gia' «SI RENDE IL MOTIVO VERO, non ENOENT per tutto» —
    il principio era giusto, e con i permessi i motivi veri sono diventati
    **tre** invece di due. E' lo stesso errore che quel commento denunciava, un
    caso piu' in la';
 2. **la shell non conosceva `EACCES`** e cadeva nel ramo di ENOEXEC;
 3. e la prima correzione suggeriva **`ls -l`**, che `ls` di EX-OS non ha: un
    consiglio che non si puo' seguire e' peggio di nessun consiglio.

Adesso dice il motivo vero e un rimedio che esiste.

## Le prove

    mario, file 0644     rifiutato, con il motivo giusto e il comando per
                         rimediare
    dopo chmod 755       parte
    da floppy (FAT12)    libctest 294 su 294, uid=0, redirezione e grafica
                         intatte — dove i permessi non ci sono, non mordono

# I permessi mordono: proprietario scritto e controlli attivi (17 agosto 2026)

      utente: mario
      password: ******
    ex-os:/home/mario> id
    uid=1000(mario) gid=1000
    ex-os:/home/mario> hello > mio.txt         <- la casa e' sua
    ex-os:/home/mario> hello > /bin/x
    sh: non riesco ad aprire /bin/x            <- RIFIUTATO
    ex-os:/home/mario> cat /boot/ombra
    cat: /boot/ombra: file non trovato         <- le impronte sono di root

Le sette regole dettate il 17 agosto sono **tutte vere**, tranne il controllo
sull'esecuzione — i bit ci sono, il controllo no, e il perche' e' in fondo.

## Il proprietario si scrive

! **CHI CREA E' UN PARAMETRO, NON UNO STATO NASCOSTO.** Un «uid corrente»
tenuto in una variabile del modulo andrebbe impostato prima di ogni chiamata, e
la volta che qualcuno se ne dimenticasse il file nascerebbe di root **senza che
nessuno lo dica**. Un parametro non si puo' dimenticare: lo chiede il
compilatore.

File 0644, directory 0755. Il bit x su una directory non vuol dire
«eseguibile», vuol dire **attraversabile**: senza, nessuno potrebbe entrarci
nemmeno per leggere un file suo.

## `vfs_permesso()` — e «root passa» sta scritto UNA volta

! **SPARSO NEI SINGOLI CONTROLLI SAREBBE LA COSA PIU' FACILE DA DIMENTICARE IN
UNO DI ESSI** — e un controllo che dimentica root e' un sistema in cui
l'amministratore non puo' riparare niente.

! **UN FILESYSTEM SENZA PROPRIETARI LASCIA PASSARE TUTTO**, e non e' una falla:
su FAT `modo` vale 0 e non c'e' niente su cui decidere. Rifiutare li' renderebbe
illeggibile un floppy; fingere un proprietario vorrebbe dire decidere in base a
un dato inventato.

! **CREARE, CANCELLARE E RINOMINARE GUARDANO IL PADRE, NON IL FILE.** Cambiano
l'elenco della directory: chi guardasse i permessi del file lascerebbe
cancellare a chiunque un file leggibile da tutti. E rinominare ne guarda
**due** — toglie una voce da una directory e ne mette una in un'altra.

## `chown`, `chmod`, e perche' si comportano diversamente su FAT

`chown` lo puo' fare solo root: se un utente potesse regalare i propri file
potrebbe anche **prendersi** quelli che trova. `chmod` lo puo' fare il
proprietario: cambiare i propri permessi non toglie niente a nessuno.

! **SU FAT `chown` RENDE ENOSYS E `chmod` RENDE 0**, ed e' voluto. `chown`
chiede di CONSEGNARE un file: su FAT quel concetto non esiste, e dire «fatto»
farebbe credere che il file ha cambiato padrone. `chmod` chiede di METTERE dei
permessi, e lo stato che si ottiene — nessuna restrizione — e' gia' quello del
volume.

! **E NON E' ACCADEMICO: la libctest l'ha preso.** Il primo `chmod` rendeva
ENOSYS su FAT e la prova «chmod accetta un file che c'e'» e' passata da 294 a
293. Dietro c'e' una ragione concreta: **bfd chiude ogni eseguibile che produce
con umask/chmod**, ed e' il motivo per cui quella funzione esisteva inerte da
un anno. Un chmod che fallisce sul floppy vorrebbe dire binutils che non
collega piu' niente dentro EX-OS.

## Due file per gli utenti, come `/etc/passwd` e `/etc/shadow`

    /boot/utenti   nome:uid:gid          0644, lo legge chiunque
    /boot/ombra    nome:sale:impronta    0600, solo root

! **CON UN FILE SOLO BISOGNA SCEGLIERE FRA DUE MALI**, e si e' visto subito:
messo a 0600 per proteggere le impronte, `id` rispondeva `uid=1000(uid1000)` a
un utente che si chiamava mario — nessuno poteva piu' tradurre un uid in un
nome. I nomi sono pubblici perche' servono a chiunque; le impronte sono di root
perche' non servono a nessun altro.

! **E SI SCRIVE PRIMA L'OMBRA, POI I NOMI.** Se fallisce la seconda scrittura
resta un'impronta senza nome, che non fa entrare nessuno. All'incontrario
resterebbe **un nome senza impronta**: un conto che esiste e non ha password.

## La casa si crea da root e si consegna

! **E' L'UNICO ORDINE CHE FUNZIONA.** `/home` appartiene a root con 0755: un
utente normale non ci puo' creare dentro, quindi non puo' farsi la propria
directory. E se la crea root senza consegnarla, il padrone ha **una casa in cui
non puo' scrivere**. Creare-e-consegnare e' quello che fa `adduser` su ogni
Unix, e qui si puo' perche' login in quell'istante e' ancora root.

`/home` e' il contenitore, la casa e' `/home/<utente>`. root fa eccezione con
`/root`, come su ogni Unix: `/home` puo' essere un volume a parte montato dopo,
e l'amministratore deve poter entrare anche quando non c'e'.

## Quello che manca, dichiarato

! **IL CONTROLLO SULL'ESECUZIONE NON C'E' ANCORA.** `install` adesso mette
0755 ai programmi — quindi i bit sono giusti — ma la VFS non li guarda. E'
deliberato e va in quest'ordine: accendere il controllo su un disco pieno di
file senza x sarebbe un sistema installato in cui **non parte niente**. Prima i
bit, poi il controllo.

Manca anche un `chmod` da riga di comando, e un `chown` (oggi si fanno solo da
programma).

# Utenti e permessi, passo 1: l'identita' esiste (17 agosto 2026)

Avviando **dal disco** (radice ext2):

      Sistema nuovo: non c'e' ancora nessun utente.
      Creane uno adesso — da qui in poi servira' per entrare.
      nome utente: root
      password: *******
      Utente 'root' creato.

      EX-OS — accesso
      utente: root
      password: *******
    ex-os:/> id
    uid=0(root) gid=0
    ex-os:/> cat /boot/utenti
    root:0:0:27262b2846ff031f:e372ebb9de25c2cab34a...

Avviando **da floppy** (radice FAT12): il prompt arriva subito, `id` dice
`uid=0(root)`, nessuna password.

Fatti i primi due dei cinque passi, piu' la META' SICURA del terzo.
**I controlli sui file non ci sono ancora**: c'e' il soggetto a cui
attribuirli, e adesso anche l'oggetto.

## `uid` e `gid` nel PCB, ereditati

! **SI EREDITANO SEMPRE, E NON C'E' UN FLAG PER CAMBIARLI.** Un processo non
puo' decidere di nascere con un utente diverso dal proprio: se potesse, i
permessi si aggirerebbero con uno spawn. Chi cambia utente e' `setuid()`.

! **`setuid()` LA PUO' CHIAMARE SOLO root, E IN UNA DIREZIONE SOLA: SI SCENDE.**
Non c'e' il bit setuid sui file, quindi non esiste nemmeno il caso legittimo in
cui servirebbe risalire — e senza quel controllo qualunque processo diventerebbe
root chiamandola, rendendo i permessi una decorazione.

! **ED E' COSI' CHE `login` CAMBIA UTENTE SENZA setuid SUI FILE.** init resta
root, avvia login che resta root, login autentica e **scende** con `setuid()`
prima di lanciare la shell. Da quel momento non puo' piu' tornare su: il
privilegio si spende, non si presta.

`getuid()` e compagne rendevano **zero fisso**. Era onesto quando non c'erano
utenti e sarebbe una bugia adesso: un programma che chiede chi e' e si sente
rispondere «root» si comporta da root.

## Il login e' obbligatorio solo su una radice ext2

    const char *avviare = (cfg->login_path[0] && vfs_radice_ext2())
                          ? cfg->login_path : cfg->shell_path;

! **E' LA REGOLA DELL'UTENTE, SCRITTA IN CODICE.** Avviando da floppy o da CD
la radice e' FAT o ISO 9660: nessun proprietario, nessun permesso da far
rispettare, e chiedere una password su un sistema dove chiunque puo' riscrivere
qualunque file sarebbe **una serratura su una porta senza muri**. Li' si entra
da root senza password — che e' anche l'unico modo di poter installare.

`login = /bin/login` in `kernel.cfg` si puo' adesso lasciare acceso sempre.

## `/boot/utenti` porta l'uid

    nome:uid:gid:sale:impronta

Prima era `nome:sale:impronta` e bastava: login autenticava e la shell girava
comunque da root. Adesso login **scende**, e per scendere deve sapere a chi.
Il primo utente creato e' root (uid 0); gli altri partono da **1000**, e i
numeri fra 1 e 999 restano liberi per i conti di servizio.

## `/bin/id` e `/bin/whoami`

! **ESISTONO PERCHE' SENZA NON C'ERA MODO DI GUARDARE L'uid.** Il kernel lo
teneva, lo ereditava, login ci scendeva: tutto corretto e **completamente
invisibile**. Un meccanismo che non si puo' osservare e' un meccanismo di cui
si crede di sapere lo stato — ed e' quello che rende difficili da trovare i
difetti dei permessi.

! **IL NUMERO VIENE DAL KERNEL, IL NOME DALL'AMBIENTE**, e `id` lo dice quando
i due non coincidono: `getuid()` non puo' mentire perche' e' una syscall,
`$USER` lo cambia chiunque con `set`.

## `install` prepara il posto, ma non chiede la password

Su ext2 crea `/home` e `/root` e **dice** che al primo avvio l'accesso sara'
obbligatorio.

! **L'UTENTE LO CREA `login` AL PRIMO AVVIO, NON L'INSTALLATORE.** Per chiedere
una password bisogna poterla leggere SENZA MOSTRARLA, cioe' mettere la tastiera
in modo raw — e install gira mentre la shell possiede quella console. Chiederla
li' vorrebbe dire contendersi la tastiera con chi ci ha lanciati. login parte da
solo, ha la console tutta per se', e quel codice ce l'ha gia'.

## Il proprietario si LEGGE, e non si fa ancora rispettare

`ext2_stat()` legge `i_mode`, `i_uid` e `i_gid` agli offset che ext2 usa dal
1993, e la VFS li porta in `VfsStat`. Su FAT valgono **0**, e non e' un
ripiego: su quei volumi il proprietario non esiste e chiunque puo' fare tutto —
inventarne uno finto vorrebbe dire che un controllo deciderebbe in base a un
dato che non c'e'.

! **LEGGERE E FAR RISPETTARE SONO DUE CAMBIAMENTI, DELIBERATAMENTE SEPARATI.**
Leggere e' additivo e non puo' rompere niente. Far rispettare puo' **chiudere
fuori l'utente dal proprio sistema**, e va fatto con le sue prove.

## Cosa manca — i due passi che mordono

 1. **`vfs_permesso()` e i controlli** su open, exec, unlink e rename. E' il
    punto in cui le regole 6 e 7 diventano vere: i file di sistema non
    accessibili all'utente normale, e nessuno nelle directory degli altri.
    **uid 0 passa dappertutto, e quella riga deve stare in UN posto solo** —
    sparsa nei singoli controlli sarebbe la cosa piu' facile da dimenticare in
    uno di essi.
 2. **Scrivere il proprietario quando si crea un file.** Oggi ext2 non lo
    scrive: i campi resterebbero a zero, cioe' tutto di root, e i controlli
    chiuderebbero fuori chiunque non sia root dal proprio stesso file appena
    creato. **Questo passo va PRIMA del precedente**, o il primo utente che
    salva qualcosa non riesce a rileggerlo.

## E un errore che ho fatto io, che vale la pena non rifare

! **HO RICOSTRUITO `build/` MENTRE `mkhd.sh` CI STAVA COPIANDO DENTRO.** Il
disco e' venuto meta' vecchio e meta' nuovo, e non partiva affatto: nessun
output sulla seriale, MBR perfetto. Sono due regole gia' scritte, violate
insieme — «niente build con QEMU acceso» e «due qemu_drive insieme si
cancellano l'output», perche' condividono `/tmp/exos/serial.txt` senza
`EXOS_ISTANZA`. Costo: una ricostruzione del disco e mezz'ora a cercare un
difetto che non c'era.

# Il toolkit smette di farsi copiare: ex_fuoco, lista, area di testo (17 agosto 2026)

    l'elenco comincia a y=67
    all'apertura:            barra della scelta a  67..82    riga 0
    dopo tre frecce giu':    barra della scelta a 115..130   riga 3   (67 + 3x16)

    edit /a1.txt, battuto «riga uno» + Invio, «riga due» + Invio, Ctrl+S
    ls /a1.txt   ->  19 byte  =  "riga uno\n" + "riga due\n" + la riga vuota

Tre controlli nuovi in `exwin.so`, e **tre copie disegnate a mano che
spariscono**: l'elenco del file manager, quello del dialogo Apri/Salva, e
l'area dell'editor.

## `ex_fuoco()` — la piu' piccola, e sbloccava le altre due

`fuoco_metti()` esisteva gia' ed era `static`. Bastava esportarla.

! **SENZA, IL FUOCO ANDAVA AL PRIMO CONTROLLO CREATO** che lo accettasse, e per
spostarlo bisognava creare i controlli **in un ordine che non e' quello in cui
si leggono**. Nel dialogo di ExDlg la casella del nome si creava prima del
pulsante «Su» solo per questo, con un commento che prometteva di rimettere le
righe a posto il giorno che `ex_fuoco()` fosse esistita. E' quel giorno: le
righe sono tornate in ordine.

## `"lista"` — l'elenco che scorre

Frecce, PgSu/PgGiu, Home/End muovono la scelta e la vista la insegue. Invio e
il clic arrivano come `EXM_COMANDO` con l'id della lista — **lo stesso
messaggio di un pulsante premuto**, perche' e' la stessa decisione presa in due
modi, e chi la riceve non deve imparare un secondo meccanismo.

! **LA LISTA CONSUMA SOLO CIO' CHE SA USARE.** Una lettera passa oltre, ed e'
quello che permette a un'applicazione di avere una scorciatoia mentre la lista
ha il fuoco. Un controllo che mangia tutto e' un controllo che si prende
l'applicazione.

! **IL TESTO SI COPIA DENTRO LA LISTA.** Un vettore passato dal chiamante
vorrebbe dire che lui lo tiene vivo finche' la lista esiste, e nessuno se ne
ricorda: cosi' si puo' passare un buffer sullo stack e dimenticarsene.

! **UN CLIC SCEGLIE, NON APRE.** Un clic che entrasse subito in una directory
renderebbe impossibile scegliere senza aprire.

## `"areatesto"` — l'area multiriga

Un'area non e' una lista con dentro delle righe: ha un cursore che si muove in
due direzioni, scorre anche in orizzontale, e i tasti la **cambiano** invece di
limitarsi a sceglierne una riga. Inserimento, Backspace, Canc, Invio che spezza
la riga, clic che posiziona il cursore: tutto nel controllo.

! **`ex_area_aggiungi()` RENDE 0 QUANDO L'AREA E' PIENA**, e chi carica un file
deve guardarlo. Caricare mezzo file e poi salvarlo cancellerebbe il resto senza
averlo mai mostrato — ed e' il modo piu' silenzioso che un editor abbia di
distruggere dei dati.

! **NON C'E' UNA FUNZIONE CHE RENDA TUTTO IL BUFFER.** Darebbe a chi chiama un
puntatore dentro la libreria, cioe' un modo di scriverci sopra senza che il
controllo se ne accorga: e allora il cursore e il numero di righe direbbero una
cosa e il testo un'altra. Si carica e si rilegge una riga per volta.

## Lo schema, e perche' non e' `Oggetto`

Lista e area tengono lo stato in una **tabella a parte**, trovata dall'oggetto
— `lista_di()`, `area_di()` — esattamente come faceva gia' il terminale.
`Oggetto` e' una struttura fissa uguale per tutte le classi: allargarla per la
lista vorrebbe dire farla pagare anche a un separatore.

E le voci sono **un blocco solo**, chiesto una volta alla creazione: `free()`
non restituisce niente, quindi allocare e liberare a ogni cambio di directory
perderebbe memoria per sempre.

## Le prove

    libctest                294 prove superate, 0 fallite
    la lista                le frecce muovono la barra di 3 righe da 16 px,
                            misurate sullo schermo
    l'area                  due righe battute e salvate: 19 byte esatti
    il dialogo Salva-come   riscritto sulla lista, scrive il file col nome dato
    filemgr, edit, term     girano tutti sul toolkit nuovo

# /exwin/bin/term, e il prompt che si leggeva alla lettera (17 agosto 2026)

    term: /bin/sh aperto in una finestra 644x404 (80 colonne per 25 righe)

    la griglia, prima:   [92mex-os[97m:[96m/[97m>
    dopo:                ex-os:/>

La terza voce del menu esiste. E' quasi tutta nel toolkit — il controllo
«terminale» apre le due pipe, avvia il programma e disegna la griglia — e
questo binario e' la finestra che ci sta intorno, di misura **multipla esatta
della cella**: 80x25 celle da 8x16, cioe' 640x400 di area utile. Una misura
qualunque lascerebbe una striscia nera che sembra un difetto di disegno e
invece e' aritmetica.

! **LE SEQUENZE ANSI SI INGOIANO INTERE, NON UN CARATTERE PER VOLTA.** Il
codice diceva gia' «i caratteri di controllo si buttano», ed era vero e
insufficiente: di `ESC [ 9 2 m` l'unico carattere sotto 0x20 e' `ESC`. Il resto
sono lettere e cifre normali, che finivano dritte nella griglia. Sembrava che
la shell mandasse spazzatura, e invece mandava esattamente quello che manda a
qualunque terminale: eravamo noi a non saperlo leggere. Adesso c'e' uno stato —
tre valori — che ingoia la sequenza fino al byte finale.

! **SI INGOIA, NON SI INTERPRETA.** Fingere di avere i colori vorrebbe dire un
attributo per cella e un secondo posto in cui decidere come si disegna il
testo. Buttare la sequenza da' un prompt in bianco e nero, che e' esattamente
quello che questa griglia sa fare.

! **E ADESSO IL TERMINALE SI CHIUDE QUANDO LA SHELL ESCE.** Prima `read` rendeva
0 e il ciclo usciva in silenzio: la finestra restava aperta intorno a una shell
morta, accettava i tasti, li mostrava e non rispondeva — sembrava bloccata
mentre era semplicemente vuota. C'e' un messaggio nuovo, `EXM_TERMFINITO`,
consegnato **una volta sola** alla finestra che contiene il terminale.

Restano dichiarati: niente `Ctrl+C` (un segnale attraverso una pipe non si
manda), niente cronologia ne' frecce (le fa la line discipline, e una pipe non
ne ha), e la finestra non si ridimensiona.

# Utenti, permessi e installazione a componenti — la specifica (17 agosto 2026)

Dettata dall'utente il 17 agosto. **Fatta la parte dell'installatore**; il
resto e' progetto, non codice.

## FATTO: l'installazione a componenti

    uso: install [-a|-m|-t] <punto di montaggio>

    -m   sistema MINIMALE, nessun componente, non chiede
    -t   sistema e TUTTI i componenti, non chiede
    (senza) mostra i componenti trovati e li chiede uno per uno

! **IL SISTEMA MINIMALE E' UN ELENCO CHIUSO; TUTTO IL RESTO E' OPZIONALE.**
`bin`, `boot`, `lib`, `dev`, `drivers` sono cio' senza cui EX-OS non parte.
Qualunque ALTRA directory nella radice del supporto e' un componente:
l'installatore la trova, la mostra e la chiede.

E' il contrario di un elenco scritto dentro l'installatore. Aggiungere un
pacchetto — oggi `/exwin`, domani quello che sara' — vuol dire **metterne la
directory sul supporto, e basta**. Chi prepara un pacchetto non ha i sorgenti
dell'installatore.

! **`installa_exwin()` E' STATA TOLTA.** Sapeva a memoria di `/exwin`: creava
la directory, copiava `bin/` e `lib/`, creava `dev/` anche vuota. Funzionava, e
ogni pacchetto nuovo avrebbe voluto una funzione come quella. Adesso `/exwin`
non e' un caso speciale.

! **`copia_albero()` SEGUE LE SOTTODIRECTORY**, e `copia_dir()` no. Un
livello solo va bene per `/bin` e `/lib`; un componente no — `/exwin` non
contiene file, contiene `bin/ lib/ dev/`. Copiarlo con `copia_dir` avrebbe dato
una directory vuota **e nessun errore**, che e' il caso peggiore.

! **LA SCELTA SI FA PRIMA DI SCRIVERE.** Chiedere «installo anche /exwin?» dopo
aver sostituito il kernel vorrebbe dire che rispondere «annulla» non annulla
piu' niente.

! **E `tools/mkhd.sh` USA `-t`.** Senza il flag l'installatore si fermerebbe su
una domanda a cui nessuno risponde, per poi dire «l'installazione non e'
arrivata in fondo» — un messaggio che non somiglia alla sua causa.

## E un difetto che solo questa prova poteva trovare

! **`wserver.drv` NON CONOSCEVA `-i`, E LA SONDA LO AVVIAVA PER DAVVERO.**

`hwconfig -d` sceglie quali driver installare provandoli uno per uno con `-i`:
si aspetta che ognuno dica cosa fa e ritorni. wserver ignorava il flag e
partiva — **e un server non esce mai**. L'installazione dal CD si fermava li',
dopo aver gia' sostituito kernel e stage2: un disco a meta', e un log che
finiva con «wserver: entro nel ciclo» invece che con un errore.

Non si era mai visto perche' **installando dal floppy quel driver non c'e' nel
catalogo**. E' bastato che l'installazione dal CD diventasse una cosa che si
prova.

Restano senza `-i`, e non hanno dato problemi perche' escono da soli quando non
trovano la periferica: `floppy`, `mouseser`, `tty`, `uhci`, `vgaprova`. Vale la
pena dargliene uno lo stesso, prima che uno di loro diventi un server.

## Le prove

    mkhd.sh                 disco da 256 MB costruito con `install -t`
    avvio dal disco         libctest: 289 prove superate, 0 fallite
                            (289 e non 294: su ext2 cinque prove di FAT non
                            si fanno)
    installazione dal CD    mostra /doc e /exwin, li chiede uno per uno
    scelto solo /exwin      copiato con bin/ dev/ lib/, e /doc NON c'e'
    grafica dal disco       exwin, pm e filemgr girano sul sistema installato

## DA FARE: utenti e permessi, come li ha dettati l'utente

**Le regole, testuali:**

 1. **avviando da floppy o da CD** si e' root senza password: serve a poter
    lanciare l'installazione;
 2. **`install`, su ext2, chiede la creazione degli utenti**;
 3. **avviando dal disco rigido il login parte automaticamente**, e **senza
    autenticazione il sistema resta non accessibile**;
 4. **su ext2 ogni utente ha la sua directory sotto `/home`**; su un altro
    filesystem si crea solo la directory di root;
 5. **ogni utente ha privilegi diversi** per leggere i file ed eseguire i
    programmi;
 6. **i file e le directory di sistema** (`bin`, `boot`, `lib`, ...) non sono
    accessibili all'utente normale, che pero' **puo' eseguire una parte delle
    applicazioni**;
 7. **un utente non accede alle directory degli altri utenti**, a meno che non
    abbia diritti di amministrazione o di root.

**L'ordine di costruzione, e ogni passo dipende dal precedente:**

 1. **`uid` e `gid` nel PCB**, ereditati allo spawn. Senza, non c'e' un
    soggetto a cui attribuire un permesso;
 2. **proprietario e permessi per file in ext2**. Ci sono gia' nel formato —
    l'inode ha `i_uid`, `i_gid`, `i_mode`: oggi si scrivono e non si leggono;
 3. **i controlli nella VFS**, su open, exec, unlink e rename. E' il punto in
    cui la regola 6 e la 7 diventano vere;
 4. **`install`** che crea root con userid e password e scrive `/boot/utenti`,
    piu' `/home/<utente>`;
 5. **`login`** obbligatorio, avviato da init quando la radice e' ext2.

! **E QUESTO RENDE IL VARCO `*.drv` UNA BARRIERA VERA.** Oggi e' una
definizione: senza proprietari dei file, un programma puo' copiarsi in `x.drv`
e ripartire da li' con i privilegi di un driver. Con i proprietari, `/dev`
appartiene a root e la copia non si puo' fare. E' lo stesso lavoro, e chiude un
difetto dichiarato da giorni.

! **UNA COSA DA DECIDERE PRIMA DI SCRIVERE IL CODICE**: EX-OS non ha ancora un
modo di dire «questo programma gira con i privilegi del proprietario» (il bit
setuid). Senza, `login` non puo' cambiare utente — dovrebbe essere init a
lanciarlo e init a fare la exec come l'utente scelto. E' la strada piu'
semplice e non richiede setuid: **init resta root, login chiede le credenziali,
init lancia la shell con l'uid giusto.**

# La libc condivisa: /lib/libc.so (17 agosto 2026)

    /bin       850.132 -> 608.468 byte     risparmiati 241.664
    ISO         4728 KB -> 4252 KB
    libctest     64.861 -> 33.312 di testo, e passa 294 prove su 294
    uname        11.277 ->  6.108 caricati

    /lib/libc.so   78.424 sul disco, 56.266 di testo — una volta sola

322 funzioni condivise, 39 programmi collegati a lei, piu' `exwin.so` ed
`exdlg.so`. `login` e `install` restano STATICI, ed e' una decisione: sono i
due programmi con cui si entra e con cui si ripara. Se `libc.so` mancasse o
fosse rotta, un login collegato a lei renderebbe il sistema inaccessibile e non
ci sarebbe modo di rimediare dall'interno.

## Le due cose che la rendono diversa da exwin.so

! **LE FUNZIONI SI PASSANO SENZA CONOSCERNE LA FIRMA.** Sono 322: scrivere a
mano 322 ponti in C vorrebbe dire copiare 322 firme, e ognuna sbagliabile in
silenzio. Un ponte in assembly e' un `jmp` indiretto, che non tocca ne' gli
argomenti ne' il valore di ritorno — li lascia dove sono:

        printf:  ff 25 c4 25 00 08     jmp *0x80025c4

E' lo stesso mestiere di una PLT. Li genera `tools/genlibc.py` leggendo i
simboli con `nm` dall'oggetto vero, quindi l'elenco non puo' divergere da cio'
che la libc contiene davvero.

! **LE CINQUE VARIABILI GLOBALI NON SI POSSONO AVVOLGERE.** `errno`, `stdin`,
`stdout`, `stderr`, `environ`: un programma che scrive `errno = 0` scriverebbe
nella PROPRIA copia mentre la libc legge la sua — due variabili con lo stesso
nome, e nessun errore da nessuna parte. Si esporta l'INDIRIZZO e l'header lo
trasforma in una lettura:

        #define errno (*__errno_dove())

Il sorgente di chi le usa non cambia. Valgono anche collegando staticamente,
apposta: un comportamento solo invece di due che divergono.

## L'avvio, che non si puo' condividere

`_libc_start` e `_libc_distruttori` toccano `main`, `__init_array_*` e
`__fini_array_*`, che appartengono al BINARIO in cui si trovano. Dentro la
libreria `main` non esisterebbe nemmeno (il collegamento fallirebbe), e i
vettori sarebbero quelli della libreria — vuoti: **i costruttori globali del
programma non girerebbero mai, e nessuno lo direbbe.**

Stanno in `lib/libc_avvio.c`, e si usano in tre modi con una definizione sola:

 - `lib/libc.c` lo **include** in fondo, quindi i programmi statici non
   cambiano di una virgola;
 - `libc.so` lo esclude con `-DEXOS_LIBC_SO`;
 - lo stub della libc condivisa lo compila per conto suo.

Includere un `.c` e' insolito: la ragione e' che la libc la compilano venti
regole del Makefile, e un oggetto separato sarebbe stato venti modifiche e
almeno una dimenticanza.

`exit()` sta nella libreria e non puo' trovare il `__fini_array` del programma:
il programma glielo **registra** con `__libc_distruttori_registra()`.

## Il gancio delle librerie

Un programma ha `_start`, e li' c'e' un posto naturale in cui agganciare cio'
che gli serve. **Una libreria non parte**: le sue funzioni vengono chiamate e
basta. Se `exwin.so` usa la libc condivisa, i suoi ponti li deve riempire
qualcuno.

`exlib_apri()` cerca il nome facoltativo `__lib_avvio` e lo chiama: e' l'unico
momento in cui si sa che la libreria e' appena stata mappata. Una libreria che
non dipende da nessuno — la libc — non lo esporta.

## Difetti trovati

! **DUE VOLTE «UN'USCITA CHE NON SA DI ESSERE SCADUTA», e fanno cinque e sei.**

 1. `LIBC_PONTI_OBJ` era definita SEIcento righe sotto la regola di
    `/bin/libctest`. Make espande i prerequisiti mentre LEGGE il file, quindi
    li' valeva stringa vuota: la ricetta era giusta e non e' mai stata
    eseguita. **Le 294 prove che credevo di aver fatto sulla libc condivisa le
    avevo fatte su quella statica** — me ne sono accorto solo guardando se
    `printf` fosse un ponte o la funzione vera;
 2. le regole di `ls` e `mem` avevano `$(LS_START)` invece di `$(LIBC_START)`,
    quindi la mia sostituzione dei prerequisiti non le ha prese: ricetta nuova,
    prerequisiti vecchi, binario vecchio.

Le altre quattro: dipendenze finte del bersaglio floppy, `uhci.drv` mancante
fra quelle dell'ISO, SVGA non prerequisito di Stage 2, immagini non dipendenti
dal Makefile. **Un prerequisito scritto con una variabile vuota non e' un
prerequisito debole: non esiste, e nessuno lo dice.**

! **C'ERA GIA' UNA libc.so, ED ERA MORTA.** Una regola vecchia la costruiva con
`-shared -fPIC`, cioe' un `ET_DYN`. Il caricatore ELF del kernel accetta solo
`ET_EXEC`: quella libreria non e' mai stata caricata da nessuno — si costruiva,
finiva sul floppy e occupava spazio. Tolta.

! **`__udivdi3` NELLA LIBC.** `nanosleep` faceva una divisione a 64 bit, che
chiama un aiuto di libgcc. In un programma statico `--gc-sections` la buttava
via insieme a nanosleep; nella libreria condivisa — che si collega SENZA
`--gc-sections`, perche' non sa chi la usera' domani — restava, e la libreria
non collega libgcc. La divisione e' diventata a 32 bit, e puo' esserlo perche'
`tv_nsec` e' gia' validato sotto il miliardo due righe sopra.

## Il costo, dichiarato

Ogni programma si porta ~5 KB fissi: i ponti che usa, la tabella dei puntatori
(1288 byte) e i nomi da risolvere. Per un programma piccolo il guadagno e'
modesto — `uname` da 11.277 a 6.108 — per uno che usa molta libc e' grande.

I nomi sono un BLOCCO di stringhe una dopo l'altra, non un vettore di
puntatori: quel vettore sarebbe stato 1288 byte in ogni programma per dire una
cosa che le stringhe dicono gia' da se'.

Resta da valutare: risolvere per **hash a 4 byte** invece che per nome
risparmierebbe altri ~3 KB per programma, con il generatore che verifica a
costruzione che non ci siano collisioni.

# Le librerie condivise: exwin.so ed exdlg.so (17 agosto 2026)

    ex-os:/> exwin
    ex-os:/> /cdrom/exwin/bin/edit &
    edit: file nuovo, senza nome
    (battuto «testo scritto», poi Ctrl+S -> si apre il dialogo, «dlg.txt»)

    ex-os:/> cat /dlg.txt
    testo scritto
    ex-os:/> ls /dlg.txt
    dlg.txt      15        <- 13 caratteri + "\n" + "\n"

    edit      37.940 -> 24.304        exwin.so   26.836   una volta sola
    filemgr   35.724 -> 22.600        exdlg.so   21.296   una volta sola
    pm        36.588 -> 22.752

## La strada scelta, e perche' non il collegamento dinamico vero

Un `.so` con `ld.so`, codice PIC, GOT e PLT e' la strada standard, ed e' mesi
di lavoro: caricatore ELF da riscrivere, un linker dinamico da scrivere, la
libc ricompilata PIC. E ogni difetto li' dentro sarebbe un difetto di TUTTE le
applicazioni insieme.

Qui la libreria e' un **ELF normalissimo, ET_EXEC e non PIC, collegato a un
indirizzo riservato**. Sta sempre li', quindi non c'e' niente da rilocare e non
serve nessuna GOT. Cio' che serviva davvero — aggiornare la libreria senza
ricompilare le applicazioni — si ottiene con la **risoluzione per NOME**.

! **L'ORDINE DELLA TABELLA NON E' PARTE DELL'ABI.** Con una tabella posizionale
riordinare le voci romperebbe ogni applicazione gia' compilata, e nessun errore
lo direbbe: si chiamerebbe semplicemente la funzione sbagliata. Coi nomi si
puo' aggiungere, riordinare e riscrivere il corpo di qualunque funzione. Solo
TOGLIERE un nome rompe — ed e' esattamente il patto di una DLL.

## La mappa degli indirizzi

    0x04000000   exwin.so    il toolkit
    0x04400000   exdlg.so    i dialoghi
    0x04800000   libere
    0x08000000   i programmi

Lo spazio era gia' li', vuoto: i 64 MB fra `USER_SPACE_BASE` e l'indirizzo dei
programmi. Non tocca ne' lo heap ne' la riserva dello stack. Quattro megabyte a
libreria, cioe' una page table esatta.

! **LE FETTE SI ASSEGNANO IN UN POSTO SOLO**, in `lib/exwin/exwin.ld`. Due
librerie alla stessa base si sovrascriverebbero dentro il processo che le usa
tutt'e due — e exwin+exdlg sono proprio la coppia che si usa insieme.

## Cosa si condivide e cosa no

    .text/.rodata   sola lettura  -> LA STESSA PAGINA FISICA in ogni processo,
                                     con pmm_ref_inc(), come fa shm.c
    .data/.bss      scrivibili    -> una copia FRESCA per processo, dall'originale

Condividere anche i dati scrivibili vorrebbe dire che due applicazioni si
scrivono addosso le variabili: la tabella delle finestre di ExWin sta in `.bss`
e sarebbe la stessa per tutti.

! **E IL LINKER SCRIPT ALLINEA `.data` A PAGINA.** Se la fine di `.rodata` e
l'inizio di `.data` stessero nella stessa pagina, quella pagina sarebbe
scrivibile — cioe' copiata per ogni processo — e mezza `.rodata` smetterebbe di
essere condivisa **senza che nessuno lo dica**.

## I pezzi

| dove | cosa |
|---|---|
| `kernel/loader/lib.c` | la cache, il caricamento, l'aggancio |
| `SYS_LIB_APRI` (248) | rende l'indirizzo della tabella |
| `lib/include/exlib.h` | il formato della tabella, `EXLIB_TESTA()` |
| `lib/exlib/exlib.c` | il risolutore, ~40 righe, senza libc |
| `lib/exwin/exwin_esporta.c` | i 17 nomi di ExWin |
| `lib/exwin/exwin_stub.c` | i ponti che entrano nell'applicazione |
| `lib/exdlg/` | la seconda libreria: `ex_dlg_apri/salva/avviso` |

! **GLI HEADER NON SONO CAMBIATI DI UNA RIGA** — `exwin.h`, `exwin.hpp`,
`exwin.bi` — e nemmeno il sorgente delle applicazioni. Cambia solo cosa si
collega, e lo decide il Makefile.

! **LA RISOLUZIONE E' PIGRA.** Non c'e' niente da chiamare prima di `main()`:
la prima chiamata a una funzione qualunque risolve tutti i nomi in un colpo. Un
`ex_avvia()` da mettere in cima a ogni main sarebbe una riga che, dimenticata,
da' un salto a zero invece di un messaggio.

! **exdlg USA exwin COME UN'APPLICAZIONE**, collegandosi allo stub. Portarsene
dentro una copia darebbe due tabelle di finestre nello stesso processo, e una
finestra creata dall'una sarebbe invisibile all'altra.

## Due difetti trovati costruendola

**Il link scriveva in `/exwin.so`.** `BUILD_EXWIN_LIB` era definita PIU' IN
BASSO di dove la usavo, e con `:=` make espande subito: la variabile valeva
stringa vuota e il percorso diventava la radice del disco. Non un errore di
sintassi — un permesso negato come unico indizio.

**Il controllo dipendeva dalla lingua.** La verifica che la tabella stia
davvero a 0x04000000 leggeva `readelf -h | grep 'Entry point'`, e su un sistema
italiano readelf scrive «Indirizzo punto d'ingresso»: il confronto avveniva con
la stringa vuota e falliva sempre. Adesso `LC_ALL=C`. Un controllo che dipende
dalla lingua non e' un controllo.

**Il dialogo sembrava sordo.** ExWin da' il fuoco al PRIMO controllo creato che
lo accetta, e non ha un modo pubblico di spostarlo: creando prima il pulsante
«Su», tutto quello che si batteva finiva in un pulsante che i tasti non li usa.
La casella si crea per prima — e serve un `ex_fuoco()` nel toolkit.

## Quello che manca

 - **la libc condivisa**: e' il pezzo grosso rimasto (62 KB di testo contro i
   13 di exwin). Il meccanismo adesso e' provato su due librerie, quindi la
   strada e' aperta — ma un difetto li' dentro sarebbe un difetto di OGNI
   programma di EX-OS, shell compresa;
 - **un dialogo con «si'/no»**: ExDlg ne ha uno con un pulsante solo, quindi
   «vuoi perdere le modifiche?» si chiede facendo premere due volte lo stesso
   pulsante;
 - **finestre modali vere**: il server non sa cosa siano, quindi il dialogo sta
   sopra e prende il fuoco ma non impedisce i clic sotto;
 - **`ex_fuoco()`** nel toolkit, e **una lista a scorrimento**: e' la TERZA
   volta che un elenco si disegna a mano.

# L'editor, e tre difetti vecchi che nessuno vedeva (17 agosto 2026)

    edit: /prova.txt non c'e': file nuovo
    (battuto «ciao dall editor» + Invio, poi Ctrl+S)
    ex-os:/> cat /prova.txt
    ciao dall editor

    ex-os:/> ls /prova.txt
    prova.txt    18        <- 16 caratteri + "\n" + "\n" della riga vuota

`/exwin/bin/edit`: area di testo disegnata a mano, cursore a blocco, frecce,
Home/End, PgSu/PgGiu, Canc, Backspace, clic del mouse per posizionare il
cursore, Ctrl+S per salvare, Ctrl+Q per uscire. E il file manager, premendo
«Apri» su un file, lo lancia — cercandolo in `/exwin/bin` e poi in
`/cdrom/exwin/bin`, come fa il program manager.

! **UN FILE PIU' GRANDE DEI LIMITI SI CARICA IN PARTE E IL SALVATAGGIO SI
BLOCCA.** Salvare quello che si e' letto vorrebbe dire cancellare il resto del
file senza averlo mai mostrato: e' il modo piu' silenzioso che un editor abbia
di distruggere dei dati. Limiti: 512 righe, 200 colonne, come `/bin/gfedit` e
per la stessa ragione (`free()` non restituisce niente).

! **IL TAB SI MOSTRA COME UNO SPAZIO E RESTA UN TAB NEL FILE.** Espanderlo
vorrebbe dire che una colonna sullo schermo non e' piu' un carattere nel testo,
e allora cursore, clic e lunghezza della riga direbbero tre cose diverse.

Manca, dichiarato: «Apri» e «Salva con nome» (vogliono un dialogo modale con
una casella di testo, che il toolkit non ha), l'annullamento, la selezione e
gli appunti.

! **ED E' LA SECONDA APPLICAZIONE CHE SI DISEGNA IL CONTENUTO A MANO.** La
prima e' l'elenco del file manager. Due volte vuol dire che il pezzo mancante
e' nel toolkit, non nelle applicazioni: serve una lista a scorrimento e serve
un'area di testo multiriga.

===============================================================================
## Il fuoco non era il fuoco: la barra si prendeva ogni tasto
===============================================================================

L'editor sembrava sordo — ne' lettere ne' Ctrl+S — e tre fotografie dello
schermo prima, durante e dopo la digitazione erano **identiche**. Non era
l'editor.

`wserver.c` mandava il tasto a `g_ordine[g_n_ordine - 1]`, con il commento «la
finestra in cima, che e' il fuoco». Era vero finche' l'ordine di disegno
dipendeva solo da chi si era portato davanti. Ha smesso di esserlo il 14
agosto, con `WIN_ST_SOPRA`: `in_cima()` rimette in fondo all'ordine — cioe' in
cima allo schermo — tutte le finestre «sopra», qualunque cosa sia appena
salita. E la barra delle applicazioni e' «sopra».

! **QUINDI DA QUEL GIORNO NESSUNA FINESTRA POTEVA RICEVERE UN TASTO** finche'
il program manager era acceso. Il difetto non era nell'editor ne' in
`WIN_ST_SOPRA`: era in `in_cima()`, che decideva DUE cose mentre il suo nome ne
prometteva una.

Adesso il fuoco e' `g_fuoco`, una variabile sua: chi sale lo prende, le
finestre «sopra» e lo sfondo restano dove devono stare a schermo senza
portarselo via, e chi muore lo restituisce con `fuoco_ricalcola()`.

! **ERA LA QUARTA VOLTA CON QUESTA FORMA:**

    la tastiera del server   sembrava il modo raw    era il tty del kernel
    il clic senza fuoco      sembravano gli eventi   era aritmetica
    il terminale muto        sembravano le pipe      era la shell
    l'editor sordo           sembrava l'editor       era l'ordine di disegno

===============================================================================
## La shell aveva perso redirezioni e ambiente da tre giorni
===============================================================================

Scoperto per caso, da un avviso assurdo nel log a ogni comando battuto:

    [WARN]  SYSCALL spawn: console 2723776 non esiste, eredito la 0

`2723776` e' `0x298000`: spazzatura. Tirando quel filo:

    ex-os:/> hello > /red.txt
    Ciao da /bin/hello!            <- a video, NON nel file
    ex-os:/> ls /red.txt
    red.txt      0                 <- creato dalla shell, e vuoto

Di `SpawnExtra` c'erano **quattro copie**: `kernel/include/syscall.h`,
`lib/include/libc.h`, `lib/libc.c` e `bin/sh/shell.c`. Il 14 agosto ne sono
state aggiornate tre — aggiunti `flag` e `console`, magia da `SPNY` a `SPNZ` —
e la quarta e' rimasta indietro.

! **UNA MAGIA PROTEGGE DAL DANNO, NON DALLO SFASAMENTO.** Ha fatto esattamente
il suo mestiere: il kernel non ha riconosciuto il blocco e l'ha IGNORATO invece
di leggerlo storto. Ma «ignorato» vuol dire che per tre giorni la shell non ha
avuto ne' redirezioni ne' ambiente, **senza un messaggio**. Un silenzio non si
nota.

La correzione non e' aggiornare la quarta copia: e' **non averne quattro**.
`lib/include/spawn_abi.h` e' la definizione unica, e usa solo `unsigned int`
apposta per poter essere inclusa anche da `shell.c`, che non usa la libc e si
dichiara i tipi da se'. In fondo c'e' un'asserzione sulla misura:

    typedef char spawn_abi_misura_invariata[(sizeof(SpawnExtra) == 596) ? 1 : -1];

Un campo aggiunto senza cambiare la magia adesso ferma la COMPILAZIONE, invece
di lasciare in giro binari che non si capiscono fra loro.

E nel kernel: `kex` veniva letto anche quando il blocco era stato rifiutato,
cioe' spazzatura dello stack. Era quello a stampare l'avviso — ed e' l'avviso
che ha fatto trovare tutto il resto.

===============================================================================
## Altri due, piu' piccoli
===============================================================================

**Il consiglio sbagliato.** `winprova` e `pm`, quando il server non risponde,
dicevano `Avvialo: /cdrom/dev/wserver.drv &`. Due cose sbagliate insieme: quel
percorso non esiste avviando DAL CD (li' la radice **e'** il CD), e il driver
avviato a mano nasce sulla console della shell, dove le contende la tastiera.
Chi seguiva il consiglio non arrivava da nessuna parte e dava la colpa al
server. Ora dicono `exwin`.

**`cat` in una pipe.** `hello | cat` rispondeva «uso: cat [file]», e sembrava
un difetto della pipe. La pipe era giusta — la shell mette un builtin in fondo
a una pipeline dentro un `/bin/sh -c` figlio, che il descrittore 0 ce l'ha
buono — era `cat` a non guardarlo mai. Adesso senza argomenti legge stdin, **ma
solo se stdin non e' la console**: battuto al prompt si metterebbe a leggere la
tastiera per sempre, e senza Ctrl+C non se ne uscirebbe.

===============================================================================
## Le librerie condivise: la decisione (non ancora costruita)
===============================================================================

Un'applicazione grafica oggi e' 38 KB, e solo 6 KB sono sue:

| pezzo | testo |
|---|---|
| `edit.c` | 6.111 |
| `exwin.c` | 12.998 |
| `font8x16` | 4.096 |
| **`libc.c`** | **62.444** |

**La strada scelta: risoluzione per NOME a caricamento, libreria a base fissa.**
E' il comportamento delle DLL — si aggiorna la libreria, le applicazioni non si
toccano — senza dover scrivere un `ld.so`, il codice PIC e le GOT/PLT.

 - lo spazio c'e' gia' ed e' vuoto: **0x04000000–0x08000000**, 64 MB fra
   `USER_SPACE_BASE` e l'indirizzo dove si caricano i programmi. Niente
   collisioni con heap ne' stack;
 - la libreria e' un ELF collegato a quella base, il cui `e_entry` punta a una
   tabella `{nome, indirizzo}` invece che a del codice;
 - `.text`/`.rodata` condivise fra i processi in sola lettura (`pmm_ref_inc` +
   `paging_map_page`, lo stesso meccanismo di `kernel/mm/shm.c`), `.data`/`.bss`
   private;
 - l'applicazione si lega a uno stub che risolve i nomi all'avvio. Aggiungere
   funzioni, riordinarle, riscriverne il corpo: le applicazioni continuano a
   funzionare. Solo TOGLIERE un nome le rompe, come una DLL;
 - **gli header non cambiano di una riga**: `exwin.h`, `exwin.hpp`, `exwin.bi`;
 - la libreria si porta dentro la propria libc statica, come una DLL con il CRT
   statico. Prima exwin, la libc dopo — deciso il 17 agosto: se il meccanismo
   ha un difetto, ne risentono tre programmi e non tutto il sistema.

Da fare, nell'ordine: il caricatore nel kernel + la syscall, il risolutore in
spazio utente, lo stub, la divisione `exwin` / `exdlg` (il dialogo Apri/Salva,
che serve anche al file manager), il Makefile.

===============================================================================
## Utenti, login e install: le regole, come le ha dette l'utente
===============================================================================

 - **avviando da floppy o da CD** l'utente ha privilegi di root, senza
   password: serve a poter lanciare l'installazione;
 - **`install` chiede la creazione di un utente root** con userid e password;
 - **il login e' obbligatorio sempre**, tranne che all'avvio da floppy o CD;
 - **`install` lo imposta quando trova ext2**: e' ext2 a rendere possibili
   proprietari e permessi, quindi e' ext2 a far partire il login in automatico;
 - **su ext2, creando l'utente si creano anche `/home` e `/home/<utente>`**;
 - **su un altro filesystem si crea solo la directory di root.** FAT12/16/32
   restano per compatibilita', non sono il sistema installato.

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

    ~~SUPERATO~~: sotto il terminale non c'e' piu' una pipe ma un **pty**, e il
    Ctrl+C ci arriva — `exwin.c` lo lascia passare apposta come byte 3, che e'
    l'eccezione senza la quale tutto il lavoro sul pty non sarebbe servito.

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


## 17 agosto 2026 — TSC calibrato, PSE a meta'

TSC: fatto e verificato. `kernel/arch/x86/tsc.c`, calibrato sul canale 2 del PIT
letto a polling — NON su g_ticks, che durante l'avvio vale zero perche' l'IRQ0
resta mascherato fino a sched_start(). Misura: 1897,091 MHz contro 1895,845
dichiarati dall'host, cioe' 0,066% di errore.

PSE: FERMO A META', e lo stato e' sicuro. Fatto: PG_HUGE, spezza_4mb(), e le
tre guardie nei punti che camminano sulle PDE (map_page, unmap_page,
get_physical). NON fatto: alzare CR4.PSE e creare le PDE da 4 MB in
paging_init. Finche' nessuno crea una PDE con PG_HUGE le guardie sono inerti e
il kernel si comporta esattamente come prima — si puo' restare qui senza rischio.

Da riprendere: in paging_init, se cpu_capacita()->pse, alzare CR4.PSE PRIMA di
CR0.PG e riempire le PDE 0..N con blocchi da 4 MB al posto di
kernel_page_table_low e di paging_map_range_identity. Cappare a USER_SPACE_BASE.

! SOSPETTO DA VERIFICARE, trovato leggendo: paging_init mappa per identita'
tutta la RAM fino a `total_ram` senza cappare a USER_SPACE_BASE (64 MB). Con
piu' di 64 MB di RAM quella mappatura si sovrappone allo spazio utente. Non
verificato: le prove girano a 32 MB.

---

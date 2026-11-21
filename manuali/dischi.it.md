# EX-OS — Dischi, filesystem, CD e DVD

[🇬🇧 English](dischi.md) · **🇮🇹 Italiano**

[← README](../README.it.md) · [Tutti i manuali](../README.it.md#i-manuali)

**Indice**

- [Inizializzare un disco rigido: /bin/fdisk e /bin/mkfs](#inizializzare-un-disco-rigido-binfdisk-e-binmkfs)
- [CD e DVD — driver ATAPI e ISO 9660](#cd-e-dvd--driver-atapi-e-iso-9660)
- [Nomi lunghi su FAT (VFAT), in lettura](#nomi-lunghi-su-fat-vfat-in-lettura)
- [`mkfs` sceglie il filesystem dalla dimensione](#mkfs-sceglie-il-filesystem-dalla-dimensione)

---

## Inizializzare un disco rigido: /bin/fdisk e /bin/mkfs

Il ciclo completo, da disco vergine a sistema avviabile:

```
disk                        cosa vede il sistema
fdisk hd0                   crea le partizioni, marca attiva la prima
mkfs -t ext2 -L exos hd0p1  ci scrive dentro un filesystem
mount hd0p1 /disk           montalo
install /disk               rendilo avviabile
```

Poi si toglie il floppy e si riavvia. Funziona sia su **FAT16/FAT32** sia su
**ext2**.

### La mappa dei settori, e perché il kernel ne ha una lista

Il settore di avvio non sa leggere alcun filesystem: in 512 byte, tolti BPB e
firma, non ci sta. Riceve LBA e lunghezza di Stage 2 e del kernel, e legge
settori. È `install` — che gira *dentro* EX-OS, dove i driver ci sono già — a
comporre quella mappa.

! Il prezzo è lo stesso patto di LILO: **ricopiare kernel o Stage 2 obbliga a
rilanciare `install`.**

### `install` verifica prima di sostituire (dal 0.161)

Rilanciarlo su un sistema già installato **riscrive ogni file**, kernel
compreso, e rilegge ognuno per confrontarne la dimensione. Nel resoconto `+`
è creato, `~` sostituito, `!` errore.

Fino alla 0.147 i file già presenti venivano saltati: un aggiornamento
copiava solo i file nuovi, lasciava il kernel vecchio e poi riscriveva la
mappa dei settori *per quello* — il disco ripartiva con la versione di
prima e l'installatore diceva «completata». Le directory continuano a non
essere ricreate, e ciò che sul volume non fa parte del sistema resta dov'è:
`install` aggiorna, non azzera.

**E fino alla 0.160 distruggeva prima di sapere se ce l'avrebbe fatta.**
Apriva `/boot/kernel.bin` con `O_TRUNC`, ci scriveva il kernel nuovo, e
*poi* chiedeva la mappa dei settori. Su FAT — dove la mappa ammette **un
solo intervallo** — un kernel cresciuto di qualche KB non entrava più nel
buco lasciato dal vecchio, finiva in due tratti, `bootinstall` rifiutava
giustamente:

```
! installazione dell'avvio fallita: file frammentato (errore -29)
```

…ma a quel punto il sistema che funzionava non c'era più, il settore di
avvio puntava ancora alla mappa vecchia, e **il disco non ripartiva**. Su
ext2 non si vedeva: lì la mappa regge 12 intervalli.

Ora i file nuovi si scrivono **con nomi temporanei mentre i vecchi sono
ancora al loro posto** — quindi finiscono nella coda libera, contigua. Poi
si chiede al kernel se sono mappabili. Solo se la risposta è sì si cancella
e si rinomina:

```
Avvio (scritti a parte e verificati prima di sostituire)
  = verifica: 585 settori in 1 intervallo — si puo' sostituire
  ~ /disk/boot/stage2.bin  (verificato, poi sostituito)
  ~ /disk/boot/kernel.bin  (verificato, poi sostituito)
```

Lo scenario che distruggeva il disco ora **riesce**. Quando invece non si
può fare, si legge `Il sistema gia' installato NON e' stato toccato` e il
disco resta avviabile.

> ! **È servita una primitiva che mancava.** Lo scambio non si poteva
> fare: `rename()` era copia+cancella, quindi riallocava i blocchi e
> mandava a monte la verifica appena fatta. Vedi *La rinomina che non
> sposta i dati* più avanti.

**`kernel.cfg` non si sovrascrive più.** È l'unico file dell'installatore
che appartiene a chi usa il sistema e non al sistema: montaggi automatici,
`verboseboot`, shell, variabili d'ambiente. Un aggiornamento non deve
riportarli indietro in silenzio. Se manca si installa, se c'è si lascia e
lo si dice.

### `install -a` — aggiornare invece di reinstallare

```
install -a /disk
```

Confronta il volume montato con il supporto di avvio, **elenca cosa
cambierebbe**, chiede conferma e solo allora scrive. `+` è un file che sul
disco non c'è, `~` uno che c'è ma è diverso.

```
Confronto di /disk con il supporto di avvio
  +  da creare    ~  da sostituire
  + /disk/bin/ftp
  ~ /disk/bin/ls
  ~ /disk/boot/kernel.bin

35 file da aggiornare (14 nuovi).
Procedo? [si/no]
```

L'elenco viene **prima** della domanda perché «aggiorno 3 file?» e «aggiorno
47 file?» sono due decisioni diverse: un aggiornamento che tocca tutto quando
ci si aspettava un ritocco è il momento in cui ci si accorge di aver montato
il volume sbagliato.

> ! La regola è **«la sorgente è più nuova»**, non «le date sono diverse».
> Copiare un file non ne conserva la data: la copia sul disco nasce con l'ora
> corrente, quindi con la regola ingenua ogni file risulterebbe da aggiornare
> a ogni esecuzione, per sempre, anche subito dopo averlo appena copiato.

La dimensione si confronta **per prima**, ed è il controllo che conta di più:
un file scritto a metà ha la stessa data e una dimensione diversa, e senza
quel confronto il volume resta rotto senza che nessuno lo dica. Se una delle
due date è zero — cioè «questo volume le date non le tiene» — si guarda solo
la dimensione.

Senza `-a`, `install` continua a fare l'installazione completa: riscrive
tutto, che è quello che serve la prima volta e dopo un `mkfs`.

Per il kernel la mappa è una **lista** di intervalli, non uno solo. Su ext2 un
file non è quasi mai contiguo, e non per frammentazione: il blocco di
*puntatori* viene allocato in mezzo ai dati, perché serve prima del tredicesimo
blocco. Un kernel da 147 KB appena copiato sta così:

```
(0-11):74-85, (IND):86, (12-144):87-219
```

Stage 2 invece resta un intervallo solo: sta in ~1 KB, cioè dentro i 12 blocchi
diretti, dove nessun indiretto si è ancora infilato — ed è il pezzo che va
trovato da 512 byte di codice, quindi la sua mappa deve stare in sei byte.

Su FAT la lista ha una voce sola: il formato è lo stesso per i due filesystem,
così Stage 2 non deve sapere da dove sta caricando.

### `chkdsk` — controllo e riparazione di un volume FAT

```
chkdsk <partizione>       controlla e riferisce, non scrive un settore
chkdsk -r <partizione>    controlla e corregge
```

Controlla, in quest'ordine — ogni passo si fida solo di quelli già fatti:
il BPB e la coerenza dei suoi numeri, le copie della FAT, le catene di
cluster percorrendo tutte le directory (condivisi, catene fuori dal volume,
anelli), la dimensione dichiarata di ogni file contro quella della sua
catena, i nomi lunghi, `.` e `..`, e i cluster perduti.

```
tipo FAT32 — 130556 cluster da 8 settori, 2 FAT da 1021 settori
= FAT[0] = 0xffffff8, FAT[1] = 0xfffffff
= le 2 copie sono identiche
! 3 cluster risultano occupati ma nessun file li nomina (12 KB)
```

> ! **Lavora solo su una partizione smontata**, e non è un fastidio da
> aggirare: sopra un volume montato c'è una cache write-back, metà delle
> modifiche recenti sta in RAM. Un controllore che leggesse i settori
> grezzi segnalerebbe incoerenze inventate, e riparando riscriverebbe
> settori che il primo `sync` ricoprirebbe.

> ! **Senza `-r` non scrive un solo settore.** Un controllore che ripara
> di sua iniziativa trasforma un volume danneggiato in un volume
> danneggiato *diversamente*, senza che nessuno abbia visto com'era.

Alcune scelte che non sono ovvie:

- **il tipo si ricava dal numero di cluster**, non dalla stringa
  `"FAT16   "` nel settore di avvio: quella è decorativa e nessuno la
  verifica mai, quindi su un volume malandato è proprio un campo di cui non
  fidarsi;
- **FAT12 si legge con due settori in mano**: una voce occupa dodici bit e
  può cominciare nell'ultimo byte di un settore. In scrittura, se è a
  cavallo, **rinuncia e lo dice** invece di scrivere mezzo valore — sarebbe
  un danno nuovo causato dallo strumento che doveva ripararne uno;
- **su FAT32 i quattro bit alti di una voce non sono nostri**: si
  conservano;
- **la dimensione si confronta con un intervallo**, non con un numero: un
  file di N byte occupa `ceil(N/cluster)` cluster, e pretendere
  l'uguaglianza esatta segnalerebbe come guasto quasi ogni file;
- **i cluster condivisi non si riparano da soli**: quale dei due file abbia
  diritto ai dati non è deducibile, e ripararli d'ufficio significherebbe
  scegliere a caso quale rovinare;
- **i perduti si liberano, non si raccolgono in `FOUND.000`**: sarebbe una
  collezione di frammenti senza nome né struttura, che occupa lo stesso
  spazio che si voleva recuperare.

Sui **nomi lunghi**: una fila di voci finte precede quella 8.3, in ordine
rovesciato, legata a essa da un solo checksum.

> ! Chi rinomina toccando solo la voce 8.3 — e **`rename` di EX-OS fa
> esattamente questo** — lascia il nome lungo a nominare un file che non è
> più quello. Non è un caso di scuola: è un difetto che questo sistema sa
> produrre, ed è il motivo per cui il controllo serve qui.

### I nomi si creano in minuscolo

Il kernel cerca `/bin/sh`, `/boot/kernel.cfg`, `/dev/kbd.drv`. Su FAT il caso
non conta — il driver mette in maiuscolo sia ciò che scrive sia ciò che cerca —
ma su **ext2 `BIN` e `bin` sono due directory diverse**, e un sistema installato
in `BIN` non troverebbe la propria shell. `install` crea tutto in minuscolo,
nomi dei file compresi.

### /bin/fdisk — partizionatore MBR

```
fdisk            elenca i dischi
fdisk hd0        apre la sessione sul disco 0

  p  mostra tabella e spazio libero    a  commuta il flag avviabile
  n  crea una partizione               w  SCRIVE (chiede conferma)
  d  cancella una partizione           q  esce senza scrivere
  t  cambia il tipo
```

**Niente tocca il disco fino a `w`.** Una tabella scritta un pezzo alla volta
passa per stati in cui le partizioni si sovrappongono; se la macchina si
spegne lì in mezzo, resta sbagliata.

È un programma separato da `disk`, che resta in **sola lettura**: è il comando
che si lancia senza pensarci su un disco a cui si tiene, e un programma che a
seconda degli argomenti guarda *oppure* riscrive perde quella garanzia per
tutti gli usi.

Le **politiche** stanno in `fdisk` (allineamento a 1 MiB, primo settore utile
2048, valori predefiniti). Le **regole** stanno nel kernel e non si aggirano:
niente sovrapposizioni, niente partizioni oltre la fine del disco, niente
scrittura su un disco GPT o su una partizione montata.

Non gestisce le partizioni **logiche** e non scrive EBR: le mostra, ma la voce
estesa che le contiene è bloccata — spostarla lascerebbe la loro catena viva
sul disco e irraggiungibile.

`fdisk` non formatta. Una partizione appena creata contiene i byte che c'erano
prima in quei settori: non è vuota, è non inizializzata.

### /bin/mkfs — formattatore FAT16/FAT32/ext2

```
mkfs -t fat32 -L ETICHETTA hd0p1
mkfs -t fat16 hd0p2
mkfs -t ext2  -L dati hd0p3
```

La partizione **non dev'essere montata**: sopra un volume montato c'è una
cache write-back, e scriverci sotto significa che il primo `sync` ci ricopre i
settori vecchi.

Il numero che conta è il **conteggio dei cluster**, e `mkfs` lo mostra accanto
alla soglia. Il tipo di un volume FAT non è scritto da nessuna parte: la
stringa `"FAT16   "` nel settore di avvio è decorativa, e il tipo si deduce dal
numero di cluster dell'area dati (< 4085 → FAT12, < 65525 → FAT16, oltre →
FAT32). Un formattatore che sceglie male i settori per cluster produce un
volume che *dice* FAT16 e *cade* nella banda FAT12, e nessuno se ne accorge
finché i dati non sono già rovinati.

Il settore di avvio vecchio viene azzerato **per primo** e quello nuovo scritto
**per ultimo**: in mezzo il volume non è riconoscibile da nessuno. L'ordine
opposto lascerebbe, su una formattazione interrotta, un settore di avvio che
descrive il filesystem vecchio sopra tabelle FAT già azzerate — un volume che
si monta, sembra funzionare e restituisce file vuoti.

`mkfs` non tocca la tabella delle partizioni, e non per scelta: le syscall
`SYS_BLKREAD`/`SYS_BLKWRITE` accettano solo nomi di **partizione**, e il
settore 0 non appartiene a nessuna partizione. Non esiste una coppia
(nome, LBA) che lo raggiunga. Se il byte di tipo nella tabella contraddice il
filesystem creato, `mkfs` lo segnala e dice come correggerlo con `fdisk`.

### ext2 — creazione e lettura

`mkfs -t ext2` crea un ext2 revisione 1 (`filetype`, `sparse_super`) con
blocchi da 1024. È scritto **dalla specifica**, non portato da e2fsprogs né dal
driver di Linux: quest'ultimo non è un modulo che legge ext2, è un modulo che
*traduce* ext2 nel VFS di Linux, e portarlo significherebbe portare quel VFS.

`kernel/fs/ext2.c` lo legge **e lo scrive**: `mkdir`, `cp`, `delete`, `rmdir`.
La lettura è stata scritta e verificata per prima, da sola — leggere richiede di
capire il formato, scrivere richiede di non romperlo mai, e con un lettore già
provato contro volumi fatti da `mke2fs` ogni errore di scrittura si vede subito
per quello che è.

### /bin/trunc — cambia la dimensione di un file

```
trunc <file> <byte>     suffissi K e M ammessi
```

**Allungare non occupa spazio** su ext2: lo spazio in mezzo diventa un *buco*
che si legge come zeri, e i blocchi si materializzano solo quando ci si scrive.
Un file portato a 2 MB così occupa un blocco solo. Su FAT il kernel alloca sul
serio, perché FAT non sa rappresentare un buco.

**Accorciare è distruttivo e non chiede conferma**, per la stessa ragione per
cui non la chiede `delete` su un nome preciso: chi scrive il nome di un file e
un numero più piccolo della sua dimensione ha già detto cosa vuole. La conferma
serve quando il comando fa più di quanto l'utente abbia nominato.

Sul floppy risponde `-38` (ENOSYS): `fat12.c` non ha un troncamento, ed è il
driver del volume di avvio — la strada collaudata che non si tocca senza una
ragione forte.

! ext2 non ha un giornale e questo driver non ne inventa uno: un'interruzione a
metà di un'operazione può lasciare i contatori dei liberi indietro rispetto alle
bitmap, ed è quello che serve `e2fsck`. Ciò che **non** può succedere è che un
blocco risulti libero mentre è già in uso — le bitmap si scrivono sempre prima
che il blocco venga consegnato.

**Nomi lunghi**: fino a 255 caratteri, il massimo di ext2. Non era solo la
struttura `VfsDirEntry` da allargare — il nome attraversa sei tetti in fila
(driver, VFS, ABI della syscall, lunghezza dei percorsi, argomenti di `spawn`,
riga della tastiera) e alzarne uno solo avrebbe spostato il taglio di un passo.
Un nome troncato non è un nome accorciato: è un nome che non apre niente.

Su FAT i nomi restano 8.3; il campo è largo per il filesystem più generoso.
Una riga di comando arriva fino a 32 768 caratteri, alla console come da
telnet (kernel 0.353): una riga più lunga di un messaggio IPC fra
tastiera e shell viaggia in più messaggi. Vedi
[Cronologia dei comandi e modifica della riga](comandi.it.md#cronologia-dei-comandi-e-modifica-della-riga).

Il driver legge e scrive anche i volumi fatti da `mke2fs`: la dimensione del blocco
(1024/2048/4096), `s_first_data_block` (1 o 0 a seconda) e la dimensione
dell'inode (128 o 256) vengono tutte dal superblocco, mai date per scontate.

Rifiuta invece di provarci quando trova una funzionalità **incompat** che non
conosce — un volume ext4 con extent ha `i_block` che non contiene numeri di
blocco, e leggerlo "come se" restituirebbe dati presi a caso dal disco senza
che nulla lo segnali.

---

### `install -m` / `install -t` — minimale, oppure con i componenti

```
install /disk          mostra i componenti e li chiede uno per uno
install -m /disk       solo il sistema minimale, nessuna domanda
install -t /disk       sistema e tutti i componenti, nessuna domanda
```

L'installatore guarda la radice del supporto di avvio e chiama **sistema
minimale** un elenco chiuso: `bin`, `boot`, `lib`, `dev`, `drivers`. Qualunque
altra directory è un componente, e viene mostrata e chiesta:

```
===============================================================
 COSA INSTALLARE
===============================================================

Il sistema minimale e' sempre incluso: kernel, /bin, /lib,
i driver e l'avvio. Senza quello EX-OS non parte.

Su questo supporto ci sono anche 2 componenti in piu':

    /doc
    /exwin

Installo solo il sistema minimale? [si/no] no

Uno per volta:

  /doc ? [si/no] no
  /exwin ? [si/no] si
```

! **AGGIUNGERE UN PACCHETTO NON RICHIEDE DI TOCCARE L'INSTALLATORE:** basta
metterne la directory sul supporto. Un elenco scritto dentro `install` sarebbe
una seconda verità accanto al contenuto della directory, e le due divergono al
primo pacchetto aggiunto o tolto. **Chi prepara un pacchetto non ha i sorgenti
dell'installatore.**

! **I COMPONENTI SI COPIANO CON TUTTI I LORO LIVELLI.** `/exwin` non contiene
file, contiene `bin/ lib/ dev/`: copiarlo a un livello solo darebbe una
directory vuota **e nessun errore**, cioè un'installazione che sembra riuscita.

! **E LA SCELTA SI FA PRIMA DI SCRIVERE**, non a metà strada: chiedere dopo aver
sostituito il kernel vorrebbe dire che rispondere «annulla» non annulla più
niente.

Gli script che installano senza nessuno davanti — `tools/mkhd.sh` — usano `-t`.
Senza il flag l'installatore si fermerebbe su una domanda a cui nessuno
risponde, e la prova direbbe «l'installazione non è arrivata in fondo»: un
messaggio che non somiglia alla sua causa.

---

## CD e DVD — driver ATAPI e ISO 9660

```
disk                    il lettore compare come cd0
mount cd0 /cdrom        montaggio manuale (sempre in sola lettura)
ls /cdrom
umount /cdrom           prima di espellere il disco
```

E in `/boot/kernel.cfg`, per averlo **montato all'avvio**:

```ini
[mount]
/cdrom = cd0
```

La riga è attiva nella configurazione predefinita, e può restarlo su qualunque
macchina: un lettore vuoto — o assente — non produce un avviso, solo un
"montaggio saltato" nel log. Un CD assente all'accensione è la condizione
normale, non un problema, e segnalarlo come tale metterebbe una riga fra i
*problemi durante l'inizializzazione* a ogni accensione.

### Le tre cose che un lettore non ha in comune con un disco

**Il blocco è da 2048 byte, non da 512.** La traduzione sta in un punto solo,
`kernel/block/blk.c`: il resto del sistema chiede settori da 512 come per
qualunque disco. Le richieste allineate — cioè quasi tutte, perché ISO 9660
lavora a blocchi — vanno dritte al dispositivo senza copie intermedie.

**La capacità appartiene al disco, non al lettore.** Un `cd0` con zero settori
non è un lettore rotto: è un lettore vuoto, o che nessuno ha ancora sondato. La
finestra viene riempita da `blk_supporto()` quando serve, e azzerata quando il
disco esce.

**Gli errori sono a due livelli.** Il bit ERR dice solo "CHECK CONDITION": il
motivo sta nei dati di *sense*, che vanno chiesti con un secondo comando. Senza
leggerli, «non c'è il disco», «il disco è appena stato cambiato» e «il disco è
illeggibile» sono la stessa cosa — e le prime due non sono errori.

Un disco **inserito a sistema avviato** si monta senza riavviare. Un lettore
appena rifornito però risponde ancora "supporto assente" per un comando o due, e
in emulazione il vassoio resta *aperto*: il driver insiste qualche volta e lo
chiude una volta sola, come fa Linux. La conseguenza va detta — montare su un
lettore lasciato aperto e vuoto lo chiude.

### ISO 9660, e perché Joliet vince quando c'è

Un disco masterizzato con nomi lunghi contiene **due alberi completi**: quello
ISO 9660, con i nomi maiuscoli, troncati e con il numero di versione
(`LEGGIMI.TXT;1`), e quello Joliet, con i nomi veri in UCS-2. Non sono due viste
della stessa struttura: sono due catene di directory separate che puntano agli
stessi dati. `kernel/fs/iso9660.c` sceglie Joliet quando c'è, e dice quale ha
scelto; senza, i nomi che si vedono non sono quelli che l'utente ha scritto.

Sui nomi ISO toglie il `;1` e il punto finale — sono formato, non nome — e li
mostra in minuscolo; il confronto è insensibile alle maiuscole, altrimenti ciò
che `ls` mostra non sarebbe digitabile.

**Sola lettura, e non per pigrizia**: ISO 9660 non ha bitmap di spazio libero né
voci riutilizzabili. Non esiste "aggiungere un file", esiste rifare l'immagine.
Ogni scrittura è respinta con `-30` (EROFS) prima di toccare il volume.

**Rock Ridge non è gestito** — l'estensione Unix annidata nei campi di sistema
dei record. Un disco che la usa resta leggibile: si vedono i nomi ISO o Joliet,
che ci sono comunque.

### `make iso` — il CD degli strumenti

```bash
make iso        # dist/exos-tools.iso
make run-iso    # avvia QEMU con il CD già inserito
```

Un secondo supporto, separato dal floppy e **non avviabile**: ci va ciò che in
1.44 MB non entra e che non serve a tutti. Il floppy resta il supporto di avvio
collaudato; gli strumenti cambiano spesso, pesano, e non devono poter rompere
l'avvio.

Il disco contiene oggi:

```
/leggimi.txt          cos'è questo disco e come si monta
/exos/include/        gli header della libc (stdio.h, stdlib.h, …)
/exos/libc.c          la libc in un file solo
/exos/start.S         il pezzo di avvio che chiama main()
/doc/                 README, note sul kernel, licenza
/bin/                 as, ld, cc1 e i programmi di prova
```

`/exos/` non è documentazione: è ciò che serve per **compilare su EX-OS**.
`/bin` non è più vuota: ci stanno `as` e `ld` di binutils 2.44 e `cc1` di
GCC, cioè la catena di compilazione vera — vedi
[La catena di compilazione dentro EX-OS](sviluppo.it.md#la-catena-di-compilazione-dentro-ex-os).

! Il CD è in sola lettura per costruzione, quindi header e librerie si leggono
da lì ma **l'output di una compilazione deve andare altrove** — cioè su un EX-OS
installato su ext2, o sul floppy.

### Provare il driver senza masterizzare niente

`tools/mkiso.py` genera anche un'immagine sintetica di collaudo, di cui si
conosce ogni byte — utile proprio perché, quando il driver legge un nome
sbagliato, si sa cosa c'era scritto:

```bash
python3 tools/mkiso.py /tmp/test.iso --prova                  # con Joliet
python3 tools/mkiso.py /tmp/solo-iso.iso --prova --senza-joliet
qemu-system-i386 -fda dist/floppy.img -m 32M -boot a -cdrom /tmp/test.iso
```

Lo stesso strumento fa da masterizzatore per qualunque albero di directory:

```bash
python3 tools/mkiso.py /tmp/mio.iso --da /percorso/albero --etichetta "MIO CD"
```

Costruisce **entrambi** gli alberi — nomi ISO 9660 8.3 maiuscoli con `;1` e nomi
Joliet veri in UCS-2 — condividendo i blocchi dei file, e gestisce nomi lunghi,
sottodirectory e collisioni del troncamento a 8.3 (due file distinti che
diventassero lo stesso nome sarebbero un file perso in silenzio).

---

## Nomi lunghi su FAT (VFAT), in lettura

Un FAT32 scritto da Linux o da Windows si legge con i nomi veri:

```
ex-os:/> ls /disco
appunti di riunione.txt 19
UnNomeMoltoLungoDavveroInterminabile.dati 19

ex-os:/> cat "/disco/appunti di riunione.txt"     funziona
ex-os:/> cat /disco/UNNOME~1.DAT                  funziona anche l'alias
```

Funzionano **entrambe** le vie: il nome lungo e l'alias 8.3. Confrontare
solo col lungo renderebbe impossibile aprire un file col suo alias corto,
che è un nome legittimo e che i programmi vecchi usano.

! **La somma di controllo non è facoltativa.** Ogni voce di nome lungo
porta la somma del nome 8.3 a cui appartiene, e serve a riconoscere le
catene **orfane**: un sistema che non conosce i nomi lunghi può cancellare
la voce 8.3 lasciando indietro i suoi frammenti, e attaccarli al primo
nome 8.3 che capita darebbe a un file il nome di un altro.

! **Solo lettura.** Creare un file con un nome lungo vorrebbe dire
allocare più voci consecutive e inventare un alias 8.3 unico (`NOME~1`,
`NOME~2`…): è un'altra cosa, e non c'è. Un file creato da EX-OS ha un nome
8.3, e si vede.

! **Solo ASCII**: i caratteri sopra `0x7F` diventano `?`. EX-OS non ha una
tabella di caratteri, e inventarne una qui vorrebbe dire scegliere una
codifica per tutto il sistema.

## `mkfs` sceglie il filesystem dalla dimensione

```
mkfs hd0p1        fino a 2 GB → FAT16, oltre → FAT32
mkfs -t ext2 hd0p1
```

Non è una soglia arbitraria: FAT16 arriva a **65524 cluster**, che con
cluster da 32 KB fanno poco più di 2 GB. Sotto quella misura FAT16 è
preferibile — tabella metà più piccola e root directory a dimensione
fissa, cioè meno settori da leggere per fare la stessa cosa.

! **ext2 non entra mai nella scelta automatica**: è un formato che si
chiede, non uno in cui si finisce.

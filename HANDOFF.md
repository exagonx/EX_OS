# EX-OS — Handoff

---

# SESSIONE 2026-07-31 (n) — Installatore e avvio da disco rigido

Kernel a **0.133** → **0.134**. Punto 6, l'ultimo del piano: **EX-OS si
installa su un disco rigido e ci si avvia**.

```
mount hd0p1 /disk
install /disk
```
poi si toglie il floppy e si riavvia.

## La catena di avvio nuova

```
BIOS -> MBR (bootloader/mbr/mbr.asm)
          trova la partizione attiva, ne carica il settore 0 a 0x7C00
     -> settore di avvio partizione (bootloader/stage1hd/boothd.asm)
          legge Stage 2 dagli LBA scritti nella sua area di patch
     -> Stage 2 (loader.asm, ramo nuovo)
          legge il kernel dagli LBA della stessa area, poi prosegue
          identico all'avvio da floppy
```

Marker seriali dell'avvio da disco: **`SDKPJK`** — la `D` in seconda
posizione e' il ramo nuovo che si riconosce a colpo d'occhio nel log.

## Le decisioni, e cosa proteggono

### Perche' una mappa di settori e non un lettore FAT nel settore di avvio

In 512 byte, tolti BPB e firma, restano ~320 byte. Un lettore FAT12 ci sta
a fatica, uno FAT32 no: si otterrebbe un avvio che funziona su FAT16 e non
su FAT32, cioe' una trappola.

L'installatore invece gira **dentro EX-OS**, dove il driver FAT completo
c'e' gia': trova i file, verifica che siano contigui e scrive nel settore
di avvio il loro primo LBA e la lunghezza. Il settore di avvio legge dei
settori e basta, identico su qualunque FAT.

⚠️ **Il prezzo, ed e' scritto in tre posti**: la mappa vale finche' quei
file non si spostano. Ricopiare kernel o Stage 2 sul disco **obbliga a
rieseguire `install`**. E' lo stesso patto di LILO.

### Perche' l'installazione dell'avvio sta nel KERNEL e non in /bin/install

E' l'unico punto in cui EX-OS scrive in un settore che non appartiene a
nessun filesystem. La strada facile — una syscall "scrivi questo settore
grezzo" — una volta esistita permette a **qualunque** programma di
sovrascrivere la tabella delle partizioni, e nessun controllo dentro
`/bin/install` lo impedirebbe: basta non usare `/bin/install`.

`kernel/boot/bootinst.c` compone lui i settori e garantisce due invarianti
che nessun chiamante puo' aggirare:

1. dell'MBR si riscrivono **solo i byte 0..445**. I 64 byte della tabella
   delle partizioni non vengono mai toccati, tranne il singolo byte del
   flag "attiva".
2. del settore di avvio della partizione si riscrive tutto **tranne i byte
   3..89**, che sono il BPB del filesystem gia' presente: vengono riletti
   dal disco e rimessi al loro posto.

Senza la (2), installare l'avvio renderebbe illeggibile il volume che si
sta cercando di rendere avviabile.

**Verificato dopo l'installazione**: la tabella e' identica (`start 2048,
1046528 settori, tipo FAT16`, flag di avvio acceso) e il BPB conserva
`OEM = "mkfs.fat"` ed etichetta `EXOSDISK` — cioe' quelli scritti da
`mkfs.fat`, non dal kernel.

### La partizione attiva si sceglie con lo STESSO criterio in due posti

L'MBR cerca il flag 0x80 nella tabella; `vfs_init()`, avviato da disco,
deve montare come root **la stessa** partizione. Se usasse un altro
criterio — l'ordine, o la prima FAT trovata — il kernel monterebbe come
root un volume diverso da quello da cui e' stato caricato, e il sintomo
sarebbe una `/` che non contiene il sistema che sta girando.

Entrambi leggono il settore 0 e confrontano l'LBA di partenza. Il
confronto e' sull'LBA e non sull'indice, perche' la voce N della tabella
non e' la partizione N in ordine di disco.

### Un riordino che non e' cosmetico

`ata_init()` + `blk_init()` sono stati spostati **prima** di `vfs_init()`.
Prima la root era per forza il floppy e i dischi non servivano ad avviare;
ora `vfs_init()` deve poter montare una partizione ATA, e per farlo i
dispositivi a blocchi devono gia' esistere.

Di conseguenza `cfg.c` e `drvmgr.c` sono passati da `fat12_*` a `vfs_*`:
avviando da disco, `/boot/kernel.cfg` e `/dev/kbd.drv` stanno sul disco.
Erano rimasti sul floppy di proposito finche' la root era per forza quella
— ora non lo e' piu'.

### Il floppy non si sonda quando non si e' avviati da li'

Sondare l'FDC assente costava **dodici righe di ERROR/WARN a ogni avvio da
disco**, con cinque ritentativi e attese reali. Chi legge quel log vede un
sistema che sembra rotto mentre funziona — e il rumore costante e' il modo
migliore per non accorgersi dell'errore vero, il giorno che arriva.

Conseguenza da sapere: **avviando da disco il floppy non e' raggiungibile.**
Montarlo richiedera' di separare "inizializza l'FDC" da "monta la root",
che oggi `fat12_init()` fa insieme.

### Cosa l'installatore NON fa

Non partiziona e non formatta. Sono operazioni che distruggono dati e
vanno fatte di proposito, non come effetto collaterale di "installa".

## Verifica

Disco vergine da 512 MB, una primaria FAT16 formattata con `mkfs.fat`.

```
mount hd0p1 /disk
install /disk
  + /disk/BOOT/STAGE2.BIN  (1095 byte)
  + /disk/BOOT/KERNEL.BIN  (123172 byte)
  + /disk/BOOT/KERNEL.CFG  (5459 byte)
  + 14 programmi in /BIN, libc.so in /LIB, 2 driver in /DEV
  + MBR di hd0, partizione 1 marcata attiva
    stage2: LBA 2624, 3 settori
    kernel: LBA 2640, 241 settori
```

Poi **avvio dal solo disco**, floppy staccato:

```
Drive di boot : 0x80
[PASSO 13] Avvio da disco (0x80): FDC non sondato
VFS: root '/' su hd0p1 (FAT16, avvio da disco 0x80)
[PASSO 15] Shell '/bin/sh' caricata
```

Dalla shell, con il sistema che gira **interamente da disco**:

| Comando | Esito |
|---|---|
| `ver` | 0.134 |
| `mount` | `/  hd0p1  FAT16  lettura/scrittura` |
| `ls /` | BOOT/ BIN/ LIB/ DEV/ |
| `ls /BIN` | i 14 programmi |
| `/BIN/HELLO` | eseguito |

**Zero errori e zero warning** nel log di avvio da disco.
`fsck.fat` sulla partizione: pulita, 25 file.

## Non regressione

| Configurazione | Esito |
|---|---|
| solo floppy | 0 errori, root sul floppy |
| floppy + disco installato | avvia dal **floppy** (drive 0x00), root sul floppy |

Il secondo caso e' quello che conta: un disco reso avviabile non deve
dirottare l'avvio da floppy.

## Il piano e' finito. Cosa resta, in ordine di utilita'

1. **Montare il floppy dopo un avvio da disco** — separare `fat12_init()`
   in "inizializza l'FDC" e "monta". Serve a copiare roba dentro e fuori
   dal sistema installato.
2. **Partizionare e formattare da EX-OS** (`fdisk`/`mkfs`). Oggi servono
   strumenti esterni per preparare il disco.
3. **Rendere l'installazione ripetibile senza rilanciarla**: un settore di
   avvio che sappia leggere almeno FAT16 toglierebbe il vincolo della
   contiguita' e della mappa.
4. **Verifica su hardware reale.** Tutto quanto sopra e' su QEMU: il
   Pentium II e VirtualBox non hanno ancora visto nulla di questa
   sessione, e l'MBR dipende dalle estensioni INT 13h del BIOS — che su un
   Pentium II ci sono quasi certamente, ma "quasi" non e' "verificato".

---

# SESSIONE 2026-07-31 (m) — Scrittura FAT12/16/32

Kernel a **0.132** → **0.133**. Punto 5 del piano: e' cio' che mancava per
poter installare.

## Cosa sa fare ora

`fat_create`, `fat_mkdir`, `fat_rmdir`, `fat_unlink`, `fat_write`,
`fat_truncate`, `fat_sync` in `kernel/fs/fat.c`, instradate dal VFS. I
montaggi sono ora in **lettura/scrittura** salvo richiesta esplicita
(`mount -r`, oppure `,ro` nella sezione `[mount]`).

Nuovo comando **`/bin/cp`**: copia attraversando i montaggi
(`cp /bin/sh /disk/SH` prende dal floppy e scrive sul disco). E' il mattone
di cui ha bisogno un installatore, ed e' anche il modo con cui e' stata
provata la scrittura.

## Le decisioni che contano, e perche'

### La cache diventa write-back, e lo sfratto deve riversare

Finche' la cache era di sola lettura, sfrattare uno slot voleva dire
sovrascriverlo. Da quando si scrive, sfrattare uno slot **sporco** butta
via una modifica gia' accettata dal chiamante — e la butta via in silenzio.
E' il modo piu' diretto di perdere una voce di FAT a meta' di una catena.
`cache_slot()` ora riversa prima di riusare.

Corollario meno ovvio: `settore_mut()` marca lo slot sporco **prima** che il
chiamante lo modifichi, non dopo. Fra le due cose il chiamante puo'
chiedere un altro settore, quello sfratta proprio questo slot, e la
modifica sparisce prima di essere registrata come tale.

### Le copie della FAT vanno aggiornate TUTTE

Un volume ha quasi sempre due FAT. Scriverne una sola non produce alcun
errore visibile **qui**, perche' qui si legge sempre la stessa copia. Il
danno si vede altrove: un chkdsk, o un altro sistema che legge la copia 1,
trova due mappe di allocazione che non concordano — e a quel punto non si
puo' piu' sapere quale sia quella giusta.

Unica eccezione: mirroring disattivato su FAT32 (`BPB_ExtFlags` bit 7),
dove la specifica dice che l'attiva e' una sola e le altre non vanno
toccate.

### Il cluster si marca occupato PRIMA di restituirlo

`clus_alloca()` scrive la fine-catena nella voce del cluster trovato prima
di darlo al chiamante. Fra "l'ho trovato libero" e "qualcuno lo aggancia"
il cluster risulterebbe ancora libero, e la **richiesta successiva dello
stesso chiamante** — un file che vuole due cluster — lo assegnerebbe una
seconda volta. Due file che condividono un cluster e' il danno che chkdsk
chiama *cross-linked*, e non e' riparabile senza perdere dati.

### Prima i dati, poi la dimensione

Scrivendo si fanno due cose: i byte nei cluster e la dimensione nella voce
di directory. Se manca la corrente in mezzo, l'ordine decide che cosa resta:

- **dati prima**: il file dichiara MENO di quanto e' stato scritto. Si
  perdono byte appena scritti — spiacevole, ma il file e' coerente.
- **dimensione prima**: il file dichiara byte mai scritti, e restituisce il
  contenuto PRECEDENTE di quei cluster — dati di un altro file, a un utente
  che non doveva vederli.

Le due direzioni di sbaglio non si equivalgono. Stessa logica in
cancellazione: la catena si libera **prima** di marcare la voce libera,
perche' l'ordine opposto lascerebbe cluster occupati da un file che non
esiste piu' — spazio perso per sempre, che nessuno sa a chi apparteneva.

### Il conteggio dei cluster liberi si dichiara IGNOTO, non si inventa

FSInfo su FAT32 ha un campo "cluster liberi". Tenerlo esatto vorrebbe dire
scandire l'intera FAT al montaggio (megabyte, su un volume grande). La
specifica prevede `0xFFFFFFFF` per "non noto", ed e' la risposta onesta:
scriverci un numero plausibile ma sbagliato sarebbe peggio del non saperlo,
perche' gli altri sistemi lo userebbero come se fosse vero.

`fsck.fat` infatti segnala *"Free cluster summary uninitialized"* — che e'
una **nota**, non un errore, ed e' esattamente il comportamento voluto. Il
suggerimento su dove cercare il prossimo libero, invece, e' un suggerimento
per definizione: quello si scrive.

### Data e ora: costante riconoscibile, non data finta

Non c'e' ancora un driver RTC. Scrivere una data plausibile renderebbe
indistinguibile "non lo sappiamo" da "e' stato scritto allora", e qualunque
strumento che ordini per data darebbe un risultato inventato. Si scrive
`2026-01-01 00:00`; quando ci sara' l'RTC bastera' cambiare due costanti.

### I nomi si rifiutano, non si storpiano

`nome_a_83()` respinge nomi troppo lunghi o con caratteri vietati invece di
troncarli. Molte implementazioni li ripuliscono in silenzio: il risultato e'
che l'utente chiede un file e ne ottiene un altro, con un nome che non ha
scelto e che non ritrovera'.

### Altre due, brevi

- **La root fissa di FAT12/16 non puo' crescere**: ha un numero di voci
  deciso alla formattazione. Estenderla scriverebbe sopra l'inizio dell'area
  dati, quindi "piena" e' l'unica risposta corretta.
- **Ogni percorrenza di catena ha un contatore di passi.** Una FAT corrotta
  puo' descrivere una catena ciclica, e senza quel contatore il kernel ci
  girerebbe dentro per sempre, in ring0, per colpa dei metadati di un disco
  esterno.

## Verifica — non "sembra funzionare", ma cosa dice fsck.fat

Tutte le prove su QEMU con il disco IDE da 1200 MB delle sessioni
precedenti (FAT12 su hd0p1, FAT16 su hd0p3, FAT32 su hd0p5).

Dalla shell: `mkdir`, `cp`, `delete`, `rmdir` su tutti e tre i tipi, poi
`umount` (che riversa), poi il disco estratto e passato a **`fsck.fat`**
da Debian:

| Partizione | FAT | Operazioni | `fsck.fat -n` |
|---|---|---|---|
| hd0p1 | 12 | cancellato BIG.BIN (3 MB, 384 cluster), copiato /bin/ls | ✅ pulita, 5 file |
| hd0p3 | 16 | mkdir + cp + copia di KERNEL.BIN (61 cluster) | ✅ pulita, 6 file |
| hd0p5 | 32 | mkdir + cp | ✅ pulita, solo la nota su FSInfo |

**La cancellazione di BIG.BIN non e' un caso scelto a caso**: la sua catena
comprendeva il cluster **341**, l'unico la cui voce FAT12 sta a cavallo di
due settori. Liberarla ha esercitato la scrittura a cavallo, e se avesse
sporcato la voce vicina `fsck` lo avrebbe visto come catena rotta o
incrocio.

I file scritti dal kernel sono stati riestratti con `mtools` e confrontati
**byte a byte** con gli originali:

```
/d1/LS              11776 byte   identico a build/bin/ls       ✅
/d16/NUOVA/HELLO2    4816 byte   identico a build/bin/hello    ✅
/d32/NEWDIR/MEM     12988 byte   identico a build/bin/mem      ✅
/d16/KERNEL.BIN    123172 byte   identico a build/kernel.bin   ✅  (61 cluster)
```

E la prova piu' diretta di tutte: i programmi copiati sul disco **sono
stati eseguiti** (`/d32/NEWDIR/MEM` ha stampato il suo prospetto della
memoria). Un ELF che gira e' un checksum piuttosto severo.

`mount -r hd0p5 /ro` seguito da `mkdir /ro/X` restituisce **-30 (EROFS)**.

## Non regressione

Avvio da floppy da solo: **zero errori e zero warning**, versione 0.133,
shell al prompt. `mkdir`, `cp`, `delete`, `rmdir` sul floppy funzionano —
il percorso `fat12.c` non e' stato toccato.

## Prossimo passo

Punto 6: **installatore e avviabilita' da disco**. Servono, nell'ordine:
scrivere un MBR e un settore di avvio su `hd0`, copiare kernel e /bin sul
disco, e insegnare a Stage 2 a caricare il kernel da una partizione invece
che dal floppy. Il primo e' l'unico punto in cui questo sistema scrivera'
in un settore che non appartiene a un filesystem, e va trattato con la
stessa prudenza usata finora per la finestra delle partizioni.

**Non ancora verificato sul Pentium II ne' su VirtualBox.**

---

# SESSIONE 2026-07-31 (l) — Ambiente Debian, driver FAT provato, VFS e mount

Kernel a **0.130** → **0.132**. Punti 2, 3 e 4 del piano.

## Prima di tutto: i sorgenti si sono spostati da WSL a Debian 13

GCC 14 tratta `implicit-function-declaration` come **errore**, non piu' come
avviso (GCC <= 13 lo lasciava passare). `lib/libc.c` e' self-contained e non
include `libc.h`, dove `int main(int, char**)` era gia' dichiarato: la
chiamata a `main()` dentro `_libc_start` non compilava piu'.

Corretto con la dichiarazione locale in `lib/libc.c`, prima di
`_libc_start`. Non e' un ripiego: il file non include nulla di proposito, e
il Makefile compila `lib/libc.c` **senza** `-I lib/include` in tutti i
target dei programmi utente, quindi includere l'header non avrebbe
funzionato comunque.

Sull'ambiente: serve `mtools` (`mformat`/`mcopy`/`mmd`/`mattrib`) per
`tools/mkfloppy.sh`. Il resto (gcc multilib per `-m32`, nasm, binutils,
qemu, gdb) era gia' a posto. `xorriso` risulta assente ma compare solo in
`tools/install_crosscompiler.sh`, non serve alla build.

Due cose che cambiano rispetto a WSL:

- `make` da solo costruisce **solo** `build/bin/sh` — il primo target del
  file. Per la build completa serve `make all`.
- binutils 2.44 emette `warning: LOAD segment with RWX permissions` su ogni
  link. Innocuo per un OS freestanding.

## Punto 2 — `kernel/fs/fat.c` era scritto ma non lo eseguiva nessuno

834 righe di driver FAT12/16/32, **assenti dal Makefile e senza un solo
chiamante**. Sarebbe entrato nel kernel senza che una riga venisse mai
eseguita.

Aggiunto al Makefile e provato con un autotest temporaneo (poi rimosso, come
promesso dal suo commento) che monta ogni partizione FAT, ne percorre
l'albero e legge ogni file per intero sommandone i byte.

**La somma e' cio' che distingue "ha letto qualcosa" da "ha letto i byte
giusti".** Disco da 1200 MB costruito con `sfdisk` + `mkfs.fat`, con lo slot
2 **vuoto** e una partizione **logica**, per provare insieme la numerazione
alla fdisk e i tre tipi di FAT:

| Dispositivo | FAT | Cluster | File | Byte | Somma kernel | Somma host |
|---|---|---|---|---|---|---|
| hd0p1 | 12 | 2554 | `/BIG.BIN` | 3145728 | `0x176FFE76` | `0x176FFE76` ✅ |
| hd0p1 | 12 | 2554 | `/SUB/NESTED.TXT` | 33 | `0x00000C74` | `0x00000C74` ✅ |
| hd0p3 | 16 | 51078 | `/DIR16/FILE.TXT` | 28 | `0x00000834` | `0x00000834` ✅ |
| hd0p5 | 32 | 275169 | `/BIG32.BIN` | 2097152 | `0x0F9FFBD9` | `0x0F9FFBD9` ✅ |
| hd0p5 | 32 | 275169 | `/DIR/DEEP/FILE.TXT` | 47 | `0x00000E78` | `0x00000E78` ✅ |

I conteggi di cluster (2554 / 51078 / 275169) coincidono **esattamente** con
il calcolo di riferimento fatto a parte sui byte del BPB.

Il file da 3 MB su FAT12 non e' scelto a caso: con cluster da 16 settori la
sua catena passa dal cluster **341**, che e' l'unico la cui voce FAT12 sta a
cavallo di due settori (341 + 341/2 = 511, l'ultimo byte del settore). E'
il caso descritto al punto 2 in testa a `fat.c`, e senza un file abbastanza
lungo non lo si tocca mai.

L'autotest confrontava anche il tipo dedotto da `fat.c` con quello dedotto
da `vol.c` — due implementazioni indipendenti della stessa regola. Nessun
disaccordo.

## Punto 3 — `kernel/fs/vfs.c`, lo strato di montaggio

Prima le syscall passavano il percorso risolto direttamente a `fat12_*`: il
filesystem del floppy era cablato come unico e globale.

**Il montaggio 0 e' la root sul floppy via fat12.c, e non si smonta.** Non e'
una semplificazione: e' cio' che garantisce che l'avvio resti quello
collaudato. Se domani il VFS avesse un difetto nell'instradamento, il caso
peggiore sarebbe un disco non raggiungibile, non un sistema che non parte.

Le trappole affrontate, ognuna con la sua ragione, stanno in testa a
`vfs.c`. Le due che vale la pena ricordare qui:

**Il confine del prefisso.** `/disk2` NON sta dentro `/disk`. Confrontare i
primi N caratteri e fermarsi li' manda `/disk2` sul montaggio `/disk` con
percorso interno `2` — cioe' su un disco **sbagliato, in silenzio**. Il
carattere dopo il prefisso deve essere `\0` oppure `/`. **Provato davvero**,
montando `hd0p1` su `/d1` e `hd0p3` su `/d16`: `ls /d16` elenca hd0p3.

**I punti di montaggio sono virtuali.** `/disk` non esiste sul floppy:
compare nell'elenco della root perche' `vfs_readdir` lo aggiunge. Ne segue
una regola migliore di quella Unix: montare su un nome **che esiste gia'
viene rifiutato**. Su Unix quel caso nasconde dei file senza dirlo; qui non
puo' succedere, e in cambio l'elenco di una directory non puo' mai
contenere due volte lo stesso nome.

Sulla paginazione di `readdir`: i punti di montaggio vanno emessi **in
testa** (indici virtuali 0..K-1), non in coda. In coda bisognerebbe sapere
quante voci ha il filesystem per capire dove comincia la coda, e chiedere
una pagina oltre la fine li ristamperebbe a ogni chiamata successiva.

Ricablati su `vfs_*`: `open`, `read`, `write`, `close`, `readdir`, `chdir`,
`mkdir`, `rmdir`, `unlink`, `sync`, e **il caricatore ELF** (`elf.c`,
`dynlink.c`) — cosi' i programmi sono eseguibili anche da un disco montato.

`cfg.c` e `drvmgr.c` restano su `fat12_*` di proposito: girano prima che
esista un montaggio, e leggono roba che sta sul floppy per definizione.

In `sys_chdir` e' sparito il caso speciale della root: `vfs_stat()` sa che la
radice di **ogni** montaggio non ha una voce di directory da interrogare.
Quel controllo, che li' riguardava solo `/`, avrebbe dovuto essere esteso a
ogni punto di montaggio — e dimenticarlo avrebbe reso `cd /disk` un "non
trovato".

## Punto 4 — `mount`, `umount`, sezione `[mount]`

Tre syscall nuove: `SYS_MOUNT` (191), `SYS_UMOUNT` (192), `SYS_MOUNTINFO`
(193). Un solo binario per i due comandi: guarda `argv[0]` e smonta se e'
stato invocato come `umount`. `mkfloppy.sh` lo copia due volte — ~12 KB
contro il rischio che due sorgenti divergano.

I montaggi diversi dalla root sono in **sola lettura** e la cosa e'
dichiarata, non subita: `EROFS` viene restituito **prima** di toccare il
filesystem. Lasciar fallire la scrittura piu' in basso darebbe errori
diversi a seconda del punto in cui il driver si arrende, e un giorno una
scrittura parziale.

### Verifica dalla shell, su hardware emulato con il disco attaccato

```
mount                      -> / fd0 FAT12 lettura/scrittura
mount hd0p1 /disk          -> montato (sola lettura)
mount hd0p3 /d16           -> montato (sola lettura)
mount                      -> i tre montaggi, con FAT12/FAT12/FAT16
ls /                       -> disk/ d16/ BOOT/ BIN/ LIB/ DEV/ LOADER.BIN KERNEL.BIN
ls /disk                   -> BIG.BIN 3145728, HELLO.TXT 24, SUB/
ls /disk/SUB               -> ./ ../ NESTED.TXT 33
cd /disk ; pwd             -> /disk
/d16/HELLO                 -> "Ciao da /bin/hello!"   (ELF eseguito da FAT16)
mkdir /d1/NUOVA            -> errore -30 (EROFS)
umount /d1                 -> smontato
```

`/d16/HELLO` e' la prova che conta: un ELF caricato ed eseguito da una
partizione FAT16 montata, passando dal VFS.

## ⚠️ La trappola piu' costosa della sessione: kernel.cfg troncato in silenzio

Il montaggio automatico non funzionava, senza un errore da nessuna parte.

`cfg_load()` leggeva il file in un buffer da **4096** byte. Con i commenti
della nuova sezione, `kernel.cfg` era arrivato a **5246**. Il kernel ne
leggeva 4095 e proseguiva **senza dire nulla**: `[mount]`, che sta in fondo,
spariva. Nessuna riga di codice sbagliata da cercare.

Buffer portato a 8192, ma **non e' quello il rimedio**: qualunque tetto si
scelga, un giorno il file lo supera. Il rimedio e' l'avviso aggiunto in
`cfg.c` — una lettura che riempie tutto il buffer significa che il file
potrebbe continuare, e va detto con un `LOG_ERROR`.

## Difetto minore corretto per strada

`tools/mkfloppy.sh` copiava in `/bin` **tutto** cio' che trovava in
`build/bin/`, compresi `.o` e `.d` rimasti da build precedenti: 82 KB su
1.44 MB, il 6% del floppy, occupati da roba non eseguibile. Ora copia solo
i file eseguibili.

## Verifiche di non regressione

- Avvio da floppy **senza** disco: identico a prima, shell al prompt.
- Avvio con il disco e `[mount]` attiva: montaggio al PASSO 13d.
- Avvio **senza** disco con `[mount]` attiva: `[WARN] non montato (errore
  -2)` e l'avvio **prosegue**. E' una decisione: un disco tolto o
  ripartizionato altrove non deve rendere il sistema non avviabile.

## Prossimo passo

Punto 5: **scrittura FAT16/32**. E' cio' che manca per installare, ed e'
anche il punto in cui `sola_lettura` nel VFS smette di essere una costante
e diventa una proprieta' del montaggio.

**Non ancora verificato sul Pentium II ne' su VirtualBox** — tutte le prove
qui sopra sono su QEMU con disco IDE.

---

# SESSIONE 2026-07-31 (k) — Astrazione a blocchi

Kernel a **0.128** → **0.129**. Primo dei sei punti del piano.

## `kernel/block/blk.c` — un'interfaccia, tre supporti

Registra `fd0` (floppy), `hd<n>` (disco ATA intero) e `hd<n>p<m>`
(partizione). Chi legge chiede "il settore N di QUESTO dispositivo" e non
sa se sotto c'e' un motore, un disco o una fetta di disco.

### La finestra — la proprieta' di sicurezza per cui il file esiste

Una partizione e' una **finestra**: primo settore + lunghezza. Ogni
accesso viene tradotto (`lba -> primo + lba`) e **rifiutato se ne esce**.

Il controllo sta in **un punto solo**, dove passano tutti. Metterlo nei
filesystem significherebbe riscriverlo per ognuno e sbagliarlo prima o
poi; qui rende impossibile a un driver montato su `hd0p1` di toccare
`hd0p2` o il settore 0 con la tabella delle partizioni — anche con un bug
nei calcoli o con metadati corrotti letti da un disco malformato.

**Provato**, con un autotest temporaneo poi rimosso, su `hd0p1` da 102400
settori:

| Caso | Atteso | Esito |
|---|---|---|
| ultimo settore (102399) | riesce | ✅ 0 |
| uno oltre la fine (102400) | rifiutato | ✅ -1 |
| 2 settori a cavallo del confine | rifiutato | ✅ -1 |
| n = 0 | rifiutato | ✅ -1 |
| lba = 0xFFFFFFFFFFFFFFFF (overflow) | rifiutato | ✅ -1 |

L'ultimo caso e' il motivo per cui l'overflow si controlla **prima** della
somma: `lba + n` puo' traboccare e far sembrare interna una richiesta
assurda.

## Numerazione delle partizioni: uguale a fdisk, e non era scontato

`mbr.c` registra ora il **numero** della partizione: 1-4 sono gli **slot**
delle primarie — anche se qualcuno e' vuoto — e le logiche partono da 5.
Non e' l'indice nell'array.

Senza questo, un disco con gli slot 1 e 3 occupati avrebbe prodotto i nomi
`hd0p1` e `hd0p2`, mentre `fdisk` chiama la seconda `p3`: **chi partiziona
con uno strumento esterno e poi monta qui monterebbe la partizione
sbagliata credendo di aver capito il nome.**

Verificato su un disco costruito a mano con lo slot 2 vuoto:

```
  hd0p1   partizione          2048      102400        50
  hd0p3   partizione        104448      102400        50
  hd0p5   partizione        208896      100352        49
  hd0p6   partizione        311296      100352        49
```

Numeri identici a `fdisk`. L'estesa (`p4`) **non** viene registrata come
dispositivo: e' un contenitore, e offrirla darebbe una finestra che si
sovrappone alle proprie logiche — esattamente cio' che la finestra serve
a impedire. Stesso trattamento per le partizioni che escono dal disco:
non diventano dispositivi, con un avviso.

## Il floppy passa dalla stessa cache del filesystem

`fat12_dev_read/write` sono wrapper su `fat12_read_sector/write_sector`,
**non** sull'FDC diretto. Se il livello a blocchi scavalcasse la cache
vedrebbe dati vecchi ogni volta che il FAT12 ha una scrittura ancora
sporca in memoria, e viceversa una scrittura grezza non invaliderebbe la
copia in cache. Due viste incoerenti dello stesso supporto sono un modo
perfetto per corrompere un filesystem.

## `vol_identifica` ora passa dal livello a blocchi

Prima prendeva LBA assoluti, ora prende un dispositivo e legge il **settore
0 relativo**. Due vantaggi: la lunghezza non puo' piu' divergere dalla
finestra vera perche' viene dal dispositivo stesso, e il riconoscimento del
filesystem diventa **la prova che la traduzione della finestra e' giusta** —
se lo fosse sbagliata leggerebbe l'MBR o spazzatura invece del settore di
avvio di ciascuna partizione.

Confermato: sul disco con FAT12/16/32 i tre tipi vengono ancora
riconosciuti correttamente (2552 / 51078 / 275931 cluster).

## `/bin/disk` mostra ora anche i dispositivi

Con nome, tipo, primo LBA, lunghezza e MB: e' la vista che conta per
montare, e vedere scritta la finestra serve proprio a poterla verificare.

## Prossimo passo

Punto 2 del piano: driver FAT nuovo guidato dal BPB (12/16/32), a piu'
istanze, sopra questo livello. Nasce **accanto** a `fat12.c`, non al suo
posto.

**Non ancora verificato su VirtualBox ne' sul Pentium II.**

---

# SESSIONE 2026-07-31 (j) — Riconoscimento del filesystem, e il piano per /disk

Kernel a **0.127** → **0.128**. L'utente ha chiarito lo scopo: **installare
EX-OS su disco rigido**, avviando da floppy/CD/USB, con i dischi montati
sotto `/disk` alla maniera Unix, un comando `mount` e i montaggi
automatici da `kernel.cfg`. Prove su **VirtualBox**.

## ⚠️ VIRTUALBOX: attaccare il disco al controller IDE, non SATA

Va detto subito perche' costa un pomeriggio. VirtualBox crea le macchine
nuove con un controller **SATA (AHCI)** predefinito. Il driver di EX-OS e'
**ATA/IDE su porte 0x1F0/0x170**: un disco su SATA **non verra' visto**,
e il sintomo sara' "disk non trova niente" senza alcun errore.

In VirtualBox: *Impostazioni → Archiviazione → Controller IDE → aggiungi
disco fisso*. Il tipo di controller IDE (PIIX3/PIIX4/ICH6) va bene
qualunque.

## Riconoscimento del filesystem: `kernel/block/vol.c`

Aggiunto prima del driver FAT vero, di proposito. La determinazione del
tipo di FAT e' il punto in cui quasi tutte le implementazioni sbagliano, e
**sbagliarlo non da' un errore**: da' un filesystem letto con le regole di
un altro, cioe' dati corrotti in silenzio. Vale la pena isolarlo e
provarlo prima di costruirci sopra un driver che monta e scrive.

**La regola, l'unica corretta**: il tipo si decide dal **numero di cluster
dell'area dati** — <4085 FAT12, <65525 FAT16, altrimenti FAT32. NON dal
byte di tipo MBR (0x06/0x0B/0x0C), che e' un suggerimento e puo' mentire.
NON dalla stringa "FAT16   " nel settore di avvio, che e' testo decorativo
e la specifica dice di ignorare.

I due confini hanno quei valori esatti perche' 4085 = 0xFF5 e 65525 =
0xFFF5: il primo valore di cluster che collide con i codici riservati di
ciascun formato. Non vanno arrotondati.

**Ogni divisione e' protetta.** I campi arrivano dal disco: un settore
dati scambiato per un settore di avvio produrrebbe una divisione per zero
in ring0, cioe' un **kernel panic causato da un disco esterno**. Sono
protetti: `byts_per_sec` fra i valori validi, `sett_per_cluster` potenza
di due, `riservati != 0`, `n_fat` 1-4, totale e dimensione FAT non nulli,
metadati che non superano il volume, e il volume che non dichiara piu'
settori della partizione che lo contiene.

C'e' anche un controllo di **coerenza incrociata**: un FAT32 vero ha per
forza `root_entries = 0` e `fatsz16 = 0`; un FAT12/16 ha
`root_entries != 0`. Se il conteggio dei cluster dice una cosa e questi
campi un'altra, il volume viene marcato incoerente invece di procedere —
il dubbio qui significa leggere la FAT con la larghezza sbagliata.

### Verifica

Disco da 1200 MB con tre partizioni formattate davvero con `mkfs.fat`:

| Partizione | Cluster | Atteso | `/bin/disk` |
|---|---|---|---|
| 20 MB, `-F 12` | 2552 | FAT12 | ✅ FAT12, etichetta FLOPPYLIKE |
| 100 MB, `-F 16` | 51078 | FAT16 | ✅ FAT16, etichetta DATIFAT16 |
| 1080 MB, `-F 32` | 275931 | FAT32 | ✅ FAT32, etichetta SISTEMA32 |

I conteggi di cluster coincidono **esattamente** con il calcolo di
riferimento fatto a parte sui byte del BPB.

## Architettura per /disk — dove si inserisce, esattamente

Trovato il punto: `resolve_path()` in `kernel/syscall/syscall_impl.c:89`
produce un percorso assoluto e poi le syscall lo passano **direttamente a
`fat12_*`**. Non esiste alcun VFS: il filesystem del floppy e' cablato
come unico e globale.

Lo strato di mount va inserito subito dopo `resolve_path`, come funzione
che traduce un percorso assoluto in *(istanza di filesystem, percorso
dentro quel filesystem)*.

**Nomi dei dispositivi** proposti, alla Unix ma senza lettere di unita':

```
fd0          floppy
hd0p1        disco ATA 0, partizione 1
hd1p2        disco ATA 1, partizione 2
```

**Sezione in `kernel.cfg`**, nello stile di `[modules]` gia' esistente:

```
[mount]
/disk  = hd0p1
/disk2 = hd0p2
```

**Comando**: `mount` elenca, `mount hd0p1 /disk` monta, `umount /disk`
smonta.

## Ordine dei lavori, e il vincolo che lo detta

1. **astrazione a blocchi** — un'interfaccia unica per floppy e partizioni
   ATA. E' il prerequisito di tutto.
2. **driver FAT nuovo, guidato dal BPB** (12/16/32), a piu' istanze. Non
   una modifica a `fat12.c`: quello ha LBA a 16 bit (tetto 32 MB) e la
   geometria del floppy compilata dentro.
3. **VFS con tabella di mount** e instradamento dei percorsi.
4. **`mount`/`umount` + sezione `[mount]`** in kernel.cfg.
5. **scrittura FAT16/32**, necessaria per installare.
6. **installatore** e avviabilita' da disco.

> **Il vincolo piu' stretto, in ogni passo: l'avvio da floppy che oggi
> funziona non deve rompersi.** Per questo il driver FAT nuovo nasce
> accanto a `fat12.c` e non al suo posto: il floppy continua a usare la
> strada collaudata finche' quella nuova non e' provata su entrambi.

## Restano da decidere (dalla sessione precedente, ancora aperte)

MBR soltanto o anche GPT; allineamento a 2048 settori o a 63; se
implementare la rimozione della HPA. Nessuna delle tre blocca i punti
1-4 qui sopra, quindi si procede.

**Non ancora verificato sul Pentium II ne' su VirtualBox.**

---

# SESSIONE 2026-07-31 (i) — Driver ATA/IDE, lettura MBR, /bin/disk

Kernel a **0.126** → **0.127**. Richiesta: driver FAT16 e FAT32, gestione
delle partizioni a livello disco (non BIOS), programma di formattazione.

## ⚠️ COSA È FATTO E COSA NO — leggere prima di tutto il resto

| Richiesto | Stato |
|---|---|
| Accesso al disco senza BIOS, capacità vera | ✅ fatto |
| Rilevamento spazio nascosto (64 GB visti come 32) | ✅ fatto, **non provato su disco clippato** |
| Mostrare le partizioni | ✅ fatto (`/bin/disk`, sola lettura) |
| Validazione della tabella | ✅ 9 controlli, provati su tabelle patologiche |
| Driver FAT16 | ❌ **non fatto** |
| Driver FAT32 | ❌ **non fatto** |
| Selezionare e formattare una partizione | ❌ **non fatto** |
| Creare/modificare partizioni | ❌ **non fatto** (deliberatamente) |
| ext2 | ❌ futuro |

**Perché non è stato fatto tutto**: mancava lo strato sotto. Il sistema non
aveva **alcun driver per dischi rigidi** — solo gli stub IRQ14/15 — e non
aveva nessuna astrazione a blocchi. Senza quello, un formattatore non ha
niente su cui scrivere.

## Perché il FAT12 esistente NON è una base per FAT16/32

`kernel/fs/fat12.c` non è generalizzabile con qualche `#ifdef`:

- **LBA a 16 bit ovunque**: `fat12_read_sector(uint16_t lba)`,
  `fdc_rw_sector(uint16_t lba)`, e il campo `lba` della cache. Tetto: 65535
  settori, cioè **32 MB**. Una partizione FAT32 li supera per definizione.
- **Geometria compilata dentro**: `SECTORS_PER_FAT 9`, `ROOT_DIR_LBA`,
  `TOTAL_SECTORS 2880`, `MAX_CLUSTERS 2848` sono costanti del floppy da
  1.44 MB. Un vero driver FAT deve leggere il BPB al montaggio.
- **Parla direttamente all'FDC**: nessun livello che dica "leggi il settore
  N dal dispositivo X".
- **Un solo montaggio globale**: `g_fat[]` e `g_root_dir[]` sono array
  statici dimensionati sul floppy.

Serve quindi un driver FAT nuovo, guidato dal BPB, sopra un'astrazione a
blocchi — non una modifica a quello esistente. E va fatto **senza rompere
l'avvio da floppy che oggi funziona**, il che è il vincolo più stretto.

## Cosa c'è ora

**`kernel/block/ata.c`** — driver ATA/IDE in PIO, polling (nIEN=1, niente
IRQ14/15). Attese in **tempo reale su g_ticks**, non a conteggio: è la
lezione già pagata tre volte in questo progetto.

Rileva le 4 unità dei due canali, distingue ATA da ATAPI dalla firma
0x14/0xEB, legge e scrive in LBA28 o LBA48 scegliendo da sé.

**Le tre capacità, che è il cuore della richiesta:**

| Fonte | Cosa dice |
|---|---|
| BIOS | limitata dalle barriere storiche (504 MB, 2.1, 8.4, 32, 137 GB). **Mai consultata** |
| `IDENTIFY DEVICE` | quanto il disco dichiara **adesso** |
| `READ NATIVE MAX ADDRESS` | la capacità **di fabbrica** |

Se la terza supera la seconda c'è **spazio nascosto**: HPA attiva, o il
jumper di limitazione che molti dischi dell'epoca del Pentium II avevano
proprio per farsi accettare dai BIOS con la barriera dei 32 GB. Il driver
lo rileva e lo **riporta**; non lo rimuove — togliere una HPA è una
modifica persistente al disco e non può essere l'effetto collaterale di un
rilevamento.

⚠️ **Questo percorso è verificato solo a metà**: QEMU risponde
correttamente a `READ NATIVE MAX ADDRESS` (il valore coincide con
IDENTIFY, e il +1 sull'ultimo LBA è confermato), ma **non emula una HPA**,
quindi il ramo "clippato" non è mai scattato in prova. Va verificato sul
disco vero.

**`kernel/block/mbr.c`** — lettura e validazione della tabella. Nove
controlli, ognuno per un guasto reale:

| Controllo | Perché |
|---|---|
| firma 0x55AA | senza, il settore 0 non è un MBR e le "partizioni" sono byte casuali |
| MBR protettivo (0xEE) | un disco GPT ha una finta tabella: trattarla da vera è il modo classico per distruggerlo |
| oltre la fine del disco | capita davvero quando la tabella fu scritta con una capacità diversa — per esempio con una HPA poi rimossa |
| sovrapposizioni | scrivere in una corrompe l'altra, in silenzio |
| flag di avvio ≠ 0x00/0x80 | tabella danneggiata, non una scelta |
| catena EBR ciclica | un parser ingenuo ci gira dentro **per sempre**: blocco a ogni avvio con quel disco collegato |
| dimensione zero con tipo valido | voce malformata |
| più di una estesa primaria | fuori specifica: quale contenga le logiche è indeterminato |
| troncamento | più partizioni di quante se ne possano mostrare |

Due trappole risolte nel codice, entrambe classiche:

1. **I campi CHS non vengono usati per calcolare nulla.** Oltre 8,4 GB sono
   aritmeticamente incapaci di esprimere una posizione e vengono saturati a
   `FE FF FF`. Solo `lba_inizio` e `n_settori` sono affidabili.
2. **I due riferimenti dell'EBR sono diversi**: la voce 0 è relativa
   all'EBR corrente, la voce 1 (puntatore al prossimo EBR) è relativa
   all'**inizio dell'estesa**. Confonderli produce una catena che sembra
   giusta sul primo elemento e sbaglia da lì in poi.

**`/bin/disk`** — mostra dischi e partizioni. **Sola lettura**, e in questa
fase è voluto: prima si dimostra di leggere e criticare correttamente una
tabella, poi si acquisisce il diritto di riscriverne una.

## Verifica

Disco da 512 MB creato con `sfdisk` (2 primarie + estesa con 2 logiche):
`/bin/disk` riproduce **esattamente** la mappa di `sfdisk`, LBA assoluti
delle logiche compresi (413696, 620544) — cioè la catena EBR è corretta.

Cinque tabelle patologiche costruite a mano, tutte riconosciute:

| Immagine | Esito |
|---|---|
| senza firma 0x55AA | ✅ "il settore 0 NON è un MBR", nessuna partizione inventata |
| MBR protettivo GPT | ✅ "il disco è GPT, non toccare la tabella MBR" |
| due primarie sovrapposte | ✅ segnalate |
| partizione oltre la fine + bootflag 0x55 | ✅ entrambe segnalate |
| catena EBR **circolare** | ✅ rilevata, **il sistema non si è bloccato** e il comando è uscito con codice 0 |

QEMU riconosce e classifica anche il CD-ROM ATAPI del canale secondario
senza scambiarlo per un disco guasto.

## Da decidere prima della prossima fase

1. **MBR soltanto, o anche GPT?** Per un disco da 64 GB l'MBR basta (tetto
   2 TB). GPT serve solo oltre, e raddoppia il lavoro.
2. **Allineamento delle nuove partizioni**: 2048 settori (1 MiB, universale
   e sicuro) oppure il tradizionale 63 dell'epoca?
3. **La HPA va rimossa su richiesta?** È l'unico modo di usare davvero i
   GB nascosti, ma `SET MAX ADDRESS` è persistente. Serve una conferma
   esplicita, e va deciso se metterlo del tutto.

## Piano per il resto

1. astrazione a blocchi (floppy + ATA + partizione come dispositivo)
2. driver FAT guidato dal BPB — **il tipo si decide dal numero di cluster**
   (<4085 FAT12, <65525 FAT16, altrimenti FAT32): è l'unico criterio
   corretto, e sbagliarlo è il bug classico di ogni implementazione FAT
3. `mkfs` per FAT16/FAT32
4. scrittura della tabella delle partizioni — **la parte pericolosa**, da
   fare per ultima e con conferme esplicite
5. ext2

**Non ancora verificato sul Pentium II.**

---

# SESSIONE 2026-07-31 (h) — /bin/stack, e un bug scoperto proprio guardandolo

Kernel a **0.124** → **0.126**. Richiesta: un programma che mostri la
dimensione degli stack e **come sono stati allocati**.

## /bin/stack

Nuovo `SYS_PROCINFO` (188) + `/bin/stack`. `stack -v` aggiunge gli
indirizzi grezzi.

```
PID  nome          stato      u.imp.   u.ris.   kernel
--------------------------------------------------------------
1    idle          pronto          -        -     128K
2    init          pronto          -        -     128K
3    kbd           blocc.         8K     256K     128K
4    shell         blocc.         8K     256K     128K
5    /bin/stack    esecuz         8K     256K     128K
--------------------------------------------------------------
TOTALE                           24K     768K     640K
```

La distinzione che il programma esiste per rendere visibile:

- **impegnato** — RAM fisica occupata adesso (`top - base`). Cresce su
  page fault, una pagina per volta, solo se il programma la tocca.
- **riservato** — spazio di indirizzamento prenotato (`top - limit`).
  **Non costa RAM**: è il confine oltre il quale il processo viene
  terminato.

`SYS_PROCINFO` espone gli **indirizzi grezzi**, non dimensioni già
calcolate: sono loro a rispondere alla domanda "come sono stati allocati".
Paginazione come `sys_readdir`, e per lo stesso motivo — il tetto per
chiamata protegge lo stack del kernel ma non deve diventare un tetto sul
totale, che fu il troncamento silenzioso di `ls`/`delete` della sessione (l).

Un trattino segnala un task kernel (idle, init): non ha stack utente, e un
"0 KB" sembrerebbe invece una misura.

## Il dato che salta all'occhio: 640K di stack kernel contro 24K di utente

Il lavoro sulla crescita su fault ha ridotto lo stack utente da 64 a 8 KB
impegnati, ma `KERNEL_STACK_SIZE` è **128 KB per processo, fisso**,
allocato per intero alla creazione e mai cresciuto. Oggi è **27 volte** lo
stack utente impegnato, ed è di gran lunga la voce più grossa.

Non l'ho toccato — non era la richiesta, e lo stack kernel è molto più
delicato: ci girano gli handler di interrupt, e non può crescere su fault
perché il fault stesso avrebbe bisogno di stack. Ma è il candidato ovvio
per il prossimo giro di risparmio, e va detto: 128 KB sono con ogni
probabilità un ordine di grandezza più del necessario.

## Il bug trovato guardando i numeri

Alla prima esecuzione il programma riportava **7K impegnati e 255K
riservati** invece di 8K e 256K. Non era un errore di visualizzazione.

`proc_set_entry()` (`kernel/sched/sched.c`) faceva
`proc->user_stack_top = user_stack_top`, sovrascrivendo il valore che
`elf_load` aveva appena scritto. I due sembrano la stessa cosa e non lo
sono:

- l'argomento di `proc_set_entry` è l'**ESP iniziale** — il top meno
  l'allineamento a 16 byte, meno l'eventuale `argv` già impilato da
  `sys_spawn`;
- `proc->user_stack_top` è il **top della regione di stack**, il
  riferimento da cui si calcolano impegnato e riservato.

Con la sovrascrittura ogni calcolo sbagliava di 16 byte. Prima di questa
sessione non se ne accorgeva nessuno perché **nessuno leggeva quel campo**:
kernel_main e sys_spawn usano `ElfLoadResult`, non il PCB. Il campo è ora
lasciato in pace; l'ESP iniziale vive già nello slot ECX del contesto
salvato e non ha bisogno di un secondo posto.

È un buon promemoria: uno strumento diagnostico trova bug **solo se si
guardano davvero i numeri che produce** e li si confronta con le costanti
dichiarate.

## Verifica

Compilato pulito. In QEMU: `stack` e `stack -v` corretti, valori coerenti
con `USER_STACK_INIT`/`USER_STACK_MAX`/`KERNEL_STACK_SIZE` di sched.h.
**Non ancora verificato sul Pentium II.**

---

# SESSIONE 2026-07-31 (g) — Stack utente a crescita su fault (via B)

Kernel a **0.123** → **0.124**. Scelta la via B fra quelle proposte nella
sessione (f).

## Cosa cambia

Prima `elf_load` allocava a **ogni** processo 64 KB di stack, azzerandoli
byte per byte (~65000 iterazioni), anche per programmi che ne usano
duecento. Ora due concetti prima coincidenti sono separati
(`kernel/include/sched.h`):

- **`USER_STACK_MAX` = 256 KB** — spazio di indirizzamento **riservato**.
  Non costa RAM: è solo il confine oltre il quale il processo muore.
- **`USER_STACK_INIT` = 8 KB** — RAM davvero **impegnata** al caricamento.
  Il resto viene mappato una pagina alla volta, solo se toccato.

Il tetto è salito da 64 a 256 KB **senza che nessuno lo paghi**: è il
guadagno vero: prima alzarlo costava memoria reale per ogni processo.

**Misurato in QEMU a 192 MB, stessi processi**: memoria estesa usata da
**1804 a 1644 KB**. E in uso normale (shell, driver kbd, `ls`, `mem`)
**nessun evento di crescita**: gli 8 KB iniziali bastano a tutti, quindi
il costo dei fault è zero nella pratica.

## Il pericolo trovato prima di scrivere: il kernel può faultare

`syscall_verify_ptr()` (`kernel/syscall/syscall.c:19`) controlla solo
l'**intervallo** di un puntatore utente, non che le sue pagine siano
mappate. Un programma che fa

```c
char buf[8192];  read(0, buf, sizeof buf);
```

passa un buffer legittimo di cui non ha mai toccato le pagine: **è il
kernel a scriverci per primo**, con CPL=0, quindi il fault arriva con U=0
e sarebbe stato un **kernel panic**. L'allocazione ansiosa di prima
nascondeva il problema perché le pagine c'erano comunque tutte.

Per questo `pf_cresci_stack` serve anche i fault da ring0. Lì la
condizione di vicinanza a ESP **non è applicabile**: in un fault
ring0→ring0 la CPU non impila SS:ESP, quindi `frame->user_esp` conterrebbe
un valore arbitrario.

## Come è protetta la sicurezza

`pf_cresci_stack` ritorna 0 — cioè "non è crescita, uccidi il processo" —
in ogni caso dubbio. Cinque condizioni, ognuna scarta qualcosa di preciso:

| # | Condizione | Cosa scarta |
|---|---|---|
| 1 | pagina **assente** (P=0) | violazione di protezione su pagina già mappata: non è mai crescita |
| 2 | riserva presente (`limit != 0`) | task kernel, che non hanno stack utente |
| 3 | indirizzo **sotto** la base impegnata | sopra è già mappato: il fault è altro |
| 4 | indirizzo **sopra** il limite della riserva | ricorsione infinita: è esaurimento, deve morire |
| 5 | indirizzo **vicino a ESP** (solo ring3) | puntatore impazzito finito per caso nella finestra dello stack |

La 5 usa `USER_STACK_SLACK = 32`: `pusha` scrive 32 byte sotto ESP prima
di aggiornarlo, ed è il caso peggiore fra le istruzioni che toccano
memoria sotto il puntatore di stack.

Il messaggio di errore distingue ora **esaurimento dello stack** da fault
generico: è la differenza fra "ricorsione infinita" e "puntatore
sbagliato", che prima si presentavano identiche.

## Verifica: quattro prove con un programma temporaneo

Scritto un `/bin/stktest` usa-e-getta (poi rimosso) perché **non si spedisce
codice non provato dentro un gestore di fault**:

| Prova | Atteso | Esito |
|---|---|---|
| ricorsione da 40 KB | crescita da ring3, pagina per pagina | ✅ 19 crescite fino a 83 KB, ritorno regolare |
| `sub esp, 40960` + `int 0x80` diretta, nessuna scrittura ring3 | crescita da **ring0** | ✅ `cresciuto a 0xbffea000 (83 KB, ring0)` — **senza quel ramo sarebbe stato un panic** |
| scrittura a `0xBFFC1000` (dentro la finestra, lontano da ESP) | processo **terminato** | ✅ `[FAULT] ... page fault a 0xbffc1000 — processo terminato` |
| ricorsione infinita | terminato per **esaurimento** | ✅ `PF: PID 6 ha esaurito lo stack (riserva di 256 KB ...)` |

Dopo i due processi uccisi la shell è rimasta viva e `mem` è girato
regolarmente: nessun danno collaterale.

## Costo prestazionale: nullo a regime, negativo al caricamento

Da chiarire perché nella sessione (f) avevo usato "costo" in modo
ambiguo, e l'utente ha giustamente chiesto: **il costo era rischio di
correttezza, non prestazioni**.

- **A regime**: zero. Un handler di fault gira solo quando un fault
  accade; nessuna istruzione aggiunta a context switch, syscall o
  scheduler.
- **In caricamento**: più **veloce**, non più lento — spariscono 14 delle
  16 allocazioni e ~56 KB di azzeramento per processo.
- **In crescita**: un fault per pagina, una volta sola, e solo per pagine
  davvero toccate. Il totale resta sotto quanto si pagava prima in
  anticipo.

## Correzione a quanto avevo scritto nella sessione (f)

Avevo indicato la "pagina di guardia" come il vantaggio principale della
via B. **Era sbagliato**: già prima lo stack stava a
`0xBFFEF000-0xBFFFF000` con nulla di mappato sotto, quindi un overflow
prendeva già un fault e il processo moriva con diagnostica. Il vantaggio
reale è un altro — **poter riservare spazio senza pagarlo in RAM**.

**Non ancora verificato sul Pentium II.**

---

# SESSIONE 2026-07-31 (f) — /bin/mem, e l'analisi sullo stack per programma

Kernel a **0.121** → **0.122**.

## /bin/mem — stato della memoria per fascia

Nuovo `SYS_MEMINFO` (187) + `/bin/mem`. I numeri vengono dalla bitmap del
PMM, quindi sono la situazione reale della memoria fisica.

Nuova `pmm_region_stat(base, len, &tot, &libere)` in `kernel/mm/pmm.c`:
conta le pagine di una fascia interrogando la bitmap. ~48000 iterazioni
per la fascia estesa su 192 MB, irrilevanti per un comando interattivo, e
soprattutto **nessuno stato da tenere aggiornato in tempo reale**.

Le fasce sono quelle dell'architettura PC, non una scelta di EX-OS:
convenzionale (<640 KB), superiore/UMA (640 KB-1 MB), estesa (>=1 MB).

### La memoria ESPANSA: la risposta è "non esiste, e va bene così"

Era la richiesta esplicita dell'utente, e la risposta onesta non è un
numero. La memoria **espansa** (EMS) è un meccanismo a **banchi
commutati** — una scheda EMS, o un emulatore tipo EMM386 — che affaccia
finestre di memoria dentro l'area superiore. Serviva a superare il limite
di 1 MB del modo reale su 8086/286.

EX-OS gira in modo protetto con paginazione, dove quel limite non esiste:
tutta la RAM oltre 1 MB è già direttamente indirizzabile come **estesa**.
Un gestore EMS non è una funzione mancante, sarebbe codice senza scopo.

I campi `ems_*` esistono nella struttura e valgono sempre 0, e `mem`
stampa la riga a trattini con una nota che spiega il perché: **rendere la
risposta esplicita invece di lasciare un buco che sembra una lacuna**.

### Due inciampi, entrambi dovuti a convenzioni del progetto

1. **`lib/libc.c` non include `lib/include/libc.h`**: duplica le proprie
   struct localmente (`DirEntry`, `IpcMessage`). `MemInfo` va quindi
   scritta in **tre** posti — `kernel/include/syscall.h`, `libc.h`,
   `libc.c` — e restare identica. Per questo `sys_meminfo` riceve la
   `sizeof` del chiamante e **rifiuta con EINVAL** se non coincide: una
   desincronizzazione fra le copie diventa un errore dichiarato invece di
   numeri sbagliati. `mem` lo traduce in un messaggio esplicito.
2. **La `printf` della libc non supporta la larghezza dinamica `%*u`**:
   ha i flag `-`/`0` e una larghezza numerica, nient'altro. Le colonne di
   `mem` sono scritte a mano (`%8u`, `%-14s`).

Misurato in QEMU con `-m 192M`: totale 196608 KB, convenzionale 640
(120 usati), superiore 384 (tutti riservati), estesa 195584.

## Stack per programma: è possibile, ed è più contenuto del previsto

Domanda dell'utente. Oggi `elf_load` assegna a **ogni** processo
`USER_STACK_SIZE = 65536` (64 KB = 16 pagine), driver e programmi
minuscoli compresi.

**Verificata la fattibilità**: `USER_STACK_SIZE` compare in soli tre
punti, e in `kernel/sched/sched.c:308` è **solo dentro un commento**.
L'unico codice che la usa davvero è `kernel/loader/elf.c:334`; l'indirizzo
dello stack viene calcolato lì e scritto in `proc->user_stack_base/top`,
che tutto il resto legge senza fare assunzioni. **Non c'è nessun vincolo
architetturale a uno stack di dimensione variabile.**

Le tre strade, in ordine di rapporto risultato/rischio:

| Strada | Come | Costo | Note |
|---|---|---|---|
| **A. `PT_GNU_STACK`** | il linker scrive la dimensione voluta nel program header (`ld -z stacksize=N`); `elf_load` la legge e usa il default se assente o 0 | ~15 righe in `elf.c` + un flag per programma | è il meccanismo **standard**, la dimensione vive accanto al programma, nessun file di configurazione |
| **B. crescita su fault** | mappare 1-2 pagine, allargare lo stack quando arriva un page fault appena sotto | tocca il gestore dei fault | la soluzione **generale**: footprint minimo per tutti e massimo alto per chi serve. In più regala una **pagina di guardia**: uno stack overflow diventa un fault pulito invece di corruzione silenziosa |
| C. valore in `kernel.cfg` | una voce per programma | banale | scartata: l'informazione vive lontano dal programma e si desincronizza |

Raccomandazione: **A adesso, B quando si vorrà toccare il gestore dei
fault**. Non sono alternative — A resta utile anche con B, perché dà il
tetto massimo per processo.

**Quantificazione, per non sopravvalutare il guadagno**: con shell + driver
kbd + un comando in esecuzione siamo a 3-4 processi, cioè 192-256 KB di
stack su 192 MB. Il guadagno di memoria è trascurabile su questa macchina;
il vero valore di B è la **pagina di guardia**, non il risparmio.

## Verifica

Compilato pulito. In QEMU: `mem` digitato al prompt, tabella corretta,
uscita con codice 0. **Non ancora verificato sul Pentium II.**

---

# ✅ VERIFICATO SUL PENTIUM II — 2026-07-31, kernel 0.121

**Tutta la serie di sessioni (a)-(e) è confermata su hardware reale.** Con
`verboseboot=0` il sistema si avvia **senza un solo messaggio, errore o
warning**, e la shell risponde.

Il silenzio non è assenza di informazione: `verboseboot=0` abbassa il
livello di log a WARN, quindi ogni avviso di questa serie sarebbe
comparso. Non essere comparso significa, uno per uno:

| Warning assente | Cosa dimostra |
|---|---|
| nessun `FAT12: ... ritento` / `fallita dopo 5 tentativi` | il floppy si legge **al primo colpo** su tutti i cilindri, shell compresa. I ritentativi della sessione (a) sono una rete che non serve tirare — e il supporto è sano |
| nessun `FAT12: SEEK non confermato` | il posizionamento della testina funziona |
| nessun `kbd: self-test KBC fallito` | l'8042 risponde entro la scadenza reale: la sessione (d)+(e) ha corretto il difetto vero, non l'ha aggirato |
| nessun `kbd: lettura del configuration byte fallita` | il read-modify-write del configuration byte va a buon fine — **la tastiera non funziona più per via del ripiego `0x45`**, ma perché legge e riscrive davvero il registro |
| nessun `kbd: ACK enable-scan non ricevuto` | non ci sono più byte sfasati nel dialogo col controller |
| nessun `PMM: nessuna mappa E820` | **il BIOS del Pentium II supporta E820 e la mappa è stata costruita**: la RAM non è più tappata a 64 MB |
| nessun `[PASSO 14c] ... driver ring3 assente` | il driver tastiera ring3 è caricato e in servizio |

## Unica cosa non ancora osservata

**Quanta RAM dichiara adesso la macchina.** L'assenza del warning prova
che la mappa E820 c'è, ma la riga che stampa il totale
(`PMM: pagine totali ... (N MB)`) è `LOG_INFO` e con `verboseboot=0` non
si vede. La shell non ha un comando che lo mostri.

Per leggerlo, senza modificare codice: rimettere `verboseboot = 1` in
`/boot/kernel.cfg` con `textline` per un avvio, oppure collegare la
seriale su COM1 (che riceve tutto a prescindere dal filtro di livello).
Attesi ~192 MB invece dei 66112 KB di prima.

---

# SESSIONE 2026-07-31 (e) — SYS_UPTIME e la mappa E820

Due lavori richiesti esplicitamente. Kernel a **0.119** → **0.121**.

## SYS_UPTIME (186): finalmente un orologio in ring3

Prima di questa syscall un processo ring3 non aveva **nessun modo di
leggere l'ora**: poteva solo dormire con `SYS_SLEEP`. Un driver che deve
dare una scadenza a un'attesa era quindi costretto a scegliere fra due
cose sbagliate — ed è esattamente il motivo per cui le attese del KBC
hanno sbagliato due volte in due sessioni:

| Approccio | Difetto | Dove è costato |
|---|---|---|
| contare iterazioni | dipende dalla velocità della CPU | `KBC_POLL_MAX`, sessione (c); loop di NOP dell'FDC, giugno |
| dormire a passi | granularità non migliore di un tick (10 ms) | `usleep(1000)`, sessione (d) — `kbd_hw_init()` da 40 s |

`sys_uptime` ritorna i millisecondi dall'avvio (`g_ticks * 10`). La
risoluzione resta 10 ms — è il PIT — ma è **l'ora vera**, e con l'ora si
può fare la cosa diretta: leggere, ciclare finché non è passato il tempo
dichiarato, cedere la CPU nel frattempo.

`drivers/kbd/kbd.c` è stato riscritto su questa base: giro stretto iniziale
(nessuna syscall nel caso normale), poi scadenza reale con `sched_yield()`
invece di `usleep()`. **`sched_yield` e non `usleep` è il punto**: cedere
la CPU non impone una durata minima, quindi si torna a controllare il KBC
appena lo scheduler ridà il turno, invece di aspettare comunque un tick
intero quando il byte è già arrivato.

> **Trappola per chi userà `uptime_ms()`**: torna a zero dopo ~24,8 giorni.
> Confrontare sempre DIFFERENZE fra due letture in aritmetica senza segno
> (`(unsigned)(ora - inizio) >= timeout`), mai valori assoluti. È
> documentato in `libc.h` e sopra `sys_uptime`.

## E820: da 64 MB a tutta la RAM

`bootloader/stage2/loader.asm` ora costruisce la mappa E820 con **INT 15h
AX=E820** prima di entrare in Protected Mode, e riempie `e820_count` /
`e820_addr` in BootInfo (offset +13 e +17, che il commento chiamava ancora
`reserved0`/`reserved1`). Il ripiego AH=88h resta per i BIOS che non
supportano E820.

Il lato kernel non è stato toccato: `pmm_init` aveva già tutto il percorso.

**Misurato in QEMU**: 16M → 16 MB, 32M → 32 MB, 192M → 192 MB, 512M →
512 MB, 1G → 1024 MB. Prima erano 64 MB in tutti i casi.

### Due trappole trovate scrivendo questo

1. **`E820_MAP_ADDR = 0x0A800` in `stage2.h` è sbagliato.** Quel file
   appartiene al vecchio Stage2 in C (`fat12.c`/`loader.c`/`print.c`) che
   **non viene più compilato** — il Makefile assembla solo `loader.asm`.
   0xA800 cade in mezzo alla FAT1, che stage1 carica a 0xA000 e che arriva
   a 0xB200: usarlo avrebbe distrutto la FAT proprio mentre serve a
   seguire la catena del kernel. La mappa sta a **0xD000** (libero fra
   BootInfo a 0xC000 e il kernel a 0x10000).

2. **Stage2 è passato da 583 a 1095 byte, cioè da 2 a 3 settori.** Non è
   un problema di caricamento — stage1 segue la catena FAT e non ha un
   conteggio fisso — ma è un problema di spazio: Stage2 vive a 0x0500 e la
   **GDT viene costruita a 0x0A00**, quindi il tetto è 1280 byte. Il
   margine residuo è ~185 byte. Ho aggiunto un `%if`/`%error` in fondo a
   `loader.asm` che **rompe la build** se lo si supera: quel guasto,
   scoperto sulla macchina vera, si manifesterebbe come un salto nel vuoto
   senza un solo messaggio. Se serve più spazio, spostare la GDT (è
   referenziata solo lì dentro) invece di alzare il limite.

Si chiedono **20 byte per entry** (`ECX=20`), non 24: è esattamente la
dimensione di `E820Entry` nel kernel, così la tabella si legge come un
array. I BIOS che scrivono comunque 24 byte non fanno danno — i 4 byte in
più finiscono nell'entry successiva, che viene riscritta al giro dopo.

## Verifica

Compilato pulito. In QEMU con `-m 192M`: 6 entry E820, 192 MB, boot
completo fino al prompt, `ls /bin` digitato dal monitor ed eseguito,
nessun warning `kbd:`. **✅ Confermato sul Pentium II** (vedi la sezione
di verifica in testa al file): boot pulito, nessun warning, shell
funzionante. Era la prima volta che questa serie toccava il bootloader.

---

# SESSIONE 2026-07-31 (d) — La granularità dell'attesa è un tick, non un millisecondo

**Regressione introdotta da me nella sessione (c).** Sul Pentium II la
0.118 arrivava al prompt `ex-os:/>`, subito dopo compariva la coda di un
`kbd: ... (0xffffffff)`, e **nessun tasto rispondeva**. Kernel a **0.118**
→ **0.119**.

## L'errore: contavo millisecondi, spendevo tick

Il ciclo lento delle attese del KBC chiamava `usleep(1000)` credendo di
aspettare un millisecondo per iterazione. La catena reale è:

```
usleep(1000)  ->  ms = (1000+999)/1000 = 1
              ->  sched_sleep(1)
              ->  ticks = (1+9)/10 = 1  ->  10 ms
```

Il PIT è a 100 Hz: **non si può aspettare meno di un tick**. Ogni
iterazione costava dieci volte quanto creduto. `KBC_TMO_SELFTEST = 1000`
non era un secondo ma **dieci**, e ciascuna delle otto `kbc_wait_write()`
di `kbd_hw_init()` poteva costarne cinque: nel caso peggiore
l'inizializzazione della tastiera durava **oltre 40 secondi**.

Il sintomo è ingannevole perché il prompt è già a video: la shell stampa
`ex-os:/>` e si blocca su `read(0)` mentre il driver è ancora dentro
`kbd_hw_init()`. Sembra un sistema morto, ed è invece un sistema che sta
aspettando. I messaggi `kbd:` comparivano **dopo** il prompt, che è ciò che
rendeva l'output incomprensibile.

Fix: il ciclo lento avanza a passi di `KBC_STEP_MS` (10 ms, un tick) e il
numero di passi si ricava dividendo, così il tempo dichiarato dal chiamante
è anche quello trascorso. Aggiunta `KBC_TMO_IBF` (100 ms): l'attesa che
l'input buffer si liberi non ha bisogno della stessa scadenza di un ACK.
Caso peggiore ora ~2,5 s invece di 40+, e millisecondi quando l'hardware
risponde davvero.

## Lezione da tenere

Il progetto ha già corretto due volte attese tarate male (i loop di NOP
dell'FDC a giugno, il conteggio di iterazioni del KBC nella sessione (c)).
Questa è la terza variante: **un'attesa espressa in un'unità più fine
della granularità del temporizzatore**. Qualunque `usleep()` sotto i 10 ms
in questo sistema costa 10 ms. Se serve davvero una precisione
sub-millisecondo in ring3, manca il pezzo per averla — non esiste una
syscall che legga i tick (`SYS_UPTIME`), quindi non si può fare
un'attesa a scadenza reale senza bloccarsi.

## Verifica

In QEMU, digitando dal monitor: `ls /bin` ecoato ed eseguito, nessun
warning `kbd:`. **✅ Confermato sul Pentium II**: nessun warning `kbd:`
all'avvio, quindi il controller risponde davvero entro le scadenze.

---

# SESSIONE 2026-07-31 (c) — Attese del KBC a conteggio invece che a tempo, e il tetto dei 64 MB

**La tastiera sul Pentium II funziona.** Con `verboseboot=0` (modificato
sulla macchina stessa con `textline`, quindi anche la scrittura su floppy
reale funziona) restano visibili tre warning, che è esattamente ciò che il
boot silenzioso deve fare. Kernel a **0.117** → **0.118**.

## I tre messaggi della tastiera erano tre timeout in fila

```
kbd: self-test KBC fallito (0xffffffff), continuo
kbd: lettura del configuration byte fallita, uso il ripiego 0x45
kbd: ACK enable-scan non ricevuto (0x30)
```

`0xffffffff` è `-1` stampato con `%x`: **timeout**. E il terzo messaggio
non è un errore diverso — è la conseguenza dei primi due: `0x30` è un byte
sfasato, la risposta a un comando precedente arrivata dopo che avevamo
smesso di aspettarla.

Causa in `drivers/kbd/kbd.c`: `kbc_wait_read()` e `kbc_wait_write()`
contavano **iterazioni** (`KBC_POLL_MAX = 2000`), non tempo. La
motivazione scritta nel commento — "il KBC risponde in decine di
microsecondi" — è vera per la lettura di un registro e **falsa per il
self-test `0xAA`**: l'8042 è un microcontrollore che a quel comando esegue
la propria diagnostica interna e ci mette **millisecondi**.

> È la stessa famiglia di difetti già corretta a giugno nel driver FDC (i
> loop di NOP a conteggio fisso, tarati implicitamente sulla CPU virtuale).
> Qui era mascherata dal fatto di contare syscall invece di NOP, ma la
> sostanza è identica: **un'attesa ancorata alla velocità della CPU invece
> che all'orologio**. Vale la pena cercarne altre.

Fix: attese in due fasi. Un giro veloce a vuoto (`KBC_POLL_FAST`) copre il
caso normale senza pagare una sola syscall di sleep; poi attesa in tempo
reale con `usleep(1000)`, che passa da `SYS_SLEEP` ed è quindi ancorata al
PIT. Il chiamante dichiara la scadenza, perché un ACK e un self-test non
sono la stessa cosa: `KBC_TMO_ACK` 500 ms, `KBC_TMO_CFG` 200 ms,
`KBC_TMO_SELFTEST` 1000 ms.

Nota: il ripiego `0x45` introdotto nella sessione (b) **ha funzionato** —
è il motivo per cui la tastiera andava comunque. Ma andava per caso, non
per progetto: la lettura del configuration byte va fatta davvero.

## Il terzo messaggio è un problema diverso, e resta aperto

```
PMM: nessuna mappa E820, uso fallback: 66112 KB
```

La macchina ha **192 MB**, EX-OS ne vede **64,5**. Non è un guasto e non
impedisce nulla, ma sono due terzi della RAM buttati.

Causa: `bootloader/stage2/entry.asm`, `get_memory_size` usa **INT 15h
AH=88h**, che restituisce la memoria estesa in KB **in AX — un registro a
16 bit**. Il tetto è quindi 65535 KB, cioè 64 MB, qualunque sia la RAM
installata. I 66112 KB riportati sono `65088 + 1024` (l'`add ax, 1024`
subito dopo).

**Il lato kernel è già pronto**: `pmm_init` legge `info->e820_addr` /
`info->e820_count` e ha già il percorso completo per marcare libere solo
le regioni usabili (passo 4). Manca solo che Stage2 riempia quei campi con
un vero ciclo **INT 15h AX=E820**. È un lavoro circoscritto ma tocca il
bootloader, quindi non l'ho fatto senza chiedere: un errore lì rende la
macchina non avviabile.

## Verifica

Compilato pulito (restano i due warning preesistenti). In QEMU, digitando
davvero dal monitor: `ls /bin` battuto al prompt viene ecoato ed eseguito
correttamente, e **non compare più alcun warning `kbd:`**.

---

# SESSIONE 2026-07-31 (b) — Il sistema si avvia tutto, ma la tastiera è morta: il self-test 0xAA

Esito della sessione (a): sul Pentium II **la shell ora carica e il sistema
arriva in fondo** — banner del kernel, banner della shell, prompt
`ex-os:/>`. I ritentativi sui settori erano la cosa giusta.

Due cose restavano: **nessun tasto produce alcun effetto**, e il LED del
drive resta acceso fisso (silenzioso, motore che gira, nessun ticchettio).
Kernel a **0.116** → **0.117**.

## La tastiera: `0xAA` azzera il configuration byte dell'8042

`drivers/kbd/kbd.c`, `kbd_hw_init()`, emetteva il self-test del controller
(`0xAA`) e **non toccava mai il configuration byte** — non esistevano
nemmeno le define per i comandi `0x20`/`0x60`. Si affidava implicitamente
al fatto che il BIOS avesse lasciato IRQ1 abilitato.

Su un 8042 vero **`0xAA` reinizializza il configuration byte**, e la
configurazione predefinita ha il **bit 0 (KBD interrupt enable) a ZERO**.
Da quel momento il controller riceve regolarmente gli scancode ma non alza
mai IRQ1: nessuna notifica raggiunge il driver, che resta in `ipc_recv()`
per sempre. La tastiera non è rotta, il buffer non è pieno, il driver non è
in crash — semplicemente **nessuno viene più avvisato**. È il motivo per
cui il sintomo era "sistema perfettamente avviato e completamente sordo".

**Perché in emulazione non si vede**: l'8042 di QEMU gestisce `0xAA`
restituendo `0x55` e aggiornando i flag di stato, ma **non tocca il proprio
registro di modo**. Il bit di interrupt resta come l'ha lasciato il BIOS.

Fix: dopo `0xAE`, un **read-modify-write** del configuration byte —
`0x20` per leggerlo, si forza bit 0 (IRQ1) a 1, bit 4 (clock tastiera
disabilitato) a 0, bit 6 (traduzione in set 1, che è ciò che le tabelle
scancode di questo file si aspettano) a 1, poi `0x60` per riscriverlo.
Read-modify-write e non un valore fisso: quel byte contiene anche le
impostazioni della seconda porta PS/2 e il system flag, che non ci
riguardano e non vanno calpestati. Se la lettura fallisce si usa `0x45`.

**Il percorso di ripiego non era toccato**: il TTY in-kernel
(`drivers/tty/tty.c`) si limita a drenare `0x64`/`0x60` e non emette mai
`0xAA`, quindi eredita la configurazione del BIOS e funziona. Il difetto
era esclusivo del driver ring3.

## Il motore del floppy non veniva mai spento

`fdc_motor_off()` era marcata `__attribute__((unused))`: **nessuno la
chiamava**. Il motore, acceso al primo accesso al disco, restava acceso per
sempre. In emulazione è invisibile; su una macchina vera il dischetto gira
sotto le testine per tutta la sessione, consumando supporto e drive che
hanno vent'anni abbondanti.

Lo spegnimento **non è automatico di proposito**. Un timer di inattività
sarebbe la soluzione ovvia ed è una trappola: il driver FDC non è
rientrante, e un processo bloccato dentro `fdc_wait_irq()` lascia girare
l'idle task, che spegnerebbe il motore **in mezzo a un trasferimento**. Si
spegne quindi solo da punti sincroni in cui il disco è certamente fermo:

- **fine del boot**, in `kernel_main` prima di `sched_start()`;
- **fine di `fat12_sync()`**, che gira a ogni `sys_exit` — quindi il drive
  si ferma alla fine di ogni comando, come su qualunque PC dell'epoca.

Le uscite in errore di `fat12_sync` lasciano il motore acceso di
proposito: se una scrittura è fallita, il tentativo successivo non deve
ripagare lo spin-up.

`fdc_motor_off()` ora invalida anche `g_fdc_cyl`: alcuni drive rilasciano
lo stepper quando il motore si ferma. Costa al massimo un SEEK in più e
rispetta l'invariante già scritto lì — meglio un seek in più che leggere la
traccia sbagliata credendo di sapere dove siamo.

## Verifica

Compilato pulito (restano solo i due warning preesistenti: `libc.c:601`
implicit `main` e il LOAD RWX di `kbd.drv`). In QEMU, **digitando davvero**
dal monitor (`sendkey`): `ls` e `ls /bin` battuti al prompt vengono ecoati,
eseguiti e producono l'elenco corretto, con ritorno al prompt. Verificato
di conseguenza anche il ciclo motore: spento a fine boot, `/bin/ls`
(cilindro 8) caricato correttamente dopo la riaccensione.

**✅ Confermato sul Pentium II** — vedi la sezione di verifica in testa
al file.

---

# SESSIONE 2026-07-31 (a) — Il drive è interno: non era il floppy USB, erano i ritentativi mancanti

Segnalazione: sul Pentium II reale il boot si ferma non riuscendo a
caricare `/bin/sh`. Kernel a **0.115** → **0.116**.

## Prima cosa: la pista USB della sessione (m) è chiusa

L'utente ha confermato che il drive è **interno, collegato al connettore
FDC della scheda madre**. Il controller 82077 c'è davvero, quindi tutta
l'analisi della sessione (m) sul floppy USB — per quanto corretta in
generale — **non si applica a questo caso**. Va tenuta come nota per il
futuro, non come diagnosi di questo guasto.

L'ultimo messaggio a schermo è `[PASSO 15] '/bin/sh' non trovata`.

## Il messaggio mentiva, ed è ciò che ha fatto perdere tempo

`kernel_main.c` stampava quella riga per **qualsiasi** fallimento di
`elf_load` — file assente, header non valido, errore di lettura a metà di
un segmento, memoria esaurita. Dichiarava sempre la causa meno probabile.
Il file c'era eccome. Ora il messaggio rimanda alla riga `ELF: ...`
stampata subito sopra, che la causa vera la dice.

## Il dato decisivo: dove vivono i file sul disco

Mappando `dist/floppy.img` (script in scratchpad, banale da rifare):

| Cosa | LBA | Cilindri |
|---|---|---|
| FAT + root directory | 1-9, 19-32 | **0** |
| `/bin` (dati della directory) | 34 | **0** |
| `/boot/kernel.cfg` | 200-207 | 5 |
| `/dev/kbd.drv` | 236-264 | 6-7 |
| **`/bin/sh`** | **370-396** | **10-11** |

**Tutto ciò che `fat12_init()` legge sta sul cilindro 0**, cioè dove il
`RECALIBRATE` ha appena messo la testina: quelle letture non fanno un solo
SEEK. Quindi `[PASSO 13] FAT12 OK` **non dimostra affatto** che il drive
sappia posizionarsi — la tabella diagnostica della sessione (m) su questo
punto era troppo ottimista. Correggerla mentalmente: "il disco si legge"
va letto come "il cilindro 0 si legge".

## La causa: zero ritentativi, da sempre

`fdc_rw_sector()` veniva chiamata **una volta sola** per settore. Nessun
livello dello stack ritentava. In emulazione non si nota: un floppy QEMU
non sbaglia mai una lettura. Su un drive vero gli errori transitori sono
la norma — ST1 bit 0 (MA), bit 5 (DE/CRC), bit 4 (OR) — e arrivano
tipicamente al **primo accesso a un cilindro appena raggiunto**, quando la
testina non si è ancora assestata. BIOS, DOS e Linux ritentano tutti 3-5
volte con un recalibrate in mezzo: non è prudenza eccessiva, è il modo in
cui questo hardware va usato.

Caricare `/bin/sh` sono 27 settori consecutivi a cilindro 10-11: **basta
che uno fallisca** perché `elf_load` abortisca e il kernel dica "non
trovata". Nel frattempo `fat12_init` continuava a riuscire perché faceva
le letture più facili del disco. Il sintomo puntava nella direzione
sbagliata.

## Cosa è cambiato

In `kernel/fs/fat12.c`:

- **`fdc_rw_sector()` è ora un involucro con 5 tentativi**; il corpo
  precedente è `fdc_rw_sector_once()`. Fra i tentativi si alterna
  `fdc_recalibrate()` (dispari) e la sola invalidazione di `g_fdc_cyl`
  (pari), che forza comunque un nuovo SEEK. Ogni ritentativo è
  registrato — un disco che funziona *solo* grazie ai ritentativi sta per
  morire e deve poterlo dire.
- **Motore acceso prima del `RECALIBRATE`** in `fat12_init()`. Prima
  partiva con `DOR=0x0C`: unità selezionata, motore fermo. QEMU ignora lo
  stato del motore, un drive vero è una scommessa. Costo zero, i 300 ms
  erano comunque dovuti alla prima lettura.
- **Fase di comando di SEEK e RECALIBRATE protetta da `cli`**, come già
  era quella di READ/WRITE. Il motivo è lo stesso già documentato lì (il
  timeout interno di ~500 µs fra byte di comando), e l'incoerenza non
  aveva ragione di esistere.
- L'errore FDC riporta ora **C/H/S** oltre all'LBA.

## Come leggere il prossimo tentativo sul Pentium II

| Cosa si legge | Cosa significa |
|---|---|
| prompt della shell | risolto: erano gli errori transitori |
| `READ LBA=... riuscita al tentativo N` e poi il prompt | **funziona grazie ai ritentativi**: il supporto o il drive sono deboli. Provare un altro dischetto |
| `READ LBA=... fallita dopo 5 tentativi (C=10 ...)` con ST1 | errore duro a cilindro alto: media rovinata o testina disallineata |
| `SEEK non confermato (cyl richiesto=...)` | il posizionamento non funziona proprio: è il meccanismo, non il supporto |
| errori già a `C=0` | controller o DMA: nulla a che vedere con il seek |

Se compaiono errori duri **solo a cilindri alti** mentre il cilindro 0 va,
il sospetto si sposta sul **supporto**: il dischetto è stato scritto su un
drive USB su PC moderno e riletto su un drive del 1998, e l'allineamento
delle testine fra i due può non coincidere sulle tracce interne. Provare a
scrivere il floppy da un secondo dischetto/drive è a quel punto
l'esperimento più informativo, e non costa codice.

## Stato

Compilato pulito (`-Wall -Wextra -m32`, zero warning nuovi) e verificato
in QEMU: nessuna regressione, prompt raggiunto, v0.116, e — correttamente
— **zero messaggi di ritentativo**, che è la prova che in emulazione
questo difetto non poteva manifestarsi.

**✅ Confermato sul Pentium II** — vedi la sezione di verifica in testa
al file.

---

# SESSIONE 2026-07-30 (m) — La shell non parte su hardware reale + scrittura del floppy da WSL

Segnalazione: su macchina reale la shell non si avvia. Il floppy è **USB**,
visto da Windows come A:, e l'ambiente di sviluppo è WSL. Kernel a **0.115**.

## Trovato un difetto che da solo può spiegare tutto

`fdc_motor_on()` aspettava **300 ms a ogni chiamata**, cioè a ogni accesso a
un settore, nonostante il commento dicesse "no-op se il motore è già in moto".
I 300 ms sono il tempo di stabilizzazione del motore: servono **una volta**,
non per operazione.

Il conto per un avvio: FAT (9 settori) + root directory (14) + `/dev/kbd.drv`
(~27) + `/bin/sh` (~27), più le ricerche nelle directory — oltre 80 accessi.
**Più di 24 secondi di sola attesa del motore**, a cui su hardware vero si
sommano i tempi reali di seek e di lettura. Un avvio così sembra un sistema
bloccato, ed è facilissimo spegnere prima che la shell compaia.

**Misurato in QEMU: da 19,7 s a 0,8 s.** Un fattore 24. Su hardware reale il
guadagno assoluto è ancora maggiore, perché lì i 300 ms sono veri.

Il flag `g_motor_running` viene azzerato dal reset del controller e da
`fdc_motor_off`, così l'attesa torna a farsi quando serve davvero.

## Rete di sicurezza: se l'IRQ6 non arriva

Dalla sessione (f) il driver aspetta l'IRQ6 per sapere che il comando è
finito. È il modo corretto, ma dipende da come il chipset instrada
l'interrupt: se su una macchina sconosciuta non arriva, **ogni lettura
fallirebbe** e il sistema non caricherebbe nemmeno la shell.

Ora, se `fdc_wait_irq` va in timeout, si prova a leggere la fase di risultato
sondando MSR — che è esattamente ciò che il driver faceva prima di usare
l'IRQ6. Il trasferimento lo fa il DMA, non l'interrupt: se i dati sono
arrivati, il controller è comunque in fase di risultato.

Il warning resta e dice cosa controllare: un sistema che funziona solo grazie
al ripiego deve dirlo, altrimenti il problema vero non si scopre mai.

## ⚠️ Il floppy USB: un problema che nessuna correzione del driver risolve

Questo va capito prima di continuare a cercare bug.

| Fase | Come accede al disco | Funziona con floppy USB? |
|---|---|---|
| Stage1/Stage2 (bootloader) | **BIOS INT 13h** | ✅ sì, il BIOS emula |
| Kernel (`kernel/fs/fat12.c`) | **hardware diretto**: porte 0x3F0-0x3F7, DMA canale 2, IRQ6 | ❌ **no** |

Un floppy USB **non è** collegato al controller floppy ISA. Il BIOS lo fa
sembrare un'unità A: avviabile finché si è in real mode, ma appena il kernel
passa in protected mode e parla al controller 82077, quel controller o non
esiste o non ha nessun disco dentro.

Il sintomo previsto è esattamente quello riportato: **il bootloader carica il
kernel, il kernel parte, e poi non riesce a leggere `/bin/sh`.**

Le tre strade possibili, nessuna gratuita:

1. **Usare un drive floppy interno vero** (connettore FDC sulla scheda madre).
   È l'unica che funziona subito, senza scrivere codice.
2. **Caricare `/bin/sh` come initrd**: far leggere a Stage2, che è ancora in
   real mode e può usare INT 13h, anche la shell oltre al kernel, passandola
   in memoria. Il sistema arriverebbe al prompt anche senza un driver FDC
   funzionante. Fattibile e circoscritto.
3. **Scrivere un driver USB** (UHCI/EHCI + mass storage + BOT). Enormemente
   più grande di tutto il resto del sistema messo insieme.

## Come distinguere le cause: la console seriale

Non serve indovinare. Il kernel specchia tutto su COM1, e i messaggi
introdotti nelle ultime sessioni separano i casi:

```bash
qemu-system-i386 ... -serial file:/tmp/serial.txt      # in emulazione
# su hardware reale: cavo null-modem su COM1, 38400 8N1
```

| Cosa si legge | Cosa significa |
|---|---|
| `[PASSO 13] FAT12 OK` poi `[PASSO 15] Shell ... caricata` | il disco si legge: il problema è altrove |
| `FAT12: timeout IRQ6 su READ ... proseguo con il polling` | l'IRQ6 non arriva ma il DMA sì: sistema funzionante col ripiego, da indagare |
| `timeout IRQ6 ... e nessuna fase di risultato` | il controller non risponde affatto → **compatibile con il floppy USB** |
| `FAT12: SEEK non confermato` | il controller c'è ma la testina non si posiziona |
| `[PASSO 15] '/bin/sh' non trovata — avvio senza shell` | la lettura fallisce o restituisce dati sbagliati |
| nessun `[PASSO 13]` | il kernel non arriva nemmeno lì: problema a monte |

**Prima di tutto: riprovare con questa versione e aspettare.** Se prima il
sistema veniva spento dopo pochi secondi, il difetto del motore da solo
spiegherebbe il sintomo.

## Scrivere il floppy: tre modi, un solo motore

| Da dove | Comando |
|---|---|
| WSL | `./tools/write_floppy.sh` |
| PowerShell | `.\tools\write_floppy.ps1` |
| cmd.exe / doppio clic | `tools\write-floppy.cmd` |

Tutti finiscono in `write_floppy.ps1`, che **si rieleva da solo** (UAC) in una
finestra che resta aperta per mostrare l'esito: non serve aprire una console
come Amministratore. Senza argomenti prende `..\dist\floppy.img` rispetto alla
posizione dello script e scrive su A:.

I controlli che non richiedono privilegi (immagine esistente, lettera di unità
valida) sono fatti **prima** di elevare: un refuso non deve costare una
richiesta UAC per poi scoprire che era solo un refuso.

### ⚠️ Trappola trovata: PowerShell, UTF-8 e i trattini lunghi

Il file `.ps1` va tenuto in **puro ASCII**, e c'è un motivo preciso.

PowerShell 5.1 legge uno script privo di BOM come **Windows-1252**, non come
UTF-8. Un trattino lungo `—` (U+2014, byte `E2 80 94`) diventa quindi tre
caratteri, e l'ultimo è `”` — U+201D, la virgoletta doppia tipografica di
chiusura, che il parser di PowerShell **accetta come delimitatore di stringa**.

Bastavano sei trattini lunghi nei commenti perché le virgolette del resto del
file non tornassero più: il blocco `Add-Type` smetteva di essere riconosciuto
come here-string e il parser segnalava
`Un'istruzione 'using' deve precedere tutte le altre istruzioni` su una riga
che non c'entrava nulla. Un errore che manda fuori strada, perché indica un
punto lontano dalla causa.

Diagnosticato bisezionando il file e confermato per esperimento: sostituendo i
sei trattini con `-`, lo stesso file parsa senza errori. Ora c'è un avviso in
testa allo script.

### Trappola in cui sono caduto io, durante la correzione

Uno script Python di conversione faceva `p.write_text(s, encoding="ascii")` su
un testo che conteneva ancora un carattere non-ASCII. `write_text` **apre il
file in scrittura (troncandolo) e solo dopo codifica**: l'eccezione ha lasciato
il file a **zero byte**. I controlli successivi continuavano a rispondere
"sintassi OK" — perché un file vuoto è sintatticamente valido. Il file è stato
riscritto da capo.

Lezione buona anche altrove: quando una verifica passa su un file che si è
appena scritto, vale la pena controllare che quel file non sia vuoto.

## Dettagli dell'implementazione

```bash
./tools/write_floppy.sh              # dist/floppy.img su A:
./tools/write_floppy.sh -d B: -y
```

**Perché non basta `dd`**: WSL2 gira in una VM leggera e non vede i dischi
fisici di Windows — non esiste `/dev/fd0`, e `wsl --mount` non gestisce i
floppy USB. L'unica strada è passare da Windows. Lo script converte il
percorso con `wslpath`, poi lancia `tools/write_floppy.ps1` tramite l'interop,
chiedendo l'elevazione (serve per aprire il volume grezzo `\\.\A:`).

Cosa fa lo script PowerShell, e perché:

- **blocca e smonta il volume** (`FSCTL_LOCK_VOLUME`, `FSCTL_DISMOUNT_VOLUME`)
  prima di scrivere: altrimenti il filesystem montato può riscrivere i propri
  metadati sopra l'immagine, e la cache di Windows restituire dati vecchi alla
  verifica;
- apre con `NO_BUFFERING | WRITE_THROUGH`: si va sul supporto, non in cache;
- **rifiuta le unità non rimovibili** (`DriveType != 2`) salvo `-Force`. Non è
  una formalità: lo stesso codice puntato su C: lo renderebbe non avviabile;
- chiede conferma esplicita (`SI` in maiuscolo) salvo `-y`;
- **rilegge e confronta** byte per byte, e indica il primo settore diverso.
  Un floppy con un settore difettoso è una causa perfettamente plausibile di
  "non si avvia", e senza verifica non lo si scoprirebbe.

**Non testato su hardware**: sintassi di entrambi gli script verificata e
percorsi di errore provati (immagine assente, interop mancante), ma la
scrittura vera richiede un disco inserito e l'UAC.

---

# SESSIONE 2026-07-30 (l) — "delete cancella solo le prime 128 voci?": era peggio, 64

Domanda dell'utente sul limite di `/bin/delete`. Kernel a **0.114**.

## Il limite vero era 64, e nessuno lo diceva

`sys_readdir` ha un tetto interno di **64 voci per chiamata**
(`READDIR_MAX_BATCH`), applicato *indipendentemente da quanto chiede il
chiamante*. Quindi:

- `delete` chiedeva 128 voci e ne riceveva al massimo 64;
- il suo avviso "esaminate solo le prime 128" **non poteva scattare mai**:
  `n` non poteva raggiungere 128. Codice morto che dava una falsa sicurezza;
- `/bin/ls` aveva lo stesso identico problema: buffer da 64, una sola
  chiamata. Una directory più grande veniva mostrata **incompleta senza il
  minimo avviso** — sembrava semplicemente che quei file non esistessero.

Il troncamento silenzioso è la parte peggiore: `delete /big/tmp*` avrebbe
riportato "cancellati N file" lasciandone indietro decine, e l'utente non
avrebbe avuto modo di accorgersene se non contando a mano.

## Correzione: paginazione, non un tetto più alto

Alzare il limite avrebbe solo spostato il problema. `sys_readdir` accetta ora
un **indice di partenza** (`esi`), così il chiamante può percorrere l'intera
directory a blocchi. Il tetto per singola chiamata resta — protegge lo stack
del kernel — ma non è più un tetto sul totale.

- kernel: `fat12_readdir_path(..., start)` salta le prime `start` voci valide;
- libc: nuova `listdir_from(path, buf, max, start)`; `listdir()` resta com'era
  (`start = 0`), quindi nessun programma esistente si rompe;
- `ls` e `delete` percorrono a blocchi da 32 fino alla fine.

## Perché `delete` ora lavora in DUE fasi

Prima raccoglie **tutti** i nomi corrispondenti percorrendo la directory, poi
cancella. Non è una complicazione gratuita: **non si può cancellare mentre si
elenca**. La cancellazione marca la entry come libera e `readdir` salta le
entry libere, quindi le voci successive scalerebbero all'indietro rispetto
all'indice di paginazione, e a ogni blocco si perderebbero tanti file quanti
ne sono stati cancellati nel blocco precedente.

Raccogliere prima serve anche alla richiesta di conferma, che così può dire un
numero esatto. Il limite di 256 nomi selezionati resta, ma ora **è dichiarato**:
se viene superato il programma dice di ripetere il comando.

## Secondo problema, emerso solo provando su 80 file: era inutilizzabile

Al primo test con 80 file ne sono stati cancellati **10 in 60 secondi**.
`fat12_delete` chiamava `fat12_sync()` per ogni singolo file — 32 settori
riscritti (18 di FAT + 14 di root) per ogni cancellazione. È la stessa trappola
già corretta in `fat12_write` nella sessione (g), ricomparsa in una funzione
nuova.

Correzione: la sincronizzazione si sposta **all'uscita del processo**
(`sys_exit`). È il punto giusto — un comando finisce e a quel punto tutto il
suo lavoro va su disco in una volta sola — e non costa nulla a chi non ha
modificato niente, perché `fat12_sync()` controlla i flag e i settori sporchi
prima di fare qualsiasi cosa. `ls` non paga alcun costo.

Va **prima** di `proc_exit()`, che non ritorna: lì siamo ancora in un normale
contesto di processo con gli interrupt abilitati, che è ciò che il driver FDC
richiede per le sue attese su `g_ticks` e IRQ6.

> Nota: un processo terminato da un fault non passa da `sys_exit`, quindi le
> sue modifiche possono andare perse. È il comportamento atteso — un programma
> che si schianta non ha garantito nulla.

## Verificato con una directory da 100 file

| Prova | Prima | Dopo |
|---|---|---|
| `ls /big` (100 file) | 64 voci in tutto, senza avviso | **100 file elencati** |
| `delete /big/tmp*` (80 su 100) | 10 cancellati in 60 s | **80 cancellati**, in una sola passata |
| `keep*` (20 file non corrispondenti) | — | **tutti intatti** |

---

# SESSIONE 2026-07-30 (k) — /bin/delete con caratteri jolly

Richiesta: `delete` per cancellare uno o più file, con `?` (un carattere) e `*`
(tutti i caratteri rimanenti). Kernel a **0.112**.

## `fat12_delete` esisteva ma era irraggiungibile — e rotta

Nessuna syscall la esponeva, quindi non era mai stata usata da nulla. Aveva
tre problemi:

1. **Cercava solo nella root** (`fat12_find_in_root`), quindi non poteva
   cancellare nulla dentro una sottodirectory.
2. **Non controllava l'attributo DIRECTORY.** Cancellare una directory con
   questa funzione ne avrebbe liberato il cluster lasciando i file contenuti
   irraggiungibili, con i loro cluster occupati per sempre. Ora ritorna
   `-EISDIR`: per le directory c'è `rmdir`, che verifica che siano vuote.
3. **Non sincronizzava la cache dei settori**: dopo il passaggio a write-back
   la cancellazione sarebbe rimasta in RAM.

Aggiunta `SYS_UNLINK` (10, come su Linux) e l'errno `EISDIR` (21), che mancava.

## L'espansione dei jolly sta nel programma, non nel kernel

Tre posti possibili, uno solo sensato:

- **non nel kernel**: una syscall che cancella "tutto ciò che assomiglia a X" è
  molto più difficile da rendere sicura di una che cancella un nome preciso.
  `sys_unlink` prende un nome e basta;
- **non nella shell**: la shell di EX-OS non fa espansione, e aggiungerla
  cambierebbe il comportamento di *tutti* i comandi;
- **nel programma**, come faceva MS-DOS. Con un vantaggio pratico: `delete` sa
  quali file ha selezionato e può dirlo prima di cancellarli.

### Il matcher ha bisogno di tornare indietro

`*` può assorbire una sequenza di lunghezza qualsiasi, quindi non basta
consumare avidamente: se il resto del modello non si aggancia, bisogna
riprovare facendo assorbire al `*` un carattere in più. Senza backtracking
`*.txt` fallirebbe su `NOTA.TXT` — il `*` si mangerebbe tutto e non
resterebbe nulla per `.txt`.

L'implementazione è iterativa (ricorda l'ultimo `*` e dove riprendere nel
nome): niente ricorsione, niente allocazioni. Il confronto è insensibile al
caso perché FAT12 conserva i nomi in maiuscolo, e chi digita `tmp*` si aspetta
di trovare `TMP1.TXT`.

## Conferma per le cancellazioni di massa

`delete *` su `/bin` renderebbe il sistema inservibile. Il programma chiede
conferma **solo** quando il modello è esattamente `*` e i file sono più di uno,
dicendo quanti sono e da dove:

```
delete: stai per cancellare 4 file da '/temp'. Procedere? (s/n)
```

Un modello mirato come `tmp*` non la chiede: sa già cosa sta facendo. È lo
stesso criterio del `del *.*` di MS-DOS.

Altre scelte: le directory vengono saltate nell'espansione (per quelle c'è
`rmdir`); un nome senza jolly non richiede di elencare la directory, così
funziona anche se questa contiene più voci del limite di lettura; e se il
limite viene raggiunto il programma **lo dice**, invece di far credere di aver
esaminato tutto.

## Verificato

| Comando | Esito |
|---|---|
| `delete /temp/tmp*` | cancella TMP1.TXT, TMP2.TXT, TMPA.LOG; lascia ALTRO.TXT, DATI1.LOG, DATI2.LOG |
| `delete /temp/dati?.log` | cancella DATI1.LOG e DATI2.LOG, non tocca ALTRO.TXT |
| `delete /p/*.txt` | cancella UNO.TXT e DUE.TXT, lascia TRE.DAT e QUATTRO.LOG — **il backtracking funziona** |
| `delete /p/tre.dat` | nome preciso, senza elencare la directory |
| `cd /temp` + `delete *` + `n` | chiede conferma, **annulla**, tutti e 4 i file intatti |
| `cd /temp` + `delete *` + `s` | cancella tutto; la directory resta e diventa vuota |
| `delete /temp` (directory) | `e' una directory — usa rmdir` |
| `delete /nonesiste` | `non esiste` |
| `delete /temp/zzz*` | `nessun file corrisponde` |
| `delete` senza argomenti | stampa l'uso |
| `rmdir /temp` dopo `delete *` | riesce — la catena delete → rmdir funziona |

## Nota sullo strumento di test

`tools/qemu_drive.py` non sapeva digitare `*` e `?`: aggiunti alla mappa dei
tasti (`kp_multiply` e `shift-slash`). Senza, ogni prova dei jolly sarebbe
stata impossibile — e il comando arrivava troncato a `delete /temp/tmp`, che
avrebbe potuto trarre in inganno.

---

# SESSIONE 2026-07-30 (j) — /bin/rmdir

Richiesta: `rmdir` per cancellare una directory vuota; se contiene file non
deve funzionare. Kernel a **0.111**.

`fat12_rmdir()` + `SYS_RMDIR` (40, come su Linux) + wrapper libc + `/bin/rmdir`.
Aggiunti anche gli errno `ENOTDIR` (20) e `ENOTEMPTY` (39), che mancavano.

## Il rifiuto sulle directory non vuote non è una limitazione

È l'unico comportamento sicuro. Senza una cancellazione ricorsiva, i file
rimasti dentro diventerebbero **irraggiungibili** — nessuna funzione saprebbe
più arrivarci — e i loro cluster resterebbero marcati occupati nella FAT per
sempre. Spazio perso in silenzio, recuperabile solo da un controllo del
filesystem che qui non esiste.

`fat12_dir_vuota()` scorre la catena di cluster ignorando `.`, `..` e le entry
cancellate (`0xE5`), e si ferma al primo `0x00` — che nella convenzione FAT
significa "da qui in poi non c'è più nulla". Nota: `0xE5` va **saltato** ma non
interrompe la scansione, perché dopo una entry cancellata possono esserci
ancora file vivi.

## Ordine delle operazioni, che conta

Prima si liberano i cluster, poi si marca la entry come cancellata. Se il
sistema si fermasse a metà:

- con questo ordine resta un cluster libero con una entry ancora viva →
  recuperabile;
- con l'ordine opposto resterebbe una entry cancellata e i cluster ancora
  occupati → spazio perso senza modo di ritrovarlo.

Come `mkdir`, sincronizza subito: con la cache write-back una cancellazione
solo in RAM verrebbe annullata a metà da uno spegnimento improvviso.

La root è protetta esplicitamente (`rmdir /` rifiutato), e si rifiuta di
cancellare qualcosa che non sia una directory — `rmdir` su un file sarebbe una
sorpresa sgradevole.

## Verificato

| Caso | Esito |
|---|---|
| `mkdir /vuota` poi `rmdir /vuota` | cancellata; sparita anche dall'host |
| `rmdir /piena` (contiene un file) | **rifiutato**; directory e file intatti, verificati dall'host |
| `rmdir /kernel.bin` (un file) | `non e' una directory`; KERNEL.BIN intatto, 82212 byte |
| `rmdir /nonesiste` | `non esiste` |
| `rmdir /` | rifiutato, radice protetta |
| `rmdir` senza argomenti | stampa l'uso |
| `rmdir rel` (percorso relativo) | funziona |
| `mkdir` → `rmdir` → `mkdir` | lo spazio liberato viene riusato |
| riavvio dopo i tentativi falliti | sistema integro, versione corretta |

Il test più importante è il terzo: un tentativo fallito **non deve danneggiare
nulla**. KERNEL.BIN è ancora lì con la dimensione giusta e il sistema riavvia.

---

# SESSIONE 2026-07-30 (i) — /bin/mkdir e creazione di directory

Richiesta: un programma `mkdir` che crea una directory. Kernel a **0.110**.

Non esisteva nulla a nessun livello: né `fat12_mkdir`, né una syscall, né il
programma. Aggiunti tutti e tre.

## Cosa serve per creare una directory in FAT12

Una directory è un file come gli altri con l'attributo `DIRECTORY`, e un
contenuto convenzionale: le prime due entry sono `.` (punta a sé stessa) e `..`
(punta al genitore, con **cluster 0 quando il genitore è la root**). Il resto
del cluster va azzerato — un `name[0]` a `0x00` significa "da qui in poi la
directory è vuota", ed è ciò che ferma tutte le scansioni.

`fat12_mkdir()` alloca il cluster, ci scrive `.`/`..`, crea la entry nella root
e **sincronizza subito**. Quest'ultimo punto non è pignoleria: con la cache
write-back introdotta nella sessione (g), una directory con il cluster scritto
ma la entry solo in RAM lascerebbe un cluster occupato e invisibile se il
sistema venisse spento in quel momento.

Su ogni percorso di fallimento il cluster appena allocato viene restituito
(`fat12_free_chain`), altrimenti resterebbe occupato senza appartenere a nulla.

## Limite deliberato: solo directory nella root

Il driver risolve i percorsi a **un solo livello** — `fat12_find_path` cerca la
directory genitore esclusivamente nella root. Creare `/dati/sub` produrrebbe
una entry corretta sul supporto ma **irraggiungibile**: non ci si potrebbe
entrare con `cd`, né aprirci file.

`mkdir` lo rifiuta con un messaggio esplicito invece di creare qualcosa di
inutilizzabile:

```
ex-os:/> mkdir /dati/sub
mkdir: '/dati/sub' — EX-OS supporta directory solo nella root;
       un percorso annidato non sarebbe raggiungibile
```

Chi in futuro estenderà `fat12_find_path` a più livelli toglierà anche questo
controllo — è segnato nel commento della funzione.

## Il programma

`SYS_MKDIR` è il numero 39, come su Linux. Nessun parametro `mode`: FAT12 non
ha permessi. Il programma accetta più nomi in una volta e **traduce l'errno in
una spiegazione** invece di stampare un numero:

| Errore | Messaggio |
|---|---|
| -17 EEXIST | `'X' esiste gia'` |
| -38 ENOSYS | percorso annidato non supportato (sopra) |
| -28 ENOSPC | spazio esaurito (disco o root directory piena) |

Esce con stato diverso da zero se almeno una directory non è stata creata.

## Verificato

| Caso | Esito |
|---|---|
| `mkdir /dati` | creata; `ls` mostra `DATI/`; mtools la vede come `<DIR>` |
| `cd /dati` + `pwd` | funziona, prompt `ex-os:/dati>` |
| `textline nota.txt` dentro `/dati` | file creato **nella directory giusta**, 17 byte, contenuto corretto letto dall'host |
| `ls` dentro `/dati` | mostra `./`, `../`, `NOTA.TXT` |
| `mkdir relativa` (percorso relativo) | creata, grazie a `resolve_path()` della sessione (g) |
| `mkdir /uno /due` | entrambe create |
| `mkdir /dati` già esistente | rifiutato con messaggio |
| `mkdir /dati/sub` | rifiutato con spiegazione |
| `mkdir` senza argomenti | stampa l'uso |
| riavvio | directory e file persistono |

Le directory create dentro EX-OS sono leggibili dall'host con mtools, e il file
scritto dentro `/dati` sopravvive ai riavvii — cioè la struttura su disco è
davvero conforme a FAT12, non solo coerente con sé stessa.

---

# SESSIONE 2026-07-30 (h) — verboseboot: il default era 1 solo sulla carta

Richiesta: `verboseboot` deve valere 1, e se la voce è assente deve comunque
diventare 1. Kernel a **0.109**.

## Cosa era già corretto

Il default `verbose_boot = 1` è impostato in `cfg_load()` **prima** di aprire il
file, quindi sopravvive sia alla voce assente sia a un file mancante o
illeggibile. Il file e il generatore del Makefile scrivono entrambi
`verboseboot=1`.

## Cosa NON lo era

Verificando il caso "valore non valido" ho trovato che il codice non faceva
quello che il suo stesso commento prometteva:

```c
/* Qualunque valore diverso da 0 vale 1: "verboseboot = si"
 * o un refuso danno comunque un boot verboso... */
cfg->verbose_boot = (cfg_atoi(value) != 0) ? 1u : 0u;
```

`cfg_atoi()` si ferma al primo carattere non numerico e ritorna **0**. Quindi
`verboseboot = si`, un valore vuoto o un qualunque errore di battitura
**zittivano** il sistema — l'esatto contrario del comportamento sicuro
dichiarato nel commento. Un utente che sbaglia a scrivere il valore si ritrova
un boot muto senza capire perché.

Ora il valore è considerato zero solo se è **davvero un numero** e quel numero
è zero.

## Verificato, tutti e cinque i casi

| Configurazione | Risultato |
|---|---|
| `verboseboot = 1` | `verbose = 1` ✅ |
| voce **assente** dal file | `verbose = 1` ✅ |
| `verboseboot = si` | `verbose = 1` ✅ |
| `verboseboot =` (vuoto) | `verbose = 1` ✅ |
| `verboseboot = 0` | `verbose = 0` ✅ |

Ogni riga è un boot reale con l'immagine floppy modificata, non una
deduzione dal codice: i primi tre casi non erano mai stati provati, ed è
proprio lì che si nascondeva il difetto.

---

# SESSIONE 2026-07-30 (g) — "premendo l mi dice documento vuoto": sei difetti dietro un sintomo

Segnalazione dell'utente: aprendo `/boot/kernel.cfg` con textline, il comando
`l` risponde "documento vuoto". Kernel da **0.103 a 0.107**.

Il sintomo era reale ma era solo il primo di una catena: modificare un file in
una sottodirectory toccava sei difetti diversi, ognuno dei quali nascondeva il
successivo.

## 1. ⛔ Nessuna syscall risolveva i percorsi relativi

`g_cwd` era impostata da `chdir()` e letta da `getcwd()`. **Nient'altro la
usava**: `open`, `exec`, `spawn` e `readdir` passavano la stringa ricevuta
direttamente a `fat12_*`, che la interpreta sempre a partire dalla root.

Quindi dopo `cd /boot`, `textline kernel.cfg` cercava `/kernel.cfg`, non lo
trovava, e l'editor apriva — correttamente, per la sua semantica — un documento
nuovo e vuoto. Da cui "documento vuoto".

Nuova `resolve_path()`, applicata a open/exec/spawn/readdir/chdir. Gestisce
anche `.` e `..`, perché `cd ..` è la prima cosa che si prova.

**In più**: `chdir()` non verificava nulla — `cd /inesistente` "riusciva" e da
quel momento ogni percorso relativo puntava in un posto che non c'è. Ora
controlla che la destinazione esista e sia una directory.

## 2. ⛔ `root_index` calcolato da un puntatore allo STACK

Per un file **già esistente** nella root, `fat12_open` faceva:

```c
entry = fat12_find_path(path, &path_entry);
if (entry) entry = &path_entry;              /* ← copia sullo stack */
...
uint32_t idx = ((uint8_t *)entry - g_root_dir) / sizeof(Fat12DirEntry);
```

La differenza fra un indirizzo di stack e un array statico dà un indice fuori
scala, che `fat12_write` usava poi per `g_root_dir[indice_assurdo] = entry`:
**una scrittura fuori dai limiti nella memoria del kernel**.

Non era mai emerso perché l'unico percorso esercitato era quello di
*creazione*, che imposta l'indice correttamente. Bastava salvare due volte lo
stesso file per attivarlo.

Ora la entry viene localizzata esplicitamente — indice nella root, oppure
settore e posizione nella sottodirectory — invece di essere dedotta da un
puntatore.

## 3. ⛔ Scrivere in una sottodirectory non lasciava traccia

Due difetti in uno:

- la **creazione** avveniva sempre nella ROOT, ignorando la directory
  richiesta: `/boot/test.txt` creava `TEST.TXT` in root;
- per un file esistente in sottodirectory la entry non veniva **mai**
  riscritta, quindi dimensione e primo cluster restavano quelli vecchi. Il
  codice lo chiamava "limitazione nota".

L'editor diceva "salvato" e il file non esisteva. Aggiunti
`fat12_split_path()`, `fat12_dir_scan()` e `fat12_writeback_entry()`.

## 4. ⛔ `O_TRUNC` non era implementato da nessuno

Definito negli header, ignorato da `fat12_open`. Siccome `fat12_write` si
accoda a `entry->file_size`, salvare su un file esistente **appendeva** il
nuovo contenuto al vecchio: kernel.cfg passava da 79 a 88 righe, con l'inizio
del vecchio contenuto ricomparso in coda.

Truncare significa anche liberare la catena di cluster, altrimenti la FAT
continua a dichiarare occupati cluster che non appartengono più a nessuno.

## 5. 🐢 Salvare richiedeva minuti

Con i difetti precedenti risolti, il salvataggio *funzionava* ma era
inutilizzabile. Due cause, entrambe misurate:

- **`fat12_write` riversava FAT e root directory su disco a ogni chiamata.** Un
  editor salva riga per riga: 79 righe = 158 `write()`, ognuna delle quali
  scriveva 18 settori di FAT (due copie) più 14 di root. Oltre 5000 scritture
  di settore per 3,6KB. Ora il flush avviene in `fat12_close()` e
  `fat12_sync()` — una volta sola, a lavoro finito.
- **La cache dei settori era write-through**, quindi ogni riga toccava
  davvero il floppy. Resa **write-back**, e portata da 4 a 16 slot: scrivere un
  file tocca ciclicamente il settore dati, quello della directory e quelli
  della FAT, e con 4 slot si sfrattavano a vicenda.

Il compromesso del write-back — dati non ancora sul supporto se si toglie
corrente — è accettabile proprio perché esiste la procedura di arresto della
sessione (e) che chiama `fat12_sync()`. Le due cose si tengono.

Misurato: da "24 righe in 25 secondi" a **79 righe salvate ben dentro i 20
secondi di attesa del test**.

## 6. ⛔ Il Makefile non ricompilava al cambiare di un header

Scoperto per caso durante le verifiche: dopo aver incrementato `EXOS_VERSION`
il sistema continuava a mostrare la versione precedente. Nessuna regola
dipendeva dagli header, quindi `version.h` poteva cambiare senza che nulla
venisse ricompilato.

È **la stessa classe di trappola già documentata a luglio per `LIBC_SRC`** —
"un fix sembra non avere effetto". Aggiunti `-MMD -MP` e l'inclusione dei file
`.d` generati: ora ogni `.o` dipende dagli header che include.

## Verifica finale: il caso dell'utente, dall'inizio alla fine

```
ex-os:/> cd /boot
ex-os:/boot> textline kernel.cfg
textline: 'kernel.cfg' caricato, 79 righe
*m14
   14: timer_hz  = 100
   14> timer_hz  = 100
*e
textline: 'kernel.cfg' salvato, 79 righe
```

Confronto del file dall'host prima e dopo: **79 righe entrambi, byte
identici**. E il sistema **riavvia dall'immagine riscritta dall'editor**,
leggendo correttamente la configurazione (`loglevel = 3`, `verbose = 1`,
`modules = kbd`).

## Nota di rischio

Il write-back sposta il momento in cui i dati toccano davvero il floppy. Chi
spegne la macchina senza `halt` o `poweroff` può perdere l'ultimo lavoro. È il
comportamento normale di qualunque sistema con cache in scrittura, ma qui è
nuovo: prima ogni scrittura andava subito sul supporto (al prezzo di essere
inutilizzabile). `fat12_close()` riversa comunque tutto alla chiusura del file,
quindi la finestra reale è stretta.

---

# SESSIONE 2026-07-30 (f) — /bin/textline, e la scoperta che scrivere su disco non aveva mai funzionato

Richiesta: un editor di testo lineare stile edlin (`textline`), con opzioni
`-v`/`-vp`/`-c:`, comandi `h m n d c l lp`, ESC per annullare; e — requisito
sottolineato — **tutte le librerie devono essere dinamiche**.

Kernel a **0.103**, Text Line a **0.001**.

## Riepilogo onesto di cosa è stato consegnato

| Parte | Stato |
|---|---|
| `/bin/textline` completo e funzionante | ✅ fatto e testato |
| Tre bug del kernel che impedivano all'editor di salvare | ✅ trovati e corretti |
| Librerie dinamiche | ❌ **non implementato** — fattibilità validata, piano concreto in fondo |

## Il muro: scrivere un file non aveva MAI funzionato

`textline` è il primo programma di EX-OS che scrive su disco. Appena ha
provato a salvare sono emersi **tre difetti indipendenti**, tutti nella stessa
condizione: codice mai esercitato da nessuno.

### 1. ⛔ Il comando FDC di scrittura chiedeva un'intera traccia

`fat12_write_sector` inviava `0xC5` con `EOT = SECTORS_PER_TRACK`. Il bit 7 di
0xC5 è **MT (Multi-Track)** e EOT=18 significa "scrivi da questo settore fino
al 18° della traccia, poi passa all'altra testina": il controller si aspettava
decine di migliaia di byte, il ciclo gliene mandava 512. Restava in fase di
esecuzione (`MSR=0xb0`) e la fase di risultato non arrivava mai.

Il commento nel codice diceva già `MT=0` — era il valore a non corrispondere.
La lettura era corretta (`0x46`, MT=0, EOT=sec) perché era l'unica esercitata.

### 2. ⛔ QEMU non completa le scritture non-DMA — misurato, non supposto

Corretto il comando, la scrittura è passata da `MSR=0xb0` a `MSR=0x30` e
comunque non finiva. Invece di continuare a ipotizzare ho **misurato quanti
byte il controller accetta davvero**, con un ciclo strumentato:

```
DIAG: byte accettati=512 MSR finale=0x30
```

Esattamente 512, poi `RQM=0, EXM=1, CMD BSY=1` per sempre — nessuna fase di
risultato. E i dati **arrivavano sul disco**: confrontando l'immagine floppy
prima e dopo, il settore 379 passava da tutti zeri a `ZZZTEST12345`. QEMU
scrive il settore ma non conclude il comando; `-d guest_errors` non registra
nulla.

Escluse per esperimento: il filesystem host (provato anche su `/tmp`), lo stato
degli interrupt (provato con IF=1 durante l'attesa).

**Correzione: passaggio al DMA (canale 2)** per lettura e scrittura. È la strada
corretta a prescindere, e risolve tre problemi in uno:

- il trasferimento non passa più dalla CPU, quindi **cade il `cli`** che
  proteggeva il ciclo PIO;
- la fine del comando è segnalata dall'**IRQ6**, che dalla sessione (b) sappiamo
  già attendere con `fdc_wait_irq()`;
- **toglie l'ostacolo principale al driver floppy in ring3**, documentato nella
  sessione (b): senza DMA un processo utente verrebbe preemptato in mezzo al
  trasferimento causando un overrun, e non può fare `cli`. Ora quella strada è
  aperta.

Lettura e scrittura sono state unificate in un solo `fdc_rw_sector()`: prima
erano due copie quasi identiche della stessa sequenza, ed è **il motivo per cui
il baco è sopravvissuto tanto** — la correzione applicata alla lettura non si
era propagata all'altra copia. La fase di risultato ora legge i **sette** byte
previsti, non tre: leggerne meno lasciava il controller a metà risultato, e il
comando successivo falliva con "FDC non in modalita' write".

Il `cli` resta sulla sola fase di **comando**: il controller ha un timeout
interno di ~500 µs fra un byte e il successivo, e un IRQ0 in mezzo ai 9 byte
glielo farebbe abbandonare. La fase di attesa non può averlo — aspettiamo
proprio un interrupt.

### 3. ⛔ `fat12_write` sovrascriveva invece di accodare

Scriveva **sempre dall'inizio dell'ultimo cluster**, ignorando la posizione
raggiunta nel file. Con le quattro `write()` che textline fa per salvare due
righe:

| chiamata | effetto reale |
|---|---|
| `write("prima riga", 10)` | scrive a offset 0 |
| `write("\n", 1)` | scrive a offset 0 — sovrascrive la 'p' |
| `write("seconda riga", 12)` | scrive a offset 0 |
| `write("\n", 1)` | scrive a offset 0 |

Risultato sul disco: `\neconda riga`, con dimensione dichiarata 24 byte —
la `file_size` era l'unica cosa che si accumulava correttamente. È esattamente
ciò che si vedeva.

Ora è un vero append: parte da `entry->file_size`, percorre la catena dei
cluster fino a quello che contiene quella posizione (estendendola quando
serve) e usa read-modify-write sui settori riempiti in parte, azzerando i
cluster appena allocati invece di ereditarne la spazzatura.

**Verificato dall'esterno**: il file scritto da dentro EX-OS, letto dall'host
con `mtype`, è byte-esatto.

## `/bin/textline`

Modello edlin: si opera per numero di riga, non con un cursore. È la scelta
obbligata — il TTY consegna il testo una riga alla volta e non esiste una
modalità raw, quindi un editor a schermo pieno non potrebbe leggere i tasti
mentre vengono premuti.

Tutte le opzioni e i comandi richiesti sono implementati e testati:
`-v`, `-vp`, `-c:file2`, `h/help`, `l`, `lNN`, `lNN,MM`, `lpNN,MM`, `m`, `mNN`,
`n`, `dNN`, `cNN,MM`, ESC.

### Tre decisioni da conoscere

**Righe in un array statico, non allocate.** La `free()` della libc è un no-op
dichiarato (allocatore a bump su `sbrk`): un editor modifica le righe molte
volte e ogni modifica perderebbe per sempre la memoria precedente. 512 righe ×
128 caratteri = 64KB di BSS, costo noto e costante, zero sul floppy.

**ESC si rileva dentro la riga, non come tasto immediato.** Il servizio kbd
consegna la riga solo su Invio, quindi un ESC premuto arriva come byte `0x1B`
insieme al resto: si considera annullata qualunque riga che ne contenga uno. Un
ESC che interrompe all'istante richiederebbe una modalità raw nel protocollo
IPC della tastiera, oggi assente.

**`-vp` chiede INVIO, non "un tasto qualunque"**, per lo stesso motivo.

### Comandi aggiunti rispetto alla specifica

`w` (salva), `e` (salva ed esce), `q` (esce senza salvare, con conferma se ci
sono modifiche). Senza un modo di salvare e uscire l'editor non sarebbe
utilizzabile; i nomi vengono da edlin (E = end, Q = quit).

## Due correzioni di supporto

- **`getchar()` della libc era rotto sul TTY a righe**: faceva `read(0,&c,1)`,
  e il servizio kbd consumava l'INTERA riga per consegnarne un solo carattere.
  `gets()`, che lo chiama in ciclo, restituiva quindi il primo carattere di
  ogni riga digitata e ne buttava il resto. Ora la libc tiene un buffer di riga
  interno: comportamento corretto e una syscall per riga invece che per
  carattere. (textline non lo usa — legge con `read()` diretta — ma qualunque
  altro programma sì.)
- **Il driver kbd non ecoa più i caratteri di controllo**: finiscono
  regolarmente nel buffer di riga, ma non a video, dove la VGA li renderebbe
  come glifi casuali della code page 437. Senza questo, premere ESC in textline
  faceva comparire una freccia.

## ❌ Librerie dinamiche — non implementato, ma la strada è verificata

Questo era un requisito esplicito e **non è stato fatto**: `textline` è linkato
staticamente, come `ls`. Non l'ho lasciato per svista — richiede un dynamic
linker nel kernel, che è un lavoro di dimensioni paragonabili all'editor
stesso, e farlo di fretta su un sistema che ora funziona sarebbe stato
imprudente.

**Quello che ho verificato sperimentalmente**, perché il prossimo passo parta
da fatti e non da ipotesi:

1. Il toolchain produce già il binario giusto. Compilando `textline.c` senza
   `libc.c` e linkandolo contro `libc.so` si ottiene un **ET_EXEC dinamico**
   con tutto il necessario:
   ```
   .rel.plt: 9 × R_386_JUMP_SLOT → printf, write, read, strncpy,
                                    _libc_start, strcmp, strlen, open, close
   PLTGOT 0x8003ff4   JMPREL 0x804832c   DT_NEEDED [libc.so]
   ```
2. **`-soname libc.so` è già stato aggiunto** al Makefile: senza, il `DT_NEEDED`
   conteneva il *percorso* passato a ld (`build/lib/libc.so`) invece del nome, e
   il loader avrebbe cercato un file inesistente.
3. Il kernel ha già il meccanismo per scrivere nello spazio di un altro
   processo: `paging_get_physical(proc->page_directory, vaddr)` più l'identità
   di mapping della memoria bassa — è così che `elf_load` copia i segmenti, ed è
   esattamente ciò che serve per applicare le rilocazioni sulla GOT.
4. `kernel/loader/dynlink.c` esiste (761 righe) con `dl_load_so`,
   `dl_apply_relocations`, `dl_lookup_symbol`, e prende già un `Process *`. È
   però scritto per mappare i moduli nello spazio del **kernel** e ha una cache
   globale delle librerie, che per il per-processo va ripensata.

**Cosa resta da fare**, in ordine:

1. Un linker script per eseguibili dinamici: quello attuale ha
   `/DISCARD/ { *(.rel*) *(.gnu*) }` e butterebbe via proprio le sezioni
   necessarie. Serve anche per compattare il layout — il link di prova produce
   6 segmenti LOAD sparsi fra `0x07fff000` e `0x08049000`.
2. In `elf_load`: individuare `PT_DYNAMIC`, leggere `DT_NEEDED`/`DT_JMPREL`/
   `DT_PLTRELSZ`/`DT_SYMTAB`/`DT_STRTAB`.
3. Caricare `/lib/libc.so` nella page directory del processo a una base fissa
   (es. `0x40000000`) e applicare le sue `R_386_RELATIVE`.
4. Per ogni `R_386_JUMP_SLOT` dell'eseguibile: risolvere il nome nella
   `.dynsym` della libreria e scrivere `base + st_value` nello slot GOT.
5. Convertire `textline` e `ls`, e misurare il guadagno reale di spazio (oggi
   `ls` 10784 byte, `textline` 17216, `sh` 13676 — la libc è duplicata in
   ognuno).

Nota di progetto: la condivisione sarà **di codice sul floppy e di pagine
fisiche**, non di indirizzi — ogni processo ha la propria page directory, quindi
la libreria va mappata in ciascuna. Le pagine fisiche però possono essere le
stesse, ed è lì che sta il risparmio di RAM.

## File toccati

`bin/textline/textline.c` e `.ld` (nuovi), `kernel/fs/fat12.c` (DMA + append),
`lib/libc.c` (getchar), `drivers/kbd/kbd.c` (eco), `Makefile` (regola textline,
soname), `kernel/include/version.h`, `tools/qemu_drive.py` (tasto ESC, args
QEMU extra).

---

# SESSIONE 2026-07-30 (e) — `halt` causava un kernel panic: arresto e spegnimento veri

Segnalazione dell'utente: `halt` mostra un kernel panic. Richiesta: un arresto
pulito, sistema pronto per essere spento, e se possibile lo spegnimento hardware
dopo 3 secondi.

Versione portata a **0.102**.

## La causa, che spiegava anche un secondo comando rotto

```c
static void cmd_halt(void)      /* ← girava in RING3 */
{
    println("Sistema fermato. Spegnere il computer.");
    __asm__ volatile ("cli; hlt");     /* cli e' PRIVILEGIATA */
}
```

`cli` in ring3 solleva **#GP (vettore 13)**. Non essendoci un handler registrato
per quel vettore, l'eccezione finiva nel ramo di default di `isr_handler()` —
che faceva `kpanic()` per qualunque eccezione non gestita. Da cui il panic.

**`reboot` era rotto allo stesso identico modo**: faceva `cli`, `inb $0x64` e
`outb $0x64` in ring3. Il panic arrivava un istante dopo, ma arrivava.

## Tre livelli di correzione

### 1. La causa radice: un'eccezione da ring3 non deve abbattere il kernel

`isr_handler()` faceva panic per QUALUNQUE eccezione senza handler, anche se
generata da un processo utente. Bastava che un programma ring3 eseguisse
un'istruzione privilegiata per far cadere tutto il sistema.

L'equivalente per il #PF era già stato sistemato a giugno con lo stesso
ragionamento (`page_fault_handler`): un processo che sbaglia deve morire da
solo. Ora la protezione è estesa a **tutte** le altre eccezioni — #GP, #UD,
divisione per zero. Il discriminante è `(frame->cs & 3) == 3`: i due bit bassi
del CS salvato sono il CPL al momento dell'eccezione.

Il `kpanic()` resta per le eccezioni originate in ring0, dove indicano un bug
reale del kernel e proseguire sarebbe pericoloso.

**Verificato** con un builtin temporaneo che eseguiva `cli` in ring3:

```
[FAULT] PID 4 'shell': eccezione 13 (#GP General Protection Fault)
        a EIP=0x08001813 err=0x00000000 — processo terminato
```

Il kernel è sopravvissuto. (La shell no, ovviamente: il sistema resta vivo ma
senza console. Un init che la riavvii è lavoro futuro — vedi in fondo.)

### 2. `kernel/arch/x86/power.c` — la sequenza di arresto, dove è legale

`power_off()`, `power_halt()`, `power_reboot()`. Tutte:

1. **sincronizzano il filesystem** (`fat12_sync()`, nuova): FAT, root directory e
   settori in cache marcati dirty. Spegnere senza farlo perderebbe le modifiche e
   lascerebbe il floppy con un filesystem incoerente. Dalla shell non era
   comunque possibile;
2. **fermano lo scheduler** (`sched_stop()`, nuova);
3. agiscono.

`power_off()` aggiunge il conto alla rovescia di 3 secondi richiesto, basato su
`g_ticks` e non su cicli di CPU — stesso motivo per cui i ritardi dell'FDC sono
stati convertiti a giugno: un conteggio a NOP durerebbe "3 secondi" solo sulla
macchina su cui è stato tarato.

> **`sched_stop()` NON maschera IRQ0, di proposito.** Mascherarlo fermerebbe
> `g_ticks`, e tutto ciò che serve ancora — il conto alla rovescia e i delay del
> driver FDC durante la sincronizzazione — è basato proprio su `g_ticks`: si
> bloccherebbe per sempre. È lo stesso errore del deadlock del PIC di luglio, in
> forma diversa. Il tick continua a contare, i context switch no.

### 3. `SYS_REBOOT` (88) e i comandi della shell

Numero preso da Linux, con un comando in `ebx`:
`EXOS_RB_POWEROFF` / `EXOS_RB_RESTART` / `EXOS_RB_HALT`.

| Comando shell | Effetto |
|---|---|
| `halt` | sincronizza, ferma, **non** spegne — resta a schermo "è ora sicuro spegnere" |
| `poweroff` / `shutdown` | sincronizza, 3 secondi, spegne l'hardware |
| `reboot` | sincronizza e riavvia |

## Spegnimento hardware: cosa funziona davvero e cosa no

Non esiste un'istruzione x86 "spegni il computer": lo spegnimento è una funzione
del chipset. Ci sono tre strade, e ne è implementata una sola.

| Strada | Stato | Perché |
|---|---|---|
| Porte note degli emulatori | ✅ implementata | QEMU (`0x604`), Bochs/QEMU legacy (`0xB004`), VirtualBox (`0x4004`); si scrive il comando ACPI di sleep S5 direttamente. Provate in sequenza: scrivere su una porta non implementata è innocuo |
| ACPI vero | ❌ | richiede un parser RSDP → RSDT → FADT → DSDT per trovare PM1a_CNT e i valori SLP_TYP. Lavoro a sé |
| APM (INT 15h) | ❌ | funzionerebbe sull'hardware d'epoca, ma INT 15h è una chiamata in **real mode**: da protected mode servirebbe tornare in real mode o un monitor virtual-8086 |

**Conseguenza da tenere presente: in QEMU/Bochs/VirtualBox la macchina si spegne
davvero; su un Pentium II reale, con ogni probabilità no** — resterà nel loop di
halt con il messaggio "è ora sicuro spegnere". Non è un fallimento silenzioso: il
sistema è comunque in uno stato sicuro, filesystem sincronizzato, scheduler fermo
e interrupt disabilitati. È esattamente ciò che facevano i PC pre-ATX.

## Verificato, guardando se QEMU esce davvero

Non basta leggere il messaggio a schermo: la prova dello spegnimento è che il
processo QEMU termini. Script in `/tmp/exos/power_test.py`.

| Comando | Risultato |
|---|---|
| `poweroff` | **QEMU esce dopo 3,5 s** (3 s di countdown + avvio), exit code 0 |
| `halt` | QEMU **ancora vivo dopo 30 s**, schermo su "è ora sicuro spegnere" — corretto, `halt` non deve spegnere |
| `reboot` | QEMU esce dopo 0,4 s (con `-no-reboot` il reset termina la VM) |

Nessun kernel panic in nessuno dei tre casi. Sincronizzazione del filesystem
riuscita ovunque (`Sincronizzazione filesystem... ok`).

Il fallback triple-fault di `power_reboot()` **non è stato esercitato**: il reset
via 8042 funziona in QEMU. Resta non verificato.

## Lavori che questa sessione ha reso evidenti

- **Nessun init che riavvii la shell.** Se la shell muore — per un fault o per
  `exit` — il sistema resta vivo ma senza console. `init_reaper_task()` raccoglie
  gli zombie ma non fa supervisione. Ora che un'eccezione ring3 non abbatte più
  il kernel, questo è il limite che si nota per primo.
- **`SYS_REBOOT` non ha controlli di accesso**: qualunque processo può spegnere
  il sistema. Non c'è ancora un concetto di utente o privilegio in EX-OS su cui
  basare un controllo — da rivedere quando esisteranno gli UID.
- **ACPI**: servirebbe comunque, non solo per lo spegnimento su hardware reale.

---

# SESSIONE 2026-07-30 (d) — Identità di sistema in una globale, `ver`/`version`, verboseboot

Richiesta dell'utente: una variabile globale del kernel con l'identità del
sistema, mostrata all'avvio e recuperabile dai programmi; comandi `ver`/`version`
nella shell; nuova opzione `verboseboot` in kernel.cfg (default 1) che con 0
sopprime le informazioni durante caricamento ed esecuzione.

## Versione: `EXOS_VERSION` in `kernel/include/version.h`

```
0.101   ← incrementare di 0.001 a OGNI modifica del kernel
```

La versione precedente del progetto era `0.1`, che in questo schema si legge
`0.100`: la numerazione prosegue da lì, e questa sessione porta a `0.101`.

**È una stringa, non un numero, di proposito**: un float non rappresenta 0.001 in
modo esatto e stamparlo richiederebbe aritmetica in virgola mobile, che questo
kernel non ha (nessuna FPU inizializzata, nessun softfloat). L'incremento è
un'operazione manuale e deliberata — chi tocca il kernel aggiorna quella riga.

## La globale

`kernel/version.c` definisce due `const char[]`:

- `g_os_version` — blocco completo su 4 righe (nome, copyright+email, licenza,
  versione+architettura). È ciò che stampa il banner di boot e ciò che
  restituisce `ver`/`version`.
- `g_os_version_short` — riga singola, per il boot silenzioso.

Composte per **concatenazione di letterali a compile time**: niente heap, niente
filesystem, niente formattazione a runtime. È il motivo per cui si possono
stampare nel banner iniziale, dove nulla di tutto ciò esiste ancora.

## Nuove syscall

| N. | Nome | Argomenti | Cosa fa |
|---|---|---|---|
| 184 | `getenv` | key\*, buf\*, size | legge `[env]` **e** le opzioni scalari fuori da `[env]` |
| 185 | `version` | buf\*, size | copia `g_os_version` |

Entrambe copiano in un buffer utente, mai puntatori interni: stessa regola di
`sys_getcwd`/`sys_readdir`. `sys_version` rifiuta i buffer troppo piccoli invece
di troncare — un'identità tagliata a metà frase sarebbe peggio di un errore.

Wrapper in libc: `getconf()`, `osversion()`, `verboseboot()`. Il primo **non** si
chiama `getenv()` di proposito: la firma è diversa da quella standard (il
chiamante fornisce il buffer) e un nome identico con semantica diversa sarebbe
una trappola.

## La trappola in cui sono caduto, e come l'ho chiusa

Al primo giro funzionava tutto ma `ver` diceva `0.101` e `uname` diceva `0.1`:
avevo **ricreato la doppia fonte di verità** eliminata poche ore prima nella
sessione (c). `ver` legge la globale del kernel; `uname` ed `env` leggono
l'ambiente, che veniva da `[env]` di kernel.cfg.

Risoluzione: **la globale è l'autorità, `[env]` è derivato**. `cfg_load()` inietta
`OSNAME`/`OSVER`/`AUTHOR` da `version.h` — sia prima di aprire il file (così ci
sono anche se manca) sia dopo il parsing (così vincono su un eventuale valore
scritto nel file). Quelle tre chiavi sono state tolte da `kernel.cfg`, con il
commento che spiega perché non vanno rimesse.

> Sembra un passo indietro rispetto alla sessione (c), dove avevo reso `OSNAME`
> configurabile. Non lo è: lì il problema era che i valori fossero **hardcoded in
> cinque punti**. Ora la fonte è una sola, e non può essere il file di
> configurazione perché la versione si incrementa a ogni modifica del *kernel* —
> il file vive sul floppy e non viene ricompilato. Restano configurabili PATH,
> HOME, TERM: quelle sono davvero scelte dell'utente.

## verboseboot

Opzione in `[kernel]` (non in `[env]`: è una scelta di sistema, non una variabile
d'ambiente). Default **1**, impostato nei valori di default *prima* di leggere il
file: se kernel.cfg manca o è illeggibile il sistema deve parlare — un boot muto
che nasconde un fallimento è peggio di uno rumoroso. Qualunque valore diverso da
`0` vale 1, così un refuso non zittisce il sistema.

Con `verboseboot = 0` il silenzio richiede **tre** interventi distinti, perché il
rumore ha tre sorgenti diverse:

| Sorgente | Come si zittisce |
|---|---|
| messaggi klog **futuri** (PASSI 14+, e `SYSCALL spawn`/`ELF:` a ogni comando) | `klog_set_level(LOG_WARN)`: informativi e debug spariscono, **avvisi ed errori no** |
| messaggi klog **già stampati** (PASSI 1-13) | non sopprimibili a posteriori → `vga_clear()` |
| `printf` del driver kbd (processo ring3) | **non passa da klog**: è una `write()` di userspace, il filtro del kernel non la vede nemmeno. Gated con `verboseboot()` di libc |

Quel terzo punto è il tipo di dettaglio che sfugge: al primo test la riga
`kbd: servizio 'kbd' pronto ...` era l'unica sopravvissuta al silenzio.

## Garanzia: errori ed eventi inattesi restano visibili

Richiesta esplicita dell'utente, e c'era un buco reale.

Il filtro di livello già lasciava passare WARN ed ERROR, e i messaggi di fault
(`page_fault_handler`) usano `kprintf` diretto, quindi non sono nemmeno
filtrabili. **Ma `vga_clear()` al PASSO 13c cancellava lo schermo**, e con esso
qualunque problema segnalato durante i PASSI 1-13 — proprio la finestra in cui
non si può ancora sapere che l'utente voleva silenzio.

Due garanzie, implementate in `kernel/arch/x86/kprintf.c`:

1. **Un `LOG_ERROR` è stampato SEMPRE**, qualunque sia il livello configurato,
   anche con un `loglevel = 0` esplicito. Il livello regola quanto il sistema è
   loquace, non se può nascondere i propri guasti: un errore invisibile non è un
   sistema silenzioso, è un sistema che mente.

2. **Ogni ERROR/WARN è registrato in un ring** (`PROB_SLOTS = 8` × 120 caratteri,
   ~1KB statico) **mentre viene emesso, anche se il filtro lo sta nascondendo**.
   Il PASSO 13c, dopo aver pulito lo schermo, ripropone il registro sotto
   l'intestazione `Avvio silenzioso: N problema/i durante l'inizializzazione`.

Se i problemi sono più degli slot, la ristampa lo **dichiara**
(`(N problemi precedenti non conservati, vedi console seriale)`) invece di
mostrare una lista troncata in silenzio — che sarebbe esattamente il tipo di
bugia che questo meccanismo esiste per evitare.

### Come è stato implementato senza duplicare il formattatore

Tutte le funzioni di `kprintf.c` scrivevano con `vga_putchar`/`vga_puts` sparsi
in 13 punti. Sono stati incanalati in un unico `kout()`, che devia su un buffer
quando `g_capture` è impostato. `klog()` per un problema formatta **una volta
sola** dentro lo slot del ring e poi stampa quel testo già pronto, invece di
eseguire due volte il formattatore sulla stessa `va_list`.

### Verificato in tre scenari

- **Problema prima della pulizia**: `[WARN] PMM: nessuna mappa E820` (presente a
  ogni boot in QEMU) sopravvive al `vga_clear()` e compare sotto l'intestazione.
- **Problema dopo la pulizia**: con `/dev/kbd.drv` rimosso dall'immagine, i tre
  messaggi del PASSO 14b/14c (`[ERROR] ELF: file non trovato`, driver saltato,
  ripiego sulla tastiera in-kernel) vengono stampati dal vivo, e il sistema resta
  usabile.
- **Ring saturo**: iniettati temporaneamente 11 avvisi al boot. Con 12 problemi
  totali il registro ha mostrato gli 8 più recenti **in ordine cronologico**
  (wraparound del ring corretto) e ha dichiarato i 4 scartati.

**Perché serve `vga_clear()`**: i PASSI 1-13 vengono stampati *prima* che
kernel.cfg sia leggibile — serve FAT12, che parte al PASSO 13. Non c'è modo di
sapere prima che l'utente voleva silenzio. Il nuovo **PASSO 13c** esiste apposta
per questo ed è documentato nel codice, così nessuno prova a "sistemarlo"
spostando la lettura della configurazione più in alto (non si può: dipende da
paginazione, heap, interrupt e filesystem).

La console seriale continua a ricevere tutto anche in modalità silenziosa: il
mirroring in `vga.c` è a monte del filtro di livello. Il debug non si perde.

## Cosa si vede, nelle due modalità

`verboseboot = 1` (default): banner iniziale con l'identità completa, ~140 righe
di log, banner finale, dump dello scheduler.

`verboseboot = 0`: schermo pulito, una riga di identità, prompt. Nient'altro.

```
EX OS 0.101 (Extensible Operating System) - Copyright (C) 2025 Graziano Falcone - GPL 2.0

ex-os:/> ver
EX OS (Extensible Operating System)
Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
Licenza: GPL 2.0 (GNU General Public License)
Versione: 0.101 (x86 32-bit)
ex-os:/> hello
Ciao da /bin/hello!
```

L'identità su una riga resta anche in modalità silenziosa: `verboseboot` spegne la
diagnostica, non l'identità di ciò che si è avviato. Scelta deliberata, dato che
la richiesta era sia "mostrata in avvio" sia "silenzioso".

## Nota per gli script di test

`tools/qemu_drive.py` aspettava il marker `sblocco IRQ0` per capire che la shell
era pronta — ma è un `klog` INFO e **sparisce con verboseboot=0**, quindi lo
script andava in timeout senza digitare nulla. Ora accetta anche il prompt
`ex-os`, presente in entrambe le modalità.

## File toccati

`kernel/include/version.h` e `kernel/version.c` (nuovi), `kernel/fs/cfg.c`,
`kernel/include/cfg.h`, `kernel/kernel_main.c`, `kernel/syscall/syscall.c`,
`kernel/syscall/syscall_impl.c`, `kernel/include/syscall.h`, `lib/libc.c`,
`lib/include/libc.h`, `drivers/kbd/kbd.c`, `bin/sh/shell.c`, `boot/kernel.cfg`,
`Makefile`, `tools/qemu_drive.py`.

---

# SESSIONE 2026-07-30 (c) — La sezione [env] di kernel.cfg era decorativa

Segnalazione dell'utente: il nome del sistema è in `/boot/kernel.cfg` ma non
compare né al boot del kernel né nella shell.

## La diagnosi

`cfg_getenv()` **non aveva un solo chiamante in tutto il progetto**. La sezione
`[env]` veniva letta, memorizzata in `KernelConfig`, perfino stampata nel log di
boot (`env[3]: OSNAME = EX-OS`) — e poi ignorata. Il nome mostrato era hardcoded
in quattro punti indipendenti:

| Punto | Cosa mostrava |
|---|---|
| `print_boot_banner()` in kernel_main.c | `"ExOS Kernel v0.1"` — e si noti: **"ExOS"**, l'unico posto in tutto il progetto a scriverlo così invece di "EX-OS" |
| banner finale in kernel_main.c | `"EX-OS — Extensible Operating System v0.1"` |
| `env_init()` in shell.c | ri-hardcodava `OSNAME`/`OSVER`/`AUTHOR`, cioè una seconda copia degli stessi valori |
| `cmd_uname()` in shell.c | stringhe letterali — non usava nemmeno le variabili che `env_init` aveva appena impostato |

Due copie della stessa verità, destinate a divergere al primo che avesse
modificato `kernel.cfg` aspettandosi un effetto. Che è esattamente quello che è
successo.

## Il vincolo che NON è un bug

`print_boot_banner()` **non può** leggere da `kernel.cfg`, e non è una
dimenticanza: gira come primissima cosa in `kernel_main`, mentre la
configurazione è caricata al PASSO 13b — dopo FAT12 (PASSO 13), che a sua volta
richiede paginazione, heap e interrupt. In quel punto non esiste ancora un
filesystem da cui leggere. La stringa lì resta necessariamente nel codice; è
documentato nel commento sopra la funzione perché nessuno ci ritorni sopra.

Il banner *finale*, in fondo a `kernel_main`, gira invece dopo `cfg_load()` e ora
usa i valori del file.

## Cosa è cambiato

- **Nuova syscall `SYS_GETENV` (184)** — `getenv(key, buf, size)`: legge una
  variabile di `[env]`. Copia in un buffer utente, mai un puntatore interno
  esposto: stessa regola già seguita da `sys_getcwd` e `sys_readdir`.
- **Banner finale del kernel**: nome, versione e autore da `cfg_getenv()`, con
  fallback se il file manca o la sezione è incompleta — il kernel deve avviarsi
  comunque.
- **`env_init()` della shell**: i valori arrivano dal kernel via `SYS_GETENV`.
  La tabella `ENV_INHERITED` elenca le chiavi ereditate con un default accanto,
  usato **solo** se il kernel non ha quella chiave. `SHELL=/bin/sh` resta locale:
  è la shell a sapere cosa sta eseguendo, e `[boot] shell=` nel file ha un altro
  scopo (chi lanciare), non è una variabile d'ambiente.
- **`print_banner()` e `cmd_uname()` della shell**: leggono da `env_get()`.
- **`print_boot_banner()`**: allineato a `EX-OS` (era l'unico "ExOS"), e tolto il
  riferimento a "Fase 1b" ormai vecchio di tre fasi.

## Verificato cambiando davvero il file

Non basta che compili: il punto era che modificare `kernel.cfg` avesse un
effetto. Con `OSNAME = PROVA-OS`, `OSVER = 9.9`, `AUTHOR = Test Autore`:

```
   PROVA-OS — Extensible Operating System v9.9      ← banner kernel
   Copyright (C) 2025 Test Autore
PROVA-OS version 9.9 (x86 32-bit) — ... Test Autore ← uname nella shell
```

Tutti e tre i punti seguono il file. Valori originali poi ripristinati.

## Nota per chi aggiungerà variabili

Aggiungere una chiave a `[env]` in `kernel.cfg` **non** la rende visibile alla
shell da sola: va aggiunta anche a `ENV_INHERITED` in `bin/sh/shell.c`. È una
scelta deliberata — la shell decide cosa ereditare invece di assorbire tutto
ciò che il kernel espone — ma è il tipo di dettaglio che si dimentica.

---

# SESSIONE 2026-07-30 (b) — FDC sincronizzato su IRQ6, e tre bug latenti da hardware reale

Seguito diretto della sessione qui sotto. Nato come "punto 2 della lista"
(sincronizzare l'FDC su IRQ6 invece che a polling), ha scoperto tre difetti che
in QEMU non si vedono e su un drive vero causerebbero letture sbagliate
**senza errore I/O segnalato** — la stessa firma del bug del Pentium II di
giugno.

## Perché non si è partiti invece dal driver floppy in ring3

Era il passo previsto, ed è stato accantonato per una ragione trovata leggendo
il codice: `fat12_read_sector` fa il trasferimento PIO byte-per-byte con
`interrupts_disable()` attorno al loop, e **un processo ring3 non può fare
`cli`**. Verrebbe preempato da IRQ0 in mezzo al settore.

In QEMU passerebbe (FDC emulato, nessun timing reale). Su hardware vero no: a
500 kbit/s il FIFO va servito entro microsecondi, un tick da 10 ms è tre ordini
di grandezza oltre il margine, e il risultato è un **overrun** (ST1 bit 4).
Sarebbe stato esattamente il tipo di bug che questo progetto ha già pagato:
funziona in emulazione, si rompe sul ferro.

Le tre strade possibili sono descritte in fondo. Nessuna è stata imboccata:
IRQ6 in-kernel viene prima perché è prerequisito di tutte e tre e non ha
rischi.

## Bug latenti trovati e corretti

### 1. ⛔ L'attesa dopo RECALIBRATE/SEEK era di 15 ms — troppo pochi per un drive vero

`fdc_recalibrate()` e `fdc_seek()` inviavano il comando, aspettavano
`fdc_delay_ms(15)` e poi leggevano ST0/PCN con SENSE INTERRUPT.

15 ms bastano in emulazione, dove il seek è istantaneo. Su un drive reale un
recalibrate a stroke pieno muove la testina di 80 tracce e richiede **centinaia
di millisecondi**. Passati i 15 ms, SENSE INTERRUPT veniva letto con il seek
ancora in corso: `pcn == 0` falliva, e dopo 3 tentativi (45 ms in tutto, ancora
troppo pochi) `fdc_recalibrate` **usciva in silenzio** lasciando la testina dove
capitava.

**Fix**: attesa vera dell'IRQ6 di fine comando (`fdc_wait_irq`), con timeout
1000 ms per recalibrate e 500 ms per seek. Il timeout non è fatale: logga un
warning e prosegue, invece di appendere il boot su hardware senza controller.

Il controllo di esito è anche più severo di prima: non basta `pcn == 0` (è 0
anche se il comando non è mai partito), si verificano ST0 bit 5 (SE, Seek End) e
bit 4 (EC, Equipment Check).

### 2. ⛔ Il comando READ non muoveva la testina — funzionava solo grazie all'emulatore

`fat12_read_sector` inviava il comando READ con i parametri C/H/S **senza mai
fare un SEEK**. `fdc_seek()` esisteva ma non era chiamata da nessuno (era la
causa del warning `fdc_seek defined but not used`, presente da mesi).

Il comando READ non muove la testina da solo se l'implicit seek non è
abilitato — e non lo è, `CONFIGURE` con EIS non viene mai inviato. Il codice si
affidava quindi, implicitamente, al fatto che QEMU facesse il seek per conto
suo. **Un controller reale legge la traccia su cui la testina si trova
fisicamente**, ignorando il cilindro passato nei parametri: se non
corrispondono, escono i dati di un'altra traccia o un errore di ID address
mark. Il commento sopra `fdc_recalibrate` descriveva già esattamente questo
rischio — ma lo gestiva solo all'init, una volta, non a ogni accesso.

**Fix**: `fdc_seek_if_needed(cyl, head)` prima di ogni comando READ/WRITE,
fuori dalla sezione critica (l'attesa dell'IRQ6 richiede interrupt abilitati).

**Ottimizzazione necessaria, non gratuita**: un SEEK + 15 ms di settle per ogni
settore sarebbe stato un costo enorme. `g_fdc_cyl` ricorda il cilindro corrente
e il seek si fa solo quando cambia davvero: su un floppy 1.44MB un cilindro
contiene 36 settori (18 × 2 testine), quindi una lettura sequenziale sposta la
testina una volta ogni 36 settori. La variabile è invalidata (-1) a ogni reset,
seek fallito o errore di trasferimento: meglio un seek in più che leggere la
traccia sbagliata credendo di sapere dove siamo.

**Misurato, non stimato**: boot fino al prompt **19,7 s** con la cache della
posizione, **20,9 s** disattivandola (`if (0 && ...)`). La differenza di 1,2 s
corrisponde a ~80 settori letti al boot che pagherebbero ciascuno i 15 ms di
settle — conferma sia che i seek vengono davvero emessi, sia che la cache li
elimina quasi tutti.

### 3. ⛔ `fat12_write_sector` non aveva la protezione `cli` che la lettura aveva

Il path di lettura ha `interrupts_disable()` attorno a comando+dati+status, con
un commento che ne spiega la necessità; perfino il flush della cache dirty
*dentro* `fat12_read_sector` ce l'ha. **La scrittura era rimasta indietro.**

Stesso identico ragionamento: se IRQ0 scatta fra due `fdc_send_byte()`, il
controller (timeout interno ~500 µs per il byte di comando successivo)
abbandona il comando; da lì `fdc_wait_ready()` non vede più RQM e i 512 byte del
settore vengono scartati uno per uno **senza che nulla lo segnali**. La
scrittura "riesce" ma sul disco non arriva niente. Non si manifesta durante il
boot (scheduler non ancora attivo), si manifesta appena un processo scrive un
file a sistema avviato.

**Fix**: `cli` attorno alla fase comando+dati+status, più il controllo
dell'esito della status phase prima di usare ST0 (stessa correzione già
applicata a giugno alla lettura: su fallimento ST0 restava non inizializzato).

### 4. Residuo della bonifica di giugno: due loop di NOP sopravvissuti

Il reset del controller in `fat12_init` usava ancora
`for (d = 0; d < 100000; d++) nop;` — due volte. Sono sfuggiti alla bonifica di
giugno che aveva sostituito gli altri tre con `fdc_delay_ms()`. Stesso difetto:
durata dipendente dalla velocità della CPU. Ora sono attese in tempo reale, e
l'uscita dal reset attende il proprio IRQ6.

## Perché IRQ6 NON è usato per READ/WRITE — non è una dimenticanza

Il 82077AA genera IRQ6 in due situazioni molto diverse:

- **fine comando senza fase di risultato** (RECALIBRATE, SEEK, reset):
  l'interrupt è l'*unico* segnale di completamento, e va seguito da SENSE
  INTERRUPT per leggere ST0/PCN e disarmare il controller. Questi sono i casi
  in cui aspettare l'IRQ è corretto ed è ciò che ora facciamo.
- **fase di esecuzione di READ/WRITE in PIO** (SPECIFY con NDMA=1, come qui):
  il controller alza INT a **ogni byte pronto**, non a fine comando.
  Aspettare "l'IRQ6" lì non significherebbe "settore trasferito" ma "c'è un
  byte" — inutile, dato che lo stesso segnale è già leggibile da MSR/RQM, che è
  quello che il loop di trasferimento fa.

Per READ/WRITE il polling di MSR **è** il modello giusto in PIO. Chi in futuro
volesse "completare il lavoro" sostituendolo con un'attesa di IRQ6 romperebbe
il driver.

## Verificato

Build a **zero warning** (è sparito anche `fdc_seek defined but not used`, che
c'era da mesi). Boot pulito, nessun timeout IRQ6, nessun
`RECALIBRATE non confermato`. Sequenza di comandi (`ls`, `hello`, `cd /bin`,
`ls`, `pid`, `hello`, `cat /boot/kernel.cfg`) tutta corretta, PIC pulito
(`irr=00 imr=bc isr=00`).

**Non verificato su hardware reale** — è il punto centrale: questi tre bug sono
proprio quelli che l'emulazione non mostra. Il valore del lavoro si misura solo
sul Pentium II. Se lì qualcosa non va, i log da guardare sono i nuovi warning
`timeout IRQ6 su ...` e `SEEK non confermato`, che prima non esistevano e che
distinguono un problema di timing da uno di posizionamento.

## Il driver floppy in ring3: le tre strade, per quando si riprenderà

1. **DMA** — elimina il problema alla radice (la CPU non serve il
   trasferimento, la preemption è innocua). Richiede però: `sys_ioport_bind`
   oggi accetta **un solo range contiguo** e servirebbero anche le porte del
   controller DMA (0x00–0x0F, 0x81); più una syscall nuova per ottenere un
   buffer fisicamente contiguo sotto i 16MB di cui il driver conosca
   l'indirizzo **fisico**. È l'architettura corretta e il lavoro più lungo.
2. **PIO + sezione non-preemptible** — flag nel PCB che `sched_irq0_handler`
   controlla per saltare il context switch, con timeout a tick perché un driver
   buggy non appenda il sistema. Strada più corta, ma concede a un processo
   utente di bloccare lo scheduler: compromesso sulla premessa dell'isolamento.
3. **Lasciare il floppy in-kernel.** Legittima: il kernel deve comunque
   conservare FAT12/FDC per il bootstrap (il driver del floppy va letto *dal*
   floppy), quindi la migrazione aggiunge codice invece di toglierne.

Nota già emersa con kbd e ancora valida: il TTY può fare da client IPC perché
`drv_read()` gira **nel contesto del processo che legge**. `elf_load()` no — è
chiamato anche da `kernel_main` al boot, quando `proc_get_current()` è il task
idle. Prima di partire va deciso se il client del servizio floppy sarà il
kernel o un vero VFS in userspace.

---

# SESSIONE 2026-07-30 (a) — Il driver tastiera gira in ring3

Ambiente di test: identico alla sessione precedente (QEMU 8.2.2
`qemu-system-i386`, SeaBIOS 1.16.3, floppy FAT12 1.44MB, `gcc -m32` nativo).

## TL;DR — stato attuale

**`/dev/kbd.drv` è il primo driver di EX-OS che gira davvero in userspace.**
È un processo ring3 con la propria page directory, che non esegue nessuna
istruzione privilegiata: prende gli scancode con `SYS_IOPORT_IN`, riceve gli
IRQ1 come messaggi IPC grazie a `SYS_IRQ_BIND`, e consegna le righe digitate al
TTY del kernel con `SYS_IPC_SEND`. Il "problema aperto" della sessione
precedente — migrazione dei driver a metà, nessun driver caricato al boot — è
chiuso per la tastiera. Il floppy resta in-kernel (vedi in fondo).

Verificato in QEMU: 12 comandi consecutivi (`uname`, `hello`, `ls`, `pid`,
`echo Ciao Mondo`, `cd /bin`, `cat /boot/kernel.cfg`, …), line editing con
Backspace, type-ahead, maiuscole con Shift, PIC pulito a fine sequenza
(`irr=00 imr=bc isr=00`) e CPU nell'idle loop (`HLT=1`).

## L'architettura, in breve

```
    IRQ1 (hardware)
        |
        v
  irq_handler()  isr.c        ← EOI, poi: nessun handler kernel per IRQ1,
        |                       quindi consegna al proprietario ring3
        v
  ipc_notify_irq(pid_kbd)     ← messaggio in mailbox, sblocca il driver
        |
        v
  ============ ring 3 ==============================================
  /dev/kbd.drv  (drivers/kbd/kbd.c)
     ipc_recv()  ←  unico punto di attesa del driver
     ioport_in(0x64/0x60)     ← drena il KBC finché OBF è alto
     traduzione scancode → ASCII, line discipline, Backspace
     write(1, ...)            ← eco a video (passa dal TTY del kernel)
     ipc_send(pid_client, KBD_MSG_LINE, riga)   ← su Invio
  ==================================================================
        |
        v
  drv_read()  drivers/tty/tty.c   ← gira nel contesto della SHELL:
        |                            ipc_send(READLINE) + ipc_recv()
        v
  sys_read(fd=0) → shell
```

Il punto non ovvio: **il TTY in-kernel non fa da intermediario di dati**.
`drv_read()` è chiamata da `sys_read()`, quindi gira già nel contesto del
processo che legge — `proc_get_current()` è la shell. Le `ipc_send`/`ipc_recv`
che esegue operano sulla mailbox della shell, non su una del kernel: il kernel
presta solo il proprio codice. È per questo che non è servito inventare un
meccanismo nuovo per far parlare un processo ring0-less con un driver ring3.

## File toccati

| File | Cosa |
|---|---|
| `drivers/kbd/kbd_proto.h` | **nuovo** — protocollo IPC condiviso fra le due sponde (`KBD_MSG_READLINE`/`KBD_MSG_LINE`, nome servizio, `KBD_LINE_MAX`). Nessuna dipendenza: né header del kernel né libc. |
| `drivers/kbd/kbd.c` | riscritto: da modulo ET_DYN kernel-space a programma ring3 con `main()` e loop di servizio |
| `drivers/kbd/kbd.ld` | da ET_DYN (ORG 0, `.dynsym`/`.rel.*`, `ENTRY(drv_init)`) a ET_EXEC a `0x08000000` con `ENTRY(_start)` — copia dello schema di `bin/ls/ls.ld` |
| `drivers/tty/tty.c`/`.h` | sorgente input selezionabile (`tty_set_input_source`); `drv_read` è un dispatcher fra `tty_read_ipc` e il percorso storico `tty_read_internal` |
| `kernel/kernel_main.c` | PASSO 14c (scelta della sorgente input); driver adottati da init; helper `str_equal` |
| `boot/kernel.cfg` | `modules = kbd`; rimossi `tty` (mai esistito) e `floppy` (ancora ET_DYN) |
| `Makefile` | regola `kbd.drv` statica ET_EXEC (kbd.c + libc.c + start.S); `LIBC_START`; `-I drivers/kbd` nei CFLAGS del kernel |

## Decisioni di progetto e perché

### 1. La line discipline sta nel driver, non nel kernel

Eco a video, Backspace e assemblaggio della riga sono nel driver ring3. Il TTY
del kernel riceve righe già complete. L'alternativa (driver che consegna
caratteri grezzi, kernel che fa l'editing) avrebbe lasciato metà della logica
di terminale in ring0 senza guadagno: il Backspace deve poter modificare
caratteri **non ancora consegnati** al lettore, quindi deve stare dalla stessa
parte del buffer di riga.

L'eco passa da `write(1, ...)`: il driver ha fd 0/1/2 come qualunque processo
(li imposta `proc_create`), e il suo stdout è il TTY del kernel. Non serve
mappargli la memoria VGA.

### 2. Nel driver non esiste più nessun contesto interrupt

Il vecchio `kbd.c` aveva un `kbd_irq1_handler()` che girava in contesto IRQ e
condivideva ring buffer e `g_waiting_pid` con `drv_read()`. Tutte le race di
quel modello (lost wakeup, necessità di `volatile`, sezioni critiche) sono
**strutturalmente assenti** ora: c'è un unico flusso di esecuzione, il loop di
`main()`. Nessuna variabile del file è toccata da due contesti.

### 3. `kbd_drain()` è un loop, non una lettura singola

La notifica IPC dice "c'è lavoro", non "c'è esattamente un byte". Fra l'IRQ e
il momento in cui lo scheduler fa girare il driver passa tempo indefinito, e
`ipc_notify_irq()` **scarta** la notifica se la mailbox è piena (scelta
deliberata, documentata in `kernel/ipc/ipc.c`). Se il driver leggesse un solo
byte per notifica, una notifica persa lascerebbe un byte in `0x60` per sempre:
con OBF alto il KBC non genera più fronti su IRQ1 e la tastiera morirebbe del
tutto. Il loop drena finché OBF è basso, quindi una notifica persa non ha
conseguenze.

Stessa ragione per la drenata subito dopo `irq_bind()`: un tasto premuto fra
`kbd_hw_init()` e il bind lascerebbe OBF alto senza aver prodotto un fronte
utile.

### 4. Ordine di `ioport_bind` / `kbd_hw_init` / `irq_bind`

`irq_bind()` smaschera l'IRQ nel PIC, quindi va **dopo** `kbd_hw_init()`: l'init
genera traffico sul KBC (ACK del self-test, ACK di enable-scan) che non
vogliamo scambiare per input dell'utente.

### 5. I timeout di polling sono ~50x più corti di prima

Nel vecchio driver kernel-space `while (... ) io_delay()` girava 100000 volte:
erano `in`/`out` dirette. Qui **ogni lettura è una syscall**: gli stessi numeri
significherebbero secondi di CPU bruciati. `KBC_POLL_MAX` è 2000, ampiamente
sufficiente (il KBC risponde in decine di microsecondi) e non appende il boot
se il controller è morto.

### 6. Fallback in-kernel conservato — non è codice morto

`tty.c` conserva tabelle scancode, handler IRQ1 e line discipline sotto
`TTY_INPUT_INTERNAL`. Serve in due casi reali:

- `/dev/kbd.drv` assente o non caricabile → `kernel_main` (PASSO 14c) sceglie
  la sorgente interna e il sistema ha comunque una console;
- il driver muore a runtime → `tty_read_ipc()` se ne accorge (`ipc_send`
  ritorna `-ESRCH`, o `ipc_lookup` non trova più il nome) e chiama
  `tty_set_input_source(TTY_INPUT_INTERNAL)` da sé.

Il fallback è stato **testato**, non solo scritto: cancellando `/dev/kbd.drv`
dall'immagine con `mdel` il boot logga `[PASSO 14c] Tastiera: driver ring3
assente` e la shell resta pienamente usabile.

Nel passaggio al fallback il TTY ri-drena `0x60`: il driver morto può aver
lasciato un byte non letto, e senza quella drenata la tastiera sembrerebbe
rotta anche dopo il ripiego.

### 7. Le due strade si escludono a vicenda — attenzione

`irq_handler()` (`kernel/arch/x86/isr.c`) consulta **prima** `irq_handlers[]`
(handler kernel) e solo se è vuoto consegna la notifica IPC al proprietario
ring3. Quindi registrare l'handler IRQ1 interno "per sicurezza" **affama
completamente** il driver ring3: non vedrebbe un solo scancode.

Per questo `drv_init()` (PASSO 14) non registra più nulla e la scelta è
rimandata al PASSO 14c, quando l'esito del caricamento è noto. Se in futuro
qualcuno "ripristina" la `irq_register_handler(1, ...)` dentro `drv_init()`
pensando di renderlo più robusto, romperà la tastiera ring3 in modo
silenzioso.

### 8. I driver sono adottati da init alla creazione

`proc_create` assegna come `ppid` il processo corrente, che al PASSO 14b è il
task **idle**. Idle non muore mai e non chiama mai `waitpid()`, e `proc_exit()`
ri-genitorializza a init solo gli orfani il cui padre è già morto — condizione
che con idle come padre non si verifica mai. Un driver che si schianta
resterebbe quindi ZOMBIE per sempre.

`kernel_main` ora imposta `drv_proc->ppid = g_init_task->pid` subito dopo la
creazione. Non è solo igiene di memoria: `proc_reap_zombie()` chiama
`irq_unbind_process()` e `ipc_cleanup_process()`, quindi senza l'adozione, dopo
un crash del driver **nessun altro processo potrebbe più registrarsi con quel
nome di servizio né rivendicare IRQ1** — il fallback funzionerebbe ma il driver
non sarebbe mai riavviabile.

## Trappole incontrate (non ripercorrere)

- **Lo script di test che manda `\b` letterale.** Stessa identica trappola già
  documentata per `\n` nella sessione precedente: in bash `"lx\bs"` passa
  backslash + `b`, non un backspace. Serve `$'lx\bs'`. Se un test di line
  editing "non cancella niente", controllare prima questo.
- **Il primo boot di prova sembrava bloccato durante `elf_load('/bin/sh')`**:
  era solo il timeout di 15s dello script, troppo corto ora che al boot si
  carica davvero un driver dal floppy (13KB) *prima* della shell. Con 60s
  arriva regolarmente al prompt. Non è una regressione: il caricamento del
  floppy è lento per via dei delay del FDC, non per il driver.
- **`-I drivers/kbd` va nei CFLAGS come riga propria, non in coda con un
  commento `#`**: make tronca la riga al `#`, e in un assegnamento
  multi-riga il risultato è silenziosamente diverso da quello che si legge.

## Limiti noti di questa implementazione

- **Un solo lettore alla volta.** Il driver tiene un unico `g_reader_pid`: una
  richiesta che arriva mentre un'altra è pendente sostituisce la precedente
  (e il vecchio richiedente resta bloccato in `ipc_recv`). Oggi non capita —
  solo il TTY chiede righe, e in modo sincrono per un processo alla volta — ma
  diventerà reale il giorno in cui ci saranno più terminali.
- **`tty_read_ipc` scarta i messaggi che non sono `KBD_MSG_LINE`.** Se un
  giorno qualcuno manderà IPC alla shell per altri motivi, quei messaggi
  verranno persi (con un `[WARN]`). Servirà una vera demultiplazione per tipo.
- **Nessun `ioctl` sul servizio kbd**: modalità raw, flush e controllo LED
  esistevano come `drv_ioctl` nel vecchio driver e non sono state riportate nel
  protocollo IPC (i LED sono gestiti internamente per il CapsLock). Vanno
  aggiunti come nuovi tipi di messaggio quando serviranno.
- **Il claim I/O è un solo range contiguo per processo** (`0x60..0x64` qui): è
  un limite di `sys_ioport_bind`, non del driver.

## Cosa resta della migrazione — il floppy

`drivers/floppy/floppy.c` è ancora un modulo ET_DYN scritto contro i simboli
del kernel, **non è più elencato in `kernel.cfg`** (prima produceva un `[WARN]`
a ogni boot) e l'accesso al floppy resta fatto dal FAT12/FDC interno al kernel.
Viene ancora compilato e copiato sul floppy da `make all`, come promemoria.

Per migrarlo servono gli stessi passi fatti qui, più uno specifico:

1. riscriverlo contro `SYS_IOPORT_*` / `SYS_IRQ_BIND` / `SYS_IPC_*`;
2. `_start` + link ET_EXEC statico (regola Makefile: copiare quella di `kbd.drv`);
3. loop di servizio IPC al posto delle `drv_*`;
4. **bootstrap**: il driver del floppy va letto *dal* floppy. Il kernel deve
   quindi conservare il proprio FAT12/FDC minimo almeno per caricarlo — la
   migrazione del floppy non elimina codice dal kernel, lo affianca.

Nota progettuale emersa lavorando su kbd: il floppy è un caso più difficile
anche perché il TTY in-kernel legge dal servizio kbd *nel contesto del
chiamante*, e lo stesso trucco non si applica a `elf_load()`, che gira nel
contesto di chi fa `spawn` ma è chiamato anche da `kernel_main` al boot, quando
`proc_get_current()` è il task idle. Prima di partire, decidere se il client
del servizio floppy sarà il kernel (come qui) o un vero VFS in userspace.

## Comandi di build/test

```bash
make all

# Boot con log seriale completo:
qemu-system-i386 -drive file=dist/floppy.img,format=raw,if=floppy \
  -m 32M -boot a -display none -serial file:/tmp/serial.txt -no-reboot

# Marker attesi nel log, in ordine:
#   [PASSO 14] TTY OK (output VGA)
#   [PASSO 14b] Driver 'kbd' (/dev/kbd.drv) avviato: PID=3 entry=0x08000000
#   [PASSO 14c] Tastiera: driver ring3 'kbd'
#   SYSCALL ipc_register('kbd') PID=3 -> 0
#   SYSCALL ioport_bind(base=0x60, count=5) PID=3
#   SYSCALL irq_bind(irq=1) PID=3 -> 0
#   kbd: servizio 'kbd' pronto (IRQ1, porte 0x60-0x64, PID 3)

# Test del fallback in-kernel (driver assente):
cp dist/floppy.img /tmp/nokbd.img && mdel -i /tmp/nokbd.img ::/dev/kbd.drv
EXOS_IMG=/tmp/nokbd.img python3 tools/qemu_drive.py "ls" "pid"
# atteso: [PASSO 14c] Tastiera: driver ring3 assente, ripiego sull'handler IRQ1 in-kernel
```

**`tools/qemu_drive.py` è ora versionato** (le sessioni precedenti tenevano
questi script in `/tmp` e li riscrivevano ogni volta). Pilota la tastiera via
monitor QEMU su socket unix, raccoglie la seriale e stampa `info pic` /
`info registers` alla fine:

```bash
python3 tools/qemu_drive.py "uname" "hello" "cd /bin" "ls"

# Type-ahead: "@0" manda il comando dopo senza aspettare il precedente
python3 tools/qemu_drive.py "hello@0" "pid@0" "pwd"

# Line editing: usare $'...' — vedi la trappola dei backslash qui sopra
python3 tools/qemu_drive.py $'lx\bs'
```

Il boot completo fino al prompt richiede **oltre 15 secondi** in QEMU (motore
FDC emulato + due ELF caricati dal floppy): tarare i timeout degli script di
test di conseguenza.

---

# SESSIONE 2026-07-29 — Deadlock PIC: le applicazioni lanciate dalla shell si bloccavano

Ambiente di test: QEMU 8.2.2 (`qemu-system-i386`), SeaBIOS 1.16.3, floppy FAT12
1.44MB (`dist/floppy.img`), build con `make all` (toolchain: `gcc -m32` nativo,
**non** il cross-compiler `i686-elf-*`, che in questo ambiente non è installato —
il Makefile usa già `CC := gcc` con `-m32`).

## TL;DR — stato attuale

Il sistema **funziona end-to-end**: boot → kernel → shell → esecuzione di
programmi esterni (`hello`, `ls`, `cat`) con ritorno al prompt. Testati 12
comandi consecutivi senza blocchi, PID riciclati correttamente, PIC in stato
pulito (`irr=00 isr=00`).

Il sintomo riportato — "le applicazioni eseguite dalla shell si bloccano" — era
un **deadlock del PIC 8259** causato dall'invio dell'EOI *dopo* il context
switch. Dettagli sotto.

## Bug trovati e corretti in questa sessione

### 1. ⛔ DEADLOCK PRINCIPALE — EOI inviato dopo il context switch

**File**: `kernel/arch/x86/isr.c`, funzione `irq_handler()`.

`irq_handler()` inviava `pic_send_eoi(irq)` come **ultima** istruzione, dopo
aver chiamato l'handler registrato. Ma l'handler di IRQ0
(`sched_irq0_handler` in `kernel/sched/sched.c`) può chiamare
`sched_switch_to()` → `context_switch()`, che **cambia lo stack kernel e non
ritorna**: riprende l'esecuzione di un altro processo. La riga
`pic_send_eoi(irq)` restava quindi non eseguita fino a che il processo
preemptato non veniva rischedulato.

Catena esatta del deadlock:

```
IRQ0 (timer) → irq_handler → sched_irq0_handler → sched_switch_to(shell)
             → context_switch  ✗ NON RITORNA → pic_send_eoi(0) MAI ESEGUITO
                                                (PIC: ISR bit 0 = 1, IRQ0 In-Service)
shell riprende → spawn("/bin/hello") → sys_spawn → elf_load
             → fat12_read_sector → fdc_motor_on → fdc_delay_ms(300)
             → `hlt` in attesa che g_ticks avanzi
                ✗ il PIC non consegna più IRQ0 (in-service blocca IRQ0 e
                  tutti gli IRQ di priorità inferiore) → g_ticks non avanza mai
             → SISTEMA CONGELATO
```

**Prova diagnostica** (QEMU monitor, al momento del blocco):

```
pic0: irr=03 imr=bc isr=01     ← isr=01: IRQ0 In-Service, EOI mai inviato
                                  irr=03: IRQ0 e IRQ1 pendenti ma bloccati
EIP=00106781 EFL=00000287 CPL=0 HLT=1   ← dentro fdc_motor_on, IF=1, halted
```

**Fix**: spostare `pic_send_eoi(irq)` **prima** del dispatch dell'handler. È
sicuro perché gli IRQ entrano da interrupt gate (IF=0 per tutta la durata
dell'handler, vedi `irq_common_stub` in `isr_stubs.asm`), quindi non è
possibile un rientro dello stesso IRQ prima dell'`iret` finale.

> **Regola da ricordare**: in questo kernel qualunque handler di interrupt può
> non ritornare (context switch cooperativo via stack switch). Tutto ciò che
> deve accadere "a fine interrupt" va fatto **prima** di chiamare codice che
> possa schedulare.

### 2. `printf()` di libc ignorava flag e larghezza di campo

**File**: `lib/libc.c`.

Non c'era parsing di `-`, `0` e della larghezza. Su `"%-12s"` il `-` finiva nel
ramo `default`, che stampava `%-` **senza consumare l'argomento variadico**;
`12s` veniva stampato come testo e lo specificatore successivo leggeva
l'argomento sbagliato. Sintomo: `ls` stampava letteralmente
`%-12s 3221219808` (il numero era il *puntatore* al nome del file, letto da
`%u` al posto della dimensione).

**Fix**: riscritta `printf()` con flag `-`/`0`, larghezza minima di campo, e
aggiunti `%o` e `%p`. Helper interni `pf_pad()` e `pf_utoa()`.
Ora `ls` stampa correttamente `LOADER.BIN   583` / `KERNEL.BIN   65824`.

### 3. Makefile: `/bin/ls` non veniva mai ricompilato al cambiare di `libc.c`

**File**: `Makefile`.

`LIBC_SRC := lib/libc.c` era definita **dopo** la regola `$(LS_BIN): ... $(LIBC_SRC) ...`
che la usa come prerequisito. Make espande i prerequisiti nel momento in cui
legge la regola, quindi `$(LIBC_SRC)` si espandeva a **stringa vuota**:
`ls` non dipendeva da `libc.c` e sul floppy finiva un binario vecchio.

Questo bug è insidioso perché fa sembrare che un fix a `libc.c` "non abbia
effetto". **Se un fix a libc sembra non applicarsi, controllare prima l'mtime
di `build/bin/ls`.**

**Fix**: definizioni `LIBC_SRC`/`LIBC_SO`/`LIBC_LD` spostate prima della regola
di `$(LS_BIN)`.

### 4. `kprintf("%s", NULL)` causava un #PF nel kernel

**File**: `kernel/arch/x86/kprintf.c`, `print_str()`.

`const char *p = s;` veniva eseguito **prima** di `if (!s) s = "(null)";`,
quindi il loop di conteggio della lunghezza dereferenziava il puntatore nullo.
**Fix**: controllo NULL prima di qualunque dereferenziazione.

### 5. TTY: race "lost wakeup" in `drv_read()`

**File**: `drivers/tty/tty.c`.

La sequenza era, **a interrupt abilitati**:

```c
g_waiting_pid = proc_get_current()->pid;
sched_block(PROC_BLOCKED);
```

Se IRQ1 arrivava nella finestra fra le due istruzioni, l'handler trovava
`g_waiting_pid` già impostato e chiamava `sched_unblock(pid)` su un processo
ancora RUNNING (no-op), azzerando poi `g_waiting_pid`. Subito dopo
`sched_block()` metteva il processo in BLOCKED: la sveglia era già stata
consumata e nessuno l'avrebbe più emessa → **shell bloccata per sempre al
prompt, con la riga già nel ring buffer**.

**Fix**: test-and-block atomico rispetto all'IRQ (`cli`), con ri-controllo del
buffer a interrupt disabilitati. Inoltre `g_input_buf` è ora `volatile`: è
scritto dall'handler IRQ1 e letto in contesto processo, e con `-O2` il
compilatore poteva tenere `count` in un registro attraverso il loop di attesa.

### 6. Leak: PCB, page directory e stack kernel dei driver non caricati

**File**: `kernel/kernel_main.c` (PASSO 14b e PASSO 15).

Su fallimento di `elf_load()` il codice chiamava solo `proc_kill()`, che porta
il processo a ZOMBIE. Nessuno poteva poi raccoglierlo: `init_reaper_task()`
raccoglie **esclusivamente** gli zombie con `ppid == PID di init`, mentre questi
hanno come `ppid` il task che girava durante il boot (idle), che non chiama mai
`waitpid()`. Ogni driver mancante lasciava così per sempre uno slot del pool
PCB, una page directory e le pagine dello stack kernel.

**Fix**: `proc_kill()` seguito da `proc_reap_zombie()`. Il reap diretto è sicuro
perché il processo è creato BLOCKED e non è mai stato eseguito, quindi non è
`g_current` (precondizione di `proc_reap_zombie`).

> Nota: **non** è stato modificato `proc_kill()` per ri-genitorializzare a init,
> perché `sys_waitpid()` si ri-blocca in loop: se init raccogliesse lo zombie
> prima del genitore in attesa, quel genitore resterebbe bloccato per sempre.
> Il fix è quindi al chiamante, non in `proc_kill()`.

## Strumentazione di debug aggiunta (da riusare, non rifare)

### Console seriale kernel su COM1

**File**: `kernel/arch/x86/vga.c` — `serial_init()` + `serial_putchar()`,
chiamate da `vga_init()` e da `vga_putchar()`.

Tutto l'output che passa da `vga_putchar` (quindi `kprintf`, `klog`, l'eco della
tastiera e l'output dei processi via `write`) viene specchiato su COM1 a 38400
8N1. **Indispensabile**: lo schermo VGA è 80x25 e i log di boot scorrono via,
mentre il file seriale conserva tutto. Ha una guardia di spin (100000 cicli) per
non bloccare il kernel su hardware senza COM1.

Uso:
```bash
qemu-system-i386 -drive file=dist/floppy.img,format=raw,if=floppy \
  -m 32M -boot a -display none -serial file:/tmp/serial.txt -no-reboot
```

### Script di pilotaggio QEMU

Non versionati (stavano in `/tmp/exos/`), ma banali da ricreare — l'approccio è
quello che conta:

- **Pilotare la tastiera senza display**: QEMU monitor su socket unix
  (`-monitor unix:/tmp/mon.sock,server,nowait`), poi `sendkey <nome>` per ogni
  carattere. Mappare i caratteri non alfanumerici ai nomi QEMU (`spc`, `ret`,
  `slash`, `dot`, `minus`, `shift-<x>` per le maiuscole).
- **Leggere lo stato hardware al momento del blocco**: `info pic` (è così che si
  è vista la firma `isr=01`), `info registers` (EIP + flag HLT), campionando
  l'EIP più volte per distinguere un vero blocco da un idle loop.
- **Vedere lo schermo VGA**: `screendump /tmp/scr.ppm` dal monitor, poi
  conversione PPM→PNG (in questo ambiente non ci sono PIL/ImageMagick/netpbm:
  serve un convertitore PPM→PNG in Python puro con `zlib` + `struct`, ~20 righe).

### Riferimenti di simboli utili

Per risalire da un `EIP` al simbolo: `nm build/kernel.elf | sort` e cercare
l'intervallo. `build/kernel.elf` è l'ELF con simboli, `build/kernel.bin` è il
flat binary che finisce sul floppy.

## Piste escluse / falsi allarmi (non riesplorare)

- **Il PIT non si era fermato**: la prima lettura di `info pic` mostrava
  `irr=00 isr=00` con la CPU in `hlt` e sembrava un PIT morto. Era invece un
  **idle loop normale** (`sched_start`, `EIP=0x1043e1`): il test non aveva
  premuto Invio perché lo script passava `hello\n` come *backslash + n*
  letterali. Attenzione a questo errore quando si scrivono gli script di test.
- **Perdita di caratteri da tastiera**: sembra che il primo carattere del
  comando successivo venga perso (a schermo si legge `s /bin` invece di
  `ls /bin`). **Non è un bug**: è solo l'eco del type-ahead che compare *prima*
  che la shell stampi il prompt. La riga arriva integra — verificato con
  `argc=2` nel log di `spawn` e con l'output corretto del comando.
- **`fdc_delay_ms()` non è la causa del blocco**, è solo dove il blocco si
  manifestava. Il suo `sti`/`hlt`/`cli` è corretto; il problema era a monte, nel
  PIC.

## Problema aperto — driver in ring3: migrazione a metà

> **SUPERATO il 2026-07-30 per la tastiera** — vedi la sessione in cima al
> file. `/dev/kbd.drv` è ora un processo ring3 vero e viene caricato al boot;
> la voce `tty` è stata tolta da `kernel.cfg` (quel driver non è mai esistito
> come file) e anche `floppy`, che continuerebbe a fallire. Resta valida
> l'analisi qui sotto **per il solo driver floppy**, che è ancora ET_DYN e
> ancora servito da codice in-kernel.

**Non è un bug da correggere, è una decisione di progetto da prendere.**

Allo stato attuale i driver **non vengono caricati** e il boot logga:

```
[WARN] [PASSO 14b] Driver 'tty': '/dev/tty.drv' non caricato — saltato
[WARN] [PASSO 14b] Driver 'floppy': '/dev/floppy.drv' non caricato — saltato
```

Il sistema funziona comunque perché TTY e floppy sono serviti da codice
**in-kernel** (vedi sotto). Situazione precisa:

| Cosa | Stato reale |
|---|---|
| `boot/kernel.cfg` | dichiara `modules = tty,floppy` → `/dev/tty.drv`, `/dev/floppy.drv` |
| `/dev/tty.drv` | **non viene mai compilato**: `drivers/tty/tty.c` è linkato *dentro* il kernel (`$(BUILD_KERNEL)/tty.o` in `KERNEL_OBJS`) e usato al PASSO 14 via `drv_init()`. La voce in `kernel.cfg` è quindi obsoleta. |
| `/dev/floppy.drv`, `/dev/kbd.drv` | compilati come **ET_DYN** (`-shared -fPIC`) per `drvmgr.c`/`dynlink.c`, cioè come moduli **kernel-space**. `elf_load()` accetta solo ET_EXEC → rifiutati con `tipo non supportato (type=3)`. |
| `kbd.drv` | è sul floppy ma **non** è elencato in `kernel.cfg` |
| accesso al floppy | fatto dal driver FAT12/FDC **interno al kernel** (`kernel/fs/fat12.c`) |

Il punto chiave: **anche se `elf_load()` accettasse ET_DYN, quei driver non
potrebbero girare in ring3 così come sono scritti.** Usano `port_inb`/`port_outb`
diretti (→ #GP in ring3) e chiamano simboli del kernel per linkage diretto
(`klog`, `irq_register_handler`, `pic_unmask_irq`, `sched_unblock`), risolti da
`drvmgr.c` solo perché vengono mappati nello spazio del kernel.

L'API syscall per i driver ring3 **esiste già** ed è quella giusta da usare:
`SYS_IRQ_BIND` (224), `SYS_IOPORT_BIND` (225), `SYS_IOPORT_IN` (226),
`SYS_IOPORT_OUT` (227), `SYS_IPC_*` (220–223), con `irq_bind_process()` e
`ipc_notify_irq()` già implementati in `kernel/arch/x86/isr.c`.

Per completare la migrazione servirebbe, per ciascun driver:
1. riscriverlo contro l'API syscall (niente `port_*` diretti, niente simboli kernel);
2. aggiungere un `_start` e linkarlo **ET_EXEC** statico (come `/bin/*`, non `-shared`);
3. un loop di servizio IPC (`ipc_register(nome)` + `ipc_recv`) al posto delle
   funzioni `drv_*` chiamate direttamente;
4. decidere il **bootstrap**: il driver del floppy va letto *dal* floppy, quindi
   il kernel deve conservare il proprio FAT12/FDC minimo almeno per caricarlo;
5. allineare `boot/kernel.cfg` (rimuovere `tty` o costruire davvero `tty.drv`,
   aggiungere `kbd`).

Vedi anche il punto 5 di "Da fare" in `KERNEL_CORE_NOTES.md`.

## Comandi di build/test

```bash
make all        # compila tutto e crea dist/floppy.img

# Boot con log seriale completo (il log VGA scorre, la seriale no):
qemu-system-i386 -drive file=dist/floppy.img,format=raw,if=floppy \
  -m 32M -boot a -display none -serial file:/tmp/serial.txt -no-reboot

# Marker seriali del bootloader attesi (prima che il kernel prenda COM1): SFKPJK
```

---

# ARCHIVIO — SESSIONE 2026-06-14 (bootloader) — ✅ RISOLTO

> Il fault `#GP` a `IP=0x707` descritto qui sotto **non si verifica più**: il
> bootloader completa il passaggio a Protected Mode e salta al kernel. I marker
> seriali arrivano fino a `SFKPJK` (inclusi `J` = PM32 e `K` = `kernel_entry`),
> che nella tabella qui sotto erano segnati come "MAI raggiunto".
> Il contenuto è conservato per riferimento storico sulle piste esplorate.

Data: 2026-06-14
Ambiente di test: QEMU 8.2.2 (`qemu-system-i386`), SeaBIOS 1.16.3-debian-1.16.3-2,
floppy FAT12 1.44MB (`dist/floppy.img`), build con `make clean && make stage1
stage2 kernel floppy`.

## TL;DR

Il bootloader ora funziona in modo solido fino alla **fine del codice a 16
bit**: Stage1 carica Stage2, Stage2 trova ed carica `KERNEL.BIN` in RAM,
costruisce la struttura `BootInfo`, copia il kernel a 1MB, prepara la GDT e
imposta `CR0.PE=1`. Il marker seriale risultante è **`SFKP`**.

Il **far jump verso Protected Mode 32-bit fallisce** con un `#GP` (poi
`#DF` → triple fault → reset) **immediatamente dopo** il salto, con queste
caratteristiche costanti, riprodotte con `qemu-system-i386 ... -d int`:

```
v=0d e=0000 i=0 cpl=0 IP=0010:00000707 pc=00000707 SP=0000:00007000 EAX=00000000
v=08 e=0000 i=0 cpl=0 IP=0010:00000707 pc=00000707 SP=0000:00007000 EAX=00000000
```

Questo fault si verifica **identico** (stesso `IP=0x707`, stesso `EAX=0`,
stesso `e=0000`) con **almeno 4 varianti diverse** del codice 32-bit in
`pm32_entry` (vedi sotto), il che fa pensare a un evento **asincrono**
(interrupt/SMI) che interviene proprio nella finestra fra `mov cr0,eax`
(set `PE=1`) e l'esecuzione del codice 32-bit, piuttosto che a un bug nel
nostro codice 32-bit stesso.

## Bug risolti in questa sessione (rispetto allo stato precedente)

1. **BPB malformato in `boot.asm`**: i campi erano scritti come `dw
   512,1,1,2,224,2880,...` (tutti a 16 bit), ma `SectorsPerCluster` e
   `NumberOfFATs` devono essere `db` (8 bit). Questo rendeva il filesystem
   non riconoscibile da `mtools`/`mformat` ("non DOS media") e/o produceva
   un salto iniziale (`jmp short`) verso un offset sbagliato. **Fix**: ogni
   campo del BPB è ora `db`/`dw`/`dd` separato con il tipo corretto (vedi
   `bootloader/stage1/boot.asm`).

2. **Overlap Root Dir / FAT in Stage1**: la versione precedente caricava la
   FAT a `0x9000` (9 settori) e DOPO la root dir a `0x7E00` (14 settori =
   7168 byte = `0x7E00..0x9A00`), che **sovrascrive** `0x9000..0xA200`. La
   FAT risultava corrotta per qualsiasi file con più di un cluster. **Fix**:
   ordine invertito — root dir prima (`0x7E00..0x9A00`), FAT dopo a
   `0xA000..0xB200` (nessun overlap).

3. **Doppio incremento di `BX` nel loop di caricamento multi-cluster**:
   `rsect` usa `pusha`/`popa`, quindi **ripristina BX** al ritorno. Il loop
   principale deve quindi avanzare `BX` di 512 esso stesso dopo ogni
   chiamata — questo era già presente ma in una versione precedente era
   stato rimosso per errore, causando che ogni cluster venisse scritto
   sempre allo stesso indirizzo (`0x500`), sovrascrivendo i cluster
   precedenti. **Fix**: `call rsect` seguito da `add bx, 512` nel loop
   `.lp` di `boot.asm`.

4. **Porte COM1 sbagliate (0xF8 invece di 0x3F8)**: in diversi punti il
   codice usava `out 0xF8, al` ecc. — `out imm8, al` accede alla porta I/O
   **0xF8 = 248 decimale**, non `0x3F8 = 1016`. Le porte COM1
   (`0x3F8-0x3FD`) sono **sempre > 255** e richiedono `mov dx, 0x3FX; out
   dx, al`. **Fix**: applicato ovunque in `boot.asm` e `loader.asm`.

5. **Makefile: Stage2 compilato dai vecchi file C** (`fat12.c`, `loader.c`,
   `print.c`, linkati con `stage2.ld`) producendo un ELF a 32 bit di 5742
   byte, **non eseguibile** in Real Mode 16 bit (il primo byte `0xFA`=`cli`
   veniva eseguito come istruzione 16-bit ma il resto del codice presupponeva
   un ambiente 32-bit con paging/GDT già attivi). **Fix**: nuovo target
   `$(STAGE2_BIN)` nel `Makefile` che assembla `bootloader/stage2/loader.asm`
   direttamente con `nasm -f bin` (flat binary 16-bit, ORG `0x0500`).

6. **`loader.asm` riscritto da zero** (vedi sotto) per essere un loader
   monolitico a 16 bit puro che:
   - cerca `KERNEL.BIN` nella root dir (già in RAM a `0x7E00`);
   - lo carica a `0x10000` via INT 13h, seguendo la catena FAT a `0xA000`;
   - costruisce `BootInfo` a `0xC000` (magic, drive, mem_lower/upper via
     INT 15h/88h, kernel_size);
   - copia il kernel da `0x10000` a `0x100000` **in Real Mode**, usando il
     trucco A20 `ES=0xFFFF, DI=0x0010` → fisico `0xFFFF0+0x10 = 0x100000`
     (`rep movsw`, `CX=0x7200` word ≥ dimensione kernel 57632 byte);
   - scrive la GDT a `0x0A00` (null, data flat `0x08`, code flat `0x10`,
     entrambi `base=0 limit=4GB`, flags `00CF`);
   - `lgdt` + `CR0.PE=1` + far jump a `pm32_entry` con selettore `0x10`.

7. **Bug di allineamento cluster scoperto e fixato**: `loader.asm` (≈550-580
   byte) occupa **2 cluster** (1 cluster = 512 byte su FAT12 1.44MB). Stage1
   carica il cluster 1 a `0x500..0x6FF` e il cluster 2 a `0x700..`. Se
   un'istruzione (in particolare l'inizio di `pm32_entry`, target del far
   jump) **attraversa il confine `0x6FF/0x700`**, il codice letto dalla CPU
   in quella zona risulta **diverso dal contenuto del file** (verificato con
   marker seriali: il codice in cluster 2 produceva byte/comportamento
   inconsistenti quando un'istruzione iniziava nell'ultimo byte del cluster
   1). **Fix**: aggiunto `times 512-($-$$) db 0x90` prima di `[BITS 32]` per
   garantire che `pm32_entry` inizi **esattamente** a file-offset 512 =
   fisico `0x700` (inizio del cluster 2), così nessuna istruzione è spezzata
   tra i due cluster. Questo fix è corretto e va mantenuto, ma **non è
   sufficiente** a risolvere il fault residuo descritto sopra (il fault
   `IP=0x707` si riproduce identico anche con `pm32_entry` correttamente
   allineato).

8. **Marker seriale precoce nel kernel**: aggiunta in
   `kernel/arch/x86/entry.asm`, come primissima istruzione di
   `kernel_entry`, una stampa `'K'` su COM1 (porta 0x3FD/0x3F8, `mov dx,...`)
   per confermare in futuro che il salto a `0x100000` sia avvenuto e il
   kernel sia stato copiato correttamente. **Non ancora raggiunta** perché
   il fault avviene prima.

## Stato attuale dei marker seriali

| Marker | Significato                                            | Stato |
|--------|---------------------------------------------------------|-------|
| `S`    | Stage2 (`loader.asm`) avviato, COM1 ok                  | ✅ OK |
| `F`    | `KERNEL.BIN` trovato in root dir                        | ✅ OK |
| `K`    | Kernel caricato in RAM a `0x10000` (BootInfo costruito) | ✅ OK |
| `P`    | GDT scritta a `0x0A00`, pronto per `lgdt`+`CR0.PE=1`    | ✅ OK |
| `J`    | In PM32, segmenti flat impostati, sto per `jmp 0x100000`| ❌ MAI raggiunto |
| `K` (kernel) | `kernel_entry` raggiunto a `0x100000`             | ❌ MAI raggiunto |

## Il fault residuo: dettagli e ipotesi

Sequenza esatta in `loader.asm` prima del fault:

```asm
.pmloop:
    lgdt [0x0A1E]        ; GDTR: limit=23, base=0x0A00
    mov  eax, cr0
    or   al, 1
    mov  cr0, eax        ; CR0.PE = 1
    db   0x66, 0xEA      ; far jmp dword 0x10:0x00000700
    dd   pm32_entry
    dw   0x10
    jmp  .pmloop
```

`pm32_entry` (allineato a fisico `0x700`):

```asm
[BITS 32]
pm32_entry:
    mov  ax, 0x08
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x9F000
    ; ... marker 'J', poi jmp dword 0x100000
```

**Varianti testate, TUTTE con fault identico `IP=0x707 EAX=0 e=0000`:**

- `pm32_entry` con setup segmenti completo + marker `J` + `jmp 0x100000`
  (versione "di produzione").
- `pm32_entry` ridotto a solo `mov ax,0x08; mov ds/es/fs/gs/ss,ax; mov
  esp,...` seguito da spin-loop (`jmp $`).
- `pm32_entry` con **ri-caricamento esplicito della GDTR** all'inizio,
  usando `db 0x2E` (prefisso CS:) + `0F 01 15` (`lgdt`) + `dd 0x0A1E`,
  PRIMA di toccare i segmenti — ipotesi "SMI resetta GDTR tra `lgdt` e il
  far jump" (annotata in una sessione precedente). **Stesso fault**, stesso
  `IP=0x707`.
- `pm32_entry` ridotto a **solo** `.spin: jmp .spin` (2 byte, `EB FE`): in
  questo caso **non** c'è stato il fault entro 4s (il processo va in
  timeout, cioè la VM resta "viva" — ma questo test aveva `pm32_entry` a
  file-offset 511 = fisico `0x6FF`, quindi l'istruzione `EB FE` era spezzata
  tra i due cluster e il risultato non è probante: la CPU potrebbe
  semplicemente eseguire un loop diverso su byte "garbage" che non causa
  `#GP`).

**Osservazione chiave**: `EAX=0x00000000` al momento del fault, e
`e=0000` (selector index 0, GDT, non-external) è la firma classica di un
**"load SS with null selector → #GP(0)"**. Ma nel nostro codice `AX=0x08`
viene caricato esplicitamente PRIMA di `mov ss,ax`. Il fatto che il fault
sia **indipendente dal contenuto effettivo di `pm32_entry`** (4 varianti
diverse, stesso `IP` e stesso `EAX=0`) suggerisce che **il fault non avviene
nel nostro codice**, ma in un **gestore asincrono** (interrupt o SMI) il cui
codice fisico risiede per coincidenza intorno a `0x700` in questa
combinazione QEMU/SeaBIOS, e che viene attivato proprio nella finestra
critica subito dopo `mov cr0,eax` (PE=1) / durante il far jump.

### Piste da investigare nella prossima sessione

1. **Disabilitare gli interrupt mascherabili E non mascherabili** prima
   della sequenza critica: oltre a `cli` (già presente), provare a
   mascherare il PIC (`out 0x21,0xFF` / `out 0xA1,0xFF`) prima di
   `lgdt`/`CR0.PE=1`, per escludere IRQ hardware (timer) come causa.

2. **SMI (System Management Interrupt)**: NON è mascherabile da `cli` né dal
   PIC. Se il chipset PIIX4 emulato genera un SMI per qualche motivo (es.
   accesso a porte ACPI/PM, o periodicamente), il SMM handler di SeaBIOS
   potrebbe girare a un indirizzo fisico basso e fare assunzioni sullo stato
   del processore che non sono più valide con `CR0.PE=1`. Provare:
   - disabilitare SMI generation via i registri SMI_EN del PIIX4 (porta
     I/O base ACPI, tipicamente `0xB2`/`0xB3` per APMC/APMS, oppure i
     registri PMBASE — da determinare con `qemu -d guest_errors` o
     analizzando `hw/i386/` di QEMU);
   - testare con `-machine pc-i440fx-...` vs `-machine q35` e/o con
     `-no-acpi` per vedere se il fault cambia/scompare;
   - testare con un `-bios` diverso (versione SeaBIOS) per isolare se è
     un bug/comportamento specifico di SeaBIOS 1.16.3.

3. **Confrontare `IP=0x707` con la mappa di memoria di SeaBIOS**: con `-d
   cpu` su una run "pulita" (senza il nostro floppy, solo BIOS), verificare
   cosa c'è normalmente a fisico `0x700-0x720` dopo il boot di SeaBIOS — se
   è codice/dati del BIOS stesso (es. un trampoline SMM), questo confermerebbe
   l'ipotesi del punto 2 e indicherebbe di **spostare tutto il nostro Stage2
   a un indirizzo più alto** (es. `0x8000` invece di `0x500`), lontano da
   eventuali aree riservate al BIOS.

4. **Approccio alternativo più robusto**: spostare Stage2 (e quindi
   `pm32_entry`, GDT, ecc.) in un'area di memoria sicuramente libera e ben
   sopra `0x7C00` (es. caricare Stage2 a `0x00008000` invece di `0x0500`).
   Questo richiede modifiche minime a `boot.asm` (buffer destinazione) e
   `loader.asm` (`[ORG 0x8000]`), ma elimina la possibile interferenza con
   aree basse usate dal BIOS/SMM.

## File modificati in questa sessione

- `bootloader/stage1/boot.asm` — riscritto: BPB corretto, root-dir-prima-FAT,
  `rsect` con `pusha`/`popa` + `add bx,512` nel loop principale, COM1 init
  prima del far jump a Stage2.
- `bootloader/stage2/loader.asm` — **nuovo file**, sostituisce
  `entry.asm`/`fat12.c`/`loader.c`/`print.c` come unico sorgente di Stage2.
  I vecchi file C/asm sono ancora presenti nella directory ma **non sono più
  usati dal Makefile** (possono essere rimossi in futuro).
- `Makefile` — target `stage2`/`$(STAGE2_BIN)` riscritto per usare
  `nasm -f bin bootloader/stage2/loader.asm`.
- `kernel/arch/x86/entry.asm` — aggiunto marker seriale `'K'` come prima
  istruzione di `kernel_entry`.

## Comandi di build/test

```bash
cd exos_fixed
make clean && make stage1 stage2 kernel floppy

qemu-system-i386 -drive file=dist/floppy.img,format=raw,if=floppy \
  -m 32M -boot a -display none -serial file:/tmp/out.txt -no-reboot
cat /tmp/out.txt        # atteso: SFKP

# Per il debug del fault PM32:
qemu-system-i386 -drive file=dist/floppy.img,format=raw,if=floppy \
  -m 32M -boot a -display none -d int -D /tmp/int.log -no-reboot
grep "v=0d\|v=08" /tmp/int.log
```

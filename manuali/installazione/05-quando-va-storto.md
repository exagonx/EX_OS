# 5. Quando va storto

[torna all'indice](00-indice.md)

Ogni voce qui sotto è successa davvero. Il sintomo è scritto com'è apparso,
perché è da quello che si parte.

---

## «comando non trovato» per un programma che c'è

Due cause diverse, e si distinguono in un secondo.

### Il file c'è ma il PATH non lo copre

```
ls /exos/bin/make      ->  make   199032
make                   ->  comando non trovato
```

Gli strumenti si installano in `/exos/bin`. Fino alla 0.217 nel PATH c'era
solo `/cdrom/exos/bin`, cioè il caso in cui il CD è nel lettore: chi li
prendeva dalla rete se li ritrovava invisibili.

! **LA STAMPELLA C'ERA, E PROPRIO PER QUESTO NON SI VEDEVA.** `toolinst` —
  l'installatore degli strumenti **dal CD** — aggiunge `/exos/bin` al PATH del
  bersaglio. Funzionava per una strada sola, e la seconda è arrivata dopo.

**Rimedio subito:** il percorso completo, `/exos/bin/make`.
**Rimedio definitivo:** la riga in `/boot/kernel.cfg`

```
PATH        = /bin:/dev:/exos/bin:/cdrom/exos/bin:/cdrom/bin
```

### Il nome è più lungo di otto lettere e il volume è FAT

```
exec: comando non trovato: netdetect
exec: comando non trovato: automount
```

Esattamente i due programmi con nove lettere. Su FAT diventano `NETDET~1` e
`AUTOMO~1`, e **`fat12.c` i nomi lunghi non li legge affatto** — `fat.c`, che
serve il volume in RAM e i dischi, sì.

! **SE SUCCEDE SU UN DISCHETTO CHE DOVREBBE AVERE IL VOLUME IN RAM**, allora il
  volume in RAM non c'è: guarda il registro d'avvio. `VFS: root '/' sul floppy
  (fat12.c)` vuol dire che Stage 2 non è stato costruito con `RAMDISCO=1`.

---

## `fdisk`: non riesco a cancellare la partizione estesa

```
d  sull'estesa       si rifiuta finche' ci sono logiche
t                    si rifiuta per lo stesso motivo
```

E le logiche `fdisk` non le sa cancellare, perché non scrive EBR. E il kernel
non espone nemmeno l'estesa come dispositivo su cui formattare: c'è `hd0p5`,
`hd0p3` no.

! **OGNI RIFIUTO E' SENSATO DA SOLO, E INSIEME ERANO UN VICOLO CIECO.**

La via d'uscita è il comando **`z`**, che azzera tutta la tabella. Dice per
esteso che la catena di EBR resta orfana sul disco e ti fa scrivere `azzera`.

! **E SERVONO DUE GIRI.** Il kernel concede di far sparire un'estesa con
  logiche dentro **solo** verso una tabella completamente vuota: una proposta
  che la cancella e intanto crea altro è un ripartizionamento, e lì il rifiuto
  vale ancora. Quindi prima `z` + `w`, poi si rientra e si crea.

---

## L'installazione si ferma su «i file nuovi non sono mappabili»

```
! i file nuovi non sono mappabili: non trovato (errore -2)
Aggiornamento ANNULLATO.
```

Il messaggio parla di frammentazione e suggerisce di riformattare. **Non è
quello.** Era una variabile riusata in `install`: i file di avvio finivano in
`/USB/` invece che in `/boot/`, e `bootverify` li cercava dove dovevano
stare.

Corretto in `install` 0.002. Se lo vedi ancora, il tuo `install` è più
vecchio: `install -version`.

---

## Il sistema installato si avvia senza tastiera

Guarda se durante l'installazione è passata la riga:

```
! /bin/hwconfig non si avvia
  I driver NON sono stati installati.
```

`kernel.cfg` carica `[modules] kbd = /dev/kbd.drv`: senza quel file la
macchina si avvia e non risponde a un tasto, e da davanti non puoi nemmeno
chiedere cos'è successo.

La causa era in `install`: i driver si copiavano **solo** se il supporto
portava `/boot/minimale.txt`. Corretto in `install` 0.003 — adesso si copiano
sempre.

---

## `netupdate`: «il server si contraddice»

```
! catalogo.txt non ha l'impronta che versione.txt dichiara.
  Il server si contraddice: non tocco niente.
```

`netupdate` sta facendo il suo mestiere: si ferma invece di scaricare da un
server incoerente. La causa è quasi sempre **il caricamento**, non il
pacchetto.

### Il file è vuoto

Dalla 0.012 `netupdate` te lo dice: «zero byte — non è un pacchetto sbagliato,
è un file che non è stato caricato». Ricaricalo.

### I file sono corti di qualche byte

```
bin/automount   18068 dichiarati,  18061 sul server     -7
bin/blkscan     13676 dichiarati,  13673 sul server     -3
bin/sh          42896 dichiarati,      0 sul server
```

! **OGNI FILE PIU' CORTO DI UNA QUANTITA' DIVERSA E' LA FIRMA DEL MODO
  ASCII.** Il trasferimento converte i byte `0x0D` e ne toglie tanti quanti ne
  conteneva il file.

! **E IL MODO AUTOMATICO SCEGLIE ASCII PROPRIO PER I NOSTRI FILE**, perché
  decide guardando l'estensione e i programmi di EX-OS non ce l'hanno: `sh`,
  `cp`, `ls` sembrano testo a qualunque euristica. I `.drv` e le `.so`
  passano; tutto `/bin` no.

! **E NESSUNO DA' UN ERRORE**: l'FTP dice che è andato bene, il web risponde
  200. L'unico rilevatore è l'impronta.

**Rimedio:** non caricare a mano. Vedi il [capitolo 6](06-pubblicare-il-repo.md).

---

## La macchina sparisce dalla rete dopo `netinst.sh`

Hai usato la versione per floppy su un sistema che la rete ce l'aveva già:
sono partiti due `pci.drv` e due `ip.drv` a contendersi la scheda.

Le copie però erano già riuscite: **basta riavviare**. La versione per
chiavetta non lo fa più e te lo dice.

---

## La sessione telnet si pianta dopo un comando che stampa molto

Sintomo: il comando parte, gira ed esce con codice 0, ma l'uscita non torna
indietro — prima una riga troncata a metà parola, poi più niente. E da lì la
macchina accetta le connessioni senza servirle, perché `telnetd` serve una
sessione per volta.

Era `telnetd` che ignorava il **numero di byte accettati** dallo stack e
avanzava comunque. Corretto in `telnetd` 0.004: `telnetd -version`.

---

## La porta 23 si apre ma telnet non scrive niente, nemmeno dopo `kill`

Sintomo: il client si collega e non riceve un byte; uccidere `telnetd` e
rilanciarlo non cambia niente («non posso ascoltare sulla porta 23 (-98)»),
serve riavviare.

Erano tre difetti in fila, corretti nel kernel 0.278, `ip.drv` 0.012,
`sh` 0.008 e `telnetd` 0.011:

- un programma per l'altra architettura (32 bit su un sistema a 64, o il
  contrario) veniva aperto con `gfedit`, che legge la tastiera della
  macchina e non la sessione: la sessione restava ferma lì. Ora la shell
  dice «e' un programma a 32 bit, questo sistema e' a 64»;
- la shell interrotta da `telnetd` quando il client se ne va non usciva se
  stava aspettando un comando, e teneva il terminale della sessione;
- la porta di un `telnetd` ucciso restava intestata a lui in `ip.drv`:
  accettava le connessioni senza nessuno dietro e respingeva il `telnetd`
  nuovo. Ora una porta di un programma morto si libera.

! Un programma lanciato dalla sessione che **non** legge la sessione
  (`gfedit`, per dirne uno) resta vivo dopo che il client se n'è andato:
  si ferma con `ps` e `kill`.

---

## Con molti client insieme la rete si ferma, il loopback no

Sintomo: un server (MariaDB, ma vale per qualunque servizio TCP) regge uno
o due client; con quattro, dopo una manciata di secondi, le richieste
restano ferme (10 s, 20 s), poi le connessioni cadono e le nuove non
entrano più. Dalla macchina stessa, su `127.0.0.1`, prima «Can't connect
(100)» e poi di nuovo tutto a posto; da fuori niente fino al riavvio.

Era la casella dei messaggi di `ip.drv`, che tiene quattro messaggi: sotto
carico la riempiono i programmi, e il driver della scheda, per consegnargli
un frame, restava fino a 10 s ad aspettare un posto senza più servire la
scheda, mentre `ip.drv` aspettava un posto nella casella del driver. Poi il
frame si perdeva insieme alla richiesta di riceverne un altro, e la scheda
restava sorda per sempre. Corretto in `ip.drv` 0.015 e nei driver di rete
(`e1000` 0.003, `ne2k` 0.003, `rtl8169` 0.003, `pcnet` 0.003, `sis900`
0.019, `nforce` 0.014, `ar5k` 0.003): il driver non aspetta più la casella
di `ip.drv`, un frame che non ci sta resta in coda con la sua richiesta, e
`ip.drv` rimanda la richiesta se per un secondo non arriva niente.

Nella stessa prova sono venute fuori altre tre cose, corrette in `ip.drv`
0.015 e nella libc:

- una connessione che il client chiudeva prima che il server la
  accettasse non veniva più né accettata né liberata, e occupava un posto
  della tabella per sempre: ora `accept` la consegna e il server legge la
  fine;
- le connessioni morte e già chiuse dal programma restavano nella tabella
  finché non serviva il posto per una connessione in uscita: ora tornano
  libere subito, e anche una connessione in entrata, a tabella piena,
  prende il posto di una già chiusa;
- `poll` diceva che un socket in ascolto era sempre pronto: un server con
  il socket in ascolto non bloccante (MariaDB) girava a vuoto fra `poll` e
  `accept` riempiendo la casella di `ip.drv`. Ora `poll` chiede allo stack
  se c'è davvero qualcuno da accettare. Vale per i programmi ricompilati
  con la libc nuova.

Per vedere la tabella delle connessioni TCP:

```
ipcfg -tcp
```

Una riga per connessione: stato, porta, chi c'è dall'altra parte, il
gruppo del programma che la tiene, i byte in coda, e «chiusa dal programma»
per quelle che il programma ha già chiuso e che aspettano l'altra parte.
Il tetto della tabella lo dice l'ultima riga («N slot occupati su M»).
Dalla versione 0.017 di `ip.drv` non è un numero fisso: viene dalla
memoria, una connessione ogni 32 KB di RAM e almeno 4096 (16384 con 512 MB,
32768 con 1 GB), e i posti si allocano solo quando servono. Si cambia con
`tcpmax = N` nella sezione [kernel] di `/boot/kernel.cfg` (vedi
`/boot/kernel.txt`) o, per una sola accensione, lanciando `ip.drv -t N`.
Quando è piena, una connessione nuova (in entrata o in uscita) prende il
posto della più vecchia già chiusa dal suo programma.

Anche i descrittori di un programma (file, pipe, socket, insiemi epoll,
contati insieme) dal kernel 0.281 non hanno più un tetto fisso: uno ogni
2 KB di RAM, almeno 1024, oppure `fdmax = N` in [kernel]. Un programma che
dice «Too many open files» (EMFILE) ha raggiunto quel numero o quello che
si è dato lui con `setrlimit`. Fino al kernel 0.280 un programma teneva
1024 socket e 128 file.

Se `ipcfg -tcp` dice «ip.drv non accetta la richiesta (casella piena?)»,
`ip.drv` non ha preso il messaggio per 10 secondi: è fermo o sommerso.
Dal kernel 0.280 chi manda un messaggio a una casella piena fa la fila nel
kernel e passa nell'ordine in cui è arrivato; prima riprovava ogni 5 ms e
sotto carico (gli otto fili di MariaDB) i più svelti passavano sempre e gli
altri, come `ipcfg`, restavano fuori.

---

## Il ping non risponde ma la porta risponde

Se `ping` è muto **e** una connessione TCP riesce, la macchina **non è morta**:
lo stack è vivo per il TCP. Un ping muto da solo non dimostra niente — è una
conclusione che è già stata tirata due volte a torto.

Prima di concludere, prova sempre anche una connessione TCP.

---

## Un `ls` fa sparire la macchina

Su una partizione FAT che arriva da fuori può succedere: una catena di cluster
che torna su se stessa non finisce mai, e il ciclo sta **dentro il kernel**.

Corretto dalla 0.216 con due guardie — la lepre e la tartaruga per i cicli, e
un tetto per le catene lunghe ma diritte. Adesso si ferma con

```
FAT: catena ciclica in una directory (cluster N), mi fermo
```

! **NESSUN FILESYSTEM DEVE POTER FERMARE LA MACCHINA**, per malmesso che sia.
  Un supporto che arriva da fuori non è un dato fidato.

---

## `umount /` o `system -unload`: «occupato»

Qualcuno usa ancora il disco. Chi, lo dice

```
ps -m /
```

per ogni processo: il programma (sta sul disco), file aperti, la directory
corrente. La directory corrente da sola non blocca: `system -unload` sposta
in `/` chi stava in una directory che la base non ha. Un programma partito
dal disco sì, finché gira.

! **FINO AL KERNEL 0.273 ANCHE UN PROGRAMMA GIA' FINITO TENEVA IL DISCO**: un
  processo terminato resta «zombie» finché il padre non lo raccoglie, e
  intanto teneva aperto il proprio programma. Dal 0.274 il programma si
  chiude subito, e gli zombie il cui padre è morto li raccoglie `init`.

! **E `system` STESSO, SE PARTE DA `/bin`**: il PATH mette `/bin` prima di
  `/rdboot/bin`. Lo sa, e si rilancia da solo dalla copia della base.

---

## Ogni avvio dice che `kernel.cfg` e stage 2 non vanno d'accordo sulla risoluzione

Guarda la sezione `[kernel]` di `/boot/kernel.cfg`: se ci sono **più righe
`svga =`**, il kernel ne legge una e `svga.drv` ne cambiava un'altra. È
successo sul Compaq CQ60, con cinque righe alternate. Da `svga.drv` 0.005
basta rilanciare

```
svga.drv 1024x768        (o il modo che vuoi)
```

e ne resta una sola. Poi `reboot`.

## Exilla (Firefox): la finestra resta bianca, o le pagine non si aprono

**Prima leggi cosa dice Firefox.** Da `exilla` 0.006 (con `attesa` 0.005) i
suoi messaggi non vanno più sulla console di testo — dove nessuno li legge e
dove ogni riga rallenta Firefox — ma in un file:

```
cat /root/.exilla/firefox.log       questo avvio
cat /root/.exilla/firefox.log.1     quello prima, utile dopo un riavvio
```

Si leggono anche da un'altra macchina, con `telnet`, mentre Firefox gira. Le
righe `exilla: ...` sono il diario della finestra (creazione, misura, la
memoria condivisa con il server a finestre); `console.error` e
`JavaScript error` vengono da Firefox stesso.

**Con un kernel prima del 0.277 il testo può mancare** e le pagine restare
bianche: due fili di Firefox che leggevano lo stesso file si spostavano la
posizione a vicenda, e i caratteri e i database del profilo arrivavano
sbagliati. `netupdate -check -yesall` e `netupdate -install:exilla` portano il
kernel e il Firefox corretti. Se dopo l'aggiornamento resta bianca, il
profilo può essere stato rovinato da quelle letture: spostalo e Firefox ne
crea uno nuovo (si perdono segnalibri e cronologia):

```
mv /root/.exilla/profilo /root/.exilla/profilo-vecchio
```

È quello che è successo sul portatile di prova il 24 ottobre 2026: con un
profilo rovinato Firefox partiva ma non apriva nessuna pagina (nel registro
`NetworkError` e `database disk image is malformed`); con il profilo nuovo
si è aperto anche Gmail.

**Le impostazioni con la sola colonna di sinistra** (la casella «Cerca» e
niente al centro) erano un difetto di Exilla, corretto il 24 ottobre 2026:
la pagina cercava l'impostazione «browser predefinito», che esiste solo sui
sistemi dove Firefox può diventarlo, e si fermava. Si corregge con
`netupdate -install:exilla`.

**Firefox si chiude da solo dopo un po'**, e sulla console compare
`[FAULT] ... firefox: page fault a 0x00000000`: se nel registro, poco prima,
c'è `ShmSegmentsWriter failed to allocate chunk`, Firefox aveva finito le
zone di memoria condivisa che la libc gli concedeva (128, troppo poche per
una pagina pesante come Gmail tenuta aperta mezz'ora). Dal 24 ottobre 2026
sono 1024, e se finissero ancora il registro lo dice con una riga
`libc: shm_open: le zone di memoria condivisa ... sono finite`.
`netupdate -install:exilla` porta il Firefox con la libc nuova.

**Lo swap a zero non è un guasto.** `mem` mostra lo scambio inutilizzato
finché la RAM basta: Firefox occupa qualche centinaio di MB, e su una
macchina da 3 GB non ce n'è bisogno. Se invece `mem` dice «nessuna area
attiva» l'area non si è accesa all'avvio: il perché lo dice `dmesg SWAP`,
e cosa controllare sta in [4, l'area di scambio](04-dopo-installazione.md).

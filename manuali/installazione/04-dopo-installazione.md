# 4. Dopo l'installazione

[torna all'indice](00-indice.md)

---

## 4.1 I conti

Se hai installato con `-senza-conti`, al primo avvio `login` ti chiede di
crearne due: **root** per riparare, **il tuo** per lavorare.

! **DUE, E NON UNO.** Lasciando fare a `login` da solo si otteneva un utente
  solo, ed era root: da lì in poi si lavorava sempre da amministratore, che è
  il modo in cui un errore qualunque diventa un danno qualunque.

Per aggiungerne altri dopo: `login -a`.

---

## 4.2 La rete all'avvio

La rete si accende da `/boot/avvio.sh`, che `login` esegue **da root, una
volta, prima di chiedere chi sei**: così c'è anche per chi non ha ancora
fatto l'accesso. Lo scrive `hwconfig` con il driver della scheda che trova.
`/boot/autoexec.sh` resta per i comandi di chi entra.

```
pci.drv &
netdetect -c
ip.drv &
dhcp
```

! **L'ORDINE NON E' UNA FORMALITA'.** `pci.drv` è il fornitore di tutti: senza,
  `netdetect` non trova nessuna scheda. `netdetect -c` **trova la scheda e
  avvia il driver** — `netdetect` da solo elenca e basta, ed è l'errore che
  viene naturale fare. `ip.drv` monta lo stack sopra quel driver. `dhcp` sta
  sopra UDP come un client qualunque.

! **`dhcp` SENZA `&`, SE DOPO C'E' QUALCOSA CHE HA BISOGNO DELL'INDIRIZZO.** In
  primo piano blocca finché l'indirizzo non c'è. Con `dhcp -r &` la riga dopo
  parte subito e vince la corsa una volta su due.

La macchina raggiunge sé stessa su `127.0.0.1` (`localhost`) e sul proprio
indirizzo, da `ip.drv` 0.013: un programma si collega a un server che gira
sulla stessa macchina (MariaDB, un `openssl s_server`) senza passare dalla
scheda di rete. Serve comunque che `ip.drv` sia avviato, cioè una scheda
riconosciuta.

---

## 4.3 La shell remota

```
telnetd &          chiede utente e password
telnetd -s &       dà una shell da amministratore SENZA password
```

Per averla a ogni avvio non si tocca nessuno script: in `/boot/avvio.sh` c'è
già la riga `telnetd -auto &`, che legge `/boot/telnetd.cfg` e parte solo se
la chiave `avvio` dice `login` (chiede la password) o `root` (non la
chiede). `telnetd -h` spiega i valori.

! **`-s` E' UNA PORTA APERTA, E VA SCELTO SAPENDOLO.** Su una rete di casa, per
  provare i driver di una macchina in un'altra stanza, è esattamente quel che
  serve. Su qualunque altra rete è una porta aperta. E telnet è in chiaro
  comunque: la password viaggia leggibile sul cavo.

  Ora che hai i conti veri, `telnetd &` senza `-s` costa una digitazione in
  più e chiude la porta.

! **`avvio.sh` LO RISCRIVE `hwconfig`**, per intero: una riga aggiunta a
  mano sparisce alla configurazione dopo. Per questo telnetd ha il suo file
  di configurazione, e i driver hanno `-install` (vedi 4.9).

Dalla versione 0.279 la sessione telnet è un terminale vero, come la console:
i programmi che chiedono il modo grezzo (editor, ncurses) ricevono i tasti
uno per uno, e la misura della finestra è quella del tuo client telnet.
Se un programma si chiude male e lascia il terminale senza eco, basta
tornare al prompt: la shell rimette i modi normali da sola.

---

## 4.4 Gli strumenti

```
netupdate -install:base       gcc e la catena del C
netupdate -install:build      make e i binutils
netupdate -install:curses     ncurses: curses, terminfo, tic, tput
netupdate -install:zlib       e bzip2, xz, lz4, zstd, pcre2, fmt: le
                              librerie (con i programmi xz, zstd...)
netupdate -install:ssl        OpenSSL 4.1.0: libssl, libcrypto e il
                              programma openssl
netupdate -install:mariadb    MariaDB 11.8.9: il server SQL, i client,
                              il servizio e i connettori (vedi 4.10)
netupdate -install:mariadb-connector
                              solo i connettori, per i programmi che
                              parlano con un MariaDB/MySQL (vedi 4.10)
```

Gli strumenti stanno su un DVD, non più su un CD: con MariaDB (circa 500 MB
per architettura, sorgenti compresi) non entrano più in 700 MB.

Finiscono in `/exos/bin`, che dalla versione 0.217 è nel PATH predefinito.

Se il tuo `kernel.cfg` è più vecchio, la riga è questa:

```
PATH        = /bin:/dev:/exos/bin:/cdrom/exos/bin:/cdrom/bin
```

Dal netupdate 0.035 la aggiunge da solo anche `netupdate -install:<gruppo>`
(prima lo faceva solo `-check`), con la copia di prima in `kernel.cfg.bak`,
e vale dal riavvio. Senza, la shell trovava `gcc` per nome ma `gcc` non
collegava: «collect2: fatal error: cannot find 'ld'», perché `ld` si cerca
nel PATH di `kernel.cfg`.

---

## 4.5 La grafica

La risoluzione la imposta **Stage 2 con il BIOS**, prima del modo protetto:
dopo, il BIOS non è più raggiungibile. Per questo si sceglie una volta e si
riavvia.

```
/dev/svga.drv 800x600        poi  reboot
/dev/svga.drv -n 800x600     dice cosa farebbe, senza scrivere
/dev/svga.drv testo          torna alla console 80x25
```

! **`sis.drv` NON E' QUESTO.** Non cambia risoluzione: rimette la scheda nello
  stato che il BIOS le aveva dato, ed esiste per **recuperare il testo da modo
  protetto** quando il server grafico è morto e lo schermo è congelato. Se gli
  passi `800x600` ti risponde con l'aiuto, perché lui vuole `-800`.

### Se la console è lenta

A 800x600 a 32 bit ogni riga che scorre sposta **1,8 MB** di memoria video:
non passa dalla cache e va sul bus. Su una macchina dei primi anni duemila si
sente.

```
/dev/svga.drv 800x600 -16      poi  reboot
```

`-16` dimezza i byte da spostare (900 KB invece di 1,8 MB) al prezzo delle
sfumature: il 5-6-5 butta i bit bassi di ogni componente. Su una macchina
veloce non conviene, e infatti il predefinito resta 32.

L'altra leva è la risoluzione: 640x480 sposta il 36% in meno di 800x600.

### Il pannello alla risoluzione nativa (NVIDIA G80, Compaq CQ60)

Il BIOS video di alcuni portatili non offre la risoluzione del pannello: il
CQ60 ha un pannello 1366x768 e il BIOS si ferma a 1280x720, che il pannello
allarga e sfoca. `nv50.drv` programma da sé il display della scheda:

```
nv50.drv -stato          com'è adesso (non tocca niente)
nv50.drv -nativo         pannello e console a 1366x768 adesso
nv50.drv -install        a ogni avvio; -remove lo toglie
```

! **NON SOSTITUISCE `svga.drv`**: parte dal modo VESA che `svga.drv` ha
  scelto (va bene 1024x768). Su una macchina con un'altra scheda o un altro
  pannello `-avvio` esce senza dire niente.

! **VA FATTO PRIMA DELLA SCRIVANIA**: con ExWin acceso il kernel rifiuta il
  cambio. Da `-install` in poi succede da sé, prima del prompt.

La console arriva a 170 colonne per 48 righe.

! **SULLE INTEGRATE LO SCHERMO STA IN RAM** (MCP77/79, come il CQ60): la
  «memoria video» è RAM che il BIOS ha messo da parte, e da `nv50.drv` 0.008
  console e scrivania la scrivono lì direttamente invece che attraverso la
  scheda. Sul CQ60 il processore scrive lo schermo a circa 4000 MB/s invece
  di 47: un fotogramma intero passa da 90 ms a 3. `-nativo` dice
  l'indirizzo («framebuffer a 0xb0800000»).

Il motore 2D della scheda (riempimenti e copie fatti dalla GPU) è in prova,
e vuole il pannello già nativo:

```
nv50.drv -2dprova        accende il motore, disegna, rilegge: «giusto» o MALE
nv50.drv -2dmisura       processore contro motore, in MB/s
nv50.drv -2dservizio &   il motore offerto alla scrivania (accel2d)
```

Finché le prove non sono passate sul portatile non entra in `-install`, e
la scrivania a 1366x768 disegna con il processore.

### Se resta in testo dopo il riavvio

Non è un guasto silenzioso: **Stage 2 dice perché**, con una riga che comincia
per `VESA:`. Se la scheda non offre quella risoluzione con framebuffer
lineare, ripiega sul testo invece di lasciarti uno schermo nero.

---

## 4.6 ExWin

```
exwin
```

Vuole due cose, e le dice se mancano:

- **lo schermo già in grafica** (vedi sopra: serve un riavvio)
- `/exwin/bin/wserver` e `/exwin/bin/pm`, che stanno nel pacchetto `sistema`

! **NON SERVE ESSERE root.** Il server grafico mappa il framebuffer con
  `fb_map()`, che non chiede privilegi. Finché stava in `/dev` — che è di
  root — un utente normale non poteva eseguirlo: è stato spostato apposta.

---

## 4.7 Il mouse e il touchpad

Non serve un driver a parte: **il PS/2 sta dentro `kbd.drv`**, perché mouse e
tastiera condividono lo stesso controller. C'è l'abilitazione della seconda
porta, l'IRQ 12, il reset e il report.

Per provarlo:

```
/bin/mouse
```

Un mouse **seriale** è invece hardware separato, e ha il suo driver:

```
/dev/mouseser.drv           COM2 (0x2F8, IRQ3)
/dev/mouseser.drv com1      COM1
/dev/mouseser.drv -i        dice solo se la UART c'e'
```

---

## 4.8 Il disco di sistema a macchina accesa

Dal kernel 0.269 il sistema parte da un **sistema di base** in memoria
(`/rdboot`: shell, login, `mount`, `ps`, `kill`, `fdisk`, `chkdsk`,
`system`, i driver dei dischi) e monta il disco di sistema **sopra** `/`.
Il disco si può quindi togliere e rimettere senza riavviare:

```
system -status               chi fa da radice
system -unload               chiude chi usa il disco e lo smonta
fdisk hd0                    ripartizionare il disco di sistema
chkdsk -r hd0p1              o controllarlo
system -load                 lo rimette e riaccende rete, telnetd, ...
system -load -check hd0p1    lo controlla prima di rimontarlo
```

! **`-unload` CHIUDE TUTTO CIO' CHE PARTE DAL DISCO**: la rete, telnetd, la
  scrivania, la barra di stato. Prima elenca e chiede; restano tastiera e
  console. Da una sessione telnet si chiude anche la sessione: meglio dalla
  tastiera, o tutto in una riga
  (`system -unload -yes; chkdsk -r hd0p1; system -load`).

! **DOPO UNO SPEGNIMENTO BRUSCO** l'avvio trova il disco segnato «in uso»,
  lo controlla con `chkdsk -r` mentre è smontato, e poi lo monta. Lo dice a
  schermo; i dettagli con `dmesg SISTEMA`.

! **SE `/rdboot` NON C'E'** la macchina è stata aggiornata da un kernel 0.268
  o prima: vedi [3.4b](03-dalla-rete.md).

Chi tiene occupato un montaggio lo dicono `ps -m /` e `umount`, quando
rifiuta.

### L'area di scambio (swap) su una macchina già installata

L'area di scambio è una partizione **primaria di tipo 0x82** sullo stesso
disco o su un altro. Sul disco di sistema va creata a disco tolto, **dalla
tastiera** (non da telnet: `-unload` chiude anche telnetd):

```
system -unload               chiude chi usa il disco e lo smonta
fdisk hd0                    spazio libero -> nuova primaria, tipo 82
                             (scambio), poi scrivi ed esci
mkswap hd0p2                 la firma dell'area (chiede conferma)
system -load                 rimette il disco
```

Poi in `/boot/kernel.cfg`, nella sezione `[kernel]`:

```
swap = hd0p2
```

e si riavvia: l'area si accende **solo all'avvio**, e il kernel scrive
quanti slot da 4 KB ha trovato (`dmesg SWAP`).

**Funziona, a 32 e a 64 bit**, dal kernel 0.276 (24 ottobre 2026). La
prima versione, di agosto, si accendeva ma sotto pressione no: a 32 bit il
kernel si fermava al primo sfratto, a 64 la memoria dei programmi si
rovinava. Dal 0.276 la prova `tools/prova_swapmem.sh` riempie la RAM con
`swaptest` (uno da solo e due insieme), manda sul disco decine di migliaia
di pagine, le riprende e controlla che ognuna torni **identica**: passa,
anche col kernel 0.279. Sul portatile CQ60 l'area di 4 GB si accende
(1048575 slot), ma finora la RAM è sempre bastata e non è mai stata usata
davvero: la prova sotto pressione sul ferro manca ancora.

Per sapere se sulla tua macchina è accesa:

```
dmesg SWAP          deve dire «SWAP: 'hd0p2' attiva: N slot da 4 KB (M MB)»
mem                 la riga «scambio (swap)» con la dimensione
```

Se `mem` dice **«nessuna area attiva»**, `dmesg SWAP` dice perché: la
partizione non ha la firma (manca `mkswap`), il nome in `kernel.cfg` non
è quello giusto, o la firma dichiara più slot di quanti ne stiano nella
partizione. Per provarla davvero, dal CD: `/cdrom/bin/swaptest` chiede il
doppio della RAM libera e controlla ogni pagina (`swaptest 300` chiede
300 MB); poi `mem` deve mostrare pagine andate e tornate.

Cosa va sull'area: le pagine **private** dei programmi (heap, stack,
dati). Non ci vanno mai: il kernel, le pagine condivise fra due processi
(memoria condivisa, codice in comune), le pagine che un dispositivo usa
(DMA, registri della scheda, schermo) e quelle di un programma che in quel
momento gira sull'altro processore.

Se e quanto viene usata lo dice `mem`, nella riga **scambio (swap)**
sotto il totale: la dimensione, quanto c'è sopra adesso e quante pagine
sono andate sul disco e tornate in RAM dall'avvio. Finché la RAM basta i
due contatori restano a zero: l'area entra in gioco solo quando la
memoria finisce.

! **NON PIU' DI 4 GB**: il kernel conta al massimo 2^20 pagine da 4 KB.
  Una partizione più grande funziona, ma il di più resta inutilizzato.

! **UN PROGRAMMA HA SEMPRE AL MASSIMO 3 GB** di indirizzi, swap o no: lo
  scambio dà più memoria alla macchina, non a un programma solo. E una
  richiesta singola oltre 2 GB la rifiuta la libc (`sbrk` conta in 32 bit
  con segno), prima ancora che si arrivi allo swap.

! **È LENTO, come ovunque**: una pagina che torna dal disco costa qualche
  millisecondo. Serve a non far morire un programma quando la RAM finisce,
  non a lavorare con metà della memoria sul disco.

! I nomi `hd0` e `hd0p2` sono quelli di esempio: il disco giusto lo dice
  `system -status` (il disco di sistema) e `disk` (le partizioni).

---

## 4.9 I driver all'avvio: `-install`

Un driver si fa partire a ogni avvio **con il driver stesso**, non
scrivendo a mano negli script:

```
nv50.drv -install            scrive il suo blocco in /boot/avvio.sh
nv50.drv -remove             lo toglie
```

Il blocco comincia con `# -install <driver>` e finisce alla prima riga
vuota; `hwconfig`, che riscrive `avvio.sh`, lo riporta avanti. Oggi lo ha
`nv50.drv`; gli altri driver lo ricevono man mano. Fino ad allora vale il
vecchio modo: la riga in `avvio.sh` (che `hwconfig` riscrive) o la voce in
`[modules]` di `kernel.cfg` per ciò che deve esserci prima di tutto, come la
tastiera.

---

## 4.10 Il server MariaDB

```
netupdate -install:mariadb    (o il gruppo [mariadb] del DVD degli strumenti)
mariadb-svc -install          una volta, da root
```

`-install` crea l'utente `mysql` (senza password: nessuno entra come
`mysql`), scrive `/etc/my.cnf`, crea le tabelle di sistema in
`/var/lib/mysql` (un minuto o due), crea il certificato TLS del server (una
chiave EC P-256, valida 10 anni), mette il suo blocco in `/boot/avvio.sh` e
accende il server. Il certificato si fa una volta sola: senza, MariaDB 11.8
ne genererebbe uno RSA da 4096 bit a ogni avvio, e su una macchina lenta
vuol dire da venti secondi a qualche minuto di attesa ogni volta. Da quel momento MariaDB parte **a ogni avvio, prima del
login**, e allo spegnimento si chiude da solo: il kernel manda SIGTERM a ogni
programma e aspetta fino a 30 secondi prima di fermare i dischi.

```
mariadb -u root               il client (legge [client] in /etc/my.cnf)
mariadb-svc -stato            gira? risponde?
mariadb-svc -ferma            lo ferma e aspetta che abbia chiuso
mariadb-svc -avvia            lo riaccende
mariadb-svc -remove           lo toglie dall'avvio (dati e my.cnf restano)
```

! **SOLO TCP, SOLO DA QUESTA MACCHINA.** EX-OS non ha i socket AF_UNIX: il
  server ascolta su `127.0.0.1:3306`, e i client ci vanno da soli. Funziona
  anche senza scheda di rete: dalla versione 0.014 `ip.drv` resta acceso con
  il solo 127.0.0.1 quando non trova la scheda. Per aprirlo alla rete si
  cambia `bind-address` in `/etc/my.cnf`, e prima si mette una password.

! **root@localhost NON HA PASSWORD** all'inizio, come dopo
  `mariadb-install-db`. Mettila:

  ```
  mariadb -u root -e "ALTER USER root@localhost IDENTIFIED BY '...'"
  ```

Il registro è `/var/log/mariadb/mariadb.log`; quello che `mariadbd` stampa da
sé va in `/var/log/mariadb/mariadbd.out`. Le misure in `/etc/my.cnf` sono
quelle di EX-OS: 1024 connessioni servite da un gruppo di fili
(`thread_handling = pool-of-threads`, al massimo 64 fili), un buffer pool di
64 MB che può crescere fino a 256 (InnoDB riserva in anticipo lo spazio di
indirizzi per il massimo, e un programma di EX-OS ne ha 3 GB).

! **NON METTERE `thread_handling = one-thread-per-connection`.** Un filo per
  connessione vuol dire 128 KB di memoria del kernel ciascuno, nei primi
  64 MB: dal kernel 0.282 non c'è più un numero fisso di posti, ma quella
  fascia ne tiene circa 400 in tutto il sistema, e con quella riga il server
  si ferma lì. Il gruppo di fili usa `epoll`, che
  c'è dalla versione 0.016 di `ip.drv`: un `/etc/my.cnf` scritto da
  `mariadb-svc` 0.002 o prima ha ancora `max_connections = 16` e
  `-install` non lo tocca (lo dice): si cambia a mano, in `[mysqld]`,
  `max_connections = 1024` e si aggiungono `thread_handling =
  pool-of-threads` e `thread_pool_max_threads = 64`, poi
  `mariadb-svc -ferma` e `-avvia`.

! **UN FILE ARRIVA A 2 GB**, su tutte e due le architetture: è un limite del
  kernel di oggi. Oltre, la libc rifiuta la scrittura (EFBIG, «file troppo
  grande») invece di scrivere in un punto sbagliato del file. Tienilo
  presente per `ibdata1` e per le tabelle grandi.

### I connettori: parlare con MariaDB dai propri programmi

Due voci nel catalogo:

```
netupdate -install:mariadb            il server, i client, mariadb-svc
                                      E i connettori
netupdate -install:mariadb-connector  SOLO i connettori: per una macchina
                                      che parla con un MariaDB o un MySQL
                                      che sta altrove (o qui, installato poi)
```

I connettori si portano dietro da soli quello con cui si collegano:
OpenSSL (`[ssl]`), zlib, zstd e il C++ (`[cpp]`, per Connector/C++), e
`libpthread.a` sta in `[base]`. Se un `gcc ... -lmariadbclient` dice
«cannot find -lssl» (o `-lz`, `-lzstd`, `-lpthread`), il disco viene da un
netupdate o da un toolinst più vecchi: `netupdate -install:ssl`,
`-install:zlib`, `-install:zstd`, oppure `netupdate -check`.

Cosa c'è dentro, tutto statico come ogni programma di EX-OS, a 32 e a 64
bit:

| linguaggio | cosa | come si usa |
|---|---|---|
| C | Connector/C 3.4.11, `<mysql.h>` | `gcc prog.c -I/exos/include/mariadb -lmariadbclient -lssl -lcrypto -lz -lzstd -lpthread -lm` |
| C++ | Connector/C++ 1.1.9, `<conncpp.hpp>` (lo stile JDBC: `sql::Driver`, `PreparedStatement`, `ResultSet`) | `g++ prog.cpp -I/exos/include/mariadb -lmariadbcpp` e le stesse del C |
| FreeBASIC | `mariadb.bi`, l'API del C (sta in `include/freebasic` e arriva col gruppo `[fb]`, come gli altri .bi; le librerie col gruppo dei connettori) | `#include "mariadb.bi"`, poi `fbc prog.bas -x prog`: le librerie le chiede il .bi |
| Rust | il crate `mariadb` in `/exos/lib/rust/mariadb`, senza dipendenze | `rustc -O --crate-type rlib --edition 2021 /exos/lib/rust/mariadb/src/lib.rs`, poi `rustc prog.rs --extern mariadb=libmariadb.rlib` |

`mariadb_config --cflags --libs` dice le opzioni del C. Il collegamento è
**solo TCP**: `127.0.0.1` e la porta 3306 per il server di questa macchina
(EX-OS non ha i socket AF_UNIX, quindi niente `localhost` via socket).

! **IL `mysql.bi` DI FREEBASIC NON SERVE**: è del 2003 (MySQL 4.0), ha le
  strutture di allora e cerca una `libmysqlclient` che non c'è. Si usa
  `mariadb.bi`.

! **RUST NON HA CARGO DENTRO EX-OS**: per questo il crate è un file solo,
  senza dipendenze, da compilare con `rustc`. I driver di crates.io
  (`mysql`, `sqlx`) vorrebbero decine di crate. Il gruppo `[rust]` (rustc
  dentro EX-OS) c'è solo a 64 bit, e solo quando è stato costruito; i
  programmi Rust si possono anche compilare fuori (`tools/rust-exos`).

Provati dentro EX-OS contro il server, a 32 e a 64 bit
(`tools/prova_mariadb.sh`): un programma per linguaggio si collega, crea una
tabella, inserisce righe (con un'istruzione preparata in C, C++ e
FreeBASIC), le rilegge e riconosce l'errore di una tabella che non c'è.
FreeBASIC è provato a 32 bit: il FreeBASIC a 64 bit per EX-OS va ricostruito
(`tools/freebasic-exos/prepara-fb64.sh`), e allora `mariadb.bi` vale anche lì.

## 4.11 Il server web Apache

```
netupdate -install:apache     (o il gruppo [apache] del DVD degli strumenti)
/exos/apache2/bin/httpd -X -d /exos/apache2 -f conf/httpd-exos.conf &
```

Apache httpd 2.4.69 con il MPM worker, **in un processo solo** (`-X`): EX-OS
non ha ancora `fork`, quindi non c'è il processo padre che ne fa partire
altri; tutte le connessioni sono fili di quel processo. La configurazione è
`/exos/apache2/conf/httpd-exos.conf`: porta 80, le pagine in
`/exos/apache2/htdocs`, i programmi CGI in `/exos/apache2/cgi-bin`
(programmi compilati: lo script di shell con `#!` non c'è), lo stato del
server su `/server-status` solo da 127.0.0.1, il registro in
`/exos/apache2/logs`. Parte da root e serve come utente 100 (`User #100`).

```
/exos/apache2/bin/httpd -k stop -d /exos/apache2 -f conf/httpd-exos.conf
/exos/apache2/bin/ab -n 1000 -c 20 http://127.0.0.1/     misura la velocità
```

! **QUANTI FILI: `ThreadsPerChild`, `ThreadLimit` e `MaxRequestWorkers`,
  tutti e tre uguali** (256 nella configurazione di EX-OS). Dal kernel 0.282
  un programma non ha un numero fisso di fili; il tetto è quello del
  sistema, circa 400 fra processi e fili (128 KB di pila del kernel
  ciascuno nei primi 64 MB), e `procmax` in `/boot/kernel.cfg` può
  abbassarlo. Se Apache ne chiede più di quelli che ci sono, il registro dice
  `AH03142: ap_thread_create: unable to create worker thread`.

! **I CGI PARTONO CON `spawn_ex`**, non con `fork` ed `exec`: il programma
  riceve la richiesta sullo standard input e le variabili CGI
  nell'ambiente, e parte nella sua cartella, come su Linux.

! **mod_ssl c'è** (OpenSSL 4.1): per HTTPS si aggiungono `Listen 443` e un
  `<VirtualHost>` con `SSLCertificateFile` e `SSLCertificateKeyFile`.
  `mod_http2`, `mod_md`, `mod_lua`, `mod_proxy_html`, LDAP e `cgid` non ci
  sono.

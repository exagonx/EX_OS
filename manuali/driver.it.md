# EX-OS — Driver e hardware

[🇬🇧 English](driver.md) · **🇮🇹 Italiano**

[← README](../README.it.md) · [Tutti i manuali](../README.it.md#i-manuali)

**Indice**

- [Disposizione della tastiera: `keymap`](#disposizione-della-tastiera-keymap)
- [`hwconfig` — configurare senza leggere niente](#hwconfig--configurare-senza-leggere-niente)
- [`help helpconfig` — la procedura, e a che punto sei](#help-helpconfig--la-procedura-e-a-che-punto-sei)
- [Driver ring3 (modello attuale, da luglio 2026)](#driver-ring3-modello-attuale-da-luglio-2026)
- [Accessi I/O a 16 e 32 bit](#accessi-io-a-16-e-32-bit)
- [Il bus PCI: `/dev/pci.drv` e `/bin/netdetect`](#il-bus-pci-devpcidrv-e-binnetdetect)
- [Interrupt: `irq_bind` e `irq_done` vanno in coppia](#interrupt-irq_bind-e-irq_done-vanno-in-coppia)
- [Rete: `/dev/ne2k.drv` e `/bin/nettest`](#rete-devne2kdrv-e-binnettest)
- [Rete: `/dev/pcnet.drv` — la prima scheda che scrive in RAM da sola](#rete-devpcnetdrv--la-prima-scheda-che-scrive-in-ram-da-sola)
- [La cache dei settori: il compilatore va il doppio](#la-cache-dei-settori-il-compilatore-va-il-doppio)
- [! `rep insw` invece di 256 chiamate a settore](#-rep-insw-invece-di-256-chiamate-a-settore)
- [Interfaccia `drv_*` (modello precedente, kernel-space)](#interfaccia-drv_-modello-precedente-kernel-space)

---

## Interfaccia driver

### Disposizione della tastiera: `keymap`

```ini
[kernel]
keymap = it        # us it fr de es uk
```

```
keymap            quale c'è adesso, e quali si possono avere
keymap it         passa a quella italiana, subito
keymap -p         stampa la riga da mettere in kernel.cfg
```

La legge `/dev/kbd.drv` all'avvio, **prima di registrarsi** — cioè prima
che qualcuno possa digitare: leggerla dopo lascerebbe una finestra in cui
i primi tasti vengono tradotti con la disposizione sbagliata, e sono
proprio quelli dell'autoexec. Un nome sconosciuto non ferma niente: il
driver lo dice e tiene `us`, perché una tastiera muta per un refuso nella
configurazione è un sistema che non si può usare nemmeno per correggere
quel refuso.

! **Ogni disposizione sono QUATTRO tabelle, non due**: normale, Shift,
AltGr, AltGr+Shift. Sembra un lusso finché non si prova a scrivere una
funzione — su una tastiera italiana le graffe stanno **solo** su
AltGr+Shift:

```
@  AltGr+ò        [  AltGr+è        {  AltGr+Shift+è
#  AltGr+à        ]  AltGr++        }  AltGr+Shift++
```

Una disposizione che si ferma a tre tabelle dà una tastiera con cui non si
può aprire un blocco, e questo sistema ci porta dentro un editor e un
compilatore C.

! **Le lettere accentate sono byte della code page 437**: la `à` è 0x85,
non UTF-8. È l'unico byte che la VGA disegna. Conseguenza dichiarata: con
quei byte finiscono anche nei nomi di file, e chi legge quei file su Linux
vede caratteri diversi. Non è un difetto della tabella: è che EX-OS non ha
una codifica di sistema, e sceglierne una è una decisione più grande di
una disposizione di tastiera.

! **Non ci sono i tasti morti.** Su una francese o una tedesca `^` e `¨`
scrivono sé stessi invece di aspettare la vocale. Farli funzionare vuol
dire uno stato in più nel driver e una tabella di combinazioni per
disposizione; per ora si dichiara che non ci sono, invece di farli
sembrare rotti.

**Due difetti trovati provandola**, entrambi lontani da dove li si
cercava:

| | |
|---|---|
| la shell buttava via gli accenti | `riga_modifica` accettava `k >= 32 && k < 127`, cioè «solo ASCII». Invisibile finché la tastiera è stata americana: con quella italiana la `ò` arrivava dal driver e la shell la scartava, e il tasto sembrava rotto |
| AltGr non esisteva | `e0 38` finiva nel blocco dei tasti estesi — quello che consegna le sequenze ANSI dei cursori — e usciva dal suo `default: return` prima di arrivare al codice che lo gestiva. Il codice c'era ed era giusto: stava solo dopo |

! **`us` e `it` sono verificate tasto per tasto** in QEMU, mandando il
tasto *fisico* e guardando che carattere ne esce. Le altre sono scritte
dalla disposizione nota e provate solo dove cambiano posizione rispetto a
US: sono utilizzabili, ma chi ha quella tastiera davanti e trova un tasto
sbagliato ha trovato un difetto vero, non un limite.

! **Se si sbaglia disposizione, la via d'uscita è il riavvio.** Non c'è un
comando digitabile da tutte: fra QWERTY le lettere non si muovono, ma su
AZERTY la `a` e la `m` cambiano posto e `keymap it` battuto alla cieca
diventa `keyq,p`. Il cambio a caldo però **non è permanente**: si riavvia e
torna quella di `kernel.cfg`. È il motivo per cui `keymap` non scrive su
nessun file — a scriverlo è `hwconfig`, che conserva quella che trova.

### `hwconfig` — configurare senza leggere niente

```
hwconfig            guarda, propone, chiede, scrive
hwconfig -n         guarda e basta
hwconfig /disco     configura il sistema installato lì dentro
```

`kernel.cfg` si scrive a mano, e per scriverlo bisogna già sapere che i
dischi si chiamano `hd0p1`, che i punti di montaggio non devono esistere,
che i moduli sono processi ring3 e che l'ordine dei comandi di rete non è
modificabile. Sono tutte cose vere, tutte documentate qui sopra, e tutte da
leggere **prima** di poter accendere una macchina.

`hwconfig` le sa già:

```
Cosa c'e' in questa macchina

  tastiera   /dev/kbd.drv — si carica all'avvio, serve alle frecce e a gfedit
  lettore    cd0 — montato all'avvio su /cdrom
  volume     hd0p1  ext2   'dati' — montato su /dati
  rete       scheda Ethernet sul bus PCI — si accende all'avvio
```

Poi mostra i due file che scriverebbe, per intero, e chiede. Il round trip
è verificato: analizza, scrive, e la macchina riparte dal disco con la rete
accesa **senza un solo `[WARN]`**.

! **I file di prima finiscono in `.bak`**, ed è ciò che rende la proposta
accettabile: se la macchina non riparte, quello di prima è lì accanto.

! **Il nuovo file è generato, non modificato.** Il `kernel.cfg` che viene
col sistema è lungo duecento righe di spiegazioni; conservarle vorrebbe
dire un parser INI che le rimette a posto, cioè un programma molto più
grande e con molti più modi di sbagliare. Quello scritto qui è corto e
sostituisce il precedente per intero — detto in chiaro **prima** di
chiedere.

Tre scelte che si vedono solo provandolo:

| | |
|---|---|
| **non guarda quale scheda sia** | la tabella dei modelli sta in `netdetect`, e duplicarla darebbe due elenchi che divergono al primo driver nuovo. L'autoexec generato chiama `netdetect -c`, che quella tabella ce l'ha: a `hwconfig` serve sapere **se** c'è una scheda, non quale |
| **il volume che sarà la radice non finisce in `[mount]`** | il kernel se ne accorgerebbe da solo («è già montato altrove»), ma è una riga che non serve dentro un file che qualcuno leggerà per capire la propria macchina |
| **un'etichetta sfortunata non diventa un punto di montaggio** | un volume etichettato `boot` darebbe `/boot = hd0p1`, che il kernel rifiuta — un `[WARN]` a ogni accensione, e chi lo legge non ha motivo di sospettare l'etichetta del disco. In quel caso si ripiega su `/disco` |

Scrive anche `TMPDIR`, che non è un vezzo: `mkstemp` e il driver del
compilatore ci mettono i file di passaggio, e senza finiscono nella radice
— che avviando da CD è in sola lettura.

Sta **sul floppy**, con `fdisk` e `install`: serve proprio quando si prepara
una macchina, cioè quando il CD magari non c'è ancora. Senza `/dev/pci.drv`
configura montaggi e moduli e dice che la parte di rete non ha potuto
verificarla.

### `help helpconfig` — la procedura, e a che punto sei

```
help helpconfig     (oppure `helpconfig` da solo)
```

Spiega come si accendono i driver — la catena di rete, la configurazione a
mano, la diagnosi, l'autoexec — e **mostra lo stato attuale** chiedendo al
registro IPC chi c'è già:

```
A che punto sei adesso

  [ok]    bus PCI          /dev/pci.drv &
  [manca] scheda di rete   netdetect -c
  [manca] stack IP         /dev/ip.drv &
  [ok]    tastiera         [modules] in /boot/kernel.cfg
```

! **Lo stato è il motivo per cui esiste.** Un elenco di comandi da dare sta
già in questo file; quello che al prompt non si sa è a che punto si è
arrivati. Costa una syscall per servizio e trasforma «ecco la procedura» in
«sei qui». L'esempio sopra è una macchina senza scheda di rete: il bus c'è,
la scheda no, e non è un guasto da inseguire.

Il testo è più lungo di uno schermo da 25 righe e si ferma da solo; le pause
stanno dove cambia argomento, non ogni N righe, perché una pagina
interrotta a metà di un elenco è peggio di una più corta. `q` smette.

### Driver ring3 (modello attuale, da luglio 2026)

Un driver è un normale eseguibile ELF32 **ET_EXEC statico**, come i programmi
di `/bin`. Non esegue istruzioni privilegiate: il kernel media ogni accesso
all'hardware e verifica i permessi a ogni chiamata.

```c
int main(void)
{
    ipc_register("kbd");            /* si fa trovare per nome    */
    ioport_bind(0x60, 5);           /* whitelist porte I/O       */
    irq_bind(1);                    /* IRQ -> messaggi IPC       */

    for (;;) {
        IpcMessage m;
        ipc_recv(&m, buf, sizeof buf);

        if (m.sender_pid == IPC_SENDER_KERNEL &&
            m.type == IPC_TYPE_IRQ_NOTIFY) { /* interrupt hardware */ }
        else                                 { /* richiesta client  */ }
    }
}
```

I client trovano il servizio con `ipc_lookup("kbd")` e dialogano via
`ipc_send`/`ipc_recv`. Riferimento completo: `drivers/kbd/kbd.c` e il
protocollo in `drivers/kbd/kbd_proto.h`.

### Accessi I/O a 16 e 32 bit

Oltre a `ioport_in`/`ioport_out`, che lavorano a byte, un driver ha:

```c
int ioport_in16 (unsigned int porta);                    /* 0..65535, o -errno */
int ioport_out16(unsigned int porta, unsigned int val);
int ioport_in32 (unsigned int porta, unsigned int *out); /* 0, o -errno        */
int ioport_out32(unsigned int porta, unsigned int val);
```

Non sono una comodità. Il registro CONFIG_ADDRESS del bus PCI (0xCF8) **deve**
essere scritto con un singolo accesso a 32 bit: uno a byte o a word non viene
riconosciuto dal ponte come ciclo di configurazione, e siccome 0xCF9 è il
registro di reset di molti chipset, scriverlo a pezzi tende a riavviare la
macchina.

! `ioport_in32` **non restituisce il valore letto**: `0xFFFFFFFF` («nessun
dispositivo») come `int` sarebbe `-1`, indistinguibile da un errore. Il valore
esce dal puntatore. `ioport_in16` non ha il problema e lo restituisce.

La porta deve essere allineata all'ampiezza, altrimenti `-EINVAL`: un accesso
disallineato viene spezzato dal chipset in due cicli e sul bus PCI il secondo
non è più un ciclo di configurazione.

### Il bus PCI: `/dev/pci.drv` e `/bin/netdetect`

L'enumerazione PCI è un **processo ring3**, non codice del kernel: legge
tabelle scritte da BIOS e firmware di terzi, e un ciclo che non termina su un
ponte mal formato dev'essere un processo da rilanciare, non una macchina
bloccata.

```
/dev/pci.drv -l          elenca i dispositivi ed esce
/dev/pci.drv &           registra il servizio "pci" e serve i client
netdetect                schede di rete presenti e driver di ciascuna
netdetect -t             tabella dei modelli riconosciuti
```

Esempio (VirtualBox con la scheda predefinita):

```
ex-os:/> /dev/pci.drv &
[1] 8
pci: servizio 'pci' attivo, 8 dispositivi
ex-os:/> netdetect
00:04.0  1022:2000  AMD PCnet-PCI II / FAST III (Am79C970/C973)
           porte I/O da 0xc140, IRQ 11
           driver: /dev/pcnet.drv
```

Il protocollo è in `drivers/pci/pci_proto.h`. Il server espone lettura della
configurazione e `PCI_MSG_ABILITA`/`DISABILITA` sui bit I/O, memoria e bus
master; **non** una scrittura di configurazione generica, perché riprogrammare
i BAR di un dispositivo che il kernel sta usando (per esempio il controller
ATA) toglierebbe il disco da sotto i piedi a chi di quella scrittura non sa
nulla.

Entrambi vivono **solo sul CD di EX-OS** (`make iso-exos`): il floppy serve ad
avviare e a installare, e gli strumenti di rete senza i driver di rete — che
sul floppy non ci starebbero — non servirebbero a niente una volta lì.

### Interrupt: `irq_bind` e `irq_done` vanno in coppia

```c
irq_bind(11);                    /* da qui gli IRQ arrivano come IPC */
...
/* alla notifica: */
servi_la_scheda();               /* azzera lo stato del dispositivo */
irq_done(11);                    /* SOLO ADESSO si riapre la linea */
```

! `irq_done()` non è facoltativa. Il kernel **maschera** l'IRQ nel PIC
prima di consegnare la notifica, e senza questa chiamata la linea resta
chiusa: il driver riceve un interrupt e poi silenzio.

Il motivo è che un driver ring3 non gira dentro l'interrupt: fra la
notifica e il momento in cui tocca la scheda passano dei tick. Su un IRQ
**a livello** — tutti quelli PCI — il dispositivo tiene la linea alta
finché non gli si azzera il registro di stato, quindi senza mascheramento
l'interrupt riparte subito dopo l'`iret` e il processo driver non riceve
mai la CPU per andare ad azzerarlo. La macchina si ferma, senza panic.

L'ordine conta: prima si serve il dispositivo, poi si riapre. Riaprire con
la linea ancora alta rimette in piedi la tempesta.

### Rete: `/dev/ne2k.drv` e `/bin/nettest`

Il primo driver di rete guida la famiglia NE2000/DP8390 — RTL8029 su PCI e
cloni Winbond/VIA/KTI. Sta in userspace perché quella scheda **non fa DMA
verso la memoria di sistema**: la RAM dei pacchetti è sulla scheda e ci si
arriva da una porta di I/O, quindi il kernel non deve sapere niente di
indirizzi fisici o pagine bloccate.

```
ex-os:/> /dev/pci.drv &
ex-os:/> netdetect -c              # sceglie e avvia il driver giusto
ex-os:/> nettest -a 10.0.2.2
chi ha 10.0.2.2? lo chiede 52:54:00:12:34:56 (10.0.2.15)

  64 byte  52:55:0a:00:02:02 -> 52:54:00:12:34:56  ARP (0x0806)
      ARP risposta: 10.0.2.2 e' 52:55:0a:00:02:02, cerca 10.0.2.15

Risposta ricevuta: 10.0.2.2 ha indirizzo 52:55:0a:00:02:02
```

`nettest` usa **ARP e non ping** di proposito: ARP è il primo scambio
possibile senza avere uno stack, e una risposta dimostra in un colpo solo
che la scheda trasmette, che il frame arriva, che la scheda riceve e che
la catena driver → IPC → programma consegna i byte giusti. Se ARP
funziona, a `ping` manca solo software.

Il protocollo (`drivers/net/net_proto.h`) è quello di **ogni** driver di
rete, non della NE2000: il PCnet parlerà la stessa lingua. Due scelte che
valgono per tutti:

- **Il driver non spinge mai un frame non richiesto.** `ipc_send` blocca
  se la mailbox del destinatario è piena, e driver e stack che si spingono
  dati a vicenda finiscono fermi ognuno dentro la propria `ipc_send`. Si
  usa domanda e risposta (`NET_MSG_RICEVI`), come il driver di tastiera.
- **Un battito ogni 250 ms.** `ipc_notify_irq` non blocca: se la mailbox
  del driver è piena quando arriva l'interrupt, la notifica viene
  scartata e la linea resterebbe mascherata per sempre. La scadenza
  sull'attesa fa sì che una notifica persa costi un ritardo, non
  un'interfaccia morta.

! Una NE2000 **ISA** non si cerca da sola: per riconoscerla bisognerebbe
scrivere sulla sua porta di reset, e se lì c'è un'altra scheda le si
scrive addosso. Va dichiarata: `/dev/ne2k.drv -p 0x300 -q 3`.

### Rete: `/dev/pcnet.drv` — la prima scheda che scrive in RAM da sola

AMD PCnet-PCI II / FAST III (Am79C970, C970A, C971, C972, C973: sul bus si
presentano tutte come `1022:2000`). Parla lo stesso protocollo del ne2k,
quindi lo stack IP non sa quale delle due c'è sotto.

! **La differenza con la NE2000 è tutto.** Quella tiene la memoria dei
pacchetti *sulla scheda*, e ci si arriva da una porta di I/O: per questo è
stato il primo driver: non chiedeva niente di nuovo al sistema. Il PCnet è
un **bus master**: legge e scrive la RAM di sistema da solo, agli indirizzi
**fisici** che gli si sono dati, senza passare dalla MMU.

Da qui due cose che prima non esistevano:

| | |
|---|---|
| il bit **bus master** nel comando PCI | senza, il ponte blocca ogni ciclo che la scheda inizia. I registri si leggono e si scrivono benissimo — quelli passano da noi — ma la scheda non riesce nemmeno a leggere il proprio blocco di inizializzazione |
| **`SYS_DMA_ALLOC`** | memoria fisicamente contigua di cui si conosca l'indirizzo fisico |

> ! **Un indirizzo sbagliato qui non dà un errore.** Dare alla scheda un
> indirizzo virtuale invece di uno fisico non produce un fault e non ferma
> niente: produce una scheda che scrive pacchetti in un punto a caso della
> memoria fisica. Su una macchina piccola quel punto è spesso il kernel, e
> il sintomo arriva minuti dopo, altrove. È il motivo per cui `dma_alloc`
> restituisce i due indirizzi separati e con nomi diversi — `virt` per il
> processo, `fisico` per la scheda.

`SYS_DMA_ALLOC` la può chiedere **solo chi ha già una finestra di porte
I/O**. Non è una difesa rigorosa: è il modo di dire che serve ai driver.
Memoria contigua e non liberabile è la risorsa più scarsa che ci sia, e il
tetto è 64 pagine per processo.

Due trappole del formato, entrambe silenziose:

- **BCNT è in complemento a due** su dodici bit, con i quattro bit sopra a
  uno. Un buffer da 2048 byte si dichiara `(-2048) & 0xFFF | 0xF000`;
  scriverci 2048 in chiaro dà una scheda che crede di avere un buffer di
  2048 byte *negativi*.
- **Il reset si fa in WIO**, prima del passaggio a 32 bit, perché
  l'offset del registro di reset è diverso nei due modi.

```
ex-os:/> nettest -c
inviati        7
ricevuti       7
notifiche IRQ  7
battiti        89
```

! **`notifiche IRQ 7` su 7 frame è la riga che conta**, non `ricevuti 7`.
Il driver guarda la scheda anche a ogni battito: senza quel numero, una
rete che funziona con 250 ms di ritardo sarebbe indistinguibile da una che
funziona. È lo stesso controllo che ha scoperto la cascata del PIC mai
smascherata.

! **`-l` non sonda una scheda già guidata.** Per leggerne lo stato
bisognerebbe resettarla, e se un altro processo la sta usando quel reset
gli porta via la rete senza dare un errore a nessuno dei due. Se il
servizio c'è già, `-l` lo *interroga*: la risposta viene da chi la scheda
la sta usando davvero. È successo alla prima prova, e il sintomo era
illeggibile — `CSR0 = 0x3b, atteso STOP`.

### La cache dei settori: il compilatore va il doppio

Il disco rigido ha una cache di 128 settori (64 KB) in `kernel/block/blk.c`.
Il guadagno, misurato cambiando **una sola costante** e ricostruendo:

| `cc1` che compila | tempo |
|---|---|
| senza cache | **19,61 s** |
| con cache | **10,19 s** |

! **Sulla copia sequenziale da 35 MB non cambia niente** (~80-100 s in
entrambi i casi), e la differenza spiega a cosa serve davvero una cache.
Per arrivare a ogni pagina da 4 KB di un file grande, ext2 legge prima i
**blocchi indiretti** che dicono dove sta quella pagina: richieste piccole
e sempre le stesse, ed è lì che la cache toglie lavoro. I dati veri
passano una volta sola e non hanno niente da riusare.

Ne segue una stranezza che sembra un errore di misura e non lo è: `cc1`
**dal CD** girava più veloce dello stesso `cc1` dal disco rigido senza
cache. Non perché il CD sia veloce — perché ISO 9660 non ha blocchi
indiretti da inseguire e legge 2 KB per comando invece di 1 KB. Con la
cache il disco rigido pareggia il CD.

Tre scelte, e il perché:

- **Solo richieste fino a 8 settori.** Una cache si rovina da sola: farci
  passare i 34 MB di `cc1` sfratterebbe ogni settore utile per riempirla
  di dati che nessuno rileggerà mai.
- **Write-through, non write-back.** La scrittura va sul disco *subito*,
  poi aggiorna la copia. Così `vfs_sync` continua a voler dire quello che
  ha sempre voluto dire, e uno spegnimento brutale non perde niente che
  non fosse già perso. Il write-back sarebbe più veloce e sarebbe **un'altra
  promessa**. Il disco ha però una cache sua: dal kernel 0.284 `fsync` gli
  manda FLUSH CACHE.
- **La chiave è (disco fisico, LBA assoluto)**, presa *dopo* la traduzione
  di partizione: gli LBA relativi di due partizioni dello stesso disco si
  sovrappongono, e usare quelli darebbe a una i settori dell'altra.

! **Si svuota su `blk_rescan` e `blk_ripartiziona`.** Senza, dopo un
`mkfs` si servirebbero i settori di prima, e il sintomo sarebbe «un
filesystem corrotto appena creato».

### ! `rep insw` invece di 256 chiamate a settore

Il trasferimento PIO di un settore era un ciclo C che chiamava `port_inw`
**256 volte** — e `port_inw` è una funzione vera, non una macro: 256
`call` e altrettante `ret`, più il montaggio a mano dei due byte di ogni
parola. Duemila e passa istruzioni per 512 byte. Ora è **una** istruzione,
in `ata.c` e in `atapi.c`.

! In ATAPI la via veloce vale **solo se la raffica ci sta nel buffer**:
`rep insw` scrive e basta, non sa saltare i byte in eccesso, e una raffica
va *sempre* consumata tutta o il canale resta inutilizzabile. Quando
sborda si torna al ciclo lento.

! Da solo questo cambiamento **non si vede nelle misure**, ed è
un'informazione utile: il costo del disco non è il trasferimento, sono i
comandi. Quella copia da 35 MB sono ~17.000 comandi ATAPI più ~35.000 ATA,
ognuno con la sua attesa di `BSY` e `DRQ`. La leva che manca è
raggruppare le richieste contigue — e sarà anche ciò che renderà sensato
il DMA bus-master, che oggi ridurrebbe il costo del pezzo che già non pesa.

### Interfaccia `drv_*` (modello precedente, kernel-space)

```c
int  drv_init(void);
int  drv_read(void *buf, size_t n);
int  drv_write(const void *buf, size_t n);
int  drv_ioctl(int cmd, void *arg);
void drv_exit(void);
```

Usata ancora da `drivers/tty/tty.c` (compilato dentro il kernel) e da
`drivers/floppy/floppy.c` (modulo ET_DYN non più caricato). I moduli ET_DYN
girano in ring0 e vanno riscritti contro il modello sopra.

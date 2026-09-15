# 3. Installare e aggiornare dalla rete

[torna all'indice](00-indice.md)

La rete non installa il sistema da zero: **lo riempie**. Il sistema di base
arriva da [floppy](01-da-floppy.md) o da [CD](02-da-cdrom.md); la rete
aggiunge i compilatori, ExWin, OpenSSL, e porta gli aggiornamenti.

---

## 3.1 Il problema dell'uovo: servono i pezzi per prendere i pezzi

Un sistema appena installato dal floppy **non ha gli strumenti di rete**:
`netupdate` parla HTTP tramite `/exwin/lib/exhttp.so`, che il minimale non
contiene. Senza, muore dicendo «non trovo la libreria condivisa della rete».

Il supporto che rompe il cerchio si costruisce così:

```
make netinst-img
```

e produce **due cose con dentro gli stessi file**:

| | cosa | per chi |
|---|---|---|
| `dist/netinst.img` | un floppy da riversare | il kernel sa leggere il lettore |
| `dist/netinst-usb/` | una cartella da copiare | tutti gli altri |

! **NON E' AVVIABILE, ED E' VOLUTO.** Il floppy di avvio resta quello
  minimale, che è collaudato e pieno. Questo serve DOPO aver installato.

### Col lettore di floppy

```
mount fd0 /mnt
/mnt/netinst.sh
```

### Con una chiavetta USB

! **SE IL LETTORE DI FLOPPY STA SULL'USB, QUESTA E' L'UNICA VIA.** Il BIOS quel
  lettore lo sa usare — per questo il dischetto di avvio funziona — ma il
  kernel no: dopo il modo protetto non c'è nessun controller alle porte, e
  `mount fd0` fallisce. La chiavetta invece si monta: i driver USB ci sono e
  `automount` la mette in `/USB/DRIVE0`.

Copia il contenuto di `dist/netinst-usb/` su una chiavetta **già formattata**
(si scrive da qualunque sistema: è FAT), infilala, e:

```
/USB/DRIVE0/netinst.sh
```

! **LA VERSIONE PER CHIAVETTA NON ACCENDE LA RETE, E DICE DI RIAVVIARE.** Il
  floppy arriva su un sistema appena installato dove la rete non c'è per
  costruzione, e lì accenderla è tutto il senso dello script. La chiavetta
  arriva su una macchina che la rete ce l'ha già, e ripartire i driver sopra
  quelli che girano vuol dire due `pci.drv` e due `ip.drv` che si contendono
  la stessa scheda — con il risultato che la macchina sparisce dalla rete.

---

## 3.2 Dire da dove ci si aggiorna

```
netupdate -repo:esempio.org/exos/netinst
netupdate -check
```

`-repo:` cerca `<indirizzo>/repo.txt` e ne prende trasporto e radice.
`-check` guarda cosa è cambiato sul server.

---

## 3.3 Aggiungere i pacchetti

`-check` aggiorna quel che **c'è già**. Per aggiungere:

```
netupdate -install:list          cosa c'e' sul server
netupdate -install:base          gcc, cpp, as, ld, la libc
netupdate -install:build         make, ar, ranlib, nm, strip, objcopy, objdump
netupdate -install:cpp           C++
netupdate -install:ssl           OpenSSL
netupdate -install:fb            FreeBASIC
netupdate -install:nasm          nasm e ndisasm
```

! **`gcc` STA IN `base`, NON IN `build`.** È l'errore che viene naturale fare:
  `build` sono gli strumenti *attorno* al compilatore.

! **E GLI STRUMENTI FINISCONO IN `/exos/bin`.** Se dopo averli installati un
  comando risponde «non trovato» ma il file c'è, è il PATH: vedi il
  [capitolo 5](05-quando-va-storto.md).

---

## 3.4 Cosa netupdate non tocca, e perché

Alcuni file sono di **questa** macchina e non del repository:

```
boot/kernel.cfg        quali driver caricare, che tastiera, che montaggi
```

`netupdate` lo lascia stare apposta. `install` invece lo sa **fondere**. Se
una voce nuova deve arrivarti — per esempio una riga del PATH — o la
aggiungi a mano, o passi da `install`.

---

## 3.5 Mandare indietro un referto

La rete va in tutte e due le direzioni. Se il server ha il punto di raccolta
(vedi [capitolo 6](06-pubblicare-il-repo.md)):

```
senderror http://esempio.org/exos/netinst/report/ /SIS900.TXT
```

! **PORTARE VIA UN FILE DA UNA MACCHINA DI PROVA E' LA PARTE DIFFICILE.** Un
  referto di duecento righe si produce in un secondo; farlo arrivare a chi lo
  deve leggere voleva dire una chiavetta e un viaggio.

! **VIAGGIA IN CHIARO E FINISCE SU UN SERVER CHE NON E' TUO.** Un referto
  dell'hardware va benissimo; un file con dentro qualcosa di personale, no.

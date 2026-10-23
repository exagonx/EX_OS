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

---

## 4.4 Gli strumenti

```
netupdate -install:base       gcc e la catena del C
netupdate -install:build      make e i binutils
```

Finiscono in `/exos/bin`, che dalla versione 0.217 è nel PATH predefinito.

Se il tuo `kernel.cfg` è più vecchio, la riga è questa:

```
PATH        = /bin:/dev:/exos/bin:/cdrom/exos/bin:/cdrom/bin
```

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

! **NON PIU' DI 4 GB**: il kernel conta al massimo 2^20 pagine da 4 KB.
  Una partizione più grande funziona, ma il di più resta inutilizzato.

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


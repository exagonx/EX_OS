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

`/boot/autoexec.sh` è una riga per comando, come se le battessi tu.

```
/dev/pci.drv &
netdetect -c
/dev/ip.drv &
dhcp
ipcfg
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

! **`-s` E' UNA PORTA APERTA, E VA SCELTO SAPENDOLO.** Su una rete di casa, per
  provare i driver di una macchina in un'altra stanza, è esattamente quel che
  serve. Su qualunque altra rete è una porta aperta. E telnet è in chiaro
  comunque: la password viaggia leggibile sul cavo.

  Ora che hai i conti veri, `telnetd &` senza `-s` costa una digitazione in
  più e chiude la porta.

Per averlo a ogni avvio, aggiungi la riga a `/boot/autoexec.sh`.

! **ATTENZIONE: `autoexec.sh` FA PARTE DEL PACCHETTO `sistema`**, quindi
  `netupdate` te lo può risostituire. Se un giorno la shell remota sparisce
  dall'avvio senza che tu abbia toccato niente, guarda se accanto c'è un
  `autoexec.sh.old`.

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

# EX-OS — Handoff

---

# SESSIONE 2026-08-04 (u) — cc1 gira, e trovando un difetto in realloc

Kernel **0.166**. `/bin/libctest` passa **271 prove su 271** (una nuova).

Tre cose: `cc1` è stato **eseguito** dentro EX-OS, ha trovato un difetto
grave nel nostro allocatore, e lo stack ha guadagnato **UDP e DHCP**.

## ⛔ realloc RESTITUIVA BLOCCHI MAI INGRANDITI

Questo è il difetto più grave trovato finora nella libc, ed era invisibile
finché non è arrivato un programma che alloca sul serio.

Nel ramo di `realloc` che allunga sul posto:

```c
heap_fondi_con_succ(b);          /* b è ALLOCATO */
heap_spezza(b, heap_allinea(size));
return ptr;                      /* «ecco i tuoi byte» */
```

`heap_fondi_con_succ()` comincia con `if (... || !b->libero) return;` — su
un blocco allocato **non fa niente**. Poi `heap_spezza()` vede `b->dim`
ancora piccolo e rinuncia anche lei. `realloc` restituisce il puntatore
dichiarando una dimensione che il blocco **non ha mai avuto**: chi scrive
sfonda nell'intestazione del blocco successivo.

Il danno non si vede lì: si vede alla `malloc` DOPO, che segue un
puntatore fatto dei dati dell'utente.

### Come è saltato fuori

`cc1 --version` dentro EX-OS moriva con

```
page fault a 0x0000000f (protezione, lettura, EIP=0x0a0c5307)
```

`addr2line` sull'eseguibile non spogliato: **`heap_fondi_con_succ`**, cioè
codice nostro. Il disassemblato dice `mov 0xc(%edx),%ecx` con `%edx` =
`b->succ`, e un fault a `0x0f` significa `succ == 3`. Un puntatore di lista
che vale 3 è un valore scritto da qualcun altro.

### La prova prima della correzione

In `libctest`: alloca due blocchi adiacenti, libera il secondo, fa
`realloc` del primo a 120 byte, lo riempie, alloca un terzo blocco e
**rilegge il primo**. Con il difetto, il programma cade con

```
page fault a 0xa7a6a5a4
```

— che sono **i byte di riempimento del test** (`a4 a5 a6 a7`) letti come
puntatore. Difficile avere una dimostrazione più diretta.

### La correzione

`heap_fondi_con_succ()` è stata divisa in due:

- **`heap_assorbi_succ(b)`** — assorbe il vicino se è libero e adiacente,
  senza guardare lo stato di `b`. È quella che serve a `realloc`.
- **`heap_fondi_con_succ(b)`** — il guscio con il controllo su `b->libero`.

⚠️ Il controllo **serve** agli altri chiamanti: `free()` e `memalign()`
chiamano `heap_fondi_con_succ(b->prec)` senza sapere se il predecessore
sia libero, e fondere un blocco allocato col suo vicino consegnerebbe due
volte la stessa memoria. Togliere il controllo dappertutto avrebbe
sostituito un difetto con uno peggiore.

## cc1 SI CARICA ED ESEGUE

41 MB di ELF, caricati e avviati. Il caricamento a richiesta
(`kernel/loader/elf.c`) regge: il processo è partito, è arrivato dentro il
proprio codice a `0x0a0c53xx` e ha fatto lavorare l'allocatore fino a
romperlo. La dimensione non è stata il limite; il limite è stato la libc.

Il CD degli strumenti ora porta `cc1`, `gcc` (il driver), `cpp` e
`collect2`, e la regola **stampa quanto pesano**: la differenza fra
l'albero `release` e quello con i controlli di sviluppo si vede solo in
megabyte, e andarla a misurare sul CD ogni volta è tempo perso.

⚠️ Il build con `--enable-checking=release` è **in corso** in
`~/gcc-build-rel` (directory nuova: l'opzione cambia macro incluse
dappertutto e non si può aggiungere a un albero già configurato). Quando
finisce, la regola del CD lo preferisce da sola.

## UDP

Nello stack, con tre scelte che vanno lette prima di toccarlo:

- **Si aprono porte, non prese.** Nessun descrittore: il client dice quale
  porta vuole. Basta a DHCP e a un futuro risolutore DNS; una vera API a
  prese si costruirà sopra questa, non al posto suo.
- **Niente coda.** Se arriva un datagramma per una porta aperta e nessuno
  sta aspettando, si scarta e si conta. UDP perde pacchetti per
  definizione; una coda che cresce mentre nessuno legge è un modo lento di
  finire la memoria per colpa di chi manda.
- **L'attesa dell'ARP serve a due operazioni, non a una.** Un ping e un
  datagramma UDP verso un indirizzo sconosciuto hanno lo stesso problema e
  la stessa soluzione: la macchina a stati è una sola, con un campo che
  dice cosa fare quando l'ARP arriva.

⚠️ **La somma di controllo UDP copre una pseudo-intestazione** di dodici
byte che non viaggia sul cavo (sorgente, destinazione, protocollo,
lunghezza). E il risultato 0 va scritto come `0xFFFF`: in UDP lo zero
significa «somma non calcolata», quindi una somma che venisse davvero zero
spegnerebbe il controllo invece di superarlo.

⚠️ **Somma a zero in arrivo si accetta**: su IPv4 il controllo è
facoltativo, e rifiutare quei datagrammi farebbe sparire traffico
legittimo.

## Broadcast

Serviva a DHCP e mancava del tutto: in uscita per non fare ARP verso
«tutti», in entrata per **accettare pacchetti non indirizzati a noi**.
Senza il secondo, un client DHCP non riceverebbe mai la risposta — il
server la manda in broadcast proprio perché un indirizzo non ce l'abbiamo
ancora.

## `/bin/dhcp`

È un **programma**, non un pezzo dello stack: DHCP sta sopra UDP come un
client DNS, e un errore lì dentro spegnerebbe la rete invece di far
fallire un comando.

⚠️ **Parte azzerando l'indirizzo**, perché un client DHCP deve mandare con
sorgente 0.0.0.0 — è l'unico modo di dire «non ho un indirizzo, è per
questo che sto chiedendo». La configurazione precedente viene salvata e
**rimessa se la negoziazione fallisce**: altrimenti il comando che doveva
dare un indirizzo lo toglie.

⚠️ **Alza il bit di broadcast** nel campo `flags`: l'indirizzo che il
server sta per darci non ce l'abbiamo, e non risponderemmo a un ARP per
quell'indirizzo.

⚠️ **Controlla `xid` e `chaddr`** su ogni risposta. Le risposte agli altri
client arrivano anche a noi perché sono in broadcast: prendere la prima
che passa significherebbe configurarsi con l'indirizzo di qualcun altro.

Non rinnova la concessione, non rilascia, prende la prima offerta. Tutto
detto nel file.

### Una corsa trovata leggendo i contatori

La prima versione mandava il DISCOVER, aspettava la conferma di invio, e
**solo poi** chiedeva un datagramma. In quella finestra la risposta arriva
già — su rete emulata torna in microsecondi — e lo stack, che non tiene
code, la buttava. Non si vedeva come un guasto: si vedeva come un DHCP che
ci mette tre secondi in più, perché la ritrasmissione andava a buon fine.

L'ho trovato guardando `ipcfg`: **3 datagrammi inviati dove ne bastavano
2**. Ora la prenotazione si fa PRIMA dell'invio, e i due messaggi (esito e
dato) si accettano in qualunque ordine — aspettare esplicitamente l'esito
avrebbe scartato il datagramma se arrivava per primo, cioè lo stesso
guasto spostato di due righe. Dopo: **2 inviati, 2 ricevuti**.

## Verificato in QEMU

```
cc1 dentro EX-OS      41 MB caricati, processo avviato
                      (poi il fault che ha svelato realloc)

dhcp su rete DIVERSA dai valori predefiniti:
  -netdev user,net=192.168.76.0/24,host=192.168.76.9,dhcpstart=192.168.76.30
  -> indirizzo 192.168.76.30, maschera 255.255.255.0,
     gateway 192.168.76.9, DNS 192.168.76.3, concessione 86400 s
  -> ping 192.168.76.9: 2 su 2
  -> UDP inviati 2, ricevuti 2, senza porta 0

libctest             271/271
```

⚠️ **La prima prova DHCP non provava niente**, e me ne sono accorto solo
guardandola: girava sulla rete predefinita di QEMU, che assegna
**10.0.2.15** — cioè esattamente l'indirizzo che lo stack ha già scritto
nel codice. Un risultato indistinguibile da «non è successo niente». È lo
stesso errore dei due MAC uguali della sessione scorsa: un test che non
può fallire non è un test.

## Prossimo passo

TCP, e prima di TCP la tabella delle operazioni in sospeso. Poi un
risolutore DNS — l'indirizzo del server ce l'abbiamo già, lo mette il
DHCP. E il rinnovo della concessione, che è un processo che resta acceso e
quindi un programma diverso da `dhcp`.

## File nuovi e modificati

- `bin/dhcp/dhcp.c`, `dhcp.ld` — nuovi
- `tools/iso/prova-cc1.c` — nuovo (il sorgente minimo per provare cc1)
- `lib/libc.c` — **`heap_assorbi_succ`**, `realloc` corretta
- `bin/libctest/libctest.c` — la prova che riproduce il difetto
- `drivers/net/ip_proto.h` — messaggi e strutture UDP, `dns` in `IpConfig`
- `drivers/ip/ip.c` — UDP, broadcast, macchina a stati generalizzata
- `bin/ipcfg/ipcfg.c` — DNS e contatori UDP
- `tools/gcc-exos/prepara-cc1.sh` — `--enable-checking=release`
- `tools/qemu_drive.py` — `EXOS_RAM`
- `Makefile` — GCC nativo sul CD degli strumenti, target `dhcp`
- `kernel/include/version.h` — 0.165 -> 0.166

---

# SESSIONE 2026-08-04 (t) — ARP, IP, ICMP: EX-OS fa ping

Kernel **0.165**. `/bin/libctest` passa **270 prove su 270**.

```
ex-os:/> ping 8.8.8.8 -n 3
  60 byte da 8.8.8.8: seq=1 ttl=255 tempo=70 ms
  60 byte da 8.8.8.8: seq=2 ttl=255 tempo=50 ms
  60 byte da 8.8.8.8: seq=3 ttl=255 tempo=60 ms
3 inviati, 3 ricevuti, 0% persi
```

## ⛔ IL DIFETTO PRINCIPALE: IRQ2 NON E' MAI STATO SBLOCCATO

**Nessun IRQ fra 8 e 15 ha mai potuto raggiungere la CPU in tutta la
storia di EX-OS.** I due 8259 sono in cascata: lo slave non ha un piedino
verso la CPU, alza la linea INT del master che la vede come **IRQ2**. Se
IRQ2 è mascherato nel master, tutto ciò che arriva dallo slave resta
fuori, per quanto lo si sblocchi nel registro dello slave.

`isr_install()` maschera tutto, e ogni driver poi sblocca il proprio IRQ.
Per gli IRQ bassi — timer 0, tastiera 1, floppy 6 — funzionava. Il primo
IRQ alto è arrivato adesso, con la scheda di rete su IRQ11.

### Come è saltato fuori: un numero che tornava sempre uguale

Il ping funzionava. Ma verso 8.8.8.8 misurava **esattamente 250 ms**, tre
volte di fila — e 250 è **esattamente** `PERIODO_MS`, il battito di
riserva del driver NE2000. Un numero troppo tondo per essere un tempo di
rete.

Due prove, in ordine:

1. `ping -c 3 8.8.8.8` **dall'host**: 46-115 ms. Quindi i 250 non erano il
   tempo vero.
2. Ricompilato il driver con `PERIODO_MS 500`: l'RTT è diventato
   **esattamente 500 ms**. Il tempo misurato seguiva il battito, cioè non
   era la rete: era il polling.

A quel punto ho aggiunto due contatori al driver — `notifiche_irq` e
`battiti` — e la risposta è stata immediata:

```
notifiche IRQ  0
battiti        131
```

Zero interrupt. La rete andava **solo per sondaggio**, perché il driver
guarda la scheda anche a scadenza e dopo ogni richiesta di un client. Un
guasto che non rompe niente e rallenta tutto è il tipo peggiore: senza
quei due numeri accanto, non c'era modo di distinguerlo.

⚠️ **La conferma era già nei log e non l'avevo letta.** Nel dump `info
pic` di due sessioni fa c'era `pic0: imr=bc` — bit 2 a uno, IRQ2
mascherato — e `irr=04`, cioè la cascata che chiedeva attenzione senza
ottenerla. Avevo guardato l'IOAPIC (che qui non si usa) e lasciato perdere.

### La correzione

`pic_unmask_irq()` sblocca anche IRQ2 quando l'IRQ è ≥ 8. E
`pic_mask_irq()` **non** lo rimaschera: IRQ2 non è la linea di nessuno, è
la strada per otto dispositivi, e chiuderla perché uno ha finito
toglierebbe l'interrupt agli altri sette.

L'ordine conta: prima si sblocca nello slave, poi la cascata. Fra le due
scritture il master lascerebbe passare un interrupt che nello slave è
ancora mascherato, e il PIC lo consegnerebbe come IRQ7 spurio.

Dopo: `notifiche IRQ 7`, e l'RTT verso 8.8.8.8 è **50-70 ms** — dentro i
46-115 misurati dall'host.

### ⚠️ E la mia "prova" della sessione scorsa era sbagliata

Avevo scritto che l'IRQ funzionava, «provato ricompilando il driver con il
battito a 60 secondi». Il ragionamento non teneva: in quel test `nettest`
manda `NET_MSG_RICEVI` e il driver, alla fine di **ogni** richiesta di un
client, guarda comunque la scheda. Era la richiesta di nettest a far
raccogliere il frame, non l'interrupt. Il battito escluso non bastava a
isolare la causa: bisognava contare le notifiche.

## Lo stack: `/dev/ip.drv`

ARP + IPv4 + ICMP in un **processo a sé**, come concordato. Non tocca
nessuna porta: parla col driver via IPC come qualunque programma.

Perché fuori dal driver: questi protocolli sono uguali su ogni scheda
(dentro il driver andrebbero riscritti per la PCnet); lo stack ha dei
**tempi** — scadenze ARP, attese di risposta — mentre il driver deve solo
rispondere all'hardware; e se sbaglia lo stack si riavvia lo stack, la
scheda resta accesa.

Punti che valgono la riga in più:

- **Il ciclo non si ferma mai su una singola attesa.** La tentazione è
  «manda la richiesta, aspetta la risposta»: qui non si può, perché dalla
  stessa mailbox arrivano frame, richieste dei client e conferme di invio.
  Fermarsi sul ping lascia in coda tutto il resto, e con la mailbox
  profonda 4 basta poco perché il driver si blocchi dentro `ipc_send`
  verso di noi. Un ciclo solo, scadenza breve, stato in una struttura.
- **Si tiene sempre una `NET_MSG_RICEVI` in volo**, e si riarma *prima* di
  guardare il frame ricevuto. Il driver non spinge di sua iniziativa: se
  non si richiede subito, i frame restano nella sua coda e la rete sembra
  funzionare per un pacchetto e poi fermarsi.
- **Il ripiegamento della somma di controllo è un ciclo, non una riga.**
  `s = (s & 0xFFFF) + (s >> 16)` una volta sola è giusto quasi sempre, e
  quel «quasi» è un pacchetto ogni tanto buttato dall'altra parte.
- **La somma ICMP copre intestazione *e* dati**, quella IP solo
  l'intestazione. Confonderle è l'errore più comune del mestiere: il
  pacchetto parte, sembra giusto, e sparisce.
- **Si impara anche dalle richieste ARP altrui**, non solo dalle risposte:
  chi ci chiede chi siamo ci sta dicendo il proprio indirizzo, e quasi
  sempre subito dopo gli dovremo rispondere.
- **Due errori distinti**: fermi all'ARP → `-EHOSTUNREACH` (non si è
  usciti dal cavo); echo senza risposta → `-ETIMEDOUT`. Un unico «non
  raggiungibile» costringerebbe a rifare la diagnosi da capo.

Cosa **non** fa, scritto in `ip_proto.h`: non frammenta e non riassembla
(i frammenti in arrivo si contano e si scartano), nessuna tabella di
routing, niente DHCP, una richiesta echo per volta.

## `/bin/ping` e `/bin/ipcfg`

`ping` **non sa cos'è ICMP**: manda un messaggio a `ip.drv` e stampa la
risposta. Deve restare così — quando arriveranno UDP e TCP, i loro client
saranno altrettanto ignoranti.

⚠️ **`ping` non stampa mai «0 ms».** `uptime_ms()` conta i tick del PIT a
100 Hz: avanza a scatti di 10 ms, e una risposta locale che torna in 300
microsecondi cade nello stesso tick della partenza. Scrivere «0 ms»
dichiarerebbe una misura che non è stata fatta; si scrive `<10 ms`, e se
tutte le risposte sono sotto il tick la riga delle statistiche lo dice
invece di stampare tre zeri.

`ipcfg` mostra indirizzo, **contatori** e (`-r`) la tabella ARP. I
contatori sono metà del programma: `IP ricevuti` a zero, `scartati` che
salgono e `somme errate` che salgono indicano tre punti diversi in cui
cercare.

## Provato anche il verso opposto: due EX-OS che si pingano

Rispondere a un ARP o a un ping **in arrivo** non era verificabile con lo
slirp di QEMU, che verso l'ospite non origina mai niente. Ho esteso
`tools/qemu_drive.py` con `EXOS_ISTANZA`, `EXOS_NET_SOCKET` e `EXOS_MAC`:
due macchine EX-OS su una LAN a socket, senza NAT e senza gateway.

```
B: ping 10.0.0.1 -n 3
   60 byte da 10.0.0.1: seq=1 ttl=64 tempo=10 ms   (3 su 3)
   tabella ARP di B:  10.0.0.1 -> 52:54:00:aa:aa:01
```

⚠️ **`ttl=64` è la prova.** È il TTL che imposta il nostro stack; lo slirp
usa 255. Quelle risposte le ha composte `rispondi_echo` sulla macchina A,
e l'ARP l'ha risolto `arp_rispondi`.

⚠️ **`EXOS_MAC` non è un dettaglio.** Il primo tentativo aveva le due
macchine con lo **stesso** MAC (QEMU ne assegna uno predefinito uguale per
tutti): il test passava, ma con due host identici sulla stessa LAN
qualunque risposta ARP sembra quella giusta — non provava niente. Rifatto
con indirizzi distinti.

## Un errore di misura, per non rifarlo

La prima lettura dei contatori della macchina A era tutta a zero. Non era
un guasto: avevo mandato `ipcfg` **prima** che l'altra macchina esistesse.
Un contatore letto nel momento sbagliato è un dato falso che sembra vero.

## Prossimo passo

UDP, poi TCP. E il client DHCP, che toglie di mezzo gli indirizzi
dichiarati a mano. Prima di TCP conviene però la tabella delle operazioni
in sospeso: oggi lo stack ne serve una per volta, che basta a `ping` e
non basta a niente altro.

## File nuovi e modificati

- `drivers/ip/ip.c`, `ip.ld` — nuovi (lo stack)
- `drivers/net/ip_proto.h` — nuovo (protocollo verso i client)
- `bin/ping/ping.c`, `bin/ipcfg/ipcfg.c` + linker script — nuovi
- `kernel/arch/x86/idt.c` — **`pic_unmask_irq` sblocca la cascata IRQ2**
- `drivers/net/net_proto.h` — contatori `notifiche_irq` e `battiti`
- `drivers/ne2k/ne2k.c` — li incrementa
- `bin/nettest/nettest.c` — li stampa
- `tools/qemu_drive.py` — `EXOS_ISTANZA`, `EXOS_NET_SOCKET`, `EXOS_MAC`,
  `EXOS_ATTESA_FINALE`
- `Makefile` — `ip_drv`, `ping`, `ipcfg`
- `kernel/include/version.h` — 0.164 -> 0.165

---

# SESSIONE 2026-08-04 (s) — La rete risponde, e cc1 esiste

Kernel **0.164**. `/bin/libctest` passa **270 prove su 270**.

Due traguardi in una sessione: EX-OS **manda e riceve frame Ethernet**, e
il compilatore C vero e proprio (`cc1`) **è stato linkato** per i386-exos.

## `cc1` c'è — e il difetto che lo bloccava era in libc

Il build canadian cross era arrivato in fondo: tutti gli oggetti
compilati, e poi il **link finale** fallito su tre simboli definiti due
volte fra la nostra `libc.a` e `libm.a` (openlibm).

Invece di correggere il primo e rilanciare, ho chiesto al linker
l'elenco completo — `nm` su entrambe le librerie e `comm -12` — e sono
**quattro**, non tre: `fabs`, `sqrt`, `ldexp`, `frexp`. Il quarto non era
ancora arrivato all'errore solo perché il link si era fermato prima. È la
stessa lezione di libstdc++: prendere l'elenco, non l'errore.

La correzione: `__attribute__((weak))` sulle quattro in `lib/libc.c`.
Toglierle non si poteva — i programmi di EX-OS linkano solo libc — e
`weak` risolve entrambi i casi con una regola sola: **chi linka anche
libm prende la versione di openlibm**, che è quella giusta (la nostra
`frexp` perde precisione sui denormali, la nostra `ldexp` fa
moltiplicazioni ripetute invece di toccare l'esponente).

```
cc1: ET_EXEC, Intel 80386, entry 0x8034fb0
     421 MB con i simboli, 40 MB spogliato (text 41 MB)
```

⚠️ **40 MB non entrano in 32 MB di RAM.** Il build ha
`--enable-checking=yes,types,extra` (il default di stage1 senza
bootstrap), che è la ragione principale della dimensione: rifarlo con
`--enable-checking=release` è il prossimo passo di quel filone, prima
ancora di provare a eseguirlo.

## Il difetto del kernel che bloccava qualunque driver PCI

Prima di poter scrivere un driver di rete è saltato fuori un problema che
la tastiera non aveva mai fatto emergere.

`irq_dispatch()` manda l'EOI al PIC **prima** dell'handler (correzione di
luglio 2026, per il deadlock di IRQ0), poi notifica il driver ring3 via
IPC. Su un IRQ **a fronte** come la tastiera va bene: ogni byte è un
fronte, il PIC lo latcha e basta.

Su un IRQ **a livello** — e tutti gli interrupt PCI lo sono — la scheda
tiene la linea alta finché non le si azzera il registro di stato. Ma il
driver è un processo: fra la notifica e il momento in cui tocca la scheda
passano dei tick. Quindi l'interrupt riparte subito dopo l'`iret`, e il
processo driver **non riceve mai la CPU** per andare ad azzerare quel
registro. Non è "qualche interrupt di troppo": è la macchina ferma, senza
panic e senza niente da leggere.

Soluzione, la stessa degli "interrupt in un thread":

- `irq_dispatch()` **maschera** l'IRQ nel PIC prima di notificare;
- nuova **`SYS_IRQ_DONE` (237)**: il driver riapre la linea dopo aver
  servito la scheda. Verifica il proprietario — senza, un processo
  qualunque potrebbe riaprire un IRQ che un altro sta ancora servendo.

⚠️ Vale per **tutti** i driver ring3, tastiera compresa: `kbd.drv` ora
chiama `irq_done(1)` dopo `kbd_drain()`, in quest'ordine (riaprire prima
di svuotare il KBC rimetterebbe in piedi la tempesta). Una regola sola per
tutti è meglio di due modi di rivendicare un IRQ di cui uno è quello
sbagliato di default. Un tasto premuto mentre l'IRQ è mascherato non si
perde: il fronte resta nell'IRR del PIC.

## Il messaggio IPC è passato da 512 a 1536 byte

Un frame Ethernet arriva a 1514 byte e un driver deve poterlo consegnare
**intero**: quello che entra dal cavo lo decide chi sta dall'altra parte.

L'alternativa era spezzarlo in quattro messaggi con un numero di
sequenza. Più economica in RAM e peggiore in tutto il resto: la mailbox è
profonda 4, quindi **un frame solo la riempirebbe**, e basta un secondo
mittente che si intromette per lasciare mezzo frame in attesa di un pezzo
che non arriva. E la logica di riassemblaggio finirebbe in ogni client.

Il costo, per intero: 64 processi × 4 messaggi × 1536 = **384 KB di BSS**
contro 128. Verificato: `kernel.bin` resta 180584 byte (il BSS non sta nel
file), BSS da 701 KB a 964 KB.

**E non lo paga lo spazio utente.** `sys_ipc_recv` copiava nella
`IpcMessage` del chiamante l'**intera** struttura, `data[]` compreso — 512
byte a ogni ricezione per un campo che nessuno legge, visto che il payload
arriva nel buffer separato. Ora copia i tre campi di intestazione, la
`IpcMessage` di `libc.h` **non ha più `data[]`** ed è di 12 byte, e la
verifica del puntatore misura quello che si scrive davvero (verificarne
1548 per scriverne 12 rifiuterebbe una struttura valida vicino alla fine
dello spazio utente).

## `/dev/ne2k.drv` — il primo driver di rete

NE2000/DP8390: la RTL8029 su PCI e i cloni Winbond/VIA/KTI.

**Perché proprio questa scheda, che nessuno produce più**: perché non ha
DMA verso la memoria di sistema. La RAM dei pacchetti sta *sulla scheda* e
ci si arriva da una porta di I/O — il "DMA remoto", che di DMA ha solo il
nome. Quindi il driver può girare in ring3 **oggi**, senza che il kernel
sappia di indirizzi fisici, pagine bloccate o bus mastering. La PCnet, che
il DMA vero ce l'ha, dovrà aspettare quella parte.

Dettagli che valgono la riga in più:

- **PROM a 16 bit**: ogni byte esce duplicato dentro una parola, quindi il
  MAC sta nei byte pari e la firma `0x57 0x57` a 28 e 30. Se non combacia
  si stampa **cosa si è letto**, perché è l'unico dato che permette di
  aggiungere un clone con firma diversa.
- **BNRY resta una pagina indietro rispetto a CURR**: se coincidessero la
  scheda leggerebbe l'anello come vuoto invece che pieno.
- **`prossima` si controlla prima di usarla**: è un byte scritto dalla
  scheda, e se finisce fuori intervallo scriverlo in BNRY manda l'anello
  in uno stato da cui non esce, con il ciclo di lettura che gira a vuoto.
- **Si azzera il bit di ISR *prima* di svuotare l'anello**: un pacchetto
  che arriva durante la lettura rialza PRX, e azzerarlo dopo cancellerebbe
  la segnalazione di un pacchetto non ancora letto.
- **Riempimento a 60 byte in trasmissione**: una richiesta ARP è lunga 42,
  e sotto i 60 uno switch scarta il frame senza dire niente — cioè la
  prima cosa che si prova a fare non funzionerebbe. Lo fa il driver perché
  è un vincolo del mezzo, non di chi compone il pacchetto.
- **Trabocco (OVW) → reinizializzazione completa.** La procedura ufficiale
  è di undici passi con un ramo che dipende da una trasmissione in corso:
  è il punto dove i driver NE2000 sbagliano, perché si esegue di rado e
  quindi si prova di rado. Un trabocco significa che i pacchetti li
  abbiamo già persi: serve tornare a uno stato noto, e la
  reinizializzazione è la strada più battuta del file.

### Due scelte di protocollo che valgono per ogni driver futuro

**Il driver non spinge mai un frame non richiesto.** `ipc_send` blocca il
mittente se la mailbox del destinatario è piena (profonda 4): se il driver
spinge frame verso uno stack che intanto gli sta mandando un pacchetto da
trasmettere, si arriva allo stato in cui **ognuno dei due è fermo dentro
`ipc_send`** ad aspettare che l'altro svuoti. Nessuno torna mai a
`ipc_recv`. Quindi domanda e risposta, come `kbd.drv`.

**Il battito.** `ipc_notify_irq` non blocca: se la mailbox del driver è
piena quando arriva l'interrupt, la notifica viene **scartata** — e la
linea resterebbe mascherata per sempre. Perciò l'attesa ha una scadenza
(250 ms): senza messaggi si guarda comunque la scheda e si riapre la
linea. Una notifica persa costa un ritardo, non un'interfaccia morta.

⚠️ **Le schede ISA non si sondano.** Per riconoscerne una bisogna scrivere
sulla sua porta di reset, e se a quell'indirizzo c'è un'altra scheda le si
è appena scritto addosso — la lista dei "soliti indirizzi" (0x300, 0x280,
0x320…) è esattamente la lista di quelli che si contendevano tutte le
schede ISA. L'indirizzo va dichiarato: `-p 0x300 -q 3`.

## `/bin/nettest` — ARP, non ping

ARP è il primo scambio possibile **senza avere uno stack**: sta subito
sopra Ethernet, niente IP, niente checksum. Una risposta che arriva
dimostra in un colpo solo che la scheda trasmette, che il frame arriva a
destinazione, che la scheda riceve e che la catena driver → IPC →
programma consegna i byte giusti. Se ARP funziona, a `ping` manca solo
software; se ARP non funziona, `ping` non direbbe **dove** si è rotto.

## Verificato in QEMU

```
/dev/pci.drv &  +  netdetect -c
    -> sceglie la NE2000, avvia /dev/ne2k.drv, e ASPETTA che il servizio
       'rete0' compaia nel registro IPC prima di dire che è andata bene

nettest -a 10.0.2.2
    chi ha 10.0.2.2? lo chiede 52:54:00:12:34:56 (10.0.2.15)
    64 byte  52:55:0a:00:02:02 -> 52:54:00:12:34:56  ARP (0x0806)
        ARP risposta: 10.0.2.2 e' 52:55:0a:00:02:02, cerca 10.0.2.15

nettest -c   ->  inviati 2, ricevuti 2, tutti gli errori a zero
libctest     ->  270/270, tastiera funzionante con la nuova disciplina IRQ
```

**Che l'IRQ funzioni davvero è stato provato, non supposto.** Il test ARP
da solo non lo dimostra: il driver guarda la scheda anche dopo ogni
richiesta di un client, quindi il frame sarebbe arrivato lo stesso. Ho
allora ricompilato il driver con il battito a **60 secondi** e rifatto la
prova: la risposta è arrivata subito. Con il battito escluso e il polling
post-richiesta che avviene prima che il frame torni, l'unica cosa che può
averla consegnata è l'interrupt.

## Due difetti trovati e corretti in corsa

1. **`netdetect` prometteva un driver che non c'è.** Diceva «2 schede, 2
   con un driver disponibile» leggendo solo la propria tabella, mentre
   `/dev/pcnet.drv` non esiste. Ora fa `stat()` sul file e distingue tre
   casi: driver presente, driver nominato ma assente dal supporto, scheda
   sconosciuta. Sono tre guasti diversi e vengono confusi ogni volta che
   non si può guardare.
2. **`PCI_DRV_PROTO` e `NET_PROTO` erano definiti nel Makefile DOPO le
   regole che li usavano.** Le prerequisite si espandono quando make legge
   la riga: una variabile definita più sotto risulta vuota, la dipendenza
   sparisce senza errore, e **modificare un protocollo smetteva di
   ricompilare i client**. Spostate in testa; verificato che ora un
   `touch` sui due header ricompila tutti e quattro i programmi.

## Prossimo passo

Lo stack: ARP (tabella e timer), IP, ICMP, con `ping` come primo
traguardo — in un **processo autonomo**, come concordato. Il driver è
pronto a servirlo: `NET_MSG_RICEVI` blocca finché non c'è un frame, e
`NET_MSG_INVIA` conferma l'invio.

## File nuovi e modificati

- `drivers/ne2k/ne2k.c`, `ne2k.ld` — nuovi
- `drivers/net/net_proto.h` — nuovo (protocollo di **ogni** driver di rete)
- `bin/nettest/nettest.c`, `nettest.ld` — nuovi
- `kernel/arch/x86/isr.c` — maschera prima di notificare, `irq_done_process`
- `kernel/include/isr.h`, `syscall.h` (`SYS_IRQ_DONE`, `SYSCALL_COUNT` 238),
  `syscall_impl.c` (`sys_irq_done`, copia della sola intestazione IPC),
  `syscall.c`
- `kernel/include/sched.h` — `IPC_MSG_MAX_DATA` 512 -> 1536
- `lib/libc.c` — `irq_done`, le quattro funzioni `weak`, `IpcMessage` senza
  `data[]`; `lib/include/libc.h` idem
- `drivers/kbd/kbd.c` — `irq_done(1)` dopo `kbd_drain()`
- `bin/netdetect/netdetect.c` — `-c`, verifica dell'esistenza del driver
- `Makefile` — `ne2k_drv`, `nettest`, header di protocollo spostati in testa
- `kernel/include/version.h` — 0.163 -> 0.164

---

# SESSIONE 2026-08-03 (r) — Il bus PCI si enumera da userspace

Kernel **0.163**. `/bin/libctest` passa **270 prove su 270** (nessuna regressione).

Primo passo del piano di rete concordato: il server PCI. Non tocca il
kernel per quello che fa — enumerare — ma **ha richiesto quattro syscall
nuove** per poterlo fare, e la ragione merita di essere letta prima di
riaprire il file.

## Il byte non bastava: SYS_IOPORT_IN16/OUT16/IN32/OUT32

Nella sessione precedente avevo scritto che il server PCI si poteva fare
«senza modifiche al kernel, perché `ioport_bind` accetta 0xCF8». La
`bind` sì; **le `in`/`out` no**, ed erano solo a 8 bit.

Il meccanismo di configurazione PCI #1 vuole che CONFIG_ADDRESS (0xCF8)
sia scritto con **un solo accesso a 32 bit**. Non è una preferenza di
stile: un accesso a byte o a word verso 0xCF8..0xCFB non viene
interpretato dal ponte come ciclo di configurazione e finisce sul bus
come normale I/O. E siccome **0xCF9 è il registro di reset** di molti
chipset, scrivere l'indirizzo in quattro `outb` non «funziona piano»:
riavvia la macchina.

Quindi quattro numeri di syscall nuovi (233-236), non un argomento
"ampiezza" su quelli esistenti — stessa ragione già scritta per
`SYS_IPC_RECV_TMO`: `SYS_IOPORT_IN` oggi legge solo EBX, e un binario
già installato lascia in ECX quel che c'era prima. Un numero nuovo non
ha ambiguità; un argomento in più farebbe leggere a 32 bit dove si
voleva un byte, il giorno che quel binario gira su un kernel nuovo.

Tre dettagli che erano facili da sbagliare:

- **Il controllo del range copre tutti i byte toccati.** `ioport_allowed()`
  verifica UNA porta; una `out32` su 0xCF8 scrive fino a 0xCFB. Senza il
  controllo esteso, un processo che ha rivendicato la sola 0xCF8 poteva
  scrivere su 0xCF9, cioè resettare la piattaforma.
- **Allineamento obbligatorio** (`-EINVAL` altrimenti): un accesso a 32
  bit disallineato viene spezzato in due cicli e il secondo non è più un
  ciclo di configurazione — il risultato è silenziosamente sbagliato.
- **`ioport_in32` non restituisce il valore letto.** `0xFFFFFFFF` è la
  risposta *normale* di uno slot vuoto, e come `int32_t` è `-1`:
  indistinguibile da un errore. Il valore esce da un puntatore, il
  ritorno dice solo se è andata. `in16` non ha il problema e restituisce
  il valore direttamente.

## `/dev/pci.drv` — il server

`drivers/pci/pci.c`, ~470 righe, stesso schema di `kbd.drv`: ET_EXEC
statico, nessuna istruzione privilegiata, un solo punto di attesa
(`ipc_recv`). Rivendica 0xCF8..0xCFF e nient'altro.

Due modi:

```
/dev/pci.drv         registra il servizio "pci" e serve i client
/dev/pci.drv -l      enumera, stampa, esce
```

Il secondo modo esiste per **non scrivere un `lspci` separato**: una
seconda copia dell'enumerazione diverge dalla prima e poi si passa il
tempo a chiedersi quale delle due dice il vero.

### Due cose che il server NON fa, di proposito

**Non c'è una scrittura di configurazione generica.** La simmetria
suggerirebbe `LEGGI`/`SCRIVI` su un offset qualunque. Leggere non può
rompere niente; scrivere sì, e non solo il dispositivo di chi scrive: i
BAR dicono al ponte dove risponde ogni scheda, e riprogrammare quelli
del controller ATA significa togliere il disco da sotto i piedi al
kernel, che di quella scrittura non sa nulla. L'unica scrittura esposta
è `PCI_MSG_ABILITA`/`DISABILITA`, che tocca **solo** il registro comando
e **solo** tre bit (I/O, memoria, bus master), e solo su dispositivi che
il server ha effettivamente enumerato.

⚠️ `PCI_MSG_DISABILITA` non è la coppia decorativa di `ABILITA`: è il
pezzo che rende possibile far ripartire un driver. Una scheda lasciata
bus master da un processo morto continua a scrivere nei buffer che il
kernel ha già liberato, e il danno arriva minuti dopo in un punto
qualunque della memoria.

**Non misura la dimensione delle finestre BAR.** Si misura in un modo
solo — scrivere `0xFFFFFFFF` nel BAR, rileggere, riscrivere l'originale
— e per il tempo fra le due scritture il dispositivo decodifica un
indirizzo assurdo. Farlo in enumerazione vuol dire farlo su *ogni*
dispositivo, ATA compreso, mentre il kernel lo sta usando. Un server di
enumerazione non deve scrivere sull'hardware di nessuno; la dimensione
la sa già chi la usa (una NE2000 ha 32 registri perché è una NE2000, non
perché gliel'ha detto il bus).

### Dettagli dell'enumerazione che valgono la riga in più

- **Mappa dei bus già visitati** (`g_bus_visto`, 256 bit): un ponte
  PCI-PCI dichiara il bus secondario in un byte, e niente impedisce a
  quel byte di puntare al bus da cui si è arrivati. Dentro il kernel
  sarebbe una macchina bloccata; qui è un processo da rilanciare — che è
  poi il motivo per cui sta in userspace.
- **Funzioni 1..7 solo se il bit multifunzione è alzato**: su un
  dispositivo a funzione singola le altre sette sono alias della zero, e
  leggerle comunque annuncia otto volte la stessa scheda.
- **Un ponte ha due BAR, non sei**: leggerne sei significa interpretare
  come indirizzi i numeri di bus e le finestre di inoltro, che stanno lì.
- **BAR di memoria a 64 bit**: occupa due voci, la seconda è la metà
  alta dell'indirizzo. Saltarla evita di annunciare un dispositivo a
  mezzo puntatore.
- **Si scende dai ponti invece di provare tutti i 256 bus**: la forza
  bruta, su una macchina con più host bridge, annuncia dispositivi non
  raggiungibili.

## `/bin/netdetect` — quale driver assegnare

Client del server, tabella `venditore:dispositivo -> driver`. È una
tabella e non una catena di `if` perché è un **dato**: aggiungere una
scheda è una riga in un punto solo, e l'elenco si può stampare
(`netdetect -t`) senza riscriverlo a mano — cioè senza che le due
versioni prima o poi si contraddicano.

Distingue tre casi, che sono davvero diversi: driver disponibile;
modello noto ma driver da scrivere; scheda sconosciuta (e dice quale
numero manca in tabella).

⚠️ **Non carica niente, per ora.** «Quale driver assegnare» ha due metà,
riconoscere e caricare: c'è solo la prima, perché nessuno dei driver
nominati esiste ancora e un `-c` sarebbe codice mai eseguito nemmeno una
volta, spedito sul CD con l'aria di funzionare. Arriva col primo driver
di scheda.

⚠️ **Le NE2000 ISA non si vedono da qui** e non è un difetto: non hanno
spazio di configurazione, si trovano sondando le porte tipiche (0x300,
0x280, 0x320...). È un mestiere diverso — provare hardware che potrebbe
non esserci — e va nel driver NE2000, non in un programma che si fida
del bus.

Nel client c'è anche una guardia che vale per **ogni** futuro client
IPC: `ipc_recv` consegna il prossimo messaggio della mailbox, non «la
risposta alla mia domanda». Si controlla `sender_pid` e si usa la
versione con scadenza, altrimenti un server che muore fra domanda e
risposta lascia il client fermo per sempre.

## Verificato in QEMU

```
/dev/pci.drv -l                 6 dispositivi, host bridge/ISA/IDE/VGA/e1000
+ -device pcnet -device ne2k_pci  8 dispositivi:
    00:04.0  1022:2000  Ethernet  IRQ 11   -> AMD PCnet (la scheda di VirtualBox)
    00:05.0  10ec:8029  Ethernet  IRQ 10   -> RTL8029, NE2000 PCI

/dev/pci.drv &                  "servizio 'pci' attivo, 8 dispositivi"
netdetect                       3 schede, 2 con un driver disponibile
/dev/pci.drv & (seconda volta)  "ipc_register('pci') fallita (-17) — esco"
                                 il primo continua a servire
netdetect (senza server)        dice come avviarlo, non un numero
libctest                        270/270, nessuna regressione
```

## Due errori miei di questa sessione, per non rifarli

1. **Ho provato il CD con il floppy ancora attaccato.** `EXOS_CDROM=` da
   solo non basta: senza `EXOS_NO_FLOPPY=1` la radice è il floppy, e
   `/dev/pci.drv` «non esiste» perché sul floppy davvero non c'è. Ho
   perso un giro a dubitare del generatore ISO, che era corretto. La
   riga da cercare nel log è `VFS: root '/' su ...`.
2. **Trattino lungo UTF-8 in una tabella allineata con `%-44s`**:
   `printf` conta byte, non colonne, e le righe con `—` si sfalsavano di
   due caratteri. Nei dati incolonnati solo ASCII.

## Dove va il codice nuovo, e perché non sul floppy

`build/drivers-cd/` e `build/bin-cd/`: due directory di sosta per
quello che finisce **solo sul CD di EX-OS**. Sul floppy ci starebbe
(ce ne sono 731 KB liberi), ma il floppy serve ad avviare e a
installare, e uno strumento di rete senza i driver di rete — che sul
floppy non ci staranno mai — non servirebbe a niente una volta lì.

La separazione è una directory e non un filtro nella regola del floppy
perché `tools/mkfloppy.sh` prende tutto quello che trova in
`build/drivers/`: un elenco di esclusioni lì dentro sarebbe una lista da
ricordarsi di aggiornare, e il floppy si riempirebbe il giorno che
qualcuno se ne dimentica.

## Prossimo passo

`/dev/ne2k.drv`. La NE2000 **non è bus master** (l'accesso alla RAM
della scheda passa dalle porte, non dal DMA), quindi non serve ancora la
rivendicazione DMA nel kernel: si può scrivere subito, e poi ARP + IP +
ICMP con `ping` come primo traguardo. La `PCI_MSG_ABILITA` col bit bus
master serve invece a `pcnet.drv`, che viene dopo.

## File nuovi e modificati

- `drivers/pci/pci.c`, `pci_proto.h`, `pci.ld` — nuovi
- `bin/netdetect/netdetect.c`, `netdetect.ld` — nuovi
- `kernel/include/syscall.h` — 4 syscall nuove, `SYSCALL_COUNT` 233 -> 237
- `kernel/syscall/syscall_impl.c` — `ioport_allowed_range`, `ioport_prepara`,
  le quattro implementazioni
- `kernel/syscall/syscall.c` — registrazione
- `lib/libc.c`, `lib/include/libc.h` — `ioport_in16/out16/in32/out32`
- `Makefile` — `BUILD_DRIVERS_CD`, `BUILD_BIN_CD`, target `pci_drv` e
  `netdetect`, copia sul CD
- `tools/qemu_drive.py` — `&` nella mappa dei tasti (shift-7)
- `kernel/include/version.h` — 0.162 -> 0.163

---

# SESSIONE 2026-08-03 (p) — install non distrugge piu', rename vera, chkdsk

Kernel **0.161**. `/bin/libctest` passa **270 prove su 270**.

## install distruggeva prima di sapere se ce la faceva

Apriva `/boot/kernel.bin` con `O_TRUNC`, ci scriveva il kernel nuovo, e
*poi* chiedeva la mappa dei settori. Su FAT — dove la mappa ammette **un
solo intervallo** — un kernel cresciuto di qualche KB non entrava piu' nel
buco del vecchio, finiva in due tratti, e `bootinstall` rifiutava. A quel
punto il sistema funzionante non c'era piu' e **il disco non ripartiva**.

⚠️ Su ext2 non si vedeva: li' la mappa regge 12 intervalli. Quattro prove
su ext2 passavano tutte, ed e' per questo che il difetto era rimasto.

Ora si scrive con nomi temporanei mentre i vecchi sono al loro posto (i
nuovi finiscono nella coda libera, **contigua**), si verifica con
`boot_installa_ex(..., solo_verifica)`, e solo allora si scambia. Lo
scenario che distruggeva il disco ora **riesce**.

`kernel.cfg` non si sovrascrive piu': e' l'unico file dell'installatore
che appartiene a chi usa il sistema.

`fat_mkdir` schiacciava `-2` a `-1` → «errore di I/O» invece di «esiste
gia'» su ogni reinstallazione.

## rename: la primitiva che mancava

`rename()` era **copia+cancella**: riallocava i blocchi, quindi mandava a
monte qualunque verifica fatta prima. Ora e' `vfs_rename` su tutti e tre i
driver — riscrive la voce di directory, **i dati non si spostano**. Piu' il
comando `/bin/rename`.

⚠️ Solo stessa directory (ENOSYS) e non sostituisce (EEXIST). In
`ext2_rename` si **aggiunge prima e si toglie dopo**: in mezzo il file ha
due nomi, riparabile; al contrario non ne avrebbe nessuno.

⚠️ **`fat12.c` risponde in errno, gli altri due no**: li' `-2` e' ENOENT,
in `fat.c`/`ext2.c` e' «esiste gia'». Mescolarle faceva dire «esiste gia'»
a una rinomina di un file inesistente.

## /bin/chkdsk — FAT12/16/32 ed ext2

Un solo programma con due **motori** (`riconosce`/`apri`/`controlla`) che
sonda il volume: chi ha un disco malandato spesso non sa che filesystem ci
sia, e a volte il volume e' rotto *proprio* nei campi che lo direbbero.

⚠️ Solo su partizione **smontata** (sopra una montata c'e' una cache
write-back) e **senza `-r` non scrive un settore**.

Tre difetti miei, tutti trovati provando **prima sul volume sano**:

1. **`..` che punta alla root vale ZERO anche su FAT32** (dove la root un
   cluster ce l'ha): confrontarlo col numero vero segnalava come guasta
   ogni directory di primo livello di ogni volume sano;
2. **buffer globale condiviso dalla ricorsione** in ext2: scendendo in una
   sottodirectory il blocco del padre veniva sovrascritto → `rec_len 0` su
   dischi perfetti;
3. **le copie di riserva del superblocco** non appartengono a nessun inode:
   26 blocchi di differenza su un volume appena formattato.

⚠️ La riparazione delle bitmap ext2 **non parte se c'e' un solo punto di
incertezza** (`e2_incerto`): riscrivere avendo saltato un inode significa
dichiarare liberi i suoi blocchi. Per questo si e' dovuto implementare il
**triplo indiretto**: senza, la riparazione non era onesta.

## ⚠️ La marcatura GPLv3 impediva a un file di compilare

`applica.py` scriveva la nota richiesta dalla GPLv3 §5(a) sempre con `#`.
Ha funzionato finche' ogni file toccato era shell/make/configure. Al primo
file **C++**:

```
sarif-sink.cc:1:3: error: invalid preprocessing directive #Modificato
```

Ora la sintassi si sceglie dall'estensione. Non era emerso prima perche'
`pex-exos.c` e' **copiato**, non modificato, quindi non passa da li'.

---

# SESSIONE 2026-08-03 (q) — El Torito, e il CD che si avvia e poi si ferma

## Il CD avviabile per emulazione floppy funziona

`tools/mkiso.py --avvio dist/floppy.img` scrive il descrittore di avvio e
il catalogo El Torito. Provato: **il kernel parte da CD senza floppy
collegato**.

⚠️ **Il checksum del catalogo e' su parole da 16 bit e la somma deve fare
zero.** Un BIOS che non lo trova a zero **salta l'avvio senza dire
niente**, e il disco sembra semplicemente non avviabile.

⚠️ Assegnare a una fetta di `bytearray` una sequenza piu' corta la
**ACCORCIA**: l'identificativo da 24 byte va riempito, o la voce di
validazione finisce di 31 byte e sposta tutto cio' che segue.

## ⚠️ MA IL KERNEL NON PUO' LEGGERE IL FLOPPY EMULATO

```
[WARN] [PASSO 13] FAT12 init fallita — filesystem non disponibile
```

`fat12_read_sector` chiama **`fdc_rw_sector`**: programma il controller
floppy alle porte hardware. Con El Torito il floppy **esiste solo
attraverso l'INT 13h del BIOS**: dietro non c'e' nessun controller.

| | |
|---|---|
| stage1, stage2 | ✅ modo reale, usano INT 13h — che e' proprio cio' che il BIOS emula |
| il kernel | ❌ modo protetto, parla al controller: trova porte vuote |

Non e' aggirabile senza v86 o rientri in modo reale.

**La risposta e' montare il CD come radice**: il CD e' un ATAPI vero, e i
driver ATAPI + ISO 9660 + automount ci sono gia'. Il floppy emulato serve
solo a portare in memoria stage2 e il kernel, poi non serve piu'.

⚠️ Richiede di togliere l'inchiodatura del montaggio 0, che oggi e' voluta
(`kernel/fs/vfs.c`: «IL MONTAGGIO 0 E' LA ROOT SUL FLOPPY, E NON SI PUO'
SMONTARE») per garantire che l'avvio da floppy resti quello collaudato.

## ✅ RISOLTO NELLA STESSA SESSIONE — kernel 0.162

`vfs_init()` ora ripiega sul CD quando `fat12_pronto()` dice di no, e monta
l'ISO 9660 come radice **in sola lettura** (un ISO non si scrive:
dichiararlo fa rifiutare le modifiche con EROFS *prima* di toccare il
filesystem).

⚠️ **Il segnale non e' il numero del drive**, che con l'emulazione vale
0x00 come un floppy vero: e' il fallimento di `fat12_init`. Non e'
un'euristica — «il controller non risponde» E' la condizione dell'avvio da
CD, e con un floppy guasto cercare altrove resta comunque giusto.

Nuovo target `make iso-exos` → `dist/exos.iso` (2876 KB, avviabile).

⚠️ Nel Makefile sta scritta la conseguenza che decide il contenuto: **il
sistema che gira e' quello sul CD, non quello dentro `boot.img`**. I due
devono contenere le stesse cose, o si avvia una versione e se ne esegue
un'altra.

### Il giro completo, provato

```
avvio da CD (nessun floppy)  ->  mkfs -t ext2 hd0p1  ->  mount  ->
install /disk  ->  "verifica: 353 settori in 2 intervalli"  ->
riavvio dal disco: Versione 0.162
```

⚠️ **Il CD serve a installare**: la radice in sola lettura non e' un limite
da aggirare con un disco RAM, e' il punto di partenza. Gli strumenti che
scrivono (`fdisk`, `mkfs`, `chkdsk`) lavorano sui settori grezzi di
partizioni NON montate, quindi non hanno bisogno di una radice scrivibile;
`install` legge dal CD e scrive sul disco montato.

⚠️ Due prove di `libctest` fallivano da CD: e' stata corretta **la prova**,
non il codice — `EROFS` era la risposta giusta, e segnalarla come guasto
insegna a ignorare le righe rosse.

`tools/qemu_drive.py` ha ora `EXOS_CDROM=<iso>` per avviare da CD. Serve
perche' uno script ad-hoc non ha la mappa dei tasti: le maiuscole vanno
mandate come `shift-x`, e senza, `-L` arriva come `-`.

## Piano di rete concordato

Due CD: **sviluppo** (`exos-tools.iso`, con GCC e i porting) ed **EX-OS**
(roba propria, avviabile). Driver in **userspace come server**, riavviabili
dopo un crash.

⚠️ **Il kernel deve poter FERMARE la scheda**, non solo assegnarle il DMA:
alla morte del proprietario azzera **Bus Master Enable** nel registro
comando PCI — una scrittura, indipendente dal dispositivo — *poi* libera la
memoria, *poi* l'IRQ. Senza, un bus master continua a scrivere in RAM
liberata.

⚠️ Userspace da' **riavviabilita', non contenimento**: senza IOMMU un
processo che programma un bus master puo' far scrivere ovunque.

⚠️ **NE2000 non e' un bus master** — si programma a porte — quindi il suo
driver ring3 funziona **oggi**, senza modifiche al kernel: e' il banco su
cui collaudare ARP/IP/ICMP prima di affrontare il DMA del PCnet.
VirtualBox pero' non offre NE2000 (QEMU si').

---

# SESSIONE 2026-08-03 (o) — Le pipe, e uno zombie che tratteneva un tubo

Kernel **0.160**. `/bin/libctest` passa **265 prove su 265** (erano 239).

Le quattro funzioni POSIX che mancavano a GCC per girare come programma
ospite: `getpagesize`, `getrusage`, `mmap`/`munmap`, `pipe`.

## Le tre facili, e cosa dichiarano di non saper fare

| Funzione | ⚠️ Il limite, dichiarato |
|---|---|
| `getpagesize` | nessuno: 4096 |
| `getrusage` | ⚠️ **non e' una misura**: `ru_utime` e' il tempo TRASCORSO dall'avvio, non il tempo di CPU del processo — EX-OS non tiene contabilita' per processo. Tutto il resto e' zero |
| `mmap` | ⚠️ **solo memoria anonima**: `fd != -1` da' `ENODEV` |

⚠️ **`getrusage` riporta un limite superiore, non zero**, ed e' una scelta:
zero secco sarebbe altrettanto falso e meno utile. Cosi' due chiamate
successive danno numeri che crescono e una differenza fra due istanti resta
leggibile. Chi ci costruisce sopra un profilo — `gcc -ftime-report` —
ottterra' per ogni passaggio lo stesso tempo. Il giorno che servisse
davvero, la contabilita' va nello scheduler.

⚠️ **`mmap` di un file si rifiuta invece di consegnare zeri.** Servirebbero
le pagine sporche e il momento in cui riscriverle, cioe' un pezzo di
gestore della memoria che non c'e'. Una mmap che finge darebbe un programma
che legge dati sbagliati senza che niente lo segnali.

⚠️ E ritorna **`MAP_FAILED`, cioe' `(void *)-1`, non `NULL`**: e' la
convenzione di POSIX ed e' il modo classico di sbagliare a usarla.

### Un difetto trovato strada facendo: `munmap` non abbassava il confine

`sys_munmap` liberava le pagine fisiche ma lasciava `heap_end` dov'era.
Ogni ciclo mmap/munmap si mangiava un pezzo di **spazio di
indirizzamento** che non tornava piu' — e `ggc-page.cc` di GCC fa
esattamente quel ciclo, migliaia di volte, su ogni file che compila.

Non e' una perdita di memoria fisica, e infatti non si vedeva: si vedrebbe
solo dopo molto lavoro, come una `mmap` che comincia a fallire con dello
spazio apparentemente libero. Ora, se la zona smappata e' in cima, il
confine torna giu' — solo la cima, come per `sbrk`: un buco in mezzo resta
un buco, perche' `heap_end` e' un confine e non una mappa.

### ⚠️ E una prova mia mal specificata

`ma una coda si tiene` asseriva `dopo_free >= base`. Falso: il blocco da
2 MB si **fonde con la coda libera che c'era gia' prima** di `base`, quindi
comincia piu' in basso e restituire fin sotto `base` e' corretto. Ora la
prova dice quello che quella frase significa davvero: **una malloc da 1 KB
subito dopo non deve toccare il kernel.**

## Le pipe

`kernel/ipc/pipe.c`: buffer circolare da 4 KB, conteggio separato di
lettori e scrittori, attesa con la stessa disciplina anti-risveglio-perduto
del driver tastiera.

⚠️ **`FD_PIPE_R` e `FD_PIPE_W` sono due TIPI, non uno con un flag.** La
direzione non e' un dettaglio del descrittore: e' cio' che decide se una
`read` blocca o e' un errore. Tenerle distinte fa sbagliare il compilatore
invece del programma.

Le prove che contano non sono il giro di andata e ritorno:

```
[ok]  chiusa la scrittura, read da' 0 (fine dei dati)
[ok]  senza lettori, write da' EPIPE
[ok]  leggere dall'estremita' sbagliata fallisce
```

⚠️ **Vuota con scrittori vivi = aspetta; vuota senza scrittori = 0.** E'
tutta qui la ragione per cui il conteggio degli scrittori esiste: senza,
una pipe non e' distinguibile da un blocco eterno.

⚠️ **Niente SIGPIPE**: EX-OS non ha i segnali, quindi si vede solo il
`-EPIPE`. Chi non guarda il valore di ritorno di `write()` non se ne
accorge. Limite dichiarato.

⚠️ **Niente garanzia di atomicita' di POSIX**: la scrittura puo' essere
parziale anche sotto `PIPE_BUF`. Con un filo per processo garantirla
costerebbe complessita' per nessun beneficio misurabile.

### L'eredita' dei descrittori, senza cui le pipe non servono

`SpawnRedir` con `percorso` **NULL** passa al figlio un descrittore del
padre invece di aprire un file. E' quello che rende possibile
`cmd1 | cmd2`.

⚠️ **La magia di `SpawnExtra` e' passata da `'SPNX'` a `'SPNY'`** perche' e'
cambiata la disposizione di `SpawnAzione`. Un binario compilato per la
forma vecchia verrebbe letto storto, e **una redirezione letta storta
scrive nel file sbagliato**: con la magia nuova il kernel non riconosce il
blocco e lo ignora, che e' il modo meno dannoso di sbagliare.

⚠️ **Il padre deve chiudere l'estremita' che ha passato.** Se non lo fa, la
pipe conta ancora uno scrittore vivo — lui — e chi legge aspettera' per
sempre. E' l'errore classico con le pipe e qui non c'e' niente che lo
segnali.

## ⚠️ LO STALLO CHE HA RIVELATO UN DIFETTO DI PROGETTO

Il caso a due processi si bloccava. Strumentando invece di ipotizzare, la
traccia diceva tutto:

```
TRACCIA pipe0: scritti 19 byte, sveglio PID 8
TRACCIA pipe0: scritti 1 byte, sveglio PID 0
TRACCIA pipe0: PID 8 si blocca in lettura (scrittori=1)      <- e poi piu' niente
```

Il figlio aveva scritto ed era uscito, ma **`scrittori` era ancora 1**.
Perche' i descrittori si chiudevano in `proc_reap_zombie()`, che gira solo
quando il padre chiama `waitpid()` — e il padre era bloccato nella `read`.

```
il figlio esce                 -> ZOMBIE, fd ancora contati
il padre e' fermo nella read   -> non arrivera' mai a waitpid()
nessuno chiama reap            -> nessuno decrementa
```

Due processi fermi ad aspettarsi a vicenda, senza nessun errore.

⚠️ **E' il motivo per cui su Unix i descrittori si chiudono in `do_exit()`
e non in `wait()`: uno zombie non deve trattenere risorse di I/O, solo il
proprio codice di uscita.** Con i soli file non si notava — uno zombie che
tiene un file per qualche millisecondo non da' fastidio a nessuno. Con una
pipe e' uno stallo.

Spostato in `proc_chiudi_fd()`, chiamata da `proc_exit()` **e** da
`proc_kill()` (che copre la morte per fault). In `proc_reap_zombie` resta
una spazzata di sicurezza: la funzione e' idempotente.

⚠️ **Servono le varianti `_locked`**: `proc_exit`/`proc_kill` girano gia'
sotto `cli`, e `interrupts_disable/enable` in questo kernel sono `cli`/`sti`
**grezzi, senza contatore di annidamento** — chiamare la versione pubblica
da li' farebbe `sti` a meta' di un'operazione non atomica. E' la stessa
ragione per cui esiste `sched_unblock_locked()`.

### E un difetto che ci avevo messo io

La prima stesura di `pipe.c` spostava gli indici dell'anello **fuori** dalla
sezione critica:

  - in scrittura, alzare `quanti` prima di copiare espone al lettore
    memoria non ancora scritta;
  - in lettura, avanzare `testa` prima di copiare via lascia uno scrittore
    libero di sovrascrivere quei byte.

Nessuno dei due da' un errore: danno byte sbagliati, ogni tanto, sotto
carico. Da qui il **buffer di rimbalzo**, che separa i due vincoli — la
memoria utente si tocca solo a interrupt abilitati (puo' faultare),
l'anello solo a interrupt disabilitati (dev'essere coerente).

## File toccati

| File | Cosa |
|---|---|
| `kernel/ipc/pipe.c`, `kernel/include/pipe.h` | **nuovi** |
| `kernel/sched/sched.c` | `proc_chiudi_fd()` alla morte, non al reap |
| `kernel/syscall/syscall_impl.c` | `sys_pipe`, pipe in read/write/close/dup, eredita' in spawn, `munmap` che abbassa il confine |
| `kernel/include/sched.h` | `FD_PIPE_R`, `FD_PIPE_W` |
| `kernel/include/syscall.h` | `SYS_PIPE`, `SPAWN_AZ_*`, magia `'SPNY'`, `EAGAIN`/`ENFILE` |
| `lib/libc.c`, `lib/include/libc.h` | `pipe`, `mmap`/`munmap`, `getrusage`, `getpagesize`, `SpawnRedir.fd_padre` |
| `tools/gcc-exos/prepara-cc1.sh` | **nuovo** — il canadian cross |
| `bin/libctest/libctest.c` | +26 prove (239 → 265) |
| `lib/include/libc.h`, `lib/libc.c` | `struct stat` con i tipi di POSIX |

## Verso `cc1`: la prima risposta precotta

Il canadian cross si configura con
`--build=x86_64-pc-linux-gnu --host=i386-exos --target=i386-exos`, e la
prima cosa che ha chiesto e' stata:

```
configure: error: unknown endianness
```

⚠️ **Non e' un'incognita: e' un difetto della prova.** i386 e'
little-endian. Il test prova quattro strade e l'ultima — quella che cerca
una stringa magica dentro l'oggetto — **non compila in C++**:

```
error: narrowing conversion of '35283' from 'int' to 'short int'
```

Da qui `ac_cv_c_bigendian=no`, che va **esportata** o le sotto-configure
lanciate da `make` non la vedono. La regola per le prossime: prima si
stabilisce il fatto, poi lo si scrive — non si mette una risposta per far
passare la compilazione.

## ⚠️ E UN DIFETTO VERO: `struct stat` AVEVA I TIPI SBAGLIATI

Dopo 526 oggetti, `libcpp/files.cc`:

```
error: invalid conversion from 'unsigned int*' to 'off_t*' {aka 'long int*'}
```

cioe' `&file->st.st_size` passato dove serve un `off_t *`. La nostra
`struct stat` dichiarava **tutti** i campi `unsigned int` invece dei tipi
di POSIX.

⚠️ **Sul nostro bersaglio hanno la stessa larghezza, quindi i VALORI erano
corretti** e in un anno nessuno se n'era accorto. Il tipo si vede solo
quando qualcuno prende l'**indirizzo** di un campo — ed e' esattamente
quello che fa il preprocessore di GCC, per dire al lettore quanti byte ha
letto davvero.

Corretto: `dev_t`, `ino_t`, `mode_t`, `nlink_t`, `uid_t`, `gid_t`,
`off_t`. Piu' due campi che mancavano del tutto: `st_blksize` e
`st_blocks`.

⚠️ **`st_blksize` vale 512 e non 4096**: e' il SETTORE, che e' l'unita'
vera di tutti i filesystem di EX-OS. Chi dimensiona un buffer su quel
numero deve ricevere qualcosa che corrisponde a come si legge davvero.

⚠️ **`st_size` ora e' SEGNATO** (`off_t` e' `long`), quindi il tetto e'
2 GB e non 4. E' lo stesso limite che ha gia' `lseek()`, che e' la syscall
sotto: dichiararlo senza segno non renderebbe piu' grandi i file,
renderebbe solo silenziosa la troncatura al confine col kernel.

La prova nuova e' scritta per fallire **in compilazione** se il tipo
regredisce:

```c
struct stat s;
off_t *punta = &s.st_size;   /* se st_size torna unsigned int, non compila */
```

E' il genere di prova che vale piu' di un confronto di valori: quello
sarebbe passato anche prima.

---

# SESSIONE 2026-08-03 (n) — libstdc++ gira dentro EX-OS

Kernel **invariato a 0.157**. `/bin/libctest` passa **239 prove su 239**.

## La libreria standard del C++ risponde

```
ex-os:/> /cdrom/bin/provacpp
La libreria standard del C++ dentro EX-OS

  vector+sort : 1 3 5 7 9
  string      : "std::string concatenata" (23 caratteri)
  cerchio     : area = 12566 (x1000)
  quadrato    : area = 9000 (x1000)
  eccezione   : lanciata e ripresa
  out_of_range : presa da dentro la libreria

La libreria standard risponde.
```

`libstdc++.a` **25 MB** e `libsupc++.a` **1 MB** in
`~/exos-cross/i386-exos/lib`. E `provacpp` ora si costruisce con **una riga
sola**:

```sh
i386-exos-g++ -O2 -o provacpp prova-cpp.cpp
```

Fino a ieri serviva compilare con `g++` e collegare con `gcc`, perche'
`-lstdc++` non esisteva. L'ultima cosa che distingueva questo bersaglio da
uno qualunque e' sparita.

## ⚠️ Le eccezioni funzionano, e non era scontato

E' il pezzo che ha bisogno di piu' cose insieme: `__cxa_throw`, lo
svolgimento dello stack, le tabelle `.eh_frame`, i descrittori di tipo. Lo
svolgimento **legge a runtime le tabelle prodotte dal collegatore e le
percorre**: e' l'unica parte del C++ che pretende che il programma caricato
in memoria sia esattamente come il collegatore l'ha descritto.

Con il caricamento su richiesta di EX-OS (le pagine arrivano al primo
accesso, sessione (d)) e' anche una prova indiretta che quel meccanismo e'
corretto. E la seconda prova — `vector::at(10)` che lancia `out_of_range`
da dentro la libreria — attraversa piu' livelli di stack, che e' dove lo
svolgimento fa il suo lavoro invece di essere un salto.

⚠️ Le eccezioni si abilitano **da sole**: `-fno-exceptions -fno-rtti` non
serve piu' e non e' piu' nella riga di costruzione.

## Cos'e' servito, in ordine

Undici tentativi di build. Ogni volta la libstdc++ ha chiesto una cosa e si
e' fermata; l'elenco completo sta nelle sessioni (l) e (m). In sintesi:

| Passo | Cosa mancava |
|---|---|
| 1 | `os/newlib` al posto di `os/generic` (patch in `applica.py`) |
| 2 | `memalign` e famiglia |
| 3 | ~20 nomi di `<cstdlib>`/`<cstdio>` (`div`, `rand`, `mblen`, `system`, …) |
| 4 | **`extern "C"` in `libc.h` e `math.h`** — la riga che mancava da sempre |
| 5 | `sleep()` che ritorna `unsigned int` invece di `void` |
| 6 | 40 codici errno, le costanti `DT_*` e `S_IF*` |
| 7 | `mktime`, `localeconv`, `sig_atomic_t` |
| 8 | le sei macro di confronto C99 e la famiglia `<inttypes.h>` |
| 9 | **`nearbyintl`, che a openlibm manca**, e `HAVE_STRTOLD` |

⚠️ **Nessuno di questi si e' scoperto leggendo la documentazione**: si sono
scoperti uno alla volta, e ogni volta il messaggio d'errore indicava un
punto diverso dalla causa. I due casi peggiori — `memalign` che segnalava
l'assenza di `extern "C"`, e `cbrt` che segnalava l'assenza di
`nearbyintl` — sono descritti per esteso nelle sessioni (m) e (l).

## Cosa resta per `cc1`

`libstdc++` c'e'. Restano, in ordine:

1. i quattro nomi che GCC usa sull'ospite e che la libc non ha:
   `getrusage`, `getpagesize`, `mmap`, `pipe` (i primi due facili, `mmap`
   ha il ripiego `malloc` in `ggc-page.cc`, `pipe` non serve);
2. il **canadian cross** vero e proprio
   (`--build=x86_64-linux --host=i386-exos --target=i386-exos`);
3. ⚠️ **lo spazio**: `cc1` per x86-64 spogliato e' 50 MB, per i386 sara'
   sui 25-30. Ci sta su un CD, non su un floppy, e vuole una macchina con
   abbastanza RAM — il tetto dello heap della sessione (k) e la
   restituzione della memoria della sessione (l) servono a quello.

## File toccati

| File | Cosa |
|---|---|
| `tools/gcc-exos/applica.py` | `HAVE_STRTOLD` accanto a `os_include_dir` |
| `tools/openlibm-exos/nearbyintl-exos.c` | **nuovo** — la funzione che manca a openlibm |
| `tools/openlibm-exos/prepara-libm.sh` | la compila e la infila in `libm.a` |
| `lib/include/math.h` | le sei macro di confronto, `float_t`/`double_t`, `nearbyintl` |
| `lib/include/inttypes.h` | `intmax_t`, `imaxdiv_t`, `imaxdiv`, `strtoimax`/`strtoumax` |
| `lib/include/libc.h` | `sig_atomic_t` |
| `lib/libc.c` | la famiglia `imax*` |
| `tools/iso/prova-cpp.cpp` | riscritto: usa libstdc++ per intero |
| `tools/iso/prova-mat.c` | la prova di `nearbyintl`, che guarda INEXACT |
| `Makefile` | `provacpp` con una riga sola, se `libstdc++.a` c'e' |

## Comandi

```sh
# libstdc++ per il bersaglio (GCC gia' costruito):
rm -rf ~/gcc-build-cxx/i386-exos/libstdc++-v3     # ⚠️ la cache di configure
cd ~/gcc-build-cxx
make -j1 all-target-libstdc++-v3 && make -j1 install-target-libstdc++-v3

make iso
EXOS_QEMU_EXTRA="-cdrom dist/exos-tools.iso" \
    python3 tools/qemu_drive.py "/cdrom/bin/provacpp@10"
```

---

# SESSIONE 2026-08-03 (m) — Il C++ gira, e la riga che mancava da sempre

Kernel **invariato a 0.157**: e' tutto libc e header. `/bin/libctest`
passa **239 prove su 239**.

## Il C++ gira dentro EX-OS

```
ex-os:/> /cdrom/bin/provacpp
C++ dentro EX-OS

  cerchio  area = 12566 (x1000)
  quadrato area = 9000 (x1000)

  template: massimo(3, 7) = 7, massimo(2.5, 1.5) = 2500
  la libc risponde anche da C++ (29 caratteri)

Il C++ gira.
```

Classi, ereditarieta', funzioni virtuali (con vtable), template, e la libc
di EX-OS chiamata da C++. Sorgente in `tools/iso/prova-cpp.cpp`, sul CD sia
compilato (`/bin/provacpp`) sia in sorgente.

⚠️ **Si compila con `g++` e si collega con `gcc`**, ed e' la descrizione
esatta di dove siamo: `g++` in fase di collegamento chiede `-lstdc++`, che
per i386-exos non c'e' ancora; `gcc` no. Quello che il programma usa lo
risolve tutto il compilatore e non ha bisogno di una libreria a runtime.

⚠️ **Niente distruttore virtuale**, ed e' il confine esatto: un
`virtual ~Forma()` fa emettere anche il *deleting destructor* (`D0`), che
chiama `operator delete` — cioe' libsupc++:

```
undefined reference to `operator delete(void*, unsigned int)'
```

Non e' un errore del programma: e' la prova che il pezzo mancante e' quello
che si sta costruendo.

## ⚠️ LA RIGA CHE MANCAVA DA SEMPRE — `extern "C"`

**Nessuno dei nostri header aveva la guardia `#ifdef __cplusplus extern "C" {`.**

Il C++ decora i nomi delle funzioni con i tipi degli argomenti — `printf`
diventa `_Z6printfPKcz` — perche' gli serve per il sovraccarico. La nostra
`libc.a` e' compilata da un compilatore C e dentro ha il nome nudo. Senza
la guardia, **ogni programma C++ chiamava simboli che nell'archivio non
esistono e non sono mai esistiti.**

⚠️ **E il sintomo era fuorviante.** Non e' arrivato al collegamento: e'
arrivato prima, in compilazione, perche' la libstdc++ dichiara per conto
suo alcune funzioni della libc con `extern "C"` esplicito
(`libsupc++/new_opa.cc` lo fa con `memalign`). Il messaggio dice una cosa
che sembra assurda:

```
error: conflicting declaration of 'void* memalign(size_t, size_t)'
       with 'C' linkage
```

cioe' «questa dichiarazione e' in conflitto con se stessa». La causa e' che
la NOSTRA era in C++ e la loro in C: due funzioni diverse con lo stesso
nome. Ho perso tempo a cercare il problema in `memalign`, che non c'entrava
niente.

Aggiunta a `libc.h` e `math.h` — gli unici due header del progetto che
dichiarano funzioni proprie (gli altri sono facciate che includono
`libc.h`). Verificato che un programma C++ ora si collega davvero, non solo
compila:

```
$ i386-exos-nm provacpp | grep -E ' T (printf|strcpy|sqrt)$'
08002b80 T printf
08008490 T sqrt
08001760 T strcpy
```

## ⚠️ `sleep()` ritornava `void`, e POSIX dice `unsigned int`

Non e' una finezza. Il modo canonico di usarla e'

```c
while ((secs = sleep(secs))) { }
```

cioe' «riprova finche' non hai finito davvero», ed e' esattamente cio' che
scrive `src/c++11/thread.cc` della libstdc++. Con un ritorno `void` quella
riga non compila:

```
error: void value not ignored as it ought to be
```

⚠️ Su EX-OS il valore e' **sempre 0**, e non e' una bugia: non ci sono
segnali che possano interrompere il sonno, quindi la dormita e' sempre
completa. **La firma dice la verita' sul contratto, il valore dice la
verita' su questo sistema.** Stessa correzione per `usleep`, che ora
ritorna `int`.

## I nomi che servono a essere nominati, non a essere ritornati

Tre gruppi di costanti aggiunti per lo stesso motivo, e vale la pena
capirlo una volta sola:

| Gruppo | Chi le chiede | EX-OS le usa? |
|---|---|---|
| 40 codici errno di rete e IPC (`ECONNRESET`, `ENOLCK`, …) | `std::errc` in `<system_error>` | **mai** |
| `DT_FIFO`, `DT_CHR`, `DT_BLK`, `DT_LNK`, `DT_SOCK`, `DT_WHT` | `std::filesystem::file_type` | **mai** |
| `S_IFLNK`, `S_IFSOCK`, `S_ISBLK()`, … e i bit dei permessi | idem | **mai** |

⚠️ **Un nome mancante e' un errore di compilazione, non un ramo morto.**
La libstdc++ scrive `case DT_LNK:` in uno switch: le serve la costante, non
il comportamento. Ritornare sempre 0 da `S_ISLNK()` e' la risposta
**giusta** — su EX-OS un collegamento simbolico non esiste, quindi «questo
file e' un collegamento?» ha davvero risposta no. E' diverso dal caso in
cui non si sapesse rispondere.

⚠️ **I valori sono quelli di Linux e non vanno reinventati**: il giorno che
EX-OS avesse i collegamenti simbolici, `S_IFLNK` dovra' valere `0120000`
come ovunque, o ogni programma portato di la' leggerebbe il tipo sbagliato.

⚠️ `EWOULDBLOCK` **e'** `EAGAIN` e `ENOTSUP` **e'** `EOPNOTSUPP`, stesso
numero, come su Linux: dargli valori propri romperebbe ogni
`if (errno == EAGAIN || errno == EWOULDBLOCK)`.

## Altre due, e nessuna delle due era nota

**`mktime` non esisteva** — ma il README diceva di si'. Ora c'e', e
⚠️ **normalizza la struttura che riceve**, che e' meta' del suo lavoro:
`tm_mon = 12` diventa gennaio dell'anno dopo, `tm_sec = 90` diventa un
minuto e mezzo. Per questo il parametro non e' `const`.

**`localeconv` e `struct lconv`.** ⚠️ I campi non specificati valgono
**127** (`CHAR_MAX`), non zero: 127 significa «questa locale non lo dice»,
zero significa «zero cifre». Un programma che formatta denaro leggendo
`frac_digits` stamperebbe, con lo zero, importi senza decimali credendo che
sia la regola locale. E' l'errore classico di chi riempie questa struttura
a memoria.

## Come si e' lavorato, e come NON si e' lavorato

⚠️ **Dopo il primo errore non si e' andati a tentativi.** L'elenco di cio'
che la libstdc++ pretende e' stato estratto dai suoi header:

```sh
grep -rhoE '^\s*using ::[a-zA-Z_][a-zA-Z0-9_]*;' cstring cstdio cstdlib ... \
  | sed 's/.*using :://;s/;//' | sort -u
```

225 nomi, di cui ~150 `wchar_t` che **non servono**: `_GLIBCXX_USE_WCHAR_T`
risulta spento nella nostra configurazione. Lo stesso metodo per le
costanti: si e' letto `bits/error_constants.h` e `src/filesystem/*` e si e'
fatto il diff con i nostri header, invece di aspettare l'errore
successivo.

⚠️ **Il diff con `grep -w` da' falsi negativi**: `mktime` risultava
presente perche' comparivano in un COMMENTO di `time.h` che diceva che non
c'era. Il controllo giusto e' su `^#define NOME` o sulla forma di una
dichiarazione.

## ⚠️ LA TRAPPOLA PIU' COSTOSA — sei macro mancanti, 136 errori lontanissimi

A un certo punto `std.cc` (il modulo `import std;` del C++23) ha prodotto
**136 errori** tutti della forma:

```
error: 'trunc' has not been declared in 'std'
error: 'strtoll' has not been declared in 'std'
error: 'vsnprintf' has not been declared in 'std'
```

⚠️ **Tutte quelle funzioni ESISTONO nella nostra libc.** Il messaggio non
dice «manca `trunc`»: dice «manca `std::trunc`», ed e' un'altra cosa. La
libstdc++ mette una funzione in `namespace std` solo se il suo configure
ha stabilito che l'header di provenienza e' conforme al C99, e quel
verdetto sta in `_GLIBCXX_USE_C99`, che era **spento**.

La causa vera, dentro `config.log`:

```
conftest.cpp:39: error: 'isgreater' was not declared in this scope
conftest.cpp:40: error: 'isgreaterequal' was not declared in this scope
...
```

Sei macro di confronto del C99 che non avevamo. Il configure compila **un
solo** programma che le usa tutte, e **una sola assenza fa dichiarare non
conforme l'header intero** — con cui la libreria rinuncia a esportare
decine di funzioni che invece ci sono.

⚠️ **Non sono sinonimi di `<`, `<=`, `>`, `>=`.** Con un operando NaN danno
la stessa risposta (falso) ma non lo stesso effetto: l'operatore solleva
l'eccezione "invalid" dell'x87, queste macro no. E' l'unica ragione per cui
esistono, e sono `__builtin_*`: le fa il compilatore, quindi **non serve
`-lm`**.

Stessa storia per `<inttypes.h>`, dove mancavano `imaxdiv`, `strtoumax` e
`intmax_t`: il commento nel file diceva «nessuno li ha ancora chiesti».
Adesso li chiede la libstdc++.

## ⚠️ E LA CACHE DI configure ANDAVA BUTTATA

Aggiunte le macro, il build **falliva ancora allo stesso modo**. Il motivo:
`config.log` e `config.h` di libstdc++ erano del configure di ORE prima,
con `glibcxx_cv_c99_math_cxx98=no` gia' deciso e messo in cache. Le
funzioni nuove nella libc non cambiavano niente, perche' nessuno le stava
piu' cercando.

```sh
rm -rf ~/gcc-build-cxx/i386-exos/libstdc++-v3
make -j1 all-target-libstdc++-v3      # riconfigura SOLO libstdc++
```

⚠️ **Si cancella la directory di build di libstdc++, NON si rilancia il
configure di primo livello**: quello rigenererebbe i file di GCC e
farebbe ricompilare l'intero compilatore, che a `-j1` sono ore. E' la
lezione della sessione (k), applicata.

Dopo: `_GLIBCXX_USE_C99` passa da `#undef` a `1`, e i 136 errori spariscono
tutti insieme.

## ⚠️ `nearbyintl` MANCA A openlibm 0.8.7 — e costa cara

`src/s_nearbyint.c` genera le funzioni da una macro:

```c
DECL(double, nearbyint, rint)
DECL(float, nearbyintf, rintf)
```

e la terza riga — `DECL(long double, nearbyintl, rintl)` — **non c'e'**.
`rintl` invece c'e', ed e' pure in assembly x87 (`i387/s_rintl.S`): manca
solo l'involucro. Non e' una nostra configurazione sbagliata, e' una lacuna
a monte.

Costa cara per il motivo di sempre: il configure della libstdc++ compila
**un solo** programma che usa tutte le funzioni C99 di `<math.h>`, e una
sola assenza fa dichiarare non conforme l'header intero. L'errore che si
vede poi e' `std.cc: 'cbrt' has not been declared in 'std'` — cioe' «manca
`cbrt`» quando `cbrt` c'e' e a mancare e' `nearbyintl`.

Aggiunta in `tools/openlibm-exos/nearbyintl-exos.c` e infilata
**nell'archivio** da `prepara-libm.sh`, non nella libc: cosi' resta vero che
«se e' dichiarata in `math.h` sta in `libm.a` e vuole `-lm`».

⚠️ **La differenza fra `rintl` e `nearbyintl` e' UNA SOLA**: danno lo stesso
numero, ma `rintl` alza il flag INEXACT quando l'argomento non era gia'
intero e `nearbyintl` no. Per questo la prova in `prova-mat.c` **non
confronta solo i valori** — legge la status word dell'x87 con `fnstsw` e
guarda il bit 5. Confrontare i numeri proverebbe la meta' sbagliata.

```
ex-os:/> /cdrom/bin/provamat
    nearbyintl(2.5)=2  nearbyintl(-2.5)=-2  nearbyintl(3.7)=4
    stesso risultato: si
    rintl alza INEXACT: si   nearbyintl: no
```

### ⚠️ Il difetto che ci ho messo dentro io, e come si e' visto

La prima versione ritornava **zero** per ogni argomento. Il motivo:

```c
r = rintl(x);
__asm__ ("fldenv %0" : : "m" (env));   /* <-- qui il valore sparisce */
return r;
```

`fldenv` ripristina anche la **tag word**, cioe' quali registri dell'x87
risultano occupati. Un `long double` restituito da una funzione sta in
`st(0)`, e GCC lo tiene volentieri li' invece di scriverlo in memoria: al
`fldenv` quel registro viene rimarcato come **vuoto** e il valore sparisce.

Nessun errore, nessun avviso: solo `nearbyintl(2.5) == 0`. Il rimedio e'
forzare il risultato in memoria prima:

```c
__asm__ __volatile__ ("" : "+m" (r));   /* da qui `r` sta in memoria */
```

Si controlla nel disassemblato che ci sia una `fstpt` **prima** della
`fldenv`. E' cio' che fa anche FreeBSD, dove il valore passa per una
variabile prima di `fesetenv()`.

### ⚠️ E un errore di conduzione: due membri omonimi in un archivio

Per provare in fretta la correzione ho ricompilato l'oggetto a mano
chiamandolo `nbl.o` e l'ho aggiunto con `ar rcs`. Ma `prepara-libm.sh` lo
inserisce come `nearbyintl-exos.o`: **in `libm.a` sono finite due
definizioni di `nearbyintl`**, una vecchia e una nuova, e il collegatore
prendeva quella che trovava per prima.

`ar` sostituisce per NOME DEL MEMBRO, non per simbolo definito. Il risultato
e' stato mezz'ora passata a cercare un difetto nella FPU di EX-OS che non
c'era: lo stesso identico binario dava i valori giusti su Linux e zeri
dentro EX-OS, il che sembrava — e non era — un problema del sistema. La
diagnosi vera e' arrivata da un programmino che stampava control word,
status word e tag word: **identiche sulle due macchine**, quindi il
sospetto era mal riposto.

Rifatto con lo script, che ricostruisce l'archivio da zero, i valori sono
corretti su entrambe.

## Progressione, per dare la misura

Oggetti compilati prima di fermarsi, tentativo per tentativo. Il conteggio
riparte a ogni directory:

```
3  →  25  →  28  →  47  →  22 (src/c++11)  →  37  →  26  →  153+ (0 errori)
```

`libsupc++` e `src/c++11` complete, `src/c++17` in corso.

## File toccati

| File | Cosa |
|---|---|
| `lib/include/libc.h` | **`extern "C"`**, 40 errno, `DT_*`, `S_IF*`/`S_IS*`/permessi, `mktime`, `localeconv`, `sleep`/`usleep` |
| `lib/include/math.h` | `extern "C"`, le sei macro di confronto C99, `float_t`/`double_t` |
| `lib/include/inttypes.h` | `intmax_t`, `imaxdiv_t`, `imaxdiv`, `strtoimax`/`strtoumax` |
| `lib/libc.c` | `mktime`, `localeconv`, `sleep`/`usleep` con il ritorno giusto, la famiglia `imax*` |
| `tools/iso/prova-cpp.cpp` | **nuovo** — il C++ sul CD |
| `Makefile`, `tools/iso/leggimi.txt` | `provacpp` sul CD se `i386-exos-g++` c'e' |
| `bin/libctest/libctest.c` | +14 prove (225 → 239) |

---

# SESSIONE 2026-08-03 (l) — La memoria torna al kernel, e due difetti che si tenevano nascosti a vicenda

Kernel **0.157**. `/bin/libctest` passa **210 prove su 210** (erano 194).

## Il punto di partenza

`free()` non chiamava **mai** `sbrk` con un incremento negativo. Un blocco
liberato tornava disponibile per il **processo**, non per il **sistema**: un
programma che alloca a picchi — cioe' qualunque compilatore, che costruisce
e butta un albero di sintassi per funzione — teneva il picco massimo fino
alla propria uscita. Con `cc1` e `as` che girano di seguito sulla stessa
macchina, il primo affamava il secondo pur avendo gia' finito.

Ora la coda dello heap si restituisce. Solo la coda: `sbrk` sposta un
confine e non sa bucare il mezzo.

```
Crescita dello heap:
  [ok]     2 MB fanno salire il confine
  [ok]     e la free lo fa riscendere
  [ok]     ma una coda si tiene
  [ok]     e si rialloca senza danni
```

⚠️ **La prova guarda `sbrk(0)`, non `malloc()`.** Che `malloc` riesca lo
sapevamo gia': la lista dei blocchi liberi bastava a quello. La domanda e'
se il **confine** si e' abbassato, ed e' l'unica cosa che distingue
«memoria riusabile» da «memoria restituita».

## ⚠️ I DUE DIFETTI CHE SI TENEVANO NASCOSTI A VICENDA

Nessuno dei due si vedeva finche' `sbrk` negativa non veniva usata. E il
primo impediva di accorgersi del secondo.

### 1. La libc chiedeva byte, il kernel dava pagine

`heap_estendi()` chiamava `sbrk(2 MB + 16)`. Il kernel **non** sposta il
confine dei byte chiesti: lo sposta di **pagine intere** (`ALIGN_UP` in
`sys_sbrk`). Quindi il confine saliva di 2 MB + 4096 e la libc si segnava
un blocco di 2 MB + 16: **gli ultimi 4080 byte esistevano, erano del
processo, ed erano invisibili all'allocatore.**

Fino a ieri era solo uno spreco — fino a una pagina per ogni estensione,
che nessuno notava perche' un blocco che non e' in nessuna lista non fa
danni, occupa e basta. Da oggi era un impedimento: il controllo «questo
blocco e' davvero in cima al break?» **non poteva mai riuscire**, perche'
fra la fine del blocco e il confine c'era sempre quel residuo. La prima
versione di `heap_restituisci()` non restituiva niente, e non diceva
perche'. Rimedio: `heap_estendi` chiede a pagine intere.

### 2. `sbrk` negativa buttava via una pagina viva

```c
pages = ALIGN_UP(shrink, PAGE_SIZE) / PAGE_SIZE;   /* <- ALIGN_UP */
for (i = 0; i < pages; i++) {
    uint32_t va = new_end + i * PAGE_SIZE;         /* <- da new_end */
    ...libera...
}
```

Con `heap_end = 0x9000` e `sbrk(-0x800)`: `new_end = 0x8800`, `pages = 1`,
e si libera la pagina che contiene `0x8800` — cioe' `0x8000-0x8FFF`. **Ma
`0x8000-0x87FF` sono ancora del chiamante.**

⚠️ Non dava nessun errore: dava byte che sparivano dallo heap di un
processo che li stava usando. Non si notava perche' **nessuno chiamava mai
`sbrk` con un incremento negativo** — e la prima cosa che lo avrebbe fatto
era proprio la `free()` di questa sessione.

Corretto arrotondando per **difetto**: si restituisce meno di quanto
chiesto, mai una pagina che serve ancora. `heap_end` resta multiplo di
pagina, come lo e' sempre stato crescendo.

## Le tre condizioni di `heap_restituisci()`

| Condizione | Cosa evita |
|---|---|
| arrotonda a pagine intere | che il kernel butti la pagina del nuovo confine |
| `sbrk(0)` per verificare la cima | di restituire memoria di qualcun altro (una `mmap`) |
| tiene 64 KB, soglia 64 KB | due syscall a giro in un ciclo alloca/libera |

⚠️ **`heap_ultimo` non e' «l'ultimo blocco prima del break»**: e' l'ultimo
blocco *della nostra lista*. Fra la sua fine e il confine puo' esserci roba
di qualcun altro, e restituirla sarebbe buttare via memoria che non ci
appartiene. Da qui il controllo esplicito con `sbrk(0)`.

⚠️ **Il controllo di taglia viene PRIMA di `sbrk(0)`**, ed e' voluto: e'
aritmetica pura, quindi la `free()` normale — quella che non restituisce
niente — non paga nessuna syscall. Rimettere il conto dopo annullerebbe il
guadagno che l'allocatore era andato a prendere.

⚠️ **L'intestazione resta dov'e'** e il blocco continua a esistere, solo
piu' corto. Sfilarlo dalla lista avrebbe voluto dire scriverci dentro dopo
averlo smappato.

## Quello che chiedeva la libstdc++

Il primo tentativo di costruire libsupc++ si e' fermato su nomi che i suoi
header `<cstdlib>`, `<cstring>`, `<cstdio>` dichiarano con `using ::`,
**senza chiedersi se qualcuno li usera'**: se il nome non esiste, l'header
non compila e con lui non compila niente.

⚠️ **NON SI E' ANDATI A TENTATIVI.** Dopo il primo errore l'elenco e' stato
estratto dagli header stessi e confrontato con i nostri:

```sh
grep -rhoE '^\s*using ::[a-zA-Z_][a-zA-Z0-9_]*;' cstring cstdio cstdlib ... \
  | sed 's/.*using :://;s/;//' | sort -u
```

225 nomi, di cui **~150 sono `wchar_t`** (`wcs*`, `isw*`, `tow*`) e non
servono: `_GLIBCXX_USE_WCHAR_T` risulta **spento** nella nostra
configurazione, quindi quegli header non vengono nemmeno compilati. Restano
questi, aggiunti tutti in un colpo invece che uno per giro di build:

| Aggiunto | Nota |
|---|---|
| `div_t`, `ldiv_t`, `lldiv_t`, `div`, `ldiv`, `lldiv` | ⚠️ troncamento verso **zero**: `div(-7,2)` = (-3, -1) |
| `llabs`, `atoll` | |
| `rand`, `srand`, `RAND_MAX` | ⚠️ LCG del K&R — **non** e' casuale, non per chiavi |
| `mblen`, `mbtowc`, `wctomb` | ⚠️ ritornano `int`, non `size_t` come le `mbr*` |
| `strxfrm` | ⚠️ ritorna la lunghezza dell'**originale**, non del copiato |
| `fpos_t`, `fgetpos`, `fsetpos` | ⚠️ ritornano 0/-1, non la posizione |
| `setbuf`, `setvbuf` | ⚠️ **setvbuf dice di no** invece di fingere |
| `vscanf`, `_Exit`, `at_quick_exit`, `quick_exit` | |
| `difftime`, `struct timespec`, `timespec_get` | ⚠️ `timespec_get` ritorna **`base`**, non 0 |
| `system` | ⚠️ **ritorna sempre -1 con ENOSYS** |

### ⚠️ `lldiv` non poteva usare l'operatore `/`

Sull'i386 una divisione fra due interi a 64 bit **non e' un'istruzione**:
il compilatore la trasforma in una chiamata a `__divmoddi4` di libgcc, e i
programmi di EX-OS si linkano con `-nostdlib` e **senza libgcc**.

```
ld: libc.o: in function `lldiv':
    undefined reference to `__divmoddi4'
```

L'errore non e' a runtime, e' **al link**, su qualunque programma che
includa la funzione — anche senza chiamarla mai. Da qui `u64_divmod()`, la
divisione a spostamenti (la `div64` che gia' c'era per la printf divide per
una base piccola e tiene il resto in 32 bit: non bastava).

⚠️ Il resto prende il segno del **dividendo**, non del divisore, e il
troncamento e' verso zero: `lldiv(-9000000000, 7)` da' `(-1285714285, -5)`.
Con la divisione fatta a mano quella regola va riprodotta a mano.

### ⚠️ Le due funzioni che dicono di no invece di fingere

**`system()`** ritorna sempre -1 con `ENOSYS`, e non e' un segnaposto:
`/bin/sh` ha un `_start(void)` e legge solo dal terminale, quindi non c'e'
niente a cui passare la stringa. `system(NULL)` ritorna 0, che e' il modo
corretto di dire «non c'e' un interprete».

**`setvbuf()`** ritorna diverso da zero. La bufferizzazione di EX-OS non e'
regolabile — i buffer stanno dentro la struttura `FILE`, non allocati a
parte. Un programma che chiede `_IONBF` e riceve 0 andrebbe avanti convinto
che ogni `putc` sia gia' arrivato: su un log di debug e' la differenza fra
vedere l'ultima riga prima di un crash e non vederla.

⚠️ **`system()` c'e' ma non esegue niente, e lo dice.** Non e' un
segnaposto: e' la risposta giusta finche' `/bin/sh` non sa accettare un
comando sulla riga di argomenti — oggi ha un `_start(void)` e legge solo
dal terminale, quindi non c'e' niente a cui passare la stringa. Ritornare 0
fingendo di aver eseguito sarebbe peggio di non esserci: il chiamante
andrebbe avanti convinto che il comando sia stato fatto. `system(NULL)`
ritorna 0, che e' il modo corretto di dire «non c'e' un interprete».

## ⚠️ Una trappola nella conduzione, non nel codice

Il primo build di libstdc++ e' stato lanciato cosi':

```sh
make -j1 all-target-libstdc++-v3 > log 2>&1; echo "exit=$?"
```

Il task e' stato riportato **completato con exit 0** — ma quello era
l'exit code dell'`echo`, non di `make`. La build era **fallita**. In un
comando in background, l'ultimo comando della catena e' quello che
determina l'esito: niente dopo il `make`, o l'errore diventa invisibile.

## File toccati

| File | Cosa |
|---|---|
| `lib/libc.c` | `heap_restituisci()`, `heap_estendi` a pagine, `div`/`ldiv`, `rand`/`srand`, `mblen`/`mbtowc`/`wctomb`, `system` |
| `lib/include/libc.h` | i prototipi, `div_t`/`ldiv_t`, `RAND_MAX` |
| `kernel/syscall/syscall_impl.c` | `sbrk` negativa: arrotondamento per difetto |
| `kernel/include/version.h` | 0.156 → 0.157 |
| `bin/libctest/libctest.c` | +12 prove (194 → 210) |

---

# SESSIONE 2026-08-03 (k) — Lo heap aveva un tetto solo per finta

Kernel **0.156**. `/bin/libctest` passa **194 prove su 194** (erano 188).
Nessuna funzione nuova: e' un confine che mancava.

## Cosa andava storto

`sys_sbrk` cresceva finche' `pmm_alloc_page()` aveva pagine da dare.
L'unico limite era la **RAM fisica**, non lo spazio di indirizzamento — e
sopra lo heap non c'e' il vuoto:

```
heap_start ....... heap_end -->        <-- tls_base    riserva stack
0x0800c000                              0xbffbd000     0xbffbf000
```

E `paging_map_page()` (`kernel/mm/paging.c`) **sovrascrive una PTE gia'
presente senza dire niente**. ⚠️ Uno heap che avesse superato `tls_base`
avrebbe rimappato **il blocco TLS del processo stesso** su pagine nuove
azzerate: il thread pointer sarebbe andato a zero e ogni variabile
`__thread` avrebbe cominciato a leggere memoria altrui. **Senza un fault,
senza un log, senza niente.** Piu' sopra c'e' la riserva dello stack, dove
il danno sarebbe stato lo stesso al contrario: pagine gia' mappate proprio
dove `page_fault_handler` conta di poterle mappare lui.

## ⚠️ Due cose che avevo detto e che il codice ha smentito

**«Lo heap parte da `USER_SPACE_BASE` (64 MB) e sbatte contro il testo del
programma a 128 MB».** Falso: `elf.c` (Passo 7) fa gia' partire lo heap
**subito dopo l'ultimo segmento caricato**. Il limite di 64 MB non e' mai
esistito per un processo caricato da `elf_load` — e sono tutti.

Quello che esisteva davvero era il **ripiego** dentro `sys_sbrk`:

```c
if (proc->heap_start == 0) {
    proc->heap_start = USER_SPACE_BASE;   /* 0x04000000 */
    proc->heap_end   = USER_SPACE_BASE;
}
```

Quello si', partiva **sotto** l'immagine del programma. Oggi non ci arriva
nessuno, ma era una risposta plausibile e sbagliata che aspettava il primo
processo costruito in un altro modo. Ora non c'e' piu': chi non ha uno
spazio di indirizzamento preparato da `elf_load` **non ha uno heap**, e
`sbrk` glielo dice con `ENOMEM` invece di indovinare un indirizzo.

## Il tetto

Nuovo campo `Process.heap_max`, calcolato in `elf.c` **dopo** il blocco
TLS e lo stack, perche' dipende da entrambi:

```c
uint32_t soffitto = proc->tls_base ? proc->tls_base : proc->user_stack_limit;
proc->heap_max = soffitto - PAGE_SIZE;      /* una pagina di guardia */
```

Verificato al caricamento, con `verboseboot = 1`:

```
ELF: TLS 0xbffbd000-0xbffbe000, tp=0xbffbd014 (memsz=20 filesz=20 align=4)
ELF: heap utente 0x0800c000, tetto 0xbffbc000 (2943 MB)     <- libctest, con TLS
ELF: heap utente 0x08005000, tetto 0xbffbe000 (2943 MB)     <- la shell, senza
```

Il confine e' controllato in **tutte e due** le vie per cui lo spazio di un
processo cresce:

| Dove | Cosa cambia |
|---|---|
| `sys_sbrk` | controllo **prima** di allocare, `ENOMEM` |
| `sys_mmap` senza `MAP_FIXED` | stesso tetto: anche questa via alza `heap_end` |
| `sys_mmap` con `MAP_FIXED` | rifiuta sopra il tetto con `EINVAL` |

⚠️ **Il controllo sta prima del ciclo, non dentro.** Fermarsi a meta'
lascerebbe `heap_end` avanzato di un valore che il chiamante non ha mai
visto — e la richiesta e' atomica: o cresce tutta o non cresce.

⚠️ **`MAP_FIXED` non puo' piu' prendersi la zona alta.** Su POSIX
`MAP_FIXED` sostituisce cio' che trova, e va bene finche' si tratta di
roba del processo. Il blocco TLS e la riserva dello stack non lo sono: il
kernel ci tiene degli invarianti sopra. Rimpiazzarli non da' un errore, da'
un processo che legge variabili `__thread` altrui o uno stack che smette di
crescere.

⚠️ **Il confronto e' scritto per non traboccare**: `heap_max - heap_end`
a sinistra invece di `heap_end + n` a destra. `heap_end` e' gia' vicino a
3 GB e la somma su 32 bit gira; la sottrazione no, perche'
`heap_max >= heap_end` per costruzione.

## ⚠️ La prova non raggiunge il tetto, e non puo'

Fra lo heap e la riserva dello stack ci sono **2943 MB**, mentre QEMU qui
ha **32 MB**: la memoria fisica finisce molto prima dello spazio di
indirizzamento. Il tetto serve alla macchina che di RAM ne ha abbastanza —
quella su cui un giorno girera' `cc1`.

Quello che `prova_tetto_heap()` prova e' l'altra meta', ed e' altrettanto
importante: che un `sbrk` **rifiutato lasci il processo esattamente com'era**.

```
Crescita dello heap:
  [ok]     sbrk(0) da' la cima dell'heap
[ERROR] PMM: OUT OF MEMORY! Nessuna pagina fisica libera.
  [ok]     sbrk finisce per rifiutare, invece di crescere all'infinito
  [ok]     sbrk negativo restituisce la memoria
  [ok]     e lo heap torna dov'era
  [ok]     le variabili __thread sono intatte
  [ok]     l'heap funziona ancora dopo il rifiuto
```

L'`[ERROR]` del PMM e' **voluto**: la prova cresce apposta finche' la RAM
finisce. Le due righe che contano sono le ultime due — il vicino di sopra
(il blocco TLS) e' intatto, e l'heap funziona ancora.

⚠️ La prova **restituisce** la memoria con `sbrk` negativo, e non e'
pulizia facoltativa: `free()` non chiama mai `sbrk` con un incremento
negativo, quindi senza quella riga il processo terrebbe tutta la RAM libera
del sistema fino alla propria uscita e le prove successive non
troverebbero piu' niente.

## File toccati

| File | Cosa |
|---|---|
| `kernel/include/sched.h` | `Process.heap_max` |
| `kernel/loader/elf.c` | calcolo del tetto (Passo 7) |
| `kernel/syscall/syscall_impl.c` | `sys_sbrk`, `sys_mmap`; via il ripiego |
| `kernel/mm/paging.c` | documentato il contratto di `paging_map_page` |
| `kernel/include/version.h` | 0.155 → 0.156 |
| `bin/libctest/libctest.c` | `prova_tetto_heap()`, +6 prove (188 → 194) |

## Cosa resta aperto qui

`free()` non restituisce mai memoria al kernel: `sbrk` negativo esiste ed
e' provato, ma l'allocatore non lo chiama. Un processo che alloca a picchi
tiene la RAM fino all'uscita. Per `cc1` — che alloca a ondate e libera fra
una funzione e l'altra — questo conta, e va affrontato prima del porting,
non dopo.

---

# SESSIONE 2026-08-03 (j) — Una libm vera, e il prezzo di una parola

Kernel **0.155**: l'unica modifica sotto `kernel/` e' un **rinomino**, e non
cambia il comportamento di niente. `/bin/libctest` passa **188 prove su
188** (erano 182). E' il passo 3 della lista della sessione (e): la libm che
serve alla libstdc++, che serve a `cc1plus`.

## La matematica risponde, dentro EX-OS

```
ex-os:/> /cdrom/bin/provamat
openlibm dentro EX-OS

sin(pi/2)=1000 cos(0)=1000 pow(2,10)=1024000 log(e)=1000 sqrt(16)=4000
atan2(1,1)=785 hypot(3,4)=5000 isnan(0/0.)=1
sinf(pi/2)=1000  sinl(pi/2)=1000  exp2(10)=1024000

La libm risponde.
```

⚠️ **I valori sono moltiplicati per mille e stampati come interi**: la
`printf` di EX-OS non formatta i `double` (non c'e' `%f`), e mostrarli in
virgola mobile avrebbe provato la printf invece della libm — fallendo per
un motivo che con la matematica non c'entra. Quindi `pow(2,10)` si legge
1024000 = 1024,000 e `atan2(1,1)` = 785 = 0,785 = π/4.

Il sorgente e' `tools/iso/prova-mat.c` e finisce sul CD degli strumenti sia
compilato (`/bin/provamat`) sia come sorgente (`/prova-mat.c`), come
`prova-mp.c`. Le varianti `f` e `l` sono provate a parte perche' **non sono
scorciatoie: sono implementazioni distinte**, e senza provarle non si sa se
ci sono davvero.

## ⚠️ Perche' una libm DI TERZI, dopo aver scritto per un anno che non ce n'era una

Fino a ieri `lib/include/math.h` dichiarava tre funzioni e diceva che una
libm non c'era, con la motivazione che vale ancora oggi parola per parola:

> «una `sqrt` quasi giusta e' peggio di nessuna `sqrt`: sbaglia in
> silenzio».

**Quel ragionamento non e' cambiato — e' cambiata la conseguenza.** La
risposta coerente a "non so scrivere `sin` con l'errore giusto" non era
scriverne una mediocre: era **portarne una vera**. Quello che c'e' dietro i
nomi ora e' **openlibm 0.8.7**, cioe' la `msun` di FreeBSD in versione
autonoma (licenza MIT/BSD): l'implementazione di riferimento del settore,
con trent'anni di correzioni sugli arrotondamenti, e con una directory
`i387` che usa le istruzioni dell'x87 dove convengono.

**Chi la chiede: la libstdc++.** Il suo `<cmath>` scrive `using ::sin;` per
circa centottanta nomi, e quei nomi devono ESISTERE o la libreria non
compila. Senza libm non c'e' libstdc++, e senza libstdc++ non c'e'
`cc1plus`.

## Il nuovo `<math.h>`: 184 prototipi, e nessuno inventato

`lib/include/math.h` e' stato riscritto (283 righe). ⚠️ **Le dichiarazioni
sono state RICAVATE dai simboli davvero definiti in `libm.a`**, non copiate
da uno standard: se una funzione e' dichiarata li', esiste. E' la stessa
regola di sempre, applicata a un elenco piu' lungo.

⚠️ **Chi usa queste funzioni deve linkare `-lm`.** Le eccezioni sono
`sqrt`, `fabs`, `ldexp` e `frexp`, che stanno nella libc.

⚠️ **`sqrt` resta nella libc e la doppia definizione NON e' un problema.**
`libm.a` definisce `sqrt` (`i387/e_sqrt.S`) e la nostra libc pure — la
versione a due istruzioni con il controllo di `EDOM`. Provato **in
entrambi gli ordini di collegamento**: vince sempre quella della libc,
perche' `libc.o` viene tirato dentro comunque (printf, crt0) e a quel punto
il simbolo e' gia' risolto, quindi `e_sqrt.S.o` non entra. Non c'e'
"multiple definition". La conseguenza utile e' che i programmi di EX-OS
usano `sqrt` **senza `-lm`**, che e' cio' che fa `/bin/libctest`.

## ⚠️ `ARCH=i387` e `OS=Linux`, e nessuno dei due e' una bugia

`tools/openlibm-exos/prepara-libm.sh` costruisce con:

```sh
make ARCH=i387 OS=Linux USEGCC=1 \
     CC=i386-exos-gcc AR=i386-exos-ar RANLIB=i386-exos-ranlib libopenlibm.a
```

`ARCH` sceglie le implementazioni in assembly x87 — che e' il coprocessore
che EX-OS ha e inizializza (`kernel/include/fpu.h`). `OS` serve solo a
openlibm per decidere il formato della libreria e i flag: "Linux" significa
"ELF con le convenzioni di sempre", che e' esattamente il nostro caso. Non
c'e' un `OS=exos` da aggiungere perche' non ci sarebbe niente da metterci
dentro di diverso.

Lo script **verifica** invece di fidarsi: controlla che diciannove nomi
siano davvero in archivio, e poi **collega un programma che li usa**. Un
archivio con dentro meta' delle funzioni si distingue in un modo solo:
provandolo.

## ⚠️ LA TRAPPOLA DELLA SESSIONE — `type` e' una parola che non ci appartiene

openlibm compilava e si fermava su:

```
error: two or more data types in declaration specifiers
```

dentro **il nostro** `libc.h`. La causa: openlibm fa, in `src/math_private.h`,

```c
#define type float
```

come parte di un meccanismo di generazione, e la nostra `IpcMessage`
aveva un campo che si chiamava `type`. Dopo il `#define`, quel campo
diventava `float float`.

⚠️ **La colpa non e' di openlibm.** Un header pubblico non puo' usare come
nome di campo una parola cosi' comune da essere un candidato ovvio a
diventare macro nel codice di chiunque: `type`, `min`, `max`, `index`. Il
rimedio corretto e' cambiare il nostro nome, non chiedere a mezzo mondo di
non usare il loro.

Rinominato `type` → **`tipo`** in tutte e quattro le copie:

| File | Cosa |
|---|---|
| `lib/include/libc.h` | `IpcMessage.tipo` (la copia utente) |
| `kernel/include/sched.h` | `IpcMessage.tipo` (la copia kernel) |
| `kernel/include/ipc.h` | il parametro di `ipc_send` |
| `kernel/ipc/ipc.c` | gli usi |

Le due copie della struttura **devono restare identiche** — e' la
convenzione del progetto — quindi il nome cambia anche nel kernel, dove
non servirebbe. Da qui il 0.155: e' un rinomino, non un cambio di
comportamento.

## Allocazione allineata: `memalign`, `aligned_alloc`, `posix_memalign`

Chiesta dalla libstdc++: dal C++17 un tipo con allineamento superiore a
quello naturale non passa piu' per `operator new(size_t)` ma per la
variante allineata, che in `libsupc++/new_opa.cc` e' un involucro attorno a
`memalign()`. **Se `memalign` non c'e', la libstdc++ ne mette una che
ignora l'allineamento richiesto** — cioe' sbaglia in silenzio.

Come si fa con il nostro heap, dove i blocchi sono allineati a otto e
l'intestazione ne occupa sedici: si chiede a `malloc` un blocco abbastanza
grande da contenere il risultato ovunque cada l'allineamento, poi lo si
**spezza in due** mettendo una vera intestazione subito prima
dell'indirizzo allineato.

```
prima:   [hdr b][........... dati grezzi ...........]
dopo:    [hdr b][avanzo][hdr n][ dati allineati ....]
           ^libero               ^ e' questo che si restituisce
```

⚠️ **La conseguenza che conta: il puntatore restituito si libera con
`free()`**, non con una free speciale. Ha davanti a se' un'intestazione
normale, agganciata alla lista in ordine di indirizzo come tutte, quindi
per `free()` e' un blocco qualunque e la fusione con i vicini funziona
senza sapere nulla di tutto questo. La testa resta come blocco **libero**
invece di essere sprecata: su una richiesta con allineamento 4096 sono
fino a quattro KB che tornano disponibili.

⚠️ `posix_memalign` **ritorna** il codice di errore e non lo mette in
`errno`: e' l'eccezione della famiglia, ed e' il modo classico di
sbagliare a usarla.

## ⚠️ La seconda trappola — `.gitignore` si era mangiato roba nostra

Aggiungendo `openlibm-*/` per tenere fuori i sorgenti di terzi, la regola
si e' portata via anche **`tools/openlibm-exos/`**, cioe' lo script che
costruisce la libm. Un pattern git senza slash iniziale combacia a
**qualunque profondita'**.

```
$ git check-ignore -v tools/openlibm-exos/prepara-libm.sh
.gitignore:52:openlibm-*/	tools/openlibm-exos/prepara-libm.sh
```

⚠️ **Una directory ignorata per sbaglio non da' nessun errore**: da' un
repository che al prossimo clone non ha piu' quello script, e nessuno se ne
accorge finche' non serve. Corretto ancorando alla radice **tutti** i
pacchetti di terzi — `/gcc/`, `/make/`, `/sed/`, `/awk/`, `/grep/`,
`/coreutils/`, `/openlibm/`, `/openlibm-*/` — perche' il problema non era
di openlibm, era della forma dei pattern.

## File toccati

| File | Cosa |
|---|---|
| `tools/openlibm-exos/prepara-libm.sh` | **nuovo** — costruisce e verifica openlibm |
| `tools/iso/prova-mat.c` | **nuovo** — `/bin/provamat` sul CD |
| `Makefile` | il CD porta `provamat` se `libm.a` c'e' nel sysroot |
| `lib/include/math.h` | riscritto: 184 prototipi ricavati da `libm.a` |
| `lib/libc.c` | `memalign`, `aligned_alloc`, `posix_memalign` |
| `lib/include/libc.h` | i tre prototipi, e `IpcMessage.tipo` |
| `kernel/include/sched.h`, `kernel/include/ipc.h`, `kernel/ipc/ipc.c` | `type` → `tipo` |
| `kernel/include/version.h` | 0.154 → 0.155 |
| `bin/libctest/libctest.c` | +6 prove (182 → 188) |
| `.gitignore` | openlibm, e **tutti** i pattern ancorati alla radice |
| `tools/gcc-exos/applica.py` | libstdc++: `os/generic` invece di `os/newlib` |
| `tools/iso/leggimi.txt` | la voce di `/bin/provamat` |

## Comandi

```sh
# openlibm NON si scarica da qui, come tutto il codice di terzi:
wget https://github.com/JuliaMath/openlibm/archive/refs/tags/v0.8.7.tar.gz
tar xf v0.8.7.tar.gz -C ~/exos-native

tools/openlibm-exos/prepara-libm.sh          # -> ~/exos-cross/i386-exos/lib/libm.a

make all && make floppy
python3 tools/qemu_drive.py "libctest@25"    # atteso: 188 prove superate, 0 fallite

make iso                                     # il CD prende /bin/provamat
EXOS_QEMU_EXTRA="-cdrom dist/exos-tools.iso" \
    python3 tools/qemu_drive.py "/cdrom/bin/provamat@8"
```

## Cosa viene dopo — libstdc++, e la riga che ho gia' preparato

Il passo successivo e' **libstdc++ per `i386-exos`**, e la modifica che
serve e' gia' in `tools/gcc-exos/applica.py`.

⚠️ **`--with-newlib` per la libstdc++ non vuol dire "usa newlib"**: vuol
dire "NON sei su glibc, non fare i test di collegamento, prendi questa
tabella di risposte" (`libstdc++-v3/configure.ac`, il ramo a riga ~341).
La tabella e' quasi tutta giusta anche per noi — le funzioni `f` della
matematica ci sono ora, `strtof` c'e', `hypot` c'e'. **Una riga e'
sbagliata**, ed e' `os_include_dir="os/newlib"`: quella directory contiene
un `ctype_base.h` scritto sui **macro interni di newlib** (`_U`, `_L`,
`_N`, la tabella `_ctype_`), che nella nostra `<ctype.h>` non esistono e
non esisteranno — sono un dettaglio di implementazione di quella libc, non
un'interfaccia. `os/generic` invece non chiede niente a nessuno.

Quindi si cambia **quella riga sola**, in `configure` e in `configure.ac`
insieme (il primo e' quello che gira, il secondo quello da cui il primo si
rigenera):

```sh
  if test "x${with_newlib}" = "xyes"; then
    case "${host}" in
      *-exos*) os_include_dir="os/generic" ;;
      *)       os_include_dir="os/newlib"  ;;
    esac
```

⚠️ E **`--enable-clocale=generic` sulla riga di configure**, perche'
`--with-newlib` porta il modello di locale a `newlib`, che tira dentro
`config/locale/newlib/ctype_members.cc` — stesso problema del
`ctype_base.h` di sopra.

⚠️ **Riconfigurare in una directory di build gia' fatta ricompila TUTTO
GCC.** `config.status` rigenera i file e make li vede piu' recenti dei
propri oggetti: sono ore, a `-j1`. Vale la pena mettere in conto la spesa
prima, non scoprirla dopo.

---

# SESSIONE 2026-08-02 (i) — GMP, MPFR e MPC per EX-OS

Kernel **invariato a 0.154**: sotto `kernel/` non e' stato toccato niente.
`/bin/libctest` passa **182 prove su 182**. E' il passo 2 della lista della
sessione (e), quello che aspettava il thread pointer.

## Le tre librerie girano dentro EX-OS

```
ex-os:/> /cdrom/bin/provamp
GMP 6.3.0, MPFR 4.2.1, MPC 1.3.1 — dentro EX-OS

GMP   2^128 = 340282366920938463463374607431768211456
MPFR  pi     = 3.1415926535897932384626433832795028841971693993751e0
MPC   sqrt(i)= (7.0710678118654752440e-1 7.0710678118654752440e-1)

Tutte e tre le librerie rispondono.
```

Sono il codice di terzi **piu' pesante** che EX-OS abbia ospitato: 4,6 MB
di archivi, aritmetica a precisione arbitraria, allocazioni continue.
`libgmp.a` 838 KB, `libmpfr.a` 2,99 MB, `libmpc.a` 764 KB, tutte in
`~/exos-cross/i386-exos/lib`.

⚠️ Nell'ordine `-lmpc -lmpfr -lgmp` e non l'inverso: `ld` scorre gli
archivi da sinistra a destra e non torna indietro.

## Quanto e' costato: tre funzioni e una macro

Dopo binutils la libc regge quasi tutto. Le tre librerie hanno chiesto:

| Cosa | Chi | Nota |
|---|---|---|
| `isascii`, `toascii`, `isblank` | `printf/doprnt.c` di GMP | POSIX, non C |
| `sqrt` | `src/eta.c` di MPC | ⚠️ vedi sotto |
| `_STDIO_H` | `gmp.h` | ⚠️ una macro, non una funzione |

⚠️ **`sqrt` entra in `<math.h>` senza contraddire quello che c'e' scritto
sopra.** L'header dice, e continua a dire, che qui non c'e' una libm,
perche' «una sqrt quasi giusta e' peggio di nessuna sqrt». Questa non e'
quasi giusta: e' `fsqrt` dell'x87, **una delle cinque operazioni che
l'IEEE 754 obbliga a essere correttamente arrotondate** (le altre quattro
sono +, -, *, /). Non c'e' un'approssimazione da giudicare, c'e'
un'istruzione da chiamare — ed e' il motivo per cui entra lei e non
entrano `log`, `exp`, `sin`. Su argomento negativo l'x87 da' NaN e noi
impostiamo `EDOM`.

⚠️ **`_STDIO_H` non e' la nostra guardia: e' una bandiera per gli altri.**
`gmp.h` dichiara le funzioni che prendono un `FILE *` — `mpz_inp_str`,
`mpz_out_str` — solo se riconosce che `<stdio.h>` e' gia' stato incluso, e
lo capisce annusando **quindici macro note, una per libc storica**:
`_STDIO_H` per glibc, `__DEFINED_FILE` per musl, `_FILE_DEFINED` per
Microsoft… La nostra guardia si chiama `EXOS_STDIO_H` e non e' in elenco,
quindi gmp.h le ometteva in silenzio e i suoi stessi sorgenti non
compilavano:

```
gmp.h:884: error: implicit declaration of function '__gmpz_inp_str'
```

cioe' una libreria che non compila se stessa perche' non riconosce la libc
sotto. Ora `<stdio.h>` definisce anche `_STDIO_H`, ed e' onesto:
quel contratto lo rispettiamo davvero.

## ⚠️ Un binario di EX-OS parte anche su Linux, e la cosa ha ingannato GMP

GMP compila dei generatori di tabelle (`gen-fac`, `gen-fib`, `gen-bases`…)
che devono girare sulla macchina che compila, e per scegliere il
compilatore fa la prova piu' ragionevole del mondo: ne compila uno e lo
**esegue**. La prova e' riuscita **con il cross-compilatore**.

Non e' un caso: un binario per EX-OS e' un ELF32 i386 statico, Linux lo
carica senza obiezioni, e le syscall di EX-OS hanno i numeri di Linux —
`SYS_WRITE` e' 4 di qua e di la' — quindi anche la `printf` funziona. Il
programma parte, stampa, sembra tutto a posto.

Quello che non combacia sono `argc` e `argv`, che EX-OS passa a modo suo:

```
./gen-fac 32 0 >fac_table.h
Usage: gen-fac limbbits nailbits
```

un generatore che non vede i propri argomenti e si rifiuta di generare.
Il rimedio e' `CC_FOR_BUILD=gcc` esplicito. **Da ricordare per ogni
pacchetto che costruisce strumenti per se stesso** — GCC e' pieno di
questi.

## 🐛 Un difetto di GCC 17, che non e' nostro

Con `--host=i386-pc-exos` GMP compila tutto con `-march=i386 -mtune=i386`,
e su quella strada il compilatore emette una rotazione a 16 bit scritta con
il nome del registro a 32:

```
/tmp/ccRgZogI.s:325: Error: incorrect register `%eax' used with `w' suffix
```

Si riproduce in tre righe, senza GMP di mezzo:

```c
unsigned int scambia(unsigned int v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8)
         | ((v >> 8) & 0xFF00) | (v >> 24);
}
```

```
$ i386-exos-gcc -O2 -march=i386 -mtune=i386 -S -o - scambia.c
        rolw    $8, %eax        <- deve essere %ax
        roll    $16, %eax
        rolw    $8, %eax
```

La causa e' in `gcc/config/i386/i386.md`, `bswaphisi2_lowpart`: la seconda
alternativa stampa `rol{w}\t{$8, %0|...}` con `%0` invece di `%w0`. Si
prende solo quando `bswap` non c'e' (386) **e** la messa a punto preferisce
`rolw` a `xchgb` (`!TARGET_USE_XCHGB`, cioe' tutto tranne il Pentium 4).
Non dipende dal nostro bersaglio: e' un difetto a monte, in questo
snapshot (GCC 17.0.0 20260801).

⚠️ **Non e' la prima volta che lo incontriamo**, e la risposta era gia'
scritta nell'albero: `tools/gcc-exos/applica.py` mette
`with_arch=${with_arch:-i486}` nella voce `i[34567]86-*-exos*` di
`config.gcc`, con il commento «con -march=i386 la libgcc NON COMPILA», che
e' lo stesso difetto trovato costruendo libgcc. Il bersaglio i386-exos ha
gia' i486 come architettura predefinita; a scavalcarla e' stato il TRIPLO
HOST di GMP, che dicendo `i386-pc-exos` si e' portato dietro il proprio
`-march=i386`.

**Non l'abbiamo corretto**, e la ragione e' che non serve: si costruisce con
`--host=i486-pc-exos`, e non e' un ripiego per aggirare il difetto —
⚠️ **486 e' il minimo che EX-OS gia' richiede per conto suo**,
`kernel/mm/paging.c` usa `invlpg`, che e' 486+. Con i486 c'e' `bswap`, e la
strada rotta non si percorre nemmeno.

## Gli strumenti

```
tools/gcclibs-exos/applica.py             una riga in config.sub, per tutte e tre
tools/gcclibs-exos/prepara-gcclibs.sh     configure + make + install + verifica
tools/iso/prova-mp.c                      la prova, che finisce sul CD
```

⚠️ **Il file da toccare non e' lo stesso nei tre alberi**, e non e' sempre
`config.sub`: GMP ha `configfsf.sub` (il suo `config.sub` e' un involucro
che gestisce i nomi di CPU e delega il resto), MPFR ha `config.sub`, MPC
`build-aux/config.sub`. E nemmeno la riga e' la stessa, perche' le tre
versioni hanno terminatori diversi nell'elenco dei sistemi ammessi.

La verifica dello script non si fida del codice di uscita: **collega**
`tools/iso/prova-mp.c` contro tutte e tre. Un archivio presente ma con
simboli irrisolti non si distingue in nessun altro modo.

## Verifica

- `/bin/libctest`: **182 su 182** (erano 177), con `sqrt` esatta sui
  quadrati perfetti, `isascii`/`toascii`/`isblank`.
- `/cdrom/bin/provamp` dentro EX-OS: 2^128, pi a 50 cifre e la radice di i,
  tutti giusti.
- `make iso` include `provamp` da solo se le tre librerie sono nel sysroot,
  e lo dice quando non ci sono.
- Nessuna regressione; il kernel non e' stato toccato.

## Cosa resta per il compilatore

1. **Il sottoinsieme di libstdc++**: `cc1` e' C++, ma GCC compila i propri
   sorgenti con `-fno-exceptions -fno-rtti`, quindi niente unwinder:
   restano `operator new`/`delete`, `__cxa_atexit` e i contenitori che usa
   davvero.
2. **Costruire e installare `cc1`** su ext2 (~35-45 MB per i386).
3. `pex-exos.c` e' gia' pronto dalla sessione (g): il driver di GCC lancia
   i propri figli attraverso quello.

## File toccati

- `lib/libc.c`, `lib/include/libc.h` — `sqrt`, `isascii`, `toascii`,
  `isblank`, `EDOM`
- `lib/include/math.h` — `sqrt` e il perche' non contraddice l'header
- `lib/include/stdio.h` — `_STDIO_H`, la bandiera per il codice di terzi
- `bin/libctest/libctest.c` — 177 → 182 prove
- **nuovi**: `tools/gcclibs-exos/{applica.py,prepara-gcclibs.sh}`,
  `tools/iso/prova-mp.c`
- `Makefile` — `provamp` sul CD se le librerie ci sono (`CROSS_SYSROOT`)

---

# SESSIONE 2026-08-02 (h) — Il thread pointer

Kernel a **0.153** → **0.154**. `/bin/libctest` passa **177 prove su 177**.

La sessione (g) aveva chiuso con un limite invece che con un difetto: `as`
moriva su `mov %gs:0x0,%ebx` perche' EX-OS non aveva un thread pointer, e
il rimedio era disattivare il TLS in ogni pacchetto che si porta. Ora c'e',
e quel rimedio non serve piu'.

## Perche' farlo, dato che qui i fili sono uno per processo

Non serve a EX-OS: una variabile `__thread` con un filo solo e' una
variabile globale con un nome piu' lungo. **Serve perche' il modo in cui
mancava era il peggiore possibile.**

La prova che ogni configure fa per il TLS e' una **compilazione**, e il
compilatore la supera sempre: sa emettere gli accessi via `%gs` da
vent'anni, ed e' il SISTEMA a non avere dove puntarli. Nessun errore,
nessun avviso, un binario che si costruisce benissimo e muore alla terza
istruzione della prima funzione. Il prossimo a chiederlo era MPFR, che
usa `__thread` per la propria cache.

## Com'e' fatto: local-exec, variante II

    indirizzi bassi                          indirizzi alti
    +----------------------+-------------------+
    |  blocco TLS (memsz)  |  TCB (8 byte)     |
    +----------------------+-------------------+
    ^ tls_base             ^ tp

Il TCB comincia con un puntatore a **se stesso**: e' la convenzione ABI, ed
e' cio' che rende `mov %gs:0x0,%ebx` una lettura del thread pointer invece
che di una variabile qualunque. Le variabili stanno a offset **negativi**
da `tp`, gia' risolti da `ld` al link (rilocazioni `R_386_TLS_LE`): **a
runtime non c'e' niente da rilocare**, ed e' il motivo per cui questo
modello costa cosi' poco.

Cinque pezzi, nessuno grande:

| Dove | Cosa |
|---|---|
| `gdt.c` | un settimo descrittore, `0x33`: User Data con una base che cambia |
| `gdt.c` | `gdt_set_tls_base()`, che riscrive solo i tre pezzi della base |
| `sched.c` | la chiama a ogni switch, accanto a `gdt_set_kernel_stack` |
| `context_switch.asm` | i due trampolini caricano `0x33` in GS invece di `0x23` |
| `elf.c` | `PT_TLS`: una copia per processo, sotto lo stack, con la guardia |

⚠️ **Con base zero il descrittore e' indistinguibile da `0x23`.** I
processi senza variabili thread-local non pagano niente: nessuna pagina in
piu', nessun ramo in piu' nel caricatore, e un `%gs:0` legge l'indirizzo
lineare 0 come prima.

## ⚠️ Il pezzo che non era ovvio: gli stub degli interrupt

Tutto quanto sopra non sarebbe bastato. I tre stub — eccezioni, IRQ,
syscall — salvavano **solo DS**, caricavano il selettore dati del kernel in
DS, ES, FS *e GS*, e all'uscita rimettevano in tutti e quattro il DS
salvato. Su un ritorno a ring3 significa **GS = 0x23**, qualunque cosa il
processo ci avesse messo.

Il primo tick di timer avrebbe cancellato il thread pointer. E il guasto
sarebbe comparso **a caso**, perche' dipende da quando arriva l'interrupt:
il genere di difetto che si insegue per giorni.

La soluzione e' non toccarlo: **il kernel non usa GS**. Non c'e' una riga
che dereferenzi `%gs` — niente percpu, niente stack canary — quindi
lasciarlo con il valore dell'utente non espone niente, e in cambio
`context_switch`, che GS lo salva e lo ripristina gia', diventa da solo il
meccanismo che lo rende per-processo.

Il valore che un programma ci puo' mettere e' comunque limitato da una
regola della CPU: `mov gs, ax` con un selettore piu' privilegiato di CPL
solleva #GP al momento del **caricamento**, non dopo.

## ⚠️ E i linker script: due righe in diciotto file

`.tdata` e `.tbss` non sono sezioni normali — insieme sono l'immagine di
partenza che il caricatore copia — e nei linker script di EX-OS non le
nominava nessuno. Senza,

    .tdata : { *(.tdata) *(.tdata.*) }
    .tbss  : { *(.tbss)  *(.tbss.*) *(.tcommon) }

`ld` le sistema dove capita e il segmento `PT_TLS` puo' non essere generato
affatto: il programma compila, si collega, e legge una variabile
thread-local che sta da un'altra parte. Aggiunte a tutti e diciotto,
non solo a quelli che oggi ne hanno bisogno: un programma che comincia a
usare `__thread` non deve rompersi in silenzio.

## Una pagina di guardia fra stack e TLS

Il blocco sta sotto la riserva dello stack, con una pagina non mappata in
mezzo. Non e' un vezzo: senza, una ricorsione infinita scenderebbe dallo
stack **dentro** il blocco TLS — che e' mappato, quindi non fault — e
invece di terminare il processo con «stack esaurito» ne corromperebbe le
variabili in silenzio.

## Cosa NON c'e'

⚠️ **Il TLS dinamico.** `__tls_get_addr`, i modelli general-dynamic e
local-dynamic, le variabili `__thread` dentro una libreria condivisa: non
funzionano. Servono a chi carica codice a runtime, e qui i binari sono
statici. Il giorno che non lo fossero, il punto da cui ripartire e'
`elf.c`, accanto a `PT_TLS`.

E non ci sono i **thread**: questo e' il pezzo che serve ad averli, non
loro. Il blocco e' uno per processo perche' i fili sono uno per processo.

## Verifica

- `/bin/libctest`: **177 su 177**, sei prove nuove. Quella che conta e' la
  terza: il valore sopravvive a **venti** `sched_yield()`, cioe' al
  meccanismo che riscrive la base del descrittore ad ogni switch.
- `.tbss` azzerata, `.tdata` con il valore che sta nel file.
- **binutils ricostruito senza `ac_cv_tls`**, cioe' con `bfd` che usa
  davvero `_Thread_local`: `as` e `ld` girano, la catena
  assembla-collega-esegui e' quella di prima.
- Avvio pulito, nessuna regressione.

## File toccati

- `kernel/include/kernel.h` — `GDT_TLS_SEL`
- `kernel/arch/x86/gdt.c`, `kernel/include/gdt.h` — descrittore 6 e
  `gdt_set_tls_base()`
- `kernel/arch/x86/isr_stubs.asm` — GS non si tocca piu'
- `kernel/sched/context_switch.asm` — i due trampolini caricano `0x33`
- `kernel/sched/sched.c` — la base del TLS a ogni switch
- `kernel/include/sched.h` — `Process.tls_tp`/`tls_base`, `TLS_TCB_SIZE`,
  `TLS_MAX`
- `kernel/loader/elf.c` — `PT_TLS`
- `bin/*/*.ld` (18 file) — `.tdata` e `.tbss`
- `bin/libctest/libctest.c` — 171 → 177 prove
- `kernel/include/version.h` — 0.154

---

# SESSIONE 2026-08-02 (g) — Binutils nativi: quello che chiede il codice che non abbiamo scritto noi

Kernel a **0.150** → **0.153**. `/bin/libctest` passa **171 prove su 171**.

**Un programma assemblato, collegato ed eseguito dentro EX-OS**, da GNU
binutils 2.44 compilato per `i386-exos`:

```
ex-os:/> /cdrom/bin/as -o /prova.o /cdrom/prova.s
ex-os:/> /cdrom/bin/ld -o /prova /prova.o
ex-os:/> /prova
Assemblato e collegato dentro EX-OS.
```

L'oggetto prodotto qui dentro e' **identico byte per byte** a quello che
produce il cross sulla macchina Linux.

Il passo 1 della lista era «binutils veri»; questa sessione fa il passo
successivo di quella riga, quello annotato in
`tools/binutils-exos/leggimi.md`: **binutils NATIVI**,
`--host=i386-exos`, cioe' `as` e `ld` compilati per girare dentro EX-OS.

E' anche cio' per cui erano stati fatti: fino a ieri la libc era provata
solo dal nostro `libctest`, che chiama le funzioni che sappiamo di avere.
Binutils chiama quelle che gli servono, e non ha nessun riguardo.

## ⚠️ Il compilatore installato non aveva le modifiche della sessione (f)

Prima di tutto il resto. La sessione (f) ha scritto in `exos.h`
l'indirizzo di caricamento, `-fno-asynchronous-unwind-tables` e i quattro
tipi fondamentali — e **non ha ricostruito GCC**. `~/exos-cross` conteneva
ancora il compilatore di mezzogiorno: `size_t` era `long unsigned int`,
nelle specs non c'era `-Ttext-segment`, e `tools/gcc-exos/prova.c` — il
programma che quelle modifiche le verifica — **non compilava**.

Le tre correzioni sono ora in vigore davvero, verificate sul binario:

- `__SIZE_TYPE__ unsigned int`, `__INT32_TYPE__ int` (`prova.c` compila);
- `LOAD 0x08000000` in `readelf -l`;
- **zero sezioni `.eh_frame`**.

⚠️ Il terzo ha avuto una coda: `.eh_frame` restava anche con il
compilatore nuovo, 8 KB per binario, e non veniva da GCC —
veniva da **`libc.a`**, che `prepara-cross.sh` compila con il `gcc` di
sistema, cioe' con un compilatore a cui nessuno aveva detto niente. Le
regole di `Makefile` non lo mostravano perche' li' sono i linker script a
buttare via `.eh_frame`. Due strade per la stessa libreria che producono
binari diversi: `prova` e' passato da 43 KB a 35 KB.

## Il conteggio dei riferimenti che mancava — dup, dup2, fcntl

La prima cosa che ha chiesto il codice di terzi. `ar`, `objcopy` e
`arsup` di binutils fanno tutti e tre lo stesso gesto — `fd = dup(fd)`
prima di chiudere l'oggetto BFD che possiede l'originale — per tenere un
file aperto oltre la `close()` di qualcun altro.

Non si poteva fare, e la sessione (e) lo aveva gia' scritto: «passare un
fd vorrebbe dire due processi sullo stesso handle VFS, cioe' un conteggio
di riferimenti che non c'e'». Ora c'e', ed e' quattro righe in `vfs.c`:
`VfsFile` ha un campo `rif`, `vfs_close` scala e chiude davvero solo
l'ultima volta, `vfs_dup` incrementa.

Sopra ci stanno tre syscall con i numeri di Linux: **41 dup**, **63
dup2**, **55 fcntl** (`F_DUPFD`, `F_GETFD`, `F_SETFD`, `F_GETFL`,
`F_SETFL`).

⚠️ **La posizione non e' condivisa, e su POSIX lo sarebbe.** L'offset sta
nel descrittore del processo, non in un oggetto «file aperto» intermedio:
due fd duplicati condividono il file — che e' cio' che serve perche' resti
aperto — ma ognuno ricorda dove era arrivato. Metterlo in comune vorrebbe
dire spostarlo dentro `VfsFile`, quindi cambiare la firma di
`vfs_read`/`vfs_write` e la gestione della posizione di `fat12.c`, che la
tiene per conto suo. Binutils non se ne accorge (in `rename.c` fa un
`lseek(SEEK_SET)` esplicito prima di leggere); una pipe se ne accorgerebbe.

`dup2` e' anche l'unico modo di sostituire stdin/stdout/stderr: `close()`
su 0, 1 e 2 e' rifiutata apposta — lascerebbe il processo senza uscita —
mentre chi arriva da `dup2` il rimpiazzo ce l'ha gia' pronto.

## 🐛 I file aperti alla terminazione non venivano chiusi da nessuno

Trovato guardando dove mettere il conteggio dei riferimenti.
`proc_reap_zombie` chiudeva `exe_handle` e basta: i descrittori che il
processo aveva ancora aperti restavano occupati **per sempre**. Il conto
arriva dopo `VFS_MAX_OPEN` volte, con un `open()` che risponde `EMFILE`
mentre nessuno sta tenendo aperto niente.

Si vedeva poco finche' i programmi aprivano un file per volta e lo
chiudevano. Un compilatore ne tiene aperti sei o sette, e ogni invocazione
che finisce male ne perde altrettanti. La chiusura sta in
`proc_reap_zombie` e non in `sys_exit` di proposito: **un processo
terminato da un fault non passa da `sys_exit`**, e sono proprio quelli che
i file li lasciano aperti.

## pex-exos.c — lanciare un programma senza fork

`libiberty` compila sempre un `pex-*.c`, e per tutto cio' che non e'
Windows o MSDOS sceglie `pex-unix.c`, che e' costruito su `fork()`.

`tools/binutils-exos/pex-exos.c` e' il rimpiazzo, ed e' il passo 5 della
lista della sessione (e), arrivato prima del previsto perche' senza non
compila `libiberty`. Il modello non e' `pex-unix.c` ma **`pex-msdos.c`**:
un «descrittore» e' un indice in una tabella di NOMI, perche' e' il nome
cio' che serve a `spawn_ex`. La differenza rispetto a MSDOS e' che qui il
figlio gira davvero in parallelo — `exec_child` ritorna un PID e `wait`
chiama `waitpid`.

⚠️ **I tre NULL nella tabella `funcs` — pipe, fdopenr, fdopenw — non sono
un "da fare": sono la dichiarazione che questo sistema non ha pipe**, ed
e' cosi' che `pex-common.c` lo viene a sapere. Se ne accorge da solo e
passa alla modalita' a file temporanei, che sapeva gia' fare. Piu' lento,
identico nel risultato.

`PEX_STDERR_TO_STDOUT` risponde `ENOSYS` invece di fingere: «l'errore va
dove va l'uscita» e' una `dup2` nel figlio, cioe' due descrittori sullo
stesso file con una posizione sola, e per percorso diventerebbero due
aperture che si riscrivono sopra.

`applica.py` ora installa il file e lo aggancia in tre punti —
`libiberty/configure`, `configure.ac` e `Makefile.in`. ⚠️ **Si tocca
`configure`, non solo `configure.ac`**: il primo e' il prodotto di
autoconf ed e' quello che gira davvero. E ci vuole una **regola esplicita**
nel `Makefile.in`, perche' la regola implicita di libiberty per i `.c` e'
`false` — apposta — e senza si fallisce con un messaggio che dice solo
«false».

## Quello che mancava alla libc, in ordine di scoperta

Ognuna di queste ha fermato la compilazione di binutils una volta.
L'ordine e' quello vero, e vale la pena tenerlo: e' l'ordine in cui le
chiedera' il prossimo sorgente esterno.

| Cosa | Chi la chiedeva | Nota |
|---|---|---|
| `<sys/types.h>` | 102 inclusioni non condizionate | `pid_t`, `off_t`, `mode_t`… |
| `dup`, `dup2`, `fcntl` | `libiberty/filedescriptor.c`, `ar`, `objcopy` | vedi sopra |
| `frexp` | `libiberty/floatformat.c` | l'inversa di `ldexp` |
| **`EOF`** | `libiberty/safe-ctype.h` | il valore c'era, il **nome** no |
| `mktemp`, `freopen`, `lstat` | `choose-temp.c`, `fopen_unlocked.c`, `unlink-if-ordinary.c` | |
| `strerror` → `char *` | `libiberty/xstrerror.c` | era `const char *` |
| `strcasecmp`, `strncasecmp` | `bfd/archures.c` | + `<strings.h>` |
| `_exit` | `bfd/bfd.c` | esce senza svuotare niente |
| `chmod`, `fchmod`, `umask` | `bfd/opncls.c` | ⚠️ **non fanno niente** |
| `strpbrk` | `gas/config/tc-i386.c` | |
| `strftime` | `gas/listing.c` | sottoinsieme, dichiarato |
| `<wchar.h>`, `mbstowcs` | `gas/read.c` | un byte = un carattere |
| `<sys/param.h>`, `EDOM` | `libctf` | `MIN`, `MAX`, `roundup` |
| `ctime`, `asctime` | `binutils/bucomm.c` | forma fissa, buffer statico |
| **`mkdir` a due argomenti** | `binutils/bucomm.c` | ⚠️ cambio di firma |
| `strcoll` | `binutils/nm.c` | nella locale "C" e' `strcmp` |
| `mbrtowc`, `MB_CUR_MAX` | `binutils/readelf.c` | |
| `utime`, `<utime.h>` | `binutils/rename.c` | ⚠️ **non fa niente** |
| `atof`, `fabs` | `binutils/stabs.c`, `gprof` | |
| `<memory.h>` | `binutils/testsuite` | nome vecchio di `<string.h>` |
| `fscanf`, `scanf`, `vfscanf` | `gprof/corefile.c` | finestra di 1024 byte |
| `realpath`, `<limits.h>` | `libiberty/lrealpath.c`, `ld` | vedi sotto |

Quattro meritano una riga in piu':

⚠️ **`EOF` non c'era.** Il valore c'era dal principio — `fgetc()` ritorna
-1 — ma il nome lo scriveva solo chi aveva letto `libc.h`. Il codice di
terzi scrive `!= EOF` e basta; e `safe-ctype.h` fa di peggio, verifica di
poter lavorare con `#if EOF != -1`, che con la macro assente vede uno zero
e conclude che la libc e' sbagliata.

⚠️ **`strerror` tornava `const char *`.** Piu' sicuro, e **incompatibile**:
lo standard dichiara `char *`, e chi ridichiara la funzione — `xstrerror.c`
lo fa — non compila piu'. Nessun cast serve: in C un letterale di stringa
ha tipo `char[]`. Stessa cosa per `strsignal`.

⚠️ **`mkdir` ora prende due argomenti.** Era `mkdir(const char *)` — piu'
onesta, perche' EX-OS non ha permessi da applicare, e **incompatibile**:
`mkdir(nome, 0755)` non compilava, ed e' cio' che scrive ogni programma
portato da un Unix. I due chiamanti interni (`bin/mkdir`, `bin/install`)
sono stati aggiornati; il secondo argomento si ignora, e sta scritto.

⚠️ **`chmod`, `fchmod`, `umask` e `utime` non cambiano niente**, e fino a ieri non
esistevano proprio: `<sys/stat.h>` diceva che dichiararle «vorrebbe dire
promettere che cambiano qualcosa». Le ha fatte entrare bfd, che chiude
ogni eseguibile che produce con `umask(0); umask(m); chmod(...)`.
L'alternativa era rattoppare i sorgenti di terzi a ogni loro rilascio. Il
nome c'e', il commento dice forte che e' inerte — la stessa convenzione
gia' usata per `O_EXCL` in `<fcntl.h>`. `umask` e' l'unica a dire il vero:
ritorna 0, cioe' «non maschero niente». `utime` ritorna successo di
proposito: `objcopy` e `strip` la chiamano per conservare la data
dell'originale e stampano un avviso a ogni fallimento, e un avviso per
file su un'operazione che non e' andata storta e' solo rumore.

## ⚠️ Due trappole della catena, che costano un'ora a testa

**`-std=gnu17` va passato a mano.** GCC 17 compila in C23, dove una
dichiarazione implicita e' un **errore** e non un avviso. binutils 2.44 e'
pieno di codice che presume l'indulgenza di C17, e senza quel flag si
passa il tempo a rincorrere errori che non sono difetti. Con il flag, gli
errori che restano sono quelli veri — ed e' cosi' che l'elenco qui sopra
e' venuto fuori pulito.

**Il configure va rifatto ogni volta che la libc cresce.** `libiberty`
compila una **propria** copia delle funzioni che l'ospite non ha
(`strcasecmp.o`, `strdup.o`…) in base a cio' che il configure ha trovato
il giorno in cui e' girato. Se poi la libc quella funzione ce l'ha, il
link di `as-new` finisce con

```
multiple definition of `strcasecmp'
```

⚠️ e non basterebbe cambiare l'ordine delle librerie, perche' **`libc.a` e'
un solo oggetto**: quando il linker lo tira dentro per una `printf`, si
porta dietro tutto quello che c'e'. Il giorno che la libc verra' spezzata
in un archivio vero — un file per area, come `lib/include/unistd.h` gia'
prevede — questo problema sparisce da solo.

## 🐛 `ld` rifiutava di collegare: una funzione che non ritorna niente

Con `as` funzionante, la catena si ferma un passo dopo:

```
/cdrom/bin/as -o /prova.o /cdrom/prova.s      -> PROVA.O, 652 byte  ✔
/cdrom/bin/ld -o /prova /prova.o
    ld: input file '/prova.o' is the same as output file
```

`ld` confronta il file di uscita con quelli di ingresso passando da
`lrealpath()` di libiberty, che prova quattro strade — `realpath` con un
limite noto, `canonicalize_file_name`, `realpath` con `pathconf`, quella di
Windows — e se il sistema non ne offre **nessuna** ⚠️ **cade in fondo alla
funzione senza ritornare niente**. Non e' un errore che si vede: e' un
valore di ritorno che vale quel che resta in EAX, uguale per due chiamate
consecutive, quindi due file qualunque risultano lo stesso file.

Il rimedio e' dare al sistema la funzione che manca: `realpath()` — che
qui e' piu' semplice che altrove, perche' non ci sono collegamenti
simbolici da seguire (⚠️ il `..` si risolve sulla stringa, e senza
collegamenti le due cose coincidono sempre).

## ⚠️ E il nostro `<limits.h>` non lo includeva nessuno

`lrealpath` prende la prima strada solo se `PATH_MAX` esiste. Aggiunto
`lib/include/limits.h`, il programma di prova continuava a non vederlo, e
la ragione e' istruttiva: **GCC installa un proprio `<limits.h>`**, lo
trova per primo, e ne esistono due versioni. Quella che fa
`#include_next <limits.h>` — cioe' che prende anche il nostro — viene
generata **solo se, al momento di costruire GCC, un `limits.h` di sistema
c'era gia'**; altrimenti si installa la versione «sotto non c'e'
nessuno», che lo schermerebbe per sempre.

E GCC quel file lo cerca in `$prefisso/i386-exos/**sys-include**`, non in
`include/`. `prepara-cross.sh` ora crea il collegamento — e' la
disposizione standard di una cross-toolchain — e la coppia
`rm gcc/stmp-int-hdrs && make all-gcc install-gcc` rigenera l'header
giusto in un paio di minuti, senza ricostruire `cc1`.

## 🐛 Il primo binario di terzi che gira qui dentro, e come e' morto

`as` compilato, messo sul CD degli strumenti, lanciato:

```
[FAULT] PID 9 '/cdrom/bin/as': page fault a 0x00000000 (lettura, EIP=0x0804fbe3)
```

Tre istruzioni dentro `bfd_init`:

```
 804fbe3:  65 8b 1d 00 00 00 00    mov %gs:0x0,%ebx
```

**Variabili thread-local.** `bfd` dichiara `static TLS bfd_error_type
bfd_error`, e il compilatore emette l'accesso attraverso `%gs` — che su
EX-OS non punta a niente, perche' un thread pointer non c'e'.

⚠️ **La prova che il configure fa per il TLS e' una COMPILAZIONE, non
un'esecuzione.** `i386-exos-gcc` accetta `_Thread_local` senza fiatare: e'
il compilatore a saperlo fare, ed e' il sistema a non avere dove metterlo.
Nessun errore, nessun avviso, un binario che si costruisce benissimo e
muore alla terza istruzione della prima funzione che chiama.

Il rimedio non tocca i sorgenti: `export ac_cv_tls=` — la variabile di
cache gia' impostata perche' la prova non giri, con la stringa **vuota**.

⚠️ E' vuota e non `none`, e la differenza costa una ricostruzione: con
`none` il configure non definisce affatto la macro `TLS`, e binutils 2.44
**non ha un ripiego** — `bfd.c` scrive `static TLS bfd_error_type
bfd_error;` senza guardia e non compila piu' (`unknown type name 'TLS'`).
Con la stringa vuota la macro c'e' e non vale niente, quindi `static TLS
x` diventa `static x`.

⚠️ E va **esportata**, perche' deve valere anche durante il `make`: il
configure di primo livello non configura `bfd`, lo fa il make quando ci
arriva. Metterla solo davanti al primo comando non ha alcun effetto, e il
sintomo e' identico a non averla messa — la build riesce e il binario
muore in `bfd_init`. Si controlla con `grep TLS bfd/config.h`. `bfd` usa allora variabili statiche normali, che su un sistema dove
un processo ha un filo solo sono la stessa identica cosa.

Il giorno che serviranno i thread veri, la strada e' l'altra: `PT_TLS` nel
caricatore ELF, un blocco per processo e una voce di GDT per `%gs`
aggiornata a ogni cambio di contesto. E' una sessione di lavoro per conto
suo, ed e' annotata qui perche' e' la prima volta che il sistema incontra
il proprio limite invece di un difetto.

**→ Fatto nella sessione (h)**, il giorno stesso: dalla 0.154 il thread
pointer c'e' e `ac_cv_tls` non serve piu'.

## 🐛 Due difetti di FAT12 che si vedono solo TORNANDO INDIETRO

Con `as` funzionante e `ld` che finalmente leggeva il file, l'oggetto
prodotto dentro EX-OS restava sbagliato — in due modi diversi, uno dopo
l'altro, e sono **il pezzo di kernel di questa sessione**.

Nessuno dei due e' un difetto nuovo: sono li' da sempre e non si potevano
vedere, perche' fino a ieri ogni programma di EX-OS scriveva un file
dall'inizio alla fine. **Un qualunque scrittore di ELF no**: bfd scrive le
sezioni, poi si riposiziona a zero e ci mette l'intestazione.

**1. L'offset del descrittore non arrivava a fat12_write.**

```c
if (g_mnt[im].tipo == VFS_FS_FAT12FD)
    return fat12_write(g_file[h].h12, buf, size);   /* niente offset */
```

`vfs_write_nl` non lo passava — «fat12 tiene la propria posizione» — e
`fat12_write` scriveva sempre da `entry->file_size`, cioe' in coda,
qualunque cosa avesse fatto `lseek()`. Il risultato era un file con tutti
i pezzi giusti nell'ordine di scrittura e il magic `\x7fELF` a **offset
240**:

```
ld: /prova.o: file format not recognized
```

⚠️ **Su ext2 e FAT16/32 non succedeva**: li' l'offset il VFS lo passava
gia'. Era un difetto del solo supporto di avvio, cioe' di quello su cui si
prova tutto.

**2. Scrivere all'inizio di un settore lo azzerava per intero.**

```c
if (nuovo || in == 0) {
    for (i = 0; i < BYTES_PER_SECTOR; i++) sector_buf[i] = 0;
}
```

`in == 0` vuol dire «scrivo dall'inizio del settore», **non** «il settore
e' vuoto» — e le due cose coincidono solo se si scrive in coda. Con
l'offset finalmente onorato, i 52 byte dell'intestazione ELF scritti per
ultimi azzeravano i 460 byte di sezioni che stavano nello stesso settore:

```
ld: /prova.o: local symbol at index 4 (>= sh_info of 4)
```

cioe' un oggetto con le intestazioni giuste e il contenuto a zero. La
condizione giusta e' «questo settore sta oltre la fine attuale del file»,
non «comincio da capo».

⚠️ **Resta un caso non coperto, ed e' scritto nel codice**: un BUCO — una
scrittura che comincia oltre la fine del file — lascia i byte in mezzo
come stavano sul disco invece di farli leggere come zeri. Nessun programma
lo fa oggi.

## Metodo: `make -k` invece di un errore per volta

Ogni giro di «correggi, ricompila la libc, rifa' il sysroot, riparti» costa
dieci minuti. `make -k` non si ferma al primo errore e li raccoglie tutti:
la prima volta, su `libiberty`, ne ha dati nove in un colpo invece di uno.
Da riusare per il prossimo pacchetto.

## Verifica

- `/bin/libctest`: **171 su 171** (erano 114), con quattro sezioni nuove —
  descrittori duplicati, interfacce per il codice di terzi, `frexp`,
  confronti senza maiuscole.
- **La catena intera dentro EX-OS**: `as` assembla, `ld` collega, il
  binario gira. L'oggetto e' identico byte per byte a quello del cross.
- `make all` senza errori; nessuna regressione nei programmi esistenti.
- Regressione floppy: `mkdir`, `cp` (7007 byte), `delete`, `rmdir`.
  Avvio pulito: zero `[WARN]`, zero `[ERROR]`.
- Binutils nativi costruiti per intero: `as`, `ld`, `ar`, `nm`, `objdump`,
  `strip`, `gprof`. `as` e `ld` sono sul CD degli strumenti, strippati da
  7,3 a 1,4 MB.

## File toccati

- `kernel/fs/vfs.c`, `kernel/include/vfs.h` — `rif`, `vfs_dup`
- `kernel/syscall/syscall_impl.c` — `sys_dup`, `sys_dup2`, `sys_fcntl`
- `kernel/syscall/syscall.c`, `kernel/include/syscall.h` — i tre numeri, i `F_*`
- `kernel/sched/sched.c` — i descrittori chiusi alla raccolta
- `kernel/fs/fat12.c`, `kernel/include/fat12.h`, `kernel/fs/vfs.c` —
  l'offset in `fat12_write`, e il settore che non si azzera piu'
- `kernel/include/version.h` — 0.153
- `lib/libc.c`, `lib/include/libc.h` — tutta la tabella qui sopra
- **nuovi**: `lib/include/{strings.h,wchar.h,memory.h,utime.h}`,
  `lib/include/sys/{types.h,param.h}`
- `lib/include/{fcntl.h,math.h,stdint.h}`, `lib/include/sys/stat.h` — i commenti
- `bin/libctest/libctest.c` — 114 → 171 prove
- `bin/mkdir/mkdir.c`, `bin/install/install.c` — `mkdir` a due argomenti
- **nuovo**: `tools/binutils-exos/pex-exos.c`
- `tools/binutils-exos/applica.py` — i file nostri, tre agganci in libiberty
- `tools/gcc-exos/prepara-cross.sh` — `-fno-asynchronous-unwind-tables`,
  il collegamento `sys-include`
- `Makefile` — `as` e `ld` nativi sul CD degli strumenti (`BINUTILS_NATIVI`)
- **nuovi**: `tools/iso/prova.s`, `lib/include/limits.h`
- `README.md`, `tools/binutils-exos/leggimi.md`

---

# SESSIONE 2026-08-02 (f) — Binutils veri, e i tre difetti del bersaglio

Kernel **invariato a 0.150**: sotto `kernel/` non e' stato toccato niente.
Questa sessione lavora sulla catena di compilazione — il passo 1 della
lista lasciata dalla sessione (e).

## Non sono piu' wrapper

`i386-exos-as` e `i386-exos-ld` erano otto script di tre righe attorno agli
strumenti di sistema forzati a 32 bit. Ora sono **binutils 2.44 compilati
per il bersaglio**: `as`, `ld`, `ar`, `ranlib`, `nm`, `objcopy`, `objdump`,
`strip`, `readelf`, `addr2line`.

Il port vive in `tools/binutils-exos/`, con la stessa convenzione di
`gcc-exos/`: i sorgenti di terzi non entrano nel repository, la parte
nostra si'.

```
applica.py             quattro righe in quattro file
prepara-binutils.sh    configure + make + install + verifica
leggimi.md             il perche', e il passo successivo
```

Le quattro modifiche dicono due cose sole: **`exos` e' un sistema
operativo** (`config.sub`) e **per lui il formato e' ELF32 i386**
(`bfd/config.bfd`, `gas/configure.tgt`, `ld/configure.tgt`). Non serve
altro, ed e' interessante perche' spiega dove sta davvero la differenza fra
`elf` ed `exos`: nel **compilatore** — indirizzo di caricamento, `crt0.o`,
`-lc` aggiunto da solo — non negli strumenti che manipolano gli oggetti.

**Che il driver li usi davvero** si vede con `gcc -v`:

```
.../i386-exos/bin/as -o /tmp/ccKeg9q7.o /tmp/cc0kRJOo.s
```

e `ld` ha finalmente `elf_i386` come emulazione predefinita, invece di
dipendere dal `-m elf_i386` che gli passa GCC.

## 🐛 Due difetti in `applica.py`, trovati dalla sua stessa prova

Due delle quattro modifiche sono **inserimenti**: il testo di partenza
resta dentro quello di arrivo. Il controllo di idempotenza — «il testo
nuovo c'e' gia'?» — non bastava, perche' trovava anche quello vecchio:

- riapplicare **duplicava** la riga (`| exos*` due volte in `config.sub`);
- la rimozione successiva ne toglieva **una sola**, lasciando l'albero
  modificato mentre lo script diceva di averlo pulito.

Ora ogni modifica porta un **marcatore**: una stringa che esiste solo nello
stato applicato. C'e' → non fare niente; non c'e' → applica. Vale in
entrambi i versi e non dipende da come e' fatta la sostituzione.

Provato: applica → riapplica (`0 modificati, 4 gia' a posto`) → togli
(`config.sub` rifiuta di nuovo `exos`) → riapplica.

⚠️ Aggiunta una protezione a `prepara-cross.sh`, che si rilancia quasi
sempre per aggiornare gli header: **non reinstalla i wrapper sopra i
binari** se li trova gia'. Senza, un aggiornamento di routine avrebbe fatto
tornare `ld` a non conoscere la propria emulazione, senza che nessuno
avesse toccato niente.

## I tre difetti del bersaglio, corretti insieme

Erano annotati come noti in `tools/gcc-exos/leggimi.md` e costavano tutti e
tre un rebuild di GCC — le specs e i tipi sono compilati dentro il driver e
dentro `cc1` — quindi tanto valeva farli in un colpo solo.

- **Indirizzo di caricamento**: `LINK_SPEC` passa ora
  `-Ttext-segment=0x08000000`. Era `0x08048000`, il default storico di
  `ld`, mentre `exos.h` dichiarava a parole `0x08000000`. Il caricatore del
  kernel accetta entrambi, quindi non si vedeva: si vedeva solo che due
  binari dello stesso programma — uno dal cross, uno dal Makefile — stavano
  a indirizzi diversi, il che rende inconfrontabili due disassemblati.
- **`.eh_frame`**, 5,6 KB per binario. ⚠️ `DWARF2_UNWIND_INFO 0` toglie
  l'unwind delle **eccezioni**, non le tabelle **asincrone**: sono due cose
  diverse che si chiamano quasi uguale, ed e' il motivo per cui il difetto
  era sopravvissuto a una correzione che sembrava averlo risolto. Ora
  `CC1_SPEC` passa `-fno-asynchronous-unwind-tables`.
- **I tipi fondamentali**: il bersaglio prendeva i predefiniti, cioe'
  `long unsigned int` per `size_t` e `long int` per `int32_t`. Larghezze
  giuste, **tipi diversi** da quelli del `gcc` di sistema con `-m32`. Ora
  `exos.h` dichiara `SIZE_TYPE`, `PTRDIFF_TYPE`, `INT32_TYPE` e
  `UINT32_TYPE` come `i386-linux`, che e' il bersaglio con cui EX-OS
  condivide l'ABI.

`tools/gcc-exos/prova.c` verifica ora i tipi **in compilazione**, con
`_Static_assert` e `_Generic`: non la larghezza — quella era gia' giusta —
ma il tipo. Se qualcuno cambia quelle righe di `exos.h`, il programma
smette di compilare, che e' il momento in cui accorgersene costa meno.

## File toccati

- **nuovi**: `tools/binutils-exos/{applica.py,prepara-binutils.sh,leggimi.md}`
- `tools/gcc-exos/exos.h` — `-Ttext-segment`, `-fno-asynchronous-unwind-tables`,
  i quattro tipi
- `tools/gcc-exos/prepara-cross.sh` — non sovrascrive i binutils veri
- `tools/gcc-exos/leggimi.md` — i wrapper non sono piu' la regola, i tre
  difetti non sono piu' aperti
- `tools/gcc-exos/prova.c` — gli `_Static_assert` sui tipi

---

# SESSIONE 2026-08-02 (e) — Quello che serve a un driver di compilatore

Kernel a **0.149** → **0.150**. `/bin/libctest` passa **114 prove su 114**,
sia con root su floppy sia con root su ext2.

L'obiettivo dichiarato e' GCC ospitato. Questa sessione fa i due pezzi che
stanno dentro EX-OS e che vengono prima di tutto il resto: senza, avere
`cc1` sul disco non servirebbe a niente.

## Il pezzo che conta: lanciare un figlio e prenderne l'uscita

`xgcc` non compila niente. Lancia `cc1`, poi `as`, poi `ld`, e ne raccoglie
l'esito. Mancavano tre cose:

- **Redirezione dei descrittori.** `sys_spawn` ora accetta fino a quattro
  azioni «il descrittore N del figlio e' questo file». Per **percorso**,
  non per fd gia' aperto del padre: passare un fd vorrebbe dire due
  processi sullo stesso handle VFS, cioe' un conteggio di riferimenti che
  non c'e' e una `close()` che sfila il file da sotto all'altro. Basta a
  `gcc`; non basta alle pipe, che infatti non ci sono ancora.
- **Ambiente per processo.** `envp` viaggia come `argv` — copiato sullo
  stack del figlio — e in libc ci sono `environ`, `putenv`, `setenv`,
  `unsetenv`. GCC si configura cosi': `TMPDIR`, `GCC_EXEC_PREFIX`,
  `COMPILER_PATH`, `LIBRARY_PATH`.
- **Tetti piu' alti.** `MAX_FD` da 16 a 32 (un compilatore tiene aperti
  sorgente, uscita, la catena degli header e un paio di temporanei),
  `VFS_MAX_OPEN` da 48 a 64.

La prova che vale piu' di tutte, dentro `libctest`: lancia `/bin/hello`
con **stdout rediretto su un file**, aspetta con `waitpid`, riapre il file
e ci trova quello che hello stampa. Sono spawn, redirezione e attesa
verificati insieme, che e' esattamente cio' che fara' il driver di GCC.

## ⚠️ L'estensione passa da una parola magica, non da due argomenti in piu'

La forma storica e' `spawn(percorso, argc, argv)`, tre registri. In giro
ci sono binari — anche gia' installati su un disco — che la chiamano cosi',
e per loro ESI contiene spazzatura: leggerlo come puntatore vorrebbe dire
che un programma vecchio, il giorno che gira su un kernel nuovo, apre file
a caso o non parte.

Percio' ESI punta a una struttura che comincia con `SPAWN_EXTRA_MAGIA`. Se
non e' leggibile o la magia non combacia, il kernel fa finta che non ci
sia. La probabilita' che spazzatura casuale sia insieme un puntatore
valido e la magia giusta e' quella di indovinare 32 bit.

## Il resto della libc che GCC e binutils chiamano davvero

`opendir`/`readdir`/`closedir`/`rewinddir` (sopra `listdir`, paginata),
`mkstemp`/`tmpnam`/`tmpfile`, `access`, `isatty` (sopra `ioctl`, che gia'
rispondeva ENOTTY a tutto cio' che non e' la console: era gia' la domanda
giusta), `atexit` con `exit()` che ora chiama gli handler all'indietro,
`signal`/`raise`/`strsignal`, `setlocale` (solo la locale "C", detto),
`sysconf`, `times`/`clock`, e **i nomi degli errori**: `ENOENT`, `EINVAL`,
`ENOTDIR`… c'erano i numeri dal principio, ma senza i nomi non compila una
riga di codice scritto per POSIX.

Header nuovi: `<dirent.h>`, `<signal.h>`, `<locale.h>`, `<sys/wait.h>`,
`<sys/times.h>`.

⚠️ **`rename()` copia e cancella**, e sta scritto nel codice: il VFS non ha
una rinomina, che nei driver significa aggiungere una voce di directory e
toglierne un'altra senza lasciare il file irraggiungibile in mezzo. Per i
temporanei di un compilatore va bene; per un file grosso no. Una
`SYS_RENAME` vera resta da fare.

## ⚠️ Due macro con il nome sbagliato, trovate dal test al primo giro

In `lib/libc.c` c'erano

```c
#define S_ISDIR(attr)   (((attr) & 0x10) != 0)
```

cioe' le macro standard definite sull'**attributo FAT**, mentre lo stesso
file, piu' sotto, le voleva sul `st_mode` POSIX. Nessuno le usava, finche'
`opendir()` non ha scritto la riga piu' naturale del mondo,
`S_ISDIR(st.st_mode)`, e si e' presa la prima: `0040755 & 0x10` fa zero,
quindi `opendir("/")` rispondeva «non e' una directory». Tolte: per
l'attributo FAT c'e' gia' `EXOS_ATTR_DIR()`, che si chiama come cio' che fa.

Nota di metodo: la prova `isatty su un file` apriva `/KERNEL.BIN`, che
esiste sul floppy e **non** su un sistema installato su ext2 — falliva li'
per il motivo sbagliato. Ora le prove si creano da sole i file che usano.

## Verifica

- `/bin/libctest`: **114 su 114**, su floppy **e** su root ext2 (il secondo
  caso ha trovato la prova che dipendeva dal supporto).
- Regressione floppy: `ls`, `mkdir`, `cp`, `delete`, `rmdir`.
- Regressione ext2: `mount`, `mkdir`, `cp`, `delete`, `rmdir`, `install`,
  **`e2fsck -fn` esito 0**, avvio da ext2 senza floppy con 0.150.
- CD: `ls /cdrom`. Memoria: `bigmem` 300 MB su macchina da 512.
- Zero `[WARN]`/`[ERROR]` all'avvio, nessun panic. Floppy: 854 KB liberi.

## Cosa resta per GCC, in ordine

1. **binutils veri per `i386-exos`**: oggi `i386-exos-as` e `-ld` sono
   wrapper di tre righe sugli strumenti di sistema. Chiedono poco oltre la
   libc: e' il pezzo piu' abbordabile della catena esterna.
2. **GMP, MPFR, MPC per il bersaglio**: `cc1` ci si linka
   (`GMPLIBS = -lmpc -lmpfr -lgmp`). Tre `configure` da portare.
3. **Il sottoinsieme di libstdc++**: `cc1` e' C++, ma GCC compila i propri
   sorgenti con `-fno-exceptions -fno-rtti` (verificato nel Makefile della
   build), quindi niente unwinder: restano `operator new`/`delete`,
   `__cxa_atexit` e i contenitori che usa davvero.
4. **Costruire e installare `cc1`** (~35-45 MB per i386) su ext2, e
   sistemare insieme i tre difetti gia' annotati in `exos.h` — indirizzo di
   caricamento, `-fno-asynchronous-unwind-tables`, tipi — perche' costano
   un rebuild di GCC.
5. **`pex-exos.c`** al posto di `pex-unix.c` in libiberty: spawn + waitpid
   + file temporanei invece di fork/pipe. Ora c'e' tutto quello che gli
   serve.
6. Da fare quando dara' fastidio: `SYS_RENAME` vera, le pipe, e il **DMA**
   (vedi la sessione (d): il disco va a 0,75 MB/s).

## File toccati

- `kernel/include/syscall.h` — `SpawnExtra`/`SpawnAzione` e la magia
- `kernel/syscall/syscall_impl.c` — `sys_spawn` con ambiente e redirezioni
- `kernel/include/sched.h` — `MAX_FD` 32
- `kernel/include/vfs.h`, `kernel/fs/fat12.c` — handle a 64
- `lib/start.S` — envp come terzo argomento
- `lib/libc.c` — ambiente, spawn/waitpid, dirent, temporanei, access,
  isatty, rename, atexit, segnali, locale, sysconf, times; tolte le due
  macro ambigue
- `lib/include/libc.h` — dichiarazioni e **i nomi degli errno**
- **nuovi**: `lib/include/{dirent,signal,locale}.h`,
  `lib/include/sys/{wait,times}.h`
- `bin/libctest/libctest.c` — quattro sezioni nuove (41 → 114 prove)
- `kernel/include/version.h` — 0.150
- `README.md`, `KERNEL_CORE_NOTES.md`

---

# SESSIONE 2026-08-02 (d) — Le pagine arrivano quando servono

Kernel a **0.148** → **0.149**. I segmenti di un eseguibile non si copiano
piu' in RAM al momento dello spawn: si annota dove vivono nel file e le
pagine arrivano al primo accesso. Un binario con **8 MB di `.rodata`**
parte occupando **36 KB**.

```
bigbin: binario da 8 MB di .rodata
  memoria usata all'avvio        : 2756 KB
  dopo aver letto 3 byte su 8 MB : 2792 KB  (+36 KB)
  dopo aver letto tutti gli 8 MB : 10972 KB  (+8216 KB)
  esito                          : tutti gli 8 MB corretti
```

E' il pezzo che mancava per far partire un compilatore: `cc1` strippato,
per i386, sara' 35-45 MB, e con il caricamento residente andavano
impegnati tutti prima della prima istruzione.

## `install` non aggiornava niente, e diceva "completata"

Segnalato durante il beta testing, ed era il difetto peggiore che un
installatore possa avere. `copia()` saltava i file gia' presenti: su un
sistema gia' installato copiava solo i file NUOVI e lasciava indietro
tutti gli altri, **kernel compreso**. Poi riscriveva la mappa dei settori
per quel kernel vecchio — cioe' produceva il risultato piu' convincente
possibile di un aggiornamento che non era avvenuto.

Ora ogni file viene riscritto e **riletto** per confrontarne la
dimensione: senza quel controllo un kernel troncato si scoprirebbe al
riavvio, che e' il momento in cui non si puo' piu' fare niente. Nel
resoconto `+` e' creato, `~` sostituito, `!` errore. Le directory no:
esistono o non esistono. Quel che c'e' sul volume e non fa parte del
sistema resta dov'e' — `install` aggiorna, non azzera.

## Come funziona il caricamento su richiesta

`elf_carica()` ha due modi. **Su richiesta** annota i PT_LOAD in
`Process.vma` (fino a quattro; oltre, carica tutto in RAM invece di
mappare a meta') e tiene l'eseguibile aperto in `Process.exe_handle` per
tutta la vita del processo. `pf_carica_da_file()` nel gestore di page
fault legge la pagina che manca e la mappa. Oltre `file_fine` c'e' il BSS:
niente da leggere, pagina azzerata e via.

⚠️ **I driver si caricano RESIDENTI** (`elf_load_residente`). Un driver che
serve il filesystem, paginato *da* quel filesystem, dovrebbe servire la
propria lettura mentre e' fermo ad aspettarla.

Il gestore di #PF riaccende gli interrupt per la sola `vfs_read` — il gate
e' un interrupt gate e leggere dal disco richiede il timer — e il buffer
di lettura sta sullo stack kernel: due processi possono essere li' dentro
insieme.

## Le tre cose che sono venute fuori facendolo funzionare

Nessuna delle tre si vedeva prima, e nessuna assomigliava alla propria
causa. Sono il vero contenuto di questa sessione.

**1. I driver di filesystem non sono rientranti.** ext2 lavora su cinque
buffer globali; la separazione per genere di blocco basta a impedire a una
funzione di pestare i piedi a un'altra, ma presuppone **un'operazione alla
volta**. Con il fault-in gli intrecci sono diventati la norma: al PASSO 15
il kernel carica la shell della console 1 mentre quella della 0 gia' gira
e fa fault. Il sintomo:

```
EXT2: blocco 0 fuori dal volume (523264)
PF: PID 4, lettura della pagina 0x08000000 fallita (-1)
ATA: timeout DRQ sul canale 0 (stato=0x50)
```

cioe' un numero di blocco calcolato con l'inode di qualcun altro e un
controller lasciato a meta' comando. Rimedio: un lucchetto nel VFS.

**2. Chi tiene il lucchetto non deve poter faultare.** Una `read()` il cui
buffer cade in una pagina non ancora presente fa scrivere il driver dentro
quella pagina con il lucchetto in mano: il page fault chiama il VFS e
aspetta se stesso. `vm_precarica_utente()` porta in RAM le pagine del
buffer PRIMA di entrare nel VFS. Stessa trappola in versione interna: tre
punti del VFS chiamavano il **guscio pubblico** `vfs_stat` invece
dell'implementazione — lo stesso processo prendeva il lucchetto due volte
(`DIAG fs: PID 8 aspetta, tiene 8`).

**3. ⚠️ Inversione di priorita', e il sistema si fermava senza dire
niente.** La prima attesa cedeva la CPU in un ciclo. Ma `kernel_main` gira
nel contesto del task **IDLE**: al PASSO 15 e' il processo meno prioritario
a tenere il lucchetto mentre carica le shell. Appena una shell diventa
eseguibile e comincia a cedere-e-riprovare a priorita' NORMAL, l'idle non
viene piu' scelto: non finisce la lettura, non rilascia, e le shell
riprovano per sempre. Schermo fermo al banner, **zero messaggi**, e
intermittente. La diagnosi e' arrivata dal monitor di QEMU: `CR2` fisso a
`0x08000000`, `CR3` che cambiava a ogni campione, EIP dentro
`context_switch` — tutti a faultare sulla stessa pagina senza avanzare.

Ora chi aspetta si **blocca** ed esce dalla coda dei pronti; l'idle torna
a essere l'unico eseguibile, finisce, e sveglia tutti al rilascio.

## E il difetto del Makefile che nascondeva tutto

Aggiungendo campi a `struct Process` le quattro shell hanno cominciato a
chiedere la riga tutte sulla console 0, con `kbd: la richiesta di PID 5
sostituisce quella di PID 4` e i comandi digitati che sparivano senza
errori. Sembrava un difetto delle console virtuali: era `tty.o`, che ha
una regola propria nel Makefile e **non aveva le dipendenze dagli header**
(non fa parte di `KERNEL_C_OBJ`, quindi la `-include` dei `.d` non lo
copriva). Continuava a leggere `Process.console` all'offset vecchio.

⚠️ **Questo spiega anche l'anomalia annotata come "osservata una volta e
non riprodotta" nella sessione (b)**: era lo stesso oggetto stantio, dopo
che la sessione della FPU aveva aggiunto `fpu_state` al PCB. Non era
intermittente: dipendeva da quando `tty.o` era stato ricompilato l'ultima
volta.

## Verifica

- **8 MB di `.rodata`, tre byte letti → +36 KB**; letti tutti → +8216 KB,
  ogni byte corretto (il contenuto e' `(i*7+3) % 251`, verificabile senza
  copie). Provato sia con root su floppy sia con root su ext2.
- `/bin/libctest`: **78 su 78**, zero `[WARN]`/`[ERROR]`.
- **Avvio da ext2 senza floppy: tre volte di fila, zero problemi** — e'
  il caso che falliva prima del lucchetto e si bloccava prima del blocco.
- `install` su un sistema gia' installato: tutti i file `~ sostituito`,
  kernel compreso, `e2fsck -fn` **esito 0**, e il disco riavvia con il
  kernel nuovo (0.149 dopo che il precedente era 0.148).
- Regressione floppy (`ls`, `mkdir`, `cp`, `delete`, `rmdir`, `hello`,
  12 spawn di fila per gli handle), ext2 (`mount`/`mkdir`/`cp`/`delete`/
  `rmdir`/`umount`), CD (`ls /cdrom`), memoria (`bigmem`: 300 MB su una
  macchina da 512).

## Cosa resta

- **Il file dei binari resta aperto per tutta la vita del processo.** E'
  un handle VFS per processo: da qui l'aumento di `VFS_MAX_OPEN` a 48. Con
  molti processi contemporanei il tetto va rialzato ancora, o l'handle va
  condiviso fra i processi che eseguono lo stesso file.
- **Nessuna pagina viene mai buttata via.** Una pagina caricata resta fino
  alla morte del processo: manca il rovescio del demand paging, cioe' lo
  sfratto (e con esso il pagefile). Serve quando la RAM finisce davvero,
  non prima.
- **Il lucchetto e' unico per tutto il VFS.** Due dischi diversi si
  aspettano a vicenda anche se non hanno niente in comune. Un lucchetto
  per montaggio si aggiunge quando dara' fastidio.

### 📌 DA FARE: DMA per l'ATA (misurato, non stimato)

Il disco va a **0,75 MB/s**: 8 MB di `/bin/bigbin` letti da ext2 in
**10,6 secondi**, cioe' ~1,3 ms per ogni richiesta da 1 KB. `ata.c` e' PIO
puro — `port_inw` 256 volte per settore piu' un'attesa DRQ a polling
attivo — e in QEMU ogni accesso a porta e' un VM exit: circa mille uscite
per KB. Con il caricamento su richiesta quel costo si paga a ogni pagina.

Il **floppy il DMA ce l'ha gia'** (canale ISA 2, `fat12.c`): li' non c'e'
niente da guadagnare. Il lavoro e' il **bus-master DMA PCI** per ATA/ATAPI.

⚠️ Il terreno e' piu' favorevole di quanto sembri: **nessun chiamante di
`ata_read`/`blk_read` passa memoria utente**. Sono tutti buffer statici del
kernel (i cinque di ext2, quelli di FAT, `mbr.c`, `vol.c`) e perfino
`sys_blkread` rimbalza gia' su un buffer kernel da 512 byte. Stanno tutti
nella fascia identity-mapped, quindi l'indirizzo fisico di una voce PRD e'
`(uint32_t)buf` senza traduzioni. L'unico caso da gestire e' un buffer che
cade a cavallo di un confine di 64 KB: due voci PRD invece di una.

Manca invece **tutta l'enumerazione PCI** (nel kernel non c'e' una riga:
niente 0xCF8/0xCFC), che serve per trovare BAR4 del controller IDE.

A strati, ognuno fermabile:

1. PCI in sola lettura: trova classe 0x01 sottoclasse 0x01, legge BAR4 e
   il bit Bus Master. Nessun cambio di comportamento, solo una riga di log.
2. Lettura in DMA con completamento a polling sul bit 2 dello stato
   bus-master. **Ricaduta automatica su PIO** se manca il controller, se
   IDENTIFY (parole 49/63/88) non dichiara DMA, se il buffer non e'
   mappabile, o al primo errore — con una riga di log una volta sola.
3. Scrittura in DMA.
4. Completamento a **interrupt** (IRQ 14/15) invece del polling: chi ha
   faultato si blocca e la CPU va a qualcun altro. Con il demand paging e'
   il pezzo che conta quanto la velocita' pura.
5. ATAPI in DMA (opzionale).

Piu' un interruttore in `kernel.cfg` (`[kernel] atadma = 0`) per spegnerlo
su una macchina dove desse problemi, senza ricompilare.

⚠️ Il DMA **non toglie il costo per richiesta**, e su ext2 con blocchi da
1 KB le richieste sono tante e piccole. Per avvicinarsi al massimo serve
anche leggere di piu' per volta (una pagina intera in un comando, o una
cache di blocchi): e' un lavoro distinto, che moltiplica il primo.
- Poi il resto della strada per il compilatore ospitato, che il kernel non
  puo' dare: spawn con redirezione dei descrittori, environment per
  processo, `opendir`/`rename`/`mkstemp`/`isatty`, binutils per il
  bersaglio, GMP/MPFR/MPC, il sottoinsieme di libstdc++.

## File toccati

- `kernel/loader/elf.c` + `elf.h` — `elf_carica` a due modi,
  `elf_load_residente`, handle tenuto aperto
- `kernel/include/sched.h` — `ProcVma`, `vma[]`, `n_vma`, `exe_handle`
- `kernel/mm/paging.c` + `paging.h` — `pf_carica_da_file`,
  `vm_precarica_utente`
- `kernel/fs/vfs.c` — lucchetto con attesa bloccante, gusci pubblici,
  tre rientri tolti
- `kernel/sched/sched.c` — `exe_handle` inizializzato e chiuso in
  `proc_reap_zombie`
- `kernel/kernel_main.c` — driver residenti
- `kernel/include/vfs.h`, `kernel/fs/fat12.c` — tetti degli handle a 48
- `kernel/syscall/syscall_impl.c` — precarica dei buffer in read/write
- `bin/install/install.c` — sostituisce e verifica
- `Makefile` — dipendenze dagli header per `tty.o`
- `kernel/include/version.h` — 0.149
- `README.md`, `KERNEL_CORE_NOTES.md`

---

# SESSIONE 2026-08-02 (c) — Il tetto dei 4 MB per processo

Kernel a **0.147** → **0.148**. Un processo passa da **4 MB** a **300 MB**
di memoria, provati e riletti byte per byte.

## Il numero che non dipendeva dalla RAM

La domanda era «cosa manca per ospitare GCC su EX-OS», e la prima risposta
non riguardava GCC. Un programma che cresce lo heap 1 MB per volta:

```
bigmem:  4 MB ok  (sbrk=0x08457000)

╔══════════════════════════════════════════════╗
║            PAGE FAULT (KERNEL)               ║
║  Indirizzo : 0x00800000   Accesso: scrittura ║
╚══════════════════════════════════════════════╝
```

`0x00800000` era `USER_SPACE_BASE`. Con 32 MB installati o con 4 GB il
programma moriva alla stessa cifra, perche' il limite non era la memoria
disponibile: era **quanta memoria fisica il kernel riesce a toccare mentre
gira un processo**. `cc1` ne chiede dieci volte tanto.

## Le due righe che si ignoravano a vicenda

`paging_create_directory()` copia nella page directory di un processo solo
le PDE **sotto `USER_SPACE_BASE`**; il mapping identita' di tutta la RAM
sta nelle PDE successive della sola `kernel_page_directory`. Intanto
`sys_sbrk`, `sys_mmap`, `elf_load` e la crescita dello stack riempivano le
pagine appena allocate **al loro indirizzo fisico**, con il CR3
dell'utente caricato. Finche' il PMM consegnava pagine sotto la soglia
funzionava tutto; la prima oltre era un fault in ring0.

Il commento in `paging.c` diceva gia' «mappare qui tutta la RAM elimina
l'intera classe di bug»: era vero per la PD del kernel, e la copia verso
le PD dei processi si fermava a due PDE.

## Il rimedio, e i tre modi in cui poteva restare rotto

**1. La finestra di rimappatura fisica.** Una PTE dentro
`kernel_page_table_low` — la tabella statica in PDE[0], presente in *ogni*
spazio di indirizzamento — che il kernel ripunta alla pagina fisica che
deve toccare, e richiude. `paging_finestra_apri/chiudi`,
`paging_azzera_fisica`. Due regole obbligatorie, scritte nel codice:
interrupt spenti mentre e' aperta, e **mai aperta attraverso una chiamata
che si blocca** — in `elf_load` fra una pagina e l'altra c'e' una
`vfs_read`, cioe' IPC verso un driver in ring3. Aprirla due volte e'
`kpanic`: l'alternativa sarebbe scrivere nel posto sbagliato in silenzio.

**2. La fascia kernel, dichiarata invece che sperata.**
`pmm_alloc_page_kernel()` per tutto cio' che il kernel raggiunge
fisicamente: heap di kmalloc, stack kernel, page directory e page table,
immagini dei driver. `USER_SPACE_BASE` da 8 a 64 MB, perche' 64 processi ×
128 KB di stack kernel sono 8 MB tondi — la vecchia soglia intera occupata
da un solo genere di allocazione.

**3. ⚠️ E poi la fascia se la mangiavano le pagine utente.** Al primo giro
con 512 MB il programma e' arrivato a 72 MB e si e' fermato con:

```
PMM: fascia kernel esaurita (0x0-0x04000000): 111913 pagine libere altrove
```

`pmm_alloc_page()` partiva da 1 MB e consumava la fascia dal basso: il
programma cresceva e il kernel restava senza una pagina per una page
table, con 437 MB liberi. Ora le pagine utente prendono la fascia **per
ultima**, e su una macchina piccola — dove la RAM ci sta tutta dentro — il
ripiego e' immediato, che e' il comportamento giusto: li' non c'e' niente
da preservare.

⚠️ L'eccezione dichiarata resta il caricatore dei moduli (`dynlink.c`,
`drvmgr.c`): rilocando `R_386_COPY` legge da una pagina e scrive in
un'altra nello stesso istante, e la finestra e' una sola. Le immagini dei
driver stanno nella fascia kernel — sono due, di ~15 KB.

## Verifica

- **300 MB** allocati e riletti byte per byte su una macchina da 512 MB
  (prima: 4 MB), **due volte di fila**, con `mem` che torna a
  `3132 KB usati` esatti dopo ogni uscita: alloca e libera entrambi
  corretti attraverso il confine della fascia.
- Su 32 MB il programma arriva a **29 MB** e fallisce con un ENOMEM
  pulito, prompt vivo, invece che con un panic a 4.
- `/bin/libctest`: **78 su 78**, zero `[WARN]`/`[ERROR]` all'avvio.
- Floppy: `mem`, `ls`, `mkdir`, `cp`, `ls`, `delete`, `rmdir`, `hello`.
- ext2 su `hd0p1`: `mount`, `mkdir`, `cp` (58 KB), `ls`, `delete`,
  `rmdir`, `umount` — **`e2fsck -fn` esito 0**.
- CD: automount `/cdrom` e `ls /cdrom`.
- **Avvio da ext2 senza floppy con il kernel nuovo**: `install /disk`
  rifatto (321 settori in 3 intervalli), `ver` mostra 0.148, root su
  `hd0p1`, zero problemi nel log, e2fsck 0 dopo l'installazione.

Il programma di prova non e' entrato in `/bin` (non e' un target di
`make`, e il floppy e' stretto). E' venti righe, si ricompila col cross:

```c
/* i386-exos-gcc bigmem.c -o bigmem ; mcopy -i floppy.img -o bigmem ::/bin/bigmem */
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    int mb;
    printf("heap iniziale sbrk(0)=%p\n", sbrk(0));
    for (mb = 1; mb <= 300; mb++) {
        char *p = (char *)malloc(1024 * 1024); int i;
        if (!p) { printf("malloc fallita a %d MB\n", mb); return 1; }
        for (i = 0; i < 1024*1024; i += 4096) p[i] = (char)mb;
        for (i = 0; i < 1024*1024; i += 4096)
            if (p[i] != (char)mb) { printf("CORRUZIONE a %d MB\n", mb); return 2; }
        if (mb % 20 == 0) printf("%3d MB ok (sbrk=%p)\n", mb, sbrk(0));
    }
    printf("%d MB allocati e verificati\n", mb - 1);
    return 0;
}
```

## Cosa resta, in ordine

- **Demand paging da file.** Il caricatore ELF e' *eager*: un binario da
  40 MB impegna 40 MB prima di eseguire un'istruzione. `cc1` strippato,
  per i386, sara' 35-45 MB. La finestra serve gia' allo scopo — la pagina
  da riempire e' quella di un altro spazio di indirizzamento.
- **Higher-half.** La fascia da 64 MB e' un compromesso: e' memoria che
  l'utente non puo' usare, e va rialzata a ogni crescita del sistema. La
  soluzione definitiva e' il kernel a `0xC0000000` con la RAM mappata
  sopra e le PDE alte **condivise per puntatore** invece che copiate —
  `USER_SPACE_END` gia' vale `0xC0000000`, il layout lo prevede. Il costo
  e' l'avvio: PD provvisoria con doppio mapping e salto lungo dopo
  `mov cr0`.
- Poi il resto della strada per un compilatore ospitato, che il kernel non
  puo' dare: spawn con redirezione dei descrittori, environment per
  processo, `opendir`/`rename`/`mkstemp`/`isatty`, binutils per il
  bersaglio, GMP/MPFR/MPC, il sottoinsieme di libstdc++.

## File toccati

- `kernel/include/kernel.h` — `USER_SPACE_BASE` 8 → 64 MB, con il perche'
- `kernel/mm/paging.c` + `paging.h` — la finestra; PD e page table dalla
  fascia kernel; crescita stack via finestra
- `kernel/mm/pmm.c` + `pmm.h` — `pmm_alloc_page[s]_kernel`, preferenza
  fuori fascia in `pmm_alloc_page`, riserva della pagina della finestra
- `kernel/mm/kmalloc.c` — heap kernel dalla fascia
- `kernel/sched/sched.c` — stack kernel dalla fascia, in un colpo solo
- `kernel/loader/elf.c` — azzeramento e copia dei segmenti via finestra
- `kernel/syscall/syscall_impl.c` — `sbrk`, `mmap`, copia di argv nel
  figlio via finestra
- `kernel/loader/dynlink.c`, `drvmgr.c` — immagini dei driver in fascia
- `kernel/include/version.h` — 0.148
- `README.md`, `KERNEL_CORE_NOTES.md`

---

# SESSIONE 2026-08-02 (b) — La virgola mobile, e due bug che solo lei ha fatto uscire

Kernel a **0.146** → **0.147**. `/bin/libctest` passa **78 prove su 78**
dentro EX-OS (erano 41).

La sessione doveva aggiungere alla libc cio' che manca a un compilatore
ospitato — virgola mobile, `sscanf`, data e ora, `stat` — e ha finito per
scoprire che **due dei tre "difetti della libc" non erano nella libc**.

## Perche' la FPU e' un problema del kernel

`strtod` non e' un lusso: un compilatore deve leggere i letterali numerici
dei sorgenti che compila, e una `strtod` che ritorna zero non da' un
errore — da' un programma compilato con la costante sbagliata.

Ma il primo programma che tocca l'x87 su un kernel che non l'ha
inizializzata prende un'eccezione, e — molto peggio, perche' silenzioso —
**due processi che fanno conti si sovrascrivono i registri a vicenda**.
Da qui `kernel/arch/x86/fpu.c`, il **PASSO 7b**, e 108 byte di stato nel
PCB salvati e ripristinati a ogni cambio di contesto. Le scelte (dov'e' il
passo, perche' salvataggio sempre e non switch pigro, perche' zero non e'
uno stato valido per `FRSTOR`) stanno per esteso in
`kernel/include/fpu.h` e in `KERNEL_CORE_NOTES.md`.

La prova che conta e' in `libctest`: un prodotto ripetuto venti volte con
un `sched_yield()` in mezzo a ogni moltiplicazione. Se nessuno salvasse i
registri, al ritorno il valore sarebbe di qualcun altro. Da' 1048576.

## 🐛 Il file temporaneo che conteneva un ELF

`fseek(f, 0, SEEK_END); ftell(f)` su un file da 29 byte rispondeva
**31640**, e il primo `fgetc()` dopo un `fseek` all'inizio ritornava
**0x7F**. Cioe' la dimensione, e la prima lettera, di `/bin/libctest`.

Non era `FILE*`. **`vfs_open()` prenotava lo slot di `g_file[]` troppo
tardi**: sceglieva quello libero, poi parlava con il driver del
filesystem, e solo alla fine lo marcava `usato`. In mezzo c'e' una lettura
di directory, che e' un messaggio IPC a un driver in ring3, che e' un
punto di riscadenzamento. Il PASSO 15 stava caricando lo stesso ELF per le
altre tre console: due `open` diversi, un solo slot.

L'altra meta' del danno la vedeva il caricatore, e sembrava un problema
del tutto scollegato: al primo `fclose()` del programma si trovava
l'handle chiuso sotto i piedi — «ELF: impossibile leggere program headers»
su due console su quattro, **a ogni avvio, da prima di questa sessione**.

`fat12_open()` aveva lo stesso difetto un piano piu' sotto. Corretti
tutti e due: prenotazione immediata, e un `..._fallito()` che la disfa su
ogni uscita d'errore — senza, dopo `VFS_MAX_OPEN` aperture fallite non si
aprirebbe piu' niente.

La regola, che vale per ogni tabella condivisa di questo kernel:
**si prenota prima di bloccarsi, non dopo.**

## 🐛 `strtod("2.5e-2") == 0.025` era falso, e strtod aveva ragione

L'ultima prova rossa. La diagnostica ha stampato i bit dei due operandi:
`3F9999999999999A` da entrambe le parti. Identici, e il confronto falso.

Il colpevole e' `FLDT` nel codice generato: su x87 GCC valuta le costanti
in virgola mobile alla **precisione del coprocessore**, 64 bit di
mantissa, mentre `strtod` ritorna un `double` a 53. Sono due numeri
diversi per costruzione, e 0.025 e' l'unico valore di quella sezione a
non essere rappresentabile esattamente — tutti gli altri (1.5, 7.75,
1024.0) passavano, il che rendeva il difetto ancora piu' convincente.

Il rimedio e' mettere il valore atteso in una variabile `double`, che
forza l'arrotondamento a 53 bit. E' scritto nel codice della prova perche'
la stessa trappola aspetta chiunque confronti virgole mobili in un
programma di EX-OS.

## Il resto della libreria

- **Virgola mobile**: `strtod`/`strtof`/`strtold`, `ldexp`, `strtoll`,
  `strtoull`. La mantissa si accumula in `double` cifra per cifra e si
  scala per una potenza di dieci esatta (`10^22` e' l'ultima che lo e');
  per gli esponenti negativi si **divide** invece di moltiplicare per la
  reciproca, che sarebbe un errore in piu'. Non e' correttamente
  arrotondata a mezzo ULP, ed e' dichiarato in testa alla sezione.
- **`sscanf`/`vsscanf`**: larghezze, `%n`, soppressione `%*`, `%lf`.
- **Data e ora**: `time`, `localtime`, `gmtime`, `mktime`,
  `gettimeofday` sopra `SYS_TIME` (13), che c'era gia'. Senza fuso:
  `localtime` e `gmtime` danno la stessa ora, perche' il sistema non sa
  in quale fuso si trova e inventarne uno sarebbe peggio.
- **`stat`/`fstat`** nella forma POSIX, sopra le syscall rese vere nella
  sessione precedente.
- **Header nuovi**: `math.h`, `inttypes.h`, `assert.h`, `fcntl.h`,
  `time.h`, `sys/stat.h`, `sys/time.h`. `LIBC_HDR` nel Makefile ora
  prende anche `lib/include/sys/*.h`, altrimenti il CD si porterebbe
  dietro copie vecchie di quelli.
- `/bin/mkfs` non dice piu' «EX-OS non sa ancora MONTARE un ext2»: era
  vero nel 0.12x e manda a cercare un driver che c'e'.

## Verifica

- **`/bin/libctest`: 78 su 78**, sia avviato come shell su tutte e quattro
  le console (`[boot] shell = /bin/libctest`, il caso che scatenava la
  corsa) sia lanciato dal prompt.
- **Nessun `[WARN]` di caricamento ELF all'avvio**: prima ne comparivano
  due su quattro console.
- Regressione floppy: `ls`, `mem`, `mount`, `mkdir`, `cp`, `ls`,
  `delete`, `rmdir` — tutti corretti, su piu' avvii consecutivi.
- Regressione **ISO**: automount di `/cdrom`, `ls /cdrom`,
  `cat /cdrom/leggimi.txt`.
- Regressione **ext2** su `dist/hd.img`: `mount hd0p1 /disk`, `mkdir`,
  `cp`, `ls`, `delete`, `rmdir`, `umount` — e **`e2fsck -fn` esito 0**
  sulla partizione estratta.
- Build pulita, nessun warning nuovo (resta il solito `LOAD segment with
  RWX permissions`).

## Cosa resta

- ✅ **RISOLTO nella sessione (d)** — era annotato qui come «osservato una
  volta e non riprodotto»: in un avvio le quattro shell hanno chiesto
  tutte la riga sulla **console 0** («kbd: su console 0 la richiesta di
  PID 5 sostituisce quella di PID 4»), lasciando i comandi senza risposta.
  Non era intermittente e non era delle console virtuali: `tty.o` ha una
  regola propria nel Makefile e non aveva le dipendenze dagli header,
  quindi dopo l'aggiunta di `fpu_state` a `struct Process` continuava a
  leggere `console` all'offset vecchio. Dipendeva da quando `tty.o` era
  stato ricompilato l'ultima volta.
- `make hd` / `tools/mkhd.sh` (disco avviabile da 512 MB) non e'
  documentato ne' qui ne' nel README: e' arrivato con la sessione del
  cross-compilatore e non ha mai avuto la sua riga.
- I tre difetti del bersaglio `i386-exos` gia' annotati (tipi in
  `exos.h`, indirizzo di caricamento, unwind tables): costano un rebuild
  di GCC e conviene farli insieme.
- Il passo grosso resta TCC in `tools/tcc/` e poi in `/bin` del CD.

## File toccati

- **nuovi**: `kernel/arch/x86/fpu.c`, `kernel/include/fpu.h`,
  `lib/include/{math,inttypes,assert,fcntl,time}.h`,
  `lib/include/sys/{stat,time}.h`
- `kernel/fs/vfs.c` — prenotazione dello slot in `vfs_open`, `apri_fallito`
- `kernel/fs/fat12.c` — stesso rimedio in `fat12_open`
- `kernel/sched/sched.c` + `sched.h` — `fpu_state` nel PCB, salvataggio
  e ripristino in `sched_switch_to`
- `kernel/kernel_main.c` — PASSO 7b
- `kernel/include/version.h` — 0.147
- `lib/libc.c` + `lib/include/libc.h` — virgola mobile, `sscanf`, tempo,
  `stat`
- `bin/libctest/libctest.c` — sezioni «Virgola mobile», «sscanf e tempo»,
  «stat»
- `bin/mkfs/mkfs.c` — avviso ext2 obsoleto
- `Makefile` — `LIBC_HDR` con `sys/`, regola di `/bin/libctest`
- `README.md`, `KERNEL_CORE_NOTES.md`

---

# SESSIONE 2026-08-02 — Gli header parlano la lingua del compilatore

Kernel **invariato a 0.146**: sotto `kernel/` non e' stato toccato niente,
e la regola di versionamento chiede l'incremento solo per il kernel.

Il presupposto di questa sessione e' il lavoro immediatamente precedente:
esiste un **cross-compilatore `i386-exos`** (`i386-exos-gcc (GCC) 17.0.0`,
con `libgcc.a` per il bersaglio), il cui port vive in `tools/gcc-exos/` ed
e' documentato per esteso in `tools/gcc-exos/leggimi.md`. `i386-exos-gcc
programma.c -o programma` produce un binario che parte dentro EX-OS senza
una sola opzione sulla riga di comando.

E qui e' saltato fuori che **i nostri header non erano pronti per un
compilatore che non fosse il gcc di sistema**.

## size_t era una promessa che solo un compilatore manteneva

`libc.h` dichiarava i tipi fondamentali da se':

```c
typedef unsigned int    size_t;
```

Vero per il `gcc` di sistema con `-m32`. **Falso** per il bersaglio
`i386-exos`, dove `__SIZE_TYPE__` e' `long unsigned int` — stessa
larghezza, tipo diverso. Basta un sorgente che includa `<stddef.h>`
accanto ai nostri header perche' non compili piu':

```
error: conflicting types for 'size_t'; have 'long unsigned int'
note:  previous declaration of 'size_t' with type 'unsigned int'
```

Cioe' quasi tutto il codice di terzi — a cominciare da quello del
compilatore che si vuole portare dentro EX-OS, che e' il motivo per cui il
cross esiste.

Ora `size_t`, `ptrdiff_t` e `NULL` vengono da `#include <stddef.h>`, che
**non e' un header della libreria ma del compilatore**: lo fornisce anche
in modalita' freestanding, quindi si include senza `-I` e anche dentro
`lib/libc.c`, che il Makefile compila di proposito senza `-I lib/include`.
`ssize_t`, `intptr_t` e `uintptr_t` in `<stddef.h>` non ci sono e vengono
dalle macro predefinite `__PTRDIFF_TYPE__`, `__INTPTR_TYPE__`,
`__UINTPTR_TYPE__`.

Il principio, che vale per tutto cio' che verra' dopo: **un tipo lo
dichiara il compilatore, non noi.** Un typedef scritto a mano non e' una
descrizione, e' una seconda opinione — e vale finche' non cambia
compilatore.

Il blocco dei tipi e' ripetuto in `lib/libc.c`, che resta autosufficiente:
e' la stessa convenzione gia' usata per `DirEntry`, `MemInfo` e i numeri di
syscall, e va tenuta allineata a mano.

## Un `main()` dichiarato in un header vieta `int main(void)`

`libc.h` conteneva `int main(int argc, char **argv);`. Un prototipo in un
header incluso da tutti rende **errore di compilazione** l'altra forma
ammessa dallo standard, `int main(void)` — quella con cui e' scritto quasi
ogni programma di esempio, di prova e di configure:

```
error: conflicting types for 'main'; have 'int(void)'
```

Tolto. L'unico che ha bisogno di quel prototipo e' `lib/libc.c`, che se lo
dichiara da se' prima di `_libc_start`; i due argomenti in piu' che passa a
un `main(void)` la convenzione cdecl li ignora.

## `<stdint.h>` non esisteva sul bersaglio

Sondando gli header del cross: `<stdbool.h>`, `<stdarg.h>`, `<float.h>` e
`<limits.h>` ci sono (li installa GCC), **`<stdint.h>` no** — perche'
`gcc/config.gcc` non imposta `use_gcc_stdint` per il nostro caso. Mancano
anche `<time.h>`, `<fcntl.h>` e `<sys/types.h>`, che sono roba di libc e
non di compilatore.

Nuovo `lib/include/stdint.h`. **Non e' una facciata come gli altri**: gli
altri rimandano a `libc.h` perche' li' ci sono le funzioni, qui ci sono
tipi, e i tipi li sa il compilatore. Ogni riga chiede a lui —
`__INT32_TYPE__`, `__UINT32_MAX__`, `__INT64_C()` — e non scrive
`unsigned int` sperando di indovinare. Larghezze esatte, `least`, `fast`,
`intmax_t`, tutti i limiti (compresi `SIZE_MAX`, `PTRDIFF_MAX`, `WCHAR_*`,
`WINT_*`, `SIG_ATOMIC_*`) e le macro `INTn_C()`.

Il rimedio costa zero rebuild di GCC: `prepara-cross.sh` copia gli header
di `lib/include` nell'albero del bersaglio, quindi basta che il file
esista.

⚠️ **Sul bersaglio `int32_t` e' `long int`, non `int`.** Corretto (32 bit
in entrambi i casi) ma inconsueto: `i386-linux` dichiara `INT32_TYPE "int"`
e il nostro `exos.h` non dichiara niente, quindi GCC ripiega su `"long
int"`. Stessa radice di `__SIZE_TYPE__`. Non fa danni ora che gli header
chiedono al compilatore, ma si vede negli avvisi (`long` dove ci si aspetta
`int`).

## Il CD si rifaceva solo per `libc.h`

`LIBC_HDR` nel Makefile nominava un file solo, mentre la regola del CD
copia `lib/include/*.h`. Con gli header standard aggiunti in agosto,
cambiarne uno non rifaceva l'immagine: sul CD sarebbero finiti header
vecchi, consegnati proprio a chi compila **dentro** EX-OS e non ha modo di
accorgersene. Ora e' `$(wildcard lib/include/*.h)`.

## Verifica

- `make all` pulita, zero warning nuovi (restano i soliti `LOAD segment
  with RWX permissions`, preesistenti).
- **I binari in albero non cambiano.** Il `gcc` di sistema con `-m32`
  predefinisce `__SIZE_TYPE__ unsigned int` e `__PTRDIFF_TYPE__ int`,
  cioe' esattamente i tipi che prima erano scritti a mano: la modifica e'
  a costo zero da questa parte e serve solo dall'altra.
- Col cross: un programma che include `<stddef.h>`, `<stdarg.h>`,
  `<limits.h>`, `<stdint.h>` e i nostri header insieme, con `int main(void)`,
  compila con `-Wall -Wextra` e zero diagnostica. Prima falliva su due
  errori.
- `stdint.h` provato con **17 `_Static_assert`** (larghezze, limiti,
  `INT64_C(1) << 40`, `sizeof(intptr_t) == sizeof(void *)`), superati sia
  dal cross sia dal gcc di sistema a 32 bit.
- `prova.c` ricompilato col cross, `prepara-cross.sh` rilanciato, `make
  floppy` e `make iso` rifatti — il CD ora porta 16 file invece di 15.

Non e' stato rieseguito `/bin/libctest` dentro EX-OS: la shell e'
interattiva. Il codice generato per i programmi di `/bin` e' pero'
identico, per la ragione detta sopra.

## Cosa resta

- **`exos.h`: dire al bersaglio i tipi giusti** — `SIZE_TYPE "unsigned
  int"`, `PTRDIFF_TYPE "int"`, `INT32_TYPE "int"` come fa `i386-linux`.
  Conviene farlo insieme ai due difetti gia' annotati in
  `tools/gcc-exos/leggimi.md` (indirizzo di caricamento `0x08000000` e
  `-fno-asynchronous-unwind-tables`), perche' tutti e tre costano un
  rebuild di GCC: le specs sono compilate dentro il driver.
- `<time.h>`, `<fcntl.h>`, `<sys/types.h>`: header di libc che non
  esistono. Servono quando arrivera' un programma che li include davvero —
  scriverli prima vuol dire indovinare quali `typedef` gli servono.
- Il passo grosso resta quello di sempre: TCC in `tools/tcc/` e poi in
  `/bin` del CD, o il cross usato per compilare su una macchina di
  sviluppo.

## File toccati

- **nuovo**: `lib/include/stdint.h`
- `lib/include/libc.h` — tipi dal compilatore, via `<stddef.h>`; tolto il
  prototipo di `main()`
- `lib/libc.c` — stesso blocco di tipi, allineato
- `Makefile` — `LIBC_HDR` a wildcard
- `dist/floppy.img`, `dist/exos-tools.iso` — rifatti

---

# SESSIONE 2026-08-01 (b) — Libc ospitata e CD degli strumenti

Kernel a **0.145** → **0.146**. Obiettivo dichiarato: portare TCC dentro
EX-OS (e piu' avanti valutare GCC), distribuito sul CD degli strumenti e
non sul floppy. Questa sessione fa il pezzo che nessuna delle due strade
puo' evitare: la **libc**.

`/bin/libctest` prova tutto DENTRO EX-OS e passa **41 prove su 41**.

## Il problema non era stdio: era che free() non esisteva

`free()` era una funzione vuota con un TODO al posto del corpo, e
`malloc()` chiamava `sbrk` a ogni allocazione. Sui programmi di /bin non
si vedeva — allocano poche volte e poi escono, e il processo si porta via
tutto — ma nessun compilatore sopravvive a questo: un parser alloca e
libera in ciclo, e senza riuso cresce fino a esaurire lo spazio pur non
tenendo mai in mano piu' di qualche KB.

C'era di peggio, e silenzioso: `realloc()` copiava `size` byte dal blocco
vecchio **senza conoscerne la dimensione**. Ingrandire leggeva oltre la
fine. Su un heap che cresceva e basta erano byte non ancora scritti, e il
difetto non si manifestava; su un heap con riuso sarebbe stato il dato di
qualcun altro copiato dentro il proprio.

Ora: lista di blocchi in ordine di INDIRIZZO (usati e liberi insieme),
primo adatto, spezzatura con avanzo minimo, e fusione con il vicino
precedente e successivo. La lista e' in ordine di indirizzo e non "solo i
liberi" perche' la fusione ha bisogno dei vicini FISICI; con una lista dei
soli liberi servirebbero i tag di confine — stessa memoria, piu' modi di
sbagliare.

Prezzo dichiarato nel codice: 16 byte di intestazione per blocco e ricerca
lineare. Su centinaia di migliaia di oggetti piccoli si sentira', e il
rimedio (liste per taglia) si aggiunge sopra senza cambiare il contratto.

**La prova che conta**: 2000 `malloc`/`free` in ciclo non spostano
`sbrk(0)` di un byte.

## stdio, e perche' NON e' il line buffering di Unix

`putchar()` faceva una syscall per carattere e `printf()` lo chiamava per
ognuno: ottanta cambi di contesto per una riga.

Ora c'e' il FILE* completo e un solo formattatore per printf/fprintf/
sprintf/snprintf. Ma la politica di buffering e' diversa da Unix, di
proposito: **stdout e stderr si svuotano alla fine di ogni chiamata**.

Con il line buffering, tutto cio' che non finisce con '\n' resterebbe nel
buffer: il prompt della shell sparirebbe fino alla riga dopo, e gfedit
mostrerebbe l'ultima riga incompleta solo al tasto successivo. Unix se la
cava perche' leggere da stdin svuota stdout — ma **gfedit non legge da
stdin**, parla via IPC con il servizio kbd, quindi quella convenzione non
lo salverebbe. Svuotare a fine chiamata toglie la classe di problemi e
tiene quasi tutto il guadagno (una syscall per printf invece di ottanta).
Verificato con uno screenshot di gfedit: disegna correttamente.

`exit()` svuota tutti i flussi. Senza, un programma che scrive un file e
esce senza `fclose()` lo troverebbe monco — l'ultimo pezzo nel buffer di
un processo che non esiste piu'.

## Due syscall rispondevano ENOSYS da sempre

`SYS_STAT` non era **mai stata scritta** (TODO dalla Fase 3) e
`SYS_LSEEK` non sapeva fare SEEK_END. Senza quelle due nessun FILE* puo'
offrire `ftell()` sulla fine, cioe' il modo con cui ogni programma misura
un file prima di leggerlo.

Ora passano dal VFS e valgono su ogni filesystem montato. Per SEEK_END
serviva la dimensione di un file **gia' aperto**: e' nato `vfs_fstat()`,
che parte dall'handle e non dal percorso — ripassare dal percorso darebbe
la dimensione sbagliata su un file rinominato mentre e' aperto. Il corpo
di `vfs_stat` e' stato estratto in `stat_interno()` cosi' i due condividono
la scelta del driver invece di averne due copie.

Corretto anche `SEEK_CUR` con offset negativo: sotto zero diventava una
posizione enorme su interi senza segno.

## errno si AGGIUNGE, non sostituisce

Su Unix una syscall fallita ritorna -1 e mette il motivo in errno. Qui le
funzioni ritornano l'errore negativo (-2 = ENOENT, -30 = EROFS) e tutti i
programmi stampano quel numero. Cambiare convenzione voleva dire
riscrivere ogni chiamante per guadagnare zero: `< 0` resta il test giusto
in entrambi i mondi, e -EIO dice piu' di -1.

Quindi errno viene impostato **in piu'** (helper `err_reg`), e ci sono
`strerror()` e `perror()` per chi vuole un messaggio.

## Il resto della libreria

setjmp/longjmp in assembly — non si possono scrivere in C: salvare
esattamente i registri che la convenzione dichiara conservati e' proprio
cio' che il compilatore ha il permesso di riorganizzare. Poi strtol,
strtoul, qsort (shell sort: niente ricorsione, niente caso peggiore
sull'input gia' ordinato), bsearch, strstr, strdup, strtok, strspn,
strcspn, memchr, strncat, ctype, e i wrapper lseek/stat/sbrk/fsize.

`sbrk` e' esposto perche' newlib e picolibc chiedono esattamente quella
funzione, e nient'altro, per far girare il proprio malloc: il giorno che
si decide di portarne una, il gancio c'e'.

Intestazioni con i nomi standard (`<stdio.h>`, `<stdlib.h>`, `<string.h>`,
`<ctype.h>`, `<errno.h>`, `<setjmp.h>`, `<unistd.h>`) che rimandano a
libc.h: facciate sottili di proposito, perche' due elenchi della stessa
funzione divergono e la divergenza si manifesta come prototipo sbagliato,
non come errore di compilazione.

## ⚠️ I binari erano raddoppiati, e il rimedio vale per sempre

Ogni programma di /bin compila `lib/libc.c` dentro di se'. Con lo stdio
nuovo `ls` e' passato da 12 a 25 KB, e 17 programmi × 13 KB sono 220 KB su
un floppy da 1.44 MB.

`-ffunction-sections -fdata-sections` in `CFLAGS_USER` e `--gc-sections`
su tutti i link dei programmi utente: il linker butta le funzioni che
nessuno raggiunge. `ls` e' tornato a **10 KB** — meno di prima che la
libreria crescesse — e il floppy ha **897 KB liberi**, contro i 909 di
partenza, con in piu' tutta la libc nuova e /bin/libctest.

I linker script raccoglievano gia' `.text.*` / `.rodata.*` / `.data.*` /
`.bss.*` e dichiarano `ENTRY(_start)`, che e' la radice della raccolta:
non e' servito toccarli.

## Verifica

- `/bin/libctest`: **41 prove su 41** dentro EX-OS — riuso dei blocchi,
  fusione, heap stabile su 2000 cicli, realloc che conserva, ampiezza e
  precisione di printf, interi a 64 bit, snprintf che tronca e riporta la
  lunghezza vera, fopen/fgets/ftell/fseek/ungetc/feof, dimensione del file
  su disco, longjmp da dodici livelli di stack, strtol/qsort/bsearch/
  strtok/ctype, errno.
- Regressione: `ls`, `mem`, `mount`, `mkdir`, `rmdir`, `cp`, `delete`,
  `trunc`, `ls /cdrom` — tutti invariati. `gfedit` disegna correttamente
  (screenshot).
- Build pulita, zero warning nuovi.

## `make iso` — il CD degli strumenti (fatto nella stessa sessione)

```bash
make iso        # dist/exos-tools.iso
make run-iso    # QEMU con il CD gia' inserito
```

`tools/mkiso.py` e' passato da "immagine di prova a contenuto fisso" a
masterizzatore vero: prende un albero di directory e costruisce
**entrambi** gli alberi del formato — ISO 9660 (8.3 maiuscoli con `;1`) e
Joliet (nomi veri in UCS-2) — condividendo i blocchi dei file. Gestisce
sottodirectory annidate, directory su piu' blocchi, tabelle dei percorsi
per tutti e due gli alberi, e le collisioni del troncamento a 8.3 con un
contatore (due file distinti che diventassero lo stesso nome sarebbero un
file perso in silenzio).

Il modo di prova resta, con `--prova`: serve a collaudare il driver con
un'immagine di cui si conosce ogni byte.

Contenuto attuale del disco (`tools/iso/leggimi.txt` + il Makefile):

```
/leggimi.txt          cos'e' questo disco e come si monta
/exos/include/        gli header della libc
/exos/libc.c          la libc in un file solo
/exos/start.S         il pezzo di avvio che chiama main()
/doc/                 README, note sul kernel, licenza
/bin/                 vuota: e' il posto del compilatore
```

`/exos/` non e' documentazione: e' cio' che serve per COMPILARE su EX-OS,
ed e' il motivo per cui questo disco esiste prima di TCC. Il CD **non**
fa parte di `make all`: il floppy e' l'artefatto principale e chi compila
il sistema non deve aspettare un CD che magari non usera'.

⚠️ Conseguenza del supporto, da tenere presente quando arrivera' il
compilatore: il CD e' in sola lettura, quindi header e librerie si
leggono da li' ma l'OUTPUT deve andare altrove — un EX-OS installato su
ext2, o il floppy. Compilare avendo come unica scrittura il floppy e'
possibile ma stretto.

Provato in QEMU: `ls /cdrom`, `ls /cdrom/exos/include`, `ls /cdrom/doc`,
`cat /cdrom/leggimi.txt` e `cp /cdrom/exos/include/stdio.h /copia.h` —
1615 byte copiati dal CD al floppy, cioe' esattamente la dimensione del
file.

## Prossimo passo

I sorgenti di TCC in `tools/tcc/` (versione consigliata: 0.9.27, C99
puro, backend i386 collaudato), da mettere poi in `/bin` del CD. Quando la
libc verra' spezzata in un archivio vero (`libc.a`, un file per area) gli
header standard prenderanno il proprio contenuto e ogni programma
smettera' di ricompilarsela.

## File toccati

- **nuovi**: `bin/libctest/libctest.c` + `.ld`, `lib/include/{stdio,stdlib,
  string,ctype,errno,setjmp,unistd}.h`
- `lib/libc.c` — allocatore, stdio, formattatore, setjmp, conversioni,
  ctype, errno (da 997 a ~2200 righe)
- `lib/include/libc.h` — dichiarazioni, `FILE` opaco, `jmp_buf`, `Stat`
- `kernel/syscall/syscall_impl.c` — `sys_stat` implementata, `SEEK_END`
- `kernel/fs/vfs.c` + `vfs.h` — `vfs_fstat`, `stat_interno`
- `Makefile` — `-ffunction-sections`/`--gc-sections`, `LIBC_HDR`,
  regola di `/bin/libctest`, target `iso` e `run-iso`
- `tools/mkiso.py` — riscritto: da immagine di prova a masterizzatore di
  un albero di directory, con entrambi gli alberi del formato
- `tools/iso/leggimi.txt` — il testo che si trova sul CD
- `kernel/include/version.h`, `README.md`, `KERNEL_CORE_NOTES.md`

---

# SESSIONE 2026-08-01 — CD/DVD: driver ATAPI, ISO 9660, automount

Kernel a **0.144** → **0.145**. Un CD dati si monta, si elenca e si legge;
la riga `[mount] /cdrom = cd0` lo monta all'avvio. E il default di
`verboseboot` è passato da **1 a 0**: l'avvio è silenzioso salvo richiesta.

```
disk                    il lettore compare come cd0
mount cd0 /cdrom
ls /cdrom
umount /cdrom
```

## `verboseboot` — il default si è invertito, e i casi dubbi lo seguono

Il valore predefinito è 0 e vale in tutti i casi dubbi: voce assente, file
mancante, valore non numerico. Solo un NUMERO diverso da zero fa parlare
il sistema — `verboseboot = si` non è un sì, è un refuso, e un refuso non
deve decidere niente. È la stessa regola di prima, girata: il controllo
«è davvero un numero?» resta perché `cfg_atoi()` si ferma al primo
carattere non numerico e ritorna 0, quindi senza di lui un errore di
battitura sceglierebbe al posto dell'utente.

Cambiati insieme: `cfg.c` (default e ramo della chiave), `cfg.h`,
`boot/kernel.cfg`, la regola del Makefile che genera un `kernel.cfg` di
default, README.

## Il lettore sta sullo stesso bus del disco, ma non prende comandi ATA

Un dispositivo ATAPI vive sui canali IDE e ne usa i registri, ma vuole un
**pacchetto** di 12 byte — un comando SCSI — consegnato attraverso il
registro dati. Il registro che su un disco contiene l'LBA, qui contiene
quanti byte il dispositivo consegna per ogni DRQ.

Per questo `kernel/block/atapi.c` non è una variante di `ata_rw()` con un
comando diverso, e per questo gli helper di temporizzazione di `ata.c`
(ritardo di 400 ns, attese ancorate al PIT, selezione dell'unità) sono
stati **esportati** invece che duplicati: cambiano i comandi, non il bus.
Una seconda copia delle regole di attesa è una copia che un giorno diverge
— la lezione è già in testa a `ata_rw()`.

`ata_attendi_drq()` si è sdoppiata in una variante **muta**: su ATAPI un
ERR è spesso «vassoio vuoto», cioè una risposta, e stamparlo come errore
riempirebbe il log a ogni sondaggio.

### Il ciclo della fase dati è l'unico punto dove ci si blocca davvero

Un ATAPI non dice in anticipo quanti byte manda: consegna una raffica per
volta e prima di ognuna scrive in LBA1/LBA2 quanti byte contiene *quella*.
Tre regole ne discendono, e tutte e tre sono nel codice con il perché:

1. non si conta «quanti DRQ mi aspetto»: si cicla finché il dispositivo
   abbassa DRQ con BSY basso;
2. il conteggio della raffica va riletto ogni volta — un lettore può
   spezzare 2048 byte in due da 1024 e ha ragione lui;
3. i byte che eccedono il buffer del chiamante vanno letti e **buttati**.
   Fermarsi a metà lascia il dispositivo in attesa e il canale
   inutilizzabile per chiunque altro, disco rigido accanto compreso.

## 2048 contro 512: la traduzione sta in un punto solo

`blk_read()` traduce, e nessun altro lo sa. Un blocco vale quattro
settori: chiedere il settore 3 significa chiedere il blocco 0 e prenderne
l'ultimo quarto — senza traduzione si leggerebbe il blocco 3, 6 KB più in
là, **senza alcun errore**. Le richieste allineate (quasi tutte, perché
ISO 9660 lavora a blocchi) vanno dritte al dispositivo; solo le code
disallineate passano dal buffer di appoggio.

## La capacità è del disco, non del lettore

È la differenza che ha richiesto una funzione nuova, `blk_supporto()`. Per
un disco rigido la lunghezza è una proprietà del dispositivo, nota una
volta per tutte al rilevamento; per un lettore no: esiste solo finché c'è
un disco dentro, e cambia a ogni inserimento. Un `cd0` con zero settori
non è rotto — è vuoto, o non ancora sondato.

`blk_supporto()` sonda e aggiorna la finestra; su un dispositivo non
rimovibile risponde 1 senza toccare niente, così chi chiama non deve
sapere che tipo ha in mano. La chiamano `vfs_mount()` (prima di
identificare il volume) e `blk_read()` (se la finestra è ancora a zero),
ed è per questo che un disco inserito dopo l'avvio si legge senza doverlo
annunciare.

**Il vassoio all'avvio non viene sondato.** Un lettore può metterci
secondi a dichiararsi pronto, e pagarli a ogni accensione per un supporto
che magari nessuno leggerà non ha senso.

## ⚠️ Il caso che ha richiesto un giro di debug: «disco inserito, non lo vede»

Inserendo un'immagine a caldo (`change ide1-cd0 file.iso` dal monitor
QEMU) il montaggio continuava a rispondere `-123` (ENOMEDIUM). Il log
strumentato ha detto perché:

```
sense k=2 asc=0x3a ascq=0x00      <- MEDIUM NOT PRESENT
sense k=2 asc=0x3a ascq=0x00
```

**Un lettore appena rifornito risponde con lo stato di un attimo prima**,
per un comando o due, e solo dopo ammette il cambio con UNIT ATTENTION.
In più, in emulazione l'immagine viene inserita lasciando il vassoio
*aperto*, e un vassoio aperto risponde «nessun supporto» anche con un
disco dentro. Alcuni lettori distinguono i due casi con ASCQ 0x02 («tray
open»), ma non tutti — QEMU risponde 0x00 in entrambi — e fidarsi di un
campo facoltativo significa funzionare su un lettore su due.

La risposta è in `atapi_supporto()`: alla prima «assenza» si chiude il
vassoio (una volta sola, come fa Linux all'apertura di un lettore), poi si
insiste ancora due volte a 250 ms prima di crederci. Il prezzo, dichiarato
nel codice: montare su un lettore lasciato aperto e vuoto lo chiude, e
«vassoio vuoto» costa qualche decimo di secondo invece di essere
immediato. Meglio questo di un disco inserito che il sistema non vede.

## ISO 9660: due alberi, non due viste

Un disco con nomi lunghi contiene **due strutture di directory complete**
che puntano agli stessi dati: quella ISO (`LEGGIMI.TXT;1`) e quella Joliet
(nomi veri in UCS-2). Si sceglie Joliet quando c'è — altrimenti si mostra
all'utente `LEGGIM~1.TXT` al posto di ciò che ha scritto — e si dice quale
si è scelta.

Le trappole del formato, tutte silenziose (nessuna dà errore, tutte danno
dati sbagliati), sono elencate in testa a `kernel/fs/iso9660.c`. Le due
che costano di più:

- **i numeri sono scritti due volte**, little e big endian di seguito: un
  campo a 32 bit occupa 8 byte, e sbagliare di quattro l'offset del campo
  dopo legge la metà big endian del precedente;
- **un byte di lunghezza a zero non è la fine della directory**, è
  riempimento fino al blocco successivo. Trattarlo come fine tronca
  l'elenco al primo blocco pieno: su una directory con molti file
  spariscono quelli dopo i primi 2 KB.

Sola lettura per proprietà del formato, non per limite del driver: ISO
9660 non ha bitmap di liberi né voci riutilizzabili. Non esiste
«aggiungere un file», esiste rifare l'immagine. `vfs_write`, `modifica()`
e `vfs_truncate` hanno comunque un rifiuto esplicito per ISO, ridondante
finché il montaggio resta forzato in sola lettura — serve a impedire che
un domani un montaggio ISO finisca nel ramo FAT *per esclusione*.

## Automount: un lettore vuoto non è un problema

`[mount] /cdrom = cd0` è **attiva** nella configurazione predefinita, e
può restarlo su qualunque macchina. Il PASSO 13d distingue: `ENOMEDIUM` e
«lettore assente» sono INFO e «montaggio saltato», tutto il resto resta
`[WARN]`. Senza la distinzione, ogni accensione senza CD avrebbe messo una
riga fra i *problemi durante l'inizializzazione* dell'avvio silenzioso —
cioè esattamente il rumore costante che quel registro esiste per evitare.

## Come si prova senza masterizzare niente

`tools/mkiso.py` (versionato) costruisce un'immagine di cui si conosce
ogni byte: PVD, SVD Joliet, una sottodirectory e un file da 5280 byte
— più di due blocchi — con ogni riga numerata, così un salto di blocco si
vede subito.

```bash
python3 tools/mkiso.py /tmp/test.iso                    # con Joliet
python3 tools/mkiso.py /tmp/solo-iso.iso --senza-joliet

EXOS_QEMU_EXTRA="-cdrom /tmp/test.iso" \
  python3 tools/qemu_drive.py "mount" "ls /cdrom" "cat /cdrom/hello.txt"
```

## Verifica

- **Automount con disco**: `[PASSO 13d] 1 montaggi su 1 riusciti`,
  `VFS: montato cd0 su /cdrom (ISO 9660/Joliet, sola lettura)`.
  `ls /cdrom` → `Leggimi importante.txt`, `documenti/`, `hello.txt`.
- **Automount senza disco**: nessun `[WARN]`, solo
  `'cd0' su '/cdrom': nessun disco nel lettore, montaggio saltato`.
- **Nomi ISO puri** (immagine `--senza-joliet`): `docs/`, `hello.txt`,
  `leggimi.txt` — `;1` tolto, minuscole, sottodirectory raggiungibile.
- **File su più blocchi**: `cat /cdrom/documenti/note-lunghe.txt` rende
  tutte e 120 le righe, dalla 0000 alla 0119, senza salti ai confini.
- **Cambio a caldo**: inserimento → `mount` riuscito; espulsione →
  `-123`; inserimento di un'immagine DIVERSA → montata, e `ls` mostra il
  contenuto NUOVO (la capacità e i nomi in cache erano stati invalidati).
- **Scrittura respinta**: `mkdir /cdrom/prova` → `-30` (EROFS).
- **Avvio silenzioso**: schermata con la sola riga di identità e il
  prompt, nessun banner, nessun problema segnalato.
- **Regressione floppy**: avvio normale, `/` su `fd0` FAT12 in
  lettura/scrittura.

## File toccati

- **nuovi**: `kernel/block/atapi.c`, `kernel/include/atapi.h`,
  `kernel/fs/iso9660.c`, `kernel/include/iso9660.h`, `tools/mkiso.py`
- `kernel/block/ata.c` + `ata.h` — helper esportati, `IDENTIFY PACKET`
  per il modello del lettore, `ata_attendi_drq_muto`, `ata_attesa_ms`
- `kernel/block/blk.c` + `blk.h` — `cd0`, `BLK_TIPO_CDROM`,
  `blk_supporto`, traduzione 2048/512
- `kernel/block/vol.c` + `vol.h` — riconoscimento ISO (firma al blocco 16)
- `kernel/fs/vfs.c` + `vfs.h` — `VFS_FS_ISO`, sola lettura imposta
- `kernel/kernel_main.c` — `atapi_init` al PASSO 13a, PASSO 13d gentile
- `kernel/syscall/syscall_impl.c`, `kernel/include/syscall.h` — `ENOMEDIUM`,
  `fs = 9` in `MountInfo`, documentazione di `BlkInfo.tipo`
- `kernel/fs/cfg.c` + `cfg.h`, `boot/kernel.cfg`, `Makefile`, `README.md`
  — `verboseboot`, sezione `[mount]`, sorgenti nuovi
- `bin/mount/mount.c`, `bin/disk/disk.c` — ISO 9660 e `cd0` nell'elenco

---

# SESSIONE 2026-07-31 (n+7) — Avvio da ext2

Kernel a **0.140** → **0.141**. **EX-OS si installa su ext2 e ci si
avvia.** Verificato senza floppy collegato: root `hd0p1` ext2 in
lettura/scrittura.

```
fdisk hd0                    partizione tipo 83, attiva
mkfs -t ext2 -L exos hd0p1
mount hd0p1 /disk
install /disk
```

## Il problema non era leggere ext2: era che un file ext2 non è contiguo

Il protocollo di avvio passava **un solo** `(lba, cnt)` per il kernel. Non
bastava, e non per frammentazione — è la struttura del formato. Il kernel
da 147 KB appena copiato su un volume vergine sta così:

```
(0-11):74-85, (IND):86, (12-144):87-219
```

Il blocco di **puntatori** viene allocato in mezzo ai dati, perché serve
prima del tredicesimo blocco. Con un intervallo solo non sarebbe
caricabile **nessun** kernel da ext2.

## Cosa è cambiato nel contratto a tre

L'area di patch a 0x1A0 è passata da 20 byte a 88:

```
+0   magia   'EXHD'
+4   s2_lba  dd     Stage 2: sempre UN intervallo
+8   s2_cnt  dw
+10  k_size  dd     dimensione esatta del kernel
+14  k_next  dw     quanti intervalli seguono
+16  k_ext[12] { dd lba; dw cnt }
```

`s2_lba`/`s2_cnt` **non hanno cambiato offset**, quindi
`boothd.asm` non è stato toccato nel codice: solo la dichiarazione.

**Stage 2 resta un intervallo solo, e non è una svista.** Sta in ~1 KB,
cioè dentro i primi 12 blocchi diretti, dove nessun indiretto si è ancora
infilato. Ed è il pezzo che dev'essere trovato da 512 byte di codice: la
sua mappa deve stare in sei byte.

Su FAT la lista ha una voce sola. Il formato è **lo stesso per i due
filesystem**, così Stage 2 non deve sapere da dove sta caricando.

## Il BPB si conserva solo su FAT, e su ext2 è l'opposto

Su FAT i byte 3..89 del settore 0 sono il BPB: sovrascriverlo rende
illeggibile il volume che si sta rendendo avviabile.

Su ext2 quei byte **non sono metadati**. I primi 1024 byte sono l'area
riservata al record di avvio — è per questo che `s_first_data_block` vale
1 con blocchi da 1024 — e il superblocco comincia solo dopo. Conservarli
avrebbe bucato il codice del settore di avvio con 87 byte di spazzatura:
un avvio che salta nel nulla.

## Le maiuscole, che FAT nascondeva

Il kernel cerca `/bin/sh`, `/boot/kernel.cfg`, `/dev/kbd.drv`: tutto
minuscolo. `install` creava `BOOT`, `BIN`, `LIB`, `DEV` e i file con i
nomi che FAT12 restituisce — cioè **maiuscoli**.

Su FAT non si vedeva: il driver mette in maiuscolo sia ciò che scrive sia
ciò che cerca, quindi `BIN` e `bin` sono la stessa directory. Su ext2 sono
due directory diverse, e il sistema installato non avrebbe trovato la
propria shell.

`install` ora crea tutto in minuscolo, **nomi dei file compresi**: da FAT12
i nomi arrivano sempre maiuscoli perché è come il formato li conserva, e
l'informazione sul caso originale non esiste più. Il minuscolo è l'unica
ricostruzione sensata, ed è quella che il sistema si aspetta.

## `vfs_init` riconosce il filesystem della root invece di assumerlo

Prima c'era solo `fat_mount()`. Avviando da ext2 quella chiamata fallisce
e il sistema ripiegava sul floppy — che durante un avvio da disco non c'è.
Il sintomo era «shell non trovata», a diversi passi dalla causa vera. Ora
passa da `vol_identifica()`.

## Perché non un lettore ext2 dentro Stage 2

Era l'altra strada, e va detto perché non è stata presa. Stage 2 è
**assembly puro**, 1095 byte (`bootloader/stage2/loader.asm`; i `.c` in
quella cartella non li compila nessuno). Un lettore ext2 lì dentro sono
~350 righe di NASM a 16 bit — superblocco, descrittori, inode, indiretti,
ricerca per nome — e **Stage 2 stesso resterebbe agganciato alla mappa**,
perché va trovato senza saper leggere niente.

La lista di intervalli costa ~80 righe, è indipendente dal filesystem e
non introduce codice nel percorso più difficile da verificare del sistema.
Il prezzo è che il patto LILO resta: ricopiare kernel o Stage 2 obbliga a
rilanciare `install`.

## Verifica

- **Avvio da ext2 senza floppy**: `ver` mostra 0.141, `ls /` mostra
  `boot/ bin/ lib/ dev/`, `mount` dice `/ hd0p1 ext2 lettura/scrittura`.
  Marker seriale `SDKPJK`. `e2fsck -fn` sul volume avviabile: **esito 0**.
- L'installatore ha riportato `kernel 289 settori in 2 intervalli` — cioè
  esattamente i due tratti previsti dal formato.
- **Regressione FAT32**: stessa procedura su un disco FAT32,
  `kernel 289 settori in 1 intervallo`, avvio senza floppy riuscito,
  `fsck.vfat` esito 0.
- **Regressione floppy**: avvio pulito, nessun errore nel log.

---

# SESSIONE 2026-07-31 (n+6) — Nomi lunghi ext2

Kernel a **0.139** → **0.140**. Un nome di **255 caratteri** si crea, si
elenca, si digita e si apre. `VfsDirEntry` non tronca più a 16 byte.

## Non era una struttura: erano sei tetti in fila

`VfsDirEntry.nome[16]` era il più visibile, non l'unico. Alzarlo da solo
avrebbe spostato il taglio di un passo, e il nome sarebbe morto poco più
avanti. La catena, dal disco alla tastiera:

| dove | era | ora | cosa tagliava |
|---|---|---|---|
| `Ext2DirEntry.nome` | 60 | 256 | il nome letto dal driver |
| `VfsDirEntry.nome` | 16 | 256 | il nome consegnato al VFS |
| `DIRENT_NAME_MAX` (ABI syscall) | 13 | 256 | il nome consegnato a userland |
| `VFS_PATH_MAX` / percorsi syscall | 128 / 256 | 320 | il percorso da aprire |
| `MAX_ARG_LEN` (sys_spawn) | 128 | 320 | l'argomento passato al programma |
| `KBD_LINE_MAX` | 256 | 512 | la riga digitata |

Il numero è 256 = 255 caratteri + NUL, cioè il massimo di ext2. Su FAT i
nomi restano 8.3: il campo è largo per il filesystem più generoso.

**Un nome troncato non è un nome accorciato: è un nome che non apre
niente.** È la ragione per cui non aveva senso fermarsi a metà strada.

## I due tetti che tagliavano IN SILENZIO

Sono i più istruttivi, perché nessuno dei due dava un errore.

**`MAX_ARG_LEN` in `sys_spawn`.** Un argomento più lungo di 127 byte
faceva uscire dal ciclo di copia, e il programma partiva **con gli
argomenti raccolti fino a lì**: `cp <lungo> <dest>` diventava `cp`, che
stampa il proprio uso. L'utente conclude che il file non esiste. Ora un
argomento illeggibile o troppo lungo **ferma lo spawn** con EINVAL e una
riga di log che dice quale.

**`KBD_LINE_MAX`.** Una riga più lunga di 256 byte veniva consegnata
tagliata — ma **l'eco a schermo era completo**, perché lo fa il TTY mentre
si digita. Si vedeva il comando giusto e ne veniva eseguito un altro.
Alzato a 512, che è il tetto vero: `IPC_MSG_MAX_DATA`. Oltre, il payload
verrebbe troncato dal kernel senza che nessuna delle due sponde se ne
accorga.

## Il blocco per chiamata ora è una costante condivisa

`DirEntry` è passata da 20 a 264 byte, quindi il numero di voci per
chiamata è diventato il numero che moltiplica: `READDIR_MAX_BATCH` è
sceso da 64 a 16 (4,2 KB di stack kernel invece di 17).

Ma abbassare quel tetto rendeva sbagliato un idioma diffuso: `ls`,
`delete` e `install` chiedevano 32 voci e si fermavano appena ne
ricevevano meno di 32 — con il tetto a 16 si sarebbero fermati alla prima
pagina, mostrando mezza directory. Ora `libc.h` espone
**`LISTDIR_MAX_BATCH`** e i quattro chiamanti usano quello: "ne ho
ricevute meno di quante ne ho chieste, quindi sono finite" torna a essere
sempre vero.

## Verifica

- File con nomi da **255** e **200** caratteri creati da Linux: `ls` in
  EX-OS li mostra **interi**, e `cp` li apre e ne legge il contenuto —
  quello da 255 con un comando di 276 caratteri.
- Directory con nome da **117 caratteri creata da EX-OS**: `debugfs` da
  Linux conferma il nome esatto, `e2fsck -fn` esito **0**.
- Regressione FAT: `ls /bin`, `mkdir`, `cp`, `delete`, `rmdir` sul floppy;
  poi `fdisk` + `mkfs -t fat32` + `mount` + `cp` su disco. `fsck.vfat`
  esito 0, avvio da floppy senza errori nel log.

## Cosa resta troncato, e va detto

Sopra i 511 caratteri una riga di comando non si può digitare: è il limite
di un singolo messaggio IPC. Un nome da 255 ci sta con abbondanza, due
percorsi lunghi nello stesso comando no. Superarlo richiederebbe di
spezzare la riga su più messaggi, cioè un protocollo diverso fra tastiera
e shell — non un numero più grande.

---

# SESSIONE 2026-07-31 (n+5) — Troncamento a una dimensione qualunque

Kernel a **0.138** → **0.139**. `ext2_truncate()` accetta qualunque
dimensione, in su e in giù. Nuovo comando **`/bin/trunc`** e nuova syscall
**`SYS_TRUNCATE` (92)**.

```
trunc dati.bin 4096
trunc dati.bin 512K
trunc dati.bin 2M
```

## Il caso che si dimentica: l'indiretto a cavallo del taglio

Un blocco di puntatori ha **tre** destini, non due:

| dove sta | cosa fare |
|---|---|
| interamente oltre il taglio | libera il sottoalbero **e** il blocco di puntatori |
| interamente prima | non toccare niente |
| **a cavallo** | scendi dentro, pota solo le voci oltre il taglio, e il blocco di puntatori **sopravvive** |

Il terzo è quello che si salta. Liberare un indiretto perché "il
troncamento lo tocca" significa buttare via i puntatori ai blocchi che
**restano**: il file conserva la sua dimensione e perde i dati in mezzo, e
i blocchi orfani vengono riassegnati ad altri file.

`pota_indiretto()` ritorna 1 solo se il blocco di puntatori è rimasto
davvero **vuoto** — è il chiamante a liberarlo, non lui.

## Allungare non alloca

Portare un file da 1 KB a 2 MB non consuma 2 MB: lo spazio in mezzo
diventa un **buco**. `mappa_blocco()` ritorna 0 e `ext2_read()` consegna
zeri; i blocchi si materializzano solo quando ci si scrive.

Verificato: un file da 2 MB creato così ha `Blockcount: 2`, cioè **un solo
blocco da 1024 byte**.

## La coda dell'ultimo blocco va azzerata

Accorciando, i byte fra la nuova dimensione e la fine del blocco che resta
sono oltre `i_size` e nessuno li legge — **finché il file non viene
riallungato**, e allora ricomparirebbero come contenuto. Un file troncato
e poi riesteso deve dare zeri, non i propri dati vecchi.

Verificato: un file portato a 100 byte e poi a 2 MB ha i primi 100 byte
originali e **tutto il resto a zero**.

## L'arrotondamento è in su

`tenere = ceil(nuova_dim / dim_blocco)`. Con `nuova_dim = 1500` e blocchi
da 1024 servono **due** blocchi, perché il secondo contiene i byte da 1024
a 1499. Arrotondare in giù libererebbe un blocco ancora dentro il file.

## `SYS_TRUNCATE` e `/bin/trunc`

Il troncamento parziale non aveva un chiamante — `O_TRUNC` va solo a zero —
e codice non raggiungibile in uno scrittore di filesystem è codice non
provato. Da qui la syscall (numero 92, lo stesso di Linux) e il comando.

`vfs_truncate()` rifiuta le directory con EISDIR: le loro dimensioni le
decide il driver aggiungendo o togliendo voci, e tagliarne una a metà
renderebbe irraggiungibili i file che contiene senza cancellarli. Sul
floppy risponde **ENOSYS**: `fat12.c` non ha un troncamento e non gliene
si aggiunge uno per l'occasione — è il driver del volume di avvio, la
strada collaudata che non si tocca senza una ragione forte.

`trunc` distingue "argomento non numerico" da "zero": senza,
`trunc file pippo` svuoterebbe il file invece di dire che non ha capito.

## Verifica

Tutte con `e2fsck -fn`, **esito 0**.

- File da 614400 byte (doppio indiretto) troncato a 307200: restano 300
  blocchi, cioè 12 diretti + 256 dell'indiretto semplice + 32 nel doppio
  → **il doppio indiretto è potato parzialmente**, che è il caso difficile.
  I 307200 byte tenuti sono identici all'originale.
- File portato a 100 byte e poi a 2 MB: primi 100 byte intatti, buco tutto
  a zero, `Blockcount: 2`.
- **Sull'altra disposizione**: volume `mke2fs -b 4096 -I 256`, file da
  716800 byte troncato a 200000 — 49 blocchi tenuti, quindi indiretto
  semplice potato parzialmente. Contenuto identico, e2fsck 0.
- Troncamenti a 0 e a un multiplo esatto del blocco: nessuna anomalia.

Nota metodologica: due confronti sono risultati falliti a un primo giro, e
non era un difetto del troncamento — il file di riferimento era
`build/kernel.bin`, ricompilato più volte da quando era stato copiato nel
volume. Le verifiche buone usano un contenuto deterministico generato per
l'occasione, non un artefatto di build che cambia sotto.

---

# SESSIONE 2026-07-31 (n+4) — Scrittura ext2

Kernel a **0.137** → **0.138**. Su un ext2 si scrive: `mkdir`, `cp`,
`delete`, `rmdir`, e i volumi restano puliti per `e2fsck`.

## La decisione che ha reso possibile il resto: cinque buffer, non uno

Era già un difetto in lettura — c'era una toppa in `ext2_readdir` che
rileggeva il blocco della directory dopo ogni `leggi_inode`, perché
condividevano lo stesso buffer. Ora ce n'è uno per **genere** di blocco:
`b_dati`, `b_ind`, `b_ino`, `b_bmp`, `b_desc`.

In lettura quel difetto dava nomi inventati. **In scrittura la stessa
confusione fa scrivere una voce di directory dentro una tabella di
inode**, cioè corrompe il volume in un punto che non c'entra niente con
l'operazione richiesta. Il costo sono 20 KB di BSS; il beneficio è che chi
legge il codice vede dal **nome** quale contenuto sta guardando, e nessuna
funzione può pestare i piedi a un'altra.

La toppa in `readdir` è sparita: non serve più.

## I tre posti che devono restare d'accordo

Ogni allocazione e ogni liberazione tocca:

1. il bit nella bitmap del gruppo;
2. il contatore dei liberi nel **descrittore** di quel gruppo;
3. il contatore dei liberi nel **superblocco**.

Se uno resta indietro il volume non è rotto — i dati ci sono — ma
l'allocatore comincia a mentire: un contatore più alto del vero fa credere
che ci sia spazio che non c'è. Non esiste una scorciatoia che ne tocchi
solo uno.

Quando il descrittore dice "ci sono liberi" e la bitmap dice di no, si
crede alla **bitmap**: è la verità, il contatore è un riassunto. Il gruppo
viene saltato con un WARN invece di consegnare un blocco già occupato.

## Un blocco appena allocato si azzera sempre

Contiene i byte di chi lo usava prima. Consegnarlo com'è significa che il
contenuto di un file cancellato ricompare dentro un file nuovo — e usato
come blocco di **puntatori** quei byte sarebbero indirizzi verso mezzo
volume.

## L'errore che ha trovato e2fsck: `i_dtime` non può valere 1

Su un inode **liberato** ext2 riusa `i_dtime` come puntatore al prossimo
elemento della catena degli orfani, cioè come **numero di inode**. Ne
discende una regola che la specifica non enuncia da nessuna parte:

> `i_dtime` di un inode liberato deve essere `>= s_inodes_count`.

Con `i_dtime = 1` — il valore più innocuo del mondo — ogni file cancellato
produceva `Inode NN was part of the orphaned inode list`, ed e2fsck usciva
con 4. EX-OS non ha un orologio letto dal kernel, quindi si usa una data
fissa e dichiaratamente convenzionale (`0x40000000`, gennaio 2004) con una
guardia che alza il valore se un volume avesse davvero più inode di così.
La guardia rende esplicito l'invariante invece di affidarlo al fatto che
nessuno formatterà mai un miliardo di inode.

## Lo spazio libero di una directory è nascosto dentro i `rec_len`

Una directory ext2 non ha una lista di buchi: la lunghezza dichiarata di
una voce **non è** quella occupata. L'ultima voce di ogni blocco si allunga
fino in fondo, e una voce cancellata viene assorbita da quella che la
precede.

Aggiungere una voce significa quindi cercare un `rec_len` più lungo del
necessario e **spezzarlo**, non cercare un buco. Cancellarne una significa
allungare il `rec_len` della precedente — o azzerare l'inode, se è la prima
del blocco.

`i_size` di una directory è **sempre** un multiplo esatto della dimensione
del blocco, mai la somma delle voci.

## Cosa NON c'è, e perché

`ext2_truncate()` gestisce **solo** il troncamento a zero, cioè `O_TRUNC`.
Troncare a una dimensione qualunque significa liberare la coda della catena
lasciando intatta la testa, indiretti parziali compresi: è l'operazione più
facile da sbagliare del driver, e sbagliarla libera blocchi che il file usa
ancora. Finché non serve davvero, non c'è.

Non c'è cache in scrittura: ogni operazione arriva al disco prima di
ritornare. `ext2_sync()` esiste per completare l'interfaccia — un VFS che
deve sapere quali filesystem hanno un sync e quali no è un VFS che tira a
indovinare.

⚠️ **Niente giornale.** ext2 non ne ha e questo driver non ne inventa uno:
fra la scrittura della bitmap e quella del superblocco c'è una finestra in
cui un'interruzione lascia i contatori indietro. È la stessa finestra di
ext2 su Linux e la risposta è la stessa, e2fsck. Ciò che **non** può
succedere è che un blocco risulti libero mentre è in uso: la bitmap si
scrive **prima** che il blocco venga consegnato.

## Verifica

Tutte con `e2fsck -fn`, **esito 0**.

- Volume creato da EX-OS (120 MB, blocchi da 1024): due livelli di
  directory, due file, cancellazione di file e directory, poi un file
  nuovo scritto **sui blocchi riciclati**. Contenuti estratti con
  `debugfs` e confrontati con `cmp`: identici.
- File da **614400 byte** — oltre i 274 KB coperti dall'indiretto
  semplice, quindi con **doppio indiretto** — copiato da EX-OS *dentro*
  l'ext2: letto e riscritto, identico all'originale.
- `rmdir` su directory non vuota: rifiutata con ENOTEMPTY.
- Montaggio con `-r`: `mkdir` respinta con EROFS.

**La prova che conta**: scrittura su un volume di `mke2fs -b 4096 -I 256`
— blocchi da 4096, `s_first_data_block = 0`, inode da 256, cioè nessuna
delle tre cose che il nostro formattatore produce. Creata una directory,
scritti due file, cancellato un file preesistente: e2fsck esito 0 e il
file da 143656 byte identico.

---

# SESSIONE 2026-07-31 (n+3) — ext2

Kernel a **0.136** → **0.137**. EX-OS **crea** un ext2 e lo **legge**.

```
mkfs -t ext2 -L dati hd0p1
mount hd0p1 /e
ls /e
```

Scritto **dalla specifica**, non portato.

## Perché non un porting, visto che le licenze lo permettevano

La licenza non c'entrava: EX-OS è GPL-2.0-or-later, `libext2fs` è LGPL-2.0,
`mke2fs` e il driver ext2 di Linux sono GPL-2.0. Si poteva copiare.

Il problema era ingegneristico. `fs/ext2/` di Linux **non è un modulo che
legge ext2**: è un modulo che *traduce* ext2 nel VFS di Linux, cucito
addosso a `buffer_head`, alla page cache e a `struct super_block`.
Portarlo significa portare quel VFS, cioè un pezzo di Linux più grande di
EX-OS. e2fsprogs trascina `libext2fs` + `libcom_err` + `libuuid` +
`libblkid`, `open`/`pread`/`ioctl` e allocazione dinamica ovunque.

Il formato invece è documentato, e la documentazione non è coperta da
quelle licenze. Sono ~600 righe per il formattatore e ~500 per il driver:
meno del porting, e si capisce ogni byte — che con un porting non succede.

## Le sei cose che e2fsck pretende e la specifica non dice

Sono quelle che un formattatore scritto "leggendo le struct" sbaglia:

1. **`i_blocks` è in unità da 512 byte**, non in blocchi del filesystem.
   Una directory da un blocco da 1024 ha `i_blocks = 2`.
2. **I bit di riempimento delle bitmap**, oltre la fine reale del gruppo,
   devono essere a **uno**. A zero si dichiarano liberi blocchi che non
   esistono.
3. Gli **inode riservati 1..10** vanno marcati usati anche se non
   descrivono niente. `s_first_ino` vale 11 proprio per quello.
4. **`/lost+found` deve esistere.** Senza, e2fsck si offre di crearlo — e
   un filesystem appena formattato che fa già proporre una riparazione è
   un filesystem su cui, il giorno che il problema è vero, nessuno
   guarderà più.
5. **`i_links_count` della radice è 3, non 2**: le voci che puntano alla
   radice sono `.` dentro di sé, `..` dentro di sé (la radice è padre di
   sé stessa) e `..` dentro `lost+found`.
6. La somma dei contatori liberi dei descrittori deve combaciare
   **esattamente** con quella nel superblocco.

## L'errore che ha trovato il compilatore

`-Warray-bounds` ha segnalato una scrittura all'offset 1280 di un buffer
da 1024. L'inode 11 (`lost+found`) **non sta nello stesso blocco della
tabella** dell'inode 2 (la radice): con inode da 128 byte in blocchi da
1024 ce ne stanno **otto** per blocco, quindi l'11 è nel secondo blocco
all'offset 256.

Scritto com'era, `lost+found` sarebbe rimasto senza inode e 256 byte
sarebbero finiti oltre il buffer.

## Il driver rifiuta invece di provarci

ext2 dichiara le funzionalità in tre insiemi, e la differenza fra i tre è
precisamente cosa fare quando non le si conosce: `compat` si ignora,
`ro_compat` impone la sola lettura, **`incompat` impone di NON montare**.

La terza è la ragione del rifiuto: una funzionalità incompatibile cambia
il significato dei campi che già si sanno leggere. Un volume con extent
(ext4) ha `i_block` che **non contiene numeri di blocco**: leggerlo "come
se" restituisce dati presi da posizioni arbitrarie del disco, in silenzio.

Essendo in sola lettura, di `ro_compat` non ci importa niente.

## Le tre trappole che rendono un driver "funziona solo sui miei volumi"

1. La **dimensione del blocco** non è fissa: `1024 << s_log_block_size`.
   `mkfs` scrive sempre 1024, ma mke2fs usa 4096 sui volumi grandi.
2. **`s_first_data_block`** vale 1 con blocchi da 1024 e **0** con blocchi
   più grandi. Cambia dove comincia *ogni* gruppo.
3. La **dimensione dell'inode** è 128 solo in revisione 0. In revisione 1
   la dice `s_inode_size`, e su ext2 moderni è spesso **256**.

Per questo il test decisivo è stato su un volume fatto da `mke2fs` con
blocchi da 4096 e inode da 256 — cioè la configurazione che il nostro
formattatore non produce mai.

## Sola lettura è una scelta, non un lavoro lasciato a metà

Un ext2 scrivibile richiede allocatore di blocchi e inode, aggiornamento
coerente di due bitmap e tre contatori a ogni operazione, e una risposta a
cosa succede se la corrente va via a metà. **Leggere richiede di capire il
formato; scrivere richiede di non romperlo mai.** Farli insieme significa
scoprire gli errori del primo dentro i danni del secondo.

`vfs_mount()` impone `sola_lettura = 1` sugli ext2 **qualunque cosa abbia
chiesto il chiamante**. Così una `mkdir` fallisce con EROFS — che l'utente
capisce — invece di arrivare a un driver che quella funzione non ce l'ha e
fallire con un errore generico a tre livelli di distanza.

## Due bug trovati integrando

**`sys_mountinfo` chiamava `fat_tipo(m->mnt)` su un montaggio ext2**:
l'handle appartiene al driver del montaggio, e passarlo a fat.c legge la
sua tabella a un indice che lì descrive tutt'altro volume. Il tipo va
scelto *prima* di chiamare chiunque.

**`/bin/mount` riferiva il flag richiesto, non il risultato**: diceva
"lettura/scrittura" su un ext2 che il kernel aveva appena forzato in sola
lettura. Ora interroga `mountinfo` e dice cosa è successo davvero.

C'è anche una trappola dentro `ext2_readdir`: leggere l'inode di una voce
riusa il buffer globale, quindi il blocco della directory va **riletto**
prima di guardare la voce successiva. Senza, si scorrono i byte di una
tabella di inode credendo che siano voci di directory.

## Verifica

**Formattatore**: `e2fsck -fn` esito **0** su un ext2 creato da EX-OS su
una partizione da 299 MB (38 gruppi, 19152 inode). Poi Linux ci ha creato
una directory e scritto due file — incluso uno da 127 KB che richiede
blocchi indiretti — ed e2fsck è rimasto a 0. `dumpe2fs` conferma
etichetta, UUID (versione 4, variante DCE), `filetype sparse_super`,
revisione 1.

**Driver**: montato in EX-OS un ext2 *scritto da Linux*, percorsi a tre
livelli risolti (`/e/prova/dentro`), scrittura rifiutata con EROFS. Il
file da 127268 byte copiato su floppy e confrontato con `cmp`:
**identico**.

**La prova che conta**: un volume di `mke2fs -b 4096 -I 256`, cioè blocchi
da 4096, `s_first_data_block = 0` e inode da 256 — nessuna delle tre cose
che il nostro formattatore produce. Montato, elencato, e un file da 131368
byte letto e confrontato: **identico**.

## Cosa manca

La scrittura. E `VfsDirEntry.nome` è di 16 byte mentre ext2 arriva a 255:
i nomi lunghi vengono troncati, ed è un limite della struttura che
attraversa la syscall, non del driver.

---

# SESSIONE 2026-07-31 (n+2) — Formattazione, e il ciclo si chiude

Kernel a **0.135** → **0.136**. Da un disco vergine a un volume montato
**senza uscire da EX-OS**:

```
fdisk hd0                       crea le partizioni
mkfs -t fat32 -L DATI hd0p1     ci scrive un filesystem
mount hd0p1 /disk               montalo
install /disk                   rendilo avviabile
```

Resta ext2, che è un lavoro a sé.

## `mkfs` in userspace, `bootinst` nel kernel: stessa domanda, risposte opposte

Vale la pena scriverlo perché la contraddizione è solo apparente e prima o
poi qualcuno la solleverà.

`bootinst.c` sta nel kernel perché scrive nel **settore 0**, fuori da ogni
filesystem, dove un errore rende irraggiungibile un disco intero. Lì non
esiste un controllo in userspace che un programma non possa semplicemente
non usare: basta non usare `/bin/install`.

Un formattatore scrive solo **dentro** una partizione, cioè dentro una
finestra che `blk.c` fa già rispettare su ogni singolo accesso. Non c'è
niente da proteggere che `blk_write()` non protegga già, e mettere 500
righe di generazione di tabelle nel kernel per una garanzia che il kernel
offre gratis sarebbe pagare due volte.

## Perché la tabella delle partizioni è irraggiungibile da `mkfs`

Non per una regola, **per costruzione**: `SYS_BLKREAD`/`SYS_BLKWRITE`
accettano solo dispositivi di tipo `BLK_TIPO_PART`. Il settore 0 non
appartiene a nessuna partizione, quindi non esiste una coppia (nome, LBA)
che lo raggiunga. Non c'è un controllo sull'LBA da aggirare perché non
c'è un controllo sull'LBA.

Le altre tre condizioni: partizione non montata (sopra c'è una cache
write-back, scriverci sotto vuol dire che il primo `sync` ci ricopre i
settori vecchi), non in sola lettura, al massimo `BLKIO_MAX_SETT` settori
per chiamata. Il kernel copia **un settore per volta** con un buffer di
512 byte: il costo sullo stack kernel non cresce con quanto chiede il
processo.

## Il numero di cluster, che è tutto

Il tipo di un volume FAT **non è scritto da nessuna parte**. La stringa
`"FAT16   "` nel settore di avvio è decorativa, e `vol.c` giustamente non
la guarda: il tipo si deduce dal numero di cluster dell'area dati.

Ne discende che un formattatore che sceglie male i settori per cluster
produce un volume che **dice FAT16 e cade nella banda FAT12**. Il nostro
driver lo leggerebbe in un modo, Linux in un altro, e nessuno dei due
segnalerebbe niente finché i dati non sono già rovinati.

Per questo `mkfs` ricalcola il conteggio, lo **riverifica** contro le
soglie prima di scrivere un byte, e lo **mostra** accanto alla soglia. Le
costanti stanno in `mkfs.c` accanto a un commento che dice che devono
restare uguali a quelle di `vol.c`.

## L'ordine di scrittura protegge da un'interruzione

Il settore di avvio vecchio si azzera **per primo**, quello nuovo si
scrive **per ultimo**. In mezzo il volume non è riconoscibile da nessuno.

L'ordine opposto sembra equivalente e non lo è: una formattazione
interrotta lascerebbe un settore di avvio che descrive il filesystem
**vecchio** sopra tabelle FAT già azzerate. Il volume verrebbe montato,
sembrerebbe funzionante e restituirebbe file vuoti. Meglio un volume che
nessuno riconosce che uno che mente.

## L'errore che ha trovato fsck.vfat

`BPB_BkBootSec` sta all'offset **50**, non 52. A 52 cominciano 12 byte
riservati. Scritto a 52 — l'errore facile, e l'avevo fatto — il campo
vero resta a zero: il volume **dichiara di non avere copia del settore di
avvio**, e ogni strumento di verifica lo segnala.

Non l'avrebbe trovato nessun test scritto da chi ha scritto il
formattatore: il nostro driver non guarda quel campo, quindi montare e
rileggere sarebbe andato benissimo. È il motivo per cui la verifica vera
è `fsck.vfat`, non `mount`.

Corretto anche il conteggio dei cluster liberi in FSInfo: scrivevo
0xFFFFFFFF ("sconosciuto"), che è legale ma è rumore, perché al momento
della formattazione il numero è esatto e non stimato. Un volume che nasce
con una segnalazione addosso è un volume in cui la segnalazione **vera**,
il giorno che arriva, passa inosservata.

Nota su chi mantiene quel campo dopo: `fat.c` non lo aggiorna quando
alloca cluster, e lo rimette a "sconosciuto". È la cosa giusta — meglio
"non lo so" di un numero stantìo — e infatti `fsck.vfat` lo segnala senza
considerarlo un errore.

## Un messaggio d'errore che mentiva

`mkfs` diceva sempre «il volume ora NON è riconoscibile, il settore di
avvio vecchio è stato azzerato per primo». Vero quasi sempre — ma non nel
caso più probabile: quando la partizione è **montata** il kernel rifiuta
la primissima scrittura, quindi sul volume non è successo niente.

Il messaggio mandava a cercare un danno inesistente su una partizione che
può essere la root del sistema che sta girando. Ora `mkfs` tiene un flag
`toccato`, alzato solo dopo la prima scrittura riuscita, e nell'altro caso
dice che il volume è esattamente com'era.

## Verifica

Disco vergine da 512 MB, partizionato e formattato **da dentro EX-OS**.

- `fsck.vfat -n` su entrambi i volumi: **esito 0**, nessun errore, sia
  appena formattati sia dopo che EX-OS ci ha scritto dentro;
- `mdir` legge le etichette `PRIMA` e `SECONDA`;
- montati entrambi contemporaneamente (`/a` FAT32, `/b` FAT16), creata una
  directory, copiati due file, riletti con `ls`, smontati;
- i file scritti da EX-OS estratti con `mcopy` e confrontati con `cmp`:
  **identici byte per byte** agli originali.

FAT32 su 200 MB: 1 settore per cluster, FAT da 3175 settori, 403218
cluster. FAT16 su 311 MB: 16 settori per cluster, FAT da 156 settori,
39786 cluster. Entrambe le dimensioni di cluster sono quelle delle tabelle
della specifica Microsoft — un volume formattato con valori diversi
funziona, ma smette di somigliare a ciò che ogni altro sistema si aspetta.

## Cosa NON fa

Niente FAT12 (il floppy si formatta altrove), niente etichette lunghe,
niente allineamento della FAT a un confine di cluster.

`mkfs -t ext2` risponde «non ancora implementato», di proposito: è già
nell'elenco delle opzioni perché è il prossimo passo, e un'opzione che
manca è meno chiara di una che dice quando arriverà.

---

# SESSIONE 2026-07-31 (n+1) — Partizionamento

Kernel a **0.134** → **0.135**. EX-OS **inizializza un disco rigido
vergine**: fino a ieri un disco nuovo andava partizionato altrove e poi
portato qui.

```
fdisk hd1
```

Restano da fare il formattatore (`mkfs` FAT16/FAT32) e poi ext2. Senza
formattatore il ciclo non si chiude ancora: `fdisk` crea le partizioni,
`mount` fallisce finché non c'è un filesystem dentro.

## Le due invarianti si incastrano

`bootinst.c` riscrive **solo i byte 0..445** dell'MBR e non tocca mai la
tabella. `mbr_scrivi()` fa l'esatto complementare: **solo i byte
446..511**, e rilegge dal disco il codice di avvio per rimetterlo al suo
posto.

Ne discende una proprietà che vale la pena sapere: **`fdisk` e `install`
si possono dare in qualunque ordine**. Partizionare non cancella l'avvio,
installare l'avvio non cancella la tabella.

⚠️ **Unica eccezione, ed è deliberata**: se il settore 0 non ha la firma
0x55AA, `mbr_scrivi()` **azzera** i 446 byte. Aggiungere la firma
significa dire al BIOS di eseguire quei byte, che fino a un attimo prima
nessuno eseguiva; lasciarli com'erano vorrebbe dire far saltare la
macchina dentro il contenuto casuale di un disco mai inizializzato.

## Un solo elenco di controlli, non due

`mbr_valida()` è stata estratta da `mbr_leggi()` e ora la chiamano in
due: la **lettura** su ciò che trova sul disco, la **scrittura** sulla
proposta.

Non è fattorizzazione per eleganza. Due elenchi separati — uno per
leggere, uno per scrivere — divergono, ed è questione di tempo; il giorno
che divergono la scrittura accetta una tabella che la lettura segnala
come rotta, cioè il partizionatore produce dischi che il sistema stesso
critica.

Due controlli nuovi che valgono anche in lettura: `PT_PROB_SETTORE0`
(partizione che comincia dall'LBA 0, cioè che contiene l'MBR che la
descrive) e il limite dei 32 bit dell'MBR, sull'ultimo settore e non
sulla somma — una partizione che arriva esattamente in fondo è legittima.

## Il contatore degli usi, e perché non è una domanda al VFS

`BlkDev` ha un campo `in_uso`: `vfs_mount()` lo incrementa, `vfs_umount()`
lo decrementa, `blk_rescan()` lo consulta per rifiutare.

La strada ovvia sarebbe stata far chiedere al livello a blocchi «VFS, hai
per caso montato questo?». Ma così il livello a blocchi dovrebbe
conoscere i filesystem, **e conoscerli tutti, uno per uno**, man mano che
arrivano: il giorno che esiste ext2 servirebbe una domanda in più, e il
giorno che qualcuno la dimentica il rescan ripartiziona sotto un volume
montato senza accorgersene.

Col contatore la dipendenza va nel verso giusto — chi usa dichiara di
usare — e un filesystem nuovo non può sbagliare per omissione: se non
acquisisce, non ha nemmeno un dispositivo su cui lavorare.

Acquisita anche la root in `vfs_init()`: senza quella riga il disco da
cui EX-OS sta girando risulterebbe libero, e ripartizionarlo sarebbe
permesso.

## `blk_rescan()` NON compatta l'array

Gli indici dei dispositivi a blocchi sono tenuti dai montaggi attivi
(`VfsMount.blkdev`). Se rimuovere `hd0p1` facesse scalare di uno tutti
quelli dopo, un filesystem montato su `hd1p1` si troverebbe a leggere da
un'altra partizione **senza che nulla lo segnali**.

Gli slot liberati restano vuoti con `usato = 0` e vengono riusati.
Conseguenza da sapere: `g_n` è il massimo indice mai usato, non il numero
di dispositivi vivi, e **chiunque scorra l'array deve saltare le voci con
`usato == 0`**. `blk_get()` ora ritorna NULL su quelle voci e
`blk_trova()` le salta.

## La porta d'ingresso è una sola

`blk_ripartiziona()` (in `blk.c`, non in `mbr.c`) fa nell'ordine:
rifiuta se una partizione del disco è in uso — **prima** di toccare il
disco, non dopo — scrive, rilegge.

Sta lì perché `mbr_scrivi()` sa scrivere una tabella ma non sa niente dei
dispositivi che quella tabella descrive. Lasciare al chiamante il compito
di ricordarsi il rescan è un contratto che regge finché qualcuno non se
ne dimentica, e chi se ne dimentica lascia in memoria finestre che sul
disco non esistono più: il livello a blocchi smette di essere una
protezione e diventa il modo per scrivere nel posto sbagliato.

## Perché `/bin/fdisk` e non `/bin/disk` che cresce

`disk` è dichiarato in sola lettura in testa al proprio sorgente, ed è
una proprietà che serve: è il comando che si lancia senza pensarci su un
disco a cui si tiene. Un programma che a seconda degli argomenti guarda
**oppure** riscrive la tabella perde quella garanzia per tutti gli usi,
non solo per quello nuovo.

Divisione dei compiti: in `fdisk` le **politiche** (allineamento a 1 MiB,
primo settore utile 2048, valori predefiniti, tipi proponibili); nel
kernel le **regole** (niente sovrapposizioni, niente oltre la fine, né
GPT né partizioni montate). I controlli nel programma servono a dare
l'errore mentre l'utente sta ancora componendo la tabella, non alla `w`
quando pensa di aver finito — non sostituiscono quelli del kernel.

## Verifica

Disco vergine da 256 MB, settore 0 tutto a zero.

I 64 byte della tabella scritti da EX-OS sono risultati **identici byte
per byte** a quelli che `sfdisk` di Linux scrive per la stessa tabella,
campi CHS compresi (geometria 255×63, saturazione 0xFE 0xFF 0xFF sopra il
cilindro 1023). `fdisk -l` da Linux rilegge le partizioni esatte.

Verificato inoltre:

- area di avvio azzerata quando la firma non c'era;
- area di avvio 0..445 **intatta** ripartizionando un disco che ce
  l'aveva già (motivo riconoscibile scritto prima, riletto identico dopo);
- `w` su un disco con una partizione montata: rifiutato con -16, e la
  tabella sul disco **non modificata**;
- dispositivi `hd1p1`/`hd1p2` disponibili subito dopo la `w`, senza
  riavviare.

## Cosa NON fa

Non formatta. Una partizione appena creata contiene i byte che c'erano
prima in quei settori: non è vuota, è non inizializzata.

Non gestisce le partizioni **logiche** e non scrive EBR. Le mostra, ma la
voce estesa che le contiene è bloccata sia in `fdisk` sia nel kernel:
spostarla lascerebbe la catena di EBR viva sul disco e irraggiungibile,
senza che niente lo dica.

Non converte da e verso GPT: un disco GPT viene riconosciuto e rifiutato.

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

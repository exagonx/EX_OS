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

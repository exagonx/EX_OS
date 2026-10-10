# 1. Installare da floppy

[torna all'indice](00-indice.md)

È la strada collaudata, e l'unica che funziona su una macchina il cui lettore
ottico il kernel non sa leggere.

---

## 1.1 Quale immagine

| immagine | quando |
|---|---|
| `dist/floppy.img` | la macchina normale: il kernel legge il floppy da sé |
| `dist/test_aa3k_remoto.img` | lettore sull'USB, oppure si vuole guidare da un'altra stanza |

Il secondo è più grande e porta di più: driver di rete, `fdisk`, `mkfs`,
`install`, `mkdir`, `scarica`, e una shell telnet.

! **SI COSTRUISCE COL VOLUME IN RAM, E NON E' UN DETTAGLIO.** Il bersaglio
  `test-aa3k-remoto` passa `RAMDISCO=1`, cioè Stage 2 copia l'intero dischetto
  in memoria e la radice è quella copia. Senza, su una macchina col lettore
  sull'USB il kernel arriva in fondo e non trova nessuna radice.

```
make test-aa3k-remoto
dd if=dist/test_aa3k_remoto.img of=/dev/fd0 bs=512
sync
```

! **VERIFICA DI AVER SCRITTO QUELLO CHE CREDI.** I floppy sbagliano, e un
  dischetto vecchio non si distingue da uno nuovo a occhio. Appena la macchina
  è su:

```
install -version
```

  Se la versione non è quella che ti aspetti, il dischetto è vecchio e andare
  avanti non ha senso. `make test-aa3k` stampa apposta l'md5 del driver che ha
  messo sul dischetto accanto a quello appena costruito: due file possono
  pesare uguale al byte ed essere diversi.

---

## 1.2 Preparare il disco

```
disk                      cosa c'e' (ma vedi l'avvertenza)
fdisk hd0                 la tabella vera
```

! **`disk` PUO' MOSTRARTI MENO DI QUEL CHE C'E'.** Su un disco con quattro
  partizioni ne ha elencata una sola. La tabella completa la dà `fdisk hd0`
  appena parte, prima ancora di digitare qualcosa.

Dentro `fdisk`:

```
 z        azzera TUTTA la tabella  (chiede di scrivere `azzera`)
 w   si   scrive
 q
```

! **CON UNA PARTIZIONE ESTESA SERVONO DUE GIRI.** Il kernel lascia sparire
  un'estesa che contiene partizioni logiche **solo** verso una tabella
  completamente vuota: prima si scrive il vuoto, poi si rientra e si creano le
  nuove. `fdisk` te lo dice da solo quando vede delle logiche. Il perché è nel
  [capitolo 5](05-quando-va-storto.md).

Secondo giro:

```
fdisk hd0
 n        Invio  Invio  Invio      una sola, dall'inizio, tutto il disco
 83                                tipo Linux
 w   si
 q
```

---

## 1.3 Formattare

```
mkfs -t ext2 -L SISTEMA -f hd0p1
```

! **ext2 E NON FAT, SE PUOI SCEGLIERE.** Su FAT i nomi lunghi sono in sola
  lettura, e i programmi con più di otto lettere — `netdetect`, `automount` —
  diventano `NETDET~1` e la shell non li trova. Su ext2 il problema non
  esiste.

! **`-t ext2` VA CHIESTO.** Senza `-t`, `mkfs` sceglie FAT16 o FAT32 dalla
  dimensione: ext2 non entra mai nella scelta automatica, ed è voluto — è un
  formato che si chiede, non uno in cui si finisce.

---

## 1.4 Installare

```
mount hd0p1 /disk
install -t -senza-conti /disk
```

Alla domanda sulla lingua rispondi `1`.

| opzione | cosa fa |
|---|---|
| `-t` | sistema e tutti i componenti trovati, senza chiedere |
| `-m` | solo il minimale |
| `-senza-conti` | non chiede le password: le crea `login` al primo avvio |
| `-a` | aggiorna solo i file cambiati, e prima li elenca |

! **`-senza-conti` NON E' UN RIPIEGO.** Chiedere una password vuol dire
  leggerla senza mostrarla, e questo si può fare solo su una console vera: da
  una sessione remota quella lettura non torna e l'installazione resta lì con
  il sistema già copiato. E chi installa da un'altra stanza non deve digitare
  la password di chi userà la macchina.

Devi vedere scorrere anche una sezione **Driver** con i file copiati in
`/disk/dev/`. Se dice «i driver NON sono stati installati», fermati e leggi il
[capitolo 5](05-quando-va-storto.md): senza `/dev/kbd.drv` la macchina si
avvia **senza tastiera**.

---

## 1.5 Riavviare

Togli il floppy e riavvia. `login` ti chiederà di creare i due conti — `root`
per riparare, il tuo per lavorare — con la tastiera davanti.

Poi passa a [Dopo l'installazione](04-dopo-installazione.md).

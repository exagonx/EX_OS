# 2. Installare da CD-ROM

[torna all'indice](00-indice.md)

È la strada comoda quando c'è: l'installatore mostra il disco a schermo
intero, chiede le misure e fa tutto lui.

! **QUESTA PROCEDURA NON E' STATA PROVATA DI RECENTE.** Quel che segue viene
  dal codice di `bin/cdinstall` e dalle sue stesse parole, non da
  un'installazione eseguita e vista finire. È scritto qui perché serva, non
  perché sia collaudato come quella da floppy. Se la percorri e qualcosa non
  torna, la cosa più utile che puoi fare è segnarlo nel
  [capitolo 5](05-quando-va-storto.md).

---

## 2.1 Prima di tutto: il kernel sa leggere quel lettore?

Questa domanda viene prima di ogni altra, e la risposta non è ovvia.

Il BIOS avvia dal CD con l'emulazione floppy (El Torito), e fin lì tutto bene.
Ma dopo il modo protetto il kernel deve parlare al controller ATAPI da sé, e
su certe macchine non ci riesce.

! **IL SEGNALE E' CHIARO E IL SISTEMA LO DICE.** Se `fat12_init` fallisce — il
  controller floppy non risponde perché dietro l'emulazione non c'è nessun
  controller — il VFS cerca un CD-ROM vero e ci monta sopra la radice. Nel
  registro d'avvio si legge:

```
VFS: root '/' su cd0 (ISO 9660, avvio da CD per emulazione floppy)
```

  Se invece nessun CD è leggibile, il messaggio è:

```
VFS: nessun floppy e nessun CD leggibile: la root restera' vuota
```

  In quel caso il CD **non è la tua strada**: passa al
  [capitolo 1](01-da-floppy.md).

---

## 2.2 L'immagine

```
make iso-exos          ->  dist/exos.iso
```

Non va confusa con `dist/exos-tools.iso`, che è il CD degli **strumenti** —
compilatori e sorgenti — e non si avvia.

---

## 2.3 L'installatore

Avviata la macchina dal CD:

```
cdinstall
```

Fa tutto il giro in quattro passi, e li numera a schermo:

1. la tabella delle partizioni
2. il filesystem — **ext2**, sul volume di sistema e su quello dei dati
3. l'area di scambio (`mkswap`), se l'hai chiesta
4. la copia, che chiama `/bin/install` — perché la copia sta scritta in un
   posto solo, ed è quello

! **CHIEDE UNA VOLTA SOLA, E MOSTRA IL PIANO INTERO PRIMA.** Partizioni,
  misure, cosa si perde: si legge tutto e si risponde una volta. È il motivo
  per cui `mkfs` ha l'opzione `-f`, che serve a chi la domanda l'ha già fatta
  al posto tuo — non a chi ha fretta.

Le partizioni che propone sono di tipo `0x83`, cioè quello che dichiara ext2
anche a Linux e a un `fdisk` qualunque.

---

## 2.4 Perché non si può guidare da remoto

`cdinstall` è a schermo intero: disegna riquadri, aspetta tasti, e il suo
stato sta sullo schermo. Una sessione telnet a righe non lo guida.

Se la macchina la stai installando da un'altra stanza, la strada è il
[floppy](01-da-floppy.md) con `install` a riga di comando.

---

## 2.5 Dopo

Il CD porta il sistema di base **e** gli strumenti, se li hai costruiti con
`make iso`. Per il resto, e per la rete, vedi
[Dopo l'installazione](04-dopo-installazione.md).

# Installare EX-OS — indice

Tre supporti, tre capitoli. Si scelgono guardando **come la macchina sa
avviarsi**, non quale sembri più comodo.

| | capitolo | quando si sceglie |
|---|---|---|
| 1 | [Da floppy](01-da-floppy.md) | la macchina ha un lettore di dischetti, anche USB |
| 2 | [Da CD-ROM](02-da-cdrom.md) | c'è un lettore ottico e il BIOS ci sa avviare |
| 3 | [Dalla rete](03-dalla-rete.md) | il sistema di base c'è già e va riempito o aggiornato |

Dopo l'installazione:

| | capitolo | cosa copre |
|---|---|---|
| 4 | [Dopo l'installazione](04-dopo-installazione.md) | conti, rete, strumenti, grafica, accesso remoto |
| 5 | [Quando va storto](05-quando-va-storto.md) | gli errori veri, con la causa e il rimedio |
| 6 | [Pubblicare il repository](06-pubblicare-il-repo.md) | per chi tiene il server degli aggiornamenti |

---

## Le tre strade non sono alternative: sono in fila

Questa è la cosa che si capisce tardi, e costa tempo.

**Il floppy e il CD installano il SISTEMA DI BASE**: kernel, shell, programmi,
driver, librerie. È un sistema che si avvia e si usa, e non ha i compilatori,
non ha ExWin completo, non ha OpenSSL.

**La rete aggiunge il resto.** Il repository è diviso in pacchetti, e quello
di base è uno solo:

| pacchetto | contiene |
|---|---|
| `sistema` | shell, programmi, driver, librerie, ExWin |
| `base` | gcc, cpp, as, ld, la libc e gli header |
| `build` | make, ar, ranlib, nm, strip, objcopy, objdump |
| `cpp` | C++: cc1plus, libstdc++ |
| `fb` | FreeBASIC |
| `nasm` | nasm e ndisasm |
| `ssl` | OpenSSL |

! **`gcc` STA IN `base`, NON IN `build`.** Il nome inganna: `build` sono gli
  strumenti *attorno* al compilatore. Chi installa `build` e prova `gcc` si
  sente rispondere «comando non trovato» con tutta la ragione del mondo.

---

## Prima di cominciare: cosa serve sapere della macchina

Tre domande, e le risposte cambiano la procedura.

**1. Il kernel sa leggere il supporto da cui la macchina si avvia?**

Non è la stessa cosa del BIOS. Il BIOS legge in modo reale con l'INT 13h; il
kernel, dopo il modo protetto, quell'interfaccia non ce l'ha più e deve
parlare al controller. Un lettore di floppy **sull'USB** il BIOS lo usa, il
kernel no.

Se la risposta è no, serve `RAMDISCO=1`: Stage 2 copia l'intero supporto in
memoria e il kernel ci monta sopra la radice. Vedi il capitolo 1.

**2. C'è un disco rigido, e cosa c'è sopra?**

`disk` lo dice, ma **`fdisk hd0` lo dice meglio**: `disk` può mostrarne una
sola quando ce ne sono quattro. Se c'è una partizione **estesa** con dentro
delle logiche, leggi il capitolo 5 prima di toccare qualcosa: c'era un vicolo
cieco, e la via d'uscita è un comando che non si trova per caso.

**3. La macchina è raggiungibile in rete?**

Se sì, si può installare da un'altra stanza. Se no, tutto si fa dalla
tastiera.

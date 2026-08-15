# La direzione — vincoli che valgono per OGNI modifica futura

*Deciso il 12 agosto 2026. Non è un elenco di cose da fare in ordine: è un
insieme di vincoli che ogni servizio, driver, server e applicazione scritti
da qui in avanti devono rispettare, anche quelli che con la grafica e con la
memoria non c'entrano niente.*

> **Perché un documento a parte e non una voce in `RIPRENDERE.md`.** Quel file
> è un diario: si legge dall'alto e le voci vecchie scorrono via. Questo va
> letto **prima di scrivere codice nuovo**, e deve restare in cima per mesi.
> `KERNEL_CORE_NOTES.md` dice già cos'è questo kernel; questo dice dove va.

---

## Le quattro direttive

### 1. Ogni cosa nuova nasce in spazio utente

`KERNEL_CORE_NOTES.md` lo chiede già — *«la domanda da farsi prima di
aggiungere codice qui: può essere un processo ring3? Quasi sempre sì»* — e
d'ora in poi non è più una preferenza.

Il kernel oggi è **ibrido**: alcune cose passano da syscall, altre da server
in ring 3. ! **Quello stato è un fatto da ridurre, non un permesso.** Che
esista già un pezzo dentro il kernel non autorizza il prossimo pezzo a
entrarci accanto: se ci sta un server, si fa un server.

Nel kernel restano soltanto:

- ciò che richiede il privilegio (paginazione, tabelle, modo protetto);
- ciò che arbitra fra processi (scheduler, confini fra spazi, IPC);
- ciò che serve **prima** che i processi esistano (ATA, perché il disco si
  legge per caricare i driver — vedi il commento in testa a `ata.c`).

Tutto il resto è un processo, e un processo che muore è un processo che
muore.

### 2. La grafica va in userspace, ed è il caso di prova della direttiva 1

Su Linux un guasto nel driver grafico porta via la macchina. Qui il server
grafico dev'essere un processo ring 3: quando muore, muore lui. Kernel,
scheduler, console seriale e tastiera restano vivi, la macchina si comanda da
un terminale remoto o si riavvia digitando alla cieca.

> ! **MA OGGI «ALLA CIECA» SAREBBE LETTERALE E DEFINITIVO, e va risolto.**
>
> La modalità video si imposta con `INT 10h`, cioè col BIOS, cioè **in modo
> reale**: l'unico che può farlo è Stage 2, prima del passaggio a modo
> protetto (il perché è scritto in testa a `drivers/svga/svga.c`). Dopo, quella
> porta è chiusa, e **niente nel kernel tocca i registri VGA**.
>
> Conseguenza: se il server grafico muore con la scheda in modalità grafica,
> il sistema è vivo ma **lo schermo resta congelato su quello che c'era**,
> fino al riavvio. Il comando digitato alla cieca funziona e non se ne vede
> l'effetto.
>
> Perciò fa parte del progetto, non è un dettaglio da rimandare: **deve
> esistere un modo di rimettere il testo da modo protetto**, senza il BIOS e
> senza il server morto.
>
> **FATTO il 13 agosto 2026**, con la sequenza classica del modo 3 sui
> registri VGA: `kernel/arch/x86/vga_modo3.c`, la syscall `SYS_MODO_TESTO` e
> il comando `/bin/testo`, che si digita alla cieca. Sta nel kernel perché è
> l'unico che c'è sempre — una delle pochissime eccezioni ammesse dalla
> direttiva 1, per la stessa ragione per cui c'è l'ATA.

Il server grafico non deve tenere niente che al kernel serva. Un supervisore
deve poterlo far ripartire.

### 3. Il pagefile è un vincolo DA SUBITO, anche se si implementa dopo

Oggi non c'è swap: quando la RAM finisce, finisce. Ci sarà, perché le
applicazioni che vogliamo far girare superano la memoria disponibile.

! **Il punto è che lo swap non si aggiunge in fondo.** Ogni pagina che un
servizio inchioda in memoria, ogni buffer che un driver tiene per sempre, ogni
struttura che il kernel non sa spostare, è una decisione che un pagefile
futuro **non può disfare**. Se si scrive per due anni senza distinguere la
memoria che può muoversi da quella che non può, allo swap non resta niente da
scambiare.

Quindi, da adesso, ogni allocazione nuova risponde a una domanda: **questa
pagina si può portare su disco mentre il proprietario non guarda?**

- **No, e va dichiarato**: i buffer di un bus master (un dispositivo che scrive
  a indirizzi fisici non passa dalla MMU: se la pagina si sposta, scrive
  addosso a qualcun altro — è esattamente il motivo per cui `SYS_DMA_ALLOC`
  esiste e non si può liberare), le tabelle delle pagine, gli stack del kernel,
  il codice che gestisce il fault di pagina.
- **Sì**: quasi tutto il resto, e in particolare i buffer delle finestre, che
  saranno la cosa più grossa in memoria e la più facile da riportare da disco.

! **La memoria condivisa fra processi è il caso difficile**, e va progettata
adesso perché la si sta per scrivere: una pagina condivisa fra il server
grafico e un'applicazione ha **due** proprietari, e portarla su disco richiede
di sapere che entrambi non la stanno guardando. Se nasce senza un conteggio dei
riferimenti e senza un modo di bloccarla temporaneamente, lo swap dovrà
saltarla per sempre.

### 4. Le primitive che abilitano tutto il resto

Nessuna riguarda la grafica, e senza di loro qualunque lavoro grafico poggia
sul vuoto.

| | primitiva | perché | vincoli dalle direttive 2 e 3 |
|---|---|---|---|
| 1 | **mappare il framebuffer in spazio utente** | `SYS_MMIO_MAP`, 13 agosto 2026: mappa una finestra fisica sopra la RAM, non cacheabile. `svga.drv` sceglie il modo, non disegna | il server è ring 3; la mappatura si revoca quando muore |
| 2 | **FATTA** — memoria condivisa fra processi | `SYS_SHM_APRI`/`SYS_SHM_CHIUDI`, 13 agosto 2026: zone nominate, il conteggio dei riferimenti è **per pagina fisica** nel PMM | il conteggio c'è; il blocco temporaneo si aggiunge quando ci sarà uno swapper, vedi sotto |
| 3 | **FATTA** — attesa su più sorgenti (`poll`/`select`) | `SYS_POLL`, 13 agosto 2026: descrittori **e** mailbox IPC nella stessa chiamata | provato che il processo dorme davvero: mentre aspetta è `BLOCKED` |
| 4 | **thread reali** | è **l'unico** cancello verso Qt e GTK: `GMainLoop`, `GMutex` e l'affinità degli oggetti Qt stanno nel modello, ci passa anche un'applicazione a thread singolo | il `cwd` nel PCB (12 agosto) era il primo gradino |

> **FATTA il 13 agosto 2026: `SYS_MMIO_MAP` (241).** E non è servita alla
> grafica: è servita a una scheda di rete. Il driver per l'Intel e1000 — la
> predefinita di QEMU — si era fermato esattamente qui, perché i suoi
> registri stanno in memoria (BAR0) e da ring 3 non ci si arrivava.
> L'alternativa documentata da Intel, la finestra a porte della BAR1, **QEMU
> non la implementa**: `e1000_io_read` rende 0 ed `e1000_io_write` scarta
> tutto (verificato in `hw/net/e1000.c`).
>
> Con la syscall il driver gira: `STATUS 0x80080783`, link su, MAC dalla
> EEPROM, DHCP, `ping 10.0.2.2` 4/4 senza perdite. **In ring 3.**
>
> Resta vero il punto generale: quella syscall è ciò che separa EX-OS da
> **ogni** dispositivo moderno, perché da vent'anni i registri non stanno più
> nello spazio I/O.
>
> **E IL VARCO È STATO RIFATTO lo stesso giorno**, perché era sbagliato per
> il caso dopo. Chiedeva che il processo avesse già delle porte I/O, e non
> teneva da nessuno dei due lati: non teneva **chiuso**, perché `ioport_bind`
> le dava a chiunque le chiedesse senza controllare niente — due righe e un
> programma qualunque era «un driver»; e non teneva **aperto**, perché un
> framebuffer porte I/O non ne ha nessuna.
>
> Adesso il varco è il flag `is_driver` del PCB, che mette il **caricatore**
> guardando il nome dell'eseguibile (`*.drv`) e che nessuna syscall concede.
> Ci passano `ioport_bind`, `dma_alloc` e `mmio_map`.
>
> ! **E non è una barriera, il che va detto adesso e non scoperto dopo.**
> Senza proprietari dei file un programma può copiarsi in `x.drv` e ripartire
> da lì — provato, funziona. È la *definizione* di cosa sia un driver, fatta
> dal kernel su un fatto fissato prima che il programma parta, invece che su
> ciò che il programma dichiara di sé mentre gira. La barriera vera vuole un
> concetto di proprietario, e arriverà con quello.

> **FATTA il 13 agosto 2026: `SYS_SHM_APRI` (242) e `SYS_SHM_CHIUDI` (243).**
> Zone di memoria condivisa aperte **per nome**, come i servizi IPC, perché un
> identificatore numerico avrebbe richiesto un modo di passarselo — cioè
> l'IPC, cioè la cosa che si sta cercando di non usare.
>
> ! IL CONTEGGIO DEI RIFERIMENTI È PER PAGINA FISICA E STA NEL PMM, e questa è
> la decisione che conta. Lì copre **ogni** strada che libera una pagina —
> `munmap`, la morte del processo, un errore a metà di `elf_load` — senza che
> nessuna di quelle sappia che la condivisione esiste. Tenuto più in alto
> avrebbe richiesto di correggere ogni sito di liberazione, e ne basta uno
> dimenticato perché il guasto sia una pagina riusata mentre qualcuno ci
> scrive dentro. La nota che chiedeva esattamente questo era in
> `kernel/mm/paging.c` da mesi.
>
> ! IL BLOCCO TEMPORANEO PER IL PAGEFILE NON C'È, ed è una scelta, non una
> dimenticanza. Ciò che la direttiva 3 dice di non poter aggiungere dopo è
> *sapere che una pagina ha più di un proprietario*, e quello c'è. Un
> contatore di blocco che nessuno consulta sarebbe codice morto oggi e non
> renderebbe più facile lo swap domani: quando ci sarà uno swapper si aggiunge
> un campo a `ShmZonaK` e le due chiamate che lo muovono.
>
> ! LA ZONA MUORE CON L'ULTIMO CHE LA TIENE APERTA. Non c'è `shm_unlink`: su
> un sistema dove un processo che muore non lascia niente dietro, una zona che
> sopravvive a tutti i suoi utenti è memoria che nessuno libererà mai più.
> Conseguenza da sapere: non si può creare una zona, riempirla e uscire perché
> qualcun altro la trovi dopo.

> **FATTA il 13 agosto 2026: il ritorno al modo testo senza BIOS.**
> `SYS_MODO_TESTO` (245) e `/bin/testo`. Con questa **il gradino 0 è chiuso**.
>
> ! STA NEL KERNEL, ed è una delle pochissime eccezioni alla direttiva 1, per
> la stessa ragione per cui c'è l'ATA: deve funzionare **quando il server è
> morto**, e un processo che risponde solo finché un altro processo è vivo non
> è una rete di sicurezza.
>
> ! IL CARATTERE E LA TAVOLOZZA VANNO RICARICATI, ed è il pezzo che si
> dimentica. In modo testo i disegni dei caratteri non stanno nella ROM:
> stanno nel piano 2 della memoria video, e una modalità grafica ci scrive
> sopra i propri pixel. Rimettere solo i registri dà 80x25 di simboli casuali
> — uno schermo che risponde e non si legge, che è quasi peggio di uno spento.
>
> ! LA PROVA NON È «LA RISOLUZIONE È TORNATA». Sarebbe tornata anche con il
> carattere distrutto. La prova è che lo schermo dopo il ripristino è
> **identico pixel per pixel** a uno schermo di testo buono — 18 pixel di
> differenza su 288000, che sono il cursore. E vale per entrambe le strade:
> da un modo grafico VGA e da una VESA lineare 800x600 si arriva allo stesso
> identico schermo.
>
> ! IL LIMITE, DICHIARATO: su schede Bochs/QEMU/VirtualBox la VESA si spegne
> dalla finestra a porte 0x1CE/0x1CF, e questo codice lo fa. Su ferro vero
> l'equivalente è l'interfaccia in modo protetto di VBE 2.0, che EX-OS non ha:
> lì i registri VGA da soli possono non bastare.

> **FATTA il 13 agosto 2026: `SYS_POLL` (244), più `select()` nella libc.**
> Descrittori e mailbox IPC nella stessa attesa.
>
> ! LA MAILBOX SI NOMINA CON `FD_IPC`, E NON DIVENTA UN DESCRITTORE. Su questo
> sistema metà degli eventi non passa dai file: driver, servizi e notifiche di
> IRQ arrivano tutti nella mailbox. Un poll che guardasse solo i descrittori
> lascerebbe fuori proprio le sorgenti per cui serve. Farne un fd vero sarebbe
> stato più elegante da fuori e falso da dentro — non si apre, non si chiude,
> non si eredita, non si duplica.
>
> ! `select()` STA NELLA LIBC, SOPRA `poll()`. Nel kernel l'attesa su più
> sorgenti è una sola: scritta due volte, la race del risveglio perduto la si
> sbaglia due volte, e la seconda si scopre mesi dopo.
>
> ! LA PROVA CHE CONTA NON È CHE `poll()` TORNI. Un'attesa attiva darebbe gli
> stessi identici risultati su ogni altra prova — stessi `revents`, stessi
> valori di ritorno — solo con la CPU bruciata. Perciò `polltest` fa leggere
> al **figlio** lo stato del padre mentre questo aspetta, e glielo manda
> dentro il messaggio che lo sveglia: `BLOCKED`.

---

## L'ordine, e perché è quello

Ogni gradino è utile da solo, anche fermandosi lì.

| | cosa | sblocca |
|---|---|---|
| **0** | primitive 1, 2, 3 + **rimettere il modo testo da modo protetto** | tutto il resto, e la possibilità di sopravvivere a un server grafico morto |
| **1** | server a finestre in ring 3 + toolkit proprio + applicazioni | il sistema a finestre: spostare, ridimensionare, ridurre a icona, più applicazioni insieme |
| **2** | strato **SDL 1.2** | il software di terzi, a un decimo del costo di GTK: vuole un framebuffer ed eventi, **nessun thread obbligatorio**, nessun glib |
| **3** | **pagefile** | applicazioni più grandi della RAM. Va dopo il server perché i buffer delle finestre sono il primo carico che lo giustifica — ma i suoi vincoli valgono da adesso (direttiva 3) |
| **4** | **thread reali nel kernel** | l'unica porta verso Qt e GTK |
| **5** | server X11, poi Qt | il porting vero |

! **Sul gradino 5: il protocollo non è il costo.** Un server X perfetto da
solo non fa girare niente. GTK3 vuole glib, gobject, gio, cairo, pango,
harfbuzz, freetype, fontconfig, pixman, gdk-pixbuf; Qt5 vuole il runtime C++
completo con eccezioni e RTTI più le proprie copie di freetype, harfbuzz e
pcre2. È lì che sta il lavoro, ed è il porting di GCC moltiplicato più volte.

**Fra i due, Qt è il bersaglio più realistico di GTK**: con QPA si scrive *un*
plugin di piattaforma e Qt si porta dietro le proprie dipendenze, mentre GTK le
ha sparse in una decina di librerie separate. Ma resta dopo il gradino 4.

---

## Cosa cambia per chi scrive codice domani

Quattro domande, da farsi **prima**:

1. **Può essere un processo ring 3?** Se sì, lo è. Se no, il perché va scritto
   nel file, come è scritto in `ata.c`.
2. **Questa memoria si può portare su disco?** Se no, va dichiarato e
   motivato. Se è condivisa, ha un conteggio dei riferimenti?
3. **Se questo muore, cosa resta rotto?** La risposta buona è «solo lui».
4. **Aspetta su una sorgente sola?** Se sì, prima o poi dovrà aspettarne
   diverse: meglio saperlo adesso.

---

## Da dove si comincia, quando si comincerà

Gradino 0, tutto fatto il 13 agosto 2026:

1. ~~`SYS_FB_MAP` — mappa la LFB nello spazio del chiamante.~~ **Fatta il 13
   agosto 2026, e non si chiama così**: `SYS_MMIO_MAP` mappa una finestra
   fisica qualunque purché stia sopra la RAM, quindi la LFB è un caso di
   quella e non ha bisogno di una syscall sua. Il varco d'accesso, che il
   primo giorno era sbagliato proprio per il framebuffer, è stato rifatto lo
   stesso giorno (vedi sopra).
2. ~~**Memoria condivisa** — due processi, le stesse pagine fisiche, con
   conteggio dei riferimenti (direttiva 3).~~ **Fatta il 13 agosto 2026**:
   `SYS_SHM_APRI`/`SYS_SHM_CHIUDI`, con il conteggio per pagina fisica dentro
   il PMM. Vedi sopra.
3. ~~**`poll`/`select`** su descrittori e mailbox IPC insieme.~~ **Fatta il
   13 agosto 2026**: `SYS_POLL`, con `FD_IPC` per la mailbox e `select()`
   costruita sopra dentro la libc. Vedi sopra.
4. ~~**Ritorno al modo testo senza BIOS** — la rete di sicurezza di tutto il
   resto.~~ **Fatta il 13 agosto 2026**: `SYS_MODO_TESTO` e `/bin/testo`,
   che si digita alla cieca. Vedi sopra.

**Il gradino 0 è chiuso.** Il prossimo è il gradino 1: il server a finestre in
ring 3.

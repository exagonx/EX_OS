# EX-OS — Extensible Operating System

**Autore:** Graziano Falcone <exagonx@hotmail.com>
**Licenza:** GNU General Public License v2 (GPL-2.0)
**Architettura:** x86 32-bit, floppy FAT12 1.44MB

---

## Che cos'è EX-OS

EX-OS è un sistema operativo baremetal scritto in C e ASM per architettura
x86 32-bit. Gira da floppy da 1.44MB formattato FAT12, da disco rigido
(FAT16/32 o ext2) e legge CD/DVD. L'obiettivo è un sistema estensibile: il
kernel è piccolo e read-only in RAM convenzionale, tutto il resto (driver,
shell, programmi) gira in RAM estesa in spazio protetto.

Un crash di un driver o di un programma non può abbattere il sistema.

**Da agosto 2026 EX-OS ospita codice di terzi**: GNU binutils 2.44 —
`as` e `ld` — è compilato *per* EX-OS e ci gira dentro, e un programma
assemblato e collegato qui è identico byte per byte a uno prodotto dal
cross-compilatore su Linux. Vedi
[La catena di compilazione dentro EX-OS](#la-catena-di-compilazione-dentro-ex-os).

---

## Struttura floppy

```
/
├── LOADER.BIN       ← Stage 2: trova e carica il kernel via FAT12
├── KERNEL.BIN       ← EX-OS Kernel
├── boot/
│   └── kernel.cfg   ← Configurazione: env, shell, moduli
├── bin/
│   ├── sh           ← Shell (ELF statico, primo processo)
│   ├── ls           ← Elenco directory
│   ├── hello        ← Programma di esempio
│   ├── textline     ← Editor di testo lineare (stile edlin)
│   ├── gfedit       ← Editor a schermo intero (stile MS-DOS EDIT)
│   ├── mkdir        ← Crea directory
│   ├── rmdir        ← Cancella directory vuote
│   └── delete       ← Cancella file (con jolly ? e *)
├── lib/             ← Shared libraries (Fase 4b)
└── dev/
    ├── kbd.drv      ← Driver tastiera PS/2 (processo ring3)
    └── floppy.drv   ← Driver floppy controller (ancora ET_DYN, non caricato)
```

Il TTY non compare in `/dev`: `drivers/tty/tty.c` è compilato **dentro** il
kernel (possiede la VGA), e per l'input fa da client del servizio `kbd`.

Quello che in 1.44 MB non entra sta sul **CD degli strumenti**
(`make iso`): `as` e `ld` nativi in `/bin`, gli header e il sorgente della
libc in `/exos`, la documentazione in `/doc`.

---

## Prerequisiti (Debian 12)

```bash
# 1. Installa il cross-compiler (tutto automatico, ~30 min):
chmod +x tools/install_crosscompiler.sh
./tools/install_crosscompiler.sh

# 2. Attiva nella sessione corrente:
export PATH="$HOME/opt/cross/bin:$PATH"

# 3. Verifica:
i686-elf-gcc --version   # deve stampare: i686-elf-gcc 13.2.0 ...
nasm --version           # nasm version 2.x
mformat --version        # Mtools version ...
```

Lo script `install_crosscompiler.sh` installa automaticamente:
- Dipendenze Debian (`build-essential`, `nasm`, `mtools`, `qemu-system-i386`, ecc.)
- `binutils 2.41` cross-compilato per target `i686-elf`
- `GCC 13.2.0` cross-compilato per target `i686-elf` (solo linguaggio C)
- Aggiunge `~/opt/cross/bin` al `~/.bashrc`

---

## Build e test

```bash
make all          # Compila tutto + crea dist/floppy.img
make run          # Avvia con QEMU (32MB RAM)
make iso          # CD degli strumenti (as, ld, header, doc)
make run-iso      # QEMU con il CD montato su /cdrom
make hd           # Disco rigido avviabile (formattato da EX-OS stesso)
make run-hd       # QEMU dal disco, senza floppy
make debug        # QEMU + GDB stub porta 1234
make verify       # Verifica struttura floppy
make clean        # Rimuove build/
make distclean    # Rimuove build/ e dist/
```

`make iso` include `as` e `ld` nativi se li trova in
`$(BINUTILS_NATIVI)` (default `~/exos-native/build-nativi`); se non ci
sono lo dice e fa il CD lo stesso. Come costruirli:
`tools/binutils-exos/leggimi.md`.

### Log di boot completo via seriale

Il kernel specchia su COM1 (38400 8N1) tutto l'output che passa da
`vga_putchar()`: `kprintf`, `klog`, eco tastiera e output dei processi. Serve
perché lo schermo VGA è 80x25 e i messaggi di boot scorrono via.

```bash
qemu-system-i386 -drive file=dist/floppy.img,format=raw,if=floppy \
  -m 32M -boot a -display none -serial file:/tmp/serial.txt -no-reboot
```

### Scrivere l'immagine su un floppy fisico (da WSL)

Tre modi, stesso motore:

```bash
# da WSL
./tools/write_floppy.sh              # dist/floppy.img su A:
./tools/write_floppy.sh -d B: -y
```

```powershell
# da PowerShell
.\tools\write_floppy.ps1
.\tools\write_floppy.ps1 -Drive B: -Yes
```

```bat
REM da cmd.exe, o doppio clic da Esplora risorse
tools\write-floppy.cmd
tools\write-floppy.cmd B:
```

Non serve una console come Amministratore: lo script si rieleva da solo (UAC)
in una finestra che resta aperta per mostrare l'esito.

WSL2 non vede i dischi fisici di Windows, quindi `dd` non serve: si passa da
PowerShell, che blocca e smonta il volume, scrive il volume grezzo e
**rilegge per verificare** byte per byte. Rifiuta le unità non rimovibili
salvo `-Force`.

> ⚠️ **Floppy USB**: il bootloader usa il BIOS (INT 13h) e funziona, ma il
> kernel accede al controller floppy direttamente (porte 0x3F0-0x3F7, DMA,
> IRQ6). Un floppy USB non è collegato a quel controller: il kernel parte ma
> non riesce a leggere `/bin/sh`. Serve un drive floppy interno — vedi
> `HANDOFF.md` per le alternative.

**Nota toolchain**: il `Makefile` usa `gcc -m32` nativo, non il cross-compiler
`i686-elf-*`. Lo script `tools/install_crosscompiler.sh` resta disponibile, ma
la build non lo richiede (serve però `gcc-multilib`).

---

## Architettura kernel

```
RAM CONVENZIONALE < 1MB — Kernel EX-OS (read-only, piccolo)
  GDT | IDT | ISR | VGA | PMM | Paging | Heap | Scheduler | Syscall

RAM ESTESA > 1MB — Tutto il resto (protetto, isolato)
  Driver ELF (/dev/)  — crash isolato, non tocca il kernel
  Processi (/bin/)    — spazi di indirizzamento separati
  Librerie (/lib/)    — shared, mappate in ogni processo
```

### Le pagine di un programma arrivano quando servono

Dalla 0.149 il caricatore ELF non copia i segmenti in RAM: annota dove
vivono nel file, tiene l'eseguibile aperto e le pagine arrivano al primo
accesso, dal gestore di page fault. Un binario con **8 MB di dati
costanti** parte occupando 36 KB e sale a 8 MB solo se lo si legge tutto.

Il costo di avvio non dipende piu' dalla dimensione del binario — che e'
la condizione per far girare qui dentro un compilatore, dove `cc1` da solo
sono decine di MB.

I **driver** fanno eccezione e si caricano tutti in RAM: un driver che
serve il filesystem, paginato da quel filesystem, dovrebbe servire la
propria lettura mentre e' fermo ad aspettarla.

Conseguenza da sapere: **l'eseguibile resta aperto finche' il processo
vive**, e le pagine caricate non vengono mai buttate via (manca lo
sfratto, e con esso il file di scambio).

### La fascia kernel, e la finestra di rimappatura

La page directory di un processo copia dalla PD del kernel solo le PDE
**sotto `USER_SPACE_BASE` (64 MB)**. Quella fascia è l'unica memoria che il
kernel può rileggere al proprio indirizzo fisico mentre gira un processo,
ed è dove il PMM è obbligato a mettere ciò che il kernel indirizza così:
heap di `kmalloc`, stack kernel dei processi, page directory e page table,
immagini dei driver in corso di rilocazione (`pmm_alloc_page_kernel()`).

Le pagine dei processi invece stanno **ovunque in RAM**: il kernel le tocca
attraverso una pagina virtuale che ripunta al volo alla pagina fisica che
gli serve — `paging_finestra_apri()`, il `kmap_atomic` dei kernel grandi
ridotto all'osso. Senza, un processo poteva crescere solo finché il PMM
consegnava pagine sotto la soglia: **4 MB, e poi kernel panic**, con
qualunque quantità di RAM installata. Ora un processo arriva a occupare la
RAM disponibile (provato: 300 MB su una macchina da 512).

---

## Syscall interface (int 0x80, stile Linux)

| EAX | Syscall      | EBX        | ECX       | EDX     |
|-----|--------------|------------|-----------|---------|
|   1 | exit         | status     | —         | —       |
|   3 | read         | fd         | buf*      | count   |
|   4 | write        | fd         | buf*      | count   |
|   5 | open         | path*      | flags     | mode    |
|   6 | close        | fd         | —         | —       |
|  11 | exec         | path*      | argv**    | envp**  |
|  41 | dup          | fd         | —         | —       |
|  63 | dup2         | vecchio    | nuovo     | —       |
|  55 | fcntl        | fd         | cmd       | arg     |
|  20 | getpid       | —          | —         | —       |
|  45 | sbrk         | increment  | —         | —       |
|  54 | ioctl        | fd         | request   | arg     |
|  90 | mmap         | params*    | —         | —       |
|  91 | munmap       | addr       | length    | —       |
| 158 | sched_yield  | —          | —         | —       |
| 162 | sleep        | ms         | —         | —       |
| 183 | getcwd       | buf*       | size      | —       |
| 184 | getenv       | key*       | buf*      | size    |
|  39 | mkdir        | path*      | —         | —       |
|  40 | rmdir        | path*      | —         | —       |
|  10 | unlink       | path*      | —         | —       |
| 185 | version      | buf*       | size      | —       |
|  88 | reboot       | cmd        | —         | —       |

`getenv` legge la configurazione di `/boot/kernel.cfg`: sia le variabili di
`[env]` sia le opzioni scalari fuori da `[env]` come `verboseboot`. È il modo
in cui un processo utente accede alla configurazione senza rileggersi il file.

`version` copia `g_os_version`, la variabile globale del kernel con nome,
copyright, licenza e versione del sistema (`kernel/version.c`). La shell la
espone con i comandi `ver` e `version`.

Wrapper libc: `getconf()`, `osversion()`, `verboseboot()`.

`reboot` spegne, riavvia o ferma il sistema (`cmd` = 0 poweroff, 1 restart,
2 halt). Sincronizza sempre il filesystem e ferma lo scheduler prima di
agire — vedi sotto.

---

## /bin/mkdir e /bin/rmdir

```
mkdir <nome> [nome2 ...]    crea una o piu' directory
rmdir <nome> [nome2 ...]    cancella una o piu' directory VUOTE
```

Accettano percorsi assoluti o relativi alla directory corrente. **Solo
directory nella root**: il driver FAT12 risolve i percorsi a un livello, quindi
una directory annidata sarebbe corretta sul supporto ma irraggiungibile —
entrambi rifiutano con un messaggio esplicito invece di creare o cancellare
qualcosa di inutilizzabile.

`rmdir` rifiuta le directory non vuote, e non è una limitazione temporanea:
senza cancellazione ricorsiva i file rimasti dentro diventerebbero
irraggiungibili e i loro cluster resterebbero occupati per sempre. La root è
protetta, e `rmdir` su un file viene rifiutato.

---

## /bin/delete

```
delete <modello> [modello2 ...]

  ?   un carattere qualsiasi
  *   una sequenza qualsiasi di caratteri
```

```
delete nota.txt        un file preciso
delete *               tutto il contenuto della directory corrente
delete /temp/tmp*      i file che iniziano per "tmp" dentro /temp
delete dati?.log       DATI1.LOG, DATI2.LOG, ...
delete *.txt           per estensione
```

L'espansione dei jolly la fa il programma, non il kernel né la shell — come in
MS-DOS. Il confronto è insensibile al caso (FAT12 conserva i nomi in
maiuscolo). Le directory vengono saltate: per quelle c'è `rmdir`.

`delete` lavora in due fasi: prima raccoglie tutti i nomi corrispondenti
percorrendo l'intera directory, poi cancella. Non si può cancellare mentre si
elenca — le entry liberate vengono saltate da `readdir` e le voci successive
scalerebbero, facendo perdere file a ogni blocco.

`delete *` chiede conferma quando i file sono più di uno, dicendo quanti e da
dove. Un modello mirato come `tmp*` non la chiede.

---

## Job control: `&`, `jobs`, `fg`

```
comando &     esegue in background e torna subito al prompt
jobs          elenca i job ancora in esecuzione
fg [n]        riporta in primo piano il job n (l'ultimo se omesso)
```

Un job terminato viene annunciato al prompt successivo — `[1] terminato: tsleep
(codice 0)` — ed è anche il momento in cui il suo slot di processo viene
liberato: un figlio resta `ZOMBIE` finché il padre non lo raccoglie (il reaper di
init si occupa solo degli orfani).

**Non c'è `bg`**, e non è una dimenticanza: `bg` riprende un processo *sospeso*, e
per sospenderlo servirebbe un Ctrl+Z — cioè i segnali, che EX-OS non ha. Un job
qui o gira o è finito, non esiste lo stato in mezzo.

**L'output si mescola.** Un job in background scrive sulla stessa console della
shell, quindi le sue righe finiscono in mezzo al prompt e a ciò che stai
digitando. È il comportamento di qualunque shell Unix; se il programma ha bisogno
dello schermo tutto per sé, si lancia su un'altra console con Alt+Fn invece che
con `&`.

**L'input invece è protetto**, e serviva davvero. Il driver tastiera serve
l'*ultimo* che ha chiesto una riga: senza difese, un job in background che legge
`stdin` sostituirebbe la shell come lettore e il prompt non riceverebbe mai più
un comando — la console morirebbe. Due meccanismi lo impediscono:

1. `sys_read` su `stdin` restituisce la **fine dell'input** a chi non è il
   processo in primo piano della propria console. La shell dichiara il primo
   piano con `SYS_CONSOLE_SETFG` (sé stessa al prompt, il figlio quando lo
   aspetta). Unix qui userebbe `SIGTTIN`; senza segnali, l'EOF è l'unica risposta
   possibile — ed è comunque vera, quel programma input non ne avrà mai.
2. Chi prende la tastiera parlando **direttamente** al servizio `kbd` via IPC —
   la modalità raw di `gfedit` — non passa da `sys_read`, quindi controlla da sé
   `ConsoleInfo.fg` e si rifiuta di partire in background, spiegando perché.

---

## Console virtuali — Alt+F1 … Alt+F4

Quattro schermi indipendenti, uno solo visibile per volta, ognuno con la propria
shell avviata al boot. **Alt+F1..F4** commuta: il programma che stava girando
non viene sospeso né chiuso, continua a lavorare e a disegnare nel proprio
buffer, e si ritrova lo schermo intatto quando ci si torna sopra.

È la risposta alla domanda "come lancio un'altra cosa senza chiudere questa":
apri `gfedit` sulla console 2, premi Alt+F3, hai un prompt pulito, e Alt+F2 ti
riporta all'editor esattamente dove l'avevi lasciato.

| | |
|---|---|
| Console 0 (Alt+F1) | è anche la console di **sistema**: i messaggi del kernel (`klog`) escono qui, accanto al prompt. Le altre restano pulite. |
| Ereditarietà | un programma nasce sulla console del padre (`sys_spawn`), quindi resta dove è stato lanciato |
| Tastiera | i tasti vanno **solo** alla console in primo piano; le shell delle altre restano ferme al proprio prompt con la richiesta di lettura pendente |
| Modalità raw | è **per console**: mentre gfedit tiene la 2 in raw, la shell della 1 continua a ricevere righe intere con eco e Backspace |

Alt+Fn è intercettato dal driver tastiera **prima** di qualunque altra
elaborazione e non viene consegnato a nessuno: è un comando all'interfaccia, non
input per il programma in esecuzione. Senza quella precedenza basterebbe un
editor che usa Alt+F per il menu File per rendere impossibile cambiare schermo —
cioè proprio nel caso in cui serve di più.

Costo: 4 KB di BSS del kernel per console (il buffer di schermo) più un processo
shell da ~14 KB. Il numero è `VGA_N_CONSOLE` in `kernel/include/vga.h`, e deve
restare uguale a `KBD_N_CONSOLE` in `drivers/kbd/kbd_proto.h`.

---

## Data e ora

`time_now()` legge l'orologio CMOS della macchina (MC146818, porte 0x70/0x71) e
restituisce data e ora vere — quelle che l'orologio a batteria continua a contare
a macchina spenta. Da non confondere con `uptime_ms()`, che misura *durate* e non
sa che ora sia.

Ritorna `-ENODEV` se l'orologio non risponde o consegna una data impossibile
(succede su hardware vecchio con la batteria del CMOS scarica): in quel caso il
chiamante deve dire "ora ignota" invece di mostrare un orario inventato — gfedit
scrive `--:--:--`.

⚠️ In QEMU il RTC parte in **UTC**, non in ora locale. Per vedere l'ora del fuso
serve `-rtc base=localtime` fra i `QEMU_FLAGS` del Makefile.

---

## /bin/textline — editor di testo lineare

Modello edlin: si opera per numero di riga, non con un cursore. Resta il modo
più rapido di correggere una riga sola, e l'unico che funziona anche quando
`/dev/kbd.drv` non è disponibile e la console è servita dalla tastiera
in-kernel di ripiego.

```
textline <file>              apre il file per l'editing
textline <file> -v           visualizza il contenuto
textline <file> -vp          visualizza a pagine
textline <file> -c:<file2>   copia <file> in <file2>
```

Comandi: `h`/`help`, `l`, `lNN`, `lNN,MM`, `lp…` (a pagine), `m`, `mNN`, `n`,
`dNN`, `cNN,MM`, `w` (salva), `e` (salva ed esce), `q` (esce). ESC annulla la
riga in inserimento e riporta al prompt.

---

## /bin/gfedit — editor a schermo intero

Riscrittura per EX-OS di **GF_TEXTEDITOR**, l'editor ncurses+pthread dello
stesso autore (sorgenti originali in `gftexteditor/`). Non è un porting: di
ncurses, dei thread, di stdio POSIX e di una `free()` vera EX-OS non ha
niente. Quello che resta uguale è il programma — menu a tendina in stile
MS-DOS EDIT, otto aree aperte insieme, find/replace, annullamento,
evidenziazione sintattica.

```
gfedit                apre un'area vuota
gfedit <file> [...]   apre fino a 8 file
gfedit -h             elenco delle scorciatoie
```

| | |
|---|---|
| Movimento | frecce, Home/Fine, Ctrl+Home/Fine, PagSu/PagGiu, Ctrl+G (vai a riga) |
| Selezione | Shift+movimento, Ctrl+A, ESC per abbandonarla |
| Modifica | Ins, Ctrl+Z, Ctrl+X/C/V |
| File | Ctrl+N, Ctrl+O, F2 o Ctrl+S, Ctrl+W, Alt+X |
| Ricerca | Ctrl+F, F3, Shift+F3, Ctrl+H |
| Aree | F6, Shift+F6, Alt+1…Alt+8 |
| Menu | F10 o ESC, oppure Alt+F M C O A |

La barra di stato mostra l'ora del giorno vera, che **avanza da sola** anche a
tastiera ferma: il ciclo principale non aspetta più un tasto all'infinito ma si
risveglia ogni mezzo secondo (`ipc_recv_timeout`). Fra un risveglio e l'altro il
processo è `BLOCKED` e non consuma un tick di CPU.

Linguaggi evidenziati: C, C++, BASIC, assembly, riconosciuti dall'estensione e
cambiabili da *Opzioni → Linguaggio*.

**Limiti, e perché sono lì.** 512 righe per file, 200 caratteri per riga, 8
aree. Le righe sono slot a lunghezza fissa perché la `free()` di EX-OS è un
no-op dichiarato (allocatore a bump su `sbrk`): con stringhe riallocate ogni
tasto premuto perderebbe per sempre la memoria della riga precedente. Un file
più grande dei limiti viene caricato **in parte**, la barra di stato lo dice, e
il salvataggio su quel file resta bloccato — per non cancellare la parte mai
letta. *Salva con nome* su un file diverso è invece permesso.

**Serve `/dev/kbd.drv`.** Un editor a schermo intero ha bisogno dei tasti uno
per uno, e la modalità raw vive nel driver tastiera. Senza quel servizio
gfedit non parte e rimanda a textline, invece di mostrare un'interfaccia che
non risponderebbe.

---

## Inizializzare un disco rigido: /bin/fdisk e /bin/mkfs

Il ciclo completo, da disco vergine a sistema avviabile:

```
disk                        cosa vede il sistema
fdisk hd0                   crea le partizioni, marca attiva la prima
mkfs -t ext2 -L exos hd0p1  ci scrive dentro un filesystem
mount hd0p1 /disk           montalo
install /disk               rendilo avviabile
```

Poi si toglie il floppy e si riavvia. Funziona sia su **FAT16/FAT32** sia su
**ext2**.

### La mappa dei settori, e perché il kernel ne ha una lista

Il settore di avvio non sa leggere alcun filesystem: in 512 byte, tolti BPB e
firma, non ci sta. Riceve LBA e lunghezza di Stage 2 e del kernel, e legge
settori. È `install` — che gira *dentro* EX-OS, dove i driver ci sono già — a
comporre quella mappa.

⚠️ Il prezzo è lo stesso patto di LILO: **ricopiare kernel o Stage 2 obbliga a
rilanciare `install`.**

### `install` sostituisce, e verifica (dal 0.148)

Rilanciarlo su un sistema già installato **riscrive ogni file**, kernel
compreso, e rilegge ognuno per confrontarne la dimensione. Nel resoconto `+`
è creato, `~` sostituito, `!` errore.

Fino alla 0.147 i file già presenti venivano saltati: un aggiornamento
copiava solo i file nuovi, lasciava il kernel vecchio e poi riscriveva la
mappa dei settori *per quello* — il disco ripartiva con la versione di
prima e l'installatore diceva «completata». Le directory continuano a non
essere ricreate, e ciò che sul volume non fa parte del sistema resta dov'è:
`install` aggiorna, non azzera.

Per il kernel la mappa è una **lista** di intervalli, non uno solo. Su ext2 un
file non è quasi mai contiguo, e non per frammentazione: il blocco di
*puntatori* viene allocato in mezzo ai dati, perché serve prima del tredicesimo
blocco. Un kernel da 147 KB appena copiato sta così:

```
(0-11):74-85, (IND):86, (12-144):87-219
```

Stage 2 invece resta un intervallo solo: sta in ~1 KB, cioè dentro i 12 blocchi
diretti, dove nessun indiretto si è ancora infilato — ed è il pezzo che va
trovato da 512 byte di codice, quindi la sua mappa deve stare in sei byte.

Su FAT la lista ha una voce sola: il formato è lo stesso per i due filesystem,
così Stage 2 non deve sapere da dove sta caricando.

### I nomi si creano in minuscolo

Il kernel cerca `/bin/sh`, `/boot/kernel.cfg`, `/dev/kbd.drv`. Su FAT il caso
non conta — il driver mette in maiuscolo sia ciò che scrive sia ciò che cerca —
ma su **ext2 `BIN` e `bin` sono due directory diverse**, e un sistema installato
in `BIN` non troverebbe la propria shell. `install` crea tutto in minuscolo,
nomi dei file compresi.

### /bin/fdisk — partizionatore MBR

```
fdisk            elenca i dischi
fdisk hd0        apre la sessione sul disco 0

  p  mostra tabella e spazio libero    a  commuta il flag avviabile
  n  crea una partizione               w  SCRIVE (chiede conferma)
  d  cancella una partizione           q  esce senza scrivere
  t  cambia il tipo
```

**Niente tocca il disco fino a `w`.** Una tabella scritta un pezzo alla volta
passa per stati in cui le partizioni si sovrappongono; se la macchina si
spegne lì in mezzo, resta sbagliata.

È un programma separato da `disk`, che resta in **sola lettura**: è il comando
che si lancia senza pensarci su un disco a cui si tiene, e un programma che a
seconda degli argomenti guarda *oppure* riscrive perde quella garanzia per
tutti gli usi.

Le **politiche** stanno in `fdisk` (allineamento a 1 MiB, primo settore utile
2048, valori predefiniti). Le **regole** stanno nel kernel e non si aggirano:
niente sovrapposizioni, niente partizioni oltre la fine del disco, niente
scrittura su un disco GPT o su una partizione montata.

Non gestisce le partizioni **logiche** e non scrive EBR: le mostra, ma la voce
estesa che le contiene è bloccata — spostarla lascerebbe la loro catena viva
sul disco e irraggiungibile.

`fdisk` non formatta. Una partizione appena creata contiene i byte che c'erano
prima in quei settori: non è vuota, è non inizializzata.

### /bin/mkfs — formattatore FAT16/FAT32/ext2

```
mkfs -t fat32 -L ETICHETTA hd0p1
mkfs -t fat16 hd0p2
mkfs -t ext2  -L dati hd0p3
```

La partizione **non dev'essere montata**: sopra un volume montato c'è una
cache write-back, e scriverci sotto significa che il primo `sync` ci ricopre i
settori vecchi.

Il numero che conta è il **conteggio dei cluster**, e `mkfs` lo mostra accanto
alla soglia. Il tipo di un volume FAT non è scritto da nessuna parte: la
stringa `"FAT16   "` nel settore di avvio è decorativa, e il tipo si deduce dal
numero di cluster dell'area dati (< 4085 → FAT12, < 65525 → FAT16, oltre →
FAT32). Un formattatore che sceglie male i settori per cluster produce un
volume che *dice* FAT16 e *cade* nella banda FAT12, e nessuno se ne accorge
finché i dati non sono già rovinati.

Il settore di avvio vecchio viene azzerato **per primo** e quello nuovo scritto
**per ultimo**: in mezzo il volume non è riconoscibile da nessuno. L'ordine
opposto lascerebbe, su una formattazione interrotta, un settore di avvio che
descrive il filesystem vecchio sopra tabelle FAT già azzerate — un volume che
si monta, sembra funzionare e restituisce file vuoti.

`mkfs` non tocca la tabella delle partizioni, e non per scelta: le syscall
`SYS_BLKREAD`/`SYS_BLKWRITE` accettano solo nomi di **partizione**, e il
settore 0 non appartiene a nessuna partizione. Non esiste una coppia
(nome, LBA) che lo raggiunga. Se il byte di tipo nella tabella contraddice il
filesystem creato, `mkfs` lo segnala e dice come correggerlo con `fdisk`.

### ext2 — creazione e lettura

`mkfs -t ext2` crea un ext2 revisione 1 (`filetype`, `sparse_super`) con
blocchi da 1024. È scritto **dalla specifica**, non portato da e2fsprogs né dal
driver di Linux: quest'ultimo non è un modulo che legge ext2, è un modulo che
*traduce* ext2 nel VFS di Linux, e portarlo significherebbe portare quel VFS.

`kernel/fs/ext2.c` lo legge **e lo scrive**: `mkdir`, `cp`, `delete`, `rmdir`.
La lettura è stata scritta e verificata per prima, da sola — leggere richiede di
capire il formato, scrivere richiede di non romperlo mai, e con un lettore già
provato contro volumi fatti da `mke2fs` ogni errore di scrittura si vede subito
per quello che è.

### /bin/trunc — cambia la dimensione di un file

```
trunc <file> <byte>     suffissi K e M ammessi
```

**Allungare non occupa spazio** su ext2: lo spazio in mezzo diventa un *buco*
che si legge come zeri, e i blocchi si materializzano solo quando ci si scrive.
Un file portato a 2 MB così occupa un blocco solo. Su FAT il kernel alloca sul
serio, perché FAT non sa rappresentare un buco.

**Accorciare è distruttivo e non chiede conferma**, per la stessa ragione per
cui non la chiede `delete` su un nome preciso: chi scrive il nome di un file e
un numero più piccolo della sua dimensione ha già detto cosa vuole. La conferma
serve quando il comando fa più di quanto l'utente abbia nominato.

Sul floppy risponde `-38` (ENOSYS): `fat12.c` non ha un troncamento, ed è il
driver del volume di avvio — la strada collaudata che non si tocca senza una
ragione forte.

⚠️ ext2 non ha un giornale e questo driver non ne inventa uno: un'interruzione a
metà di un'operazione può lasciare i contatori dei liberi indietro rispetto alle
bitmap, ed è quello che serve `e2fsck`. Ciò che **non** può succedere è che un
blocco risulti libero mentre è già in uso — le bitmap si scrivono sempre prima
che il blocco venga consegnato.

**Nomi lunghi**: fino a 255 caratteri, il massimo di ext2. Non era solo la
struttura `VfsDirEntry` da allargare — il nome attraversa sei tetti in fila
(driver, VFS, ABI della syscall, lunghezza dei percorsi, argomenti di `spawn`,
riga della tastiera) e alzarne uno solo avrebbe spostato il taglio di un passo.
Un nome troncato non è un nome accorciato: è un nome che non apre niente.

Su FAT i nomi restano 8.3; il campo è largo per il filesystem più generoso.
Sopra i 511 caratteri una riga di comando non si può digitare, ed è il limite di
un singolo messaggio IPC fra tastiera e shell.

Il driver legge e scrive anche i volumi fatti da `mke2fs`: la dimensione del blocco
(1024/2048/4096), `s_first_data_block` (1 o 0 a seconda) e la dimensione
dell'inode (128 o 256) vengono tutte dal superblocco, mai date per scontate.

Rifiuta invece di provarci quando trova una funzionalità **incompat** che non
conosce — un volume ext4 con extent ha `i_block` che non contiene numeri di
blocco, e leggerlo "come se" restituirebbe dati presi a caso dal disco senza
che nulla lo segnali.

---

## La libc: da minimale a ospitata

Fino alla 0.145 `lib/libc.c` era una libreria da programma di servizio: `printf`,
le syscall e poco altro. Da agosto 2026 è la base su cui si può portare del
software scritto per un sistema POSIX — a cominciare da un compilatore.

### L'allocatore, che è il pezzo che mancava davvero

`free()` era una funzione **vuota**, con un TODO al posto del corpo, e `malloc()`
chiamava `sbrk` a ogni allocazione. Per i programmi di `/bin` non si notava:
allocano poche volte e poi escono. Per qualunque cosa lavori su una struttura ad
albero — un parser, un compilatore — significava crescere fino a esaurire lo
spazio senza aver mai tenuto in mano più di qualche KB. E `realloc()` copiava
`size` byte da un blocco di cui non conosceva la dimensione: ingrandire leggeva
**oltre la fine**.

Ora i blocchi stanno in una lista in ordine di indirizzo, `free()` li rende
riusabili e li **fonde** con i vicini liberi. La prova che conta sta in
`/bin/libctest`: 2000 `malloc`/`free` in ciclo non fanno crescere l'heap di un
byte.

### Stdio bufferizzato, con una politica diversa da Unix

C'erano `printf` e `putchar` — e `putchar` faceva **una syscall per carattere**.
Ora ci sono i `FILE*` (`fopen`, `fread`, `fwrite`, `fseek`, `ftell`, `fgets`,
`fprintf`, …), e un solo formattatore serve `printf`, `fprintf`, `sprintf` e
`snprintf`.

I file su disco sono bufferizzati a 4 KB. `stdout` e `stderr` **no**: sono
bufferizzati *dentro* la singola chiamata e svuotati alla sua fine. Non è il line
buffering di Unix, ed è deliberato — con quello, il prompt della shell
(`ex-os:/> `, senza newline) resterebbe nel buffer, e `gfedit` mostrerebbe
l'ultima riga solo dopo il tasto successivo. Unix se la cava perché leggere da
stdin svuota stdout; qui `gfedit` non legge da stdin, parla via IPC con il
servizio `kbd`, quindi quella convenzione non lo salverebbe. Svuotare a fine
chiamata elimina il problema e conserva quasi tutto il guadagno: un `printf`
costa una syscall invece di ottanta.

`exit()` svuota tutti i flussi: un programma che scrive un file e poi esce senza
`fclose()` trova il file scritto, non monco.

### Il resto

`setjmp`/`longjmp` (in assembly: sono esattamente i registri che il compilatore
ha il permesso di riorganizzare), `errno` con `strerror`/`perror`, `strtol`,
`strtoul`, `qsort`, `bsearch`, `strstr`, `strdup`, `strtok`, `memchr`, `ctype`,
e i wrapper `lseek`/`stat`/`sbrk`.

**`errno` si aggiunge, non sostituisce.** Le funzioni continuano a ritornare
l'errore negativo (`-2` = ENOENT, `-30` = EROFS): `< 0` resta il test giusto in
entrambe le convenzioni, e `-EIO` dice più di `-1`. Riscrivere ogni chiamante per
guadagnare zero non aveva senso.

Le syscall `stat` e `lseek(SEEK_END)` **rispondevano `ENOSYS`** — mai
implementate, con un TODO dalla Fase 3. Senza di loro nessun `FILE*` può offrire
`ftell()` sulla fine, cioè il modo con cui ogni programma misura un file prima di
leggerlo. Ora passano dal VFS e valgono su ogni filesystem montato.

### Virgola mobile, e la FPU che il kernel ha dovuto accendere

`strtod`, `strtof`, `strtold`, `ldexp`, `strtoll`, `strtoull`. Non è un lusso:
un compilatore deve leggere i letterali numerici dei sorgenti che compila —
`float x = 1.5;` passa da `strtod` — e una `strtod` che ritorna zero non dà un
errore, dà un programma compilato con la costante sbagliata.

Da qui il **PASSO 7b** del kernel: la FPU x87 viene inizializzata e il suo stato
(108 byte) salvato nel PCB a ogni cambio di contesto. Senza, due processi che
fanno conti in virgola mobile si sovrascrivono i registri a vicenda.

⚠️ **`x == 0.025` può essere falso anche quando `x` è giusto.** Su x87 GCC valuta
le costanti a 64 bit di mantissa, il `double` ne ha 53: il confronto avviene fra
due numeri diversi per costruzione. Il valore atteso va messo in una variabile
`double`, che forza l'arrotondamento.

### Lettura formattata, data e ora, stat

`sscanf`/`vsscanf` con larghezze, `%n`, soppressione e `%lf`. `time`,
`localtime`, `gmtime`, `mktime`, `gettimeofday` sopra `SYS_TIME`, cioè
l'orologio CMOS — senza fuso orario, perché il sistema non sa in quale si trova:
`localtime` e `gmtime` danno la stessa ora. `stat`/`fstat` nella forma POSIX.

### Processi: spawn con ambiente e redirezioni

`spawn()` lancia un programma e ritorna il PID; `waitpid()` ne raccoglie
l'esito. Non c'è `fork()`, ed è una scelta: duplicare uno spazio di
indirizzamento per buttarlo via alla `exec` successiva, su un sistema senza
copy-on-write, sarebbe la cosa più costosa che si possa fare.

```c
SpawnRedir r = { 1, O_WRONLY | O_CREAT | O_TRUNC, "/uscita.txt" };
int pid = spawn_ex("/bin/hello", argv, environ, &r, 1);
waitpid(pid, &stato, 0);
```

La redirezione è **per percorso**, non per descrittore già aperto del
padre: il figlio apre il proprio file. Passare un fd significherebbe due
processi sullo stesso handle VFS, cioè un conteggio di riferimenti che non
c'è e una `close()` che sfila il file da sotto all'altro. Basta a un driver
di compilatore; non basta alle pipe, che infatti non ci sono ancora.

L'ambiente si eredita per copia (`environ`, `putenv`, `setenv`,
`unsetenv`). `getenv()` ripiega sulla sezione `[env]` di `kernel.cfg` per
le chiavi che non trova: senza quel ripiego il primo processo — che un
padre non ce l'ha — resterebbe senza `PATH`.

### Intestazioni con i nomi standard

`<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<ctype.h>`, `<errno.h>`, `<setjmp.h>`,
`<unistd.h>`, `<stdint.h>`, `<inttypes.h>`, `<math.h>`, `<time.h>`, `<fcntl.h>`,
`<assert.h>`, `<sys/stat.h>`, `<sys/time.h>` esistono e rimandano a `libc.h`
(`<stdint.h>` no: i tipi li dichiara il compilatore, vedi
`tools/gcc-exos/leggimi.md`). Sono facciate sottili di proposito:
due elenchi della stessa funzione divergono, e la divergenza si manifesta come
prototipo sbagliato — argomenti passati storti, non un errore di compilazione.

### Il costo, e come è stato riassorbito

Ogni programma di `/bin` compila la libc dentro di sé. Con lo stdio nuovo i
binari erano raddoppiati (`ls`: 12 → 25 KB). `-ffunction-sections`
`-fdata-sections` più `--gc-sections` al link buttano ciò che nessuno chiama:
`ls` è tornato a **10 KB**, meno di prima che la libreria crescesse, e il floppy
ha più spazio libero di quanto ne avesse all'inizio.

```
libctest       171 prove: allocatore, formattazione, flussi, salti non
                locali, conversioni, errno, virgola mobile, sscanf,
                data e ora, stat, ambiente, directory, temporanei,
                descrittori duplicati, interfacce per il codice di terzi,
                spawn con redirezione — tutte dentro EX-OS
```

### Descrittori duplicati: `dup`, `dup2`, `fcntl`

Dalla 0.151 gli handle aperti del VFS hanno un **conteggio dei
riferimenti**: `close()` chiude davvero solo l'ultima volta. È ciò che
rende possibile `dup()`, cioè tenere un file aperto oltre la `close()` di
chi possedeva il descrittore originale — il gesto che fanno `ar`,
`objcopy` e `arsup` di binutils.

`dup2()` è anche l'unico modo di sostituire `stdin`/`stdout`/`stderr`:
`close()` su 0, 1 e 2 è rifiutata apposta, perché lascerebbe il processo
senza uscita, mentre chi arriva da `dup2` il rimpiazzo ce l'ha già.

> ⚠️ **I due descrittori condividono il file, non la posizione.** Su POSIX
> una `read()` da uno dei due sposta anche l'altro; qui l'offset sta nel
> descrittore del processo, non in un oggetto «file aperto» intermedio, e
> ognuno tiene il suo. Chi legge da un fd duplicato faccia una `lseek()`
> esplicita. È la prima cosa da sistemare il giorno che arriveranno le
> pipe.

---

## La catena di compilazione dentro EX-OS

**GNU binutils 2.44 gira nativamente su EX-OS.** `as` e `ld` sono
compilati **per** `i386-exos`, non per la macchina che li ha costruiti:

```
ex-os:/> /cdrom/bin/as --version
GNU assembler (GNU Binutils) 2.44
This assembler was configured for a target of `i386-exos'.

ex-os:/> /cdrom/bin/as -o /prova.o /cdrom/prova.s
ex-os:/> /cdrom/bin/ld -o /prova /prova.o
ex-os:/> /prova
Assemblato e collegato dentro EX-OS.
```

L'oggetto prodotto qui dentro è **identico byte per byte** a quello che
produce il cross su Linux. I due strumenti stanno sul CD degli strumenti
(`make iso`, ~1,4 MB l'uno dopo lo strip); il sorgente di prova è
`/cdrom/prova.s`.

### Perché è il collaudo che conta

`libctest` chiama le funzioni che **sappiamo** di avere. binutils chiama
quelle che gli servono, e non ha nessun riguardo: le ha chieste una alla
volta, ognuna fermando la compilazione, e l'elenco è la misura di quanto
mancava a una libc «ospitata» vera —

| | |
|---|---|
| processi | `dup`, `dup2`, `fcntl`, `_exit` |
| file | `realpath`, `lstat`, `freopen`, `mktemp`, `pathconf`, `utime` |
| stringhe | `strcasecmp`, `strncasecmp`, `strcoll`, `strpbrk` |
| formato | `fscanf`, `scanf`, `strftime`, `asctime`, `ctime` |
| numeri | `frexp`, `atof`, `fabs` |
| caratteri larghi | `mbstowcs`, `mbrtowc`, `wcstombs` |
| permessi (inerti) | `chmod`, `fchmod`, `umask` |
| header | `<sys/types.h>` `<strings.h>` `<wchar.h>` `<sys/param.h>` `<limits.h>` `<memory.h>` `<utime.h>` |

più due che non erano funzioni: **`EOF`** — il valore c'era dal principio,
mancava il *nome*, e `safe-ctype.h` verifica di poter lavorare con
`#if EOF != -1`, che senza la macro vede zero e conclude che la libc è
sbagliata — e **`strerror` che tornava `const char *`**, più sicuro e
incompatibile con la firma dello standard.

### `pex-exos.c`: lanciare un programma senza `fork`

`libiberty` compila sempre un `pex-*.c` — «lancia un programma e
aspettalo» — e per tutto ciò che non è Windows o MSDOS sceglie
`pex-unix.c`, costruito su `fork()`. EX-OS non ha `fork`, e non è una
mancanza da colmare: duplicare uno spazio di indirizzamento per buttarlo
via un'istruzione dopo è esattamente ciò che `spawn_ex()` evita.

`tools/binutils-exos/pex-exos.c` è il rimpiazzo, modellato su
`pex-msdos.c`: un «descrittore» è un indice in una tabella di **nomi**,
perché è il nome ciò che serve a `spawn_ex`. I tre `NULL` nella tabella
`funcs` (pipe, fdopenr, fdopenw) **sono la dichiarazione che questo
sistema non ha pipe**, e `pex-common.c` se ne accorge da solo e passa alla
modalità a file temporanei.

### Tre trappole, che costano un'ora a testa

- **`-std=gnu17`.** GCC 17 compila in C23, dove una dichiarazione
  implicita è un **errore**. binutils 2.44 presume l'indulgenza di C17.
- **`export ac_cv_tls=`** (stringa vuota, non `none`). La prova che il
  configure fa per le variabili thread-local è una **compilazione**:
  `i386-exos-gcc` accetta `_Thread_local` senza fiatare, perché è il
  compilatore a saper emettere gli accessi via `%gs` ed è il *sistema* a
  non avere un thread pointer. Il risultato è un `as` che si compila
  benissimo e muore alla terza istruzione di `bfd_init`. Con `none` la
  macro non viene definita affatto e binutils non compila: serve
  **definita a vuoto**.
- **`sys-include`.** GCC installa un proprio `<limits.h>` e ne esistono
  due versioni; quella che fa `#include_next` — cioè che prende anche la
  nostra — viene generata solo se, al momento di costruire GCC, un
  `limits.h` di sistema c'era già, e GCC lo cerca in
  `$prefisso/i386-exos/sys-include`.

Ricetta completa in **`tools/binutils-exos/leggimi.md`**.

### Cosa manca per il compilatore

`as` traduce e `ld` collega; manca il pezzo che trasforma il C in
assembly. In ordine: **GMP, MPFR, MPC** per il bersaglio (`cc1` ci si
linka), il sottoinsieme di **libstdc++** che serve a codice compilato
`-fno-exceptions -fno-rtti`, e infine **`cc1`** stesso.

---

## CD e DVD — driver ATAPI e ISO 9660

```
disk                    il lettore compare come cd0
mount cd0 /cdrom        montaggio manuale (sempre in sola lettura)
ls /cdrom
umount /cdrom           prima di espellere il disco
```

E in `/boot/kernel.cfg`, per averlo **montato all'avvio**:

```ini
[mount]
/cdrom = cd0
```

La riga è attiva nella configurazione predefinita, e può restarlo su qualunque
macchina: un lettore vuoto — o assente — non produce un avviso, solo un
"montaggio saltato" nel log. Un CD assente all'accensione è la condizione
normale, non un problema, e segnalarlo come tale metterebbe una riga fra i
*problemi durante l'inizializzazione* a ogni accensione.

### Le tre cose che un lettore non ha in comune con un disco

**Il blocco è da 2048 byte, non da 512.** La traduzione sta in un punto solo,
`kernel/block/blk.c`: il resto del sistema chiede settori da 512 come per
qualunque disco. Le richieste allineate — cioè quasi tutte, perché ISO 9660
lavora a blocchi — vanno dritte al dispositivo senza copie intermedie.

**La capacità appartiene al disco, non al lettore.** Un `cd0` con zero settori
non è un lettore rotto: è un lettore vuoto, o che nessuno ha ancora sondato. La
finestra viene riempita da `blk_supporto()` quando serve, e azzerata quando il
disco esce.

**Gli errori sono a due livelli.** Il bit ERR dice solo "CHECK CONDITION": il
motivo sta nei dati di *sense*, che vanno chiesti con un secondo comando. Senza
leggerli, «non c'è il disco», «il disco è appena stato cambiato» e «il disco è
illeggibile» sono la stessa cosa — e le prime due non sono errori.

Un disco **inserito a sistema avviato** si monta senza riavviare. Un lettore
appena rifornito però risponde ancora "supporto assente" per un comando o due, e
in emulazione il vassoio resta *aperto*: il driver insiste qualche volta e lo
chiude una volta sola, come fa Linux. La conseguenza va detta — montare su un
lettore lasciato aperto e vuoto lo chiude.

### ISO 9660, e perché Joliet vince quando c'è

Un disco masterizzato con nomi lunghi contiene **due alberi completi**: quello
ISO 9660, con i nomi maiuscoli, troncati e con il numero di versione
(`LEGGIMI.TXT;1`), e quello Joliet, con i nomi veri in UCS-2. Non sono due viste
della stessa struttura: sono due catene di directory separate che puntano agli
stessi dati. `kernel/fs/iso9660.c` sceglie Joliet quando c'è, e dice quale ha
scelto; senza, i nomi che si vedono non sono quelli che l'utente ha scritto.

Sui nomi ISO toglie il `;1` e il punto finale — sono formato, non nome — e li
mostra in minuscolo; il confronto è insensibile alle maiuscole, altrimenti ciò
che `ls` mostra non sarebbe digitabile.

**Sola lettura, e non per pigrizia**: ISO 9660 non ha bitmap di spazio libero né
voci riutilizzabili. Non esiste "aggiungere un file", esiste rifare l'immagine.
Ogni scrittura è respinta con `-30` (EROFS) prima di toccare il volume.

**Rock Ridge non è gestito** — l'estensione Unix annidata nei campi di sistema
dei record. Un disco che la usa resta leggibile: si vedono i nomi ISO o Joliet,
che ci sono comunque.

### `make iso` — il CD degli strumenti

```bash
make iso        # dist/exos-tools.iso
make run-iso    # avvia QEMU con il CD già inserito
```

Un secondo supporto, separato dal floppy e **non avviabile**: ci va ciò che in
1.44 MB non entra e che non serve a tutti. Il floppy resta il supporto di avvio
collaudato; gli strumenti cambiano spesso, pesano, e non devono poter rompere
l'avvio.

Il disco contiene oggi:

```
/leggimi.txt          cos'è questo disco e come si monta
/exos/include/        gli header della libc (stdio.h, stdlib.h, …)
/exos/libc.c          la libc in un file solo
/exos/start.S         il pezzo di avvio che chiama main()
/doc/                 README, note sul kernel, licenza
/bin/                 vuota: è il posto del compilatore
```

`/exos/` non è documentazione: è ciò che serve per **compilare su EX-OS**. Il
primo inquilino di `/bin` sarà TCC — si porta dietro assemblatore e linker, gira
in pochi MB e chiede alla libreria una frazione di ciò che chiede GCC.

⚠️ Il CD è in sola lettura per costruzione, quindi header e librerie si leggono
da lì ma **l'output di una compilazione deve andare altrove** — cioè su un EX-OS
installato su ext2, o sul floppy.

### Provare il driver senza masterizzare niente

`tools/mkiso.py` genera anche un'immagine sintetica di collaudo, di cui si
conosce ogni byte — utile proprio perché, quando il driver legge un nome
sbagliato, si sa cosa c'era scritto:

```bash
python3 tools/mkiso.py /tmp/test.iso --prova                  # con Joliet
python3 tools/mkiso.py /tmp/solo-iso.iso --prova --senza-joliet
qemu-system-i386 -fda dist/floppy.img -m 32M -boot a -cdrom /tmp/test.iso
```

Lo stesso strumento fa da masterizzatore per qualunque albero di directory:

```bash
python3 tools/mkiso.py /tmp/mio.iso --da /percorso/albero --etichetta "MIO CD"
```

Costruisce **entrambi** gli alberi — nomi ISO 9660 8.3 maiuscoli con `;1` e nomi
Joliet veri in UCS-2 — condividendo i blocchi dei file, e gestisce nomi lunghi,
sottodirectory e collisioni del troncamento a 8.3 (due file distinti che
diventassero lo stesso nome sarebbero un file perso in silenzio).

---

## Arresto e spegnimento

| Comando shell | Effetto |
|---|---|
| `halt` | sincronizza il filesystem, ferma il sistema, **non** spegne |
| `poweroff` / `shutdown` | sincronizza, conta 3 secondi, spegne l'hardware |
| `reboot` | sincronizza e riavvia (reset via 8042, fallback triple fault) |

Lo spegnimento hardware usa le porte ACPI note di QEMU (`0x604`), Bochs
(`0xB004`) e VirtualBox (`0x4004`). **In emulazione la macchina si spegne
davvero; su hardware reale con ogni probabilità no** — servirebbe un parser
ACPI (FADT/DSDT) o APM via real mode, nessuno dei due ancora implementato. In
quel caso il sistema resta fermo in stato sicuro con il messaggio "è ora
sicuro spegnere il computer", come i PC pre-ATX.

---

## Identità e versione del sistema

`kernel/include/version.h` è la **fonte unica di verità** per nome, versione,
autore e licenza. Da lì derivano il banner di avvio, i comandi `ver`/`version`
e `uname`, e le variabili `OSNAME`/`OSVER`/`AUTHOR` dell'ambiente — che il
kernel inietta in `[env]` e che **non** vanno scritte in `kernel.cfg`.

```c
#define EXOS_VERSION    "0.101"   /* +0.001 a ogni modifica del kernel */
```

È una stringa e non un numero perché il kernel non usa la virgola mobile.
L'incremento è manuale e deliberato.

---

## Avvio silenzioso

```ini
[kernel]
verboseboot = 0    # 0 = solo output normale (default), 1 = log e banner
```

**Il default è `0` dalla 0.142** (prima era `1`): un sistema che si avvia
mostra il proprio nome, non i propri passi di inizializzazione. Vale in tutti
i casi dubbi — voce assente, file mancante, valore non numerico — e solo un
numero diverso da zero fa parlare il sistema.

Con `0`: schermo pulito, una riga di identità, prompt. I messaggi dei PASSI
1-13 vengono emessi lo stesso — sono stampati prima che il file di
configurazione sia leggibile — e il kernel li cancella dallo schermo al
PASSO 13c.

**Errori ed eventi inattesi restano sempre visibili**, in silenzioso come in
verboso:

- un `LOG_ERROR` è stampato qualunque sia il livello di log, anche con
  `loglevel = 0`;
- ogni ERROR/WARN è registrato mentre viene emesso e **riproposto dopo la
  pulizia dello schermo**, sotto l'intestazione `Avvio silenzioso: N
  problema/i durante l'inizializzazione` — così cancellare il log di avvio
  non cancella le prove di ciò che è andato storto;
- se i problemi superano gli slot del registro, la ristampa lo dichiara
  invece di troncare in silenzio;
- la console seriale riceve comunque tutto, filtro incluso.

```
EX OS 0.101 (Extensible Operating System) - Copyright (C) 2025 Graziano Falcone - GPL 2.0

  Avvio silenzioso: 1 problema/i durante l'inizializzazione
[WARN]  PMM: nessuna mappa E820, uso fallback: 32256 KB

ex-os:/>
```

---

## Interfaccia driver

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

---

## Piano di sviluppo (Strategia D Ibrida)

- [x] **Fase 1a** — Bootloader Stage1 + Stage2 + FAT12 read
- [x] **Fase 1b** — Kernel entry, GDT, IDT, ISR, VGA, kprintf
- [x] **Fase 1c** — Physical Memory Manager (E820 + bitmap)
- [x] **Fase 1d** — Paginazione x86 + heap kernel (kmalloc)
- [x] **Fase 2a** — Scheduler preemptive 100Hz + context switch
- [x] **Fase 2b** — Syscall interface int 0x80
- [x] **Fase 3**  — TTY driver + FAT12 R/W kernel + ELF loader
- [x] **Fase 4**  — Shell utente + cfg reader
- [~] **Fase 5**  — Driver in userspace (ring3): tastiera fatta, floppy da fare
- [~] **Fase 6**  — Sistema ospitante: libc POSIX, `as` e `ld` nativi fatti;
                    compilatore da fare

**Stato Fase 4 (luglio 2026)**: la shell parte come primo processo ring3, legge
`/boot/kernel.cfg`, ed esegue programmi esterni (`hello`, `ls`, `cat`) come task
separati con ritorno al prompt. Il deadlock del PIC che bloccava ogni programma
lanciato dalla shell è risolto — vedi `HANDOFF.md`.

**Stato Fase 5 (30 luglio 2026)**: `/dev/kbd.drv` è il **primo driver di EX-OS
che gira davvero in ring3**. È un processo con la propria page directory che
non esegue istruzioni privilegiate né chiama simboli del kernel: legge gli
scancode con `SYS_IOPORT_IN`, riceve gli IRQ1 come messaggi IPC via
`SYS_IRQ_BIND`, e consegna le righe digitate al TTY con `SYS_IPC_SEND`. Il TTY
resta in-kernel per la VGA ma per l'input è un client del servizio.

Il driver floppy è ancora un modulo ET_DYN kernel-space e l'accesso al floppy
resta servito dal FAT12/FDC interno al kernel. Analisi e passi necessari in
`KERNEL_CORE_NOTES.md`, punto 5; dettagli di progetto e trappole in
`HANDOFF.md`.

**Stato Fase 6 (2 agosto 2026)**: la libc è cresciuta fino a reggere codice
scritto per POSIX, e la prova è che **binutils 2.44 gira dentro EX-OS**.
Portarlo ha scoperto tre difetti del kernel che nessun programma di EX-OS
poteva mostrare, perché tutti scrivevano un file dall'inizio alla fine:

- i descrittori ancora aperti alla terminazione non li chiudeva nessuno —
  uno slot VFS perso per file, ed `EMFILE` dopo 64 volte;
- sul **floppy** la scrittura ignorava la posizione del descrittore e si
  accodava sempre in fondo (su ext2 e FAT16/32 no: lì l'offset arrivava);
- scrivere all'**inizio** di un settore lo azzerava per intero, cancellando
  i byte che c'erano dietro.

Tutti e tre invisibili finché nessuno torna indietro in un file. Un
qualunque scrittore di ELF lo fa.

**Cosa manca ancora**, in ordine di quanto darà fastidio:

| | |
|---|---|
| **pipe** | e con esse una posizione condivisa fra descrittori duplicati |
| **`SYS_RENAME`** | oggi `rename()` copia e cancella: va bene per un temporaneo, non per un file grosso |
| **DMA per il disco** | oggi 0,75 MB/s in PIO |
| **thread pointer (TLS)** | il compilatore sa emettere `%gs:0`, il sistema non ha dove puntarlo |
| **GMP, MPFR, MPC, `cc1`** | il compilatore ospitato |

---

## Licenza

EX-OS è software libero: puoi ridistribuirlo e/o modificarlo
secondo i termini della GNU General Public License versione 2
pubblicata dalla Free Software Foundation.

Vedi il file `LICENSE` per il testo completo.

---

*EX-OS — "Il sistema si estende, il kernel rimane piccolo."*

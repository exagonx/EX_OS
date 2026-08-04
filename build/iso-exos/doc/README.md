# EX-OS — Extensible Operating System

**Autore:** Graziano Falcone <exagonx@hotmail.com>
**Licenza:** GNU General Public License v2 (GPL-2.0)
**Architettura:** x86 32-bit, floppy FAT12 1.44MB

*Questo è il documento in italiano. La versione inglese è
[README.en.md](README.en.md); le due si aggiornano insieme.*

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
cross-compilatore su Linux. Ci gira anche **`cc1`**, il compilatore C di
GCC, che qui compila sorgenti C e ne produce l'assembly. Vedi
[La catena di compilazione dentro EX-OS](#la-catena-di-compilazione-dentro-ex-os).

---

## Novità

Le voci sono marcate **testato** quando il lavoro è stato verificato girando
dentro EX-OS, **da testare** quando il codice c'è ma la prova che conta —
quella sull'hardware o sul caso reale — non è ancora stata fatta.

### Rete — dal bus PCI a un client FTP

| | |
|---|---|
| Enumerazione PCI in userspace (`/dev/pci.drv`, `netdetect`) | testato |
| Driver NE2000 in ring3 (`/dev/ne2k.drv`, `nettest`) | testato |
| ARP, IPv4, ICMP — `ping` | testato |
| UDP | testato |
| Client DHCP (`dhcp`) | testato |
| Risolutore DNS in libc, record A (`host`) | testato |
| TCP: apertura attiva, invio, ricezione, chiusura (`tcptest`) | testato |
| Client FTP passivo, `get`/`put`/`ls`/`cd` (`ftp`) | testato |
| Configurazione a mano e tabella ARP (`ipcfg`, `ipcfg -r`) | testato |
| Rinnovo della concessione DHCP | da fare |
| Riordino dei segmenti TCP fuori sequenza | da fare |
| Driver PCnet (Am79C970/C973), bus master con DMA vero | testato |
| `SYS_DMA_ALLOC`: memoria contigua per un bus master | testato |

Ogni comando di rete, quando qualcosa manca, stampa **la catena completa** e
**il prossimo comando da dare** invece del solo messaggio d'errore.

### CPU — SSE, SSE2, SSE3, MMX

| | |
|---|---|
| Rilevamento capacità via CPUID, con la prova del bit ID in EFLAGS | testato |
| Salvataggio dello stato: FXSAVE dove c'è, FNSAVE sulle CPU vecchie | testato |
| Attivazione di CR4.OSFXSR / OSXMMEXCPT quando SSE è presente | testato |
| MMX: nessun lavoro necessario, MM0-MM7 sono alias di ST0-ST7 | testato |
| Esecuzione su 486 e Pentium MMX veri | da testare |

Il percorso FNSAVE è quello che permette al kernel di girare su CPU senza
SSE; è stato provato forzando la via lenta in QEMU, non su un 486 fisico.

### Filesystem

| | |
|---|---|
| Avvio da CD: FAT12 non viene più sondata sul CD-ROM | testato |
| Nomi lunghi VFAT in **lettura** su FAT16 e FAT32 | testato |
| Data e ora reali dei file su FAT, ext2 e ISO 9660 | testato |
| Timbratura di data e ora sui file creati su floppy | testato |
| `mkfs` sceglie da solo FAT16 sotto i 2 GB, FAT32 sopra | testato |
| Nomi lunghi VFAT in **scrittura** | da fare |

### Shell e comandi

| | |
|---|---|
| Cronologia comandi con le frecce su/giù | testato |
| `/boot/autoexec.sh` eseguito da `/bin/sh` all'avvio | testato |
| `ls`: `-h`, `-a`, `-d`, `-mc`, `-md`, `-p` | testato |
| `install -a`: elenca i file cambiati e propone l'aggiornamento | testato |
| `hwconfig`: analizza la macchina e scrive kernel.cfg e autoexec.sh | testato |
| Argomenti con spazi fra virgolette (`cp "il mio file.txt"`) | testato |
| `help helpconfig`: come si accendono i driver, con lo stato attuale | testato |
| Backspace che non cancella più il prompt né lascia caratteri invisibili | testato |
| `!silenced` negli script: nasconde i comandi, non il loro risultato | testato |
| `source file.sh`, e i `.sh` eseguibili per nome | testato |

### libc

| | |
|---|---|
| `printf` con `%f`, `%e`, `%g`: 18 cifre significative, arrotondamento pari | testato |
| Costruttori globali `.init_array` e distruttori `.fini_array` | testato |
| `realloc` che ingrandisce sul posto — prima non ingrandiva mai | testato |
| `gettimeofday` monotòno, ancorato una volta sola all'orologio | testato |
| `time_t` a 64 bit | testato |
| I file temporanei seguono `TMPDIR`, non più solo la radice | testato |
| 276 prove automatiche in `libctest` | testato |

### Catena di compilazione

| | |
|---|---|
| `as` e `ld` (binutils 2.44) nativi | testato |
| `cc1`: compila C e produce assembly dentro EX-OS | testato prima del cambio di ABI, da riprovare |
| Runtime del bersaglio sul CD (`crt0.o`, `libc.a`, `libgcc.a`) | testato |
| `as` + `ld` collegano un programma C vero con gli archivi | testato |
| `gcc` come programma di guida, che concatena cc1 → as → ld | da fare |
| TLS/SSL come libreria userspace (porting OpenSSL) | da fare |

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
│   ├── delete       ← Cancella file (con jolly ? e *)
│   ├── rename       ← Cambia il nome di un file (non sposta: vedi sotto)
│   └── chkdsk       ← Controlla e ripara un volume FAT12/16/32
├── lib/             ← Shared libraries (Fase 4b)
└── dev/
    ├── kbd.drv      ← Driver tastiera PS/2 (processo ring3)
    └── floppy.drv   ← Driver floppy controller (ancora ET_DYN, non caricato)
```

Il TTY non compare in `/dev`: `drivers/tty/tty.c` è compilato **dentro** il
kernel (possiede la VGA), e per l'input fa da client del servizio `kbd`.

### Cosa va sul floppy, e cosa no

Il floppy porta **il sistema**: avviarsi, preparare un disco, installarsi,
leggere e scrivere file. Partizionatore (`fdisk`), formattatore (`mkfs`),
controllore (`chkdsk`), montaggio, installatore, editor; il driver del
floppy e quello della tastiera, che servono a partire.

⚠️ **I driver aggiuntivi non ci vanno.** Rete (`pci`, `ne2k`, `pcnet`,
`ip`) e tutto ciò che verrà dopo stanno sul **CD di EX-OS**. Non è una
preferenza: in 1.44 MB non ci stanno, e il modo in cui non ci stanno è il
peggiore — `mcopy` fallisce a metà dell'elenco, l'immagine resta priva di
qualche file scelto dall'ordine alfabetico, e il sistema si avvia fino al
punto in cui gli serve quello che manca.

Il CD-ROM **non ha un driver in `/dev`**: ATAPI e ISO 9660 stanno *dentro*
il kernel, perché il kernel deve poterci montare la radice prima che
esista un processo che possa servirla.

```
                    floppy    CD di EX-OS    CD strumenti
sistema e shell       si          si              -
fdisk, mkfs, chkdsk   si          si              -
kbd.drv, floppy.drv   si          si              -
driver di rete        NO          si              -
ping, ftp, dhcp…      NO          si              -
as, ld, cc1           NO          NO             si
```

`make verify` **controlla la regola** invece di fidarsi, e dice quanto
spazio resta sul floppy — il numero che avvisa prima che l'immagine
smetta di contenere tutto:

```
[OK] nessun driver da CD sul floppy
                            629 760 bytes free
```

Quello che in 1.44 MB non entra sta sul **CD degli strumenti**
(`make iso`): `as`, `ld` e `cc1` nativi in `/bin`, gli header e il sorgente
della libc in `/exos`, il runtime del bersaglio in `/exos/lib`, la
documentazione in `/doc`.

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

### Il thread pointer: variabili `__thread`

Dalla 0.154 il caricatore riconosce `PT_TLS` e ne fa una copia per
processo, sotto la riserva dello stack con una pagina di guardia in mezzo.
Il descrittore GDT numero 6 (selettore `0x33`, quello che i processi
tengono in `GS`) è User Data con una **base che cambia**: lo scheduler la
riscrive a ogni switch con il thread pointer del processo entrante.

È il modello **local-exec**, quello dei binari statici: gli offset delle
variabili li risolve `ld` al link, quindi a runtime non c'è niente da
rilocare. Con base zero il descrittore è indistinguibile da `0x23`, perciò
chi non usa `__thread` non paga niente.

> ⚠️ Non c'è il TLS **dinamico** (`__tls_get_addr`, variabili
> thread-local dentro una libreria condivisa): serve a chi carica codice a
> runtime, e qui i binari sono statici. E non ci sono i thread: questo è il
> pezzo che serve ad averli, non loro.

Perché farlo, se un processo ha un filo solo e una variabile `__thread` è
una globale con un nome più lungo? Perché il modo in cui *mancava* era il
peggiore possibile: la prova che ogni `configure` fa per il TLS è una
**compilazione**, e il compilatore la supera sempre — sa emettere gli
accessi via `%gs` da vent'anni, ed è il sistema a non avere dove puntarli.
Nessun errore, nessun avviso, un binario che si costruisce benissimo e
muore alla terza istruzione della prima funzione che chiama.

### Lo spazio di un processo, e il tetto dello heap

```
0x08000000  testo, dati, bss del programma
            heap ---->                             (sbrk, mmap senza MAP_FIXED)
0xbffbc000  heap_max — il tetto
0xbffbd000  pagina di guardia
0xbffbd000  blocco TLS, se il programma ne ha uno
0xbffbf000  riserva dello stack (256 KB)   <---- lo stack cresce all'ingiù
0xbffff000  cima dello stack
```

Lo heap comincia **subito dopo l'ultimo segmento caricato**, non a un
indirizzo fisso. Dalla 0.156 ha anche un **tetto**: una pagina di guardia
sotto il blocco TLS se c'è, sotto la riserva dello stack se non c'è.

Prima non ce l'aveva, e l'unico limite era la RAM fisica. Sembra
innocuo — la memoria finisce prima — ma sopra lo heap non c'è il vuoto, e
`paging_map_page()` **sovrascrive una PTE già presente senza dire niente**.

> ⚠️ Uno heap abbastanza grande avrebbe rimappato **il blocco TLS del
> processo stesso** su pagine nuove azzerate: il thread pointer a zero, e
> ogni variabile `__thread` a leggere memoria altrui. Senza un fault, senza
> un log. Ora chi supera il confine si prende `ENOMEM`, che è un errore che
> `malloc()` sa già trattare.

Il controllo sta **prima** di allocare, non dentro il ciclo: fermarsi a
metà lascerebbe lo heap avanzato di un valore che il chiamante non ha mai
visto. E `mmap` rispetta lo stesso confine, `MAP_FIXED` compreso — il
blocco TLS e la riserva dello stack non sono roba che un processo possa
farsi rimpiazzare, perché il kernel ci tiene degli invarianti sopra.

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

### Il Backspace e le colonne che non ci sono

La disciplina di riga «cooked» — quella che accumula i caratteri e li
consegna su Invio — cancellava **una colonna per ogni carattere nel
buffer**. Sembra ovvio e non lo è: i caratteri di controllo entrano nella
riga ma non vengono ecoati (ESC ci va, `/bin/textline` lo usa per annullare
una riga), e le frecce ci entrano come sequenza `ESC [ A`, di cui due byte
su tre sono stampabili e nessuno dei tre è stato disegnato.

> ⚠️ Risultato: due ESC battuti per sbaglio, due Backspace, e le due
> colonne cancellate erano **le ultime del prompt**. Nel registro seriale
> si vedeva `^H ^H^H ^H` e l'asterisco di textline sparire.

Ora ogni carattere del buffer porta con sé un bit — *questa l'ho disegnata
io oppure no* — e il Backspace cancella una colonna solo se quella colonna
è nostra. Il prompt è fuori portata per costruzione, non per un controllo
in più. Nello stesso giro sono cadute due asimmetrie della stessa
famiglia: a riga piena il carattere veniva ecoato ma non accumulato (si
eseguiva meno di quello che si leggeva), e il tab veniva disegnato pur
essendo impossibile da disfare — avanza fino alla prossima tabulazione,
che dipende da dove comincia il prompt.

**Arrivati al limite la riga si azzera del tutto.** Se non resta più
niente di visibile, quello che eventualmente sopravvive nel buffer sono
caratteri invisibili, pronti a finire dentro il comando successivo: chi
cancella fino in fondo si aspetta una riga vuota e la trova vuota davvero.

Vale per `drivers/kbd/kbd.c` e per il TTY interno di ripiego
(`drivers/tty/tty.c`). La modifica di riga della shell — quella con le
frecce e la cronologia — non era coinvolta: lavora in raw e accetta solo
caratteri stampabili.

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

### `install` verifica prima di sostituire (dal 0.161)

Rilanciarlo su un sistema già installato **riscrive ogni file**, kernel
compreso, e rilegge ognuno per confrontarne la dimensione. Nel resoconto `+`
è creato, `~` sostituito, `!` errore.

Fino alla 0.147 i file già presenti venivano saltati: un aggiornamento
copiava solo i file nuovi, lasciava il kernel vecchio e poi riscriveva la
mappa dei settori *per quello* — il disco ripartiva con la versione di
prima e l'installatore diceva «completata». Le directory continuano a non
essere ricreate, e ciò che sul volume non fa parte del sistema resta dov'è:
`install` aggiorna, non azzera.

**E fino alla 0.160 distruggeva prima di sapere se ce l'avrebbe fatta.**
Apriva `/boot/kernel.bin` con `O_TRUNC`, ci scriveva il kernel nuovo, e
*poi* chiedeva la mappa dei settori. Su FAT — dove la mappa ammette **un
solo intervallo** — un kernel cresciuto di qualche KB non entrava più nel
buco lasciato dal vecchio, finiva in due tratti, `bootinstall` rifiutava
giustamente:

```
! installazione dell'avvio fallita: file frammentato (errore -29)
```

…ma a quel punto il sistema che funzionava non c'era più, il settore di
avvio puntava ancora alla mappa vecchia, e **il disco non ripartiva**. Su
ext2 non si vedeva: lì la mappa regge 12 intervalli.

Ora i file nuovi si scrivono **con nomi temporanei mentre i vecchi sono
ancora al loro posto** — quindi finiscono nella coda libera, contigua. Poi
si chiede al kernel se sono mappabili. Solo se la risposta è sì si cancella
e si rinomina:

```
Avvio (scritti a parte e verificati prima di sostituire)
  = verifica: 585 settori in 1 intervallo — si puo' sostituire
  ~ /disk/boot/stage2.bin  (verificato, poi sostituito)
  ~ /disk/boot/kernel.bin  (verificato, poi sostituito)
```

Lo scenario che distruggeva il disco ora **riesce**. Quando invece non si
può fare, si legge `Il sistema gia' installato NON e' stato toccato` e il
disco resta avviabile.

> ⚠️ **È servita una primitiva che mancava.** Lo scambio non si poteva
> fare: `rename()` era copia+cancella, quindi riallocava i blocchi e
> mandava a monte la verifica appena fatta. Vedi *La rinomina che non
> sposta i dati* più avanti.

**`kernel.cfg` non si sovrascrive più.** È l'unico file dell'installatore
che appartiene a chi usa il sistema e non al sistema: montaggi automatici,
`verboseboot`, shell, variabili d'ambiente. Un aggiornamento non deve
riportarli indietro in silenzio. Se manca si installa, se c'è si lascia e
lo si dice.

### `install -a` — aggiornare invece di reinstallare

```
install -a /disk
```

Confronta il volume montato con il supporto di avvio, **elenca cosa
cambierebbe**, chiede conferma e solo allora scrive. `+` è un file che sul
disco non c'è, `~` uno che c'è ma è diverso.

```
Confronto di /disk con il supporto di avvio
  +  da creare    ~  da sostituire
  + /disk/bin/ftp
  ~ /disk/bin/ls
  ~ /disk/boot/kernel.bin

35 file da aggiornare (14 nuovi).
Procedo? [si/no]
```

L'elenco viene **prima** della domanda perché «aggiorno 3 file?» e «aggiorno
47 file?» sono due decisioni diverse: un aggiornamento che tocca tutto quando
ci si aspettava un ritocco è il momento in cui ci si accorge di aver montato
il volume sbagliato.

> ⚠️ La regola è **«la sorgente è più nuova»**, non «le date sono diverse».
> Copiare un file non ne conserva la data: la copia sul disco nasce con l'ora
> corrente, quindi con la regola ingenua ogni file risulterebbe da aggiornare
> a ogni esecuzione, per sempre, anche subito dopo averlo appena copiato.

La dimensione si confronta **per prima**, ed è il controllo che conta di più:
un file scritto a metà ha la stessa data e una dimensione diversa, e senza
quel confronto il volume resta rotto senza che nessuno lo dica. Se una delle
due date è zero — cioè «questo volume le date non le tiene» — si guarda solo
la dimensione.

Senza `-a`, `install` continua a fare l'installazione completa: riscrive
tutto, che è quello che serve la prima volta e dopo un `mkfs`.

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

### `chkdsk` — controllo e riparazione di un volume FAT

```
chkdsk <partizione>       controlla e riferisce, non scrive un settore
chkdsk -r <partizione>    controlla e corregge
```

Controlla, in quest'ordine — ogni passo si fida solo di quelli già fatti:
il BPB e la coerenza dei suoi numeri, le copie della FAT, le catene di
cluster percorrendo tutte le directory (condivisi, catene fuori dal volume,
anelli), la dimensione dichiarata di ogni file contro quella della sua
catena, i nomi lunghi, `.` e `..`, e i cluster perduti.

```
tipo FAT32 — 130556 cluster da 8 settori, 2 FAT da 1021 settori
= FAT[0] = 0xffffff8, FAT[1] = 0xfffffff
= le 2 copie sono identiche
! 3 cluster risultano occupati ma nessun file li nomina (12 KB)
```

> ⚠️ **Lavora solo su una partizione smontata**, e non è un fastidio da
> aggirare: sopra un volume montato c'è una cache write-back, metà delle
> modifiche recenti sta in RAM. Un controllore che leggesse i settori
> grezzi segnalerebbe incoerenze inventate, e riparando riscriverebbe
> settori che il primo `sync` ricoprirebbe.

> ⚠️ **Senza `-r` non scrive un solo settore.** Un controllore che ripara
> di sua iniziativa trasforma un volume danneggiato in un volume
> danneggiato *diversamente*, senza che nessuno abbia visto com'era.

Alcune scelte che non sono ovvie:

- **il tipo si ricava dal numero di cluster**, non dalla stringa
  `"FAT16   "` nel settore di avvio: quella è decorativa e nessuno la
  verifica mai, quindi su un volume malandato è proprio un campo di cui non
  fidarsi;
- **FAT12 si legge con due settori in mano**: una voce occupa dodici bit e
  può cominciare nell'ultimo byte di un settore. In scrittura, se è a
  cavallo, **rinuncia e lo dice** invece di scrivere mezzo valore — sarebbe
  un danno nuovo causato dallo strumento che doveva ripararne uno;
- **su FAT32 i quattro bit alti di una voce non sono nostri**: si
  conservano;
- **la dimensione si confronta con un intervallo**, non con un numero: un
  file di N byte occupa `ceil(N/cluster)` cluster, e pretendere
  l'uguaglianza esatta segnalerebbe come guasto quasi ogni file;
- **i cluster condivisi non si riparano da soli**: quale dei due file abbia
  diritto ai dati non è deducibile, e ripararli d'ufficio significherebbe
  scegliere a caso quale rovinare;
- **i perduti si liberano, non si raccolgono in `FOUND.000`**: sarebbe una
  collezione di frammenti senza nome né struttura, che occupa lo stesso
  spazio che si voleva recuperare.

Sui **nomi lunghi**: una fila di voci finte precede quella 8.3, in ordine
rovesciato, legata a essa da un solo checksum.

> ⚠️ Chi rinomina toccando solo la voce 8.3 — e **`rename` di EX-OS fa
> esattamente questo** — lascia il nome lungo a nominare un file che non è
> più quello. Non è un caso di scuola: è un difetto che questo sistema sa
> produrre, ed è il motivo per cui il controllo serve qui.

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

### La libm: openlibm, e perché una di terzi

Fino ad agosto 2026 `<math.h>` dichiarava tre funzioni e diceva che una libm
non c'era, con questa motivazione:

> «una `sqrt` quasi giusta è peggio di nessuna `sqrt`: sbaglia in silenzio».

**Il ragionamento non è cambiato — è cambiata la conseguenza.** La risposta
coerente a "non so scrivere `sin` con l'errore giusto" non era scriverne una
mediocre: era **portarne una vera**. Dietro i nomi c'è **openlibm 0.8.7**,
cioè la `msun` di FreeBSD in versione autonoma (MIT/BSD), con trent'anni di
correzioni sugli arrotondamenti e una directory `i387` che usa le istruzioni
dell'x87 dove convengono. Si costruisce con
`tools/openlibm-exos/prepara-libm.sh`; i sorgenti non stanno nel repository,
come per GCC e binutils.

```
ex-os:/> /cdrom/bin/provamat
openlibm dentro EX-OS

sin(pi/2)=1000 cos(0)=1000 pow(2,10)=1024000 log(e)=1000 sqrt(16)=4000
atan2(1,1)=785 hypot(3,4)=5000 isnan(0/0.)=1
sinf(pi/2)=1000  sinl(pi/2)=1000  exp2(10)=1024000
```

I valori sono moltiplicati per mille e stampati come interi — la `printf`
di EX-OS non formatta i `double`, e mostrarli in virgola mobile avrebbe
provato la printf invece della libm. Quindi `atan2(1,1)` = 785 = 0,785 =
π/4.

I 184 prototipi in `lib/include/math.h` sono **ricavati dai simboli davvero
definiti in `libm.a`**, non copiati da uno standard: se una funzione è
dichiarata lì, esiste.

> ⚠️ **Chi usa queste funzioni deve linkare `-lm`.** Le eccezioni sono
> `sqrt`, `fabs`, `ldexp` e `frexp`, che stanno nella libc. `sqrt` è
> `fsqrt` dell'x87, cioè **una delle cinque operazioni che l'IEEE 754
> obbliga a essere correttamente arrotondate**: non c'è
> un'approssimazione da giudicare, c'è un'istruzione da chiamare. La
> definisce anche `libm.a`, ma vince sempre quella della libc — `libc.o`
> entra comunque nel link (printf, crt0) e a quel punto il simbolo è già
> risolto. Nessun "multiple definition", e `sqrt` si usa senza `-lm`.

Chi la chiede davvero è **libstdc++**: il suo `<cmath>` scrive `using
::sin;` per circa centottanta nomi, e quei nomi devono esistere o la
libreria non compila.

### Allocazione allineata

`memalign`, `aligned_alloc`, `posix_memalign`. Dal C++17 un tipo con
allineamento superiore a quello naturale non passa più per `operator
new(size_t)` ma per la variante allineata, che nella libstdc++ è un
involucro attorno a `memalign()` — e se `memalign` non c'è, la libreria ne
mette una che **ignora l'allineamento richiesto**.

L'allineamento si ottiene ritagliando: si chiede a `malloc` un blocco
abbastanza grande, poi lo si **spezza in due** mettendo una vera
intestazione subito prima dell'indirizzo allineato, e la testa resta come
blocco libero invece di essere sprecata.

> ⚠️ **Il puntatore restituito si libera con `free()`**, non con una free
> speciale: per l'heap è un blocco come tutti gli altri, e la fusione con i
> vicini funziona senza sapere nulla di tutto questo.

> ⚠️ `posix_memalign` **ritorna** il codice di errore e non lo mette in
> `errno`. È l'eccezione della famiglia, ed è il modo classico di sbagliare
> a usarla.

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
libctest       276 prove: allocatore (compresa l'allocazione allineata e
                la crescita dello heap fino al rifiuto), formattazione,
                flussi, salti non locali, conversioni, errno, virgola
                mobile, sscanf, data e ora, stat, ambiente, directory,
                temporanei, descrittori duplicati, variabili __thread,
                interfacce per il codice di terzi, spawn con
                redirezione — tutte dentro EX-OS
```

### Le pipe

`pipe()` dà due descrittori collegati da un buffer circolare di 4 KB nel
kernel. Le regole che contano sono tutte di confine:

> ⚠️ **Vuota con uno scrittore vivo = aspetta; vuota senza scrittori = 0.**
> È tutta qui la ragione per cui il conteggio degli scrittori esiste: senza,
> una pipe non è distinguibile da un blocco eterno. E scrivere quando non
> c'è più nessun lettore dà `EPIPE`, non un'attesa.

> ⚠️ **Niente `SIGPIPE`**: EX-OS non ha i segnali, quindi si vede solo il
> valore di ritorno. Chi non guarda quello di `write()` non se ne accorge.
> E **niente garanzia di atomicità**: la scrittura può essere parziale
> anche sotto `PIPE_BUF`.

`FD_PIPE_R` e `FD_PIPE_W` sono due **tipi** di descrittore, non uno con un
flag: la direzione non è un dettaglio, è ciò che decide se una `read`
blocca o è un errore.

Per collegare due processi serve passare un'estremità al figlio, e lo si fa
con `SpawnRedir` a `percorso` NULL — l'**eredità dei descrittori**, che è la
sola aggiunta che rende le pipe utili fuori da un singolo processo:

```c
int p[2]; pipe(p);
SpawnRedir a = { 1, 0, NULL, p[1] };   /* stdout del figlio = scrittura */
spawn_ex("/bin/cmd", argv, environ, &a, 1);
close(p[1]);                            /* ⚠️ indispensabile */
```

> ⚠️ **Il padre deve chiudere l'estremità che ha passato.** Se non lo fa, la
> pipe conta ancora uno scrittore vivo — lui — e chi legge aspetterà per
> sempre. È l'errore classico con le pipe, e qui non c'è niente che lo
> segnali.

**Un difetto di progetto che le pipe hanno fatto emergere.** I descrittori
si chiudevano in `proc_reap_zombie()`, cioè quando il genitore chiama
`waitpid()`. Con i soli file passava inosservato; con una pipe è uno
stallo: il figlio esce, i suoi fd restano contati, il padre è bloccato
nella `read` e non arriverà mai al `waitpid`. Due processi ad aspettarsi a
vicenda, nessun errore. Ora si chiudono alla morte del processo — è il
motivo per cui su Unix stanno in `do_exit()` e non in `wait()`: uno zombie
non deve trattenere risorse di I/O, solo il proprio codice di uscita.

### La rinomina che non sposta i dati

`rename()` era **copia+cancella**: costava quanto il file e **rialloca i
blocchi**. Portava il nome di un'altra cosa, e la differenza non era
accademica — è ciò che rendeva impossibile a `install` verificare la mappa
dei settori del kernel prima di dargli il nome definitivo.

Dalla 0.161 è una syscall vera (`vfs_rename`, implementata su tutti e tre i
driver: `fat.c`, `ext2.c`, `fat12.c`) che riscrive la voce di directory e
basta. **I blocchi non si spostano**: è la garanzia su cui si regge
l'installatore. C'è anche il comando:

```
ex-os:/> rename /r1.bin /r2.bin
/r1.bin -> /r2.bin
```

> ⚠️ **Si chiama `rename` e non `mv`, di proposito.** `mv` su Unix
> rinomina *e sposta*, e quando sposta fra filesystem copia e cancella —
> un'operazione completamente diversa sotto lo stesso nome. Qui i dati non
> si muovono mai, e il nome dice cosa fa.

> ⚠️ **Due differenze da POSIX, dichiarate:** solo nella **stessa
> directory** (ENOSYS), e **non sostituisce** la destinazione (EEXIST).
> Attraversare directory sarebbe una copia più una cancellazione;
> sostituire vuol dire cancellare un file che il chiamante non ha nominato
> come vittima.

In `ext2_rename` **si aggiunge prima e si toglie dopo**: in mezzo il file
ha due nomi, che è riparabile; nell'ordine opposto non ne avrebbe nessuno e
l'inode resterebbe allocato e irraggiungibile.

> ⚠️ **`fat12.c` risponde in errno, gli altri due no.** Lì `-2` significa
> `ENOENT`, in `fat.c` ed `ext2.c` significa «esiste già». Mescolare le due
> convenzioni fa dire «esiste già» a una rinomina di un file inesistente —
> ed è successo, alla prima prova con un nome sbagliato.

### `getrusage`, `getpagesize`, `mmap`

Le tre funzioni che GCC usa come programma ospite, ognuna con il proprio
limite dichiarato.

> ⚠️ **`getrusage` non è una misura.** EX-OS non tiene contabilità per
> processo: lo scheduler assegna quanti e non misura consumi. `ru_utime`
> riporta il tempo **trascorso dall'avvio** — un limite superiore onesto —
> e tutto il resto è zero. Chi ci costruisce sopra un profilo
> (`gcc -ftime-report`) otterrà per ogni passaggio lo stesso tempo.

> ⚠️ **`mmap` mappa solo memoria anonima**: con `fd != -1` dà `ENODEV`.
> Mappare un file richiederebbe le pagine sporche e il momento in cui
> riscriverle; una mmap che finge consegnando zeri darebbe un programma che
> legge dati sbagliati senza che niente lo segnali. E ritorna
> **`MAP_FAILED`, non `NULL`**.

`munmap` **riporta giù il confine** se la zona smappata era in cima: prima
le pagine fisiche tornavano al PMM ma lo spazio di indirizzamento no, e il
garbage collector di GCC fa quel ciclo migliaia di volte per file
compilato.

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

### GMP, MPFR e MPC

Le tre librerie a cui `cc1` si linka girano anche loro dentro EX-OS — sono
il codice di terzi più pesante che il sistema abbia ospitato, 4,6 MB di
archivi di aritmetica a precisione arbitraria:

```
ex-os:/> /cdrom/bin/provamp
GMP 6.3.0, MPFR 4.2.1, MPC 1.3.1 — dentro EX-OS

GMP   2^128 = 340282366920938463463374607431768211456
MPFR  pi     = 3.1415926535897932384626433832795028841971693993751e0
MPC   sqrt(i)= (7.0710678118654752440e-1 7.0710678118654752440e-1)
```

Costruirle è `tools/gcclibs-exos/prepara-gcclibs.sh`. Due cose non ovvie,
entrambe documentate lì:

- **`CC_FOR_BUILD=gcc` esplicito.** GMP sceglie il compilatore per i propri
  generatori di tabelle compilandone uno e *eseguendolo* — e la prova
  riesce con il cross, perché un binario di EX-OS è un ELF32 statico che
  Linux carica, con syscall che hanno i numeri di Linux. Parte, stampa,
  sembra a posto; non combaciano `argc` e `argv`, e il generatore si
  rifiuta di generare.
- **`--host=i486-pc-exos`, non i386.** Non è un ripiego: 486 è il minimo
  che EX-OS già richiede per conto suo (`invlpg` in `kernel/mm/paging.c`).
  Con `i386` si finisce su un difetto di GCC 17 che emette `rolw $8, %eax`
  — rotazione a 16 bit con il registro a 32 — e l'assemblatore rifiuta.

### La libreria standard del C++ gira

```
ex-os:/> /cdrom/bin/provacpp
La libreria standard del C++ dentro EX-OS

  vector+sort : 1 3 5 7 9
  string      : "std::string concatenata" (23 caratteri)
  cerchio     : area = 12566 (x1000)
  quadrato    : area = 9000 (x1000)
  eccezione   : lanciata e ripresa
  out_of_range : presa da dentro la libreria
```

**libstdc++ 25 MB e libsupc++ 1 MB** compilate per `i386-exos`. Il
programma si costruisce con una riga sola, con `g++`, come su qualunque
altro bersaglio:

```sh
i386-exos-g++ -O2 -o provacpp prova-cpp.cpp
```

> ⚠️ **Le eccezioni funzionano, e non era scontato.** Sono il pezzo che ha
> bisogno di più cose insieme: `__cxa_throw`, lo svolgimento dello stack,
> le tabelle `.eh_frame`, i descrittori di tipo. Lo svolgimento **legge a
> runtime le tabelle prodotte dal collegatore e le percorre**: è l'unica
> parte del C++ che pretende che il programma caricato in memoria sia
> esattamente come il collegatore l'ha descritto — quindi è anche una prova
> indiretta che il caricamento su richiesta di EX-OS è corretto.

**La riga che mancava da sempre: `extern "C"`.** Nessuno degli header di
EX-OS aveva la guardia `#ifdef __cplusplus`. Il C++ decora i nomi con i
tipi degli argomenti — `printf` diventa `_Z6printfPKcz` — mentre `libc.a` è
compilata da un compilatore C e dentro ha il nome nudo: **ogni programma
C++ chiamava simboli che nell'archivio non esistono.**

Il sintomo era fuorviante, perché arrivava in compilazione e non al link:
la libstdc++ dichiara alcune funzioni della libc con `extern "C"` esplicito,
e il messaggio diceva `conflicting declaration of 'void* memalign(...)'
with 'C' linkage` — cioè «questa dichiarazione è in conflitto con se
stessa».

### I nomi che servono a essere nominati

Perché la libstdc++ compili, la libc deve *dichiarare* molte cose che
EX-OS non farà mai: 40 codici errno di rete e IPC, le costanti `DT_*` per
FIFO, socket e collegamenti simbolici, i `S_IF*`/`S_IS*` corrispondenti.

> ⚠️ **Un nome mancante è un errore di compilazione, non un ramo morto.**
> `<system_error>` costruisce `std::errc` da quell'elenco e `<filesystem>`
> scrive `case DT_LNK:` in uno switch: serve la costante, non il
> comportamento. Ritornare sempre 0 da `S_ISLNK()` è la risposta **giusta**
> — su EX-OS un collegamento simbolico non esiste, quindi «questo file è un
> collegamento?» ha davvero risposta no.

I valori sono quelli di Linux e non vanno reinventati: il giorno che i
collegamenti simbolici arrivassero, `S_IFLNK` dovrà valere `0120000` come
ovunque.

### Collegare un programma C vero, dentro EX-OS

Il CD degli strumenti porta anche il **runtime del bersaglio** in
`/exos/lib`: `crt0.o`, `crti/crtn/crtbegin/crtend.o`, `libc.a`, `libgcc.a`,
`libm.a`. Sono oggetti `i386-exos` prodotti dal cross, quindi già codice di
EX-OS: `ld` nativo li legge qui dentro come li legge il cross su Linux.

⚠️ **Senza questi il driver arriva a metà.** `gcc -c` ha bisogno solo di
cpp, cc1 e as; `gcc -o programma` ha bisogno anche di crt0, `libgcc.a` e
`libc.a` — e senza si ottiene un errore di `ld` su simboli che non
c'entrano niente col sorgente che si stava compilando.

La prova è `prova-gcc.c`, sul CD insieme al suo assembly generato dal cross
(che fa anche da termine di paragone per quello che `cc1` dovrà produrre):

```
ex-os:/> /cdrom/bin/as -o /pg.o /cdrom/prova-gcc.s
ex-os:/> /cdrom/bin/ld -o /pg /cdrom/exos/lib/crt0.o /pg.o \
             /cdrom/exos/lib/libc.a /cdrom/exos/lib/libgcc.a
ex-os:/> /pg
La catena intera dentro EX-OS

  somma dei quadrati 1..10 : 385   (atteso 385)
  lunghezza del nome       : 5     (atteso 5)
  divisione a 64 bit       : 64   (atteso 64)

Compilato, assemblato e collegato qui dentro.
```

⚠️ **La divisione a 64 bit è lì apposta.** È una delle poche cose che il
compilatore non sa fare con un'istruzione: chiama `__divdi3` in `libgcc.a`.
Se libgcc non è stato collegato, il difetto si vede lì e solo lì. Gli altri
due valori sono attesi e scritti nel sorgente — un programma che stampa un
numero sbagliato senza che nessuno sappia quale fosse quello giusto è una
prova che non prova niente.

### `cc1` compila dentro EX-OS

`as` traduce, `ld` collega, le tre librerie di calcolo ci sono, la libm c'è,
**libstdc++ gira** — e **`cc1` compila**. Il compilatore C di GCC,
costruito in canadian cross
(`--build=x86_64-linux --host=i386-exos --target=i386-exos`), legge un
sorgente C dentro EX-OS e ne produce l'assembly, che `as` e `ld`
trasformano in un eseguibile.

**⚠️ Il binario va ricostruito.** La prova è stata fatta prima del
passaggio a `time_t` a 64 bit; dopo quel cambiamento `cc1` è stato solo
**rilinkato**, e gli oggetti già compilati continuavano a credere che
`struct timeval` fosse di 8 byte mentre la libc nuova ne scrive 12 — pila
corrotta, sistema fermo. È il promemoria che un cambio di ABI non si
risolve con un link. La ricostruzione completa è **fatta** — il binario
nuovo c'è — ma non è ancora stata riprovata dentro EX-OS: finché non lo
sarà, la riga qui sopra dice «da riprovare» e non «testato».

Sono 41 MB di binario, e girano solo grazie al caricamento su richiesta
(vedi *Le pagine di un programma arrivano quando servono*): il costo
d'avvio non dipende dalla dimensione, e la memoria restituita al kernel
tiene il resto sotto controllo.

Arrivarci ha scoperto tre difetti nella libc, tutti invisibili ai
programmi di EX-OS perché nessuno di loro fa quello che fa un compilatore:

| | |
|---|---|
| `realloc` non ingrandiva **mai** sul posto | la fusione col blocco successivo rifiutava i blocchi non liberi — cioè esattamente il caso da gestire. Si vedeva come un salto a `0xa7a6a5a4`, che sono i byte di riempimento del test letti come puntatore |
| i costruttori globali non venivano chiamati | `cc1` ha 57 voci in `.init_array`; la prima struttura usata era vuota |
| `printf` con `%f` inventava cifre | oltre la diciottesima, e arrotondava 2,5 a 3 invece che a 2 |

### Un binario di terzi porta dentro la libc del giorno in cui è stato collegato

Qui non ci sono librerie condivise: `as`, `ld` e `cc1` hanno **una copia
della libc dentro di sé**, quella con cui sono stati collegati. Correggere
`lib/libc.c` non li tocca. Sembra ovvio detto così, e non lo è affatto
quando il difetto corretto è nell'allocatore.

`ld` andava in page fault appena gli si davano degli archivi da collegare:

```
[FAULT] PID 9 '/cdrom/bin/ld': page fault a 0x00000005
        (protezione, scrittura, EIP=0x080d2e16)
```

L'indirizzo si risolve sul binario non strippato, e non è codice di
binutils:

```
EIP 0x080d2e16  ->  malloc + 0x116
```

⚠️ **La conferma sta nella tabella dei simboli, non nel ragionamento.**
Dentro `ld` c'era `heap_fondi_con_succ` e **non** `heap_assorbi_succ` —
cioè la funzione che la correzione di `realloc` ha introdotto. Quel
binario è del 2 agosto: si porta dentro la libc in cui `realloc` non
ingrandiva **mai** sul posto, e il chiamante che credeva di avere più
spazio scriveva oltre la fine. La corruzione non si vede dove nasce, si
vede alla `malloc` successiva.

Collegare un solo `.o` passava: poco traffico di `realloc`. Con `libc.a` e
`libgcc.a` da leggere, bfd fa crescere le tabelle dei simboli e arriva.

#### Il ricollegamento da solo non basta, e crederlo costa un secondo difetto

La prima risposta è stata ricollegare `as` e `ld` contro la libc corretta,
senza ricompilarli: l'allocatore è *implementazione*, l'ABI non cambia,
quindi il relink dovrebbe bastare. `ld` ha smesso di andare in fault e ha
collegato gli archivi. **E `as` ha cominciato a saltare a un indirizzo a
caso** (`EIP=0x6a722690`, pagina assente).

Il ragionamento aveva una premessa non verificata: *fra il 2 agosto e oggi
è cambiata solo l'implementazione*. Non è vero.

```
oggetti dei binutils   2 agosto    typedef long      time_t;
libc di oggi                       typedef long long time_t;
```

`struct stat` contiene **tre** campi `time_t`: è cresciuta di dodici byte
e ha spostato tutti gli offset successivi. Un oggetto compilato con
l'header vecchio la legge alla vecchia maniera mentre la libc nuova la
scrive alla nuova — e quello che ne esce, se finisce in un puntatore a
funzione, è esattamente un salto a `0x6a722690`.

> ⚠️ **È lo stesso difetto di `cc1`, non un altro.** Là l'avevo capito
> subito perché il cambio di `time_t` era fresco; qui l'avevo dimenticato
> e ho concluso «basta ricollegare» **prima** di verificarlo. La
> ricostruzione completa dei binutils è l'unica risposta giusta, come per
> `cc1`.

> ⚠️ **`ld` ricollegato ha funzionato lo stesso**, e questa è la parte
> istruttiva: nel percorso che collega archivi la `struct stat` sbagliata
> non viene toccata. «Ha funzionato una volta» non è una prova di
> correttezza — è una prova che quel percorso non passa di lì.

Regola generale, valida per qualunque cosa verrà portata qui dentro:

| cos'è cambiato nella libc | cosa basta |
|---|---|
| solo `lib/libc.c` (implementazione) | ricollegare |
| anche `lib/include/libc.h` (tipi, strutture) | **ricompilare tutto** |

Il modo di accorgersi del primo caso è cercare nella tabella dei simboli
una funzione che esiste solo dopo la correzione. Il modo di accorgersi del
secondo è guardare `git diff` sull'header **prima** di decidere, che è
esattamente il passo che qui è saltato.

**Cosa manca ancora:** il programma di guida `gcc`, quello che concatena
`cc1 → as → ld` passando i file intermedi. Oggi i tre passi si danno a
mano.

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
/bin/                 as, ld, cc1 e i programmi di prova
```

`/exos/` non è documentazione: è ciò che serve per **compilare su EX-OS**.
`/bin` non è più vuota: ci stanno `as` e `ld` di binutils 2.44 e `cc1` di
GCC, cioè la catena di compilazione vera — vedi
[La catena di compilazione dentro EX-OS](#la-catena-di-compilazione-dentro-ex-os).

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

### `hwconfig` — configurare senza leggere niente

```
hwconfig            guarda, propone, chiede, scrive
hwconfig -n         guarda e basta
hwconfig /disco     configura il sistema installato lì dentro
```

`kernel.cfg` si scrive a mano, e per scriverlo bisogna già sapere che i
dischi si chiamano `hd0p1`, che i punti di montaggio non devono esistere,
che i moduli sono processi ring3 e che l'ordine dei comandi di rete non è
modificabile. Sono tutte cose vere, tutte documentate qui sopra, e tutte da
leggere **prima** di poter accendere una macchina.

`hwconfig` le sa già:

```
Cosa c'e' in questa macchina

  tastiera   /dev/kbd.drv — si carica all'avvio, serve alle frecce e a gfedit
  lettore    cd0 — montato all'avvio su /cdrom
  volume     hd0p1  ext2   'dati' — montato su /dati
  rete       scheda Ethernet sul bus PCI — si accende all'avvio
```

Poi mostra i due file che scriverebbe, per intero, e chiede. Il round trip
è verificato: analizza, scrive, e la macchina riparte dal disco con la rete
accesa **senza un solo `[WARN]`**.

⚠️ **I file di prima finiscono in `.bak`**, ed è ciò che rende la proposta
accettabile: se la macchina non riparte, quello di prima è lì accanto.

⚠️ **Il nuovo file è generato, non modificato.** Il `kernel.cfg` che viene
col sistema è lungo duecento righe di spiegazioni; conservarle vorrebbe
dire un parser INI che le rimette a posto, cioè un programma molto più
grande e con molti più modi di sbagliare. Quello scritto qui è corto e
sostituisce il precedente per intero — detto in chiaro **prima** di
chiedere.

Tre scelte che si vedono solo provandolo:

| | |
|---|---|
| **non guarda quale scheda sia** | la tabella dei modelli sta in `netdetect`, e duplicarla darebbe due elenchi che divergono al primo driver nuovo. L'autoexec generato chiama `netdetect -c`, che quella tabella ce l'ha: a `hwconfig` serve sapere **se** c'è una scheda, non quale |
| **il volume che sarà la radice non finisce in `[mount]`** | il kernel se ne accorgerebbe da solo («è già montato altrove»), ma è una riga che non serve dentro un file che qualcuno leggerà per capire la propria macchina |
| **un'etichetta sfortunata non diventa un punto di montaggio** | un volume etichettato `boot` darebbe `/boot = hd0p1`, che il kernel rifiuta — un `[WARN]` a ogni accensione, e chi lo legge non ha motivo di sospettare l'etichetta del disco. In quel caso si ripiega su `/disco` |

Scrive anche `TMPDIR`, che non è un vezzo: `mkstemp` e il driver del
compilatore ci mettono i file di passaggio, e senza finiscono nella radice
— che avviando da CD è in sola lettura.

Sta **sul floppy**, con `fdisk` e `install`: serve proprio quando si prepara
una macchina, cioè quando il CD magari non c'è ancora. Senza `/dev/pci.drv`
configura montaggi e moduli e dice che la parte di rete non ha potuto
verificarla.

### `help helpconfig` — la procedura, e a che punto sei

```
help helpconfig     (oppure `helpconfig` da solo)
```

Spiega come si accendono i driver — la catena di rete, la configurazione a
mano, la diagnosi, l'autoexec — e **mostra lo stato attuale** chiedendo al
registro IPC chi c'è già:

```
A che punto sei adesso

  [ok]    bus PCI          /dev/pci.drv &
  [manca] scheda di rete   netdetect -c
  [manca] stack IP         /dev/ip.drv &
  [ok]    tastiera         [modules] in /boot/kernel.cfg
```

⚠️ **Lo stato è il motivo per cui esiste.** Un elenco di comandi da dare sta
già in questo file; quello che al prompt non si sa è a che punto si è
arrivati. Costa una syscall per servizio e trasforma «ecco la procedura» in
«sei qui». L'esempio sopra è una macchina senza scheda di rete: il bus c'è,
la scheda no, e non è un guasto da inseguire.

Il testo è più lungo di uno schermo da 25 righe e si ferma da solo; le pause
stanno dove cambia argomento, non ogni N righe, perché una pagina
interrotta a metà di un elenco è peggio di una più corta. `q` smette.

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

### Accessi I/O a 16 e 32 bit

Oltre a `ioport_in`/`ioport_out`, che lavorano a byte, un driver ha:

```c
int ioport_in16 (unsigned int porta);                    /* 0..65535, o -errno */
int ioport_out16(unsigned int porta, unsigned int val);
int ioport_in32 (unsigned int porta, unsigned int *out); /* 0, o -errno        */
int ioport_out32(unsigned int porta, unsigned int val);
```

Non sono una comodità. Il registro CONFIG_ADDRESS del bus PCI (0xCF8) **deve**
essere scritto con un singolo accesso a 32 bit: uno a byte o a word non viene
riconosciuto dal ponte come ciclo di configurazione, e siccome 0xCF9 è il
registro di reset di molti chipset, scriverlo a pezzi tende a riavviare la
macchina.

⚠️ `ioport_in32` **non restituisce il valore letto**: `0xFFFFFFFF` («nessun
dispositivo») come `int` sarebbe `-1`, indistinguibile da un errore. Il valore
esce dal puntatore. `ioport_in16` non ha il problema e lo restituisce.

La porta deve essere allineata all'ampiezza, altrimenti `-EINVAL`: un accesso
disallineato viene spezzato dal chipset in due cicli e sul bus PCI il secondo
non è più un ciclo di configurazione.

### Il bus PCI: `/dev/pci.drv` e `/bin/netdetect`

L'enumerazione PCI è un **processo ring3**, non codice del kernel: legge
tabelle scritte da BIOS e firmware di terzi, e un ciclo che non termina su un
ponte mal formato dev'essere un processo da rilanciare, non una macchina
bloccata.

```
/dev/pci.drv -l          elenca i dispositivi ed esce
/dev/pci.drv &           registra il servizio "pci" e serve i client
netdetect                schede di rete presenti e driver di ciascuna
netdetect -t             tabella dei modelli riconosciuti
```

Esempio (VirtualBox con la scheda predefinita):

```
ex-os:/> /dev/pci.drv &
[1] 8
pci: servizio 'pci' attivo, 8 dispositivi
ex-os:/> netdetect
00:04.0  1022:2000  AMD PCnet-PCI II / FAST III (Am79C970/C973)
           porte I/O da 0xc140, IRQ 11
           driver: /dev/pcnet.drv
```

Il protocollo è in `drivers/pci/pci_proto.h`. Il server espone lettura della
configurazione e `PCI_MSG_ABILITA`/`DISABILITA` sui bit I/O, memoria e bus
master; **non** una scrittura di configurazione generica, perché riprogrammare
i BAR di un dispositivo che il kernel sta usando (per esempio il controller
ATA) toglierebbe il disco da sotto i piedi a chi di quella scrittura non sa
nulla.

Entrambi vivono **solo sul CD di EX-OS** (`make iso-exos`): il floppy serve ad
avviare e a installare, e gli strumenti di rete senza i driver di rete — che
sul floppy non ci starebbero — non servirebbero a niente una volta lì.

### Interrupt: `irq_bind` e `irq_done` vanno in coppia

```c
irq_bind(11);                    /* da qui gli IRQ arrivano come IPC */
...
/* alla notifica: */
servi_la_scheda();               /* azzera lo stato del dispositivo */
irq_done(11);                    /* SOLO ADESSO si riapre la linea */
```

⚠️ `irq_done()` non è facoltativa. Il kernel **maschera** l'IRQ nel PIC
prima di consegnare la notifica, e senza questa chiamata la linea resta
chiusa: il driver riceve un interrupt e poi silenzio.

Il motivo è che un driver ring3 non gira dentro l'interrupt: fra la
notifica e il momento in cui tocca la scheda passano dei tick. Su un IRQ
**a livello** — tutti quelli PCI — il dispositivo tiene la linea alta
finché non gli si azzera il registro di stato, quindi senza mascheramento
l'interrupt riparte subito dopo l'`iret` e il processo driver non riceve
mai la CPU per andare ad azzerarlo. La macchina si ferma, senza panic.

L'ordine conta: prima si serve il dispositivo, poi si riapre. Riaprire con
la linea ancora alta rimette in piedi la tempesta.

### Rete: `/dev/ne2k.drv` e `/bin/nettest`

Il primo driver di rete guida la famiglia NE2000/DP8390 — RTL8029 su PCI e
cloni Winbond/VIA/KTI. Sta in userspace perché quella scheda **non fa DMA
verso la memoria di sistema**: la RAM dei pacchetti è sulla scheda e ci si
arriva da una porta di I/O, quindi il kernel non deve sapere niente di
indirizzi fisici o pagine bloccate.

```
ex-os:/> /dev/pci.drv &
ex-os:/> netdetect -c              # sceglie e avvia il driver giusto
ex-os:/> nettest -a 10.0.2.2
chi ha 10.0.2.2? lo chiede 52:54:00:12:34:56 (10.0.2.15)

  64 byte  52:55:0a:00:02:02 -> 52:54:00:12:34:56  ARP (0x0806)
      ARP risposta: 10.0.2.2 e' 52:55:0a:00:02:02, cerca 10.0.2.15

Risposta ricevuta: 10.0.2.2 ha indirizzo 52:55:0a:00:02:02
```

`nettest` usa **ARP e non ping** di proposito: ARP è il primo scambio
possibile senza avere uno stack, e una risposta dimostra in un colpo solo
che la scheda trasmette, che il frame arriva, che la scheda riceve e che
la catena driver → IPC → programma consegna i byte giusti. Se ARP
funziona, a `ping` manca solo software.

Il protocollo (`drivers/net/net_proto.h`) è quello di **ogni** driver di
rete, non della NE2000: il PCnet parlerà la stessa lingua. Due scelte che
valgono per tutti:

- **Il driver non spinge mai un frame non richiesto.** `ipc_send` blocca
  se la mailbox del destinatario è piena, e driver e stack che si spingono
  dati a vicenda finiscono fermi ognuno dentro la propria `ipc_send`. Si
  usa domanda e risposta (`NET_MSG_RICEVI`), come il driver di tastiera.
- **Un battito ogni 250 ms.** `ipc_notify_irq` non blocca: se la mailbox
  del driver è piena quando arriva l'interrupt, la notifica viene
  scartata e la linea resterebbe mascherata per sempre. La scadenza
  sull'attesa fa sì che una notifica persa costi un ritardo, non
  un'interfaccia morta.

⚠️ Una NE2000 **ISA** non si cerca da sola: per riconoscerla bisognerebbe
scrivere sulla sua porta di reset, e se lì c'è un'altra scheda le si
scrive addosso. Va dichiarata: `/dev/ne2k.drv -p 0x300 -q 3`.

### Rete: `/dev/pcnet.drv` — la prima scheda che scrive in RAM da sola

AMD PCnet-PCI II / FAST III (Am79C970, C970A, C971, C972, C973: sul bus si
presentano tutte come `1022:2000`). Parla lo stesso protocollo del ne2k,
quindi lo stack IP non sa quale delle due c'è sotto.

⚠️ **La differenza con la NE2000 è tutto.** Quella tiene la memoria dei
pacchetti *sulla scheda*, e ci si arriva da una porta di I/O: per questo è
stato il primo driver: non chiedeva niente di nuovo al sistema. Il PCnet è
un **bus master**: legge e scrive la RAM di sistema da solo, agli indirizzi
**fisici** che gli si sono dati, senza passare dalla MMU.

Da qui due cose che prima non esistevano:

| | |
|---|---|
| il bit **bus master** nel comando PCI | senza, il ponte blocca ogni ciclo che la scheda inizia. I registri si leggono e si scrivono benissimo — quelli passano da noi — ma la scheda non riesce nemmeno a leggere il proprio blocco di inizializzazione |
| **`SYS_DMA_ALLOC`** | memoria fisicamente contigua di cui si conosca l'indirizzo fisico |

> ⚠️ **Un indirizzo sbagliato qui non dà un errore.** Dare alla scheda un
> indirizzo virtuale invece di uno fisico non produce un fault e non ferma
> niente: produce una scheda che scrive pacchetti in un punto a caso della
> memoria fisica. Su una macchina piccola quel punto è spesso il kernel, e
> il sintomo arriva minuti dopo, altrove. È il motivo per cui `dma_alloc`
> restituisce i due indirizzi separati e con nomi diversi — `virt` per il
> processo, `fisico` per la scheda.

`SYS_DMA_ALLOC` la può chiedere **solo chi ha già una finestra di porte
I/O**. Non è una difesa rigorosa: è il modo di dire che serve ai driver.
Memoria contigua e non liberabile è la risorsa più scarsa che ci sia, e il
tetto è 64 pagine per processo.

Due trappole del formato, entrambe silenziose:

- **BCNT è in complemento a due** su dodici bit, con i quattro bit sopra a
  uno. Un buffer da 2048 byte si dichiara `(-2048) & 0xFFF | 0xF000`;
  scriverci 2048 in chiaro dà una scheda che crede di avere un buffer di
  2048 byte *negativi*.
- **Il reset si fa in WIO**, prima del passaggio a 32 bit, perché
  l'offset del registro di reset è diverso nei due modi.

```
ex-os:/> nettest -c
inviati        7
ricevuti       7
notifiche IRQ  7
battiti        89
```

⚠️ **`notifiche IRQ 7` su 7 frame è la riga che conta**, non `ricevuti 7`.
Il driver guarda la scheda anche a ogni battito: senza quel numero, una
rete che funziona con 250 ms di ritardo sarebbe indistinguibile da una che
funziona. È lo stesso controllo che ha scoperto la cascata del PIC mai
smascherata.

⚠️ **`-l` non sonda una scheda già guidata.** Per leggerne lo stato
bisognerebbe resettarla, e se un altro processo la sta usando quel reset
gli porta via la rete senza dare un errore a nessuno dei due. Se il
servizio c'è già, `-l` lo *interroga*: la risposta viene da chi la scheda
la sta usando davvero. È successo alla prima prova, e il sintomo era
illeggibile — `CSR0 = 0x3b, atteso STOP`.

### Lo stack IPv4: `/dev/ip.drv`, `ping`, `ipcfg`

ARP, IPv4 e ICMP stanno in un **processo a sé**, non nel driver:

```
ping ──IPC──> ip.drv ──IPC──> ne2k.drv ──porte I/O──> scheda
```

Tre ragioni, tutte pratiche: questi protocolli sono uguali su qualunque
scheda (metterli nel driver vorrebbe dire riscriverli per la PCnet); lo
stack ha dei **tempi** — scadenze ARP, attese di risposta — mentre il
driver deve solo rispondere all'hardware; e se sbaglia lo stack si riavvia
lo stack, mentre la scheda resta accesa e configurata.

```
ex-os:/> /dev/pci.drv &
ex-os:/> netdetect -c
ex-os:/> /dev/ip.drv &
ip: indirizzo  10.0.2.15
    maschera   255.255.255.0
    gateway    10.0.2.2
ip: servizio 'ip' attivo

ex-os:/> ping 8.8.8.8 -n 3
  60 byte da 8.8.8.8: seq=1 ttl=255 tempo=70 ms
  60 byte da 8.8.8.8: seq=2 ttl=255 tempo=50 ms
  60 byte da 8.8.8.8: seq=3 ttl=255 tempo=60 ms

3 inviati, 3 ricevuti, 0% persi
tempi: minimo 50 ms, medio 60 ms, massimo 70 ms
```

`ipcfg` mostra indirizzo e **contatori**, `ipcfg -r` la tabella ARP.
I contatori sono metà del programma: quando la rete non va, la domanda
non è «va o non va» ma dove si ferma, e `IP ricevuti` a zero, `scartati`
che salgono o `somme errate` che salgono indicano tre punti diversi.

Cosa lo stack **non** fa, detto subito:

- **non frammenta e non riassembla** — un datagramma più grande della MTU
  viene rifiutato, uno in arrivo che è un frammento viene contato e
  scartato;
- **nessuna tabella di routing** — c'è una rete locale e un gateway;
- **niente DHCP dentro lo stack** — l'indirizzo si dichiara (`ip.drv -a …
  -m … -g …` o `ipcfg -a …`); a prenderlo da un server ci pensa il
  programma `dhcp`, che sta sopra UDP come un client qualunque;
- **una richiesta echo per volta** — `ping` è sequenziale per natura.

⚠️ `ping` distingue **tre** esiti, non due: risposta ricevuta; nessuna
risposta all'**ARP** (a quell'indirizzo non c'è nessuno — non si è nemmeno
usciti dal cavo); nessuna risposta all'**echo** (il pacchetto è partito, il
problema è più in là). Un unico «non raggiungibile» costringerebbe a
rifare la diagnosi da capo ogni volta.

⚠️ Un tempo di `<10 ms` non è uno zero: `uptime_ms()` conta i tick del PIT
a 100 Hz, quindi avanza a scatti di 10 ms. Scrivere «0 ms» dichiarerebbe
una precisione che non c'è.

### Nomi lunghi su FAT (VFAT), in lettura

Un FAT32 scritto da Linux o da Windows si legge con i nomi veri:

```
ex-os:/> ls /disco
appunti di riunione.txt 19
UnNomeMoltoLungoDavveroInterminabile.dati 19

ex-os:/> cat "/disco/appunti di riunione.txt"     funziona
ex-os:/> cat /disco/UNNOME~1.DAT                  funziona anche l'alias
```

Funzionano **entrambe** le vie: il nome lungo e l'alias 8.3. Confrontare
solo col lungo renderebbe impossibile aprire un file col suo alias corto,
che è un nome legittimo e che i programmi vecchi usano.

⚠️ **La somma di controllo non è facoltativa.** Ogni voce di nome lungo
porta la somma del nome 8.3 a cui appartiene, e serve a riconoscere le
catene **orfane**: un sistema che non conosce i nomi lunghi può cancellare
la voce 8.3 lasciando indietro i suoi frammenti, e attaccarli al primo
nome 8.3 che capita darebbe a un file il nome di un altro.

⚠️ **Solo lettura.** Creare un file con un nome lungo vorrebbe dire
allocare più voci consecutive e inventare un alias 8.3 unico (`NOME~1`,
`NOME~2`…): è un'altra cosa, e non c'è. Un file creato da EX-OS ha un nome
8.3, e si vede.

⚠️ **Solo ASCII**: i caratteri sopra `0x7F` diventano `?`. EX-OS non ha una
tabella di caratteri, e inventarne una qui vorrebbe dire scegliere una
codifica per tutto il sistema.

### `mkfs` sceglie il filesystem dalla dimensione

```
mkfs hd0p1        fino a 2 GB → FAT16, oltre → FAT32
mkfs -t ext2 hd0p1
```

Non è una soglia arbitraria: FAT16 arriva a **65524 cluster**, che con
cluster da 32 KB fanno poco più di 2 GB. Sotto quella misura FAT16 è
preferibile — tabella metà più piccola e root directory a dimensione
fissa, cioè meno settori da leggere per fare la stessa cosa.

⚠️ **ext2 non entra mai nella scelta automatica**: è un formato che si
chiede, non uno in cui si finisce.

### UDP e DHCP

Lo stack fa anche UDP. Non ci sono prese né descrittori: si apre una
**porta**, e da quel momento i datagrammi per quella porta sono di chi
l'ha aperta. Basta a un client DHCP e a un futuro risolutore DNS; una vera
API a prese si costruirà sopra questa, non al posto suo.

```
ex-os:/> dhcp
dhcp: cerco un server (52:54:00:12:34:56)...
dhcp: offerta di 192.168.76.9: 192.168.76.30

  indirizzo  192.168.76.30
  maschera   255.255.255.0
  gateway    192.168.76.9
  DNS        192.168.76.3
  concessione 86400 s

dhcp: configurato.
```

`dhcp` è un **programma**, non un pezzo dello stack: DHCP sta sopra UDP
come un client DNS, e un errore lì dentro fa fallire un comando invece di
spegnere la rete. `dhcp -n` chiede e stampa senza applicare.

⚠️ **Non rinnova la concessione.** Quando scade, va rilanciato. Il rinnovo
vuole un processo che resti acceso a metà del tempo di scadenza, cioè un
programma diverso da questo — che deve poter essere lanciato a mano e
finire.

⚠️ Un datagramma per una porta aperta ma senza nessuno in attesa viene
**scartato e contato** (`ipcfg` lo mostra). UDP perde pacchetti per
definizione, e una coda che cresce mentre nessuno legge è un modo lento di
finire la memoria per colpa di chi manda. Chi aspetta un datagramma deve
prenotarne la ricezione **prima** di mandare la richiesta.

### `printf` in virgola mobile

`%f`, `%e`, `%g` e le loro maiuscole, con larghezza, precisione e flag.
Prima consumavano l'argomento e stampavano `<float>`.

⚠️ **Le cifre significative si fermano a 18, e oltre si stampano zeri.** È
un numero **misurato**, non stimato: confrontando il motore con glibc su
una dozzina di valori, fino a 18 non c'è una discordanza, a 19 compare la
prima. Un `double` porta al massimo 17 cifre di informazione; quello che
c'è oltre è l'espansione esatta del valore *binario*, che glibc stampa con
un'aritmetica a precisione arbitraria e noi no:

```
printf("%.30f", 0.1)
  glibc  0.100000000000000005551115123126
  EX-OS  0.100000000000000000000000000000
```

Le prime 17 cifre coincidono — è tutto ciò che `0.1` contiene.

⚠️ **L'arrotondamento è al pari**, come prescrive lo standard: `%.0f` di
2.5 dà `2`, di 3.5 dà `4`. Con la regola ingenua («da 5 in su sale»),
sommare una colonna di valori arrotondati accumula un errore che cresce
col numero di righe.

**Su 399 confronti con glibc, 390 identici**; i 9 restanti compaiono solo
chiedendo più di 18 cifre significative.

### ⚠️ `time_t` è a 64 bit

Non solo per il 2038 — che pure è una scadenza da non scriversi in
partenza nel 2026. Il difetto che l'ha reso urgente è aritmetico: GCC
misura il tempo con

```c
now->wall = tv.tv_sec * 1000000000 + tv.tv_usec * 1000;
```

e con `tv_sec` a 32 bit quella moltiplicazione **trabocca prima di essere
allargata**. Il rapporto dei tempi di `cc1` usciva con fasi da 18 miliardi
di secondi. Non è codice di GCC da correggere: è codice giusto su un
`time_t` giusto.

⚠️ E `gettimeofday` prende ora **secondi e microsecondi dalla stessa
sorgente**. Prima i secondi venivano dall'orologio CMOS e i microsecondi
dal contatore dei tick: due orologi indipendenti, e la coppia poteva
**tornare indietro**. Un orologio che torna indietro non dà un errore, dà
intervalli negativi a chi sottrae due istanti. Il prezzo dichiarato: se
qualcuno corregge l'ora di sistema mentre un programma gira, `gettimeofday`
non se ne accorge — un orologio che non torna mai indietro vale di più.

### `/boot/autoexec.sh` — comandi all'avvio

Una riga = un comando, eseguito **esattamente come se fosse digitato**:
stessi built-in, stesse virgolette, stesso `&` per il background. Le righe
vuote e quelle che cominciano con `#` si saltano; una riga che comincia
con `@` viene eseguita senza essere stampata, come nell'autoexec del DOS.

Sul CD di EX-OS ce n'è uno che accende la rete da solo:

```
autoexec> /dev/pci.drv &
autoexec> netdetect -c
autoexec> /dev/ip.drv &
autoexec> dhcp
```

Dopo l'avvio `ping` e `ftp` funzionano senza toccare niente.

⚠️ **Lo esegue solo la shell della PRIMA console.** EX-OS ne avvia una per
ognuna delle quattro console virtuali: senza questo controllo l'autoexec
girerebbe quattro volte, e per `/dev/pci.drv &` significherebbe quattro
processi che si contendono lo stesso servizio.

⚠️ **La via d'uscita esiste prima di servire.** Un autoexec con dentro un
comando che si blocca renderebbe il sistema inutilizzabile, e il file per
correggerlo sta sul supporto che non si raggiunge più. Quindi:

| | |
|---|---|
| `autoexec=0` in `kernel.cfg` | lo salta (il file si modifica da un'altra macchina) |
| **Alt+F2, Alt+F3, Alt+F4** | danno sempre una shell pulita, anche mentre la prima è impegnata |

Il secondo è quello che conta davvero: non richiede di poter modificare
nulla.

### `!silenced` — l'`echo off` degli script

```
!silenced      da qui in poi i comandi non si vedono piu'
!verbose       si tornano a vedere
@comando       zittisce UNA riga sola
```

⚠️ **Zittisce il comando, non il suo risultato**, ed è la distinzione che
rende l'opzione utile: quello che un comando stampa è il motivo per cui lo
si è messo nello script, mentre la riga di comando la si è già scritta.
L'autoexec del CD comincia con `!silenced` e mostra solo l'indirizzo
ottenuto, non i quattro comandi che sono serviti a ottenerlo.

Vale **da dove sta in poi**, non per tutto il file: si può zittire la parte
rumorosa e lasciar vedere quella che interessa. La riga della direttiva non
si stampa mai.

Gli script non sono più solo l'autoexec:

```
source /prova.sh      esegue in QUESTA shell
/prova.sh             lo stesso, per nome
```

⚠️ **`source` e non una spawn**: i comandi devono girare nella shell
corrente, altrimenti un `cd` o un `export` dentro lo script sparirebbero
insieme al processo figlio. Un nome che finisce in `.sh` si riconosce
*prima* di provare a lanciarlo, non dopo che la spawn è fallita: la spawn
fallisce per molti motivi, e trattarli tutti come «sarà uno script»
trasforma un errore preciso in un secondo errore che parla d'altro.

### Cronologia dei comandi e modifica della riga

Le frecce **su** e **giù** ripercorrono i comandi già dati (24 di
cronologia); **sinistra**, **destra**, **Home**, **Fine**, **Backspace** e
**Canc** modificano la riga in corso. `Ctrl+C` la abbandona.

⚠️ **La riga in corso non si perde.** Chi ha scritto mezzo comando e va a
cercarne uno vecchio con la freccia in su la ritrova scendendo fino in
fondo.

Righe vuote e doppioni consecutivi non entrano in cronologia: chi ripete
lo stesso comando dieci volte non vuole dieci voci da riattraversare.

⚠️ **Serve la modalità raw della tastiera**, perché in cooked il driver
assembla la riga e la consegna su Invio — le frecce non hanno modo di
attraversare un flusso di testo. La shell prende quindi la disciplina di
riga su di sé: eco, backspace, cursore.

⚠️ **Se il servizio `kbd` non risponde si torna a leggere righe intere**:
si perde la cronologia, non la shell. E il driver torna in cooked da solo
ogni volta che un programma legge da stdin, quindi la modalità si
riafferma a ogni prompt — il che la rende anche autoriparante.

⚠️ **Solo la console in primo piano** prende i tasti. Senza quel
controllo tutte e quattro le shell si contendevano la tastiera, e quelle
non visibili la riportavano in cooked togliendola a chi stava scrivendo.

⚠️ Il ridisegno usa **solo Backspace**, perché il TTY di EX-OS non ha un
linguaggio di posizionamento del cursore. Conseguenza: su una riga più
lunga della larghezza dello schermo la modifica si vede male — ma la riga
resta corretta, e quello che si legge è ciò che verrà eseguito.

### Nomi con spazi: le virgolette

```
ex-os:/> cat "/disco/appunti di riunione.txt"
contenuto di prova
ex-os:/> cp '/disco/appunti di riunione.txt' /disco/copia.txt
copiati 19 byte in /disco/copia.txt
```

⚠️ **Apici singoli e doppi fanno la stessa cosa.** Su una shell Unix la
differenza esiste perché fra virgolette doppie `$VAR` viene espansa e fra
apici singoli no. Qui non c'è nessuna espansione — né di variabili né di
caratteri jolly — quindi le due forme non avrebbero niente da
distinguere. Accettarle entrambe e trattarle uguale è onesto; accettarne
una sola costringerebbe a ricordare quale.

Una virgoletta non chiusa viene **segnalata**: prima l'argomento si
prendeva fino a fine riga in silenzio, e il comando falliva lamentando un
file inesistente dal nome assurdo — il difetto era nella riga, non nel
file. Lo stesso vale per gli argomenti oltre il sedicesimo, che prima
sparivano senza dire niente.

### `ls` — modi di visualizzazione

```
ls -h              elenca tutte le opzioni
ls -mc /bin        a colonne: solo i nomi, il piu' compatto
ls -d              dettagli: dimensione, data e ora
ls -md             dettagliato stile dir: aggiunge gli attributi
ls -a              mostra anche i nomi che cominciano con un punto
ls -p              una pagina per volta (Invio avanza, q smette)
```

```
ex-os:/> ls -md /
data        ora    attr   dimensione  nome
2026-08-04  10:51  D----       <DIR>  BOOT
2026-08-04  10:51  D----       <DIR>  BIN
2026-08-04  10:51  -----      180584  KERNEL.BIN

2 file, 181679 byte    4 directory
```

⚠️ **`-d` qui significa «dettagli»**, non quello che significa su Unix (dove
`ls -d` mostra la directory invece del contenuto). È una scelta di questo
progetto, e l'aiuto la dichiara perché nessuno la scopra per tentativi.

⚠️ Senza `-a` si nascondono i nomi che cominciano con un punto, `.` e `..`
compresi. È un cambiamento rispetto a prima, quando venivano sempre
mostrati. Su un CD `.` e `..` non compaiono comunque: ISO 9660 non li
consegna (sono due record con nome `0x00` e `0x01`, e il driver li salta).

⚠️ Il bit «nascosto» di FAT si **vede** con `-md` ma non nasconde niente.
Guardarlo costerebbe una `statraw()` per ogni voce anche quando si
stampano solo i nomi, e su un floppy si sente; a nascondere è il punto
iniziale, che è la convenzione di tutti i filesystem che EX-OS monta.

**Le date arrivano davvero dal filesystem** (kernel 0.168). Prima
`sys_stat` scriveva zero e nessun programma poteva mostrarle. Ora:

| | |
|---|---|
| FAT12 / FAT16 / FAT32 | il formato è quello nativo, nessuna conversione |
| ext2 | da `i_mtime` (tempo Unix) al formato FAT |
| ISO 9660 | dai sette byte del record di directory |

⚠️ Una data **zero significa «questo volume non la tiene»** e i programmi
stampano dei trattini: un 1980 inventato sembrerebbe una data vera. Il
formato copre 1980-2107 — un file ext2 datato prima del 1980 esce senza
data invece che con un anno sbagliato.

### TCP

```
ex-os:/> tcptest example.com 80
connessione a 172.66.147.243:80 ...
connessa (id 1)
mandati 18 byte

HTTP/1.1 403 Forbidden
Server: cloudflare
...
--- ricevuti 408 byte ---
```

DNS → ARP → IP → TCP, dati in entrambi i versi attraverso il NAT. (Il 403
è HTTP: `GET / HTTP/1.0` senza `Host` viene rifiutato da Cloudflare. Il
trasporto ha funzionato — quella risposta lo dimostra.)

⚠️ **Solo connessioni in uscita.** Manca il ramo `LISTEN`/`SYN_RECEIVED`
della macchina a stati, che è circa metà del lavoro e serve a fare da
**server**. Il primo cliente di questo TCP è un client FTP in modo
**passivo** (`PASV`), che apre lui stesso anche la connessione dati; il
modo attivo richiederebbe l'ascolto e non funziona comunque dietro un NAT.
Si fa la metà che serve, e si dice che è metà.

Cosa **non** fa, dichiarato in `drivers/net/ip_proto.h`:

| | |
|---|---|
| **niente riordino** | un segmento fuori sequenza si **scarta** e si riconferma: chi l'ha mandato lo ritrasmette. Corretto ma non efficiente — tenere i pezzi vuole una lista con le sue scadenze, ed è dove un TCP giovane prende i bug peggiori |
| **niente controllo di congestione** | si manda quanto la finestra dell'altro consente. Su rete locale non cambia nulla; su Internet significa essere maleducati sotto perdita |
| **RTO fisso** | non si misura il tempo di andata e ritorno: si raddoppia da 600 ms. Misurarlo davvero (Karn, Jacobson) è il passo dopo |
| **niente SACK, window scaling, timestamp** | |

⚠️ **I numeri di sequenza si confrontano con la sottrazione, mai con `<`.**
Sono a 32 bit e si avvolgono: `a < b` a cavallo dell'avvolgimento dà la
risposta rovesciata, una volta ogni 4 GB trasmessi — cioè raramente, e
sempre quando la connessione è carica.

⚠️ `IP_MSG_TCP_APRI` può rispondere **`-EAGAIN`**: significa che lo stack
ha appena chiesto l'ARP del prossimo salto. Non è un fallimento, è «fra un
istante». Infilare l'attesa dell'ARP dentro la macchina a stati di TCP
vorrebbe dire due scadenze annidate sulla stessa connessione.

### `ftp` — client FTP

Sul CD di EX-OS, insieme agli altri strumenti di rete.

```
ex-os:/> ftp 10.0.2.2 ls
220 Server pronto
331 Serve la password
230 Accesso eseguito
-rw-r--r-- 1 exos exos     4053 Jan  1 00:00 grande.txt
-rw-r--r-- 1 exos exos       56 Jan  1 00:00 leggimi.txt

ex-os:/> ftp 10.0.2.2 get grande.txt /disco/copia.bin
4053 byte in '/disco/copia.bin'
```

Senza comando si apre una riga di comando: `ls`, `cd`, `pwd`, `get`,
`put`, `bye`.

⚠️ **Solo modo passivo (`PASV`).** In modo attivo è il *server* a
ricollegarsi al client, che deve quindi mettersi in **ascolto** — e il TCP
di EX-OS non sa farlo, di proposito. Non è un ripiego: il modo attivo non
funziona comunque dietro un NAT, ed è per questo che ogni client serio usa
`PASV` da vent'anni.

⚠️ **FTP manda la password in chiaro.** Non è un difetto del programma, è
il protocollo: chiunque stia sul percorso legge utente e password così
come sono. Il client lo dice all'accesso, una volta, invece di lasciarlo
intendere. L'alternativa si chiamerà SFTP o FTPS quando ci sarà TLS — non
«ftp con una toppa».

⚠️ Se il server annuncia in `PASV` un indirizzo diverso da quello a cui
siamo connessi, il client **usa quello vero**: un server dietro NAT
annuncia spesso il proprio indirizzo privato, che da fuori non è
raggiungibile. La porta è l'informazione utile; l'indirizzo lo sappiamo
già.

Per provarlo senza un server vero c'è `tools/ftpserver-prova.py` — ⚠️ che
**non è un server FTP**: fa entrare chiunque e serve una directory sola,
va lanciato su localhost per il tempo di una prova.

### Risoluzione dei nomi: `host`, e `ping` per nome

```
ex-os:/> host one.one.one.one
one.one.one.one ha indirizzo 1.0.0.1  (risposta da 10.0.2.3)

ex-os:/> ping www.google.com -n 2
ping www.google.com (142.251.151.119) con 32 byte di dati
  60 byte da 142.251.151.119: seq=1 ttl=255 tempo=50 ms
```

Il risolutore è un **modulo** (`lib/dns.c`), compilato dentro i programmi
che ne hanno bisogno — non un servizio e non parte dello stack. DNS sta
sopra UDP esattamente come DHCP: un errore nell'analisi di una risposta
scritta da un server sconosciuto deve far fallire un comando, non spegnere
la rete. E non ha stato da conservare fra una chiamata e l'altra, quindi
un processo dedicato costerebbe soltanto un'altra cosa da avviare e
sorvegliare.

⚠️ **I puntatori di compressione dei nomi si seguono solo in lettura, con
un tetto ai salti.** In una risposta DNS un nome può finire con un
puntatore a un punto precedente del messaggio; quel puntatore lo scrive il
server, e niente gli impedisce di farlo puntare a sé stesso. Per *saltare*
un nome non si segue affatto — un puntatore chiude il nome, e la lunghezza
è nota.

⚠️ `ping` stampa nome **e** indirizzo quando gli si dà un nome: senza,
davanti a una risposta strana non si distingue un guasto del DNS da uno
della rete.

`host` esiste per poter provare il risolutore da solo. Quando `ping nome`
non funziona, la domanda è se sia rotto il ping o il DNS, e senza questo
comando bisogna indovinare.

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
- [~] **Fase 6**  — Sistema ospitante: libc POSIX, `as`, `ld` e `cc1` nativi
                    fatti; manca il programma di guida `gcc`
- [~] **Fase 7**  — Rete: PCI, NE2000, ARP/IPv4/ICMP/UDP/TCP, DHCP, DNS e un
                    client FTP fatti; TLS da fare

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

**Stato Fase 6, seguito (agosto 2026)**: **`cc1` compila C dentro EX-OS** e produce
assembly vero, che `as` e `ld` trasformano in un eseguibile. Arrivarci ha
scoperto altri tre difetti, tutti nella libc e tutti invisibili ai programmi
di EX-OS:

- `realloc` non ingrandiva **mai** sul posto — la fusione col blocco
  successivo rifiutava i blocchi non liberi, cioè proprio il caso da gestire;
- i costruttori globali di `.init_array` non li chiamava nessuno: le 57
  voci di `cc1` non venivano eseguite e la prima struttura usata era vuota;
- `printf` con `%f` inventava cifre oltre la diciottesima e arrotondava
  2,5 a 3 invece che a 2.

**Stato Fase 7 (agosto 2026)**: la rete parte dal bus e arriva a un
trasferimento FTP verificato byte per byte. Il difetto che è costato di più
non era nella rete: **la linea 2 del PIC — la cascata — non veniva mai
smascherata**, quindi nessun IRQ da 8 a 15 poteva raggiungere la CPU. Si è
visto perché il tempo di andata e ritorno di `ping` era *esattamente* il
battito del driver: non stava rispondendo la rete, stava rispondendo il
timer. I contatori `notifiche IRQ 0, battiti 131` lo hanno detto in chiaro.

**Cosa manca ancora**, in ordine di quanto darà fastidio:

| | |
|---|---|
| **`gcc` come programma di guida** | `cc1` compila e produce assembly, `as` e `ld` ci sono: manca chi li concatena passando i file intermedi |
| **posizione condivisa fra fd duplicati** | `dup()` funziona, ma i due descrittori tengono ognuno il proprio offset |
| **TCP fuori sequenza** | i segmenti arrivati in disordine si scartano invece di riordinarli, e l'RTO è fisso invece che misurato |
| **DNS: solo record A** | i CNAME si saltano invece di seguirli; se la risposta non contiene già il record A finale, il nome non si risolve |
| **rinnovo DHCP** | `dhcp` prende la concessione e finisce; il rinnovo vuole un processo che resti acceso |
| **TLS** | senza cifratura non esistono HTTPS né SFTP; il primo passo è una sorgente di entropia, non il protocollo |
| **exFAT** | ⚠️ non esiste come filesystem: prima va implementato, poi ha senso un chkdsk |
| **`rename` fra directory** | oggi ENOSYS: sarebbe una copia più una cancellazione, cioè un'altra operazione |
| **DMA per il disco** | oggi 0,75 MB/s in PIO |

---

## Licenza

EX-OS è software libero: puoi ridistribuirlo e/o modificarlo
secondo i termini della GNU General Public License versione 2
pubblicata dalla Free Software Foundation.

Vedi il file `LICENSE` per il testo completo.

---

*EX-OS — "Il sistema si estende, il kernel rimane piccolo."*

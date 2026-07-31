# EX-OS — Extensible Operating System

**Autore:** Graziano Falcone <exagonx@hotmail.com>
**Licenza:** GNU General Public License v2 (GPL-2.0)
**Architettura:** x86 32-bit, floppy FAT12 1.44MB

---

## Che cos'è EX-OS

EX-OS è un sistema operativo baremental scritto in C e ASM per architettura
x86 32-bit. Gira da floppy da 1.44MB formattato FAT12. L'obiettivo è un
sistema estensibile: il kernel è piccolo e read-only in RAM convenzionale,
tutto il resto (driver, shell, programmi) gira in RAM estesa in spazio protetto.

Un crash di un driver o di un programma non può abbattere il sistema.

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
make debug        # QEMU + GDB stub porta 1234
make verify       # Verifica struttura floppy
make clean        # Rimuove build/
make distclean    # Rimuove build/ e dist/
```

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

## /bin/textline — editor di testo lineare

Modello edlin: si opera per numero di riga, non con un cursore. È la scelta
obbligata finché il TTY consegna il testo una riga alla volta e non esiste una
modalità raw.

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
verboseboot = 1    # 1 = log e banner (default), 0 = solo output normale
```

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

---

## Licenza

EX-OS è software libero: puoi ridistribuirlo e/o modificarlo
secondo i termini della GNU General Public License versione 2
pubblicata dalla Free Software Foundation.

Vedi il file `LICENSE` per il testo completo.

---

*EX-OS — "Il sistema si estende, il kernel rimane piccolo."*

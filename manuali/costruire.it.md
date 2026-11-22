# EX-OS — Costruire EX-OS dai sorgenti

[🇬🇧 English](costruire.md) · **🇮🇹 Italiano**

[← README](../README.it.md) · [Tutti i manuali](../README.it.md#i-manuali)

**Indice**

- [Struttura floppy](#struttura-floppy)
- [Prerequisiti (Debian 12)](#prerequisiti-debian-12)
- [Build e test](#build-e-test)

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
│   ├── login        ← L'accesso (ELF statico: è il programma con cui si entra)
│   ├── sudo         ← Esegue un comando come root, se ne hai il diritto
│   ├── ls           ← Elenco directory
│   ├── hello        ← Programma di esempio
│   ├── textline     ← Editor di testo lineare (stile edlin)
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

! `gfedit` **non sta sul floppy**: dal 16 settembre 2026 si costruisce solo
per il CD. L'immagine da 1,44 MB era scesa a 3072 byte liberi e lui ne
occupa 60236. E' rimasto `textline`, che ne occupa 18184 — ed e' il
pavimento giusto, perche' `gfedit` senza `/dev/kbd.drv` rimanda a lui
comunque. Sul CD e su ogni sistema installato c'e' come prima.


Il TTY non compare in `/dev`: `drivers/tty/tty.c` è compilato **dentro** il
kernel (possiede la VGA), e per l'input fa da client del servizio `kbd`.

### Cosa va sul floppy, e cosa no

Il floppy porta **il sistema**: avviarsi, preparare un disco, installarsi,
leggere e scrivere file. Partizionatore (`fdisk`), formattatore (`mkfs`),
controllore (`chkdsk`), montaggio, installatore, editor; il driver del
floppy e quello della tastiera, che servono a partire.

! **I driver aggiuntivi non ci vanno.** Rete (`pci`, `ne2k`, `pcnet`,
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

> **La lista vera la tiene `tools/ambiente.sh`.** Lanciato senza argomenti
> dice, parte per parte, che cosa c'è, che cosa manca e il comando da dare:
> il gcc della macchina a 32 e 64 bit, NASM, binutils, mtools, Python, QEMU
> per le prove, e i compilatori incrociati `i386-exos` e `x86_64-exos` in
> `cross_build/` (`tools/gcc-exos/cross64.sh` per quello a 64 bit, più di
> un'ora). Con `-per iso-exos` (o `iso64`, `netinst`, `prove`...) guarda solo
> quello che serve a quel bersaglio, e il Makefile lo chiama da solo
> all'inizio dei bersagli grandi.
>
> **Ciò che la costruzione produce sta fuori dall'albero dei sorgenti**, in
> `~/ex_os` (la home di chi costruisce; `EXOS_FUORI` la sposta): `cross_build`,
> `build`, `build-64`, `dischi` e `stress/risultati` stanno lì, usati
> direttamente dal Makefile e dagli script con `$EXOS_FUORI` (`tools/fuori.sh`),
> senza collegamenti. La cartella dei sorgenti tiene
> i sorgenti, le ISO finite e `dist/netinst`, `dist/netinst_64`, e i cloni git
> dei sorgenti di terzi (mesa, mariadb-server, httpd...): MEGA li tiene, ma
> senza collegamenti e modi, a volte senza `.git`. `tools/ripara-cloni.sh`
> rimette cio' che manca da `tools/cloni.txt`, senza toccare il lavoro.
>
> Il cross-compilatore `i686-elf` descritto qui sotto è quello dei primi
> mesi: il Makefile di oggi non lo usa più.

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
make ambiente-stato  # Cosa serve per costruire, cosa manca, il comando che lo fa
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
`$(BINUTILS_NATIVI)` (default `cross_build/exos-native/build-nativi`); se non ci
sono lo dice e fa il CD lo stesso. Come costruirli:
`tools/binutils-exos/leggimi.md`.

**Quel che manca si dice prima di partire.** `all`, `iso`, `iso-exos`,
`iso64`, `utente64`, `kernel64`, `tools64`, `netinst`, `netinst64`, `usb` e
`usb64` chiedono prima a `tools/ambiente.sh` cosa usano: se manca una parte
indispensabile (il compilatore incrociato, libgcc, nasm, gcc -m32...) make si
ferma prima della prima compilazione, con il comando che rimedia
(`sudo apt install ...`, `tools/ricostruisci-bersaglio.sh libm`,
`tools/gcc-exos/cross64.sh`...); se ne manca una facoltativa avvisa, e il
bersaglio si fa piu' piccolo. `tools/ambiente.sh -per toolchain` controlla
cosa serve per ricostruire i compilatori dai sorgenti. Una parte nuova va in
quell'unico elenco, non nella ricetta che la usa.

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

> ! **Floppy USB**: il bootloader usa il BIOS (INT 13h) e funziona, ma il
> kernel accede al controller floppy direttamente (porte 0x3F0-0x3F7, DMA,
> IRQ6). Un floppy USB non è collegato a quel controller: il kernel parte ma
> non riesce a leggere `/bin/sh`. Serve un drive floppy interno — vedi
> `HANDOFF.md` per le alternative.

**Nota toolchain**: il `Makefile` usa `gcc -m32` nativo, non il cross-compiler
`i686-elf-*`. Lo script `tools/install_crosscompiler.sh` resta disponibile, ma
la build non lo richiede (serve però `gcc-multilib`).

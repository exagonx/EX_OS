# EX-OS — Building EX-OS from source

**🇬🇧 English** · [🇮🇹 Italiano](costruire.it.md)

[← README](../README.md) · [All the manuals](../README.md#the-manuals)

**Contents**

- [Floppy layout](#floppy-layout)
- [Prerequisites (Debian 12)](#prerequisites-debian-12)
- [Build and test](#build-and-test)

---

## Floppy layout

```
/
├── LOADER.BIN       ← Stage 2: finds and loads the kernel via FAT12
├── KERNEL.BIN       ← EX-OS Kernel
├── boot/
│   └── kernel.cfg   ← Configuration: env, shell, modules
├── bin/
│   ├── sh           ← Shell (static ELF, first process)
│   ├── login        ← The login (static ELF: it is the program you get in with)
│   ├── sudo         ← Runs a command as root, if you are entitled to
│   ├── ls           ← Directory listing
│   ├── hello        ← Example program
│   ├── textline     ← Line-oriented text editor (edlin style)
│   ├── mkdir        ← Create a directory
│   ├── rmdir        ← Remove empty directories
│   ├── delete       ← Delete files (with ? and * wildcards)
│   ├── rename       ← Change a file's name (does not move it: see below)
│   └── chkdsk       ← Check and repair a FAT12/16/32 volume
├── lib/             ← Shared libraries (Phase 4b)
└── dev/
    ├── kbd.drv      ← PS/2 keyboard driver (ring3 process)
    └── floppy.drv   ← Floppy controller driver (still ET_DYN, not loaded)
```

! `gfedit` is **not on the floppy**: since 16 September 2026 it is built for
the CD only. The 1.44 MB image was down to 3072 free bytes and gfedit takes
60236. `textline` stayed, at 18184 bytes - and it is the right floor,
because `gfedit` falls back to it anyway when `/dev/kbd.drv` is missing.
On the CD and on every installed system it is there as before.


The TTY does not appear in `/dev`: `drivers/tty/tty.c` is compiled **into**
the kernel (it owns the VGA), and for input it acts as a client of the `kbd`
service.

### What goes on the floppy, and what does not

The floppy carries **the system**: booting, preparing a disk, installing
itself, reading and writing files. Partitioner (`fdisk`), formatter
(`mkfs`), checker (`chkdsk`), mounting, installer, editors; the floppy
driver and the keyboard one, which are needed to start.

! **The extra drivers do not go there.** Networking (`pci`, `ne2k`,
`pcnet`, `ip`) and everything that comes later live on the **EX-OS CD**. It
is not a preference: they do not fit in 1.44 MB, and the way they do not
fit is the worst one — `mcopy` fails halfway through the list, the image
ends up missing some file picked by alphabetical order, and the system
boots as far as the point where it needs what is missing.

The CD-ROM **has no driver in `/dev`**: ATAPI and ISO 9660 live *inside*
the kernel, because the kernel must be able to mount the root from there
before any process exists that could serve it.

```
                    floppy    EX-OS CD    tools CD
system and shell      yes        yes          -
fdisk, mkfs, chkdsk   yes        yes          -
kbd.drv, floppy.drv   yes        yes          -
network drivers       NO         yes          -
ping, ftp, dhcp…      NO         yes          -
as, ld, cc1           NO         NO          yes
```

`make verify` **checks the rule** instead of trusting it, and reports how
much space is left on the floppy — the number that warns you before the
image stops holding everything:

```
[OK] nessun driver da CD sul floppy
                            629 760 bytes free
```

Whatever does not fit in 1.44 MB lives on the **tools CD** (`make iso`):
native `as`, `ld` and `cc1` in `/bin`, the libc headers and source in
`/exos`, the target runtime in `/exos/lib`, the documentation in `/doc`.

---

## Prerequisites (Debian 12)

> **The real list is kept by `tools/ambiente.sh`.** Run with no arguments, it
> says part by part what is there, what is missing and the command to give:
> the host gcc at 32 and 64 bit, NASM, binutils, mtools, Python, QEMU for the
> tests, and the `i386-exos` and `x86_64-exos` cross compilers in
> `cross_build/` (`tools/gcc-exos/cross64.sh` for the 64-bit one, more than
> an hour). With `-per iso-exos` (or `iso64`, `netinst`, `prove`...) it looks
> only at what that target needs, and the Makefile calls it by itself at the
> start of the big targets.
>
> **What the build makes lives outside the source tree**, in `~/ex_os` (the
> home of the user who builds; `EXOS_FUORI` moves it): `cross_build`, `build`,
> `build-64`, `dischi` and `stress/risultati` are there, used directly by the
> Makefile and the scripts through `$EXOS_FUORI` (`tools/fuori.sh`), no links. The source folder keeps the sources, the
> finished ISOs and `dist/netinst`, `dist/netinst_64`, and the git clones of
> the third-party sources (mesa, mariadb-server, httpd...): MEGA keeps them,
> but without links and modes, sometimes without `.git`. `tools/ripara-cloni.sh`
> puts back what is missing from `tools/cloni.txt`, without touching the work.
>
> The `i686-elf` cross compiler described below is the one of the first
> months: today's Makefile no longer uses it.

```bash
# 1. Install the cross-compiler (fully automatic, ~30 min):
chmod +x tools/install_crosscompiler.sh
./tools/install_crosscompiler.sh

# 2. Enable it in the current session:
export PATH="$HOME/opt/cross/bin:$PATH"

# 3. Check:
i686-elf-gcc --version   # must print: i686-elf-gcc 13.2.0 ...
nasm --version           # nasm version 2.x
mformat --version        # Mtools version ...
```

The `install_crosscompiler.sh` script automatically installs:
- Debian dependencies (`build-essential`, `nasm`, `mtools`, `qemu-system-i386`, etc.)
- `binutils 2.41` cross-compiled for the `i686-elf` target
- `GCC 13.2.0` cross-compiled for the `i686-elf` target (C language only)
- Adds `~/opt/cross/bin` to `~/.bashrc`

---

## Build and test

```bash
make ambiente-stato  # What the build needs, what is missing, the command that makes it
make all          # Build everything + create dist/floppy.img
make run          # Boot under QEMU (32MB RAM)
make iso          # Tools CD (as, ld, headers, docs)
make run-iso      # QEMU with the CD mounted on /cdrom
make hd           # Bootable hard disk (formatted by EX-OS itself)
make run-hd       # QEMU from the disk, without a floppy
make debug        # QEMU + GDB stub on port 1234
make verify       # Check the floppy layout
make clean        # Remove build/
make distclean    # Remove build/ and dist/
```

`make iso` includes native `as` and `ld` if it finds them in
`$(BINUTILS_NATIVI)` (default `cross_build/exos-native/build-nativi`); if they are not
there it says so and builds the CD anyway. How to build them:
`tools/binutils-exos/leggimi.md`.

**Missing parts are said before starting.** `all`, `iso`, `iso-exos`,
`iso64`, `utente64`, `kernel64`, `tools64`, `netinst`, `netinst64`, `usb`
and `usb64` first ask `tools/ambiente.sh` what they use: a missing
indispensable part (the cross compiler, libgcc, nasm, gcc -m32...) stops make
before the first compilation, with the command that fixes it
(`sudo apt install ...`, `tools/ricostruisci-bersaglio.sh libm`,
`tools/gcc-exos/cross64.sh`...); a missing optional one is a warning, and the
target builds a smaller result. `tools/ambiente.sh -per toolchain` checks
what rebuilding the compilers from the sources needs. A new part goes in
that one list, not in the recipe that uses it.

### Full boot log over the serial port

The kernel mirrors on COM1 (38400 8N1) all the output that goes through
`vga_putchar()`: `kprintf`, `klog`, keyboard echo and process output. It is
needed because the VGA screen is 80x25 and the boot messages scroll away.

```bash
qemu-system-i386 -drive file=dist/floppy.img,format=raw,if=floppy \
  -m 32M -boot a -display none -serial file:/tmp/serial.txt -no-reboot
```

### Writing the image to a physical floppy (from WSL)

Three ways, same engine:

```bash
# from WSL
./tools/write_floppy.sh              # dist/floppy.img to A:
./tools/write_floppy.sh -d B: -y
```

```powershell
# from PowerShell
.\tools\write_floppy.ps1
.\tools\write_floppy.ps1 -Drive B: -Yes
```

```bat
REM from cmd.exe, or double-click from Explorer
tools\write-floppy.cmd
tools\write-floppy.cmd B:
```

An Administrator console is not needed: the script elevates itself (UAC) in a
window that stays open to show the outcome.

WSL2 cannot see Windows physical disks, so `dd` is of no use: the job goes
through PowerShell, which locks and dismounts the volume, writes the raw
volume and **reads it back to verify** byte by byte. It refuses non-removable
drives unless given `-Force`.

> ! **USB floppy**: the bootloader goes through the BIOS (INT 13h) and works,
> but the kernel talks to the floppy controller directly (ports 0x3F0-0x3F7,
> DMA, IRQ6). A USB floppy is not attached to that controller: the kernel
> starts but cannot read `/bin/sh`. An internal floppy drive is required —
> see `HANDOFF.md` for the alternatives.

**Toolchain note**: the `Makefile` uses the native `gcc -m32`, not the
`i686-elf-*` cross-compiler. The `tools/install_crosscompiler.sh` script is
still available, but the build does not require it (it does require
`gcc-multilib`).

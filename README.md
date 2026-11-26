# EX-OS — Extensible Operating System

**🇬🇧 English** · [🇮🇹 Italiano](README.it.md)

**Download the ISOs:** <http://exagonx.altervista.org/exos>

**Version:** 0.372
**Author:** Graziano Falcone <exagonx@hotmail.com>
**License:** GNU General Public License v2 (GPL-2.0)
**Architecture:** x86 32-bit (Pentium MMX and up) and x86-64, from the same sources
**Boots from:** CD, hard disk or USB key; at 32 bit from a floppy too

---

## What EX-OS is

EX-OS is an operating system written from scratch in C and assembly, for x86
PCs at 32 and 64 bit. It is not a Linux distribution and uses none of its
code: the kernel, drivers, libc, shell and graphical desktop are its own. On
top of it runs real software ported from outside — GCC, Firefox, VLC,
MariaDB, Apache, PHP — built for EX-OS without being cut down.

**The kernel is a minikernel.** Inside it are only virtual memory, the
scheduler, the filesystems, the program loader and the disk cache. The
drivers — keyboard, network, video, audio, USB — are **ordinary processes**
in ring 3. They talk to the kernel through messages and execute no
privileged instruction: if one breaks, it dies, not the system, and it is
started again.

    32-bit kernel     435 KB
    64-bit kernel     457 KB      ~71,600 lines of C and assembly, ~150 system calls

The 32-bit and 64-bit versions are kept in step: what is in one is in the
other. The only differences are the ones the hardware makes, listed below.

---

## What it can do today

| | |
|---|---|
| **System** | several processors (SMP), virtual memory, POSIX threads, signals, ptys, virtual consoles (Alt+F1…F5), users and login, quiet boot, ACPI power-off |
| **Disks** | FAT12/16/32 with long names, ext2, ext3 and ext4 read and write; CDs and DVDs (ISO 9660 and Joliet); IDE, AHCI and USB keys |
| **Desktop** | ExWin: program manager, file manager, terminal, editor (RTF too), browser, image viewer, painting, the `exide` development environment, games, the ExPackage package manager |
| **Network** | TCP/IP with DHCP and DNS, `telnet`/`telnetd`, `ssh`/`sshd`, `ftp`, TLS with OpenSSL; every connection grows with demand, no fixed ceilings |
| **Updates** | `netupdate` downloads from the repository only what changed, one application at a time, and checks the hash of every file |
| **Compilers** | GCC and G++ run **inside** EX-OS, with binutils, make, NASM and FreeBASIC; Rust at 64 bit. Every program uses the shared libc `/lib/ld-exos.so` |
| **Ported software** | Exilla (Firefox), VLC with Qt 5 and FFmpeg, MariaDB 11.8, Apache 2.4, PHP 8.5, Valkey, GNU nano, ncurses, Mesa (OpenGL ES), git (libgit2) |
| **Hardware** | network NE2000, PCnet, e1000, RTL8169, SiS900, nForce, Atheros Wi-Fi; video VESA, SiS, NVIDIA (nv40/nv50) with 2D acceleration; audio AC97, ES1371, HD Audio, Sound Blaster; USB UHCI, OHCI, EHCI, xHCI; CardBus, parallel port, CPU frequency |

### 32 and 64 bit

| | 32 bit | 64 bit |
|---|---|---|
| processor | Pentium MMX and up | x86-64 |
| boots from floppy | yes | no |
| commands in `/bin` | 105 | 99 |
| Rust, git (libgit2) | no | yes |
| ISO | `dist/exos.iso` (`make iso-exos`) | `dist/exos64.iso` (`make iso64`) |
| update repository | `netinst` | `netinst_64` |

Both install to disk and are tested in QEMU and on real PCs.

---

## Installing EX-OS

The step-by-step procedure, one chapter per medium, is in
**[manuali/installazione/](manuali/installazione/00-indice.md)** (in Italian):

| | |
|---|---|
| [From floppy](manuali/installazione/01-da-floppy.md) | a USB floppy drive too |
| [From CD-ROM](manuali/installazione/02-da-cdrom.md) | with the full-screen `cdinstall` |
| [From the network](manuali/installazione/03-dalla-rete.md) | to fill and update a system already installed |
| [After installing](manuali/installazione/04-dopo-installazione.md) | accounts, network, tools, graphics, remote access, MariaDB, Apache, PHP |
| [When it goes wrong](manuali/installazione/05-quando-va-storto.md) | the real errors, with cause and remedy |
| [Publishing the repository](manuali/installazione/06-pubblicare-il-repo.md) | for whoever keeps the update server |

The simplest way: download the ISO, boot the PC from the CD (or the USB key),
type `cdinstall` and answer the questions. Inside EX-OS, `help` explains every
command.

---

## Building EX-OS from source

On Debian:

    bash tools/ambiente.sh      # what the build is missing, and the command that installs it
    make iso-exos               # dist/exos.iso, the 32-bit CD
    make iso64                  # dist/exos64.iso, the 64-bit CD
    qemu-system-i386 -cdrom dist/exos.iso -boot d -m 512M

The prerequisites, the other images (floppy, USB, repository) and the tests
are in [Building EX-OS from source](manuali/costruire.md).

---

## The manuals

| manual | what is in it |
|---|---|
| [What's new](manuali/novita.md) | every change, newest first, with how it was tested |
| [Installing](manuali/installazione/00-indice.md) | from floppy, CD or the network, and what to do when it goes wrong (in Italian) |
| [Building from source](manuali/costruire.md) | prerequisites, `make`, the tests in QEMU, the floppy layout |
| [The kernel](manuali/kernel.md) | architecture, system calls, version, quiet boot, power-off |
| [The shell and the commands](manuali/comandi.md) | history, quoting, job control, `autoexec.sh`, virtual consoles, `date`, `ls`, the editors |
| [The graphical desktop](manuali/scrivania.md) | ExWin in practice: editor, file manager, terminal, browser, exide |
| [Disks and filesystems](manuali/dischi.md) | `fdisk`, `mkfs`, FAT and ext2, CDs and DVDs |
| [Programming](manuali/sviluppo.md) | the libc, shared libraries, GCC and G++ inside EX-OS |
| [Drivers and hardware](manuali/driver.md) | the ring-3 driver model, PCI, interrupts, keyboard, `hwconfig` |
| [Networking](manuali/rete.md) | IPv4, UDP, DHCP, TCP, `ftp`, `telnet`, name resolution |
| [The development plan](manuali/piano.md) | the plan EX-OS started from, as history |

Inside EX-OS the same manuals are in `/doc/manuali` (from the CD:
`/cdrom/doc/manuali`), and `help <command>` explains every command.

---

## Latest changes

- [Firefox account login: localStorage and indexedDB have their space (kernel 0.372)](manuali/novita.md#logging-into-the-firefox-account-works-localstorage-and-indexeddb-have-their-space-kernel-0372-voce-131--tested-in-qemu) — *tested in QEMU*
- [Names resolve at the first try: a lost DNS answer is asked again (libc)](manuali/novita.md#names-resolve-at-the-first-try-a-lost-dns-answer-is-asked-again-libc-voce-132--tested-in-qemu) — *tested in QEMU*
- [Programs keep their folders in the user's profile, not in / (sh 0.018)](manuali/novita.md#programs-keep-their-folders-in-the-users-profile-not-in--sh-0018-exilla-0009-voce-132--tested-in-qemu) — *tested in QEMU*
- [Writing on a fragmented ext2/3/4 disk costs fewer disk commands (ext4.mod 0.009)](manuali/novita.md#writing-on-a-fragmented-ext234-disk-costs-fewer-disk-commands-kernel-0372-ext4mod-0009-voce-123--tested-in-qemu) — *tested in QEMU*
- [Every program starts in the user's folder (exwin, pm 0.033, filemgr 0.017)](manuali/novita.md#every-program-starts-in-the-users-folder-exwin-pm-0033-filemgr-0017-voce-124--tested-in-qemu) — *tested in QEMU*
- [The shell has history and line editing in the Terminal and over telnet too (sh 0.017)](manuali/novita.md#the-shell-has-history-and-line-editing-in-the-terminal-and-over-telnet-too-sh-0017-exwinso-0029-voce-125--tested-in-qemu) — *tested in QEMU*
- [The file manager opens a shell in the folder shown (filemgr 0.017)](manuali/novita.md#the-file-manager-opens-a-shell-in-the-folder-shown-filemgr-0017-voce-126--tested-in-qemu) — *tested in QEMU*
- [Start menu: icon in a small button, name beside it, a triangle on the groups (exwin.so 0.029)](manuali/novita.md#the-start-menu-has-rows-with-the-icon-in-a-small-button-and-the-name-beside-it-and-a-triangle-on-the-groups-exwinso-0029-menuitem-pm-0032-voce-122--tested-in-qemu) — *tested in QEMU*

All the others, newest first: [What's new](manuali/novita.md).

---

## License

EX-OS is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License version 2 as published by the Free
Software Foundation.

See the `LICENSE` file for the full text.

---

*EX-OS — "The system extends, the kernel stays small."*

# EX-OS — Extensible Operating System

**🇬🇧 English** · [🇮🇹 Italiano](README.it.md)

**Download the ISOs:** <http://exagonx.altervista.org/exos>

**Version:** 0.353
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

- [A command line up to 32 KB, at the console and over telnet (kernel 0.353, sh 0.014, kbd.drv)](manuali/novita.md#a-command-line-up-to-32-kb-at-the-console-and-over-telnet-kernel-0353-sh-0014-kbddrv--tested-in-qemu-and-on-the-test-pc) — *tested in QEMU and on the test PC*
- [netupdate 0.051 and 0.052: avvio and sistema are always looked at](manuali/novita.md#netupdate-0051-and-0052-avvio-and-sistema-are-always-looked-at--tested-in-qemu-and-on-the-test-pc) — *tested in QEMU and on the test PC*
- [Kernel 0.352: at 64 bit the system calls run with interrupts on](manuali/novita.md#kernel-0352-at-64-bit-the-system-calls-run-with-interrupts-on--tested-in-qemu-and-on-the-test-pc) — *tested in QEMU and on the test PC*
- [exide compiles at 64 bit too (exide 0.027)](manuali/novita.md#exide-compiles-at-64-bit-too-exide-0027--tested-in-qemu-and-on-the-test-pc) — *tested in QEMU and on the test PC*
- [VLC as on Linux: libvlc.so, libvlccore.so and every plugin a .so opened with dlopen (voce 105, step 4)](manuali/novita.md#vlc-as-on-linux-libvlcso-libvlccoreso-and-every-plugin-a-so-opened-with-dlopen-voce-105-step-4--tested-in-qemu-and-on-the-test-pc) — *tested in QEMU and on the test PC*
- [The archive by application: netupdate brings only what changed, ExPackage has Update and Upgrade (netupdate 0.050, expackage 0.002, voci 106 and 107)](manuali/novita.md#the-archive-by-application-netupdate-brings-only-what-changed-expackage-has-update-and-upgrade-netupdate-0050-expackage-0002-voci-106-and-107--tested-in-qemu-and-on-the-test-pc) — *tested in QEMU and on the test PC*
- [Every program on /lib/ld-exos.so, the EX-OS libraries are real .so files (kernel 0.351, voce 105)](manuali/novita.md#every-program-on-libld-exosso-the-ex-os-libraries-are-real-so-files-kernel-0351-voce-105--tested-in-qemu) — *tested in QEMU*
- [The shared libc: /lib/ld-exos.so, and the kernel starts the interpreter (kernel 0.350, voce 105)](manuali/novita.md#the-shared-libc-libld-exosso-and-the-kernel-starts-the-interpreter-kernel-0350-voce-105--tested-in-qemu) — *tested in QEMU*

All the others, newest first: [What's new](manuali/novita.md).

---

## License

EX-OS is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License version 2 as published by the Free
Software Foundation.

See the `LICENSE` file for the full text.

---

*EX-OS — "The system extends, the kernel stays small."*

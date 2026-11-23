# EX-OS — Extensible Operating System

[🇬🇧 English](README.md) · **🇮🇹 Italiano**

**Scarica le ISO:** <http://exagonx.altervista.org/exos>

**Versione:** 0.357
**Autore:** Graziano Falcone <exagonx@hotmail.com>
**Licenza:** GNU General Public License v2 (GPL-2.0)
**Architettura:** x86 a 32 bit (dal Pentium MMX in su) e x86-64, dagli stessi sorgenti
**Si avvia da:** CD, disco rigido o chiavetta USB; a 32 bit anche da floppy

---

## Che cos'è EX-OS

EX-OS è un sistema operativo scritto da zero in C e assembly, per PC x86 a
32 e a 64 bit. Non è una distribuzione Linux e non ne usa il codice: kernel,
driver, libc, shell e scrivania grafica sono suoi. Sopra ci girano programmi
veri portati da fuori — GCC, Firefox, VLC, MariaDB, Apache, PHP — compilati
per EX-OS senza toglierli dalla loro forma.

**Il kernel è un minikernel.** Dentro ci sono soltanto la memoria virtuale, lo
scheduler, i filesystem, il caricatore dei programmi e la cache dei dischi. I
driver — tastiera, rete, video, audio, USB — sono **processi normali** in
ring 3. Parlano col kernel per messaggi e non eseguono istruzioni
privilegiate: se uno si guasta muore lui, non il sistema, e lo si rilancia.

    kernel a 32 bit     435 KB
    kernel a 64 bit     457 KB      ~71 600 righe di C e assembly, ~150 chiamate di sistema

Le due versioni, a 32 e 64 bit, si aggiornano insieme: quello che c'è in una
c'è nell'altra. Le uniche differenze sono quelle date dall'hardware, elencate
più sotto.

---

## Che cosa sa fare oggi

| | |
|---|---|
| **Sistema** | più processori (SMP), memoria virtuale, fili POSIX, segnali, pty, console virtuali (Alt+F1…F5), utenti e login, avvio silenzioso, spegnimento ACPI |
| **Dischi** | FAT12/16/32 con i nomi lunghi, ext2, ext3 ed ext4 in lettura e scrittura; CD e DVD (ISO 9660 e Joliet); IDE, AHCI e chiavette USB |
| **Scrivania** | ExWin: program manager, file manager, terminale, editor (anche RTF), navigatore, visualizzatore di immagini, disegno, ambiente di sviluppo `exide`, giochi, gestore dei pacchetti ExPackage |
| **Rete** | TCP/IP con DHCP e DNS, `telnet`/`telnetd`, `ssh`/`sshd`, `ftp`, TLS con OpenSSL; ogni connessione cresce con le richieste, senza tetti fissi |
| **Aggiornamenti** | `netupdate` scarica dal repository solo quello che è cambiato, un'applicazione alla volta, e controlla l'impronta di ogni file |
| **Compilatori** | GCC e G++ girano **dentro** EX-OS, con binutils, make, NASM e FreeBASIC; Rust a 64 bit. Ogni programma usa la libc condivisa `/lib/ld-exos.so` |
| **Programmi portati** | Exilla (Firefox), VLC con Qt 5 e FFmpeg, MariaDB 11.8, Apache 2.4, PHP 8.5, Valkey, GNU nano, ncurses, Mesa (OpenGL ES), git (libgit2) |
| **Hardware** | rete NE2000, PCnet, e1000, RTL8169, SiS900, nForce, Wi-Fi Atheros; video VESA, SiS, NVIDIA (nv40/nv50) con il 2D accelerato; audio AC97, ES1371, HD Audio, Sound Blaster; USB UHCI, OHCI, EHCI, xHCI; CardBus, porta parallela, frequenza della CPU |

### 32 e 64 bit

| | 32 bit | 64 bit |
|---|---|---|
| processore | dal Pentium MMX | x86-64 |
| avvio da floppy | sì | no |
| comandi in `/bin` | 105 | 99 |
| Rust, git (libgit2) | no | sì |
| ISO | `dist/exos.iso` (`make iso-exos`) | `dist/exos64.iso` (`make iso64`) |
| repository degli aggiornamenti | `netinst` | `netinst_64` |

Tutte e due si installano su disco e sono provate in QEMU e su PC veri.

---

## Installare EX-OS

La procedura passo passo, con un capitolo per supporto, sta in
**[manuali/installazione/](manuali/installazione/00-indice.md)**:

| | |
|---|---|
| [Da floppy](manuali/installazione/01-da-floppy.md) | anche con il lettore sull'USB |
| [Da CD-ROM](manuali/installazione/02-da-cdrom.md) | con `cdinstall` a schermo intero |
| [Dalla rete](manuali/installazione/03-dalla-rete.md) | per riempire e aggiornare un sistema già installato |
| [Dopo l'installazione](manuali/installazione/04-dopo-installazione.md) | conti, rete, strumenti, grafica, accesso remoto, MariaDB, Apache, PHP |
| [Quando va storto](manuali/installazione/05-quando-va-storto.md) | gli errori veri, con la causa e il rimedio |
| [Pubblicare il repository](manuali/installazione/06-pubblicare-il-repo.md) | per chi tiene il server degli aggiornamenti |

Il modo più semplice: scarica l'ISO, avvia il PC dal CD (o dalla chiavetta),
scrivi `cdinstall` e segui le domande. Dentro EX-OS, `help` spiega ogni
comando.

---

## Costruire EX-OS dai sorgenti

Su Debian:

    bash tools/ambiente.sh      # che cosa manca per costruire, e il comando che lo installa
    make iso-exos               # dist/exos.iso, il CD a 32 bit
    make iso64                  # dist/exos64.iso, il CD a 64 bit
    qemu-system-i386 -cdrom dist/exos.iso -boot d -m 512M

I prerequisiti, le altre immagini (floppy, USB, repository) e le prove stanno
in [Costruire EX-OS dai sorgenti](manuali/costruire.it.md).

---

## I manuali

| manuale | che cosa c'è |
|---|---|
| [Novità](manuali/novita.it.md) | ogni cambiamento, dal più recente, con come è stato provato |
| [Installazione](manuali/installazione/00-indice.md) | da floppy, CD o rete, e cosa fare quando va storto |
| [Costruire dai sorgenti](manuali/costruire.it.md) | prerequisiti, `make`, le prove in QEMU, la struttura del floppy |
| [Il kernel](manuali/kernel.it.md) | architettura, chiamate di sistema, versione, avvio silenzioso, spegnimento |
| [La shell e i comandi](manuali/comandi.it.md) | cronologia, virgolette, job control, `autoexec.sh`, console virtuali, `date`, `ls`, gli editor |
| [La scrivania grafica](manuali/scrivania.it.md) | ExWin in pratica: editor, file manager, terminale, navigatore, exide |
| [Dischi e filesystem](manuali/dischi.it.md) | `fdisk`, `mkfs`, FAT ed ext2, CD e DVD |
| [Programmare](manuali/sviluppo.it.md) | la libc, le librerie condivise, GCC e G++ dentro EX-OS |
| [Driver e hardware](manuali/driver.it.md) | il modello dei driver in ring 3, PCI, interrupt, tastiera, `hwconfig` |
| [La rete](manuali/rete.it.md) | IPv4, UDP, DHCP, TCP, `ftp`, `telnet`, la risoluzione dei nomi |
| [Il piano di sviluppo](manuali/piano.it.md) | il piano da cui EX-OS è partito, come storia |

Dentro EX-OS gli stessi manuali sono in `/doc/manuali` (dal CD:
`/cdrom/doc/manuali`), e `help <comando>` spiega ogni comando.

---

## Ultime novità

- [Quando un programma cade il kernel scrive chi l'ha chiamato: fino a 48 chiamanti dalla pila (kernel 0.357)](manuali/novita.it.md#quando-un-programma-cade-il-kernel-scrive-chi-lha-chiamato-fino-a-48-chiamanti-dalla-pila-kernel-0357--testato-in-qemu) — *testato in QEMU*
- [Firefox non si chiude più sulle pagine piene di immagini: fcntl F_DUPFD_CLOEXEC copia un descrittore di memoria condivisa](manuali/novita.it.md#firefox-non-si-chiude-più-sulle-pagine-piene-di-immagini-fcntl-f_dupfd_cloexec-copia-un-descrittore-di-memoria-condivisa--da-testare-sul-pc-di-prova) — *da testare sul PC di prova*
- [Il mittente di ogni messaggio timbrato dal kernel; la scheda di rete la usa solo root (kernel 0.356, voce 111 passo 4)](manuali/novita.it.md#il-mittente-di-ogni-messaggio-timbrato-dal-kernel-la-scheda-di-rete-la-usa-solo-root-kernel-0356-voce-111-passo-4--testato-in-qemu-e-sul-pc-di-prova) — *testato in QEMU e sul PC di prova*
- [Utenti separati davvero: una cartella chiusa e' chiusa, /tmp ha il bit sticky (kernel 0.355, voce 111)](manuali/novita.it.md#utenti-separati-davvero-una-cartella-chiusa-e-chiusa-tmp-ha-il-bit-sticky-kernel-0355-voce-111--testato-in-qemu-e-sul-pc-di-prova) — *testato in QEMU e sul PC di prova*
- [I cloni dei sorgenti di terzi restano su MEGA e li rimettono a posto gli script (tools/ripara-cloni.sh)](manuali/novita.it.md#i-cloni-dei-sorgenti-di-terzi-restano-su-mega-e-li-rimettono-a-posto-gli-script-toolsripara-clonish--testato-su-questo-pc) — *testato su questo PC*
- [Cio' che la costruzione produce sta fuori dall'albero dei sorgenti, in ~/ex_os (tools/fuori.sh)](manuali/novita.it.md#cio-che-la-costruzione-produce-sta-fuori-dallalbero-dei-sorgenti-in-ex_os-toolsfuorish--testato-su-questo-pc) — *testato su questo PC*
- [Firefox su /lib/ld-exos.so: AAC e H.264 suonano di nuovo (voce 105)](manuali/novita.it.md#firefox-su-libld-exosso-aac-e-h264-suonano-di-nuovo-voce-105--testato-in-qemu) — *testato in QEMU*
- [Due fili non ricevono piu' lo stesso descrittore, SQLite non dice piu' disk I/O error (kernel 0.354)](manuali/novita.it.md#due-fili-non-ricevono-piu-lo-stesso-descrittore-sqlite-non-dice-piu-disk-io-error-kernel-0354--testato-in-qemu) — *testato in QEMU*

Tutte le altre, dalla più recente: [Novità](manuali/novita.it.md).

---

## Licenza

EX-OS è software libero: puoi ridistribuirlo e/o modificarlo
secondo i termini della GNU General Public License versione 2
pubblicata dalla Free Software Foundation.

Vedi il file `LICENSE` per il testo completo.

---

*EX-OS — "Il sistema si estende, il kernel rimane piccolo."*

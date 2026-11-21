# EX-OS — What's new

**🇬🇧 English** · [🇮🇹 Italiano](novita.it.md)

[← README](../README.md) · [All the manuals](../README.md#the-manuals)

**Contents**

- [A command line up to 32 KB, at the console and over telnet (kernel 0.353, sh 0.014, kbd.drv) — tested in QEMU and on the test PC](#a-command-line-up-to-32-kb-at-the-console-and-over-telnet-kernel-0353-sh-0014-kbddrv--tested-in-qemu-and-on-the-test-pc)
- [netupdate 0.051 and 0.052: avvio and sistema are always looked at — tested in QEMU and on the test PC](#netupdate-0051-and-0052-avvio-and-sistema-are-always-looked-at--tested-in-qemu-and-on-the-test-pc)
- [Kernel 0.352: at 64 bit the system calls run with interrupts on — tested in QEMU and on the test PC](#kernel-0352-at-64-bit-the-system-calls-run-with-interrupts-on--tested-in-qemu-and-on-the-test-pc)
- [exide compiles at 64 bit too (exide 0.027) — tested in QEMU and on the test PC](#exide-compiles-at-64-bit-too-exide-0027--tested-in-qemu-and-on-the-test-pc)
- [VLC as on Linux: libvlc.so, libvlccore.so and every plugin a .so opened with dlopen (voce 105, step 4) — tested in QEMU and on the test PC](#vlc-as-on-linux-libvlcso-libvlccoreso-and-every-plugin-a-so-opened-with-dlopen-voce-105-step-4--tested-in-qemu-and-on-the-test-pc)
- [The archive by application: netupdate brings only what changed, ExPackage has Update and Upgrade (netupdate 0.050, expackage 0.002, voci 106 and 107) — tested in QEMU and on the test PC](#the-archive-by-application-netupdate-brings-only-what-changed-expackage-has-update-and-upgrade-netupdate-0050-expackage-0002-voci-106-and-107--tested-in-qemu-and-on-the-test-pc)
- [Every program on /lib/ld-exos.so, the EX-OS libraries are real .so files (kernel 0.351, voce 105) — tested in QEMU](#every-program-on-libld-exosso-the-ex-os-libraries-are-real-so-files-kernel-0351-voce-105--tested-in-qemu)
- [The shared libc: /lib/ld-exos.so, and the kernel starts the interpreter (kernel 0.350, voce 105) — tested in QEMU](#the-shared-libc-libld-exosso-and-the-kernel-starts-the-interpreter-kernel-0350-voce-105--tested-in-qemu)
- [pthread_cancel stops a thread that waits, FD_CLOEXEC closes at exec, posix_spawn (kernel 0.349, ip.drv 0.027, voce 105) — tested in QEMU](#pthread_cancel-stops-a-thread-that-waits-fd_cloexec-closes-at-exec-posix_spawn-kernel-0349-ipdrv-0027-voce-105--tested-in-qemu)
- [Qt 5 on EX-OS, and VLC with its menus (tools/qt5-exos, kernel 0.348, voce 85) — tested in QEMU](#qt-5-on-ex-os-and-vlc-with-its-menus-toolsqt5-exos-kernel-0348-voce-85--tested-in-qemu)
- [GNU nano 9.3 (tools/nano-exos) — tested in QEMU](#gnu-nano-93-toolsnano-exos--tested-in-qemu)
- [The fixed ceilings that were left go (kernel 0.347, voce 35) — tested in QEMU](#the-fixed-ceilings-that-were-left-go-kernel-0347-voce-35--tested-in-qemu)
- [A signal cuts a wait on a socket short (kernel 0.346, ip.drv 0.026, voce 37) — tested in QEMU and on the bench PC](#a-signal-cuts-a-wait-on-a-socket-short-kernel-0346-ipdrv-0026-voce-37--tested-in-qemu-and-on-the-bench-pc)
- [The bench PC's switch-off: what its BIOS does first (voce 102, under way)](#the-bench-pcs-switch-off-what-its-bios-does-first-voce-102-under-way)
- [Every driver has -install and -remove (voce 24) — tested in QEMU](#every-driver-has--install-and--remove-voce-24--tested-in-qemu)
- [A telnet session takes away what it started (telnetd 0.016, voci 21 and 100) — tested in QEMU and on the bench PC](#a-telnet-session-takes-away-what-it-started-telnetd-0016-voci-21-and-100--tested-in-qemu-and-on-the-bench-pc)
- [ATA: a failed FLUSH CACHE is tried again, BSY and DMA wait 30 s (kernel 0.345, voci 40 and 70) — tested in QEMU and on the bench PC](#ata-a-failed-flush-cache-is-tried-again-bsy-and-dma-wait-30-s-kernel-0345-voci-40-and-70--tested-in-qemu-and-on-the-bench-pc)
- [`cachedisco` in kernel.cfg counts (kernel 0.344, voce 27) — tested in QEMU and on the bench PC](#cachedisco-in-kernelcfg-counts-kernel-0344-voce-27--tested-in-qemu-and-on-the-bench-pc)
- [netupdate tries a failed boot map again (netupdate 0.049, voci 60, 49, 47) — tested in QEMU](#netupdate-tries-a-failed-boot-map-again-netupdate-0049-voci-60-49-47--tested-in-qemu)
- [Exilla: the first tab's address bar works from the second start on (voce 73) — tested in QEMU, package to be published](#exilla-the-first-tabs-address-bar-works-from-the-second-start-on-voce-73--tested-in-qemu-package-to-be-published)
- [telnetd serves several sessions at once (telnetd 0.015, voce 62) — tested in QEMU and on the bench PC](#telnetd-serves-several-sessions-at-once-telnetd-0015-voce-62--tested-in-qemu-and-on-the-bench-pc)
- [No ceiling on mounted volumes (kernel 0.343, mount 0.005, automount 0.005, voce 50) — tested in QEMU, runs on the bench PC](#no-ceiling-on-mounted-volumes-kernel-0343-mount-0005-automount-0005-voce-50--tested-in-qemu-runs-on-the-bench-pc)
- [cpufreq reads the two pairs a Core 2 declares, and writes nothing (kernel 0.342, voce 78) — tested on the bench PC](#cpufreq-reads-the-two-pairs-a-core-2-declares-and-writes-nothing-kernel-0342-voce-78--tested-on-the-bench-pc)
- [ExEditor prints, shows the print preview, and has a bar and a ruler for text files too (ExEditor 0.009, exwin.so 0.022, lpt.drv 0.004, voce 81) — tested in QEMU](#exeditor-prints-shows-the-print-preview-and-has-a-bar-and-a-ruler-for-text-files-too-exeditor-0009-exwinso-0022-lptdrv-0004-voce-81--tested-in-qemu)
- [lpt.drv finds the PCI parallel cards (lpt.drv 0.003, voce 98) — tested](#lptdrv-finds-the-pci-parallel-cards-lptdrv-0003-voce-98--tested)
- [munmap can no longer stop the machine (kernel 0.341)](#munmap-can-no-longer-stop-the-machine-kernel-0341)
- [Programs get the real universal time: EX-OS knows its time zone (kernel 0.340)](#programs-get-the-real-universal-time-ex-os-knows-its-time-zone-kernel-0340)
- [stat() tells the real permissions, owner, links and times, and files written on FAT have their date (kernel 0.339)](#stat-tells-the-real-permissions-owner-links-and-times-and-files-written-on-fat-have-their-date-kernel-0339)
- [Sleeps that last what they ask, a timer that looks only when needed, processes found by PID at once, CDs read three times faster, a program that exits while its threads read no longer stops the files (kernel 0.334-0.338, analisi_kernel voci 6, 21, 22, 24, 27, 28, 75)](#sleeps-that-last-what-they-ask-a-timer-that-looks-only-when-needed-processes-found-by-pid-at-once-cds-read-three-times-faster-a-program-that-exits-while-its-threads-read-no-longer-stops-the-files-kernel-0334-0338-analisi_kernel-voci-6-21-22-24-27-28-75)
- [Less useless disk work: ext2, the ext4 journal, AHCI and FAT (kernel 0.333, ext4.mod 0.006, analisi_kernel voci 56, 63, 73, 74, 75)](#less-useless-disk-work-ext2-the-ext4-journal-ahci-and-fat-kernel-0333-ext4mod-0006-analisi_kernel-voci-56-63-73-74-75)
- [A paste into telnet or ssh is no longer cut, floppy files opened twice, a real random generator (kernel 0.332, telnetd 0.014, sshd 0.002, analisi_kernel voci 53, 55, 67, 70)](#a-paste-into-telnet-or-ssh-is-no-longer-cut-floppy-files-opened-twice-a-real-random-generator-kernel-0332-telnetd-0014-sshd-0002-analisi_kernel-voci-53-55-67-70)
- [A bad pointer kills the program, not the machine, and twenty more kernel holes closed (kernel 0.331, analisi_kernel)](#a-bad-pointer-kills-the-program-not-the-machine-and-twenty-more-kernel-holes-closed-kernel-0331-analisi_kernel)
- [An ordinary user can no longer read or write the kernel, nor write a file opened for reading (kernel 0.330, analisi_kernel voci 1-4 and 46)](#an-ordinary-user-can-no-longer-read-or-write-the-kernel-nor-write-a-file-opened-for-reading-kernel-0330-analisi_kernel-voci-1-4-and-46)
- [The GeForce 210 with its 2D engine, a driver for the NV4x, and `shutdown -r` that reboots (kernel 0.327-0.329, nv50.drv 0.011, nv40.drv 0.005, sh 0.012, voce 99)](#the-geforce-210-with-its-2d-engine-a-driver-for-the-nv4x-and-shutdown--r-that-reboots-kernel-0327-0329-nv50drv-0011-nv40drv-0005-sh-0012-voce-99)
- [A script and its background child no longer wipe each other's lines (kernel 0.326, sh 0.011, voce 94), and `mount` shows /proc as proc (kernel 0.325, mount 0.004, voce 93)](#a-script-and-its-background-child-no-longer-wipe-each-others-lines-kernel-0326-sh-0011-voce-94-and-mount-shows-proc-as-proc-kernel-0325-mount-0004-voce-93)
- [No edge lost with two processors, and the parallel port without system calls (kernel 0.324, lpt.drv 0.002, voce 98)](#no-edge-lost-with-two-processors-and-the-parallel-port-without-system-calls-kernel-0324-lptdrv-0002-voce-98)
- [One driver for the parallel port (kernel 0.323, lpt.drv 0.001, voce 98)](#one-driver-for-the-parallel-port-kernel-0323-lptdrv-0001-voce-98)
- [Exilla writes Chinese, Japanese and Korean (package font-cjk, netupdate 0.047)](#exilla-writes-chinese-japanese-and-korean-package-font-cjk-netupdate-0047)
- [Two processors no longer stop the disk for good (kernel 0.322)](#two-processors-no-longer-stop-the-disk-for-good-kernel-0322)
- [Sending is no longer held to 4 KB, and TCP has congestion control (ip.drv 0.024, ipcfg 0.006)](#sending-is-no-longer-held-to-4-kb-and-tcp-has-congestion-control-ipdrv-0024-ipcfg-0006)
- [The CPU frequency follows the load (kernel 0.311, cpufreq.drv 0.001)](#the-cpu-frequency-follows-the-load-kernel-0311-cpufreqdrv-0001)
- [C++ as shared objects, and a loader with no fixed number of libraries (voce 85, the way to Qt 5)](#c-as-shared-objects-and-a-loader-with-no-fixed-number-of-libraries-voce-85-the-way-to-qt-5)
- [VLC is in the start menu (vlcapri 0.001, voce 79)](#vlc-is-in-the-start-menu-vlcapri-0001-voce-79)
- [The screen switches off when nobody uses it (kernel 0.312, dpms.drv 0.002, voce 78 b)](#the-screen-switches-off-when-nobody-uses-it-kernel-0312-dpmsdrv-0002-voce-78-b)
- [The laptop's sound starts by itself, and ls lists every name (audio 0.005, ls 0.003)](#the-laptops-sound-starts-by-itself-and-ls-lists-every-name-audio-0005-ls-0003)
- [The boot sector no longer goes back to the old kernel after an update (netupdate 0.043, voce 75)](#the-boot-sector-no-longer-goes-back-to-the-old-kernel-after-an-update-netupdate-0043-voce-75)
- [netupdate brings the new files of installed packages, and VLC finds its libraries (netupdate 0.042)](#netupdate-brings-the-new-files-of-installed-packages-and-vlc-finds-its-libraries-netupdate-0042)
- [ExPackage, and netupdate reads the whole catalogue (netupdate 0.041, expackage 0.001)](#expackage-and-netupdate-reads-the-whole-catalogue-netupdate-0041-expackage-0001)
- [Third-party libraries are shared objects, and programs load them at start](#third-party-libraries-are-shared-objects-and-programs-load-them-at-start)
- [Audio and video: the codecs, VLC, and three faults that kept every player silent (ac97.drv 0.003, hdaudio.drv 0.006, sb.drv 0.002, es1371.drv 0.002)](#audio-and-video-the-codecs-vlc-and-three-faults-that-kept-every-player-silent-ac97drv-0003-hdaudiodrv-0006-sbdrv-0002-es1371drv-0002)
- [Dead drivers no longer write into other programs (kernel 0.309 and 0.310, chkdsk 0.004)](#dead-drivers-no-longer-write-into-other-programs-kernel-0309-and-0310-chkdsk-0004)
- [A disk error is an error (kernel 0.308, date 0.004, netupdate 0.040)](#a-disk-error-is-an-error-kernel-0308-date-0004-netupdate-0040)
- [Fewer FLUSH: the journal does not commit at every operation (kernel 0.307)](#fewer-flush-the-journal-does-not-commit-at-every-operation-kernel-0307)
- [Disk read cache (kernel 0.304)](#disk-read-cache-kernel-0304)
- [Disk counters (kernel 0.303, disk 0.003, /proc/disco)](#disk-counters-kernel-0303-disk-0003-procdisco)
- [reboot, halt and poweroff unmount the volumes (kernel 0.302) — tested in QEMU at 32 and 64 bit](#reboot-halt-and-poweroff-unmount-the-volumes-kernel-0302--tested-in-qemu-at-32-and-64-bit)
- [A stale module no longer locks the disk; netupdate retries (kernel 0.293, netupdate 0.038, install 0.011, hwconfig 0.009, nv50.drv 0.009) — tested in QEMU, to be tested on real hardware](#a-stale-module-no-longer-locks-the-disk-netupdate-retries-kernel-0293-netupdate-0038-install-0011-hwconfig-0009-nv50drv-0009--tested-in-qemu-to-be-tested-on-real-hardware)
- [ext3 and ext4 in the tools too (kernel 0.292, mkfs 0.002, chkdsk 0.003) — tested in QEMU, to be tested on real hardware](#ext3-and-ext4-in-the-tools-too-kernel-0292-mkfs-0002-chkdsk-0003--tested-in-qemu-to-be-tested-on-real-hardware)
- [Real file locks (kernel 0.291) — tested in QEMU, Firefox still to be relinked](#real-file-locks-kernel-0291--tested-in-qemu-firefox-still-to-be-relinked)
- [The clock comes from the network at every boot (date 0.003) — tested in QEMU](#the-clock-comes-from-the-network-at-every-boot-date-0003--tested-in-qemu)
- [Deleting a folder no longer stops at the first error (filemgr 0.015) — tested in QEMU](#deleting-a-folder-no-longer-stops-at-the-first-error-filemgr-0015--tested-in-qemu)
- [/proc in the base system (kernel 0.290, /bin/readlink) — tested in QEMU](#proc-in-the-base-system-kernel-0290-binreadlink--tested-in-qemu)
- [grep (/bin/grep) — tested](#grep-bingrep--tested)
- [Third-party sources against the official ones (tools/aggiorna-sorgenti.sh) — tested without a new version](#third-party-sources-against-the-official-ones-toolsaggiorna-sorgentish--tested-without-a-new-version)
- [The Firefox sources from the latest release (tools/exilla/sorgenti.sh) — tested on Firefox 160.0.1](#the-firefox-sources-from-the-latest-release-toolsexillasorgentish--tested-on-firefox-16001)
- [The Stamp key saves the screen to a PNG (kbd.drv 0.008, wserver 0.015, pm 0.026, /bin/cattura) — tested in QEMU](#the-stamp-key-saves-the-screen-to-a-png-kbddrv-0008-wserver-0015-pm-0026-bincattura--tested-in-qemu)
- [Network rings sized from the RAM on every driver (e1000.drv 0.005, nforce.drv 0.015, rtl8169.drv 0.004, pcnet.drv 0.004, sis900.drv 0.020, ne2k.drv 0.004, ar5k.drv 0.004) — e1000, pcnet and ne2k tested in QEMU; nforce, rtl8169, sis900 and ar5k **to be tested**](#network-rings-sized-from-the-ram-on-every-driver-e1000drv-0005-nforcedrv-0015-rtl8169drv-0004-pcnetdrv-0004-sis900drv-0020-ne2kdrv-0004-ar5kdrv-0004--e1000-pcnet-and-ne2k-tested-in-qemu-nforce-rtl8169-sis900-and-ar5k-to-be-tested)
- [A new keyboard in ExWin: accented letters, presses and releases (kbd.drv 0.007, wserver 0.014, exwin.so 0.021) — tested in QEMU](#a-new-keyboard-in-exwin-accented-letters-presses-and-releases-kbddrv-0007-wserver-0014-exwinso-0021--tested-in-qemu)
- [ExWin without fixed ceilings: windows, list, objects (kernel 0.289, wserver 0.013, exwin.so 0.020) — tested in QEMU](#exwin-without-fixed-ceilings-windows-list-objects-kernel-0289-wserver-0013-exwinso-0020--tested-in-qemu)
- [PHP 8.5 with PDO inside Apache, and 2048 clients for 10 minutes (kernel 0.286)](#php-85-with-pdo-inside-apache-and-2048-clients-for-10-minutes-kernel-0286)
- [A real fsync, and on a par with Linux (kernel 0.284-0.285)](#a-real-fsync-and-on-a-par-with-linux-kernel-0284-0285)
- [No fixed ceilings, and Apache 2.4 (kernel 0.281-0.283, ip.drv 0.017-0.019)](#no-fixed-ceilings-and-apache-24-kernel-0281-0283-ipdrv-0017-0019)
- [1024 connections to MariaDB: epoll, a pool of threads, a fair mailbox (kernel 0.280, ip.drv 0.016)](#1024-connections-to-mariadb-epoll-a-pool-of-threads-a-fair-mailbox-kernel-0280-ipdrv-0016)
- [MariaDB 11.8 LTS runs in EX-OS (0.279)](#mariadb-118-lts-runs-in-ex-os-0279)
- [OpenSSL 4.1.0 with sockets and threads, and 127.0.0.1](#openssl-410-with-sockets-and-threads-and-127001)
- [Seven libraries as real EX-OS libraries: zlib, bzip2, xz, lz4, zstd, PCRE2, {fmt}](#seven-libraries-as-real-ex-os-libraries-zlib-bzip2-xz-lz4-zstd-pcre2-fmt)
- [ncurses 6.6: full-screen programs on the console and over telnet](#ncurses-66-full-screen-programs-on-the-console-and-over-telnet)
- [A real terminal: termios on the console and in telnet (0.279)](#a-real-terminal-termios-on-the-console-and-in-telnet-0279)
- [A telnet session that will not come back, and the screen of the integrated NVIDIA (0.278)](#a-telnet-session-that-will-not-come-back-and-the-screen-of-the-integrated-nvidia-0278)
- [Threads reading the same file: a real `pread` and locked streams (0.277)](#threads-reading-the-same-file-a-real-pread-and-locked-streams-0277)
- [Swap works (kernel 0.276) — tested in QEMU, still to be tested under pressure on real hardware](#swap-works-kernel-0276--tested-in-qemu-still-to-be-tested-under-pressure-on-real-hardware)
- [USB sticks are `/USB/USB0`, `/USB/USB1`... — tested in QEMU](#usb-sticks-are-usbusb0-usbusb1--tested-in-qemu)
- [The status bar: `extaskbar` (kernel 0.255) — tested in QEMU](#the-status-bar-extaskbar-kernel-0255--tested-in-qemu)
- [WiFi: the first real card, Atheros AR2425 (`ar5k.drv`) — tested](#wifi-the-first-real-card-atheros-ar2425-ar5kdrv--tested)
- [Libraries: a real dynamic loader, OpenSSL at 64 bit, iconv](#libraries-a-real-dynamic-loader-openssl-at-64-bit-iconv)
- [WiFi: the `wifi` command and the icon next to the clock (kernel 0.250)](#wifi-the-wifi-command-and-the-icon-next-to-the-clock-kernel-0250)
- [ExWin: every control can be hidden and switched off](#exwin-every-control-can-be-hidden-and-switched-off)
- [Installing without a CD drive: `make installa`, and the guide (kernel 0.240)](#installing-without-a-cd-drive-make-installa-and-the-guide-kernel-0240)
- [A real motherboard: SATA disk and NVIDIA network (kernel 0.239)](#a-real-motherboard-sata-disk-and-nvidia-network-kernel-0239)
- [More processors, stages 1 and 2 (kernel 0.237 and 0.238)](#more-processors-stages-1-and-2-kernel-0237-and-0238)
- [exide: miniatures, pointer, non-modal code; and tiscali.it opens](#exide-miniatures-pointer-non-modal-code-and-tiscaliit-opens)
- [Four games: EXKlondike, EXSpider, EXMajong, EXGO](#four-games-exklondike-exspider-exmajong-exgo)
- [The ExWin API is in English, and exide is consistent](#the-exwin-api-is-in-english-and-exide-is-consistent)
- [Firefox in an ExWin window](#firefox-in-an-exwin-window)
- [Firefox draws its first page inside EX-OS](#firefox-draws-its-first-page-inside-ex-os)
- [Kernel 0.228–0.236: what Firefox found](#kernel-02280236-what-firefox-found)
- [The floppy has room again](#the-floppy-has-room-again)
- [Selecting and copying text in EXBrowser](#selecting-and-copying-text-in-exbrowser)
- [The pointer over links](#the-pointer-over-links)
- [EXBrowser and Wikipedia](#exbrowser-and-wikipedia)
- [Progressive JPEG](#progressive-jpeg)
- [ExEditor, pop-up menus, tabs](#exeditor-pop-up-menus-tabs)
- [ExEditor writes RTF](#exeditor-writes-rtf)
- [exide](#exide)
- [Profile folders](#profile-folders)
- [toolinst](#toolinst)
- [`dlopen` and `dlsym`](#dlopen-and-dlsym)
- [POSIX shared memory](#posix-shared-memory)
- [The graphics no longer flicker](#the-graphics-no-longer-flicker)
- [Signals are really delivered, and there is `mprotect`](#signals-are-really-delivered-and-there-is-mprotect)
- [BSD sockets](#bsd-sockets)
- [Rust's `std` runs inside EX-OS](#rusts-std-runs-inside-ex-os)
- [The browser starts at 32 MB too](#the-browser-starts-at-32-mb-too)
- [ExJs has regular expressions](#exjs-has-regular-expressions)
- [Tabs: in the editor, in exide and in the browser](#tabs-in-the-editor-in-exide-and-in-the-browser)
- [ExJs reads today's JavaScript](#exjs-reads-todays-javascript)
- [ExJs: `throw`, `try`, `switch` and prototypes](#exjs-throw-try-switch-and-prototypes)
- [The right button, dragging, and file associations](#the-right-button-dragging-and-file-associations)
- [Pennello writes](#pennello-writes)
- [Pennello, the paint program](#pennello-the-paint-program)
- [EXBrowser lays out the page](#exbrowser-lays-out-the-page)
- [Searching Wikipedia](#searching-wikipedia)
- [Calctor as an adding machine](#calctor-as-an-adding-machine)
- [The mouse wheel](#the-mouse-wheel)
- [A fast mouse no longer freezes the desktop](#a-fast-mouse-no-longer-freezes-the-desktop)
- [gfedit: F5 and BASIC INPUT](#gfedit-f5-and-basic-input)
- [ExJs: `Date` and `valueOf`](#exjs-date-and-valueof)
- [ExJs: implicit `toString` and `join` without a limit](#exjs-implicit-tostring-and-join-without-a-limit)
- [Forms to local pages, and the image button](#forms-to-local-pages-and-the-image-button)
- [`onclick=` runs even without a `<script>`](#onclick-runs-even-without-a-script)
- [Animated GIFs move](#animated-gifs-move)
- [A click in a page's text box puts the caret there](#a-click-in-a-pages-text-box-puts-the-caret-there)
- [EXBrowser understands em, rem and %](#exbrowser-understands-em-rem-and-)
- [EXBrowser draws CSS borders, and padding](#exbrowser-draws-css-borders-and-padding)
- [Immagini, the picture viewer, and PNGs of every kind](#immagini-the-picture-viewer-and-pngs-of-every-kind)
- [Ctrl+C on the text console asks, then stops](#ctrlc-on-the-text-console-asks-then-stops)
- [Searching Wikipedia, and @media rules](#searching-wikipedia-and-media-rules)
- [Calctor, the calculator](#calctor-the-calculator)
- [The desktop: the profile folder, the drives, the log](#the-desktop-the-profile-folder-the-drives-the-log)
- [The editor finds and replaces](#the-editor-finds-and-replaces)
- [Archivi opens tar files, and the file manager renames](#archivi-opens-tar-files-and-the-file-manager-renames)
- [`runbas`: QBASIC programs run on their own](#runbas-qbasic-programs-run-on-their-own)
- [Program paths in a file](#program-paths-in-a-file)
- [HTTPS: from 23 to 28 sites out of 30 — AES-GCM, P-256 and TLS 1.2](#https-from-23-to-28-sites-out-of-30--aes-gcm-p-256-and-tls-12)
- [The Download window, in the toolkit](#the-download-window-in-the-toolkit)
- [EXBrowser asks "open or download", and Archivi takes directories](#exbrowser-asks-open-or-download-and-archivi-takes-directories)
- [Certificates: the chain stops at the first root, and more can be added](#certificates-the-chain-stops-at-the-first-root-and-more-can-be-added)
- [The browser is called EXBrowser](#the-browser-is-called-exbrowser)
- [Icons: a toolkit API, and the desktop menu that uses it](#icons-a-toolkit-api-and-the-desktop-menu-that-uses-it)
- [ZIP archives: one library, one command and one window](#zip-archives-one-library-one-command-and-one-window)
- [Archivi: a real table, and an archive that builds itself](#archivi-a-real-table-and-an-archive-that-builds-itself)
- [Minimize, and the taskbar of open programs](#minimize-and-the-taskbar-of-open-programs)
- [tar, gzip and gunzip](#tar-gzip-and-gunzip)
- [The login page no longer gets covered by the boot messages](#the-login-page-no-longer-gets-covered-by-the-boot-messages)
- [EX-IDE's new project picks where, and remembers it](#ex-ides-new-project-picks-where-and-remembers-it)
- [`wserver` uses the 2D engine where there is one, and draws itself where there is not](#wserver-uses-the-2d-engine-where-there-is-one-and-draws-itself-where-there-is-not)
- [The SiS 2D engine draws: 14x on screen-to-screen copies](#the-sis-2d-engine-draws-14x-on-screen-to-screen-copies)
- [The clock can be set: kernel 0.218 and `/bin/date`](#the-clock-can-be-set-kernel-0218-and-bindate)
- [A bare `ls` also says when and what](#a-bare-ls-also-says-when-and-what)
- [USB sticks get ejected, not yanked: `eject`](#usb-sticks-get-ejected-not-yanked-eject)
- [A remote session that goes neither deaf nor mute: `telnetd` 0.008](#a-remote-session-that-goes-neither-deaf-nor-mute-telnetd-0008)
- [A half-finished update must not leave a dead machine](#a-half-finished-update-must-not-leave-a-dead-machine)
- [Big files over lines that drop: resuming](#big-files-over-lines-that-drop-resuming)
- [`ftpswap`: a directory and an FTP server, kept in step](#ftpswap-a-directory-and-an-ftp-server-kept-in-step)
- [NASM on the tools CD, and the other syntax](#nasm-on-the-tools-cd-and-the-other-syntax)
- [A thread's stack grows on demand](#a-threads-stack-grows-on-demand)
- [The «Cerca» box, and two boxes in the same bar](#the-cerca-box-and-two-boxes-in-the-same-bar)
- [The working directory belongs to both (and the environment already did)](#the-working-directory-belongs-to-both-and-the-environment-already-did)
- [Stopping a thread means asking it to](#stopping-a-thread-means-asking-it-to)
- [Condition variables and semaphores, on top of the sleeping wait](#condition-variables-and-semaphores-on-top-of-the-sleeping-wait)
- [Threads: several flows inside the same program](#threads-several-flows-inside-the-same-program)
- [The editor shortcuts, and something I had written wrong](#the-editor-shortcuts-and-something-i-had-written-wrong)
- [Cut and paste: a control moves between forms](#cut-and-paste-a-control-moves-between-forms)
- [Redo: the same function with the two stacks swapped](#redo-the-same-function-with-the-two-stacks-swapped)
- [Search actually works, and no browser port was needed](#search-actually-works-and-no-browser-port-was-needed)
- [The layout leaves browser.c, and the proof is that you cannot see it](#the-layout-leaves-browserc-and-the-proof-is-that-you-cannot-see-it)
- [The EX-IDE manual becomes a page, with an index](#the-ex-ide-manual-becomes-a-page-with-an-index)
- [Anchors: a link that goes to a point in the page](#anchors-a-link-that-goes-to-a-point-in-the-page)
- [Undo: photograph everything, do not record what changed](#undo-photograph-everything-do-not-record-what-changed)
- [The handles can be pulled, and the manual actually explains](#the-handles-can-be-pulled-and-the-manual-actually-explains)
- [More than one window: the designer learns to count](#more-than-one-window-the-designer-learns-to-count)
- [Save as, Replace, and a name that lagged behind](#save-as-replace-and-a-name-that-lagged-behind)
- [Files and Directory: one new window, and one that did not need writing](#files-and-directory-one-new-window-and-one-that-did-not-need-writing)
- [The project card, and a rewrite that should not have happened](#the-project-card-and-a-rewrite-that-should-not-have-happened)
- [The compiler window: real GCC, inside EX-OS, from a drawing](#the-compiler-window-real-gcc-inside-ex-os-from-a-drawing)
- [Three defects found by using it, and none was where it seemed](#three-defects-found-by-using-it-and-none-was-where-it-seemed)
- [EX-IDE: draw a window, out comes C that runs](#ex-ide-draw-a-window-out-comes-c-that-runs)
- [Windows inside a window: the MDI container](#windows-inside-a-window-the-mdi-container)
- [Coloured text, and a second text area that was never born](#coloured-text-and-a-second-text-area-that-was-never-born)
- [The toolkit learns five controls, and where new ones go](#the-toolkit-learns-five-controls-and-where-new-ones-go)
- [The window stays alive while downloading, and the mailbox has an owner](#the-window-stays-alive-while-downloading-and-the-mailbox-has-an-owner)
- [The browser runs JavaScript, and there are two engines](#the-browser-runs-javascript-and-there-are-two-engines)
- [Cookies, both halves of them](#cookies-both-halves-of-them)
- [Sound: four cards and MIDI](#sound-four-cards-and-midi)
- [Google is not a target for the browser, and now we know why](#google-is-not-a-target-for-the-browser-and-now-we-know-why)
- [The desktop has a console of its own, shuts down, and speaks your language](#the-desktop-has-a-console-of-its-own-shuts-down-and-speaks-your-language)
- [The browser holds a real page: layout, images, characters, forms](#the-browser-holds-a-real-page-layout-images-characters-forms)
- [Real users: two accounts, `sudo`, and a recovery when login is broken](#real-users-two-accounts-sudo-and-a-recovery-when-login-is-broken)
- [`ls -l`: permissions and owners are visible](#ls--l-permissions-and-owners-are-visible)
- [The screen is recomposed only where it changed](#the-screen-is-recomposed-only-where-it-changed)
- [The browser: tables, lists, `<pre>` and style sheets all the way](#the-browser-tables-lists-pre-and-style-sheets-all-the-way)
- [«About», and the clock that was not disappearing](#about-and-the-clock-that-was-not-disappearing)
- [A browser: from the network to the screen](#a-browser-from-the-network-to-the-screen)
- [Fonts: TrueType, measured against FreeType](#fonts-truetype-measured-against-freetype)
- [`rename` replaces the destination, like POSIX](#rename-replaces-the-destination-like-posix)
- [The taskbar: the clock, and applications that add themselves](#the-taskbar-the-clock-and-applications-that-add-themselves)
- [The graphical interface: a window server in ring 3](#the-graphical-interface-a-window-server-in-ring-3)
- [The desktop: `/exwin`, taskbar and start menu](#the-desktop-exwin-taskbar-and-start-menu)
- [Shared libraries: `exwin.so`, `exdlg.so`, `libc.so`](#shared-libraries-exwinso-exdlgso-libcso)
- [Installing by components](#installing-by-components)
- [Four old defects, and what they have in common](#four-old-defects-and-what-they-have-in-common)
- [Drivers: the machine picks them, they are not copied wholesale](#drivers-the-machine-picks-them-they-are-not-copied-wholesale)
- [A bigger shell: VESA graphics mode and `svga.drv`](#a-bigger-shell-vesa-graphics-mode-and-svgadrv)
- [Two images, and nothing is left out](#two-images-and-nothing-is-left-out)
- [The tools install themselves: `toolinst`](#the-tools-install-themselves-toolinst)
- [Paths and messages: say one thing, and say it true](#paths-and-messages-say-one-thing-and-say-it-true)
- [Networking — from the PCI bus to an FTP client](#networking--from-the-pci-bus-to-an-ftp-client)
- [CPU — SSE, SSE2, SSE3, MMX](#cpu--sse-sse2-sse3-mmx)
- [Filesystems](#filesystems)
- [Shell and commands](#shell-and-commands)
- [libc](#libc)
- [Compilation chain](#compilation-chain)

---

## What's new

Entries are marked **tested** when the work has been verified running inside
EX-OS, **to be tested** when the code is there but the proof that counts —
the one on real hardware or on the real case — has not been done yet.

### A command line up to 32 KB, at the console and over telnet (kernel 0.353, sh 0.014, kbd.drv) — tested in QEMU and on the test PC

- **Before**, a line typed over telnet or ssh stopped at 255 characters (the kernel's pty line,
  256 bytes) and at the console at 511 (sh, and one `kbd.drv` message). A pasted command, or a
  `gcc` line with many files, arrived cut.
- **The pty** starts with a 256-byte line and doubles it when needed up to `PTY_RIGA_MAX`
  (32,768, what a spawn carries); the input ring grows to take the whole line. A longer line is
  cut there and the session lives on.
- **sh 0.014**: one limit, `MAX_CMDLINE`, for the prompt, scripts and `-c`. The history (24
  lines) keeps every line at its own length in an arena that grows, not 24 copies of 512 bytes;
  if the line arrives in several pieces, the shell gathers them up to the newline.
- **kbd.drv**: at the console the line and the ring grow up to 32,768; a line longer than one
  IPC message arrives in several messages instead of being cut. The kernel's fallback keyboard
  (`tty.c`) has a fixed 32 KB.
- **Tested** with `tools/prova_riga_lunga.sh` at 32 and 64 bit, 13 out of 13: over telnet lines
  of 300, 4000, 20,000 and 32,000 characters, each writing a file compared byte for byte on the
  disk after power-off; one of 40,000 is cut, no file, and the session answers; at the console
  a line of 1200 typed key by key and recalled from the history. Also fine: termios, telnet
  (several sessions, end of session), ssh, nano, the shell test, VLC, locks and epoll.
- **On the test PC** (.253, 64 bit): updated with `netupdate -check -yes` from 0.352 and
  rebooted; `netupdate -verifica` all fine. Over telnet the lines of 300, 4000, 20,000 and
  32,000 characters arrive whole (306, 4006, 20,006 and 32,006 bytes in the files), the one of
  40,000 is cut and the session answers.

### netupdate 0.051 and 0.052: avvio and sistema are always looked at — tested in QEMU and on the test PC

With 0.050 an application with the same build as the installed one was not looked at any more,
and so neither were `avvio` and `sistema`, whose essential files (kernel, modules, libc) are checked
on the disk: a stale `ext4.mod` under a register that called it right stayed, and the disk came up
read-only. The `sempre` applications are never skipped. `tools/prova_modulo_disco.sh` 16 of 16;
`prova_menu_netupdate.sh` and `prova_verifiche.sh` publish the new menu and kernel in
`app/<id>.txt` and `app.txt` too, as `mknetinst.sh` does, not only in `elenco.txt`.

Looking at `avvio` every time, on the .253 each `-check` offered `boot/stage2.bin` "da verificare",
rewrote it with the boot sector and said "IL KERNEL E' CAMBIATO: riavvia". On the disk `stage2.bin`
never matches the list: `bootinstall` writes the sectors of `rdboot.lz4` in its `RDMAPPA` table,
and `svga.drv` the video mode after `SVGAMODE`. With 0.052 the file is the server's if, with the
table empty as published and one value of the mode byte, it has the server's hash. On the .253
(0.352, 64 bit) `netupdate -check` says "niente" and every check is fine.

### Kernel 0.352: at 64 bit the system calls run with interrupts on — tested in QEMU and on the test PC

At 64 bit `system -load` remounted the disk, printed "Riaccendo:" and stopped there: the machine
answered no more. The kernel spun forever in `ata_attesa_ms`, called by `sys_mount` for the empty
CD drive of `kernel.cfg`, with interrupts off: the clock did not move and the wait never ended. At
32 bit `int 0x80` is a trap gate (0xEF) and a system call leaves interrupts on
(`kernel/arch/x86/idt.c` says why); at 64 bit it had been an interrupt gate (0xEE) since the first
stage, and every system call ran with IF=0. Now it is a trap gate at 64 bit too
(`kernel/arch/x86_64/idt64.c`). `tools/prova_system.sh` at 64 bit: 8 of 8 (7 before). On the .253
(Q9650, 64 bit) `system -unload -yes` and `system -load` from the console: the disk is root again,
writable.

Also fixed, in the tests: `prova_sorgenti.sh` and `prova_pennello.sh` call scripts through
`sh`/`bash` (MEGA strips the execute bit); `libgit2-exos/prova.sh` takes headers and library from
the build, not the sysroot; the disk of `prova-fb64-nativo.sh` grows with the tools tree (a fixed
400 MB no longer held it); `prova_exbrowser_misura.sh` puts the pointer on the grip with `vai:`,
which checks it, and no longer says `[OK]` when the window did not move.

### exide compiles at 64 bit too (exide 0.027) — tested in QEMU and on the test PC

On 64-bit EX-OS a new exide project did not compile: "`/exos/start.S`: no such file or
directory" and "`exwin.h`: no such file or directory". The 64-bit tools CD
(`tools/mktools64.sh`) had neither the headers of ExWin and the other libraries nor `start.S` and
`libexos_avvio.a`; and even with them exide wrote `-m32` in its compile line.

- exide writes the line of its own architecture: `-m64` and 4 KB pages at 64 bit, `-m32` at 32.
- `mktools64.sh` copies the same files as the 32-bit CD, with the `ESOS_STUB` list read from the
  Makefile (one list only).
- `exhttp.h` includes `http.h`, which was on neither CD: it is in `ESOS_STUB` now.
- The 32-bit CD lacked the specs of `tools/gcc-exos/specs-condivise.sh`: its native `gcc` always
  linked `-static` ("attempted static link of dynamic object exwin.so"). Now it has them, with
  `ld-exos.so`, `libc.so`, `libgcc_s` and `libstdc++.so` in `/exos/lib`, as at 64 bit.

If an older project does not compile: in exide **Strumenti > Compilatore**, "Scrivi compila.sh",
and the new line replaces the one with `-m32`. `tools/prova_exide_compila.sh` (and `BIT=64`):
exide generates, the gcc inside EX-OS compiles, the window opens, at 32 and 64 bit. On the test
PC the project that did not compile now opens its window.

### VLC as on Linux: libvlc.so, libvlccore.so and every plugin a .so opened with dlopen (voce 105, step 4) — tested in QEMU and on the test PC

`/exos/bin/vlc` now runs on `/exos/lib/libvlc.so.5` and `libvlccore.so.9`, and each of its 241
modules is a `.so` in `/exos/lib/vlc/plugins/<kind>/`, opened with dlopen when VLC needs it, as on
Linux. Until now VLC was one program with every plugin linked in: three plugins with the same
name were lost (VLC names a module after its source file: flac, vc1, file), two tables had to be
made static by sed, and the FLAC packetizer was missing, so no FLAC played. The FLAC file of
`tools/vlc-exos/prova.sh` now plays at 32 and 64 bit like the rest.

- **libtool learns EX-OS** (`lib_libtool_exos` in `tools/librerie-exos/comune.sh`): in every
  per-system case of libtool's code in a configure, `exos*` goes where `linux*` is. Before it,
  libtool said "dynamic_linker=no" and built only static archives, for every autoconf port.
- **`gcc -shared` links the libc** as on Linux (`tools/gcc-exos/specs-condivise.sh`): `-lc` is
  `/lib/ld-exos.so`, so a library gets `DT_NEEDED ld-exos.so` and its libc symbols are checked
  when it is linked. Without it libtool's C++ links (`-nostdlib`, then the libraries g++ names)
  had no libc at all. A static program with `libc.a` that opens such a library skips that
  `DT_NEEDED` and lends its own libc, as before (`lib/libc_dl.c`).
- **libstdc++.la tells the truth** (`tools/librerie-exos/la-condivisa.sh`, called by
  `cxx-condivise.sh`): GCC's libtool had written "static only", and every C++ shared object
  linked on top of Qt took the non-PIC `libstdc++.a`.
- **ncurses is a shared library**: `libncurses.so.6`, `libpanel`, `libmenu`, `libform`,
  `libncurses++` (the `.a` stay for `-static` programs). VLC's text interface is a plugin, and a
  `.so` cannot carry the non-PIC archive. `tic`, `tput` and the others now use it.
- **`sincos`, `sincosf`, `sincosl`** declared in `<math.h>` (with `_GNU_SOURCE`): openlibm had
  them, the header did not; VLC's OpenGL output calls `sincosf`.
- **The modules' list** `plugins.dat` is written on the machine by
  `/exos/lib/vlc/vlc-cache-gen` (a cross build cannot run it): netupdate's check for VLC
  (`tools/netinst/verifiche/vlc.sh`) makes it when it is missing. Without it VLC still starts,
  but opens every module at each start.
- Only `glspectrum` is not built: EX-OS has EGL and GLES2, no libGL to link `-lGL` with (VLC's
  OpenGL output takes its functions through EGL and is there).

`tools/vlc-exos/prova.sh` at 32 and 64 bit: the three audio files compared with FFmpeg (FLAC
included), the ncurses interface and its volume, the three videos in their window, `vlc-cache-gen`
writes `plugins.dat`, the Qt interface with the cone: all `[OK]`. On the 64-bit test PC, updated
with netupdate: FLAC and MP3 on the real sound card, the ncurses interface over telnet, gizmo.mp4
and vp9cake.webm in their window, Qt with the cone; `plugins.dat` written by netupdate's check.
The ncurses `.so` files did not reach the tools CD (`tools/ncurses-exos/su-cd.sh` copied the
`.a` only): now they do.

### The archive by application: netupdate brings only what changed, ExPackage has Update and Upgrade (netupdate 0.050, expackage 0.002, voci 106 and 107) — tested in QEMU and on the test PC

- **The repository has a new archive next to the old one.** `app.txt` has one block per
  application: name, what it wants, **version** (the program's: VLC 3.0.24, nano 9.3) and
  **build**, the moment (UTC, `YYYYMMDDhhmm`) its files last changed; `app/<id>.txt` has its files
  with bytes and sha256. `versione.txt` carries the hash of `app.txt` (`app = ...`). The build
  moves by itself when the list moves (`tools/netinst/archivio_app.py`, run by `mknetinst.sh`): a
  rebuild with the same version — every port on ld-exos.so on 19/11 — arrives all the same.
- **The old `catalogo.txt` and `elenco.txt` hold only `[avvio]` and `[sistema]`.** A netupdate
  before 0.050 updates kernel and system (netupdate 0.050 is in it), and from the next round it
  reads `app.txt`. Kernel and system stay whole.
- **An application with the same build as the installed one is not looked at**: its list is not
  downloaded and its block in the register stays as it is. The others are compared file by file
  as before, and the register (`/boot/netupdate.reg`) records the version and build of each.
- **`-check`** updates only what is installed, and names the new dependencies that are missing;
  **`-check -yes`** does it without asking; **`-check -yesall`** installs those dependencies too.
  New applications come only with `-install:<app>`, with everything they want and their start
  menu entry if they are graphical (`grafica = si` in `app.txt`).
- **`netupdate -list`** the server's applications with their state (no, installato,
  **aggiornabile**) and version; **`-list:<piece>`** those whose name contains it; `-p` one page at
  a time, `-tab` for programs (five more columns at the end: version, build, updatable, installed
  version, graphical). `-install:list` stays as it was.
- **Files that do not arrive** are retried by themselves `tentativi` times (`/boot/netupdate.cnf`,
  3 if it does not say), then netupdate asks (with `-yes` it stops); those left go to
  `/boot/netupdate.err`, and the next `-check` names them first. The file goes when a `-check` ends
  with none missing. The `sistema.sh` check reports it.
- **ExPackage**: **Update** reads the list and the versions again, installing nothing;
  **Upgrade** updates the installed ones that are updatable (`netupdate -check -yes`), then
  removes the `[-]` and installs the `[+]` with the `[d]`. Next to the name, the repository's
  version; the state "aggiornabile".
- **Fault found (the 0.346 -> 0.351 jump):** between the update and the reboot the old kernel is
  still running, and it does not know `/lib/ld-exos.so`: `ls`, `sync` and every new program die
  with a page fault at 0. No harm done: `reboot` is inside the shell, and after the reboot all
  works. After "IL KERNEL E' CAMBIATO", just reboot.
- **Fault found: a VLC installed by netupdate did not start.** Qt 5 wants libpng16, FreeType and
  HarfBuzz, ported but never put on the CD or in the repository (the VLC tests copied them by
  hand), and zlib, pcre2, zstd its `vuole` did not name; the gcc of `base` wants libz and libzstd,
  nasm libz. Now there are `[libpng]` `[freetype]` `[harfbuzz]`, the right `vuole` (zlib and zstd
  no longer want `base`), and `mknetinst` checks with `tools/netinst/controlla_dipendenze.py`
  that every library asked for (`readelf -d`) comes with the chain of `vuole`. On a machine
  already updated: `netupdate -check -yesall`.
- **Fault found: toolinst (0.007)** kept 12 groups, 10 `solo`, 4 `file`, 8 `nota`, 4 `vuole` and
  read 16 KB of catalogue: the tools CD has 38 groups and 25 KB. Every list now grows with the
  file (`tools/prova_toolinst.sh`).
- **The first -check of 0.050 does not fetch again what is already right**: a file the register
  does not know but whose bytes and sha256 on the disk are the server's stays (on the test PC
  5141 files recognised, 2672 really changed).
- **Fault found: between the update and the reboot nobody could get in by telnet.** For every
  connection telnetd runs `/bin/telnetd` again, which was dynamic: with `/lib/ld-exos.so` already
  new and the old kernel it died at EIP 0 and the session closed without a byte (on the test PC
  and in QEMU). Now **telnetd, sshd and netupdate are static** like login, install and sh
  (`STATICI_OBBLIGATI` in the Makefile, `STATICI` in `tools/costruisci-utente64.sh`): a new
  session gets in, updates, and `reboot` (built into the shell) reboots. The other dynamic
  commands (`ps`, `ls`...) still die on the old kernel: just reboot.
- Tests: `tools/prova_netupdate_app.sh`, `tools/prova_expackage.sh` with Upgrade.

### Every program on /lib/ld-exos.so, the EX-OS libraries are real .so files (kernel 0.351, voce 105) — tested in QEMU

The rest of the third step of voce 105. Until now the shared libc served the ports; the system's
own programs still carried `libc.c` (at 64 bit) or went through `libc.so` and its bridges (at 32
bit), and the twelve EX-OS libraries were ET_EXEC files at fixed addresses with a table of names.
Now there is one way, the Unix one.

- **The kernel keeps one copy of each shared object** (`kernel/loader/condivise.c`,
  `SYS_SO_MAPPA` 274): code and read-only data are read once for the whole system and mapped in
  every process that loads the file, only the writable part is private. `ld-exos.so` and the
  libraries it loads (`DT_NEEDED`, `dlopen`) come from there. Measured with `tools/prova_ld.sh`
  (11/11 at 32 and 64 bit): six sleeping copies of a program cost 208 KB each at 32 bit (215 at
  64) against 219 (231) of the same program built `-static`.
- **Every command, every ExWin application, exilla and vlcapri are on `/lib/ld-exos.so`**, at 32
  and 64 bit. Static remain the drivers (they start before there is a filesystem to read a
  library from), `login`, `install`, `soccorso` and `sh` (the way in and the way to repair), and
  `extaskbar`, `wifid`, `hello`, `prova64`, `readlink`. `make verifica-statici` checks them by
  looking for PT_INTERP. At 64 bit every program carried the libc and ExWin inside: `pm` goes from
  451 KB to 78 KB, `/bin` from 5.7 MB to 2.8 MB, `/exwin/bin` from 10 MB to 1.2 MB, the ISO from
  39 MB to 28 MB.
- **The twelve libraries are ELF shared objects** (ET_DYN, `-fPIC`, no text relocations): exwin,
  exdlg, eximg, exfont, exhtml, excss, exjs, quickjs, exdom, exhttp, exsuono, exzip. What each
  offers is its version script, `lib/<name>/<name>.ver` (the same names as the old tables). A
  program names the ones it cannot live without (`exwin.so`, `exdlg.so`, `exhttp.so`,
  `exhtml.so`, `exdom.so`, `exzip.so`) and the interpreter loads them; the optional ones (`excss`,
  `exsuono`, the two JavaScript engines) keep a small stub that opens them with `dlopen` and lives
  without them. `exlib_apri()` and `exlib_simbolo()` are still there, on top of
  `dlopen`/`dlsym`.
- **Gone:** `libc.so` and its bridges (`lib/libc_ponti.c`, `tools/genlibc.py`), the loader
  `lib/exlib/exlib.c`, the fixed library addresses between 0x04000000 and 0x08000000
  (`lib/*/*.ld`, `tools/fette.py`) and the kernel's old library loader (`kernel/loader/lib.c`):
  `SYS_LIB_APRI` (248) and `SYS_LIB_TROVA` (238) now return ENOSYS.
- **The floppy too:** `ld-exos.so` is in `/rdboot/lib`, and the kernel looks for an interpreter
  in `/lib/...` also under `/rdboot` and `/cdrom`, so a program on the floppy or on the CD starts
  before anything is mounted on `/lib`.
- **The address of a function is the same everywhere.** In a program built without `-fPIC` the
  address of `strlen` is its PLT entry: `dlsym(RTLD_DEFAULT, "strlen")` gave the libc's and
  `&strlen` the program's. Now, as in glibc, an undefined FUNC symbol with a value in the
  program is the definition for every lookup except the JUMP_SLOT that fills that PLT entry.
- **The stage 2 bounce buffer moved** (`RDSEG` 0x90000): it read the base system at 0x80000, on
  top of the tail of a kernel larger than 448 KB, and the 64-bit kernel 0.351 (461,008 bytes)
  died after "J" without a line on the serial port. The kernel can now be 512 KB, and the
  Makefile refuses a larger one.
- **exide 0.026** writes the link line of a program on `ld-exos.so` with the `.so` files of
  `/exwin/lib` (or `/cdrom/exwin/lib`); the tools CD carries `libexos_avvio.a` and the public
  headers, and only the stubs of the optional libraries.
- ! A program built before 0.351 against `libc.so` or a library stub does not start any more:
  rebuild it, or take it again with `netupdate`.

### The shared libc: /lib/ld-exos.so, and the kernel starts the interpreter (kernel 0.350, voce 105) — tested in QEMU

The third step of voce 105. Until now every program carried the libc inside it (`libc.a`), and a
program linked to shared objects loaded them itself at start. Now there is a shared libc, as on
every Unix, and a program linked without `-static` uses it.

- **The kernel honours PT_INTERP** (0.350). A program that names an interpreter gets it mapped
  after its own image (an ET_DYN, read once into the process), an auxv at the top of the stack
  (`AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_PAGESZ`, `AT_BASE`, `AT_ENTRY`) whose address goes in the
  first of the four words the program starts from (0 before, and still 0 for a static program),
  and starts there. Until 0.349 PT_INTERP was ignored.
- **/lib/ld-exos.so is the libc and its own interpreter**, as musl does it (`lib/libc_ld.c`): it
  relocates itself, loads the program's `DT_NEEDED` with the same loader as `dlopen`
  (`lib/libc_dl.c`), relocates the program (COPY relocations too), binds its own variables to the
  program's copies (`environ`, `optarg`, `stdout`...), runs its constructors and the libraries',
  and jumps to the program. The program keeps its own start (`libexos_avvio.a`: `_libc_start`,
  its constructors, `main`); in the sysroot `libc.so` is a linker script that joins the two.
  `-static` still takes `libc.a`.
- **The order of the global scope is Linux's:** the libc enters where a `DT_NEEDED` names it, so a
  library before it wins (VLC's `libvlc` defines `putenv`, `setlocale`... for its plugins).
- **`dlsym(RTLD_NEXT)` is real:** the first definition after the caller's module. Until now it
  was the global scope, and a library that wraps a libc function found itself again.
- **VLC uses its `bin/override.c` as it is**: with `libc.a` inside, its `putenv`, `setlocale`,
  `strerror`... were a second definition of the libc's names and the file was replaced by an empty
  one; now the program's win, and reach the libc's with `dlsym(RTLD_NEXT)`. `tools/vlc-exos/prova.sh`
  as before at 32 and 64 bit (the FLAC file waits for step 4, VLC's plugins as `.so`).
- A program built before, with `libc.a` inside and its own `.so`, also has PT_INTERP but does not
  name `ld-exos.so`: the interpreter lets it start as before. ! With kernel 0.350
  `/lib/ld-exos.so` must be there: kernel and libc go together.
- Built by `tools/costruisci-ld.sh` (the two sysroots, `/lib` of both CDs, the libc of today of
  the third-party builds). Tests: `tools/prova_ld.sh` 10/10 at 32 and 64 bit; `tools/prova-dl/`
  (start, dlopen, 40 libraries with TLS and threads, C++ with exceptions, QuickJS) passes with
  the libc inside and with the shared one (`LIBC_CONDIVISA=1` for `prova-tante.sh`).
- **The ports are on it**: bzip2, xz, lz4, zstd, PCRE2, Expat, OpenSSL, ncurses, FFmpeg,
  MariaDB, httpd, PHP, Valkey, Mesa, nano and VLC are built without `-static`, every program
  with PT_INTERP `/lib/ld-exos.so` and the shared libraries it names. The tests of each pass at
  32 and 64 bit (nano, Valkey, EGL, OpenSSL, httpd with PHP, MariaDB with the four connectors,
  the MariaDB service on a disk installed from the CD, the compressors, VLC). `[mariadb]` now
  wants `bzip2`, `xz`, `lz4` and `pcre2` too: `mariadbd` names them. ! A test that copies the
  libraries a program names skips `ld-exos.so`: it is the system's, in `/lib` of the CD.

### pthread_cancel stops a thread that waits, FD_CLOEXEC closes at exec, posix_spawn (kernel 0.349, ip.drv 0.027, voce 105) — tested in QEMU

The first step of voce 105: what VLC's generic POSIX code needs from EX-OS, implemented instead
of worked around (until now VLC was built on its Android thread subset and its own `netconf.c`).

- **pthread_cancel cuts a wait short.** Until 0.348 a thread asleep in `read` or `poll` was
  cancelled only when the data came: the request did not reach a wait in the kernel. Now the
  libc marks the POSIX cancellation points (`read`, `write`, `poll`, `select`, `epoll_wait`,
  `recv`, `send`, `accept`, `connect`, `nanosleep`, `sleep`, `usleep`, `clock_nanosleep`,
  `sem_wait`, `sem_timedwait`, `waitpid`, `sigwait`, `sigsuspend`, `pause`, `fcntl(F_SETLKW)`)
  with a bit in the syscall number (`SYS_PUNTO`); the kernel ends that wait with EINTR as a
  caught signal does, or answers EINTR at once when the request is already there (POSIX: a
  `read` with data ready still acts), and the libc ends the thread with its cleanup handlers.
  With cancellation disabled nothing changes. A kernel before 0.349 answers ENOSYS to the bit,
  asked once: cancellation as it was.
- **accept and connect can be interrupted** (ip.drv 0.027): `IP_MSG_RITIRA` answered only a
  pending `poll` or `epoll_wait`; now also a pending accept (-EINTR, also when queued behind
  another process's accept) and connect (-EINTR, the half-open connection is dropped). So a
  signal cuts them short too, as on Linux (accept restarts with `SA_RESTART`, connect never).
- **FD_CLOEXEC is real.** `fcntl(F_GETFD)` answered 0 and exec kept every descriptor, so after
  `fork` + `exec` the child held the parent's pipes and their reader never saw the end. Now the
  bit is kept (`open(O_CLOEXEC)`, `pipe2(O_CLOEXEC)`, `F_SETFD`, `F_DUPFD_CLOEXEC`, the new
  `dup3`; `dup`, `dup2` and `F_DUPFD` clear it), and exec closes those descriptors and gives back
  the numbers of the libc's own (sockets, epoll, shm), whose state stayed in the old program.
- **posix_spawn and `<spawn.h>`:** fork, the file actions in the child in the order given (open,
  close, dup2, `addchdir_np`; a list that grows), the attributes (signal mask and defaults,
  `SETSID`, `SETPGROUP` for its own group, `RESETIDS`; one scheduling policy, `SCHED_OTHER` at
  priority 0), execve, or the `PATH` search for `posix_spawnp`. The exec error comes back
  through a pipe with O_CLOEXEC: `posix_spawn("/no/such")` is ENOENT and no child is left.
- **clock_nanosleep** (relative, and `TIMER_ABSTIME` on its own clock; ENOTSUP on a CPU clock)
  and **strerror_l** (one language of messages: strerror's).
- Found on the way: the syscall number in the frame kept the bit, and `ipc_recv_tmo`, which picks
  by that number the wait a signal cuts, took the one nothing cuts.

`tools/prova_annulla.sh` (`tools/prove/annullaprova.c`): a thread cancelled 300 ms into each of
those waits ends at once with `PTHREAD_CANCELED` and its cleanup run (nanosleep in 90 ms: the
kernel's sleep looks every 100 ms, as for `kill`), cancellation disabled leaves the `read` alone, a request
already there ends a `read` with data ready, `FD_CLOEXEC` through `fork` + `exec`, `posix_spawn`
with `adddup2` of a pipe read to its end, `clock_nanosleep` and `strerror_l`: 21 of 21 at 32 and
64 bit. `prova_eintr.sh`, `prova_segnali.sh`, `prova_epoll.sh` and `prova_moltifili.sh` still
pass.

**VLC is back on its own POSIX code:** `src/posix/thread.c` (real `pthread_cancel`, `mwait` on
`clock_nanosleep`, conditions on the monotonic clock), `error.c` (`strerror_l`) and `netconf.c`
(`posix_spawnp`), as they come in the archive. Gone from `tools/vlc-exos`: the Android thread
subset, `indirizzi.c` (futex stand-ins) and a `netconf.c` of its own; `<unistd.h>` now says
`_POSIX_CLOCK_SELECTION` and `_POSIX_SPAWN`. `tools/vlc-exos/prova.sh` as before at 32 and 64
bit, but for `sin-441-1s-44100.flac`, `[NO]` until the plugins are `.so` files: in the one-program
VLC the FLAC decoder and packetizer have the same entry name and the linker keeps one (the symbol
rename that hid it is gone: voce 105 wants the cause fixed). Next: the shared libc, in a clone of
the repository.

### Qt 5 on EX-OS, and VLC with its menus (tools/qt5-exos, kernel 0.348, voce 85) — tested in QEMU

VLC's window had no menu and no controls: in VLC 3 those are the Qt interface, and Qt was not
on EX-OS. Now it is, built the same way as Apache, PHP and MariaDB: a script, the newest sources,
a group on the tools CD, a check for netupdate, the tests at 32 and 64 bit.
- **Qt 5.15.19** (qtbase and qtsvg, the last public Qt 5), `tools/qt5-exos/costruisci.sh [32|64]`:
  Core, Gui, Widgets, Network with SSL (the OpenSSL 4.1 of the sysroot), Sql with SQLite, Xml,
  Svg, PrintSupport, Concurrent, Test, all shared, in `/exos/qt5`. The fonts are DejaVu 2.37 in
  `/exos/qt5/lib/fonts` (there is no fontconfig). The sources go in `tools/sorgenti-terzi.txt`;
  Qt signs nothing, so `aggiorna-sorgenti.sh` takes its published `.sha256` for these two only.
- **The platform plugin "exwin"** (`tools/qt5-exos/qpa-exwin`): every Qt window is an ExWin
  window, painted through the server's shared zone; keyboard, mouse, wheel, right button, close
  box, menus and tooltips (popup windows of the server). The mkspec is `exos-g++`.
- **What it took in the libc** (in the sysroots with the new
  `tools/librerie-exos/aggiorna-sysroot.sh`): `nfds_t`, `<byteswap.h>`, `O_LARGEFILE`,
  `_SC_SYMLOOP_MAX`, `statvfs64`, `CLD_*`, `waitid()` with `WEXITED`/`WNOWAIT`, `<resolv.h>`
  (`res_init`), `struct ifreq`/`ifconf` and the `SIOCGIF*` numbers (the interface list is empty
  until interfaces have names, voce 101), the IPv6 socket options and `struct ipv6_mreq`.
- **Kernel 0.348:** `waitpid` with `WNOWAIT` leaves the zombie (for `waitid`), and **a thread can
  read the console**: the foreground check compared the thread's own pid, so every thread but
  the first got "end of input" and the keys went to the shell. VLC's text interface reads the
  keyboard from a thread: `q` did not quit it. `tools/prova_curses.sh` now tests it
  (`tools/prove/cursesfilo.c`).
- **Every port rebuilt** on the new libc: `tools/librerie-exos/ricostruisci-tutto.sh` (new) does
  every `tools/<name>-exos/costruisci.sh` in order at 32 and 64 bit; Qt goes before VLC.
- **VLC:** `vlc -I qt film.mp4` opens VLC's window with its menus, the time bar, the buttons and
  the volume; the video stays in its own window (`embedded-video` off: VLC 3 puts it inside the
  interface only on X11, Windows and macOS). `vlc -I ncurses` is the text interface. The `[vlc]`
  group now wants `[qt5]`; VLC finds Qt through its RUNPATH `/exos/qt5/lib`.
- **Not yet in Qt:** QSharedMemory and QSystemSemaphore (no named semaphores or IPC between
  processes), QDnsLookup, QNetworkInterface (empty), file times (`futimens`), inotify. moc, rcc
  and uic run on the build machine only.
- **At 32 bit Qt is built with `-no-sse2`:** Qt 5 turns SSE2 on in all its libraries on its own
  and refuses to start without it; EX-OS at 32 bit runs on a Pentium MMX.
- **Tested in QEMU at 32 and 64 bit:** `tools/qt5-exos/prova.sh` 9/9 (`ciaoqt`: the window,
  typing in a line edit, a button, the File menu and its Esci, a property animation, a PNG
  written and read back, no FAULT); `tools/vlc-exos/prova.sh` with `vlc -I qt` (VLC's log says
  the "qt" interface is in use, its window on the screen) and `vlc -I ncurses` (the screen read
  back, `q` quits), the audio and the three videos as before; `tools/prova_curses.sh`,
  `tools/prova_tetti.sh`, `tools/prova_segnali.sh` and `tools/prova_telnet_piu.sh` for kernel
  0.348.
- **The middle of VLC's window was black, without the cone** (19 November): the plugin's
  `winId()` was the server's window id, which exists only from `setVisible`; Qt reads `winId()`
  once, when the window is made, and kept 0. With 0 a child widget that repaints alone (the cone
  fades in) finds no native parent, and Qt threw its flush away: painted, never on the screen.
  Now `winId()` is never 0. Found with `QT_EXWIN_DEBUG=1` (the plugin says what it paints and
  flushes) and `QT_LOGGING_RULES=qt.widgets.painting=true`, both passed by the two tests;
  `ciaoqt` checks `nativeParentWidget()` (QT-NATIVO) and the VLC test counts the cone's orange.
- `VLC_SOLO_COLLEGA=1 tools/vlc-exos/costruisci.sh` really only relinks now (9 s): `lib_per`
  emptied the build tree first, so it was a whole build from fresh sources (`LIB_TIENI=1` in
  `tools/librerie-exos/comune.sh`).
- **VLC's volume read 0% and did nothing** (19 November): `aout_exos` had no `volume_set`, so
  VLC had no volume at all (the Qt slider at 0%, `----` or 0% in ncurses). Now it is VLC's own
  software volume, as with ALSA (`modules/audio_output/volume.h`): the slider, `a`/`z` in
  ncurses and `--aout_exos-gain` (0..8) change the samples, VLC remembers the level for the next
  start, and the card's master volume (`audio -v`, the Volume panel) stays the user's. Measured on
  what the card plays, a 1 kHz tone at 10000: 1250 with `--aout_exos-gain=0.125`, 7303 after two
  `z` (90%, i.e. 0.9 cubed). The VLC test now plays that tone in ncurses and checks the WAV.
- **The end of every sound was cut, and a short one was never heard** (19 November): at the end
  `aout_exos` waited until the driver had *taken* every sample, then closed; but what the driver
  takes sits in the card's two halves, not heard yet, and closing stops the DMA on it. Every file
  lost its last tenth of a second, and a sound shorter than the two halves (all taken at the
  start) none at all. Now the drain also waits for two underflows of the ring (`sottoflussi`,
  one per interrupt with nothing left to take): both halves have played. `small-shot.mp3`: 0.21 s
  of 0.31 before, the whole of it now.
- **The VLC test now listens to each audio file alone:** QEMU's `wavcapture` records the card
  around each one, and the recording is compared with what FFmpeg on this machine decodes from
  the file: as long, as many seconds of sound, the same peak. The old check counted the seconds
  of sound of the whole run, so it passed even with a file that never reached the card.
  `sound.ogg` turned out to be silence (-91 dB: it still tests the Vorbis decoder, and the check
  wants silence back), and `small-shot.flac` is not decoded by VLC 3.0.24 even on Linux
  (`cvlc --aout afile` writes an empty WAV; FFmpeg decodes it): the test uses Firefox's
  `sin-441-1s-44100.flac` instead.

### GNU nano 9.3 (tools/nano-exos) — tested in QEMU

The text editor of every Linux, at 32 and 64 bit, on the ncurses of tools/ncurses-exos:
`/exos/bin/nano`, the system nanorc in `/exos/nano/nanorc` with the syntax colouring of
`/exos/nano/share/nano` (C, sh, Python, HTML, Makefile...). Group `[nano]` of the tools CD,
`netupdate -install:nano`, and a check for netupdate (`tools/netinst/verifiche/nano.sh`).
- **Sources:** nano 9.3 from ftp.gnu.org, signed by Benno Schulenberg (key
  `168E6F4297BFD7A79AFD4496514BBE2EB8E1961F`, from the GNU keyring), in
  `tools/sorgenti-terzi.txt`: `tools/aggiorna-sorgenti.sh -scarica nano` takes the newest.
- **No UTF-8:** libncurses is the byte library, the libc has no UTF-8 locale, the console is
  CP437 (voce 30).
- **What the libc lacked, now there (18 November):** `<stdio_ext.h>` (`__fseterr`, `__fpending`,
  `__freading`, `__fpurge`... as musl: gnulib's fseterr.c is an `#error` without them);
  `st_atim`, `st_mtim`, `st_ctim` in `struct stat` (POSIX 2008; the seconds of `st_mtime`,
  `tv_nsec` 0); `pathconf`/`fpathconf` with `_PC_PIPE_BUF`, `_PC_MAX_CANON`, `_PC_MAX_INPUT`,
  `_PC_VDISABLE`, and `fpathconf` on a pipe (it answered EBADF: a pipe has no size).
- **struct stat grew again, without breaking anything:** as for `st_rdev` on 7 November, stat,
  lstat, fstat and their `*64` get a third symbol (`__exos_stat3`, `__exos_stat64_3`...) for new
  code; `__exos_stat2` and the old names stay and fill only the struct they knew (the user's
  choice).
- nano is linked to the libc of today (`libc_di_oggi` in its script), not to the older one of
  the sysroots.
- **Tested:** `tools/nano-exos/prova.sh` 6/6 at 32 and 64 bit: `nano -V`, the title bar and
  the help lines read back from the screen, a line typed and saved with ^O, `cat` reads it,
  the system nanorc with the syntax files opens a `.c`, no FAULT.

### The fixed ceilings that were left go (kernel 0.347, voce 35) — tested in QEMU

Fixed numbers in the code, against the rule that a ceiling comes from the configuration or
the hardware. They all grow now, as the process and file tables have done since 0.282:
- **spawn and exec:** up to 0.346 at most 512 arguments and 32 KB of strings, and **the
  environment variables after the 32nd were dropped without a word** (a make or a mach has
  more). Now the kernel measures what the caller passes and allocates exactly that; the
  ceiling is the kernel heap (and 1 GB for one command line);
- **disks:** 16 block devices in all, 16 partitions per disk, 8 disks served by a process
  (USB sticks): now as many as there are. `disk` still shows 16 per disk (the `DiskInfo`
  ABI) and says when there are more;
- **kernel.cfg:** read in 8 KB (beyond, the last sections were lost), 16 `[modules]`, 32
  `[env]`: now the whole file and every line;
- **the floppy:** 64 open files; **shm_open:** 1024 zones per process (libc);
- **`dup()` and `fcntl(F_DUPFD)` of a socket** answered EOPNOTSUPP: now they work, the
  socket closes with its last descriptor and a fork hands it to the child once. `dup2` of a
  socket is still EOPNOTSUPP;
- the environment table in every process control block (32 × 128 bytes) was never used:
  gone, 4 KB less per process in the kernel band.
- **Tested:** new `tools/prova_tetti.sh` (3000 arguments and 200 variables through
  `spawn_ex` and `fork`+`execve`, 1500 shm zones, socket `dup` across a fork, a disk with 22
  logical partitions where the last one mounts and reads) and `tools/prova_tetti_cfg.sh` (a
  13.6 KB kernel.cfg with 20 more modules and 40 more variables, installed on disk), 32 and
  64 bit.
- **Still there:** an ELF with more than 4 segments is loaded whole instead of on demand
  (slower, correct); `gfedit` keeps 512 lines and 200 columns (it refuses to save a file it
  could not open whole); at most 4 redirections per spawn (`SPAWN_MAX_AZIONI`, an ABI); the
  128 KB kernel stacks in the first 64 MB (about 450 tasks, a decision to take with the user).

### A signal cuts a wait on a socket short (kernel 0.346, ip.drv 0.026, voce 37) — tested in QEMU and on the bench PC

- **poll, epoll_wait, recv and send on a socket return EINTR** when a caught signal arrives,
  as on Linux: recv and send start again after a handler with SA_RESTART, poll and
  epoll_wait never do. Until 0.345 a thread in poll on a socket did not wake with
  `pthread_kill`: Apache and MariaDB worked around it by looking every 100 ms.
- **How:** the wait is in ip.drv, so cutting it in the libc alone would leave ip.drv's late
  answer in the mailbox for the next call. The kernel has an IPC wait a caught signal ends
  (SYS_IPC_RECV_SEG 272, and _SEG_R 273 that SA_RESTART restarts); then the libc sends
  IP_MSG_RITIRA and ip.drv answers the pending wait at once — exactly one answer, so
  nothing stale is left. A new libc on an older kernel or ip.drv falls back to the old
  wait.
- **Tested:** new `tools/prova_eintr.sh` (`tools/prove/eintrprova.c`, against a libc.a made
  from today's sources), 32 and 64 bit: poll, poll with SA_RESTART, epoll_wait, the next
  poll finds the client at once, recv with SO_RCVTIMEO cut and restarted, `pthread_kill` on
  a thread in poll, a poll without signals waits its timeout: 8/8.
- **On the bench PC (.253, 64 bit, 18/11):** 0.346 and ip.drv 0.026 through netupdate,
  sha256 of the kernel checked before the reboot, `netupdate -verifica` all fine; the same
  test on the machine: 8/8.
- **Not yet:** a blocking recv or accept with no timeout still cannot be cut (its request
  stays in ip.drv until data comes); the toolchain's libc.a (sysroot) is not redone, so the
  programs built with it (Apache, MariaDB, Valkey) get this when they are linked again.

### The bench PC's switch-off: what its BIOS does first (voce 102, under way)

- `shutdown` does not switch the .253 off. Its ACPI tables, read with `sonda.drv -acpi` and
  disassembled with iasl, show that the BIOS's `_PTS(5)` sets two bits of the nForce chipset
  (ports 0x4401 and 0x4405, bit 7) that EX-OS does not touch; on the machine the second one
  is off. Whether that is the cause is tried with the user at the machine.
- `sonda.drv` 0.011: `-porta:IND` reads an I/O port, `-porta:IND|=VAL` sets bits in it.

### Every driver has -install and -remove (voce 24) — tested in QEMU

- **Each driver writes its start in the right place** (the user's choice of 17/11): USB
  (ehci, ohci, uhci, xhci), cardbus, sis, ar5k, mouseser `[com1|com2]` write a block in
  `/boot/avvio.sh`, headed `# -install <driver>`, that hwconfig carries over; the network
  cards (e1000, ne2k, pcnet, rtl8169, sis900, nforce) put their line right before
  `ip.drv &` in the network chain, or write the whole chain (card, ip.drv, dhcp) when there
  is none; the sound cards (ac97, es1371, hdaudio, sb) write `audio = /dev/<driver>` in
  `[modules]` of `/boot/kernel.cfg`, as `audio -i` but without the test, only if the probe
  finds the card, and `-remove` takes only their own value.
- **A line hwconfig already wrote counts**: `-install` says "gia' in /boot/avvio.sh (riga
  N)" instead of starting the driver twice, `-remove` comments it out.
- **The parts of the system say who starts them**: pci, ip, kbd, svga; the test tools
  (ramdisk, mappa, fetta, sonda, vgaprova, wifisim) are started by hand.
- The code is in libc (`driver_installa`, `driver_installa_rete`, `driver_installa_modulo`,
  `driver_chi_accende`, `file_sostituisci`): every file is written through `.nuovo` and a
  rename. 28 drivers moved +0.001.
- **Tested:** new `tools/prova_install.sh`, 32 and 64 bit: a disk installed from the CD, the
  missing drivers added with debugfs, an Intel HDA: 40/40.
- **Not yet:** several network cards together — every driver registers as "rete0" and
  ip.drv has one link (voce 101, the user's request: one driver per card, all cards of a
  model served).

### A telnet session takes away what it started (telnetd 0.016, voci 21 and 100) — tested in QEMU and on the bench PC

- **At the end of a session telnetd stops the whole tree, not only the shell**, as SIGHUP
  does on Unix (the user's choice of 2/11). A `dmesg -segui` or a job started with `&` used
  to stay asleep for ever on a pty with nobody on the other side. EX-OS has no process
  groups: telnetd reads pid and parent of every process (`proc_uso`, as `ps`) before it
  stops the shell, stops every descendant, and repeats while it finds new ones. The console
  says "telnetd: chiusi N processi lanciati dalla sessione".
- **Jobs started with `&` from the session go too**: that is the price, and it was chosen.
- **Tested:** `tools/prova_telnet_piu.sh` (32 and 64 bit) now starts `dmesg -segui &` and
  `sh -c 'dmesg -segui' &` in a session, cuts the connection and checks from another
  session that ps has no dmesg left: 8/8. `tools/prova_telnet.sh`: all fine at 32 and 64 bit.
- **Measured, still open:** with those jobs running the session is seen closed after about
  22 s, not the 2-4 s of an idle session: the loop does not count as idle (voce 100).
- **On the bench PC (.253, 64 bit, 17/11):** telnetd 0.016 through netupdate; the same test
  against the machine: 8/8, the session's dmesg gone after 22 s there too.

### ATA: a failed FLUSH CACHE is tried again, BSY and DMA wait 30 s (kernel 0.345, voci 40 and 70) — tested in QEMU and on the bench PC

- **FLUSH CACHE tried once more after a reset, as Linux's libata does.** A flush that ran
  out of time was an EIO at the first go, and a mount that flushes (ext3/ext4 journal) put
  the volume read-only. Now dmesg says "FLUSH CACHE fallito ... ci riprovo una volta", the
  IDE channel is reset and the flush is sent again; AHCI already restarts the port.
- **BSY and DMA wait 30 s** (were 5 and 8), the user's choice of 2/11; the BSY waits of a
  PIO transfer and of a channel reset give the processor away meanwhile.
- **"timeout BSY" says who and how long:** the ms really waited, the calling function
  (addr2line on kernel-intero.elf), the pid and the holder of the disk lock.
- **What it found (voce 70):** Firefox's "Profile Missing" in QEMU was FLUSH CACHE at mount
  waiting for the host to write a fresh 1.1 GB disk image: 60 s measured. The Exilla tests
  now `sync` the image before QEMU; `SENZA_SYNC=1 tools/exilla/prova-riapertura.sh` keeps
  the slow flush on purpose: at 32 and 64 bit the first flush times out, the second one
  succeeds and the volume is mounted read/write.
- **tools/exilla/prova-riapertura.sh rewritten:** it took a Firefox linked before 0.339 and
  tested nothing. Now it packages the current one on a fresh ext4 disk and checks the page
  at both starts, Ctrl+Q (exit code 0) and prefs.js. In QEMU Firefox starts again after
  Ctrl+Q at 32 and 64 bit; the laptop's report of 1/11 is still to be tried on the laptop.
- extfs, ext4, smonta, montaggi and cachedisco regressions pass at 32 bit; extfs and ext4
  at 64 bit too.
- **Bench PC (17/11):** updated with netupdate, kernel sha256 517448e1... checked before the
  reboot; 0.345 starts, root ext4 read/write, no BSY timeout or flush error in dmesg, page
  cache still 1024 MB, `netupdate -verifica` all fine.

### `cachedisco` in kernel.cfg counts (kernel 0.344, voce 27) — tested in QEMU and on the bench PC

- **Up to 0.343 `cachedisco` was never read.** The page cache was made at the first disk
  read, at PASSO 13a, and kernel.cfg is read at 13b: on the bench PC `cachedisco = 1024`
  still gave 383 MB. Now the cache is made after kernel.cfg (`blk_cache_pagine_avvia`, or
  the first disk read if it comes later).
- **Why it matters (measured on the bench PC, 17/11, kernel 0.343):** Exilla starts in
  59.8 s cold, 4.0 s warm; after 587 MB of other files are read, 32.3 s and 211 MB read
  again: 383 MB of cache does not keep Firefox. With 0.344 and `cachedisco = 1024` on the
  same PC: 32.0 s cold, 3.7 s warm, and **1.2 s after the same 587 MB** (23 MB read). The
  first 59.8 s was the first start after an Exilla update (390 flushes); the true cold start
  is about 32 s either way, and that is what is left to do (scattered 4 KB faults, PIO).
- **383 MB is 1/8 of the RAM the kernel counts under 4 GB** (3071 MB on a 4 GB machine), not
  the 4 MB table: that one allows 512 MB at 64 bit, 682 at 32. The table lives in the 64 MB
  kernel band; the default and where the table goes are for the user to decide.
- `bootinst` names the file it maps (`kernel.new` too) in its errors (voce 60).
- `tools/prova_cachedisco.sh`: the CD as it is (63 MB, made after kernel.cfg, the second read
  of a 4 MB file from the cache), `cachedisco = 48`, `cachedisco = 0`, and at 64 bit 4 GB with
  `cachedisco = 1024`: 1024 MB. 5 out of 5 at 32 bit, 6 out of 6 at 64.

### netupdate tries a failed boot map again (netupdate 0.049, voci 60, 49, 47) — tested in QEMU

- **A map that failed is written down** in `/boot/netupdate.avvio`. The next `netupdate
  -check` rewrites it first, even with nothing to update, and while the file is there it
  keeps `rdboot.lz4.old`, the sectors the old map still points to. Up to 0.048 the message
  said "rilancia netupdate", and the next run did not rewrite the map. `netupdate -avvio`
  does the same by hand. `tools/prova_verifiche.sh` phase E: 19 out of 19 at 32 and 64 bit.
- **The system check (`sistema.sh`) also looks for ExPackage**: `/exwin/bin/expackage`, and
  its line in the start menu (a suggestion with the line to add). Tried on the bench PC with
  a menu without the line.
- **The parts a build leaves out are said in one way**: `tools/mancanti.sh` writes "MANCA
  name | command" (OpenSSL, libgit2, FreeBASIC, rustc, the third-party libraries, ncurses,
  Exilla, the CJK fonts), and the publishing script asks, on a terminal, which ones to build,
  builds them and composes the repository again.

### Exilla: the first tab's address bar works from the second start on (voce 73) — tested in QEMU, package to be published

From the second start of Firefox without an address, what was typed in the
first tab's bar never opened; in a new tab it did. The cause is in
`Tabbrowser.sys.mjs`, line 752. While the start page is still a
session-restore promise, Firefox makes the first `<browser>` remote, in the
"privilegedabout" process. EX-OS launches no child processes, so that
browser never loads anything, and on Enter the console says
`aBrowser.frameLoader.remoteTab is null`. Now the remote type is chosen
only when the window has content processes (`gMultiProcessBrowser`);
without them the browser stays in the main process, like those of new tabs.
The patch is `tools/exilla/patch/browser_components_tabbrowser_Tabbrowser.sys.mjs.patch`.

Tested with `tools/exilla/prova-primotab.sh` at 32 and 64 bit: two starts
on the same profile, the address typed in the first tab and in a new tab,
a photo after Enter. 8 of 8 at 32 and at 64 bit; before the fix the second
start gave `remoteTab is null`. The first tab starts blank, like new tabs.

On the bench PC, with the new package installed, the fault stayed: the
profile keeps the JavaScript already compiled (`startupCache`), and Firefox
drops it only when the build number inside the `firefox` program changes,
not when `omni.ja` does. exilla 0.007 remembers size and date of `firefox`
and of the two `omni.ja` in `~/.exilla/installata` and, when they changed,
starts Firefox once with `MOZ_PURGE_CACHES=1`: one slower start after each
update, not at every start. Tested with `tools/exilla/prova-aggiornamento.sh`:
a profile used with the previous `omni.ja`, then updated; without exilla the
first tab stays dead, with exilla it loads, and at the next start the cache
stays. 14 of 14 at 64 bit and at 32 bit.

### telnetd serves several sessions at once (telnetd 0.015, voce 62) — tested in QEMU and on the bench PC

Up to 0.014 telnetd served one session at a time: a second connection was
accepted and stayed mute until the first one exited, and a forgotten
session locked everyone else out. Now each accepted connection goes to a
copy of the program (`telnetd -sessione N`), which serves it as before,
while the parent goes straight back to accepting. The connection is handed
to the copy with `IP_MSG_EREDITA` (ip.drv 0.021). A copy rather than
`fork()`, because fork lives only in the static libc and telnetd uses
libc.so; ExWin does the same. No ceiling: each session costs a process, a
pty and a shell. The status bar shows how many there are (`TELNET cnn 2`).
With an ip.drv older than 0.021, telnetd says so and falls back to one
session at a time.
On the bench PC (17/11): `tools/telnet_piu_prova.py` 6 out of 6, first on
a copy listening on port 2341 and then on the telnetd of the boot, port 23,
under kernel 0.343; `dmesg -segui` held open while two other sessions
answer. Seen there and older than 0.015 (voce 100): when a session closes,
a `dmesg -segui` started from its shell stays alive.

Tested with `tools/prova_telnet_piu.sh` at 32 and 64 bit: three sessions
open together each answer their own command, one closes, a fourth comes in
and the two left still answer (6 of 6). `prova_telnet.sh` and
`prova_telnet_fine.sh` pass at 32 and 64 bit.

### No ceiling on mounted volumes (kernel 0.343, mount 0.005, automount 0.005, voce 50) — tested in QEMU, runs on the bench PC

- **The mount tables grow**: they were 6 mount points in all (VFS), 4 ext volumes, 4 FAT, 2 ISO
  9660 and 5 lines in the `[mount]` section of `kernel.cfg`, all `#define`s against the rule
  "no fixed ceilings". `prova_extfs` met "nessuno slot di montaggio libero" at its fourth ext
  volume. Now each table is an array of pointers to slots that doubles when it is full, as the
  open-file table since 0.282 (`kernel/include/posti.h`): a slot never moves, the old array is
  not freed under whoever is reading it. The ceiling is the kernel heap.
- **The ext4 write module keeps its state for a volume in the volume** (the journal, the
  checksum parameters, the superblock it owes), not in tables as long as the old ceiling: the
  ext2 interface is now ABI 5, and `ext4.mod` must be the one of the same kernel (it already
  was).
- `mount` and `automount` explain the new error (the kernel has no memory for another mount)
  and say that "nessuno slot di montaggio libero" is a kernel up to 0.342.
- **`tools/prova_montaggi.sh`**: a disk with eleven logical partitions made on Linux (3 ext2,
  3 ext4, 5 FAT16), all mounted together from the CD (with cd0, `/rdboot` and `/proc`: 14
  mount points), a file written and read back on each, all unmounted, two mounted again;
  Linux's e2fsck and fsck.fat find every volume clean with the file in it. 18 out of 18 at 32
  and 64 bit. Still fixed, written in voce 50: `BLK_MAX_DEV` 16 block devices,
  `MBR_MAX_PART` 16 partitions a disk, `FAT_MAX_OPEN` 16 open FAT files.
- On the bench PC (17/11): installed with netupdate from the local server, the kernel's
  fingerprint the one published, rebooted; it starts from hd2p1 (ext4) and `netupdate
  -verifica` finds everything in order. Eleven volumes at once only in QEMU: that PC has
  one disk.

### cpufreq reads the two pairs a Core 2 declares, and writes nothing (kernel 0.342, voce 78) — tested on the bench PC

- **On a Core 2 (family 6, models 0Fh, 16h, 17h, 1Dh) with SpeedStep** the kernel reads the two
  ratio/VID pairs the processor declares, as coreboot's speedstep does: the highest from
  IA32_PERF_STATUS (198h, bits 44:32), the lowest from MSR_THERM2_CTL (19Dh, bits 12:0), the bus
  from MSR_FSB_FREQ (CDh). It prints them in dmesg with the MHz, the pair in use now and the raw
  registers (198h, 199h, 19Dh, 17h, CDh), and **writes nothing**: `cpufreq.drv` says "Core 2: le
  coppie massima e minima sono lette". It is the user's choice: only the two declared pairs, and
  using them only after the numbers have been looked at on the real machine (the Q9650 of the
  bench PC).
- **The dmesg line of the states lists the n there are**: "2 stati: 2100 1050 0 0 MHz" made two
  states look like four, two of them broken.
- QEMU does not pass EIST even with `-cpu Penryn,+est` (measured): there the model 17h is seen and
  nothing stops; the Core 2 path itself is seen only on the bench PC. `tools/prova_cpufreq.sh`
  has the Penryn run, and checks the kernel version from `version.h` (it still asked for 0.311).
- **On the bench PC (Q9650, 17/11)**: model 17h, SpeedStep on from the BIOS, bus 333 MHz;
  highest 9 x VID 26h (2997 MHz), lowest 6 x VID 16h (1998 MHz), now 9 x. Whether and how to
  drive the two pairs is the user's decision.

### ExEditor prints, shows the print preview, and has a bar and a ruler for text files too (ExEditor 0.009, exwin.so 0.022, lpt.drv 0.004, voce 81) — tested in QEMU

- **File > Stampa (Ctrl+P)** sends the tab to the parallel port (lpt0) as a text printer prints
  it: 80 columns and 60 lines a page, three blank lines above, the page number at the foot, a
  form feed after each page (66 lines: they fit Letter and A4). Long lines wrap at 80, a tab
  becomes the spaces up to the next multiple of 8, accented letters go out in CP437 (the PC
  printers' set; a letter CP437 has not becomes `?`). Of an RTF only the text is printed: fonts
  on paper need a graphic printer driver, which EX-OS has not.
- **The editor never waits for the printer**: the pages go to `lpt.drv -stampa -` through a pipe
  written a piece at a time on a timer, its words come back through another pipe, and the last
  one ("lpt: N byte alla stampante", or what went wrong) goes in the status line or in a
  message. A printer out of paper keeps lpt.drv 30 s; the editor keeps working. No temporary
  file: a live CD has nowhere to write one.
- **File > Anteprima di stampa** shows the same pages on an A4 sheet, one at a time: Prima/Dopo,
  PgUp/PgDn or the arrows; Stampa from there prints.
- **lpt.drv 0.004: `-stampa -` reads the standard input** (`cat f | lpt.drv -stampa -`).
- **Text files have the bar too**: Nuovo, Apri, Salva, Taglia, Copia, Incolla, Annulla, Cerca;
  and a **ruler in character columns** with the tab stops every 8 and a mark on the caret's
  column. The Formato menu is grey on a text file (it is for RTF), and on an RTF the entries that
  do not work there yet (undo, find, replace) are grey.
- **Tab writes a tab in the text** and the columns line up under the ruler: the area draws a tab
  up to the next multiple of 8, and the cursor, the selection and the click count screen columns.
- **exwin.so 0.022**: `ex_textarea_set_tab(area, n)` (n > 1: tabs drawn as stops every n columns,
  and Tab goes to the area instead of moving the focus; 0 or 1 as before),
  `ex_textarea_get_column` (the caret's screen column), `ex_menu_enable(menu, id, si)` (grey
  entries, drawn but not chosen). A program linked with the stub runs on an older exwin.so too:
  the three just do nothing there.
- In QEMU at 32 and 64 bit: `/boot/help.txt` printed from ExEditor to QEMU's parallel file is,
  byte for byte, what a script on Linux makes with the same rules (30 pages, 81239 bytes), with
  the menu and with Ctrl+P. `tools/prova_lpt.sh` 28 out of 28 (the new check: `-stampa FILE`
  then `cat | -stampa -`, the file twice), `tools/prova_rtf.sh` 6 out of 6. On a real printer:
  to be tested.

### lpt.drv finds the PCI parallel cards (lpt.drv 0.003, voce 98) — tested

- **After 0x378, 0x278 and 0x3BC, every device of class 07.01** that `pci.drv` knows. A PCI card
  has no base+0x400: its ECR is in a BAR of its own. The SUNIX/Lenovo 1fd4:1999 (subsystem
  17aa:0100, like the SUNIX 4008A) keeps the port in BAR1 and the ECR in BAR2, as Linux's
  `parport_serial` says; any other card gets its first two I/O BARs, and the probe decides. A PCI
  line is shared and level-triggered: no IRQ unless `-irq:N` asks for it (`-avvia` names the
  line).
- **The probe takes the port if the data OR the control register reads back**, as Linux's
  `parport_SPP_supported`: on the SUNIX card nSelectIn always reads 0 (0x0C written, 0x04 read).
- **What was written to the control register is kept in memory** (Linux's `priv->ctr`), and
  printing, edges and `-dati`/`-controllo` start from it: reading it back, the SUNIX card lost
  pin 17 at every byte printed.
- **`-dati` and `-controllo` said "0x00, 0x00"**, and `-prova-onda` could not see an interrupted
  wave: the answer was read after the one of LASCIA had overwritten it (since 0.001).
- On the bench PC (64 bit, MCP73): `lpt0: 0xdc00, SPP PS/2 EPP ECP (PCI, ecr a 0xd882)`, `-dati`
  read back by `-pin`, `-id` and `-stampa` with nothing attached give up with their message.
  The wave of 2000 half-waves of 200 us on the real port: worst delay 4.8 us, none over 10 us;
  run again while ExWin was starting, 22.8 ms (188 over 10 us), the reserved processor still
  gets the timer's tick (not measured). `tools/prova_lpt.sh` 14 out of 14 at 32 and 64 bit.

### munmap can no longer stop the machine (kernel 0.341)

- **`munmap` checks the end of the range too**, as `mmap` and `mprotect` already did. Until 0.340
  it checked only the start: `munmap(0xBFFFF000, 0x44001000)` at 32 bit went over the kernel's
  pages, wrapped round to the kernel's memory and freed its physical pages, and the machine
  stopped ("PMM: pagina fuori range"). Any user could do it. Now it is `EINVAL`. Found while
  rereading item 17 of `analisi_kernel.txt`, tried first in QEMU (the machine really stopped);
  `tools/prove/varchiprova.c` has both cases.
- **The 12-hour clock**: with a CMOS in 12-hour format midnight stayed 12 instead of 0. And the two
  CMOS ports (index and data) are used with interrupts off: a process switch in between made one
  register be read in place of another (item 77).
- **A program starts with its registers at zero**: on the first entry into ring 3 the general
  registers held kernel values (its addresses, at 32 and 64 bit). Now they are zeroed; only RDI
  stays at 64 bit, a thread's first argument (item 77).
- **Lost interrupt notices are counted**: when a driver's mailbox is full the notice drops, and
  until 0.340 nobody said so. Now the first drop goes in the log ("IRQn: notifica caduta per PID
  p") and `/proc/interrupts` shows `cadute N` beside the count. It is the measure for item 14 (a
  card that goes silent after a lost notice).
- **Item 76 verified**: a file deleted while open can be read and written until it is closed
  (it has been so since kernel 0.268). The new test writes 1 MB after `unlink` and meanwhile
  makes the name again with another file: right on ext2 and FAT32, 32 and 64 bit
  (`tools/prova_aperti.sh`). Also verified that `rmdir /a/.` does not leave a half-deleted
  directory on FAT.
- **A shell on a pipe no longer restarts the system** (sh 0.013). A `/bin/sh` started without
  arguments with stdin on a pipe (`echo command | sh`, the interruption test of `libctest`)
  believed it was the boot shell and ran `/boot/avvio.sh` and `/boot/autoexec.sh` again: every
  driver a second time. On the test PC the second network driver took the network from the first
  and the machine stopped answering. Now the boot shell puts `EXOS_AVVIO=1` in the environment,
  and whatever descends from it does not redo the start.
- **libctest 0.004 writes its files in the directory it starts from**, not in `/`: from a CD the
  root is read only and a third of the checks failed for nothing (started from `/` it does as
  before). Checks brought up to date: `sbrk` asks 2 GB twice (one fits under the 3 GB ceiling,
  two do not) and gives them back; `rename` across directories moves or says ENOSYS/EXDEV; the
  `mmap` of a file has its bytes with the static libc (which copies it since October) and says ENODEV with the shared one, never zeros. From `/a` in QEMU: 354 checks
  out of 354, at 32 and 64 bit.
- **Tried on hardware** (test PC, 64 bit, ext4 disk): updated from 0.340 with
  `netupdate -check -yes` from the local server, kernel fingerprint equal to the published one,
  `netupdate -verifica` all right. `varchiprova`: munmap gives EINVAL and the machine stays up;
  `fusoprova` right; the certificate made by the package's openssl (relinked to the 0.340 libc)
  has the right UTC time and Linux verifies it.

### Programs get the real universal time: EX-OS knows its time zone (kernel 0.340)

- **`time()` gives UTC seconds**, comparable with any other machine (kernel 0.340). The machine's
  clock stays in local time, as on a PC that also boots Windows; the kernel knows the zone
  (`/boot/fuso.cfg`, read at boot, written by `date -fuso:`, by default UTC+1 with European summer
  time) and the libc converts: `localtime()` gives local time back with summer time, `mktime()`
  the other way, `strftime` writes `%z` and `%Z` (`+0200 CEST`), `tzset`, `timezone`, `tzname`
  and `gettimeofday` tell the zone. Until 0.339 `time()` counted local time as if it were UTC: a
  certificate made with `openssl` in EX-OS was born an hour in the future and Linux refused it
  ("not yet valid"). **tested in QEMU** (tools/prova_fuso.sh, 32 and 64 bit: both summer time
  changes, mktime of the missing and of the repeated hour, new files, /boot/fuso.cfg read at boot)
- **File times are UTC**: on ext2 and ext4 the inode keeps UTC, as Linux writes it; FAT keeps local
  time, as Windows writes it, and `stat` converts it; a CD states its zone and that is used.
  **tested in QEMU**
- **`date -fuso:` applies at once**, without a reboot; from a CD, where `/boot` cannot be written,
  it holds until switch-off. `date -fuso` also shows the zone in use when it differs from the file.
- ! Programs linked to the old libc still count local time as UTC until they are relinked; the
  update brings them relinked.
- The QEMU tests start with the clock in local time (`-rtc base=localtime` in
  `tools/qemu_drive.py`), as on a real machine.
- `tools/prova_librerie.sh` and `tools/prova_openssl.sh` put the shared libraries they use on the
  disk: the EX-OS CD has none, and third-party libraries are shared since 3 November.
- **Tested on real hardware** (the test PC with the nForce MCP73 board, updated with netupdate
  from 0.328): `time()` within a few seconds of another machine's UTC, both summer time changes,
  the new file on ext4 dated in UTC, `date -fuso:+5:30,no` applying at once; a certificate made
  by the relinked `openssl` is accepted by Linux, the old one is not.
- **netupdate 0.048: the checks wait for the reboot** when the update changes the kernel. Under
  the old kernel the new programs ask it for what it lacks (the 0.339 stat): on the test PC, from
  0.328 to 0.340, every `[ -d ]` and `[ -f ]` came out false and four checks asked to create by
  hand directories that were there. Now netupdate says so and asks for `netupdate -verifica`
  after the reboot. **tested in QEMU** (tools/prova_verifiche.sh, test D: a new kernel on the
  server). `verifiche/sistema.sh` also says when `/boot/fuso.cfg` is missing.
- **Local time also where UTC stood in for local time**, searched through all the EX-OS code
  (`gmtime` equalled `localtime` until 0.339):
  - **ExJs**: `getHours`, `setHours`, `new Date(y, m, d)`, `toString`, `toLocaleString` and
    strings without a zone are local time, `getTimezoneOffset` says -60 or -120; the host gives
    the zone with `exjs_fuso_metti` (optional: an old exjs.so stays UTC), EXBrowser 0.033 takes
    it from `localtime_r`. The hour repeated in autumn counts the first time, the one missing in
    spring an hour later. **tested on the host** (`make prova-exjs`, 510 checks, 18 new with the
    Rome 2026 zone); `tools/prova_date.sh` also checks the zone, with both engines.
  - **tar** lists dates in local time, as GNU tar does (the archive keeps UTC).
  - **libctest 0.003**: the `mktime` checks were `timegm`'s (the UTC instant) and the `strftime`
    check left `tm_gmtoff` dirty; now `timegm` against `gmtime` and `mktime` against
    `localtime`, in winter and in summer.

### stat() tells the real permissions, owner, links and times, and files written on FAT have their date (kernel 0.339)

- **`stat()` and `fstat()` report what the volume keeps** (kernel 0.339): on ext2 and ext4 the
  inode's mode, owner, group, link count and three times; on /proc those of the processes. Before,
  the libc made up `st_mode` (0644, 0755 or 0555), `st_uid` and `st_gid` 0 and `st_nlink` 1,
  whatever the disk said: `chmod 600` succeeded and `stat` still said 644. FAT and ISO 9660 have
  no owners: there uid 0, one link and the usual permissions stay. The structure the kernel hands
  the libc changed (56 bytes, new calls 268 and 269); programs' `struct stat` did not. **tested in
  QEMU** (tools/prova_stat.sh, 32 and 64 bit)
- **Files created or written by EX-OS on FAT16 and FAT32 get the clock's date** instead of a fixed
  1 January 2026. **tested in QEMU**
- ! **A program linked to the old libc no longer finds any file with `stat`**: the old calls
  (106 and 108) answer "function not implemented", and the kernel log says
  `SYSCALL: '<name>' (PID n) usa lo stat di prima della 0.339: va ricollegato alla libc nuova`.
  EX-OS's own programs are rebuilt with the system; ported ones (gcc, MariaDB, Apache, Valkey, PHP,
  Firefox...) must be relinked to the new `libc.a`, and come with the update.

### Sleeps that last what they ask, a timer that looks only when needed, processes found by PID at once, CDs read three times faster, a program that exits while its threads read no longer stops the files (kernel 0.334-0.338, analisi_kernel voci 6, 21, 22, 24, 27, 28, 75)

- **A process is found by its PID without scanning the table, and a thread waiting on a mutex is
  woken without scanning it either** (voce 22, kernel 0.334): a PID -> slot table and a hash of
  the waits by (process, address), both as wide as the process table. MariaDB under load (KVM,
  1 processor): from 1854/1743/1651 to 1935/1813/1665 statements/s with 8/32/64 clients.
  **tested in QEMU**
- **The timer scans the process table only when the nearest deadline has come** (voce 21, kernel
  0.335) instead of at every tick. With MariaDB (few processes) it changes nothing: 0.334
  1992/1825/1703, 0.335 1974/1784/1695 statements/s, run one after the other. **tested in QEMU**
- **sleep, usleep and nanosleep last at least the time asked** (voce 27, kernel 0.336): a 10 ms
  sleep asked just before the beat came back almost at once, and `sleep(1)` could last 990 ms.
  Waits shorter than 10 ms still last 10-20 ms (the 100 Hz beat). **tested in QEMU**
- **Files on a CD are read three times faster** (voce 75, the ISO part, kernel 0.337): a path is
  resolved once until the next mount, and whole blocks go straight into the program's buffer, up
  to 128 KB in one command. An 8 MB file in 64 KB pieces: from 4.4 to 13.3 MB/s (QEMU with KVM).
  **tested in QEMU** (also gcc from the tools CD)
- **A program that exits while its threads are inside the kernel no longer leaves the files
  stuck** (voce 6, kernel 0.338): the threads are no longer stopped where they are, holding a lock;
  each one ends by itself on its way out of the kernel, and the main thread waits for them (2
  seconds at most). And the file lock goes to the first in line instead of a race (voce 28): with
  one processor the thread that let it go always took it back, and the others waited for minutes.
  **tested in QEMU** (`tools/prova_filimorti.sh`, 200 times, 1 and 2 processors)
- Tried and taken out: a message to an idle processor when a thread becomes ready (voce 24). A
  woken thread was taken in microseconds instead of 10 ms (`tools/prova_palla.sh`: from 117 to
  47729 rounds a second), but MariaDB with 2 processors went worse (1641 -> 1349 statements/s with
  64 clients) because both processors fight over the single kernel lock. It waits in
  `fuori_uso/voce24-ipi.patch` for voce 30.
- New tests: `tools/prova_sonno.sh` (`tools/prove/sonnoprova.c`), `tools/prova_iso_letture.sh`
  (`tools/prove/isoletture.c`), `tools/prova_filimorti.sh` (`tools/prove/filimorti.c`), `tools/prova_palla.sh`
  (`tools/prove/pallaprova.c`, fails on purpose without the voce 24 patch).

### Less useless disk work: ext2, the ext4 journal, AHCI and FAT (kernel 0.333, ext4.mod 0.006, analisi_kernel voci 56, 63, 73, 74, 75)

- **ext2 no longer walks the path again at every read after a write** (voce 56): the path cache is
  forgotten only when a name changes (create, mkdir, rmdir, unlink, rename, mount); when only an
  inode changes, that inode is read again. **tested in QEMU**
- **Deleting a file on ext3/ext4 no longer commits the journal at once** (voce 74, ext4.mod 0.006):
  the freed blocks are held until the commit, so no new data can land in them first. A delete
  reaches the disk like any other operation: at a file's close, with fsync, or within 5 seconds;
  after a hard power-off the deleted file may come back, whole and with its own bytes. **tested
  in QEMU** (stopped half-way)
- **Waiting for an AHCI command lets the other programs run** (voce 63). **tested in QEMU, to be
  tested on the laptop and the .247**
- **A FAT file read or written in pieces goes on from the last cluster reached** (voce 75) instead
  of walking its chain from the start: big copies from a USB key. **tested in QEMU**
- `kernel/loader/dynlink.c` and `drvmgr.c`, never called, are in `fuori_uso/loader/` (voce 73).
- MariaDB under load (QEMU with KVM, 1 processor, IDE disk, ext2): from 1952/1662/1624 to
  1972/1811/1701 statements/s with 8/32/64 clients, but p95 with 32 and 64 clients went up (from
  40 and 78 to 64 and 131 ms); one run only, `tools/stress/risultati/20261113-*`.
- New tests: `tools/prova_nomi.sh` (`tools/prove/nomiprova.c`), case D of
  `tools/prova_giornale_tempo.sh`.

### A paste into telnet or ssh is no longer cut, floppy files opened twice, a real random generator (kernel 0.332, telnetd 0.014, sshd 0.002, analisi_kernel voci 53, 55, 67, 70)

- **A paste into telnet or ssh is no longer cut at 1 KB** (voce 53, telnetd 0.014, sshd 0.002):
  a full pty input ate the bytes and the write said "all". Now the master takes what fits
  (4 KB, was 1 KB), gives `EAGAIN` when full, `poll` says `POLLOUT` only with room, and the two
  servers keep the rest and send it again. Ctrl+C always goes through. **tested in QEMU**
- **Two handles of one floppy file are one file** (voce 70): the second rewrote its old copy of
  the directory entry and lost the clusters of the first; a file deleted while open is read to
  the end and its clusters freed at the last close; a rename is no longer undone. **tested in QEMU**
- **The random numbers come from a generator** (voce 67): SHA-256 over the pool, RDRAND only
  reseeds it, a new key after every request; without RDRAND (Core 2, the .247) reading the
  pool no longer told the next numbers. **tested in QEMU** (`-cpu core2duo`)
- **The pty reads the program's bytes and termios with interrupts on** (voce 55): a page in the
  swap faulted inside the critical section. **tested in QEMU**
- New tests: `tools/prove/ptypienoprova.c`, `tools/prova_floppy.sh`, `tools/prova_caso.sh`.

### A bad pointer kills the program, not the machine, and twenty more kernel holes closed (kernel 0.331, analisi_kernel)

- **A program that hands the kernel a pointer to nothing dies alone** (voce 5): the page fault
  inside the system call was a kernel panic; now "PF: PID n ... passed a bad pointer" and the
  program ends with -14, the machine goes on. **tested in QEMU**
- **`O_EXCL`** (0x80, voce 47): `open(O_CREAT | O_EXCL)` of a file that exists gives `EEXIST`, on
  ext2/ext4 and FAT; `mkstemp` and `shm_open` use it. **tested in QEMU**
- **The holes of a FAT file are zeros** (voce 71): writing past the end of a file on a FAT
  stick showed what was on the disk before, pieces of deleted files. New clusters are zeroed
  when the write does not cover them; `tools/prova_fatbuchi.sh` (new). **tested in QEMU**
- **Only drivers bind interrupts, only one's own processes go to the foreground** (voci 13,
  52, 43 for the console): `irq_bind`/`irq_fronti` from a program give `EPERM`, `console_setfg`
  and the pty foreground of somebody else's process `EPERM`. **tested in QEMU**
- **The console parser takes no more than 9999 and stays on screen** (voce 64):
  `ESC[2147483600B` wrote outside the text buffer. **tested in QEMU**
- **A failed `exec` leaves the program whole** (voce 10), **waitpid and thread_attendi miss no
  wake-up** (7), **an unmapped page gives back its swap slot** (12), **no user page below
  64 MB and the ELF size check at 32 bit too** (61), **a ring 3 disk driver cannot answer
  more sectors than asked** (50), **a damaged FAT12 or ext4 extent tree is refused instead of
  looping or reading outside** (65, 66). **tested in QEMU**
- **Threads and processes** (voci 8, 9, 18): the TLS image of a new thread has its own lock,
  a thread that fails to start gives its stack pages back, two processes never get the same
  PID and the process count in `sched_dump` no longer climbs. **tested in QEMU**
- **Libraries, ring 3 disks and locks** (voci 69, 68, 51, 54): a library page copy cannot be
  mixed with another program's, a failed hook frees the process's copy and not the library's
  page, a USB disk box is freed when the program using it dies, and the disk, file and pty
  locks give the interrupt flag back as they found it. **tested in QEMU**
- `tools/prove/varchiprova.c` grew the new cases; `tools/prove/tlsfiliprova.c` (new): two
  programs with different `__thread` images make 300 threads each at the same time.

### An ordinary user can no longer read or write the kernel, nor write a file opened for reading (kernel 0.330, analisi_kernel voci 1-4 and 46)

- **System call pointers below 64 MB are refused** (`EFAULT`): that is the kernel, mapped in
  every address space, and `write(1, (void *)0x100000, n)` printed its code while `read()`
  overwrote it. **tested**
- **The access mode of an open counts**: `write`/`pwrite`/`ftruncate` on `O_RDONLY` give
  `EBADF`/`EINVAL`, `read`/`pread` on `O_WRONLY` `EBADF`; `open` asks write permission for
  `O_TRUNC`, `O_APPEND` and `O_RDWR` (read and write), `truncate` asks write permission. Before,
  opening `/boot/utenti` read-only and writing to it worked. **tested**
- **`cld` in the 32-bit entries**: a program interrupted with the direction flag set made every
  `rep movs` of the kernel copy backwards. **tested**
- **kmalloc, kfree and the page allocator with interrupts off**: the kernel is preemptible, and
  two processes could be halfway through the free list, or take the same page. **tested in QEMU**
- **A lock held by a dead thread is taken over** (file, disk, shm, address space), with an
  error in the log: a thread made zombie inside a read kept the file lock, and no file opened
  any more. **tested in QEMU**
- `tools/prove/varchiprova.c` (new): the holes tried as uid 1000, 0 NO at 32 and 64 bit;
  `tools/prove/filimorti.c` (new): 200 children killed while 8 threads read, 200 out of 200.

### The GeForce 210 with its 2D engine, a driver for the NV4x, and `shutdown -r` that reboots (kernel 0.327-0.329, nv50.drv 0.011, nv40.drv 0.005, sh 0.012, voce 99)

- **nv50.drv drives a dedicated GT218 (GeForce 210, N210) on VGA**: `-modi`, `-provamodo LxA`,
  `-modo LxA`, `-install LxA` (the mode and the `accel2d` service at every boot). VESA timings
  packed as G80CrtcDoModeFixup does, the single-stage PLL of the GT21x. Seen on the .247
  (MCP73 + Q9650): 1024x768 clean, ExWin and Firefox "smooth, no jerks". `-2dmisura`: the
  CPU writes 2.8 GB/s, the engine fills 1.1 GB/s and copies only ~100 MB/s whatever the
  shape (being looked at: probably the linear surfaces). **tested**
- **nv40.drv 0.005**: the 2D engine of the NV4x (GeForce 7100 IGP of the MCP73), its stolen
  RAM found with a marker, `-2dservizio`, `-install`. The IGP's display flickers with FIFO
  bands at every mode tried: on that board the N210 is the way. **tested, flicker open**
- **Kernel 0.328**: the page 0xD000 (Stage 2's E820 map, VBE modes, card strings) is kept by
  the PMM: with 4 GB of RAM and more the kernel took it before copying it, and
  `svga.drv -lista` said "0x0 0 bit". **Kernel 0.327**: `mmio_map` maps 0xA0000-0xFFFFF too
  (the video BIOS of an IGP). **tested**
- **Kernel 0.329: leaving ExWin on a driver's framebuffer** (nv50, nv40) no longer goes to VGA
  text mode 3, which those displays ignore — the screen kept the last frame and the machine
  looked frozen. The framebuffer is wiped and the console redrawn in it. **to be tested on the .247**
- **`shutdown -r` reboots** (sh 0.012): the shell's built-in `shutdown` ignored -r and powered
  off. The ACPI power-off follows ACPICA (GPEs off, wake status cleared, PM1 control
  read-modify-write, SLP_TYP then SLP_EN), and prints the PM1a registers if the machine stays
  on (the MCP73 did). **to be tested on the .247**

### A script and its background child no longer wipe each other's lines (kernel 0.326, sh 0.011, voce 94), and `mount` shows /proc as proc (kernel 0.325, mount 0.004, voce 93)

- **The position of an open file is shared**, as POSIX wants, by every descriptor that came
  from the same open: `dup`, `dup2`, spawn and fork. It lives in the VFS handle
  (`VfsPosizione`), which `vfs_dup` already shared; so does its lock, taken over if its holder is
  gone. Before, each had its own: `sh x.sh > log &` with a background program inside lost the
  parent's lines (seen on the laptop on 9/11).
- **The shell writes each line with one `write`** (`echo`, messages, the echoed line of a
  script): with two processors parent and child really write at the same time, and a line
  written in pieces came out mixed with the other's.
- **`mount` shows "proc"** for `/proc` instead of "FAT4294967295": `mountinfo` gives type 10
  (`VOL_FS_PROC`); toolinst 0.006 names it too, and ISO 9660.
- `tools/prova_sottofondo.sh` (new): 40 lines of the script and 40 of its background child in the
  same file, on ext2 and FAT16, at 32 and 64 bit, with one and two processors: 80 out of 80.

### No edge lost with two processors, and the parallel port without system calls (kernel 0.324, lpt.drv 0.002, voce 98)

- **The end of interrupt before the lock.** `irq_prima` (`kernel/arch/x86/isr.c`), called by the
  interrupt entries before the kernel's lock, notes the instant, sends the end of interrupt and
  puts an edge in its ring (under a lock of its own). The end of interrupt used to go after the
  lock: an interrupt waiting there (the timer, a disk) kept its bit in service in the PIC and
  blocked IRQ 7.
- **Processor 0 waits for the lock with the edge lines open**, while some line is in edge mode:
  for that time the PIC has a mask with only those lines, the APIC's priority is raised, and an
  edge that came during the wait is notified as soon as the lock is taken. With no line in edge
  mode the wait is the one of before.
- **The PIC of both kernels in one file**, `kernel/arch/x86/pic.c`, with the mask kept in memory
  and a lock for the chip.
- **The I/O bitmap in the TSS** (`SYS_IOPORT_DIRETTO` 267, `ioport_diretto()` in libc): the windows
  `ioport_bind` gave a driver open to `in`/`out` in ring 3 while it runs, and close at every
  context switch. It gives no new port. lpt.drv 0.002 uses it.
- `tools/prova_lpt.sh`: 13 out of 13 at 32 and 64 bit with one processor. With `-smp 2`, six runs:
  300 edges out of 300 in four, 299 and 298 in the other two (the test sends them 60-100 us apart;
  297 before). The wave: worst delay 58-254 us instead of 1.3 ms, once 1.9 ms. Not tested on
  real hardware yet.

### One driver for the parallel port (kernel 0.323, lpt.drv 0.001, voce 98)

- **`/dev/lpt.drv`**: one driver for everything on the parallel port (printers, the Iomega ZIP,
  scanners, the Commodore datasette through a C64S-style adapter, prototype boards). `-avvia` finds
  the ports (0x378, 0x278, 0x3BC) and what they can do (SPP, PS/2, EPP, ECP) and starts a service
  per port (`lpt0`...); one client at a time holds it. It gives the basic operations: printing with
  the Busy/nStrobe handshake (`-stampa FILE`), the raw registers (`-pin`, `-dati:V`,
  `-controllo:V`), the IEEE 1284 Device ID in nibble mode (`-id`), the edges of nACK with their
  instant (`-fronti:S`), a wave on the data lines with given half-wave times (`-prova-onda:N:US`).
  `-install`/`-remove`. Protocol in `drivers/lpt/lpt_proto.h`. `help lpt`, manual 4.9.
- **Kernel 0.323, IRQ edges with their instant** (`SYS_IRQ_FRONTI` 266, `irq_fronti_*` in libc):
  the stubs note the fine clock (TSC, or PIT on a 486) before waiting for the kernel's lock; a line
  in this mode keeps every edge in a ring the driver reads, notifies once until read, and is not
  masked between edges. Only for the sole owner of an edge line. A "spurious" IRQ 7 or 15 on such a
  line is counted as an edge (the request went down before it was taken).
- The datasette's wires and timings come from the mtap and ptap programs, read only as
  documentation: nothing of their code is in EX-OS. The datasette program is voce 96.
- `tools/prova_lpt.sh`: in QEMU with `-parallel file:`, 12 out of 12 at 32 and at 64 bit with one
  processor: `/boot/help.txt` printed byte for byte, 300 edges out of 300, the wave played.
  ! With `-smp 2`: 300 out of 300 at 32 bit, 297 out of 300 at 64 (294 at 32 before spurious IRQ 7 counted as edges): processor 0
  waits for the kernel's lock with interrupts off, and two edges in that wait become one interrupt
  in the PIC. The wave was at most 0.26-1.3 ms late. Not tested on real hardware yet.

### Exilla writes Chinese, Japanese and Korean (package font-cjk, netupdate 0.047)

- **The fonts.** The new netupdate package `font-cjk` brings Noto Sans CJK 2.004 and Noto Serif CJK
  2.003, regular and bold, into `/exwin/font` (90 MB, SIL OFL 1.1). Each file is a collection with
  Japanese, Korean, Simplified, Traditional and Hong Kong Chinese (and the Sans one also the
  monospace faces): four files cover all of it, and Gecko makes the other weights. Before, Exilla
  drew those pages as boxes.
- **It comes with Exilla**: `netupdate -install:exilla` wants it (`vuole = font-cjk` in the
  catalogue); alone, `netupdate -install:font-cjk`. Like Exilla it has its own list and 16 MB pieces,
  so `-check -yesall` does not bring it to machines without Exilla. Who had Exilla already asks for
  it: the new check `verifiche/exilla.sh`, at the end of `-check`, says so.
- **netupdate 0.047**: a separate list was downloaded only for the package asked for, not for the
  ones it wants; with 0.046 `font-cjk` would have been installed without its files.
- **Which font for each language**: `defaults/pref/exos-cjk.js` in the Exilla package tells Gecko that
  Japanese `serif` is Noto Serif CJK JP, `sans-serif` Noto Sans CJK JP, and so on for Korean and
  Chinese; without it a page asking for serif got the Sans.
- **Where from**: `tools/font-cjk/prepara.sh` downloads the two archives from the notofonts/noto-cjk
  releases, checks the digests (`tools/font-cjk/IMPRONTE`: GitHub gives none) and takes the four
  files; `tools/mknetinst.sh` publishes them.
- **Tested** in QEMU at 32 and 64 bit with two processors: Exilla draws Chinese, Japanese (serif
  too) and Korean (`tools/exilla/prova-gecko.sh`); `tools/prova_font_cjk.sh` 4 out of 4: from a disk
  installed from the CD `-install:exilla` brings the 5 files with the right digests, the register
  has `[font-cjk]`, the check says fine and, with a font removed, says `-install:font-cjk`. **To
  test** on the laptop.

### Two processors no longer stop the disk for good (kernel 0.322)

- **The fault.** With two processors at work (the default since 0.319) a program with several
  threads reading and writing files could stop the whole system: every file waited for the disk,
  and the disk was held by `e1000.drv`, which never touches it. Kernel code can be switched away by
  a tick and come back on the other processor; the kernel read "who is running" as the processor's
  number and then that processor's entry, and between the two it could move, getting the process
  of the processor it had just left. So a thread that had taken the disk lock gave it back in
  someone else's name, the give-back was refused, and the lock stayed taken.
- **The fix.** The running process is read with interrupts off, so it cannot move in between. The
  batch of pages freed by `munmap` had the same weak point (it belonged to the processor) and now
  belongs to the process.
- **Found with a new test**, `tools/prova-letture/prova-letture.sh [32]`: known files read back with
  `read`, `pread` and `mmap` while other threads and processes write, grow, rewrite and delete new
  ones, with plenty of RAM, with little RAM, and on ext4. Before the fix it stopped one time in two or
  three at 64 bit; after it, six runs in a row ended with no error. The guard's probe now also says
  where the disk lock was taken, and the kernel says once if someone gives it back without holding it.
- **The libc's locks no longer change `errno`.** `pthread_mutex_lock`, `pthread_cond_wait` and
  `sem_wait` wrote EAGAIN into `errno` when the lock changed just before going to sleep; with two
  threads really at once (0.321) that happened almost every time, and `tools/prova_moltifili.sh`
  found 377 threads of 382 with their `errno` changed. Now the waits inside the libc leave it alone,
  as glibc does. And a thread taking its `errno` place for the first time uses compare-and-swap, so
  two threads starting together on two processors can no longer take the same one.
- **Tested** in QEMU at 32 and 64 bit with two processors, and **on the laptop** (two cores): the
  same test for 60 s, no error and no hang.

### Sending is no longer held to 4 KB, and TCP has congestion control (ip.drv 0.024, ipcfg 0.006)

- **The send queue grows.** Up to 0.023 a connection had at most 4 KB in flight on the way out:
  towards a server 50 ms away that meant about 80 KB/s, whatever the line. A connection now starts
  with the 4 KB it has inside and takes a 63 KB queue only the first time that one is full, so a
  request or a short answer costs nothing more. At most a sixteenth of the RAM goes to large
  queues; past that a connection keeps working with 4 KB. `ip.drv -W KB` (4 to 63) chooses the size.
- **Congestion control (RFC 5681, 6582, 6928).** Slow start from ten segments, one segment more per
  round trip after the threshold, a retransmission at the third duplicate ACK (fast recovery, with
  NewReno's partial ACKs), and on a timeout one segment and slow start again.
- **Two old faults found on the way.** After a timeout, an ACK that jumped past what was being sent
  again left the connection silent until the next timeout. And the receiver kept at most 8 segments
  out of order, even when they followed one another: after a loss the ninth was thrown away. Now the
  segments that follow one another make one tract (`drivers/ip/tcp_rx.inc`).
- **No more 120-byte segments.** A `write()` reaches the stack 1520 bytes per message, and each
  message left as a full segment plus a 120-byte one. The libc now says how much of the write is
  left, and the stack waits for it before cutting a short segment. A write that ends goes at once.
- `ipcfg` shows `TCP rimandati` (sent again at once / when the timer expired) and `TCP code grandi`.
- **Tested in QEMU** with `tools/prova-invio/prova-invio.sh` (64 and 32 bit): two EX-OS machines
  joined by `rele.py`, a cable with 25 ms each way, 20 Mbit/s and a router queue. 2 MB, every byte
  checked: 853 KB/s on a clean line (57 KB/s with `-W 4`, as up to 0.023), and 464 KB/s with 1% of
  the frames lost (46 KB/s before the two fixes). **Tested on the laptop** (nForce, 100 Mbit, kernel 0.322) against a PC on the LAN, 16 MB three
  times each way: PC to laptop 8.3-8.5 MB/s with no retransmission from the PC (it used to resend
  about 20% of the segments), laptop to PC 11.2-11.5 MB/s, the line full. **To be tested** over the
  Internet.

### The CPU frequency follows the load (kernel 0.311, cpufreq.drv 0.001)

- **The kernel lowers the clock, and with it the voltage, when there is little to do**
  (`kernel/arch/x86/cpufreq.c`). Until 0.310 the only saving was the `hlt` of the idle task, always
  at full clock. At the tick of every working processor the kernel counts the idle ticks of the last
  100 ms: at 80% load or more it goes to the fastest state at once, below 30% it steps down one state.
  Each processor writes its own MSR: no messages between processors.
- **Which processors:** AMD family 10h and later with hardware P-states (the Athlon QL-64 of the
  laptop is family 11h) and Intel Sandy Bridge and later with SpeedStep. Without an ACPI interpreter
  the table comes from the processor itself (AMD MSR C001_0064+, Intel MSR_PLATFORM_INFO). Nothing is
  touched in a virtual machine, with a TSC that is not invariant (the kernel's timeouts are measured
  with the TSC), with SpeedStep off in the BIOS or with Intel HWP on: `cpufreq.drv` says why.
- **`/dev/cpufreq.drv`**: shows the states and where each processor is; `-auto`, `-max`, `-min`,
  `-fisso:N`, `-spento`, `-guarda`; `-install` writes `cpufreq.drv -auto` in `/boot/avvio.sh`. At boot
  the mode is `spento`: nothing changes until it is asked. `help cpufreq`, manual 4.9.
- `tools/prova_cpufreq.sh`: in QEMU at 32 and 64 bit, with and without the hypervisor bit (nothing
  breaks, the reasons are right, -install and -remove), 16 out of 16.
- **Tested on the laptop** (Athlon QL-64, 4 November): the kernel finds 2 states, 2100 and 1050 MHz;
  `-min` goes to 1050, `-max` to 2100; with `-auto` it stays at 1050 when idle, goes to 2100 at 100%
  load for the whole of a 9-second `sha256sum` and comes back to 1050 the second after.
- ! **Only the first processor**: the laptop's second core is started but does not run tasks yet
  (`SMP: ... 0 dei quali lavorano`), so it has no tick and its P-state stays where the BIOS left it.

### C++ as shared objects, and a loader with no fixed number of libraries (voce 85, the way to Qt 5)

- **`libstdc++.so.6` and `libgcc_s.so.1`** (`tools/librerie-exos/cxx-condivise.sh`, new): Qt is a set of
  C++ shared objects, and they need the C++ runtime below them as shared objects too. The
  `libstdc++.a` of the toolchain is not PIC, so it is built again with `-fPIC` next to it (the `.a`
  stays for `-static` programs). The **exception unwinder is one for everybody**: the static libgcc
  builds it hidden, so every `.so` would get a private copy that knows only its own unwind tables,
  and an exception crossing two libraries would end in `terminate()`. Its four files are compiled
  again without the hidden flags into `libgcc_s.so.1`, and the specs
  (`tools/gcc-exos/specs-condivise.sh`) take `-lgcc_s --as-needed` where the sysroot has it. Both go
  in `/lib` of the CDs, like `libm.so.4`. A C program still comes out static.
- **The loader has no fixed limits** (`lib/libc_dl.c`): 32 libraries per program, 16 `DT_NEEDED` per
  library, 32 TLS modules, 256 thread blocks, 24 program headers and 200-byte paths are gone; the
  tables grow (the ones read without the lock are not freed when they grow, so a reader on the old
  copy still reads valid memory). VLC with its Qt interface loads more than 32 libraries. Also fixed:
  the first `__thread` variable exported by a library (offset 0) was "undefined symbol".
- **Tested in QEMU at 64 bit**: `tools/prova-dl/prova-cxx.sh` (new): an exception of the library caught
  in the program, one of the program thrown through a frame of the library, `std::bad_alloc` from
  `libstdc++.so`, `std::thread`, `std::mutex`, a guarded static: 8 of 8. `tools/prova-dl/prova-tante.sh`
  (new): 40 and 100 libraries, 8 threads, every `__thread` right, then `dlopen` of one more: OK.
  ! At 32 bit the toolchain of the build machine had no threads (Thread model: single), and Qt needs
  them: `tools/exilla/gcc-fili.sh` rebuilds it first.

### VLC is in the start menu (vlcapri 0.001, voce 79)

- **VLC installed from ExPackage did not appear in the Program Manager**: netupdate writes a menu
  entry only from the `menu` key of the catalogue, and the `[vlc]` group of `tools/iso/strumenti.txt`
  had none. Now it has `menu = Multimedia/VLC | /exos/bin/vlcapri | /exos/share/vlc/vlc_64.ico`
  (the `menu` key is documented at the top of that file).
- **The entry starts `vlcapri`, not `vlc`**: the Program Manager starts an entry with no arguments,
  and VLC with no file and without its Qt interface would open its line interface on a console that
  is not there, running and invisible. `vlcapri` (`exwin/bin/vlcapri`, part of the vlc package like
  the exilla launcher) shows the "Apri" dialog, warns if the sound is off, and starts
  `vlc -I dummy --play-and-exit <file>`. `tools/librerie-exos/su-cd.sh` puts it in `/exos/bin`,
  and the icons in `/exos/share/vlc`, when VLC is built.
- **The icon** comes from VLC's own `share/icons/256x256/vlc.png`, converted by `tools/png2ico.py`
  (new, pure Python) to the ICO of ExWin at 64 and 128 pixels (`tools/vlc-exos/icone`).
- The 64-bit system no longer carries `/exwin/bin/exilla` and `/exwin/bin/vlcapri`:
  `costruisci-utente64.sh` builds every `exwin/bin/*`, and `tools/mkiso64.sh` now leaves out the
  launchers of the packages.
- **Tested on the laptop**: `netupdate -check -yes` brought `vlcapri` and the icons to the VLC
  already installed and wrote the entry; from the desktop, Avvio > Multimedia > VLC, the cone icon,
  `gizmo.mp4` chosen in the dialog: video in a window and sound, the window closes at the end.
- **The red cross did not close VLC**: in VLC 3 the close event of a video output is a no-op
  (the FIXME in `src/video_output/event.h`). `tools/vlc-exos/src/vout_exos.c` now calls `libvlc_Quit()`
  on `WIN_EV_CHIUDI`, as closing the main window does in the VLC of other systems. Tested on the
  laptop: the cross closes it. `tools/vlc-exos/costruisci.sh` has `VLC_SOLO_COLLEGA=1`, to relink
  only (about two minutes) after changing a module of ours.
- **The window has no menu and no controls**: they are VLC's Qt interface, and Qt is not on EX-OS
  yet. The port of Qt 5 (libstdc++ shared, FreeType, HarfBuzz, libpng, Qt 5.15 with an ExWin QPA
  plugin, then VLC with `--enable-qt`) is voce 85 of the queue. (18 November: done, `vlc -I qt`.)
  ! `toolinst` (installing from the tools CD) does not read the `menu` key yet.

### The screen switches off when nobody uses it (kernel 0.312, dpms.drv 0.002, voce 78 b)

- **The kernel knows when the machine was last used** (`SYS_ATTIVITA` 262): `kbd.drv` 0.010 at every
  key and PS/2 mouse or touchpad packet, the USB mice (`usb_comune.c`: `ohci.drv` 0.009, `uhci.drv`
  0.005, `xhci.drv` 0.004) and `mouseser.drv` 0.002 call `attivita_tocca()`. One store in the
  kernel, no message per event. A USB mouse report that says nothing new does not count. On a kernel
  older than 0.312 the call is not made at all (libc asks the version once), so the new drivers write
  nothing in the log of an old kernel.
- **`/dev/dpms.drv`** (`-dopo:M`, `-spegni`, `-accendi`, `-ferma`, `-install [M]`, `-remove`): a
  small service that switches the screen off after M minutes without input and back on at the first
  key, click or movement. On NVIDIA G80 and later it powers down the SOR that drives the panel, as
  xf86-video-nv does; on every other card it blanks the picture with the VGA registers (SR01 and the
  PAS bit of the attribute controller: QEMU in a VBE mode honours only the second). `help dpms`,
  manual 4.9.
- `tools/prova_dpms.sh`: in QEMU at 32 and 64 bit, with screendumps (not keys) to look: off on
  command, back with a key, off by itself after a minute, back with the mouse; 26 out of 26.
- **Tested on the laptop** (GeForce 8200M, 4 November): `-spegni` from telnet takes the panel down,
  backlight included; a key brings it back at once with the same picture; with `-dopo:1` it went off
  by itself and the touchpad brought it back. Left installed there at 10 minutes.
- **netupdate 0.043 tested** in the same update (voce 75): the boot sector read from outside after
  `-check -yes` held the 0.312 map, equal byte by byte to `/boot/kernel.bin`; after `reboot`, 0.312
  with the disk read-write.

### The laptop's sound starts by itself, and ls lists every name (audio 0.005, ls 0.003)

- **`audio -i` did not try the laptop's card.** The NVIDIA MCP78 HD Audio (`10de:0774`, Conexant
  codec `14f1:5051`) was not in the table of `lib/audio.c`, so `audio -i` said "scheda audio non
  riconosciuta" and tried only the Sound Blaster, while `hdaudio.drv` started by hand played. No new
  driver was needed: HD Audio and AC'97 are standard interfaces. The MCP78 is now in the table, and a
  card that is not there is recognised by its PCI class (`audio_per_classe`: 04/03 `hdaudio.drv`,
  04/01 `ac97.drv`). **Tested on the laptop:** `audio -i` installs `hdaudio.drv` in
  `/boot/kernel.cfg`, after `reboot` the sound is on with no command and `audio -c` passes. In QEMU at
  32 bit with an ICH9 HD Audio: the same. The class fallback alone is **to be tested** on a card that
  is not in the table.
- **`ls` with several names showed only the first** and dropped the others without a word
  (`ls -l /exos/lib/libavcodec*` showed one file out of three). From 0.003 every name is listed in
  order; a directory among several names gets its name above it, the header is printed once, a name
  that does not exist is an error but the others are still listed. **Tested** on the laptop (64 bit)
  and in QEMU (32 bit).
- **Firefox and VLC on the laptop** (4 November): Firefox plays Vorbis, FLAC, MP3, AAC, VP8, VP9, AV1
  and H.264 (`gizmo.mp4` to the end, 166 frames): the H.264 stall of voce 77 is QEMU's only. VLC
  plays `gizmo.mp4` with sound through the EX-OS output. ! The **first** file Firefox plays after it
  starts stays at 0 seconds, whatever it is; the following ones play. Not found yet.

### The boot sector no longer goes back to the old kernel after an update (netupdate 0.043, voce 75)

- **Cause found** on the laptop, which twice after `netupdate` (2 and 4 November) started the kernel of
  before with the disk read-only. With the partition unmounted (`system -unload`, from `/rdboot`)
  `tools/prove/mappa.c` read the boot sector: it held the map of 2 November, while netupdate had
  printed the new one. On ext4 with 4 KB blocks block 0 holds both the boot sector and the
  superblock; the `ext4.mod` loaded in memory was the one from before the update (the new one was
  only in `/dev`), which up to 2 November wrote block 0 whole at the journal checkpoint, with the
  boot sector as it was before bootinstall. Since kernel 0.307 that checkpoint comes up to 5 seconds
  later. The module has been fixed since 0.310 (`al_suo_posto`, `tools/prova_mappa_giornale.sh`).
- **netupdate 0.043 calls `sync()` right before rewriting the boot sector**, and once more after: the
  journal is emptied first, so a module of before can no longer put the old map back.
- Repaired on the laptop with the recipe of `help problemi` (`modulo carica /dev/ext4.mod.old`,
  `netupdate -avvio`): the boot sector read again from outside held the 0.311 map, the sectors equal
  byte by byte to `/boot/kernel.bin`; after `reboot`, 0.311 with the disk read-write. **Tested** the
  repair; netupdate 0.043 **tested** on the laptop with kernel 0.312 (4 November).

### netupdate brings the new files of installed packages, and VLC finds its libraries (netupdate 0.042)

- **Fault found on the laptop:** up to netupdate 0.041, a file that was not on the disk counted as
  "never installed" unless it was essential, even when its package was installed. `-check -yes` left
  out the shared libraries that came with the packages already there (`/lib/libm.so.4`,
  `libz.so.1`, `libssl.so.4`, `liblzma.so.5` and more): `xmlwf` was updated and then said
  "libm.so.4: cannot open shared object file". Now a missing file of an installed package is part
  of the update, and the versioned libraries (`.so.N`) arrive before the programs. The `.so` link
  scripts of packages the machine does not have no longer arrive by themselves. **Tested** on the
  laptop: 24 files arrived, `xmlwf` starts.
- **Fault found on the laptop:** the register loaded for the comparison had fixed limits (2048
  files, 192 KB). With 3298 files, 0.041 compared only half of them ("il registro non ci sta in
  memoria: confronto parziale"): 1418 files unknown and 1655 to update instead of 255. Now the
  register is measured and allocated as large as it is. **Tested** on the laptop.
- **Catalogue:** `vlc` also needs `flac`, `ffmpeg` needs `zlib`, `flac` needs `ogg`. Without them
  `netupdate -install:vlc` installed VLC at 64 bit and it said "libFLAC.so.14: cannot open shared
  object file". All the dependencies of the 64-bit repository were checked against what each
  program and library loads (`readelf -d`).
- If a program says "cannot open shared object file" after an update: `help problemi`.

### ExPackage, and netupdate reads the whole catalogue (netupdate 0.041, expackage 0.001)

- **ExPackage** (`/exwin/bin/expackage`, menu Strumenti > ExPackage): the applications of the
  repository in a window. `[ ]` not installed, `[x]` installed, `[+]` to install, `[-]` to remove,
  `[d]` needed by a `[+]`; Enter or a double click changes the mark, and an application another one
  uses cannot be marked for removal. **Applica** runs netupdate in the background (`-remove:<app>
  -yes`, then `-install:<app> -yes`), the last line it writes is on the status line and
  Visualizza > Registro di netupdate shows all of it. **Repository > Imposta e verifica**: mode
  (HTTP, FTP, HTTPS, FTPS) and address, written with `netupdate -set:` and checked to hold a list
  of applications; if not, the old repository goes back. Changing anything needs root.
- **`netupdate -install:list -p`** one page at a time (Enter, q); `-install:list -tab` one line
  per package for programs; `-yes` after `-install:<app>` or `-remove:<app>`; `-set:<TIPO> <address>`.
- **Fault found:** netupdate read only the first 32 packages of the catalogue and 4 `vuole` each.
  With the shared libraries the catalogue has 34: apache and exilla could not be installed
  ("piu' di 32 pacchetti: ignoro il resto") and apache lost its fifth dependency. Now the arrays
  are as long as the file.
- `tools/prova_expackage.sh`: 12 out of 12 at 32 and 64 bit in QEMU. On the laptop only the new
  netupdate from /tmp (`-tab`, `-p`); expackage not tried there.

### Third-party libraries are shared objects, and programs load them at start

The rule was "every library dynamic" (30 July) and the third-party libraries had all come
out as static archives. Now:

- **Programs linked to .so files start.** `gcc prog.c -lz` with the cross compiler makes an
  ordinary dynamic ELF; the kernel ignores PT_INTERP, and before `main` the libc inside the
  program loads its DT_NEEDED with the dlopen loader (`lib/libc_dl.c`,
  `__exos_dl_avvio_programma`): COPY relocations, library constructors held until the
  program is bound, the same search path as dlopen. A missing library stops the program with
  its name and exit code 127. Only in the libc of ported software: the system's own commands
  stay static. `tools/prova-dl/prova-avvio.sh` **tested at 32 and 64 bit** (constructors, a
  copied variable, malloc from a library, `__thread` in two threads, a missing library);
  `tools/prova-dl/prova-dl.sh` still passes.
- **The compiler** (`tools/gcc-exos/specs-condivise.sh`): static only with `-static`.
- **Every C library is shared** at 32 and 64 bit, with its upstream soname: zlib, bzip2, xz,
  lz4, zstd, pcre2, expat, OpenSSL (`libcrypto.so.4`), APR, APR-util, Connector/C, Ogg,
  Vorbis, Opus, FLAC, libvpx, dav1d, FFmpeg (`libavcodec.so.63`...). `comune.sh` compiles
  everything `-fPIC`, makes the .so, and refuses one that exports nothing or uses a symbol no
  dependency gives. No symbolic links (EX-OS has none): `libz.so` is an `INPUT(libz.so.1)`
  script, as glibc does. fmt and Connector/C++ stay static (C++, no shared libstdc++).
- **`/lib/libm.so.4`** (openlibm) in the base system at 32 and 64 bit.
- **VLC** is now linked to them: 15 MB instead of 47 at 64 bit.
- **Firefox** opens the system's FFmpeg with dlopen: **AAC plays**; H.264 decodes but in QEMU
  playback stops under a second (Firefox goes to BUFFERING), not yet tried on hardware.
- Not yet: the native gcc on EX-OS still links static; Firefox 32 bit not rebuilt.

### Audio and video: the codecs, VLC, and three faults that kept every player silent (ac97.drv 0.003, hdaudio.drv 0.006, sb.drv 0.002, es1371.drv 0.002)

**Tested** in QEMU at 32 and 64 bit (`tools/vlc-exos/prova.sh 32` and `64`): VLC plays Vorbis,
FLAC and MP3 on the AC'97 card, and H.264, VP9 and AV1 in an ExWin window, then quits by
itself. On the laptop (HD Audio, codec 14f1:5051) VLC plays the sound of an H.264/AAC file
through `hdaudio.drv` 0.006 and quits; **whether it sounds right has still to be heard**.

- **The codecs as real libraries**, 32 and 64 bit, latest public versions, sources on the
  tools CD: libogg 1.3.6, libvorbis 1.3.7, Opus 1.6.1, FLAC 1.5.0, libvpx 1.17.0, dav1d 1.5.4
  and FFmpeg 9.0.2 (`ffmpeg`, `ffprobe` and the libav libraries, LGPL build), each with its
  `tools/<name>-exos/costruisci.sh`. `tools/prova_codec.sh` decodes Firefox's test files inside
  EX-OS and compares with FFmpeg on Linux: the video is identical frame by frame.
- **VLC 3.0.24** (`tools/vlc-exos/costruisci.sh`, group VLC of the tools CD). EX-OS has no
  dlopen, so its 234 plugins are linked into one program. Two outputs are new and ours: sound
  on the `audio` service (`aout_exos`) and pictures in an ExWin window (`vout_exos`). See
  `help vlc`.
- **Audio drivers.** They recognised the player by the thread that had opened the sound: a
  player that opens from one thread and starts from another (VLC, Firefox) had its start
  thrown away without a word, so nothing played and the player waited forever. The program
  now counts, not the thread.
- **AC'97.** The descriptor list had 2 entries while the controller counts 32: the third half
  of every playback was lost, 85 ms, and the clock of whoever was playing jumped. Found with a
  file in which every sample carries its own number.
- **Firefox** (`tools/exilla/prova-media.sh`, QEMU, 64 bit): Ogg Vorbis, FLAC, MP3, VP8, VP9
  and AV1 play to the end, with sound on the `audio` service and frames decoded. Before,
  the first video crashed Firefox: the HAL (wake locks) was never started, now the AppShell
  starts it. cubeb now adapts to the card: mono/stereo and the rate are converted when the
  driver grants something else than what was asked. **Not yet**: H.264 and AAC (`gizmo.mp4`,
  `.m4a` say "Failed to open media": on Linux Firefox takes them from the system FFmpeg with
  dlopen, which EX-OS has not); Firefox at 32 bit not rebuilt; not tried on the laptop.
- **libc.** `pthread_exit` now runs the `pthread_cleanup_push` handlers. Before, a thread ended
  that way kept its mutex locked, and VLC never quit at the end of a file. `sync()` is no longer
  declared to third-party code outside `<unistd.h>`: libaom and libvpx have their own.

### Dead drivers no longer write into other programs (kernel 0.309 and 0.310, chkdsk 0.004)

**Tested** on the laptop with kernel 0.310: after `system -unload`, 0 stray bytes in 512 MB
(`memsporca`) and four `chkdsk` in a row with no problem found. In QEMU at 32 and 64 bit
(`tools/prova_dma_morto.sh`, `tools/prova_zona_rossa.sh`); both fail on the code before.

- **0.310.** After `system -unload` the USB controller kept writing its frame counter, 1000
  times a second, into pages freed by the dead `ohci.drv`, and those pages went to the next
  program. That was why `chkdsk` disagreed with itself on a sane disk. DMA pages of a finished
  driver now stay out of use until the next boot; `dmesg` says how many.
- **0.309, 64 bit.** The kernel grew the stack without counting the 128 bytes of red zone, so
  a program could crash on valid code. It was a Firefox crash in NSS.
- **`chkdsk -dettagli`** names the differing blocks and inodes.

If the old kernel still runs after netupdate and the disk is read-only, see `help problemi`.

### A disk error is an error (kernel 0.308, date 0.004, netupdate 0.040)

**Tested** in QEMU at 32 and 64 bit with failing sectors (`tools/prova_lettura_errata.sh`);
the same test fails on the code before. **To be tested** on the laptop.

An unreadable block made `ext2_read` return zero bytes, the end of the file; with the
fault in an indirect block the data came full of zeros and no error; a page of a program
filled with zeros in place of code. Now the read fails and the program stops with a
`PF:` line in `dmesg`.

`date -ntp` gives up after 5 seconds, server name included. New `/bin/sync`. `netupdate`
stops and says so if the server has an older system than the one running; to go back
on purpose add `-indietro`. ! Before netupdate 0.045 (6/11/2026) this check never fired: it read the
kernel version into 64 bytes, and `osversion()` gives a block of about 130 and returns its
length, not 0. Now it reads only the `Versione:` number.

The programs of the packages with a tree of their own (`/exos/apache2/bin`, `/exos/mariadb/bin`,
`/exos/valkey/bin`, `/exos/php/bin`) arrived from netupdate 0644 up to 0.044: root runs them
anyway, a user cannot. Apache's check found it on the laptop. From netupdate 0.045 every
`/exos/<package>/bin` and `sbin` is a program directory; on a machine already installed:
`netupdate -check -yes`, then `netupdate -permessi` (see `help problemi`).

The program manager collects the programs it starts (pm 0.027): up to 0.026 every program
launched from the menu or the desktop stayed a zombie under `pm` once closed, for the whole
session (seen on the laptop: `exilla` and `exvolume`). Now `pm` collects them every 2 seconds.

At 64 bit a JPG or PNG wallpaper was not read, and neither were the images in Immagini,
Pennello and EXBrowser or the TrueType fonts: only BMP and the 8x16 system font were left.
The toolkit opens `/exwin/lib/eximg.so` and `/exwin/lib/exfont.so` when it needs them, but at
64 bit those two files were never built, and the loader (`lib/exlib/exlib.c`) still made the
32-bit call. Now they are ordinary ELF shared objects, built like `exjs.so`, and the loader
opens them with `dlopen`. Tried in QEMU at 64 bit: a JPG wallpaper from `pm.cfg`, JPG and PNG
in Immagini, the TrueType fonts in Prova font. On a machine already installed:
`netupdate -check -yes` brings the two libraries and the programs (see `help problemi`).

What Mesa asks of the libc (voce 88, the way to OpenGL and WebGL), in `lib/libc_gnu.c`, static
libc only, as the software ported to EX-OS links it: `open_memstream`, `getline`,
`getdelim`; `pthread_barrier_*`; the locale objects `newlocale`, `uselocale`, `duplocale`,
`freelocale` with `strtod_l`, `strtof_l`, `strtold_l`; `stpcpy`, `stpncpy`, `qsort_r` (the
GNU order), `reallocarray`, `secure_getenv`; `sched_getaffinity`, `sched_setaffinity`,
`sched_getcpu` and the `cpu_set_t` macros, `pthread_getaffinity_np`,
`pthread_setaffinity_np`, `pthread_getcpuclockid`; `getpriority`, `setpriority`; `fegetexcept`;
`program_invocation_short_name`. They say what EX-OS is: one locale ("C"; any other name
fails with ENOENT), programs on processor 0 (a set without it fails with EINVAL), one priority
(0; another fails with EACCES). On the way: the libc kept at most 16 open streams in its list,
and `exit()` did not flush the 17th and later ones: a program that wrote a file without
`fclose` lost its end. Now the list has no ceiling.

`struct stat` has `st_rdev` (POSIX; Mesa hashes descriptors with it): for a character device
(`/dev/null`, `/dev/ptmx`, `/dev/pts/N`) it is the device's identity, the same as `st_ino`; 0
for every other file. The field is at the end, and the struct grew: code compiled before
(libstdc++, openssl, ffmpeg and the other libraries already in the sysroots) passes the old,
shorter struct. So, as glibc does, `stat`, `lstat`, `fstat` and their `*64` have two symbols:
the headers send new code to `__exos_stat2` and the others, which fill the whole struct; the
old names stay and fill only up to `st_ctime`. Nothing has to be rebuilt, and a library rebuilt
later moves to the new names by itself. `tools/abi-bersaglio.c` has the new field.
`tools/prova-gnu/prova-gnu.sh` (18 checks, the last two on `st_rdev` and the old symbol)
passes at 32 and 64 bit in QEMU.

Mesa 26.2.6 for EX-OS (voce 88): `tools/mesa-exos/costruisci.sh [32|64]` builds `libEGL.so.1`
and `libGLESv2.so.2` with the softpipe driver, from the signed archive plus
`tools/mesa-exos/exos-26.2.6.patch`, the "exos" platform: system `exos` in meson, an EGL
driver with surfaces in memory (pbuffer and surfaceless) and in ExWin windows, a winsys
for them, Haiku's hgl frontend made portable. `tools/mesa-exos/prova-egl.sh [32|64]` links a
program to the two libraries, puts it in EX-OS under QEMU with the shared objects its
DT_NEEDED asks for, and draws a triangle with GLSL ES shaders: 8/8 at 32 and 64 bit,
"softpipe | OpenGL ES 3.1 Mesa 26.2.6".

On the tools CD and in netupdate as the group `mesa` (`netupdate -install:mesa`, it brings
zlib too): `/exos/lib/libEGL.so.1`, `/exos/lib/libGLESv2.so.2`, the headers (`EGL`, `GLES2`,
`GLES3`, `KHR`), the sources, and `/exos/bin/provaegl`, which draws the triangle and says which
of its eight checks fails. The netupdate check `tools/netinst/verifiche/mesa.sh` looks for the
libraries Mesa needs (libz in /exos/lib; libstdc++, libgcc_s and libm in /lib) and then starts
`provaegl`: "a posto" means that OpenGL works on that machine, not only that the files arrived.
A program: `gcc prog.c -lEGL -lGLESv2`. `tools/mesa-exos/prova-netupdate.sh` does it as a user
would, in QEMU, on a disk installed from the CD (see `help problemi`, MESA).

OpenGL ES in an ExWin window: `EGL_DEFAULT_DISPLAY` is the platform `exos` (Mesa's
`_EGL_PLATFORM_EXOS`, in the patch), and `eglCreateWindowSurface` takes a
`struct exos_egl_finestra` (`#include <exos/egl_exwin.h>`, from `drivers/wserver/egl_exwin.h`):
a version and two functions given by the window's owner. `misura` says how big the drawing area
is; `mostra` gets each finished frame at `eglSwapBuffers` (0xAARRGGBB, top row first), and the
owner copies it into the window's zone and tells the server. The owner, not libEGL, talks to
the server because the server's news (a resize, the events) reach the process's mailbox, which
the program's own loop reads; a library reading it would steal them. The shape of Wayland's
`wl_egl_window`. A window resized between two frames gives the next frame its new size.
`tools/mesa-exos/prova-finestra.sh [32]` opens a window with the protocol (as Firefox's widget
does), draws 60 frames, changes the size, and photographs the screen with ExWin on: 8/8 and
the triangle in the window.

Two faults of the cross toolchain found on the way, both in `tools/gcc-exos/specs-condivise.sh`
(run it again on a toolchain built before 7 November 2026): ld looked for the DT_NEEDED of a
shared object in its own SEARCH_DIR, which is the directory where the toolchain was built (on
another machine): "libz.so.1 ... not found" and every symbol of it undefined. And at 32 bit,
with no sysroot, ld took the host's `/lib32/libstdc++.so.6` (glibc's) through the host's
`/etc/ld.so.conf`: "undefined reference to `_Unwind_Resume@GCC_3.0`". The specs now give ld
`-rpath-link` and `--sysroot` on the toolchain's own target directory. And in
`tools/librerie-exos/comune.sh` the 32-bit `__stack_chk_fail_local` for shared objects is weak:
meson passes it to every link, executables too, where libc.a has it already.

### Fewer FLUSH: the journal does not commit at every operation (kernel 0.307)

ext3 and ext4 commit at `fsync`, `sync` and unmount, when the journal is half
full, after 5 seconds, and at once only if the operation freed blocks. Each
operation used to cost three FLUSH: Firefox at its first start on the laptop goes
from 6605 to 1866 FLUSH. A hard stop loses up to 5 seconds of operations without
`fsync`, as on Linux; the volume stays consistent. Test:
`tools/prova_giornale_tempo.sh`. `/proc/interrupts` says how many times each line
reached its driver.

### Disk read cache (kernel 0.304)

The disks' read cache keeps pages of 4 KB of file data and metadata in RAM, up
to `cachedisco` MB in `kernel.cfg` (by default 1/8 of the RAM, 0 turns it off).
It always writes to the disk too, so a hard stop loses nothing more than before.
Under 1/8 of free RAM it stops growing, and whoever runs out of pages takes them
back before the swap is touched. Test: `tools/prova_cache.sh`.

### Disk counters (kernel 0.303, disk 0.003, /proc/disco)

`disk -stat` says what each disk has done since the start: commands, KB, times, FLUSH, what came from memory and how long the disk lock was waited for. `disk -stat 5` repeats it every 5 seconds, as differences, with the busy percentage. They exist to measure the cache before it is written.

### reboot, halt and poweroff unmount the volumes (kernel 0.302) — tested in QEMU at 32 and 64 bit

Up to 0.301 the three commands synced and set the "clean" mark, but did not unmount: on ext3 and ext4 the journal stayed open and the next start said `not unmounted cleanly and its journal is not replayed`. Now every volume is really unmounted, `/` last, and the screen says `Smonto / (hd0p1)... ok`. A volume that cannot be unmounted is closed anyway: journal shut, no writes afterwards, and `ancora in uso` on the screen. After the 30 seconds of SIGTERM, what has not ended gets SIGKILL. `sleep` can be interrupted: `kill -9` on a sleeping program used to have no effect until it woke. Test: `tools/prova_smonta.sh`.

### A stale module no longer locks the disk; netupdate retries (kernel 0.293, netupdate 0.038, install 0.011, hwconfig 0.009, nv50.drv 0.009) — tested in QEMU, to be tested on real hardware

A laptop installed from an older CD and updated with netupdate came back with **the whole disk read-only**: `mount` said so, even for root. It was not the permissions. Writing ext2/3/4 lives in `/dev/ext4.mod`, the kernel loads it only if it was built for that same kernel, and netupdate had replaced everything but that file: the old and the new one were both 40920 bytes, and the register was written trusting the size. Fixed:

- **64-bit ISO on a CPU without long mode: it now says so on screen.** The screen used to stay black with the message on the serial port only: it happens in VirtualBox with a 32-bit VM type. Fix: type Other/Unknown (64-bit) with VT-x/AMD-V enabled.
- **`install` no longer copies what it finds, it follows a catalogue (install 0.012).** The system directories, with their attributes, are a list inside `install`: /rdboot, /proc, /cdrom and any other unit are never copied, from any medium. Nothing from the medium reaches the disk but file contents: at the end `install` sets owner and modes of the whole system, and /tmp is created 1777. `/dev/ext4.mod` is written last and taken from /rdboot, so it always matches the kernel being installed: a mismatch is what sends the disk read-only. A new system directory must be added to the catalogue.
- **The kernel** takes `/rdboot/dev/ext4.mod` when the disk's module is refused, not only when it is missing.
- **netupdate** writes a hash in the register only after reading the file, and always checks the essential files on the disk; a register that lies repairs itself. The first `-check` without a register rereads every file and can take minutes.
- **netupdate** keeps the files that did not arrive in a queue, lists them at the end and asks whether to retry (with `-yes`, one round by itself).
- **install** no longer copies `/rdboot` and `/proc`: they exist only in memory.
- **`nv50.drv -i`** answers `hwconfig -d`, which never copied it before, and **hwconfig** writes its block in `/boot/avvio.sh`.

**If your disk is read-only**, as root:

```
dmesg | grep MODULO                      # the reason
modulo carica /rdboot/dev/ext4.mod       # writable again, at once
cp /rdboot/dev/ext4.mod /dev/ext4.mod    # puts the disk's copy right
netupdate -check -yes                    # brings the rest in line
```

From 0.293 only the last line is needed. If netupdate says there is nothing to do while a file is old: `delete /boot/netupdate.reg`, then `netupdate -check -yes`. All of this is in `help problemi`, on the machine itself.

### ext3 and ext4 in the tools too (kernel 0.292, mkfs 0.002, chkdsk 0.003) — tested in QEMU, to be tested on real hardware

The kernel read and wrote ext3 and ext4 already; the tools did not know them. Now:

- `mkfs -t ext3` and `mkfs -t ext4`. Blocks of 4 KB from 512 MB up, inodes of 256 bytes, a
  jbd2 journal; ext4 has extents and metadata_csum. `-L ext4` without `-t` is a label and
  not a filesystem: `mkfs` now says so before asking for confirmation.
- `chkdsk` checks and repairs all three: it replays the journal with `-r`, walks extents,
  reads descriptors of any size, flex_bg, groups left uninitialized and quota and orphan
  files, verifies and rewrites the checksums. Read-only with the reason printed: inline
  data, meta_bg, xattrs in inodes, an external journal, a non-empty orphan file. bigalloc
  is refused. A bad block inside an extent is reported, not moved.
- Three old `chkdsk` bugs, also on ext2: a directory stopped at its twelfth block, so with
  `-r` the blocks of the files beyond it could be declared free; short symbolic links and
  devices were read as block pointers; extended attribute blocks were not counted.
- `disk`, `mount`, `hwconfig`, `toolinst` and `/proc/mounts` name ext2, ext3 and ext4;
  `toolinst` accepts them, `fdisk` suggests them, `cdinstall` asks which one to use (ext2
  stays the default).
- An ext4 system disk is now marked "in use" like ext2 and ext3, with the superblock's
  checksum rewritten: before, after a crash it was never checked.

`tools/prova_extfs.sh` (make, write, check, damage and repair, Linux's `e2fsck` as judge) and
`tools/prova_sistema.sh` with `EXOS_FS=ext3` and `ext4` pass at 32 and 64 bit.

### Real file locks (kernel 0.291) — tested in QEMU, Firefox still to be relinked

Until now `fcntl` locks and `flock` excluded nobody: `F_GETLK` always said "free" and
`F_SETLK` always said "taken". Now `F_GETLK`, `F_SETLK` and `F_SETLKW` are real: per
process and per byte range, advisory as in POSIX, and they go away when the process closes
any descriptor of the file or ends. `flock` sits on them, for the whole file and per
process. There is no `EDEADLK`: two programs waiting for each other wait until a signal.
Statically linked programs get the locks only when relinked with the new `libc.a`.
`tools/prova_lucchetti.sh` passes at 32 and 64 bit, also with `_FILE_OFFSET_BITS=64`.
Before this change, the same test showed a second process taking a lock already held.

### The clock comes from the network at every boot (date 0.003) — tested in QEMU

A machine with a dead CMOS battery restarts from the date of its BIOS: on the laptop
2007-09-01, so files were dated 2007 and Firefox showed "Warning: Security Risk" until
someone set the clock. `/boot/avvio.sh` now runs `date -auto &` after `dhcp`: the same
as `date -ntp`, in silence and in the background, so that no network does not hold up
the boot. hwconfig writes the line on new installations; on a system already installed
`date -install` adds it and `date -remove` removes it. The zone is `/boot/fuso.cfg`
(`date -fuso:`, default +1 with EU summer time).

### Deleting a folder no longer stops at the first error (filemgr 0.015) — tested in QEMU

The file manager stopped at the first thing it could not remove and left every entry
after it, with a warning only in the status bar. That is how a Firefox profile deleted
while Firefox was alive ended up half deleted. Now it removes everything it can and a
window says how many entries were removed, how many remain and the first one left, with
the hint that a program may be using it. A file deleted while open stays in its folder
as `~EXV0001.DEL` until it is closed, and is not counted as removed. Moving a folder
across disks stops in the same way when the source cannot be removed in full.
`tools/prova_filemgr.sh` has the new case at 32 and 64 bit.

### /proc in the base system (kernel 0.290, /bin/readlink) — tested in QEMU

`/proc` is mounted by itself at boot and cannot be unmounted. It is not a folder on
the disk: a read-only filesystem whose files are written at each read, from the
kernel's tables, in the format of Linux, because the programs reading them parse that
text. There is no buffer and no limit: only the window asked for is copied.

`/proc/meminfo`, `cpuinfo`, `stat`, `uptime`, `mounts`, `version`, `/proc/self` and
`/proc/<pid>/` with `stat` (the 52 fields), `status`, `cmdline`, `exe`, `cwd`, `limits`,
`fd/N` and `task/<tid>/stat`, the CPU counter of each thread. The list comes from what
the ported programs really ask for: Firefox `self/exe`, `status`, `fd/N`, MariaDB
`self/limits`, `self/cwd`, gcc `self/exe`. Data of a process is readable only by its
user and root; the environment is deliberately not there.

EX-OS has no symlinks, so `exe`, `cwd` and `fd/N` are small files holding their target
and `readlink()` reads them. Not there yet: memory per process (`vsize`, `rss` are 0),
the split between user and system time (all in `utime`), the file behind `fd/N`.

`tools/prova_proc.sh`: all of it at 32 and 64 bit. **To be tested** on the laptop:
`task/` with Firefox's threads.

### grep (/bin/grep) — tested

`grep [-EFGivcnlLoqsHhrwxa] [-e MODEL]... [MODEL] [FILE...]`, from a file, a folder
with `-r` or a pipe. The libc has no regular expressions, so they are in the program:
basic and extended, classes, `{m,n}`, run as a set of states, so a pattern can never
blow up on a long line. No limit on the length of a line. Same output as GNU grep in
1463 cases out of 1472 on Linux (the other 9 are `-F -E` together, which GNU refuses);
counts checked inside EX-OS at 32 and 64 bit by `tools/prova_grep.sh`.

### Third-party sources against the official ones (tools/aggiorna-sorgenti.sh) — tested without a new version

`tools/aggiorna-sorgenti.sh` compares the 17 libraries of `tools/sorgenti-terzi.txt`
with the tags of their repositories. `-scarica NOME` takes the official archive,
not the tag, and keeps it only if the signature is good with the keys already
next to it; `-prova NOME` applies the EX-OS patches to a copy; `-applica NOME`
changes `VER=` only after both. It does not compile: the proof stays the build
at 32 and 64 bit and the tests in QEMU. Tried on Apache and APR at their current
version, with a corrupted archive refused. Firefox has its own, `tools/exilla/sorgenti.sh`
(below); gcc, binutils and Rust are not covered.

### The Firefox sources from the latest release (tools/exilla/sorgenti.sh) — tested on Firefox 160.0.1

`tools/exilla/sorgenti.sh` does for Firefox (and, voce 103, Thunderbird) what
`tools/aggiorna-sorgenti.sh` does for the libraries: with no argument it says our version
and Mozilla's (release, ESR, beta, from product-details.mozilla.org); `-scarica` takes
`firefox-VER.source.tar.xz` from archive.mozilla.org, checks its signature with Mozilla's
key and the sha512 with `SHA512SUMS`; `-prova` applies the Exilla patches in order on a copy
in `cross_build/aggiorna` and keeps aside the originals of the files they touch, for
`patch-crea.sh`; `-applica` refuses while a `.rej` is left, moves the old tree out of MEGA
(not deleted) and puts the copy in its place.
- **The key is pinned** in `tools/exilla/mozilla-chiave.txt` (Mozilla Software Releases,
  `14F2 6682 … D98F 0353`, checked against Mozilla's published one; its subkey of August
  2026 is the one after the rotation): a different key stops the download.
- **A `.cargo-checksum.json` is not patched**, it is recomputed for the crates we touch: its
  sums belong to one version of the crate.
- **Found doing it:** three Exilla files of ours (`widget/exos/ExosPosta.cpp`, `ExosPosta.h`,
  `nsWindow.h`) had as "original" an earlier version of ours, so their patches held only the
  last change and a clean tree could not be rebuilt from `tools/exilla/patch`. Now they are
  whole files, like the other new ones.
- **Tried on 160.0.1** (our tree is the 158.0a1 nightly): signature and sha512 good, 340
  patches exact, 42 shifted, 3 refused and adapted by hand (two because Mozilla changed the
  lines, one no longer needed: abseil turns on mmap by itself when `<sys/mman.h>` exists).
  The copy is ready; the switch (`-applica`), the build at 32 and 64 bit and the Exilla tests
  are still to be done.

### The Stamp key saves the screen to a PNG (kbd.drv 0.008, wserver 0.015, pm 0.026, /bin/cattura) — tested in QEMU

**Stamp** saves what is on screen to `$HOME/Images/schermata-AAAAMMGG-HHMMSS.png`
and **Alt+Stamp** only the window with the focus. The same work from a shell,
telnet included: `cattura [-f] [file.png]`, which prints the name and the size.
It works from any graphics mode and depth, and the pointer is in the picture.
With a text console on screen the picture is the desktop as it was left, and
`cattura` says so; without ExWin it says graphics are not running.

wserver only copies the pixels into a zone created by the client (messages
`WIN_MSG_FOTO` and `WIN_MSG_FOTATA`): the PNG is compressed by `cattura`, so the
mouse and the windows do not stop meanwhile. The zone's name comes from the pid
of the sender, not from the message, or a client could have the server write
over another window. A press with the picture of the last one still in progress
is ignored; a picture that was not saved opens a dialog, a saved one is silent.

To take the pictures off the machine, Apache has `http://address/schermate/`, an
alias on `/root/Images` reachable from private addresses only, since a picture
of the desktop shows whatever was open. Only root's folder: the configuration
reaches an installed Apache with the next package.

`tools/prova_cattura.sh` (32 and 64 bit, at 800x600: QEMU did not reach 1366x768,
so the size of the laptop is **to be tested** there): the Stamp file
identical pixel by pixel to QEMU's own screen dump, Alt+Stamp and `-f` smaller
than the screen, from the console, without graphics. `tools/png_confronta.py`
decodes a PNG without ImageMagick.

### Network rings sized from the RAM on every driver (e1000.drv 0.005, nforce.drv 0.015, rtl8169.drv 0.004, pcnet.drv 0.004, sis900.drv 0.020, ne2k.drv 0.004, ar5k.drv 0.004) — e1000, pcnet and ne2k tested in QEMU; nforce, rtl8169, sis900 and ar5k **to be tested**

What the e1000 had since 0.287 now holds for all: no fixed ring sizes. The
order is the driver's `-rx N` / `-tx N`, then `netrx` and `nettx` of
`kernel.cfg`, then the RAM (one receive descriptor per MB and a quarter of
that for sending), always within what the card allows; the table is in
`/boot/kernel.txt`. The queue towards `ip.drv` is the same for all drivers,
ne2k included: it starts at 64 places and grows up to four rings.

- **Tried:** `tools/prova_schede_rete.sh` downloads 16 MB on every card QEMU
  emulates and checks sha256, at 32 and 64 bit. e1000 5-6 s, pcnet 5-8 s, ne2k
  about 225 s (as before: the limit is the card's 16 KB).
- **Not tried:** nforce, rtl8169, sis900 and ar5k are only compiled, QEMU has
  no such cards.
- **The drop to ~90 requests per second is gone:** with 2 GB and 2048 clients,
  125-127 requests per second with 256 and with 2040 descriptors alike, and no
  frames lost by the card.

### A new keyboard in ExWin: accented letters, presses and releases (kbd.drv 0.007, wserver 0.014, exwin.so 0.021) — tested in QEMU

Until yesterday **text boxes and text areas took ASCII only**: an «è» typed in
the editor did not go in. And graphical programs only knew which key had been
pressed, never when it was let go. This is point 2 of voce 42, the step before
SDL and GTK.

- **Accented letters can be typed** in boxes and areas, and are saved as
  **UTF-8**, the encoding of Linux and of the web. Backspace, Delete and the
  arrows move by a whole letter, even a two-byte one. Old files with the
  console's accented letters (CP437) read as before. The text console stays
  CP437, which is what the VGA can draw.
- **The euro and § can be typed**: AltGr+E for the euro (Italian, French,
  German, Spanish; AltGr+4 on the British layout), Shift+ù for § on the
  Italian keyboard. The keymaps are Unicode (they were CP437, which has no
  euro) and the system font has a euro glyph. In the text console the euro
  types nothing, because the VGA cannot draw it.
- **kbd.drv 0.007**: whoever asks gets a long event, with press or release,
  repeat, the character as Unicode, the physical key and all the modifiers
  (AltGr, the Windows key, Caps Lock and Num Lock too). Whoever asks the old
  way gets keys as before. The **right Ctrl** works (it used to be ignored);
  **Num Lock** lights its LED and makes the keypad type digits; Pause is no
  longer read as a Ctrl; holding Caps Lock no longer makes it blink.
- **wserver 0.014**: presses and releases go only to the windows that ask for
  them (`WIN_MSG_TASTI`); the keys the server keeps for itself
  (Ctrl+Alt+Del, the Windows key, Alt+F10) do not reach programs, and neither
  do their releases.
- **exwin.so 0.021**: `ex_track_keys`, the messages `EXM_KEY_DOWN` and
  `EXM_KEY_UP`, and `ex_key_info` for the character, the physical key and the
  modifiers. `EXM_KEY` is unchanged; the euro comes as `KBD_K_UNICODE`.
- The test is `tools/prova_tasti.sh` (with `finestreprova -t`), on the Italian
  keyboard: «aèù€§» in the box, the arrow jumping over the «è» in the area,
  the releases, Shift alone, the right Ctrl. All fine at 32 and 64 bit.
- The tests that click on the desktop and in the file manager find the
  entries **by name** in the screen's pixels (`tools/trova_testo.py`, the
  `punta:` step of `tools/qemu_drive.py`) instead of at fixed positions.

### ExWin without fixed ceilings: windows, list, objects (kernel 0.289, wserver 0.013, exwin.so 0.020) — tested in QEMU

Until yesterday ExWin had **16 windows**: the seventeenth was refused, and the
taskbar saw sixteen at most. Now the windows, the taskbar list, the titles and
the toolkit objects grow with what is asked, up to the memory there is. Tested
with 200 windows opened and closed in a row, at 32 and 64 bit
(`tools/prova_finestre.sh <dir> 200`).

- **Kernel 0.289**: the shared-memory zones (one per window) no longer have
  the ceilings of 20 per process and 24 in all; the tables double when full,
  under a lock (there is SMP).
- **wserver 0.013**: the window table and each window's event queue double;
  titles are as long as they are (48 bytes before), cut on the title bar
  before the buttons. The list for the taskbar goes **in pieces** of 16
  (`WIN_MSG_ELENCATA`, then `WIN_MSG_ELENCO_SEGUE`).
- **Several programs can follow the list**: before, only the last one that
  asked, and a program following it for itself took it away from the taskbar,
  which froze. And the server no longer waits on a follower's mailbox: with a
  program closing 40 windows in a row the server stood still ten seconds per
  piece, screen included. Now a full mailbox means trying again at the next
  loop, from the piece that did not go.
- **exwin.so 0.020**: no more 192 objects per program, nor 4 terminals,
  4 lists, 2 text areas, 6 item controls, 2 menus, 16 watched descriptors:
  each starts from those slots and doubles. And inside the controls:
  - a text area no longer stops at 512 lines of 200 columns (3000 of 240
    for code): it doubles its lines when full and its columns when a line
    does not fit;
  - a list no longer stops at 65536 rows of 63 characters, and draws only
    what fits in the control;
  - a text box holds what is typed (1023 characters before);
  - combos and tabs, more than 32 entries; menus, more than 6 titles, 16
    entries per drop-down and 4 side drop-downs; the right-click menu, more
    than 16 entries; open fonts (96), font files (24), icons (32).
- **filemgr 0.014**: a folder with more than 512 files is shown whole (the
  first 512 before, with no warning), the file clipboard does not stop at
  64, and sorting is a heapsort, since with thousands of files the old one
  (n²) got slow.
- The test `finestreprova -c` fills the controls past the old ceilings: a
  combo of 100 entries, a menu of 10×40 with 8 side drop-downs, 5000 lines of
  600, 4000 of 300 in code, a text box of 5000 characters, a list of 70000
  rows of 150. All fine at 32 and 64 bit.
- `pm` 0.025 (the taskbar) and `attesa` 0.006 read the list with no fixed
  number; `finestreprova` is the test program.

### PHP 8.5 with PDO inside Apache, and 2048 clients for 10 minutes (kernel 0.286)

PHP 8.5.12 runs in EX-OS, on the command line (`/exos/php/bin/php`) and
**inside Apache**: `mod_php` is compiled into `httpd`, because modules are
not loaded into a running program. It is the thread-safe build (ZTS), the
one that goes with the worker MPM; databases are opened with **PDO**
(`pdo_mysql`) or `mysqli`, both on `mysqlnd`, the client written inside PHP.

The sources come from the **GitHub clone** (`php-src`, branch `exos-8.5.12`
from the release tag), as for Apache and MariaDB: every update brings its
sources along. The clone was checked against `php-8.5.12.tar.xz` from
php.net (GPG signature of the release manager): the same files, the archive
only adds the 28 generated ones. A clone needs `bison` and `re2c`; re2c 4.6
was also built from its GitHub clone.

What it asked for, to get in:

- **the libc**: `creat`, `scandir` and `alphasort`, `getdtablesize`; and
  `mkstemp` now returns `open`'s real error (it always said EEXIST: opcache
  wrote "lock file in /tmp: exists" for a `/tmp` that was not there);
- **two patches to PHP**: `st_rdev`, which EX-OS's `struct stat` has not got
  (no device files), and shutting down under `httpd -X`. On Linux the child
  shutdown and the server shutdown run in two processes; on EX-OS, which has
  no `fork`, in the same one, and the second used a TSRM already destroyed:
  `httpd -k stop` died in `ts_free_id`;
- **kernel 0.286**: FLUSH CACHE waits up to 30 s (as Linux's libata). With
  the 5 s of any command, a flush with a lot to write timed out, `fsync`
  said EIO and MariaDB's bootstrap stopped;
- **`/exos/php/tmp`**: EX-OS has no `/tmp`, and without that directory
  opcache does not start, and Apache with it.

**The test** (`tools/stress/stress_httpd.sh`, QEMU 64 bit with KVM, 1 CPU,
2 GB): MariaDB and Apache with PHP in the same EX-OS, a page that with PDO
and prepared statements reads (GET: one row by key, 20 rows by group) and
writes (POST: insert or update, change a value, a transaction of three
statements). 2048 clients from Linux, each with a new connection for every
request, a 60 s ramp and then 10 minutes:

| | 20 clients | 2048 clients |
|---|---|---|
| good requests per second | 145 | 150 at the start, 95 at the end (118 on average) |
| median answer (p50) | 131 ms | 1.8-3 s |
| p95 | 312 ms | 5-34 s |
| errors | 0 of 5140 | 16,783 of 102,067 (16,757 without an answer in 60 s) |

All 2048 clients stayed connected the whole time. No fault, memory flat
(480 MB in use with 310 processes and threads), and at the end Apache and
MariaDB shut down cleanly, with code 0. What the table says is that **the
server answers about 150 requests a second, with 20 clients as with 2048**:
with 2048 each waits its turn, and those left queuing for more than a minute
the client counts as errors.

! **The test found two bottlenecks, and neither is PHP.** The first is the
  host's: QEMU's "user" network accepts the forwarded port with a tiny queue,
  and with a new connection for every request it overflowed 128 thousand
  times. The second is EX-OS's: the e1000 card's receive ring has **96 fixed
  places**, and under the burst of 2048 clients the driver did not empty it
  fast enough (858 thousand frames lost). Queued, voce 39.

### A real fsync, and on a par with Linux (kernel 0.284-0.285)

`fsync()` did nothing: the libc answered 0 at once, because EX-OS's file
systems write at every `write` and nothing was believed to be pending. The
write reaches the disk's cache, though, not the medium (and on FAT not even
that: `fat.c`'s cache waits for `fat_sync`). An InnoDB commit confirmed to
the client could vanish in a power cut.

- **kernel 0.284, `SYS_FSYNC` (258):** the file's volume writes out what it
  keeps (FAT's cache, ext2/ext3's reserved blocks and journal) and the disk
  gets FLUSH CACHE. The whole volume is flushed, not only the file: the
  disk's cache does not know what a file is. `fdatasync` is there too and
  does the same. With an older kernel the libc behaves as before.
- **checked from outside:** with QEMU's trace (`ide_bus_exec_cmd`), 30 s of
  MariaDB with 4 clients send the disk 17,927 FLUSH CACHE EXT; none before.

The comparison (voce 34 of the queue): 10 minutes, 512 clients with no
pause, the same generator, the same `my.cnf` options, the same virtual
machine (QEMU 64 bit with KVM, one CPU, 2 GB, IDE disk, e1000 on QEMU's
user network), the disk images on the same host disk; InnoDB writes to the
disk at every commit on both (`innodb_flush_log_at_trx_commit = 1`):

| 512 clients, 10 minutes | Linux (Debian 13) | EX-OS 0.284 | EX-OS 0.285 |
|---|---|---|---|
| statements/s | 1764 | 343 | **1940** |
| p50 | 220 ms | 1.25-1.48 s | 220 ms |
| p95 | 440-880 ms | 3-5 s | 440-740 ms |
| errors | 1 | 35 (InnoDB deadlocks) | 4 |
| failed connections | 1 | 5 | 5 |

The failed connections are handshakes that timed out during the ramp,
when 512 clients arrive within 25 seconds; all of them reconnected.

Still different: ext4 against ext2, qcow2 against raw, Debian's MariaDB
11.8.6 against 11.8.9. No packet lost by EX-OS.

! **THE MEASUREMENTS IN THE SECTIONS BELOW (2109/s, the 8 hours at 1011/s)
  ARE WITHOUT FSYNC**: the disk was never flushed.

With 0.284 EX-OS was five times slower than Linux on the same work, and
there were two causes, fixed in **kernel 0.285**:

- **the flush with the VFS lock held.** While the disk emptied its cache no
  other thread could open, read or write a file: with MariaDB that means
  every thread, at every commit. Now `fsync` on ext2 does only the file
  system's part under the lock (reserved blocks, the journal) and flushes
  the disk after giving it back.
- **the disk driver spun.** It waits for the end of a command by reading
  the status register (no IRQ 14, for the reasons written in `ata.c`), and
  on one CPU that wait was time taken from everyone. Now, after a few
  readings, it yields the CPU to the other threads and looks again later;
  the disk lock stays its own, and the deadlines are in ticks as before.

Together they are enough: the commits of many transactions now share one
flush (InnoDB's group commit), because the other threads work while the
disk flushes. The disk still gets its FLUSH CACHE commands (17,660 in 30 s
with 4 clients, as with 0.284): commits are just as durable.

Who prefers speed and accepts losing at most the last second of
transactions puts `innodb_flush_log_at_trx_commit = 2` in `[mysqld]`
(manual 4.10).

### No fixed ceilings, and Apache 2.4 (kernel 0.281-0.283, ip.drv 0.017-0.019)

The user's rule: processes, threads, applications and connections are
limited only by the configuration of each server and by the hardware, never
by a number written in the code. What changed:

- **descriptors (kernel 0.281):** a program's table is no longer 128 slots
  in the PCB but blocks of 128 added when needed. The ceiling is
  `RLIMIT_NOFILE`: `fdmax` in `kernel.cfg`, or one per 2 KB of RAM (262144
  with 512 MB), and `setrlimit` lowers it for one program. Sockets, epoll
  sets and shared-memory zones take their numbers from the kernel like
  files; `select` accepts descriptors up to the program's limit.
- **processes and threads (kernel 0.282):** `MAX_PROCESSES` (192 in the
  whole system) and `FILO_MAX` (128 threads per program) are gone. PCBs live
  in blocks added when needed; thread stacks past the 127 reserved slots
  move down towards the heap. `procmax` in `kernel.cfg` sets a lower ceiling
  for a server. **Today's real ceiling is the kernel zone:** every process
  and thread has a 128 KB kernel stack in the first 64 MB, so about 400-450
  in the whole system whatever the RAM (item 35 of the queue).
- **open files, pipes, terminals, poll (kernel 0.282):** the system tables
  of 256 open files, 16 pipes and 4 pseudo-terminals grow on demand; the
  kernel's `poll` takes more than 16 entries.
- **libc:** the tables of threads (128) and of `errno` (64) become chains
  of blocks.
- **textline 0.003:** the file lives in allocated memory, line by line; it
  was 512 lines of 128 characters, and `textline file -v` stopped at line
  512 without a word (`/boot/kernel.txt` has 527: prova_telnet saw the
  last line missing).
- **ip.drv 0.017:** the TCP table grows up to `tcpmax` in `kernel.cfg`, or
  one connection per 32 KB of RAM; `ip.drv -t N` for one boot. **ip.drv
  0.018:** `shutdown(SHUT_WR)` (the half close: stop writing, keep
  reading), and the FIN leaves after the data still queued (before, a
  `close` right after a `write` could lose the tail of the data).

**Apache httpd 2.4.69** (with APR 1.7.6, APR-util 1.6.5, Expat 2.9.0;
`tools/httpd-exos/costruisci.sh 32|64`, group `[apache]` of the tools ISO
and of netinst, in `/exos/apache2`): worker MPM in a single process with
256 threads (`httpd -X`), all modules static, mod_ssl with OpenSSL 4.1, CGI
through `spawn_ex` (EX-OS has no `fork`: item 33 of the queue).
`tools/prova_httpd.sh` tests it in QEMU, at 32 and 64 bit: a page, a CGI
with GET and with POST, a 404, `ab` inside EX-OS and from the host. The
half close took every request from 2 s (Apache waited for the client to
close) to a few milliseconds:

| | before ip.drv 0.018 | after, 32 bit | after, 64 bit |
|---|---|---|---|
| `ab` inside EX-OS, 20 at a time | 8 requests/s | 240/s | 216/s |
| `ab` from the host, 50 at a time | 24/s | 245/s | 234/s |

Making Apache stop cleanly (`httpd -k stop`, exit code 0 at 32 and 64 bit)
took two patches and three fixes outside Apache:

- **The worker listener.** Apache wakes it at shutdown with `pthread_kill`,
  but on EX-OS a signal does not interrupt `accept` or `poll` (item 37 of
  the queue): Apache gave up waiting after 5 s and exited with the listener
  still alive, which then crashed on the freed scoreboard.
  `patch/worker-ascolto.diff` polls every 100 ms and looks at
  `listener_may_exit`, like MariaDB's listener; `patch/worker-uscita.diff`
  destroys `pruntime` before `exit` (the recycled pools were destroyed
  twice: glibc forgives it, the EX-OS malloc does not).
- **Kernel 0.283, a leak of every CGI.** `spawn` gives the child the
  parent's descriptors 0, 1 and 2, then applies the redirections asked for
  over them, and the inherited reference was never given back: each CGI left
  one reference on `logs/error_log`, and after the stop the disk would not
  unmount. `umount` now says which file keeps a disk busy.
- **ip.drv 0.019:** the other side's FIN arriving before ours was
  acknowledged (CLOSING) killed the connection, and our FIN and the queued
  data were no longer retransmitted.
- **Kernel 0.283:** a program that faults prints the addresses of its
  segments found on its stack (`PF:   sulla pila:`, for `addr2line`); that
  is how the listener was found.

The test from the host goes through QEMU's user network, which accepts the
forwarded port on a Linux socket with a tiny backlog: with 50 clients at a
time the host's queue overflows (`ListenOverflows`, 14-16 times per run),
the host drops SYNs and the client retries after 1, 3, 7, 15 s. A capture at
the e1000 showed that the requests that took 20 s, or never finished,
never reached EX-OS. `prova_httpd.sh` now waits up to 120 s and prints the
overflows next to the result.

New tests: `tools/prova_fd.sh` (2000 epoll connections, descriptors up to
4005, `select` past 2048), `tools/prova_moltifili.sh` (386 threads alive
together in one program at 32 bit, 354 at 64, then a clean EAGAIN). Manual
[4.11, the Apache web server](installazione/04-dopo-installazione.md).

### 1024 connections to MariaDB: epoll, a pool of threads, a fair mailbox (kernel 0.280, ip.drv 0.016)

The user asked for a stress test: 1024 clients writing, reading and updating
for 8 hours, measuring speed, load, tolerance and leaks
(a local script, kept out of the repository; clients from Linux over the network,
EX-OS 64 bit in QEMU). The first run stopped at about 20 clients; what it
took to reach 1024:

- **kernel 0.280, a line in front of a full mailbox.** A mailbox holds four
  messages; a sender that found it full retried every 5 ms, and under load
  the fastest threads always got in while others (`ipcfg`) waited 10 s and
  gave up. Now the senders queue in the kernel and are served in order.
- **ip.drv 0.016:** 4096 TCP connections (they were 24) with a hash table;
  a program holds 1024 sockets (they were 64); the 63 KB receive buffer of a
  reused slot was lost (a leak); **epoll and poll live in the stack**, which
  wakes the waiting program itself when a connection is ready.
- **libc:** `<sys/epoll.h>` (`epoll_create1`, `epoll_ctl`, `epoll_wait`,
  `epoll_pwait`, TCP sockets only), `poll` in one message to the stack,
  `FD_SETSIZE` 2048, `RLIMIT_NOFILE` 4096, `shutdown(SHUT_RDWR)` keeps the
  connection in its epoll set and reports the end as Linux does, `strerror`
  knows every network error (it said only «errore»).
- **MariaDB with its pool of threads** (`thread_handling=pool-of-threads`,
  `patch/server-threadpool-epoll.diff`): one thread per connection would
  want a 128 KB kernel stack each in the 64 MB kernel band, and EX-OS had
  192 places for processes and threads (until kernel 0.281). A few dozen threads now serve every
  connection. `mariadb-svc` 0.003 writes `max_connections = 1024` and the
  pool in `/etc/my.cnf` (manual 4.10).
- **e1000:** the receive ring went from 32 to 96 descriptors and the queue
  towards ip.drv from 64 to 256 frames: with a thousand connections the ring
  overflowed and each lost frame stalled a connection for up to 22 s.
- `ipcfg -tcp` lists the TCP connections; `memlog` (inside EX-OS) logs
  memory, threads, the TCP table and where packets get lost.

Measured (QEMU 64 bit with KVM, statements of every kind, TLS; up to
kernel 0.283, when `fsync` did not flush the disk: see the kernel 0.284
section):

| clients | pause | statements/s | p50 | p95 |
|---|---|---|---|---|
| 64 | none | 2109 | 23 ms | 93 ms |
| 1024 | 1 s | 870 | 5.5 ms | 20 ms |
| 1022, 8 hours | 1 s | 1011 | 5.8 ms | 23 ms |

Without KVM the ceiling is the emulated CPU (about 400/s whatever the
number of clients). 1024 clients with no pause at all are more than the
machine can serve: answers pass 30 s, the clients reconnect with a TLS
handshake each, and the server falls behind (that is the tolerance limit,
not a crash).

The 8-hour run (1022 clients, because each program has 1024 sockets and
the listener and the status monitor take two): 29.1 million statements, 5
errors (all InnoDB deadlocks between concurrent transactions, which the
client is meant to retry), no reconnection, no refused or dropped
connection, no packet lost by the card or by ip.drv, a clean shutdown
("Shutdown complete"). The speed and the p95 of the first and last quarter
are the same (1012/s and 1013/s, 23 ms). MariaDB's `Memory_used` stays at
840 MB for the whole run; EX-OS's free memory drops by about 1 MB per hour
while the tables grow and comes back entirely when mariadbd exits: no
leak in the system. One slowdown of about two minutes (p95 2.5 s, worst
answer 9.8 s, at hour 4.3) left no trace on the server side; its cause is
still to be found. The comparison with Linux on the same hardware (voce
34) is in the kernel 0.284 section: with the same work on the disk EX-OS is
slower.

### MariaDB 11.8 LTS runs in EX-OS (0.279)

**tested at 32 and 64 bit in QEMU** (`tools/prova_mariadb.sh`): `mariadbd
--bootstrap` makes the system tables, the server starts and listens on
`127.0.0.1:3306`, the `mariadb` client creates an InnoDB table, puts three
rows in, rolls a delete back, and `mariadb-admin shutdown` stops the server
cleanly; Linux finds `ibdata1` and the table's `.ibd` on the disk afterwards.

MariaDB **11.8.9**, the latest release of the stable long-term branch (the
official archive, GPG signature checked, in `cross_build/terze-parti/mariadb`),
built by `tools/mariadb-exos/costruisci.sh` on the real EX-OS libraries:
ncurses, zlib, bzip2, xz, lz4, zstd, PCRE2, fmt and OpenSSL 4.1.0. Everything
is static (`mariadbd` is 27 MB stripped); the server listens on TCP only,
because EX-OS has no AF_UNIX sockets. `innodb_buffer_pool_size_max` must be
given: by default InnoDB reserves 8 TB of addresses at 64 bit, and an EX-OS
program has 3 GB. Group `[mariadb]` of the tools ISO and netinst: the server
in `/exos/mariadb`, the clients and `mariadb-svc` in `/exos/bin`, the
sources. The tools ISO is a DVD now (about 500 MB per architecture for
MariaDB alone).

**The connectors, also on their own:** the catalogue has two entries.
`netupdate -install:mariadb` brings the server and the connectors;
`netupdate -install:mariadb-connector` only the connectors, for programs that
talk to a MariaDB or MySQL server: **Connector/C 3.4.11** (`<mysql.h>`,
`libmariadbclient.a`), **Connector/C++ 1.1.9** (`<conncpp.hpp>`,
`libmariadbcpp.a`, built on the system Connector/C), **`mariadb.bi`** for
FreeBASIC and the Rust crate **`mariadb`** (one file, no dependencies, because
EX-OS has `rustc` but not `cargo`). `tools/prova_mariadb.sh` runs one program
per language against the server inside EX-OS (FreeBASIC at 32 bit).

**The service:** `mariadb-svc -install` (as root) makes the user `mysql`,
`/etc/my.cnf`, the system tables in `/var/lib/mysql`, the server's TLS
certificate (once: MariaDB 11.8 would make a 4096-bit RSA key at every start,
minutes on a slow CPU), and a block in
`/boot/avvio.sh`: the server starts at every boot, before the login, and the
shutdown closes it (manual 4.10). The details are in
`tools/mariadb-exos/leggimi.md`.

What it needed in EX-OS:

- **kernel 0.279, signals.** `sigwait`, `sigtimedwait`, `sigwaitinfo`
  (`SYS_SEG_ATTENDI`) and `sigsuspend` (`SYS_SEG_SOSPENDI`). A signal with the
  default action sent to a program that blocks it now stays pending, as POSIX
  says, instead of ending the program: that is how a server blocks SIGTERM in
  every thread and waits for it in one (`tools/prova_sigwait.sh`, 32 and 64
  bit, also from another process).
- **kernel 0.279, `open` of a directory.** `open(dir, O_RDONLY)` gives a
  descriptor instead of EISDIR: `fstat`, `fsync`, `dup` and `close` work,
  `read` gives EISDIR, asking to write gives EISDIR. Aria and InnoDB open the
  data directory to make a new file durable. A directory opened with
  `O_TRUNC` used to be truncated before being refused.
- **libc:** `setgid`, `setreuid`/`setregid`, `setgroups`/`initgroups`,
  `execl`/`execlp`, `getpwent`, `getlogin`, `IN6_IS_ADDR_*`, `NI_NAMEREQD`,
  `POLLRDNORM` and its siblings, `<netinet/ip.h>`, `<netinet/in_systm.h>`,
  `<sys/poll.h>`, the BSD types (`u_char`, `uint`...); `getrlimit` says 128
  descriptors (it still said 32, from before kernel 0.232); `fscanf` left
  `feof` off at the end of a file, because its `fseek` back cleared it.
- **libc, `_FILE_OFFSET_BITS=64` on the 32-bit system.** As with glibc, a
  program compiled with it (MariaDB is) gets a 64-bit `off_t`, and `lseek`,
  `pread`, `pwrite`, `ftruncate`, `truncate`, `fseeko`/`ftello`, `mmap`,
  `stat`/`lstat`/`fstat` become their `*64` versions in `libc.a`; programs
  compiled without it do not change. The kernel still holds 2 GB per file
  on both architectures: past it the libc refuses (EFBIG, EOVERFLOW, end of
  file) instead of cutting the position. At 64 bit `lseek`, `pwrite` and
  `ftruncate` used to pass a 3 GB position to the kernel cut to 32 bits (an
  `ftruncate` to 3 GB changed the file): refused now too
  (`tools/prova_lfs.sh`, 32 and 64 bit).
- **ip.drv 0.014, 127.0.0.1 without a card.** The loopback lives in
  `ip.drv`, which used to end when it found no network card: a machine
  without a network could not reach its own server. Now it stays with
  127.0.0.1 alone; a card arriving later is taken as before.
- **kernel 0.279, shutdown.** Before power off or restart the kernel sends
  SIGTERM to every program that is not a driver and waits up to 30 s, so
  `mariadbd` closes InnoDB before the disks are synced; `setuid` changes
  every thread of the program, not only the caller (`mariadbd` drops to
  `mysql` with its threads already running). The libc's `getpwnam` &c.
  read `/boot/utenti`.

### OpenSSL 4.1.0 with sockets and threads, and 127.0.0.1

**tested at 32 and 64 bit in QEMU** (`tools/prova_openssl.sh`, with the QEMU
network): four threads hash, encrypt and make `SSL_CTX`s at once; a TLS
server and a client in one program talk over `127.0.0.1` (`SSL_set_fd` on
an accepted socket, as MariaDB does); TLS 1.3 to `www.example.com:443` on a
real socket, the certificate verified; the `openssl` program computes the
same SHA-256 as Linux, makes an Ed25519 key and a certificate that Linux's
`openssl verify` accepts, and `openssl s_client` connects and verifies.

Until now EX-OS had a snapshot of OpenSSL's development branch built with
`no-sock no-threads`: TLS only through two memory BIOs (`provatls`). MariaDB
needs sockets and locks, and the rule is the latest public release: now it
is OpenSSL **4.1.0** (23 October 2026, GPG signature checked), built by
`tools/openssl-exos/costruisci.sh` like the other libraries, with sockets,
pthreads and the `openssl` program; sources on both tools CDs, group
`[ssl]`, so in netinst too. `provatls`, `provassl` and libgit2 are rebuilt
on it (`tools/openssl-exos/prepara-openssl.sh` is gone).

What it needed elsewhere:

- **ip.drv 0.013, loopback.** A frame for this machine (127.x.x.x or its own
  address) went to the network card and was lost: no program could reach a
  server on the same machine. Now it waits in a queue inside ip.drv and
  comes back in as if it had arrived; no ARP, the source address is the
  destination itself, and 127.x.x.x from the wire is dropped.
- **libc:** `select()` sees sockets and every file (`fd_set` grew from one
  32-bit word to 1088 descriptors; a program built with the old one still
  works with the new `libc.so`, because only the words below `nfds` are
  touched); `getsockname` gives the address `bind` was given; `connect` to
  `0.0.0.0` goes to `127.0.0.1` as on Linux; `getservbyname`/`getservbyport`
  (with `mysql` 3306 in the table); `setpgid` to the process's own group.

### Seven libraries as real EX-OS libraries: zlib, bzip2, xz, lz4, zstd, PCRE2, {fmt}

**tested at 32 and 64 bit in QEMU** (`tools/prova_librerie.sh`): one
program (`tools/prove/libreprova.cpp`) packs and unpacks 64 KB with each
compressor, matches with PCRE2 (and its POSIX `regcomp`/`regexec`), formats
with fmt; then `xz`, `zstd`, `bzip2` and `lz4` compress a file inside EX-OS
that Linux unpacks, and unpack archives Linux made — compared byte by byte.

The libraries MariaDB needs, ported first and on their own because others
will need them too (the rule: every dependency becomes a real library before
the program that wants it). Each is the latest public release, its archive
checked (GPG signature, or the published sha256) and kept in
`cross_build/terze-parti/`; each has its `tools/<name>-exos/costruisci.sh`
(the shared part in `tools/librerie-exos/comune.sh`), builds at 32 and 64
bit, goes into the cross sysroots (`-lz`, `-lbz2`, `-llzma`, `-llz4`,
`-lzstd`, `-lpcre2-8`, `-lfmt`) and onto both tools CDs with its sources,
one catalogue group each (`[zlib]` `[bzip2]` `[xz]` `[lz4]` `[zstd]`
`[pcre2]` `[fmt]`), so into netinst as well:

| library | version | programs |
|---|---|---|
| zlib | 1.3.2 | |
| bzip2 | 1.0.8 | bzip2, bzip2recover |
| XZ Utils (liblzma) | 5.8.4 | xz, xzdec, lzmadec, lzmainfo |
| LZ4 | 1.10.0 | lz4 |
| Zstandard | 1.5.7 | zstd |
| PCRE2 | 10.49 (8, 16, 32 bit, POSIX) | pcre2grep, pcre2test |
| {fmt} | 12.2.0 (headers and libfmt) | |

- {fmt} 12.2.0 does not build for any 32-bit target (its own issues #4811,
  #4886): the fix merged upstream (pull request #4813) is applied until a
  release carries it;
- PCRE2 without the JIT for now (same results, interpreted);
- the libc gained `O_NOCTTY`, and `fchown` to the owner `stat` already
  reports succeeds (xz said "Cannot set the file owner").

**And a kernel fault the 64-bit xz found:** every `pthread_create` of a
64-bit program with no `__thread` variables failed (`EIO`, "non riesco a
leggere l'immagine TLS per il filo"). A log line in the ELF loader read the
TLS segment even when there was none; the compiler took that as proof the
pointer was not NULL and dropped the checks above it, so the thread image
came from address 0. Fixed in `kernel/loader/elf.c`.

### ncurses 6.6: full-screen programs on the console and over telnet

**tested at 32 and 64 bit in QEMU** (`tools/prova_curses.sh`): a curses
program draws boxes, reverse video, colours, deletes a line, opens a window,
and reads Up, F1, Delete and Page Down as `KEY_UP`, `KEY_F(1)`, `KEY_DC`,
`KEY_NPAGE`; the screen is read back exactly from QEMU
(`tools/schermo_testo.py` matches every cell with the console's own glyphs).
**And the same program compiled inside EX-OS** with the tools CD's `gcc
-lncurses` does the same.

The real curses library, the first of the libraries MariaDB needs that are
useful to everything else as well: `libncurses.a` (`-lcurses` too),
`libpanel`, `libmenu`, `libform`, `libncurses++`, the headers, the terminfo
database, and `tic`, `infocmp`, `toe`, `tput`, `tset`, `reset`, `clear`,
`tabs`. Built from the upstream 6.6 release (signature checked) by
`tools/ncurses-exos/costruisci.sh`; in the cross toolchains' sysroots, and
on both tools CDs with its sources (group `[curses]`), so in the netinst
repositories too.

- the console is `TERM=vga`, and it has its own terminfo entry
  (`tools/ncurses-exos/exos.ti`): only what `vga.c` really does. For it the
  console learnt the sequences curses relies on: insert and delete
  characters and lines, erase characters, column and row, a scrolling
  region, reverse index, saving the cursor, reverse video. In raw mode the
  keyboard also sends F1-F12, Insert, Delete, Page Up and Page Down;
- `vga`, `linux`, `xterm`, `xterm-256color`, `vt100`, `vt220`, `ansi`,
  `screen` and `dumb` are compiled into the library: curses works on a
  system with no terminfo on its disk;
- the libc gained what ncurses asked for: `getopt`, `getopt_long`
  (`<getopt.h>`), `getpgrp`, `ttyname`, and the terminal `ioctl` numbers in
  `<sys/ioctl.h>`.

Not yet: `ncursesw`, the wide-character library, which waits for UTF-8 in
the libc (its `wchar_t` is Latin-1 today); and `SIGWINCH`, so a resized
telnet window is not noticed by a running program.

### A real terminal: termios on the console and in telnet (0.279)

**tested at 32 and 64 bit in QEMU** (`tools/prova_termios.sh`: libctest and
`tools/prove/termiosprova.c` with `tcgetattr`/`tcsetattr`/`cfmakeraw` from
libc.a, keys typed on the console in raw mode; `prova_telnet`, `prova_ctrlc`,
`prova_ssh` again).

Until now a terminal had two modes, "lines with echo" and "raw", and no
`struct termios`: `tcsetattr` did nothing. Editors, `less`, `top`, and above
all **ncurses** (the next port, which MariaDB's client and many other
programs need) cannot work like that. Now both terminals keep a Linux
`struct termios` (same values, same `ioctl`: `TCGETS`, `TCSETS`, `TCSETSW`,
`TCSETSF`, `TCFLSH`, `TIOCGWINSZ`, `TIOCSWINSZ`):

- **in a telnet session** (pty): `ICANON`, `ECHO`, `ECHOE`, `ECHOK`, `ISIG`,
  `ICRNL`/`INLCR`/`IGNCR`, `OPOST`+`ONLCR`, `VMIN`/`VTIME`. `isatty()` is
  true, and `TIOCGWINSZ` gives the client's window size;
- **on the console**, one per virtual console: with `ICANON` off the keyboard
  driver hands over keys one by one instead of lines, an arrow as its whole
  `ESC [ A`, Enter as CR, `^C` as a byte when `ISIG` is off, no echo when
  `ECHO` is off; `poll()` on stdin wakes as soon as a key is pressed.
  `TIOCGWINSZ` gives the real size (100x37 in QEMU), not 80x25.

A program that leaves the terminal raw and dies does not leave a deaf
shell: the shell (0.009) puts sane modes back before every prompt.

### A telnet session that will not come back, and the screen of the integrated NVIDIA (0.278)

**tested at 32 and 64 bit in QEMU** (`tools/prova_telnet.sh`, telnetd killed
and run again, a program of the other architecture, a session left in the
middle of `gfedit`), and the screen numbers **measured on the CQ60**.

**On the CQ60 port 23 opened and no byte came back, even after `kill` and a
new `telnetd`.** It took a reboot. Three faults in a row:

- a 32-bit program on the 64-bit kernel is `ENOEXEC`, and the shell (0.008)
  sent every `ENOEXEC` to the program associated with the file — `gfedit`.
  `gfedit` reads the machine's own keyboard, not the session, so the session
  sat in it. Now the shell says **"e' un programma a 32 bit, questo sistema
  e' a 64"** (or the other way round) and goes on;
- when the client leaves, `telnetd` interrupts the session's shell; but
  `waitpid`, woken, **went back to waiting** for the child, and the shell
  never reached the exit on the way out of the system call. Now `waitpid` and
  `thread_attendi` return `EINTR` to an interrupted process, as pipes, IPC
  and the pty already did. `telnetd` (0.011) also collects the shell, which
  stayed a zombie;
- the killed `telnetd`'s listener **stayed on port 23 in `ip.drv`**: it
  accepted every connection with nobody behind it and refused the new
  `telnetd` (`EADDRINUSE`). `ip.drv` (0.012) frees a port whose program is
  dead, both when someone asks to listen and when a SYN arrives.

**The integrated NVIDIA wrote its screen at 47 MB/s.** On the MCP77/79 the
"video memory" is RAM the BIOS set aside, and through the card's BAR1 every
pixel goes CPU -> card -> RAM: write-combining (0.275) changed nothing.
`nv50.drv` 0.008 hands the kernel the address of that RAM instead, which the
kernel maps write-combining like any framebuffer:

```
                               BAR1     stolen RAM
CPU fills the screen             47       4167   MB/s
shadow in RAM -> screen          47       1475
CPU reads the screen              4         82
```

A full 1366x768 frame of the desktop goes from about 90 ms to about 3.

### Threads reading the same file: a real `pread` and locked streams (0.277)

**tested at 32 and 64 bit** with `tools/prova_fili.sh`, which puts the threads
of one program on the same file and on the same `FILE`:

```
FILI pread 0 giri 101824      8 threads, random pread on one descriptor, 3 s
FILI posizione 0 letti 1048576 su 1048576, pezzi storti 0
FILI pwrite 0                4 threads, pwrite on different words
FILI stdio 0 righe 12000 rotte 0
FILI mutex 0                 8 threads, 20000 rounds each under pthread_mutex
```

**And the same test with the old libc** (`PROVA_CFLAGS=-L<dir with the old libc.a>`)
fails, as it must: `FILI pread 7 giri 84118` and `FILI stdio 1 righe 12019
rotte 9`. A test that passes with the fault proves nothing: the first version
lasted less than one scheduler quantum, the threads never crossed, and it
passed with both.

**Firefox showed the fault on the CQ60: pages with no text.** The log said
"Unable to load glyph" and "logins.db: file is not a database", with the font
files right on disk (same sha256). libc did `pread` as `lseek` + `read` +
`lseek`, and **threads share the descriptor table**, so the position too: two
threads reading the same file at once move the position under each other and
take home the bytes of the wrong place. Firefox maps its fonts by reading them
with `pread` from several threads.

**`pread` and `pwrite` are now kernel calls** (`SYS_PREAD` 8, `SYS_PWRITE` 9):
they read and write at the given spot without touching the descriptor's
position. A new libc on an old kernel gets `ENOSYS` and falls back to the old
way, under its own lock. And **`read`, `write` and `lseek` on a file hold the
position while they move it** (`posizione_di` in the descriptor): before, two
`read`s at once could read the same piece and move the position backwards.

**The stdio streams have a lock** (`flockfile`, `funlockfile`, `ftrylockfile`
and the `_unlocked` forms). They had none: two threads writing on `stderr`
together could write the same byte of the buffer or flush it twice. Every
function takes it by itself, once per call; whoever wants one whole line out of
several calls holds `flockfile`. It is **recursive**, and the holder is known
by its thread pointer, read from `%gs`/`%fs` with no system call.

> **What was already there, and stays:** `pthread_mutex`, conditions,
> reader/writer locks, `pthread_once` and semaphores sit on the three-state
> lock that sleeps in the kernel (`SYS_ATTESA_DORMI`/`SYS_ATTESA_SVEGLIA`);
> `malloc`, `errno` (per thread) and the mapping table had theirs. For a
> program's own atomic variables EX-OS's GCC has `<stdatomic.h>` and the
> `__atomic_*` builtins, which become `lock` instructions: no libc needed.

### Swap works (kernel 0.276) — tested in QEMU, still to be tested under pressure on real hardware

The swap partition existed since August, but did not hold under pressure:
at 32 bit the kernel stopped at the first eviction (it read a 4 MB directory
entry as a table), at 64 bit program memory got corrupted (the page was
written to disk while its owner could still write to it, the one bounce
buffer was shared by evictions and swap-ins with no lock, the owner could die
meanwhile) and a process was killed when the other CPU held the pages. Fixed
in 0.276, and pages a device uses (DMA, MMIO, framebuffer) are never evicted.
`mem` shows the area: size, what is on it, pages written out and read back
since boot. `tools/prova_swapmem.sh` (32 and 64 bit, run again with kernel
0.279) fills the RAM with `swaptest`, one alone and two at once, and every
page comes back identical. On the CQ60 the 4 GB area comes up but RAM has
always been enough. How to set it up and check it: manual,
[4, the swap area](installazione/04-dopo-installazione.md).

### USB sticks are `/USB/USB0`, `/USB/USB1`... — tested in QEMU

A stick now always goes to `/USB/USB<n>`, one number per stick inserted, the
first free one: before, it was `/USB/DRIVE0` without a partition table and
`/USB/HDD0p1` with one (most sticks formatted by Windows). A second partition
of the same stick is `/USB/USB0p2`. And two sticks on two different
controllers (OHCI and EHCI) now both appear: every controller driver asked for
the block device name `usb0`, and the second was refused; the shared mass
storage code (`drivers/usb/usb_massa.c`) now takes the first free `usb<n>`.
Tried in QEMU: a partitioned stick and a whole one at once, `/USB/USB0` and
`/USB/USB1` with their files, `eject` of one, the bar's `USB` item. Still one
stick per controller: two sticks in ports of the same controller give one disk.

### The status bar: `extaskbar` (kernel 0.255) — tested in QEMU

What background services print (WiFi, USB, the network, automount...) no
longer has to cover the command line. `extaskbar` puts a text bar on the
last line of the screen, 80 columns, with fixed-width items: `USB0
Connected` / `USB none`, `WIFI Connected`, `RX 00000000`, `TX 00000000`,
`FAIL 000000`, `LOST 000000`; also available: `ETH`, the time, `BAT`,
`TELNET`.

- `extaskbar -show` starts it, `-hide` hides the bar (the logs are still
  collected and the console stays clean), `-stop` stops it and everything
  goes back to the console as before.
- `extaskbar -menu`: which items to show, with `[x]` boxes and the count of
  the columns left - an item that does not fit is refused; changes apply at
  once. Also the memory kept for each log (64 KB unless changed).
- `extaskbar -details`: the services on the left, the chosen one's log on the
  right, live, each line with its time; `f` also keeps it in
  `/boot/<service>/log.txt`, never more than 1024 KB (the oldest lines go).
- The settings are in `$HOME/.config/extaskbar.cfg`. `extaskbar -boot:on`
  (root, or the box in `-menu`) starts it at every boot: `extaskbar -auto` is
  the first line of `/boot/avvio.sh` and does nothing unless switched on.
- How: what a process started with `&` (and its children) writes goes, while
  `extaskbar` runs, to a ring in the kernel instead of the screen; the
  commands typed in the foreground print as always, and a session opened by
  `telnetd` is not a background job. The services send their items with
  `lib/include/extaskbar.h` (the WiFi driver, `wifisim` and `automount` do).
  `-menu` and `-details` draw on the terminal's alternate screen
  (`ESC[?1049h`, new in the console), so when they close the console shows
  what it showed before; they work over telnet as well.

Tried in QEMU at 32 and 64 bit: the bar, `ping &` collected and not on the
screen, `-details`, `-menu` (an item refused when it does not fit, one
removed and another added, applied at once), `-hide`, `-stop` with the output
back on the screen, the WiFi items of the simulated card. Not yet on a real
machine; saving to `/boot/<service>/log.txt` not tried yet (the CD is
read-only).

### WiFi: the first real card, Atheros AR2425 (`ar5k.drv`) — tested

On the user's HP laptop (NVIDIA MCP77, Atheros AR2425, 168c:001c) EX-OS 64
joins the home network in WPA2 and gets its address by DHCP: telnet, ping
to the router and to the Internet, DNS and a download all go over the radio.
`drivers/ar5k` uses Linux's ath5k hardware code (ISC licence) for the chip;
association, the WPA/WPA2 key handshakes (`lib/exwifi`) and CCMP/TKIP are
done in software. Open, WPA and WPA2 networks with a password; WEP,
Enterprise and WPA3 are refused with the reason. The radio is switched on
through the laptop's embedded controller. `tools/prova-ar5k/prova.sh`
checks the connection code on the host against a pretend access point (23
of 23). Still slow (about 50 KB/s: the driver looks at the card every 10 ms
instead of using its interrupt), and the driver has to be started by hand.

### Libraries: a real dynamic loader, OpenSSL at 64 bit, iconv

- **`dlopen` loads ordinary ELF shared objects** (`lib/libc_dl.c`, static
  libc), at 32 and at 64 bit: the libraries a program needs (DT_NEEDED,
  searched in `LD_LIBRARY_PATH`, DT_RUNPATH with `$ORIGIN`, `/lib`,
  `/exos/lib`...), every relocation a PIC library has, constructors and
  destructors, `__thread` variables (one block per thread), `dlclose` that
  unmaps, `dladdr`. EX-OS programs are static and keep no symbol table, so the
  libc carries its own: `lib/libc_esportati.h`, written by
  `tools/libc-esportati.py`, gives a library every libc function of the
  program that opened it; it stays out of programs that never call `dlopen`.
  The cross compilers now accept `gcc -fPIC -shared`
  (`tools/gcc-exos/specs-condivise.sh`). `tools/prova-dl/prova-dl.sh`: 14 of
  14 inside EX-OS, 64 and 32 bit. Not handled: the initial-exec TLS model in
  a library (refused with a message), lazy binding (everything is bound at
  load). It is the first piece a native `rustc` needs (procedural macros are
  shared objects).
- **QuickJS at 64 bit**, thanks to it: as at 32 bit the two JavaScript
  engines are libraries, `/exwin/lib/exjs.so` and `/exwin/lib/quickjs.so`,
  now ordinary ELF objects that the ExJs stub opens with `dlopen`
  (`tools/costruisci-utente64.sh`, step 6). `tools/prova-dl/prova-js.sh`
  runs the same script on both inside EX-OS 64 and gets the same answer. Not
  yet tried in the browser at 64 bit.
- **OpenSSL at 64 bit**: `tools/openssl-exos/costruisci.sh 64` (since 0.279
  OpenSSL 4.1.0 with sockets and threads, see above).
  Inside EX-OS 64, `provatls www.example.com` makes a TLS 1.3 handshake over
  the EX-OS network, verifies the certificate and reads the page. libcrypto,
  libssl, the headers and the CA store are on `dist/exos64-tools.iso` and in
  `netinst_64`.
- **`iconv`** in the static libc: UTF-8/16/32, ASCII, Latin-1, Latin-9,
  Windows-1252, CP437, with `//TRANSLIT` and `//IGNORE`.

### WiFi: the `wifi` command and the icon next to the clock (kernel 0.250)

The connection manager and its two faces, at 32 and at 64 bit:

- **`wifid`** (`bin/wifid`) is the one process that decides which network the
  radio joins. It keeps the saved networks, connects at start-up and again
  after a drop (the strongest saved network, or the one marked default),
  gives the address to `ip.drv` (DHCP or static, per network) and, when the
  radio drops, moves the way out back to the Ethernet card. Passwords are
  never stored: it keeps the key derived from them (PBKDF2), encrypted with
  AES-256-GCM, one key file per user (`/cfg/wifi/root.*` for root,
  `~/.exwin/config/wifi.*` for a user).
- **`wifi`** (`bin/wifi`): without arguments it shows the state, the signal
  and the speed; `wifi -config` opens a menu (networks heard, hidden
  network with every field, saved networks, disconnect, connect at boot);
  `-showlist`, `-connect:SSID [-save] [-default]`, `-disconnect`,
  `-forget:SSID`, `-default:SSID`, `-boot:on|off` do the same from a script.
- **The desktop**: an icon next to the volume, grey with an X when not
  connected, green with the signal bars when connected; a click opens the
  panel (`exwin/bin/exwifi`) with the list, the form (SSID, hidden, security,
  password, save, default, DHCP or static IP) and Disconnect / Default /
  Forget. Every change shows a notice next to the icon that goes away by
  itself.
- **Who may change what**: root may connect at boot, before anyone logs in
  (`wifi -boot:on`); every user then uses that connection, and only root
  changes it. When root has not switched it on, each user runs and manages
  their own. To know who is asking, kernel 0.250 adds `proc_uid()`, the uid
  of another process (an IPC message does not carry it).
- `ip.drv` 0.011 moves to another card while running (`IP_MSG_LINK`), and
  `dhcp` 0.003 gives its port back when it exits (a second `dhcp` used to fail).
  The shell keeps 16 background jobs instead of 8: a boot script with the
  network, USB, telnetd and `wifid` starts nine.

**The first real card is the Atheros AR2425 (see above).** Everything above was tried in QEMU, at
32 and 64 bit, with `wifisim.drv`, a simulated card with five networks (WPA2,
open, WEP, hidden) that carries the frames over the Ethernet card: list,
wrong password refused, WPA2 with DHCP and with a static address, hidden
network, a drop and the reconnection, default and forget, connection at boot
before login, a user refused when changing root's network, the icon and the
panel. The security every driver will share is in `lib/exwifi`: the WPA/WPA2
keys, the 4-way and group key handshakes (client side), CCMP, TKIP with
Michael, and WEP; `tools/prova_exwifi.sh` checks it on the host against the
IEEE 802.11 and RFC test vectors (41 of 41); `ar5k.drv` uses it. To do: other
chips, WPA3 and WPA2 Enterprise.

### ExWin: every control can be hidden and switched off

**tested in QEMU** (`tools/prova_exwin_stato.sh`: the project is compiled
inside EX-OS and tried with the keyboard) — asked in correzioni.txt.
`ex_set_visible(c, 0)` hides a control without destroying it;
`ex_set_enabled(c, 0)` leaves it in place, dimmed, showing whatever the program
puts in it, but the user cannot press it or type in it. They work on every
kind of control; `ex_is_visible` and `ex_is_enabled` say how it stands. Before,
a disabled control could be told apart only if it was a button, a checkbox or
a radio. In exide they are two properties, `visibile` and `attivo`, and on the
form they can be told at a glance. The EX-IDE manual explains them in Italian
and in English.

**Mouse over** (`tools/prova_exwin_sulmouse.sh`): with the `sulmouse` property
a control gets one more handler, `<Name>_MouseOver()`, called when the pointer
arrives over it; `ex_show_popup_pointer("text", "0", "0", "0")` shows a box by
the pointer, with background, text and border colours written as
`"255,255,255"` or `"0"` for the defaults. Trying it showed that in programs
made with exide the `_Changed` and `_Enter` handlers of text boxes were never
called: now they are (exwin.so 0.015, exide 0.024; open the project and save
it once).

### Installing without a CD drive: `make installa`, and the guide (kernel 0.240)

**tried in QEMU; on hardware it is under way** — for a PC with neither floppy
nor CD. `make installa` builds `dist/installa.img`: 1.44 MB loaded whole into
RAM (so it boots from a stick), with the disk tools, the network and
`netupdate`. The small running system is installed on the disk, and from
there the rest comes from the network. The step-by-step guide is in Italian
and in English, page and text: `exwin/doc/installa-usb.html`,
`exwin/doc/installa-usb.en.html`, `INSTALLA-USB.txt`, `INSTALLA-USB.en.txt`
(generated by `tools/doc-installa-usb.py` from one source).

From the first trial on the real PC: the SATA disk in native mode is found,
but in DMA it stayed stuck («timeout BSY»). From 0.240 those channels work in
PIO and a disk left busy is reset.

**0.246**: the extra processors run programs (SMP stage 3). One lock on the
kernel, taken on entry (interrupts, exceptions, system calls) and released on
the way back to ring 3: one processor at a time in the kernel, programs on
all of them. A GDT and a TSS per processor, the current process per
processor; the lock is a ticket lock (first come, first served). Two
different programs use two processors; the threads of one program do not,
until there is TLB shootdown between processors (stage 4). **Off by
default**: `smpprova -accendi` turns it on until the next reboot, `smp = 2`
in `kernel.cfg` at every boot. `smpprova` measures the work done: in QEMU
with KVM, 4284 rounds for one program on one processor, 4296 alone on two
(no loss), 8152 for two together on two processors, 12638 for four together
on four. With one processor nothing is switched on. **On the real PC**
(Core 2 Quad, NVIDIA MCP73, 10 October): after `smpprova -accendi` two
programs together do 2.04 times the work of one, four do 3.32; network, DNS
and `netupdate` work with the processors running. A short telnet session:
long use and ExWin are still to be tried.

**0.247**: writing to ext2 takes far fewer disk commands. For every
kilobyte appended to a file the driver issued nine (bitmap, descriptor,
zeroed block, pointers, data): blocks are now taken in groups of up to 64
with one bitmap write, a new data block is not zeroed on disk if it is about
to be written, and the pointer block is written once per call. When
`write()` returns the disk is as consistent as before. `netupdate` 0.028
writes in 64 KB pieces instead of at every packet. Tried in QEMU: Exilla
(290 MB) installs in 5 minutes instead of 9, the disk passes Linux's
`e2fsck` and the files have the right hashes. On a disk in PIO the gain
should be larger: to be measured on the PC. Deleting is still slow (one
block at a time).

**0.248**: deleting a file on ext2 no longer costs five disk commands per
kilobyte. The group's bitmap stays in memory while a file's blocks are freed
and is written once, with the descriptor. In QEMU `rm` of 50 MB goes from
more than twenty seconds to about six, and the disk passes `e2fsck`.

**0.249**: ext3 and ext4 volumes are **read**: extent trees, 64-bit group
descriptors, `flex_bg`, large inodes, indexed directories (read linearly).
Such a volume is mounted **read-only** and the log says why; plain ext2 is
written as before. Writing ext3/ext4 is planned as a module loaded after
boot. Not handled: `meta_bg`, encryption, compression, and files whose data
sits inside the inode (`inline_data`), which are refused with a message.
Tested in QEMU at 32 and at 64 bit on volumes made by Linux's `mke2fs`
(ext4 with 1 KB and 4 KB blocks, ext3, ext2): a 5 MB file, a sparse file
and a small one have the same SHA-256 as on the host, a directory of 400
files lists whole, and writing is refused. New command `sha256sum`.

**0.250**: `proc_uid()`, the uid of another process, for the WiFi manager
(see "WiFi" above). The 32-bit kernel changed only in the system-call table.

**0.251**: a disk whose status shows BSY together with an error left over from
the previous command (the user's NVIDIA MCP77 laptop, after it refuses READ
NATIVE MAX) no longer loses its partitions at boot: the error bits are read
only once BSY is clear, as the ATA standard says, and a partition table that
cannot be read is retried and logged. Before, the system installed fine and
then found no root. Also for the USB keys (`make usb`, `make usb64`): when the
BIOS boots the key as a hard disk, the loaders read it by LBA and copy it into
RAM anyway - tried on the laptop, whose BIOS does exactly that.

**0.252**: `shutdown` turns a real PC off. Until now only emulators were
switched off (their fixed ports) and a real machine stopped on "it is now safe
to switch off". `kernel/arch/x86/acpi.c` reads the ACPI tables at boot (RSDP,
RSDT/XSDT, FADT, and the `_S5_` values from the DSDT) and writes what it found
in the log - `dmesg` tells what shutdown will do on that machine - and
shutdown writes the S5 state to the power-management ports, switching the
chipset to ACPI mode first if needed. `reboot` tries, in order, the ACPI reset
register, the keyboard controller, the chipset's 0xCF9 port and a triple
fault. Tried in QEMU (the shutdown goes through ACPI); on the user's laptop
not yet.

**date 0.002**: **the time from the network**. `date -ntp` (also `-npt`) asks
`pool.ntp.org`, or `-ntp:SERVER[:PORT]`, and sets the clock on the tick of
the next second, with half the network delay added (SNTP). NTP gives UTC and
the machine's clock keeps local time: the zone in `/boot/fuso.cfg` converts
it (`date -fuso:+1,ue`: hours from UTC, minutes allowed, `ue` for European
summer time, decided on the UTC instant); without the file Italy is assumed
and the command says so. An answer that is not ours, a Kiss-o'-Death or a
silent server leave the clock alone; without root it shows how far ahead or
behind the clock is. It talks to the stack over IPC like `host` (BSD sockets
are not in the shared libc), so it stays on the floppy (28 KB free).
`tools/prova_ntp.sh`: summer, winter, both changes of the hour, Kiss-o'-Death,
a silent server and the real `pool.ntp.org`, at 32 and 64 bit.

**netupdate 0.032**: **the sources from the repository**. Next to
`.../netinst` and `.../netinst_64` there is `.../netinst_src`
(`make netinst-src`): one archive for 32 and 64 bit with the files git
tracks - what goes to GitHub. `netupdate -install:sourcecode` downloads it
(in 16 MB pieces when needed, resuming), checks its hash and opens it in
`/src/exos-<version>`; a new version goes into a new directory, and the
sources stay out of the register (`-check` leaves them alone). `tar` (0.004)
and Archivi restore the execute bit written in the archive: scripts came out
0644. `tools/prova_sorgenti.sh` (QEMU, a disk installed from the CD).

**0.265, ext4.mod 0.004**: **ext4 is written** - the volumes today's
`mke2fs -t ext4` makes, with metadata_csum, 64bit, flex_bg, orphan_file and
groups left uninitialised. The extent tree is written
(`kernel/moduli/ext4/estesi.c`: an extent grows, a new one goes in, a full
node splits, a full root moves into a block of its own; truncation cuts the
tree), and every piece of metadata carries its checksum as Linux computes it
(`somme.c`: crc32c of the superblock, descriptors, bitmaps, inodes, extent
blocks, the 12-byte tail of every directory block, attribute blocks; crc16
descriptors for uninit_bg). A group never initialised gets its bitmap built
at the first allocation, as Linux does; a new inode is written whole, with
its extra space and creation time; an indexed directory on metadata_csum
gets a checksum tail in every block before its index is dropped. The journal
writes and replays Linux's checksums too (CSUM_V3). The two known limits of
ext3 are gone: a long `write()` is cut into pieces of 256 KB, each inside a
transaction of its own, and the superblock goes through the journal.
`tools/prova_ext4.sh` (QEMU, with `tools/prove/estesi.c`): four ext4 made by
Linux - 4 KB blocks with a hashed directory, 1 KB blocks without flex_bg
where EX-OS fills uninitialised groups and grows an extent tree two levels
deep, a checksummed journal left by debugfs with a revoke, uninit_bg without
64bit - written, cut and grown back, every file identical byte for byte,
`e2fsck` clean, EX-OS's checksummed transaction replayed by `e2fsck`, a
crash repaired. Still read-only: inline data, bigalloc, quota, encryption,
casefold, a volume with orphans pending.

**0.265**: **ext3 is written**, with its journal (`/dev/ext4.mod` 0.003,
`kernel/moduli/ext4/giornale.c`). Every change to the metadata - bitmaps,
descriptors, inodes, pointer blocks, directories - goes to the journal first
as a whole transaction and only then to its place; file data reaches the
disk before the commit ("ordered" mode, ext3's default). The journal format
is Linux's (jbd2): a volume EX-OS was writing when the power went is put
right by Linux, its `e2fsck` or EX-OS itself. At mount EX-OS replays a
journal left by Linux (scan, revoke, replay, as jbd2). `mount -r` writes
nothing, not even to replay the journal. A transaction is committed at the
end of every operation, at a file's close, at `sync`, at unmount and after 5
seconds. The kernel has three new hooks in the ext2 interface (ABI 4).
Found while testing: the 64-bit CD had no `umount`. `tools/prova_ext3.sh`
(QEMU, 32 and 64 bit, 27 of 27): a journal written by debugfs with a revoke
replayed as Linux does, `mount -r` leaving the partition identical byte for
byte, ext3 with 4 KB and 1 KB blocks written (identical files, `e2fsck`
clean, needs_recovery off), EX-OS's last transaction replayed by `e2fsck`,
and a QEMU stopped without `umount` that `e2fsck` repairs without losing
what had been written. Next: ext4 writing (extents, metadata checksums) - done, see above.

**0.264**: a faster disk, without losing safety. **A disk lock**: the
controller, its DMA buffer and the sector cache had none, and swap and the
raw-sector calls run outside the VFS lock, so two commands could interleave
on one channel or the cache take back old data; now one at a time, CD
included, and a failed write drops the sectors it touched from the cache.
**Read-ahead** of 128 KB, only when a read goes on from where the last one
ended and never past the partition; every write throws it away. Word copies
instead of byte copies. In the module (ext4.mod 0.002) a file being
appended to keeps 128 KB of blocks reserved between write()s and the
superblock counters are written at close; the blocks left over are freed at
close, sync, unmount and before any other operation. In QEMU, ext2 with
4 KB blocks, 32 bit: writing in 4 KB pieces from 5.5 to 14.4 MB/s, in 64 KB
pieces from 19.7 to 30.9; reading in 4 KB pieces from 20.6 to 63.0, in 1 MB
pieces from 72.8 to 172.5 (up to 218 at 64 bit).
`tools/prova_disco_veloce.sh` checks every byte read back, also after an
in-place rewrite, and runs `e2fsck` at the end.

**0.263**: **kernel modules**, and the first one is ext2 writing. Reading
ext2/ext3/ext4 stays in the kernel; the code that writes left the kernel and
is `/dev/ext4.mod`, which the kernel loads at boot right after mounting the
root (`kernel/loader/modulo.c`, the same loader at 32 and 64 bit: an ELF
shared object with RELATIVE relocations only). The build appends to the
module the kernel version it is made for, the architecture and its SHA-256;
if they do not match the module is **not** loaded, the screen and the log
say what was expected, and ext2 volumes stay **read-only** (ext3/ext4 are
anyway, for now). `modulo` lists the loaded modules; `modulo carica -forza
FILE` loads one anyway, after the warning and a typed `SI` - never
automatically. An ext2 root starts read-only and becomes writable when the
module registers. `install` copies the module with the kernel, and the
floppy, the CDs and netinst carry it. ext2 takes four mounts instead of two.
`tools/prova_modulo.sh` (QEMU, 32 and 64 bit): EX-OS formats and writes an
ext2 (mkdir, cp, mv, rm, rmdir, indexed directories; identical files,
`e2fsck` clean), a damaged module or one made for another version is
refused, an ext4 made by Linux is read and not written.
`tools/prova_modulo_disco.sh`: a disk installed from the floppy boots, its
root becomes writable and is written. **netupdate 0.031: system
components.** An `elenco.txt` line can end with the column `essenziale`;
`tools/mknetinst.sh` writes it for the files matching
`tools/netinst/essenziali.txt` (kernel, second stage, every `dev/*.mod`,
libc, sh, login, mount, install, netupdate). A machine missing one gets it
with the changed files, `-check -yes` included, instead of it being an
optional file "never installed". Older netupdates ignore the column: **when
updating to 0.263 answer "si" to "Installo anche quelli?" or use
`-yesall`**, otherwise the new kernel arrives without its module and the
disk boots read-only (the kernel says so on screen, with the remedy). Next:
the ext3 journal, then ext4 writing (extents, metadata checksums) in the
module.

**0.260**: `rename()` moves files and directories between directories on
ext2 (until now it renamed in place only and answered ENOSYS otherwise). A
directory that moves gets its `..` rewritten and its link moves from the old
parent to the new one; moving a directory into itself is refused. Found by
**libgit2 on 64-bit EX-OS**: it writes every object to a temporary file and
moves it into `.git/objects/xx/`. libgit2 1.9.6 (with HTTPS through OpenSSL)
now builds for EX-OS (`tools/libgit2-exos/costruisci.sh`) and, in QEMU, a
test program does init, commit, status, log, branch, diff and a local clone
(16 of 16); the repository it writes passes the real `git fsck`. Also new:
the shared FTP/FTPS library and `/bin/ftp` as a full ftp shell with FTPS
(see the network manual), and a "loading, please wait" box with an hourglass
for every big program. FAT still renames in place only.

**0.259**: stat no longer reports every file on ext2 and FAT as read-only, a
leftover from when those filesystems were not written: programs saw files
without write bits. The volume, the inode's permissions or the FAT attribute
now decide. Found by the news of the day: **the Rust compiler runs on 64-bit
EX-OS**. `rustc` is cross-built for the host `x86_64-unknown-exos` (one
static program of 131 MB, LLVM 23 inside) and, in QEMU, `rustc -O ciao.rs`
compiles a program and links it with the native gcc, and the program runs
(collections, threads, a caught panic, a child process). Not yet: cargo, the
tools CD, a real machine.

**0.258**: `/dev/null` exists. On EX-OS `/dev` is the drivers' directory on
the disk, so a program opening `/dev/null` got an error - read-only, from a
CD. It is now a device of the kernel: reading gives end of file, writing
takes everything and keeps nothing, at 32 and at 64 bit; `cmd > /dev/null`
works in a redirection too. Ported software opens it all the time: Rust's
`std::process` does for every stream it is told to ignore. That is the other
news, for the native Rust coming at 64 bit: `std::process::Command` works on
EX-OS (a backend on `spawn_ex`: PATH lookup, pipes, environment, working
directory, exit codes - 7 of 7 in QEMU), and so does `panic=unwind`
(`catch_unwind`, `Drop` while unwinding, threads - 8 of 8). Also `abort()`
now ends a program at once (`_exit`): it ran the exit handlers, and one
waiting on a lock the aborting code held hung the program for ever.

**0.257** (64 bit only): the RAM above 4 GB is used. Until now the 64-bit
kernel used only the RAM below 4 GB - on an 8 GB machine half stayed idle, and
even a 4 GB machine has a part above 4 GB, moved there by the BIOS to make room
for the devices. It is now a second pool of the physical memory manager,
given to programs' own pages (heap, stack, mmap) as they touch them; page
tables, kernel stacks and DMA buffers keep the RAM below 4 GB, which 32-bit
devices can reach. `mem` counts it. Tested in QEMU with 6 GB: one process takes
3.5 GB, writes and reads back every page with no error, three times in a row,
and the memory in use afterwards is what it was before; with 2 GB nothing
changes. The 32-bit kernel is identical.

**0.256** (64 bit only): a process gets almost 4 GB of address space instead
of 3. The framebuffer, the one thing of the kernel that sat between 3 and 4 GB
of every process, is seen by the kernel from above 4 GB, through an alias all
address spaces share; the user space ends just below 4 GB, and the heap's
ceiling goes from about 2681 to 3705 MB. Memory is still taken only when it
is touched. Tested in QEMU: Exilla in its window, the libc self-test, C++
threads. The 32-bit kernel is identical, instruction by instruction. Next:
processes above 4 GB and RAM above 4 GB.

**64-bit tools, 16 October 2026**: **FreeBASIC** 1.10.1 runs on 64-bit
EX-OS. FreeBASIC has no assembly backend of its own at 64 bit, so it goes
through C (`-gen gcc`): `tools/freebasic-exos/prepara-fb64.sh` builds the
runtime (libfb) with `x86_64-exos-gcc` and the native `fbc`, and `make
tools64` puts them on `dist/exos64-tools.iso` (fb group). In QEMU the native
`fbc` compiles a program inside EX-OS 64-bit with the native gcc, as and ld,
and the program runs (`tools/freebasic-exos/prova-fb64-nativo.sh`).
**Exilla (Firefox) runs at 64 bit**: built for `x86_64-unknown-exos`
(`EXOS_ARCH=x86_64 tools/exilla/gecko-costruisci.sh`), it opens its window in
ExWin inside EX-OS 64-bit, draws the test page and opens a second one typed
in the address bar (`EXOS_ARCH=x86_64 tools/exilla/prova-finestra.sh`, QEMU
with 2 GB); `make netinst64` now carries the `exilla` package (306 MB). On the
way: libstdc++ with threads for the 64-bit toolchain, the 64-bit format
macros of `<inttypes.h>`, the six `<fenv.h>` functions openlibm's amd64 build
does not export, and SpiderMonkey's JIT reservation cut to 140 MB - a 64-bit
process on EX-OS still has the 32-bit address space (about 2.6 GB), to be
widened. gcc and g++ were also tried on the real laptop. Not yet tried on a
real machine: Exilla and FreeBASIC.

**0.255**: the status bar (`extaskbar`, above): the last line of the screen
can belong to it, the consoles have the alternate screen (`ESC[?1049h/l`),
and what background processes write goes to a ring that `extaskbar`
collects (`SYS_EXTASKBAR`), only while it runs. The shell marks `&` jobs
(`SPAWN_F_SOTTOFONDO`) and now always clears the spawn flags it used to leave
as they were on the stack.

**0.254**: on the laptop `reboot` then stopped for good inside the keyboard
controller step. The chipset's reset port 0xCF9 is now tried before it (a
write to a port nobody decodes does nothing), and each of the ten 8042
attempts prints its line. Not yet tried on the laptop.

**0.253**: `reboot` on the laptop stopped right after "Sincronizzazione
filesystem... ok". Its firmware declares no ACPI reset register, and the waits
between the other methods were counted loops that lasted under a millisecond
on its CPU. Each method now waits about half a second, measured on the I/O
port 0x80 as Linux does; the keyboard controller gets its reset command ten
times; and each method prints its own line on the screen
("Riavvio: controller della tastiera (8042)", "Riavvio: porta 0xCF9",
"Riavvio: triple fault"), so the last line shown says which one the machine
did not take. Not yet tried on the laptop.

**0.245**: SATA disks behind an AHCI controller (`kernel/block/ahci.c`).
Controllers that declare themselves AHCI are always used, and their disks take
a place among `hd0`..`hd3` like the others: in QEMU the system installs to,
reads, writes and boots from an AHCI disk. For controllers that offer the same
disk twice (IDE registers and AHCI, like the NVIDIA MCP73 of the test PC,
where only PIO works through the IDE registers) the system starts as always
and moves to AHCI afterwards: with `ahci = 1` in `/boot/kernel.cfg` at every
boot, or with the command `ahci` for the current session only, which is the
way to try it without risk. `ahci -t` is the same pass with a reset of the
controller, as Linux does on the MCP77: it reads samples through IDE, passes,
reads them again through AHCI and, if anything differs, reboots after 60
seconds without writing to the disk; it also takes the DVD reader away until
the next boot. After it `ahci -s` writes `ahci = 2`, the same pass at every
boot. On the MCP77 laptop reads went from 5.3 to 76 MB/s, but under load the disk
once stopped answering (see `in_lavorazione.txt`, voce 58): not proven safe yet.

Since kernel 0.299 `guardia SECONDS` resets the machine unless it is called
again in time, and `guardia 0` turns it off: it is for remote tests on a real
PC that a fault would leave stuck. The countdown lives in the kernel, so it
fires with the disk stopped or the network down too; the reset does no sync.
From the PC `tools/guardia-pc.py` keeps it alive, one call every 30 seconds
against a margin of 120. At half and three quarters of the time the kernel logs
who holds the locks. `dmesg -segui` shows warnings and errors as they come, and
`tools/prova_carico_pc.py` runs a remote disk load test in a single session,
because telnetd serves one at a time. Since 0.300 AHCI timeouts last as long as
stated: they used to run out after about 2 seconds, and fired falsely when the
thread stopped. netupdate 0.039 rewrites a fragmented kernel and no longer says
to reboot when the boot map was not updated.

**Since kernel 0.319 every processor works by default** (voce 92). `smp = 2`
is the default in `/boot/kernel.cfg`; it is a mode, not a number: 2 means
every processor the ACPI MADT or the MP table lists runs programs (1 parks the
extra ones, 0 only counts them, for a board that does not boot with them).
There is no ceiling of 16 any more: the list of processors and every
per-processor table (GDT, TSS, current and idle process, kernel lock, CR0.WP,
frequency) are as long as what the machine has; the limit left is the xAPIC's
8-bit numbering (x2APIC is not driven yet). `sysconf(_SC_NPROCESSORS_ONLN)`
gives the true number (it said 1), so Firefox, FFmpeg and Mesa size their
threads on it; `/proc/stat` has the real time of each processor (it split the
total evenly) and `/proc/PID/status` a `Cpus_allowed_list` line.
**Affinity and reserved processors**: `sched_setaffinity`,
`pthread_setaffinity_np` and `sched_getcpu` are real (`SYS_CPU_AFFINITA`
265), and the new `cpu` command lists the processors, sends a process's
threads to given ones (`cpu -p PID 1,3-5`), starts a command already there
(`cpu -x 2-3 CMD`), and reserves a processor to one process (`cpu -r PID 2`,
`cpu -R 2 CMD`): its threads go there and nobody else runs on it, until it
ends or `cpu -l 2`. Processor 0 cannot be reserved. **A scheduler fault fixed
on the way**: a process working alone handed the processor to the idle task for
one tick at the end of every quantum (1 in 5, 20% of any program that
computes); found because two programs on one processor did 1.25 times the
work of one alone. Tested in QEMU at 32 and 64 bit: `tools/prova_tutti_core.sh`
(1, 2, 4 processors under KVM: 2.00 and 3.8 times the work of one; 20
processors found and at work, emulated) 22/22, `tools/prova_affinita.sh` 9/9
(two programs pinned to one processor 1.00, to two 2.00, three beside a
reserved one 2.93). **Kernel 0.320**: shutdown, reboot and halt move to processor 0
first; asked from a program on processor 1 the 0.319 stopped at
"Spegnimento fra 3..." (the countdown waited for the clock, which rises on
processor 0, while holding the kernel's lock); the clock waits of the CD
drives and the floppy let the lock go by. **Kernel 0.321: the threads of one
program run on several processors at once** (voce 92 b, SMP stage 4; up to
0.320 they took turns on one processor, so Firefox, with its 60-70 threads,
used one core at a time). When the kernel takes a page away from a program
(munmap, the swap), makes it read-only (mprotect, fork's copy-on-write) or
moves it (copy-on-write), every other processor running that program is told
to drop its TLB, and the kernel waits for all of them (TLB shootdown: a
message on vector 0xF2 that does not take the kernel's lock). munmap, sbrk and
a thread's stack send one message per call, not one per page. A fault on a
page another thread has just brought in goes back at once instead of killing
the thread; `exec` from a thread ends its brothers and frees the old memory
when the last one has left it. The C programs built with the cross gcc need
the sysroots' libc of 8 November or later to see the true number of
processors (`sysconf` said 1 in the older one): programs linked statically
before, Firefox among them, must be linked again. `smpprova -fili N`
measures N threads of one program. Tested in QEMU:
`tools/prova-fili-core/prova-fili-core.sh` 8/8 at 32 and 64 bit (2 threads
1.97 times the work of one, 4 on 4 processors 3.16; the other thread stops on
its fault as soon as munmap or mprotect returns; a child forked while a
thread writes sees the page as it was) - and with a kernel built without the
shootdown the same test says NO three times.

Since kernel 0.297 the 64-bit system wakes the extra processors too, as the
32-bit one does: `smpprova -accendi` puts them to work until the next reboot,
`smp = 2` in `/boot/kernel.cfg` at every boot. Programs run on all of them;
inside the kernel there is one processor at a time; since 0.321 the threads
of one program run together too. `ahci -m FILE` measures how many KB/s a file is
read at. Confirmed on the real PC on 9 October, at the third attempt: the
disk moves to AHCI and the 35 MB of `cc1` are read at 59 MB/s, against about
3.4 MB/s in PIO. On that controller the port had never been used in AHCI:
switching AHCI mode on drops the link, which has to be brought back before
the disk's signature can be read. `install` (0.006) recognises such a machine
by itself: it moves the disk to AHCI before copying and, only if that works,
writes `ahci = 1` in the installed `kernel.cfg`; on an installed system
`ahci -s` does it.

**0.244**: on leaving the graphics the text console was full of lines that
looked like faults and were not. The log of graphical programs (`SYS_LOG`:
every style sheet the browser loads, every error of a page's script) was also
written on the console; it now goes to the serial line and to the log `dmesg`
reads, and nowhere else. The browser uses that log instead of `printf`. And
"ENTROPIA: jitter...", which appears at the first encrypted connection on a
machine without RDRAND, is no longer written as `[ERROR]`: it is the news that
the random bits were gathered.

**0.243**: the graphics console was slow on the real PC. Measured there with
`fbprova` (1024x768 at 32 bit, NVIDIA integrated video): reading from video
memory runs at 28 MB/s against 10,000 for RAM, and scrolling copied the screen
onto itself, that is, read all of it back at every line: a tenth of a second
per line. Scrolling now never reads from the screen: the cells are in RAM, and
only those that show a different character after the scroll are redrawn. Also
`mtrr.c`, written in September and never connected for lack of a machine to
try it on, is now called: it declares video memory write-combining, and gives
up saying so (`dmesg MTRR`) if anything is off. In QEMU the console scrolls
correctly. Measured on the PC after the update: `MTRR: framebuffer 0xd0000000,
4096 KB combinabili in scrittura`, and writing to the screen goes from 160-345
to 1335 MB/s.

**0.242**: the kernel keeps a log of its own messages, and the new command
`dmesg` reads it: the boot (the first 32 KB, never overwritten) and the latest
messages, including those the log level hides on screen. `dmesg WORD` shows
only the lines containing it. It comes from the trials on the real PC driven
over telnet: without a serial cable there was no way to know what the kernel
had said at power-on. From the same trials: the shell exits when its input
ends (every dropped telnet session left a live shell holding its
pseudo-terminal, and at the fifth nobody could log in); `nforce.drv` 0.006
transmits (a transmit ring of 8 descriptors was too small for the card: it
received and sent nothing; with 64 it works, tried on the PC with
`nforce.drv -p`); `audio -i` recognises the MCP73 audio (ALC888 codec);
`netupdate -check -yes` and `-yesall` update without asking.

**0.241**: the system installed, but on rebooting from the disk the kernel
could not find its root. It looked on `hd0` only, and a SATA disk on a native
channel sits in the first free place, which may be another. It now looks at
every disk: first each one's active partition, then the first ext2 it finds.
To update a disk already installed: boot from the new stick,
`mount hdNp1 /disk`, `install -a /disk`.

### A real motherboard: SATA disk and NVIDIA network (kernel 0.239)

**to be tested on hardware: QEMU emulates neither** — from the report of a PC
with an NVIDIA MCP73 chipset and a Core 2 Quad (`sonda/mb_oem_000`):
- **The SATA disk.** The controller shows up as IDE in *native mode*: its
  registers are not at 0x1F0 and 0x170 but where its BARs say. The kernel
  looked only at the two usual places and did not see the disk. It now looks
  on the PCI bus for IDE, RAID and SATA controllers that are not in AHCI and
  adds their channels; the disks found take the free places among `hd0` to
  `hd3`, so `disk`, `fdisk` and the installer do not change. The usual disks
  stay where they were (this one tried in QEMU).
- **The network.** `/dev/nforce.drv`, for the Ethernet built into NVIDIA
  nForce chipsets (MCP67, 73, 77, 79): registers in memory, no interrupt, the
  PHY read as it is. `netdetect` recognises it and starts it. `-d` prints the
  registers, the PHY and the counters of the running driver, `-v` tells the
  power-up step by step. Confirmed on the real board (MCP73) with 0.008 on 8 October:
  at boot `dhcp` gets the address and the gateway answers ping in 20 ms, at
  1000 Mbit. There were three faults: the transmit ring of 8 descriptors, too
  small for the card (it did not transmit); the address filter, which let only
  broadcasts through (the DHCP offer was dropped); and short waits done with
  `usleep`, which stretched the power-up beyond ten seconds. `-p` tries it
  alone.
- **HD Audio on nVidia bridges** (`hdaudio.drv` 0.003). On the test PC the
  codec answered, the self-test passed and nothing was heard. Two things:
  nVidia controllers read the stream memory without looking at the CPU
  caches until three bits are set in their PCI configuration (0x4C, 0x4D,
  0x4E), and the driver now sets them; and the volume set only the left
  channel, leaving the right as it is born (muted, on an ALC888).
  `hdaudio.drv -d 1` prints the whole codec - connections, amplifiers, pins,
  streams - without writing anything, even while the driver runs; `-c 0`
  leaves the nVidia bits alone, `-k N` picks the codec. On the real PC it is
  now heard (10 October). Random gaps and distorted pieces remained: at the
  end-of-half interrupt the position counter can still read just before the
  boundary, and the wrong half was refilled. 0.004 moves the boundary by a
  quarter of a half and writes the counts of each playback to the kernel log
  (`dmesg hdaudio`). To be listened to again.
- **Groundwork for 64 bit.** EX-OS will have a 64-bit version beside the
  32-bit one, from the same source: `make ARCH=x86_64` (default `i386`, and
  unless said nothing changes). Two repositories, `netinst` and `netinst_64`:
  `netupdate` 0.029 knows which machine it was built for, `versione.txt` says
  `arch`, and a repository for the other architecture is refused. The staged
  plan is in `in_lavorazione.txt` (`@EXOS-64`). The first stage is done: the
  kernel reaches 64-bit mode in QEMU (`tools/prova_kernel64.sh`), with the
  same loader; on a Pentium it says there are no 64 bits. It is not a system
  yet. Second stage half done: all the shared kernel COMPILES at 64 bit (the
  six processor files remain to be rewritten), and at every step the 32-bit
  kernel came out identical, instruction by instruction.
  Third stage done: the SHARED kernel runs at 64 bit. `make ARCH=x86_64
  kernel64` compiles the same sources as the 32-bit kernel, minus the three
  processor files and plus those in `kernel/arch/x86_64` (entry, segments,
  interrupts, context switch, four-level tables), and in QEMU it goes
  through the whole start-up - memory, scheduler, system calls, the CD -
  and starts in ring 3 an ELF64 program that makes system calls, sleeps and
  comes back. The 32-bit kernel stayed identical at every step. It is not a
  system yet: the libc and the programs are missing (stage 4), and at 64 bit
  there are no signals, shared libraries or drivers.
  The 64-bit version has no floppies: `make iso64` makes the CD
  (`dist/exos64.iso`), `make netinst64` the repository (`dist/netinst_64`),
  `sudo make usb64 DISPOSITIVO=/dev/sdX` the USB key, `make tools64` will
  make the tools CD once they exist.
- **The 64-bit USB key boots with its root in RAM (`make usb64`, and `make usb` at 32 bit).** A key
  made like an installed disk booted and stayed without a shell: the kernel
  reads ATA disks, not USB, and the USB drivers are on the disk it cannot
  read. Now the key starts with a 1.44 MB floppy image (the shape of `make
  installa`): the BIOS boots it as a floppy, the loader copies it into RAM
  and the kernel mounts its root there; the USB drivers start from it, and
  `fetta.drv` mounts on `/sistema` the rest of the key, where the whole
  system is. The RAM root is lost at power-off. Tried in QEMU, not yet on a
  real machine.
- **64-bit EX-OS: a command-line system.** Fourth stage done. The libc, the
  shell, 78 commands of `/bin` and 20 drivers are the SAME sources as the
  32-bit version compiled at 64 (`make utente64`), with the libc linked into
  each program. The CD `dist/exos64.iso` boots to the shell with keyboard,
  mouse, PCI and network (e1000, DHCP, ping), installs to disk with
  `install` and boots from the disk. Signals, threads and `__thread`
  variables work at 64 bit. Tried in QEMU, not yet on a real machine. The
  ExWin desktop is there too (19 programs and the graphic server, linked
  statically) and so is the `netinst_64` repository. Since 0.249 the
  **compilers** too: `tools/gcc-exos/cross64.sh` builds the `x86_64-exos`
  cross toolchain (GCC 17, binutils 2.44, C and C++ with libstdc++) and the
  native gcc, g++, binutils, GNU make and NASM that run on EX-OS; `make
  tools64` puts them on `dist/exos64-tools.iso`, `toolinst` installs them,
  and `make netinst64` publishes them. In QEMU a C and a C++ program (with
  exceptions) are compiled and run inside EX-OS 64-bit. Sound, FreeBASIC,
  OpenSSL and Exilla are still missing at 64 bit. The
  32-bit version stayed identical, instruction by instruction, in the kernel
  and in all 181 sources of libc, libraries and commands.
- **Desktop: memory and start-up (pm 0.020).** A menu entry may say how much
  memory it wants (`ram=2048`) and the menu warns before starting it on a
  machine with less. The Settings window switches the clock and the volume
  icon of the bar on and off.
- **Exilla: the scroll bar can be seen.** It was Android's, a thin thumb,
  white on white: it is the classic one now, with arrows and ExWin's greys.
  The `exilla` package has to be published again.
- **The network is no longer held back by TCP's window (`ip.drv` 0.009).**
  The receive window was 4 KB: towards a server 50 ms away that meant 80 KB/s
  at most, whatever the line. It is 63 KB now, taken only by the connections
  that use it, and segments arriving out of order are kept instead of being
  asked again. `ip.drv -w KB` makes it smaller. In QEMU a megabyte goes from
  3.6 to 0.3 seconds; the gain on a real line is to be measured on the PC
  with `scarica -i`.
- **ExEditor writes documents (0.008).** In RTF documents: a tool bar of
  icons, a ruler in centimetres with indents and tab stops to drag, Formato >
  Paragrafo and Pagina, line spacing, page layout with page breaks; the
  Visualizza menu switches bar, ruler and page layout. The `exrtf` library
  now has indents, tab stops, spacing and paper size, read and written in
  RTF. **Word 6 and 95** `.doc` files open (`lib/exrtf/doc95.c`) and are
  saved as RTF; Word 97 and later do not. The .doc reader was tried on
  hand-written files and compared with LibreOffice, not on files from a real
  Word. The second scroll bar in text mode is gone.
- **Two small items from the queue.** The desktop background accepts a
  photograph: the toolkit (0.017) read only the first 256 KB of the file, now
  the whole of it. In the file manager (0.013) the splitter between tree and
  list can be dragged, and the width is kept in
  `$HOME/.exwin/config/filemgr.cfg`.
- **Exilla is a repository package (stage 8 of the Firefox port).**
  `netupdate -install:exilla` downloads it to `/exwin/app/exilla` (browser,
  launcher, icons: 290 MB) and writes its entry in the desktop menu, in the
  new **Internet** category where EXBrowser has moved too;
  `netupdate -remove:exilla` removes files and entry. `netupdate` 0.027: a
  package may have its own file list (`pacchetti/exilla.txt`, so that
  `-check -yesall` does not bring Exilla to machines that never asked for
  it), files above 16 MB are published and downloaded in parts
  (`firefox.p000`...) resuming from the missing part, and the catalogue key
  `menu` becomes a line of `applicazioni.txt`. The program manager (0.019)
  holds 48 entries instead of 16: the base system already filled them all.
  `firefox` is published
  stripped: 192 MB instead of 274. `make netinst` puts it in the repository
  by itself; `exagonx/pubblica.sh` and `verifica.sh` know lists and parts.
  Tried in QEMU against the repository built here (not the published one);
  still to be tried on a real machine.
- **EXBrowser: Yahoo's results in place, and Bing's open.** On Yahoo the
  results column ended up to the right of the tabs and past the edge: a block
  that does not fit beside a float now drops below it, and a `min-width`
  wider than the window no longer pushes the block out of the page (there is
  no horizontal scrolling: what goes out is not seen). On Bing every result
  led to a blank page: the click-through page redirects from `<body
  onload="...">`, and the `load` event started at the document root and
  never met the body.
- **EXBrowser: CSS background images.** Logos and icons of most sites are
  the BACKGROUND of an empty element: `background-image` (also inside
  `background`), with `background-repeat`, `-position`, `-size`, and `height`
  / `min-height` giving the box a size. The address is relative to the sheet
  it is written in, not to the page. Yahoo's logo now shows. Two faults found
  and fixed: a `;` inside `url(data:...;base64,...)` ended the declaration
  half-way (Yahoo's sheet goes from 2652 to 2856 rules read), and a page with
  a timer lost ALL its style if the recalculation fired while an image was
  downloading. Not yet: `data:` images, backgrounds on inline elements.
- **EXBrowser: lazy-loaded thumbnails.** Image-heavy pages no longer put the
  address in `src`: they keep it in `data-src` and a script moves it when the
  picture scrolls into view. That script does not run here, and
  wallpapers.com and pixels.com had no thumbnails. `<img>` now takes its
  address from `data-src` (or `srcset`) when `src` is missing or a
  placeholder. A button made of one picture is named after its `alt`. Still
  open: CSS backgrounds (Yahoo's logo), the WebP format, and the layout of
  those pages.
- **The full-screen button.** In the title bar, between "minimise" and
  "close", a third button makes the window fill the screen down to the
  taskbar, and pressed again puts it back; Alt+F10 does the same. The window
  server (`wserver` 0.011) does it for every resizable window, so no program
  was touched. Trying it showed that the file manager, at every resize, moved
  its two lists up over their headers: fixed.
- **Downloaded tools are found without help.** `netupdate -check -yesall`
  installs the whole system, compilers included, in `/exos`; but it left the
  `PATH` of `/boot/kernel.cfg` alone, and `gcc` was "command not found" on a
  machine that had it on disk. Now at the end of `-check`, if the tools are
  there and the PATH does not name them, `toolinst -c` writes it (new:
  configuration only, with a copy in `kernel.cfg.bak`); it takes effect at
  the next boot. And exide (0.025) picks the compiler root by itself: `/exos`
  if it is there, otherwise the CD - it was fixed at `/cdrom/exos`.
- **The volume panel.** In the taskbar, next to the clock, there is a speaker
  icon: a click opens `exvolume`, one row for every output and every input of
  the card and two sliders per row, left and right channel (a checkbox links
  them). The driver declares the entries: `hdaudio.drv` 0.005 gives each
  output jack a converter of its own, so the volumes really are one per jack,
  and lists as inputs the microphone, line and CD that the codec can mix into
  the outputs (they start at zero). Cards with only a master volume have one
  entry, "Volume". Choices live in `$HOME/.exwin/config/volume.cfg` and the
  desktop restores them at every start (`exvolume -applica`); from a console,
  `audio -mix`. In QEMU: one output, left at 0 and right at 78, and the left
  channel of the recording is silent. The five outputs and the inputs of the
  ALC888 are to be tried on the PC.
- **The audio player and the sound library.** `explayer` (Start > Lettore
  audio, or a double click on a `.wav` or `.mp3` in the file manager) plays a
  list of tracks: previous, play, pause, stop, next; a bar that shows where
  the track is and can be dragged to any point; three counters, time elapsed,
  time left and the total of the list. The sound is not inside the player:
  `exsuono.so` (`lib/exsuono`) does it, and any program can open it. For a
  sound after an action `exsuono_avvia_file("/exwin/sound/x.wav")` is enough,
  and it returns at once; the program manager uses it for the intro when the
  desktop starts; the checkbox "Suono all'avvio della scrivania" in Start >
  Impostazioni turns it off. Found on the
  way: `audio` did not return to the prompt at the end of a track (it waited
  for the card to consume the last bytes, less than one block, which a card
  never takes) and kept the card busy; and in the toolkit a focused button
  could not be pressed from the keyboard in any program: the space bar now
  does it (exwin.so 0.016). Tried in QEMU with the emulated HD Audio, from
  the keyboard. On the real PC it is still to be listened to.
- **MP3.** `audio track.mp3` plays an MP3 and `audio -w track.mp3 track.wav`
  converts it to WAV. The decoder is `minimp3`, public domain (CC0), copied
  as it is into `lib/terze/minimp3` and built without SIMD for the Pentium
  MMX. Tried in QEMU by converting the intro in `exwin/sound`: the samples
  produced inside EX-OS differ by at most 1 in 32768 from those of the same
  decoder on the development PC, and match the original WAV to 99.97%.
  Listening on the real PC is still to be done.
- **The start menu from the keyboard.** The Windows key or Ctrl+Esc open it
  from any window; up and down arrows to choose, right (or Enter, or space) to
  enter a category, left or Esc to leave it, Enter or space to start, Esc to
  close. The Windows key is a new key for the keyboard driver too
  (`KBD_K_WIN`). Tried in QEMU.
- **USB mouse and keyboard on OHCI controllers.** `ohci.drv` (0.003) knew only
  sticks, and a USB mouse is low speed: on a board whose slow controller is an
  OHCI (NVIDIA, SiS, ALi) it did not exist, and the system looked for a PS/2
  mouse. A "boot" HID is now recognised and served through the `mouse` service
  (or by handing keys to `kbd`), as `uhci.drv` already did. Tried in QEMU with
  a USB mouse on OHCI: `mouse -n 6` reads the movements and the button; the
  USB keyboard and the real PC are still to be tried.
- **Two network cards, one cable.** `netdetect -c` picks the card that has the
  cable: it asks the drivers with the new option `-l` (`nforce.drv`,
  `rtl8169.drv`). Before, "the added one" won, cable or not.
- **A second network card and the support medium.** `/dev/rtl8169.drv` (0.001)
  for the Realtek PCI Gigabit cards RTL8169/8169S/8110S/8169SB/8169SC, written
  for the card put in the test PC; QEMU does not emulate it. Confirmed on the
  machine (RTL8169SC): `dhcp` gets the address and `ping 8.8.8.8` answers in
  60 ms. `make support` creates `dist/support` and
  `dist/support.iso`: drivers and programs to add to an installed system, from
  a stick or a CD, with the program `aggiungi`, which copies them without
  asking and starts sticks and network in `/boot/autoexec.sh`. With two cards
  `netdetect -c` picks the added one; `netdetect -c <word>` chooses by hand.
  Tried in QEMU from the installed disk: after `aggiungi` and a reboot the
  network starts by itself (e1000).
- **USB sticks** were not recognised on that PC: the EHCI driver misread
  64-bit controllers, and the drivers now say in full what fails. With the
  fix the report came back on a stick.
- **The probe floppy** (`make sonda`) no longer built: it is back to reading
  the hardware only, with 470 KB free for reports.

### More processors, stages 1 and 2 (kernel 0.237 and 0.238)

**tested on hardware** (Core 2 Quad Q9650, NVIDIA MCP73 chipset: four
processors found from ACPI, the three extra ones with their timer running and
messages arriving) **and in QEMU** (`tools/prova_smp.sh`: 1, 2 and 4
processors, with ACPI and with the MP table alone, also under KVM; emulated
two-socket Pentium and Pentium II) — at boot the kernel looks for the
processors in the ACPI tables (MADT) and, where there are none, in the
MultiProcessor table of the two-processor boards of the Nineties (dual
Pentium, dual Pentium Pro). It wakes the extra ones one at a time and takes
them to protected mode with the kernel's tables (**0.237**). Each one switches
its own local APIC on, has a timer of its own at 100 beats a second and
receives messages from the other processors; the APIC sits in a page every
process sees, so a system call can send one too (**0.238**).

`hwinfo` has a PROCESSORI section: how many, from which table, and for each
one whether it is in use, waiting or silent; for the waiting ones it measures
over half a second whether the timer runs and whether messages arrive.

! **EX-OS still works with one processor.** The others are ready but run no
processes: that is stage 3, one lock on the kernel, and it is where the gain
comes. Then finer locks. Devices stay on the PIC towards the boot processor:
the I/O APIC is for spreading them, and comes later. On a machine with one
processor — and on anything without a local APIC — nothing changes: the
kernel reads a few bytes of the BIOS and goes on.

Should boot stop on a board with several processors: `smp = 0` in
`/boot/kernel.cfg` makes the kernel count them without waking them.

### exide: miniatures, pointer, non-modal code; and tiscali.it opens

**to be tested on hardware, tested in QEMU** — in exide the tools panel shows
a miniature of each control and a sentence describing it; after a control is
placed the mouse goes back to the pointer; a double click puts the cursor
inside the function; the source window is no longer modal. `make netinst`
now publishes up-to-date development headers (after `netupdate` exide
generated code that did not compile). EXBrowser opens sites that in TLS 1.2
only have RSA with AES-CBC, such as www.tiscali.it.

### Four games: EXKlondike, EXSpider, EXMajong, EXGO

**to be tested on hardware, tested in QEMU** — in Start > Giochi: the classic
solitaire, Spider with one, two or four suits, the mahjong solitaire (every
deal can be won) and go on 9x9, 13x13 or 19x19 against the computer or
between two people. Cards, tiles and stones are drawn by program, each game
has its own icon, and choices and games won are kept in the profile.

### The ExWin API is in English, and exide is consistent

**to be tested on hardware, tested in QEMU** — ExWin's functions, messages,
styles and types have English names (`ex_create`, `ex_set_text`,
`EXM_CLOSE`, `ExWindow`...); the Italian names remain as aliases, and
programs already compiled keep working. In exide a control's name is the
variable to use in code (`Button1`, no longer `h_Button1`), events are
`Button1_Click()` and the like, and old projects still open and build.
Name table in `tools/exwin-inglese/mappa.txt`.

### Firefox in an ExWin window

**tested in QEMU** (`tools/exilla/prova-finestra.sh`) — the seventh stage of
Exilla: Firefox opens in a real window of the graphics server, with tabs, the
address bar and the page, and receives mouse and keyboard. To get there EX-OS
got a new `malloc` (free lists by size: the old one walked the whole heap on
every call), a stack aligned for SSE code, and wserver 0.009, which accepts
pixels from any thread of the program. Picture in
`tools/exilla/tappa7-finestra.png`.

### Firefox draws its first page inside EX-OS

**tested in QEMU** (`tools/exilla/prova-gecko.sh`) — the sixth stage of
Exilla, the port of Firefox: `firefox --headless --screenshot` opens a local
page and saves what it drew (text, bold, italic, coloured boxes, a line
written by JavaScript). Diary and picture in `tools/exilla/leggimi.md`. The
seventh stage — a real window in ExWin — is under way.

### Kernel 0.228–0.236: what Firefox found

**tested in QEMU** — fixes that matter to every program with threads:
- **0.236**: anonymous memory (`mmap`, `sbrk`) is given on first access, not
  on request: Firefox goes from 405 to 300 MB. Two races between the threads
  of one program, which made a stack disappear under Firefox once every few
  starts: a new page table is installed with interrupts off, and two threads
  created at the same moment no longer get the same slot. In the libc
  `time()` and `clock_gettime()` read the CMOS clock once and then count from
  the timer (Firefox asks the time 1.7 million times in a quarter of an hour:
  that was 209 seconds of port accesses).
  **Firefox's real times**, measured from its own start and not from power-on
  (QEMU with KVM, 2 GB): window after 4 seconds, start page after 8, a local
  page 0.1 seconds after Enter. The «65 seconds» written below also counted
  the boot of EX-OS.
- **0.235**: ext2 reads and writes the contiguous blocks of a file in one
  command, and the sector cache (from 64 KB to 1 MB) keeps the metadata
  instead of the data: Firefox's window arrives 10 seconds earlier (65
  seconds after power-on instead of 75, in QEMU).
- **0.234**: `mmap` with `MAP_FIXED` over pages already mapped frees them
  first (they were lost): Firefox's JavaScript JIT needs it.
- **0.233**: `ftruncate` on an open file and 256 open files in the system
  (was 64): Firefox's SQLite databases (WAL mode too).
- **0.232**: 128 open files per process (was 32): Firefox opens more and
  could not find its fonts.
- **0.231**: 128 threads per process (was 64) and 192 processes in the
  system: Firefox with a real window uses more than 63.
- **0.230**: the threads of a process each had a *copy* of the heap
  boundary, and `sbrk`/`mmap` from different threads handed out the same
  addresses. The address space now belongs to the process. A page table born
  from a `PROT_NONE` mapping no longer stays the kernel's, and `PROT_NONE`
  reserves address space without using RAM until `mprotect` opens it.
- **0.229**: the main thread's stack is reserved for 8 MB.
- **0.228**: `fstat` gives the file's real identity (SQLite needs it).
- In `libc.a`: `malloc` with a lock and 16-byte alignment, `arc4random`
  (ChaCha20), a fuller `shm_open` (with `SHM_ANON`), `mmap` of a file,
  `getpid` the same in every thread.

### The floppy has room again

**tested in QEMU** — `help` and `kbprova` (and `/boot/help.txt`) now live on
the CD only: the floppy was down to 512 free bytes and now has 64 KB. From
the floppy, `help` shows the shell's fallback help; with the CD mounted there
is `/cdrom/bin/help`. The diagnostic floppies carry `kbprova` themselves.

### Selecting and copying text in EXBrowser

**tested in QEMU** (`tools/prova_selezione.sh`) — drag the mouse over the
page's text, the selection turns blue, Ctrl+C copies it and Ctrl+V pastes it
in ExEditor or exide. Ctrl+A selects the whole page. For this, mouse
coordinates outside the window (left or above) now reach applications as
negative numbers: before, they became huge ones.

### The pointer over links

**tested in QEMU** (`tools/prova_passaggio.sh`) — over a link the status bar
shows where it leads, and the page gets `onmouseover` and `onmouseout`. The
window server sends button-less movement only to windows that ask for it
(`ex_mouse_passaggio`), like X's event mask. Handlers written in attributes
(`onclick="..."`) are real functions: `this` is the element and
`return false` cancels what follows.

### EXBrowser and Wikipedia

**tested in QEMU** (`tools/prova_wiki.sh`) — on a long article, links after
the 2048th were drawn but did not open: the limit is now 8192. Images are
fetched when they are about to enter the window, not all at the start (one
Wikipedia article: network time from 49 to 7 seconds). Still to do:
`display:grid`, `:has()`, generated content (`::before`) and the script and
layout times.

### Progressive JPEG

**tested on the host** (`tools/prova_jpg.sh`, against ImageMagick) —
wallpapers and web photos saved as progressive JPEG now open, in the browser,
the image viewer and as the desktop wallpaper.

### ExEditor, pop-up menus, tabs

**tested in QEMU** (`tools/prova_menu_chiudi.sh`) — the editor is now called
ExEditor (`/exwin/bin/exeditor`). The right-click menu closes with Esc or a
click outside, and the desktop no longer turns grey while it is open. The
tabs' X is a red button.

### ExEditor writes RTF

**tested in QEMU** (`tools/prova_rtf.sh`) and on the host (`rtfprova`,
`rtfvistaprova`, and with LibreOffice) — an `.rtf` file opens in ExEditor
with its styles, and File > "Nuovo documento RTF" makes an empty one. Above
the text there is a format bar: bold, italic and underline (showing the
style under the caret), typeface, size, colour and the four alignments;
there are also the Formato menu and Ctrl+B, Ctrl+I, Ctrl+U. It saves RTF,
which WordPad and LibreOffice open. Undo and find are not there yet in RTF
documents.

### exide

**tested in QEMU** (`tools/prova_exide_autoaggiorna.sh`) — scroll bars in
the source, every window resizable, and an "Autoaggiorna Proprieta" item
(Strumenti) saved in its configuration: leaving a box applies the property
without pressing Applica. A click in the box now puts the caret where it
lands (exide used to take the focus away), and a form wider than the canvas
no longer covers the properties.

### Profile folders

**tested in QEMU** (`tools/prova_desktop.sh`) — the first time the desktop
starts, the profile gets the Documents, Images and Media folders, each with
a link on the desktop; deleting the link removes only the link, and says so.

### toolinst

**to be tested** — `toolinst` makes the programs it installs executable:
before, only root could run `gcc` or `fbc`.

### `dlopen` and `dlsym`

**tested in QEMU** (`tools/prova_dl.sh`, 11 out of 11) - a program linked
with `libc.a` can open an EX-OS library by name on demand
(`dlopen("exzip.so", ...)`), find its functions with `dlsym` and call them;
if the library is not there, `dlerror` says why. An arbitrary ELF `.so` is
not loaded: EX-OS libraries have a table of their own, and Firefox links
statically. With this the first stage of Exilla (the system) is done.

### POSIX shared memory

**tested in QEMU** (`tools/prova_shm.sh`, 16 out of 16) - `libc.a` has
`shm_open`, `ftruncate` and `mmap` on that descriptor, on top of the EX-OS
shared memory zones: two programs open the same name and see the same
pages, even with the long names Mozilla uses. A zone does not grow after it
is born and `O_EXCL` does nothing. Firefox (Exilla) needs it.

### The graphics no longer flicker

**tested in QEMU, to be judged on real hardware** - the flicker came "with
the load", worst in big programs like EXBrowser. There were three causes.
The server read a window while the program was still drawing in it: now
every window draws into a buffer of its own and the server only gets the
finished rectangle (Windows' double buffering, Wayland's *commit*). The
pointer blinked: the server composes into a copy of the screen in RAM and
then copies it to the screen in one go (`exwin -noombra` goes back). And
every handled message redrew the whole window: the desktop repainted every
two seconds, EXBrowser redrew the page at every timer tick and every mouse
move. A program can now say "nothing changed" (`EX_NON_RIDISEGNARE`). With
the desktop idle the server composes one frame a minute, where it did two
or three a second; `exwin -conta` also says who asks for updates.

### Signals are really delivered, and there is `mprotect`

**tested in QEMU** (`tools/prova_segnali.sh`, 23 out of 23) - since kernel
0.227 a program linked with `libc.a` can install a handler with
`sigaction`: a bad pointer, an invalid instruction or a division by zero
call the handler instead of closing the program, with the address and the
registers, on an alternate stack too (`sigaltstack`); the handler may change
the registers and the program resumes from them. `raise`, `kill` and
`pthread_kill` reach the right thread, and there are `sigprocmask`,
`sigsetjmp` and `siglongjmp`. `mprotect` makes a page read-only or
untouchable. Firefox (Exilla) needs it. The floppy's `libc.so` has no room:
there `signal` and `raise` stay as they were.

### BSD sockets

**tested in QEMU** (in C 23 out of 23, in Rust 9 out of 9) - `libc.a` has
`socket`, `connect`, `bind`, `listen`, `accept`, `send`, `recv`, `poll` on
sockets and `getaddrinfo` with DNS, on top of the EX-OS IP stack (kernel
0.226). IPv4 only. They are for ported software: Rust (`std::net`) and
Firefox.

### Rust's `std` runs inside EX-OS

**tested in QEMU** (`tools/rust-exos/prova-std.sh`, 29 out of 29) - a Rust
program with the real standard library, compiled on Linux, uses files,
directories, time, threads, `Mutex`, `Condvar` and channels inside EX-OS
(kernel 0.225). It is the second stage of Exilla, the Firefox port.

**At 64 bit too** (14 October 2026): `EXOS_ARCH=x86_64 tools/rust-exos/prova-std.sh`
builds the same program for the new target `x86_64-unknown-exos`
(`tools/rust-exos/x86_64-unknown-exos.json`, linked with `x86_64-exos-gcc`) and
runs it in the 64-bit system: 29 out of 29. The C structures Rust repeats
(`stat`, `pthread_*`, `sigaction`...) are now checked against the C compilers
of both architectures at every build (`tools/rust-exos/std/verifica-abi.py`
writes the checks; a size or offset that differs stops the compilation). It
found two: `pthread_attr_t` is twice as large at 64 bit, and `struct lconv`
lacked its last six fields at 32 bit too. This is Rust as a cross compiler
from Linux; a `rustc` that runs inside EX-OS does not exist yet at either size.

### The browser starts at 32 MB too

**tested in QEMU** - at 32 MB EXBrowser sometimes did not start ("LIB:
pagina non allocata"). It was not memory: two programs asking for the same
shared library at the same time stole each other's slot in the kernel's
loader while the first waited for the disk. Kernel 0.224 reserves the slot
before reading, and whoever comes later waits.

### ExJs has regular expressions

**tested in QEMU and on the host** - the ExJs engine reads `/.../flags` and
has `RegExp`: classes, groups (named too), alternation, greedy and lazy
quantifiers, backreferences, `(?=` and `(?!`, the flags g i m s y; and with
them `exec`, `test`, `replace` and `replaceAll` (with `$1`, `$<name>` or a
function), `split`, `match`, `matchAll`, `search`. An expression that would
run for ever stops with an error instead of freezing the page. With regular
expressions the browser's prelude runs under ExJs too. Lookbehind is missing
(`make prova-exjs`, 492 tests).

### Tabs: in the editor, in exide and in the browser

**tested in QEMU** - the editor opens several files, one per tab (`edit
a.txt b.txt`, Ctrl+N, Ctrl+O); Ctrl+Tab moves between tabs, Ctrl+W closes
the chosen one and asks if it was modified. In exide the project's file
window keeps several sources in tabs. EXBrowser opens links in a new tab
(`target="_blank"`, Ctrl+click, Ctrl+T) or in a new window (Shift+click,
Ctrl+N, the right button on a link); background tabs keep their address and
reload when you come back. The tab bar is a toolkit control, available to
every program (`tools/prova_edit_schede.sh`, `prova_exide_schede.sh`,
`prova_nav_schede.sh`, `prova_nav_finestra.sh`).

### ExJs reads today's JavaScript

**tested in QEMU and on the host** - the ExJs engine (QuickJS stays the
default) reads `let` and `const`, arrows, classes with `extends` and
`super`, `` `${}` `` templates, destructuring, spread, `?.`, `??`, `**`,
`for..of` and labels, and has `Object`, `Array`, `Map`, `Set`,
`call`/`apply`/`bind`, the newer array and string methods, `toFixed` and the
`Math` functions. Numbers print as in every other engine (`0.1 + 0.2` is
`0.30000000000000004`, `1e21` is `1e+21`) and `for..in` gives keys in
creation order. Regular expressions and Promises are still missing, and
objects are not reclaimed (`make prova-exjs`, 448 tests;
`tools/prova_es2015.sh`).

### ExJs: `throw`, `try`, `switch` and prototypes

**tested on the host** - the ExJs engine (QuickJS stays the default) has
`throw`, `try`/`catch`/`finally` and `switch`; `new F()` inherits the
methods of `F.prototype`, and `instanceof` works. `Error`, `TypeError`,
`RangeError` and the others exist, and the engine's own errors can be caught.
A script stopped for running too long cannot. Reading a property of
`undefined` still gives `undefined` instead of throwing (`make prova-exjs`,
342 tests).

### The right button, dragging, and file associations

**tested in QEMU** - the right button opens a menu. On the desktop: New
folder, New file, Paste, and on an icon also Open, Rename, Copy, Cut, Delete.
The file manager has the same items; there several rows can be chosen
(Ctrl+click, Shift+click) and dragged onto a folder on the left: a window asks
whether to copy them, move them or leave them. A file that is not a program
opens with the program it belongs to (`/exwin/lib/tipi.txt`): from the file
manager, the desktop and the shell, where `nota.txt` opens the editor
(`tools/prova_tasto_destro.sh`; Ctrl and Shift with the mouse cannot be tested
in QEMU).

### Pennello writes

**tested in QEMU** — Pennello has a Text tool (key T): click where the text
starts, type it, and it goes into the picture in the chosen colour; the width
buttons choose its size (12, 16, 24 or 36 pixels). It is undone like any
other stroke (`tools/prova_pennello_testo.sh`).

### Pennello, the paint program

**tested in QEMU** — `/exwin/bin/pennello [file]`, in the Start menu: pencil,
brush, eraser, spray, fill, colour picker, line, rectangle and ellipse (empty
or full), four widths, zoom from 1x to 8x, mirror, rotate, invert colours,
resize, **undo and redo**. It **opens** BMP, PNG, JPG, GIF and ICO and
**saves** PNG and BMP — a JPG opened and saved says so first and offers the
same name as `.png`. It writes a copy next to the file first and then
renames it: a failed save leaves the old file in place.

Undo does not photograph the picture at every stroke: each operation leaves
only the **rectangle** it touched in a pool taken at start (a quarter of the free memory), and undoing **swaps** it
with the picture — so the same record is also the redo. The PNG and BMP
encoders are new (`lib/eximg/scrivi.c`), and eximg now reads BMP in all its
forms (`lib/eximg/bmp.c`). Tested on the host against ImageMagick
(`tools/prova_scrivi.sh`) and in QEMU by drawing with the mouse
(`tools/prova_pennello.sh`). The icon is provisional.

### EXBrowser lays out the page

**tested in QEMU** — EXBrowser understands the CSS that lays out a page
instead of putting everything in one column: `width`, `max-width` and
`min-width` (percentages too), `margin: auto` to centre, `float` with text
flowing beside it, `display: inline-block` and `display: flex` in a row (with
`flex-grow`, `flex-wrap`, `gap`, `justify-content` and `align-items`),
`clear`, `position: relative`, and off-screen hidden text no longer shows. On
Wikipedia the infobox sits on the right with the text beside it, and long
articles no longer stop at 1 MB: the main page holds 3. An old bug is
fixed too: white text came out black (`make prova-excss`,
`tools/prova_disposizione.sh`).

### Searching Wikipedia

**tested in QEMU** — Wikipedia's search box is shown and works: "roma" +
Enter opens the article, "canale grande" + the Search button opens the
results page. The bug was in the transport: URLs with a path over 512
characters were cut, and Wikipedia's stylesheet arrived wrong. The limit is
now 1024, and a longer URL gives an error instead of asking for something
else.

### Calctor as an adding machine

**tested in QEMU** — with the Cronologia box on, Calctor becomes an office
adding machine: the = key goes away, + and - become two tall keys, and each
adds (or subtracts) to the total and shows it at once. `12 * 3 +` finishes
the product and adds 36; `+` again prints the subtotal on the tape.

### The mouse wheel

**tested in QEMU** — the wheel scrolls the page in EXBrowser and, in every
ExWin program, the lists, text areas and scroll bars under the pointer: three
lines per notch. It scrolls what you are looking at, not the window with the
focus. For now it works with PS/2 mice (including the one QEMU emulates and
many laptops); USB and serial mice do not send the wheel yet
(`tools/prova_rotella.sh`).

### A fast mouse no longer freezes the desktop

**tested in QEMU** — moving the mouse quickly while drawing in Pennello froze
all of ExWin. Two bugs: Pennello's line routine never reached the end of some
segments, and the window server, delivering a move to a program that was not
reading, waited up to ten seconds. Mouse moves are now delivered **without
waiting** (kernel 0.223, `IPC_SENZA_ATTESA`): a hung program only slows
itself down. Clicks and keys still all arrive
(`tools/prova_pennello_veloce.sh`, `tools/prova_linea.sh`).

### gfedit: F5 and BASIC INPUT

**tested in QEMU** — a BASIC program started with F5 from gfedit now shows
the `INPUT` prompt and reads the keyboard: gfedit hands it the console while
it runs, and Ctrl+C stops it (`tools/prova_runbas.sh`).

### ExJs: `Date` and `valueOf`

**tested in QEMU** — the ExJs engine has `Date`: `new Date()`, `Date.now()`,
`Date.UTC`, `Date.parse`, the getters and setters, `toISOString`,
`toString`, `toLocaleString` and `JSON.stringify` of a date. The browser
supplies the time, and since 16 November 2026 the zone too
(`exjs_fuso_metti`, from the libc's `localtime`: kernel 0.340); before, it
was all UTC. Objects in
arithmetic go through `valueOf`: the difference between two dates is a
number, which is how pages measure time. Tested on the host
(`make prova-exjs`, 307 checks) and in EXBrowser with both engines
(`tools/prova_date.sh`).

### ExJs: implicit `toString` and `join` without a limit

**tested on the host** — with the ExJs engine (QuickJS stays the default)
`String(object)`, `'x' + object` and `join` call the object's `toString`:
`String(location)` is the address. `join` no longer stops at 511 characters,
and the tests written first found three more bugs that were fixed (nested
arrays, long separators, an array containing itself) (`make prova-exjs`,
`make prova-exdom`).

### Forms to local pages, and the image button

**tested in QEMU** — a `file:` address with a query (`page.html?x=1`) opens
the right file instead of looking for one with the question mark in its name:
a GET form sent to a local page now arrives, and the page reads
`location.search`. And `<input type="image">` sends the clicked point instead
of zero (`tools/prova_modulo_immagine.sh`).

### `onclick=` runs even without a `<script>`

**tested in QEMU** — a bug fix: the JavaScript engine was opened only for a
`<script>`, so a page with nothing but inline handlers (`onclick=`,
`onchange=`, `onload=`) ran none of them (`tools/prova_onclick.sh`).

### Animated GIFs move

**tested in QEMU** — in EXBrowser and in the Immagini viewer. eximg composes
the frames — position, transparency, disposal, the loop starting again — and
says how long each one stays; the clock belongs to whoever shows the picture.
Tested against ImageMagick frame by frame (`tools/prova_gif.sh`) and
photographed while it changes (`tools/prova_gif_anima.sh`). ExWin's wake-up
runs at most five times a second: a very fast GIF runs slower.

### A click in a page's text box puts the caret there

**tested in QEMU** — a click inside a page's text box or text area used to
give it the focus and leave the caret at the end; now it goes between the two
letters nearest to the click (`tools/prova_cursore.sh`).

### EXBrowser understands em, rem and %

**tested in QEMU** — `font-size: 1.5em`, `margin: 2em`, `padding: 0.5rem`,
`font-size: 120%`, and the keywords `small`, `large`, `larger`: until now
lengths were pixels only and everything else was dropped. A relative length
is resolved at the end of the cascade — font size against the parent, the
rest against the element's own size — and the test measures it in pixels on
screen (`tools/prova_bordi.sh`, `make prova-excss`). Percent margins are
still taken on the window width.

### EXBrowser draws CSS borders, and padding

**tested in QEMU** — `border`, `border-top` and the other sides,
`border-width`, `border-style`, `border-color`, `padding` and the `margin`
shorthand with one to four values: blocks are drawn with their box and the
content is inset. Before, borders existed only for tables with `border=`, and
no CSS shorthand was read at all — not even `margin: 0 auto`. Borders of
inline elements and CSS borders of tables are still missing
(`tools/prova_bordi.sh`, `make prova-excss`).

### Immagini, the picture viewer, and PNGs of every kind

**tested in QEMU** — `/exwin/bin/immagini [file]`, in the Start menu: fits
large pictures to the window, "1" for 100%, + and - from 10% to 800%, and
PgDn / PgUp to move to the other pictures of the directory. From the file
manager a double click gets there: the rule is in `/exwin/lib/tipi.txt`,
**"these extensions open with that program"**, a file of the system and not
of the program. Along the way eximg's PNG reader learned **every bit depth
(1, 2, 4, 16) and Adam7 interlacing**: it used to read 8 bits only, and a
one-colour PNG did not open. Tested against ImageMagick on 92 files
(`tools/prova_png.sh`) and in QEMU (`tools/prova_immagini.sh`).

### Ctrl+C on the text console asks, then stops

**tested in QEMU** — in line mode Ctrl+C asks `[Ctrl+C] Fermo "textline"
(PID 20)? s/n` and stops the program only on "s". At the prompt it empties
the line and the prompt comes back; a program in raw mode (gfedit) gets it as
a key. Who to stop was already said by the shell: the kernel (0.221) now keeps
it per console, and the keyboard driver reads it with `SYS_CONSOLE_CTRLC`
(216). Together with Ctrl+Alt+Del this completes the "keys that stop things"
task (`tools/prova_ctrlc.sh`, `tools/prova_tasti_sistema.sh`).

### Searching Wikipedia, and @media rules

**tested in QEMU, on the real network** — "Search does nothing" was not the
search: it was the TCP table of `ip.drv`, full after one Wikipedia page, with
dead slots that never came free. Now they do. EXBrowser evaluates `@media`,
`@supports` and `@layer` rules instead of skipping them (578 out of 964 in
Wikipedia's sheet), and with them `visibility`, `:checked` and `<label>`. The
page is still laid out in one column: `width`, `float`, `padding` and `flex`
are missing.

### Calctor, the calculator

**tested in QEMU** — `/exwin/bin/calctor`: normal, scientific and programmer
(32 bits in base 2, 8, 10 and 16), a four-line display and the tape in a
window of its own, which can be saved as text. What you type is an
expression, with precedence: `2 + 3 * 4` gives 14. Along the way the toolkit
got side menus, `ex_abilita()` and `ex_dlg_testo()`, and the limit of
controls per program went from 64 — which made `ex_crea` fail silently — to
192 (`tools/prova_calctor.sh`).

### The desktop: the profile folder, the drives, the log

**tested in QEMU** — on the left the icons of `$HOME/desktop` (a shortcut is
a text `.lnk`), on the right every mounted drive: pm notices by itself, even
a `mount` typed by hand. The wallpaper can be placed (corner, centre,
stretch, tile). The **System log** is no longer one screen but a 32 KB ring
in the kernel, with the time and the PID on every line
(`tools/prova_desktop.sh`, `tools/prova_registro.sh`).

### The editor finds and replaces

**tested in QEMU** — Find (Ctrl+F), F3 and Shift+F3 for next and previous,
Replace one or all. In the toolkit **Shift+Tab** now really goes back, and
the focused button shows it (`tools/prova_edit_cerca.sh`).

### Archivi opens tar files, and the file manager renames

**tested in QEMU** — Archivi opens and creates `.tar` and `.tar.gz` with the
same code as `/bin/tar` (`lib/extar`, compiled into both). In the file
manager, **Rename** with F2 (`tools/prova_tar.sh`, `tools/prova_filemgr.sh`).

### `runbas`: QBASIC programs run on their own

**tested in QEMU** — `runbas myprog.bas` runs a program with the `gfbasic/`
interpreter, and gfedit launches it with **F5**. The maths functions the libc
lacks (SIN, EXP, ^...) are the x87's (`tools/prova_runbas.sh`).

### Program paths in a file

**to be tested on an installed system** — `/boot/percorsi.txt`: the shell
appends to the PATH the directories it finds there, and netupdate writes its
own when it installs a package in a new place.

### HTTPS: from 23 to 28 sites out of 30 — AES-GCM, P-256 and TLS 1.2

**tested in QEMU, on the real network** — of the seven sites out of thirty
that did not open, one was the certificate chain (fixed this morning) and the
others were TLS. The client now has:

- **AES-128-GCM** next to ChaCha20 (`lib/excrypt/gcm.c`, on top of the Wi-Fi's AES);
- key exchange on **P-256**, **in constant time** (`lib/excrypt/p256.c`, not
  `excurva`, which is for public numbers only), asked for by the server with a
  **HelloRetryRequest** — which used to be an error;
- **TLS 1.2**, the modern half: ECDHE with AES-GCM or ChaCha20, RSA-PSS,
  PKCS#1 v1.5 and ECDSA signatures, Extended Master Secret, and the
  "DOWNGRD" check.

Poste, Corriere, Gazzetta and Istat open. The two left (Stack Overflow and
eBay) open TLS and then answer 403: a robot block of theirs. Tested on the
host (`tools/prova_gcm.sh`, `tools/prova_p256.sh`: thousands of cases against
Python's `cryptography`) and against `openssl s_server` (`make
prova-cliente-tls`, 20 cases).

### The Download window, in the toolkit

**tested in QEMU** — EXBrowser's downloads run in a process of their own
(`scarica -avanza`), and the browser stays free to browse. The **Download**
window opens by itself, shows percentage, size, average speed and time left,
closes without stopping anything and comes back from **Strumenti > Download**;
**Ferma** removes the half file. It lives in the toolkit
(`lib/exdlg/scarichi.c`, with `ex_guarda_fd` in exwin), so any program made
with exide can use it: see the exide manual, "Scaricare file"
(`tools/prova_download.sh`).

### EXBrowser asks "open or download", and Archivi takes directories

**tested in QEMU** — a link to a file that is not a page (a ZIP, a program, an
image) is no longer laid out as garbage: EXBrowser asks **Apri con Archivi**
(or with the editor), **Scarica** or **Annulla**. It notices from the name,
before downloading, or from the server's `Content-Type` when the name says
nothing. The download goes to disk as it arrives, without the 1 MB ceiling of
pages (`tools/prova_apri_scarica.sh`).

In **Archivi** there is **Comandi > Aggiungi cartella...**, and `zip
archivio.zip cartella` takes the whole tree too: every directory has its own
`name/` entry, so empty ones come back out on extraction (`tools/prova_zip.sh`,
step 6).

### Certificates: the chain stops at the first root, and more can be added

**tested in QEMU, on the real network** — before changing anything it was
measured (`tools/prova_certificati.sh`): of thirty common sites EX-OS opened
23, and **only one** was a certificate problem. www.amazon.it sends at the end
a *cross-signed* copy of the DigiCert root, signed by an old, retired VeriSign
root; `excert` required the last link sent to be signed by one of our roots,
and refused it. Now the chain stops at the first root in the store, as the big
browsers do: Amazon opens.

The other six were not certificates: three servers speak only TLS 1.2 (our
client only 1.3), two do not accept ChaCha20 or x25519, one blocks robots.
They are written down in `@EXBROWSER-CERT`.

And **CAs can be added**: **File > Aggiungi un certificato...** in EXBrowser,
from an address or a file; who it is and its SHA-256 fingerprint are shown
before saying "Mi fido". They go into `$HOME/.app/exhttp/certi.pem` and apply
to every program that opens `https`, `scarica` included — tested by
`tools/prova_certi_aggiunti.sh`.

### The browser is called EXBrowser

**to be tested** — `/exwin/bin/browser` becomes `/exwin/bin/exbrowser`, at
version 0.003: the name was chosen by its user, and it changes everywhere it is
a name rather than a word — the window title, "About", the Start menu entry,
the icon (`exbrowser_64.ico`), the manual (`/exwin/doc/exbrowser.html`) and the
data in `$HOME/.app/exbrowser/`.

! **THE OLD DATA MOVES BY ITSELF**: at the first start `$HOME/.app/browser/`
becomes `$HOME/.app/exbrowser/`, with settings, cookies and cache. If it
cannot — a home on FAT, where "exbrowser" does not fit in 8.3 — the old one
keeps being used, and it says so: an old name is better than being logged out
of every site.

### Icons: a toolkit API, and the desktop menu that uses it

**tested in QEMU** — `ex_icona_apri` / `ex_icona_disegna` / `ex_icona_metti`
live in `exwin.so`, that is, in what every graphical program already links:
icons belong to everybody, not to one program. The real weight — decoding PNG,
JPG and ICO — stays in `eximg.so`, which is opened only in front of the first
icon that is not a BMP.

! **An icon is not an image drawn once**, which is why it has functions of its
own: a menu is repainted every time it opens, and with `ex_immagine()` that
would be ten file reads and ten decodes each time. It is opened once, kept, and
the last rendering (size + background) stays ready.

! **The scaler really was needed.** Icons are born at 64 and 128 pixels, a menu
entry is 24 tall. It **averages** the pixels that collapse into one: taking the
middle one, on a four-times reduction, throws away fifteen pixels out of
sixteen and thin edges disappear in patches.

! **And `eximg` was throwing the alpha away.** The code said so: *"the 1-bit
mask is skipped... the day [the server] can blend, that is where it will be
fetched from"*. That day was this one — what blends is not the server but the
toolkit — and without alpha the icons came out as squares of pure background:
invisible, that is, "they do not load" while they were being drawn perfectly,
entirely transparent.

In the **Avvio** menu: the icon before the name, and **categories** that open
to the side. A slash in the name is enough to make one — `Strumenti/Editor` —
and they are declared nowhere else.

### ZIP archives: one library, one command and one window

**tested in QEMU, and by a foreign reader** — `lib/exzip` knows about ZIP and
knows nothing about windows; `/bin/zip` and `/exwin/bin/archivi` call the same
nine functions.

```
zip a.zip one.txt two.txt        create
zip -l a.zip                     list
zip -x a.zip /disk/out           extract
```

It writes "store" and also reads **deflate**, because the hard part was already
there: `lib/eximg/inflate.c` has been decoding DEFLATE for months for PNG, GIF
and the fonts.

! **The test that counts is the one by whoever did not write the format**: the
archive EX-OS makes is opened by Info-ZIP's `unzip -t`. An archiver that
re-reads its own archives only proves it is consistent with itself.

### Archivi: a real table, and an archive that builds itself

**tested in QEMU** — separate **Nome** and **Percorso** columns, a click on
a header sorts (another reverses), the borders between headers can be
dragged, a scroll bar. Every file or directory added leaves the archive
complete at once: `lib/exzip` can reopen a finished archive and add to it
(`ex_zip_riapri`). **Opzioni**: compression (none, fast, normal, best) and
default extension, in `/exwin/config/archivi.cfg`.

### Minimize, and the taskbar of open programs

**tested in QEMU** — every window has a **«_»** button next to the close
button, and the bottom bar shows the open programs with their icon. A click
on an entry restores a minimized window, exactly where it was and as large as
it was, or brings forward one hidden behind the others. The window server
keeps the list and sends it to the taskbar by itself; programs did not have
to change a line. `tools/prova_riduci.sh` measures it in pixels.

### tar, gzip and gunzip

**tested in QEMU, and by outside readers** — one program, `bin/tar/tar.c`,
answering to three names. It is static: the DEFLATE of `lib/exzip` and the
inflate of `lib/eximg` are compiled into it.

```
tar czf a.tgz folder             create (z = gzip)
tar tf a.tgz                     list
tar xf a.tgz -C /disk/out        extract (gzip is recognised by itself)
gzip file / gunzip file.gz
```

It reads GNU tar archives with long names too, in GNU and pax format.
`tools/prova_tar.sh` has its archives opened by GNU tar, `gzip -t` and
Python, and has EX-OS open archives made by GNU tar.

### The login page no longer gets covered by the boot messages

**tested in QEMU on an installed disk** — `login` waits for the console to go
quiet (up to 1500 ms, measured) before printing the name prompt, and listens to
the keyboard while it waits: whoever is already typing waits for nothing.

! **The `waitpid` was there, and it waited for the wrong thing**: half of
`/boot/avvio.sh` is lines ending in `&`, and those processes are
**grandchildren** — children of the script's shell. When `waitpid` returns they
are alive, they hold the console and they have not spoken yet.

### EX-IDE's new project picks where, and remembers it

**tested in QEMU, over two boots** — Ctrl+N opens the file dialog with the tree
and the **"Nuova cartella (Ctrl+N)"** button; home is asked of `HOME` and the
last directory used lives in `$HOME/.app/exide/`.

! **The button lives in the dialog, not in the program**: `ex_dlg_salva` has it
for everyone, and the ZIP archiver — written a few hours later — found it ready
without doing anything.

### `wserver` uses the 2D engine where there is one, and draws itself where there is not

**tested in QEMU for the software path, on the metal for the protocol** — the
window server asks the card for large fills when a 2D engine is there, and does
them with MMX when it is not.

! **And it does not ask for privileges: it asks for a rectangle.** The engine is
driven with `mmio_map` and `ioport_bind`, which are a driver's. `wserver` lost
those privileges **on purpose** on 19 August 2026: it was called
`/dev/wserver.drv` only to get `mmio_map`, and that name kept graphics out of
multi-user — `/dev` is root's, so an ordinary user could not run the server.
Putting it back among the drivers to go faster would be a month backwards, and
would hand it the *wide* capability instead of the narrow one.

So a **service**, not a library:

```
drivers/accel/accel_proto.h   the contract, and it belongs to neither side
sis.drv -2dservizio           registers 'accel2d': FILL, COPY, INFO
sis.drv -2dchiedi             tests it FROM OUTSIDE, the way wserver uses it
```

`/boot/avvio.sh` starts it (written by `hwconfig`), because only the system can
execute things in `/dev`. On a machine that is not a SiS the command **exits
silently**: no card, no service, no line.

! **The software path is the NORMAL path, not a fallback.** On VESA, on the
generic framebuffer and inside QEMU nobody registers the service, `ipc_lookup`
finds nothing, and filling is done with MMX at 377 MB/s — the bus limit for
writing. In QEMU the boot line says exactly that:

```
wserver: nessun motore 2D, riempio da me con MMX
```

! **The protocol carries operations, not pixels**, which is what makes it
affordable: one IPC round trip measured on the Acer — whole background, 480000
pixels — is **under a millisecond**, and filling the screen costs milliseconds.

! **And there is a threshold, because the engine saves half the time, not all
of it.** The message must be paid for out of *half* the rectangle's cost. At
4096 pixels the saving is 21 µs, less than the message: a net loss, and that was
the first draft's threshold. At 32768 the saving is 165 µs, and the whole
background is worth 2500.

! **Waiting for the reply uses `ipc_scegli`, not `ipc_recv_timeout`**, and that
is the most important line of the work. This server's rule is written above
`mouse_chiedi()`: «one place reads the mailbox». A `recv` that takes any message
eats the clients' requests — it has happened, and the symptom was «the server
does not answer», one time in three. `ipc_scegli` filters, and **while it waits
it drains** the kernel queue onto the shelf instead of letting it fill: the
mailbox is four messages deep, and when it is full every `ipc_send` towards the
server fails — including the delivery of a keystroke.

! **What is left is a single measurement:** proving that `wserver` really uses
it on the real machine. The `servite` counter in `AccInfo` exists for that. And
the gain to expect is the **fill one (1.9x)**, not the copy one: the compositor
does no screen-to-screen copy, because the clients' areas live in system RAM.
The 14x is only reachable by changing the compositing strategy.

### The SiS 2D engine draws: 14x on screen-to-screen copies

**tested on the real machine, from cold** — the 2D accelerator of the Acer
Aspire 3000's SiS 6330 works. After a clean reboot, with no bring-up command
beforehand:

```
sis2d: motore acceso - 65536 KB di memoria video, coda a 0x03f80000
  il riempimento e' giusto, bordi compresi.
  la copia e' identica all'originale.
  la sovrapposizione e' gestita dal motore.

operazione                   CPU     motore   guadagno
riempire                     377 MB/s    734 MB/s   1.9x
copiare schermo->schermo      25 MB/s    356 MB/s  14.2x
```

! **The line that counts is the second.** Filling the screen the CPU already
does at the limit of the bus (see `fbprova` further down); copying screen to
screen costs it a **read** from the framebuffer, which on this card is four and
a half times slower than a write. The engine does it inside the card, without
the pixels crossing the bus even once. That is exactly what the compositor does
when it moves a window or scrolls a terminal.

**It was two bits, and the diagnostic had been printing them from day one:**

| register | bit | name in sisfb | what it turns on |
|---|---|---|---|
| SR20 | 0x01 | `SIS_MEM_MAP_IO_ENABLE` | the register window |
| SR1E | 0x40 | `SIS_ENABLE_2D` | the engine |

Step 4 of `-2ddiagnosi` showed «SR1E = 0x20, SR20 = 0xa0» for as long as that
code has existed, and those numbers had ended up in the reports among the facts
«established on the real machine». They were two **measurements**, and nobody
read them as two **absences** — while five explanations were tried and closed.

! **And the order is not negotiable**, because the step that was missing in the
middle is the one that hung the machine:

```
1. SR20 bit 0            open the window
2. Q_BASE_ADDR = base    WHERE the engine reads its commands from
3. SR27 0x1F, SR26 0x01  threshold and queue RESET
4. WRITEPORT <- READPORT empty queue
5. SR1E bit 6            THE ENGINE, last
6. SR26 = 0x22           queue in MMIO mode
```

Right after the window opens `Q_BASE_ADDR` reads **zero**: turning the engine on
there means starting it fetching commands from address zero of video memory and
writing wherever those commands say — a bus master off on its own. The machine
stops and no longer answers even a ping. It happened, once, and it is why step 2
is there now.

! **The base is computed, not picked**: `CR79` bits 7-4 give the megabytes of
shared memory (64 on the Acer) and the queue goes in the last 512 KB. If that
measurement is not recognised, `sis2d_apri()` **gives up** and leaves the engine
off.

! **The bring-up is done by `sis2d_apri()`, and only if needed** — that is, if
`Q_STATUS` reads all ones. If the window already answers, somebody else turned
it on: redoing the sequence would mean resetting a queue that may be working.

! **`wserver` does not use it yet.** The gain is there and the compositor is not
taking it: that is the next step, and it lives together with narrowing the dirty
regions.

### The clock can be set: kernel 0.218 and `/bin/date`

**tested on the real machine** — ever since it existed, August 2026, the EX-OS
CMOS clock could only be **read**: there was `SYS_TIME` and no twin of it. That
was not a theoretical hole. The Acer Aspire 3000 used for testing read **2005**,
and every file it uploaded to an FTP server arrived dated «Feb 9 2005»: the only
way to fix it was to enter the BIOS.

    date                        mercoledi' 16 settembre 2026, 16:09:20
    date -d                     2026-09-16
    date -t                     16:09:30
    date -set-date:2026/09/16   the date  (root only)
    date -set-time:17:52:30     the time  (the seconds can be left out)
    date -ntp[:SERVER[:PORT]]   the time from the network, NTP (root; also -npt)
    date -fuso[:+1,ue]          the zone that turns NTP's UTC into local time

`SYS_TIME_SET` (213) calls `rtc_write()`, and it is **root's, like `mount`**:
whoever moves the time changes the date of every file anybody else will write,
and with it `netupdate -check`'s judgement about what is newer.

! **Writing has one trap more than the three of reading**: you do not write
while the chip is counting. If the update falls in the middle of the six
registers, the chip overwrites half of what you have just put there. The `SET`
bit of register B stops the count for the duration of the write.

! **There is no `time` command, and that is deliberate**: on any Unix `time
command` *measures* how long a command takes to run. Spending that name on
printing the clock would mean not having it the day it is needed. The details —
the format that is preserved, the century register, the 2099 limit — are under
**[`date`](comandi.md#date--what-time-it-is-and-putting-it-right)**.

On the metal: the Acer went from Thursday 10 February 2005 to Wednesday 16
September 2026, written and read back, and the date survived a power cycle.

### A bare `ls` also says when and what

**tested in QEMU** — `ls` used to print two things, the name and the size. The
date was already there, but only under `-d`, `-md` or `-l`, that is, only for
whoever knew they existed; and the type had to be inferred from the trailing
slash on the name. The two commonest questions in front of a list of files are
«how old is it?» and «is it a folder?».

```
ex-os:/> ls
data        ora    tipo    dimensione  nome
2026-09-16  18:10  <DIR>            -  BOOT
2026-09-16  18:10  <FILE>      270376  KERNEL.BIN
```

! **And it costs one `stat()` per entry**, which the bare mode did not do
before: on a floppy that is felt. So the old behaviour has not gone away, it
lives under **`-s`**, and that is the mode to use on a slow disk or in a large
directory.

! **There is no creation date**, and that is not a choice: the directory entry
FAT, ext2 and ISO 9660 hand to the VFS keeps **one** date/time pair, and
`struct stat` indeed sets `st_ctime` and `st_atime` equal to `st_mtime`.

### USB sticks get ejected, not yanked: `eject`

**tested in QEMU, the whole round trip** — `automount` already mounted them by
itself under `/USB/DRIVE0`; the other direction was missing. The driver serving
a disk sits inside `blk_attendi()` and **stops watching the ports**: pulling the
stick left a mount point answering errors, and putting it back produced nothing,
because nobody was looking.

    eject                  what can be removed
    eject /USB/DRIVE0      unmount and withdraw

It unmounts whatever sits on top and then **withdraws** the device
(`SYS_BLK_ESPELLI`): the driver's wait returns `-ENODEV`, it goes back to the
port loop, and the same stick put back in is picked up again. The order cannot
be reversed: a device withdrawn while still mounted does not disappear, it stays
FAULTY — which is why the syscall refuses with `-EBUSY`.

! **And it uncovered an old defect that could not be seen before**: the `ehci`
control QH was still pointing at the previous stick's address, and a
freshly-plugged device answers only address zero. With one stick for the whole
life of the process it never showed; now that the driver goes back to watching,
the second stick is the first to pay for it.

### A remote session that goes neither deaf nor mute: `telnetd` 0.008

**tested on the real machine** — two different defects, both found on a laptop
at the other end of a real network, neither reproducible in emulation on the
first try.

*The deafness*: while `tcp_scrivi()` waited for its own result, `attendi()`
**threw away** the messages it was not looking for — including the keys typed at
the other end. And the stack delivers to whoever booked, **consuming the
booking**: with the delivery thrown away, the session never made another one.
The symptom: the prompt appears and from then on nothing responds.

*The muteness*: `ipc_recv_timeout(..., 0)` does not mean "do not wait", it means
**wait forever** — and the mailbox can be empty even when `poll` said otherwise,
because between the two there is someone else fishing. The loop stopped there,
and since `telnetd` serves one session at a time, from that moment port 23
accepted connections without sending a byte.

### A half-finished update must not leave a dead machine

**the failure really happened; the protection is written and still to be proven
in the field** — programs call the libc **by name**, and that produces an
asymmetry that decides everything:

| | |
|---|---|
| OLD binary + NEW libc | works: the old names are still there |
| NEW binary + OLD libc | **nothing starts**: one name is missing |

An update interrupted between the two leaves the machine in the second case: it
boots, the network is fine, and there is not a single command left to fix it
with — `scarica`, `dhcp`, `ipcfg`, `netupdate` all answer "the shared library
does not have the function ...". Hence three things:

- **`netupdate` 0.021**: two passes over the list, shared libraries first and
  everything else after; and if a library **does not arrive**, nothing else is
  touched and it says so. The machine stays old and alive.
- **`/bin/soccorso`**: static, with the libc compiled in like the drivers, so it
  runs even when nothing else does. It puts `/lib/libc.so` back, taking it from
  the server and checking size and hash as it streams.
- **`dist/fixsys.img`** (`make fixsys`): a bootable rescue floppy with `sh`,
  `install`, `mount`, `disk`, `cp`, the libc and the keyboard drivers. It is
  built with `RAMDISCO=1` because it boots from a USB drive, where the BIOS can
  read and the kernel cannot.

### Big files over lines that drop: resuming

**tested on the real machine** — `cc1plus` is thirty-seven megabytes, and at
forty kilobytes a second that is a quarter of an hour of open connection: it
drops more often than not. Two measurements of the same file, 33,306,298 bytes
and then 37,550,104: different numbers, so not a ceiling but a drop.

Now `exhttp` declares a body shorter than promised instead of returning it as a
success, and knows how to ask for `Range: bytes=N-`; `netupdate` resumes the
half-finished `.new` instead of throwing it away, re-chewing the hash of the
bytes already written. And whoever asks for a piece **checks that a piece is
what arrives**: a server that ignores `Range` answers 200 with the whole file,
and appending that to half a file would be worse than the original fault.

### `ftpswap`: a directory and an FTP server, kept in step

**tested on real hardware on 16 September 2026** — `ftpswap /mydir` keeps a directory in step
with an FTP server: what is born on one side appears on the other, what dies on
one side dies on the other, and on a conflict the newer one wins while the loser
is kept aside as `<name>.prima`.

Five tests on the Acer against a real FTP server: the `.cfg` asked for and
written 0600 on the first pass, the tree replicated both ways, a file changed
on the server and read back on the machine, a 3000-byte binary that went there
and back **byte for byte identical**, deletion propagating both ways, and a
real conflict whose loser was found in its `.prima`.

The piece that decides everything is the **journal**: without memory, a file
present on one side only is always a new file to copy, and deleting becomes
impossible. And the journal keeps the dates of **both** sides, because on EX-OS
`utime()` changes nothing: a freshly downloaded file carries today's date, and
comparing the two clocks would send it up and down forever.

The FTP client ended up in `lib/exftp` — MLSD, MDTM, MFMT, PASV and PORT — with
`lib/exuser` as the precedent: two programs speaking FTP are two copies that
drift apart.

### NASM on the tools CD, and the other syntax

**tested inside EX-OS** — the CD carried `as` and `ld`, that is the chain the
compiler uses without anybody looking at it. Now it carries **NASM** too, and
it is not a duplicate: they are **two languages**.

| | `as` (GNU) | `nasm` |
|---|---|---|
| syntax | AT&T | Intel |
| made for | receiving `gcc`'s output | being written by a person |
| | `movl $4, %eax` | `mov eax, 4` |
| | `.ascii "..."` | `db "..."` |

! **And above all: this is an operating system.** Whoever learns to write one
starts from sixteen bits and a boot sector, and that stuff is written in NASM
ninety-nine times out of a hundred — including `boot/stage1.asm`, EX-OS's own
boot sector. A system that can compile its own C but cannot assemble an
`org 0x7c00` is crippled exactly where it should be strongest.

**It is the smallest port done so far — two lines — and the reason becomes
clear by looking at what NASM does *not* have: a target.** It produces every
output format (`elf32`, `bin`, `coff`, `macho`) always, and the choice is made
by whoever runs it with `-f`: there is nothing to teach it about EX-OS, because
`nasm -f elf32` already emits exactly what our `ld` links. All that was missing
was for it to **run here**:

```
autoconf/helpers/config.sub   'exos' among the accepted systems
nasmlib/path.c                __exos__ among those with Unix paths
```

> The second one showed up as an honest and, luckily, loud error —
> `path.c:204: 'separators' undeclared`. NASM picks its path style from the
> compiler's macros, and for a system it does not recognise it takes
> `PATH_UNKNOWN`, where `separators` is not defined at all. **The right line was
> not to make our GCC claim to be Unix** — it is not — but to say there that
> EX-OS has Unix-shaped paths: the slash as the only separator, no notion of a
> volume. That is true, and it fits in one line.

**There are two tests because they prove two different things.**
`prova-nasm.asm` is the twin of `prova.s` (which is for `as`): the very same
thing said in the two syntaxes, which is the shortest way to learn the
difference.

```
nasm -f elf32 /cdrom/prova-nasm.asm -o /prova-nasm.o
ld -o /prova-nasm /prova-nasm.o
/prova-nasm
Assemblato con NASM dentro EX-OS.
```

`prova-nasm16.asm` is the one that shows **why** NASM belongs on the CD:
sixteen bits, `org 0x7c00`, no linker and no operating system underneath. With
`ndisasm` the round trip comes back exact:

```
nasm -f bin /cdrom/prova-nasm16.asm -o /avvio.bin
ndisasm -b 16 -o 0x7c00 /avvio.bin
00007C0B  BE207C            mov si,0x7c20
```

`0x7c20` and not `0x0020`: that is `org` doing its job, and it is the thing
`ld` cannot do for you — there is no linker there to put the labels in the
right place. **`ndisasm` comes with it** and earns the megabyte it takes:
inside an operating system it is needed the first time you look at a boot
sector, or at the dump of a fault, without the source at hand.

> **And a breakage found along the way: the CD would not build any more.**
> `make iso` stops linking `/bin/provassl` with `libcrypto.a(o_str.o):
> undefined reference to 'errno'`, because as of today `errno` is no longer a
> global symbol but a macro calling `__errno_dove()` — and `libcrypto.a` was
> built before that. `tools/ricostruisci-bersaglio.sh --verifica` was already
> saying it at every `make iso` ("the libc has CHANGED since the last target
> rebuild"), but as a *warning*: that check looks at the shapes of types, not at
> symbols. **libcrypto has been rebuilt and the CD builds again**; the rest of
> the target — cc1, as, ld, fbc, the arithmetic libraries — is still the old
> one, and it is in `in_lavorazione.txt` as `@ABI-BERSAGLIO`.

### A thread's stack grows on demand

**tested** — a thread was born holding 64 KB of real RAM: sixteen pages
allocated and zeroed one by one, even for a thread that uses two hundred bytes
of stack. It was not an oversight, it was a price paid on purpose, and the
reason was written in the code: `page_fault_handler` could grow **one** stack —
the one in the current PCB — and faced with a missing page inside the thread
band it could not tell **whose** it was.

Now it can. Of a thread's slot only the TLS block and the first eight pages are
committed; the rest arrives when the thread really goes down, and stops at the
guard page below the slot. **Seven threads cost 980 KB instead of 1344**, that
is 140 each instead of 192 — and of those 140, one hundred and twenty-eight are
the task's *kernel* stack, which this work does not touch: the user stack went
from 64 KB to 12.

**The real question was not «how much», it was «whose».** Threads share memory,
so the one faulting inside a thread's stack may be **somebody else**: a thread
declares `char buf[16384]`, touches only its top and hands the bottom to a
companion — or to a `read()`. The bottom is not committed, the companion writes
there, and the fault arrives while *it* is running; its ESP says nothing about
that address, because it lives in another slot. As long as all 64 KB were
committed the case did not exist. That is why `pf_cresci_stack` became two
questions instead of one: **whether** the growth is legitimate and **whose**
the stack is, and only then `pf_cresci_pagine` commits the pages.

> **The «close to ESP» condition does not apply there, and it is not a
> surrender:** it would compare two different slots, that is a number without
> meaning. In its place there is an equally tight boundary — the address must
> fall inside the reserve of a **live** thread of the same group — and between
> one slot and the next the guard page remains, which no growth can step over.

**Two defects found by reading what was about to be touched**, both in
`proc_reap_zombie`:

- **sixteen pages lost for every thread that ends.** Slots are reused, but the
  pages of a dead thread stayed mapped until the end of the *process*: the next
  thread took the same slot and new pages were mapped over them —
  `paging_map_page` overwrites the entry silently — with the previous thread's
  data underfoot. Now the slot is taken down, and the test reads "reaped, 980 KB
  come back out of 980";
- **a page directory destroyed more than once.** It is freed "when nobody is
  left", and nobody was counted with `proc_gruppo_vivi()`, which does not count
  zombies; but when the group leader exits the threads become zombies *all at
  once*, and each was reaped holding the same pointer. The first destroyed the
  directory, the following ones walked it after it had been freed. **It is not
  proven** that this was the cause of the two rare defects already open — the
  panic inside `kfree`, the driver starting with a zero stack — but the shape
  is right: a bill that arrives elsewhere and much later.

**The tests are two new modes of `/bin/filiprova`,** and each of the three
parts of `pila` fails on its own: what a thread costs (the judgment line is at
160 KB — 128 of kernel stack plus *either* 64 of a fully committed slot *or* 12
committed little by little: a number in between does not exist), that it then
grows (forty calls of one kilobyte, five times what it was given), and that it
grows **at somebody else's hand**. `sfonda` goes down without end and demands
that the one who dies is the thread, on the guard page, with code -11, while
the process stays alive.

> **In the third part the waiting thread cannot call anything.** A `call`
> writes the return address *below* ESP, and the fault that follows commits
> everything between there and the part already live — that is, exactly the
> piece the test wants to leave empty. It waits spinning on a `volatile`
> variable. It is also why the case is rare in real life: a thread's live data
> always sits above its own ESP, and above ESP everything is already committed.

Two real defects, found while writing the test, stay declared: **a thread that
dies of a page fault does not take the process with it** (it dies alone, and the
program carries on, possibly with a lock held by someone who is no longer
there), and **the zombie of a thread nobody is waiting for is reaped by the
shell**, which returns to the prompt with the program still running.

### The «Cerca» box, and two boxes in the same bar

**tested, on the network, inside EX-OS** — searching already worked:
`html.duckduckgo.com` answers in plain HTML and the browser renders it. What was
missing was the *convenience* — you typed the engine's address by hand — and now
there is a **Cerca** box on the right of the bar: words, Enter, results.

There are three engines, chosen in **File > Impostazioni**: **duckduckgo**
(default), **wikipedia**, **marginalia**. **There are three because three
answer:** `google.com` sends ninety thousand bytes, three scripts and *zero*
result links inside the HTML — JavaScript builds the results, so no browser
without JS can ever see them — and Mojeek sends a Captcha. Listing them would
mean an entry that promises a search and returns an empty page.

**Enter does two different things, and the difference is the focus.** In a
toolkit text box Enter reaches the application as `EXM_TASTO` — the box lets it
through on purpose — but the message **does not say which box it came from**,
and only the toolkit knows where the focus is. Hence `ex_fuoco_chi()`, eight
lines: the alternative was guessing from which text had changed, that is,
getting it wrong the day somebody searches for the same thing twice.

> **And a toolkit defect that did not bite while the boxes were wide.** With the
> second box the address field shrank from 636 to 436 pixels, and on the first
> long page the defect was there in the screenshot: the address was **writing
> over the «Cerca» label**. `ex_scrivi` clips nothing and `CL_TESTO` never asked
> it to — the defect is not new, what is new is the first place where two
> controls sit that close. Now the box shows the **tail** and not the head,
> because its cursor is always at the end: whoever is typing must see what they
> are typing, not the beginning of an address they have already finished.

### The working directory belongs to both (and the environment already did)

**tested** — a `chdir` inside a thread is now seen by the others, and it is one
line: `filo->cwdt = capo->cwdt;` instead of copying the path. **It is the same
job `fdt` does for file descriptors**, with the same idiom: the PCB has the
`cwd` field and the `cwdt` pointer, which for a process points at its own field
and for a thread at the group leader's. The whole kernel uses `cwdt` — five
places in all — and **nothing changes between processes**: the directory stays
one per process, inherited from the parent at `spawn` as before.

**The reason is the same one that made the directory per-process, read
backwards.** Back then the defect was that `cd` inside one program moved every
other one; here it is that two threads **are one program**, and a function that
steps into a directory, opens a relative file and steps back does the right
thing or the wrong thing depending on which thread runs it — no error, just a
file opened in the wrong place.

**And the environment does not go through the kernel: that one was to be
checked, not built.** The list said "cwd and env are copied", and for `env` it
was not true: `environ` lives in `libc.so`'s data, which threads share because
they share the memory, and the PCB's `env[]` field is used by nobody. There was
nothing to fix; there was something to *look at*, which is not the same as
assuming it.

> **The test looks in both directions, and one alone would not do.** With the
> directory copied at creation, the "the main thread moves and the thread
> notices" direction would pass anyway whenever the thread is born *after* the
> change: it is the thread moving and the main thread having to see it that
> tells "shared" from "copied at the right moment". With the old copy put back
> for one run, both directions fail — and the environment passes in both cases,
> which confirms that half was never a kernel matter.

### Stopping a thread means asking it to

**tested** — killing a thread was already possible (the tid is a pid, and
`kill` works), but you **must not**: a thread killed wherever it happened to be
leaves the locks taken, files half open and structures as they were, and inside
a single process those are not its own — they belong to **everybody**. Now
there is the orderly way:

```c
while (!thread_devo_fermarmi()) { ...a piece of work... }
...release the locks, close what you opened...
thread_esci(0);
```

`thread_ferma(tid)` leaves a message, `thread_devo_fermarmi()` reads it where
the thread decides. **The kernel only does the two things that cannot be done
from outside**: putting the message where the thread will find it, and *shaking*
whoever is asleep — because a sleeping thread looks at nothing.

**Two words in the PCB, doing two different jobs.** `ferma` is the message and
it stays: a request read once is still true the second time. `scuoti` is the
shake and it **is consumed**, and it closes the one window this thing has:
between the moment the thread looks and the moment it falls asleep on a wait.
If the request lands in there the thread sleeps *after* having looked — the
same race as a lost wake-up, and the same cure: the asker leaves it written
that the next wait must not sleep, and `sys_attesa_dormi` finds it inside the
very `cli` that protects the expected value. One shake, not a "never sleep
again": a thread on its way out still has cleaning up to do, and with every
wait returning at once that cleanup would spin instead of sleeping.

> **And the test passed even without the thing it was supposed to prove** — the
> most instructive defect of the day, and it was *in the test*. With the shake
> taken out of the kernel for one line, the case meant to hit that window
> passed all the same: on a single CPU, creating a thread and stopping it right
> away always lands in one of the two easy cases — either the request arrives
> before the thread has looked, or with the thread already asleep. The case in
> between lasts a handful of instructions and **you never hit it by chance**.
> The cure is to widen the window on command instead of hoping: between the
> look and the sleep the thread yields the CPU and raises a flag, and the
> caller waits for that flag. Twenty races now cost **60-180 ms** with the
> shake and **20200 without** — twenty one-second deadlines, one per race — and
> the test fails. Before, the difference was zero.

**What it does not do, and it is written down:** the thread notices only where
it looks. A `semaforo_prendi` with no deadline is not a cancellation point, and
neither is a read from the keyboard or the network; whoever wants to be
stoppable there uses the timed variants. And reading the message costs a system
call: removing it would mean a word of ABI inside the TLS block, which is a
one-way decision nobody has measured yet.

### Condition variables and semaphores, on top of the sleeping wait

**tested** — the sleeping wait was the brick; now the two things you build on
top of it are there, and they are **eight libc functions, not one line of
kernel**: `condizione_aspetta` (plus the timed variant), `condizione_segnala`,
`condizione_segnala_tutti`, `semaforo_prendi` (idem), `semaforo_prova`,
`semaforo_lascia`. Both types are **a plain int**, like the lock:
`Condizione c = CONDIZIONE_ZERO;`, `Semaforo posti = 1;` — no init function,
which is the only shape you cannot forget to call.

**A condition variable is a counter of signals, and nothing else.** It does not
hold the list of who is waiting: that is already in the kernel, and it is the
queue of whoever sleeps on that address. Waiting is three lines — read the
counter, release the lock, sleep on that value, take the lock back — and **the
first one is the only one that matters**: between the release and the sleep
there is a window a signaller can walk through, and that signal would arrive
before there is anybody to wake. Having read the counter *first*, if somebody
signals in there the value is no longer that one and you do not sleep at all.
The window is not closed: it is made harmless.

> **Which is why you wait inside a `while`, never inside an `if`.** Waking up
> says "look again", not "it is here now": it can come from a signal, from the
> deadline, or because a signal had already gone by. Whoever checks once will
> eventually carry on with the condition false, and that is the defect that
> does not reproduce.

**A semaphore has no owner**, and that is not a licence: the one who releases
need not be the one who took it, which is exactly what a producer and a
consumer need — one consumes the free slot, the other gives it back. One cost
was left in, written next to the code: releasing always calls the wake-up, even
when nobody is asleep. The lock avoids that cost with its third state, but
there the number *is* the state of the lock, while here the counter counts
slots and has nowhere to put it. The way out exists (a second `dormienti`
field) and wants a new type in the ABI: **you do not pay for a new type for an
optimisation nobody has measured yet.**

> **The test is a one-slot queue, and it can fail in three different ways.** A
> thousand items between one producer and one consumer: with ten slots the two
> barely interleave and the test becomes almost sequential, with one slot each
> of the thousand items forces the other to wait. It watches the **sum**
> (losing one and reading another twice would give the same number of rounds),
> the **empty rounds** — and they must be *zero*, not "few": with a single
> consumer, whoever wakes always finds the goods — and the **stopwatch**, which
> is the witness for the empty rounds. Inside the wait there is a half-second
> deadline that is a *net*, not a way of working: without it a lost signal
> would be a machine stopped forever, and the test would not fail, it would
> just sit there. A thousand hand-offs cost between **20 and 60 ms** measured,
> one single deadline would cost 500: the judgement line sits at 400, well
> above the measurement and still able to tell the two apart.

### Threads: several flows inside the same program

**tested** — and the proof is not that the count adds up, it is that **without
the lock it does not**:

```
filiprova: 4 threads, 20000 rounds each
  with the lock   80000   expected  80000   exact
  without         20000   expected  80000   lost on the way
  hand-offs       80000   the threads really do interleave
```

Sixty thousand lost increments are four flows treading on each other in the
same memory. If the threads were fake — if `thread_crea` ran the function
inside the caller — that number would be 80000 like the other one, and the test
would have passed without proving anything.

**The difference between a process and a thread is one line: the page
directory.** `proc_create` allocates a new one, `proc_thread_crea` copies the
group leader's. Not one line of the scheduler was touched: same run queue, same
quantum, same `context_switch` — which already took CR3 as a parameter. And
`tgid` (the first member's pid) equals `pid` for a normal process, so all the
kernel that knows nothing about threads keeps working without a single `if`.

**File descriptors are shared by pointer, not by copy**: two threads opening
and closing files must see the same table. The PCB gained `fdt`, and the 153
occurrences of `->fds[` in the kernel became `->fdt[` with a mechanical
substitution — for a normal process `fdt == fds` and nothing changes.

**Stacks are not shared**: 64 KB per thread, in a band reserved for *every*
process at startup — even for one that will never make a thread. They are
addresses, not pages. The alternative (reserving it when the first thread is
born) would mean lowering the heap ceiling under memory the heap might already
have taken: either you refuse the thread, or you put its stack on top of
somebody else's data.

> **Whoever exits takes the group with them**, like `exit_group` on Linux and
> for the same reason: the other threads live in this process's memory, and
> letting them run while the address space goes away means code running over
> freed pages. Tested on purpose: a program that creates three endless threads
> and exits without joining them leaves the machine healthy and the prompt
> comes back.

**What is not per-thread, written down rather than discovered:** `__thread`
variables and `errno` are per *process* — the TLS block is shared. Giving each
thread its own means copying in the initial image from the ELF; zeroing it
instead would start a variable initialised to five at zero, silently. Between a
written limit and a wrong value there is no contest. And the lock spins yielding
the CPU: fine for a short critical section, not for waiting — a futex is the
next step.


**And then the per-thread TLS block.** In the first hour threads shared the
process's one; in the second that limit went too: **each thread has its own**,
at the top of its own stack — where those pages are already mapped and nobody
else can reach, which is also where glibc puts it. The initial image is **read
back from the file**, not copied from the leader's: copying that one would mean
starting the thread with another flow's *current* values — a half-updated
counter, a pointer to an object in use. The PCB gained the three `PT_TLS`
coordinates, and the executable is already open for demand loading.

> **The test starts from seven, not from zero**, which is the only way to tell
> "block copied" from "block zeroed": a zeroed block passes any test that starts
> at zero. Each thread checks it finds 7, writes its own number, yields twice
> and checks again; the main thread, at the end, finds its 42 untouched. What is
> left out is `errno`, which lives inside `libc.so` where `__thread` does not
> work — and it now has a written way out.


**And `errno` is per-thread too.** Inside `libc.so` you cannot write
`__thread` — there is no dynamic TLS — but the thread pointer can be *read*:
`%gs:0` holds a different number for each thread, good as a key into a
sixteen-slot table where the slot is claimed with `xchg` and not with "if it is
free then write it". That is why the TLS block is now made for *every* process,
even those without a single `__thread` variable: cut down to the TCB alone,
eight bytes in a page — without it that descriptor's base is zero and
`movl %gs:0` does not give a wrong value, it gives a page fault at address 0.

> **The defect that came out of it is worth more than the feature.** With the
> table in place, `close(999)` started answering `errno 0` on a call that
> always fails: **inside `libc.c` the word `errno` is not the macro**, because
> that file does not include `libc.h` — it says so at the top, it compiles
> without `-I lib/include`. `err_posix` was writing the global while the
> program read its thread's slot. And the test that caught it had been rewritten
> on purpose: the first version made the main thread fail with a call that
> answered zero, and zero never changes — it would have passed forever without
> testing anything.

**And the most instructive defect: a half-built task being run.** With
per-thread TLS added, the test program died *one time in three* with a page
fault at the entry of the thread function. The diagnosis came in a single run by
printing the numbers: the thread faulted **before its own creation line was
printed**, with the context `proc_create` had built for it — ESP at zero. In
between there was a `vfs_read`, that is, a call **that can block**: while the
group leader waited for the disk, the scheduler was running a thread that was
not finished being built. The rule that follows holds for any kernel: **between
creating a task and the moment it is ready there must be nothing that can
block.**


**And the wait that really sleeps.** `attesa_dormi`/`attesa_sveglia`: a thread
leaves the scheduler's queue and comes back when somebody calls it — by the
**address** it stopped on, not by pid, which is what lets a lock be just an
integer, with no queue of names to remember.

**The expected value closes the race**, and that is why the call takes three
arguments instead of two: between the moment a waiter looks at the lock and the
moment it falls asleep there is a window, and a wake-up arriving in there would
be lost — the thread would sleep forever. The comparison is done by the
*kernel*, with interrupts off. And the page is touched *before* turning them
off: reading user memory can trigger a page fault that wants the disk, and a
disk awaited with interrupts off is a stopped machine.

**The lock has three states** — free, taken, taken-with-sleepers — and the third
exists for whoever *releases*: without it they would have to call the wake on
every unlock, just in case somebody is asleep. With the 2, a releaser holding a
1 knows nobody is there: **with no contention the lock does not cost even one
system call.**

> **The test is a stopwatch, because nothing else tells them apart.** A wait
> that spins and one that sleeps look the same from outside. Two measurements
> with opposite outcomes are needed: with nobody waking and a 300 ms deadline it
> came back after **310 ms** — it slept; woken by a thread after 100 ms with the
> deadline at 2000, it came back after **100 ms** — the thread woke it, not the
> clock.

### The editor shortcuts, and something I had written wrong

**tested** — a line typed in the editor and only Ctrl+S pressed: the status line
says "saved", and the file read back from the shell contains it. It was the last
item on EX-IDE's list: the "Source" window's menu had promised Ctrl+S, Ctrl+C,
Ctrl+V, Ctrl+X and Ctrl+F since day one, and nobody had ever wired them.

**The toolkit does not eat them, on purpose.** In `exwin.c`, in the key path:
"for every other control a Ctrl+letter is an application shortcut and must not
be eaten" — the only exception is the terminal, where Ctrl+C is byte 3 and must
reach the pty. The Ctrls were already arriving; what was missing was someone
looking at them.

> **And what I had written in the manual was wrong.** Under "Text area" it said
> "with cursor, selection and clipboard (Ctrl+C, Ctrl+V, Ctrl+X)", as if the
> keys were the toolkit's doing. The toolkit provides the *functions* —
> `ex_area_copia/taglia/incolla` — and leaves the keys to whoever writes the
> program. The line now says so, and says how it is done: exactly what someone
> writing their own editor with EX-IDE needs.

**The repaint is manual, from the keyboard.** That is this job's trap: pressing
"Cut" in the menu makes the toolkit repaint the window as the dropdown closes,
so the changed text shows; from the keyboard nothing closes, and without an
explicit `EXM_DISEGNA` the text changes and the screen stays as it was. The two
paths go through the same functions and do not have the same surroundings.

### Cut and paste: a control moves between forms

**tested** — copied and pasted within the same form, then cut, form switched and
pasted: the drawing file shows the control moved under the other window, same
position and same name. It was the last big item left in EX-IDE, and its real
reason was not copying: it was that a control placed on the wrong form could
only be deleted and redone by hand over there.

**The drawing clipboard is not the system one.** It holds a *control*, not text:
ExWin's own clipboard carries characters and the editors already use it to pass
pieces of source around. Putting a control in there would mean inventing a
textual format for a rectangle and having it read back by whoever else writes
into it meanwhile. In the main window Ctrl+C talks about the drawing, which is
what that window is.

**The name and the id are not copied, they are made anew**: they are unique
across the whole project because they become `ID_...`, `h_...` and a function
name inside `finestra.h`, which is a single file. With a pleasant consequence
that was not aimed for — on a cut the name becomes free again and the control
takes it back: **a move renames nothing**.

**And the code is not copied at all.** The original's handler stays the
original's; the copy will get its own, empty, on the first double click. Copying
the body too would mean exide writing into `finestra.c` things nobody wrote —
the one rule this program never breaks.

> **Within the same form the pasted control shifts by eight pixels, in another
> one it does not.** Placing it exactly over the original would hide it: you
> would see one control and there would be two, and the click would always take
> the top one. In another form that spot is free, and it is exactly where you
> want it.

### Redo: the same function with the two stacks swapped

**tested** — undone, redone, and the case that matters most verified: after an
Undo, a new change discards the redo branch.

**What you undo is not thrown away, it is moved to the other side.** Two stacks
instead of one, and *a single function that swaps them*: `passo(from, to)` takes
the present, pushes it onto `to`, and restores the drawing on top of `from`.
Undo is `passo(back, forward)`, Redo is `passo(forward, back)` — two lines each,
and the day a field is added to the drawing there is one place to remember it.
For the same reason capture and restore became two functions instead of the
three `memcpy` blocks copied into the three places that use them.

**A new change discards the redo branch**, and that is the only rule this thing
needs: if after three steps back you draw something, that "forward" is a future
born of a past that no longer exists, and keeping it would mean a Redo that
restores a drawing which never existed. Every program does it this way, and this
is the reason — not habit.

> **The stack slides when full**, instead of refusing the new snapshot: the
> oldest step is the one needed least, and losing the most *recent* one would
> mean an Undo that does not undo the last thing done. Cost measured with
> `size`: exide's BSS goes from 140,384 to 261,888 bytes — a hundred and
> twenty-one kilobytes, the second stack of sixteen snapshots, zeroed memory and
> not bytes in the binary.

### Search actually works, and no browser port was needed

**tested** — live, over HTTPS: `html.duckduckgo.com/html/?q=exos` opens in the
EX-OS browser and shows the results, with titles, addresses and snippets.

The question was whether to port Firefox, or NetSurf, in order to search. The
answer came from five HTTP requests rather than an estimate — one per engine,
with EX-OS's real User-Agent:

| engine | what it answers |
|---|---|
| google.com/search | 200, 91,980 bytes, **three scripts and zero result links**: the results are built by JavaScript |
| mojeek.com/search | 200, `<title>Captcha</title>` |
| html.duckduckgo.com | 200, 33,784 bytes, **ten results in plain HTML** |
| marginalia, wikipedia | plain HTML |

**So NetSurf does not solve Google, and that is not its fault**: NetSurf does
not run JavaScript, and Google sends result HTML to nobody. Porting a
third-party engine — months for NetSurf, a second operating system for Firefox,
which without threads and without Rust does not even start — would not have
moved the actual problem an inch.

**Then the real defect.** The DuckDuckGo page arrived (TLS fine, `200, 31671
bytes, 738 nodes`) and the screen stayed blank. The status line also said "style
truncated", and that was the answer: the same page saved locally *without* its
stylesheet drew immediately. **DuckDuckGo's stylesheet is 105,607 bytes and the
cap was 24,576**: the browser read a quarter of it and stopped halfway through a
rule, and with half a stylesheet applied the page vanished.

The three numbers that actually mattered — 1652 selectors, 2207 declarations,
105 KB — against caps of 600, 2000 and 24 KB. Raised to 2400, 5000 and 160 KB:
they cost **284 kilobytes of BSS** on a program that already had 5.2 MB,
measured with `size` rather than guessed.

> **You can search from your own browser without impersonating anyone.** No
> Chrome-identical fingerprint was needed, no human jitter, no third-party
> engine: what was needed was an engine that answers in HTML, and a raised cap.
> And the third result DuckDuckGo returned was `github.com/exagonx/EX_OS` —
> this project.

### The layout leaves browser.c, and the proof is that you cannot see it

**tested** — the same page photographed before and after: ten rows of pixels
differ out of six hundred, from 582 to 591, and they are **the clock** in the
taskbar. Above that, nothing. First of the two steps towards a formatted-text
library: split the file before splitting the library.

```
browser.c            6749 -> 4824 lines
browser_impagina.c        1836 lines   (the layout, moved out of it)
browser_priv.h             222 lines   (the seam, which did not exist before)
```

**The machine was asked where to cut.** Before moving a single line I had a map
of the file built — a hundred and thirty-eight definitions, who calls whom, who
touches which global — and the layout group fell out by itself: twenty-eight
functions, 1588 lines, *contiguous*. By eye, over six thousand lines, it was
not visible.

And above all it produced the measure of the cut, which was the real question:
21 shared variables, 11 functions asked of the browser and **only 3 offered
back**. Outwards the layout is nearly closed; what ties it down are not the
calls but the variables — exactly the kind of tie you cannot see while
everything lives in one file.

> **Three script mistakes, all of the same kind.** For a one-line function the
> signature was cut at the *last* parenthesis, which sits inside the body: an
> open brace ended up in the header. A declaration ending in a comment instead
> of a semicolon swallowed the next line. And globals declared several per line
> were not seen at all. Every time: restore the file, fix the *script*, redo the
> cut from scratch — fixing the output instead of the script would have meant a
> cut nobody can reproduce, and this cut must be reproduced on the day of step 2.

### The EX-IDE manual becomes a page, with an index

**tested** — Help > Manual opens the browser on `/exwin/doc/exide.html`, and a
click on the index lands exactly on the paragraph. Twenty thousand bytes,
twenty-nine `id`s, fifty-six internal links.

**You do not rewrite a viewer**: it is the same decision by which "Directory"
does not rewrite a file manager but launches `filemgr`. The browser is there,
it lays out, colours and follows links, and already does so for the other nine
pages of the guide. The EX-IDE manual has become the tenth page of that set —
same navigation bar, same stylesheet, one row in the documentation index — and
the other pages' bars now name it.

**The examples are shared, and that is why the index was needed.** Swapping two
text boxes is as much a *Text box* example as a *Button* one; quitting with a
confirmation counts for *Check*, for *Button* and for "how you quit". Repeating
it under each would mean three copies to keep in agreement; putting it under
only one would mean whoever looks for the other never finds it. So the examples
all live at the end, one `id` each, and **every tool links to them** — and every
example says which tools it covers. It is the only structure where the same
thing is written once and reachable from every place you look for it; without
anchor jumping it would have been a list of titles and "scroll until you find
it".

> **The in-program manual stayed**, and that is why it was written: a manual
> that lives in a file is a manual that one day is not there — the `/exwin`
> component not installed, a CD half mounted, only the binary copied. It is now
> the second choice instead of the only one, and its first line says where the
> good one is. But they are two copies of the same text and will drift: the way
> out, written down among the open items, is to shorten the internal one to a
> reminder and leave the long text to the page.

**And the in-program manual became a reminder**, from 289 lines to 61: how to
start, the four files, the windows and the table of tools with their events.
The examples, the properties one by one and the menu live only in the page —
they are the long part, the part that drifts first. The exide binary loses
eleven kilobytes, and the reminder's first line says that if you are reading it
the page was not there.

### Anchors: a link that goes to a point in the page

**tested** — four cases inside EX-OS: an anchor in the same page, one that does
not exist, an address with a fragment typed in the bar, and a link that changes
page *and* lands on the paragraph. It was needed for the documentation: a long
manual with an index at the top, where you press an entry and find yourself at
the explanation.

The browser did not go there, and had been saying so for months in a comment:
"this browser cannot jump to a point inside a document yet; until the jump
exists, doing nothing is the most honest answer". Now the jump exists.

**The piece to restart from is found from the node, not from the text.** Every
laid-out piece already knows which document node it came from — a field added
back when a script needed to be told *where* a click landed — so the jump is
one walk over the tree (the element with that `id`, or an old `<a name>`) and
one over the layout, without laying the page out a second time. An anchor in
the current page reloads nothing: reloading in order to jump would mean a
network round trip, the tree rebuilt and hand-filled forms cleared, all to move
a scrollbar.

> **The defect needed a measurement, not a guess.** On the first run the jump
> landed two lines below the heading: it looked like a wrong margin, or the
> font baseline, or line rounding — three plausible guesses, all wrong. Instead
> of trying them I printed the numbers on the status line: `node 86, piece 152,
> y 870, scroll 0`. The piece found was the right one, it was the heading.
> **A piece's `y` is already in window coordinates**, not document ones: layout
> starts below the address bar, so scrolling to the piece's `y` puts that piece
> *behind* the bar and you see the line after it. With the numbers in hand it
> took two minutes.

### Undo: photograph everything, do not record what changed

**tested** — three edits of three different kinds, each undone, and at the end
the drawing file read back from the shell **identical to the starting one**.
Sixteen steps back, with Ctrl+Z or Edit > Undo.

**The whole drawing is photographed before every change**, instead of recording
what changed. The alternative means writing the inverse of each operation —
placing a control, deleting it, moving it, resizing it, changing one of its
eight properties, changing one of the form's four, adding a form, removing one
that takes its controls with it: nine inverses, each wrong in its own way, and
the wrong ones surface a month later. "Put everything back as it was" cannot be
wrong: it is a copy. And the drawing is small enough for that to make sense:
sixteen snapshots fit in about a hundred kilobytes of zeroed memory, which never
reach the binary.

**One photograph per drag, and only if something really changed.** A drag sends
dozens of events: photographing each one, a single movement eats all sixteen
steps and you go back half a pixel at a time. Photographing at the *start* of
the drag instead, a click that only selects would leave a step that does
nothing — and an Undo that does nothing is worse than no Undo, because whoever
presses it thinks it is broken. The photograph is taken at the first real
change.

**And the history does not cross projects**: opening another one and pressing
Undo would put the previous project's controls back on the form, with their
names and their ids — a drawing that never existed, ready to be saved over the
real one.

> **The shortcuts were labels.** The menus had promised Ctrl+N, Ctrl+O, Ctrl+S
> and Ctrl+Q since day one and pressing them did nothing: nobody had ever wired
> them. For Undo the shortcut matters more than for the others — you undo right
> after the mistake, hand still on the keyboard, not by opening a menu — so they
> were all wired at once. The ones in the "Source" window are still labels, and
> now that is written down where the missing things are kept.

### The handles can be pulled, and the manual actually explains

**tested** — inside EX-OS, pulling every kind of handle and reading back the
numbers that end up in the drawing file. Controls can be resized with the
mouse: the eight handles had been drawn since day one and were good for
nothing.

**The problem was not pulling them, it was grabbing them.** Half of every
handle falls *inside* the control, and the click looked for the control first —
the natural order: so a click on the corner started a move and the handle could
never be taken. Handles are now looked at first. And the little square you see
is 5 pixels — any bigger would cover the control — while the mouse target is
11: you aim at the square and grab it anyway.

**You pull an edge, not a size.** Pulling the left handle changes `x` **and**
width together, because the right edge must not move: changing the width alone
would make the control slide right while you drag it left. The same goes for
the limits — the minimum size stops the edge being pulled instead of shortening
from the other side, or the control would run away the moment it hit the
minimum.

**The in-program manual became a manual.** For every tool it now says what it
is for, which events it has and **which functions drive it** from `finestra.c`:
`ex_acceso`/`ex_accendi` for Check and Radio, the six `ex_lista_*`, the six
`ex_voce_*` that Combo and Tabs share, `ex_scorri_*`; plus the properties one
by one — what each becomes in the generated code — and two complete examples.
One was there: two boxes swapping their text, now with the reason the scratch
copy is really needed (`ex_testo_prendi` returns a *pointer* into the control's
text, not a copy). The other was missing: **a button that closes the window**,
which in the main window is `ex_esci(0)` and in a secondary one is not — there
you call the generated procedure with `EXM_CHIUDI`, which destroys the window
*and* zeroes the handles of its controls.

> **An old defect, found while writing a new one.** Adding the repaint to the
> resize turned up that *moving* had never had one: dragging a control changed
> `x` and `y` and nobody repainted the canvas, so the control was seen jumping
> to its new place only when something else caused the window to redraw. Both
> drags now end with a repaint.

### More than one window: the designer learns to count

**tested** — from the drawing to the running program: two windows drawn,
generated, compiled with real GCC inside EX-OS, and the second one opening when
a button on the first is pressed. An exide project could draw **one window
only**; now it draws eight.

**The drawing format did not change to make room.** It already had the right
shape — one line for the form, then the lines of its controls — and all it took
was letting there be more than one form:

```
F principale 400 260 prg6
c etichetta Etichetta1 1001 76 52 90 16 0 Etichetta1
F finestra2 400 260 Finestra 2
c spunta Spunta1 1003 76 124 140 20 0 Spunta1
```

The old line was called `f` and **is still read**: it had no name — it did not
need one, there was a single form — and slipping one in the middle would have
made the first word of the *title* be read as the name. So the new line got a
letter of its own. Projects made before open without noticing a thing.

**The main window keeps the names it always had**, and the others do not:
`g_form`, `finestra_crea()`, `finestra_proc()` against `g_form_opzioni`,
`opzioni_crea()`, `opzioni_proc()`. Naming them all alike would be more
symmetric, and **every project made before today would stop compiling**: its
`finestra.c` — the one the IDE never rewrites — calls `finestra_crea()` from
main. Secondary windows are opened by your code, usually from a button handler.

Three details the generator writes and a hand-writer forgets: calling
`<nome>_crea()` twice **does not open two windows** (a button gets pressed more
than once); closing a secondary window **does not quit the program**, only the
main one does; and on closing, the pointers to its controls **go back to zero**,
because `ex_distruggi` takes the children with it and those names would point
at nothing.

> **One form at a time, and not an MDI container — against what I had written
> in the to-do list myself.** The MDI had been in the toolkit since the day
> before and was the marked road; the numbers decided otherwise: the designer's
> canvas is 436x396 and a form is born 400x260. Two windows that size in there
> cover each other almost entirely — you would spend your time dragging them
> apart to see the one underneath, to gain seeing at once two things you work on
> one at a time anyway. It stays the right road the day the canvas grows, or the
> day you want to drag a control from one window into another.

### Save as, Replace, and a name that lagged behind

**tested** — inside EX-OS, with the file read back from the shell before and
after, in the same machine run. The two entries that were at the top of
exide's to-do list.

**"Save as" copies the tree, it does not regenerate the drawing.** It could
have meant two things: rebuilding `finestra.dis`, `finestra.h` and
`finestra_gen.c` inside a new directory, or copying everything and carrying
on in the copy. The first is easier to write and **throws away
`finestra.c`**, the one file of the four the IDE does not own: it is the
user's, the one where the IDE appends the missing handlers and never rewrites
anything. A "save as" that loses the bodies of the functions is not a save.
So the whole tree is copied — `src/`, `inc/`, `lib/`, `bin/`, `obj/`,
`progetto.txt`, `compila.sh` — after saving any open editors, and **the
original is left untouched**: it is a copy, not a move.

**"Replace" changes one occurrence at a time, like Find.** In source code the
same sequence of characters lives inside strings, inside comments and inside
identifiers: `msg` is also part of `message`. A "replace all" changes all
three silently, and whoever ran it finds out at compile time, or later. It is
in both editors — the source one and the file editor — and in the toolkit
`ex_area_riga_metti()` was born, which rewrites one line in the middle of the
document: sixteen lines that go through `area_tocca()`, so the colouriser's
state chain is invalidated from that row down. That is exactly why such a
primitive belongs in the toolkit: an application rewriting the row from
outside would leave the old colour behind, and only sometimes.

**And then rereading found what the happy path never touches.** Three defects,
all in the same blind spot — the paths where something goes wrong. The worst:
**copying a file onto itself deletes it, and the copy reports success**. Not a
suspicion, it is in `kernel/fs/vfs.c`: opening with `O_TRUNC` empties the file
without checking who else holds it open, so the read that follows finds zero
bytes, the loop never runs, and the function returns success. Anyone typing
the current project's path into the dialog field ended up with every file at
zero and a status line saying "project copied". The check is now in two
places, and a directory that already holds a project is not silently
overwritten. Alongside it: no return value from `mkdir` or from the copy was
ever checked — on a read-only disk exide moved to the new directory anyway,
one that did not exist. Now there is the same write test "New project" uses,
the copy counts the files that did not make it, and **if even one is missing
the IDE does not move**: half a copy plus an IDE pointing into it means the
next Save turns it into the original.

> **A defect found by the test, not by rereading the code.** `progetto.txt`
> is copied as it was, `nome = ...` line included, and the project card was
> reading the name from there: the directory was `prg6-copia` while the card
> still said `prg6` — and the window title, which derives the name from the
> directory, said the right one. Two places answering the same question
> differently; the same would have happened renaming the directory from
> outside. The cure is not to keep them in sync: it is to remove one. The name
> now **always** comes from the directory, and the file is still written, so
> the first Save puts it right by itself.

### Files and Directory: one new window, and one that did not need writing

**tested** — listing, opening, editing, saving, closing again, and checking
against the real file, inside EX-OS. The last two Strumenti menu entries that
still said "coming soon": with this round the menu is complete.

**"Files" is not the "Source" window**, and that is a choice, not a
shortcut. "Source" knows exactly three names — `finestra.c`,
`finestra_gen.c`, `finestra.h` — and half the program is written knowing they
are those three; slipping in a fourth arbitrary name would have meant
carrying that certainty everywhere. A separate window, with Save/Find/Close
and nothing else, costs less and risks nothing to what already works.

**It is scoped to `<project>/src` only**, not the whole tree: it is the only
reading of "files" compatible with "a double-click opens it as source" — a
double-click on a `.o` inside `obj/` would open nothing readable. The
colouriser is chosen from the name (`.c`/`.h` get `ex_colora_c`, everything
else none) and is **explicitly turned off on every open**: the same area
stays alive from one file to the next, and without resetting it a `.txt`
read after a `.c` would appear coloured as if it were C.

**"Directory" does not rewrite a second file manager.** exide knows how to
draw rectangles and lists; it does not know how to copy, move or delete files
safely — `filemgr` already does, with the same tree-plus-list layout this
entry has promised since the original request. Twelve lines look for
`filemgr` (first in `/exwin/bin`, then `/cdrom/exwin/bin`) and launch it with
the project directory as an argument — the same one it already accepts from
the Applications menu.

> **And search was not rewritten a second time.** The file editor wanted a
> "Find" too, identical to the one already proven in the "Source" window —
> only *which* area and *which* status line changed. `ed_cerca()` became
> `area_cerca(area, status)`, and the original stayed a one-line wrapper: two
> copies of the same loop would have been two copies to keep in step for the
> exact same algorithm.

### The project card, and a rewrite that should not have happened

**tested** — written, closed, reopened: everything came back. And a real
defect found by testing exactly that.

**It reads the same `progetto.txt` a project already writes at birth.** Not a
second format: five `key = value` lines — name, author, version, created,
description — followed by a `[nota]` marker after which everything, blank
lines included, is the note's free text.

**The name and creation date are not editable**, and that is not an
oversight: the name comes from the directory — rewriting it here would not
rename anything — and a creation date, by definition, cannot be corrected
without ceasing to be true. They are labels, not boxes. **Closing the window
saves on its own**, like the editor: it is a low-risk data card, and asking
for confirmation on low-risk information is a question people learn to
dismiss without reading.

> **"New project" pointed at an already-existing directory used to wipe the
> card.** The drawing (`finestra.dis`) was opened without truncating — a file
> already there stayed exactly what it was — but `progetto.txt` was always
> truncated: the same action treated two files of the same project two
> different ways. Found by restarting to test *reopening* a project, not just
> saving: the file on disk was already correct, checked with `cat` before the
> restart — it was the next step, not the save, overwriting it. The fix:
> check first whether the file already exists, and write only when it is
> missing.

### The compiler window: real GCC, inside EX-OS, from a drawing

**tested** — a real project, drawn in exide, compiled with GCC from the tools
CD *inside EX-OS*, linked and **launched on the desktop**.

**The compile line ends up in a file, and the file is the product.**
`compila.sh` lives in the project directory, can be read, hand-edited and run
like any other script: the «Compila» button does nothing you could not do by
typing. **Output goes to a file** (`obj/compila.log`), not a pipe — it can be
reread at leisure, which is what someone reviewing an error needs.

**A shared library links through its stub**, not an archive: in EX-OS a `.so`
is never linked, a few lines get compiled into the program that resolve names
on first call. The Libraries window picks which stubs join the command line —
`exwin` always, the rest by checkbox.

**And it waits without dying**: cc1 weighs forty megabytes, and a window
frozen for a minute looks hung. It polls whether the child is done with
`WNOHANG`, dispatches a handful of messages, sleeps an instant — the same
shape as the browser's network wait — with a five-minute ceiling past which it
stops waiting for a compiler that hanged.

> **Two defects found by running the real compiler, neither visible reading
> the code.** Options came out truncated halfway — the field is a "testo"
> control holding 63 characters, not the C buffer's 160 — and the cut
> produced an error that looked nothing like its cause: `-fno-pie` mangled
> into a lone dash, and to gcc a bare `-` means "read from stdin". The fix
> was not widening the box: it was **removing** from the box what did not
> belong there — the mandatory flags (`-ffreestanding -fno-builtin -nostdlib
> -fno-pic -fno-pie`) are now fixed in the program, not editable by accident.
> The second: the child compiled in the wrong directory — the one exide had
> started from, read-only — because `spawn_ex` inherits the parent's *cwd*
> and nothing changed it before launching.

### Three defects found by using it, and none was where it seemed

**tested** — on the browser, on exide booted from CD, and on a disk installed
from scratch.

**The menu drop-down vanished when the mouse moved.** It was not the server: it
was the drawing order. The base procedure draws the controls and *then* the
drop-down, but an application that also draws its own — the browser with the
page, exide with the form — does so **after** calling the base, and paints over
it. With the mouse still you never noticed: it is movement that delivers
`EXM_MOUSE_MOSSO` and hence a repaint. The drop-down is now drawn inside
`ex_aggiorna()`, the line with which *anyone* says "I am done drawing, show it":
whatever was drawn, the drop-down comes after.

**exide opened an empty editor.** "`mkdir` failed" does not mean "it was already
there", and the program believed it did from day one: booted from CD nothing is
writable, project creation failed at every step *silently*, and the editor opened
on a file that had never been written. The proof that counts is **writing, not
asking**: now it tries, and if it fails it says so and stops — explaining that a
read-write mounted disk is needed.

**The installer asked for the language and left the keyboard American.** The
installed system spoke Italian and typed American: letters in the right place and
punctuation not. The language table now has a column for the layout — **a
keyboard is not a language**, English is typed on a `us` and tomorrow someone
will want English on a `uk` — and `keymap` is *overwritten*, unlike every other
setting: that value was nobody's choice, it was the installation medium's, copied
a moment earlier.

> **And the best proof is a typo.** From the freshly installed disk the driver's
> commands come out crooked: `keymap -p` becomes `keymap 'p`. The tool sends
> American scancodes — if the punctuation comes out shifted, there is an Italian
> keyboard in there.

### EX-IDE: draw a window, out comes C that runs

**tested** — from the CD in QEMU with an ext2 disk mounted, and the proof that
counts is not a screenshot of the form: it is an 18 KB binary, **generated from
the drawing**, that opens inside EX-OS with the controls where I had put them
with the mouse.

```
drawing -> finestra.dis -> finestra.h + finestra_gen.c -> gcc -> ld -> runs
```

**Three panes, like Visual Basic**: the toolkit's fourteen tools on the left, the
form in the middle, the selected control's properties on the right. Double-click
a control and the editor opens **inside the function its event will call**.

**Four files, and only one is yours.**

| file | who writes it |
|---|---|
| `finestra.dis` | exide only: the drawing |
| `finestra.h` | exide only: the ids, the handles, the prototypes |
| `finestra_gen.c` | exide only: creates the controls and dispatches events |
| `finestra.c` | **you only**: exide *adds* the handlers that are missing and never rewrites what is there |

This is where VB6 broke — one file, half written by the generator and half by
hand, and every regeneration was a gamble. Here the generated and the written
never touch.

**The joint is the id, and it was already there.** `ex_crea(..., parent, ID, 0)`
gives every control a number and the event comes back as `EXM_COMANDO` carrying
it: in the drawing the button *is* `ID_PULSANTE1`, in the source there is `case
ID_PULSANTE1:`. exide is feasible here more than elsewhere because ExWin was
already shaped like VB6.

**And a toolkit defect, found because it needed it.** Lists and text areas did
not release their slot when destroyed: a window containing a list, opened and
closed four times, used up all four slots and on the fifth the list would not
open — no error, just an empty box. It had been there for months and nobody had
found it, because no program opened and closed the same window often enough. The
first one to do so was exide, on the day it was born.

> **The linker script was born empty, and the link succeeded anyway.**
> `open(f,'w').write(open(f).read()...)` truncates the file before reading it;
> `ld -T` with an empty script does not complain — it throws everything away and
> produces a 256-byte ELF with no segments. The symptom was "ELF load failed" at
> runtime, which looks nothing like its cause.

### Windows inside a window: the MDI container

**tested** — from the CD, in QEMU, with `winprova -m`: three windows in one
container, each with its own procedure, and every command on the serial line with
the **name of the procedure** that received it.

**A child window is not a server window**, and everything else follows from that.
Giving each one a real window would mean a shared memory zone apiece, a round of
requests on every open, and — above all — windows that can leave the container,
because the server does not know they should stay inside. Here a child is a
*control*: pixels inside the parent's zone, drawn by the library. The price is
stated — drag one and it stops at the edge — and that is exactly what an MDI is
for.

**Clipping, which four months of toolkit had never needed.** Drawing was clipped
against one thing only: the top-level window's pixel zone. That was enough,
because a control sits where you put it and does not move. An MDI window *moves*,
and a control larger than its client area would draw over the frame — a defect
you only see while dragging, which is to say late. Now every child is drawn
clipped twice: its frame inside the container, its contents inside its own client
area. It costs one comparison per pixel when on, and **nothing when off** — that
is, in every window that does not use MDI.

**Activate-then-act is a single gesture.** Click a button in a window that is
behind, and that button is pressed: the window comes to the front *and* the click
lands where it fell.

**Commands go to the child's procedure**, not the application's: that is what
makes MDI useful rather than decorative. And if the child has no procedure they
go to the application as always.

**Closing a child does not close the program** — the distinction lives where
whoever does not handle the close ends up, or that button would shut the whole
application down on the first click. And **Tab cycles inside the active window**:
otherwise you would watch a cursor blink in a window you are not looking at.

> **For the second time in one day the eye was wrong.** After F6 two windows
> looked active; measured, the pixels were `(30,77,125)` and `(128,128,128)` —
> active and inactive, exactly. The rule, now that it has cost twice: look at a
> screenshot to work out *where* to look, then count the pixels.

### Coloured text, and a second text area that was never born

**tested** — from the CD, in QEMU, with `winprova -c`: a piece of C chosen to
touch every role, and the colours checked by reading the **pixels** of the
screenshot rather than looking at it.

**The most important part is the part that was not written.** The obvious route
was a new control with its own cursor, its own selection, its own clipboard, its
own click-to-position: a thousand lines already written once for the text area,
and the second copy would have been a copy to keep in step forever. `areacodice`
**is not a new control: it is the same area with two different numbers** — 3000
lines of 240 columns instead of 512 of 200, plus a colouriser. The class is the
same, and past `ex_crea` there is no difference at all: same drawing, same keys,
same `ex_area_*`.

**The colouriser lives with whoever knows the language, not in the toolkit.** The
control knows how to *draw* coloured text; which words are keywords and where a
comment ends is known by whoever writes the editor. The hook receives a line and
fills in **one role per character** — keyword, type, string, number, comment,
preprocessor, function — and roles are roles, not colours: the palette is a
single table, so two editors do not colour keywords two different blues.

**And a block comment crosses lines.** The colouriser also returns *how the line
ends*, and the control keeps that state: one byte per line. It is recomputed only
from where the text changed downwards — from scratch on every repaint would be
three thousand calls for every keystroke. Type `/*` halfway through a document
and the lines below turn green as you type.

**The cursor can now be moved**, where before it could only be read:
`ex_area_vai` is what a column listing a source file's functions needs — click a
name and land on that line, at mid-height, with its context around it.

> **An hour lost reading a screenshot.** The probes said "comment" and the screen
> seemed to disagree; the defect was hunted in three places before measuring the
> PPM's pixels and finding `(0,128,0)` there — the palette's green. The lines had
> been green all along: scaled down at 800x600, a comment's green and a keyword's
> blue look alike. A screenshot is looked at; its numbers are counted.

### The toolkit learns five controls, and where new ones go

**tested** — from the CD, in QEMU, with `winprova -n`: every command lands on the
serial line with its id and value, which is a number to compare rather than an
impression.

**They were missing, and the gap was already written in the code.** Next to the
browser's settings window stood: "the toolkit has no check box, and drawing one
by hand in here would mean a control that lives in a single program". That is why
the settings switches are buttons that change their label. Now there are
**`spunta`, `radio`, `scorrimento`, `combo` and `tab`** (check box, radio,
scrollbar, drop-down and tab strip) — the first step towards `exide`, the visual
development environment.

**A scrollbar's orientation is told by its shape**, not by a style bit: wider
than tall is horizontal, taller than wide is vertical. One more bit could
disagree with the size, and then you would have to decide which of the two is
right.

**A radio's group is its siblings** — the controls with the same parent. There is
no group to declare: two sets of choices go inside two `riquadro` frames, which
is how they have always been drawn. The frame you see *is* the group that counts,
so the visual rule and the logical rule cannot disagree.

**A check box arms on press and fires on release**, like a button: sliding off
before lifting cancels. **A scrollbar acts at once instead** — an arrow held down
must scroll — and dragging its thumb wakes the application on every pixel, where
a dragged list says "I chose" only once: a dragged scrollbar *is* the document
scrolling.

**And the seven places to touch when adding another one** are written down in a
single spot, above the class numbers: the way to get this wrong is not writing a
control badly, it is forgetting one of them — and finding out because the control
draws but does not click, or clicks but Tab never reaches it.

### The window stays alive while downloading, and the mailbox has an owner

**tested** — from the CD, in QEMU: `https://www.google.com/` (200, 83 KB, 108
nodes), a local page with six images each delayed by twenty seconds, and the
host test suites (256 + 244 + 244 + 51, zero failures).

**While a page was downloading, the window was dead.** Whoever reads sleeps
inside the read, and while it sleeps the pointer does not move, the window does
not repaint and no key arrives: on a large page that is tens of seconds of a
frozen screen. The transport can now ask a question it could not ask before —
*how many bytes are there right now?* — and when the answer is zero it hands the
turn back to its host. The answer comes from the stack (`IP_MSG_TCP_STATO`,
which reserves nothing and waits for nothing) and not from a read attempt with a
short deadline: that one cannot tell a timeout from an error, and leaves a
pending reservation behind for every attempt.

**And so it can be stopped.** `Esc` interrupts the download and the previous page
stays where it was; what had already arrived is *not* kept, because a truncated
page shown as a whole one is worse than no page at all. Before this, it could not
even be offered: while downloading, no key arrived.

**The encrypted handshake says where it has got to.** Seven steps — the key, the
ServerHello, the secret, the certificates, the signature, the chain, the end —
written into the status line as they go by. And measured: the handshake costs
**490 ms** in total (the 150-certificate CA store 60 ms, DNS and the TCP
connection 320 ms). The "twenty seconds on an emulated 386" was written in three
places and had never been measured.

**The mailbox has an owner.** A graphical application receives window-server
events *and* IP stack replies in the same mailbox, and while a page downloads
there are two waiters: the one reading the network and the message loop. Each
scanned the queue looking for its own and *held* what belonged to the other, to
put it back at the end — and whoever holds something can drop it: the shelf
things were put back onto had four slots, and nobody looked at the `-1` for when
it was full. Now there is `ipc_scegli`: you pass a filter that says, of every
message, **mine / someone else's / nobody's**, and what belongs to someone else
**stays where it is**. It never passes through the hands of whoever does not
want it, so it cannot be lost.

> The shelf counts bytes, not messages, in the same six kilobytes as before. A
> mouse click is twenty bytes: it used to take one of the four slots, now
> twenty-four of them fit. And a message belonging to someone else is **skipped**
> rather than put back, which removes the trap that stalled the message pump
> every time the IP stack had a reply set aside.

**And the TCP window would not reopen.** Whoever reads in bursts — instead of
reserving and sleeping — lets the receive buffer fill up between one burst and
the next, and a fast sender fills four kilobytes in an instant. The stack
advertised the free space **only inside an ACK**, that is, only when a segment
arrived: when the buffer later drained it had no way left to say so, and the
server sat waiting for us. The symptom was an `https` page that never finished
arriving, and for a day it looked like a mailbox defect. Now whoever frees space
says so: an empty twenty-byte ACK, and the flow restarts.

### The browser runs JavaScript, and there are two engines

**tested** — 256 tests on the language, 244 on the bridge with ExJs and 244 with
QuickJS, plus fifteen panels exercised inside EX-OS (on 29 September 2026 they
are 448 on the language and 264 on the bridge with each engine).

**Two engines, one interface.** `exjs.so` is written here; `quickjs.so` is
QuickJS compiled for EX-OS. You choose which one to load, and the bridge to the
page — `exdom.so` — is the same for both: the 244 tests run identically on one
and on the other, which is how you say the interface really is one.

**The DOM a real page needs**: `document`, elements, attributes, classes,
`innerHTML`, events with `addEventListener` and `preventDefault`, forms and the
button that submits them.

**`XMLHttpRequest` and `fetch`**, synchronous and asynchronous — and the
asynchronous one is real: the request leaves, the rest of the script carries on,
and the response arrives when the message loop delivers it. The **order** in
which things happen is one of the fifteen panels, because it is the part that
goes wrong silently.

**A defect that did not look like its cause.** Two stubs in the same process can
pick two different libraries: the browser opened one engine and `exdom.so` the
other, and two engines that could not see each other were running in the same
process. The symptom was a page fault inside the first one holding a context
built by the second. Ring 3 cannot answer this by itself — there is no way to
know which pages are mapped for you without trying to read them — so the loader
answers it, with `SYS_LIB_TROVA` (0.208): the process page table *is* the list.

### Cookies, both halves of them

**tested** — 51 tests on the jar, plus the full round trip against a test server.

A cookie is not a string to send back: it is a rule about **which domain** and
**which path**, with an expiry. The jar keeps the rules, decides which cookies
apply to the address being opened, and attaches them to the request — including
requests that start inside a redirect, which is where half the web puts its
login. It is tested on its own, with no screen and no network, because matching
rules go wrong silently.

> **Persistence is not yet tested inside EX-OS**, only on the host: nothing is
> writable when booting from CD, and the full round trip needs an installed
> system.

### Sound: four cards and MIDI

**tested** — in QEMU, by listening to the file QEMU records, not to the driver's
own counters.

Four drivers — SB16, AC'97, ES1370/ES1371 and HD Audio — behind a single
protocol, so a program that plays sound does not know which card is there. MIDI
on the PCI cards is audible and in tune: it is an eight-sine wavetable, that is,
a single timbre — program change is ignored and there is no percussion. It is
declared, and it is where the work resumes.

> **What has never run is marked where it starts.** QEMU emulates the ES1370, not
> the ES1371: the sample-rate converter and the AC'97 codec inside the ES1371
> driver are written from the documented sequence and have never been executed,
> on silicon or in emulation. And **recording does not exist**: all four know
> only how to play.

### Google is not a target for the browser, and now we know why

**tested** — measured, and the road is closed.

`google.com` opens and renders. The **results** page does not, and it is not our
JavaScript's fault: with the User-Agent of Lynx, of w3m or of MSIE 6, Google
answers "update your browser", and with that of a recent Firefox it sends its
obfuscated anti-robot check. There is no third door. It is written down because
a road tried and closed is worth as much as a feature added: whoever tries it
again pays the same time twice.

### The desktop has a console of its own, shuts down, and speaks your language

**tested** — from the CD and on an ext2 disk installed from scratch, under QEMU.

**Five consoles now.** The window server used to take one of the four, leaving
whoever worked in text with three they never asked to give up. Alt+F1–F4 are now
the text consoles and graphics lives at the end: typing `exwin` switches the
screen there by itself, and returns to the starting console when graphics stops.

**And the graphics console only exists while graphics does.** Until the server
runs, Alt+F5 does nothing — there would be a black screen with a shell nobody is
watching, and no obvious way back. The state lives in the *kernel*, not in the
server, so a killed server frees the console on its own.

**`Exit` from the menu shuts down the desktop**, not just the program manager.
Applications are *asked*, not killed: each gets the same close event as the
window's close box, so anyone with unsaved work has time; then text mode returns.

**Install asks for the language** and writes it to `kernel.cfg`. The kernel never
uses it — translating is the programs' job — but it keeps it and hands it back
the way it does with `keymap`, so there are never two diverging lists.

**And it offers `hwconfig` at the end**, pointed at the freshly installed disk:
the copied `kernel.cfg` is the install medium's, and first boot from disk is the
worst possible moment to discover the disk controller's driver is missing.

**Resolution is chosen from Start > Settings** — the mechanism was already all
there (`/dev/svga.drv` writes both the `kernel.cfg` entry and a byte inside Stage
2); the window was what was missing. It applies on reboot, because the video mode
is chosen at boot through the BIOS.

**Copy and paste inside a page's form fields**, with both standards:
`Ctrl+C/X/V` and `Ctrl+Ins` / `Shift+Ins` / `Shift+Del`. The clipboard is the
whole desktop's — the same shared memory `ex_area` uses — so you can copy from a
form and paste into an editor.

### The browser holds a real page: layout, images, characters, forms

**tested** — from the CD, under QEMU, on Wikipedia's "Operating system" article
(676 KB) and on pages built for the purpose.

**The slowness was no longer the network, it was layout.** Every image that
arrived re-laid-out the whole document. When the `<img>` declares `width` and
`height` the final size is known before a single byte is downloaded: the space
is reserved right away, and the image drops into it without moving anything.
From 7.0 s to 3.3 s for twelve images — and the proof that the text does not
move is pixel by pixel: **zero** pixels changed outside the image band.

**The arena was not full of text: it was full of attributes.** Measured on that
article: node text 71 KB (15%), tag names 20 KB (4%), attributes 386 KB (81%).
A page with 70 KB of words was being truncated because there was no room for
the attributes. Raised to 1 MB — and `ATTR_MAX` to 16000, since that article
has ~13,600 attributes — the page **is no longer truncated**.

**How many images to keep is decided by the machine**, not by a constant: one
sixteenth of the free memory, read with `meminfo()`. A fixed ceiling was stingy
on a large PC and caused `OUT OF MEMORY` on a 32 MB one. And the room is
checked *before* downloading, so an image that will not fit costs neither
network nor CPU.

**`font-family` is now read.** Before, every page came out in serif and
monospace only ever arrived from the `<pre>` tag. The list is scanned and the
first recognised name wins — real names count as much as the generic ones,
because half the web just writes `font-family: Courier New`.

**The characters that were missing.** Liberation covers 2327 codepoints, DejaVu
5918: the fallback is **per character** onto DejaVu rather than replacing the
font, so the text stays as it is and only the holes are filled. Greek,
Cyrillic, Hebrew and Arabic render. And numeric entities above Latin-1 —
`&#8212;`, curly quotes, ellipses — are no longer `?`.

**`colspan` and `rowspan`**, without which any table with a heading spanning
two columns pushed every cell after it out of place.

**And forms were not receiving keystrokes.** The cursor was not what was
missing: the *keys* were. Focus stayed on the address bar — a toolkit control —
while a page's fields are drawn rectangles, which cannot hold focus. You typed
into the address bar believing you were typing into the form. The cursor now
moves with arrows, Home, End and deletions, and inserts mid-text.

> **A toolkit defect, not a browser one:** when a text box consumed a key,
> `exwin` repainted the background and the controls *without telling the
> application*, so as not to wake it for every letter. Right for a window made
> only of controls; for one that also draws its own content, that content
> vanished on every keystroke. It affects any program mixing controls with its
> own drawing.

### Real users: two accounts, `sudo`, and a recovery when login is broken

**tested** — install from scratch on ext2, both accounts asked for and created,
then boot from the disk: `uid=1000(graziano)`, home `/home/graziano` owned by
them. `sudo id` returns `uid=0(root)`, and after `exit` from `sudo -s` you are
back to `uid=1000`.

Accounts used to be created by `login` on first boot, and it created **one**:
root. From there on you always worked as the administrator, which is how any
mistake becomes any amount of damage. Now the installer asks for two — root to
repair, yours to work — and also asks whether yours should be able to do root
things with its own password.

| | |
|---|---|
| `/boot/utenti` | `name:uid:gid`, 0644 |
| `/boot/ombra` | `name:salt:digest`, 0600 — SHA-256 of `salt:password` |
| `/boot/amministratori` | one name per line, 0644 |
| `lib/exuser` | read a password without echo, verify it, add an account |
| `bin/sudo` | the command |
| `SYS_SU` (254) | the narrow capability: «become root IF you know the password» |

    sudo <command> [arguments]   runs that command as root
    sudo -s                      opens a root shell
    sudo                         does nothing and prints the usage

! **RUNNING A COMMAND AND OPENING A SHELL ARE NOT THE SAME THING SAID TWO
WAYS.** With `sudo command` the powers last as long as the command and end by
themselves; with a shell they last until somebody remembers to leave. The
second one is available — `-s` — but it has to be **asked for**: opening it for
someone who typed `sudo` and Enter would mean handing the more dangerous of the
two to somebody who did not ask.

! **THE `sudo` PROGRAM HAS NO POWER OF ITS OWN**, and that is what holds up
everything else. It is not setuid — the setuid bit on files does not exist in
EX-OS, on purpose, because it would make every executable carrying it dangerous
and there is no way to audit them all — and it decides nothing. It reads a
password, hands it to the kernel, and **the kernel decides**. A `sudo` replaced
with some other program is just some other program.

! **THE CHECK IS IN THE KERNEL BECAUSE IT COULD NOT BE ANYWHERE ELSE.**
`/boot/ombra` is 0600 root and must stay that way: if it were readable, anyone
could carry the digests off and try them at leisure on another machine. So the
comparison has to be done by somebody who can open that file. It is the same
reason `su` is setuid root on Unix, and it costs an SHA-256 inside the kernel.

! **AND THE KERNEL COULD NOT OPEN IT.** `SYS_SU` runs in the caller's process,
uid 1000, and the VFS was looking at the **process's** credentials instead of
those of whoever was really reading. The fix is `vfs_open_autorita()`: not a
bit inside `flags` — that would come from `sys_open`, that is from a number the
user chooses, and guessing it would be enough to read the passwords — and not a
global state either, which would hold for **every** process while it is on,
while the VFS reschedules in here. The permission travels as an argument and
ends with the call.

! **AND A CONSOLE THAT CANNOT OPEN `login` IS NOT PROTECTING ANYTHING**: it is
a console whose authentication is **corrupt**. Leaving it shut does not defend
— whoever is standing at the machine boots from CD and mounts the disk in
thirty seconds — and it removes the only way to repair it from the inside. A
root shell opens, and says so plainly before it does: «this shell runs as root
and NOBODY has logged in». It is not a login: it is a broken machine opening up
to be repaired.

### `ls -l`: permissions and owners are visible

**tested** — and it found two defects, one of them in the kernel.

! **ONE MORE CALL, NOT ONE MORE FIELD IN `Stat`.** Changing a structure that
programs already pass around means rebuilding everything that uses it, and
`Stat` is used by anyone who opens a file. `st_uid` and `st_gid` stay zero and
keep saying so; the truth is asked for by whoever wants it, with `statperm()`
(`SYS_STATPERM`, 253).

! **THE DATA WAS ALREADY THERE AND WAS NOT COMING OUT**: `VfsStat` has carried
mode, uid and gid ever since ext2 learned about owners, but `sys_stat` was not
copying them. What was missing was the transport, not the information.

! **AND `VfsStat` WAS NOT INITIALISED.** On a CD, `ls -l` showed different
permissions for every file and five-digit owners: `stat_interno()` left mode,
uid and gid at whatever was on the stack — only the ext2 branch wrote them,
while ISO 9660, the floppy's FAT12 and the root did not touch them at all. And
it was not cosmetic: **`vfs_permesso()` decides on `st->modo`**, where zero
means «this volume has no owners, let it through».

On a volume without owners `ls -l` prints `?????????` and `-`, which is the
truth: on FAT and on ISO 9660 the owner does not exist.

### The screen is recomposed only where it changed

**tested** — measured with a temporary probe in the compositor loop, not
estimated:

    recompositions while moving the pointer
        29 of   260 pixels
        17 of   240
         2 of   117
        39 of 480,000   (startup, new windows, button presses)

that is, **260 pixels instead of 480,000** — roughly 1850 times less — for the
case that repeats with every movement of the hand.

! **THE CLIPPING LIVES IN THE TWO PRIMITIVES, NOT IN THE CALLERS.** `px()` and
`riempi()` are the only two roads to the framebuffer, so everything that draws
— frames, grips, outlines, the pointer — inherits the clipping without knowing
it exists. Putting it in the callers would mean remembering it in every new
function, and sooner or later somebody does not.

! **THE CLIENT-AREA COPY IS THE EXCEPTION**, and it has to be: it deliberately
does not go through the primitives, because it works whole rows at a time with
MMX and that is what makes it fast. There the clipping is applied by hand.

! **THE DEFAULT IS «EVERYTHING», AND IT MUST STAY THAT WAY.** A dirty region
that is wrong on the small side leaves old pixels on the screen: a defect that
does not show where it was made and that turns up as «every so often a piece of
a window stays behind». Today **one case** is narrowed, the pointer's movement;
the other eleven still declare the whole screen, and that is stated.

### The browser: tables, lists, `<pre>` and style sheets all the way

**tested** — right-hand edges identical on every row (232, 469, 753), gaps of
8 px, sum 744 = the width of the area.

! **A COLUMN'S WIDTH IS NOT KNOWN UNTIL EVERY CELL IN THAT COLUMN HAS BEEN
LOOKED AT**, and it is the only place in the browser that wants **two** passes:
the rest of the page lays out forwards, one word after another, without going
back.

! **AND WHILE MEASURING YOU DO NOT ALIGN.** That is the defect the pixels
found: the default sheet centres `<th>`, so the measuring pass pushed the
pieces into the middle of the row and the width came back as «half the page»
instead of «as wide as the word». Alignment decides **where** to put a row,
measuring asks **how much** it takes. No `colspan`/`rowspan`, no borders:
stated.

`<hr>` used to be just some air: now it is a line, drawn as a background two
pixels tall because that is exactly what it is. `<ul>` and `<ol>` have their
marker, and for `<ol>` the number is counted **among siblings** — two nested
lists would hand each other their numbers.

! **INSIDE `<pre>`, SPACES AND NEWLINES ARE THE CONTENT**, and that is the
whole reason the tag exists. The fix belonged in `exhtml.so`, not in the
browser: the parser collapses any run of whitespace to a single space — which
is the HTML rule and is right for everything else — and collapsing it there
means no user of the library, however careful, can ever put it back. The
information is lost before it reaches them.

From the style sheets, **alignment, the four margins and the background
colour** are now applied as well; `excss.so` was computing them and the browser
was throwing them away — three properties out of the eight the library promises,
discarded by its user, the kind of gap that raises no error at all.

! **ALIGNMENT CANNOT BE APPLIED WHILE WRITING**: to centre something you need
to know how wide the row is, and you only know that when it is finished. The
first piece is marked, and at the line break every piece on that row is moved.

! **A BACKGROUND IS NOT A PIECE, IT IS WHAT LIES UNDER THE PIECES.** It lives
in a list of its own and is drawn before all the text: putting it among the
pieces would mean painting over words already written, because the order of the
pieces is the document's and has nothing to do with depth.

### «About», and the clock that was not disappearing

**tested** — and the first diagnosis was wrong, disproved by an A/B.

Every desktop program — browser, file manager, editor — now states its name,
what it does, the author, and **the memory it is using**: image
(`_start`→`_bss_end`), heap (`sbrk(0)`) and stack. From the shell, `ver` says
it. It lives in `lib/exinfo`, because four copies of the same box diverge at
the first line added.

! **THE CLOCK WAS NOT DISAPPEARING ON THE FIRST CLICK: IT WAS GOING UNDER.** I
had blamed the window repaint on any unhandled message, and the A/B disproved
it — two runs identical to the pixel, the only difference being the digit of
the clock. The real cause was `in_cima()`, which re-raised a window that was
already on top: the bar is `WIN_ST_SOPRA`, and re-raising it put it **below**
the others in the same layer.

### A browser: from the network to the screen

**tested** — `http://www.google.com` returns 200 and 82550 bytes;
`http://example.com` renders laid out in `/exwin/bin/browser`, with the heading
in Liberation Sans Bold at 22 and the body in Serif at 15. And the **page's
images show up**: a run against three hand-generated PNGs gives exactly 3000
pixels for each of the three expected colours — that is, both the natural size
and the one declared with `width`/`height` are right to the pixel.

| | |
|---|---|
| `lib/exhttp/http.c` | HTTP/1.1 without the network: URL, request, headers, chunked body |
| `lib/exhttp/exhttp.c` | the TCP transport, redirects |
| `lib/exhtml/html.c` | from text to tree — **`/exwin/lib/exhtml.so`**, available to any program |
| `bin/scarica` | fetches a page and prints or saves it |
| `exwin/bin/browser` | address bar, links, scrolling, images |
| `lib/eximg/eximg.c` | PNG, JPG and ICO — opened on demand, not linked in |

! **THE TRANSPORT IS A PARAMETER, NOT SOMETHING KNOWN.** Today TCP sits under
HTTP; tomorrow, for `https://`, TLS will. If this code opened the connection
itself, that day it would have to be rewritten — and it would be the second
time «read the headers, then the body» gets written.

! **`chunked` IS NOT OPTIONAL.** A server that does not know in advance how long
the answer will be — that is, any page generated on the fly — does not send
`Content-Length`: it sends chunks. Without unrolling them you would see the
hexadecimal length numbers in the middle of the text.

! **HTML IS NOT XML**, and that is the whole difficulty: tags stay open, close in
the wrong order, half of them are missing. `<ul><li>one<li>two` are two
siblings and not a staircase; `<b><i>x</b>` closes up to the `<b>`; inside
`<script>` and `<style>` there **is no markup**, or from the JavaScript's first
`a < b` onwards the tree is garbage.

! **`https://` WORKS, AND ACTUALLY VERIFIES.** TLS 1.3 written here: X25519 for
the key exchange, ChaCha20-Poly1305 for the data, the certificate chain checked
against a store of real CAs and the site name matched against the
`subjectAltName`. Signatures are verified in RSA-PSS **and in ECDSA over P-256
and P-384**, which is what it takes to open real sites: wikipedia.org,
news.ycombinator.com and github.com only have elliptic certificates. Without a
CA store nothing opens: encrypting with whoever answers means encrypting with
whoever is in the middle, and the bar would say `https://` all the same.

! **FORMS ARE DRAWN, FILLED IN AND SUBMITTED**: text and password boxes, check
boxes, selects with a drop-down list, buttons and text areas are drawn like the
system's own controls, take keystrokes, and the button really submits — **over
GET and POST**, percent-encoded.

! **AND THE CONNECTION IS REUSED.** Over `https` the handshake is the whole
cost — ephemeral key, certificate chain, signature — and a page with ten images
paid it ten times. Now the connection is kept when the end of the body is
known, the server's `Connection: close` is honoured, and a request is retried
once if the other side closed without saying so.

! **AND COLOURS WRITTEN IN ATTRIBUTES COUNT**: `bgcolor`, `text`, `align`. Half
the web still uses them — Hacker News's orange bar is a `bgcolor` on a
`<table>` — and they sit at the lowest step of the cascade, below every style
rule.

! **AND IMAGES COME IN THREE FORMATS**: PNG, JPEG and GIF (first frame,
transparency included).

! **IMAGES COME AFTER THE TEXT.** The page is laid out and drawn with the words
alone; only then is one image fetched at a time, and on each arrival the page is
laid out again. Fetching them first would mean an empty window until the last
one answers, and one that does not answer costs eight seconds by itself. One
that never arrives leaves its place to its `alt`, which is what that attribute
is for.

! **AND THE PIXELS BELONG TO THE BROWSER, NOT TO THE DECODER**: it decodes,
copies into the size it will be drawn at, and gives the natural bitmap straight
back. Keeping it would mean letting the page choose how much memory to take —
128 KB of PNG can be 4000×3000 pixels, that is 48 MB on a machine that has 32.
The ceilings are declared: twelve images, 128 KB per file, 512 K pixels in all.

! **STYLE SHEETS ARE THERE, in `/exwin/lib/excss.so`**: `<style>`, `<link
rel=stylesheet>` and the `style=` attribute, with the real cascade (origin,
specificity, order). Selectors by type, class, id, descendant and list; colors,
sizes, bold, italic, `display:none`, alignment and margins.

! **AND WHAT CANNOT BE READ IS DISCARDED, NOT GUESSED**: `div > p` does not
become `div p`, a selector longer than the ceiling is dropped rather than
shortened — shortening it would make it **wider** than the original — and `2em`
is refused rather than taken for two pixels. Less style, never wrong style.

! **AND THE APPEARANCE TAGS BECAME RULES**: `h1`, `<b>`, `<i>`, `<strong>`,
`<em>` and the link color now live in a built-in sheet at the lowest cascade
origin, so a page can override them. They used to be `if`s in the engine, and
`<b>` and `<i>` were not there at all.

What is **not** there, declared: JavaScript, `@media`, relative units,
`colspan`/`rowspan`.


### Fonts: TrueType, measured against FreeType

**tested** — six Liberation faces at six sizes inside EX-OS, and the rasterizer
compared pixel by pixel with FreeType over 1460 glyphs: **bounding box
identical 1460 out of 1460**, mean difference 0.94 levels out of 255.

| | |
|---|---|
| `lib/exfont/exfont.c` | the EXFN bitmap format, inside `exwin.so` |
| `lib/exfont/ttf.c` | the TrueType container |
| `lib/exfont/raster.c` | outlines → coverage, in 26.6 integers |
| `exfont.so` | instance, glyph cache, opened on demand |

! **`strlen(s) * 8` IS THE SUM TO REMOVE BEFORE SOMEONE WRITES ANOTHER ONE ON
TOP OF IT.** It is true only with the system font: a layout engine born on that
assumption would have to be rewritten the day a proportional font arrives. That
is why fonts came **before** the browser.

! **THE ARITHMETIC IS INTEGER.** On the declared Pentium 133 floating point is
slower, but above all every process that touches the FPU pays a state save at
every switch — and drawing text is what one does continuously.

! **THERE IS NO 64-BIT DIVISION** in an EX-OS library: it links without libgcc,
so `__divdi3` is not there and the link fails.

! **THE COMPARISON WITH FreeType FOUND A DEFECT NO HYPOTHESIS WOULD HAVE
FOUND.** On the first pass identical boxes were 95.4%, and the missing ones had
a shape: at 16 pixels almost every capital came out one pixel shorter. The scale
truncated instead of rounding — half a sixty-fourth lost that becomes a whole
pixel.

What is missing, declared: hinting (which is why the interface still uses the
8x16), kerning, ligatures, basic plane only, CFF refused on purpose.


### `rename` replaces the destination, like POSIX

**tested** on ext2, FAT12 and FAT16.

Up to 0.184 `rename()` returned `EEXIST` if the destination existed. It was a
declared choice, and it did not hold:

! **«DELETE FIRST» IS NOT EQUIVALENT.** There is only one scheme for saving a
file without risking losing it: write alongside, then **swap**. If the swap does
not replace, you have to delete first — and between the deletion and the swap
**the file does not exist**.

! **AND THAT WINDOW CAN ONLY BE CLOSED IN THE KERNEL.** `vfs_rename` holds the
filesystem lock for the whole operation: no other process sees the intermediate
state. In user space that guarantee cannot be had.

! **THE REPLACEMENT LIVES IN THE VFS, BEFORE THE DISPATCH**, so it holds for
ext2, FAT12 and FAT16/32 without any driver reimplementing it. POSIX rules on
types: a directory only over an empty directory, a file only over a file. And
`rename("x","x")` does nothing — without that check the replacement would
**destroy x**.


### The taskbar: the clock, and applications that add themselves

**tested** — the clock advances on its own (06:52 → 06:53 in 75 seconds); the
«Applicazioni...» menu adds, removes and saves on ext2.

! **THREADS DO NOT EXIST IN EX-OS**, and the clock is a separate **process**.
And even if they existed, here a process is better: a thread inside the program
manager would share its message queue, so a busy program manager would be a
stopped clock.

! **`ex_sveglia()` AND `EXM_TEMPO`**: without them an application cannot do
anything by itself — the message loop sleeps until an event arrives. It costs no
extra round: the `poll` already has a 200 ms deadline.

! **AUTOSTART IS A DIRECTIVE, NOT A MARK ON THE ENTRY.** One may want a program
that starts on its own and does **not** appear in the menu — a panel, a clock —
or an entry that does not start by itself.

! **`ipc_rimetti()`**: the mailbox is one and the consumers are more than one.
Whoever waits for an answer from the IP stack scans the messages and used to
**throw away** the others — in a browser those are the user's clicks. Now they
are put back, and **at the end**: the shelf is served before the kernel queue,
so putting one back and re-reading immediately would return the same message
forever.


### The graphical interface: a window server in ring 3

| | |
|---|---|
| `/exwin/bin/wserver` composes windows and moves the pointer, **in ring 3 and unprivileged** | tested |
| **ExWin** toolkit, Win32-style, with headers for **C, C++ and FreeBASIC** | tested |
| Controls: window, button, label, text box, group box, separator, header, terminal | tested |
| `exwin` brings up graphics **on console 5**: Alt+F5 goes there, Alt+F1 comes back to the shell | tested |
| A **shell inside a window**, over two pipes | tested |
| Image backgrounds: BMP today, and the reader table is already the right shape for JPG, PNG and ICO | tested |

! **IT RUNS IN RING 3, AND THAT IS THE POINT.** When the server dies, only it
dies: kernel, scheduler, serial console and keyboard stay alive, and the screen
is restored with `/bin/testo` — which you type blind, and which was built as
the safety net *before* the server was written.

! **IT DOES NOT DRAW WINDOW CONTENTS, IT COMPOSES THEM.** Every window is a
shared memory zone the client fills with pixels; the server adds the border and
the title bar, stacks them and copies them to the framebuffer. A client that
draws badly ruins its own window, not the screen. It is also why image decoders
live in the client library: a JPG reader inside the server would be a defect of
**every** application at once.

! **CLIENTS ALWAYS DRAW IN 32-BIT ARGB** and know nothing about the screen. The
conversion to 16, 24 or 32 bits lives in exactly one place. A toolkit that had
to know the screen format would have six paths to try instead of one.

### The desktop: `/exwin`, taskbar and start menu

| | |
|---|---|
| `pm` — desktop, taskbar at the bottom, **Start** button, menu of applications; the right button creates, renames, copies and deletes | tested |
| **Exit** (back to the shell) and **Shut down** entries in the menu | tested |
| `/exwin/bin`, `/exwin/lib`, `/exwin/dev` — graphical applications **do not live in `/bin`** | tested |
| The application list is a **text file**, `/exwin/lib/applicazioni.txt` | tested |
| `filemgr` — file manager: scrolling list, directories first, right button, drag and drop, opens every file with its program | tested |
| `edit` — text editor: several files in tabs, find and replace, undo, clipboard, Ctrl+S, Ctrl+Q | tested |

! **GRAPHICAL APPLICATIONS DO NOT LIVE IN `/bin`, AND THAT IS A DECISION.**
Programs in `/bin` are launched from a shell and talk to a terminal; these want
the window server, and launched without it they do nothing. Mixing them would
mean an `ls /bin` where half the names cannot be used where you are looking.
Same reason drivers live in `/dev`.

! **THE LIST IS A FILE, NOT A COMPILED TABLE.** Adding an application is one
line. A list inside the binary would mean rebuilding the program manager for
every new application, and **whoever installs a program does not have the
sources**.

! **A FILE LARGER THAN THE LIMITS IS LOADED IN PART AND SAVING IS BLOCKED.**
Saving what was read would mean erasing the rest of the file without ever
having shown it: it is the quietest way an editor has of destroying data. The
limit — 512 lines, 200 columns — follows from the bump allocator, where
`free()` gives nothing back.

### Shared libraries: `exwin.so`, `exdlg.so`, `libc.so`

> **History.** This is how libraries worked until kernel 0.350. Since 0.351 the libc is
> `/lib/ld-exos.so` and the EX-OS libraries are ELF shared objects: see
> [Writing a shared library](sviluppo.md#writing-a-shared-library). `SYS_LIB_APRI`, `libc.so`, the bridges
> and the fixed addresses below are gone.

| | |
|---|---|
| `SYS_LIB_APRI` (248): the kernel maps a library **once** and attaches it to whoever asks | tested |
| `.text`/`.rodata` **shared** across processes; `.data`/`.bss` a private copy | tested |
| Resolution **by name**, not by position: update the library without recompiling applications | tested |
| `/lib/libc.so` — **322 functions**, used by 39 programs | tested |
| `/exwin/lib/exwin.so` — the toolkit; `/exwin/lib/exdlg.so` — the Open/Save dialogs | tested |
| Headers do not change by a single line, and neither does application source | tested |

```
    0x04000000   exwin.so    the toolkit
    0x04400000   exdlg.so    the dialogs
    0x04800000   libc.so     the C library
    0x08000000   programs
```

What it saved, measured:

| | before | after |
|---|---|---|
| all of `/bin` | 850,132 | **608,468** |
| EX-OS ISO | 4728 KB | **4252 KB** |
| `libctest` (text) | 64,861 | **33,312** |
| graphical applications | ~37,000 | **~15,000** |

! **THIS IS NOT REAL DYNAMIC LINKING, AND THAT IS DELIBERATE.** A `.so` with
`ld.so`, PIC code, GOT and PLT is the standard road, and it is months of work:
the ELF loader rewritten, a dynamic linker written, the libc rebuilt as PIC.
And any defect in there would be a defect of **every** application at once.

Here a library is a perfectly ordinary ELF — `ET_EXEC`, not PIC — linked at a
**reserved** address: it is always there, so there is nothing to relocate and
no GOT is needed. What was actually wanted — updating the library without
recompiling applications — comes from resolution by name.

! **THE ORDER OF THE TABLE IS NOT PART OF THE ABI.** With a positional table,
reordering entries would break every already-compiled application and no error
would say so: you would simply call the wrong function. With names you can add,
reorder and rewrite the body of any function. Only **removing** a name breaks
things — which is exactly the bargain a DLL makes.

! **FUNCTIONS ARE FORWARDED WITHOUT KNOWING THEIR SIGNATURES.** There are 322
of them: writing the thunks in C would mean copying 322 signatures, each one
silently wrong-able. A thunk in assembly is an indirect `jmp` that touches
neither the arguments nor the return value — `printf: ff 25 c4 25 00 08
jmp *0x80025c4`. It is the same job a PLT does, and `tools/genlibc.py`
generates them by reading the real symbols with `nm`.

! **THE FIVE GLOBAL VARIABLES CANNOT BE.** `errno`, `stdin`, `stdout`,
`stderr`, `environ`: a program writing `errno = 0` would write into its **own**
copy while the libc reads its own — two variables with the same name, and no
error anywhere. The address is exported and the header turns the name into a
read of it: `#define errno (*__errno_dove())`. Source using them does not
change.

! **AND STARTUP CANNOT BE SHARED.** `_libc_start` touches `main`,
`__init_array_*` and `__fini_array_*`, which belong to the binary they sit in.
Inside the library `main` would not even exist, and the arrays would be the
library's — empty: the program's global constructors would never run, **and
nobody would say so**.

! **`login` AND `install` STAY STATIC**, and that is a decision: they are the
two programs you get in with and repair with. If `libc.so` were missing or
broken, a login linked against it would make the system unreachable with no way
to fix it from inside.

### Installing by components

| | |
|---|---|
| `install` shows the components found on the medium and asks about them **one by one** | tested |
| `install -m` minimal system, `install -t` everything: no questions | tested |
| `copia_albero()` follows subdirectories: a component is not one level deep | tested |

! **THE MINIMAL SYSTEM IS A CLOSED LIST; EVERYTHING ELSE IS OPTIONAL.** `bin`,
`boot`, `lib`, `dev`, `drivers` are what EX-OS cannot start without. Any
**other** directory in the root of the medium is a component, and the installer
finds it by itself.

It is the opposite of a list written inside the installer: adding a package —
`/exwin` today, whatever comes tomorrow — means **putting its directory on the
medium, and nothing else**. Whoever prepares a package does not have the
installer's sources.

! **THE CHOICE IS MADE BEFORE ANYTHING IS WRITTEN.** Asking "shall I install
/exwin too?" after the kernel has already been replaced would mean that
answering "cancel" no longer cancels anything.

### Four old defects, and what they have in common

| | |
|---|---|
| Keyboard focus was not focus: the taskbar took every key | fixed |
| The shell had lost redirections and environment for three days, silently | fixed |
| `wserver.drv` did not know `-i`: the probe **started** it and installing from CD hung | fixed |
| `cat` with no arguments did not read `stdin`, so in a pipe it was useless | fixed |

! **FOCUS WAS NOT FOCUS.** The server sent the key to the last window drawn.
True while draw order depended only on who had come to the front; false since
`WIN_ST_SOPRA` exists, which keeps the taskbar permanently on top. From that
day **no window could receive a key** while the program manager was running.
The defect was neither in the editor that seemed deaf nor in `WIN_ST_SOPRA`: it
was in a function that decided **two** things while its name promised one.

! **A MAGIC NUMBER PROTECTS AGAINST DAMAGE, NOT AGAINST DRIFT.** Of
`SpawnExtra` — the structure that crosses the `spawn` syscall — there were
**four copies**. Three were updated, the fourth (the shell's) was not. The
kernel did exactly what the magic exists for: it did not recognise the block
and **ignored** it rather than reading it crooked. But "ignored" meant that for
three days `hello > file` printed to the screen and left the file at zero
bytes, **with no message**. Silence is not noticed.

The fix is not to update the fourth copy: it is to **not have four**.
`lib/include/spawn_abi.h` is the single definition, and at the bottom it has
`typedef char spawn_abi_misura_invariata[(sizeof(SpawnExtra) == 596) ? 1 : -1];`
— a field added without changing the magic now stops the **build**.

! **AND SIX TIMES "AN OUTPUT THAT DOES NOT KNOW IT IS STALE".** Fake
prerequisites on the floppy target, `uhci.drv` missing from the ISO's, the SVGA
resolution not a prerequisite of Stage 2, the images not depending on the
`Makefile`, and twice a `Makefile` variable used as a prerequisite **before**
being defined — where `make` expands it to the empty string. The second one
made 294 tests appear to run against the shared libc while they ran against the
static one. **A prerequisite written with an empty variable is not a weak
prerequisite: it does not exist, and nobody says so.**

### Drivers: the machine picks them, they are not copied wholesale

| | |
|---|---|
| A `-i` convention shared by every driver: probe, report, exit 0 if needed here | tested |
| `-i` **does not touch the device**: it only reads from the PCI bus | tested |
| A `/drivers` catalogue on the EX-OS CD, separate from the `/dev` that boots it | tested |
| `hwconfig -d <mount>`: probes the catalogue, installs into `<mount>/dev` only what answers | tested |
| `install` calls that selection instead of dumping `/dev` onto the disk | tested |
| Cross-check on NE2000 and on AMD PCnet: each installs only its own driver | tested |

An installed disk ends up with the drivers that work on **that** machine.
Before, everything on the CD went across: the driver for the network card the
machine does not have, and `floppy.drv` — an ET_DYN module that `spawn()`
rejects, that is, a file nobody could load, installed on every installation.

There is no list of drivers anywhere, and that is deliberate: a list would be a
second truth alongside the contents of the directory, and the two diverge the
first time a driver is added or removed. The question goes to the driver, which
is the only one able to answer it.

! The easiest thing to get wrong here, and what the first version did get
wrong: `-i` runs on a **running** system, where the autoexec has already
started the right driver. Initialising the card in order to probe it reset it
underneath whoever was driving it, and resetting a busy card answers with an
unexpected status — so a driver declared itself unnecessary on the very machine
whose card it was driving at that moment. Hence the rule: look, do not touch.

### A bigger shell: VESA graphics mode and `svga.drv`

| | |
|---|---|
| 640×480 → **80×30**, 800×600 → **100×37**, 1024×768 → **128×48** characters | tested |
| `/dev/svga.drv <mode>` picks the resolution, the way `keymap` picks the layout | tested |
| It is a driver in full: `hwconfig -d` installs it onto the disk by itself | tested |
| It says **loudly** that a reboot is needed: Stage 2 sets the mode | tested |
| Default: 80×25 text console, system identical to before | tested |
| Any VBE probe failure falls back to text, never to a black screen | tested |
| The kernel reports it when `kernel.cfg` and the bootloader disagree | tested |

! **It is not a driver, and it cannot be one.** Setting a VESA mode means
INT 10h — that is, the BIOS, that is, **real mode**. By the time the kernel is
running that door is already shut, and in ring3 it never was open. The only
one who can do it is **Stage 2**, before the switch to protected mode, and
that is where the VBE probe lives: `bootloader/stage2/loader.asm`.

The kernel receives address, pitch, dimensions and depth in `BootInfo` and
draws the console into the framebuffer with the 8×16 font in
`kernel/arch/x86/font8x16.c`. The rest of `vga.c` never noticed: the whole
console — scrolling, ANSI parser, four virtual consoles, erasures — works on
its own cell array, and it is `riversa_cella()`, `riversa_tutto()` and the
cursor that know where those cells actually end up.

**How the choice reaches the bootloader.** Stage 2 has no filesystem: Stage 1
hands it a ready-made sector map for the kernel, and on a hard disk not even
that. It cannot read `kernel.cfg`. So `svga.drv` writes in two places — the entry
in `kernel.cfg`, which is the configuration, and a byte marked by the
`SVGAMODE` signature inside the Stage 2 image, which the bootloader reads from
itself. They are not two truths: only `svga.drv` writes the second, and the kernel
**compares them on every boot** and says so if they diverge. It is the same
bargain as the sector map `install` writes into the boot sector.

Three real defects met while building it, all three silent:

- **Stage 2 did not fit.** The limit was 1280 bytes — the GDT sat at `0x0A00` —
  and it already used 1095. The GDT moved to `0xE400`: only that file knows
  it, writes it and loads it, and moving it was the remedy the size-limit
  comment already pointed at.
- **Page fault at `0xfd00c000`.** The framebuffer was mapped only in the
  kernel page directory, but the console writes while a process is running
  too, and at that moment `CR3` is that process's.
- **`PMM: page out of range` on every process that exited.**
  `paging_destroy_directory` treated the framebuffer entries as user pages and
  returned them to the PMM — which are not RAM. Worse: it freed the shared
  page table, so the first process to die would have left every other one
  without a screen.

And one of performance: the first version redrew 3700 cells on every scroll —
half a million writes into video memory, which does not go through the cache —
and the system reached the prompt in tens of seconds. Now it scrolls the
framebuffer with a single copy and repaints only the last row.

**Three defects closed while testing it on an installed disk**, which is where
`svga.drv` is actually of use — from the CD you cannot write anyway:

- `svga.drv` looked for `/boot/stage2`, but `install` writes **`/boot/stage2.bin`**:
  it answered "Stage 2 image not found" on exactly the machine where it was
  meant to work.
- The backup was called `kernel.cfg.bak` — two dots, not a valid 8.3 name —
  so on FAT it failed with an error about names while the user was changing
  the resolution. It now replaces the extension: `kernel.bak`.
- `hwconfig` recognised drivers by comparing `.drv` **case-sensitively**: on
  ISO 9660 with Joliet the names are lowercase, on FAT they are `KBD.DRV`.
  Every earlier test had been on the CD, so it never showed.

And a regression introduced by the driver probe itself: **installing from the
floppy no longer installed any driver at all**, `kbd.drv` included, because
without a CD there is no catalogue — and the installed system fell back to the
in-kernel IRQ1 handler with nothing to explain it. Now `/dev` of the running
medium is the last entry of the catalogue: it is a legitimate catalogue, and
its drivers get probed like all the others.

### Two images, and nothing is left out

| | |
|---|---|
| `make iso-tutte`: builds `dist/exos.iso` and `dist/exos-tools.iso` | tested |
| Every program declares its destination in `PROGRAMMI_FLOPPY` or `PROGRAMMI_CD` | tested |
| `make verifica-programmi`: stops if a source under `bin/` or `drivers/` is left out | tested |
| The `i386-exos` cross is put on PATH by the Makefile, not by whoever runs `make` | tested |
| `bin/xcp` and `pcnet.drv`, which had been left out, are now in | tested |

The two images answer two different questions:

- **`dist/exos.iso`** — the **system**: kernel, base commands, networking
  (`ping`, `ftp`, `telnet`, `dhcp`, `host`, `netdetect`…) and every driver. It
  is bootable, and it is a **superset of the floppy**, not a different thing.
- **`dist/exos-tools.iso`** — the **languages**: `gcc`, `g++`, `cpp`, `cc1`,
  `fbc`, `as`, `ld`, `nasm`, `ndisasm`, `make`, libstdc++, OpenSSL. 150 MB
  installed separately with `toolinst`.
- **`dist/floppy.img`** — the **base system** only: boot, prepare a disk,
  install itself, read and write files. What fits in 1.44 MB.

! **A source that is in no list is not compiled and lands on no image, and
nobody notices.** That happened for months with `bin/xcp/`: the source was
there, the rule was not, and the command simply did not exist on the machine.
`pcnet.drv` had the opposite problem, equally silent — a rule existed, but no
list named it: it got built only as a side effect, because the ISO recipe
mentioned it among its own prerequisites, and `make all` did not build it.

`make verifica-programmi` closes both cases, and it does so by **looking at the
result rather than reading the Makefile**: for every `bin/<name>/` there must be
a `build/bin/<name>` or a `build/bin-cd/<name>`, for every `drivers/<name>/` a
`.drv` — except for the two declared exceptions (`net`, which is only protocol
headers, and `tty`, which is compiled into the kernel). Both ISOs run it
themselves before building, as an order-only prerequisite: if something is
missing the build stops and says which name to add, and to which list.

### The tools install themselves: `toolinst`

| | |
|---|---|
| `toolinst [root]`: copies the tools CD's `/exos` tree onto the disk | tested |
| Chooses by language: C mandatory, C++, FreeBASIC and OpenSSL on request | tested |
| Adds `/exos/bin` to the `PATH` entry of `[env]` in the target's `kernel.cfg` | tested |
| `-n` counts files and bytes without writing anything; `-p` changes the prefix | tested |
| Stops if the volume is read-only or is not ext2 | tested |

! **Copying the binaries into `/bin` and adding a PATH entry does not work**,
and this is the thing to know before anything else. Both the GCC driver and
`fbc` work out where their own parts live from **where their binary sits** —
`<my directory>/..` — not from PATH. You can read it in the `fbc -v` trace:

```
assembling: /cdrom/exos/bin/../bin/as --32 ...
            /cdrom/exos/bin/../lib/gcc/i386-exos/17.0.0/libgcc.a
```

Hence:

```
/cdrom/bin/gcc -c test.c        ->  cannot execute 'cc1'
/cdrom/exos/bin/gcc -c test.c   ->  compiles
```

Same binary, same PATH, same command line: only where it is launched from
changes. From `/bin/gcc` the prefix becomes `/` and it looks for
`/libexec/gcc/…`, `/lib/gcc/…`, `/include/freebasic`, which do not exist — and
the message that comes out talks about missing headers while the fault is in
the path. So `toolinst` copies **the whole tree, keeping its shape**, and puts
its `bin` on the PATH.

! **The target disk must be ext2.** Writing long names on FAT is not there
yet, and the tree is full of names 8.3 cannot hold (`bits/stdc++.h`,
`libstdc++.a`): the copy would succeed and the compiler would no longer find
its own headers, which are on the disk under a different name. `toolinst` looks
at the mount's filesystem and says so before starting, instead of letting you
find out 50 MB later.

The optional groups are defined by the paths to **skip**, not by the ones to
copy: a new file on the CD lands on the disk along with everything else, and
the only way to lose it is to have written it into the exclusion list.

### Paths and messages: say one thing, and say it true

| | |
|---|---|
| A missing command prints **one** message, not one per `PATH` entry | tested |
| `./myprog` and `sub/myprog` run: `PATH` applies to bare names only | tested |
| A file that exists but is not an ELF says so, instead of posing as absent | tested |
| FAT12 refuses paths deeper than one level instead of flattening them | tested |
| `cp`: `-y`, per-file confirmation, `t` = all, progress during `-r` | tested |

Four different ways of giving a wrong answer while looking like a right one.

**Six red lines for one mistyped command.** The shell probed `PATH` by calling
`spawn()` on every entry and using the failure as the answer; the kernel
reported "file not found" as `LOG_ERROR`, which prints regardless of
`loglevel`. Now the shell asks *"is it there?"* with an `open` — as
`spawn_cerca_path()` in the libc already did — and in the kernel an absent file
is no longer an error: it is the answer to a question, and whoever asked
reports it.

**`./myprog` would not start.** The choice between `PATH` and a direct path was
made on `prog[0] != '/'`. The right rule is the `execvp` one: search `PATH`
only for a name with **no slash** in it. With the first-character test
`./myprog` became `/bin/./myprog` — and when `/bin` held a namesake, **that
other one** ran, with nothing to say so.

**`cat /bin/test/t.txt` read `/test/t.txt`.** FAT12 built the directory part of
the path and then kept only its last component, looking it up in the root: any
made-up prefix worked as long as it ended with the name of a real directory.
The same flaw, in a different guise, made `stat("/bin/hello")` answer with the
root's `/hello` — a different file, of a different size. The driver still
handles one level only, as before; the difference is that a path it cannot
represent now yields "not found" instead of a file other than the one asked
for.

**`cp` would not overwrite.** An existing file made the copy fail, and on a
recursive tree a single shared file forced you to redo everything by hand. Now
it asks — `s`, `n`, or `t` for all the remaining ones — and `-y` answers yes to
all of them up front. A skipped file does not count as an error: the copy did
what it was told. With `-r` every file is printed as it is copied, because on a
floppy a tree of a few dozen files is minutes during which nothing used to
appear, and a silent program and a stuck program look too much alike.

### Networking — from the PCI bus to an FTP client

| | |
|---|---|
| PCI enumeration in userspace (`/dev/pci.drv`, `netdetect`) | tested |
| NE2000 driver in ring3 (`/dev/ne2k.drv`, `nettest`) | tested |
| ARP, IPv4, ICMP — `ping` | tested |
| UDP | tested |
| DHCP client (`dhcp`) | tested |
| DNS resolver in libc, A records (`host`) | tested |
| TCP: active open, send, receive, close (`tcptest`) | tested |
| Passive-mode FTP client, `get`/`put`/`ls`/`cd` (`ftp`) | tested |
| Interactive Telnet client with option negotiation (`telnet`) | tested |
| Manual configuration and ARP table (`ipcfg`, `ipcfg -r`) | tested |
| WiFi manager `wifid`, `wifi`, desktop icon (simulated card only) | tested |
| WPA/WPA2/WEP security, `lib/exwifi` (host test vectors) | tested |
| WiFi drivers for real chips | to do |
| DHCP lease renewal | to do |
| Reassembly of out-of-order TCP segments | to do |
| PCnet driver (Am79C970/C973), a bus master with real DMA | tested |
| `SYS_DMA_ALLOC`: contiguous memory for a bus master | tested |

When something is missing, every network command prints **the whole chain**
and **the next command to type** instead of just an error message.

### CPU — SSE, SSE2, SSE3, MMX

| | |
|---|---|
| Feature detection via CPUID, with the EFLAGS ID-bit test first | tested |
| State saving: FXSAVE where available, FNSAVE on old CPUs | tested |
| Enabling CR4.OSFXSR / OSXMMEXCPT when SSE is present | tested |
| MMX: no work needed, MM0-MM7 alias ST0-ST7 | tested |
| Running on a real 486 and Pentium MMX | to be tested |

The FNSAVE path is what lets the kernel run on CPUs without SSE; it was
exercised by forcing the slow path in QEMU, not on a physical 486.

### Filesystems

| | |
|---|---|
| Booting from CD: FAT12 is no longer probed on the CD-ROM | tested |
| VFAT long names, **reading**, on FAT16 and FAT32 | tested |
| Real file dates and times on FAT, ext2 and ISO 9660 | tested |
| Date and time stamped on files created on the floppy | tested |
| `mkfs` picks FAT16 below 2 GB and FAT32 above, on its own | tested |
| VFAT long names, **writing** | to do |

### Shell and commands

| | |
|---|---|
| Command history with the up/down arrow keys | tested |
| `/boot/autoexec.sh` run by `/bin/sh` at startup | tested |
| `ls`: `-h`, `-a`, `-d`, `-mc`, `-md`, `-p` | tested |
| `install -a`: lists the changed files and offers to update them | tested |
| `hwconfig`: analyses the machine and writes kernel.cfg and autoexec.sh | tested |
| Keyboard layouts: `us it fr de es uk`, with AltGr | `us`/`it` tested |
| Arguments with spaces between quotes (`cp "my file.txt"`) | tested |
| `help helpconfig`: how to bring drivers up, with the current state | tested |
| Backspace no longer eats the prompt nor leaves invisible characters | tested |
| `!silenced` in scripts: hides the commands, not their output | tested |
| `source file.sh`, and `.sh` files runnable by name | tested |

### libc

| | |
|---|---|
| `printf` with `%f`, `%e`, `%g`: 18 significant digits, round half to even | tested |
| Global constructors `.init_array` and destructors `.fini_array` | tested |
| `realloc` growing in place — before, it never grew | tested |
| `gettimeofday` monotonic, anchored to the clock exactly once | tested |
| 64-bit `time_t` | tested |
| Temporary files follow `TMPDIR`, no longer only the root | tested |
| 128-sector disk cache: `cc1` from 19.61 s to 10.19 s | measured |
| 276 automated checks in `libctest` | tested |

### Compilation chain

| | |
|---|---|
| Native `as` and `ld` (binutils 2.44) | tested |
| `cc1`: compiles C and produces assembly inside EX-OS | tested |
| Target runtime on the CD (`crt0.o`, `libc.a`, `libgcc.a`) | tested |
| `as` + `ld` link a real C program together with the archives | tested |
| **`gcc` as the driver, chaining cc1 → as → collect2 → ld** | **tested** |
| **`gcc` finds the system headers on its own, with no `-I`** | **tested** |
| **`g++`: the same chain in C++, with libstdc++ and exceptions** | **tested** |
| FreeBASIC: `fbc` finds its own `.bi` files on its own | tested |
| FreeBASIC: final link (fbc emits Linux options) | to do by hand |
| TLS/SSL as a userspace library (OpenSSL 4.1.0, `openssl` program) | tested |

# EX-OS — Drivers and hardware

**🇬🇧 English** · [🇮🇹 Italiano](driver.it.md)

[← README](../README.md) · [All the manuals](../README.md#the-manuals)

**Contents**

- [Keyboard layout: `keymap`](#keyboard-layout-keymap)
- [`hwconfig` — configuring without reading anything](#hwconfig--configuring-without-reading-anything)
- [`help helpconfig` — the procedure, and where you are in it](#help-helpconfig--the-procedure-and-where-you-are-in-it)
- [Ring3 drivers (current model, since July 2026)](#ring3-drivers-current-model-since-july-2026)
- [16- and 32-bit I/O accesses](#16--and-32-bit-io-accesses)
- [The PCI bus: `/dev/pci.drv` and `/bin/netdetect`](#the-pci-bus-devpcidrv-and-binnetdetect)
- [Interrupts: `irq_bind` and `irq_done` go in pairs](#interrupts-irq_bind-and-irq_done-go-in-pairs)
- [Networking: `/dev/ne2k.drv` and `/bin/nettest`](#networking-devne2kdrv-and-binnettest)
- [Networking: `/dev/pcnet.drv` — the first card that writes to RAM by itself](#networking-devpcnetdrv--the-first-card-that-writes-to-ram-by-itself)
- [The sector cache: the compiler runs twice as fast](#the-sector-cache-the-compiler-runs-twice-as-fast)
- [! `rep insw` instead of 256 calls per sector](#-rep-insw-instead-of-256-calls-per-sector)
- [The `drv_*` interface (previous model, kernel-space)](#the-drv_-interface-previous-model-kernel-space)

---

## Driver interface

### Keyboard layout: `keymap`

```ini
[kernel]
keymap = it        # us it fr de es uk
```

```
keymap            which one is active, and which are available
keymap it         switch to the Italian one, right now
keymap -p         print the line to put in kernel.cfg
```

`/dev/kbd.drv` reads it at startup, **before registering** — that is,
before anyone can type: reading it later would leave a window in which the
first keys are translated with the wrong layout, and those are exactly the
autoexec's. An unknown name stops nothing: the driver says so and keeps
`us`, because a keyboard struck dumb by a typo in the configuration is a
system you cannot even use to fix that typo.

! **Each layout is FOUR tables, not two**: plain, Shift, AltGr,
AltGr+Shift. It looks like a luxury until you try to write a function — on
an Italian keyboard the braces are **only** on AltGr+Shift:

```
@  AltGr+ò        [  AltGr+è        {  AltGr+Shift+è
#  AltGr+à        ]  AltGr++        }  AltGr+Shift++
```

A layout that stops at three tables gives a keyboard that cannot open a
block, and this system ships an editor and a C compiler.

! **Accented letters are code page 437 bytes**: `à` is 0x85, not UTF-8.
It is the only byte the VGA draws. Declared consequence: those same bytes
end up in file names, and whoever reads those files on Linux sees
different characters. It is not a defect of the table: it is that EX-OS has
no system encoding, and choosing one is a bigger decision than a keyboard
layout.

! **There are no dead keys.** On a French or German keyboard `^` and `¨`
write themselves instead of waiting for the vowel. Making them work means
one more state in the driver and a combination table per layout; for now it
is declared that they are absent, rather than made to look broken.

**Two defects found by testing it**, both far from where they were looked
for:

| | |
|---|---|
| the shell threw the accents away | `riga_modifica` accepted `k >= 32 && k < 127`, that is «ASCII only». Invisible for as long as the keyboard was American: with the Italian one the `ò` arrived from the driver and the shell dropped it, and the key looked broken |
| AltGr did not exist | `e0 38` ended up in the extended-key block — the one that delivers the cursors' ANSI sequences — and left through its `default: return` before reaching the code that handled it. The code was there and it was right: it was just after |

! **`us` and `it` are verified key by key** in QEMU, by sending the
*physical* key and looking at which character comes out. The others are
written from the known layout and tested only where they move with respect
to US: they are usable, but whoever has that keyboard in front of them and
finds a wrong key has found a real defect, not a limitation.

! **If you pick the wrong layout, the way out is a reboot.** There is no
command typeable under all of them: among QWERTY variants the letters do
not move, but on AZERTY `a` and `m` change place and `keymap it` typed
blind comes out as `keyq,p`. The hot change, though, is **not permanent**:
reboot and you get back the one in `kernel.cfg`. That is why `keymap`
writes to no file — the one that writes it is `hwconfig`, which keeps
whatever it finds.

### `hwconfig` — configuring without reading anything

```
hwconfig            looks, proposes, asks, writes
hwconfig -n         only looks
hwconfig /disco     configures the system installed in there
```

`kernel.cfg` is written by hand, and to write it you already have to know
that disks are called `hd0p1`, that mount points must not exist, that
modules are ring3 processes and that the order of the network commands is
not negotiable. All true, all documented above, and all to be read
**before** you can bring a machine up.

`hwconfig` knows it already:

```
Cosa c'e' in questa macchina

  tastiera   /dev/kbd.drv — si carica all'avvio, serve alle frecce e a gfedit
  lettore    cd0 — montato all'avvio su /cdrom
  volume     hd0p1  ext2   'dati' — montato su /dati
  rete       scheda Ethernet sul bus PCI — si accende all'avvio
```

Then it shows the two files it would write, in full, and asks. The round
trip is verified: it analyses, it writes, and the machine boots from the
disk with the network up **without a single `[WARN]`**.

! **The previous files go to `.bak`**, and that is what makes the proposal
acceptable: if the machine does not boot, the earlier one is right there.

! **The new file is generated, not edited.** The `kernel.cfg` that ships
with the system is two hundred lines of explanation; keeping them would
mean an INI parser that puts them back, that is, a much bigger program with
many more ways to be wrong. What is written here is short and replaces the
previous one entirely — said plainly **before** asking.

Three choices that only show when you use it:

| | |
|---|---|
| **it does not look at which card it is** | the model table lives in `netdetect`, and duplicating it would give two lists that diverge at the first new driver. The generated autoexec calls `netdetect -c`, which has that table: `hwconfig` needs to know **whether** there is a card, not which one |
| **the volume that will be the root does not go into `[mount]`** | the kernel would notice by itself («already mounted elsewhere»), but it is a line that serves no purpose inside a file someone will read to understand their own machine |
| **an unlucky label does not become a mount point** | a volume labelled `boot` would give `/boot = hd0p1`, which the kernel refuses — a `[WARN]` at every power-on, and whoever reads it has no reason to suspect the disk label. In that case it falls back to `/disco` |

It also writes `TMPDIR`, which is not a flourish: `mkstemp` and the
compiler driver put their intermediate files there, and without it they end
up in the root — which, booting from CD, is read-only.

It lives **on the floppy**, with `fdisk` and `install`: it is needed exactly
when preparing a machine, that is, when the CD may not be there yet. Without
`/dev/pci.drv` it configures mounts and modules and says it could not check
the network part.

### `help helpconfig` — the procedure, and where you are in it

```
help helpconfig     (or just `helpconfig`)
```

It explains how to bring the drivers up — the network chain, manual
configuration, diagnosis, the autoexec — and **shows the current state** by
asking the IPC registry who is already there:

```
A che punto sei adesso

  [ok]    bus PCI          /dev/pci.drv &
  [manca] scheda di rete   netdetect -c
  [manca] stack IP         /dev/ip.drv &
  [ok]    tastiera         [modules] in /boot/kernel.cfg
```

! **The state is why it exists.** A list of commands to type is already in
this file; what you do not know at the prompt is how far you have got. It
costs one syscall per service and turns «here is the procedure» into «you
are here». The example above is a machine with no network card: the bus is
there, the card is not, and that is not a fault to chase.

The text is longer than a 25-line screen and stops on its own; the pauses
sit where the subject changes, not every N lines, because a page cut in the
middle of a list is worse than a shorter one. `q` stops.

### Ring3 drivers (current model, since July 2026)

A driver is an ordinary **static ET_EXEC** ELF32 executable, like the
programs in `/bin`. It executes no privileged instructions: the kernel
mediates every hardware access and checks the permissions on every call.

```c
int main(void)
{
    ipc_register("kbd");            /* make itself findable by name */
    ioport_bind(0x60, 5);           /* I/O port whitelist           */
    irq_bind(1);                    /* IRQ -> IPC messages          */

    for (;;) {
        IpcMessage m;
        ipc_recv(&m, buf, sizeof buf);

        if (m.sender_pid == IPC_SENDER_KERNEL &&
            m.type == IPC_TYPE_IRQ_NOTIFY) { /* hardware interrupt */ }
        else                                 { /* client request    */ }
    }
}
```

Clients find the service with `ipc_lookup("kbd")` and talk to it over
`ipc_send`/`ipc_recv`. Full reference: `drivers/kbd/kbd.c` and the protocol in
`drivers/kbd/kbd_proto.h`.

### 16- and 32-bit I/O accesses

Besides `ioport_in`/`ioport_out`, which work a byte at a time, a driver has:

```c
int ioport_in16 (unsigned int port);                    /* 0..65535, or -errno */
int ioport_out16(unsigned int port, unsigned int val);
int ioport_in32 (unsigned int port, unsigned int *out); /* 0, or -errno        */
int ioport_out32(unsigned int port, unsigned int val);
```

They are not a convenience. The PCI bus's CONFIG_ADDRESS register (0xCF8)
**must** be written with a single 32-bit access: a byte or word one is not
recognised by the bridge as a configuration cycle, and since 0xCF9 is the
reset register of many chipsets, writing it in pieces tends to reboot the
machine.

! `ioport_in32` **does not return the value read**: `0xFFFFFFFF` («no
device») as an `int` would be `-1`, indistinguishable from an error. The
value comes out through the pointer. `ioport_in16` does not have the problem
and returns it.

The port must be aligned to the width, otherwise `-EINVAL`: a misaligned
access is split by the chipset into two cycles, and on the PCI bus the second
one is no longer a configuration cycle.

### The PCI bus: `/dev/pci.drv` and `/bin/netdetect`

PCI enumeration is a **ring3 process**, not kernel code: it reads tables
written by third-party BIOSes and firmware, and a loop that does not
terminate on a malformed bridge must be a process to restart, not a hung
machine.

```
/dev/pci.drv -l          list the devices and exit
/dev/pci.drv &           register the "pci" service and serve clients
netdetect                network cards present and each one's driver
netdetect -t             table of recognised models
```

Example (VirtualBox with the default card):

```
ex-os:/> /dev/pci.drv &
[1] 8
pci: servizio 'pci' attivo, 8 dispositivi
ex-os:/> netdetect
00:04.0  1022:2000  AMD PCnet-PCI II / FAST III (Am79C970/C973)
           porte I/O da 0xc140, IRQ 11
           driver: /dev/pcnet.drv
```

The protocol is in `drivers/pci/pci_proto.h`. The server exposes
configuration reads and `PCI_MSG_ABILITA`/`DISABILITA` on the I/O, memory and
bus-master bits; **not** a generic configuration write, because reprogramming
the BARs of a device the kernel is using (the ATA controller, for instance)
would pull the disk out from under someone who knows nothing about that
write.

Both live **only on the EX-OS CD** (`make iso-exos`): the floppy is there to
boot and to install, and network tools without the network drivers — which
would not fit on the floppy — would be of no use once there.

### Interrupts: `irq_bind` and `irq_done` go in pairs

```c
irq_bind(11);                    /* from here IRQs arrive as IPC */
...
/* on the notification: */
servi_la_scheda();               /* clear the device's state     */
irq_done(11);                    /* ONLY NOW is the line reopened */
```

! `irq_done()` is not optional. The kernel **masks** the IRQ in the PIC
before delivering the notification, and without this call the line stays
closed: the driver receives one interrupt and then silence.

The reason is that a ring3 driver does not run inside the interrupt: ticks
pass between the notification and the moment it touches the card. On a
**level**-triggered IRQ — all the PCI ones — the device holds the line high
until its status register is cleared, so without masking the interrupt fires
again right after the `iret` and the driver process never gets the CPU to go
and clear it. The machine stops, without a panic.

The order matters: serve the device first, then reopen. Reopening with the
line still high brings the storm right back.

### Networking: `/dev/ne2k.drv` and `/bin/nettest`

The first network driver handles the NE2000/DP8390 family — RTL8029 on PCI
and Winbond/VIA/KTI clones. It lives in userspace because that card **does no
DMA into system memory**: the packet RAM is on the card and is reached
through an I/O port, so the kernel need know nothing about physical addresses
or locked pages.

```
ex-os:/> /dev/pci.drv &
ex-os:/> netdetect -c              # picks and starts the right driver
ex-os:/> nettest -a 10.0.2.2
chi ha 10.0.2.2? lo chiede 52:54:00:12:34:56 (10.0.2.15)

  64 byte  52:55:0a:00:02:02 -> 52:54:00:12:34:56  ARP (0x0806)
      ARP risposta: 10.0.2.2 e' 52:55:0a:00:02:02, cerca 10.0.2.15

Risposta ricevuta: 10.0.2.2 ha indirizzo 52:55:0a:00:02:02
```

`nettest` uses **ARP and not ping** on purpose: ARP is the first exchange
possible without having a stack, and a reply proves in one shot that the card
transmits, that the frame arrives, that the card receives and that the chain
driver → IPC → program delivers the right bytes. If ARP works, all `ping`
lacks is software.

The protocol (`drivers/net/net_proto.h`) is that of **every** network driver,
not of the NE2000: the PCnet will speak the same language. Two choices that
hold for all of them:

- **The driver never pushes an unrequested frame.** `ipc_send` blocks if the
  recipient's mailbox is full, and a driver and a stack pushing data at each
  other end up stuck, each inside its own `ipc_send`. Request and reply is
  used instead (`NET_MSG_RICEVI`), as with the keyboard driver.
- **A heartbeat every 250 ms.** `ipc_notify_irq` does not block: if the
  driver's mailbox is full when the interrupt arrives, the notification is
  dropped and the line would stay masked forever. The timeout on the wait
  makes a lost notification cost a delay, not a dead interface.

! An **ISA** NE2000 is not probed for on its own: recognising it would mean
writing to its reset port, and if another card is there you write on top of
it. It must be declared: `/dev/ne2k.drv -p 0x300 -q 3`.

### Networking: `/dev/pcnet.drv` — the first card that writes to RAM by itself

AMD PCnet-PCI II / FAST III (Am79C970, C970A, C971, C972, C973: on the bus
they all show up as `1022:2000`). It speaks the same protocol as the ne2k,
so the IP stack does not know which of the two is underneath.

! **The difference from the NE2000 is everything.** That one keeps the
packet memory *on the card*, reached through an I/O port: that is why it was
the first driver — it asked the system for nothing new. The PCnet is a **bus
master**: it reads and writes system RAM by itself, at the **physical**
addresses it was given, without going through the MMU.

Two things follow that did not exist before:

| | |
|---|---|
| the **bus master** bit in the PCI command | without it the bridge blocks every cycle the card initiates. The registers read and write perfectly — those go through us — but the card cannot even read its own initialisation block |
| **`SYS_DMA_ALLOC`** | physically contiguous memory whose physical address is known |

> ! **A wrong address here does not give an error.** Giving the card a
> virtual address instead of a physical one produces no fault and stops
> nothing: it produces a card writing packets into a random point of
> physical memory. On a small machine that point is often the kernel, and
> the symptom arrives minutes later, elsewhere. That is why `dma_alloc`
> returns the two addresses separately and under different names — `virt`
> for the process, `fisico` for the card.

`SYS_DMA_ALLOC` can only be asked for by **someone who already holds an I/O
port window**. It is not a rigorous defence: it is the way of saying this is
for drivers. Contiguous, non-freeable memory is the scarcest resource there
is, and the ceiling is 64 pages per process.

Two traps in the format, both silent:

- **BCNT is two's complement** over twelve bits, with the four bits above it
  set to ones. A 2048-byte buffer is declared as `(-2048) & 0xFFF | 0xF000`;
  writing 2048 in plain gives a card that believes it has a buffer of 2048
  *negative* bytes.
- **The reset is done in WIO**, before the switch to 32-bit, because the
  offset of the reset register differs between the two modes.

```
ex-os:/> nettest -c
inviati        7
ricevuti       7
notifiche IRQ  7
battiti        89
```

! **`notifiche IRQ 7` against 7 frames is the line that counts**, not
`ricevuti 7`. The driver looks at the card on every heartbeat too: without
that number, a network working with 250 ms of delay would be
indistinguishable from one that works. It is the same check that uncovered
the never-unmasked PIC cascade.

! **`-l` does not probe a card someone else is driving.** Reading its state
would mean resetting it, and if another process is using it that reset takes
the network away without giving either of them an error. If the service is
already there, `-l` *asks* it: the answer comes from whoever is actually
using the card. It happened on the very first try, and the symptom was
unreadable — `CSR0 = 0x3b, atteso STOP`.

### The sector cache: the compiler runs twice as fast

The hard disk has a 128-sector (64 KB) cache in `kernel/block/blk.c`. The
gain, measured by changing **a single constant** and rebuilding:

| `cc1` compiling | time |
|---|---|
| without cache | **19.61 s** |
| with cache | **10.19 s** |

! **On a sequential 35 MB copy it changes nothing** (~80-100 s either
way), and that difference is what a cache is actually for. To reach each
4 KB page of a large file, ext2 first reads the **indirect blocks** that
say where that page lives: small requests, always the same ones, and that
is the work the cache removes. The data itself passes once and has nothing
to reuse.

Hence an oddity that looks like a measurement error and is not: `cc1`
**from the CD** ran faster than the same `cc1` from the hard disk without
a cache. Not because the CD is fast — because ISO 9660 has no indirect
blocks to chase and reads 2 KB per command instead of 1 KB. With the cache
the hard disk draws level with the CD.

Three decisions, and why:

- **Only requests up to 8 sectors.** A cache can ruin itself: pushing
  `cc1`'s 34 MB through it would evict every useful sector to make room
  for data nobody will ever read again.
- **Write-through, not write-back.** A write goes to disk *immediately*,
  then updates the copy. That way `vfs_sync` keeps meaning what it has
  always meant, and a brutal power-off loses nothing that was not already
  lost. Write-back would be faster and would be **a different promise**.
  The disk has a cache of its own, though: since kernel 0.284 `fsync` sends
  it FLUSH CACHE.
- **The key is (physical disk, absolute LBA)**, taken *after* partition
  translation: relative LBAs of two partitions on the same disk overlap,
  and using those would hand one partition the other's sectors.

! **It is flushed on `blk_rescan` and `blk_ripartiziona`.** Without that,
after a `mkfs` the old sectors would still be served, and the symptom
would be "a freshly created filesystem is corrupt".

### ! `rep insw` instead of 256 calls per sector

A sector's PIO transfer used to be a C loop calling `port_inw` **256
times** — and `port_inw` is a real function, not a macro: 256 `call`s and
as many `ret`s, plus assembling both bytes of every word by hand. Over two
thousand instructions for 512 bytes. It is now **one** instruction, in
`ata.c` and in `atapi.c`.

! In ATAPI the fast path applies **only if the burst fits the buffer**:
`rep insw` just writes, it cannot skip the excess bytes, and a burst must
*always* be consumed in full or the channel is left unusable. When it
overflows, the slow loop takes over.

! On its own this change **does not show up in the measurements**, which
is useful information: the cost of the disk is not the transfer, it is the
commands. That 35 MB copy is ~17,000 ATAPI commands plus ~35,000 ATA ones,
each with its own `BSY`/`DRQ` wait. The missing lever is coalescing
contiguous requests — and that is also what will make bus-master DMA worth
having, since today it would cut the cost of the part that already does
not weigh.

### The `drv_*` interface (previous model, kernel-space)

```c
int  drv_init(void);
int  drv_read(void *buf, size_t n);
int  drv_write(const void *buf, size_t n);
int  drv_ioctl(int cmd, void *arg);
void drv_exit(void);
```

Still used by `drivers/tty/tty.c` (compiled into the kernel) and by
`drivers/floppy/floppy.c` (an ET_DYN module no longer loaded). ET_DYN modules
run in ring0 and have to be rewritten against the model above.

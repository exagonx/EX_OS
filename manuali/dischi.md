# EX-OS — Disks, filesystems, CDs and DVDs

**🇬🇧 English** · [🇮🇹 Italiano](dischi.it.md)

[← README](../README.md) · [All the manuals](../README.md#the-manuals)

**Contents**

- [Setting up a hard disk: /bin/fdisk and /bin/mkfs](#setting-up-a-hard-disk-binfdisk-and-binmkfs)
- [CDs and DVDs — ATAPI driver and ISO 9660](#cds-and-dvds--atapi-driver-and-iso-9660)
- [Long names on FAT (VFAT), reading](#long-names-on-fat-vfat-reading)
- [`mkfs` picks the filesystem from the size](#mkfs-picks-the-filesystem-from-the-size)

---

## Setting up a hard disk: /bin/fdisk and /bin/mkfs

The full cycle, from a blank disk to a bootable system:

```
disk                        what the system can see
fdisk hd0                   create the partitions, mark the first active
mkfs -t ext2 -L exos hd0p1  write a filesystem into it
mount hd0p1 /disk           mount it
install /disk               make it bootable
```

Then remove the floppy and reboot. It works both on **FAT16/FAT32** and on
**ext2**.

### The sector map, and why the kernel keeps a list of it

The boot sector cannot read any filesystem: in 512 bytes, minus the BPB and
the signature, there is no room. It is handed the LBA and length of Stage 2
and of the kernel, and it reads sectors. It is `install` — which runs
*inside* EX-OS, where the drivers already exist — that builds that map.

! The price is the same bargain LILO made: **copying a new kernel or Stage 2
means running `install` again.**

### `install` verifies before replacing (since 0.161)

Running it again on an already installed system **rewrites every file**, the
kernel included, and reads each one back to compare its size. In the report
`+` means created, `~` replaced, `!` error.

Up to 0.147 files already present were skipped: an update copied only the new
files, left the old kernel in place and then rewrote the sector map *for
that one* — the disk booted the previous version again and the installer said
«completed». Directories are still not recreated, and whatever on the volume
is not part of the system stays where it is: `install` updates, it does not
wipe.

**And up to 0.160 it destroyed before knowing whether it would succeed.** It
opened `/boot/kernel.bin` with `O_TRUNC`, wrote the new kernel into it, and
*then* asked for the sector map. On FAT — where the map allows **a single
extent** — a kernel that had grown by a few KB no longer fitted in the hole
left by the old one, ended up in two runs, and `bootinstall` rightly refused:

```
! installazione dell'avvio fallita: file frammentato (errore -29)
```

…but by then the system that worked was gone, the boot sector still pointed
at the old map, and **the disk would not boot**. On ext2 it did not show:
there the map holds 12 extents.

The new files are now written **under temporary names while the old ones are
still in place** — so they land in the free tail, contiguous. Then the kernel
is asked whether they are mappable. Only if the answer is yes are the old
ones deleted and the new ones renamed:

```
Avvio (scritti a parte e verificati prima di sostituire)
  = verifica: 585 settori in 1 intervallo — si puo' sostituire
  ~ /disk/boot/stage2.bin  (verificato, poi sostituito)
  ~ /disk/boot/kernel.bin  (verificato, poi sostituito)
```

The scenario that used to destroy the disk now **succeeds**. When it cannot
be done, you read `Il sistema gia' installato NON e' stato toccato` and the
disk stays bootable.

> ! **A missing primitive was needed.** The swap could not be done:
> `rename()` was copy+delete, so it reallocated the blocks and threw away the
> verification just performed. See *The rename that does not move the data*
> further down.

**`kernel.cfg` is no longer overwritten.** It is the one installer file that
belongs to whoever uses the system rather than to the system: automatic
mounts, `verboseboot`, shell, environment variables. An update must not put
them back silently. If it is missing it gets installed, if it is there it is
left alone and that is said out loud.

### `install -a` — updating instead of reinstalling

```
install -a /disk
```

It compares the mounted volume with the boot medium, **lists what would
change**, asks for confirmation and only then writes. `+` is a file that is
not on the disk, `~` one that is there but different.

```
Confronto di /disk con il supporto di avvio
  +  da creare    ~  da sostituire
  + /disk/bin/ftp
  ~ /disk/bin/ls
  ~ /disk/boot/kernel.bin

35 file da aggiornare (14 nuovi).
Procedo? [si/no]
```

The list comes **before** the question because "update 3 files?" and "update
47 files?" are two different decisions: an update that touches everything
when you expected a small change is the moment you realise you mounted the
wrong volume.

> ! The rule is **"the source is newer"**, not "the dates differ". Copying a
> file does not preserve its date: the copy on the disk is born with the
> current time, so under the naive rule every file would come out as needing
> an update on every run, forever, even right after being copied.

Size is compared **first**, and it is the check that matters most: a
half-written file has the same date and a different size, and without that
comparison the volume stays broken with nobody saying so. If either date is
zero — that is, "this volume does not keep dates" — only the size is looked
at.

Without `-a`, `install` still does the full installation: it rewrites
everything, which is what you want the first time and after a `mkfs`.

For the kernel the map is a **list** of extents, not a single one. On ext2 a
file is almost never contiguous, and not because of fragmentation: the
*pointer* block is allocated in the middle of the data, because it is needed
before the thirteenth block. A freshly copied 147 KB kernel looks like this:

```
(0-11):74-85, (IND):86, (12-144):87-219
```

Stage 2, on the other hand, stays a single extent: it fits in ~1 KB, that is,
within the 12 direct blocks, where no indirect has slipped in yet — and it is
the piece that has to be found by 512 bytes of code, so its map must fit in
six bytes.

On FAT the list has a single entry: the format is the same for both
filesystems, so Stage 2 does not have to know what it is loading from.

### `chkdsk` — checking and repairing a FAT volume

```
chkdsk <partition>       checks and reports, does not write a sector
chkdsk -r <partition>    checks and repairs
```

It checks, in this order — each step trusts only the ones already done: the
BPB and the consistency of its numbers, the FAT copies, the cluster chains by
walking every directory (shared clusters, chains outside the volume, loops),
each file's declared size against that of its chain, the long names, `.` and
`..`, and the lost clusters.

```
tipo FAT32 — 130556 cluster da 8 settori, 2 FAT da 1021 settori
= FAT[0] = 0xffffff8, FAT[1] = 0xfffffff
= le 2 copie sono identiche
! 3 cluster risultano occupati ma nessun file li nomina (12 KB)
```

> ! **It only works on an unmounted partition**, and that is not an
> annoyance to work around: above a mounted volume there is a write-back
> cache, and half of the recent changes are in RAM. A checker reading the raw
> sectors would report invented inconsistencies, and while repairing would
> rewrite sectors that the first `sync` would cover right back up.

> ! **Without `-r` it does not write a single sector.** A checker that
> repairs on its own initiative turns a damaged volume into a *differently*
> damaged volume, without anyone having seen what it looked like.

A few choices that are not obvious:

- **the type is derived from the cluster count**, not from the `"FAT16   "`
  string in the boot sector: that one is decorative and nobody ever checks
  it, so on a sick volume it is precisely the field not to trust;
- **FAT12 is read with two sectors in hand**: an entry is twelve bits wide
  and may start in the last byte of a sector. When writing, if it straddles
  the boundary, it **gives up and says so** instead of writing half a value —
  that would be fresh damage caused by the tool that was supposed to repair
  some;
- **on FAT32 the top four bits of an entry are not ours**: they are
  preserved;
- **size is compared against a range**, not against a number: a file of N
  bytes occupies `ceil(N/cluster)` clusters, and demanding exact equality
  would flag almost every file as broken;
- **shared clusters are not repaired automatically**: which of the two files
  is entitled to the data cannot be deduced, and repairing them by decree
  would mean picking at random which one to ruin;
- **lost clusters are freed, not collected into `FOUND.000`**: that would be
  a collection of fragments with neither name nor structure, taking up the
  very space you wanted to recover.

On **long names**: a row of fake entries precedes the 8.3 one, in reverse
order, tied to it by a single checksum.

> ! Whoever renames by touching only the 8.3 entry — and **EX-OS's `rename`
> does exactly that** — leaves the long name naming a file that is no longer
> the same one. It is not a textbook case: it is a defect this system knows
> how to produce, and it is the reason the check belongs here.

### Names are created in lower case

The kernel looks for `/bin/sh`, `/boot/kernel.cfg`, `/dev/kbd.drv`. On FAT
case does not matter — the driver upper-cases both what it writes and what it
looks for — but on **ext2 `BIN` and `bin` are two different directories**, and
a system installed into `BIN` would not find its own shell. `install` creates
everything in lower case, file names included.

### /bin/fdisk — MBR partitioner

```
fdisk            list the disks
fdisk hd0        open a session on disk 0

  p  show the table and the free space    a  toggle the bootable flag
  n  create a partition                   w  WRITE (asks for confirmation)
  d  delete a partition                   q  quit without writing
  t  change the type
```

**Nothing touches the disk until `w`.** A table written a piece at a time
passes through states in which the partitions overlap; if the machine dies in
the middle, it stays wrong.

It is a separate program from `disk`, which stays **read-only**: that is the
command you run without thinking twice on a disk you care about, and a
program that depending on its arguments either looks *or* rewrites loses that
guarantee for every use.

The **policies** live in `fdisk` (1 MiB alignment, first usable sector 2048,
defaults). The **rules** live in the kernel and cannot be worked around: no
overlaps, no partitions past the end of the disk, no writing to a GPT disk or
to a mounted partition.

It does not handle **logical** partitions and does not write EBRs: it shows
them, but the extended entry containing them is locked — moving it would
leave their chain alive on the disk and unreachable.

`fdisk` does not format. A freshly created partition contains the bytes that
were in those sectors before: it is not empty, it is uninitialised.

### /bin/mkfs — FAT16/FAT32/ext2/ext3/ext4 formatter

```
mkfs -t fat32 -L LABEL hd0p1
mkfs -t fat16 hd0p2
mkfs -t ext2  -L data hd0p3
```

The partition **must not be mounted**: above a mounted volume there is a
write-back cache, and writing underneath it means the first `sync` will cover
your work with the old sectors.

The number that matters is the **cluster count**, and `mkfs` shows it next to
the threshold. A FAT volume's type is not written anywhere: the `"FAT16   "`
string in the boot sector is decorative, and the type is deduced from the
cluster count of the data area (< 4085 → FAT12, < 65525 → FAT16, above →
FAT32). A formatter that picks the sectors per cluster badly produces a
volume that *says* FAT16 and *falls* into the FAT12 band, and nobody notices
until the data is already ruined.

The old boot sector is zeroed **first** and the new one written **last**: in
between, the volume is recognisable to nobody. The opposite order would
leave, on an interrupted format, a boot sector describing the old filesystem
on top of already-zeroed FAT tables — a volume that mounts, seems to work,
and returns empty files.

`mkfs` does not touch the partition table, and not by choice: the
`SYS_BLKREAD`/`SYS_BLKWRITE` syscalls only accept **partition** names, and
sector 0 belongs to no partition. There is no (name, LBA) pair that reaches
it. If the type byte in the table contradicts the filesystem created, `mkfs`
points it out and says how to fix it with `fdisk`.

### ext2 — creating and reading

`mkfs -t ext2` creates a revision 1 ext2 (`filetype`, `sparse_super`) with
1024-byte blocks. It is written **from the specification**, not ported from
e2fsprogs nor from Linux's driver: the latter is not a module that reads
ext2, it is a module that *translates* ext2 into Linux's VFS, and porting it
would mean porting that VFS.

`kernel/fs/ext2.c` reads it **and writes it**: `mkdir`, `cp`, `delete`,
`rmdir`. Reading was written and verified first, on its own — reading
requires understanding the format, writing requires never breaking it, and
with a reader already tested against volumes made by `mke2fs` every write bug
shows up immediately for what it is.

### /bin/trunc — change a file's size

```
trunc <file> <bytes>     K and M suffixes accepted
```

**Growing takes no space** on ext2: the space in between becomes a *hole*
that reads as zeros, and the blocks materialise only when written to. A file
grown to 2 MB this way occupies a single block. On FAT the kernel allocates
for real, because FAT cannot represent a hole.

**Shrinking is destructive and does not ask for confirmation**, for the same
reason `delete` does not ask on an exact name: whoever types a file name and
a number smaller than its size has already said what they want. Confirmation
is for when the command does more than the user named.

On the floppy it answers `-38` (ENOSYS): `fat12.c` has no truncation, and it
is the boot volume's driver — the proven road you do not touch without a
strong reason.

! ext2 has no journal and this driver does not invent one: an interruption
in the middle of an operation can leave the free counters behind the bitmaps,
and that is what `e2fsck` is for. What **cannot** happen is a block looking
free while already in use — the bitmaps are always written before the block
is handed out.

**Long names**: up to 255 characters, ext2's maximum. It was not just the
`VfsDirEntry` structure that had to grow — a name crosses six ceilings in a
row (driver, VFS, syscall ABI, path length, `spawn` arguments, keyboard line)
and raising only one of them would have moved the cut by one step. A
truncated name is not a shortened name: it is a name that opens nothing.

On FAT names stay 8.3; the field is sized for the most generous filesystem.
A command line goes up to 32,768 characters, at the console as over telnet
(kernel 0.353): a line longer than one IPC message between the
keyboard and the shell travels in several messages. See
[Command history and line editing](comandi.md#command-history-and-line-editing).

The driver reads and writes volumes made by `mke2fs` too: the block size
(1024/2048/4096), `s_first_data_block` (1 or 0 depending) and the inode size
(128 or 256) all come from the superblock, never taken for granted.

It refuses to try, however, when it finds an **incompat** feature it does not
know — an ext4 volume with extents has an `i_block` that does not contain
block numbers, and reading it "as if" would return data picked at random from
the disk with nothing flagging it.

---

### `install -m` / `install -t` — minimal, or with components

```
install /disk          shows the components and asks about them one by one
install -m /disk       minimal system only, no questions
install -t /disk       system and every component, no questions
```

The installer looks at the root of the boot medium and calls **minimal system**
a closed list: `bin`, `boot`, `lib`, `dev`, `drivers`. Any other directory is a
component, and it is shown and asked about:

```
===============================================================
 COSA INSTALLARE
===============================================================

Il sistema minimale e' sempre incluso: kernel, /bin, /lib,
i driver e l'avvio. Senza quello EX-OS non parte.

Su questo supporto ci sono anche 2 componenti in piu':

    /doc
    /exwin

Installo solo il sistema minimale? [si/no] no

Uno per volta:

  /doc ? [si/no] no
  /exwin ? [si/no] si
```

! **ADDING A PACKAGE DOES NOT REQUIRE TOUCHING THE INSTALLER:** you put its
directory on the medium, and that is all. A list written inside `install` would
be a second truth beside the directory's contents, and the two diverge the
first time a package is added or removed. **Whoever prepares a package does not
have the installer's sources.**

! **COMPONENTS ARE COPIED WITH ALL THEIR LEVELS.** `/exwin` contains no files,
it contains `bin/ lib/ dev/`: copying it one level deep would give an empty
directory **and no error**, that is, an installation that looks successful.

! **AND THE CHOICE IS MADE BEFORE ANYTHING IS WRITTEN**, not halfway: asking
after the kernel has been replaced would mean that answering "cancel" no longer
cancels anything.

Scripts that install with nobody watching — `tools/mkhd.sh` — use `-t`. Without
the flag the installer would stop on a question nobody answers, and the test
would report "the installation did not get to the end": a message that does not
resemble its cause.

---

## CDs and DVDs — ATAPI driver and ISO 9660

```
disk                    the drive shows up as cd0
mount cd0 /cdrom        manual mount (always read-only)
ls /cdrom
umount /cdrom           before ejecting the disc
```

And in `/boot/kernel.cfg`, to have it **mounted at boot**:

```ini
[mount]
/cdrom = cd0
```

The line is active in the default configuration, and it can stay so on any
machine: an empty — or absent — drive produces no warning, just a "mount
skipped" in the log. A missing CD at power-on is the normal condition, not a
problem, and reporting it as one would put a line among the *problems during
initialisation* at every boot.

### The three things a drive does not share with a disk

**The block is 2048 bytes, not 512.** The translation lives in a single
place, `kernel/block/blk.c`: the rest of the system asks for 512-byte sectors
as it would for any disk. Aligned requests — that is, nearly all of them,
because ISO 9660 works in blocks — go straight to the device with no
intermediate copies.

**The capacity belongs to the disc, not to the drive.** A `cd0` with zero
sectors is not a broken drive: it is an empty one, or one nobody has probed
yet. The field is filled in by `blk_supporto()` when needed, and cleared when
the disc comes out.

**Errors come at two levels.** The ERR bit only says "CHECK CONDITION": the
reason is in the *sense* data, which has to be asked for with a second
command. Without reading it, «there is no disc», «the disc has just been
changed» and «the disc is unreadable» are the same thing — and the first two
are not errors.

A disc **inserted with the system already running** mounts without a reboot.
A freshly loaded drive, though, still answers "medium not present" for a
command or two, and under emulation the tray stays *open*: the driver insists
a few times and closes it once, as Linux does. The consequence must be said
out loud — mounting on a drive left open and empty closes it.

### ISO 9660, and why Joliet wins when it is there

A disc burned with long names contains **two complete trees**: the ISO 9660
one, with upper-case, truncated names carrying a version number
(`LEGGIMI.TXT;1`), and the Joliet one, with the real names in UCS-2. They are
not two views of the same structure: they are two separate directory chains
pointing at the same data. `kernel/fs/iso9660.c` picks Joliet when it is
there, and says which one it picked; without that, the names you see are not
the ones the user wrote.

On ISO names it strips the `;1` and the trailing dot — those are format, not
name — and shows them in lower case; the comparison is case-insensitive,
otherwise what `ls` shows would not be typeable.

**Read-only, and not out of laziness**: ISO 9660 has neither a free-space
bitmap nor reusable entries. "Adding a file" does not exist; remaking the
image does. Every write is refused with `-30` (EROFS) before the volume is
touched.

**Rock Ridge is not handled** — the Unix extension nested in the records'
system-use fields. A disc that uses it stays readable: you see the ISO or
Joliet names, which are there anyway.

### `make iso` — the tools CD

```bash
make iso        # dist/exos-tools.iso
make run-iso    # start QEMU with the CD already inserted
```

A second medium, separate from the floppy and **not bootable**: it holds what
does not fit in 1.44 MB and what not everybody needs. The floppy stays the
proven boot medium; the tools change often, they are heavy, and they must not
be able to break booting.

The disc holds, today:

```
/leggimi.txt          what this disc is and how to mount it
/exos/include/        the libc headers (stdio.h, stdlib.h, …)
/exos/libc.c          the libc in a single file
/exos/start.S         the startup piece that calls main()
/doc/                 README, kernel notes, license
/bin/                 as, ld, cc1 and the test programs
```

`/exos/` is not documentation: it is what is needed to **compile on EX-OS**.
`/bin` is no longer empty: it holds binutils 2.44's `as` and `ld` and GCC's
`cc1`, that is, the real compilation chain — see
[The compilation chain inside EX-OS](sviluppo.md#the-compilation-chain-inside-ex-os).

! The CD is read-only by construction, so headers and libraries are read
from there but **the output of a compilation has to go somewhere else** —
that is, to an EX-OS installed on ext2, or to the floppy.

### Testing the driver without burning anything

`tools/mkiso.py` also generates a synthetic test image whose every byte is
known — useful precisely because, when the driver reads a wrong name, you
know what was written there:

```bash
python3 tools/mkiso.py /tmp/test.iso --prova                  # with Joliet
python3 tools/mkiso.py /tmp/solo-iso.iso --prova --senza-joliet
qemu-system-i386 -fda dist/floppy.img -m 32M -boot a -cdrom /tmp/test.iso
```

The same tool acts as a burner for any directory tree:

```bash
python3 tools/mkiso.py /tmp/mio.iso --da /path/to/tree --etichetta "MY CD"
```

It builds **both** trees — upper-case ISO 9660 8.3 names with `;1` and real
Joliet names in UCS-2 — sharing the files' blocks, and it handles long names,
subdirectories and 8.3 truncation collisions (two distinct files that ended
up with the same name would be a file lost in silence).

---

## Long names on FAT (VFAT), reading

A FAT32 written by Linux or by Windows is read with the real names:

```
ex-os:/> ls /disco
appunti di riunione.txt 19
UnNomeMoltoLungoDavveroInterminabile.dati 19

ex-os:/> cat "/disco/appunti di riunione.txt"     works
ex-os:/> cat /disco/UNNOME~1.DAT                  the alias works too
```

**Both** ways work: the long name and the 8.3 alias. Comparing against the
long one only would make it impossible to open a file by its short alias,
which is a legitimate name and the one old programs use.

! **The checksum is not optional.** Every long-name entry carries the
checksum of the 8.3 name it belongs to, and it is there to spot **orphaned**
chains: a system that does not know about long names can delete the 8.3 entry
leaving its fragments behind, and attaching them to the first 8.3 name that
comes along would give a file the name of another one.

! **Reading only.** Creating a file with a long name would mean allocating
several consecutive entries and inventing a unique 8.3 alias (`NOME~1`,
`NOME~2`…): that is another thing, and it is not there. A file created by
EX-OS has an 8.3 name, and it shows.

! **ASCII only**: characters above `0x7F` become `?`. EX-OS has no character
table, and inventing one here would mean choosing an encoding for the whole
system.

## `mkfs` picks the filesystem from the size

```
mkfs hd0p1        up to 2 GB → FAT16, above → FAT32
mkfs -t ext2 hd0p1
```

It is not an arbitrary threshold: FAT16 reaches **65524 clusters**, which
with 32 KB clusters is a little over 2 GB. Below that size FAT16 is
preferable — a table half the size and a fixed-size root directory, that is,
fewer sectors to read to do the same thing.

! **ext2 never enters the automatic choice**: it is a format you ask for,
not one you end up in.

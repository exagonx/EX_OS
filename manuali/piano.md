# EX-OS — The development plan (historical)

**🇬🇧 English** · [🇮🇹 Italiano](piano.it.md)

[← README](../README.md) · [All the manuals](../README.md#the-manuals)

> The plan EX-OS started from, and the state of its phases up to August 2026. It is kept as history: what is there today is in the [README](../README.md) and in [What's new](novita.md).

## Development plan (Hybrid Strategy D)

- [x] **Phase 1a** — Bootloader Stage1 + Stage2 + FAT12 read
- [x] **Phase 1b** — Kernel entry, GDT, IDT, ISR, VGA, kprintf
- [x] **Phase 1c** — Physical Memory Manager (E820 + bitmap)
- [x] **Phase 1d** — x86 paging + kernel heap (kmalloc)
- [x] **Phase 2a** — Preemptive 100Hz scheduler + context switch
- [x] **Phase 2b** — int 0x80 syscall interface
- [x] **Phase 3**  — TTY driver + kernel FAT12 R/W + ELF loader
- [x] **Phase 4**  — User shell + cfg reader
- [~] **Phase 5**  — Userspace (ring3) drivers: keyboard done, floppy to do
- [~] **Phase 6**  — Hosting system: POSIX libc, native `as`, `ld` and `cc1`
                     done; the `gcc` driver program is missing
- [~] **Phase 7**  — Networking: PCI, NE2000, ARP/IPv4/ICMP/UDP/TCP, DHCP, DNS
                     and an FTP client done; TLS to do

**Phase 4 status (July 2026)**: the shell starts as the first ring3 process,
reads `/boot/kernel.cfg`, and runs external programs (`hello`, `ls`, `cat`)
as separate tasks returning to the prompt. The PIC deadlock that blocked
every program started from the shell is fixed — see `HANDOFF.md`.

**Phase 5 status (30 July 2026)**: `/dev/kbd.drv` is the **first EX-OS driver
that really runs in ring3**. It is a process with its own page directory that
executes no privileged instructions and calls no kernel symbols: it reads
scancodes with `SYS_IOPORT_IN`, receives IRQ1 as IPC messages via
`SYS_IRQ_BIND`, and delivers the typed lines to the TTY with `SYS_IPC_SEND`.
The TTY stays in-kernel for the VGA but for input it is a client of the
service.

The floppy driver is still a kernel-space ET_DYN module and floppy access is
still served by the kernel's internal FAT12/FDC. Analysis and the steps
needed are in `KERNEL_CORE_NOTES.md`, point 5; design details and traps in
`HANDOFF.md`.

**Phase 6 status (2 August 2026)**: the libc has grown enough to carry code
written for POSIX, and the proof is that **binutils 2.44 runs inside EX-OS**.
Porting it uncovered three kernel defects that no EX-OS program could show,
because all of them wrote a file from beginning to end:

- descriptors still open at termination were closed by nobody — one VFS slot
  lost per file, and `EMFILE` after 64 times;
- on the **floppy**, writing ignored the descriptor's position and always
  appended at the end (not on ext2 and FAT16/32: there the offset arrived);
- writing at the **start** of a sector zeroed the whole of it, erasing the
  bytes that were behind.

All three invisible as long as nobody goes back in a file. Any ELF writer
does.

**Phase 6 status, continued (August 2026)**: **`cc1` compiles C inside
EX-OS** and produces real assembly, which `as` and `ld` turn into an
executable. Getting there uncovered three more defects, all in the libc and
all invisible to EX-OS's own programs:

- `realloc` **never** grew in place — merging with the following block
  refused non-free blocks, which is precisely the case to handle;
- nobody called `.init_array`'s global constructors: `cc1`'s 57 entries were
  not run and the first structure used was empty;
- `printf` with `%f` invented digits past the eighteenth and rounded 2.5 to 3
  instead of to 2.

**Phase 7 status (August 2026)**: the network starts at the bus and reaches
an FTP transfer verified byte for byte. The defect that cost the most was not
in the network: **line 2 of the PIC — the cascade — was never unmasked**, so
no IRQ from 8 to 15 could reach the CPU. It showed because `ping`'s
round-trip time was *exactly* the driver's heartbeat: it was not the network
answering, it was the timer. The counters `notifiche IRQ 0, battiti 131` said
it in plain words.

**What is still missing**, in order of how much it will get in the way:

| | |
|---|---|
| **`gcc` as the driver program** | `cc1` compiles and produces assembly, `as` and `ld` are there: what is missing is whoever chains them, passing the intermediate files |
| **out-of-order TCP** | segments arriving out of order are dropped instead of reordered, and the RTO is fixed rather than measured |
| **DNS: A records only** | CNAMEs are skipped instead of followed; if the reply does not already contain the final A record, the name does not resolve |
| **DHCP renewal** | `dhcp` takes the lease and finishes; renewal wants a process that stays alive |
| **TLS** | without encryption there is no HTTPS and no SFTP; the first step is an entropy source, not the protocol |
| **exFAT** | ! it does not exist as a filesystem: it has to be implemented first, and only then does a chkdsk make sense |
| **`rename` across directories** | ENOSYS today: it would be a copy plus a delete, that is, another operation |
| **DMA for the disk** | 0.75 MB/s in PIO today |

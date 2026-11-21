# EX-OS — The kernel

**🇬🇧 English** · [🇮🇹 Italiano](kernel.it.md)

[← README](../README.md) · [All the manuals](../README.md#the-manuals)

**Contents**

- [Kernel architecture](#kernel-architecture)
- [Syscall interface (int 0x80, Linux style)](#syscall-interface-int-0x80-linux-style)
- [Halting and powering off](#halting-and-powering-off)
- [System identity and version](#system-identity-and-version)
- [Quiet boot](#quiet-boot)

---

## Kernel architecture

```
CONVENTIONAL RAM < 1MB — EX-OS kernel (read-only, small)
  GDT | IDT | ISR | VGA | PMM | Paging | Heap | Scheduler | Syscall

EXTENDED RAM > 1MB — Everything else (protected, isolated)
  ELF drivers (/dev/)  — isolated crash, does not touch the kernel
  Processes (/bin/)    — separate address spaces
  Libraries (/lib/)    — shared, mapped into every process
```

### A program's pages arrive when they are needed

Since 0.149 the ELF loader does not copy segments into RAM: it records where
they live in the file, keeps the executable open, and the pages arrive on
first access, from the page-fault handler. A binary with **8 MB of constant
data** starts up occupying 36 KB and grows to 8 MB only if it is all read.

Startup cost no longer depends on the size of the binary — which is the
precondition for running a compiler in here, where `cc1` alone is tens of MB.

**Drivers** are the exception and are loaded fully into RAM: a driver serving
the filesystem, paged from that same filesystem, would have to serve its own
read while stopped waiting for it.

A consequence worth knowing: **the executable stays open as long as the
process lives**, and loaded pages are never thrown away to be read again
from the file. When RAM runs out and there is a swap area, they go to the
area like any other private page (since kernel 0.276: see *Swap works*).

### The thread pointer: `__thread` variables

Since 0.154 the loader recognises `PT_TLS` and makes a per-process copy of
it, below the stack reservation with a guard page in between. GDT descriptor
number 6 (selector `0x33`, the one processes keep in `GS`) is User Data with
a **base that changes**: the scheduler rewrites it at every switch with the
incoming process's thread pointer.

This is the **local-exec** model, the one static binaries use: variable
offsets are resolved by `ld` at link time, so there is nothing to relocate at
run time. With a zero base the descriptor is indistinguishable from `0x23`,
so anyone not using `__thread` pays nothing.

> ! There is no **dynamic** TLS (`__tls_get_addr`, thread-local variables
> inside a shared library): that serves code loaded at run time, and here the
> binaries are static.

**And since 4 September 2026 threads do exist, and the TLS block belongs to
each of them.** Every thread has its own, at the top of its own stack, with the
initial image **read again from the executable** — copying the group leader's
would mean starting the thread with another flow's *current* values. `errno` is
per thread too: `__errno_dove()` reads the thread pointer from `%gs:0` and uses
it as a key. That is why the block is made **even for programs with no
`__thread` variables**: without it the descriptor's base would be zero and that
read would be a page fault at address 0.

Why do it, if a process has a single thread and a `__thread` variable is a
global with a longer name? Because the way it was *missing* was the worst
possible one: the test every `configure` runs for TLS is a **compilation**,
and the compiler always passes it — it has known how to emit `%gs` accesses
for twenty years, and it is the system that has nowhere to point them.
No error, no warning, a binary that builds perfectly and dies at the third
instruction of the first function it calls.

### A process's address space, and the heap ceiling

```
0x08000000  program text, data, bss
            heap ---->                             (sbrk, mmap without MAP_FIXED)
0xbff44000  heap_max — the ceiling
0xbff44000  guard page
0xbff45000  band of the thread stacks — seven 64 KB slots,
            with a guard page between one and the next
0xbffbc000  guard page
0xbffbd000  the process's TLS block, if the program has one
0xbffbe000  guard page
0xbffbf000  stack reservation (256 KB)   <---- the stack grows downwards
0xbffff000  top of the stack
```

*(The addresses are those of a program with a one-page TLS block: a larger
block pushes everything below it further down.)*

The heap starts **right after the last loaded segment**, not at a fixed
address. Since 0.156 it also has a **ceiling**: a guard page below the first
object that is really there — today the thread band, and below it the TLS block
and the stack reservation.

! **THE BAND IS RESERVED AT LOAD TIME, EVEN FOR A PROGRAM THAT WILL NEVER MAKE
A THREAD**, and it is the choice that keeps everything else simple: they are
**addresses, not pages** — half a megabyte less for a heap that has three
gigabytes — whereas moving the heap ceiling when the first thread is born would
mean lowering it below memory the heap may **already** have taken. Either you
refuse the thread, or you put its stack on top of somebody else's things: the
first is a limit that shows up at random, the second is silently corrupted
memory.

Before, it had none, and the only limit was physical RAM. That sounds
harmless — memory runs out first — but above the heap there is no void, and
`paging_map_page()` **overwrites an existing PTE without saying anything**.

> ! A large enough heap would have remapped **the process's own TLS block**
> onto fresh zeroed pages: the thread pointer at zero, and every `__thread`
> variable reading someone else's memory. Without a fault, without a log.
> Now whoever crosses the boundary gets `ENOMEM`, which is an error
> `malloc()` already knows how to handle.

The check comes **before** allocating, not inside the loop: stopping halfway
would leave the heap advanced by an amount the caller never saw. And `mmap`
honours the same boundary, `MAP_FIXED` included — the TLS block and the stack
reservation are not things a process may have replaced, because the kernel
holds invariants over them.

### The kernel band, and the remapping window

A process's page directory copies from the kernel PD only the PDEs **below
`USER_SPACE_BASE` (64 MB)**. That band is the only memory the kernel can read
back at its own physical address while a process is running, and it is where
the PMM is obliged to put whatever the kernel addresses that way: the
`kmalloc` heap, the processes' kernel stacks, page directories and page
tables, driver images being relocated (`pmm_alloc_page_kernel()`).

Process pages, on the other hand, live **anywhere in RAM**: the kernel
touches them through one virtual page that is re-pointed on the fly at the
physical page it needs — `paging_finestra_apri()`, the `kmap_atomic` of the
big kernels boiled down to the bone. Without it, a process could only grow as
long as the PMM handed out pages below the threshold: **4 MB, then a kernel
panic**, with any amount of RAM installed. Now a process can grow to fill the
available RAM (measured: 300 MB on a 512 MB machine).

---

## Syscall interface (int 0x80, Linux style)

| EAX | Syscall      | EBX        | ECX       | EDX     |
|-----|--------------|------------|-----------|---------|
|   1 | exit         | status     | —         | —       |
|   3 | read         | fd         | buf*      | count   |
|   4 | write        | fd         | buf*      | count   |
|   5 | open         | path*      | flags     | mode    |
|   6 | close        | fd         | —         | —       |
|  11 | exec         | path*      | argv**    | envp**  |
|  41 | dup          | fd         | —         | —       |
|  63 | dup2         | old        | new       | —       |
|  55 | fcntl        | fd         | cmd       | arg     |
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

`getenv` reads the configuration in `/boot/kernel.cfg`: both the `[env]`
variables and the scalar options outside `[env]` such as `verboseboot`. It is
how a user process gets at the configuration without re-reading the file.

`version` copies `g_os_version`, the kernel global holding the system's name,
copyright, license and version (`kernel/version.c`). The shell exposes it
through the `ver` and `version` commands.

libc wrappers: `getconf()`, `osversion()`, `verboseboot()`.

`reboot` powers off, restarts or halts the system (`cmd` = 0 poweroff,
1 restart, 2 halt). It always syncs the filesystem and stops the scheduler
before acting — see below.


### Syscalls added in 0.184

| EAX | Syscall       | EBX          | ECX      | EDX | What it is for |
|-----|---------------|--------------|----------|-----|----------------|
| 246 | video_info    | `VideoInfo*` | —        | —   | where the framebuffer is and what shape it has |
| 247 | log           | `const char*`| length   | —   | one line on the kernel log, i.e. on the **serial port** |
| 248 | lib_apri      | `const char*`| —        | —   | map a shared library, return its export table (**retired in 0.351**: ENOSYS) |

`log` is not a duplicate of `printf`, and the difference matters: `printf`
writes to the process's console, and if that console is not the one on screen
nobody reads the message — which is exactly the case it exists for, a graphical
server running on a console of its own. A blind tool costs more than the defect
it is meant to find.

`lib_apri` returns an **address**, not a handle, and it is fine that it is
positive: the library band is 0x04000000-0x08000000, so the value can never be
confused with a negative errno. What the caller wants is precisely the address
to read the names from.

### Syscalls added between 0.185 and 0.202

| EAX | Syscall     | EBX           | ECX        | EDX | What it is for |
|-----|-------------|---------------|------------|-----|----------------|
| 249 | fb_map      | `void**`      | —          | —   | maps the framebuffer: the **narrow** capability that replaced `mmio_map` for the window server |
| 250 | interrompi  | pid           | signal     | —   | Ctrl+C that bites: the signal reaches the foreground group |
| 251 | pty_apri    | `int fd[2]`   | —          | —   | a master/slave pair |
| 252 | pty_ctl     | fd            | command    | arg | window size, raw mode, foreground group (on the console: who Ctrl+C stops) |
| 253 | statperm    | `const char*` | `StatPerm*`| —   | mode, uid and gid of a path **without opening it** |
| 254 | su          | `const char*` | password   | —   | «become root IF you know the password», and the kernel decides |

! **TWO NARROW CAPABILITIES, AND THE SAME PRINCIPLE.** `fb_map` does one thing
— map the framebuffer — where `mmio_map` did «map any physical address», which
a window server does not need and a hostile program needs very much. `su` does
one thing — become root knowing the password — where the setuid bit on files
would make every executable carrying it dangerous. A permission that does
exactly what is needed can be reasoned about; one that does more can only be
hoped not to be used.

### Syscalls added between 0.203 and 0.208

| EAX | Syscall          | What it is for |
|-----|------------------|----------------|
| 233 | console_grafica  | who holds the graphics console, and which one it is — so Alt+Fn does not land on a black screen with no way back, and a killed server frees the console by itself |
| 238 | lib_trova        | «do I already have this library inside me?» — ring 3 cannot answer that, and the process page table *is* the list |

! **BOTH ARE STATE THAT LIVES IN THE KERNEL BECAUSE IT MUST OUTLIVE ITS USER.** A
flag held by the window server would die with it and leave the door open onto an
empty room; a list of libraries held by one stub would not be seen by the other
stub in the same process — which is exactly the defect 238 was born from, two
JavaScript engines running side by side without seeing each other.

### The thread syscalls, from 0.208 to 0.209

| EAX | Syscall           | EBX        | ECX      | EDX | What it is for |
|-----|-------------------|------------|----------|-----|----------------|
| 201 | thread_crea       | entry      | argument | —   | a second flow inside the same program: returns the tid, which **is** a pid |
| 202 | thread_esci       | code       | —        | —   | leaves the thread, not the process; does not return |
| 203 | thread_attendi    | tid        | `int*`   | —   | waits for a thread of its own group and collects its code |
| 204 | attesa_dormi      | address    | value    | ms  | "sleep while that address still holds this value": locks, condition variables and semaphores all stand on it |
| 205 | attesa_sveglia    | address    | how many | —   | wakes those sleeping on that address (0 = all) |
| 206 | thread_ferma      | tid        | —        | —   | **asks** a thread to stop, and shakes it if it is asleep |
| 207 | thread_devo_fermarmi | —       | —        | —   | 1 if somebody asked: it is the thread that chooses where to look |

! **A THREAD IS A TASK THAT SHARES THE PAGE DIRECTORY**, and for the scheduler
there is nothing new: same run queue, same quantum, same `context_switch`. It
also shares the descriptors and the working directory; of its own it has the
stack — a slot in a band reserved below the TLS — and its own TLS block,
`errno` included. Whoever leaves the *process* takes every thread with them.

! **STOPPING A THREAD MEANS ASKING IT, AND IT IS NOT TIMIDITY.** A thread
interrupted wherever it happens to be would leave locks held and structures
half-built, and inside a single process those belong to everybody. The kernel
does the two things that cannot be done from outside: it puts the message in
the PCB and **shakes** the sleeper awake.

! **CONDITION VARIABLES AND SEMAPHORES ARE NOT SYSCALLS.**
`condizione_aspetta`, `semaforo_prendi` and the other six all live in the libc,
built on top of 204 and 205: with no contention they do not even cost a system
call. You always wait inside a `while`, never inside an `if` — waking up says
"look again", not "it is there now".

---

## Halting and powering off

| Shell command | Effect |
|---|---|
| `halt` | unmounts the volumes, stops the system, does **not** power off |
| `poweroff` / `shutdown` | unmounts the volumes, counts 3 seconds, powers the hardware off |
| `reboot` | unmounts the volumes and restarts (reset via the 8042, triple-fault fallback) |

Hardware power-off uses the known ACPI ports of QEMU (`0x604`), Bochs
(`0xB004`) and VirtualBox (`0x4004`). **Under emulation the machine really
does power off; on real hardware most likely not** — that would need an ACPI
parser (FADT/DSDT) or APM through real mode, neither of them implemented yet.
In that case the system stays halted in a safe state with the message "it is
now safe to turn off your computer", like pre-ATX PCs.

---

## System identity and version

`kernel/include/version.h` is the **single source of truth** for the name,
version, author and license. From it come the boot banner, the `ver`/`version`
and `uname` commands, and the `OSNAME`/`OSVER`/`AUTHOR` environment
variables — which the kernel injects into `[env]` and which must **not** be
written in `kernel.cfg`.

```c
#define EXOS_VERSION    "0.101"   /* +0.001 at every kernel change */
```

It is a string and not a number because the kernel does not use floating
point. The increment is manual and deliberate.

! **The «Version» line at the top of these two READMEs comes from there
too**, and is not copied by hand: `make leggimi-versione` rewrites it from
`version.h`, and `make verify` fails if the two disagree. A copied number
goes stale the next day, and a README declaring a wrong version is worse
than one not declaring it at all.

---

## Quiet boot

```ini
[kernel]
verboseboot = 0    # 0 = normal output only (default), 1 = log and banner
```

**The default is `0` since 0.142** (it used to be `1`): a system that boots
shows its name, not its initialisation steps. It applies in every doubtful
case — missing entry, missing file, non-numeric value — and only a non-zero
number makes the system talk.

With `0`: a clean screen, one identity line, a prompt. The messages of STEPS
1-13 are emitted anyway — they are printed before the configuration file can
be read — and the kernel clears them from the screen at STEP 13c.

**Errors and unexpected events always stay visible**, in quiet mode as in
verbose:

- a `LOG_ERROR` is printed whatever the log level, even with `loglevel = 0`;
- every ERROR/WARN is recorded as it is emitted and **shown again after the
  screen is cleared**, under the heading `Avvio silenzioso: N problema/i
  durante l'inizializzazione` — so clearing the boot log does not clear the
  evidence of what went wrong;
- if the problems exceed the log's slots, the reprint says so instead of
  truncating in silence;
- the serial console receives everything anyway, filter included.

```
EX OS 0.101 (Extensible Operating System) - Copyright (C) 2025 Graziano Falcone - GPL 2.0

  Avvio silenzioso: 1 problema/i durante l'inizializzazione
[WARN]  PMM: nessuna mappa E820, uso fallback: 32256 KB

ex-os:/>
```

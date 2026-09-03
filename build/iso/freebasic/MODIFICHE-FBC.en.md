# Changes made to the FreeBASIC sources

*Italian version: [`MODIFICHE-FBC.md`](MODIFICHE-FBC.md)*

This file lists **everything** EX-OS changes inside the FreeBASIC compiler
sources, and why. It is meant for three different readers: whoever ports a
newer FreeBASIC release, whoever needs to understand why a binary built for
`exos` differs from one built for Linux, and whoever wants to honour the GPL
v2 knowing exactly what was touched.

FreeBASIC ships under **GPL v2** (the compiler) and **LGPL v2.1 with a
linking exception** (the runtime). The changes described here fall under the
same licences.

---

## Two layers, two scripts

The port is split in two, and the split is not organisational: the two layers
answer different questions and break at different moments.

| script | what it teaches | where it touches |
|---|---|---|
| `applica.py` | **how to talk to the system** | `src/rtlib/` |
| `bersaglio-exos.py` | **that an `exos` target exists** — FreeBASIC 1.07.3 | `src/compiler/`, `inc/crt/` |
| `bersaglio-exos-110.py` | the same, for FreeBASIC **1.10.1** | idem |

The runtime layer is written in C and rarely changes: from 1.07.3 to 1.10.1
all three of its anchors still hold. The compiler layer is written in
FreeBASIC and changes with every release: of the 26 anchors written for
1.07.3, 14 still hold on 1.10.1.

### Why scripts and not patch files

A `diff` patch carries line numbers and exact context: applied to a tree that
differs even slightly, it either fails outright or — worse — succeeds with an
offset and puts the code two functions further down. These scripts instead
look for an **anchor text** and demand to find it **exactly the expected
number of times**: zero means upstream rewrote those lines, more than one
means there is no way to know which is the right one.

> ! In both cases the script stops and names the anchor. Inside a
> **compiler**, a substitution that lands in the wrong place does not raise
> an error: it produces generated code different from what you believe you
> asked for, and you find out much further downstream.

Each script is **idempotent and reversible**: re-applying does nothing,
`--togli` restores the tree. The full round trip — apply, re-apply, remove,
re-remove — has been verified: the tree comes back **byte-for-byte
identical** to the original.

---

## Layer 1 — the runtime (`applica.py`)

FreeBASIC splits its runtime into a common part and a system layer per
platform. The `unix/` layer assumes `termios`, signals, threads and
`_FILE_OFFSET_BITS=64`; **EX-OS has none of those four**. The `exos/` layer is
therefore modelled on `dos/`, the only one written for a system without those
assumptions.

### Three modified files

| file | change |
|---|---|
| `src/rtlib/fb_config.h` | declares `exos` a known platform and turns off the features that presuppose Unix |
| `src/rtlib/fb.h` | `#include "exos/fb_exos.h"` |
| `src/rtlib/fb_private_thread.h` | no threads: EX-OS has none |

### Nine new files, all ours (`src/rtlib/exos/`)

`fb_exos.h`, `fb_private_console.h`, `drv_intl.c`, `file_hlock.c`,
`hinit.c`, `io_console.c`, `sys_execex.c`, `sys_hshell.c`, `sys_paths.c`.

These are original EX-OS code, not derived from FreeBASIC.

> ! `sys_execex.c` and `sys_hshell.c` used to be **one file**, `sys_exec.c`.
> They were split on 11 August 2026 for a reason invisible when
> cross-building: FreeBASIC's makefile flattens objects into a single
> directory through `VPATH`, and `sys_exec.c` collided with another layer's
> `sys_exec.c`. The first of the two won and the second vanished without a
> message. `applica.py` now guards against name collisions and deletes files
> left over from earlier versions.

---

## Layer 2 — the target inside the compiler

Below are all the changes, grouped by what they achieve. Line numbers change
between releases; the **reasons** do not.

### 2.1 — Declaring that `exos` exists

**`src/compiler/fb.bi`** — two changes.

1. `FB_COMPTARGET_EXOS` joins the target enum.

   > ! **AT THE END, NOT IN THE MIDDLE.** The numeric value of these members
   > ends up inside the bootstrap `.asm` files. Inserting one halfway
   > renumbers everything that follows, and an already-generated bootstrap
   > starts talking about one target while meaning another — with no error
   > anywhere.

   In 1.07.3 the last entry was `FB_COMPTARGET_NETBSD`; in 1.10.1 it is
   `FB_COMPTARGET_JS`.

2. The `#elseif defined(__FB_EXOS__)` block providing `FB_HOST_EXEEXT`,
   `FB_HOST_PATHDIV` and `FB_DEFAULT_TARGET` for when fbc **runs** on EX-OS.
   `__FB_EXOS__` needs no declaration anywhere: `symb-define.bas` derives it
   from the target identifier.

**`src/compiler/fb.bas`** — the `exos` row in the `targetinfo` table: `wchar`
type, default calling conventions, and the flags.

```
FB_TARGETOPT_UNIX                  __FB_UNIX__ for the compiled program
FB_TARGETOPT_CALLEEPOPSHIDDENPTR
FB_TARGETOPT_STACKALIGN16
FB_TARGETOPT_ELF
```

> ! **The leading underscore before the comment is not decoration.** Inside a
> table initialiser the logical line continues, and a bare comment lands
> where fbc expects an expression: it answers *"Expected expression, found
> '''"* naming a line that is not the wrong one.

> ! **In 1.10.1 the neighbouring rows carry `FB_TARGETOPT_RETURNINFLTS`; for
> `exos` it must NOT be set.** The ABI is 32-bit Linux x86, where floats come
> back on the x87 stack and not in integer registers. Copying that flag from
> the 64-bit rows next door would give wrong float return values, with no
> error.

**`src/compiler/fbc.bas`** — the name `exos` among those `-target` accepts,
and among the names recognised in GNU-style triplets.

### 2.2 — How a program for EX-OS is linked

Still in **`src/compiler/fbc.bas`**.

**The emulation handed to `ld`:** `-m elf_i386`. EX-OS is 32-bit x86 and
nothing else, so — unlike Linux — there is no CPU-family `select` to make.

**The link flags:**

```
-static -e _start -Ttext-segment=0x08000000
```

- ! **`-static` is not a preference**: EX-OS has no dynamic loader, so no
  `-dynamic-linker` and no `.so`.
- `-e _start` — the entry point is defined by `crt0.o`. It is also `ld`'s
  default for ELF, but stating it removes a dependency on a default.
- `0x08000000` — the address EX-OS loads programs at, the same one used by
  the linker scripts in `bin/*.ld`. `ld`'s default for `elf_i386` is
  `0x08048000`, which is not it.

**The default libraries:** `c`, `m`, `gcc`, in that order — libfb calls libc,
libc calls libgcc.

> ! **No `pthread`, `dl`, `ncurses`, `tinfo`.** The EX-OS runtime is built
> without threads and without terminfo. Asking for them here would produce a
> link that fails on non-existent libraries, with a message about symbols
> never seen rather than about their absence.

**Where to look for support files:** `<prefix>/lib`, which holds `crt0.o`,
`libc.a`, `libm.a` and `libgcc.a`.

> ! **`gcc` is never asked.** On other platforms fbc queries the GCC driver
> to locate the startup files. Inside EX-OS that route goes through
> `popen()`, and **a child's output does not come back**: the answer would be
> empty and the error would surface at link time as a missing file. The EX-OS
> toolchain tree is complete by construction, so we look there and stop.

**The gold-linker check is skipped.** `fbcIsUsingGoldLinker()` runs `ld
--version` and reads its first line: again `popen()`, again `/bin/sh`, to
answer a question whose answer is already known. One extra process per link,
for nothing.

### 2.3 — Our libc's headers (`inc/crt/`)

Eight dispatchers (`sys/types.bi`, `time.bi`, `stdio.bi`, `stdlib.bi`,
`ctype.bi`, `fcntl.bi`, `wchar.bi`, `errno.bi`) gain an
`#elseif defined(__FB_EXOS__)` branch pointing at `crt/exos/…`.

The real files live in `tools/freebasic-exos/crt-exos/` and are **ours**: they
are not aliased to the Linux ones, because they describe the EX-OS libc. The
script copies them into `inc/crt/exos/` and `inc/crt/sys/exos/`.

`errno.bi` takes two changes rather than one: besides the dispatcher, its
generic table of codes must be excluded, because our numbers do not match
Linux's.

> **In 1.10.1 `stdlib.bi` is no longer touched, and that is not an
> oversight.** Upstream rewrote it to dispatch on `__FB_UNIX__`, which `exos`
> already satisfies (the `targetinfo` row sets `FB_TARGETOPT_UNIX`, and
> `symb-define.bas` derives `__FB_UNIX__` from it). The resulting file,
> `crt/unix/stdlib.bi`, declares exactly what ours declares: `mkstemp` alone.

### 2.4 — The generated code (`emit_x86.bas`)

Five sites, and the most delicate of all because they **do not fail loudly**:
getting them wrong yields assembly that assembles and misbehaves, or that does
not assemble at all.

| site | what it does | `exos` |
|---|---|---|
| constant data section | `.rodata` instead of `.data` | **like Linux** |
| `.size` at the end of procedures | | **like Linux** |
| `.type … , @function` | | **like Linux** |
| ctor/dtor section attributes | `"aw", @progbits` | **like Linux** (2 sites) |
| `.cfi_*` directives (unwind) | | **NO** |

The object format is the same as Linux's — ELF32 i386, same assembler — so the
first four follow Linux without discussion.

#### ! Unwind: the most delicate decision of the port

In 1.07.3 the question did not arise: **that release emits no unwind at
all**, zero occurrences of `.cfi_` in `emit_x86.bas`, and the already
generated `exos` bootstrap contains not one `.cfi_startproc`.

1.10 emits `.cfi_*` when `FB_COMPOPT_UNWINDINFO` is on — and **FreeBASIC's
own makefile compiles the compiler with `-e`**, which turns it on. Worse, in
1.10 that check flows through the same `islinux` variable that governs `.size`
and `.type`. Folding `exos` into `islinux` alone would turn on the unwind
**too**, as a side effect nobody asked for.

Here the two are separated: `islinux` includes `exos`, `hasunwind` stays
Linux-only. The reason is that EX-OS has no machinery that consumes
`.eh_frame` and its `crt0` does not register any: those would be sections
nobody reads, at best.

> ! **There are THREE unwind sites, not two.** One in the prologue, one
> **inside the body of the epilogue**, one at the head of the procedure. The
> second was missed on the first pass and inherited the widened `islinux`.
> The result was not "code with some extra CFI": it was **code that does not
> assemble**.
>
> ```
> u_exos.asm:26: Error: CFI instruction used without previous .cfi_startproc
> ```
>
> The prologue — already off for `exos` — never opened the region, and the
> epilogue closed it anyway. Two halves of one switch turned opposite ways.
> A test that diffs the assembly of both targets and actually runs the
> assembler caught it; **reading the code did not**.

Whoever wants exceptions on EX-OS one day will turn that line on
deliberately, having first built the machinery they need.

---

## From 1.07.3 to 1.10.1

|  | 1.07.3 | 1.10.1 |
|---|---|---|
| entries in the script | 26 | **25** |
| sites touched in the sources | 26 | 26 |
| anchors reused verbatim | — | **14** |
| anchors re-written | — | **11** |

The 14 that hold are **not copied**: `bersaglio-exos-110.py` imports them from
`bersaglio-exos.py`. Fixing one therefore fixes it for both releases — which
matters for the `inc/crt/` ones, since they describe our libc and have nothing
to do with the FreeBASIC version.

The site count lands on 26 for both releases through a coincidence worth
writing down: **one disappeared** (`stdlib.bi`, made unnecessary upstream) and
**one appeared** (the second `hasunwind`, absent from 1.07 because the unwind
itself was absent). One script entry covers two source sites (the ctor/dtor
attributes, identical byte for byte).

### What upstream actually changed

```
enum FB_COMPTARGET     ended at NETBSD, now ends at JS
targetinfo table       has FB_TARGETOPT_RETURNINFLTS added
triplet names          gained dragonfly, solaris, js
ldcline                the LINUX case is no longer followed by `end select`
gold-linker check      has two more conditions (SOLARIS, JS) and indents
                       made of tabs instead of spaces
emit_x86.bas           uses `islinux = (…)` instead of scattered compares,
                       and gained unwind emission
inc/crt/stdlib.bi      dispatches on __FB_UNIX__ instead of __FB_LINUX__
```

### Why two scripts and not one with `if`s

Old and new anchors resemble each other enough to match in the wrong place,
and the day a third release is ported the branches would become three. Two
separate files can be read as a `diff` against each other, which is how you
see what changed between releases.

---

## A defect found in the scripts themselves

While verifying the round trip on 1.10.1, a defect surfaced that was present
**in the 1.07.3 script as well**, and that nobody had seen because nobody had
ever checked removal all the way through.

To decide whether a change had already been made, the scripts looked at the
*searched* text when removing and the *inserted* text when applying. That
looks symmetric but is not: **nearly all of these changes insert around the
searched text rather than replacing it**, so after applying them the searched
text is still in there.

The case that proves it is `#else` versus `#elseif`: the former is a prefix of
the latter. A removal that trusts the searched text convinces itself it is
already done.

> ! The result was not an error but a **half-removed tree**, which still
> compiles and goes wrong elsewhere. On 1.07.3, of 26 changes **12** stayed
> behind.

Both scripts now always look at the *inserted* text, in both directions. The
round trip is verified on both releases.

---

## What has been verified

On **1.10.1**, 12 August 2026:

| test | result |
|---|---|
| apply → re-apply → remove → re-remove | tree **byte-for-byte identical** to the original |
| `make compiler` from the patched sources | fbc built and linked |
| `-target exos` recognised | yes, emits 32-bit x86 |
| `.size` and `.type @function` for `exos` | present (2 and 2 in a test case) |
| `.cfi_*` for `exos` with `-e` | **0** |
| `.cfi_*` for `linux-x86` with `-e` | 14 — **unchanged** |
| `exos` assembly accepted by `as --32` | yes |
| runtime for exos: `src/rtlib/*.c` | 421 compiled, **0 failed** |
| runtime for exos: `src/rtlib/exos/*.c` | 7 compiled, **0 failed** |
| `libfb.a` for exos | 575,562 bytes |
| compiler `.asm` for exos | 145 generated, 145 assembled, 0 failed |
| `fbc` for exos, linked | ELF 32-bit i386, static, **no undefined symbols** |
| size | 1,905,988 bytes, 1,643,500 after `strip` |

### And then INSIDE EX-OS, the same day

Everything above is a cross-build on Linux. The test that counts is a
different one: **1.10.1 built from the inside**, by the installed fbc 1.07.3,
with the native GNU make and GCC. ext2 disk, 512 MB of RAM, QEMU without KVM.

```
make TARGET_OS=exos TARGET_ARCH=x86 DISABLE_MT=1 \
     CFLAGS="-O2 -DDISABLE_FFI" rtlib
make TARGET_OS=exos TARGET_ARCH=x86 DISABLE_MT=1 compiler
```

| step | result |
|---|---|
| `rtlib` | 430 files compiled, **0 failed** — `AR lib/freebasic/exos-x86/libfb.a` |
| `libfb.a` built in here | **576,120 bytes** (cross-built on Linux: 575,562 — a different `ar`) |
| `compiler` | **145** `.bas` compiled by fbc 1.07.3, 0 failed, `LINK bin/fbc` |
| `fbc` 1.10.1 built in here | **1,909,644 bytes** |

And the new compiler works:

```
ex-os:/src/fb> /src/fb/bin/fbc -version
FreeBASIC Compiler - Version 1.10.1 (2026-08-12), built for exos-x86 (32bit)

ex-os:/src> /src/fb/bin/fbc prova.bas -x prova110
ex-os:/src> /src/prova110
prova-fb — compilato dentro EX-OS
  saluto      EX-OS e FreeBASIC
  quadrati     385
  esito       tutto a posto
```

385 is the sum of the squares from 1 to 10: a value you can check by hand.

> ! **THE NEW COMPILER NEEDS A COMPLETE PREFIX.** `fbc` derives its prefix
> from the path it was launched from: `/src/fb/bin/fbc` gives `/src/fb/`, and
> in there it looks for `lib/crt0.o`, `lib/libc.a`, `lib/libm.a` and
> `lib/libgcc.a` — because that is what change 2.2 in this document makes it
> do. The source tree does not contain those files: copy them from
> `/cdrom/exos/lib/`. Without them the link fails on `crt0.o`, and the error
> talks about a missing file rather than about an incomplete prefix.
>
> ```
> cp /cdrom/exos/lib/crt0.o   /src/fb/lib/
> cp /cdrom/exos/lib/libc.a   /src/fb/lib/
> cp /cdrom/exos/lib/libm.a   /src/fb/lib/
> cp /cdrom/exos/lib/libgcc.a /src/fb/lib/
> ```

Two linker warnings show up and neither is ours: `fbextra.x contains output
sections` (the script that discards `.fbctinf`) and `cpudetect.o: missing
.note.GNU-stack section` — the latter is exactly why `prepara-fb.sh` passes
`-z noexecstack` by hand when cross-linking.

> **The fixed point IS CLOSED** (12 August 2026): `gen2` and `gen3` are
> byte-identical — 1,906,112 each, verified with `/bin/cmp` inside EX-OS;
> `gen1` differs at byte 33, which is correct because it was produced by a
> 1.07 code generator. From that moment 1.10.1 is the INSTALLED release.
> The procedure is in [`PORTING-1.10.1.en.txt`](PORTING-1.10.1.en.txt).
>
> ! Superseded note, kept for the record: the three-generation fixed point — that 1.10.1
> rebuilds itself down to an identical binary. Here 1.10.1 was built by
> **1.07.3** and can compile; closing the fixed point means running `compiler`
> again with the freshly built 1.10.1 fbc and comparing. It was closed at three
> generations with 1.07.3, and it can be closed with this one too.

---

## How to apply it

```sh
# the runtime layer (applies to every release)
python3 tools/freebasic-exos/applica.py <fb-tree>

# the compiler target — PICK THE SCRIPT FOR THE RELEASE
python3 tools/freebasic-exos/bersaglio-exos.py     <1.07.3-tree>
python3 tools/freebasic-exos/bersaglio-exos-110.py <1.10.1-tree>

# to undo
python3 tools/freebasic-exos/bersaglio-exos-110.py <1.10.1-tree> --togli
```

`bersaglio-exos-110.py` checks `version.mk` before touching anything: on a
1.07 tree the anchors would match **in part**, and a half-modified tree is
worse than an unmodified one — the first still compiles.

### And inside EX-OS

The already-patched sources land on the tools CD under `/freebasic-nuovo`,
with a `BERSAGLIO-EXOS.txt` repeating the commands. They are the same as for
1.07.3 — FreeBASIC's makefile was not modified:

```
make TARGET_OS=exos TARGET_ARCH=x86 DISABLE_MT=1 \
     CFLAGS="-O2 -DDISABLE_FFI" rtlib
make TARGET_OS=exos TARGET_ARCH=x86 DISABLE_MT=1 compiler
```

> ! **The variable is `TARGET_OS`, not `TARGET`.** `TARGET` does exist, but
> the makefile reads it as a GNU-style *triplet* and looks it up in a table
> that has no `exos`: the result is `BUILD_PREFIX=exos-` — meaning
> `exos-gcc`, `exos-ar`, which do not exist — and an **empty** `TARGET_OS`.
> With `TARGET_OS` empty the makefile looks for the runtime in `src/rtlib/`
> alone, skips `src/rtlib/exos/`, and the link fails on functions unrelated
> to the file it was compiling.

> ! **`TARGET_ARCH=x86` matters just as much.** Inside EX-OS the makefile
> derives the architecture from `uname -m`, and our answer is not among the
> ones it recognises.

> ! **`DISABLE_MT=1` is not an optimisation.** Without it the makefile also
> builds `libfbmt.a`: another 427 files, nearly three hours, for a runtime
> with threads EX-OS does not have.

> ! **`-DDISABLE_FFI` is needed.** `src/rtlib/thread_call.c` includes
> `<ffi.h>`; there is no libffi on EX-OS, and the define reduces that file to
> a stub — exactly as the makefile does by itself for Xbox and DOS.

Verified on 1.10.1 with `make -n`: the seven `src/rtlib/exos/` sources enter
the build plan, `libfbmt.a` does not.

> ! **Do not replace `/freebasic` (1.07.3) with `/freebasic-nuovo`.** It is
> the tree that closed the three-generation fixed point, and the only
> reference to compare against if the new one behaves oddly.

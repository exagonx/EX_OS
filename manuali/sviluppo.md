# EX-OS — Programming: the libc, shared libraries, the compilers

**🇬🇧 English** · [🇮🇹 Italiano](sviluppo.it.md)

[← README](../README.md) · [All the manuals](../README.md#the-manuals)

**Contents**

- [The libc: from minimal to hosted](#the-libc-from-minimal-to-hosted)
- [Writing a shared library](#writing-a-shared-library)
- [The compilation chain inside EX-OS](#the-compilation-chain-inside-ex-os)
- [Floating-point `printf`](#floating-point-printf)
- [! `time_t` is 64-bit](#-time_t-is-64-bit)

---

## The libc: from minimal to hosted

Up to 0.145 `lib/libc.c` was a utility-program library: `printf`, the
syscalls and little else. Since August 2026 it is the base on which software
written for a POSIX system can be ported — starting with a compiler.

### The allocator, which is the piece that was really missing

`free()` was an **empty** function, with a TODO where the body should be, and
`malloc()` called `sbrk` on every allocation. For the programs in `/bin` it
did not show: they allocate a few times and then exit. For anything working
on a tree structure — a parser, a compiler — it meant growing until the space
ran out without ever having held more than a few KB. And `realloc()` copied
`size` bytes from a block whose size it did not know: growing read **past the
end**.

Now the blocks live in a list in address order, `free()` makes them reusable
and **merges** them with free neighbours. The proof that counts is in
`/bin/libctest`: 2000 `malloc`/`free` in a loop do not grow the heap by a
single byte.

### Buffered stdio, with a different policy from Unix

There were `printf` and `putchar` — and `putchar` made **one syscall per
character**. Now there are `FILE*`s (`fopen`, `fread`, `fwrite`, `fseek`,
`ftell`, `fgets`, `fprintf`, …), and a single formatter serves `printf`,
`fprintf`, `sprintf` and `snprintf`.

Files on disk are buffered at 4 KB. `stdout` and `stderr` are **not**: they
are buffered *within* the single call and flushed at its end. This is not
Unix's line buffering, and it is deliberate — with that, the shell prompt
(`ex-os:/> `, with no newline) would stay in the buffer, and `gfedit` would
show the last line only after the next keystroke. Unix gets away with it
because reading from stdin flushes stdout; here `gfedit` does not read from
stdin, it talks over IPC with the `kbd` service, so that convention would not
save it. Flushing at the end of the call removes the problem and keeps almost
all the gain: one `printf` costs one syscall instead of eighty.

`exit()` flushes every stream: a program that writes a file and then exits
without `fclose()` finds the file written, not truncated.

### The rest

`setjmp`/`longjmp` (in assembly: they are exactly the registers the compiler
is allowed to reshuffle), `errno` with `strerror`/`perror`, `strtol`,
`strtoul`, `qsort`, `bsearch`, `strstr`, `strdup`, `strtok`, `memchr`,
`ctype`, and the `lseek`/`stat`/`sbrk` wrappers.

**`errno` is an addition, not a replacement.** The functions still return the
negative error (`-2` = ENOENT, `-30` = EROFS): `< 0` remains the right test
under both conventions, and `-EIO` says more than `-1`. Rewriting every
caller to gain nothing made no sense.

The `stat` and `lseek(SEEK_END)` syscalls **used to answer `ENOSYS`** — never
implemented, with a TODO since Phase 3. Without them no `FILE*` can offer
`ftell()` at the end, which is how every program measures a file before
reading it. Now they go through the VFS and work on every mounted
filesystem.

### Floating point, and the FPU the kernel had to switch on

`strtod`, `strtof`, `strtold`, `ldexp`, `strtoll`, `strtoull`. It is not a
luxury: a compiler has to read the numeric literals of the sources it
compiles — `float x = 1.5;` goes through `strtod` — and a `strtod` returning
zero does not give an error, it gives a program compiled with the wrong
constant.

Hence the kernel's **STEP 7b**: the x87 FPU is initialised and its state
(108 bytes) saved into the PCB at every context switch. Without it, two
processes doing floating-point arithmetic overwrite each other's registers.

! **`x == 0.025` can be false even when `x` is right.** On x87 GCC evaluates
constants with a 64-bit mantissa, while a `double` has 53: the comparison
happens between two numbers that are different by construction. The expected
value must be put in a `double` variable, which forces the rounding.

### The libm: openlibm, and why a third-party one

Until August 2026 `<math.h>` declared three functions and said there was no
libm, with this justification:

> «a `sqrt` that is almost right is worse than no `sqrt` at all: it is wrong
> in silence».

**The reasoning has not changed — the consequence has.** The coherent answer
to "I do not know how to write `sin` with the right error" was not to write a
mediocre one: it was to **port a real one**. Behind the names is
**openlibm 0.8.7**, that is, FreeBSD's `msun` in standalone form (MIT/BSD),
with thirty years of rounding fixes and an `i387` directory that uses the x87
instructions where they pay off. It is built with
`tools/openlibm-exos/prepara-libm.sh`; the sources are not in the repository,
as with GCC and binutils.

```
ex-os:/> /cdrom/bin/provamat
openlibm dentro EX-OS

sin(pi/2)=1000 cos(0)=1000 pow(2,10)=1024000 log(e)=1000 sqrt(16)=4000
atan2(1,1)=785 hypot(3,4)=5000 isnan(0/0.)=1
sinf(pi/2)=1000  sinl(pi/2)=1000  exp2(10)=1024000
```

The values are multiplied by a thousand and printed as integers — at the time
EX-OS's `printf` did not format `double`s, and showing them in floating point
would have tested printf instead of the libm. So `atan2(1,1)` = 785 = 0.785 =
π/4.

The 184 prototypes in `lib/include/math.h` are **derived from the symbols
actually defined in `libm.a`**, not copied from a standard: if a function is
declared there, it exists.

> ! **Whoever uses these functions must link `-lm`.** The exceptions are
> `sqrt`, `fabs`, `ldexp` and `frexp`, which live in the libc. `sqrt` is the
> x87's `fsqrt`, that is, **one of the five operations IEEE 754 requires to
> be correctly rounded**: there is no approximation to judge, there is an
> instruction to call. `libm.a` defines it too, but the libc one always wins
> — `libc.o` enters the link anyway (printf, crt0) and by then the symbol is
> already resolved. No "multiple definition", and `sqrt` is used without
> `-lm`.

The one that really asks for it is **libstdc++**: its `<cmath>` writes `using
::sin;` for about a hundred and eighty names, and those names must exist or
the library does not compile.

### Aligned allocation

`memalign`, `aligned_alloc`, `posix_memalign`. Since C++17 a type with an
alignment above the natural one no longer goes through `operator
new(size_t)` but through the aligned variant, which in libstdc++ is a wrapper
around `memalign()` — and if `memalign` is missing, the library supplies one
that **ignores the requested alignment**.

The alignment is obtained by cutting: a large enough block is asked of
`malloc`, then it is **split in two** by putting a real header right before
the aligned address, and the head stays as a free block instead of being
wasted.

> ! **The returned pointer is released with `free()`**, not with a special
> free: to the heap it is a block like any other, and merging with the
> neighbours works without knowing any of this.

> ! `posix_memalign` **returns** the error code and does not put it in
> `errno`. It is the odd one out in the family, and it is the classic way to
> use it wrong.

### Formatted input, date and time, stat

`sscanf`/`vsscanf` with widths, `%n`, suppression and `%lf`. `time`,
`localtime`, `gmtime`, `mktime`, `gettimeofday` on top of `SYS_TIME`, that is
the CMOS clock in local time, and `SYS_FUSO` (270), the kernel's zone: `time`
gives UTC, `localtime` and `mktime` use the zone with European summer time
(kernel 0.340). `stat`/`fstat` in the
POSIX form.

### Processes: spawn with environment and redirections

`spawn()` starts a program and returns the PID; `waitpid()` collects its
result. `spawn` is still the cheap way to start another program; since
kernel 0.313 there is also a real `fork()`, with copy-on-write (next
section).

```c
SpawnRedir r = { 1, O_WRONLY | O_CREAT | O_TRUNC, "/uscita.txt" };
int pid = spawn_ex("/bin/hello", argv, environ, &r, 1);
waitpid(pid, &stato, 0);
```

Redirection is **by path**, not by a descriptor the parent already has open:
the child opens its own file. Passing an fd would mean two processes on the
same VFS handle, that is, a reference count that does not exist and a
`close()` that pulls the file out from under the other one. It is enough for
a compiler driver; it is not enough for pipes, which indeed are not there
yet.

The environment is inherited by copy (`environ`, `putenv`, `setenv`,
`unsetenv`). `getenv()` falls back to the `[env]` section of `kernel.cfg` for
keys it does not find: without that fallback the first process — which has no
parent — would be left without `PATH`.

### Processes: `fork()` with copy-on-write (kernel 0.313)

Redis (BGSAVE), Apache's prefork MPM and Qt's `QProcess` want a real
`fork()`, so EX-OS has one (`SYS_FORK` 263, `fork`, `vfork`,
`pthread_atfork`):

- **Copy-on-write, 32 and 64 bit.** The child gets the parent's page tables;
  every private writable page becomes read-only in both, marked `PG_COW`
  (PTE bit 10), and the physical page gets one more owner in the PMM. The
  first write copies it (or, with one owner left, just makes it writable
  again). `CR0.WP` is now on, so a write by the kernel itself — `read()` into
  a buffer of the child — copies too. `MAP_SHARED` pages and shared memory
  are marked `PG_CONDIVISA` (bit 11) and stay shared.
- **No ceiling of owners.** One byte per page counts up to 256 owners; past
  that a hash table that grows holds the rest. The pages above 4 GB have
  their own counters.
- **Descriptors** are copied with references (files, pipes, pty), as Unix
  does; the **sockets** become shared in ip.drv (0.021): the parent makes the
  child one more owner of each before `fork()` returns, a close by one owner
  only lets that one go, several processes can wait in `accept()` on the
  same listener, and an accepted connection is its acceptor's.
- **POSIX threads**: only the calling thread lives on in the child; libc's
  locks are taken around the call and freed in the child; the `atfork`
  handlers run in POSIX order.
- **Children**: `getppid`, `waitpid` on any child (`-1`, `0`, `WNOHANG`),
  `SIGCHLD` to a handler.
- **Pipes remember every waiter** (kernel 0.313): until 0.312 a pipe woke
  only the last process that blocked on it; with fork, many processes read
  the same pipe.

- **Kernel stacks from all of RAM** (kernel 0.315, 64 bit): every process
  has a 128 KB kernel stack; up to 0.314 those were 32 contiguous pages in
  the first 64 MB, and the laptop stopped at 291 children. Now they live in
  a kernel window (the third PML4 entry) shared by every address space,
  page by page from all of RAM, with an unmapped page under each one: an
  overflow is a fault, not a corrupted neighbour. `/proc/pile` (0.314) shows
  how much each process uses and the deepest seen: 9272 bytes on the laptop
  under load.
- **All of RAM for the kernel** (kernel 0.316, 64 bit): the fourth PML4
  entry maps all of RAM, shared by every address space, so page tables,
  kmalloc's heap and the process table come from all of memory instead of
  the first 64 MB. The ceiling of processes is the RAM: 9000 children at once
  in QEMU with 4 GB, 5000 on the laptop (2.8 GB); up to 0.315, 1222 with
  1 GB and 3045 with 4 GB.
- **One socket in several epoll sets** (ip.drv 0.023): after fork each
  process may put the same connection in its own epoll set, as on Linux;
  `EEXIST` only for the same set twice.
- **Many readers on one UDP socket** (ip.drv 0.023): a parent thread and a
  forked child both in `recvfrom` get one datagram each; up to 0.022 the
  second reader replaced the first, which waited for ever. The table of UDP
  ports grows with the requests (16 at first, then doubling).
- **Many waiters on one pty** (kernel 0.317): every reader and writer of
  each side is remembered (the same lists as the pipes); up to 0.316 the
  second replaced the first.
- **`alarm` rings, and a caught signal ends a wait** (kernel 0.317):
  `alarm`, `setitimer`/`getitimer` (`ITIMER_REAL`; the other two say
  `EINVAL`) send SIGALRM at 10 ms resolution, and `pause` waits for a
  signal; up to 0.316 `alarm` did nothing. A caught signal cuts short
  `read`, `write`, `waitpid`, `poll`, `sleep`, `nanosleep` (with the time
  left) and `F_SETLKW` with `EINTR`; with `SA_RESTART` (`signal()` sets it,
  as on Linux) the handler runs and `read`/`write`/`waitpid`/`fcntl` start
  again by themselves; `poll` and `sleep` always say `EINTR`, as POSIX
  wants. Up to 0.316 only `kill -9` and Ctrl+C ended a wait.
- **A closed port refuses** (ip.drv 0.022): a SYN where nobody listens gets
  an RST, and `connect` says `ECONNREFUSED` at once instead of waiting.
- **A pipe with `O_NONBLOCK` does not wait** (kernel 0.317): an empty pipe
  that still has writers, or a full one, gives `EAGAIN`. Up to 0.316
  `fcntl(F_SETFL, O_NONBLOCK)` (and `pipe2`) kept the flag and `read`/`write`
  ignored it.
- **Apache as a Unix daemon**: APR's `apr_proc_detach` forks for real (the
  EX-OS patch used to skip it), so `httpd` without `-X` goes into the
  background, the worker MPM forks its children (4 processes, the `fcntl`
  accept mutex, the scoreboard in `MAP_ANON` memory) and `httpd -k stop`
  ends them all. Test: `DEMONE=1 tools/prova_httpd.sh`.
- **`openpty`, `forkpty`, `login_tty`** (`<pty.h>`, and BSD's `<util.h>`,
  `<libutil.h>`): on top of `pty_apri`, which gives both ends at once.
  `forkpty` makes the slave the child's standard input, output and error
  and tells the pty that Ctrl+C on the master interrupts the child
  (`PTY_CTL_FG`: EX-OS has no process groups).
- **Pseudo-terminals with names** (kernel 0.318): `open("/dev/ptmx")` makes
  a new pair and gives its master; the slave of pair N is `/dev/pts/N`,
  locked until `unlockpt`, as on Linux. Until the slave is first opened the
  master waits instead of reading the end of the data. `posix_openpt`,
  `grantpt`, `unlockpt`, `ptsname`, `ptsname_r` and `getpt` are in
  `<stdlib.h>`; `ttyname` says `/dev/pts/N` for a slave and `/dev/ptmx` for
  a master (`/dev/tty` stays the console's name); `openpty` gives the
  slave's name. `stat` and `fstat` say "character device" (`S_ISCHR`) for
  `/dev/null`, `/dev/ptmx`, `/dev/pts/N` and for a pty's descriptors; the
  name of a pair goes away with its master. The names exist for `open` and
  `stat`, not for `ls /dev/pts`, and `struct stat` has no `st_rdev`: the
  device's identity is in `st_ino`.

Test: `tools/prova-fork/prova-fork.sh` (64 bit) and `... 32`, in QEMU;
`--figli=N` sets how many children live at once (300 by default).
Checks 17 to 19 cover UDP, the pty and the alarm; 20 `openpty`/`forkpty`,
21 the pipes with `O_NONBLOCK`, 22 the pty names (`posix_openpt`, the lock,
`/dev/pts/N` both ways, `ttyname`, `stat`).

**Fault: a terminal or `script`-like program says "posix_openpt: No such
file" or cannot open `/dev/pts/N`**: the kernel is older than 0.318, where
a pty had no name. Fix: `netupdate`, then reboot.

**Fault: a program waits for ever although it set `alarm()`** (timeout
around a read, a watchdog): the kernel is older than 0.317, where `alarm`
did nothing. Fix: `netupdate`, then rebuild the program against the new
libc (static programs carry the old `alarm`).

**Fault: Apache installed with netupdate does not start** ("AH02291: Cannot
access directory '/exos/apache2/logs/'"). The repository carries files, not
directories, and `logs/` was empty. Fix: `mkdir /exos/apache2/logs`, or
`netupdate`: every empty directory of apache2, mariadb and php now holds a
`.tieni` file that brings it along.

### Valkey 9.1.3: BGSAVE and BGREWRITEAOF, fork under load (voce 33 i)

Valkey is the BSD line of Redis 7.2 (Redis 8 is AGPL/RSAL/SSPL): the same
protocol, the same RDB and AOF files; `redis-cli` and every Redis client
work with it. `tools/valkey-exos/costruisci.sh [32|64]` builds it into
`/exos/valkey`: `bin/valkey-server`, `valkey-cli`, `valkey-benchmark`, and
`valkey-check-rdb`, `valkey-check-aof`, `valkey-sentinel` as **copies** of
the server (Valkey picks its role from the name it was started with, and
EX-OS has no symbolic links); `etc/valkey.conf` (IPv4 bind, data and pid
file in `/exos/valkey/dati`). It is built with the libc's malloc (no
jemalloc), the `select` event loop, without TLS.

    /exos/valkey/bin/valkey-server /exos/valkey/etc/valkey.conf &
    /exos/valkey/bin/valkey-cli ping

`tools/valkey-exos/prova.sh [32|64]` in QEMU: PING, SET/GET, 200000 keys,
BGSAVE while `valkey-benchmark` loads the parent (about 1200 SET and GET a
second), `valkey-check-rdb` on the snapshot, a restart that loads the keys
back, BGREWRITEAOF. 12 out of 12 at 32 and 64 bit.

What the port needed from EX-OS:
- **the libc**: `pthread_cancel` (deferred; cancellation points are
  `pthread_testcancel`, `pthread_cond_wait`/`timedwait`, `pthread_join`),
  `glob`/`fnmatch` (`<glob.h>`, `<fnmatch.h>`), POSIX regular expressions
  (`<regex.h>`: `regcomp`/`regexec`/`regerror`/`regfree`, basic and
  extended), `strndup`, `asprintf`/`vasprintf`, `bzero`, `ctime_r`/
  `asctime_r`, `IOV_MAX` in `<limits.h>`, `BYTE_ORDER` from `<sys/types.h>`,
  `ESOCKTNOSUPPORT`; `printf` learns `%jd` and `%td`;
- **the loader**: a library is relocated only when the whole tree is loaded
  (libapr calls `modf` without naming libm, and the symbol arrived after
  it); initial-exec TLS (`R_X86_64_TPOFF64`, i386 `TPOFF`/`TPOFF32`) on the
  program's own variables is resolved;
- **the kernel**: pipes with `O_NONBLOCK` (above).

**Fault: Valkey stops answering after BGSAVE** (the child is a zombie, the
server `blocc.` in `ps`): kernel older than 0.317, where a non-blocking pipe
waited. Fix: `netupdate`.

**Fault: Valkey dies of signal 11 on `INFO`** ("Accessing address" near
the current time in seconds), **and its log dates read "-73188 Jan
1970"**: Valkey was linked with a libc without `%jd` and whose
`gettimeofday` did not fill the time zone. Fix: rebuild it against the
new libc (`tools/valkey-exos/costruisci.sh`); the same goes for any program
printing `intmax_t` with `%jd`.

### Headers with the standard names

`<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<ctype.h>`, `<errno.h>`,
`<setjmp.h>`, `<unistd.h>`, `<stdint.h>`, `<inttypes.h>`, `<math.h>`,
`<time.h>`, `<fcntl.h>`, `<assert.h>`, `<sys/stat.h>`, `<sys/time.h>` exist
and defer to `libc.h` (`<stdint.h>` does not: the types are declared by the
compiler, see `tools/gcc-exos/leggimi.md`). They are thin façades on purpose:
two listings of the same function drift apart, and the drift shows up as a
wrong prototype — arguments passed crooked, not a compile error.

### The cost, and how it was absorbed

Every program in `/bin` compiles the libc into itself. With the new stdio the
binaries had doubled (`ls`: 12 → 25 KB). `-ffunction-sections`
`-fdata-sections` plus `--gc-sections` at link time throw away what nobody
calls: `ls` is back to **10 KB**, less than before the library grew, and the
floppy has more free space than it had at the start.

```
libctest       276 checks: allocator (including aligned allocation and
                growing the heap until refusal), formatting, streams,
                non-local jumps, conversions, errno, floating point,
                sscanf, date and time, stat, environment, directories,
                temporaries, duplicated descriptors, __thread variables,
                interfaces for third-party code, spawn with
                redirection — all inside EX-OS
```

### Pipes

`pipe()` gives two descriptors joined by a 4 KB circular buffer in the
kernel. The rules that matter are all about boundaries:

> ! **Empty with a live writer = wait; empty with no writers = 0.** That is
> the whole reason the writer count exists: without it, a pipe is
> indistinguishable from an eternal block. And writing when there is no
> reader left gives `EPIPE`, not a wait.

> ! **No `SIGPIPE`**: a pipe sends no signals (those of 0.227 come only from
> faults and `kill`), so only the return value is
> visible. Whoever does not look at `write()`'s does not notice. And **no
> atomicity guarantee**: a write can be partial even below `PIPE_BUF`.

`FD_PIPE_R` and `FD_PIPE_W` are two descriptor **types**, not one with a
flag: the direction is not a detail, it is what decides whether a `read`
blocks or is an error.

Connecting two processes means handing one end to the child, and that is done
with `SpawnRedir` with a NULL `percorso` — **descriptor inheritance**, which
is the only addition that makes pipes useful outside a single process:

```c
int p[2]; pipe(p);
SpawnRedir a = { 1, 0, NULL, p[1] };   /* the child's stdout = write end */
spawn_ex("/bin/cmd", argv, environ, &a, 1);
close(p[1]);                            /* ! indispensable */
```

> ! **The parent must close the end it handed over.** If it does not, the
> pipe still counts one live writer — itself — and the reader will wait
> forever. It is the classic mistake with pipes, and here nothing flags it.

**A design flaw that pipes brought to light.** Descriptors were closed in
`proc_reap_zombie()`, that is, when the parent calls `waitpid()`. With files
alone it went unnoticed; with a pipe it is a deadlock: the child exits, its
fds stay counted, the parent is blocked in `read` and will never reach the
`waitpid`. Two processes waiting for each other, no error. Now they are
closed when the process dies — which is why on Unix they live in `do_exit()`
and not in `wait()`: a zombie must not hold on to I/O resources, only to its
own exit code.

### The rename that does not move the data

`rename()` was **copy+delete**: it cost as much as the file and
**reallocated the blocks**. It carried the name of something else, and the
difference was not academic — it is what made it impossible for `install` to
verify the kernel's sector map before giving it its final name.

Since 0.161 it is a real syscall (`vfs_rename`, implemented on all three
drivers: `fat.c`, `ext2.c`, `fat12.c`) that rewrites the directory entry and
nothing else. **The blocks do not move**: it is the guarantee the installer
rests on. There is a command too:

```
ex-os:/> rename /r1.bin /r2.bin
/r1.bin -> /r2.bin
```

> ! **It is called `rename` and not `mv`, on purpose.** `mv` on Unix renames
> *and moves*, and when it moves across filesystems it copies and deletes — a
> completely different operation under the same name. Here the data never
> moves, and the name says what it does.

> ! **Two differences from POSIX, declared:** same **directory** only
> (ENOSYS), and it **does not replace** the destination (EEXIST). Crossing
> directories would be a copy plus a delete; replacing means deleting a file
> the caller never named as the victim.

In `ext2_rename` **the new entry is added first and the old one removed
after**: in between the file has two names, which is repairable; the other
way round it would have none and the inode would stay allocated and
unreachable.

> ! **`fat12.c` answers in errno, the other two do not.** There `-2` means
> `ENOENT`, in `fat.c` and `ext2.c` it means «already exists». Mixing the two
> conventions makes a rename of a non-existent file say «already exists» —
> and it did, on the very first try with a wrong name.

### `getrusage`, `getpagesize`, `mmap`

The three functions GCC uses as a hosted program, each with its own declared
limit.

> ! **`getrusage` is not a measurement.** EX-OS keeps no per-process
> accounting: the scheduler hands out quanta and does not measure
> consumption. `ru_utime` reports the time **elapsed since boot** — an honest
> upper bound — and everything else is zero. Whoever builds a profile on top
> of it (`gcc -ftime-report`) will get the same time for every pass.

> ! **`mmap` maps anonymous memory only**: with `fd != -1` it gives
> `ENODEV`. Mapping a file would require dirty pages and a moment to write
> them back; an mmap that pretends by handing out zeros would give a program
> that reads wrong data with nothing flagging it. And it returns
> **`MAP_FAILED`, not `NULL`**.

`munmap` **brings the boundary back down** if the unmapped area was at the
top: before, the physical pages went back to the PMM but the address space
did not, and GCC's garbage collector does that cycle thousands of times per
compiled file.

### Duplicated descriptors: `dup`, `dup2`, `fcntl`

Since 0.151 the VFS's open handles have a **reference count**: `close()` only
really closes on the last one. That is what makes `dup()` possible, that is,
keeping a file open past the `close()` of whoever owned the original
descriptor — the move `ar`, `objcopy` and binutils' `arsup` make.

`dup2()` is also the only way to replace `stdin`/`stdout`/`stderr`: `close()`
on 0, 1 and 2 is refused on purpose, because it would leave the process with
no output, whereas whoever comes in through `dup2` already has the
replacement in hand.

> ! **The two descriptors share the file, not the position.** On POSIX a
> `read()` from one moves the other too; here the offset lives in the
> process's descriptor, not in an intermediate "open file" object, and each
> keeps its own. Whoever reads from a duplicated fd should do an explicit
> `lseek()`. It is the first thing to fix the day pipes arrive.

---

## Writing a shared library

Since kernel 0.351 an EX-OS library is an ordinary ELF shared object, as on any Unix: position
independent code, a dynamic symbol table, and `/lib/ld-exos.so` (the libc and the interpreter)
loads it. The kernel keeps a single copy of its code for the whole system.

**Build it** with `-fPIC`, and say what it exports with a version script:

```
gcc -m32 -ffreestanding -fno-builtin -nostdlib -fPIC -O2 -I lib/include -c mialib.c -o mialib.o
ld -m elf_i386 -shared --hash-style=both -z defs -z text -z noexecstack -z separate-code \
   -soname mialib.so --version-script mialib.ver mialib.o \
   /lib/ld-exos.so libexos_avvio.a -o mialib.so
```

```
/* mialib.ver */
{
    global: pippo; pluto;
    local:  *;
};
```

**A program that needs it** names it when it is linked, and the interpreter loads it at start
(`DT_NEEDED`):

```
gcc -m32 -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie \
    -Wl,-Ttext-segment=0x08000000 -Wl,-z,separate-code -Wl,--hash-style=both \
    -Wl,-dynamic-linker,/lib/ld-exos.so \
    start.S programma.c /exwin/lib/mialib.so libexos_avvio.a /lib/ld-exos.so -o programma
```

**A program that can live without it** opens it when it wants it:

```c
void *h = dlopen("/exwin/lib/mialib.so", RTLD_NOW);
void (*pippo)(void) = h ? (void (*)(void))dlsym(h, "pippo") : NULL;
```

(`exlib_apri()` and `exlib_simbolo()` of `exlib.h` do the same, and `exlib_apri_fra()` tries a
list of paths.) The stubs of `excss`, `exsuono` and `exjs` are the model: the same functions as
the library, each one opening it at the first call.

### The rules

! **`-z defs` AND `-z text`.** The first refuses a library with a name nobody defines (the link
says it, not the program at start); the second refuses code that would need writing to at load,
which would be a private copy in every process instead of one copy for all.

! **`ld-exos.so` BEFORE `libexos_avvio.a`** when linking a library: everything comes from the
shared libc, and from the archive only `__stack_chk_fail_local`, the hidden name that
`-fstack-protector` uses in PIC code.

! **YOU ADD, YOU DO NOT REMOVE.** Adding functions and rewriting their bodies is always allowed:
programs already built find them by name. Removing a name from the version script breaks them,
and the interpreter says which name is missing.

! **A library's constructors run before the program's `main`**, in the order of the libraries
(the ones it needs first): a library that needs another names it with `DT_NEEDED` too (`exdlg.so`
names `exwin.so`), it does not open it by hand.

! **`LD_DEBUG=libs` SAYS WHERE IT LOOKS** (since 22/11, like glibc's): every name looked for, every
path tried (`LD_LIBRARY_PATH`, `DT_RUNPATH`, `/lib`, `/exos/lib`, `/exwin/lib` and the same on the
CD) and how it went, on stderr. It holds for programs on `/lib/ld-exos.so`; a program still linked
to the static `libc.a` has its own loader inside, and does not say it.

```sh
export LD_DEBUG=libs
vlc -I dummy /root/Musica/brano.flac
```

---

## The compilation chain inside EX-OS

### The whole chain, in a single command (6 August 2026)

```
ex-os:/> mount hd0p1 /src
ex-os:/> cd /src
ex-os:/src> /cdrom/exos/bin/gcc -O2 -o pg pg.c
ex-os:/src> /src/pg
La catena intera dentro EX-OS

  somma dei quadrati 1..10 : 385   (atteso 385)
  lunghezza del nome       : 5     (atteso 5)
  divisione a 64 bit       : 64    (atteso 64)

Compilato, assemblato e collegato qui dentro.
```

`pg.c` is `tools/iso/prova-gcc.c`: `#include <stdio.h>` and `<string.h>`,
`printf` with `%lld`, `memcpy` from the libc, a 64-bit division that calls
`__divdi3` in libgcc, a struct returned by value. **The values are known in
advance** — 385 is the sum of squares from 1 to 10 — because a program that
prints a number nobody knows the right value of proves nothing.

The driver ran cc1, `as`, `collect2` and `ld` by itself, and found the
headers with **not one `-I`**.

### And the same in C++

```
ex-os:/src> /cdrom/exos/bin/g++ -O2 -o pp pp.cpp
ex-os:/src> /src/pp
La libreria standard del C++ dentro EX-OS

  vector+sort  : 1 3 5 7 9
  string       : "std::string concatenata" (23 caratteri)
  cerchio      : area = 12566 (x1000)
  quadrato     : area = 9000 (x1000)
  eccezione    : lanciata e ripresa
  out_of_range : presa da dentro la libreria

La libreria standard risponde.
```

Containers with `<algorithm>`, `std::string` (that is, `operator new` on top
of our `malloc`), polymorphism with a virtual destructor, and **exceptions**
— including one thrown from inside libstdc++ and caught across several stack
frames, which is the piece that needs the largest number of things working
at once.

! **No `-B`, and until 6 August 2026 one was needed.** The paths GCC has
compiled into it are absolute (`/exos/...`) and do not match with the CD
mounted on `/cdrom`: the driver has to *relocate*, that is, recompute its
own prefix from where it sits. It could not, and `-B` was the crutch.

It can now that the environment actually reaches the child process — it is
`GCC_EXEC_PREFIX` that carries the relocated prefix, and `pex-exos.c` was
handing `spawn_ex` an empty environment. The other four fixes served the
same end: `..` resolving inside paths, `st_ino` telling directories apart,
`-1` instead of `-errno`, and arguments no longer being truncated. **The
`-B` disappeared as a consequence, not as a goal.**

### The `/exos` tree, and why it looks like this

It is not a matter of taste: it is what the binaries look for at runtime,
and it can be read off them (`strings gcc/cc1 | grep /exos`).

```
/exos/bin/                            gcc, g++, cpp, fbc, nasm, ndisasm, make
/exos/libexec/gcc/i386-exos/17.0.0/   cc1, cc1plus, collect2
/exos/lib/gcc/i386-exos/17.0.0/       libgcc.a, crt*.o, include/
/exos/lib/                            libc.a, libm.a, libstdc++.a, libcrypto.a
/exos/lib/freebasic/linux-x86/        libfb.a, fbrt0.o
/exos/include/                        the libc
/exos/include/c++/17.0.0/             libstdc++
/exos/include/freebasic/              FreeBASIC's .bi files
/exos/i386-exos/include/              the libc, where cc1 looks by itself
/exos/i386-exos/bin/                  as, ld
```

! **The libc lives in two places, and that is not waste.** In
`/exos/include` because that is where the prefix puts it; in
`/exos/i386-exos/include` because that one is `TOOL_INCLUDE_DIR` — "another
place the target system's headers might be", `gcc/cppdefault.cc` — and it is
the only one of the two that relocation via `-iprefix` can reach.

! **FreeBASIC wants the same structure, and works it out by itself.** Its
prefix is the executable's directory minus `bin`, recomputed at every run:
from `/exos/bin/fbc` it finds `include/freebasic` and
`lib/freebasic/<target>` wherever the CD is mounted — which is what GCC can
only manage with `GCC_EXEC_PREFIX` or `-B`.

**GNU binutils 2.44 runs natively on EX-OS.** `as` and `ld` are compiled
**for** `i386-exos`, not for the machine that built them:

```
ex-os:/> /cdrom/bin/as --version
GNU assembler (GNU Binutils) 2.44
This assembler was configured for a target of `i386-exos'.

ex-os:/> /cdrom/bin/as -o /prova.o /cdrom/prova.s
ex-os:/> /cdrom/bin/ld -o /prova /prova.o
ex-os:/> /prova
Assemblato e collegato dentro EX-OS.
```

The object produced in here is **byte-for-byte identical** to the one the
cross toolchain produces on Linux. The two tools live on the tools CD
(`make iso`, ~1.4 MB each after stripping); the test source is
`/cdrom/prova.s`.

### Why it is the test that counts

`libctest` calls the functions we **know** we have. binutils calls the ones
it needs, and it has no consideration whatsoever: it asked for them one at a
time, each stopping the build, and the list is the measure of how far a real
"hosted" libc still was —

| | |
|---|---|
| processes | `dup`, `dup2`, `fcntl`, `_exit` |
| files | `realpath`, `lstat`, `freopen`, `mktemp`, `pathconf`, `utime` |
| strings | `strcasecmp`, `strncasecmp`, `strcoll`, `strpbrk` |
| formatting | `fscanf`, `scanf`, `strftime`, `asctime`, `ctime` |
| numbers | `frexp`, `atof`, `fabs` |
| wide characters | `mbstowcs`, `mbrtowc`, `wcstombs` |
| permissions (inert) | `chmod`, `fchmod`, `umask` |
| headers | `<sys/types.h>` `<strings.h>` `<wchar.h>` `<sys/param.h>` `<limits.h>` `<memory.h>` `<utime.h>` |

plus two that were not functions: **`EOF`** — the value had been there from
the start, the *name* was missing, and `safe-ctype.h` checks that it can
work with `#if EOF != -1`, which without the macro sees zero and concludes
the libc is wrong — and **`strerror` returning `const char *`**, safer and
incompatible with the standard signature.

### `pex-exos.c`: starting a program without `fork`

`libiberty` always compiles a `pex-*.c` — «start a program and wait for
it» — and for everything that is neither Windows nor MSDOS it picks
`pex-unix.c`, built on `fork()`. EX-OS has no `fork`, and that is not a gap
to fill: duplicating an address space to throw it away one instruction later
is exactly what `spawn_ex()` avoids.

`tools/binutils-exos/pex-exos.c` is the replacement, modelled on
`pex-msdos.c`: a "descriptor" is an index into a table of **names**, because
the name is what `spawn_ex` needs. The three `NULL`s in the `funcs` table
(pipe, fdopenr, fdopenw) **are the declaration that this system has no
pipes**, and `pex-common.c` notices by itself and switches to the temporary
file mode.

### Three traps, an hour each

- **`-std=gnu17`.** GCC 17 compiles in C23, where an implicit declaration is
  an **error**. binutils 2.44 assumes C17's leniency.
- **`export ac_cv_tls=`** (empty string, not `none`). The test configure runs
  for thread-local variables is a **compilation**: `i386-exos-gcc` accepts
  `_Thread_local` without a murmur, because it is the compiler that knows how
  to emit the `%gs` accesses and the *system* that has no thread pointer. The
  result is an `as` that builds perfectly and dies at the third instruction
  of `bfd_init`. With `none` the macro is not defined at all and binutils
  does not build: it must be **defined and empty**.
- **`sys-include`.** GCC installs its own `<limits.h>` and two versions of it
  exist; the one that does `#include_next` — that is, the one that also picks
  up ours — is generated only if, at the time GCC was built, a system
  `limits.h` was already there, and GCC looks for it in
  `$prefix/i386-exos/sys-include`.

Full recipe in **`tools/binutils-exos/leggimi.md`**.

### GMP, MPFR and MPC

The three libraries `cc1` links against run inside EX-OS too — they are the
heaviest third-party code the system has hosted, 4.6 MB of arbitrary
precision arithmetic archives:

```
ex-os:/> /cdrom/bin/provamp
GMP 6.3.0, MPFR 4.2.1, MPC 1.3.1 — dentro EX-OS

GMP   2^128 = 340282366920938463463374607431768211456
MPFR  pi     = 3.1415926535897932384626433832795028841971693993751e0
MPC   sqrt(i)= (7.0710678118654752440e-1 7.0710678118654752440e-1)
```

Building them is `tools/gcclibs-exos/prepara-gcclibs.sh`. Two non-obvious
things, both documented there:

- **an explicit `CC_FOR_BUILD=gcc`.** GMP picks the compiler for its own
  table generators by compiling one and *running it* — and the test succeeds
  with the cross compiler, because an EX-OS binary is a static ELF32 that
  Linux loads, with syscalls that have Linux's numbers. It starts, it prints,
  it looks fine; `argc` and `argv` do not line up, and the generator refuses
  to generate.
- **`--host=i486-pc-exos`, not i386.** It is not a fallback: 486 is the
  minimum EX-OS already requires on its own account (`invlpg` in
  `kernel/mm/paging.c`). With `i386` you land on a GCC 17 defect that emits
  `rolw $8, %eax` — a 16-bit rotate with a 32-bit register — and the
  assembler refuses it.

### The C++ standard library runs

```
ex-os:/> /cdrom/bin/provacpp
La libreria standard del C++ dentro EX-OS

  vector+sort : 1 3 5 7 9
  string      : "std::string concatenata" (23 caratteri)
  cerchio     : area = 12566 (x1000)
  quadrato    : area = 9000 (x1000)
  eccezione   : lanciata e ripresa
  out_of_range : presa da dentro la libreria
```

**libstdc++ 25 MB and libsupc++ 1 MB** compiled for `i386-exos`. The program
is built with a single line, with `g++`, as on any other target:

```sh
i386-exos-g++ -O2 -o provacpp prova-cpp.cpp
```

> ! **Exceptions work, and that was not a given.** They are the piece that
> needs the most things at once: `__cxa_throw`, stack unwinding, the
> `.eh_frame` tables, the type descriptors. Unwinding **reads the tables
> produced by the linker at run time and walks them**: it is the only part of
> C++ that demands the program loaded in memory be exactly as the linker
> described it — so it is also indirect proof that EX-OS's on-demand loading
> is correct.

**The line that had always been missing: `extern "C"`.** None of the EX-OS
headers had the `#ifdef __cplusplus` guard. C++ decorates names with the
types of the arguments — `printf` becomes `_Z6printfPKcz` — while `libc.a` is
compiled by a C compiler and holds the bare name inside: **every C++ program
was calling symbols that do not exist in the archive.**

The symptom was misleading, because it arrived at compile time and not at
link time: libstdc++ declares some libc functions with an explicit
`extern "C"`, and the message said `conflicting declaration of 'void*
memalign(...)' with 'C' linkage` — that is, «this declaration conflicts with
itself».

### The names that are needed just to be named

For libstdc++ to compile, the libc must *declare* many things EX-OS will
never do: 40 network and IPC errno codes, the `DT_*` constants for FIFOs,
sockets and symbolic links, and the matching `S_IF*`/`S_IS*`.

> ! **A missing name is a compile error, not a dead branch.**
> `<system_error>` builds `std::errc` from that list and `<filesystem>`
> writes `case DT_LNK:` in a switch: it is the constant that is needed, not
> the behaviour. Always returning 0 from `S_ISLNK()` is the **right** answer
> — on EX-OS a symbolic link does not exist, so "is this file a link?" really
> does answer no.

The values are Linux's and must not be reinvented: the day symbolic links
arrive, `S_IFLNK` will have to be `0120000` as it is everywhere else.

### Linking a real C program, inside EX-OS

The tools CD also carries the **target runtime** in `/exos/lib`: `crt0.o`,
`crti/crtn/crtbegin/crtend.o`, `libc.a`, `libgcc.a`, `libm.a`. They are
`i386-exos` objects produced by the cross toolchain, so already EX-OS code:
the native `ld` reads them in here as the cross reads them on Linux.

! **Without them the driver only gets halfway.** `gcc -c` needs only cpp,
cc1 and as; `gcc -o program` also needs crt0, `libgcc.a` and `libc.a` — and
without them you get an `ld` error about symbols that have nothing to do
with the source you were compiling.

The test is `prova-gcc.c`, on the CD together with its cross-generated
assembly (which also serves as the yardstick for what `cc1` will have to
produce):

```
ex-os:/> /cdrom/bin/as -o /pg.o /cdrom/prova-gcc.s
ex-os:/> /cdrom/bin/ld -o /pg /cdrom/exos/lib/crt0.o /pg.o \
             /cdrom/exos/lib/libc.a /cdrom/exos/lib/libgcc.a
ex-os:/> /pg
La catena intera dentro EX-OS

  somma dei quadrati 1..10 : 385   (atteso 385)
  lunghezza del nome       : 5     (atteso 5)
  divisione a 64 bit       : 64   (atteso 64)

Compilato, assemblato e collegato qui dentro.
```

! **The 64-bit division is there on purpose.** It is one of the few things
the compiler cannot do with a single instruction: it calls `__divdi3` in
`libgcc.a`. If libgcc was not linked in, the defect shows there and only
there. The other two values are expected and written in the source — a
program printing a wrong number with nobody knowing what the right one was
is a test that tests nothing.

### `cc1` compiles inside EX-OS

`as` translates, `ld` links, the three arithmetic libraries are there, the
libm is there, **libstdc++ runs** — and **`cc1` compiles**. GCC's C compiler,
built as a canadian cross
(`--build=x86_64-linux --host=i386-exos --target=i386-exos`), reads a C
source inside EX-OS and produces its assembly, which `as` and `ld` turn into
an executable.

**! The binary has to be rebuilt.** The test was done before the move to a
64-bit `time_t`; after that change `cc1` was only **relinked**, and the
already-compiled objects went on believing `struct timeval` was 8 bytes while
the new libc writes 12 — corrupted stack, system stopped. It is the reminder
that an ABI change is not settled with a link. The full rebuild is **done**
— the new binary is there — but it has not been retried inside EX-OS yet:
until it has, the row above says «to be retried» and not «tested».

It is 41 MB of binary, and it runs only thanks to on-demand loading (see *A
program's pages arrive when they are needed*): startup cost does not depend
on the size, and the memory returned to the kernel keeps the rest in check.

Getting there uncovered three defects in the libc, all invisible to EX-OS's
own programs because none of them does what a compiler does:

| | |
|---|---|
| `realloc` **never** grew in place | merging with the following block refused non-free blocks — which is exactly the case to handle. It showed up as a jump to `0xa7a6a5a4`, which is the test's own fill bytes read as a pointer |
| global constructors were never called | `cc1` has 57 entries in `.init_array`; the first structure used was empty |
| `printf` with `%f` invented digits | past the eighteenth, and it rounded 2.5 to 3 instead of to 2 |

### A third-party binary carries the libc of the day it was linked

There are no shared libraries here: `as`, `ld` and `cc1` each hold **a copy
of the libc inside them**, the one they were linked against. Fixing
`lib/libc.c` does not touch them. Put like that it sounds obvious, and it is
not obvious at all when the fixed defect is in the allocator.

`ld` page-faulted as soon as it was given archives to link:

```
[FAULT] PID 9 '/cdrom/bin/ld': page fault a 0x00000005
        (protezione, scrittura, EIP=0x080d2e16)
```

The address resolves on the unstripped binary, and it is not binutils code:

```
EIP 0x080d2e16  ->  malloc + 0x116
```

! **The confirmation is in the symbol table, not in the reasoning.** Inside
`ld` there was `heap_fondi_con_succ` and **not** `heap_assorbi_succ` — the
function the `realloc` fix introduced. That binary is from 2 August: it
carries the libc in which `realloc` **never** grew in place, and the caller
that believed it had more room wrote past the end. The corruption does not
show where it is caused, it shows at the next `malloc`.

Linking a single `.o` got through: little `realloc` traffic. With `libc.a`
and `libgcc.a` to read, bfd grows its symbol tables and it gets there.

#### Relinking alone is not enough, and believing it costs a second defect

The first answer was to relink `as` and `ld` against the corrected libc
without recompiling them: the allocator is *implementation*, the ABI does
not change, so a relink ought to be enough. `ld` stopped faulting and linked
the archives. **And `as` started jumping to a random address**
(`EIP=0x6a722690`, page not present).

The reasoning rested on an unverified premise: *between 2 August and today
only the implementation changed*. It is not true.

```
binutils objects   2 August    typedef long      time_t;
today's libc                   typedef long long time_t;
```

`struct stat` holds **three** `time_t` fields: it grew by twelve bytes and
shifted every offset after them. An object compiled with the old header
reads it the old way while the new libc writes it the new way — and what
comes out, if it ends up in a function pointer, is exactly a jump to
`0x6a722690`.

> ! **It is the same defect as `cc1`'s, not another one.** There I caught it
> at once because the `time_t` change was fresh; here I had forgotten it and
> concluded «a relink is enough» **before** checking. A full rebuild of
> binutils is the only right answer, as it was for `cc1`.

> ! **The relinked `ld` worked anyway**, and that is the instructive part:
> in the path that links archives the wrong `struct stat` is never touched.
> «It worked once» is not proof of correctness — it is proof that that path
> does not go through there.

A general rule, valid for anything that gets ported in here:

| what changed in the libc | what is enough |
|---|---|
| only `lib/libc.c` (implementation) | relink |
| also `lib/include/libc.h` (types, structures) | **recompile everything** |

The way to spot the first case is to look in the symbol table for a function
that only exists after the fix. The way to spot the second is to look at
`git diff` on the header **before** deciding, which is exactly the step that
was skipped here.

**What is still missing:** the `gcc` driver program, the one that chains
`cc1 → as → ld` passing the intermediate files along. Today the three steps
are given by hand.

---

## Floating-point `printf`

`%f`, `%e`, `%g` and their upper-case forms, with width, precision and flags.
They used to consume the argument and print `<float>`.

! **Significant digits stop at 18, and zeros are printed beyond.** It is a
**measured** number, not an estimated one: comparing the engine with glibc
over a dozen values, up to 18 there is not one disagreement, at 19 the first
appears. A `double` carries at most 17 digits of information; what lies
beyond is the exact expansion of the *binary* value, which glibc prints with
arbitrary-precision arithmetic and we do not:

```
printf("%.30f", 0.1)
  glibc  0.100000000000000005551115123126
  EX-OS  0.100000000000000000000000000000
```

The first 17 digits agree — that is all `0.1` contains.

! **Rounding is to even**, as the standard prescribes: `%.0f` of 2.5 gives
`2`, of 3.5 gives `4`. Under the naive rule («five and up goes up»), summing
a column of rounded values accumulates an error that grows with the number of
rows.

**Out of 399 comparisons with glibc, 390 identical**; the remaining 9 appear
only when asking for more than 18 significant digits.

## ! `time_t` is 64-bit

Not only because of 2038 — which is anyway a deadline not to write into
something that starts in 2026. The defect that made it urgent is arithmetic:
GCC measures time with

```c
now->wall = tv.tv_sec * 1000000000 + tv.tv_usec * 1000;
```

and with a 32-bit `tv_sec` that multiplication **overflows before being
widened**. `cc1`'s timing report came out with phases of 18 billion seconds.
It is not GCC code to be fixed: it is correct code on a correct `time_t`.

! And `gettimeofday` now takes **seconds and microseconds from the same
source**. Before, the seconds came from the CMOS clock and the microseconds
from the tick counter: two independent clocks, and the pair could **go
backwards**. A clock that goes backwards does not give an error, it gives
negative intervals to whoever subtracts two instants. The declared price: if
someone corrects the system time while a program is running, `gettimeofday`
does not notice — a clock that never goes backwards is worth more.

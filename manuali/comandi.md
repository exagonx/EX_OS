# EX-OS — The shell and the commands

**🇬🇧 English** · [🇮🇹 Italiano](comandi.it.md)

[← README](../README.md) · [All the manuals](../README.md#the-manuals)

**Contents**

- [/bin/mkdir and /bin/rmdir](#binmkdir-and-binrmdir)
- [/bin/delete](#bindelete)
- [Job control: `&`, `jobs`, `fg`](#job-control--jobs-fg)
- [Virtual consoles — Alt+F1 … Alt+F5](#virtual-consoles--altf1--altf5)
- [Date and time](#date-and-time)
- [/bin/textline — line-oriented text editor](#bintextline--line-oriented-text-editor)
- [/bin/gfedit — full-screen editor](#bingfedit--full-screen-editor)
- [`/boot/autoexec.sh` — commands at startup](#bootautoexecsh--commands-at-startup)
- [`!silenced` — the scripts' `echo off`](#silenced--the-scripts-echo-off)
- [Command history and line editing](#command-history-and-line-editing)
- [Names with spaces: quoting](#names-with-spaces-quoting)
- [`date` — what time it is, and putting it right](#date--what-time-it-is-and-putting-it-right)
- [`ls` — display modes](#ls--display-modes)

---

## /bin/mkdir and /bin/rmdir

```
mkdir <name> [name2 ...]    create one or more directories
rmdir <name> [name2 ...]    remove one or more EMPTY directories
```

They accept absolute paths or paths relative to the current directory.
**Root-level directories only**: the FAT12 driver resolves paths one level
deep, so a nested directory would be correct on the medium but unreachable —
both refuse with an explicit message instead of creating or deleting
something unusable.

`rmdir` refuses non-empty directories, and this is not a temporary
limitation: without recursive deletion the files left inside would become
unreachable and their clusters would stay allocated forever. The root is
protected, and `rmdir` on a file is refused.

---

## /bin/delete

```
delete <pattern> [pattern2 ...]

  ?   any single character
  *   any sequence of characters
```

```
delete note.txt        one specific file
delete *               everything in the current directory
delete /temp/tmp*      files starting with "tmp" inside /temp
delete data?.log       DATA1.LOG, DATA2.LOG, ...
delete *.txt           by extension
```

Wildcard expansion is done by the program, not by the kernel nor by the
shell — as in MS-DOS. The comparison is case-insensitive (FAT12 keeps names
in upper case). Directories are skipped: `rmdir` is there for those.

`delete` works in two passes: first it collects every matching name by
walking the whole directory, then it deletes. You cannot delete while
listing — freed entries are skipped by `readdir` and the following entries
would shift down, losing a file per block.

`delete *` asks for confirmation when more than one file matches, saying how
many and from where. A targeted pattern such as `tmp*` does not ask.

---

## Job control: `&`, `jobs`, `fg`

```
command &     run in the background and return to the prompt at once
jobs          list the jobs still running
fg [n]        bring job n to the foreground (the last one if omitted)
```

A finished job is announced at the next prompt — `[1] terminato: tsleep
(codice 0)` — and that is also when its process slot is freed: a child stays
`ZOMBIE` until the parent collects it (init's reaper only handles orphans).

**There is no `bg`**, and that is not an oversight: `bg` resumes a
*suspended* process, and suspending one would need a Ctrl+Z — that is,
a signal that stops a process and lets it resume, which EX-OS does not have
(signals exist since 0.227, but not SIGSTOP and SIGCONT, and the shell sends
none). A job here is either running or finished;
the state in between does not exist.

**Output gets mixed together.** A background job writes to the same console
as the shell, so its lines land in the middle of the prompt and of what you
are typing. That is the behaviour of any Unix shell; if a program needs the
screen to itself, you start it on another console with Alt+Fn instead of
with `&`.

**Input, on the other hand, is protected**, and it really was needed. The
keyboard driver serves the *last* one who asked for a line: with no defence,
a background job reading `stdin` would replace the shell as the reader and
the prompt would never receive another command — the console would die. Two
mechanisms prevent it:

1. `sys_read` on `stdin` returns **end of input** to anyone who is not the
   foreground process of its own console. The shell declares the foreground
   with `SYS_CONSOLE_SETFG` (itself at the prompt, the child while waiting
   for it). Unix would use `SIGTTIN` here; without that signal, EOF is the only
   possible answer — and it is true anyway, that program will never get any
   input.
2. Whoever takes the keyboard by talking **directly** to the `kbd` service
   over IPC — `gfedit`'s raw mode — does not go through `sys_read`, so it
   checks `ConsoleInfo.fg` itself and refuses to start in the background,
   explaining why.


**Ctrl+C** (since 28 September 2026, kernel 0.221). On the text console, in
line mode, it asks `[Ctrl+C] Fermo "textline" (PID 20)? s/n` and stops the
program only on "s"; at the prompt it empties the line. Who to stop is said
by the shell with `pty_ctl(0, PTY_CTL_FG, pid)` — the child it waits for, 0 at
the prompt — and not by `SYS_CONSOLE_SETFG`, because at the prompt the
foreground is the shell itself. In a window terminal Ctrl+C stops at once,
without asking: there the pty's line discipline in the kernel handles it.

---

## Virtual consoles — Alt+F1 … Alt+F5

**Five** independent screens (`VGA_N_CONSOLE`), only one visible at a time,
each with its own shell — or its own `login` — started at boot. **Alt+F1..F5**
switches: the program that was running is neither suspended nor closed, it
keeps working and drawing into its own buffer, and finds the screen intact
when you come back to it.

It is the answer to "how do I start something else without closing this
one": open `gfedit` on console 2, press Alt+F3, you get a clean prompt, and
Alt+F2 takes you back to the editor exactly where you left it.

! **The graphics live on console 5.** `exwin` restarts the window server up
there and says so at startup (`grafica accesa sulla console 5`): from a text
console you get there with **Alt+F5**, and **Alt+F1** takes you back to the
shell. It is also why a test driving the desktop presses `Alt+F1` before
launching an application and `Alt+F5` right after.

| | |
|---|---|
| Console 0 (Alt+F1) | is also the **system** console: kernel messages (`klog`) come out here, next to the prompt. The others stay clean. |
| Inheritance | a program is born on its parent's console (`sys_spawn`), so it stays where it was started |
| Keyboard | keys go **only** to the foreground console; the shells of the others stay at their prompt with the read request pending |
| Raw mode | is **per console**: while gfedit holds 2 in raw mode, the shell on 1 keeps receiving whole lines with echo and Backspace |

Alt+Fn is intercepted by the keyboard driver **before** any other processing
and is delivered to nobody: it is a command to the interface, not input for
the running program. Without that precedence, an editor using Alt+F for its
File menu would be enough to make switching screens impossible — that is,
exactly in the case where you need it most.

### Backspace and the columns that are not there

The "cooked" line discipline — the one that accumulates characters and
delivers them on Enter — used to erase **one column per character in the
buffer**. That sounds obvious and is not: control characters go into the line
but are not echoed (ESC does, `/bin/textline` uses it to cancel a line), and
the arrow keys go in as the sequence `ESC [ A`, of which two bytes out of
three are printable and none of the three was ever drawn.

> ! The result: two ESC keys pressed by mistake, two Backspaces, and the two
> columns erased were **the last ones of the prompt**. In the serial log you
> could see `^H ^H^H ^H` and textline's asterisk disappearing.

Now every character in the buffer carries a bit with it — *did I draw this
one or not* — and Backspace erases a column only if that column is ours. The
prompt is out of reach by construction, not by one more check. Two
asymmetries of the same family fell in the same pass: on a full line the
character was echoed but not accumulated (you ran less than you read), and
tab was drawn despite being impossible to undo — it advances to the next tab
stop, which depends on where the prompt starts.

**On reaching the limit, the line is cleared entirely.** If nothing visible
is left, whatever survives in the buffer is invisible characters, ready to
end up inside the next command: whoever deletes all the way back expects an
empty line and now finds it truly empty.

This applies to `drivers/kbd/kbd.c` and to the fallback in-kernel TTY
(`drivers/tty/tty.c`). The shell's line editor — the one with the arrows and
the history — was not involved: it works in raw mode and accepts printable
characters only.

Cost: 4 KB of kernel BSS per console (the screen buffer) plus a shell process
of ~14 KB. The number is `VGA_N_CONSOLE` in `kernel/include/vga.h`, and it
must stay equal to `KBD_N_CONSOLE` in `drivers/kbd/kbd_proto.h`.

---

## Date and time

`time_now()` reads the machine's CMOS clock (MC146818, ports 0x70/0x71) and
returns the real date and time — the ones the battery-backed clock keeps
counting while the machine is off. Not to be confused with `uptime_ms()`,
which measures *durations* and does not know what time it is.

It returns `-ENODEV` if the clock does not answer or hands back an impossible
date (which happens on old hardware with a flat CMOS battery): in that case
the caller must say "time unknown" instead of showing an invented one —
gfedit writes `--:--:--`.

**And from kernel 0.218 it can also be WRITTEN**: `time_set()` (`SYS_TIME_SET`,
213) puts the clock right, is root's, and accepts years from 1980 to 2099. The
command that uses it is `date`, with `-set-date:` and `-set-time:`; the reason
behind every choice is under
**[`date`](#date--what-time-it-is-and-putting-it-right)**. Until then the clock
could only be read, and on a machine with the wrong date there was no way to fix
it from inside EX-OS.

! In QEMU the RTC starts in **UTC**, not local time: EX-OS reads it as local time
and the clock is off by the zone. The tests (`tools/qemu_drive.py`) and `make run`
pass `-rtc base=localtime`, as on a real machine (kernel 0.340).

---

## /bin/textline — line-oriented text editor

The edlin model: you work by line number, not with a cursor. It is still the
quickest way to fix a single line, and the only one that works even when
`/dev/kbd.drv` is not available and the console is served by the fallback
in-kernel keyboard.

```
textline <file>              open the file for editing
textline <file> -v           show the contents
textline <file> -vp          show them a page at a time
textline <file> -c:<file2>   copy <file> to <file2>
```

Commands: `h`/`help`, `l`, `lNN`, `lNN,MM`, `lp…` (paged), `m`, `mNN`, `n`,
`dNN`, `cNN,MM`, `w` (save), `e` (save and exit), `q` (quit). ESC cancels the
line being inserted and returns to the prompt.

---

## /bin/gfedit — full-screen editor

A rewrite for EX-OS of **GF_TEXTEDITOR**, the same author's ncurses+pthread
editor (original sources in `gftexteditor/`). It is not a port: of ncurses,
threads, POSIX stdio and a real `free()`, EX-OS has none. What stays the same
is the program — MS-DOS EDIT style drop-down menus, eight areas open at once,
find/replace, undo, syntax highlighting.

```
gfedit                open an empty area
gfedit <file> [...]   open up to 8 files
gfedit -h             list the shortcuts
```

| | |
|---|---|
| Movement | arrows, Home/End, Ctrl+Home/End, PgUp/PgDn, Ctrl+G (go to line) |
| Selection | Shift+movement, Ctrl+A, ESC to drop it |
| Editing | Ins, Ctrl+Z, Ctrl+X/C/V |
| Files | Ctrl+N, Ctrl+O, F2 or Ctrl+S, Ctrl+W, Alt+X |
| Search | Ctrl+F, F3, Shift+F3, Ctrl+H |
| Areas | F6, Shift+F6, Alt+1…Alt+8 |
| Menu | F10 or ESC, or Alt+F M C O A |

The status bar shows the real time of day, which **advances on its own** even
with the keyboard idle: the main loop no longer waits for a key forever but
wakes up every half second (`ipc_recv_timeout`). Between two wake-ups the
process is `BLOCKED` and does not consume a CPU tick.

Highlighted languages: C, C++, BASIC, assembly, recognised by extension and
changeable from *Options → Language*.

**Limits, and why they are there.** 512 lines per file, 200 characters per
line, 8 areas. Lines are fixed-length slots because EX-OS's `free()` is a
declared no-op (bump allocator over `sbrk`): with strings reallocated on
every keystroke, the memory of the previous line would be lost forever. A
file larger than the limits is loaded **in part**, the status bar says so,
and saving over that file stays blocked — so as not to erase the part that
was never read. *Save as* to a different file is allowed instead.

**It needs `/dev/kbd.drv`.** A full-screen editor needs keys one at a time,
and raw mode lives in the keyboard driver. Without that service gfedit does
not start and points you at textline, instead of showing an interface that
would not respond.

---

## `/boot/autoexec.sh` — commands at startup

One line = one command, executed **exactly as if it had been typed**: same
built-ins, same quoting, same `&` for the background. Empty lines and lines
starting with `#` are skipped; a line starting with `@` is executed without
being printed, as in DOS's autoexec.

On the EX-OS CD there is one that brings the network up by itself:

```
autoexec> /dev/pci.drv &
autoexec> netdetect -c
autoexec> /dev/ip.drv &
autoexec> dhcp
```

After booting, `ping` and `ftp` work without touching anything.

! **Only the FIRST console's shell runs it.** EX-OS starts one for each of
the four virtual consoles: without this check the autoexec would run four
times, and for `/dev/pci.drv &` that would mean four processes fighting over
the same service.

! **The way out exists before it is needed.** An autoexec containing a
command that hangs would make the system unusable, and the file to fix it is
on the medium you can no longer reach. So:

| | |
|---|---|
| `autoexec=0` in `kernel.cfg` | skips it (the file is edited from another machine) |
| **Alt+F2, Alt+F3, Alt+F4** | always give a clean shell, even while the first one is busy |

The second is the one that really counts: it does not require being able to
modify anything.

## `!silenced` — the scripts' `echo off`

```
!silenced      from here on the commands are not shown
!verbose       they are shown again
@command       silences ONE line only
```

! **It silences the command, not its output**, and that is the distinction
that makes the option useful: what a command prints is the reason it was put
in the script, whereas the command line itself has already been written. The
CD's autoexec starts with `!silenced` and shows only the address obtained,
not the four commands it took to obtain it.

It applies **from where it sits onwards**, not to the whole file: you can
silence the noisy part and let the interesting one show. The directive line
is never printed.

Scripts are no longer only the autoexec:

```
source /prova.sh      runs in THIS shell
/prova.sh             the same, by name
```

! **`source` and not a spawn**: the commands must run in the current shell,
otherwise a `cd` or an `export` inside the script would vanish along with
the child process. A name ending in `.sh` is recognised *before* trying to
launch it, not after the spawn has failed: a spawn fails for many reasons,
and treating them all as «it must be a script» turns a precise error into a
second error that talks about something else.

## Command history and line editing

The **up** and **down** arrows walk back through the commands already given
(24 of history); **left**, **right**, **Home**, **End**, **Backspace** and
**Delete** edit the current line. `Ctrl+C` abandons it.

! **The line in progress is not lost.** Whoever has typed half a command and
goes looking for an old one with the up arrow finds it again by coming all
the way back down.

Empty lines and consecutive duplicates do not enter the history: whoever
repeats the same command ten times does not want ten entries to walk back
through.

! **The keyboard's raw mode is needed**, because in cooked mode the driver
assembles the line and delivers it on Enter — the arrows have no way of
crossing a stream of text. So the shell takes the line discipline upon
itself: echo, backspace, cursor.

! **If the `kbd` service does not answer, it goes back to reading whole
lines**: you lose the history, not the shell. And the driver returns to
cooked by itself every time a program reads from stdin, so the mode is
reasserted at every prompt — which also makes it self-repairing.

! **In ExWin's Terminal and over telnet too** (sh 0.017): there the input is a
pseudo-terminal; the shell puts it in raw mode while the line is typed and
puts it back as it was before running the command. The arrows come as an xterm
sends them (`ESC [ A` and friends); `Ctrl+A` and `Ctrl+E` work too (start and
end of the line, as in bash), and `Ctrl+D` on an empty line closes the session
as before. There characters are bytes: an accented letter in the middle of the
line moves one byte at a time.

! **Only the foreground console** gets the keys. Without that check all four
shells fought over the keyboard, and the invisible ones put it back into
cooked mode, taking it away from whoever was typing.

! The redraw uses **Backspace only**, because EX-OS's TTY has no cursor
positioning language. Consequence: on a line longer than the screen width the
editing looks wrong — but the line stays correct, and what you read is what
will be executed.


**How long a line can be: up to 32,768 characters** (kernel 0.353, sh
0.014), at the console and over telnet; ssh goes through the same pty, and
scripts and `sh -c` have the same limit, but the test covers the console and
telnet. Before, the console stopped at 511 characters and telnet at 255: a `gcc`
line with many files did not fit. The history keeps every line whole, at
its own length; a line longer than the limit is cut there and the shell
lives on. The kernel and `kbd.drv` start with a small buffer and grow it
only when needed. Test: `tools/prova_riga_lunga.sh 32|64`.

## Names with spaces: quoting

```
ex-os:/> cat "/disco/appunti di riunione.txt"
contenuto di prova
ex-os:/> cp '/disco/appunti di riunione.txt' /disco/copia.txt
copiati 19 byte in /disco/copia.txt
```

! **Single and double quotes do the same thing.** On a Unix shell the
difference exists because inside double quotes `$VAR` is expanded and inside
single quotes it is not. Here there is no expansion at all — neither of
variables nor of wildcards — so the two forms would have nothing to tell
apart. Accepting both and treating them the same is honest; accepting only
one would force you to remember which.

An unclosed quote is **reported**: before, the argument was taken to the end
of the line in silence, and the command failed complaining about a
non-existent file with an absurd name — the defect was in the line, not in
the file. The same goes for arguments past the sixteenth, which used to
disappear without a word.

## `date` — what time it is, and putting it right

```
date                        day, date and time in full
date -d                     the date only, 2026-09-16
date -t                     the time only, 17:52:31
date -set-date:2026/09/16   sets the date   (root only)
date -set-time:17:52:30     sets the time   (root only)
date -ntp                   the time from pool.ntp.org   (root only)
date -fuso:+1,ue            the zone for -ntp: Italy (/boot/fuso.cfg)
```

```
ex-os:/> date
mercoledi' 16 settembre 2026, 16:09:20
ex-os:/> date -set-time:10:11:12
orologio rimesso: mercoledi' 16 settembre 2026, 10:11:12
```

**The clock can be set from kernel 0.218 on, and not before.** Ever since it
existed — August 2026 — the EX-OS CMOS clock could only be **read**: there was
`SYS_TIME` and no twin of it. That was not a theoretical hole: the Acer Aspire
3000 used for testing read **2005**, and every file it uploaded to an FTP
server arrived dated «Feb 9 2005». The only way to fix it was to enter the
BIOS.

Now there is `SYS_TIME_SET` (213), which calls `rtc_write()` in
`kernel/arch/x86/rtc.c`.

! **It is root's, like `mount`.** The system time is not a personal setting:
whoever moves it changes the date of every file anybody else will write, and
with it `netupdate -check`'s judgement about what is newer. From an ordinary
user you go through `sudo date ...`.

! **Writing has one trap more than the three of reading.** You do not write
while the chip is counting: if the update falls in the middle of the six
registers, the chip overwrites half of what you have just put there, and you
get a date half old and half new. The `SET` bit of register B stops the count
for the duration of the write.

! **And you write in the format that is already there** — BCD or binary, 12 or
24 hours, as register B says. Changing it would be easier to program and would
break the BIOS's own reading, which has known that format since power-on.

! **The century register is updated only where it is already there.**
`rtc_read` does not read it at all (it uses the «below 70 = 2000+» convention),
so for EX-OS it would be useless; but a BIOS that does use it, finding it stuck
at 20 while the two digits say 26, would restart from the wrong year. So 0x32
is written **only if what is in it already looks like a century** (19 or 20):
on a machine where that register serves another purpose, the check leaves it
alone.

! **The year is between 1980 and 2099**, while reading accepts up to 2199. The
chip keeps TWO digits, and the read interprets them with that convention:
asking for 2150 would mean reading back 2050 without telling anyone, and a
silent error is worse than a refusal.

! **The two halves are set separately.** Whoever changes only the time —
daylight saving, or a clock that has drifted by two minutes — must not have to
retype the date, and above all must not risk getting it wrong. The fields you
do not touch are read back from the clock, because `time_set()` wants a whole
`RtcTime`: the chip has no way of writing «the minutes only».

! **There is no `time` command, and that is deliberate.** On any Unix system
`time command` MEASURES how long a command takes to run. Spending that name on
printing the clock would mean not having it the day it is really needed — and
then either you break whoever uses it, or you pick a worse name. One binary
says both things, and on the floppy it costs one instead of two: when `date`
was written the floppy had 63488 bytes free and the smallest EX-OS program
weighs 13.5 KB.

! **The time zone** (kernel 0.340). The machine's clock is local time; the
kernel knows the zone of `/boot/fuso.cfg` and the seconds counted from 1970 are
UTC, comparable with an instant on another machine. `date -fuso:` changes it at
once; from a CD it holds until switch-off.

## `ls` — display modes

```
ls -h              list every option
ls                 date and time, <DIR> or <FILE>, size and name
ls -d              the same: it is there for the fingers that know it
ls -s              name and size only, without asking the files
ls -mc /bin        columns: names only, the most compact
ls -l              permissions, owner, group, size and date
ls -md             like the normal mode, plus the attributes
ls -a              also show names beginning with a dot
ls -p              one page at a time (Enter advances, q stops)
```

```
ex-os:/> ls
data        ora    tipo    dimensione  nome
2026-09-16  18:10  <DIR>            -  BOOT
2026-09-16  18:10  <DIR>            -  BIN
2026-09-16  18:10  <FILE>        2631  LOADER.BIN
2026-09-16  18:10  <FILE>      270376  KERNEL.BIN

2 file, 273007 byte    5 directory
```

**Since 16 September 2026 a bare `ls` also says WHEN and WHAT.** It used to
print two things, the name and the size. The date was already there — but only
under `-d`, `-md` or `-l`, that is, only for whoever knew they existed; and the
type had to be inferred from the trailing slash on the name, which is a
convention you have to know. The two commonest questions in front of a list of
files are «how old is it?» and «is it a folder?», and for both the answer sat
behind an option nobody types.

! **And it costs one `stat()` per entry**, which the bare mode did not do
before. On a floppy that is felt, and it is exactly why `-s` exists and is not
a fallback: it is the mode to use when all you want is which names are there,
on a slow disk or in a very large directory.

! **The date is the MODIFICATION one, and there is no creation date.** That is
not a choice: the directory entry the three filesystems hand to the VFS keeps
ONE date/time pair, and `struct stat` indeed sets `st_ctime` and `st_atime`
equal to `st_mtime`, and says so. Showing the same date under two different
headings would be worse than showing one.

! **A directory has no size to show**, and now that the column next to it says
`<DIR>` there is no need to say it twice. The number filesystems keep there is
the size of the directory entry on disk — zero on FAT — not how much the
contents weigh. A dash says «the question does not apply»; a zero would say
something false.

! **Here `-d` means «details»**, not what it means on Unix (where `ls -d`
shows the directory instead of its contents). It is this project's choice,
and the help says so, so that nobody discovers it by trial and error.

! Without `-a`, names beginning with a dot are hidden, `.` and `..`
included. It is a change from before, when they were always shown. On a CD
`.` and `..` do not appear anyway: ISO 9660 does not deliver them (they are
two records named `0x00` and `0x01`, and the driver skips them).

! FAT's «hidden» bit is **shown** with `-md` but hides nothing. Looking at
it would cost one `statraw()` per entry even when only the names are being
printed, and on a floppy that is felt; what hides is the leading dot, which
is the convention of every filesystem EX-OS mounts.

**The dates really do come from the filesystem** (kernel 0.168). Before,
`sys_stat` wrote zero and no program could show them. Now:

| | |
|---|---|
| FAT12 / FAT16 / FAT32 | the format is the native one, no conversion |
| ext2 | from `i_mtime` (Unix time) into the FAT format |
| ISO 9660 | from the seven bytes of the directory record |

! A zero date **means «this volume does not keep them»** and programs print
dashes: an invented 1980 would look like a real date. The format covers
1980-2107 — an ext2 file dated before 1980 comes out with no date rather than
with a wrong year.

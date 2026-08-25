# EX-OS — Extensible Operating System

[🇮🇹 Italiano](README.md) · **🇬🇧 English**

**Version:** 0.205
**Author:** Graziano Falcone <exagonx@hotmail.com>
**License:** GNU General Public License v2 (GPL-2.0)
**Architecture:** x86 32-bit, FAT12 1.44MB floppy

*The two versions are kept in step: what is in one is in the other.*

---

## What EX-OS is

EX-OS is a baremetal operating system written in C and assembly for the x86
32-bit architecture. It boots from a 1.44MB FAT12 floppy, from a hard disk
(FAT16/32 or ext2), and reads CDs and DVDs. The goal is an extensible
system: the kernel is small and read-only in conventional RAM, everything
else (drivers, shell, programs) runs in extended RAM in protected space.

A driver or a program crashing cannot bring the system down.

### The kernel is a **minikernel**

It is not a microkernel and it is not monolithic, and it is worth saying
why neither word fits.

**Not a microkernel**: virtual memory, the scheduler, the VFS with
FAT12/16/32, ext2 and ISO 9660, the ELF loader and the block cache all live
inside the kernel. A real microkernel keeps those outside, and this one
doesn't — at 30,000 lines and 66 system calls, the label would be
flattering and false.

**Not monolithic**: the device drivers — keyboard, floppy, PCI, NE2000,
PCnet, tty, IP — are **ring3 processes**, ELF executables like the ones in
`/bin`, talking over IPC and executing not one privileged instruction. The
kernel mediates every hardware access and checks permissions on every call;
a driver that dies gets restarted.

"Hybrid monolithic" would be accurate and useless: it is a label that only
describes. **Minikernel** says the same thing and carries a commitment as
well — *stay small* — which is the reason this architecture was chosen in
the first place. Today the number is:

    build/kernel.bin      184 KB      ~30,000 lines of C and assembly

The number is here because a commitment without a measurement is an
intention. Anyone adding code to the kernel should first ask whether it
could be a ring3 process instead — it almost always can, and that is how
every driver came about.

**Since August 2026 EX-OS hosts third-party code, and hosts it on its
own**: GNU binutils 2.44 — `as` and `ld` — is compiled *for* EX-OS and runs
inside it, and a program assembled and linked here is byte-for-byte
identical to one produced by the cross-compiler on Linux.

Since 6 August **the chain is closed**: `gcc` runs inside EX-OS, finds its
own headers with nobody telling it where, and chains cc1, `as`, `collect2`
and `ld` by itself all the way to an executable that runs. The same holds
for **`g++`** — containers, `std::string` and exceptions included. See
[The compilation chain inside EX-OS](#the-compilation-chain-inside-ex-os).

---

## What's new

Entries are marked **tested** when the work has been verified running inside
EX-OS, **to be tested** when the code is there but the proof that counts —
the one on real hardware or on the real case — has not been done yet.

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
`subjectAltName`. Without a store nothing opens: encrypting with whoever answers
means encrypting with whoever is in the middle, and the bar would say
`https://` all the same.

The price, declared: a site serving **only** ECDSA certificates does not open —
chain verification wants RSA — and gets a clear error instead of a page.

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

What is **not** there, declared: JavaScript, `@media`, relative units.


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
| `exwin` brings up graphics **on a console of its own**: Alt+F2 goes there, Alt+F1 comes back to the shell | tested |
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
| `pm` — desktop, taskbar at the bottom, **Start** button, menu of applications | tested |
| **Exit** (back to the shell) and **Shut down** entries in the menu | tested |
| `/exwin/bin`, `/exwin/lib`, `/exwin/dev` — graphical applications **do not live in `/bin`** | tested |
| The application list is a **text file**, `/exwin/lib/applicazioni.txt` | tested |
| `filemgr` — file manager: scrolling list, directories first, opens files with the editor | tested |
| `edit` — text editor: arrows, Home/End, PgUp/PgDn, Delete, mouse click, Ctrl+S, Ctrl+Q | tested |

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
  `fbc`, `as`, `ld`, libstdc++, OpenSSL. 150 MB installed separately with
  `toolinst`.
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
| TLS/SSL as a userspace library (OpenSSL port) | to do |

---

## Floppy layout

```
/
├── LOADER.BIN       ← Stage 2: finds and loads the kernel via FAT12
├── KERNEL.BIN       ← EX-OS Kernel
├── boot/
│   └── kernel.cfg   ← Configuration: env, shell, modules
├── bin/
│   ├── sh           ← Shell (static ELF, first process)
│   ├── login        ← The login (static ELF: it is the program you get in with)
│   ├── sudo         ← Runs a command as root, if you are entitled to
│   ├── ls           ← Directory listing
│   ├── hello        ← Example program
│   ├── textline     ← Line-oriented text editor (edlin style)
│   ├── gfedit       ← Full-screen editor (MS-DOS EDIT style)
│   ├── mkdir        ← Create a directory
│   ├── rmdir        ← Remove empty directories
│   ├── delete       ← Delete files (with ? and * wildcards)
│   ├── rename       ← Change a file's name (does not move it: see below)
│   └── chkdsk       ← Check and repair a FAT12/16/32 volume
├── lib/             ← Shared libraries (Phase 4b)
└── dev/
    ├── kbd.drv      ← PS/2 keyboard driver (ring3 process)
    └── floppy.drv   ← Floppy controller driver (still ET_DYN, not loaded)
```

The TTY does not appear in `/dev`: `drivers/tty/tty.c` is compiled **into**
the kernel (it owns the VGA), and for input it acts as a client of the `kbd`
service.

### What goes on the floppy, and what does not

The floppy carries **the system**: booting, preparing a disk, installing
itself, reading and writing files. Partitioner (`fdisk`), formatter
(`mkfs`), checker (`chkdsk`), mounting, installer, editors; the floppy
driver and the keyboard one, which are needed to start.

! **The extra drivers do not go there.** Networking (`pci`, `ne2k`,
`pcnet`, `ip`) and everything that comes later live on the **EX-OS CD**. It
is not a preference: they do not fit in 1.44 MB, and the way they do not
fit is the worst one — `mcopy` fails halfway through the list, the image
ends up missing some file picked by alphabetical order, and the system
boots as far as the point where it needs what is missing.

The CD-ROM **has no driver in `/dev`**: ATAPI and ISO 9660 live *inside*
the kernel, because the kernel must be able to mount the root from there
before any process exists that could serve it.

```
                    floppy    EX-OS CD    tools CD
system and shell      yes        yes          -
fdisk, mkfs, chkdsk   yes        yes          -
kbd.drv, floppy.drv   yes        yes          -
network drivers       NO         yes          -
ping, ftp, dhcp…      NO         yes          -
as, ld, cc1           NO         NO          yes
```

`make verify` **checks the rule** instead of trusting it, and reports how
much space is left on the floppy — the number that warns you before the
image stops holding everything:

```
[OK] nessun driver da CD sul floppy
                            629 760 bytes free
```

Whatever does not fit in 1.44 MB lives on the **tools CD** (`make iso`):
native `as`, `ld` and `cc1` in `/bin`, the libc headers and source in
`/exos`, the target runtime in `/exos/lib`, the documentation in `/doc`.

---

## Prerequisites (Debian 12)

```bash
# 1. Install the cross-compiler (fully automatic, ~30 min):
chmod +x tools/install_crosscompiler.sh
./tools/install_crosscompiler.sh

# 2. Enable it in the current session:
export PATH="$HOME/opt/cross/bin:$PATH"

# 3. Check:
i686-elf-gcc --version   # must print: i686-elf-gcc 13.2.0 ...
nasm --version           # nasm version 2.x
mformat --version        # Mtools version ...
```

The `install_crosscompiler.sh` script automatically installs:
- Debian dependencies (`build-essential`, `nasm`, `mtools`, `qemu-system-i386`, etc.)
- `binutils 2.41` cross-compiled for the `i686-elf` target
- `GCC 13.2.0` cross-compiled for the `i686-elf` target (C language only)
- Adds `~/opt/cross/bin` to `~/.bashrc`

---

## Build and test

```bash
make all          # Build everything + create dist/floppy.img
make run          # Boot under QEMU (32MB RAM)
make iso          # Tools CD (as, ld, headers, docs)
make run-iso      # QEMU with the CD mounted on /cdrom
make hd           # Bootable hard disk (formatted by EX-OS itself)
make run-hd       # QEMU from the disk, without a floppy
make debug        # QEMU + GDB stub on port 1234
make verify       # Check the floppy layout
make clean        # Remove build/
make distclean    # Remove build/ and dist/
```

`make iso` includes native `as` and `ld` if it finds them in
`$(BINUTILS_NATIVI)` (default `~/exos-native/build-nativi`); if they are not
there it says so and builds the CD anyway. How to build them:
`tools/binutils-exos/leggimi.md`.

### Full boot log over the serial port

The kernel mirrors on COM1 (38400 8N1) all the output that goes through
`vga_putchar()`: `kprintf`, `klog`, keyboard echo and process output. It is
needed because the VGA screen is 80x25 and the boot messages scroll away.

```bash
qemu-system-i386 -drive file=dist/floppy.img,format=raw,if=floppy \
  -m 32M -boot a -display none -serial file:/tmp/serial.txt -no-reboot
```

### Writing the image to a physical floppy (from WSL)

Three ways, same engine:

```bash
# from WSL
./tools/write_floppy.sh              # dist/floppy.img to A:
./tools/write_floppy.sh -d B: -y
```

```powershell
# from PowerShell
.\tools\write_floppy.ps1
.\tools\write_floppy.ps1 -Drive B: -Yes
```

```bat
REM from cmd.exe, or double-click from Explorer
tools\write-floppy.cmd
tools\write-floppy.cmd B:
```

An Administrator console is not needed: the script elevates itself (UAC) in a
window that stays open to show the outcome.

WSL2 cannot see Windows physical disks, so `dd` is of no use: the job goes
through PowerShell, which locks and dismounts the volume, writes the raw
volume and **reads it back to verify** byte by byte. It refuses non-removable
drives unless given `-Force`.

> ! **USB floppy**: the bootloader goes through the BIOS (INT 13h) and works,
> but the kernel talks to the floppy controller directly (ports 0x3F0-0x3F7,
> DMA, IRQ6). A USB floppy is not attached to that controller: the kernel
> starts but cannot read `/bin/sh`. An internal floppy drive is required —
> see `HANDOFF.md` for the alternatives.

**Toolchain note**: the `Makefile` uses the native `gcc -m32`, not the
`i686-elf-*` cross-compiler. The `tools/install_crosscompiler.sh` script is
still available, but the build does not require it (it does require
`gcc-multilib`).

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
process lives**, and loaded pages are never thrown away (there is no
eviction, and therefore no swap file).

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
> binaries are static. And there are no threads: this is the piece needed in
> order to have them, not the threads themselves.

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
0xbffbc000  heap_max — the ceiling
0xbffbd000  guard page
0xbffbd000  TLS block, if the program has one
0xbffbf000  stack reservation (256 KB)   <---- the stack grows downwards
0xbffff000  top of the stack
```

The heap starts **right after the last loaded segment**, not at a fixed
address. Since 0.156 it also has a **ceiling**: a guard page below the TLS
block if there is one, below the stack reservation if there is not.

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
| 248 | lib_apri      | `const char*`| —        | —   | map a shared library, return its export table |

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
| 252 | pty_ctl     | fd            | command    | arg | window size, raw mode, foreground group |
| 253 | statperm    | `const char*` | `StatPerm*`| —   | mode, uid and gid of a path **without opening it** |
| 254 | su          | `const char*` | password   | —   | «become root IF you know the password», and the kernel decides |

! **TWO NARROW CAPABILITIES, AND THE SAME PRINCIPLE.** `fb_map` does one thing
— map the framebuffer — where `mmio_map` did «map any physical address», which
a window server does not need and a hostile program needs very much. `su` does
one thing — become root knowing the password — where the setuid bit on files
would make every executable carrying it dangerous. A permission that does
exactly what is needed can be reasoned about; one that does more can only be
hoped not to be used.

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
signals, which EX-OS does not have. A job here is either running or finished;
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
   for it). Unix would use `SIGTTIN` here; without signals, EOF is the only
   possible answer — and it is true anyway, that program will never get any
   input.
2. Whoever takes the keyboard by talking **directly** to the `kbd` service
   over IPC — `gfedit`'s raw mode — does not go through `sys_read`, so it
   checks `ConsoleInfo.fg` itself and refuses to start in the background,
   explaining why.

---

## Virtual consoles — Alt+F1 … Alt+F4

Four independent screens, only one visible at a time, each with its own shell
started at boot. **Alt+F1..F4** switches: the program that was running is
neither suspended nor closed, it keeps working and drawing into its own
buffer, and finds the screen intact when you come back to it.

It is the answer to "how do I start something else without closing this
one": open `gfedit` on console 2, press Alt+F3, you get a clean prompt, and
Alt+F2 takes you back to the editor exactly where you left it.

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

! In QEMU the RTC starts in **UTC**, not local time. To see the time in your
timezone you need `-rtc base=localtime` among the Makefile's `QEMU_FLAGS`.

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

## The graphical interface in practice

```
exwin                       brings up graphics on a console of its own
                            Alt+F2 goes there, Alt+F1 returns to the shell

/exwin/bin/pm               the desktop (exwin starts it by itself)
/exwin/bin/filemgr [DIR]    the file manager
/exwin/bin/edit [FILE]      the text editor
```

Once graphics are up, the shell **stays alive on console 0**: you keep working
there and switch to the desktop with `Alt+F2`.

! **APPLICATIONS ARE LAUNCHED FROM THE SHELL'S CONSOLE, THEN YOU SWITCH.**
Typing the command *after* `Alt+F2` sends the keys to the graphical server, not
to the shell — and it looks as if the system had frozen. It is the same
separation that makes everything else possible, seen from the awkward side.

### The editor

| key | what it does |
|---|---|
| arrows, Home/End, PgUp/PgDn | move the cursor |
| Backspace, Delete | erase backwards and forwards |
| Enter | splits the line |
| mouse click | places the cursor |
| Ctrl+S | save; with no name it opens the **Save as** dialog |
| Ctrl+Q | quit; if the text was modified it warns and asks again |

The buttons are **New**, **Open**, **Save**, **Save as**, **Reload**. "Open"
and "Save as" live in `exdlg.so`, the shared dialog library, which the file
manager uses too.

Missing, and declared: undo, selection and clipboard. And a **yes/no** dialog:
today "do you want to lose your changes?" is asked by making you press the same
button twice, and it shows.

### The file manager

Scrolling list, **directories first**, `Up` and `Open` buttons, a status line
with the path. Arrows and Enter as well as the mouse. Pressing "Open" on a file
hands it to the editor, looked for in `/exwin/bin` and then in
`/cdrom/exwin/bin`.

! **DIRECTORIES COME FIRST, AND IT IS NOT COSMETIC:** in a directory with a
hundred files, the ones you want to enter would be scattered among them. It is
the only thing this list sorts — sorting names would mean a comparison that
depends on the language.

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

### /bin/mkfs — FAT16/FAT32/ext2 formatter

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
Above 511 characters a command line cannot be typed, and that is the limit of
a single IPC message between the keyboard and the shell.

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
the CMOS clock — with no timezone, because the system does not know which one
it is in: `localtime` and `gmtime` give the same time. `stat`/`fstat` in the
POSIX form.

### Processes: spawn with environment and redirections

`spawn()` starts a program and returns the PID; `waitpid()` collects its
result. There is no `fork()`, and that is a choice: duplicating an address
space only to throw it away at the following `exec`, on a system without
copy-on-write, would be the most expensive thing one could do.

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

> ! **No `SIGPIPE`**: EX-OS has no signals, so only the return value is
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

An EX-OS library is a perfectly ordinary ELF, linked at a reserved address,
whose **entry point is not code**: it is a table of names.

**The library source** declares what it exports:

```c
#include "exlib.h"

static const char *const nomi[] = { "pippo", "pluto" };
static void *const dove[]       = { (void *)pippo, (void *)pluto };

EXLIB_TESTA(mia_tabella, nomi, dove);
```

**The linker script** puts it at the base of its slice:

```
ENTRY(mia_tabella)

SECTIONS
{
    . = 0x04C00000;             /* the first free slice */

    .exlib_testa : { KEEP(*(.exlib_testa)) }
    .text        : { *(.text) *(.text.*) }
    .rodata      : { *(.rodata) *(.rodata.*) }

    . = ALIGN(4096);            /* mandatory, see below */

    .data : { *(.data) *(.data.*) }
    .bss  : { *(.bss) *(.bss.*) *(COMMON) }
}
```

**Whoever uses it** asks for the names it needs:

```c
const ExLibTesta *t = exlib_apri("/lib/mialib.so");
void (*pippo)(void) = exlib_simbolo(t, "pippo");
```

In practice you do not do this by hand: you write a **stub** — a file with the
same functions as the library, each one a thunk to the resolved pointer — and
applications link against that. Their source does not change by a single line.
See `lib/exwin/exwin_stub.c` as the model.

### The three rules you cannot break

! **`. = ALIGN(4096)` BEFORE `.data`.** The kernel shares read-only pages and
gives a private copy of writable ones, and it works in **whole pages**. If the
end of `.rodata` and the start of `.data` sat in the same page, that page would
be writable — that is, copied for every process — and half of `.rodata` would
quietly stop being shared **with nobody saying so**.

! **ONE SLICE PER LIBRARY, ASSIGNED IN ONE PLACE.** The map lives in
`lib/exwin/exwin.ld`. Two libraries at the same base would overwrite each other
inside the process that uses both.

! **YOU ADD, YOU DO NOT REMOVE.** Adding functions, reordering them and
rewriting their bodies is always allowed: already-compiled applications keep
working. Removing a name breaks them — and they say so, naming the missing
symbol, instead of jumping into nothing.

### If the library uses another library

A program has `_start`, and there is a natural place there to attach what it
needs. **A library does not start**: its functions are simply called. If it
needs other libraries, it exports the optional name `__lib_avvio`:
`exlib_apri()` looks it up and calls it, and that is the only moment at which
the library is known to have just been mapped.

That is how `exwin.so` and `exdlg.so` use `libc.so` without carrying a copy of
it inside.

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
/exos/bin/                            gcc, g++, cpp, fbc
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
[The compilation chain inside EX-OS](#the-compilation-chain-inside-ex-os).

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

## Halting and powering off

| Shell command | Effect |
|---|---|
| `halt` | syncs the filesystem, stops the system, does **not** power off |
| `poweroff` / `shutdown` | syncs, counts 3 seconds, powers the hardware off |
| `reboot` | syncs and restarts (reset via the 8042, triple-fault fallback) |

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

### The IPv4 stack: `/dev/ip.drv`, `ping`, `ipcfg`

ARP, IPv4 and ICMP live in a **process of their own**, not in the driver:

```
ping ──IPC──> ip.drv ──IPC──> ne2k.drv ──I/O ports──> card
```

Three reasons, all practical: these protocols are the same on any card
(putting them in the driver would mean rewriting them for the PCnet); the
stack has **timing** — ARP expiry, waiting for replies — while the driver
only has to answer the hardware; and if the stack gets it wrong you restart
the stack, while the card stays powered up and configured.

```
ex-os:/> /dev/pci.drv &
ex-os:/> netdetect -c
ex-os:/> /dev/ip.drv &
ip: indirizzo  10.0.2.15
    maschera   255.255.255.0
    gateway    10.0.2.2
ip: servizio 'ip' attivo

ex-os:/> ping 8.8.8.8 -n 3
  60 byte da 8.8.8.8: seq=1 ttl=255 tempo=70 ms
  60 byte da 8.8.8.8: seq=2 ttl=255 tempo=50 ms
  60 byte da 8.8.8.8: seq=3 ttl=255 tempo=60 ms

3 inviati, 3 ricevuti, 0% persi
tempi: minimo 50 ms, medio 60 ms, massimo 70 ms
```

`ipcfg` shows the address and the **counters**, `ipcfg -r` the ARP table. The
counters are half the program: when the network does not work, the question
is not «does it work or not» but where it stops, and `IP ricevuti` at zero,
`scartati` rising, or `somme errate` rising point at three different places.

What the stack does **not** do, said up front:

- **it does not fragment and does not reassemble** — a datagram larger than
  the MTU is refused, an incoming one that is a fragment is counted and
  dropped;
- **no routing table** — there is one local network and one gateway;
- **no DHCP inside the stack** — the address is declared (`ip.drv -a … -m …
  -g …` or `ipcfg -a …`); getting one from a server is the `dhcp` program's
  job, and it sits on top of UDP like any other client;
- **one echo request at a time** — `ping` is sequential by nature.

! `ping` distinguishes **three** outcomes, not two: reply received; no
answer to the **ARP** (there is nobody at that address — we did not even get
out onto the wire); no answer to the **echo** (the packet left, the problem
is further away). A single «unreachable» would force you to start the
diagnosis over every time.

! A time of `<10 ms` is not a zero: `uptime_ms()` counts PIT ticks at
100 Hz, so it advances in 10 ms steps. Writing «0 ms» would claim a precision
that is not there.

### Long names on FAT (VFAT), reading

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

### `mkfs` picks the filesystem from the size

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

### UDP and DHCP

The stack does UDP too. There are no sockets and no descriptors: you open a
**port**, and from that moment the datagrams for that port belong to whoever
opened it. It is enough for a DHCP client and for a future DNS resolver; a
real socket API will be built on top of this one, not in its place.

```
ex-os:/> dhcp
dhcp: cerco un server (52:54:00:12:34:56)...
dhcp: offerta di 192.168.76.9: 192.168.76.30

  indirizzo  192.168.76.30
  maschera   255.255.255.0
  gateway    192.168.76.9
  DNS        192.168.76.3
  concessione 86400 s

dhcp: configurato.
```

`dhcp` is a **program**, not a piece of the stack: DHCP sits on top of UDP
like a DNS client, and a bug in there makes one command fail instead of
taking the network down. `dhcp -n` asks and prints without applying.

! **It does not renew the lease.** When it expires, run it again. Renewal
wants a process that stays alive until halfway through the lease time, that
is, a different program from this one — which has to be startable by hand and
to finish.

! A datagram for an open port with nobody waiting is **dropped and counted**
(`ipcfg` shows it). UDP loses packets by definition, and a queue growing
while nobody reads is a slow way of running out of memory at the sender's
discretion. Whoever expects a datagram must post the receive **before**
sending the request.

### Floating-point `printf`

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

### ! `time_t` is 64-bit

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

### `/boot/autoexec.sh` — commands at startup

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

### `!silenced` — the scripts' `echo off`

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

### Command history and line editing

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

! **Only the foreground console** gets the keys. Without that check all four
shells fought over the keyboard, and the invisible ones put it back into
cooked mode, taking it away from whoever was typing.

! The redraw uses **Backspace only**, because EX-OS's TTY has no cursor
positioning language. Consequence: on a line longer than the screen width the
editing looks wrong — but the line stays correct, and what you read is what
will be executed.

### Names with spaces: quoting

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

### `ls` — display modes

```
ls -h              list every option
ls -mc /bin        columns: names only, the most compact
ls -d              details: size, date and time
ls -md             detailed, dir style: adds the attributes
ls -a              also show names beginning with a dot
ls -p              one page at a time (Enter advances, q stops)
```

```
ex-os:/> ls -md /
data        ora    attr   dimensione  nome
2026-08-04  10:51  D----       <DIR>  BOOT
2026-08-04  10:51  D----       <DIR>  BIN
2026-08-04  10:51  -----      180584  KERNEL.BIN

2 file, 181679 byte    4 directory
```

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

### TCP

```
ex-os:/> tcptest example.com 80
connessione a 172.66.147.243:80 ...
connessa (id 1)
mandati 18 byte

HTTP/1.1 403 Forbidden
Server: cloudflare
...
--- ricevuti 408 byte ---
```

DNS → ARP → IP → TCP, data in both directions through the NAT. (The 403 is
HTTP: `GET / HTTP/1.0` without a `Host` is refused by Cloudflare. The
transport worked — that reply proves it.)

! **Outgoing connections only.** The `LISTEN`/`SYN_RECEIVED` branch of the
state machine is missing, which is about half the work and is what it takes
to be a **server**. This TCP's first customer is an FTP client in **passive**
mode (`PASV`), which opens the data connection itself too; active mode would
require listening and does not work behind a NAT anyway. You build the half
that is needed, and you say it is a half.

What it does **not** do, declared in `drivers/net/ip_proto.h`:

| | |
|---|---|
| **no reordering** | an out-of-sequence segment is **dropped** and re-acknowledged: whoever sent it retransmits. Correct but not efficient — keeping the pieces wants a list with its own timers, and that is where a young TCP picks up its worst bugs |
| **no congestion control** | as much is sent as the other side's window allows. On a local network it changes nothing; on the Internet it means being rude under loss |
| **fixed RTO** | the round-trip time is not measured: it doubles from 600 ms. Measuring it properly (Karn, Jacobson) is the next step |
| **no SACK, window scaling, timestamps** | |

! **Sequence numbers are compared by subtraction, never with `<`.** They are
32-bit and they wrap: `a < b` across the wrap gives the reversed answer, once
every 4 GB transmitted — that is, rarely, and always when the connection is
busy.

! `IP_MSG_TCP_APRI` can answer **`-EAGAIN`**: it means the stack has just
asked for the next hop's ARP. It is not a failure, it is «in a moment».
Threading the ARP wait into TCP's state machine would mean two nested timers
on the same connection.

### `ftp` — FTP client

On the EX-OS CD, along with the other network tools.

```
ex-os:/> ftp 10.0.2.2 ls
220 Server pronto
331 Serve la password
230 Accesso eseguito
-rw-r--r-- 1 exos exos     4053 Jan  1 00:00 grande.txt
-rw-r--r-- 1 exos exos       56 Jan  1 00:00 leggimi.txt

ex-os:/> ftp 10.0.2.2 get grande.txt /disco/copia.bin
4053 byte in '/disco/copia.bin'
```

With no command it opens a command line: `ls`, `cd`, `pwd`, `get`, `put`,
`bye`.

! **Passive mode (`PASV`) only.** In active mode it is the *server* that
connects back to the client, which therefore has to **listen** — and EX-OS's
TCP cannot, on purpose. It is not a fallback: active mode does not work
behind a NAT anyway, which is why every serious client has used `PASV` for
twenty years.

! **FTP sends the password in the clear.** It is not a defect of the
program, it is the protocol: anyone on the path reads the user and the
password just as they are. The client says so at login, once, instead of
leaving it to be inferred. The alternative will be called SFTP or FTPS when
there is TLS — not «ftp with a patch».

! If the server announces in `PASV` an address different from the one we are
connected to, the client **uses the real one**: a server behind a NAT often
announces its private address, which is unreachable from outside. The port is
the useful information; the address we already know.

To try it without a real server there is `tools/ftpserver-prova.py` — !
which **is not an FTP server**: it lets anyone in and serves a single
directory, and is meant to be run on localhost for the length of a test.

### `telnet` — an interactive session

```
telnet name-or-address [port]        Ctrl+] to leave
```

! **Telnet sends everything in the clear, password included.** It is not a
defect of the program, it is the protocol: anyone on the path reads the
user name, the password and everything typed afterwards. The client says so
once at startup instead of leaving it to be inferred. The alternative will
be called SSH when there is TLS — not «telnet with a patch».

**How it listens to the network and the keyboard at once.** There is no
`select()` and there are no threads, but there is something better for this
case: in EX-OS everything comes through the same mailbox. You *book* a
receive from the IP stack (`IP_MSG_TCP_RICEVI`), you *book* a key from the
keyboard service (`KBD_MSG_READKEY`), and then you wait with a single
`ipc_recv_timeout()` and look at **who** answered. The two bookings re-arm
independently: if the server is quiet you keep receiving keys, if nobody
types you keep receiving data.

! **The timeout matters even when nothing times out**: the keyboard's raw
mode goes away by itself whenever somebody else asks for a line, and
without a periodic wake-up that reasserts it the program would sit waiting
for a key the driver will never deliver.

**Option negotiation cannot be skipped.** Telnet interleaves commands
starting with byte 255 (IAC) into the data. A client that ignored them
would print control characters and — far worse — would leave the server
*waiting*: many servers do not even send the `login:` until negotiation is
done.

| | |
|---|---|
| **refuse everything you cannot do** | to a `DO` for an unknown option answer `WONT`, to a `WILL` answer `DONT`. Silence is not a refusal: it is a server waiting |
| **never answer an answer** | two polite implementations that always reply bounce the same option back and forth forever. The state of what has already been granted is kept |
| accepted | `ECHO` and `SGA` from the server, `TTYPE` and `NAWS` towards it |
| `IAC IAC` | is a 255 byte in the data, not a command |

The parser state is **static, not local**, and that matters: an IAC
sequence can be split across two segments — TCP delivers bytes, not
messages — and a state reset on every call would print half a command and
answer the other half as if it were a different one.

Two keyboard translations that are not obvious:

- **Enter is `CR LF`**, not a bare `LF`: telnet's network virtual terminal
  wants a CR always followed by LF or NUL, and servers that apply the rule
  literally do nothing with a lone LF.
- **Backspace is sent as `0x7F`**, not as the `0x08` the key produces here:
  on Unix systems the default erase character is DEL, and sending `0x08`
  leaves the line unchanged and prints `^H` — which looks like a keyboard
  defect while it is a convention on the other side.

To try it without exposing a shell there is `tools/telnetserver-prova.py` —
! which **is not a telnet server**: it echoes and answers made-up
commands. It exists because putting a real `telnetd`, which hands out a
shell with no encryption, on a listening port would be a bad idea on any
machine. The test as seen from its side:

```
  <- DO ECHO
  <- DO SGA
  <- WILL TTYPE
  <- WILL NAWS
  <- schermo: 80x25
  <- terminale: 'EXOS'
  riga: 'ciao mondo'
```

### Name resolution: `host`, and `ping` by name

```
ex-os:/> host one.one.one.one
one.one.one.one ha indirizzo 1.0.0.1  (risposta da 10.0.2.3)

ex-os:/> ping www.google.com -n 2
ping www.google.com (142.251.151.119) con 32 byte di dati
  60 byte da 142.251.151.119: seq=1 ttl=255 tempo=50 ms
```

The resolver is a **module** (`lib/dns.c`), compiled into the programs that
need it — not a service and not part of the stack. DNS sits on top of UDP
exactly as DHCP does: an error parsing a reply written by an unknown server
must make one command fail, not take the network down. And it has no state to
keep between calls, so a dedicated process would only cost one more thing to
start and watch over.

! **Name-compression pointers are followed only when reading, with a cap on
the hops.** In a DNS reply a name can end with a pointer to an earlier point
in the message; that pointer is written by the server, and nothing stops it
from pointing at itself. To *skip* a name it is not followed at all — a
pointer ends the name, and the length is known.

! `ping` prints the name **and** the address when given a name: without it,
faced with a strange answer you cannot tell a DNS failure from a network one.

`host` exists so the resolver can be tested on its own. When `ping name` does
not work, the question is whether ping or DNS is broken, and without this
command you have to guess.

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

---

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
| **shared position between duplicated fds** | `dup()` works, but the two descriptors each keep their own offset |
| **out-of-order TCP** | segments arriving out of order are dropped instead of reordered, and the RTO is fixed rather than measured |
| **DNS: A records only** | CNAMEs are skipped instead of followed; if the reply does not already contain the final A record, the name does not resolve |
| **DHCP renewal** | `dhcp` takes the lease and finishes; renewal wants a process that stays alive |
| **TLS** | without encryption there is no HTTPS and no SFTP; the first step is an entropy source, not the protocol |
| **exFAT** | ! it does not exist as a filesystem: it has to be implemented first, and only then does a chkdsk make sense |
| **`rename` across directories** | ENOSYS today: it would be a copy plus a delete, that is, another operation |
| **DMA for the disk** | 0.75 MB/s in PIO today |

---

## License

EX-OS is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License version 2 as published by the Free
Software Foundation.

See the `LICENSE` file for the full text.

---

*EX-OS — "The system extends, the kernel stays small."*

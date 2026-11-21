# EX-OS — The graphical desktop (ExWin)

**🇬🇧 English** · [🇮🇹 Italiano](scrivania.it.md)

[← README](../README.md) · [All the manuals](../README.md#the-manuals)

**Contents**

- [The editor](#the-editor)
- [The file manager](#the-file-manager)
- [The terminal in a window](#the-terminal-in-a-window)
- [EXBrowser, the browser](#exbrowser-the-browser)
- [EX-IDE — the visual development environment](#ex-ide--the-visual-development-environment)
- [The font test](#the-font-test)
- [The clock](#the-clock)

---

## The graphical interface in practice

```
exwin                       brings up graphics on console 5
                            Alt+F5 goes there, Alt+F1 returns to the shell

/exwin/bin/pm               the desktop (exwin starts it by itself)
/exwin/bin/filemgr [DIR]    the file manager
/exwin/bin/edit [FILE...]   the text editor: one file per tab
/exwin/bin/term [PROG]      the terminal in a window (no PROG: the shell)
/exwin/bin/exbrowser [URL]  EXBrowser, the browser (an absolute path becomes a file:)
/exwin/bin/exide [DIR]      the visual development environment
/exwin/bin/archivi [ARCH]   ZIP, TAR and TAR.GZ archives: open, extract, create
/exwin/bin/calctor          the calculator: normal, scientific, programmer
/exwin/bin/pennello [FILE]  the paint program: opens BMP PNG JPG GIF ICO, saves PNG and BMP
/exwin/bin/immagini [FILE]  the picture viewer, with zoom and the directory
/exwin/bin/fontprova        the TrueType font test, made to be looked at
/exwin/bin/orologio         date and time in the corner of the bar
```

Once graphics are up, the shell **stays alive on console 0**: you keep working
there and switch to the desktop with `Alt+F5`.

**From the desktop they open from the Avvio menu**, which reads its entries
from `/exwin/lib/applicazioni.txt`. One line per application, with an
**optional** third field for the icon:

```
Displayed name         | /path/executable     | /path/icon.ico
Strumenti/Editor       | /exwin/bin/edit      | /exwin/icon/baseapp/edit_64.ico
```

! **A slash in the name makes a category.** `Strumenti/Editor` is the entry
"Editor" inside "Strumenti": in the menu, categories sit at the top and open a
list **to the side**, with their icons. They are declared nowhere else — they
exist as long as an entry names them.

Since 7 November 2026 (voce 80, pm 0.028) the published menu has five groups:
**Applicazioni** (File manager, Archivi, Calctor, Pennello, Immagini),
**Internet**, **Strumenti**, **Multimedia** (Lettore audio, Volume, VLC) and
**Giochi**. On a machine already installed netupdate (0.046) brings the new
menu and puts back at the end the entries the user had added: an entry is known
by the program it starts, so the system's entries take their new group and the
others stay (`menu_ricupera`). The Start menu closes when a program is chosen
or the desktop is clicked; the icons beside its entries can be turned off
(Impostazioni, Scrivania tab, "Icone accanto alle voci del menu", on by
default).

The button **Applicazioni del menu...** in the Sistema tab of Impostazioni (until
pm 0.027 the item "Applicazioni..." of the menu) adds and removes lines from that
file (putting back the category and icon of what it does not show), and the
`@avvio <path>` directive says which program starts by itself with the desktop
(that is how the clock is already there).

! **ADDING AN APPLICATION IS ONE LINE, NOT A RECOMPILATION**, and the file
stays readable and editable by hand on purpose: a configuration file only a
program can write is a file you cannot repair when that program will not start.

! **FROM THE SHELL YOU LAUNCH THEM WITH THE COMMAND, BUT BEFORE SWITCHING.**
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
| Shift + arrows, Ctrl+A | select text |
| Ctrl+X, Ctrl+C, Ctrl+V | cut, copy, paste |
| Ctrl+Z | undo (cut, paste, delete) |
| Ctrl+F, F3, Shift+F3, Ctrl+H | find, next, previous, replace |
| Ctrl+N, Ctrl+O | a new file, an opened file: each in its own tab |
| Ctrl+Tab, Ctrl+Shift+Tab | next tab, previous tab |
| Ctrl+W | closes the tab; asks if the file was modified |
| Ctrl+S | save; with no name it opens the **Save as** dialog |
| Ctrl+Q | quit; if files were modified it says how many and asks |

The same things are in the **File** and **Modifica** menus. "Open" and "Save
as" live in `exdlg.so`, the shared dialog library, which the file manager uses
too. `edit a.txt b.txt` opens two tabs.

Missing, and declared: **redo** after an undo, and undoing typing (the
commands that change the text are undone, not the keys typed). A file over
512 lines opens partially and is not saved.

### The file manager

Scrolling list, **directories first**, `Up` and `Open` buttons, a status line
with the path. Arrows and Enter as well as the mouse. "Open" on a file opens it
with the program it belongs to (`/exwin/lib/tipi.txt`: images with Immagini,
archives with Archivi, pages with EXBrowser, everything else with the editor),
looked for in `/exwin/bin` and then in `/cdrom/exwin/bin`.

The **right button** opens a menu: Open, New folder, New file, Rename, Copy,
Cut, Paste, Delete. **Several rows** are chosen with Ctrl+click and
Shift+click, or marked with the space bar; dragged onto a folder of the tree on
the left, a window asks whether to copy them, move them or leave them.

**Since 16 September 2026 the list has four columns and sorts on a click**
(`filemgr` 0.002):

```
Cartelle     |[Nome ^][Tipo][Dimensione][Data]
- /          | bin         <DIR>          -  2026-09-16 20:57
  + bin      | boot.img    <FILE>   1474560  2026-09-16 20:57
  + boot     | KERNEL.BIN  <FILE>    270376  2026-09-16 20:57
```

One click picks the column, a second click on the same one **reverses the
direction**, and the arrow in the label says which it is: «what is it sorted
by» and «in which direction» are two questions, and without a hint the second
one is answered by guessing. **Each column remembers its own direction** —
sorting by date you want the newest on top, sorting by name you want the A.

! **The header is not a new toolkit control**: it is four buttons aligned to
the columns, derived from the **same constants** the row is. It is the same
choice as the tree on the left, which is «a list with indentation in it» and
not a tree control. Written twice, the widths would drift apart the first time
a column was widened — and the symptom would be a header pointing at the wrong
column, which is a lie.

! **DIRECTORIES COME FIRST, AND IT IS NOT COSMETIC:** in a directory with a
hundred files, the ones you want to enter would be scattered among them. They
stay on top **whichever column you pick**; the only one that mixes them is
«Tipo», where separating them is exactly what the click asked for.

! **Two directories are not sorted by size.** The column shows a dash — the
number filesystems keep there is the size of the entry on disk, not how much
the contents weigh — but the number is there all the same, and sorting on it
left the folders in an order the viewer cannot explain. Between two dashes they
sort by name, which is the only thing on show.

! **The date costs one `stat()` per entry**, which the directory entry does not
carry. Here the price can be paid: a directory is read when somebody enters it,
not in a loop. It is the same date as `ls`, the **modification** one: the
filesystems do not keep a creation date.

### The terminal in a window

`/exwin/bin/term` opens a shell inside a window; `term /bin/gfedit` opens that
program there instead of the shell. The window is an exact multiple of the 8x16
font cell, because the toolkit's "terminal" control computes columns as
width/8 and rows as height/16.

! **THE SHELL RUNS ON A PIPE, NOT ON THE CONSOLE'S `tty`**, and that is the
whole point of a terminal in a window. A shell reading descriptor 0 of the
console competes for the keyboard with anyone else on that console; behind a
pipe that question does not exist — the server gives the keys to the focused
window, and from there they go into *that* shell's pipe. That is how two of
them can be open without disturbing each other.

### EXBrowser, the browser

`/exwin/bin/exbrowser [URL]`. With no argument it starts from the home page;
with an absolute path (`exbrowser /exwin/doc/exbrowser.html`) it turns it into a `file:`,
which is the only form the rest of the program knows.

The bar carries **two boxes**: the address and **Cerca**, which composes the
query for the engine chosen in **File > Impostazioni** (duckduckgo, wikipedia,
marginalia) by itself. What the page can do — text that wraps and scrolls,
links, images, style sheets, tables, forms, HTTPS, cookies, JavaScript — is in
the "What's new" entries above, which are where that work is told in full.

| keys | what they do |
|---|---|
| Ctrl+T, `target="_blank"`, `window.open()` | a new tab |
| Ctrl+click on a link | a new tab, in the background |
| Ctrl+N, Shift+click | a new window (a second EXBrowser) |
| right button on a link | Open, in a new tab, in a new window, Copy the address |
| Ctrl+Tab, Ctrl+W | next tab; closes the tab (the last one closes the window) |

Tabs you are not looking at keep their address and position, not the page:
coming back reloads it, from the cache if it is there.

### EX-IDE — the visual development environment

`/exwin/bin/exide [DIR]`. With an argument it opens the project living in that
directory straight away. Three areas as in Visual Basic: the tools on the left,
in the middle the form they are dropped onto, on the right the properties of
the selected one; double-click a control and the editor opens inside the
function its event will call.

! **THE JOINT BETWEEN THE DRAWING AND THE CODE IS THE ID**, and it was already
there: in the drawing the button *is* `ID_PULSANTE1`, in the source there is
`case ID_PULSANTE1:`. exide is feasible here more than elsewhere because ExWin
was already shaped like VB6 — there is nothing to invent, there is what the
drawing says to *write*.

A project is four files, and the rule that decides whether it survives is that
the generated and the written never touch:

| file | who writes it |
|---|---|
| `finestra.dis` | exide only: the drawing |
| `finestra.h` | exide only: the ids, the pointers, the prototypes |
| `finestra_gen.c` | exide only: creates the controls and dispatches the events |
| `finestra.c` | **you only**: exide *adds* the missing handlers at the end, and never rewrites what is there |

The project's other sources (`src/`) open from **Strumenti > Files**, in a
window with **tabs**: one file per tab, "Apri..." or Ctrl+O to add one, and
switching tab saves the file.

### The font test

`/exwin/bin/fontprova` draws the same lines in TrueType at different sizes, and
is **made to be photographed**: this system's graphical tests are measured in
pixels. The rasterizer has already been compared against FreeType, but that
comparison runs on the host — it says the glyphs come out right and says
nothing about `exfont.so` loaded at run time, about the cache, about blending
with the background, or about the font files being readable from the CD. This
window tests the whole round trip.

! **THE TOP LINE USES THE SYSTEM FONT, AND IT IS THE YARDSTICK:** if TrueType
does not load, only that line and the error messages remain, and you see at
once where it stopped instead of staring at an empty window.

### The clock

`/exwin/bin/orologio` puts date and time in the corner of the bar, and stays
**above every** window because it is a piece of the bar. It is a **separate
process**, not a thread inside the program manager: what you want is the time
to keep up whatever the rest of the system is doing, and a thread would share
the server connection and the message queue with it — a busy program manager
would be a stopped clock.

! **THE TIME IS LOCAL TIME**, in the zone of `/boot/fuso.cfg` (kernel 0.340):
until 0.339 the libc had no zones and `localtime()` was identical to `gmtime()`.

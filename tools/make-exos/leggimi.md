# GNU make dentro EX-OS

11 agosto 2026. `make` gira dentro EX-OS, lancia `gcc`, `as`, `ld`, `ar` e
`ranlib`, e costruisce un programma da un makefile scritto come su qualunque
altro sistema.

    /disk/pm> make
    gcc -O2 -c main.c -o main.o
    gcc -O2 -c conta.c -o conta.o
    gcc -O2 -c somma.c -o somma.o
    rm -f libprova.a; ar rcs libprova.a conta.o somma.o
    ranlib libprova.a
    gcc -O2 -o prova main.o libprova.a

    /disk/pm> ./prova
    prova-make — costruito DENTRO EX-OS
      somma dei quadrati : 385   (atteso 385)
      occorrenze di 'c'  : 3     (attese 3)
      esito              : tutto a posto

    /disk/pm> make
    make: 'prova' is up to date.

385 è la somma dei quadrati da 1 a 10: un valore noto in anticipo, non un
«sembra giusto». L'ultima riga vale quanto le altre — vuol dire che il
confronto delle date sul filesystem funziona, cioè che `make` è `make` e non
uno script che riesegue tutto.

---

## Come si rifà

```bash
tools/make-exos/prepara-make.sh              # -> ~/exos-native/build-make/make
PATH=$HOME/exos-cross/bin:$PATH make iso     # -> /exos/bin/make e /bin/make sul CD
```

Serve il cross `i386-exos-gcc` (`tools/gcc-exos/prepara-cross.sh`). I sorgenti
di GNU make 4.2 stanno in `make/` e lo script non scarica niente — il perché è
in testa a `prepara-binutils.sh`.

---

## Il porting: sei modifiche, e una sola è il porting vero

Le applica `tools/make-exos/applica.py`, con lo stesso marcatore per file di
`binutils-exos`: c'è → applicato, non c'è → da applicare.

| dove | cosa |
|---|---|
| `config/config.sub` | `exos` fra i sistemi operativi ammessi |
| **`job.c`** | **`child_execute_job` con `spawn_ex` invece di `vfork`** |
| `main.c` | `--output-sync` si rifiuta invece di essere ignorata |
| `commands.c` | `kill()`: a se stessi è `raise()`, agli altri ENOSYS |
| `arscan.c` | `<ar.h>` non c'è: si usa il ramo che upstream ha già per Android/BeOS |
| `read.c`, `glob/glob.c` | `<pwd.h>` non c'è: `getpwnam` rende NULL |

### Perché `spawn_ex` e non il ramo `__MSDOS__`

`child_execute_job` di GNU make è, per intero: `vfork()`, e nel figlio `dup2`
dei tre descrittori più `exec`. Cioè la definizione a parole di

    spawn_ex(percorso, argv, envp, redirezioni, n)

Non è un ripiego: è la stessa cosa detta in una chiamata invece che in cinque,
e senza duplicare uno spazio di indirizzamento per buttarlo via un'istruzione
dopo.

! **Il ramo `__MSDOS__` c'era già ed è la strada sbagliata.** Quello esegue il
comando *dentro* `child_execute_job` con `spawnvpe(P_WAIT, ...)` e poi finge di
avere un figlio da raccogliere (`++dead_children; child->pid = dos_pid++`).
Funziona, e costa il parallelismo: con `-j2` i lavori si metterebbero in fila.
Qui il figlio è un figlio vero e `reap_children` resta quella di sempre,
`WNOHANG` compreso.

### Cosa NON è disponibile, e lo dice

`make --output-sync=...` **si rifiuta di partire**. La sincronizzazione
dell'output fra lavori paralleli è costruita sui lock di record di `fcntl`
(`struct flock`, `F_SETLKW`), e la `fcntl` di EX-OS conosce solo `F_GETFL` e
`F_SETFL`: sono i lock POSIX sui file, cioè un pezzo di kernel, non una riga da
aggiungere.

! Upstream, compilato con `-DNO_OUTPUT_SYNC`, **accetta l'opzione e la
ignora**. Chi scrive `make -j4 --output-sync=target` in uno script lo fa
proprio perché l'output mescolato non gli serve, e si ritroverebbe esattamente
ciò che voleva evitare senza un avviso. La modifica a `main.c` serve a questo.

`~utente` non si espande (non c'è un registro degli utenti); `~` da solo sì,
viene da `HOME`.

---

## Le tre cose che hanno dovuto cambiare FUORI da make

Il porting di make è stato la parte facile. Quello che è servito perché
funzionasse davvero stava altrove, e ognuna delle tre si presentava con un
messaggio che accusava qualcun altro.

### 1. La shell non sapeva cosa fosse un `;`

`/bin/sh` eseguiva **un comando per riga**: niente `;`, `&&`, `||`, `>`, `|`.
Bastava finché a scrivere le righe c'era una persona. Ma make non spezza le
ricette: consegna la riga intera a `/bin/sh -c`, e la prima ricetta della
runtime di FreeBASIC è

    rm -f $@; $(AR) rcs $@ $^

Senza il `;`, la shell cercava un programma chiamato «rm -f libfb.a; ar» e
rispondeva «comando non trovato» nominando una stringa che nel makefile non
c'è. Ora ci sono sequenze, redirezioni (`>`, `>>`, `<`, `2>`, `2>&1`) e pipe.

Per strada è saltato fuori che **`>>` sovrascriveva invece di accodare**:
`O_APPEND` era accettato da `vfs_open`, copiato nel descrittore, e non lo
guardava nessuno. Un flag accettato e ignorato è la forma peggiore del difetto.

E poi che **la shell non espandeva i caratteri jolly**, per scelta: li
espandevano i comandi che li dichiarano (`delete`, `cp`), come su MS-DOS. La
scelta regge finché a scrivere la riga c'è una persona; cade con `make clean`,
che è `rm -f *.o` — e `rm` è il comando POSIX, che non espande niente perché
su POSIX lo fa la shell. Il risultato era che `make clean` **non cancellava
nulla e non lo diceva** (il `-f` tace sui file assenti), e la compilazione
successiva riusava oggetti vecchi. Ora la shell espande, distinguendo
maiuscole e minuscole; `delete` resta insensibile al caso.

### 2. `main(argc, argv, envp)` riceveva due argomenti

`_libc_start` chiamava `main(argc, argv)`. La terza forma di `main` non è nello
standard C ma esiste su ogni Unix, e make la usa: la prima cosa che fa dopo
l'avvio è scorrere `envp`. Riceveva quello che c'era sullo stack, cioè zero:

    [FAULT] page fault a 0x00000000 (protezione, lettura, EIP=0x08000450)

prima di aver stampato una riga.

### 2b. `mkdir` non conosceva `-p`

Ogni makefile crea le proprie directory degli oggetti con `mkdir -p`, e conta
su due cose che il `mkdir` nudo non faceva: i livelli intermedi
(`src/rtlib/obj/exos-x86` sono quattro) e **l'esito zero su una directory che
esiste già** — che è il caso normale dalla seconda esecuzione in poi.

! Nel farlo è saltata fuori una trappola: la strada ovvia — «prova `mkdir`,
perdona `EEXIST`» — presume di sapere *quale* errore significa «c'è già», e
non è sempre `EEXIST`: su un **punto di montaggio** il kernel risponde
`EBUSY`. `mkdir -p /disk/a/b/c` si fermava sul primo pezzo, `/disk`, dicendo
«risorsa occupata». Ora si chiede `stat` prima, e non c'è niente da indovinare.

### 2c. `uname` era solo un built-in della shell

`$(shell uname)` rispondeva

    make: makefile:601: fork: file o directory inesistente

cioè un errore che parla di `fork` mentre a mancare è un **eseguibile**.

! Il ragionamento che aveva lasciato le cose così — «`$(shell ...)` passa da
`/bin/sh -c`, quindi il built-in basta» — è sbagliato, e si è visto solo
eseguendo: **GNU make non passa dalla shell per un comando senza
metacaratteri**, lo lancia diretto per risparmiare un processo. Cercava un
programma di nome `uname` e non c'era.

E il built-in comunque non sarebbe bastato: stampava una frase per una persona
(«EX-OS version 0.176 (x86 32-bit) - Copyright…»), mentre un makefile scrive
`ifeq ($(shell uname),Linux)` e confronta con **una parola**.

Ora c'è `/bin/uname` (POSIX: `-s -n -r -v -m -a`) e **il built-in è stato
tolto**: due risposte diverse sotto lo stesso nome, a seconda di chi chiama,
sono peggio di una risposta scomoda. La frase per le persone si chiama `ver`,
e c'era già.

### 3. `readdir()` metteva `d_ino = 0` su ogni voce

! **È questo il difetto che è costato di più**, ed è il gemello di
`st_first_clus` (vedi RIPRENDERE.md). Su Unix `d_ino == 0` marca una voce
**cancellata**, e il codice di terzi salta quelle voci — GNU make lo fa in
`dir.c`, con `REAL_DIR_ENTRY(dp)`. Quindi make vedeva **ogni directory vuota**.

Il sintomo non parlava di directory:

    make: *** No targets specified and no makefile found.  Stop.

dentro una directory con dentro un `makefile` che `ls` elencava e che
`make -f makefile` leggeva senza storie. La differenza fra le due strade è che
quella senza `-f` passa dalla lettura della directory.

La lezione, la seconda volta: **un campo di identità riempito con una costante
non è "non implementato", è un'informazione falsa**, e chi la legge non ha modo
di accorgersene.

Ora `DirEntry` porta `ident`, composto con `VFS_IDENT` — la stessa macro di
`stat_interno()`, così `d_ino` e `st_ino` combaciano per lo stesso file. Ogni
driver l'identità ce l'aveva già pronta: ext2 l'inode, ISO l'extent, FAT il
primo cluster.

! **Ha cambiato l'ABI** (264 → 268 byte), e `DirEntry` è entrata
nell'impronta di `tools/abi-bersaglio.c` *da questa volta*: `struct dirent`
c'era già, ma quella la costruisce la libc per conto suo — la struttura che
attraversa la syscall è `DirEntry`, ed era fuori.

---

## Provarlo

Sul CD c'è `/prova-make/`. Il CD è in sola lettura e make deve scrivere, quindi
si copia prima:

```
mount hd0p1 /disk
export PATH=/bin:/cdrom/exos/bin:/cdrom/bin
mkdir /disk/pm
cp /cdrom/prova-make/makefile /disk/pm/makefile      (e gli altri quattro file)
cd /disk/pm
make
./prova
```

! **Su ext2, non su FAT.** Su FAT i nomi tornano in maiuscolo, quindi
`$(wildcard *.c)` rende `MAIN.C` e il `$(filter-out main.c,...)` del makefile
non lo toglie più: `main` finirebbe due volte nel link. Non è un difetto di
make né di EX-OS — è cosa succede a un makefile scritto per un filesystem che
distingue le maiuscole quando gira su uno che non le distingue.

! **`export PATH` con `/cdrom/exos/bin` dentro**: `gcc` non sta in
`/cdrom/bin` (da lì non potrebbe funzionare, vedi la regola del CD nel
Makefile). `ar`, `ranlib`, `as` e `ld` stanno in tutti e due.

---

## Costruire FreeBASIC dentro EX-OS

È il motivo per cui esiste tutto questo. Il makefile di FreeBASIC non va
modificato, ma il bersaglio va detto a mano:

    make TARGET_OS=exos TARGET_ARCH=x86 rtlib
    make TARGET_OS=exos TARGET_ARCH=x86 compiler

! **Senza quelle due variabili il makefile chiama `uname`** e non riconosce la
risposta, lasciando `TARGET_OS` vuoto: sceglierebbe la directory sbagliata
sotto `src/rtlib/`. Con esse prende `src/rtlib/exos/`, che è quella che
`tools/freebasic-exos/applica.py` ha creato.

! **Le due righe più larghe del progetto sono qui**: `ar rcs libfb.a` con
~200 oggetti e il link di `fbc` con 145. Sono loro che hanno fatto alzare
`MAX_SPAWN_ARGS` da 64 a 512 nel kernel, l'arena degli argomenti da 4 a 32 KB,
e hanno fatto sì che lo stack iniziale del figlio venga dimensionato sulla riga
di comando vera invece che su una costante (`elf_load_argv`).

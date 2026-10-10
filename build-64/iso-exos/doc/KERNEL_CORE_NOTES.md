# EX-OS — Sottoinsieme "solo kernel"

## Che cosa e' questo kernel: un **minikernel**

Non microkernel — memoria virtuale, scheduler, VFS (FAT12/16/32, ext2,
ISO 9660), caricatore ELF e cache dei blocchi stanno qui dentro. Non
monolitico — i driver di dispositivo sono processi ring3 che parlano per
IPC e non eseguono istruzioni privilegiate.

! **La parola non e' una classificazione, e' un vincolo.** «Monolitico
ibrido» descriverebbe la stessa architettura senza chiedere niente a
nessuno; *minikernel* chiede di restare piccoli, ed e' la ragione per cui
i driver sono finiti fuori uno dopo l'altro.

    build/kernel.bin      184 KB      ~30 000 righe di C e ASM

**La domanda da farsi prima di aggiungere codice qui:** puo' essere un
processo ring3? Quasi sempre si'. Il kernel serve per cio' che richiede il
privilegio o l'arbitraggio fra processi — paginazione, scheduling,
mediazione dell'hardware, confini fra spazi di indirizzamento — e per il
resto c'e' `drivers/`. Il precedente da guardare quando si e' in dubbio e'
l'enumerazione PCI: legge tabelle scritte da firmware di terzi, quindi un
ciclo che non termina dev'essere **un processo da rilanciare, non una
macchina da riavviare**.

> ! **Dal 12 agosto 2026 quella domanda non e' piu' una preferenza.** Che il
> kernel sia oggi ibrido — alcune cose per syscall, altre per server ring3 —
> e' uno stato da RIDURRE, non un permesso ad aggiungerne. Le regole per il
> codice nuovo, insieme ai vincoli su memoria condivisa, pagefile e grafica in
> spazio utente, stanno in **`DIREZIONE.md`**: si legge prima di scrivere.


## ! `paging_map_page()` sovrascrive in silenzio — il confine lo mette chi chiama

E' il contratto, non una dimenticanza: chi chiama sa gia' che quella pagina
e' sua, e un rifiuto costringerebbe ogni chiamante a distinguere fra "non
era mappata" e "era mappata e va bene cosi'". Ma il prezzo va conosciuto:
la vecchia pagina fisica **non viene liberata**, e soprattutto **non c'e'
nessun segnale**. Un errore di calcolo nell'indirizzo non da' un fault ne'
un log: da' due oggetti diversi allo stesso indirizzo virtuale, e il
guasto salta fuori molto dopo, altrove.

**Perche' conta (kernel 0.156).** Lo spazio di un processo e' fatto cosi':

```
0x08000000  testo, dati, bss
            heap ---->
            heap_max          <- il tetto
            pagina di guardia
            blocco TLS (se c'e')
            riserva dello stack (256 KB)   <---- lo stack cresce all'ingiu'
0xbffff000  cima dello stack
```

Fino alla 0.155 `sys_sbrk` cresceva finche' il PMM aveva pagine: l'unico
limite era la RAM fisica, non lo spazio di indirizzamento. Uno heap
abbastanza grande avrebbe rimappato **il blocco TLS del processo stesso**
su pagine azzerate — thread pointer a zero, ogni variabile `__thread` a
leggere memoria altrui, in silenzio.

Ora il confine e' esplicito in `Process.heap_max` (calcolato in `elf.c`,
Passo 7) e lo controllano **tutte e due** le vie di crescita, `sys_sbrk` e
`sys_mmap`. Chi aggiunge una terza via deve controllarlo anche lei: qui non
c'e' niente che glielo ricordi.

! **Il ripiego `heap_start = USER_SPACE_BASE` in `sys_sbrk` non c'e'
piu'.** Era una risposta plausibile e sbagliata: `USER_SPACE_BASE` e'
`0x04000000`, cioe' **sotto** l'indirizzo a cui vengono caricati i
programmi. Chi non ha uno spazio di indirizzamento preparato da `elf_load`
non ha uno heap, e `sbrk` glielo dice con `ENOMEM` invece di indovinare.

## ! Nomi nelle strutture pubbliche: `tipo` e non `type` (dal 2026-08-03)

`IpcMessage` aveva un campo `type`. Portando **openlibm** il codice si e'
fermato su un errore dentro **il nostro** `libc.h`:

```
error: two or more data types in declaration specifiers
```

perche' openlibm fa `#define type float` in `src/math_private.h` — dopo di
che quel campo diventa `float float`.

! **La colpa non e' di openlibm.** Un header che il codice di terzi include
non puo' usare come nome di campo una parola cosi' comune da essere un
candidato ovvio a diventare macro nel codice di chiunque: `type`, `min`,
`max`, `index`, `near`, `far`. Il rimedio corretto e' cambiare il nostro
nome, non chiedere a mezzo mondo di non usare il loro.

Rinominato `type` → **`tipo`** in `lib/include/libc.h`,
`kernel/include/sched.h`, `kernel/include/ipc.h` e `kernel/ipc/ipc.c`
(kernel 0.155). Le due copie di `IpcMessage` **devono restare identiche** —
e' la convenzione del progetto — quindi il nome cambia anche nel kernel,
dove non servirebbe.

! **La regola vale solo per cio' che finisce in `lib/include/`.** I campi
`type` di `FileDescriptor` (`kernel/include/sched.h`) e della mappa E820
restano come sono: quelle strutture non le vede nessun compilatore esterno.

## ! Ambiente di compilazione — Debian 13, GCC 14 (dal 2026-07-31)

I sorgenti sono passati da WSL/Windows 11 a **Debian 13 (trixie)**, dove il
compilatore e' **GCC 14**. Due conseguenze da ricordare, perche' non si
manifestano come "differenza di ambiente" ma come errori del codice:

1. `implicit-function-declaration` e' un **errore**, non piu' un avviso.
   Codice che compilava su GCC <= 13 puo' non compilare qui. E' il caso di
   `lib/libc.c`, che chiamava `main()` senza dichiararlo (corretto).
2. binutils 2.44 emette `warning: LOAD segment with RWX permissions` a ogni
   link. Innocuo per un OS freestanding.

Serve **`mtools`** (`mformat`/`mcopy`/`mmd`/`mattrib`): senza,
`tools/mkfloppy.sh` si ferma e l'immagine non viene prodotta.

**`make` da solo costruisce solo `build/bin/sh`** — e' il primo target del
Makefile. Per la build completa serve **`make all`**.


Estratto da exa_os_2026_05_20_207.zip per lavorare sull'evoluzione verso
driver in userspace senza portarsi dietro l'intero progetto.

## Incluso
- `kernel/`               — tutto il kernel (arch, fs, include, loader, mm, sched, syscall)
- `drivers/tty/`           — compilato STATICAMENTE dentro il kernel (vedi Makefile,
                              target $(BUILD_KERNEL)/tty.o), quindi conta come kernel
- `drivers/floppy/`, `drivers/kbd/` — moduli .so caricati a runtime in ring0 da
                              kernel/loader/drvmgr.c. Non sono "kernel" in senso
                              stretto, ma sono il riferimento del contratto attuale
                              (drv_init/drv_read/drv_write/drv_ioctl/drv_exit) da
                              ridisegnare per farli girare in userspace (ring3)
- `bin/sh/`, `bin/hello/`, `bin/ls/` — shell.c usa SYS_SPAWN/SYS_WAITPID;
                              ls.c è il primo programma esterno che usa la
                              libc invece di reimplementare le syscall a mano
- `lib/`                   — libc.c/libc.h, ora necessari: ls.c ci è
                              compilato/linkato staticamente insieme (vedi
                              regola Makefile "ls", niente link dinamico
                              ancora supportato dal loader per programmi
                              normali)
- `boot/kernel.cfg`        — definisce shell= e modules= al boot
- `Makefile`                — completo (riferimento a bootloader esiste
                              ancora, ma non serve per leggere/modificare il kernel)
- `HANDOFF.md`, `README.md` — contesto di progetto

## Escluso (non necessario per il lavoro sul kernel)
- `bootloader/`  — stage1/stage2, FAT12 reader del bootloader (diverso da
                    kernel/fs/fat12.c, che è il filesystem driver del kernel)
- `tools/`       — script di build/test (install_apt, mkfloppy, test_vbox)

## Punti aperti identificati durante la review (giugno 2026)

### Risolti (in questo pacchetto)

1. **Mancava fork()/spawn()** → Aggiunta `SYS_SPAWN` (numero 2, syscall.h/
   syscall_impl.c/syscall.c). Crea un processo figlio con PID e page
   directory proprie, senza toccare il chiamante (a differenza di
   `sys_exec`, che resta una vera exec() POSIX — sostituisce il
   processo). Il chiamante riceve il PID del figlio e può attendere la
   sua conclusione con `SYS_WAITPID`.

2. **`SYS_WAITPID` esisteva ma non poteva mai svegliarsi**: `proc_exit()`
   non chiamava mai `sched_unblock()` sul genitore in attesa →
   aggiunto. Nota tecnica: non si poteva chiamare `sched_unblock()`
   "pubblica" da dentro `proc_exit()` perché entrambe fanno
   cli/sti grezzi non annidati — riattivare le interrupt a metà di
   `proc_exit()` (prima dello switch di contesto) sarebbe stato
   pericoloso. Aggiunta `sched_unblock_locked()` (variante senza
   cli/sti) usata da entrambi i punti.

3. **`proc_exit()` distruggeva la page directory ATTIVA in CR3** prima
   dello switch di contesto (stesso CR3 ancora in uso). Risolto
   spostando TUTTA la liberazione delle risorse del processo (page
   directory, stack utente, **e finalmente anche lo stack kernel, mai
   liberato in nessun punto prima d'ora — leak pre-esistente**) dentro
   `sys_waitpid()`, nel momento in cui il genitore raccoglie lo zombie:
   a quel punto il processo non è più "current" da un pezzo, quindi è
   sicuro.

4. **`page_fault_handler` chiamava `kpanic()` per QUALSIASI fault**,
   anche da ring3. Ora un fault da un processo utente termina solo
   quel processo (via `proc_exit(-11)`, stile SIGSEGV); `kpanic()`
   resta riservato ai fault originati in ring0 (bug reale del kernel).
   Step necessario prima di mettere driver in userspace: un driver
   buggy non deve poter affondare il kernel.

Tutti e 4 i file toccati (`kernel/sched/sched.c`,
`kernel/syscall/syscall_impl.c`, `kernel/syscall/syscall.c`,
`kernel/mm/paging.c`, più l'header `kernel/include/syscall.h`)
compilano senza warning con `-Wall -Wextra -m32`.

**Limite noto, volutamente fuori scope per questo batch**: se un
processo termina e il suo genitore non chiama mai `waitpid()` (es. un
servizio/driver in background mai atteso esplicitamente, o un genitore
che muore prima), le sue risorse restano "zombie" non riOccupate
per sempre — manca ancora un reaper/init che adotti gli orfani. Da
affrontare quando si arriverà alla supervisione dei driver/servizi
userspace.

**Risolto anche `bin/sh/shell.c`**: `run_program()` (usata per ogni comando
non built-in digitato) ora chiama `sh_spawn()`+`sh_waitpid()` invece di
`sh_exec()` — la shell lancia un task autonomo e attende la sua fine,
restando viva. Il built-in esplicito `exec` mantiene invece la vecchia
semantica di sostituzione vera (nuova funzione `run_program_replace()`,
via `sh_exec`/`SYS_EXEC`) — comportamento intenzionale, come l'`exec`
builtin di bash: chi lo digita sa che la shell corrente finisce lì.
Compilato e verificato con i flag utente del progetto (`-m32
-ffreestanding ... -Wall -O2`), zero warning. File incluso ora in
`bin/sh/shell.c` in questo pacchetto, insieme a `bin/hello/hello.c` come
riferimento (hello.c non ha richiesto modifiche: il bug era tutto nella
shell/scheduler, non nel programma).

### ! Regressione introdotta dal fix sopra, trovata testando e corretta

Il primo giro di fix (punti 1-4) ha introdotto un bug nuovo, scoperto
provando `hello` su build reale: **KERNEL PANIC, page fault in ring0 a
un indirizzo 16 byte sotto l'inizio dello stack utente del processo**.

Causa: sia `sys_waitpid()` sia il percorso di fallimento di `sys_spawn()`
chiamavano `kfree((void*)p->user_stack_base)`. Ma `user_stack_base` **non
è un puntatore kmalloc** — è un indirizzo virtuale fisso
(`USER_SPACE_END - PAGE_SIZE - USER_STACK_SIZE`) mappato da `elf_load()`
con `pmm_alloc_page()`+`paging_map_page()` nella PD del FIGLIO, valido
solo quando quella PD è attiva in CR3 (qui non lo è mai: il genitore
gira con la propria PD). `kfree()` ci legge/scrive sopra metadati
dell'heap a un indirizzo che lì non significa nulla → fault.

Fix, alla radice invece che solo togliere la kfree sbagliata:
- `paging_destroy_directory()` ora libera anche le pagine fisiche di
  DATI mappate da ogni page table dello spazio utente (codice, dati,
  heap, stack), non solo le page table stesse — prima il commento nel
  codice diceva esplicitamente che questo "lo fa il VMM/exec", ma in
  pratica non lo faceva **nessuno**, leak pre-esistente più ampio,
  risolto come effetto collaterale.
- Rimosse le `kfree(user_stack_base)` ora superflue da `sys_waitpid()` e
  `sys_spawn()`.
- Trovato e corretto un leak collegato in `proc_create()`: allocava uno
  stack utente "placeholder" da 64KB via `kmalloc()` che `elf_load()`
  sovrascriveva sempre subito dopo — 64KB persi a ogni processo creato.
  Rimossa l'allocazione inutile (`user_stack_base`/`top` partono a 0,
  `elf_load()` li imposta sempre prima che il processo possa girare).

Ricompila pulito con `-Wall -Wextra -m32`.

### SYS_READDIR + /bin/ls (giugno 2026)

- `kernel/fs/fat12.c`/`.h`: sostituita `fat12_readdir()` (solo root,
  restituiva un puntatore interno) con `fat12_readdir_path()` — elenca
  root O una subdirectory di un livello (stessa limitazione di
  `fat12_find_path()` già esistente), **copia** sempre in un buffer
  fornito dal chiamante invece di esporre `g_root_dir` direttamente.
  Aggiunta `fat12_format_name()` per convertire il nome 8.3 raw in
  stringa leggibile.
- `kernel/include/syscall.h` / `syscall_impl.c` / `syscall.c`: nuova
  `SYS_READDIR` (141). Copia sempre attraverso un buffer kernel
  intermedio (mai esporre puntatori interni allo userspace), limite di
  sicurezza a 64 entry per chiamata indipendente da quanto richiesto.
  Struct `DirEntry` (name/size/is_dir) condivisa "a mano" fra kernel e
  libc (stessa convenzione di duplicazione già in uso nel progetto per i
  numeri di syscall fra kernel e bin/sh/shell.c).
- `lib/libc.c`/`.h`: aggiunta `listdir()` (non è la tripletta POSIX
  opendir/readdir/closedir — un'unica chiamata, niente handle).
- `bin/ls/ls.c` + `bin/ls/ls.ld`: primo programma esterno che usa la
  libc invece di reimplementare le syscall a mano (come hello/shell
  fanno). **Linkato staticamente** con `lib/libc.c` (vedi nuova regola
  Makefile "ls") — il loader ELF non supporta ancora link dinamico
  runtime per programmi normali (`PT_DYNAMIC` definito in `elf.c` ma mai
  elaborato; quel meccanismo esiste solo per i driver via
  `kernel/loader/dynlink.c`). Aggiunto anche a `make all`.
- ~~**Limite noto**: niente argv ancora — `ls` elenca sempre `getcwd()`.~~
  **SUPERATO**: `sys_spawn` costruisce lo stack iniziale con argc/argv
  (vedi il commento sul layout in `syscall_impl.c`), `run_program()` in
  shell.c passa gli argomenti, `lib/start.S` li legge e `ls.c` usa
  `argv[1]`. Verificato il 2026-07-30: `ls /bin` elenca `/bin`.
- Compilato/linkato e verificato (gcc -m32 -Wall -Wextra per il kernel,
  link ELF32 statico riuscito per ls — stesso warning RWX benigno che ha
  già anche `shell`, non specifico di ls).

### Fix hardware reale: timing FDC (giugno 2026)

Bug riportato dall'utente su Pentium II MMX reale (192MB RAM): boot
funzionante in QEMU/VirtualBox, ma `[FAULT] PID 1 'shell': page fault a
0x00000000 (protezione, lettura, EIP=0x00000000)` su hardware reale —
**non un vero kernel panic**: il nostro gestore fault-da-ring3 ha isolato
correttamente il problema (la shell e' stata terminata invece di far
cadere tutto il kernel — conferma che quel fix precedente funziona).

Causa reale: `kernel/fs/fat12.c`, i comandi FDC (motor on, seek,
recalibrate) usavano **loop di NOP a conteggio fisso** invece di attese
basate sul tempo reale:
```c
for (d = 0; d < 5000000; d++) __asm__ volatile("nop");  // "seek settling"
for (i = 0; i < 10000000; i++) __asm__ volatile("nop"); // "~300ms" motor
```
Il numero di iterazioni necessario dipende dalla velocita' della CPU —
tarato (implicitamente) sulla CPU virtuale di QEMU/VirtualBox. Su un
Pentium II reale, con timing completamente diverso, il floppy reale
(motore meccanico, testina) puo' non essere ancora pronto quando parte
il comando, e l'FDC puo' restituire dati sbagliati **senza segnalare un
errore I/O** — compatibile al 100% con: ELF della shell letto come
tutto-zero, entry_point=0, fault a EIP=0x00000000 appena lo scheduler ci
salta dentro.

Fix:
- Nuova `fdc_delay_ms()` basata su `g_ticks` (PIT a 100Hz, kernel/sched/
  sched.c — tempo reale, indipendente dalla CPU; richiede interrupt
  abilitati, sempre vero per fat12_init()/letture, che girano dopo
  [PASSO 12] in kernel_main.c). Sostituisce i tre loop NOP in
  `fdc_recalibrate`, `fdc_seek`, `fdc_motor_on`.
- Bug collegato in `fat12_read_sector()`: se la lettura dei byte di
  stato post-comando falliva (`fdc_recv_byte` in timeout), `st0`
  restava **non inizializzato** ma veniva comunque controllato
  (`if (st0 & 0xC0)`) — innocuo per coincidenza quando lo stack e'
  zero-ish (frequente in emulazione), indeterministico su hardware
  reale. Fix: su fallimento della status phase si esce subito con
  errore, senza usare `st0` non garantito.
- ~~**Non ancora fatto**: il driver FDC non si sincronizza mai su IRQ6.~~
  **FATTO il 2026-07-30**, vedi la sezione dedicata più sotto — e ha
  scoperto altri tre difetti della stessa famiglia.
- Compilato pulito (`-Wall -Wextra -m32`), zero warning nuovi (resta
  solo `fdc_seek` "defined but not used", preesistente, non collegato).
- **Non testato su hardware reale da me** (nessun Pentium II/QEMU/VBox
  disponibile in questo ambiente) — da verificare alla prossima build.

### Il thread pointer — variabili `__thread` (2026-08-02, 0.154)

Modello **local-exec, variante II** (quella di i386), che e' quello dei
binari statici. Il caricatore trova `PT_TLS`, ne fa una copia per processo
sotto la riserva dello stack (con una **pagina di guardia** in mezzo), e ci
mette in coda un TCB di 8 byte che comincia con un puntatore a se stesso —
la convenzione ABI che rende `mov %gs:0x0,%ebx` una lettura del thread
pointer. Le variabili stanno a offset negativi da li', gia' risolti da `ld`
(`R_386_TLS_LE`): a runtime non c'e' niente da rilocare.

Il descrittore GDT numero 6 (selettore `0x33`) e' User Data con una base
che cambia: `gdt_set_tls_base()` la riscrive a ogni switch, accanto a
`gdt_set_kernel_stack()`. ! **Con base zero e' indistinguibile da `0x23`**,
quindi i processi senza variabili thread-local non pagano niente.

! **Gli stub degli interrupt non toccano piu' GS.** Salvavano solo DS e
all'uscita rimettevano quel valore anche in ES, FS e GS: il primo tick di
timer avrebbe cancellato il thread pointer, e il guasto sarebbe comparso a
caso. Il kernel non usa GS — niente percpu, niente stack canary — quindi
lasciarlo con il valore dell'utente non espone niente, e `context_switch`,
che GS lo salva e ripristina gia', diventa da solo il meccanismo che lo
rende per-processo.

! **`.tdata` e `.tbss` vanno nominate nei linker script**, tutti e
diciotto: senza, `ld` le sistema dove capita e `PT_TLS` puo' non essere
generato affatto — il programma compila, si collega e legge una variabile
che sta da un'altra parte.

! **Non c'e' il TLS dinamico** (`__tls_get_addr`, general-dynamic,
local-dynamic, `__thread` dentro una libreria condivisa): serve a chi
carica codice a runtime, e qui i binari sono statici.

### Scrittura FAT12 alla posizione giusta (2026-08-02, 0.153)

Due difetti che erano li' da sempre e che **si vedono solo tornando
indietro** in un file. Fino ad agosto 2026 ogni programma di EX-OS
scriveva dall'inizio alla fine; il primo che non lo fa e' `as` di
binutils, che scrive le sezioni e poi si riposiziona a zero per
l'intestazione ELF.

1. **`vfs_write_nl` non passava l'offset a `fat12_write`** — «fat12 tiene
   la propria posizione» — e quella scriveva sempre da
   `entry->file_size`, cioe' in coda. Il file usciva con i pezzi giusti
   nell'ordine di scrittura e il magic `\x7fELF` a offset 240.
   ! Su ext2 e FAT16/32 non succedeva: li' l'offset arrivava gia'.
2. **`if (nuovo || in == 0)` azzerava il settore intero.** `in == 0`
   significa «scrivo dall'inizio del settore», non «il settore e' vuoto»:
   i 52 byte dell'intestazione ELF cancellavano i 460 di sezioni che gli
   stavano dietro. La condizione giusta e' «il settore sta oltre la fine
   attuale del file».

! Resta scoperto il caso del **buco**: una scrittura che comincia oltre
la fine del file lascia i byte in mezzo come stavano sul disco invece di
farli leggere come zeri.

### dup/dup2/fcntl, e i file che nessuno chiudeva (2026-08-02, 0.151)

`VfsFile` ha un **conteggio dei riferimenti** (`rif`): `vfs_close` scala e
chiude davvero solo l'ultima volta, `vfs_dup` incrementa. E' quello che
mancava alla 0.150 — sta scritto nella sezione qui sotto — e senza non si
poteva avere `dup()`. Sopra ci stanno tre syscall con i numeri di Linux:
**41 `dup`**, **63 `dup2`**, **55 `fcntl`** (`F_DUPFD`, `F_GETFD`,
`F_SETFD`, `F_GETFL`, `F_SETFL`).

Le chiede il codice di terzi: `ar`, `objcopy` e `arsup` di binutils fanno
tutti `fd = dup(fd)` per tenere aperto un file oltre la `close()` di chi
possedeva l'originale.

! **I due descrittori condividono il FILE, non la POSIZIONE**, e su POSIX
condividerebbero anche quella. L'offset sta in `FileDescriptor`, non in un
oggetto «file aperto» intermedio; metterlo in comune vorrebbe dire
spostarlo in `VfsFile` e quindi cambiare `vfs_read`/`vfs_write` e la
gestione della posizione di `fat12.c`, che la tiene per conto suo. Il
giorno che arrivera' una pipe, questa e' la prima cosa da sistemare.

! **`dup2` e' l'unico modo di sostituire stdin/stdout/stderr**: `close()`
su 0, 1 e 2 e' rifiutata apposta — lascerebbe il processo senza uscita —
mentre chi arriva da `dup2` il rimpiazzo ce l'ha gia'.

! **I descrittori aperti alla terminazione non venivano chiusi da
nessuno.** `proc_reap_zombie` chiudeva `exe_handle` e basta: uno slot del
VFS perso per ogni file lasciato aperto, e dopo `VFS_MAX_OPEN` volte un
`open()` che risponde `EMFILE` senza che nessuno stia tenendo aperto
niente. Ora si chiudono li' — e non in `sys_exit`, perche' **un processo
terminato da un fault non passa da `sys_exit`**, e sono proprio quelli che
i file li lasciano aperti.

### spawn con ambiente e redirezioni (2026-08-02, 0.150)

Serve al driver di un compilatore: `xgcc` non compila niente, lancia `cc1`,
`as` e `ld` e ne raccoglie l'uscita. Mancavano tre cose, e ora ci sono:

- **redirezione dei descrittori**: il figlio puo' nascere con un fd
  qualunque agganciato a un file. Per **percorso**, non per descrittore
  gia' aperto del padre — passare un fd vorrebbe dire due processi sullo
  stesso handle VFS, cioe' un conteggio di riferimenti che non c'e' e una
  `close()` che sfila il file da sotto all'altro. Basta a `gcc`; non basta
  alle pipe, che infatti non ci sono.
- **ambiente per processo**: `envp` viaggia come `argv`, copiato sullo
  stack del figlio. `environ`, `putenv`, `setenv`, `unsetenv` in libc.
- **`MAX_FD` da 16 a 32**, `VFS_MAX_OPEN` da 48 a 64.

! **L'estensione passa da un blocco con una PAROLA MAGICA in ESI, non da
due argomenti in piu'.** La forma storica e' `spawn(percorso, argc, argv)`
e in giro ci sono binari — anche gia' installati su un disco — che la
chiamano con tre registri: per loro ESI contiene spazzatura. Leggerlo come
puntatore significherebbe che un programma vecchio, su un kernel nuovo,
apre file a caso. Con la magia, o il blocco e' quello giusto o non esiste.

! **Due macro con il nome sbagliato, e il test che le ha trovate.** In
`lib/libc.c` c'erano `S_ISDIR`/`S_ISREG` definite sull'**attributo FAT**
(`& 0x10`) mentre le stesse macro, nello stesso file piu' sotto, andavano
definite sul `st_mode` POSIX. Nessuno le usava, finche' `opendir()` non ha
scritto la riga piu' naturale del mondo — `S_ISDIR(st.st_mode)` — e si e'
presa la prima: `0040755 & 0x10` fa zero, quindi `opendir("/")` rispondeva
«non e' una directory». Per l'attributo FAT esiste `EXOS_ATTR_DIR()`, che
si chiama come cio' che fa.

### Caricamento su richiesta + il lucchetto del VFS (2026-08-02, 0.149)

`elf_load` non copia piu' i segmenti in RAM: annota dove vivono nel file
(`Process.vma`), tiene l'eseguibile **aperto** (`Process.exe_handle`) e le
pagine arrivano al primo accesso, da `pf_carica_da_file()` nel gestore di
page fault. Un binario con **8 MB di `.rodata`** parte occupando 36 KB e
sale a 8 MB solo se lo si legge tutto — verificato byte per byte.

Il costo d'avvio smette di dipendere dalla dimensione del binario. Senza
questo, ospitare un compilatore vuol dire impegnare decine di MB prima
della prima istruzione.

! **I driver si caricano RESIDENTI** (`elf_load_residente`). Un driver che
serve il filesystem e che venisse paginato *da* quel filesystem dovrebbe
servire la propria lettura mentre e' fermo ad aspettarla. Sono due file da
~15 KB: non c'e' niente da risparmiare e c'e' un blocco da evitare.

Il gestore di #PF **riaccende gli interrupt** per la sola `vfs_read` (il
gate e' un interrupt gate, e leggere dal disco richiede il timer) e
ripristina lo stato di prima. Il buffer di lettura sta sullo stack kernel,
non e' uno statico: due processi possono essere qui dentro insieme.

#### Le tre cose che sono venute fuori facendolo funzionare

1. **I driver di filesystem non sono rientranti.** ext2 lavora su cinque
   buffer globali, e quella separazione presuppone un'operazione alla
   volta. Con il fault-in gli intrecci sono diventati la norma (al PASSO
   15 il kernel carica la shell della console 1 mentre quella della 0 gia'
   gira e fa fault), e il sintomo non diceva "concorrenza":
   `EXT2: blocco 0 fuori dal volume`, poi `ATA: timeout DRQ`. Rimedio: un
   lucchetto nel VFS, un'operazione per volta.
2. ! **Chi tiene il lucchetto non deve poter faultare.** Una `read()` il
   cui buffer cade in una pagina non ancora presente fa scrivere il driver
   dentro quella pagina, con il lucchetto in mano: il fault chiama il VFS
   e aspetta se stesso. `vm_precarica_utente()` porta in RAM le pagine del
   buffer PRIMA di entrare nel VFS (chiamata da `sys_read`/`sys_write`).
   Stessa trappola, versione interna: tre punti del VFS chiamavano il
   *guscio pubblico* `vfs_stat` invece dell'implementazione — un lucchetto
   preso due volte dallo stesso processo.
3. ! **Inversione di priorita', e il sistema si fermava senza dire
   niente.** La prima versione dell'attesa cedeva la CPU in un ciclo.
   `kernel_main` gira nel contesto del task **IDLE**: al PASSO 15 e' il
   processo meno prioritario a tenere il lucchetto mentre carica le shell.
   Appena una shell diventa eseguibile e comincia a cedere-e-riprovare a
   priorita' NORMAL, l'idle non viene piu' scelto — non finisce, non
   rilascia, e tutti riprovano per sempre. Schermo fermo al banner, zero
   messaggi, intermittente. Ora chi aspetta si **blocca** (esce dalla coda
   dei pronti) e viene svegliato al rilascio.

`VFS_MAX_OPEN` e' passato da 24 a 48 e `MAX_OPEN_FILES` di fat12 da 16 a
48: ogni processo tiene ora aperto il proprio eseguibile per tutta la
vita, quindi il tetto non conta piu' i soli file aperti dai programmi.

### ! Un processo non poteva crescere oltre 4 MB, con qualunque RAM (2026-08-02, 0.148)

Il sintomo: un programma che cresce lo heap 1 MB per volta arriva a 4 MB e
la macchina va in **kernel panic**, `PAGE FAULT (KERNEL)` all'indirizzo
`0x00800000` — cioe' esattamente il vecchio `USER_SPACE_BASE`. Con 32 MB o
con 4 GB installati non cambiava niente: il tetto non era la RAM.

**La causa, in due righe che si ignoravano a vicenda.**
`paging_create_directory()` copia nella PD di un processo solo le PDE sotto
`USER_SPACE_BASE`; il mapping identita' di TUTTA la RAM vive nelle PDE
successive della sola `kernel_page_directory`. Ma `sys_sbrk`, `sys_mmap`,
`elf_load` e `pf_cresci_stack` azzeravano e riempivano le pagine appena
allocate **dereferenziandone l'indirizzo fisico**, mentre era caricato il
CR3 dell'utente. Finche' il PMM (next-fit dal basso) consegnava pagine
sotto la soglia funzionava; la prima oltre era un fault in ring0.

**Il rimedio, in tre pezzi.**

1. **Finestra di rimappatura fisica** (`paging_finestra_apri/chiudi`,
   `paging_azzera_fisica`). Una PTE dentro `kernel_page_table_low` — la
   tabella statica installata in PDE[0], quindi presente in *ogni* spazio
   di indirizzamento — che il kernel ripunta alla pagina fisica che deve
   toccare. E' il `kmap_atomic`/fixmap di Linux ridotto all'osso. Due
   regole, entrambe nel codice: interrupt spenti mentre e' aperta (e'
   una risorsa sola), e mai tenuta aperta attraverso una chiamata che si
   blocca — in `elf_load` fra una pagina e l'altra c'e' una `vfs_read`,
   che e' IPC verso un driver in ring3. Aprirla due volte e' `kpanic`:
   sarebbe corruzione silenziosa.
2. **Fascia kernel esplicita** (`pmm_alloc_page_kernel`,
   `pmm_alloc_pages_kernel`). Tutto cio' che il kernel raggiunge al
   proprio indirizzo fisico — heap di kmalloc, stack kernel, page
   directory e page table, immagini dei driver — viene ora allocato
   sotto `USER_SPACE_BASE` per costruzione, non per fortuna.
   `USER_SPACE_BASE` e' salita da 8 a 64 MB: 64 processi × 128 KB di
   stack kernel fanno 8 MB tondi, cioe' la vecchia soglia intera.
3. ! **`pmm_alloc_page()` serve la fascia per ULTIMA.** Senza questa
   preferenza il primo programma affamato consuma la fascia dal basso e
   subito dopo il kernel non trova una pagina per una page table. Visto
   davvero, ed e' il guasto piu' ingannevole dei tre: *«fascia kernel
   esaurita: 111913 pagine libere altrove»*, con 437 MB liberi.

! **Il caso in cui sbagliare costa di piu' e si diagnostica di meno** e'
lo **stack kernel** di un processo: lo usa la CPU al suo indirizzo fisico
(TSS.ESP0) nell'istante in cui arriva un interrupt mentre gira quel
processo. Sopra la soglia, il fault avverrebbe durante la commutazione di
stack — e il gestore, per gestirlo, avrebbe bisogno dello stesso stack.
Triplo fault, riavvio, nessuna riga di log.

Il driver dei moduli (`dynlink.c`, `drvmgr.c`) resta l'eccezione
dichiarata: rilocando `R_386_COPY` legge da una pagina e scrive in
un'altra nello stesso istante, e la finestra e' una sola. Le immagini dei
driver stanno percio' nella fascia kernel — sono due, di ~15 KB.

**Verifica**: 300 MB allocati e riletti byte per byte su una macchina da
512 MB (prima: 4 MB), due volte di fila, con la memoria libera che torna
al valore esatto di partenza. Su 32 MB il programma arriva a 29 MB e
fallisce con un ENOMEM pulito invece che con un panic.

**Cosa NON risolve**: il caricatore ELF resta *eager* — nessun demand
paging da file — quindi far partire un binario da 40 MB significa
impegnare 40 MB prima che esegua. E' il prossimo pezzo sulla strada di un
compilatore ospitato.

### ! Lo slot di un file aperto si prenotava troppo tardi (2026-08-02, 0.147)

`vfs_open()` sceglieva lo slot libero in `g_file[]`, poi parlava con il
driver del filesystem, e **solo alla fine** lo marcava `usato`. Fra le due
cose c'e' una lettura di directory, e leggere una directory significa un
messaggio IPC a un driver in ring3: un punto di riscadenzamento. Un
secondo processo entrato in quella finestra trovava lo stesso slot ancora
libero e lo prendeva.

Il guasto in cronaca: `/bin/libctest`, lanciato come shell su quattro
console, apriva un proprio file temporaneo nell'istante in cui il PASSO 15
caricava lo stesso ELF per le altre console. Lo slot era uno solo per due
file: `fseek(SEEK_END)` rispondeva **31640 byte** (la dimensione del
binario), `fgetc()` ritornava **0x7F** (la prima lettera di `\x7fELF`), e
al primo `fclose()` del programma il caricatore si trovava l'handle chiuso
sotto i piedi — «ELF: impossibile leggere program headers» su due console
su quattro, a ogni avvio.

Nessuna delle due meta' del danno somigliava alla causa, ed e' la cosa da
ricordare: **una tabella condivisa si prenota prima di bloccarsi, non
dopo.** Ora `usato`, `im` e `interno` si scrivono subito dopo la scelta
dello slot — insieme, perche' `vfs_umount` scorre quei campi cercando file
aperti e ha diritto di trovarli coerenti — e ogni uscita d'errore disfa la
prenotazione (`apri_fallito`), altrimenti dopo `VFS_MAX_OPEN` aperture
fallite non si aprirebbe piu' niente.

`fat12_open()` aveva **lo stesso difetto un piano piu' sotto** (stessa
forma, stesso rimedio, `fat12_open_fallito`): trovarne uno e non l'altro
avrebbe spostato la corsa invece di toglierla.

### FPU x87 — kernel/arch/x86/fpu.c (2026-08-02, 0.147)

Il kernel non usa la virgola mobile e non ne aveva bisogno; i programmi
ring3 si': una `strtod()` serve a chiunque legga un numero da un file, a
cominciare da un compilatore. Senza inizializzazione, la prima istruzione
x87 di un processo prende un'eccezione; **senza salvataggio dello stato,
due processi che fanno conti si sovrascrivono i registri a vicenda** — e
non con un errore, con un risultato sbagliato.

- **PASSO 7b**, dopo l'IDT e non prima: il rilevamento imposta `CR0.NE=1`
  ("gli errori aritmetici arrivano come #MF"), e #MF ha senso solo con un
  gate installato. Rilevamento con `FNINIT`+`FNSTSW` su una variabile
  precaricata: sono le due sole istruzioni x87 che non aspettano il
  coprocessore, quindi su una macchina senza 387 non restano appese. Se
  non c'e', `CR0.EM=1` trasforma le istruzioni x87 in #NM invece di
  lasciar produrre numeri inventati.
- **108 byte nel PCB** (`Process.fpu_state`, allineati a 16 per il giorno
  in cui `FNSAVE` lasciasse il posto a `FXSAVE`). Non stanno in
  `CpuContext`: quello descrive lo stack costruito da `pushad`, che
  l'assembly di `context_switch` legge per posizione.
- `sched_switch_to` fa `fpu_save(prev)` e `fpu_restore(next)` **sempre**,
  non con lo switch pigro (CR0.TS + handler #NM). A 100 Hz sono ~200 cicli
  ogni 10 ms, e in cambio non esiste un "proprietario della FPU" da
  aggiornare quando un processo muore — che e' il modo in cui lo switch
  pigro si sbaglia.
- ! Zero **non** e' uno stato valido per `FRSTOR`: parola di controllo a
  zero significa tutte le eccezioni smascherate, e i tag dicono che tutti
  e otto i registri contengono un valore buono. Per questo `proc_create`
  chiama `fpu_init_state()`, che copia il modello salvato dopo `FNINIT`,
  invece di fidarsi del PCB azzerato.

! **Trappola per chi scrive programmi, non per il kernel.** GCC valuta le
costanti in virgola mobile alla precisione del coprocessore (64 bit di
mantissa), quindi `x == 0.025` compila in un confronto con una costante
caricata da `FLDT` ed e' **falso** anche quando `x` e' il double piu'
vicino a 0.025. Il valore atteso va messo in una variabile `double`, che
forza l'arrotondamento a 53 bit. Costato un giro di diagnosi in
`/bin/libctest`, dove sembrava un difetto di `strtod`.

### stat e lseek(SEEK_END) esistevano solo di nome (2026-08-01, 0.146)

`SYS_STAT` (106) non era **mai stata implementata**: aveva un TODO "Fase 3"
e ritornava ENOSYS. `SYS_LSEEK` (19) rispondeva ENOSYS su SEEK_END, con lo
stesso TODO. Non se ne accorgeva nessuno perche' nessun programma di /bin
le usava — ma sono le due su cui si appoggia qualunque `FILE*` per
misurare un file, e senza di loro una libc ospitata non si puo' scrivere.

Ora entrambe passano dal VFS, quindi valgono su FAT12, FAT16/32, ext2 e
ISO 9660 senza casi particolari.

Per SEEK_END serviva la dimensione di un file **gia' aperto**: e' nato
`vfs_fstat(handle)`. Ripassare dal percorso conservato nel descrittore
sarebbe stato piu' semplice e sbagliato — un file rinominato o cancellato
mentre e' aperto darebbe la dimensione di un altro file, o nessuna, e la
posizione di scrittura verrebbe calcolata su quel numero. Il corpo di
`vfs_stat` e' stato estratto in `stat_interno()`, condiviso dai due:
un secondo elenco dei driver sarebbe un elenco da aggiornare due volte
ogni volta che si aggiunge un filesystem.

Corretto anche SEEK_CUR con offset negativo: sotto zero diventava una
posizione enorme (interi senza segno), e la lettura successiva falliva con
un errore che non assomigliava alla causa.

! `Stat.st_first_clus` resta 0 fuori da una FAT, ed e' voluto: e' un
numero che ha senso solo dentro quel formato. Gli attributi usano le
convenzioni FAT (0x10 directory, 0x01 sola lettura) su tutti i filesystem,
perche' sono quelle che i programmi gia' interpretano.

### CD/DVD — kernel/block/atapi.c + kernel/fs/iso9660.c (2026-08-01, 0.145)

Un lettore ottico sta sugli STESSI canali IDE del disco e ne usa gli
STESSI registri, ma non prende comandi ATA: prende un PACCHETTO di 12 byte
(un comando SCSI) consegnato attraverso il registro dati. Il registro che
su un disco contiene l'LBA, qui contiene quanti byte il dispositivo
consegna per ogni DRQ.

Conseguenze sull'organizzazione del codice:

- gli helper di temporizzazione di `ata.c` (ritardo di 400 ns, attese
  ancorate al PIT, selezione dell'unita') sono stati **esportati** in
  `ata.h` invece che duplicati: cambiano i comandi, non il bus;
- `ata_attendi_drq()` ha una variante **muta** (`ata_attendi_drq_muto`):
  su ATAPI un ERR e' spesso "vassoio vuoto", cioe' una risposta e non un
  guasto, e stamparlo riempirebbe il log a ogni sondaggio;
- la fase dati NON si conta a settori come su ATA: il dispositivo consegna
  raffiche di lunghezza variabile, dichiarata in LBA1/LBA2 prima di
  ognuna. Si cicla finche' DRQ si abbassa, e i byte in eccesso vanno letti
  e buttati — fermarsi a meta' lascia il canale inutilizzabile anche per
  il disco rigido che ci sta accanto.

**Il blocco e' da 2048 byte**: la traduzione da/verso i settori da 512 sta
solo in `blk_read()` (`cd_read`). Chiedere il settore 3 significa chiedere
il blocco 0 e prenderne l'ultimo quarto; senza traduzione si leggerebbe il
blocco 3, 6 KB piu' in la', senza alcun errore.

**La capacita' e' del SUPPORTO, non del dispositivo** — da qui
`blk_supporto()` (blk.h), che sonda e aggiorna la finestra e su un
dispositivo non rimovibile risponde 1 senza toccare niente. Un `cd0` con
zero settori e' un lettore vuoto o non ancora sondato, non un guasto.

! **Il caso che non si indovina**: un lettore appena rifornito risponde
MEDIUM NOT PRESENT (sense 2 / ASC 0x3A) ancora per un comando o due prima
di ammettere il cambio con UNIT ATTENTION; e un vassoio APERTO da' la
stessa risposta anche con un disco dentro. Non tutti i lettori
distinguono i due casi con ASCQ 0x02 — QEMU risponde 0x00 in entrambi.
`atapi_supporto()` quindi chiude il vassoio una volta sola (come Linux
all'apertura di un lettore) e insiste altre due volte a 250 ms prima di
dichiarare l'assenza.

`kernel/fs/iso9660.c` e' in sola lettura per proprieta' del FORMATO: ISO
9660 non ha bitmap di liberi ne' voci riutilizzabili. Preferisce l'albero
**Joliet** quando c'e' (i nomi veri, in UCS-2) al posto dei nomi ISO
maiuscoli e troncati: sono due catene di directory separate che puntano
agli stessi dati, non due viste della stessa. Trappole del formato
elencate in testa al file; le due che costano di piu' sono i numeri
scritti due volte (little **e** big endian di seguito: un campo a 32 bit
occupa 8 byte) e il byte di lunghezza a zero, che significa "salta al
blocco successivo" e NON "fine della directory".

Prova senza masterizzare: `tools/mkiso.py` (con `--senza-joliet` per il
ramo dei nomi ISO puri).

### Avvio da disco e installatore — kernel/boot/bootinst.c (2026-07-31, 0.134)

! **L'INSTALLAZIONE DELL'AVVIO STA NEL KERNEL, E NON E' PIGRIZIA.** E' il
solo punto in cui EX-OS scrive fuori da un filesystem. Una syscall
"scrivi settore grezzo", una volta esistita, permette a QUALUNQUE
programma di sovrascrivere la tabella delle partizioni: nessun controllo
dentro /bin/install lo impedirebbe, basta non usare /bin/install.

Le due invarianti che il kernel garantisce e nessuno puo' aggirare:

1. **dell'MBR si riscrivono SOLO i byte 0..445.** I 64 byte della tabella
   non si toccano mai, tranne il singolo byte del flag "attiva".
2. **del settore di avvio di partizione si riscrive tutto TRANNE 3..89**,
   che sono il BPB: si rilegge dal disco e si rimette. Senza questo,
   installare l'avvio rende ILLEGGIBILE il volume che si sta cercando di
   rendere avviabile.

**La mappa di settori invece di un lettore FAT.** In 512 byte, tolti BPB e
firma, restano ~320: FAT12 ci sta a fatica, FAT32 no — si otterrebbe un
avvio che funziona su FAT16 e non su FAT32. L'installatore gira dentro
EX-OS, dove il driver FAT c'e' gia': trova i file, ne VERIFICA LA
CONTIGUITA' e scrive LBA + lunghezza nell'area di patch a offset 0x1A0.

! **Prezzo della mappa: ricopiare kernel o Stage 2 sul disco OBBLIGA a
rieseguire `install`.** Stesso patto di LILO. E' il motivo per cui
fat_estensione() RIFIUTA i file frammentati invece di seguirne la catena:
una mappa che comprende cluster altrui darebbe un kernel mescolato a dati
di altri, cioe' un avvio che fallisce in modo incomprensibile.

! **LA PARTIZIONE ATTIVA VA SCELTA CON LO STESSO CRITERIO IN DUE POSTI.**
L'MBR (bootloader/mbr/mbr.asm) e vfs_init() devono individuare la STESSA
partizione: se il kernel ne montasse un'altra come root, `/` non
conterrebbe il sistema che sta girando. Entrambi leggono il settore 0 e
confrontano l'LBA di partenza — non l'indice, perche' la voce N della
tabella non e' la partizione N in ordine di disco.

**Contratto a tre**: l'offset 0x1A0 dell'area di patch e la magia 'EXHD'
sono condivisi da bootloader/stage1hd/boothd.asm, kernel/boot/bootinst.c e
bootloader/stage2/loader.asm. Se cambia in uno e non negli altri, l'unico
sintomo e' un sistema che non parte.

**Un bivio in Stage 2, non due Stage 2.** E820, A20, GDT, modo protetto e
copia a 0x100000 sono identici fra floppy e disco: duplicarli
significherebbe correggere ogni bug futuro in due posti e accorgersi di
averne corretto uno solo al prossimo avvio da floppy.

### ! Ordine di inizializzazione cambiato (0.134)

`ata_init()` + `blk_init()` vanno ora PRIMA di `vfs_init()`: la root puo'
essere una partizione ATA, e per montarla i dispositivi a blocchi devono
gia' esistere. Di conseguenza `cfg.c` e `drvmgr.c` sono passati a `vfs_*`
— avviando da disco, kernel.cfg e i driver stanno sul disco.

**L'FDC non si sonda se non si e' avviati da floppy**: costava dodici
righe di ERROR/WARN a ogni avvio da disco, con cinque ritentativi e attese
reali. Un log che sembra rotto mentre il sistema funziona e' il modo
migliore per non accorgersi dell'errore vero il giorno che arriva.
Conseguenza nota: **avviando da disco il floppy non e' raggiungibile**
finche' fat12_init() non sara' separata in "inizializza l'FDC" e "monta".

### Scrittura FAT12/16/32 — kernel/fs/fat.c (2026-07-31, 0.133)

! **LA CACHE E' WRITE-BACK: LO SFRATTO DI UNO SLOT SPORCO DEVE RIVERSARLO.**
Finche' era di sola lettura, sfrattare voleva dire sovrascrivere. Ora
sovrascrivere uno slot sporco butta via — in silenzio — una modifica gia'
accettata. E' il modo piu' diretto di perdere una voce di FAT a meta' di
una catena.

Corollario: `settore_mut()` marca sporco PRIMA della modifica, non dopo.
Fra le due cose il chiamante puo' chiedere un altro settore, quello sfratta
proprio questo slot, e la modifica sparisce prima di essere registrata.

! **LE COPIE DELLA FAT VANNO AGGIORNATE TUTTE.** Scriverne una sola non
da' errore QUI, perche' qui si legge sempre la stessa copia. Il danno lo
vede chkdsk, o un altro sistema che legge la copia 1: due mappe di
allocazione che non concordano, e nessun modo di sapere quale sia giusta.
Eccezione unica: mirroring disattivo su FAT32 (ExtFlags bit 7).

! **IL CLUSTER SI MARCA OCCUPATO PRIMA DI RESTITUIRLO.** Fra "trovato
libero" e "agganciato a una catena" risulterebbe ancora libero, e la
richiesta successiva dello STESSO chiamante lo assegnerebbe due volte.
Cross-linked: non riparabile senza perdere dati.

**PRIMA I DATI, POI LA DIMENSIONE.** Le due direzioni di sbaglio non si
equivalgono: dati-prima perde byte appena scritti (file coerente);
dimensione-prima espone il contenuto precedente di quei cluster, cioe' dati
di un altro file. Speculare in cancellazione: la catena si libera PRIMA di
marcare la voce libera, o restano cluster occupati da un file che non
esiste e di cui nessuno sa piu' nulla.

**IL CONTEGGIO DEI LIBERI SI DICHIARA IGNOTO (0xFFFFFFFF), NON SI INVENTA.**
Tenerlo esatto costerebbe una scansione dell'intera FAT al montaggio. Un
numero plausibile ma sbagliato e' peggio del non saperlo: gli altri sistemi
lo userebbero come vero. `fsck.fat` lo segnala come NOTA, non errore.

**DATA E ORA: costante riconoscibile** (2026-01-01), non una data finta —
non c'e' RTC, e una data plausibile renderebbe indistinguibile "non lo
sappiamo" da "e' stato scritto allora".

**I NOMI SI RIFIUTANO, NON SI STORPIANO.** Troncare in silenzio significa
che l'utente ottiene un file con un nome che non ha scelto e non
ritrovera'.

**La root fissa di FAT12/16 NON cresce**: estenderla scriverebbe sopra
l'area dati. "Piena" e' l'unica risposta corretta.

**Ogni percorrenza di catena ha un contatore di passi**: una FAT corrotta
puo' essere ciclica, e il kernel ci girerebbe dentro per sempre in ring0
per colpa dei metadati di un disco esterno.

### Come si prova un filesystem scritto da noi

Non basta rileggerlo con lo stesso driver: un driver che sbaglia in modo
coerente si rilegge benissimo. Le tre prove che contano, in ordine di
severita':

1. **`fsck.fat` da fuori** sulla partizione estratta con `dd`. Vede
   incroci, catene rotte, voci incoerenti.
2. **Confronto byte a byte** dei file riestratti con `mtools` contro gli
   originali.
3. **Eseguire un ELF copiato dal sistema stesso.** Un binario che gira e'
   un checksum severo.

Per esercitare la scrittura della voce FAT12 **a cavallo di due settori**
serve toccare il cluster **341** (l'unico, con settori da 512 byte):
cancellare un file la cui catena ci passa e' il modo piu' semplice.

### Strato di montaggio — kernel/fs/vfs.c (2026-07-31, 0.132)

**IL MONTAGGIO 0 E' LA ROOT SUL FLOPPY VIA fat12.c, E NON SI SMONTA.** Non
e' una semplificazione: e' la garanzia che l'avvio resti quello collaudato.
Nel caso peggiore di un difetto nell'instradamento si perde un disco, non
l'avviabilita' del sistema.

! **IL CONFINE DEL PREFISSO: `/disk2` NON STA DENTRO `/disk`.** E' l'errore
classico di ogni instradamento a prefisso. Confrontare i primi N caratteri e
fermarsi li' manda `/disk2` sul montaggio `/disk` con percorso interno `2` —
un disco SBAGLIATO, in silenzio. Il carattere dopo il prefisso deve essere
`\0` oppure `/`. Provato montando due punti di cui uno e' prefisso
dell'altro (`/d1` e `/d16`): e' un caso che non capita da solo.

**Prefisso PIU' LUNGO, non primo che combacia.** Con `/disk` e `/disk/dati`
montati entrambi, `/disk/dati/x` combacia con tutti e due. Vince il piu'
lungo, altrimenti il montaggio annidato sarebbe irraggiungibile e l'ordine
della tabella deciderebbe il significato di un percorso.

**I punti di montaggio sono VIRTUALI e quindi NON devono esistere.** `/disk`
compare nell'elenco della root perche' `vfs_readdir` lo aggiunge. Ne segue
una regola migliore di quella Unix: montare su un nome che ESISTE viene
RIFIUTATO. Su Unix quel caso nasconde dei file senza dirlo.

**Paginazione di readdir: i montaggi vanno in TESTA, non in coda.** In coda
servirebbe sapere quante voci ha il filesystem per capire dove comincia la
coda, e una pagina oltre la fine li ristamperebbe ogni volta. In testa
(indici 0..K-1) il conto e' esatto e locale.

**Sola lettura DICHIARATA, non subita**: `EROFS` viene restituito PRIMA di
toccare il filesystem. Lasciar fallire la scrittura piu' in basso darebbe
errori diversi a seconda di dove il driver si arrende, e un giorno una
scrittura parziale.

**La radice di OGNI montaggio non ha una voce di directory**: `fat12_stat("/")`
e `fat_stat("/")` falliscono entrambe. `vfs_stat()` lo sa. Il caso speciale
che stava in `sys_chdir` riguardava solo `/`: lasciarlo li' avrebbe reso
`cd /disk` un "non trovato".

`cfg.c` e `drvmgr.c` restano su `fat12_*` di proposito: girano prima che
esista un montaggio.

### ! kernel.cfg troncato in silenzio — kernel/fs/cfg.c (2026-07-31, 0.132)

Il buffer di lettura era 4096 byte; il file era arrivato a 5246. Il kernel
ne leggeva 4095 e **proseguiva senza dire nulla**: la sezione `[mount]`, in
fondo, spariva. Sintomo: "il montaggio automatico non funziona", nessun
errore, nessuna riga di codice sbagliata da cercare.

Il buffer e' ora 8192, ma **la dimensione non e' il rimedio**: qualunque
tetto si sceglie, un giorno il file lo supera. Il rimedio e' il `LOG_ERROR`
quando la lettura riempie tutto il buffer — significa che il file potrebbe
continuare, e va detto.

Vale come regola generale: **ogni lettura a buffer fisso che puo' troncare
deve dirlo.** Un troncamento silenzioso e' indistinguibile da un dato
assente.

### Driver FAT12/16/32 — kernel/fs/fat.c (2026-07-31, 0.131)

Guidato dal BPB, a piu' istanze, sopra il livello a blocchi. Nasce ACCANTO a
`fat12.c`, non al suo posto. Le sette decisioni di progetto e il perche' di
ognuna stanno in testa al file.

**Provato leggendo, non guardandolo**: ogni file di tre partizioni FAT12/16/32
letto per intero, con la somma dei byte confrontata con quella calcolata
fuori dal sistema. Un driver che legge i settori sbagliati passa qualunque
prova che si limiti a "ha restituito qualcosa".

! **Per toccare il caso della voce FAT12 a cavallo di due settori serve un
file abbastanza lungo.** Con settori da 512 byte solo la voce del cluster
**341** ci finisce (341 + 341/2 = 511). Con cluster da 16 settori vuol dire
un file di circa 3 MB: sotto quella soglia il caso non si presenta mai, e un
driver che lo sbaglia sembra funzionare.

I conteggi di cluster vanno confrontati con il calcolo di riferimento sui
byte del BPB, non solo con `vol.c`: due implementazioni che sbagliano allo
stesso modo sono d'accordo fra loro.

### Astrazione a blocchi — kernel/block/blk.c (2026-07-31, 0.130)

`fd0` (floppy), `hd<n>` (disco ATA intero), `hd<n>p<m>` (partizione).

**LA FINESTRA e' la ragione per cui il file esiste.** Una partizione ha
primo settore + lunghezza; ogni accesso viene tradotto
(`lba -> primo + lba`) e RIFIUTATO se ne esce. Il controllo sta in **un
punto solo**: nei filesystem andrebbe riscritto per ognuno e sbagliato
prima o poi. Rende impossibile a un fs su `hd0p1` di toccare `hd0p2` o il
settore 0, anche con un bug nei calcoli.

! **L'overflow si controlla PRIMA della somma**: `lba + n` puo'
traboccare e far sembrare interna una richiesta assurda. Provato con
lba=0xFFFFFFFFFFFFFFFF.

Provato (autotest temporaneo, poi rimosso) su una partizione da 102400
settori: ultimo settore ok; uno oltre, 2 a cavallo del confine, n=0 e
overflow tutti rifiutati.

**Numerazione uguale a fdisk**: 1-4 sono gli SLOT delle primarie (anche
vuoti), logiche da 5. Non l'indice nell'array. Senza, un disco con slot 1
e 3 darebbe `hd0p1`/`hd0p2` mentre fdisk dice `p1`/`p3`: chi partiziona
fuori e monta qui monterebbe **la partizione sbagliata**.

Non diventano dispositivi: le partizioni ESTESE (contenitori: darebbero
una finestra sovrapposta alle proprie logiche) e quelle che escono dal
disco.

**Il floppy passa dalla stessa cache del filesystem** (`fat12_dev_read/
write` -> `fat12_read_sector/write_sector`, non l'FDC diretto): due viste
incoerenti dello stesso supporto corrompono un filesystem.

`vol_identifica` prende ora un dispositivo e legge il **settore 0
relativo**: la lunghezza non puo' divergere dalla finestra vera, e il
riconoscimento del filesystem diventa la prova che la traduzione e'
giusta.

### Riconoscimento del filesystem — kernel/block/vol.c (2026-07-31, 0.128)

**LA REGOLA, l'unica corretta**: il tipo di FAT si decide dal **NUMERO DI
CLUSTER dell'area dati** — <4085 FAT12, <65525 FAT16, altrimenti FAT32.
NON dal byte di tipo MBR (0x06/0x0B/0x0C: e' un suggerimento e puo'
mentire), NON dalla stringa "FAT16   " nel settore di avvio (testo
decorativo, la specifica dice di ignorarlo). I confini sono 0xFF5 e
0xFFF5, cioe' il primo cluster che collide con i codici riservati di
ciascun formato: non vanno arrotondati.

Sbagliare qui **non produce un errore**: produce un filesystem letto con
la larghezza di voce di un altro, cioe' corruzione silenziosa. E' il
motivo per cui questo modulo esiste da solo e viene provato prima del
driver FAT.

**Ogni divisione e' protetta**: i campi arrivano dal disco, e un settore
dati scambiato per settore di avvio darebbe una divisione per zero in
ring0 — un kernel panic causato da un disco esterno. Validati:
byts_per_sec fra 512/1024/2048/4096, sett_per_cluster potenza di due <=128,
riservati != 0, n_fat 1-4, totale e dimensione FAT non nulli, metadati che
non superano il volume, volume che non eccede la partizione.

**Coerenza incrociata**: FAT32 vero ha root_entries=0 e fatsz16=0;
FAT12/16 ha root_entries!=0. Se il conteggio cluster e questi campi si
contraddicono, si marca `incoerente` invece di procedere.

Verificato su tre partizioni formattate con mkfs.fat: 2552 cluster ->
FAT12, 51078 -> FAT16, 275931 -> FAT32, conteggi identici al calcolo di
riferimento.

### ! VirtualBox: il disco va sul controller IDE, non SATA

VirtualBox crea le macchine nuove con controller **SATA (AHCI)**
predefinito. Il driver ATA di EX-OS usa le porte 0x1F0/0x170: un disco su
SATA **non viene visto**, e il sintomo e' "nessun disco" senza errori.
Impostazioni -> Archiviazione -> Controller IDE -> aggiungi disco fisso.

### ! Dove va inserito il VFS (per /disk)

`resolve_path()` (`kernel/syscall/syscall_impl.c:89`) produce un percorso
assoluto e le syscall lo passano **direttamente a fat12_***. Non esiste
VFS: il floppy e' cablato come filesystem unico e globale. Lo strato di
mount va subito dopo resolve_path, come traduzione
percorso-assoluto -> (istanza fs, percorso interno).

Nomi proposti: `fd0`, `hd0p1`, `hd1p2`. Sezione `[mount]` in kernel.cfg
nello stile di `[modules]`: `/disk = hd0p1`.

**Vincolo permanente**: il driver FAT nuovo nasce ACCANTO a fat12.c, non
al suo posto — l'avvio da floppy che funziona non deve rompersi finche'
la strada nuova non e' provata su entrambi.

### Driver ATA/IDE + lettura MBR (2026-07-31, kernel 0.127)

`kernel/block/ata.c` — PIO, **polling** (nIEN=1, non usa IRQ14/15: evita
di dipendere dal routing degli interrupt, che su questo progetto ha gia'
fatto perdere una sessione con l'IRQ6 del floppy). Attese su **g_ticks**,
mai a conteggio di iterazioni.

**Le tre capacita' e perche' non coincidono** — il cuore della richiesta
"se il BIOS dice 32 GB ma il disco ne ha 64":

- BIOS: barriere storiche (504 MB, 2.1, 8.4, 32, 137 GB). **Mai usato**.
- `IDENTIFY DEVICE`: quanto il disco dichiara ADESSO.
- `READ NATIVE MAX ADDRESS`: capacita' di fabbrica. Ritorna l'ULTIMO LBA,
  quindi i settori sono quel valore **+1** (fuori-di-uno che in un
  partizionatore si paga caro).

Se nativo > dichiarato c'e' **spazio nascosto**: HPA o jumper di
limitazione. Il driver lo **riporta e non lo rimuove**: `SET MAX ADDRESS`
e' persistente e non puo' essere effetto collaterale di un rilevamento.
! QEMU non emula HPA: il ramo "clippato" **non e' mai stato eseguito**.

Trappole ATA gia' trattate nel codice: ritardo 400 ns via stato
*alternato* (0x3F6, che non consuma l'interrupt pendente); bus flottante
= 0xFF prima di qualunque attesa; firma ATAPI 0x14/0xEB; stringhe
IDENTIFY a byte scambiati; conteggio 0 = 256/65536; **LBA48 scrive prima
il byte ALTO poi il BASSO sulla stessa porta** (invertirli da' indirizzi
sbagliati senza errore); **DRQ va atteso per OGNI settore**, non una volta
per comando.

`kernel/block/mbr.c` — sola lettura, 9 validazioni (firma, MBR
protettivo GPT, oltre-fine, sovrapposizioni, bootflag, catena ciclica,
voce vuota, piu' estese, troncamento). **Non corregge nulla**: correggere
in automatico una tabella sospetta significa decidere al posto
dell'utente su dati forse recuperabili.

Due punti da non dimenticare:
1. **I campi CHS non servono a calcolare niente**: oltre 8,4 GB saturano a
   `FE FF FF`. Solo `lba_inizio`/`n_settori` sono affidabili.
2. **I due riferimenti dell'EBR hanno basi diverse**: voce 0 relativa
   all'EBR corrente, voce 1 relativa all'**inizio dell'estesa**.

### ! Il FAT12 attuale NON e' una base per FAT16/FAT32

`kernel/fs/fat12.c` non e' generalizzabile: **LBA a 16 bit ovunque** (tetto
32 MB), geometria del floppy compilata dentro (`SECTORS_PER_FAT 9`,
`TOTAL_SECTORS 2880`, `MAX_CLUSTERS 2848`), parla direttamente all'FDC,
un solo montaggio globale con array statici. Serve un driver FAT nuovo
guidato dal BPB sopra un'astrazione a blocchi — **senza rompere l'avvio da
floppy**, che e' il vincolo piu' stretto.

Quando si scrivera': **il tipo FAT si decide dal NUMERO DI CLUSTER**
(<4085 FAT12, <65525 FAT16, altrimenti FAT32), non dall'etichetta nel BPB
ne' dal byte di tipo MBR. E' l'unico criterio corretto e sbagliarlo e' il
bug classico di ogni implementazione FAT.

### SYS_PROCINFO (188) + /bin/stack (2026-07-31, kernel 0.126)

Elenca i processi vivi con i loro stack. Espone gli **indirizzi grezzi**
(top / base / limit / kstack) invece di dimensioni già calcolate: sono
loro a dire come lo stack è stato allocato. Paginazione come
`sys_readdir` (`PROCINFO_MAX_BATCH = 16`), per non reintrodurre il
troncamento silenzioso di `ls`/`delete`.

**Bug trovato usandolo**: `proc_set_entry()` sovrascriveva
`proc->user_stack_top` con l'**ESP iniziale** (top − 16 − argv), non con
il top della regione. Ogni calcolo di impegnato/riservato sbagliava di 16
byte (7K/255K invece di 8K/256K). Nessuno se n'era accorto perché nessuno
leggeva quel campo del PCB — kernel_main e sys_spawn usano
`ElfLoadResult`. Ora `proc_set_entry` non lo tocca: l'ESP iniziale vive
già nello slot ECX del contesto salvato.

**! Prossimo candidato al risparmio**: `KERNEL_STACK_SIZE` è **128 KB per
processo, fisso** — con lo stack utente sceso a 8 KB impegnati, il kernel
stack è ora 27 volte più grande ed è la voce dominante (640K contro 24K
con 5 processi). Non può crescere su fault (il fault stesso richiederebbe
stack), ma 128 KB sono quasi certamente un ordine di grandezza di troppo.

### Stack utente a crescita su fault (2026-07-31, kernel 0.124)

`elf_load` non alloca più 64 KB fissi per processo. Due concetti separati
in `kernel/include/sched.h`:

- `USER_STACK_MAX` **256 KB** — spazio di indirizzamento riservato, non
  costa RAM, è solo il confine oltre il quale il processo muore;
- `USER_STACK_INIT` **8 KB** — RAM impegnata al caricamento; il resto lo
  mappa `pf_cresci_stack()` in `kernel/mm/paging.c`, una pagina alla volta.

Nuovo campo `Process.user_stack_limit`. `paging_destroy_directory` non è
stato toccato: libera ciò che è effettivamente mappato, quindi gestisce
già uno stack di dimensione variabile.

Misurato a 192 MB: memoria estesa usata 1804 → 1644 KB. In uso normale
**nessuna crescita**: 8 KB bastano a shell, driver kbd, `ls`, `mem`.

**! IL PUNTO DA NON PERDERE — perché si servono anche i fault da ring0.**
`syscall_verify_ptr()` controlla solo l'INTERVALLO di un puntatore utente,
non che le pagine siano mappate. Con
`char buf[8192]; read(0, buf, sizeof buf);` è il **kernel** a scrivere per
primo su pagine mai toccate: fault con U=0, che senza quel ramo sarebbe un
**kernel panic**. L'allocazione ansiosa di prima nascondeva il problema.
Nel ramo ring0 la condizione di vicinanza a ESP non è applicabile: in un
fault ring0→ring0 la CPU non impila SS:ESP e `frame->user_esp` è
spazzatura.

**Le cinque condizioni** di `pf_cresci_stack`, che ritorna 0 (= uccidi il
processo) in ogni caso dubbio: pagina assente (P=0); riserva presente;
indirizzo sotto la base impegnata; indirizzo sopra il limite della
riserva; e — **solo per ring3** — indirizzo vicino a ESP entro
`USER_STACK_SLACK = 32` (caso peggiore: `pusha` scrive 32 byte sotto ESP
prima di aggiornarlo). È la quinta a distinguere "lo stack cresce" da
"un puntatore impazzito è finito nella finestra dello stack".

Verificato con un programma temporaneo su quattro casi: crescita ring3,
crescita ring0, puntatore impazzito dentro la finestra (terminato),
ricorsione infinita (terminata per esaurimento, con messaggio dedicato).
La shell è sopravvissuta a entrambe le terminazioni.

Il costo è **rischio di correttezza, non prestazioni**: a regime zero
istruzioni aggiunte, in caricamento più veloce di prima.

### SYS_UPTIME (186) — l'orologio che mancava a ring3 (2026-07-31)

Ritorna i millisecondi dall'avvio (`g_ticks * 10`). Prima non esisteva
alcun modo per un processo ring3 di leggere l'ora: solo `SYS_SLEEP`. Da lì
i due difetti delle attese del KBC (contare iterazioni → dipende dalla
CPU; dormire a passi → granularità di un tick).

Uso corretto, in `drivers/kbd/kbd.c`:
- giro stretto iniziale, così nel caso normale non si paga una syscall;
- poi scadenza reale, con **`sched_yield()` e non `usleep()`**: cedere la
  CPU non impone una durata minima, quindi si ricontrolla appena si
  riottiene il turno invece di aspettare comunque un tick intero.

! `uptime_ms()` torna a zero dopo ~24,8 giorni: confrontare sempre
DIFFERENZE senza segno (`(unsigned)(ora - inizio) >= timeout`), mai valori
assoluti.

### Mappa E820 in Stage2 — la RAM non è più tappata a 64 MB (2026-07-31)

`bootloader/stage2/loader.asm` costruisce la mappa con INT 15h AX=E820
prima del passaggio a Protected Mode e riempie `e820_count`/`e820_addr`
in BootInfo (+13/+17). Il ripiego AH=88h resta per i BIOS che non la
supportano. `pmm_init` non è stato toccato: aveva già tutto.

Verificato in QEMU: 16/32/192/512/1024 MB rilevati correttamente (prima
sempre 64).

**Due cose da sapere prima di toccare Stage2:**

1. `E820_MAP_ADDR = 0x0A800` in `stage2.h` **è sbagliato e inutilizzato**:
   quel file appartiene al vecchio Stage2 in C, che il Makefile non
   compila più. 0xA800 cade dentro la FAT1 (0xA000-0xB200). La mappa vera
   sta a **0xD000**.
2. **Stage2 non può superare 1280 byte**: vive a 0x0500 e la GDT è
   costruita a 0x0A00. Ora è a 1095 byte (3 settori; stage1 segue la
   catena FAT, quindi il numero di settori non è un problema). C'è un
   `%if`/`%error` in fondo a `loader.asm` che rompe la build se lo si
   supera — senza, il guasto apparirebbe solo su hardware vero come un
   salto nel vuoto senza messaggi. Per più spazio: spostare la GDT.

### ! REGOLA: non si può aspettare meno di un tick (2026-07-31)

`usleep(us)` arrotonda a millisecondi, `sched_sleep(ms)` arrotonda a tick,
e il PIT è a 100 Hz: **qualunque `usleep()` sotto i 10 ms costa 10 ms**.

```
usleep(1000) -> ms=1 -> sched_sleep(1) -> ticks=(1+9)/10=1 -> 10 ms
```

Costato caro (kernel 0.118): il ciclo lento delle attese del KBC contava
`usleep(1000)` per iterazione credendo di contare millisecondi. Ogni
iterazione ne valeva dieci, `KBC_TMO_SELFTEST=1000` diventava 10 secondi e
`kbd_hw_init()` poteva durare **oltre 40 secondi** — con il prompt già a
video e la tastiera muta, cioè indistinguibile da un blocco.

Regola generale: un'attesa a passi va espressa in multipli di un tick, e
il numero di passi ricavato dividendo la scadenza per la granularità (in
`drivers/kbd/kbd.c`: `KBC_STEP_MS` + `kbc_steps()`).

**Manca il pezzo per fare di meglio**: non esiste una syscall che legga i
tick (`SYS_UPTIME`), quindi in ring3 non si può fare un'attesa a scadenza
reale *senza bloccarsi*. È la ragione per cui queste attese restano
"giro veloce a vuoto + passi da un tick" invece di un semplice
polling con deadline.

### Attese del KBC a conteggio di iterazioni invece che a tempo (2026-07-31)

Sul Pentium II, con la tastiera ormai funzionante, restavano tre warning:
self-test fallito `(0xffffffff)`, lettura del configuration byte fallita,
ACK enable-scan `(0x30)`. `0xffffffff` è `-1` con `%x`: tre **timeout** in
fila, e il terzo byte era solo sfasato — la risposta a un comando
precedente arrivata dopo che avevamo smesso di aspettarla.

`kbc_wait_read()`/`kbc_wait_write()` contavano iterazioni
(`KBC_POLL_MAX = 2000`) sulla premessa "il KBC risponde in decine di
microsecondi": vera per un registro, **falsa per il self-test `0xAA`**,
che sull'8042 reale è una diagnostica interna da millisecondi.

> **Stessa famiglia del bug FDC di giugno** (loop di NOP a conteggio
> fisso): un'attesa ancorata alla velocità della CPU invece che
> all'orologio. Qui mascherata dal contare syscall invece di NOP. Se ne
> trovano altre, sono da convertire allo stesso modo.

Fix: due fasi — giro veloce a vuoto (`KBC_POLL_FAST`) per il caso normale,
poi attesa reale con `usleep(1000)` (passa da `SYS_SLEEP`, ancorata al
PIT). Scadenza dichiarata dal chiamante: `KBC_TMO_ACK` 500 ms,
`KBC_TMO_CFG` 200 ms, `KBC_TMO_SELFTEST` 1000 ms.

### ! APERTO: tetto di 64 MB sulla RAM — manca E820 in Stage2

`PMM: nessuna mappa E820, uso fallback: 66112 KB` su una macchina da
192 MB. `bootloader/stage2/entry.asm`, `get_memory_size` usa **INT 15h
AH=88h**, che ritorna i KB di memoria estesa **in AX, 16 bit**: tetto
strutturale a 65535 KB qualunque sia la RAM installata.

**Il lato kernel è già completo**: `pmm_init` legge `info->e820_addr` /
`info->e820_count` e ha già il percorso che marca libere solo le regioni
usabili. Manca solo un ciclo **INT 15h AX=E820** in Stage2 che riempia
quei campi. Circoscritto, ma tocca il bootloader.

### Tastiera muta su hardware reale — il self-test 0xAA dell'8042 (2026-07-31)

Sintomo sul Pentium II: sistema avviato **completamente** (banner kernel,
banner shell, prompt `ex-os:/>`) e **nessun tasto produce alcun effetto**.
In QEMU la stessa immagine funziona.

Causa in `drivers/kbd/kbd.c`, `kbd_hw_init()`: emetteva il self-test del
controller (`0xAA`) e **non toccava mai il configuration byte** dell'8042
— non esistevano nemmeno le define per `0x20`/`0x60`. Su un 8042 vero
`0xAA` **reinizializza il configuration byte**, e il default ha il **bit 0
(KBD interrupt enable) a ZERO**. Il controller riceve gli scancode ma non
alza più IRQ1: il driver resta in `ipc_recv()` per sempre. Non è un crash,
non è un buffer pieno — nessuno viene più avvisato.

QEMU non lo mostra perché il suo 8042 risponde `0x55` a `0xAA` senza
toccare il proprio registro di modo: il bit resta come l'ha messo il BIOS.

Fix: read-modify-write del configuration byte dopo `0xAE` — `0x20` per
leggere, bit 0 (IRQ1) a 1, bit 4 (clock tastiera disabilitato) a 0, bit 6
(traduzione in set 1, che è ciò che le tabelle scancode del file
assumono) a 1, `0x60` per riscrivere. Ripiego `0x45` se la lettura
fallisce. RMW e non un valore fisso: il byte contiene anche le
impostazioni della seconda porta PS/2 e il system flag.

Il TTY in-kernel (`drivers/tty/tty.c`) non era toccato: drena `0x64`/
`0x60` e non emette mai `0xAA`, quindi eredita la configurazione del BIOS.
Il difetto era esclusivo del driver ring3.

### Il motore del floppy non veniva mai spento (2026-07-31)

`fdc_motor_off()` era `__attribute__((unused))`: nessuno la chiamava. Il
motore restava acceso dal primo accesso fino allo spegnimento. Invisibile
in emulazione; su hardware vero il dischetto gira sotto le testine per
tutta la sessione.

**Non aggiungere un timer di inattività**: il driver FDC non è rientrante,
e un processo bloccato in `fdc_wait_irq()` lascia girare l'idle task, che
spegnerebbe il motore in mezzo a un trasferimento. Lo spegnimento avviene
solo da punti sincroni sicuri — fine del boot (`kernel_main`, prima di
`sched_start()`) e fine di `fat12_sync()` (che gira a ogni `sys_exit`,
quindi il drive si ferma dopo ogni comando). Le uscite in errore di
`fat12_sync` lasciano il motore acceso di proposito.

`fdc_motor_off()` invalida ora anche `g_fdc_cyl`: alcuni drive rilasciano
lo stepper a motore fermo.

### Zero ritentativi sui settori — il floppy non è un disco affidabile (2026-07-31)

Segnalazione: sul Pentium II reale, con **drive floppy interno** (non USB),
il boot arriva a `[PASSO 15]` e si ferma con `'/bin/sh' non trovata`.

**Il dato che ha orientato la diagnosi** — la mappa fisica dell'immagine:

| Cosa | LBA | Cilindri |
|---|---|---|
| FAT + root directory | 1-9, 19-32 | **0** |
| `/bin` (dati della directory) | 34 | **0** |
| `/boot/kernel.cfg` | 200-207 | 5 |
| `/dev/kbd.drv` | 236-264 | 6-7 |
| **`/bin/sh`** | **370-396** | **10-11** |

Tutto ciò che `fat12_init()` legge sta sul cilindro 0, cioè dove il
`RECALIBRATE` ha appena messo la testina: **quelle letture non fanno un
solo SEEK**. Un `[PASSO 13] FAT12 OK` non dimostra quindi nulla sulla
capacità del drive di posizionarsi, ed è la ragione per cui il sintomo
sembrava specifico della shell.

**Causa**: `fdc_rw_sector()` veniva chiamata **una volta sola** per
settore, da `fat12_read_sector`/`fat12_write_sector`. Nessun livello dello
stack ritentava. In emulazione non si nota — un floppy QEMU non sbaglia
mai una lettura. Su un drive vero gli errori transitori sono la norma, non
l'eccezione: ST1 bit 0 (MA, address mark mancante), bit 5 (DE, CRC), bit 4
(OR, overrun DMA), tipicamente al **primo accesso a un cilindro appena
raggiunto**. BIOS, DOS e Linux ritentano tutti 3-5 volte con un recalibrate
in mezzo: non è prudenza, è il modo in cui l'hardware va usato.

Caricare `/bin/sh` sono 27 settori consecutivi: **basta che uno fallisca**
perché `elf_load` abortisca.

Fix in `kernel/fs/fat12.c`:
- `fdc_rw_sector()` diventa un involucro con **5 tentativi**; il corpo
  precedente è ora `fdc_rw_sector_once()`. Fra i tentativi si alterna
  `fdc_recalibrate()` (tentativi dispari) e la sola invalidazione di
  `g_fdc_cyl` (pari), che forza comunque un nuovo SEEK. Ogni ritentativo
  è registrato: un disco che funziona solo grazie ai ritentativi sta per
  morire e deve poterlo dire.
- **Motore acceso prima del `RECALIBRATE`** in `fat12_init()`: prima
  partiva con `DOR=0x0C`, unità selezionata ma motore fermo. QEMU ignora
  lo stato del motore, un drive vero no. Costo zero: i 300 ms erano
  comunque dovuti alla prima lettura, sono solo anticipati.
- **Fase di comando di SEEK e RECALIBRATE protetta da `cli`**, come già
  era quella di READ/WRITE: il controller ha un timeout interno di ~500 µs
  fra un byte di comando e il successivo.
- Il messaggio d'errore FDC ora riporta **C/H/S** oltre all'LBA: distingue
  "il controller non funziona" da "la testina non si posiziona".
- `kernel/kernel_main.c`: `[PASSO 15]` non afferma più `'non trovata'`.
  `elf_load` fallisce per una decina di ragioni diverse e stampa una riga
  `ELF: ...` specifica subito sopra; il messaggio vecchio dichiarava
  sempre la causa meno probabile e ha mandato la diagnosi fuori strada.

Kernel a **0.116**. Verificato in QEMU: nessuna regressione, prompt
raggiunto, e — correttamente — **zero messaggi di ritentativo**.
**Confermato sul Pentium II il 2026-07-31** (kernel 0.121): avvio con
`verboseboot=0` senza un solo warning, quindi le letture del floppy
riescono al primo tentativo su tutti i cilindri.

### Deadlock PIC — EOI dopo il context switch (luglio 2026)

**Il bug per cui "le applicazioni lanciate dalla shell si bloccavano".**

`irq_handler()` (`kernel/arch/x86/isr.c`) inviava `pic_send_eoi(irq)` come
ultima istruzione, dopo l'handler. Ma `sched_irq0_handler()` può chiamare
`sched_switch_to()` → `context_switch()`, che **cambia lo stack kernel e non
ritorna**: l'EOI restava non eseguito fino al riscaldulo del processo
preemptato. Con IRQ0 marcato In-Service il PIC non consegna più né IRQ0 né gli
IRQ di priorità inferiore, quindi `g_ticks` non avanza più e qualunque attesa
basata sui tick si blocca per sempre.

Percorso completo: `IRQ0` → switch alla shell (EOI perso) → `spawn()` →
`elf_load()` → `fat12_read_sector()` → `fdc_motor_on()` → `fdc_delay_ms(300)` →
`hlt` in attesa di `g_ticks` → congelamento. Firma diagnostica al monitor QEMU:
`pic0: irr=03 imr=bc isr=01`.

Fix: EOI **prima** del dispatch dell'handler. Sicuro perché gli IRQ entrano da
interrupt gate (IF=0 per tutto l'handler), quindi nessun rientro prima
dell'`iret`.

> **Invariante da rispettare in tutto il kernel**: qualunque handler di
> interrupt può non ritornare, perché il context switch è uno stack switch
> cooperativo. Tutto ciò che deve accadere "a fine interrupt" (EOI, rilascio
> lock, ripristino di stato hardware) va eseguito **prima** di chiamare codice
> che possa schedulare.

### Race "lost wakeup" nel TTY (luglio 2026)

`drv_read()` in `drivers/tty/tty.c` faceva `g_waiting_pid = pid;` e
`sched_block(PROC_BLOCKED);` **a interrupt abilitati**. Se IRQ1 cadeva nella
finestra fra le due, l'handler chiamava `sched_unblock()` su un processo ancora
RUNNING (no-op) e azzerava `g_waiting_pid`; subito dopo `sched_block()`
addormentava il processo con la sveglia già consumata → shell bloccata al
prompt per sempre, con la riga già nel ring buffer.

Fix: test-and-block atomico rispetto all'IRQ (`cli` + ri-controllo del buffer
prima di bloccare). Inoltre `g_input_buf` è ora `volatile` — scritto
dall'handler IRQ1 e letto in contesto processo, con `-O2` il compilatore poteva
tenere `count` in un registro attraverso il loop di attesa.

Lo stesso schema difettoso è presente in `drivers/kbd/kbd.c` (che però usa
busy-wait, non `sched_block`): da rivedere quando quel driver verrà migrato.

### Leak di PCB/page directory/stack kernel sui driver non caricati (luglio 2026)

In `kernel_main.c` (PASSO 14b e 15), sul fallimento di `elf_load()` si chiamava
solo `proc_kill()` → stato ZOMBIE. `init_reaper_task()` raccoglie però solo gli
zombie con `ppid == PID di init`, e questi hanno `ppid` = task attivo durante il
boot (idle), che non chiama mai `waitpid()`. Ogni driver mancante lasciava per
sempre uno slot PCB, una page directory e le pagine dello stack kernel.

Fix: `proc_kill()` + `proc_reap_zombie()` al chiamante. **Non** è stato
modificato `proc_kill()` per ri-genitorializzare a init: `sys_waitpid()` si
ri-blocca in loop, quindi se init raccogliesse lo zombie prima del genitore in
attesa, quel genitore resterebbe bloccato per sempre.

### Bug minori di supporto (luglio 2026)

- **`kprintf("%s", NULL)` → #PF nel kernel**: in `print_str()`
  (`kernel/arch/x86/kprintf.c`) `const char *p = s;` precedeva
  `if (!s) s = "(null)";`, quindi il conteggio della lunghezza dereferenziava
  il puntatore nullo. Risolto invertendo l'ordine.
- **`printf()` di libc ignorava flag e larghezza**: su `"%-12s"` il `-` cadeva
  nel `default` che stampava `%-` **senza consumare l'argomento**, sfasando
  tutti gli specificatori successivi (`ls` stampava `%-12s 3221219808`, dove il
  numero era il puntatore al nome letto da `%u`). Riscritta con supporto a
  `-`/`0`/larghezza e aggiunti `%o` e `%p`.
- **Makefile: `/bin/ls` non si ricompilava al cambiare di `libc.c`**.
  `LIBC_SRC` era definita *dopo* la regola `$(LS_BIN)` che la usa come
  prerequisito; make espande i prerequisiti alla lettura della regola, quindi
  `$(LIBC_SRC)` era stringa vuota. Sintomo ingannevole: i fix a `libc.c`
  sembravano "non avere effetto". Le definizioni sono ora prima della regola.
  **Regola generale: in questo Makefile ogni variabile usata come prerequisito
  deve essere definita più in alto del punto d'uso.**

### ! Console seriale di debug (luglio 2026) — da riusare

`kernel/arch/x86/vga.c` ora specchia su COM1 (38400 8N1) tutto ciò che passa da
`vga_putchar()`: `kprintf`, `klog`, eco tastiera e output dei processi. Serve
perché lo schermo VGA è 80x25 e i log di boot scorrono via. Ha una guardia di
spin per non bloccare il kernel su hardware senza COM1.

```bash
qemu-system-i386 -drive file=dist/floppy.img,format=raw,if=floppy \
  -m 32M -boot a -display none -serial file:/tmp/serial.txt -no-reboot
```

Per la diagnosi di blocchi: QEMU monitor su socket unix, `info pic` (mostra
`irr`/`imr`/`isr` — è così che si è trovato il deadlock EOI), `info registers`
(EIP + flag HLT, campionato più volte per distinguere blocco da idle loop),
`screendump` per lo schermo. Da `EIP` al simbolo: `nm build/kernel.elf | sort`.

### Primo driver in ring3: la tastiera (luglio 2026)

**Il punto 5 qui sotto è risolto per la tastiera.** `/dev/kbd.drv` è un
processo ring3 con la propria page directory, che non esegue nessuna
istruzione privilegiata e non chiama un solo simbolo del kernel:

| Prima (ET_DYN, kernel space) | Ora (ET_EXEC, ring3) |
|---|---|
| `port_inb`/`port_outb` diretti | `ioport_in`/`ioport_out` (SYS_IOPORT_IN/OUT), limitate al range `0x60..0x64` dichiarato con `ioport_bind` |
| `irq_register_handler(1, h)` | `irq_bind(1)` (SYS_IRQ_BIND): gli IRQ1 arrivano come messaggi IPC |
| `sched_unblock(pid)` | `ipc_send(pid, KBD_MSG_LINE, riga)` |
| `klog()` | `printf()` su fd 1 |
| `ENTRY(drv_init)`, chiamate dirette `drv_*` | `ENTRY(_start)`, `main()` con loop di servizio `ipc_recv` |

Il TTY resta nel kernel (possiede la VGA, implementa `drv_write`) ma per
l'input diventa un **client** del servizio kbd. Il dettaglio che rende
tutto semplice: `drv_read()` è chiamata da `sys_read()`, quindi gira già
nel contesto del processo che legge — le sue `ipc_send`/`ipc_recv`
operano sulla mailbox della shell, non su una del kernel.

Punti da NON rompere (dettagli completi in `HANDOFF.md`):

- **`drv_init()` del TTY non deve registrare l'handler IRQ1.**
  `irq_handler()` (isr.c) dà la precedenza agli handler kernel rispetto al
  proprietario ring3: un handler registrato "per sicurezza" affamerebbe
  completamente il driver ring3. La scelta è fatta al PASSO 14c di
  `kernel_main`, quando l'esito del caricamento è noto.
- **Il driver deve drenare il KBC in loop**, non leggere un byte per
  notifica: `ipc_notify_irq()` scarta le notifiche se la mailbox è piena,
  e un byte non letto tiene OBF alto bloccando ogni fronte IRQ1
  successivo.
- **I timeout di polling in ring3 sono ~50x più corti** di quelli
  kernel-space: lì erano `in`/`out`, qui ogni lettura è una syscall.
- **I processi driver sono adottati da init alla creazione**
  (`drv_proc->ppid = g_init_task->pid` in `kernel_main`): con idle come
  padre nessuno li raccoglierebbe mai, e `proc_reap_zombie()` è ciò che
  rilascia il claim sull'IRQ e il nome IPC registrato. Senza, un driver
  crashato non sarebbe mai riavviabile.

Il percorso in-kernel storico è conservato in `tty.c` sotto
`TTY_INPUT_INTERNAL` come fallback, ed è **testato**: senza
`/dev/kbd.drv` sul floppy la console resta pienamente usabile.

### FDC sincronizzato su IRQ6 + tre bug latenti da hardware reale (30 luglio 2026)

Dettagli completi in `HANDOFF.md`. In sintesi, quattro difetti che in
QEMU non si vedono e su un drive vero darebbero letture sbagliate
**senza errore I/O segnalato** — stessa firma del bug del Pentium II:

1. **Attesa di 15ms dopo RECALIBRATE/SEEK.** Su drive reale un
   recalibrate a stroke pieno richiede centinaia di ms: SENSE INTERRUPT
   veniva letto a seek ancora in corso e, dopo 3 tentativi, la funzione
   usciva in silenzio con la testina dove capitava. Ora si attende
   davvero l'IRQ6 (`fdc_wait_irq`, timeout 1000ms/500ms non fatale) e si
   controllano ST0 bit 5 (SE) e bit 4 (EC), non il solo `pcn == 0` — che
   è 0 anche se il comando non è mai partito.
2. **READ non faceva SEEK.** `fdc_seek()` esisteva ma non la chiamava
   nessuno (da mesi era la causa del warning `defined but not used`).
   Senza implicit seek (`CONFIGURE`/EIS mai inviato) il comando READ non
   muove la testina: un controller reale legge la traccia su cui si trova
   *fisicamente*. Funzionava solo perché QEMU fa il seek per conto suo.
   Ora `fdc_seek_if_needed()` prima di ogni READ/WRITE, con `g_fdc_cyl` a
   ricordare il cilindro corrente (36 settori per cilindro → una lettura
   sequenziale sposta la testina una volta ogni 36 settori). Misurato:
   boot 19,7s con la cache, 20,9s senza.
3. **`fat12_write_sector` non aveva il `cli`** che la lettura ha da
   sempre, commento incluso. Un IRQ0 fra due `fdc_send_byte()` fa
   abbandonare il comando al controller e i 512 byte vengono scartati
   silenziosamente: la scrittura "riesce" senza scrivere nulla.
4. **Due loop di NOP sopravvissuti** alla bonifica di giugno, nel reset
   del controller in `fat12_init`.

> **Da non "correggere"**: IRQ6 NON è usato per READ/WRITE, ed è giusto
> così. In PIO (SPECIFY con NDMA=1) il controller alza INT a *ogni byte*,
> non a fine comando: aspettarlo lì non significherebbe "settore
> trasferito" ma "c'è un byte", informazione già disponibile da MSR/RQM.
> Il polling di MSR è il modello corretto per il PIO.

**Non verificato su hardware reale** — ed è il punto: sono proprio i bug
che l'emulazione non mostra. In caso di problemi sul Pentium II, i log da
guardare sono i nuovi warning `timeout IRQ6 su ...` e
`SEEK non confermato`, che distinguono un problema di timing da uno di
posizionamento.

### FDC in DMA + scrittura su file mai funzionante (30 luglio 2026)

`/bin/textline` è il primo programma che scrive su disco, e ha fatto emergere
**tre difetti indipendenti** in codice mai esercitato. Dettagli in `HANDOFF.md`.

1. **Comando FDC di scrittura sbagliato**: `0xC5` (bit 7 = MT, Multi-Track) con
   `EOT=18` faceva attendere al controller un'intera traccia invece di un
   settore. Il commento diceva già "MT=0", era il valore a non corrispondere.
2. **QEMU non completa le scritture non-DMA.** Misurato: accetta esattamente
   512 byte, li scrive davvero sul supporto, poi resta in `MSR=0x30` per sempre
   senza mai entrare in fase di risultato. → **passaggio al DMA (canale 2)** per
   lettura e scrittura.
3. **`fat12_write` sovrascriveva invece di accodare**: partiva sempre
   dall'inizio dell'ultimo cluster ignorando `file_size`. Quattro `write()`
   consecutive producevano `\neconda riga` con dimensione 24.

> Il DMA **apre la strada al driver floppy in ring3**: era l'ostacolo
> principale documentato nel punto 5 qui sotto (un processo utente non può
> fare `cli` e verrebbe preemptato in mezzo a un trasferimento PIO). Ora il
> trasferimento non passa dalla CPU e la fine comando arriva via IRQ6.

Lettura e scrittura sono ora un solo `fdc_rw_sector()`: erano due copie quasi
identiche, ed è il motivo per cui il baco è sopravvissuto — la correzione della
lettura non si era propagata all'altra copia. La fase di risultato legge i
**sette** byte previsti, non tre.

### Percorsi relativi e scrittura in sottodirectory (30 luglio 2026)

Segnalazione "premendo l mi dice documento vuoto" aprendo `/boot/kernel.cfg`
con textline. Dietro un solo sintomo c'erano **sei difetti**, dettagli in
`HANDOFF.md`:

1. **Nessuna syscall risolveva i percorsi relativi.** `g_cwd` era scritta da
   `chdir()` e letta solo da `getcwd()`; open/exec/spawn/readdir passavano la
   stringa tale e quale a `fat12_*`, che parte sempre dalla root. Nuova
   `resolve_path()` (gestisce anche `.` e `..`). `chdir()` ora verifica pure
   che la destinazione esista e sia una directory — prima accettava qualunque
   stringa.
2. **`root_index` dedotto da un puntatore allo stack**: per un file esistente
   nella root, `fat12_write` scriveva `g_root_dir[indice_fuori_scala]` — una
   scrittura fuori dai limiti nella memoria del kernel. Nascosto dal fatto che
   solo il percorso di *creazione* era mai stato esercitato.
3. **Scrivere in sottodirectory non lasciava traccia**: la creazione finiva
   sempre in root e la entry non veniva mai riscritta.
4. **`O_TRUNC` non implementato da nessuno**: salvare su un file esistente
   appendeva al vecchio contenuto invece di sostituirlo.
5. **Salvare richiedeva minuti**: `fat12_write` riversava FAT e root a ogni
   chiamata (5000+ scritture di settore per 3,6KB) e la cache era
   write-through. Ora flush in `fat12_close`/`fat12_sync` e cache
   **write-back** a 16 slot.
6. **Il Makefile non ricompilava al cambiare di un header** — stessa classe di
   trappola di `LIBC_SRC`. Aggiunti `-MMD -MP`.

> **Conseguenza del write-back da tenere presente**: i dati non sono sul
> supporto finché non si chiude il file o non si chiama `fat12_sync()`. È il
> motivo per cui la procedura di arresto (`halt`/`poweroff`) sincronizza prima
> di fermare il sistema: le due cose si tengono, non toccarne una sola.

### ! Link dinamico ring3 — fattibilità verificata, non implementato

Requisito esplicito dell'utente ("le librerie devono essere tutte dinamiche"),
non ancora soddisfatto: i programmi di `/bin` restano statici. Verificato però
sperimentalmente che la strada è percorribile — il toolchain produce già un
ET_EXEC dinamico con `.rel.plt`/`R_386_JUMP_SLOT` e `DT_NEEDED [libc.so]`
(`-soname` già aggiunto al Makefile), e il kernel ha già il meccanismo per
scrivere nello spazio di un altro processo (`paging_get_physical`, usato da
`elf_load`). Passi concreti elencati in `HANDOFF.md`.

### Da fare (prossimi step verso driver in userspace)

5. **Il driver floppy gira ancora in ring0**: `kernel/loader/dynlink.c`
   risolve i simboli e mappa i moduli .so (floppy, kbd) nello stesso
   spazio del kernel. Per l'evoluzione a userspace serve: eseguire il
   binario driver come processo ring3 reale (ora possibile via
   `SYS_SPAWN`), una syscall/IPC per registrare/esporre le operazioni
   drv_*, e instradamento degli IRQ verso quel processo invece che
   verso una chiamata diretta C.

   **Stato aggiornato al 2026-07-30** — la tastiera è migrata, il floppy no:

   | Cosa | Stato reale |
   |---|---|
   | `boot/kernel.cfg` | dichiara `modules = kbd` |
   | `/dev/kbd.drv` | **ET_EXEC statico, processo ring3**, caricato al PASSO 14b e funzionante |
   | `/dev/tty.drv` | non è mai esistito: `drivers/tty/tty.c` è linkato *dentro* il kernel (`$(BUILD_KERNEL)/tty.o` in `KERNEL_OBJS`), usato al PASSO 14 via `drv_init()`. La voce è stata **rimossa** da `kernel.cfg`: produceva un `[WARN]` fuorviante a ogni boot |
   | `/dev/floppy.drv` | ancora **ET_DYN** (`-shared -fPIC`) per `drvmgr.c`/`dynlink.c`. `elf_load()` accetta solo ET_EXEC → sarebbe rifiutato con `tipo non supportato (type=3)`. **Rimosso** da `kernel.cfg` finché non è riscritto; viene ancora compilato e copiato sul floppy come promemoria |
   | accesso al floppy | fatto dal FAT12/FDC **interno al kernel** (`kernel/fs/fat12.c`) |

   Il floppy non è incompatibile solo per il tipo ELF: **anche accettando
   ET_DYN non potrebbe girare in ring3 così com'è scritto** — usa
   `port_inb`/`port_outb` diretti (#GP in ring3) e chiama simboli del
   kernel per linkage diretto (`klog`, `irq_register_handler`,
   `pic_unmask_irq`, `sched_unblock`), risolti da `drvmgr.c` solo perché
   mappati nello spazio del kernel.

   L'API syscall giusta **esiste già** ed è quella usata da kbd:
   `SYS_IRQ_BIND` (224), `SYS_IOPORT_BIND` (225), `SYS_IOPORT_IN` (226),
   `SYS_IOPORT_OUT` (227), `SYS_IPC_*` (220–223), con
   `irq_bind_process()` e `ipc_notify_irq()` in `kernel/arch/x86/isr.c`.

   Per completare il floppy: (a) riscriverlo contro l'API syscall;
   (b) aggiungere `_start` e linkarlo **ET_EXEC** statico come `/bin/*`,
   non `-shared` (copiare la regola Makefile di `kbd.drv`); (c) loop di
   servizio IPC (`ipc_register(nome)` + `ipc_recv`) al posto delle `drv_*`
   chiamate direttamente; (d) risolvere il **bootstrap** — il driver del
   floppy va letto *dal* floppy, quindi il kernel deve conservare il
   proprio FAT12/FDC minimo almeno per caricarlo: questa migrazione non
   toglie codice dal kernel, lo affianca.

   ! **OSTACOLO PRINCIPALE, scoperto il 2026-07-30**: `fat12_read_sector`
   fa il trasferimento PIO byte-per-byte con `interrupts_disable()`
   attorno al loop, e **un processo ring3 non può fare `cli`**. Verrebbe
   preempato da IRQ0 in mezzo al settore. In QEMU passerebbe; su hardware
   vero a 500 kbit/s il FIFO va servito entro microsecondi e un tick da
   10 ms è tre ordini di grandezza oltre il margine → **overrun**
   (ST1 bit 4). Tre strade possibili, nessuna ancora imboccata:

   1. **DMA** — la CPU non serve il trasferimento, la preemption diventa
      innocua. Richiede però lavoro nel kernel: `sys_ioport_bind` accetta
      **un solo range contiguo** e servirebbero anche le porte del
      controller DMA (0x00–0x0F, 0x81), più una syscall per ottenere un
      buffer fisicamente contiguo sotto i 16MB di cui il driver conosca
      l'indirizzo **fisico**. Architettura corretta, lavoro più lungo.
   2. **PIO + sezione non-preemptible** — flag nel PCB che
      `sched_irq0_handler` controlla per saltare il context switch, con
      timeout a tick. Più corta, ma concede a un processo utente di
      bloccare lo scheduler: compromesso sull'isolamento.
   3. **Lasciare il floppy in-kernel.** Legittima: il kernel deve
      comunque conservare FAT12/FDC per il bootstrap.

   Differenza rispetto a kbd da valutare prima di partire: il TTY può fare
   da client IPC perché `drv_read()` gira **nel contesto del processo che
   legge**. `elf_load()` no — è chiamato anche da `kernel_main` al boot,
   quando `proc_get_current()` è il task idle. Va deciso se il client del
   servizio floppy sarà il kernel o un vero VFS in userspace.


# EX-OS — Programmare: la libc, le librerie condivise, i compilatori

[🇬🇧 English](sviluppo.md) · **🇮🇹 Italiano**

[← README](../README.it.md) · [Tutti i manuali](../README.it.md#i-manuali)

**Indice**

- [La libc: da minimale a ospitata](#la-libc-da-minimale-a-ospitata)
- [Scrivere una libreria condivisa](#scrivere-una-libreria-condivisa)
- [La catena di compilazione dentro EX-OS](#la-catena-di-compilazione-dentro-ex-os)
- [`printf` in virgola mobile](#printf-in-virgola-mobile)
- [! `time_t` è a 64 bit](#-time_t-è-a-64-bit)

---

## La libc: da minimale a ospitata

Fino alla 0.145 `lib/libc.c` era una libreria da programma di servizio: `printf`,
le syscall e poco altro. Da agosto 2026 è la base su cui si può portare del
software scritto per un sistema POSIX — a cominciare da un compilatore.

### L'allocatore, che è il pezzo che mancava davvero

`free()` era una funzione **vuota**, con un TODO al posto del corpo, e `malloc()`
chiamava `sbrk` a ogni allocazione. Per i programmi di `/bin` non si notava:
allocano poche volte e poi escono. Per qualunque cosa lavori su una struttura ad
albero — un parser, un compilatore — significava crescere fino a esaurire lo
spazio senza aver mai tenuto in mano più di qualche KB. E `realloc()` copiava
`size` byte da un blocco di cui non conosceva la dimensione: ingrandire leggeva
**oltre la fine**.

Ora i blocchi stanno in una lista in ordine di indirizzo, `free()` li rende
riusabili e li **fonde** con i vicini liberi. La prova che conta sta in
`/bin/libctest`: 2000 `malloc`/`free` in ciclo non fanno crescere l'heap di un
byte.

### Stdio bufferizzato, con una politica diversa da Unix

C'erano `printf` e `putchar` — e `putchar` faceva **una syscall per carattere**.
Ora ci sono i `FILE*` (`fopen`, `fread`, `fwrite`, `fseek`, `ftell`, `fgets`,
`fprintf`, …), e un solo formattatore serve `printf`, `fprintf`, `sprintf` e
`snprintf`.

I file su disco sono bufferizzati a 4 KB. `stdout` e `stderr` **no**: sono
bufferizzati *dentro* la singola chiamata e svuotati alla sua fine. Non è il line
buffering di Unix, ed è deliberato — con quello, il prompt della shell
(`ex-os:/> `, senza newline) resterebbe nel buffer, e `gfedit` mostrerebbe
l'ultima riga solo dopo il tasto successivo. Unix se la cava perché leggere da
stdin svuota stdout; qui `gfedit` non legge da stdin, parla via IPC con il
servizio `kbd`, quindi quella convenzione non lo salverebbe. Svuotare a fine
chiamata elimina il problema e conserva quasi tutto il guadagno: un `printf`
costa una syscall invece di ottanta.

`exit()` svuota tutti i flussi: un programma che scrive un file e poi esce senza
`fclose()` trova il file scritto, non monco.

### Il resto

`setjmp`/`longjmp` (in assembly: sono esattamente i registri che il compilatore
ha il permesso di riorganizzare), `errno` con `strerror`/`perror`, `strtol`,
`strtoul`, `qsort`, `bsearch`, `strstr`, `strdup`, `strtok`, `memchr`, `ctype`,
e i wrapper `lseek`/`stat`/`sbrk`.

**`errno` si aggiunge, non sostituisce.** Le funzioni continuano a ritornare
l'errore negativo (`-2` = ENOENT, `-30` = EROFS): `< 0` resta il test giusto in
entrambe le convenzioni, e `-EIO` dice più di `-1`. Riscrivere ogni chiamante per
guadagnare zero non aveva senso.

Le syscall `stat` e `lseek(SEEK_END)` **rispondevano `ENOSYS`** — mai
implementate, con un TODO dalla Fase 3. Senza di loro nessun `FILE*` può offrire
`ftell()` sulla fine, cioè il modo con cui ogni programma misura un file prima di
leggerlo. Ora passano dal VFS e valgono su ogni filesystem montato.

### Virgola mobile, e la FPU che il kernel ha dovuto accendere

`strtod`, `strtof`, `strtold`, `ldexp`, `strtoll`, `strtoull`. Non è un lusso:
un compilatore deve leggere i letterali numerici dei sorgenti che compila —
`float x = 1.5;` passa da `strtod` — e una `strtod` che ritorna zero non dà un
errore, dà un programma compilato con la costante sbagliata.

Da qui il **PASSO 7b** del kernel: la FPU x87 viene inizializzata e il suo stato
(108 byte) salvato nel PCB a ogni cambio di contesto. Senza, due processi che
fanno conti in virgola mobile si sovrascrivono i registri a vicenda.

! **`x == 0.025` può essere falso anche quando `x` è giusto.** Su x87 GCC valuta
le costanti a 64 bit di mantissa, il `double` ne ha 53: il confronto avviene fra
due numeri diversi per costruzione. Il valore atteso va messo in una variabile
`double`, che forza l'arrotondamento.

### La libm: openlibm, e perché una di terzi

Fino ad agosto 2026 `<math.h>` dichiarava tre funzioni e diceva che una libm
non c'era, con questa motivazione:

> «una `sqrt` quasi giusta è peggio di nessuna `sqrt`: sbaglia in silenzio».

**Il ragionamento non è cambiato — è cambiata la conseguenza.** La risposta
coerente a "non so scrivere `sin` con l'errore giusto" non era scriverne una
mediocre: era **portarne una vera**. Dietro i nomi c'è **openlibm 0.8.7**,
cioè la `msun` di FreeBSD in versione autonoma (MIT/BSD), con trent'anni di
correzioni sugli arrotondamenti e una directory `i387` che usa le istruzioni
dell'x87 dove convengono. Si costruisce con
`tools/openlibm-exos/prepara-libm.sh`; i sorgenti non stanno nel repository,
come per GCC e binutils.

```
ex-os:/> /cdrom/bin/provamat
openlibm dentro EX-OS

sin(pi/2)=1000 cos(0)=1000 pow(2,10)=1024000 log(e)=1000 sqrt(16)=4000
atan2(1,1)=785 hypot(3,4)=5000 isnan(0/0.)=1
sinf(pi/2)=1000  sinl(pi/2)=1000  exp2(10)=1024000
```

I valori sono moltiplicati per mille e stampati come interi — la `printf`
di EX-OS non formatta i `double`, e mostrarli in virgola mobile avrebbe
provato la printf invece della libm. Quindi `atan2(1,1)` = 785 = 0,785 =
π/4.

I 184 prototipi in `lib/include/math.h` sono **ricavati dai simboli davvero
definiti in `libm.a`**, non copiati da uno standard: se una funzione è
dichiarata lì, esiste.

> ! **Chi usa queste funzioni deve linkare `-lm`.** Le eccezioni sono
> `sqrt`, `fabs`, `ldexp` e `frexp`, che stanno nella libc. `sqrt` è
> `fsqrt` dell'x87, cioè **una delle cinque operazioni che l'IEEE 754
> obbliga a essere correttamente arrotondate**: non c'è
> un'approssimazione da giudicare, c'è un'istruzione da chiamare. La
> definisce anche `libm.a`, ma vince sempre quella della libc — `libc.o`
> entra comunque nel link (printf, crt0) e a quel punto il simbolo è già
> risolto. Nessun "multiple definition", e `sqrt` si usa senza `-lm`.

Chi la chiede davvero è **libstdc++**: il suo `<cmath>` scrive `using
::sin;` per circa centottanta nomi, e quei nomi devono esistere o la
libreria non compila.

### Allocazione allineata

`memalign`, `aligned_alloc`, `posix_memalign`. Dal C++17 un tipo con
allineamento superiore a quello naturale non passa più per `operator
new(size_t)` ma per la variante allineata, che nella libstdc++ è un
involucro attorno a `memalign()` — e se `memalign` non c'è, la libreria ne
mette una che **ignora l'allineamento richiesto**.

L'allineamento si ottiene ritagliando: si chiede a `malloc` un blocco
abbastanza grande, poi lo si **spezza in due** mettendo una vera
intestazione subito prima dell'indirizzo allineato, e la testa resta come
blocco libero invece di essere sprecata.

> ! **Il puntatore restituito si libera con `free()`**, non con una free
> speciale: per l'heap è un blocco come tutti gli altri, e la fusione con i
> vicini funziona senza sapere nulla di tutto questo.

> ! `posix_memalign` **ritorna** il codice di errore e non lo mette in
> `errno`. È l'eccezione della famiglia, ed è il modo classico di sbagliare
> a usarla.

### Lettura formattata, data e ora, stat

`sscanf`/`vsscanf` con larghezze, `%n`, soppressione e `%lf`. `time`,
`localtime`, `gmtime`, `mktime`, `gettimeofday` sopra `SYS_TIME`, cioè
l'orologio CMOS in ora locale, e `SYS_FUSO` (270), il fuso del kernel: `time` dà
UTC, `localtime` e `mktime` usano il fuso con l'ora legale europea (kernel 0.340). `stat`/`fstat` nella forma POSIX.

### Processi: spawn con ambiente e redirezioni

`spawn()` lancia un programma e ritorna il PID; `waitpid()` ne raccoglie
l'esito. `spawn` resta il modo economico di lanciare un altro programma; dal
kernel 0.313 c'è anche una `fork()` vera, con la copia su scrittura
(sezione seguente).

```c
SpawnRedir r = { 1, O_WRONLY | O_CREAT | O_TRUNC, "/uscita.txt" };
int pid = spawn_ex("/bin/hello", argv, environ, &r, 1);
waitpid(pid, &stato, 0);
```

La redirezione è **per percorso**, non per descrittore già aperto del
padre: il figlio apre il proprio file. Passare un fd significherebbe due
processi sullo stesso handle VFS, cioè un conteggio di riferimenti che non
c'è e una `close()` che sfila il file da sotto all'altro. Basta a un driver
di compilatore; non basta alle pipe, che infatti non ci sono ancora.

L'ambiente si eredita per copia (`environ`, `putenv`, `setenv`,
`unsetenv`). `getenv()` ripiega sulla sezione `[env]` di `kernel.cfg` per
le chiavi che non trova: senza quel ripiego il primo processo — che un
padre non ce l'ha — resterebbe senza `PATH`.

### Processi: `fork()` con la copia su scrittura (kernel 0.313)

Redis (BGSAVE), il MPM prefork di Apache e `QProcess` di Qt vogliono una
`fork()` vera, e EX-OS ora ce l'ha (`SYS_FORK` 263, `fork`, `vfork`,
`pthread_atfork`):

- **Copia su scrittura, a 32 e 64 bit.** Il figlio riceve le tabelle delle
  pagine del padre; ogni pagina privata scrivibile diventa di sola lettura in
  tutti e due, segnata `PG_COW` (bit 10 della PTE), e la pagina fisica ha un
  proprietario in più nel PMM. La prima scrittura la copia (o, se è rimasto
  un solo proprietario, la rende di nuovo scrivibile). `CR0.WP` ora è acceso,
  quindi copia anche una scrittura del kernel stesso — una `read()` in un
  buffer del figlio. Le pagine `MAP_SHARED` e la memoria condivisa sono
  segnate `PG_CONDIVISA` (bit 11) e restano condivise.
- **Nessun tetto di proprietari.** Un byte per pagina conta fino a 256; oltre,
  una tabella hash che cresce tiene il resto. Le pagine sopra i 4 GB hanno i
  loro contatori.
- **I descrittori** si copiano con i riferimenti (file, pipe, pty), come su
  Unix; i **socket** diventano condivisi in ip.drv (0.021): il padre fa del
  figlio un proprietario in più di ognuno prima che `fork()` gli ritorni, una
  chiusura di un proprietario lascia andare solo quello, più processi possono
  aspettare in `accept()` sullo stesso ascoltatore, e una connessione
  accettata è di chi l'ha accettata.
- **Fili POSIX**: nel figlio resta solo il filo che chiama; i lucchetti della
  libc si prendono attorno alla chiamata e nel figlio si liberano; i gestori
  `atfork` girano nell'ordine di POSIX.
- **Figli**: `getppid`, `waitpid` su un figlio qualunque (`-1`, `0`,
  `WNOHANG`), `SIGCHLD` a un gestore.
- **Le pipe ricordano chi aspetta, tutti** (kernel 0.313): fino alla 0.312 una
  pipe svegliava solo l'ultimo processo fermo su di lei; con fork molti
  processi leggono la stessa pipe.

- **Le pile del kernel da tutta la RAM** (kernel 0.315, 64 bit): ogni processo
  ha 128 KB di pila nel kernel; fino alla 0.314 erano 32 pagine contigue nei
  primi 64 MB, e il portatile si fermava a 291 figli. Ora stanno in una
  finestra del kernel (la terza voce della PML4) condivisa da ogni spazio di
  processo, pagina per pagina da tutta la RAM, con una pagina non mappata
  sotto ognuna: un trabocco è un errore, non la pila accanto rovinata.
  `/proc/pile` (0.314) dice quanta ne usa ogni processo e il massimo visto:
  9272 byte sul portatile sotto carico.
- **Tutta la RAM per il kernel** (kernel 0.316, 64 bit): la quarta voce della
  PML4 mappa tutta la RAM, condivisa da ogni spazio di processo, e tabelle
  delle pagine, heap di kmalloc e tabella dei processi vengono da tutta la
  memoria invece che dai primi 64 MB. Il tetto dei processi è la RAM: 9000
  figli insieme in QEMU con 4 GB, 5000 sul portatile (2,8 GB); fino alla
  0.315, 1222 con 1 GB e 3045 con 4 GB.
- **Un socket in più insiemi epoll** (ip.drv 0.023): dopo fork ogni processo
  può mettere la stessa connessione nel proprio insieme epoll, come su Linux;
  `EEXIST` solo per lo stesso insieme due volte.
- **Più lettori su un socket UDP** (ip.drv 0.023): un filo del padre e un
  figlio nati da fork, entrambi in `recvfrom`, ricevono un datagramma a testa;
  fino alla 0.022 il secondo lettore prendeva il posto del primo, che
  aspettava per sempre. La tabella delle porte UDP cresce con le richieste
  (16 all'inizio, poi raddoppia).
- **Più attese su un pty** (kernel 0.317): ogni lettore e scrittore di
  ciascun lato viene ricordato (le stesse liste delle pipe); fino alla 0.316
  il secondo prendeva il posto del primo.
- **`alarm` suona, e un segnale catturato interrompe un'attesa** (kernel
  0.317): `alarm`, `setitimer`/`getitimer` (`ITIMER_REAL`; gli altri due
  dicono `EINVAL`) mandano SIGALRM con risoluzione di 10 ms, e `pause` aspetta
  un segnale; fino alla 0.316 `alarm` non faceva niente. Un segnale catturato
  interrompe `read`, `write`, `waitpid`, `poll`, `sleep`, `nanosleep` (col
  tempo che resta) e `F_SETLKW` con `EINTR`; con `SA_RESTART` (`signal()` lo
  mette, come su Linux) il gestore gira e `read`/`write`/`waitpid`/`fcntl`
  ripartono da sole; `poll` e `sleep` dicono sempre `EINTR`, come vuole
  POSIX. Fino alla 0.316 solo `kill -9` e Ctrl+C chiudevano un'attesa.
- **Una porta chiusa rifiuta** (ip.drv 0.022): un SYN dove nessuno ascolta
  riceve un RST, e `connect` dice `ECONNREFUSED` subito invece di aspettare.
- **Una pipe con `O_NONBLOCK` non aspetta** (kernel 0.317): una pipe vuota
  con ancora scrittori, o una piena, dà `EAGAIN`. Fino alla 0.316
  `fcntl(F_SETFL, O_NONBLOCK)` (e `pipe2`) ricordava il flag e `read`/`write`
  lo ignoravano.
- **Apache come demone Unix**: `apr_proc_detach` di APR fa la fork davvero
  (la patch di EX-OS la saltava), quindi `httpd` senza `-X` va sullo sfondo,
  il MPM worker fa i figli con la fork (4 processi, il mutex di accept con
  `fcntl`, lo scoreboard in memoria `MAP_ANON`) e `httpd -k stop` li spegne
  tutti. Prova: `DEMONE=1 tools/prova_httpd.sh`.
- **`openpty`, `forkpty`, `login_tty`** (`<pty.h>`, e `<util.h>`,
  `<libutil.h>` di BSD): sopra `pty_apri`, che dà i due capi insieme.
  `forkpty` fa dello slave l'ingresso, l'uscita e gli errori del figlio e
  dice al pty che Ctrl+C sul master interrompe il figlio (`PTY_CTL_FG`: EX-OS
  non ha i gruppi di processi).
- **I pseudo-terminali hanno un nome** (kernel 0.318): `open("/dev/ptmx")`
  crea una coppia nuova e ne dà il master; lo slave della coppia N è
  `/dev/pts/N`, bloccato fino a `unlockpt`, come su Linux. Finché nessuno ha
  aperto lo slave il master aspetta, invece di leggere la fine dei dati.
  `posix_openpt`, `grantpt`, `unlockpt`, `ptsname`, `ptsname_r` e `getpt`
  sono in `<stdlib.h>`; `ttyname` dice `/dev/pts/N` per uno slave e
  `/dev/ptmx` per un master (`/dev/tty` resta il nome della console);
  `openpty` dà il nome dello slave. `stat` e `fstat` dicono "dispositivo a
  caratteri" (`S_ISCHR`) per `/dev/null`, `/dev/ptmx`, `/dev/pts/N` e per i
  descrittori di un pty; il nome di una coppia sparisce col suo master. I
  nomi esistono per `open` e `stat`, non per `ls /dev/pts`, e `struct stat`
  non ha `st_rdev`: l'identità del dispositivo sta in `st_ino`.

Prova: `tools/prova-fork/prova-fork.sh` (64 bit) e `... 32`, in QEMU;
`--figli=N` per quanti figli vivi insieme (300 se manca).
Le prove dalla 17 alla 19 coprono UDP, il pty e l'allarme; la 20
`openpty`/`forkpty`, la 21 le pipe con `O_NONBLOCK`, la 22 i nomi dei pty
(`posix_openpt`, il blocco, `/dev/pts/N` nei due sensi, `ttyname`, `stat`).

**Guasto: un terminale o un programma come `script` dice "posix_openpt: No
such file" o non apre `/dev/pts/N`**: il kernel è più vecchio della 0.318,
dove un pty non aveva nome. Rimedio: `netupdate`, poi riavviare.

**Guasto: un programma aspetta per sempre anche se ha messo `alarm()`**
(una scadenza attorno a una lettura, un cane da guardia): il kernel è più
vecchio della 0.317, dove `alarm` non faceva niente. Rimedio: `netupdate`,
poi ricompilare il programma con la libc nuova (i programmi statici si
portano dietro la `alarm` vecchia).

**Guasto: Apache installato con netupdate non parte** ("AH02291: Cannot
access directory '/exos/apache2/logs/'"). Il repository porta file, non
cartelle, e `logs/` era vuota. Rimedio: `mkdir /exos/apache2/logs`, o
`netupdate`: ora ogni cartella vuota di apache2, mariadb e php ha un file
`.tieni` che la porta con sé.

### Valkey 9.1.3: BGSAVE e BGREWRITEAOF, la fork sotto carico (voce 33 i)

Valkey è la linea BSD di Redis 7.2 (Redis 8 è AGPL/RSAL/SSPL): lo stesso
protocollo, gli stessi file RDB e AOF; `redis-cli` e ogni client di Redis
funzionano. `tools/valkey-exos/costruisci.sh [32|64]` lo costruisce in
`/exos/valkey`: `bin/valkey-server`, `valkey-cli`, `valkey-benchmark`, e
`valkey-check-rdb`, `valkey-check-aof`, `valkey-sentinel` come **copie** del
server (Valkey sceglie cosa fare dal nome con cui parte, ed EX-OS non ha
collegamenti simbolici); `etc/valkey.conf` (bind IPv4, dati e file del pid in
`/exos/valkey/dati`). Usa il malloc della libc (niente jemalloc), il ciclo
degli eventi con `select`, niente TLS.

    /exos/valkey/bin/valkey-server /exos/valkey/etc/valkey.conf &
    /exos/valkey/bin/valkey-cli ping

`tools/valkey-exos/prova.sh [32|64]` in QEMU: PING, SET/GET, 200000 chiavi,
BGSAVE mentre `valkey-benchmark` carica il padre (circa 1200 SET e GET al
secondo), `valkey-check-rdb` sulla fotografia, un riavvio che ricarica le
chiavi, BGREWRITEAOF. 12 su 12 a 32 e 64 bit.

Cosa ha chiesto il porting a EX-OS:
- **la libc**: `pthread_cancel` (differita; i punti di annullamento sono
  `pthread_testcancel`, `pthread_cond_wait`/`timedwait`, `pthread_join`),
  `glob`/`fnmatch` (`<glob.h>`, `<fnmatch.h>`), le espressioni regolari
  POSIX (`<regex.h>`: `regcomp`/`regexec`/`regerror`/`regfree`, di base ed
  estese), `strndup`, `asprintf`/`vasprintf`, `bzero`, `ctime_r`/
  `asctime_r`, `IOV_MAX` in `<limits.h>`, `BYTE_ORDER` da `<sys/types.h>`,
  `ESOCKTNOSUPPORT`; `printf` impara `%jd` e `%td`;
- **il caricatore**: una libreria si riloca solo quando tutto l'albero è
  caricato (libapr chiama `modf` senza nominare libm, e il simbolo arrivava
  dopo); il TLS initial-exec (`R_X86_64_TPOFF64`, i386 `TPOFF`/`TPOFF32`)
  sulle variabili del programma stesso si risolve;
- **il kernel**: le pipe con `O_NONBLOCK` (sopra).

**Guasto: Valkey non risponde più dopo BGSAVE** (il figlio è uno zombi, il
server `blocc.` in `ps`): kernel più vecchio della 0.317, dove una pipe non
bloccante aspettava. Rimedio: `netupdate`.

**Guasto: Valkey muore col segnale 11 su `INFO`** ("Accessing address"
vicino all'ora attuale in secondi), **e le date del suo log dicono "-73188
Jan 1970"**: Valkey è stato collegato con una libc senza `%jd` e la cui
`gettimeofday` non riempiva il fuso orario. Rimedio: ricostruirlo con la
libc nuova (`tools/valkey-exos/costruisci.sh`); lo stesso vale per ogni
programma che stampa un `intmax_t` con `%jd`.

### Intestazioni con i nomi standard

`<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<ctype.h>`, `<errno.h>`, `<setjmp.h>`,
`<unistd.h>`, `<stdint.h>`, `<inttypes.h>`, `<math.h>`, `<time.h>`, `<fcntl.h>`,
`<assert.h>`, `<sys/stat.h>`, `<sys/time.h>` esistono e rimandano a `libc.h`
(`<stdint.h>` no: i tipi li dichiara il compilatore, vedi
`tools/gcc-exos/leggimi.md`). Sono facciate sottili di proposito:
due elenchi della stessa funzione divergono, e la divergenza si manifesta come
prototipo sbagliato — argomenti passati storti, non un errore di compilazione.

### Il costo, e come è stato riassorbito

Ogni programma di `/bin` compila la libc dentro di sé. Con lo stdio nuovo i
binari erano raddoppiati (`ls`: 12 → 25 KB). `-ffunction-sections`
`-fdata-sections` più `--gc-sections` al link buttano ciò che nessuno chiama:
`ls` è tornato a **10 KB**, meno di prima che la libreria crescesse, e il floppy
ha più spazio libero di quanto ne avesse all'inizio.

```
libctest       276 prove: allocatore (compresa l'allocazione allineata e
                la crescita dello heap fino al rifiuto), formattazione,
                flussi, salti non locali, conversioni, errno, virgola
                mobile, sscanf, data e ora, stat, ambiente, directory,
                temporanei, descrittori duplicati, variabili __thread,
                interfacce per il codice di terzi, spawn con
                redirezione — tutte dentro EX-OS
```

### Le pipe

`pipe()` dà due descrittori collegati da un buffer circolare di 4 KB nel
kernel. Le regole che contano sono tutte di confine:

> ! **Vuota con uno scrittore vivo = aspetta; vuota senza scrittori = 0.**
> È tutta qui la ragione per cui il conteggio degli scrittori esiste: senza,
> una pipe non è distinguibile da un blocco eterno. E scrivere quando non
> c'è più nessun lettore dà `EPIPE`, non un'attesa.

> ! **Niente `SIGPIPE`**: la pipe non manda segnali (quelli della 0.227 li
> mandano solo i guasti e `kill`), quindi si vede solo il
> valore di ritorno. Chi non guarda quello di `write()` non se ne accorge.
> E **niente garanzia di atomicità**: la scrittura può essere parziale
> anche sotto `PIPE_BUF`.

`FD_PIPE_R` e `FD_PIPE_W` sono due **tipi** di descrittore, non uno con un
flag: la direzione non è un dettaglio, è ciò che decide se una `read`
blocca o è un errore.

Per collegare due processi serve passare un'estremità al figlio, e lo si fa
con `SpawnRedir` a `percorso` NULL — l'**eredità dei descrittori**, che è la
sola aggiunta che rende le pipe utili fuori da un singolo processo:

```c
int p[2]; pipe(p);
SpawnRedir a = { 1, 0, NULL, p[1] };   /* stdout del figlio = scrittura */
spawn_ex("/bin/cmd", argv, environ, &a, 1);
close(p[1]);                            /* ! indispensabile */
```

> ! **Il padre deve chiudere l'estremità che ha passato.** Se non lo fa, la
> pipe conta ancora uno scrittore vivo — lui — e chi legge aspetterà per
> sempre. È l'errore classico con le pipe, e qui non c'è niente che lo
> segnali.

**Un difetto di progetto che le pipe hanno fatto emergere.** I descrittori
si chiudevano in `proc_reap_zombie()`, cioè quando il genitore chiama
`waitpid()`. Con i soli file passava inosservato; con una pipe è uno
stallo: il figlio esce, i suoi fd restano contati, il padre è bloccato
nella `read` e non arriverà mai al `waitpid`. Due processi ad aspettarsi a
vicenda, nessun errore. Ora si chiudono alla morte del processo — è il
motivo per cui su Unix stanno in `do_exit()` e non in `wait()`: uno zombie
non deve trattenere risorse di I/O, solo il proprio codice di uscita.

### La rinomina che non sposta i dati

`rename()` era **copia+cancella**: costava quanto il file e **rialloca i
blocchi**. Portava il nome di un'altra cosa, e la differenza non era
accademica — è ciò che rendeva impossibile a `install` verificare la mappa
dei settori del kernel prima di dargli il nome definitivo.

Dalla 0.161 è una syscall vera (`vfs_rename`, implementata su tutti e tre i
driver: `fat.c`, `ext2.c`, `fat12.c`) che riscrive la voce di directory e
basta. **I blocchi non si spostano**: è la garanzia su cui si regge
l'installatore. C'è anche il comando:

```
ex-os:/> rename /r1.bin /r2.bin
/r1.bin -> /r2.bin
```

> ! **Si chiama `rename` e non `mv`, di proposito.** `mv` su Unix
> rinomina *e sposta*, e quando sposta fra filesystem copia e cancella —
> un'operazione completamente diversa sotto lo stesso nome. Qui i dati non
> si muovono mai, e il nome dice cosa fa.

> ! **Due differenze da POSIX, dichiarate:** solo nella **stessa
> directory** (ENOSYS), e **non sostituisce** la destinazione (EEXIST).
> Attraversare directory sarebbe una copia più una cancellazione;
> sostituire vuol dire cancellare un file che il chiamante non ha nominato
> come vittima.

In `ext2_rename` **si aggiunge prima e si toglie dopo**: in mezzo il file
ha due nomi, che è riparabile; nell'ordine opposto non ne avrebbe nessuno e
l'inode resterebbe allocato e irraggiungibile.

> ! **`fat12.c` risponde in errno, gli altri due no.** Lì `-2` significa
> `ENOENT`, in `fat.c` ed `ext2.c` significa «esiste già». Mescolare le due
> convenzioni fa dire «esiste già» a una rinomina di un file inesistente —
> ed è successo, alla prima prova con un nome sbagliato.

### `getrusage`, `getpagesize`, `mmap`

Le tre funzioni che GCC usa come programma ospite, ognuna con il proprio
limite dichiarato.

> ! **`getrusage` non è una misura.** EX-OS non tiene contabilità per
> processo: lo scheduler assegna quanti e non misura consumi. `ru_utime`
> riporta il tempo **trascorso dall'avvio** — un limite superiore onesto —
> e tutto il resto è zero. Chi ci costruisce sopra un profilo
> (`gcc -ftime-report`) otterrà per ogni passaggio lo stesso tempo.

> ! **`mmap` mappa solo memoria anonima**: con `fd != -1` dà `ENODEV`.
> Mappare un file richiederebbe le pagine sporche e il momento in cui
> riscriverle; una mmap che finge consegnando zeri darebbe un programma che
> legge dati sbagliati senza che niente lo segnali. E ritorna
> **`MAP_FAILED`, non `NULL`**.

`munmap` **riporta giù il confine** se la zona smappata era in cima: prima
le pagine fisiche tornavano al PMM ma lo spazio di indirizzamento no, e il
garbage collector di GCC fa quel ciclo migliaia di volte per file
compilato.

### Descrittori duplicati: `dup`, `dup2`, `fcntl`

Dalla 0.151 gli handle aperti del VFS hanno un **conteggio dei
riferimenti**: `close()` chiude davvero solo l'ultima volta. È ciò che
rende possibile `dup()`, cioè tenere un file aperto oltre la `close()` di
chi possedeva il descrittore originale — il gesto che fanno `ar`,
`objcopy` e `arsup` di binutils.

`dup2()` è anche l'unico modo di sostituire `stdin`/`stdout`/`stderr`:
`close()` su 0, 1 e 2 è rifiutata apposta, perché lascerebbe il processo
senza uscita, mentre chi arriva da `dup2` il rimpiazzo ce l'ha già.

> ! **I due descrittori condividono il file, non la posizione.** Su POSIX
> una `read()` da uno dei due sposta anche l'altro; qui l'offset sta nel
> descrittore del processo, non in un oggetto «file aperto» intermedio, e
> ognuno tiene il suo. Chi legge da un fd duplicato faccia una `lseek()`
> esplicita. È la prima cosa da sistemare il giorno che arriveranno le
> pipe.

---

## Scrivere una libreria condivisa

Dal kernel 0.351 una libreria di EX-OS e' un normale oggetto condiviso ELF, come su ogni Unix:
codice indipendente dalla posizione, una tabella dinamica dei simboli, e `/lib/ld-exos.so` (la
libc e l'interprete) la carica. Il kernel tiene una copia sola del suo codice per tutto il
sistema.

**Si costruisce** con `-fPIC`, e cio' che esporta lo dice un version script:

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

**Un programma che ne ha bisogno** la nomina quando si collega, e l'interprete la carica alla
partenza (`DT_NEEDED`):

```
gcc -m32 -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie \
    -Wl,-Ttext-segment=0x08000000 -Wl,-z,separate-code -Wl,--hash-style=both \
    -Wl,-dynamic-linker,/lib/ld-exos.so \
    start.S programma.c /exwin/lib/mialib.so libexos_avvio.a /lib/ld-exos.so -o programma
```

**Un programma che sa farne a meno** la apre quando la vuole:

```c
void *h = dlopen("/exwin/lib/mialib.so", RTLD_NOW);
void (*pippo)(void) = h ? (void (*)(void))dlsym(h, "pippo") : NULL;
```

(`exlib_apri()` ed `exlib_simbolo()` di `exlib.h` fanno lo stesso, ed `exlib_apri_fra()` prova un
elenco di percorsi.) Il modello sono gli stub di `excss`, `exsuono` ed `exjs`: le stesse funzioni
della libreria, e ognuna la apre alla prima chiamata.

### Le regole

! **`-z defs` E `-z text`.** La prima rifiuta una libreria con un nome che nessuno definisce (lo
dice il collegamento, non il programma alla partenza); la seconda rifiuta codice che andrebbe
scritto al caricamento, cioe' una copia privata in ogni processo invece di una per tutti.

! **`ld-exos.so` PRIMA DI `libexos_avvio.a`** quando si collega una libreria: tutto viene dalla
libc condivisa, e dall'archivio solo `__stack_chk_fail_local`, il nome nascosto che
`-fstack-protector` usa nel codice PIC.

! **SI AGGIUNGE, NON SI TOGLIE.** Aggiungere funzioni e riscriverne il corpo e' sempre permesso:
i programmi gia' costruiti le trovano per nome. Togliere un nome dal version script li rompe, e
l'interprete dice quale nome manca.

! **I costruttori di una libreria girano prima del `main` del programma**, nell'ordine delle
librerie (prima quelle che servono): una libreria che ha bisogno di un'altra la nomina anche lei
con `DT_NEEDED` (`exdlg.so` nomina `exwin.so`), non la apre a mano.

! **`LD_DEBUG=libs` DICE DOVE CERCA** (dalla 22/11, come quello di glibc): ogni nome cercato, ogni
percorso provato (`LD_LIBRARY_PATH`, `DT_RUNPATH`, `/lib`, `/exos/lib`, `/exwin/lib` e lo stesso sul
CD) e com'e' andata, su stderr. Vale per i programmi su `/lib/ld-exos.so`; un programma collegato
ancora alla `libc.a` statica ha dentro il suo caricatore, e non lo dice.

```sh
export LD_DEBUG=libs
vlc -I dummy /root/Musica/brano.flac
```

---

## La catena di compilazione dentro EX-OS

### La catena intera, con un comando solo (6 agosto 2026)

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

`pg.c` è `tools/iso/prova-gcc.c`: `#include <stdio.h>` e `<string.h>`,
`printf` con `%lld`, `memcpy` dalla libc, una divisione a 64 bit che chiama
`__divdi3` in libgcc, una struttura restituita per valore. **I valori sono
noti in anticipo** — 385 è la somma dei quadrati da 1 a 10 — perché un
programma che stampa un numero senza che nessuno sappia quale fosse quello
giusto è una prova che non prova niente.

Il driver ha lanciato da sé cc1, `as`, `collect2` e `ld`, e ha trovato gli
header **senza un solo `-I`**.

### E lo stesso in C++

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

Contenitori con `<algorithm>`, `std::string` (cioè `operator new` sopra la
nostra `malloc`), polimorfismo con distruttore virtuale, e **le eccezioni**
— compresa una lanciata da dentro libstdc++ e ripresa attraverso più
livelli di stack, che è il pezzo che ha bisogno del maggior numero di cose
funzionanti insieme.

! **Nessun `-B`, e fino al 6 agosto 2026 serviva.** I percorsi che GCC ha
compilati dentro sono assoluti (`/exos/...`) e con il CD montato su
`/cdrom` non combaciano: il driver deve *rilocarsi*, cioè ricalcolare il
proprio prefisso da dove sta lui. Non ci riusciva, e il `-B` era la stampella.

Ci riesce da quando l'ambiente arriva davvero al processo figlio — è
`GCC_EXEC_PREFIX` a portare il prefisso rilocato, e `pex-exos.c` girava a
`spawn_ex` un ambiente vuoto. Le altre quattro correzioni servivano allo
stesso scopo: `..` che si risolve nei percorsi, `st_ino` che distingue le
directory, `-1` invece di `-errno`, e gli argomenti che non vengono
troncati. **Il `-B` è sparito come conseguenza, non come obiettivo.**

### L'albero `/exos`, e perché è fatto così

Non è una scelta di stile: è ciò che i binari cercano a runtime, e si legge
da loro (`strings gcc/cc1 | grep /exos`).

```
/exos/bin/                            gcc, g++, cpp, fbc, nasm, ndisasm, make
/exos/libexec/gcc/i386-exos/17.0.0/   cc1, cc1plus, collect2
/exos/lib/gcc/i386-exos/17.0.0/       libgcc.a, crt*.o, include/
/exos/lib/                            libc.a, libm.a, libstdc++.a, libcrypto.a
/exos/lib/freebasic/linux-x86/        libfb.a, fbrt0.o
/exos/include/                        la libc
/exos/include/c++/17.0.0/             libstdc++
/exos/include/freebasic/              i .bi di FreeBASIC
/exos/i386-exos/include/              la libc, dove cc1 la cerca da solo
/exos/i386-exos/bin/                  as, ld
```

! **La libc sta in due posti e non è uno spreco.** In `/exos/include`
perché è lì che la mette il prefisso; in `/exos/i386-exos/include` perché
quello è `TOOL_INCLUDE_DIR` — «un altro posto dove potrebbero stare gli
header del sistema bersaglio», `gcc/cppdefault.cc` — ed è l'unico dei due
che la rilocazione con `-iprefix` sappia raggiungere.

! **FreeBASIC vuole la stessa struttura, e se la calcola da solo.** Il suo
prefisso è la directory dell'eseguibile meno `bin`, ricalcolata a ogni
avvio: da `/exos/bin/fbc` trova `include/freebasic` e
`lib/freebasic/<target>` ovunque sia montato il CD — che è la cosa che a
GCC riesce solo con `GCC_EXEC_PREFIX` o con `-B`.

**GNU binutils 2.44 gira nativamente su EX-OS.** `as` e `ld` sono
compilati **per** `i386-exos`, non per la macchina che li ha costruiti:

```
ex-os:/> /cdrom/bin/as --version
GNU assembler (GNU Binutils) 2.44
This assembler was configured for a target of `i386-exos'.

ex-os:/> /cdrom/bin/as -o /prova.o /cdrom/prova.s
ex-os:/> /cdrom/bin/ld -o /prova /prova.o
ex-os:/> /prova
Assemblato e collegato dentro EX-OS.
```

L'oggetto prodotto qui dentro è **identico byte per byte** a quello che
produce il cross su Linux. I due strumenti stanno sul CD degli strumenti
(`make iso`, ~1,4 MB l'uno dopo lo strip); il sorgente di prova è
`/cdrom/prova.s`.

### Perché è il collaudo che conta

`libctest` chiama le funzioni che **sappiamo** di avere. binutils chiama
quelle che gli servono, e non ha nessun riguardo: le ha chieste una alla
volta, ognuna fermando la compilazione, e l'elenco è la misura di quanto
mancava a una libc «ospitata» vera —

| | |
|---|---|
| processi | `dup`, `dup2`, `fcntl`, `_exit` |
| file | `realpath`, `lstat`, `freopen`, `mktemp`, `pathconf`, `utime` |
| stringhe | `strcasecmp`, `strncasecmp`, `strcoll`, `strpbrk` |
| formato | `fscanf`, `scanf`, `strftime`, `asctime`, `ctime` |
| numeri | `frexp`, `atof`, `fabs` |
| caratteri larghi | `mbstowcs`, `mbrtowc`, `wcstombs` |
| permessi (inerti) | `chmod`, `fchmod`, `umask` |
| header | `<sys/types.h>` `<strings.h>` `<wchar.h>` `<sys/param.h>` `<limits.h>` `<memory.h>` `<utime.h>` |

più due che non erano funzioni: **`EOF`** — il valore c'era dal principio,
mancava il *nome*, e `safe-ctype.h` verifica di poter lavorare con
`#if EOF != -1`, che senza la macro vede zero e conclude che la libc è
sbagliata — e **`strerror` che tornava `const char *`**, più sicuro e
incompatibile con la firma dello standard.

### `pex-exos.c`: lanciare un programma senza `fork`

`libiberty` compila sempre un `pex-*.c` — «lancia un programma e
aspettalo» — e per tutto ciò che non è Windows o MSDOS sceglie
`pex-unix.c`, costruito su `fork()`. EX-OS non ha `fork`, e non è una
mancanza da colmare: duplicare uno spazio di indirizzamento per buttarlo
via un'istruzione dopo è esattamente ciò che `spawn_ex()` evita.

`tools/binutils-exos/pex-exos.c` è il rimpiazzo, modellato su
`pex-msdos.c`: un «descrittore» è un indice in una tabella di **nomi**,
perché è il nome ciò che serve a `spawn_ex`. I tre `NULL` nella tabella
`funcs` (pipe, fdopenr, fdopenw) **sono la dichiarazione che questo
sistema non ha pipe**, e `pex-common.c` se ne accorge da solo e passa alla
modalità a file temporanei.

### Tre trappole, che costano un'ora a testa

- **`-std=gnu17`.** GCC 17 compila in C23, dove una dichiarazione
  implicita è un **errore**. binutils 2.44 presume l'indulgenza di C17.
- **`export ac_cv_tls=`** (stringa vuota, non `none`). La prova che il
  configure fa per le variabili thread-local è una **compilazione**:
  `i386-exos-gcc` accetta `_Thread_local` senza fiatare, perché è il
  compilatore a saper emettere gli accessi via `%gs` ed è il *sistema* a
  non avere un thread pointer. Il risultato è un `as` che si compila
  benissimo e muore alla terza istruzione di `bfd_init`. Con `none` la
  macro non viene definita affatto e binutils non compila: serve
  **definita a vuoto**.
- **`sys-include`.** GCC installa un proprio `<limits.h>` e ne esistono
  due versioni; quella che fa `#include_next` — cioè che prende anche la
  nostra — viene generata solo se, al momento di costruire GCC, un
  `limits.h` di sistema c'era già, e GCC lo cerca in
  `$prefisso/i386-exos/sys-include`.

Ricetta completa in **`tools/binutils-exos/leggimi.md`**.

### GMP, MPFR e MPC

Le tre librerie a cui `cc1` si linka girano anche loro dentro EX-OS — sono
il codice di terzi più pesante che il sistema abbia ospitato, 4,6 MB di
archivi di aritmetica a precisione arbitraria:

```
ex-os:/> /cdrom/bin/provamp
GMP 6.3.0, MPFR 4.2.1, MPC 1.3.1 — dentro EX-OS

GMP   2^128 = 340282366920938463463374607431768211456
MPFR  pi     = 3.1415926535897932384626433832795028841971693993751e0
MPC   sqrt(i)= (7.0710678118654752440e-1 7.0710678118654752440e-1)
```

Costruirle è `tools/gcclibs-exos/prepara-gcclibs.sh`. Due cose non ovvie,
entrambe documentate lì:

- **`CC_FOR_BUILD=gcc` esplicito.** GMP sceglie il compilatore per i propri
  generatori di tabelle compilandone uno e *eseguendolo* — e la prova
  riesce con il cross, perché un binario di EX-OS è un ELF32 statico che
  Linux carica, con syscall che hanno i numeri di Linux. Parte, stampa,
  sembra a posto; non combaciano `argc` e `argv`, e il generatore si
  rifiuta di generare.
- **`--host=i486-pc-exos`, non i386.** Non è un ripiego: 486 è il minimo
  che EX-OS già richiede per conto suo (`invlpg` in `kernel/mm/paging.c`).
  Con `i386` si finisce su un difetto di GCC 17 che emette `rolw $8, %eax`
  — rotazione a 16 bit con il registro a 32 — e l'assemblatore rifiuta.

### La libreria standard del C++ gira

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

**libstdc++ 25 MB e libsupc++ 1 MB** compilate per `i386-exos`. Il
programma si costruisce con una riga sola, con `g++`, come su qualunque
altro bersaglio:

```sh
i386-exos-g++ -O2 -o provacpp prova-cpp.cpp
```

> ! **Le eccezioni funzionano, e non era scontato.** Sono il pezzo che ha
> bisogno di più cose insieme: `__cxa_throw`, lo svolgimento dello stack,
> le tabelle `.eh_frame`, i descrittori di tipo. Lo svolgimento **legge a
> runtime le tabelle prodotte dal collegatore e le percorre**: è l'unica
> parte del C++ che pretende che il programma caricato in memoria sia
> esattamente come il collegatore l'ha descritto — quindi è anche una prova
> indiretta che il caricamento su richiesta di EX-OS è corretto.

**La riga che mancava da sempre: `extern "C"`.** Nessuno degli header di
EX-OS aveva la guardia `#ifdef __cplusplus`. Il C++ decora i nomi con i
tipi degli argomenti — `printf` diventa `_Z6printfPKcz` — mentre `libc.a` è
compilata da un compilatore C e dentro ha il nome nudo: **ogni programma
C++ chiamava simboli che nell'archivio non esistono.**

Il sintomo era fuorviante, perché arrivava in compilazione e non al link:
la libstdc++ dichiara alcune funzioni della libc con `extern "C"` esplicito,
e il messaggio diceva `conflicting declaration of 'void* memalign(...)'
with 'C' linkage` — cioè «questa dichiarazione è in conflitto con se
stessa».

### I nomi che servono a essere nominati

Perché la libstdc++ compili, la libc deve *dichiarare* molte cose che
EX-OS non farà mai: 40 codici errno di rete e IPC, le costanti `DT_*` per
FIFO, socket e collegamenti simbolici, i `S_IF*`/`S_IS*` corrispondenti.

> ! **Un nome mancante è un errore di compilazione, non un ramo morto.**
> `<system_error>` costruisce `std::errc` da quell'elenco e `<filesystem>`
> scrive `case DT_LNK:` in uno switch: serve la costante, non il
> comportamento. Ritornare sempre 0 da `S_ISLNK()` è la risposta **giusta**
> — su EX-OS un collegamento simbolico non esiste, quindi «questo file è un
> collegamento?» ha davvero risposta no.

I valori sono quelli di Linux e non vanno reinventati: il giorno che i
collegamenti simbolici arrivassero, `S_IFLNK` dovrà valere `0120000` come
ovunque.

### Collegare un programma C vero, dentro EX-OS

Il CD degli strumenti porta anche il **runtime del bersaglio** in
`/exos/lib`: `crt0.o`, `crti/crtn/crtbegin/crtend.o`, `libc.a`, `libgcc.a`,
`libm.a`. Sono oggetti `i386-exos` prodotti dal cross, quindi già codice di
EX-OS: `ld` nativo li legge qui dentro come li legge il cross su Linux.

! **Senza questi il driver arriva a metà.** `gcc -c` ha bisogno solo di
cpp, cc1 e as; `gcc -o programma` ha bisogno anche di crt0, `libgcc.a` e
`libc.a` — e senza si ottiene un errore di `ld` su simboli che non
c'entrano niente col sorgente che si stava compilando.

La prova è `prova-gcc.c`, sul CD insieme al suo assembly generato dal cross
(che fa anche da termine di paragone per quello che `cc1` dovrà produrre):

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

! **La divisione a 64 bit è lì apposta.** È una delle poche cose che il
compilatore non sa fare con un'istruzione: chiama `__divdi3` in `libgcc.a`.
Se libgcc non è stato collegato, il difetto si vede lì e solo lì. Gli altri
due valori sono attesi e scritti nel sorgente — un programma che stampa un
numero sbagliato senza che nessuno sappia quale fosse quello giusto è una
prova che non prova niente.

### `cc1` compila dentro EX-OS

`as` traduce, `ld` collega, le tre librerie di calcolo ci sono, la libm c'è,
**libstdc++ gira** — e **`cc1` compila**. Il compilatore C di GCC,
costruito in canadian cross
(`--build=x86_64-linux --host=i386-exos --target=i386-exos`), legge un
sorgente C dentro EX-OS e ne produce l'assembly, che `as` e `ld`
trasformano in un eseguibile.

**! Il binario va ricostruito.** La prova è stata fatta prima del
passaggio a `time_t` a 64 bit; dopo quel cambiamento `cc1` è stato solo
**rilinkato**, e gli oggetti già compilati continuavano a credere che
`struct timeval` fosse di 8 byte mentre la libc nuova ne scrive 12 — pila
corrotta, sistema fermo. È il promemoria che un cambio di ABI non si
risolve con un link. La ricostruzione completa è **fatta** — il binario
nuovo c'è — ma non è ancora stata riprovata dentro EX-OS: finché non lo
sarà, la riga qui sopra dice «da riprovare» e non «testato».

Sono 41 MB di binario, e girano solo grazie al caricamento su richiesta
(vedi *Le pagine di un programma arrivano quando servono*): il costo
d'avvio non dipende dalla dimensione, e la memoria restituita al kernel
tiene il resto sotto controllo.

Arrivarci ha scoperto tre difetti nella libc, tutti invisibili ai
programmi di EX-OS perché nessuno di loro fa quello che fa un compilatore:

| | |
|---|---|
| `realloc` non ingrandiva **mai** sul posto | la fusione col blocco successivo rifiutava i blocchi non liberi — cioè esattamente il caso da gestire. Si vedeva come un salto a `0xa7a6a5a4`, che sono i byte di riempimento del test letti come puntatore |
| i costruttori globali non venivano chiamati | `cc1` ha 57 voci in `.init_array`; la prima struttura usata era vuota |
| `printf` con `%f` inventava cifre | oltre la diciottesima, e arrotondava 2,5 a 3 invece che a 2 |

### Un binario di terzi porta dentro la libc del giorno in cui è stato collegato

Qui non ci sono librerie condivise: `as`, `ld` e `cc1` hanno **una copia
della libc dentro di sé**, quella con cui sono stati collegati. Correggere
`lib/libc.c` non li tocca. Sembra ovvio detto così, e non lo è affatto
quando il difetto corretto è nell'allocatore.

`ld` andava in page fault appena gli si davano degli archivi da collegare:

```
[FAULT] PID 9 '/cdrom/bin/ld': page fault a 0x00000005
        (protezione, scrittura, EIP=0x080d2e16)
```

L'indirizzo si risolve sul binario non strippato, e non è codice di
binutils:

```
EIP 0x080d2e16  ->  malloc + 0x116
```

! **La conferma sta nella tabella dei simboli, non nel ragionamento.**
Dentro `ld` c'era `heap_fondi_con_succ` e **non** `heap_assorbi_succ` —
cioè la funzione che la correzione di `realloc` ha introdotto. Quel
binario è del 2 agosto: si porta dentro la libc in cui `realloc` non
ingrandiva **mai** sul posto, e il chiamante che credeva di avere più
spazio scriveva oltre la fine. La corruzione non si vede dove nasce, si
vede alla `malloc` successiva.

Collegare un solo `.o` passava: poco traffico di `realloc`. Con `libc.a` e
`libgcc.a` da leggere, bfd fa crescere le tabelle dei simboli e arriva.

#### Il ricollegamento da solo non basta, e crederlo costa un secondo difetto

La prima risposta è stata ricollegare `as` e `ld` contro la libc corretta,
senza ricompilarli: l'allocatore è *implementazione*, l'ABI non cambia,
quindi il relink dovrebbe bastare. `ld` ha smesso di andare in fault e ha
collegato gli archivi. **E `as` ha cominciato a saltare a un indirizzo a
caso** (`EIP=0x6a722690`, pagina assente).

Il ragionamento aveva una premessa non verificata: *fra il 2 agosto e oggi
è cambiata solo l'implementazione*. Non è vero.

```
oggetti dei binutils   2 agosto    typedef long      time_t;
libc di oggi                       typedef long long time_t;
```

`struct stat` contiene **tre** campi `time_t`: è cresciuta di dodici byte
e ha spostato tutti gli offset successivi. Un oggetto compilato con
l'header vecchio la legge alla vecchia maniera mentre la libc nuova la
scrive alla nuova — e quello che ne esce, se finisce in un puntatore a
funzione, è esattamente un salto a `0x6a722690`.

> ! **È lo stesso difetto di `cc1`, non un altro.** Là l'avevo capito
> subito perché il cambio di `time_t` era fresco; qui l'avevo dimenticato
> e ho concluso «basta ricollegare» **prima** di verificarlo. La
> ricostruzione completa dei binutils è l'unica risposta giusta, come per
> `cc1`.

> ! **`ld` ricollegato ha funzionato lo stesso**, e questa è la parte
> istruttiva: nel percorso che collega archivi la `struct stat` sbagliata
> non viene toccata. «Ha funzionato una volta» non è una prova di
> correttezza — è una prova che quel percorso non passa di lì.

Regola generale, valida per qualunque cosa verrà portata qui dentro:

| cos'è cambiato nella libc | cosa basta |
|---|---|
| solo `lib/libc.c` (implementazione) | ricollegare |
| anche `lib/include/libc.h` (tipi, strutture) | **ricompilare tutto** |

Il modo di accorgersi del primo caso è cercare nella tabella dei simboli
una funzione che esiste solo dopo la correzione. Il modo di accorgersi del
secondo è guardare `git diff` sull'header **prima** di decidere, che è
esattamente il passo che qui è saltato.

**Cosa manca ancora:** il programma di guida `gcc`, quello che concatena
`cc1 → as → ld` passando i file intermedi. Oggi i tre passi si danno a
mano.

---

## `printf` in virgola mobile

`%f`, `%e`, `%g` e le loro maiuscole, con larghezza, precisione e flag.
Prima consumavano l'argomento e stampavano `<float>`.

! **Le cifre significative si fermano a 18, e oltre si stampano zeri.** È
un numero **misurato**, non stimato: confrontando il motore con glibc su
una dozzina di valori, fino a 18 non c'è una discordanza, a 19 compare la
prima. Un `double` porta al massimo 17 cifre di informazione; quello che
c'è oltre è l'espansione esatta del valore *binario*, che glibc stampa con
un'aritmetica a precisione arbitraria e noi no:

```
printf("%.30f", 0.1)
  glibc  0.100000000000000005551115123126
  EX-OS  0.100000000000000000000000000000
```

Le prime 17 cifre coincidono — è tutto ciò che `0.1` contiene.

! **L'arrotondamento è al pari**, come prescrive lo standard: `%.0f` di
2.5 dà `2`, di 3.5 dà `4`. Con la regola ingenua («da 5 in su sale»),
sommare una colonna di valori arrotondati accumula un errore che cresce
col numero di righe.

**Su 399 confronti con glibc, 390 identici**; i 9 restanti compaiono solo
chiedendo più di 18 cifre significative.

## ! `time_t` è a 64 bit

Non solo per il 2038 — che pure è una scadenza da non scriversi in
partenza nel 2026. Il difetto che l'ha reso urgente è aritmetico: GCC
misura il tempo con

```c
now->wall = tv.tv_sec * 1000000000 + tv.tv_usec * 1000;
```

e con `tv_sec` a 32 bit quella moltiplicazione **trabocca prima di essere
allargata**. Il rapporto dei tempi di `cc1` usciva con fasi da 18 miliardi
di secondi. Non è codice di GCC da correggere: è codice giusto su un
`time_t` giusto.

! E `gettimeofday` prende ora **secondi e microsecondi dalla stessa
sorgente**. Prima i secondi venivano dall'orologio CMOS e i microsecondi
dal contatore dei tick: due orologi indipendenti, e la coppia poteva
**tornare indietro**. Un orologio che torna indietro non dà un errore, dà
intervalli negativi a chi sottrae due istanti. Il prezzo dichiarato: se
qualcuno corregge l'ora di sistema mentre un programma gira, `gettimeofday`
non se ne accorge — un orologio che non torna mai indietro vale di più.

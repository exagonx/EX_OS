# Le modifiche ai sorgenti di FreeBASIC

*Versione inglese: [`MODIFICHE-FBC.en.md`](MODIFICHE-FBC.en.md)*

Questo file elenca **tutto** ciò che EX-OS cambia dentro i sorgenti del
compilatore FreeBASIC, e il perché di ogni punto. È il documento che serve a
tre persone diverse: chi porta una versione nuova di FreeBASIC, chi deve
capire perché un binario prodotto per `exos` è diverso da uno prodotto per
Linux, e chi vuole rispettare la GPL v2 sapendo esattamente cosa è stato
toccato.

FreeBASIC è distribuito sotto **GPL v2** (il compilatore) e **LGPL v2.1 con
eccezione di collegamento** (la runtime). Le modifiche qui descritte sono
soggette alle stesse licenze.

---

## Due strati, due script

Il porting è diviso in due, e la divisione non è organizzativa: i due strati
rispondono a domande diverse e si rompono in momenti diversi.

| script | cosa insegna | dove tocca |
|---|---|---|
| `applica.py` | **come si parla col sistema** | `src/rtlib/` |
| `bersaglio-exos.py` | **che esiste un bersaglio `exos`** — FreeBASIC 1.07.3 | `src/compiler/`, `inc/crt/` |
| `bersaglio-exos-110.py` | lo stesso, per FreeBASIC **1.10.1** | idem |

Lo strato di runtime è scritto in C e cambia raramente: dalla 1.07.3 alla
1.10.1 le sue tre ancore reggono tutte e tre. Lo strato del compilatore è
scritto in FreeBASIC e cambia a ogni versione: delle 26 ancore della 1.07.3
sulla 1.10.1 ne reggono 14.

### Perché script e non file di patch

Una patch `diff` porta con sé i numeri di riga e il contesto esatto: applicata
a un albero anche di poco diverso, o fallisce del tutto o — peggio — riesce
con offset e mette il codice due funzioni più in là. Questi script cercano
invece un **testo di ancoraggio** e pretendono di trovarlo **esattamente il
numero di volte previsto**: zero volte significa che upstream ha riscritto
quelle righe, più volte significa che non si sa quale sia quella giusta.

> ! In tutti e due i casi lo script si ferma e dice quale ancora. Dentro un
> **compilatore** una sostituzione finita nel posto sbagliato non dà un
> errore: dà codice generato diverso da quello che si crede, e lo si scopre
> molto più a valle.

Ogni script è **idempotente e reversibile**: riapplicarlo non fa niente,
`--togli` riporta l'albero a com'era. Il giro completo — metti, rimetti,
togli, ritogli — è stato verificato: l'albero torna **identico byte per
byte** all'originale.

---

## Strato 1 — la runtime (`applica.py`)

FreeBASIC divide la propria runtime in una parte comune e uno strato di
sistema per ogni piattaforma. Lo strato `unix/` dà per scontati `termios`, i
segnali, i thread e `_FILE_OFFSET_BITS=64`; **EX-OS non ha nessuna di quelle
quattro cose**. Lo strato `exos/` è quindi modellato su `dos/`, l'unico
scritto per un sistema senza quelle premesse.

### Tre file modificati

| file | modifica |
|---|---|
| `src/rtlib/fb_config.h` | dichiara che `exos` è una piattaforma nota e spegne le funzioni che presuppongono Unix |
| `src/rtlib/fb.h` | `#include "exos/fb_exos.h"` |
| `src/rtlib/fb_private_thread.h` | niente thread: EX-OS non ne ha |

### Nove file nuovi, tutti nostri (`src/rtlib/exos/`)

`fb_exos.h`, `fb_private_console.h`, `drv_intl.c`, `file_hlock.c`,
`hinit.c`, `io_console.c`, `sys_execex.c`, `sys_hshell.c`, `sys_paths.c`.

Sono codice originale di EX-OS, non derivato da FreeBASIC.

> ! `sys_execex.c` e `sys_hshell.c` erano **un solo file**, `sys_exec.c`.
> Sono stati separati l'11 agosto 2026 per una ragione che non si vede
> costruendo in croce: il makefile di FreeBASIC appiattisce gli oggetti in
> una sola directory tramite `VPATH`, e `sys_exec.c` collideva col
> `sys_exec.c` di un altro strato. Il primo dei due vinceva, e il secondo
> spariva senza un messaggio. `applica.py` ora ha una guardia contro le
> collisioni di nome e cancella i file rimasti da versioni precedenti.

---

## Strato 2 — il bersaglio nel compilatore

Qui sotto tutte le modifiche, raggruppate per ciò che ottengono. I numeri di
riga cambiano fra le versioni; il **perché** no.

### 2.1 — Dichiarare che `exos` esiste

**`src/compiler/fb.bi`** — due modifiche.

1. `FB_COMPTARGET_EXOS` entra nell'enum dei bersagli.

   > ! **IN FONDO, NON IN MEZZO.** Il valore numerico di questi elementi
   > finisce dentro i `.asm` del bootstrap. Infilarne uno a metà rinumera
   > tutti quelli che seguono, e un bootstrap già generato comincia a parlare
   > di un bersaglio per un altro — senza nessun errore.

   Nella 1.07.3 l'ultima voce era `FB_COMPTARGET_NETBSD`; nella 1.10.1 è
   `FB_COMPTARGET_JS`.

2. Il blocco `#elseif defined(__FB_EXOS__)` che dà `FB_HOST_EXEEXT`,
   `FB_HOST_PATHDIV` e `FB_DEFAULT_TARGET` quando fbc **gira** su EX-OS.
   `__FB_EXOS__` non va dichiarato da nessuna parte: lo definisce
   `symb-define.bas` partendo dall'identificatore del bersaglio.

**`src/compiler/fb.bas`** — la riga di `exos` nella tabella `targetinfo`:
tipo di `wchar`, convenzione di chiamata predefinita, e i flag.

```
FB_TARGETOPT_UNIX                  __FB_UNIX__ per chi compila
FB_TARGETOPT_CALLEEPOPSHIDDENPTR
FB_TARGETOPT_STACKALIGN16
FB_TARGETOPT_ELF
```

> ! **Il trattino basso davanti al commento non è decorazione.** Dentro un
> inizializzatore di tabella la riga logica prosegue, e un commento nudo cade
> dove fbc si aspetta un'espressione: risponde *«Expected expression, found
> '''»* nominando una riga che non è quella sbagliata.

> ! **Nella 1.10.1 le voci vicine hanno `FB_TARGETOPT_RETURNINFLTS`; per
> `exos` NON si mette.** L'ABI è quella di Linux x86 a 32 bit, dove i float
> tornano sullo stack dell'x87 e non nei registri interi. Copiare quel flag
> dalle voci a 64 bit qui accanto darebbe valori di ritorno sbagliati sui
> float, e senza nessun errore.

**`src/compiler/fbc.bas`** — il nome `exos` fra quelli che `-target`
riconosce, e fra i nomi accettati nei triplet in stile GNU.

### 2.2 — Come si collega un programma per EX-OS

Sempre in **`src/compiler/fbc.bas`**.

**L'emulazione da dare a `ld`:** `-m elf_i386`. EX-OS è x86 a 32 bit e basta,
quindi — a differenza di Linux — non c'è nessun `select` sulla famiglia di
CPU da fare.

**I flag di collegamento:**

```
-static -e _start -Ttext-segment=0x08000000
```

- ! **`-static` non è una preferenza**: EX-OS non ha un caricatore
  dinamico, quindi niente `-dynamic-linker` e niente `.so`.
- `-e _start` — l'ingresso lo definisce `crt0.o`. È anche il predefinito di
  `ld` per ELF, ma dirlo toglie una dipendenza da un default.
- `0x08000000` — l'indirizzo a cui EX-OS carica i programmi, lo stesso dei
  linker script di `bin/*.ld`. Il predefinito di `ld` per `elf_i386` è
  `0x08048000`, e non è quello.

**Le librerie predefinite:** `c`, `m`, `gcc`, in quest'ordine — libfb chiama
la libc, la libc chiama libgcc.

> ! **Niente `pthread`, `dl`, `ncurses`, `tinfo`.** La runtime per EX-OS è
> costruita senza thread e senza terminfo. Chiederle qui darebbe un
> collegamento che fallisce su librerie inesistenti, con un messaggio che
> parla di simboli mai visti invece che della loro assenza.

**Dove cercare i file di supporto:** `<prefisso>/lib`, dove stanno `crt0.o`,
`libc.a`, `libm.a` e `libgcc.a`.

> ! **A `gcc` non si chiede niente.** Sulle altre piattaforme fbc interroga
> il driver di GCC per farsi dire dove sono i file di avvio. Dentro EX-OS
> quella strada passa da `popen()`, e **l'output di un figlio non torna
> indietro**: la risposta sarebbe vuota, e l'errore comparirebbe al link su
> un file mancante. L'albero degli strumenti di EX-OS è completo per
> costruzione, quindi si guarda lì e si smette.

**Non si chiede se il linker è `gold`.** `fbcIsUsingGoldLinker()` lancia `ld
--version` e ne legge la prima riga: di nuovo `popen()`, di nuovo `/bin/sh`,
per rispondere a una domanda la cui risposta è nota. Un processo in più a ogni
collegamento, per niente.

### 2.3 — Gli header della nostra libc (`inc/crt/`)

Otto smistatori (`sys/types.bi`, `time.bi`, `stdio.bi`, `stdlib.bi`,
`ctype.bi`, `fcntl.bi`, `wchar.bi`, `errno.bi`) prendono un ramo
`#elseif defined(__FB_EXOS__)` che punta a `crt/exos/…`.

I file veri stanno in `tools/freebasic-exos/crt-exos/` e sono **nostri**: non
si aliasano a quelli di Linux, perché descrivono la libc di EX-OS. Li copia
lo script in `inc/crt/exos/` e `inc/crt/sys/exos/`.

`errno.bi` prende due modifiche invece di una: oltre allo smistatore, la sua
tabella generica di codici va esclusa, perché i nostri numeri non coincidono
con quelli di Linux.

> **Nella 1.10.1 `stdlib.bi` non si tocca più, e non è una dimenticanza.**
> Upstream l'ha riscritto per smistare su `__FB_UNIX__`, che `exos`
> soddisfa già (la voce `targetinfo` accende `FB_TARGETOPT_UNIX`, e
> `symb-define.bas` ne ricava `__FB_UNIX__`). Il file che ne risulta,
> `crt/unix/stdlib.bi`, dichiara esattamente ciò che dichiara il nostro: la
> sola `mkstemp`.

### 2.4 — Il codice generato (`emit_x86.bas`)

Cinque punti, e sono i più delicati di tutti perché **non falliscono
rumorosamente**: sbagliarli dà assembly che si assembla e si comporta male,
oppure che non si assembla affatto.

| punto | cosa fa | `exos` |
|---|---|---|
| sezione dei dati costanti | `.rodata` invece di `.data` | **come Linux** |
| `.size` in fondo alle procedure | | **come Linux** |
| `.type … , @function` | | **come Linux** |
| attributi delle sezioni ctor/dtor | `"aw", @progbits` | **come Linux** (2 punti) |
| direttive `.cfi_*` (unwind) | | **NO** |

Il formato oggetto è lo stesso di Linux — ELF32 i386, con lo stesso
assemblatore — quindi le prime quattro voci seguono Linux senza discussione.

#### ! L'unwind: la decisione più delicata del porting

Nella 1.07.3 il problema non si poneva: **quella versione non ha nessuna
emissione di unwind**, zero occorrenze di `.cfi_` in `emit_x86.bas`, e il
bootstrap `exos` già generato non contiene un solo `.cfi_startproc`.

La 1.10 emette le `.cfi_*` quando `FB_COMPOPT_UNWINDINFO` è acceso — e **il
makefile di FreeBASIC compila il compilatore proprio con `-e`**, che lo
accende. Peggio: nella 1.10 quel controllo passa dalla stessa variabile
`islinux` che governa `.size` e `.type`. Comprendere `exos` in `islinux` e
basta accenderebbe **anche** l'unwind, di rimbalzo e senza averlo chiesto.

Qui le due cose sono separate: `islinux` comprende `exos`, `hasunwind` resta
di Linux soltanto. La ragione è che EX-OS non ha la macchina che consuma
`.eh_frame` e il suo `crt0` non la registra: sarebbero sezioni che nessuno
legge, nel migliore dei casi.

> ! **I punti che guardano l'unwind sono TRE, non due.** Uno sta nel
> prologo, uno **dentro il corpo dell'epilogo**, uno alla testa della
> procedura. Il secondo è passato inosservato alla prima stesura, ed ereditava
> l'`islinux` allargato. Il risultato non era «codice con un po' di CFI in
> più»: era **codice che non si assembla**.
>
> ```
> u_exos.asm:26: Error: CFI instruction used without previous .cfi_startproc
> ```
>
> Il prologo — già spento per `exos` — non apriva la regione, e l'epilogo la
> chiudeva lo stesso. Due metà di un interruttore girate in senso opposto.
> L'ha trovato una prova che confronta l'assembly dei due bersagli e prova
> ad assemblarlo davvero; **non** la lettura del codice.

Chi un domani vorrà le eccezioni su EX-OS accenderà quella riga di proposito,
avendo prima messo la macchina che serve a farle funzionare.

---

## Dalla 1.07.3 alla 1.10.1

|  | 1.07.3 | 1.10.1 |
|---|---|---|
| voci nello script | 26 | **25** |
| punti toccati nei sorgenti | 26 | 26 |
| ancore riusate tali e quali | — | **14** |
| ancore rifatte | — | **11** |

Le 14 che reggono **non sono ricopiate**: `bersaglio-exos-110.py` le importa
da `bersaglio-exos.py`. Così correggerne una la corregge per tutte e due le
versioni — ed è il caso di quelle su `inc/crt/`, che descrivono la nostra libc
e non hanno niente a che vedere con la versione di FreeBASIC.

Il conto dei punti torna a 26 in tutte e due le versioni per una coincidenza
che vale la pena scrivere: **uno è sparito** (`stdlib.bi`, reso inutile da
upstream) e **uno è comparso** (il secondo `hasunwind`, che nella 1.07 non
esisteva perché non esisteva l'unwind). Una voce dello script ne copre due di
sorgente (gli attributi ctor/dtor, identici byte per byte).

### Cosa è cambiato upstream, in concreto

```
enum FB_COMPTARGET     finiva con NETBSD, ora finisce con JS
tabella targetinfo     ha FB_TARGETOPT_RETURNINFLTS in più
nomi dei triplet       hanno dragonfly, solaris, js
ldcline                dopo il caso LINUX non c'è più `end select`
controllo su gold      ha due condizioni in più (SOLARIS, JS) e rientri
                       fatti di tabulazioni invece che di spazi
emit_x86.bas           usa `islinux = (…)` invece di confronti sparsi,
                       e ha aggiunto l'emissione dell'unwind
inc/crt/stdlib.bi      smista su __FB_UNIX__ invece che su __FB_LINUX__
```

### Perché due script e non uno con degli `if`

Le ancore vecchie e nuove si somigliano abbastanza da poter combaciare nel
posto sbagliato, e il giorno che si porta una terza versione i rami
diventerebbero tre. Due file separati si leggono in `diff` uno contro
l'altro, che è come si capisce cosa è cambiato fra una versione e l'altra.

---

## Un difetto trovato negli script stessi

Mentre si verificava il giro completo sulla 1.10.1 è saltato fuori un difetto
che c'era **anche nello script della 1.07.3**, e che nessuno aveva visto
perché nessuno aveva mai controllato la rimozione fino in fondo.

Per decidere se una modifica era già stata fatta, gli script guardavano il
testo *cercato* quando toglievano e quello *messo* quando applicavano. Sembra
simmetrico, ma non lo è: **quasi tutte queste modifiche inseriscono attorno al
testo cercato invece di sostituirlo**, quindi dopo averle applicate il testo
cercato è ancora lì dentro.

Il caso che lo dimostra è `#else` contro `#elseif`: il primo è un prefisso del
secondo. Una rimozione che si fidi del testo cercato si convince di aver già
finito.

> ! Il risultato non era un errore ma **un albero tolto a metà**, che si
> compila lo stesso e sbaglia altrove. Sulla 1.07.3, su 26 modifiche ne
> restavano dentro **12**.

Ora tutti e due gli script guardano sempre il testo *messo*, in tutte e due le
direzioni. Il giro completo è verificato su tutte e due le versioni.

---

## Cosa è stato verificato

Sulla **1.10.1**, il 12 agosto 2026:

| prova | esito |
|---|---|
| applica → riapplica → togli → ritogli | albero **identico byte per byte** all'originale |
| `make compiler` dai sorgenti modificati | fbc costruito e collegato |
| `-target exos` riconosciuto | sì, produce x86 a 32 bit |
| `.size` e `.type @function` per `exos` | presenti (2 e 2 su un caso di prova) |
| `.cfi_*` per `exos` con `-e` | **0** |
| `.cfi_*` per `linux-x86` con `-e` | 14 — **invariato** |
| l'assembly di `exos` passa da `as --32` | sì |
| runtime per exos: `src/rtlib/*.c` | 421 compilati, **0 falliti** |
| runtime per exos: `src/rtlib/exos/*.c` | 7 compilati, **0 falliti** |
| `libfb.a` per exos | 575.562 byte |
| `.asm` del compilatore per exos | 145 generati, 145 assemblati, 0 falliti |
| `fbc` per exos, collegato | ELF 32-bit i386, statico, **nessun simbolo irrisolto** |
| dimensione | 1.905.988 byte, 1.643.500 dopo lo `strip` |

### E poi DENTRO EX-OS, lo stesso giorno

Tutto quanto sopra è costruzione in croce su Linux. La prova che conta è
un'altra: **la 1.10.1 costruita dall'interno**, dall'fbc 1.07.3 installato,
con GNU make e il GCC nativi. Disco ext2, 512 MB di RAM, QEMU senza KVM.

```
make TARGET_OS=exos TARGET_ARCH=x86 DISABLE_MT=1 \
     CFLAGS="-O2 -DDISABLE_FFI" rtlib
make TARGET_OS=exos TARGET_ARCH=x86 DISABLE_MT=1 compiler
```

| passo | esito |
|---|---|
| `rtlib` | 430 file compilati, **0 falliti** — `AR lib/freebasic/exos-x86/libfb.a` |
| `libfb.a` prodotta qui dentro | **576.120 byte** (in croce su Linux: 575.562 — è un altro `ar`) |
| `compiler` | **145** `.bas` compilati dall'fbc 1.07.3, 0 falliti, `LINK bin/fbc` |
| `fbc` 1.10.1 prodotto qui dentro | **1.909.644 byte** |

E il compilatore nuovo funziona:

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

385 è la somma dei quadrati da 1 a 10: un valore che si controlla a mano.

> ! **AL COMPILATORE NUOVO SERVE UN PREFISSO COMPLETO.** `fbc` ricava il
> proprio prefisso dal percorso da cui è stato lanciato: `/src/fb/bin/fbc`
> dà `/src/fb/`, e lì dentro cerca `lib/crt0.o`, `lib/libc.a`, `lib/libm.a`
> e `lib/libgcc.a` — perché così dice la modifica 2.2 di questo documento.
> L'albero dei sorgenti quei file non li ha: vanno copiati da
> `/cdrom/exos/lib/`. Senza, il collegamento fallisce su `crt0.o`, e
> l'errore parla di un file mancante invece che di un prefisso incompleto.
>
> ```
> cp /cdrom/exos/lib/crt0.o   /src/fb/lib/
> cp /cdrom/exos/lib/libc.a   /src/fb/lib/
> cp /cdrom/exos/lib/libm.a   /src/fb/lib/
> cp /cdrom/exos/lib/libgcc.a /src/fb/lib/
> ```

Due avvisi del linker compaiono e non sono difetti nostri: `fbextra.x
contains output sections` (lo script che scarta `.fbctinf`) e `cpudetect.o:
missing .note.GNU-stack section` — quest'ultimo è il motivo per cui
`prepara-fb.sh` passa `-z noexecstack` a mano quando collega in croce.

> **Il punto fisso E' CHIUSO** (12 agosto 2026): `gen2` e `gen3` sono
> identici byte per byte — 1.906.112 ciascuno, verificato con `/bin/cmp`
> dentro EX-OS; `gen1` differisce al byte 33, ed e' giusto perche' l'aveva
> prodotto un generatore di codice 1.07. Da quel momento la 1.10.1 e' la
> versione INSTALLATA. La procedura sta in
> [`PORTING-1.10.1.it.txt`](PORTING-1.10.1.it.txt).
>
> ! Nota superata, tenuta per storia: il punto fisso a tre generazioni — cioè che
> la 1.10.1 ricostruisca sé stessa fino a un binario identico. Qui la 1.10.1
> è stata costruita dalla **1.07.3** e sa compilare; chiudere il punto fisso
> vuol dire rifare `compiler` con l'fbc 1.10.1 appena prodotto e confrontare.
> Col la 1.07.3 era stato chiuso a tre generazioni, e con questa si può.

---

## Come si applica

```sh
# lo strato di runtime (vale per tutte le versioni)
python3 tools/freebasic-exos/applica.py <albero-fb>

# il bersaglio nel compilatore — SI SCEGLIE LO SCRIPT DELLA VERSIONE
python3 tools/freebasic-exos/bersaglio-exos.py     <albero-1.07.3>
python3 tools/freebasic-exos/bersaglio-exos-110.py <albero-1.10.1>

# per tornare indietro
python3 tools/freebasic-exos/bersaglio-exos-110.py <albero-1.10.1> --togli
```

`bersaglio-exos-110.py` controlla `version.mk` prima di toccare qualsiasi
cosa: su un albero 1.07 le ancore combacerebbero **in parte**, e un albero
modificato a metà è peggio di uno non modificato — il primo si compila lo
stesso.

### E dentro EX-OS

I sorgenti già modificati finiscono sul CD degli strumenti in
`/freebasic-nuovo`, con un `BERSAGLIO-EXOS.txt` che ripete i comandi. Sono
gli stessi della 1.07.3 — il makefile di FreeBASIC non è stato toccato:

```
make TARGET_OS=exos TARGET_ARCH=x86 DISABLE_MT=1 \
     CFLAGS="-O2 -DDISABLE_FFI" rtlib
make TARGET_OS=exos TARGET_ARCH=x86 DISABLE_MT=1 compiler
```

> ! **La variabile è `TARGET_OS`, non `TARGET`.** `TARGET` esiste, ma il
> makefile lo interpreta come un *triplet* in stile GNU e lo cerca in una
> tabella dove `exos` non c'è: il risultato è `BUILD_PREFIX=exos-` — cioè
> `exos-gcc`, `exos-ar`, che non esistono — e `TARGET_OS` **vuoto**. Con
> `TARGET_OS` vuoto il makefile cerca la runtime nella sola `src/rtlib/`,
> salta `src/rtlib/exos/`, e il link fallisce su funzioni che non c'entrano
> con il file che stava compilando.

> ! **`TARGET_ARCH=x86` serve quanto l'altra.** Dentro EX-OS il makefile
> ricava l'architettura da `uname -m`, e la nostra risposta non è fra quelle
> che riconosce.

> ! **`DISABLE_MT=1` non è un'ottimizzazione.** Senza, il makefile costruisce
> anche `libfbmt.a`: altri 427 file, quasi tre ore, per una runtime con i
> thread che EX-OS non ha.

> ! **`-DDISABLE_FFI` serve.** `src/rtlib/thread_call.c` include `<ffi.h>`;
> libffi su EX-OS non c'è, e il define riduce quel file a uno stub — come il
> makefile fa da sé per Xbox e per DOS.

Verificato sulla 1.10.1 con `make -n`: le sette sorgenti di
`src/rtlib/exos/` entrano nel piano di costruzione, `libfbmt.a` no.

> ! **`/freebasic` (la 1.07.3) non va sostituito con `/freebasic-nuovo`.** È
> l'albero con cui si è chiuso il punto fisso a tre generazioni, e l'unico
> riferimento contro cui confrontarsi se il nuovo si comporta in modo strano.

# FreeBASIC su EX-OS

> **Il lavoro in corso — il bersaglio `exos` e il bootstrap — sta in
> [`bootstrap-exos.md`](bootstrap-exos.md).** Questo file racconta com'è
> fatto il porting di oggi; quello dice cosa manca e in che ordine farlo.
>
> **L'elenco puntuale di cosa viene cambiato dentro i sorgenti di FreeBASIC,
> e il perché di ogni punto, sta in [`MODIFICHE-FBC.md`](MODIFICHE-FBC.md)**
> — [English](MODIFICHE-FBC.en.md). È anche il documento da leggere per
> portare una versione nuova, e quello che serve per rispettare la GPL v2
> sapendo esattamente cosa è stato toccato.

`fbc` — il compilatore FreeBASIC — **gira dentro EX-OS** e compila
programmi che girano dentro EX-OS. Questa directory contiene ciò che è
nostro; i sorgenti di FreeBASIC no, come per GCC.

## Lo stato, provato il 10 agosto 2026

**Un comando solo**, dentro EX-OS:

```
fbc prova.bas -x prova
prova
prova-fb — compilato dentro EX-OS
  saluto      EX-OS e FreeBASIC
  quadrati     385
  esito       tutto a posto
```

385 è la somma dei quadrati da 1 a 10: un valore che si controlla a mano.

`fbc` sa di produrre per EX-OS — `built for exos-x86 (32bit)` — perché il
bersaglio `exos` è nei sorgenti del compilatore e il bootstrap è stato
rigenerato. Come si è fatto, e come si rifà, sta in
[`bootstrap-exos.md`](bootstrap-exos.md).

! **`-o` non è quello di gcc**, ed è il primo inciampo. Dall'aiuto di
`fbc` stesso:

```
-o <file>   Set .o (or -pp .bas) file name for prev/next input file
-x <file>   Set output executable/library file name
```

`fbc prova.bas -o prova` chiede di chiamare `prova` **l'oggetto**, e resta
sul disco un ELF rilocabile: EX-OS risponde «non e' un programma
eseguibile», ed è vero.

! **Un `#include "crt.bi"` non compila ancora**: gli header `.bi` del C si
diramano su `__FB_LINUX__` e non conoscono `__FB_EXOS__`. È il lavoro che
resta, e non si chiude aliasando i due — `time_t` è di 8 byte qui e di 4 su
Linux x86. Il conto per esteso è in `bootstrap-exos.md`.

### Il link a mano, se serve ancora

Non serve più, ma resta utile per capire cosa fa `fbc` e per rimettere
insieme i pezzi a mano:

```
fbc -c -m t /t.bas -o /t.o
ld -static -e _start -Ttext-segment=0x08000000 -o /t \
   /cdrom/exos/lib/crt0.o \
   /cdrom/exos/lib/freebasic/exos-x86/fbrt0.o \
   /t.o \
   /cdrom/exos/lib/freebasic/exos-x86/libfb.a \
   /cdrom/exos/lib/libc.a /cdrom/exos/lib/libm.a /cdrom/exos/lib/libgcc.a
```

! La directory della runtime si chiama come il bersaglio: era
`linux-x86`, dal 10 agosto è **`exos-x86`**.

## Perché non serve un fbc per costruire fbc

Il compilatore FreeBASIC è scritto in FreeBASIC. Il pacchetto
**source-bootstrap** rompe il cerchio: si porta dietro i sorgenti del
compilatore già tradotti — in assembly per i bersagli a 32 bit
(`bootstrap/<target>/*.asm`, 145 file), in C per quelli a 64 bit.

! Il nostro `bootstrap/exos-x86/` **non è nel pacchetto**: si genera, ed è
il lavoro descritto in [`bootstrap-exos.md`](bootstrap-exos.md).

Quell'assembly è i386 GAS in sintassi intel e chiama solo la runtime e la
libc: **il nostro `as` lo assembla tale e quale, 145 su 145**. Da lì il
compilatore per EX-OS si costruisce senza avere niente.

## Che cosa mancava, e dove è finito

Il conto è stato fatto misurando, non stimando: assemblati i 145 oggetti,
i simboli irrisolti erano **124** — 7 dalla libc, 4 da libgcc, ~110 dalla
runtime. Di quei 110 la stragrande maggioranza è codice comune di
FreeBASIC, che compila così com'è: **418 file su 420**.

| dove | cosa |
|---|---|
| `exos/` | lo strato di sistema della runtime: 6 file, ~450 righe |
| `lib/libc.c` | `<wchar.h>` e `<wctype.h>` veri (26 funzioni), `alloca`, `NSIG` |
| `lib/libc.c` | `system()`, `popen()`, `pclose()` — prima erano `ENOSYS` |
| `lib/libc.c` | `spawn()` cerca nel PATH un nome senza barre, come `execvp` |
| `bin/sh/` | `sh -c "comando"`, e la shell che vede i propri argomenti |

! **Le ultime due righe non sono dettagli di FreeBASIC**: valgono per
ogni programma di terzi portato qui. `fbc` lancia l'assemblatore
chiamandolo `as` e basta — come fa il driver di GCC, come fa `make` — e
un programma non ha una shell dietro che gli cerchi il PATH.

## I due file della runtime che restano fuori

- `thread_call.c` vuole **libffi**, per costruire chiamate con firma
  decisa a tempo di esecuzione. Su EX-OS non c'è.
- I thread veri (`THREADCREATE` e compagne) non ci sono: un programma che
  li usa **non si collega**, e il messaggio nomina la funzione. È meglio
  di uno stub che ritorna NULL e lascia credere di aver creato qualcosa.

`thread_ctx.c` invece entra: ha già un ramo per i sistemi senza thread, e
da lì arrivano `fb_TlsGetCtx` e `fb_TlsFreeCtxTb`.

## ! fbc credeva di produrre per Linux — CHIUSO il 10 agosto 2026

Resta qui perché la diagnosi è servita e perché il sintomo tornerà a
somigliare a qualcos'altro, se il bersaglio si rompe di nuovo.

I `.asm` del bootstrap del pacchetto sono generati con `-target linux-x86`,
quindi l'`fbc` che ne usciva aveva «linux-x86» scritto dentro. Reggeva
quasi tutto — l'ABI ELF32 i386 è la stessa — ma non il link.

! **E non era «lo affida a gcc», come si è scritto per giorni: `fbc` chiama
`ld` DIRETTAMENTE** (`fbcRunBin( "linking", FBCTOOL_LD, … )`). Il problema
era cosa gli metteva sulla riga di comando per il bersaglio Linux:

| riga di `fbc.bas` | cosa aggiungeva | perché qui non andava |
|---|---|---|
| 837 | `crt1.o` | noi abbiamo `crt0.o` |
| 692 | `-dynamic-linker /lib/ld-linux.so.2` | EX-OS collega statico |
| 845-926 | `crti.o`, `crtbegin.o`, `crtend.o`, `crtn.o` | roba della glibc |

! **Il sintomo non somigliava alla causa.** Il primo `hFindLib()` che non
trovava il proprio file chiamava `errReportEx()` e si usciva **prima** di
stampare la riga `linking:`. Con `-v` si vedeva `assembling:` e poi più
niente: sembrava rotto l'assemblatore, che invece aveva finito con 0.

La cura è stata aggiungere il bersaglio `exos` ai sorgenti del compilatore
e rigenerare il bootstrap — `bersaglio-exos.py` e
[`bootstrap-exos.md`](bootstrap-exos.md).

## Uso

```bash
# i sorgenti: pacchetto source-bootstrap da freebasic.net, in ./FreeBASIC-*/
tools/freebasic-exos/prepara-fb.sh            # -> ~/fb-build-exos
make iso                                      # -> /exos/bin/fbc sul CD
```

! **fbc STA IN /exos/bin, NON IN /bin** (dal 6 agosto 2026), e non e' una
questione di ordine: fbc calcola il proprio prefisso come *directory
dell'eseguibile meno `bin`*, e da li' cerca

    <prefisso>/include/freebasic/        i .bi
    <prefisso>/lib/freebasic/<target>/   libfb.a, fbrt0.o

Da `/bin` il prefisso sarebbe `/` e non troverebbe niente. Da `/exos/bin`
trova tutto da solo, ovunque sia montato il CD — cosa che a GCC riesce solo
con `GCC_EXEC_PREFIX` o `-B`, perche' li' i percorsi sono compilati dentro.

! **Dei 31 MB di `inc/` ne vanno sul CD 452 KB**: `crt.bi`, `crt/`,
`fb*.bi`, `fbc-int/`. Il resto sono binding ad allegro, GTK, SDL, X11,
zlib — librerie che su EX-OS non esistono, e un header che promette e' peggio
di un header assente: compila, e il link fallisce con simboli mai visti.

La prova che l'albero e' a posto e' `tools/iso/prova-fb2.bas`, il gemello
di `prova-fb.bas` **con** un `#include`.

`applica.py <albero-fb>` mette il bersaglio dentro l'albero (e `--togli`
lo riporta com'era); `prepara-fb.sh` lo chiama da solo.

## Che cosa cambia nell'albero di FreeBASIC

| file | modifica |
|---|---|
| `src/rtlib/fb_config.h` | `__exos__` → `HOST_EXOS` |
| `src/rtlib/fb.h` | include `exos/fb_exos.h` |
| `src/rtlib/fb_private_thread.h` | `struct _FBTHREAD` anche senza thread |
| `src/rtlib/exos/` | **nuova**: lo strato di sistema |

! **Non si definisce `HOST_UNIX`.** Quella macro tira dentro termios, i
segnali, i thread e `_FILE_OFFSET_BITS=64`, e di quei quattro EX-OS non ha
niente. Lo strato in `exos/` è modellato su `dos/`, che è l'unico scritto
per un sistema a un flusso solo.

## Licenze

Il compilatore `fbc` è **GPL v2**; la runtime (`src/rtlib/`) è **LGPL
v2.1 con eccezione di collegamento**, ed è ciò che permette a un programma
FreeBASIC di non essere costretto alla GPL.

Per questo i file nuovi sotto `exos/` sono **LGPL-2.1-or-later**, cioè la
licenza della runtime in cui entrano — e non GPL: una runtime con dentro
un pezzo GPL costringerebbe alla GPL ogni programma compilato con `fbc`,
che è esattamente ciò che l'eccezione esiste per evitare.

EX-OS resta `GPL-2.0-or-later`. Chi ridistribuisce deve fornire i sorgenti
corrispondenti di FreeBASIC e conservarne le note di copyright.

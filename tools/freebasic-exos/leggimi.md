# FreeBASIC su EX-OS

`fbc` — il compilatore FreeBASIC — **gira dentro EX-OS** e compila
programmi che girano dentro EX-OS. Questa directory contiene ciò che è
nostro; i sorgenti di FreeBASIC no, come per GCC.

## Lo stato, provato

```
fbc -c -m prova /src/prova.bas -o /src/pbas.o
ld -static -e _start -Ttext-segment=0x08000000 -o /src/pbas \
   /cdrom/exos/lib/crt0.o /cdrom/exos/lib/fbrt0.o /src/pbas.o \
   /cdrom/exos/lib/libfb.a /cdrom/exos/lib/libc.a \
   /cdrom/exos/lib/libm.a /cdrom/exos/lib/libgcc.a
/src/pbas
```

```
prova-fb — compilato dentro EX-OS
  saluto      EX-OS e FreeBASIC
  quadrati     385
  esito       tutto a posto
```

385 è la somma dei quadrati da 1 a 10: un valore che si controlla a mano.

## Perché non serve un fbc per costruire fbc

Il compilatore FreeBASIC è scritto in FreeBASIC. Il pacchetto
**source-bootstrap** rompe il cerchio: si porta dietro i sorgenti del
compilatore già tradotti — in assembly per i bersagli a 32 bit
(`bootstrap/linux-x86/*.asm`, 145 file), in C per quelli a 64 bit.

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

⚠️ **Le ultime due righe non sono dettagli di FreeBASIC**: valgono per
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

## ⚠️ fbc crede di produrre per Linux, e va detto

I `.asm` del bootstrap sono stati generati con `-target linux-x86`, quindi
l'`fbc` che ne esce ha «linux-x86» scritto dentro come bersaglio
predefinito. Regge quasi tutto — EX-OS condivide con Linux l'ABI ELF32
i386 — ma **il link finale no**: `fbc` lo affida a `gcc` con opzioni
Linux, e lì si ferma. Per questo il link si fa a mano, come sopra.

Il passo che chiude anche quello è aggiungere un bersaglio `exos` ai
sorgenti `.bas` del compilatore (entry `_start`, `0x08000000`, niente PIE,
`fbrt0.o` e `libfb.a` da `/exos/lib`) e rigenerare il bootstrap. Serve un
`fbc` che giri **su Linux**, e si costruisce dallo stesso pacchetto con
`make bootstrap` — i prerequisiti (`ncurses`, `libffi`) ci sono già.

## Uso

```bash
# i sorgenti: pacchetto source-bootstrap da freebasic.net, in ./FreeBASIC-*/
tools/freebasic-exos/prepara-fb.sh            # -> ~/fb-build-exos
make iso                                      # -> /bin/fbc sul CD
```

`applica.py <albero-fb>` mette il bersaglio dentro l'albero (e `--togli`
lo riporta com'era); `prepara-fb.sh` lo chiama da solo.

## Che cosa cambia nell'albero di FreeBASIC

| file | modifica |
|---|---|
| `src/rtlib/fb_config.h` | `__exos__` → `HOST_EXOS` |
| `src/rtlib/fb.h` | include `exos/fb_exos.h` |
| `src/rtlib/fb_private_thread.h` | `struct _FBTHREAD` anche senza thread |
| `src/rtlib/exos/` | **nuova**: lo strato di sistema |

⚠️ **Non si definisce `HOST_UNIX`.** Quella macro tira dentro termios, i
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

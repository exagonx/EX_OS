# Il bersaglio `i386-exos` per GCC

Questa directory contiene il **port di GCC a EX-OS**. Non contiene GCC:
contiene ciò che va aggiunto a un albero dei sorgenti di GCC perché sappia
produrre binari per EX-OS.

## Perché il port sta qui e i sorgenti no

L'albero di GCC è **692 MB di codice di terzi** che cambiano a ogni
aggiornamento upstream. Dentro la cronologia di questo repository lo
renderebbero impraticabile da clonare, e ogni `git status` mostrerebbe
migliaia di file che non sono nostri. Perciò `gcc/` è in `.gitignore`, e
qui resta la parte che *è* nostra — quella che, se si perde, va riscritta.

```
applica.py           mette (o toglie) il bersaglio in un albero GCC
exos.h               il file nuovo: predefiniti e specs del bersaglio
prepara-cross.sh     binutils e ambiente del bersaglio (crt0, libc.a, header)
```

## Uso

```bash
# 1. i prerequisiti (una volta sola)
sudo apt install flex bison gperf texinfo m4 \
                 libgmp-dev libmpfr-dev libmpc-dev

# 2. il bersaglio dentro l'albero GCC
python3 tools/gcc-exos/applica.py gcc

# 3. binutils e ambiente del bersaglio
tools/gcc-exos/prepara-cross.sh              # default: ~/exos-cross

# 4. il compilatore — NON in /tmp, vedi sotto
mkdir -p ~/gcc-build && cd ~/gcc-build
PATH=$HOME/exos-cross/bin:$PATH \
  /percorso/di/gcc/configure \
    --target=i386-exos --prefix=$HOME/exos-cross \
    --enable-languages=c --without-headers --with-newlib \
    --disable-nls --disable-shared --disable-threads \
    --disable-libssp --disable-libgomp --disable-libquadmath \
    --disable-libatomic --disable-libvtv --disable-libstdcxx \
    --disable-bootstrap
make -j$(nproc) all-gcc && make install-gcc
```

`applica.py gcc --togli` riporta l'albero com'era: serve per rigenerare le
modifiche dopo un aggiornamento di upstream senza riclonare tutto.

## ! La directory di build non va in `/tmp`

Su una Debian recente `/tmp` è una **tmpfs**, cioè RAM: qui sono 1,9 GB. La
build di GCC ne occupa 1,9 GB di oggetti e poi ne chiede ancora per i link
finali, quindi riempie il disco **esattamente all'ultimo passo** — dopo
aver compilato tutti e ~1400 i file. Il sintomo è

```
/usr/bin/ld: link finale non riuscito: Spazio esaurito sul device
make: *** [Makefile:4746: all-gcc] Error 2
```

su `cc1`, `lto1` e `lto-dump`, mentre `xgcc` — che è piccolo e viene
linkato prima — riesce. Ed è la combinazione più ingannevole possibile: il
driver del compilatore esiste, quindi `i386-exos-gcc --version` risponde,
ma non c'è il compilatore vero dietro.

Da qui la regola di verifica che vale sempre: **non fidarsi del codice di
uscita di `make`, controllare che `gcc/xgcc` e `gcc/cc1` esistano
entrambi.** Un bersaglio parziale può uscire zero.

Se ci si è già cascati, la build **non** va rifatta da zero: i Makefile
generati non contengono riferimenti assoluti alla propria directory, quindi

```bash
cp -a /tmp/gcc-build ~/gcc-build && rm -rf /tmp/gcc-build
cd ~/gcc-build && make -j$(nproc) all-gcc
```

riprende dai link mancanti e non ricompila niente.

## Perché uno script invece di una patch

Una patch a contesto contro il **trunk** di GCC scade in giorni: basta che
qualcuno tocchi una riga vicina alle nostre. `applica.py` sostituisce
stringhe esatte, quindi sopravvive a tutto ciò che non tocca proprio quelle
righe — e quando invece le tocca **lo dice**, invece di applicarsi a metà.
È anche idempotente: rilanciarlo non fa danni.

## Che cosa cambia nell'albero GCC

| File | Modifica |
|---|---|
| `config.sub` | `exos*` fra i sistemi operativi ammessi |
| `gcc/config.gcc` | il caso `i[34567]86-*-exos*` e la catena di header |
| `libgcc/config.host` | libgcc per il bersaglio, come il caso `elf` |
| `gcc/config/i386/exos.h` | **nuovo**: predefiniti `__exos__`, specs di link |

Cosa mette in un posto solo `exos.h`, che oggi ogni programma di `/bin`
rifà a mano nel proprio linker script: indirizzo di caricamento
`0x08000000`, ingresso `_start`, binario statico, **niente PIE** (il
caricatore ELF del kernel non riloca i programmi normali: un binario PIE
salterebbe nel vuoto), niente `.eh_frame`, e `crt0.o` più `-lc` aggiunti
da soli al link.

Il risultato è che `i386-exos-gcc programma.c -o programma` basta: niente
`-ffreestanding`, niente `-m32`, niente script di link scritto a mano.

## I binutils: non sono più wrapper (dal 0.150)

Erano otto script di tre righe attorno agli strumenti di sistema forzati a
32 bit, e bastavano: il formato di uscita è ELF32 i386, esattamente quello
che l'`as` e l'`ld` di sistema producono con `--32` e `-m elf_i386`.

Ora ci sono i binutils **veri** per `i386-exos`, e si costruiscono con
`tools/binutils-exos/prepara-binutils.sh` — quattro righe di modifica in
quattro file, documentate in `tools/binutils-exos/leggimi.md`. I wrapper
restano come ripiego, e `prepara-cross.sh` **non li reinstalla sopra i
binari** se li trova già.

Detto com'era previsto qui: *«otto file di tre righe lasciano il posto ai
binari e nient'altro cambia»*. È andata proprio così.

## Licenze

I file modificati e `exos.h` **appartengono a GCC** e sono sotto **GNU GPL
versione 3 o successiva**: le modifiche non possono che essere GPLv3+, e
`applica.py` marca ogni file toccato con la dichiarazione di modifica e la
data che la GPLv3 §5(a) richiede a chi distribuisce una versione
modificata.

**EX-OS resta `GPL-2.0-or-later`.** Non serve cambiare: è il *«or later»* a
rendere possibile la combinazione con codice GPLv3. Passare EX-OS alla v3
non darebbe nulla in cambio e chiuderebbe la porta al codice GPLv2-only —
per esempio ai driver del kernel Linux.

Chi ridistribuisce il port deve fornire i sorgenti corrispondenti di GCC,
conservare le note di copyright e i testi di licenza originali
(`COPYING3`, `COPYING.RUNTIME`) e mantenere le dichiarazioni di modifica.

## Stato: il cross funziona, ed è provato dentro EX-OS

`i386-exos-gcc (GCC) 17.0.0`, con `libgcc.a` per il bersaglio. La prova è
`tools/gcc-exos/prova.c`, compilato **senza una sola opzione**:

```
i386-exos-gcc tools/gcc-exos/prova.c -o build/bin/provagcc
```

ed eseguito dentro EX-OS in QEMU:

```
provagcc — compilato con i386-exos-gcc
  __exos__     definito dal bersaglio
  argc         3
  argv[0]      /bin/provagcc
  argv[1]      uno
  argv[2]      due
  chiamata indiretta: 6+7=13  6*7=42
  malloc       libc.a linkata dal bersaglio
  esito        tutto a posto
```

Le tre cose che una compilazione riuscita **non** garantisce, e che solo
l'esecuzione dimostra: l'avvio (`crt0.o` linkato da solo, `argv` passato),
la libreria (`printf` e `malloc` da `libc.a`, aggiunta dal bersaglio con
`-lc`) e l'ABI (chiamata indiretta e aritmetica).

! **Il floppy non si rigenera da solo.** `provagcc` non è un target di
`make` — lo produce il cross, che non tutti hanno — quindi la regola del
floppy non dipende da lui. Dopo aver ricompilato con il cross serve un
`make floppy` esplicito, altrimenti in QEMU gira il binario di prima. Ci
sono cascato: la prova diceva `__exos__ ASSENTE` mentre il compilatore lo
definiva correttamente, e il colpevole era l'immagine vecchia.

Prerequisiti da non dimenticare: `flex`, `bison`, `gperf`, `texinfo`, `m4`.
L'albero è un checkout del trunk e, a differenza di un tarball di rilascio,
non contiene i file generati (`gengtype-lex.cc` e i parser).

## I tre difetti del bersaglio, corretti insieme (0.150)

Erano annotati qui come noti, e costavano tutti e tre un rebuild di GCC —
le specs e i tipi sono compilati dentro il driver e dentro cc1, quindi
tanto valeva farli in un colpo solo.

- **Indirizzo di caricamento**: era `0x08048000`, il default storico di
  `ld`, mentre `exos.h` dichiarava a parole `0x08000000` e gli script di
  link di `/bin` usano quello. Ora `LINK_SPEC` passa
  `-Ttext-segment=0x08000000`. Il caricatore del kernel accettava
  entrambi, quindi non si vedeva: si vedeva solo che due binari dello
  stesso programma — uno dal cross, uno dal Makefile — stavano a indirizzi
  diversi, il che rende inconfrontabili due disassemblati.
- **`.eh_frame`**: 5,6 KB per binario. `DWARF2_UNWIND_INFO 0` toglie
  l'unwind delle *eccezioni*, non le tabelle *asincrone* — sono due cose
  diverse che si chiamano quasi uguale. Ora `CC1_SPEC` passa
  `-fno-asynchronous-unwind-tables`.
- **I tipi fondamentali**: il bersaglio prendeva i predefiniti, cioè
  `long unsigned int` per `size_t` e `long int` per `int32_t` — larghezze
  giuste, tipi diversi da quelli del `gcc` di sistema con `-m32`. Ora
  `exos.h` dichiara `SIZE_TYPE`, `PTRDIFF_TYPE`, `INT32_TYPE` e
  `UINT32_TYPE` come fa `i386-linux`, che è il bersaglio con cui EX-OS
  condivide l'ABI.

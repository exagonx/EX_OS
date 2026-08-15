# binutils per il bersaglio `i386-exos`

Come `tools/gcc-exos/`, questa directory **non contiene binutils**:
contiene ciò che va aggiunto a un albero dei sorgenti perché sappia
produrre e manipolare oggetti per EX-OS.

```
applica.py             mette (o toglie) il bersaglio in un albero binutils
prepara-binutils.sh    configure + make + install + verifica (cross)
pex-exos.c             "lancia un programma e aspettalo" senza fork
```

## Uso

```bash
# 1. i sorgenti (questo script NON scarica niente, di proposito)
wget http://deb.debian.org/debian/pool/main/b/binutils/binutils_2.44.orig.tar.xz
tar xf binutils_2.44.orig.tar.xz

# 2. costruzione e installazione
tools/binutils-exos/prepara-binutils.sh ~/exos-cross binutils-2.44
```

Provato con **binutils 2.44**.

## Che cosa cambia nell'albero

Quattro righe, in quattro file:

| File | Modifica |
|---|---|
| `config.sub` | `exos*` fra i sistemi operativi ammessi |
| `bfd/config.bfd` | `i[3-7]86-*-exos*` → `i386_elf32_vec` |
| `gas/configure.tgt` | `i386-*-exos*` → `fmt=elf` |
| `ld/configure.tgt` | `i[3-7]86-*-exos*` → emulazione `elf_i386` |

Non serve altro, e vale la pena dire perché: EX-OS **non ha un formato
eseguibile proprio** né convenzioni di rilocazione diverse da quelle di un
ELF i386 qualunque. La differenza fra `elf` ed `exos` sta nel
**compilatore** — indirizzo di caricamento, `crt0.o`, `-lc` aggiunto da
solo: vedi `tools/gcc-exos/exos.h` — non negli strumenti che manipolano gli
oggetti.

## Perché sostituire i wrapper

Fino alla 0.150 `i386-exos-as` e `i386-exos-ld` erano script di tre righe
attorno agli strumenti di sistema forzati a 32 bit. Bastava, finché si
compilava **su** Linux **per** EX-OS. Tre cose cambiano:

1. `ld` non conosceva il bersaglio, quindi non aveva un'emulazione
   predefinita per lui: ogni link dipendeva dal `-m elf_i386` passato da
   GCC, e chi invocava `ld` a mano se lo dimenticava.
2. `nm`, `objdump`, `readelf` funzionavano **per coincidenza**, perché il
   formato combacia. Il giorno che EX-OS avesse una convenzione propria
   smetterebbero senza dirlo.
3. Soprattutto: un binutils **nativo** — quello che girerà dentro EX-OS —
   si costruisce solo a partire da uno **cross**. Questo è il primo dei due
   passi.

## Binutils nativi

`--host=i386-exos --target=i386-exos`, cioè `as` e `ld` che girano
**dentro** EX-OS.

```bash
cd ~/exos-native/build-nativi
CC="i386-exos-gcc -std=gnu17" ../binutils-2.44/configure \
    --build=x86_64-pc-linux-gnu --host=i386-exos --target=i386-exos \
    --prefix=/usr --disable-nls --disable-werror \
    --disable-shared --enable-static \
    --disable-plugins --disable-gprofng \
    --without-zstd --without-msgpack --without-debuginfod
make -j2                      # ! -j2, non -j$(nproc): 4 GB di RAM
```

### Il TLS: risolto dalla 0.154, e vale la pena sapere com'era

Fino al kernel 0.153 questo configure andava lanciato con
`export ac_cv_tls=` (stringa **vuota**, non `none`), e senza `as` moriva
alla terza istruzione di `bfd_init`:

```
[FAULT] PID 9 '/cdrom/bin/as': page fault a 0x00000000 (lettura, EIP=0x0804fbe3)
 804fbe3:  65 8b 1d 00 00 00 00    mov %gs:0x0,%ebx
```

`bfd` dichiara `static TLS bfd_error_type bfd_error` e il compilatore
emette l'accesso attraverso `%gs`, che su EX-OS non puntava a niente.

! **La trappola era che la prova del configure è una COMPILAZIONE**, non
un'esecuzione: `i386-exos-gcc` accetta `_Thread_local` senza fiatare,
perché è il compilatore a saper emettere quegli accessi ed è il *sistema* a
non avere dove puntarli. Nessun errore, nessun avviso, un binario che si
costruisce benissimo e muore appena parte.

Dalla **0.154 EX-OS ha un thread pointer** (modello local-exec, vedi
`README.md`), quindi non serve più disattivare niente: `bfd` usa
`_Thread_local` davvero e funziona. Chi dovesse tornare su un kernel
precedente ricordi che il valore giusto era la stringa **vuota** e non
`none` — con `none` la macro non viene definita affatto, e binutils 2.44
non ha un ripiego (`bfd.c:802: unknown type name 'TLS'`).

! **`-std=gnu17` non è un vezzo.** GCC 17 compila in C23, dove una
dichiarazione implicita è un **errore**, non un avviso — e binutils 2.44 è
pieno di codice che presume l'indulgenza di C17. Senza, si passa il tempo
a rincorrere errori che non sono difetti.

### Che cosa serviva alla libc, in ordine di scoperta

Il configure nativo è il primo collaudo vero della libc: `libctest` chiama
le funzioni che sappiamo di avere, binutils chiama quelle che gli servono.
L'elenco è nella sessione 2026-08-02 (g) di `HANDOFF.md` — `dup`/`dup2`/
`fcntl`, `EOF`, `frexp`, `strftime`, `strcasecmp`, `mbstowcs`,
`chmod`/`umask`, `<sys/types.h>`, `<strings.h>`, `<wchar.h>`,
`<sys/param.h>` — e vale la pena tenerlo, perché è l'ordine in cui le
chiederà il prossimo sorgente esterno.

! **La libc va rimessa nel sysroot E il configure va rifatto**, ogni
volta che cresce. `libiberty` compila una **propria** copia delle funzioni
che il sistema ospite non ha (`strcasecmp.o`, `strdup.o`…) in base a ciò
che il configure ha trovato: se poi la libc quella funzione ce l'ha, il
link fallisce con `multiple definition`. Non basterebbe nemmeno un ordine
diverso delle librerie, perché **`libc.a` è un solo oggetto**: quando il
linker lo tira dentro per una `printf`, si porta dietro tutto.

### pex-exos.c

`libiberty` compila sempre un `pex-*.c` — «lancia un programma e
aspettalo» — e per tutto ciò che non è Windows o MSDOS sceglie
`pex-unix.c`, costruito su `fork()`. EX-OS non ha `fork`: ha `spawn_ex()`,
che il figlio lo crea già fatto, con le redirezioni dichiarate **per
percorso**.

`pex-exos.c` è il rimpiazzo, modellato su `pex-msdos.c` e non su
`pex-unix.c`: un «descrittore» è un indice in una tabella di nomi, perché
è il nome ciò che serve a `spawn_ex`. ! I tre `NULL` nella tabella
`funcs` — pipe, fdopenr, fdopenw — **sono la dichiarazione che questo
sistema non ha pipe**: `pex-common.c` se ne accorge da solo e passa alla
modalità a file temporanei.

`applica.py` lo installa e lo aggancia in tre punti: `libiberty/configure`
(! **il generato**, non solo `configure.ac`, che nessuno rigenera),
`configure.ac` per coerenza, e `Makefile.in` — dove ci vuole una regola
**esplicita**, perché la regola implicita di libiberty per i `.c` è
`false`, apposta.

### Metodo

`make -k` invece di un errore per volta: non si ferma al primo e li
raccoglie tutti. Su `libiberty` ne ha dati nove in un colpo. Ogni giro
completo — correggi la libc, `make all`, `prepara-cross.sh`, riparti —
costa dieci minuti, quindi conviene.

## Licenza

binutils è **GPLv3+**: le modifiche non possono che esserlo, e `applica.py`
marca ogni file toccato con la dichiarazione di modifica e la data che la
GPLv3 §5(a) richiede a chi distribuisce una versione modificata.

**EX-OS resta `GPL-2.0-or-later`**, per la stessa ragione spiegata in
`tools/gcc-exos/leggimi.md`: è il *«or later»* a rendere possibile la
combinazione.

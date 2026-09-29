# Rust su EX-OS

I sorgenti di Rust (1.100.0, 424 MB) stanno in `/rust/` e **non sono nella
cronologia** di questo repository — stessa regola di GCC, OpenSSL e FreeBASIC,
scritta nel `.gitignore`. Qui dentro va ciò che è nostro: il bersaglio, gli
involucri, e questa ricetta.

> **Stato, 28 settembre 2026: il passo 0 è fatto e provato** (@RUST-0, tappa 2
> di Exilla). Un programma Rust `no_std` compilato su Linux gira dentro EX-OS:
> `argc`/`argv` arrivano, `printf` della libc risponde, l'aritmetica a 64 bit
> di `compiler_builtins` e un iteratore di `core` danno i numeri giusti, e nel
> binario (11 816 byte) non c'è un'istruzione SSE o MMX. Lo rifà:
>
>     tools/rust-exos/prova.sh
>
> (il programma è `tools/rust-exos/prova/`; la preparazione del nightly è in
> testa allo script).
>
> **Anche il passo 1 è fatto, lo stesso giorno** (@RUST-1): l'allocatore
> globale sta in `tools/rust-exos/prova/src/allocatore.rs` — `malloc` fino
> agli 8 byte di allineamento che garantisce, `memalign` sopra, `realloc` solo
> dove non tradisce l'allineamento. Provati dentro EX-OS: `Vec` + `sort`,
> `format!`, `Box<dyn Trait>`, `BTreeMap`, un `#[repr(align(64))]` e centomila
> `push` (cioè le `realloc`).
>
> **E il passo 3 — la `std` vera — è fatto il 29 settembre 2026** (@RUST-STD):
> un programma Rust normale, con `fn main`, gira dentro EX-OS con file,
> directory, ambiente, tempo, **thread**, `Mutex`, `Condvar`, canali, `RwLock` e
> `thread_local!` — 29 controlli su 29. Lo rifà:
>
>     tools/rust-exos/prova-std.sh
>
> Com'è fatto sta nel paragrafo del passo 3, più sotto. Il passo 2 (`libexos`)
> resta da fare; il passo 4 non è più bloccato dai thread.

### Passo 0: cosa è cambiato rispetto al piano, provandolo

Tre cose che il piano del 4 settembre non poteva sapere, perché Rust è
cambiato nel frattempo (nightly 1.101, 27 settembre 2026):

- **i numeri del bersaglio sono numeri**: `"target-pointer-width": 32`, non
  `"32"` — la stringa adesso è un errore;
- **la disposizione dei dati vuole `i128:128`**: LLVM dal 2024 allinea gli
  interi a 128 bit a 16 byte, e un `data-layout` senza quel pezzo è rifiutato
  perché «diverso da quello di `i686-unknown-none`»;
- **un bersaglio da file va dichiarato**: `-Z json-target-spec`.

E una del collegamento: **`libprova.a` va DENTRO il gruppo** con `libc.a`.
`main` sta nella libreria Rust, ma lo chiede `_libc_start` in `libc.a`, che
viene dopo: fuori dal gruppo `ld` ha già scartato l'archivio quando scopre che
serviva. Si collega con il `ld` i386-exos (quello di questa macchina:
`tools/exilla/toolchain-questa-macchina.sh`) e con `crt0.o`, `libc.a` e
`libgcc.a` della toolchain condivisa: la stessa strada dei programmi C.

! **Rust non sta nella home**, per la regola del progetto: `rustup` lavora con
`RUSTUP_HOME`/`CARGO_HOME` in `cross_build/<macchina>/rust-macchina`, e gli
oggetti di cargo in `cross_build/<macchina>/costruzione-rust`. Tutt'e due sono
esclusi da MEGA (`.megaignore`): sono di una macchina sola.

## La domanda da cui parte tutto

«Rust sul CD degli strumenti, accanto a gcc e a fbc» vuol dire **rustc che gira
DENTRO EX-OS**. Quella è l'ultima riga di questo documento, non la prima, e la
ragione è una sola e si misura:

```
thread_crea / attendi / esci     C'E' dal 4 settembre 2026 (syscall 201-203)
TLS per filo                     c'e'       mmap        c'e'
futex / attese che bloccano      c'e'       poll/select c'e'
pthread                          c'e' (28 settembre 2026, kernel 0.222)
fork                             manca      signal      c'e'
socket (BSD)                     manca      C++ 17      c'e' (gcc 17 sul CD)
dlopen / dlsym                   manca
```

**I thread ci sono dal 4 settembre 2026** — un filo condivide memoria e
descrittori e ha il suo stack — ma la `std` di Rust non vuole solo che
esistano: vuole **TLS per filo** (qui il blocco TLS è ancora del processo) e
primitive che **bloccano davvero** (il nostro mutex gira cedendo la CPU). Sono
i due punti in cima all'elenco dei fili in `in_lavorazione.txt`. E `rustc` non
è C: è scritto in Rust, quindi per girare gli serve una `std` per la macchina
su cui gira. In più rustc porta LLVM, che è C++ e che si
costruisce con qualche giga di RAM — cc1 di GCC, che qui gira, è 33 MB; LLVM è
un ordine di grandezza sopra.

Perciò il piano è in quattro passi, e i primi tre danno programmi Rust che
girano su EX-OS **senza toccare EX-OS**.

## Passo 0 — un programma Rust che gira dentro EX-OS (compilato da Linux)

Non serve portare niente: serve un **bersaglio** e la stessa riga di
collegamento che il Makefile usa già per i programmi C.

`tools/rust-exos/i686-unknown-exos.json`:

```json
{
  "llvm-target": "i686-unknown-none",
  "data-layout": "e-m:e-p:32:32-p270:32:32-p271:32:32-p272:64:64-f64:32:64-f80:32-n8:16:32-S128",
  "arch": "x86",
  "target-pointer-width": "32",
  "target-c-int-width": "32",
  "os": "none",
  "vendor": "unknown",
  "executables": true,
  "linker-flavor": "ld",
  "panic-strategy": "abort",
  "relocation-model": "static",
  "disable-redzone": true,
  "features": "-mmx,-sse",
  "dynamic-linking": false
}
```

! **`relocation-model: static` e niente SSE, perché è quel che dice il
Makefile.** I programmi di EX-OS si compilano `-fno-pic -fno-pie
-march=pentium-mmx`: un oggetto Rust con codice PIC o con istruzioni SSE non
sarebbe «quasi compatibile», sarebbe un programma che parte e muore su una
macchina che quelle istruzioni non le ha. Le due righe qui sopra sono la stessa
decisione già presa per il C.

Il programma, `no_std` perché la `std` non c'è ancora:

```rust
#![no_std]
#![no_main]

// start.S chiama main(argc, argv): la firma e' quella del C.
#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8) -> i32 {
    unsafe { printf(b"EX-OS, da Rust\n\0".as_ptr()); }
    0
}

extern "C" { fn printf(fmt: *const u8, ...) -> i32; }

#[panic_handler]
fn panico(_: &core::panic::PanicInfo) -> ! { unsafe { uscita(1) } }
extern "C" { fn exit(codice: i32) -> !; }
unsafe fn uscita(c: i32) -> ! { exit(c) }
```

Si costruisce e si collega **con la stessa riga dei programmi C** — è il punto
di tutto il passo 0:

```
cargo +nightly build -Z build-std=core --release \
      --target tools/rust-exos/i686-unknown-exos.json

ld -m elf_i386 -nostdlib --gc-sections -T lib/programma.ld \
   build/obj/<prog>_start.o target/i686-unknown-exos/release/libprova.a \
   build/obj/libc_ponti_*.o -o build/bin/prova
```

! **NON SERVE CHE EX-OS CAMBI DI UNA RIGA.** `core` è indipendente dal sistema
operativo per costruzione; tutto quel che il programma vuole dal sistema passa
da `extern "C"` sulla libc che c'è già. È anche il modo di scoprire subito se
il bersaglio è giusto: se il binario parte e stampa, il collegamento è a posto.

## Passo 1 — `alloc`: la memoria dinamica

`core` non alloca. Per avere `Box`, `Vec` e `String` basta un allocatore
globale che chiami la `malloc` che EX-OS ha già:

```rust
struct AllocExos;
unsafe impl core::alloc::GlobalAlloc for AllocExos {
    unsafe fn alloc(&self, l: core::alloc::Layout) -> *mut u8 { malloc(l.size()) }
    unsafe fn dealloc(&self, p: *mut u8, _: core::alloc::Layout) { free(p) }
}
#[global_allocator] static A: AllocExos = AllocExos;
```

! **L'ALLINEAMENTO VA GUARDATO, non dato per buono**: `Layout` può chiedere più
dell'allineamento che la malloc di EX-OS garantisce. Se non lo garantisce, si
alloca `size + align` e si arrotonda — e si scrive perché, invece di scoprirlo
il giorno che una struttura con un `f64` dentro si corrompe.

## Passo 2 — `libexos`: il sistema, in Rust

Un crate nostro con gli involucri sicuri su ciò che EX-OS offre: file
(`open`/`read`/`write`), processi (`spawn_ex`, `waitpid`), IPC, e la finestra
(`ex_crea`, `ex_prendi_msg`, `ex_smista`). È il pezzo che rende Rust utile qui
invece che soltanto possibile — e vive in questa directory, non dentro
l'albero di Rust.

## Passo 3 — la `std` (FATTO il 29 settembre 2026)

Il piano diceva «un `sys` nuovo, e i thread dichiarati non supportati». È
andata diversamente, per due ragioni misurate:

- **i thread ci sono** (tappa 1 di Exilla, kernel 0.222: `pthread` vero);
- **Firefox usa il crate `libc` dappertutto**, non solo la `std`: un bersaglio
  che non fosse della famiglia unix lascerebbe fuori tutto il suo codice Rust.

Quindi EX-OS è un bersaglio **unix** (`"os": "exos"`, `"target-family":
["unix"]` in `i686-unknown-exos.json`) che passa per la nostra libc, e dentro
la `std` **si comporta come NuttX**: un sistema POSIX con i pthread e senza
fork, che la `std` nomina già in una cinquantina di punti.

I sorgenti del porting stanno in `tools/rust-exos/std/`:

| file | cosa fa |
|---|---|
| `libc-exos.rs` | il modulo EX-OS del crate `libc`: tipi, strutture e costanti **presi da `lib/include`**. Un numero sbagliato qui è un campo letto spostato. |
| `aggiungi-exos.py` | ogni `target_os = "nuttx"` di `std` e `libc` diventa `any(nuttx, exos)`; poi le eccezioni, una per una, ognuna con il suo perché |
| `os-exos/` | `std::os::exos` (le estensioni dei metadati sulla nostra `struct stat`) |
| `prepara.sh` | copia `library` (dal rust-src del nightly) e il crate `libc` in `cross_build/<macchina>/costruzione-std`, e ci applica quanto sopra |

Le eccezioni, cioè dove EX-OS **non** è NuttX: il nome del sistema; la
casualità da `getentropy`; `errno` da `__errno_dove`; i tempi dei file al
secondo (`st_mtime`, non `st_mtim`); niente `openat` (`remove_dir_all` cammina
per nomi); `FD_CLOEXEC` senza effetto (EX-OS non ha exec che erediti).

**I SOCKET** ci sono dal 29 settembre 2026 (@SOCKET-BSD): `std::net` —
`TcpStream`, `TcpListener`, `UdpSocket`, `to_socket_addrs` col DNS — gira
dentro EX-OS. Prova: `tools/rust-exos/prova-rete.sh` (con la rete di QEMU e
un host dall'altra parte). Solo IPv4; stanno nella `libc.a`.

! **CARGO NON VEDE I CAMBI ALLA NOSTRA `std`**: per le librerie di
`-Z build-std` guarda la versione, non le date. `prova-std.sh` tiene
un'impronta dei sorgenti del porting e butta la costruzione quando cambia.

! **`libc.a` DELLA TOOLCHAIN**: il programma si collega con `i386-exos-gcc`,
cioè con la `libc.a` di questa macchina. Dopo ogni modifica alla libc va
rifatta: `tools/gcc-exos/prepara-cross.sh "$PWD/cross_build/<macchina>/exos-cross"`.

Cosa ha trovato, e sistemato, la prima `std` (29 settembre 2026):

- `struct timespec` aveva `tv_sec` a 32 bit con `time_t` a 64: ora è `time_t`,
  come vuole POSIX (`lib/include/libc.h`, 8 → 12 byte);
- `stat()` e `poll()` rendevano `-errno` invece di `-1`: per la `std` ogni file
  esisteva;
- il kernel rispondeva `EIO` a un `mkdir` con il genitore mancante (kernel
  0.225: `ENOENT`, `ENOTDIR`, `EISDIR`);
- la `libc.a` della toolchain era compilata per i686 e non per il Pentium MMX:
  `cmov` in `malloc`, `stat`, `nanosleep`;
- mancava `strerror_r`.

## Passo 4 — rustc sul CD degli strumenti

È la richiesta di partenza, ed è l'unica che dipende da EX-OS e non da noi:

1. serve il passo 3 (rustc è scritto in Rust: gli serve una `std` sulla
   macchina su cui gira);
2. servono **i thread veri**, perché LLVM e rustc si possono costruire a flusso
   singolo (`LLVM_ENABLE_THREADS=OFF`, `-Z threads=1`) ma la `std` con cui si
   collegano deve comunque avere `Thread`;
3. serve la memoria: LLVM in compilazione vuole qualche giga, e il binario è un
   ordine di grandezza sopra cc1 (33 MB).

(Aggiornamento, 29 settembre 2026: i punti 1 e 2 ci sono — `std` e thread
veri. Resta il 3, la memoria, e la costruzione di LLVM stesso.)

! **QUINDI IL CD NON PUÒ AVERE RUSTC FINCHÉ EX-OS NON HA I THREAD**, ed è lo
stesso muro contro cui si ferma il porting di Firefox (diario, 3 settembre
2026). I thread però si giustificano da soli: mezzo mondo del software li dà
per scontati, e sono l'unica cosa in questo elenco che serve a tutto il resto —
non solo a Rust.

## L'ordine, e perché

Passo 0 si fa in una sera e si prova subito: un binario che parte dentro EX-OS
dice più di qualunque preventivo. I passi 1 e 2 danno un linguaggio con cui
scrivere programmi veri per questo sistema. Il passo 3 è un lavoro serio ma
delimitato. Il passo 4 non è un porting: è una conseguenza dei thread.

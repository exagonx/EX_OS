# Rust su EX-OS

I sorgenti di Rust (1.100.0, 424 MB) stanno in `/rust/` e **non sono nella
cronologia** di questo repository — stessa regola di GCC, OpenSSL e FreeBASIC,
scritta nel `.gitignore`. Qui dentro va ciò che è nostro: il bersaglio, gli
involucri, e questa ricetta.

> **Stato: niente di questo è ancora stato costruito.** Questo file è il piano,
> scritto con i blocchi misurati dentro EX-OS il 4 settembre 2026. Quando il
> passo 0 sarà fatto, questa riga sparisce e al suo posto ci va il comando che
> lo rifà, come in `tools/freebasic-exos/leggimi.md`.

## La domanda da cui parte tutto

«Rust sul CD degli strumenti, accanto a gcc e a fbc» vuol dire **rustc che gira
DENTRO EX-OS**. Quella è l'ultima riga di questo documento, non la prima, e la
ragione è una sola e si misura:

```
thread_crea / attendi / esci     C'E' dal 4 settembre 2026 (syscall 201-203)
TLS per filo                     manca      mmap        c'e'
futex / attese che bloccano      manca      poll/select c'e'
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

## Passo 3 — la `std`: un `sys` nuovo dentro la libreria standard

È il porting vero, e si fa nella libreria, non nel compilatore: `library/std`
ha uno strato per sistema (`sys/pal/`), e ci sono già port fatti così per UEFI,
per Hermit, per SGX. Il grosso — file, io, tempo, processi — si mappa su quel
che EX-OS ha.

! **I THREAD SI DICHIARANO NON SUPPORTATI, e non è una scorciatoia**: EX-OS non
ne ha, e `sys/pal/unsupported/thread.rs` esiste apposta perché anche altri
bersagli sono in quella condizione. `std::thread::spawn` renderà un errore
invece di un thread; `File`, `String`, `Vec`, i formattatori e cargo
funzionano. Fingere dei thread — eseguire la closure nel chiamante — sarebbe la
cosa peggiore: un programma che crede di avere due flussi e ne ha uno solo si
comporta bene finché non conta.

Da qui in poi funziona **cargo**, e con lui i crate che non vogliono thread né
socket.

## Passo 4 — rustc sul CD degli strumenti

È la richiesta di partenza, ed è l'unica che dipende da EX-OS e non da noi:

1. serve il passo 3 (rustc è scritto in Rust: gli serve una `std` sulla
   macchina su cui gira);
2. servono **i thread veri**, perché LLVM e rustc si possono costruire a flusso
   singolo (`LLVM_ENABLE_THREADS=OFF`, `-Z threads=1`) ma la `std` con cui si
   collegano deve comunque avere `Thread`;
3. serve la memoria: LLVM in compilazione vuole qualche giga, e il binario è un
   ordine di grandezza sopra cc1 (33 MB).

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

// =============================================================================
// tools/rust-exos/prova/src/lib.rs — @RUST-0 (28 settembre 2026)
//
// Un programma Rust no_std che gira DENTRO EX-OS, compilato da Linux col
// bersaglio i686-unknown-exos.json. Non serve che EX-OS cambi di una riga:
// `core` e' indipendente dal sistema, e tutto il resto passa da `extern "C"`
// sulla libc che c'e' gia'. crt0.o chiama main(argc, argv): la firma e' quella
// del C.
//
// Controlla le tre cose che un collegamento riuscito NON garantisce: l'avvio
// (argc e argv arrivano), la libc (printf), e il codice di `core` compilato
// per il bersaglio (aritmetica a 64 bit, che su i686 passa da compiler_builtins,
// e un iteratore).
// =============================================================================
#![no_std]

// @RUST-1 (28 settembre 2026): Box, Vec, String sopra la malloc di EX-OS.
extern crate alloc;
mod allocatore;
#[global_allocator]
static ALLOCATORE: allocatore::AllocExos = allocatore::AllocExos;

use alloc::boxed::Box;
use alloc::collections::BTreeMap;
use alloc::format;
use alloc::string::String;
use alloc::vec::Vec;

trait Forma { fn lati(&self) -> u32; }
struct Triangolo;
struct Quadrato;
impl Forma for Triangolo { fn lati(&self) -> u32 { 3 } }
impl Forma for Quadrato { fn lati(&self) -> u32 { 4 } }

#[repr(align(64))]
struct Allineata([u8; 64]);

/* Una String di Rust non finisce con lo zero: per printf se ne fa una copia che
 * finisce. */
fn c(s: &str) -> Vec<u8> { let mut v = Vec::from(s.as_bytes()); v.push(0); v }

extern "C" {
    fn printf(fmt: *const u8, ...) -> i32;
    fn exit(codice: i32) -> !;
}

#[no_mangle]
pub extern "C" fn main(argc: i32, argv: *const *const u8) -> i32 {
    unsafe {
        printf(b"rust-exos: un programma Rust dentro EX-OS\n\0".as_ptr());
        printf(b"  argc        %d\n\0".as_ptr(), argc);
        if argc > 0 {
            printf(b"  argv[0]     %s\n\0".as_ptr(), *argv);
        }
    }

    // 64 bit su una macchina a 32: divisione e moltiplicazione vere.
    let grande: u64 = 0x1234_5678_9abc_def0;
    let quoziente = grande / 1_000_003;
    let rifatto = quoziente * 1_000_003 + grande % 1_000_003;

    // un iteratore di core: la somma dei quadrati da 1 a 100
    let somma: u32 = (1..=100u32).map(|x| x * x).sum();

    // --- @RUST-1: alloc ---------------------------------------------------
    let mut v: Vec<u64> = (0..1000u64).map(|i| (i * 7919) % 1000).collect();
    v.sort_unstable();
    let vec_ok = v.len() == 1000 && v[0] == 0 && v[999] == 999;

    let s: String = format!("Exilla {} EX-OS, {} elementi", "su", v.len());
    let string_ok = s == "Exilla su EX-OS, 1000 elementi";

    let forme: Vec<Box<dyn Forma>> = alloc::vec![Box::new(Triangolo), Box::new(Quadrato)];
    let box_ok = forme.iter().map(|f| f.lati()).sum::<u32>() == 7;

    let mut m = BTreeMap::new();
    m.insert("tre", 3); m.insert("uno", 1); m.insert("due", 2);
    let mappa_ok = m.len() == 3 && m.keys().next() == Some(&"due");

    let a = Box::new(Allineata([7u8; 64]));
    let allinea_ok = (&*a as *const Allineata as usize) % 64 == 0 && a.0[63] == 7;

    let mut crescente: Vec<u8> = Vec::new();
    for i in 0..100_000u32 { crescente.push(i as u8); }
    let realloc_ok = crescente.len() == 100_000 && crescente[99_999] == (99_999u32 as u8);

    unsafe {
        let esito = |ok: bool| if ok { b"OK\0".as_ptr() } else { b"NO\0".as_ptr() };
        printf(b"  Vec + sort  %s\n\0".as_ptr(), esito(vec_ok));
        let cs = c(&s);
        printf(b"  format!     %s  (%s)\n\0".as_ptr(), esito(string_ok), cs.as_ptr());
        printf(b"  Box<dyn>    %s\n\0".as_ptr(), esito(box_ok));
        printf(b"  BTreeMap    %s\n\0".as_ptr(), esito(mappa_ok));
        printf(b"  align(64)   %s\n\0".as_ptr(), esito(allinea_ok));
        printf(b"  100 000 push (realloc) %s\n\0".as_ptr(), esito(realloc_ok));
    }
    let alloc_ok = vec_ok && string_ok && box_ok && mappa_ok && allinea_ok && realloc_ok;

    let va = rifatto == grande && somma == 338_350 && alloc_ok;
    unsafe {
        printf(b"  64 bit      %s\n\0".as_ptr(),
               if rifatto == grande { b"OK\0".as_ptr() } else { b"NO\0".as_ptr() });
        printf(b"  iteratore   %u (atteso 338350)\n\0".as_ptr(), somma);
        printf(b"rust-exos: %s\n\0".as_ptr(),
               if va { b"tutto a posto\0".as_ptr() } else { b"QUALCOSA NON VA\0".as_ptr() });
    }
    if va { 0 } else { 1 }
}

#[panic_handler]
fn panico(_: &core::panic::PanicInfo) -> ! {
    unsafe {
        printf(b"rust-exos: panic\n\0".as_ptr());
        exit(101)
    }
}

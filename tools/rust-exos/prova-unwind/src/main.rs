// tools/rust-exos/prova-unwind/src/main.rs - panic=unwind inside EX-OS.
// Each check prints [OK] or [NO]; the last line counts the NO.
use std::panic;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::thread;

static DROP: AtomicUsize = AtomicUsize::new(0);
static mut NO: u32 = 0;

struct Conta;
impl Drop for Conta {
    fn drop(&mut self) {
        DROP.fetch_add(1, Ordering::SeqCst);
    }
}

fn ok(cosa: &str, va: bool) {
    println!("  {}  {}", if va { "[OK]" } else { "[NO]" }, cosa);
    if !va {
        unsafe { NO += 1 };
    }
}

#[inline(never)]
fn profondo(n: u32) -> u32 {
    let _c = Conta;
    if n == 0 {
        panic!("dal fondo");
    }
    profondo(n - 1) + 1
}

fn main() {
    println!("rust-unwind: panic=unwind dentro EX-OS");
    panic::set_hook(Box::new(|_| {}));

    let r = panic::catch_unwind(|| 6 * 7);
    ok("catch_unwind senza panic rende il valore", r.ok() == Some(42));

    let r = panic::catch_unwind(|| -> u32 { panic!("presa") });
    ok("catch_unwind prende un panic", r.is_err());

    let r = panic::catch_unwind(|| panic!("{}", String::from("messaggio")));
    let msg = r.err().and_then(|e| e.downcast::<String>().ok()).map(|s| *s);
    ok("il messaggio del panic arriva", msg.as_deref() == Some("messaggio"));

    DROP.store(0, Ordering::SeqCst);
    let r = panic::catch_unwind(|| profondo(10));
    ok(&format!("11 Drop eseguiti svolgendo la pila (visti {})", DROP.load(Ordering::SeqCst)),
       r.is_err() && DROP.load(Ordering::SeqCst) == 11);

    let h = thread::spawn(|| -> u32 { panic!("nel filo") });
    ok("un filo in panic: join da' Err", h.join().is_err());

    let h = thread::spawn(|| 5u32 + 5);
    ok("un filo normale dopo: join da' Ok(10)", h.join().ok() == Some(10));

    let r = panic::catch_unwind(|| {
        let v: Vec<u32> = Vec::new();
        v[3]
    });
    ok("indice fuori dal vettore: panic preso", r.is_err());

    let r = panic::catch_unwind(|| {
        let _ = panic::catch_unwind(|| panic!("dentro"));
        panic!("fuori")
    });
    ok("panic annidati", r.is_err());

    println!("rust-unwind: {} NO", unsafe { NO });
}

// tools/rust-exos/rustc-nativo/usa.rs - uses the macros of macro.rs.
use quarantadue::{quarantadue, Nome};

#[derive(Nome)]
struct Pinguino;

fn main() {
    let x = quarantadue!();
    println!("macro: {} {}", x, Pinguino::nome());
    println!("MACRO-{}", if x == 42 && Pinguino::nome() == "Pinguino" { "OK" } else { "NO" });
}

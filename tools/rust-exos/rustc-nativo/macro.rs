// tools/rust-exos/rustc-nativo/macro.rs - a procedural macro crate built BY
// rustc INSIDE EX-OS (prova-nativo.sh): a .so that the same rustc then
// opens with dlopen to expand macros.
extern crate proc_macro;
use proc_macro::TokenStream;

/// `quarantadue!()` -> `42`
#[proc_macro]
pub fn quarantadue(_input: TokenStream) -> TokenStream {
    "42".parse().unwrap()
}

/// `#[derive(Nome)]` on `struct X` -> `impl X { fn nome() -> &'static str { "X" } }`
#[proc_macro_derive(Nome)]
pub fn nome(input: TokenStream) -> TokenStream {
    let s = input.to_string();
    let n = s.split_whitespace().skip_while(|w| *w != "struct").nth(1).unwrap_or("?");
    let n: String = n.chars().take_while(|c| c.is_alphanumeric() || *c == '_').collect();
    format!("impl {n} {{ fn nome() -> &'static str {{ \"{n}\" }} }}").parse().unwrap()
}

/// `rifiuta!()` panics: rustc must report it as an error, not crash.
#[proc_macro]
pub fn rifiuta(_input: TokenStream) -> TokenStream {
    panic!("questa macro rifiuta di proposito")
}

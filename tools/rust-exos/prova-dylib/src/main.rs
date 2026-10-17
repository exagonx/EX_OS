// tools/rust-exos/prova-dylib - loads libprova_dylib.so with dlopen
//
// Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// @RUST-64 R64-2 step b: a Rust shared object loaded by the EX-OS ELF loader
// (lib/libc_dl.c), as rustc loads a procedural macro. Prints one line per
// check and "prova-dylib: N NO" at the end.
use std::ffi::{c_char, c_int, c_void, CString};

unsafe extern "C" {
    fn dlopen(nome: *const c_char, modo: c_int) -> *mut c_void;
    fn dlsym(h: *mut c_void, nome: *const c_char) -> *mut c_void;
    fn dlerror() -> *const c_char;
    fn dlclose(h: *mut c_void) -> c_int;
    fn free(p: *mut c_void);
}

fn simbolo(h: *mut c_void, nome: &str) -> *mut c_void {
    let c = CString::new(nome).unwrap();
    let p = unsafe { dlsym(h, c.as_ptr()) };
    if p.is_null() {
        println!("  manca il simbolo {}", nome);
        std::process::exit(1);
    }
    p
}

fn main() {
    let mut no = 0;
    let mut esito = |nome: &str, ok: bool| {
        println!("{} {}", if ok { "  ok" } else { "  NO" }, nome);
        if !ok {
            no += 1;
        }
    };

    let nome = CString::new("libprova_dylib.so").unwrap();
    let h = unsafe { dlopen(nome.as_ptr(), 2 /* RTLD_NOW */) };
    if h.is_null() {
        let e = unsafe { std::ffi::CStr::from_ptr(dlerror()) };
        println!("prova-dylib: dlopen: {}", e.to_string_lossy());
        println!("prova-dylib: 1 NO");
        return;
    }
    esito("dlopen", true);

    unsafe {
        let somma: extern "C" fn(i64, i64) -> i64 = std::mem::transmute(simbolo(h, "pd_somma"));
        esito("somma", somma(40, 2) == 42);

        let saluta: unsafe extern "C" fn(*const c_char, *mut u8, usize) -> usize =
            std::mem::transmute(simbolo(h, "pd_saluta"));
        let mut buf = [0u8; 64];
        let n = saluta(c"EX-OS".as_ptr(), buf.as_mut_ptr(), buf.len());
        let s = std::str::from_utf8(&buf[..n]).unwrap_or("?");
        println!("    {}", s);
        esito("String e format! nella libreria", s == "ciao, EX-OS! (5 lettere)");

        let conta: extern "C" fn() -> u32 = std::mem::transmute(simbolo(h, "pd_conta"));
        let a = conta();
        let b = conta();
        esito("thread_local nella libreria", a == 1 && b == 2);

        let filo: extern "C" fn() -> u32 = std::mem::transmute(simbolo(h, "pd_conta_in_un_filo"));
        esito("un filo della libreria ha il suo thread_local", filo() == 2);
        esito("quello del programma e' rimasto", conta() == 3);

        let altro = std::thread::spawn(move || conta()).join().unwrap_or(99);
        esito("un filo del programma che chiama la libreria", altro == 1);

        let panico: extern "C" fn() -> i32 = std::mem::transmute(simbolo(h, "pd_panico"));
        esito("panic preso dentro la libreria", panico() == 1);

        let r = std::panic::catch_unwind(|| panic!("nel programma"));
        esito("panic preso nel programma, dopo", r.is_err());

        let parole: extern "C" fn() -> usize = std::mem::transmute(simbolo(h, "pd_parole"));
        esito("BTreeMap nella libreria", parole() == 3 + 3 * 2 + 3 * 3);

        let scatola: extern "C" fn() -> *mut u64 = std::mem::transmute(simbolo(h, "pd_scatola"));
        let p = scatola();
        esito("Box della libreria letto dal programma", *p == 4242);
        free(p as *mut c_void);

        esito("dlclose", dlclose(h) == 0);
    }
    // A real procedural macro (macro/lib.rs, built with --crate-type
    // proc-macro): rustc finds its macros through the static
    // __rustc_proc_macro_decls_<hash>__, a &[ProcMacro] - a pointer and a
    // length. The script passes the name it read with nm.
    if let (Ok(file), Ok(decls)) = (std::env::var("PROVA_MACRO"), std::env::var("PROVA_DECLS")) {
        let f = CString::new(file).unwrap();
        let h = unsafe { dlopen(f.as_ptr(), 2) };
        if h.is_null() {
            let e = unsafe { std::ffi::CStr::from_ptr(dlerror()) };
            println!("    dlopen: {}", e.to_string_lossy());
        }
        esito("dlopen della macro procedurale", !h.is_null());
        if !h.is_null() {
            let p = simbolo(h, &decls) as *const [usize; 2];
            let n = unsafe { (*p)[1] };
            println!("    {}: {} macro", decls, n);
            esito("la tabella delle macro dice una macro", n == 1);
            esito("dlclose della macro", unsafe { dlclose(h) } == 0);
        }
    }
    println!("prova-dylib: {} NO", no);
}

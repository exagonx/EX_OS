// tools/rust-exos/prova-dylib/libreria - libprova_dylib.so
//
// Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// What a procedural macro does inside rustc, in small: its own std in a
// shared object (allocator calls reaching the program's malloc through the
// libc's export table, thread-locals through __tls_get_addr, threads), and a
// panic caught inside the library - proc macros report errors by panicking.
use std::cell::Cell;
use std::collections::BTreeMap;
use std::os::raw::c_char;

thread_local! {
    static CONTA: Cell<u32> = const { Cell::new(0) };
}

#[no_mangle]
pub extern "C" fn pd_somma(a: i64, b: i64) -> i64 {
    a + b
}

/// Writes "ciao, NAME! (n letters)" into out; returns its length.
#[no_mangle]
pub unsafe extern "C" fn pd_saluta(nome: *const c_char, out: *mut u8, n: usize) -> usize {
    let nome = unsafe { std::ffi::CStr::from_ptr(nome) }.to_string_lossy().into_owned();
    let s = format!("ciao, {}! ({} lettere)", nome, nome.chars().count());
    let k = s.len().min(n);
    unsafe { std::ptr::copy_nonoverlapping(s.as_ptr(), out, k) };
    k
}

/// The calling thread's counter, after adding one.
#[no_mangle]
pub extern "C" fn pd_conta() -> u32 {
    CONTA.with(|c| {
        c.set(c.get() + 1);
        c.get()
    })
}

/// A thread started by the library: its counter starts from zero.
#[no_mangle]
pub extern "C" fn pd_conta_in_un_filo() -> u32 {
    std::thread::spawn(|| {
        pd_conta();
        pd_conta()
    })
    .join()
    .unwrap_or(99)
}

/// A panic thrown and caught inside the library: 1 if caught with its text.
#[no_mangle]
pub extern "C" fn pd_panico() -> i32 {
    let r = std::panic::catch_unwind(|| {
        let v: Vec<u32> = Vec::new();
        if v.is_empty() {
            panic!("panico voluto nella libreria");
        }
        v[0]
    });
    match r {
        Err(e) => match e.downcast_ref::<&str>() {
            Some(s) if s.contains("voluto") => 1,
            _ => 2,
        },
        Ok(_) => 0,
    }
}

/// Collections and sorting: the sum of the keys of a word count.
#[no_mangle]
pub extern "C" fn pd_parole() -> usize {
    let mut m: BTreeMap<String, usize> = BTreeMap::new();
    for p in "uno due tre due tre tre".split(' ') {
        *m.entry(p.to_string()).or_insert(0) += 1;
    }
    m.iter().map(|(k, v)| k.len() * v).sum()
}

/// A Box made here and freed by the program (same malloc on both sides).
#[no_mangle]
pub extern "C" fn pd_scatola() -> *mut u64 {
    Box::into_raw(Box::new(4242))
}

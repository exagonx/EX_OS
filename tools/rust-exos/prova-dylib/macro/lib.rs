// tools/rust-exos/prova-dylib/macro/lib.rs - a real procedural macro crate,
// built for x86_64-unknown-exos by tools/rust-exos/prova-dylib.sh: the
// shared object a native rustc will dlopen (@RUST-64 R64-2 step b).
//
// Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
extern crate proc_macro;
use proc_macro::TokenStream;

/// `quarantadue!()` expands to `42`.
#[proc_macro]
pub fn quarantadue(_input: TokenStream) -> TokenStream {
    "42".parse().unwrap()
}

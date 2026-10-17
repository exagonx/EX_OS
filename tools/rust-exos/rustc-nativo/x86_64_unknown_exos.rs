// compiler/rustc_target/src/spec/targets/x86_64_unknown_exos.rs
// (from tools/rust-exos/rustc-nativo/, applied by applica.py)
//
// Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
// SPDX-License-Identifier: MIT OR Apache-2.0 (as the compiler it joins)
//
// 64-bit EX-OS, built into rustc: the same target as
// tools/rust-exos/x86_64-unknown-exos.json, which the Linux nightly uses as a
// cross compiler. Programs are static and non-PIE, loaded at 0x08000000 (small
// code model); code is PIC with global-dynamic TLS so that a crate can also
// be a shared object for dlopen (lib/libc_dl.c) - procedural macros are.
// The linker is the EX-OS gcc: `gcc` inside EX-OS, x86_64-exos-gcc when
// cross-building (bootstrap.toml sets it).
use crate::spec::{
    Arch, CodeModel, LinkerFlavor, Os, PanicStrategy, RelocModel, StackProbeType, Target,
    TargetMetadata, TargetOptions, TlsModel, cvs,
};

pub(crate) fn target() -> Target {
    let opts = TargetOptions {
        os: Os::Exos,
        families: cvs!["unix"],
        linker_flavor: LinkerFlavor::Gnu(crate::spec::Cc::Yes, crate::spec::Lld::No),
        linker: Some("gcc".into()),
        cpu: "x86-64".into(),
        max_atomic_width: Some(64),
        relocation_model: RelocModel::Pic,
        code_model: Some(CodeModel::Small),
        position_independent_executables: false,
        dynamic_linking: true,
        dll_prefix: "lib".into(),
        dll_suffix: ".so".into(),
        has_thread_local: true,
        tls_model: TlsModel::GeneralDynamic,
        panic_strategy: PanicStrategy::Unwind,
        stack_probes: StackProbeType::Inline,
        // ! gcc's own libraries ARE wanted (-lc, -lgcc): the default, true,
        // makes rustc pass -nodefaultlibs, and crt0.o's _libc_start went
        // unresolved (the .json says "no-default-libraries": false).
        no_default_libraries: false,
        ..Default::default()
    };
    Target {
        llvm_target: "x86_64-unknown-none".into(),
        metadata: TargetMetadata {
            description: Some("64-bit EX-OS".into()),
            tier: Some(3),
            host_tools: Some(true),
            std: Some(true),
        },
        pointer_width: 64,
        data_layout:
            "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128".into(),
        arch: Arch::X86_64,
        options: opts,
    }
}

#!/usr/bin/env python3
# =============================================================================
# tools/rust-exos/rustc-nativo/applica.py — x86_64-unknown-exos inside rustc
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     python3 tools/rust-exos/rustc-nativo/applica.py <rustc source tree>
#
# @RUST-64 R64-2 step c. The Linux nightly knows EX-OS only from a .json file;
# a rustc that RUNS on EX-OS must have the target built in, as its host. This
# adds to compiler/rustc_target:
#   - Os::Exos ("exos") in the Os enum of spec/mod.rs,
#   - ("x86_64-unknown-exos", x86_64_unknown_exos) in supported_targets!,
#   - spec/targets/x86_64_unknown_exos.rs (copied from this directory),
# and STAGE0_MISSING_TARGETS in src/bootstrap (the stage-0 compiler, the
# nightly, does not list it). Idempotent: what is there already is left alone.
# The std side (library/ and the libc crate) is prepara-sorgenti.sh's job.
# =============================================================================
import os, shutil, sys

if len(sys.argv) != 2:
    sys.exit("usage: applica.py <rustc source tree>")
ALBERO = sys.argv[1]
QUI = os.path.dirname(os.path.abspath(__file__))


def cambia(rel, prima, dopo, segno):
    p = os.path.join(ALBERO, rel)
    s = open(p).read()
    if segno in s:
        return "already"
    if s.count(prima) != 1:
        sys.exit("applica: %s: anchor found %d times: %r" % (rel, s.count(prima), prima[:60]))
    open(p, "w").write(s.replace(prima, dopo))
    return "done"


MOD = "compiler/rustc_target/src/spec/mod.rs"
print("Os::Exos:", cambia(MOD, '        EspIdf = "espidf",\n',
                          '        EspIdf = "espidf",\n        Exos = "exos",\n', 'Exos = "exos"'))
print("supported_targets:", cambia(MOD, '    ("x86_64-unknown-redox", x86_64_unknown_redox),\n',
                                   '    ("x86_64-unknown-redox", x86_64_unknown_redox),\n'
                                   '    ("x86_64-unknown-exos", x86_64_unknown_exos),\n',
                                   '"x86_64-unknown-exos"'))
shutil.copy(os.path.join(QUI, "x86_64_unknown_exos.rs"),
            os.path.join(ALBERO, "compiler/rustc_target/src/spec/targets/x86_64_unknown_exos.rs"))
print("target file: copied")
# Step d: rustc built to RUN on EX-OS. Its LLVM is cross-built by x.py with
# cmake, which needs a system name; EX-OS is told "Linux" (a POSIX system
# with -lpthread -ldl -lrt, the libc answers the rest of LLVM's checks).
print("CMAKE_SYSTEM_NAME:", cambia(
    "src/bootstrap/src/core/build_steps/llvm.rs",
    '        } else if target.contains("linux") {\n            cfg.define("CMAKE_SYSTEM_NAME", "Linux");\n',
    '        } else if target.contains("linux") || target.contains("exos") {\n'
    '            cfg.define("CMAKE_SYSTEM_NAME", "Linux");\n',
    'target.contains("exos")'))
# ...and the compiler's own crates use the libc crate too: the EX-OS copy
# (<tree>/libc, prepara-sorgenti.sh) for the whole workspace.
print("workspace libc:", cambia(
    "Cargo.toml", "#[patch.crates-io]\n",
    "#[patch.crates-io]\n[patch.crates-io]\nlibc = { path = \"libc\" }\n",
    'libc = { path = "libc" }'))
# rustc's fallback file locks are fcntl(F_SETLK), which EX-OS's libc does not
# have (lib/libc.c says so): on EX-OS the compiler's own "unsupported" branch,
# which incremental compilation already knows how to live with.
print("flock fallback:", cambia(
    "compiler/rustc_data_structures/src/flock.rs",
    "cfg_select! {\n    unix => {\n        mod unix;",
    "cfg_select! {\n    all(unix, not(target_os = \"exos\")) => {\n        mod unix;",
    'not(target_os = \"exos\")) => {\n        mod unix;'))

# ctrlc (through nix, which does not know EX-OS) only makes Ctrl+C print a
# cleaner end: rustc leaves it out where it cannot have it, as for wasm.
print("ctrlc dependency:", cambia(
    "compiler/rustc_driver_impl/Cargo.toml",
    "[target.'cfg(not(target_family = \"wasm\"))'.dependencies]\n# tidy-alphabetical-start\nctrlc",
    "[target.'cfg(not(any(target_family = \"wasm\", target_os = \"exos\")))'.dependencies]\n"
    "# tidy-alphabetical-start\nctrlc",
    'target_os = \"exos\")))\'.dependencies]\n# tidy-alphabetical-start\nctrlc'))
print("ctrlc handler:", cambia(
    "compiler/rustc_driver_impl/src/lib.rs",
    "    #[cfg(all(not(miri), not(target_family = \"wasm\")))]\n    ctrlc::set_handler(",
    "    #[cfg(all(not(miri), not(target_family = \"wasm\"), not(target_os = \"exos\")))]\n    ctrlc::set_handler(",
    "not(target_os = \"exos\")))]\n    ctrlc::set_handler("))

# ! rustc IN ONE STATIC PROGRAM. rustc_driver is normally librustc_driver.so,
# which the small rustc binary needs at start-up; EX-OS programs are static
# (the kernel's loader starts no dynamic linker), and the .so could not even
# be linked (libstdc++.a is not PIC). As an rlib the whole compiler goes into
# the rustc executable. Crate types cannot depend on the target, so the Linux
# stage-1 rustc is built the same way - it works, and is only bigger.
# Procedural macros are not affected: they stay .so files opened with dlopen.
print("rustc_driver rlib:", cambia(
    "compiler/rustc_driver/Cargo.toml",
    '[lib]\ncrate-type = ["dylib"]\n',
    '[lib]\n# EX-OS: rlib, see tools/rust-exos/rustc-nativo/applica.py\ncrate-type = ["rlib"]\n',
    'crate-type = ["rlib"]'))

# Third-party crates in vendor/ that must know EX-OS when the COMPILER is
# built for it (step d). A vendored file that changes must leave the crate's
# .cargo-checksum.json, or cargo refuses the crate ("checksum changed").
import glob, json


def vendor_cambia(cartella, rel, prima, dopo, segno, volte=1):
    p = os.path.join(cartella, rel)
    s = open(p).read()
    if segno in s:
        return "already"
    if s.count(prima) != volte:
        return "anchor found %d times, not %d" % (s.count(prima), volte)
    open(p, "w").write(s.replace(prima, dopo))
    c = os.path.join(cartella, ".cargo-checksum.json")
    j = json.load(open(c))
    j.get("files", {}).pop(rel, None)
    json.dump(j, open(c, "w"))
    return "done"


# getrandom (0.2, 0.3, 0.4): EX-OS's libc has getrandom(2) with Linux's
# signature, so EX-OS joins the branch that calls it (with illumos, cygwin...).
for d in sorted(glob.glob(os.path.join(ALBERO, "vendor", "getrandom-*"))):
    rel = "src/backends.rs" if os.path.exists(os.path.join(d, "src/backends.rs")) else "src/lib.rs"
    print(os.path.basename(d) + ":", vendor_cambia(
        d, rel, '        target_os = "illumos",\n',
        '        target_os = "illumos",\n        target_os = "exos",\n', 'target_os = "exos"'))

# ...its errno: EX-OS's libc gives the thread's errno with __errno_dove
# (libc-exos.rs), and 0.3/0.4 take libc only on the systems they list.
ERRNO_LINUX = ('    } else if #[cfg(any(target_os = "linux", target_os = "emscripten", '
               'target_os = "hurd", target_os = "redox", target_os = "dragonfly"))] {\n')
for d in sorted(glob.glob(os.path.join(ALBERO, "vendor", "getrandom-*"))):
    for rel in ("src/util_libc.rs", "src/utils/get_errno.rs"):
        if os.path.exists(os.path.join(d, rel)):
            print(os.path.basename(d), rel + ":", vendor_cambia(
                d, rel, ERRNO_LINUX,
                '    } else if #[cfg(target_os = "exos")] {\n'
                '        use libc::__errno_dove as errno_location;\n' + ERRNO_LINUX,
                '__errno_dove'))
    if 'target_os = "illumos"' in open(os.path.join(d, "Cargo.toml")).read():   # 0.2: cfg(unix)
        print(os.path.basename(d), "Cargo.toml:", vendor_cambia(
            d, "Cargo.toml", 'target_os = "hurd", target_os = "illumos", ',
            'target_os = "hurd", target_os = "illumos", target_os = "exos", ', 'target_os = "exos"'))

# tempfile: on unix it takes rustix for rename and unlink only - and rustix
# wants the whole *at family, which EX-OS's libc does not have. On EX-OS the
# std's own rename/remove_file do the same job; rustix is not built at all.
for d in sorted(glob.glob(os.path.join(ALBERO, "vendor", "tempfile-*"))):
    u = open(os.path.join(d, "src/file/imp/unix.rs")).read()
    prima = ('#[cfg(not(target_os = "redox"))]\nuse {\n    rustix::fs::{rename, unlink},\n'
             if '#[cfg(not(target_os = "redox"))]\nuse {\n    rustix' in u
             else '\nuse {\n    rustix::fs::{rename, unlink},\n')       # 3.27: no redox cfg
    print(os.path.basename(d), "unix.rs:", vendor_cambia(
        d, "src/file/imp/unix.rs", prima,
        '\n#[cfg(target_os = "exos")]\nuse std::fs::{hard_link, remove_file as unlink, rename};\n'
        '#[cfg(not(any(target_os = "redox", target_os = "exos")))]\nuse {\n    rustix::fs::{rename, unlink},\n',
        'remove_file as unlink'))
    print(os.path.basename(d), "Cargo.toml:", vendor_cambia(
        d, "Cargo.toml",
        "[target.'cfg(any(unix, target_os = \"wasi\"))'.dependencies.rustix]",
        "[target.'cfg(all(any(unix, target_os = \"wasi\"), not(target_os = \"exos\")))'.dependencies.rustix]",
        'not(target_os = \"exos\")'))

# libloading (how rustc opens procedural macros): RTLD_* written per system.
# EX-OS's are Linux's (lib/include/libc.h: LAZY 1, NOW 2, GLOBAL 0x100,
# LOCAL 0), so EX-OS joins Linux in the four tables.
for d in sorted(glob.glob(os.path.join(ALBERO, "vendor", "libloading-*"))):
    rel = "src/os/unix/consts.rs"
    if os.path.exists(os.path.join(d, rel)):
        print(os.path.basename(d) + ":", vendor_cambia(
            d, rel, '            target_os = "linux",\n',
            '            target_os = "linux",\n            target_os = "exos",\n',
            'target_os = "exos"', volte=4))

print("STAGE0_MISSING_TARGETS:", cambia(
    "src/bootstrap/src/core/sanity.rs",
    "    // just a dummy comment so the list doesn't get onelined\n",
    "    // just a dummy comment so the list doesn't get onelined\n    \"x86_64-unknown-exos\",\n",
    '"x86_64-unknown-exos"'))

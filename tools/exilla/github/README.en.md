# Exilla — Firefox on EX-OS

[🇮🇹 Italiano](README.md) · **🇬🇧 English**

Exilla is the port of Firefox (Gecko) to [EX-OS](https://github.com/exagonx/EX_OS),
a 32-bit x86 operating system whose baseline CPU is the Pentium MMX. This
repository holds only what the port needs: the changes to Mozilla's sources,
the scripts that produce them, the toolchain and the tests. Firefox's sources
come from Mozilla; EX-OS's (libc, headers, kernel) from its own repository.

## Status

- **Done:** SpiderMonkey, NSPR, NSS (TLS 1.3), SQLite and Gecko run inside
  EX-OS. `firefox --headless --screenshot` renders a page (see
  `tools/exilla/tappa6-schermata.png`).
- **In progress:** a real window in the ExWin window server (`widget/exos`).

The full diary, stage by stage, is in `tools/exilla/leggimi.md` (Italian).

## What is here

| Path | Contents |
|---|---|
| `tools/exilla/patch/` | the changes to Firefox, one patch per file |
| `tools/exilla/gn-exos.sh`, `rust-fornitori.py` | regenerate the mechanical changes (GN files, Rust crates) |
| `tools/exilla/gecko-costruisci.sh`, `mozconfig-gecko` | the build |
| `tools/exilla/prova-*.sh` | the tests inside EX-OS (QEMU) |
| `tools/rust-exos/` | the Rust target `i686-unknown-exos` and its `std` |
| `tools/gcc-exos/` | the GCC toolchain for EX-OS |
| `BASE.txt` | the Firefox version the patches apply to |

## Applying the patches

```
cd firefox-main
for p in ../tools/exilla/patch/*.patch; do patch -p1 < "$p"; done
```

On a newer Firefox, the patches to GN files and Rust crates are redone with
the scripts instead of applied; the others are small changes (`XP_EXOS`) to
carry over by hand where they do not apply.

## Licences

See `LICENZE.md`.

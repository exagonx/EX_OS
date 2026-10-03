# Exilla — Firefox su EX-OS

**🇮🇹 Italiano** · [🇬🇧 English](README.en.md)

Exilla e' il porting di Firefox (Gecko) su [EX-OS](https://github.com/exagonx/EX_OS),
un sistema operativo x86 a 32 bit con CPU di base Pentium MMX. Questo
repository contiene solo cio' che serve al porting: le modifiche ai sorgenti
di Mozilla, gli script che le producono, la toolchain e le prove. I sorgenti
di Firefox si scaricano da Mozilla; quelli di EX-OS (libc, header, kernel) dal
suo repository.

## A che punto e'

- **Fatto:** SpiderMonkey, NSPR, NSS (TLS 1.3), SQLite e Gecko girano dentro
  EX-OS. `firefox --headless --screenshot` disegna una pagina (vedi
  `tools/exilla/tappa6-schermata.png`).
- **In corso:** la finestra vera nel server grafico ExWin (`widget/exos`).

Il diario completo, tappa per tappa, e' in `tools/exilla/leggimi.md`.

## Cosa c'e'

| Percorso | Contenuto |
|---|---|
| `tools/exilla/patch/` | le modifiche a Firefox, una patch per file |
| `tools/exilla/gn-exos.sh`, `rust-fornitori.py` | rigenerano le modifiche meccaniche (file GN, crate Rust) |
| `tools/exilla/gecko-costruisci.sh`, `mozconfig-gecko` | la costruzione |
| `tools/exilla/prova-*.sh` | le prove dentro EX-OS (QEMU) |
| `tools/rust-exos/` | il bersaglio Rust `i686-unknown-exos` e la sua `std` |
| `tools/gcc-exos/` | la toolchain GCC per EX-OS |
| `BASE.txt` | la versione di Firefox a cui le patch si applicano |

## Applicare le patch

```
cd firefox-main
for p in ../tools/exilla/patch/*.patch; do patch -p1 < "$p"; done
```

Su una versione piu' nuova di Firefox, le patch dei file GN e dei crate Rust
si rifanno con gli script invece di applicarle; le altre sono modifiche
piccole (`XP_EXOS`) da riportare a mano dove non entrano.

## Licenze

Vedi `LICENZE.md`.

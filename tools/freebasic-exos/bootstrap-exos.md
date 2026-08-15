# Il bersaglio `exos` in FreeBASIC — FATTO

10 agosto 2026. `fbc` gira dentro EX-OS, sa di produrre per EX-OS, e
compila+assembla+collega in un comando solo.

    /cdrom/exos/bin/fbc /t.bas -x /tb        (codice 0)
    /tb
    prova-fb — compilato dentro EX-OS
      saluto      EX-OS e FreeBASIC
      quadrati     385
      esito       tutto a posto

385 è la somma dei quadrati da 1 a 10: un valore noto in anticipo, non un
«sembra giusto». `fbc -version` dentro EX-OS adesso dice

    FreeBASIC Compiler - Version 1.07.3, built for exos-x86 (32bit)

Prima diceva `linux-x86`, e al link chiedeva `crt1.o` e
`-dynamic-linker /lib/ld-linux.so.2`.

---

## Come si rifà, in quattro passi

! **L'ORDINE È OBBLIGATO, e la trappola è al passo 3**: i `.asm` del
bootstrap li genera un `fbc`, e quel `fbc` deve già conoscere `-target
exos`. Lo `fbc` di sistema (1.10.1) non lo conosce e non lo conoscerà.

```bash
cd FreeBASIC-1.07.3-source-bootstrap

# 1. un fbc 1.07.3 che gira su Linux, dai 145 .c gia' pronti.
#    NON usa lo fbc di sistema: solo gcc. Niente rischio di versioni.
make bootstrap-minimal -j2                       # -> bin/fbc

# 2. le nostre modifiche ai sorgenti del compilatore
cd .. && python3 tools/freebasic-exos/bersaglio-exos.py \
             FreeBASIC-1.07.3-source-bootstrap    # 26 modifiche + 8 header

# 3. si ricostruisce lo STESSO fbc dai sorgenti modificati:
#    adesso conosce -target exos
cd FreeBASIC-1.07.3-source-bootstrap && make compiler -j2
./bin/fbc -print target -target exos              # -> exos-x86

# 4. i 145 .asm per exos
./bin/fbc src/compiler/*.bas -m fbc -i inc -e -r -v -target exos
mkdir -p bootstrap/exos-x86 && mv src/compiler/*.asm bootstrap/exos-x86/

# poi, dalla radice del progetto:
PATH=$HOME/exos-cross/bin:$PATH bash tools/freebasic-exos/prepara-fb.sh
rm -f dist/exos-tools.iso && PATH=$HOME/exos-cross/bin:$PATH make iso
```

! `bootstrap/exos-x86/` **non è nel pacchetto** e non lo sarà mai: il
pacchetto ne porta nove, da `dos` a `win64`. Si genera, ed è il passo 4.

---

## Che cosa cambia `bersaglio-exos.py` — 26 modifiche

È il gemello di `applica.py`: quello tocca la **runtime** (come gira su
EX-OS), questo il **compilatore** (come collega per EX-OS).

| dove | cosa |
|---|---|
| `fb.bi` enum | `FB_COMPTARGET_EXOS`, **in fondo** |
| `fb.bi` default | `#elseif defined(__FB_EXOS__)` |
| `fb.bas` targetinfo | l'ABI, identica a linux |
| `fbc.bas` gnuosmap / targetmap | `-target exos` |
| `fbc.bas` hLinkFiles | `-m elf_i386`, `-static -e _start -Ttext-segment=0x08000000`, `crt0.o` |
| `fbc.bas` deflibs | `c`, `m`, `gcc` — niente pthread/dl/ncurses |
| `fbc.bas` gold | non si chiede se il linker è gold |
| `fbc.bas` lib file | si cerca in `<prefisso>/lib`, e a gcc non si chiede |
| `fbc.bas` lib path | `<prefisso>/lib` fra i `-L` |
| `emit_x86.bas` ×5 | le direttive ELF come linux |
| `inc/crt/*.bi` ×9 | il ramo `__FB_EXOS__` negli smistatori |
| `crt-exos/` → `inc/crt/exos/` | 8 header nostri, copiati |

**Non toccati di proposito**: `ir-gas64.bas` (28 confronti, backend a 64
bit, EX-OS è a 32) e `symb-struct.bas` (2, dentro `if fbIs64Bit()`).
Modificare codice che nessuna prova esercita è come non modificarlo, ma
sembra fatto.

### I tre difetti trovati per strada

1. ! **In un inizializzatore di tabella FreeBASIC il commento vuole il
   trattino basso davanti** (`_ '' testo`). Senza, `fbc` risponde
   «Expected expression, found '''» e nomina una riga che non è quella
   sbagliata.
2. ! **`fbc` chiede a `gcc` dove sono le librerie**
   (`gcc -m32 -print-file-name=…`), e la risposta la legge con `popen()`.
   Dentro EX-OS l'output del figlio finisce sulla **console** invece che
   nel buffer: quella riga solitaria con il percorso di `libgcc.a` in
   mezzo all'output di `fbc` era esattamente questo, e la funzione rendeva
   una stringa vuota. Per `exos` non si chiede: si guarda in
   `<prefisso>/lib` e si smette.
3. ! **`fbextra.x` serve a ogni collegamento**, ed è un file di
   FreeBASIC, non nostro. `fbc` lo passa a `ld` prendendolo da `<libpath>`:
   se manca, `ld` si ferma su «cannot open linker script file», che parla
   di uno script e non del fatto che nessuno l'ha installato. Lo installa
   `prepara-fb.sh`.

### Il nome della directory è il nome del bersaglio

`lib/freebasic/exos-x86/`, non più `linux-x86/`. Lo compone `fbc` da solo.
Sono allineati: `prepara-fb.sh`, la regola del CD nel `Makefile`,
`boot/help.txt` e `leggimi.md`.

---

## Gli header `.bi` del C — FATTI anche quelli

    fbc /t2.bas -x /t2               (codice 0)
    /t2
    Gli header di FreeBASIC dentro EX-OS
      strlen da crt.bi   : 5     (atteso 5)
      somma dei quadrati : 385   (atteso 385)
      lunghezza stringa  : 36    (atteso 36)

`strlen` arriva da `crt.bi`, cioè dalla **nostra libc vista da FreeBASIC**:
i due mondi si parlano.

### ! NON si è fatto `__FB_EXOS__` uguale a `__FB_LINUX__`

Sarebbe stato di cinque minuti ed è la cosa sbagliata. I tipi non
coincidono, e sbagliarli **non dà un errore, dà numeri sbagliati**:

| tipo | EX-OS | Linux x86 | |
|---|---|---|---|
| `time_t` | `longint` (8 byte) | `clong` (4) | **DIVERSO** |
| `dev_t` | `ulong` (4) | `__u_quad_t` (8) | **DIVERSO** |
| `CLOCKS_PER_SEC` | 100 | 1000000 | **DIVERSO** |
| `BUFSIZ` | 4096 | 8192 | **DIVERSO** |
| `FILENAME_MAX` | 256 | 4096 | **DIVERSO** |
| `L_tmpnam` / `TMP_MAX` | 64 / 32 | 20 / 238328 | **DIVERSO** |
| `fpos_t` | `clong` (4) | `longint` (8) | **DIVERSO** |
| `struct tm` | 9 campi | 11 campi | **DIVERSO** |

! **E sei valori di `errno` pure.** `crt/errno.bi` usa la numerazione di
MSVC, EX-OS quella di Linux: `ENOSYS` è 40 là e 38 qui, `EILSEQ` 42 contro
84, e così `EDEADLK`, `ENAMETOOLONG`, `ENOLCK`, `ENOTEMPTY`. Un `errno =
ENOSYS` compilerebbe e non entrerebbe mai nel ramo giusto. I 77 valori sono
stati **generati** da `lib/include/libc.h`, non ricopiati a mano.

### I file, e la regola che li ha scritti

`tools/freebasic-exos/crt-exos/` → `inc/crt/exos/` e `inc/crt/sys/exos/`:

    sys/types.bi   i tipi POSIX, ognuno con accanto la riga di libc.h
    time.bi        CLOCKS_PER_SEC, struct tm, timespec, gettimeofday
    stdio.bi       le costanti, FILE opaco, fpos_t, popen/pclose/fileno
    stdlib.bi      mkstemp
    ctype.bi       VUOTO di proposito: _toupper/_tolower non esistono qui
    fcntl.bi       solo i flag che open() onora davvero
    wchar.bi       wint_t, mbstate_t, WEOF, MB_CUR_MAX
    errno.bi       i 77 valori, generati da libc.h

! **La regola è la stessa in tutti e otto: si dichiara solo ciò che la
libc ha davvero.** Il `.bi` di Linux dichiara `getw`, `putw`, `_toupper`,
`gmtime_r`, `timegm`, `O_DIRECTORY`, `O_CLOEXEC`… Portarseli dietro darebbe
programmi che compilano e non si collegano — o peggio, flag accettati e
ignorati, cioè una promessa. Un header che promette è peggio di un header
assente.

! **Due trappole del formato, trovate provando:**

1. `crt/time.bi` dichiara da sé `clock`, `time`, `mktime`, `gmtime`… subito
   **dopo** aver incluso il file di piattaforma: ripeterle dà «Duplicated
   definition» su ognuna. Alla piattaforma restano i tipi.
2. `ssize_t` e `size_t` li definisce `crt/stddef.bi`. Rifarli è lo stesso
   errore.

### Il contratto sui tipi è sorvegliato

`tools/iso/prova-fb2.bas` ora comincia con tre `#error` a compilazione:

    #if sizeof(time_t) <> 8   -> "time_t deve essere 8 byte"
    #if sizeof(clock_t) <> 4
    #if sizeof(off_t) <> 4

! **E la prova è stata provata**: cambiando l'attesa da 8 a 4, l'errore
scatta e nomina il tipo. Un controllo che non si è mai visto fallire non è
un controllo.

Il giorno che `lib/include/libc.h` cambia un tipo senza che `crt/exos/`
segua, la compilazione si ferma e dice quale — invece di dare date assurde
sei mesi dopo.

## Altre due cose rimaste lì

- **Due avvisi di `ld` a ogni collegamento**, entrambi innocui e entrambi
  rumore:

      ld: warning: …/fbextra.x contains output sections; did you forget -T?
      ld: warning: /t.o: missing .note.GNU-stack section implies executable stack

  Il primo lo dà anche FreeBASIC a monte con binutils recenti. Il secondo
  si toglie con `-z noexecstack`, che `prepara-fb.sh` già passa quando
  costruisce `fbc` stesso: andrebbe aggiunto anche alla riga che `fbc`
  costruisce per i programmi.
- **Il compilatore su Linux è anche un cross-compilatore per EX-OS.**
  `FreeBASIC-…/bin/fbc -target exos` con `AS`/`LD` che puntano al cross
  produce un ELF32 i386 per EX-OS senza passare da QEMU — comodo per
  provare in fretta. Serve che `lib/crt0.o`, `lib/libc.a`, `lib/libm.a`,
  `lib/libgcc.a` e `lib/freebasic/exos-x86/` esistano nell'albero: ce li
  ho messi a mano, non li mette nessuno script.

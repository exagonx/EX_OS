# Dove riprendere — 5 agosto 2026

Appunti scritti prima di un riavvio. Non e' documentazione: e' lo stato di
un lavoro a meta'.

## ⚠️ Cosa si perde con il riavvio, e cosa no

`/tmp` viene svuotato. **Le immagini di prova vanno rifatte** (la ricetta
e' qui sotto, sono cinque minuti). Tutto il resto sopravvive:

| dove | cosa |
|---|---|
| il repository | tutte le modifiche ai sorgenti, non committate |
| `~/exos-cross` | cross toolchain + sysroot (libc.a e header aggiornati) |
| `~/gcc-build-rel` | `cc1` e `xgcc` costruiti, gia' ricollegati |
| `~/exos-native/build-nativi` | `as` e `ld` ricostruiti da zero |
| `~/openssl-build-exos` | `libcrypto.a` per i386-exos, 5,8 MB |

## Il difetto aperto: cc1 legge zero byte

`cc1` apre il sorgente, ne riconosce il nome, e produce una unita' di
traduzione VUOTA. Su ext2 compare il messaggio che lo localizza:

    cc1: warning: /src/prova.c is shorter than expected

Viene da `read_file_guts()` in `gcc/libcpp/files.cc`: `total != size`,
cioe' **`read()` rende meno byte di quanti `st_size` ne dichiari**.

**Gia' escluso, misurandolo:**

- non e' il filesystem (stesso sintomo su FAT32 e su ext2);
- non sono le syscall: `tools/prove/leggi.c` riproduce la sequenza di
  libcpp alla lettera e legge tutti i byte da entrambi i supporti;
- non e' iconv (`HAVE_ICONV` non definito);
- non sono i flag (`O_NOCTTY` e `O_BINARY` valgono 0 in
  `gcc/libcpp/system.h`);
- `struct stat` e' coerente: 60 byte, `st_size` a offset 24.

**Prossimo passo, deciso:** strumentare `read_file_guts` — stampare fd,
size, count, errno — ricompilare quel solo file e rilinkare. Interrogare
cc1 da fuori ha smesso di rendere.

## Come rifare il disco di prova ext2

⚠️ `mke2fs` non e' installato: formatta EX-OS stesso, come `tools/mkhd.sh`.

    qemu-img create -f raw /tmp/ext2disk.img 512M
    printf 'label: dos\nstart=2048, type=83, bootable\n' | /usr/sbin/sfdisk /tmp/ext2disk.img

poi dentro QEMU, con il CD degli strumenti attaccato:

    mkfs -t ext2 -L src hd0p1     <- CHIEDE CONFERMA: rispondere `si`
    mount hd0p1 /src
    cp /cdrom/prova-cc1.c /src/prova.c

⚠️ Senza la risposta a `mkfs`, il comando dopo viene mangiato come
risposta e il montaggio fallisce con un `-2` che sembra tutt'altro.

La riga per QEMU:

    EXOS_RAM=512M EXOS_QEMU_EXTRA="-drive file=/tmp/ext2disk.img,format=raw,if=ide,index=0 -drive file=dist/exos-tools.iso,media=cdrom,if=ide,index=2" python3 tools/qemu_drive.py ...

⚠️ **Lanciare cc1 con `&`**, non in primo piano: in primo piano il
messaggio d'errore e il codice di uscita non si vedono, e una morte
sembra una lentezza. E' cosi' che il difetto precedente e' rimasto
nascosto per ore.

## Cosa e' stato chiuso in questa sessione

- **`struct timeval` diversa fra `lib/libc.c` e `lib/include/libc.h`**:
  residuo del passaggio a `time_t` a 64 bit. Faceva tornare a
  `gettimeofday()` un tv_sec con `tv_usec` nella meta' alta, e cc1 moriva
  con `internal compiler error: in validate_phases`. Corretto.
- **`fclose(stdout)` tornava -1** perche' il kernel rifiuta `close(0/1/2)`.
  Ora svuota e riesce.
- **Entropia nel kernel** (`kernel/arch/x86/entropia.c`) + `SYS_RANDOM` +
  `getentropy()`/`getrandom()` in libc. OpenSSL la usa senza patch.
- **OpenSSL**: `libcrypto.a` costruita e provata dentro EX-OS —
  SHA-256("abc") corrisponde al vettore di FIPS 180-4.
- `nanosleep`, `getuid/geteuid/getgid/getegid` in libc.
- Driver **PCnet**, `SYS_DMA_ALLOC`, `hwconfig`, `keymap` (6 disposizioni),
  `telnet`, `!silenced` negli script.

`libctest`: **290 prove su 290**. `make verify` verde.

## Rimasto indietro di proposito

- `libssl` non si costruisce: `ssl/rio/` di OpenSSL 4.x vuole `fd_set` e
  il polling su socket. Serve un BIO sopra lo stack IPC — lavoro vero, non
  uno stub.
- `RAND_bytes` rifiuta su macchina appena accesa: il serbatoio e' 32 byte
  e una sola chiamata lo svuota. La via giusta e' ChaCha20 in kernel per
  espandere il seme (vettori di prova in RFC 8439), non allargare il
  serbatoio.
- **Obiettivo dichiarato dall'utente**: compilare i sorgenti di GCC e cpp
  dentro EX-OS, cosi' che aggiornare la suite voglia dire scaricarli. Il
  difetto qui sopra e' il primo gradino di quella scala.

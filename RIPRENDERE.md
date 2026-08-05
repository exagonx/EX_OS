# Dove riprendere — 5 agosto 2026

Appunti su un lavoro a metà. Non è documentazione: è lo stato di ciò che
sta girando adesso e di ciò che si è appena capito.

## Il difetto «cc1 legge zero byte» è CHIUSO, e la causa non era dove si cercava

`cc1` dentro EX-OS apriva il sorgente, lo riconosceva, e produceva
un'unità di traduzione **vuota** con uscita 0. Su ext2 lo diceva anche:

    cc1: warning: /src/prova.c is shorter than expected

Non era il filesystem, non erano le syscall, non era iconv. Era un
**cambio di ABI nella libc, subìto da binari solo ricollegati**.

Il 4 agosto `time_t` è passato da `long` a `long long` — riga giusta, per
non finire nel 2038. Ma `struct stat` contiene tre `time_t` in coda, e da
48 byte è passata a 60. Il giorno dopo il sysroot aveva la libc nuova e
`cc1` era ancora fatto di oggetti del giorno prima, che di `struct stat`
ne conoscevano 48. Quindi:

```c
struct stat st;   /* 48 byte per cc1, 60 per la libc che ci scrive dentro */
size_t limit;     /* calpestato */
off_t  offset;    /* calpestato */
int    fd;        /* CALPESTATO dalla metà bassa di st_mtime  →  0 */
```

`fd` diventava **0**, cioè stdin; e un processo lanciato con `&` che legge
da stdin trova la fine dell'input (è il job control, vedi `sys_read`).
Quindi `read()` rendeva zero byte **senza errore** — il sintomo che non
somigliava per niente alla causa.

⚠️ **La misura che ha risolto**: strumentare `read_file_guts()` in
`gcc/libcpp/files.cc` con `write(2, ...)`. Con `fprintf(stderr, ...)` non
si vedeva niente, perché il processo moriva subito dopo e il buffer se ne
andava con lui. Su un programma che si schianta l'unica stampa che vale è
quella già consegnata al kernel.

## La ricostruzione del bersaglio è FATTA, e verificata

    tools/ricostruisci-bersaglio.sh        (log: ~/ricostruzione-bersaglio.log)

Ricostruisce **tutto** il codice di terzi per `i386-exos` nell'ordine in
cui dipende da sé stesso: libc → gmp/mpfr/mpc → libm → libgcc/libstdc++ →
binutils nativi → cc1 → openssl → CD. Sono ore: `cc1` va a `-j1` su 4 GB
di RAM.

Alla fine scrive l'impronta della libc in `$SYSROOT/.abi-libc`.
`tools/ricostruisci-bersaglio.sh --verifica` la confronta con quella
corrente e dice se il bersaglio è disallineato — è il controllo che, se
fosse esistito, avrebbe risparmiato due giorni.

## Come rifare il disco di prova ext2

⚠️ `mke2fs` non è installato: formatta EX-OS stesso, come `tools/mkhd.sh`.

    qemu-img create -f raw /tmp/ext2disk.img 512M
    printf 'label: dos\nstart=2048, type=83, bootable\n' | /usr/sbin/sfdisk /tmp/ext2disk.img

poi dentro QEMU, con il CD degli strumenti attaccato:

    mkfs -t ext2 -L src hd0p1     <- CHIEDE CONFERMA: rispondere `si`
    mount hd0p1 /src
    cp /cdrom/prova-cc1.c /src/prova.c

⚠️ Senza la risposta a `mkfs`, il comando dopo viene mangiato come
risposta e il montaggio fallisce con un `-2` che sembra tutt'altro.

La riga per QEMU:

    EXOS_RAM=512M EXOS_QEMU_EXTRA="-drive file=/tmp/ext2disk.img,format=raw,if=ide,index=0 -drive file=dist/exos-tools.iso,media=cdrom,if=ide,index=2" python3 tools/qemu_drive.py "mount hd0p1 /src@5" "/cdrom/bin/cc1 /src/prova.c -O2 -o /src/prova.s &@240" "ls /src@8"

⚠️ **Lanciare cc1 con `&`**, non in primo piano: in primo piano il
messaggio d'errore e il codice di uscita non si vedono, e una morte sembra
una lentezza.

## FreeBASIC gira su EX-OS (5 agosto, sera)

`fbc` 1.07.3 compila dentro EX-OS, `as` assembla, `ld` collega, il
programma parte e stampa i numeri giusti. La strada, i limiti e il passo
che manca stanno in `tools/freebasic-exos/leggimi.md`.

⚠️ **Il link finale si fa a mano**: `fbc` crede di produrre per Linux (il
bootstrap è `-target linux-x86`) e affida il link a `gcc` con opzioni
Linux. Il comando che funziona è nel leggimi.

Tre cose entrate nella libc per questo, che **non riguardano solo
FreeBASIC**:

- `system()`, `popen()`, `pclose()` — prima erano `ENOSYS`. Ora passano da
  `/bin/sh -c`, che è potuto nascere perché la shell adesso vede i propri
  argomenti (`bin/sh/start.S`).
- `spawn()` cerca nel **PATH** un nome senza barre, come `execvp`. Serviva
  a `fbc` per trovare `as`; serve allo stesso modo al driver di GCC e a
  `make`.
- `<wchar.h>` e `<wctype.h>` veri: 26 funzioni, codifica Latin-1
  dichiarata (non UTF-32 finto).

## La catena in C è chiusa, e provata

`cc1 /src/prova.c -O2 -o /src/prova.s` dentro EX-OS produce **1113 byte**,
gli stessi che produce il `cc1` del cross sullo stesso sorgente con lo
stesso nome di file. Poi `as` → `ld` → esecuzione, **uscita 70**, che è il
valore calcolato a mano (`385 & 0x7F` più `'E'`).

## Il prossimo passo

1. Gli **header**: `--prefix=/exos` e la catena di `-isystem`, che
   finora è stata evitata di proposito (`prova-cc1.c` non ha un solo
   `#include`, così una prova fallita risponde a una domanda sola).
2. Poi i linguaggi oltre il C. `c++` ha già `libstdc++.a` nel sysroot e
   `provacpp` gira; **Ada** vuole `gnat1` e un `libgnat` — e vuole un
   compilatore Ada già funzionante per costruirsi, che è il vero ostacolo.

## Rimasto indietro di proposito

- `libssl` non si costruisce: `ssl/rio/` di OpenSSL 4.x vuole `fd_set` e
  il polling su socket. Serve un BIO sopra lo stack IPC — lavoro vero, non
  uno stub.
- `RAND_bytes` rifiuta su macchina appena accesa: il serbatoio è 32 byte e
  una sola chiamata lo svuota. La via giusta è ChaCha20 in kernel per
  espandere il seme (vettori di prova in RFC 8439), non allargare il
  serbatoio.

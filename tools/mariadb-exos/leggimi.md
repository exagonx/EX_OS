# MariaDB su EX-OS

Il server SQL di EX-OS: MariaDB, portato. Parte prima del login e della shell,
una volta configurato, e da li' vive da solo.

- **I sorgenti**: l'ultima versione pubblicata, **11.8.9** (scelta
  dell'utente il 25/10: una versione uscita, non la testa del ramo 11.8):
  `mariadb-11.8.9.tar.gz` e la sua `.asc` da
  https://archive.mariadb.org/mariadb-11.8.9/source/, firma verificata
  (MariaDB Signing Key 177F4010FE56CA3336300305F1656F24C74CD1D8), in
  `cross_build/terze-parti/mariadb`. `costruisci.sh` lo estrae in
  `cross_build/costruzione-mariadb-src`. Il clone `mariadb-server/` della
  radice (11.8.9 + 300 commit, si chiamava 11.8.10) non serve piu'.
- **Una versione nuova**: `VER` in `costruisci.sh`, l'archivio accanto al
  vecchio; le cartelle di compilazione ripartono da sole. Le modifiche di
  EX-OS **non** si scrivono dentro l'albero: stanno qui, in
  `tools/mariadb-exos/patch/`, e le applica `costruisci.sh`.
- **La licenza**: GPL 2, come EX-OS.
- **Dove va**: sorgenti e compilati sulla ISO degli strumenti; il servizio nel
  repository di `netupdate`, a 64 bit e a 32 se si riesce.

> **Stato al 25 ottobre 2026: MariaDB 11.8.9 gira a 32 e 64 bit**
> (`mariadbd` statico, 27 MB senza simboli, il client `mariadb`,
> `mariadb-admin`, `mariadb-dump` e gli altri) sulle librerie vere di EX-OS
> (ncurses, zlib, bzip2, xz, lz4, zstd, PCRE2, fmt, OpenSSL 4.1.0). **In
> QEMU `mariadbd --bootstrap` crea le tabelle di sistema** e il server
> ascolta su 127.0.0.1:3306 (`tools/prova_mariadb.sh`, 32 e 64 bit). Il
> servizio e' `mariadb-svc` (sotto, "Il servizio"); gruppo `[mariadb]` della
> ISO degli strumenti. A 32 bit serviva `_FILE_OFFSET_BITS=64` nella libc. Per arrivarci:
> `sigwait`/`sigsuspend` nel kernel, `open` di una directory, il limite dei
> descrittori giusto (128), `fscanf` che lascia `feof` alla fine del file, e
> le funzioni e intestazioni elencate in "Cosa e' servito" qui sotto.

## Quale versione

`main` e' la 13.2 **alpha**. I rami stabili, al 25/10/2026:

| ramo | versione | maturita' |
|------|----------|-----------|
| 11.4 | 11.4.14 | stable (LTS) |
| 11.8 | 11.8.10 | stable (LTS) |
| 12.3 | 12.3.4  | stable |
| 13.0 | 13.0.2  | stable |
| 13.1 | 13.1.2  | gamma |

Per un servizio che deve stare in piedi da solo serve un ramo **stabile**.
**DECISO (25/10):** 11.8 LTS, e di quel ramo l'ultima versione pubblicata,
11.8.9, dall'archivio ufficiale firmato.

## Sottomoduli

`.gitmodules` ne ha sette; ne servono due: `libmariadb` (la libreria client,
obbligatoria) e forse `wsrep-lib` (solo con Galera, che si spegne). Gli
altri (`rocksdb`, `columnstore`, `duckdb`, `wolfssl`, `libmarias3`) restano
vuoti: motori e TLS che non si costruiscono.

## Come si costruisce: incrociato con CMake

- Toolchain: `cross_build/nasi5deb/exos-cross/bin/i386-exos-*` e
  `exos-cross64/bin/x86_64-exos-*` (GCC 17, quelli di Exilla); `cmake` 3.31
  sull'host. Serve un file toolchain CMake per EX-OS
  (`CMAKE_SYSTEM_NAME=EXOS`).
- **Gli eseguibili che la compilazione lancia** (`comp_err`,
  `gen_lex_hash`, `gen_lex_token`, `factorial`...) devono girare sull'host:
  MariaDB lo prevede con `IMPORT_EXECUTABLES` = `import_executables.cmake`
  di una compilazione nativa Linux fatta prima.
- **Niente download durante la compilazione**: PCRE2 (10.47) e libfmt
  (11.0.2/12.2.0) li scarica `ExternalProject` da GitHub. Si preparano gli
  archivi in locale una volta.
- zlib, PCRE2, fmt, curses e i compressori dal sysroot (le librerie di
  EX-OS, `-DWITH_ZLIB=system` & c.); TLS con OpenSSL 4.1.0
  (`tools/openssl-exos/costruisci.sh`), non wolfSSL.
- Opzioni che spengono quel che non serve o non c'e':
  `-DWITH_WSREP=OFF -DPLUGIN_ROCKSDB=NO -DPLUGIN_COLUMNSTORE=NO
  -DPLUGIN_MROONGA=NO -DPLUGIN_SPIDER=NO -DPLUGIN_CONNECT=NO -DPLUGIN_S3=NO
  -DPLUGIN_OQGRAPH=NO -DPLUGIN_AUTH_PAM=NO -DPLUGIN_AUTH_GSSAPI=NO
  -DWITH_UNIT_TESTS=OFF -DWITH_EMBEDDED_SERVER=OFF`, `-DPLUGIN_DUCKDB=NO`
  dove c'e'. Restano InnoDB, Aria, MyISAM, MEMORY, CSV.

## Cosa MariaDB chiede al sistema, e cosa EX-OS ha

Guardato nei sorgenti (`sql/`, `mysys/`, `vio/`, `tpool/`,
`storage/innobase/`) e nella libc di EX-OS (`lib/include/libc.h`).

**Ci sono gia'** (molti grazie a Exilla): C++ e libstdc++, pthread (mutex,
condizioni con scadenza, `pthread_sigmask`), `mmap`/`madvise`, socket TCP
BSD, `poll`, `fcntl`, `flock`, `fsync`, `pread`/`pwrite` veri (kernel
0.277), `ftruncate`, `getaddrinfo`, `setsockopt` (`SO_REUSEADDR`,
`TCP_NODELAY`), `dlopen`, `getrlimit`, `setuid`, `getpwnam`, `syslog`.

**Si aggira da solo, senza toccare niente:**
- il *thread pool* vuole epoll/kqueue/event port: CMake lo accende solo su
  Linux, Solaris, Windows o con kqueue. Su EX-OS resta spento: **un filo per
  connessione**, il modo classico;
- l'I/O asincrono di InnoDB (libaio, io_uring) e' facoltativo:
  `tpool/aio_simulated.cc` lo fa con i fili;
- i lucchetti di InnoDB usano `futex` solo su Linux/BSD; altrove pthread
  (piu' avanti si puo' collegarli a `SYS_ATTESA_DORMI`/`SVEGLIA`, che e'
  la stessa idea);
- `O_DIRECT`, `posix_fallocate`, `fdatasync`, `preadv` sono tutti dietro un
  `#ifdef`: senza, si usa la strada normale;
- i socket AF_UNIX sono dietro `HAVE_SYS_UN_H`: senza, il server ascolta solo
  in TCP.

**Manca, e va fatto in EX-OS prima o durante il porting:**

1. **`sigwait`. FATTO 25/10 (kernel 0.279):** `SYS_SEG_ATTENDI` (sigwait,
   sigtimedwait, sigwaitinfo) e `SYS_SEG_SOSPENDI` (sigsuspend); un segnale
   con l'azione predefinita mandato a un programma che lo blocca resta in
   attesa invece di terminarlo. Provato con `tools/prova_sigwait.sh`.
2. **Lo spegnimento non avvisa nessuno.** `shutdown`/`reboot` sincronizzano
   i dischi e spengono (`kernel/arch/x86/power.c`), senza dire ai servizi di
   chiudere. InnoDB a ogni avvio farebbe il recupero dopo un crollo. Serve
   un arresto dei servizi prima dello spegnimento (SIGTERM, attesa con
   scadenza, poi lo spegnimento), utile a ogni servizio, non solo a questo.
3. **La connessione locale. FATTO 25/10:** senza AF_UNIX il client sulla
   stessa macchina (`mariadb`, `mariadb-admin`) passa da 127.0.0.1, e il
   loopback in `ip.drv` mancava davvero; c'e' dalla 0.013, provato con un
   server e un client TLS nello stesso programma (`tools/prova_openssl.sh`).
   Resta: una porta chiusa non risponde RST, il client aspetta la scadenza.
4. **I limiti, che decidono quanto server si puo' fare:**
   - **24 connessioni TCP in tutto il sistema** (`IP_TCP_CONNESSIONI`,
     `drivers/net/ip_proto.h`), 64 socket per processo (`PRESE_MAX`);
   - 128 descrittori per processo (`MAX_FD`): tabelle aperte, file di
     InnoDB e connessioni li condividono (`open_files_limit`,
     `table_open_cache` piccoli);
   - 192 processi o fili in tutto (`MAX_PROCESSES`): MariaDB ha una ventina
     di fili suoi piu' uno per connessione;
   - lo spazio di un programma finisce a 3 GB anche a 64 bit: va bene,
     il `innodb_buffer_pool_size` si tiene sotto.
5. **`select` con i socket. FATTO 25/10:** `fd_set` arriva a 1088
   descrittori, i socket (da 1024) compresi (`openssl s_client` lo usa).
   MariaDB usa comunque `poll` dove c'e' (`HAVE_POLL`).

## Cosa e' servito (25/10)

- **libc**: `setgid`, `setreuid`/`setregid`, `setgroups`/`initgroups`,
  `execl`/`execlp`, `getpwent`/`setpwent`/`endpwent`, `getlogin`/`_r`,
  `IN6_IS_ADDR_*`, `NI_NAMEREQD` (sempre `EAI_NONAME`: non c'e' la ricerca
  inversa), `POLLRDNORM` & c., `P_tmpdir`, `suseconds_t`, i tipi BSD in
  `<sys/types.h>`, `ULLONG_MAX` in C++, `_POSIX_PATH_MAX`; intestazioni
  `netinet/ip.h`, `netinet/in_systm.h`, `sys/poll.h`; `RLIMIT_NOFILE` 128
  (diceva 32, valore di prima della 0.232); `fscanf` lasciava `feof` spento
  dopo l'ultimo dato (InnoDB legge cosi' `ib_buffer_pool`).
- **kernel 0.279**: i segnali (punto 1) e `open(dir, O_RDONLY)`, che Aria e
  InnoDB usano per l'fsync della cartella dei dati.
- **in `costruisci.sh`**: `DISABLE_SHARED` (niente `.so`; i plugin del
  client dentro la libreria, `CLIENT_PLUGIN_*=STATIC`), la patch
  `patch/libmariadb-senza-so.diff` (libmariadb faceva la `.so` comunque),
  niente thread pool (vuole epoll), niente plugin feedback, niente socket
  UNIX (`HAVE_SYS_UN_H` vuoto: il server ascolta solo in TCP), niente
  `link.h`, `-fcommon` per `PC`/`BC`/`UP` di readline contro ncurses,
  pkg-config spento anche nel file toolchain.
- **da configurare**: `innodb_buffer_pool_size_max` (InnoDB riserva 8 TB di
  indirizzi a 64 bit, un programma di EX-OS ne ha 3 GB).

## Il servizio

`mariadb-svc.c` (in questa cartella; `costruisci.sh installa` lo compila in
`/exos/bin`). Scelte dell'utente, 25/10: nome `mariadb-svc`, `/etc/my.cnf` e
`/var/lib/mysql`, utente `mysql` dedicato.

- `-install` (root): l'utente `mysql` in `/boot/utenti` (uid libero fra 100
  e 999, nessuna riga in `/boot/ombra`: nessuno entra come mysql),
  `/etc/my.cnf` se manca, `/var/lib/mysql`, `/var/lib/mysql-tmp`,
  `/var/log/mariadb`; le tabelle di sistema con `mariadbd --bootstrap <
  share/bootstrap.sql` (quello che farebbe `mariadb-install-db`, che e' uno
  script di shell); il blocco `mariadb-svc` in `/boot/avvio.sh`
  (`avvio_blocco`) con `ip.drv &` se manca; accende `ip.drv` se non gira, poi
  il server. Prima di `/etc/my.cnf` crea il certificato TLS (0.002): chiave
  EC P-256 e certificato autofirmato con la libcrypto di EX-OS,
  `server-cert.pem`/`server-key.pem` in `/var/lib/mysql`, `ssl-cert` e
  `ssl-key` in my.cnf solo se i file ci sono (un ssl-cert che manca ferma il
  server). Senza, MariaDB 11.8 genera una RSA da 4096 bit a ogni avvio
  (`vio_keygen` in `vio/viosslfactories.c`): in QEMU a 32 bit 21 s una volta,
  136 s un'altra, e `-install` scadeva.
- `-avvia`: aspetta `ip.drv` (127.0.0.1 sta dentro di lui), lancia
  `mariadbd --defaults-file=/etc/my.cnf` e aspetta «ready for connections» nel
  registro (non una connect: una porta chiusa non riceve RST da ip.drv).
- `-ferma`: SIGTERM al pid di `mariadbd.pid`, aspetta fino a 120 s.
- `-stato`, `-remove` (dati e my.cnf restano).
- Lo spegnimento: il kernel (0.279, `segnali_arresto`) manda SIGTERM a ogni
  programma e aspetta fino a 30 s prima di sincronizzare i dischi.
- Senza scheda di rete `ip.drv` (0.014) resta acceso col solo 127.0.0.1.

Prova: `tools/prova_mariadb_svc.sh` (32 bit, disco installato da
`tools/mkhd.sh` senza scheda di rete).

## Tappe

1. Scegliere il ramo stabile; `git submodule update --init libmariadb`.
2. Compilazione nativa Linux per gli eseguibili dell'host
   (`import_executables.cmake`); archivi locali di PCRE2 e libfmt.
3. File toolchain CMake per EX-OS a 64 bit; prima configurazione: l'elenco
   di quel che CMake non trova e' la lista vera dei buchi della libc.
4. In EX-OS: `sigwait`, l'arresto dei servizi allo spegnimento, la prova di
   127.0.0.1.
5. `mariadbd` che parte in QEMU a 64 bit, `mariadb-install-db`, un client
   che si collega, una tabella InnoDB che sopravvive a un riavvio pulito.
6. Il servizio (`-install`, log, utente), poi ISO degli strumenti e netinst.
7. Lo stesso a 32 bit.

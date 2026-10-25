# MariaDB su EX-OS

Il server SQL di EX-OS: MariaDB, portato. Parte prima del login e della shell,
una volta configurato, e da li' vive da solo.

- **I sorgenti**: `mariadb-server/` nella radice del progetto, clone di
  https://github.com/MariaDB/server. Fuori dalla cronologia di EX-OS
  (`.gitignore`), dentro MEGA e dentro `backup.sh` con la sua `.git`.
- **Al passo con l'ufficiale**: `git -C mariadb-server pull`, poi si
  riapplicano le nostre patch e si ricompila. Le modifiche di EX-OS **non** si
  scrivono dentro l'albero: stanno qui, in `tools/mariadb-exos/patch/`, con lo
  script che le riapplica (come `tools/exilla/`).
- **La licenza**: GPL 2, come EX-OS.
- **Dove va**: sorgenti e compilati sulla ISO degli strumenti; il servizio nel
  repository di `netupdate`, a 64 bit e a 32 se si riesce.

> **Stato al 25 ottobre 2026: scelto il ramo 11.8 LTS; prima di MariaDB le
> librerie che usa sono diventate librerie vere di EX-OS** (ncurses, zlib,
> bzip2, xz, lz4, zstd, PCRE2, fmt, OpenSSL 4.1.0 con socket e fili: ognuna
> nel suo `tools/<nome>-exos/`, nei sysroot, a 32 e 64 bit). `costruisci.sh`
> va portato su quelle (le tappe pcre2 e termcap spariscono). Il loopback e
> `select` con i socket ci sono (punti 3 e 5 qui sotto).

## Quale versione

`main` e' la 13.2 **alpha**. I rami stabili, al 25/10/2026:

| ramo | versione | maturita' |
|------|----------|-----------|
| 11.4 | 11.4.14 | stable (LTS) |
| 11.8 | 11.8.10 | stable (LTS) |
| 12.3 | 12.3.4  | stable |
| 13.0 | 13.0.2  | stable |
| 13.1 | 13.1.2  | gamma |

Per un servizio che deve stare in piedi da solo serve un ramo **stabile**,
seguito con `git pull` su quel ramo (riceve le correzioni, non le novita'
che rompono). **DA DECIDERE con l'utente** quale (`git -C mariadb-server
switch <ramo>`).

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

1. **`sigwait` non c'e'** (la libc rende `ENOSYS`). MariaDB ha un filo che
   aspetta SIGTERM, SIGHUP e SIGQUIT con `sigwait` (`sql/mysqld.cc`,
   `my_sigwait`): e' cosi' che si ferma pulito. Serve un `sigwait` vero.
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

## Il servizio

Prima del login e della shell, senza nessuno davanti:

- un programma di EX-OS che lo accende e lo spegne (`mariadb-svc`, nome da
  decidere) con `-install`/`-remove`, che scrivono e tolgono il blocco in
  `/boot/avvio.sh` (`avvio_blocco` della libc, come `nv50.drv`), dopo la
  rete;
- `mariadbd` in sottofondo, come utente suo (`mysql`), non root;
- i messaggi in un file di log (`log_error`), non sulla console;
- la prima configurazione: `mariadb-install-db` crea il datadir e le tabelle
  di sistema; il file di configurazione in `/etc` o `/boot` (da decidere);
- l'arresto pulito allo spegnimento (punto 2 sopra).

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

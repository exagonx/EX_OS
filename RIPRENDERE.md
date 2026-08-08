# Dove riprendere — 6 agosto 2026

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

## La catena in C con cc1 a mano è chiusa, e provata

`cc1 /src/prova.c -O2 -o /src/prova.s` dentro EX-OS produce **1113 byte**,
gli stessi che produce il `cc1` del cross sullo stesso sorgente con lo
stesso nome di file. Poi `as` → `ld` → esecuzione, **uscita 70**, che è il
valore calcolato a mano (`385 & 0x7F` più `'E'`).

## Gli header: FATTI (6 agosto, notte)

    cd /src
    /cdrom/exos/bin/gcc -B/cdrom/exos/libexec/gcc/i386-exos/17.0.0/ \
        -O2 -c inc.c
    -> inc.o, 872 byte, uscita 0

`inc.c` ha `#include <stdio.h>` e `<string.h>`, e il driver li trova DA
SOLO — nessun `-I`. Ci sono voluti quattro difetti del sistema, non di GCC:
il racconto sta piu' avanti, in «Perche' non funzionava».

⚠️ Allora il `-B` serviva, su **libexec**. Dal 6 agosto non piu': vedi
«Il -B non serve piu'» piu' sopra.

## IL `-B` NON SERVE PIU' (6 agosto, sera)

    cd /src
    /cdrom/exos/bin/gcc -O2 -o nb pg.c      <- nessuna opzione di percorso
    /src/nb
    somma dei quadrati 1..10 : 385   (atteso 385)
    ...
    -> nb, 63324 byte: IDENTICO al pg prodotto col -B

Il `-B` era la stampella per una rilocazione che non funzionava. Il driver
ricalcola il proprio prefisso da dove sta lui e lo passa ai figli in
`GCC_EXEC_PREFIX` — ma quell'ambiente non arrivava, perche' `pex-exos.c`
girava a spawn_ex l'env NULL di pex_run() come «nessun ambiente» invece che
come «eredita».

⚠️ **Non e' stato tolto: e' sparito.** Le cinque correzioni servivano tutte
allo stesso scopo senza che si vedesse — l'ambiente, il `..` nei percorsi,
st_ino che distingue le directory, il -1 al posto di -errno, gli argomenti
non troncati — e il `-B` ha smesso di servire da solo. Era il sintomo, non
la malattia.

⚠️ **Il piano per provarlo era un altro, e piu' costoso**: installare
l'albero /exos (140 MB) sul disco rigido, per far combaciare i percorsi
assoluti. Sarebbe stato inutile: bastava riprovare senza `-B` dopo le
correzioni. **Prima di costruire un banco di prova, riprovare il caso
semplice** — il difetto potrebbe essere gia' chiuso da un'altra parte.

## LA CATENA INTERA FUNZIONA — un comando solo (6 agosto)

    cd /src
    /cdrom/exos/bin/gcc -B/cdrom/exos/libexec/gcc/i386-exos/17.0.0/ \
        -O2 -o pg pg.c
    -> pg, 63324 byte, uscita 0

    /src/pg
    La catena intera dentro EX-OS
      somma dei quadrati 1..10 : 385   (atteso 385)
      lunghezza del nome       : 5     (atteso 5)
      divisione a 64 bit       : 64   (atteso 64)
    Compilato, assemblato e collegato qui dentro.

`pg.c` e' `tools/iso/prova-gcc.c`: `#include <stdio.h>` e `<string.h>`,
`printf` con `%lld`, `memcpy` dalla libc, una divisione a 64 bit che chiama
`__divdi3` in libgcc, e una struttura restituita per valore. **I numeri
sono noti in anticipo** — non «sembra giusto».

Il driver ha lanciato da solo cc1, `as`, `collect2` e `ld`, ha trovato gli
header senza un `-I`, e ha collegato con crt0, libc e libgcc. Ci sono
voluti cinque difetti del sistema (nessuno in GCC), raccontati qui sotto.

## Il page fault della catena a piu' stadi: CHIUSO — era strncpy

Il sintomo:

    /cdrom/exos/bin/gcc -B/cdrom/exos/libexec/gcc/i386-exos/17.0.0/ \
        -O2 -o pg pg.c
    [FAULT] PID 12 '/cdrom/exos/libexec/gcc/i386-ex': page fault a
            0x0813b000 (pagina assente, scrittura, EIP=0x080aeef0)

**La causa era in `strncpy` della nostra libc**, e non c'entrava niente
con l'allocatore, con sbrk o con i percorsi:

    while (n-- && (*d++ = *src++));
    while (n--) *d++ = '\0';

Con una sorgente piu' corta di n il conto torna. Con una sorgente lunga
almeno n — cioe' **il caso per cui strncpy esiste** — il primo ciclo esce
perche' `n--` VALE 0, ma il post-decremento scatta lo stesso: n e' un
size_t e passa a SIZE_MAX. Il secondo ciclo si mette a scrivere zeri per
quattro miliardi di byte e si ferma solo sulla prima pagina non mappata.

⚠️ **La gemella larga `wcsncpy` era gia' scritta bene** (`while (n && ...)`
con il decremento dentro il corpo). Quando due funzioni fanno la stessa
cosa su tipi diversi, la differenza fra le due e' il primo posto dove
guardare.

Provato prima sull'ospite con ASan (`stack-buffer-overflow` sulla versione
vecchia, pulito sulla nuova), poi in `libctest` con quattro prove nuove —
compresa una **sentinella dopo il buffer**, senza la quale un riempimento
che sfora di poco resterebbe verde.

### Come si e' arrivati: la strumentazione, non il ragionamento

⚠️ Due sospetti scritti qui ieri erano **entrambi sbagliati**
(`heap_restituisci`, `sys_sbrk` negativo), e leggere il codice non li
smontava: sulla carta erano a posto. A chiudere la partita in una corsa
sola sono stati due klog aggiunti al kernel, che adesso restano:

1. **La mappa del processo sotto il page fault** (`kernel/mm/paging.c`):
   heap, stack e VMA, con `<-- QUI` sulla VMA che contiene l'indirizzo.
   Il nome del processo sta in PROCESS_NAME_LEN byte e i percorsi lunghi
   ci si troncano dentro — `/cdrom/exos/libexec/gcc/i386-ex` puo' essere
   cc1 come collect2 — mentre le VMA si confrontano con `readelf -l` e non
   lasciano dubbi: era collect2.
2. **`sbrk` che restituisce pagine** lo dice a LOG_INFO
   (`kernel/syscall/syscall_impl.c`): e' raro e smappa memoria sotto i
   piedi di un processo vivo.

Poi il colpo decisivo, che non costa niente e va ricordato:

    i386-exos-nm -n <binario> | awk 'strtonum("0x"$1) <= 0x080aeef0' | tail -3
    -> 080aeea0 T strncpy

**L'EIP del fault, cercato nella tavola dei simboli.** Dice in quale
funzione si e' rotto, che e' la domanda a cui tutto il resto girava
intorno.

## IL C++ GIRA DENTRO EX-OS (6 agosto)

    cd /src
    /cdrom/exos/bin/g++ -B/cdrom/exos/libexec/gcc/i386-exos/17.0.0/ \
        -O2 -o pp pp.cpp
    -> pp, 1 266 568 byte, uscita 0

    /src/pp
    La libreria standard del C++ dentro EX-OS

      vector+sort : 1 3 5 7 9
      string      : "std::string concatenata" (23 caratteri)
      cerchio     : area = 12566 (x1000)
      quadrato    : area = 9000 (x1000)
      eccezione   : lanciata e ripresa
      out_of_range : presa da dentro la libreria

    La libreria standard risponde.

`pp.cpp` e' `tools/iso/prova-cpp.cpp`. Non e' un «hello world in C++»:
contenitori con `<algorithm>`, `std::string` (cioe' `operator new` sopra la
nostra malloc), polimorfismo con distruttore virtuale, e **le eccezioni** —
compresa una lanciata da dentro libstdc++ e ripresa attraverso piu' livelli
di stack, che e' il pezzo che ha bisogno del maggior numero di cose
funzionanti insieme.

⚠️ Resta sul disco un `ccHm016b.s` da 4096 byte: il driver **non cancella
il proprio file temporaneo**. Da guardare — non fa danno subito, ma una
directory di lavoro che si riempie a ogni compilazione lo fara'.

### I due pezzi che mancavano sul CD

`g++` non c'era proprio: il Makefile copiava `cpp` e `xgcc`, non `xg++`.

⚠️ **Il nome del driver non e' un'etichetta**: lo stesso binario decide da
COME E' STATO CHIAMATO se compilare in C o in C++, quale cc1 lanciare e se
collegare libstdc++. Con il solo `gcc` sul CD, cc1plus c'era e non
esisteva un modo di arrivarci.

Messo `g++`, la catena e' arrivata fino in fondo e si e' fermata al **link**:

    ld: cannot find -lstdc++
    ld: have you installed the static version of the stdc++ library ?

Gli header C++ erano sul CD, la LIBRERIA no. E' l'errore piu' tardi che
potesse uscire — dopo che cc1plus ha compilato e as ha assemblato — e dice
una cosa sola: `libstdc++.a` (24 MB) va in `/exos/lib`, accanto a libc.a e
libgcc.a, dove ld guarda gia'.

⚠️ **E anche `xg++` andava ricollegato**: quando ho forzato il rilink dopo
la correzione di `strncpy` avevo cancellato `cc1 cc1plus collect2 xgcc cpp`
e NON `xg++`, che e' rimasto indietro di una versione. La prova con il
driver vecchio sarebbe stata inconcludente comunque. Il controllo che lo
smaschera e' la data del binario contro quella di `$SYSROOT/lib/libc.a` —
non quella della copia sul CD, che `make iso` rifa' ogni volta.

## Una prova che misurava la macchina, non il sistema (6 agosto)

`libctest` falliva su «sbrk finisce per rifiutare»... **a 512 MB**, e
passava a 64. Cresceva di 1 MB per 64 volte e pretendeva un rifiuto: su una
macchina piccola a fallire era la RAM, su una grande i 64 MB ci stavano e la
prova diventava rossa su un sistema perfettamente sano.

E non poteva funzionare: il tetto vero e' `heap_max`, a quasi 3 GB da
heap_start. A 1 MB per volta non lo si raggiunge nemmeno in 64 passi.

Ora il tetto si prova con **una richiesta piu' grande dello spazio di
indirizzamento** (`sbrk(0x7FFF0000)`), che dev'essere rifiutata su
qualunque macchina. La crescita a blocchi resta come **misura stampata**,
non come verdetto:

    64 MB  -> (cresciuto di 60 MB, poi la RAM e' finita)     294/0
    512 MB -> (cresciuto di 64 MB, senza esaurire la RAM)    294/0

⚠️ **Una prova che dipende dall'ambiente e' peggio di una prova assente**:
la sua riga rossa fa cercare un difetto che non c'e'. Se una prova ha
bisogno di una macchina particolare, deve dirlo o non essere una prova.

## Tutti i binari del bersaglio sono allineati (6 agosto)

Dopo la correzione di `strncpy` sono stati ricollegati **tutti**, e il
controllo e' la data contro `$SYSROOT/lib/libc.a`:

    cc1  cc1plus  collect2  xgcc  xg++  cpp      (gcc-build-cpp, rilink forzato)
    as-new  ld-new                                (exos-native/build-nativi)
    fbc                                           (prepara-fb.sh, ricostruito)

⚠️ **Non guardare la data della copia sul CD**: `make iso` la rifa' a ogni
giro e risulta sempre fresca, anche quando il binario sorgente e' vecchio
di giorni. E' cosi' che `xg++` e' passato inosservato.

## ⚠️ `make iso` interrotto lascia l'albero a meta', e non si ripara da solo

Una corsa di `make iso` uccisa a meta' lascia `build/iso/` monco — comincia
con `rm -rf $(ISO_ROOT)` e ricostruisce. Il guaio e' il giro dopo:

    make iso
    make: Nessuna operazione da eseguire per «iso».

perche' `dist/exos-tools.iso` risulta piu' recente dei suoi prerequisiti.
L'ISO in `dist/` e' giusta e l'albero in `build/` no, e make non se ne
accorge. Siccome `build/` e' tracciato da git, la cosa si vede come una
frana di file cancellati.

    rm -f dist/exos-tools.iso && make iso    <- l'unico modo di riallinearli

Da tenere presente ogni volta che una prova viene interrotta.

## FreeBASIC ha lib e header come il C, e ora ce li ha davvero (6 agosto)

Mancava un pezzo intero: **i .bi di FreeBASIC non stavano sul CD**. C'erano
fbc e libfb.a, quindi un `.bas` senza `#include` compilava — ed e' proprio
cio' che `prova-fb.bas` prova, di proposito. Con un `#include` non si
andava da nessuna parte, e nessuno se n'era accorto perche' nessuna prova
lo chiedeva.

### FreeBASIC vuole il layout Unix, e lo dice il suo makefile

Prevede due disposizioni. La nostra fbc usa la **non-standalone**:

    <prefisso>/bin/fbc
    <prefisso>/include/freebasic/        i .bi
    <prefisso>/lib/freebasic/<target>/   libfb.a, fbrt0.o

Non e' dedotto dai sorgenti, si legge dal comportamento: con fbc in /bin,
`fbc -v` stampava `assembling: /cdrom/bin/../bin/as`. **Il prefisso e' la
directory dell'eseguibile meno `bin`**, ricalcolata ogni volta.

⚠️ **Ed e' la differenza vera con GCC**: i percorsi di GCC sono COMPILATI
DENTRO (`--prefix=/exos`, dodici stringhe dentro cc1) e per spostarli
servono GCC_EXEC_PREFIX o `-B`. FreeBASIC e' rilocabile per costruzione.
Percio' fbc e' passato da `/bin` a **`/exos/bin`**: da li' trova i propri
inc e lib da solo, ovunque sia montato il CD.

### Dei 31 MB di inc/ se ne copiano 452 KB

Il resto sono binding ad allegro, GTK, SDL, X11, zlib — librerie che su
EX-OS non esistono. ⚠️ **Peggio di un header assente e' un header che
promette**: compilerebbe e il link fallirebbe con simboli mai visti. Si
copia cio' che corrisponde a cio' che c'e': `crt.bi` e `crt/` (che mappano
sulla nostra libc), `fb*.bi` e `fbc-int/` (la runtime).

### Provato: la ricerca degli header FUNZIONA

`tools/iso/prova-fb2.bas` e' il gemello di `prova-fb.bas` CON `#include
"crt.bi"`, sulla falsariga della coppia prova-cc1.c / prova-gcc.c. Dentro
EX-OS:

    /cdrom/exos/bin/fbc -v f2.bas
    compiling:    f2.bas -o f2.asm (main module)     <- l'include e' stato TROVATO
    assembling:   as --32 --strip-local-absolute "f2.asm" -o "f2.o"

    f2.bas   2945      il sorgente con #include "crt.bi"
    f2.asm   4010      fbc ha compilato
    f2.o     2516      as ha assemblato
    [1] terminato: fbc -v f2.bas (codice 1)     <- cade al link

Nessuna opzione, nessun `-i`: fbc ha risolto
`/cdrom/exos/include/freebasic/crt.bi` da solo. Il sorgente e' verificato
prima su Linux (uscita 0, valori attesi 5/385/36), cosi' un errore di
sintassi non si confonde con un difetto di EX-OS.

⚠️ **Il link resta quello di sempre**: fbc crede di produrre per Linux e
affida il link a `gcc` con opzioni Linux — vedi
`tools/freebasic-exos/leggimi.md`. Non c'entra con la disposizione delle
directory: e' il passo che mancava gia' prima.

### `as` e `ld` stanno in due posti, e adesso di proposito

fbc cerca `as` in `<prefisso>/bin/as` — cioe' `/exos/bin/as` — mentre GCC
lo vuole in `/exos/i386-exos/bin/`, che e' il `gcc_tooldir` del suo
configure. Con la sola copia sotto i386-exos, fbc non lo trovava, ripiegava
sul nome nudo e lo faceva cercare al PATH.

⚠️ **Funzionava, ma per coincidenza** — e una coincidenza smette di
funzionare il giorno che il PATH cambia, senza che nessuno colleghi le due
cose. Ora `make iso` li copia in entrambi i posti: 2,9 MB in piu' sul CD
per togliere di mezzo un ripiego silenzioso.

### E il prefisso? `/usr` o `/exos`

La struttura che si voleva c'e' gia': `/exos/bin`, `/exos/lib`,
`/exos/include` **sono** la disposizione Unix, con un prefisso diverso dal
solito. Rinominare `/exos` in `/usr` e' una decisione separata e non
gratuita: GCC ha il prefisso compilato dentro, quindi vuole configure e
ricostruzione da capo — ore — mentre FreeBASIC seguirebbe da solo. Il
guadagno e' la familiarita', il costo e' quello. Non e' stato fatto.

⚠️ E se si fa: e' `include/`, non `inc/`. `inc/` e' la disposizione
standalone di FreeBASIC, che non e' quella che usiamo.

## ⚠️ Il progetto sta dentro ~/MEGA/, e MEGAsync ci mette le mani

Durante il lavoro del 6 agosto i file sotto `build/iso/` sono spariti da
sotto le mani piu' volte: 869 header di libstdc++ riscritti a ogni
`make iso` sono esattamente cio' su cui un client di sincronizzazione si
accanisce. Sommato al fatto che un `make iso` interrotto non si ripara da
solo, ha bruciato diverse corse di prova.

Il dubbio serio non e' pero' che CANCELLI: e' che **RIPRISTINI**. Un
binario vecchio risorto dentro un'immagine gia' dichiarata buona renderebbe
falsa una prova passata, e nel modo peggiore — silenziosamente.

**La verifica, fatta a MEGAsync spento** (e da rifare cosi' ogni volta che
il dubbio si ripresenta):

1. **I build dir stanno FUORI da ~/MEGA** (`~/gcc-build-cpp`,
   `~/exos-native`, `~/fb-build-exos`, `~/exos-cross`): i binari veri non
   sono mai stati a rischio. Il rischio era confinato al repository.
2. **I sorgenti**: ogni correzione e' ancora al suo posto (strncpy,
   err_posix, path_normalizza, MAX_SPAWN_ARGS, VFS_IDENT, l'env di
   pex-exos, la mappa nel page fault, le prove nuove).
3. **I binari spediti**: ⚠️ sul CD sono SPOGLIATI DEI SIMBOLI, quindi
   cercarci dentro `strncpy` per nome non funziona. Si strippa il binario
   sorgente allo stesso modo e si confronta byte per byte:

       i386-exos-strip -o /tmp/x ~/gcc-build-cpp/gcc/cc1
       cmp /tmp/x build/iso/exos/libexec/.../cc1

   cc1, cc1plus, collect2, gcc, g++, as, ld, fbc: tutti identici.
4. **L'ISO contro l'albero**, letta a mano dal descrittore Joliet e
   confrontata con sha256: 13 file campione, tutti uguali.
   ⚠️ I nomi nell'ISO portano il suffisso di versione `;1`: un confronto
   per nome esatto non trova NIENTE e sembra un CD vuoto.
5. **Il floppy**: KERNEL.BIN identico a `build/kernel.bin`, e i 14 file di
   `/BIN` identici a `build/bin/`.
6. **Le due prove decisive rifatte**: libctest 294/0, e la catena C con un
   `delete pg` davanti — cosi' l'eseguibile e' ricostruito da zero e non
   riletto.

**Nessun risultato era falso.** Ma il controllo va rifatto, non dato per
scontato, ogni volta che si lavora con la sincronizzazione accesa.

## Il file temporaneo di g++: NON era un difetto (6 agosto)

Dopo `g++ -o pp pp.cpp` restava `ccHm016b.s` in `/src`, e sembrava che il
driver non cancellasse i propri temporanei. **Non e' cosi'**: ripulita la
directory dai `cc*.s` e rifatta una compilazione RIUSCITA, non resta
niente. Il driver pulisce.

⚠️ **Il residuo veniva dalla corsa FALLITA** — quella morta su «cannot find
-lstdc++» perche' libstdc++.a non era ancora sul CD. Era rimasto li' e la
compilazione successiva, che creava un temporaneo con un nome diverso, lo
lasciava in vista: sembrava suo.

⚠️ **La lezione e' sul metodo, non su GCC**: la prova iniziale guardava una
directory che conteneva gia' i rifiuti di un tentativo precedente. Una
prova che non parte da uno stato noto misura anche il passato. Il `delete
cc*.s` prima del comando, e un `ls` a confermare, sono costati due righe.

Resta da guardare, se un giorno da' fastidio: sulla via del FALLIMENTO il
temporaneo sopravvive, e secondo gcc.cc non dovrebbe — `record_temp_file`
lo mette in entrambe le code, `always_delete_queue` e `failure_delete_queue`.

## Il prossimo passo (6 agosto)

1. **Il link finale di FreeBASIC.** `fbc` compila e assembla da solo
   (provato: f2.asm, f2.o), ma crede di produrre per Linux e passa opzioni
   Linux a `gcc`. E' l'ultimo terzo che manca perche' anche il BASIC sia
   una catena chiusa come C e C++.
2. Poi i linguaggi oltre il C. **Ada** vuole `gnat1` e un `libgnat` — e
   vuole un compilatore Ada già funzionante per costruirsi, che è il vero
   ostacolo.

*(Chiusi il 6 agosto, erano in questo elenco: `fbc` ricollegato, `fstat()`
convertita a -1, il file temporaneo di g++ — che non era un difetto —
`as`/`ld` messi dove entrambi i compilatori li cercano, e il `-B`, sparito
da solo.)*

## Rimasto indietro di proposito

- `libssl` non si costruisce: `ssl/rio/` di OpenSSL 4.x vuole `fd_set` e
  il polling su socket. Serve un BIO sopra lo stack IPC — lavoro vero, non
  uno stub.
- `RAND_bytes` rifiuta su macchina appena accesa: il serbatoio è 32 byte e
  una sola chiamata lo svuota. La via giusta è ChaCha20 in kernel per
  espandere il seme (vettori di prova in RFC 8439), non allargare il
  serbatoio.

## Grafica VESA — analizzata, non iniziata (5 agosto 2026)

Richiesta messa da parte dall'utente, che vuole studiarla. Qui resta il
ragionamento, per non rifarlo.

**Si può fare, e non serve un driver per scheda.** Da VBE 2.0 in poi S3
Trio, Mystique, i740 e successive espongono tutte un *linear framebuffer*
dalla stessa interfaccia; gli emulatori la implementano tutti. Un driver
solo.

Il lavoro si divide in tre, e **uno solo tocca il kernel**:

1. **`stage2`**, in modo reale: `INT 10h` si può chiamare solo lì. Sceglie
   il modo e lascia indirizzo e geometria in una struttura. Non è né
   kernel né userspace — è codice di avvio, che il BIOS lo chiama già.
2. **Kernel, ~50 righe**: portare avanti quella struttura, e una syscall
   che **mappi il framebuffer** nello spazio di un processo. Il precedente
   giusto è `SYS_DMA_ALLOC`. Nessun font, nessun disegno.
3. **`/dev/vesa.drv`, ring3**: tutto il resto. Non gli servono nemmeno le
   porte I/O — dopo il mode set basta il framebuffer mappato.

⚠️ **Il limite da accettare in partenza**: la risoluzione si sceglie
all'avvio. Cambiarla a runtime vuole un monitor v8086 o un emulatore di
modo reale nel kernel.

⚠️ **La decisione da prendere PRIMA di scrivere codice**: oggi `klog` e i
panici li stampa il kernel con `vga_putchar_su`. Se la console diventa un
servizio userspace, un panico — o la morte del driver stesso — non ha più
dove scrivere, e il sintomo è **uno schermo fermo senza spiegazione**. Le
due uscite oneste: un disegnatore di testo minimo nel kernel per i soli
klog e panici, oppure la console di emergenza che resta in modo testo.

Il lavoro grosso non è il mode set — sono venti righe — è che il testo in
modo grafico va disegnato: font, blit, scorrimento, cursore. Tutto ciò che
oggi passa da `vga_putchar_su`.

1024x768x32 sono 3 MB di framebuffer da mappare (1,5 MB a 16 bit), su un
sistema che punta a girare in 32 MB.

## I percorsi /exos — chiusi (6 agosto, notte)

Il CD ora ha l'albero che il driver cerca davvero. Non e' una scelta
nostra: si legge dal binario, `strings gcc/cc1 | grep /exos`.

    /exos/libexec/gcc/i386-exos/17.0.0/   cc1, cc1plus, collect2
    /exos/lib/gcc/i386-exos/17.0.0/       libgcc.a, crt*.o, include/
    /exos/i386-exos/include               header di sistema del bersaglio
    /exos/include                         la libc
    /exos/include/c++/17.0.0[/i386-exos]  libstdc++
    /exos/i386-exos/bin/                  as, ld

⚠️ cc1 e cc1plus NON stanno piu' in /bin: tenerli in due posti raddoppiava
il CD (190 MB invece di 118). Le prove dirette vanno fatte col percorso
lungo, /cdrom/exos/libexec/gcc/i386-exos/17.0.0/cc1.

**Tre cose imparate provando, in ordine:**

1. `gcc` lanciato da `/` muore con «Cannot create temporary file in ./».
   Gli serve una directory scrivibile: `cd /src` prima, oppure TMPDIR.
2. Poi: «cannot execute 'cc1': spawn: operazione non permessa». Il
   permesso non c'entra — era un DIFETTO in tools/binutils-exos/pex-exos.c,
   ora corretto: leggeva l'errore da `-pid` invece che da errno.
   ⚠️ Questa nota diceva anche «spawn_ex ritorna -1 e mette errno»: era
   FALSO quando e' stata scritta — ritornava -errno — ed e' diventato vero
   solo il 6 agosto, con il terzo difetto qui sotto. Il messaggio EPERM
   nasceva proprio da quel malinteso.
   ⚠️ La correzione ha effetto solo dopo aver ricollegato GCC (libiberty).
3. Il `-B` puntava alla directory sbagliata: cc1 sta sotto **libexec**, non
   sotto lib/gcc. La prossima prova va fatta con entrambi:

       cd /src
       gcc -B/cdrom/exos/libexec/gcc/i386-exos/17.0.0/ \
           -B/cdrom/exos/lib/gcc/i386-exos/17.0.0/ -O2 -o inc inc.c

⚠️ I percorsi compilati dentro sono ASSOLUTI (/exos/...): combaciano quando
il CD e' la radice o quando l'albero viene installato sul disco. Montato su
/cdrom serve il -B, e questa e' la ragione per cui esiste.

### Perche' non funzionava: QUATTRO difetti, uno dietro l'altro

⚠️ **Nessuno dei due sospetti scritti qui sopra era quello giusto**, e la
pista «e' `-o`, e' `make_temp_file`» era sbagliata: `gcc -c` senza `-o`
falliva identico. Quello che c'era sotto erano quattro difetti in fila,
ognuno nascosto dal precedente, e ognuno con un messaggio che accusava
qualcun altro.

**1. Il kernel troncava gli argomenti a 16, in silenzio.**
`MAX_SPAWN_ARGS` valeva 16. Il driver lancia cc1 con DICIASSETTE argomenti
(diciotto con `-v`) e il taglio cadeva esattamente fra `-o` e il nome del
file:

    cc1 -quiet -v -iprefix <p> inc.c -fno-pic -fno-asynchronous-unwind-tables
        -quiet -dumpbase inc.c -dumpbase-ext .c -mtune=i386 -march=i486
        -O2 -version -o ./ccXXXXXX.s
                        ^ il sedicesimo

Da cui «missing filename after '-o'», che manda a cercare il difetto nella
generazione dei nomi temporanei — dove non c'era.
Ora il tetto e' 64, le stringhe stanno in un'arena sullo heap invece che
sullo stack del kernel, e **superarlo e' un errore (E2BIG) con un klog**:
un comando accorciato di nascosto e' il difetto peggiore che quella
syscall possa produrre. Stesso tetto anche in `bin/sh` (era 16 pure li').

**2. Il kernel non risolveva `..` dentro un percorso assoluto.**
`resolve_path` copiava un percorso assoluto «così com'è». I percorsi di
ricerca di GCC hanno il `..` NEL MEZZO per costruzione —
`/exos/lib/gcc/i386-exos/17.0.0/../../../../i386-exos/include` — perche' e'
cosi' che un compilatore ritrova le proprie cose dopo essere stato spostato.
Il VFS cercava una directory chiamata davvero `..`, non la trovava, cc1
scartava in silenzio ogni directory e rispondeva «no include path in which
to search for stdio.h».
Ora c'e' `path_normalizza()`: `.`, `..` e le barre doppie si risolvono
ovunque, in assoluti e relativi. Diciannove casi provati su Linux con ASan
prima di metterla nel kernel.

**3. La libc rendeva `-errno` invece di `-1`.**
`open()` su un file assente rendeva `-2`. Il codice di terzi non scrive
`< 0`, scrive `!= -1`, perche' e' cio' che lo standard promette:

    file->fd = open (file->path, O_RDONLY, 0666);   /* libcpp/files.cc */
    if (file->fd != -1)
        { fstat (file->fd, &file->st); ... }

cc1 proseguiva con «descrittore» -2, faceva `fstat(-2)` — EBADF — e moriva
con «fatal error: stdio.h: descrittore non valido» su un header che
semplicemente stava nella directory successiva.
Ora le funzioni con un **nome POSIX** rendono -1 e parlano per errno
(`err_posix` in lib/libc.c); quelle **nostre** tengono il -errno, dove
nessuno arriva con un'aspettativa da standard. Aggiornati `cp`, `trunc` e
`install`, che stampavano il numero.

**4. `stat()` dava a ogni file la stessa identita'.**
`st_first_clus` era 0 sempre, «per non inventare un numero», e la libc ci
costruiva sopra `st_ino`. Ma `remove_duplicates()` di GCC confronta
st_dev/st_ino per togliere le directory ripetute dalla lista degli include:
con st_ino tutti uguali ha concluso che le sue sei directory erano una
sola.

    ignoring duplicate directory ".../i386-exos/include"
    #include <...> search starts here:
     /cdrom/exos/lib/gcc/i386-exos/17.0.0/include
    End of search list.

Ne ha tenuta una — quella degli header del compilatore — e buttata proprio
quella con la libc.
Ora il campo si chiama `st_ident` (stessa parola, stessa posizione: nessuna
ABI cambia) e il VFS lo compone come montaggio nei 4 bit alti +
inode/extent/cluster negli altri 28. Vedi `stat_interno()` in
kernel/fs/vfs.c.

⚠️ **E un quinto, corretto per strada**: `pex-exos.c` girava a spawn_ex
l'`env` NULL che `pex_run()` passa sempre. Su Unix quel NULL vuol dire
«eredita» (execv invece di execve); qui voleva dire «ambiente vuoto». GCC
parla ai propri stadi PROPRIO per variabili d'ambiente — GCC_EXEC_PREFIX,
COMPILER_PATH, LIBRARY_PATH, TMPDIR — quindi senza quelle non poteva
funzionare niente di quello che viene dopo cc1.

### Cosa ha richiesto, e cosa NO

Nessuno di questi cambia un tipo condiviso: l'impronta ABI e' rimasta
identica e i binari del bersaglio si **RICOLLEGANO**, non si ricostruiscono.

    tools/ricostruisci-bersaglio.sh libc     # ~1 minuto
    make -C ~/gcc-build-cpp -j2 all-gcc      # ~5 minuti: rilink di cc1
    make && make floppy && make iso

⚠️ `make iso` vuole il cross nel PATH:
`PATH=$HOME/exos-cross/bin:$PATH make iso`.

### La prova che le correzioni non hanno rotto niente

    libctest      ->  290 prove superate, 0 fallite

⚠️ Due erano **gia'** rosse prima di stanotte, e non per un difetto del
sistema: `system()` ha imparato a passare da `/bin/sh -c` il 5 agosto, e le
prove continuavano a pretendere ENOSYS. Ora dicono la verita'. Una suite
che fallisce su un sistema che funziona smette di essere creduta, ed e' il
modo piu' rapido di perdere una rete di sicurezza.

⚠️ Per lanciarla mentre gira gia' un'altra macchina serve
`EXOS_ISTANZA=<n>`: senza, le due condividono `/tmp/exos/serial.txt` e la
seconda cancella l'output della prima da sotto a QEMU — che continua a
scrivere su un file scollegato, e il risultato e' un file vuoto senza
nessun errore.

### La libc adesso sta in DUE posti sul CD

`/exos/include` c'era gia'. Adesso c'e' anche `/exos/i386-exos/include`,
che e' **TOOL_INCLUDE_DIR** — «un altro posto dove potrebbero stare gli
header del sistema bersaglio», gcc/cppdefault.cc — ed e' l'unico dei due
che la rilocazione con `-iprefix` sappia raggiungere. Finche' e' rimasta
una directory vuota, `gcc -c` rispondeva «no include path» con gli header
a due passi. Sono 26 file di testo.


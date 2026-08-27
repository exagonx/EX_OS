# QuickJS su EX-OS

Questa directory tiene **cio' che di QuickJS cambiamo noi**. I sorgenti non
stanno nel repository — `/quickjs/` e' in `.gitignore`, vale la stessa regola
di GCC, della userland e dei sorgenti dei font.

    quickjs-ng 0.16.2, licenza MIT
    Fabrice Bellard, Charlie Gordon, Ben Noordhuis, Saul Ibarra Corretge

## In due comandi

    python3 tools/quickjs-exos/applica.py quickjs
    tools/quickjs-exos/prova-compila.sh quickjs

Il primo adatta l'albero (e `--togli` lo riporta com'era). Il secondo compila,
mette insieme e stampa **quanto viene** e **cosa chiede ancora**.

---

## Il risultato, misurato

    quickjs.c     537 630 byte di codice        -Os, i386
    libunicode.c   57 707
    libregexp.c    26 208
    dtoa.c          8 134
    + openlibm e i quattro nomi di libgcc
    ------------------------------------------
    TOTALE        654 824 byte  (639 KB)

! **CI STA IN UNA FETTA DA UN MEGABYTE.** La previsione scritta in `exwin.ld`
— «un motore JavaScript, che in un megabyte non ci sta per definizione» — era
gia' stata smentita da ExJs, che ne occupa 66 KB. Adesso e' smentita anche dal
motore vero, e per la stessa ragione di allora: a misurare sono gli ELF, non i
commenti. A `-O1` invece sono 946 KB, cioe' **la scelta di `-Os` non e' un
gusto: e' quella che fa entrare il motore nella fetta**.

! **E TUTTO CIO' CHE RESTA INDEFINITO E' LA NOSTRA LIBC.** Ventotto nomi:
malloc, printf, memcpy, strtod, gettimeofday, localtime_r... Nessun buco di
sistema, nessuna syscall che non abbiamo. Lo script lo ristampa ogni volta
apposta: il giorno che in quell'elenco compare qualcosa che non e' della libc,
e' un buco nuovo.

---

## Le cinque modifiche, e perche' sono cinque e non cento

Stanno tutte in `applica.py`, con accanto il motivo. In due righe:

| dove | che cosa | perche' |
|---|---|---|
| `cutils.h` | niente `<pthread.h>` | EX-OS non ha i thread |
| `quickjs.c` | niente `CONFIG_ATOMICS` | `Atomics` senza thread non ha senso |
| `cutils.h` | `JS_HAVE_THREADS 0` | l'interruttore che QuickJS offre gia' |
| `cutils.h` | `gettimeofday` al posto di `clock_gettime` | la nostra libc ha il primo |
| `cutils.h` | `js_exepath` rende -1 | qui non c'e' `/proc` |

! **IL MOTORE VERO NON SI TOCCA.** `quickjs.c` ha una modifica sola, ed e' un
`#if`; l'interprete, il parser, il raccoglitore di memoria, le espressioni
regolari e Unicode compilano per EX-OS **come sono**. Non era scontato, e va
scritto: chi riprende questo lavoro deve sapere che il difficile non e' far
compilare QuickJS.

! **E TUTTE E CINQUE SONO «QUESTO NON E' LINUX»**, mai «QuickJS e' fatto
male». Quattro delle cinque si limitano ad aggiungere `__EXOS__` a un elenco
che esisteva gia' con dentro DJGPP, WASI o Emscripten — cioe' gli altri
sistemi senza thread e senza `/proc`. E' il segno che si sta seguendo una
strada gia' prevista, non che se ne sta aprendo una a forza.

---

## Che cosa ha dovuto imparare la libc

Tre cose, e nessuna delle tre e' un puntello per QuickJS: sono buchi veri, che
si sono visti perche' qualcuno e' passato di li'.

- **`localtime_r` e `gmtime_r`** — le versioni rientranti. Quelle statiche
  fanno si' che `gmtime(&a)` e `gmtime(&b)` nella stessa riga rendano lo
  stesso risultato, e non se ne accorge nessuno.
- **`tm_gmtoff` e `tm_zone` in `struct tm`** — in POSIX dal 2024, sempre `0` e
  `"UTC"` perche' EX-OS non sa in che fuso si trova. Valgono la verita', non
  un fuso inventato. Stanno **in fondo** alla struttura: i campi di prima non
  si spostano di un byte.
- **`malloc_usable_size`** — quanti byte sono davvero utilizzabili dietro un
  puntatore. Serve al raccoglitore di memoria per sapere quanto sta
  occupando; senza, QuickJS conta solo le taglie che ha chiesto lui e sbaglia
  per difetto di tutto l'arrotondamento.

! **`struct tm` E' UN TIPO CONDIVISO: chi la usa va RICOSTRUITO, non
ricollegato.** I campi nuovi stanno in coda, quindi un binario vecchio legge
ancora tutto agli scostamenti di sempre; ma `sizeof` e' cambiato, e chi passa
una propria `struct tm` a `gmtime_r` dev'essere compilato con l'header di
oggi. `make clean && make all`.

E c'e' un file nuovo: **`lib/include/malloc.h`**, che non e' standard e proprio
per questo esiste — mezzo mondo lo include senza chiederselo, e senza il
nostro finiva su quello del sistema ospite.

---

## Che cosa manca, in ordine

### 1. L'ADATTATORE, che e' tutto il lavoro vero

`lib/exdom` e il browser parlano all'interfaccia di **`lib/exjs/exjs.h`** —
una trentina di funzioni: `exjs_apri`, `exjs_esegui`, `exjs_esotico`,
`exjs_chiama`, `exjs_accoda`, `exjs_pompa`... Quell'interfaccia e' stata
divisa in tre librerie proprio perche' oggi si potesse cambiare il motore
**senza toccare ne' il ponte ne' il browser**. Serve un `lib/exqjs/` che la
implementi sopra QuickJS.

Le corrispondenze ci sono quasi tutte:

| exjs.h | QuickJS |
|---|---|
| `exjs_apri` / `exjs_chiudi` | `JS_NewRuntime` + `JS_NewContext` |
| `exjs_esegui` | `JS_Eval` |
| `exjs_esotico` (ganci lettura/scrittura) | `JSClassExoticMethods` |
| `exjs_chiama` / `exjs_invoca` | `JS_Call` |
| `exjs_memoria` | `JS_ComputeMemoryUsage` |
| `exjs_accoda` / `exjs_pompa` | **non c'e'**: la coda dei lavori la teniamo noi, come adesso, piu' `JS_ExecutePendingJob` per le promesse |

! **E IL PUNTO DIFFICILE E' UNO SOLO: I RIFERIMENTI.** ExJs non ha un
raccoglitore e un `ExJsVal` e' un valore che si puo' tenere dove si vuole;
QuickJS conta i riferimenti, e un `JSValue` tenuto da parte senza
`JS_DupValue` e' un puntatore che muore quando il motore decide. Il ponte
tiene involucri per ogni nodo del documento: se ne tiene migliaia.
**La strada e' una tabella di maniglie dentro l'adattatore** — `ExJsVal`
diventa un indice, e la tabella e' l'unica cosa che il raccoglitore vede come
radice. E' li' che questo porting si decide.

### 2. La fetta, e il `.ld`

639 KB ci stanno in una fetta da un megabyte, ma la mappa la fa
`python3 tools/fette.py --libera`, non un commento. Oggi risponde
**`0x04A00000`**, ed e' li' che andra' `exqjs.ld` — da richiedere il giorno
che si scrive, perche' nel frattempo qualcun altro puo' aver preso quella
fetta.

### 3. Il tempo e il caso

QuickJS chiede l'ora a `gettimeofday` (per `Date`) e a `js__hrtime_ns` (per i
tetti di esecuzione). La regola di ExJs — **il tempo arriva da fuori**, vedi
`exjs_pompa` — va tenuta anche qui: una libreria che chiede l'orologio da se'
da' prove che passano oggi e falliscono domani.

### 4. `Math.random` e la memoria

Da guardare quando il motore gira: QuickJS vuole un seme, e alloca a manciate.
Il `malloc` di EX-OS regge, ma non e' mai stato messo sotto un raccoglitore
che libera migliaia di oggetti per ciclo.

---

## E ExJs resta

Non e' codice da buttare il giorno che QuickJS gira: sono 4400 righe che
partono in 66 KB e non chiedono ne' openlibm ne' libgcc. Su una macchina
piccola, o per una pagina che ha uno script di dieci righe, e' il motore
giusto. La scelta fra i due e' un'impostazione, non un bivio da decidere qui.

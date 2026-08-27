# QuickJS su EX-OS

Questa directory tiene **cio' che di QuickJS cambiamo noi**. I sorgenti non
stanno nel repository — `/quickjs/` e' in `.gitignore`, vale la stessa regola
di GCC, della userland e dei sorgenti dei font.

    quickjs-ng 0.16.2, licenza MIT
    Fabrice Bellard, Charlie Gordon, Ben Noordhuis, Saul Ibarra Corretge

## In due comandi

    python3 tools/quickjs-exos/applica.py quickjs
    tools/quickjs-exos/prova-compila.sh quickjs
    make prova-exqjs

Il primo adatta l'albero (e `--togli` lo riporta com'era). Il secondo compila,
mette insieme e stampa **quanto viene** e **cosa chiede ancora**. Il terzo fa
girare le novantadue prove del ponte **con QuickJS sotto**.

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

## L'adattatore c'e', e passa le prove del ponte

`lib/exqjs/exqjs.c` implementa **l'interfaccia di `exjs.h`** sopra QuickJS.
Novecento righe, e questo e' il risultato:

    make prova-exdom     92 prove, 0 sbagliate      con ExJs sotto
    make prova-exqjs     92 prove, 0 sbagliate      con QuickJS sotto

! **LE PROVE SONO LE STESSE E NON E' CAMBIATA UNA RIGA**, ne' in
`tools/prove/domprova.c` ne' in `lib/exdom/exdom.c`. Cambia solo chi
implementa `exjs.h`. Era la promessa scritta in cima a quel file dalla prima
stesura — «il giorno che si portera' QuickJS si sostituisce cio' che sta sotto
senza riscrivere il ponte» — e adesso e' una cosa misurata.

E la pagina vera, quella con i sette riquadri:

    /tmp/exos-prove/domqjs tools/prove/sito/script.html

rende un documento **identico byte per byte** a quello di ExJs, `diff` compreso.

### Come si traducono

| exjs.h | QuickJS |
|---|---|
| `exjs_apri` / `exjs_chiudi` | `JS_NewRuntime` + `JS_NewContext` / `JS_FreeRuntime` |
| `exjs_esegui` | `JS_Eval` |
| `exjs_esotico` (ganci lettura/scrittura) | `JSClassExoticMethods.get_own_property` + `.set_property` |
| `exjs_nativa` | `JS_NewCFunctionData`, con l'indice della tabella come dato |
| `exjs_chiama` / `exjs_invoca` | `JS_Call` |
| `exjs_accoda` / `exjs_pompa` | nostri, piu' `JS_ExecutePendingJob` per le promesse |
| `exjs_memoria` | `JS_ComputeMemoryUsage` |
| `console.log`, `setTimeout` | **nostri**: in QuickJS stanno in quickjs-libc, che scrive su stdout |

! **UN `ExJsVal` E' UNA MANIGLIA.** ExJs non ha raccoglitore e un valore si
tiene dove si vuole; QuickJS conta i riferimenti, e un `JSValue` messo da parte
senza `JS_DupValue` muore quando decide il motore — mentre il ponte ne tiene
uno per ogni nodo del documento. La tabella delle maniglie dentro l'adattatore
e' l'unica radice che il raccoglitore vede da questa parte, e si libera tutta
insieme con la pagina. **Una maniglia nasce solo quando un valore attraversa il
confine C**: un ciclo che costruisce diecimila stringhe dentro il JavaScript non
la tocca nemmeno una volta — in ExJs invece ogni valore costava una casella.

### Le tre cose che il porting ha insegnato

! **1. IL GANCIO GIUSTO E' `get_own_property`, NON `get_property`.** Sembrano
la stessa cosa. Col secondo QuickJS ci consegna la lettura e si ferma a quel
che rispondiamo: il prototipo non viene piu' guardato, cioe'
`elemento.appendChild` non esiste piu'. Col primo ci chiede se la proprieta' e'
NOSTRA, e se diciamo di no prosegue lui — proprie, gancio, prototipo, che e'
esattamente l'ordine scritto in `exjs.h`. Prima della correzione: 75 prove
rosse su 92, tutte con «not a function».

! **2. UN OGGETTO CHE ATTRAVERSA DUE VOLTE DEVE RENDERE LA STESSA MANIGLIA.**
Il sintomo era piccolo e preciso: due `addEventListener` con la stessa
funzione registravano due gestori, e `removeEventListener` non ne trovava
nessuno. Il ponte confronta gli `ExJsVal`, e ogni passaggio ne fabbricava uno
nuovo. La maniglia adesso si scrive **sull'oggetto stesso**, in una proprieta'
che non si elenca: ricerca in tempo costante. E' lo stesso problema che exdom
risolve dall'altra parte con «un nodo si avvolge una volta sola».

! **3. `console.log` E' UNA RIGA.** Mancava l'a-capo finale, e sedici prove
sugli eventi risultavano rosse mostrando il testo giusto: il banco confronta
l'uscita, e due log di seguito diventavano una parola sola. Un difetto da una
riga che sembrava sedici difetti.

## Che cosa manca, in ordine

### 1. La libreria condivisa

    exqjs.c  compilato per i386     8 958 byte
    QuickJS + openlibm + libgcc   654 824 byte
    ------------------------------------------
    TOTALE                        663 792 byte  (648 KB)

Tutto cio' che resta indefinito e' la libc. Serve un `exqjs.ld` alla fetta che
`tools/fette.py --libera` indica (oggi `0x04A00000`), una tabella di
esportazione con gli stessi nomi di `exjs_esporta.c`, e la regola nel Makefile.
Da li' il browser puo' aprire `quickjs.so` invece di `exjs.so` **senza saperlo**:
lo stub e' lo stesso.

### 2. Chi sceglie, e come

Due motori nello stesso sistema vogliono una decisione dichiarata: un'opzione
in `File > Impostazioni` accanto a JavaScript acceso/spento, oppure la regola
«se la pagina ha uno `<script>` che ExJs rifiuta, riapri con QuickJS».
La seconda e' piu' furba e piu' difficile da spiegare; la prima si vede.

### 3. Le maniglie, quando la pagina resta aperta per ore

Non si liberano una per una, perche' l'interfaccia non ha un `exjs_libera`.
Per una pagina va benissimo — muoiono con lei — ma una pagina che chiama una
funzione nativa in un ciclo lungo le consuma. Il giorno che si vede, la
risposta e' una funzione in piu' nell'interfaccia, che ExJs implementa come
una riga vuota: esattamente com'e' andata per `exjs_chiudi`.

### 4. Il tempo e il caso

`Date` e `Math.random` girano gia' (sono di QuickJS). Da guardare quando il
motore sara' dentro EX-OS: `gettimeofday` a 10 ms di risoluzione e un seme per
il generatore che non sia sempre lo stesso.

## E ExJs resta

Non e' codice da buttare il giorno che QuickJS gira: sono 4400 righe che
partono in 66 KB e non chiedono ne' openlibm ne' libgcc. Su una macchina
piccola, o per una pagina che ha uno script di dieci righe, e' il motore
giusto. La scelta fra i due e' un'impostazione, non un bivio da decidere qui.

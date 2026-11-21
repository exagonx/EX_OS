# EX-OS — Il kernel

[🇬🇧 English](kernel.md) · **🇮🇹 Italiano**

[← README](../README.it.md) · [Tutti i manuali](../README.it.md#i-manuali)

**Indice**

- [Architettura kernel](#architettura-kernel)
- [Syscall interface (int 0x80, stile Linux)](#syscall-interface-int-0x80-stile-linux)
- [Arresto e spegnimento](#arresto-e-spegnimento)
- [Identità e versione del sistema](#identità-e-versione-del-sistema)
- [Avvio silenzioso](#avvio-silenzioso)

---

## Architettura kernel

```
RAM CONVENZIONALE < 1MB — Kernel EX-OS (read-only, piccolo)
  GDT | IDT | ISR | VGA | PMM | Paging | Heap | Scheduler | Syscall

RAM ESTESA > 1MB — Tutto il resto (protetto, isolato)
  Driver ELF (/dev/)  — crash isolato, non tocca il kernel
  Processi (/bin/)    — spazi di indirizzamento separati
  Librerie (/lib/)    — shared, mappate in ogni processo
```

### Le pagine di un programma arrivano quando servono

Dalla 0.149 il caricatore ELF non copia i segmenti in RAM: annota dove
vivono nel file, tiene l'eseguibile aperto e le pagine arrivano al primo
accesso, dal gestore di page fault. Un binario con **8 MB di dati
costanti** parte occupando 36 KB e sale a 8 MB solo se lo si legge tutto.

Il costo di avvio non dipende piu' dalla dimensione del binario — che e'
la condizione per far girare qui dentro un compilatore, dove `cc1` da solo
sono decine di MB.

I **driver** fanno eccezione e si caricano tutti in RAM: un driver che
serve il filesystem, paginato da quel filesystem, dovrebbe servire la
propria lettura mentre e' fermo ad aspettarla.

Conseguenza da sapere: **l'eseguibile resta aperto finche' il processo
vive**, e le pagine caricate non vengono mai buttate via per essere
rilette dal file. Se la RAM finisce e c'e' un'area di scambio, vanno
sull'area come le altre pagine private (dal kernel 0.276: vedi *Lo swap
funziona*).

### Il thread pointer: variabili `__thread`

Dalla 0.154 il caricatore riconosce `PT_TLS` e ne fa una copia per
processo, sotto la riserva dello stack con una pagina di guardia in mezzo.
Il descrittore GDT numero 6 (selettore `0x33`, quello che i processi
tengono in `GS`) è User Data con una **base che cambia**: lo scheduler la
riscrive a ogni switch con il thread pointer del processo entrante.

È il modello **local-exec**, quello dei binari statici: gli offset delle
variabili li risolve `ld` al link, quindi a runtime non c'è niente da
rilocare. Con base zero il descrittore è indistinguibile da `0x23`, perciò
chi non usa `__thread` non paga niente.

> ! Non c'è il TLS **dinamico** (`__tls_get_addr`, variabili
> thread-local dentro una libreria condivisa): serve a chi carica codice a
> runtime, e qui i binari sono statici.

**E dal 4 settembre 2026 i fili ci sono, e il blocco TLS è di ciascuno.** Ogni
filo ha il suo, in cima al proprio stack, con l'immagine iniziale **riletta
dall'eseguibile** — copiare quella del capogruppo vorrebbe dire far partire il
filo con i *valori di adesso* di un altro flusso. Anche `errno` è per filo:
`__errno_dove()` legge il thread pointer da `%gs:0` e lo usa come chiave. È
per questo che il blocco si fa **anche ai programmi senza variabili
`__thread`**: senza, la base di quel descrittore varrebbe zero e quella
lettura sarebbe un page fault all'indirizzo 0.

Perché farlo, se un processo ha un filo solo e una variabile `__thread` è
una globale con un nome più lungo? Perché il modo in cui *mancava* era il
peggiore possibile: la prova che ogni `configure` fa per il TLS è una
**compilazione**, e il compilatore la supera sempre — sa emettere gli
accessi via `%gs` da vent'anni, ed è il sistema a non avere dove puntarli.
Nessun errore, nessun avviso, un binario che si costruisce benissimo e
muore alla terza istruzione della prima funzione che chiama.

### Lo spazio di un processo, e il tetto dello heap

```
0x08000000  testo, dati, bss del programma
            heap ---->                             (sbrk, mmap senza MAP_FIXED)
0xbff44000  heap_max — il tetto
0xbff44000  pagina di guardia
0xbff45000  banda degli stack dei fili — sette piazzole da 64 KB,
            con una pagina di guardia fra l'una e l'altra
0xbffbc000  pagina di guardia
0xbffbd000  blocco TLS del processo, se il programma ne ha uno
0xbffbe000  pagina di guardia
0xbffbf000  riserva dello stack (256 KB)   <---- lo stack cresce all'ingiù
0xbffff000  cima dello stack
```

*(Gli indirizzi sono quelli di un programma con un blocco TLS di una pagina:
un blocco più grande sposta all'ingiù tutto quel che gli sta sotto.)*

Lo heap comincia **subito dopo l'ultimo segmento caricato**, non a un
indirizzo fisso. Dalla 0.156 ha anche un **tetto**: una pagina di guardia
sotto il primo oggetto che c'è davvero — oggi la banda dei fili, e sotto di
essa il blocco TLS e la riserva dello stack.

! **LA BANDA SI RISERVA ALL'AVVIO, ANCHE A CHI NON FARÀ MAI UN FILO**, ed è la
scelta che rende semplice tutto il resto: sono **indirizzi, non pagine** —
mezzo megabyte in meno per uno heap che ne ha tre giga — mentre spostare il
tetto dello heap quando nasce il primo filo vorrebbe dire abbassarlo sotto
memoria che lo heap potrebbe **già** aver preso. O si rifiuta il filo, o si
mette il suo stack sopra la roba di qualcun altro: il primo è un limite che
salta fuori a caso, il secondo è memoria corrotta in silenzio.

Prima non ce l'aveva, e l'unico limite era la RAM fisica. Sembra
innocuo — la memoria finisce prima — ma sopra lo heap non c'è il vuoto, e
`paging_map_page()` **sovrascrive una PTE già presente senza dire niente**.

> ! Uno heap abbastanza grande avrebbe rimappato **il blocco TLS del
> processo stesso** su pagine nuove azzerate: il thread pointer a zero, e
> ogni variabile `__thread` a leggere memoria altrui. Senza un fault, senza
> un log. Ora chi supera il confine si prende `ENOMEM`, che è un errore che
> `malloc()` sa già trattare.

Il controllo sta **prima** di allocare, non dentro il ciclo: fermarsi a
metà lascerebbe lo heap avanzato di un valore che il chiamante non ha mai
visto. E `mmap` rispetta lo stesso confine, `MAP_FIXED` compreso — il
blocco TLS e la riserva dello stack non sono roba che un processo possa
farsi rimpiazzare, perché il kernel ci tiene degli invarianti sopra.

### La fascia kernel, e la finestra di rimappatura

La page directory di un processo copia dalla PD del kernel solo le PDE
**sotto `USER_SPACE_BASE` (64 MB)**. Quella fascia è l'unica memoria che il
kernel può rileggere al proprio indirizzo fisico mentre gira un processo,
ed è dove il PMM è obbligato a mettere ciò che il kernel indirizza così:
heap di `kmalloc`, stack kernel dei processi, page directory e page table,
immagini dei driver in corso di rilocazione (`pmm_alloc_page_kernel()`).

Le pagine dei processi invece stanno **ovunque in RAM**: il kernel le tocca
attraverso una pagina virtuale che ripunta al volo alla pagina fisica che
gli serve — `paging_finestra_apri()`, il `kmap_atomic` dei kernel grandi
ridotto all'osso. Senza, un processo poteva crescere solo finché il PMM
consegnava pagine sotto la soglia: **4 MB, e poi kernel panic**, con
qualunque quantità di RAM installata. Ora un processo arriva a occupare la
RAM disponibile (provato: 300 MB su una macchina da 512).

---

## Syscall interface (int 0x80, stile Linux)

| EAX | Syscall      | EBX        | ECX       | EDX     |
|-----|--------------|------------|-----------|---------|
|   1 | exit         | status     | —         | —       |
|   3 | read         | fd         | buf*      | count   |
|   4 | write        | fd         | buf*      | count   |
|   5 | open         | path*      | flags     | mode    |
|   6 | close        | fd         | —         | —       |
|  11 | exec         | path*      | argv**    | envp**  |
|  41 | dup          | fd         | —         | —       |
|  63 | dup2         | vecchio    | nuovo     | —       |
|  55 | fcntl        | fd         | cmd       | arg     |
|  20 | getpid       | —          | —         | —       |
|  45 | sbrk         | increment  | —         | —       |
|  54 | ioctl        | fd         | request   | arg     |
|  90 | mmap         | params*    | —         | —       |
|  91 | munmap       | addr       | length    | —       |
| 158 | sched_yield  | —          | —         | —       |
| 162 | sleep        | ms         | —         | —       |
| 183 | getcwd       | buf*       | size      | —       |
| 184 | getenv       | key*       | buf*      | size    |
|  39 | mkdir        | path*      | —         | —       |
|  40 | rmdir        | path*      | —         | —       |
|  10 | unlink       | path*      | —         | —       |
| 185 | version      | buf*       | size      | —       |
|  88 | reboot       | cmd        | —         | —       |

`getenv` legge la configurazione di `/boot/kernel.cfg`: sia le variabili di
`[env]` sia le opzioni scalari fuori da `[env]` come `verboseboot`. È il modo
in cui un processo utente accede alla configurazione senza rileggersi il file.

`version` copia `g_os_version`, la variabile globale del kernel con nome,
copyright, licenza e versione del sistema (`kernel/version.c`). La shell la
espone con i comandi `ver` e `version`.

Wrapper libc: `getconf()`, `osversion()`, `verboseboot()`.

`reboot` spegne, riavvia o ferma il sistema (`cmd` = 0 poweroff, 1 restart,
2 halt). Sincronizza sempre il filesystem e ferma lo scheduler prima di
agire — vedi sotto.


### Le syscall aggiunte nella 0.184

| EAX | Syscall       | EBX          | ECX       | EDX | A cosa serve |
|-----|---------------|--------------|-----------|-----|--------------|
| 246 | video_info    | `VideoInfo*` | —         | —   | dov'è il framebuffer e che forma ha |
| 247 | log           | `const char*`| lunghezza | —   | una riga sul log del kernel, cioè sulla **seriale** |
| 248 | lib_apri      | `const char*`| —         | —   | mappa una libreria condivisa e rende la sua tabella (**tolta nella 0.351**: ENOSYS) |

`log` non è un doppione di `printf`, e la differenza conta: `printf` scrive
sulla console del processo, e se quella console non è quella a video il
messaggio non lo legge nessuno — cioè esattamente il caso per cui esiste, un
server grafico che gira su una console sua. Uno strumento cieco costa più del
difetto che deve trovare.

`lib_apri` rende un **indirizzo**, non una maniglia, e va bene che sia
positivo: la fascia delle librerie è 0x04000000-0x08000000, quindi il valore
non può mai essere confuso con un -errno. Quello che il chiamante vuole è
proprio l'indirizzo da cui leggere i nomi.

### Le syscall aggiunte dalla 0.185 alla 0.202

| EAX | Syscall     | EBX           | ECX        | EDX | A cosa serve |
|-----|-------------|---------------|------------|-----|--------------|
| 249 | fb_map      | `void**`      | —          | —   | mappa il framebuffer: la capacità **stretta** che ha sostituito `mmio_map` per il server grafico |
| 250 | interrompi  | pid           | segnale    | —   | Ctrl+C che morde: il segnale arriva al gruppo in primo piano |
| 251 | pty_apri    | `int fd[2]`   | —          | —   | una coppia padrone/schiavo |
| 252 | pty_ctl     | fd            | comando    | arg | misura della finestra, modo raw, gruppo in primo piano (sulla console: chi ferma Ctrl+C) |
| 253 | statperm    | `const char*` | `StatPerm*`| —   | modo, uid e gid di un percorso **senza aprirlo** |
| 254 | su          | `const char*` | password   | —   | «diventa root SE sai la password», e decide il kernel |

! **DUE CAPACITÀ STRETTE, E LO STESSO PRINCIPIO.** `fb_map` fa una cosa sola —
mappare il framebuffer — dove `mmio_map` faceva «mappa qualunque indirizzo
fisico», che a un server grafico non serve e a un programma ostile serve
moltissimo. `su` fa una cosa sola — diventare root sapendo la password — dove
il bit setuid sui file renderebbe pericoloso ogni eseguibile che lo porta. Un
permesso che fa esattamente ciò che serve si può ragionare; uno che fa di più
si può solo sperare che nessuno lo usi.

### Le syscall aggiunte dalla 0.203 alla 0.208

| EAX | Syscall          | A cosa serve |
|-----|------------------|--------------|
| 233 | console_grafica  | chi tiene la console della grafica, e quale sia — così Alt+Fn non porta su uno schermo nero da cui non si sa tornare, e un server ucciso libera la console da solo |
| 238 | lib_trova        | «questa libreria ce l'ho già dentro?» — la risposta ring 3 non può darsela, e la tavola delle pagine del processo *è* l'elenco |

! **TUTT'E DUE SONO STATO CHE VIVE NEL KERNEL PERCHÉ DEVE SOPRAVVIVERE A CHI LO
USA.** Una bandiera tenuta dal server grafico morirebbe con lui e lascerebbe la
porta aperta su una stanza vuota; un elenco di librerie tenuto da uno stub non
lo vedrebbe l'altro stub dello stesso processo — ed è esattamente il difetto da
cui la 238 è nata, due motori JavaScript che giravano insieme senza vedersi.

### Le syscall dei fili, dalla 0.208 alla 0.209

| EAX | Syscall           | EBX        | ECX       | EDX | A cosa serve |
|-----|-------------------|------------|-----------|-----|--------------|
| 201 | thread_crea       | entry      | argomento | —   | un secondo flusso dentro lo stesso programma: rende il tid, che **è** un pid |
| 202 | thread_esci       | codice     | —         | —   | esce dal filo, non dal processo; non ritorna |
| 203 | thread_attendi    | tid        | `int*`    | —   | aspetta un filo del proprio gruppo e ne raccoglie il codice |
| 204 | attesa_dormi      | indirizzo  | valore    | ms  | «dormi finché lì c'è ancora questo valore»: è su questa che stanno lucchetti, condizioni e semafori |
| 205 | attesa_sveglia    | indirizzo  | quanti    | —   | sveglia chi dorme su quell'indirizzo (0 = tutti) |
| 206 | thread_ferma      | tid        | —         | —   | **chiede** a un filo di fermarsi, e scrolla chi dorme |
| 207 | thread_devo_fermarmi | —       | —         | —   | 1 se qualcuno l'ha chiesto: è il filo a scegliere dove guardare |

! **UN FILO È UN TASK CHE CONDIVIDE LA PAGE DIRECTORY**, e per lo scheduler non
c'è niente di nuovo: stessa run queue, stesso quanto, stesso `context_switch`.
Condivide anche i descrittori e la directory di lavoro; ha di suo lo stack —
una piazzola in una banda riservata sotto il TLS — e il proprio blocco TLS,
`errno` compreso. Chi esce dal *processo* porta via tutti i fili.

! **FERMARE UN FILO È CHIEDERGLIELO, E NON È TIMIDEZZA.** Un filo interrotto
dove capita lascerebbe i lucchetti presi e le strutture a metà, che dentro un
processo solo sono quelle di tutti. Il kernel fa le due cose che da fuori non
si possono fare: mette il messaggio nel PCB e **scrolla** chi dorme.

! **LE CONDIZIONI E I SEMAFORI NON SONO SYSCALL.** `condizione_aspetta`,
`semaforo_prendi` e le altre sei stanno tutte nella libc, costruite sopra la
204 e la 205: senza contesa non costano nemmeno una chiamata di sistema. Si
aspetta **sempre** dentro un `while`, mai dentro un `if` — il risveglio dice
«guarda di nuovo», non «adesso c'è».

---

## Arresto e spegnimento

| Comando shell | Effetto |
|---|---|
| `halt` | smonta le unita', ferma il sistema, **non** spegne |
| `poweroff` / `shutdown` | smonta le unita', conta 3 secondi, spegne l'hardware |
| `reboot` | smonta le unita' e riavvia (reset via 8042, fallback triple fault) |

Lo spegnimento hardware usa le porte ACPI note di QEMU (`0x604`), Bochs
(`0xB004`) e VirtualBox (`0x4004`). **In emulazione la macchina si spegne
davvero; su hardware reale con ogni probabilità no** — servirebbe un parser
ACPI (FADT/DSDT) o APM via real mode, nessuno dei due ancora implementato. In
quel caso il sistema resta fermo in stato sicuro con il messaggio "è ora
sicuro spegnere il computer", come i PC pre-ATX.

---

## Identità e versione del sistema

`kernel/include/version.h` è la **fonte unica di verità** per nome, versione,
autore e licenza. Da lì derivano il banner di avvio, i comandi `ver`/`version`
e `uname`, e le variabili `OSNAME`/`OSVER`/`AUTHOR` dell'ambiente — che il
kernel inietta in `[env]` e che **non** vanno scritte in `kernel.cfg`.

```c
#define EXOS_VERSION    "0.101"   /* +0.001 a ogni modifica del kernel */
```

È una stringa e non un numero perché il kernel non usa la virgola mobile.
L'incremento è manuale e deliberato.

! **Anche la riga «Versione» in cima a questi due leggimi viene da lì**, e
non è copiata a mano: `make leggimi-versione` la riscrive da `version.h`, e
`make verify` fallisce se le due divergono. Un numero copiato invecchia il
giorno dopo, e un leggimi che dichiara una versione sbagliata è peggio di
uno che non la dichiara affatto.

---

## Avvio silenzioso

```ini
[kernel]
verboseboot = 0    # 0 = solo output normale (default), 1 = log e banner
```

**Il default è `0` dalla 0.142** (prima era `1`): un sistema che si avvia
mostra il proprio nome, non i propri passi di inizializzazione. Vale in tutti
i casi dubbi — voce assente, file mancante, valore non numerico — e solo un
numero diverso da zero fa parlare il sistema.

Con `0`: schermo pulito, una riga di identità, prompt. I messaggi dei PASSI
1-13 vengono emessi lo stesso — sono stampati prima che il file di
configurazione sia leggibile — e il kernel li cancella dallo schermo al
PASSO 13c.

**Errori ed eventi inattesi restano sempre visibili**, in silenzioso come in
verboso:

- un `LOG_ERROR` è stampato qualunque sia il livello di log, anche con
  `loglevel = 0`;
- ogni ERROR/WARN è registrato mentre viene emesso e **riproposto dopo la
  pulizia dello schermo**, sotto l'intestazione `Avvio silenzioso: N
  problema/i durante l'inizializzazione` — così cancellare il log di avvio
  non cancella le prove di ciò che è andato storto;
- se i problemi superano gli slot del registro, la ristampa lo dichiara
  invece di troncare in silenzio;
- la console seriale riceve comunque tutto, filtro incluso.

```
EX OS 0.101 (Extensible Operating System) - Copyright (C) 2025 Graziano Falcone - GPL 2.0

  Avvio silenzioso: 1 problema/i durante l'inizializzazione
[WARN]  PMM: nessuna mappa E820, uso fallback: 32256 KB

ex-os:/>
```

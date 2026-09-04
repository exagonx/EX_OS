# EX-OS — Extensible Operating System

**🇮🇹 Italiano** · [🇬🇧 English](README.en.md)

**Versione:** 0.209
**Autore:** Graziano Falcone <exagonx@hotmail.com>
**Licenza:** GNU General Public License v2 (GPL-2.0)
**Architettura:** x86 32-bit — si avvia da floppy, da CD o da disco rigido

*Le due versioni si aggiornano insieme: quello che c'è in una c'è
nell'altra.*

---

## Che cos'è EX-OS

EX-OS è un sistema operativo baremetal scritto in C e ASM per architettura
x86 32-bit. L'obiettivo è un sistema estensibile: il kernel è piccolo e
read-only in RAM convenzionale, tutto il resto (driver, shell, programmi) gira
in RAM estesa in spazio protetto.

**Non è un sistema «da floppy»: il floppy è uno dei modi di avviarlo, non il
posto dove vive.** Si parte da tre supporti, e sono tre sistemi con lo stesso
kernel dentro:

| supporto | che cosa c'è | come si fa |
|---|---|---|
| **floppy** 1.44MB FAT12 | il minimo per arrivare a una shell e installare | `make floppy` -> `dist/floppy.img` |
| **CD** avviabile | il sistema **completo**: scrivania grafica, navigatore, rete, driver, font, documentazione | `make iso-exos` -> `dist/exos.iso` |
| **disco rigido** FAT16/32 o ext2 | quello che l'installazione ci ha messo, ed è l'unico che si scrive | `install` (o `cdinstall` dal CD) |

Il floppy è 1,44 MB che non crescono, e il kernel cresce a ogni cosa che
impara: già dal 26 agosto 2026 l'installatore vero sta sul CD e sul floppy
resta la parte congelata. **Il CD è il supporto di riferimento** — è da lì che
si prova e si installa — e i CD/DVD si leggono comunque (ISO 9660 e Joliet)
qualunque sia il supporto d'avvio.

Un crash di un driver o di un programma non può abbattere il sistema.

### Il kernel è un **minikernel**

Non è un microkernel e non è monolitico, e vale la pena dire perché nessuna
delle due parole va bene.

**Microkernel no**: dentro il kernel ci sono la memoria virtuale, lo
scheduler, il VFS con FAT12/16/32, ext2 e ISO 9660, il caricatore ELF e la
cache dei blocchi. Un microkernel vero quelli li mette fuori, e questo non
lo fa — con 30 000 righe e 66 syscall sarebbe una descrizione lusinghiera e
falsa.

**Monolitico no**: i driver di dispositivo — tastiera, floppy, PCI, NE2000,
PCnet, tty, IP — sono **processi ring3**, eseguibili ELF come quelli di
`/bin`, che parlano per IPC e non eseguono una sola istruzione
privilegiata. Il kernel media ogni accesso all'hardware e controlla i
permessi a ogni chiamata; un driver che muore lo si rilancia.

«Monolitico ibrido» sarebbe corretto e non servirebbe a niente: è
un'etichetta che descrive e basta. **Minikernel** dice la stessa cosa e in
più contiene un impegno — *restare piccolo* — che è la ragione per cui
questa architettura è stata scelta. Oggi il numero è:

    build/kernel.bin      184 KB      ~30 000 righe di C e ASM

Il numero è qui perché un impegno senza una misura è un'intenzione. Chi
aggiunge codice al kernel dovrebbe prima chiedersi se non possa essere un
processo ring3 — è quasi sempre la risposta giusta, ed è come sono nati
tutti i driver.

**Da agosto 2026 EX-OS ospita codice di terzi, e da solo**: GNU binutils
2.44 — `as` e `ld` — è compilato *per* EX-OS e ci gira dentro, e un
programma assemblato e collegato qui è identico byte per byte a uno
prodotto dal cross-compilatore su Linux.

Dal 6 agosto **la catena è chiusa**: `gcc` gira dentro EX-OS, trova i
propri header senza che nessuno glielo dica, e concatena da sé cc1, `as`,
`collect2` e `ld` fino a un eseguibile che parte. Lo stesso vale per
**`g++`** — contenitori, `std::string` ed eccezioni comprese. Vedi
[La catena di compilazione dentro EX-OS](#la-catena-di-compilazione-dentro-ex-os).

---

## Novità

Le voci sono marcate **testato** quando il lavoro è stato verificato girando
dentro EX-OS, **da testare** quando il codice c'è ma la prova che conta —
quella sull'hardware o sul caso reale — non è ancora stata fatta.

### La pila di un filo cresce su richiesta

**testato** — un filo nasceva con 64 KB di RAM vera in mano: sedici pagine
allocate e azzerate una per una, anche per un filo che di pila ne usa duecento
byte. Non era una svista, era un prezzo pagato apposta, e il perché stava
scritto nel codice: `page_fault_handler` sapeva far crescere **uno** stack —
quello del PCB corrente — e davanti a una pagina mancante dentro la banda dei
fili non sapeva **di chi** fosse.

Adesso lo sa dire. Della piazzola si impegnano il blocco TLS e le prime otto
pagine; il resto arriva quando il filo scende davvero, e si ferma sulla pagina
di guardia sotto la piazzola. **Sette fili costano 980 KB invece di 1344**,
cioè 140 a testa invece di 192 — e di quei 140, centoventotto sono lo stack di
*kernel* del task, che con questo lavoro non c'entra: la pila utente è passata
da 64 KB a 12.

**La domanda vera non era «quanto», era «di chi».** I fili condividono la
memoria, quindi a faultare dentro la pila di un filo può essere **qualcun
altro**: un filo dichiara `char buf[16384]`, ne tocca solo la cima e ne passa
il fondo a un compagno — o a una `read()`. Il fondo non è impegnato, il
compagno ci scrive, e il fault arriva mentre gira lui; il suo ESP non dice
niente su quell'indirizzo, perché sta in un'altra piazzola. Finché i 64 KB
c'erano tutti il caso non esisteva. Per questo `pf_cresci_stack` è diventata
due domande invece di una: **se** la crescita è legittima e **di chi** è la
pila, poi `pf_cresci_pagine` impegna.

> **La condizione «vicino a ESP» lì non si applica, e non è una rinuncia:**
> sarebbe un paragone fra due piazzole diverse, cioè un numero senza
> significato. Al suo posto c'è un confine altrettanto stretto — l'indirizzo
> deve cadere nella riserva di un filo **vivo** dello stesso gruppo — e fra una
> piazzola e l'altra resta la guardia, che nessuna crescita può scavalcare.

**Due difetti trovati leggendo quel che si stava per toccare**, tutt'e due in
`proc_reap_zombie`:

- **sedici pagine perse a ogni filo che finisce.** Le piazzole si riusano, ma
  le pagine di un filo morto restavano mappate fino alla fine del *processo*:
  il filo dopo prendeva lo stesso posto e ci si mappava sopra pagine nuove —
  `paging_map_page` sovrascrive la voce senza dire niente — con i dati di
  quello di prima sotto i piedi. Adesso la piazzola si smonta, e nella prova si
  legge «riassorbiti tornano 980 KB su 980»;
- **una page directory distrutta più di una volta.** Si libera «quando non
  resta nessuno», e nessuno si contava con `proc_gruppo_vivi()`, che gli zombie
  non li conta; ma quando il capogruppo esce i fili diventano zombie *tutti
  insieme*, e ognuno veniva raccolto con lo stesso puntatore in mano. Il primo
  distruggeva la directory, i successivi la ripercorrevano da liberata. **Non è
  provato** che fosse la causa dei due difetti rari già aperti — il panic dentro
  `kfree`, il driver che parte con lo stack a zero — ma la forma è quella: un
  conto che arriva altrove e molto dopo.

**Le prove sono due modi nuovi di `/bin/filiprova`,** e ognuna delle tre parti
di `pila` fallisce da sola: quanto costa un filo (la riga di giudizio è a 160 KB
— 128 di stack kernel più *o* 64 di piazzola tutta *o* 12 impegnati a poco a
poco: un numero in mezzo non esiste), che poi cresca (quaranta chiamate da un
kilobyte, cinque volte quel che gli è stato dato), e che cresca **per mano di un
altro**. `sfonda` scende senza fine e pretende che a morire sia il filo, sulla
guardia, con codice -11, mentre il processo resta vivo.

> **Nella terza parte il filo che aspetta non può chiamare niente.** Una `call`
> scrive l'indirizzo di ritorno *sotto* l'ESP, e il fault che ne segue fa
> impegnare tutto quel che sta fra lì e la parte già viva — cioè proprio il
> pezzo che la prova vuole lasciare vuoto. Aspetta girando su una variabile
> `volatile`. È anche la ragione per cui il caso è raro nella vita vera: i dati
> vivi di un filo stanno sempre sopra il suo ESP, e sopra l'ESP è già tutto
> impegnato.

Restano dichiarati, e sono difetti veri usciti scrivendo la prova: **un filo che
muore di page fault non porta via il processo** (muore lui, e il programma
prosegue magari con un lucchetto preso da chi non c'è più), e **lo zombie di un
filo che nessuno aspetta lo raccoglie la shell**, che torna al prompt con il
programma ancora vivo.

### La casella «Cerca», e due caselle nella stessa barra

**testato, in rete, dentro EX-OS** — cercare si poteva già: `html.duckduckgo.com`
risponde in HTML semplice e il navigatore lo mostra. Mancava la *comodità* — si
scriveva l'indirizzo del motore a mano — e adesso c'è una casella **Cerca** a
destra nella barra: parole, Invio, risultati.

I motori sono tre, e si scelgono in **File > Impostazioni**: **duckduckgo**
(predefinito), **wikipedia**, **marginalia**. **Sono tre perché tre
rispondono:** `google.com` manda novantamila byte, tre script e *zero*
collegamenti di risultato dentro l'HTML — i risultati li costruisce il
JavaScript, quindi non c'è browser senza JS che possa vederli — e Mojeek manda
un Captcha. Metterli in elenco vorrebbe dire una voce che promette una ricerca
e rende una pagina vuota.

**Invio fa due cose diverse, e la differenza è il fuoco.** In una casella del
toolkit Invio arriva all'applicazione come `EXM_TASTO` — la casella lo lascia
passare apposta — ma il messaggio **non dice da quale casella arrivi**, e il
fuoco lo sa solo il toolkit. Per questo è nata `ex_fuoco_chi()`, otto righe:
l'alternativa era indovinarlo guardando quale testo è cambiato, cioè sbagliarlo
il giorno che qualcuno cerca due volte la stessa cosa.

> **E un difetto del toolkit che non mordeva finché le caselle erano larghe.**
> Con la seconda casella l'indirizzo si è ristretto da 636 a 436 pixel, e alla
> prima pagina lunga il difetto era in fotografia: l'indirizzo **scriveva sopra
> l'etichetta «Cerca»**. `ex_scrivi` non taglia niente e `CL_TESTO` non lo
> chiedeva a nessuno — non è un difetto nato oggi, è nato oggi il primo posto in
> cui due controlli sono così vicini. Adesso si mostra la **coda** e non la
> testa, perché in quella casella il cursore sta sempre in fondo: chi scrive
> deve vedere quel che sta scrivendo, non l'inizio di un indirizzo che ha già
> finito di battere.

### La directory è di tutti e due (e l'ambiente lo era già)

**testato** — un `chdir` dentro un filo adesso lo vedono gli altri, ed è una
riga: `filo->cwdt = capo->cwdt;` invece di copiare il percorso. **È lo stesso
mestiere che fa `fdt` per i descrittori**, con lo stesso idioma: nel PCB c'è il
campo `cwd` e c'è il puntatore `cwdt`, che per un processo punta al proprio
campo e per un filo a quello del capogruppo. Tutto il kernel usa `cwdt` — sono
cinque posti in croce — e **fra processi non cambia niente**: la directory
resta una per ciascuno, ereditata dal padre a `spawn` come prima.

**Il motivo è lo stesso di quando la directory diventò una per processo, letto
all'incontrario.** Allora il difetto era che `cd` dentro un programma spostava
tutti gli altri; qui è che due fili **sono un programma solo**, e una funzione
che entra in una directory, apre un file relativo e torna indietro fa la cosa
giusta o quella sbagliata a seconda di quale filo la esegue — senza errore,
aprendo un file nel posto sbagliato.

**E l'ambiente non passa dal kernel: quello era da controllare, non da fare.**
L'elenco diceva «cwd ed env sono copiati», e per `env` non era vero: `environ`
sta nei dati di `libc.so`, che i fili condividono perché condividono la
memoria, e il campo `env[]` del PCB non lo usa nessuno. Non c'era niente da
aggiustare; c'era da *guardare*, che è un'altra cosa dal darlo per buono.

> **La prova guarda nei due versi, e uno solo non basterebbe.** Con la
> directory copiata alla creazione, il verso «il principale si sposta e il filo
> se ne accorge» passerebbe lo stesso ogni volta che il filo nasce *dopo* il
> cambio: è il filo che si sposta e il principale che deve vederlo a dire
> «condivisa» invece di «copiata al momento giusto». Rimessa la copia di prima
> per un giro, falliscono tutt'e due i versi — e l'ambiente passa in tutt'e due
> i casi, che è la conferma che quella metà non è mai stata un problema del
> kernel.

### Fermare un filo è chiederglielo

**testato** — uccidere un filo si poteva già (il tid è un pid, e `kill`
funziona), ma **non si deve**: un filo ucciso dove capita lascia i lucchetti
presi, i file aperti a metà e le strutture come stavano, e dentro un processo
solo quelle non sono le sue — sono **di tutti**. Adesso c'è il modo ordinato:

```c
while (!thread_devo_fermarmi()) { ...un pezzo di lavoro... }
...lascia i lucchetti, chiudi quel che hai aperto...
thread_esci(0);
```

`thread_ferma(tid)` lascia un messaggio, `thread_devo_fermarmi()` lo legge dove
il filo decide. **Il kernel fa solo le due cose che da fuori non si possono
fare**: mettere il messaggio dove il filo lo troverà, e *scrollare* chi dorme —
perché un filo addormentato non guarda niente.

**Due parole nel PCB, e fanno due mestieri diversi.** `ferma` è il messaggio e
resta: una richiesta letta una volta vale anche la seconda. `scuoti` è la
scrollata e **si consuma**, e serve a chiudere l'unica finestra che questa cosa
ha: fra il momento in cui il filo guarda e quello in cui si addormenta. Se la
richiesta arriva lì in mezzo il filo dorme *dopo* aver guardato — la stessa
corsa del risveglio perso, e la stessa cura: chi chiede lascia scritto che la
prossima attesa non deve dormire, e `sys_attesa_dormi` lo trova dentro lo
stesso `cli` che protegge il valore atteso. Una scrollata sola, non un «non
dormire mai più»: un filo che sta uscendo ha ancora una pulizia da fare, e con
ogni attesa che torna subito quella girerebbe a vuoto.

> **E la prova passava anche senza la cosa che doveva provare** — il difetto più
> istruttivo della giornata, e stava *nella prova*. Tolta la scrollata dal
> kernel per una riga, il caso che doveva colpire quella finestra passava lo
> stesso: con un processore solo, creando un filo e fermandolo subito si finisce
> sempre in uno dei due casi facili — o la richiesta arriva prima che il filo
> abbia guardato, o a filo già addormentato. Il caso di mezzo dura poche
> istruzioni e **a caso non ci si casca**. La cura è allargare la finestra a
> comando invece di sperare: il filo, fra l'occhiata e il sonno, cede la CPU e
> alza una bandierina, e chi comanda aspetta quella. Adesso venti corse costano
> **60-180 ms** con la scrollata e **20200 senza** — venti scadenze da un
> secondo, una per corsa — e la prova fallisce. Prima la differenza era di zero.

**Quel che non fa, ed è scritto:** il filo si accorge solo dove guarda. Un
`semaforo_prendi` senza scadenza non è un punto di controllo, e nemmeno una
lettura da tastiera o da rete; chi vuole potersi fermare lì usa le varianti con
scadenza. E la lettura del messaggio costa una chiamata di sistema: toglierla
vorrebbe dire una parola di ABI dentro il blocco TLS, che è una decisione a
senso unico e nessuno l'ha ancora misurata.

### Le condizioni e i semafori, sopra l'attesa che dorme

**testato** — l'attesa che dorme era il mattone; adesso ci sono le due cose che
ci si costruiscono sopra, e sono **otto funzioni di libc, nessuna riga di
kernel**: `condizione_aspetta` (con la variante a scadenza), `condizione_segnala`,
`condizione_segnala_tutti`, `semaforo_prendi` (idem), `semaforo_prova`,
`semaforo_lascia`. Tutt'e due i tipi sono **un intero**, come il lucchetto:
`Condizione c = CONDIZIONE_ZERO;`, `Semaforo posti = 1;` — nessuna funzione di
inizializzazione, che è l'unica forma che non si può dimenticare di chiamare.

**Una condizione è un contatore di segnali, e basta.** Non tiene la lista di
chi aspetta: quella è già nel kernel, ed è la coda di chi dorme su
quell'indirizzo. Aspettare è tre righe — leggi il contatore, lascia il
lucchetto, dormi su quel valore, riprendi il lucchetto — e **la prima è l'unica
che conta**: fra il «lascia» e il «dormi» c'è una finestra in cui chi segnala
può passare, e quel segnale arriverebbe prima che ci sia qualcuno da svegliare.
Avendo letto il contatore *prima*, se qualcuno segnala lì in mezzo il valore
non è più quello e non si dorme affatto. La finestra non si chiude: si rende
innocua.

> **Perciò si aspetta dentro un `while`, mai dentro un `if`.** Il risveglio
> dice «guarda di nuovo», non «adesso c'è»: può arrivare per un segnale, per
> la scadenza, o perché un segnale era già passato. Chi controlla una volta
> sola prima o poi prosegue con la condizione falsa, ed è il difetto che non si
> riproduce.

**Un semaforo non ha un padrone**, e non è una licenza: chi lascia può non
essere chi ha preso, ed è esattamente ciò che serve fra un produttore e un
consumatore, dove il posto libero lo consuma uno e lo restituisce l'altro. Una
spesa è rimasta lì, scritta accanto al codice: chi lascia chiama la sveglia
*sempre*, anche quando non dorme nessuno — il lucchetto quella spesa la evita
col terzo stato, ma lì il numero *è* lo stato del lucchetto, mentre qui il
contatore conta posti e non ha dove metterlo. La via c'è (un secondo campo
«dormienti») e vuole un tipo nuovo nell'ABI: **non si paga un tipo nuovo per
un'ottimizzazione che nessuno ha ancora misurato.**

> **La prova è una coda di UN posto, e può fallire in tre modi diversi.** Mille
> elementi fra un produttore e un consumatore: con dieci posti i due si
> incrociano poco e la prova diventa quasi sequenziale, con uno solo ognuno dei
> mille costringe l'altro ad aspettare. Si guarda la **somma** (perderne uno e
> leggerne un altro due volte darebbe lo stesso numero di giri), i **giri a
> vuoto** — e devono essere *zero*, non «pochi»: con un consumatore solo, chi si
> sveglia trova sempre la roba — e il **cronometro**, che è il testimone dei
> giri a vuoto. Dentro l'attesa c'è una scadenza di mezzo secondo che è una
> *rete*, non un modo di funzionare: senza, un segnale perso sarebbe una
> macchina ferma per sempre e la prova non fallirebbe, resterebbe lì. Mille
> passaggi ne costano fra **20 e 60 ms** misurati, una sola scadenza ne
> costerebbe 500: la riga di giudizio sta a 400, comoda sopra la misura e
> ancora capace di distinguere.

### I fili: più flussi dentro lo stesso programma

**testato** — e la prova non è che il conto torni, è che **senza lucchetto non
torna**:

```
filiprova: 4 fili, 20000 giri l'uno
  col lucchetto   80000   atteso  80000   esatto
  senza           20000   atteso  80000   perso per strada
  scambi di mano  80000   i fili si alternano davvero
```

Sessantamila incrementi persi sono quattro flussi che si pestano i piedi sulla
stessa memoria. Se i fili fossero finti — se `thread_crea` eseguisse la
funzione dentro chi chiama — quel numero sarebbe 80000 come l'altro, e la prova
sarebbe passata senza provare niente.

**La differenza fra un processo e un filo è una riga: la page directory.**
`proc_create` ne alloca una nuova, `proc_thread_crea` copia quella del
capogruppo. Non c'è una riga dello scheduler che sia stata toccata: stessa run
queue, stesso quanto, stesso `context_switch` — che riceveva già il CR3 come
parametro. E `tgid` (il pid del primo del gruppo) vale `pid` per un processo
normale, così tutto il kernel che non sa niente di fili continua a funzionare
senza un solo `if`.

**I descrittori si condividono per puntatore, non per copia**: due fili che
aprono e chiudono file devono vedere la stessa tabella. Nel PCB è comparso
`fdt`, e le 153 occorrenze di `->fds[` nel kernel sono diventate `->fdt[` con
una sostituzione meccanica — per un processo normale `fdt == fds` e non cambia
niente.

**Lo stack no**: 64 KB per filo, in una banda riservata *a tutti* i processi
all'avvio — anche a chi un filo non lo farà mai. Sono indirizzi, non pagine.
L'alternativa (riservarla quando nasce il primo filo) vorrebbe dire abbassare
il tetto dello heap sotto memoria che lo heap potrebbe già avere preso: o si
rifiuta il filo, o gli si mette lo stack sopra la roba di qualcun altro.

> **Chi esce porta via il gruppo**, come `exit_group` su Linux e per la stessa
> ragione: gli altri fili vivono nella memoria di questo processo, e lasciarli
> correre mentre lo spazio di indirizzamento se ne va vuol dire codice che gira
> sopra pagine liberate. Provato apposta: un programma che crea tre fili
> infiniti ed esce senza aspettarli lascia la macchina sana e il prompt torna.

**Quel che non è per filo, ed è scritto invece che scoperto:** le variabili
`__thread` e `errno` sono per *processo* — il blocco TLS è in comune. Darne uno
per filo vuol dire copiarci l'immagine iniziale che sta nell'ELF; azzerarlo e
basta farebbe partire a zero una variabile inizializzata a cinque, in silenzio.
Fra un limite scritto e un valore sbagliato non c'è partita. E il lucchetto
gira cedendo la CPU: va bene per una sezione critica corta, non per aspettare —
un futex è il passo dopo.


**E poi il blocco TLS per filo.** Nella prima ora i fili condividevano quello
del processo; nella seconda è stato tolto anche quel limite: **ogni filo ha il
suo**, in cima al proprio stack — dove quelle pagine sono già mappate e nessun
altro può arrivarci, che è anche dove lo mette glibc. L'immagine iniziale si
**rilegge dal file**, non si copia da quella del capogruppo: copiare la sua
vorrebbe dire far partire il filo con i valori *di adesso* di un altro flusso —
un contatore a metà, un puntatore a un oggetto in uso. Nel PCB sono comparse le
tre coordinate del `PT_TLS`, e l'eseguibile è già aperto per il caricamento su
richiesta.

> **La prova parte da sette, non da zero**, ed è l'unico modo di distinguere
> «blocco copiato» da «blocco azzerato»: un blocco azzerato passa qualunque
> prova che parta da zero. Ogni filo controlla di trovarci 7, ci scrive il suo
> numero, cede la CPU due volte e ricontrolla; il filo principale, alla fine,
> ritrova il suo 42 intatto. Resta fuori `errno`, che vive dentro `libc.so`
> dove `__thread` non funziona — e adesso ha una strada scritta per smettere di
> esserlo.


**E anche `errno` è per filo.** Dentro `libc.so` non si può scrivere
`__thread` — manca il TLS dinamico — ma il thread pointer si può *leggere*:
`%gs:0` contiene un numero diverso per ogni filo, buono come chiave in una
tabellina di sedici posti, dove il posto si prende con `xchg` e non con «se è
libero allora scrivilo». Per questo il blocco TLS ora si fa a *tutti* i
processi, anche a quelli senza una sola variabile `__thread`: ridotto al solo
TCB, otto byte in una pagina — senza, la base di quel descrittore vale zero e
`movl %gs:0` non dà un valore sbagliato, dà un page fault all'indirizzo 0.

> **Il difetto uscito da lì vale più della funzione.** Messa la tabella,
> `close(999)` ha cominciato a rispondere `errno 0` su una chiamata che
> fallisce di sicuro: **dentro `libc.c` la parola `errno` non è la macro**,
> perché quel file non include `libc.h` — sta scritto in testa che si compila
> senza `-I lib/include`. `err_posix` scriveva la variabile globale mentre il
> programma leggeva il posto del suo filo. E la prova che l'ha trovato era
> stata rifatta apposta: la prima versione faceva sbagliare il filo principale
> con una chiamata che rispondeva zero, e zero non cambia mai — sarebbe passata
> per sempre senza provare niente.

**E il difetto più istruttivo: un task a metà che viene eseguito.** Aggiunto il
TLS per filo, il programma di prova moriva *una volta su tre* con un page fault
all'ingresso della funzione del filo. La diagnosi è arrivata in un giro solo
facendo stampare i numeri: il filo andava in fault **prima che la sua riga di
creazione fosse stampata**, col contesto che `proc_create` gli aveva costruito —
ESP a zero. In mezzo c'era una `vfs_read`, cioè una chiamata **che può
bloccare**: mentre il capogruppo aspettava il disco, lo scheduler metteva in
esecuzione un filo non finito di costruire. La regola che ne esce vale per
qualunque kernel: **fra la creazione di un task e il momento in cui è pronto
non ci deve stare niente che possa bloccare.**


**E l'attesa che dorme davvero.** `attesa_dormi`/`attesa_sveglia`: un filo esce
dalla coda dello scheduler e ci rientra quando qualcuno lo chiama — per
l'**indirizzo** su cui si è fermato, non per il pid, ed è ciò che permette a un
lucchetto di essere un intero e basta, senza doversi ricordare chi c'è in fila.

**Il valore atteso chiude la corsa**, ed è la ragione per cui la chiamata ha tre
argomenti invece di due: fra il momento in cui chi aspetta guarda il lucchetto e
quello in cui si addormenta c'è una finestra, e una sveglia arrivata lì in mezzo
si perderebbe — il filo dormirebbe per sempre. Il confronto lo fa il *kernel*, a
interruzioni spente. E la pagina si tocca *prima* di spegnerle: leggere memoria
utente può far scattare un page fault che vuole il disco, e un disco che si
aspetta a interruzioni spente è una macchina ferma.

**Il lucchetto ha tre stati** — libero, preso, preso-con-gente-che-dorme — e il
terzo esiste per chi *lascia*: senza, dovrebbe chiamare la sveglia a ogni
sblocco per il dubbio che qualcuno dorma. Con il 2, chi lasciando si ritrova in
mano un 1 sa che non c'è nessuno: **senza contesa il lucchetto non costa nemmeno
una chiamata di sistema.**

> **La prova è un cronometro, perché nient'altro distingue.** Un'attesa che gira
> a vuoto e una che dorme, viste da fuori, fanno la stessa cosa. Servono due
> misure con esiti opposti: senza nessuno che svegli, con scadenza 300 ms, è
> tornata dopo **310 ms** — ha dormito; svegliata da un filo dopo 100 ms con la
> scadenza a 2000, è tornata dopo **100 ms** — l'ha svegliata lui, non
> l'orologio.

### Le scorciatoie degli editor, e una cosa che avevo scritto sbagliata

**testato** — scritta una riga nell'editor e premuto solo Ctrl+S: la riga di
stato dice «salvato», e il file riletto dalla shell la contiene. Era l'ultima
voce dell'elenco di EX-IDE: il menu della finestra «Sorgente» prometteva
Ctrl+S, Ctrl+C, Ctrl+V, Ctrl+X e Ctrl+F dal primo giorno, e non le aveva
collegate nessuno.

**Il toolkit non le mangia, apposta.** In `exwin.c`, nel giro dei tasti: «per
ogni altro controllo un Ctrl+lettera è una scorciatoia dell'applicazione e non
deve essere mangiata» — l'unica eccezione è il terminale, dove Ctrl+C è il byte
3 e deve arrivare al pty. I Ctrl arrivavano già; mancava che qualcuno li
guardasse.

> **E quel che avevo scritto nel manuale era sbagliato.** Sotto «Area testo»
> c'era «con cursore, selezione e appunti (Ctrl+C, Ctrl+V, Ctrl+X)», come se i
> tasti li facesse il toolkit. Il toolkit dà le *funzioni* —
> `ex_area_copia/taglia/incolla` — e lascia i tasti a chi scrive il programma.
> La riga adesso lo dice, e dice anche come si fa: è esattamente ciò che serve a
> chi con EX-IDE scrive un editor suo.

**Il disegno si rifà a mano, da tastiera.** È la trappola di questo lavoro:
premendo «Taglia» nel menu il toolkit ridisegna la finestra chiudendo la
tendina, quindi il testo cambiato si vede; da tastiera non si chiude niente, e
senza un `EXM_DISEGNA` esplicito il testo cambia e lo schermo resta com'era. Le
due strade passano dalle stesse funzioni e non hanno lo stesso contorno.

### Taglia e incolla: un controllo si sposta fra le maschere

**testato** — copiato e incollato nella stessa maschera, e poi tagliato,
cambiata maschera e incollato: il file del disegno mostra il controllo passato
sotto l'altra finestra, stessa posizione e stesso nome. Era l'ultima voce grossa
di EX-IDE, e la sua ragione vera non era copiare: era che un controllo messo
sulla maschera sbagliata si poteva solo cancellare e rifare a mano di là.

**Gli appunti del disegno non sono quelli di sistema.** Dentro c'è un
*controllo*, non del testo: quelli di ExWin portano caratteri e li usano già gli
editor per passarsi pezzi di sorgente. Infilarci un controllo vorrebbe dire
inventare un formato testuale per un rettangolo e farlo rileggere anche a chi ci
scrive dentro nel frattempo. Nella finestra principale Ctrl+C parla del disegno,
che è quel che quella finestra è.

**Il nome e l'id non si copiano, si rifanno**: sono unici in tutto il progetto
perché diventano `ID_...`, `h_...` e un nome di funzione dentro `finestra.h`,
che è un file solo. Con una conseguenza gradevole che non era cercata —
tagliando, il nome torna disponibile e il controllo se lo riprende: **uno
spostamento non rinomina niente**.

**E il codice non si copia affatto.** L'handler dell'originale resta
dell'originale; la copia avrà il suo, vuoto, al primo doppio clic. Copiare anche
il corpo vorrebbe dire che exide scrive dentro `finestra.c` cose che non ha
scritto nessuno — l'unica regola che questo programma non rompe mai.

> **Nella stessa maschera l'incollato si scosta di otto pixel, in un'altra no.**
> Metterlo esattamente sopra l'originale lo nasconderebbe: si vedrebbe un
> controllo e ce ne sarebbero due, e il clic prenderebbe sempre quello di sopra.
> In un'altra maschera invece quel posto è libero, ed è esattamente dove lo si
> vuole.

### Rifai: la stessa funzione con le due pile scambiate

**testato** — annullato, rifatto, e verificato il caso che conta di più: dopo un
Annulla, una modifica nuova butta il ramo rifatto.

**Quel che si annulla non si butta, si mette dall'altra parte.** Due pile invece
di una, e *una funzione sola che le scambia*: `passo(da, verso)` prende il
presente, lo mette nella pila `verso`, e rimette il disegno che stava in cima a
`da`. Annulla è `passo(indietro, avanti)`, Rifai è `passo(avanti, indietro)` —
due righe l'uno, e il giorno che si aggiunge un campo al disegno il posto in cui
ricordarsene è uno. Per la stessa ragione la cattura e il ripristino sono
diventati due funzioni invece delle tre `memcpy` copiate nei tre posti che le
usano.

**Una modifica nuova butta il ramo rifatto**, ed è l'unica regola che questa
cosa deve avere: se dopo tre passi indietro si disegna qualcosa, quell'«avanti»
è un futuro nato da un passato che non c'è più, e tenerlo vorrebbe dire un Rifai
che riporta a un disegno mai esistito. Lo fanno tutti i programmi così, e la
ragione è questa — non l'abitudine.

> **La pila scorre quando è piena**, invece di rifiutare l'istante nuovo: il
> passo più vecchio è quello che serve meno, e perdere il più *recente* vorrebbe
> dire un Annulla che non annulla l'ultima cosa fatta. Costo misurato con
> `size`: la BSS di exide passa da 140.384 a 261.888 byte — centoventuno
> kilobyte, cioè la seconda pila di sedici istanti, memoria azzerata e non byte
> nel binario.

### Si cerca davvero, e non serviva portare un browser

**testato** — dal vivo, in HTTPS: `html.duckduckgo.com/html/?q=exos` si apre
nel navigatore di EX-OS e mostra i risultati, con titoli, indirizzi e testi di
anteprima.

La domanda era se portare Firefox, o NetSurf, per poter cercare. La risposta è
venuta da cinque richieste HTTP, non da un preventivo — una per motore, con
l'User-Agent vero di EX-OS:

| motore | cosa risponde |
|---|---|
| google.com/search | 200, 91.980 byte, **tre script e zero link di risultato**: i risultati li costruisce il JavaScript |
| mojeek.com/search | 200, `<title>Captcha</title>` |
| html.duckduckgo.com | 200, 33.784 byte, **dieci risultati in HTML semplice** |
| marginalia, wikipedia | HTML semplice |

**Quindi NetSurf non risolve Google, e non è colpa sua**: NetSurf non esegue
JavaScript, e Google i risultati in HTML non li manda a nessuno. Portare un
motore di terze parti — mesi per NetSurf, un secondo sistema operativo per
Firefox, che senza thread e senza Rust non parte nemmeno — non avrebbe spostato
di un millimetro il problema che si voleva risolvere.

**Poi il difetto vero.** La pagina di DuckDuckGo arrivava (TLS a posto, `200,
31671 byte, 738 nodi`) e lo schermo restava bianco. La riga di stato diceva
anche «stile troncato», ed era lì la risposta: la stessa pagina salvata in
locale *senza* foglio di stile si disegnava subito. **Il foglio di DuckDuckGo è
105.607 byte e il tetto era 24.576**: il navigatore ne leggeva un quarto e si
fermava a metà di una regola, e con mezzo foglio applicato la pagina spariva.

I tre numeri che servivano davvero — 1652 selettori, 2207 dichiarazioni, 105 KB
— contro tetti di 600, 2000 e 24 KB. Alzati a 2400, 5000 e 160 KB: costano
**284 kilobyte di BSS** su un programma che ne aveva già 5,2 MB, misurati con
`size` e non stimati.

> **Si cerca dal proprio navigatore senza fingersi nessuno.** Non serviva
> un'impronta identica a Chrome, né il jitter umano, né un motore di terze
> parti: serviva un motore che risponde in HTML e un tetto alzato. E il terzo
> risultato che DuckDuckGo ha restituito era `github.com/exagonx/EX_OS` — cioè
> questo progetto.

### L'impaginato esce da browser.c, e la prova è che non si vede

**testato** — la stessa pagina fotografata prima e dopo: dieci righe di pixel
diverse su seicento, dalla 582 alla 591, e sono **l'orologio** della barra in
basso. Sopra, niente. Primo dei due passi verso una libreria di testo
formattato: si spezza il file prima di spezzare la libreria.

```
browser.c            6749 -> 4824 righe
browser_impagina.c        1836 righe   (l'impaginato, uscito da lì)
browser_priv.h             222 righe   (la giuntura, che prima non c'era)
```

**Il taglio se l'è fatto dire dalla macchina.** Prima di spostare una riga ho
fatto costruire la mappa del file — centotrentotto definizioni, chi chiama chi,
chi tocca quali variabili globali — e il gruppo dell'impaginazione è venuto
fuori da solo: ventotto funzioni, 1588 righe, *contigue*. A occhio, su seimila
righe, non si vedeva.

E soprattutto è venuta fuori la misura del taglio, che era la domanda vera: 21
variabili condivise, 11 funzioni chieste al navigatore e **3 sole offerte a
lui**. Verso l'esterno l'impaginato è quasi chiuso; quel che lo tiene legato
non sono le chiamate, sono le variabili — esattamente il genere di legame che
non si vede finché tutto sta in un file solo.

> **Tre errori dello script, tutti dello stesso tipo.** Per una funzione
> scritta su una riga sola la firma veniva tagliata all'*ultima* parentesi, che
> sta dentro il corpo: nell'intestazione finiva una graffa aperta. Una
> dichiarazione che finisce con un commento invece che col punto e virgola
> faceva inghiottire la riga dopo. E i globali dichiarati più d'uno per riga non
> venivano visti affatto. Ogni volta: rimetti il file com'era, correggi lo
> *script*, rifai il taglio da capo — correggere il risultato invece dello
> script avrebbe voluto dire un taglio che non si sa più rifare, e questo taglio
> va rifatto il giorno del passo 2.

### Il manuale di EX-IDE diventa una pagina, con l'indice

**testato** — Aiuto > Manuale apre il navigatore su
`/exwin/doc/exide.html`, e un clic sull'indice porta esattamente sul
paragrafo. Ventimila byte, ventinove `id`, cinquantasei rimandi interni.

**Non si riscrive un visualizzatore**, è la stessa decisione con cui
«Directory» non riscrive un file manager ma lancia `filemgr`: il navigatore
c'è, impagina, colora, segue i link e lo fa già per le altre nove pagine della
guida. Il manuale di EX-IDE è diventato la decima pagina di quell'insieme —
stessa barra di navigazione, stesso foglio di stile, una riga nell'indice della
documentazione — e le barre delle altre pagine ora lo nominano.

**Gli esempi sono condivisi, ed è il motivo per cui serviva l'indice.** Lo
scambio fra due caselle è un esempio di *Casella* tanto quanto di *Pulsante*;
l'uscita con la conferma vale per *Spunta*, per *Pulsante* e per «come si
esce». Ripeterlo sotto ognuno vorrebbe dire tre copie da tenere d'accordo;
metterlo sotto uno solo vorrebbe dire che chi cerca l'altro non lo trova.
Perciò gli esempi stanno tutti in fondo, con un `id` per uno, e **ogni
strumento ci rimanda** — e ogni esempio dice per quali strumenti vale. È
l'unica struttura in cui la stessa cosa è scritta una volta sola e si raggiunge
da tutti i posti da cui la si cerca; senza il salto all'ancora sarebbe stata un
elenco di titoli e «scorri finché non lo trovi».

> **Il manuale dentro il programma è rimasto**, ed è la ragione per cui era
> stato scritto: un manuale che sta in un file è un manuale che un giorno non
> c'è — il componente `/exwin` non installato, un CD montato a metà, il solo
> binario copiato. Adesso è la seconda scelta invece che l'unica, e la sua
> prima riga dice dov'è quello buono. Ma sono due copie dello stesso testo e
> prima o poi divergeranno: la strada, scritta fra le cose da fare, è
> accorciare quella interna a un promemoria e lasciare alla pagina il testo
> lungo.

**E il manuale interno è diventato un promemoria**, da 289 righe a 61: come si
comincia, i quattro file, le finestre e la tavola degli strumenti coi loro
eventi. Gli esempi, le proprietà una per una e il menu stanno solo nella
pagina — sono la parte lunga, cioè quella che diverge per prima. Il binario di
exide cala di undici kilobyte, e la prima riga del promemoria dice che se lo
stai leggendo vuol dire che la pagina non c'era.

### Le ancore: un link che porta a un punto della pagina

**testato** — quattro casi dentro EX-OS: un'ancora della stessa pagina, una che
non esiste, un indirizzo con la coda scritto nella barra, e un link che cambia
pagina *e* atterra sul paragrafo. Serviva alla documentazione: un manuale lungo
con l'indice in cima, dove si preme una voce e ci si trova sulla spiegazione.

Il navigatore non ci andava, e lo diceva da mesi in un commento: «questo browser
non sa ancora saltare a un punto dentro un documento; finché il salto non c'è,
non fare niente è la risposta più onesta». Adesso il salto c'è.

**Il pezzo da cui ripartire si trova dal nodo, non dal testo.** Ogni pezzo
impaginato sa già da quale nodo del documento viene — un campo aggiunto a suo
tempo per dire a uno script *dove* si è cliccato — quindi il salto è un giro
sull'albero (l'elemento con quell'`id`, o un vecchio `<a name>`) e uno
sull'impaginato, senza impaginare una seconda volta. Un'ancora della pagina
corrente non ricarica niente: ricaricare per poi saltare vorrebbe dire un giro
di rete, l'albero rifatto e i moduli riempiti a mano azzerati, tutto per
muovere una barra di scorrimento.

> **Il difetto ha richiesto una misura, non un'ipotesi.** Al primo giro il
> salto atterrava due righe sotto il titolo: sembrava un margine sbagliato, o
> la linea di base del carattere, o l'arrotondamento della riga — tre ipotesi
> plausibili e tutte sbagliate. Invece di provarle ho fatto stampare i numeri
> sulla riga di stato: `nodo 86, pezzo 152, y 870, scorri 0`. Il pezzo trovato
> era giusto, era il titolo. **Le `y` dei pezzi sono già in coordinate della
> finestra**, non del documento: l'impaginazione comincia sotto la barra
> dell'indirizzo, quindi scorrendo alla `y` del pezzo quel pezzo finisce
> *dietro* la barra e si vede la riga dopo. Con i numeri in mano ci sono voluti
> due minuti.

### Annulla: si fotografa tutto, non si registra cosa

**testato** — tre modifiche di tre tipi diversi, ognuna annullata, e alla fine
il file del disegno riletto dalla shell **identico a quello di partenza**.
Sedici passi indietro, con Ctrl+Z o Modifica > Annulla.

**Si fotografa il disegno intero prima di ogni modifica**, invece di registrare
cosa è cambiato. L'alternativa vuol dire scrivere l'operazione inversa di
ognuna — mettere un controllo, cancellarlo, spostarlo, ridimensionarlo,
cambiargli una delle otto proprietà, cambiare una delle quattro della maschera,
aggiungere una maschera, toglierne una che si porta via i suoi controlli: nove
inverse, ognuna sbagliabile in un modo suo, e quelle sbagliate si scoprono un
mese dopo. «Rimetti tutto com'era» non può sbagliare, è una copia. E il disegno
è piccolo abbastanza perché sia sensato: sedici istanti stanno in un centinaio
di kilobyte di memoria azzerata, che non finiscono nel binario.

**Una fotografia per trascinamento, e solo se qualcosa cambia davvero.** Un
trascinamento manda decine di eventi: fotografando a ognuno, i sedici passi se
li mangia un movimento solo e si torna indietro mezzo pixel per volta.
Fotografando invece all'inizio del trascinamento, un clic che sceglie e basta
lascerebbe un passo che non fa niente — e un Annulla che non fa niente è peggio
di non averlo, perché chi lo preme crede che sia rotto. Si fotografa al primo
cambiamento vero.

**E la storia non attraversa i progetti**: aprirne un altro e premere Annulla
rimetterebbe sulla maschera i controlli di quello di prima, con i loro nomi e i
loro id — un disegno mai esistito, pronto per essere salvato sopra quello vero.

> **Le scorciatoie erano etichette.** I menu promettevano Ctrl+N, Ctrl+O,
> Ctrl+S e Ctrl+Q dal primo giorno e premerli non faceva niente: nessuno le
> aveva mai collegate. Per Annulla la scorciatoia conta più che per gli altri —
> si annulla subito dopo aver sbagliato, con la mano ancora sulla tastiera, non
> aprendo un menu — e allora sono state collegate tutte insieme. Restano
> etichette quelle della finestra «Sorgente», e adesso è scritto dove si tiene
> quel che manca.

### Le maniglie si tirano, e il manuale spiega davvero

**testato** — dentro EX-OS, tirando ogni tipo di maniglia e leggendo i numeri
che finiscono nel file del disegno. I controlli si ridimensionano col mouse:
le otto maniglie si disegnavano dal primo giorno e non servivano a niente.

**Il problema non era tirarle, era prenderle.** Metà di ogni maniglia cade
*dentro* il controllo, e il clic cercava prima il controllo — l'ordine
naturale: così un clic sull'angolo cominciava uno spostamento e la maniglia non
si prendeva mai. Ora le maniglie si guardano per prime. E il quadratino che si
vede è 5 pixel — di più coprirebbe il controllo — mentre il bersaglio del mouse
è 11: si mira al quadratino e si prende comunque.

**Si tira un bordo, non una misura.** Tirando la maniglia di sinistra cambiano
`x` **e** larghezza insieme, perché il bordo destro non si deve muovere:
cambiando la sola larghezza il controllo scivolerebbe a destra mentre lo si
tira a sinistra. Lo stesso vale per i limiti — la misura minima ferma il bordo
che si sta tirando, non accorcia dall'altra parte, o il controllo scapperebbe
appena arrivato al minimo.

**Il manuale dentro il programma è diventato un manuale.** Per ogni strumento
dice a cosa serve, quali eventi ha e **con quali funzioni si comanda** da
`finestra.c`: `ex_acceso`/`ex_accendi` per Spunta e Radio, i sei `ex_lista_*`,
i sei `ex_voce_*` che Elenco e Linguette condividono, `ex_scorri_*`; più le
proprietà una per una — cosa diventa ognuna nel codice generato — e due esempi
completi. Uno c'era: due caselle che si scambiano il testo, ora con scritto
perché la copia d'appoggio serve davvero (`ex_testo_prendi` rende un
*puntatore* al testo del controllo, non una copia). L'altro mancava: **un
pulsante che chiude la finestra**, che nella principale è `ex_esci(0)` e in una
secondaria no — lì si chiama la procedura generata con `EXM_CHIUDI`, che
distrugge la finestra *e* azzera gli handle dei suoi controlli.

> **Un difetto vecchio, trovato scrivendone uno nuovo.** Aggiungendo il
> ridisegno al ridimensionamento è saltato fuori che lo *spostamento* non ne
> aveva mai avuto uno: trascinando un controllo cambiavano `x` e `y` e nessuno
> ridisegnava la tela, così il controllo si vedeva saltare nel posto nuovo solo
> quando qualcos'altro faceva ridisegnare la finestra. Adesso tutti e due i
> trascinamenti finiscono con un ridisegno.

### Più di una finestra: il disegnatore impara a contare

**testato** — dal disegno al programma che gira: due finestre disegnate,
generate, compilate con GCC vero dentro EX-OS, e la seconda che si apre
premendo un pulsante della prima. Un progetto di exide poteva disegnare **una
finestra sola**; adesso ne disegna otto.

**Il formato del disegno non è cambiato per fare posto.** Aveva già la forma
giusta — una riga per la maschera, poi le righe dei suoi controlli — e bastava
che le maschere potessero essere più d'una:

```
F principale 400 260 prg6
c etichetta Etichetta1 1001 76 52 90 16 0 Etichetta1
F finestra2 400 260 Finestra 2
c spunta Spunta1 1003 76 124 140 20 0 Spunta1
```

La riga vecchia si chiamava `f` e **si continua a leggerla**: non aveva il nome
— non serviva, la maschera era una — e infilarne uno in mezzo avrebbe fatto
leggere la prima parola del *titolo* come nome. Perciò la riga nuova ha una
lettera sua. I progetti fatti prima si aprono senza accorgersi di niente.

**La finestra principale tiene i nomi di sempre**, e le altre no: `g_form`,
`finestra_crea()`, `finestra_proc()` contro `g_form_opzioni`, `opzioni_crea()`,
`opzioni_proc()`. Sarebbe più simmetrico chiamarle tutte allo stesso modo, e
**ogni progetto fatto prima di oggi smetterebbe di compilare**: il suo
`finestra.c` — quello che l'IDE non riscrive mai — chiama `finestra_crea()` dal
main. Le secondarie le apre il tuo codice, di solito dall'handler di un
pulsante.

Tre dettagli che il generatore scrive e che chi scrive a mano dimentica:
chiamare `<nome>_crea()` due volte **non apre due finestre** (un pulsante si
preme più di una volta); chiudere una secondaria **non fa uscire dal
programma**, esce solo la principale; e alla chiusura i puntatori ai suoi
controlli **tornano a zero**, perché `ex_distruggi` porta via anche i figli e
quei nomi punterebbero al vuoto.

> **Una maschera per volta, e non un contenitore MDI — contro quel che avevo
> scritto io stesso nell'elenco delle cose da fare.** L'MDI c'è nel toolkit
> dal giorno prima ed era la strada segnata; a decidere sono stati i numeri: il
> ripiano del disegnatore è 436x396 e una maschera nasce 400x260. Due finestre
> di quella misura lì dentro si coprono quasi per intero — si passerebbe il
> tempo a spostarle per vedere quella sotto, per guadagnare di vedere insieme
> due cose su cui si lavora comunque una per volta. Resta la strada giusta il
> giorno che la tela diventa grande, o che si vorrà trascinare un controllo da
> una finestra all'altra.

### Salva con nome, Sostituisci, e un nome che restava indietro

**testato** — dentro EX-OS, con il file riletto dalla shell prima e dopo,
nello stesso giro di macchina. Le due voci che erano in cima all'elenco delle
cose da fare di exide.

**«Salva con nome» copia l'albero, non rigenera il disegno.** Poteva voler
dire due cose: rifare `finestra.dis`, `finestra.h` e `finestra_gen.c` dentro
una directory nuova, oppure copiare tutto e continuare a lavorare sulla copia.
La prima è più facile da scrivere e **butta via `finestra.c`**, che è l'unico
dei quattro file che l'IDE non possiede: è quello dell'utente, quello in cui
l'IDE aggiunge gli handler mancanti e non riscrive mai niente. Un «salva con
nome» che perde il corpo delle funzioni non è un salvataggio. Si copia
l'albero intero — `src/`, `inc/`, `lib/`, `bin/`, `obj/`, `progetto.txt`,
`compila.sh` — dopo aver salvato gli editor aperti, e **l'originale resta
intatto**: è una copia, non uno spostamento.

**«Sostituisci» cambia una occorrenza per volta, come Cerca.** In un sorgente
la stessa sequenza di caratteri sta dentro le stringhe, dentro i commenti e
dentro i nomi: `msg` sta anche in `messaggio`. Un «sostituisci tutto» le
cambia in silenzio tutte e tre, e chi lo lancia se ne accorge alla
compilazione o dopo. C'è in tutt'e due gli editor — quello del sorgente e il
file-editor — e nel toolkit è nato `ex_area_riga_metti()`, che riscrive una
riga in mezzo al documento: sedici righe che passano da `area_tocca()`, così
la catena del coloritore si invalida da lì in giù. Una primitiva del genere
va nel toolkit proprio per questo: un'applicazione che riscrivesse la riga da
fuori lascerebbe il colore vecchio, e solo qualche volta.

**E poi la rilettura ha trovato quel che la prova felice non tocca.** Tre
difetti, tutti nello stesso punto cieco — i percorsi in cui qualcosa va
storto. Il più grave: **copiare un file su se stesso lo cancella, e la copia
dice «riuscito»**. Non è un sospetto, sta in `kernel/fs/vfs.c`: l'apertura con
`O_TRUNC` azzera il file senza guardare chi altro lo tiene aperto, quindi la
lettura che segue trova zero byte, il ciclo non gira e la funzione riporta
successo. Chi riscriveva nel campo del dialogo il percorso del progetto
corrente si ritrovava ogni file a zero e la riga di stato che diceva
«progetto copiato». Adesso il controllo c'è in due posti, e una directory dove
c'è già un progetto non si sovrascrive in silenzio. Insieme a quello: nessun
ritorno di `mkdir` o della copia veniva guardato — col disco in sola lettura
exide si spostava comunque sulla directory nuova, che non esisteva. Ora c'è la
stessa prova di scrittura di «Nuovo progetto», la copia conta i file che non
sono arrivati, e **se ne manca uno solo l'IDE non si sposta**: mezza copia più
un IDE che ci punta dentro vuol dire che il prossimo Salva la trasforma
nell'originale.

> **Un difetto trovato dalla prova, non dalla rilettura del codice.**
> `progetto.txt` viene copiato com'era, riga `nome = ...` compresa, e la
> scheda del progetto la leggeva da lì: la directory era `prg6-copia` e la
> scheda diceva ancora `prg6`, mentre il titolo della finestra — che il nome
> lo ricava dalla directory — diceva quello giusto. Due posti che rispondono
> alla stessa domanda in modo diverso; lo stesso sarebbe successo rinominando
> la directory da fuori. La cura non è sincronizzarli: è toglierne uno. Adesso
> il nome viene **sempre** dalla directory, e il file continua a scriverlo, così
> il primo Salva lo rimette a posto da sé.

### Files e Directory: una finestra nuova, e una che non serviva scrivere

**testato** — elenco, apertura, modifica, salvataggio, richiusura e verifica
dal file vero, dentro EX-OS. Le ultime due voci del menu Strumenti che
dicevano ancora «in arrivo»: con questo giro il menu è completo.

**«Files» non è la finestra di «Sorgente»**, ed è una scelta e non una
scorciatoia. «Sorgente» conosce esattamente tre nomi — `finestra.c`,
`finestra_gen.c`, `finestra.h` — e mezzo programma è scritto sapendo che sono
quelli; infilarci un quarto nome qualunque avrebbe voluto dire portare quella
certezza dappertutto. Una finestra a parte, con Salva/Cerca/Chiudi e niente
altro, costa meno e non rischia di rompere quella che già funziona.

**È scoperta solo su `<progetto>/src`**, non su tutto l'albero: è l'unica
lettura di «files» compatibile con «un doppio clic apre come sorgente» — un
doppio clic su un `.o` dentro `obj/` non aprirebbe niente di leggibile. Il
coloritore si decide dal nome (`.c`/`.h` prendono `ex_colora_c`, il resto
niente) e **si spegne esplicitamente a ogni apertura**: la stessa area resta
in vita da un file all'altro, e senza azzerarlo un `.txt` letto dopo un `.c`
si vedrebbe colorato come se fosse C.

**«Directory» non riscrive un secondo file manager.** exide sa disegnare
rettangoli e liste; non sa copiare, spostare o cancellare file in sicurezza —
`filemgr` lo sa già fare, con la stessa struttura ad albero più elenco che
questa voce promette fin dalla richiesta originale. Dodici righe cercano
`filemgr` (prima in `/exwin/bin`, poi in `/cdrom/exwin/bin`) e lo lanciano con
la directory del progetto come argomento — lo stesso che accetta già dal menu
Applicazioni.

> **E la ricerca non si è riscritta una seconda volta.** Il file-editor voleva
> anche lui un «Cerca», identico a quello già provato nella finestra
> «Sorgente» — cambiava solo *quale* area e *quale* riga di stato usare.
> `ed_cerca()` è diventata `area_cerca(area, stato)`, e l'originale resta un
> involucro di una riga: due copie dello stesso ciclo sarebbero state due
> copie da tenere d'accordo per lo stesso identico algoritmo.

### La scheda del progetto, e una riscrittura che non doveva esserci

**testato** — scritta, chiusa, riaperta: tutto tornava. E un difetto vero
trovato provando esattamente questo.

**Rilegge lo stesso `progetto.txt` che il progetto già scrive alla nascita.**
Non un secondo formato: cinque chiavi `chiave = valore` — nome, autore,
versione, creato, descrizione — e in coda un marcatore `[nota]` dopo il quale
tutto, righe vuote comprese, è il testo libero della nota.

**Il nome e la data di creazione non si editano**, e non è una dimenticanza:
il nome viene dalla directory — riscriverlo qui non rinominerebbe niente — e
la data di creazione, per definizione, non è correggibile senza smettere di
essere vera. Sono etichette, non caselle. **Chiudere la finestra salva da
solo**, come l'editor: è una scheda a basso rischio, e chiedere conferma per
un'informazione a basso rischio è una domanda che si impara a schiacciare
senza leggerla.

> **«Nuovo progetto» su una directory già esistente cancellava la scheda.**
> Il disegno (`finestra.dis`) si apriva senza troncare — un file già presente
> restava quello che era — ma `progetto.txt` si troncava sempre: la stessa
> azione trattava due file dello stesso progetto in due modi diversi. Trovato
> riavviando per provare la *riapertura* di un progetto, non solo il
> salvataggio: il file sul disco era già corretto, verificato con `cat` prima
> di riavviare — era il passo dopo, non il salvataggio, a riscriverlo sopra.
> La cura: si controlla prima se il file c'è già, e si scrive solo se manca.

### La finestra del compilatore: GCC vero, dentro EX-OS, da un disegno

**testato** — un progetto vero, disegnato in exide, compilato con GCC del CD
degli strumenti *dentro EX-OS*, collegato e **avviato nella scrivania**.

**La riga di compilazione finisce in un file, e il file è il prodotto.**
`compila.sh` sta nella directory del progetto, si legge, si corregge a mano e
si lancia come qualunque altro script: il pulsante «Compila» non fa niente che
non si potrebbe fare digitando. **L'uscita va in un file** (`obj/compila.log`),
non in una pipe — che si rilegge con calma, ed è quel che serve a chi vuole
rivedere l'errore.

**Una libreria condivisa si collega con il suo stub**, non con un archivio: in
EX-OS una `.so` non si linka, si compila dentro il programma un file di poche
righe che risolve i nomi alla prima chiamata. La finestra Librerie sceglie
quali stub entrano nella riga — `exwin` sempre, il resto a spunta.

**E si aspetta senza morire**: cc1 pesa quaranta megabyte, e una finestra
ferma per un minuto sembra piantata. Si guarda se il figlio è finito con
`WNOHANG`, si smista un pugno di messaggi, si dorme un istante — la stessa
forma dell'attesa di rete del navigatore — con un tetto di cinque minuti oltre
il quale si smette di aspettare un compilatore impiccato.

> **Due difetti trovati girando il compilatore vero, e nessuno si vedeva
> leggendo il codice.** Le opzioni finivano tagliate a metà — la casella è un
> controllo "testo" che tiene 63 caratteri, non i 160 del campo C che la
> leggeva — e il taglio produceva un errore che non gli somigliava affatto:
> `-fno-pie` mozzato a un trattino solitario, e per gcc un `-` da solo vuol
> dire «leggi da stdin». La cura non è stata allargare la casella: è stata
> **togliere** dalla casella quel che non doveva starci — i flag obbligatori
> (`-ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie`) sono adesso
> fissi nel programma, non modificabili per sbaglio. Il secondo: il figlio
> compilava nella directory sbagliata — quella da cui era partito exide, in
> sola lettura — perché `spawn_ex` eredita il *cwd* del padre e nessuno lo
> cambiava prima di lanciarlo.

### Tre difetti visti usandolo, e nessuno stava dove sembrava

**testato** — sul navigatore, su exide dal CD, e su un disco installato da zero.

**La tendina del menu spariva muovendo il mouse.** Non era il server: era
l'ordine del disegno. La procedura di base disegna i controlli e *poi* la
tendina, ma un'applicazione che disegna anche del proprio — il navigatore con la
pagina, exide con la maschera — lo fa **dopo** aver chiamato la base, e ci passa
sopra. Col mouse fermo non si notava: è il movimento che fa arrivare
`EXM_MOUSE_MOSSO` e quindi un ridisegno. Ora la tendina si disegna dentro
`ex_aggiorna()`, la riga con cui *chiunque* dice «ho finito, mostralo»: qualunque
cosa si sia disegnata, la tendina viene dopo.

**exide apriva l'editor vuoto.** «`mkdir` è fallito» non vuol dire «c'era già», e
il programma ha creduto di sì dal primo giorno: avviando dal CD non si scrive da
nessuna parte, la creazione del progetto falliva a ogni passo *in silenzio*, e
l'editor si apriva su un file mai scritto. La prova che conta è **scrivere, non
chiedere**: ora si prova, e se non riesce lo dice e si ferma — spiegando che
serve un disco montato in lettura e scrittura.

**L'installatore chiedeva la lingua e lasciava la tastiera americana.** Il
sistema installato parlava italiano e scriveva americano: le lettere al posto
giusto e la punteggiatura no. Ora la tavola delle lingue ha una colonna per la
disposizione — **la tastiera non è la lingua**, l'inglese si scrive su una `us` e
domani qualcuno vorrà l'inglese su una `uk` — e `keymap` si *sovrascrive*, al
contrario di ogni altra voce: quel valore non era la scelta di nessuno, era
quello del supporto d'installazione copiato un momento prima.

> **E la prova migliore è un errore di battitura.** Dal disco appena installato i
> comandi del pilota escono storti: `keymap -p` diventa `keymap 'p`. Lo strumento
> manda scancode americani — se la punteggiatura esce spostata, dentro c'è una
> tastiera italiana.

### EX-IDE: si disegna una finestra, esce del C che gira

**testato** — dal CD in QEMU con un disco ext2 montato, e la prova che conta non
è una fotografia della maschera: è un binario da 18 KB, **generato dal disegno**,
che si apre dentro EX-OS con i controlli dove li avevo messi col mouse.

```
disegno -> finestra.dis -> finestra.h + finestra_gen.c -> gcc -> ld -> gira
```

**Tre aree, come in Visual Basic**: a sinistra i quattordici strumenti del
toolkit, in mezzo la maschera su cui si dispongono, a destra le proprietà di
quello scelto. Doppio clic su un controllo e si apre l'editor **dentro la
funzione che il suo evento chiamerà**.

**Quattro file, e uno solo è tuo.**

| file | chi lo scrive |
|---|---|
| `finestra.dis` | solo exide: il disegno |
| `finestra.h` | solo exide: gli id, i puntatori, i prototipi |
| `finestra_gen.c` | solo exide: crea i controlli e smista gli eventi |
| `finestra.c` | **solo tu**: exide ci *aggiunge* gli handler che mancano e non riscrive mai quel che c'è |

È il punto in cui VB6 si rompeva — un file solo, scritto a metà dal generatore e
a metà a mano, e ogni rigenerazione era una scommessa. Qui il generato e lo
scritto non si toccano mai.

**La giuntura è l'id, e c'era già.** `ex_crea(..., padre, ID, 0)` dà a ogni
controllo un numero e l'evento torna come `EXM_COMANDO` con quel numero dentro:
nel disegno il pulsante *è* `ID_PULSANTE1`, nel sorgente c'è `case
ID_PULSANTE1:`. exide è fattibile qui più che altrove perché ExWin era già fatto
a forma di VB6.

**E un difetto del toolkit, trovato perché serviva a lui.** Le liste e le aree
non liberavano il loro posto alla distruzione: una finestra con dentro una lista,
aperta e chiusa quattro volte, esauriva i quattro posti e alla quinta la lista
non si apriva più — nessun errore, un elenco vuoto. Era lì da mesi e non l'aveva
trovato nessuno, perché nessun programma apriva e chiudeva la stessa finestra
abbastanza volte. Il primo è stato exide, il giorno in cui è nato.

> **Il linker script è nato vuoto, e il collegamento è riuscito lo stesso.**
> `open(f,'w').write(open(f).read()...)` tronca il file prima di leggerlo; `ld -T`
> con uno script vuoto non protesta, butta via tutto e produce un ELF di 256 byte
> senza segmenti. Il sintomo era «ELF load fallito» a runtime — che non somiglia
> affatto alla sua causa.

### Finestre dentro una finestra: il contenitore MDI

**testato** — dal CD, in QEMU, con `winprova -m`: tre finestre in un contenitore,
ognuna con la sua procedura, e ogni comando sulla seriale con il **nome della
procedura** che l'ha ricevuto.

**Una finestra figlia non è una finestra del server**, ed è la decisione da cui
discende tutto il resto. Dargliene una vera vorrebbe dire una zona di memoria
condivisa per ognuna, un giro di richieste a ogni apertura e — soprattutto —
finestre che possono uscire dal contenitore, perché il server non sa che
dovrebbero starci dentro. Qui la figlia è un *controllo*: pixel dentro la zona
del padre, disegnati dalla libreria. Il prezzo è dichiarato — trascinandola si
ferma al bordo — ed è esattamente quel che un MDI deve fare.

**Il ritaglio, che in quattro mesi di toolkit non era mai servito.** Il disegno
si ritagliava su una cosa sola: la zona di pixel della finestra di primo livello.
Bastava, perché un controllo sta dove lo si è messo e non si muove. Una finestra
MDI *si muove*, e un controllo più grande del suo client si disegnerebbe sopra il
telaio — un difetto che si vede solo trascinando, cioè tardi. Ora ogni figlia si
disegna due volte ritagliata: il telaio dentro il contenitore, il contenuto
dentro la propria area del client. Costa un confronto per pixel quando è acceso,
e **zero quando è spento** — cioè in ogni finestra che non usa l'MDI.

**Attivare e poi fare è un gesto solo.** Cliccando un pulsante di una finestra
dietro, quel pulsante si preme: la finestra viene davanti *e* il clic arriva dove
è caduto.

**I comandi vanno alla procedura della figlia**, non a quella dell'applicazione:
è cio che rende l'MDI utile invece che decorativo. E se la figlia non ha una
procedura vanno all'applicazione come sempre.

**Chiudere una figlia non chiude il programma** — la distinzione sta dove finisce
chi non gestisce la chiusura, altrimenti quel pulsante spegnerebbe l'applicazione
intera al primo clic. E **Tab gira dentro la finestra attiva**: altrimenti si
vedrebbe un cursore lampeggiare in una finestra che non si sta guardando.

> **Per la seconda volta in un giorno l'occhio ha sbagliato.** Dopo F6 sembravano
> attive due finestre; misurati i pixel, `(30,77,125)` e `(128,128,128)` — attiva
> e inattiva, esatte. La regola, adesso che è costata due volte: una fotografia si
> guarda per capire *dove* guardare, e poi si contano i pixel.

### Il testo colorato, e una seconda area di testo che non è nata

**testato** — dal CD, in QEMU, con `winprova -c`: un pezzo di C scelto per
toccare ogni ruolo, e i colori verificati leggendo i **pixel** della fotografia,
non guardandola.

**La cosa più importante è quella che non si è scritta.** La strada ovvia era un
controllo nuovo con dentro il suo cursore, la sua selezione, i suoi appunti, il
suo clic che posiziona: mille righe già scritte una volta per l'area di testo, e
la seconda copia sarebbe stata una copia da tenere d'accordo per sempre.
`areacodice` **non è un controllo nuovo: è la stessa area con altri due numeri** —
3000 righe da 240 colonne invece di 512 da 200, più un coloritore. La classe è la
stessa, e da `ex_crea` in poi non c'è nessuna differenza: stesso disegno, stessi
tasti, stesse `ex_area_*`.

**Il coloritore sta in chi sa la lingua, non nel toolkit.** Il controllo sa
*disegnare* del testo colorato; quali parole siano chiavi e dove finisca un
commento lo sa chi scrive l'editor. Il gancio riceve una riga e riempie **un
ruolo per carattere** — chiave, tipo, stringa, numero, commento, preprocessore,
funzione — e i ruoli sono ruoli, non colori: la tavolozza è una tabella sola,
quindi due editor non colorano le chiavi di due blu diversi.

**E un commento a blocco attraversa le righe.** Il coloritore rende anche *come
finisce* la riga, e il controllo tiene quello stato: un byte per riga. Si
ricalcola solo da dove il testo è cambiato in giù — da capo a ogni ridisegno
sarebbero tremila chiamate per ogni tasto premuto. Scrivendo `/*` a metà
documento, le righe sotto diventano verdi mentre si batte.

**Il cursore adesso si porta**, e prima si poteva solo leggere: `ex_area_vai` è
quel che serve a una colonna che elenca le funzioni di un sorgente — cliccarci
sopra e finire su quella riga, a metà altezza, con il contesto intorno.

> **Un'ora persa a leggere una fotografia.** Le sonde dicevano «commento» e lo
> schermo sembrava dire di no; il difetto è stato cercato in tre posti prima di
> misurare i pixel del PPM e trovarci `(0,128,0)` — il verde della tavolozza. Le
> righe erano verdi da sempre: a 800x600 rimpicciolito, il verde di un commento e
> il blu di una parola chiave si somigliano. Una fotografia si guarda; i suoi
> numeri si contano.

### Il toolkit impara cinque controlli, e il posto dove si aggiungono

**testato** — dal CD, in QEMU, con `winprova -n`: ogni comando finisce sulla
seriale con l'id e il valore, che è un numero da confrontare e non un'impressione.

**Mancavano, e la mancanza era già scritta nel codice.** Accanto alla finestra
delle impostazioni del navigatore c'era: «nel toolkit una casella di spunta non
c'è, e disegnarne una a mano qui dentro vorrebbe dire un controllo che vive in un
programma solo». Per questo gli interruttori delle impostazioni sono pulsanti che
cambiano scritta. Adesso ci sono: **`spunta`, `radio`, `scorrimento`, `combo` e
`tab`**, e sono il primo passo verso `exide`, l'ambiente di sviluppo visuale.

**L'orientamento della barra lo dice la forma**, non un bit di stile: più larga
che alta è orizzontale, più alta che larga è verticale. Un bit in più si potrebbe
mettere in disaccordo con la misura, e allora bisognerebbe decidere chi ha
ragione.

**Il gruppo di un radio sono i fratelli** — i controlli con lo stesso padre. Non
c'è nessun gruppo da dichiarare: due gruppi di scelte si mettono dentro due
`riquadro`, che è come si disegnano da sempre. La cornice che si vede *è* il
gruppo che vale, quindi la regola visiva e quella logica non possono andare in
disaccordo.

**La spunta si arma premendo e scatta alzando il dito**, come un pulsante:
scivolare via prima di alzarlo annulla. **La barra invece agisce subito** — una
freccia tenuta premuta deve scorrere — e il suo trascinamento sveglia
l'applicazione a ogni pixel, dove una lista trascinata dice «ho scelto» una volta
sola: una barra trascinata *è* il documento che scorre.

**E i sette posti da toccare per aggiungerne un altro** stanno scritti in un
punto solo, sopra i numeri delle classi: il modo di sbagliare non è scrivere male
un controllo, è dimenticarne uno — e accorgersene dal fatto che il controllo si
vede ma non si clicca, o si clicca ma Tab non ci arriva.

### La finestra resta viva mentre scarica, e la posta ha un padrone

**testato** — dal CD, in QEMU: `https://www.google.com/` (200, 83 KB, 108 nodi),
una pagina locale con sei immagini ritardate di venti secondi l'una, e le prove
sul banco (256 + 244 + 244 + 51, zero sbagliate).

**Mentre si scaricava, la finestra era morta.** Chi legge dorme dentro la
lettura, e mentre dorme il puntatore non si muove, la finestra non si ridisegna
e nessun tasto arriva: su una pagina grande sono decine di secondi di schermo
fermo. Adesso il trasporto sa fare una domanda che prima non sapeva fare —
*quanti byte ci sono adesso?* — e quando la risposta è zero cede il turno a chi
ospita. La risposta viene dallo stack (`IP_MSG_TCP_STATO`, che non prenota
niente e non aspetta) e non da un tentativo di lettura con una scadenza corta:
quello non distingue il timeout dall'errore e lascia una prenotazione pendente
per ogni tentativo.

**E così si può fermare.** `Esc` interrompe lo scaricamento e la pagina di prima
resta dov'era; quel che era arrivato non si tiene, perché una pagina tagliata
mostrata come intera è peggio di una pagina che non c'è. Prima non si poteva
nemmeno offrirlo: mentre si scaricava, nessun tasto arrivava.

**La stretta di mano cifrata dice a che punto è.** Sette passi — la chiave, il
ServerHello, il segreto, i certificati, la firma, la catena, la fine — scritti
nella riga di stato mentre passano. E misurata: la stretta costa **490 ms** in
tutto (magazzino delle 150 CA 60 ms, DNS e connessione TCP 320 ms). I «venti
secondi su un 386 emulato» stavano scritti in tre posti e non erano mai stati
misurati.

**La cassetta postale ha un padrone.** Un'applicazione grafica riceve nella
stessa mailbox gli eventi del server a finestre *e* le risposte dello stack IP,
e mentre si scarica ci sono due che aspettano: chi legge la rete e il ciclo dei
messaggi. Ognuno scorreva la coda cercando il proprio e teneva in mano quel che
era dell'altro per rimetterlo alla fine — e chi tiene in mano può lasciar
cadere: lo scaffale dove si rimetteva aveva quattro posti, e nessuno guardava il
`-1` di quando era pieno. Adesso c'è `ipc_scegli`: si passa un filtro che di
ogni messaggio dice **è mio / è di un altro / non è di nessuno**, e quel che è di
un altro **resta dov'è**. Non passa mai per le mani di chi non lo vuole, quindi
non lo può perdere.

> Lo scaffale conta i byte e non i messaggi, ed è la stessa memoria di prima —
> sei kilobyte. Un clic del mouse sono venti byte: prima ne occupava uno dei
> quattro posti, adesso ce ne stanno ventiquattro. E un messaggio «di un altro»
> si **salta** invece di rimetterlo, il che toglie di mezzo la trappola che
> teneva ferma la pompa dei messaggi ogni volta che lo stack aveva una risposta
> da parte.

**E la finestra di TCP non si riapriva.** Chi legge a scatti — invece di
prenotare e dormire — lascia che il buffer di ricezione si riempia fra uno
scatto e l'altro, e quattro kilobyte un mittente veloce li riempie in un
istante. Lo stack annunciava lo spazio libero **solo dentro un ACK**, cioè solo
all'arrivo di un segmento: quando poi il buffer si svuotava non aveva più modo
di dirlo, e il server restava fermo ad aspettare noi. Il sintomo era una pagina
`https` che non finiva mai di arrivare, e per un giorno è sembrato un difetto
della cassetta postale. Adesso chi si libera lo dice: un ACK vuoto di venti
byte, e la finestra riparte.

### Il navigatore esegue JavaScript, e i motori sono due

**testato** — 256 prove sul linguaggio, 244 sul ponte con ExJs e 244 con
QuickJS, più quindici riquadri provati dentro EX-OS.

**Due motori, la stessa interfaccia.** `exjs.so` è scritto qui dentro;
`quickjs.so` è QuickJS compilato per EX-OS. Si sceglie quale caricare, e il
ponte con la pagina — `exdom.so` — è lo stesso per tutt'e due: le 244 prove
girano identiche sull'uno e sull'altro, che è il modo di dire che
l'interfaccia è davvero una.

**Il DOM che serve a una pagina vera**: `document`, gli elementi, gli attributi,
le classi, `innerHTML`, gli eventi con `addEventListener` e `preventDefault`, i
moduli e il pulsante che li manda.

**`XMLHttpRequest` e `fetch`**, sincroni e asincroni — e l'asincrono è vero: la
richiesta parte, il resto dello script prosegue, e la risposta arriva quando il
ciclo dei messaggi la consegna. L'**ordine** in cui le cose accadono è una delle
quindici prove, perché è la parte che si sbaglia in silenzio.

**Un difetto che non somigliava alla sua causa.** Due stub nello stesso processo
possono scegliere due librerie diverse: il navigatore apriva un motore ed
`exdom.so` l'altro, e nello stesso processo giravano due motori che non si
vedevano. Il sintomo era un page fault dentro il primo con in mano un contesto
costruito dal secondo. La risposta ring 3 non può darsela — non c'è modo di
sapere quali pagine ti sono mappate senza provare a leggerle — e infatti la dà
il caricatore, con `SYS_LIB_TROVA` (0.208): la tavola delle pagine del processo
*è* l'elenco.

### I biscotti, tutt'e due le metà

**testato** — 51 prove sulla dispensa, più il giro completo contro un server di
prova.

Un cookie non è una stringa da rispedire: è una regola su **quale dominio** e
**quale percorso**, con una scadenza. La dispensa tiene le regole, decide quali
biscotti valgono per l'indirizzo che si sta aprendo, e li attacca alla richiesta
— comprese quelle che partono dentro una redirezione, che è dove metà dei siti
mette l'accesso. Si prova da sola, senza schermo e senza rete, perché le regole
di corrispondenza si sbagliano in silenzio.

> **La persistenza non è ancora provata dentro EX-OS**, solo sul banco: da CD non
> si scrive, e il giro completo vuole un sistema installato.

### Il suono: quattro schede e il MIDI

**testato** — in QEMU, ascoltando il file che QEMU registra e non i contatori del
driver.

Quattro driver — SB16, AC'97, ES1370/ES1371 e HD Audio — dietro un protocollo
solo, così un programma che suona non sa quale scheda c'è. Il MIDI sulle schede
PCI si sente ed è intonato: è una tavola d'onde a otto seni, cioè un timbro solo
— il cambio di strumento si ignora e la percussione non c'è. È dichiarato, ed è
il punto da cui si riprende.

> **Quel che non è mai girato è segnato dove comincia.** QEMU emula l'ES1370, non
> l'ES1371: il convertitore di frequenza e il codec AC'97 dentro il driver
> dell'ES1371 sono scritti sulla sequenza documentata e non sono mai stati
> eseguiti, né su silicio né in emulazione. E la **registrazione non c'è**: tutti
> e quattro sanno solo suonare.

### Google non è un bersaglio del navigatore, e adesso si sa perché

**testato** — misurato, e la strada è chiusa.

`google.com` si apre e si vede. La pagina dei **risultati** no, e non è colpa del
nostro JavaScript: con lo User-Agent di Lynx, di w3m o di MSIE 6 Google risponde
«Aggiorna il browser», e con quello di un Firefox recente manda il suo controllo
anti-robot offuscato. Non c'è una terza porta. Sta scritto qui perché una strada
provata e chiusa vale quanto una funzione aggiunta: chi la riprova ci perde lo
stesso tempo due volte.

### La scrivania ha una console sua, si spegne, e parla la tua lingua

**testato** — dal CD e su un disco ext2 installato da zero, in QEMU.

**Le console diventano cinque.** Il server grafico se ne prendeva una delle
quattro e chi lavorava in testo ne aveva tre senza averlo chiesto. Ora Alt+F1–F4
restano di chi scrive e la grafica sta in fondo: digitando `exwin` lo schermo ci
passa da solo, e quando la grafica si spegne torna alla console di partenza.

**E la console della grafica esiste solo mentre la grafica c'è.** Finché il
server non gira, Alt+F5 non fa niente — lì ci sarebbe uno schermo nero con una
shell che nessuno guarda, e chi ci finisce non sa come tornare. Lo stato sta nel
*kernel* e non nel server, così un server ucciso libera la console da solo.

**`Esci` dal menu spegne la scrivania**, non solo il program manager. Alle
applicazioni si *chiede*, non le si uccide: a ognuna arriva la stessa chiusura
della crocetta, così chi ha da salvare fa in tempo; poi torna il modo testo.

**L'installazione chiede la lingua** e la scrive in `kernel.cfg`. Il kernel non
la usa per niente — tradurre è lavoro dei programmi — ma la conserva e la
riconsegna come fa con `keymap`, così non ci sono due elenchi che divergono.

**E in fondo propone `hwconfig`**, puntato al disco appena installato: il
`kernel.cfg` copiato è quello del supporto d'installazione, e il primo avvio da
disco è il momento peggiore per scoprire che manca il driver del controller.

**La risoluzione si sceglie da Avvio > Impostazioni...** — il meccanismo c'era
già tutto (`/dev/svga.drv` scrive la voce in `kernel.cfg` e un byte dentro Stage
2); mancava la finestra. Si applica al riavvio, perché il modo video lo sceglie
l'avvio col BIOS.

**Copia e incolla dentro i campi di una pagina**, con tutt'e due gli standard:
`Ctrl+C/X/V` e `Ctrl+Ins` / `Shift+Ins` / `Shift+Canc`. Gli appunti sono quelli
di tutta la scrivania — la stessa memoria condivisa di `ex_area` — quindi si
copia da un modulo e si incolla in un editor.

### Il browser regge una pagina vera: impaginazione, immagini, caratteri, moduli

**testato** — dal CD, in QEMU, sulla voce «Operating system» di Wikipedia (676
KB) e su pagine costruite apposta.

**La lentezza non era più la rete, era l'impaginazione.** A ogni immagine che
arrivava si rifaceva l'impaginazione dell'intero documento. Quando l'`<img>`
dichiara `width` e `height` la misura finale si sa prima di scaricare un byte:
il posto si tiene subito, e quando l'immagine arriva ci entra senza spostare
niente. Da 7,0 s a 3,3 s per dodici immagini; e la prova che il testo non si
muove è pixel per pixel — **zero** pixel cambiati fuori dalla banda delle
immagini.

**L'arena non era piena di testo: era piena di attributi.** Misurata su quella
voce: il testo dei nodi 71 KB (15%), i nomi dei tag 20 KB (4%), gli attributi
386 KB (81%). Si troncava una pagina di 70 KB di parole perché non c'era posto
per gli attributi. Alzata a 1 MB (e `ATTR_MAX` a 16000, perché quella voce ha
~13.600 attributi), la pagina **non si tronca più**.

**Quante immagini tenere lo decide la macchina**, non una costante: un
sedicesimo della memoria libera letta con `meminfo()`. Un tetto fisso era avaro
su un PC grande e causava `OUT OF MEMORY` su uno da 32 MB. E il posto si
controlla *prima* di scaricare, così l'immagine che non ci sta non costa né
rete né CPU.

**`font-family` adesso si legge.** Prima ogni pagina usciva in serif e il
monospazio arrivava solo dal tag `<pre>`. L'elenco si scorre e ci si ferma al
primo nome riconosciuto — i nomi veri contano quanto le generiche, perché metà
del web scrive `font-family: Courier New` e basta.

**I caratteri che mancavano.** Liberation copre 2327 codici, DejaVu 5918: si
ripiega **carattere per carattere** su DejaVu invece di sostituire il font, così
il testo resta com'è e si riempiono solo i buchi. Greco, cirillico, ebraico e
arabo si leggono. E le entità sopra il Latin-1 — `&#8212;`, le virgolette
curve, i puntini — non diventano più `?`.

**`colspan` e `rowspan`**, senza i quali ogni tabella con un'intestazione che
scavalca due colonne mandava fuori posto tutte le celle dopo di lei.

**E i moduli non ricevevano i tasti.** Non mancava il cursore: mancavano i
tasti. Il fuoco restava alla barra dell'indirizzo — un controllo del toolkit —
mentre i campi di una pagina sono rettangoli disegnati, che il fuoco non
possono averlo. Si scriveva nella barra credendo di scrivere nel modulo. Ora il
cursore si muove con frecce, Home, Fine e cancellazioni, e si inserisce in
mezzo al testo.

> **Un difetto del toolkit, non del browser:** quando una casella consumava un
> tasto, `exwin` rifaceva sfondo e controlli *senza avvisare l'applicazione*,
> per non svegliarla a ogni lettera. Giusto per una finestra di soli controlli;
> per una che disegna anche del suo, quel disegno spariva a ogni tasto battuto.
> Riguarda ogni programma che mescoli controlli e disegno proprio.

### Utenti veri: due conti, `sudo`, e un recovery quando l'accesso è rotto

**testato** — installazione da zero su ext2, i due conti chiesti e creati, poi
avvio dal disco: `uid=1000(graziano)`, casa `/home/graziano` di sua proprietà.
`sudo id` rende `uid=0(root)`, e dopo `exit` da `sudo -s` si torna a
`uid=1000`.

Prima i conti li creava `login` al primo avvio, e ne creava **uno solo**: root.
Da lì in poi si lavorava sempre da amministratore, che è il modo in cui un
errore qualunque diventa un danno qualunque. Adesso li chiede l'installatore —
root per riparare, il tuo per lavorare — e chiede anche se il tuo deve poter
fare le cose da root con la propria password.

| | |
|---|---|
| `/boot/utenti` | `nome:uid:gid`, 0644 |
| `/boot/ombra` | `nome:sale:impronta`, 0600 — SHA-256 di `sale:password` |
| `/boot/amministratori` | un nome per riga, 0644 |
| `lib/exuser` | leggere una password senza eco, verificarla, aggiungere un conto |
| `bin/sudo` | il comando |
| `SYS_SU` (254) | la capacità stretta: «diventa root SE sai la password» |

    sudo <comando> [argomenti]   esegue quel comando come root
    sudo -s                      apre una shell di root
    sudo                         non fa niente e stampa l'uso

! **ESEGUIRE UN COMANDO E APRIRE UNA SHELL NON SONO LA STESSA COSA DETTA IN DUE
MODI.** Con `sudo comando` i poteri durano quanto il comando e finiscono da
soli; con una shell durano finché qualcuno si ricorda di uscire. La seconda si
ottiene — `-s` — ma bisogna **chiederla**: aprirla a chi ha battuto `sudo` e
invio vorrebbe dire dare la più pericolosa delle due a chi non l'ha domandata.

! **IL PROGRAMMA `sudo` NON HA NESSUN POTERE**, ed è la parte che regge tutto
il resto. Non è setuid — il bit setuid sui file in EX-OS non esiste, di
proposito, perché renderebbe pericoloso ogni eseguibile che lo porta e non c'è
modo di controllarli tutti — e non decide niente. Legge una password, la passa
al kernel, e **decide il kernel**. Un `sudo` sostituito con un programma
qualunque resta un programma qualunque.

! **LA VERIFICA STA NEL KERNEL PERCHÉ NON POTEVA STARE ALTROVE.**
`/boot/ombra` è 0600 di root e deve restarlo: se fosse leggibile, chiunque si
porterebbe via le impronte e le proverebbe con comodo su un'altra macchina.
Quindi il confronto lo deve fare qualcuno che quel file lo può aprire. È lo
stesso motivo per cui su Unix `su` è setuid root, e costa uno SHA-256 dentro il
kernel.

! **E IL KERNEL NON RIUSCIVA AD APRIRLO.** `SYS_SU` gira nel processo di chi
chiama, uid 1000, e il VFS guardava le credenziali del **processo** invece di
quelle di chi stava davvero leggendo. La riparazione è `vfs_open_autorita()`:
non un bit dentro `flags` — arriverebbe da `sys_open`, cioè da un numero
scelto dall'utente, e basterebbe indovinarlo per leggersi le password — e non
uno stato globale, che varrebbe per **tutti** i processi finché è acceso,
mentre il VFS qui dentro riscadenza. Il permesso viaggia come argomento e
finisce con la chiamata.

! **E UNA CONSOLE CHE NON APRE `login` NON STA PROTEGGENDO NIENTE**: è una
console il cui sistema di autenticazione è **corrotto**. Lasciarla chiusa non
difende — chi ha la macchina davanti avvia da CD e monta il disco in trenta
secondi — e toglie l'unico modo di ripararla da dentro. Si apre una shell di
root, e lo dice a chiare lettere prima di aprirsi: «questa shell gira come root
e NESSUNO ha fatto l'accesso». Non è un accesso: è una macchina rotta che si
apre per essere riparata.

### `ls -l`: i permessi e i proprietari si vedono

**testato** — e ha trovato due difetti, uno dei quali nel kernel.

! **UNA CHIAMATA IN PIÙ, NON UN CAMPO IN PIÙ A `Stat`.** Cambiare una struttura
che i programmi si passano già vuol dire ricostruire tutto ciò che la usa, e
`Stat` la usa chiunque apra un file. `st_uid` e `st_gid` restano a zero e
continuano a dichiararlo; la verità la chiede chi la vuole, con `statperm()`
(`SYS_STATPERM`, 253).

! **I DATI C'ERANO GIÀ E NON USCIVANO**: `VfsStat` porta modo, uid e gid da
quando ext2 ha imparato i proprietari, ma `sys_stat` non li copiava. Mancava il
trasporto, non l'informazione.

! **E `VfsStat` NON ERA INIZIALIZZATA.** Su un CD `ls -l` mostrava permessi
diversi per ogni file e proprietari a cinque cifre: `stat_interno()` lasciava
modo, uid e gid a quello che c'era nello stack: li scriveva solo il ramo ext2,
mentre ISO 9660, il FAT12 del floppy e la radice non li toccavano affatto. E
non era estetica: **`vfs_permesso()` decide con `st->modo`**, dove zero vuol
dire «questo volume non ha proprietari, passa».

Su un volume senza proprietari `ls -l` scrive `?????????` e `-`, che è la
verità: su FAT e su ISO 9660 il proprietario non esiste.

### Lo schermo si ricompone solo dove è cambiato

**testato** — misurato con una spia nel ciclo del compositore, non stimato:

    ricomposizioni durante il movimento del puntatore
        29 da   260 pixel
        17 da   240
         2 da   117
        39 da 480.000   (avvio, finestre nuove, pressioni di bottone)

cioè **260 pixel invece di 480.000** — circa 1850 volte meno — per il caso che
si ripete a ogni movimento della mano.

! **IL RITAGLIO STA NELLE DUE PRIMITIVE, NON NEI CHIAMANTI.** `px()` e
`riempi()` sono le sole due strade per arrivare al framebuffer, quindi tutto
ciò che disegna — cornici, prese, contorni, il puntatore — eredita il ritaglio
senza sapere che esiste. Metterlo nei chiamanti vorrebbe dire ricordarselo a
ogni funzione nuova, e prima o poi qualcuno non se lo ricorda.

! **LA COPIA DELLA ZONA DEL CLIENT È L'ECCEZIONE**, e ce l'ha per forza: non
passa dalle primitive apposta, perché va per righe intere con MMX ed è quello
che la rende veloce. Lì il ritaglio si applica a mano.

! **IL PREDEFINITO È «TUTTO», E VA TENUTO COSÌ.** Una regione sporca sbagliata
per difetto lascia pixel vecchi sullo schermo: un difetto che non si vede dove
è stato fatto e che si manifesta come «ogni tanto resta un pezzo di finestra».
Oggi si stringe **un caso solo**, il movimento del puntatore; gli altri undici
dichiarano ancora tutto lo schermo, ed è dichiarato.

### Il browser: tabelle, elenchi, `<pre>` e i fogli di stile fino in fondo

**testato** — bordi destri identici su tutte le righe (232, 469, 753),
distacchi di 8 px, somma 744 = la larghezza dell'area.

! **LA LARGHEZZA DI UNA COLONNA NON SI SA FINCHÉ NON SI È GUARDATO OGNI
CONTENUTO DI QUELLA COLONNA**, ed è l'unico posto del browser che vuole **due**
passate: il resto della pagina si impagina in avanti, una parola dopo l'altra,
senza tornare indietro.

! **E MENTRE SI MISURA NON SI ALLINEA.** È il difetto che hanno trovato i
pixel: il foglio predefinito centra i `<th>`, quindi la passata di misura
spingeva i pezzi in mezzo alla riga e la larghezza tornava «metà pagina»
invece che «quanto la parola». L'allineamento decide **dove** mettere una riga,
la misura chiede **quanto** occupa. Niente `colspan`/`rowspan`, niente bordi:
dichiarato.

`<hr>` era solo un po' d'aria: adesso è una riga, disegnata come uno sfondo
alto due pixel perché è esattamente ciò che è. `<ul>` e `<ol>` hanno il loro
segno, e per `<ol>` il numero si conta **fra i fratelli** — due liste annidate
si darebbero i numeri a vicenda.

! **DENTRO `<pre>` GLI SPAZI E GLI A CAPO SONO IL CONTENUTO**, ed è tutta la
ragione per cui quel tag esiste. La correzione andava in `exhtml.so`, non nel
browser: chi analizza riduce a uno spazio qualunque sequenza di bianchi — che è
la regola dell'HTML e va bene per tutto il resto — e ridurli lì vuol dire che
nessun utilizzatore, per quanto attento, può più rimetterli. L'informazione è
persa prima di arrivargli.

Dei fogli di stile si applicano adesso anche **allineamento, i quattro margini
e il colore di sfondo**, che `excss.so` calcolava e il browser buttava via —
tre proprietà su otto promesse dalla libreria e scartate da chi la usa, il tipo
di divario che non dà nessun errore.

! **L'ALLINEAMENTO NON SI PUÒ APPLICARE MENTRE SI SCRIVE**: per centrare
bisogna sapere quanto è larga la riga, e lo si sa solo quando è finita. Si
segna il primo pezzo, e al momento di andare a capo si spostano tutti quelli
della riga.

! **UNO SFONDO NON È UN PEZZO, È CIÒ CHE STA SOTTO I PEZZI.** Vive in un elenco
suo e si disegna prima di tutto il testo: metterlo fra i pezzi vorrebbe dire
dipingere sopra le parole già scritte, perché l'ordine dei pezzi è quello del
documento e non ha niente a che fare con la profondità.

### «Informazioni su», e l'orologio che non spariva

**testato** — e la prima diagnosi era sbagliata, smentita da un A/B.

Ogni programma della scrivania — browser, file manager, editor — dice adesso
nome, che cosa fa, l'autore e **la memoria che sta usando**: immagine
(`_start`→`_bss_end`), heap (`sbrk(0)`) e pila. Dalla shell lo dice `ver`. Sta
in `lib/exinfo`, perché quattro copie dello stesso riquadro divergono alla
prima riga aggiunta.

! **L'OROLOGIO NON SPARIVA AL PRIMO CLIC: ANDAVA SOTTO.** Avevo dato la colpa
al ridisegno della finestra su qualunque messaggio non gestito, e l'A/B l'ha
smentito — due esecuzioni identiche al pixel, con l'unica differenza nella
cifra dell'ora. La causa vera era `in_cima()`, che rialzava una finestra già in
cima: la barra è `WIN_ST_SOPRA`, e rialzarla la rimetteva **sotto** le altre
dello stesso strato.

### Un browser: dalla rete allo schermo

**testato** — `http://www.google.com` rende 200 e 82550 byte; `http://example.com`
si vede impaginato in `/exwin/bin/browser`, con il titolo in Liberation Sans
Bold a 22 e il corpo in Serif a 15. E le **immagini della pagina si vedono**:
una prova con tre PNG generati a mano dà 3000 pixel esatti per ciascuno dei tre
colori attesi — cioè la misura naturale e quella dichiarata con `width`/`height`
sono tutt'e due giuste al pixel.

| | |
|---|---|
| `lib/exhttp/http.c` | HTTP/1.1 senza rete: URL, richiesta, intestazioni, corpo «a pezzi» |
| `lib/exhttp/exhttp.c` | il trasporto TCP, le redirezioni |
| `lib/exhtml/html.c` | da testo ad albero — **`/exwin/lib/exhtml.so`**, a disposizione di ogni programma |
| `bin/scarica` | prende una pagina e la stampa o la salva |
| `exwin/bin/browser` | barra dell'indirizzo, collegamenti, scorrimento, immagini |
| `lib/eximg/eximg.c` | PNG, JPG e ICO — aperta a richiesta, non collegata |

! **IL TRASPORTO È UN PARAMETRO, NON UNA COSA SAPUTA.** Oggi sotto l'HTTP c'è
il TCP; domani, per `https://`, ci sarà il TLS. Se il codice aprisse la
connessione da sé, quel giorno andrebbe riscritto — e sarebbe la seconda volta
che si scrive «leggi le intestazioni, poi il corpo».

! **`chunked` NON È UN OPZIONALE.** Un server che non sa in anticipo quanto
sarà lunga la risposta — cioè qualunque pagina generata al momento — non manda
`Content-Length`: manda i pezzi. Senza saperli srotolare si vedrebbero i numeri
esadecimali della lunghezza in mezzo al testo.

! **L'HTML NON È XML**, ed è tutta la difficoltà: i tag restano aperti, si
chiudono nell'ordine sbagliato, ne mancano metà. `<ul><li>uno<li>due` sono due
fratelli e non una scala; `<b><i>x</b>` chiude fino alla `<b>`; dentro
`<script>` e `<style>` **non c'è markup**, o dal primo `a < b` del JavaScript in
poi l'albero è spazzatura.

! **`https://` FUNZIONA, E VERIFICA DAVVERO.** TLS 1.3 scritto qui dentro:
X25519 per lo scambio di chiavi, ChaCha20-Poly1305 per i dati, la catena dei
certificati controllata contro un magazzino di CA vere e il nome del sito
confrontato col `subjectAltName`. Le firme si verificano in RSA-PSS **e in
ECDSA su P-256 e P-384**, che e' cio' che serve per aprire i siti veri:
wikipedia.org, news.ycombinator.com e github.com hanno solo certificati
ellittici. Senza magazzino di CA non si apre niente: cifrare con chiunque
risponda vuol dire cifrare con chi sta in mezzo, e la barra scriverebbe
`https://` lo stesso.

! **I MODULI SI VEDONO, SI COMPILANO E SI MANDANO**: caselle di testo,
password, spunte, scelte con l'elenco a tendina, pulsanti e aree di testo si
disegnano come i controlli del sistema, prendono i tasti, e il pulsante manda
davvero — **in GET e in POST**, con la codifica percento.

! **E LA CONNESSIONE SI RIUSA.** Su `https` la stretta di mano e' tutto il
costo — chiave effimera, catena di certificati, firma — e una pagina con dieci
immagini la pagava dieci volte. Adesso si riusa la connessione quando si sa
dove finisce il corpo, si rispetta il `Connection: close` del server, e si
riprova una volta se l'altra parte l'ha chiusa senza dirlo.

! **E I COLORI SCRITTI NEGLI ATTRIBUTI CONTANO**: `bgcolor`, `text`, `align`.
Mezzo web li usa ancora — la barra arancione di Hacker News e' un `bgcolor` su
una `<table>` — e stanno al gradino piu' basso della cascata, sotto ogni regola
di stile.

! **E LE IMMAGINI SONO TRE FORMATI**: PNG, JPEG e GIF (primo fotogramma,
trasparenza compresa).

! **LE IMMAGINI ARRIVANO DOPO IL TESTO.** La pagina si impagina e si disegna con
le sole parole; solo allora si scarica un'immagine per volta, e a ognuna che
arriva si reimpagina. Prenderle prima vorrebbe dire una finestra vuota finché
l'ultima non risponde, e una che non risponde costa otto secondi da sé. Quella
che non arriva lascia il posto al suo `alt`, che è il motivo per cui
quell'attributo esiste.

! **E I PIXEL SONO DEL BROWSER, NON DEL DECODIFICATORE**: si decodifica, si
copia nella misura con cui si disegnerà, e il bitmap naturale si restituisce
subito. Tenerlo vorrebbe dire lasciar scegliere alla pagina quanta memoria
prendere — 128 KB di PNG possono essere 4000×3000 pixel, cioè 48 MB su una
macchina che ne ha 32. I tetti sono dichiarati: dodici immagini, 128 KB per
file, 512 K pixel in tutto.

! **I FOGLI DI STILE CI SONO, in `/exwin/lib/excss.so`**: `<style>`, `<link
rel=stylesheet>` e l'attributo `style=`, con la cascata vera (origine,
specificità, ordine). Selettori per tipo, classe, id, discendenza ed elenco;
colori, corpi, grassetto, corsivo, `display:none`, allineamento e margini.

! **E QUELLO CHE NON SI SA LEGGERE SI SCARTA, NON SI INDOVINA**: `div > p` non
diventa `div p`, un selettore troppo lungo si butta invece di essere accorciato
— accorciarlo lo renderebbe più **largo** dell'originale — e `2em` si rifiuta
invece di valere due pixel. Meno stile, mai stile sbagliato.

! **E I TAG DI ASPETTO SONO DIVENTATI REGOLE**: `h1`, `<b>`, `<i>`, `<strong>`,
`<em>` e il colore dei collegamenti stanno in un foglio predefinito con
l'origine più bassa della cascata, quindi una pagina può sovrascriverli. Prima
erano `if` nel motore, e `<b>` e `<i>` non c'erano affatto.

Quello che **non** c'è, dichiarato: JavaScript, `@media`, le unità relative,
`colspan`/`rowspan`.


### I font: TrueType, misurato contro FreeType

**testato** — sei facce Liberation a sei corpi dentro EX-OS, e il
rasterizzatore confrontato pixel per pixel con FreeType su 1460 glifi:
**riquadro identico 1460 su 1460**, differenza media 0,94 livelli su 255.

| | |
|---|---|
| `lib/exfont/exfont.c` | il formato bitmap EXFN, dentro `exwin.so` |
| `lib/exfont/ttf.c` | il contenitore TrueType |
| `lib/exfont/raster.c` | contorni → copertura, in interi 26.6 |
| `exfont.so` | istanza, cache dei glifi, aperta a richiesta |

! **`strlen(s) * 8` È IL CONTO DA TOGLIERE PRIMA CHE QUALCUNO NE SCRIVA UN
ALTRO SOPRA.** È vero solo col font di sistema: un motore d'impaginazione nato
su quel presupposto andrebbe riscritto il giorno che arriva un font
proporzionale. Per questo i font sono arrivati **prima** del browser.

! **L'ARITMETICA È INTERA.** Sul Pentium 133 dichiarato la virgola mobile è più
lenta, ma soprattutto ogni processo che tocca l'FPU paga un salvataggio dello
stato a ogni cambio — e disegnare testo è ciò che si fa di continuo.

! **NON ESISTE LA DIVISIONE A 64 BIT** in una libreria di EX-OS: si collega
senza libgcc, quindi `__divdi3` non c'è e il collegamento fallisce.

! **IL CONFRONTO CON FreeType HA TROVATO UN DIFETTO CHE NESSUNA IPOTESI AVREBBE
TROVATO.** Alla prima passata i riquadri identici erano il 95,4%, e i mancanti
avevano una forma: a 16 pixel quasi ogni maiuscola veniva alta un pixel meno.
La scala troncava invece di arrotondare — mezzo sessantaquattresimo perso che
diventa un pixel intero.

Cosa manca, dichiarato: hinting (per questo l'interfaccia usa ancora l'8x16),
crenatura, legature, solo il piano base, CFF rifiutato apposta.


### `rename` sostituisce la destinazione, come POSIX

**testato** su ext2, FAT12 e FAT16.

Fino alla 0.184 `rename()` rendeva `EEXIST` se la destinazione esisteva. Era una
scelta dichiarata, e non reggeva:

! **«CANCELLA PRIMA» NON È EQUIVALENTE.** Lo schema con cui si salva un file
senza rischiare di perderlo è uno solo: scrivi accanto, poi **scambia**. Se lo
scambio non sostituisce bisogna cancellare prima — e fra la cancellazione e lo
scambio **il file non esiste**.

! **E QUELLA FINESTRA SI CHIUDE SOLO NEL KERNEL.** `vfs_rename` tiene il
lucchetto del filesystem per tutta l'operazione: nessun altro processo vede lo
stato intermedio. In spazio utente quella garanzia non si può avere.

! **LA SOSTITUZIONE STA NEL VFS, PRIMA DELLO SMISTAMENTO**, quindi vale per
ext2, FAT12 e FAT16/32 senza che nessun driver la reimplementi. Regole POSIX sui
tipi: directory solo su directory vuota, file solo su file. E
`rename("x","x")` non fa niente — senza quel controllo la sostituzione
**distruggerebbe x**.


### La barra: l'ora, e le applicazioni che si aggiungono da sole

**testato** — l'orologio avanza da solo (06:52 → 06:53 in 75 secondi); il menu
«Applicazioni...» aggiunge, toglie e salva su ext2.

! **I THREAD IN EX-OS NON ESISTONO**, e l'orologio è un **processo** a parte. E
anche se ci fossero, qui un processo è meglio: un thread dentro il program
manager ne condividerebbe la coda dei messaggi, quindi un program manager
occupato sarebbe un orologio fermo.

! **`ex_sveglia()` ED `EXM_TEMPO`**: senza, un'applicazione non può fare niente
da sé — il ciclo dei messaggi dorme finché non arriva un evento. Non costa un
giro in più: il `poll` ha già una scadenza di 200 ms.

! **L'AVVIO AUTOMATICO È UNA DIRETTIVA, NON UN SEGNO SULLA VOCE.** Si può volere
un programma che parte da solo e **non** compare nel menu — un pannello, un
orologio — oppure una voce che non parte da sola.

! **`ipc_rimetti()`**: la mailbox è una sola e i consumatori sono più d'uno. Chi
aspetta una risposta dello stack IP scorre i messaggi e prima li **buttava** —
in un browser sono i clic dell'utente. Ora si rimettono a posto, e **alla
fine**: lo scaffale si serve prima della coda del kernel, quindi rimettere e
rileggere subito renderebbe lo stesso messaggio all'infinito.


### L'interfaccia grafica: un server a finestre in ring 3

| | |
|---|---|
| `/exwin/bin/wserver` compone le finestre e muove il puntatore, **in ring 3 e senza privilegi** | testato |
| Toolkit **ExWin** in stile Win32, con header per **C, C++ e FreeBASIC** | testato |
| Controlli: finestra, pulsante, etichetta, casella di testo, riquadro, separatore, intestazione, terminale | testato |
| `exwin` accende la grafica su una **console sua**: con Alt+F2 ci si va, con Alt+F1 si torna alla shell | testato |
| Una **shell dentro una finestra**, su due pipe | testato |
| Sfondo da immagine: oggi BMP, e la tabella dei lettori è già quella giusta per JPG, PNG e ICO | testato |

! **GIRA IN RING 3, ED È IL PUNTO.** Quando il server muore, muore lui: kernel,
scheduler, console seriale e tastiera restano vivi, e lo schermo si rimette con
`/bin/testo` — che si digita alla cieca ed è la rete di sicurezza costruita
apposta *prima* di scrivere il server.

! **NON DISEGNA IL CONTENUTO DELLE FINESTRE, LO COMPONE.** Ogni finestra è una
zona di memoria condivisa che il client riempie di pixel; il server ci mette
bordo e barra del titolo, le impila e le copia nel framebuffer. Un client che
sbaglia a disegnare rovina la propria finestra, non lo schermo. Ed è anche
perché i decodificatori di immagini stanno nella libreria del client: un
lettore JPG dentro il server sarebbe un difetto di **tutte** le applicazioni
insieme.

! **I CLIENT DISEGNANO SEMPRE IN ARGB A 32 BIT** e non sanno com'è fatto lo
schermo. La conversione a 16, 24 o 32 bit sta in un posto solo. Un toolkit che
dovesse conoscere il formato dello schermo avrebbe sei strade da provare invece
di una.

### La scrivania: `/exwin`, barra delle applicazioni e menu di avvio

| | |
|---|---|
| `pm` — scrivania, barra in basso, pulsante **Avvio**, menu con le applicazioni | testato |
| Voci **Esci** (torna alla shell) e **Spegni** nel menu | testato |
| `/exwin/bin`, `/exwin/lib`, `/exwin/dev` — le applicazioni grafiche **non stanno in `/bin`** | testato |
| L'elenco delle applicazioni è un **file di testo**, `/exwin/lib/applicazioni.txt` | testato |
| `filemgr` — file manager: elenco che scorre, directory in cima, apre i file con l'editor | testato |
| `edit` — editor di testo: frecce, Home/End, PgSu/PgGiù, Canc, clic del mouse, Ctrl+S, Ctrl+Q | testato |

! **LE APPLICAZIONI GRAFICHE NON STANNO IN `/bin`, ED È UNA DECISIONE.** I
programmi di `/bin` si lanciano da una shell e parlano con un terminale; questi
vogliono il server a finestre, e lanciati senza non fanno niente. Mescolarli
vorrebbe dire un `ls /bin` in cui metà dei nomi non si può usare lì dove si sta
guardando. Stessa ragione per cui i driver stanno in `/dev`.

! **L'ELENCO È UN FILE, NON UNA TABELLA COMPILATA.** Aggiungere
un'applicazione è una riga. Un elenco dentro il binario vorrebbe dire rifare il
program manager per ogni applicazione nuova, e **chi installa un programma non
ha i sorgenti**.

! **UN FILE PIÙ GRANDE DEI LIMITI L'EDITOR LO CARICA IN PARTE E BLOCCA IL
SALVATAGGIO.** Salvare quello che si è letto vorrebbe dire cancellare il resto
del file senza averlo mai mostrato: è il modo più silenzioso che un editor
abbia di distruggere dei dati. Il limite — 512 righe, 200 colonne — è una
conseguenza dell'allocatore a bump, dove `free()` non restituisce niente.

### Le librerie condivise: `exwin.so`, `exdlg.so`, `libc.so`

| | |
|---|---|
| `SYS_LIB_APRI` (248): il kernel mappa una libreria **una volta** e la aggancia a chi la chiede | testato |
| `.text`/`.rodata` **condivise** fra i processi; `.data`/`.bss` copia privata | testato |
| Risoluzione **per nome**, non per posizione: si aggiorna la libreria senza ricompilare le applicazioni | testato |
| `/lib/libc.so` — **322 funzioni**, usata da 39 programmi | testato |
| `/exwin/lib/exwin.so` — il toolkit; `/exwin/lib/exdlg.so` — i dialoghi Apri/Salva | testato |
| Gli header non cambiano di una riga, e nemmeno il sorgente delle applicazioni | testato |

```
    0x04000000 - 0x08000000   le librerie, una fetta da 1 MB a testa (64 in tutto)
    0x08000000               i programmi
```

La mappa di chi occupa cosa **non e' scritta da nessuna parte**: la si legge
dai file, perche' scritta a mano ha sbagliato tre volte.

```
    python3 tools/fette.py            la mappa vera, e dice se qualcosa si tocca
    python3 tools/fette.py --libera   il prossimo indirizzo libero
```

Il risparmio, misurato:

| | prima | dopo |
|---|---|---|
| `/bin` in tutto | 850.132 | **608.468** |
| ISO di EX-OS | 4728 KB | **4252 KB** |
| `libctest` (testo) | 64.861 | **33.312** |
| applicazioni grafiche | ~37.000 | **~15.000** |

! **NON È COLLEGAMENTO DINAMICO VERO, ED È DELIBERATO.** Un `.so` con `ld.so`,
codice PIC, GOT e PLT è la strada standard, ed è mesi di lavoro: caricatore ELF
da riscrivere, un linker dinamico da scrivere, la libc ricompilata PIC. E ogni
difetto lì dentro sarebbe un difetto di **tutte** le applicazioni insieme.

Qui la libreria è un ELF normalissimo — `ET_EXEC`, non PIC — collegato a un
indirizzo **riservato**: sta sempre lì, quindi non c'è niente da rilocare e non
serve nessuna GOT. Ciò che serviva davvero — aggiornare la libreria senza
ricompilare le applicazioni — si ottiene con la risoluzione per nome.

! **L'ORDINE DELLA TABELLA NON È PARTE DELL'ABI.** Con una tabella posizionale,
riordinare le voci romperebbe ogni applicazione già compilata e nessun errore
lo direbbe: si chiamerebbe semplicemente la funzione sbagliata. Coi nomi si può
aggiungere, riordinare e riscrivere il corpo di qualunque funzione. Solo
**togliere** un nome rompe — ed è esattamente il patto di una DLL.

! **LE FUNZIONI SI PASSANO SENZA CONOSCERNE LA FIRMA.** Sono 322: scriverne i
ponti in C vorrebbe dire copiare 322 firme, ognuna sbagliabile in silenzio. Un
ponte in assembly è un `jmp` indiretto, che non tocca né gli argomenti né il
valore di ritorno — `printf: ff 25 c4 25 00 08  jmp *0x80025c4`. È lo stesso
mestiere di una PLT, e li genera `tools/genlibc.py` leggendo i simboli veri con
`nm`.

! **LE CINQUE VARIABILI GLOBALI NO.** `errno`, `stdin`, `stdout`, `stderr`,
`environ`: un programma che scrive `errno = 0` scriverebbe nella **propria**
copia mentre la libc legge la sua — due variabili con lo stesso nome, e nessun
errore da nessuna parte. Si esporta l'indirizzo e l'header lo trasforma in una
lettura: `#define errno (*__errno_dove())`. Il sorgente di chi le usa non
cambia.

! **E L'AVVIO NON SI PUÒ CONDIVIDERE.** `_libc_start` tocca `main`,
`__init_array_*` e `__fini_array_*`, che appartengono al binario in cui si
trovano. Dentro la libreria `main` non esisterebbe nemmeno, e i vettori
sarebbero quelli della libreria — vuoti: i costruttori globali del programma
non girerebbero mai, **e nessuno lo direbbe**.

! **`login` E `install` RESTANO STATICI**, ed è una decisione: sono i due
programmi con cui si entra e con cui si ripara. Se `libc.so` mancasse o fosse
rotta, un login collegato a lei renderebbe il sistema inaccessibile e non ci
sarebbe modo di rimediare dall'interno.

### L'installazione a componenti

| | |
|---|---|
| `install` mostra i componenti trovati sul supporto e li chiede **uno per uno** | testato |
| `install -m` sistema minimale, `install -t` tutto: nessuna domanda | testato |
| `copia_albero()` segue le sottodirectory: un componente non è fatto di un livello solo | testato |

! **IL SISTEMA MINIMALE È UN ELENCO CHIUSO; TUTTO IL RESTO È OPZIONALE.**
`bin`, `boot`, `lib`, `dev`, `drivers` sono ciò senza cui EX-OS non parte.
Qualunque **altra** directory nella radice del supporto è un componente, e
l'installatore la trova da solo.

È il contrario di un elenco scritto dentro l'installatore: aggiungere un
pacchetto — oggi `/exwin`, domani quello che sarà — vuol dire **metterne la
directory sul supporto, e basta**. Chi prepara un pacchetto non ha i sorgenti
dell'installatore.

! **LA SCELTA SI FA PRIMA DI SCRIVERE.** Chiedere «installo anche /exwin?» dopo
aver già sostituito il kernel vorrebbe dire che rispondere «annulla» non
annulla più niente.

### Quattro difetti vecchi, e cosa hanno in comune

| | |
|---|---|
| Il fuoco della tastiera non era il fuoco: la barra si prendeva ogni tasto | corretto |
| La shell aveva perso redirezioni e ambiente per tre giorni, in silenzio | corretto |
| `wserver.drv` non conosceva `-i`: la sonda lo **avviava** e l'installazione dal CD si fermava | corretto |
| `cat` senza argomenti non leggeva `stdin`, quindi in una pipe non serviva a niente | corretto |

! **IL FUOCO NON ERA IL FUOCO.** Il server mandava il tasto alla finestra
disegnata per ultima. Vero finché l'ordine di disegno dipendeva solo da chi si
era portato davanti; falso da quando esiste `WIN_ST_SOPRA`, che tiene la barra
delle applicazioni sempre in cima. Da quel giorno **nessuna finestra poteva
ricevere un tasto** col program manager acceso. Il difetto non era nell'editor
che sembrava sordo, né in `WIN_ST_SOPRA`: era in una funzione che decideva
**due** cose mentre il suo nome ne prometteva una.

! **UNA MAGIA PROTEGGE DAL DANNO, NON DALLO SFASAMENTO.** Di `SpawnExtra` — la
struttura che attraversa la syscall `spawn` — c'erano **quattro copie**. Tre
sono state aggiornate, la quarta (quella della shell) no. Il kernel ha fatto
esattamente ciò per cui la magia esiste: non ha riconosciuto il blocco e l'ha
**ignorato** invece di leggerlo storto. Ma «ignorato» vuol dire che per tre
giorni `hello > file` stampava a video e lasciava il file a zero byte, **senza
un messaggio**. Un silenzio non si nota.

La correzione non è aggiornare la quarta copia: è **non averne quattro**.
`lib/include/spawn_abi.h` è la definizione unica, e in fondo ha
`typedef char spawn_abi_misura_invariata[(sizeof(SpawnExtra) == 596) ? 1 : -1];`
— un campo aggiunto senza cambiare la magia adesso ferma la **compilazione**.

! **E SEI VOLTE «UN'USCITA CHE NON SA DI ESSERE SCADUTA».** Dipendenze finte
del bersaglio floppy, `uhci.drv` mancante fra quelle dell'ISO, la risoluzione
SVGA non prerequisito di Stage 2, le immagini che non dipendevano dal
`Makefile`, e due volte una variabile del `Makefile` usata come prerequisito
**prima** di essere definita — dove `make` la espande a stringa vuota. La
seconda ha fatto credere che 294 prove girassero sulla libc condivisa mentre
giravano su quella statica. **Un prerequisito scritto con una variabile vuota
non è un prerequisito debole: non esiste, e nessuno lo dice.**

### Driver: li sceglie la macchina, non li copia in blocco

| | |
|---|---|
| Convenzione `-i` comune a tutti i driver: sonda, riferisce, esce 0 se serve qui | testato |
| `-i` **non tocca la periferica**: legge dal bus PCI e basta | testato |
| Catalogo `/drivers` sul CD di EX-OS, separato dal `/dev` che serve ad avviarlo | testato |
| `hwconfig -d <punto>`: sonda il catalogo e installa in `<punto>/dev` solo chi risponde | testato |
| `install` chiama la selezione invece di riversare `/dev` sul disco | testato |
| Prova incrociata su NE2000 e su AMD PCnet: ognuna installa solo il proprio driver | testato |

Un disco installato si ritrova in `/dev` i driver che su **quella** macchina
funzionano. Prima ci finiva tutto ciò che stava sul CD: il driver della scheda
di rete che la macchina non ha, e `floppy.drv` — che è un modulo ET_DYN e che
`spawn()` rifiuta, cioè un file che nessuno poteva caricare, installato a ogni
installazione.

Non esiste un elenco di driver da nessuna parte, ed è deliberato: un elenco
sarebbe una seconda verità accanto al contenuto della directory, e le due
divergono al primo driver aggiunto o tolto. La domanda si fa al driver, che è
l'unico a sapersi rispondere.

! Il punto in cui è più facile sbagliare, e su cui la prima versione ha
sbagliato: `-i` girava su un sistema **acceso**, dove l'autoexec ha già avviato
il driver giusto. Inizializzando la scheda per sondarla la si resettava sotto a
chi la stava guidando, e il reset di una scheda occupata risponde con uno stato
inatteso — così un driver dichiarava di non servire sulla macchina la cui
scheda stava guidando in quel momento. Da qui la regola: si guarda, non si
tocca.

### Una shell più grande: modalità grafica VESA e `svga.drv`

| | |
|---|---|
| 640×480 → **80×30**, 800×600 → **100×37**, 1024×768 → **128×48** caratteri | testato |
| `/dev/svga.drv <modo>` sceglie la risoluzione, come `keymap` la disposizione | testato |
| È un driver a tutti gli effetti: `hwconfig -d` lo installa da solo sul disco | testato |
| Dice **forte** che serve un riavvio: la modalità la imposta Stage 2 | testato |
| Predefinito: console di testo 80×25, sistema identico a prima | testato |
| Ogni fallimento del sondaggio VBE ripiega sul testo, mai su uno schermo nero | testato |
| Il kernel segnala se `kernel.cfg` e il bootloader non dicono la stessa cosa | testato |

! **Non è un driver, e non può esserlo.** Una modalità VESA si imposta con
INT 10h — cioè con il BIOS, cioè in **modo reale**. Quando il kernel comincia
a girare quella porta è già chiusa, e in ring3 non lo è mai stata. L'unico che
può farlo è **Stage 2**, prima del passaggio a modo protetto, ed è lì che il
sondaggio VBE vive: `bootloader/stage2/loader.asm`.

Il kernel riceve indirizzo, pitch, dimensioni e profondità in `BootInfo` e
disegna la console nel framebuffer con il font 8×16 di
`kernel/arch/x86/font8x16.c`. Il resto del file `vga.c` non se n'è accorto:
tutta la console — scorrimento, parser ANSI, quattro console virtuali,
cancellazioni — lavora sul proprio array di celle, e sono `riversa_cella()`,
`riversa_tutto()` e il cursore a sapere dove finiscono davvero.

**Come arriva la scelta fino al bootloader.** Stage 2 non ha un filesystem:
riceve da Stage 1 una mappa di settori già pronta per il kernel, e da disco
rigido nemmeno quella. Non può leggere `kernel.cfg`. Quindi `svga.drv` scrive in
due posti — la voce in `kernel.cfg`, che è la configurazione, e un byte
marcato dalla firma `SVGAMODE` dentro l'immagine di Stage 2, che il bootloader
legge da sé stesso. Non sono due verità: il secondo lo scrive solo `svga.drv`, e
il kernel li **confronta a ogni avvio** dicendolo se divergono. È lo stesso
patto della mappa di settori che `install` scrive nel settore di avvio.

Tre difetti veri incontrati costruendolo, tutti e tre silenziosi:

- **Stage 2 non ci stava.** Il limite era 1280 byte — la GDT stava a `0x0A00` —
  e ne usava già 1095. La GDT è stata spostata a `0xE400`: la conosce solo
  quel file, che la scrive e la carica, ed era il rimedio che il commento sul
  limite indicava già.
- **Page fault a `0xfd00c000`.** Il framebuffer era mappato nella sola page
  directory del kernel, ma la console scrive anche mentre gira un processo, e
  in quel momento `CR3` è la sua.
- **`PMM: pagina fuori range` a ogni processo che finiva.**
  `paging_destroy_directory` trattava le entry del framebuffer come pagine
  utente e le restituiva al PMM — che non sono RAM. Peggio: liberava la page
  table condivisa, quindi il primo processo a morire avrebbe lasciato senza
  schermo tutti gli altri.

E uno di prestazioni: la prima versione ridisegnava 3700 celle a ogni
scorrimento — mezzo milione di scritture in memoria video, che non passa dalla
cache — e il sistema arrivava al prompt in decine di secondi. Ora fa scorrere
il framebuffer con una copia sola e ridipinge la sola ultima riga.

**Tre difetti chiusi mentre lo si provava sul disco installato**, che è il
posto dove `svga.drv` serve davvero — dal CD non si può scrivere comunque:

- `svga.drv` cercava `/boot/stage2`, ma `install` scrive **`/boot/stage2.bin`**:
  rispondeva «non trovo l'immagine di Stage 2» esattamente sulla macchina in
  cui doveva funzionare.
- Il salvataggio si chiamava `kernel.cfg.bak` — due punti, nome 8.3 non
  valido — quindi su FAT falliva con un errore che parla di nomi mentre
  l'utente stava cambiando la risoluzione. Ora sostituisce l'estensione:
  `kernel.bak`.
- `hwconfig` riconosceva i driver confrontando `.drv` **distinguendo le
  maiuscole**: su ISO 9660 con Joliet i nomi sono minuscoli, su FAT sono
  `KBD.DRV`. Tutte le prove precedenti erano sul CD, quindi non si vedeva.

E una regressione introdotta dalla sonda dei driver stessa: **installando dal
floppy non veniva installato più nessun driver**, `kbd.drv` compreso, perché
senza CD non esiste un catalogo — e il sistema installato ripiegava
sull'handler IRQ1 dentro il kernel senza che niente lo spiegasse. Ora `/dev`
del supporto in esecuzione è l'ultima voce del catalogo: è un catalogo
legittimo, e i suoi driver vengono sondati come tutti gli altri.

### Due immagini, e niente resta fuori

| | |
|---|---|
| `make iso-tutte`: costruisce `dist/exos.iso` e `dist/exos-tools.iso` | testato |
| Ogni programma dichiara la propria destinazione in `PROGRAMMI_FLOPPY` o `PROGRAMMI_CD` | testato |
| `make verifica-programmi`: si ferma se un sorgente di `bin/` o `drivers/` resta fuori | testato |
| Il cross `i386-exos` se lo mette nel PATH il Makefile, non chi lancia `make` | testato |
| `bin/xcp` e `pcnet.drv`, che erano rimasti fuori, ora ci sono | testato |

Le due immagini rispondono a due domande diverse:

- **`dist/exos.iso`** — il **sistema**: kernel, comandi di base, rete (`ping`,
  `ftp`, `telnet`, `dhcp`, `host`, `netdetect`…) e tutti i driver. È
  avviabile, ed è un **superinsieme del floppy**, non un'altra cosa.
- **`dist/exos-tools.iso`** — i **linguaggi**: `gcc`, `g++`, `cpp`, `cc1`,
  `fbc`, `as`, `ld`, libstdc++, OpenSSL. 150 MB che si installano a parte con
  `toolinst`.
- **`dist/floppy.img`** — solo il **sistema di base**: avviarsi, preparare un
  disco, installarsi, leggere e scrivere file. Quello che sta in 1.44 MB.

! **Un sorgente che non è in nessuna lista non viene compilato e non finisce
su nessuna immagine, e nessuno se ne accorge.** È successo per mesi con
`bin/xcp/`: il sorgente c'era, la regola no, e il comando semplicemente non
esisteva sulla macchina. `pcnet.drv` aveva il problema opposto e altrettanto
silenzioso — una regola c'era, ma nessuna lista la nominava: si costruiva solo
di rimbalzo, perché la ricetta della ISO lo citava fra le proprie prerequisite,
e `make all` non lo faceva.

`make verifica-programmi` chiude entrambi i casi, e lo fa **guardando il
risultato invece di leggere il Makefile**: per ogni `bin/<nome>/` ci dev'essere
un `build/bin/<nome>` o un `build/bin-cd/<nome>`, per ogni `drivers/<nome>/` un
`.drv` — salvo le due eccezioni dichiarate (`net`, che sono solo header di
protocollo, e `tty`, che è compilato dentro il kernel). Le due ISO lo eseguono
da sole prima di costruirsi, come prerequisito d'ordine: se manca qualcosa la
costruzione si ferma e dice quale nome aggiungere e a quale lista.

### Gli strumenti si installano da soli: `toolinst`

| | |
|---|---|
| `toolinst [radice]`: copia l'albero `/exos` del CD tools sul disco | testato |
| Sceglie per linguaggio: C obbligatorio, C++, FreeBASIC e OpenSSL a richiesta | testato |
| Aggiunge `/exos/bin` alla voce `PATH` di `[env]` in `kernel.cfg` del bersaglio | testato |
| `-n` conta file e byte senza scrivere niente; `-p` cambia il prefisso | testato |
| Si ferma se il volume è in sola lettura o non è ext2 | testato |

! **Copiare i binari in `/bin` e aggiungere una voce al PATH non funziona**, e
questa è la cosa da sapere prima di ogni altra. Sia il driver di GCC sia `fbc`
calcolano dove stanno le proprie cose da **dove sta il loro binario** —
`<la mia directory>/..` — non dal PATH. Si legge nella traccia di `fbc -v`:

```
assembling: /cdrom/exos/bin/../bin/as --32 ...
            /cdrom/exos/bin/../lib/gcc/i386-exos/17.0.0/libgcc.a
```

Da cui:

```
/cdrom/bin/gcc -c prova.c        ->  cannot execute 'cc1'
/cdrom/exos/bin/gcc -c prova.c   ->  compila
```

Stesso binario, stesso PATH, stessa riga di comando: cambia solo da dove è
lanciato. Da `/bin/gcc` il prefisso diventa `/` e si cercano `/libexec/gcc/…`,
`/lib/gcc/…`, `/include/freebasic`, che non esistono — e il messaggio che ne
esce parla di header mancanti mentre il difetto è nel percorso. Perciò
`toolinst` copia **l'albero intero conservandone la forma** e mette nel PATH la
sua `bin`.

! **Il disco bersaglio dev'essere ext2.** Su FAT la scrittura dei nomi lunghi
non c'è ancora, e l'albero è pieno di nomi che l'8.3 non regge
(`bits/stdc++.h`, `libstdc++.a`): la copia riuscirebbe e il compilatore non
troverebbe più i propri header, che sul disco ci sono con un altro nome.
`toolinst` guarda il filesystem del montaggio e lo dice prima di cominciare,
invece di lasciarlo scoprire a 50 MB di distanza.

I gruppi opzionali sono definiti dai percorsi da **saltare**, non da quelli da
copiare: un file nuovo sul CD finisce sul disco insieme al resto, e l'unico
modo di perderlo è averlo scritto nell'elenco delle esclusioni.

### Percorsi e messaggi: se ne dice uno solo, e vero

| | |
|---|---|
| Un comando inesistente stampa **un** messaggio, non uno per voce del `PATH` | testato |
| `./mioprog` e `sotto/mioprog` si eseguono: il `PATH` vale solo per i nomi nudi | testato |
| Un file che c'è e non è un ELF lo dice, invece di farsi passare per assente | testato |
| FAT12 rifiuta i percorsi più profondi di un livello invece di appiattirli | testato |
| `cp`: `-y`, conferma per file, `t` = tutti, avanzamento durante `-r` | testato |

Erano quattro modi diversi di dare una risposta sbagliata con l'aria di darne
una giusta.

**Sei righe rosse per un comando battuto male.** La shell sondava il `PATH`
chiamando `spawn()` su ogni voce e usando il fallimento come risposta; il
kernel segnalava «file non trovato» come `LOG_ERROR`, che si stampa sempre
qualunque sia `loglevel`. Ora la shell chiede *«c'è?»* con una `open` — come
faceva già `spawn_cerca_path()` nella libc — e nel kernel un file assente non è
più un errore: è la risposta a una domanda, e chi l'ha posta la riferisce.

**`./mioprog` non partiva.** La scelta fra `PATH` e percorso diretto si faceva
su `prog[0] != '/'`. La regola giusta è quella di `execvp`: si cerca nel `PATH`
solo un nome **senza barre**. Con il controllo sul primo carattere `./mioprog`
diventava `/bin/./mioprog` — e quando in `/bin` c'era un omonimo partiva
**quell'altro**, senza che niente lo segnalasse.

**`cat /bin/prove/t.txt` leggeva `/prove/t.txt`.** FAT12 costruiva la parte
directory del percorso e poi ne teneva solo l'ultima componente, cercandola
nella radice: qualunque prefisso inventato funzionava purché finisse col nome
di una directory vera. Lo stesso vizio, in una variante diversa, faceva
rispondere a `stat("/bin/hello")` con il `/hello` della radice — un altro file,
di un'altra dimensione. Il driver resta a un livello solo, come prima; la
differenza è che ora un percorso che non sa rappresentare dà «non trovato»
invece di un file diverso da quello chiesto.

**`cp` non sovrascriveva.** Un file già presente faceva fallire la copia, e su
un albero ricorsivo bastava un file in comune per costringere a rifare tutto a
mano. Ora chiede — `s`, `n`, oppure `t` per tutti i prossimi — e `-y` risponde
di sì a tutte in anticipo. Un file saltato non conta fra gli errori: la copia
ha fatto quello che le è stato detto. Con `-r` ogni file viene stampato mentre
lo si copia, perché su un floppy un albero di qualche decina di file sono
minuti in cui prima non compariva niente, e un programma muto e un programma
bloccato si somigliano troppo.

### Rete — dal bus PCI a un client FTP

| | |
|---|---|
| Enumerazione PCI in userspace (`/dev/pci.drv`, `netdetect`) | testato |
| Driver NE2000 in ring3 (`/dev/ne2k.drv`, `nettest`) | testato |
| ARP, IPv4, ICMP — `ping` | testato |
| UDP | testato |
| Client DHCP (`dhcp`) | testato |
| Risolutore DNS in libc, record A (`host`) | testato |
| TCP: apertura attiva, invio, ricezione, chiusura (`tcptest`) | testato |
| Client FTP passivo, `get`/`put`/`ls`/`cd` (`ftp`) | testato |
| Client Telnet interattivo con negoziazione delle opzioni (`telnet`) | testato |
| Configurazione a mano e tabella ARP (`ipcfg`, `ipcfg -r`) | testato |
| Rinnovo della concessione DHCP | da fare |
| Riordino dei segmenti TCP fuori sequenza | da fare |
| Driver PCnet (Am79C970/C973), bus master con DMA vero | testato |
| `SYS_DMA_ALLOC`: memoria contigua per un bus master | testato |

Ogni comando di rete, quando qualcosa manca, stampa **la catena completa** e
**il prossimo comando da dare** invece del solo messaggio d'errore.

### CPU — SSE, SSE2, SSE3, MMX

| | |
|---|---|
| Rilevamento capacità via CPUID, con la prova del bit ID in EFLAGS | testato |
| Salvataggio dello stato: FXSAVE dove c'è, FNSAVE sulle CPU vecchie | testato |
| Attivazione di CR4.OSFXSR / OSXMMEXCPT quando SSE è presente | testato |
| MMX: nessun lavoro necessario, MM0-MM7 sono alias di ST0-ST7 | testato |
| Esecuzione su 486 e Pentium MMX veri | da testare |

Il percorso FNSAVE è quello che permette al kernel di girare su CPU senza
SSE; è stato provato forzando la via lenta in QEMU, non su un 486 fisico.

### Filesystem

| | |
|---|---|
| Avvio da CD: FAT12 non viene più sondata sul CD-ROM | testato |
| Nomi lunghi VFAT in **lettura** su FAT16 e FAT32 | testato |
| Data e ora reali dei file su FAT, ext2 e ISO 9660 | testato |
| Timbratura di data e ora sui file creati su floppy | testato |
| `mkfs` sceglie da solo FAT16 sotto i 2 GB, FAT32 sopra | testato |
| Nomi lunghi VFAT in **scrittura** | da fare |

### Shell e comandi

| | |
|---|---|
| Cronologia comandi con le frecce su/giù | testato |
| `/boot/autoexec.sh` eseguito da `/bin/sh` all'avvio | testato |
| `ls`: `-h`, `-a`, `-d`, `-mc`, `-md`, `-p` | testato |
| `install -a`: elenca i file cambiati e propone l'aggiornamento | testato |
| `hwconfig`: analizza la macchina e scrive kernel.cfg e autoexec.sh | testato |
| Disposizioni di tastiera: `us it fr de es uk`, con AltGr | `us`/`it` testate |
| Argomenti con spazi fra virgolette (`cp "il mio file.txt"`) | testato |
| `help helpconfig`: come si accendono i driver, con lo stato attuale | testato |
| Backspace che non cancella più il prompt né lascia caratteri invisibili | testato |
| `!silenced` negli script: nasconde i comandi, non il loro risultato | testato |
| `source file.sh`, e i `.sh` eseguibili per nome | testato |

### libc

| | |
|---|---|
| `printf` con `%f`, `%e`, `%g`: 18 cifre significative, arrotondamento pari | testato |
| Costruttori globali `.init_array` e distruttori `.fini_array` | testato |
| `realloc` che ingrandisce sul posto — prima non ingrandiva mai | testato |
| `gettimeofday` monotòno, ancorato una volta sola all'orologio | testato |
| `time_t` a 64 bit | testato |
| I file temporanei seguono `TMPDIR`, non più solo la radice | testato |
| Cache del disco da 128 settori: `cc1` da 19,61 s a 10,19 s | misurato |
| 276 prove automatiche in `libctest` | testato |

### Catena di compilazione

| | |
|---|---|
| `as` e `ld` (binutils 2.44) nativi | testato |
| `cc1`: compila C e produce assembly dentro EX-OS | testato |
| Runtime del bersaglio sul CD (`crt0.o`, `libc.a`, `libgcc.a`) | testato |
| `as` + `ld` collegano un programma C vero con gli archivi | testato |
| **`gcc` come programma di guida, che concatena cc1 → as → collect2 → ld** | **testato** |
| **`gcc` trova gli header di sistema da solo, senza `-I`** | **testato** |
| **`g++`: la stessa catena in C++, con libstdc++ ed eccezioni** | **testato** |
| FreeBASIC: `fbc` trova i propri `.bi` da solo | testato |
| FreeBASIC: link finale (fbc emette opzioni Linux) | da fare a mano |
| TLS/SSL come libreria userspace (porting OpenSSL) | da fare |

---

## Struttura floppy

```
/
├── LOADER.BIN       ← Stage 2: trova e carica il kernel via FAT12
├── KERNEL.BIN       ← EX-OS Kernel
├── boot/
│   └── kernel.cfg   ← Configurazione: env, shell, moduli
├── bin/
│   ├── sh           ← Shell (ELF statico, primo processo)
│   ├── login        ← L'accesso (ELF statico: è il programma con cui si entra)
│   ├── sudo         ← Esegue un comando come root, se ne hai il diritto
│   ├── ls           ← Elenco directory
│   ├── hello        ← Programma di esempio
│   ├── textline     ← Editor di testo lineare (stile edlin)
│   ├── gfedit       ← Editor a schermo intero (stile MS-DOS EDIT)
│   ├── mkdir        ← Crea directory
│   ├── rmdir        ← Cancella directory vuote
│   ├── delete       ← Cancella file (con jolly ? e *)
│   ├── rename       ← Cambia il nome di un file (non sposta: vedi sotto)
│   └── chkdsk       ← Controlla e ripara un volume FAT12/16/32
├── lib/             ← Shared libraries (Fase 4b)
└── dev/
    ├── kbd.drv      ← Driver tastiera PS/2 (processo ring3)
    └── floppy.drv   ← Driver floppy controller (ancora ET_DYN, non caricato)
```

Il TTY non compare in `/dev`: `drivers/tty/tty.c` è compilato **dentro** il
kernel (possiede la VGA), e per l'input fa da client del servizio `kbd`.

### Cosa va sul floppy, e cosa no

Il floppy porta **il sistema**: avviarsi, preparare un disco, installarsi,
leggere e scrivere file. Partizionatore (`fdisk`), formattatore (`mkfs`),
controllore (`chkdsk`), montaggio, installatore, editor; il driver del
floppy e quello della tastiera, che servono a partire.

! **I driver aggiuntivi non ci vanno.** Rete (`pci`, `ne2k`, `pcnet`,
`ip`) e tutto ciò che verrà dopo stanno sul **CD di EX-OS**. Non è una
preferenza: in 1.44 MB non ci stanno, e il modo in cui non ci stanno è il
peggiore — `mcopy` fallisce a metà dell'elenco, l'immagine resta priva di
qualche file scelto dall'ordine alfabetico, e il sistema si avvia fino al
punto in cui gli serve quello che manca.

Il CD-ROM **non ha un driver in `/dev`**: ATAPI e ISO 9660 stanno *dentro*
il kernel, perché il kernel deve poterci montare la radice prima che
esista un processo che possa servirla.

```
                    floppy    CD di EX-OS    CD strumenti
sistema e shell       si          si              -
fdisk, mkfs, chkdsk   si          si              -
kbd.drv, floppy.drv   si          si              -
driver di rete        NO          si              -
ping, ftp, dhcp…      NO          si              -
as, ld, cc1           NO          NO             si
```

`make verify` **controlla la regola** invece di fidarsi, e dice quanto
spazio resta sul floppy — il numero che avvisa prima che l'immagine
smetta di contenere tutto:

```
[OK] nessun driver da CD sul floppy
                            629 760 bytes free
```

Quello che in 1.44 MB non entra sta sul **CD degli strumenti**
(`make iso`): `as`, `ld` e `cc1` nativi in `/bin`, gli header e il sorgente
della libc in `/exos`, il runtime del bersaglio in `/exos/lib`, la
documentazione in `/doc`.

---

## Prerequisiti (Debian 12)

```bash
# 1. Installa il cross-compiler (tutto automatico, ~30 min):
chmod +x tools/install_crosscompiler.sh
./tools/install_crosscompiler.sh

# 2. Attiva nella sessione corrente:
export PATH="$HOME/opt/cross/bin:$PATH"

# 3. Verifica:
i686-elf-gcc --version   # deve stampare: i686-elf-gcc 13.2.0 ...
nasm --version           # nasm version 2.x
mformat --version        # Mtools version ...
```

Lo script `install_crosscompiler.sh` installa automaticamente:
- Dipendenze Debian (`build-essential`, `nasm`, `mtools`, `qemu-system-i386`, ecc.)
- `binutils 2.41` cross-compilato per target `i686-elf`
- `GCC 13.2.0` cross-compilato per target `i686-elf` (solo linguaggio C)
- Aggiunge `~/opt/cross/bin` al `~/.bashrc`

---

## Build e test

```bash
make all          # Compila tutto + crea dist/floppy.img
make run          # Avvia con QEMU (32MB RAM)
make iso          # CD degli strumenti (as, ld, header, doc)
make run-iso      # QEMU con il CD montato su /cdrom
make hd           # Disco rigido avviabile (formattato da EX-OS stesso)
make run-hd       # QEMU dal disco, senza floppy
make debug        # QEMU + GDB stub porta 1234
make verify       # Verifica struttura floppy
make clean        # Rimuove build/
make distclean    # Rimuove build/ e dist/
```

`make iso` include `as` e `ld` nativi se li trova in
`$(BINUTILS_NATIVI)` (default `~/exos-native/build-nativi`); se non ci
sono lo dice e fa il CD lo stesso. Come costruirli:
`tools/binutils-exos/leggimi.md`.

### Log di boot completo via seriale

Il kernel specchia su COM1 (38400 8N1) tutto l'output che passa da
`vga_putchar()`: `kprintf`, `klog`, eco tastiera e output dei processi. Serve
perché lo schermo VGA è 80x25 e i messaggi di boot scorrono via.

```bash
qemu-system-i386 -drive file=dist/floppy.img,format=raw,if=floppy \
  -m 32M -boot a -display none -serial file:/tmp/serial.txt -no-reboot
```

### Scrivere l'immagine su un floppy fisico (da WSL)

Tre modi, stesso motore:

```bash
# da WSL
./tools/write_floppy.sh              # dist/floppy.img su A:
./tools/write_floppy.sh -d B: -y
```

```powershell
# da PowerShell
.\tools\write_floppy.ps1
.\tools\write_floppy.ps1 -Drive B: -Yes
```

```bat
REM da cmd.exe, o doppio clic da Esplora risorse
tools\write-floppy.cmd
tools\write-floppy.cmd B:
```

Non serve una console come Amministratore: lo script si rieleva da solo (UAC)
in una finestra che resta aperta per mostrare l'esito.

WSL2 non vede i dischi fisici di Windows, quindi `dd` non serve: si passa da
PowerShell, che blocca e smonta il volume, scrive il volume grezzo e
**rilegge per verificare** byte per byte. Rifiuta le unità non rimovibili
salvo `-Force`.

> ! **Floppy USB**: il bootloader usa il BIOS (INT 13h) e funziona, ma il
> kernel accede al controller floppy direttamente (porte 0x3F0-0x3F7, DMA,
> IRQ6). Un floppy USB non è collegato a quel controller: il kernel parte ma
> non riesce a leggere `/bin/sh`. Serve un drive floppy interno — vedi
> `HANDOFF.md` per le alternative.

**Nota toolchain**: il `Makefile` usa `gcc -m32` nativo, non il cross-compiler
`i686-elf-*`. Lo script `tools/install_crosscompiler.sh` resta disponibile, ma
la build non lo richiede (serve però `gcc-multilib`).

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
vive**, e le pagine caricate non vengono mai buttate via (manca lo
sfratto, e con esso il file di scambio).

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
| 248 | lib_apri      | `const char*`| —         | —   | mappa una libreria condivisa e rende la sua tabella |

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
| 252 | pty_ctl     | fd            | comando    | arg | misura della finestra, modo raw, gruppo in primo piano |
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

## /bin/mkdir e /bin/rmdir

```
mkdir <nome> [nome2 ...]    crea una o piu' directory
rmdir <nome> [nome2 ...]    cancella una o piu' directory VUOTE
```

Accettano percorsi assoluti o relativi alla directory corrente. **Solo
directory nella root**: il driver FAT12 risolve i percorsi a un livello, quindi
una directory annidata sarebbe corretta sul supporto ma irraggiungibile —
entrambi rifiutano con un messaggio esplicito invece di creare o cancellare
qualcosa di inutilizzabile.

`rmdir` rifiuta le directory non vuote, e non è una limitazione temporanea:
senza cancellazione ricorsiva i file rimasti dentro diventerebbero
irraggiungibili e i loro cluster resterebbero occupati per sempre. La root è
protetta, e `rmdir` su un file viene rifiutato.

---

## /bin/delete

```
delete <modello> [modello2 ...]

  ?   un carattere qualsiasi
  *   una sequenza qualsiasi di caratteri
```

```
delete nota.txt        un file preciso
delete *               tutto il contenuto della directory corrente
delete /temp/tmp*      i file che iniziano per "tmp" dentro /temp
delete dati?.log       DATI1.LOG, DATI2.LOG, ...
delete *.txt           per estensione
```

L'espansione dei jolly la fa il programma, non il kernel né la shell — come in
MS-DOS. Il confronto è insensibile al caso (FAT12 conserva i nomi in
maiuscolo). Le directory vengono saltate: per quelle c'è `rmdir`.

`delete` lavora in due fasi: prima raccoglie tutti i nomi corrispondenti
percorrendo l'intera directory, poi cancella. Non si può cancellare mentre si
elenca — le entry liberate vengono saltate da `readdir` e le voci successive
scalerebbero, facendo perdere file a ogni blocco.

`delete *` chiede conferma quando i file sono più di uno, dicendo quanti e da
dove. Un modello mirato come `tmp*` non la chiede.

---

## Job control: `&`, `jobs`, `fg`

```
comando &     esegue in background e torna subito al prompt
jobs          elenca i job ancora in esecuzione
fg [n]        riporta in primo piano il job n (l'ultimo se omesso)
```

Un job terminato viene annunciato al prompt successivo — `[1] terminato: tsleep
(codice 0)` — ed è anche il momento in cui il suo slot di processo viene
liberato: un figlio resta `ZOMBIE` finché il padre non lo raccoglie (il reaper di
init si occupa solo degli orfani).

**Non c'è `bg`**, e non è una dimenticanza: `bg` riprende un processo *sospeso*, e
per sospenderlo servirebbe un Ctrl+Z — cioè i segnali, che EX-OS non ha. Un job
qui o gira o è finito, non esiste lo stato in mezzo.

**L'output si mescola.** Un job in background scrive sulla stessa console della
shell, quindi le sue righe finiscono in mezzo al prompt e a ciò che stai
digitando. È il comportamento di qualunque shell Unix; se il programma ha bisogno
dello schermo tutto per sé, si lancia su un'altra console con Alt+Fn invece che
con `&`.

**L'input invece è protetto**, e serviva davvero. Il driver tastiera serve
l'*ultimo* che ha chiesto una riga: senza difese, un job in background che legge
`stdin` sostituirebbe la shell come lettore e il prompt non riceverebbe mai più
un comando — la console morirebbe. Due meccanismi lo impediscono:

1. `sys_read` su `stdin` restituisce la **fine dell'input** a chi non è il
   processo in primo piano della propria console. La shell dichiara il primo
   piano con `SYS_CONSOLE_SETFG` (sé stessa al prompt, il figlio quando lo
   aspetta). Unix qui userebbe `SIGTTIN`; senza segnali, l'EOF è l'unica risposta
   possibile — ed è comunque vera, quel programma input non ne avrà mai.
2. Chi prende la tastiera parlando **direttamente** al servizio `kbd` via IPC —
   la modalità raw di `gfedit` — non passa da `sys_read`, quindi controlla da sé
   `ConsoleInfo.fg` e si rifiuta di partire in background, spiegando perché.

---

## Console virtuali — Alt+F1 … Alt+F4

Quattro schermi indipendenti, uno solo visibile per volta, ognuno con la propria
shell avviata al boot. **Alt+F1..F4** commuta: il programma che stava girando
non viene sospeso né chiuso, continua a lavorare e a disegnare nel proprio
buffer, e si ritrova lo schermo intatto quando ci si torna sopra.

È la risposta alla domanda "come lancio un'altra cosa senza chiudere questa":
apri `gfedit` sulla console 2, premi Alt+F3, hai un prompt pulito, e Alt+F2 ti
riporta all'editor esattamente dove l'avevi lasciato.

| | |
|---|---|
| Console 0 (Alt+F1) | è anche la console di **sistema**: i messaggi del kernel (`klog`) escono qui, accanto al prompt. Le altre restano pulite. |
| Ereditarietà | un programma nasce sulla console del padre (`sys_spawn`), quindi resta dove è stato lanciato |
| Tastiera | i tasti vanno **solo** alla console in primo piano; le shell delle altre restano ferme al proprio prompt con la richiesta di lettura pendente |
| Modalità raw | è **per console**: mentre gfedit tiene la 2 in raw, la shell della 1 continua a ricevere righe intere con eco e Backspace |

Alt+Fn è intercettato dal driver tastiera **prima** di qualunque altra
elaborazione e non viene consegnato a nessuno: è un comando all'interfaccia, non
input per il programma in esecuzione. Senza quella precedenza basterebbe un
editor che usa Alt+F per il menu File per rendere impossibile cambiare schermo —
cioè proprio nel caso in cui serve di più.

### Il Backspace e le colonne che non ci sono

La disciplina di riga «cooked» — quella che accumula i caratteri e li
consegna su Invio — cancellava **una colonna per ogni carattere nel
buffer**. Sembra ovvio e non lo è: i caratteri di controllo entrano nella
riga ma non vengono ecoati (ESC ci va, `/bin/textline` lo usa per annullare
una riga), e le frecce ci entrano come sequenza `ESC [ A`, di cui due byte
su tre sono stampabili e nessuno dei tre è stato disegnato.

> ! Risultato: due ESC battuti per sbaglio, due Backspace, e le due
> colonne cancellate erano **le ultime del prompt**. Nel registro seriale
> si vedeva `^H ^H^H ^H` e l'asterisco di textline sparire.

Ora ogni carattere del buffer porta con sé un bit — *questa l'ho disegnata
io oppure no* — e il Backspace cancella una colonna solo se quella colonna
è nostra. Il prompt è fuori portata per costruzione, non per un controllo
in più. Nello stesso giro sono cadute due asimmetrie della stessa
famiglia: a riga piena il carattere veniva ecoato ma non accumulato (si
eseguiva meno di quello che si leggeva), e il tab veniva disegnato pur
essendo impossibile da disfare — avanza fino alla prossima tabulazione,
che dipende da dove comincia il prompt.

**Arrivati al limite la riga si azzera del tutto.** Se non resta più
niente di visibile, quello che eventualmente sopravvive nel buffer sono
caratteri invisibili, pronti a finire dentro il comando successivo: chi
cancella fino in fondo si aspetta una riga vuota e la trova vuota davvero.

Vale per `drivers/kbd/kbd.c` e per il TTY interno di ripiego
(`drivers/tty/tty.c`). La modifica di riga della shell — quella con le
frecce e la cronologia — non era coinvolta: lavora in raw e accetta solo
caratteri stampabili.

Costo: 4 KB di BSS del kernel per console (il buffer di schermo) più un processo
shell da ~14 KB. Il numero è `VGA_N_CONSOLE` in `kernel/include/vga.h`, e deve
restare uguale a `KBD_N_CONSOLE` in `drivers/kbd/kbd_proto.h`.

---

## Data e ora

`time_now()` legge l'orologio CMOS della macchina (MC146818, porte 0x70/0x71) e
restituisce data e ora vere — quelle che l'orologio a batteria continua a contare
a macchina spenta. Da non confondere con `uptime_ms()`, che misura *durate* e non
sa che ora sia.

Ritorna `-ENODEV` se l'orologio non risponde o consegna una data impossibile
(succede su hardware vecchio con la batteria del CMOS scarica): in quel caso il
chiamante deve dire "ora ignota" invece di mostrare un orario inventato — gfedit
scrive `--:--:--`.

! In QEMU il RTC parte in **UTC**, non in ora locale. Per vedere l'ora del fuso
serve `-rtc base=localtime` fra i `QEMU_FLAGS` del Makefile.

---

## /bin/textline — editor di testo lineare

Modello edlin: si opera per numero di riga, non con un cursore. Resta il modo
più rapido di correggere una riga sola, e l'unico che funziona anche quando
`/dev/kbd.drv` non è disponibile e la console è servita dalla tastiera
in-kernel di ripiego.

```
textline <file>              apre il file per l'editing
textline <file> -v           visualizza il contenuto
textline <file> -vp          visualizza a pagine
textline <file> -c:<file2>   copia <file> in <file2>
```

Comandi: `h`/`help`, `l`, `lNN`, `lNN,MM`, `lp…` (a pagine), `m`, `mNN`, `n`,
`dNN`, `cNN,MM`, `w` (salva), `e` (salva ed esce), `q` (esce). ESC annulla la
riga in inserimento e riporta al prompt.

---

## /bin/gfedit — editor a schermo intero

Riscrittura per EX-OS di **GF_TEXTEDITOR**, l'editor ncurses+pthread dello
stesso autore (sorgenti originali in `gftexteditor/`). Non è un porting: di
ncurses, dei thread, di stdio POSIX e di una `free()` vera EX-OS non ha
niente. Quello che resta uguale è il programma — menu a tendina in stile
MS-DOS EDIT, otto aree aperte insieme, find/replace, annullamento,
evidenziazione sintattica.

```
gfedit                apre un'area vuota
gfedit <file> [...]   apre fino a 8 file
gfedit -h             elenco delle scorciatoie
```

| | |
|---|---|
| Movimento | frecce, Home/Fine, Ctrl+Home/Fine, PagSu/PagGiu, Ctrl+G (vai a riga) |
| Selezione | Shift+movimento, Ctrl+A, ESC per abbandonarla |
| Modifica | Ins, Ctrl+Z, Ctrl+X/C/V |
| File | Ctrl+N, Ctrl+O, F2 o Ctrl+S, Ctrl+W, Alt+X |
| Ricerca | Ctrl+F, F3, Shift+F3, Ctrl+H |
| Aree | F6, Shift+F6, Alt+1…Alt+8 |
| Menu | F10 o ESC, oppure Alt+F M C O A |

La barra di stato mostra l'ora del giorno vera, che **avanza da sola** anche a
tastiera ferma: il ciclo principale non aspetta più un tasto all'infinito ma si
risveglia ogni mezzo secondo (`ipc_recv_timeout`). Fra un risveglio e l'altro il
processo è `BLOCKED` e non consuma un tick di CPU.

Linguaggi evidenziati: C, C++, BASIC, assembly, riconosciuti dall'estensione e
cambiabili da *Opzioni → Linguaggio*.

**Limiti, e perché sono lì.** 512 righe per file, 200 caratteri per riga, 8
aree. Le righe sono slot a lunghezza fissa perché la `free()` di EX-OS è un
no-op dichiarato (allocatore a bump su `sbrk`): con stringhe riallocate ogni
tasto premuto perderebbe per sempre la memoria della riga precedente. Un file
più grande dei limiti viene caricato **in parte**, la barra di stato lo dice, e
il salvataggio su quel file resta bloccato — per non cancellare la parte mai
letta. *Salva con nome* su un file diverso è invece permesso.

**Serve `/dev/kbd.drv`.** Un editor a schermo intero ha bisogno dei tasti uno
per uno, e la modalità raw vive nel driver tastiera. Senza quel servizio
gfedit non parte e rimanda a textline, invece di mostrare un'interfaccia che
non risponderebbe.

---

## L'interfaccia grafica in pratica

```
exwin                       accende la grafica su una console sua
                            Alt+F2 ci va, Alt+F1 torna alla shell

/exwin/bin/pm               la scrivania (la avvia exwin da sola)
/exwin/bin/filemgr [DIR]    il file manager
/exwin/bin/edit [FILE]      l'editor di testo
/exwin/bin/term [PROG]      il terminale in finestra (senza PROG: la shell)
/exwin/bin/browser [URL]    il navigatore (un percorso assoluto diventa file:)
/exwin/bin/exide [DIR]      l'ambiente di sviluppo visuale
/exwin/bin/fontprova        la prova dei font TrueType, fatta per essere vista
/exwin/bin/orologio         data e ora nell'angolo della barra
```

Avviata la grafica, la shell **resta viva sulla console 0**: si continua a
lavorare da lì e con `Alt+F2` si passa alla scrivania.

**Dalla scrivania si aprono dal menu Avvio**, che legge le voci da
`/exwin/lib/applicazioni.txt` — una riga per applicazione, `nome mostrato |
percorso`. La voce **Applicazioni...** dello stesso menu aggiunge e toglie
righe da quel file, e la direttiva `@avvio <percorso>` dice quale programma
parte da solo con la scrivania (è così che l'orologio si trova già lì).

! **AGGIUNGERE UN'APPLICAZIONE È UNA RIGA, NON UNA RICOMPILAZIONE**, e il file
resta leggibile e modificabile a mano apposta: un file di configurazione che
solo un programma sa scrivere è un file che non si può riparare quando quel
programma non parte.

! **DALLA SHELL SI LANCIANO COL COMANDO, MA PRIMA DI COMMUTARE.** Battendo il
comando *dopo* `Alt+F2` i tasti vanno al server grafico, non alla shell — e
sembra che il sistema si sia bloccato. È la stessa separazione che rende
possibile tutto il resto, vista dal lato scomodo.

### L'editor

| tasto | cosa fa |
|---|---|
| frecce, Home/End, PgSu/PgGiù | muovono il cursore |
| Backspace, Canc | cancellano indietro e avanti |
| Invio | spezza la riga |
| clic del mouse | posiziona il cursore |
| Ctrl+S | salva; senza nome apre il dialogo **Salva con nome** |
| Ctrl+Q | esce; se il testo è modificato avvisa e chiede di nuovo |

I pulsanti sono **Nuovo**, **Apri**, **Salva**, **Salva come**, **Ricarica**.
«Apri» e «Salva con nome» stanno in `exdlg.so`, la libreria condivisa dei
dialoghi, che usa anche il file manager.

Manca, dichiarato: annullamento, selezione e appunti. E un dialogo con
**sì/no**: oggi «vuoi perdere le modifiche?» si chiede facendo premere due
volte lo stesso pulsante, e si vede che è un ripiego.

### Il file manager

Elenco che scorre, **directory in cima**, pulsanti `Su` e `Apri`, riga di
stato col percorso. Frecce e Invio oltre al mouse. Premendo «Apri» su un file
lo passa all'editor, cercandolo in `/exwin/bin` e poi in `/cdrom/exwin/bin`.

! **LE DIRECTORY VENGONO PRIMA, E NON È ESTETICA:** in una directory con cento
file, quelle in cui si vuole entrare sarebbero sparse in mezzo. È l'unica cosa
che questo elenco ordina — ordinare i nomi vorrebbe dire un confronto che
dipende dalla lingua.

### Il terminale in finestra

`/exwin/bin/term` apre una shell dentro una finestra; `term /bin/gfedit` ci
apre quel programma invece della shell. La finestra è un multiplo esatto della
cella del font 8x16, perché il controllo «terminale» del toolkit calcola le
colonne come larghezza/8 e le righe come altezza/16.

! **LA SHELL GIRA SU UNA PIPE, NON SUL `tty` DELLA CONSOLE**, ed è tutto il
punto del terminale in finestra. Una shell che legge il descrittore 0 della
console si contende la tastiera con chiunque altro stia su quella console;
dietro una pipe quella domanda non esiste — i tasti li dà il server alla
finestra col fuoco, e da lì vanno nella pipe di *quella* shell. È così che se
ne possono aprire due senza che si disturbino.

### Il navigatore

`/exwin/bin/browser [URL]`. Senza argomento parte dalla pagina di casa; con un
percorso assoluto (`browser /exwin/doc/browser.html`) lo trasforma in un
`file:`, che è la sola forma che il resto del programma conosce.

Nella barra ci sono **due caselle**: l'indirizzo e **Cerca**, che compone da
sola l'interrogazione del motore scelto in **File > Impostazioni**
(duckduckgo, wikipedia, marginalia). Quel che sa fare la pagina — testo che si
spezza e scorre, collegamenti, immagini, fogli di stile, tabelle, moduli,
HTTPS, biscotti, JavaScript — sta nelle voci di «Novità» qui sopra, che sono
il posto dove quel lavoro è raccontato per intero.

### EX-IDE — l'ambiente di sviluppo visuale

`/exwin/bin/exide [DIR]`. Con un argomento apre subito il progetto che sta in
quella directory. Tre aree come in Visual Basic: a sinistra gli strumenti, in
mezzo la maschera su cui si dispongono, a destra le proprietà di quello scelto;
doppio clic su un controllo e si apre l'editor dentro la funzione che l'evento
chiamerà.

! **LA GIUNTURA FRA IL DISEGNO E IL CODICE È L'ID**, e c'era già: nel disegno
il pulsante *è* `ID_PULSANTE1`, nel sorgente c'è `case ID_PULSANTE1:`. exide è
fattibile qui più che altrove perché ExWin era già fatto a forma di VB6 — non
c'è niente da inventare, c'è da *scrivere* quel che il disegno dice.

Un progetto sono quattro file, e la regola che decide se sopravvive è che il
generato e lo scritto non si tocchino mai:

| file | chi lo scrive |
|---|---|
| `finestra.dis` | solo exide: il disegno |
| `finestra.h` | solo exide: gli id, i puntatori, i prototipi |
| `finestra_gen.c` | solo exide: crea i controlli e smista gli eventi |
| `finestra.c` | **solo tu**: exide ci *aggiunge* gli handler che mancano, in fondo, e non riscrive mai quel che c'è |

### La prova dei font

`/exwin/bin/fontprova` disegna le stesse righe in TrueType a corpi diversi, ed
è **fatta per essere fotografata**: le prove grafiche di questo sistema si
misurano nei pixel. Il rasterizzatore è già confrontato con FreeType, ma quel
confronto gira sull'host — dice che i glifi vengono giusti e non dice niente su
`exfont.so` caricata a caldo, sulla cache, sulla fusione col fondo o sul fatto
che i file dei font siano leggibili dal CD. Questa finestra prova il giro
intero.

! **IN CIMA C'È LA RIGA COL FONT DI SISTEMA, e serve da metro:** se il TrueType
non si carica restano solo quella e le scritte di errore, e si capisce subito
dove si è fermato invece di guardare una finestra vuota.

### L'orologio

`/exwin/bin/orologio` mette data e ora nell'angolo della barra, e sta **sopra a
tutte** le finestre perché è un pezzo della barra. È un **processo a parte**, e
non un filo dentro il program manager: quel che si vuole è che l'ora si
aggiorni qualunque cosa faccia il resto del sistema, e un filo condividerebbe
con lui la connessione al server e la coda dei messaggi — un program manager
occupato sarebbe un orologio fermo.

! **L'ORA È QUELLA UNIVERSALE**, e va detto invece di lasciarlo scoprire: la
libc dichiara che `localtime()` è identica a `gmtime()`, perché questo sistema
non sa in che fuso si trovi né ha un posto dove tenerlo. Un'ora locale
inventata sarebbe peggio di quella universale, che almeno è vera.

---

## Inizializzare un disco rigido: /bin/fdisk e /bin/mkfs

Il ciclo completo, da disco vergine a sistema avviabile:

```
disk                        cosa vede il sistema
fdisk hd0                   crea le partizioni, marca attiva la prima
mkfs -t ext2 -L exos hd0p1  ci scrive dentro un filesystem
mount hd0p1 /disk           montalo
install /disk               rendilo avviabile
```

Poi si toglie il floppy e si riavvia. Funziona sia su **FAT16/FAT32** sia su
**ext2**.

### La mappa dei settori, e perché il kernel ne ha una lista

Il settore di avvio non sa leggere alcun filesystem: in 512 byte, tolti BPB e
firma, non ci sta. Riceve LBA e lunghezza di Stage 2 e del kernel, e legge
settori. È `install` — che gira *dentro* EX-OS, dove i driver ci sono già — a
comporre quella mappa.

! Il prezzo è lo stesso patto di LILO: **ricopiare kernel o Stage 2 obbliga a
rilanciare `install`.**

### `install` verifica prima di sostituire (dal 0.161)

Rilanciarlo su un sistema già installato **riscrive ogni file**, kernel
compreso, e rilegge ognuno per confrontarne la dimensione. Nel resoconto `+`
è creato, `~` sostituito, `!` errore.

Fino alla 0.147 i file già presenti venivano saltati: un aggiornamento
copiava solo i file nuovi, lasciava il kernel vecchio e poi riscriveva la
mappa dei settori *per quello* — il disco ripartiva con la versione di
prima e l'installatore diceva «completata». Le directory continuano a non
essere ricreate, e ciò che sul volume non fa parte del sistema resta dov'è:
`install` aggiorna, non azzera.

**E fino alla 0.160 distruggeva prima di sapere se ce l'avrebbe fatta.**
Apriva `/boot/kernel.bin` con `O_TRUNC`, ci scriveva il kernel nuovo, e
*poi* chiedeva la mappa dei settori. Su FAT — dove la mappa ammette **un
solo intervallo** — un kernel cresciuto di qualche KB non entrava più nel
buco lasciato dal vecchio, finiva in due tratti, `bootinstall` rifiutava
giustamente:

```
! installazione dell'avvio fallita: file frammentato (errore -29)
```

…ma a quel punto il sistema che funzionava non c'era più, il settore di
avvio puntava ancora alla mappa vecchia, e **il disco non ripartiva**. Su
ext2 non si vedeva: lì la mappa regge 12 intervalli.

Ora i file nuovi si scrivono **con nomi temporanei mentre i vecchi sono
ancora al loro posto** — quindi finiscono nella coda libera, contigua. Poi
si chiede al kernel se sono mappabili. Solo se la risposta è sì si cancella
e si rinomina:

```
Avvio (scritti a parte e verificati prima di sostituire)
  = verifica: 585 settori in 1 intervallo — si puo' sostituire
  ~ /disk/boot/stage2.bin  (verificato, poi sostituito)
  ~ /disk/boot/kernel.bin  (verificato, poi sostituito)
```

Lo scenario che distruggeva il disco ora **riesce**. Quando invece non si
può fare, si legge `Il sistema gia' installato NON e' stato toccato` e il
disco resta avviabile.

> ! **È servita una primitiva che mancava.** Lo scambio non si poteva
> fare: `rename()` era copia+cancella, quindi riallocava i blocchi e
> mandava a monte la verifica appena fatta. Vedi *La rinomina che non
> sposta i dati* più avanti.

**`kernel.cfg` non si sovrascrive più.** È l'unico file dell'installatore
che appartiene a chi usa il sistema e non al sistema: montaggi automatici,
`verboseboot`, shell, variabili d'ambiente. Un aggiornamento non deve
riportarli indietro in silenzio. Se manca si installa, se c'è si lascia e
lo si dice.

### `install -a` — aggiornare invece di reinstallare

```
install -a /disk
```

Confronta il volume montato con il supporto di avvio, **elenca cosa
cambierebbe**, chiede conferma e solo allora scrive. `+` è un file che sul
disco non c'è, `~` uno che c'è ma è diverso.

```
Confronto di /disk con il supporto di avvio
  +  da creare    ~  da sostituire
  + /disk/bin/ftp
  ~ /disk/bin/ls
  ~ /disk/boot/kernel.bin

35 file da aggiornare (14 nuovi).
Procedo? [si/no]
```

L'elenco viene **prima** della domanda perché «aggiorno 3 file?» e «aggiorno
47 file?» sono due decisioni diverse: un aggiornamento che tocca tutto quando
ci si aspettava un ritocco è il momento in cui ci si accorge di aver montato
il volume sbagliato.

> ! La regola è **«la sorgente è più nuova»**, non «le date sono diverse».
> Copiare un file non ne conserva la data: la copia sul disco nasce con l'ora
> corrente, quindi con la regola ingenua ogni file risulterebbe da aggiornare
> a ogni esecuzione, per sempre, anche subito dopo averlo appena copiato.

La dimensione si confronta **per prima**, ed è il controllo che conta di più:
un file scritto a metà ha la stessa data e una dimensione diversa, e senza
quel confronto il volume resta rotto senza che nessuno lo dica. Se una delle
due date è zero — cioè «questo volume le date non le tiene» — si guarda solo
la dimensione.

Senza `-a`, `install` continua a fare l'installazione completa: riscrive
tutto, che è quello che serve la prima volta e dopo un `mkfs`.

Per il kernel la mappa è una **lista** di intervalli, non uno solo. Su ext2 un
file non è quasi mai contiguo, e non per frammentazione: il blocco di
*puntatori* viene allocato in mezzo ai dati, perché serve prima del tredicesimo
blocco. Un kernel da 147 KB appena copiato sta così:

```
(0-11):74-85, (IND):86, (12-144):87-219
```

Stage 2 invece resta un intervallo solo: sta in ~1 KB, cioè dentro i 12 blocchi
diretti, dove nessun indiretto si è ancora infilato — ed è il pezzo che va
trovato da 512 byte di codice, quindi la sua mappa deve stare in sei byte.

Su FAT la lista ha una voce sola: il formato è lo stesso per i due filesystem,
così Stage 2 non deve sapere da dove sta caricando.

### `chkdsk` — controllo e riparazione di un volume FAT

```
chkdsk <partizione>       controlla e riferisce, non scrive un settore
chkdsk -r <partizione>    controlla e corregge
```

Controlla, in quest'ordine — ogni passo si fida solo di quelli già fatti:
il BPB e la coerenza dei suoi numeri, le copie della FAT, le catene di
cluster percorrendo tutte le directory (condivisi, catene fuori dal volume,
anelli), la dimensione dichiarata di ogni file contro quella della sua
catena, i nomi lunghi, `.` e `..`, e i cluster perduti.

```
tipo FAT32 — 130556 cluster da 8 settori, 2 FAT da 1021 settori
= FAT[0] = 0xffffff8, FAT[1] = 0xfffffff
= le 2 copie sono identiche
! 3 cluster risultano occupati ma nessun file li nomina (12 KB)
```

> ! **Lavora solo su una partizione smontata**, e non è un fastidio da
> aggirare: sopra un volume montato c'è una cache write-back, metà delle
> modifiche recenti sta in RAM. Un controllore che leggesse i settori
> grezzi segnalerebbe incoerenze inventate, e riparando riscriverebbe
> settori che il primo `sync` ricoprirebbe.

> ! **Senza `-r` non scrive un solo settore.** Un controllore che ripara
> di sua iniziativa trasforma un volume danneggiato in un volume
> danneggiato *diversamente*, senza che nessuno abbia visto com'era.

Alcune scelte che non sono ovvie:

- **il tipo si ricava dal numero di cluster**, non dalla stringa
  `"FAT16   "` nel settore di avvio: quella è decorativa e nessuno la
  verifica mai, quindi su un volume malandato è proprio un campo di cui non
  fidarsi;
- **FAT12 si legge con due settori in mano**: una voce occupa dodici bit e
  può cominciare nell'ultimo byte di un settore. In scrittura, se è a
  cavallo, **rinuncia e lo dice** invece di scrivere mezzo valore — sarebbe
  un danno nuovo causato dallo strumento che doveva ripararne uno;
- **su FAT32 i quattro bit alti di una voce non sono nostri**: si
  conservano;
- **la dimensione si confronta con un intervallo**, non con un numero: un
  file di N byte occupa `ceil(N/cluster)` cluster, e pretendere
  l'uguaglianza esatta segnalerebbe come guasto quasi ogni file;
- **i cluster condivisi non si riparano da soli**: quale dei due file abbia
  diritto ai dati non è deducibile, e ripararli d'ufficio significherebbe
  scegliere a caso quale rovinare;
- **i perduti si liberano, non si raccolgono in `FOUND.000`**: sarebbe una
  collezione di frammenti senza nome né struttura, che occupa lo stesso
  spazio che si voleva recuperare.

Sui **nomi lunghi**: una fila di voci finte precede quella 8.3, in ordine
rovesciato, legata a essa da un solo checksum.

> ! Chi rinomina toccando solo la voce 8.3 — e **`rename` di EX-OS fa
> esattamente questo** — lascia il nome lungo a nominare un file che non è
> più quello. Non è un caso di scuola: è un difetto che questo sistema sa
> produrre, ed è il motivo per cui il controllo serve qui.

### I nomi si creano in minuscolo

Il kernel cerca `/bin/sh`, `/boot/kernel.cfg`, `/dev/kbd.drv`. Su FAT il caso
non conta — il driver mette in maiuscolo sia ciò che scrive sia ciò che cerca —
ma su **ext2 `BIN` e `bin` sono due directory diverse**, e un sistema installato
in `BIN` non troverebbe la propria shell. `install` crea tutto in minuscolo,
nomi dei file compresi.

### /bin/fdisk — partizionatore MBR

```
fdisk            elenca i dischi
fdisk hd0        apre la sessione sul disco 0

  p  mostra tabella e spazio libero    a  commuta il flag avviabile
  n  crea una partizione               w  SCRIVE (chiede conferma)
  d  cancella una partizione           q  esce senza scrivere
  t  cambia il tipo
```

**Niente tocca il disco fino a `w`.** Una tabella scritta un pezzo alla volta
passa per stati in cui le partizioni si sovrappongono; se la macchina si
spegne lì in mezzo, resta sbagliata.

È un programma separato da `disk`, che resta in **sola lettura**: è il comando
che si lancia senza pensarci su un disco a cui si tiene, e un programma che a
seconda degli argomenti guarda *oppure* riscrive perde quella garanzia per
tutti gli usi.

Le **politiche** stanno in `fdisk` (allineamento a 1 MiB, primo settore utile
2048, valori predefiniti). Le **regole** stanno nel kernel e non si aggirano:
niente sovrapposizioni, niente partizioni oltre la fine del disco, niente
scrittura su un disco GPT o su una partizione montata.

Non gestisce le partizioni **logiche** e non scrive EBR: le mostra, ma la voce
estesa che le contiene è bloccata — spostarla lascerebbe la loro catena viva
sul disco e irraggiungibile.

`fdisk` non formatta. Una partizione appena creata contiene i byte che c'erano
prima in quei settori: non è vuota, è non inizializzata.

### /bin/mkfs — formattatore FAT16/FAT32/ext2

```
mkfs -t fat32 -L ETICHETTA hd0p1
mkfs -t fat16 hd0p2
mkfs -t ext2  -L dati hd0p3
```

La partizione **non dev'essere montata**: sopra un volume montato c'è una
cache write-back, e scriverci sotto significa che il primo `sync` ci ricopre i
settori vecchi.

Il numero che conta è il **conteggio dei cluster**, e `mkfs` lo mostra accanto
alla soglia. Il tipo di un volume FAT non è scritto da nessuna parte: la
stringa `"FAT16   "` nel settore di avvio è decorativa, e il tipo si deduce dal
numero di cluster dell'area dati (< 4085 → FAT12, < 65525 → FAT16, oltre →
FAT32). Un formattatore che sceglie male i settori per cluster produce un
volume che *dice* FAT16 e *cade* nella banda FAT12, e nessuno se ne accorge
finché i dati non sono già rovinati.

Il settore di avvio vecchio viene azzerato **per primo** e quello nuovo scritto
**per ultimo**: in mezzo il volume non è riconoscibile da nessuno. L'ordine
opposto lascerebbe, su una formattazione interrotta, un settore di avvio che
descrive il filesystem vecchio sopra tabelle FAT già azzerate — un volume che
si monta, sembra funzionare e restituisce file vuoti.

`mkfs` non tocca la tabella delle partizioni, e non per scelta: le syscall
`SYS_BLKREAD`/`SYS_BLKWRITE` accettano solo nomi di **partizione**, e il
settore 0 non appartiene a nessuna partizione. Non esiste una coppia
(nome, LBA) che lo raggiunga. Se il byte di tipo nella tabella contraddice il
filesystem creato, `mkfs` lo segnala e dice come correggerlo con `fdisk`.

### ext2 — creazione e lettura

`mkfs -t ext2` crea un ext2 revisione 1 (`filetype`, `sparse_super`) con
blocchi da 1024. È scritto **dalla specifica**, non portato da e2fsprogs né dal
driver di Linux: quest'ultimo non è un modulo che legge ext2, è un modulo che
*traduce* ext2 nel VFS di Linux, e portarlo significherebbe portare quel VFS.

`kernel/fs/ext2.c` lo legge **e lo scrive**: `mkdir`, `cp`, `delete`, `rmdir`.
La lettura è stata scritta e verificata per prima, da sola — leggere richiede di
capire il formato, scrivere richiede di non romperlo mai, e con un lettore già
provato contro volumi fatti da `mke2fs` ogni errore di scrittura si vede subito
per quello che è.

### /bin/trunc — cambia la dimensione di un file

```
trunc <file> <byte>     suffissi K e M ammessi
```

**Allungare non occupa spazio** su ext2: lo spazio in mezzo diventa un *buco*
che si legge come zeri, e i blocchi si materializzano solo quando ci si scrive.
Un file portato a 2 MB così occupa un blocco solo. Su FAT il kernel alloca sul
serio, perché FAT non sa rappresentare un buco.

**Accorciare è distruttivo e non chiede conferma**, per la stessa ragione per
cui non la chiede `delete` su un nome preciso: chi scrive il nome di un file e
un numero più piccolo della sua dimensione ha già detto cosa vuole. La conferma
serve quando il comando fa più di quanto l'utente abbia nominato.

Sul floppy risponde `-38` (ENOSYS): `fat12.c` non ha un troncamento, ed è il
driver del volume di avvio — la strada collaudata che non si tocca senza una
ragione forte.

! ext2 non ha un giornale e questo driver non ne inventa uno: un'interruzione a
metà di un'operazione può lasciare i contatori dei liberi indietro rispetto alle
bitmap, ed è quello che serve `e2fsck`. Ciò che **non** può succedere è che un
blocco risulti libero mentre è già in uso — le bitmap si scrivono sempre prima
che il blocco venga consegnato.

**Nomi lunghi**: fino a 255 caratteri, il massimo di ext2. Non era solo la
struttura `VfsDirEntry` da allargare — il nome attraversa sei tetti in fila
(driver, VFS, ABI della syscall, lunghezza dei percorsi, argomenti di `spawn`,
riga della tastiera) e alzarne uno solo avrebbe spostato il taglio di un passo.
Un nome troncato non è un nome accorciato: è un nome che non apre niente.

Su FAT i nomi restano 8.3; il campo è largo per il filesystem più generoso.
Sopra i 511 caratteri una riga di comando non si può digitare, ed è il limite di
un singolo messaggio IPC fra tastiera e shell.

Il driver legge e scrive anche i volumi fatti da `mke2fs`: la dimensione del blocco
(1024/2048/4096), `s_first_data_block` (1 o 0 a seconda) e la dimensione
dell'inode (128 o 256) vengono tutte dal superblocco, mai date per scontate.

Rifiuta invece di provarci quando trova una funzionalità **incompat** che non
conosce — un volume ext4 con extent ha `i_block` che non contiene numeri di
blocco, e leggerlo "come se" restituirebbe dati presi a caso dal disco senza
che nulla lo segnali.

---

### `install -m` / `install -t` — minimale, oppure con i componenti

```
install /disk          mostra i componenti e li chiede uno per uno
install -m /disk       solo il sistema minimale, nessuna domanda
install -t /disk       sistema e tutti i componenti, nessuna domanda
```

L'installatore guarda la radice del supporto di avvio e chiama **sistema
minimale** un elenco chiuso: `bin`, `boot`, `lib`, `dev`, `drivers`. Qualunque
altra directory è un componente, e viene mostrata e chiesta:

```
===============================================================
 COSA INSTALLARE
===============================================================

Il sistema minimale e' sempre incluso: kernel, /bin, /lib,
i driver e l'avvio. Senza quello EX-OS non parte.

Su questo supporto ci sono anche 2 componenti in piu':

    /doc
    /exwin

Installo solo il sistema minimale? [si/no] no

Uno per volta:

  /doc ? [si/no] no
  /exwin ? [si/no] si
```

! **AGGIUNGERE UN PACCHETTO NON RICHIEDE DI TOCCARE L'INSTALLATORE:** basta
metterne la directory sul supporto. Un elenco scritto dentro `install` sarebbe
una seconda verità accanto al contenuto della directory, e le due divergono al
primo pacchetto aggiunto o tolto. **Chi prepara un pacchetto non ha i sorgenti
dell'installatore.**

! **I COMPONENTI SI COPIANO CON TUTTI I LORO LIVELLI.** `/exwin` non contiene
file, contiene `bin/ lib/ dev/`: copiarlo a un livello solo darebbe una
directory vuota **e nessun errore**, cioè un'installazione che sembra riuscita.

! **E LA SCELTA SI FA PRIMA DI SCRIVERE**, non a metà strada: chiedere dopo aver
sostituito il kernel vorrebbe dire che rispondere «annulla» non annulla più
niente.

Gli script che installano senza nessuno davanti — `tools/mkhd.sh` — usano `-t`.
Senza il flag l'installatore si fermerebbe su una domanda a cui nessuno
risponde, e la prova direbbe «l'installazione non è arrivata in fondo»: un
messaggio che non somiglia alla sua causa.

---

## La libc: da minimale a ospitata

Fino alla 0.145 `lib/libc.c` era una libreria da programma di servizio: `printf`,
le syscall e poco altro. Da agosto 2026 è la base su cui si può portare del
software scritto per un sistema POSIX — a cominciare da un compilatore.

### L'allocatore, che è il pezzo che mancava davvero

`free()` era una funzione **vuota**, con un TODO al posto del corpo, e `malloc()`
chiamava `sbrk` a ogni allocazione. Per i programmi di `/bin` non si notava:
allocano poche volte e poi escono. Per qualunque cosa lavori su una struttura ad
albero — un parser, un compilatore — significava crescere fino a esaurire lo
spazio senza aver mai tenuto in mano più di qualche KB. E `realloc()` copiava
`size` byte da un blocco di cui non conosceva la dimensione: ingrandire leggeva
**oltre la fine**.

Ora i blocchi stanno in una lista in ordine di indirizzo, `free()` li rende
riusabili e li **fonde** con i vicini liberi. La prova che conta sta in
`/bin/libctest`: 2000 `malloc`/`free` in ciclo non fanno crescere l'heap di un
byte.

### Stdio bufferizzato, con una politica diversa da Unix

C'erano `printf` e `putchar` — e `putchar` faceva **una syscall per carattere**.
Ora ci sono i `FILE*` (`fopen`, `fread`, `fwrite`, `fseek`, `ftell`, `fgets`,
`fprintf`, …), e un solo formattatore serve `printf`, `fprintf`, `sprintf` e
`snprintf`.

I file su disco sono bufferizzati a 4 KB. `stdout` e `stderr` **no**: sono
bufferizzati *dentro* la singola chiamata e svuotati alla sua fine. Non è il line
buffering di Unix, ed è deliberato — con quello, il prompt della shell
(`ex-os:/> `, senza newline) resterebbe nel buffer, e `gfedit` mostrerebbe
l'ultima riga solo dopo il tasto successivo. Unix se la cava perché leggere da
stdin svuota stdout; qui `gfedit` non legge da stdin, parla via IPC con il
servizio `kbd`, quindi quella convenzione non lo salverebbe. Svuotare a fine
chiamata elimina il problema e conserva quasi tutto il guadagno: un `printf`
costa una syscall invece di ottanta.

`exit()` svuota tutti i flussi: un programma che scrive un file e poi esce senza
`fclose()` trova il file scritto, non monco.

### Il resto

`setjmp`/`longjmp` (in assembly: sono esattamente i registri che il compilatore
ha il permesso di riorganizzare), `errno` con `strerror`/`perror`, `strtol`,
`strtoul`, `qsort`, `bsearch`, `strstr`, `strdup`, `strtok`, `memchr`, `ctype`,
e i wrapper `lseek`/`stat`/`sbrk`.

**`errno` si aggiunge, non sostituisce.** Le funzioni continuano a ritornare
l'errore negativo (`-2` = ENOENT, `-30` = EROFS): `< 0` resta il test giusto in
entrambe le convenzioni, e `-EIO` dice più di `-1`. Riscrivere ogni chiamante per
guadagnare zero non aveva senso.

Le syscall `stat` e `lseek(SEEK_END)` **rispondevano `ENOSYS`** — mai
implementate, con un TODO dalla Fase 3. Senza di loro nessun `FILE*` può offrire
`ftell()` sulla fine, cioè il modo con cui ogni programma misura un file prima di
leggerlo. Ora passano dal VFS e valgono su ogni filesystem montato.

### Virgola mobile, e la FPU che il kernel ha dovuto accendere

`strtod`, `strtof`, `strtold`, `ldexp`, `strtoll`, `strtoull`. Non è un lusso:
un compilatore deve leggere i letterali numerici dei sorgenti che compila —
`float x = 1.5;` passa da `strtod` — e una `strtod` che ritorna zero non dà un
errore, dà un programma compilato con la costante sbagliata.

Da qui il **PASSO 7b** del kernel: la FPU x87 viene inizializzata e il suo stato
(108 byte) salvato nel PCB a ogni cambio di contesto. Senza, due processi che
fanno conti in virgola mobile si sovrascrivono i registri a vicenda.

! **`x == 0.025` può essere falso anche quando `x` è giusto.** Su x87 GCC valuta
le costanti a 64 bit di mantissa, il `double` ne ha 53: il confronto avviene fra
due numeri diversi per costruzione. Il valore atteso va messo in una variabile
`double`, che forza l'arrotondamento.

### La libm: openlibm, e perché una di terzi

Fino ad agosto 2026 `<math.h>` dichiarava tre funzioni e diceva che una libm
non c'era, con questa motivazione:

> «una `sqrt` quasi giusta è peggio di nessuna `sqrt`: sbaglia in silenzio».

**Il ragionamento non è cambiato — è cambiata la conseguenza.** La risposta
coerente a "non so scrivere `sin` con l'errore giusto" non era scriverne una
mediocre: era **portarne una vera**. Dietro i nomi c'è **openlibm 0.8.7**,
cioè la `msun` di FreeBSD in versione autonoma (MIT/BSD), con trent'anni di
correzioni sugli arrotondamenti e una directory `i387` che usa le istruzioni
dell'x87 dove convengono. Si costruisce con
`tools/openlibm-exos/prepara-libm.sh`; i sorgenti non stanno nel repository,
come per GCC e binutils.

```
ex-os:/> /cdrom/bin/provamat
openlibm dentro EX-OS

sin(pi/2)=1000 cos(0)=1000 pow(2,10)=1024000 log(e)=1000 sqrt(16)=4000
atan2(1,1)=785 hypot(3,4)=5000 isnan(0/0.)=1
sinf(pi/2)=1000  sinl(pi/2)=1000  exp2(10)=1024000
```

I valori sono moltiplicati per mille e stampati come interi — la `printf`
di EX-OS non formatta i `double`, e mostrarli in virgola mobile avrebbe
provato la printf invece della libm. Quindi `atan2(1,1)` = 785 = 0,785 =
π/4.

I 184 prototipi in `lib/include/math.h` sono **ricavati dai simboli davvero
definiti in `libm.a`**, non copiati da uno standard: se una funzione è
dichiarata lì, esiste.

> ! **Chi usa queste funzioni deve linkare `-lm`.** Le eccezioni sono
> `sqrt`, `fabs`, `ldexp` e `frexp`, che stanno nella libc. `sqrt` è
> `fsqrt` dell'x87, cioè **una delle cinque operazioni che l'IEEE 754
> obbliga a essere correttamente arrotondate**: non c'è
> un'approssimazione da giudicare, c'è un'istruzione da chiamare. La
> definisce anche `libm.a`, ma vince sempre quella della libc — `libc.o`
> entra comunque nel link (printf, crt0) e a quel punto il simbolo è già
> risolto. Nessun "multiple definition", e `sqrt` si usa senza `-lm`.

Chi la chiede davvero è **libstdc++**: il suo `<cmath>` scrive `using
::sin;` per circa centottanta nomi, e quei nomi devono esistere o la
libreria non compila.

### Allocazione allineata

`memalign`, `aligned_alloc`, `posix_memalign`. Dal C++17 un tipo con
allineamento superiore a quello naturale non passa più per `operator
new(size_t)` ma per la variante allineata, che nella libstdc++ è un
involucro attorno a `memalign()` — e se `memalign` non c'è, la libreria ne
mette una che **ignora l'allineamento richiesto**.

L'allineamento si ottiene ritagliando: si chiede a `malloc` un blocco
abbastanza grande, poi lo si **spezza in due** mettendo una vera
intestazione subito prima dell'indirizzo allineato, e la testa resta come
blocco libero invece di essere sprecata.

> ! **Il puntatore restituito si libera con `free()`**, non con una free
> speciale: per l'heap è un blocco come tutti gli altri, e la fusione con i
> vicini funziona senza sapere nulla di tutto questo.

> ! `posix_memalign` **ritorna** il codice di errore e non lo mette in
> `errno`. È l'eccezione della famiglia, ed è il modo classico di sbagliare
> a usarla.

### Lettura formattata, data e ora, stat

`sscanf`/`vsscanf` con larghezze, `%n`, soppressione e `%lf`. `time`,
`localtime`, `gmtime`, `mktime`, `gettimeofday` sopra `SYS_TIME`, cioè
l'orologio CMOS — senza fuso orario, perché il sistema non sa in quale si trova:
`localtime` e `gmtime` danno la stessa ora. `stat`/`fstat` nella forma POSIX.

### Processi: spawn con ambiente e redirezioni

`spawn()` lancia un programma e ritorna il PID; `waitpid()` ne raccoglie
l'esito. Non c'è `fork()`, ed è una scelta: duplicare uno spazio di
indirizzamento per buttarlo via alla `exec` successiva, su un sistema senza
copy-on-write, sarebbe la cosa più costosa che si possa fare.

```c
SpawnRedir r = { 1, O_WRONLY | O_CREAT | O_TRUNC, "/uscita.txt" };
int pid = spawn_ex("/bin/hello", argv, environ, &r, 1);
waitpid(pid, &stato, 0);
```

La redirezione è **per percorso**, non per descrittore già aperto del
padre: il figlio apre il proprio file. Passare un fd significherebbe due
processi sullo stesso handle VFS, cioè un conteggio di riferimenti che non
c'è e una `close()` che sfila il file da sotto all'altro. Basta a un driver
di compilatore; non basta alle pipe, che infatti non ci sono ancora.

L'ambiente si eredita per copia (`environ`, `putenv`, `setenv`,
`unsetenv`). `getenv()` ripiega sulla sezione `[env]` di `kernel.cfg` per
le chiavi che non trova: senza quel ripiego il primo processo — che un
padre non ce l'ha — resterebbe senza `PATH`.

### Intestazioni con i nomi standard

`<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<ctype.h>`, `<errno.h>`, `<setjmp.h>`,
`<unistd.h>`, `<stdint.h>`, `<inttypes.h>`, `<math.h>`, `<time.h>`, `<fcntl.h>`,
`<assert.h>`, `<sys/stat.h>`, `<sys/time.h>` esistono e rimandano a `libc.h`
(`<stdint.h>` no: i tipi li dichiara il compilatore, vedi
`tools/gcc-exos/leggimi.md`). Sono facciate sottili di proposito:
due elenchi della stessa funzione divergono, e la divergenza si manifesta come
prototipo sbagliato — argomenti passati storti, non un errore di compilazione.

### Il costo, e come è stato riassorbito

Ogni programma di `/bin` compila la libc dentro di sé. Con lo stdio nuovo i
binari erano raddoppiati (`ls`: 12 → 25 KB). `-ffunction-sections`
`-fdata-sections` più `--gc-sections` al link buttano ciò che nessuno chiama:
`ls` è tornato a **10 KB**, meno di prima che la libreria crescesse, e il floppy
ha più spazio libero di quanto ne avesse all'inizio.

```
libctest       276 prove: allocatore (compresa l'allocazione allineata e
                la crescita dello heap fino al rifiuto), formattazione,
                flussi, salti non locali, conversioni, errno, virgola
                mobile, sscanf, data e ora, stat, ambiente, directory,
                temporanei, descrittori duplicati, variabili __thread,
                interfacce per il codice di terzi, spawn con
                redirezione — tutte dentro EX-OS
```

### Le pipe

`pipe()` dà due descrittori collegati da un buffer circolare di 4 KB nel
kernel. Le regole che contano sono tutte di confine:

> ! **Vuota con uno scrittore vivo = aspetta; vuota senza scrittori = 0.**
> È tutta qui la ragione per cui il conteggio degli scrittori esiste: senza,
> una pipe non è distinguibile da un blocco eterno. E scrivere quando non
> c'è più nessun lettore dà `EPIPE`, non un'attesa.

> ! **Niente `SIGPIPE`**: EX-OS non ha i segnali, quindi si vede solo il
> valore di ritorno. Chi non guarda quello di `write()` non se ne accorge.
> E **niente garanzia di atomicità**: la scrittura può essere parziale
> anche sotto `PIPE_BUF`.

`FD_PIPE_R` e `FD_PIPE_W` sono due **tipi** di descrittore, non uno con un
flag: la direzione non è un dettaglio, è ciò che decide se una `read`
blocca o è un errore.

Per collegare due processi serve passare un'estremità al figlio, e lo si fa
con `SpawnRedir` a `percorso` NULL — l'**eredità dei descrittori**, che è la
sola aggiunta che rende le pipe utili fuori da un singolo processo:

```c
int p[2]; pipe(p);
SpawnRedir a = { 1, 0, NULL, p[1] };   /* stdout del figlio = scrittura */
spawn_ex("/bin/cmd", argv, environ, &a, 1);
close(p[1]);                            /* ! indispensabile */
```

> ! **Il padre deve chiudere l'estremità che ha passato.** Se non lo fa, la
> pipe conta ancora uno scrittore vivo — lui — e chi legge aspetterà per
> sempre. È l'errore classico con le pipe, e qui non c'è niente che lo
> segnali.

**Un difetto di progetto che le pipe hanno fatto emergere.** I descrittori
si chiudevano in `proc_reap_zombie()`, cioè quando il genitore chiama
`waitpid()`. Con i soli file passava inosservato; con una pipe è uno
stallo: il figlio esce, i suoi fd restano contati, il padre è bloccato
nella `read` e non arriverà mai al `waitpid`. Due processi ad aspettarsi a
vicenda, nessun errore. Ora si chiudono alla morte del processo — è il
motivo per cui su Unix stanno in `do_exit()` e non in `wait()`: uno zombie
non deve trattenere risorse di I/O, solo il proprio codice di uscita.

### La rinomina che non sposta i dati

`rename()` era **copia+cancella**: costava quanto il file e **rialloca i
blocchi**. Portava il nome di un'altra cosa, e la differenza non era
accademica — è ciò che rendeva impossibile a `install` verificare la mappa
dei settori del kernel prima di dargli il nome definitivo.

Dalla 0.161 è una syscall vera (`vfs_rename`, implementata su tutti e tre i
driver: `fat.c`, `ext2.c`, `fat12.c`) che riscrive la voce di directory e
basta. **I blocchi non si spostano**: è la garanzia su cui si regge
l'installatore. C'è anche il comando:

```
ex-os:/> rename /r1.bin /r2.bin
/r1.bin -> /r2.bin
```

> ! **Si chiama `rename` e non `mv`, di proposito.** `mv` su Unix
> rinomina *e sposta*, e quando sposta fra filesystem copia e cancella —
> un'operazione completamente diversa sotto lo stesso nome. Qui i dati non
> si muovono mai, e il nome dice cosa fa.

> ! **Due differenze da POSIX, dichiarate:** solo nella **stessa
> directory** (ENOSYS), e **non sostituisce** la destinazione (EEXIST).
> Attraversare directory sarebbe una copia più una cancellazione;
> sostituire vuol dire cancellare un file che il chiamante non ha nominato
> come vittima.

In `ext2_rename` **si aggiunge prima e si toglie dopo**: in mezzo il file
ha due nomi, che è riparabile; nell'ordine opposto non ne avrebbe nessuno e
l'inode resterebbe allocato e irraggiungibile.

> ! **`fat12.c` risponde in errno, gli altri due no.** Lì `-2` significa
> `ENOENT`, in `fat.c` ed `ext2.c` significa «esiste già». Mescolare le due
> convenzioni fa dire «esiste già» a una rinomina di un file inesistente —
> ed è successo, alla prima prova con un nome sbagliato.

### `getrusage`, `getpagesize`, `mmap`

Le tre funzioni che GCC usa come programma ospite, ognuna con il proprio
limite dichiarato.

> ! **`getrusage` non è una misura.** EX-OS non tiene contabilità per
> processo: lo scheduler assegna quanti e non misura consumi. `ru_utime`
> riporta il tempo **trascorso dall'avvio** — un limite superiore onesto —
> e tutto il resto è zero. Chi ci costruisce sopra un profilo
> (`gcc -ftime-report`) otterrà per ogni passaggio lo stesso tempo.

> ! **`mmap` mappa solo memoria anonima**: con `fd != -1` dà `ENODEV`.
> Mappare un file richiederebbe le pagine sporche e il momento in cui
> riscriverle; una mmap che finge consegnando zeri darebbe un programma che
> legge dati sbagliati senza che niente lo segnali. E ritorna
> **`MAP_FAILED`, non `NULL`**.

`munmap` **riporta giù il confine** se la zona smappata era in cima: prima
le pagine fisiche tornavano al PMM ma lo spazio di indirizzamento no, e il
garbage collector di GCC fa quel ciclo migliaia di volte per file
compilato.

### Descrittori duplicati: `dup`, `dup2`, `fcntl`

Dalla 0.151 gli handle aperti del VFS hanno un **conteggio dei
riferimenti**: `close()` chiude davvero solo l'ultima volta. È ciò che
rende possibile `dup()`, cioè tenere un file aperto oltre la `close()` di
chi possedeva il descrittore originale — il gesto che fanno `ar`,
`objcopy` e `arsup` di binutils.

`dup2()` è anche l'unico modo di sostituire `stdin`/`stdout`/`stderr`:
`close()` su 0, 1 e 2 è rifiutata apposta, perché lascerebbe il processo
senza uscita, mentre chi arriva da `dup2` il rimpiazzo ce l'ha già.

> ! **I due descrittori condividono il file, non la posizione.** Su POSIX
> una `read()` da uno dei due sposta anche l'altro; qui l'offset sta nel
> descrittore del processo, non in un oggetto «file aperto» intermedio, e
> ognuno tiene il suo. Chi legge da un fd duplicato faccia una `lseek()`
> esplicita. È la prima cosa da sistemare il giorno che arriveranno le
> pipe.

---

## Scrivere una libreria condivisa

Una libreria di EX-OS è un ELF normalissimo, collegato a un indirizzo
riservato, il cui **punto d'ingresso non è codice**: è una tabella di nomi.

**Il sorgente della libreria** dichiara cosa esporta:

```c
#include "exlib.h"

static const char *const nomi[] = { "pippo", "pluto" };
static void *const dove[]       = { (void *)pippo, (void *)pluto };

EXLIB_TESTA(mia_tabella, nomi, dove);
```

**Il linker script** la mette alla base della sua fetta:

```
ENTRY(mia_tabella)

SECTIONS
{
    . = 0x04C00000;             /* la prima fetta libera */

    .exlib_testa : { KEEP(*(.exlib_testa)) }
    .text        : { *(.text) *(.text.*) }
    .rodata      : { *(.rodata) *(.rodata.*) }

    . = ALIGN(4096);            /* obbligatorio, vedi sotto */

    .data : { *(.data) *(.data.*) }
    .bss  : { *(.bss) *(.bss.*) *(COMMON) }
}
```

**Chi la usa** chiede i nomi che gli servono:

```c
const ExLibTesta *t = exlib_apri("/lib/mialib.so");
void (*pippo)(void) = exlib_simbolo(t, "pippo");
```

Nella pratica non lo fa a mano: si scrive uno **stub** — un file con le stesse
funzioni della libreria, ognuna un ponte verso il puntatore risolto — e le
applicazioni si collegano a quello. Il loro sorgente non cambia di una riga.
Vedi `lib/exwin/exwin_stub.c` come modello.

### Le tre regole che non si possono violare

! **`. = ALIGN(4096)` PRIMA DI `.data`.** Il kernel condivide le pagine di sola
lettura e ne dà una copia privata di quelle scrivibili, e lavora a **pagine
intere**. Se la fine di `.rodata` e l'inizio di `.data` stessero nella stessa
pagina, quella pagina sarebbe scrivibile — cioè copiata per ogni processo — e
mezza `.rodata` smetterebbe di essere condivisa **senza che nessuno lo dica**.

! **UNA FETTA PER LIBRERIA, ASSEGNATA IN UN POSTO SOLO.** La mappa sta in
`lib/exwin/exwin.ld`. Due librerie alla stessa base si sovrascriverebbero
dentro il processo che le usa tutt'e due.

! **SI AGGIUNGE, NON SI TOGLIE.** Aggiungere funzioni, riordinarle e riscriverne
il corpo è sempre lecito: le applicazioni già compilate continuano a
funzionare. Togliere un nome le rompe — e lo dicono, con il nome che manca,
invece di saltare nel vuoto.

### Se la libreria ne usa un'altra

Un programma ha `_start`, e lì c'è un posto naturale in cui agganciare ciò che
gli serve. **Una libreria non parte**: le sue funzioni vengono chiamate e
basta. Se ha bisogno di altre librerie, esporta il nome facoltativo
`__lib_avvio`: `exlib_apri()` lo cerca e lo chiama, ed è l'unico momento in cui
si sa che la libreria è appena stata mappata.

È così che `exwin.so` ed `exdlg.so` usano `libc.so` senza portarsene dentro una
copia.

---

## La catena di compilazione dentro EX-OS

### La catena intera, con un comando solo (6 agosto 2026)

```
ex-os:/> mount hd0p1 /src
ex-os:/> cd /src
ex-os:/src> /cdrom/exos/bin/gcc -O2 -o pg pg.c
ex-os:/src> /src/pg
La catena intera dentro EX-OS

  somma dei quadrati 1..10 : 385   (atteso 385)
  lunghezza del nome       : 5     (atteso 5)
  divisione a 64 bit       : 64    (atteso 64)

Compilato, assemblato e collegato qui dentro.
```

`pg.c` è `tools/iso/prova-gcc.c`: `#include <stdio.h>` e `<string.h>`,
`printf` con `%lld`, `memcpy` dalla libc, una divisione a 64 bit che chiama
`__divdi3` in libgcc, una struttura restituita per valore. **I valori sono
noti in anticipo** — 385 è la somma dei quadrati da 1 a 10 — perché un
programma che stampa un numero senza che nessuno sappia quale fosse quello
giusto è una prova che non prova niente.

Il driver ha lanciato da sé cc1, `as`, `collect2` e `ld`, e ha trovato gli
header **senza un solo `-I`**.

### E lo stesso in C++

```
ex-os:/src> /cdrom/exos/bin/g++ -O2 -o pp pp.cpp
ex-os:/src> /src/pp
La libreria standard del C++ dentro EX-OS

  vector+sort  : 1 3 5 7 9
  string       : "std::string concatenata" (23 caratteri)
  cerchio      : area = 12566 (x1000)
  quadrato     : area = 9000 (x1000)
  eccezione    : lanciata e ripresa
  out_of_range : presa da dentro la libreria

La libreria standard risponde.
```

Contenitori con `<algorithm>`, `std::string` (cioè `operator new` sopra la
nostra `malloc`), polimorfismo con distruttore virtuale, e **le eccezioni**
— compresa una lanciata da dentro libstdc++ e ripresa attraverso più
livelli di stack, che è il pezzo che ha bisogno del maggior numero di cose
funzionanti insieme.

! **Nessun `-B`, e fino al 6 agosto 2026 serviva.** I percorsi che GCC ha
compilati dentro sono assoluti (`/exos/...`) e con il CD montato su
`/cdrom` non combaciano: il driver deve *rilocarsi*, cioè ricalcolare il
proprio prefisso da dove sta lui. Non ci riusciva, e il `-B` era la stampella.

Ci riesce da quando l'ambiente arriva davvero al processo figlio — è
`GCC_EXEC_PREFIX` a portare il prefisso rilocato, e `pex-exos.c` girava a
`spawn_ex` un ambiente vuoto. Le altre quattro correzioni servivano allo
stesso scopo: `..` che si risolve nei percorsi, `st_ino` che distingue le
directory, `-1` invece di `-errno`, e gli argomenti che non vengono
troncati. **Il `-B` è sparito come conseguenza, non come obiettivo.**

### L'albero `/exos`, e perché è fatto così

Non è una scelta di stile: è ciò che i binari cercano a runtime, e si legge
da loro (`strings gcc/cc1 | grep /exos`).

```
/exos/bin/                            gcc, g++, cpp, fbc
/exos/libexec/gcc/i386-exos/17.0.0/   cc1, cc1plus, collect2
/exos/lib/gcc/i386-exos/17.0.0/       libgcc.a, crt*.o, include/
/exos/lib/                            libc.a, libm.a, libstdc++.a, libcrypto.a
/exos/lib/freebasic/linux-x86/        libfb.a, fbrt0.o
/exos/include/                        la libc
/exos/include/c++/17.0.0/             libstdc++
/exos/include/freebasic/              i .bi di FreeBASIC
/exos/i386-exos/include/              la libc, dove cc1 la cerca da solo
/exos/i386-exos/bin/                  as, ld
```

! **La libc sta in due posti e non è uno spreco.** In `/exos/include`
perché è lì che la mette il prefisso; in `/exos/i386-exos/include` perché
quello è `TOOL_INCLUDE_DIR` — «un altro posto dove potrebbero stare gli
header del sistema bersaglio», `gcc/cppdefault.cc` — ed è l'unico dei due
che la rilocazione con `-iprefix` sappia raggiungere.

! **FreeBASIC vuole la stessa struttura, e se la calcola da solo.** Il suo
prefisso è la directory dell'eseguibile meno `bin`, ricalcolata a ogni
avvio: da `/exos/bin/fbc` trova `include/freebasic` e
`lib/freebasic/<target>` ovunque sia montato il CD — che è la cosa che a
GCC riesce solo con `GCC_EXEC_PREFIX` o con `-B`.

**GNU binutils 2.44 gira nativamente su EX-OS.** `as` e `ld` sono
compilati **per** `i386-exos`, non per la macchina che li ha costruiti:

```
ex-os:/> /cdrom/bin/as --version
GNU assembler (GNU Binutils) 2.44
This assembler was configured for a target of `i386-exos'.

ex-os:/> /cdrom/bin/as -o /prova.o /cdrom/prova.s
ex-os:/> /cdrom/bin/ld -o /prova /prova.o
ex-os:/> /prova
Assemblato e collegato dentro EX-OS.
```

L'oggetto prodotto qui dentro è **identico byte per byte** a quello che
produce il cross su Linux. I due strumenti stanno sul CD degli strumenti
(`make iso`, ~1,4 MB l'uno dopo lo strip); il sorgente di prova è
`/cdrom/prova.s`.

### Perché è il collaudo che conta

`libctest` chiama le funzioni che **sappiamo** di avere. binutils chiama
quelle che gli servono, e non ha nessun riguardo: le ha chieste una alla
volta, ognuna fermando la compilazione, e l'elenco è la misura di quanto
mancava a una libc «ospitata» vera —

| | |
|---|---|
| processi | `dup`, `dup2`, `fcntl`, `_exit` |
| file | `realpath`, `lstat`, `freopen`, `mktemp`, `pathconf`, `utime` |
| stringhe | `strcasecmp`, `strncasecmp`, `strcoll`, `strpbrk` |
| formato | `fscanf`, `scanf`, `strftime`, `asctime`, `ctime` |
| numeri | `frexp`, `atof`, `fabs` |
| caratteri larghi | `mbstowcs`, `mbrtowc`, `wcstombs` |
| permessi (inerti) | `chmod`, `fchmod`, `umask` |
| header | `<sys/types.h>` `<strings.h>` `<wchar.h>` `<sys/param.h>` `<limits.h>` `<memory.h>` `<utime.h>` |

più due che non erano funzioni: **`EOF`** — il valore c'era dal principio,
mancava il *nome*, e `safe-ctype.h` verifica di poter lavorare con
`#if EOF != -1`, che senza la macro vede zero e conclude che la libc è
sbagliata — e **`strerror` che tornava `const char *`**, più sicuro e
incompatibile con la firma dello standard.

### `pex-exos.c`: lanciare un programma senza `fork`

`libiberty` compila sempre un `pex-*.c` — «lancia un programma e
aspettalo» — e per tutto ciò che non è Windows o MSDOS sceglie
`pex-unix.c`, costruito su `fork()`. EX-OS non ha `fork`, e non è una
mancanza da colmare: duplicare uno spazio di indirizzamento per buttarlo
via un'istruzione dopo è esattamente ciò che `spawn_ex()` evita.

`tools/binutils-exos/pex-exos.c` è il rimpiazzo, modellato su
`pex-msdos.c`: un «descrittore» è un indice in una tabella di **nomi**,
perché è il nome ciò che serve a `spawn_ex`. I tre `NULL` nella tabella
`funcs` (pipe, fdopenr, fdopenw) **sono la dichiarazione che questo
sistema non ha pipe**, e `pex-common.c` se ne accorge da solo e passa alla
modalità a file temporanei.

### Tre trappole, che costano un'ora a testa

- **`-std=gnu17`.** GCC 17 compila in C23, dove una dichiarazione
  implicita è un **errore**. binutils 2.44 presume l'indulgenza di C17.
- **`export ac_cv_tls=`** (stringa vuota, non `none`). La prova che il
  configure fa per le variabili thread-local è una **compilazione**:
  `i386-exos-gcc` accetta `_Thread_local` senza fiatare, perché è il
  compilatore a saper emettere gli accessi via `%gs` ed è il *sistema* a
  non avere un thread pointer. Il risultato è un `as` che si compila
  benissimo e muore alla terza istruzione di `bfd_init`. Con `none` la
  macro non viene definita affatto e binutils non compila: serve
  **definita a vuoto**.
- **`sys-include`.** GCC installa un proprio `<limits.h>` e ne esistono
  due versioni; quella che fa `#include_next` — cioè che prende anche la
  nostra — viene generata solo se, al momento di costruire GCC, un
  `limits.h` di sistema c'era già, e GCC lo cerca in
  `$prefisso/i386-exos/sys-include`.

Ricetta completa in **`tools/binutils-exos/leggimi.md`**.

### GMP, MPFR e MPC

Le tre librerie a cui `cc1` si linka girano anche loro dentro EX-OS — sono
il codice di terzi più pesante che il sistema abbia ospitato, 4,6 MB di
archivi di aritmetica a precisione arbitraria:

```
ex-os:/> /cdrom/bin/provamp
GMP 6.3.0, MPFR 4.2.1, MPC 1.3.1 — dentro EX-OS

GMP   2^128 = 340282366920938463463374607431768211456
MPFR  pi     = 3.1415926535897932384626433832795028841971693993751e0
MPC   sqrt(i)= (7.0710678118654752440e-1 7.0710678118654752440e-1)
```

Costruirle è `tools/gcclibs-exos/prepara-gcclibs.sh`. Due cose non ovvie,
entrambe documentate lì:

- **`CC_FOR_BUILD=gcc` esplicito.** GMP sceglie il compilatore per i propri
  generatori di tabelle compilandone uno e *eseguendolo* — e la prova
  riesce con il cross, perché un binario di EX-OS è un ELF32 statico che
  Linux carica, con syscall che hanno i numeri di Linux. Parte, stampa,
  sembra a posto; non combaciano `argc` e `argv`, e il generatore si
  rifiuta di generare.
- **`--host=i486-pc-exos`, non i386.** Non è un ripiego: 486 è il minimo
  che EX-OS già richiede per conto suo (`invlpg` in `kernel/mm/paging.c`).
  Con `i386` si finisce su un difetto di GCC 17 che emette `rolw $8, %eax`
  — rotazione a 16 bit con il registro a 32 — e l'assemblatore rifiuta.

### La libreria standard del C++ gira

```
ex-os:/> /cdrom/bin/provacpp
La libreria standard del C++ dentro EX-OS

  vector+sort : 1 3 5 7 9
  string      : "std::string concatenata" (23 caratteri)
  cerchio     : area = 12566 (x1000)
  quadrato    : area = 9000 (x1000)
  eccezione   : lanciata e ripresa
  out_of_range : presa da dentro la libreria
```

**libstdc++ 25 MB e libsupc++ 1 MB** compilate per `i386-exos`. Il
programma si costruisce con una riga sola, con `g++`, come su qualunque
altro bersaglio:

```sh
i386-exos-g++ -O2 -o provacpp prova-cpp.cpp
```

> ! **Le eccezioni funzionano, e non era scontato.** Sono il pezzo che ha
> bisogno di più cose insieme: `__cxa_throw`, lo svolgimento dello stack,
> le tabelle `.eh_frame`, i descrittori di tipo. Lo svolgimento **legge a
> runtime le tabelle prodotte dal collegatore e le percorre**: è l'unica
> parte del C++ che pretende che il programma caricato in memoria sia
> esattamente come il collegatore l'ha descritto — quindi è anche una prova
> indiretta che il caricamento su richiesta di EX-OS è corretto.

**La riga che mancava da sempre: `extern "C"`.** Nessuno degli header di
EX-OS aveva la guardia `#ifdef __cplusplus`. Il C++ decora i nomi con i
tipi degli argomenti — `printf` diventa `_Z6printfPKcz` — mentre `libc.a` è
compilata da un compilatore C e dentro ha il nome nudo: **ogni programma
C++ chiamava simboli che nell'archivio non esistono.**

Il sintomo era fuorviante, perché arrivava in compilazione e non al link:
la libstdc++ dichiara alcune funzioni della libc con `extern "C"` esplicito,
e il messaggio diceva `conflicting declaration of 'void* memalign(...)'
with 'C' linkage` — cioè «questa dichiarazione è in conflitto con se
stessa».

### I nomi che servono a essere nominati

Perché la libstdc++ compili, la libc deve *dichiarare* molte cose che
EX-OS non farà mai: 40 codici errno di rete e IPC, le costanti `DT_*` per
FIFO, socket e collegamenti simbolici, i `S_IF*`/`S_IS*` corrispondenti.

> ! **Un nome mancante è un errore di compilazione, non un ramo morto.**
> `<system_error>` costruisce `std::errc` da quell'elenco e `<filesystem>`
> scrive `case DT_LNK:` in uno switch: serve la costante, non il
> comportamento. Ritornare sempre 0 da `S_ISLNK()` è la risposta **giusta**
> — su EX-OS un collegamento simbolico non esiste, quindi «questo file è un
> collegamento?» ha davvero risposta no.

I valori sono quelli di Linux e non vanno reinventati: il giorno che i
collegamenti simbolici arrivassero, `S_IFLNK` dovrà valere `0120000` come
ovunque.

### Collegare un programma C vero, dentro EX-OS

Il CD degli strumenti porta anche il **runtime del bersaglio** in
`/exos/lib`: `crt0.o`, `crti/crtn/crtbegin/crtend.o`, `libc.a`, `libgcc.a`,
`libm.a`. Sono oggetti `i386-exos` prodotti dal cross, quindi già codice di
EX-OS: `ld` nativo li legge qui dentro come li legge il cross su Linux.

! **Senza questi il driver arriva a metà.** `gcc -c` ha bisogno solo di
cpp, cc1 e as; `gcc -o programma` ha bisogno anche di crt0, `libgcc.a` e
`libc.a` — e senza si ottiene un errore di `ld` su simboli che non
c'entrano niente col sorgente che si stava compilando.

La prova è `prova-gcc.c`, sul CD insieme al suo assembly generato dal cross
(che fa anche da termine di paragone per quello che `cc1` dovrà produrre):

```
ex-os:/> /cdrom/bin/as -o /pg.o /cdrom/prova-gcc.s
ex-os:/> /cdrom/bin/ld -o /pg /cdrom/exos/lib/crt0.o /pg.o \
             /cdrom/exos/lib/libc.a /cdrom/exos/lib/libgcc.a
ex-os:/> /pg
La catena intera dentro EX-OS

  somma dei quadrati 1..10 : 385   (atteso 385)
  lunghezza del nome       : 5     (atteso 5)
  divisione a 64 bit       : 64   (atteso 64)

Compilato, assemblato e collegato qui dentro.
```

! **La divisione a 64 bit è lì apposta.** È una delle poche cose che il
compilatore non sa fare con un'istruzione: chiama `__divdi3` in `libgcc.a`.
Se libgcc non è stato collegato, il difetto si vede lì e solo lì. Gli altri
due valori sono attesi e scritti nel sorgente — un programma che stampa un
numero sbagliato senza che nessuno sappia quale fosse quello giusto è una
prova che non prova niente.

### `cc1` compila dentro EX-OS

`as` traduce, `ld` collega, le tre librerie di calcolo ci sono, la libm c'è,
**libstdc++ gira** — e **`cc1` compila**. Il compilatore C di GCC,
costruito in canadian cross
(`--build=x86_64-linux --host=i386-exos --target=i386-exos`), legge un
sorgente C dentro EX-OS e ne produce l'assembly, che `as` e `ld`
trasformano in un eseguibile.

**! Il binario va ricostruito.** La prova è stata fatta prima del
passaggio a `time_t` a 64 bit; dopo quel cambiamento `cc1` è stato solo
**rilinkato**, e gli oggetti già compilati continuavano a credere che
`struct timeval` fosse di 8 byte mentre la libc nuova ne scrive 12 — pila
corrotta, sistema fermo. È il promemoria che un cambio di ABI non si
risolve con un link. La ricostruzione completa è **fatta** — il binario
nuovo c'è — ma non è ancora stata riprovata dentro EX-OS: finché non lo
sarà, la riga qui sopra dice «da riprovare» e non «testato».

Sono 41 MB di binario, e girano solo grazie al caricamento su richiesta
(vedi *Le pagine di un programma arrivano quando servono*): il costo
d'avvio non dipende dalla dimensione, e la memoria restituita al kernel
tiene il resto sotto controllo.

Arrivarci ha scoperto tre difetti nella libc, tutti invisibili ai
programmi di EX-OS perché nessuno di loro fa quello che fa un compilatore:

| | |
|---|---|
| `realloc` non ingrandiva **mai** sul posto | la fusione col blocco successivo rifiutava i blocchi non liberi — cioè esattamente il caso da gestire. Si vedeva come un salto a `0xa7a6a5a4`, che sono i byte di riempimento del test letti come puntatore |
| i costruttori globali non venivano chiamati | `cc1` ha 57 voci in `.init_array`; la prima struttura usata era vuota |
| `printf` con `%f` inventava cifre | oltre la diciottesima, e arrotondava 2,5 a 3 invece che a 2 |

### Un binario di terzi porta dentro la libc del giorno in cui è stato collegato

Qui non ci sono librerie condivise: `as`, `ld` e `cc1` hanno **una copia
della libc dentro di sé**, quella con cui sono stati collegati. Correggere
`lib/libc.c` non li tocca. Sembra ovvio detto così, e non lo è affatto
quando il difetto corretto è nell'allocatore.

`ld` andava in page fault appena gli si davano degli archivi da collegare:

```
[FAULT] PID 9 '/cdrom/bin/ld': page fault a 0x00000005
        (protezione, scrittura, EIP=0x080d2e16)
```

L'indirizzo si risolve sul binario non strippato, e non è codice di
binutils:

```
EIP 0x080d2e16  ->  malloc + 0x116
```

! **La conferma sta nella tabella dei simboli, non nel ragionamento.**
Dentro `ld` c'era `heap_fondi_con_succ` e **non** `heap_assorbi_succ` —
cioè la funzione che la correzione di `realloc` ha introdotto. Quel
binario è del 2 agosto: si porta dentro la libc in cui `realloc` non
ingrandiva **mai** sul posto, e il chiamante che credeva di avere più
spazio scriveva oltre la fine. La corruzione non si vede dove nasce, si
vede alla `malloc` successiva.

Collegare un solo `.o` passava: poco traffico di `realloc`. Con `libc.a` e
`libgcc.a` da leggere, bfd fa crescere le tabelle dei simboli e arriva.

#### Il ricollegamento da solo non basta, e crederlo costa un secondo difetto

La prima risposta è stata ricollegare `as` e `ld` contro la libc corretta,
senza ricompilarli: l'allocatore è *implementazione*, l'ABI non cambia,
quindi il relink dovrebbe bastare. `ld` ha smesso di andare in fault e ha
collegato gli archivi. **E `as` ha cominciato a saltare a un indirizzo a
caso** (`EIP=0x6a722690`, pagina assente).

Il ragionamento aveva una premessa non verificata: *fra il 2 agosto e oggi
è cambiata solo l'implementazione*. Non è vero.

```
oggetti dei binutils   2 agosto    typedef long      time_t;
libc di oggi                       typedef long long time_t;
```

`struct stat` contiene **tre** campi `time_t`: è cresciuta di dodici byte
e ha spostato tutti gli offset successivi. Un oggetto compilato con
l'header vecchio la legge alla vecchia maniera mentre la libc nuova la
scrive alla nuova — e quello che ne esce, se finisce in un puntatore a
funzione, è esattamente un salto a `0x6a722690`.

> ! **È lo stesso difetto di `cc1`, non un altro.** Là l'avevo capito
> subito perché il cambio di `time_t` era fresco; qui l'avevo dimenticato
> e ho concluso «basta ricollegare» **prima** di verificarlo. La
> ricostruzione completa dei binutils è l'unica risposta giusta, come per
> `cc1`.

> ! **`ld` ricollegato ha funzionato lo stesso**, e questa è la parte
> istruttiva: nel percorso che collega archivi la `struct stat` sbagliata
> non viene toccata. «Ha funzionato una volta» non è una prova di
> correttezza — è una prova che quel percorso non passa di lì.

Regola generale, valida per qualunque cosa verrà portata qui dentro:

| cos'è cambiato nella libc | cosa basta |
|---|---|
| solo `lib/libc.c` (implementazione) | ricollegare |
| anche `lib/include/libc.h` (tipi, strutture) | **ricompilare tutto** |

Il modo di accorgersi del primo caso è cercare nella tabella dei simboli
una funzione che esiste solo dopo la correzione. Il modo di accorgersi del
secondo è guardare `git diff` sull'header **prima** di decidere, che è
esattamente il passo che qui è saltato.

**Cosa manca ancora:** il programma di guida `gcc`, quello che concatena
`cc1 → as → ld` passando i file intermedi. Oggi i tre passi si danno a
mano.

---

## CD e DVD — driver ATAPI e ISO 9660

```
disk                    il lettore compare come cd0
mount cd0 /cdrom        montaggio manuale (sempre in sola lettura)
ls /cdrom
umount /cdrom           prima di espellere il disco
```

E in `/boot/kernel.cfg`, per averlo **montato all'avvio**:

```ini
[mount]
/cdrom = cd0
```

La riga è attiva nella configurazione predefinita, e può restarlo su qualunque
macchina: un lettore vuoto — o assente — non produce un avviso, solo un
"montaggio saltato" nel log. Un CD assente all'accensione è la condizione
normale, non un problema, e segnalarlo come tale metterebbe una riga fra i
*problemi durante l'inizializzazione* a ogni accensione.

### Le tre cose che un lettore non ha in comune con un disco

**Il blocco è da 2048 byte, non da 512.** La traduzione sta in un punto solo,
`kernel/block/blk.c`: il resto del sistema chiede settori da 512 come per
qualunque disco. Le richieste allineate — cioè quasi tutte, perché ISO 9660
lavora a blocchi — vanno dritte al dispositivo senza copie intermedie.

**La capacità appartiene al disco, non al lettore.** Un `cd0` con zero settori
non è un lettore rotto: è un lettore vuoto, o che nessuno ha ancora sondato. La
finestra viene riempita da `blk_supporto()` quando serve, e azzerata quando il
disco esce.

**Gli errori sono a due livelli.** Il bit ERR dice solo "CHECK CONDITION": il
motivo sta nei dati di *sense*, che vanno chiesti con un secondo comando. Senza
leggerli, «non c'è il disco», «il disco è appena stato cambiato» e «il disco è
illeggibile» sono la stessa cosa — e le prime due non sono errori.

Un disco **inserito a sistema avviato** si monta senza riavviare. Un lettore
appena rifornito però risponde ancora "supporto assente" per un comando o due, e
in emulazione il vassoio resta *aperto*: il driver insiste qualche volta e lo
chiude una volta sola, come fa Linux. La conseguenza va detta — montare su un
lettore lasciato aperto e vuoto lo chiude.

### ISO 9660, e perché Joliet vince quando c'è

Un disco masterizzato con nomi lunghi contiene **due alberi completi**: quello
ISO 9660, con i nomi maiuscoli, troncati e con il numero di versione
(`LEGGIMI.TXT;1`), e quello Joliet, con i nomi veri in UCS-2. Non sono due viste
della stessa struttura: sono due catene di directory separate che puntano agli
stessi dati. `kernel/fs/iso9660.c` sceglie Joliet quando c'è, e dice quale ha
scelto; senza, i nomi che si vedono non sono quelli che l'utente ha scritto.

Sui nomi ISO toglie il `;1` e il punto finale — sono formato, non nome — e li
mostra in minuscolo; il confronto è insensibile alle maiuscole, altrimenti ciò
che `ls` mostra non sarebbe digitabile.

**Sola lettura, e non per pigrizia**: ISO 9660 non ha bitmap di spazio libero né
voci riutilizzabili. Non esiste "aggiungere un file", esiste rifare l'immagine.
Ogni scrittura è respinta con `-30` (EROFS) prima di toccare il volume.

**Rock Ridge non è gestito** — l'estensione Unix annidata nei campi di sistema
dei record. Un disco che la usa resta leggibile: si vedono i nomi ISO o Joliet,
che ci sono comunque.

### `make iso` — il CD degli strumenti

```bash
make iso        # dist/exos-tools.iso
make run-iso    # avvia QEMU con il CD già inserito
```

Un secondo supporto, separato dal floppy e **non avviabile**: ci va ciò che in
1.44 MB non entra e che non serve a tutti. Il floppy resta il supporto di avvio
collaudato; gli strumenti cambiano spesso, pesano, e non devono poter rompere
l'avvio.

Il disco contiene oggi:

```
/leggimi.txt          cos'è questo disco e come si monta
/exos/include/        gli header della libc (stdio.h, stdlib.h, …)
/exos/libc.c          la libc in un file solo
/exos/start.S         il pezzo di avvio che chiama main()
/doc/                 README, note sul kernel, licenza
/bin/                 as, ld, cc1 e i programmi di prova
```

`/exos/` non è documentazione: è ciò che serve per **compilare su EX-OS**.
`/bin` non è più vuota: ci stanno `as` e `ld` di binutils 2.44 e `cc1` di
GCC, cioè la catena di compilazione vera — vedi
[La catena di compilazione dentro EX-OS](#la-catena-di-compilazione-dentro-ex-os).

! Il CD è in sola lettura per costruzione, quindi header e librerie si leggono
da lì ma **l'output di una compilazione deve andare altrove** — cioè su un EX-OS
installato su ext2, o sul floppy.

### Provare il driver senza masterizzare niente

`tools/mkiso.py` genera anche un'immagine sintetica di collaudo, di cui si
conosce ogni byte — utile proprio perché, quando il driver legge un nome
sbagliato, si sa cosa c'era scritto:

```bash
python3 tools/mkiso.py /tmp/test.iso --prova                  # con Joliet
python3 tools/mkiso.py /tmp/solo-iso.iso --prova --senza-joliet
qemu-system-i386 -fda dist/floppy.img -m 32M -boot a -cdrom /tmp/test.iso
```

Lo stesso strumento fa da masterizzatore per qualunque albero di directory:

```bash
python3 tools/mkiso.py /tmp/mio.iso --da /percorso/albero --etichetta "MIO CD"
```

Costruisce **entrambi** gli alberi — nomi ISO 9660 8.3 maiuscoli con `;1` e nomi
Joliet veri in UCS-2 — condividendo i blocchi dei file, e gestisce nomi lunghi,
sottodirectory e collisioni del troncamento a 8.3 (due file distinti che
diventassero lo stesso nome sarebbero un file perso in silenzio).

---

## Arresto e spegnimento

| Comando shell | Effetto |
|---|---|
| `halt` | sincronizza il filesystem, ferma il sistema, **non** spegne |
| `poweroff` / `shutdown` | sincronizza, conta 3 secondi, spegne l'hardware |
| `reboot` | sincronizza e riavvia (reset via 8042, fallback triple fault) |

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

---

## Interfaccia driver

### Disposizione della tastiera: `keymap`

```ini
[kernel]
keymap = it        # us it fr de es uk
```

```
keymap            quale c'è adesso, e quali si possono avere
keymap it         passa a quella italiana, subito
keymap -p         stampa la riga da mettere in kernel.cfg
```

La legge `/dev/kbd.drv` all'avvio, **prima di registrarsi** — cioè prima
che qualcuno possa digitare: leggerla dopo lascerebbe una finestra in cui
i primi tasti vengono tradotti con la disposizione sbagliata, e sono
proprio quelli dell'autoexec. Un nome sconosciuto non ferma niente: il
driver lo dice e tiene `us`, perché una tastiera muta per un refuso nella
configurazione è un sistema che non si può usare nemmeno per correggere
quel refuso.

! **Ogni disposizione sono QUATTRO tabelle, non due**: normale, Shift,
AltGr, AltGr+Shift. Sembra un lusso finché non si prova a scrivere una
funzione — su una tastiera italiana le graffe stanno **solo** su
AltGr+Shift:

```
@  AltGr+ò        [  AltGr+è        {  AltGr+Shift+è
#  AltGr+à        ]  AltGr++        }  AltGr+Shift++
```

Una disposizione che si ferma a tre tabelle dà una tastiera con cui non si
può aprire un blocco, e questo sistema ci porta dentro un editor e un
compilatore C.

! **Le lettere accentate sono byte della code page 437**: la `à` è 0x85,
non UTF-8. È l'unico byte che la VGA disegna. Conseguenza dichiarata: con
quei byte finiscono anche nei nomi di file, e chi legge quei file su Linux
vede caratteri diversi. Non è un difetto della tabella: è che EX-OS non ha
una codifica di sistema, e sceglierne una è una decisione più grande di
una disposizione di tastiera.

! **Non ci sono i tasti morti.** Su una francese o una tedesca `^` e `¨`
scrivono sé stessi invece di aspettare la vocale. Farli funzionare vuol
dire uno stato in più nel driver e una tabella di combinazioni per
disposizione; per ora si dichiara che non ci sono, invece di farli
sembrare rotti.

**Due difetti trovati provandola**, entrambi lontani da dove li si
cercava:

| | |
|---|---|
| la shell buttava via gli accenti | `riga_modifica` accettava `k >= 32 && k < 127`, cioè «solo ASCII». Invisibile finché la tastiera è stata americana: con quella italiana la `ò` arrivava dal driver e la shell la scartava, e il tasto sembrava rotto |
| AltGr non esisteva | `e0 38` finiva nel blocco dei tasti estesi — quello che consegna le sequenze ANSI dei cursori — e usciva dal suo `default: return` prima di arrivare al codice che lo gestiva. Il codice c'era ed era giusto: stava solo dopo |

! **`us` e `it` sono verificate tasto per tasto** in QEMU, mandando il
tasto *fisico* e guardando che carattere ne esce. Le altre sono scritte
dalla disposizione nota e provate solo dove cambiano posizione rispetto a
US: sono utilizzabili, ma chi ha quella tastiera davanti e trova un tasto
sbagliato ha trovato un difetto vero, non un limite.

! **Se si sbaglia disposizione, la via d'uscita è il riavvio.** Non c'è un
comando digitabile da tutte: fra QWERTY le lettere non si muovono, ma su
AZERTY la `a` e la `m` cambiano posto e `keymap it` battuto alla cieca
diventa `keyq,p`. Il cambio a caldo però **non è permanente**: si riavvia e
torna quella di `kernel.cfg`. È il motivo per cui `keymap` non scrive su
nessun file — a scriverlo è `hwconfig`, che conserva quella che trova.

### `hwconfig` — configurare senza leggere niente

```
hwconfig            guarda, propone, chiede, scrive
hwconfig -n         guarda e basta
hwconfig /disco     configura il sistema installato lì dentro
```

`kernel.cfg` si scrive a mano, e per scriverlo bisogna già sapere che i
dischi si chiamano `hd0p1`, che i punti di montaggio non devono esistere,
che i moduli sono processi ring3 e che l'ordine dei comandi di rete non è
modificabile. Sono tutte cose vere, tutte documentate qui sopra, e tutte da
leggere **prima** di poter accendere una macchina.

`hwconfig` le sa già:

```
Cosa c'e' in questa macchina

  tastiera   /dev/kbd.drv — si carica all'avvio, serve alle frecce e a gfedit
  lettore    cd0 — montato all'avvio su /cdrom
  volume     hd0p1  ext2   'dati' — montato su /dati
  rete       scheda Ethernet sul bus PCI — si accende all'avvio
```

Poi mostra i due file che scriverebbe, per intero, e chiede. Il round trip
è verificato: analizza, scrive, e la macchina riparte dal disco con la rete
accesa **senza un solo `[WARN]`**.

! **I file di prima finiscono in `.bak`**, ed è ciò che rende la proposta
accettabile: se la macchina non riparte, quello di prima è lì accanto.

! **Il nuovo file è generato, non modificato.** Il `kernel.cfg` che viene
col sistema è lungo duecento righe di spiegazioni; conservarle vorrebbe
dire un parser INI che le rimette a posto, cioè un programma molto più
grande e con molti più modi di sbagliare. Quello scritto qui è corto e
sostituisce il precedente per intero — detto in chiaro **prima** di
chiedere.

Tre scelte che si vedono solo provandolo:

| | |
|---|---|
| **non guarda quale scheda sia** | la tabella dei modelli sta in `netdetect`, e duplicarla darebbe due elenchi che divergono al primo driver nuovo. L'autoexec generato chiama `netdetect -c`, che quella tabella ce l'ha: a `hwconfig` serve sapere **se** c'è una scheda, non quale |
| **il volume che sarà la radice non finisce in `[mount]`** | il kernel se ne accorgerebbe da solo («è già montato altrove»), ma è una riga che non serve dentro un file che qualcuno leggerà per capire la propria macchina |
| **un'etichetta sfortunata non diventa un punto di montaggio** | un volume etichettato `boot` darebbe `/boot = hd0p1`, che il kernel rifiuta — un `[WARN]` a ogni accensione, e chi lo legge non ha motivo di sospettare l'etichetta del disco. In quel caso si ripiega su `/disco` |

Scrive anche `TMPDIR`, che non è un vezzo: `mkstemp` e il driver del
compilatore ci mettono i file di passaggio, e senza finiscono nella radice
— che avviando da CD è in sola lettura.

Sta **sul floppy**, con `fdisk` e `install`: serve proprio quando si prepara
una macchina, cioè quando il CD magari non c'è ancora. Senza `/dev/pci.drv`
configura montaggi e moduli e dice che la parte di rete non ha potuto
verificarla.

### `help helpconfig` — la procedura, e a che punto sei

```
help helpconfig     (oppure `helpconfig` da solo)
```

Spiega come si accendono i driver — la catena di rete, la configurazione a
mano, la diagnosi, l'autoexec — e **mostra lo stato attuale** chiedendo al
registro IPC chi c'è già:

```
A che punto sei adesso

  [ok]    bus PCI          /dev/pci.drv &
  [manca] scheda di rete   netdetect -c
  [manca] stack IP         /dev/ip.drv &
  [ok]    tastiera         [modules] in /boot/kernel.cfg
```

! **Lo stato è il motivo per cui esiste.** Un elenco di comandi da dare sta
già in questo file; quello che al prompt non si sa è a che punto si è
arrivati. Costa una syscall per servizio e trasforma «ecco la procedura» in
«sei qui». L'esempio sopra è una macchina senza scheda di rete: il bus c'è,
la scheda no, e non è un guasto da inseguire.

Il testo è più lungo di uno schermo da 25 righe e si ferma da solo; le pause
stanno dove cambia argomento, non ogni N righe, perché una pagina
interrotta a metà di un elenco è peggio di una più corta. `q` smette.

### Driver ring3 (modello attuale, da luglio 2026)

Un driver è un normale eseguibile ELF32 **ET_EXEC statico**, come i programmi
di `/bin`. Non esegue istruzioni privilegiate: il kernel media ogni accesso
all'hardware e verifica i permessi a ogni chiamata.

```c
int main(void)
{
    ipc_register("kbd");            /* si fa trovare per nome    */
    ioport_bind(0x60, 5);           /* whitelist porte I/O       */
    irq_bind(1);                    /* IRQ -> messaggi IPC       */

    for (;;) {
        IpcMessage m;
        ipc_recv(&m, buf, sizeof buf);

        if (m.sender_pid == IPC_SENDER_KERNEL &&
            m.type == IPC_TYPE_IRQ_NOTIFY) { /* interrupt hardware */ }
        else                                 { /* richiesta client  */ }
    }
}
```

I client trovano il servizio con `ipc_lookup("kbd")` e dialogano via
`ipc_send`/`ipc_recv`. Riferimento completo: `drivers/kbd/kbd.c` e il
protocollo in `drivers/kbd/kbd_proto.h`.

### Accessi I/O a 16 e 32 bit

Oltre a `ioport_in`/`ioport_out`, che lavorano a byte, un driver ha:

```c
int ioport_in16 (unsigned int porta);                    /* 0..65535, o -errno */
int ioport_out16(unsigned int porta, unsigned int val);
int ioport_in32 (unsigned int porta, unsigned int *out); /* 0, o -errno        */
int ioport_out32(unsigned int porta, unsigned int val);
```

Non sono una comodità. Il registro CONFIG_ADDRESS del bus PCI (0xCF8) **deve**
essere scritto con un singolo accesso a 32 bit: uno a byte o a word non viene
riconosciuto dal ponte come ciclo di configurazione, e siccome 0xCF9 è il
registro di reset di molti chipset, scriverlo a pezzi tende a riavviare la
macchina.

! `ioport_in32` **non restituisce il valore letto**: `0xFFFFFFFF` («nessun
dispositivo») come `int` sarebbe `-1`, indistinguibile da un errore. Il valore
esce dal puntatore. `ioport_in16` non ha il problema e lo restituisce.

La porta deve essere allineata all'ampiezza, altrimenti `-EINVAL`: un accesso
disallineato viene spezzato dal chipset in due cicli e sul bus PCI il secondo
non è più un ciclo di configurazione.

### Il bus PCI: `/dev/pci.drv` e `/bin/netdetect`

L'enumerazione PCI è un **processo ring3**, non codice del kernel: legge
tabelle scritte da BIOS e firmware di terzi, e un ciclo che non termina su un
ponte mal formato dev'essere un processo da rilanciare, non una macchina
bloccata.

```
/dev/pci.drv -l          elenca i dispositivi ed esce
/dev/pci.drv &           registra il servizio "pci" e serve i client
netdetect                schede di rete presenti e driver di ciascuna
netdetect -t             tabella dei modelli riconosciuti
```

Esempio (VirtualBox con la scheda predefinita):

```
ex-os:/> /dev/pci.drv &
[1] 8
pci: servizio 'pci' attivo, 8 dispositivi
ex-os:/> netdetect
00:04.0  1022:2000  AMD PCnet-PCI II / FAST III (Am79C970/C973)
           porte I/O da 0xc140, IRQ 11
           driver: /dev/pcnet.drv
```

Il protocollo è in `drivers/pci/pci_proto.h`. Il server espone lettura della
configurazione e `PCI_MSG_ABILITA`/`DISABILITA` sui bit I/O, memoria e bus
master; **non** una scrittura di configurazione generica, perché riprogrammare
i BAR di un dispositivo che il kernel sta usando (per esempio il controller
ATA) toglierebbe il disco da sotto i piedi a chi di quella scrittura non sa
nulla.

Entrambi vivono **solo sul CD di EX-OS** (`make iso-exos`): il floppy serve ad
avviare e a installare, e gli strumenti di rete senza i driver di rete — che
sul floppy non ci starebbero — non servirebbero a niente una volta lì.

### Interrupt: `irq_bind` e `irq_done` vanno in coppia

```c
irq_bind(11);                    /* da qui gli IRQ arrivano come IPC */
...
/* alla notifica: */
servi_la_scheda();               /* azzera lo stato del dispositivo */
irq_done(11);                    /* SOLO ADESSO si riapre la linea */
```

! `irq_done()` non è facoltativa. Il kernel **maschera** l'IRQ nel PIC
prima di consegnare la notifica, e senza questa chiamata la linea resta
chiusa: il driver riceve un interrupt e poi silenzio.

Il motivo è che un driver ring3 non gira dentro l'interrupt: fra la
notifica e il momento in cui tocca la scheda passano dei tick. Su un IRQ
**a livello** — tutti quelli PCI — il dispositivo tiene la linea alta
finché non gli si azzera il registro di stato, quindi senza mascheramento
l'interrupt riparte subito dopo l'`iret` e il processo driver non riceve
mai la CPU per andare ad azzerarlo. La macchina si ferma, senza panic.

L'ordine conta: prima si serve il dispositivo, poi si riapre. Riaprire con
la linea ancora alta rimette in piedi la tempesta.

### Rete: `/dev/ne2k.drv` e `/bin/nettest`

Il primo driver di rete guida la famiglia NE2000/DP8390 — RTL8029 su PCI e
cloni Winbond/VIA/KTI. Sta in userspace perché quella scheda **non fa DMA
verso la memoria di sistema**: la RAM dei pacchetti è sulla scheda e ci si
arriva da una porta di I/O, quindi il kernel non deve sapere niente di
indirizzi fisici o pagine bloccate.

```
ex-os:/> /dev/pci.drv &
ex-os:/> netdetect -c              # sceglie e avvia il driver giusto
ex-os:/> nettest -a 10.0.2.2
chi ha 10.0.2.2? lo chiede 52:54:00:12:34:56 (10.0.2.15)

  64 byte  52:55:0a:00:02:02 -> 52:54:00:12:34:56  ARP (0x0806)
      ARP risposta: 10.0.2.2 e' 52:55:0a:00:02:02, cerca 10.0.2.15

Risposta ricevuta: 10.0.2.2 ha indirizzo 52:55:0a:00:02:02
```

`nettest` usa **ARP e non ping** di proposito: ARP è il primo scambio
possibile senza avere uno stack, e una risposta dimostra in un colpo solo
che la scheda trasmette, che il frame arriva, che la scheda riceve e che
la catena driver → IPC → programma consegna i byte giusti. Se ARP
funziona, a `ping` manca solo software.

Il protocollo (`drivers/net/net_proto.h`) è quello di **ogni** driver di
rete, non della NE2000: il PCnet parlerà la stessa lingua. Due scelte che
valgono per tutti:

- **Il driver non spinge mai un frame non richiesto.** `ipc_send` blocca
  se la mailbox del destinatario è piena, e driver e stack che si spingono
  dati a vicenda finiscono fermi ognuno dentro la propria `ipc_send`. Si
  usa domanda e risposta (`NET_MSG_RICEVI`), come il driver di tastiera.
- **Un battito ogni 250 ms.** `ipc_notify_irq` non blocca: se la mailbox
  del driver è piena quando arriva l'interrupt, la notifica viene
  scartata e la linea resterebbe mascherata per sempre. La scadenza
  sull'attesa fa sì che una notifica persa costi un ritardo, non
  un'interfaccia morta.

! Una NE2000 **ISA** non si cerca da sola: per riconoscerla bisognerebbe
scrivere sulla sua porta di reset, e se lì c'è un'altra scheda le si
scrive addosso. Va dichiarata: `/dev/ne2k.drv -p 0x300 -q 3`.

### Rete: `/dev/pcnet.drv` — la prima scheda che scrive in RAM da sola

AMD PCnet-PCI II / FAST III (Am79C970, C970A, C971, C972, C973: sul bus si
presentano tutte come `1022:2000`). Parla lo stesso protocollo del ne2k,
quindi lo stack IP non sa quale delle due c'è sotto.

! **La differenza con la NE2000 è tutto.** Quella tiene la memoria dei
pacchetti *sulla scheda*, e ci si arriva da una porta di I/O: per questo è
stato il primo driver: non chiedeva niente di nuovo al sistema. Il PCnet è
un **bus master**: legge e scrive la RAM di sistema da solo, agli indirizzi
**fisici** che gli si sono dati, senza passare dalla MMU.

Da qui due cose che prima non esistevano:

| | |
|---|---|
| il bit **bus master** nel comando PCI | senza, il ponte blocca ogni ciclo che la scheda inizia. I registri si leggono e si scrivono benissimo — quelli passano da noi — ma la scheda non riesce nemmeno a leggere il proprio blocco di inizializzazione |
| **`SYS_DMA_ALLOC`** | memoria fisicamente contigua di cui si conosca l'indirizzo fisico |

> ! **Un indirizzo sbagliato qui non dà un errore.** Dare alla scheda un
> indirizzo virtuale invece di uno fisico non produce un fault e non ferma
> niente: produce una scheda che scrive pacchetti in un punto a caso della
> memoria fisica. Su una macchina piccola quel punto è spesso il kernel, e
> il sintomo arriva minuti dopo, altrove. È il motivo per cui `dma_alloc`
> restituisce i due indirizzi separati e con nomi diversi — `virt` per il
> processo, `fisico` per la scheda.

`SYS_DMA_ALLOC` la può chiedere **solo chi ha già una finestra di porte
I/O**. Non è una difesa rigorosa: è il modo di dire che serve ai driver.
Memoria contigua e non liberabile è la risorsa più scarsa che ci sia, e il
tetto è 64 pagine per processo.

Due trappole del formato, entrambe silenziose:

- **BCNT è in complemento a due** su dodici bit, con i quattro bit sopra a
  uno. Un buffer da 2048 byte si dichiara `(-2048) & 0xFFF | 0xF000`;
  scriverci 2048 in chiaro dà una scheda che crede di avere un buffer di
  2048 byte *negativi*.
- **Il reset si fa in WIO**, prima del passaggio a 32 bit, perché
  l'offset del registro di reset è diverso nei due modi.

```
ex-os:/> nettest -c
inviati        7
ricevuti       7
notifiche IRQ  7
battiti        89
```

! **`notifiche IRQ 7` su 7 frame è la riga che conta**, non `ricevuti 7`.
Il driver guarda la scheda anche a ogni battito: senza quel numero, una
rete che funziona con 250 ms di ritardo sarebbe indistinguibile da una che
funziona. È lo stesso controllo che ha scoperto la cascata del PIC mai
smascherata.

! **`-l` non sonda una scheda già guidata.** Per leggerne lo stato
bisognerebbe resettarla, e se un altro processo la sta usando quel reset
gli porta via la rete senza dare un errore a nessuno dei due. Se il
servizio c'è già, `-l` lo *interroga*: la risposta viene da chi la scheda
la sta usando davvero. È successo alla prima prova, e il sintomo era
illeggibile — `CSR0 = 0x3b, atteso STOP`.

### Lo stack IPv4: `/dev/ip.drv`, `ping`, `ipcfg`

ARP, IPv4 e ICMP stanno in un **processo a sé**, non nel driver:

```
ping ──IPC──> ip.drv ──IPC──> ne2k.drv ──porte I/O──> scheda
```

Tre ragioni, tutte pratiche: questi protocolli sono uguali su qualunque
scheda (metterli nel driver vorrebbe dire riscriverli per la PCnet); lo
stack ha dei **tempi** — scadenze ARP, attese di risposta — mentre il
driver deve solo rispondere all'hardware; e se sbaglia lo stack si riavvia
lo stack, mentre la scheda resta accesa e configurata.

```
ex-os:/> /dev/pci.drv &
ex-os:/> netdetect -c
ex-os:/> /dev/ip.drv &
ip: indirizzo  10.0.2.15
    maschera   255.255.255.0
    gateway    10.0.2.2
ip: servizio 'ip' attivo

ex-os:/> ping 8.8.8.8 -n 3
  60 byte da 8.8.8.8: seq=1 ttl=255 tempo=70 ms
  60 byte da 8.8.8.8: seq=2 ttl=255 tempo=50 ms
  60 byte da 8.8.8.8: seq=3 ttl=255 tempo=60 ms

3 inviati, 3 ricevuti, 0% persi
tempi: minimo 50 ms, medio 60 ms, massimo 70 ms
```

`ipcfg` mostra indirizzo e **contatori**, `ipcfg -r` la tabella ARP.
I contatori sono metà del programma: quando la rete non va, la domanda
non è «va o non va» ma dove si ferma, e `IP ricevuti` a zero, `scartati`
che salgono o `somme errate` che salgono indicano tre punti diversi.

Cosa lo stack **non** fa, detto subito:

- **non frammenta e non riassembla** — un datagramma più grande della MTU
  viene rifiutato, uno in arrivo che è un frammento viene contato e
  scartato;
- **nessuna tabella di routing** — c'è una rete locale e un gateway;
- **niente DHCP dentro lo stack** — l'indirizzo si dichiara (`ip.drv -a …
  -m … -g …` o `ipcfg -a …`); a prenderlo da un server ci pensa il
  programma `dhcp`, che sta sopra UDP come un client qualunque;
- **una richiesta echo per volta** — `ping` è sequenziale per natura.

! `ping` distingue **tre** esiti, non due: risposta ricevuta; nessuna
risposta all'**ARP** (a quell'indirizzo non c'è nessuno — non si è nemmeno
usciti dal cavo); nessuna risposta all'**echo** (il pacchetto è partito, il
problema è più in là). Un unico «non raggiungibile» costringerebbe a
rifare la diagnosi da capo ogni volta.

! Un tempo di `<10 ms` non è uno zero: `uptime_ms()` conta i tick del PIT
a 100 Hz, quindi avanza a scatti di 10 ms. Scrivere «0 ms» dichiarerebbe
una precisione che non c'è.

### Nomi lunghi su FAT (VFAT), in lettura

Un FAT32 scritto da Linux o da Windows si legge con i nomi veri:

```
ex-os:/> ls /disco
appunti di riunione.txt 19
UnNomeMoltoLungoDavveroInterminabile.dati 19

ex-os:/> cat "/disco/appunti di riunione.txt"     funziona
ex-os:/> cat /disco/UNNOME~1.DAT                  funziona anche l'alias
```

Funzionano **entrambe** le vie: il nome lungo e l'alias 8.3. Confrontare
solo col lungo renderebbe impossibile aprire un file col suo alias corto,
che è un nome legittimo e che i programmi vecchi usano.

! **La somma di controllo non è facoltativa.** Ogni voce di nome lungo
porta la somma del nome 8.3 a cui appartiene, e serve a riconoscere le
catene **orfane**: un sistema che non conosce i nomi lunghi può cancellare
la voce 8.3 lasciando indietro i suoi frammenti, e attaccarli al primo
nome 8.3 che capita darebbe a un file il nome di un altro.

! **Solo lettura.** Creare un file con un nome lungo vorrebbe dire
allocare più voci consecutive e inventare un alias 8.3 unico (`NOME~1`,
`NOME~2`…): è un'altra cosa, e non c'è. Un file creato da EX-OS ha un nome
8.3, e si vede.

! **Solo ASCII**: i caratteri sopra `0x7F` diventano `?`. EX-OS non ha una
tabella di caratteri, e inventarne una qui vorrebbe dire scegliere una
codifica per tutto il sistema.

### `mkfs` sceglie il filesystem dalla dimensione

```
mkfs hd0p1        fino a 2 GB → FAT16, oltre → FAT32
mkfs -t ext2 hd0p1
```

Non è una soglia arbitraria: FAT16 arriva a **65524 cluster**, che con
cluster da 32 KB fanno poco più di 2 GB. Sotto quella misura FAT16 è
preferibile — tabella metà più piccola e root directory a dimensione
fissa, cioè meno settori da leggere per fare la stessa cosa.

! **ext2 non entra mai nella scelta automatica**: è un formato che si
chiede, non uno in cui si finisce.

### UDP e DHCP

Lo stack fa anche UDP. Non ci sono prese né descrittori: si apre una
**porta**, e da quel momento i datagrammi per quella porta sono di chi
l'ha aperta. Basta a un client DHCP e a un futuro risolutore DNS; una vera
API a prese si costruirà sopra questa, non al posto suo.

```
ex-os:/> dhcp
dhcp: cerco un server (52:54:00:12:34:56)...
dhcp: offerta di 192.168.76.9: 192.168.76.30

  indirizzo  192.168.76.30
  maschera   255.255.255.0
  gateway    192.168.76.9
  DNS        192.168.76.3
  concessione 86400 s

dhcp: configurato.
```

`dhcp` è un **programma**, non un pezzo dello stack: DHCP sta sopra UDP
come un client DNS, e un errore lì dentro fa fallire un comando invece di
spegnere la rete. `dhcp -n` chiede e stampa senza applicare.

! **Non rinnova la concessione.** Quando scade, va rilanciato. Il rinnovo
vuole un processo che resti acceso a metà del tempo di scadenza, cioè un
programma diverso da questo — che deve poter essere lanciato a mano e
finire.

! Un datagramma per una porta aperta ma senza nessuno in attesa viene
**scartato e contato** (`ipcfg` lo mostra). UDP perde pacchetti per
definizione, e una coda che cresce mentre nessuno legge è un modo lento di
finire la memoria per colpa di chi manda. Chi aspetta un datagramma deve
prenotarne la ricezione **prima** di mandare la richiesta.

### `printf` in virgola mobile

`%f`, `%e`, `%g` e le loro maiuscole, con larghezza, precisione e flag.
Prima consumavano l'argomento e stampavano `<float>`.

! **Le cifre significative si fermano a 18, e oltre si stampano zeri.** È
un numero **misurato**, non stimato: confrontando il motore con glibc su
una dozzina di valori, fino a 18 non c'è una discordanza, a 19 compare la
prima. Un `double` porta al massimo 17 cifre di informazione; quello che
c'è oltre è l'espansione esatta del valore *binario*, che glibc stampa con
un'aritmetica a precisione arbitraria e noi no:

```
printf("%.30f", 0.1)
  glibc  0.100000000000000005551115123126
  EX-OS  0.100000000000000000000000000000
```

Le prime 17 cifre coincidono — è tutto ciò che `0.1` contiene.

! **L'arrotondamento è al pari**, come prescrive lo standard: `%.0f` di
2.5 dà `2`, di 3.5 dà `4`. Con la regola ingenua («da 5 in su sale»),
sommare una colonna di valori arrotondati accumula un errore che cresce
col numero di righe.

**Su 399 confronti con glibc, 390 identici**; i 9 restanti compaiono solo
chiedendo più di 18 cifre significative.

### ! `time_t` è a 64 bit

Non solo per il 2038 — che pure è una scadenza da non scriversi in
partenza nel 2026. Il difetto che l'ha reso urgente è aritmetico: GCC
misura il tempo con

```c
now->wall = tv.tv_sec * 1000000000 + tv.tv_usec * 1000;
```

e con `tv_sec` a 32 bit quella moltiplicazione **trabocca prima di essere
allargata**. Il rapporto dei tempi di `cc1` usciva con fasi da 18 miliardi
di secondi. Non è codice di GCC da correggere: è codice giusto su un
`time_t` giusto.

! E `gettimeofday` prende ora **secondi e microsecondi dalla stessa
sorgente**. Prima i secondi venivano dall'orologio CMOS e i microsecondi
dal contatore dei tick: due orologi indipendenti, e la coppia poteva
**tornare indietro**. Un orologio che torna indietro non dà un errore, dà
intervalli negativi a chi sottrae due istanti. Il prezzo dichiarato: se
qualcuno corregge l'ora di sistema mentre un programma gira, `gettimeofday`
non se ne accorge — un orologio che non torna mai indietro vale di più.

### La cache dei settori: il compilatore va il doppio

Il disco rigido ha una cache di 128 settori (64 KB) in `kernel/block/blk.c`.
Il guadagno, misurato cambiando **una sola costante** e ricostruendo:

| `cc1` che compila | tempo |
|---|---|
| senza cache | **19,61 s** |
| con cache | **10,19 s** |

! **Sulla copia sequenziale da 35 MB non cambia niente** (~80-100 s in
entrambi i casi), e la differenza spiega a cosa serve davvero una cache.
Per arrivare a ogni pagina da 4 KB di un file grande, ext2 legge prima i
**blocchi indiretti** che dicono dove sta quella pagina: richieste piccole
e sempre le stesse, ed è lì che la cache toglie lavoro. I dati veri
passano una volta sola e non hanno niente da riusare.

Ne segue una stranezza che sembra un errore di misura e non lo è: `cc1`
**dal CD** girava più veloce dello stesso `cc1` dal disco rigido senza
cache. Non perché il CD sia veloce — perché ISO 9660 non ha blocchi
indiretti da inseguire e legge 2 KB per comando invece di 1 KB. Con la
cache il disco rigido pareggia il CD.

Tre scelte, e il perché:

- **Solo richieste fino a 8 settori.** Una cache si rovina da sola: farci
  passare i 34 MB di `cc1` sfratterebbe ogni settore utile per riempirla
  di dati che nessuno rileggerà mai.
- **Write-through, non write-back.** La scrittura va sul disco *subito*,
  poi aggiorna la copia. Così `vfs_sync` continua a voler dire quello che
  ha sempre voluto dire, e uno spegnimento brutale non perde niente che
  non fosse già perso. Il write-back sarebbe più veloce e sarebbe **un'altra
  promessa**.
- **La chiave è (disco fisico, LBA assoluto)**, presa *dopo* la traduzione
  di partizione: gli LBA relativi di due partizioni dello stesso disco si
  sovrappongono, e usare quelli darebbe a una i settori dell'altra.

! **Si svuota su `blk_rescan` e `blk_ripartiziona`.** Senza, dopo un
`mkfs` si servirebbero i settori di prima, e il sintomo sarebbe «un
filesystem corrotto appena creato».

### ! `rep insw` invece di 256 chiamate a settore

Il trasferimento PIO di un settore era un ciclo C che chiamava `port_inw`
**256 volte** — e `port_inw` è una funzione vera, non una macro: 256
`call` e altrettante `ret`, più il montaggio a mano dei due byte di ogni
parola. Duemila e passa istruzioni per 512 byte. Ora è **una** istruzione,
in `ata.c` e in `atapi.c`.

! In ATAPI la via veloce vale **solo se la raffica ci sta nel buffer**:
`rep insw` scrive e basta, non sa saltare i byte in eccesso, e una raffica
va *sempre* consumata tutta o il canale resta inutilizzabile. Quando
sborda si torna al ciclo lento.

! Da solo questo cambiamento **non si vede nelle misure**, ed è
un'informazione utile: il costo del disco non è il trasferimento, sono i
comandi. Quella copia da 35 MB sono ~17.000 comandi ATAPI più ~35.000 ATA,
ognuno con la sua attesa di `BSY` e `DRQ`. La leva che manca è
raggruppare le richieste contigue — e sarà anche ciò che renderà sensato
il DMA bus-master, che oggi ridurrebbe il costo del pezzo che già non pesa.

### `/boot/autoexec.sh` — comandi all'avvio

Una riga = un comando, eseguito **esattamente come se fosse digitato**:
stessi built-in, stesse virgolette, stesso `&` per il background. Le righe
vuote e quelle che cominciano con `#` si saltano; una riga che comincia
con `@` viene eseguita senza essere stampata, come nell'autoexec del DOS.

Sul CD di EX-OS ce n'è uno che accende la rete da solo:

```
autoexec> /dev/pci.drv &
autoexec> netdetect -c
autoexec> /dev/ip.drv &
autoexec> dhcp
```

Dopo l'avvio `ping` e `ftp` funzionano senza toccare niente.

! **Lo esegue solo la shell della PRIMA console.** EX-OS ne avvia una per
ognuna delle quattro console virtuali: senza questo controllo l'autoexec
girerebbe quattro volte, e per `/dev/pci.drv &` significherebbe quattro
processi che si contendono lo stesso servizio.

! **La via d'uscita esiste prima di servire.** Un autoexec con dentro un
comando che si blocca renderebbe il sistema inutilizzabile, e il file per
correggerlo sta sul supporto che non si raggiunge più. Quindi:

| | |
|---|---|
| `autoexec=0` in `kernel.cfg` | lo salta (il file si modifica da un'altra macchina) |
| **Alt+F2, Alt+F3, Alt+F4** | danno sempre una shell pulita, anche mentre la prima è impegnata |

Il secondo è quello che conta davvero: non richiede di poter modificare
nulla.

### `!silenced` — l'`echo off` degli script

```
!silenced      da qui in poi i comandi non si vedono piu'
!verbose       si tornano a vedere
@comando       zittisce UNA riga sola
```

! **Zittisce il comando, non il suo risultato**, ed è la distinzione che
rende l'opzione utile: quello che un comando stampa è il motivo per cui lo
si è messo nello script, mentre la riga di comando la si è già scritta.
L'autoexec del CD comincia con `!silenced` e mostra solo l'indirizzo
ottenuto, non i quattro comandi che sono serviti a ottenerlo.

Vale **da dove sta in poi**, non per tutto il file: si può zittire la parte
rumorosa e lasciar vedere quella che interessa. La riga della direttiva non
si stampa mai.

Gli script non sono più solo l'autoexec:

```
source /prova.sh      esegue in QUESTA shell
/prova.sh             lo stesso, per nome
```

! **`source` e non una spawn**: i comandi devono girare nella shell
corrente, altrimenti un `cd` o un `export` dentro lo script sparirebbero
insieme al processo figlio. Un nome che finisce in `.sh` si riconosce
*prima* di provare a lanciarlo, non dopo che la spawn è fallita: la spawn
fallisce per molti motivi, e trattarli tutti come «sarà uno script»
trasforma un errore preciso in un secondo errore che parla d'altro.

### Cronologia dei comandi e modifica della riga

Le frecce **su** e **giù** ripercorrono i comandi già dati (24 di
cronologia); **sinistra**, **destra**, **Home**, **Fine**, **Backspace** e
**Canc** modificano la riga in corso. `Ctrl+C` la abbandona.

! **La riga in corso non si perde.** Chi ha scritto mezzo comando e va a
cercarne uno vecchio con la freccia in su la ritrova scendendo fino in
fondo.

Righe vuote e doppioni consecutivi non entrano in cronologia: chi ripete
lo stesso comando dieci volte non vuole dieci voci da riattraversare.

! **Serve la modalità raw della tastiera**, perché in cooked il driver
assembla la riga e la consegna su Invio — le frecce non hanno modo di
attraversare un flusso di testo. La shell prende quindi la disciplina di
riga su di sé: eco, backspace, cursore.

! **Se il servizio `kbd` non risponde si torna a leggere righe intere**:
si perde la cronologia, non la shell. E il driver torna in cooked da solo
ogni volta che un programma legge da stdin, quindi la modalità si
riafferma a ogni prompt — il che la rende anche autoriparante.

! **Solo la console in primo piano** prende i tasti. Senza quel
controllo tutte e quattro le shell si contendevano la tastiera, e quelle
non visibili la riportavano in cooked togliendola a chi stava scrivendo.

! Il ridisegno usa **solo Backspace**, perché il TTY di EX-OS non ha un
linguaggio di posizionamento del cursore. Conseguenza: su una riga più
lunga della larghezza dello schermo la modifica si vede male — ma la riga
resta corretta, e quello che si legge è ciò che verrà eseguito.

### Nomi con spazi: le virgolette

```
ex-os:/> cat "/disco/appunti di riunione.txt"
contenuto di prova
ex-os:/> cp '/disco/appunti di riunione.txt' /disco/copia.txt
copiati 19 byte in /disco/copia.txt
```

! **Apici singoli e doppi fanno la stessa cosa.** Su una shell Unix la
differenza esiste perché fra virgolette doppie `$VAR` viene espansa e fra
apici singoli no. Qui non c'è nessuna espansione — né di variabili né di
caratteri jolly — quindi le due forme non avrebbero niente da
distinguere. Accettarle entrambe e trattarle uguale è onesto; accettarne
una sola costringerebbe a ricordare quale.

Una virgoletta non chiusa viene **segnalata**: prima l'argomento si
prendeva fino a fine riga in silenzio, e il comando falliva lamentando un
file inesistente dal nome assurdo — il difetto era nella riga, non nel
file. Lo stesso vale per gli argomenti oltre il sedicesimo, che prima
sparivano senza dire niente.

### `ls` — modi di visualizzazione

```
ls -h              elenca tutte le opzioni
ls -mc /bin        a colonne: solo i nomi, il piu' compatto
ls -d              dettagli: dimensione, data e ora
ls -md             dettagliato stile dir: aggiunge gli attributi
ls -a              mostra anche i nomi che cominciano con un punto
ls -p              una pagina per volta (Invio avanza, q smette)
```

```
ex-os:/> ls -md /
data        ora    attr   dimensione  nome
2026-08-04  10:51  D----       <DIR>  BOOT
2026-08-04  10:51  D----       <DIR>  BIN
2026-08-04  10:51  -----      180584  KERNEL.BIN

2 file, 181679 byte    4 directory
```

! **`-d` qui significa «dettagli»**, non quello che significa su Unix (dove
`ls -d` mostra la directory invece del contenuto). È una scelta di questo
progetto, e l'aiuto la dichiara perché nessuno la scopra per tentativi.

! Senza `-a` si nascondono i nomi che cominciano con un punto, `.` e `..`
compresi. È un cambiamento rispetto a prima, quando venivano sempre
mostrati. Su un CD `.` e `..` non compaiono comunque: ISO 9660 non li
consegna (sono due record con nome `0x00` e `0x01`, e il driver li salta).

! Il bit «nascosto» di FAT si **vede** con `-md` ma non nasconde niente.
Guardarlo costerebbe una `statraw()` per ogni voce anche quando si
stampano solo i nomi, e su un floppy si sente; a nascondere è il punto
iniziale, che è la convenzione di tutti i filesystem che EX-OS monta.

**Le date arrivano davvero dal filesystem** (kernel 0.168). Prima
`sys_stat` scriveva zero e nessun programma poteva mostrarle. Ora:

| | |
|---|---|
| FAT12 / FAT16 / FAT32 | il formato è quello nativo, nessuna conversione |
| ext2 | da `i_mtime` (tempo Unix) al formato FAT |
| ISO 9660 | dai sette byte del record di directory |

! Una data **zero significa «questo volume non la tiene»** e i programmi
stampano dei trattini: un 1980 inventato sembrerebbe una data vera. Il
formato copre 1980-2107 — un file ext2 datato prima del 1980 esce senza
data invece che con un anno sbagliato.

### TCP

```
ex-os:/> tcptest example.com 80
connessione a 172.66.147.243:80 ...
connessa (id 1)
mandati 18 byte

HTTP/1.1 403 Forbidden
Server: cloudflare
...
--- ricevuti 408 byte ---
```

DNS → ARP → IP → TCP, dati in entrambi i versi attraverso il NAT. (Il 403
è HTTP: `GET / HTTP/1.0` senza `Host` viene rifiutato da Cloudflare. Il
trasporto ha funzionato — quella risposta lo dimostra.)

! **Solo connessioni in uscita.** Manca il ramo `LISTEN`/`SYN_RECEIVED`
della macchina a stati, che è circa metà del lavoro e serve a fare da
**server**. Il primo cliente di questo TCP è un client FTP in modo
**passivo** (`PASV`), che apre lui stesso anche la connessione dati; il
modo attivo richiederebbe l'ascolto e non funziona comunque dietro un NAT.
Si fa la metà che serve, e si dice che è metà.

Cosa **non** fa, dichiarato in `drivers/net/ip_proto.h`:

| | |
|---|---|
| **niente riordino** | un segmento fuori sequenza si **scarta** e si riconferma: chi l'ha mandato lo ritrasmette. Corretto ma non efficiente — tenere i pezzi vuole una lista con le sue scadenze, ed è dove un TCP giovane prende i bug peggiori |
| **niente controllo di congestione** | si manda quanto la finestra dell'altro consente. Su rete locale non cambia nulla; su Internet significa essere maleducati sotto perdita |
| **RTO fisso** | non si misura il tempo di andata e ritorno: si raddoppia da 600 ms. Misurarlo davvero (Karn, Jacobson) è il passo dopo |
| **niente SACK, window scaling, timestamp** | |

! **I numeri di sequenza si confrontano con la sottrazione, mai con `<`.**
Sono a 32 bit e si avvolgono: `a < b` a cavallo dell'avvolgimento dà la
risposta rovesciata, una volta ogni 4 GB trasmessi — cioè raramente, e
sempre quando la connessione è carica.

! `IP_MSG_TCP_APRI` può rispondere **`-EAGAIN`**: significa che lo stack
ha appena chiesto l'ARP del prossimo salto. Non è un fallimento, è «fra un
istante». Infilare l'attesa dell'ARP dentro la macchina a stati di TCP
vorrebbe dire due scadenze annidate sulla stessa connessione.

### `ftp` — client FTP

Sul CD di EX-OS, insieme agli altri strumenti di rete.

```
ex-os:/> ftp 10.0.2.2 ls
220 Server pronto
331 Serve la password
230 Accesso eseguito
-rw-r--r-- 1 exos exos     4053 Jan  1 00:00 grande.txt
-rw-r--r-- 1 exos exos       56 Jan  1 00:00 leggimi.txt

ex-os:/> ftp 10.0.2.2 get grande.txt /disco/copia.bin
4053 byte in '/disco/copia.bin'
```

Senza comando si apre una riga di comando: `ls`, `cd`, `pwd`, `get`,
`put`, `bye`.

! **Solo modo passivo (`PASV`).** In modo attivo è il *server* a
ricollegarsi al client, che deve quindi mettersi in **ascolto** — e il TCP
di EX-OS non sa farlo, di proposito. Non è un ripiego: il modo attivo non
funziona comunque dietro un NAT, ed è per questo che ogni client serio usa
`PASV` da vent'anni.

! **FTP manda la password in chiaro.** Non è un difetto del programma, è
il protocollo: chiunque stia sul percorso legge utente e password così
come sono. Il client lo dice all'accesso, una volta, invece di lasciarlo
intendere. L'alternativa si chiamerà SFTP o FTPS quando ci sarà TLS — non
«ftp con una toppa».

! Se il server annuncia in `PASV` un indirizzo diverso da quello a cui
siamo connessi, il client **usa quello vero**: un server dietro NAT
annuncia spesso il proprio indirizzo privato, che da fuori non è
raggiungibile. La porta è l'informazione utile; l'indirizzo lo sappiamo
già.

Per provarlo senza un server vero c'è `tools/ftpserver-prova.py` — ! che
**non è un server FTP**: fa entrare chiunque e serve una directory sola,
va lanciato su localhost per il tempo di una prova.

### `telnet` — sessione interattiva

```
telnet nome-o-indirizzo [porta]      si esce con Ctrl+]
```

! **Telnet manda tutto in chiaro, password compresa.** Non è un difetto
del programma, è il protocollo: chiunque stia sul percorso legge nome
utente, password e tutto quello che si scrive dopo. Il client lo dice una
volta all'avvio invece di lasciarlo intendere. L'alternativa si chiamerà
SSH quando ci sarà TLS — non «telnet con una toppa».

**Come fa a sentire la rete e la tastiera insieme.** Non c'è `select()`, e i
fili sono arrivati dopo questo programma, ma c'è una cosa migliore per questo
caso: in EX-OS tutto passa dalla stessa cassetta postale. Si *prenota* una ricezione allo
stack IP (`IP_MSG_TCP_RICEVI`), si *prenota* un tasto al servizio tastiera
(`KBD_MSG_READKEY`), e poi si aspetta con un solo `ipc_recv_timeout()`
guardando **chi** ha risposto. Le due prenotazioni si riarmano
indipendentemente: se il server tace si continua a ricevere tasti, se
nessuno digita si continua a ricevere dati.

! **La scadenza serve anche quando non scade niente**: la modalità raw
della tastiera se ne va da sola ogni volta che qualcun altro chiede una
riga, e senza un risveglio periodico che la riafferma il programma
resterebbe in attesa di un tasto che il driver non consegnerà mai.

**La negoziazione delle opzioni non si può saltare.** Telnet intreccia ai
dati dei comandi che cominciano con il byte 255 (IAC). Un client che li
ignorasse stamperebbe caratteri di controllo e — molto peggio —
lascerebbe il server ad *aspettare*: parecchi server non mandano nemmeno
il `login:` finché non hanno finito di negoziare.

| | |
|---|---|
| **rifiuta tutto quello che non sa fare** | al `DO` di un'opzione sconosciuta si risponde `WONT`, al `WILL` si risponde `DONT`. Il silenzio non è un rifiuto: è un server che aspetta |
| **non risponde mai a una risposta** | due implementazioni educate che replicano sempre si rimpallano la stessa opzione all'infinito. Si tiene lo stato di ciò che si è già concesso |
| accettate | `ECHO` e `SGA` dal server, `TTYPE` e `NAWS` verso il server |
| `IAC IAC` | è un byte 255 nei dati, non un comando |

Lo stato del riconoscimento è **statico e non locale**, e conta: una
sequenza IAC può essere spezzata fra due segmenti — TCP consegna byte,
non messaggi — e uno stato azzerato a ogni chiamata farebbe stampare metà
comando e rispondere all'altra metà come se fosse un comando diverso.

Due traduzioni della tastiera che non sono ovvie:

- **Invio è `CR LF`**, non un solo `LF`: il terminale virtuale di telnet
  vuole che un CR sia sempre seguito da LF o NUL, e i server che applicano
  la regola alla lettera con un LF solo non fanno niente.
- **Backspace si manda come `0x7F`**, non come lo `0x08` che il tasto
  produce qui: sui sistemi Unix il carattere di cancellazione predefinito
  è DEL, e mandando `0x08` la riga non si accorcia e compare `^H` — che
  sembra un difetto della tastiera mentre è una convenzione dall'altra
  parte.

Per provarlo senza esporre una shell c'è `tools/telnetserver-prova.py` —
! che **non è un server telnet**: fa l'eco e risponde a comandi finti.
Serve perché mettere in ascolto un `telnetd` vero, che dà una shell senza
cifratura, sarebbe una pessima idea su qualunque macchina. La prova vista
dal suo lato:

```
  <- DO ECHO
  <- DO SGA
  <- WILL TTYPE
  <- WILL NAWS
  <- schermo: 80x25
  <- terminale: 'EXOS'
  riga: 'ciao mondo'
```

### Risoluzione dei nomi: `host`, e `ping` per nome

```
ex-os:/> host one.one.one.one
one.one.one.one ha indirizzo 1.0.0.1  (risposta da 10.0.2.3)

ex-os:/> ping www.google.com -n 2
ping www.google.com (142.251.151.119) con 32 byte di dati
  60 byte da 142.251.151.119: seq=1 ttl=255 tempo=50 ms
```

Il risolutore è un **modulo** (`lib/dns.c`), compilato dentro i programmi
che ne hanno bisogno — non un servizio e non parte dello stack. DNS sta
sopra UDP esattamente come DHCP: un errore nell'analisi di una risposta
scritta da un server sconosciuto deve far fallire un comando, non spegnere
la rete. E non ha stato da conservare fra una chiamata e l'altra, quindi
un processo dedicato costerebbe soltanto un'altra cosa da avviare e
sorvegliare.

! **I puntatori di compressione dei nomi si seguono solo in lettura, con
un tetto ai salti.** In una risposta DNS un nome può finire con un
puntatore a un punto precedente del messaggio; quel puntatore lo scrive il
server, e niente gli impedisce di farlo puntare a sé stesso. Per *saltare*
un nome non si segue affatto — un puntatore chiude il nome, e la lunghezza
è nota.

! `ping` stampa nome **e** indirizzo quando gli si dà un nome: senza,
davanti a una risposta strana non si distingue un guasto del DNS da uno
della rete.

`host` esiste per poter provare il risolutore da solo. Quando `ping nome`
non funziona, la domanda è se sia rotto il ping o il DNS, e senza questo
comando bisogna indovinare.

### Interfaccia `drv_*` (modello precedente, kernel-space)

```c
int  drv_init(void);
int  drv_read(void *buf, size_t n);
int  drv_write(const void *buf, size_t n);
int  drv_ioctl(int cmd, void *arg);
void drv_exit(void);
```

Usata ancora da `drivers/tty/tty.c` (compilato dentro il kernel) e da
`drivers/floppy/floppy.c` (modulo ET_DYN non più caricato). I moduli ET_DYN
girano in ring0 e vanno riscritti contro il modello sopra.

---

## Piano di sviluppo (Strategia D Ibrida)

- [x] **Fase 1a** — Bootloader Stage1 + Stage2 + FAT12 read
- [x] **Fase 1b** — Kernel entry, GDT, IDT, ISR, VGA, kprintf
- [x] **Fase 1c** — Physical Memory Manager (E820 + bitmap)
- [x] **Fase 1d** — Paginazione x86 + heap kernel (kmalloc)
- [x] **Fase 2a** — Scheduler preemptive 100Hz + context switch
- [x] **Fase 2b** — Syscall interface int 0x80
- [x] **Fase 3**  — TTY driver + FAT12 R/W kernel + ELF loader
- [x] **Fase 4**  — Shell utente + cfg reader
- [~] **Fase 5**  — Driver in userspace (ring3): tastiera fatta, floppy da fare
- [~] **Fase 6**  — Sistema ospitante: libc POSIX, `as`, `ld` e `cc1` nativi
                    fatti; manca il programma di guida `gcc`
- [~] **Fase 7**  — Rete: PCI, NE2000, ARP/IPv4/ICMP/UDP/TCP, DHCP, DNS e un
                    client FTP fatti; TLS da fare

**Stato Fase 4 (luglio 2026)**: la shell parte come primo processo ring3, legge
`/boot/kernel.cfg`, ed esegue programmi esterni (`hello`, `ls`, `cat`) come task
separati con ritorno al prompt. Il deadlock del PIC che bloccava ogni programma
lanciato dalla shell è risolto — vedi `HANDOFF.md`.

**Stato Fase 5 (30 luglio 2026)**: `/dev/kbd.drv` è il **primo driver di EX-OS
che gira davvero in ring3**. È un processo con la propria page directory che
non esegue istruzioni privilegiate né chiama simboli del kernel: legge gli
scancode con `SYS_IOPORT_IN`, riceve gli IRQ1 come messaggi IPC via
`SYS_IRQ_BIND`, e consegna le righe digitate al TTY con `SYS_IPC_SEND`. Il TTY
resta in-kernel per la VGA ma per l'input è un client del servizio.

Il driver floppy è ancora un modulo ET_DYN kernel-space e l'accesso al floppy
resta servito dal FAT12/FDC interno al kernel. Analisi e passi necessari in
`KERNEL_CORE_NOTES.md`, punto 5; dettagli di progetto e trappole in
`HANDOFF.md`.

**Stato Fase 6 (2 agosto 2026)**: la libc è cresciuta fino a reggere codice
scritto per POSIX, e la prova è che **binutils 2.44 gira dentro EX-OS**.
Portarlo ha scoperto tre difetti del kernel che nessun programma di EX-OS
poteva mostrare, perché tutti scrivevano un file dall'inizio alla fine:

- i descrittori ancora aperti alla terminazione non li chiudeva nessuno —
  uno slot VFS perso per file, ed `EMFILE` dopo 64 volte;
- sul **floppy** la scrittura ignorava la posizione del descrittore e si
  accodava sempre in fondo (su ext2 e FAT16/32 no: lì l'offset arrivava);
- scrivere all'**inizio** di un settore lo azzerava per intero, cancellando
  i byte che c'erano dietro.

Tutti e tre invisibili finché nessuno torna indietro in un file. Un
qualunque scrittore di ELF lo fa.

**Stato Fase 6, seguito (agosto 2026)**: **`cc1` compila C dentro EX-OS** e produce
assembly vero, che `as` e `ld` trasformano in un eseguibile. Arrivarci ha
scoperto altri tre difetti, tutti nella libc e tutti invisibili ai programmi
di EX-OS:

- `realloc` non ingrandiva **mai** sul posto — la fusione col blocco
  successivo rifiutava i blocchi non liberi, cioè proprio il caso da gestire;
- i costruttori globali di `.init_array` non li chiamava nessuno: le 57
  voci di `cc1` non venivano eseguite e la prima struttura usata era vuota;
- `printf` con `%f` inventava cifre oltre la diciottesima e arrotondava
  2,5 a 3 invece che a 2.

**Stato Fase 7 (agosto 2026)**: la rete parte dal bus e arriva a un
trasferimento FTP verificato byte per byte. Il difetto che è costato di più
non era nella rete: **la linea 2 del PIC — la cascata — non veniva mai
smascherata**, quindi nessun IRQ da 8 a 15 poteva raggiungere la CPU. Si è
visto perché il tempo di andata e ritorno di `ping` era *esattamente* il
battito del driver: non stava rispondendo la rete, stava rispondendo il
timer. I contatori `notifiche IRQ 0, battiti 131` lo hanno detto in chiaro.

**Cosa manca ancora**, in ordine di quanto darà fastidio:

| | |
|---|---|
| **`gcc` come programma di guida** | `cc1` compila e produce assembly, `as` e `ld` ci sono: manca chi li concatena passando i file intermedi |
| **posizione condivisa fra fd duplicati** | `dup()` funziona, ma i due descrittori tengono ognuno il proprio offset |
| **TCP fuori sequenza** | i segmenti arrivati in disordine si scartano invece di riordinarli, e l'RTO è fisso invece che misurato |
| **DNS: solo record A** | i CNAME si saltano invece di seguirli; se la risposta non contiene già il record A finale, il nome non si risolve |
| **rinnovo DHCP** | `dhcp` prende la concessione e finisce; il rinnovo vuole un processo che resti acceso |
| **TLS** | senza cifratura non esistono HTTPS né SFTP; il primo passo è una sorgente di entropia, non il protocollo |
| **exFAT** | ! non esiste come filesystem: prima va implementato, poi ha senso un chkdsk |
| **`rename` fra directory** | oggi ENOSYS: sarebbe una copia più una cancellazione, cioè un'altra operazione |
| **DMA per il disco** | oggi 0,75 MB/s in PIO |

---

## Licenza

EX-OS è software libero: puoi ridistribuirlo e/o modificarlo
secondo i termini della GNU General Public License versione 2
pubblicata dalla Free Software Foundation.

Vedi il file `LICENSE` per il testo completo.

---

*EX-OS — "Il sistema si estende, il kernel rimane piccolo."*

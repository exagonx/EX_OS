# NASM per l'ospite `i386-exos`

Come `tools/gcc-exos/` e `tools/binutils-exos/`, questa directory **non
contiene NASM**: contiene ciò che va aggiunto a un albero dei sorgenti perché
NASM giri **dentro** EX-OS, più la ricetta per costruirlo.

```
applica.py          mette (o toglie) l'ospite i386-exos in un albero NASM
prepara-nasm.sh     configure + make + verifica (incrociata)
```

## Uso

```bash
# 1. i sorgenti (questo script NON scarica niente, di proposito)
wget https://www.nasm.us/pub/nasm/releasebuilds/3.02/nasm-3.02.tar.xz
tar xf nasm-3.02.tar.xz

# 2. costruzione
tools/nasm-exos/prepara-nasm.sh ./nasm

# 3. sul CD degli strumenti
make iso
```

Provato con **NASM 3.02**. Il risultato finisce in `/exos/bin/nasm`,
`/bin/nasm` e altrettanto per `ndisasm`.

## Perché un secondo assemblatore, se c'è già `as`

Non è un doppione: sono **due lingue**. `as` (GNU) parla la sintassi AT&T ed è
fatto per ricevere quel che sputa il compilatore — nessuno scrive `as` a mano
se può evitarlo. NASM parla la sintassi **Intel**, che è quella dei manuali di
Intel e AMD, dei settori di avvio, dei tutorial e di quasi tutto l'assembly
scritto da una persona.

E soprattutto: **EX-OS è un sistema operativo**. Chi impara a scriverne uno
comincia da sedici bit e da un settore di avvio, e quella roba è scritta in
NASM nel novantanove per cento dei casi. Un sistema che sa compilarsi il C ma
non sa assemblare un `org 0x7c00` è monco proprio nel punto in cui dovrebbe
essere più forte — e il settore di avvio di EX-OS stesso (`boot/stage1.asm`) è
scritto in NASM.

## Che cosa cambia nell'albero: due righe

| File | Modifica |
|---|---|
| `autoconf/helpers/config.sub` | `exos*` fra i sistemi operativi ammessi |
| `nasmlib/path.c` | `__exos__` fra i sistemi con i percorsi di Unix |

! **QUI SI TOCCA L'OSPITE, NON IL BERSAGLIO**, ed è tutta la differenza con
binutils e GCC. NASM non ha un «bersaglio» nel senso di autoconf: i formati
d'uscita — `elf32`, `bin`, `coff`, `macho` — li produce **tutti sempre**, e la
scelta la fa chi lo usa con `-f`. Non c'è niente da insegnargli su EX-OS come
formato: `nasm -f elf32` fa già esattamente ciò che serve, ed è quello che il
nostro `ld` sa collegare. L'unica cosa che mancava era che girasse qui.

### `config.sub`

Senza, qualunque `configure` risponde `Invalid configuration 'i386-exos': OS
'exos' not recognized` e si ferma. È la stessa riga che serve a binutils, a
GCC e a make.

### `nasmlib/path.c`

NASM sceglie lo stile dei percorsi dai macro del compilatore: MS-DOS, Unix,
Mac classico, VMS. Per un sistema che non riconosce prende `PATH_UNKNOWN`, e
`separators` resta **indefinito**:

```
path.c:204:21: error: 'separators' undeclared (first use in this function)
```

che per fortuna è un errore rumoroso. La riga giusta non è far dire al nostro
GCC di essere Unix — non lo è — ma dire **lì** che EX-OS ha i percorsi fatti
come quelli di Unix: la barra come unico separatore, nessun concetto di
volume. È vero, e sta in una riga.

! **Il cross definisce `__exos__` e `__EXOS__`** (vedi `tools/gcc-exos/exos.h`),
ed è su quello che si aggancia la riga.

## Due cose che sono costate un giro di compilazione

! **`marca()` scrive il commento nella lingua del file.** La riga «modificato
il ...» che `applica.py` mette in testa a ogni file toccato era sempre un
`# ...`: in `config.sub` è un commento, in `path.c` è una **direttiva del
preprocessore**, e il compilatore risponde `invalid preprocessing directive
#EX`. Adesso i file `.c` e `.h` la ricevono come `/* ... */`.

! **L'albero del repository di NASM non ha `configure`**, ha `autogen.sh`: è un
albero di sviluppo, non un pacchetto di rilascio. `prepara-nasm.sh` lo genera
da solo se manca; da un tarball di rilascio quel passo non serve.

## Le prove, sul CD

```
/cdrom/prova-nasm.asm     ELF a 32 bit: nasm -f elf32, poi ld, poi si esegue
/cdrom/prova-nasm16.asm   un settore di avvio: nasm -f bin, poi ndisasm
```

La prima è la gemella di `prova.s` (che è per `as`) e serve a confrontare le
due sintassi riga per riga. La seconda è quella che dimostra il motivo per cui
NASM sta sul CD: `org 0x7c00`, sedici bit, nessun linker, e il giro completo —
dai byte si torna alle istruzioni con `ndisasm` e si vede che sono quelle che
si erano scritte.

## Licenza

NASM sta sotto una **BSD a due clausole**: chiede che l'avviso di copyright
resti nelle copie, e non chiede di marcare le modifiche. `applica.py` le marca
lo stesso, con la stessa riga che mette in binutils e in GCC: chi apre uno di
quei file dopo di noi deve vedere subito che non è l'originale, e la licenza
non è l'unico motivo per cui vale la pena.

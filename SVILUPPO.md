# Sviluppare EX-OS su un PC nuovo

Questa guida dice **come si prepara una macchina** per lavorare su EX-OS e
**dove sta scritto tutto il resto**. Non ripete i documenti che esistono: li
indica. Si tiene aggiornata a ogni novita' che cambia il modo di preparare,
compilare, provare o pubblicare.

> **La regola del progetto: una novita' non e' finita finche' non e' scritta.**
> Il codice nuovo porta il suo commento; la giornata va nel diario; un porting
> ha il suo `tools/<nome>/leggimi.md`; e se cambia cio' che serve sulla macchina,
> si aggiorna questo file. Chi riprende il lavoro altrove deve poterlo fare
> leggendo, senza chiedere.

---

## 1. Due modi di avere l'albero

**La copia speculare della cartella** (il modo che si usa oggi fra le macchine
di sviluppo). Si copia l'intera directory del progetto, con `.git/` dentro: cosi'
arrivano anche gli alberi di terzi, gli strumenti locali e le configurazioni
private, che git non contiene (vedi sotto).

**Il clone da GitHub** (`https://github.com/exagonx/EX_OS`). Porta solo cio' che
e' nostro. Gli alberi di terzi vanno riscaricati, ognuno come dice il suo
`leggimi.md`.

### Cio' che NON e' in git, e perche'

| Directory | Cos'e' | Come si riottiene |
|---|---|---|
| `gcc/`, `rust/`, `quickjs/`, `nasm/`, `openssl/`, `openlibm*/` | sorgenti di terzi | `tools/gcc-exos/`, `tools/rust-exos/`, `tools/quickjs-exos/`, `tools/nasm-exos/` (`leggimi.md`) |
| `FreeBASIC-*-source-bootstrap/`, `freebasic/` | FreeBASIC | `tools/freebasic-exos/leggimi.md` |
| `make/`, `sed/`, `awk/`, `grep/`, `coreutils/`, `gnu/` | gli strumenti GNU portati | `tools/make-exos/leggimi.md` e i commenti del Makefile |
| `liberation-fonts/` | i caratteri | si riscaricano (Liberation Fonts, licenza OFL) |
| `firefox-main/` | i sorgenti di Firefox per **Exilla** | `tools/exilla/leggimi.md` |
| `3p_app_source/`, `drv_prop/` | programmi e driver di terzi con licenze loro | commenti in `.gitignore` |
| `exagonx/` | configurazione del server di aggiornamento (contiene una password) | solo nella copia speculare: `exagonx/LEGGIMI.md` |
| `tools/locali/` | attrezzi di lavoro di questa macchina (mappa dei sorgenti, confronto di istantanee, script di taglio) | solo nella copia speculare: `tools/locali/leggimi.md` |
| `dist/*.iso`, gran parte di `dist/` | i prodotti della compilazione | si rifanno con `make` |

`.gitignore` spiega, sezione per sezione, il perche' di ogni esclusione.

---

## 2. La macchina: Debian 12 o 13

La macchina di sviluppo ha Debian 13 (GCC 14.2); anche Debian 12 (GCC 12.2)
compila tutto e rifa' l'ISO. Gli oggetti in `build/` escono diversi fra i due,
ed e' normale.

! **UN make FALLITO SUL FLOPPY lascia `dist/floppy.img` scritto a meta'**, ed
e' un file tracciato: si rimette con `git checkout HEAD -- build
dist/floppy.img`. Vedi `@FLOPPY-PIENO` in `in_lavorazione.txt`.

! **MEGA E I FILE COL PUNTO.** Se la cartella viaggia con MEGA, il suo
`.megaignore` non deve avere `-:.*`, o `.git/` e `.gitignore` non arrivano.
MEGA mette i file a 777: `git config core.fileMode false`. E git su un PC
alla volta.

Pacchetti dell'host (quelli che il Makefile e gli script di prova chiamano):

    sudo apt install build-essential gcc-multilib g++-multilib nasm mtools \
        qemu-system-x86 python3 util-linux e2fsprogs curl git

- `gcc-multilib`: il Makefile compila con `gcc -m32` (e `ld -m elf_i386`).
- `util-linux` (`sfdisk`) ed `e2fsprogs` (`debugfs`): le prove su disco
  preparano immagini partizionate e ci leggono e scrivono file dall'host.
- `qemu-system-i386` (in `qemu-system-x86`): tutte le prove girano in QEMU.
- ImageMagick (`convert`) e' facoltativo: `tools/ppm2png.py` fa lo stesso.

Il cross-compilatore storico `i686-elf` si installa con
`tools/install_crosscompiler.sh` (vedi README, «Prerequisiti»). Per i
programmi C++ e per il GCC che gira dentro EX-OS c'e' la toolchain
`i386-exos` (`CROSS_SYSROOT`, predefinito `~/exos-cross/i386-exos`): come si
costruisce e' in `tools/gcc-exos/leggimi.md`; `as`, `ld` e `nasm` nativi in
`tools/binutils-exos/` e `tools/nasm-exos/`.

**Memoria**: con 4 GB di RAM GCC si ricostruisce con `-j1` e il resto con
`-j2`. Con 32 GB non ci sono limiti pratici.

---

## 3. Compilare

    make all          il floppy (dist/floppy.img)
    make iso-exos     il CD di EX-OS (dist/exos.iso), avviabile
    make iso          il CD degli strumenti (gcc, as, ld, header, doc)
    make hd           un disco avviabile
    make fixsys       la radice in RAM (in QEMU vuole EXOS_RAM=128M)

Il README, «Build e test», ha l'elenco completo e le opzioni.

! **Mai ricostruire mentre QEMU gira**: riscrivere `floppy.img` o l'ISO sotto
una macchina accesa produce guasti che si vedono ore dopo.

! **La prima `make` in una copia senza i file `build/.flag-*` cancella gli
oggetti committati**: vedi il commento nel Makefile sui segnaposto dei flag.

! **Se cambia un tipo condiviso** (una struttura di una libreria condivisa,
per esempio `CssRegola` in `lib/excss`), si ricostruisce ANCHE chi la usa:
ricollegare non basta, e nessuno avvisa.

---

## 4. Provare

**Le suite sull'host** (veloci, niente QEMU):

    make prova-exjs prova-exdom prova-exqjs prova-excss prova-exhtml \
         prova-exhttp prova-biscotti prova-exfont prova-tls

**In QEMU**, con `tools/qemu_drive.py`: avvia la macchina, digita i comandi
dati come argomenti (`"comando@secondi"`), manda tasti (`key:...`) e comandi al
monitor (`mon:...`), fotografa lo schermo (`foto:percorso.ppm`) e stampa la
seriale. Le variabili:

    EXOS_ISTANZA   un nome per ogni macchina accesa insieme (senza, due
                   prove parallele si cancellano l'uscita a vicenda)
    EXOS_CDROM     avvia da un'ISO (con EXOS_NO_FLOPPY=1)
    EXOS_RAM       la memoria (predefinita 32M)
    EXOS_QEMU_EXTRA  opzioni in piu' (rete: -netdev user,id=n1 -device ne2k_pci,netdev=n1;
                   un disco: -drive file=...,format=raw,if=ide)

! **Niente `pkill` nelle prove**: `qemu_drive.py` si ripulisce da solo.
! **Il mouse di QEMU e' relativo**: le prove si pilotano da tastiera, o col
mouse a passi di dieci.
! **ExWin prende lo schermo da solo**: Alt+F1 per tornare alla shell prima di
lanciare un programma, Alt+F5 per vederlo.

Le prove esistenti sono script in `tools/prova_*.sh`: ognuno dice nella testa
cosa prova e cosa gli serve. Il sito di prova del browser e' in
`tools/prove/sito/` (`leggimi.md` e `servi.py`).

---

## 5. Pubblicare

- **Il commit**: il messaggio si scrive in `messaggio-commit.txt` (fuori da
  git), poi `./gitupdate.sh` aggiunge, committa e spinge. Commenti nel codice e
  messaggi di commit **in inglese**; diari, script e stringhe a schermo in
  italiano.
- **Il repository di rete per `netupdate`**: `exagonx/repo-update.sh` e
  `exagonx/pubblica.sh` (solo nella copia speculare, perche' c'e' la password
  del server). La procedura e' in `exagonx/LEGGIMI.md` e, per chi pubblica un
  repository suo, in `manuali/installazione/06-pubblicare-il-repo.md`.
- **Sulla macchina vera** i binari arrivano con `pubblica.sh` + `netupdate`,
  non a mano: cosi' si prova anche netupdate.

---

## 6. Dove sta scritto il resto

| Documento | Cosa contiene |
|---|---|
| `README.md` | che cos'e' EX-OS, com'e' fatto, build e test |
| `RIPRENDERE.md` | **il diario**, giornata per giornata: si sfoglia con `tools/diario.sh elenco`, `mostra N`, `cerca parola` |
| `in_lavorazione.txt` | **cio' che e' aperto**, con un indice di compiti etichettati (`@NOME`) |
| `correzioni.txt` | le richieste di chi usa il sistema (`!` urgente, `$` a comodo) |
| `diario_browser.txt` | EXBrowser: siti veri, cosa manca, cosa si e' fatto e provato |
| `KERNEL_CORE_NOTES.md` | il kernel |
| `HANDOFF.md` | i passaggi di consegne delle sessioni di agosto |
| `DIREZIONE.md` | dove va il progetto |
| `manuali/installazione/` | i manuali per chi installa |
| `tools/*/leggimi.md` | un porting o uno strumento ciascuno |

---

## 7. Le regole della casa, in breve

- Stringhe a schermo in **ASCII**: la console e' code page 437; gli accenti
  stanno nei commenti.
- I dati di un programma stanno in `$HOME/.app/<programma>/`, mai in un
  percorso fisso.
- Un percorso che scriverebbe sopra un file esistente si chiede prima.
- Un file grande si spezza cosi': prima la mappa fatta da uno script, poi il
  file nuovo con i legami scritti, e solo dopo, eventualmente, la libreria.
- Un commento che spiega un numero e' una prova gia' pagata: prima di cambiare
  la costante, si rilegge.

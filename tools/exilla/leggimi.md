# Exilla — Firefox portato su EX-OS

Exilla e' il browser completo di EX-OS: il motore Gecko di Mozilla Firefox,
portato. **EXBrowser resta** il browser leggero del sistema (documentazione,
manuali, siti semplici); Exilla serve dove serve un motore intero.

- **Il nome**: il marchio «Firefox» e il logo sono di Mozilla e una build non
  ufficiale non li puo' usare. Si parte dal branding neutro dell'albero
  (`browser/branding/unofficial`) e se ne fa uno nostro, `exilla`.
- **La licenza**: MPL 2.0. Si ridistribuisce modificato pubblicando i sorgenti
  dei file che cambiamo: sono le patch di questa directory.
- **Dove va**: non sul CD di base. Sulla ISO degli strumenti, e come pacchetto
  del repository di rete di `netupdate`.
- **Dove si compila**: su una macchina con molta memoria (Core i5 di quarta
  generazione, 32 GB, Debian 12). Il collegamento di `libxul` chiede oltre
  16 GB.

> **Stato al 24 settembre 2026: analisi, niente ancora costruito.** Questo file
> e' il piano, con i dati misurati sull'albero. Ogni tappa, quando e' fatta,
> lascia qui il comando che la rifa'.

---

## 1. L'albero

`/firefox-main/` (fuori da git, vedi `.gitignore`): **Firefox 158.0a1**, 4,9 GB.

Quel che chiede per costruirsi, misurato nell'albero:

| | minimo richiesto | cosa c'e' per EX-OS |
|---|---|---|
| GCC | 11.1 (`build/moz.configure/toolchain.configure`) | GCC 17 (`gcc/`, `tools/gcc-exos/`) |
| Rust | 1.90 (`python/mozboot/mozboot/util.py`) | sorgenti 1.100 (`rust/`); bersaglio `i686-unknown-exos` solo progettato (`tools/rust-exos/`) |
| cbindgen | 0.29.4 (`build/moz.configure/bindgen.configure`) | si installa sull'host |

**Il livello di supporto.** Linux su x86 a 32 bit e' una piattaforma di
**livello 3** (`build/docs/supported-configurations.md`): Mozilla non la prova
nella sua integrazione continua (che costruisce solo Linux a 64 bit), e una
build puo' rompersi senza che nessuno se ne accorga. E' come FreeBSD, OpenBSD e
NetBSD: funziona se qualcuno la tiene in piedi. Le versioni ESR (supporto
esteso) sono il punto di partenza piu' stabile da valutare per la prima build.

---

## 2. Cosa c'e' gia' nell'albero che aiuta

- **`widget/headless`**: un backend senza schermo. E' il modello per il backend
  nuovo `widget/exos`, che disegna in una finestra ExWin.
- **WebRender con `swgl`** (`gfx/wr/swgl`): un rasterizzatore **software**.
  Non serve una GPU.
- **JavaScript (SpiderMonkey)**: c'e' il backend JIT `x86` a 32 bit, e il
  backend `none` (solo interprete), che si porta ovunque ed e' il primo passo.
- **Un sistema Unix che non e' Linux si aggiunge toccando decine di file, non
  migliaia**: il codice specifico di OpenBSD sta in 23 file, quello di FreeBSD
  in 20 (`XP_OPENBSD`, `XP_FREEBSD`), contro i 131 di Linux. OpenBSD e FreeBSD
  sono i modelli per `XP_EXOS`.
- **NSPR** ha un file per piattaforma (`nsprpub/pr/src/md/unix/openbsd.c` e
  simili, configurati in `nsprpub/configure.in`): se ne scrive uno per EX-OS.
- **Il riconoscimento del sistema** nel build: `split_triplet` in
  `build/moz.configure/init.configure` (i nomi `linux`, `freebsd`, `openbsd`...):
  si aggiunge `exos`.
- **Un processo solo**: la variabile d'ambiente `MOZ_FORCE_DISABLE_E10S`
  (`toolkit/xre/nsAppRunner.cpp`) tiene tutto in un processo. Per la prima
  accensione su EX-OS vuol dire niente comunicazione fra processi da portare.

Opzioni di configurazione per spegnere cio' che su EX-OS non c'e' (tutte in
`toolkit/moz.configure`, `js/moz.configure`, `build/moz.configure/*`):

    --disable-sandbox --disable-crashreporter --disable-webrtc
    --disable-updater --disable-dbus --disable-pulseaudio --disable-alsa
    --disable-jack --disable-necko-wifi --disable-geckodriver
    --disable-jit            (all'inizio: il backend `none`)
    --with-branding=browser/branding/exilla

---

## 3. Cosa manca in EX-OS

### Il sistema (kernel e libc)

L'elenco e' lo stesso che serve a Rust (`tools/rust-exos/leggimi.md`), e va
fatto per primo, perche' Exilla ha bisogno di entrambi:

- **TLS per filo** (oggi il blocco TLS e' del processo) e **attese che bloccano
  davvero** (futex): i fili ci sono dal 4 settembre 2026, ma il mutex gira
  cedendo la CPU;
- l'**interfaccia `pthread`** sopra i fili: thread, mutex, variabili di
  condizione, chiavi;
- i **socket BSD** (`socket`, `connect`, `bind`, `listen`, `accept`, `send`,
  `recv`, `getaddrinfo`) sopra lo stack IP, che oggi si usa per messaggi;
- **`dlopen`/`dlsym`** (o, in alternativa, un `libxul` collegato staticamente);
- `mmap`/`mprotect` con tutti i permessi (il JIT scrive e poi esegue), memoria
  condivisa, `poll`, i segnali che SpiderMonkey usa, `clock_gettime`,
  `getrandom` (c'e').

### ExWin (il server grafico e il toolkit)

| Serve a Exilla | ExWin oggi (24 settembre 2026) |
|---|---|
| movimento del mouse SEMPRE (passaggio sopra, menu, puntatore) | arriva solo con un pulsante premuto |
| pulsante destro e centrale | il messaggio non dice quale |
| rotella del mouse | assente, nel driver e nei messaggi |
| tasto rilasciato, testo Unicode (UTF-8) | solo la pressione e un codice di scansione |
| finestra attivata o disattivata | nessun messaggio |
| finestre a comparsa senza bordo (menu, tendine, suggerimenti) | solo `EX_SOPRA` |
| forme del puntatore (freccia, mano, testo, ridimensiona) | assenti |
| tempi fini (circa 16 ms) | la sveglia ha risoluzione di 200 ms |
| risoluzioni grandi | fino a 1024x768 (`drivers/svga`) |

Ci sono gia': la memoria condivisa per i pixel verso il server, gli appunti,
`ex_guarda_fd` (un altro filo sveglia il ciclo dei messaggi scrivendo in un
tubo), `EXM_MISURA` per il ridimensionamento. I caratteri li porta Gecko
(FreeType e HarfBuzz sono nell'albero; i Liberation Fonts sono nel progetto).

Queste migliorie servono anche agli altri programmi: vanno fatte **prima** di
Exilla, una alla volta, ognuna con la sua prova.

---

## 4. Le tappe

1. **Il sistema**: TLS per filo, futex, `pthread`, socket BSD. Ognuna con una
   prova in C dentro EX-OS.
2. **Rust**: il passo 0 di `tools/rust-exos/leggimi.md` (un programma Rust
   compilato da Linux che gira dentro EX-OS), poi la `std`.
3. **ExWin**: la tabella del paragrafo 3.
4. **SpiderMonkey da solo** (`js/src`, con `--disable-jit`): la shell `js`
   dentro EX-OS. E' la prima prova che il C++ di Mozilla, il suo allocatore e
   i fili girano.
5. **NSPR e NSS** per EX-OS: la rete e il TLS di Gecko.
6. **Gecko con `widget/headless`**: nessuna finestra, una pagina caricata e
   salvata come immagine. Prova che l'impaginazione e il disegno software
   funzionano.
7. **`widget/exos`**: la finestra vera in ExWin, mouse e tastiera.
8. **Il pacchetto**: ISO degli strumenti e repository di `netupdate`.

Sulla macchina di compilazione: il `mozconfig` di partenza, le patch e i
comandi si scrivono qui, tappa per tappa, man mano che si fanno.

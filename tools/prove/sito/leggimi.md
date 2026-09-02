# Il sito di prova del browser

Nove pagine scritte a mano, ognuna per una cosa sola, piu' un server che le
serve. Servono a provare il browser di EX-OS **dentro QEMU** su casi che si
conoscono: una pagina vera cambia sotto i piedi, e quando qualcosa si vede
storto non si sa se e' colpa nostra o del sito.

## Come si usa

Sull'host, dalla radice del progetto:

    python3 tools/prove/sito/servi.py          # resta in ascolto sulla 8000

Dentro EX-OS la macchina host e' **10.0.2.2** (lo slirp di QEMU), quindi:

    EXOS_QEMU_EXTRA="-netdev user,id=n1 -device ne2k_pci,netdev=n1" \
    EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso \
    python3 tools/qemu_drive.py "exwin@14" "mon:sendkey alt-f1@3" \
        "/exwin/bin/browser &@14" "mon:sendkey alt-f5@3" \
        "http://10.0.2.2:8000/con.html@20" "foto:/tmp/exos/x.ppm@2"

! **`exwin` SPOSTA LO SCHERMO DA SOLO** sulla console della grafica: per
digitare nella shell bisogna tornare con Alt+F1, ed e' il motivo di quel
`mon:sendkey alt-f1` in mezzo.

! **I MOVIMENTI DEL MOUSE VANNO IN UN `mon:` SOLO**, separati da `;`. Un `mon:`
per conto suo costa due secondi — il timeout del drenaggio del socket — e
settanta movimenti diventano tre minuti.

## Cosa prova ognuna

| pagina | a cosa serve |
|---|---|
| `con.html` | immagini con `width`/`height`: il posto si tiene prima, il testo non si sposta |
| `senza.html` | le stesse immagini SENZA misure: il testo si sposta, ed e' il controllo |
| `trenta.html` | trenta immagini: il tetto dei pixel e «N immagini fuori» |
| `bordi.html` | `border`, `colspan`, `rowspan` |
| `tabella.html` | colspan e rowspan senza bordo |
| `copertura.html` | accentate, entita' numeriche, greco, cirillico, ebraico, arabo |
| `font.html` | `font-family`: elenchi, nomi veri senza generica, `<pre>`, e il foglio che batte il tag |
| `script.html` | il motore JavaScript: quattordici riquadri — innerHTML, i nodi costruiti a mano, gli attributi, i clic, `preventDefault`, `setTimeout`, `style`, i selettori, `classList`, `dataset`, `location`, XMLHttpRequest e `fetch` |
| `biscotti.html` | i cookie, **tutt'e due le meta'**: quel che uno script vede e quel che il server RICEVE. Si apre da `/metti-biscotti`, non dal suo nome — vedi qui sotto |
| `modulo.html` | una casella e un'area: cursore, selezione, appunti |
| `perdita.html` | sei immagini senza misure e dodici caselle: i controlli che si moltiplicavano |

## `servi.py`

Serve le pagine e **RITARDA le immagini** di `RITARDO` secondi (6 di
predefinito, `RITARDO=0` per non ritardare). Il ritardo non e' un trucco: serve
a fotografare lo stato intermedio — testo gia' impaginato, pixel non ancora
arrivati — che senza sparirebbe prima di riuscire a guardarlo.

### I due indirizzi finti dei biscotti

Non sono file su disco: per provare i cookie serve un server che ne **metta** e
uno che dica quali gli sono **arrivati**. Sono le due meta' del giro, e senza la
seconda si puo' solo vedere che il browser non si rompe — non che il biscotto e'
davvero tornato indietro.

| indirizzo | cosa fa |
|---|---|
| `/metti-biscotti` | manda `biscotti.html` con due `Set-Cookie`: uno normale e uno `HttpOnly` |
| `/eco-biscotti` | risponde con l'intestazione `Cookie` che ha ricevuto, e basta |

    EXOS_QEMU_EXTRA="-netdev user,id=n1 -device ne2k_pci,netdev=n1" \
    EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso EXOS_RAM=64M \
    python3 tools/qemu_drive.py "netdetect -c@18" "exwin@25" \
        "http://10.0.2.2:8000/metti-biscotti@45" "foto:/tmp/b.ppm@3"

I tre riquadri devono dire, in quest'ordine: che `document.cookie` mostra il
biscotto normale e **non** quello `HttpOnly`; che il server ha ricevuto
**tutt'e due**; e che un biscotto scritto da uno script parte con la richiesta
subito dopo.

## Cosa NON c'e', e perche'

La copia della voce «Operating system» di Wikipedia con cui si sono misurati i
tempi. E' 672 KB di testo altrui sotto CC BY-SA: si riscarica in un secondo con
`curl` quando serve, e non ha senso portarsela dietro nel repository.

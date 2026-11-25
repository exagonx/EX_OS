# 6. Pubblicare il repository degli aggiornamenti

[torna all'indice](00-indice.md)

Per chi tiene il server da cui le macchine si aggiornano.

---

## 6.1 Un comando solo

```
./exagonx/repo-update.sh              il sistema (repository a 32 bit)
./exagonx/repo-update.sh -64          il repository a 64 bit
./exagonx/repo-update.sh -src         i sorgenti
./exagonx/repo-update.sh -tutti       i tre: 32 bit, 64 bit e sorgenti
./exagonx/repo-update.sh -strumenti   anche i compilatori (ore, non minuti)
./exagonx/repo-update.sh -ricrea      ricarica ogni file, non solo i cambiati
./exagonx/repo-update.sh -verifynew   verifica solo i file appena caricati
./exagonx/repo-update.sh -guarda      dice cosa farebbe e si ferma
```

! **`-tutti` E `-ricrea` SONO DUE COSE DIVERSE.** `-tutti` dice *quali*
  repository (tutti e tre); `-ricrea` dice *come* caricarli (ogni file, anche
  quelli gia' uguali sul server). Si combinano. Fino al 4 novembre 2026
  `-ricrea` si chiamava `-tutto`, e `-tutto -verifynew` sembrava voler dire
  "tutti i repository" mentre ricaricava il solo 32 bit: adesso `-tutto` si
  rifiuta e spiega la differenza. Per aggiornare i tre repository con gli
  strumenti nuovi (compilatori, librerie, codec, VLC):

```
./exagonx/repo-update.sh -tutti -strumenti -verifynew
```

Fa quattro passi in fila:

1. `make -j2 iso-exos` — ricompila e rifà l'albero del CD di sistema
2. `make netinst` — compone `dist/netinst`: `versione.txt`, `catalogo.txt`,
   `elenco.txt` con l'impronta di **ogni** file, e `file/` con i file veri
3. `exagonx/pubblica.sh` — carica sull'FTP **solo cio' che è cambiato**
4. `exagonx/verifica.sh` — riscarica dal web e confronta le impronte

---

## 6.2 I file non si caricano a mano. Mai.

! **UN CARICAMENTO CON UN CLIENT FTP GRAFICO IN MODO AUTOMATICO HA CORROTTO 73
  ESEGUIBILI SU 106.** Il modo ASCII converte i byte `0x0D` nei binari, e il
  modo automatico sceglie ASCII proprio per i nostri file: decide
  dall'estensione, e `sh`, `cp`, `ls` non ce l'hanno.

! **`curl` CARICA IN BINARIO DI SUO.** Non c'è un'opzione da aggiungere: c'è da
  non usare altro. `pubblica.sh` usa curl.

! **E VERIFICARE NON E' PARANOIA.** Un FTP che converte non dà nessun errore:
  dice che è andato bene, il web risponde 200, e i file sono rotti. L'unico
  rilevatore è l'impronta — ed è per questo che `verifica.sh` **riscarica dal
  web** invece di fidarsi del codice di uscita.

---

## 6.3 I tre file che si devono corrispondere

```
versione.txt     dichiara l'impronta di catalogo.txt e di elenco.txt
catalogo.txt     i pacchetti: nome, cosa contengono, da cosa dipendono
elenco.txt       un file per riga: percorso, byte, impronta, pacchetto
```

! **NON SI MESCOLANO FRA DUE BUILD.** Ogni `make netinst` rigenera tutti e tre;
  caricarne uno di ieri e due di oggi produce esattamente il messaggio «il
  server si contraddice». Se ne carichi uno, caricali tutti.

! **L'IMPRONTA E' PER FILE, E NON E' UNA SEMPLIFICAZIONE: E' IL PUNTO.** Se sul
  server sono cambiati solo `ls` e `fdisk`, `netupdate` deve poter scaricare
  solo quei due. Un'impronta per pacchetto farebbe riscaricare l'intero
  sistema per due programmi.

! **SUL SERVER NON C'E' NESSUN `.gz`: SI CHIAMA `_gz`.** Un file `.gz`
  Altervista lo serve con `Content-Encoding: gzip`, e Cloudflare lo consegna
  già decompresso: l'impronta non torna mai. Per questo `mknetinst.sh` mette
  in `file/` ogni `.gz` e `.tgz` come `_gz` e `_tgz`, anche in mezzo al nome
  (`openssl-4.1.0.tar.gz.asc` diventa `openssl-4.1.0.tar_gz.asc`, dal
  netupdate 0.036), e i pacchetti in `pacchetti/` sono `.tar_gz`.
  `elenco.txt` tiene il nome vero, quello che finisce sul disco: è
  `netupdate` a chiedere al server l'altro. `pubblica.sh` e `verifica.sh`
  seguono la stessa regola. Un netupdate più vecchio della 0.036 non trova
  le firme e i riassunti dei sorgenti (`.tar_gz.asc`, `.sig`, `.sha256`):
  va aggiornato prima.

---

## 6.3.1 versione.txt è firmato (netupdate 0.053)

L'ultima riga di `versione.txt` è la firma ed25519 di tutto ciò che la
precede, insieme a due righe nuove:

```
serie     = 1795563538     quando è stato firmato: mai più basso di quello installato
scade     = 1798155538     dopo questa data netupdate lo rifiuta (30 giorni)
firma     = 6e14251a...    128 cifre esadecimali
```

`mknetinst.sh` lo firma da solo con `tools/netinst/firma.py`. La chiave
privata sta **fuori dal progetto e fuori da MEGA**, in
`~/ex_os/chiavi/netupdate-ed25519.key` (o dove dice `EXOS_CHIAVE`); quella
pubblica è compilata in netupdate (`bin/netupdate/chiave_netupdate.h`).

! **PERCHÉ.** `versione.txt` porta l'impronta di tutti gli altri file e
  viaggia in HTTP in chiaro: chi stava sulla rete (o falsificava una risposta
  DNS) poteva dare il suo, e netupdate installava un kernel suo ed eseguiva
  le sue verifiche come root.

! **LA CHIAVE PRIVATA NON SI PERDE E NON SI PUBBLICA.** Fanne una copia su un
  supporto separato. Senza, le macchine installate non accettano più un
  repository nuovo: ci vuole un netupdate con un'altra chiave, portato a mano.
  Chi la ha può far installare qualunque cosa a ogni macchina.

! **UN REPOSITORY CHE NON SI RIPUBBLICA PER 30 GIORNI SCADE.** Le macchine lo
  rifiutano («l'albero del server è scaduto»): si rifà `make netinst` e si
  ricarica. È voluto: protegge chi viene tenuto fermo su un albero vecchio.

! **UN REPOSITORY TUO.** `tools/netinst/firma.py crea` fa la tua chiave,
  `firma.py pub netupdate.pub` ne scrive la metà pubblica: va in
  `/boot/netupdate.pub` sulle tue macchine, di root e non scrivibile da
  altri. Da lì netupdate accetta solo i tuoi alberi.

! **IL PASSAGGIO.** Un netupdate più vecchio della 0.053 ignora la firma:
  l'aggiornamento che porta la 0.053 è l'ultimo non controllato. Un
  repository non firmato si accetta solo con `netupdate -check -insicuro`,
  e solo se non c'è `/boot/netupdate.pub`.

---

## 6.4 Quanto pesa

```
completo      163 MB   1606 file
senza exos/    13 MB    196 file
```

I 150 MB sono il cross-toolchain. Su uno spazio web gratuito caricare 1600
file richiede tempo: si può pubblicare tutto **tranne `file/exos/`** e il
sistema di base si aggiorna lo stesso. I compilatori si aggiungono dopo.

---

## 6.5 Il punto di raccolta dei referti

`make netinst` copia anche `tools/netinst/report/` dentro `dist/netinst/`,
perché è lo stesso server: chiedere di caricare due cose in due posti diversi
vuol dire che una delle due, prima o poi, resta indietro.

Da una macchina qualunque:

```
senderror http://esempio.org/exos/netinst/report/ /SIS900.TXT
```

! **E' UN POSTO DOVE CHIUNQUE PUO' SCRIVERE, e non c'è modo di renderlo
  altrimenti.** La chiave dentro `senderror` sta in un binario che si
  distribuisce: ferma uno scanner, non ferma una persona.

Le difese vere sono tetti, in cima a `report/index.php`:

| | predefinito | cosa limita |
|---|---|---|
| `BYTE_MAX` | 256 KB | quanto può essere grande un referto |
| `FILE_MAX` | 500 | quanti possono esistere in tutto |
| `AL_GIORNO` | 20 | quanti ne può mandare un indirizzo |

Nessuno dei tre impedisce a qualcuno di riempire lo spazio: tutti e tre fanno
in modo che ci metta tanto e che si veda. Se lo spazio è poco, il primo da
abbassare è `FILE_MAX`.

Il nome del file lo decide lo script e mai chi manda, e in `ricevuti/` un
`.htaccess` spegne l'esecuzione — seconda difesa, nel caso la prima avesse un
buco.

! **PER CAMBIARE LA CHIAVE L'ORDINE CONTA**: prima sul server
  (`index.php`, `CHIAVE`), poi in `bin/senderror/senderror.c` (`CHIAVE_PRED`)
  e ricompilare. Al contrario, le macchine già in giro smettono di poter
  mandare referti senza che nessuno capisca perché.

# 6. Pubblicare il repository degli aggiornamenti

[torna all'indice](00-indice.md)

Per chi tiene il server da cui le macchine si aggiornano.

---

## 6.1 Un comando solo

```
./exagonx/repo-update.sh              il sistema
./exagonx/repo-update.sh -strumenti   anche i compilatori (ore, non minuti)
./exagonx/repo-update.sh -tutto       ricarica tutto, non solo i cambiati
./exagonx/repo-update.sh -guarda      dice cosa farebbe e si ferma
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

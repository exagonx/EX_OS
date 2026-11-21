# EX-OS — La rete

[🇬🇧 English](rete.md) · **🇮🇹 Italiano**

[← README](../README.it.md) · [Tutti i manuali](../README.it.md#i-manuali)

**Indice**

- [Lo stack IPv4: `/dev/ip.drv`, `ping`, `ipcfg`](#lo-stack-ipv4-devipdrv-ping-ipcfg)
- [UDP e DHCP](#udp-e-dhcp)
- [TCP](#tcp)
- [`ftp` — client FTP](#ftp--client-ftp)
- [`telnet` — sessione interattiva](#telnet--sessione-interattiva)
- [Risoluzione dei nomi: `host`, e `ping` per nome](#risoluzione-dei-nomi-host-e-ping-per-nome)

---

## Lo stack IPv4: `/dev/ip.drv`, `ping`, `ipcfg`

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

`ipcfg` mostra indirizzo e **contatori**, `ipcfg -r` la tabella ARP,
`ipcfg -tcp` le connessioni TCP (stato, porte, l'altra parte, il programma
che le tiene; da `ip.drv` 0.015).
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

## UDP e DHCP

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

## TCP

Un programma che muore senza chiudere (segnale, guasto, spegnimento) chiude lo
stesso le sue connessioni: ip.drv 0.020 controlla ogni mezzo secondo i
proprietari e manda il FIN al posto loro. Un segmento per una connessione che
non esiste riceve un RST: dopo un riavvio improvviso il client lo scopre al primo
tasto. Prima un client telnet restava fermo sul prompt di una macchina gia'
riavviata. Prova: `tools/prova_telnet_fine.sh 32` e `64`.

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

## `ftp` — client FTP

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

## `telnet` — sessione interattiva

```
telnet nome-o-indirizzo [porta]      si esce con Ctrl+]
```

! **Telnet manda tutto in chiaro, password compresa.** Non è un difetto
del programma, è il protocollo: chiunque stia sul percorso legge nome
utente, password e tutto quello che si scrive dopo. Il client lo dice una
volta all'avvio invece di lasciarlo intendere. L'alternativa si chiamerà
SSH quando ci sarà TLS — non «telnet con una toppa».

! **Un server SSH c'è, `sshd` 0.001, e non è ancora dimostrato.** Parla SSH-2
con un algoritmo per mestiere - curve25519-sha256 per lo scambio delle chiavi,
ssh-ed25519 per l'identità del server, chacha20-poly1305 per i dati - e la sua
crittografia (`lib/excrypt`) è controllata contro i vettori degli RFC. Quello
che non è scritto da nessuna parte è una sessione con un vero client OpenSSH,
che è la prova che il suo stesso sorgente chiede. Finché non è fatta va
considerato presente ma non dimostrato: non va esposto a una rete di cui non
ci si fida. `sshd -s`, come `telnetd -s`, dà la shell senza passare dal login.

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

## Risoluzione dei nomi: `host`, e `ping` per nome

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

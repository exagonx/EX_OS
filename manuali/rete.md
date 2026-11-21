# EX-OS — Networking

**🇬🇧 English** · [🇮🇹 Italiano](rete.it.md)

[← README](../README.md) · [All the manuals](../README.md#the-manuals)

**Contents**

- [The IPv4 stack: `/dev/ip.drv`, `ping`, `ipcfg`](#the-ipv4-stack-devipdrv-ping-ipcfg)
- [UDP and DHCP](#udp-and-dhcp)
- [TCP](#tcp)
- [`ftp` — FTP client](#ftp--ftp-client)
- [`telnet` — an interactive session](#telnet--an-interactive-session)
- [Name resolution: `host`, and `ping` by name](#name-resolution-host-and-ping-by-name)

---

## The IPv4 stack: `/dev/ip.drv`, `ping`, `ipcfg`

ARP, IPv4 and ICMP live in a **process of their own**, not in the driver:

```
ping ──IPC──> ip.drv ──IPC──> ne2k.drv ──I/O ports──> card
```

Three reasons, all practical: these protocols are the same on any card
(putting them in the driver would mean rewriting them for the PCnet); the
stack has **timing** — ARP expiry, waiting for replies — while the driver
only has to answer the hardware; and if the stack gets it wrong you restart
the stack, while the card stays powered up and configured.

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

`ipcfg` shows the address and the **counters**, `ipcfg -r` the ARP table,
`ipcfg -tcp` the TCP connections (state, ports, the other end, the program
holding them; from `ip.drv` 0.015). The
counters are half the program: when the network does not work, the question
is not «does it work or not» but where it stops, and `IP ricevuti` at zero,
`scartati` rising, or `somme errate` rising point at three different places.

What the stack does **not** do, said up front:

- **it does not fragment and does not reassemble** — a datagram larger than
  the MTU is refused, an incoming one that is a fragment is counted and
  dropped;
- **no routing table** — there is one local network and one gateway;
- **no DHCP inside the stack** — the address is declared (`ip.drv -a … -m …
  -g …` or `ipcfg -a …`); getting one from a server is the `dhcp` program's
  job, and it sits on top of UDP like any other client;
- **one echo request at a time** — `ping` is sequential by nature.

! `ping` distinguishes **three** outcomes, not two: reply received; no
answer to the **ARP** (there is nobody at that address — we did not even get
out onto the wire); no answer to the **echo** (the packet left, the problem
is further away). A single «unreachable» would force you to start the
diagnosis over every time.

! A time of `<10 ms` is not a zero: `uptime_ms()` counts PIT ticks at
100 Hz, so it advances in 10 ms steps. Writing «0 ms» would claim a precision
that is not there.

## UDP and DHCP

The stack does UDP too. There are no sockets and no descriptors: you open a
**port**, and from that moment the datagrams for that port belong to whoever
opened it. It is enough for a DHCP client and for a future DNS resolver; a
real socket API will be built on top of this one, not in its place.

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

`dhcp` is a **program**, not a piece of the stack: DHCP sits on top of UDP
like a DNS client, and a bug in there makes one command fail instead of
taking the network down. `dhcp -n` asks and prints without applying.

! **It does not renew the lease.** When it expires, run it again. Renewal
wants a process that stays alive until halfway through the lease time, that
is, a different program from this one — which has to be startable by hand and
to finish.

! A datagram for an open port with nobody waiting is **dropped and counted**
(`ipcfg` shows it). UDP loses packets by definition, and a queue growing
while nobody reads is a slow way of running out of memory at the sender's
discretion. Whoever expects a datagram must post the receive **before**
sending the request.

## TCP

A program that dies without closing (signal, fault, shutdown) still closes its
connections: every half second ip.drv 0.020 checks the owners and sends the FIN
for them. A segment for a connection that does not exist gets an RST: after a
sudden restart the client finds out at the next key. Before, a telnet client
stayed on the prompt of a machine that had already restarted. Test:
`tools/prova_telnet_fine.sh 32` and `64`.

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

DNS → ARP → IP → TCP, data in both directions through the NAT. (The 403 is
HTTP: `GET / HTTP/1.0` without a `Host` is refused by Cloudflare. The
transport worked — that reply proves it.)

! **Outgoing connections only.** The `LISTEN`/`SYN_RECEIVED` branch of the
state machine is missing, which is about half the work and is what it takes
to be a **server**. This TCP's first customer is an FTP client in **passive**
mode (`PASV`), which opens the data connection itself too; active mode would
require listening and does not work behind a NAT anyway. You build the half
that is needed, and you say it is a half.

What it does **not** do, declared in `drivers/net/ip_proto.h`:

| | |
|---|---|
| **no reordering** | an out-of-sequence segment is **dropped** and re-acknowledged: whoever sent it retransmits. Correct but not efficient — keeping the pieces wants a list with its own timers, and that is where a young TCP picks up its worst bugs |
| **no congestion control** | as much is sent as the other side's window allows. On a local network it changes nothing; on the Internet it means being rude under loss |
| **fixed RTO** | the round-trip time is not measured: it doubles from 600 ms. Measuring it properly (Karn, Jacobson) is the next step |
| **no SACK, window scaling, timestamps** | |

! **Sequence numbers are compared by subtraction, never with `<`.** They are
32-bit and they wrap: `a < b` across the wrap gives the reversed answer, once
every 4 GB transmitted — that is, rarely, and always when the connection is
busy.

! `IP_MSG_TCP_APRI` can answer **`-EAGAIN`**: it means the stack has just
asked for the next hop's ARP. It is not a failure, it is «in a moment».
Threading the ARP wait into TCP's state machine would mean two nested timers
on the same connection.

## `ftp` — FTP client

On the EX-OS CD, along with the other network tools.

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

With no command it opens a command line: `ls`, `cd`, `pwd`, `get`, `put`,
`bye`.

! **Passive mode (`PASV`) only.** In active mode it is the *server* that
connects back to the client, which therefore has to **listen** — and EX-OS's
TCP cannot, on purpose. It is not a fallback: active mode does not work
behind a NAT anyway, which is why every serious client has used `PASV` for
twenty years.

! **FTP sends the password in the clear.** It is not a defect of the
program, it is the protocol: anyone on the path reads the user and the
password just as they are. The client says so at login, once, instead of
leaving it to be inferred. The alternative will be called SFTP or FTPS when
there is TLS — not «ftp with a patch».

! If the server announces in `PASV` an address different from the one we are
connected to, the client **uses the real one**: a server behind a NAT often
announces its private address, which is unreachable from outside. The port is
the useful information; the address we already know.

To try it without a real server there is `tools/ftpserver-prova.py` — !
which **is not an FTP server**: it lets anyone in and serves a single
directory, and is meant to be run on localhost for the length of a test.

## `telnet` — an interactive session

```
telnet name-or-address [port]        Ctrl+] to leave
```

! **Telnet sends everything in the clear, password included.** It is not a
defect of the program, it is the protocol: anyone on the path reads the
user name, the password and everything typed afterwards. The client says so
once at startup instead of leaving it to be inferred.

! **There is an SSH server, `sshd` 0.001, and it is not yet proven.** It
speaks SSH-2 with one algorithm per job - curve25519-sha256 for the key
exchange, ssh-ed25519 for the server's identity, chacha20-poly1305 for the
data - and its cryptography (`lib/excrypt`) is checked against the RFC test
vectors. What has not been recorded anywhere is a session with a real
OpenSSH client, which is the test its own source asks for. Until that is
done, treat it as present but not demonstrated: do not expose it to a
network you do not trust. `sshd -s`, like `telnetd -s`, gives a shell
without going through the login.

**How it listens to the network and the keyboard at once.** There is no
`select()`, and threads arrived after this program, but there is something
better for this case: in EX-OS everything comes through the same mailbox. You *book* a
receive from the IP stack (`IP_MSG_TCP_RICEVI`), you *book* a key from the
keyboard service (`KBD_MSG_READKEY`), and then you wait with a single
`ipc_recv_timeout()` and look at **who** answered. The two bookings re-arm
independently: if the server is quiet you keep receiving keys, if nobody
types you keep receiving data.

! **The timeout matters even when nothing times out**: the keyboard's raw
mode goes away by itself whenever somebody else asks for a line, and
without a periodic wake-up that reasserts it the program would sit waiting
for a key the driver will never deliver.

**Option negotiation cannot be skipped.** Telnet interleaves commands
starting with byte 255 (IAC) into the data. A client that ignored them
would print control characters and — far worse — would leave the server
*waiting*: many servers do not even send the `login:` until negotiation is
done.

| | |
|---|---|
| **refuse everything you cannot do** | to a `DO` for an unknown option answer `WONT`, to a `WILL` answer `DONT`. Silence is not a refusal: it is a server waiting |
| **never answer an answer** | two polite implementations that always reply bounce the same option back and forth forever. The state of what has already been granted is kept |
| accepted | `ECHO` and `SGA` from the server, `TTYPE` and `NAWS` towards it |
| `IAC IAC` | is a 255 byte in the data, not a command |

The parser state is **static, not local**, and that matters: an IAC
sequence can be split across two segments — TCP delivers bytes, not
messages — and a state reset on every call would print half a command and
answer the other half as if it were a different one.

Two keyboard translations that are not obvious:

- **Enter is `CR LF`**, not a bare `LF`: telnet's network virtual terminal
  wants a CR always followed by LF or NUL, and servers that apply the rule
  literally do nothing with a lone LF.
- **Backspace is sent as `0x7F`**, not as the `0x08` the key produces here:
  on Unix systems the default erase character is DEL, and sending `0x08`
  leaves the line unchanged and prints `^H` — which looks like a keyboard
  defect while it is a convention on the other side.

To try it without exposing a shell there is `tools/telnetserver-prova.py` —
! which **is not a telnet server**: it echoes and answers made-up
commands. It exists because putting a real `telnetd`, which hands out a
shell with no encryption, on a listening port would be a bad idea on any
machine. The test as seen from its side:

```
  <- DO ECHO
  <- DO SGA
  <- WILL TTYPE
  <- WILL NAWS
  <- schermo: 80x25
  <- terminale: 'EXOS'
  riga: 'ciao mondo'
```

## Name resolution: `host`, and `ping` by name

```
ex-os:/> host one.one.one.one
one.one.one.one ha indirizzo 1.0.0.1  (risposta da 10.0.2.3)

ex-os:/> ping www.google.com -n 2
ping www.google.com (142.251.151.119) con 32 byte di dati
  60 byte da 142.251.151.119: seq=1 ttl=255 tempo=50 ms
```

The resolver is a **module** (`lib/dns.c`), compiled into the programs that
need it — not a service and not part of the stack. DNS sits on top of UDP
exactly as DHCP does: an error parsing a reply written by an unknown server
must make one command fail, not take the network down. And it has no state to
keep between calls, so a dedicated process would only cost one more thing to
start and watch over.

! **Name-compression pointers are followed only when reading, with a cap on
the hops.** In a DNS reply a name can end with a pointer to an earlier point
in the message; that pointer is written by the server, and nothing stops it
from pointing at itself. To *skip* a name it is not followed at all — a
pointer ends the name, and the length is known.

! `ping` prints the name **and** the address when given a name: without it,
faced with a strange answer you cannot tell a DNS failure from a network one.

`host` exists so the resolver can be tested on its own. When `ping name` does
not work, the question is whether ping or DNS is broken, and without this
command you have to guess.

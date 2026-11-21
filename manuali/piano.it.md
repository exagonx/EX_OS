# EX-OS — Il piano di sviluppo (storico)

[🇬🇧 English](piano.md) · **🇮🇹 Italiano**

[← README](../README.it.md) · [Tutti i manuali](../README.it.md#i-manuali)

> Il piano con cui EX-OS è partito, e lo stato delle fasi fino ad agosto 2026. È qui come storia: quello che c'è oggi è nel [README](../README.it.md) e nelle [novità](novita.it.md).

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
| **TCP fuori sequenza** | i segmenti arrivati in disordine si scartano invece di riordinarli, e l'RTO è fisso invece che misurato |
| **DNS: solo record A** | i CNAME si saltano invece di seguirli; se la risposta non contiene già il record A finale, il nome non si risolve |
| **rinnovo DHCP** | `dhcp` prende la concessione e finisce; il rinnovo vuole un processo che resti acceso |
| **TLS** | senza cifratura non esistono HTTPS né SFTP; il primo passo è una sorgente di entropia, non il protocollo |
| **exFAT** | ! non esiste come filesystem: prima va implementato, poi ha senso un chkdsk |
| **`rename` fra directory** | oggi ENOSYS: sarebbe una copia più una cancellazione, cioè un'altra operazione |
| **DMA per il disco** | oggi 0,75 MB/s in PIO |

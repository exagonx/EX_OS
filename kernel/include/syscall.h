/* =============================================================================
 * kernel/include/syscall.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "kernel.h"
#include "idt.h"

/* =============================================================================
 * Numeri di syscall (stile Linux x86)
 * ============================================================================= */
#define SYS_EXIT        1
#define SYS_SPAWN       2      /* crea processo figlio autonomo (vedi sys_spawn) */
#define SYS_READ        3
#define SYS_WRITE       4
#define SYS_OPEN        5
#define SYS_CLOSE       6
#define SYS_WAITPID     7
#define SYS_GETPID      20
#define SYS_GETPPID     64

/* =============================================================================
 * L'IDENTITA' — getuid/setuid, numeri di Linux
 *
 * ! setuid SI PUO' CHIAMARE SOLO DA root, E IN UNA DIREZIONE SOLA: si scende,
 * non si sale. Un processo che potesse alzarsi da solo renderebbe i permessi
 * una decorazione — e non c'e' il bit setuid sui file, quindi non esiste
 * nemmeno il caso legittimo in cui servirebbe risalire.
 *
 * ! ED E' COSI' CHE login CAMBIA UTENTE SENZA setuid SUI FILE. init resta
 * root, avvia login che resta root, login chiede le credenziali e poi scende
 * con setuid() prima di lanciare la shell: da quel momento non puo' piu'
 * tornare su. Il privilegio si spende, non si presta.
 * ============================================================================= */
#define SYS_GETUID      24      /* rende l'uid del processo */
#define SYS_CHOWN      182      /* ebx = percorso, ecx = uid, edx = gid */
#define SYS_CHMOD       15      /* ebx = percorso, ecx = modo */
#define SYS_SETUID      23      /* ebx = uid, ecx = gid. Solo da root */
#define SYS_MMAP        90
#define SYS_MUNMAP      91
#define SYS_IOCTL       54
#define SYS_EXEC        11
#define SYS_SCHED_YIELD 158
#define SYS_SLEEP       162
#define SYS_SBRK        45
#define SYS_GETCWD      183
#define SYS_CHDIR       12
#define SYS_STAT        106
#define SYS_LSEEK       19
#define SYS_READDIR     141    /* elenca una directory (vedi sys_readdir) */
#define SYS_GETENV      184    /* legge una variabile [env] di /boot/kernel.cfg */
#define SYS_MKDIR        39    /* crea una directory */
#define SYS_RMDIR        40    /* cancella una directory vuota */
#define SYS_UNLINK       10    /* cancella un file */
#define SYS_VERSION     185    /* copia g_os_version (identità del sistema) */
#define SYS_UPTIME      186    /* millisecondi dall'avvio (vedi sys_uptime) */
#define SYS_MEMINFO     187    /* stato della memoria per fascia (vedi MemInfo) */
#define SYS_PROCINFO    188    /* elenca i processi e i loro stack (vedi ProcInfo) */
#define SYS_DISKINFO    189    /* disco fisico + tabella partizioni (vedi DiskInfo) */
#define SYS_BLKINFO     190    /* elenca i dispositivi a blocchi (vedi BlkInfo) */
#define SYS_MOUNT       191    /* monta un dispositivo su un punto */
#define SYS_UMOUNT      192    /* smonta un punto di montaggio */
#define SYS_MOUNTINFO   193    /* elenca i montaggi attivi (vedi MountInfo) */
#define SYS_BOOTINSTALL 194    /* installa MBR + settore di avvio (vedi bootinst.h) */
#define SYS_PARTWRITE   195    /* riscrive la tabella delle partizioni (vedi PartTabella) */
#define SYS_BLKREAD     196    /* legge settori da una partizione NON montata */
#define SYS_BLKWRITE    197    /* scrive settori in una partizione NON montata */
#define SYS_TRUNCATE     92    /* cambia la dimensione di un file (vedi sys_truncate) */
#define SYS_REBOOT       88    /* spegne, riavvia o ferma il sistema */
#define SYS_DUP          41    /* un secondo descrittore sullo stesso file */
#define SYS_DUP2         63    /* come dup, ma su un numero scelto dal chiamante */
#define SYS_FCNTL        55    /* interroga/modifica un descrittore (vedi sys_fcntl) */
#define SYS_PIPE         42    /* due descrittori collegati (vedi kernel/ipc/pipe.c) */
#define SYS_RENAME       38    /* rinomina SENZA spostare i dati (vedi vfs_rename) */

/* Numero totale syscall supportate */
/* ! VA ALZATO A OGNI SYSCALL NUOVA, e dimenticarlo NON da' un errore: la
 * scrittura fuori dall'array corrompe cio' che gli sta dietro, e il guasto
 * si vede altrove. Il 13 agosto 2026 l'ha preso -Warray-bounds aggiungendo
 * SYS_MMIO_MAP=241 con la tabella ancora a 241 voci (0..240). */
/* ! DEVE COPRIRE LA SYSCALL PIU' ALTA + 1, E SBAGLIARLO NON DA' UN ERRORE DI
 * COMPILAZIONE. Aggiungendo SYS_VIDEO_INFO (246) senza toccare questo numero
 * succedevano DUE cose insieme: la syscall veniva rifiutata come inesistente,
 * e la riga che la registra scriveva un elemento OLTRE LA FINE dell'array —
 * cioe' sopra qualunque cosa il compilatore avesse messo dopo. Il sintomo era
 * «video_info non risponde», che non somiglia per niente a una scrittura fuori
 * limite. Chi aggiunge una syscall aggiorna anche questo. */
#define SYSCALL_COUNT   255     /* la piu' alta e' SYS_SU = 254 */

/* =============================================================================
 * Codici errno
 * ============================================================================= */
#define EOK         0
#define EPERM       1
#define ENOENT      2
#define EINTR       4
#define EIO         5
#define EBADF       9
#define ENOMEM      12
#define EACCES      13
#define EFAULT      14
#define EBUSY       16
#define EEXIST      17
#define EINVAL      22
#define EMFILE      24
#define ENOSPC      28
#define EPIPE       32
#define ENOSYS      38
#define ESRCH       3       /* processo destinatario non esiste */
#define ENOTDIR     20      /* atteso una directory, trovato altro */
#define ENOTEMPTY   39      /* directory non vuota (rmdir) */
#define EISDIR      21      /* atteso un file, trovata una directory */
#define EROFS       30      /* filesystem montato in sola lettura */
#define ENODEV      19      /* dispositivo assente */
#define EFBIG       27      /* file troppo grande per l'operazione */
#define ESPIPE      29      /* qui: file frammentato, non mappabile a un intervallo */
#define ENOTTY      25      /* ioctl su un descrittore che non è un terminale */
#define ETIMEDOUT   110     /* attesa scaduta senza che l'evento arrivasse */
#define ECHILD      10      /* waitpid: nessun figlio corrispondente */
#define ENOMEDIUM   123     /* il lettore c'e', il disco dentro no */
#define EAGAIN      11      /* riprova: qui, nessun processo a cui bloccarsi */
#define ENFILE      23      /* limite di SISTEMA (non del processo) raggiunto */
#define E2BIG        7      /* troppi argomenti, o riga di comando troppo lunga */
/* Il file c'e' ma non e' un programma eseguibile: ELF malformato, di un'altra
 * architettura, o troncato. Distinguerlo da ENOENT e' cio' che permette a chi
 * cerca un comando nel PATH di fermarsi e dire la verita' invece di proseguire
 * e concludere «non trovato». Valore allineato a lib/include/libc.h:1371. */
#define ENOEXEC      8

/* Syscall IPC — comunicazione kernel-mediata tra task ring3 */
#define SYS_IPC_SEND     220
#define SYS_IPC_RECV     221
#define SYS_IPC_REGISTER 222
#define SYS_IPC_LOOKUP   223

/* Syscall hardware kernel-mediato — driver ring3 */
#define SYS_IRQ_BIND      224   /* rivendica un IRQ hardware, notifiche via IPC */
#define SYS_IOPORT_BIND   225   /* richiede un range di porte I/O in whitelist */
#define SYS_IOPORT_IN     226   /* legge un byte da una porta nel proprio range */
#define SYS_IOPORT_OUT    227   /* scrive un byte su una porta nel proprio range */

/* Numero SEPARATO invece di un quarto argomento su SYS_IPC_RECV: quella
 * passa tre registri, e leggerne un quarto significherebbe interpretare
 * come scadenza il contenuto di ESI lasciato lì da chi ha usato il
 * wrapper a tre argomenti. Un numero nuovo non ha ambiguità. */
#define SYS_IPC_RECV_TMO  228   /* ipc_recv con scadenza (vedi ipc_recv_timeout) */

#define SYS_TIME           13   /* data e ora dall'orologio CMOS (vedi RtcTime) */

/* =============================================================================
 * Console virtuali (vedi kernel/arch/x86/vga.c)
 *
 * SYS_CONSOLE_WRITE esiste per UN solo cliente: il driver tastiera, che
 * deve fare l'eco dei tasti sulla console di chi sta digitando e non
 * sulla propria. Ogni altro programma scrive con write(1, ...) e
 * finisce sulla console che gli assegna il kernel, senza poter toccare
 * quelle altrui.
 * ============================================================================= */
#define SYS_CONSOLE_SWITCH 229  /* porta in primo piano la console ebx */
#define SYS_CONSOLE_WRITE  230  /* scrive su una console specifica */
#define SYS_CONSOLE_INFO   231  /* quante sono, qual e' la mia, qual e' visibile */
#define SYS_CONSOLE_SETFG  232  /* dichiara il processo in primo piano (job control) */

/* =============================================================================
 * Accessi I/O a 16 e 32 bit — servono al bus PCI, non sono un lusso
 *
 * ! IL BYTE NON BASTA, E NON E' UNA QUESTIONE DI COMODITA'.
 *
 * Il meccanismo di configurazione PCI #1 usa due registri: CONFIG_ADDRESS
 * (0xCF8) e CONFIG_DATA (0xCFC). La specifica PCI dice che CONFIG_ADDRESS
 * va scritto con UN accesso a 32 bit: un accesso a byte o a word verso
 * 0xCF8..0xCFB NON viene interpretato dal ponte come ciclo di
 * configurazione, viene passato al bus come normale I/O. Scrivere
 * l'indirizzo in quattro byte separati quindi non "funziona piu' piano":
 * non funziona, e su molti chipset 0xCF9 e' il registro di reset — quattro
 * outb in fila hanno buone probabilita' di riavviare la macchina invece di
 * leggere un dispositivo.
 *
 * Servono anche i 16 bit: la porta dati di una NE2000 si legge a word, e
 * leggerla a byte dimezza il throughput e sfasa il puntatore interno.
 *
 * PERCHE' QUATTRO NUMERI E NON UN ARGOMENTO "AMPIEZZA". Stesso motivo di
 * SYS_IPC_RECV_TMO: SYS_IOPORT_IN oggi legge solo EBX, e chi la chiama
 * lascia in ECX quel che c'era prima. Aggiungere li' l'ampiezza vorrebbe
 * dire che un binario gia' installato, il giorno che gira su un kernel
 * nuovo, esegue una lettura a 32 bit dove ne voleva una a 8. Un numero
 * nuovo non ha ambiguita'.
 *
 * ! IN32 RESTITUISCE IL VALORE FUORI BANDA, LE ALTRE NO. Una lettura di
 * configurazione PCI che vale 0xFFFFFFFF ("nessun dispositivo") e' un
 * risultato legittimo e frequente; come int32_t e' -1, cioe'
 * indistinguibile da un errore. Percio' SYS_IOPORT_IN32 scrive il valore
 * in un puntatore utente e ritorna 0/-errno. IN16 non ha il problema
 * (0..65535 sta tutto nei positivi) e ritorna il valore direttamente.
 * ============================================================================= */
#define SYS_IOPORT_IN16   233   /* legge una word da una porta nel proprio range */
#define SYS_IOPORT_OUT16  234   /* scrive una word su una porta nel proprio range */
#define SYS_IOPORT_IN32   235   /* legge una dword; il valore esce da un puntatore */
#define SYS_IOPORT_OUT32  236   /* scrive una dword su una porta nel proprio range */

/* =============================================================================
 * SYS_IRQ_DONE — «ho servito l'interrupt, riapri la linea»
 *
 * Obbligatoria per ogni driver ring3 che ha chiamato SYS_IRQ_BIND: il
 * dispatcher maschera l'IRQ nel PIC PRIMA di consegnare la notifica, e
 * senza questa chiamata la linea resta chiusa per sempre.
 *
 * Il perché per esteso sta in kernel/arch/x86/isr.c. In breve: un driver
 * ring3 non gira dentro l'interrupt, e su un IRQ a livello — tutti quelli
 * PCI — la scheda tiene la linea alta finché non le si azzera il registro
 * di stato. Senza mascheramento l'interrupt riparte subito dopo l'iret e
 * il processo driver non riceve mai la CPU per andare ad azzerarlo: la
 * tempesta non finisce da sola.
 * ============================================================================= */
#define SYS_IRQ_DONE      237   /* ebx = irq; riapre la linea mascherata */

/* =============================================================================
 * SYS_DMA_ALLOC — memoria che una scheda puo' leggere e scrivere da sola
 *
 * ebx = puntatore a DmaZona: in `byte` quanti ne servono, in uscita
 * `virt` (l'indirizzo con cui la vede il processo) e `fisico` (quello da
 * scrivere nei registri della scheda).
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' NON BASTA malloc()
 *
 * Un bus master come il PCnet non passa dalla MMU: legge e scrive la RAM
 * agli indirizzi FISICI che gli si sono dati. Un blocco di malloc ha un
 * indirizzo virtuale, e le sue pagine possono stare ovunque e sparse. Dare
 * alla scheda l'indirizzo virtuale significa farle scrivere in un punto a
 * caso della memoria fisica — e il punto a caso, su una macchina piccola,
 * e' spesso il kernel.
 *
 * Servono due cose che malloc non da':
 *   - CONTIGUITA' fisica, perche' un anello di descrittori la scheda lo
 *     percorre sommando, non seguendo tabelle di pagine;
 *   - l'indirizzo FISICO, che il processo da solo non ha modo di sapere.
 *
 * ! SOLO A UN ESEGUIBILE CHE SI CHIAMA *.drv — il flag `is_driver` del PCB,
 * lo stesso varco di ioport_bind e mmio_map (vedi kernel/include/sched.h).
 * Memoria fisicamente contigua e non liberabile e' la risorsa piu' scarsa
 * che ci sia: darla a chiunque la chieda significa che il primo programma
 * distratto la finisce.
 *
 * ! FINO AL 13 AGOSTO 2026 IL CRITERIO ERA «hai gia' una finestra di porte
 * I/O», ed era circolare: le porte le dava ioport_bind, che non controllava
 * niente. Due chiamate e un programma qualunque si prendeva la memoria che
 * non si puo' liberare.
 *
 * ! NON SI PUO' LIBERARE, e la si riprende solo quando il processo muore.
 * Un driver la chiede una volta all'avvio e la tiene per sempre; una
 * dma_free servirebbe a un caso che non esiste, e costerebbe il problema
 * vero — sapere se la scheda ha smesso davvero di scriverci dentro.
 * ============================================================================= */
#define SYS_DMA_ALLOC     239   /* ebx = DmaZona*; memoria per un bus master */

/* =============================================================================
 * SYS_RANDOM — byte imprevedibili
 *
 * ebx = buffer, ecx = quanti byte. Ritorna quanti ne ha scritti, oppure
 * -EAGAIN se il sistema non ne ha ancora raccolti abbastanza.
 *
 * ! NON RIEMPIE MAI CON QUELLO CHE HA. E' la proprieta' che rende questa
 * syscall utilizzabile per una chiave: un generatore che risponde comunque
 * non da' modo a chi chiama di accorgersi che i byte sono deboli, perche'
 * ci sono e sembrano buoni. Qui, se non ce n'e' abbastanza, si torna
 * -EAGAIN e la decisione la prende il programma.
 *
 * ! E NON E' UN GENERATORE. Il kernel raccoglie entropia e la consegna;
 * l'espansione crittografica la fa chi ha un DRBG vero. Il perche' per
 * esteso sta in kernel/arch/x86/entropia.c.
 * ============================================================================= */
#define SYS_RANDOM        240   /* ebx = buf, ecx = len */

/* =============================================================================
 * SYS_MMIO_MAP — i registri di un dispositivo, dentro un processo
 *
 * ebx = puntatore a MmioZona: in `fisico` e `byte` la finestra che il
 * dispositivo decodifica (viene da un BAR PCI), in uscita `virt`, cioe'
 * l'indirizzo con cui il processo la vede.
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' SERVE, VISTO CHE C'E' GIA' ioport_bind
 *
 * NE2000 e PCnet hanno i registri nello spazio I/O, e un driver in ring 3 li
 * raggiunge con in/out. Da vent'anni non e' piu' cosi': i registri stanno in
 * MEMORIA, e senza un modo di mapparla un driver in spazio utente non puo'
 * toccarli. Non e' un caso di scuola — e' successo il 13 agosto 2026 con
 * l'Intel e1000, la scheda predefinita di QEMU: driver scritto, completo, e
 * fermo perche' i suoi registri stanno dietro un BAR di memoria.
 *
 * Questa syscall e' quindi cio' che separa EX-OS da OGNI dispositivo
 * moderno, e insieme il primo gradino del server grafico (DIREZIONE.md).
 *
 * -----------------------------------------------------------------------------
 * ! LE PAGINE SONO NON CACHEABILI, E NON E' UN'OTTIMIZZAZIONE AL CONTRARIO
 *
 * Un registro non e' memoria: leggerlo ha effetti, e il suo valore cambia
 * senza che nessuno scriva. Se le pagine restassero cacheabili, le scritture
 * ai registri resterebbero nella cache e le letture renderebbero valori
 * vecchi. Il driver sembrerebbe funzionare e la scheda non riceverebbe mai i
 * comandi — un guasto che somiglia a un valore, non a un errore.
 *
 * Percio' PG_CACHE_DIS e PG_WRITE_THRU, sempre, senza opzione per toglierli.
 *
 * -----------------------------------------------------------------------------
 * ! NON SI PUO' MAPPARE LA RAM, E QUI STA TUTTA LA SICUREZZA
 *
 * Una syscall che mappa un indirizzo fisico QUALUNQUE dentro un processo non
 * e' un servizio: e' un buco attraverso cui chiunque legge e scrive il
 * kernel. Il controllo su chi chiama (`e' un driver?`) non basta — un driver
 * non deve poter leggere il kernel piu' di chiunque altro.
 *
 * La regola e' che la finestra deve stare INTERAMENTE SOPRA LA RAM. Il PMM
 * conosce l'indirizzo piu' alto della memoria usabile, perche' lo ricava
 * dalla mappa E820 all'avvio; le finestre dei dispositivi su un PC stanno
 * sempre molto piu' in alto (la e1000 di QEMU risponde a 0xfebc0000 con 64
 * MB di RAM, cioe' oltre 4 gigabyte piu' su del limite).
 *
 * Non e' una difesa rigorosa contro un driver ostile — quello ha gia' le
 * porte I/O e il bus mastering — ma toglie di mezzo l'errore vero: un
 * indirizzo sbagliato di qualche cifra che invece di non funzionare mappa il
 * kernel dentro un processo utente.
 *
 * -----------------------------------------------------------------------------
 * ! CHI PUO' CHIAMARLA: SOLO UN ESEGUIBILE CHE SI CHIAMA *.drv
 *
 * Il flag `is_driver` del PCB, che mette il caricatore e nessuna syscall
 * concede. Rifiuta con EPERM chiunque altro. Il criterio, il perche' di
 * questo e non di un altro, e cosa NON garantisce: kernel/include/sched.h e
 * percorso_di_driver() in kernel/loader/elf.c.
 *
 * ! FINO AL 13 AGOSTO 2026 IL CRITERIO ERA «hai gia' delle porte I/O?», e
 * non teneva da nessuno dei due lati. Non teneva chiuso, perche' ioport_bind
 * le dava a chiunque le chiedesse; e non teneva aperto, perche' un
 * framebuffer porte I/O non ne ha nessuna — il primo utente vero di questa
 * syscall dopo l'e1000 sarebbe stato respinto.
 *
 * ! NON SI SMAPPA, come per SYS_DMA_ALLOC: la finestra torna al sistema
 * quando il processo muore. Una mmio_unmap servirebbe a un caso che non
 * esiste — un driver la chiede all'avvio e la tiene — e costerebbe il
 * problema vero, cioe' sapere se la scheda ha smesso davvero di usarla.
 * ========================================================================== */
#define SYS_MMIO_MAP      241   /* ebx = MmioZona*; i registri di una scheda */

/* DUPLICATA A MANO in lib/include/libc.h E in lib/libc.c — TRE copie,
 * perche' libc.c non include libc.h. Devono restare identiche. */
typedef struct {
    uint32_t fisico;    /* in:  base della finestra, da un BAR PCI */
    uint32_t byte;      /* in:  quanto e' larga */
    uint32_t virt;      /* out: dove il processo la vede */
} MmioZona;


/* Quanta ne puo' avere un processo, in pagine. 64 = 256 KB: gli anelli di
 * una scheda di rete con i loro buffer stanno in molto meno. */
#define DMA_PAGINE_MAX    64

typedef struct {
    uint32_t byte;      /* in:  quanti byte servono */
    uint32_t virt;      /* out: indirizzo nel processo */
    uint32_t fisico;    /* out: indirizzo da dare alla scheda */
} DmaZona;

/* =============================================================================
 * SYS_SHM_APRI / SYS_SHM_CHIUDI — le stesse pagine fisiche in due processi
 *
 *     ebx = ShmZona*        apri: nome e byte in ingresso, virt e byte in uscita
 *     ebx = virt            chiudi
 *
 * Gradino 0 punto 2 di DIREZIONE.md. I messaggi IPC sono da 1536 byte: una
 * finestra 640x480x32 sarebbero ~800 messaggi a fotogramma, che non e'
 * lentezza ma architettura sbagliata.
 *
 * Il progetto per esteso — perche' per nome, perche' la zona muore con
 * l'ultimo processo, e perche' il conteggio dei riferimenti sta nel PMM e non
 * qui — sta in kernel/include/shm.h.
 *
 * ! CHI SI ATTACCA NON SCEGLIE LA DIMENSIONE, LA RICEVE. `byte` in ingresso
 * vale solo per chi crea la zona; a tutti gli altri viene reso quanto misura
 * davvero. Due processi con due idee diverse della stessa memoria sono il
 * difetto che questa interfaccia deve rendere impossibile, ed e' per questo
 * che `byte` e' anche un campo di USCITA e non solo d'ingresso.
 * ========================================================================== */
#define SYS_SHM_APRI      242   /* ebx = ShmZona*  */
#define SYS_SHM_CHIUDI    243   /* ebx = virt      */

/* =============================================================================
 * SYS_POLL (244) — aspettare piu' sorgenti insieme
 *
 *     ebx = struct pollfd*   ecx = quanti   edx = scadenza in ms
 *
 * Gradino 0 punto 3 di DIREZIONE.md. Senza, un processo che debba badare a
 * piu' cose puo' solo aspettarne UNA e interrogare le altre a giro: e' attesa
 * attiva, funziona e brucia la CPU. Un server a finestre aspetta tastiera,
 * mouse e N client, e non puo' scegliere quale.
 *
 * La scadenza segue POSIX: <0 aspetta per sempre, 0 guarda e torna subito
 * («e' pronto qualcosa ADESSO?»), >0 e' un limite in millisecondi. Il PIT
 * batte a 100 Hz, quindi sotto i 10 ms non c'e' niente da rappresentare e una
 * scadenza piu' fine viene arrotondata a un tick.
 *
 * Rende quante voci hanno `revents` diverso da zero, 0 se e' scaduta senza
 * che succedesse niente, o un -errno.
 *
 * -----------------------------------------------------------------------------
 * ! LA MAILBOX IPC SI NOMINA CON FD_IPC, E NON DIVENTA UN DESCRITTORE
 *
 * Su questo sistema la meta' degli eventi non passa dai file: i driver, i
 * servizi e le notifiche di IRQ arrivano tutti nella mailbox IPC. Un poll che
 * sapesse guardare solo i descrittori lascerebbe fuori proprio le sorgenti
 * per cui e' stato chiesto.
 *
 * Si nomina con `fd = FD_IPC`, che vale MAX_FD: un numero non negativo — cosi'
 * i valori negativi conservano il significato POSIX di «salta questa voce» —
 * e fuori dall'intervallo dei descrittori veri, quindi non puo' collidere con
 * nessuno. POLLIN vuol dire «c'e' un messaggio da leggere».
 *
 * Farne un descrittore vero sarebbe stato piu' elegante da fuori e sbagliato
 * da dentro: la mailbox non si apre, non si chiude, non si eredita e non si
 * duplica. Un fd che non fa niente di cio' che fanno gli fd e' una bugia
 * comoda per una riga sola di codice.
 *
 * -----------------------------------------------------------------------------
 * ! I FILE NORMALI SONO SEMPRE PRONTI, e non e' una scorciatoia: e' quello che
 * dice POSIX. Una read su un file non blocca mai — al piu' arriva alla fine —
 * quindi rispondere «pronto» e' la verita'. Un poll che aspettasse su un file
 * aspetterebbe per sempre qualcosa che e' gia' successo.
 * ========================================================================== */
#define SYS_POLL          244   /* ebx = pollfd*, ecx = n, edx = ms */

/* =============================================================================
 * SYS_MODO_TESTO (245) — rimette lo schermo, senza BIOS e senza argomenti
 *
 * Gradino 0 punto 4 di DIREZIONE.md: la rete di sicurezza della grafica. Se un
 * server grafico muore con la scheda in modalita' grafica, il sistema e' vivo
 * ma lo schermo resta congelato su cio' che c'era, e un comando digitato alla
 * cieca viene eseguito senza che se ne veda l'effetto. Questa syscall e' cio'
 * che si digita alla cieca.
 *
 * Il lavoro sta in kernel/arch/x86/vga_modo3.c, e li' c'e' anche il limite
 * dichiarato su hardware reale con una VESA attiva.
 *
 * ! NON E' RISERVATA AI DRIVER, ed e' una scelta contro l'istinto. Rimettere
 * il video tocca tutto lo schermo, quindi somiglia a un'operazione da
 * privilegiati; ma serve PROPRIO QUANDO il processo privilegiato e' morto, e
 * una rete di sicurezza che si possa tirare solo dai driver e' una rete che
 * non c'e' nel momento in cui serve. Il danno che puo' fare — rimettere il
 * testo a chi guardava della grafica — e' esattamente cio' per cui esiste.
 *
 * Rende sempre 0: non c'e' un modo di fallire che chi chiama possa gestire, e
 * qualunque risposta diversa da «fatto» sarebbe un'informazione a cui, con lo
 * schermo rotto, nessuno potrebbe reagire.
 * ========================================================================== */
#define SYS_MODO_TESTO    245   /* nessun argomento */

/* =============================================================================
 * SYS_VIDEO_INFO (246) — dov'e' il framebuffer e che forma ha
 *
 * ! SENZA QUESTO, SYS_MMIO_MAP NON BASTA A DISEGNARE. Quella sa mappare una
 * finestra fisica qualunque, ma l'indirizzo del framebuffer lo conosce solo
 * il kernel: gliel'ha dato Stage 2, che e' l'unico che poteva chiederlo al
 * BIOS in modo reale. Un server grafico in ring 3 senza questa informazione
 * dovrebbe INDOVINARE un indirizzo fisico — cioe' fare esattamente cio' che
 * il varco dei driver serve a impedire.
 *
 * ! NON CONCEDE NIENTE, DICE DEI NUMERI. Mappare resta un privilegio da
 * .drv; chi non lo e', con questi numeri non ci fa niente. Per questo non e'
 * riservata ai driver: `hwinfo` ha lo stesso diritto di dire a che
 * risoluzione sta girando lo schermo.
 *
 * ! larghezza == 0 VUOL DIRE MODO TESTO, ed e' il caso normale. Non e' un
 * errore: vuol dire che nessuno ha chiesto la grafica, o che la scheda non
 * offriva la risoluzione richiesta e Stage 2 ha ripiegato sul testo.
 *
 *     VideoInfo v;
 *     if (video_info(&v) == 0 && v.larghezza) {
 *         MmioZona m;
 *         m.fisico = v.fisico;
 *         m.byte   = v.passo * v.altezza;
 *         mmio_map(&m);          // solo da un .drv
 *     }
 * ========================================================================== */
typedef struct {
    uint32_t fisico;        /* indirizzo fisico, 0 = modo testo */
    uint32_t passo;         /* byte per riga di scansione */
    uint32_t larghezza;     /* pixel */
    uint32_t altezza;       /* pixel */
    uint32_t bit;           /* bit per pixel: 16, 24 o 32 */
} VideoInfo;

#define SYS_VIDEO_INFO    246   /* ebx = VideoInfo* */

/* =============================================================================
 * SYS_LOG (247) — una riga sul log del kernel, cioe' sulla SERIALE
 *
 * ! SERVE A CHI NON PUO' USARE printf, E NON E' UN DOPPIONE. printf scrive
 * sulla console del processo: se quella console non e' quella a video, quel
 * messaggio non lo legge nessuno — e se il processo e' un server grafico che
 * gira su una console sua, non lo legge nessuno MAI.
 *
 * E' esattamente il caso in cui ci si e' fermati il 14 agosto 2026: il server
 * a finestre avviato sulla console 1 non dipingeva, e i suoi messaggi d'errore
 * finivano proprio sulla console che non si riusciva a guardare. Uno strumento
 * cieco costa piu' del difetto che deve trovare.
 *
 * ! NON E' UN CANALE DI DATI. Una riga per chiamata, tagliata a LOG_MAX, e
 * marcata con il PID di chi l'ha scritta: e' un diario, non un flusso. Chi ha
 * dati da consegnare usa una pipe.
 * ========================================================================== */
#define SYS_LOG           247   /* ebx = const char*, ecx = lunghezza */
#define SYS_LOG_MAX       200

/* ==========================================================================
 * SYS_LIB_APRI (248) — aggancia una libreria condivisa a questo processo
 *
 *     ebx = const char *percorso      es. "/exwin/lib/exwin.so"
 *     rende l'indirizzo della TABELLA DI ESPORTAZIONE, o un -errno
 *
 * ! RENDE UN INDIRIZZO, NON UNA MANIGLIA, e va bene che sia positivo: la
 * fascia delle librerie e' 0x04000000-0x08000000, quindi il valore non puo'
 * mai essere confuso con un -errno. Una maniglia vorrebbe una tabella per
 * processo per tradurla, e non servirebbe a niente: quello che il chiamante
 * vuole e' proprio l'indirizzo da cui leggere i nomi.
 *
 * ! LA LIBRERIA SI CARICA UNA VOLTA SOLA per tutto il sistema; il secondo
 * processo che la chiede si vede mappare LE STESSE pagine di codice. Vedi
 * kernel/loader/lib.c per cosa si condivide e cosa no.
 *
 * La tabella e' un ExLibTesta — vedi lib/include/exlib.h.
 * ========================================================================== */
#define SYS_LIB_APRI      248   /* ebx = const char* */

/* ==========================================================================
 * SYS_FB_MAP (249) — il framebuffer, e SOLO quello
 *
 * Rende l'indirizzo virtuale del framebuffer mappato nel processo, o -errno.
 * Nessun argomento: e' il kernel a sapere dov'e' e quanto e' grande.
 *
 * ! E' UNA CAPACITA' STRETTA AL POSTO DI UNA LARGA. mmio_map() mappa un
 * indirizzo fisico QUALUNQUE ed e' di root; il server grafico non vuole un
 * indirizzo qualunque, vuole IL framebuffer. Chiedere «mappami quello che sai
 * tu» toglie a chi chiama la possibilita' di sbagliare e a chi attacca quella
 * di scegliere — ed e' cosi' che la grafica gira senza privilegi.
 * ========================================================================== */
#define SYS_FB_MAP        249

/* -----------------------------------------------------------------------------
 * SYS_INTERROMPI (250) — «smettila», detto da fuori
 *
 * ebx = pid.  Rende 0, oppure -ESRCH / -EPERM / -EINVAL.
 *
 * Il bersaglio ESCE con codice 130 (128 + 2, come una shell Unix riporta un
 * Ctrl+C). Non e' un segnale: non c'e' gestore, non c'e' maschera, non si puo'
 * ignorare. Vedi il commento esteso su sys_interrompi in syscall_impl.c.
 * --------------------------------------------------------------------------- */
#define SYS_INTERROMPI    250   /* ebx = pid */

/* -----------------------------------------------------------------------------
 * SYS_PTY_APRI (251) / SYS_PTY_CTL (252) — pseudo-terminali
 *
 * pty_apri:  ebx = int fd[2] — fd[0] master, fd[1] slave. Stessa forma di
 *            pipe(), e le stesse regole: chi tiene il master chiude lo slave.
 * pty_ctl:   ebx = fd, ecx = comando (PTY_CTL_*), edx = argomento.
 *
 * Il perche' di tutto sta in kernel/include/pty.h.
 * --------------------------------------------------------------------------- */
#define SYS_PTY_APRI      251   /* ebx = int[2] */
#define SYS_PTY_CTL       252   /* ebx = fd, ecx = cmd, edx = arg */

/* =============================================================================
 * SYS_STATPERM (253) — proprietario e permessi di un file
 *
 * ! E' UNA CHIAMATA IN PIU', NON UN CAMPO IN PIU' A `Stat`, ed e' la stessa
 * regola gia' scritta accanto a spawn_su_console in libc.h: cambiare una
 * struttura che i programmi si passano gia' vuol dire ricostruire tutto cio'
 * che la usa — e `Stat` la usa chiunque apra un file. Un'aggiunta che non
 * rompe niente costa una chiamata; una che rompe tutto costa un pomeriggio a
 * chi non sa perche'.
 *
 * ! I DATI C'ERANO GIA' E NON USCIVANO. `VfsStat` porta `modo`, `uid` e `gid`
 * dal luglio in cui ext2 ha imparato i proprietari, ma `sys_stat` non li
 * copiava: la `Stat` dello spazio utente ha cinque campi e `struct stat`
 * dichiarava «st_uid sempre 0: non ci sono utenti», che era vero prima di
 * /bin/login. Mancava il trasporto, non l'informazione.
 *
 * ! `modo == 0` VUOL DIRE «QUESTO VOLUME NON HA PROPRIETARI» e non «nessun
 * permesso»: su FAT non esistono, e il VFS lo dice cosi' (vedi stat_interno).
 * Chi stampa deve distinguere i due casi, o mostrerebbe `----------` su ogni
 * file di un floppy.
 * ============================================================================= */
#define SYS_STATPERM      253   /* ebx = percorso, ecx = StatPerm*, edx = sizeof */

typedef struct {
    uint16_t modo;      /* permessi POSIX (0644, 0755...), 0 = non ci sono */
    uint16_t uid;
    uint16_t gid;
} StatPerm;

/* =============================================================================
 * SYS_SU (254) — diventare root provando di sapere una password
 *
 * ! E' UNA CAPACITA' STRETTA, NON UNA LARGA, ed e' lo stesso principio con cui
 * `fb_map` ha sostituito `mmio_map`. L'alternativa classica — il bit setuid sui
 * file — renderebbe pericoloso OGNI eseguibile che lo porta, e su un sistema
 * senza NX e senza modo di controllarli tutti sarebbe una porta che non si
 * richiude. Qui c'e' un solo modo di diventare root: sapere una password.
 *
 * ! LA VERIFICA STA NEL KERNEL PERCHE' DEVE STARCI. /boot/ombra e' 0600: un
 * processo di un utente normale non puo' leggerlo, ed e' esattamente il punto.
 * Se controllasse `su` bisognerebbe consegnargli qualcosa di quel file, e
 * allora il file potrebbe anche essere pubblico.
 *
 * ! LA REGOLA E' UNA SOLA: DIMOSTRA DI ESSERE X, E SE X PUO' FARE ROOT LO
 * DIVENTI. X puo' fare root se e' uid 0, oppure se il suo nome sta in
 * /boot/amministratori. Cosi' «entro come root con la sua password» e «sono un
 * amministratore e uso la MIA» sono lo stesso meccanismo con due dati diversi,
 * invece che due strade da tenere d'accordo.
 *
 * ebx = nome, ecx = password. Rende 0, oppure -EPERM.
 * ============================================================================= */
#define SYS_SU            254

/* La mailbox IPC come sorgente. MAX_FD e' il primo numero che un descrittore
 * vero non puo' avere: vedi sched.h. */
#define FD_IPC            32
#define POLL_MAX          16    /* voci per chiamata: il kernel le copia */

#define POLLIN        0x0001    /* c'e' qualcosa da leggere */
#define POLLOUT       0x0004    /* si puo' scrivere senza bloccare */
#define POLLERR       0x0008    /* errore: solo in uscita */
#define POLLHUP       0x0010    /* l'altro capo se n'e' andato: solo in uscita */
#define POLLNVAL      0x0020    /* descrittore non valido: solo in uscita */

/* DUPLICATA A MANO in lib/include/libc.h E in lib/libc.c — TRE copie,
 * perche' libc.c non include libc.h. Devono restare identiche.
 * I campi sono `int` e `short` come su POSIX. */
struct pollfd {
    int   fd;
    short events;       /* in:  cosa aspetto */
    short revents;      /* out: cosa e' successo */
};

#define SHM_CREA          0x0001    /* creala se non c'e' (senza: solo attacco) */

/* DUPLICATA A MANO in lib/include/libc.h E in lib/libc.c — TRE copie,
 * perche' libc.c non include libc.h. Devono restare identiche. */
typedef struct {
    char     nome[16];  /* in:  come si chiama la zona */
    uint32_t byte;      /* in:  quanto e' grande (solo se la si crea)
                         * out: quanto e' grande DAVVERO */
    uint32_t flag;      /* in:  SHM_CREA, oppure 0 per il solo attacco */
    uint32_t virt;      /* out: dove il processo la vede */
} ShmZona;

/* Opzioni di sys_waitpid (terzo argomento, edx). Un chiamante che
 * passa solo due registri lascia in edx un valore qualunque: tutti i
 * wrapper devono usare la forma a tre argomenti con options=0. */
#define WNOHANG     0x0001  /* non bloccare se nessun figlio e' finito */

/* DUPLICATA A MANO in lib/include/libc.h e lib/libc.c. */
typedef struct {
    uint32_t totale;    /* quante console esistono */
    uint32_t mia;       /* quella del processo chiamante */
    uint32_t visibile;  /* quella attualmente a video */
    uint32_t fg;        /* PID in primo piano sulla console del chiamante */
} ConsoleInfo;

/* Converti errno in valore di ritorno negativo */
#define ERR(e)      (-(int32_t)(e))

/* =============================================================================
 * Flag per sys_open
 * ============================================================================= */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_NONBLOCK  0x0800

/* =============================================================================
 * Comandi di sys_fcntl
 *
 * I numeri sono quelli di Linux, come tutto il resto della numerazione.
 *
 * ! FD_CLOEXEC NON HA NIENTE DA CHIUDERE. In EX-OS spawn() non eredita i
 * descrittori del padre — il figlio riceve i suoi da SpawnAzione, per
 * percorso — quindi non esiste il momento in cui un fd "sopravvive a un
 * exec". F_GETFD risponde sempre 0 e F_SETFD accetta e dimentica: sono
 * li' perche' il codice di terzi li chiama a coppie (leggi i flag,
 * riscrivili con FD_CLOEXEC in piu') e vuole due successi, non perche'
 * cambino qualcosa.
 * ============================================================================= */
#define F_DUPFD     0   /* duplica su un numero >= arg */
#define F_GETFD     1   /* sempre 0; serve a dire "questo fd esiste" */
#define F_SETFD     2   /* accettata e ignorata: vedi sopra */
#define F_GETFL     3   /* i flag passati a open() */
#define F_SETFL     4   /* solo O_APPEND e O_NONBLOCK sono modificabili */

#define FD_CLOEXEC  1

/* =============================================================================
 * Flag per sys_mmap
 * ============================================================================= */
#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20
#define MAP_FIXED       0x10

/* =============================================================================
 * Struttura stat (per sys_stat)
 * ============================================================================= */
typedef struct {
    uint32_t    st_size;        /* Dimensione file */
    /* Identita' del file: due percorsi danno lo STESSO numero se e solo se
     * sono lo stesso oggetto. Si chiamava st_first_clus e valeva sempre 0
     * fuori da FAT12; e' diventato st_ident ad agosto 2026 — la stessa
     * parola nella stessa posizione, quindi nessuna ABI cambia. Il perche'
     * sta in stat_interno(), kernel/fs/vfs.c. */
    uint32_t    st_ident;
    uint16_t    st_attr;        /* Attributi FAT12 */
    uint16_t    st_date;        /* Data modifica */
    uint16_t    st_time;        /* Ora modifica */
} Stat;

/* =============================================================================
 * Struttura voce di directory (per sys_readdir)
 * ============================================================================= */
/* =============================================================================
 * 256 = 255 caratteri + NUL, cioe' il massimo che ext2 ammette.
 *
 * Era 13 ("NOME8.EXT3" + NUL) perche' l'unico filesystem era FAT12, dove
 * un nome piu' lungo NON PUO' esistere. Con ext2 esiste, e una struttura
 * che non lo contiene non "tronca un caso limite": rende irraggiungibili
 * dei file, perche' il nome troncato non apre niente.
 *
 * Il prezzo e' che DirEntry passa da 20 a 264 byte, e chi ne chiede un
 * blocco paga 264 byte a voce. Per questo il tetto per chiamata
 * (READDIR_MAX_BATCH) e' sceso da 64 a 16: e' il numero che moltiplica.
 * ============================================================================= */
#define DIRENT_NAME_MAX 256

/* =============================================================================
 * ! `ident` E' LA STESSA IDENTITA' DI Stat.st_ident, E DEVE ESSERLO.
 *
 * L'ha aggiunta agosto 2026, e prima non c'era: la readdir() della libc
 * scriveva `d_ino = 0` su OGNI voce, perche' non aveva niente da metterci.
 *
 * Zero non e' un valore neutro per un numero di inode. POSIX dice che
 * d_ino e' il numero di serie del file, e il codice di terzi ci conta:
 * GNU make salta le voci con d_ino == 0 (`REAL_DIR_ENTRY(dp)` in dir.c —
 * su Unix quello zero marca una voce CANCELLATA). Il risultato era che
 * make vedeva ogni directory VUOTA. Il sintomo non parlava di directory:
 *
 *     make: *** No targets specified and no makefile found.  Stop.
 *
 * dentro una directory con dentro un `makefile` che `ls` elencava e che
 * `make -f makefile` leggeva senza storie. La differenza fra le due strade
 * e' che quella senza -f passa dalla lettura della directory.
 *
 * ! E' LO STESSO DIFETTO DI st_first_clus, un livello piu' in la'. Quello
 * dava a ogni file la stessa identita' e GCC concludeva che le sue sei
 * directory di include erano una sola; questo la dava a zero e make
 * concludeva che le directory erano vuote. La lezione, due volte: un campo
 * di identita' riempito con una costante non e' "non implementato", e'
 * un'informazione FALSA, e chi la legge non ha modo di accorgersene.
 *
 * ! SI COMPONE CON VFS_IDENT, la stessa macro di stat_interno(): d_ino e
 * st_ino devono combaciare per lo stesso file, o il codice che li confronta
 * — ed e' normale confrontarli — vedrebbe due file dove ce n'e' uno.
 *
 * ! AGGIUNGERLA HA CAMBIATO L'ABI (264 -> 268 byte). E' nell'impronta di
 * tools/abi-bersaglio.c proprio da questa volta: `struct dirent` c'era
 * gia', DirEntry no — e DirEntry e' quella che attraversa la syscall.
 * ============================================================================= */
typedef struct {
    char     name[DIRENT_NAME_MAX];
    uint32_t size;
    uint32_t ident;
    uint8_t  is_dir;
} DirEntry;

/* =============================================================================
 * Stato della memoria fisica, per fascia (per sys_meminfo)
 *
 * Tutti i valori sono in KB e si riferiscono alla memoria FISICA, contata
 * interrogando la bitmap del PMM. "usata" si ricava come totale - libera.
 *
 * LE FASCE, e perche' sono queste. Sono le tre dell'architettura PC, non
 * una scelta di questo sistema:
 *
 *   convenzionale  0x00000-0x9FFFF   i primi 640 KB
 *   superiore/UMA  0xA0000-0xFFFFF   384 KB riservati a BIOS, video, ROM
 *   estesa (XMS)   da 0x100000       tutto il resto, dove vive il sistema
 *
 * MEMORIA ESPANSA (EMS): i campi ems_* esistono e valgono SEMPRE ZERO.
 * Non e' una lacuna da colmare — l'espansa e' un meccanismo a banchi
 * commutati (una scheda EMS, o un emulatore tipo EMM386) che affaccia
 * finestre di memoria dentro l'area superiore, ed era il modo di superare
 * il limite di 1 MB del modo reale su 8086/286. Su un 386+ in modo
 * protetto con paginazione quel limite non esiste: tutta la RAM oltre 1 MB
 * e' direttamente indirizzabile come memoria estesa. EX-OS non ha, e non
 * avrebbe motivo di avere, un gestore EMS. I campi restano per rendere la
 * risposta esplicita invece di far sembrare che manchi un dato.
 * ============================================================================= */
typedef struct {
    uint32_t conv_total_kb, conv_free_kb;   /* convenzionale, < 640 KB   */
    uint32_t uma_total_kb,  uma_free_kb;    /* superiore, 640 KB - 1 MB  */
    uint32_t ext_total_kb,  ext_free_kb;    /* estesa, >= 1 MB           */
    uint32_t ems_total_kb,  ems_free_kb;    /* espansa: sempre 0, v. sopra */
    uint32_t total_kb,      free_kb;        /* complessivi               */
    uint32_t page_size;                     /* granularita' del PMM      */
} MemInfo;

/* =============================================================================
 * Descrizione di un processo e dei suoi stack (per sys_procinfo)
 *
 * Espone gli indirizzi grezzi invece di dimensioni gia' calcolate: sono
 * loro a dire COME lo stack e' stato allocato, che e' la domanda vera.
 * Le dimensioni si ricavano per differenza, e il programma che le mostra
 * puo' scegliere come presentarle.
 *
 *   ustack_top    indirizzo piu' alto dello stack utente (fisso)
 *   ustack_base   pagina piu' bassa ATTUALMENTE mappata. Scende quando lo
 *                 stack cresce su fault: top - base = RAM impegnata ORA
 *   ustack_limit  confine della riserva. top - limit = spazio riservato.
 *                 Vale 0 per i task kernel, che non hanno stack utente
 *   kstack_base/top  stack kernel, allocato per intero alla creazione del
 *                 processo e mai cresciuto: la' KERNEL_STACK_SIZE e' ancora
 *                 una dimensione fissa
 *
 * Un ustack_limit a 0 con ustack_top a 0 identifica un task kernel (idle,
 * init): non e' un dato mancante.
 * ============================================================================= */
#define PROCINFO_NAME_MAX   32   /* deve coincidere con PROCESS_NAME_LEN */
#define PROCINFO_MAX_BATCH  16   /* voci per chiamata: limita lo stack kernel */

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t state;          /* ProcState: 1=READY 2=RUNNING 3=BLOCKED
                              * 4=ZOMBIE 5=SLEEPING 6=NASCENTE (creato, non
                              * ancora caricato) */
    uint32_t prio;
    char     name[PROCINFO_NAME_MAX];
    uint32_t ustack_top;
    uint32_t ustack_base;
    uint32_t ustack_limit;
    uint32_t kstack_base;
    uint32_t kstack_top;
} ProcInfo;


/* =============================================================================
 * Disco fisico e sua tabella delle partizioni (per sys_diskinfo)
 *
 * I 64 bit viaggiano SPEZZATI in due uint32 (_lo/_hi). Non e' eleganza
 * mancata: questa struttura attraversa l'ABI della syscall ed e'
 * duplicata a mano in tre file (qui, lib/include/libc.h, lib/libc.c). Un
 * uint64_t dentro una struct condivisa introdurrebbe un allineamento a 8
 * byte su cui kernel e libc devono concordare esattamente; con due
 * uint32 il problema non esiste. Le stringhe sono dimensionate a
 * multipli di 4 per lo stesso motivo.
 *
 * settori_*  quanto il disco dichiara ORA (IDENTIFY DEVICE)
 * nativi_*   capacita' di fabbrica (READ NATIVE MAX ADDRESS)
 * clippato   1 se nativi > settori: c'e' spazio NASCOSTO, tipicamente una
 *            HPA o un jumper di limitazione. E' il caso in cui un disco
 *            da 64 GB si presenta come da 32.
 * ============================================================================= */
#define DISKINFO_MAX_PART   16

typedef struct {
    uint32_t attiva;        /* 0x80 = avviabile */
    uint32_t tipo;          /* byte di tipo MBR */
    uint32_t logica;        /* 1 = dentro la partizione estesa */
    uint32_t numero;        /* numero alla fdisk: 1-4 primarie, 5+ logiche */
    uint32_t inizio_lo, inizio_hi;
    uint32_t settori_lo, settori_hi;
    /* Filesystem riconosciuto leggendo il BPB della partizione (vedi
     * kernel/block/vol.c). fs_tipo usa i valori VOL_FS_*: 0 sconosciuto,
     * 12/16/32 la larghezza della FAT, 255 illeggibile. Il tipo NON viene
     * dal byte di tipo MBR, che e' solo un suggerimento: viene dal numero
     * di cluster, che e' l'unico criterio corretto. */
    uint32_t fs_tipo;
    uint32_t fs_incoerente;
    uint32_t fs_sett_per_clu;
    uint32_t fs_n_cluster;
    char     fs_etichetta[12];
} PartInfo;

typedef struct {
    uint32_t presente;
    uint32_t tipo;          /* 0 nessuno, 1 ATA, 2 ATAPI, 3 sconosciuto */
    uint32_t canale;        /* 0 primario, 1 secondario */
    uint32_t unita;         /* 0 master, 1 slave */
    uint32_t lba48;
    uint32_t hpa;
    uint32_t clippato;
    uint32_t settori_lo, settori_hi;
    uint32_t nativi_lo,  nativi_hi;
    char     modello[44];
    char     seriale[24];
    char     firmware[12];
    uint32_t schema;        /* 0 nessuno, 1 MBR, 2 GPT (protettivo) */
    uint32_t problemi;      /* maschera PT_PROB_* di kernel/include/mbr.h */
    uint32_t n_part;
    PartInfo part[DISKINFO_MAX_PART];
} DiskInfo;

/* =============================================================================
 * Un dispositivo a blocchi (per sys_blkinfo)
 *
 * `primo` e `settori` descrivono la FINESTRA: ogni accesso viene tradotto
 * e rifiutato se ne esce. E' cio' che impedisce a un filesystem montato
 * su una partizione di toccarne un'altra o la tabella delle partizioni.
 * ============================================================================= */
#define BLKINFO_NOME_MAX    12

typedef struct {
    char     nome[BLKINFO_NOME_MAX];  /* "fd0", "hd0", "hd0p1", "cd0" */
    /* 1 floppy, 2 disco intero, 3 partizione, 4 lettore CD/DVD.
     * Su un lettore `settori` vale zero finche' il supporto non e' stato
     * sondato o se il vassoio e' vuoto: la lunghezza e' del disco
     * inserito, non del dispositivo. */
    uint32_t tipo;
    uint32_t sola_lettura;
    uint32_t primo_lo, primo_hi;      /* LBA di partenza nel supporto */
    uint32_t settori_lo, settori_hi;  /* lunghezza della finestra */
} BlkInfo;

/* =============================================================================
 * Un montaggio attivo (per sys_mountinfo)
 * ============================================================================= */
#define MOUNTINFO_PUNTO_MAX 24

/* Flag di sys_mount (edx) */
#define MNT_SOLA_LETTURA    0x0001

typedef struct {
    char     punto[MOUNTINFO_PUNTO_MAX];  /* "/", "/disk" */
    char     dev[BLKINFO_NOME_MAX];       /* "fd0", "hd0p1" */
    uint32_t fs;                          /* 12, 16, 32 */
    uint32_t sola_lettura;
} MountInfo;

/* Esito dell'installazione dell'avvio (per sys_bootinstall) */
typedef struct {
    uint32_t s2_lba, s2_cnt;
    uint32_t k_lba,  k_cnt;     /* primo intervallo, e settori TOTALI */
    uint32_t k_next;            /* in quanti intervalli e' spezzato il kernel */
    uint32_t disco;
    uint32_t voce;
} BootInstallInfo;

/* =============================================================================
 * Tabella delle partizioni PROPOSTA (per sys_partwrite)
 *
 * Si passano sempre tutti e quattro gli SLOT delle primarie, anche quelli
 * liberi (tipo = 0). Una tabella e' un oggetto unico: consegnarla una
 * voce alla volta produrrebbe stati intermedi in cui le partizioni si
 * sovrappongono, e il kernel non avrebbe modo di validare l'insieme.
 *
 * Le partizioni LOGICHE non si toccano da qui: il kernel rifiuta la
 * proposta se sposta o rimuove l'estesa che le contiene (vedi mbr.h).
 *
 * `problemi` e' un campo di USCITA: quando la chiamata rifiuta con
 * -EINVAL ci trovi la maschera PT_PROB_* di kernel/include/mbr.h, cioe'
 * QUALE controllo non e' passato. Un rifiuto senza quel dettaglio
 * lascerebbe l'utente a indovinare cosa c'e' di sbagliato in una tabella
 * che a occhio sembra sensata.
 * ============================================================================= */
#define PARTWRITE_MAX_VOCI  4

typedef struct {
    uint32_t attiva;                    /* 0x00, oppure 0x80 = avviabile */
    uint32_t tipo;                      /* byte di tipo MBR; 0 = slot libero */
    uint32_t inizio_lo,  inizio_hi;     /* LBA assoluto del primo settore */
    uint32_t settori_lo, settori_hi;    /* lunghezza in settori */
} PartVoce;

typedef struct {
    uint32_t problemi;                  /* USCITA: maschera PT_PROB_* */
    PartVoce voce[PARTWRITE_MAX_VOCI];  /* voce[i] = slot i+1 */
} PartTabella;

/* =============================================================================
 * Accesso ai settori grezzi di una partizione (sys_blkread / sys_blkwrite)
 *
 * Serve a /bin/mkfs: un formattatore scrive strutture — BPB, tabelle FAT,
 * directory radice — che nessun filesystem montato sa produrre, perche' il
 * filesystem e' proprio cio' che sta creando.
 *
 * PERCHE' QUI LA CONCLUSIONE E' OPPOSTA A QUELLA DI bootinst.c. Li' la
 * logica sta nel kernel perche' l'installatore scrive FUORI da ogni
 * filesystem, nel settore 0, dove un errore rende irraggiungibile un disco
 * intero. Un formattatore invece scrive solo DENTRO una partizione, cioe'
 * dentro una finestra che il livello a blocchi fa gia' rispettare: non
 * c'e' niente da proteggere che blk_write() non protegga.
 *
 * Le quattro condizioni, e cosa impedisce ognuna:
 *
 *   solo BLK_TIPO_PART   il disco intero e il floppy non sono nominabili.
 *                        E' cio' che rende il settore 0 — la tabella delle
 *                        partizioni — IRRAGGIUNGIBILE da userspace: non
 *                        esiste un dispositivo che lo contenga e sia
 *                        accettato qui.
 *   non in uso           una partizione montata ha una cache write-back
 *                        sopra (vedi fat.c): scriverci sotto significa che
 *                        il primo fat_sync() ripristina i vecchi settori
 *                        sopra i nuovi. In lettura darebbe dati che non
 *                        corrispondono a quelli che il filesystem crede
 *                        di avere.
 *   non in sola lettura  lo stesso vincolo dei montaggi.
 *   n <= BLKIO_MAX_SETT  limita il lavoro per chiamata. Il kernel copia un
 *                        settore per volta con un buffer di 512 byte, non
 *                        n settori insieme: il costo sullo stack kernel non
 *                        cresce col numero richiesto.
 *
 * ebx = nome*  ("hd0p1")   ecx = lba RELATIVO   edx = n settori
 * esi = buf*   (n * 512 byte)
 *
 * Ritorna il numero di settori trasferiti, o un errno negativo.
 * ============================================================================= */
#define BLKIO_MAX_SETT      64      /* 32 KB per chiamata */

/* =============================================================================
 * Struttura parametri mmap (passata come puntatore in EBX)
 * ============================================================================= */
typedef struct {
    uint32_t    addr;
    uint32_t    length;
    uint32_t    prot;
    uint32_t    flags;
    int32_t     fd;
    uint32_t    offset;
} MmapParams;

/* =============================================================================
 * Tipo handler syscall
 * Ogni syscall riceve l'InterruptFrame e ritorna int32_t
 * (valore positivo = successo, negativo = errore)
 * ============================================================================= */
typedef int32_t (*SyscallFn)(InterruptFrame *frame);

/* =============================================================================
 * Interfaccia pubblica
 * ============================================================================= */
void    syscall_init(void);
void    syscall_handler(InterruptFrame *frame);

/* Implementazioni singole syscall (definite in syscall_impl.c) */
int32_t sys_exit(InterruptFrame *f);
int32_t sys_spawn(InterruptFrame *f);
int32_t sys_read(InterruptFrame *f);
int32_t sys_write(InterruptFrame *f);
int32_t sys_open(InterruptFrame *f);
int32_t sys_close(InterruptFrame *f);
int32_t sys_dup(InterruptFrame *f);
int32_t sys_pipe(InterruptFrame *f);
int32_t sys_rename(InterruptFrame *f);
int32_t sys_dup2(InterruptFrame *f);
int32_t sys_fcntl(InterruptFrame *f);
int32_t sys_waitpid(InterruptFrame *f);
int32_t sys_getpid(InterruptFrame *f);
int32_t sys_getppid(InterruptFrame *f);
int32_t sys_mmap(InterruptFrame *f);
int32_t sys_munmap(InterruptFrame *f);
int32_t sys_ioctl(InterruptFrame *f);
int32_t sys_exec(InterruptFrame *f);
int32_t sys_sched_yield(InterruptFrame *f);
int32_t sys_sleep(InterruptFrame *f);
int32_t sys_sbrk(InterruptFrame *f);
int32_t sys_getcwd(InterruptFrame *f);
int32_t sys_chdir(InterruptFrame *f);
int32_t sys_stat(InterruptFrame *f);
int32_t sys_lseek(InterruptFrame *f);
int32_t sys_readdir(InterruptFrame *f);
int32_t sys_getenv(InterruptFrame *f);
int32_t sys_mkdir(InterruptFrame *f);
int32_t sys_rmdir(InterruptFrame *f);
int32_t sys_unlink(InterruptFrame *f);
int32_t sys_version(InterruptFrame *f);
int32_t sys_uptime(InterruptFrame *f);
int32_t sys_meminfo(InterruptFrame *f);
int32_t sys_procinfo(InterruptFrame *f);
int32_t sys_diskinfo(InterruptFrame *f);
int32_t sys_blkinfo(InterruptFrame *f);
int32_t sys_mount(InterruptFrame *f);
int32_t sys_umount(InterruptFrame *f);
int32_t sys_mountinfo(InterruptFrame *f);
int32_t sys_bootinstall(InterruptFrame *f);
int32_t sys_partwrite(InterruptFrame *f);
int32_t sys_blkread(InterruptFrame *f);
int32_t sys_blkwrite(InterruptFrame *f);
int32_t sys_truncate(InterruptFrame *f);
int32_t sys_reboot(InterruptFrame *f);
int32_t sys_ipc_send(InterruptFrame *f);
int32_t sys_ipc_recv(InterruptFrame *f);
int32_t sys_ipc_recv_tmo(InterruptFrame *f);
int32_t sys_time(InterruptFrame *f);
int32_t sys_console_switch(InterruptFrame *f);
int32_t sys_console_write(InterruptFrame *f);
int32_t sys_console_info(InterruptFrame *f);
int32_t sys_console_setfg(InterruptFrame *f);
int32_t sys_ipc_register(InterruptFrame *f);
int32_t sys_ipc_lookup(InterruptFrame *f);
int32_t sys_irq_bind(InterruptFrame *f);
int32_t sys_poll(InterruptFrame *f);
int32_t sys_modo_testo(InterruptFrame *f);
int32_t sys_video_info(InterruptFrame *f);
int32_t sys_log(InterruptFrame *f);
int32_t sys_lib_apri(InterruptFrame *f);
int32_t sys_interrompi(InterruptFrame *f);
int32_t sys_pty_apri(InterruptFrame *f);
int32_t sys_pty_ctl(InterruptFrame *f);
int32_t sys_statperm(InterruptFrame *f);
int32_t sys_su(InterruptFrame *f);
int32_t sys_fb_map(InterruptFrame *f);
int32_t sys_getuid(InterruptFrame *f);
int32_t sys_setuid(InterruptFrame *f);
int32_t sys_chown(InterruptFrame *f);
int32_t sys_chmod(InterruptFrame *f);
int32_t sys_shm_apri(InterruptFrame *f);
int32_t sys_shm_chiudi(InterruptFrame *f);
int32_t sys_ioport_bind(InterruptFrame *f);
int32_t sys_ioport_in(InterruptFrame *f);
int32_t sys_ioport_out(InterruptFrame *f);
int32_t sys_ioport_in16(InterruptFrame *f);
int32_t sys_ioport_out16(InterruptFrame *f);
int32_t sys_ioport_in32(InterruptFrame *f);
int32_t sys_ioport_out32(InterruptFrame *f);
int32_t sys_irq_done(InterruptFrame *f);
int32_t sys_dma_alloc(InterruptFrame *f);
int32_t sys_mmio_map(InterruptFrame *f);
int32_t sys_random(InterruptFrame *f);

/* =============================================================================
 * SYS_SPAWN — il blocco EXTRA (ambiente e redirezioni)
 *
 * PERCHE' UN BLOCCO E NON DUE ARGOMENTI IN PIU'. La forma storica della
 * syscall e' spawn(percorso, argc, argv) e ci sono in giro programmi —
 * compresi quelli gia' installati su un disco — che la chiamano con tre
 * registri soli. ESI ed EDI, per loro, contengono spazzatura: leggerli
 * come puntatori significherebbe che un binario vecchio, il giorno che
 * lo si esegue su un kernel nuovo, apre file a caso o non parte.
 *
 * Percio' l'estensione passa da UN puntatore in ESI a una struttura che
 * comincia con una parola magica. Se ESI non e' leggibile o la magia non
 * combacia, il kernel fa finta che non ci sia: la vecchia forma continua
 * a funzionare esattamente come prima, e la probabilita' che spazzatura
 * casuale sia insieme un puntatore valido e la magia giusta e' quella di
 * indovinare 32 bit.
 *
 * ! E LA STESSA PROMESSA VALE FRA UNA FORMA DEL BLOCCO E L'ALTRA, dal 24
 * agosto 2026. La magia non dice «capisco» o «non capisco»: dice QUALE FORMA,
 * e il kernel le conosce tutte quelle che sono state pubblicate. Legge tanti
 * byte quanti ne dichiara quella magia, azzera i campi che in quella forma non
 * esistevano e spegne i bit di `flag` che allora non volevano dire niente.
 *
 * Senza, «magia sconosciuta» vuol dire «blocco ignorato», cioe' un programma
 * che parte SENZA redirezioni e SENZA ambiente, in silenzio — e i programmi
 * gia' costruiti sono quelli del CD degli strumenti, `gcc` compreso, che
 * redirige l'uscita di cc1 su un file temporaneo. L'elenco delle forme sta in
 * lib/include/spawn_abi.h; la prova che continuano a funzionare sta in
 * /bin/libctest, e manda al kernel la forma del 14 agosto.
 *
 * L'AMBIENTE si eredita per copia, come argv: le stringhe finiscono sullo
 * stack del figlio. Non c'e' un ambiente "del sistema" che i processi
 * condividono — quello di /boot/kernel.cfg resta consultabile con
 * SYS_GETENV ed e' il ripiego di getenv() per le chiavi che il padre non
 * ha passato.
 *
 * LE REDIREZIONI sono per PERCORSO e non per descrittore aperto, ed e' una
 * scelta: passare un fd del padre vorrebbe dire due processi sullo stesso
 * handle VFS, cioe' un conteggio di riferimenti che oggi non c'e' e una
 * chiusura che sfila il file da sotto i piedi all'altro. Il figlio apre
 * il proprio. Basta a `gcc`, che redirige l'uscita di cc1 su un file
 * temporaneo; non basta alle pipe, che infatti non ci sono ancora.
 * ============================================================================= */
/* ! LA STRUTTURA STA IN UN FILE SOLO dal 17 agosto 2026. Ne esistevano
 * quattro copie e una e' rimasta indietro: la shell ha perso redirezioni e
 * ambiente per tre giorni, in silenzio. Vedi lib/include/spawn_abi.h, che
 * spiega per esteso com'e' andata. */
#include "spawn_abi.h"

/* Verifica indirizzo utente (evita accessi kernel da ring3) */
int     syscall_verify_ptr(const void *ptr, uint32_t size);
int     syscall_verify_str(const char *str, uint32_t max_len);

#endif /* SYSCALL_H */

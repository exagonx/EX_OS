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
#define SYSCALL_COUNT   238     /* deve coprire il numero syscall più alto (SYS_IRQ_DONE=237) + 1 */

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
 * ⚠️ IL BYTE NON BASTA, E NON E' UNA QUESTIONE DI COMODITA'.
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
 * ⚠️ IN32 RESTITUISCE IL VALORE FUORI BANDA, LE ALTRE NO. Una lettura di
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
 * ⚠️ FD_CLOEXEC NON HA NIENTE DA CHIUDERE. In EX-OS spawn() non eredita i
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
    uint32_t    st_first_clus;  /* Primo cluster FAT12 */
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

typedef struct {
    char     name[DIRENT_NAME_MAX];
    uint32_t size;
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
                              * 4=ZOMBIE 5=SLEEPING */
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
int32_t sys_ioport_bind(InterruptFrame *f);
int32_t sys_ioport_in(InterruptFrame *f);
int32_t sys_ioport_out(InterruptFrame *f);
int32_t sys_ioport_in16(InterruptFrame *f);
int32_t sys_ioport_out16(InterruptFrame *f);
int32_t sys_ioport_in32(InterruptFrame *f);
int32_t sys_ioport_out32(InterruptFrame *f);
int32_t sys_irq_done(InterruptFrame *f);

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
/* ⚠️ 'SPNY' E NON PIU' 'SPNX' (agosto 2026): e' cambiata la disposizione
 * di SpawnAzione, che ha due campi in piu'. Un binario compilato per la
 * forma vecchia verrebbe letto storto, e una redirezione letta storta
 * scrive nel file sbagliato. Con la magia nuova il kernel non riconosce il
 * blocco e lo ignora, che e' il modo meno dannoso di sbagliare. */
#define SPAWN_EXTRA_MAGIA    0x53504E59u   /* 'SPNY' */
#define SPAWN_MAX_AZIONI     4
#define SPAWN_RED_PATH_MAX   128

/* Le due cose che si possono fare a un descrittore del figlio. */
#define SPAWN_AZ_FILE   0   /* apri `percorso` e mettilo su `fd` */
#define SPAWN_AZ_FD     1   /* ⚠️ dai al figlio il descrittore `fd_padre` DEL PADRE */

typedef struct {
    uint32_t tipo;                        /* SPAWN_AZ_FILE / SPAWN_AZ_FD */
    uint32_t fd;                          /* descrittore del FIGLIO da sostituire */
    uint32_t flags;                       /* O_RDONLY/O_WRONLY/O_CREAT/... */
    int32_t  fd_padre;                    /* SPAWN_AZ_FD: quale fd del padre */
    char     percorso[SPAWN_RED_PATH_MAX];
} SpawnAzione;

typedef struct {
    uint32_t    magia;                    /* SPAWN_EXTRA_MAGIA, o il blocco e' ignorato */
    char      **envp;                     /* NULL-terminato; NULL = nessun ambiente */
    uint32_t    n_azioni;
    SpawnAzione azioni[SPAWN_MAX_AZIONI];
} SpawnExtra;

/* Verifica indirizzo utente (evita accessi kernel da ring3) */
int     syscall_verify_ptr(const void *ptr, uint32_t size);
int     syscall_verify_str(const char *str, uint32_t max_len);

#endif /* SYSCALL_H */

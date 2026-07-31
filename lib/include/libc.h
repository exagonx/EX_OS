/* =============================================================================
 * lib/include/libc.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef LIBC_H
#define LIBC_H

typedef unsigned int    size_t;
typedef int             ssize_t;
typedef unsigned int    uintptr_t;
typedef int             intptr_t;
#define NULL ((void*)0)

/* Stringa */
size_t  strlen(const char *s);
char   *strcpy(char *dst, const char *src);
char   *strncpy(char *dst, const char *src, size_t n);
int     strcmp(const char *a, const char *b);
int     strncmp(const char *a, const char *b, size_t n);
char   *strcat(char *dst, const char *src);
char   *strchr(const char *s, int c);
char   *strrchr(const char *s, int c);

/* Memoria */
void   *memset(void *dst, int c, size_t n);
void   *memcpy(void *dst, const void *src, size_t n);
void   *memmove(void *dst, const void *src, size_t n);
int     memcmp(const void *a, const void *b, size_t n);

/* Stdio */
int     putchar(int c);
int     puts(const char *s);
int     getchar(void);
char   *gets(char *buf, int max);
int     printf(const char *fmt, ...);

/* Stdlib */
int     atoi(const char *s);
void    exit(int code);
void    abort(void);
void   *malloc(size_t size);
void    free(void *ptr);
void   *calloc(size_t nmemb, size_t size);
void   *realloc(void *ptr, size_t size);

/* Syscall wrapper */
int     open(const char *path, int flags);
int     close(int fd);
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int     getpid(void);
int     chdir(const char *path);

/* Crea una directory. Nessun parametro 'mode': FAT12 non ha permessi.
 * Ritorna 0, o un errno negativo — in particolare -17 (EEXIST) se il nome
 * e' gia' occupato e -38 (ENOSYS) se il percorso richiederebbe piu' di un
 * livello di annidamento, che questo FAT12 non risolve. */
int     mkdir(const char *path);

/* Cancella una directory VUOTA. Ritorna 0, o un errno negativo:
 * -2 non trovata, -20 non e' una directory, -39 non vuota. */
int     rmdir(const char *path);

/* Cancella un file. Ritorna 0, -2 se non esiste, -21 se e' una directory
 * (per quelle serve rmdir). Non espande caratteri jolly: accetta un nome
 * preciso. */
int     unlink(const char *path);
char   *getcwd(char *buf, size_t size);
void    sched_yield(void);
void    usleep(unsigned int us);
void    sleep(unsigned int sec);

/* Millisecondi dall'avvio. Serve a dare una scadenza REALE a un'attesa
 * senza contare iterazioni (che dipende dalla velocita' della CPU) e senza
 * dormire a passi (che non puo' scendere sotto il tick da 10 ms).
 *
 * Avanza a scatti di 10 (PIT a 100Hz) e torna a zero dopo ~24,8 giorni:
 * confrontare sempre DIFFERENZE fra due letture in aritmetica senza segno,
 *   unsigned inizio = uptime_ms();
 *   ... while (uptime_ms() - inizio < timeout_ms) ...
 * che attraversa il wrap correttamente. Mai confrontare valori assoluti. */
unsigned int uptime_ms(void);

/* =============================================================================
 * Stato della memoria fisica, per fascia. Valori in KB; "usata" = totale -
 * libera. STRUTTURA DUPLICATA A MANO da kernel/include/syscall.h (stessa
 * convenzione di DirEntry): deve restare identica, e meminfo() fallisce
 * con -EINVAL se le due copie divergono di dimensione.
 *
 * Le fasce sono quelle dell'architettura PC:
 *   convenzionale  < 640 KB
 *   superiore/UMA  640 KB - 1 MB (BIOS, video, ROM)
 *   estesa (XMS)   >= 1 MB, dove vive tutto il sistema
 *
 * I campi ems_* (memoria ESPANSA) valgono sempre 0, e non e' una lacuna:
 * l'espansa e' un meccanismo a banchi commutati per superare il limite di
 * 1 MB del modo reale su 8086/286. Su un 386+ in modo protetto con
 * paginazione quel limite non esiste e tutta la RAM oltre 1 MB e'
 * direttamente indirizzabile come estesa. Vedi il commento esteso in
 * kernel/include/syscall.h.
 * ============================================================================= */
typedef struct {
    unsigned int conv_total_kb, conv_free_kb;
    unsigned int uma_total_kb,  uma_free_kb;
    unsigned int ext_total_kb,  ext_free_kb;
    unsigned int ems_total_kb,  ems_free_kb;
    unsigned int total_kb,      free_kb;
    unsigned int page_size;
} MemInfo;

/* Riempie *mi. Ritorna 0, o un valore negativo in caso di errore. */
int meminfo(MemInfo *mi);

/* =============================================================================
 * Descrizione di un processo e dei suoi stack. DUPLICATA A MANO in
 * kernel/include/syscall.h e lib/libc.c: deve restare identica, e
 * procinfo() fallisce con -EINVAL se le copie divergono di dimensione.
 *
 *   ustack_top    indirizzo piu' alto dello stack utente (fisso)
 *   ustack_base   pagina piu' bassa ATTUALMENTE mappata; scende quando lo
 *                 stack cresce su fault. top - base = RAM impegnata ORA
 *   ustack_limit  confine della riserva. top - limit = spazio riservato.
 *                 0 = task kernel, che non ha stack utente
 *   kstack_*      stack kernel: allocato per intero alla creazione e mai
 *                 cresciuto, li' la dimensione e' ancora fissa
 * ============================================================================= */
#define PROCINFO_NAME_MAX   32
#define PROCINFO_MAX_BATCH  16   /* voci per chiamata */

typedef struct {
    unsigned int pid;
    unsigned int ppid;
    unsigned int state;     /* 1=READY 2=RUNNING 3=BLOCKED 4=ZOMBIE 5=SLEEPING */
    unsigned int prio;
    char         name[PROCINFO_NAME_MAX];
    unsigned int ustack_top;
    unsigned int ustack_base;
    unsigned int ustack_limit;
    unsigned int kstack_base;
    unsigned int kstack_top;
} ProcInfo;

/* Riempie fino a max voci partendo dalla start-esima (paginazione come
 * listdir_from). Ritorna quante ne ha scritte, 0 quando sono finite. */
int procinfo(ProcInfo *buf, unsigned int max, unsigned int start);

/* =============================================================================
 * Disco fisico e sua tabella delle partizioni. DUPLICATA A MANO in
 * kernel/include/syscall.h e lib/libc.c: deve restare identica.
 *
 * I 64 bit viaggiano spezzati in _lo/_hi per non dover concordare un
 * allineamento a 8 byte fra kernel e libc attraverso l'ABI.
 *
 * settori_*  quanto il disco dichiara ORA (IDENTIFY DEVICE)
 * nativi_*   capacita' di fabbrica (READ NATIVE MAX ADDRESS)
 * clippato   1 se nativi > settori: c'e' spazio NASCOSTO. E' il caso di un
 *            disco da 64 GB che si presenta come da 32, per una HPA
 *            attiva o per un jumper di limitazione.
 * ============================================================================= */
#define DISKINFO_MAX_PART   16

/* Schema */
#define PT_SCHEMA_NESSUNO   0
#define PT_SCHEMA_MBR       1
#define PT_SCHEMA_GPT       2

/* Anomalie (maschera) — vedi kernel/include/mbr.h per il dettaglio */
#define PT_PROB_FIRMA       0x0001
#define PT_PROB_OLTRE_FINE  0x0002
#define PT_PROB_SOVRAPP     0x0004
#define PT_PROB_CATENA      0x0008
#define PT_PROB_BOOTFLAG    0x0010
#define PT_PROB_GPT         0x0020
#define PT_PROB_VUOTA       0x0040
#define PT_PROB_TROPPE_EXT  0x0080
#define PT_PROB_TRONCATA    0x0100
#define PT_PROB_SETTORE0    0x0200

typedef struct {
    unsigned int attiva, tipo, logica;
    unsigned int numero;   /* numero alla fdisk: 1-4 primarie, 5+ logiche */
    unsigned int inizio_lo, inizio_hi;
    unsigned int settori_lo, settori_hi;
    unsigned int fs_tipo;          /* 0 sconosciuto, 12/16/32, 255 illeggibile */
    unsigned int fs_incoerente;
    unsigned int fs_sett_per_clu;
    unsigned int fs_n_cluster;
    char         fs_etichetta[12];
} PartInfo;

typedef struct {
    unsigned int presente, tipo, canale, unita;
    unsigned int lba48, hpa, clippato;
    unsigned int settori_lo, settori_hi;
    unsigned int nativi_lo,  nativi_hi;
    char         modello[44];
    char         seriale[24];
    char         firmware[12];
    unsigned int schema, problemi, n_part;
    PartInfo     part[DISKINFO_MAX_PART];
} DiskInfo;

/* Riempie *di per l'unita' idx (0..3). Ritorna 0 anche se lo slot e'
 * vuoto: in quel caso di->presente vale 0. */
int diskinfo(unsigned int idx, DiskInfo *di);

/* Dispositivo a blocchi. `primo`/`settori` sono la FINESTRA: ogni accesso
 * viene tradotto e rifiutato se ne esce. DUPLICATA A MANO in
 * kernel/include/syscall.h e lib/libc.c. */
#define BLKINFO_NOME_MAX    12
typedef struct {
    char         nome[BLKINFO_NOME_MAX];
    unsigned int tipo;          /* 1 floppy, 2 disco intero, 3 partizione */
    unsigned int sola_lettura;
    unsigned int primo_lo, primo_hi;
    unsigned int settori_lo, settori_hi;
} BlkInfo;

int blkinfo(BlkInfo *buf, unsigned int max, unsigned int start);

/* =============================================================================
 * Montaggi
 *
 * `punto` e' un percorso assoluto ("/disk") e NON deve esistere gia': i
 * punti di montaggio sono virtuali e compaiono nell'elenco della root
 * perche' li aggiunge il VFS. Montare su un nome esistente viene
 * rifiutato invece di nasconderlo in silenzio.
 *
 * I montaggi sono in lettura/scrittura salvo MNT_SOLA_LETTURA; su un
 * montaggio in sola lettura ogni tentativo di modifica torna -EROFS.
 * ============================================================================= */
#define MOUNTINFO_PUNTO_MAX 24

typedef struct {
    char         punto[MOUNTINFO_PUNTO_MAX];
    char         dev[BLKINFO_NOME_MAX];
    unsigned int fs;             /* 12, 16, 32 */
    unsigned int sola_lettura;
} MountInfo;

/* flag: MNT_SOLA_LETTURA per montare senza permettere modifiche. */
#define MNT_SOLA_LETTURA 0x0001

int mount(const char *dev, const char *punto, unsigned int flag);
int umount(const char *punto);
int mountinfo(MountInfo *buf, unsigned int max, unsigned int start);

/* =============================================================================
 * Installazione dell'avvio su disco
 *
 * `punto` e' un punto di montaggio GIA' ATTIVO in scrittura ("/disk") che
 * contiene /BOOT/STAGE2.BIN e /BOOT/KERNEL.BIN.
 *
 * Il kernel scrive l'MBR (solo il codice: la tabella delle partizioni non
 * viene toccata) e il settore di avvio della partizione (conservandone il
 * BPB), con dentro la mappa dei settori dei due file.
 *
 * ⚠️ La mappa vale finche' quei file non si spostano: ricopiare il kernel
 * OBBLIGA a rieseguire l'installazione. -29 (ESPIPE) significa che un file
 * e' frammentato e non riassumibile in un intervallo unico.
 * ============================================================================= */
typedef struct {
    unsigned int s2_lba, s2_cnt;
    unsigned int k_lba,  k_cnt;
    unsigned int disco;
    unsigned int voce;
} BootInstallInfo;

int bootinstall(const char *punto, BootInstallInfo *info);

/* =============================================================================
 * Scrittura della tabella delle partizioni
 *
 * Si passano SEMPRE tutti e quattro gli slot delle primarie, anche quelli
 * liberi (tipo = 0): una tabella e' un oggetto unico, e consegnarla una
 * voce alla volta darebbe stati intermedi in cui le partizioni si
 * sovrappongono, che il kernel non potrebbe validare.
 *
 * Il kernel RIFIUTA in blocco — senza scrivere niente — una proposta che
 * abbia una qualunque delle anomalie che `disk` sa segnalare, piu' due
 * casi che riguardano solo la scrittura: un disco GPT, e un'estesa che
 * contiene partizioni logiche e che la proposta sposterebbe o
 * cancellerebbe (le logiche da qui non si toccano).
 *
 * Rifiuta anche se una partizione di quel disco e' MONTATA — compresa la
 * root, se il sistema sta girando da li'.
 *
 * In caso di -22 (EINVAL) il campo `problemi` contiene la maschera
 * PT_PROB_* di cosa non andava: usala per dire all'utente quale controllo
 * non e' passato invece di un generico "rifiutata".
 *
 * Dopo il successo i dispositivi hd<n>p* sono gia' aggiornati: non serve
 * riavviare.
 *
 * ⚠️ Non formatta niente. Le partizioni nuove contengono quello che c'era
 * prima in quei settori.
 * ============================================================================= */
#define PARTWRITE_MAX_VOCI  4

typedef struct {
    unsigned int attiva;                  /* 0x00, oppure 0x80 = avviabile */
    unsigned int tipo;                    /* byte di tipo MBR; 0 = slot libero */
    unsigned int inizio_lo,  inizio_hi;   /* LBA assoluto del primo settore */
    unsigned int settori_lo, settori_hi;  /* lunghezza in settori */
} PartVoce;

typedef struct {
    unsigned int problemi;                  /* USCITA: maschera PT_PROB_* */
    PartVoce     voce[PARTWRITE_MAX_VOCI];  /* voce[i] = slot i+1 */
} PartTabella;

int partwrite(unsigned int disco, PartTabella *tab);




/* Flag open */
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     0x40
#define O_TRUNC     0x200
#define O_APPEND    0x400

/* Voce di directory (vedi SYS_READDIR in kernel/include/syscall.h —
 * struct identica, deve restare sincronizzata a mano: stessa convenzione
 * usata altrove nel progetto, es. i numeri di syscall duplicati in
 * bin/sh/shell.c invece di includere gli header del kernel). */
#define DIRENT_NAME_MAX 13
typedef struct {
    char           name[DIRENT_NAME_MAX];
    unsigned int   size;
    unsigned char  is_dir;
} DirEntry;

/* listdir — elenca una directory in un colpo solo (non e' la classica
 * tripletta POSIX opendir/readdir/closedir, qui non serve un handle).
 * path: NULL/""/"/" per la root, "/NOME" per una subdirectory.
 * Ritorna il numero di entry scritte in buf (>=0), o <0 in caso di errore. */
int     listdir(const char *path, DirEntry *buf, int max);

/* Come listdir, ma parte dalla 'start'-esima voce. Serve per percorrere
 * una directory piu' grande del buffer: il kernel limita comunque quante
 * voci restituisce per chiamata (vedi READDIR_MAX_BATCH), e senza questa
 * il resto della directory era invisibile SENZA alcuna segnalazione.
 *
 * Uso tipico:
 *   int start = 0, n;
 *   while ((n = listdir_from(dir, buf, MAX, start)) > 0) {
 *       ... elabora le n voci ...
 *       start += n;
 *       if (n < MAX) break;      // ultima pagina
 *   }
 */
int     listdir_from(const char *path, DirEntry *buf, int max, int start);

/* Programmi che usano la libc implementano main() invece di _start().
 * La libc fornisce _start (naked, weak): legge argc/argv dallo stack
 * iniziale costruito da sys_spawn e chiama _libc_start → main().
 * I programmi che definiscono _start() propria (es. hello, shell)
 * la sovrascrivono al link senza includere _libc_start. */
int     main(int argc, char **argv);
void    _libc_start(int argc, char **argv);  /* richiamata da _start naked */

/* =============================================================================
 * IPC — comunicazione kernel-mediata tra task ring3.
 *
 * Un driver o servizio registra un nome all'avvio (ipc_register), poi
 * resta in attesa di messaggi con ipc_recv. Un client cerca il PID del
 * servizio con ipc_lookup e gli invia richieste con ipc_send — senza mai
 * conoscere direttamente la memoria del destinatario: il kernel copia i
 * dati attraverso un buffer intermedio (vedi kernel/ipc/ipc.c).
 * ============================================================================= */
#define IPC_MSG_MAX_DATA    512
#define IPC_NAME_LEN        16

typedef struct {
    unsigned int  sender_pid;
    unsigned int  type;
    unsigned int  len;
    unsigned char data[IPC_MSG_MAX_DATA];
} IpcMessage;

/* Invia un messaggio a dest_pid. data può essere NULL se len=0.
 * Ritorna 0 su successo, <0 su errore (-ESRCH se dest_pid non esiste,
 * -EBUSY se la mailbox del destinatario resta piena troppo a lungo). */
int     ipc_send(unsigned int dest_pid, unsigned int type,
                  const void *data, unsigned int len);

/* Riceve il prossimo messaggio nella propria mailbox, bloccando se
 * vuota. out_meta (opzionale) riceve sender_pid/type/len; buf riceve il
 * payload fino a buf_len byte. Ritorna 0 su successo. */
int     ipc_recv(IpcMessage *out_meta, void *buf, unsigned int buf_len);

/* Registra il chiamante come fornitore del servizio 'name' (es. "tty",
 * "floppy"). Ritorna 0 su successo, <0 se il nome è già in uso. */
int     ipc_register(const char *name);

/* Cerca il PID del processo che fornisce 'name'.
 * Ritorna il PID (>0) su successo, <0 se non trovato. */
int     ipc_lookup(const char *name);

/* =============================================================================
 * Hardware kernel-mediato — accesso a IRQ e porte I/O per driver ring3.
 *
 * Un driver ring3 non può eseguire in/out o gestire direttamente un IDT
 * gate (istruzioni privilegiate, CPL=3 le rifiuta con #GP). Il kernel
 * media entrambi gli accessi: irq_bind() fa arrivare gli interrupt
 * hardware come messaggi IPC (vedi IpcMessage — sender_pid=0 e
 * type=0xFFFFFFFF segnalano una notifica IRQ, il payload è l'uint32_t
 * col numero IRQ); ioport_bind() dichiara quali porte il processo può
 * toccare, verificate ad ogni ioport_in/ioport_out dal kernel.
 * ============================================================================= */
#define IPC_SENDER_KERNEL    0
#define IPC_TYPE_IRQ_NOTIFY  0xFFFFFFFFu

/* Rivendica un IRQ hardware (0-15). Ritorna 0 su successo, -EEXIST se
 * già rivendicato da un altro processo vivo. */
int     irq_bind(unsigned int irq);

/* Richiede accesso a un range di porte I/O [base, base+count).
 * Sovrascrive un'eventuale bind precedente. Ritorna 0 su successo. */
int     ioport_bind(unsigned int base, unsigned int count);

/* Legge/scrive un byte su una porta nel proprio range. Ritorna -EPERM
 * se la porta non è stata dichiarata con ioport_bind(). */
int     ioport_in(unsigned int port);
int     ioport_out(unsigned int port, unsigned int value);

/* =============================================================================
 * Configurazione e identità del sistema
 * ============================================================================= */

/* Legge un'opzione di /boot/kernel.cfg: sia le variabili di [env]
 * (PATH, HOME, TERM, OSNAME, ...) sia le opzioni scalari fuori da [env]
 * come 'verboseboot'. Copia il valore in buf.
 * Ritorna la lunghezza del valore (>=0), <0 se assente o buffer piccolo.
 *
 * Non si chiama getenv() di proposito: la firma è diversa da quella
 * standard (nessun puntatore a memoria statica del sistema, il chiamante
 * fornisce il buffer) e un nome identico con semantica diversa sarebbe
 * una trappola. */
int     getconf(const char *key, char *buf, size_t size);

/* Copia l'identità del sistema — nome, copyright, licenza, versione — dalla
 * variabile globale del kernel (g_os_version). Ritorna la lunghezza, <0
 * se il buffer è troppo piccolo (nessuna copia parziale). */
int     osversion(char *buf, size_t size);

/* Comodità: 1 se il boot verboso è attivo, 0 se il sistema deve restare
 * silenzioso. In caso di errore di lettura ritorna 1 — un programma muto
 * per un problema di configurazione è indistinguibile da uno bloccato. */
int     verboseboot(void);

#endif /* LIBC_H */

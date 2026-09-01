/* =============================================================================
 * kernel/syscall/syscall_impl.c
 * EX-OS -- Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#include "kernel.h"
#include "idt.h"
#include "syscall.h"
#include "entropia.h"
#include "sha256.h"
#include "sched.h"
#include "pmm.h"
#include "paging.h"
#include "swap.h"
#include "kmalloc.h"
#include "ipc.h"
#include "shm.h"
#include "pipe.h"
#include "pty.h"
#include "isr.h"

/* Forward: funzioni VGA per sys_write su fd=1/2 */
extern void vga_puts(const char *s);
extern void vga_putchar(char c);

/* Forward: ELF loader (Fase 3b/4a) */
#include "elf.h"
#include "cfg.h"       /* cfg_getenv: sezione [env] di /boot/kernel.cfg */
#include "version.h"   /* g_os_version: identità del sistema (SYS_VERSION) */
#include "power.h"     /* power_off/reboot/halt (SYS_REBOOT) */
#include "fat12.h"
#include "ata.h"
#include "mbr.h"
#include "vol.h"
#include "blk.h"
#include "fat.h"
#include "vfs.h"
#include "bootinst.h"
#include "rtc.h"       /* RtcTime, rtc_read: data e ora (SYS_TIME) */
#include "vga.h"       /* VGA_N_CONSOLE e le console virtuali */
#include "tty.h"       /* TTY_IOCTL_*: comandi nativi del terminale (sys_ioctl) */

/* =============================================================================
 * ! LE SYSCALL CHE SCAVALCANO IL FILESYSTEM DEVONO CHIEDERE CHI SEI
 *
 * Trovato il 17 agosto 2026, subito dopo aver acceso i permessi sui file: in
 * tutto il kernel c'era UNA SOLA syscall che guardava l'uid, ed era setuid.
 * Tutte le altre no — comprese quelle che parlano al disco SENZA passare dalla
 * VFS:
 *
 *     blkread / blkwrite   settori grezzi di una partizione
 *     partwrite            la tabella delle partizioni
 *     bootinstall          l'MBR e il settore di avvio
 *     mount / umount       cosa si vede e da dove
 *     reboot               spegnere la macchina
 *
 * ! CON blkwrite APERTA A TUTTI, I PERMESSI SUI FILE SONO UNA DECORAZIONE. Un
 * utente normale non puo' scrivere /boot/ombra passando dalla VFS — ma puo'
 * riscrivere il settore che lo contiene, o /bin/sh, o l'intero filesystem. Il
 * controllo piu' accurato del mondo non serve a niente se accanto c'e' una
 * porta che non chiede niente.
 *
 * ! ED E' LA DOMANDA GIUSTA DA FARSI DOPO OGNI CONTROLLO NUOVO: non «questo
 * controllo e' giusto», ma «esiste un'altra strada per ottenere la stessa
 * cosa?». Qui ce n'erano sei.
 * ============================================================================= */
static int solo_root(const char *chi)
{
    Process *self = proc_get_current();

    /* Senza processo corrente e' il kernel a chiedere, durante l'avvio: passa.
     * Vedi vfs_uid_corrente(), stessa regola e stessa ragione. */
    if (self == NULL || self->uid == 0) return 1;

    klog(LOG_WARN, "SYSCALL %s: rifiutata al PID %u (uid %u): serve root",
         chi, self->pid, self->uid);
    return 0;
}

extern Process g_process_pool[MAX_PROCESSES];

/* Directory di lavoro corrente (globale, semplificazione per ora) */
/* =============================================================================
 * Quanto puo' essere lungo un percorso assoluto, in un posto solo.
 *
 * Era 256 sparso in una dozzina di dichiarazioni, ed era abbastanza finche'
 * i nomi erano 8.3. Con ext2 un NOME solo puo' essere 255 byte: "/" piu' un
 * nome massimo fa gia' 257, e con 256 il percorso di un file legittimo
 * verrebbe troncato — cioe' aprirebbe un altro file, o nessuno.
 *
 * Deve restare >= VFS_PATH_MAX, altrimenti il VFS riceve percorsi gia'
 * tagliati e nessun controllo piu' a valle puo' accorgersene.
 * ============================================================================= */
#define PERCORSO_MAX  VFS_PATH_MAX

/* =============================================================================
 * La directory corrente sta nel PCB, NON qui.
 *
 * ! QUI C'ERA `static char g_cwd[PERCORSO_MAX] = "/";` — UNA SOLA PER TUTTO
 * IL SISTEMA. Reggeva finche' a usarla era una shell sola, e sotto quella
 * soglia il difetto non si vede: e' cio' che lo rendeva pericoloso.
 *
 * Bastano due processi che lavorano in posti diversi, e sono casi normali:
 * `make -C sotto` spostava anche la shell che l'aveva lanciato; un subshell
 * che fa `cd` muoveva tutti; e con `make -j2` il secondo compilatore
 * risolveva i propri percorsi relativi contro la directory del primo —
 * senza errore, aprendo un file NEL POSTO SBAGLIATO.
 *
 * Adesso e' `Process.cwd`, si eredita dal padre come `console`, e ogni
 * processo ha la sua. Vedi il commento esteso in kernel/include/sched.h.
 * ============================================================================= */

/* La directory corrente del processo in esecuzione.
 *
 * ! RENDE "/" FUORI DA UN CONTESTO DI PROCESSO, e non e' un ripiego: al
 * PASSO 13 il kernel monta i volumi e carica i driver girando nel task
 * IDLE, prima che esista un processo con una directory sua. La radice e'
 * l'unica risposta che non inventa niente. */
static const char *cwd_corrente(void)
{
    Process *p = proc_get_current();

    if (p == NULL || p->cwd[0] == '\0') return "/";
    return p->cwd;
}

/* =============================================================================
 * Helper: trova un fd libero nel processo corrente
 * ============================================================================= */
static int find_free_fd(Process *proc)
{
    int i;
    for (i = 0; i < MAX_FD; i++) {
        if (proc->fds[i].type == FD_UNUSED) return i;
    }
    return -1;
}

/* =============================================================================
 * Helper: copia stringa sicura (no libc)
 * ============================================================================= */
static uint32_t kstrlen(const char *s)
{
    uint32_t n = 0;
    while (s && *s++) n++;
    return n;
}

static void kstrcpy(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* =============================================================================
 * resolve_path — trasforma un percorso relativo in assoluto usando la
 * directory corrente DEL PROCESSO (Process.cwd)
 *
 * BUG CORRETTO (luglio 2026): NESSUNA syscall risolveva i percorsi
 * relativi. La directory corrente veniva impostata da chdir() e letta da
 * getcwd(), ma
 * open/stat/exec/spawn passavano la stringa ricevuta direttamente a
 * fat12_*, che la interpreta sempre a partire dalla root.
 *
 * Conseguenza concreta, segnalata dall'utente: dopo `cd /boot`, il
 * comando `textline kernel.cfg` cercava "/kernel.cfg" nella root, non lo
 * trovava, e l'editor apriva un documento nuovo e vuoto. Lo stesso valeva
 * per cat, per l'esecuzione di un programma nella directory corrente e
 * per qualunque altro percorso non assoluto. La shell offre `cd` e `pwd`,
 * quindi era lecito aspettarsi che i percorsi relativi funzionassero.
 *
 * Gestisce anche "." e "..", perché `cd ..` è la prima cosa che si prova.
 *
 * ! IL ".." SI RISOLVE OVUNQUE, NON SOLO IN TESTA E NON SOLO NEI PERCORSI
 * RELATIVI (corretto ad agosto 2026). Prima un percorso assoluto veniva
 * copiato tale e quale — «si usa così com'è» — e uno relativo capiva un
 * ".." solo se stava all'inizio, perché FAT12 aveva un livello solo di
 * sottodirectory e tanto bastava.
 *
 * Non basta più, e il conto l'ha pagato GCC: i suoi percorsi di ricerca
 * hanno il ".." NEL MEZZO, e per costruzione — sono scritti dentro il
 * binario come /exos/lib/gcc/i386-exos/17.0.0/../../../../i386-exos/include,
 * cioè "risali dalla mia directory fino al prefisso, poi riscendi". È così
 * che un compilatore trova le proprie cose dopo essere stato spostato.
 * Consegnati al VFS senza normalizzarli, quei percorsi cercano una
 * directory che si chiama davvero ".." dentro 17.0.0/, non la trovano, e
 * cc1 conclude che la directory non esiste: la scarta in silenzio, le
 * scarta tutte, e risponde «no include path in which to search for
 * stdio.h». Un messaggio che parla di header mentre il difetto è nel
 * percorso.
 *
 * Ritorna 0, o <0 se il risultato non entra nel buffer.
 * ============================================================================= */

/* Riscrive `p` — che deve essere assoluto — in forma canonica: via le barre
 * ripetute, via le componenti ".", e ogni ".." cancella quella prima. Un
 * ".." di troppo si ferma alla radice, come su Unix, invece di uscirne. */
static void path_normalizza(char *p)
{
    const char *in  = p + 1;    /* la barra iniziale resta dov'è */
    char       *out = p + 1;

    while (*in != '\0') {
        const char *fine;
        uint32_t    len, k;
        int         ultimo;

        while (*in == '/') in++;
        if (*in == '\0') break;

        fine = in;
        while (*fine != '\0' && *fine != '/') fine++;
        len = (uint32_t)(fine - in);

        /* ! SI GUARDA SE SIAMO ALL'ULTIMA COMPONENTE PRIMA DI SCRIVERE: la
         * barra che si aggiunge qui sotto finisce esattamente sopra il byte
         * che `fine` sta indicando, e se quello era il terminatore lo si
         * perderebbe — con il ciclo che prosegue oltre la fine della
         * stringa. Scrittura e lettura corrono sullo stesso buffer. */
        ultimo = (*fine == '\0');

        if (len == 1 && in[0] == '.') {
            /* "." non muove niente */
        } else if (len == 2 && in[0] == '.' && in[1] == '.') {
            if (out > p + 1) {
                out--;                                   /* la barra */
                while (out > p + 1 && out[-1] != '/') out--;
            }
        } else {
            for (k = 0; k < len; k++) out[k] = in[k];
            out += len;
            *out++ = '/';
        }

        if (ultimo) break;
        in = fine;
    }

    /* Via la barra finale, tranne quando il percorso È la radice. */
    if (out > p + 1) out--;
    *out = '\0';
}

static int32_t resolve_path(const char *in, char *out, uint32_t max)
{
    uint32_t i = 0, j = 0;

    if (in == NULL || out == NULL || max < 2) return ERR(EINVAL);

    if (in[0] == '/') {
        out[i++] = '/';
        in++;
    } else {
        /* Relativo: parte dalla directory corrente.
         *
         * Lo slash separatore si aggiunge solo se non c'è già: con cwd "/"
         * ce l'abbiamo, con "/boot" no. Senza questo controllo si
         * otterrebbe "//kernel.cfg" oppure "/bootkernel.cfg" — e la prima
         * delle due la ripulirebbe path_normalizza, la seconda no. */
        const char *cwd = cwd_corrente();

        while (cwd[j] && i < max - 1) out[i++] = cwd[j++];
        if (i > 0 && out[i - 1] != '/' && i < max - 1) out[i++] = '/';
    }

    while (*in && i < max - 1) out[i++] = *in++;
    out[i] = '\0';

    if (*in != '\0') return ERR(EINVAL);   /* percorso troppo lungo */

    path_normalizza(out);
    return 0;
}

/* =============================================================================
 * SYS_EXIT (1) -- Termina il processo corrente
 *
 * ebx = exit_code
 * ============================================================================= */
int32_t sys_exit(InterruptFrame *frame)
{
    int32_t code = (int32_t)frame->ebx;
    klog(LOG_INFO, "SYSCALL exit(%d) PID=%u", code,
         proc_get_current()->pid);

    /* Riversa su disco quello che il processo ha modificato.
     *
     * PERCHÉ QUI (luglio 2026): dopo il passaggio della cache a
     * write-back, le operazioni sul filesystem restano in RAM finché
     * qualcuno non le riversa. Sincronizzare dentro ogni singola
     * operazione (fat12_delete lo faceva) costa 32 settori per file e
     * rende inutilizzabile qualunque comando che ne tocchi molti:
     * /bin/delete con un modello jolly cancellava 10 file in 60 secondi.
     *
     * L'uscita del processo è il punto giusto: un comando qualsiasi
     * finisce, e a quel punto tutto il suo lavoro va su disco in una
     * volta sola. Se non ha modificato nulla, fat12_sync() non fa niente
     * — controlla i flag e i settori sporchi — quindi `ls` non paga
     * alcun costo.
     *
     * Va PRIMA di proc_exit(), che non ritorna e fa il cambio di
     * contesto: qui siamo ancora in un normale contesto di processo con
     * gli interrupt abilitati, che è ciò che il driver FDC richiede per
     * le sue attese su g_ticks e IRQ6.
     *
     * Nota: un processo terminato da un fault non passa di qui, quindi le
     * sue modifiche possono andare perse. È il comportamento atteso — un
     * programma che si schianta non ha garantito nulla. */
    vfs_sync();

    proc_exit(code);
    /* Non ritorna */
    return 0;
}

/* =============================================================================
 * SYS_READ (3) -- Legge da un file descriptor
 *
 * ebx = fd
 * ecx = buf*  (indirizzo buffer utente)
 * edx = count (byte da leggere)
 *
 * Ritorna: byte letti, o errore negativo
 * ============================================================================= */
int32_t sys_read(InterruptFrame *frame)
{
    int32_t   fd    = (int32_t)frame->ebx;
    char     *buf   = (char *)frame->ecx;
    uint32_t  count = frame->edx;
    Process  *proc  = proc_get_current();

    if (fd < 0 || fd >= MAX_FD)                         return ERR(EBADF);
    if (!syscall_verify_ptr(buf, count))                 return ERR(EFAULT);
    if (proc->fds[fd].type == FD_UNUSED)                return ERR(EBADF);

    /* stdin: tastiera, tramite il driver TTY compilato staticamente nel
     * kernel (drivers/tty/tty.c, inizializzato al PASSO 14 con
     * extern drv_init() -- non e' un modulo dinamico nel driver manager,
     * quindi le sue funzioni sono simboli globali del kernel). */
    if (proc->fds[fd].type == FD_STDIN) {
        extern int drv_read(void *buf, size_t n);
        uint32_t fg = sched_console_fg(proc->console);

        /* =================================================================
         * JOB CONTROL: la tastiera e' di chi sta in primo piano.
         *
         * Un processo lanciato con '&' che prova a leggere trova la fine
         * dell'input, e si comporta di conseguenza — di solito esce. Non
         * e' una restrizione arbitraria: il driver tastiera serve
         * l'ULTIMO che ha chiesto una riga, quindi senza questo controllo
         * un job in background sostituirebbe la shell come lettore, e la
         * shell resterebbe bloccata per sempre in attesa di una riga che
         * nessuno le consegnerebbe piu'. Il prompt sparirebbe e la
         * console con lui.
         *
         * Unix in questo caso ferma il processo con SIGTTIN e lo lascia
         * riprendibile con 'fg'. Qui non ci sono segnali: la fine
         * dell'input e' l'unica risposta possibile, ed e' comunque
         * un'informazione vera — quel programma, in background, input non
         * ne avra' mai.
         *
         * fg == 0 significa "nessuno ha dichiarato il primo piano": legge
         * chi vuole, che e' il comportamento di prima dei job. Serve
         * perche' il sistema funzioni anche prima che una shell abbia
         * detto la sua.
         * ================================================================= */
        if (fg != 0 && fg != proc->pid) return 0;

        int32_t n = drv_read(buf, count);
        return n;
    }

    /* Estremita' di lettura di una pipe. ! Puo' BLOCCARE: se la pipe e'
     * vuota e c'e' ancora uno scrittore, si aspetta. Ritorna 0 solo
     * quando non c'e' piu' nessuno che possa scrivere. Vedi
     * kernel/ipc/pipe.c. */
    if (proc->fds[fd].type == FD_PIPE_R) {
        return pipe_leggi((int)proc->fds[fd].inode, buf, count);
    }

    /* ! Leggere dall'estremita' SBAGLIATA e' un errore, non un'attesa. */
    if (proc->fds[fd].type == FD_PIPE_W) return ERR(EBADF);

    /* Le due estremita' di un pty. Sono due strade diverse e non una con un
     * flag: vedi FD_PTY_M in sched.h. */
    if (proc->fds[fd].type == FD_PTY_M)
        return pty_leggi_master((int)proc->fds[fd].inode, buf, count);
    if (proc->fds[fd].type == FD_PTY_S)
        return pty_leggi_slave((int)proc->fds[fd].inode, buf, count);

    /* file FAT12 */
    if (proc->fds[fd].type == FD_FILE) {
        /* Il driver scrivera' dentro `buf`: le sue pagine devono essere
         * gia' in RAM, o il fault avverrebbe con il lucchetto del VFS in
         * mano. Vedi vm_precarica_utente(). */
        vm_precarica_utente((uint32_t)buf, count);
        int32_t n = vfs_read((int)proc->fds[fd].inode, buf, count, proc->fds[fd].offset);
        if (n >= 0) proc->fds[fd].offset += (uint32_t)n;
        return n;
    }

    return ERR(EBADF);
}

/* =============================================================================
 * SYS_WRITE (4) -- Scrive su un file descriptor
 *
 * ebx = fd
 * ecx = buf*  (indirizzo buffer utente)
 * edx = count (byte da scrivere)
 *
 * Ritorna: byte scritti, o errore negativo
 * ============================================================================= */
int32_t sys_write(InterruptFrame *frame)
{
    int32_t        fd    = (int32_t)frame->ebx;
    const char    *buf   = (const char *)frame->ecx;
    uint32_t       count = frame->edx;
    Process       *proc  = proc_get_current();
    uint32_t       i;

    if (fd < 0 || fd >= MAX_FD)              return ERR(EBADF);
    if (!syscall_verify_ptr(buf, count))      return ERR(EFAULT);
    if (proc->fds[fd].type == FD_UNUSED)     return ERR(EBADF);

    /* stdout / stderr: scrivi sulla console DEL PROCESSO, non su quella
     * visibile. Un programma che gira su una console nascosta continua a
     * disegnare nel proprio buffer e si ritrova lo schermo intatto
     * quando l'utente ci torna sopra con Alt+Fn. */
    if (proc->fds[fd].type == FD_STDOUT ||
        proc->fds[fd].type == FD_STDERR) {
        for (i = 0; i < count; i++) {
            vga_putchar_su(proc->console, buf[i]);
        }
        return (int32_t)count;
    }

    /* Estremita' di scrittura di una pipe. ! Puo' BLOCCARE se il buffer
     * e' pieno, e ritorna -EPIPE se non c'e' piu' nessun lettore. La
     * scrittura puo' essere PARZIALE: il chiamante deve guardare il
     * valore di ritorno e richiamare. Vedi kernel/ipc/pipe.c. */
    if (proc->fds[fd].type == FD_PIPE_W) {
        return pipe_scrivi((int)proc->fds[fd].inode, buf, count);
    }

    /* ! Scrivere sull'estremita' SBAGLIATA e' un errore. */
    if (proc->fds[fd].type == FD_PIPE_R) return ERR(EBADF);

    /* ! SCRIVERE NEL MASTER VUOL DIRE BATTERE UN TASTO, non stampare: quei
     * byte passano dalla disciplina di linea. Scrivere nello slave e' l'uscita
     * di un programma, e non viene toccata da nessuno. */
    if (proc->fds[fd].type == FD_PTY_M)
        return pty_scrivi_master((int)proc->fds[fd].inode, buf, count);
    if (proc->fds[fd].type == FD_PTY_S)
        return pty_scrivi_slave((int)proc->fds[fd].inode, buf, count);

    /* file FAT12 */
    if (proc->fds[fd].type == FD_FILE) {
        /* =====================================================================
         * O_APPEND: si scrive sempre IN FONDO, e la posizione si rilegge a
         * ogni scrittura.
         *
         * ! IL FLAG ERA ACCETTATO E NON FACEVA NIENTE. vfs_open lo ammetteva
         * fra i flag validi, sys_open lo copiava nel descrittore, e poi la
         * posizione partiva da zero come per ogni altro file: `>>` della shell
         * SOVRASCRIVEVA invece di accodare. Un flag accettato e ignorato e' la
         * forma peggiore di questo difetto — chi lo passa ha ogni ragione di
         * credere che sia stato onorato, e se ne accorge dai dati persi.
         *
         * ! SI RILEGGE LA DIMENSIONE A OGNI WRITE invece di posizionarsi una
         * volta all'apertura. Costa una fstat per scrittura e in cambio fa la
         * cosa che O_APPEND promette: due processi che accodano allo stesso
         * file di registro non si sovrascrivono a vicenda. Con la posizione
         * fissata all'apertura, il secondo scriverebbe sopra il primo — e solo
         * quando i due capitano insieme, cioe' di rado e senza un motivo
         * visibile.
         * ===================================================================== */
        if (proc->fds[fd].flags & O_APPEND) {
            VfsStat st;

            if (vfs_fstat((int)proc->fds[fd].inode, &st) == 0)
                proc->fds[fd].offset = st.dimensione;
        }

        /* Stessa ragione di sys_read, dall'altro verso: il driver LEGGE
         * da `buf`, e una pagina assente farebbe faultare lui. */
        vm_precarica_utente((uint32_t)buf, count);
        int32_t n = vfs_write((int)proc->fds[fd].inode, buf, count,
                              proc->fds[fd].offset);
        if (n >= 0) proc->fds[fd].offset += (uint32_t)n;
        return n;
    }

    return ERR(EBADF);
}

/* =============================================================================
 * SYS_OPEN (5) -- Apre/crea un file
 *
 * ebx = path*  (stringa percorso, null-terminated)
 * ecx = flags  (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, ...)
 * edx = mode   (permessi, non usati per ora)
 *
 * Ritorna: fd >= 0 successo, errore negativo
 * ============================================================================= */
int32_t sys_open(InterruptFrame *frame)
{
    const char *path  = (const char *)frame->ebx;
    uint32_t    flags = frame->ecx;
    Process    *proc  = proc_get_current();
    int         free_fd;
    int         inode;

    if (!syscall_verify_str(path, PERCORSO_MAX)) return ERR(EFAULT);

    free_fd = find_free_fd(proc);
    if (free_fd < 0) return ERR(EMFILE);

    /* Risolvi eventuali percorsi relativi contro la directory corrente */
    {
        char abs[PERCORSO_MAX];
        if (resolve_path(path, abs, sizeof(abs)) != 0) return ERR(EINVAL);
        inode = vfs_open(abs, (uint32_t)flags);
    }
    if (inode < 0) return (int32_t)inode;  /* Propaga errore */

    proc->fds[free_fd].type   = FD_FILE;
    proc->fds[free_fd].flags  = flags;
    proc->fds[free_fd].offset = 0;
    proc->fds[free_fd].inode  = (uint32_t)inode;

    klog(LOG_DEBUG, "SYSCALL open('%s') -> fd=%d", path, free_fd);
    return free_fd;
}

/* =============================================================================
 * SYS_CLOSE (6) -- Chiude un file descriptor
 *
 * ebx = fd
 * ============================================================================= */
int32_t sys_close(InterruptFrame *frame)
{
    int32_t  fd   = (int32_t)frame->ebx;
    Process *proc = proc_get_current();

    if (fd < 0 || fd >= MAX_FD)          return ERR(EBADF);
    if (proc->fds[fd].type == FD_UNUSED) return ERR(EBADF);
    /* Non chiudere stdin/stdout/stderr */
    if (fd <= 2)                          return ERR(EBADF);

    if (proc->fds[fd].type == FD_FILE) {
        vfs_close((int)proc->fds[fd].inode);
    }

    /* ! CHIUDERE UN'ESTREMITA' DI PIPE NON E' SOLO CONTABILITA': e' anche
     * cio' che sveglia chi dorme dall'altra parte. Senza, `cmd1 | cmd2`
     * con cmd1 che finisce lascia cmd2 fermo per sempre ad aspettare byte
     * che nessuno scrivera'. Vedi kernel/ipc/pipe.c. */
    if (proc->fds[fd].type == FD_PIPE_R) pipe_chiudi_lettore((int)proc->fds[fd].inode);
    if (proc->fds[fd].type == FD_PIPE_W) pipe_chiudi_scrittore((int)proc->fds[fd].inode);
    if (proc->fds[fd].type == FD_PTY_M)  pty_chiudi((int)proc->fds[fd].inode, 1);
    if (proc->fds[fd].type == FD_PTY_S)  pty_chiudi((int)proc->fds[fd].inode, 0);

    proc->fds[fd].type        = FD_UNUSED;
    proc->fds[fd].flags       = 0;
    proc->fds[fd].offset      = 0;
    proc->fds[fd].inode       = 0;
    proc->fds[fd].driver_data = NULL;

    return 0;
}

/* =============================================================================
 * SYS_DUP (41) / SYS_DUP2 (63) / SYS_FCNTL (55)
 *
 * PERCHE' ESISTONO. Sono la prima cosa che ha chiesto del codice di terzi
 * vero: `ar`, `objcopy` e `arsup` di binutils fanno tutti e tre lo stesso
 * gesto — `fd = dup(fd)` prima di chiudere l'oggetto BFD che possiede
 * l'originale — per tenere il file aperto oltre la close() di qualcun
 * altro. Senza dup() quel file si chiude e il rinomina finale scrive in un
 * descrittore morto.
 *
 * ! LA POSIZIONE NON E' CONDIVISA, e su POSIX invece lo sarebbe.
 *
 * Qui l'offset sta nel descrittore del processo (FileDescriptor.offset),
 * non in un oggetto "file aperto" intermedio: due fd duplicati condividono
 * il FILE — cioe' l'handle VFS, che e' quel che serve perche' resti aperto
 * — ma ognuno si ricorda dove era arrivato. Su Linux una read() da uno dei
 * due sposta anche l'altro.
 *
 * Non e' una svista: metterlo in comune vorrebbe dire spostare l'offset
 * dentro VfsFile, e quindi cambiare la firma di vfs_read/vfs_write e la
 * gestione della posizione di fat12.c, che la tiene per conto suo
 * nell'handle. Il codice che ci gira sopra oggi non se ne accorge —
 * binutils, nell'unico punto in cui legge da un fd duplicato, fa prima un
 * lseek(SEEK_SET) esplicito (rename.c: simple_copy). Il giorno che
 * arrivera' una pipe, o una shell che scrive in append da due fd, questa
 * diventera' la cosa da sistemare per prima.
 * ============================================================================= */

/* Il corpo comune: mette in `nuovo` una copia del descrittore `vecchio`.
 * Chiude cio' che c'era in `nuovo`, come vuole dup2. */
static int32_t fd_duplica(Process *proc, int vecchio, int nuovo)
{
    if (vecchio < 0 || vecchio >= MAX_FD || nuovo < 0 || nuovo >= MAX_FD)
        return ERR(EBADF);
    if (proc->fds[vecchio].type == FD_UNUSED) return ERR(EBADF);
    if (vecchio == nuovo) return nuovo;

    /* Il riferimento in piu' si prende PRIMA di chiudere il vecchio
     * contenuto di `nuovo`: se i due fd guardassero lo stesso file, farlo
     * al contrario lo chiuderebbe e il dup successivo troverebbe un handle
     * morto. */
    if (proc->fds[vecchio].type == FD_FILE) {
        int r = vfs_dup((int)proc->fds[vecchio].inode);
        if (r < 0) return (int32_t)r;
    }
    /* Le pipe hanno il loro conteggio, uno per estremita': duplicare un
     * descrittore di pipe senza incrementarlo farebbe credere alla pipe di
     * avere meno estremita' aperte di quante ne ha, e la prima close()
     * annuncerebbe la fine dei dati a chi legge. */
    if (proc->fds[vecchio].type == FD_PIPE_R)
        pipe_apri_lettore((int)proc->fds[vecchio].inode);
    if (proc->fds[vecchio].type == FD_PIPE_W)
        pipe_apri_scrittore((int)proc->fds[vecchio].inode);

    /* Qui `nuovo` puo' essere 0, 1 o 2: e' il caso della redirezione, ed e'
     * l'unico modo di farla. sys_close li rifiuta apposta, perche' un
     * programma che chiude stdout e basta resterebbe senza uscita; chi
     * arriva da dup2 una sostituzione ce l'ha gia' pronta. */
    if (proc->fds[nuovo].type == FD_FILE)
        vfs_close((int)proc->fds[nuovo].inode);
    if (proc->fds[nuovo].type == FD_PIPE_R)
        pipe_chiudi_lettore((int)proc->fds[nuovo].inode);
    if (proc->fds[nuovo].type == FD_PIPE_W)
        pipe_chiudi_scrittore((int)proc->fds[nuovo].inode);

    proc->fds[nuovo] = proc->fds[vecchio];
    return nuovo;
}

/* =============================================================================
 * SYS_PIPE (42) — due descrittori collegati
 *
 * ebx = int fd[2]*, dove finiscono lettura (fd[0]) e scrittura (fd[1]).
 *
 * ! I DUE DESCRITTORI SI ASSEGNANO INSIEME O NON SI ASSEGNANO. Se il
 * secondo non si trova, il primo va restituito: lasciarne uno appeso
 * consumerebbe un posto in tabella e terrebbe viva una pipe che nessuno
 * puo' piu' usare.
 *
 * ! E SI SCRIVONO NELLA MEMORIA UTENTE PER ULTIMI, dopo che tutto il
 * resto e' riuscito: un fallimento a meta' lascerebbe il chiamante con un
 * numero valido e uno no, senza modo di sapere quale.
 * ============================================================================= */
int32_t sys_pipe(InterruptFrame *frame)
{
    int32_t *ufd  = (int32_t *)frame->ebx;
    Process *proc = proc_get_current();
    int      h, r = -1, w = -1;

    if (!syscall_verify_ptr(ufd, 2 * sizeof(int32_t))) return ERR(EFAULT);

    h = pipe_crea();
    if (h < 0) return (int32_t)h;

    r = find_free_fd(proc);
    if (r < 0) {
        pipe_chiudi_lettore(h);
        pipe_chiudi_scrittore(h);
        return ERR(EMFILE);
    }
    /* Si occupa subito il posto, o find_free_fd lo ridarebbe una seconda
     * volta per l'estremita' di scrittura. */
    proc->fds[r].type        = FD_PIPE_R;
    proc->fds[r].flags       = O_RDONLY;
    proc->fds[r].offset      = 0;
    proc->fds[r].inode       = (uint32_t)h;
    proc->fds[r].driver_data = NULL;

    w = find_free_fd(proc);
    if (w < 0) {
        proc->fds[r].type = FD_UNUSED;
        pipe_chiudi_lettore(h);
        pipe_chiudi_scrittore(h);
        return ERR(EMFILE);
    }
    proc->fds[w].type        = FD_PIPE_W;
    proc->fds[w].flags       = O_WRONLY;
    proc->fds[w].offset      = 0;
    proc->fds[w].inode       = (uint32_t)h;
    proc->fds[w].driver_data = NULL;

    ufd[0] = r;
    ufd[1] = w;

    klog(LOG_DEBUG, "SYSCALL pipe: PID %u -> fd %d (lettura) e %d (scrittura), "
         "pipe %d", proc->pid, r, w, h);
    return 0;
}

int32_t sys_dup(InterruptFrame *frame)
{
    Process *proc = proc_get_current();
    int      fd   = (int)frame->ebx;
    int      libero;

    if (fd < 0 || fd >= MAX_FD || proc->fds[fd].type == FD_UNUSED)
        return ERR(EBADF);

    libero = find_free_fd(proc);
    if (libero < 0) return ERR(EMFILE);

    return fd_duplica(proc, fd, libero);
}

int32_t sys_dup2(InterruptFrame *frame)
{
    Process *proc = proc_get_current();

    return fd_duplica(proc, (int)frame->ebx, (int)frame->ecx);
}

int32_t sys_fcntl(InterruptFrame *frame)
{
    Process *proc = proc_get_current();
    int      fd   = (int)frame->ebx;
    uint32_t cmd  = frame->ecx;
    uint32_t arg  = frame->edx;

    if (fd < 0 || fd >= MAX_FD || proc->fds[fd].type == FD_UNUSED)
        return ERR(EBADF);

    switch (cmd) {
    case F_DUPFD: {
        /* "il piu' basso libero da `arg` in su", che non e' find_free_fd:
         * chi lo chiede vuole stare SOPRA i descrittori standard. */
        int i;
        if (arg >= (uint32_t)MAX_FD) return ERR(EINVAL);
        for (i = (int)arg; i < MAX_FD; i++) {
            if (proc->fds[i].type == FD_UNUSED)
                return fd_duplica(proc, fd, i);
        }
        return ERR(EMFILE);
    }

    /* Vedi il commento su FD_CLOEXEC in syscall.h: qui non c'e' niente da
     * ricordare, perche' non c'e' un exec che erediti descrittori. */
    case F_GETFD:  return 0;
    case F_SETFD:  return 0;

    case F_GETFL:  return (int32_t)proc->fds[fd].flags;

    case F_SETFL:
        /* La modalita' di accesso di un file aperto non si cambia: e' cio'
         * che dice POSIX, e qui sarebbe anche una bugia — il driver ha
         * gia' aperto il file come gli e' stato chiesto. */
        proc->fds[fd].flags = (proc->fds[fd].flags & ~(O_APPEND | O_NONBLOCK))
                            | (arg & (O_APPEND | O_NONBLOCK));
        return 0;

    default:
        return ERR(EINVAL);
    }
}

/* proc_reap_zombie e' definita in kernel/sched/sched.c (accede al pool PCB
 * e ai moduli paging/kmalloc); sched.h la dichiara, qui usiamo solo il
 * prototipo gia' incluso via sched.h. */

/* =============================================================================
 * SYS_WAITPID (7) -- Attende terminazione di un processo figlio
 *
 * ebx = pid  (-1 = qualsiasi figlio)
 * ecx = status* (puntatore dove scrivere exit code, può essere NULL)
 * edx = options (non usate per ora)
 *
 * Ritorna: pid del figlio terminato, o errore
 * ============================================================================= */
int32_t sys_waitpid(InterruptFrame *frame)
{
    int32_t   pid_wait = (int32_t)frame->ebx;
    int32_t  *status   = (int32_t *)frame->ecx;
    uint32_t  options  = frame->edx;
    Process  *current  = proc_get_current();
    uint32_t  i;

    if (status && !syscall_verify_ptr(status, sizeof(int32_t)))
        return ERR(EFAULT);

    /* Cerca un figlio ZOMBIE */
    for (;;) {
        uint32_t figli_vivi = 0;

        for (i = 0; i < MAX_PROCESSES; i++) {
            Process *p = &g_process_pool[i];

            if (p->state == PROC_UNUSED)  continue;
            if (p->ppid  != current->pid) continue;
            if (pid_wait != -1 && (int32_t)p->pid != pid_wait) continue;

            if (p->state != PROC_ZOMBIE) { figli_vivi++; continue; }

            int32_t  child_exit = p->exit_code;
            uint32_t child_pid  = p->pid;
            if (status) *status = child_exit;

            proc_reap_zombie(p);

            klog(LOG_DEBUG, "SYSCALL waitpid: raccolto PID %u, code=%d",
                 child_pid, child_exit);
            return (int32_t)child_pid;
        }

        /* =================================================================
         * Nessuno zombie da raccogliere. Tre casi diversi, e prima ce
         * n'era uno solo — si bloccava e basta.
         *
         * ECHILD: non esiste nemmeno un figlio VIVO che corrisponda alla
         * richiesta. Bloccarsi qui significherebbe aspettare per sempre
         * un evento che non puo' accadere, e ci si arriva facilmente:
         * basta un 'fg' su un job gia' terminato e raccolto. Prima la
         * shell ci moriva dentro.
         *
         * WNOHANG: il chiamante ha detto che non vuole aspettare. Serve a
         * 'jobs', che deve poter dire quali sono finiti senza fermarsi
         * sul primo ancora in esecuzione.
         * ================================================================= */
        if (figli_vivi == 0)        return ERR(ECHILD);
        if (options & WNOHANG)      return 0;

        sched_block(PROC_BLOCKED);
        /* Svegliato da proc_exit di un figlio → riprova */
    }
}

/* =============================================================================
 * SYS_CONSOLE_SETFG (232) -- Dichiara il processo in primo piano
 *
 * ebx = pid (0 = nessuno)
 *
 * Lo chiama la shell: se stessa quando torna al prompt, il figlio quando
 * ne aspetta uno in primo piano. Vale per la console DEL CHIAMANTE — non
 * si puo' toccare il primo piano di una console altrui, che sarebbe un
 * modo per rubare la tastiera a un'altra shell.
 * ============================================================================= */
int32_t sys_console_setfg(InterruptFrame *frame)
{
    uint32_t pid  = frame->ebx;
    Process *proc = proc_get_current();

    sched_set_console_fg(proc->console, pid);
    return 0;
}

/* =============================================================================
 * SYS_CONSOLE_GRAFICA (233) -- Chi rivendica la console della grafica
 *
 * ebx = 0 chiedi, 1 prendi (quella del chiamante), 2 lascia
 *
 * ! SERVE PERCHE' UNA CONSOLE SENZA NESSUNO SOPRA NON E' UN POSTO DOVE
 * ANDARE. La grafica sta sull'ultima console, e finche' il server non c'e'
 * quella console e' uno schermo nero con una shell che nessuno guarda:
 * portarcisi con Alt+Fn vuol dire sparire dentro il nulla e non capire come
 * tornare indietro. Con questa voce il driver di tastiera sa se quel tasto ha
 * un senso adesso, e se non ce l'ha non fa niente — che e' la risposta giusta
 * a un tasto che non porta da nessuna parte.
 *
 * ! E LO STATO STA NEL KERNEL, non nel server: quando il server MUORE — anche
 * male, anche ucciso — il kernel deve poter dire «li' non c'e' piu' niente».
 * Un flag tenuto dal server morirebbe con lui, lasciando la porta aperta su
 * una stanza vuota. Chi la teneva si ricontrolla a ogni domanda: se quel
 * processo non c'e' piu', la console torna libera da sola.
 * ============================================================================= */
static int32_t g_console_grafica = -1;   /* quale console, -1 = nessuna */
static uint32_t g_console_grafica_pid = 0;

int32_t sys_console_grafica(InterruptFrame *frame)
{
    uint32_t azione = frame->ebx;
    Process *proc   = proc_get_current();

    /* Chi la teneva e' ancora vivo? Se no, la console e' libera. */
    if (g_console_grafica >= 0 && g_console_grafica_pid != 0 &&
        proc_get_by_pid(g_console_grafica_pid) == NULL) {
        g_console_grafica     = -1;
        g_console_grafica_pid = 0;
    }

    if (azione == 1) {
        g_console_grafica     = (int32_t)proc->console;
        g_console_grafica_pid = proc->pid;
        return g_console_grafica;
    }
    if (azione == 2) {
        /* La lascia solo chi l'aveva presa: un altro processo non deve poter
         * dichiarare spenta una grafica che sta girando. */
        if (g_console_grafica_pid == proc->pid) {
            g_console_grafica     = -1;
            g_console_grafica_pid = 0;
        }
        return 0;
    }
    return g_console_grafica;
}

/* Riferimento al pool PCB globale (definito in sched.c) */

/* =============================================================================
 * SYS_GETPID (20) -- Ritorna il PID del processo corrente
 * ============================================================================= */
int32_t sys_getpid(InterruptFrame *frame)
{
    (void)frame;
    return (int32_t)proc_get_current()->pid;
}

/* =============================================================================
 * SYS_GETPPID (64) -- Ritorna il PPID del processo corrente
 * ============================================================================= */
int32_t sys_getppid(InterruptFrame *frame)
{
    (void)frame;
    return (int32_t)proc_get_current()->ppid;
}

/* =============================================================================
 * SYS_MMAP (90) -- Mappa memoria virtuale nel processo
 *
 * ebx = puntatore a struct MmapParams
 *
 * Gestisce:
 *   MAP_ANONYMOUS: alloca pagine fisiche nuove
 *   MAP_FIXED:     mappa a indirizzo esatto richiesto
 *   (MAP_FILE non implementato -- richiede Fase 3)
 *
 * Ritorna: indirizzo virtuale mappato, o errore negativo
 * ============================================================================= */
int32_t sys_mmap(InterruptFrame *frame)
{
    MmapParams *p    = (MmapParams *)frame->ebx;
    Process    *proc = proc_get_current();
    uint32_t    pages;
    uint32_t    vaddr;
    uint32_t    i;
    uint32_t    pg_flags;

    if (!syscall_verify_ptr(p, sizeof(MmapParams))) return ERR(EFAULT);

    if (p->length == 0) return ERR(EINVAL);

    pages = ALIGN_UP(p->length, PAGE_SIZE) / PAGE_SIZE;

    /* Determina indirizzo virtuale */
    if (p->flags & MAP_FIXED) {
        uint32_t fine;

        vaddr = p->addr & 0xFFFFF000;
        if (vaddr < USER_SPACE_BASE || vaddr >= USER_SPACE_END)
            return ERR(EINVAL);

        if (pages > (USER_SPACE_END - vaddr) / PAGE_SIZE) return ERR(EINVAL);
        fine = vaddr + pages * PAGE_SIZE;

        /* ! MAP_FIXED NON PUO' PRENDERSI LA ZONA DEL KERNEL (0.156).
         *
         * Su POSIX MAP_FIXED sostituisce cio' che trova, e va bene finche'
         * si tratta di roba del processo. Il blocco TLS e la riserva dello
         * stack non lo sono: il kernel ci tiene degli invarianti sopra —
         * tls_tp punta dentro il primo, e page_fault_handler conta di
         * poter mappare lui le pagine della seconda. Rimpiazzarli non da'
         * un errore, da' un processo che legge variabili __thread altrui o
         * uno stack che smette di crescere. Chi lo chiede si sbaglia, e
         * qui glielo si dice invece di ubbidire. */
        if (proc->heap_max != 0 && fine > proc->heap_max) {
            klog(LOG_WARN, "SYSCALL mmap: PID %u chiede MAP_FIXED a "
                 "0x%08x-0x%08x, oltre il tetto 0x%08x",
                 proc->pid, vaddr, fine, proc->heap_max);
            return ERR(EINVAL);
        }
    } else {
        /* Alloca nel heap utente (cresce verso l'alto) */
        vaddr = ALIGN_UP(proc->heap_end, PAGE_SIZE);
        if (vaddr < USER_SPACE_BASE) vaddr = USER_SPACE_BASE;

        /* Lo stesso tetto di sbrk, e per la stessa ragione: questa via
         * fa crescere heap_end, quindi senza controllo mmap scavalcherebbe
         * il confine che sbrk rispetta. Vedi kernel/include/sched.h. */
        if (proc->heap_max == 0) {
            klog(LOG_ERROR, "SYSCALL mmap: PID %u non ha uno heap", proc->pid);
            return ERR(ENOMEM);
        }
        if (vaddr > proc->heap_max ||
            pages > (proc->heap_max - vaddr) / PAGE_SIZE) {
            klog(LOG_WARN, "SYSCALL mmap: PID %u chiede %u pagine a 0x%08x, "
                 "il tetto e' 0x%08x", proc->pid, pages, vaddr, proc->heap_max);
            return ERR(ENOMEM);
        }
    }

    /* Flag pagine */
    pg_flags = PG_PRESENT | PG_USER;
    if (p->prot & PROT_WRITE) pg_flags |= PG_WRITABLE;

    /* Alloca e mappa le pagine */
    for (i = 0; i < pages; i++) {
        uint32_t phys = pmm_alloc_page();
        /* Se la RAM e' finita si prova a fare posto: vedi swap.h. */
        if (phys == 0 && swap_sfratta()) phys = pmm_alloc_page();
        if (phys == 0) {
            /* OOM: libera le pagine già allocate */
            uint32_t j;
            for (j = 0; j < i; j++) {
                uint32_t va = vaddr + j * PAGE_SIZE;
                uint32_t pa = paging_get_physical(proc->page_directory, va);
                if (pa) pmm_free_page(pa);
                paging_unmap_page(proc->page_directory, va);
            }
            return ERR(ENOMEM);
        }

        /* Azzera la pagina appena allocata (via finestra: vedi sys_sbrk) */
        paging_azzera_fisica(phys);

        if (paging_map_page(proc->page_directory,
                             vaddr + i * PAGE_SIZE,
                             phys, pg_flags) != 0) {
            pmm_free_page(phys);
            return ERR(ENOMEM);
        }
    }

    /* Aggiorna heap_end del processo */
    uint32_t mapped_end = vaddr + pages * PAGE_SIZE;
    if (mapped_end > proc->heap_end) proc->heap_end = mapped_end;

    klog(LOG_DEBUG, "SYSCALL mmap: %u pagine a 0x%08x (prot=0x%x)",
         pages, vaddr, p->prot);

    return (int32_t)vaddr;
}

/* =============================================================================
 * SYS_MUNMAP (91) -- Rimuove un mapping
 *
 * ebx = addr
 * ecx = length
 * ============================================================================= */
int32_t sys_munmap(InterruptFrame *frame)
{
    uint32_t  addr   = frame->ebx & 0xFFFFF000;
    uint32_t  length = frame->ecx;
    Process  *proc   = proc_get_current();
    uint32_t  pages  = ALIGN_UP(length, PAGE_SIZE) / PAGE_SIZE;
    uint32_t  i;

    if (addr < USER_SPACE_BASE || addr >= USER_SPACE_END) return ERR(EINVAL);
    if (length == 0) return ERR(EINVAL);

    for (i = 0; i < pages; i++) {
        uint32_t va   = addr + i * PAGE_SIZE;
        uint32_t phys = paging_get_physical(proc->page_directory, va);
        if (phys) pmm_free_page(phys);
        paging_unmap_page(proc->page_directory, va);
    }

    /* ! SE LA ZONA ERA IN CIMA, IL CONFINE TORNA GIU' (kernel 0.158).
     *
     * Prima non succedeva mai: le pagine fisiche tornavano al PMM ma
     * heap_end restava dov'era. Ogni ciclo mmap/munmap si mangiava un
     * pezzo di SPAZIO DI INDIRIZZAMENTO che non tornava piu' — e il
     * garbage collector di GCC (ggc-page.cc) fa esattamente quel ciclo,
     * migliaia di volte, su ogni file che compila.
     *
     * Non e' una perdita di memoria fisica, e infatti non si vedeva: si
     * vedrebbe solo dopo molto lavoro, come una mmap che comincia a
     * fallire con dello spazio apparentemente libero.
     *
     * Solo la CIMA si puo' recuperare, come per sbrk: un buco in mezzo
     * resta un buco, perche' heap_end e' un confine e non una mappa. */
    if (addr + pages * PAGE_SIZE == proc->heap_end &&
        addr >= proc->heap_start) {
        proc->heap_end = addr;
        klog(LOG_DEBUG, "SYSCALL munmap: confine riportato a 0x%08x", addr);
    }

    klog(LOG_DEBUG, "SYSCALL munmap: %u pagine da 0x%08x", pages, addr);
    return 0;
}

/* =============================================================================
 * SYS_IOCTL (54) -- Controllo device
 *
 * ebx = fd
 * ecx = request
 * edx = arg
 * ============================================================================= */
int32_t sys_ioctl(InterruptFrame *frame)
{
    int32_t  fd      = (int32_t)frame->ebx;
    uint32_t request = frame->ecx;
    uint32_t arg     = frame->edx;
    Process *proc    = proc_get_current();
    int      is_tty;

    extern int drv_ioctl(int cmd, void *arg);

    if (fd < 0 || fd >= MAX_FD)          return ERR(EBADF);
    if (proc->fds[fd].type == FD_UNUSED) return ERR(EBADF);

    /* I tre descrittori standard sono tutti la console: non c'è ancora
     * un concetto di terminale distinto dal TTY del kernel. */
    is_tty = (proc->fds[fd].type == FD_STDIN  ||
              proc->fds[fd].type == FD_STDOUT ||
              proc->fds[fd].type == FD_STDERR);

    if (!is_tty) {
        klog(LOG_DEBUG, "SYSCALL ioctl(fd=%d, req=0x%x): non e' un terminale", fd, request);
        return ERR(ENOTTY);
    }

    /* TIOCGWINSZ di Linux, tenuta perché qualcuno potrebbe già usarla.
     * La forma nativa è TTY_IOCTL_GETSIZE qui sotto. */
    if (request == 0x5413) {
        uint16_t *winsize = (uint16_t *)arg;
        if (!syscall_verify_ptr(winsize, 8)) return ERR(EFAULT);
        winsize[0] = 25;   /* ws_row */
        winsize[1] = 80;   /* ws_col */
        winsize[2] = 640;  /* ws_xpixel */
        winsize[3] = 400;  /* ws_ypixel */
        return 0;
    }

    /* =====================================================================
     * Comandi nativi del TTY (drivers/tty/tty.h), finora irraggiungibili.
     *
     * drv_ioctl li implementava già tutti e cinque, ma questa syscall
     * rispondeva ENOSYS a chiunque: il driver aveva le funzioni e nessuna
     * porta d'ingresso. La porta serve a /bin/gfedit, che deve poter
     * spegnere lo specchio seriale prima di mettersi a ridisegnare
     * schermate intere (vedi TTY_IOCTL_SETRAW in tty.c).
     *
     * L'unico comando che riceve un PUNTATORE è GETSIZE; gli altri
     * portano un valore. Verificare l'argomento come puntatore anche per
     * quelli rifiuterebbe un legittimo SETCOLOR con un attributo che per
     * caso non è un indirizzo valido.
     * ===================================================================== */
    switch (request) {
        case TTY_IOCTL_GETSIZE:
            if (!syscall_verify_ptr((void *)arg, sizeof(TtyWinSize)))
                return ERR(EFAULT);
            return drv_ioctl((int)request, (void *)arg) == 0 ? 0 : ERR(EINVAL);

        /* Questi due riguardano UNA console precisa — quella del
         * chiamante — e vengono risolti qui invece che in drv_ioctl:
         * quella riceve solo (comando, argomento) e non ha modo di
         * sapere per conto di chi sta lavorando. Passarle il numero
         * attraverso una variabile condivisa sarebbe una corsa, perché
         * fra l'impostarla e la chiamata il timer può dare la CPU a un
         * processo di un'altra console. */
        case TTY_IOCTL_CLEAR:
            vga_clear_su(proc->console);
            return 0;

        case TTY_IOCTL_SETCOLOR:
            vga_setcolor_su(proc->console, (uint8_t)(arg & 0xF),
                            (uint8_t)((arg >> 4) & 0xF));
            return 0;

        case TTY_IOCTL_SETRAW:
        case TTY_IOCTL_SETCOOKED:
            return drv_ioctl((int)request, (void *)arg) == 0 ? 0 : ERR(EINVAL);

        default:
            break;
    }

    klog(LOG_DEBUG, "SYSCALL ioctl(fd=%d, req=0x%x, arg=0x%x) non implementato",
         fd, request, arg);
    return ERR(ENOSYS);
}

/* =============================================================================
 * SYS_CONSOLE_SWITCH (229) -- Porta in primo piano una console
 *
 * ebx = numero della console
 *
 * Non serve nessun permesso: la commutazione è un gesto dell'UTENTE, e
 * l'unico programma che la esegue è il driver tastiera quando riconosce
 * Alt+Fn. Il processo che chiama non cambia console — cambia solo cosa
 * si vede.
 * ============================================================================= */
int32_t sys_console_switch(InterruptFrame *frame)
{
    uint32_t n = frame->ebx;

    if (vga_switch_console(n) != 0) return ERR(EINVAL);
    return 0;
}

/* =============================================================================
 * SYS_CONSOLE_WRITE (230) -- Scrive su una console specifica
 *
 * ebx = numero della console
 * ecx = buf*
 * edx = lunghezza
 *
 * Esiste per il driver tastiera, che deve ecoare i tasti sulla console
 * di chi sta digitando — non sulla propria, che è la 0. Un normale
 * write(1, ...) non basterebbe: finisce sempre sulla console del
 * processo che scrive.
 * ============================================================================= */
int32_t sys_console_write(InterruptFrame *frame)
{
    uint32_t    n     = frame->ebx;
    const char *buf   = (const char *)frame->ecx;
    uint32_t    count = frame->edx;
    uint32_t    i;

    if (n >= VGA_N_CONSOLE)                  return ERR(EINVAL);
    if (count == 0)                          return 0;
    if (!syscall_verify_ptr((void *)buf, count)) return ERR(EFAULT);

    for (i = 0; i < count; i++) vga_putchar_su(n, buf[i]);
    return (int32_t)count;
}

/* =============================================================================
 * SYS_CONSOLE_INFO (231) -- Quante console, la mia, quella visibile
 *
 * ebx = ConsoleInfo*
 * ============================================================================= */
int32_t sys_console_info(InterruptFrame *frame)
{
    ConsoleInfo *out  = (ConsoleInfo *)frame->ebx;
    Process     *proc = proc_get_current();

    if (!syscall_verify_ptr(out, sizeof(ConsoleInfo))) return ERR(EFAULT);

    out->totale   = VGA_N_CONSOLE;
    out->mia      = proc->console;
    out->visibile = vga_visible_console();
    out->fg       = sched_console_fg(proc->console);
    return 0;
}

/* =============================================================================
 * SYS_EXEC (11) -- Esegue un programma ELF
 *
 * ebx = path*  (percorso del file ELF in /bin o /dev)
 * ecx = argv** (array argomenti, NULL-terminated)
 * edx = envp** (array environment, NULL-terminated)
 *
 * Sostituisce il processo corrente con il nuovo programma.
 * (Fase 4: ELF loader -- per ora placeholder funzionale)
 * ============================================================================= */
int32_t sys_exec(InterruptFrame *frame)
{
    const char  *path = (const char *)frame->ebx;
    Process     *proc = proc_get_current();
    ElfLoadResult res;
    char         kpath[PERCORSO_MAX];

    if (!syscall_verify_str(path, PERCORSO_MAX)) return ERR(EFAULT);

    /* Copia il path in un buffer kernel PRIMA di cambiare CR3: il
     * puntatore originale vive nello user stack del chiamante (vecchia
     * PD), che la nuova PD (quasi vuota) non mappa. Senza questa copia,
     * qualunque dereferenziazione successiva di 'path' (incluso il klog
     * di errore in elf_load se il file non esiste) causa un page fault. */
    {
        uint32_t pi;
        for (pi = 0; pi < sizeof(kpath) - 1 && path[pi]; pi++) kpath[pi] = path[pi];
        kpath[pi] = '\0';
    }

    /* Risolvi contro la directory corrente: `exec prog` dentro /bin deve
     * funzionare come `exec /bin/prog`. */
    {
        char abs[PERCORSO_MAX];
        if (resolve_path(kpath, abs, sizeof(abs)) != 0) return ERR(EINVAL);
        kstrcpy(kpath, abs, sizeof(kpath));
    }

    klog(LOG_INFO, "SYSCALL exec('%s') PID=%u", kpath, proc->pid);

    /* Crea una nuova Page Directory per il processo */
    PDE *new_pd = paging_create_directory();
    if (!new_pd) return ERR(ENOMEM);

    /* Salva la vecchia PD per eventuale rollback */
    PDE *old_pd = proc->page_directory;

    /* Imposta la nuova PD e attiva il mapping */
    proc->page_directory = new_pd;
    paging_switch(new_pd);

    /* Carica il binario ELF (usa kpath, valido in qualunque PD perché
     * vive nello stack della funzione kernel, mappato identicamente da
     * tutte le page directory). */
    int rc_elf = elf_load(kpath, proc, &res);
    if (rc_elf != 0) {
        /* Errore: ripristina vecchia PD */
        paging_destroy_directory(new_pd);
        proc->page_directory = old_pd;
        paging_switch(old_pd);
        /* Stessa distinzione di sys_spawn: «non c'e'» e' una risposta,
         * non un guasto — e chi ha chiesto la riceve e la riferisce. */
        klog(rc_elf == ERR(ENOENT) ? LOG_INFO : LOG_ERROR,
             "SYSCALL exec: ELF load fallito per '%s' (err=%d)", kpath, rc_elf);
        /* Stessi tre motivi di sys_spawn: vedi il commento la'. */
        if (rc_elf == ERR(ENOENT)) return ERR(ENOENT);
        if (rc_elf == ERR(EACCES)) return ERR(EACCES);
        return ERR(ENOEXEC);
    }

    /* Libera la vecchia PD (non serve più) */
    if (old_pd != paging_get_kernel_directory()) {
        paging_destroy_directory(old_pd);
    }

    /* ! LE ZONE CONDIVISE NON SOPRAVVIVONO A UN exec, e vanno chiuse QUI.
     *
     * L'immagine di prima non c'e' piu' e la nuova non ha nessuna di quelle
     * mappature: le pagine le ha gia' mollate paging_destroy_directory poco
     * sopra, passando dal PMM. Ma il PCB continuerebbe a dire che questo
     * processo tiene aperte quelle zone — con indirizzi che nella page
     * directory nuova non vogliono dire niente. La zona resterebbe viva per un
     * utente che non la vede, e il suo nome occupato fino alla morte del
     * processo. Si passa `smonta = 0` perche' non c'e' piu' niente da
     * smontare. */
    shm_cleanup_process(proc);

    klog(LOG_INFO, "SYSCALL exec: salto a entry=0x%08x stack=0x%08x",
         res.entry_point, res.user_stack_top);

    /* Salta al nuovo programma in ring3 -- non ritorna */
    sched_enter_usermode(res.entry_point, res.user_stack_top);

    /* Non raggiunto */
    return 0;
}

/* =============================================================================
 * SYS_SPAWN (2) -- Crea un processo figlio autonomo che esegue un programma
 *
 * ebx = path*   (percorso ELF, stringa in user space)
 * ecx = argc    (numero argomenti, incluso argv[0])
 * edx = argv*   (char** in user space, NULL-terminato; NULL = usa solo path)
 * esi = SpawnExtra* (ambiente e redirezioni; ignorato se la magia non
 *                    combacia — vedi kernel/include/syscall.h per il perche')
 *
 * Stack utente iniziale del figlio (visto da _start in lib/start.S):
 *   [esp+0]  = 0        (fake return address -- _start non ritorna)
 *   [esp+4]  = argc
 *   [esp+8]  = argv_ptr (char** verso array subito sopra, stessa pagina stack)
 *   [argc+1 puntatori argv[], NULL-terminati]
 *   [stringhe argv null-terminated, empilate dal top dello stack verso il basso]
 *
 * Scrittura sullo stack del figlio senza switch di CR3:
 *   paging_get_physical(child->pd, virt) restituisce l'indirizzo fisico
 *   corrispondente all'indirizzo virtuale virt nella PD del figlio.
 *   Poiche' il kernel usa identity mapping (fisico == virtuale per la memoria
 *   del kernel e per i frame utente prima che vengano rimappati), possiamo
 *   scrivere a quell'indirizzo fisico direttamente, senza mai cambiare CR3.
 *   Questo e' sicuro, non richiede cli, e funziona correttamente attraverso
 *   i confini di pagina.
 *
 * Ritorna: PID del figlio (>0), errno negativo in caso di errore
 * ============================================================================= */
/* ! ERA 16, ED E' COSTATO DUE GIORNI. Il driver di GCC lancia cc1 con
 * DICIASSETTE argomenti — li si conta da `gcc -v`:
 *
 *   cc1 -quiet -v inc.c -fno-pic -fno-asynchronous-unwind-tables -quiet
 *       -dumpbase inc.c -dumpbase-ext .c -mtune=i386 -march=i486 -O2
 *       -version -o /tmp/ccXXXXXX.s
 *
 * A sedici il taglio cadeva ESATTAMENTE fra `-o` e il nome del file, e cc1
 * rispondeva «missing filename after '-o'» — un messaggio che manda a
 * cercare il difetto nella generazione dei nomi temporanei, dove non c'era.
 * Sessantaquattro tiene una riga di link di collect2, che e' la piu' lunga
 * che si veda in una compilazione, e resta lontana dai 4096 di Linux perche'
 * allora ogni argomento costava un buffer intero. (Non e' piu' vero: le
 * stringhe stanno impacchettate nell'arena, vedi SPAWN_ARENA_BYTES.)
 *
 * ! E SOPRATTUTTO: adesso il troncamento NON esiste piu'. Chi supera il
 * tetto riceve E2BIG e un klog che dice quanti ne ha passati. Un comando
 * accorciato di nascosto e' il difetto peggiore che questa syscall possa
 * produrre, perche' l'errore lo dichiara il PROGRAMMA LANCIATO, parlando
 * dei propri argomenti, e nessuno pensa piu' al kernel. */
/* ! E POI SESSANTAQUATTRO NON SONO BASTATI, per la stessa ragione di
 * prima vista da piu' lontano: il tetto era tarato sulla riga di comando
 * piu' lunga che si fosse MAI VISTA, non su quella che un costruttore puo'
 * produrre. Da quando dentro EX-OS gira `make`, la riga la scrive un
 * makefile, e quella di FreeBASIC ne produce due che passano i duecento
 * argomenti:
 *
 *     ar rcs libfb.a <~200 oggetti>          la libreria della runtime
 *     fbc -x fbc <145 oggetti>               il link del compilatore
 *
 * Cinquecentododici e' scelto per stare largo su entrambe senza che il
 * costo si senta: gli array che lo dimensionano stanno sullo heap insieme
 * all'arena (vedi SpawnBuf), quindi alzarlo costa memoria solo durante la
 * spawn, e solo quanto la riga di comando usa davvero. */
#define MAX_SPAWN_ARGS  512
#define MAX_SPAWN_ENV   32     /* variabili d'ambiente ereditate da un figlio */

/* =============================================================================
 * Quanto puo' essere lungo UN argomento.
 *
 * ! ERA PERCORSO_MAX (320), E LA MOTIVAZIONE ERA «un argomento e' quasi
 * sempre un percorso». «Quasi» era la parola sbagliata, e l'ha trovata la
 * costruzione della runtime di FreeBASIC — dopo 427 file compilati, cioe' nel
 * punto piu' tardo e piu' caro:
 *
 *     CPPAS src/rtlib/obj/exos-x86/cpudetect.o
 *     [ERROR] SYSCALL spawn('/bin/sh'): argomento 2 illeggibile o piu'
 *             lungo di 319 byte
 *     make: fork: argomento non valido
 *
 * L'argomento 2 di `/bin/sh -c <riga>` non e' un percorso: e' UNA RIGA DI
 * COMANDO INTERA, e make ne passa una ogni volta che la ricetta contiene un
 * metacarattere. Le ricette di FreeBASIC cominciano tutte con
 * `@echo "CC $@";` — c'e' un punto e virgola, quindi passano tutte dalla
 * shell — e la riga completa gira intorno ai trecento byte.
 *
 * ! E' PER QUESTO CHE 426 FILE SU 427 SONO PASSATI. Stavamo appena sotto il
 * tetto per tutta la costruzione; la riga di `cpudetect.s` ha in piu' il
 * `-x assembler-with-cpp` — ventidue caratteri — e ha traboccato. Un limite
 * che si supera in funzione della LUNGHEZZA DEL NOME di un file e' un limite
 * che si scopre a caso, mesi dopo, su un file che non c'entra niente.
 *
 * Adesso il tetto e' l'arena: un singolo argomento non puo' comunque
 * superare lo spazio che li tiene tutti, quindi legarlo a un secondo numero
 * piu' piccolo aggiungeva un modo di fallire e nessuna protezione.
 * ============================================================================= */
#define MAX_ARG_LEN    SPAWN_ARENA_BYTES

/* Le stringhe di argv e dell'ambiente stanno in un'ARENA sullo heap, non
 * sullo stack del kernel.
 *
 * ! E' il tetto che conta davvero, ed e' sui BYTE TOTALI, non sul numero
 * di argomenti: sessantaquattro buffer da MAX_ARG_LEN l'uno sarebbero venti
 * kilobyte di stack a ogni spawn, per una riga di comando che quasi sempre
 * ne occupa trecento. Impacchettate, quattro kilobyte tengono la riga di
 * link piu' lunga che collect2 sappia produrre.
 *
 * ! ERA META' DELLO STACK INIZIALE DEL FIGLIO, e il legame era giusto per
 * la ragione sbagliata. Queste stringhe finiscono sullo stack UTENTE del
 * figlio, di cui elf_load impegnava USER_STACK_INIT byte e basta: legare
 * l'arena a quella costante garantiva che ci stessero, al prezzo di un
 * tetto di quattro kilobyte per la riga di comando di TUTTO il sistema.
 * Adesso il verso e' invertito — e' lo stack del figlio a essere impegnato
 * quanto la riga di comando chiede, vedi elf_load_argv — quindi l'arena
 * puo' essere dimensionata su cio' che serve davvero.
 *
 * Trentadue kilobyte: la `ar rcs libfb.a <200 oggetti>` di FreeBASIC sono
 * circa dieci kilobyte di soli percorsi, e il triplo lascia margine
 * all'ambiente, che divide l'arena con argv.
 *
 * ! E NON E' PIU' `static`: l'ambiente lo era, e due processi che facevano
 * spawn insieme si sovrascrivevano le variabili a vicenda — elf_load legge
 * dal disco, quindi fra il riempimento del buffer e il suo uso il processo
 * si blocca di sicuro. Non si e' mai visto perche' finora a lanciare
 * programmi era quasi sempre la sola shell. */
#define SPAWN_ARENA_BYTES  32768

/* =============================================================================
 * Tutto cio' che una spawn tiene da parte, in UNA allocazione sola.
 *
 * ! GLI ARRAY DI PUNTATORI STANNO QUI E NON SULLO STACK DEL KERNEL. A
 * sessantaquattro argomenti erano mezzo kilobyte e nessuno ci badava; a
 * cinquecentododici sono quattro kilobyte l'uno, e sys_spawn ne usa due —
 * i puntatori in kernel space e gli indirizzi utente corrispondenti. Otto
 * kilobyte di stack kernel in una funzione che poi chiama elf_load, che
 * legge dal disco, che passa dal VFS: e' il punto del sistema in cui la
 * catena di chiamate e' piu' profonda.
 *
 * ! E STANNO IN UNA STRUTTURA SOLA per non moltiplicare le uscite. Questa
 * funzione ha nove cammini d'errore, ognuno con la sua kfree: con tre
 * allocazioni separate sarebbero ventisette righe da tenere allineate, e
 * la prima che si dimentica e' una perdita di memoria che si vede solo
 * dopo qualche migliaio di comandi. Con una sola, `kfree(sb)` e' sempre
 * giusta. */
typedef struct {
    char     *kargv[MAX_SPAWN_ARGS + 1];
    char     *kenvp[MAX_SPAWN_ENV + 1];
    uint32_t  arg_ptrs[MAX_SPAWN_ARGS + 1];
    uint32_t  env_ptrs[MAX_SPAWN_ENV + 1];
    char      arena[SPAWN_ARENA_BYTES];
} SpawnBuf;

/* Copia `s` nell'arena e ne ritorna il puntatore, o NULL se non ci sta piu'. */
static char *spawn_arena_dup(char *arena, uint32_t *uso, const char *s)
{
    uint32_t len = 0, k;
    char    *dst;

    while (s[len]) len++;
    if (*uso + len + 1 > SPAWN_ARENA_BYTES) return NULL;

    dst = arena + *uso;
    for (k = 0; k <= len; k++) dst[k] = s[k];
    *uso += len + 1;
    return dst;
}

/* Scrive `len` byte da `src` all'indirizzo virtuale `user_virt` nella PD `pd`,
 * usando l'indirizzo fisico (identity-mapped) per ogni pagina — nessun switch CR3.
 * Ritorna 0 se ok, -1 se una pagina non e' mappata. */
static int spawn_write_user(PDE *pd, uint32_t user_virt,
                             const void *src, uint32_t len)
{
    const uint8_t *s = (const uint8_t *)src;
    while (len > 0) {
        uint32_t phys = paging_get_physical(pd, user_virt);
        if (!phys) return -1;
        uint32_t off   = user_virt & 0xFFF;
        uint32_t avail = PAGE_SIZE - off;
        uint32_t chunk = (len < avail) ? len : avail;
        /* La pagina di destinazione appartiene al FIGLIO, che non e' il
         * processo in esecuzione: si raggiunge dalla finestra, non dal suo
         * indirizzo fisico. Una pagina per volta, cosi' la finestra resta
         * aperta il tempo di una copia dentro un solo confine di pagina. */
        uint8_t *dst = (uint8_t *)paging_finestra_apri(phys);
        uint32_t k;
        for (k = 0; k < chunk; k++) dst[k] = s[k];
        paging_finestra_chiudi();
        s        += chunk;
        user_virt += chunk;
        len      -= chunk;
    }
    return 0;
}

int32_t sys_spawn(InterruptFrame *frame)
{
    const char  *path   = (const char *)frame->ebx;
    uint32_t     argc   = frame->ecx;
    char       **uargv  = (char **)frame->edx;
    Process     *parent = proc_get_current();
    ElfLoadResult res;
    char kpath[PERCORSO_MAX];

    if (!syscall_verify_str(path, PERCORSO_MAX)) return ERR(EFAULT);
    { uint32_t pi; for (pi = 0; pi < 255 && path[pi]; pi++) kpath[pi] = path[pi]; kpath[pi] = '\0'; }

    /* Percorso relativo -> assoluto usando la directory corrente. */
    { char abs[PERCORSO_MAX];
      if (resolve_path(kpath, abs, sizeof(abs)) != 0) return ERR(EINVAL);
      kstrcpy(kpath, abs, sizeof(kpath)); }

    /* --- Copia argv in kernel space ---------------------------------------- */
    SpawnBuf *sb        = (SpawnBuf *)kmalloc(sizeof(SpawnBuf));
    char     *arena;
    uint32_t  arena_uso = 0;
    char    **kargv;
    uint32_t i, real_argc = 0;

    if (sb == NULL) {
        klog(LOG_ERROR, "SYSCALL spawn('%s'): heap esaurito per gli argomenti",
             kpath);
        return ERR(ENOMEM);
    }
    arena = sb->arena;
    kargv = sb->kargv;

    if (uargv && argc > 0) {
        /* ! TROPPI ARGOMENTI E' UN ERRORE, non un motivo per accorciare la
         * riga. Il taglio silenzioso c'e' stato fino ad agosto 2026 e ha
         * fatto rispondere a cc1 «missing filename after '-o'»: chi legge
         * quel messaggio cerca il difetto nel programma lanciato, che aveva
         * ragione — a mentire era il kernel. */
        if (argc > MAX_SPAWN_ARGS) {
            klog(LOG_ERROR, "SYSCALL spawn('%s'): %u argomenti, il tetto e' %u",
                 kpath, argc, (uint32_t)MAX_SPAWN_ARGS);
            kfree(sb);
            return ERR(E2BIG);
        }
        for (i = 0; i < argc; i++) {
            if (!syscall_verify_ptr(&uargv[i], sizeof(char*))) break;
            const char *ua = uargv[i];

            /* Un argomento illeggibile o piu' lungo del tetto FERMA lo
             * spawn. Prima si usciva dal ciclo e il programma partiva con
             * gli argomenti raccolti fin li': `cp lungo dest` diventava
             * `cp`, che stampa il proprio uso — e l'utente conclude che il
             * file non esiste. Un errore esplicito dice cosa e' successo. */
            if (!ua || !syscall_verify_str(ua, MAX_ARG_LEN)) {
                klog(LOG_ERROR, "SYSCALL spawn('%s'): argomento %u illeggibile "
                                "o piu' lungo di %u byte", kpath, i,
                                MAX_ARG_LEN - 1u);
                kfree(sb);
                return ERR(EINVAL);
            }
            kargv[i] = spawn_arena_dup(arena, &arena_uso, ua);
            if (kargv[i] == NULL) {
                klog(LOG_ERROR, "SYSCALL spawn('%s'): riga di comando oltre "
                                "%u byte all'argomento %u", kpath,
                     (uint32_t)SPAWN_ARENA_BYTES, i);
                kfree(sb);
                return ERR(E2BIG);
            }
            real_argc++;
        }
    }
    if (real_argc == 0) {
        /* argv[0] = basename del path */
        const char *bn = kpath;
        for (const char *p = kpath; *p; p++) if (*p == '/') bn = p+1;
        kargv[0] = spawn_arena_dup(arena, &arena_uso, bn);
        if (kargv[0] == NULL) { kfree(sb); return ERR(E2BIG); }
        real_argc = 1;
    }
    kargv[real_argc] = NULL;

    /* --- Blocco EXTRA: ambiente e redirezioni ------------------------------
     *
     * Si legge ESI solo se e' un puntatore valido E la magia combacia. Un
     * programma compilato per la forma a tre argomenti ci lascia
     * spazzatura, e trattarla come una struttura significherebbe aprirgli
     * file a caso. Vedi il commento in syscall.h. */
    SpawnExtra *uex = (SpawnExtra *)frame->esi;
    SpawnExtra  kex;
    int         ha_extra = 0;

    /* ! SI LEGGE LA MAGIA PRIMA DI SAPERE QUANTO E' GRANDE IL BLOCCO, e per
     * leggerla bastano quattro byte. Verificarne 604 per un blocco che ne ha
     * 596 vuol dire rifiutare un chiamante legittimo la cui struttura sta a
     * ridosso della fine di una pagina: un difetto che si vede una volta ogni
     * mille, sul programma sbagliato. */
    if (uex != NULL && syscall_verify_ptr(uex, sizeof(uint32_t))) {
        uint32_t magia  = uex->magia;
        uint32_t quanti = 0;      /* byte che quella forma dichiara */
        uint32_t noti   = 0;      /* bit di `flag` che quella forma conosceva */

        /* ! OGNI FORMA MAI PUBBLICATA SI CONTINUA A CAPIRE. Il perche' sta in
         * lib/include/spawn_abi.h: un blocco ignorato e' un programma senza
         * redirezioni e senza ambiente, in silenzio, e i binari del CD degli
         * strumenti sono compilati contro la libc del giorno in cui sono stati
         * fatti. Il 24 agosto 2026 questo elenco aveva una riga sola, e il
         * `gcc` che gira dentro EX-OS ha smesso di redirigere l'uscita di cc1
         * su un file: la scriveva a video. */
        if (magia == SPAWN_EXTRA_MAGIA) {
            quanti = sizeof(SpawnExtra);
            noti   = SPAWN_F_CONSOLE | SPAWN_F_UTENTE;
        } else if (magia == SPAWN_EXTRA_MAGIA_V1) {
            quanti = SPAWN_EXTRA_V1_BYTE;
            noti   = SPAWN_F_V1;
        }

        if (quanti != 0 && syscall_verify_ptr(uex, quanti)) {
            uint32_t bi;
            const uint8_t *src = (const uint8_t *)uex;
            uint8_t       *dst = (uint8_t *)&kex;

            /* Cio' che quella forma ha, e ZERO per tutto il resto: un campo
             * che non esisteva vale zero, che e' esattamente «non l'ho
             * chiesto». */
            for (bi = 0; bi < quanti; bi++)              dst[bi] = src[bi];
            for (     ; bi < sizeof(SpawnExtra); bi++)   dst[bi] = 0;

            /* ! E I BIT CHE QUELLA FORMA NON CONOSCEVA SI SPENGONO. Un
             * programma vecchio non poteva chiedere SPAWN_F_UTENTE, e trovarlo
             * acceso significa spazzatura, non una richiesta: senza questa
             * riga farebbe nascere un figlio con l'identita' che capita. */
            kex.flag &= noti;

            if (kex.n_azioni > SPAWN_MAX_AZIONI) kex.n_azioni = SPAWN_MAX_AZIONI;
            ha_extra = 1;
        }
    }

    /* ! CHI CHIEDE UN FIGLIO DI UN ALTRO UTENTE DEV'ESSERE root, E SI CONTROLLA
     * QUI — prima che il figlio esista. La regola e' quella di setuid(), per la
     * stessa ragione: se un processo qualunque potesse scegliersi l'identita'
     * del figlio, i permessi si aggirerebbero con uno spawn, e il figlio
     * potrebbe fare cio' che il padre non puo'.
     *
     * ! E SI RIFIUTA, NON SI IGNORA. Partire lo stesso con l'identita'
     * ereditata vorrebbe dire che login, il giorno che un difetto lo lasciasse
     * girare da non-root, aprirebbe all'utente una shell CON I PRIVILEGI DI
     * QUELL'ALTRO — cioe' il guasto peggiore possibile, in silenzio. */
    if (ha_extra && (kex.flag & SPAWN_F_UTENTE) != 0 &&
        parent != NULL && parent->uid != 0) {
        klog(LOG_WARN, "SYSCALL spawn('%s'): il PID %u (uid %u) ha chiesto un "
                       "figlio uid %u: serve root", kpath, parent->pid,
             parent->uid, kex.uid);
        kfree(sb);
        return ERR(EPERM);
    }

    /* Ambiente: stesse regole degli argomenti — copiato in kernel space
     * PRIMA di creare il figlio, cosi' un puntatore illeggibile ferma lo
     * spawn invece di lasciare il processo a meta'. Divide l'arena con
     * argv: e' la riga di comando INTERA a dover stare in un tetto. */
    char      **kenvp     = sb->kenvp;
    uint32_t    real_envc = 0;

    if (ha_extra && kex.envp != NULL) {
        for (i = 0; i < MAX_SPAWN_ENV; i++) {
            const char *uv;

            if (!syscall_verify_ptr(&kex.envp[i], sizeof(char *))) break;
            uv = kex.envp[i];
            if (uv == NULL) break;
            if (!syscall_verify_str(uv, MAX_ARG_LEN)) {
                klog(LOG_ERROR, "SYSCALL spawn('%s'): variabile d'ambiente %u "
                                "illeggibile", kpath, i);
                kfree(sb);
                return ERR(EINVAL);
            }
            kenvp[real_envc] = spawn_arena_dup(arena, &arena_uso, uv);
            if (kenvp[real_envc] == NULL) {
                klog(LOG_ERROR, "SYSCALL spawn('%s'): ambiente oltre %u byte "
                                "alla variabile %u", kpath,
                     (uint32_t)SPAWN_ARENA_BYTES, i);
                kfree(sb);
                return ERR(E2BIG);
            }
            real_envc++;
        }
    }
    kenvp[real_envc] = NULL;

    klog(LOG_INFO, "SYSCALL spawn('%s') argc=%u envc=%u PID_parent=%u",
         kpath, real_argc, real_envc, parent->pid);

    /* --- Crea figlio in stato BLOCKED, carica ELF -------------------------- */

Process *child = proc_create(kpath, 0, PRIO_NORMAL, 0);

if (!child) {
kfree(sb);
return ERR(ENOMEM); }

    /* Il figlio nasce sulla console del padre: un programma lanciato dal
     * prompt della console 2 deve scrivere sulla 2, non su quella che si
     * sta guardando in questo istante. È ciò che gli permette di
     * continuare a lavorare — e a disegnare sul proprio schermo — mentre
     * l'utente e' passato altrove con Alt+Fn. */
    child->console = parent->console;

    /* ! L'IDENTITA' SI EREDITA, e chi vuole cambiarla dev'essere gia' root. Un
     * processo non puo' decidere di nascere con un utente diverso dal proprio:
     * se potesse, i permessi si aggirerebbero con uno spawn.
     *
     * ! DAL 24 AGOSTO 2026 root PUO' DIRLO, con SPAWN_F_UTENTE, e il permesso
     * e' gia' stato verificato la' dove si legge il blocco EXTRA. Serve a chi
     * lancia una sessione per conto di qualcun altro e deve RESTARE se stesso:
     * `login` e' un ciclo, e scendendo con setuid() non poteva piu' autenticare
     * l'accesso successivo. Vedi lib/include/spawn_abi.h. */
    child->uid = parent->uid;
    child->gid = parent->gid;

    if (ha_extra && (kex.flag & SPAWN_F_UTENTE) != 0) {
        child->uid = kex.uid;
        child->gid = kex.gid;
        klog(LOG_INFO, "SYSCALL spawn: '%s' nasce come uid %u gid %u "
                       "(chiesto dal PID %u)", kpath, child->uid, child->gid,
             parent->pid);
    }

    /* =========================================================================
     * ! LE TRE STANDARD SI EREDITANO DAL PADRE, e fino al 18 agosto 2026 non
     * era cosi': proc_create() le metteva sempre alla console, e un figlio
     * lanciato da una shell che aveva stdout altrove scriveva comunque sullo
     * schermo. Con le pipe non si vedeva — la shell le passa esplicitamente in
     * SpawnRedir, un caso per volta — ma un terminale dentro una finestra ha
     * reso il difetto lampante: `id` battuto li' dentro stampava sulla console
     * di testo, e nella finestra tornava soltanto il prompt.
     *
     * ! ED E' ANCHE CIO' CHE RENDE UTILE UN PTY. Senza questa eredita', dentro
     * uno pseudo-terminale ci parlerebbe solo la shell: ogni programma che
     * lancia scriverebbe da un'altra parte. Un terminale in cui l'output dei
     * comandi non si vede non e' un terminale.
     *
     * Le redirezioni esplicite vengono DOPO e vincono: sono una richiesta, e
     * questa e' solo l'eredita' di chi non ne ha chieste.
     * ========================================================================= */
    {
        int sfd;

        for (sfd = 0; sfd < 3; sfd++) {
            child->fds[sfd] = parent->fds[sfd];

            /* Un capo condiviso in piu' va contato, o il primo dei due che
             * chiude convincerebbe l'altro che non c'e' piu' nessuno. */
            switch (parent->fds[sfd].type) {
            case FD_PIPE_R: pipe_apri_lettore((int)parent->fds[sfd].inode);   break;
            case FD_PIPE_W: pipe_apri_scrittore((int)parent->fds[sfd].inode); break;
            case FD_PTY_M:  pty_apri_riferimento((int)parent->fds[sfd].inode, 1); break;
            case FD_PTY_S:  pty_apri_riferimento((int)parent->fds[sfd].inode, 0); break;

            /* ! UN FILE APERTO NON SI EREDITA, E VA DETTO. Il figlio si
             * ritroverebbe la posizione di lettura del padre e un secondo
             * riferimento nella VFS da chiudere: chi vuole passare un file lo
             * fa con una redirezione esplicita, che quella strada la percorre
             * per intero. Qui si torna alla console. */
            case FD_FILE:
            case FD_DRIVER:
                child->fds[sfd].type        = (sfd == 0) ? FD_STDIN :
                                              (sfd == 1) ? FD_STDOUT : FD_STDERR;
                child->fds[sfd].inode       = 0;
                child->fds[sfd].offset      = 0;
                child->fds[sfd].driver_data = NULL;
                break;

            default:
                break;
            }
        }
    }

    /* ! MA SE IL CHIAMANTE NE HA CHIESTA UNA, VINCE QUELLA. Serve al server
     * grafico, che deve stare su una console sua mentre su un'altra gira una
     * shell. Il flag e' obbligatorio: senza, una struttura azzerata con
     * memset chiederebbe la console 0 invece di dire «eredita». */
    /* ! `kex` VALE QUALCOSA SOLO SE ha_extra, e senza questa condizione si
     * leggeva spazzatura dello stack. Il sintomo era un avviso assurdo —
     * «console 2723776 non esiste» — a ogni comando battuto nella shell, e
     * quell'avviso e' stato la cosa che ha fatto scoprire il difetto grosso:
     * il blocco della shell veniva RIFIUTATO per via della magia vecchia.
     * Il danno diretto era nullo (il valore non passa il controllo di
     * intervallo), ma un campo non inizializzato che ogni tanto ha il bit
     * giusto acceso e' una console scelta a caso. */
    if (ha_extra && (kex.flag & SPAWN_F_CONSOLE) != 0) {
        if (kex.console < VGA_N_CONSOLE) child->console = kex.console;
        else klog(LOG_WARN, "SYSCALL spawn: console %u non esiste, eredito la %u",
                  kex.console, parent->console);
    }

    /* ! E LA DIRECTORY CORRENTE, per la stessa ragione della console: un
     * programma lanciato dalla shell deve partire da dove si trovava chi
     * l'ha lanciato. Senza, `gcc prova.c` dopo un `cd /lavoro` cercherebbe
     * /prova.c — e la shell offre `cd`, quindi e' lecito aspettarselo.
     *
     * Finche' la directory era una sola per il sistema questa riga non
     * serviva: il figlio la trovava gia' buona perche' era di tutti. E'
     * l'altra faccia di quel difetto — vedi Process.cwd in sched.h. */
    kstrcpy(child->cwd, parent->cwd[0] ? parent->cwd : "/", sizeof(child->cwd));

/* ! SI DICE A elf_load QUANTO STACK SERVE, e si dice PRIMA di caricare.
 * Qui sotto le stringhe di argv e i due array di puntatori vengono scritti
 * sullo stack del figlio con spawn_write_user, che legge la tabella delle
 * pagine dall'esterno: una pagina non mappata non genera nessun fault che
 * possa mapparla, perche' non c'e' nessun processo in esecuzione a cui il
 * fault possa capitare. Chiedere lo spazio dopo non e' possibile.
 *
 * Il conto e' esattamente cio' che il blocco «Costruisci argc/argv» qui
 * sotto consuma — stringhe, i due vettori di puntatori, il frame C da 16
 * byte — piu' un margine per i due allineamenti a quattro. Tenerlo
 * allineato a quel blocco e' l'unica cosa che conta: se un giorno gli si
 * aggiunge qualcosa (un auxv, per dire) e questo conto resta indietro, il
 * sintomo e' «stack non mappato per argv[n]» su una riga di comando lunga
 * — cioe' intermittente, e che accusa la paginazione. */
uint32_t stack_serve = arena_uso
                     + (real_argc + 1) * sizeof(uint32_t)
                     + (real_envc + 1) * sizeof(uint32_t)
                     + 16    /* frame C: ret fittizio, argc, argv, envp */
                     + 32;   /* margine per i due allineamenti a 4 */

int rc_elf = elf_load_argv(kpath, child, &res, stack_serve);
if (rc_elf != 0) {

/* FIX BUG #5 (path ELF fail): proc_kill sposta in ZOMBIE e rimuove
         * dalla runq; proc_reap_zombie libera immediatamente le risorse.
         * Chiamarli in sequenza senza cedere la CPU è sicuro: il reaper di
         * init non ha modo di intervenire tra i due perché non c'è switch di
         * contesto (siamo in syscall con interrupt abilitati ma senza yield). */
        proc_kill(child->pid);
        proc_reap_zombie(child);
        /* «Non c'e'» e' la risposta normale a chi cerca nel PATH, non un
         * guasto: vedi il commento esteso su vfs_open in elf_carica. Un
         * file che c'e' e non si carica resta invece un errore. */
        klog(rc_elf == ERR(ENOENT) ? LOG_INFO : LOG_ERROR,
             "SYSCALL spawn: ELF load fallito per '%s' (err=%d)", kpath, rc_elf);
        kfree(sb);
        /* ! SI RENDE IL MOTIVO VERO, non ENOENT per tutto. Rispondere
         * «non esiste» a proposito di un binario corrotto manda la shell
         * a cercarlo nella voce successiva del PATH, e il suo verdetto
         * finale — «comando non trovato» — indica il posto sbagliato a
         * chi deve capire cos'e' andato storto.
         *
         * ! E DAL 17 AGOSTO 2026 I MOTIVI VERI SONO TRE, non due. Con i
         * permessi esiste anche EACCES, e schiacciarlo in ENOEXEC faceva
         * dire alla shell «non e' un programma eseguibile» di un binario
         * che lo E' — mandando a cercare il difetto nel file invece che
         * nei permessi. E' lo stesso errore che questo commento
         * denunciava, un caso piu' in la'. */
        if (rc_elf == ERR(ENOENT)) return ERR(ENOENT);
        if (rc_elf == ERR(EACCES)) return ERR(EACCES);
        return ERR(ENOEXEC);
    }

    /* --- Costruisci argc/argv sullo stack del figlio ----------------------- */
    /* Scriviamo sullo stack del figlio tramite paging_get_physical: otteniamo
     * l'indirizzo fisico di ogni pagina dello stack (gia' allocata e mappata
     * da elf_load) e ci scriviamo direttamente. Il kernel usa identity mapping
     * per la memoria fisica: indirizzo fisico == indirizzo virtuale in kernel
     * space, quindi il puntatore fisico e' accessibile da qui senza cambiare
     * CR3. Nessun rischio di interrupt nel mezzo di un cambio di PD. */

uint32_t usp       = res.user_stack_top;
    uint32_t *arg_ptrs = sb->arg_ptrs;
    uint32_t *env_ptrs = sb->env_ptrs;
    uint32_t envv_uptr;
    PDE     *cpd       = child->page_directory;

    /* 0. Stringhe dell'ambiente, sopra quelle di argv: l'ordine fra i due
     *    gruppi non conta, contano i puntatori. */
    for (i = 0; i < real_envc; i++) {
        uint32_t len = 0;
        while (kenvp[i][len]) len++;
        len++;
        usp -= len;
        if (spawn_write_user(cpd, usp, kenvp[i], len) != 0) {
            klog(LOG_ERROR, "SYSCALL spawn: stack non mappato per envp[%u]", i);
            goto spawn_fail;
        }
        env_ptrs[i] = usp;
    }
    env_ptrs[real_envc] = 0;

    /* 1. Stringhe argv (partendo dal top e scendendo) */
    for (i = 0; i < real_argc; i++) {
        uint32_t len = 0;
        while (kargv[i][len]) len++;
        len++;          /* include il NUL */
        usp -= len;
        if (spawn_write_user(cpd, usp, kargv[i], len) != 0) {
            klog(LOG_ERROR, "SYSCALL spawn: stack non mappato per argv[%u]", i);
            goto spawn_fail;
        }
        arg_ptrs[i] = usp;
    }
    arg_ptrs[real_argc] = 0;

    /* 2. Allineamento a 4 byte */
    usp &= ~3u;

    /* 3. Array di puntatori argv[] (NULL-terminato) */
    usp -= (real_argc + 1) * sizeof(uint32_t);
    uint32_t argv_uptr = usp;
    for (i = 0; i <= real_argc; i++) {
        if (spawn_write_user(cpd, usp + i*4, &arg_ptrs[i], 4) != 0)
            goto spawn_fail;
    }

    /* 3b. Array di puntatori envp[] (NULL-terminato) */
    usp &= ~3u;
    usp -= (real_envc + 1) * sizeof(uint32_t);
    envv_uptr = usp;
    for (i = 0; i <= real_envc; i++) {
        if (spawn_write_user(cpd, usp + i*4, &env_ptrs[i], 4) != 0)
            goto spawn_fail;
    }

    /* 4. Frame C: [fake_ret=0, argc, argv_ptr, envp_ptr]
     *
     * Il terzo argomento e' nuovo (0.150) e non rompe niente: un _start
     * che ne legge due lo ignora. */
    usp -= 16;
    { uint32_t z = 0;
      spawn_write_user(cpd, usp+0,  &z,          4);
      spawn_write_user(cpd, usp+4,  &real_argc,  4);
      spawn_write_user(cpd, usp+8,  &argv_uptr,  4);
      spawn_write_user(cpd, usp+12, &envv_uptr,  4); }

    /* --- Redirezioni: il figlio apre i propri file -------------------------
     *
     * Si fa QUI, dopo che l'ELF e' stato caricato e prima di rendere il
     * figlio schedulabile: un processo non deve mai partire con una
     * redirezione applicata a meta'. */
    if (ha_extra) {
        uint32_t k;

        for (k = 0; k < kex.n_azioni; k++) {
            SpawnAzione *az = &kex.azioni[k];
            char         abs[PERCORSO_MAX];
            int          h;

            if (az->fd >= MAX_FD) {
                klog(LOG_ERROR, "SYSCALL spawn: redirezione su fd %u fuori range",
                     az->fd);
                goto spawn_fail;
            }

            /* =========================================================
             * EREDITA' DI UN DESCRITTORE — e' cio' che rende usabili le
             * pipe fra due processi.
             *
             * ! SI COPIA IL DESCRITTORE E SI PRENDE UN RIFERIMENTO IN
             * PIU'. Copiarlo e basta darebbe due processi che credono di
             * possedere lo stesso oggetto: il primo che chiude lo
             * chiuderebbe anche all'altro. Per i file e' il conteggio del
             * VFS, per le pipe quello delle estremita'.
             *
             * ! NON si eredita FD_DRIVER: `driver_data` e' un puntatore
             * a stato che appartiene al processo che ha aperto il device,
             * e passarlo a un altro vorrebbe dire due processi sullo
             * stesso stato privato. Si rifiuta invece di provarci.
             * ========================================================= */
            if (az->tipo == SPAWN_AZ_FD) {
                int pf = (int)az->fd_padre;

                if (pf < 0 || pf >= MAX_FD ||
                    parent->fds[pf].type == FD_UNUSED) {
                    klog(LOG_ERROR, "SYSCALL spawn: fd %d del padre non valido",
                         pf);
                    goto spawn_fail;
                }
                if (parent->fds[pf].type == FD_DRIVER) {
                    klog(LOG_ERROR, "SYSCALL spawn: un fd di driver non si "
                         "eredita (fd %d)", pf);
                    goto spawn_fail;
                }

                if (parent->fds[pf].type == FD_FILE) {
                    int r = vfs_dup((int)parent->fds[pf].inode);
                    if (r < 0) {
                        klog(LOG_ERROR, "SYSCALL spawn: vfs_dup del fd %d "
                             "fallita (%d)", pf, r);
                        goto spawn_fail;
                    }
                } else if (parent->fds[pf].type == FD_PIPE_R) {
                    pipe_apri_lettore((int)parent->fds[pf].inode);
                } else if (parent->fds[pf].type == FD_PIPE_W) {
                    pipe_apri_scrittore((int)parent->fds[pf].inode);
                } else if (parent->fds[pf].type == FD_PTY_M) {
                    pty_apri_riferimento((int)parent->fds[pf].inode, 1);
                } else if (parent->fds[pf].type == FD_PTY_S) {
                    /* ! IL CONTEGGIO SALE ANCHE QUI, e dimenticarlo sarebbe il
                     * difetto piu' difficile da vedere di tutto il pty: il
                     * figlio userebbe lo slave, il padre chiuderebbe il suo, il
                     * conteggio andrebbe a zero e il master leggerebbe «fine
                     * dei dati» mentre dall'altra parte una shell viva sta
                     * ancora scrivendo. */
                    pty_apri_riferimento((int)parent->fds[pf].inode, 0);
                }

                child->fds[az->fd] = parent->fds[pf];

                klog(LOG_INFO, "SYSCALL spawn: fd %u del figlio = fd %d del "
                     "padre (tipo %d)", az->fd, pf, (int)parent->fds[pf].type);
                continue;
            }

            /* Il percorso e' gia' in kernel space, ma nessuno garantisce
             * che sia terminato. */
            az->percorso[SPAWN_RED_PATH_MAX - 1] = '\0';

            if (resolve_path(az->percorso, abs, sizeof(abs)) != 0) {
                klog(LOG_ERROR, "SYSCALL spawn: percorso di redirezione non "
                     "valido: '%s'", az->percorso);
                goto spawn_fail;
            }

            h = vfs_open(abs, az->flags);
            if (h < 0) {
                klog(LOG_ERROR, "SYSCALL spawn: redirezione di fd %u su '%s' "
                     "fallita (%d)", az->fd, abs, h);
                goto spawn_fail;
            }

            child->fds[az->fd].type        = FD_FILE;
            child->fds[az->fd].flags       = az->flags;
            child->fds[az->fd].offset      = 0;
            child->fds[az->fd].inode       = (uint32_t)h;
            child->fds[az->fd].driver_data = NULL;

            klog(LOG_INFO, "SYSCALL spawn: fd %u del figlio -> '%s'",
                 az->fd, abs);
        }
    }

proc_set_entry(child, res.entry_point, usp);

proc_set_ready(child);

klog(LOG_INFO, "SYSCALL spawn: PID %u (entry=0x%08x usp=0x%08x) parent=%u",
         child->pid, res.entry_point, usp, parent->pid);
    kfree(sb);
    return (int32_t)child->pid;

spawn_fail:
    kfree(sb);
    /* FIX BUG #5: rendiamo il cleanup atomico disabilitando gli interrupt
     * per tutta la durata. proc_kill → ZOMBIE, proc_reap_zombie → UNUSED.
     * Nessun IRQ0 può far scansionare il reaper di init tra i due passi,
     * eliminando la race condition di doppia free sulla page directory. */
    interrupts_disable();
    proc_kill(child->pid);
    proc_reap_zombie(child);
    interrupts_enable();
    return ERR(ENOMEM);
}

/* =============================================================================
 * SYS_SCHED_YIELD (158) -- Cede volontariamente la CPU
 * ============================================================================= */
int32_t sys_sched_yield(InterruptFrame *frame)
{
    (void)frame;
    sched_yield();
    return 0;
}

/* =============================================================================
 * SYS_SLEEP (162) -- Dorme per N millisecondi
 *
 * ebx = millisecondi
 * ============================================================================= */
int32_t sys_sleep(InterruptFrame *frame)
{
    uint32_t ms = frame->ebx;
    sched_sleep(ms);
    return 0;
}

/* =============================================================================
 * SYS_SBRK (45) -- Espande/riduce lo heap del processo
 *
 * ebx = increment (signed, può essere negativo)
 *
 * Ritorna: indirizzo vecchio heap_end, o errore
 * ============================================================================= */
int32_t sys_sbrk(InterruptFrame *frame)
{
    int32_t   incr = (int32_t)frame->ebx;
    Process  *proc = proc_get_current();
    uint32_t  old_end;
    uint32_t  pages, i, pg_flags;

    /* ! NIENTE RIPIEGO SU USER_SPACE_BASE (kernel 0.156).
     *
     * Qui c'era: se heap_start e' 0, si parte da USER_SPACE_BASE. Era una
     * risposta plausibile e sbagliata. USER_SPACE_BASE vale 0x04000000,
     * cioe' SOTTO l'indirizzo a cui vengono caricati i programmi
     * (0x08000000): uno heap che fosse partito di li' avrebbe raggiunto il
     * TESTO DEL PROCESSO STESSO dopo 64 MB, e paging_map_page() lo avrebbe
     * rimappato in silenzio. Il processo sarebbe poi saltato dentro
     * memoria azzerata.
     *
     * Oggi non ci arriva nessuno — elf_load imposta sempre heap_start — ma
     * un ripiego che indovina un indirizzo e' una trappola che aspetta il
     * primo processo costruito in un altro modo. Chi non ha uno spazio di
     * indirizzamento preparato non ha uno heap, e lo dice. */
    if (proc->heap_start == 0 || proc->heap_max == 0) {
        klog(LOG_ERROR, "SYSCALL sbrk: PID %u non ha uno heap "
             "(spazio di indirizzamento non preparato da elf_load)", proc->pid);
        return ERR(ENOMEM);
    }

    old_end = proc->heap_end;

    if (incr == 0) return (int32_t)old_end;

    if (incr > 0) {
        /* FIX BUG #4: non riusare il frame (corromperebbe EBX/ECX del
         * processo utente al ritorno via popad in syscall_stub).
         * Espandiamo l'heap allocando e mappando le pagine direttamente,
         * senza passare per sys_mmap che richiederebbe un frame valido. */
        pages    = ALIGN_UP((uint32_t)incr, PAGE_SIZE) / PAGE_SIZE;
        pg_flags = PG_PRESENT | PG_WRITABLE | PG_USER;

        /* IL TETTO. Si controlla PRIMA di allocare, non a meta' del ciclo:
         * fermarsi dopo aver mappato mezza richiesta lascerebbe heap_end
         * avanzato di un valore che il chiamante non ha mai visto.
         *
         * ! Il confronto e' scritto per NON traboccare: `pages` arriva da
         * un int32_t positivo, quindi al piu' 2^19 pagine, ma heap_end e'
         * gia' vicino a 3 GB e la somma su 32 bit puo' girare. Con la
         * sottrazione a sinistra non gira, perche' heap_max >= heap_end
         * per costruzione. */
        if (proc->heap_end > proc->heap_max ||
            pages > (proc->heap_max - proc->heap_end) / PAGE_SIZE) {
            klog(LOG_WARN, "SYSCALL sbrk: PID %u chiede %d byte a 0x%08x, "
                 "il tetto e' 0x%08x", proc->pid, incr,
                 proc->heap_end, proc->heap_max);
            return ERR(ENOMEM);
        }

        for (i = 0; i < pages; i++) {
            uint32_t vaddr = proc->heap_end + i * PAGE_SIZE;
            uint32_t phys  = pmm_alloc_page();
            /* Se la RAM e' finita si prova a fare posto: vedi swap.h. */
            if (phys == 0 && swap_sfratta()) phys = pmm_alloc_page();
            if (phys == 0) {
                /* OOM: rilascia le pagine già allocate in questo ciclo */
                uint32_t j;
                for (j = 0; j < i; j++) {
                    uint32_t va = proc->heap_end + j * PAGE_SIZE;
                    uint32_t pa = paging_get_physical(proc->page_directory, va);
                    if (pa) pmm_free_page(pa);
                    paging_unmap_page(proc->page_directory, va);
                }
                return ERR(ENOMEM);
            }
            /* Azzera la pagina attraverso la finestra di rimappatura: la
             * pagina appena allocata puo' stare ovunque in RAM, e qui e'
             * caricato il CR3 del processo chiamante, che mappa per
             * identita' solo la fascia kernel. Scriverci all'indirizzo
             * fisico e' esattamente il page fault in ring0 che questa
             * finestra esiste per evitare. */
            paging_azzera_fisica(phys);

            if (paging_map_page(proc->page_directory, vaddr, phys, pg_flags) != 0) {
                pmm_free_page(phys);
                /* Rilascia le precedenti */
                uint32_t j;
                for (j = 0; j < i; j++) {
                    uint32_t va = proc->heap_end + j * PAGE_SIZE;
                    uint32_t pa = paging_get_physical(proc->page_directory, va);
                    if (pa) pmm_free_page(pa);
                    paging_unmap_page(proc->page_directory, va);
                }
                return ERR(ENOMEM);
            }
        }
        proc->heap_end += pages * PAGE_SIZE;

    } else {
        /* Riduzione: libera pagine direttamente (stessa ragione: no frame mutation) */
        uint32_t shrink = (uint32_t)(-incr);
        uint32_t new_end;

        /* ! SI ARROTONDA PER DIFETTO A PAGINE INTERE (kernel 0.157), e
         * non e' pignoleria: prima si faceva ALIGN_UP e si liberava a
         * partire da new_end, cioe' si buttava via LA PAGINA CHE CONTIENE
         * new_end — quella dove i byte sotto il nuovo confine sono ancora
         * vivi.
         *
         *      heap_end = 0x9000, sbrk(-0x800)  ->  new_end = 0x8800
         *      pages = ALIGN_UP(0x800)/0x1000 = 1
         *      libera la pagina di 0x8800, cioe' 0x8000-0x8FFF
         *      ma 0x8000-0x87FF sono ancora del chiamante
         *
         * Non dava nessun errore: dava byte che sparivano dallo heap di un
         * processo che li stava usando. Non si notava perche' nessuno
         * chiamava sbrk con un incremento negativo — ora la free() della
         * libc lo fa (vedi heap_restituisci in lib/libc.c).
         *
         * Arrotondare per DIFETTO e non per eccesso e' la scelta
         * conservativa: si restituisce meno di quanto chiesto, mai una
         * pagina che serve ancora. heap_end resta multiplo di pagina, come
         * lo e' sempre stato crescendo. */
        shrink &= ~(PAGE_SIZE - 1u);
        if (shrink == 0) return (int32_t)old_end;   /* niente da restituire */

        /* ! Scritto per non traboccare: `shrink > heap_end - heap_start`
         * invece di `heap_end - shrink < heap_start`, che gira quando la
         * richiesta e' piu' grande dello heap intero. */
        if (shrink > proc->heap_end - proc->heap_start) return ERR(EINVAL);
        new_end = proc->heap_end - shrink;

        pages = shrink / PAGE_SIZE;
        for (i = 0; i < pages; i++) {
            uint32_t va   = new_end + i * PAGE_SIZE;
            uint32_t phys = paging_get_physical(proc->page_directory, va);
            if (phys) pmm_free_page(phys);
            paging_unmap_page(proc->page_directory, va);
        }
        proc->heap_end = new_end;

        /* ! LO HEAP CHE SI RESTRINGE SI DICE, e a LOG_INFO, non a DEBUG.
         * E' un evento raro — solo heap_restituisci() della libc lo
         * provoca — e smappa pagine sotto i piedi di un processo vivo: se
         * qualcosa la' dentro era ancora in uso, il fault arriva dopo, in
         * un punto che con sbrk non sembra avere niente a che fare. Averlo
         * nel registro subito sopra il fault e' la differenza fra vedere
         * il nesso e cercarlo. */
        klog(LOG_INFO, "SYSCALL sbrk: PID %u RESTITUISCE %u KB, "
             "heap 0x%08x -> 0x%08x", proc->pid, shrink / 1024u,
             old_end, new_end);
    }

    klog(LOG_DEBUG, "SYSCALL sbrk(%d): heap 0x%08x -> 0x%08x",
         incr, old_end, proc->heap_end);

    return (int32_t)old_end;
}

/* =============================================================================
 * SYS_GETCWD (183) -- Ritorna la directory di lavoro corrente
 *
 * ebx = buf*   (buffer utente)
 * ecx = size   (dimensione buffer)
 * ============================================================================= */
int32_t sys_getcwd(InterruptFrame *frame)
{
    char    *buf  = (char *)frame->ebx;
    uint32_t size = frame->ecx;
    uint32_t len;

    if (!syscall_verify_ptr(buf, size)) return ERR(EFAULT);
    if (size == 0)                       return ERR(EINVAL);

    {
        const char *cwd = cwd_corrente();

        len = kstrlen(cwd);
        if (len + 1 > size) return ERR(EINVAL);

        kstrcpy(buf, cwd, size);
    }
    return (int32_t)len;
}

/* =============================================================================
 * SYS_CHDIR (12) -- Cambia directory di lavoro
 *
 * ebx = path*
 * ============================================================================= */
int32_t sys_chdir(InterruptFrame *frame)
{
    const char *path = (const char *)frame->ebx;

    if (!syscall_verify_str(path, PERCORSO_MAX)) return ERR(EFAULT);

    {
        char     abs[PERCORSO_MAX];
        VfsStat  st;

        if (resolve_path(path, abs, sizeof(abs)) != 0) return ERR(EINVAL);

        /* VERIFICA AGGIUNTA (luglio 2026): prima chdir() accettava
         * qualunque stringa senza controllare nulla — `cd /inesistente`
         * "riusciva" e da quel momento ogni percorso relativo puntava
         * in un posto che non c'è. Ora si conferma che la destinazione
         * esista e sia davvero una directory.
         *
         * Il caso speciale della root non serve piu': vfs_stat() sa che
         * la radice di OGNI montaggio (sia "/" sia "/disk") esiste e non
         * ha una voce di directory da interrogare. Quel controllo, che
         * qui riguardava solo "/", avrebbe dovuto essere esteso a ogni
         * punto di montaggio — e dimenticarlo avrebbe reso `cd /disk`
         * un "non trovato". */
        if (vfs_stat(abs, &st) != 0) {
            klog(LOG_DEBUG, "SYSCALL chdir('%s'): non trovato", abs);
            return ERR(ENOENT);
        }
        if (!st.is_dir) {
            klog(LOG_DEBUG, "SYSCALL chdir('%s'): non e' una directory", abs);
            return ERR(EINVAL);
        }

        /* ! SI CAMBIA LA DIRECTORY DI QUESTO PROCESSO, non quella del
         * sistema. Prima era una variabile sola e `cd` dentro un processo
         * spostava tutti gli altri — vedi il commento su Process.cwd in
         * kernel/include/sched.h.
         *
         * Fuori da un contesto di processo non c'e' niente da cambiare:
         * ci si arriva solo prima che lo scheduler parta, dove nessuno
         * chiama chdir. */
        {
            Process *p = proc_get_current();

            if (p == NULL) return ERR(ESRCH);
            kstrcpy(p->cwd, abs, sizeof(p->cwd));
        }
    }

    klog(LOG_DEBUG, "SYSCALL chdir('%s')", cwd_corrente());
    return 0;
}

/* =============================================================================
 * SYS_STAT (106) -- Informazioni su un file
 *
 * ebx = path*
 * ecx = stat*  (buffer Stat utente)
 * ============================================================================= */
int32_t sys_stat(InterruptFrame *frame)
{
    const char *path = (const char *)frame->ebx;
    Stat       *st   = (Stat *)frame->ecx;
    char        abs[PERCORSO_MAX];
    VfsStat     vs;
    int32_t     r;

    if (!syscall_verify_str(path, PERCORSO_MAX)) return ERR(EFAULT);
    if (!syscall_verify_ptr(st, sizeof(Stat)))   return ERR(EFAULT);

    if (resolve_path(path, abs, sizeof(abs)) != 0) return ERR(EINVAL);

    r = vfs_stat(abs, &vs);
    if (r != 0) return r;

    /* I campi di Stat hanno i nomi di FAT12 perche' li' e' nata, ma il
     * dato arriva dal VFS e vale su qualunque filesystem montato.
     *
     * ! QUI st_first_clus RESTAVA 0 «per non inventare un numero», e la
     * motivazione si e' rivelata sbagliata al primo programma che quel
     * numero l'ha letto davvero: il ragionamento per esteso, e cosa e'
     * costato, stanno sopra stat_interno() in kernel/fs/vfs.c. Adesso e'
     * st_ident e il VFS lo compone per tutti i filesystem.
     *
     * Gli attributi usano le convenzioni FAT (0x10 directory, 0x01 sola
     * lettura) perche' sono quelle che i programmi gia' interpretano. */
    st->st_size       = vs.dimensione;
    st->st_ident      = vs.ident;
    st->st_attr       = (uint16_t)((vs.is_dir       ? 0x10 : 0x00) |
                                   (vs.sola_lettura ? 0x01 : 0x00));
    /* Dal 0.168 la data arriva davvero dal filesystem. Zero continua a
     * significare «questo volume non la tiene» — la libc lo sa e stampa
     * dei trattini invece di inventare il 1980. */
    st->st_date       = vs.data;
    st->st_time       = vs.ora;

    klog(LOG_DEBUG, "SYSCALL stat('%s') -> %u byte%s", abs, vs.dimensione,
         vs.is_dir ? " (directory)" : "");
    return 0;
}

/* =============================================================================
 * SYS_LSEEK (19) -- Sposta la posizione in un file
 *
 * ebx = fd
 * ecx = offset
 * edx = whence (0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END)
 * ============================================================================= */
int32_t sys_lseek(InterruptFrame *frame)
{
    int32_t  fd     = (int32_t)frame->ebx;
    int32_t  offset = (int32_t)frame->ecx;
    uint32_t whence = frame->edx;
    Process *proc   = proc_get_current();
    uint32_t new_off;

    if (fd < 0 || fd >= MAX_FD)          return ERR(EBADF);
    if (proc->fds[fd].type == FD_UNUSED) return ERR(EBADF);
    if (proc->fds[fd].type != FD_FILE)   return ERR(EBADF);

    switch (whence) {
        case 0: /* SEEK_SET */
            if (offset < 0) return ERR(EINVAL);
            new_off = (uint32_t)offset;
            break;
        case 1: /* SEEK_CUR */
            /* Un offset negativo non deve poter scendere sotto zero: su
             * interi senza segno diventerebbe una posizione enorme, e la
             * lettura successiva fallirebbe con un errore che non
             * assomiglia alla causa. */
            if (offset < 0 && (uint32_t)(-offset) > proc->fds[fd].offset)
                return ERR(EINVAL);
            new_off = proc->fds[fd].offset + (uint32_t)offset;
            break;
        case 2: {
            /* SEEK_END — la dimensione si chiede al VFS, che la sa per
             * ogni filesystem montato. E' il posizionamento con cui ogni
             * programma misura un file prima di leggerlo, e finche'
             * rispondeva ENOSYS nessuna libc poteva offrire un ftell()
             * sulla fine. */
            VfsStat vs;
            int32_t r = vfs_fstat((int)proc->fds[fd].inode, &vs);

            if (r != 0) return r;
            if (offset < 0 && (uint32_t)(-offset) > vs.dimensione)
                return ERR(EINVAL);

            new_off = vs.dimensione + (uint32_t)offset;
            break;
        }
        default:
            return ERR(EINVAL);
    }

    proc->fds[fd].offset = new_off;
    return (int32_t)new_off;
}

/* =============================================================================
 * SYS_READDIR (141) -- Elenca il contenuto di una directory
 *
 * ebx = path*        (NULL, "" o "/" per la root; "/NOME" per una
 *                      subdirectory di un livello, es. "/bin")
 * ecx = DirEntry* buf (buffer utente, gia' allocato dal chiamante)
 * edx = max_entries  (capacita' del buffer, in numero di DirEntry)
 * esi = start        (indice della prima voce da restituire: permette di
 *                      percorrere a blocchi una directory piu' grande del
 *                      buffer)
 *
 * PAGINAZIONE AGGIUNTA (luglio 2026). Prima la syscall restituiva sempre
 * e solo le PRIME voci, con un tetto interno di 64 indipendente da quanto
 * chiedeva il chiamante. Il troncamento era SILENZIOSO: /bin/ls mostrava
 * una directory incompleta senza dirlo, e /bin/delete cancellava solo i
 * file corrispondenti fra i primi 64 lasciando gli altri, facendo credere
 * di aver finito. Il tetto resta (protegge lo stack del kernel), ma ora il
 * chiamante puo' continuare da dove era arrivato.
 *
 * Ritorna il numero di entry scritte in buf (>= 0), o errno negativo.
 * Wrapper sottile su vfs_readdir(): legge in un buffer kernel intermedio
 * (mai esporre puntatori interni allo userspace), poi copia nel buffer
 * utente gia' verificato.
 *
 * PASSA DAL VFS (0.132): cosi' `ls /disk` elenca il disco montato, e
 * `ls /` mostra i punti di montaggio insieme ai file del floppy.
 * ============================================================================= */
#define READDIR_MAX_BATCH 16   /* limite di sicurezza, indipendente da
                                   quanto richiesto dal chiamante */

int32_t sys_readdir(InterruptFrame *frame)
{
    const char *path        = (const char *)frame->ebx;
    DirEntry   *user_buf    = (DirEntry *)frame->ecx;
    uint32_t    max_entries = frame->edx;
    uint32_t    start       = frame->esi;
    char        kpath[PERCORSO_MAX];
    VfsDirEntry ventries[READDIR_MAX_BATCH];
    uint32_t    fcount, cap, i;

    if (path && !syscall_verify_str(path, PERCORSO_MAX)) return ERR(EFAULT);
    if (max_entries == 0) return ERR(EINVAL);

    cap = max_entries;
    if (cap > READDIR_MAX_BATCH) cap = READDIR_MAX_BATCH;

    if (!syscall_verify_ptr(user_buf, sizeof(DirEntry) * cap)) return ERR(EFAULT);

    /* path == NULL significa "la directory corrente". Prima finiva a
     * fat12_readdir_path(NULL), che intendeva la root: con il VFS un
     * percorso vuoto non individuerebbe alcun montaggio, quindi la cwd
     * va resa esplicita qui. */
    if (path) {
        if (resolve_path(path, kpath, sizeof(kpath)) != 0) return ERR(EINVAL);
    } else {
        if (resolve_path(".", kpath, sizeof(kpath)) != 0) return ERR(EINVAL);
    }

    if (vfs_readdir(kpath, ventries, cap, &fcount, start) != 0) {
        klog(LOG_DEBUG, "SYSCALL readdir('%s'): non trovata o non e' una directory", kpath);
        return ERR(ENOENT);
    }

    for (i = 0; i < fcount; i++) {
        DirEntry de;
        kstrcpy(de.name, ventries[i].nome, DIRENT_NAME_MAX);
        de.size   = ventries[i].dimensione;
        /* L'identita' del file, che diventa d_ino nella readdir() della
         * libc. Vedi DirEntry in kernel/include/syscall.h per cosa costava
         * lo zero che c'era prima. */
        de.ident  = ventries[i].ident;
        de.is_dir = ventries[i].is_dir;
        user_buf[i] = de;
    }

    klog(LOG_DEBUG, "SYSCALL readdir('%s', da %u): %u entry",
         kpath, start, fcount);
    return (int32_t)fcount;
}

/* =============================================================================
 * SYS_GETENV (184) -- Legge una variabile della sezione [env] di kernel.cfg
 *
 * ebx = key*   (nome della variabile, stringa null-terminated)
 * ecx = buf*   (buffer utente dove copiare il valore)
 * edx = size   (dimensione del buffer)
 *
 * Ritorna la lunghezza del valore copiato (>=0), -ENOENT se la variabile
 * non esiste, -EINVAL se il buffer è troppo piccolo.
 *
 * PERCHÉ ESISTE (luglio 2026): la sezione [env] di /boot/kernel.cfg
 * veniva letta, memorizzata e perfino stampata nel log di boot, ma
 * cfg_getenv() non aveva UN SOLO chiamante in tutto il progetto. I
 * processi utente non avevano alcun modo di raggiungere quei valori, e
 * la shell si limitava a ri-hardcodare le stesse coppie chiave/valore in
 * env_init() — due copie della stessa verità, destinate a divergere al
 * primo che avesse modificato kernel.cfg aspettandosi un effetto.
 *
 * Il valore viene copiato in un buffer utente, mai esposto come
 * puntatore interno: stessa regola già seguita da sys_getcwd e
 * sys_readdir.
 * ============================================================================= */
int32_t sys_getenv(InterruptFrame *frame)
{
    const char *key  = (const char *)frame->ebx;
    char       *buf  = (char *)frame->ecx;
    uint32_t    size = frame->edx;
    const char *val;
    uint32_t    len;

    if (!syscall_verify_str(key, CFG_NAME_LEN)) return ERR(EFAULT);
    if (!syscall_verify_ptr(buf, size))          return ERR(EFAULT);
    if (size == 0)                                return ERR(EINVAL);

    val = cfg_getenv(key);
    if (val == NULL) val = cfg_get_option(key);   /* opzioni fuori da [env] */
    if (val == NULL) return ERR(ENOENT);

    len = kstrlen(val);
    if (len + 1 > size) return ERR(EINVAL);

    kstrcpy(buf, val, size);
    return (int32_t)len;
}

/* =============================================================================
 * SYS_MKDIR (39) -- Crea una directory
 *
 * ebx = path*  (assoluto o relativo alla directory corrente)
 *
 * Ritorna 0, -EEXIST se esiste già, -ENOSYS se il percorso richiederebbe
 * più di un livello di annidamento (limite del driver FAT12, vedi
 * fat12_mkdir), o un altro errore negativo.
 *
 * Il secondo argomento POSIX (mode) non esiste: FAT12 non ha permessi.
 * ============================================================================= */
int32_t sys_mkdir(InterruptFrame *frame)
{
    const char *path = (const char *)frame->ebx;
    char        abs[PERCORSO_MAX];
    int         r;

    if (!syscall_verify_str(path, PERCORSO_MAX)) return ERR(EFAULT);
    if (resolve_path(path, abs, sizeof(abs)) != 0) return ERR(EINVAL);

    r = vfs_mkdir(abs);
    klog(LOG_INFO, "SYSCALL mkdir('%s') -> %d", abs, r);

    /* fat12_mkdir usa già la convenzione degli errno negativi. */
    return (int32_t)r;
}

/* =============================================================================
 * SYS_RMDIR (40) -- Cancella una directory vuota
 *
 * ebx = path*  (assoluto o relativo alla directory corrente)
 *
 * Ritorna 0, oppure: -ENOENT non trovata, -ENOTDIR non è una directory,
 * -ENOTEMPTY se contiene qualcosa, -ENOSYS per percorsi annidati.
 * ============================================================================= */
int32_t sys_rmdir(InterruptFrame *frame)
{
    const char *path = (const char *)frame->ebx;
    char        abs[PERCORSO_MAX];
    int         r;

    if (!syscall_verify_str(path, PERCORSO_MAX)) return ERR(EFAULT);
    if (resolve_path(path, abs, sizeof(abs)) != 0) return ERR(EINVAL);

    r = vfs_rmdir(abs);
    klog(LOG_INFO, "SYSCALL rmdir('%s') -> %d", abs, r);
    return (int32_t)r;
}

/* =============================================================================
 * SYS_UNLINK (10) -- Cancella un file
 *
 * ebx = path*  (assoluto o relativo alla directory corrente)
 *
 * Ritorna 0, -ENOENT se non esiste, -EISDIR se è una directory (per
 * quelle c'è rmdir). Nessuna espansione di caratteri jolly qui: il
 * kernel cancella un nome preciso, l'espansione la fa /bin/delete
 * elencando la directory — così il programma può anche dire all'utente
 * quali file sta per togliere.
 * ============================================================================= */
int32_t sys_unlink(InterruptFrame *frame)
{
    const char *path = (const char *)frame->ebx;
    char        abs[PERCORSO_MAX];
    int         r;

    if (!syscall_verify_str(path, PERCORSO_MAX)) return ERR(EFAULT);
    if (resolve_path(path, abs, sizeof(abs)) != 0) return ERR(EINVAL);

    r = vfs_unlink(abs);
    klog(LOG_INFO, "SYSCALL unlink('%s') -> %d", abs, r);
    return (int32_t)r;
}

/* =============================================================================
 * SYS_RENAME (38) -- rinomina SENZA spostare i dati
 *
 * ebx = vecchio percorso, ecx = nuovo percorso.
 *
 * ! NON E' LA rename() DI POSIX, e le due differenze vanno sapute:
 *   - solo NELLA STESSA DIRECTORY e nello stesso montaggio (ENOSYS);
 *   - NON sostituisce la destinazione (EEXIST).
 *
 * Vedi vfs_rename in kernel/fs/vfs.c per il perche' di entrambe. La
 * garanzia che invece si da', e che serve a `install`, e' che i blocchi
 * del file non si spostano: una mappa dei settori calcolata prima della
 * rinomina vale ancora dopo.
 * ============================================================================= */
int32_t sys_rename(InterruptFrame *frame)
{
    const char *u_da = (const char *)frame->ebx;
    const char *u_a  = (const char *)frame->ecx;
    char        abs_da[PERCORSO_MAX], abs_a[PERCORSO_MAX];
    int         r;

    if (!syscall_verify_str(u_da, PERCORSO_MAX)) return ERR(EFAULT);
    if (!syscall_verify_str(u_a,  PERCORSO_MAX)) return ERR(EFAULT);
    if (resolve_path(u_da, abs_da, sizeof(abs_da)) != 0) return ERR(EINVAL);
    if (resolve_path(u_a,  abs_a,  sizeof(abs_a))  != 0) return ERR(EINVAL);

    r = vfs_rename(abs_da, abs_a);
    klog(LOG_INFO, "SYSCALL rename('%s' -> '%s') -> %d", abs_da, abs_a, r);
    return (int32_t)r;
}

/* =============================================================================
 * SYS_VERSION (185) -- Copia l'identità del sistema in un buffer utente
 *
 * ebx = buf*   (buffer utente)
 * ecx = size   (dimensione del buffer)
 *
 * Ritorna la lunghezza della stringa copiata (>=0), -EINVAL se il buffer
 * è troppo piccolo (nessuna copia parziale: o tutto o niente, così il
 * chiamante non stampa mai un'identità troncata a metà frase).
 *
 * La sorgente è `g_os_version`, la variabile globale definita in
 * kernel/version.c — unica fonte di verità per nome, versione, autore e
 * licenza. La shell la espone con i comandi `ver` e `version`.
 * ============================================================================= */
int32_t sys_version(InterruptFrame *frame)
{
    char    *buf  = (char *)frame->ebx;
    uint32_t size = frame->ecx;
    uint32_t len;

    if (!syscall_verify_ptr(buf, size)) return ERR(EFAULT);
    if (size == 0)                       return ERR(EINVAL);

    len = kstrlen(g_os_version);
    if (len + 1 > size) return ERR(EINVAL);

    kstrcpy(buf, g_os_version, size);
    return (int32_t)len;
}

/* =============================================================================
 * SYS_UPTIME (186) -- Millisecondi trascorsi dall'avvio
 *
 * Nessun argomento. Ritorna i millisecondi come valore di ritorno.
 *
 * PERCHE' ESISTE (kernel 0.120). Prima di questa syscall un processo ring3
 * non aveva NESSUN modo di leggere l'ora: poteva solo dormire con
 * SYS_SLEEP. Un driver che deve dare una scadenza a un'attesa era quindi
 * costretto a scegliere fra due cose sbagliate:
 *
 *   - contare iterazioni, che dipende dalla velocita' della CPU — il
 *     difetto gia' corretto due volte in questo progetto (i loop di NOP
 *     dell'FDC a giugno, KBC_POLL_MAX in drivers/kbd/kbd.c);
 *   - dormire a passi, che non puo' avere granularita' migliore di un
 *     tick (10 ms a 100Hz) e che al primo tentativo mi e' costato un
 *     kbd_hw_init() da oltre 40 secondi, vedi HANDOFF sessione (d).
 *
 * Con questa syscall si puo' fare la cosa giusta: leggere l'ora, poi
 * ciclare finche' non e' passato il tempo dichiarato, cedendo la CPU nel
 * frattempo. La scadenza e' reale e la granularita' del POLLING non e'
 * piu' legata a quella del temporizzatore.
 *
 * La sorgente e' g_ticks (PIT a 100Hz, kernel/sched/sched.c), quindi la
 * risoluzione resta di 10 ms: il valore avanza a scatti di 10. E' l'ora
 * vera, non una stima — e' esattamente cio' che serviva.
 *
 * ATTENZIONE ALL'ARITMETICA NEL CHIAMANTE: il ritorno e' int32_t e i
 * millisecondi tornano a zero dopo ~24,8 giorni di uptime. Un chiamante
 * deve confrontare DIFFERENZE fra letture in aritmetica senza segno
 * (`(unsigned)(ora - inizio) >= timeout`), che attraversa il wrap
 * correttamente, e non valori assoluti.
 * ============================================================================= */
int32_t sys_uptime(InterruptFrame *frame)
{
    (void)frame;
    return (int32_t)(g_ticks * 10u);
}

/* =============================================================================
 * SYS_MEMINFO (187) -- Stato della memoria fisica, per fascia
 *
 * ebx = puntatore a MemInfo (utente)
 * ecx = sizeof(MemInfo) visto dal chiamante
 *
 * Il chiamante passa la propria sizeof e il kernel rifiuta se non
 * coincide. La struttura e' duplicata a mano fra kernel/include/syscall.h
 * e lib/include/libc.h — convenzione gia' usata altrove nel progetto
 * (DirEntry, i numeri di syscall in bin/sh/shell.c) — e senza questo
 * controllo una desincronizzazione fra le due copie si manifesterebbe
 * come numeri sbagliati invece che come un errore.
 *
 * Si compila su un MemInfo locale e si copia in fondo: se la verifica del
 * puntatore fallisse a meta' compilazione, il chiamante resterebbe con una
 * struttura riempita a meta'.
 * ============================================================================= */
int32_t sys_meminfo(InterruptFrame *frame)
{
    MemInfo  *dst  = (MemInfo *)frame->ebx;
    uint32_t  size = frame->ecx;
    MemInfo   mi;
    uint32_t  tot, libere;
    uint32_t  ext_pagine, ext_max;

    if (size != sizeof(MemInfo))          return ERR(EINVAL);
    if (!syscall_verify_ptr(dst, size))   return ERR(EFAULT);

    /* Convenzionale: 0x00000-0x9FFFF (640 KB) */
    pmm_region_stat(0x00000000, 0xA0000, &tot, &libere);
    mi.conv_total_kb = tot * (PAGE_SIZE / 1024);
    mi.conv_free_kb  = libere * (PAGE_SIZE / 1024);

    /* Area superiore: 0xA0000-0xFFFFF (384 KB) */
    pmm_region_stat(0x000A0000, 0x60000, &tot, &libere);
    mi.uma_total_kb = tot * (PAGE_SIZE / 1024);
    mi.uma_free_kb  = libere * (PAGE_SIZE / 1024);

    /* Estesa: da 1 MB fino alla fine della RAM.
     * La lunghezza si calcola in pagine e si limita per non far
     * traboccare base+len su una macchina con 4 GB. */
    ext_pagine = pmm_get_total_pages();
    ext_pagine = (ext_pagine > 256) ? ext_pagine - 256 : 0;   /* 1 MB = 256 pagine */
    ext_max    = (0xFFFFFFFFu - 0x00100000u) / PAGE_SIZE;
    if (ext_pagine > ext_max) ext_pagine = ext_max;

    pmm_region_stat(0x00100000, ext_pagine * PAGE_SIZE, &tot, &libere);
    mi.ext_total_kb = tot * (PAGE_SIZE / 1024);
    mi.ext_free_kb  = libere * (PAGE_SIZE / 1024);

    /* Espansa: non esiste su questo sistema, e non e' una lacuna.
     * Vedi il commento su MemInfo in kernel/include/syscall.h. */
    mi.ems_total_kb = 0;
    mi.ems_free_kb  = 0;

    mi.total_kb  = pmm_get_total_pages() * (PAGE_SIZE / 1024);
    mi.free_kb   = pmm_get_free_pages()  * (PAGE_SIZE / 1024);
    mi.page_size = PAGE_SIZE;

    *dst = mi;
    return 0;
}

/* =============================================================================
 * SYS_PROCINFO (188) -- Elenca i processi vivi e i loro stack
 *
 * ebx = ProcInfo* (buffer utente)
 * ecx = numero massimo di voci richieste
 * edx = indice della prima voce da restituire (paginazione)
 * esi = sizeof(ProcInfo) visto dal chiamante
 *
 * Ritorna: numero di voci scritte (0 = finito), o errore negativo.
 *
 * La paginazione e' quella gia' usata da sys_readdir, e per lo stesso
 * motivo: il tetto per singola chiamata (PROCINFO_MAX_BATCH) protegge lo
 * stack del kernel, ma non deve diventare un tetto sul TOTALE — quello fu
 * il difetto silenzioso di ls e delete corretto nella sessione (l), dove
 * un elenco piu' lungo del batch veniva troncato senza dirlo.
 *
 * L'indice conta solo gli slot NON liberi, cosi' il chiamante puo'
 * percorrere il pool a blocchi senza sapere nulla di come e' fatto.
 *
 * Gli slot UNUSED sono saltati; gli ZOMBIE no — un processo terminato e
 * non ancora raccolto occupa comunque risorse, ed e' proprio quello che
 * un elenco del genere serve a far vedere.
 * ============================================================================= */
int32_t sys_procinfo(InterruptFrame *frame)
{
    ProcInfo *user_buf = (ProcInfo *)frame->ebx;
    uint32_t  max      = frame->ecx;
    uint32_t  start    = frame->edx;
    uint32_t  size     = frame->esi;

    ProcInfo  batch[PROCINFO_MAX_BATCH];
    uint32_t  cap, i, visti = 0, scritti = 0;

    if (size != sizeof(ProcInfo)) return ERR(EINVAL);
    if (max == 0)                 return ERR(EINVAL);

    cap = max;
    if (cap > PROCINFO_MAX_BATCH) cap = PROCINFO_MAX_BATCH;

    if (!syscall_verify_ptr(user_buf, sizeof(ProcInfo) * cap)) return ERR(EFAULT);

    for (i = 0; i < MAX_PROCESSES && scritti < cap; i++) {
        Process  *p = &g_process_pool[i];
        ProcInfo *o;
        uint32_t  n;

        if (p->state == PROC_UNUSED) continue;

        if (visti++ < start) continue;   /* gia' consegnato in un blocco precedente */

        o = &batch[scritti];
        o->pid   = p->pid;
        o->ppid  = p->ppid;
        o->state = (uint32_t)p->state;
        o->prio  = p->priority;

        for (n = 0; n < PROCINFO_NAME_MAX - 1 && p->name[n]; n++) {
            o->name[n] = p->name[n];
        }
        o->name[n] = '\0';

        o->ustack_top   = p->user_stack_top;
        o->ustack_base  = p->user_stack_base;
        o->ustack_limit = p->user_stack_limit;
        o->kstack_base  = p->kernel_stack_base;
        o->kstack_top   = p->kernel_stack_top;

        scritti++;
    }

    for (i = 0; i < scritti; i++) user_buf[i] = batch[i];

    return (int32_t)scritti;
}

/* =============================================================================
 * SYS_SU (254) -- diventare root provando di sapere una password
 *
 * Il perche' sta in kernel/include/syscall.h. Qui c'e' solo come si verifica.
 *
 * ! IL FORMATO DEI DUE FILE E' RIPETUTO QUI, e non si puo' evitare: lo stesso
 * codice sta in lib/exuser, ma quello gira in ring 3 e questo no. Le due copie
 * devono restare d'accordo — nome:uid:gid in /boot/utenti, nome:sale:impronta
 * in /boot/ombra — e chi cambia il formato deve cambiare tutt'e due.
 * ============================================================================= */
#define SU_FILE_MAX   8192

/* Un intero senza segno da una stringa. Il kernel non ha atoi. */
static uint32_t su_numero(const char *s)
{
    uint32_t v = 0;

    while (*s >= '0' && *s <= '9') { v = v * 10u + (uint32_t)(*s - '0'); s++; }
    return v;
}

/* Legge un file intero in `buf`. Rende i byte letti, <0 se non si apre. */
static int32_t su_leggi(const char *percorso, char *buf, uint32_t max)
{
    int h = vfs_open_autorita(percorso, 0);
    int n;

    if (h < 0) return -1;
    n = vfs_read(h, buf, max - 1, 0);
    vfs_close(h);
    if (n < 0) n = 0;
    buf[n] = '\0';
    return n;
}

/* Il campo `quale` (0,1,2) della riga che comincia col nome dato. Rende 1 se
 * l'ha trovato. */
static int su_campo(const char *testo, int n, const char *nome, int quale,
                    char *out, uint32_t max)
{
    int i = 0;

    while (i < n) {
        int p = i, c[3], nc = 0, k, l;

        while (i < n && testo[i] != '\n') i++;
        l = i - p;
        if (i < n) i++;
        if (l <= 0 || testo[p] == '#') continue;

        for (k = 0; k < l && nc < 3; k++)
            if (testo[p + k] == ':') c[nc++] = k;
        if (nc < 2) continue;

        /* il nome e' il campo 0 */
        for (k = 0; k < c[0] && nome[k]; k++)
            if (testo[p + k] != nome[k]) break;
        if (k != c[0] || nome[k] != '\0') continue;

        {
            int da = (quale == 0) ? 0 : c[quale - 1] + 1;
            int a  = (quale >= nc) ? l : c[quale];
            uint32_t q = 0;

            if (quale == 0) a = c[0];
            for (k = da; k < a && q + 1 < max; k++) out[q++] = testo[p + k];
            out[q] = '\0';
            return 1;
        }
    }
    return 0;
}

/* 1 se il nome sta in /boot/amministratori, una riga per nome. */
static int su_amministratore(const char *nome)
{
    static char testo[1024];
    int n = su_leggi("/boot/amministratori", testo, sizeof(testo));
    int i = 0;

    if (n <= 0) return 0;

    while (i < n) {
        int p = i, l, k;

        while (i < n && testo[i] != '\n') i++;
        l = i - p;
        if (i < n) i++;
        while (l > 0 && (testo[p + l - 1] == '\r' || testo[p + l - 1] == ' ')) l--;
        if (l <= 0 || testo[p] == '#') continue;

        for (k = 0; k < l && nome[k]; k++)
            if (testo[p + k] != nome[k]) break;
        if (k == l && nome[k] == '\0') return 1;
    }
    return 0;
}

int32_t sys_su(InterruptFrame *frame)
{
    const char *nome = (const char *)frame->ebx;
    const char *pass = (const char *)frame->ecx;
    Process    *self = proc_get_current();

    static char testo[SU_FILE_MAX];
    char  sale[32], imp[80], misto[160], prova[65], su_[16], sg_[16];
    int   n;
    uint32_t i = 0, j;

    if (self == NULL) return ERR(ESRCH);
    if (!syscall_verify_str(nome, 64) || !syscall_verify_str(pass, 128))
        return ERR(EFAULT);

    /* --- chi e' costui: uid e gid dal file pubblico --------------------- */
    n = su_leggi("/boot/utenti", testo, sizeof(testo));
    if (n <= 0) return ERR(EPERM);
    if (!su_campo(testo, n, nome, 1, su_, sizeof(su_)) ||
        !su_campo(testo, n, nome, 2, sg_, sizeof(sg_))) return ERR(EPERM);

    /* --- l'impronta, dal file che solo il kernel e root leggono --------- */
    n = su_leggi("/boot/ombra", testo, sizeof(testo));
    if (n <= 0) return ERR(EPERM);
    if (!su_campo(testo, n, nome, 1, sale, sizeof(sale)) ||
        !su_campo(testo, n, nome, 2, imp,  sizeof(imp))) return ERR(EPERM);

    /* L'impronta e' SHA-256 di "sale:password", come la scrive lib/exuser. */
    for (j = 0; sale[j] && i < sizeof(misto) - 2; j++) misto[i++] = sale[j];
    misto[i++] = ':';
    for (j = 0; pass[j] && i < sizeof(misto) - 1; j++) misto[i++] = pass[j];
    misto[i] = '\0';
    sha256_esa(misto, i, prova);

    for (j = 0; j < 64; j++)
        if (prova[j] != imp[j]) {
            klog(LOG_WARN, "SYSCALL su: PID %u (uid %u) ha sbagliato la "
                 "password di '%s'", self->pid, self->uid, nome);
            return ERR(EPERM);
        }

    /* --- puo' fare root? ------------------------------------------------ */
    {
        uint32_t uid = su_numero(su_);
        uint32_t gid = su_numero(sg_);

        if (uid != 0 && !su_amministratore(nome)) {
            klog(LOG_WARN, "SYSCALL su: '%s' ha dato la password giusta ma non "
                 "e' amministratore (PID %u)", nome, self->pid);
            return ERR(EPERM);
        }

        self->uid = 0;
        self->gid = 0;
        (void)uid; (void)gid;

        klog(LOG_INFO, "SYSCALL su: PID %u e' diventato root provando di essere "
             "'%s'", self->pid, nome);
    }
    return 0;
}

/* =============================================================================
 * SYS_STATPERM (253) -- proprietario e permessi di un file
 *
 * Il perche' di una chiamata a parte sta in kernel/include/syscall.h.
 * ============================================================================= */
int32_t sys_statperm(InterruptFrame *frame)
{
    const char *path = (const char *)frame->ebx;
    StatPerm   *out  = (StatPerm *)frame->ecx;
    uint32_t    size = frame->edx;
    char        abs[PERCORSO_MAX];
    VfsStat     vs;
    int32_t     rc;

    /* ! LA MISURA SI CONTROLLA, come in procinfo: e' cosi' che ci si accorge
     * di una copia della struttura andata fuori sincrono fra kernel e libc,
     * invece di scriverne un pezzo a caso nello spazio utente. */
    if (size != sizeof(StatPerm))                   return ERR(EINVAL);
    if (!syscall_verify_str(path, PERCORSO_MAX))    return ERR(EFAULT);
    if (!syscall_verify_ptr(out, sizeof(StatPerm))) return ERR(EFAULT);

    if (resolve_path(path, abs, sizeof(abs)) != 0) return ERR(EINVAL);

    rc = vfs_stat(abs, &vs);
    if (rc != 0) return rc;

    out->modo = vs.modo;
    out->uid  = vs.uid;
    out->gid  = vs.gid;
    return 0;
}

/* =============================================================================
 * SYS_DISKINFO (189) -- Un disco fisico e la sua tabella delle partizioni
 *
 * ebx = indice unita' ATA (0..3: primario master/slave, secondario m/s)
 * ecx = DiskInfo* (buffer utente)
 * edx = sizeof(DiskInfo) visto dal chiamante
 *
 * Ritorna 0, o errore negativo. Uno slot vuoto NON e' un errore: ritorna 0
 * con presente = 0, cosi' il chiamante puo' scorrere i quattro slot senza
 * distinguere "non c'e' niente" da "la chiamata e' fallita".
 *
 * La tabella delle partizioni viene riletta dal disco a ogni chiamata
 * invece che tenuta in cache. E' una scelta: una cache di qualcosa che un
 * altro programma puo' aver appena riscritto e' un modo perfetto per
 * mostrare una mappa del disco che non esiste piu'. Costa una lettura di
 * un settore, e per un comando interattivo non e' un costo.
 * ============================================================================= */
int32_t sys_diskinfo(InterruptFrame *frame)
{
    uint32_t          idx  = frame->ebx;
    DiskInfo         *dst  = (DiskInfo *)frame->ecx;
    uint32_t          size = frame->edx;
    const AtaDevice  *d;
    TabellaPartizioni tab;
    DiskInfo          di;
    uint32_t          i, n;

    if (size != sizeof(DiskInfo))       return ERR(EINVAL);
    if (idx >= ATA_MAX_DEVICES)         return ERR(EINVAL);
    if (!syscall_verify_ptr(dst, size)) return ERR(EFAULT);

    /* Si compila una copia locale e la si consegna alla fine: una
     * struttura utente riempita a meta' in caso di errore sarebbe peggio
     * di nessuna struttura. */
    {
        uint8_t *z = (uint8_t *)&di;
        uint32_t k = sizeof(di);
        while (k--) *z++ = 0;
    }

    d = ata_get_device((int)idx);
    if (d == NULL || !d->presente) {
        *dst = di;              /* presente = 0 */
        return 0;
    }

    di.presente = 1;
    di.tipo     = d->tipo;
    di.canale   = d->canale;
    di.unita    = d->unita;
    di.lba48    = d->lba48;
    di.hpa      = d->hpa;
    di.clippato = d->clippato;

    di.settori_lo = (uint32_t)(d->settori & 0xFFFFFFFFu);
    di.settori_hi = (uint32_t)(d->settori >> 32);
    di.nativi_lo  = (uint32_t)(d->settori_nativi & 0xFFFFFFFFu);
    di.nativi_hi  = (uint32_t)(d->settori_nativi >> 32);

    kstrcpy(di.modello,  d->modello,  sizeof(di.modello));
    kstrcpy(di.seriale,  d->seriale,  sizeof(di.seriale));
    kstrcpy(di.firmware, d->firmware, sizeof(di.firmware));

    /* Le partizioni hanno senso solo su un disco vero: su un ATAPI non si
     * legge il settore 0 con i comandi ATA. */
    if (d->tipo == ATA_TYPE_ATA && mbr_leggi((int)idx, &tab) == 0) {
        di.schema   = (uint32_t)tab.schema;
        di.problemi = tab.problemi;

        n = (uint32_t)tab.n;
        if (n > DISKINFO_MAX_PART) n = DISKINFO_MAX_PART;
        di.n_part = n;

        for (i = 0; i < n; i++) {
            di.part[i].attiva     = tab.p[i].attiva;
            di.part[i].tipo       = tab.p[i].tipo;
            di.part[i].logica     = tab.p[i].logica;
            di.part[i].inizio_lo  = (uint32_t)(tab.p[i].inizio & 0xFFFFFFFFu);
            di.part[i].inizio_hi  = (uint32_t)(tab.p[i].inizio >> 32);
            di.part[i].settori_lo = (uint32_t)(tab.p[i].settori & 0xFFFFFFFFu);
            di.part[i].settori_hi = (uint32_t)(tab.p[i].settori >> 32);

            /* Il filesystem si guarda dentro la partizione, non si deduce
             * dal byte di tipo. Le partizioni ESTESE sono contenitori e
             * non hanno un BPB: sondarle darebbe risultati casuali. */
            di.part[i].numero = tab.p[i].numero;

            if (tab.p[i].tipo != 0x05 && tab.p[i].tipo != 0x0F &&
                tab.p[i].tipo != 0x85) {
                VolumeInfo vi;
                /* Si passa dal DISPOSITIVO A BLOCCHI, non dagli LBA
                 * assoluti: cosi' il riconoscimento del filesystem usa
                 * esattamente la stessa finestra che user
                 * il driver FAT quando montera' quella partizione. Se la
                 * finestra fosse sbagliata, qui si vedrebbe subito. */
                int bd = blk_per_partizione((int)idx, tab.p[i].numero);
                if (bd >= 0 && (vol_identifica(bd, &vi) == 0 ||
                                vi.tipo == VOL_FS_ILLEGGIBILE)) {
                    di.part[i].fs_tipo         = vi.tipo;
                    di.part[i].fs_incoerente   = vi.incoerente;
                    di.part[i].fs_sett_per_clu = vi.sett_per_clu;
                    di.part[i].fs_n_cluster    = vi.n_cluster;
                    kstrcpy(di.part[i].fs_etichetta, vi.etichetta,
                            sizeof(di.part[i].fs_etichetta));
                }
            }
        }
    }

    *dst = di;
    return 0;
}

/* =============================================================================
 * SYS_BLKINFO (190) -- Elenca i dispositivi a blocchi
 *
 * ebx = BlkInfo* (buffer utente)   ecx = max voci
 * edx = indice di partenza          esi = sizeof(BlkInfo)
 *
 * Ritorna quante voci ha scritto, 0 quando sono finite.
 * ============================================================================= */
int32_t sys_blkinfo(InterruptFrame *frame)
{
    BlkInfo  *user_buf = (BlkInfo *)frame->ebx;
    uint32_t  max      = frame->ecx;
    uint32_t  start    = frame->edx;
    uint32_t  size     = frame->esi;
    BlkInfo   batch[8];
    uint32_t  cap, i, scritti = 0;
    int       tot;

    if (size != sizeof(BlkInfo)) return ERR(EINVAL);
    if (max == 0)                return ERR(EINVAL);

    cap = max;
    if (cap > 8) cap = 8;

    if (!syscall_verify_ptr(user_buf, sizeof(BlkInfo) * cap)) return ERR(EFAULT);

    tot = blk_conta();

    for (i = start; i < (uint32_t)tot && scritti < cap; i++) {
        const BlkDev *d = blk_get((int)i);
        BlkInfo      *o;
        uint32_t      k;

        if (d == NULL || !d->usato) continue;

        o = &batch[scritti];
        for (k = 0; k < BLKINFO_NOME_MAX - 1 && d->nome[k]; k++) o->nome[k] = d->nome[k];
        o->nome[k]      = 0;
        o->tipo         = d->tipo;
        o->sola_lettura = d->sola_lettura;
        o->primo_lo     = (uint32_t)(d->primo & 0xFFFFFFFFu);
        o->primo_hi     = (uint32_t)(d->primo >> 32);
        o->settori_lo   = (uint32_t)(d->settori & 0xFFFFFFFFu);
        o->settori_hi   = (uint32_t)(d->settori >> 32);
        scritti++;
    }

    for (i = 0; i < scritti; i++) user_buf[i] = batch[i];
    return (int32_t)scritti;
}

/* =============================================================================
 * SYS_MOUNT (191) -- Monta un dispositivo a blocchi su un punto
 *
 * ebx = dev*    ("hd0p1")
 * ecx = punto*  ("/disk", assoluto)
 * edx = flag    (bit 0 = monta in sola lettura)
 *
 * Ritorna 0, o un errno negativo. I controlli veri (esistenza del
 * dispositivo, filesystem riconoscibile, punto libero) stanno in
 * vfs_mount(): qui si verificano solo i puntatori utente.
 * ============================================================================= */
int32_t sys_mount(InterruptFrame *frame)
{
    if (!solo_root("mount")) return ERR(EPERM);

    const char *dev   = (const char *)frame->ebx;
    const char *punto = (const char *)frame->ecx;
    uint32_t    flag  = frame->edx;
    char kdev[BLKINFO_NOME_MAX], kpunto[MOUNTINFO_PUNTO_MAX];

    if (!syscall_verify_str(dev,   BLKINFO_NOME_MAX))   return ERR(EFAULT);
    if (!syscall_verify_str(punto, MOUNTINFO_PUNTO_MAX)) return ERR(EFAULT);

    /* Copiati in memoria kernel PRIMA dell'uso: le stringhe utente
     * restano scrivibili dal processo, e usarle direttamente
     * significherebbe che il montaggio decide su un valore che puo'
     * cambiare fra il controllo e l'uso. */
    kstrcpy(kdev,   dev,   sizeof(kdev));
    kstrcpy(kpunto, punto, sizeof(kpunto));

    klog(LOG_INFO, "SYSCALL mount('%s', '%s')%s", kdev, kpunto,
         (flag & MNT_SOLA_LETTURA) ? " sola lettura" : "");
    return vfs_mount(kdev, kpunto, (flag & MNT_SOLA_LETTURA) ? 1 : 0);
}

/* =============================================================================
 * SYS_UMOUNT (192) -- Smonta un punto di montaggio
 *
 * ebx = punto*
 * ============================================================================= */
int32_t sys_umount(InterruptFrame *frame)
{
    if (!solo_root("umount")) return ERR(EPERM);

    const char *punto = (const char *)frame->ebx;
    char kpunto[MOUNTINFO_PUNTO_MAX];

    if (!syscall_verify_str(punto, MOUNTINFO_PUNTO_MAX)) return ERR(EFAULT);
    kstrcpy(kpunto, punto, sizeof(kpunto));

    klog(LOG_INFO, "SYSCALL umount('%s')", kpunto);
    return vfs_umount(kpunto);
}

/* =============================================================================
 * SYS_MOUNTINFO (193) -- Elenca i montaggi attivi
 *
 * ebx = buf*, ecx = max, edx = start, esi = sizeof(MountInfo)
 * ============================================================================= */
int32_t sys_mountinfo(InterruptFrame *frame)
{
    MountInfo *user_buf = (MountInfo *)frame->ebx;
    uint32_t   max      = frame->ecx;
    uint32_t   start    = frame->edx;
    uint32_t   size     = frame->esi;
    MountInfo  batch[4];
    uint32_t   cap, i, scritti = 0;
    int        tot;

    if (size != sizeof(MountInfo)) return ERR(EINVAL);
    if (max == 0)                  return ERR(EINVAL);

    cap = max;
    if (cap > 4) cap = 4;

    if (!syscall_verify_ptr(user_buf, sizeof(MountInfo) * cap)) return ERR(EFAULT);

    tot = vfs_conta();

    for (i = start; i < (uint32_t)tot && scritti < cap; i++) {
        const VfsMount *m = vfs_get((int)i);
        MountInfo      *o;

        if (m == NULL) continue;

        o = &batch[scritti];
        kstrcpy(o->punto, m->punto, MOUNTINFO_PUNTO_MAX);
        kstrcpy(o->dev,   m->dev,   BLKINFO_NOME_MAX);
        /* L'handle `mnt` appartiene al driver del montaggio: passarlo a
         * fat_tipo() quando il montaggio e' ext2 significa leggere la
         * tabella dei montaggi di fat.c a un indice che li' descrive
         * tutt'altro volume — o niente. Il tipo va scelto PRIMA di
         * chiamare chiunque. */
        if      (m->tipo == VFS_FS_FAT12FD) o->fs = 12;
        else if (m->tipo == VFS_FS_EXT2)    o->fs = 2;
        else if (m->tipo == VFS_FS_ISO)     o->fs = VOL_FS_ISO9660;
        else                                o->fs = (uint32_t)fat_tipo(m->mnt);
        o->sola_lettura = m->sola_lettura;
        scritti++;
    }

    for (i = 0; i < scritti; i++) user_buf[i] = batch[i];
    return (int32_t)scritti;
}

/* =============================================================================
 * SYS_BOOTINSTALL (194) -- Installa MBR + settore di avvio su un disco
 *
 * ebx = punto*  (punto di montaggio gia' attivo, es. "/disk")
 * ecx = info*   (BootInstallInfo, riempita con cio' che e' stato scritto)
 * edx = sizeof(BootInstallInfo)
 *
 * Qui si verificano solo i puntatori utente: le due invarianti che
 * contano — tabella delle partizioni e BPB intatti — sono garantite da
 * boot_installa(), che e' l'unico posto in cui esistono. Vedi
 * kernel/boot/bootinst.c per il perche' non stiano in userspace.
 * ============================================================================= */
int32_t sys_bootinstall(InterruptFrame *frame)
{
    if (!solo_root("bootinstall")) return ERR(EPERM);

    const char      *punto = (const char *)frame->ebx;
    BootInstallInfo *uinfo = (BootInstallInfo *)frame->ecx;
    uint32_t         size  = frame->edx;
    char             kpunto[MOUNTINFO_PUNTO_MAX];
    BootInstEsito    e;
    int32_t          r;

    if (size != sizeof(BootInstallInfo)) return ERR(EINVAL);
    if (!syscall_verify_str(punto, MOUNTINFO_PUNTO_MAX)) return ERR(EFAULT);
    if (!syscall_verify_ptr(uinfo, sizeof(BootInstallInfo))) return ERR(EFAULT);

    kstrcpy(kpunto, punto, sizeof(kpunto));

    /* ! ESI PORTA LA MODALITA', e vale 0 per chi non lo imposta.
     *
     * Il quarto registro invece di un numero di syscall nuovo: la
     * struttura in uscita e il percorso in ingresso sono gli stessi, e
     * cambia solo se si scrive o no. Un programma compilato per la forma
     * a tre argomenti lascia in ESI un valore qualunque — per questo si
     * accetta SOLO il valore 1 come "verifica", e qualunque altra cosa
     * vale "installa", che e' il comportamento di prima. */
    {
        int verifica = (frame->esi == BOOTINST_VERIFICA) ? 1 : 0;
        const char *n_s2 = NULL, *n_k = NULL;
        char k_s2[64], k_k[64];

        /* ecx+edx sono gia' presi: i nomi alternativi viaggiano in EDI
         * come una coppia di stringhe consecutive, o NULL per i nomi
         * predefiniti. */
        if (frame->edi != 0) {
            const char *u = (const char *)frame->edi;
            if (!syscall_verify_str(u, sizeof(k_s2))) return ERR(EFAULT);
            kstrcpy(k_s2, u, sizeof(k_s2));
            u += kstrlen(k_s2) + 1;
            if (!syscall_verify_str(u, sizeof(k_k))) return ERR(EFAULT);
            kstrcpy(k_k, u, sizeof(k_k));
            n_s2 = k_s2; n_k = k_k;
        }

        klog(LOG_INFO, "SYSCALL bootinstall('%s')%s", kpunto,
             verifica ? " [sola verifica]" : "");

        r = (int32_t)boot_installa_ex(kpunto, n_s2, n_k, verifica, &e);
    }
    if (r != 0) return r;

    uinfo->s2_lba = e.s2_lba; uinfo->s2_cnt = e.s2_cnt;
    uinfo->k_lba  = e.k_lba;  uinfo->k_cnt  = e.k_cnt;
    uinfo->k_next = e.k_next;
    uinfo->disco  = e.disco;  uinfo->voce   = e.voce;
    return 0;
}

/* =============================================================================
 * SYS_PARTWRITE (195) -- Riscrive la tabella delle partizioni di un disco
 *
 * ebx = indice del disco ATA (0..3)
 * ecx = tab*   (PartTabella: 4 voci in ingresso, `problemi` in uscita)
 * edx = sizeof(PartTabella)
 *
 * Questa funzione non decide NIENTE. Copia la proposta in memoria kernel,
 * la converte nel tipo interno e la consegna a blk_ripartiziona(), che e'
 * l'unico punto in cui esistono le garanzie: nessuna partizione in uso,
 * validazione identica a quella della lettura, i 446 byte del codice di
 * avvio intatti, rilettura dei dispositivi.
 *
 * PERCHE' LA COPIA IN MEMORIA KERNEL E' OBBLIGATORIA, e non una cortesia:
 * la struttura utente resta scrivibile dal processo per tutto il tempo
 * della chiamata. Validare i numeri direttamente li' e poi scriverli sul
 * disco significa validare un valore e scriverne un altro — la tabella
 * finirebbe sul disco senza essere mai passata da mbr_valida().
 *
 * PERMESSI: come sys_reboot e sys_bootinstall, qualunque processo puo'
 * chiamarla. Non esiste ancora un concetto di utente in EX-OS, quindi non
 * c'e' nulla su cui basare un controllo. Cio' che protegge il sistema qui
 * non e' un permesso ma il conteggio degli usi: il disco da cui EX-OS sta
 * girando ha la sua partizione acquisita dalla root, e viene rifiutato.
 * ============================================================================= */
int32_t sys_partwrite(InterruptFrame *frame)
{
    if (!solo_root("partwrite")) return ERR(EPERM);

    uint32_t     disco = frame->ebx;
    PartTabella *utab  = (PartTabella *)frame->ecx;
    uint32_t     size  = frame->edx;
    PartTabella  ktab;
    Partizione   voci[PARTWRITE_MAX_VOCI];
    uint32_t     problemi = 0;
    int32_t      r;
    int          i;

    if (size != sizeof(PartTabella))          return ERR(EINVAL);
    if (disco >= ATA_MAX_DEVICES)             return ERR(EINVAL);
    if (!syscall_verify_ptr(utab, size))      return ERR(EFAULT);

    ktab = *utab;               /* la copia: vedi sopra */

    for (i = 0; i < PARTWRITE_MAX_VOCI; i++) {
        const PartVoce *v = &ktab.voce[i];

        voci[i].attiva  = (uint8_t)v->attiva;
        voci[i].tipo    = (uint8_t)v->tipo;
        voci[i].logica  = 0;            /* da qui si scrivono solo primarie */
        voci[i].numero  = (uint8_t)(i + 1);
        voci[i].inizio  = ((uint64_t)v->inizio_hi  << 32) | v->inizio_lo;
        voci[i].settori = ((uint64_t)v->settori_hi << 32) | v->settori_lo;
    }

    klog(LOG_INFO, "SYSCALL partwrite(hd%u)", disco);

    r = (int32_t)blk_ripartiziona((int)disco, voci, PARTWRITE_MAX_VOCI,
                                  &problemi);

    /* Il dettaglio del rifiuto torna indietro anche — soprattutto —
     * quando la chiamata fallisce. */
    utab->problemi = problemi;
    return r;
}

/* =============================================================================
 * SYS_BLKREAD (196) / SYS_BLKWRITE (197) -- Settori grezzi di una partizione
 *
 * Le condizioni e il perche' stanno in kernel/include/syscall.h, sopra
 * BLKIO_MAX_SETT. Qui c'e' il controllo, in un punto solo per entrambe:
 * scriverlo due volte significherebbe che un giorno le due divergono, e la
 * lettura permette cio' che la scrittura nega o viceversa.
 *
 * Ritorna l'indice del dispositivo, o un errno negativo.
 * ============================================================================= */
static int32_t blkio_apri(const char *unome, uint32_t lba, uint32_t n,
                          const void *ubuf, int per_scrittura, int *out_dev)
{
    const BlkDev *b;
    char          knome[BLKINFO_NOME_MAX];
    int           dev;

    if (n == 0 || n > BLKIO_MAX_SETT)                     return ERR(EINVAL);
    if (!syscall_verify_str(unome, BLKINFO_NOME_MAX))     return ERR(EFAULT);
    if (!syscall_verify_ptr(ubuf, n * 512u))              return ERR(EFAULT);

    kstrcpy(knome, unome, sizeof(knome));

    dev = blk_trova(knome);
    if (dev < 0) return ERR(ENOENT);

    b = blk_get(dev);
    if (b == NULL) return ERR(ENODEV);

    /* Solo le partizioni. E' questa riga — non un permesso, non un
     * controllo sull'LBA — che rende la tabella delle partizioni
     * irraggiungibile da userspace: il settore 0 sta fuori da ogni
     * finestra di tipo PART, e i dispositivi che lo contengono non sono
     * nominabili qui. */
    if (b->tipo != BLK_TIPO_PART) {
        klog(LOG_ERROR, "BLKIO: '%s' non e' una partizione: rifiutato", knome);
        return ERR(EPERM);
    }

    /* Una partizione montata ha una cache write-back sopra: scriverci
     * sotto vuol dire che il primo sync la ricopre con i settori vecchi,
     * e leggerla da' dati che non sono quelli che il filesystem crede di
     * avere. In entrambi i casi il danno e' silenzioso. */
    if (blk_occupato(dev)) {
        klog(LOG_ERROR, "BLKIO: '%s' e' montato: accesso grezzo rifiutato", knome);
        return ERR(EBUSY);
    }

    if (per_scrittura && b->sola_lettura) return ERR(EROFS);

    /* Il limite della finestra lo fa rispettare blk_read/blk_write. Qui si
     * anticipa solo per restituire l'errore giusto invece di un -1. */
    if (lba + n < lba || lba + n > b->settori) return ERR(EINVAL);

    *out_dev = dev;
    return 0;
}

int32_t sys_blkread(InterruptFrame *frame)
{
    if (!solo_root("blkread")) return ERR(EPERM);

    const char *unome = (const char *)frame->ebx;
    uint32_t    lba   = frame->ecx;
    uint32_t    n     = frame->edx;
    uint8_t    *ubuf  = (uint8_t *)frame->esi;
    uint8_t     sett[512];
    int32_t     r;
    int         dev;
    uint32_t    k, i;

    r = blkio_apri(unome, lba, n, ubuf, 0, &dev);
    if (r != 0) return r;

    /* Un settore per volta: il buffer di rimbalzo resta 512 byte
     * qualunque sia n, quindi il costo sullo stack kernel non dipende da
     * cio' che chiede il processo. */
    for (k = 0; k < n; k++) {
        if (blk_read(dev, lba + k, 1, sett) != 0) {
            klog(LOG_ERROR, "BLKIO: lettura fallita a lba %u", lba + k);
            return (k > 0) ? (int32_t)k : ERR(EIO);
        }
        for (i = 0; i < 512; i++) ubuf[k * 512 + i] = sett[i];
    }

    return (int32_t)n;
}

int32_t sys_blkwrite(InterruptFrame *frame)
{
    if (!solo_root("blkwrite")) return ERR(EPERM);

    const char *unome = (const char *)frame->ebx;
    uint32_t    lba   = frame->ecx;
    uint32_t    n     = frame->edx;
    const uint8_t *ubuf = (const uint8_t *)frame->esi;
    uint8_t     sett[512];
    int32_t     r;
    int         dev;
    uint32_t    k, i;

    r = blkio_apri(unome, lba, n, ubuf, 1, &dev);
    if (r != 0) return r;

    for (k = 0; k < n; k++) {
        /* La copia in memoria kernel PRIMA della scrittura non e' una
         * cortesia: il buffer utente resta scrivibile dal processo per
         * tutta la durata della chiamata, e passarlo direttamente al
         * driver significherebbe scrivere su disco byte che possono
         * cambiare mentre li si scrive. */
        for (i = 0; i < 512; i++) sett[i] = ubuf[k * 512 + i];

        if (blk_write(dev, lba + k, 1, sett) != 0) {
            klog(LOG_ERROR, "BLKIO: scrittura fallita a lba %u", lba + k);
            return (k > 0) ? (int32_t)k : ERR(EIO);
        }
    }

    blk_flush(dev);
    return (int32_t)n;
}

/* =============================================================================
 * SYS_TRUNCATE (92) -- Cambia la dimensione di un file
 *
 * ebx = percorso*   ecx = nuova dimensione in byte
 *
 * Allungare non alloca niente: lo spazio in mezzo diventa un buco che si
 * legge come zeri. Accorciare libera i blocchi in coda.
 * ============================================================================= */
int32_t sys_truncate(InterruptFrame *frame)
{
    const char *path = (const char *)frame->ebx;
    uint32_t    dim  = frame->ecx;
    char        abs[PERCORSO_MAX];
    int         r;

    if (!syscall_verify_str(path, PERCORSO_MAX)) return ERR(EFAULT);
    if (resolve_path(path, abs, sizeof(abs)) != 0) return ERR(EINVAL);

    r = vfs_truncate(abs, dim);
    klog(LOG_INFO, "SYSCALL truncate('%s', %u) -> %d", abs, dim, r);
    return (int32_t)r;
}

/* =============================================================================
 * SYS_REBOOT (88) -- Spegne, riavvia o ferma il sistema
 *
 * ebx = comando: EXOS_RB_POWEROFF / EXOS_RB_RESTART / EXOS_RB_HALT
 *
 * Non ritorna mai (se non con errore su comando non valido).
 *
 * PERCHÉ SERVE UNA SYSCALL: prima la shell eseguiva `cli; hlt` per halt e
 * `inb`/`outb` sulla porta 0x64 per reboot, direttamente nel proprio
 * codice ring3. Sono istruzioni PRIVILEGIATE: in ring3 sollevano #GP, e
 * il #GP finiva nel ramo di default di isr_handler() producendo un
 * KERNEL PANIC. Entrambi i comandi erano quindi rotti allo stesso modo.
 * La sequenza di arresto ora sta nel kernel (kernel/arch/x86/power.c),
 * dove quelle istruzioni sono legali, e include la sincronizzazione del
 * filesystem — che dalla shell non era comunque possibile fare.
 *
 * =============================================================================
 * ! CHI PUO' SPEGNERE: ROOT SEMPRE, E CHI E' DAVANTI ALLA MACCHINA.
 *
 * Questa era `solo_root`, ed era la risposta sbagliata alla domanda giusta.
 * Spegnere non e' un privilegio amministrativo: e' il gesto di chi ha il
 * computer davanti, e su quella macchina c'e' comunque il pulsante
 * dell'alimentazione. Negarlo all'utente normale non protegge niente — lo
 * costringe a staccare la corrente, che e' il modo PEGGIORE di spegnere,
 * perche' salta la sincronizzazione del filesystem che questa syscall fa.
 *
 * ! MA UNA SESSIONE REMOTA NON SPEGNE LA MACCHINA DI QUALCUN ALTRO. Ed e' la
 * distinzione che conta davvero: chi entra via telnet non ha il pulsante, non
 * vede chi sta lavorando alla console, e spegnendo interrompe il lavoro di una
 * persona che non ha chiesto niente. E' la stessa regola dei sistemi veri, che
 * concedono l'arresto alle sessioni LOCALI e non a quelle di rete.
 *
 * ! IL CRITERIO E' IL DESCRITTORE 0, e non «da dove viene la connessione».
 * Una sessione remota — telnet, o un terminale dentro una finestra — legge da
 * uno pseudo-terminale; una console vera e' FD_STDIN. Chiedere al descrittore
 * invece che al protocollo vuol dire che il giorno che arriva ssh la regola
 * vale gia', senza che nessuno se ne debba ricordare.
 *
 * ! E root RESTA ROOT: da remoto un amministratore spegne, perche' quello si'
 * che e' amministrazione.
 * ============================================================================= */
static int puo_spegnere(void)
{
    Process *self = proc_get_current();

    if (self == NULL || self->uid == 0) return 1;      /* root, o il kernel */

    /* Una console vera: chi ha la macchina davanti. */
    if (self->fds[0].type == FD_STDIN) return 1;

    klog(LOG_WARN, "SYSCALL reboot: rifiutata al PID %u (uid %u): "
         "non e' root e non e' su una console di questa macchina",
         self->pid, self->uid);
    return 0;
}

int32_t sys_reboot(InterruptFrame *frame)
{
    if (!puo_spegnere()) return ERR(EPERM);

    uint32_t cmd  = frame->ebx;
    Process *self = proc_get_current();

    klog(LOG_INFO, "SYSCALL reboot(cmd=%u) richiesta da PID %u '%s'",
         cmd, self ? self->pid : 0, self ? self->name : "?");

    switch (cmd) {
        case EXOS_RB_POWEROFF: power_off();    /* non ritorna */
        case EXOS_RB_RESTART:  power_reboot(); /* non ritorna */
        case EXOS_RB_HALT:     power_halt();   /* non ritorna */
        default:
            return ERR(EINVAL);
    }
}

/* =============================================================================
 * SYS_IPC_SEND (220) -- Invia un messaggio a un altro processo
 *
 * ebx = dest_pid
 * ecx = type      (tipo applicativo del messaggio, libero per il chiamante)
 * edx = data*     (puntatore ai dati in userspace, può essere NULL se len=0)
 * esi = len       (byte da inviare, troncati a IPC_MSG_MAX_DATA)
 *
 * Il kernel copia i dati dal buffer utente a un buffer kernel prima di
 * qualunque operazione — mai un puntatore utente attraversa il confine
 * di processo. Ritorna 0 su successo, <0 su errore.
 * ============================================================================= */
int32_t sys_ipc_send(InterruptFrame *frame)
{
    uint32_t    dest_pid = frame->ebx;
    uint32_t    type     = frame->ecx;
    const void *data     = (const void *)frame->edx;
    uint32_t    len       = frame->esi;

    if (len > IPC_MSG_MAX_DATA) len = IPC_MSG_MAX_DATA;

    if (data != NULL && len > 0 && !syscall_verify_ptr(data, len)) {
        return ERR(EFAULT);
    }

    int32_t ret = ipc_send(dest_pid, type, data, len);
    klog(LOG_DEBUG, "SYSCALL ipc_send(dest=%u, type=%u, len=%u) -> %d",
         dest_pid, type, len, ret);
    return ret;
}

/* =============================================================================
 * SYS_IPC_RECV (221) -- Riceve il prossimo messaggio dalla propria mailbox
 *
 * ebx = out_meta*  (IpcMessage*: sender_pid/type/len — solo header, opzionale)
 * ecx = buf*       (buffer utente dove copiare il payload, opzionale)
 * edx = buf_len    (dimensione del buffer utente)
 *
 * Blocca finché non arriva un messaggio. Ritorna 0 su successo (mai <0
 * in questa implementazione, dato che il processo chiamante esiste per
 * definizione mentre esegue la propria syscall).
 * ============================================================================= */
int32_t sys_ipc_recv(InterruptFrame *frame)
{
    IpcMessage *out_meta = (IpcMessage *)frame->ebx;
    void       *buf      = (void *)frame->ecx;
    uint32_t    buf_len  = frame->edx;

    /* =========================================================================
     * ! SI VERIFICA E SI COPIA SOLO L'INTESTAZIONE, NON TUTTA LA STRUTTURA.
     *
     * Prima qui c'era `*out_meta = kmeta;`, che copiava nello spazio utente
     * l'intera IpcMessage — array data[] compreso — obbligando la copia
     * userspace della struttura a essere grande uguale. Erano 512 byte di
     * traffico a ogni ricezione per un campo che nessun chiamante legge:
     * il payload arriva nel buffer separato passato in ECX.
     *
     * Da quando il limite dei messaggi è salito a 1536 (un frame Ethernet
     * intero) quella copia sarebbe diventata di 1.5 KB, pagata da ogni
     * driver a ogni messaggio. Ora la IpcMessage dello spazio utente ha i
     * soli tre campi di intestazione, e la verifica del puntatore misura
     * quello che davvero scriviamo: verificare 1548 byte per scriverne 12
     * rifiuterebbe una struttura perfettamente valida vicino alla fine
     * dello spazio utente.
     * ========================================================================= */
    const uint32_t META_LEN = 3 * sizeof(uint32_t);

    if (out_meta != NULL && !syscall_verify_ptr(out_meta, META_LEN)) {
        return ERR(EFAULT);
    }
    if (buf != NULL && buf_len > 0 && !syscall_verify_ptr(buf, buf_len)) {
        return ERR(EFAULT);
    }

    IpcMessage kmeta;
    int32_t ret = ipc_recv(&kmeta, buf, buf_len);
    if (ret == 0 && out_meta != NULL) {
        out_meta->sender_pid = kmeta.sender_pid;
        out_meta->tipo       = kmeta.tipo;
        out_meta->len        = kmeta.len;
    }
    return ret;
}

/* =============================================================================
 * SYS_IPC_RECV_TMO (228) -- ipc_recv con scadenza
 *
 * ebx = IpcMessage* (uscita, opzionale)
 * ecx = buf*
 * edx = buf_len
 * esi = timeout in millisecondi (0 = attesa senza scadenza)
 *
 * Ritorna 0, -ETIMEDOUT se la scadenza è passata a mailbox vuota, o un
 * altro errno negativo.
 * ============================================================================= */
int32_t sys_ipc_recv_tmo(InterruptFrame *frame)
{
    IpcMessage *out_meta   = (IpcMessage *)frame->ebx;
    void       *buf        = (void *)frame->ecx;
    uint32_t    buf_len    = frame->edx;
    uint32_t    timeout_ms = frame->esi;

    /* Solo l'intestazione, come in sys_ipc_recv: il perché è là. */
    const uint32_t META_LEN = 3 * sizeof(uint32_t);

    if (out_meta != NULL && !syscall_verify_ptr(out_meta, META_LEN)) {
        return ERR(EFAULT);
    }
    if (buf != NULL && buf_len > 0 && !syscall_verify_ptr(buf, buf_len)) {
        return ERR(EFAULT);
    }

    IpcMessage kmeta;
    int32_t ret = ipc_recv_timeout(&kmeta, buf, buf_len, timeout_ms);
    if (ret == 0 && out_meta != NULL) {
        out_meta->sender_pid = kmeta.sender_pid;
        out_meta->tipo       = kmeta.tipo;
        out_meta->len        = kmeta.len;
    }
    return ret;
}

/* =============================================================================
 * SYS_TIME (13) -- Data e ora dall'orologio CMOS
 *
 * ebx = RtcTime*
 *
 * Ritorna 0, -EFAULT se il puntatore non è valido, -ENODEV se
 * l'orologio non risponde o dà una data impossibile.
 * ============================================================================= */
int32_t sys_time(InterruptFrame *frame)
{
    RtcTime *out = (RtcTime *)frame->ebx;

    if (!syscall_verify_ptr(out, sizeof(RtcTime))) return ERR(EFAULT);

    RtcTime t;
    if (rtc_read(&t) != 0) return ERR(ENODEV);

    *out = t;
    return 0;
}

/* =============================================================================
 * SYS_IPC_REGISTER (222) -- Registra il processo chiamante come servizio
 *
 * ebx = name*   (stringa null-terminated, max IPC_NAME_LEN-1 caratteri)
 *
 * Usata da driver e servizi all'avvio per farsi trovare dai client
 * tramite nome invece che PID (assegnato dinamicamente). Ritorna 0 su
 * successo, -EEXIST se il nome è già registrato da un processo vivo.
 * ============================================================================= */
int32_t sys_ipc_register(InterruptFrame *frame)
{
    const char *name = (const char *)frame->ebx;

    if (!syscall_verify_str(name, IPC_NAME_LEN)) return ERR(EFAULT);

    int32_t ret = ipc_register(name);
    klog(LOG_INFO, "SYSCALL ipc_register('%s') PID=%u -> %d",
         name, proc_get_current()->pid, ret);
    return ret;
}

/* =============================================================================
 * SYS_IPC_LOOKUP (223) -- Cerca il PID del servizio registrato con 'name'
 *
 * ebx = name*
 *
 * Ritorna il PID (>0) su successo, -ENOENT se nessun processo vivo ha
 * registrato quel nome.
 * ============================================================================= */
int32_t sys_ipc_lookup(InterruptFrame *frame)
{
    const char *name = (const char *)frame->ebx;

    if (!syscall_verify_str(name, IPC_NAME_LEN)) return ERR(EFAULT);

    return ipc_lookup(name);
}

/* =============================================================================
 * SYS_IRQ_BIND (224) -- Rivendica un IRQ hardware per il processo chiamante
 *
 * ebx = numero IRQ (0-15)
 *
 * Da questo momento, gli interrupt hardware su quell'IRQ arrivano come
 * messaggi IPC nella mailbox del chiamante (sender_pid=0, type=0xFFFFFFFF,
 * payload=uint32_t col numero IRQ) invece di essere ignorati dal kernel.
 * Un solo processo vivo alla volta può rivendicare un dato IRQ.
 * ============================================================================= */
int32_t sys_irq_bind(InterruptFrame *frame)
{
    uint8_t irq = (uint8_t)frame->ebx;
    uint32_t pid = proc_get_current()->pid;

    int32_t ret = irq_bind_process(irq, pid);
    klog(LOG_INFO, "SYSCALL irq_bind(irq=%u) PID=%u -> %d", irq, pid, ret);
    return ret;
}

/* =============================================================================
 * e_un_driver — il varco verso l'hardware, in un posto solo
 *
 * Lo attraversano ioport_bind, dma_alloc e mmio_map: le tre syscall con cui
 * un processo ring 3 tocca un dispositivo invece di chiedere a qualcuno di
 * toccarlo per lui. Chi passa lo decide il CARICATORE guardando il nome
 * dell'eseguibile (*.drv, vedi percorso_di_driver in kernel/loader/elf.c —
 * li' c'e' anche cosa questo criterio NON garantisce).
 *
 * ! IL CRITERIO DI PRIMA ERA CIRCOLARE, e vale la pena scriverlo perche' da
 * fuori sembrava un controllo. dma_alloc e mmio_map chiedevano
 * `io_port_count != 0`, cioe' «hai gia' delle porte I/O?»; ma le porte le
 * dava ioport_bind, che non controllava NIENTE. Due righe:
 *
 *     ioport_bind(0x3F8, 1);      // nessuno chiede chi sei
 *     mmio_map(&zona);            // e adesso sei un driver
 *
 * e un programma qualunque mappava i registri di un dispositivo qualunque.
 * Non era una difesa: era un passaggio in piu'.
 *
 * ! E SBAGLIAVA ANCHE NELL'ALTRO VERSO, che e' il motivo per cui e' stato
 * rifatto adesso e non un giorno qualsiasi: un framebuffer NON HA porte I/O.
 * Con quel criterio il server grafico — il primo utente vero di mmio_map —
 * non avrebbe potuto mappare i propri registri se non prendendosi delle
 * porte altrui per finta. Vedi DIREZIONE.md, gradino 0.
 * ============================================================================= */
static int e_un_driver(Process *p, const char *chi)
{
    /* =========================================================================
     * ! IL NOME NON BASTA: SERVE ANCHE ESSERE root. Aggiunto il 17 agosto 2026,
     * lo stesso giorno in cui i permessi hanno cominciato a mordere.
     *
     * `is_driver` lo accende il caricatore guardando SOLO il nome del file. E
     * i driver installati sono 0755: un utente normale poteva eseguire
     * /dev/ide.drv, ottenere il varco, e con ioport_bind parlare direttamente
     * al controller del disco — cioe' leggere e scrivere qualunque settore
     * SENZA passare da un solo controllo di permesso.
     *
     * ! ERA LA STESSA FORMA DEL DIFETTO DI blkwrite, un piano piu' sotto: un
     * controllo accurato sui file accanto a una porta che non chiede niente.
     * La domanda da farsi dopo ogni controllo nuovo non e' «questo e' giusto?»
     * ma «esiste un'altra strada per ottenere la stessa cosa?».
     *
     * ! E LA CONSEGUENZA E' DICHIARATA: un utente normale non puo' avviare il
     * server grafico con `exwin`. Per quello c'e' SYS_FB_MAP, che mappa SOLO
     * il framebuffer e non da' nessuna porta — una capacita' stretta al posto
     * di una larga, che e' il modo giusto di risolvere questo genere di cose.
     * ========================================================================= */
    if (p != NULL && p->is_driver && p->uid == 0) return 1;

    if (p != NULL && p->is_driver && p->uid != 0) {
        klog(LOG_WARN, "SYSCALL %s: PID %u e' un driver ma gira come uid %u - "
             "il varco dei driver e' di root", chi, p->pid, p->uid);
        return 0;
    }

    /* Il motivo va detto per intero: «permesso negato» su una syscall che il
     * driver ha appena chiamato manda a cercare il guasto nel driver. Qui il
     * guasto e' quasi sempre un eseguibile installato nel posto sbagliato. */
    klog(LOG_WARN, "SYSCALL %s: PID %u non e' un driver - l'eseguibile non si "
         "chiama *.drv, rifiutato", chi, (p != NULL) ? p->pid : 0);
    return 0;
}

/* =============================================================================
 * SYS_IOPORT_BIND (225) -- Richiede l'accesso a un range di porte I/O
 *
 * ebx = base (indirizzo porta iniziale)
 * ecx = count (numero di porte consecutive, es. 8 per 0x60-0x67)
 *
 * Un solo range contiguo per processo — chiamate successive sovrascrivono
 * il range precedente. Nessuna verifica di conflitto con altri processi:
 * a differenza degli IRQ, più driver che condividono un bus (es. master
 * e slave PIC) potrebbero legittimamente necessitare porte overlappanti;
 * l'isolamento reale è che un processo può toccare SOLO le porte per cui
 * ha chiamato questa bind, mai porte arbitrarie.
 *
 * ! SOLO I DRIVER, DA AGOSTO 2026. Prima chiunque poteva chiamarla, e
 * l'isolamento descritto qui sopra proteggeva soltanto dalle porte NON
 * rivendicate: bastava rivendicare quelle giuste. Con 0xCF8/0xCFC si
 * riprogrammano le BAR di qualunque dispositivo PCI, con 0x1F0 si scrive
 * sul disco sotto il filesystem. Una porta I/O e' pericolosa quanto un
 * registro mappato in memoria, quindi passa dallo stesso varco.
 * ============================================================================= */
int32_t sys_ioport_bind(InterruptFrame *frame)
{
    uint32_t base  = frame->ebx;
    uint32_t count = frame->ecx;
    Process *self  = proc_get_current();
    uint32_t i;

    if (!e_un_driver(self, "ioport_bind")) return ERR(EPERM);

    if (base > 0xFFFF || count == 0 || base + count > 0x10000) {
        return ERR(EINVAL);
    }

    /* ! SI AGGIUNGE, NON SI SOSTITUISCE — dal 1 settembre 2026. Il perche'
     * per esteso sta accanto a io_finestre in kernel/include/sched.h: un
     * driver audio ISA deve toccare quattro isole di porte lontane fra
     * loro, e coprirle con un intervallo solo vorrebbe dire dargli anche
     * il PIC e il timer.
     *
     * Chi chiama una volta sola — cioe' ogni driver scritto prima di
     * oggi — ottiene esattamente cio' che otteneva prima. */

    /* La stessa finestra chiesta due volte non ne consuma una seconda: un
     * driver che riprova dopo un reset non deve esaurire il posto. */
    for (i = 0; i < self->io_finestre_n; i++) {
        if (self->io_finestre[i].base  == base &&
            self->io_finestre[i].count == count) return 0;
    }

    if (self->io_finestre_n >= IO_FINESTRE_MAX) {
        klog(LOG_WARN, "SYSCALL ioport_bind: PID %u ha gia' %u finestre",
             self->pid, self->io_finestre_n);
        return ERR(ENOSPC);
    }

    self->io_finestre[self->io_finestre_n].base  = base;
    self->io_finestre[self->io_finestre_n].count = count;
    self->io_finestre_n++;

    klog(LOG_INFO, "SYSCALL ioport_bind(base=0x%x, count=%u) PID=%u "
         "[finestra %u]", base, count, self->pid, self->io_finestre_n);
    return 0;
}

/* Verifica che 'port' stia in UNA delle finestre rivendicate dal chiamante */
static int ioport_allowed(Process *p, uint16_t port)
{
    uint32_t i;

    for (i = 0; i < p->io_finestre_n; i++) {
        if (port >= p->io_finestre[i].base &&
            port <  p->io_finestre[i].base + p->io_finestre[i].count) return 1;
    }
    return 0;
}

/* =============================================================================
 * SYS_IOPORT_IN (226) -- Legge un byte da una porta I/O
 *
 * ebx = porta
 * Ritorna il byte letto (0-255) su successo, -EPERM se la porta non è
 * nel range rivendicato con SYS_IOPORT_BIND.
 * ============================================================================= */
int32_t sys_ioport_in(InterruptFrame *frame)
{
    uint16_t port = (uint16_t)frame->ebx;
    Process *self = proc_get_current();

    if (!ioport_allowed(self, port)) {
        klog(LOG_WARN, "SYSCALL ioport_in: PID %u nega'to accesso a porta 0x%x",
             self->pid, port);
        return ERR(EPERM);
    }

    return (int32_t)port_inb(port);
}

/* =============================================================================
 * SYS_IOPORT_OUT (227) -- Scrive un byte su una porta I/O
 *
 * ebx = porta
 * ecx = valore (0-255)
 * Ritorna 0 su successo, -EPERM se la porta non è in whitelist.
 * ============================================================================= */
int32_t sys_ioport_out(InterruptFrame *frame)
{
    uint16_t port  = (uint16_t)frame->ebx;
    uint8_t  value = (uint8_t)frame->ecx;
    Process *self  = proc_get_current();

    if (!ioport_allowed(self, port)) {
        klog(LOG_WARN, "SYSCALL ioport_out: PID %u negato accesso a porta 0x%x",
             self->pid, port);
        return ERR(EPERM);
    }

    port_outb(port, value);
    return 0;
}

/* =============================================================================
 * Accessi I/O a 16 e 32 bit (SYS_IOPORT_IN16/OUT16/IN32/OUT32)
 *
 * Il perché servano — e perché siano quattro numeri di syscall invece di
 * un argomento "ampiezza" — sta in kernel/include/syscall.h.
 *
 * ! QUI IL CONTROLLO DEL RANGE DEVE COPRIRE TUTTI I BYTE TOCCATI.
 * ioport_allowed() verifica UNA porta. Una out32 su 0xCF8 scrive su
 * 0xCF8..0xCFB: se ci si limitasse a controllare la prima, un processo
 * che ha rivendicato la sola 0xCF8 potrebbe scrivere su 0xCF9 — che su
 * molti chipset e' il registro di reset della piattaforma. Il controllo
 * si fa quindi su base e ultimo byte, e siccome il range e' contiguo,
 * questi due bastano a coprire anche quelli in mezzo.
 *
 * ! ALLINEAMENTO OBBLIGATORIO. Un accesso a 32 bit su una porta non
 * multipla di 4 viene spezzato dal chipset in due cicli, e sul bus PCI
 * il secondo non e' piu' un ciclo di configurazione: il risultato e'
 * silenziosamente sbagliato. Meglio -EINVAL subito che un dispositivo
 * enumerato con dati inventati.
 * ============================================================================= */

/* Vero se TUTTI i byte da 'port' a 'port+ampiezza-1' sono nel range del
 * processo. Ritorna falso anche in caso di traboccamento a 0x10000. */
static int ioport_allowed_range(Process *p, uint32_t port, uint32_t ampiezza)
{
    if (port + ampiezza > 0x10000) return 0;
    return ioport_allowed(p, (uint16_t)port) &&
           ioport_allowed(p, (uint16_t)(port + ampiezza - 1));
}

/* Parte comune ai quattro: controlla allineamento e permessi, e registra
 * il rifiuto una volta sola invece che in quattro punti diversi. */
static int32_t ioport_prepara(uint32_t port, uint32_t ampiezza, const char *chi)
{
    Process *self = proc_get_current();

    if (port & (ampiezza - 1)) return ERR(EINVAL);

    if (!ioport_allowed_range(self, port, ampiezza)) {
        klog(LOG_WARN, "SYSCALL %s: PID %u negato accesso a 0x%x (%u byte)",
             chi, self->pid, port, ampiezza);
        return ERR(EPERM);
    }
    return 0;
}

/* ebx = porta. Ritorna 0..65535, oppure un errore negativo. */
int32_t sys_ioport_in16(InterruptFrame *frame)
{
    uint32_t port = frame->ebx;
    int32_t  err  = ioport_prepara(port, 2, "ioport_in16");

    if (err != 0) return err;
    return (int32_t)port_inw((uint16_t)port);
}

/* ebx = porta, ecx = valore (0..65535) */
int32_t sys_ioport_out16(InterruptFrame *frame)
{
    uint32_t port = frame->ebx;
    int32_t  err  = ioport_prepara(port, 2, "ioport_out16");

    if (err != 0) return err;
    port_outw((uint16_t)port, (uint16_t)frame->ecx);
    return 0;
}

/* ebx = porta, ecx = uint32_t* dove scrivere il valore letto.
 * Ritorna 0 su successo: il valore NON passa dal ritorno perché
 * 0xFFFFFFFF è una risposta legittima del bus PCI e sarebbe
 * indistinguibile da -1. */
int32_t sys_ioport_in32(InterruptFrame *frame)
{
    uint32_t  port = frame->ebx;
    uint32_t *dst  = (uint32_t *)frame->ecx;
    int32_t   err  = ioport_prepara(port, 4, "ioport_in32");

    if (err != 0) return err;
    if (!syscall_verify_ptr(dst, sizeof(uint32_t))) return ERR(EFAULT);

    *dst = port_inl((uint16_t)port);
    return 0;
}

/* ebx = porta, ecx = valore a 32 bit */
int32_t sys_ioport_out32(InterruptFrame *frame)
{
    uint32_t port = frame->ebx;
    int32_t  err  = ioport_prepara(port, 4, "ioport_out32");

    if (err != 0) return err;
    port_outl((uint16_t)port, frame->ecx);
    return 0;
}

/* =============================================================================
 * SYS_IRQ_DONE (237) -- «ho servito l'interrupt, riapri la linea»
 *
 * ebx = numero IRQ
 *
 * Il dispatcher maschera l'IRQ nel PIC prima di consegnare la notifica al
 * driver ring3; questa è la contropartita. Il perché per esteso sta in
 * kernel/arch/x86/isr.c, sopra pic_mask_irq().
 *
 * Nessun log qui: su una scheda di rete carica passerebbe di qui una volta
 * per pacchetto, e riempire il log seriale di righe identiche significa
 * seppellire quelle che servono davvero.
 * ============================================================================= */
int32_t sys_irq_done(InterruptFrame *frame)
{
    return irq_done_process((uint8_t)frame->ebx, proc_get_current()->pid);
}

/* =============================================================================
 * SYS_IRQ_UNBIND (219) — vedi kernel/include/syscall.h per il contratto
 * ========================================================================== */
int32_t sys_irq_unbind(InterruptFrame *frame)
{
    uint8_t  irq = (uint8_t)frame->ebx;
    uint32_t pid = proc_get_current()->pid;

    int32_t ret = irq_unbind_uno(irq, pid);
    klog(LOG_INFO, "SYSCALL irq_unbind(irq=%u) PID=%u -> %d", irq, pid, ret);
    return ret;
}

/* =============================================================================
 * SYS_DMA_ALLOC (239) — memoria per un bus master
 *
 * Il perche' sta in kernel/include/syscall.h, accanto alla definizione di
 * DmaZona. Qui restano le due decisioni che si vedono solo nel codice.
 * ============================================================================= */
/* =============================================================================
 * SYS_RANDOM (240) — vedi kernel/include/syscall.h per il contratto
 * ============================================================================= */
int32_t sys_random(InterruptFrame *frame)
{
    uint8_t *buf = (uint8_t *)frame->ebx;
    uint32_t len = frame->ecx;
    uint32_t fatti = 0;

    if (len == 0) return 0;

    /* ! IL PUNTATORE SI VERIFICA UNA VOLTA SOLA, PER TUTTA LA LUNGHEZZA.
     * entropia_preleva serve al massimo 32 byte per volta e una richiesta
     * piu' grande si compone di piu' prelievi, ma ogni scrittura resta
     * dentro l'intervallo gia' controllato qui: ricontrollarlo a ogni
     * giro non aggiungerebbe sicurezza, e dimenticarsene una volta la
     * toglierebbe tutta. */
    if (!syscall_verify_ptr(buf, len)) return ERR(EFAULT);

    while (fatti < len) {
        uint32_t chiedo = len - fatti;
        int      n;

        if (chiedo > 32u) chiedo = 32u;

        n = entropia_preleva(buf + fatti, chiedo);
        if (n <= 0) {
            /* Niente entropia: se non se n'e' consegnata nemmeno un po'
             * si riporta l'errore, altrimenti si dice quanto si e'
             * fatto — un riempimento parziale e' onesto, uno silenzioso
             * no. */
            return (fatti > 0) ? (int32_t)fatti : ERR(EAGAIN);
        }

        fatti += (uint32_t)n;
    }

    return (int32_t)fatti;
}

int32_t sys_dma_alloc(InterruptFrame *frame)
{
    DmaZona  *z    = (DmaZona *)frame->ebx;
    Process  *proc = proc_get_current();
    uint32_t  pagine, fisico, vaddr, i;

    if (!syscall_verify_ptr(z, sizeof(DmaZona))) return ERR(EFAULT);
    if (z->byte == 0) return ERR(EINVAL);

    if (!e_un_driver(proc, "dma_alloc")) return ERR(EPERM);

    pagine = ALIGN_UP(z->byte, PAGE_SIZE) / PAGE_SIZE;
    if (pagine > DMA_PAGINE_MAX) return ERR(ENOMEM);

    /* ! pmm_alloc_pages E NON pmm_alloc_page IN CICLO. Sono le uniche
     * pagine del sistema che devono essere contigue FISICAMENTE: la
     * scheda percorre un anello di descrittori sommando all'indirizzo di
     * partenza, senza tabelle di pagine di mezzo. Allocarle una per una
     * darebbe pagine sparse, e la scheda scriverebbe nella RAM di
     * qualcun altro a partire dalla seconda. */
    fisico = pmm_alloc_pages(pagine);
    if (fisico == 0) return ERR(ENOMEM);

    if (proc->heap_max == 0) { pmm_free_pages(fisico, pagine); return ERR(ENOMEM); }

    vaddr = ALIGN_UP(proc->heap_end, PAGE_SIZE);
    if (vaddr < USER_SPACE_BASE) vaddr = USER_SPACE_BASE;
    if (vaddr > proc->heap_max ||
        pagine > (proc->heap_max - vaddr) / PAGE_SIZE) {
        pmm_free_pages(fisico, pagine);
        return ERR(ENOMEM);
    }

    for (i = 0; i < pagine; i++) {
        paging_azzera_fisica(fisico + i * PAGE_SIZE);
        if (paging_map_page(proc->page_directory,
                            vaddr + i * PAGE_SIZE,
                            fisico + i * PAGE_SIZE,
                            PG_PRESENT | PG_USER | PG_WRITABLE) != 0) {
            uint32_t j;
            for (j = 0; j < i; j++)
                paging_unmap_page(proc->page_directory, vaddr + j * PAGE_SIZE);
            pmm_free_pages(fisico, pagine);
            return ERR(ENOMEM);
        }
    }

    proc->heap_end = vaddr + pagine * PAGE_SIZE;

    z->virt   = vaddr;
    z->fisico = fisico;

    klog(LOG_INFO, "SYSCALL dma_alloc: PID %u, %u pagine, virt 0x%08x "
         "fisico 0x%08x", proc->pid, pagine, vaddr, fisico);
    return 0;
}

/* =============================================================================
 * SYS_MODO_TESTO (245) — vedi kernel/include/syscall.h e vga_modo3.c
 * ========================================================================== */
int32_t sys_modo_testo(InterruptFrame *frame)
{
    Process *self = proc_get_current();

    (void)frame;

    klog(LOG_WARN, "SYSCALL modo_testo: PID %u rimette lo schermo",
         self ? self->pid : 0);

    vga_ripristina_testo();
    return 0;
}

/* =============================================================================
 * SYS_VIDEO_INFO (246) — dov'e' il framebuffer e che forma ha
 *
 * Il perche' sta in kernel/include/syscall.h. Qui c'e' solo la copia, e
 * l'unica cosa da non sbagliare e':
 *
 * ! SI RIEMPIE UNA COPIA NEL KERNEL E POI SI SCRIVE, invece di far scrivere
 * vga_info_fb() direttamente nella memoria dell'utente. Quelle pagine possono
 * non essere presenti — EX-OS carica su richiesta — e passare puntatori
 * dell'utente a codice che non se lo aspetta e' il modo di prendersi un page
 * fault dentro una funzione che non c'entra niente.
 * ========================================================================== */
/* =============================================================================
 * SYS_PTY_APRI (251) -- una coppia master/slave
 *
 * ebx = int fd[2]:  fd[0] = master, fd[1] = slave
 *
 * ! I DUE DESCRITTORI NASCONO NELLO STESSO PROCESSO, e poi si separano con uno
 * spawn: il padre tiene il master e da' lo slave al figlio come stdin, stdout e
 * stderr. E' la stessa forma di pipe(), ed e' voluto — chi sa usare una pipe
 * sa usare questo senza imparare niente di nuovo.
 *
 * ! E CHI TIENE IL MASTER DEVE CHIUDERE LO SLAVE, esattamente come con le pipe.
 * Se non lo fa, il pty conta ancora un capo aperto dalla parte della shell e
 * chi legge il master non vedra' mai la fine — aspettera' per sempre l'uscita
 * di un programma che e' gia' morto.
 * ============================================================================= */
int32_t sys_pty_apri(InterruptFrame *frame)
{
    int32_t *ufd  = (int32_t *)frame->ebx;
    Process *proc = proc_get_current();
    int      h, hs, m = -1, sl = -1;
    int32_t  r;

    if (!syscall_verify_ptr(ufd, 2 * sizeof(int32_t))) return ERR(EFAULT);

    r = pty_apri(&h, &hs);
    if (r < 0) return r;

    m = find_free_fd(proc);
    if (m < 0) { pty_chiudi(h, 1); pty_chiudi(h, 0); return ERR(EMFILE); }

    proc->fds[m].type        = FD_PTY_M;
    proc->fds[m].flags       = O_RDWR;
    proc->fds[m].offset      = 0;
    proc->fds[m].inode       = (uint32_t)h;
    proc->fds[m].driver_data = NULL;

    sl = find_free_fd(proc);
    if (sl < 0) {
        proc->fds[m].type = FD_UNUSED;
        pty_chiudi(h, 1); pty_chiudi(h, 0);
        return ERR(EMFILE);
    }
    proc->fds[sl].type        = FD_PTY_S;
    proc->fds[sl].flags       = O_RDWR;
    proc->fds[sl].offset      = 0;
    proc->fds[sl].inode       = (uint32_t)h;
    proc->fds[sl].driver_data = NULL;

    ufd[0] = m;
    ufd[1] = sl;

    klog(LOG_INFO, "SYSCALL pty_apri() PID=%u -> master %d, slave %d",
         proc->pid, m, sl);
    return 0;
}

/* =============================================================================
 * SYS_PTY_CTL (252) -- primo piano, modo, misura
 *
 * ebx = fd (una delle due estremita'), ecx = comando, edx = argomento
 *
 * ! IL PRIMO PIANO LO DICHIARA CHI LANCIA, cioe' la shell, e non lo indovina
 * il kernel. Il pty potrebbe interrompere «l'ultimo che ha letto», ma quello e'
 * quasi sempre la shell stessa: un Ctrl+C ammazzerebbe la sessione invece del
 * programma che sta girando. Senza dichiarazione non si interrompe nessuno —
 * un Ctrl+C che non morde e' meglio di uno che uccide la cosa sbagliata.
 *
 * ! E LA MISURA STA QUI E NON IN UNA VARIABILE D'AMBIENTE, perche' cambia
 * mentre il programma gira: chi ridimensiona la finestra la scrive nel pty, e
 * chi disegna a schermo pieno la rilegge. Una variabile la si legge all'avvio
 * e resta quella di allora.
 * ============================================================================= */
int32_t sys_pty_ctl(InterruptFrame *frame)
{
    int32_t   fd  = (int32_t)frame->ebx;
    uint32_t  cmd = frame->ecx;
    uint32_t  arg = frame->edx;
    Process  *proc = proc_get_current();

    if (fd < 0 || fd >= MAX_FD) return ERR(EBADF);
    if (proc->fds[fd].type != FD_PTY_M && proc->fds[fd].type != FD_PTY_S)
        return ERR(ENOTTY);

    return pty_ctl((int)proc->fds[fd].inode, cmd, arg);
}

/* =============================================================================
 * SYS_INTERROMPI (250) -- «smettila», detto da fuori
 *
 * ebx = pid
 *
 * ! E' IL PEZZO CHE MANCAVA PER Ctrl+C, e per giorni la coda ha detto «serve un
 * modo di chiedere al kernel: interrompi quel processo». Fino a oggi Ctrl+C era
 * il CARATTERE 3 e basta: arrivava nel buffer della tastiera come una lettera
 * qualunque, e il programma che ci girava sopra non se ne accorgeva.
 *
 * ! NON E' UN SEGNALE, E NON DIVENTERA' UN SEGNALE PER SBAGLIO. Chi viene
 * interrotto esce con codice 130 — 128 piu' 2, come riporta una shell Unix per
 * un Ctrl+C — e non c'e' modo di intercettarlo. I segnali veri vogliono lo
 * stack del processo riscritto al ritorno da trap per farlo saltare in un
 * gestore e poi tornare indietro: un sottosistema che si aggiunge il giorno che
 * serve davvero, non una riga da infilare qui adesso.
 *
 * ! SI PUO' INTERROMPERE SOLO CHI E' TUO. La regola e' la stessa dei file: uid
 * uguale, oppure root. Senza, un utente qualunque fermerebbe i processi di un
 * altro — e su un sistema dove il server grafico gira come utente normale
 * vorrebbe dire spegnere la sessione di chi ti sta accanto.
 *
 * ! E init NON SI TOCCA. E' il processo che rilancia il login: interromperlo
 * vorrebbe dire un sistema acceso in cui non si puo' piu' entrare, e nessuno
 * lo farebbe apposta — succederebbe con un pid sbagliato.
 * ============================================================================= */
int32_t sys_interrompi(InterruptFrame *frame)
{
    uint32_t  pid  = frame->ebx;
    Process  *self = proc_get_current();
    Process  *p;

    if (self == NULL) return ERR(EINVAL);

    /* ! CHI VUOLE FERMARE SE STESSO CHIAMA exit(), e questo rifiuto non e'
     * pedanteria: interrompersi da soli vorrebbe dire alzare il flag e poi
     * tornare in ring 3 per morire un istante dopo, cioe' un exit scritto in
     * un modo che nasconde di esserlo. */
    if (pid == self->pid) return ERR(EINVAL);

    if (pid <= 1) return ERR(EPERM);        /* init non si tocca */

    p = proc_get_by_pid(pid);
    if (p == NULL) return ERR(ESRCH);

    if (self->uid != 0 && self->uid != p->uid) return ERR(EPERM);

    /* ! ALZARE IL FLAG E SVEGLIARE STANNO INSIEME IN proc_interrompi(), e non
     * qui: la disciplina di linea di un pty fa la stessa cosa davanti a un
     * Ctrl+C, e due copie di questa coppia divergerebbero alla prima
     * modifica. Senza il risveglio, chi aspetta un tasto resterebbe BLOCKED
     * col flag alzato — cioe' Ctrl+C funzionerebbe solo battendo qualcos'altro
     * dopo, che e' il modo piu' sconcertante di funzionare. */
    proc_interrompi(pid);

    klog(LOG_INFO, "SYSCALL interrompi(%u) da PID %u", pid, self->pid);
    return 0;
}

/* =============================================================================
 * SYS_FB_MAP (249) — il framebuffer, e SOLO quello
 *
 * ! E' UNA CAPACITA' STRETTA AL POSTO DI UNA LARGA, ed e' il punto. mmio_map()
 * mappa un indirizzo fisico QUALUNQUE scelto da chi chiama, e per questo e'
 * riservata a root: con i registri di un dispositivo si arriva al DMA, e col
 * DMA a tutta la RAM. Il server grafico pero' non vuole «un indirizzo
 * qualunque»: vuole IL framebuffer, che il kernel conosce gia'.
 *
 * Chiedere al kernel «mappami quello che sai tu» invece di «mappami questo
 * indirizzo» toglie a chi chiama la possibilita' di sbagliare e a chi attacca
 * la possibilita' di scegliere. Non serve nessun privilegio.
 *
 * ! ED E' COSI' CHE LA GRAFICA GIRA IN MULTIUTENZA. Senza, o il server e' di
 * root — e allora ogni utente che vuole le finestre ha bisogno
 * dell'amministratore — oppure il varco dei driver resta aperto a tutti, e
 * allora i permessi sui file non valgono niente.
 *
 * Rende l'indirizzo virtuale, oppure un -errno.
 * ========================================================================== */
int32_t sys_fb_map(InterruptFrame *frame)
{
    Process *proc = proc_get_current();
    uint32_t fisico, passo, larg, alt, bit;
    uint32_t base, fine, pagine, vaddr, i, byte;

    (void)frame;
    if (proc == NULL) return ERR(ESRCH);

    vga_info_fb(&fisico, &passo, &larg, &alt, &bit);
    if (fisico == 0 || passo == 0 || alt == 0) return ERR(ENODEV);

    byte = passo * alt;

    /* ! LA MISURA LA DECIDE IL KERNEL, non chi chiama. Se fosse un parametro,
     * chiedere piu' byte del framebuffer vorrebbe dire mappare cio' che gli
     * sta dietro — ed e' esattamente il modo in cui una capacita' stretta
     * torna a essere larga. */
    base   = fisico & ~(PAGE_SIZE - 1);
    fine   = base + ALIGN_UP((fisico - base) + byte, PAGE_SIZE);
    pagine = (fine - base) / PAGE_SIZE;

    if (fine < base) return ERR(EINVAL);
    if (proc->heap_max == 0) return ERR(ENOMEM);

    vaddr = ALIGN_UP(proc->heap_end, PAGE_SIZE);
    if (vaddr < USER_SPACE_BASE) vaddr = USER_SPACE_BASE;
    if (vaddr > proc->heap_max ||
        pagine > (proc->heap_max - vaddr) / PAGE_SIZE) return ERR(ENOMEM);

    for (i = 0; i < pagine; i++) {
        if (paging_map_page(proc->page_directory, vaddr + i * PAGE_SIZE,
                            base + i * PAGE_SIZE,
                            PG_PRESENT | PG_USER | PG_WRITABLE) != 0) {
            uint32_t j;
            for (j = 0; j < i; j++)
                paging_unmap_page(proc->page_directory, vaddr + j * PAGE_SIZE);
            return ERR(ENOMEM);
        }
    }

    proc->heap_end = vaddr + pagine * PAGE_SIZE;

    klog(LOG_INFO, "SYSCALL fb_map: PID %u (uid %u) -> 0x%08x, %u KB",
         proc->pid, proc->uid, vaddr + (fisico - base), byte / 1024u);

    return (int32_t)(vaddr + (fisico - base));
}

int32_t sys_video_info(InterruptFrame *frame)
{
    VideoInfo *u = (VideoInfo *)frame->ebx;
    VideoInfo  v;

    if (!syscall_verify_ptr(u, sizeof(VideoInfo))) return ERR(EFAULT);

    vga_info_fb(&v.fisico, &v.passo, &v.larghezza, &v.altezza, &v.bit);

    *u = v;
    return 0;
}

/* =============================================================================
 * SYS_LOG (247) — una riga sulla seriale. Il perche' sta in syscall.h.
 * ========================================================================== */
int32_t sys_log(InterruptFrame *frame)
{
    const char *u   = (const char *)frame->ebx;
    uint32_t    len = frame->ecx;
    Process    *self = proc_get_current();
    char        buf[SYS_LOG_MAX + 1];
    uint32_t    i;

    if (len == 0) return 0;
    if (len > SYS_LOG_MAX) len = SYS_LOG_MAX;
    if (!syscall_verify_ptr((void *)u, len)) return ERR(EFAULT);

    /* ! SI COPIA PRIMA DI STAMPARE, e la terminazione la mettiamo noi. Passare
     * a klog un puntatore dell'utente vorrebbe dire che il processo puo'
     * cambiarlo mentre il kernel lo legge, e una stringa senza terminatore
     * farebbe leggere il kernel oltre il buffer. */
    for (i = 0; i < len; i++) {
        char c = u[i];
        /* Una riga: i caratteri di controllo diventano spazi, o un processo
         * potrebbe scrivere sequenze ANSI nel log di chi lo sta leggendo. */
        buf[i] = (c >= 0x20 && c < 0x7F) ? c : ' ';
    }
    buf[len] = '\0';

    /* ! SI SCRIVE CON vga_putchar(), NON CON klog(). klog passa dalla console
     * CORRENTE, e se quella non e' la console di sistema il messaggio non
     * arriva sulla seriale — cioe' proprio nel caso per cui questa syscall
     * esiste: un processo che gira su una console che nessuno guarda.
     * vga_putchar() specchia sempre sulla seriale. */
    {
        char testa[32];
        uint32_t pid = self ? self->pid : 0;
        uint32_t k = 0, d, cifre = 0;
        char rov[12];

        for (k = 0; k < 6; k++) testa[k] = "[PID "[k];
        k = 5;
        d = pid;
        if (d == 0) rov[cifre++] = '0';
        while (d > 0) { rov[cifre++] = (char)('0' + (d % 10)); d /= 10; }
        while (cifre > 0) testa[k++] = rov[--cifre];
        testa[k++] = ']';
        testa[k++] = ' ';

        for (d = 0; d < k; d++) vga_putchar(testa[d]);
        for (d = 0; d < len; d++) vga_putchar(buf[d]);
        vga_putchar('\n');
    }
    return 0;
}

/* =============================================================================
 * SYS_GETUID (24) / SYS_SETUID (23) — l'identita' di chi sta girando
 *
 * Il perche' delle regole sta in kernel/include/syscall.h. Qui c'e' il come, e
 * il come e' due righe di controllo.
 * ========================================================================== */
int32_t sys_getuid(InterruptFrame *frame)
{
    Process *self = proc_get_current();

    (void)frame;
    return self ? (int32_t)self->uid : ERR(ESRCH);
}

int32_t sys_setuid(InterruptFrame *frame)
{
    Process *self = proc_get_current();
    uint32_t uid  = frame->ebx;
    uint32_t gid  = frame->ecx;

    if (self == NULL) return ERR(ESRCH);

    /* ! SOLO root, E SOLO PER SCENDERE. Senza questa riga qualunque processo
     * potrebbe diventare root chiamandola, e tutti i controlli sui file
     * diventerebbero una decorazione. Non c'e' il bit setuid sui file, quindi
     * non esiste nemmeno il caso legittimo in cui servirebbe risalire. */
    if (self->uid != 0) return ERR(EPERM);

    self->uid = uid;
    self->gid = gid;

    klog(LOG_INFO, "SYSCALL setuid: il PID %u scende a uid %u gid %u",
         self->pid, uid, gid);
    return 0;
}

/* =============================================================================
 * SYS_CHOWN (182) — consegnare un file a un altro utente
 *
 * Solo root, e solo su ext2. Il perche' delle due regole sta in vfs_chown().
 * ========================================================================== */
int32_t sys_chown(InterruptFrame *frame)
{
    const char *path = (const char *)frame->ebx;
    char        abs[PERCORSO_MAX];

    if (!syscall_verify_str(path, PERCORSO_MAX)) return ERR(EFAULT);
    if (resolve_path(path, abs, sizeof(abs)) != 0) return ERR(EINVAL);

    return (int32_t)vfs_chown(abs, frame->ecx, frame->edx);
}

int32_t sys_chmod(InterruptFrame *frame)
{
    const char *path = (const char *)frame->ebx;
    char        abs[PERCORSO_MAX];

    if (!syscall_verify_str(path, PERCORSO_MAX)) return ERR(EFAULT);
    if (resolve_path(path, abs, sizeof(abs)) != 0) return ERR(EINVAL);

    return (int32_t)vfs_chmod(abs, frame->ecx);
}

/* =============================================================================
 * SYS_LIB_APRI (248) — aggancia una libreria condivisa
 *
 * Il perche' sta in kernel/include/syscall.h e in kernel/loader/lib.c. Qui c'e'
 * solo il passaggio del confine, che e' sempre lo stesso mestiere: il percorso
 * si COPIA prima di usarlo.
 * ========================================================================== */
int32_t sys_lib_apri(InterruptFrame *frame)
{
    const char *u    = (const char *)frame->ebx;
    Process    *self = proc_get_current();
    char        perc[128];
    uint32_t    i;
    uint32_t    tabella = 0;
    int32_t     rc;

    if (self == NULL) return ERR(ESRCH);
    if (!syscall_verify_str(u, sizeof(perc) - 1)) return ERR(EFAULT);

    /* ! COPIATO PRIMA, come per ogni stringa che arriva da ring 3: lasciarlo
     * la' vorrebbe dire che il processo puo' cambiarlo fra il controllo e
     * l'uso, e la libreria aperta non sarebbe quella controllata. */
    for (i = 0; i + 1 < sizeof(perc) && u[i]; i++) perc[i] = u[i];
    perc[i] = '\0';

    rc = lib_apri(perc, self, &tabella);
    if (rc != 0) return rc;

    /* La fascia delle librerie sta sotto 0x08000000: il valore e' sempre
     * positivo e non si puo' confondere con un -errno. */
    return (int32_t)tabella;
}

/* =============================================================================
 * SYS_LIB_TROVA (238) — la stessa porta, ma solo per chiedere
 *
 * Il perche' sta in kernel/include/syscall.h e in kernel/loader/lib.c. Qui c'e'
 * il passaggio del confine, identico a quello di sys_lib_apri: il percorso si
 * COPIA prima di usarlo.
 * ========================================================================== */
int32_t sys_lib_trova(InterruptFrame *frame)
{
    const char *u    = (const char *)frame->ebx;
    Process    *self = proc_get_current();
    char        perc[128];
    uint32_t    i;
    uint32_t    tabella = 0;
    int32_t     rc;

    if (self == NULL) return ERR(ESRCH);
    if (!syscall_verify_str(u, sizeof(perc) - 1)) return ERR(EFAULT);

    for (i = 0; i + 1 < sizeof(perc) && u[i]; i++) perc[i] = u[i];
    perc[i] = '\0';

    rc = lib_trova(perc, self, &tabella);
    if (rc != 0) return rc;

    return (int32_t)tabella;
}

/* =============================================================================
 * SYS_POLL (244) — aspettare piu' sorgenti insieme
 *
 * L'interfaccia e il perche' stanno in kernel/include/syscall.h. Qui c'e' il
 * come, e il come e' quasi tutto in una regola sola:
 *
 * ! GUARDARE E BLOCCARSI DEVONO ESSERE INDIVISIBILI, SU TUTTE LE SORGENTI.
 * E' la race del risveglio perduto — quella che nel luglio 2026 teneva la
 * shell ferma al prompt — moltiplicata per N. Se fra il «non e' pronto
 * niente» e il `sched_block()` gli interrupt fossero aperti, basterebbe che
 * UNA qualunque delle sorgenti diventasse pronta in quella finestra: chi
 * sveglia troverebbe il processo ancora RUNNING, la sveglia sarebbe consumata
 * a vuoto, e subito dopo ci si addormenterebbe su un evento gia' successo.
 * Con una sorgente sola e' improbabile; con N e' N volte piu' probabile.
 *
 * Percio' tutto il ciclo gira a interrupt disabilitati, e la memoria
 * dell'utente si tocca SOLO fuori: le pagine di un processo possono non
 * essere presenti (EX-OS carica su richiesta), e leggerle con il `cli` in
 * mano vorrebbe dire un page fault a interrupt disabilitati. Da qui la copia
 * dell'elenco dentro il kernel, che e' lo stesso rimbalzo di pipe.c.
 * ========================================================================== */

/* Riempie i revents di una voce sola. Da chiamare a interrupt disabilitati.
 * Rende 1 se la voce e' «pronta», cioe' se ha qualcosa da riferire. */
static int poll_guarda(Process *self, struct pollfd *v)
{
    short pronto = 0;

    v->revents = 0;

    /* POSIX: un fd negativo si salta in silenzio. Serve a spegnere una voce
     * senza ricompattare l'elenco, ed e' il modo in cui un server smette di
     * badare a un client senza rifare l'array a ogni giro. */
    if (v->fd < 0) return 0;

    if (v->fd == FD_IPC) {
        if (self->ipc_count > 0) pronto |= POLLIN;
    } else if (v->fd >= MAX_FD || self->fds[v->fd].type == FD_UNUSED) {
        v->revents = POLLNVAL;
        return 1;
    } else {
        FileDescriptor *d = &self->fds[v->fd];

        switch (d->type) {
        case FD_PIPE_R: {
            uint32_t quanti = 0, scrittori = 0;
            if (pipe_stato_locked((int)d->inode, &quanti, NULL,
                                  NULL, &scrittori) != 0) {
                v->revents = POLLNVAL;
                return 1;
            }
            if (quanti > 0) pronto |= POLLIN;
            /* ! SENZA PIU' SCRITTORI LA PIPE E' PRONTA, non morta. Una read
             * li' sopra rende 0 — la fine dei dati — e ci arriva subito.
             * Dire «non e' pronta» vorrebbe dire far aspettare per sempre
             * qualcosa che non puo' piu' arrivare. Si segnala anche cio' che
             * resta da leggere: prima si finisce il buffer, poi la fine. */
            if (scrittori == 0) pronto |= POLLHUP;
            break;
        }
        case FD_PIPE_W: {
            uint32_t spazio = 0, lettori = 0;
            if (pipe_stato_locked((int)d->inode, NULL, &spazio,
                                  &lettori, NULL) != 0) {
                v->revents = POLLNVAL;
                return 1;
            }
            if (spazio > 0)  pronto |= POLLOUT;
            /* Nessuno legge piu': una write darebbe EPIPE, e saperlo subito
             * e' il motivo per cui si sta guardando. */
            if (lettori == 0) pronto |= POLLERR;
            break;
        }
        case FD_STDIN:
            if (tty_input_pronto_locked()) pronto |= POLLIN;
            break;

        /* ! LE DUE ESTREMITA' DEL PTY GUARDANO BUFFER DIVERSI, e sono le
         * funzioni del pty a saperlo: qui non si sbircia dentro la struttura.
         * Scrivere e' sempre possibile (il buffer pieno butta l'ultimo
         * arrivato invece di bloccare, vedi metti() in pty.c), quindi POLLOUT
         * si concede sempre. */
        case FD_PTY_M:
            if (pty_pronto_master((int)d->inode)) pronto |= POLLIN;
            pronto |= POLLOUT;
            break;

        case FD_PTY_S:
            if (pty_pronto_slave((int)d->inode)) pronto |= POLLIN;
            pronto |= POLLOUT;
            break;

        default:
            /* File normali, stdout, stderr, driver: sempre pronti. Non e' una
             * scorciatoia, e' quello che dice POSIX — una read su un file non
             * blocca mai, al piu' arriva alla fine. */
            pronto |= POLLIN | POLLOUT;
            break;
        }
    }

    /* ! POLLERR, POLLHUP e POLLNVAL NON SI CHIEDONO E ARRIVANO LO STESSO.
     * Sono condizioni, non desideri: filtrarle con `events` vorrebbe dire che
     * chi aspetta solo POLLOUT su una pipe senza lettori non lo sapra' mai. */
    v->revents = (short)(pronto & (v->events | POLLERR | POLLHUP | POLLNVAL));
    return v->revents != 0;
}

/* Si mette in coda su tutto cio' che puo' ancora diventare pronto. Rende 0 se
 * ANCHE UNA SOLA sorgente non ha potuto prendere il proprio posto in attesa.
 * A interrupt disabilitati. */
static int poll_registra(Process *self, struct pollfd *v, uint32_t n)
{
    uint32_t i;
    int      tutte = 1;

    for (i = 0; i < n; i++) {
        FileDescriptor *d;

        if (v[i].fd < 0) continue;

        /* ! LA MAILBOX NON SI REGISTRA, E NON E' UNA DIMENTICANZA: ipc_send()
         * sveglia il destinatario per PID, qualunque cosa stia aspettando.
         * La sorgente piu' importante di questo sistema e' quella che
         * funziona gratis. */
        if (v[i].fd == FD_IPC) continue;
        if (v[i].fd >= MAX_FD) continue;

        d = &self->fds[v[i].fd];

        if (d->type == FD_PIPE_R && (v[i].events & POLLIN)) {
            if (!pipe_attesa_registra_locked((int)d->inode, 1, self->pid))
                tutte = 0;
        } else if (d->type == FD_PIPE_W && (v[i].events & POLLOUT)) {
            if (!pipe_attesa_registra_locked((int)d->inode, 0, self->pid))
                tutte = 0;
        } else if (d->type == FD_STDIN && (v[i].events & POLLIN)) {
            if (!tty_attesa_registra_locked(self->pid))
                tutte = 0;
        } else if (d->type == FD_PTY_M && (v[i].events & POLLIN)) {
            if (!pty_attesa_registra_locked((int)d->inode, 1, self->pid))
                tutte = 0;
        } else if (d->type == FD_PTY_S && (v[i].events & POLLIN)) {
            if (!pty_attesa_registra_locked((int)d->inode, 0, self->pid))
                tutte = 0;
        }
    }

    return tutte;
}

static void poll_disregistra(Process *self, struct pollfd *v, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++) {
        FileDescriptor *d;

        if (v[i].fd < 0 || v[i].fd == FD_IPC || v[i].fd >= MAX_FD) continue;

        d = &self->fds[v[i].fd];

        if      (d->type == FD_PIPE_R) pipe_attesa_togli_locked((int)d->inode, 1, self->pid);
        else if (d->type == FD_PIPE_W) pipe_attesa_togli_locked((int)d->inode, 0, self->pid);
        else if (d->type == FD_STDIN)  tty_attesa_togli_locked(self->pid);
        else if (d->type == FD_PTY_M)  pty_attesa_togli_locked((int)d->inode, 1, self->pid);
        else if (d->type == FD_PTY_S)  pty_attesa_togli_locked((int)d->inode, 0, self->pid);
    }
}

int32_t sys_poll(InterruptFrame *frame)
{
    struct pollfd *u    = (struct pollfd *)frame->ebx;
    uint32_t       n    = frame->ecx;
    int32_t        ms   = (int32_t)frame->edx;
    Process       *self = proc_get_current();
    struct pollfd  k[POLL_MAX];
    uint32_t       i, scadenza = 0;
    int32_t        pronti = 0;

    if (n > POLL_MAX) return ERR(EINVAL);
    if (n > 0) {
        if (!syscall_verify_ptr(u, n * sizeof(struct pollfd))) return ERR(EFAULT);
        for (i = 0; i < n; i++) {
            k[i].fd      = u[i].fd;
            k[i].events  = u[i].events;
            k[i].revents = 0;
        }
    }

    /* ms < 0 e' l'attesa senza fine; ms == 0 e' un'occhiata e via. Il PIT
     * batte a 100 Hz, quindi si arrotonda per eccesso: una scadenza sotto i
     * 10 ms non e' rappresentabile, e renderla 0 vorrebbe dire trasformare
     * un'attesa breve in un'attesa eterna. */
    if (ms > 0) {
        uint32_t tick = ((uint32_t)ms + 9) / 10;
        if (tick == 0) tick = 1;
        scadenza = g_ticks + tick;
        if (scadenza == 0) scadenza = 1;    /* 0 vuol dire «nessuna scadenza» */
    }

    for (;;) {
        uint32_t risveglio;
        int      tutte;

        interrupts_disable();

        pronti = 0;
        for (i = 0; i < n; i++) pronti += poll_guarda(self, &k[i]);

        if (pronti > 0 || ms == 0) { interrupts_enable(); break; }
        if (scadenza != 0 && g_ticks >= scadenza) {
            interrupts_enable();
            pronti = 0;                     /* scaduta: nessuno era pronto */
            break;
        }

        tutte = poll_registra(self, k, n);

        /* ! CHI NON HA TROVATO POSTO SI RICONTROLLA AL TICK DOPO, invece di
         * rubare la sveglia a chi c'era prima. Il posto in attesa e' uno solo
         * per pipe e uno solo per il tty — e' cosi' da sempre, perche' finora
         * ad aspettare era uno solo. Sovrascriverlo farebbe dormire per
         * sempre l'altro, che e' molto peggio di un risveglio ogni 10 ms qui.
         * Il caso e' raro e il ripiego resta corretto: si guarda di nuovo. */
        risveglio = scadenza;
        if (!tutte) {
            uint32_t presto = g_ticks + 1;
            if (presto == 0) presto = 1;
            if (risveglio == 0 || presto < risveglio) risveglio = presto;
        }

        self->block_until = risveglio;
        sched_block(PROC_BLOCKED);          /* riabilita IF al risveglio */

        /* ! IL POSTO SI LASCIA SUBITO, e prima di rifare il giro. Restare
         * registrati su una pipe che non si sta piu' aspettando vorrebbe dire
         * tenerne occupato lo slot e far ripiegare tutti gli altri. */
        interrupts_disable();
        poll_disregistra(self, k, n);
        self->block_until = 0;
        interrupts_enable();
    }

    /* I revents tornano all'utente FUORI dalla sezione critica, per la stessa
     * ragione per cui l'elenco era stato copiato dentro. */
    for (i = 0; i < n; i++) u[i].revents = k[i].revents;

    return pronti;
}

/* =============================================================================
 * SYS_SHM_APRI (242) / SYS_SHM_CHIUDI (243) — memoria condivisa fra processi
 *
 * Qui c'e' solo il confine: verificare il puntatore dell'utente, copiare il
 * nome dentro il kernel e riportare il risultato. Il lavoro sta in
 * kernel/mm/shm.c, e il progetto in kernel/include/shm.h.
 * ========================================================================== */
int32_t sys_shm_apri(InterruptFrame *frame)
{
    ShmZona *u    = (ShmZona *)frame->ebx;
    Process *proc = proc_get_current();
    char     nome[SHM_NOME_LEN];
    uint32_t virt = 0, byte = 0;
    uint32_t i;
    int32_t  rc;

    if (!syscall_verify_ptr(u, sizeof(ShmZona))) return ERR(EFAULT);

    /* ! IL NOME SI COPIA PRIMA DI USARLO, e la terminazione la mettiamo noi.
     * Lasciarlo nello spazio utente vorrebbe dire che il processo puo'
     * cambiarlo dopo che shm_apri l'ha controllato e prima che lo confronti,
     * e un nome senza terminatore farebbe leggere il kernel oltre la
     * struttura. */
    for (i = 0; i < SHM_NOME_LEN; i++) nome[i] = u->nome[i];
    nome[SHM_NOME_LEN - 1] = '\0';

    rc = shm_apri(proc, nome, u->byte, u->flag, &virt, &byte);
    if (rc != 0) return rc;

    u->virt = virt;
    u->byte = byte;
    return 0;
}

int32_t sys_shm_chiudi(InterruptFrame *frame)
{
    return shm_chiudi(proc_get_current(), frame->ebx);
}

/* =============================================================================
 * SYS_MMIO_MAP (241) — i registri di un dispositivo, dentro un processo
 *
 * Il perche' per esteso sta in kernel/include/syscall.h. Qui le tre cose che
 * questo codice DEVE fare e che sono facili da sbagliare:
 *
 *   1. rifiutare la RAM              o e' un buco verso il kernel
 *   2. pagine non cacheabili         o i registri si leggono vecchi
 *   3. rendere l'offset dentro la pagina  o un BAR non allineato punta storto
 * ========================================================================== */
/* ! IL TETTO ERA 1 MB, «piu' di ogni BAR sensato», E NON BASTAVA AL
 * FRAMEBUFFER. Un 800x600 a 32 bit sono 1,83 MB; un 1920x1080 sono 8,3. La
 * frase era vera per i registri di una scheda di rete e falsa per il pezzo di
 * hardware che questa syscall doveva servire per primo — il framebuffer, che
 * e' scritto nel commento di DIREZIONE.md come primo caso d'uso.
 *
 * ! IL TETTO RESTA, ED E' IL PUNTO. Serve a impedire che un driver mappi
 * mezzo spazio di indirizzamento con una richiesta sola: 16 MB coprono un
 * framebuffer 2560x1600 a 32 bit e restano lontani dall'essere «tutto». */
#define MMIO_BYTE_MAX   (16u * 1024u * 1024u)   /* 16 MB: ci sta un framebuffer */

int32_t sys_mmio_map(InterruptFrame *frame)
{
    MmioZona *z    = (MmioZona *)frame->ebx;
    Process  *proc = proc_get_current();
    uint32_t  base, fine, dentro, pagine, vaddr, i, limite_ram;

    if (!syscall_verify_ptr(z, sizeof(MmioZona))) return ERR(EFAULT);
    if (z->byte == 0) return ERR(EINVAL);
    if (z->byte > MMIO_BYTE_MAX) return ERR(EINVAL);

    if (!e_un_driver(proc, "mmio_map")) return ERR(EPERM);

    /* ! L'OFFSET DENTRO LA PAGINA VA CONSERVATO. Un BAR non e' per forza
     * allineato a 4096: si mappa la pagina che lo contiene e si RESTITUISCE
     * l'indirizzo esatto. Arrotondare in giu' e tacere darebbe a chi chiama
     * un puntatore che sembra giusto e punta qualche byte prima. */
    base   = z->fisico & ~(PAGE_SIZE - 1);
    dentro = z->fisico - base;
    fine   = base + ALIGN_UP(dentro + z->byte, PAGE_SIZE);
    pagine = (fine - base) / PAGE_SIZE;

    /* ! IL CONTROLLO CHE VALE PER TUTTI GLI ALTRI: la finestra deve stare
     * SOPRA la RAM. pmm_get_total_pages() conta fino all'indirizzo piu' alto
     * della memoria usabile (lo ricava da E820 in pmm_init), quindi questo e'
     * il primo indirizzo che RAM non e'. */
    limite_ram = pmm_get_total_pages() * PAGE_SIZE;
    if (base < limite_ram) {
        klog(LOG_WARN, "SYSCALL mmio_map: PID %u chiede 0x%08x, che e' RAM "
             "(la RAM finisce a 0x%08x) - rifiutato",
             proc->pid, z->fisico, limite_ram);
        return ERR(EPERM);
    }
    if (fine < base) return ERR(EINVAL);        /* traboccato */

    if (proc->heap_max == 0) return ERR(ENOMEM);

    vaddr = ALIGN_UP(proc->heap_end, PAGE_SIZE);
    if (vaddr < USER_SPACE_BASE) vaddr = USER_SPACE_BASE;
    if (vaddr > proc->heap_max ||
        pagine > (proc->heap_max - vaddr) / PAGE_SIZE) return ERR(ENOMEM);

    for (i = 0; i < pagine; i++) {
        /* ! NON si azzera la pagina fisica, al contrario di dma_alloc:
         * quelli sono REGISTRI. Scriverci zeri vorrebbe dire azzerare il
         * dispositivo prima ancora che il driver lo guardi. */
        if (paging_map_page(proc->page_directory,
                            vaddr + i * PAGE_SIZE,
                            base + i * PAGE_SIZE,
                            PG_PRESENT | PG_USER | PG_WRITABLE |
                            PG_CACHE_DIS | PG_WRITE_THRU) != 0) {
            uint32_t j;
            for (j = 0; j < i; j++)
                paging_unmap_page(proc->page_directory, vaddr + j * PAGE_SIZE);
            return ERR(ENOMEM);
        }
    }

    proc->heap_end = vaddr + pagine * PAGE_SIZE;
    z->virt = vaddr + dentro;

    klog(LOG_INFO, "SYSCALL mmio_map: PID %u, fisico 0x%08x -> virt 0x%08x "
         "(%u pagine, non cacheabili)", proc->pid, z->fisico, z->virt, pagine);
    return 0;
}

/* =============================================================================
 * kernel/fs/vfs.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Instradamento dei percorsi verso il filesystem che li ospita.
 *
 * Le scelte qui sotto sono motivate una per una, perche' quasi tutte
 * hanno un'alternativa che sembra equivalente e non lo e'.
 *
 * -----------------------------------------------------------------------
 * 1. IL MONTAGGIO 0 E' LA ROOT SUL FLOPPY, E NON SI PUO' SMONTARE.
 *
 * Non e' una semplificazione: e' il modo di garantire che l'avvio da
 * floppy resti quello collaudato. Un percorso che non combacia con nessun
 * altro punto di montaggio finisce li', e ci arriva attraverso le stesse
 * fat12_* di prima. Se domani il VFS avesse un difetto nell'instradamento,
 * il caso peggiore sarebbe un disco non raggiungibile — non un sistema che
 * non si avvia.
 *
 * -----------------------------------------------------------------------
 * 2. IL CONFINE DEL PREFISSO: "/disk2" NON STA DENTRO "/disk".
 *
 * E' l'errore classico di ogni instradamento a prefisso. Confrontare i
 * primi N caratteri e fermarsi li' fa finire /disk2 sul montaggio /disk,
 * con il percorso interno "2" — cioe' su un disco SBAGLIATO, in silenzio.
 *
 * Il carattere che segue il prefisso deve essere '\0' oppure '/'. Nessuna
 * eccezione, e vale la pena provarlo con un nome che sia prefisso di un
 * altro, perche' e' un caso che non capita da solo.
 *
 * -----------------------------------------------------------------------
 * 3. PREFISSO PIU' LUNGO, NON PRIMO CHE COMBACIA.
 *
 * Con /disk e /disk/dati montati entrambi, il percorso /disk/dati/x
 * combacia con tutti e due. Vince il piu' lungo, altrimenti il montaggio
 * annidato sarebbe irraggiungibile e l'ordine della tabella deciderebbe
 * il significato di un percorso.
 *
 * -----------------------------------------------------------------------
 * 4. I PUNTI DI MONTAGGIO SONO VIRTUALI, E QUINDI NON DEVONO ESISTERE.
 *
 * Su Unix si monta SOPRA una directory esistente, che sparisce dalla
 * vista finche' il montaggio dura. Qui la directory non serve: /disk
 * compare nell'elenco della root perche' vfs_readdir lo aggiunge.
 *
 * Ne discende una regola che e' meglio dell'originale: montare su un nome
 * che ESISTE gia' viene RIFIUTATO. Su Unix quel caso nasconde dei file
 * senza dirlo; qui non puo' succedere, e in cambio l'elenco di una
 * directory non puo' mai contenere due volte lo stesso nome.
 *
 * -----------------------------------------------------------------------
 * 5. SOLA LETTURA DICHIARATA, NON SUBITA.
 *
 * Dalla 0.133 kernel/fs/fat.c sa anche scrivere, quindi i montaggi sono in
 * lettura/scrittura salvo richiesta esplicita (`mount -r`). Quando un
 * montaggio E' in sola lettura, ogni modifica viene respinta con EROFS
 * PRIMA di toccare il filesystem. L'alternativa — lasciar fallire la
 * scrittura piu' in basso — darebbe errori diversi a seconda del punto in
 * cui il driver si arrende, e un giorno una scrittura parziale.
 *
 * -----------------------------------------------------------------------
 * 6. LA PAGINAZIONE DELL'ELENCO: I MONTAGGI VENGONO PRIMA.
 *
 * readdir e' paginata (start, max). Aggiungere i punti di montaggio IN
 * CODA sembra naturale e non funziona: per sapere dove comincia la coda
 * bisognerebbe conoscere quante voci ha il filesystem, e chiedere una
 * pagina oltre la fine li ristamperebbe a ogni chiamata successiva.
 *
 * Messi in TESTA (indici virtuali 0..K-1) il conto e' esatto e locale:
 * sotto K si emettono i montaggi, sopra si chiede al filesystem a partire
 * da start-K.
 * ============================================================================= */

#include "kernel.h"
#include "vfs.h"
#include "fat12.h"
#include "fat.h"
#include "ext2.h"
#include "iso9660.h"
#include "vol.h"
#include "blk.h"
#include "syscall.h"
#include "sched.h"    /* proc_get_current, sched_yield: il lucchetto del VFS */

/* Il kernel non ha una libreria di stringhe condivisa: ogni file definisce
 * i propri helper statici (vedi cfg_strcmp in fs/cfg.c, str_equal in
 * kernel_main.c). */
static uint32_t v_len(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static int v_uguale(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void v_copia(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static VfsMount g_mnt[VFS_MAX_MOUNT];

typedef struct {
    uint8_t  usato;
    /* Quanti descrittori di processo puntano a questo slot.
     *
     * Vale 1 per ogni apertura normale e cresce solo con vfs_dup(). Serve
     * perche' dup() consegna DUE fd sullo stesso file: senza il conteggio,
     * la prima close() chiuderebbe il file sotto i piedi all'altro — ed e'
     * esattamente il motivo per cui, fino alla 0.150, la redirezione dei
     * descrittori di sys_spawn si faceva per PERCORSO e non per fd.
     *
     * 16 bit: un fd per slot per processo, quindi il massimo teorico e'
     * MAX_FD * numero di processi, molto sotto i 65535. */
    uint16_t rif;
    int      im;                     /* indice nella tabella di montaggio */
    int      h12;                    /* handle fat12.c, -1 sugli altri */
    char     interno[VFS_PATH_MAX];  /* percorso dentro il filesystem */
} VfsFile;

static VfsFile g_file[VFS_MAX_OPEN];

/* =============================================================================
 * UN'OPERAZIONE PER VOLTA — il lucchetto del VFS
 *
 * PERCHE' ESISTE, e perche' e' arrivato solo con il caricamento su
 * richiesta. I driver di filesystem NON sono rientranti: ext2 lavora su
 * cinque buffer globali (uno per genere di blocco: dati, indiretti, inode,
 * bitmap, descrittori) e quella separazione, che basta a impedire a una
 * funzione di pestare i piedi a un'altra, presuppone **un'operazione alla
 * volta**. Due ext2_read intrecciate si scambiano i buffer sotto il naso.
 *
 * Prima le occasioni di intreccio erano rare e nessuno le aveva viste. Con
 * il fault-in sono diventate la norma: al PASSO 15 il kernel carica la
 * shell della console 1 mentre quella della console 0 sta gia' girando e
 * fa fault sulla propria prima pagina. Il sintomo era
 *
 *     EXT2: blocco 0 fuori dal volume (523264)
 *     PF: PID 4, lettura della pagina 0x08000000 fallita (-1)
 *     ATA: timeout DRQ sul canale 0
 *
 * cioe' un numero di blocco calcolato con l'inode di qualcun altro, e un
 * controller lasciato a meta' comando. Nessuna delle tre righe dice
 * "concorrenza".
 *
 * COM'E' FATTO. Un solo detentore per volta, identificato dal PID; chi
 * trova occupato cede la CPU e riprova. Non e' una coda: chi aspetta non
 * ha diritto di precedenza sull'ordine di arrivo, e va benissimo, perche'
 * le operazioni durano quanto un accesso al disco e nessuno resta fuori a
 * lungo. Fuori da un contesto di processo (l'avvio, prima che lo
 * scheduler parta) il lucchetto e' sempre libero: non c'e' nessun altro.
 *
 * ⚠️ CHI LO PRENDE NON DEVE POTER FAULTARE. Se un driver scrivesse dentro
 * un buffer utente non ancora presente, il page fault chiamerebbe il VFS
 * per leggere la pagina dal file e resterebbe fermo su questo lucchetto,
 * che e' gia' suo: un blocco permanente, e nel punto meno sospettabile.
 * Per questo le syscall che consegnano un buffer utente ai driver lo
 * fanno "precaricare" PRIMA di entrare qui — vedi vm_precarica_utente()
 * in paging.c e i suoi chiamanti in syscall_impl.c.
 * ============================================================================= */
/* Le implementazioni senza lucchetto, dichiarate qui perche' qualcuna
 * viene usata da un'altra prima di essere definita. */
static int vfs_stat_nl(const char *abs, VfsStat *st);

static uint32_t g_fs_detentore = 0;   /* 0 = libero; altrimenti PID o sentinella */

/* `chi` non serve al funzionamento: e' il nome dell'operazione, e sta qui
 * perche' quando un lucchetto si inceppa la prima domanda e' "chi lo
 * tiene e cosa sta facendo". Costa un puntatore. */
static const char *g_fs_chi = "?";

#define FS_SENZA_PROCESSO   0xFFFFFFFFu

/* Chi aspetta viene BLOCCATO, non lasciato a girare.
 *
 * ⚠️ E' LA DIFFERENZA FRA FUNZIONARE E NO, non un'ottimizzazione. La
 * prima versione cedeva la CPU in un ciclo (sched_yield) e il sistema si
 * fermava all'avvio da ext2, in modo intermittente: kernel_main gira nel
 * contesto del task IDLE, quindi al PASSO 15 e' il processo a priorita'
 * piu' bassa a tenere il lucchetto mentre carica le shell. Appena una
 * shell diventa eseguibile e comincia a cedere-e-riprovare a priorita'
 * NORMAL, l'idle non viene piu' scelto: non finisce la lettura, non
 * rilascia, e le shell riprovano per sempre. Inversione di priorita' da
 * manuale — con il sintomo peggiore, cioe' nessun messaggio e uno schermo
 * fermo al banner.
 *
 * Bloccando, chi aspetta ESCE dalla coda dei pronti: l'idle torna a essere
 * l'unico eseguibile, finisce, e sveglia tutti. */
static uint32_t g_fs_attesa[MAX_PROCESSES];
static uint32_t g_fs_n_attesa = 0;

static void fs_prendi_n(const char *chi)
{
    Process *p  = proc_get_current();
    uint32_t io = (p != NULL) ? p->pid : FS_SENZA_PROCESSO;

    for (;;) {
        interrupts_disable();

        if (g_fs_detentore == 0) {
            g_fs_detentore = io;
            g_fs_chi       = chi;
            interrupts_enable();
            return;
        }

        /* Fuori da un contesto di processo non c'e' nessuno da bloccare —
         * ed e' anche il caso in cui non puo' esserci contesa: l'avvio,
         * prima che lo scheduler parta. */
        if (p == NULL) { interrupts_enable(); return; }

        /* Il task idle non si blocca MAI: lo scheduler lo vuole sempre
         * eseguibile. Non e' un caso teorico — kernel_main gira proprio
         * li' dentro — ma quando l'idle vuole il lucchetto nessun altro
         * processo esiste ancora, quindi qui non ci arriva. Se un domani
         * ci arrivasse, cedere e riprovare e' l'unica cosa che puo' fare. */
        if (p->priority == PRIO_IDLE) {
            interrupts_enable();
            sched_yield();
            continue;
        }

        /* In coda e a dormire. La registrazione e il blocco avvengono con
         * le interrupt gia' spente e sched_block() le riabilita al
         * risveglio: e' la stessa precauzione contro il "lost wakeup"
         * documentata in drivers/tty/tty.c — registrarsi e bloccarsi
         * devono essere indivisibili rispetto a chi rilascia. */
        if (g_fs_n_attesa < MAX_PROCESSES) {
            g_fs_attesa[g_fs_n_attesa++] = io;
        }
        sched_block(PROC_BLOCKED);
    }
}

static void fs_rilascia(void)
{
    uint32_t i, n;

    interrupts_disable();

    g_fs_detentore = 0;
    g_fs_chi       = "?";

    /* Si sveglia chi aspetta e si rifa' la gara: chi arriva primo prende.
     * Nessuna coda di precedenza — le operazioni durano quanto un accesso
     * al disco e nessuno resta indietro a lungo. */
    n = g_fs_n_attesa;
    g_fs_n_attesa = 0;
    for (i = 0; i < n; i++) sched_unblock_locked(g_fs_attesa[i]);

    interrupts_enable();
}

/* =============================================================================
 * Instradamento
 * ============================================================================= */

/* Lunghezza del prefisso di un punto di montaggio ai fini del confronto.
 * La root vale 0: e' il prefisso vuoto, quello che combacia con tutto e
 * perde contro qualunque altro (punto 3). */
static uint32_t lunghezza_prefisso(const VfsMount *m)
{
    if (m->punto[0] == '/' && m->punto[1] == '\0') return 0;
    return v_len(m->punto);
}

/* Ritorna l'indice del montaggio che ospita `abs` e scrive in `interno` il
 * percorso dentro quel filesystem. Ritorna -1 solo se la tabella e' vuota,
 * cioe' se vfs_init() non e' stata chiamata. */
static int instrada(const char *abs, char *interno, uint32_t max)
{
    int      i, migliore = -1;
    uint32_t len_migliore = 0;

    for (i = 0; i < VFS_MAX_MOUNT; i++) {
        uint32_t l, k;

        if (!g_mnt[i].usato) continue;
        l = lunghezza_prefisso(&g_mnt[i]);

        if (l == 0) {                       /* la root combacia sempre */
            if (migliore < 0) migliore = i;
            continue;
        }

        for (k = 0; k < l; k++) {
            if (abs[k] != g_mnt[i].punto[k]) break;
        }
        if (k < l) continue;

        /* IL CONFINE (punto 2): senza questo "/disk2" finirebbe su
         * "/disk" con percorso interno "2". */
        if (abs[l] != '\0' && abs[l] != '/') continue;

        if (migliore < 0 || l > len_migliore) {
            migliore     = i;
            len_migliore = l;
        }
    }

    if (migliore < 0) return -1;

    {
        uint32_t l = lunghezza_prefisso(&g_mnt[migliore]);
        const char *resto = abs + l;

        if (resto[0] == '\0') v_copia(interno, "/", max);
        else                  v_copia(interno, resto, max);
    }

    return migliore;
}

/* Un punto di montaggio e' figlio DIRETTO della directory `dir` se sta
 * dentro `dir` e non contiene altri '/' dopo di essa. Serve a vfs_readdir
 * per far comparire /disk nell'elenco della root. */
static int e_figlio(const char *punto, const char *dir, const char **nome_out)
{
    uint32_t ld;
    const char *r;

    if (punto[0] == '/' && punto[1] == '\0') return 0;   /* la root, mai */

    if (dir[0] == '/' && dir[1] == '\0') {
        r = punto + 1;
    } else {
        uint32_t k;
        ld = v_len(dir);
        for (k = 0; k < ld; k++) if (punto[k] != dir[k]) return 0;
        if (punto[ld] != '/') return 0;
        r = punto + ld + 1;
    }

    if (*r == '\0') return 0;
    { const char *p = r; while (*p) { if (*p == '/') return 0; p++; } }

    *nome_out = r;
    return 1;
}

/* =============================================================================
 * Tabella di montaggio
 * ============================================================================= */
/* Partizione ATTIVA del disco `disco`, come dispositivo a blocchi.
 * Ritorna -1 se non ce n'e' una.
 *
 * Si legge il settore 0 e si guarda il flag, invece di fidarsi
 * dell'ordine: la voce N della tabella non e' la partizione N in ordine di
 * disco, ed e' esattamente lo stesso confronto che fa l'MBR di
 * bootloader/mbr/mbr.asm. Due pezzi di codice che devono scegliere la
 * STESSA partizione devono usare lo stesso criterio, o il kernel monta
 * come root un volume diverso da quello da cui e' stato caricato. */
static int partizione_attiva(int disco_idx)
{
    uint8_t mbr[512];
    int     i, n;

    if (blk_read(disco_idx, 0, 1, mbr) != 0) return -1;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return -1;

    for (i = 0; i < 4; i++) {
        const uint8_t *v = mbr + 446 + i * 16;
        uint32_t primo;

        if (v[0] != 0x80) continue;

        primo = (uint32_t)v[8] | ((uint32_t)v[9] << 8)
              | ((uint32_t)v[10] << 16) | ((uint32_t)v[11] << 24);

        n = blk_conta();
        {
            int k;
            for (k = 0; k < n; k++) {
                const BlkDev *d = blk_get(k);
                if (d == NULL || !d->usato) continue;
                if (d->tipo != BLK_TIPO_PART) continue;
                if (d->primo == (uint64_t)primo) return k;
            }
        }
    }

    return -1;
}

void vfs_init(uint8_t boot_drive)
{
    int i;

    for (i = 0; i < VFS_MAX_MOUNT; i++) g_mnt[i].usato = 0;
    for (i = 0; i < VFS_MAX_OPEN;  i++) g_file[i].usato = 0;

    v_copia(g_mnt[0].punto, "/", VFS_PUNTO_MAX);
    g_mnt[0].usato        = 1;
    g_mnt[0].sola_lettura = 0;

    /* --- avviati da disco? --- */
    if (boot_drive >= 0x80) {
        /* Il primo disco ATA. E' un'assunzione, e va detta: il BIOS
         * numera 0x80, 0x81... in un ordine che non e' garantito
         * corrispondere a quello dei canali IDE. Finche' si avvia dal
         * primo disco — il caso normale — coincidono. */
        int idisco = blk_trova("hd0");
        int ipart  = (idisco >= 0) ? partizione_attiva(idisco) : -1;
        int mnt    = -1;
        int tipo   = 0;

        /* Il filesystem della root si RICONOSCE, non si assume. Prima
         * c'era solo fat_mount(): avviando da una partizione ext2 quella
         * chiamata fallisce, e il sistema ripiegava sul floppy — che
         * durante un avvio da disco non c'e', quindi il sintomo era
         * "shell non trovata" senza alcun accenno alla causa vera. */
        if (ipart >= 0) {
            VolumeInfo vi;

            if (vol_identifica(ipart, &vi) == 0 && vi.tipo == VOL_FS_EXT2) {
                mnt  = ext2_mount(ipart);
                tipo = VFS_FS_EXT2;
            } else {
                mnt  = fat_mount(ipart);
                tipo = VFS_FS_FAT;
            }
        }

        if (mnt >= 0) {
            const BlkDev *d = blk_get(ipart);

            /* La root e' il montaggio che non si smonta mai: senza questa
             * riga il disco da cui il sistema sta girando risulterebbe
             * libero, e ripartizionarlo sarebbe permesso. */
            blk_acquisisci(ipart);

            g_mnt[0].tipo   = (uint8_t)tipo;
            g_mnt[0].mnt    = mnt;
            g_mnt[0].blkdev = ipart;
            v_copia(g_mnt[0].dev, d ? d->nome : "hd0", BLK_NOME_MAX);

            if (tipo == VFS_FS_EXT2)
                klog(LOG_INFO, "VFS: root '/' su %s (ext2, avvio da disco 0x%02x)",
                     g_mnt[0].dev, boot_drive);
            else
                klog(LOG_INFO, "VFS: root '/' su %s (FAT%d, avvio da disco 0x%02x)",
                     g_mnt[0].dev, fat_tipo(mnt), boot_drive);
            return;
        }

        /* Non si puo' proseguire in silenzio: senza root non esiste
         * /bin/sh, e il sintomo sarebbe "shell non trovata" a diversi
         * passi di distanza dalla causa vera. */
        klog(LOG_ERROR, "VFS: avvio da disco 0x%02x ma la partizione attiva "
                        "non e' montabile: ripiego sul floppy", boot_drive);
    }

    /* Montaggio 0: la root sul floppy, via fat12.c (punto 1). */
    g_mnt[0].tipo   = VFS_FS_FAT12FD;
    g_mnt[0].mnt    = -1;
    g_mnt[0].blkdev = -1;
    v_copia(g_mnt[0].dev, "fd0", BLK_NOME_MAX);

    klog(LOG_INFO, "VFS: root '/' sul floppy (fat12.c)");
}

int vfs_conta(void)
{
    int i, n = 0;
    for (i = 0; i < VFS_MAX_MOUNT; i++) if (g_mnt[i].usato) n++;
    return n;
}

const VfsMount *vfs_get(int i)
{
    int k, n = 0;
    for (k = 0; k < VFS_MAX_MOUNT; k++) {
        if (!g_mnt[k].usato) continue;
        if (n == i) return &g_mnt[k];
        n++;
    }
    return NULL;
}

static int vfs_mount_nl(const char *dev, const char *punto, int sola_lettura)
{
    int        blkdev, slot = -1, i, mnt;
    VolumeInfo vi;
    VfsStat    st;

    if (dev == NULL || punto == NULL)  return ERR(EINVAL);
    if (punto[0] != '/')               return ERR(EINVAL);
    if (v_len(punto) >= VFS_PUNTO_MAX) return ERR(EINVAL);

    /* La root e' il floppy e resta tale (punto 1). */
    if (punto[1] == '\0') return ERR(EBUSY);

    /* Uno slash finale ("/disk/") darebbe un punto che non combacia mai
     * con se stesso: meglio rifiutarlo che accettarlo e non funzionare. */
    if (punto[v_len(punto) - 1] == '/') return ERR(EINVAL);

    for (i = 0; i < VFS_MAX_MOUNT; i++) {
        if (!g_mnt[i].usato) { if (slot < 0) slot = i; continue; }
        if (v_uguale(g_mnt[i].punto, punto)) return ERR(EBUSY);
        if (v_uguale(g_mnt[i].dev,   dev))   return ERR(EBUSY);
    }
    if (slot < 0) return ERR(EMFILE);

    blkdev = blk_trova(dev);
    if (blkdev < 0) return ERR(ENOENT);

    /* Un supporto rimovibile va sondato PRIMA di guardarci dentro: finche'
     * nessuno lo chiede, la finestra di un lettore ottico e' di zero
     * settori e ogni lettura verrebbe respinta come "fuori finestra" —
     * cioe' un disco perfettamente valido risulterebbe non montabile.
     *
     * ENOMEDIUM e non EIO: "non c'e' il disco" e' una cosa che l'utente
     * puo' rimediare, "errore di I/O" no, e confonderle significa mandare
     * qualcuno a cercare un guasto che non c'e'. */
    {
        int s = blk_supporto(blkdev);
        if (s == 0)  return ERR(ENOMEDIUM);
        if (s <  0)  return ERR(EIO);
    }

    /* Il nome non deve esistere gia' (punto 4): montarci sopra
     * nasconderebbe dei file senza dirlo. */
    if (vfs_stat_nl(punto, &st) == 0) {
        klog(LOG_ERROR, "VFS: '%s' esiste gia': montaggio rifiutato", punto);
        return ERR(EEXIST);
    }

    /* Anche la directory che lo conterra' deve esistere, altrimenti si
     * otterrebbe un /a/b raggiungibile senza che /a esista. */
    {
        char padre[VFS_PUNTO_MAX];
        uint32_t l = v_len(punto);
        while (l > 1 && punto[l - 1] != '/') l--;
        if (l > 1) l--;                     /* togli lo slash */
        for (i = 0; (uint32_t)i < l; i++) padre[i] = punto[i];
        padre[l] = '\0';
        if (l == 0) { padre[0] = '/'; padre[1] = '\0'; }

        if (!(padre[0] == '/' && padre[1] == '\0')) {
            if (vfs_stat_nl(padre, &st) != 0 || !st.is_dir) {
                klog(LOG_ERROR, "VFS: '%s' non esiste: montaggio rifiutato", padre);
                return ERR(ENOENT);
            }
        }
    }

    if (vol_identifica(blkdev, &vi) != 0) return ERR(EIO);
    if (vi.tipo != VOL_FS_FAT12 && vi.tipo != VOL_FS_FAT16 &&
        vi.tipo != VOL_FS_FAT32 && vi.tipo != VOL_FS_EXT2 &&
        vi.tipo != VOL_FS_ISO9660) {
        klog(LOG_ERROR, "VFS: '%s' non contiene un filesystem riconoscibile", dev);
        return ERR(EINVAL);
    }
    if (vi.incoerente) {
        klog(LOG_ERROR, "VFS: '%s': metadati incoerenti, montaggio rifiutato", dev);
        return ERR(EINVAL);
    }

    if (vi.tipo == VOL_FS_ISO9660) {
        mnt = iso_mount(blkdev);
        if (mnt < 0) return ERR(EIO);

        g_mnt[slot].tipo = VFS_FS_ISO;

        /* La sola lettura NON e' una scelta del montaggio: e' una
         * proprieta' del formato (vedi kernel/include/iso9660.h). Si
         * impone qui, cosi' `mount cd0 /cdrom` senza -r non consegna un
         * montaggio che accetta scritture per poi perderle. */
        sola_lettura = 1;
    } else if (vi.tipo == VOL_FS_EXT2) {
        mnt = ext2_mount(blkdev);
        if (mnt < 0) return ERR(EIO);

        g_mnt[slot].tipo = VFS_FS_EXT2;
    } else {
        mnt = fat_mount(blkdev);
        if (mnt < 0) return ERR(EIO);

        g_mnt[slot].tipo = VFS_FS_FAT;
    }

    /* Dichiara al livello a blocchi che questo dispositivo e' in uso: da
     * qui in poi blk_rescan() rifiutera' di rileggere la tabella del suo
     * disco. Vedi il commento in blk.h — e' il motivo per cui esiste il
     * contatore invece di una domanda al VFS. */
    blk_acquisisci(blkdev);

    g_mnt[slot].usato        = 1;
    v_copia(g_mnt[slot].punto, punto, VFS_PUNTO_MAX);
    g_mnt[slot].sola_lettura = sola_lettura ? 1 : 0;
    g_mnt[slot].mnt          = mnt;
    g_mnt[slot].blkdev       = blkdev;
    v_copia(g_mnt[slot].dev, dev, BLK_NOME_MAX);

    if (g_mnt[slot].tipo == VFS_FS_ISO)
        klog(LOG_INFO, "VFS: montato %s su %s (ISO 9660%s, sola lettura): '%s'",
             dev, punto, iso_joliet(mnt) ? "/Joliet" : "", iso_etichetta(mnt));
    else if (g_mnt[slot].tipo == VFS_FS_EXT2)
        klog(LOG_INFO, "VFS: montato %s su %s (ext2, %s)", dev, punto,
             g_mnt[slot].sola_lettura ? "sola lettura" : "lettura/scrittura");
    else
        klog(LOG_INFO, "VFS: montato %s su %s (FAT%d, %s)",
             dev, punto, fat_tipo(mnt),
             g_mnt[slot].sola_lettura ? "sola lettura" : "lettura/scrittura");
    return 0;
}

static int vfs_umount_nl(const char *punto)
{
    int i, k;

    if (punto == NULL) return ERR(EINVAL);

    for (i = 0; i < VFS_MAX_MOUNT; i++) {
        if (!g_mnt[i].usato) continue;
        if (!v_uguale(g_mnt[i].punto, punto)) continue;

        if (i == 0) return ERR(EBUSY);   /* la root non si smonta */

        /* Un file ancora aperto su questo montaggio significa che qualcuno
         * ha in mano un handle che diventerebbe un riferimento a un
         * montaggio libero — cioe' letture da un volume diverso. */
        for (k = 0; k < VFS_MAX_OPEN; k++) {
            if (g_file[k].usato && g_file[k].im == i) return ERR(EBUSY);
        }

        if      (g_mnt[i].tipo == VFS_FS_ISO)  iso_umount (g_mnt[i].mnt);
        else if (g_mnt[i].tipo == VFS_FS_EXT2) ext2_umount(g_mnt[i].mnt);
        else                                   fat_umount (g_mnt[i].mnt);

        if (g_mnt[i].blkdev >= 0) blk_rilascia(g_mnt[i].blkdev);
        g_mnt[i].usato = 0;
        klog(LOG_INFO, "VFS: smontato %s", punto);
        return 0;
    }

    return ERR(ENOENT);
}

/* =============================================================================
 * Operazioni
 * ============================================================================= */

/* Vero se il percorso interno e' la radice del suo filesystem. */
static int e_radice(const char *interno)
{
    return interno[0] == '/' && interno[1] == '\0';
}

/* Rilascia lo slot prenotato da vfs_open e riporta l'errore al chiamante.
 * Esiste perche' la prenotazione avviene PRIMA di parlare con il driver
 * (vedi il commento in vfs_open) e ogni uscita anticipata da li' in poi
 * deve disfarla: uno slot prenotato e mai liberato e' un descrittore
 * perso per sempre, e dopo VFS_MAX_OPEN aperture fallite non si apre
 * piu' niente. */
static int apri_fallito(int slot, int err)
{
    g_file[slot].usato = 0;
    g_file[slot].rif   = 0;
    return err;
}

static int vfs_open_nl(const char *abs, uint32_t flags)
{
    char interno[VFS_PATH_MAX];
    int  im, slot = -1, i;

    if (abs == NULL) return ERR(EINVAL);

    im = instrada(abs, interno, sizeof(interno));
    if (im < 0) return ERR(ENOENT);

    /* EROFS PRIMA di toccare il filesystem (punto 5). */
    if (g_mnt[im].sola_lettura &&
        (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND))) {
        return ERR(EROFS);
    }

    for (i = 0; i < VFS_MAX_OPEN; i++) {
        if (!g_file[i].usato) { slot = i; break; }
    }
    if (slot < 0) return ERR(EMFILE);

    /* ⚠️ LO SLOT SI PRENOTA QUI, non a fine funzione.
     *
     * Ogni ramo qui sotto parla con un driver, e i driver di EX-OS
     * stanno in ring3: una lettura di directory e' un messaggio IPC e
     * quindi un punto di riscadenzamento. Marcando `usato` solo alla
     * fine, fra la scelta dello slot e il suo riempimento c'e' una
     * finestra in cui un ALTRO processo entra qui, trova lo stesso slot
     * ancora libero e lo prende: due handle diversi, lo stesso slot, e
     * chi arriva secondo sovrascrive il file del primo.
     *
     * Non e' teoria: e' il guasto per cui un programma che leggeva un
     * proprio file temporaneo si e' ritrovato a leggere il binario ELF
     * che un'altra console stava caricando nello stesso istante —
     * fseek(SEEK_END) rispondeva la dimensione dell'ELF e fgetc()
     * ritornava 0x7F. L'altra meta' del danno la vedeva il caricatore,
     * che al primo close() del vicino si trovava l'handle chiuso sotto i
     * piedi ("impossibile leggere program headers").
     *
     * Il campo `im` e `interno` vanno riempiti insieme a `usato`, perche'
     * chi scorre la tabella (vfs_umount, che cerca file aperti su un
     * montaggio) guarda esattamente quei due campi e ha il diritto di
     * trovarli coerenti. */
    g_file[slot].usato = 1;
    g_file[slot].rif   = 1;
    g_file[slot].im    = im;
    g_file[slot].h12   = -1;
    v_copia(g_file[slot].interno, interno, VFS_PATH_MAX);

    if (g_mnt[im].tipo == VFS_FS_FAT12FD) {
        int h = fat12_open(interno, flags);
        if (h < 0) return apri_fallito(slot, ERR(ENOENT));
        g_file[slot].h12 = h;
    } else if (g_mnt[im].tipo == VFS_FS_ISO) {
        IsoDirEntry e;

        /* Nessun ramo O_CREAT: su ISO non si crea niente, e il montaggio
         * e' gia' stato dichiarato in sola lettura piu' sopra — quindi qui
         * ci si arriva solo in lettura. */
        if (iso_stat(g_mnt[im].mnt, interno, &e) != 0)
            return apri_fallito(slot, ERR(ENOENT));
        if (e.is_dir) return apri_fallito(slot, ERR(EISDIR));
    } else if (g_mnt[im].tipo == VFS_FS_EXT2) {
        Ext2DirEntry e;

        if (ext2_stat(g_mnt[im].mnt, interno, &e) != 0) {
            if (!(flags & O_CREAT)) return apri_fallito(slot, ERR(ENOENT));
            {
                int r = ext2_create(g_mnt[im].mnt, interno);
                if (r == -2) return apri_fallito(slot, ERR(EEXIST));
                if (r != 0)  return apri_fallito(slot, ERR(EIO));
            }
            if (ext2_stat(g_mnt[im].mnt, interno, &e) != 0)
                return apri_fallito(slot, ERR(EIO));
        } else if (flags & O_TRUNC) {
            if (ext2_truncate(g_mnt[im].mnt, interno, 0) != 0)
                return apri_fallito(slot, ERR(EIO));
        }

        if (e.is_dir) return apri_fallito(slot, ERR(EISDIR));
    } else {
        FatDirEntry e;

        if (fat_stat(g_mnt[im].mnt, interno, &e) != 0) {
            if (!(flags & O_CREAT)) return apri_fallito(slot, ERR(ENOENT));
            {
                int r = fat_create(g_mnt[im].mnt, interno);
                if (r == -2) return apri_fallito(slot, ERR(EEXIST));
                if (r != 0)  return apri_fallito(slot, ERR(EIO));
            }
            if (fat_stat(g_mnt[im].mnt, interno, &e) != 0)
                return apri_fallito(slot, ERR(EIO));
        } else if (flags & O_TRUNC) {
            if (fat_truncate(g_mnt[im].mnt, interno, 0) != 0)
                return apri_fallito(slot, ERR(EIO));
        }

        if (e.is_dir) return apri_fallito(slot, ERR(EISDIR));
    }

    return slot;
}

static int vfs_read_nl(int h, void *buf, uint32_t size, uint32_t offset)
{
    if (h < 0 || h >= VFS_MAX_OPEN || !g_file[h].usato) return ERR(EBADF);

    if (g_mnt[g_file[h].im].tipo == VFS_FS_FAT12FD)
        return fat12_read(g_file[h].h12, buf, size, offset);

    if (g_mnt[g_file[h].im].tipo == VFS_FS_ISO)
        return iso_read(g_mnt[g_file[h].im].mnt, g_file[h].interno,
                        buf, size, offset);

    if (g_mnt[g_file[h].im].tipo == VFS_FS_EXT2)
        return ext2_read(g_mnt[g_file[h].im].mnt, g_file[h].interno,
                         buf, size, offset);

    return fat_read(g_mnt[g_file[h].im].mnt, g_file[h].interno,
                    buf, size, offset);
}

/* `offset` viene dal descrittore del processo. fat12.c non lo usa: tiene
 * la propria posizione interna nell'handle, e cambiarlo qui vorrebbe dire
 * toccare il driver del floppy — proprio quello che non si vuole toccare.
 * fat.c invece e' senza stato per file e l'offset gli serve. */
static int vfs_write_nl(int h, const void *buf, uint32_t size, uint32_t offset)
{
    int im;

    if (h < 0 || h >= VFS_MAX_OPEN || !g_file[h].usato) return ERR(EBADF);
    im = g_file[h].im;
    if (g_mnt[im].sola_lettura) return ERR(EROFS);

    /* Ridondante finche' il montaggio ISO resta in sola lettura per
     * forza — ed e' li' apposta: senza, un montaggio ISO che per un
     * difetto arrivasse qui con sola_lettura a 0 finirebbe nel ramo FAT
     * per esclusione, cioe' scriverebbe su un volume ottico attraverso il
     * driver sbagliato. */
    if (g_mnt[im].tipo == VFS_FS_ISO) return ERR(EROFS);

    if (g_mnt[im].tipo == VFS_FS_FAT12FD)
        return fat12_write(g_file[h].h12, buf, size, offset);

    if (g_mnt[im].tipo == VFS_FS_EXT2) {
        int n = ext2_write(g_mnt[im].mnt, g_file[h].interno, buf, size, offset);
        return (n < 0) ? ERR(EIO) : n;
    }

    {
        int n = fat_write(g_mnt[im].mnt, g_file[h].interno, buf, size, offset);
        return (n < 0) ? ERR(EIO) : n;
    }
}

static int vfs_close_nl(int h)
{
    if (h < 0 || h >= VFS_MAX_OPEN || !g_file[h].usato) return ERR(EBADF);

    /* Il file si chiude davvero quando se ne va l'ULTIMO descrittore.
     * Finche' ne resta uno, questa close() e' solo la rinuncia di chi la
     * chiama: il driver non ne sa niente e lo slot non torna libero. */
    if (g_file[h].rif > 1) {
        g_file[h].rif--;
        return 0;
    }

    if (g_mnt[g_file[h].im].tipo == VFS_FS_FAT12FD)
        fat12_close(g_file[h].h12);

    g_file[h].usato = 0;
    g_file[h].rif   = 0;
    return 0;
}

/* Un descrittore in piu' sullo stesso file. Ritorna `h`, o EBADF. */
static int vfs_dup_nl(int h)
{
    if (h < 0 || h >= VFS_MAX_OPEN || !g_file[h].usato) return ERR(EBADF);
    if (g_file[h].rif == 0xFFFF) return ERR(EMFILE);

    g_file[h].rif++;
    return h;
}

/* Il corpo di vfs_stat, a partire da un montaggio e da un percorso GIA'
 * instradato. Esiste separato perche' lo usano in due — vfs_stat, che
 * parte da un percorso, e vfs_fstat, che parte da un handle — e due copie
 * della stessa scelta di driver sarebbero due copie da tenere allineate
 * ogni volta che si aggiunge un filesystem. */
static int stat_interno(int im, const char *interno, VfsStat *st)
{
    /* La radice di un filesystem non ha una voce di directory da
     * interrogare: fat12_stat("/") e fat_stat("/") falliscono entrambe.
     * Vale per "/" e per ogni punto di montaggio. */
    if (e_radice(interno)) {
        st->dimensione   = 0;
        st->is_dir       = 1;
        st->sola_lettura = g_mnt[im].sola_lettura;
        return 0;
    }

    if (g_mnt[im].tipo == VFS_FS_FAT12FD) {
        Fat12Stat f;
        if (fat12_stat(interno, &f) != 0) return ERR(ENOENT);
        st->dimensione   = f.size;
        st->is_dir       = (f.attr & FAT12_ATTR_DIRECTORY) ? 1 : 0;
        st->sola_lettura = (f.attr & FAT12_ATTR_READONLY)  ? 1 : 0;
    } else if (g_mnt[im].tipo == VFS_FS_ISO) {
        IsoDirEntry e;
        if (iso_stat(g_mnt[im].mnt, interno, &e) != 0) return ERR(ENOENT);
        st->dimensione   = e.dimensione;
        st->is_dir       = e.is_dir;
        st->sola_lettura = 1;
    } else if (g_mnt[im].tipo == VFS_FS_EXT2) {
        Ext2DirEntry e;
        if (ext2_stat(g_mnt[im].mnt, interno, &e) != 0) return ERR(ENOENT);
        st->dimensione   = e.dimensione;
        st->is_dir       = e.is_dir;
        st->sola_lettura = 1;
    } else {
        FatDirEntry e;
        if (fat_stat(g_mnt[im].mnt, interno, &e) != 0) return ERR(ENOENT);
        st->dimensione   = e.dimensione;
        st->is_dir       = e.is_dir;
        st->sola_lettura = 1;
    }

    return 0;
}

static int vfs_stat_nl(const char *abs, VfsStat *st)
{
    char interno[VFS_PATH_MAX];
    int  im;

    if (abs == NULL || st == NULL) return ERR(EINVAL);

    im = instrada(abs, interno, sizeof(interno));
    if (im < 0) return ERR(ENOENT);

    return stat_interno(im, interno, st);
}

static int vfs_fstat_nl(int h, VfsStat *st)
{
    if (h < 0 || h >= VFS_MAX_OPEN || !g_file[h].usato) return ERR(EBADF);
    if (st == NULL) return ERR(EINVAL);

    /* Il percorso interno e' quello con cui il file e' stato aperto, e il
     * montaggio quello su cui l'handle vive: nessuno dei due va
     * ricalcolato, o si rischierebbe di guardare un altro volume. */
    return stat_interno(g_file[h].im, g_file[h].interno, st);
}

static int vfs_readdir_nl(const char *abs, VfsDirEntry *out, uint32_t max,
                uint32_t *count, uint32_t start)
{
    char     interno[VFS_PATH_MAX];
    int      im, i;
    uint32_t scritti = 0, k_punti = 0, saltati = 0;

    if (abs == NULL || out == NULL || count == NULL || max == 0)
        return ERR(EINVAL);

    *count = 0;
    im = instrada(abs, interno, sizeof(interno));
    if (im < 0) return ERR(ENOENT);

    /* --- I punti di montaggio figli, in TESTA (punto 6) --- */
    for (i = 0; i < VFS_MAX_MOUNT && scritti < max; i++) {
        const char *nome;

        if (!g_mnt[i].usato || i == im) continue;
        if (!e_figlio(g_mnt[i].punto, abs, &nome)) continue;

        k_punti++;
        if (saltati < start) { saltati++; continue; }

        v_copia(out[scritti].nome, nome, sizeof(out[scritti].nome));
        out[scritti].dimensione = 0;
        out[scritti].is_dir     = 1;
        scritti++;
    }

    /* I montaggi gia' emessi (o saltati) non vanno richiesti al
     * filesystem: da qui in poi si conta a partire da start - k_punti. */
    {
        uint32_t s_fs = (start > k_punti) ? (start - k_punti) : 0;

        if (g_mnt[im].tipo == VFS_FS_FAT12FD) {
            while (scritti < max) {
                Fat12DirEntry tmp[8];
                uint32_t cap = max - scritti, n = 0, j;

                if (cap > 8) cap = 8;
                if (fat12_readdir_path(e_radice(interno) ? NULL : interno,
                                       tmp, cap, &n, s_fs) != 0) {
                    /* Se qualche punto di montaggio e' gia' stato scritto,
                     * la directory esiste comunque: non e' un errore. */
                    if (scritti == 0) return ERR(ENOENT);
                    break;
                }
                for (j = 0; j < n; j++) {
                    fat12_format_name(&tmp[j], out[scritti].nome);
                    out[scritti].dimensione = tmp[j].file_size;
                    out[scritti].is_dir =
                        (tmp[j].attr & FAT12_ATTR_DIRECTORY) ? 1 : 0;
                    scritti++;
                }
                s_fs += n;
                if (n < cap) break;
            }
        } else if (g_mnt[im].tipo == VFS_FS_ISO) {
            while (scritti < max) {
                IsoDirEntry tmp[4];        /* 4 e non 8: i nomi Joliet sono lunghi */
                uint32_t cap = max - scritti, j;
                int      n;

                if (cap > 4) cap = 4;
                n = iso_readdir(g_mnt[im].mnt, interno, tmp, cap, s_fs);
                if (n < 0) {
                    if (scritti == 0) return ERR(ENOENT);
                    break;
                }
                for (j = 0; j < (uint32_t)n; j++) {
                    v_copia(out[scritti].nome, tmp[j].nome,
                            sizeof(out[scritti].nome));
                    out[scritti].dimensione = tmp[j].dimensione;
                    out[scritti].is_dir     = tmp[j].is_dir;
                    scritti++;
                }
                s_fs += (uint32_t)n;
                if ((uint32_t)n < cap) break;
            }
        } else if (g_mnt[im].tipo == VFS_FS_EXT2) {
            while (scritti < max) {
                Ext2DirEntry tmp[4];       /* 4 e non 8: le voci sono piu' grosse */
                uint32_t cap = max - scritti, j;
                int      n;

                if (cap > 4) cap = 4;
                n = ext2_readdir(g_mnt[im].mnt, interno, tmp, cap, s_fs);
                if (n < 0) {
                    if (scritti == 0) return ERR(ENOENT);
                    break;
                }
                for (j = 0; j < (uint32_t)n; j++) {
                    /* I nomi ext2 arrivano a 255 byte, VfsDirEntry ne
                     * tiene 16: v_copia tronca. E' una perdita vera, ed e'
                     * limitata dal formato della struttura che attraversa
                     * la syscall, non dal driver. */
                    v_copia(out[scritti].nome, tmp[j].nome,
                            sizeof(out[scritti].nome));
                    out[scritti].dimensione = tmp[j].dimensione;
                    out[scritti].is_dir     = tmp[j].is_dir;
                    scritti++;
                }
                s_fs += (uint32_t)n;
                if ((uint32_t)n < cap) break;
            }
        } else {
            while (scritti < max) {
                FatDirEntry tmp[8];
                uint32_t cap = max - scritti, j;
                int      n;

                if (cap > 8) cap = 8;
                n = fat_readdir(g_mnt[im].mnt, interno, tmp, cap, s_fs);
                if (n < 0) {
                    if (scritti == 0) return ERR(ENOENT);
                    break;
                }
                for (j = 0; j < (uint32_t)n; j++) {
                    v_copia(out[scritti].nome, tmp[j].nome,
                            sizeof(out[scritti].nome));
                    out[scritti].dimensione = tmp[j].dimensione;
                    out[scritti].is_dir     = tmp[j].is_dir;
                    scritti++;
                }
                s_fs += (uint32_t)n;
                if ((uint32_t)n < cap) break;
            }
        }
    }

    *count = scritti;
    return 0;
}

/* Le tre operazioni di modifica hanno la stessa forma: instrada, respingi
 * se sola lettura, delega. */
static int modifica(const char *abs, int quale)
{
    char interno[VFS_PATH_MAX];
    int  im;

    if (abs == NULL) return ERR(EINVAL);

    im = instrada(abs, interno, sizeof(interno));
    if (im < 0) return ERR(ENOENT);
    if (g_mnt[im].sola_lettura) return ERR(EROFS);
    if (g_mnt[im].tipo == VFS_FS_ISO) return ERR(EROFS);   /* vedi vfs_write */

    /* La radice di un montaggio non si crea ne' si cancella. */
    if (e_radice(interno)) return ERR(EBUSY);

    if (g_mnt[im].tipo == VFS_FS_FAT12FD) {
        if (quale == 0) return fat12_mkdir(interno);
        if (quale == 1) return fat12_rmdir(interno);
        return fat12_delete(interno);
    }

    /* ext2 usa la stessa convenzione di ritorni di fat.c — -2 per il caso
     * che il chiamante deve distinguere — proprio perche' questo blocco
     * possa essere identico invece che quasi identico. */
    if (g_mnt[im].tipo == VFS_FS_EXT2) {
        int r;
        if (quale == 0) {
            r = ext2_mkdir(g_mnt[im].mnt, interno);
            if (r == -2) return ERR(EEXIST);
        } else if (quale == 1) {
            r = ext2_rmdir(g_mnt[im].mnt, interno);
            if (r == -2) return ERR(ENOTEMPTY);
        } else {
            r = ext2_unlink(g_mnt[im].mnt, interno);
        }
        return (r != 0) ? ERR(EIO) : 0;
    }

    /* fat.c distingue due casi con -2, e il chiamante deve poterli
     * distinguere anche lui: "esiste gia'" e "directory non vuota" sono
     * cose che l'utente puo' correggere, "errore" no. */
    {
        int r;
        if (quale == 0) {
            r = fat_mkdir(g_mnt[im].mnt, interno);
            if (r == -2) return ERR(EEXIST);
        } else if (quale == 1) {
            r = fat_rmdir(g_mnt[im].mnt, interno);
            if (r == -2) return ERR(ENOTEMPTY);
        } else {
            r = fat_unlink(g_mnt[im].mnt, interno);
        }
        return (r != 0) ? ERR(EIO) : 0;
    }
}

/* Le tre passano tutte da `modifica`, quindi il lucchetto sta qui e non
 * dentro: un solo punto da guardare per sapere che sono serializzate. */
int vfs_mkdir (const char *abs) { int r; fs_prendi_n("mkdir"); r = modifica(abs, 0); fs_rilascia(); return r; }
int vfs_rmdir (const char *abs) { int r; fs_prendi_n("rmdir"); r = modifica(abs, 1); fs_rilascia(); return r; }
int vfs_unlink(const char *abs) { int r; fs_prendi_n("unlink"); r = modifica(abs, 2); fs_rilascia(); return r; }

static int vfs_truncate_nl(const char *abs, uint32_t nuova_dim)
{
    char    interno[VFS_PATH_MAX];
    int     im;
    VfsStat st;

    if (abs == NULL) return ERR(EINVAL);

    im = instrada(abs, interno, sizeof(interno));
    if (im < 0) return ERR(ENOENT);
    if (g_mnt[im].sola_lettura) return ERR(EROFS);
    if (g_mnt[im].tipo == VFS_FS_ISO) return ERR(EROFS);   /* vedi vfs_write */
    if (e_radice(interno))      return ERR(EISDIR);

    /* Una directory non si tronca: le sue dimensioni le decide il driver
     * aggiungendo o togliendo voci, e tagliarla a meta' renderebbe
     * irraggiungibili i file che ci stanno dentro senza cancellarli. */
    if (vfs_stat_nl(abs, &st) != 0) return ERR(ENOENT);
    if (st.is_dir)               return ERR(EISDIR);

    if (g_mnt[im].tipo == VFS_FS_FAT12FD) {
        /* fat12.c non ha un troncamento, e non gliene si aggiunge uno per
         * questa occasione: e' il driver del floppy di avvio, la strada
         * collaudata che non si tocca senza una ragione forte. */
        return ERR(ENOSYS);
    }

    if (g_mnt[im].tipo == VFS_FS_EXT2)
        return (ext2_truncate(g_mnt[im].mnt, interno, nuova_dim) != 0)
             ? ERR(EIO) : 0;

    return (fat_truncate(g_mnt[im].mnt, interno, nuova_dim) != 0)
         ? ERR(EIO) : 0;
}

static void vfs_sync_nl(void)
{
    int i;

    fat12_sync();

    /* Ogni montaggio fat.c ha la propria cache sporca. Riversarne solo
     * alcuni lascerebbe un volume coerente e un altro no, senza che
     * l'utente possa sapere quale. */
    for (i = 0; i < VFS_MAX_MOUNT; i++) {
        if (!g_mnt[i].usato) continue;
        if (g_mnt[i].tipo == VFS_FS_FAT)  fat_sync(g_mnt[i].mnt);
        if (g_mnt[i].tipo == VFS_FS_EXT2) ext2_sync(g_mnt[i].mnt);
    }
}

/* =============================================================================
 * I gusci pubblici — prendono il lucchetto, chiamano l'implementazione,
 * lo rilasciano. Sono qui in fondo e non sparsi nel file perche' l'unica
 * cosa che aggiungono e' quella, e vederli in fila rende evidente che non
 * ne manchi uno. Vedi il commento su fs_prendi() per il perche'.
 * ============================================================================= */
int vfs_mount(const char *dev, const char *punto, int sola_lettura)
{
    int r; fs_prendi_n("mount"); r = vfs_mount_nl(dev, punto, sola_lettura); fs_rilascia(); return r;
}

int vfs_umount(const char *punto)
{
    int r; fs_prendi_n("umount"); r = vfs_umount_nl(punto); fs_rilascia(); return r;
}

int vfs_open(const char *abs, uint32_t flags)
{
    int r; fs_prendi_n("open"); r = vfs_open_nl(abs, flags); fs_rilascia(); return r;
}

int vfs_read(int h, void *buf, uint32_t size, uint32_t offset)
{
    int r; fs_prendi_n("read"); r = vfs_read_nl(h, buf, size, offset); fs_rilascia(); return r;
}

int vfs_write(int h, const void *buf, uint32_t size, uint32_t offset)
{
    int r; fs_prendi_n("write"); r = vfs_write_nl(h, buf, size, offset); fs_rilascia(); return r;
}

int vfs_close(int h)
{
    int r; fs_prendi_n("close"); r = vfs_close_nl(h); fs_rilascia(); return r;
}

int vfs_dup(int h)
{
    int r; fs_prendi_n("dup"); r = vfs_dup_nl(h); fs_rilascia(); return r;
}

int vfs_stat(const char *abs, VfsStat *st)
{
    int r; fs_prendi_n("stat"); r = vfs_stat_nl(abs, st); fs_rilascia(); return r;
}

int vfs_fstat(int h, VfsStat *st)
{
    int r; fs_prendi_n("fstat"); r = vfs_fstat_nl(h, st); fs_rilascia(); return r;
}

int vfs_readdir(const char *abs, VfsDirEntry *out, uint32_t max,
                uint32_t *count, uint32_t start)
{
    int r; fs_prendi_n("readdir"); r = vfs_readdir_nl(abs, out, max, count, start); fs_rilascia(); return r;
}

int vfs_truncate(const char *abs, uint32_t nuova_dim)
{
    int r; fs_prendi_n("truncate"); r = vfs_truncate_nl(abs, nuova_dim); fs_rilascia(); return r;
}

void vfs_sync(void)
{
    fs_prendi_n("sync"); vfs_sync_nl(); fs_rilascia();
}

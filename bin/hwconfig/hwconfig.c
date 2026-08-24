/* =============================================================================
 * bin/hwconfig/hwconfig.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Guarda cosa c'e' nella macchina e scrive /boot/kernel.cfg e
 * /boot/autoexec.sh di conseguenza.
 *
 *     hwconfig            guarda, propone, chiede, scrive
 *     hwconfig -n         guarda e basta, non scrive niente
 *     hwconfig /disco     configura il sistema installato in /disco
 *
 * E il modo che sceglie i driver invece della configurazione:
 *
 *     hwconfig -d /disco  sonda i driver del CD e installa in /disco/dev
 *                         solo quelli che trovano il proprio hardware
 *     hwconfig -d -n      sonda e riferisce, senza copiare niente
 *
 * Vedi il blocco «I DRIVER» piu' sotto per come funziona la sonda. E' il
 * modo che `install` chiama al posto della vecchia copia in blocco di
 * /dev — perche' un disco installato deve ritrovarsi i driver che su
 * QUELLA macchina funzionano, non tutti quelli che stavano sul CD.
 *
 * -----------------------------------------------------------------------------
 * PERCHE' ESISTE
 *
 * `kernel.cfg` e' un file che si scrive a mano, e per scriverlo bisogna
 * sapere che i dischi si chiamano hd0p1, che i punti di montaggio non
 * devono esistere, che i moduli sono processi ring3 e che l'ordine dei
 * comandi di rete non e' modificabile. Sono tutte cose vere, tutte
 * documentate, e tutte da leggere PRIMA di poter accendere una macchina.
 *
 * Questo programma le sa gia'. Guarda cosa c'e', propone la
 * configurazione che ne discende, e la scrive se gli si dice di si'.
 *
 * -----------------------------------------------------------------------------
 * ! MOSTRA PRIMA, CHIEDE POI, SCRIVE PER ULTIMO
 *
 * E' la stessa forma di `install -a`, e per la stessa ragione: chi
 * risponde deve poter vedere COSA cambia prima di dire di si'. Un
 * programma che riscrive la configurazione di avvio senza mostrare cosa
 * ci mette e' esattamente il programma che un utente inesperto non deve
 * lanciare.
 *
 * ! IL FILE PRECEDENTE NON SI PERDE. Va in `kernel.cfg.bak` prima che si
 * scriva una riga del nuovo. E' l'unica cosa che rende la proposta
 * accettabile: se la macchina non riparte, il file di prima e' li'
 * accanto e si rimette con `rename`.
 *
 * ! IL NUOVO FILE E' GENERATO, NON MODIFICATO. Il kernel.cfg che viene
 * con il sistema e' lungo duecento righe di spiegazioni; conservarle
 * vorrebbe dire un parser INI che le rimette a posto, cioe' un programma
 * molto piu' grande di questo e con molti piu' modi di sbagliare. Quello
 * che si scrive qui e' corto, commentato quanto basta, e sostituisce il
 * precedente per intero. Detto in chiaro prima di chiedere.
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"
#include "kbd_proto.h"
#include "rete.h"

#define PERC_MAX    256
#define RIGHE_MAX   64

/* =============================================================================
 * Quello che si e' trovato
 * ============================================================================= */
typedef struct {
    int  cd;                        /* c'e' un lettore ottico */
    char cd_nome[BLKINFO_NOME_MAX];

    int  kbd;                       /* /dev/kbd.drv esiste */
    char keymap[KBD_MAP_NOME_MAX];  /* la disposizione in uso */

    int  rete;                      /* c'e' una scheda Ethernet sul PCI */
    int  rete_ignota;               /* c'e' ma non si e' potuto chiedere */

    /* ! QUALE scheda, e non solo SE. Fino al 24 agosto 2026 qui bastava un
     * si'/no, perche' l'autoexec generato scriveva `netdetect -c` e chi
     * sceglieva il driver era quel programma, a ogni avvio. Adesso il driver
     * finisce in [modules] di kernel.cfg, cioe' la scelta si fa UNA VOLTA, qui.
     *
     * La tabella dei modelli non e' stata copiata: sta in lib/rete.c, e la
     * leggono sia questo programma sia netdetect. Vedi il commento la'. */
    char rete_modello[64];
    char rete_driver[64];           /* vuoto = nessun driver per questa scheda */

    /* Partizioni con un filesystem riconosciuto, candidate al montaggio. */
    int  n_vol;
    char vol_dev[8][BLKINFO_NOME_MAX];
    char vol_punto[8][MOUNTINFO_PUNTO_MAX];
    unsigned int vol_fs[8];
    char vol_etichetta[8][12];

    /* Il primo volume scrivibile: diventa TMPDIR, che serve al
     * compilatore e a chiunque usi mkstemp. */
    int  primo_scrivibile;
} Trovato;

static Trovato g_t;

/* Il dispositivo che regge il sistema che stiamo configurando: al prossimo
 * avvio sara' la RADICE.
 *
 * ! NON VA MESSO IN [mount], e non perche' rompa qualcosa: il kernel se ne
 * accorge da solo e stampa «e' gia' montato altrove (e' la radice)». Ma e'
 * una riga che non serve dentro un file che qualcuno leggera' per capire
 * come e' fatta la sua macchina, e una riga che non serve in un file di
 * configurazione e' una domanda in piu' a cui rispondere. */
static char g_dev_radice[BLKINFO_NOME_MAX] = "";

static void trova_radice(const char *bersaglio)
{
    MountInfo m[16];
    int       n, i;
    const char *cercato = (bersaglio && bersaglio[0]) ? bersaglio : "/";

    n = mountinfo(m, 16, 0);
    if (n <= 0) return;

    for (i = 0; i < n; i++) {
        if (strcmp(m[i].punto, cercato) == 0) {
            strncpy(g_dev_radice, m[i].dev, sizeof(g_dev_radice) - 1);
            return;
        }
    }
}

/* =============================================================================
 * Rilevamento
 * ============================================================================= */

/* I nomi che nella radice esistono gia'. Un punto di montaggio NON deve
 * esistere: e' virtuale, e il VFS lo aggiunge lui.
 *
 * ! SENZA QUESTO CONTROLLO UN'ETICHETTA SFORTUNATA ROMPE L'AVVIO. Un
 * volume etichettato "boot" o "bin" darebbe `/boot = hd0p1`, che il
 * kernel rifiuta al PASSO 13d — un [WARN] a ogni accensione per un
 * montaggio che non poteva funzionare. E chi legge quel [WARN] non ha
 * nessun motivo di sospettare l'etichetta del disco. */
static int punto_occupato(const char *punto)
{
    static const char *riservati[] = {
        "/bin", "/dev", "/lib", "/boot", "/exos", "/doc", NULL
    };
    int i;

    for (i = 0; riservati[i]; i++)
        if (strcmp(punto, riservati[i]) == 0) return 1;

    /* E qualunque altra cosa ci sia davvero: chiederlo costa una stat e
     * copre i nomi che non sappiamo. */
    return (access(punto, F_OK) == 0);
}

/* ! UN NOME DI PUNTO PER VOLTA, E MAI DUE UGUALI. Due voci con lo stesso
 * punto in [mount] sono un conflitto che il kernel rifiuta al PASSO 13d,
 * e il sintomo — un volume che non si monta — non nomina la causa. */
static void punto_per(char *dst, unsigned int max, const char *etichetta,
                      int indice)
{
    int k;

    if (etichetta[0] != '\0' && etichetta[0] != ' ') {
        unsigned int i = 0;

        dst[i++] = '/';
        while (etichetta[i - 1] != '\0' && i < max - 1 && etichetta[i - 1] != ' ') {
            char c = etichetta[i - 1];

            /* minuscolo: su ext2 BIN e bin sono due directory diverse, e
             * un punto di montaggio in maiuscolo e' scomodo da digitare */
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            dst[i] = c;
            i++;
        }
        dst[i] = '\0';
        if (i > 1 && !punto_occupato(dst)) return;
    }

    /* Ripiego: /disco, /disco2, /disco3... fino a trovarne uno libero. */
    for (k = indice; k < indice + 8; k++) {
        if (k == 0) strncpy(dst, "/disco", max);
        else        snprintf(dst, max, "/disco%d", k + 1);
        dst[max - 1] = '\0';
        if (!punto_occupato(dst)) return;
    }
}

static void cerca_volumi(void)
{
    BlkInfo b[16];
    int     n, i;

    n = blkinfo(b, 16, 0);
    if (n < 0) return;

    for (i = 0; i < n && g_t.n_vol < 8; i++) {
        DiskInfo di;
        unsigned int u, p;

        if (strncmp(b[i].nome, "cd", 2) == 0) {
            g_t.cd = 1;
            strncpy(g_t.cd_nome, b[i].nome, sizeof(g_t.cd_nome) - 1);
            continue;
        }

        /* tipo 3 = partizione: le uniche che ha senso montare. Il disco
         * intero (tipo 2) non ha un filesystem, ha una tabella. */
        if (b[i].tipo != 3) continue;

        /* Il numero dell'unita' sta nel nome: hd0p1 -> unita' 0. */
        if (b[i].nome[0] != 'h' || b[i].nome[1] != 'd') continue;
        u = (unsigned int)(b[i].nome[2] - '0');
        if (u > 3) continue;

        if (diskinfo(u, &di) != 0 || !di.presente) continue;

        for (p = 0; p < di.n_part && g_t.n_vol < 8; p++) {
            char atteso[BLKINFO_NOME_MAX];

            snprintf(atteso, sizeof(atteso), "hd%up%u", u, di.part[p].numero);
            if (strcmp(atteso, b[i].nome) != 0) continue;

            /* ! SOLO I FILESYSTEM RICONOSCIUTI. Una partizione con
             * fs_tipo 0 non e' vuota: e' non inizializzata, oppure ha
             * sopra qualcosa che questo sistema non legge. Metterla in
             * [mount] darebbe un [WARN] a ogni avvio per un volume che
             * non si sarebbe potuto montare comunque. */
            if (di.part[p].fs_tipo == 0 || di.part[p].fs_tipo == 255) break;

            /* Il volume che regge il sistema configurato sara' la radice:
             * non si monta due volte. */
            if (g_dev_radice[0] && strcmp(g_dev_radice, b[i].nome) == 0) break;

            strncpy(g_t.vol_dev[g_t.n_vol], b[i].nome,
                    sizeof(g_t.vol_dev[0]) - 1);
            strncpy(g_t.vol_etichetta[g_t.n_vol], di.part[p].fs_etichetta,
                    sizeof(g_t.vol_etichetta[0]) - 1);
            g_t.vol_fs[g_t.n_vol] = di.part[p].fs_tipo;
            punto_per(g_t.vol_punto[g_t.n_vol], MOUNTINFO_PUNTO_MAX,
                      g_t.vol_etichetta[g_t.n_vol], g_t.n_vol);

            if (g_t.primo_scrivibile < 0 && !b[i].sola_lettura)
                g_t.primo_scrivibile = g_t.n_vol;

            g_t.n_vol++;
            break;
        }
    }
}

/* La scheda di rete: si chiede al servizio PCI quali dispositivi Ethernet
 * ci sono, e per ognuno si guarda in tabella se sappiamo guidarlo.
 *
 * ! ADESSO SI GUARDA ANCHE QUALE MODELLO SIA, e fino al 24 agosto 2026 non si
 * faceva: bastava sapere SE c'era una scheda, perche' il driver lo sceglieva
 * `netdetect -c` dall'autoexec, a ogni avvio. Ma l'autoexec lo esegue la shell
 * della prima console, cioe' UNO PER ACCESSO e con i privilegi di chi entra:
 * un utente normale non puo' caricare un driver, e la rete si accendeva solo
 * se il primo a entrare era root. In [modules] di kernel.cfg il driver lo
 * carica il kernel, all'avvio, una volta — ma bisogna sapere quale, e la
 * decisione si sposta qui.
 *
 * ! LA TABELLA NON E' STATA COPIATA QUI DENTRO: sta in lib/rete.c e la leggono
 * sia netdetect sia questo programma. Era il motivo per cui prima non si
 * guardava il modello, ed era un motivo giusto — solo che la risposta non e'
 * «non guardarlo», e' «tienila in un posto solo».
 * --------------------------------------------------------------------------- */
static int chiedi_pci(int pid, unsigned int ordinale, PciDispositivo *out)
{
    PciRichiesta  r;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           t;

    r.ordinale    = ordinale;
    r.classe      = PCI_CLASSE_RETE;
    r.sottoclasse = PCI_SOTTO_ETHERNET;
    r.venditore   = PCI_QUALUNQUE;
    r.dispositivo = PCI_QUALUNQUE;

    if (ipc_send(pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0) return -1;

    for (t = 0; t < 8; t++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return -1;
        if ((int)meta.sender_pid != pid) continue;   /* non e' la nostra risposta */
        if (meta.tipo == PCI_MSG_FINE) return 0;
        if (meta.tipo == PCI_MSG_DISPOSITIVO && meta.len >= sizeof(*out)) {
            memcpy(out, buf, sizeof(*out));
            return 1;
        }
        return -1;
    }
    return -1;
}

static void cerca_rete(void)
{
    int            pid = ipc_lookup(PCI_SERVIZIO);
    PciDispositivo d;
    unsigned int   ord;
    int            i;

    if (pid <= 0) {
        /* Il servizio non c'e': lo si prova ad accendere, perche' e'
         * esattamente quello che l'utente dovrebbe fare a mano e non sa.
         * Se il driver non c'e' (sistema da floppy) non e' un errore:
         * significa solo che la parte di rete non si puo' configurare. */
        char *argv[2];

        argv[0] = "/dev/pci.drv";
        argv[1] = NULL;
        if (spawn("/dev/pci.drv", argv) > 0) {
            for (i = 0; i < 20; i++) {
                usleep(100 * 1000);
                pid = ipc_lookup(PCI_SERVIZIO);
                if (pid > 0) break;
            }
        }
    }

    if (pid <= 0) { g_t.rete_ignota = 1; return; }

    for (ord = 0; ord < 16; ord++) {
        const ReteScheda *n;
        int               esito = chiedi_pci(pid, ord, &d);

        if (esito < 0) { g_t.rete_ignota = 1; return; }
        if (esito == 0) break;                  /* finite */

        g_t.rete = 1;

        /* ! LA PRIMA CHE SAPPIAMO GUIDARE, e le altre si lasciano stare: il
         * sistema ha un solo servizio 'rete0', quindi due driver caricati si
         * contenderebbero quel nome e il secondo morirebbe con un errore che
         * nessuno andrebbe a leggere. */
        if (g_t.rete_driver[0] != '\0') continue;

        n = rete_riconosci(d.venditore, d.dispositivo);
        if (n == NULL) continue;

        strncpy(g_t.rete_modello, n->modello, sizeof(g_t.rete_modello) - 1);
        if (n->driver != NULL)
            strncpy(g_t.rete_driver, n->driver, sizeof(g_t.rete_driver) - 1);
    }
}

/* La disposizione della tastiera.
 *
 * ! SI CHIEDE, NON SI DECIDE. hwconfig riscrive kernel.cfg per intero:
 * senza questa domanda, una macchina configurata `keymap = it` si
 * ritroverebbe `us` dopo un hwconfig — cioe' il programma che serve a
 * togliere fatica avrebbe rotto in silenzio l'unica impostazione che si
 * nota subito digitando.
 *
 * La verita' e' nel DRIVER, non nel file: se qualcuno ha dato `keymap fr`
 * a macchina accesa e poi lancia hwconfig, la sua intenzione e' 'fr'.
 * Il file e' solo il ripiego per quando il driver non c'e'. */
static void cerca_keymap(void)
{
    int           pid = ipc_lookup(KBD_SERVICE_NAME);
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    KbdMapInfo    info;
    int           i;

    strncpy(g_t.keymap, "us", sizeof(g_t.keymap) - 1);

    if (pid > 0 && ipc_send(pid, KBD_MSG_GETMAP, NULL, 0) >= 0) {
        for (i = 0; i < 8; i++) {
            if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) break;
            if ((int)meta.sender_pid != pid) continue;
            if (meta.tipo != KBD_MSG_MAPINFO || meta.len < sizeof(info)) break;

            memcpy(&info, buf, sizeof(info));
            strncpy(g_t.keymap, info.attiva, sizeof(g_t.keymap) - 1);
            return;
        }
    }

    /* Nessun driver: si tiene quello che dice la configurazione attuale. */
    if (getconf("keymap", g_t.keymap, sizeof(g_t.keymap)) < 0)
        strncpy(g_t.keymap, "us", sizeof(g_t.keymap) - 1);
}

/* Il sistema che si sta configurando: vuoto = questo, altrimenti il punto in
 * cui e' montato. Serve a guardare i file DEL BERSAGLIO — un driver che c'e'
 * qui e non la' e' una riga di kernel.cfg che all'avvio non si carica. */
static char g_bersaglio[PERC_MAX] = "";

/* Vero se `assoluto` (un percorso come si vedra' all'avvio, es. /dev/ip.drv)
 * esiste nel sistema che stiamo configurando. */
static int c_e_nel_bersaglio(const char *assoluto)
{
    char         perc[PERC_MAX];
    unsigned int n;

    if (g_bersaglio[0] == '\0') return access(assoluto, F_OK) == 0;

    /* ! LE DUE BARRE NON SI SOMMANO: il punto di montaggio finisce senza
     * barra e il percorso assoluto comincia con la sua, e attaccarli senza
     * guardare darebbe /disk//dev/ip.drv. */
    n = (unsigned int)strlen(g_bersaglio);
    while (n > 1 && g_bersaglio[n-1] == '/') n--;
    snprintf(perc, sizeof(perc), "%.*s%s", (int)n, g_bersaglio, assoluto);
    return access(perc, F_OK) == 0;
}

static void esamina(const char *bersaglio)
{
    memset(&g_t, 0, sizeof(g_t));
    g_t.primo_scrivibile = -1;

    strncpy(g_bersaglio, bersaglio ? bersaglio : "", sizeof(g_bersaglio) - 1);
    g_bersaglio[sizeof(g_bersaglio) - 1] = '\0';

    trova_radice(bersaglio);

    g_t.kbd = c_e_nel_bersaglio("/dev/kbd.drv");
    cerca_keymap();
    cerca_volumi();
    cerca_rete();
}

/* =============================================================================
 * Resoconto
 * ============================================================================= */
static const char *nome_fs(unsigned int t)
{
    switch (t) {
    case 12: return "FAT12";
    case 16: return "FAT16";
    case 32: return "FAT32";
    case 2:  return "ext2";
    default: return "sconosciuto";
    }
}

static void mostra(void)
{
    int i;

    printf("Cosa c'e' in questa macchina\n\n");

    printf("  tastiera   disposizione '%s'%s\n", g_t.keymap,
           g_t.kbd ? "" : " (senza driver vale solo 'us')");
    printf("             %s\n", g_t.kbd
           ? "/dev/kbd.drv — si carica all'avvio, serve alle frecce e a gfedit"
           : "assente: la console usera' la tastiera interna di ripiego");

    if (g_t.cd)
        printf("  lettore    %s — montato all'avvio su /cdrom\n", g_t.cd_nome);
    else
        printf("  lettore    nessuno\n");

    if (g_t.n_vol == 0) {
        printf("  volumi     nessuna partizione con un filesystem leggibile\n");
    } else {
        for (i = 0; i < g_t.n_vol; i++)
            printf("  volume     %-6s %-6s '%s' — montato su %s\n",
                   g_t.vol_dev[i], nome_fs(g_t.vol_fs[i]),
                   g_t.vol_etichetta[i], g_t.vol_punto[i]);
    }

    if (g_t.rete && g_t.rete_driver[0])
        printf("  rete       %s\n             %s — caricato all'avvio dal kernel\n",
               g_t.rete_modello, g_t.rete_driver);
    else if (g_t.rete && g_t.rete_modello[0])
        printf("  rete       %s — driver da scrivere: resta spenta\n",
               g_t.rete_modello);
    else if (g_t.rete)
        printf("  rete       scheda Ethernet sconosciuta: `netdetect` dice il numero\n");
    else if (g_t.rete_ignota)
        printf("  rete       non verificabile: manca /dev/pci.drv (c'e' sul CD di EX-OS)\n");
    else
        printf("  rete       nessuna scheda Ethernet sul bus PCI\n");

    printf("\n");
}

/* =============================================================================
 * Generazione dei file
 * ============================================================================= */
static void unisci(char *dst, const char *a, const char *b)
{
    unsigned int i = 0, j = 0;

    while (a[i] && i < PERC_MAX - 2) { dst[i] = a[i]; i++; }
    while (i > 1 && dst[i-1] == '/') i--;
    dst[i++] = '/';
    while (b[j] && i < PERC_MAX - 1) dst[i++] = b[j++];
    dst[i] = '\0';
}

/* Mette in minuscolo l'ULTIMO componente di un percorso, lasciando stare
 * le directory che lo precedono: quelle esistono gia' con il nome che
 * hanno, e cambiarlo qui vorrebbe dire scrivere in un posto diverso da
 * quello che si e' appena creato.
 *
 * Si parte dall'ultima barra invece che da un conteggio di caratteri
 * perche' unisci() normalizza le barre e la lunghezza del prefisso non e'
 * quella che il chiamante crede. */
static void minuscolo_nome(char *perc)
{
    char *n = perc, *p;

    for (p = perc; *p; p++) if (*p == '/') n = p + 1;
    for (; *n; n++) if (*n >= 'A' && *n <= 'Z') *n = (char)(*n + 32);
}

/* Scrive `testo` in `percorso`, mettendo da parte il file precedente.
 * Ritorna 0, o -1 dicendo perche'. */
static int scrivi_con_copia(const char *percorso, const char *testo)
{
    char bak[PERC_MAX];
    int  fd, n, scritti;

    snprintf(bak, sizeof(bak), "%s.bak", percorso);

    if (access(percorso, F_OK) == 0) {
        /* ! SI CANCELLA IL .bak VECCHIO PRIMA. `rename` di EX-OS non
         * sostituisce la destinazione (torna EEXIST): senza questa riga
         * la copia di sicurezza sarebbe quella della PRIMA esecuzione, e
         * dalla seconda in poi non salverebbe piu' niente — in silenzio. */
        unlink(bak);
        if (rename(percorso, bak) != 0) {
            printf("hwconfig: non riesco a mettere da parte %s\n", percorso);
            return -1;
        }
    }

    fd = open(percorso, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("hwconfig: non riesco a scrivere %s (%d)\n", percorso, fd);
        if (access(bak, F_OK) == 0) rename(bak, percorso);   /* si rimette com'era */
        return -1;
    }

    n       = (int)strlen(testo);
    scritti = (int)write(fd, testo, (unsigned int)n);
    close(fd);

    if (scritti != n) {
        printf("hwconfig: scrittura incompleta di %s (%d di %d byte)\n",
               percorso, scritti, n);
        return -1;
    }
    return 0;
}

/* =============================================================================
 * ! CERTE VOCI SONO DI CHI USA IL SISTEMA, E NON SI PERDONO
 *
 * Questo programma riscrive kernel.cfg PER INTERO — e' scritto anche nella
 * domanda che fa prima di scrivere — perche' quel file rispecchia l'hardware,
 * e l'hardware lo si e' appena guardato. Ma dentro ci sono anche voci che con
 * l'hardware non c'entrano niente, e che nessuno puo' rimettere al posto loro
 * se spariscono qui.
 *
 * ! `login` E' IL CASO PEGGIORE, ed e' il difetto per cui questa funzione
 * esiste: il kernel avvia /bin/login SOLO se la voce c'e' (PASSO 15 di
 * kernel_main.c). Un `hwconfig` dato su una macchina installata la toglieva, e
 * al riavvio dopo si entrava SENZA PASSWORD — su un sistema che al primo
 * avvio aveva detto che l'accesso sarebbe stato obbligatorio. Un programma che
 * serve a togliere fatica non deve spegnere una serratura in silenzio.
 *
 * ! `svga` E' L'ALTRO: lo scrive /bin/svga insieme a un byte dentro Stage 2, e
 * i due devono restare d'accordo. Toglierlo qui vuol dire una macchina che si
 * avvia con la risoluzione che non e' quella dichiarata, e nessuno che sappia
 * dire perche'.
 *
 * ! LE RIGHE COMMENTATE NON CONTANO. kernel.cfg e' pieno di esempi spenti —
 * «# svga = 800x600» — e scambiarne uno per una voce vera vorrebbe dire
 * riportare avanti una decisione che nessuno ha preso.
 *
 * ! E LA SEZIONE SI GUARDA. Le chiavi sono uniche oggi, ma [mount] contiene
 * righe scritte dall'utente, e un punto di montaggio che si chiamasse come una
 * di queste chiavi darebbe una voce copiata nel posto sbagliato.
 * ============================================================================= */
static int voce_di_prima(const char *file, const char *sezione,
                         const char *chiave, char *out, unsigned int max)
{
    static char testo[8192];
    const char *p;
    int         fd, n;

    out[0] = '\0';

    fd = open(file, O_RDONLY);
    if (fd < 0) return 0;
    n = (int)read(fd, testo, sizeof(testo) - 1);
    close(fd);
    if (n < 0) n = 0;
    testo[n] = '\0';

    {
        char dentro[64] = "";

        for (p = testo; *p; ) {
            const char *r = p, *fine;
            char        k[64];
            unsigned int i;

            while (*p && *p != '\n') p++;
            fine = p;
            if (*p) p++;

            while (r < fine && (*r == ' ' || *r == '\t')) r++;
            if (r >= fine || *r == '#' || *r == ';') continue;

            if (*r == '[') {
                r++;
                for (i = 0; i + 1 < sizeof(dentro) && r < fine && *r != ']'; i++)
                    dentro[i] = *r++;
                dentro[i] = '\0';
                continue;
            }

            if (strcmp(dentro, sezione) != 0) continue;

            for (i = 0; i + 1 < sizeof(k) && r < fine &&
                        *r != '=' && *r != ' ' && *r != '\t'; i++)
                k[i] = *r++;
            k[i] = '\0';
            if (strcmp(k, chiave) != 0) continue;

            while (r < fine && (*r == ' ' || *r == '\t')) r++;
            if (r >= fine || *r != '=') continue;
            r++;
            while (r < fine && (*r == ' ' || *r == '\t')) r++;
            while (fine > r && (fine[-1] == ' ' || fine[-1] == '\t' ||
                                fine[-1] == '\r')) fine--;
            if (r >= fine) continue;              /* voce vuota: come assente */

            for (i = 0; i + 1 < max && r < fine; i++) out[i] = *r++;
            out[i] = '\0';
            return 1;
        }
    }
    return 0;
}

static void componi_kernel_cfg(char *out, unsigned int max, const char *vecchio)
{
    char riga[256];
    char v[128];
    int  i;

    out[0] = '\0';

    strncat(out,
        "# =============================================================================\n"
        "# kernel.cfg — scritto da `hwconfig`\n"
        "#\n"
        "# Cosa vuol dire ogni voce sta in /boot/kernel.txt: qui ci sono solo\n"
        "# le impostazioni, perche' questo file ha un tetto di 8191 byte e\n"
        "# oltre quello le sezioni finali vengono ignorate.\n"
        "#\n"
        "# Il file precedente e' in kernel.cfg.bak. Per tornare indietro:\n"
        "#     delete /boot/kernel.cfg\n"
        "#     rename /boot/kernel.cfg.bak /boot/kernel.cfg\n"
        "# =============================================================================\n"
        "\n"
        "[kernel]\n"
        "loglevel    = 3\n"
        "timer_hz    = 100\n"
        "# 0 = avvio silenzioso, 1 = mostra il log di avvio.\n"
        "# Errori e avvisi restano visibili in entrambi i casi.\n"
        "verboseboot = 0\n", max - 1);

    /* ! SI RISCRIVE LA DISPOSIZIONE TROVATA, non 'us'. Questo file lo
     * sostituiamo per intero: senza questa riga una macchina configurata
     * in italiano tornerebbe americana dopo un hwconfig, e il programma
     * che serve a togliere fatica avrebbe rotto in silenzio l'unica
     * impostazione che si nota subito digitando. */
    snprintf(riga, sizeof(riga),
             "\n# Disposizione della tastiera: us it fr de es uk.\n"
             "# Si cambia a caldo con `keymap`; `keymap -p` ristampa questa riga.\n"
             "keymap      = %s\n", g_t.keymap);
    strncat(out, riga, max - 1 - strlen(out));

    /* La risoluzione: non la decide questo programma, la decide /bin/svga
     * insieme a Stage 2. Se c'era, resta. */
    if (voce_di_prima(vecchio, "kernel", "svga", v, sizeof(v))) {
        snprintf(riga, sizeof(riga),
                 "\n# Dichiarata da `svga`: Stage 2 imposta, il kernel confronta.\n"
                 "svga        = %s\n", v);
        strncat(out, riga, max - 1 - strlen(out));
    }

    strncat(out, "\n[boot]\nshell       = /bin/sh\n", max - 1 - strlen(out));

    /* ! L'ACCESSO NON SI SPEGNE RISCRIVENDO UN FILE. Se la voce c'era si
     * riporta com'era — chi ha scritto `login = /bin/altro` sapeva cosa stava
     * facendo — e se non c'era la si mette lo stesso, purche' il programma
     * esista: il kernel lancia /bin/login SOLO se questa riga c'e', e senza si
     * entra senza password su una radice ext2. */
    if (voce_di_prima(vecchio, "boot", "login", v, sizeof(v))) {
        snprintf(riga, sizeof(riga),
                 "\n# Chi si apre su ogni console al posto della shell: chiede\n"
                 "# nome e password, e `exit` torna qui invece di lasciare la\n"
                 "# console morta. Senza questa riga NON si autentica nessuno.\n"
                 "login       = %s\n", v);
        strncat(out, riga, max - 1 - strlen(out));
    } else if (c_e_nel_bersaglio("/bin/login")) {
        strncat(out,
                "\n# Chi si apre su ogni console al posto della shell: chiede\n"
                "# nome e password, e `exit` torna qui invece di lasciare la\n"
                "# console morta. Senza questa riga NON si autentica nessuno.\n"
                "login       = /bin/login\n", max - 1 - strlen(out));
    }

    if (g_t.kbd)
        strncat(out, "modules     = kbd\n", max - 1 - strlen(out));


    strncat(out, "\n[env]\nPATH        = /bin:/dev\nHOME        = /\n"
                 "TERM        = vga\n", max - 1 - strlen(out));

    /* ! TMPDIR SERVE, e non e' un vezzo: mkstemp e il driver del
     * compilatore ci mettono i file di passaggio. Senza, finiscono nella
     * radice — che avviando da CD e' in sola lettura, e allora non
     * finiscono da nessuna parte. */
    if (g_t.primo_scrivibile >= 0) {
        snprintf(riga, sizeof(riga),
                 "# Dove finiscono i file temporanei: deve essere scrivibile.\n"
                 "TMPDIR      = %s\n", g_t.vol_punto[g_t.primo_scrivibile]);
        strncat(out, riga, max - 1 - strlen(out));
    } else if (g_dev_radice[0] != '\0') {
        /* Nessun altro volume: i temporanei vanno nella radice, che e'
         * quella del sistema configurato — e quella si scrive. */
        strncat(out,
                "# Dove finiscono i file temporanei: deve essere scrivibile.\n"
                "TMPDIR      = /\n", max - 1 - strlen(out));
    }

    /* =====================================================================
     * ! I DRIVER STANNO QUI, NON IN autoexec.sh — dal 24 agosto 2026
     *
     * L'autoexec lo esegue la shell della prima console, e con `login` in
     * mezzo quella shell nasce a OGNI ACCESSO e con l'identita' di chi entra.
     * Due conseguenze, tutt'e due sbagliate: i driver di rete si riaccendevano
     * a ogni `exit` seguito da un accesso — trovando i servizi gia' registrati
     * e stampando una fila di errori — e se il primo a entrare non era root
     * non si accendevano affatto, perche' un utente normale non puo' caricare
     * un driver.
     *
     * Le voci di [modules] le carica il KERNEL: una volta sola, prima di
     * qualunque console, da root. E' il posto giusto per tutto cio' che e' del
     * sistema e non di chi lo usa.
     *
     * ! L'ORDINE E' QUELLO DELLA CATENA — bus, scheda, stack, indirizzo —
     * perche' ognuno serve al successivo. Ma il kernel non aspetta che uno sia
     * pronto prima di avviare l'altro: li mette in coda tutti insieme, e a
     * mettere le cose in fila e' l'attesa che ciascuno fa del proprio
     * fornitore (ipc_attendi, in lib/libc.c). L'ordine qui resta quello giusto
     * lo stesso: fa partire per prima la parte che ha meno da aspettare.
     * ===================================================================== */
    if (g_t.kbd || (g_t.rete && g_t.rete_driver[0])) {
        strncat(out,
            "\n# Caricati all'avvio come processi ring3, in quest'ordine.\n"
            "[modules]\n", max - 1 - strlen(out));
    }

    if (g_t.kbd)
        strncat(out, "kbd         = /dev/kbd.drv\n", max - 1 - strlen(out));

    if (g_t.rete && g_t.rete_driver[0]) {
        int pci  = c_e_nel_bersaglio("/dev/pci.drv");
        int drv  = c_e_nel_bersaglio(g_t.rete_driver);
        int ip   = c_e_nel_bersaglio("/dev/ip.drv");
        int dhcp = c_e_nel_bersaglio("/bin/dhcp");

        if (pci && drv && ip) {
            snprintf(riga, sizeof(riga),
                     "\n# La rete: %s\n"
                     "# `ipcfg` la mostra, `ping` la prova. Per spegnerla basta\n"
                     "# commentare queste righe con un '#'.\n"
                     "pci         = /dev/pci.drv\n"
                     "rete        = %s\n"
                     "ip          = /dev/ip.drv\n",
                     g_t.rete_modello, g_t.rete_driver);
            strncat(out, riga, max - 1 - strlen(out));

            /* ! L'INDIRIZZO NON E' UN DRIVER, e sta qui lo stesso: e' l'ultimo
             * anello della stessa catena, e l'unico posto dove il resto puo'
             * accendersi. Senza un server DHCP fallisce e basta — l'indirizzo
             * si mette allora a mano con `ipcfg`, che scrive nello stack gia'
             * avviato dalle righe qui sopra. */
            if (dhcp)
                strncat(out,
                        "# Un indirizzo dal DHCP, se c'e' un server. Senza:\n"
                        "#   ipcfg -a 192.168.1.10 -m 255.255.255.0 -g 192.168.1.1\n"
                        "dhcp        = /bin/dhcp\n", max - 1 - strlen(out));
        } else {
            /* ! SI DICE COSA MANCA INVECE DI SCRIVERE UNA RIGA CHE NON PARTE.
             * Un modulo il cui file non c'e' il kernel lo salta con un avviso
             * che scorre via all'avvio: qui invece lo si legge. */
            snprintf(riga, sizeof(riga),
                     "\n# La rete NON si accende all'avvio: manca%s%s%s.\n"
                     "# Sono sul CD di EX-OS; si installano con `hwconfig -d`.\n",
                     pci ? "" : " /dev/pci.drv",
                     drv ? "" : (pci ? " il driver della scheda"
                                     : " e il driver della scheda"),
                     ip  ? "" : ((pci && drv) ? " /dev/ip.drv" : " e /dev/ip.drv"));
            strncat(out, riga, max - 1 - strlen(out));
        }
    }

    strncat(out,
        "\n# Montaggi automatici. La forma e'  punto = dispositivo, e il punto\n"
        "# NON deve esistere gia': lo crea il VFS.\n"
        "[mount]\n", max - 1 - strlen(out));

    if (g_t.cd) {
        snprintf(riga, sizeof(riga),
                 "# Un lettore vuoto o assente non e' un errore: il montaggio\n"
                 "# viene saltato e l'avvio prosegue.\n"
                 "/cdrom      = %s\n", g_t.cd_nome);
        strncat(out, riga, max - 1 - strlen(out));
    }

    for (i = 0; i < g_t.n_vol; i++) {
        snprintf(riga, sizeof(riga), "%-11s = %s\n",
                 g_t.vol_punto[i], g_t.vol_dev[i]);
        strncat(out, riga, max - 1 - strlen(out));
    }

    if (!g_t.cd && g_t.n_vol == 0)
        strncat(out, "# Niente da montare: nessun lettore e nessun volume leggibile.\n",
                max - 1 - strlen(out));
}

static void componi_autoexec(char *out, unsigned int max)
{
    out[0] = '\0';

    strncat(out,
        "# =============================================================================\n"
        "# autoexec.sh — scritto da `hwconfig`\n"
        "#\n"
        "# Una riga = un comando, come se fosse digitato.\n"
        "#   #           commento\n"
        "#   @           esegue una riga senza stamparla\n"
        "#   !silenced   da qui in poi i comandi non si vedono (il loro\n"
        "#               risultato si')\n"
        "#   !verbose    si tornano a vedere\n"
        "#\n"
        "# Se un comando qui dentro si blocca: Alt+F2 da' sempre una shell\n"
        "# pulita, e `autoexec = 0` in kernel.cfg salta del tutto questo file.\n"
        "# =============================================================================\n"
        "\n"
        "!silenced\n"
        "\n", max - 1);

    /* ! LA RETE NON STA PIU' QUI, e non e' un alleggerimento: e' dove doveva
     * stare. Questo file lo esegue la shell della prima console, cioe' a ogni
     * accesso e con l'identita' di chi entra — un utente normale non puo'
     * caricare un driver, e chi rientrava dopo un `exit` riaccendeva servizi
     * gia' accesi. I quattro anelli della catena sono voci di [modules] in
     * kernel.cfg dal 24 agosto 2026: li avvia il kernel, una volta, da root.
     *
     * Qui restano i comandi di CHI USA la macchina, che e' quello che un
     * autoexec e' sempre stato. Di suo, appena installato, non ne ha nessuno. */
    if (g_t.rete && g_t.rete_driver[0]) {
        strncat(out,
            "# La rete si accende da sola: i driver stanno in [modules] di\n"
            "# /boot/kernel.cfg. `ipcfg` la mostra, `ping` la prova.\n"
            "@echo Sistema pronto.\n", max - 1 - strlen(out));
    } else if (g_t.rete) {
        strncat(out,
            "# C'e' una scheda Ethernet ma non il driver per guidarla:\n"
            "# `netdetect` dice il numero del modello, `netdetect -t` la\n"
            "# tabella di quelli riconosciuti.\n"
            "@echo Sistema pronto.\n", max - 1 - strlen(out));
    } else {
        strncat(out,
            "# Nessuna scheda Ethernet trovata: niente da accendere.\n"
            "# Se ne aggiungi una, rilancia `hwconfig`.\n"
            "@echo Sistema pronto.\n", max - 1 - strlen(out));
    }
}

/* =============================================================================
 * main
 * ============================================================================= */
/* =============================================================================
 * I DRIVER: si sondano, e si installa solo quello che risponde
 *
 * Sul CD di EX-OS c'e' /drivers: il catalogo completo, che contiene anche
 * — anzi, soprattutto — driver che su QUESTA macchina non servono. Qui
 * dentro ognuno viene lanciato con `-i`, la convenzione comune a tutti i
 * driver del sistema: sonda il tuo hardware, di' cosa hai trovato, esci
 * con 0 se servi su questa macchina e con 1 se no. Chi risponde 0 finisce
 * in <radice>/dev, gli altri restano sul CD.
 *
 * ! NON C'E' UN ELENCO DI DRIVER, DA NESSUNA PARTE, e non e' una
 * dimenticanza. Un elenco scritto qui sarebbe una seconda verita' accanto
 * al contenuto della directory, e le due divergono al primo driver
 * aggiunto o tolto: si finirebbe per cercare un driver che non c'e' piu',
 * o per ignorarne uno nuovo che nessuno si e' ricordato di annotare. La
 * domanda si fa al driver, che e' l'unico a sapersi rispondere.
 *
 * ! UN DRIVER CHE NON PARTE E' UN DRIVER CHE NON SERVE. floppy.drv e'
 * ancora un modulo ET_DYN scritto contro i simboli del kernel, e spawn()
 * lo rifiuta. Prima veniva copiato lo stesso — `install` copiava /dev in
 * blocco — e sul disco restava un file che nessuno poteva caricare.
 *
 * ! IL SERVIZIO PCI DEVE ESSERE ACCESO PRIMA. Le schede di rete non si
 * cercano da sole: chiedono a /dev/pci.drv, e senza quello rispondono
 * «non trovata» anche quando la scheda c'e'. Ci pensa esamina(), che
 * l'accende gia' per conto suo — ed e' il motivo per cui la sonda dei
 * driver va DOPO l'esame della macchina, non prima.
 * ============================================================================= */

#define DRV_MAX       32
#define DRV_NOME_MAX  64
#define CATALOGO_PRED "/cdrom/drivers"

/* Dove cercare il catalogo quando -s non lo dice. Due voci, e servono
 * entrambe: avviando DAL CD la radice e' il CD stesso e il catalogo sta
 * in /drivers; con il CD montato da un sistema gia' installato sta in
 * /cdrom/drivers. Non e' un elenco di driver — quello non esiste — e'
 * un elenco di posti dove guardare, come un PATH. */
/* ! /dev E' L'ULTIMA SPIAGGIA, E SERVE. Installando da FLOPPY non c'e'
 * nessun CD e quindi nessun catalogo: senza questa voce `install` non
 * copiava piu' NEMMENO UN driver sul disco — kbd.drv compreso — e il
 * sistema installato ripiegava sull'handler IRQ1 dentro il kernel senza
 * che niente lo spiegasse. Prima della sonda `install` copiava /dev in
 * blocco e il caso non si poneva.
 *
 * /dev del sistema in esecuzione e' un catalogo legittimo: contiene i
 * driver che questo supporto porta con se'. Vengono sondati come tutti gli
 * altri, quindi si installa comunque solo cio' che serve. */
static const char *g_catalogo_pred[] = { CATALOGO_PRED, "/drivers", "/dev", NULL };

static char g_catalogo[PERC_MAX] = "";
static char g_drv[DRV_MAX][DRV_NOME_MAX];
static int  g_drv_serve[DRV_MAX];
static int  g_n_drv = 0;

/* ! SENZA DISTINZIONE FRA MAIUSCOLE E MINUSCOLE. I nomi arrivano dal
 * filesystem: da un CD ISO 9660 con Joliet sono minuscoli, da una FAT sono
 * MAIUSCOLI. Un confronto sensibile alle maiuscole trovava i driver sul CD
 * e nessuno su un floppy o su un disco FAT — cioe' falliva esattamente
 * nell'installazione da floppy, dove il catalogo E' /dev del supporto. */
static int finisce_in_drv(const char *n)
{
    int l = (int)strlen(n);
    const char *e = n + l - 4;

    if (l <= 4) return 0;
    return (e[0] == '.' &&
            (e[1] == 'd' || e[1] == 'D') &&
            (e[2] == 'r' || e[2] == 'R') &&
            (e[3] == 'v' || e[3] == 'V'));
}

/* Lancia `<percorso> -i` e rende 1 se il driver dice di servire qui. */
static int sonda_driver(const char *percorso)
{
    char *argv[3];
    int   pid, stato = -1;

    argv[0] = (char *)percorso;
    argv[1] = "-i";
    argv[2] = NULL;

    pid = spawn(percorso, argv);
    if (pid < 0) {
        printf("     non si avvia (%s)\n", strerror(errno));
        return 0;
    }
    if (waitpid(pid, &stato, 0) < 0) {
        printf("     non se ne conosce l'esito\n");
        return 0;
    }
    /* ! `stato` E' GIA' IL CODICE D'USCITA. EX-OS non consegna segnali,
     * quindi non c'e' l'intero impacchettato di Unix da srotolare con
     * WEXITSTATUS: il kernel ci scrive il numero e basta. */
    return (stato == 0);
}

/* Riempie g_drv[] con i .drv del catalogo. Rende il numero trovato. */
static int cerca_driver(void)
{
    DIR           *d;
    struct dirent *e;

    g_n_drv = 0;

    if (g_catalogo[0] == '\0') {
        int k;

        for (k = 0; g_catalogo_pred[k]; k++) {
            DIR *prova = opendir(g_catalogo_pred[k]);

            if (prova) {
                closedir(prova);
                strncpy(g_catalogo, g_catalogo_pred[k], sizeof(g_catalogo) - 1);
                break;
            }
        }
    }

    d = g_catalogo[0] ? opendir(g_catalogo) : NULL;
    if (!d) {
        printf("Catalogo dei driver: non trovato.\n");
        printf("  E' la directory `drivers` del CD di EX-OS. Cercata in:\n");
        {
            int k;
            for (k = 0; g_catalogo_pred[k]; k++)
                printf("    %s\n", g_catalogo_pred[k]);
        }
        printf("  Monta il CD con `mount cd0 /cdrom`, oppure indica la\n");
        printf("  directory con -s.\n");
        return 0;
    }

    while ((e = readdir(d)) != NULL && g_n_drv < DRV_MAX) {
        if (!finisce_in_drv(e->d_name)) continue;
        strncpy(g_drv[g_n_drv], e->d_name, DRV_NOME_MAX - 1);
        g_drv[g_n_drv][DRV_NOME_MAX - 1] = '\0';
        g_drv_serve[g_n_drv] = 0;
        g_n_drv++;
    }
    closedir(d);
    return g_n_drv;
}

/* Accende il servizio PCI se non c'e', prendendolo dal catalogo.
 *
 * ! NON E' UN DRIVER TRATTATO A PARTE PER CAPRICCIO: e' l'unico da cui
 * dipendono le sonde degli altri. ne2k e pcnet non frugano il bus da
 * soli, chiedono a questo servizio, e senza rispondono «scheda non
 * trovata» anche su una macchina che la scheda ce l'ha — cioe' danno la
 * risposta sbagliata proprio nel caso che conta. esamina() lo accende
 * gia', ma cerca /dev/pci.drv: avviando DAL CD la radice e' il supporto
 * di avvio, dove quel file non c'e', e allora lo si prende dal catalogo. */
static void assicura_pci(void)
{
    char  percorso[PERC_MAX];
    char *argv[2];
    int   i;

    if (ipc_lookup(PCI_SERVIZIO) > 0) return;

    unisci(percorso, g_catalogo, "pci.drv");
    if (access(percorso, F_OK) != 0) return;

    argv[0] = percorso;
    argv[1] = NULL;
    if (spawn(percorso, argv) <= 0) return;

    for (i = 0; i < 20; i++) {
        usleep(100 * 1000);
        if (ipc_lookup(PCI_SERVIZIO) > 0) return;
    }
}

/* Sonda tutti i driver trovati e stampa l'esito. Rende quanti servono. */
static int sonda_tutti(void)
{
    char percorso[PERC_MAX];
    int  i, quanti = 0;

    assicura_pci();

    printf("\nDriver nel catalogo %s\n\n", g_catalogo);

    for (i = 0; i < g_n_drv; i++) {
        unisci(percorso, g_catalogo, g_drv[i]);
        printf("  %s\n", g_drv[i]);
        g_drv_serve[i] = sonda_driver(percorso);
        printf("     -> %s\n\n",
               g_drv_serve[i] ? "SERVE, verra' installato"
                              : "non serve su questa macchina");
        if (g_drv_serve[i]) quanti++;
    }

    if (g_n_drv == 0)
        printf("  (nessun file .drv)\n\n");

    return quanti;
}

/* Copia un file. Sovrascrive: chi installa vuole la versione del CD. */
static int copia_driver(const char *src, const char *dst)
{
    static char buf[4096];
    int fs, fd, n, scritti, w;

    fs = open(src, O_RDONLY);
    if (fs < 0) { printf("  %s: %s\n", src, strerror(errno)); return -1; }

    fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf("  %s: %s\n", dst, strerror(errno));
        close(fs);
        return -1;
    }

    while ((n = (int)read(fs, buf, sizeof(buf))) > 0) {
        scritti = 0;
        while (scritti < n) {
            w = (int)write(fd, buf + scritti, (unsigned int)(n - scritti));
            if (w <= 0) {
                printf("  %s: scrittura fallita dopo %d byte\n", dst, scritti);
                close(fs); close(fd);
                return -1;
            }
            scritti += w;
        }
    }

    close(fs); close(fd);
    return (n < 0) ? -1 : 0;
}

/* Copia in <radice>/dev i driver che hanno superato la sonda. */
static int installa_driver(const char *radice)
{
    char dev[PERC_MAX], src[PERC_MAX], dst[PERC_MAX];
    int  i, errori = 0, fatti = 0;

    unisci(dev, radice[0] ? radice : "", "dev");

    /* La cartella puo' non esserci: `hwconfig -d` si usa anche da solo, su
     * un volume appena formattato, senza passare da `install`. Se c'e'
     * gia', mkdir fallisce e non e' un problema — l'errore che conta e'
     * quello della copia, che arriva subito dopo e dice il nome del file. */
    mkdir(dev, 0755);

    for (i = 0; i < g_n_drv; i++) {
        if (!g_drv_serve[i]) continue;

        unisci(src, g_catalogo, g_drv[i]);
        unisci(dst, dev, g_drv[i]);

        /* ! IL NOME DI DESTINAZIONE VA IN MINUSCOLO, E SU ext2 NON E' UN
         * DETTAGLIO. Il nome di partenza e' quello che il filesystem
         * sorgente consegna: da una FAT — cioe' dal floppy di avvio, che
         * e' il caso normale di `install` — arriva MAIUSCOLO. Scritto
         * tale e quale su ext2 diventa /dev/KBD.DRV, e il kernel cerca
         * /dev/kbd.drv: sono due file diversi, e quello che cerca non
         * c'e'.
         *
         * Il sintomo non somiglia alla causa. Il disco si avvia, dice
         *
         *     [WARN] Driver 'kbd': '/dev/kbd.drv' non caricato (err=-2)
         *     [WARN] Tastiera: ripiego sull'handler IRQ1 in-kernel
         *
         * e si ritrova senza le frecce, senza la cronologia della shell e
         * senza gfedit — mentre `ls /dev` mostra il driver al suo posto.
         * Si cerca il difetto nel driver, che sta benissimo.
         *
         * E' la stessa regola gia' scritta in testa a bin/install/install.c,
         * che la applica ai propri file: questa copia le era sfuggita
         * perche' e' l'unica che non passa da li'. */
        minuscolo_nome(dst);

        printf("  %s -> %s\n", src, dst);
        if (copia_driver(src, dst) != 0) errori++;
        else fatti++;
    }

    printf("\n%d driver installati in %s", fatti, dev);
    if (errori) printf(", %d falliti", errori);
    printf("\n");
    return errori;
}

static void uso(void)
{
    printf("uso: hwconfig [-n] [punto di montaggio]\n");
    printf("     hwconfig -d [-n] [-s catalogo] [punto di montaggio]\n\n");
    printf("Guarda cosa c'e' nella macchina e scrive kernel.cfg e\n");
    printf("autoexec.sh di conseguenza. Mostra tutto prima di chiedere.\n\n");
    printf("  -n            guarda e basta, non scrive niente\n");
    printf("  -d            sonda i driver e installa in <punto>/dev solo\n");
    printf("                quelli che trovano il proprio hardware\n");
    printf("  -s <dir>      catalogo dei driver (predefinito %s,\n", CATALOGO_PRED);
    printf("                poi /drivers se si e' avviato dal CD)\n");
    printf("  -y            con -d, copia senza chiedere conferma\n");
    printf("  <punto>       configura il sistema installato li' dentro\n");
    printf("                (senza, configura quello da cui sei partito)\n\n");
    printf("I file precedenti finiscono in kernel.cfg.bak e autoexec.sh.bak.\n\n");
    printf("Con -d ogni driver del catalogo viene lanciato con `-i`: sonda\n");
    printf("il proprio hardware ed esce con 0 se serve qui. Chi non risponde\n");
    printf("non viene copiato. Non si scrive kernel.cfg: sono due lavori.\n");
}

int main(int argc, char **argv)
{
    static char cfg[4096];
    static char aut[2048];
    char        radice[PERC_MAX] = "";
    char        p_cfg[PERC_MAX], p_aut[PERC_MAX], boot[PERC_MAX];
    int         solo_guarda = 0, solo_driver = 0, senza_chiedere = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0)      solo_guarda = 1;
        else if (strcmp(argv[i], "-d") == 0) solo_driver = 1;
        else if (strcmp(argv[i], "-y") == 0) senza_chiedere = 1;
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            strncpy(g_catalogo, argv[++i], sizeof(g_catalogo) - 1);
            g_catalogo[sizeof(g_catalogo) - 1] = '\0';
        }
        else if (strcmp(argv[i], "-h") == 0 ||
                 strcmp(argv[i], "-help") == 0 ||
                 strcmp(argv[i], "--help") == 0) { uso(); return 0; }
        else if (argv[i][0] == '-')          { uso(); return 1; }
        else strncpy(radice, argv[i], sizeof(radice) - 1);
    }

    esamina(radice);
    mostra();

    /* ── Modo driver: si sonda e si copia, e si esce ────────────────────
     *
     * Non scrive kernel.cfg: sono due lavori distinti, e chi installa un
     * sistema li vuole in momenti diversi — i driver quando il disco e'
     * appena montato, la configurazione quando ha deciso cosa montare
     * all'avvio. `install` chiama questo modo; l'altro resta a mano. */
    if (solo_driver) {
        int quanti;

        if (cerca_driver() == 0) return 1;
        quanti = sonda_tutti();

        if (solo_guarda) {
            printf("(-n: non ho copiato niente)\n");
            return 0;
        }
        if (quanti == 0) {
            printf("Nessun driver da installare.\n");
            return 0;
        }
        if (radice[0] == '\0') {
            printf("Serve il punto di montaggio del sistema da attrezzare:\n");
            printf("  hwconfig -d /disk\n");
            printf("Senza, si copierebbero i driver sopra a quelli in uso.\n");
            return 1;
        }

        /* ! IL BERSAGLIO SI CONTROLLA PRIMA DI SCRIVERE. Un montaggio
         * fallito non lascia un errore sotto gli occhi di nessuno: lascia
         * un nome che non esiste, e scriverci dentro finisce nella
         * RADICE — che avviando da CD e' in sola lettura, e allora si
         * ottengono tre «filesystem in sola lettura» di fila che parlano
         * del CD mentre il difetto sta nel disco che non si e' montato.
         * Su un sistema installato, dove la radice e' scrivibile,
         * sarebbe andata peggio: i driver copiati sopra a quelli in uso
         * senza che niente lo dicesse. */
        if (access(radice, F_OK) != 0) {
            printf("%s non esiste.\n", radice);
            printf("  E' il punto in cui hai montato il disco da attrezzare.\n");
            printf("  Montalo prima:  mount hd0p1 %s\n", radice);
            return 1;
        }

        /* -y salta la domanda: la usa `install`, che il permesso di
         * scrivere su quel disco se l'e' gia' fatto dare. Chiederlo due
         * volte per la stessa operazione insegna solo a rispondere di si'
         * senza leggere. */
        if (!senza_chiedere) {
            char risposta[16];
            int  n;

            printf("\nCopio in %s/dev. I file con lo stesso nome vengono\n",
                   radice);
            printf("sostituiti.\nProcedo? [si/no] ");

            n = (int)read(0, risposta, sizeof(risposta) - 1);
            if (n < 0) n = 0;
            risposta[n] = '\0';
            if (risposta[0] != 's' && risposta[0] != 'S') {
                printf("Annullato: niente e' stato copiato.\n");
                return 0;
            }
        }
        printf("\n");
        return installa_driver(radice) > 0 ? 1 : 0;
    }

    unisci(boot, radice[0] ? radice : "", "boot");
    unisci(p_cfg, boot, "kernel.cfg");
    unisci(p_aut, boot, "autoexec.sh");

    componi_kernel_cfg(cfg, sizeof(cfg), p_cfg);
    componi_autoexec(aut, sizeof(aut));

    printf("Cosa scriverei\n\n");
    printf("--- %s ---\n%s\n", p_cfg, cfg);
    printf("--- %s ---\n%s\n", p_aut, aut);

    if (solo_guarda) {
        printf("(-n: non ho scritto niente)\n");
        return 0;
    }

    printf("I file di adesso finiscono in .bak, e questi li sostituiscono\n");
    printf("PER INTERO — i commenti che ci sono ora si perdono.\n");
    printf("Si riportano avanti soltanto `login` e `svga`: sono decisioni che\n");
    printf("con l'hardware non c'entrano, e nessuno potrebbe rimetterle.\n\n");
    printf("Procedo? [si/no] ");

    {
        char risposta[16];
        int  n = (int)read(0, risposta, sizeof(risposta) - 1);

        if (n < 0) n = 0;
        risposta[n] = '\0';
        if (risposta[0] != 's' && risposta[0] != 'S') {
            printf("Annullato: niente e' stato scritto.\n");
            return 0;
        }
    }

    printf("\n");
    if (scrivi_con_copia(p_cfg, cfg) != 0) return 1;
    printf("  scritto %s  (il precedente e' in %s.bak)\n", p_cfg, p_cfg);

    if (scrivi_con_copia(p_aut, aut) != 0) return 1;
    printf("  scritto %s  (il precedente e' in %s.bak)\n", p_aut, p_aut);

    printf("\nFatto. Al prossimo avvio la configurazione e' questa.\n");
    return 0;
}

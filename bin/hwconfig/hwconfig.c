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
 * ⚠️ MOSTRA PRIMA, CHIEDE POI, SCRIVE PER ULTIMO
 *
 * E' la stessa forma di `install -a`, e per la stessa ragione: chi
 * risponde deve poter vedere COSA cambia prima di dire di si'. Un
 * programma che riscrive la configurazione di avvio senza mostrare cosa
 * ci mette e' esattamente il programma che un utente inesperto non deve
 * lanciare.
 *
 * ⚠️ IL FILE PRECEDENTE NON SI PERDE. Va in `kernel.cfg.bak` prima che si
 * scriva una riga del nuovo. E' l'unica cosa che rende la proposta
 * accettabile: se la macchina non riparte, il file di prima e' li'
 * accanto e si rimette con `rename`.
 *
 * ⚠️ IL NUOVO FILE E' GENERATO, NON MODIFICATO. Il kernel.cfg che viene
 * con il sistema e' lungo duecento righe di spiegazioni; conservarle
 * vorrebbe dire un parser INI che le rimette a posto, cioe' un programma
 * molto piu' grande di questo e con molti piu' modi di sbagliare. Quello
 * che si scrive qui e' corto, commentato quanto basta, e sostituisce il
 * precedente per intero. Detto in chiaro prima di chiedere.
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"
#include "kbd_proto.h"

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
 * ⚠️ NON VA MESSO IN [mount], e non perche' rompa qualcosa: il kernel se ne
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
 * ⚠️ SENZA QUESTO CONTROLLO UN'ETICHETTA SFORTUNATA ROMPE L'AVVIO. Un
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

/* ⚠️ UN NOME DI PUNTO PER VOLTA, E MAI DUE UGUALI. Due voci con lo stesso
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

            /* ⚠️ SOLO I FILESYSTEM RICONOSCIUTI. Una partizione con
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

/* La scheda di rete: si chiede al servizio PCI se c'e' un dispositivo di
 * classe Ethernet, e basta.
 *
 * ⚠️ NON SI GUARDA QUALE MODELLO SIA, di proposito. La tabella dei
 * modelli e dei driver sta in `netdetect`, e duplicarla qui darebbe due
 * elenchi che divergono al primo driver nuovo. L'autoexec generato
 * chiama `netdetect -c`, che quella tabella ce l'ha: a noi serve sapere
 * SE c'e' una scheda, non quale. */
static void cerca_rete(void)
{
    int           pid = ipc_lookup(PCI_SERVIZIO);
    PciRichiesta  r;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           i;

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

    r.ordinale    = 0;
    r.classe      = PCI_CLASSE_RETE;
    r.sottoclasse = PCI_SOTTO_ETHERNET;
    r.venditore   = PCI_QUALUNQUE;
    r.dispositivo = PCI_QUALUNQUE;

    if (ipc_send(pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0) {
        g_t.rete_ignota = 1;
        return;
    }

    for (i = 0; i < 8; i++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) {
            g_t.rete_ignota = 1;
            return;
        }
        if ((int)meta.sender_pid != pid) continue;
        g_t.rete = (meta.tipo == PCI_MSG_DISPOSITIVO);
        return;
    }
    g_t.rete_ignota = 1;
}

/* La disposizione della tastiera.
 *
 * ⚠️ SI CHIEDE, NON SI DECIDE. hwconfig riscrive kernel.cfg per intero:
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

static void esamina(const char *bersaglio)
{
    memset(&g_t, 0, sizeof(g_t));
    g_t.primo_scrivibile = -1;

    trova_radice(bersaglio);

    g_t.kbd = (access("/dev/kbd.drv", F_OK) == 0);
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

    if (g_t.rete)
        printf("  rete       scheda Ethernet sul bus PCI — si accende all'avvio\n");
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

/* Scrive `testo` in `percorso`, mettendo da parte il file precedente.
 * Ritorna 0, o -1 dicendo perche'. */
static int scrivi_con_copia(const char *percorso, const char *testo)
{
    char bak[PERC_MAX];
    int  fd, n, scritti;

    snprintf(bak, sizeof(bak), "%s.bak", percorso);

    if (access(percorso, F_OK) == 0) {
        /* ⚠️ SI CANCELLA IL .bak VECCHIO PRIMA. `rename` di EX-OS non
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

static void componi_kernel_cfg(char *out, unsigned int max)
{
    char riga[256];
    int  i;

    out[0] = '\0';

    strncat(out,
        "# =============================================================================\n"
        "# kernel.cfg — scritto da `hwconfig`\n"
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

    /* ⚠️ SI RISCRIVE LA DISPOSIZIONE TROVATA, non 'us'. Questo file lo
     * sostituiamo per intero: senza questa riga una macchina configurata
     * in italiano tornerebbe americana dopo un hwconfig, e il programma
     * che serve a togliere fatica avrebbe rotto in silenzio l'unica
     * impostazione che si nota subito digitando. */
    snprintf(riga, sizeof(riga),
             "\n# Disposizione della tastiera: us it fr de es uk.\n"
             "# Si cambia a caldo con `keymap`; `keymap -p` ristampa questa riga.\n"
             "keymap      = %s\n"
             "\n[boot]\nshell       = /bin/sh\n", g_t.keymap);
    strncat(out, riga, max - 1 - strlen(out));

    if (g_t.kbd)
        strncat(out, "modules     = kbd\n", max - 1 - strlen(out));


    strncat(out, "\n[env]\nPATH        = /bin:/dev\nHOME        = /\n"
                 "TERM        = vga\n", max - 1 - strlen(out));

    /* ⚠️ TMPDIR SERVE, e non e' un vezzo: mkstemp e il driver del
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

    if (g_t.kbd) {
        strncat(out,
            "\n# Caricati all'avvio come processi ring3.\n"
            "[modules]\n"
            "kbd         = /dev/kbd.drv\n", max - 1 - strlen(out));
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

    if (g_t.rete) {
        strncat(out,
            "echo Accendo la rete...\n"
            "\n"
            "# L'ordine non e' modificabile: ogni passo serve al successivo.\n"
            "# `netdetect -c` sceglie da solo il driver giusto per la scheda.\n"
            "/dev/pci.drv &\n"
            "netdetect -c\n"
            "/dev/ip.drv &\n"
            "\n"
            "# Un indirizzo dal DHCP, se c'e' un server. Quello che stampa si\n"
            "# vede: e' il risultato, non il comando.\n"
            "dhcp\n"
            "\n"
            "echo Rete pronta. `ipcfg` la mostra, `ping` la prova.\n",
            max - 1 - strlen(out));
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
static void uso(void)
{
    printf("uso: hwconfig [-n] [punto di montaggio]\n\n");
    printf("Guarda cosa c'e' nella macchina e scrive kernel.cfg e\n");
    printf("autoexec.sh di conseguenza. Mostra tutto prima di chiedere.\n\n");
    printf("  -n            guarda e basta, non scrive niente\n");
    printf("  <punto>       configura il sistema installato li' dentro\n");
    printf("                (senza, configura quello da cui sei partito)\n\n");
    printf("I file precedenti finiscono in kernel.cfg.bak e autoexec.sh.bak.\n");
}

int main(int argc, char **argv)
{
    static char cfg[4096];
    static char aut[2048];
    char        radice[PERC_MAX] = "";
    char        p_cfg[PERC_MAX], p_aut[PERC_MAX], boot[PERC_MAX];
    int         solo_guarda = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0)      solo_guarda = 1;
        else if (strcmp(argv[i], "-h") == 0 ||
                 strcmp(argv[i], "-help") == 0 ||
                 strcmp(argv[i], "--help") == 0) { uso(); return 0; }
        else if (argv[i][0] == '-')          { uso(); return 1; }
        else strncpy(radice, argv[i], sizeof(radice) - 1);
    }

    esamina(radice);
    mostra();

    unisci(boot, radice[0] ? radice : "", "boot");
    unisci(p_cfg, boot, "kernel.cfg");
    unisci(p_aut, boot, "autoexec.sh");

    componi_kernel_cfg(cfg, sizeof(cfg));
    componi_autoexec(aut, sizeof(aut));

    printf("Cosa scriverei\n\n");
    printf("--- %s ---\n%s\n", p_cfg, cfg);
    printf("--- %s ---\n%s\n", p_aut, aut);

    if (solo_guarda) {
        printf("(-n: non ho scritto niente)\n");
        return 0;
    }

    printf("I file di adesso finiscono in .bak, e questi li sostituiscono\n");
    printf("PER INTERO — i commenti che ci sono ora si perdono.\n\n");
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
